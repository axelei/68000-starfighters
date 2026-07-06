#ifndef TERRAIN_H
#define TERRAIN_H

#include "game.h"

// Height of the (looping) terrain/starfield plane, in pixels -- the plane is
// taller than the screen (SCREEN_H) so its content cycles as it scrolls.
#define TERRAIN_PLANE_H_PX 256

#define MAX_TERRAIN_CLUMPS 16

// One scattered terrain clump's footprint, in plane tile coordinates (see
// fillTerrainPlane() in terrain.c). Used by turret.c to place turrets on top
// of an actual clump instead of at an arbitrary position.
typedef struct
{
    s16 tileX;
    s16 tileY;
    u16 tileW;
    u16 tileH;
} TerrainClump;

extern TerrainClump terrainClumps[MAX_TERRAIN_CLUMPS];
extern u16 terrainClumpCount;

// Sets up BG_A (terrain) and BG_B (starfield, parallax) tile layers and
// loads their tilesets. Call once per game.
void terrain_init(void);

// Advances both scroll offsets. Call once per frame.
void terrain_update(void);

// Current on-screen Y for the top of a clump, given the current scroll
// position. Since the plane loops (TERRAIN_PLANE_H_PX > SCREEN_H), this can
// be negative -- meaning the clump hasn't scrolled into view yet, but will
// soon -- rather than wrapping around to a large positive value.
s16 terrain_clumpScreenY(const TerrainClump *clump);

#endif // TERRAIN_H
