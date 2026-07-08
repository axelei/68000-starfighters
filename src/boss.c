#include "boss.h"
#include "resources.h"
#include "player.h"
#include "bullet.h"
#include "explosion.h"
#include "sfx.h"
#include "score.h"
#include "enemy.h"
#include "turret.h"
#include "boss_patterns_generated.h"

// -- body: 64x64, built from a single 2-frame sheet (spr_boss_body: frame 0
// = top-left quadrant, frame 1 = bottom-left) -- top-right/bottom-right are
// the same tiles horizontally mirrored via TILE_ATTR_FULL's hflip flag, not
// separate art (see generate_placeholders.py's boss_body()).
#define BOSS_BODY_W 64
#define BOSS_BODY_H 64
#define BOSS_QUAD_W 32
#define BOSS_QUAD_H 32

// -- weak spots: 2 pods flanking the body, offset from the shared anchor
// (bossX,bossY = body's top-left corner) -- same "read the parent's
// position every frame" convention turret.c uses for terrain clumps.
#define WEAKSPOT_W 16
#define WEAKSPOT_H 16
// Meaningfully tougher than any regular enemy (HP_BIG in enemy.c is 100) --
// a boss appearing only every 5 waves should be a real fight, not a single
// BIG enemy split into two parts. At the player's ~7.5 shots/sec fire rate
// (FIRE_COOLDOWN in player.c) this is roughly a minute of sustained,
// accurately-aimed fire per pod -- comfortably inside the hidden 5-minute
// limit even with dodging cutting into actual hit uptime.
#define WEAKSPOT_HP 150
#define WEAKSPOT_HIT_FLASH_FRAMES 6
static const s16 weakSpotOffsetX[2] = {-8, BOSS_BODY_W - WEAKSPOT_W + 8};
static const s16 weakSpotOffsetY[2] = {(BOSS_BODY_H - WEAKSPOT_H) / 2, (BOSS_BODY_H - WEAKSPOT_H) / 2};

// -- anchors the boss repositions between (see BossPhase's MOVING) -- fixed
// points near the top of the playfield, spaced so the 64px-wide body always
// fits within PLAY_AREA_X_MIN/MAX.
#define BOSS_ANCHOR_Y 24
#define BOSS_ANCHOR_COUNT 3
static const s16 bossAnchorX[BOSS_ANCHOR_COUNT] = {
    PLAY_AREA_X_MIN + 8,
    (PLAY_AREA_X_MIN + PLAY_AREA_X_MAX) / 2 - BOSS_BODY_W / 2,
    PLAY_AREA_X_MAX - BOSS_BODY_W - 8,
};

#define BOSS_MOVE_SPEED_PX      2   // fixed speed -- duration derives from distance (see beginMove())
#define BOSS_MOVE_MIN_FRAMES    20
#define BOSS_ATTACK_PHASE_FRAMES 240 // ~4s per attack pattern before repositioning

// Hidden 5-minute limit -- if the fight isn't resolved by then, the boss
// dives off screen and disappears without awarding score (see
// enemies_forceDiveAllOut()'s no-reward wave timeout for the analogous
// regular-wave case).
#define BOSS_TIME_LIMIT_FRAMES (300 * 60)

typedef enum
{
    BOSS_ATTACK_SPREAD,
    BOSS_ATTACK_BURST,
    BOSS_ATTACK_RADIAL,
} BossAttackKind;
#define BOSS_ATTACK_KIND_COUNT 3

#define SPREAD_BULLET_COUNT   5
#define SPREAD_FIRE_INTERVAL  60
#define SPREAD_SPEED          FIX16(1.6)

#define BURST_SHOT_COUNT      4
#define BURST_SHOT_INTERVAL   8
#define BURST_COOLDOWN        90
#define BURST_SPEED           FIX16(1.8)

#define RADIAL_FIRE_INTERVAL  100 // bossRadialVx/Vy (boss_patterns_generated.h) are already scaled to speed

#define HOMING_FIRE_INTERVAL  240 // one homing bullet roughly every 4s, regardless of attack pattern
#define HOMING_INITIAL_SPEED  FIX16(1.3) // must match bullet.c's HOMING_SPEED
#define HOMING_MAX_ALIVE      3   // never more than this many at once (see bullet_countActiveHoming())
#define HOMING_RETRY_FRAMES   30  // how soon to check again if the cap was hit

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

// -- tile loading: boss's own VRAM tiles, based right after bullet.c's
// (TILE_USER_INDEX+228: +1 player bullet, +1 enemy bullet, +8 for the
// homing bullet's 2-frame x 4-tile-per-frame sheet -- see bullet.c's
// BULLET_TILE_BASE) -- so this starts at +238 or later. (A previous +232
// here overlapped the homing bullet's own tiles -- 2 tiles, not 8, were
// budgeted for its sheet -- corrupting its pixel data once the boss's
// tiles loaded on top of it.)
#define BOSS_TILE_BASE (TILE_USER_INDEX + 238)

static u16 bodyTLTile, bodyBLTile; // frame 0/1 of the single spr_boss_body sheet
static u16 weakSpotNormalTile, weakSpotFlashTile, weakSpotDestroyedTile;

// Pause after the killing blow before the encounter actually ends (see
// triggerDeath()) -- lets the death scream/explosions play out and gives
// the player a breather instead of formation.c cutting straight to the
// next wave the instant the last weak spot dies.
#define BOSS_DEATH_DELAY_FRAMES (90) // 1.5s at 60fps
static bool dying;
static u16 deathDelayTimer;

static bool bossActive;
static fix16 bossX, bossY; // anchor: body's top-left corner
static fix16 bossVX, bossVY;
static u16 moveTimer, moveDuration;
static s16 moveTargetX, moveTargetY;
static bool exitingAfterMove; // TRUE once the hidden timer has expired -- next arrival ends the fight, no score
static u8 anchorIndex;

static BossPhase phase;
static u16 phaseTimer; // frames remaining in the current ATTACKING phase
static BossAttackKind currentAttack;
static u16 lifeTimer;

static Sprite *bodyTL, *bodyTR, *bodyBL, *bodyBR;
static BossWeakSpot weakSpots[2];

// Per-attack-kind firing state.
static u16 spreadFireTimer;
static u8 burstShotsLeft;
static u16 burstTimer;
static u16 radialFireTimer;
static u16 homingFireTimer;

void boss_init(void)
{
    bossActive = FALSE;

    u16 totalTiles;
    u16 base = BOSS_TILE_BASE;

    u16 **idx = SPR_loadAllFrames(&spr_boss_body, base, &totalTiles);
    bodyTLTile = idx[0][0];
    bodyBLTile = idx[1][0];
    MEM_free(idx);
    base += totalTiles;

    idx = SPR_loadAllFrames(&spr_boss_weakspot, base, &totalTiles);
    weakSpotNormalTile = idx[0][0];
    weakSpotFlashTile = idx[1][0];
    weakSpotDestroyedTile = idx[2][0];
    MEM_free(idx);

    // Sprite handles intentionally left NULL here (not created yet) -- see
    // boss_begin()/boss_end(), which create/release them once per encounter
    // rather than keeping them forever, to reclaim the enemy/turret pools'
    // slots instead of growing the game's total ever-allocated sprite count
    // (see enemies_releaseIdleSprites()'s comment).
    bodyTL = bodyTR = bodyBL = bodyBR = NULL;
    weakSpots[0].sprite = NULL;
    weakSpots[1].sprite = NULL;
}

// One-shot glide setup: computes vx/vy once (a single division, not
// per-tick -- same convention as every other entrance/dive in this
// codebase) from the current position to (targetX,targetY) at a fixed
// speed, so travel time scales with distance instead of being a fixed
// duration that would look too fast/slow depending on how far it's going.
static void beginMove(s16 targetX, s16 targetY)
{
    s16 fromX = F16_toInt(bossX);
    s16 fromY = F16_toInt(bossY);
    s16 dx = targetX - fromX;
    s16 dy = targetY - fromY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    s16 dist = (dx > dy) ? dx : dy;

    u16 duration = (u16) (dist / BOSS_MOVE_SPEED_PX);
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
    phaseTimer = BOSS_ATTACK_PHASE_FRAMES;
    spreadFireTimer = SPREAD_FIRE_INTERVAL / 2; // stagger the first shot in a bit, not instantly on arrival
    burstShotsLeft = 0;
    burstTimer = BURST_COOLDOWN;
    radialFireTimer = RADIAL_FIRE_INTERVAL;
}

void boss_begin(void)
{
    bossActive = TRUE;

    // Reclaim hardware sprite slots from the two pools guaranteed idle
    // during a boss fight (see their own comments for why this is safe).
    enemies_releaseIdleSprites();
    turrets_releaseIdleSprites();

    lifeTimer = BOSS_TIME_LIMIT_FRAMES;
    exitingAfterMove = FALSE;
    dying = FALSE;
    anchorIndex = 0;
    currentAttack = BOSS_ATTACK_SPREAD;
    homingFireTimer = HOMING_FIRE_INTERVAL;

    s16 startX = bossAnchorX[0];
    s16 startY = -BOSS_BODY_H - 10;
    bossX = FIX16(startX);
    bossY = FIX16(startY);

    for (u16 i = 0; i < 2; i++)
    {
        weakSpots[i].hp = WEAKSPOT_HP;
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

    if (bodyTL == NULL) bodyTL = SPR_addSpriteEx(&spr_boss_body, tlx, tly, tlAttr, 0);
    else { SPR_setVRAMTileIndex(bodyTL, bodyTLTile); SPR_setVisibility(bodyTL, VISIBLE); SPR_setPosition(bodyTL, tlx, tly); }

    if (bodyTR == NULL) bodyTR = SPR_addSpriteEx(&spr_boss_body, trx, try_, trAttr, 0);
    else { SPR_setVRAMTileIndex(bodyTR, bodyTLTile); SPR_setVisibility(bodyTR, VISIBLE); SPR_setPosition(bodyTR, trx, try_); }
    SPR_setHFlip(bodyTR, TRUE);

    if (bodyBL == NULL) bodyBL = SPR_addSpriteEx(&spr_boss_body, blx, bly, blAttr, 0);
    else { SPR_setVRAMTileIndex(bodyBL, bodyBLTile); SPR_setVisibility(bodyBL, VISIBLE); SPR_setPosition(bodyBL, blx, bly); }

    if (bodyBR == NULL) bodyBR = SPR_addSpriteEx(&spr_boss_body, brx, bry, brAttr, 0);
    else { SPR_setVRAMTileIndex(bodyBR, bodyBLTile); SPR_setVisibility(bodyBR, VISIBLE); SPR_setPosition(bodyBR, brx, bry); }
    SPR_setHFlip(bodyBR, TRUE);

    for (u16 i = 0; i < 2; i++)
    {
        s16 wx = startX + weakSpotOffsetX[i];
        s16 wy = startY + weakSpotOffsetY[i];
        u16 attr = TILE_ATTR_FULL(PAL_BOSS, FALSE, FALSE, FALSE, weakSpotNormalTile);
        if (weakSpots[i].sprite == NULL)
            // SPR_FLAG_INSERT_HEAD -- weak spots must draw in front of the
            // body (they're created after it, and sprites later in the
            // list render underneath earlier ones), not get hidden behind it.
            weakSpots[i].sprite = SPR_addSpriteEx(&spr_boss_weakspot, wx, wy, attr, SPR_FLAG_INSERT_HEAD);
        else
        {
            SPR_setVRAMTileIndex(weakSpots[i].sprite, weakSpotNormalTile);
            SPR_setHFlip(weakSpots[i].sprite, FALSE);
            SPR_setVisibility(weakSpots[i].sprite, VISIBLE);
            SPR_setPosition(weakSpots[i].sprite, wx, wy);
        }
    }

    beginMove(bossAnchorX[0], BOSS_ANCHOR_Y);
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
    if (weakSpots[0].sprite != NULL) { SPR_releaseSprite(weakSpots[0].sprite); weakSpots[0].sprite = NULL; }
    if (weakSpots[1].sprite != NULL) { SPR_releaseSprite(weakSpots[1].sprite); weakSpots[1].sprite = NULL; }
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
    if (weakSpots[0].sprite != NULL) SPR_setVisibility(weakSpots[0].sprite, HIDDEN);
    if (weakSpots[1].sprite != NULL) SPR_setVisibility(weakSpots[1].sprite, HIDDEN);
}

AABB boss_getBounds(void)
{
    // Broad box covering the body plus both flanking pods (see
    // weakSpotOffsetX's -8/+8 spill past the body's own edges) -- used only
    // for the player-ram-death check, not bullet-vs-weak-spot precision.
    AABB box = {F16_toInt(bossX) - 8, F16_toInt(bossY), BOSS_BODY_W + 16, BOSS_BODY_H};
    return box;
}

AABB boss_weakSpotBounds(u16 index)
{
    if (weakSpots[index].destroyed)
    {
        AABB empty = {0, 0, 0, 0};
        return empty;
    }

    AABB box = {
        F16_toInt(bossX) + weakSpotOffsetX[index],
        F16_toInt(bossY) + weakSpotOffsetY[index],
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

        if (weakSpots[0].destroyed && weakSpots[1].destroyed)
            triggerDeath();
        return;
    }

    ws->hitFlashTimer = WEAKSPOT_HIT_FLASH_FRAMES;
    SPR_setVRAMTileIndex(ws->sprite, weakSpotFlashTile);
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
                for (u16 i = 0; i < BOSS_RADIAL_DIRECTION_COUNT; i++)
                    bullet_spawn_enemy(originX, originY, bossRadialVx[i], bossRadialVy[i]);
                radialFireTimer = RADIAL_FIRE_INTERVAL;
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
        // Frozen in place (both weak spots already show their destroyed
        // husk) while the death scream/staggered explosions play out --
        // see triggerDeath(). boss_isActive() stays TRUE for this whole
        // delay, so formation.c doesn't cut to the next wave early.
        if (deathDelayTimer > 0)
        {
            deathDelayTimer--;
            return;
        }
        endFight();
        return;
    }

    if (!exitingAfterMove)
    {
        if (lifeTimer > 0)
            lifeTimer--;

        if (lifeTimer == 0)
        {
            exitingAfterMove = TRUE;
            beginMove(F16_toInt(bossX), SCREEN_H + BOSS_BODY_H);
        }
    }

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
            currentAttack = (BossAttackKind) ((currentAttack + 1) % BOSS_ATTACK_KIND_COUNT);
            beginMove(bossAnchorX[anchorIndex], BOSS_ANCHOR_Y);
        }
    }

    s16 x = F16_toInt(bossX);
    s16 y = F16_toInt(bossY);
    SPR_setPosition(bodyTL, x, y);
    SPR_setPosition(bodyTR, x + BOSS_QUAD_W, y);
    SPR_setPosition(bodyBL, x, y + BOSS_QUAD_H);
    SPR_setPosition(bodyBR, x + BOSS_QUAD_W, y + BOSS_QUAD_H);

    for (u16 i = 0; i < 2; i++)
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
        SPR_setPosition(ws->sprite, x + weakSpotOffsetX[i], y + weakSpotOffsetY[i]);
    }
}
