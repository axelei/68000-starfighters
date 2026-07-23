#ifndef TURRET_H
#define TURRET_H

#include "game.h"
#include "terrain.h"

// At most this many ground turrets can be active on screen at once. See
// game.h's sprite-budget comment -- sized against actual observed
// concurrent sprite usage now that idle turret sprites are released every
// frame, not an arbitrary gameplay-pacing choice.
#define MAX_TURRETS 4

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
    // Which clump this turret spawned on -- only used to keep
    // findApproachingClump() (turret.c) from picking the same clump for a
    // second turret while this one is still riding it in.
    const TerrainClump *clump;
} Turret;

extern Turret turrets[MAX_TURRETS];

void turrets_init(void);
void turrets_update(void);

// Nulls every cached sprite handle -- boot-time only (see main.c and
// bullet.h's bullets_resetHandles(), same reasoning); not part of the
// restart-safe turrets_init() above, and a different case from
// turrets_releaseIdleSprites() below (which reclaims hardware sprite slots
// mid-session, not after a reset).
void turrets_resetHandles(void);

// Hides and deactivates every active turret without releasing its sprite
// (called on player death, so a turret doesn't hang frozen on screen
// through the game-over prompt).
void turrets_hideAll(void);

// Releases (SPR_releaseSprite) every currently-inactive turret's sprite
// handle back to SGDK's pool and nulls it -- called every frame from main.c
// (see game.h's sprite-budget comment) so the sprite-object budget tracks
// how many turrets are actually alive right now rather than the historical
// high-water mark. A turret still active when a boss encounter begins is
// deliberately left alone (see boss.c's boss_begin()) -- it keeps fighting
// into the boss fight and only gives up its slot once it actually dies or
// scrolls off, same as during ordinary gameplay. Safe regardless of timing:
// only touches slots already confirmed inactive. The existing lazy
// `if (t->sprite == NULL) SPR_addSpriteEx(...)` in trySpawn() already
// handles recreating it next time a turret spawns.
void turrets_releaseIdleSprites(void);

AABB turret_getBounds(const Turret *t);

// Applies damage to the turret; kills it (explosion, score, chance of a
// powerup drop) once hp reaches 0. Called by the collision system.
void turret_hit(Turret *t, s16 damage);

#endif // TURRET_H
