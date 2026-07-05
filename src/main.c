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
    VDP_setWindowVPos(FALSE, 28); // full screen height; only the H split varies (see score.c)

    title_run();

    terrain_init();
    sfx_init();

    bool firstRun = TRUE;

    do
    {
        bullets_init();
        enemies_init();
        powerups_init();
        explosions_init();
        score_init();
        player_init();
        formation_init();

        // Only the very first game scene fades in from the title screen's
        // fade-out; subsequent restarts (after a game over) pop in instantly,
        // same as before.
        if (firstRun)
        {
            title_fadeInGame();
            firstRun = FALSE;
        }

        while (!player_isGameOver())
        {
            u16 joyState = JOY_readJoypad(JOY_1);

            player_update(joyState);
            bullets_update();
            enemies_update();
            formation_update();
            terrain_update();
            powerups_update();
            explosions_update();
            collisions_resolve();
            score_hud_update();
            sfx_update();

            SPR_update();
            SYS_doVBlankProcess();
        }

        // Player just died -- hide whatever enemies/bullets/powerups were
        // still on screen so they don't hang there, frozen, through the
        // game-over prompt. SPR_update() must run once more here to flush
        // the visibility change to hardware; the main loop's own call
        // already ran for this frame before the while() condition was
        // re-checked.
        enemies_hideAll();
        bullets_hideAll();
        powerups_hideAll();
        explosions_hideAll();
        SPR_update();

        score_showGameOver();

        // Wait for the player to release Start (if held from before death),
        // then press it again to restart.
        while (JOY_readJoypad(JOY_1) & BUTTON_START)
            SYS_doVBlankProcess();
        while (!(JOY_readJoypad(JOY_1) & BUTTON_START))
            SYS_doVBlankProcess();

        VDP_clearTextArea(0, 0, 40, 28);
    } while (TRUE);

    return 0;
}
