#include "intro.h"
#include "resources.h"
#include "bgstarfield.h"
#include "music.h"
#include "intro_crawl_generated.h"

// Crawl text is drawn at runtime (word-wrapped and stamped onto the
// tilemap here), not pre-rendered, and not even its own font asset -- it
// reuses the game's already-loaded system font tiles directly
// (TILE_FONT_INDEX -- see main.c's VDP_loadFont()), the same one every
// other piece of text in this game uses. See generate_intro.py's module
// docstring.
#define CRAWL_PLANE_COLS 40  // usable plane width, matches VISIBLE_COLS convention (see generate_terrain.py)
#define CRAWL_MAX_LINES 20   // generous cap for wrapText()'s output buffers
#define CRAWL_MARGIN_TILES 4 // 2 tiles either side
#define CRAWL_MAX_CHARS (CRAWL_PLANE_COLS - CRAWL_MARGIN_TILES) // 1 tile/char, native font size
#define CRAWL_LINE_ROWS 2 // 1 glyph row + 1 blank row of leading

// The crawl is stamped starting this many tile rows down the (64-tile-tall,
// see terrain_initPlaneSize()) plane, so at scroll=0 it's still fully below
// the visible screen.
#define CRAWL_PLANE_ROW_START (SCREEN_H / 8)

// Screen split: text and background image can't share the same screen rows
// (Genesis background planes have no per-pixel transparency, and BG_A
// always draws over BG_B/low sprites at matching priority -- see
// startBgSlideshow()'s comment), so the top half is banded off to the
// WINDOW plane (see intro_run()) for the background slideshow, leaving the
// bottom half for the crawl to scroll through -- half the usual vertical
// room, hence CRAWL_SPEED below being slower than title.c's own scroll
// effects to keep it readable.
#define SPLIT_ROW_TILES (SCREEN_H / 8 / 2) // 14 -- the boundary row, in tiles
#define HALF_H (SCREEN_H / 2)

// Fixed-point px/frame the crawl scrolls upward. Ideally matches
// generate_intro.py's own CRAWL_SPEED float copy (used there only to
// estimate slide durations, see BG_FADE_FRAMES's block -- the main loop's
// own end-of-crawl detection is driven by real scroll state, not that
// estimate, precisely so a drift between this and the generator's copy
// can't cut text off early -- see intro_run()'s own comment on this).
//
// fix32, not fix16: SGDK's fix16 is a 16-bit type with only 10 integer bits
// (see maths.h's FIX16_INT_BITS), i.e. it silently overflows past an
// integer magnitude of ~511 -- comfortably enough for a per-frame velocity
// like this one, but nowhere near enough for the *accumulated* scroll
// distance below (a real script can need 700+ px of total scroll). This
// was the actual cause of the crawl cutting off partway through: the old
// fix16 crawlScroll/totalScroll wrapped negative well before the real
// scroll distance was reached, satisfying "crawlScroll >= totalScroll"
// far too early.
#define CRAWL_SPEED FIX32(0.25)

#define STARFIELD_FADE_FRAMES REGION_PICK(180, 150) // ~3s

// Raster sway across the whole crawl -- same per-line horizontal-scroll
// mechanism as title.c's logo wobble, but a *linear* shear (a single
// straight-line lean, like a sheet of paper tilted slightly) rather than a
// sine wobble: offset varies smoothly with distance from screen center
// (one consistent slope per frame), not with a per-row sine phase, so it
// never shows more than one lean direction on screen at once -- a sine
// with enough phase-per-line to be visible at all also puts several full
// wave crests on screen simultaneously, which reads as the text visibly
// rippling rather than leaning. The lean's *angle* still drifts slowly
// frame-to-frame (SWAY_PHASE_SPEED_DEG), giving it some life without ever
// being a multi-crest wave at any single instant.
#define SWAY_MAX_SHEAR_PX 8 // horizontal offset at the screen's top/bottom edge, at the peak of the lean
#define SWAY_PHASE_SPEED_DEG FIX16(0.3) // degrees/frame -- a full lean-left-to-lean-right cycle takes ~20s

// Background slideshow: one image visible at a time on a single dedicated
// hardware palette slot (PAL1 -- PAL0 holds the crawl text, PAL2 the
// starfield via bgstarfield.c/PAL_ENVIRONMENT; none of PAL_PLAYER/ENEMY/
// ENVIRONMENT/BOSS are loaded yet at this point in main(), so borrowing
// raw PAL1 here is safe, same reasoning as title.c's PAL0 borrow for the
// logo). Fills nearly all of the top half (see SPLIT_ROW_TILES), centered,
// not sharing rows with the crawl at all, which is what actually keeps it
// visible (see startBgSlideshow()'s comment on why coexisting with opaque
// BG_A text on the same rows doesn't work on this hardware).
//
// Each image's on-screen duration is derived from INTRO_TOTAL_FRAMES (see
// startBgSlideshow()) -- generate_intro.py's computed total split evenly
// across BG_FRAME_COUNT images -- rather than an independent fixed timer,
// so slide pacing always tracks however long the crawl text actually takes
// regardless of script length.
#define BG_FADE_FRAMES REGION_PICK(30, 25) // ~0.5s fade each way, see updateBgSlideshow()

// 240x112 (30x14 tiles), centered in the SCREEN_W-wide top-half band (see
// generate_placeholders.py's own INTRO_BG_W/H comment). Drawn onto the
// WINDOW plane's own tilemap (see startBgSlideshow()), not a sprite -- a
// single SPRITE frame this large hits two separate rescomp/hardware
// ceilings (no dimension >=32 tiles; no more than 16 internal
// hardware-sprite pieces per frame, which is content-dependent, not just
// size), neither of which applies to a background plane.
#define BG_IMG_W 240
#define BG_IMG_H 112
#define BG_TILE_X ((SCREEN_W / 8 - BG_IMG_W / 8) / 2)
#define BG_TILE_Y ((SPLIT_ROW_TILES - BG_IMG_H / 8) / 2)

// Dedicated tile range for the slideshow images -- well clear of
// TILE_FONT_INDEX (the crawl text's own tiles, at the opposite, high end of
// tile space) and of terrain.c's own TERRAIN_BASE_TILE/STARFIELD_BASE_TILE
// (TILE_USER_INDEX/+16) -- unlike title.c's/gameplay's tile ranges (which
// only ever need to avoid *sequential* reuse, since none of them are ever
// on screen at the same time as this scene), bgstarfield_start() actively
// loads its starfield tiles at STARFIELD_BASE_TILE and keeps them on
// screen for this scene's entire runtime, so this really does need to
// avoid a *concurrent* collision, not just claim an unused range for
// later. +32 matches score.c's own HUD_FILL_TILE convention for "past
// terrain(4)/starfield(3) tiles". Every slide reuses this exact same base
// index (see updateBgSlideshow()), so VRAM usage never grows across the
// show -- each new image's tiles simply overwrite the previous one's.
#define BG_BASE_TILE (TILE_USER_INDEX + 32)

typedef struct
{
    const Image *image;
    const Palette *palette;
} IntroBgFrame;

static const IntroBgFrame introBgFrames[] = {
    { &img_intro_bg_planet, &palette_intro_bg_planet },
    { &img_intro_bg_fleet,  &palette_intro_bg_fleet },
    { &img_intro_bg_nebula, &palette_intro_bg_nebula },
};
#define BG_FRAME_COUNT (sizeof(introBgFrames) / sizeof(introBgFrames[0]))

static u16 bgSlideFrames; // total frames each image gets on screen, including its own fade in/out
static u16 bgSlideTimer;  // counts down within the current image's window
static u8 bgIndex;
static bool bgFadingOut;  // guards updateBgSlideshow()'s fade-out trigger to fire exactly once per slide

static void startBgSlideshow(void)
{
    // loadpal=FALSE: colors are handled entirely through our own
    // PAL_setPalette/PAL_fadePalette calls below (see this call's own
    // comment on why the very first one must be instant, not a fade) --
    // letting VDP_drawImageEx push the image's bundled palette too would
    // just be redundant with, and race, that.
    VDP_drawImageEx(WINDOW, introBgFrames[0].image, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, BG_BASE_TILE),
                     BG_TILE_X, BG_TILE_Y, FALSE, DMA);

    // INTRO_TOTAL_FRAMES split evenly across every image -- see this
    // block's own top comment. Floored at 4x BG_FADE_FRAMES so a very
    // short crawl (or a large BG_FRAME_COUNT) can't shrink a slide's
    // window below "fade in immediately followed by fade out", which
    // would either glitch or never show the image at full brightness at
    // all.
    bgSlideFrames = INTRO_TOTAL_FRAMES / BG_FRAME_COUNT;
    if (bgSlideFrames < BG_FADE_FRAMES * 4)
        bgSlideFrames = BG_FADE_FRAMES * 4;
    bgSlideTimer = bgSlideFrames;
    bgIndex = 0;
    bgFadingOut = FALSE;

    // Set instantly, NOT via PAL_fadePalette -- SGDK's async palette fade
    // is single global state (fadeR[]/fadeG[]/fadeB[]/fadeCounter etc. in
    // pal.c, not per-palette), so a fade started here would immediately
    // stomp bgstarfield_start()'s own fade-in (called moments earlier, on
    // a different palette) before it ever gets a single real step in --
    // two concurrent PAL_fadePalette calls can never coexist regardless of
    // which palette each targets. Every *later* transition (see
    // updateBgSlideshow()) is safe to fade because by then the starfield's
    // fade has long since finished on its own.
    PAL_setPalette(PAL1, introBgFrames[0].palette->data, DMA);
}

// Genesis hardware has no true crossfade between two different images (no
// alpha blending) -- this fades PAL1 to black over the slide's last
// BG_FADE_FRAMES frames, redraws the WINDOW tilemap with the next image
// while it's invisible (mid-black), then fades back up from black. Reads
// as a clean fade-through-black transition rather than an instant swap,
// using only the one dedicated palette slot this scene already has.
static void updateBgSlideshow(void)
{
    if (bgSlideTimer > BG_FADE_FRAMES)
    {
        bgSlideTimer--;
        return;
    }

    if (!bgFadingOut)
    {
        u16 black[16] = {0};
        PAL_fadePalette(PAL1, introBgFrames[bgIndex].palette->data, black, BG_FADE_FRAMES, TRUE);
        bgFadingOut = TRUE;
    }

    if (bgSlideTimer > 0)
    {
        bgSlideTimer--;
        return;
    }

    bgIndex = (bgIndex + 1) % BG_FRAME_COUNT;
    // Same base tile index every time (see BG_BASE_TILE's own comment) --
    // this overwrites the previous image's tiles in place rather than
    // growing VRAM usage across the show.
    VDP_drawImageEx(WINDOW, introBgFrames[bgIndex].image, TILE_ATTR_FULL(PAL1, FALSE, FALSE, FALSE, BG_BASE_TILE),
                     BG_TILE_X, BG_TILE_Y, FALSE, DMA);

    u16 black[16] = {0};
    PAL_fadePalette(PAL1, black, introBgFrames[bgIndex].palette->data, BG_FADE_FRAMES, TRUE);

    bgSlideTimer = bgSlideFrames;
    bgFadingOut = FALSE;
}

// Recomputed every frame, only across the bottom half (see SPLIT_ROW_TILES
// -- the top half is WINDOW-masked, so BG_A's scroll there is never seen).
// `tm` lets the very first call use a synchronous CPU transfer instead of
// DMA -- same race title.c's playTitleWobble() has to avoid: while the
// display is still off (see intro_run()), a DMA write wouldn't actually
// flush until the main loop's own SYS_doVBlankProcess(), which by then
// runs *after* VDP_setEnable(TRUE) -- one visible frame with stale (zero)
// offsets.
static void updateSway(fix16 timePhase, TransferMethod tm)
{
    // -SWAY_MAX_SHEAR_PX..+SWAY_MAX_SHEAR_PX, the same for every row this
    // frame -- only `timePhase` (frame-to-frame) varies the lean's current
    // angle, never row-to-row (see this function's declaration comment).
    fix16 shear = F16_mul(FIX16(SWAY_MAX_SHEAR_PX), F16_sin(timePhase));

    // Static, not stack-local: this file calls into several other sizable
    // stack users each frame (wrapText()'s own buffers, etc.) on a
    // platform with a famously small stack budget -- a 224-byte array is
    // cheap as a fixed static, not worth re-risking every single frame.
    static s16 lineOffsets[HALF_H];
    for (u16 y = 0; y < HALF_H; y++)
    {
        fix16 rowFactor = F16_div(FIX16((s16) y - HALF_H / 2), FIX16(HALF_H / 2));
        lineOffsets[y] = F16_toInt(F16_mul(shear, rowFactor));
    }
    VDP_setHorizontalScrollLine(BG_A, SPLIT_ROW_TILES * 8, lineOffsets, HALF_H, tm);
}

// Greedy word-wrap: walks `text`, breaking before a word would exceed
// maxChars on the current line. Returns the number of lines produced
// (capped at CRAWL_MAX_LINES); each line's start pointer/length are
// written to lineStarts/lineLengths (into `text` itself -- not
// null-terminated substrings, callers must use the paired length).
static u16 wrapText(const char *text, u16 maxChars, const char **lineStarts, u8 *lineLengths)
{
    u16 lineCount = 0;
    const char *p = text;

    while (*p && lineCount < CRAWL_MAX_LINES)
    {
        while (*p == ' ')
            p++;
        if (!*p)
            break;

        const char *lineStart = p;
        const char *lastSpace = NULL;
        u16 col = 0;

        while (*p && col < maxChars)
        {
            if (*p == ' ')
                lastSpace = p;
            p++;
            col++;
        }

        // Still mid-word at the cutoff (and not at the text's natural
        // end) -- back up to the last space so words don't split.
        if (*p && *p != ' ' && lastSpace != NULL)
            p = lastSpace;

        lineStarts[lineCount] = lineStart;
        lineLengths[lineCount] = (u8) (p - lineStart);
        lineCount++;
    }

    return lineCount;
}

// Stamps one line at plane row `row`, reusing the already-loaded system
// font directly (TILE_FONT_INDEX -- see main.c's VDP_loadFont()) -- no
// dedicated font asset for this scene at all.
static void drawLine(const char *line, u8 len, u16 row)
{
    u16 col = (CRAWL_PLANE_COLS - len) / 2;
    for (u8 i = 0; i < len; i++)
        if (line[i] != ' ')
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, TRUE, FALSE, FALSE, TILE_FONT_INDEX + (line[i] - 32)), col + i, row);
}

// The plane is only CRAWL_PLANE_SPAN tile rows tall (see
// terrain_initPlaneSize()) and wraps every that many rows -- INTRO_LINES[]
// isn't stamped all at once up front, since a long enough script (many
// strings, generous CRAWL_GAP_LINES gaps between them) can easily add up to
// more text than the plane could ever hold in one go. Instead it's streamed
// in incrementally as the crawl scrolls (see refillCrawl()), the same
// "reuse whatever's safely scrolled out of view" spirit as terrain.c's
// off-screen half regen, just a sequential dequeue through INTRO_LINES[]
// rather than a random reroll -- so total crawl length is no longer capped
// by plane size at all, only by how much text you actually write.
#define CRAWL_PLANE_SPAN 64

// Blanks one physical plane row (mod CRAWL_PLANE_SPAN) before it's reused --
// see stampEntry()'s own comment on why every row the writer advances past
// needs this, not just the ones it's about to draw text into.
static void clearCrawlRow(u16 row)
{
    VDP_fillTileMapRect(BG_A, 0, 0, row % CRAWL_PLANE_SPAN, CRAWL_PLANE_COLS, 1);
}

// How many text-line-heights an empty INTRO_LINES[] entry leaves as a gap
// -- see generate_intro.py's module docstring: a blank line in
// intro_crawl_text.txt becomes one of these.
#define CRAWL_GAP_LINES 6

// Generous upper bound on any single INTRO_LINES[] entry's stamped height
// (a gap is CRAWL_GAP_LINES*CRAWL_LINE_ROWS=12 rows; CRAWL_MAX_LINES-worth
// of wrapped text lines would be far more, but realistic paragraphs are
// short) -- subtracted from refillCrawl()'s ceiling so that stamping one
// more *whole* entry (see stampEntry(), which never stops partway through
// one) can never push crawlStampRow past the true CRAWL_PLANE_SPAN
// ring-buffer boundary, only up to it.
#define CRAWL_MAX_ENTRY_ROWS 16

// Streaming state -- see refillCrawl(). stampRow is a monotonically
// increasing plane-row counter (only ever taken mod CRAWL_PLANE_SPAN at the
// actual VDP_setTileMapXY call site in drawLine()/here), not reset back to
// CRAWL_PLANE_ROW_START's range -- this is what lets it keep advancing
// arbitrarily far past one physical lap of the plane.
static u16 crawlNextEntry; // next not-yet-queued index into INTRO_LINES[]
static u16 crawlStampRow;  // next plane row (unbounded) to stamp/clear at
static bool crawlDone;     // TRUE once every INTRO_LINES[] entry has been queued (not necessarily scrolled past yet)
static u16 crawlDoneRow;   // crawlStampRow's value at the instant crawlDone latched -- see intro_run()'s totalScroll

// Stamps one INTRO_LINES[] entry (word-wrapped if non-empty, else just
// advances past a gap) at crawlStampRow, advancing it. Entries are always
// stamped as a whole (see the module-level comment) -- refillCrawl()'s own
// ceiling leaves headroom (CRAWL_MAX_ENTRY_ROWS) for exactly this, so a
// single entry finishing the job never overshoots the true ring-buffer
// boundary.
//
// Every physical row the writer advances past gets explicitly blanked
// (clearCrawlRow()) before anything (possibly nothing, for a gap) is drawn
// into it -- the plane is only CRAWL_PLANE_SPAN rows tall and wraps, so once
// the crawl has scrolled more than one full lap, physical rows get reused by
// later entries. A gap advancing crawlStampRow without touching VRAM at all
// left whatever an earlier lap's entry had drawn there completely
// undisturbed -- since a gap's own rows aren't guaranteed to land on exactly
// the same physical rows a previous entry used, that stale text (e.g. the
// very first lines) could sit untouched in VRAM and scroll back into view
// once the crawl wrapped around, well after it had actually already been
// shown once.
static void stampEntry(u16 e)
{
    if (INTRO_LINES[e][0] == '\0')
    {
        u16 gapRows = CRAWL_GAP_LINES * CRAWL_LINE_ROWS;
        for (u16 i = 0; i < gapRows; i++)
            clearCrawlRow(crawlStampRow + i);
        crawlStampRow += gapRows;
        return;
    }

    // Static, not stack-local -- see updateSway()'s own lineOffsets for why
    // (this file calls into several sizable stack users each frame on a
    // platform with a small stack budget).
    static const char *lineStarts[CRAWL_MAX_LINES];
    static u8 lineLengths[CRAWL_MAX_LINES];
    u16 lineCount = wrapText(INTRO_LINES[e], CRAWL_MAX_CHARS, lineStarts, lineLengths);
    for (u16 i = 0; i < lineCount; i++)
    {
        clearCrawlRow(crawlStampRow);
        clearCrawlRow(crawlStampRow + 1); // the blank leading row (see CRAWL_LINE_ROWS)
        drawLine(lineStarts[i], lineLengths[i], crawlStampRow % CRAWL_PLANE_SPAN);
        crawlStampRow += CRAWL_LINE_ROWS;
    }
}

// Tops up the stamped-ahead buffer as the crawl scrolls, queuing entries
// from INTRO_LINES[] until the queue is exhausted (sets crawlDone), then
// keeps going -- see below -- staying within CRAWL_MAX_ENTRY_ROWS of the
// current scroll position the whole time, a classic ring-buffer invariant
// (writer must stay within one buffer length of the reader, never jump
// ahead of it). Call once per frame (see intro_run()); cheap when there's
// nothing to do (the loop body just doesn't run).
static void refillCrawl(fix32 crawlScroll)
{
    u16 readerRow = (u16) (F32_toInt(crawlScroll) / 8);
    // -CRAWL_MAX_ENTRY_ROWS: see that constant's own comment -- leaves
    // enough headroom that finishing the entry which crosses this line
    // still can't reach the *true* CRAWL_PLANE_SPAN boundary.
    u16 ceiling = readerRow + CRAWL_PLANE_SPAN - CRAWL_MAX_ENTRY_ROWS;

    while (crawlStampRow < ceiling)
    {
        if (crawlNextEntry < INTRO_LINE_COUNT)
        {
            stampEntry(crawlNextEntry);
            crawlNextEntry++;
            if (crawlNextEntry >= INTRO_LINE_COUNT)
            {
                crawlDone = TRUE;
                crawlDoneRow = crawlStampRow; // see intro_run()'s totalScroll -- crawlStampRow itself keeps moving below
            }
            continue;
        }

        // No more real content, but keep advancing crawlStampRow one row
        // at a time regardless, at the exact same reader-trailing pace as
        // real entries (never touching a row until the reader has already
        // scrolled past it). The script's total content length isn't
        // guaranteed to be an exact multiple of CRAWL_PLANE_SPAN, so some
        // physical rows (the ones stampEntry() only ever visited once,
        // near the *end* of the plane's first lap) would otherwise never
        // get a second visit from real content to naturally clear them --
        // and since the crawl's total scroll distance exceeds one full lap
        // (see HALF_H + content height below), those rows alias back into
        // view a second time right as the crawl finishes, showing whatever
        // stale text was stamped there during the first lap. Clearing them
        // here, one at a time, in step with the ceiling exactly like real
        // entries do, guarantees every row the reader will ever actually
        // reach has already been blanked -- unlike an earlier version of
        // this fix that jump-cleared a whole lap the instant crawlDone
        // latched, which wrapped around and erased the just-stamped final
        // entry's own still-unseen rows instead.
        clearCrawlRow(crawlStampRow);
        crawlStampRow++;
    }
}

void intro_run(void)
{
    VDP_setVerticalScroll(BG_A, 0);

    // Bands the WINDOW plane across the top half (down=FALSE: rows
    // 0..SPLIT_ROW_TILES) -- WINDOW always substitutes for BG_A within its
    // band on real hardware, so this guarantees the crawl (BG_A) can never
    // visually intrude into the top half no matter its scroll position,
    // leaving that half exclusively to the background slideshow sprite.
    // HPos left off (0-width) so only this V band decides, same convention
    // as score.c's own window bands.
    VDP_setWindowVPos(FALSE, SPLIT_ROW_TILES);
    VDP_setWindowHPos(FALSE, 0);
    VDP_clearPlane(WINDOW, TRUE);
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);

    // Blanked while everything is drawn/set up, same reasoning as
    // title_run()'s own VDP_setEnable(FALSE) -- avoids a flash of
    // partially-drawn content before the first real frame.
    VDP_setEnable(FALSE);

    // Unlike title_run() (which starts this driver load non-blocking and
    // polls readiness across several already-visible animation frames, so
    // the logo isn't stuck undistorted while it boots), intro_run() has the
    // display off for its whole setup phase regardless -- so a blocking
    // wait here (the Z80 driver takes several frames to boot) costs nothing
    // visible. This is also the very first XGM2 driver load each power-on,
    // before title_run() ever runs; title_run()'s own XGM2_loadDriver() call
    // later is a no-op against the same already-loaded driver (see
    // xgm2.c's XGM2_loadDriver()).
    XGM2_loadDriver(TRUE);
    music_init();
    XGM2_play(intro_music);

    // Supplies the actual ink color the crawl draws with -- the reused
    // system font tiles only carry pixel *indices* (1=opaque black
    // backing, 2=ink), not colors of their own; whatever's loaded onto
    // PAL0 decides what index 2 actually looks like.
    PAL_setPalette(PAL0, palette_intro_crawl.data, CPU);

    // Fresh state for this run (see refillCrawl()) -- intro_run() only
    // ever executes once per power-on (see intro.h), so these would start
    // zeroed either way, but set explicitly for clarity.
    crawlNextEntry = 0;
    crawlStampRow = CRAWL_PLANE_ROW_START;
    crawlDone = FALSE;
    crawlDoneRow = 0;
    refillCrawl(0); // fills the buffer up to one plane-span ahead, same as the old one-shot stampCrawl() did

    VDP_setScrollingMode(HSCROLL_LINE, VSCROLL_PLANE);

    bgstarfield_start(STARFIELD_FADE_FRAMES);
    startBgSlideshow();

    fix32 crawlScroll = 0;
    fix16 swayPhase = 0;

    // Primes the joypad state with one real poll before ever reading it --
    // intro_run() runs before any SYS_doVBlankProcess() has executed this
    // power-on session at all (see main.c), and JOY_readJoypad() reads
    // whatever was last latched during vblank; without at least one poll
    // first, this first read could be reading a never-yet-updated (i.e.
    // stale/undefined) value rather than actual pad state, immediately
    // matching the "any button" skip check below on frame one regardless
    // of what's really being pressed.
    SYS_doVBlankProcess();

    // Debounce: guards against a button still physically held from before
    // boot (e.g. power-on with Start already pressed) -- without this, that
    // held press would instantly satisfy the very first skip check below.
    // Masked to BUTTON_ALL (defined bits only) rather than a raw != 0
    // check, in case any reserved/unused bit in the raw value isn't
    // actually guaranteed zero when idle.
    while (JOY_readJoypad(JOY_1) & BUTTON_ALL)
        SYS_doVBlankProcess();

    updateSway(swayPhase, CPU);
    VDP_setEnable(TRUE);

    // Ends once every INTRO_LINES[] entry has been queued (crawlDone) AND
    // the scroll has carried the last of it out of the bottom half (past
    // HALF_H, the WINDOW-masked boundary -- see SPLIT_ROW_TILES) -- i.e.
    // once the last line has genuinely disappeared into the top half. This
    // dynamic condition is authoritative and must NOT be gated behind a
    // precomputed minimum frame count in addition -- a previous version of
    // this code did that as a defensive floor, but the plane is a ring
    // buffer only CRAWL_PLANE_SPAN tiles tall while a real script can need
    // more than a full lap of scroll to show everything; forcing the loop
    // to keep running past the point this condition is satisfied scrolls
    // straight into old, already-shown rows lapping back into view --
    // visible as earlier lines repeating instead of the crawl ending. This
    // condition alone is what's actually correct.
    //
    // Edge-detected (press, not hold) -- the debounce loop above already
    // guarantees prevJoy starts clear of any button held over from before
    // boot, so the first real press here is genuinely fresh, not a stray
    // carry-over.
    u16 prevJoy = 0;

    while (TRUE)
    {
        u16 joy = JOY_readJoypad(JOY_1);
        if ((joy & BUTTON_ALL) && !(prevJoy & BUTTON_ALL))
            break;
        prevJoy = joy;

        // Un-negated (unlike terrain.c's VDP_setVerticalScroll(BG_A, -...)):
        // increasing V shows lower (higher plane-row) content at the top of
        // the screen, which is exactly "content moves up" -- the Star Wars
        // crawl direction -- with no sign flip needed, opposite of
        // terrain.c's deliberate "world slides down toward the player"
        // convention.
        crawlScroll += CRAWL_SPEED;
        VDP_setVerticalScroll(BG_A, F32_toInt(crawlScroll));

        refillCrawl(crawlScroll);

        if (crawlDone)
        {
            // crawlDoneRow, not crawlStampRow -- the latter keeps advancing
            // after crawlDone (see refillCrawl()'s trailing clear-only
            // pass), but the scroll distance needed to carry the last real
            // line off the top is fixed the instant crawlDone latches.
            fix32 totalScroll = FIX32(HALF_H + (crawlDoneRow - CRAWL_PLANE_ROW_START) * 8);
            if (crawlScroll >= totalScroll)
                break;
        }

        swayPhase += SWAY_PHASE_SPEED_DEG;
        // Keep the angle inside [0, 360) ourselves: SGDK's fix16 is a 16-bit
        // type with only 10 integer bits (maths.h's FIX16_INT_BITS), so an
        // unbounded accumulator like this one would silently overflow its
        // own container after ~1700 frames (well within this scene's
        // runtime) if left to grow forever, corrupting F16_sin()'s lookups
        // feeding updateSway()'s line offsets. Wrapping it back into
        // [0, 360) every frame keeps it inside fix16's actual safe range.
        if (swayPhase >= FIX16(360))
            swayPhase -= FIX16(360);
        updateSway(swayPhase, DMA);

        bgstarfield_update();
        updateBgSlideshow();

        SYS_doVBlankProcess();
    }

    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_setHorizontalScroll(BG_A, 0);

    // Deliberately NOT faded out here (unlike title_run()'s own end-of-
    // screen XGM2_fadeOutAndStop()): title_run() runs immediately next (see
    // main.c) and calls XGM2_play(title_music) on its very first wobble
    // frame (the Z80 driver is already loaded/ready by then, so that check
    // passes instantly) -- a fade-out queued here would still be mid-
    // countdown at that point, and its deferred auto-stop firing a few
    // frames later was silencing title_music right after it started
    // (reproducible on the very first title screen each power-on; every
    // later return to the title screen never hits this because gameplay's
    // own music is simply left playing with no fade/stop of its own, so
    // XGM2_play(title_music) there is overriding a steady-state song
    // instead of racing a pending stop). Simply leaving intro_music playing
    // right up until title_run() cuts it over avoids the race entirely.
    PAL_fadeOutAll(STARFIELD_FADE_FRAMES / 2, FALSE);

    VDP_clearPlane(WINDOW, TRUE); // clears the last slide's tiles off the WINDOW plane -- see intro_run()'s own opening VDP_clearPlane(WINDOW, TRUE) comment
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);
}
