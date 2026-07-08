#ifndef SETTINGS_H
#define SETTINGS_H

// Gameplay tuning.

// Seconds a wave's enemies get before the remaining ones are forced to dive
// off screen for good (no score/powerups) and the next wave starts -- see
// formation.c's waveTimer/enemies_forceDiveAllOut().
#define WAVE_TIME_LIMIT_SECONDS 200

// Seconds of pause between one inter-wave batch fully clearing and the next
// appearing (see formation.c's beginInterwave()/enemy.c's
// enemy_spawnWaverFormation()) -- every wave is preceded by several batches
// of fragile enemies flying through, one at a time, before the real wave
// starts. Batches are spawned sequentially (never more than one alive at
// once) rather than all up front -- see WAVER_SUBGROUP_COUNT's comment in
// enemy.h for why that matters well beyond just this pause.
#define INTERWAVE_ENTRY_STAGGER_SECONDS 4

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
