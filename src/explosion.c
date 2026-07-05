#include "explosion.h"
#include "resources.h"

#define EXPLOSION_FRAME_COUNT 4
#define EXPLOSION_FRAME_HOLD  6 // frames each animation frame is held (~0.1s)
#define EXPLOSION_SPR_W 16
#define EXPLOSION_SPR_H 16

typedef struct
{
    bool active;
    Sprite *sprite;
    u16 frameIndex;
    u16 frameTimer;
    u16 startDelay;
} Explosion;

static Explosion explosions[MAX_EXPLOSIONS];

void explosions_init(void)
{
    for (u16 i = 0; i < MAX_EXPLOSIONS; i++)
    {
        explosions[i].active = FALSE;
        explosions[i].sprite = NULL;
    }
}

void explosion_spawnAtDelayed(s16 centerX, s16 centerY, u16 delay)
{
    for (u16 i = 0; i < MAX_EXPLOSIONS; i++)
    {
        Explosion *e = &explosions[i];
        if (e->active)
            continue;

        e->active = TRUE;
        e->frameIndex = 0;
        e->frameTimer = EXPLOSION_FRAME_HOLD;
        e->startDelay = delay;

        s16 px = centerX - EXPLOSION_SPR_W / 2;
        s16 py = centerY - EXPLOSION_SPR_H / 2;

        if (e->sprite == NULL)
            e->sprite = SPR_addSprite(&spr_explosion, px, py, TILE_ATTR(PAL_ENEMY, FALSE, FALSE, FALSE));
        else
            SPR_setPosition(e->sprite, px, py);

        SPR_setAnim(e->sprite, 0);
        SPR_setFrame(e->sprite, 0);
        SPR_setVisibility(e->sprite, delay > 0 ? HIDDEN : VISIBLE);
        return;
    }
}

void explosion_spawnAt(s16 centerX, s16 centerY)
{
    explosion_spawnAtDelayed(centerX, centerY, 0);
}

void explosions_update(void)
{
    for (u16 i = 0; i < MAX_EXPLOSIONS; i++)
    {
        Explosion *e = &explosions[i];
        if (!e->active)
            continue;

        if (e->startDelay > 0)
        {
            e->startDelay--;
            if (e->startDelay == 0)
                SPR_setVisibility(e->sprite, VISIBLE);
            continue;
        }

        e->frameTimer--;
        if (e->frameTimer == 0)
        {
            e->frameIndex++;
            if (e->frameIndex >= EXPLOSION_FRAME_COUNT)
            {
                e->active = FALSE;
                SPR_setVisibility(e->sprite, HIDDEN);
                continue;
            }

            SPR_setFrame(e->sprite, e->frameIndex);
            e->frameTimer = EXPLOSION_FRAME_HOLD;
        }
    }
}

void explosions_hideAll(void)
{
    for (u16 i = 0; i < MAX_EXPLOSIONS; i++)
    {
        Explosion *e = &explosions[i];
        if (!e->active)
            continue;

        e->active = FALSE;
        SPR_setVisibility(e->sprite, HIDDEN);
    }
}
