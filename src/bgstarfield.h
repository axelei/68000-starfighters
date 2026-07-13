#ifndef BGSTARFIELD_H
#define BGSTARFIELD_H

#include "game.h"

// Draws both (looping) starfield bands onto BG_B -- see
// terrain_initStarfieldOnly() -- and kicks off an async fade-in of
// PAL_ENVIRONMENT from black over fadeFrames, so the starfield's tiles
// don't just show whatever CRAM already held the instant they're drawn.
// Shared by title.c and intro.c, both of which draw this behind their own
// foreground content before terrain_init() itself has ever run this
// session. Call once before the screen becomes visible.
void bgstarfield_start(u16 fadeFrames);

// Advances the starfield's scroll by one frame -- call every frame the
// screen using it is up, so it keeps moving continuously instead of
// freezing whenever the caller isn't otherwise busy.
void bgstarfield_update(void);

#endif // BGSTARFIELD_H
