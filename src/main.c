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
#include "sfx.h"
#include "title.h"

int main(bool hardReset)
{
    VDP_setScreenWidth320();
    SPR_init();
    JOY_init();

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
        formation_init();

        title_fadeInGame();

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
        SPR_update();

        VDP_clearTextArea(0, 0, 40, 28);
    } while (TRUE);

    return 0;
}
