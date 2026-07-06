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
"""

import os
import subprocess

from PIL import Image, ImageDraw, ImageFont

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
    #
    # 3-row sheet, one row per single-frame animation (neutral / leaning
    # left / leaning right -- see player.c), same convention as enemy_bee()
    # etc. Each lean shears the silhouette (nose shifts one way, engine base
    # the other) so it reads as banking into the turn.
    # Index 4 is the HUD side-panel separator line's color (see
    # hud_separator()), which is drawn with this same palette at runtime.
    pal = [TRANSPARENT] * 16
    pal[1] = (140, 220, 255)   # hull
    pal[2] = (235, 235, 245)   # nose highlight
    pal[3] = (40, 120, 180)    # engine glow accents
    pal[4] = (200, 30, 30)     # HUD separator line (red)
    pal[5] = (0, 0, 0)         # opaque black text background (see banner_font())
    pal[15] = (255, 255, 255) # HUD font ink

    def draw(lean):
        # lean: 0 = neutral, -1 = leaning left, 1 = leaning right.
        img = new_indexed((16, 16), pal)

        for row in range(16):
            half_width = row // 2  # widens going down
            shift = round(lean * (8 - row) / 4)
            cx = 8 + shift
            x0 = max(0, cx - half_width)
            x1 = min(16, cx + half_width + 1)
            fill_rect(img, x0, row, x1, row + 1, 1)

        nose_cx = 8 + round(lean * (8 - 1) / 4)
        fill_rect(img, nose_cx - 1, 0, nose_cx + 1, 3, 2)

        tail_cx = 8 + round(lean * (8 - 14) / 4)
        fill_rect(img, max(0, tail_cx - 6), 13, tail_cx - 3, 16, 3)
        fill_rect(img, tail_cx + 3, 13, min(16, tail_cx + 6), 16, 3)

        return img

    frames = (draw(0), draw(-1), draw(1))  # neutral, lean-left, lean-right
    combined = new_indexed((16, 16 * len(frames)), pal)
    for frame_idx, frame in enumerate(frames):
        for y in range(16):
            for x in range(16):
                set_px(combined, x, y + frame_idx * 16, frame.getpixel((x, y)))
    return combined


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
    # palette 3: terrain AND starfield share this one hardware palette (only
    # 4 exist total, all already spoken for -- see game.h). Only this image's
    # PALETTE resource is ever loaded onto hardware (starfield_tiles.png has
    # no PALETTE declaration of its own), so starfield's star colors are
    # defined here too, at indices 4-5, alongside terrain's own colors at
    # 1-3 -- even though no pixel in *this* image uses 4/5. starfield_tiles()
    # below must use those same indices for its pixels to pick up the right
    # colors instead of terrain's.
    pal = [TRANSPARENT] * 16
    pal[1] = (90, 70, 60)
    pal[2] = (130, 100, 80)
    pal[3] = (60, 45, 40)
    pal[4] = (255, 255, 255)  # star bright (was starfield's own index 1)
    pal[5] = (150, 170, 220)  # star dim (was starfield's own index 2)
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
    # Pixel indices 4/5 (not 1/2!) -- see terrain_tiles()'s comment. This
    # image's own palette is never loaded onto hardware (no PALETTE
    # resource references it), so it only needs to look right for local
    # preview/inspection; what matters at runtime is that these pixel
    # indices match terrain_tiles.png's palette slots.
    pal = [TRANSPARENT] * 6
    pal[4] = (255, 255, 255)
    pal[5] = (150, 170, 220)
    img = new_indexed((24, 8), pal)
    # tile 0: empty space
    # (all index 0)
    # tile 1: single dim star
    set_px(img, 8 + 3, 3, 5)
    # tile 2: couple bright stars
    set_px(img, 16 + 2, 2, 4)
    set_px(img, 16 + 5, 5, 5)
    set_px(img, 16 + 1, 6, 5)
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
        fill_rect(img, 2, 8, 14, 16, 3)   # base
        fill_rect(img, 6, 2, 10, 12, 1)   # barrel, pointing down at the player
        if firing:
            fill_rect(img, 4, 10, 12, 13, 2)  # muzzle flash

    idle = new_indexed((w, h), pal)
    draw(idle, False)

    firing = new_indexed((w, h), pal)
    draw(firing, True)

    flash = new_indexed((w, h), pal)
    draw(flash, False)
    for y in range(h):
        for x in range(w):
            if flash.getpixel((x, y)) != 0:
                set_px(flash, x, y, 4)  # solid white hit-flash, same as enemy_bee() etc

    combined = new_indexed((w, h * 3), pal)
    for frame_idx, frame in enumerate((idle, firing, flash)):
        for y in range(h):
            for x in range(w):
                set_px(combined, x, y + frame_idx * h, frame.getpixel((x, y)))

    return combined


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


def hud_separator():
    # A single tile, solid red, marking the boundary column between the
    # playfield and the HUD side panel (see score.c). Drawn at runtime with
    # PAL_SHIP selected, reusing its index 4 (see player_ship()) -- this
    # tile's own local palette is never loaded onto hardware.
    pal = [TRANSPARENT] * 5
    pal[4] = (200, 30, 30)
    img = new_indexed((8, 8), pal)
    fill_rect(img, 0, 0, 8, 8, 4)
    return img


def banner_font():
    # Drop-in replacement for SGDK's default font (font_default), loaded via
    # VDP_loadFont() (see main.c) -- SGDK's own font tiles have a
    # *transparent* background (index 0) behind the ink, so text drawn with
    # it never has a real opaque backing, just whatever happens to be
    # underneath. This one fills every glyph tile with an actual opaque
    # black (index 5, see player_ship()) first, so VDP_drawText always shows
    # solid black immediately behind each character -- without needing a
    # separate background rectangle painted under a whole block of text.
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
    pal = [TRANSPARENT] * 16
    pal[5] = (0, 0, 0)
    pal[15] = (255, 255, 255)

    font_path = os.path.join(os.path.dirname(__file__), "fonts", "master_512.ttf")
    font = ImageFont.truetype(font_path, 8)

    combined = new_indexed((w * FONT_LEN, h), pal)
    fill_rect(combined, 0, 0, w * FONT_LEN, h, 5)  # includes tile 0 (space) -- opaque too

    for i in range(FONT_LEN):
        ch = chr(32 + i)
        glyph = Image.new("L", (w, h), 0)
        d = ImageDraw.Draw(glyph)
        d.text((0, 0), ch, fill=255, font=font)
        for y in range(h):
            for x in range(w):
                if glyph.getpixel((x, y)) > 128:
                    set_px(combined, i * w + x, y, 15)

    return combined


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
