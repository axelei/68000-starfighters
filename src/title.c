#include "title.h"
#include "resources.h"

// Logo is 40x12 tiles (see generate_placeholders.py); centered horizontally,
// sat in the upper half of the screen.
#define LOGO_TILE_X 0
#define LOGO_TILE_Y 3

#define PRESS_START_X 14
#define PRESS_START_Y 20

// 60 frames (1s) out + 60 frames (1s) in = 2s total, as requested.
#define FADE_FRAMES 60

void title_run(void)
{
    VDP_drawImage(BG_A, &title_image, LOGO_TILE_X, LOGO_TILE_Y);
    VDP_drawText("PRESS START", PRESS_START_X, PRESS_START_Y);

    while (!(JOY_readJoypad(JOY_1) & BUTTON_START))
        SYS_doVBlankProcess();

    PAL_fadeOutAll(FADE_FRAMES, FALSE);

    VDP_clearTextArea(0, 0, 40, 28);
}

void title_fadeInGame(void)
{
    u16 combined[64];
    memcpy(&combined[0],  palette_ship.data,  16 * sizeof(u16));
    memcpy(&combined[16], palette_enemy.data, 16 * sizeof(u16));
    memcpy(&combined[32], palette_pwr.data,   16 * sizeof(u16));
    memcpy(&combined[48], palette_terra.data, 16 * sizeof(u16));

    PAL_fadeInAll(combined, FADE_FRAMES, FALSE);
}
