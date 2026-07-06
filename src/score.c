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

// The WINDOW plane's vertical position can only band rows from the top or
// bottom edge (never a floating band in the middle), so to get text reading
// as "the middle of the playfield" the band is anchored at the top but sized
// to reach just past screen-vertical-center (row SCREEN_H_TILES/2 = 14),
// with the text itself placed near the band's bottom edge.
#define GAMEOVER_HUD_Y 8
#define GAMEOVER_BAND_ROWS 18

// Same top-band trick as the game-over screen, but just for the "WAVE N"
// announcement (see formation.c), which only ever shows during the pause
// between waves when no enemies are on screen.
#define WAVE_ANNOUNCE_BAND_ROWS 14
#define WAVE_ANNOUNCE_TEXT_Y    13

#define POINTS_BEE     100
#define POINTS_SPECIAL 300
#define POINTS_BIG     1000
#define POINTS_TURRET  200

static u32 score;
static u32 displayedScore = 0xFFFFFFFF; // force first draw
static u8 displayedLives = 0xFF;        // force first draw
static u16 displayedWave = 0xFFFF;      // force first draw
static bool tilesetLoaded = FALSE;

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

// Resets a WINDOW-plane rectangle to a genuinely blank tile (raw index 0,
// not the font's space glyph -- see banner_font()'s tile 0). Needed before
// each banner draw: VDP_drawText() only touches the cells its string
// actually spans, so without this, a previous draw's leftover glyphs (e.g.
// "WAVE 9" at one X position vs "WAVE 10" at a shifted one, or a stale
// GAME OVER screen from a prior round) would keep showing wherever the new,
// shorter/differently-positioned text doesn't happen to overwrite them.
static void clearWindowRect(u16 x0, u16 y0, u16 x1, u16 y1)
{
    for (u16 y = y0; y < y1; y++)
        for (u16 x = x0; x < x1; x++)
            VDP_setTileMapXY(WINDOW, 0, x, y);
}

void score_init(void)
{
    score = 0;
    displayedScore = 0xFFFFFFFF;
    displayedLives = 0xFF;
    displayedWave = 0xFFFF;

    if (!tilesetLoaded)
    {
        VDP_loadTileSet(&hud_fill_tileset, HUD_FILL_TILE, CPU);
        VDP_loadTileSet(&hud_separator_tileset, HUD_SEPARATOR_TILE, CPU);
        tilesetLoaded = TRUE;
    }

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
}

void score_addKill(EnemyKind kind)
{
    switch (kind)
    {
        case ENEMY_KIND_SPECIAL: score += POINTS_SPECIAL; break;
        case ENEMY_KIND_BIG:     score += POINTS_BIG;     break;
        default:                 score += POINTS_BEE;     break;
    }
}

void score_addTurretKill(void)
{
    score += POINTS_TURRET;
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
    VDP_drawText("FINAL SCORE", 12, GAMEOVER_HUD_Y + 2);
    uintToStr(score, buf, 6);
    VDP_drawText(buf, 14, GAMEOVER_HUD_Y + 3);
    VDP_drawText("WAVES CLEARED", 11, GAMEOVER_HUD_Y + 5);
    uintToStr(formation_wavesCleared(), buf, 2);
    VDP_drawText(buf, 17, GAMEOVER_HUD_Y + 6);
    VDP_drawText("PRESS START", 12, GAMEOVER_HUD_Y + 8);
}
