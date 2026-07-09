#ifndef SFX_H
#define SFX_H

#include "game.h"

void sfx_init(void);

// Advances any in-progress sound effect sequences by one frame. Call once
// per frame (e.g. from the main loop, before SYS_doVBlankProcess).
void sfx_update(void);

void sfx_play_shoot(void);
void sfx_play_explosion(void);
void sfx_play_powerup(void);

// Extra life (see score.c's EXTRA_LIFE_SCORE_INTERVAL). Shares the powerup
// channel (see sfx.c) rather than claiming one of its own -- the PSG only
// has 4 channels and all 4 are already spoken for by shoot/powerup/
// deathscream/explosion; a life and a powerup pickup landing on the exact
// same frame is rare enough that one briefly cutting the other off is an
// acceptable trade for not needing a 5th channel.
void sfx_play_extraLife(void);

// Boss-only: descending-pitch scream on death (see boss.c). Plays alongside
// sfx_play_explosion() (different PSG channel, no conflict).
void sfx_play_bossDeathScream(void);

// Immediately silences every channel and cancels any in-progress sequence
// (called when leaving the game-over screen back to the title, so a sound
// that was mid-playback doesn't keep running into the title screen).
void sfx_stopAll(void);

#endif // SFX_H
