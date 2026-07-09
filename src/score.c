#include "score.h"
#include "resources.h"
#include "player.h"
#include "formation.h"
#include "options.h"
#include <string.h>

// HUD lives in a static side panel (right edge of the screen, out of the
// playfield -- see PLAY_AREA_X_MAX in game.h) on the WINDOW plane, not the
// top -- see main.c/terrain.c for why WINDOW is used instead of the default
// (scrolling) text plane. The panel is HUD_PANEL_COLS tiles wide; its column
// count doubles as the boundary the WINDOW plane is clipped to, in 2-tile
// units (see VDP_setWindowHPos).
#define SIDE_PANEL_HPOS (HUD_PANEL_COL0 / 2) // 2-tile units, must be even
#define SCREEN_H_TILES  (SCREEN_H / 8)

#define HUD_FILL_TILE      (TILE_USER_INDEX + 32) // past terrain(4)/starfield(3) tiles
#define HUD_SEPARATOR_TILE (TILE_USER_INDEX + 33)

#define SCORE_HUD_X (HUD_PANEL_COL0 + 1)
#define SCORE_HUD_Y 2
#define LIVES_HUD_X (HUD_PANEL_COL0 + 1)
#define LIVES_HUD_Y 6
// LIVES' value area is taller than one row (see LIFE_ICON_MAX) -- WAVE/TIME
// are pushed down from the old 10/14 to make room without crowding it.
#define WAVE_HUD_X  (HUD_PANEL_COL0 + 1)
#define WAVE_HUD_Y  12
#define TIME_HUD_X  (HUD_PANEL_COL0 + 1)
#define TIME_HUD_Y  16

// LIVES shows the ship *in reserve* (player.lives - 1 -- the one currently
// in play doesn't count as a spare), and once that count is low enough to
// fit, as that many little ship icons instead of a number -- reads at a
// glance the way the arcade originals this is patterned after do. Laid out
// as a 2x2 grid starting at LIVES_HUD_X (the same column the number would
// use), not the panel's full width, so it never eats into the divider
// column to its left.
#define LIFE_ICON_MAX      4
#define LIFE_ICON_COLS     2
#define LIFE_ICON_ANIM     0 // matches player.c's ANIM_NEUTRAL (spr_player frame 0)
#define LIFE_ICON_PX       16 // spr_player's frame size, both axes
#define LIFE_ICON_TILE_ROWS 4 // 2 icon-rows, 16px/2 tile-rows tall each
#define LIFE_ICON_X_PX(col) (LIVES_HUD_X * 8 + (col) * LIFE_ICON_PX)
#define LIFE_ICON_Y_PX(row) ((LIVES_HUD_Y + 1) * 8 + (row) * LIFE_ICON_PX)

// The WINDOW plane's vertical position can only band full rows from the top
// or bottom edge -- never an arbitrary rectangle, and never a subset of rows
// with gaps in it. So every line here is packed onto fully consecutive rows
// starting at row 0, with no blank spacer rows between them: the band then
// exactly equals the rows that actually have text on them, with nothing
// above or below it hidden unnecessarily.
#define GAMEOVER_HUD_Y 0
#define GAMEOVER_BAND_ROWS 6 // rows 0..5, one per line below, no gaps

// Same top-band trick as the game-over screen, but just for the "WAVE N"
// announcement (see formation.c), which only ever shows during the pause
// between waves when no enemies are on screen.
#define WAVE_ANNOUNCE_TEXT_Y    0
#define WAVE_ANNOUNCE_BAND_ROWS 1 // just row 0 -- the single line of text

// PAUSE banner: "PAUSE" flanked by an animated dash that marches across a
// fixed-width field on either side (see pauseAnimFrames) -- PAUSE_DECOR_LEN
// is the widest frame, so VDP_drawTextFill() (see drawPauseDecorations())
// always blanks a shorter frame's leftover chars from the previous one.
// "PAUSE" itself is drawn once and never moves; only the decorations either
// side of it get redrawn as the animation advances.
#define PAUSE_DECOR_LEN 4 // widest animation frame ("   -")
#define PAUSE_TEXT_LEN 5 // "PAUSE"
#define PAUSE_TOTAL_LEN (PAUSE_DECOR_LEN * 2 + PAUSE_TEXT_LEN)
#define PAUSE_LEFT_DECOR_X  ((HUD_PANEL_COL0 - PAUSE_TOTAL_LEN) / 2)
#define PAUSE_TEXT_X        (PAUSE_LEFT_DECOR_X + PAUSE_DECOR_LEN)
#define PAUSE_RIGHT_DECOR_X (PAUSE_TEXT_X + PAUSE_TEXT_LEN)
#define PAUSE_ANIM_FRAME_COUNT 4
#define PAUSE_ANIM_FRAME_TICKS REGION_PICK(15, 13) // ~0.25s per frame

#define POINTS_BEE     100
#define POINTS_SPECIAL 300
#define POINTS_BIG     1000
#define POINTS_TURRET  200
#define POINTS_WAVER   150 // ENEMY_KIND_WAVER_A/B/C -- same for all 3, they only differ in path

// Awarded once per inter-wave formation if every waver in it was shot down
// (see formation.c's beginInterwave()/enemies_waverKillCount()).
#define INTERWAVE_PERFECT_BONUS 1000

// Awarded once, on the killing blow, for defeating a boss (see boss.c) --
// points only, no separate bonus/powerup (per the confirmed design).
#define POINTS_BOSS 5000

// Every options_getExtraLifeInterval() points (see options.c's EXTRA LIFE
// setting -- NONE/50000/100000), the player is granted an extra life (see
// player_addLife()) -- tracked via nextLifeScore rather than
// score % interval so a single kill's points can cross more than one
// interval at once (e.g. POINTS_BOSS landing right on a boundary) without
// missing a life; see addScore().

static u32 score;
static u32 nextLifeScore;
static u32 displayedScore = 0xFFFFFFFF; // force first draw
static u8 displayedLives = 0xFF;        // force first draw (see LIFE_ICON_MAX -- this is the displayed *reserve* count, not player.lives)
static u16 displayedWave = 0xFFFF;      // force first draw
static u16 displayedTime = 0xFFFF;      // force first draw

// Reused across restarts (only ever created once -- see score_init()), same
// as every other sprite pool in this game; just individually shown/hidden
// rather than repositioned.
static Sprite *lifeIcons[LIFE_ICON_MAX];

// A dash marching one position per frame across a 4-char field -- indexed
// directly (left side) or reversed (right side, see drawPauseDecorations())
// so the two sides march in opposite directions instead of both drifting
// the same way. Single-step between every consecutive pair (including the
// wrap from the last entry back to the first), unlike the previous set
// (which had a blank frame partway through, making the dash visibly jump
// two positions in one step right after it).
static const char *const pauseAnimFrames[PAUSE_ANIM_FRAME_COUNT] = {
    "-   ",
    " -  ",
    "  - ",
    "   -",
};

static bool pauseActive;
static u8 pauseAnimFrame;
static u16 pauseAnimTimer;
static void drawPauseDecorations(void); // defined below, used by score_hud_update()

// Paints a WINDOW-plane rectangle solid black using an opaque fill tile --
// blank (index 0) tiles are transparent on Genesis hardware and would let
// the scrolling terrain/starfield (and any sprite passing behind) show
// through, so plain VDP_drawText/VDP_clearTextArea calls aren't enough.
static void fillWindowRect(u16 x0, u16 y0, u16 x1, u16 y1)
{
    u16 tile = TILE_ATTR_FULL(PAL_PLAYER, TRUE, FALSE, FALSE, HUD_FILL_TILE);
    for (u16 y = y0; y < y1; y++)
        for (u16 x = x0; x < x1; x++)
            VDP_setTileMapXY(WINDOW, tile, x, y);
}

// Resets a WINDOW-plane rectangle to the opaque fill tile before drawing new
// banner text into it. Needed for two reasons: (1) VDP_drawText() only
// touches the cells its string actually spans, so without clearing first, a
// previous draw's leftover glyphs (e.g. "WAVE 9" at one X position vs
// "WAVE 10" at a shifted one, or a stale GAME OVER screen from a prior
// round) would keep showing wherever the new, shorter/differently-positioned
// text doesn't happen to overwrite; and (2) centered text never spans the
// *entire* banded row, so the untouched columns left and right of it need to
// be solid, not a raw blank (index 0) tile -- blank tiles are transparent on
// Genesis hardware and would let sprites/starfield show through the gaps
// beside the text instead of the intended solid backing.
static void clearWindowRect(u16 x0, u16 y0, u16 x1, u16 y1)
{
    fillWindowRect(x0, y0, x1, y1);
}

// Blanks the LIVES row's whole value area (every tile row the icon grid
// spans) -- starts at LIVES_HUD_X, same as the divider-column-avoiding icons
// themselves, so the divider is never touched and never needs redrawing.
// Used before switching between icon mode and number mode so neither leaves
// stray tiles behind from the other.
static void clearLivesValueArea(void)
{
    fillWindowRect(LIVES_HUD_X, LIVES_HUD_Y + 1, 40, LIVES_HUD_Y + 1 + LIFE_ICON_TILE_ROWS);
}

// Every point-awarding function funnels through here so the extra-life
// threshold (EXTRA_LIFE_SCORE_INTERVAL) is checked in exactly one place.
static void addScore(u32 points)
{
    score += points;

    u32 interval = options_getExtraLifeInterval();
    if (interval == 0)
        return; // NONE -- extra lives disabled (see options.c)

    while (score >= nextLifeScore)
    {
        nextLifeScore += interval;
        player_addLife();
    }
}

void score_init(void)
{
    score = 0;
    nextLifeScore = options_getExtraLifeInterval();
    displayedScore = 0xFFFFFFFF;
    displayedLives = 0xFF;
    displayedWave = 0xFFFF;

    // Reloaded every time (no "already loaded" guard) -- a soft reset
    // (console reset button) retains RAM/globals but the VDP's own reset
    // clears VRAM, so a one-time guard would stay tripped across the reset
    // and skip the re-upload, leaving these tiles missing on screen even
    // though the C state looks fine. Fixed tile indices make this idempotent.
    VDP_loadTileSet(&hud_fill_tileset, HUD_FILL_TILE, CPU);
    VDP_loadTileSet(&hud_separator_tileset, HUD_SEPARATOR_TILE, CPU);

    // Clip the WINDOW plane back down to the side panel (game-over may have
    // banded it to the top rows on the previous round).
    VDP_setWindowHPos(TRUE, SIDE_PANEL_HPOS);
    VDP_setWindowVPos(FALSE, 0);

    // The panel's leftmost column is a red divider line; the rest is the
    // opaque black background (text starts one column further in, at
    // HUD_PANEL_COL0 + 1, so it never overlaps the line).
    u16 separatorTile = TILE_ATTR_FULL(PAL_PLAYER, TRUE, FALSE, FALSE, HUD_SEPARATOR_TILE);
    for (u16 y = 0; y < SCREEN_H_TILES; y++)
        VDP_setTileMapXY(WINDOW, separatorTile, HUD_PANEL_COL0, y);
    fillWindowRect(HUD_PANEL_COL0 + 1, 0, 40, SCREEN_H_TILES);

    VDP_drawText("SCORE", SCORE_HUD_X, SCORE_HUD_Y);
    VDP_drawText("LIVES", LIVES_HUD_X, LIVES_HUD_Y);
    VDP_drawText("WAVE", WAVE_HUD_X, WAVE_HUD_Y);
    VDP_drawText("TIME", TIME_HUD_X, TIME_HUD_Y);

    // High priority so they render above the panel's own high-priority
    // black fill tiles (see fillWindowRect()) -- plain gameplay sprites like
    // the player ship itself are deliberately LOW priority (see player.c)
    // specifically to stay *below* that same fill, so these can't reuse that
    // convention. Created once and reused across restarts, like every other
    // sprite pool in this game; only ever shown/hidden, never repositioned.
    for (u16 i = 0; i < LIFE_ICON_MAX; i++)
    {
        u16 col = i % LIFE_ICON_COLS;
        u16 row = i / LIFE_ICON_COLS;
        if (lifeIcons[i] == NULL)
            lifeIcons[i] = SPR_addSprite(&spr_player, LIFE_ICON_X_PX(col), LIFE_ICON_Y_PX(row),
                                          TILE_ATTR(PAL_PLAYER, TRUE, FALSE, FALSE));
        SPR_setAnim(lifeIcons[i], LIFE_ICON_ANIM);
        SPR_setVisibility(lifeIcons[i], HIDDEN);
    }
}

void score_addKill(EnemyKind kind)
{
    switch (kind)
    {
        case ENEMY_KIND_SPECIAL: addScore(POINTS_SPECIAL); break;
        case ENEMY_KIND_BIG:     addScore(POINTS_BIG);     break;
        case ENEMY_KIND_WAVER_A:
        case ENEMY_KIND_WAVER_B:
        case ENEMY_KIND_WAVER_C: addScore(POINTS_WAVER);   break;
        default:                 addScore(POINTS_BEE);     break;
    }
}

void score_addTurretKill(void)
{
    addScore(POINTS_TURRET);
}

void score_addInterwavePerfectBonus(void)
{
    addScore(INTERWAVE_PERFECT_BONUS);
}

void score_addBossKill(void)
{
    addScore(POINTS_BOSS);
}

u32 score_getValue(void)
{
    return score;
}

void score_hud_update(void)
{
    if (score != displayedScore)
    {
        displayedScore = score;

        char buf[12];
        uintToStr(score, buf, 6);
        VDP_drawText(buf, SCORE_HUD_X, SCORE_HUD_Y + 1);
    }

    // The ship currently in play isn't a "spare" -- shows how many are left
    // *beyond* it, same as the arcade convention this is patterned after.
    u8 reserveLives = (player.lives > 0) ? (u8) (player.lives - 1) : 0;
    if (reserveLives != displayedLives)
    {
        displayedLives = reserveLives;
        clearLivesValueArea();

        if (reserveLives <= LIFE_ICON_MAX)
        {
            for (u16 i = 0; i < LIFE_ICON_MAX; i++)
                SPR_setVisibility(lifeIcons[i], i < reserveLives ? VISIBLE : HIDDEN);
        }
        else
        {
            for (u16 i = 0; i < LIFE_ICON_MAX; i++)
                SPR_setVisibility(lifeIcons[i], HIDDEN);

            char buf[4];
            uintToStr(reserveLives, buf, 2);
            VDP_drawText(buf, LIVES_HUD_X, LIVES_HUD_Y + 1);
        }
    }

    if (pauseActive)
    {
        if (pauseAnimTimer > 0)
        {
            pauseAnimTimer--;
        }
        else
        {
            pauseAnimFrame = (pauseAnimFrame + 1) % PAUSE_ANIM_FRAME_COUNT;
            pauseAnimTimer = PAUSE_ANIM_FRAME_TICKS;
            drawPauseDecorations();
        }
    }

    // 1-indexed: the wave currently in play, not the (0-based) cleared
    // count -- reads naturally as "WAVE 1" from the very start of a game.
    u16 waveNumber = formation_wavesCleared() + 1;
    if (waveNumber != displayedWave)
    {
        displayedWave = waveNumber;

        char buf[4];
        uintToStr(displayedWave, buf, 2);
        VDP_drawText(buf, WAVE_HUD_X, WAVE_HUD_Y + 1);
    }

    u16 secondsLeft = formation_waveSecondsLeft();
    if (secondsLeft != displayedTime)
    {
        displayedTime = secondsLeft;

        char buf[4];
        uintToStr(displayedTime, buf, 3);
        VDP_drawText(buf, TIME_HUD_X, TIME_HUD_Y + 1);
    }
}

// Hides the LIVES row's ship icons (see LIFE_ICON_MAX) without releasing
// their sprite handles -- mirrors enemies_hideAll()/turrets_hideAll(), for
// the same reason (called between rounds, see main.c): the tilemap-based
// part of the HUD gets wiped by VDP_clearTextArea() there, but these are
// independent VDP sprites, not tiles, and need their own hide call or
// they'd hang frozen on screen through the next title screen.
void score_hideLivesIcons(void)
{
    for (u16 i = 0; i < LIFE_ICON_MAX; i++)
        SPR_setVisibility(lifeIcons[i], HIDDEN);
}

void score_showWaveAnnouncement(u16 waveNumber)
{
    // Bands the WINDOW's top rows across the full width (same OR-with-HPos
    // trick as score_showGameOver()). No background rectangle needed here --
    // the font itself (see main.c's VDP_loadFont() call) carries an opaque
    // black background in every glyph tile now.
    VDP_setWindowVPos(FALSE, WAVE_ANNOUNCE_BAND_ROWS);
    clearWindowRect(0, 0, HUD_PANEL_COL0, WAVE_ANNOUNCE_BAND_ROWS);

    char text[16] = "WAVE ";
    char num[4];
    uintToStr(waveNumber, num, 1);
    strcat(text, num);

    // Centered within the play area (cols 0..HUD_PANEL_COL0), not the full
    // 40-column screen -- the side panel reads as a separate strip.
    u16 x = (HUD_PANEL_COL0 - strlen(text)) / 2;
    VDP_drawText(text, x, WAVE_ANNOUNCE_TEXT_Y);
}

void score_hideWaveAnnouncement(void)
{
    VDP_setWindowVPos(FALSE, 0);
}

static void drawPauseDecorations(void)
{
    // Right side reads the same frame table back-to-front -- same speed,
    // opposite marching direction, rather than both sides drifting the
    // same way.
    u8 rightFrame = PAUSE_ANIM_FRAME_COUNT - 1 - pauseAnimFrame;
    VDP_drawTextFill(pauseAnimFrames[pauseAnimFrame], PAUSE_LEFT_DECOR_X, WAVE_ANNOUNCE_TEXT_Y, PAUSE_DECOR_LEN);
    VDP_drawTextFill(pauseAnimFrames[rightFrame], PAUSE_RIGHT_DECOR_X, WAVE_ANNOUNCE_TEXT_Y, PAUSE_DECOR_LEN);
}

void score_showPause(void)
{
    // Same single-row top band as the wave announcement.
    VDP_setWindowVPos(FALSE, WAVE_ANNOUNCE_BAND_ROWS);
    clearWindowRect(0, 0, HUD_PANEL_COL0, WAVE_ANNOUNCE_BAND_ROWS);

    VDP_drawText("PAUSE", PAUSE_TEXT_X, WAVE_ANNOUNCE_TEXT_Y);

    pauseActive = TRUE;
    pauseAnimFrame = 0;
    pauseAnimTimer = PAUSE_ANIM_FRAME_TICKS;
    drawPauseDecorations();
}

void score_hidePause(void)
{
    pauseActive = FALSE;
    VDP_setWindowVPos(FALSE, 0);
}

void score_showGameOver(void)
{
    char buf[12];

    // Band the WINDOW plane's top rows (in addition to -- not instead of --
    // the side panel's HPos, still active): the VDP ORs the window's H and V
    // conditions, so this shows the window across the *full width* for just
    // these top rows (game-over text) while leaving the rest of the screen
    // (terrain, enemies, etc.) still visible underneath, rather than
    // covering the entire screen the way expanding HPos to full width would.
    // No background rectangle needed here -- the font itself (see main.c's
    // VDP_loadFont() call) carries an opaque black background in every
    // glyph tile now.
    VDP_setWindowVPos(FALSE, GAMEOVER_BAND_ROWS);
    clearWindowRect(0, 0, HUD_PANEL_COL0, GAMEOVER_BAND_ROWS);

    VDP_drawText("GAME OVER", 13, GAMEOVER_HUD_Y);
    VDP_drawText("FINAL SCORE", 12, GAMEOVER_HUD_Y + 1);
    uintToStr(score, buf, 6);
    VDP_drawText(buf, 14, GAMEOVER_HUD_Y + 2);
    VDP_drawText("WAVES CLEARED", 11, GAMEOVER_HUD_Y + 3);
    uintToStr(formation_wavesCleared(), buf, 2);
    VDP_drawText(buf, 17, GAMEOVER_HUD_Y + 4);
    VDP_drawText("PRESS START", 12, GAMEOVER_HUD_Y + 5);
}
