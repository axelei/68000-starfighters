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

// The plane is split into two fixed halves, each BAND_ROWS (=PLANE_H_TILES/2)
// tile rows tall, so a half can be rerolled on its own without touching the
// other -- see terrain_requestRegen()/terrain_update(). PLANE_H_TILES must be
// even (independent of CLUMP_SPACING, which just governs anchor spacing
// *within* a half).
#define BAND_ROWS        (PLANE_H_TILES / 2)
#define BANDS_PER_PLANE  2
#define ANCHORS_PER_BAND ((PLANE_W_TILES + CLUMP_SPACING - 1) / CLUMP_SPACING)

// Screen height in tile rows -- the visible window's size when checking
// whether a half is currently fully off-screen (see halfIsOffscreen()).
#define VIEW_ROWS (SCREEN_H / 8)

// Set by terrain_requestRegen(); consumed by terrain_update() once a half
// is confirmed fully off-screen (see halfIsOffscreen()) -- regenerating on
// request immediately, without waiting for that, risked rewriting tiles the
// player is currently looking at.
static bool regenPending;

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
    regenPending = FALSE;
}

// True if not one of the VIEW_ROWS rows currently on screen falls within
// half-index `half`'s BAND_ROWS-tall range -- i.e. that half is entirely
// off-screen right now and safe to rewrite. Checked row-by-row (rather than
// worked out as a closed-form interval test) since VIEW_ROWS/BAND_ROWS are
// small enough that this is cheap and there's no risk of getting the
// wraparound arithmetic subtly wrong.
static bool halfIsOffscreen(u16 half, s16 topRow)
{
    s16 halfStart = half * BAND_ROWS;
    s16 halfEnd = halfStart + BAND_ROWS;

    for (s16 i = 0; i < VIEW_ROWS; i++)
    {
        s16 row = (topRow + i) % PLANE_H_TILES;
        if (row < 0)
            row += PLANE_H_TILES;
        if (row >= halfStart && row < halfEnd)
            return FALSE;
    }

    return TRUE;
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

    if (regenPending)
    {
        s16 topRow = ((s32) F16_toInt(terrainScroll) / 8) % PLANE_H_TILES;
        if (topRow < 0)
            topRow += PLANE_H_TILES;

        for (u16 half = 0; half < BANDS_PER_PLANE; half++)
        {
            if (halfIsOffscreen(half, topRow))
            {
                fillTerrainBand(half, half * BAND_ROWS);
                fillStarfieldBand(half * BAND_ROWS);
                regenPending = FALSE;
                break;
            }
        }
    }
}

void terrain_requestRegen(void)
{
    // Rerolling a half costs several hundred VDP_setTileMapXY calls (up to
    // PLANE_W_TILES=64 starfield tiles/row x BAND_ROWS rows, plus terrain
    // clumps) -- cheap as a one-off, but visibly janky if it lands while the
    // player can actually see the half being rewritten. So this doesn't
    // regenerate anything itself: it just flags the request, and
    // terrain_update() carries it out on whichever later frame first finds
    // one of the two halves entirely off-screen (see halfIsOffscreen()) --
    // which happens naturally within at most about half a lap of scrolling.
    regenPending = TRUE;
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
