#ifndef POWERUP_H
#define POWERUP_H

#include "game.h"
#include "player.h"

typedef struct
{
    bool active;
    Sprite *sprite;
    PowerupType type;
    fix16 x;
    fix16 y;
} Powerup;

extern Powerup powerups[MAX_POWERUPS];

void powerups_init(void);
void powerups_update(void);

// Spawns a drifting powerup capsule at the given position (called on
// special-enemy death). Type is chosen internally.
void powerup_spawnAt(s16 x, s16 y);

AABB powerup_getBounds(const Powerup *p);
void powerup_collect(Powerup *p);

#endif // POWERUP_H
