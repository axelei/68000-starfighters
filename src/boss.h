#ifndef BOSS_H
#define BOSS_H

#include "game.h"

// Boss encounter (every 5th wave -- see formation.c's beginBossFight()). A
// singleton -- only one boss is ever active at a time -- so this is a small
// state machine, not a pool, structured like turret.c (an independent
// entity that reads a shared anchor position each frame) rather than a new
// EnemyKind, since nothing in enemy.c's dispatch supports a multi-part
// enemy with independently-destructible sub-entities.
void boss_init(void);

// Starts a new encounter: reclaims idle enemy/turret sprite handles (see
// enemies_releaseIdleSprites()/turrets_releaseIdleSprites()) for its own 6
// sprites, then swoops the body in from off-screen top to its first anchor
// point, spawning both weak spots.
void boss_begin(void);

// Advances the boss's phase state machine (repositioning/attacking),
// movement, attack patterns, hit-flash timers, and hidden life timer. Call
// once per frame, gated on boss_isActive() at the call site (main.c) same
// as every other subsystem's update.
void boss_update(void);

// True from boss_begin() until the encounter ends, either by both weak
// spots being destroyed or the hidden life timer expiring.
bool boss_isActive(void);

// Hides and deactivates the boss without releasing its sprites (called on
// player death, so it doesn't hang frozen on screen through the game-over
// prompt) -- mirrors enemies_hideAll()/turrets_hideAll().
void boss_hideAll(void);

// Broad bounding box covering the body plus both weak-spot pods, used for
// the player-ram-death check (see collision.c) -- the boss never dies from
// ramming, unlike BEE/SPECIAL/waver kinds.
AABB boss_getBounds(void);

// Bounding box of weak spot `index` (0 or 1), used by collision.c to check
// player bullets against it specifically. Returns a zero-size box (never
// overlaps anything) if that weak spot is already destroyed.
AABB boss_weakSpotBounds(u16 index);

// Applies damage to weak spot `index`; flashes it, and on death shows its
// destroyed husk frame. Once both are destroyed, plays the death scream,
// staggers explosions across the body, awards score, and ends the
// encounter. Called by collision.c.
void boss_hitWeakSpot(u16 index, s16 damage);

#endif // BOSS_H
