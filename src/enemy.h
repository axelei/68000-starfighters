#ifndef ENEMY_H
#define ENEMY_H

#include "game.h"

typedef enum
{
    ENEMY_KIND_BEE,
    ENEMY_KIND_SPECIAL,
    ENEMY_KIND_BIG,
    // Inter-wave-only "waver" kinds (see ENEMY_STATE_WAVING/formation.c's
    // beginInterwave()) -- 1 HP, differ only in sprite/color. Which of the
    // WAVER_PATH_COUNT precalculated movement paths (interwave_generated.h)
    // a batch flies is picked independently at random, not tied to kind --
    // see enemy.c's spawnNextWaverSubgroup().
    ENEMY_KIND_WAVER_A,
    ENEMY_KIND_WAVER_B,
    ENEMY_KIND_WAVER_C,
} EnemyKind;

#define WAVER_KIND_COUNT 3 // ENEMY_KIND_WAVER_A/B/C

// One inter-wave formation = WAVER_SUBGROUP_COUNT batches of WAVER_GRID_ROWS
// x WAVER_GRID_COLS (WAVER_SUBGROUP_SIZE) enemies, spawned one batch at a
// time -- see enemy_spawnWaverFormation(). Only ever ONE batch's worth of
// enemies exists at once (the next doesn't spawn until the current one is
// entirely gone), which is what actually matters for sprite budget: SGDK
// hard-caps the whole game to 80 *ever-allocated* sprite objects (not just
// currently-visible ones -- see MAX_SPRITE in sprite_eng.c), and none of our
// pools (bullets/explosions/powerups/turrets/enemies) ever release theirs.
// Spawning every batch up front (the original design) needed
// WAVER_SUBGROUP_SIZE*WAVER_SUBGROUP_COUNT distinct sprite objects
// simultaneously and blew through that budget (bullets stopped rendering);
// spawning one batch at a time and reusing the same MAX_ENEMIES slots keeps
// the enemy pool's contribution bounded to WAVER_SUBGROUP_SIZE regardless of
// how many batches make up the whole formation.
#define WAVER_GRID_ROWS      3
#define WAVER_GRID_COLS      4
#define WAVER_SUBGROUP_SIZE  (WAVER_GRID_ROWS * WAVER_GRID_COLS) // 12
#define WAVER_SUBGROUP_COUNT 5

// Two distinct inter-wave formation shapes, alternated automatically each
// time enemy_spawnWaverFormation() is called (see currentWaverShape in
// enemy.c) -- GRID_WEAVE is the original top-down weaving grid above;
// SIDE_DIVE is rows of 2 enemies entering fast from the left or right edge
// of the playfield and curving into a steep dive off the bottom. Both share
// the same batch/sequencing machinery (isWaver, waverBatchesSpawned, etc.),
// just with different spawn layouts and per-frame motion -- see
// spawnNextWaverSubgroup() and the ENEMY_STATE_WAVING branch in
// enemies_update().
typedef enum
{
    WAVER_SHAPE_GRID_WEAVE,
    WAVER_SHAPE_SIDE_DIVE,
} WaverShape;

#define WAVER_SHAPE_COUNT 2

typedef enum
{
    ENEMY_STATE_ENTERING,
    ENEMY_STATE_IN_FORMATION,
    ENEMY_STATE_DIVING_OUT, // BEE/SPECIAL only: diving down and off the bottom of the screen
    ENEMY_STATE_WAVING,     // waver kinds only: flying its batch's precalculated path -- entered straight into this, no settle-in first
} EnemyLifeState;

typedef struct
{
    bool active;
    Sprite *sprite;
    EnemyKind kind;
    EnemyLifeState state;
    fix16 x;
    fix16 y;
    fix16 vx; // path velocity, used while ENTERING/DIVING_OUT/SIDE_DIVE's sweep-in phase
    fix16 vy;
    s16 slotX;
    s16 slotY;
    u16 enterTimer; // frame counter within the current ENTERING/DIVING_* path
    u16 enterDuration; // frames the current ENTERING swoop takes to finish -- ENTER_DURATION by
                        // default (see enemy_spawn()), overridden for a faster SIDE_DIVE sweep-in
    s16 startDelay; // frames to wait, hidden, before entrance begins
    s16 hp;
    s16 maxHp;
    u16 flashTimer;    // frames remaining showing the white hit-flash sprite frame
    u16 diveCooldown;  // BEE/SPECIAL only: frames until eligible to dive again
    u16 fireCooldown;  // BIG only: frames until its next shot
    u16 driftTimer;    // BEE/SPECIAL only: frames until this enemy's small
                       // in-formation wander picks a new drift direction
    bool diving;       // BEE/SPECIAL only: away from its slot for a dive (counts against MAX_CONCURRENT_DIVERS)
    bool forcedOut;    // set by enemies_forceDiveAllOut() -- once off screen, deactivate for good instead of re-entering
    bool isWaver;          // waver kinds only: TRUE from spawn, see enemy_spawnWaverFormation()
    s16 groupOffsetX;      // waver kinds only: fixed horizontal offset from its batch's shared path position
                           // (the row's *vertical* offset only matters at spawn -- it's baked into the
                           // initial y once, and every WAVING enemy descends at the same constant rate)
    s16 waverRowPhase;     // GRID_WEAVE only: this row's offset into the shared path clock, relative
                           // to its OWN reveal time rather than the batch's spawn time (negative --
                           // cancels out the startDelay frames the shared clock already ticked
                           // through while this row was still hidden) -- see ENEMY_STATE_WAVING in
                           // enemy.c for why rows weave independently instead of moving as one rigid
                           // block, and why this can't just be the row index times a small step
} Enemy;

extern Enemy enemies[MAX_ENEMIES];

void enemies_init(void);
void enemies_update(void);

// Nulls every cached sprite handle -- boot-time only (see main.c and
// bullet.h's bullets_resetHandles(), same reasoning); not part of the
// restart-safe enemies_init() above.
void enemies_resetHandles(void);

// Number of active BEE/SPECIAL ("small") enemies -- turrets (see turret.c)
// only spawn once this reaches 0.
u16 enemies_countSmall(void);

// Number of active enemies of any kind -- formation.c uses this to detect
// when a wave has been fully cleared.
u16 enemies_countActive(void);

// Hides and deactivates every active enemy without releasing its sprite,
// awarding no score/dropping no powerup (called on player death, so the
// formation doesn't hang frozen on screen through the game-over prompt).
void enemies_hideAll(void);

// Releases (SPR_releaseSprite) every currently-inactive enemy's sprite
// handle back to SGDK's pool and nulls it -- called every frame from main.c
// (see game.h's sprite-budget comment) so the sprite-object budget tracks
// how many enemies are actually alive right now rather than the historical
// high-water mark, plus once more from boss.c's boss_begin() (where
// formation.c already guarantees enemies_countActive()==0, reclaiming every
// slot at once right before the boss's own sprites need them). Safe
// regardless of timing: only touches slots already confirmed inactive. The
// existing lazy `if (e->sprite == NULL) SPR_addSpriteEx(...)` in
// enemy_spawn() already handles recreating it next time an enemy spawns.
void enemies_releaseIdleSprites(void);

// Sends every active enemy into (or keeps it in) ENEMY_STATE_DIVING_OUT from
// wherever it currently is, marked so that once it scrolls off the bottom of
// the screen it deactivates for good instead of re-entering -- see
// formation.c's wave timer. Awards no score/powerup, same as enemies_hideAll,
// but plays out as a visible dive instead of vanishing instantly.
void enemies_forceDiveAllOut(void);

// Starts one inter-wave formation of the given kind: WAVER_SUBGROUP_COUNT
// batches of WAVER_SUBGROUP_SIZE enemies, spawned one batch at a time as
// each previous one fully clears (see enemy.h's top comment for why).
// Returns immediately after spawning just the first batch; enemies_update()
// drives the rest. See formation.c's beginInterwave().
void enemy_spawnWaverFormation(EnemyKind kind);

// True once every batch of the current inter-wave formation has been
// spawned and fully cleared (killed or flown off screen) -- formation.c
// polls this instead of enemies_countActive() during the inter-wave phase,
// since that would also read as "clear" during the brief pause between
// batches.
bool enemies_waverFormationDone(void);

// How many waver enemies were killed (not merely deactivated by flying off
// screen) across every batch since the last enemy_spawnWaverFormation()
// call -- formation.c compares this to enemies_waverTotalCount() to award a
// perfect-clear bonus only if every one of them was actually shot down.
u16 enemies_waverKillCount(void);

// Total enemies across every batch of the current inter-wave formation --
// depends on which WaverShape got picked (GRID_WEAVE and SIDE_DIVE spawn
// different numbers per batch), so this is a function rather than a fixed
// macro like the old WAVER_TOTAL_COUNT.
u16 enemies_waverTotalCount(void);

// True for ENEMY_KIND_WAVER_A/B/C -- see collision.c's ram-death rule (as
// fragile as BEE/SPECIAL, dies on contact with the player too).
bool enemy_isWaverKind(EnemyKind kind);

// Sprite pixel size for a given kind (16x16 for BEE/SPECIAL, 32x32 for BIG).
u16 enemy_widthForKind(EnemyKind kind);
u16 enemy_heightForKind(EnemyKind kind);

// Spawns an inactive enemy starting at (startX,startY), swooping in to
// (slotX,slotY) over a fixed entrance duration, after waiting `delay` frames.
Enemy *enemy_spawn(EnemyKind kind, s16 startX, s16 startY, s16 slotX, s16 slotY, s16 delay);

AABB enemy_getBounds(const Enemy *e);

// True if (worldX,worldY) lands on a solid (non-transparent) pixel of e's
// sprite -- always TRUE for kinds other than ENEMY_KIND_BIG (see enemy.c),
// so callers can call this unconditionally after an AABB overlap without
// special-casing kind themselves. Meant to refine an already-passed
// aabb_overlaps() check, not replace it -- points outside the AABB aren't
// meaningfully "inside" just because they'd read FALSE.
bool enemy_hitTestPixel(const Enemy *e, s16 worldX, s16 worldY);

// Applies damage to the enemy, flashing it white for a few frames; kills it
// (see enemy_kill) once hp reaches 0. Called by the collision system.
void enemy_hit(Enemy *e, s16 damage);

// Deactivates the enemy, awards score, and drops a powerup if it's a
// "special" kind.
void enemy_kill(Enemy *e);

#endif // ENEMY_H
