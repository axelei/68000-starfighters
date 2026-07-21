#include "music.h"
#include "resources.h"

// See music.h's own comment -- 100 is XGM2's full-scale default.
#define MUSIC_FM_VOLUME 75

void music_init(void)
{
    XGM2_setFMVolume(MUSIC_FM_VOLUME);
}

// Rotation order is fixed and arbitrary -- only its determinism (not which
// track happens to be first) matters, see music.h's own comment.
static const u8 *const ingameTracks[] = {
    ingame_music_0,
    ingame_music_1,
    ingame_music_2,
};
#define INGAME_TRACK_COUNT (sizeof(ingameTracks) / sizeof(ingameTracks[0]))

// Persists across waves and across games within a single power-on session
// (never reset back to 0) so consecutive waves -- and consecutive
// playthroughs -- actually rotate instead of always landing back on the
// same track.
static u16 nextIngameTrack;

void music_startIngame(void)
{
    XGM2_play(ingameTracks[nextIngameTrack]);
    nextIngameTrack = (nextIngameTrack + 1) % INGAME_TRACK_COUNT;
}

void music_startBoss(void)
{
    XGM2_play(boss_music);
}

void music_startGameOver(void)
{
    XGM2_play(gameover_music);
}
