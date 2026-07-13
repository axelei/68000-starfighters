#ifndef BULLET_H
#define BULLET_H

#include "game.h"

typedef struct
{
    bool active;
    Sprite *sprite;
    fix16 x;
    fix16 y;
    fix16 vx;
    fix16 vy;
    bool isHoming;     // boss's homing bullet only (see bullet_spawn_enemy_homing()) --
                       // FALSE/unused for every ordinary player/enemy bullet
    s8 hp;             // isHoming only: destructible by player bullets (see bullet_hitHoming())
    u16 retargetTimer; // isHoming only: frames until its next one-shot re-aim at the player
    u16 flashTimer;    // isHoming only: frames remaining showing its hit-flash frame
} Bullet;

extern Bullet playerBullets[MAX_PLAYER_BULLETS];
extern Bullet enemyBullets[MAX_ENEMY_BULLETS];

void bullets_init(void);
void bullets_update(void);

// Nulls every cached sprite handle in both pools -- call once at boot (see
// main.c), right after SPR_init() and before the title/game restart loop's
// bullets_init() calls start reusing them. SPR_init() invalidates any
// handle that was still around from before a soft reset: a reset doesn't
// clear regular work RAM (only the VDP/VRAM), so without this a soft
// reset's first restart would reuse a now-dangling Sprite* left over from
// before it.
void bullets_resetHandles(void);

// Hides and deactivates every active bullet without releasing its sprite
// (called on player death, so leftover shots don't hang frozen on screen
// through the game-over prompt).
void bullets_hideAll(void);

// Spawns a bullet from the given world position with the given velocity
// (pixels/frame, fixed-point). Returns false if the relevant pool is full.
bool bullet_spawn_player(fix16 x, fix16 y, fix16 vx, fix16 vy);
bool bullet_spawn_enemy(fix16 x, fix16 y, fix16 vx, fix16 vy);

// Boss-only: a slower, destructible bullet that periodically re-aims itself
// at the player (see bullets_update()'s isHoming branch) instead of flying a
// straight line. Spawns into the same enemyBullets pool as bullet_spawn_enemy()
// (isHoming distinguishes it), so it doesn't cost a new sprite-object pool.
bool bullet_spawn_enemy_homing(fix16 x, fix16 y, fix16 vx, fix16 vy);

// How many homing bullets are currently active -- boss.c caps how many it
// keeps alive at once (see HOMING_MAX_ALIVE there) rather than letting them
// pile up if the player doesn't shoot them down quickly.
u16 bullet_countActiveHoming(void);

// Deactivates every currently active homing bullet, silently (no explosion,
// no score) -- called when the boss that fired them is defeated (see
// boss.c's triggerDeath()), so its lingering threats don't outlive it.
void bullet_clearHoming(void);

// Applies damage to a homing bullet (from a player bullet hit -- see
// collision.c), flashing it briefly; deactivates it (with an explosion,
// no score) once hp reaches 0. No-op if the bullet isn't actually homing.
void bullet_hitHoming(Bullet *b, s16 damage);

AABB bullet_getBounds(const Bullet *b);
void bullet_deactivate(Bullet *b);

#endif // BULLET_H
