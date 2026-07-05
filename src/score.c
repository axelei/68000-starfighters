#include "score.h"
#include "resources.h"

#define SCORE_HUD_X 1
#define SCORE_HUD_Y 1
#define GAMEOVER_HUD_Y 12

#define POINTS_BEE     100
#define POINTS_SPECIAL 300

static u32 score;
static u32 displayedScore = 0xFFFFFFFF; // force first draw

void score_init(void)
{
    score = 0;
    displayedScore = 0xFFFFFFFF;
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

    VDP_drawText("GAME OVER", 13, GAMEOVER_HUD_Y);
    VDP_drawText("FINAL SCORE", 12, GAMEOVER_HUD_Y + 2);
    uintToStr(score, buf, 6);
    VDP_drawText(buf, 14, GAMEOVER_HUD_Y + 3);
    VDP_drawText("PRESS START", 12, GAMEOVER_HUD_Y + 5);
}
