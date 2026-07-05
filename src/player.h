#ifndef PLAYER_H
#define PLAYER_H

#include "game.h"

typedef enum
{
    POWERUP_NONE,
    POWERUP_SPREAD,
    POWERUP_SPEED,
} PowerupType;

typedef struct
{
    Sprite *sprite;
    fix16 x;
    fix16 y;
    u16 fireCooldown;
    PowerupType activePowerup;
    u16 powerupTimer; // frames remaining, 0 = not active / permanent-until-death for untimed effects
    bool alive;
} PlayerState;

extern PlayerState player;

void player_init(void);
void player_update(u16 joyState);
void player_applyPowerup(PowerupType type);
AABB player_getBounds(void);
void player_kill(void);

#endif // PLAYER_H
