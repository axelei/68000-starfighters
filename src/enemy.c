#include "enemy.h"
#include "resources.h"
#include "score.h"
#include "powerup.h"
#include "sfx.h"

Enemy enemies[MAX_ENEMIES];

#define ENEMY_SPR_W 16
#define ENEMY_SPR_H 16
#define ENTER_DURATION 90 // frames (~1.5s at 60fps)

void enemies_init(void)
{
    for (u16 i = 0; i < MAX_ENEMIES; i++)
    {
        enemies[i].active = FALSE;
        enemies[i].sprite = NULL;
    }
}

static const SpriteDefinition *spriteDefForKind(EnemyKind kind)
{
    return (kind == ENEMY_KIND_SPECIAL) ? &spr_enemy_special : &spr_enemy_bee;
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

        if (e->sprite == NULL)
            e->sprite = SPR_addSprite(spriteDefForKind(kind), startX, startY,
                                       TILE_ATTR(PAL_ENEMY, FALSE, FALSE, FALSE));
        SPR_setVisibility(e->sprite, delay > 0 ? HIDDEN : VISIBLE);
        SPR_setPosition(e->sprite, startX, startY);

        return e;
    }
    return NULL;
}

void enemy_kill(Enemy *e)
{
    s16 x = F16_toInt(e->x);
    s16 y = F16_toInt(e->y);

    e->active = FALSE;
    SPR_setVisibility(e->sprite, HIDDEN);

    score_addKill(e->kind);
    sfx_play_explosion();

    if (e->kind == ENEMY_KIND_SPECIAL)
        powerup_spawnAt(x, y);
}

AABB enemy_getBounds(const Enemy *e)
{
    AABB box = {F16_toInt(e->x), F16_toInt(e->y), ENEMY_SPR_W, ENEMY_SPR_H};
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
            }
        }

        SPR_setPosition(e->sprite, F16_toInt(e->x), F16_toInt(e->y));
    }
}
