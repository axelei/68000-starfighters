#ifndef ENEMY_H
#define ENEMY_H

#include "game.h"

typedef enum
{
    ENEMY_KIND_BEE,
    ENEMY_KIND_SPECIAL,
    ENEMY_KIND_BIG,
} EnemyKind;

typedef enum
{
    ENEMY_STATE_ENTERING,
    ENEMY_STATE_IN_FORMATION,
    ENEMY_STATE_DIVING_OUT, // BEE/SPECIAL only: diving down and off the bottom of the screen
} EnemyLifeState;

typedef struct
{
    bool active;
    Sprite *sprite;
    EnemyKind kind;
    EnemyLifeState state;
    fix16 x;
    fix16 y;
    fix16 vx; // path velocity, used while ENTERING/DIVING_OUT
    fix16 vy;
    s16 slotX;
    s16 slotY;
    u16 enterTimer; // frame counter within the current ENTERING/DIVING_* path
    s16 startDelay; // frames to wait, hidden, before entrance begins
    s16 hp;
    s16 maxHp;
    u16 flashTimer;    // frames remaining showing the white hit-flash sprite frame
    u16 diveCooldown;  // BEE/SPECIAL only: frames until eligible to dive again
    u16 fireCooldown;  // BIG only: frames until its next shot
    bool diving;       // BEE/SPECIAL only: away from its slot for a dive (counts against MAX_CONCURRENT_DIVERS)
} Enemy;

extern Enemy enemies[MAX_ENEMIES];

void enemies_init(void);
void enemies_update(void);

// Hides and deactivates every active enemy without releasing its sprite,
// awarding no score/dropping no powerup (called on player death, so the
// formation doesn't hang frozen on screen through the game-over prompt).
void enemies_hideAll(void);

// Sprite pixel size for a given kind (16x16 for BEE/SPECIAL, 32x32 for BIG).
u16 enemy_widthForKind(EnemyKind kind);
u16 enemy_heightForKind(EnemyKind kind);

// Spawns an inactive enemy starting at (startX,startY), swooping in to
// (slotX,slotY) over a fixed entrance duration, after waiting `delay` frames.
Enemy *enemy_spawn(EnemyKind kind, s16 startX, s16 startY, s16 slotX, s16 slotY, s16 delay);

AABB enemy_getBounds(const Enemy *e);

// Applies damage to the enemy, flashing it white for a few frames; kills it
// (see enemy_kill) once hp reaches 0. Called by the collision system.
void enemy_hit(Enemy *e, s16 damage);

// Deactivates the enemy, awards score, and drops a powerup if it's a
// "special" kind.
void enemy_kill(Enemy *e);

#endif // ENEMY_H
