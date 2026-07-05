"""Generates placeholder sprite/tile PNGs for SGDK's rescomp.

Kept alongside the source PNGs it produces. Re-run after editing this script
to regenerate assets: `python generate_placeholders.py`.

Genesis constraint: sprites/tiles are 4bpp indexed (<=16 colors per
palette), so every image here is saved in "P" (palette) mode with a small
explicit palette. Index 0 is always transparent black (SGDK convention for
sprite transparency).
"""

from PIL import Image, ImageDraw

TRANSPARENT = (0, 0, 0)


def new_indexed(size, palette_rgb):
    """size: (w,h). palette_rgb: list of (r,g,b), index 0 must be TRANSPARENT."""
    img = Image.new("P", size, 0)
    flat = []
    for rgb in palette_rgb:
        flat.extend(rgb)
    # pad palette to 256 entries as PIL requires
    flat.extend([0, 0, 0] * (256 - len(palette_rgb)))
    img.putpalette(flat)
    return img


def set_px(img, x, y, idx):
    img.putpixel((x, y), idx)


def fill_rect(img, x0, y0, x1, y1, idx):
    for y in range(y0, y1):
        for x in range(x0, x1):
            set_px(img, x, y, idx)


def player_ship():
    # palette 0: player/bullets/UI. SGDK's default font (font_default.png)
    # is authored with its ink pixels at color index 15 (not 1!), and
    # VDP_drawText always renders using whichever hardware palette is
    # currently selected as the text palette (PAL_SHIP/PAL0 here, the
    # default). So index 15 is reserved as white for HUD text and left
    # unused by the ship artwork itself.
    pal = [TRANSPARENT] * 16
    pal[1] = (140, 220, 255)   # hull
    pal[2] = (235, 235, 245)   # nose highlight
    pal[3] = (40, 120, 180)    # engine glow accents
    pal[15] = (255, 255, 255) # HUD font ink
    img = new_indexed((16, 16), pal)
    # upward-pointing triangle, cockpit highlight
    for row in range(16):
        half_width = row // 2  # widens going down
        cx = 8
        x0 = cx - half_width
        x1 = cx + half_width + 1
        fill_rect(img, x0, row, x1, row + 1, 1)
    # nose highlight
    fill_rect(img, 7, 0, 9, 3, 2)
    # engine glow accents at the base corners
    fill_rect(img, 2, 13, 5, 16, 3)
    fill_rect(img, 11, 13, 14, 16, 3)
    return img


# palette 1 (PAL_ENEMY): shared by every enemy kind (bee/special/big), since
# only one of their source images (enemy_bee.png, via the PALETTE
# declaration in resources.res) actually gets loaded onto the hardware
# palette -- the others' pixel *indices* must agree with this same mapping
# or their colors come out wrong. Index 4 is reserved solid white, used for
# the hit-flash frame (see enemy.c's enemy_hit()/SPR_setFrame).
ENEMY_PAL = [TRANSPARENT, (255, 90, 90), (255, 200, 90), (140, 30, 30), (255, 255, 255)]


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
            set_px(combined, x, y + h, 4 if v != 0 else 0)
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
            fill_rect(img, x0, row, x1, row + 1, 1)
        fill_rect(img, 6, 6, 10, 10, 2)

    return _enemy_frames(16, 16, draw)


def enemy_special():
    def draw(img):
        cx, cy, r = 8, 8, 7
        for y in range(16):
            for x in range(16):
                d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
                if d2 <= r * r:
                    set_px(img, x, y, 1)
        fill_rect(img, 6, 6, 10, 10, 2)
        fill_rect(img, 0, 7, 16, 9, 3)

    return _enemy_frames(16, 16, draw)


def enemy_big():
    def draw(img):
        # blocky 32x32 "capital ship" silhouette
        fill_rect(img, 2, 4, 30, 28, 3)
        fill_rect(img, 6, 0, 26, 6, 1)
        fill_rect(img, 0, 14, 32, 18, 1)
        fill_rect(img, 12, 8, 20, 22, 2)

    return _enemy_frames(32, 32, draw)


def explosion():
    # 4-frame animation, arranged as a single row (rescomp treats each row as
    # one animation and each column as a frame within it -- see enemy.c's
    # note about SPR_setAnim vs SPR_setFrame). Reuses ENEMY_PAL's colors
    # directly (no hardware palette free to dedicate to it -- all 4 are
    # already used by ship/enemy/powerup/terrain), which happens to already
    # be a fiery red/yellow/white set well-suited to an explosion. Used by
    # both enemy and player deaths (see enemy.c/player.c), drawn with
    # PAL_ENEMY regardless of who died.
    w = h = 16
    cx = cy = 8

    def frame0(img):  # initial bright flash
        for y in range(h):
            for x in range(w):
                d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
                if d2 <= 3 * 3:
                    set_px(img, x, y, 4)
                elif d2 <= 5 * 5:
                    set_px(img, x, y, 2)

    def frame1(img):  # expanding ring
        for y in range(h):
            for x in range(w):
                d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
                if 3 * 3 <= d2 <= 4 * 4:
                    set_px(img, x, y, 4)
                elif 4 * 4 < d2 <= 7 * 7:
                    set_px(img, x, y, 2)

    def frame2(img):  # bigger, dimmer ring
        for y in range(h):
            for x in range(w):
                d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
                if 5 * 5 <= d2 < 6 * 6:
                    set_px(img, x, y, 3)
                elif 6 * 6 <= d2 <= 8 * 8:
                    set_px(img, x, y, 1)

    def frame3(img):  # sparse fading embers
        for (x, y) in [(2, 3), (13, 4), (4, 12), (11, 13), (8, 1), (1, 9), (14, 10), (7, 14)]:
            set_px(img, x, y, 3)

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
    # Index 15 is reserved white for the HUD font's ink, same trick as
    # player_ship() -- VDP_drawImage() loads this palette onto PAL0, which
    # is also the default text palette, and "PRESS START" is drawn with it.
    pal = [TRANSPARENT] * 16
    pal[1] = (90, 160, 255)
    pal[15] = (255, 255, 255)
    img = new_indexed((w, h), pal)
    for y in range(h):
        for x in range(w):
            r = big.getpixel((x, y))[0]
            set_px(img, x, y, 1 if r > 128 else 0)
    return img


def bullet_player():
    pal = [TRANSPARENT, (255, 255, 190)]
    img = new_indexed((8, 8), pal)
    fill_rect(img, 3, 0, 5, 8, 1)
    fill_rect(img, 2, 2, 6, 6, 1)
    return img


def bullet_enemy():
    pal = [TRANSPARENT, (255, 120, 60), (255, 210, 160)]
    img = new_indexed((8, 8), pal)
    cx, cy, r = 3.5, 3.5, 3.5
    for y in range(8):
        for x in range(8):
            d2 = (x - cx) ** 2 + (y - cy) ** 2
            if d2 <= r * r:
                set_px(img, x, y, 1)
    # small bright highlight so it doesn't read as a flat disc
    set_px(img, 3, 3, 2)
    return img


def powerup_spread():
    # palette 2: powerups
    pal = [TRANSPARENT, (90, 255, 140), (230, 255, 230)]
    img = new_indexed((16, 16), pal)
    cx, cy, r = 8, 8, 7
    for y in range(16):
        for x in range(16):
            d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
            if d2 <= r * r:
                set_px(img, x, y, 1)
    # 3-way "spread" glyph
    fill_rect(img, 7, 3, 9, 13, 2)
    fill_rect(img, 3, 7, 13, 9, 2)
    return img


def powerup_speed():
    pal = [TRANSPARENT, (90, 160, 255), (230, 240, 255)]
    img = new_indexed((16, 16), pal)
    cx, cy, r = 8, 8, 7
    for y in range(16):
        for x in range(16):
            d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
            if d2 <= r * r:
                set_px(img, x, y, 1)
    # chevron/arrow glyph
    for row in range(4, 12):
        w = row - 4 if row <= 8 else 11 - row
        fill_rect(img, 8 - w, row, 8 + w + 1, row + 1, 2)
    return img


def terrain_tiles():
    # palette 3: terrain/starfield. 8x8 tiles, sheet is 4 tiles wide (32x8).
    pal = [TRANSPARENT, (90, 70, 60), (130, 100, 80), (60, 45, 40)]
    img = new_indexed((32, 8), pal)
    # tile 0: flat ground
    fill_rect(img, 0, 0, 8, 8, 1)
    fill_rect(img, 0, 0, 8, 2, 2)
    # tile 1: rocky variant
    fill_rect(img, 8, 0, 16, 8, 1)
    fill_rect(img, 9, 2, 12, 4, 3)
    fill_rect(img, 13, 5, 15, 7, 3)
    # tile 2: darker patch
    fill_rect(img, 16, 0, 24, 8, 3)
    fill_rect(img, 16, 0, 24, 2, 1)
    # tile 3: base/structure block
    fill_rect(img, 24, 0, 32, 8, 2)
    fill_rect(img, 25, 1, 31, 7, 1)
    return img


def starfield_tiles():
    pal = [TRANSPARENT, (255, 255, 255), (150, 170, 220)]
    img = new_indexed((24, 8), pal)
    # tile 0: empty space
    # (all index 0)
    # tile 1: single dim star
    set_px(img, 8 + 3, 3, 2)
    # tile 2: couple bright stars
    set_px(img, 16 + 2, 2, 1)
    set_px(img, 16 + 5, 5, 2)
    set_px(img, 16 + 1, 6, 2)
    return img


def hud_fill():
    # A single fully-opaque tile used to back the HUD side panel so the
    # scrolling terrain/starfield and any sprites transiting behind it don't
    # show through. Drawn at runtime with PAL_SHIP selected, so what matters
    # is the pixel *index* (14, arbitrary but non-zero/non-15 so it doesn't
    # collide with the transparent or font-ink colors) -- this tile's own
    # local palette is never loaded onto hardware.
    pal = [TRANSPARENT] * 15
    pal[14] = (0, 0, 0)
    img = new_indexed((8, 8), pal)
    fill_rect(img, 0, 0, 8, 8, 14)
    return img


GENERATORS = {
    "player_ship.png": player_ship,
    "enemy_bee.png": enemy_bee,
    "enemy_special.png": enemy_special,
    "enemy_big.png": enemy_big,
    "explosion.png": explosion,
    "title.png": title_image,
    "bullet_player.png": bullet_player,
    "bullet_enemy.png": bullet_enemy,
    "powerup_spread.png": powerup_spread,
    "powerup_speed.png": powerup_speed,
    "terrain_tiles.png": terrain_tiles,
    "starfield_tiles.png": starfield_tiles,
    "hud_fill.png": hud_fill,
}

if __name__ == "__main__":
    for filename, gen in GENERATORS.items():
        img = gen()
        img.save(filename)
        print(f"wrote {filename} ({img.size[0]}x{img.size[1]})")
