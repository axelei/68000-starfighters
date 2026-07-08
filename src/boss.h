#ifndef BOSS_H
#define BOSS_H

#include "game.h"

// Boss encounter (every 3rd wave -- see formation.c's beginEncounter()). A
// singleton -- only one boss is ever active at a time -- so this is a small
// state machine, not a pool, structured like turret.c (an independent
// entity that reads a shared anchor position each frame) rather than a new
// EnemyKind, since nothing in enemy.c's dispatch supports a multi-part
// enemy with independently-destructible sub-entities.

// A roster of 5 distinct bosses (see boss.c's BossDef table), each with its
// own body/weak-spot art, palette, weak-spot count/placement, movement
// speed, and attack-pattern mix. formation.c picks which kind fights at
// each boss wave (cycling through the roster) and passes it to
// boss_begin().
typedef enum
{
    BOSS_KIND_A,
    BOSS_KIND_B,
    BOSS_KIND_C,
    BOSS_KIND_D,
    BOSS_KIND_E,
    BOSS_KIND_COUNT,
} BossKind;

void boss_init(void);

// Starts a new encounter of the given kind: reclaims idle enemy/turret
// sprite handles (see enemies_releaseIdleSprites()/
// turrets_releaseIdleSprites()) for its own sprites, loads that kind's
// body/weak-spot VRAM tiles and swaps its palette onto hardware PAL3 (see
// boss.c's BossDef -- only one kind's art is ever resident at a time),
// then swoops the body in from off-screen top to its first anchor point,
// spawning its weak spots.
void boss_begin(BossKind kind);

// Advances the boss's phase state machine (repositioning/attacking),
// movement, attack patterns, hit-flash timers, and hidden life timer. Call
// once per frame, gated on boss_isActive() at the call site (main.c) same
// as every other subsystem's update.
void boss_update(void);

// True from boss_begin() until the encounter ends, either by every weak
// spot being destroyed or the hidden life timer expiring.
bool boss_isActive(void);

// Hides and deactivates the boss without releasing its sprites (called on
// player death, so it doesn't hang frozen on screen through the game-over
// prompt) -- mirrors enemies_hideAll()/turrets_hideAll().
void boss_hideAll(void);

// Broad bounding box covering the body plus every weak-spot pod, used for
// the player-ram-death check (see collision.c) -- the boss never dies from
// ramming, unlike BEE/SPECIAL/waver kinds.
AABB boss_getBounds(void);

// Number of weak spots (1-3) the currently-active kind has -- collision.c
// uses this as its loop bound instead of a hardcoded count, since it
// varies per BossDef.
u16 boss_weakSpotCount(void);

// Bounding box of weak spot `index` (0..boss_weakSpotCount()-1), used by
// collision.c to check player bullets against it specifically. Returns a
// zero-size box (never overlaps anything) if that weak spot is already
// destroyed.
AABB boss_weakSpotBounds(u16 index);

// Applies damage to weak spot `index`; flashes it, and on death shows its
// destroyed husk frame. Once every weak spot is destroyed, plays the death
// scream, staggers explosions across the body, awards score, and ends the
// encounter. Called by collision.c.
void boss_hitWeakSpot(u16 index, s16 damage);

#endif // BOSS_H
