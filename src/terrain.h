#ifndef TERRAIN_H
#define TERRAIN_H

#include "game.h"

// Height of the (looping) terrain/starfield plane, in pixels -- the plane is
// taller than the screen (SCREEN_H) so its content cycles as it scrolls. 512
// (64 tiles) is the VDP's maximum plane height once the width is set to 64
// tiles (required since the screen itself is 40 tiles wide) -- see
// VDP_setPlaneSize()'s width/height coupling. terrain.c splits this into two
// halves and rerolls whichever one is currently entirely off-screen, so the
// terrain/starfield keep varying instead of repeating the same fixed pattern
// every lap -- see terrain_requestRegen().
#define TERRAIN_PLANE_H_PX 512

// One slot per (half, anchor) pair -- see BANDS_PER_PLANE (terrain.c) x
// TERRAIN_MAP_ANCHORS (terrain_generated.h) = 2 x 3 = 6. Unlike a flat
// "append as generated" list, every slot always exists (even if its anchor
// rolled empty, marked by tileW == 0) so each half owns a fixed,
// never-changing index range that can be safely rerolled in place later.
#define MAX_TERRAIN_CLUMPS 6

// One scattered terrain clump's footprint, in plane tile coordinates (see
// applyTerrainMap() in terrain.c). Used by turret.c to place turrets on top
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

// Sets BG_A/BG_B's plane size (see TERRAIN_PLANE_H_PX) -- must precede any
// tilemap/tileset placement on either plane (SGDK recomputes where the plane
// nametables live in VRAM when this changes). Exposed separately from
// terrain_init() so title.c can call it once, early (see main.c), covering
// the very first title screen -- which runs before terrain_init() itself
// ever gets a chance to (see main.c's own comment on that). Safe/cheap to
// call again later from terrain_init() too (same size each time).
void terrain_initPlaneSize(void);

// Sets up BG_A (terrain) and BG_B (starfield, parallax) tile layers and
// loads their tilesets. Call once per game.
void terrain_init(void);

// Loads the starfield tileset and stamps every band of the (looping)
// starfield plane onto BG_B, without touching BG_A or any of terrain.c's own
// regen/clump state. Exposed for title.c's standalone background starfield
// (see its own comment for why it doesn't need the regen machinery
// terrain_init()'s starfield also gets via terrain_update()).
void terrain_initStarfieldOnly(void);

// Advances both scroll offsets. Call once per frame.
void terrain_update(void);

// Requests that terrain/starfield scenery be varied, so a long play session
// doesn't just repeat the same fixed loop forever. Doesn't regenerate
// anything immediately -- see terrain_update() -- so it's safe (and
// intended) to call this at any time, e.g. once per wave change (see
// formation.c's startWave()); it won't visibly disturb what's on screen.
void terrain_requestRegen(void);

// Current on-screen Y for the top of a clump, given the current scroll
// position. Since the plane loops (TERRAIN_PLANE_H_PX > SCREEN_H), this can
// be negative -- meaning the clump hasn't scrolled into view yet, but will
// soon -- rather than wrapping around to a large positive value.
s16 terrain_clumpScreenY(const TerrainClump *clump);

#endif // TERRAIN_H
