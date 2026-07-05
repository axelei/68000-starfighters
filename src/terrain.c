#include "terrain.h"
#include "resources.h"

#define PLANE_W_TILES 64
#define PLANE_H_TILES 32

#define TERRAIN_BASE_TILE   TILE_USER_INDEX
#define STARFIELD_BASE_TILE (TILE_USER_INDEX + 16)

// Scroll speeds, fixed-point pixels/frame. Terrain scrolls at "true" game
// speed; starfield scrolls slower for a parallax effect.
#define TERRAIN_SPEED   FIX16(1.0)
#define STARFIELD_SPEED FIX16(0.4)

static fix16 terrainScroll;
static fix16 starfieldScroll;

#define CLUMP_SPACING 8 // tile grid spacing between candidate clump anchors
#define CLUMP_SIZE    2 // clumps are CLUMP_SIZE x CLUMP_SIZE tiles

// Leaves most of the plane untouched (defaults to blank tile 0, which is
// transparent and lets the BG_B starfield show through), scattering small
// 2x2 terrain clumps at sparse, deterministically-hashed grid anchors
// instead of covering the whole playfield in solid ground.
static void fillTerrainPlane(void)
{
    for (u16 cy = 0; cy < PLANE_H_TILES; cy += CLUMP_SPACING)
    {
        for (u16 cx = 0; cx < PLANE_W_TILES; cx += CLUMP_SPACING)
        {
            u16 hash = (cx * 13 + cy * 7) & 15;
            if (hash > 8) // skip most anchors so clumps stay sparse
                continue;

            u16 ox = cx + (hash & (CLUMP_SPACING - CLUMP_SIZE - 1));
            u16 oy = cy + ((hash >> 2) & (CLUMP_SPACING - CLUMP_SIZE - 1));

            for (u16 dy = 0; dy < CLUMP_SIZE; dy++)
            {
                for (u16 dx = 0; dx < CLUMP_SIZE; dx++)
                {
                    u16 variant = (dx + dy + hash) & 3;
                    u16 tile = TILE_ATTR_FULL(PAL_TERRA, FALSE, FALSE, FALSE, TERRAIN_BASE_TILE + variant);
                    VDP_setTileMapXY(BG_A, tile, ox + dx, oy + dy);
                }
            }
        }
    }
}

static void fillStarfieldPlane(void)
{
    for (u16 y = 0; y < PLANE_H_TILES; y++)
    {
        for (u16 x = 0; x < PLANE_W_TILES; x++)
        {
            // Mostly empty space (tile 0) with sparse star tiles (1,2),
            // using a cheap deterministic hash instead of true randomness
            // so the pattern is stable across rebuilds.
            u16 hash = (x * 7 + y * 13) & 31;
            u16 variant = (hash == 0) ? 1 : (hash == 17 ? 2 : 0);
            u16 tile = TILE_ATTR_FULL(PAL_TERRA, FALSE, FALSE, FALSE, STARFIELD_BASE_TILE + variant);
            VDP_setTileMapXY(BG_B, tile, x, y);
        }
    }
}

void terrain_init(void)
{
    VDP_setPlaneSize(PLANE_W_TILES, PLANE_H_TILES, TRUE);

    VDP_loadTileSet(&terrain_tileset, TERRAIN_BASE_TILE, DMA);
    VDP_loadTileSet(&starfield_tileset, STARFIELD_BASE_TILE, DMA);

    fillTerrainPlane();
    fillStarfieldPlane();

    terrainScroll = 0;
    starfieldScroll = 0;
}

void terrain_update(void)
{
    terrainScroll += TERRAIN_SPEED;
    starfieldScroll += STARFIELD_SPEED;

    // Negated: increasing the raw scroll value moves content up the screen,
    // but a forward-flying vertical shooter should show the world sliding
    // down towards the player, so we scroll in the opposite direction.
    VDP_setVerticalScroll(BG_A, -F16_toInt(terrainScroll));
    VDP_setVerticalScroll(BG_B, -F16_toInt(starfieldScroll));
}
