#include "title.h"
#include "resources.h"

// Logo is 40x12 tiles (see generate_placeholders.py); centered horizontally,
// sat in the upper half of the screen.
#define LOGO_TILE_X 0
#define LOGO_TILE_Y 3

#define PRESS_START_X 14
#define PRESS_START_Y 20

// Credits line, bottom of the screen -- 38 chars, centered across the 40-tile-
// wide screen with a 1-row margin above the bottom edge.
#define CREDITS_TEXT "By Krusher 2026 - Licensed under GPL 3"
#define CREDITS_X 1
#define CREDITS_Y 26

// 60 frames (1s) out + 60 frames (1s) in = 2s total, as requested.
#define FADE_FRAMES REGION_PICK(60, 50) // ~1s -- see game.h's REGION_PICK

// Fixed, dedicated tile range for the logo -- well clear of terrain.c's
// TERRAIN_BASE_TILE.."+32" range. Loaded explicitly (rather than via
// VDP_drawImage's auto-incrementing tile index) and with a CPU transfer
// (rather than DMA) so this doesn't depend on a DMA queue slot being free
// or on VDP_drawImage's internal tile bookkeeping across repeat title
// screens.
#define TITLE_BASE_TILE (TILE_USER_INDEX + 64)

void title_run(void)
{
    // BG_A isn't exclusively ours -- terrain.c leaves real (non-blank)
    // tilemap entries on it from the previous game, which drawing the logo
    // wouldn't otherwise overwrite outside its own footprint.
    VDP_clearPlane(BG_A, TRUE);

    // terrain_update() left BG_A's vertical scroll wherever the previous
    // game's terrain scrolled to; without resetting it, the logo/text are
    // still written at the correct *tile* coordinates but appear shifted
    // (and possibly wrapped) on screen by that leftover scroll amount.
    VDP_setVerticalScroll(BG_A, 0);

    // score_showGameOver() bands the WINDOW's top rows and never resets it
    // (there's no corresponding "hide" call before returning here, unlike
    // the wave announcement) -- left alone, that band is still active and,
    // since it's transparent rather than opaque-filled, hides the logo
    // behind it (revealing BG_B, not BG_A, within the window's band).
    VDP_setWindowVPos(FALSE, 0);

    // score_init() leaves the WINDOW's HPos set to the HUD side panel's
    // column range for the whole game, and it's never reset on the way back
    // here either -- with VPos now off, that HPos alone still keeps the
    // window (and its opaque side-panel background) showing on every row,
    // covering the right part of the logo/screen with the previous game's
    // leftover HUD panel.
    VDP_setWindowHPos(FALSE, 0);

    VDP_drawImageEx(BG_A, &title_image, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, TITLE_BASE_TILE),
                     LOGO_TILE_X, LOGO_TILE_Y, TRUE, FALSE);

    // Draw directly on BG_A rather than through the usual WINDOW text plane:
    // the WINDOW only actually shows where VDP_setWindowHPos/VPos say it
    // should (see main.c), and at title time neither has been configured to
    // anything but its default (zero-width) state, so text drawn to WINDOW
    // here wouldn't be visible at all. BG_A isn't scrolling yet (that only
    // starts once gameplay begins), so drawing text on it directly is safe.
    VDP_setTextPlane(BG_A);
    VDP_drawText("PRESS START", PRESS_START_X, PRESS_START_Y);
    VDP_drawText(CREDITS_TEXT, CREDITS_X, CREDITS_Y);

    // Loaded once here and left running -- see main.c/sfx.c if in-game music
    // is added later, since sfx.c currently writes directly to the PSG from
    // the 68000 side for its SFX (with no Z80 sound driver active, SGDK
    // boots with Z80_DRIVER_NULL) and that stops being true once XGM2 stays
    // loaded into gameplay.
    Z80_loadDriver(Z80_DRIVER_XGM2, TRUE);
    XGM2_play(title_music);

    // Debounce: the Start press that just dismissed the game-over screen
    // (see main.c) may still be physically held here -- without waiting for
    // a release first, that same held press would satisfy this wait
    // instantly, skipping the title screen entirely (a single-frame flash
    // straight through to fade-out and back into a new game).
    while (JOY_readJoypad(JOY_1) & BUTTON_START)
        SYS_doVBlankProcess();
    while (!(JOY_readJoypad(JOY_1) & BUTTON_START))
        SYS_doVBlankProcess();

    // Fade the music out over the same span as the screen's own fade-out
    // below -- XGM2_fadeOutAndStop() is driven by the Z80 in the background
    // (not blocking like PAL_fadeOutAll()'s synchronous call), so the two
    // run concurrently and finish at roughly the same time.
    XGM2_fadeOutAndStop(FADE_FRAMES);

    PAL_fadeOutAll(FADE_FRAMES, FALSE);

    // Restore WINDOW as the text plane for score.c's HUD during gameplay.
    VDP_setTextPlane(WINDOW);
}

void title_fadeInGame(void)
{
    // PAL3 (PAL_BOSS) is a placeholder here -- boss.c's roster (see its
    // BossDef table) now swaps in whichever kind's own palette is active
    // right before that encounter's sprites are ever drawn, so nothing
    // actually reads PAL3 before then; palette_boss_a is just a reasonable
    // default fill for this initial fade-in.
    u16 combined[64];
    memcpy(&combined[0],  palette_player.data,      16 * sizeof(u16));
    memcpy(&combined[16], palette_enemy.data,       16 * sizeof(u16));
    memcpy(&combined[32], palette_environment.data, 16 * sizeof(u16));
    memcpy(&combined[48], palette_boss_a.data,      16 * sizeof(u16));

    PAL_fadeInAll(combined, FADE_FRAMES, FALSE);
}
