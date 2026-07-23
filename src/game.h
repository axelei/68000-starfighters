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
// hands out SGDK Sprite objects, and SGDK hard-caps the WHOLE GAME to 80
// *simultaneously-allocated* sprite objects (sprite_eng.c's MAX_SPRITE -- a
// real Genesis hardware limit, not a tunable): once a slot is used, its
// handle stays valid and is reused (SPR_setDefinition()) the next time that
// same pool slot is needed, but every pool with high churn (bullets,
// explosions, enemies, turrets) now releases a slot's handle back to SGDK
// (SPR_releaseSprite()) the moment it goes idle -- see each pool's own
// *_releaseIdleSprites(), called every frame from main.c -- instead of
// holding it forever. That's what makes generous pool sizes affordable at
// all: the budget that matters is how many of these are ever actually alive
// at the same instant, not the sum of every pool's array size. Without this
// (the previous design), the total *ever-allocated* count only ever grew
// for the whole session, and quietly sailed past 80 the first time several
// pools' generous headroom happened to fill up at once -- from then on,
// whichever pool tried to allocate a fresh slot next just didn't get one,
// and that entity stayed logically alive/active but permanently unrendered
// until it was killed or timed out (exactly what made turrets/enemies
// "invisible" after several waves).
//
// The always-on, non-adjustable floor: player ship (1) + score.c's life
// icons (LIFE_ICON_MAX, 4) + GAME OVER letters (GAMEOVER_LETTER_COUNT, 8) =
// 13, allocated once at score_init() and held for the whole session
// regardless of what's happening in gameplay. MAX_ENEMIES is sized to real
// wave data, not guesswork (see its own comment below) -- growing it past
// that buys nothing since no wave ever asks for more; it isn't the
// bottleneck on how many enemies appear at once (waves.txt is). Everything
// else below is sized generously against actual observed concurrent usage
// (via emulator sprite-table inspection) rather than the old worst-case
// sum -- boss.c's own body/weak-spot sprites are separate again: enemies
// are released via enemies_releaseIdleSprites() right before a boss fight
// (formation.c guarantees none are active by then) and boss.c releases its
// own on endFight(); turrets are the one pool deliberately left alone at
// that transition (see boss_begin()) so one still mid-fight keeps fighting
// into the encounter instead of being yanked off screen.
// POWERUP_SPREAD fires 3 bullets per shot (see player.c's handleFire()),
// each alive for ~PLAY_AREA height / BULLET_SPEED frames (~52 @ NTSC's
// 4.0px/frame from the bottom of the play area) while refiring every
// FIRE_COOLDOWN (8 NTSC/7 PAL) frames -- held down continuously that's
// ~(52/8)*3 =~ 20 concurrent player bullets in the worst case, so this
// needs real headroom above a plain single-shot estimate.
#define MAX_PLAYER_BULLETS 24
#define MAX_ENEMY_BULLETS  20
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
