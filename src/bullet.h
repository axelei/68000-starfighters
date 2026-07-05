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
} Bullet;

extern Bullet playerBullets[MAX_PLAYER_BULLETS];
extern Bullet enemyBullets[MAX_ENEMY_BULLETS];

void bullets_init(void);
void bullets_update(void);

// Hides and deactivates every active bullet without releasing its sprite
// (called on player death, so leftover shots don't hang frozen on screen
// through the game-over prompt).
void bullets_hideAll(void);

// Spawns a bullet from the given world position with the given velocity
// (pixels/frame, fixed-point). Returns false if the relevant pool is full.
bool bullet_spawn_player(fix16 x, fix16 y, fix16 vx, fix16 vy);
bool bullet_spawn_enemy(fix16 x, fix16 y, fix16 vx, fix16 vy);

AABB bullet_getBounds(const Bullet *b);
void bullet_deactivate(Bullet *b);

#endif // BULLET_H
