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

// Nulls the cached sprite handle -- boot-time only (see main.c and
// bullet.h's bullets_resetHandles(), same reasoning); not part of the
// restart-safe player_init() above.
void player_resetHandles(void);

void player_update(u16 joyState);
void player_applyPowerup(PowerupType type);
AABB player_getBounds(void);
void player_kill(void);
bool player_isGameOver(void);

// Grants one extra life (see score.c's EXTRA_LIFE_SCORE_INTERVAL) and plays
// its jingle. Capped at MAX_LIVES so the HUD's single-digit lives field
// (see score.c) never has to display two digits.
void player_addLife(void);

#endif // PLAYER_H
