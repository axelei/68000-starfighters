"""Generates placeholder sprite/tile PNGs for SGDK's rescomp.

Kept alongside the source PNGs it produces. Re-run after editing this script
to regenerate assets:
    python generate_placeholders.py                  # everything
    python generate_placeholders.py turret hud_fill   # just these (name or name.png)
    python generate_placeholders.py --force turret    # overwrite even if uncommitted

Genesis constraint: sprites/tiles are 4bpp indexed (<=16 colors per
palette), so every image here is saved in "P" (palette) mode with a small
explicit palette. Index 0 is always transparent black (SGDK convention for
sprite transparency).

Palette layout: the game consolidates down to 3 permanent hardware
palettes (PAL_PLAYER/PAL_ENEMY/PAL_ENVIRONMENT -- see game.h) plus a 4th,
TITLE_PAL, that only exists briefly on PAL0 during the title screen before
title.c's title_fadeInGame() overwrites it with PAL_PLAYER's real colors.
Every one of these 4 fills all 16 slots -- indices 0-3 are always
transparent/black/white/gray (the same across all 4, so text/HUD tiles that
only use black+white render correctly regardless of which palette happens
to be loaded), and 4-15 are light/base/dark shades of that group's colors
instead of leaving most of the palette unused.
"""

import os
import subprocess

from PIL import Image, ImageDraw, ImageFont

# Index 0 is only transparent for SPRITE resources -- the VDP has no
# transparency concept for background planes, so every TILESET here
# (terrain/starfield/hud_fill/hud_separator/banner_font) actually renders
# whatever RGB is stored at index 0 wherever a tile leaves it blank (empty
# terrain, starfield gaps, the font's cell background, etc). A previous
# high-visibility magenta here (meant only to make sprite transparency
# obvious when previewing the PNGs) was therefore showing up as a real
# bright-pink background in-game. Must stay black.
TRANSPARENT = (0, 0, 0)
BLACK = (1, 1, 1)  # not literal (0,0,0)/TRANSPARENT -- see the note in banner_font()
WHITE = (255, 255, 255)
GRAY = (128, 128, 128)


def shade(rgb, factor):
    """factor > 1 lightens toward white, < 1 darkens toward black."""
    r, g, b = rgb
    if factor >= 1:
        t = factor - 1
        return (
            int(r + (255 - r) * t),
            int(g + (255 - g) * t),
            int(b + (255 - b) * t),
        )
    return (int(r * factor), int(g * factor), int(b * factor))


def triple(rgb):
    """(light, base, dark) shades of one hue."""
    return [shade(rgb, 1.5), rgb, shade(rgb, 0.55)]


def common4():
    return [TRANSPARENT, BLACK, WHITE, GRAY]


def new_indexed(size, palette_rgb):
    """size: (w,h). palette_rgb: list of (r,g,b), index 0 must be TRANSPARENT."""
    img = Image.new("P", size, 0)
    flat = []
    for rgb in palette_rgb:
        flat.extend(rgb)
    # pad palette to 256 entries as PIL requires
    flat.extend([0, 0, 0] * (256 - len(palette_rgb)))
    img.putpalette(flat)
    # Marks index 0 as genuinely transparent in the saved PNG's tRNS chunk
    # (honored automatically by Image.save()) -- SGDK/rescomp already treats
    # index 0 as transparent by convention regardless of this, but without
    # it the PNG's stored RGB (see TRANSPARENT's high-visibility color,
    # chosen for previewing) would show as a solid fill in normal image
    # viewers instead of looking transparent there too.
    img.info["transparency"] = 0
    return img


def set_px(img, x, y, idx):
    img.putpixel((x, y), idx)


def fill_rect(img, x0, y0, x1, y1, idx):
    for y in range(y0, y1):
        for x in range(x0, x1):
            set_px(img, x, y, idx)


# -- PAL_PLAYER: ship, player bullet, HUD (separator/fill/font), powerups --
# (only palette_player.png -- i.e. player_ship() below -- is ever loaded onto
# hardware; every other image drawn with PAL_PLAYER at runtime must use these
# same indices for its pixels to pick up the right colors.)
HULL = (140, 220, 255)
# Not too close to pure white (255,255,255, index 2) -- the Genesis's CRAM
# only holds 3 bits/channel (8 levels), and (235,235,245) rounds to exactly
# the same hardware color as white, wasting this slot. Verified distinct
# via res/gfx's quantization check (see the module docstring).
NOSE_WHITE = (180, 205, 240)
SEPARATOR_RED = (200, 30, 30)
BULLET_YELLOW = (255, 255, 190)
POWERUP_GREEN = (90, 255, 140)
POWERUP_BLUE = (90, 160, 255)

PLAYER_PAL = common4() + [
    *triple(HULL),          # 4 light, 5 base (hull), 6 dark (also: engine glow)
    NOSE_WHITE,              # 7
    *triple(SEPARATOR_RED),  # 8 light, 9 base (HUD separator), 10 dark
    BULLET_YELLOW,           # 11
    *triple(POWERUP_GREEN),  # 12 light (also: spread-powerup glyph), 13 base, 14 dark
    POWERUP_BLUE,            # 15 (speed powerup)
]

# -- PAL_ENEMY: bee/special/big, turret, explosion, enemy bullet, wavers --
# (only palette_enemy.png -- enemy_bee() below -- is ever loaded onto
# hardware; same "shared indices" rule as PLAYER_PAL above.)
BEE_RED = (255, 90, 90)
BEE_ACCENT = (255, 200, 90)
WAVER_A_CYAN = (90, 220, 255)
WAVER_B_PURPLE = (200, 110, 255)
WAVER_C_GREEN = (120, 255, 140)
ENEMY_BULLET_ORANGE = (255, 120, 60)

ENEMY_PAL = common4() + [
    *triple(BEE_RED),                  # 4 light, 5 base (hull), 6 dark (shadow)
    BEE_ACCENT,                         # 7 (also: turret muzzle flash, explosion ring)
    shade(WAVER_A_CYAN, 1.5),           # 8 waver A light
    WAVER_A_CYAN,                       # 9 waver A base
    WAVER_B_PURPLE,                     # 10 waver B
    WAVER_C_GREEN,                      # 11 waver C
    ENEMY_BULLET_ORANGE,                # 12
    shade(ENEMY_BULLET_ORANGE, 1.4),    # 13 (highlight)
    shade(BEE_RED, 0.35),               # 14 extra-dark shadow
    shade(WAVER_A_CYAN, 0.5),           # 15 waver A dark
]

# -- PAL_ENVIRONMENT: terrain + starfield --
# (only palette_environment.png -- terrain_tiles() below -- is ever loaded
# onto hardware; starfield_tiles() has no PALETTE of its own and must use
# these same indices.)
TERRAIN_BROWN = (90, 70, 60)
TERRAIN_ACCENT = (130, 100, 80)
STAR_DIM = (150, 170, 220)

ENVIRONMENT_PAL = common4() + [
    *triple(TERRAIN_BROWN),         # 4 light, 5 base, 6 dark
    TERRAIN_ACCENT,                  # 7
    shade(TERRAIN_BROWN, 0.4),       # 8 darkest brown
    shade(TERRAIN_ACCENT, 1.3),      # 9 light rock
    *triple(STAR_DIM),               # 10 light, 11 base (star dim), 12 dark
    shade(STAR_DIM, 1.3),             # 13 near-white star (bright stars use common WHITE=2 instead)
    (140, 145, 160),                  # 14 light rock-gray -- not shade(GRAY, 1.3): that quantized
                                       # to the same hardware color as index 4 (light terrain brown)
    shade(GRAY, 0.6),                 # 15 dark rock-gray
]

# -- PAL_BOSS (PAL3): boss body/weak-spots, boss's homing bullet --
# (only palette_boss.png -- i.e. boss_body_tl() below -- is ever loaded onto
# hardware; every other boss-related image must use these same indices.)
BOSS_HULL = (200, 40, 40)
WEAKSPOT_BASE = (255, 190, 60)
WEAKSPOT_HIT = (255, 50, 190)  # dedicated hit-flash color, distinct from the
                                # generic white hit-flash every other enemy
                                # uses -- see boss_weakspot()'s comment.
WEAKSPOT_DEAD = (70, 70, 70)
BOSS_PANEL = (110, 15, 15)

BOSS_PAL = common4() + [
    *triple(BOSS_HULL),         # 4 light, 5 base (hull), 6 dark
    shade(BOSS_HULL, 0.4),      # 7 darkest hull shadow/panel line
    *triple(WEAKSPOT_BASE),     # 8 light, 9 base (weak spot normal), 10 dark
    WEAKSPOT_HIT,               # 11 weak spot / homing-bullet hit-flash
    WEAKSPOT_DEAD,              # 12 destroyed weak-spot husk
    BOSS_PANEL,                 # 13 dark red trim/panel accent
    shade(BOSS_HULL, 1.7),      # 14 bright red highlight (also: homing bullet core)
    shade(WEAKSPOT_BASE, 0.5),  # 15 dark amber
]

# -- title screen only (see title.c) -- briefly loaded onto PAL0, then
# overwritten by PAL_PLAYER's real colors once gameplay starts.
TITLE_BLUE = (90, 160, 255)
TITLE_GOLD = (255, 200, 80)

TITLE_PAL = common4() + [
    *triple(TITLE_BLUE),          # 4,5,6
    *triple(TITLE_GOLD),          # 7,8,9
    shade(TITLE_BLUE, 0.4),       # 10
    (100, 78, 25),                # 11 -- not shade(TITLE_GOLD, 0.5): too close to index 9
                                   # (shade(TITLE_GOLD, 0.55)), same hardware color once quantized
    shade(GRAY, 1.3),             # 12
    shade(GRAY, 0.6),             # 13
    shade(TITLE_BLUE, 1.7),       # 14
    shade(TITLE_GOLD, 1.7),       # 15
]


def player_ship():
    # 3-row sheet, one row per single-frame animation (neutral / leaning
    # left / leaning right -- see player.c), same convention as enemy_bee()
    # etc. Each lean shears the silhouette (nose shifts one way, engine base
    # the other) so it reads as banking into the turn.
    pal = PLAYER_PAL

    def draw(lean):
        # lean: 0 = neutral, -1 = leaning left, 1 = leaning right.
        img = new_indexed((16, 16), pal)

        for row in range(16):
            half_width = row // 2  # widens going down
            shift = round(lean * (8 - row) / 4)
            cx = 8 + shift
            x0 = max(0, cx - half_width)
            x1 = min(16, cx + half_width + 1)
            fill_rect(img, x0, row, x1, row + 1, 5)  # hull

        nose_cx = 8 + round(lean * (8 - 1) / 4)
        fill_rect(img, nose_cx - 1, 0, nose_cx + 1, 3, 7)  # nose highlight

        tail_cx = 8 + round(lean * (8 - 14) / 4)
        fill_rect(img, max(0, tail_cx - 6), 13, tail_cx - 3, 16, 6)  # engine glow
        fill_rect(img, tail_cx + 3, 13, min(16, tail_cx + 6), 16, 6)

        return img

    frames = (draw(0), draw(-1), draw(1))  # neutral, lean-left, lean-right
    combined = new_indexed((16, 16 * len(frames)), pal)
    for frame_idx, frame in enumerate(frames):
        for y in range(16):
            for x in range(16):
                set_px(combined, x, y + frame_idx * 16, frame.getpixel((x, y)))
    return combined


def _enemy_frames(w, h, draw_fn):
    """Builds a 2-row sprite sheet for rescomp's SPRITE resource, where each
    *row* is a separate single-frame animation (row 0 = normal, row 1 = the
    same silhouette lit up solid white) -- selected at runtime via
    SPR_setAnim() for the enemy hit-flash effect (see enemy.c)."""
    normal = new_indexed((w, h), ENEMY_PAL)
    draw_fn(normal)

    combined = new_indexed((w, h * 2), ENEMY_PAL)
    for y in range(h):
        for x in range(w):
            v = normal.getpixel((x, y))
            set_px(combined, x, y, v)
            set_px(combined, x, y + h, 2 if v != 0 else 0)  # 2 = common WHITE
    return combined


def enemy_bee():
    def draw(img):
        # downward-pointing diamond
        for row in range(16):
            dist_from_mid = abs(row - 7.5)
            half_width = int(8 - dist_from_mid)
            cx = 8
            x0 = max(0, cx - half_width)
            x1 = min(16, cx + half_width)
            fill_rect(img, x0, row, x1, row + 1, 5)  # hull
        fill_rect(img, 6, 6, 10, 10, 7)  # accent

    return _enemy_frames(16, 16, draw)


def enemy_special():
    def draw(img):
        cx, cy, r = 8, 8, 7
        for y in range(16):
            for x in range(16):
                d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
                if d2 <= r * r:
                    set_px(img, x, y, 5)  # hull
        fill_rect(img, 6, 6, 10, 10, 7)  # accent
        fill_rect(img, 0, 7, 16, 9, 6)   # shadow band

    return _enemy_frames(16, 16, draw)


def enemy_big():
    def draw(img):
        # blocky 32x32 "capital ship" silhouette
        fill_rect(img, 2, 4, 30, 28, 6)   # shadow
        fill_rect(img, 6, 0, 26, 6, 5)    # hull
        fill_rect(img, 0, 14, 32, 18, 5)  # hull
        fill_rect(img, 12, 8, 20, 22, 7)  # accent

    return _enemy_frames(32, 32, draw)


def enemy_waver_a():
    # Upward chevron/arrow -- the inter-wave "waver" kinds (see enemy.c's
    # ENEMY_STATE_WAVING) are visually a distinct family from bee/special/big,
    # reusing ENEMY_PAL rather than a separate hardware palette.
    def draw(img):
        for row in range(16):
            half_width = row // 2 + 1
            cx = 8
            fill_rect(img, max(0, cx - half_width), row, min(16, cx + half_width), row + 1, 9)

    return _enemy_frames(16, 16, draw)


def enemy_waver_b():
    # Hexagon-ish blob.
    def draw(img):
        cx, cy = 8, 8
        for y in range(16):
            for x in range(16):
                dx, dy = abs(x - cx + 0.5), abs(y - cy + 0.5)
                if dx * 0.6 + dy <= 8:
                    set_px(img, x, y, 10)

    return _enemy_frames(16, 16, draw)


def enemy_waver_c():
    # Small cross/plus shape.
    def draw(img):
        fill_rect(img, 6, 1, 10, 15, 11)
        fill_rect(img, 1, 6, 15, 10, 11)

    return _enemy_frames(16, 16, draw)


def boss_body():
    # 2-row sheet (top-left quadrant / bottom-left quadrant), one shared
    # PNG rather than two separate files -- boss.c creates all 4 of the
    # body's Sprite objects from this single SpriteDefinition, picking
    # frame 0 (TL) or frame 1 (BL) via SPR_setVRAMTileIndex, and mirroring
    # each horizontally for the right-hand quadrants (TR/BR) instead of
    # drawing separate right-side art -- the whole 64x64 silhouette only
    # needs to be drawn once, left-right-symmetric, right here.
    w = h = 64
    full = new_indexed((w, h), BOSS_PAL)
    cx, cy = 32, 32
    for y in range(h):
        for x in range(w):
            dx, dy = abs(x - cx + 0.5), abs(y - cy + 0.5)
            if dx * 0.9 + dy * 0.6 <= 30:  # broad diamond-ish hull
                set_px(full, x, y, 5)  # hull base
    fill_rect(full, 4, 30, 60, 34, 7)    # dark panel band across the middle
    fill_rect(full, 26, 2, 38, 10, 14)   # bright nose highlight
    fill_rect(full, 10, 44, 54, 48, 13)  # dark red trim, lower hull

    combined = new_indexed((32, 64), BOSS_PAL)
    for y in range(64):
        for x in range(32):
            set_px(combined, x, y, full.getpixel((x, y)))
    return combined


def boss_weakspot():
    # 3-row sheet, one row per single-frame animation (normal / hit-flash /
    # destroyed husk) -- same multi-row convention as _enemy_frames()/
    # turret(), just 3 rows instead of 2. The hit-flash uses a dedicated
    # color (WEAKSPOT_HIT) rather than the generic white every other enemy
    # flashes to, per the design decision that the boss's own palette should
    # visibly call out a hit.
    w = h = 16
    pal = BOSS_PAL

    normal = new_indexed((w, h), pal)
    cx, cy, r = 8, 8, 7
    for y in range(h):
        for x in range(w):
            d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
            if d2 <= r * r:
                set_px(normal, x, y, 9)  # weak spot base (amber)
    fill_rect(normal, 6, 6, 10, 10, 8)   # light core

    flash = new_indexed((w, h), pal)
    destroyed = new_indexed((w, h), pal)
    for y in range(h):
        for x in range(w):
            if normal.getpixel((x, y)) != 0:
                set_px(flash, x, y, 11)      # dedicated hit-flash color
                set_px(destroyed, x, y, 12)  # husk gray

    combined = new_indexed((w, h * 3), pal)
    for frame_idx, frame in enumerate((normal, flash, destroyed)):
        for y in range(h):
            for x in range(w):
                set_px(combined, x, y + frame_idx * h, frame.getpixel((x, y)))
    return combined


def bullet_homing():
    # 2-row sheet (normal / hit-flash), 16x16 -- deliberately bigger and more
    # distinct than the 8x8 bullet_enemy() disc, so the player can recognize
    # it as a shootable threat rather than a normal bullet. Shares BOSS_PAL
    # (it's only ever fired by the boss) rather than ENEMY_PAL.
    w = h = 16
    pal = BOSS_PAL

    normal = new_indexed((w, h), pal)
    cx, cy, r = 8, 8, 6
    for y in range(h):
        for x in range(w):
            d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
            if d2 <= r * r:
                set_px(normal, x, y, 5)  # boss hull red glow
    fill_rect(normal, 6, 6, 10, 10, 14)  # bright core

    flash = new_indexed((w, h), pal)
    for y in range(h):
        for x in range(w):
            if normal.getpixel((x, y)) != 0:
                set_px(flash, x, y, 11)  # same dedicated hit-flash color as weak spots

    combined = new_indexed((w, h * 2), pal)
    for frame_idx, frame in enumerate((normal, flash)):
        for y in range(h):
            for x in range(w):
                set_px(combined, x, y + frame_idx * h, frame.getpixel((x, y)))
    return combined


def explosion():
    # 4-frame animation, arranged as a single row (rescomp treats each row as
    # one animation and each column as a frame within it -- see enemy.c's
    # note about SPR_setAnim vs SPR_setFrame). Reuses ENEMY_PAL's colors
    # (a fiery red/orange/white set well-suited to an explosion already).
    # Used by both enemy and player deaths (see enemy.c/player.c), drawn with
    # PAL_ENEMY regardless of who died.
    w = h = 16
    cx = cy = 8

    def frame0(img):  # initial bright flash
        for y in range(h):
            for x in range(w):
                d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
                if d2 <= 3 * 3:
                    set_px(img, x, y, 2)  # white
                elif d2 <= 5 * 5:
                    set_px(img, x, y, 7)  # accent

    def frame1(img):  # expanding ring
        for y in range(h):
            for x in range(w):
                d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
                if 3 * 3 <= d2 <= 4 * 4:
                    set_px(img, x, y, 2)  # white
                elif 4 * 4 < d2 <= 7 * 7:
                    set_px(img, x, y, 7)  # accent

    def frame2(img):  # bigger, dimmer ring
        for y in range(h):
            for x in range(w):
                d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
                if 5 * 5 <= d2 < 6 * 6:
                    set_px(img, x, y, 6)  # dark
                elif 6 * 6 <= d2 <= 8 * 8:
                    set_px(img, x, y, 5)  # hull

    def frame3(img):  # sparse fading embers
        for (x, y) in [(2, 3), (13, 4), (4, 12), (11, 13), (8, 1), (1, 9), (14, 10), (7, 14)]:
            set_px(img, x, y, 6)  # dark

    frames = []
    for fn in (frame0, frame1, frame2, frame3):
        img = new_indexed((w, h), ENEMY_PAL)
        fn(img)
        frames.append(img)

    combined = new_indexed((w * len(frames), h), ENEMY_PAL)
    for i, fr in enumerate(frames):
        for y in range(h):
            for x in range(w):
                set_px(combined, i * w + x, y, fr.getpixel((x, y)))
    return combined


def title_image():
    # Title-screen logo (an IMAGE resource, not a sprite -- see title.c).
    # Drawn with PIL's built-in bitmap font at native (tiny) size, then
    # scaled up with NEAREST so it reads as chunky pixel-art text instead of
    # being blurry. 80x24 native * 4 = 320x96 (40x12 tiles), exactly the
    # screen's width.
    scale = 4
    small = Image.new("RGB", (80, 24), (0, 0, 0))
    d = ImageDraw.Draw(small)
    d.text((2, 0), "68000", fill=(255, 255, 255))
    d.text((2, 12), "STARFIGHTERS", fill=(255, 255, 255))
    big = small.resize((80 * scale, 24 * scale), Image.NEAREST)

    w, h = big.size
    pal = TITLE_PAL
    img = new_indexed((w, h), pal)
    for y in range(h):
        for x in range(w):
            r = big.getpixel((x, y))[0]
            set_px(img, x, y, 5 if r > 128 else 0)  # logo blue base
    return img


def bullet_player():
    pal = PLAYER_PAL
    img = new_indexed((8, 8), pal)
    fill_rect(img, 3, 0, 5, 8, 11)  # bullet yellow
    fill_rect(img, 2, 2, 6, 6, 11)
    return img


def bullet_enemy():
    pal = ENEMY_PAL
    img = new_indexed((8, 8), pal)
    cx, cy, r = 3.5, 3.5, 3.5
    for y in range(8):
        for x in range(8):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if d2 <= r * r:
                set_px(img, x, y, 12)  # enemy bullet orange
    set_px(img, 3, 3, 13)  # highlight, so it doesn't read as a flat disc
    return img


def powerup_spread():
    pal = PLAYER_PAL
    img = new_indexed((16, 16), pal)
    cx, cy, r = 8, 8, 7
    for y in range(16):
        for x in range(16):
            d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
            if d2 <= r * r:
                set_px(img, x, y, 13)  # powerup green base
    # 3-way "spread" glyph
    fill_rect(img, 7, 3, 9, 13, 12)  # green light
    fill_rect(img, 3, 7, 13, 9, 12)
    return img


def powerup_speed():
    pal = PLAYER_PAL
    img = new_indexed((16, 16), pal)
    cx, cy, r = 8, 8, 7
    for y in range(16):
        for x in range(16):
            d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
            if d2 <= r * r:
                set_px(img, x, y, 15)  # powerup blue
    # chevron/arrow glyph
    for row in range(4, 12):
        w = row - 4 if row <= 8 else 11 - row
        fill_rect(img, 8 - w, row, 8 + w + 1, row + 1, 2)  # white
    return img


def terrain_tiles():
    # Terrain AND starfield share this one hardware palette (PAL_ENVIRONMENT
    # -- see game.h). Only this image's PALETTE resource is ever loaded onto
    # hardware (starfield_tiles.png has no PALETTE declaration of its own),
    # so starfield's star colors are defined here too (indices 2=common
    # WHITE for bright stars, 11=star dim), alongside terrain's own colors
    # at 4-9 -- even though no pixel in *this* image uses 2/11.
    # starfield_tiles() below must use those same indices.
    pal = ENVIRONMENT_PAL
    img = new_indexed((32, 8), pal)
    # tile 0: flat ground
    fill_rect(img, 0, 0, 8, 8, 5)   # base
    fill_rect(img, 0, 0, 8, 2, 7)   # accent (top edge)
    # tile 1: rocky variant
    fill_rect(img, 8, 0, 16, 8, 5)   # base
    fill_rect(img, 9, 2, 12, 4, 6)   # dark rocks
    fill_rect(img, 13, 5, 15, 7, 6)
    # tile 2: darker patch
    fill_rect(img, 16, 0, 24, 8, 6)  # dark
    fill_rect(img, 16, 0, 24, 2, 5)  # base (top edge)
    # tile 3: base/structure block
    fill_rect(img, 24, 0, 32, 8, 7)  # accent
    fill_rect(img, 25, 1, 31, 7, 5)  # base
    return img


def starfield_tiles():
    # Pixel indices 2 (common WHITE)/11 (star dim) -- see terrain_tiles()'s
    # comment. This image's own palette is never loaded onto hardware (no
    # PALETTE resource references it), so it only needs to look right for
    # local preview/inspection; what matters at runtime is that these pixel
    # indices match terrain_tiles.png's palette slots.
    pal = ENVIRONMENT_PAL
    img = new_indexed((24, 8), pal)
    # tile 0: empty space
    # (all index 0)
    # tile 1: single dim star
    set_px(img, 8 + 3, 3, 11)
    # tile 2: couple bright stars
    set_px(img, 16 + 2, 2, 2)
    set_px(img, 16 + 5, 5, 11)
    set_px(img, 16 + 1, 6, 11)
    return img


def turret():
    # Ground turret (see turret.c): shares ENEMY_PAL with the rest of the
    # enemies (bee/special/big/explosion) rather than having its own
    # palette, even though it's terrain-attached rather than a formation
    # enemy. 3-row sheet, one row per single-frame animation (idle / firing
    # / hit-flash), same convention as enemy_bee() etc -- selected at
    # runtime via a shared VRAM tile index (see turret.c), not SPR_setAnim.
    w = h = 16
    pal = ENEMY_PAL

    def draw(img, firing):
        fill_rect(img, 2, 8, 14, 16, 6)   # base (dark)
        fill_rect(img, 6, 2, 10, 12, 5)   # barrel (hull), pointing down at the player
        if firing:
            fill_rect(img, 4, 10, 12, 13, 7)  # muzzle flash (accent)

    idle = new_indexed((w, h), pal)
    draw(idle, False)

    firing = new_indexed((w, h), pal)
    draw(firing, True)

    flash = new_indexed((w, h), pal)
    draw(flash, False)
    for y in range(h):
        for x in range(w):
            if flash.getpixel((x, y)) != 0:
                set_px(flash, x, y, 2)  # solid white hit-flash, same as enemy_bee() etc

    combined = new_indexed((w, h * 3), pal)
    for frame_idx, frame in enumerate((idle, firing, flash)):
        for y in range(h):
            for x in range(w):
                set_px(combined, x, y + frame_idx * h, frame.getpixel((x, y)))

    return combined


def hud_fill():
    # A single fully-opaque tile used to back the HUD side panel so the
    # scrolling terrain/starfield and any sprites transiting behind it don't
    # show through. Drawn at runtime with PAL_PLAYER selected, so what
    # matters is the pixel *index* -- 1 (common BLACK) -- this tile's own
    # local palette is never loaded onto hardware.
    pal = PLAYER_PAL
    img = new_indexed((8, 8), pal)
    fill_rect(img, 0, 0, 8, 8, 1)
    return img


def hud_separator():
    # A single tile, solid red, marking the boundary column between the
    # playfield and the HUD side panel (see score.c). Drawn at runtime with
    # PAL_PLAYER selected, reusing its index 9 (see PLAYER_PAL) -- this
    # tile's own local palette is never loaded onto hardware.
    pal = PLAYER_PAL
    img = new_indexed((8, 8), pal)
    fill_rect(img, 0, 0, 8, 8, 9)
    return img


def banner_font():
    # Drop-in replacement for SGDK's default font (font_default), loaded via
    # VDP_loadFont() (see main.c) -- SGDK's own font tiles have a
    # *transparent* background (index 0) behind the ink, so text drawn with
    # it never has a real opaque backing, just whatever happens to be
    # underneath. This one fills every glyph tile with an actual opaque
    # black (index 1, common BLACK) first, so VDP_drawText always shows
    # solid black immediately behind each character -- without needing a
    # separate background rectangle painted under a whole block of text.
    # Index 1/2 (black/white) are the same across every one of this game's
    # palettes (see the module docstring), so this renders correctly
    # whichever one happens to be loaded on PAL0 (title's, briefly, or the
    # player's for the rest of the game).
    #
    # Space (tile 0) also gets the opaque black background -- a transparent
    # space would leave a gap in the middle of banner text (e.g. between
    # "GAME" and "OVER") showing sprites/starfield through it instead of a
    # solid backing. VDP_clearTextArea() does "clear" a region by filling it
    # with this tile (see vdp_bg.c), so main.c's one call to it at round-end
    # now paints solid black instead of truly clearing -- harmless, since
    # that call runs after the screen's already faded to black and the
    # WINDOW plane is restricted back to its normal narrow bands before
    # anything is shown again (see score_init()).
    #
    # Glyphs come from "Master 512" (fonts/master_512.ttf), an authentic
    # 8x8 oldschool PC bitmap font from VileR's Ultimate Oldschool PC Font
    # Pack (http://int10h.org, CC BY-SA 4.0 -- see fonts/CREDITS.txt),
    # rather than PIL's own tiny default font, which rendered illegibly at
    # this size. It's a genuine 8x8 bitmap strike (not an outline font
    # scaled down), so requesting size=8 reproduces its pixels exactly, no
    # anti-aliasing/interpolation.
    #
    # Covers the same FONT_LEN=96 characters (ASCII 32..127) at the same 8x8
    # tile size SGDK's own font does, one tile per character in order, so it
    # can be loaded as a straight replacement.
    FONT_LEN = 96
    w = h = 8
    pal = PLAYER_PAL

    font_path = os.path.join(os.path.dirname(__file__), "fonts", "master_512.ttf")
    font = ImageFont.truetype(font_path, 8)

    combined = new_indexed((w * FONT_LEN, h), pal)
    fill_rect(combined, 0, 0, w * FONT_LEN, h, 1)  # includes tile 0 (space) -- opaque too

    for i in range(FONT_LEN):
        ch = chr(32 + i)
        glyph = Image.new("L", (w, h), 0)
        d = ImageDraw.Draw(glyph)
        d.text((0, 0), ch, fill=255, font=font)
        for y in range(h):
            for x in range(w):
                if glyph.getpixel((x, y)) > 128:
                    set_px(combined, i * w + x, y, 2)  # common WHITE

    return combined


GENERATORS = {
    "player_ship.png": player_ship,
    "enemy_bee.png": enemy_bee,
    "enemy_special.png": enemy_special,
    "enemy_big.png": enemy_big,
    "enemy_waver_a.png": enemy_waver_a,
    "enemy_waver_b.png": enemy_waver_b,
    "enemy_waver_c.png": enemy_waver_c,
    "boss_body.png": boss_body,
    "boss_weakspot.png": boss_weakspot,
    "bullet_homing.png": bullet_homing,
    "explosion.png": explosion,
    "title.png": title_image,
    "bullet_player.png": bullet_player,
    "bullet_enemy.png": bullet_enemy,
    "powerup_spread.png": powerup_spread,
    "powerup_speed.png": powerup_speed,
    "terrain_tiles.png": terrain_tiles,
    "starfield_tiles.png": starfield_tiles,
    "turret.png": turret,
    "hud_fill.png": hud_fill,
    "hud_separator.png": hud_separator,
    "banner_font.png": banner_font,
}

def git_is_dirty(path):
    """True if `path` is untracked or has uncommitted changes (staged or
    not) -- i.e. regenerating it now would silently destroy something not
    yet safely recorded in git history."""
    try:
        result = subprocess.run(
            ["git", "status", "--porcelain", "--", path],
            capture_output=True, text=True, check=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        # Not a git repo (or git unavailable) -- nothing to protect against.
        return False
    return bool(result.stdout.strip())


if __name__ == "__main__":
    import sys

    args = sys.argv[1:]
    force = "--force" in args
    names = [a for a in args if not a.startswith("--")]

    if names:
        targets = {}
        for name in names:
            filename = name if name.endswith(".png") else name + ".png"
            if filename not in GENERATORS:
                raise SystemExit(f"unknown target '{name}' -- available: {', '.join(sorted(GENERATORS))}")
            targets[filename] = GENERATORS[filename]
    else:
        targets = GENERATORS

    for filename, gen in targets.items():
        if not force and os.path.exists(filename) and git_is_dirty(filename):
            print(f"skipped {filename}: uncommitted changes -- commit/discard them or pass --force to overwrite")
            continue

        img = gen()
        img.save(filename)
        print(f"wrote {filename} ({img.size[0]}x{img.size[1]})")
