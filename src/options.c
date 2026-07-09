#include "options.h"
#include "resources.h"
#include "sfx.h"
#include "title.h"

#define OPTIONS_DEFAULT_LIVES          4
#define OPTIONS_DEFAULT_EXTRALIFE_INDEX 1 // index into extraLifeValues/Labels -- 50000

static const u32 extraLifeValues[3]        = {0,      50000,   100000};
static const char *const extraLifeLabels[3] = {"NONE", "50000", "100000"};
#define EXTRALIFE_OPTION_COUNT 3

static u8 livesSetting;
static u8 extraLifeIndex;

// Dimmed copy of the title logo's palette (see title_drawLogoBackground()),
// loaded onto PAL0 for the whole time this scene is up so the logo reads as
// a shadowed backdrop behind the menu rather than competing with it at full
// brightness. Halves each color channel, rounding down to the nearest even
// step (VDP colors only use even 3-bit values -- see pal.h's
// VDPPALETTE_COLORMASK). Not restored on the way out -- title.c always
// redraws the title screen immediately after options_run() returns, which
// reloads PAL0 back to the bright original (see drawTitleScreen()).
static u16 shadowPalette[16];

static void buildShadowPalette(void)
{
    const u16 *src = title_image.palette->data;

    for (u16 i = 0; i < 16; i++)
    {
        u16 c = src[i];
        u16 r = (c & 0x000E) >> 1;
        u16 g = (c & 0x00E0) >> 1;
        u16 b = (c & 0x0E00) >> 1;
        shadowPalette[i] = (r & 0x000E) | (g & 0x00E0) | (b & 0x0E00);
    }
}

// Redraws the logo (dimmed, via the shadow palette PAL0 already holds --
// see options_run()) as this scene's backdrop, in place of the plain
// VDP_clearPlane() blank a menu screen would otherwise get. Still clears
// first: this scene has several screens (main menu, sound test) sharing
// BG_A, so leftover opaque text tiles from whichever one was up before need
// blanking before the logo (which only covers its own rows) goes back down.
static void drawShadowedBackground(void)
{
    VDP_clearPlane(BG_A, TRUE);
    title_drawLogoBackground();
}

void options_init(void)
{
    livesSetting = OPTIONS_DEFAULT_LIVES;
    extraLifeIndex = OPTIONS_DEFAULT_EXTRALIFE_INDEX;
}

u8 options_getStartingLives(void)
{
    return livesSetting;
}

u32 options_getExtraLifeInterval(void)
{
    return extraLifeValues[extraLifeIndex];
}

// Main menu layout -- label column, then a fixed-width value column (with a
// blank column of its own gap after the longest label, "EXTRA LIFE EVERY",
// so its value never reads glued to the label) so a shorter new value (e.g.
// "NONE" after "100000") cleanly overwrites the old one's leftover
// characters (see VDP_drawTextFill()).
#define MENU_TITLE_Y   3
#define MENU_X         10
#define MENU_VALUE_X   27
#define MENU_VALUE_LEN 7
#define ROW_LIVES_Y      10
#define ROW_EXTRALIFE_Y  13
#define ROW_SOUNDTEST_Y  16
#define ROW_BACK_Y       19
#define HINT1_Y 22
#define HINT2_Y 24

typedef enum
{
    OPT_ROW_LIVES,
    OPT_ROW_EXTRALIFE,
    OPT_ROW_SOUNDTEST,
    OPT_ROW_BACK,
    OPT_ROW_COUNT,
} OptionsRow;

static u16 rowY(OptionsRow row)
{
    switch (row)
    {
        case OPT_ROW_LIVES:      return ROW_LIVES_Y;
        case OPT_ROW_EXTRALIFE:  return ROW_EXTRALIFE_Y;
        case OPT_ROW_SOUNDTEST:  return ROW_SOUNDTEST_Y;
        default:                 return ROW_BACK_Y;
    }
}

// Only ever touches the single cursor cell for one row -- see
// drawMainMenuStatic()'s comment on why a full-plane redraw per keypress was
// causing the whole menu to visibly flicker.
static void drawMenuCursor(OptionsRow row, bool selected)
{
    VDP_drawText(selected ? ">" : " ", MENU_X - 2, rowY(row));
}

static void drawLivesValue(void)
{
    char buf[4];
    uintToStr(livesSetting, buf, 1);
    VDP_drawTextFill(buf, MENU_VALUE_X, ROW_LIVES_Y, MENU_VALUE_LEN);
}

static void drawExtraLifeValue(void)
{
    VDP_drawTextFill(extraLifeLabels[extraLifeIndex], MENU_VALUE_X, ROW_EXTRALIFE_Y, MENU_VALUE_LEN);
}

// Drawn once per entry into this scene, not per keypress: VDP_clearPlane()
// is a full 64x64-tile DMA clear, and re-running it (plus every label) on
// every single UP/DOWN/LEFT/RIGHT made the whole menu visibly flash each
// time. Selection/value changes afterward only touch their own single
// cursor cell or value field (see drawMenuCursor()/drawLivesValue()/
// drawExtraLifeValue()).
static void drawMainMenuStatic(void)
{
    drawShadowedBackground();
    VDP_drawText("OPTIONS", 16, MENU_TITLE_Y);

    VDP_drawText("LIVES", MENU_X, ROW_LIVES_Y);
    VDP_drawText("EXTRA LIFE EVERY", MENU_X, ROW_EXTRALIFE_Y);
    VDP_drawText("SOUND TEST", MENU_X, ROW_SOUNDTEST_Y);
    VDP_drawText("BACK", MENU_X, ROW_BACK_Y);

    drawLivesValue();
    drawExtraLifeValue();

    VDP_drawText("LEFT/RIGHT: CHANGE VALUE", 7, HINT1_Y);
    VDP_drawText("START: SELECT   B: EXIT", 8, HINT2_Y);
}

#define SOUND_TEST_ENTRY_COUNT 6
#define SOUND_TEST_TITLE_Y  3
#define SOUND_TEST_VALUE_X   9
#define SOUND_TEST_VALUE_Y  12
#define SOUND_TEST_VALUE_LEN 20 // longest label, "MUSIC: TITLE THEME", is 18 chars
#define SOUND_TEST_HINT_Y   16

static const char *const soundTestLabels[SOUND_TEST_ENTRY_COUNT] = {
    "MUSIC: TITLE THEME",
    "SFX: SHOOT",
    "SFX: EXPLOSION",
    "SFX: POWERUP",
    "SFX: BOSS SCREAM",
    "SFX: EXTRA LIFE",
};

// Persists across visits within this power-on session, like every other
// options.c setting -- not reset back to entry 0 each time.
static u8 soundTestIndex;

static void playSoundTestEntry(u8 index)
{
    switch (index)
    {
        case 0:
            // The only music track there is -- toggles rather than always
            // restarting, so tapping A again stops it instead of always
            // retriggering from the top.
            if (XGM2_isPlaying())
                XGM2_stop();
            else
                XGM2_play(title_music);
            break;
        case 1: sfx_play_shoot();           break;
        case 2: sfx_play_explosion();       break;
        case 3: sfx_play_powerup();         break;
        case 4: sfx_play_bossDeathScream(); break;
        case 5: sfx_play_extraLife();       break;
    }
}

// Only touches the value field -- see drawMainMenuStatic()'s comment on why
// a full redraw per keypress isn't done here either.
static void drawSoundTestValue(void)
{
    VDP_drawTextFill(soundTestLabels[soundTestIndex], SOUND_TEST_VALUE_X, SOUND_TEST_VALUE_Y, SOUND_TEST_VALUE_LEN);
}

// Drawn once per entry, not per keypress -- see drawMainMenuStatic().
static void drawSoundTestStatic(void)
{
    drawShadowedBackground();
    VDP_drawText("SOUND TEST", 15, SOUND_TEST_TITLE_Y);
    drawSoundTestValue();

    VDP_drawText("LEFT/RIGHT: CHOOSE", 10, SOUND_TEST_HINT_Y);
    VDP_drawText("A/START: PLAY OR STOP", 9, SOUND_TEST_HINT_Y + 2);
    VDP_drawText("B: BACK", 16, SOUND_TEST_HINT_Y + 4);
}

// Sub-screen reached from the main menu's SOUND TEST row -- LEFT/RIGHT
// cycles through music/sfx entries the same way the main menu's LIVES/EXTRA
// LIFE rows cycle their values, A/START plays whichever is currently shown.
// Doesn't touch music/sfx state on the way out -- whatever's playing (or
// not) when the player backs out just keeps going, same as any other sfx
// call elsewhere in the game.
static void soundTest_run(void)
{
    u16 prevJoy = JOY_readJoypad(JOY_1);

    // Debounce: A/START just opened this sub-screen (see options_run()) --
    // don't let that same physical press also trigger a play/stop.
    while (JOY_readJoypad(JOY_1) & (BUTTON_A | BUTTON_START))
    {
        sfx_update();
        SYS_doVBlankProcess();
    }

    drawSoundTestStatic();

    while (TRUE)
    {
        sfx_update();
        SYS_doVBlankProcess();

        u16 joy = JOY_readJoypad(JOY_1);
        u16 pressed = joy & ~prevJoy;
        prevJoy = joy;

        if (pressed & BUTTON_LEFT)
        {
            if (soundTestIndex > 0)
            {
                soundTestIndex--;
                drawSoundTestValue();
            }
        }
        else if (pressed & BUTTON_RIGHT)
        {
            if (soundTestIndex < SOUND_TEST_ENTRY_COUNT - 1)
            {
                soundTestIndex++;
                drawSoundTestValue();
            }
        }
        else if (pressed & (BUTTON_A | BUTTON_START))
        {
            playSoundTestEntry(soundTestIndex);
        }
        else if (pressed & BUTTON_B)
        {
            return;
        }
    }
}

void options_run(void)
{
    OptionsRow selectedRow = OPT_ROW_LIVES;
    u16 prevJoy = JOY_readJoypad(JOY_1);

    // Debounce: this scene is entered via A+START (see title.c) -- don't
    // act on either of those held buttons until both are released.
    while (JOY_readJoypad(JOY_1) & (BUTTON_A | BUTTON_START))
        SYS_doVBlankProcess();

    buildShadowPalette();
    PAL_setPalette(PAL0, shadowPalette, DMA);

    // Text stays at full brightness against the now-dimmed logo: PAL1 is
    // free to borrow here (PAL_ENEMY, only actually loaded once gameplay
    // starts -- see game.h), so load the title's real colors onto it and
    // point VDP_drawText() there instead of at PAL0. Restored to the usual
    // PAL0 below on the way out.
    PAL_setPalette(PAL1, title_image.palette->data, DMA);
    VDP_setTextPalette(PAL1);

    drawMainMenuStatic();
    drawMenuCursor(selectedRow, TRUE);

    while (TRUE)
    {
        sfx_update();
        SYS_doVBlankProcess();

        u16 joy = JOY_readJoypad(JOY_1);
        u16 pressed = joy & ~prevJoy;
        prevJoy = joy;

        if (pressed & BUTTON_UP)
        {
            drawMenuCursor(selectedRow, FALSE);
            selectedRow = (selectedRow == 0) ? OPT_ROW_COUNT - 1 : selectedRow - 1;
            drawMenuCursor(selectedRow, TRUE);
        }
        else if (pressed & BUTTON_DOWN)
        {
            drawMenuCursor(selectedRow, FALSE);
            selectedRow = (selectedRow + 1) % OPT_ROW_COUNT;
            drawMenuCursor(selectedRow, TRUE);
        }
        else if (pressed & BUTTON_LEFT)
        {
            if (selectedRow == OPT_ROW_LIVES && livesSetting > OPTIONS_MIN_LIVES)
            {
                livesSetting--;
                drawLivesValue();
            }
            else if (selectedRow == OPT_ROW_EXTRALIFE && extraLifeIndex > 0)
            {
                extraLifeIndex--;
                drawExtraLifeValue();
            }
        }
        else if (pressed & BUTTON_RIGHT)
        {
            if (selectedRow == OPT_ROW_LIVES && livesSetting < OPTIONS_MAX_LIVES)
            {
                livesSetting++;
                drawLivesValue();
            }
            else if (selectedRow == OPT_ROW_EXTRALIFE && extraLifeIndex < EXTRALIFE_OPTION_COUNT - 1)
            {
                extraLifeIndex++;
                drawExtraLifeValue();
            }
        }
        else if (pressed & (BUTTON_A | BUTTON_START))
        {
            if (selectedRow == OPT_ROW_SOUNDTEST)
            {
                soundTest_run();
                // soundTest_run() took over BG_A for its own screen -- the
                // main menu needs a full redraw on the way back, same as
                // when this scene is first entered.
                drawMainMenuStatic();
                drawMenuCursor(selectedRow, TRUE);
            }
            else if (selectedRow == OPT_ROW_BACK)
            {
                VDP_setTextPalette(PAL0);
                return;
            }
        }
        else if (pressed & BUTTON_B)
        {
            VDP_setTextPalette(PAL0);
            return;
        }
    }
}
