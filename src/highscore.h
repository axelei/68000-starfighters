#ifndef HIGHSCORE_H
#define HIGHSCORE_H

#include "game.h"

typedef struct
{
    bool newHighScore;
    bool newHighWave;
} HighScoreResult;

// Loads the saved high score / highest wave reached from battery-backed
// SRAM (see src/boot/rom_head.c's ROMHeader sram_* fields for the declared
// window). Call once at boot, before the title loop -- see main.c. Detects
// SRAM that's never actually been written by this game (first ever
// power-on, or hardware with no working battery) via a magic marker, and
// initializes it to 0/0 in that case instead of trusting whatever garbage
// happens to be sitting there.
void highscore_init(void);

u32 highscore_getScore(void);
u16 highscore_getWave(void);

// Compares this round's final score / wave reached against the saved
// records, persisting to SRAM (and updating the cached values above)
// whichever one(s) got beaten. Called once per game over -- see
// score_showGameOver().
HighScoreResult highscore_checkAndUpdate(u32 finalScore, u16 waveReached);

// Resets both records back to 0 -- see options.c's "RESET RECORDS" row.
void highscore_reset(void);

#endif // HIGHSCORE_H
