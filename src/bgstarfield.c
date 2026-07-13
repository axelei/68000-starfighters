#include "bgstarfield.h"
#include "terrain.h"
#include "resources.h"

// Matches terrain.c's own STARFIELD_SPEED -- kept as a separate constant
// (rather than exposing terrain.c's) since this module's scroll is its own
// independent state, not gameplay's.
#define BGSTARFIELD_SPEED REGION_PICK(FIX16(0.4), FIX16(0.48))

static fix16 bgStarfieldScroll;

void bgstarfield_start(u16 fadeFrames)
{
    terrain_initStarfieldOnly();
    bgStarfieldScroll = 0;
    VDP_setVerticalScroll(BG_B, 0);

    // PAL_ENVIRONMENT isn't loaded onto hardware at all outside of gameplay
    // (see game.h's PAL_ENVIRONMENT / title_fadeInGame(), the only other
    // writer) -- without this, the starfield's tiles would show as whatever
    // CRAM happened to already hold instead of a deliberate fade. Async:
    // runs in the background across ordinary SYS_doVBlankProcess() calls,
    // same as XGM2_fadeOutAndStop() elsewhere, rather than blocking the
    // caller.
    u16 black[16] = {0};
    PAL_fadePalette(PAL_ENVIRONMENT, black, palette_environment.data, fadeFrames, TRUE);
}

void bgstarfield_update(void)
{
    bgStarfieldScroll += BGSTARFIELD_SPEED;
    VDP_setVerticalScroll(BG_B, -F16_toInt(bgStarfieldScroll));
}
