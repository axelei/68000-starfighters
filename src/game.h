#ifndef GAME_H
#define GAME_H

#include <genesis.h>

#include "settings.h"

// Hardware palette assignments (4 palettes total, 16 colors each).
#define PAL_SHIP  PAL0
#define PAL_ENEMY PAL1
#define PAL_PWR   PAL2
#define PAL_TERRA PAL3

// HUD panel: a static column on the right of the screen (see score.c), out
// of the playfield the player/enemies move within.
#define HUD_PANEL_COLS       8
#define HUD_PANEL_COL0        (40 - HUD_PANEL_COLS)  // first panel column
#define HUD_PANEL_X_PX        (HUD_PANEL_COL0 * 8)   // px where the panel starts

// Play area: the full region (minus small margins) the player/enemies may
// occupy, to the left of the HUD panel.
#define PLAY_AREA_X_MIN   8
#define PLAY_AREA_X_MAX   (HUD_PANEL_X_PX - 8 - 16)
#define PLAY_AREA_Y_MIN   8
#define PLAY_AREA_Y_MAX   (224 - 8 - 16)

#define SCREEN_W 320
#define SCREEN_H 224

#define MAX_PLAYER_BULLETS 16
#define MAX_ENEMY_BULLETS  24
// Headroom above WAVER_TOTAL_COUNT (enemy.h): enemy_spawnWaverFormation()
// pre-spawns every inter-wave sub-block up front (most sitting HIDDEN with a
// long startDelay until their turn), so the *software* pool needs a slot for
// all of them at once even though only one sub-block's sprites are ever
// actually visible on the VDP at a time -- see WAVER_SUBGROUP_COUNT's
// comment for why that matters for the Genesis's 80-sprite hardware limit.
#define MAX_ENEMIES 48
#define MAX_POWERUPS        4

// Terrain scroll speed, fixed-point pixels/frame (see terrain.c). Shared
// with turret.c so ground turrets travel down the screen in lockstep with
// the terrain clumps they're attached to.
#define TERRAIN_SPEED FIX16(0.55)

typedef struct
{
    s16 x;
    s16 y;
    u16 w;
    u16 h;
} AABB;

bool aabb_overlaps(AABB a, AABB b);

#endif // GAME_H
