#ifndef PLAYER_H
#define PLAYER_H

#include "game.h"

typedef enum
{
    POWERUP_NONE,
    POWERUP_SPREAD,
    POWERUP_SPEED,
} PowerupType;

typedef enum
{
    PLAYER_ALIVE,        // playable, possibly during its post-respawn invulnerability window
    PLAYER_RESPAWN_WAIT, // dead, waiting out the pause before the next ship appears
    PLAYER_GAME_OVER,    // last life lost
} PlayerLifeState;

typedef struct
{
    Sprite *sprite;
    fix16 x;
    fix16 y;
    u16 fireCooldown;
    PowerupType activePowerup;
    u16 powerupTimer; // frames remaining, 0 = not active / permanent-until-death for untimed effects
    bool alive;        // TRUE only while state == PLAYER_ALIVE
    PlayerLifeState state;
    u8 lives;
    u16 respawnTimer; // PLAYER_RESPAWN_WAIT: frames remaining until the next ship appears
    u16 invulnTimer;  // PLAYER_ALIVE: frames remaining of post-respawn invulnerability (blinking)
} PlayerState;

extern PlayerState player;

void player_init(void);
void player_update(u16 joyState);
void player_applyPowerup(PowerupType type);
AABB player_getBounds(void);
void player_kill(void);
bool player_isGameOver(void);

#endif // PLAYER_H
