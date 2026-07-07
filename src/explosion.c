#include "explosion.h"
#include "resources.h"

#define EXPLOSION_FRAME_COUNT 4
#define EXPLOSION_FRAME_HOLD  6 // frames each animation frame is held (~0.1s)
#define EXPLOSION_SPR_W 16
#define EXPLOSION_SPR_H 16

typedef struct
{
    bool active;
    Sprite *sprite;
    u16 frameIndex;
    u16 frameTimer;
    u16 startDelay;
} Explosion;

static Explosion explosions[MAX_EXPLOSIONS];

// Every explosion plays the same 4-frame animation, so its tile graphics
// are uploaded to VRAM exactly once here (one shared tile block per frame)
// instead of once per active explosion -- SPR_addSprite's default behaviour
// would otherwise give each of the up to MAX_EXPLOSIONS sprites its own
// private VRAM copy of all 4 frames. Each instance just points its "VRAM
// tile index" at the shared block for whichever frame it's currently on
// (see SPR_setVRAMTileIndex calls below) rather than using SPR_setFrame,
// which would try to re-upload tile data.
// Enemy tiles (see enemy.c's loadSharedTiles) now run bee(8)+special(8)+
// big(32)+waverA/B/C(8 each = 24) = 72 tiles starting at +128, so this must
// start at +200 or later to avoid overlapping them.
#define EXPLOSION_TILE_BASE (TILE_USER_INDEX + 200)

static u16 frameTile[EXPLOSION_FRAME_COUNT];

// Reloaded every time (no "already loaded" guard) -- see enemy.c's
// loadSharedTiles for why: a soft reset clears VRAM but not this static
// state, so a one-time guard would skip the re-upload after a reset.
static void loadSharedTiles(void)
{
    u16 totalTiles;
    u16 **idx = SPR_loadAllFrames(&spr_explosion, EXPLOSION_TILE_BASE, &totalTiles);
    for (u16 f = 0; f < EXPLOSION_FRAME_COUNT; f++)
        frameTile[f] = idx[0][f];
    MEM_free(idx);
}

void explosions_init(void)
{
    loadSharedTiles();

    for (u16 i = 0; i < MAX_EXPLOSIONS; i++)
    {
        explosions[i].active = FALSE;
        // Sprite handle intentionally left alone -- see bullet.c pool_init.
    }
}

void explosion_spawnAtDelayed(s16 centerX, s16 centerY, u16 delay)
{
    for (u16 i = 0; i < MAX_EXPLOSIONS; i++)
    {
        Explosion *e = &explosions[i];
        if (e->active)
            continue;

        e->active = TRUE;
        e->frameIndex = 0;
        e->frameTimer = EXPLOSION_FRAME_HOLD;
        e->startDelay = delay;

        s16 px = centerX - EXPLOSION_SPR_W / 2;
        s16 py = centerY - EXPLOSION_SPR_H / 2;

        if (e->sprite == NULL)
            e->sprite = SPR_addSpriteEx(&spr_explosion, px, py,
                                         TILE_ATTR_FULL(PAL_ENEMY, FALSE, FALSE, FALSE, frameTile[0]), 0);
        else
        {
            SPR_setPosition(e->sprite, px, py);
            SPR_setVRAMTileIndex(e->sprite, frameTile[0]);
        }

        SPR_setVisibility(e->sprite, delay > 0 ? HIDDEN : VISIBLE);
        return;
    }
}

void explosion_spawnAt(s16 centerX, s16 centerY)
{
    explosion_spawnAtDelayed(centerX, centerY, 0);
}

void explosions_update(void)
{
    for (u16 i = 0; i < MAX_EXPLOSIONS; i++)
    {
        Explosion *e = &explosions[i];
        if (!e->active)
            continue;

        if (e->startDelay > 0)
        {
            e->startDelay--;
            if (e->startDelay == 0)
                SPR_setVisibility(e->sprite, VISIBLE);
            continue;
        }

        e->frameTimer--;
        if (e->frameTimer == 0)
        {
            e->frameIndex++;
            if (e->frameIndex >= EXPLOSION_FRAME_COUNT)
            {
                e->active = FALSE;
                SPR_setVisibility(e->sprite, HIDDEN);
                continue;
            }

            SPR_setVRAMTileIndex(e->sprite, frameTile[e->frameIndex]);
            e->frameTimer = EXPLOSION_FRAME_HOLD;
        }
    }
}

void explosions_hideAll(void)
{
    for (u16 i = 0; i < MAX_EXPLOSIONS; i++)
    {
        Explosion *e = &explosions[i];
        if (!e->active)
            continue;

        e->active = FALSE;
        SPR_setVisibility(e->sprite, HIDDEN);
    }
}
