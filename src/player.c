#include "player.h"
#include "bullet.h"
#include "explosion.h"
#include "sfx.h"
#include "resources.h"

PlayerState player;

#define PLAYER_SPR_W 16
#define PLAYER_SPR_H 16

// Collision box is a smaller square centered inside the sprite rather than
// the full 16x16 tile area, so near-misses around the ship's edges (mostly
// transparent pixels/engine glow, not solid hull) don't count as hits.
#define HITBOX_W 8
#define HITBOX_H 8
#define HITBOX_OFFSET_X ((PLAYER_SPR_W - HITBOX_W) / 2)
#define HITBOX_OFFSET_Y ((PLAYER_SPR_H - HITBOX_H) / 2)

// Rows in spr_player's sheet (see generate_placeholders.py's player_ship())
// -- a single instance, so plain SPR_setAnim (auto tile upload) is fine, no
// need for the shared-VRAM-tile trick the many-instance enemies/bullets use.
#define ANIM_NEUTRAL    0
#define ANIM_LEAN_LEFT  1
#define ANIM_LEAN_RIGHT 2

#define BASE_SPEED       FIX16(1.5)
#define SPEED_BOOST_MUL  FIX16(1.6)
#define FIRE_COOLDOWN     8
#define BULLET_SPEED     FIX16(4.0)
#define SPREAD_ANGLE_VX  FIX16(1.0)
#define POWERUP_DURATION (60 * 10) // 10 seconds at 60fps

#define STARTING_LIVES     4
#define DEATH_PAUSE_FRAMES 60  // 1s at 60fps before the next ship appears
#define INVULN_DURATION    180 // 3s of post-respawn invulnerability
#define BLINK_INTERVAL     4   // frames per visibility toggle while invulnerable

static void respawn(void)
{
    player.x = FIX16((PLAY_AREA_X_MIN + PLAY_AREA_X_MAX) / 2);
    player.y = FIX16(PLAY_AREA_Y_MAX);
    player.fireCooldown = 0;
    player.activePowerup = POWERUP_NONE;
    player.powerupTimer = 0;
    player.invulnTimer = INVULN_DURATION;
    player.state = PLAYER_ALIVE;
    player.alive = TRUE;

    SPR_setPosition(player.sprite, F16_toInt(player.x), F16_toInt(player.y));
    SPR_setVisibility(player.sprite, VISIBLE);
    SPR_setAnim(player.sprite, ANIM_NEUTRAL);
}

void player_init(void)
{
    player.lives = STARTING_LIVES;
    if (player.sprite == NULL)
        player.sprite = SPR_addSprite(&spr_player, 0, 0, TILE_ATTR(PAL_SHIP, TRUE, FALSE, FALSE));
    respawn();
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

static void updateLeanAnim(u16 joyState)
{
    if (joyState & BUTTON_LEFT)
        SPR_setAnim(player.sprite, ANIM_LEAN_LEFT);
    else if (joyState & BUTTON_RIGHT)
        SPR_setAnim(player.sprite, ANIM_LEAN_RIGHT);
    else
        SPR_setAnim(player.sprite, ANIM_NEUTRAL);
}

static void updateInvulnerability(void)
{
    if (player.invulnTimer == 0)
        return;

    player.invulnTimer--;
    bool visible = ((player.invulnTimer / BLINK_INTERVAL) & 1) == 0;
    SPR_setVisibility(player.sprite, visible ? VISIBLE : HIDDEN);

    if (player.invulnTimer == 0)
        SPR_setVisibility(player.sprite, VISIBLE);
}

void player_update(u16 joyState)
{
    if (player.state == PLAYER_GAME_OVER)
        return;

    if (player.state == PLAYER_RESPAWN_WAIT)
    {
        player.respawnTimer--;
        if (player.respawnTimer == 0)
            respawn();
        return;
    }

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
    updateInvulnerability();
    updateLeanAnim(joyState);

    SPR_setPosition(player.sprite, F16_toInt(player.x), F16_toInt(player.y));
}

AABB player_getBounds(void)
{
    AABB box = {F16_toInt(player.x) + HITBOX_OFFSET_X, F16_toInt(player.y) + HITBOX_OFFSET_Y, HITBOX_W, HITBOX_H};
    return box;
}

void player_kill(void)
{
    if (player.state != PLAYER_ALIVE || player.invulnTimer > 0)
        return;

    explosion_spawnAt(F16_toInt(player.x) + PLAYER_SPR_W / 2, F16_toInt(player.y) + PLAYER_SPR_H / 2);
    sfx_play_explosion();

    player.alive = FALSE;
    SPR_setVisibility(player.sprite, HIDDEN);

    if (player.lives > 0)
        player.lives--;

    if (player.lives == 0)
    {
        player.state = PLAYER_GAME_OVER;
    }
    else
    {
        player.state = PLAYER_RESPAWN_WAIT;
        player.respawnTimer = DEATH_PAUSE_FRAMES;
    }
}

bool player_isGameOver(void)
{
    return player.state == PLAYER_GAME_OVER;
}
