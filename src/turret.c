#include "turret.h"
#include "resources.h"
#include "score.h"
#include "powerup.h"
#include "bullet.h"
#include "player.h"
#include "explosion.h"
#include "enemy.h"
#include "terrain.h"
#include "sfx.h"

// A clump is a spawn candidate once it's this close to scrolling into view
// from the top (terrain_clumpScreenY() returns a negative "still above the
// screen" value) -- close enough that the turret appears to ride in with
// its clump rather than popping in already deep on screen.
#define CLUMP_APPROACH_PX 60

Turret turrets[MAX_TURRETS];

#define TURRET_SPR_W 16
#define TURRET_SPR_H 16

#define HP_TURRET 8

#define FIRE_COOLDOWN_MIN   100 // ~1.7s
#define FIRE_COOLDOWN_RANGE 120 // up to +2s more
#define SHOOT_ANIM_FRAMES   12  // frames the muzzle-flash frame is held
#define HIT_FLASH_FRAMES    3   // frames the white hit-flash frame is held
#define TURRET_BULLET_SPEED FIX16(1.8)
#define TURRET_AIM_JITTER   FIX16(0.5) // max +/- horizontal aim error

// How often a spawn is *attempted* (not guaranteed -- see trySpawn()).
#define SPAWN_CHECK_COOLDOWN_MIN   120 // 2s
#define SPAWN_CHECK_COOLDOWN_RANGE 180 // up to +3s more

#define POWERUP_DROP_PERCENT 20 // chance a killed turret drops a powerup

// Every turret looks identical (idle/firing/flash), so its tile graphics
// are uploaded to VRAM exactly once here rather than once per active
// turret -- same reasoning as enemy.c/bullet.c/explosion.c.
#define TURRET_TILE_BASE (TILE_USER_INDEX + 192)

static u16 idleTile;
static u16 firingTile;
static u16 flashTile;
static bool tilesLoaded = FALSE;

static u16 spawnCooldown;

static void loadSharedTiles(void)
{
    if (tilesLoaded)
        return;

    u16 totalTiles;
    u16 **idx = SPR_loadAllFrames(&spr_turret, TURRET_TILE_BASE, &totalTiles);
    idleTile = idx[0][0];
    firingTile = idx[1][0];
    flashTile = idx[2][0];
    MEM_free(idx);

    tilesLoaded = TRUE;
}

// What the turret should be showing right now, ignoring any hit-flash.
static u16 baseTile(const Turret *t)
{
    return t->shootAnimTimer > 0 ? firingTile : idleTile;
}

static u16 randomCooldown(u16 minFrames, u16 range)
{
    return minFrames + (random() % range);
}

void turrets_init(void)
{
    loadSharedTiles();

    for (u16 i = 0; i < MAX_TURRETS; i++)
    {
        turrets[i].active = FALSE;
        // Sprite handle intentionally left alone -- see bullet.c pool_init.
    }

    spawnCooldown = randomCooldown(SPAWN_CHECK_COOLDOWN_MIN, SPAWN_CHECK_COOLDOWN_RANGE);
}

static u16 countActive(void)
{
    u16 count = 0;
    for (u16 i = 0; i < MAX_TURRETS; i++)
        if (turrets[i].active)
            count++;
    return count;
}

// Finds a terrain clump that's about to scroll into view from the top and
// isn't too close to the HUD panel to fit a turret, picking randomly among
// whichever candidates qualify right now. Returns NULL if none do (the
// caller just tries again on its next cooldown).
static const TerrainClump *findApproachingClump(void)
{
    const TerrainClump *candidates[MAX_TERRAIN_CLUMPS];
    u16 count = 0;

    for (u16 i = 0; i < terrainClumpCount; i++)
    {
        const TerrainClump *c = &terrainClumps[i];
        s16 screenY = terrain_clumpScreenY(c);
        if (screenY >= -CLUMP_APPROACH_PX && screenY < 0)
        {
            s16 centerX = c->tileX * 8 + (c->tileW * 8) / 2 - TURRET_SPR_W / 2;
            if (centerX >= PLAY_AREA_X_MIN && centerX <= PLAY_AREA_X_MAX - TURRET_SPR_W)
                candidates[count++] = c;
        }
    }

    if (count == 0)
        return NULL;

    return candidates[random() % count];
}

// A turret is allowed to appear once the formation's small (BEE/SPECIAL)
// enemies are gone, OR once the formation is thinned out enough (fewer than
// this many enemies of any kind left) -- not just when it's fully cleared.
#define TURRET_ELIGIBLE_ENEMY_COUNT 6

static void trySpawn(void)
{
    // Never more than MAX_TURRETS at a time.
    if (countActive() >= MAX_TURRETS)
        return;
    if (enemies_countSmall() > 0 && enemies_countActive() >= TURRET_ELIGIBLE_ENEMY_COUNT)
        return;

    const TerrainClump *clump = findApproachingClump();
    if (clump == NULL)
        return;

    for (u16 i = 0; i < MAX_TURRETS; i++)
    {
        Turret *t = &turrets[i];
        if (t->active)
            continue;

        t->active = TRUE;
        t->hp = HP_TURRET;
        t->fireCooldown = randomCooldown(FIRE_COOLDOWN_MIN, FIRE_COOLDOWN_RANGE);
        t->shootAnimTimer = 0;
        t->flashTimer = 0;
        t->hasPowerup = (random() % 100) < POWERUP_DROP_PERCENT;

        s16 x = clump->tileX * 8 + (clump->tileW * 8) / 2 - TURRET_SPR_W / 2;
        s16 y = terrain_clumpScreenY(clump);
        t->x = FIX16(x);
        t->y = FIX16(y);

        if (t->sprite == NULL)
            t->sprite = SPR_addSpriteEx(&spr_turret, x, y,
                                         TILE_ATTR_FULL(PAL_TERRA, FALSE, FALSE, FALSE, idleTile), 0);
        else
            SPR_setVRAMTileIndex(t->sprite, idleTile);

        SPR_setPosition(t->sprite, x, y);
        SPR_setVisibility(t->sprite, VISIBLE);
        return;
    }
}

void turret_hit(Turret *t, s16 damage)
{
    t->hp -= damage;
    t->flashTimer = HIT_FLASH_FRAMES;
    SPR_setVRAMTileIndex(t->sprite, flashTile);

    if (t->hp > 0)
        return;

    s16 x = F16_toInt(t->x);
    s16 y = F16_toInt(t->y);

    t->active = FALSE;
    SPR_setVisibility(t->sprite, HIDDEN);

    explosion_spawnAt(x + TURRET_SPR_W / 2, y + TURRET_SPR_H / 2);
    sfx_play_explosion();
    score_addTurretKill();

    if (t->hasPowerup)
        powerup_spawnAt(x, y);
}

AABB turret_getBounds(const Turret *t)
{
    AABB box = {F16_toInt(t->x), F16_toInt(t->y), TURRET_SPR_W, TURRET_SPR_H};
    return box;
}

void turrets_update(void)
{
    if (spawnCooldown > 0)
    {
        spawnCooldown--;
    }
    else
    {
        trySpawn();
        spawnCooldown = randomCooldown(SPAWN_CHECK_COOLDOWN_MIN, SPAWN_CHECK_COOLDOWN_RANGE);
    }

    for (u16 i = 0; i < MAX_TURRETS; i++)
    {
        Turret *t = &turrets[i];
        if (!t->active)
            continue;

        // Travels in lockstep with the terrain clumps -- same speed the
        // terrain itself scrolls at -- and disappears once it scrolls past
        // the bottom of the screen, exactly like the clump it's attached to.
        t->y += TERRAIN_SPEED;
        if (F16_toInt(t->y) > SCREEN_H)
        {
            t->active = FALSE;
            SPR_setVisibility(t->sprite, HIDDEN);
            continue;
        }

        bool onScreen = F16_toInt(t->y) >= 0 && F16_toInt(t->y) <= SCREEN_H - TURRET_SPR_H;
        if (onScreen)
        {
            if (t->fireCooldown > 0)
            {
                t->fireCooldown--;
            }
            else
            {
                fix16 bx = t->x + FIX16(TURRET_SPR_W / 2 - 4);
                fix16 by = t->y + FIX16(TURRET_SPR_H);

                fix16 dx = player.x - bx;
                fix16 dy = player.y - by;
                if (dy < FIX16(20)) dy = FIX16(20); // avoid divide blowup if level with/above the target

                // Normalize (dx,dy) to a unit vector before scaling by
                // speed -- deriving vx from the slope while holding vy
                // fixed (the old approach) made the total velocity grow
                // with how horizontal the shot was, since vy never shrank
                // to compensate for a larger vx.
                fix16 dist = (fix16) getApproximatedDistance(dx, dy);
                fix16 vx = F16_mul(F16_div(dx, dist), TURRET_BULLET_SPEED);
                fix16 vy = F16_mul(F16_div(dy, dist), TURRET_BULLET_SPEED);
                fix16 jitter = (fix16) (random() % (2 * TURRET_AIM_JITTER + 1)) - TURRET_AIM_JITTER;
                vx += jitter;

                bullet_spawn_enemy(bx, by, vx, vy);
                sfx_play_shoot();

                t->fireCooldown = randomCooldown(FIRE_COOLDOWN_MIN, FIRE_COOLDOWN_RANGE);
                t->shootAnimTimer = SHOOT_ANIM_FRAMES;
                // Don't clobber an in-progress hit-flash with the firing
                // frame -- the flash takes visual priority either way.
                if (t->flashTimer == 0)
                    SPR_setVRAMTileIndex(t->sprite, firingTile);
            }
        }

        if (t->shootAnimTimer > 0)
        {
            t->shootAnimTimer--;
            if (t->shootAnimTimer == 0 && t->flashTimer == 0)
                SPR_setVRAMTileIndex(t->sprite, idleTile);
        }

        if (t->flashTimer > 0)
        {
            t->flashTimer--;
            if (t->flashTimer == 0)
                SPR_setVRAMTileIndex(t->sprite, baseTile(t));
        }

        SPR_setPosition(t->sprite, F16_toInt(t->x), F16_toInt(t->y));
    }
}

void turrets_hideAll(void)
{
    for (u16 i = 0; i < MAX_TURRETS; i++)
    {
        Turret *t = &turrets[i];
        if (!t->active)
            continue;

        t->active = FALSE;
        SPR_setVisibility(t->sprite, HIDDEN);
    }
}
