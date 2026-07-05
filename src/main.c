#include <genesis.h>
#include "resources.h"
#include "game.h"
#include "player.h"
#include "bullet.h"
#include "enemy.h"
#include "formation.h"
#include "terrain.h"
#include "powerup.h"
#include "collision.h"
#include "score.h"
#include "sfx.h"

int main(bool hardReset)
{
    VDP_setScreenWidth320();
    SPR_init();
    JOY_init();

    PAL_setPalette(PAL_SHIP,  palette_ship.data,  DMA);
    PAL_setPalette(PAL_ENEMY, palette_enemy.data, DMA);
    PAL_setPalette(PAL_PWR,   palette_pwr.data,   DMA);
    PAL_setPalette(PAL_TERRA, palette_terra.data, DMA);

    terrain_init();
    sfx_init();

    do
    {
        bullets_init();
        enemies_init();
        powerups_init();
        score_init();
        player_init();
        formation_init();

        while (player.alive)
        {
            u16 joyState = JOY_readJoypad(JOY_1);

            player_update(joyState);
            bullets_update();
            enemies_update();
            formation_update();
            terrain_update();
            powerups_update();
            collisions_resolve();
            score_hud_update();
            sfx_update();

            SPR_update();
            SYS_doVBlankProcess();
        }

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
