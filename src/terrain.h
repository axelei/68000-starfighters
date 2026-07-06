#ifndef TERRAIN_H
#define TERRAIN_H

#include "game.h"

// Sets up BG_A (terrain) and BG_B (starfield, parallax) tile layers and
// loads their tilesets. Call once per game.
void terrain_init(void);

// Advances both scroll offsets. Call once per frame.
void terrain_update(void);

#endif // TERRAIN_H
