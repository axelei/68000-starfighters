#include "formation.h"
#include "enemy.h"

#define FORMATION_ROWS 3
#define FORMATION_COLS 5
#define SLOT_SPACING_X 50
#define SLOT_MARGIN_X  35
#define SLOT_START_Y   24
#define SLOT_SPACING_Y 20
#define SPAWN_STAGGER_FRAMES 6

// Slots (row*FORMATION_COLS + col) that spawn a "special" (powerup-dropping)
// enemy instead of a basic one. Milestone 2 will replace this single static
// wave with a table of WaveScripts covering multiple layouts.
static bool isSpecialSlot(u16 row, u16 col)
{
    return (row == 0 && col == 2) || (row == 2 && col == 1);
}

void formation_init(void)
{
    for (u16 row = 0; row < FORMATION_ROWS; row++)
    {
        for (u16 col = 0; col < FORMATION_COLS; col++)
        {
            s16 slotX = SLOT_MARGIN_X + col * SLOT_SPACING_X;
            s16 slotY = SLOT_START_Y + row * SLOT_SPACING_Y;

            u16 index = row * FORMATION_COLS + col;
            bool fromRight = (index % 2) != 0;
            s16 startX = fromRight ? (SCREEN_W + 20) : -20;
            s16 startY = -20;

            EnemyKind kind = isSpecialSlot(row, col) ? ENEMY_KIND_SPECIAL : ENEMY_KIND_BEE;
            s16 delay = index * SPAWN_STAGGER_FRAMES;

            enemy_spawn(kind, startX, startY, slotX, slotY, delay);
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
