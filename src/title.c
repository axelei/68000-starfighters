#include "title.h"
#include "resources.h"
#include "options.h"
#include "bgstarfield.h"

// Logo is 40x12 tiles (see generate_placeholders.py); centered horizontally,
// sat in the upper half of the screen.
#define LOGO_TILE_X 0
#define LOGO_TILE_Y 3

#define PRESS_START_X 14
#define PRESS_START_Y 20

// A+START opens the options scene (see options.c) instead of starting the
// game -- deliberately undocumented on screen, like a cheat code.

// Credits line, bottom of the screen -- 38 chars, centered across the 40-tile-
// wide screen with a 1-row margin above the bottom edge.
#define CREDITS_TEXT "By Krusher 2026 - Licensed under GPL 3"
#define CREDITS_X 1
#define CREDITS_Y 26

// 60 frames (1s) out + 60 frames (1s) in = 2s total, as requested.
#define FADE_FRAMES REGION_PICK(60, 50) // ~1s -- see game.h's REGION_PICK

// Intro raster-wobble: a per-scanline horizontal displacement across just
// the logo's own rows, sine-driven so it rolls rather than just bends, with
// its amplitude decaying linearly from WOBBLE_AMPLITUDE_PX down to 0 over
// WOBBLE_FRAMES -- "a lot, then gradually to no deformation" per spec,
// rather than an abrupt cut once the timer runs out. See playTitleWobble().
#define WOBBLE_FRAMES REGION_PICK(90, 75) // ~1.5s -- see game.h's REGION_PICK
#define WOBBLE_AMPLITUDE_PX 20 // max horizontal displacement, at frame 0
#define WOBBLE_WAVELENGTH_DEG FIX16(14) // degrees of sine phase per screen line -- controls how many crests fit down the logo's height
#define WOBBLE_PHASE_SPEED_DEG FIX16(11) // degrees the wave's phase advances per frame -- the rolling motion, distinct from the amplitude decay
#define LOGO_PX_Y (LOGO_TILE_Y * 8)
#define LOGO_PX_H (12 * 8) // logo is 12 tiles tall, see generate_placeholders.py

// Background starfield (BG_B), drawn behind the logo, that scrolls
// continuously and fades up very slowly while the title screen is shown --
// see bgstarfield.c (shared with intro.c). Unlike gameplay's starfield
// (terrain_update()), this never regenerates (see terrain_requestRegen())
// -- the title screen is never up anywhere near terrain.c's ~40s
// auto-regen interval, so the same two bands just loop seamlessly via the
// plane's hardware wraparound.
#define TITLE_STARFIELD_FADE_FRAMES REGION_PICK(240, 200) // ~4s -- much slower than WOBBLE_FRAMES, reads as ambient rather than the hero animation

// Fixed, dedicated tile range for the logo -- well clear of terrain.c's
// TERRAIN_BASE_TILE.."+32" range. Loaded explicitly (rather than via
// VDP_drawImage's auto-incrementing tile index) and with a CPU transfer
// (rather than DMA) so this doesn't depend on a DMA queue slot being free
// or on VDP_drawImage's internal tile bookkeeping across repeat title
// screens.
#define TITLE_BASE_TILE (TILE_USER_INDEX + 64)

// Logo + prompts -- factored out of title_run() so it can be redrawn after
// returning from the options scene (see options.c), which clears BG_A for
// its own menu on the way in and never restores the logo on the way out.
static void drawTitleScreen(void)
{
    // BG_A isn't exclusively ours -- terrain.c leaves real (non-blank)
    // tilemap entries on it from the previous game, which drawing the logo
    // wouldn't otherwise overwrite outside its own footprint.
    VDP_clearPlane(BG_A, TRUE);

    // loadPal=TRUE: also resets PAL0 back to the title's bright colors --
    // needed on the way back from the options scene, which leaves PAL0
    // holding a dimmed copy for its own shadowed backdrop (see
    // title_drawLogoBackground()/options.c).
    VDP_drawImageEx(BG_A, &title_image, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, TITLE_BASE_TILE),
                     LOGO_TILE_X, LOGO_TILE_Y, TRUE, FALSE);

    VDP_drawText("PRESS START", PRESS_START_X, PRESS_START_Y);
    VDP_drawText(CREDITS_TEXT, CREDITS_X, CREDITS_Y);
}

// Plays the intro wobble once: switches BG_A to per-line horizontal scroll
// for just the logo's row span (LOGO_PX_Y..+LOGO_PX_H), leaving PRESS
// START/the credits line -- both well outside that row range -- untouched,
// then restores plain (whole-plane) scroll mode before returning so nothing
// downstream (there's no other horizontal-scroll user in this codebase, but
// SGDK's own default is plain mode) has to know this ran at all.
static void playTitleWobble(void)
{
    VDP_setScrollingMode(HSCROLL_LINE, VSCROLL_PLANE);

    s16 lineOffsets[LOGO_PX_H];
    fix16 phase = 0;
    bool musicStarted = FALSE;

    for (u16 frame = 0; frame < WOBBLE_FRAMES; frame++)
    {
        fix16 amplitude = F16_mul(FIX16(WOBBLE_AMPLITUDE_PX), FIX16(WOBBLE_FRAMES - frame) / WOBBLE_FRAMES);

        for (u16 y = 0; y < LOGO_PX_H; y++)
        {
            fix16 angle = phase + F16_mul(FIX16(y), WOBBLE_WAVELENGTH_DEG);
            lineOffsets[y] = F16_toInt(F16_mul(amplitude, F16_sin(angle)));
        }

        if (frame == 0)
        {
            // Display is still off here (see title_run()) -- a synchronous
            // CPU transfer so these offsets are actually committed in VRAM
            // before VDP_setEnable(TRUE) below, rather than a DMA transfer
            // that wouldn't flush until this loop's own SYS_doVBlankProcess()
            // call turns it into the very race this is meant to avoid.
            VDP_setHorizontalScrollLine(BG_A, LOGO_PX_Y, lineOffsets, LOGO_PX_H, CPU);
            VDP_setEnable(TRUE);
        }
        else
        {
            VDP_setHorizontalScrollLine(BG_A, LOGO_PX_Y, lineOffsets, LOGO_PX_H, DMA);
        }
        phase += WOBBLE_PHASE_SPEED_DEG;

        // Started here (rather than blocking on Z80_loadDriver(..., TRUE)
        // before this loop even begins) so the wobble itself starts at full
        // deformation on frame one instead of the logo sitting undistorted
        // on screen for however many frames the Z80 driver takes to boot.
        if (!musicStarted && Z80_isDriverReady())
        {
            XGM2_play(title_music);
            musicStarted = TRUE;
        }

        bgstarfield_update();
        SYS_doVBlankProcess();
    }

    // Driver boot is normally well under WOBBLE_FRAMES long, but fall back
    // to a blocking wait here just in case it wasn't ready in time -- this
    // screen must never proceed with the music silently never started.
    if (!musicStarted)
    {
        while (!Z80_isDriverReady())
            SYS_doVBlankProcess();
        XGM2_play(title_music);
    }

    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(BG_A, 0);
}

void title_drawLogoBackground(void)
{
    VDP_drawImageEx(BG_A, &title_image, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, TITLE_BASE_TILE),
                     LOGO_TILE_X, LOGO_TILE_Y, FALSE, FALSE);
}

void title_run(void)
{
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

    // Draw directly on BG_A rather than through the usual WINDOW text plane:
    // the WINDOW only actually shows where VDP_setWindowHPos/VPos say it
    // should (see main.c), and at title time neither has been configured to
    // anything but its default (zero-width) state, so text drawn to WINDOW
    // here wouldn't be visible at all. BG_A isn't scrolling yet (that only
    // starts once gameplay begins), so drawing text on it directly is safe.
    VDP_setTextPlane(BG_A);

    // Blanked while the logo is drawn (a CPU, not DMA, tile+tilemap
    // transfer -- see drawTitleScreen()/title_image's declaration -- slow
    // enough to be visibly mid-draw for a frame or more) and while the
    // wobble's first, fully-distorted scroll offsets are computed, so the
    // very first frame the player actually sees already has the logo at
    // full deformation instead of showing it flat first. Re-enabled inside
    // playTitleWobble() once that first frame's state is fully committed.
    VDP_setEnable(FALSE);
    drawTitleScreen();
    bgstarfield_start(TITLE_STARFIELD_FADE_FRAMES);

    // Loaded once here and left running through gameplay (see music.c) --
    // safe alongside sfx.c's own direct 68000-side PSG writes because every
    // XGM2 track in this game (see resources.res) is pure YM2612 FM data,
    // never touching the PSG chip sfx.c uses. Non-blocking load
    // (waitReady=FALSE): the driver takes several frames to boot, and
    // waiting for it here -- before playTitleWobble() ever runs -- was
    // showing the logo undistorted for those frames instead of starting the
    // wobble at full deformation right away. playTitleWobble() itself polls
    // Z80_isDriverReady() and starts the music as soon as it's up, once
    // wobble is already underway. In practice this call is already a no-op
    // by the time it runs on the very first title screen of a session --
    // intro_run() (see intro.c) loads the same driver first, blocking,
    // before title_run() ever gets here.
    XGM2_loadDriver(FALSE);

    playTitleWobble();

    // Debounce: the Start press that just dismissed the game-over screen
    // (see main.c) may still be physically held here -- without waiting for
    // a release first, that same held press would satisfy this wait
    // instantly, skipping the title screen entirely (a single-frame flash
    // straight through to fade-out and back into a new game).
    while (JOY_readJoypad(JOY_1) & BUTTON_START)
    {
        bgstarfield_update();
        SYS_doVBlankProcess();
    }

    // Wait for a fresh Start press -- if A is held at that moment, open the
    // options scene instead of falling through to the fade-in-game below.
    // options_run() takes over BG_A for its own menu and doesn't restore the
    // logo on the way out, so it's redrawn here before resuming this wait
    // (with the same release-first debounce, since A+START is still held
    // going into that redraw).
    while (TRUE)
    {
        bgstarfield_update();
        SYS_doVBlankProcess();
        u16 joy = JOY_readJoypad(JOY_1);
        if (!(joy & BUTTON_START))
            continue;

        if (joy & BUTTON_A)
        {
            options_run();
            drawTitleScreen();
            while (JOY_readJoypad(JOY_1) & BUTTON_START)
            {
                bgstarfield_update();
                SYS_doVBlankProcess();
            }
        }
        else
        {
            break;
        }
    }

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
