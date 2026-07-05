#include "sfx.h"

// Placeholder sound effects: no compiled audio resources, no XGM driver --
// just short hand-authored step sequences written directly to the PSG's
// square-wave channels (0-2) and noise channel (3) via PSG_setTone /
// PSG_setEnvelope. This is the simplest approach that actually exists in
// SGDK for one-shot SFX (see inc/psg.h).
//
// NOTE: PSG_ENVELOPE_MAX == 0 (loudest) and PSG_ENVELOPE_MIN == 15
// (silent) -- inverted from what the names suggest. Verify against the
// installed SGDK's inc/psg.h if effects come out silent or at full volume
// unexpectedly.

#define SFX_CHANNEL_SHOOT    0
#define SFX_CHANNEL_POWERUP  1
#define SFX_CHANNEL_NOISE    3

typedef struct
{
    u16 tone;      // PSG_setTone value (lower = higher pitch)
    u8 envelope;   // PSG_ENVELOPE_MAX(0)..PSG_ENVELOPE_MIN(15)
    u8 duration;   // frames to hold this step
} SfxStep;

typedef struct
{
    const SfxStep *steps;
    u8 stepCount;
    u8 stepIndex;
    u8 frameCounter;
    bool playing;
    u8 channel;
    bool isNoise;
} SfxChannelState;

static const SfxStep shootSteps[] = {
    {80,  2, 2},
    {140, 5, 2},
    {220, 10, 2},
};

static const SfxStep powerupSteps[] = {
    {600, 3, 4},
    {450, 2, 4},
    {300, 1, 4},
    {200, 0, 6},
};

// Noise-channel "explosion": frequency divider doesn't apply to noise the
// same way; PSG_setNoise(type, freq) is set once in sfx_play_explosion(),
// this table only sweeps the envelope down to fake a decay.
static const SfxStep explosionSteps[] = {
    {0, 0, 3},
    {0, 3, 3},
    {0, 6, 3},
    {0, 9, 4},
    {0, 12, 4},
    {0, 15, 1},
};

static SfxChannelState shootState  = {shootSteps, 3, 0, 0, FALSE, SFX_CHANNEL_SHOOT, FALSE};
static SfxChannelState powerupState = {powerupSteps, 4, 0, 0, FALSE, SFX_CHANNEL_POWERUP, FALSE};
static SfxChannelState explosionState = {explosionSteps, 6, 0, 0, FALSE, SFX_CHANNEL_NOISE, TRUE};

void sfx_init(void)
{
    PSG_reset();
}

static void startChannel(SfxChannelState *cs)
{
    cs->stepIndex = 0;
    cs->frameCounter = 0;
    cs->playing = TRUE;
}

void sfx_play_shoot(void)
{
    startChannel(&shootState);
}

void sfx_play_powerup(void)
{
    startChannel(&powerupState);
}

void sfx_play_explosion(void)
{
    PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK4);
    startChannel(&explosionState);
}

static void updateChannel(SfxChannelState *cs)
{
    if (!cs->playing)
        return;

    if (cs->frameCounter > 0)
    {
        cs->frameCounter--;
        return;
    }

    if (cs->stepIndex >= cs->stepCount)
    {
        PSG_setEnvelope(cs->channel, PSG_ENVELOPE_MIN);
        cs->playing = FALSE;
        return;
    }

    const SfxStep *step = &cs->steps[cs->stepIndex];
    if (!cs->isNoise)
        PSG_setTone(cs->channel, step->tone);
    PSG_setEnvelope(cs->channel, step->envelope);

    cs->frameCounter = step->duration;
    cs->stepIndex++;
}

void sfx_update(void)
{
    updateChannel(&shootState);
    updateChannel(&powerupState);
    updateChannel(&explosionState);
}
