#include "player.h"
#include "bullet.h"
#include "sfx.h"
#include "resources.h"

PlayerState player;

#define PLAYER_SPR_W 16
#define PLAYER_SPR_H 16

#define BASE_SPEED       FIX16(1.5)
#define SPEED_BOOST_MUL  FIX16(1.6)
#define FIRE_COOLDOWN     8
#define BULLET_SPEED     FIX16(4.0)
#define SPREAD_ANGLE_VX  FIX16(1.0)
#define POWERUP_DURATION (60 * 10) // 10 seconds at 60fps

void player_init(void)
{
    player.x = FIX16((PLAY_AREA_X_MIN + PLAY_AREA_X_MAX) / 2);
    player.y = FIX16(PLAY_AREA_Y_MAX);
    player.fireCooldown = 0;
    player.activePowerup = POWERUP_NONE;
    player.powerupTimer = 0;
    player.alive = TRUE;
    player.sprite = SPR_addSprite(&spr_player, F16_toInt(player.x), F16_toInt(player.y),
                                   TILE_ATTR(PAL_SHIP, TRUE, FALSE, FALSE));
}

static void clampToPlayArea(void)
{
    if (player.x < FIX16(PLAY_AREA_X_MIN)) player.x = FIX16(PLAY_AREA_X_MIN);
    if (player.x > FIX16(PLAY_AREA_X_MAX)) player.x = FIX16(PLAY_AREA_X_MAX);
    if (player.y < FIX16(PLAY_AREA_Y_MIN)) player.y = FIX16(PLAY_AREA_Y_MIN);
    if (player.y > FIX16(PLAY_AREA_Y_MAX)) player.y = FIX16(PLAY_AREA_Y_MAX);
}

static void handleFire(u16 joyState)
{
    if (player.fireCooldown > 0)
        player.fireCooldown--;

    if (!(joyState & BUTTON_A) && !(joyState & BUTTON_B) && !(joyState & BUTTON_C))
        return;

    if (player.fireCooldown > 0)
        return;

    fix16 nx = player.x + FIX16(PLAYER_SPR_W / 2 - 4);
    fix16 ny = player.y;

    if (player.activePowerup == POWERUP_SPREAD)
    {
        bullet_spawn_player(nx, ny, -SPREAD_ANGLE_VX, -BULLET_SPEED);
        bullet_spawn_player(nx, ny, 0, -BULLET_SPEED);
        bullet_spawn_player(nx, ny, SPREAD_ANGLE_VX, -BULLET_SPEED);
    }
    else
    {
        bullet_spawn_player(nx, ny, 0, -BULLET_SPEED);
    }

    sfx_play_shoot();
    player.fireCooldown = FIRE_COOLDOWN;
}

static void handlePowerupTimer(void)
{
    if (player.activePowerup == POWERUP_NONE || player.powerupTimer == 0)
        return;

    player.powerupTimer--;
    if (player.powerupTimer == 0)
        player.activePowerup = POWERUP_NONE;
}

void player_applyPowerup(PowerupType type)
{
    player.activePowerup = type;
    player.powerupTimer = POWERUP_DURATION;
    sfx_play_powerup();
}

void player_update(u16 joyState)
{
    if (!player.alive)
        return;

    fix16 speed = BASE_SPEED;
    if (player.activePowerup == POWERUP_SPEED)
        speed = F16_mul(BASE_SPEED, SPEED_BOOST_MUL);

    if (joyState & BUTTON_UP)    player.y -= speed;
    if (joyState & BUTTON_DOWN)  player.y += speed;
    if (joyState & BUTTON_LEFT)  player.x -= speed;
    if (joyState & BUTTON_RIGHT) player.x += speed;

    clampToPlayArea();
    handleFire(joyState);
    handlePowerupTimer();

    SPR_setPosition(player.sprite, F16_toInt(player.x), F16_toInt(player.y));
}

AABB player_getBounds(void)
{
    AABB box = {F16_toInt(player.x), F16_toInt(player.y), PLAYER_SPR_W, PLAYER_SPR_H};
    return box;
}

void player_kill(void)
{
    player.alive = FALSE;
    SPR_setVisibility(player.sprite, HIDDEN);
}
