#ifndef GAME_H
#define GAME_H

#include <genesis.h>

#include "settings.h"

// Picks between two precalculated values based on the console's actual
// region (IS_PAL_SYSTEM is a cheap VDP status-register read -- see
// C:/sgdk/inc/vdp.h -- safe to call anywhere/anytime, no boot-order
// dependency). When both arguments are themselves compile-time-constant
// expressions (e.g. `SECONDS * 60` / `SECONDS * 50`), the compiler folds
// each branch to a single immediate value ahead of time, so this never
// costs a runtime multiply -- only a register-read + select between two
// already-precalculated numbers. Used throughout this codebase's
// frame-count/velocity #defines so PAL (50fps) and NTSC (60fps) consoles
// see the same real-world pacing/speed instead of PAL running everything
// ~17% slower (see the PAL/NTSC parity plan).
#define REGION_PICK(ntscVal, palVal) (IS_PAL_SYSTEM ? (palVal) : (ntscVal))

// Hardware palette assignments, consolidated to 4 groups that each use all
// 16 slots (transparent/black/white/gray + shades of that group's colors --
// see generate_placeholders.py's module docstring). Title's own palette
// (title.c) briefly borrows PAL0 before title_fadeInGame() overwrites it
// with PAL_PLAYER's real colors, once gameplay actually starts.
#define PAL_PLAYER      PAL0 // ship, bullets (player's), HUD, powerups
#define PAL_ENEMY       PAL1 // bee/special/big, turret, explosion, enemy bullet, wavers
#define PAL_ENVIRONMENT PAL2 // terrain + starfield
#define PAL_BOSS        PAL3 // boss body/weak-spots, boss's homing bullet (see boss.c)

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
// Comfortable headroom above the biggest wave/inter-wave batch (see
// enemy.h's WAVER_SUBGROUP_SIZE) -- batches are spawned one at a time,
// reusing freed slots, specifically to stay well under the Genesis's
// 80-ever-allocated-sprite-object limit (see WAVER_SUBGROUP_COUNT's comment).
#define MAX_ENEMIES 48
#define MAX_POWERUPS        4

// Terrain scroll speed, fixed-point pixels/frame (see terrain.c). Shared
// with turret.c so ground turrets travel down the screen in lockstep with
// the terrain clumps they're attached to. PAL value is NTSC * 1.2 (60/50)
// so real-world pixels/sec stays the same despite PAL's slower tick rate.
#define TERRAIN_SPEED REGION_PICK(FIX16(0.55), FIX16(0.66))

typedef struct
{
    s16 x;
    s16 y;
    u16 w;
    u16 h;
} AABB;

bool aabb_overlaps(AABB a, AABB b);

#endif // GAME_H
