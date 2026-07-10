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

// 1-indexed wave slot currently in play -- unlike formation_wavesCleared()+1,
// this reads correctly even when starting mid-game via DEBUG_START_WAVE
// (settings.h), since it's the wave itself rather than a count of finished
// ones.
u16 formation_currentWave(void);

// True once the first wave's enemies have actually been spawned (i.e. the
// initial "WAVE 1" announcement has finished) -- see turret.c's trySpawn(),
// which must not start placing ground turrets before then: with zero
// enemies on screen yet, the "formation thinned out" eligibility check would
// otherwise look identical to a genuinely cleared wave.
bool formation_enemiesSpawned(void);

// Seconds left before the current wave's remaining enemies get forced to
// dive off screen -- see WAVE_TIME_LIMIT_FRAMES (formation.c) and score.c's
// HUD display of it. 0 once they've already been forced out (or before the
// first wave has spawned).
u16 formation_waveSecondsLeft(void);

#endif // FORMATION_H
