#include "score.h"
#include "resources.h"
#include "player.h"
#include "formation.h"
#include <string.h>

// HUD lives in a static side panel (right edge of the screen, out of the
// playfield -- see PLAY_AREA_X_MAX in game.h) on the WINDOW plane, not the
// top -- see main.c/terrain.c for why WINDOW is used instead of the default
// (scrolling) text plane. The panel is HUD_PANEL_COLS tiles wide; its column
// count doubles as the boundary the WINDOW plane is clipped to, in 2-tile
// units (see VDP_setWindowHPos).
#define SIDE_PANEL_HPOS (HUD_PANEL_COL0 / 2) // 2-tile units, must be even
#define SCREEN_H_TILES  (SCREEN_H / 8)

#define HUD_FILL_TILE      (TILE_USER_INDEX + 32) // past terrain(4)/starfield(3) tiles
#define HUD_SEPARATOR_TILE (TILE_USER_INDEX + 33)

#define SCORE_HUD_X (HUD_PANEL_COL0 + 1)
#define SCORE_HUD_Y 2
#define LIVES_HUD_X (HUD_PANEL_COL0 + 1)
#define LIVES_HUD_Y 6
#define WAVE_HUD_X  (HUD_PANEL_COL0 + 1)
#define WAVE_HUD_Y  10
#define TIME_HUD_X  (HUD_PANEL_COL0 + 1)
#define TIME_HUD_Y  14

// The WINDOW plane's vertical position can only band full rows from the top
// or bottom edge -- never an arbitrary rectangle, and never a subset of rows
// with gaps in it. So every line here is packed onto fully consecutive rows
// starting at row 0, with no blank spacer rows between them: the band then
// exactly equals the rows that actually have text on them, with nothing
// above or below it hidden unnecessarily.
#define GAMEOVER_HUD_Y 0
#define GAMEOVER_BAND_ROWS 6 // rows 0..5, one per line below, no gaps

// Same top-band trick as the game-over screen, but just for the "WAVE N"
// announcement (see formation.c), which only ever shows during the pause
// between waves when no enemies are on screen.
#define WAVE_ANNOUNCE_TEXT_Y    0
#define WAVE_ANNOUNCE_BAND_ROWS 1 // just row 0 -- the single line of text

#define POINTS_BEE     100
#define POINTS_SPECIAL 300
#define POINTS_BIG     1000
#define POINTS_TURRET  200
#define POINTS_WAVER   150 // ENEMY_KIND_WAVER_A/B/C -- same for all 3, they only differ in path

// Awarded once per inter-wave formation if every waver in it was shot down
// (see formation.c's beginInterwave()/enemies_waverKillCount()).
#define INTERWAVE_PERFECT_BONUS 1000

static u32 score;
static u32 displayedScore = 0xFFFFFFFF; // force first draw
static u8 displayedLives = 0xFF;        // force first draw
static u16 displayedWave = 0xFFFF;      // force first draw
static u16 displayedTime = 0xFFFF;      // force first draw

// Paints a WINDOW-plane rectangle solid black using an opaque fill tile --
// blank (index 0) tiles are transparent on Genesis hardware and would let
// the scrolling terrain/starfield (and any sprite passing behind) show
// through, so plain VDP_drawText/VDP_clearTextArea calls aren't enough.
static void fillWindowRect(u16 x0, u16 y0, u16 x1, u16 y1)
{
    u16 tile = TILE_ATTR_FULL(PAL_SHIP, TRUE, FALSE, FALSE, HUD_FILL_TILE);
    for (u16 y = y0; y < y1; y++)
        for (u16 x = x0; x < x1; x++)
            VDP_setTileMapXY(WINDOW, tile, x, y);
}

// Resets a WINDOW-plane rectangle to the opaque fill tile before drawing new
// banner text into it. Needed for two reasons: (1) VDP_drawText() only
// touches the cells its string actually spans, so without clearing first, a
// previous draw's leftover glyphs (e.g. "WAVE 9" at one X position vs
// "WAVE 10" at a shifted one, or a stale GAME OVER screen from a prior
// round) would keep showing wherever the new, shorter/differently-positioned
// text doesn't happen to overwrite; and (2) centered text never spans the
// *entire* banded row, so the untouched columns left and right of it need to
// be solid, not a raw blank (index 0) tile -- blank tiles are transparent on
// Genesis hardware and would let sprites/starfield show through the gaps
// beside the text instead of the intended solid backing.
static void clearWindowRect(u16 x0, u16 y0, u16 x1, u16 y1)
{
    fillWindowRect(x0, y0, x1, y1);
}

void score_init(void)
{
    score = 0;
    displayedScore = 0xFFFFFFFF;
    displayedLives = 0xFF;
    displayedWave = 0xFFFF;

    // Reloaded every time (no "already loaded" guard) -- a soft reset
    // (console reset button) retains RAM/globals but the VDP's own reset
    // clears VRAM, so a one-time guard would stay tripped across the reset
    // and skip the re-upload, leaving these tiles missing on screen even
    // though the C state looks fine. Fixed tile indices make this idempotent.
    VDP_loadTileSet(&hud_fill_tileset, HUD_FILL_TILE, CPU);
    VDP_loadTileSet(&hud_separator_tileset, HUD_SEPARATOR_TILE, CPU);

    // Clip the WINDOW plane back down to the side panel (game-over may have
    // banded it to the top rows on the previous round).
    VDP_setWindowHPos(TRUE, SIDE_PANEL_HPOS);
    VDP_setWindowVPos(FALSE, 0);

    // The panel's leftmost column is a red divider line; the rest is the
    // opaque black background (text starts one column further in, at
    // HUD_PANEL_COL0 + 1, so it never overlaps the line).
    u16 separatorTile = TILE_ATTR_FULL(PAL_SHIP, TRUE, FALSE, FALSE, HUD_SEPARATOR_TILE);
    for (u16 y = 0; y < SCREEN_H_TILES; y++)
        VDP_setTileMapXY(WINDOW, separatorTile, HUD_PANEL_COL0, y);
    fillWindowRect(HUD_PANEL_COL0 + 1, 0, 40, SCREEN_H_TILES);

    VDP_drawText("SCORE", SCORE_HUD_X, SCORE_HUD_Y);
    VDP_drawText("LIVES", LIVES_HUD_X, LIVES_HUD_Y);
    VDP_drawText("WAVE", WAVE_HUD_X, WAVE_HUD_Y);
    VDP_drawText("TIME", TIME_HUD_X, TIME_HUD_Y);
}

void score_addKill(EnemyKind kind)
{
    switch (kind)
    {
        case ENEMY_KIND_SPECIAL: score += POINTS_SPECIAL; break;
        case ENEMY_KIND_BIG:     score += POINTS_BIG;     break;
        case ENEMY_KIND_WAVER_A:
        case ENEMY_KIND_WAVER_B:
        case ENEMY_KIND_WAVER_C: score += POINTS_WAVER;   break;
        default:                 score += POINTS_BEE;     break;
    }
}

void score_addTurretKill(void)
{
    score += POINTS_TURRET;
}

void score_addInterwavePerfectBonus(void)
{
    score += INTERWAVE_PERFECT_BONUS;
}

u32 score_getValue(void)
{
    return score;
}

void score_hud_update(void)
{
    if (score != displayedScore)
    {
        displayedScore = score;

        char buf[12];
        uintToStr(score, buf, 6);
        VDP_drawText(buf, SCORE_HUD_X, SCORE_HUD_Y + 1);
    }

    if (player.lives != displayedLives)
    {
        displayedLives = player.lives;

        char buf[4];
        uintToStr(displayedLives, buf, 1);
        VDP_drawText(buf, LIVES_HUD_X, LIVES_HUD_Y + 1);
    }

    // 1-indexed: the wave currently in play, not the (0-based) cleared
    // count -- reads naturally as "WAVE 1" from the very start of a game.
    u16 waveNumber = formation_wavesCleared() + 1;
    if (waveNumber != displayedWave)
    {
        displayedWave = waveNumber;

        char buf[4];
        uintToStr(displayedWave, buf, 2);
        VDP_drawText(buf, WAVE_HUD_X, WAVE_HUD_Y + 1);
    }

    u16 secondsLeft = formation_waveSecondsLeft();
    if (secondsLeft != displayedTime)
    {
        displayedTime = secondsLeft;

        char buf[4];
        uintToStr(displayedTime, buf, 3);
        VDP_drawText(buf, TIME_HUD_X, TIME_HUD_Y + 1);
    }
}

void score_showWaveAnnouncement(u16 waveNumber)
{
    // Bands the WINDOW's top rows across the full width (same OR-with-HPos
    // trick as score_showGameOver()). No background rectangle needed here --
    // the font itself (see main.c's VDP_loadFont() call) carries an opaque
    // black background in every glyph tile now.
    VDP_setWindowVPos(FALSE, WAVE_ANNOUNCE_BAND_ROWS);
    clearWindowRect(0, 0, HUD_PANEL_COL0, WAVE_ANNOUNCE_BAND_ROWS);

    char text[16] = "WAVE ";
    char num[4];
    uintToStr(waveNumber, num, 1);
    strcat(text, num);

    // Centered within the play area (cols 0..HUD_PANEL_COL0), not the full
    // 40-column screen -- the side panel reads as a separate strip.
    u16 x = (HUD_PANEL_COL0 - strlen(text)) / 2;
    VDP_drawText(text, x, WAVE_ANNOUNCE_TEXT_Y);
}

void score_hideWaveAnnouncement(void)
{
    VDP_setWindowVPos(FALSE, 0);
}

void score_showGameOver(void)
{
    char buf[12];

    // Band the WINDOW plane's top rows (in addition to -- not instead of --
    // the side panel's HPos, still active): the VDP ORs the window's H and V
    // conditions, so this shows the window across the *full width* for just
    // these top rows (game-over text) while leaving the rest of the screen
    // (terrain, enemies, etc.) still visible underneath, rather than
    // covering the entire screen the way expanding HPos to full width would.
    // No background rectangle needed here -- the font itself (see main.c's
    // VDP_loadFont() call) carries an opaque black background in every
    // glyph tile now.
    VDP_setWindowVPos(FALSE, GAMEOVER_BAND_ROWS);
    clearWindowRect(0, 0, HUD_PANEL_COL0, GAMEOVER_BAND_ROWS);

    VDP_drawText("GAME OVER", 13, GAMEOVER_HUD_Y);
    VDP_drawText("FINAL SCORE", 12, GAMEOVER_HUD_Y + 1);
    uintToStr(score, buf, 6);
    VDP_drawText(buf, 14, GAMEOVER_HUD_Y + 2);
    VDP_drawText("WAVES CLEARED", 11, GAMEOVER_HUD_Y + 3);
    uintToStr(formation_wavesCleared(), buf, 2);
    VDP_drawText(buf, 17, GAMEOVER_HUD_Y + 4);
    VDP_drawText("PRESS START", 12, GAMEOVER_HUD_Y + 5);
}
