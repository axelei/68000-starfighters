#include "formation.h"
#include "enemy.h"

// Playfield bounds an entering enemy is allowed to travel through -- must
// never cross into the HUD panel (see game.h), so entrances swoop in from
// above rather than from off-screen left/right.
#define FIELD_LEFT  PLAY_AREA_X_MIN
#define FIELD_RIGHT (HUD_PANEL_X_PX - 8)

// Back row of large, tanky enemies.
#define BIG_ROW_Y     16
#define BIG_COUNT     3

// Main grid of small enemies, denser than a classic 3x5 Galaga wave.
#define GRID_ROWS       4
#define GRID_COLS       6
#define GRID_START_Y    56
#define GRID_SPACING_Y  18

#define SPAWN_STAGGER_FRAMES 5
#define ENTRANCE_SIDE_OFFSET 30 // how far off its slot an entrance starts, for a slight swoop

// Slots (row*GRID_COLS + col) that spawn a "special" (powerup-dropping)
// enemy instead of a basic one. Milestone 2 will replace this single static
// wave with a table of WaveScripts covering multiple layouts.
static bool isSpecialSlot(u16 row, u16 col)
{
    return (row == 0 && col == 1) || (row == 2 && col == 4);
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

void formation_init(void)
{
    u16 index = 0;

    // Big enemies spread evenly across the playfield width.
    u16 bigW = enemy_widthForKind(ENEMY_KIND_BIG);
    u16 bigSpacingX = (FIELD_RIGHT - FIELD_LEFT - bigW) / (BIG_COUNT - 1);
    for (u16 i = 0; i < BIG_COUNT; i++)
    {
        s16 slotX = FIELD_LEFT + i * bigSpacingX;
        spawnAtSlot(ENEMY_KIND_BIG, slotX, BIG_ROW_Y, index);
        index++;
    }

    // Small enemy grid, below the big row.
    u16 gridSpacingX = (FIELD_RIGHT - FIELD_LEFT - 16) / (GRID_COLS - 1);
    for (u16 row = 0; row < GRID_ROWS; row++)
    {
        for (u16 col = 0; col < GRID_COLS; col++)
        {
            s16 slotX = FIELD_LEFT + col * gridSpacingX;
            s16 slotY = GRID_START_Y + row * GRID_SPACING_Y;

            EnemyKind kind = isSpecialSlot(row, col) ? ENEMY_KIND_SPECIAL : ENEMY_KIND_BEE;
            spawnAtSlot(kind, slotX, slotY, index);
            index++;
        }
    }
}

void formation_update(void)
{
    // Milestone 1: entrance + hold-in-formation is handled entirely by
    // enemy_spawn()/enemies_update(). Milestone 2 adds per-wave scripted
    // "diving" attacks triggered from here (e.g. periodically pick an
    // in-formation enemy and switch it to a DIVING state/path).
}
