#include "boss.h"
#include "resources.h"
#include "player.h"
#include "bullet.h"
#include "explosion.h"
#include "sfx.h"
#include "score.h"
#include "enemy.h"
#include "turret.h"
#include "music.h"
#include "boss_patterns_generated.h"
#include "boss_mask_generated.h"

// -- body: 64x64, built from a single 2-frame sheet per kind (frame 0 =
// top-left quadrant, frame 1 = bottom-left) -- top-right/bottom-right are
// the same tiles horizontally mirrored via TILE_ATTR_FULL's hflip flag, not
// separate art (see generate_placeholders.py's boss_body()).
#define BOSS_BODY_W 64
#define BOSS_BODY_H 64
#define BOSS_QUAD_W 32
#define BOSS_QUAD_H 32

// -- weak spots: 1-3 pods per kind (see BossDef), offset from the shared
// anchor (bossX,bossY = body's top-left corner) -- same "read the parent's
// position every frame" convention turret.c uses for terrain clumps.
#define WEAKSPOT_W 16
#define WEAKSPOT_H 16
#define MAX_WEAKSPOTS 3
#define WEAKSPOT_HIT_FLASH_FRAMES 6 // short per-hit flash -- deliberately unscaled, see enemy.c's HIT_FLASH_FRAMES

// -- horizontal anchors the boss repositions between (see BossPhase's
// MOVING) -- fixed X points spaced so the 64px-wide body always fits
// within PLAY_AREA_X_MIN/MAX. Shared by every kind. Every move cycles to
// the next one, same as before this file grew the up/down dive below --
// this is the "shifts a bit horizontally" part of each reposition.
#define BOSS_ANCHOR_Y 24 // the "upper part" the boss returns to between dives
#define BOSS_ANCHOR_COUNT 3
static const s16 bossAnchorX[BOSS_ANCHOR_COUNT] = {
    PLAY_AREA_X_MIN + 8,
    (PLAY_AREA_X_MIN + PLAY_AREA_X_MAX) / 2 - BOSS_BODY_W / 2,
    PLAY_AREA_X_MAX - BOSS_BODY_W - 8,
};

// From the top anchor row, the boss only occasionally dives down to a
// random height in this range instead of every reposition -- most
// repositions just shift horizontally along BOSS_ANCHOR_Y (see
// boss_update()'s phase-transition block). Once down, it always returns to
// the top next. Y range bounded so the 64px-tall body always stays within
// the playfield (PLAY_AREA_Y_MAX - BOSS_BODY_H caps the low end).
#define BOSS_DIVE_Y_MIN 40
#define BOSS_DIVE_Y_MAX 136
#define BOSS_DIVE_CHANCE_PCT 25 // 1-in-4 repositions from the top actually dives

// PAL values throughout this file are NTSC * 50/60 (frame counts) or
// NTSC * 1.2 (speeds) -- see game.h's REGION_PICK -- so real-world
// pacing/speed stays the same regardless of the console's actual refresh
// rate.
#define BOSS_MOVE_MIN_FRAMES    REGION_PICK(45, 38) // ~0.75s -- a floor even for a short hop, so repositioning always reads as a slow drift

// Hidden 5-minute limit -- if the fight isn't resolved by then, the boss
// dives off screen and disappears without awarding score (see
// enemies_forceDiveAllOut()'s no-reward wave timeout for the analogous
// regular-wave case). Shared by every kind.
#define BOSS_TIME_LIMIT_FRAMES REGION_PICK(300 * 60, 300 * 50)

typedef enum
{
    BOSS_ATTACK_SPREAD,
    BOSS_ATTACK_BURST,
    BOSS_ATTACK_RADIAL,
    BOSS_ATTACK_CROSS,
    BOSS_ATTACK_WALL,
} BossAttackKind;

// -- shared attack-pattern library: every kind picks an ordered subset of
// these (see BossDef's attackCycle) rather than each needing its own
// bespoke bullet code. Speed/interval tuning is shared across kinds that
// use a given pattern, same as before this roster existed.
#define SPREAD_BULLET_COUNT   5
#define SPREAD_FIRE_INTERVAL  REGION_PICK(60, 50)
#define SPREAD_SPEED          REGION_PICK(FIX16(1.6), FIX16(1.92))

#define BURST_SHOT_COUNT      4
#define BURST_SHOT_INTERVAL   REGION_PICK(8, 7)
#define BURST_COOLDOWN        REGION_PICK(90, 75)
#define BURST_SPEED           REGION_PICK(FIX16(1.8), FIX16(2.16))

#define RADIAL_FIRE_INTERVAL  REGION_PICK(100, 83) // bossRadialVxNtsc/Pal (boss_patterns_generated.h) are already scaled to speed

// Fixed 8-direction volley, not aimed at the player -- a distinct read from
// the aimed SPREAD/BURST patterns. Directions are precomputed unit vectors
// (N/NE/E/SE/S/SW/W/NW) scaled by CROSS_SPEED; no runtime trig needed.
#define CROSS_DIRECTION_COUNT 8
#define CROSS_FIRE_INTERVAL   REGION_PICK(70, 58)
#define CROSS_SPEED           REGION_PICK(FIX16(1.5), FIX16(1.8))
static const fix16 crossDirX[CROSS_DIRECTION_COUNT] = {
    FIX16(0), FIX16(0.7071), FIX16(1), FIX16(0.7071), FIX16(0), FIX16(-0.7071), FIX16(-1), FIX16(-0.7071),
};
static const fix16 crossDirY[CROSS_DIRECTION_COUNT] = {
    FIX16(-1), FIX16(-0.7071), FIX16(0), FIX16(0.7071), FIX16(1), FIX16(0.7071), FIX16(0), FIX16(-0.7071),
};

// A horizontal row of bullets spawned across the boss's width, staggered
// one every WALL_SHOT_INTERVAL frames (same one-shot-per-tick shape as
// BURST), constant slow downward velocity.
#define WALL_BULLET_COUNT     5
#define WALL_SHOT_INTERVAL    REGION_PICK(6, 5)
#define WALL_COOLDOWN         REGION_PICK(110, 92)
#define WALL_SPEED            REGION_PICK(FIX16(1.2), FIX16(1.44))
static const s8 wallOffsetX[WALL_BULLET_COUNT] = {-24, -12, 0, 12, 24};

#define HOMING_FIRE_INTERVAL  REGION_PICK(240, 200) // one homing bullet roughly every 4s, regardless of attack pattern
#define HOMING_INITIAL_SPEED  REGION_PICK(FIX16(1.3), FIX16(1.56)) // must match bullet.c's HOMING_SPEED
#define HOMING_MAX_ALIVE      3   // never more than this many at once (see bullet_countActiveHoming())
#define HOMING_RETRY_FRAMES   REGION_PICK(30, 25)  // how soon to check again if the cap was hit

typedef enum
{
    BOSS_PHASE_MOVING,
    BOSS_PHASE_ATTACKING,
} BossPhase;

typedef struct
{
    s16 hp;
    u16 hitFlashTimer;
    bool destroyed;
    Sprite *sprite;
} BossWeakSpot;

// -- per-kind data (see boss.h's BossKind) -- everything that should
// plausibly vary per boss (art, weak-spot count/placement/HP, attack mix,
// move speed, attack-phase duration) lives here instead of a module-level
// #define, so the roster is data-driven rather than 5 copy-pasted files.
typedef struct
{
    const SpriteDefinition *bodySprite;
    const SpriteDefinition *weakSpotSprite;
    const Palette *palette;
    u8 weakSpotCount;
    s16 weakSpotOffsetX[MAX_WEAKSPOTS];
    s16 weakSpotOffsetY[MAX_WEAKSPOTS];
    s16 weakSpotHP;
    const BossAttackKind *attackCycle;
    u8 attackCycleLen;
    // NTSC/PAL pairs, not single REGION_PICK(...) values -- REGION_PICK's
    // IS_PAL_SYSTEM check is a runtime read, which can't appear inside a
    // static const initializer (same restriction as sfx.c's SfxStep
    // tables). beginMove()/beginAttackPhase() pick between these fields
    // themselves instead, which is the same "two precalculated numbers,
    // runtime choice" shape, just resolved at the read site rather than
    // baked into the table.
    //
    // Two separate speeds (see beginMove()'s `slow` parameter): moveSpeed
    // covers the intro/exit swoop and horizontal-only shifts along the top
    // row (brisk -- these read as the boss actively maneuvering), while
    // diveSpeed only applies to the vertical reposition when the boss
    // dives to a random height or climbs back to the top (deliberately
    // slow, a telegraphed drift rather than a dash -- see boss_update()'s
    // phase-transition block).
    fix16 moveSpeedNtsc; // px/frame -- duration derives from distance (see beginMove())
    fix16 moveSpeedPal;
    fix16 diveSpeedNtsc;
    fix16 diveSpeedPal;
    u16 attackPhaseFramesNtsc;
    u16 attackPhaseFramesPal;
} BossDef;

static const BossAttackKind cycleA[] = {BOSS_ATTACK_SPREAD, BOSS_ATTACK_BURST, BOSS_ATTACK_RADIAL};
static const BossAttackKind cycleB[] = {BOSS_ATTACK_BURST, BOSS_ATTACK_WALL, BOSS_ATTACK_CROSS};
static const BossAttackKind cycleC[] = {BOSS_ATTACK_RADIAL, BOSS_ATTACK_CROSS, BOSS_ATTACK_SPREAD};
static const BossAttackKind cycleD[] = {BOSS_ATTACK_WALL, BOSS_ATTACK_BURST, BOSS_ATTACK_RADIAL};
static const BossAttackKind cycleE[] = {BOSS_ATTACK_SPREAD, BOSS_ATTACK_WALL, BOSS_ATTACK_CROSS};

// Meaningfully tougher than any regular enemy (HP_BIG in enemy.c is 100) --
// a boss appearing every 3 waves should be a real fight, not a single BIG
// enemy split into parts. At the player's ~7.5 shots/sec fire rate
// (FIRE_COOLDOWN in player.c) 90 HP is roughly 36s of sustained,
// accurately-aimed fire per pod (60% of the original 150 -- toned down so a
// fight doesn't drag) -- comfortably inside the hidden 5-minute limit even
// with dodging cutting into actual hit uptime. Kind C (3 pods) and D (1 big
// pod) scale the per-pod HP so the total effort stays in the same ballpark.
static const BossDef bossDefs[BOSS_KIND_COUNT] = {
    // A -- crimson diamond hull, 2 side-flanking pods (the original layout).
    {
        &spr_boss_body_a, &spr_boss_weakspot_a, &palette_boss_a,
        2,
        {-8, BOSS_BODY_W - WEAKSPOT_W + 8, 0},
        {(BOSS_BODY_H - WEAKSPOT_H) / 2, (BOSS_BODY_H - WEAKSPOT_H) / 2, 0},
        90,
        cycleA, 3,
        FIX16(2), FIX16(2.4), FIX16(0.5), FIX16(0.6), 240, 200,
    },
    // B -- violet blocky saucer hull, 2 pods stacked top/bottom.
    {
        &spr_boss_body_b, &spr_boss_weakspot_b, &palette_boss_b,
        2,
        {(BOSS_BODY_W - WEAKSPOT_W) / 2, (BOSS_BODY_W - WEAKSPOT_W) / 2, 0},
        {-8, BOSS_BODY_H - WEAKSPOT_H + 8, 0},
        90,
        cycleB, 3,
        FIX16(2), FIX16(2.4), FIX16(0.5), FIX16(0.6), 180, 150,
    },
    // C -- emerald round hull, 3 pods in a triangular arrangement.
    {
        &spr_boss_body_c, &spr_boss_weakspot_c, &palette_boss_c,
        3,
        {-8, BOSS_BODY_W - WEAKSPOT_W + 8, (BOSS_BODY_W - WEAKSPOT_W) / 2},
        {16, 16, 48},
        66,
        cycleC, 3,
        FIX16(3), FIX16(3.6), FIX16(0.7), FIX16(0.84), 220, 183,
    },
    // D -- azure spike hull, a single large, tougher central pod.
    {
        &spr_boss_body_d, &spr_boss_weakspot_d, &palette_boss_d,
        1,
        {(BOSS_BODY_W - WEAKSPOT_W) / 2, 0, 0},
        {(BOSS_BODY_H - WEAKSPOT_H) / 2, 0, 0},
        180,
        cycleD, 3,
        FIX16(1), FIX16(1.2), FIX16(0.3), FIX16(0.36), 260, 217,
    },
    // E -- amber wing hull, 2 pods spread wide at the wingtips.
    {
        &spr_boss_body_e, &spr_boss_weakspot_e, &palette_boss_e,
        2,
        {-16, BOSS_BODY_W - WEAKSPOT_W + 16, 0},
        {(BOSS_BODY_H - WEAKSPOT_H) / 2, (BOSS_BODY_H - WEAKSPOT_H) / 2, 0},
        90,
        cycleE, 3,
        FIX16(3), FIX16(3.6), FIX16(0.7), FIX16(0.84), 200, 167,
    },
};

// -- tile loading: boss's own VRAM tiles, based right after bullet.c's
// (TILE_USER_INDEX+228: +1 player bullet, +1 enemy bullet, +8 for the
// homing bullet's 2-frame x 4-tile-per-frame sheet -- see bullet.c's
// BULLET_TILE_BASE) -- so this starts at +238 or later. Only one kind's
// tiles are ever resident at a time (loaded fresh in boss_begin(), not
// boss_init() -- see its comment), so this budget only needs to fit a
// single kind's body+weak-spot sheets, not all 5 kinds' worth.
#define BOSS_TILE_BASE (TILE_USER_INDEX + 238)

static u16 bodyTLTile, bodyBLTile; // frame 0/1 of the active kind's body sheet
static u16 weakSpotNormalTile, weakSpotFlashTile, weakSpotDestroyedTile;

// Pause after the killing blow before the encounter actually ends (see
// triggerDeath()) -- lets the death scream/explosions play out and gives
// the player a breather instead of formation.c cutting straight to the
// next wave the instant the last weak spot dies. Shared by every kind.
#define BOSS_DEATH_DELAY_FRAMES REGION_PICK(90, 75) // 1.5s
static bool dying;
static u16 deathDelayTimer;

static bool bossActive;
static BossKind currentKind;
static fix16 bossX, bossY; // anchor: body's top-left corner
static fix16 bossVX, bossVY;
static u16 moveTimer, moveDuration;
static s16 moveTargetX, moveTargetY;
static bool exitingAfterMove; // TRUE once the hidden timer has expired -- next arrival ends the fight, no score
static u8 anchorIndex;
static bool bossAtTop; // TRUE while at the top row (next reposition rolls a chance to dive); FALSE while down (next reposition always returns to the top)

static BossPhase phase;
static u16 phaseTimer; // frames remaining in the current ATTACKING phase
static BossAttackKind currentAttack;
static u8 attackCycleIndex; // index into bossDefs[currentKind].attackCycle
static u16 lifeTimer;

static Sprite *bodyTL, *bodyTR, *bodyBL, *bodyBR;
static BossWeakSpot weakSpots[MAX_WEAKSPOTS];

// Per-attack-kind firing state.
static u16 spreadFireTimer;
static u8 burstShotsLeft;
static u16 burstTimer;
static u16 radialFireTimer;
static u16 crossFireTimer;
static u8 wallShotsLeft;
static u16 wallTimer;
static u16 homingFireTimer;

void boss_init(void)
{
    bossActive = FALSE;

    // Sprite handles/VRAM tiles intentionally NOT loaded here -- see
    // boss_begin(), which loads the chosen kind's tiles and creates its
    // sprites once per encounter rather than keeping one fixed art set
    // forever, so the roster's 5 kinds can share the same VRAM tile range
    // and sprite handles instead of each needing its own permanent slice.
    bodyTL = bodyTR = bodyBL = bodyBR = NULL;
    for (u16 i = 0; i < MAX_WEAKSPOTS; i++)
        weakSpots[i].sprite = NULL;
}

// One-shot glide setup: computes vx/vy once (a single division, not
// per-tick -- same convention as every other entrance/dive in this
// codebase) from the current position to (targetX,targetY), so travel time
// scales with distance instead of being a fixed duration that would look
// too fast/slow depending on how far it's going. `slow` selects the
// active kind's diveSpeed (vertical repositions: dive down/climb back to
// the top) instead of its brisker moveSpeed (intro/exit swoops and
// horizontal-only shifts along the top row) -- see BossDef's comment.
static void beginMove(s16 targetX, s16 targetY, bool slow)
{
    s16 fromX = F16_toInt(bossX);
    s16 fromY = F16_toInt(bossY);
    s16 dx = targetX - fromX;
    s16 dy = targetY - fromY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    s16 dist = (dx > dy) ? dx : dy;

    const BossDef *def = &bossDefs[currentKind];
    fix16 speed;
    if (slow)
        speed = IS_PAL_SYSTEM ? def->diveSpeedPal : def->diveSpeedNtsc;
    else
        speed = IS_PAL_SYSTEM ? def->moveSpeedPal : def->moveSpeedNtsc;
    u16 duration = (u16) F16_toInt(F16_div(FIX16(dist), speed));
    if (duration < BOSS_MOVE_MIN_FRAMES)
        duration = BOSS_MOVE_MIN_FRAMES;

    moveTargetX = targetX;
    moveTargetY = targetY;
    moveDuration = duration;
    moveTimer = 0;
    bossVX = FIX16(targetX - fromX) / (s16) duration;
    bossVY = FIX16(targetY - fromY) / (s16) duration;
    phase = BOSS_PHASE_MOVING;
}

static void beginAttackPhase(void)
{
    phase = BOSS_PHASE_ATTACKING;
    const BossDef *def = &bossDefs[currentKind];
    phaseTimer = IS_PAL_SYSTEM ? def->attackPhaseFramesPal : def->attackPhaseFramesNtsc;
    spreadFireTimer = SPREAD_FIRE_INTERVAL / 2; // stagger the first shot in a bit, not instantly on arrival
    burstShotsLeft = 0;
    burstTimer = BURST_COOLDOWN;
    radialFireTimer = RADIAL_FIRE_INTERVAL;
    crossFireTimer = CROSS_FIRE_INTERVAL / 2;
    wallShotsLeft = 0;
    wallTimer = WALL_COOLDOWN;
}

void boss_begin(BossKind kind)
{
    currentKind = kind;
    const BossDef *def = &bossDefs[kind];

    bossActive = TRUE;

    // Overrides whatever ingame track music_startIngame() picked -- see
    // formation.c, which resumes it via music_resumeIngame() once this
    // encounter ends.
    music_startBoss();

    // Reclaim hardware sprite slots from the two pools guaranteed idle
    // during a boss fight (see their own comments for why this is safe).
    enemies_releaseIdleSprites();
    turrets_releaseIdleSprites();

    // Load this kind's body/weak-spot tiles into the shared boss VRAM
    // range and swap its colors onto hardware PAL3 -- see BOSS_TILE_BASE's
    // comment on why only one kind's art needs to be resident at a time.
    u16 totalTiles;
    u16 base = BOSS_TILE_BASE;

    u16 **idx = SPR_loadAllFrames(def->bodySprite, base, &totalTiles);
    bodyTLTile = idx[0][0];
    bodyBLTile = idx[1][0];
    MEM_free(idx);
    base += totalTiles;

    idx = SPR_loadAllFrames(def->weakSpotSprite, base, &totalTiles);
    weakSpotNormalTile = idx[0][0];
    weakSpotFlashTile = idx[1][0];
    weakSpotDestroyedTile = idx[2][0];
    MEM_free(idx);

    PAL_setPalette(PAL_BOSS, def->palette->data, DMA);

    lifeTimer = BOSS_TIME_LIMIT_FRAMES;
    exitingAfterMove = FALSE;
    dying = FALSE;
    anchorIndex = 0;
    bossAtTop = TRUE; // the intro swoop below lands at BOSS_ANCHOR_Y
    attackCycleIndex = 0;
    currentAttack = def->attackCycle[0];
    homingFireTimer = HOMING_FIRE_INTERVAL;

    s16 startX = bossAnchorX[0];
    s16 startY = -BOSS_BODY_H - 10;
    bossX = FIX16(startX);
    bossY = FIX16(startY);

    for (u16 i = 0; i < def->weakSpotCount; i++)
    {
        weakSpots[i].hp = def->weakSpotHP;
        weakSpots[i].hitFlashTimer = 0;
        weakSpots[i].destroyed = FALSE;
    }

    s16 tlx = startX, tly = startY;
    s16 trx = startX + BOSS_QUAD_W, try_ = startY;
    s16 blx = startX, bly = startY + BOSS_QUAD_H;
    s16 brx = startX + BOSS_QUAD_W, bry = startY + BOSS_QUAD_H;

    u16 tlAttr = TILE_ATTR_FULL(PAL_BOSS, FALSE, FALSE, FALSE, bodyTLTile);
    u16 trAttr = TILE_ATTR_FULL(PAL_BOSS, FALSE, FALSE, TRUE, bodyTLTile); // hflip of TL
    u16 blAttr = TILE_ATTR_FULL(PAL_BOSS, FALSE, FALSE, FALSE, bodyBLTile);
    u16 brAttr = TILE_ATTR_FULL(PAL_BOSS, FALSE, FALSE, TRUE, bodyBLTile); // hflip of BL

    if (bodyTL == NULL) bodyTL = SPR_addSpriteEx(def->bodySprite, tlx, tly, tlAttr, 0);
    else { SPR_setDefinition(bodyTL, def->bodySprite); SPR_setVRAMTileIndex(bodyTL, bodyTLTile); SPR_setHFlip(bodyTL, FALSE); SPR_setVisibility(bodyTL, VISIBLE); SPR_setPosition(bodyTL, tlx, tly); }

    if (bodyTR == NULL) bodyTR = SPR_addSpriteEx(def->bodySprite, trx, try_, trAttr, 0);
    else { SPR_setDefinition(bodyTR, def->bodySprite); SPR_setVRAMTileIndex(bodyTR, bodyTLTile); SPR_setVisibility(bodyTR, VISIBLE); SPR_setPosition(bodyTR, trx, try_); }
    SPR_setHFlip(bodyTR, TRUE);

    if (bodyBL == NULL) bodyBL = SPR_addSpriteEx(def->bodySprite, blx, bly, blAttr, 0);
    else { SPR_setDefinition(bodyBL, def->bodySprite); SPR_setVRAMTileIndex(bodyBL, bodyBLTile); SPR_setHFlip(bodyBL, FALSE); SPR_setVisibility(bodyBL, VISIBLE); SPR_setPosition(bodyBL, blx, bly); }

    if (bodyBR == NULL) bodyBR = SPR_addSpriteEx(def->bodySprite, brx, bry, brAttr, 0);
    else { SPR_setDefinition(bodyBR, def->bodySprite); SPR_setVRAMTileIndex(bodyBR, bodyBLTile); SPR_setVisibility(bodyBR, VISIBLE); SPR_setPosition(bodyBR, brx, bry); }
    SPR_setHFlip(bodyBR, TRUE);

    for (u16 i = 0; i < def->weakSpotCount; i++)
    {
        s16 wx = startX + def->weakSpotOffsetX[i];
        s16 wy = startY + def->weakSpotOffsetY[i];
        u16 attr = TILE_ATTR_FULL(PAL_BOSS, FALSE, FALSE, FALSE, weakSpotNormalTile);
        if (weakSpots[i].sprite == NULL)
        {
            // Explicit SPR_setDepth() rather than SPR_FLAG_INSERT_HEAD:
            // that flag splices the sprite straight to the head of SGDK's
            // internal list at creation time, bypassing SPR_setDepth's sort
            // entirely -- fine on its own, but it also sets depth to the
            // exact SPR_MIN_DEPTH, which ties with anything else at that
            // same extreme (score.c's GAME OVER letters use
            // SPR_setAlwaysOnTop(), also SPR_MIN_DEPTH). SGDK's sort always
            // resolves an exact depth tie by placing whichever sprite last
            // changed depth *behind* the one already there, so once a weak
            // spot sprite existed, the letters silently lost that tie and
            // drew behind it. SPR_MIN_DEPTH + 1 still draws in front of the
            // body (default SPR_MAX_DEPTH) via a genuine inequality, without
            // ever tying against SPR_MIN_DEPTH again.
            weakSpots[i].sprite = SPR_addSpriteEx(def->weakSpotSprite, wx, wy, attr, 0);
            SPR_setDepth(weakSpots[i].sprite, SPR_MIN_DEPTH + 1);
        }
        else
        {
            SPR_setDefinition(weakSpots[i].sprite, def->weakSpotSprite);
            SPR_setVRAMTileIndex(weakSpots[i].sprite, weakSpotNormalTile);
            SPR_setHFlip(weakSpots[i].sprite, FALSE);
            SPR_setVisibility(weakSpots[i].sprite, VISIBLE);
            SPR_setPosition(weakSpots[i].sprite, wx, wy);
        }
    }
    // Any leftover pod sprite from a previous, higher-weak-spot-count kind
    // (e.g. kind C's 3rd pod, if the last encounter was C and this one
    // isn't) stays hidden rather than released -- it's just not touched
    // above, so hide it explicitly here.
    for (u16 i = def->weakSpotCount; i < MAX_WEAKSPOTS; i++)
        if (weakSpots[i].sprite != NULL)
            SPR_setVisibility(weakSpots[i].sprite, HIDDEN);

    beginMove(bossAnchorX[0], BOSS_ANCHOR_Y, FALSE); // intro swoop -- brisk, not the slow dive speed
}

// Releases every one of the boss's own sprite handles (see boss_begin()'s
// comment on why they're created/released per-encounter instead of kept
// forever) and nulls them so a future boss_begin() recreates cleanly.
static void releaseSprites(void)
{
    if (bodyTL != NULL) { SPR_releaseSprite(bodyTL); bodyTL = NULL; }
    if (bodyTR != NULL) { SPR_releaseSprite(bodyTR); bodyTR = NULL; }
    if (bodyBL != NULL) { SPR_releaseSprite(bodyBL); bodyBL = NULL; }
    if (bodyBR != NULL) { SPR_releaseSprite(bodyBR); bodyBR = NULL; }
    for (u16 i = 0; i < MAX_WEAKSPOTS; i++)
        if (weakSpots[i].sprite != NULL) { SPR_releaseSprite(weakSpots[i].sprite); weakSpots[i].sprite = NULL; }
}

static void endFight(void)
{
    bossActive = FALSE;
    releaseSprites();
}

bool boss_isActive(void)
{
    return bossActive;
}

void boss_hideAll(void)
{
    if (!bossActive)
        return;

    bossActive = FALSE;
    if (bodyTL != NULL) SPR_setVisibility(bodyTL, HIDDEN);
    if (bodyTR != NULL) SPR_setVisibility(bodyTR, HIDDEN);
    if (bodyBL != NULL) SPR_setVisibility(bodyBL, HIDDEN);
    if (bodyBR != NULL) SPR_setVisibility(bodyBR, HIDDEN);
    for (u16 i = 0; i < MAX_WEAKSPOTS; i++)
        if (weakSpots[i].sprite != NULL) SPR_setVisibility(weakSpots[i].sprite, HIDDEN);
}

AABB boss_getBounds(void)
{
    // Broad box covering the body plus every flanking pod (see
    // weakSpotOffsetX's spill past the body's own edges) -- used only for
    // the player-ram-death check, not bullet-vs-weak-spot precision.
    AABB box = {F16_toInt(bossX) - 16, F16_toInt(bossY), BOSS_BODY_W + 32, BOSS_BODY_H};
    return box;
}

bool boss_hitTestPixel(s16 worldX, s16 worldY)
{
    s16 localX = worldX - F16_toInt(bossX);
    s16 localY = worldY - F16_toInt(bossY);
    if (localX >= 0 && localX < BOSS_BODY_MASK_SIZE && localY >= 0 && localY < BOSS_BODY_MASK_SIZE)
    {
        bool rightHalf = localX >= BOSS_QUAD_W;
        u32 row = rightHalf ? bossBodyMaskHi[currentKind][localY] : bossBodyMaskLo[currentKind][localY];
        u16 bit = rightHalf ? (u16) (localX - BOSS_QUAD_W) : (u16) localX;
        if ((row >> bit) & 1)
            return TRUE;
    }

    // Flanking pods still count as a ram hazard even where they stick out
    // past the body's own 64x64 area (see boss_getBounds()) -- plain AABB
    // here, pods stay unmasked (see boss.h).
    const BossDef *def = &bossDefs[currentKind];
    for (u16 i = 0; i < def->weakSpotCount; i++)
    {
        if (weakSpots[i].destroyed)
            continue;

        AABB box = boss_weakSpotBounds(i);
        if (worldX >= box.x && worldX < box.x + box.w && worldY >= box.y && worldY < box.y + box.h)
            return TRUE;
    }

    return FALSE;
}

u16 boss_weakSpotCount(void)
{
    return bossDefs[currentKind].weakSpotCount;
}

AABB boss_weakSpotBounds(u16 index)
{
    if (weakSpots[index].destroyed)
    {
        AABB empty = {0, 0, 0, 0};
        return empty;
    }

    const BossDef *def = &bossDefs[currentKind];
    AABB box = {
        F16_toInt(bossX) + def->weakSpotOffsetX[index],
        F16_toInt(bossY) + def->weakSpotOffsetY[index],
        WEAKSPOT_W, WEAKSPOT_H,
    };
    return box;
}

// Staggered across most of BOSS_DEATH_DELAY_FRAMES (the 1.5s breath before
// the fight actually ends -- see boss_update()'s dying state) rather than
// over in a fraction of a second, so the death reads as a bigger, more
// drawn-out demise than a regular enemy's single/BIG's 5-burst explosion.
#define DEATH_EXPLOSION_COUNT    10
#define DEATH_EXPLOSION_STAGGER  8 // frames between each explosion starting

static void triggerDeath(void)
{
    sfx_play_bossDeathScream();

    // The boss is defeated -- its still-in-flight homing bullets shouldn't
    // remain a live hazard through the death-scream/explosion sequence (or
    // outlive the fight entirely and greet the next wave).
    bullet_clearHoming();

    s16 x = F16_toInt(bossX);
    s16 y = F16_toInt(bossY);
    for (u16 i = 0; i < DEATH_EXPLOSION_COUNT; i++)
    {
        s16 cx = x + (s16) (random() % BOSS_BODY_W);
        s16 cy = y + (s16) (random() % BOSS_BODY_H);
        explosion_spawnAtDelayed(cx, cy, i * DEATH_EXPLOSION_STAGGER);
    }

    score_addBossKill();
    dying = TRUE;
    deathDelayTimer = BOSS_DEATH_DELAY_FRAMES;
}

void boss_hitWeakSpot(u16 index, s16 damage)
{
    BossWeakSpot *ws = &weakSpots[index];
    if (ws->destroyed)
        return;

    ws->hp -= damage;
    if (ws->hp <= 0)
    {
        ws->destroyed = TRUE;
        ws->hitFlashTimer = 0;
        SPR_setVRAMTileIndex(ws->sprite, weakSpotDestroyedTile);

        bool allDestroyed = TRUE;
        for (u16 i = 0; i < bossDefs[currentKind].weakSpotCount; i++)
            if (!weakSpots[i].destroyed) { allDestroyed = FALSE; break; }
        if (allDestroyed)
            triggerDeath();
        return;
    }

    ws->hitFlashTimer = WEAKSPOT_HIT_FLASH_FRAMES;
    SPR_setVRAMTileIndex(ws->sprite, weakSpotFlashTile);
    score_addHit(); // small reward for a landed shot that didn't finish the job
}

// Normalizes (dx,dy) to a unit vector scaled by speed -- one-shot per call
// (fired at most a few times a second, never per-tick), same normalize
// approach as enemy.c's fireAimedShotAt()/turret.c's fireOneShot().
static void aimAt(fix16 fromX, fix16 fromY, fix16 speed, fix16 *outVx, fix16 *outVy)
{
    fix16 dx = player.x - fromX;
    fix16 dy = player.y - fromY;
    if (dy > -FIX16(20) && dy < FIX16(20))
        dy = (dy < 0) ? -FIX16(20) : FIX16(20);

    fix16 dist = (fix16) getApproximatedDistance(dx, dy);
    *outVx = F16_mul(F16_div(dx, dist), speed);
    *outVy = F16_mul(F16_div(dy, dist), speed);
}

static void fireSpread(fix16 originX, fix16 originY)
{
    fix16 aimVx, aimVy;
    aimAt(originX, originY, SPREAD_SPEED, &aimVx, &aimVy);

    // Perpendicular to the aim direction, so each bullet in the fan is the
    // aim vector plus a scaled sideways nudge -- not a true rotation, but
    // reads fine as a fan of shots and needs no trig at all.
    fix16 perpVx = -aimVy;
    fix16 perpVy = aimVx;

    static const s8 spreadSteps[SPREAD_BULLET_COUNT] = {-2, -1, 0, 1, 2};
    for (u16 i = 0; i < SPREAD_BULLET_COUNT; i++)
    {
        s8 k = spreadSteps[i];
        fix16 vx = aimVx + F16_mul(perpVx, FIX16(0.3)) * k;
        fix16 vy = aimVy + F16_mul(perpVy, FIX16(0.3)) * k;
        bullet_spawn_enemy(originX, originY, vx, vy);
    }
}

static void fireCross(fix16 originX, fix16 originY)
{
    for (u16 i = 0; i < CROSS_DIRECTION_COUNT; i++)
        bullet_spawn_enemy(originX, originY, F16_mul(crossDirX[i], CROSS_SPEED), F16_mul(crossDirY[i], CROSS_SPEED));
}

static void updateAttack(fix16 originX, fix16 originY)
{
    switch (currentAttack)
    {
        case BOSS_ATTACK_SPREAD:
            if (spreadFireTimer > 0)
                spreadFireTimer--;
            else
            {
                fireSpread(originX, originY);
                spreadFireTimer = SPREAD_FIRE_INTERVAL;
            }
            break;

        case BOSS_ATTACK_BURST:
            if (burstShotsLeft > 0)
            {
                if (burstTimer > 0)
                    burstTimer--;
                else
                {
                    fix16 vx, vy;
                    aimAt(originX, originY, BURST_SPEED, &vx, &vy);
                    bullet_spawn_enemy(originX, originY, vx, vy);
                    burstShotsLeft--;
                    burstTimer = BURST_SHOT_INTERVAL;
                }
            }
            else if (burstTimer > 0)
            {
                burstTimer--;
            }
            else
            {
                burstShotsLeft = BURST_SHOT_COUNT - 1;
                fix16 vx, vy;
                aimAt(originX, originY, BURST_SPEED, &vx, &vy);
                bullet_spawn_enemy(originX, originY, vx, vy);
                burstTimer = BURST_SHOT_INTERVAL;
            }
            break;

        case BOSS_ATTACK_RADIAL:
            if (radialFireTimer > 0)
                radialFireTimer--;
            else
            {
                // Picks the NTSC/PAL-scaled direction table matching the
                // console's actual region (see game.h's REGION_PICK) --
                // both arrays share the same element count/type, just
                // scaled to a different speed, so a single pointer works
                // for either (unlike the interwave paths' differently
                // *sized* NTSC/PAL arrays -- see enemy.c's waverPathAt()).
                const fix16 *vxTable = IS_PAL_SYSTEM ? bossRadialVxPal : bossRadialVxNtsc;
                const fix16 *vyTable = IS_PAL_SYSTEM ? bossRadialVyPal : bossRadialVyNtsc;
                for (u16 i = 0; i < BOSS_RADIAL_DIRECTION_COUNT; i++)
                    bullet_spawn_enemy(originX, originY, vxTable[i], vyTable[i]);
                radialFireTimer = RADIAL_FIRE_INTERVAL;
            }
            break;

        case BOSS_ATTACK_CROSS:
            if (crossFireTimer > 0)
                crossFireTimer--;
            else
            {
                fireCross(originX, originY);
                crossFireTimer = CROSS_FIRE_INTERVAL;
            }
            break;

        case BOSS_ATTACK_WALL:
            if (wallShotsLeft > 0)
            {
                if (wallTimer > 0)
                    wallTimer--;
                else
                {
                    u16 shotIndex = WALL_BULLET_COUNT - wallShotsLeft;
                    bullet_spawn_enemy(originX + FIX16(wallOffsetX[shotIndex]), originY, 0, WALL_SPEED);
                    wallShotsLeft--;
                    wallTimer = WALL_SHOT_INTERVAL;
                }
            }
            else if (wallTimer > 0)
            {
                wallTimer--;
            }
            else
            {
                wallShotsLeft = WALL_BULLET_COUNT - 1;
                bullet_spawn_enemy(originX + FIX16(wallOffsetX[0]), originY, 0, WALL_SPEED);
                wallTimer = WALL_SHOT_INTERVAL;
            }
            break;
    }

    if (homingFireTimer > 0)
    {
        homingFireTimer--;
    }
    else if (bullet_countActiveHoming() >= HOMING_MAX_ALIVE)
    {
        // Already at the cap (player hasn't shot enough of them down yet)
        // -- check back again shortly instead of waiting a full
        // HOMING_FIRE_INTERVAL, so a new one appears promptly once room
        // frees up.
        homingFireTimer = HOMING_RETRY_FRAMES;
    }
    else
    {
        fix16 vx, vy;
        aimAt(originX, originY, HOMING_INITIAL_SPEED, &vx, &vy);
        bullet_spawn_enemy_homing(originX, originY, vx, vy);
        homingFireTimer = HOMING_FIRE_INTERVAL;
    }
}

void boss_update(void)
{
    if (!bossActive)
        return;

    if (dying)
    {
        // Frozen in place (every weak spot already shows its destroyed
        // husk) while the death scream/staggered explosions play out --
        // see triggerDeath(). boss_isActive() stays TRUE for this whole
        // delay, so formation.c doesn't cut to the next wave early. Left
        // running even during game over (unlike everything below) --
        // formation_update() itself is frozen by then (see its own
        // player_isGameOver() check), so endFight() clearing bossActive
        // here can't cause a new wave/encounter to start.
        if (deathDelayTimer > 0)
        {
            deathDelayTimer--;
            return;
        }
        endFight();
        return;
    }

    // Once the player is out of lives, the boss just freezes in place --
    // no life-timer countdown, no forced exit dive, no repositioning dive,
    // no new attacks -- same "wave progression stops, action already in
    // flight keeps playing out" rule as formation.c's own
    // player_isGameOver() freeze. Bullets it already fired keep moving
    // (bullets_update() in main.c isn't gated on this), just no new ones
    // get added.
    if (player_isGameOver())
        return;

    if (!exitingAfterMove)
    {
        if (lifeTimer > 0)
            lifeTimer--;

        if (lifeTimer == 0)
        {
            exitingAfterMove = TRUE;
            beginMove(F16_toInt(bossX), SCREEN_H + BOSS_BODY_H, FALSE); // exit swoop -- brisk, same as the intro
        }
    }

    const BossDef *def = &bossDefs[currentKind];

    if (phase == BOSS_PHASE_MOVING)
    {
        bossX += bossVX;
        bossY += bossVY;
        moveTimer++;

        if (moveTimer >= moveDuration)
        {
            bossX = FIX16(moveTargetX);
            bossY = FIX16(moveTargetY);

            if (exitingAfterMove)
            {
                endFight(); // timed out -- no score, see boss.h's comment
                return;
            }

            beginAttackPhase();
        }
    }
    else // BOSS_PHASE_ATTACKING
    {
        fix16 originX = bossX + FIX16(BOSS_BODY_W / 2 - 4);
        fix16 originY = bossY + FIX16(BOSS_BODY_H - 4);
        updateAttack(originX, originY);

        phaseTimer--;
        if (phaseTimer == 0)
        {
            anchorIndex = (anchorIndex + 1) % BOSS_ANCHOR_COUNT;
            attackCycleIndex = (attackCycleIndex + 1) % def->attackCycleLen;
            currentAttack = def->attackCycle[attackCycleIndex];

            // From the top, only occasionally dive down to a random height
            // (see BOSS_DIVE_CHANCE_PCT) -- most repositions just shift to
            // the next horizontal anchor along the top row instead. Once
            // down, always return to the top next. `verticalMove` is TRUE
            // exactly when this reposition actually changes height (dive
            // or climb-back), so beginMove() uses the slow diveSpeed only
            // then -- a pure horizontal shift along the top row stays at
            // the brisk moveSpeed.
            s16 targetY;
            bool verticalMove;
            if (bossAtTop)
            {
                bool dive = (random() % 100) < BOSS_DIVE_CHANCE_PCT;
                if (dive)
                {
                    targetY = BOSS_DIVE_Y_MIN + (s16) (random() % (BOSS_DIVE_Y_MAX - BOSS_DIVE_Y_MIN + 1));
                    bossAtTop = FALSE;
                    verticalMove = TRUE;
                }
                else
                {
                    targetY = BOSS_ANCHOR_Y;
                    verticalMove = FALSE;
                }
            }
            else
            {
                targetY = BOSS_ANCHOR_Y;
                bossAtTop = TRUE;
                verticalMove = TRUE;
            }
            beginMove(bossAnchorX[anchorIndex], targetY, verticalMove);
        }
    }

    s16 x = F16_toInt(bossX);
    s16 y = F16_toInt(bossY);
    SPR_setPosition(bodyTL, x, y);
    SPR_setPosition(bodyTR, x + BOSS_QUAD_W, y);
    SPR_setPosition(bodyBL, x, y + BOSS_QUAD_H);
    SPR_setPosition(bodyBR, x + BOSS_QUAD_W, y + BOSS_QUAD_H);

    for (u16 i = 0; i < def->weakSpotCount; i++)
    {
        BossWeakSpot *ws = &weakSpots[i];
        if (ws->hitFlashTimer > 0)
        {
            ws->hitFlashTimer--;
            // Don't stomp the destroyed-husk tile if this weak spot died
            // while a still-ticking flash from an earlier (non-fatal) hit
            // was in progress.
            if (ws->hitFlashTimer == 0 && !ws->destroyed)
                SPR_setVRAMTileIndex(ws->sprite, weakSpotNormalTile);
        }
        SPR_setPosition(ws->sprite, x + def->weakSpotOffsetX[i], y + def->weakSpotOffsetY[i]);
    }
}
