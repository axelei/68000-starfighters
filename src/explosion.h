#ifndef EXPLOSION_H
#define EXPLOSION_H

#include "game.h"

// See game.h's own comment on MAX_PLAYER_BULLETS/MAX_ENEMY_BULLETS/
// MAX_ENEMIES -- this pool's Sprite handles are released as soon as an
// explosion finishes (explosions_releaseIdleSprites(), called every frame
// from main.c), so its size is sized against actual observed concurrent
// usage rather than the whole-game ever-allocated ceiling.
#define MAX_EXPLOSIONS 12

void explosions_init(void);
void explosions_update(void);

// Releases (SPR_releaseSprite) every currently-inactive explosion's sprite
// handle back to SGDK's pool -- see bullet.h's bullets_releaseIdleSprites().
void explosions_releaseIdleSprites(void);

// Nulls every cached sprite handle -- boot-time only (see main.c and
// bullet.h's bullets_resetHandles(), same reasoning); not part of the
// restart-safe explosions_init() above.
void explosions_resetHandles(void);

// Hides and deactivates every active explosion without releasing its sprite
// (called on final player death, so a mid-animation burst doesn't hang
// frozen on screen through the game-over prompt).
void explosions_hideAll(void);

// Spawns an explosion centered at (centerX,centerY), hidden for `delay`
// frames before it starts animating (used to stagger the multiple bursts
// triggered by a big enemy's death). Pass delay=0 to start immediately.
void explosion_spawnAtDelayed(s16 centerX, s16 centerY, u16 delay);
void explosion_spawnAt(s16 centerX, s16 centerY);

#endif // EXPLOSION_H
