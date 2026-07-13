#include "score.h"
#include "resources.h"
#include "player.h"
#include "formation.h"
#include "options.h"
#include "highscore.h"
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

// Persisted records (see highscore.c) -- shown lower in the same panel,
// below TIME with the same one-row-label/one-row-value shape as SCORE.
// "HISCORE" (7 chars) is the widest label that still fits the panel's 7
// usable columns (HUD_PANEL_COLS(8) minus the divider column) without
// running into the screen edge -- see HUD_PANEL_COL0.
#define HISCORE_HUD_X (HUD_PANEL_COL0 + 1)
#define HISCORE_HUD_Y 20
#define HIWAVE_HUD_X  (HUD_PANEL_COL0 + 1)
#define HIWAVE_HUD_Y  23

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
#define GAMEOVER_BAND_ROWS 7 // rows 0..6 -- "GAME OVER" itself is the sprite-letter animation below, not drawn here

// Record-beaten rows -- always reserved (blank if not applicable) rather
// than conditionally resizing the band, so PRESS START always lands on the
// same row regardless of whether either record fell this round. Blinked by
// recordBlinkStep(), driven from score_hud_update() same as the rest of the
// game-over screen.
#define GAMEOVER_RECORD_SCORE_ROW (GAMEOVER_HUD_Y + 4)
#define GAMEOVER_RECORD_WAVE_ROW  (GAMEOVER_HUD_Y + 5)
#define GAMEOVER_PRESS_START_ROW  (GAMEOVER_HUD_Y + 6)
#define GAMEOVER_RECORD_BLINK_FRAMES REGION_PICK(8, 7) // quick blink, ~0.13s per phase

// "GAME OVER" assembles centered in the playfield (not the whole screen --
// PLAY_AREA_X/Y_MIN/MAX exclude the HUD side panel, see game.h) from 8
// individual letter sprites (spr_gameover_letters -- see resources.res)
// instead of being drawn on the WINDOW plane like the rest of the
// game-over text: each spirals in from a point evenly spaced around a
// circle (GAMEOVER_ORBIT_RADIUS_START out, converging on the circle's
// center, which sits at the playfield's center), then peels back off from
// that shared center out to its own final position in the spelled-out
// word, landing exactly on it -- see gameoverAnimStep().
#define GAMEOVER_LETTER_COUNT 8 // G,A,M,E,O,V,E,R -- "GAME OVER" without the space
#define GAMEOVER_LETTER_W     16
#define GAMEOVER_WORD_LEN     4  // "GAME" / "OVER" -- both 4 letters
#define GAMEOVER_WORD_GAP     8  // extra px between the two words

#define GAMEOVER_PLAYFIELD_CENTER_X ((PLAY_AREA_X_MIN + PLAY_AREA_X_MAX) / 2)
#define GAMEOVER_PLAYFIELD_CENTER_Y ((PLAY_AREA_Y_MIN + PLAY_AREA_Y_MAX) / 2)
// Sprite position is its top-left corner -- offset by half a letter so the
// letter's actual visual center (not its corner) lands on the playfield's
// center point.
#define GAMEOVER_SPIRAL_CENTER_X (GAMEOVER_PLAYFIELD_CENTER_X - GAMEOVER_LETTER_W / 2)
#define GAMEOVER_LETTERS_Y       (GAMEOVER_PLAYFIELD_CENTER_Y - GAMEOVER_LETTER_W / 2)

// Index into spr_gameover_letters' 7 unique glyph frames (G,A,M,E,O,V,R, in
// that order -- see generate_placeholders.py's gameover_letters()) for each
// of the 8 displayed letters.
static const u8 gameoverLetterFrame[GAMEOVER_LETTER_COUNT] = {0, 1, 2, 3, 4, 5, 3, 6};

#define GAMEOVER_SPIRAL_FRAMES REGION_PICK(110, 92) // ~1.8s spiraling into the shared center
#define GAMEOVER_SETTLE_FRAMES REGION_PICK(35, 29)  // ~0.6s peeling out to each letter's own slot
#define GAMEOVER_ANIM_FRAMES   (GAMEOVER_SPIRAL_FRAMES + GAMEOVER_SETTLE_FRAMES)
#define GAMEOVER_ORBIT_RADIUS_START      90
#define GAMEOVER_ORBIT_DEGREES_PER_FRAME 6 // full loop every 60 frames -- slower sweep than before

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

typedef struct
{
    Sprite *sprite;
    s16 finalX, finalY;   // its resting position once "GAME OVER" is spelled out
    u16 startAngleDeg;    // where it starts on the spiral-in circle
} GameOverLetter;

// Reused across restarts (only ever created once -- see score_init()), same
// as lifeIcons.
static GameOverLetter gameoverLetters[GAMEOVER_LETTER_COUNT];
static bool gameoverAnimActive;
static u16 gameoverAnimTimer; // counts up from 0 to GAMEOVER_ANIM_FRAMES while active
static void gameoverAnimStep(void); // defined below, used by score_hud_update()

// Set once at score_showGameOver() time (see highscore_checkAndUpdate()
// there) and left alone for the rest of the game-over screen -- recordBlinkOn
// is the only part that keeps changing, toggled every GAMEOVER_RECORD_BLINK_
// FRAMES by recordBlinkStep() to blink whichever line(s) apply.
static bool recordScoreActive;
static bool recordWaveActive;
static bool recordBlinkOn;
static u16 recordBlinkTimer;
static void recordBlinkStep(void); // defined below, used by score_hud_update()

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

// Redraws both persisted-record values -- called once at score_init() (to
// show whatever was already saved coming into this round) and again right
// after a new record is set at game over (see score_showGameOver()), so it
// reflects the just-beaten record immediately rather than only on the next
// restart.
static void drawHighScoreHudValues(void)
{
    char buf[8];
    uintToStr(highscore_getScore(), buf, 6);
    VDP_drawText(buf, HISCORE_HUD_X, HISCORE_HUD_Y + 1);
    uintToStr(highscore_getWave(), buf, 2);
    VDP_drawText(buf, HIWAVE_HUD_X, HIWAVE_HUD_Y + 1);
}

void score_resetHandles(void)
{
    for (u16 i = 0; i < LIFE_ICON_MAX; i++)
        lifeIcons[i] = NULL;
    for (u16 i = 0; i < GAMEOVER_LETTER_COUNT; i++)
        gameoverLetters[i].sprite = NULL;
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
    VDP_drawText("HISCORE", HISCORE_HUD_X, HISCORE_HUD_Y);
    VDP_drawText("HIWAVE", HIWAVE_HUD_X, HIWAVE_HUD_Y);
    drawHighScoreHudValues();

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

    // Regular (LOW) priority, like every other gameplay sprite -- these
    // fly freely over the scene, not over the WINDOW plane's HUD text, so
    // there's no need for the lifeIcons' HIGH-priority workaround. Created
    // once and reused across restarts; positions/angles are fixed layout,
    // computed once here rather than every time the animation starts.
    gameoverAnimActive = FALSE;
    recordScoreActive = FALSE;
    recordWaveActive = FALSE;
    s16 totalWidth = GAMEOVER_LETTER_COUNT * GAMEOVER_LETTER_W + GAMEOVER_WORD_GAP;
    s16 startX = GAMEOVER_PLAYFIELD_CENTER_X - totalWidth / 2;
    for (u16 i = 0; i < GAMEOVER_LETTER_COUNT; i++)
    {
        GameOverLetter *L = &gameoverLetters[i];
        L->finalX = startX + i * GAMEOVER_LETTER_W + (i >= GAMEOVER_WORD_LEN ? GAMEOVER_WORD_GAP : 0);
        L->finalY = GAMEOVER_LETTERS_Y;
        L->startAngleDeg = i * (360 / GAMEOVER_LETTER_COUNT);

        if (L->sprite == NULL)
        {
            L->sprite = SPR_addSprite(&spr_gameover_letters, L->finalX, L->finalY,
                                       TILE_ATTR(PAL_PLAYER, FALSE, FALSE, FALSE));
            // Must win the draw order against every other sprite, including
            // ones that show up *after* it (e.g. boss weak-spot pods use
            // SPR_FLAG_INSERT_HEAD to draw in front of the boss body -- see
            // boss.c -- which would otherwise also put them in front of
            // this, since these letters are only ever created once, here,
            // long before any boss encounter). SPR_setAlwaysOnTop() pins
            // this sprite's depth below SPR_MIN_DEPTH permanently, which
            // beats list-order/INSERT_HEAD entirely rather than just
            // matching it.
            SPR_setAlwaysOnTop(L->sprite);
        }
        SPR_setFrame(L->sprite, gameoverLetterFrame[i]);
        SPR_setVisibility(L->sprite, HIDDEN);
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

    // The wave currently in play, not a count of cleared ones --
    // wavesCleared()+1 would wrongly read "WAVE 1" when starting mid-game
    // via DEBUG_START_WAVE (settings.h).
    u16 waveNumber = formation_currentWave();
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

    gameoverAnimStep();
    recordBlinkStep();
}

// Hides the LIVES row's ship icons (see LIFE_ICON_MAX) and the "GAME OVER"
// letter sprites, without releasing their handles -- mirrors
// enemies_hideAll()/turrets_hideAll(), for the same reason (called between
// rounds, see main.c): the tilemap-based part of the HUD gets wiped by
// VDP_clearTextArea() there, but these are independent VDP sprites, not
// tiles, and need their own hide call or they'd hang frozen on screen
// through the next title screen.
void score_hideLivesIcons(void)
{
    for (u16 i = 0; i < LIFE_ICON_MAX; i++)
        SPR_setVisibility(lifeIcons[i], HIDDEN);

    gameoverAnimActive = FALSE;
    for (u16 i = 0; i < GAMEOVER_LETTER_COUNT; i++)
        SPR_setVisibility(gameoverLetters[i].sprite, HIDDEN);

    // Stop the record blink loop too -- the WINDOW plane's game-over band
    // is about to be wiped by VDP_clearTextArea() in main.c either way, but
    // this keeps recordBlinkStep() from redrawing into it in the meantime.
    recordScoreActive = FALSE;
    recordWaveActive = FALSE;
}

// Spirals every "GAME OVER" letter in from its own point on a circle
// (GAMEOVER_ORBIT_RADIUS_START out, GAMEOVER_SPIRAL_FRAMES long) down to
// the circle's shared center, then peels each one back out
// (GAMEOVER_SETTLE_FRAMES long) from that center to its own final position
// in the spelled-out word -- landing exactly on it, which a pure spiral
// alone can't guarantee since every letter's radius converges on the same
// point regardless of where its own slot actually is. A no-op once the
// animation isn't active (see score_showGameOver()/score_hideLivesIcons()).
static void gameoverAnimStep(void)
{
    if (!gameoverAnimActive)
        return;

    s16 centerX = GAMEOVER_SPIRAL_CENTER_X;
    s16 centerY = GAMEOVER_LETTERS_Y;

    for (u16 i = 0; i < GAMEOVER_LETTER_COUNT; i++)
    {
        GameOverLetter *L = &gameoverLetters[i];
        s16 x, y;

        if (gameoverAnimTimer < GAMEOVER_SPIRAL_FRAMES)
        {
            // Eased (quadratic), not a flat linear shrink -- barely closing
            // in at first, then progressively faster as it nears the
            // center, which reads as a much more deliberate "being pulled
            // in" than a constant-rate shrink.
            fix16 remainFrac = F16_div(FIX16(GAMEOVER_SPIRAL_FRAMES - gameoverAnimTimer), FIX16(GAMEOVER_SPIRAL_FRAMES));
            fix16 eased = F16_mul(remainFrac, remainFrac);
            fix16 radius = F16_mul(FIX16(GAMEOVER_ORBIT_RADIUS_START), eased);
            u16 angleDeg = (L->startAngleDeg + gameoverAnimTimer * GAMEOVER_ORBIT_DEGREES_PER_FRAME) % 360;
            x = centerX + F16_toInt(F16_mul(F16_cos(FIX16(angleDeg)), radius));
            y = centerY + F16_toInt(F16_mul(F16_sin(FIX16(angleDeg)), radius));
        }
        else
        {
            u16 t = gameoverAnimTimer - GAMEOVER_SPIRAL_FRAMES;
            x = centerX + (s16) (((s32) (L->finalX - centerX) * t) / GAMEOVER_SETTLE_FRAMES);
            y = centerY + (s16) (((s32) (L->finalY - centerY) * t) / GAMEOVER_SETTLE_FRAMES);
        }

        SPR_setPosition(L->sprite, x, y);
    }

    gameoverAnimTimer++;
    if (gameoverAnimTimer >= GAMEOVER_ANIM_FRAMES)
    {
        gameoverAnimActive = FALSE;
        for (u16 i = 0; i < GAMEOVER_LETTER_COUNT; i++)
            SPR_setPosition(gameoverLetters[i].sprite, gameoverLetters[i].finalX, gameoverLetters[i].finalY);
    }
}

// Blinks "NEW HIGH SCORE!"/"NEW HIGH WAVE!" (whichever apply this round) at
// a quick, fixed rate -- toggling the whole line between drawn and blanked
// every GAMEOVER_RECORD_BLINK_FRAMES, same on/off shape as the PAUSE
// decorations' marching dash, just alternating the full line instead of
// sliding a character along it. A no-op once neither record was beaten (see
// score_showGameOver()/score_init()/score_hideLivesIcons()).
static void recordBlinkStep(void)
{
    if (!recordScoreActive && !recordWaveActive)
        return;

    if (recordBlinkTimer > 0)
    {
        recordBlinkTimer--;
        return;
    }
    recordBlinkTimer = GAMEOVER_RECORD_BLINK_FRAMES;
    recordBlinkOn = !recordBlinkOn;

    if (recordScoreActive)
    {
        if (recordBlinkOn)
            VDP_drawText("NEW HIGH SCORE!", 8, GAMEOVER_RECORD_SCORE_ROW);
        else
            clearWindowRect(0, GAMEOVER_RECORD_SCORE_ROW, HUD_PANEL_COL0, GAMEOVER_RECORD_SCORE_ROW + 1);
    }
    if (recordWaveActive)
    {
        if (recordBlinkOn)
            VDP_drawText("NEW HIGH WAVE!", 9, GAMEOVER_RECORD_WAVE_ROW);
        else
            clearWindowRect(0, GAMEOVER_RECORD_WAVE_ROW, HUD_PANEL_COL0, GAMEOVER_RECORD_WAVE_ROW + 1);
    }
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

    // "GAME OVER" itself isn't drawn here -- see gameoverAnimStep()'s
    // sprite-letter animation, kicked off below.
    VDP_drawText("FINAL SCORE", 12, GAMEOVER_HUD_Y);
    uintToStr(score, buf, 6);
    VDP_drawText(buf, 14, GAMEOVER_HUD_Y + 1);
    VDP_drawText("WAVES CLEARED", 11, GAMEOVER_HUD_Y + 2);
    uintToStr(formation_wavesCleared(), buf, 2);
    VDP_drawText(buf, 17, GAMEOVER_HUD_Y + 3);
    VDP_drawText("PRESS START", 12, GAMEOVER_PRESS_START_ROW);

    // Checked (and, if beaten, persisted to SRAM) right here rather than
    // earlier at the moment lives ran out -- formation_currentWave() is
    // still the wave the player was actually on when the game ended, and
    // this is the one place both final values are settled and available.
    // score_hud_update()'s HISCORE/HIWAVE panel readout is refreshed
    // immediately too, rather than waiting for the next round's score_init(),
    // so a beaten record shows up-to-date right away.
    HighScoreResult hs = highscore_checkAndUpdate(score, formation_currentWave());
    recordScoreActive = hs.newHighScore;
    recordWaveActive = hs.newHighWave;
    recordBlinkOn = TRUE;
    recordBlinkTimer = GAMEOVER_RECORD_BLINK_FRAMES;
    if (recordScoreActive)
        VDP_drawText("NEW HIGH SCORE!", 8, GAMEOVER_RECORD_SCORE_ROW);
    if (recordWaveActive)
        VDP_drawText("NEW HIGH WAVE!", 9, GAMEOVER_RECORD_WAVE_ROW);
    if (recordScoreActive || recordWaveActive)
        drawHighScoreHudValues();

    gameoverAnimActive = TRUE;
    gameoverAnimTimer = 0;
    for (u16 i = 0; i < GAMEOVER_LETTER_COUNT; i++)
        SPR_setVisibility(gameoverLetters[i].sprite, VISIBLE);
}
