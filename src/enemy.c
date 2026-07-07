#include "enemy.h"
#include "resources.h"
#include "score.h"
#include "powerup.h"
#include "bullet.h"
#include "player.h"
#include "explosion.h"
#include "sfx.h"
#include "interwave_generated.h"

Enemy enemies[MAX_ENEMIES];

#define ENTER_DURATION 90 // frames (~1.5s at 60fps)
#define HIT_FLASH_FRAMES 3

#define HP_BEE     10
#define HP_SPECIAL 10
#define HP_BIG     100
#define HP_WAVER   1

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

// Inter-wave "waver" formation (see ENEMY_STATE_WAVING/enemy_spawnWaverFormation()).
// One formation = WAVER_SUBGROUP_COUNT batches of WAVER_GRID_ROWS x
// WAVER_GRID_COLS (WAVER_SUBGROUP_SIZE, both from enemy.h) enemies, spawned
// one batch at a time (see enemy.h's top comment for why). Members go
// straight into ENEMY_STATE_WAVING from spawn -- no separate entrance flight
// that settles into a neat grid first; they scroll onto the screen already
// riding their kind's precalculated path (interwave_generated.h). Rows
// within a batch are staggered by WAVER_ROW_STAGGER_FRAMES so the whole
// batch doesn't pop in as one block. INTERWAVE_ENTRY_STAGGER_SECONDS
// (settings.h) is the pause between one batch fully clearing and the next
// appearing.
#define WAVER_BATCH_GAP_FRAMES (INTERWAVE_ENTRY_STAGGER_SECONDS * 60)
#define WAVER_COL_SPACING 18 // px between column centers -- edges just touch (16px sprite), no overlap
#define WAVER_ROW_SPACING 30
#define WAVER_ROW_STAGGER_FRAMES 90 // 1.5s between each row starting to scroll in

// Per-row offset into the shared path clock (see waverRowPhase in enemy.h)
// -- each row samples waverPaths this many frames "ahead" of the row above
// it, so the batch ripples through the same path shape row by row instead
// of every row weaving in lockstep.
#define WAVER_ROW_PHASE_FRAMES 16
#define WAVER_DESCEND_SPEED FIX16(1.5) // must match DESCEND_SPEED_PX in generate_interwave.py

// Wavers only rarely fire back -- a long cooldown means any single one
// fires at most once or twice during its whole flight.
#define WAVER_FIRE_COOLDOWN_MIN   240 // 4s
#define WAVER_FIRE_COOLDOWN_RANGE 180 // up to +3s more
#define WAVER_BULLET_SPEED FIX16(1.6)
#define WAVER_AIM_JITTER    FIX16(0.6)

// SIDE_DIVE inter-wave shape (see WaverShape in enemy.h): rows of 2 enemies
// (offset by SIDE_DIVE_COL_GAP) enter from genuinely off-screen (past
// SCREEN_W or below 0, not just past the playfield's internal
// PLAY_AREA_X_MIN/MAX margins -- those are still on screen) on the left or
// right -- side chosen per row. The sweep-in reuses the standard
// ENEMY_STATE_ENTERING path every other entrance in this file already goes
// through (enemy_spawn()'s vx/vy, linearly interpolated to an exact slot and
// snapped there -- see e->enterDuration in enemy.h), just with a shorter
// duration than the default ENTER_DURATION so it reads as "quickly". Once
// the sweep lands exactly on its slot, it switches to ENEMY_STATE_WAVING for
// a steep constant vertical dive off the bottom. Rows enter in quick
// succession (SIDE_DIVE_ROW_STAGGER_FRAMES apart) via the same startDelay
// mechanism as the grid-weave rows.
#define SIDE_DIVE_ROW_COUNT          8
#define SIDE_DIVE_SUBGROUP_SIZE      (SIDE_DIVE_ROW_COUNT * 2) // 16
#define SIDE_DIVE_COL_GAP            18 // px between the two members of a row
#define SIDE_DIVE_ROW_STAGGER_FRAMES 20 // ~1/3s between each row starting
#define SIDE_DIVE_START_Y            24 // near the top, not mid-screen -- leaves most of the screen for the dive
#define SIDE_DIVE_DIVE_VY       FIX16(1.8) // steep constant descent once the sweep finishes

// BEE/SPECIAL only: caps how many can be away from their formation slot
// (diving out and/or swooping back in) at the same time.
#define MAX_CONCURRENT_DIVERS 5

static u16 activeDivers;

// The current inter-wave batch's shared path clock (see
// enemy_spawnWaverFormation()): only one batch is ever spawned/alive at a
// time, so a single instance is enough. Every WAVING member reads this same
// clock -- ticks once per frame from the moment the batch spawns.
static u16 currentWaverClock;

// Which of the WAVER_PATH_COUNT precalculated paths (interwave_generated.h)
// the current batch is riding -- picked at random (see spawnNextWaverSubgroup())
// each time a batch spawns, independent of ENEMY_KIND_WAVER_A/B/C (which only
// pick the sprite/color, not the movement).
static u8 currentWaverPathId;

// Which formation shape the current/most-recent inter-wave formation is
// using (see WaverShape in enemy.h) -- alternated each time
// enemy_spawnWaverFormation() runs (see interwaveFormationCount below).
static WaverShape currentWaverShape;
static u16 interwaveFormationCount;

// Inter-wave formation sequencing state -- see enemy_spawnWaverFormation()
// and updateWaverFormationSequencing().
static EnemyKind waverFormationKind;
static u16 waverBatchesSpawned;  // how many of WAVER_SUBGROUP_COUNT have started so far
static u16 waverGapTimer;        // frames since the current (cleared) batch was confirmed gone
static bool waverFormationActive; // FALSE once every batch has been spawned and cleared

// Killed (not merely flown off screen) since the last
// enemy_spawnWaverFormation() call -- see enemies_waverKillCount().
static u16 waverKillCount;

// All enemies of a given kind look identical (differing only in position),
// so their tile graphics are uploaded to VRAM exactly once per kind here
// instead of once per active enemy -- SPR_addSprite's default behaviour
// would otherwise give each of the up to MAX_ENEMIES sprites its own
// private VRAM copy of the same pixels, wasting a lot of the sprite
// engine's limited tile budget. Each enemy instance just points its
// "VRAM tile index" attribute at the shared normal/flash block for its
// kind (see SPR_setVRAMTileIndex calls below) rather than using
// SPR_setAnim/SPR_setFrame, which would try to re-upload tile data.
// Sized for every EnemyKind, including the 3 waver kinds.
static u16 normalTile[ENEMY_KIND_WAVER_C + 1];
static u16 flashTile[ENEMY_KIND_WAVER_C + 1];

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
    base += totalTiles;

    idx = SPR_loadAllFrames(&spr_enemy_waver_a, base, &totalTiles);
    normalTile[ENEMY_KIND_WAVER_A] = idx[0][0];
    flashTile[ENEMY_KIND_WAVER_A] = idx[1][0];
    MEM_free(idx);
    base += totalTiles;

    idx = SPR_loadAllFrames(&spr_enemy_waver_b, base, &totalTiles);
    normalTile[ENEMY_KIND_WAVER_B] = idx[0][0];
    flashTile[ENEMY_KIND_WAVER_B] = idx[1][0];
    MEM_free(idx);
    base += totalTiles;

    idx = SPR_loadAllFrames(&spr_enemy_waver_c, base, &totalTiles);
    normalTile[ENEMY_KIND_WAVER_C] = idx[0][0];
    flashTile[ENEMY_KIND_WAVER_C] = idx[1][0];
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

    currentWaverClock = 0;
    currentWaverPathId = 0;
    currentWaverShape = WAVER_SHAPE_GRID_WEAVE;
    interwaveFormationCount = 0;
    waverBatchesSpawned = 0;
    waverGapTimer = 0;
    waverFormationActive = FALSE;
    waverKillCount = 0;
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
        case ENEMY_KIND_WAVER_A: return &spr_enemy_waver_a;
        case ENEMY_KIND_WAVER_B: return &spr_enemy_waver_b;
        case ENEMY_KIND_WAVER_C: return &spr_enemy_waver_c;
        default:                 return &spr_enemy_bee;
    }
}

static s16 maxHpForKind(EnemyKind kind)
{
    switch (kind)
    {
        case ENEMY_KIND_BIG: return HP_BIG;
        case ENEMY_KIND_SPECIAL: return HP_SPECIAL;
        case ENEMY_KIND_WAVER_A:
        case ENEMY_KIND_WAVER_B:
        case ENEMY_KIND_WAVER_C: return HP_WAVER;
        default: return HP_BEE;
    }
}

bool enemy_isWaverKind(EnemyKind kind)
{
    return kind >= ENEMY_KIND_WAVER_A && kind <= ENEMY_KIND_WAVER_C;
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

// Fires one bullet from e's current position, aimed at the player (with
// some random horizontal error), at the given speed. Shared by BIG (regular
// combat waves) and the waver kinds (inter-wave, rare fire) -- the only
// difference between them is speed/jitter and how often it's called.
static void fireAimedShotAt(const Enemy *e, fix16 speed, fix16 jitter)
{
    fix16 bx = e->x + FIX16(enemy_widthForKind(e->kind) / 2 - 4);
    fix16 by = e->y + FIX16(enemy_heightForKind(e->kind));

    fix16 dx = player.x - bx;
    fix16 dy = player.y - by;
    // Avoid divide blowup if level with the target -- but keep dy's sign
    // (don't just clamp to +20), otherwise a player above this enemy would
    // still get shot at as if below it, aiming the shot the wrong way
    // instead of up at them.
    if (dy > -FIX16(20) && dy < FIX16(20))
        dy = (dy < 0) ? -FIX16(20) : FIX16(20);

    // Normalize (dx,dy) to a unit vector before scaling by speed -- deriving
    // vx from the slope while holding vy fixed made the total velocity grow
    // with how horizontal the shot was, since vy never shrank to compensate
    // for a larger vx.
    fix16 dist = (fix16) getApproximatedDistance(dx, dy);
    fix16 vx = F16_mul(F16_div(dx, dist), speed);
    fix16 vy = F16_mul(F16_div(dy, dist), speed);
    vx += (fix16) (random() % (2 * jitter + 1)) - jitter;

    bullet_spawn_enemy(bx, by, vx, vy);
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
        e->enterDuration = ENTER_DURATION;
        e->startDelay = delay;
        e->maxHp = maxHpForKind(kind);
        e->hp = e->maxHp;
        e->flashTimer = 0;
        e->diving = FALSE;
        e->forcedOut = FALSE;
        e->isWaver = FALSE;
        e->groupOffsetX = 0;
        e->diveCooldown = randomCooldown(DIVE_COOLDOWN_MIN, DIVE_COOLDOWN_RANGE);
        e->fireCooldown = enemy_isWaverKind(kind)
            ? randomCooldown(WAVER_FIRE_COOLDOWN_MIN, WAVER_FIRE_COOLDOWN_RANGE)
            : randomCooldown(BIG_FIRE_COOLDOWN_MIN, BIG_FIRE_COOLDOWN_RANGE);

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

    if (e->isWaver)
        waverKillCount++;
}

u16 enemies_waverKillCount(void)
{
    return waverKillCount;
}

u16 enemies_waverTotalCount(void)
{
    u16 subgroupSize = (currentWaverShape == WAVER_SHAPE_SIDE_DIVE) ? SIDE_DIVE_SUBGROUP_SIZE : WAVER_SUBGROUP_SIZE;
    return subgroupSize * WAVER_SUBGROUP_COUNT;
}

AABB enemy_getBounds(const Enemy *e)
{
    AABB box = {F16_toInt(e->x), F16_toInt(e->y), enemy_widthForKind(e->kind), enemy_heightForKind(e->kind)};
    return box;
}

// Ticks the current batch's shared path clock once per frame -- the
// per-enemy loop in enemies_update() only reads currentWaverClock, it never
// increments it (every WAVING member shares the same clock).
static void updateWaverGroup(void)
{
    currentWaverClock++;
}

static void updateWaverFormationSequencing(void); // defined below, used here

void enemies_update(void)
{
    updateWaverGroup();
    if (waverFormationActive)
        updateWaverFormationSequencing();

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
            if (e->enterTimer >= e->enterDuration)
            {
                e->x = FIX16(e->slotX);
                e->y = FIX16(e->slotY);

                if (e->diving)
                {
                    e->diving = FALSE;
                    activeDivers--;
                }

                // GRID_WEAVE wavers never reach this state -- they go straight
                // to ENEMY_STATE_WAVING from spawn (see spawnNextGridWeaveSubgroup()).
                // SIDE_DIVE wavers do enter through here (see
                // spawnNextSideDiveSubgroup()): once their sweep-in lands
                // exactly on its slot, switch to the dive instead of
                // pretending they're part of a static formation.
                e->state = e->isWaver ? ENEMY_STATE_WAVING : ENEMY_STATE_IN_FORMATION;
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
                    fireAimedShotAt(e, ENEMY_BULLET_SPEED, ENEMY_AIM_JITTER);
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
        else if (e->state == ENEMY_STATE_WAVING)
        {
            if (currentWaverShape == WAVER_SHAPE_SIDE_DIVE)
            {
                // The sweep-in already happened during ENEMY_STATE_ENTERING
                // (see spawnNextSideDiveSubgroup()/enemy_spawn()) and landed
                // exactly on its slot -- this is just the steep drop after.
                e->y += SIDE_DIVE_DIVE_VY;
            }
            else
            {
                // Signed: e->waverRowPhase is negative (see
                // spawnNextGridWeaveSubgroup()), and right at reveal time
                // currentWaverClock may be a frame or two short of/past
                // exactly cancelling it out, so this must be able to dip
                // below 0 (clamped) as well as exceed WAVER_PATH_LENGTH.
                s16 clock = (s16) currentWaverClock + e->waverRowPhase;
                if (clock < 0)
                    clock = 0;
                else if (clock >= WAVER_PATH_LENGTH)
                    clock = WAVER_PATH_LENGTH - 1;

                s16 anchorX = (PLAY_AREA_X_MIN + PLAY_AREA_X_MAX) / 2;

                e->x = FIX16(anchorX + waverPaths[currentWaverPathId][clock] + e->groupOffsetX);
                e->y += WAVER_DESCEND_SPEED;
            }

            if (F16_toInt(e->y) > SCREEN_H)
            {
                // Flew off the bottom -- gone for good, no score/powerup
                // (see enemy_kill(), never called on this path).
                e->active = FALSE;
                SPR_setVisibility(e->sprite, HIDDEN);
            }
            else if (e->fireCooldown > 0)
            {
                e->fireCooldown--;
            }
            else
            {
                fireAimedShotAt(e, WAVER_BULLET_SPEED, WAVER_AIM_JITTER);
                e->fireCooldown = randomCooldown(WAVER_FIRE_COOLDOWN_MIN, WAVER_FIRE_COOLDOWN_RANGE);
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

// Offset of grid position `index` (0..count-1) from the grid's own center,
// spaced `spacing` apart -- e.g. count=6 spacing=18 gives -45,-27,-9,9,27,45.
// Exact integer result: (2*index-(count-1)) is always an integer, and
// spacing is always even, so dividing by 2 never truncates.
static s16 gridOffset(u16 index, u16 count, s16 spacing)
{
    return (s16) ((2 * (s16) index - ((s16) count - 1)) * spacing / 2);
}

// Spawns exactly one batch of WAVER_SUBGROUP_SIZE enemies of
// waverFormationKind. Each row appears WAVER_ROW_STAGGER_FRAMES after the
// previous one (via startDelay), but once a member is actually revealed it
// goes straight into ENEMY_STATE_WAVING -- no entrance flight that settles
// into a neat grid first, it's already riding the path as it scrolls into
// view.
static void spawnNextGridWeaveSubgroup(void)
{
    s16 anchorX = (PLAY_AREA_X_MIN + PLAY_AREA_X_MAX) / 2;
    s16 h = (s16) enemy_heightForKind(waverFormationKind);

    currentWaverClock = 0;
    currentWaverPathId = (u8) (random() % WAVER_PATH_COUNT);

    for (u16 row = 0; row < WAVER_GRID_ROWS; row++)
    {
        // Purely additive (not gridOffset()'s symmetric-about-center spread):
        // that formula pushed higher row indices toward/past positive Y --
        // i.e. already inside the visible playfield -- once WAVER_ROW_SPACING
        // grew past a few px. Stacking every row strictly further above the
        // last guarantees all of them stay off-screen regardless of spacing.
        s16 startY = -h - 10 - (s16) row * WAVER_ROW_SPACING;
        s16 delay = (s16) (row * WAVER_ROW_STAGGER_FRAMES);

        // The shared currentWaverClock starts ticking the instant this batch
        // spawns, but this row won't actually be revealed (and start reading
        // it) until `delay` frames from now -- by then the clock will already
        // read ~delay. Baking in -delay here (plus a small per-row ripple
        // step) keeps the clock this row actually sees starting near 0 at
        // reveal time, regardless of how long WAVER_ROW_STAGGER_FRAMES is --
        // without it, a long enough stagger runs the clock past
        // WAVER_PATH_LENGTH before a later row is even revealed, freezing
        // its weave at the path's last entry from the moment it appears.
        s16 rowPhase = (s16) ((s16) row * WAVER_ROW_PHASE_FRAMES) - delay;

        for (u16 col = 0; col < WAVER_GRID_COLS; col++)
        {
            s16 colOffset = gridOffset(col, WAVER_GRID_COLS, WAVER_COL_SPACING);
            s16 startX = anchorX + colOffset;

            Enemy *e = enemy_spawn(waverFormationKind, startX, startY, startX, startY, delay);
            if (e == NULL)
                continue; // pool full -- shouldn't happen (see MAX_ENEMIES)

            e->isWaver = TRUE;
            e->groupOffsetX = colOffset;
            e->waverRowPhase = rowPhase;
            e->state = ENEMY_STATE_WAVING;
        }
    }

    waverBatchesSpawned++;
}

// Roughly how many px/frame the ENTERING sweep should cover, regardless of
// how far off-screen this particular row started -- entry distance varies a
// lot (landing spot is picked independently of which edge it came from), so
// a fixed *duration* like other entrances use would make far entries look
// much faster than near ones. Fixing the speed and deriving the duration
// from the actual distance keeps it consistent. Never below
// SIDE_DIVE_MIN_SWEEP_FRAMES so a very short hop still reads as a deliberate
// swoop rather than a snap.
#define SIDE_DIVE_SWEEP_SPEED_PX     3
#define SIDE_DIVE_MIN_SWEEP_FRAMES   10

// Spawns exactly one batch of SIDE_DIVE_SUBGROUP_SIZE enemies of
// waverFormationKind: SIDE_DIVE_ROW_COUNT rows of 2 side by side (offset
// SIDE_DIVE_COL_GAP apart horizontally), each row entering together from
// the left or right edge of the *whole screen* (chosen independently per
// row) and staggered SIDE_DIVE_ROW_STAGGER_FRAMES apart via startDelay.
// Every member goes through the standard ENEMY_STATE_ENTERING sweep (see
// enemy_spawn()) at a fixed speed (SIDE_DIVE_SWEEP_SPEED_PX, duration
// derived from actual distance -- see e->enterDuration) to its landing
// slot, then enemies_update()'s ENTERING completion sends waver kinds into
// ENEMY_STATE_WAVING for the steep dive instead of ENEMY_STATE_IN_FORMATION.
static void spawnNextSideDiveSubgroup(void)
{
    s16 w = (s16) enemy_widthForKind(waverFormationKind);

    for (u16 row = 0; row < SIDE_DIVE_ROW_COUNT; row++)
    {
        bool fromRight = (bool) (random() & 1);
        s16 startX = fromRight ? (SCREEN_W + w + 10) : (-w - 10);
        // Landing spot: somewhere inside the playfield with enough room for
        // both members side by side, well clear of the play area edges.
        s16 landRangeMin = PLAY_AREA_X_MIN + 10;
        s16 landRangeMax = PLAY_AREA_X_MAX - 10 - SIDE_DIVE_COL_GAP;
        s16 landX = landRangeMin + (s16) (random() % (u16) (landRangeMax - landRangeMin));
        s16 startY = SIDE_DIVE_START_Y;
        s16 delay = (s16) (row * SIDE_DIVE_ROW_STAGGER_FRAMES);

        for (u16 col = 0; col < 2; col++)
        {
            s16 thisLandX = landX + (s16) col * SIDE_DIVE_COL_GAP;
            s16 dist = thisLandX - startX;
            if (dist < 0)
                dist = -dist;
            u16 duration = (u16) (dist / SIDE_DIVE_SWEEP_SPEED_PX);
            if (duration < SIDE_DIVE_MIN_SWEEP_FRAMES)
                duration = SIDE_DIVE_MIN_SWEEP_FRAMES;

            Enemy *e = enemy_spawn(waverFormationKind, startX, startY, thisLandX, startY, delay);
            if (e == NULL)
                continue; // pool full -- shouldn't happen (see MAX_ENEMIES)

            e->isWaver = TRUE;
            e->enterDuration = duration;
            e->vx = FIX16(thisLandX - startX) / (s16) duration;
        }
    }

    waverBatchesSpawned++;
}

static void spawnNextWaverSubgroup(void)
{
    if (currentWaverShape == WAVER_SHAPE_SIDE_DIVE)
        spawnNextSideDiveSubgroup();
    else
        spawnNextGridWeaveSubgroup();
}

void enemy_spawnWaverFormation(EnemyKind kind)
{
    waverFormationKind = kind;
    currentWaverShape = (WaverShape) (interwaveFormationCount % WAVER_SHAPE_COUNT);
    interwaveFormationCount++;
    waverBatchesSpawned = 0;
    waverGapTimer = 0;
    waverFormationActive = TRUE;
    waverKillCount = 0;

    spawnNextWaverSubgroup();
}

// Advances the current inter-wave formation once its on-screen batch is
// fully gone: waits WAVER_BATCH_GAP_FRAMES, then either spawns the next
// batch or, if that was the last one, marks the whole formation done. Called
// every frame from enemies_update() while waverFormationActive.
static void updateWaverFormationSequencing(void)
{
    for (u16 i = 0; i < MAX_ENEMIES; i++)
        if (enemies[i].active && enemies[i].isWaver)
            return; // still something from the current batch on screen

    // The current batch is fully gone.
    if (waverBatchesSpawned >= WAVER_SUBGROUP_COUNT)
    {
        waverFormationActive = FALSE;
        return;
    }

    // Count up from 0 (rather than down from a preset value) so it doesn't
    // matter which frame first notices the batch is clear -- the gap always
    // measures from that point.
    waverGapTimer++;
    if (waverGapTimer >= WAVER_BATCH_GAP_FRAMES)
    {
        waverGapTimer = 0;
        spawnNextWaverSubgroup();
    }
}

bool enemies_waverFormationDone(void)
{
    return !waverFormationActive;
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
