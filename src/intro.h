#ifndef INTRO_H
#define INTRO_H

#include "game.h"

// Star Wars-style scrolling crawl intro, shown once before the very first
// title screen after boot (see main.c) -- never replayed on later returns
// to the title screen. Skippable at any point by any button press. Ends by
// fading to black, same handoff shape as title_run()'s own fade-out, so
// title_run() can set up from scratch afterward without knowing this ran.
void intro_run(void);

#endif // INTRO_H
