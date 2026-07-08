#include "terrain.h"
#include "resources.h"
#include "terrain_generated.h"

#define PLANE_W_TILES 64
#define PLANE_H_TILES 64 // TERRAIN_PLANE_H_PX (terrain.h) / 8

TerrainClump terrainClumps[MAX_TERRAIN_CLUMPS];

#define TERRAIN_BASE_TILE   TILE_USER_INDEX
#define STARFIELD_BASE_TILE (TILE_USER_INDEX + 16)

// Scroll speed, fixed-point pixels/frame. Terrain scrolls just a bit
// faster than the starfield, for a subtle parallax effect. (TERRAIN_SPEED
// itself lives in game.h -- turret.c needs it too, to travel in lockstep
// with the terrain clumps it's attached to.) PAL value is NTSC * 1.2 --
// see game.h's REGION_PICK.
#define STARFIELD_SPEED REGION_PICK(FIX16(0.4), FIX16(0.48))

static fix16 terrainScroll;
static fix16 starfieldScroll;

// The plane is split into two fixed halves, each BAND_ROWS tile rows tall,
// so a half can be rerolled on its own without touching the other -- see
// terrain_requestRegen()/terrain_update(). BAND_ROWS/ANCHORS_PER_BAND come
// from terrain_generated.h (res/generate_terrain.py) -- that's also where
// the actual clump/star shapes now live; this file just picks one of the
// pregenerated variants and stamps it, instead of rolling shapes itself.
#define BAND_ROWS        TERRAIN_MAP_BAND_ROWS
#define BANDS_PER_PLANE  2
#define ANCHORS_PER_BAND TERRAIN_MAP_ANCHORS

// Screen height in tile rows -- the visible window's size when checking
// whether a half is currently fully off-screen (see halfIsOffscreen()).
#define VIEW_ROWS (SCREEN_H / 8)

// Set by terrain_requestRegen(); consumed by terrain_update() once a half
// is confirmed fully off-screen (see halfIsOffscreen()) -- regenerating on
// request immediately, without waiting for that, risked rewriting tiles the
// player is currently looking at. Split into separate terrain/starfield
// flags (rather than one shared one) because BG_A and BG_B scroll at
// different speeds (TERRAIN_SPEED vs STARFIELD_SPEED) -- a half that's
// off-screen for one plane's current scroll position isn't necessarily
// off-screen for the other's, so they need independent "is it safe yet"
// checks rather than being gated on the same scroll position.
static bool terrainRegenPending;
static bool starfieldRegenPending;

// Also self-requests a regen every AUTO_REGEN_FRAMES, independent of
// formation.c's wave-change trigger -- a long-running wave (or a player
// stuck replaying the same one) would otherwise never see any variety.
#define AUTO_REGEN_FRAMES REGION_PICK(40 * 60, 40 * 50) // ~40s
static u16 autoRegenTimer;

// Stamps one of the pregenerated terrain variants (terrain_generated.h) onto
// one band's rows [bandRow, bandRow + BAND_ROWS). Only TERRAIN_MAP_COLS
// columns are ever touched -- the plane is wider than the screen (see
// PLANE_W_TILES) purely to satisfy VDP_setPlaneSize()'s hardware constraint,
// so there's nothing to gain from generating or writing columns that never
// scroll into view.
static void applyTerrainMap(u16 bandIndex, u16 bandRow)
{
    const TerrainMap *map = &terrainMaps[random() % TERRAIN_MAP_COUNT];

    // Blank the whole band first (one DMA-backed call) -- clumps only cover
    // part of it, and the rest needs to revert to "no clump here" from
    // whatever the previous variant left behind.
    VDP_fillTileMapRect(BG_A, 0, 0, bandRow, TERRAIN_MAP_COLS, BAND_ROWS);

    for (u16 a = 0; a < ANCHORS_PER_BAND; a++)
    {
        const TerrainMapClump *mc = &map->clumps[a];
        TerrainClump *clump = &terrainClumps[bandIndex * ANCHORS_PER_BAND + a];

        if (mc->tileW == 0) // empty anchor -- see terrain_generated.h
        {
            clump->tileW = 0;
            clump->tileH = 0;
            continue;
        }

        clump->tileX = mc->tileX;
        clump->tileY = bandRow + mc->tileY;
        clump->tileW = mc->tileW;
        clump->tileH = mc->tileH;

        for (u16 dy = 0; dy < mc->tileH; dy++)
        {
            for (u16 dx = 0; dx < mc->tileW; dx++)
            {
                u8 cell = mc->cells[(dy * mc->tileW) + dx];
                if (cell == 0) // gap -- irregular blob edge (see the generator)
                    continue;

                u16 tile = TILE_ATTR_FULL(PAL_ENVIRONMENT, FALSE, FALSE, FALSE, TERRAIN_BASE_TILE + (cell - 1));
                VDP_setTileMapXY(BG_A, tile, clump->tileX + dx, clump->tileY + dy);
            }
        }
    }
}

// Stamps one of the pregenerated starfield variants onto one band's rows.
// No clump-tracking needed here (BG_B is purely decorative).
static void applyStarfieldMap(u16 bandRow)
{
    const StarfieldMap *map = &starfieldMaps[random() % STARFIELD_MAP_COUNT];

    VDP_fillTileMapRect(BG_B, 0, 0, bandRow, TERRAIN_MAP_COLS, BAND_ROWS);

    for (u16 i = 0; i < map->count; i++)
    {
        const StarfieldStar *s = &map->stars[i];
        // s->variant is already the tile-sheet index (1=dim, 2=bright --
        // see generate_terrain.py's gen_starfield_map()), unlike terrain's
        // `cell` field which reserves 0 for "gap" and needs a -1. Tile 0
        // here is genuinely empty space, never referenced.
        u16 tile = TILE_ATTR_FULL(PAL_ENVIRONMENT, FALSE, FALSE, FALSE, STARFIELD_BASE_TILE + s->variant);
        VDP_setTileMapXY(BG_B, tile, s->x, bandRow + s->y);
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
    // drawn on BG_A too, at columns 0..39 (its full width). applyTerrainMap()
    // below blanks that same column range for both bands (i.e. the whole
    // plane height), so this is only needed for the untouched columns 40..63
    // (never generated/drawn to -- see TERRAIN_MAP_COLS -- since they never
    // scroll into view with no horizontal scroll in use).
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
        applyTerrainMap(b, b * BAND_ROWS);
        applyStarfieldMap(b * BAND_ROWS);
    }

    terrainScroll = 0;
    starfieldScroll = 0;
    terrainRegenPending = FALSE;
    starfieldRegenPending = FALSE;
    autoRegenTimer = AUTO_REGEN_FRAMES;
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

    if (autoRegenTimer > 0)
    {
        autoRegenTimer--;
    }
    else
    {
        terrainRegenPending = TRUE;
        starfieldRegenPending = TRUE;
        autoRegenTimer = AUTO_REGEN_FRAMES;
    }

    if (terrainRegenPending)
    {
        s16 topRow = ((s32) F16_toInt(terrainScroll) / 8) % PLANE_H_TILES;
        if (topRow < 0)
            topRow += PLANE_H_TILES;

        for (u16 half = 0; half < BANDS_PER_PLANE; half++)
        {
            if (halfIsOffscreen(half, topRow))
            {
                applyTerrainMap(half, half * BAND_ROWS);
                terrainRegenPending = FALSE;
                break;
            }
        }
    }

    if (starfieldRegenPending)
    {
        s16 topRow = ((s32) F16_toInt(starfieldScroll) / 8) % PLANE_H_TILES;
        if (topRow < 0)
            topRow += PLANE_H_TILES;

        for (u16 half = 0; half < BANDS_PER_PLANE; half++)
        {
            if (halfIsOffscreen(half, topRow))
            {
                applyStarfieldMap(half * BAND_ROWS);
                starfieldRegenPending = FALSE;
                break;
            }
        }
    }
}

void terrain_requestRegen(void)
{
    // Applying a half still costs a couple of bulk VDP_fillTileMapRect calls
    // plus one VDP_setTileMapXY per star/clump cell (no random() calls left,
    // now that terrain_generated.h has the shapes pregenerated) -- cheap as
    // a one-off, but still visibly janky if it lands while the player can
    // actually see the half being rewritten. So this doesn't regenerate
    // anything itself: it just flags the request, and
    // terrain_update() carries it out on whichever later frame first finds
    // one of the two halves entirely off-screen (see halfIsOffscreen()) --
    // which happens naturally within at most about half a lap of scrolling.
    // Terrain and starfield are flagged independently and each waits for its
    // own plane's half to be off-screen (see terrain_update()), since the
    // two scroll at different speeds and so go off-screen at different times.
    terrainRegenPending = TRUE;
    starfieldRegenPending = TRUE;
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
