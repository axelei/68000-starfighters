#include "formation.h"
#include "enemy.h"
#include "score.h"
#include "terrain.h"
#include "waves_generated.h"

// Playfield bounds an entering enemy is allowed to travel through -- must
// never cross into the HUD panel (see game.h), so entrances swoop in from
// above rather than from off-screen left/right.
#define FIELD_LEFT  PLAY_AREA_X_MIN
#define FIELD_RIGHT (HUD_PANEL_X_PX - 8)

#define BIG_ROW_Y      16
#define GRID_START_Y   56
#define GRID_SPACING_Y 18

#define SPAWN_STAGGER_FRAMES 5
#define ENTRANCE_SIDE_OFFSET 30 // how far off its slot an entrance starts, for a slight swoop

// Pause between a wave being fully cleared and the next one swooping in.
#define WAVE_CLEAR_DELAY 90 // 1.5s at 60fps

// How long the "WAVE N" banner stays up once a wave starts.
#define WAVE_ANNOUNCE_FRAMES 120 // 2s at 60fps

// If a wave's enemies are still around after this long, they all dive off
// at once (see enemies_forceDiveAllOut()) and the next wave starts without
// waiting for the player to clear the rest -- keeps a wave from stalling
// forever if the player just avoids the remaining enemies. Length itself
// (WAVE_TIME_LIMIT_SECONDS) lives in settings.h.
#define WAVE_TIME_LIMIT_FRAMES (WAVE_TIME_LIMIT_SECONDS * 60)

static u16 waveIndex;       // which WaveDef (mod WAVE_COUNT) is currently in play
static u16 wavesCleared;
static u16 clearDelayTimer;   // >0 while waiting to spawn the next wave
static u16 announceTimer;     // >0 while the "WAVE N" banner is showing
static bool waveSpawnPending;  // enemies for waveIndex haven't been spawned yet
static bool anyWaveSpawned;    // true once spawnWave() has run at least once this game
static u16 waveTimer;         // frames left before this wave's enemies are forced out
static bool waveForcedOut;    // true once the forced dive-out has fired for this wave

static bool isSpecialSlot(const WaveDef *wave, u16 row, u16 col)
{
    for (u16 i = 0; i < wave->specialCount; i++)
        if (wave->specialRow[i] == row && wave->specialCol[i] == col)
            return TRUE;
    return FALSE;
}

static void spawnAtSlot(EnemyKind kind, s16 slotX, s16 slotY, u16 index)
{
    u16 w = enemy_widthForKind(kind);
    u16 h = enemy_heightForKind(kind);

    bool fromRight = (index % 2) != 0;
    s16 startX = slotX + (fromRight ? ENTRANCE_SIDE_OFFSET : -ENTRANCE_SIDE_OFFSET);
    if (startX < FIELD_LEFT) startX = FIELD_LEFT;
    if (startX > FIELD_RIGHT - (s16) w) startX = FIELD_RIGHT - (s16) w;
    s16 startY = -(s16) h - 10;

    enemy_spawn(kind, startX, startY, slotX, slotY, index * SPAWN_STAGGER_FRAMES);
}

static void spawnWave(u16 index)
{
    const WaveDef *wave = &waves[index % WAVE_COUNT];
    u16 spawnIndex = 0;

    if (wave->bigCount > 0)
    {
        u16 bigW = enemy_widthForKind(ENEMY_KIND_BIG);
        u16 bigSpacingX = (wave->bigCount > 1) ? (FIELD_RIGHT - FIELD_LEFT - bigW) / (wave->bigCount - 1) : 0;

        for (u16 i = 0; i < wave->bigCount; i++)
        {
            s16 slotX = (wave->bigCount > 1)
                ? FIELD_LEFT + i * bigSpacingX
                : (FIELD_LEFT + FIELD_RIGHT - bigW) / 2;
            spawnAtSlot(ENEMY_KIND_BIG, slotX, BIG_ROW_Y, spawnIndex);
            spawnIndex++;
        }
    }

    u16 gridSpacingX = (wave->gridCols > 1) ? (FIELD_RIGHT - FIELD_LEFT - 16) / (wave->gridCols - 1) : 0;
    for (u16 row = 0; row < wave->gridRows; row++)
    {
        for (u16 col = 0; col < wave->gridCols; col++)
        {
            s16 slotX = (wave->gridCols > 1)
                ? FIELD_LEFT + col * gridSpacingX
                : (FIELD_LEFT + FIELD_RIGHT - 16) / 2;
            s16 slotY = GRID_START_Y + row * GRID_SPACING_Y;

            EnemyKind kind = isSpecialSlot(wave, row, col) ? ENEMY_KIND_SPECIAL : ENEMY_KIND_BEE;
            spawnAtSlot(kind, slotX, slotY, spawnIndex);
            spawnIndex++;
        }
    }

    anyWaveSpawned = TRUE;
    waveTimer = WAVE_TIME_LIMIT_FRAMES;
    waveForcedOut = FALSE;
}

// Only shows the "WAVE N" banner -- the actual enemies don't swoop in until
// it finishes (see formation_update()), so the announcement isn't competing
// with combat for the player's attention.
static void startWave(u16 index)
{
    waveSpawnPending = TRUE;
    score_showWaveAnnouncement(index % WAVE_COUNT + 1);
    announceTimer = WAVE_ANNOUNCE_FRAMES;

    // Vary the scrolling terrain/starfield over a long game, but only here,
    // at wave boundaries -- see terrain_requestRegen() for why this can't
    // just happen continuously during scroll.
    terrain_requestRegen();
}

void formation_init(void)
{
    waveIndex = DEBUG_START_WAVE;
    wavesCleared = 0;
    clearDelayTimer = 0;
    announceTimer = 0;
    waveSpawnPending = FALSE;
    anyWaveSpawned = FALSE;
    waveTimer = 0;
    waveForcedOut = FALSE;
    startWave(waveIndex);
}

void formation_update(void)
{
    if (announceTimer > 0)
    {
        announceTimer--;
        if (announceTimer == 0)
        {
            score_hideWaveAnnouncement();
            if (waveSpawnPending)
            {
                spawnWave(waveIndex);
                waveSpawnPending = FALSE;
            }
        }
        // Don't also check for a wave clear below while the banner is up --
        // there are deliberately no enemies on screen yet.
        return;
    }

    if (clearDelayTimer > 0)
    {
        clearDelayTimer--;
        if (clearDelayTimer == 0)
        {
            waveIndex++;
            startWave(waveIndex);
        }
        return;
    }

    if (!waveForcedOut && waveTimer > 0)
    {
        waveTimer--;
        if (waveTimer == 0)
        {
            // Ran out the clock -- send everyone left off screen for good
            // (no score/powerups) instead of waiting on the player.
            enemies_forceDiveAllOut();
            waveForcedOut = TRUE;
        }
    }

    if (enemies_countActive() == 0)
    {
        wavesCleared++;
        clearDelayTimer = WAVE_CLEAR_DELAY;
    }
}

u16 formation_wavesCleared(void)
{
    return wavesCleared;
}

bool formation_enemiesSpawned(void)
{
    return anyWaveSpawned;
}

// Seconds left before this wave's enemies get forced out -- see
// WAVE_TIME_LIMIT_FRAMES. Rounds up so it reads as a whole "60" from the
// very first frame rather than dropping to "59" immediately.
u16 formation_waveSecondsLeft(void)
{
    return (waveTimer + 59) / 60;
}
