#include "formation.h"
#include "enemy.h"
#include "score.h"
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

static u16 waveIndex;       // which WaveDef (mod WAVE_COUNT) is currently in play
static u16 wavesCleared;
static u16 clearDelayTimer;  // >0 while waiting to spawn the next wave
static u16 announceTimer;    // >0 while the "WAVE N" banner is showing

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
}

static void startWave(u16 index)
{
    spawnWave(index);
    score_showWaveAnnouncement(index % WAVE_COUNT + 1);
    announceTimer = WAVE_ANNOUNCE_FRAMES;
}

void formation_init(void)
{
    waveIndex = 0;
    wavesCleared = 0;
    clearDelayTimer = 0;
    announceTimer = 0;
    startWave(waveIndex);
}

void formation_update(void)
{
    if (announceTimer > 0)
    {
        announceTimer--;
        if (announceTimer == 0)
            score_hideWaveAnnouncement();
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
