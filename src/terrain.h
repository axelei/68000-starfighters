#ifndef TERRAIN_H
#define TERRAIN_H

#include "game.h"

// Height of the (looping) terrain/starfield plane, in pixels -- the plane is
// taller than the screen (SCREEN_H) so its content cycles as it scrolls. 512
// (64 tiles) is the VDP's maximum plane height once the width is set to 64
// tiles (required since the screen itself is 40 tiles wide) -- see
// VDP_setPlaneSize()'s width/height coupling. terrain.c uses the resulting
// slack (64 tile rows of loop vs. ~28 tiles of screen) to reroll whichever
// band has most recently scrolled fully out of view, so the terrain/
// starfield keep varying instead of repeating the same fixed pattern every
// lap -- see fillTerrainBand()/fillStarfieldBand() in terrain_update().
#define TERRAIN_PLANE_H_PX 512

// One slot per (band, anchor) pair -- see BANDS_PER_PLANE/ANCHORS_PER_BAND in
// terrain.c (512/8 / 16 rows-per-band = 4 bands x 5 anchors/row = 20). Unlike
// the old flat "append as generated" list, every slot always exists (even if
// its anchor rolled empty, marked by tileW == 0) so each band owns a fixed,
// never-changing index range that can be safely rerolled in place later.
#define MAX_TERRAIN_CLUMPS 20

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

// Always MAX_TERRAIN_CLUMPS slots -- an empty anchor (didn't roll a clump)
// is represented in place by tileW == 0 rather than being left out of the
// array, so callers (see turret.c) must skip those explicitly.
extern TerrainClump terrainClumps[MAX_TERRAIN_CLUMPS];

// Sets up BG_A (terrain) and BG_B (starfield, parallax) tile layers and
// loads their tilesets. Call once per game.
void terrain_init(void);

// Advances both scroll offsets. Call once per frame.
void terrain_update(void);

// Rerolls one off-screen band of terrain/starfield so the scenery varies
// over a long play session instead of repeating the same fixed loop
// forever. Call sparingly (see formation.c's startWave()) -- rerolling a
// band is a few hundred VDP writes, cheap individually but noticeable if
// done too often.
void terrain_regenerateOffscreenBand(void);

// Current on-screen Y for the top of a clump, given the current scroll
// position. Since the plane loops (TERRAIN_PLANE_H_PX > SCREEN_H), this can
// be negative -- meaning the clump hasn't scrolled into view yet, but will
// soon -- rather than wrapping around to a large positive value.
s16 terrain_clumpScreenY(const TerrainClump *clump);

#endif // TERRAIN_H
