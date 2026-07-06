#include "powerup.h"
#include "resources.h"

Powerup powerups[MAX_POWERUPS];

#define POWERUP_SPR_W 16
#define POWERUP_SPR_H 16
#define DRIFT_SPEED FIX16(0.6)

void powerups_init(void)
{
    for (u16 i = 0; i < MAX_POWERUPS; i++)
    {
        powerups[i].active = FALSE;
        // Sprite handle intentionally left alone -- see bullet.c pool_init.
    }
}

void powerup_spawnAt(s16 x, s16 y)
{
    for (u16 i = 0; i < MAX_POWERUPS; i++)
    {
        Powerup *p = &powerups[i];
        if (p->active)
            continue;

        // random() is SGDK's pseudo-random utility (see tools.h); verify
        // against the installed SGDK version if this fails to link.
        PowerupType type = (random() & 1) ? POWERUP_SPEED : POWERUP_SPREAD;
        const SpriteDefinition *def = (type == POWERUP_SPEED) ? &spr_powerup_speed : &spr_powerup_spread;

        p->active = TRUE;
        p->type = type;
        p->x = FIX16(x);
        p->y = FIX16(y);

        if (p->sprite == NULL)
            p->sprite = SPR_addSprite(def, x, y, TILE_ATTR(PAL_PWR, FALSE, FALSE, FALSE));
        else
        {
            // This pool slot may have last shown the other powerup type --
            // update its visual to match the one just chosen (cheap, since
            // this sprite isn't using SPR_FLAG_AUTO_VRAM_ALLOC/shared VRAM).
            SPR_setDefinition(p->sprite, def);
            SPR_setVisibility(p->sprite, VISIBLE);
            SPR_setPosition(p->sprite, x, y);
        }
        return;
    }
}

void powerup_collect(Powerup *p)
{
    p->active = FALSE;
    SPR_setVisibility(p->sprite, HIDDEN);
    player_applyPowerup(p->type);
}

AABB powerup_getBounds(const Powerup *p)
{
    AABB box = {F16_toInt(p->x), F16_toInt(p->y), POWERUP_SPR_W, POWERUP_SPR_H};
    return box;
}

void powerups_update(void)
{
    for (u16 i = 0; i < MAX_POWERUPS; i++)
    {
        Powerup *p = &powerups[i];
        if (!p->active)
            continue;

        p->y += DRIFT_SPEED;

        s16 py = F16_toInt(p->y);
        if (py > SCREEN_H)
        {
            p->active = FALSE;
            SPR_setVisibility(p->sprite, HIDDEN);
            continue;
        }

        SPR_setPosition(p->sprite, F16_toInt(p->x), py);
    }
}

void powerups_hideAll(void)
{
    for (u16 i = 0; i < MAX_POWERUPS; i++)
    {
        Powerup *p = &powerups[i];
        if (!p->active)
            continue;

        p->active = FALSE;
        SPR_setVisibility(p->sprite, HIDDEN);
    }
}
