#ifndef COLLISION_H
#define COLLISION_H

#include "game.h"

// Resolves all cross-system collisions for the current frame: player
// bullets vs enemies, enemy bullets/enemies vs player, player vs powerups.
void collisions_resolve(void);

#endif // COLLISION_H
