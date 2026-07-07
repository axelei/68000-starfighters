#ifndef SETTINGS_H
#define SETTINGS_H

// Debug/dev-only overrides, all disabled (0/FALSE) by default -- flip these
// while working on a specific wave or feature instead of having to play
// through from the start every time. Must all be back at their disabled
// values before shipping a release build.

// Wave index (0-based, mod WAVE_COUNT -- see waves_generated.h) the game
// starts on, instead of wave 0. See formation_init().
#define DEBUG_START_WAVE 0

// If TRUE, the player can never take a hit -- player_kill() becomes a no-op.
// See player_kill() in player.c.
#define DEBUG_INVULNERABLE FALSE

// If nonzero, overrides player.c's normal STARTING_LIVES.
#define DEBUG_STARTING_LIVES 0

#endif // SETTINGS_H
