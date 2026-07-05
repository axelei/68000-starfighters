"""Generates placeholder sprite/tile PNGs for SGDK's rescomp.

Kept alongside the source PNGs it produces. Re-run after editing this script
to regenerate assets: `python generate_placeholders.py`.

Genesis constraint: sprites/tiles are 4bpp indexed (<=16 colors per
palette), so every image here is saved in "P" (palette) mode with a small
explicit palette. Index 0 is always transparent black (SGDK convention for
sprite transparency).
"""

from PIL import Image

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
    # palette 0: player/bullets/UI
    pal = [TRANSPARENT, (80, 220, 255), (220, 245, 255), (40, 120, 180)]
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


def enemy_bee():
    # palette 1: enemies
    pal = [TRANSPARENT, (255, 90, 90), (255, 200, 90), (140, 30, 30)]
    img = new_indexed((16, 16), pal)
    # downward-pointing diamond
    for row in range(16):
        dist_from_mid = abs(row - 7.5)
        half_width = int(8 - dist_from_mid)
        cx = 8
        x0 = max(0, cx - half_width)
        x1 = min(16, cx + half_width)
        fill_rect(img, x0, row, x1, row + 1, 1)
    fill_rect(img, 6, 6, 10, 10, 2)
    return img


def enemy_special():
    # palette 1: enemies (bright, distinct from grunt so it reads as "special")
    pal = [TRANSPARENT, (200, 90, 255), (255, 230, 120), (110, 30, 160)]
    img = new_indexed((16, 16), pal)
    cx, cy, r = 8, 8, 7
    for y in range(16):
        for x in range(16):
            d2 = (x - cx + 0.5) ** 2 + (y - cy + 0.5) ** 2
            if d2 <= r * r:
                set_px(img, x, y, 1)
    fill_rect(img, 6, 6, 10, 10, 2)
    fill_rect(img, 0, 7, 16, 9, 3)
    return img


def bullet_player():
    pal = [TRANSPARENT, (255, 255, 190)]
    img = new_indexed((8, 8), pal)
    fill_rect(img, 3, 0, 5, 8, 1)
    fill_rect(img, 2, 2, 6, 6, 1)
    return img


def bullet_enemy():
    pal = [TRANSPARENT, (255, 120, 60)]
    img = new_indexed((8, 8), pal)
    fill_rect(img, 3, 0, 5, 8, 1)
    fill_rect(img, 2, 2, 6, 6, 1)
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


GENERATORS = {
    "player_ship.png": player_ship,
    "enemy_bee.png": enemy_bee,
    "enemy_special.png": enemy_special,
    "bullet_player.png": bullet_player,
    "bullet_enemy.png": bullet_enemy,
    "powerup_spread.png": powerup_spread,
    "powerup_speed.png": powerup_speed,
    "terrain_tiles.png": terrain_tiles,
    "starfield_tiles.png": starfield_tiles,
}

if __name__ == "__main__":
    for filename, gen in GENERATORS.items():
        img = gen()
        img.save(filename)
        print(f"wrote {filename} ({img.size[0]}x{img.size[1]})")
