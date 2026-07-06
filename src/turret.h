#ifndef TURRET_H
#define TURRET_H

#include "game.h"

// At most this many ground turrets can be active on screen at once.
#define MAX_TURRETS 2

typedef struct
{
    bool active;
    Sprite *sprite;
    fix16 x;
    fix16 y;
    s16 hp;
    u16 fireCooldown;     // frames until the next burst starts
    u8 burstShotsLeft;    // shots remaining in the current burst (0 = not bursting)
    u16 burstTimer;       // frames until the next shot in the current burst
    u16 shootAnimTimer;   // frames remaining showing the "firing" sprite
    u16 flashTimer;       // frames remaining showing the white hit-flash sprite
    bool hasPowerup;      // drops a powerup on death instead of nothing
} Turret;

extern Turret turrets[MAX_TURRETS];

void turrets_init(void);
void turrets_update(void);

// Hides and deactivates every active turret without releasing its sprite
// (called on player death, so a turret doesn't hang frozen on screen
// through the game-over prompt).
void turrets_hideAll(void);

AABB turret_getBounds(const Turret *t);

// Applies damage to the turret; kills it (explosion, score, chance of a
// powerup drop) once hp reaches 0. Called by the collision system.
void turret_hit(Turret *t, s16 damage);

#endif // TURRET_H
