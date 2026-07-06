#include "terrain.h"
#include "resources.h"

#define PLANE_W_TILES 64
#define PLANE_H_TILES 32

#define TERRAIN_BASE_TILE   TILE_USER_INDEX
#define STARFIELD_BASE_TILE (TILE_USER_INDEX + 16)

// Scroll speeds, fixed-point pixels/frame. Terrain scrolls just a bit
// faster than the starfield, for a subtle parallax effect.
#define STARFIELD_SPEED FIX16(0.4)
#define TERRAIN_SPEED   FIX16(0.55)

static fix16 terrainScroll;
static fix16 starfieldScroll;

#define CLUMP_SPACING  16 // tile grid spacing between candidate clump anchors
#define CLUMP_MAX_SIZE 7  // clumps are randomly 1x1 up to CLUMP_MAX_SIZE^2 tiles

// Leaves most of the plane untouched (defaults to blank tile 0, which is
// transparent and lets the BG_B starfield show through), scattering random-
// sized, randomly-shaped terrain clumps at sparse grid anchors instead of
// covering the whole playfield in solid ground or using uniform blocks.
static void fillTerrainPlane(void)
{
    for (u16 cy = 0; cy < PLANE_H_TILES; cy += CLUMP_SPACING)
    {
        for (u16 cx = 0; cx < PLANE_W_TILES; cx += CLUMP_SPACING)
        {
            if ((random() & 15) > 8) // skip most anchors so clumps stay sparse
                continue;

            u16 w = 1 + (random() % CLUMP_MAX_SIZE);
            u16 h = 1 + (random() % CLUMP_MAX_SIZE);
            u16 ox = cx + (random() % (CLUMP_SPACING - w));
            u16 oy = cy + (random() % (CLUMP_SPACING - h));

            for (u16 dy = 0; dy < h; dy++)
            {
                for (u16 dx = 0; dx < w; dx++)
                {
                    // Randomly skip corner cells so clumps read as
                    // irregular blobs rather than solid rectangles.
                    bool corner = (dx == 0 || dx == w - 1) && (dy == 0 || dy == h - 1);
                    if (corner && (random() & 3) == 0)
                        continue;

                    u16 variant = random() & 3;
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
            // scattered via random() rather than a fixed hash so the pattern
            // isn't a visibly repeating grid.
            u16 roll = random() & 31;
            u16 variant = (roll == 0) ? 1 : (roll == 1 ? 2 : 0);
            u16 tile = TILE_ATTR_FULL(PAL_TERRA, FALSE, FALSE, FALSE, STARFIELD_BASE_TILE + variant);
            VDP_setTileMapXY(BG_B, tile, x, y);
        }
    }
}

void terrain_init(void)
{
    // BG_A isn't exclusively ours -- the title screen's logo (title.c) is
    // drawn on BG_A too, leaving real (non-blank) tilemap entries behind in
    // the rows/columns it used. fillTerrainPlane() only touches its sparse
    // clump cells (unlike fillStarfieldPlane(), which overwrites every BG_B
    // cell), so without this explicit clear, leftover logo tilemap entries
    // would linger and reference whatever tile pattern data now occupies
    // those indices once the terrain tileset is loaded below -- showing up
    // as garbled leftover logo shapes instead of the intended sparse chunks.
    VDP_clearPlane(BG_A, TRUE);

    // CPU transfer (not DMA): these loads happen well outside any vblank-
    // aware loop, so there's no reason to depend on a queued DMA transfer
    // being flushed later -- CPU makes the upload complete synchronously,
    // right here.
    VDP_loadTileSet(&terrain_tileset, TERRAIN_BASE_TILE, CPU);
    VDP_loadTileSet(&starfield_tileset, STARFIELD_BASE_TILE, CPU);

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
