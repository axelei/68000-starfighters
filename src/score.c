#include "score.h"
#include "resources.h"
#include "player.h"

// HUD lives in a static side panel (right edge of the screen, out of the
// playfield -- see PLAY_AREA_X_MAX in game.h) on the WINDOW plane, not the
// top -- see main.c/terrain.c for why WINDOW is used instead of the default
// (scrolling) text plane. The panel is HUD_PANEL_COLS tiles wide; its column
// count doubles as the boundary the WINDOW plane is clipped to, in 2-tile
// units (see VDP_setWindowHPos).
#define SIDE_PANEL_HPOS (HUD_PANEL_COL0 / 2) // 2-tile units, must be even
#define SCREEN_H_TILES  (SCREEN_H / 8)

#define HUD_FILL_TILE (TILE_USER_INDEX + 32) // past terrain(4)/starfield(3) tiles

#define SCORE_HUD_X (HUD_PANEL_COL0 + 1)
#define SCORE_HUD_Y 2
#define LIVES_HUD_X (HUD_PANEL_COL0 + 1)
#define LIVES_HUD_Y 6
#define GAMEOVER_HUD_Y 12

#define POINTS_BEE     100
#define POINTS_SPECIAL 300
#define POINTS_BIG     1000

static u32 score;
static u32 displayedScore = 0xFFFFFFFF; // force first draw
static u8 displayedLives = 0xFF;        // force first draw
static bool tilesetLoaded = FALSE;

// Paints the panel columns solid black on the WINDOW plane using an opaque
// fill tile -- blank (index 0) tiles are transparent on Genesis hardware and
// would let the scrolling terrain/starfield (and any sprite passing behind)
// show through, so plain VDP_drawText/VDP_clearTextArea calls aren't enough.
static void fillPanelBackground(void)
{
    u16 tile = TILE_ATTR_FULL(PAL_SHIP, TRUE, FALSE, FALSE, HUD_FILL_TILE);
    for (u16 y = 0; y < SCREEN_H_TILES; y++)
        for (u16 x = HUD_PANEL_COL0; x < 40; x++)
            VDP_setTileMapXY(WINDOW, tile, x, y);
}

void score_init(void)
{
    score = 0;
    displayedScore = 0xFFFFFFFF;
    displayedLives = 0xFF;

    if (!tilesetLoaded)
    {
        VDP_loadTileSet(&hud_fill_tileset, HUD_FILL_TILE, DMA);
        tilesetLoaded = TRUE;
    }

    // Clip the WINDOW plane back down to the side panel (game-over may have
    // expanded it to the full screen on the previous round).
    VDP_setWindowHPos(TRUE, SIDE_PANEL_HPOS);
    fillPanelBackground();

    VDP_drawText("SCORE", SCORE_HUD_X, SCORE_HUD_Y);
    VDP_drawText("LIVES", LIVES_HUD_X, LIVES_HUD_Y);
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
}

void score_showGameOver(void)
{
    char buf[12];

    // Expand the window to the full screen width so the (centered)
    // game-over text is visible -- the WINDOW plane only displays within
    // its configured column range, which is normally just the side panel.
    VDP_setWindowHPos(FALSE, 20);

    VDP_drawText("GAME OVER", 13, GAMEOVER_HUD_Y);
    VDP_drawText("FINAL SCORE", 12, GAMEOVER_HUD_Y + 2);
    uintToStr(score, buf, 6);
    VDP_drawText(buf, 14, GAMEOVER_HUD_Y + 3);
    VDP_drawText("PRESS START", 12, GAMEOVER_HUD_Y + 5);
}
