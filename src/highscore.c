#include "highscore.h"

// Layout within the battery-backed SRAM window declared in
// src/boot/rom_head.c -- only a handful of bytes of the whole 64KB window
// are actually used.
#define SRAM_OFF_MAGIC 0 // u16
#define SRAM_OFF_SCORE 2 // u32
#define SRAM_OFF_WAVE  6 // u16

// Arbitrary marker distinguishing "genuinely saved by this game" from
// whatever a chip that's never been written happens to power on holding --
// some emulators zero-fill SRAM, real hardware with no battery (or a dead
// one) can read back all 1s or open-bus garbage. Bumping this value would
// (harmlessly) make every existing save look uninitialized and reset once.
#define SRAM_MAGIC 0x5343 // 'S','C'

static u32 cachedScore;
static u16 cachedWave;

static void writeDefaults(void)
{
    SRAM_enable();
    SRAM_writeWord(SRAM_OFF_MAGIC, SRAM_MAGIC);
    SRAM_writeLong(SRAM_OFF_SCORE, 0);
    SRAM_writeWord(SRAM_OFF_WAVE, 0);
    SRAM_disable();

    cachedScore = 0;
    cachedWave = 0;
}

void highscore_init(void)
{
    SRAM_enableRO();
    u16 magic = SRAM_readWord(SRAM_OFF_MAGIC);
    SRAM_disable();

    if (magic != SRAM_MAGIC)
    {
        // Never written by this game before -- start fresh rather than
        // trusting whatever's actually there.
        writeDefaults();
        return;
    }

    SRAM_enableRO();
    cachedScore = SRAM_readLong(SRAM_OFF_SCORE);
    cachedWave = SRAM_readWord(SRAM_OFF_WAVE);
    SRAM_disable();
}

u32 highscore_getScore(void)
{
    return cachedScore;
}

u16 highscore_getWave(void)
{
    return cachedWave;
}

HighScoreResult highscore_checkAndUpdate(u32 finalScore, u16 waveReached)
{
    HighScoreResult result = {FALSE, FALSE};

    if (finalScore > cachedScore)
    {
        cachedScore = finalScore;
        result.newHighScore = TRUE;
    }

    if (waveReached > cachedWave)
    {
        cachedWave = waveReached;
        result.newHighWave = TRUE;
    }

    // Only touches SRAM when something actually changed -- SRAM writes go
    // through the cartridge's write-protect latch (see sram.h), no need to
    // toggle it every game over if neither record was beaten.
    if (result.newHighScore || result.newHighWave)
    {
        SRAM_enable();
        if (result.newHighScore) SRAM_writeLong(SRAM_OFF_SCORE, cachedScore);
        if (result.newHighWave) SRAM_writeWord(SRAM_OFF_WAVE, cachedWave);
        SRAM_disable();
    }

    return result;
}

void highscore_reset(void)
{
    writeDefaults();
}
