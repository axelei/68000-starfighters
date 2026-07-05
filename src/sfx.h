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

#endif // SFX_H
