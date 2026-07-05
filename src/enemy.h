#ifndef ENEMY_H
#define ENEMY_H

#include "game.h"

typedef enum
{
    ENEMY_KIND_BEE,
    ENEMY_KIND_SPECIAL,
} EnemyKind;

typedef enum
{
    ENEMY_STATE_ENTERING,
    ENEMY_STATE_IN_FORMATION,
} EnemyLifeState;

typedef struct
{
    bool active;
    Sprite *sprite;
    EnemyKind kind;
    EnemyLifeState state;
    fix16 x;
    fix16 y;
    fix16 vx; // entrance velocity, used only while ENTERING
    fix16 vy;
    s16 slotX;
    s16 slotY;
    u16 enterTimer;
    s16 startDelay; // frames to wait, hidden, before entrance begins
} Enemy;

extern Enemy enemies[MAX_ENEMIES];

void enemies_init(void);
void enemies_update(void);

// Spawns an inactive enemy starting at (startX,startY), swooping in to
// (slotX,slotY) over a fixed entrance duration, after waiting `delay` frames.
Enemy *enemy_spawn(EnemyKind kind, s16 startX, s16 startY, s16 slotX, s16 slotY, s16 delay);

AABB enemy_getBounds(const Enemy *e);

// Deactivates the enemy, awards score, and drops a powerup if it's a
// "special" kind. Called by the collision system on a killing hit.
void enemy_kill(Enemy *e);

#endif // ENEMY_H
