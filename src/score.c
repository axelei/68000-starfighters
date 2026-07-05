#include "score.h"
#include "resources.h"

// HUD lives in a static side panel (right edge of the screen) on the
// WINDOW plane, not the top -- see main.c/terrain.c for why WINDOW is used
// instead of the default (scrolling) text plane. The panel is
// SIDE_PANEL_COLS tiles wide; its column start doubles as the boundary the
// WINDOW plane is clipped to, in 2-tile units (see VDP_setWindowHPos).
#define SIDE_PANEL_COLS   8
#define SIDE_PANEL_COL0   (40 - SIDE_PANEL_COLS)
#define SIDE_PANEL_HPOS   (SIDE_PANEL_COL0 / 2) // 2-tile units, must be even

#define SCORE_HUD_X (SIDE_PANEL_COL0 + 1)
#define SCORE_HUD_Y 2
#define GAMEOVER_HUD_Y 12

#define POINTS_BEE     100
#define POINTS_SPECIAL 300

static u32 score;
static u32 displayedScore = 0xFFFFFFFF; // force first draw

void score_init(void)
{
    score = 0;
    displayedScore = 0xFFFFFFFF;

    // Clip the WINDOW plane back down to the side panel (game-over may have
    // expanded it to the full screen on the previous round).
    VDP_setWindowHPos(TRUE, SIDE_PANEL_HPOS);

    VDP_drawText("SCORE", SCORE_HUD_X, SCORE_HUD_Y);
}

void score_addKill(EnemyKind kind)
{
    score += (kind == ENEMY_KIND_SPECIAL) ? POINTS_SPECIAL : POINTS_BEE;
}

u32 score_getValue(void)
{
    return score;
}

void score_hud_update(void)
{
    if (score == displayedScore)
        return;

    displayedScore = score;

    char buf[12];
    uintToStr(score, buf, 6);
    VDP_drawText(buf, SCORE_HUD_X, SCORE_HUD_Y + 1);
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
