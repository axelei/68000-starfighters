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
#include "collision.h"
#include "score.h"
#include "sfx.h"
#include "title.h"

int main(bool hardReset)
{
    VDP_setScreenWidth320();
    SPR_init();
    JOY_init();

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
        bullets_init();
        enemies_init();
        powerups_init();
        explosions_init();
        score_init();
        player_init();
        formation_init();

        title_fadeInGame();

        bool gameOverShown = FALSE;
        bool startReleased = FALSE; // debounce: ignore Start until it's been let go once

        while (TRUE)
        {
            u16 joyState = JOY_readJoypad(JOY_1);
            bool gameOver = player_isGameOver();

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

            if (!gameOver)
                collisions_resolve();

            score_hud_update();
            sfx_update();

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

        enemies_hideAll();
        bullets_hideAll();
        powerups_hideAll();
        explosions_hideAll();
        SPR_update();

        VDP_clearTextArea(0, 0, 40, 28);
    } while (TRUE);

    return 0;
}
