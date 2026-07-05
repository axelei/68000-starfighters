#include "collision.h"
#include "player.h"
#include "bullet.h"
#include "enemy.h"
#include "powerup.h"

#define BULLET_DAMAGE 1

bool aabb_overlaps(AABB a, AABB b)
{
    if (a.x + a.w <= b.x) return FALSE;
    if (b.x + b.w <= a.x) return FALSE;
    if (a.y + a.h <= b.y) return FALSE;
    if (b.y + b.h <= a.y) return FALSE;
    return TRUE;
}

static void resolvePlayerBulletsVsEnemies(void)
{
    for (u16 bi = 0; bi < MAX_PLAYER_BULLETS; bi++)
    {
        Bullet *b = &playerBullets[bi];
        if (!b->active)
            continue;

        AABB bbox = bullet_getBounds(b);

        for (u16 ei = 0; ei < MAX_ENEMIES; ei++)
        {
            Enemy *e = &enemies[ei];
            if (!e->active || e->startDelay > 0)
                continue;

            if (aabb_overlaps(bbox, enemy_getBounds(e)))
            {
                bullet_deactivate(b);
                enemy_hit(e, BULLET_DAMAGE);
                break;
            }
        }
    }
}

static void resolveEnemyThreatsVsPlayer(void)
{
    if (!player.alive)
        return;

    AABB pbox = player_getBounds();

    for (u16 bi = 0; bi < MAX_ENEMY_BULLETS; bi++)
    {
        Bullet *b = &enemyBullets[bi];
        if (!b->active)
            continue;

        if (aabb_overlaps(pbox, bullet_getBounds(b)))
        {
            bullet_deactivate(b);
            player_kill();
            return;
        }
    }

    for (u16 ei = 0; ei < MAX_ENEMIES; ei++)
    {
        Enemy *e = &enemies[ei];
        if (!e->active || e->startDelay > 0)
            continue;

        if (aabb_overlaps(pbox, enemy_getBounds(e)))
        {
            player_kill();
            return;
        }
    }
}

static void resolvePlayerVsPowerups(void)
{
    if (!player.alive)
        return;

    AABB pbox = player_getBounds();

    for (u16 i = 0; i < MAX_POWERUPS; i++)
    {
        Powerup *p = &powerups[i];
        if (!p->active)
            continue;

        if (aabb_overlaps(pbox, powerup_getBounds(p)))
            powerup_collect(p);
    }
}

void collisions_resolve(void)
{
    resolvePlayerBulletsVsEnemies();
    resolveEnemyThreatsVsPlayer();
    resolvePlayerVsPowerups();
}
