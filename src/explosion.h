#ifndef EXPLOSION_H
#define EXPLOSION_H

#include "game.h"

#define MAX_EXPLOSIONS 12

void explosions_init(void);
void explosions_update(void);

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
