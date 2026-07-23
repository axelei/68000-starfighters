#include "collision.h"
#include "player.h"
#include "bullet.h"
#include "enemy.h"
#include "turret.h"
#include "powerup.h"
#include "boss.h"

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
    // Bounds cached once per frame rather than recomputed inside the bullet
    // loop below: with up to MAX_PLAYER_BULLETS active bullets each
    // scanning all MAX_ENEMIES slots, enemy_getBounds() (and the
    // enemy_widthForKind()/heightForKind() switches it calls) was being
    // re-run many times a frame for the same handful of distinct boxes --
    // the slowdown reported with many player bullets on screen at once.
    AABB enemyBoxes[MAX_ENEMIES];
    bool enemyLive[MAX_ENEMIES];
    for (u16 ei = 0; ei < MAX_ENEMIES; ei++)
    {
        Enemy *e = &enemies[ei];
        enemyLive[ei] = e->active && e->startDelay == 0;
        if (enemyLive[ei])
            enemyBoxes[ei] = enemy_getBounds(e);
    }

    for (u16 bi = 0; bi < MAX_PLAYER_BULLETS; bi++)
    {
        Bullet *b = &playerBullets[bi];
        if (!b->active)
            continue;

        AABB bbox = bullet_getBounds(b);
        bool hit = FALSE;

        for (u16 ei = 0; ei < MAX_ENEMIES; ei++)
        {
            if (!enemyLive[ei])
                continue;

            if (aabb_overlaps(bbox, enemyBoxes[ei]))
            {
                // Sample the bullet's center pixel against the enemy's mask
                // (see enemy_hitTestPixel()) rather than every pixel the
                // bullet's box covers -- BULLET_SPR_W/H is only 8px, so a
                // single sample is close enough and keeps this a single
                // shift+mask on top of the AABB check.
                s16 sampleX = bbox.x + bbox.w / 2;
                s16 sampleY = bbox.y + bbox.h / 2;
                if (!enemy_hitTestPixel(&enemies[ei], sampleX, sampleY))
                    continue;

                enemy_hit(&enemies[ei], BULLET_DAMAGE);
                hit = TRUE;
                break;
            }
        }

        if (!hit)
        {
            for (u16 ti = 0; ti < MAX_TURRETS; ti++)
            {
                Turret *t = &turrets[ti];
                if (!t->active)
                    continue;

                if (aabb_overlaps(bbox, turret_getBounds(t)))
                {
                    turret_hit(t, BULLET_DAMAGE);
                    hit = TRUE;
                    break;
                }
            }
        }

        if (hit)
            bullet_deactivate(b);
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

        if (aabb_overlaps(pbox, enemy_getBounds(e)) &&
            enemy_hitTestPixel(e, pbox.x + pbox.w / 2, pbox.y + pbox.h / 2))
        {
            // Small/fragile enemies are destroyed by the collision too, not
            // just the player -- a ramming BEE/SPECIAL/waver doesn't survive
            // the hit. BIG enemies are too tough to be taken out this way.
            if (e->kind == ENEMY_KIND_BEE || e->kind == ENEMY_KIND_SPECIAL || enemy_isWaverKind(e->kind))
                enemy_kill(e);
            player_kill();
            return;
        }
    }

    // Turrets are terrain-attached scenery, not a ramming threat -- only
    // their bullets can kill the player, not touching one directly.

    // Boss body/weak-spot pods: same ramming rule as BIG -- touching it
    // kills the player, but it never dies from the contact itself (only
    // from its weak spots being shot down, see boss_hitWeakSpot()).
    if (boss_isActive() && aabb_overlaps(pbox, boss_getBounds()) &&
        boss_hitTestPixel(pbox.x + pbox.w / 2, pbox.y + pbox.h / 2))
    {
        player_kill();
        return;
    }
}

// Player bullets vs the boss's homing bullets -- the only "bullet vs
// bullet" collision path in the game, since the homing bullet is the only
// destructible one (see bullet_hitHoming()). Gated on boss_isActive() at
// the call site since enemyBullets only ever holds a homing entry while a
// boss fight is running.
static void resolvePlayerBulletsVsHomingBullets(void)
{
    for (u16 bi = 0; bi < MAX_PLAYER_BULLETS; bi++)
    {
        Bullet *pb = &playerBullets[bi];
        if (!pb->active)
            continue;

        AABB pboxBullet = bullet_getBounds(pb);

        for (u16 ei = 0; ei < MAX_ENEMY_BULLETS; ei++)
        {
            Bullet *eb = &enemyBullets[ei];
            if (!eb->active || !eb->isHoming)
                continue;

            if (aabb_overlaps(pboxBullet, bullet_getBounds(eb)))
            {
                bullet_hitHoming(eb, BULLET_DAMAGE);
                bullet_deactivate(pb);
                break;
            }
        }
    }
}

// Player bullets vs the boss's weak spots -- the only damageable part (see
// boss.h's design comment). A bullet that misses them passes straight
// through the body untouched: the pods are embedded within the body's own
// bounding box (see boss.c's weakSpotOffsetY, well inside its 64px height),
// so a solid absorbing body hitbox was intercepting bullets approaching
// from below well before they ever reached an embedded pod -- effectively
// blocking almost every shot at them.
static void resolvePlayerBulletsVsBoss(void)
{
    for (u16 bi = 0; bi < MAX_PLAYER_BULLETS; bi++)
    {
        Bullet *b = &playerBullets[bi];
        if (!b->active)
            continue;

        AABB bbox = bullet_getBounds(b);

        for (u16 wi = 0; wi < boss_weakSpotCount(); wi++)
        {
            if (aabb_overlaps(bbox, boss_weakSpotBounds(wi)))
            {
                boss_hitWeakSpot(wi, BULLET_DAMAGE);
                bullet_deactivate(b);
                break;
            }
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

    if (boss_isActive())
    {
        resolvePlayerBulletsVsHomingBullets();
        resolvePlayerBulletsVsBoss();
    }
}
