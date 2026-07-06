#ifndef SCORE_H
#define SCORE_H

#include "game.h"
#include "enemy.h"

void score_init(void);
void score_addKill(EnemyKind kind);
void score_addTurretKill(void);
void score_hud_update(void);
u32 score_getValue(void);

// Shows/hides a "WAVE N" banner across the top of the screen (see
// formation.c, which calls this when a new wave spawns).
void score_showWaveAnnouncement(u16 waveNumber);
void score_hideWaveAnnouncement(void);

// Shows the game-over screen with the final score. Milestone 1 has a single
// life, so this is called directly on player death.
void score_showGameOver(void);

#endif // SCORE_H
