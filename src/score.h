#ifndef SCORE_H
#define SCORE_H

#include "game.h"
#include "enemy.h"

void score_init(void);
void score_addKill(EnemyKind kind);
void score_addTurretKill(void);

// Awarded once when every enemy in an inter-wave formation was shot down --
// see formation.c's beginInterwave()/enemies_waverKillCount().
void score_addInterwavePerfectBonus(void);

// Awarded once, on the killing blow, for defeating a boss -- see boss.c.
void score_addBossKill(void);
void score_hud_update(void);
u32 score_getValue(void);

// Hides the LIVES row's ship icons (see score.c's LIFE_ICON_MAX) -- call
// between rounds alongside enemies_hideAll()/etc. (see main.c).
void score_hideLivesIcons(void);

// Shows/hides a "WAVE N" banner across the top of the screen (see
// formation.c, which calls this when a new wave spawns).
void score_showWaveAnnouncement(u16 waveNumber);
void score_hideWaveAnnouncement(void);

// Shows/hides a "PAUSE" banner, same top-band style as the wave
// announcement -- see main.c's Start-button pause toggle.
void score_showPause(void);
void score_hidePause(void);

// Shows the game-over screen with the final score. Milestone 1 has a single
// life, so this is called directly on player death.
void score_showGameOver(void);

#endif // SCORE_H
