#include "bullet.h"
#include "resources.h"
#include "player.h"
#include "explosion.h"

Bullet playerBullets[MAX_PLAYER_BULLETS];
Bullet enemyBullets[MAX_ENEMY_BULLETS];

#define BULLET_SPR_W 8
#define BULLET_SPR_H 8
#define HOMING_SPR_W 16
#define HOMING_SPR_H 16

// Boss's homing bullet only (see bullet_spawn_enemy_homing()). PAL's
// HOMING_RETARGET_FRAMES/HOMING_SPEED are NTSC * 50/60 / * 1.2 -- see
// game.h's REGION_PICK -- so real-world turn cadence and speed stay the
// same. HOMING_TURN_STEP deliberately does NOT scale: it's a max delta
// applied once per retarget (not per-tick), so the real-world turn rate
// already comes out consistent from the retarget interval alone (20
// frames @ 60fps and 17 frames @ 50fps are both ~0.33s). Very short
// (<=4 frame) flash/blink timers (HOMING_HIT_FLASH_FRAMES,
// HOMING_BLINK_OFF_FRAMES) also stay unscaled, same exception as
// enemy.c's HIT_FLASH_FRAMES.
#define HOMING_BULLET_HP           5
#define HOMING_RETARGET_FRAMES     REGION_PICK(20, 17) // one-shot re-aim interval -- not every tick, see the update below
#define HOMING_TURN_STEP           FIX16(0.15) // max vx/vy nudge per retarget, towards the aimed direction
#define HOMING_SPEED               REGION_PICK(FIX16(1.3), FIX16(1.56))
#define HOMING_HIT_FLASH_FRAMES    4
#define HOMING_BLINK_OFF_FRAMES    3  // brief hidden pulse once per retarget cycle -- see the update below

// Every player bullet looks identical (same for every enemy bullet), so
// their tile graphics are uploaded to VRAM exactly once here rather than
// once per active bullet -- SPR_addSprite's default behaviour would
// otherwise give each of the up to MAX_PLAYER_BULLETS/MAX_ENEMY_BULLETS
// sprites its own private VRAM copy of the same single tile.
// Turret now needs 12 tiles (3 anims x 4 tiles, see turret.c) starting at
// +216, so this must start at +228 or later to avoid overlapping it.
#define BULLET_TILE_BASE (TILE_USER_INDEX + 228)

static u16 playerBulletTile;
static u16 enemyBulletTile;
static u16 homingBulletTile;
static u16 homingBulletFlashTile;

// Reloaded every time (no "already loaded" guard) -- see enemy.c's
// loadSharedTiles for why: a soft reset clears VRAM but not this static
// state, so a one-time guard would skip the re-upload after a reset.
static void loadSharedTiles(void)
{
    u16 totalTiles;
    u16 base = BULLET_TILE_BASE;

    u16 **idx = SPR_loadAllFrames(&spr_bullet_player, base, &totalTiles);
    playerBulletTile = idx[0][0];
    MEM_free(idx);
    base += totalTiles;

    idx = SPR_loadAllFrames(&spr_bullet_enemy, base, &totalTiles);
    enemyBulletTile = idx[0][0];
    MEM_free(idx);
    base += totalTiles;

    // 2-row sheet (normal / hit-flash, see bullet_homing() in
    // generate_placeholders.py) -- both frames are loaded up front and
    // selected via SPR_setVRAMTileIndex, same convention as enemy.c's
    // normalTile[]/flashTile[].
    idx = SPR_loadAllFrames(&spr_bullet_homing, base, &totalTiles);
    homingBulletTile = idx[0][0];
    homingBulletFlashTile = idx[1][0];
    MEM_free(idx);
}

static void pool_init(Bullet *pool, u16 count)
{
    for (u16 i = 0; i < count; i++)
    {
        pool[i].active = FALSE;
        // Sprite handles are intentionally left alone (not nulled) so a
        // re-init (title -> game restart) reuses each slot's existing
        // SPR_addSprite handle instead of leaking a new one every game.
    }
}

void bullets_init(void)
{
    loadSharedTiles();
    pool_init(playerBullets, MAX_PLAYER_BULLETS);
    pool_init(enemyBullets, MAX_ENEMY_BULLETS);
}

// See bullet.h -- boot-time only, not part of the restart-safe bullets_init()
// above.
void bullets_resetHandles(void)
{
    for (u16 i = 0; i < MAX_PLAYER_BULLETS; i++)
        playerBullets[i].sprite = NULL;
    for (u16 i = 0; i < MAX_ENEMY_BULLETS; i++)
        enemyBullets[i].sprite = NULL;
}

static Bullet *spawn(Bullet *pool, u16 count, const SpriteDefinition *def, u16 pal, u16 vramTile,
                      fix16 x, fix16 y, fix16 vx, fix16 vy)
{
    for (u16 i = 0; i < count; i++)
    {
        Bullet *b = &pool[i];
        if (b->active)
            continue;

        b->active = TRUE;
        b->x = x;
        b->y = y;
        b->vx = vx;
        b->vy = vy;
        // Always reset -- a slot previously used by a homing bullet (see
        // bullet_spawn_enemy_homing()) must not leak isHoming/hp into a
        // later ordinary enemy bullet reusing the same enemyBullets slot.
        b->isHoming = FALSE;
        b->hp = 0;
        b->retargetTimer = 0;
        b->flashTimer = 0;
        if (b->sprite == NULL)
            b->sprite = SPR_addSpriteEx(def, F16_toInt(x), F16_toInt(y),
                                         TILE_ATTR_FULL(pal, FALSE, FALSE, FALSE, vramTile), 0);
        else
        {
            // This enemyBullets slot may have last held a different-sized
            // bullet (an 8x8 ordinary shot vs. the 16x16 homing one --
            // see bullet_spawn_enemy_homing()) -- SPR_setDefinition() fixes
            // up the sprite's shape, same as enemy.c's enemy_spawn()/
            // powerup.c's reuse path. Without this, a reused slot keeps
            // its old size and only shows a fraction of the new tile data.
            SPR_setDefinition(b->sprite, def);
            SPR_setVRAMTileIndex(b->sprite, vramTile);
            SPR_setVisibility(b->sprite, VISIBLE);
            SPR_setPosition(b->sprite, F16_toInt(x), F16_toInt(y));
        }
        return b;
    }
    return NULL;
}

bool bullet_spawn_player(fix16 x, fix16 y, fix16 vx, fix16 vy)
{
    return spawn(playerBullets, MAX_PLAYER_BULLETS, &spr_bullet_player, PAL_PLAYER, playerBulletTile, x, y, vx, vy) != NULL;
}

bool bullet_spawn_enemy(fix16 x, fix16 y, fix16 vx, fix16 vy)
{
    return spawn(enemyBullets, MAX_ENEMY_BULLETS, &spr_bullet_enemy, PAL_ENEMY, enemyBulletTile, x, y, vx, vy) != NULL;
}

bool bullet_spawn_enemy_homing(fix16 x, fix16 y, fix16 vx, fix16 vy)
{
    Bullet *b = spawn(enemyBullets, MAX_ENEMY_BULLETS, &spr_bullet_homing, PAL_ENEMY, homingBulletTile, x, y, vx, vy);
    if (b == NULL)
        return FALSE;

    b->isHoming = TRUE;
    b->hp = HOMING_BULLET_HP;
    b->retargetTimer = HOMING_RETARGET_FRAMES;
    return TRUE;
}

u16 bullet_countActiveHoming(void)
{
    u16 count = 0;
    for (u16 i = 0; i < MAX_ENEMY_BULLETS; i++)
        if (enemyBullets[i].active && enemyBullets[i].isHoming)
            count++;
    return count;
}

void bullet_clearHoming(void)
{
    for (u16 i = 0; i < MAX_ENEMY_BULLETS; i++)
        if (enemyBullets[i].active && enemyBullets[i].isHoming)
            bullet_deactivate(&enemyBullets[i]);
}

void bullet_hitHoming(Bullet *b, s16 damage)
{
    if (!b->isHoming)
        return;

    b->hp -= (s8) damage;
    b->flashTimer = HOMING_HIT_FLASH_FRAMES;
    SPR_setVRAMTileIndex(b->sprite, homingBulletFlashTile);

    if (b->hp <= 0)
    {
        explosion_spawnAt(F16_toInt(b->x), F16_toInt(b->y));
        bullet_deactivate(b);
    }
}

void bullet_deactivate(Bullet *b)
{
    b->active = FALSE;
    if (b->sprite != NULL)
        SPR_setVisibility(b->sprite, HIDDEN);
}

// One-shot re-aim towards the player, computed only once every
// HOMING_RETARGET_FRAMES (not every tick -- see the standing "no DIV/MUL
// every tick" rule): normalizes the direction to the player exactly once,
// then nudges vx/vy a fixed step towards it. The actual per-frame motion
// (b->x/y += vx/vy) is a plain add, same as every other bullet.
static void retarget(Bullet *b)
{
    fix16 dx = player.x - b->x;
    fix16 dy = player.y - b->y;
    if (dx > -FIX16(4) && dx < FIX16(4) && dy > -FIX16(4) && dy < FIX16(4))
        return; // avoid a divide-by-near-zero if it's already right on the player

    fix16 dist = (fix16) getApproximatedDistance(dx, dy);
    fix16 aimVx = F16_mul(F16_div(dx, dist), HOMING_SPEED);
    fix16 aimVy = F16_mul(F16_div(dy, dist), HOMING_SPEED);

    fix16 dvx = aimVx - b->vx;
    if (dvx > HOMING_TURN_STEP) dvx = HOMING_TURN_STEP;
    else if (dvx < -HOMING_TURN_STEP) dvx = -HOMING_TURN_STEP;
    b->vx += dvx;

    fix16 dvy = aimVy - b->vy;
    if (dvy > HOMING_TURN_STEP) dvy = HOMING_TURN_STEP;
    else if (dvy < -HOMING_TURN_STEP) dvy = -HOMING_TURN_STEP;
    b->vy += dvy;
}

static void update_pool(Bullet *pool, u16 count)
{
    for (u16 i = 0; i < count; i++)
    {
        Bullet *b = &pool[i];
        if (!b->active)
            continue;

        if (b->isHoming)
        {
            if (b->retargetTimer > 0)
            {
                b->retargetTimer--;
            }
            else
            {
                retarget(b);
                b->retargetTimer = HOMING_RETARGET_FRAMES;
            }

            if (b->flashTimer > 0)
            {
                b->flashTimer--;
                if (b->flashTimer == 0)
                    SPR_setVRAMTileIndex(b->sprite, homingBulletTile);
            }
        }

        b->x += b->vx;
        b->y += b->vy;

        s16 px = F16_toInt(b->x);
        s16 py = F16_toInt(b->y);

        u16 w = b->isHoming ? HOMING_SPR_W : BULLET_SPR_W;
        u16 h = b->isHoming ? HOMING_SPR_H : BULLET_SPR_H;
        if (py < -(s16) h || py > SCREEN_H || px < -(s16) w || px > SCREEN_W)
        {
            bullet_deactivate(b);
            continue;
        }

        SPR_setPosition(b->sprite, px, py);

        // Blinks continuously (not just on hit) so the player can pick it
        // out from ordinary enemy bullets as a shootable threat -- a short
        // hidden pulse (HOMING_BLINK_OFF_FRAMES) near the end of each
        // retarget cycle, not a 50/50 toggle, so it reads as mostly-visible
        // with a brief blink rather than half-invisible the whole time.
        // Reuses retargetTimer's countdown for the phase rather than a
        // dedicated counter (it always counts down from a fixed value, so
        // the blink lands at the same point in every cycle); suppressed
        // while actually flashing from a hit so that reads clearly instead
        // of blinking mid-flash.
        if (b->isHoming)
        {
            bool blinkOff = b->retargetTimer < HOMING_BLINK_OFF_FRAMES;
            SPR_setVisibility(b->sprite, (b->flashTimer > 0 || !blinkOff) ? VISIBLE : HIDDEN);
        }
    }
}

void bullets_update(void)
{
    update_pool(playerBullets, MAX_PLAYER_BULLETS);
    update_pool(enemyBullets, MAX_ENEMY_BULLETS);
}

static void hideAll_pool(Bullet *pool, u16 count)
{
    for (u16 i = 0; i < count; i++)
    {
        if (pool[i].active)
            bullet_deactivate(&pool[i]);
    }
}

void bullets_hideAll(void)
{
    hideAll_pool(playerBullets, MAX_PLAYER_BULLETS);
    hideAll_pool(enemyBullets, MAX_ENEMY_BULLETS);
}

AABB bullet_getBounds(const Bullet *b)
{
    AABB box = {F16_toInt(b->x), F16_toInt(b->y), BULLET_SPR_W, BULLET_SPR_H};
    return box;
}
