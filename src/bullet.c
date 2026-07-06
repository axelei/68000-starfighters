#include "bullet.h"
#include "resources.h"

Bullet playerBullets[MAX_PLAYER_BULLETS];
Bullet enemyBullets[MAX_ENEMY_BULLETS];

#define BULLET_SPR_W 8
#define BULLET_SPR_H 8

// Every player bullet looks identical (same for every enemy bullet), so
// their tile graphics are uploaded to VRAM exactly once here rather than
// once per active bullet -- SPR_addSprite's default behaviour would
// otherwise give each of the up to MAX_PLAYER_BULLETS/MAX_ENEMY_BULLETS
// sprites its own private VRAM copy of the same single tile.
#define BULLET_TILE_BASE (TILE_USER_INDEX + 200)

static u16 playerBulletTile;
static u16 enemyBulletTile;
static bool tilesLoaded = FALSE;

static void loadSharedTiles(void)
{
    if (tilesLoaded)
        return;

    u16 totalTiles;
    u16 base = BULLET_TILE_BASE;

    u16 **idx = SPR_loadAllFrames(&spr_bullet_player, base, &totalTiles);
    playerBulletTile = idx[0][0];
    MEM_free(idx);
    base += totalTiles;

    idx = SPR_loadAllFrames(&spr_bullet_enemy, base, &totalTiles);
    enemyBulletTile = idx[0][0];
    MEM_free(idx);

    tilesLoaded = TRUE;
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

static bool spawn(Bullet *pool, u16 count, const SpriteDefinition *def, u16 pal, u16 vramTile,
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
        if (b->sprite == NULL)
            b->sprite = SPR_addSpriteEx(def, F16_toInt(x), F16_toInt(y),
                                         TILE_ATTR_FULL(pal, FALSE, FALSE, FALSE, vramTile), 0);
        else
        {
            SPR_setVisibility(b->sprite, VISIBLE);
            SPR_setPosition(b->sprite, F16_toInt(x), F16_toInt(y));
        }
        return TRUE;
    }
    return FALSE;
}

bool bullet_spawn_player(fix16 x, fix16 y, fix16 vx, fix16 vy)
{
    return spawn(playerBullets, MAX_PLAYER_BULLETS, &spr_bullet_player, PAL_SHIP, playerBulletTile, x, y, vx, vy);
}

bool bullet_spawn_enemy(fix16 x, fix16 y, fix16 vx, fix16 vy)
{
    return spawn(enemyBullets, MAX_ENEMY_BULLETS, &spr_bullet_enemy, PAL_ENEMY, enemyBulletTile, x, y, vx, vy);
}

void bullet_deactivate(Bullet *b)
{
    b->active = FALSE;
    if (b->sprite != NULL)
        SPR_setVisibility(b->sprite, HIDDEN);
}

static void update_pool(Bullet *pool, u16 count)
{
    for (u16 i = 0; i < count; i++)
    {
        Bullet *b = &pool[i];
        if (!b->active)
            continue;

        b->x += b->vx;
        b->y += b->vy;

        s16 px = F16_toInt(b->x);
        s16 py = F16_toInt(b->y);

        if (py < -BULLET_SPR_H || py > SCREEN_H || px < -BULLET_SPR_W || px > SCREEN_W)
        {
            bullet_deactivate(b);
            continue;
        }

        SPR_setPosition(b->sprite, px, py);
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
