#include <genesis.h>
#include "resources.h"
#include "game.h"
#include "player.h"
#include "bullet.h"
#include "enemy.h"
#include "formation.h"
#include "terrain.h"
#include "powerup.h"
#include "explosion.h"
#include "turret.h"
#include "boss.h"
#include "collision.h"
#include "score.h"
#include "highscore.h"
#include "sfx.h"
#include "title.h"
#include "options.h"
#include "intro.h"

int main(bool hardReset)
{
    VDP_setScreenWidth320();
    SPR_init();
    JOY_init();

    // Every module below caches its Sprite* handles in static/global vars
    // and reuses them forever across the title/game restart do-while loop
    // (each one's own _init() intentionally leaves them alone -- see e.g.
    // bullet.c's pool_init()). That's safe *within* a single call to main():
    // SPR_init() above only ever runs once before any of those handles are
    // created. But a soft reset (the console's Reset button, not just a
    // fresh game) re-enters main() and re-runs SPR_init() -- which allocates
    // a brand new sprite pool -- while leaving ordinary work RAM (where
    // these static handles live) untouched, unlike VRAM/the VDP. Without
    // this, every cached handle from before the reset is left dangling, and
    // the first restart's "already have a sprite, just reuse it" checks
    // would hand stale pointers straight to the sprite engine -- the
    // intermittent post-reset crashes this fixes. Nulling them here, once,
    // right after SPR_init() and before anything can create a real one,
    // makes every one of those checks correctly take the "create fresh"
    // path exactly once per actual reset (hard or soft), same as if this
    // were truly the first time any of them had ever run.
    bullets_resetHandles();
    enemies_resetHandles();
    explosions_resetHandles();
    powerups_resetHandles();
    turrets_resetHandles();
    score_resetHandles();
    player_resetHandles();

    // Covers the very first title screen (see title_run(), which now draws
    // its own background starfield on BG_B), run before terrain_init() has
    // ever had a chance to set this up this session. Must precede the font
    // load right below -- changing plane size makes SGDK auto-reload its own
    // *default* font, which this call's own comment (see terrain.c) explains
    // needs our font reloaded after, not before, for it to be the one that
    // sticks. Safe/cheap to call again later from terrain_init() too.
    terrain_initPlaneSize();

    // Covers the very first title screen's "PRESS START" (see title_run()),
    // drawn before terrain_init() has run even once this session. Every
    // title screen/round after this one is covered by the reload inside the
    // loop below instead -- see the comment there for why both are needed.
    VDP_loadFont(&banner_font_tileset, CPU);

    // HUD/menu text goes on the WINDOW plane, not the default (BG_A) text
    // plane -- BG_A is continuously scrolled by terrain_update(), which
    // would otherwise drag the score display up the screen along with the
    // terrain and visually mix it with terrain tiles behind it.
    VDP_setTextPlane(WINDOW);

    // Deliberately never call VDP_setWindowVPos(): the VDP combines the
    // window's H and V ranges with OR, not AND, so setting VPos to "full
    // screen height" (as a previous version of this code did) made the
    // window cover the ENTIRE screen at all times regardless of HPos,
    // permanently hiding BG_A behind it (only BG_B showed through the
    // window's blank/transparent gaps). Leaving VPos at its default (0,0,
    // an always-false V condition) means only VDP_setWindowHPos (see
    // score.c) decides where the window shows: a right-side column when
    // HPos is column-restricted, or the whole screen when HPos spans all
    // 40 columns (used for the full-screen game-over text).

    sfx_init();

    // Once per power-on, not per game -- options chosen in the title
    // screen's options scene (see options.c) should persist across replays,
    // not reset back to defaults every time the do-while loop below restarts.
    options_init();

    // Also once per power-on: loads the saved high score/wave from SRAM (or
    // initializes it, if this cart/emulator has never written it before)
    // so it's ready before the first game over ever checks against it.
    highscore_init();

    // Once per power-on, before the very first title screen -- see
    // intro.h's own comment on why this never replays on later returns to
    // the title screen.
    intro_run();

    do
    {
        // Title screen every time we (re)start -- draws the logo over BG_A
        // and blocks until Start is pressed, then fades to black.
        title_run();

        // terrain_init() re-fills BG_A/BG_B from scratch, both to reset the
        // scroll position for the new game and because title_run() just drew
        // the logo over part of BG_A's tilemap.
        terrain_init();

        // Must come after terrain_init(): its VDP_setPlaneSize() call shifts
        // where the map/table area of VRAM starts, and SGDK detects that and
        // automatically reloads its own *default* font (transparent
        // background) at the font's new VRAM location -- silently
        // overwriting whatever font was loaded before, including this one.
        // Loading ours here, after that shift has already happened, makes
        // sure it's the one still in VRAM once gameplay starts. See
        // generate_placeholders.py's banner_font() for why this one has an
        // opaque black background baked into every glyph tile instead of a
        // transparent one.
        VDP_loadFont(&banner_font_tileset, CPU);

        bullets_init();
        enemies_init();
        powerups_init();
        explosions_init();
        turrets_init();
        boss_init();
        score_init();
        player_init();

        title_fadeInGame();

        // Must come after title_fadeInGame(): formation_init() can spawn
        // sprites/palette writes immediately (e.g. boss_begin(), when
        // DEBUG_START_WAVE lands on a boss slot) -- running it before the
        // fade-in let title_fadeInGame()'s own PAL_fadeInAll() clobber
        // whatever it had just set, most visibly stomping boss_begin()'s
        // real per-kind PAL_BOSS palette back to the placeholder default.
        formation_init();

        bool gameOverShown = FALSE;
        bool startReleased = FALSE; // debounce: ignore Start until it's been let go once
        bool paused = FALSE;
        bool startWasDown = FALSE; // edge-detects a fresh Start press for the pause toggle

        while (TRUE)
        {
            u16 joyState = JOY_readJoypad(JOY_1);
            bool gameOver = player_isGameOver();

            bool startDown = (joyState & BUTTON_START) != 0;
            bool startPressed = startDown && !startWasDown;
            startWasDown = startDown;

            // Game-over already has its own Start handling (return to
            // title) below, with its own debounce -- pausing only applies
            // while actually playing. Can't overlap with game-over anyway:
            // the player can't die while paused (collisions_resolve() is
            // skipped below), so the two never need to be true together.
            if (!gameOver && startPressed)
            {
                paused = !paused;
                if (paused)
                    score_showPause();
                else
                    score_hidePause();
            }

            if (!paused)
            {
                // Once the player is out of lives, the scene keeps animating
                // (enemies, terrain, explosions...) behind the "GAME OVER" text
                // rather than freezing, until Start is pressed to return to the
                // title screen.
                if (!gameOver)
                    player_update(joyState);

                bullets_update();
                enemies_update();
                formation_update();
                terrain_update();
                powerups_update();
                explosions_update();
                turrets_update();
                boss_update();

                if (!gameOver)
                    collisions_resolve();

                sfx_update();
            }

            score_hud_update();

            if (gameOver)
            {
                if (!gameOverShown)
                {
                    score_showGameOver();
                    gameOverShown = TRUE;
                    startReleased = !(joyState & BUTTON_START);
                }
                else if (!startReleased)
                {
                    if (!(joyState & BUTTON_START))
                        startReleased = TRUE;
                }
                else if (joyState & BUTTON_START)
                {
                    break;
                }
            }

            SPR_update();
            SYS_doVBlankProcess();
        }

        PAL_fadeOutAll(30, FALSE);

        // Otherwise whatever sound effect was mid-playback (shoot,
        // explosion...) keeps running right into the title screen.
        sfx_stopAll();

        enemies_hideAll();
        bullets_hideAll();
        powerups_hideAll();
        explosions_hideAll();
        turrets_hideAll();
        boss_hideAll();
        score_hideLivesIcons();
        SPR_update();

        VDP_clearTextArea(0, 0, 40, 28);
    } while (TRUE);

    return 0;
}
