#ifndef MUSIC_H
#define MUSIC_H

#include "game.h"

// Turns background music down a bit relative to XGM2's own full-scale
// default, so it doesn't drown out sfx.c's effects (both the PSG synth ones
// and the newer PCM samples -- see sfx_play_bossExplosionSample()/
// sfx_play_playerExplosionSample() -- which have no independent volume
// control of their own on the XGM2 driver). Call exactly once, right after
// the XGM2 driver first loads (see intro.c) -- the setting persists across
// every later XGM2_play() this session, so it never needs repeating.
void music_init(void);

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

// Starts the game-over track -- see score.c's score_showGameOver(). Only
// one game-over track exists today, same reasoning as music_startBoss().
void music_startGameOver(void);

#endif // MUSIC_H
