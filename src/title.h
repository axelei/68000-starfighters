#ifndef TITLE_H
#define TITLE_H

#include "game.h"

// Draws the splash/title screen and blocks until the player presses Start,
// then fades the screen to black (1s) and clears the title text. Call this
// once, before setting up the game scene.
void title_run(void);

// Fades the screen back in (1s) using the 3 gameplay hardware palettes
// (PAL_PLAYER/PAL_ENEMY/PAL_ENVIRONMENT -- see game.h). Call once the game
// scene (sprites, tilemaps) is fully set up but still invisible (black)
// after title_run().
void title_fadeInGame(void);

#endif // TITLE_H
