#ifndef GAME_H
#define GAME_H

#include <genesis.h>

// Hardware palette assignments (4 palettes total, 16 colors each).
#define PAL_SHIP  PAL0
#define PAL_ENEMY PAL1
#define PAL_PWR   PAL2
#define PAL_TERRA PAL3

// Play area: the region the player ship is allowed to move within, near the
// bottom of the 320x224 screen.
#define PLAY_AREA_X_MIN   8
#define PLAY_AREA_X_MAX   (320 - 8 - 16)
#define PLAY_AREA_Y_MIN   120
#define PLAY_AREA_Y_MAX   (224 - 8 - 16)

#define SCREEN_W 320
#define SCREEN_H 224

#define MAX_PLAYER_BULLETS 16
#define MAX_ENEMY_BULLETS  24
#define MAX_ENEMIES        32
#define MAX_POWERUPS        4

typedef struct
{
    s16 x;
    s16 y;
    u16 w;
    u16 h;
} AABB;

bool aabb_overlaps(AABB a, AABB b);

#endif // GAME_H
