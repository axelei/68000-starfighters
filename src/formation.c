#include "formation.h"
#include "enemy.h"
#include "score.h"
#include "terrain.h"
#include "player.h"
#include "boss.h"
#include "music.h"
#include "waves_generated.h"

// Every 5th wave slot is a boss encounter instead of the normal
// interwave+wave pair -- see beginEncounter().
#define BOSS_WAVE_INTERVAL 3

// Playfield bounds an entering enemy is allowed to travel through -- must
// never cross into the HUD panel (see game.h), so entrances swoop in from
// above rather than from off-screen left/right.
#define FIELD_LEFT  PLAY_AREA_X_MIN
#define FIELD_RIGHT (HUD_PANEL_X_PX - 8)

#define BIG_ROW_Y      16
#define GRID_START_Y   56
#define GRID_SPACING_Y 18

// PAL values throughout this block are NTSC * 50/60 -- see game.h's
// REGION_PICK -- so real-world pacing stays the same regardless of the
// console's actual refresh rate.
#define SPAWN_STAGGER_FRAMES REGION_PICK(5, 4)
#define ENTRANCE_SIDE_OFFSET 30 // how far off its slot an entrance starts, for a slight swoop

// Pause between a wave being fully cleared and the next one swooping in.
#define WAVE_CLEAR_DELAY REGION_PICK(90, 75) // 1.5s

// How long the "WAVE N" banner stays up once a wave starts.
#define WAVE_ANNOUNCE_FRAMES REGION_PICK(120, 100) // 2s

// If a wave's enemies are still around after this long, they all dive off
// at once (see enemies_forceDiveAllOut()) and the next wave starts without
// waiting for the player to clear the rest -- keeps a wave from stalling
// forever if the player just avoids the remaining enemies. Length itself
// (WAVE_TIME_LIMIT_SECONDS) lives in settings.h.
#define WAVE_TIME_LIMIT_FRAMES REGION_PICK(WAVE_TIME_LIMIT_SECONDS * 60, WAVE_TIME_LIMIT_SECONDS * 50)

static u16 waveIndex;       // which WaveDef (mod WAVE_COUNT) is currently in play
static u16 clearDelayTimer;   // >0 while waiting to spawn the next wave
static u16 announceTimer;     // >0 while the "WAVE N" banner is showing
static bool waveSpawnPending;  // enemies for waveIndex haven't been spawned yet
static bool anyWaveSpawned;    // true once spawnWave() has run at least once this game
static u16 waveTimer;         // frames left before this wave's enemies are forced out
static bool waveForcedOut;    // true once the forced dive-out has fired for this wave
static bool inInterwave;      // TRUE while the pre-wave "waver" formation is on screen
static bool inBossFight;      // TRUE while a boss encounter (see boss.c) is running

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

// Every wave (including the first) is preceded by a short formation of
// fragile "waver" enemies flying through -- see enemy_spawnWaverFormation().
// No banner for it: the staggered entrance already reads as "something's
// starting." Once every waver is gone (killed or flown off screen),
// formation_update() detects that the same way it detects a cleared combat
// wave (enemies_countActive() == 0) and proceeds to startWave().
static void beginInterwave(void)
{
    inInterwave = TRUE;
    enemy_spawnWaverFormation(ENEMY_KIND_WAVER_A + (waveIndex % WAVER_KIND_COUNT));

    // Rotates the ingame track once per wave (see music.h) -- this is the
    // one place every non-boss wave slot passes through, including the
    // very first (called from formation_init() via beginEncounter() below).
    music_startIngame();
}

// Every BOSS_WAVE_INTERVAL-th wave slot (3, 6, 9...) spawns a boss
// encounter instead of the normal interwave+wave pair -- same modulo-on-
// waveIndex dispatch pattern beginInterwave() already uses to pick a waver
// kind, just choosing between two whole phases instead of a sprite/path.
static void beginEncounter(void)
{
    if ((waveIndex % BOSS_WAVE_INTERVAL) == BOSS_WAVE_INTERVAL - 1)
    {
        inBossFight = TRUE;
        // waveIndex is 2, 5, 8, 11, 14... on boss waves, so /BOSS_WAVE_INTERVAL
        // gives 0, 1, 2, 3, 4, 5... and %BOSS_KIND_COUNT cycles the whole
        // 5-boss roster (see boss.c's BossDef table) once every 15 waves.
        BossKind kind = (BossKind) ((waveIndex / BOSS_WAVE_INTERVAL) % BOSS_KIND_COUNT);
        boss_begin(kind);
    }
    else
    {
        beginInterwave();
    }
}

// Only shows the "WAVE N" banner -- the actual enemies don't swoop in until
// it finishes (see formation_update()), so the announcement isn't competing
// with combat for the player's attention.
static void startWave(u16 index)
{
    // Vary the scrolling terrain/starfield over a long game, but only here,
    // at wave boundaries -- see terrain_requestRegen() for why this can't
    // just happen continuously during scroll.
    terrain_requestRegen();

    // formation_update() keeps running behind the "GAME OVER" screen (see
    // main.c) so the scene stays animated, but no banner should pop up over
    // it once the player's already out of lives -- so skip the announcement
    // and go straight to spawning instead of gating on announceTimer.
    if (player_isGameOver())
    {
        spawnWave(index);
        return;
    }

    waveSpawnPending = TRUE;
    // index+1, not index % WAVE_COUNT+1 -- the latter only cycles through
    // the WAVE_COUNT (10) distinct WaveDefs the layouts repeat from,
    // wrapping the banner back to "WAVE 1" every 10 slots even though the
    // game keeps progressing. index itself is the wave we're actually in
    // (not a count of how many are done), so it stays correct even when
    // starting mid-game via DEBUG_START_WAVE (settings.h) -- wavesCleared
    // would wrongly read 0 there.
    score_showWaveAnnouncement(index + 1);
    announceTimer = WAVE_ANNOUNCE_FRAMES;
}

void formation_init(void)
{
    waveIndex = DEBUG_START_WAVE;
    clearDelayTimer = 0;
    announceTimer = 0;
    waveSpawnPending = FALSE;
    anyWaveSpawned = FALSE;
    waveTimer = 0;
    waveForcedOut = FALSE;
    inInterwave = FALSE;
    inBossFight = FALSE;
    beginEncounter();
}

void formation_update(void)
{
    // Once the player is out of lives, wave progression freezes completely
    // -- no timer here keeps ticking, and no new wave (regular or boss)
    // ever spawns; only the enemies/bullets/etc. already on screen keep
    // animating out on their own (see main.c, which still calls
    // enemies_update()/bullets_update()/etc. unconditionally). The one
    // exception is tidying up a "WAVE N" banner that was already up when
    // game over hit, so it doesn't keep sitting there over the "GAME OVER"
    // text -- the wave it was about to announce is simply dropped instead
    // of spawning.
    if (player_isGameOver())
    {
        if (announceTimer > 0)
        {
            announceTimer = 0;
            score_hideWaveAnnouncement();
            waveSpawnPending = FALSE;
        }
        return;
    }

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
            beginEncounter();
        }
        return;
    }

    // No !player_isGameOver() check needed here -- the whole function
    // returns early once that's true (see the top of formation_update()).
    if (!inInterwave && !inBossFight && !waveForcedOut && waveTimer > 0)
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

    if (inBossFight)
    {
        if (!boss_isActive())
        {
            // Boss encounter resolved (killed or timed out) -- counts as
            // this wave slot being done, same progression bump a regular
            // cleared wave gets, then move on to the next slot's
            // interwave+wave (or another boss, if BOSS_WAVE_INTERVAL ever
            // shrinks to 1 -- beginEncounter() re-checks the modulo either
            // way).
            inBossFight = FALSE;
            waveIndex++;
            beginEncounter();
        }
    }
    else if (inInterwave)
    {
        // Not enemies_countActive() == 0 -- that also reads TRUE during the
        // brief pause between two inter-wave batches (see
        // enemy.c's updateWaverFormationSequencing()), which would trigger
        // this prematurely.
        if (enemies_waverFormationDone())
        {
            inInterwave = FALSE;
            // Every waver was actually shot down (none flew off screen
            // uncontested) -- see enemy_kill()'s waverKillCount tracking.
            if (enemies_waverKillCount() == enemies_waverTotalCount())
                score_addInterwavePerfectBonus();
            startWave(waveIndex);
        }
    }
    else if (enemies_countActive() == 0)
    {
        clearDelayTimer = WAVE_CLEAR_DELAY;
    }
}

// How many waves the player has fully cleared so far -- since waveIndex only
// advances once a wave slot is done (see formation_update() above), it *is*
// that count in normal play (both start at 0). Only diverges from a true
// "cleared" count under DEBUG_START_WAVE (settings.h), which skips ahead
// without having actually cleared anything -- not a real gameplay case.
u16 formation_wavesCleared(void)
{
    return waveIndex;
}

u16 formation_currentWave(void)
{
    return waveIndex + 1;
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
