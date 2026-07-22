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

// Every pool below (plus explosion.h's MAX_EXPLOSIONS, turret.h's
// MAX_TURRETS, score.c's life icons/GAME OVER letters, player.c's own ship)
// hands out SGDK Sprite objects that are never SPR_released -- once a pool
// slot is used, its handle is kept and reused (SPR_setDefinition()) for the
// rest of the session, never given back (see enemy.h's top comment for the
// one exception -- turrets/enemies during a boss fight -- and its own
// account of this exact ceiling getting hit before, with bullets silently
// failing to render). SGDK hard-caps the WHOLE GAME to 80 *ever-allocated*
// sprite objects at once (sprite_eng.c's MAX_SPRITE -- a real Genesis
// hardware limit, not a tunable), and none of these pools' sizes were ever
// picked with the *sum* across every pool in mind, only each in isolation.
// Add up generous headroom in several of them at once (as bullets/
// explosions/enemies all eventually will, given a long enough session) and
// the total quietly sails past 80 -- from then on, whichever pool tries to
// allocate a fresh slot next just doesn't get one, and that entity stays
// logically alive/active but permanently unrendered until it's killed or
// times out (exactly what made turrets/enemies "invisible" after several
// waves). These sizes are trimmed to real gameplay needs (see waves.txt via
// waves_generated.h for MAX_ENEMIES; the rest were simply over-provisioned)
// so the grand total across every pool stays safely under 80.
#define MAX_PLAYER_BULLETS 10
#define MAX_ENEMY_BULLETS  10
// Covers the biggest wave (see waves_generated.h -- wave 10's 4 BIG + 4x6
// grid = 28) with a couple of spare slots, not the inter-wave waver batch
// (WAVER_SUBGROUP_SIZE/SIDE_DIVE_SUBGROUP_SIZE in enemy.h, both well under
// this already) -- see this block's own comment on why "headroom" isn't
// free here.
#define MAX_ENEMIES 30
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
