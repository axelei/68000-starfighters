#ifndef FORMATION_H
#define FORMATION_H

#include "game.h"

// Starts the first wave of a new game (also resets the "waves cleared"
// counter). Wave layouts come from waves_generated.h (see res/waves.txt /
// res/generate_waves.py).
void formation_init(void);

// Advances formation logic; once the current wave is fully cleared (no
// active enemies left, see enemies_countActive()), automatically spawns the
// next one after a short pause. Waves loop back to the first once the last
// defined one is cleared.
void formation_update(void);

// How many waves the player has fully cleared so far this game.
u16 formation_wavesCleared(void);

#endif // FORMATION_H
