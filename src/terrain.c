#include "terrain.h"
#include "resources.h"

#define PLANE_W_TILES 64
#define PLANE_H_TILES 64 // TERRAIN_PLANE_H_PX (terrain.h) / 8

TerrainClump terrainClumps[MAX_TERRAIN_CLUMPS];

#define TERRAIN_BASE_TILE   TILE_USER_INDEX
#define STARFIELD_BASE_TILE (TILE_USER_INDEX + 16)

// Scroll speed, fixed-point pixels/frame. Terrain scrolls just a bit
// faster than the starfield, for a subtle parallax effect. (TERRAIN_SPEED
// itself lives in game.h -- turret.c needs it too, to travel in lockstep
// with the terrain clumps it's attached to.)
#define STARFIELD_SPEED FIX16(0.4)

static fix16 terrainScroll;
static fix16 starfieldScroll;

#define CLUMP_SPACING  14 // tile grid spacing between candidate clump anchors
#define CLUMP_MAX_SIZE 10 // clumps are randomly 1x1 up to CLUMP_MAX_SIZE^2 tiles

// The plane is divided into fixed horizontal bands, each BAND_ROWS tile rows
// tall, so a band can be rerolled on its own later without touching any
// other part of the plane -- see terrain_update(). PLANE_H_TILES must divide
// evenly by BAND_ROWS (independent of CLUMP_SPACING, which just governs
// anchor spacing *within* a band).
#define BAND_ROWS        16
#define BANDS_PER_PLANE  (PLANE_H_TILES / BAND_ROWS) // 64/16 = 4
#define ANCHORS_PER_BAND ((PLANE_W_TILES + CLUMP_SPACING - 1) / CLUMP_SPACING)

// Fills (or refills) one band's worth of terrain clumps -- rows
// [bandRow, bandRow + BAND_ROWS) -- scattering candidate clumps at sparse
// grid anchors exactly like the original single-shot generator did, just
// scoped to one band so terrain_update() can redo it later once that band
// has scrolled safely out of view.
static void fillTerrainBand(u16 bandIndex, u16 bandRow)
{
    for (u16 a = 0; a < ANCHORS_PER_BAND; a++)
    {
        u16 cx = a * CLUMP_SPACING;
        TerrainClump *clump = &terrainClumps[bandIndex * ANCHORS_PER_BAND + a];

        // Erase whatever this anchor held before (if anything) -- it may
        // have been a different size/shape, or empty.
        if (clump->tileW > 0)
        {
            for (u16 dy = 0; dy < clump->tileH; dy++)
                for (u16 dx = 0; dx < clump->tileW; dx++)
                    VDP_setTileMapXY(BG_A, 0, clump->tileX + dx, clump->tileY + dy);
        }

        clump->tileW = 0;
        clump->tileH = 0;

        if ((random() & 15) > 8) // skip most anchors so clumps stay sparse
            continue;

        u16 w = 1 + (random() % CLUMP_MAX_SIZE);
        u16 h = 1 + (random() % CLUMP_MAX_SIZE);
        u16 ox = cx + (random() % (CLUMP_SPACING - w));
        u16 oy = bandRow + (random() % (BAND_ROWS - h));

        clump->tileX = ox;
        clump->tileY = oy;
        clump->tileW = w;
        clump->tileH = h;

        for (u16 dy = 0; dy < h; dy++)
        {
            for (u16 dx = 0; dx < w; dx++)
            {
                // Randomly skip corner cells so clumps read as irregular
                // blobs rather than solid rectangles.
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

// Rerolls one band's worth of starfield -- rows [bandRow, bandRow + BAND_ROWS)
// across the full plane width. No clump-tracking needed here (BG_B is purely
// decorative), so unlike fillTerrainBand() this just overwrites every cell.
static void fillStarfieldBand(u16 bandRow)
{
    for (u16 y = bandRow; y < bandRow + BAND_ROWS; y++)
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
    // Must precede any tilemap/tileset setup below -- it recomputes where
    // SGDK places the plane nametables in VRAM (and therefore where
    // TILE_USER_INDEX-based uploads land), and the game.h SCREEN_H tile
    // constant relies on the standard V28 mode this doesn't disturb.
    VDP_setPlaneSize(PLANE_W_TILES, PLANE_H_TILES, TRUE);

    // BG_A isn't exclusively ours -- the title screen's logo (title.c) is
    // drawn on BG_A too, leaving real (non-blank) tilemap entries behind in
    // the rows/columns it used. fillTerrainBand() only touches its sparse
    // clump cells (unlike fillStarfieldBand(), which overwrites every BG_B
    // cell), so without this explicit clear, leftover logo tilemap entries
    // would linger and reference whatever tile pattern data now occupies
    // those indices once the terrain tileset is loaded below -- showing up
    // as garbled leftover logo shapes instead of the intended sparse chunks.
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);

    // CPU transfer (not DMA): these loads happen well outside any vblank-
    // aware loop, so there's no reason to depend on a queued DMA transfer
    // being flushed later -- CPU makes the upload complete synchronously,
    // right here.
    VDP_loadTileSet(&terrain_tileset, TERRAIN_BASE_TILE, CPU);
    VDP_loadTileSet(&starfield_tileset, STARFIELD_BASE_TILE, CPU);

    for (u16 b = 0; b < BANDS_PER_PLANE; b++)
    {
        fillTerrainBand(b, b * BAND_ROWS);
        fillStarfieldBand(b * BAND_ROWS);
    }

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

void terrain_regenerateOffscreenBand(void)
{
    // Rerolling a band costs a few hundred VDP_setTileMapXY calls (up to
    // PLANE_W_TILES=64 starfield tiles/row x BAND_ROWS=16 rows, plus terrain
    // clumps), which takes long enough that doing it every time the scroll
    // crosses a band boundary (as an earlier version of this did) was
    // visibly janky -- a multi-frame hitch every few seconds, however
    // carefully the target band was chosen to stay off-screen. Called only
    // at wave-change boundaries instead (see formation.c's startWave()), so
    // it happens rarely and lands during the "WAVE N" announcement pause
    // rather than live combat.
    //
    // Picks the band that's most recently scrolled fully past the bottom of
    // the screen -- the one immediately behind (lower plane-row index than)
    // whichever band currently sits at the top edge. BAND_ROWS=16 covers
    // more than one full band beyond SCREEN_H_TILES=28's reach in every
    // case (verified by exhaustively checking all four band/offset
    // combinations), so this band is guaranteed to already be off-screen and
    // won't scroll back into view for nearly a full lap.
    s16 topRow = ((s32) F16_toInt(terrainScroll) / 8) % PLANE_H_TILES;
    if (topRow < 0)
        topRow += PLANE_H_TILES;
    s16 bandIndex = topRow / BAND_ROWS;

    u16 target = (bandIndex + BANDS_PER_PLANE - 1) % BANDS_PER_PLANE;
    fillTerrainBand(target, target * BAND_ROWS);
    fillStarfieldBand(target * BAND_ROWS);
}

s16 terrain_clumpScreenY(const TerrainClump *clump)
{
    // The plane loops every TERRAIN_PLANE_H_PX pixels of scroll (matching
    // the sign convention in terrain_update(): the world moves down the
    // screen as terrainScroll increases). Wrap into [0, TERRAIN_PLANE_H_PX),
    // then treat the bottom portion of that range as "still above the
    // screen, about to enter" (a negative Y) rather than a large positive
    // one, so callers can just check "is this about to scroll into view".
    s32 raw = ((s32) clump->tileY * 8 + F16_toInt(terrainScroll)) % TERRAIN_PLANE_H_PX;
    if (raw < 0)
        raw += TERRAIN_PLANE_H_PX;
    if (raw > SCREEN_H)
        raw -= TERRAIN_PLANE_H_PX;
    return (s16) raw;
}
