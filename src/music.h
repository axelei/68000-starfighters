#ifndef MUSIC_H
#define MUSIC_H

#include "game.h"

// Starts this wave's ingame background track. Rotates deterministically
// through the ingame roster (music.c's ingameTracks[]) one step per call
// rather than picking randomly, so consecutive waves vary in a fixed,
// reproducible order instead of repeating the same track or jumping around
// unpredictably.
//
// Called once per non-boss wave, from formation.c's beginInterwave() --
// the one place every such wave slot passes through, including the first.
// A boss wave instead calls music_startBoss() below; the wave right after
// the boss fight ends picks the next ingame track the same way any other
// wave does, simply by also passing through beginInterwave().
void music_startIngame(void);

// Starts the boss-encounter track -- see boss.c's boss_begin(). Only one
// boss track exists today, so this is deterministic by definition; it's
// still its own entry point (rather than inlining XGM2_play() at the call
// site) so a future second/third boss track can rotate the same way
// music_startIngame() does without touching boss.c.
void music_startBoss(void);

#endif // MUSIC_H
