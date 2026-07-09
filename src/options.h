#ifndef OPTIONS_H
#define OPTIONS_H

#include "game.h"

#define OPTIONS_MIN_LIVES 1
#define OPTIONS_MAX_LIVES 10

// Resets every setting to its default -- call once at boot (not from the
// title/game restart loop in main.c), so choices made in one game persist
// into the next within the same power-on session, same as high scores would
// if this game tracked one.
void options_init(void);

// Starting lives for a new game (see player_init()) -- OPTIONS_MIN_LIVES to
// OPTIONS_MAX_LIVES, set via the options scene's LIVES row.
u8 options_getStartingLives(void);

// Score interval between automatic extra lives (see score.c's addScore()).
// 0 means disabled -- set via the options scene's EXTRA LIFE row (NONE/
// 50000/100000).
u32 options_getExtraLifeInterval(void);

// Runs the options scene: lets the player adjust the settings above and try
// out music/sound effects, until they back out to the title screen. Blocks
// (same pattern as title_run()) -- entered from title.c when A+START is
// pressed instead of a plain Start.
void options_run(void);

#endif // OPTIONS_H
