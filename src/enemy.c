#include "enemy.h"
#include "resources.h"
#include "score.h"
#include "powerup.h"
#include "bullet.h"
#include "player.h"
#include "explosion.h"
#include "sfx.h"

Enemy enemies[MAX_ENEMIES];

#define ENTER_DURATION 90 // frames (~1.5s at 60fps)
#define HIT_FLASH_FRAMES 3

#define HP_BEE     10
#define HP_SPECIAL 10
#define HP_BIG     100

// BEE/SPECIAL: occasionally peel out of formation, dive down and off the
// bottom of the screen, then re-enter from the top back into its slot --
// same path shape as the initial formation entrance.
#define DIVE_SPEED_Y          FIX16(1.2)
#define DIVE_SPEED_X_MAX      FIX16(0.6) // max horizontal drift while diving
#define DIVE_REENTRY_OFFSET   30         // +/- offset from slot X to re-enter from
#define DIVE_COOLDOWN_MIN     180 // 3s at 60fps
#define DIVE_COOLDOWN_RANGE   240 // up to +4s more

// BIG: fires at the player (with some aim error) at random intervals while
// in formation.
#define BIG_FIRE_COOLDOWN_MIN   90  // 1.5s
#define BIG_FIRE_COOLDOWN_RANGE 150 // up to +2.5s more
#define ENEMY_BULLET_SPEED FIX16(2.0)
#define ENEMY_AIM_JITTER    FIX16(0.6) // max +/- horizontal aim error

// BIG deaths trigger several staggered explosions across its body instead
// of just one, since it's much larger than a bee/special.
#define BIG_EXPLOSION_COUNT       5
#define BIG_EXPLOSION_STAGGER     6 // frames between each burst starting

// BEE/SPECIAL only: caps how many can be away from their formation slot
// (diving out and/or swooping back in) at the same time.
#define MAX_CONCURRENT_DIVERS 5

static u16 activeDivers;

// All enemies of a given kind look identical (differing only in position),
// so their tile graphics are uploaded to VRAM exactly once per kind here
// instead of once per active enemy -- SPR_addSprite's default behaviour
// would otherwise give each of the up to MAX_ENEMIES sprites its own
// private VRAM copy of the same pixels, wasting a lot of the sprite
// engine's limited tile budget. Each enemy instance just points its
// "VRAM tile index" attribute at the shared normal/flash block for its
// kind (see SPR_setVRAMTileIndex calls below) rather than using
// SPR_setAnim/SPR_setFrame, which would try to re-upload tile data.
static u16 normalTile[3];
static u16 flashTile[3];

#define ENEMY_TILE_BASE (TILE_USER_INDEX + 128)

// Reloaded every time (no "already loaded" guard): a soft reset (console
// reset button) retains RAM/globals but the VDP's own reset clears VRAM, so
// a static "loaded" flag would stay TRUE across the reset and skip the
// re-upload, leaving these tiles missing on screen even though the C state
// looks fine. Tiles land at fixed indices (ENEMY_TILE_BASE), so reloading is
// idempotent and cheap -- fine to redo on every enemies_init().
static void loadSharedTiles(void)
{
    u16 totalTiles;
    u16 base = ENEMY_TILE_BASE;

    u16 **idx = SPR_loadAllFrames(&spr_enemy_bee, base, &totalTiles);
    normalTile[ENEMY_KIND_BEE] = idx[0][0];
    flashTile[ENEMY_KIND_BEE] = idx[1][0];
    MEM_free(idx);
    base += totalTiles;

    idx = SPR_loadAllFrames(&spr_enemy_special, base, &totalTiles);
    normalTile[ENEMY_KIND_SPECIAL] = idx[0][0];
    flashTile[ENEMY_KIND_SPECIAL] = idx[1][0];
    MEM_free(idx);
    base += totalTiles;

    idx = SPR_loadAllFrames(&spr_enemy_big, base, &totalTiles);
    normalTile[ENEMY_KIND_BIG] = idx[0][0];
    flashTile[ENEMY_KIND_BIG] = idx[1][0];
    MEM_free(idx);
}

void enemies_init(void)
{
    loadSharedTiles();

    for (u16 i = 0; i < MAX_ENEMIES; i++)
    {
        enemies[i].active = FALSE;
        enemies[i].diving = FALSE;
        // Sprite handles are intentionally left alone here (not nulled) so
        // a re-init (title -> game restart) reuses each slot's existing
        // SPR_addSprite handle instead of leaking a new one every game.
    }
    activeDivers = 0;
}

u16 enemies_countSmall(void)
{
    u16 count = 0;
    for (u16 i = 0; i < MAX_ENEMIES; i++)
    {
        const Enemy *e = &enemies[i];
        if (e->active && (e->kind == ENEMY_KIND_BEE || e->kind == ENEMY_KIND_SPECIAL))
            count++;
    }
    return count;
}

u16 enemies_countActive(void)
{
    u16 count = 0;
    for (u16 i = 0; i < MAX_ENEMIES; i++)
        if (enemies[i].active)
            count++;
    return count;
}

static const SpriteDefinition *spriteDefForKind(EnemyKind kind)
{
    switch (kind)
    {
        case ENEMY_KIND_SPECIAL: return &spr_enemy_special;
        case ENEMY_KIND_BIG:     return &spr_enemy_big;
        default:                 return &spr_enemy_bee;
    }
}

static s16 maxHpForKind(EnemyKind kind)
{
    switch (kind)
    {
        case ENEMY_KIND_BIG: return HP_BIG;
        case ENEMY_KIND_SPECIAL: return HP_SPECIAL;
        default: return HP_BEE;
    }
}

u16 enemy_widthForKind(EnemyKind kind)
{
    return (kind == ENEMY_KIND_BIG) ? 32 : 16;
}

u16 enemy_heightForKind(EnemyKind kind)
{
    return (kind == ENEMY_KIND_BIG) ? 32 : 16;
}

static u16 randomCooldown(u16 minFrames, u16 range)
{
    return minFrames + (random() % range);
}

Enemy *enemy_spawn(EnemyKind kind, s16 startX, s16 startY, s16 slotX, s16 slotY, s16 delay)
{
    for (u16 i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (e->active)
            continue;

        e->active = TRUE;
        e->kind = kind;
        e->state = ENEMY_STATE_ENTERING;
        e->x = FIX16(startX);
        e->y = FIX16(startY);
        e->vx = FIX16(slotX - startX) / ENTER_DURATION;
        e->vy = FIX16(slotY - startY) / ENTER_DURATION;
        e->slotX = slotX;
        e->slotY = slotY;
        e->enterTimer = 0;
        e->startDelay = delay;
        e->maxHp = maxHpForKind(kind);
        e->hp = e->maxHp;
        e->flashTimer = 0;
        e->diving = FALSE;
        e->forcedOut = FALSE;
        e->diveCooldown = randomCooldown(DIVE_COOLDOWN_MIN, DIVE_COOLDOWN_RANGE);
        e->fireCooldown = randomCooldown(BIG_FIRE_COOLDOWN_MIN, BIG_FIRE_COOLDOWN_RANGE);

        if (e->sprite == NULL)
            e->sprite = SPR_addSpriteEx(spriteDefForKind(kind), startX, startY,
                                         TILE_ATTR_FULL(PAL_ENEMY, FALSE, FALSE, FALSE, normalTile[kind]), 0);
        else
        {
            // This pool slot may have last held a different-sized kind
            // (e.g. a BEE reused for a BIG after a restart reshuffles the
            // formation) -- SPR_setDefinition() fixes up the sprite's shape;
            // it's a cheap metadata-only update here since VRAM is manually
            // managed (no SPR_FLAG_AUTO_VRAM_ALLOC).
            SPR_setDefinition(e->sprite, spriteDefForKind(kind));
            SPR_setVRAMTileIndex(e->sprite, normalTile[kind]);
        }
        SPR_setVisibility(e->sprite, delay > 0 ? HIDDEN : VISIBLE);
        SPR_setPosition(e->sprite, startX, startY);

        return e;
    }
    return NULL;
}

void enemy_hit(Enemy *e, s16 damage)
{
    e->hp -= damage;
    e->flashTimer = HIT_FLASH_FRAMES;
    SPR_setVRAMTileIndex(e->sprite, flashTile[e->kind]);

    if (e->hp <= 0)
        enemy_kill(e);
}

void enemy_kill(Enemy *e)
{
    s16 x = F16_toInt(e->x);
    s16 y = F16_toInt(e->y);
    u16 w = enemy_widthForKind(e->kind);
    u16 h = enemy_heightForKind(e->kind);

    e->active = FALSE;
    SPR_setVisibility(e->sprite, HIDDEN);

    if (e->diving)
    {
        e->diving = FALSE;
        activeDivers--;
    }

    if (e->kind == ENEMY_KIND_BIG)
    {
        for (u16 i = 0; i < BIG_EXPLOSION_COUNT; i++)
        {
            s16 cx = x + (s16) (random() % w);
            s16 cy = y + (s16) (random() % h);
            explosion_spawnAtDelayed(cx, cy, i * BIG_EXPLOSION_STAGGER);
        }
    }
    else
    {
        explosion_spawnAt(x + (s16) w / 2, y + (s16) h / 2);
    }

    score_addKill(e->kind);
    sfx_play_explosion();

    if (e->kind == ENEMY_KIND_SPECIAL)
        powerup_spawnAt(x, y);
}

AABB enemy_getBounds(const Enemy *e)
{
    AABB box = {F16_toInt(e->x), F16_toInt(e->y), enemy_widthForKind(e->kind), enemy_heightForKind(e->kind)};
    return box;
}

void enemies_update(void)
{
    for (u16 i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active)
            continue;

        if (e->startDelay > 0)
        {
            e->startDelay--;
            if (e->startDelay == 0)
                SPR_setVisibility(e->sprite, VISIBLE);
            continue;
        }

        if (e->state == ENEMY_STATE_ENTERING)
        {
            e->x += e->vx;
            e->y += e->vy;
            e->enterTimer++;
            if (e->enterTimer >= ENTER_DURATION)
            {
                e->state = ENEMY_STATE_IN_FORMATION;
                e->x = FIX16(e->slotX);
                e->y = FIX16(e->slotY);

                if (e->diving)
                {
                    e->diving = FALSE;
                    activeDivers--;
                }
            }
        }
        else if (e->state == ENEMY_STATE_IN_FORMATION)
        {
            if (e->kind == ENEMY_KIND_BIG)
            {
                if (e->fireCooldown > 0)
                {
                    e->fireCooldown--;
                }
                else
                {
                    fix16 bx = e->x + FIX16(enemy_widthForKind(e->kind) / 2 - 4);
                    fix16 by = e->y + FIX16(enemy_heightForKind(e->kind));

                    fix16 dx = player.x - bx;
                    fix16 dy = player.y - by;
                    // Avoid divide blowup if level with the target -- but
                    // keep dy's sign (don't just clamp to +20), otherwise a
                    // player above this enemy would still get shot at as if
                    // below it, aiming the shot the wrong way instead of up
                    // at them.
                    if (dy > -FIX16(20) && dy < FIX16(20))
                        dy = (dy < 0) ? -FIX16(20) : FIX16(20);

                    // Normalize (dx,dy) to a unit vector before scaling by
                    // speed -- deriving vx from the slope while holding vy
                    // fixed (the old approach) made the total velocity grow
                    // with how horizontal the shot was, since vy never
                    // shrank to compensate for a larger vx.
                    fix16 dist = (fix16) getApproximatedDistance(dx, dy);
                    fix16 vx = F16_mul(F16_div(dx, dist), ENEMY_BULLET_SPEED);
                    fix16 vy = F16_mul(F16_div(dy, dist), ENEMY_BULLET_SPEED);
                    fix16 jitter = (fix16) (random() % (2 * ENEMY_AIM_JITTER + 1)) - ENEMY_AIM_JITTER;
                    vx += jitter;

                    bullet_spawn_enemy(bx, by, vx, vy);
                    e->fireCooldown = randomCooldown(BIG_FIRE_COOLDOWN_MIN, BIG_FIRE_COOLDOWN_RANGE);
                }
            }
            else
            {
                if (e->diveCooldown > 0)
                {
                    e->diveCooldown--;
                }
                else if (activeDivers >= MAX_CONCURRENT_DIVERS)
                {
                    // Too many divers out already -- try again shortly
                    // instead of stalling until this exact frame.
                    e->diveCooldown = randomCooldown(30, 60);
                }
                else
                {
                    s16 drift = (s16) (random() % (2 * DIVE_SPEED_X_MAX + 1)) - DIVE_SPEED_X_MAX;
                    e->vx = drift;
                    e->vy = DIVE_SPEED_Y;
                    e->state = ENEMY_STATE_DIVING_OUT;
                    e->diving = TRUE;
                    activeDivers++;
                }
            }
        }
        else if (e->state == ENEMY_STATE_DIVING_OUT)
        {
            e->x += e->vx;
            e->y += e->vy;

            s16 px = F16_toInt(e->x);
            u16 w = enemy_widthForKind(e->kind);
            if (px < PLAY_AREA_X_MIN) { px = PLAY_AREA_X_MIN; e->x = FIX16(px); }
            if (px > PLAY_AREA_X_MAX - (s16) w + 16) { px = PLAY_AREA_X_MAX - (s16) w + 16; e->x = FIX16(px); }

            if (F16_toInt(e->y) > SCREEN_H)
            {
                if (e->forcedOut)
                {
                    // Timed out (see formation.c) -- gone for good, no
                    // score/powerup, same as enemies_hideAll().
                    e->active = FALSE;
                    SPR_setVisibility(e->sprite, HIDDEN);
                    if (e->diving)
                    {
                        e->diving = FALSE;
                        activeDivers--;
                    }
                }
                else
                {
                    // Passed off the bottom of the screen -- reappear from
                    // above and swoop back into its slot, same as the initial
                    // formation entrance.
                    s16 h = (s16) enemy_heightForKind(e->kind);
                    s16 side = (random() & 1) ? DIVE_REENTRY_OFFSET : -DIVE_REENTRY_OFFSET;
                    s16 startX = e->slotX + side;
                    if (startX < PLAY_AREA_X_MIN) startX = PLAY_AREA_X_MIN;
                    if (startX > PLAY_AREA_X_MAX) startX = PLAY_AREA_X_MAX;
                    s16 startY = -h - 10;

                    e->x = FIX16(startX);
                    e->y = FIX16(startY);
                    e->vx = FIX16(e->slotX - startX) / ENTER_DURATION;
                    e->vy = FIX16(e->slotY - startY) / ENTER_DURATION;
                    e->enterTimer = 0;
                    e->state = ENEMY_STATE_ENTERING;
                    e->diveCooldown = randomCooldown(DIVE_COOLDOWN_MIN, DIVE_COOLDOWN_RANGE);
                }
            }
        }

        if (e->flashTimer > 0)
        {
            e->flashTimer--;
            if (e->flashTimer == 0)
                SPR_setVRAMTileIndex(e->sprite, normalTile[e->kind]);
        }

        SPR_setPosition(e->sprite, F16_toInt(e->x), F16_toInt(e->y));
    }
}

void enemies_forceDiveAllOut(void)
{
    for (u16 i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active)
            continue;

        if (e->state != ENEMY_STATE_DIVING_OUT)
        {
            if (!e->diving)
            {
                e->diving = TRUE;
                activeDivers++;
            }

            s16 drift = (s16) (random() % (2 * DIVE_SPEED_X_MAX + 1)) - DIVE_SPEED_X_MAX;
            e->vx = drift;
            e->vy = DIVE_SPEED_Y;
            e->state = ENEMY_STATE_DIVING_OUT;
        }

        // Already diving out on its own (a voluntary bee/special dive) --
        // just mark it so it doesn't loop back in once it's off screen.
        e->forcedOut = TRUE;
    }
}

void enemies_hideAll(void)
{
    for (u16 i = 0; i < MAX_ENEMIES; i++)
    {
        Enemy *e = &enemies[i];
        if (!e->active)
            continue;

        e->active = FALSE;
        SPR_setVisibility(e->sprite, HIDDEN);
    }
}
