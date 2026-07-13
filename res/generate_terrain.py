"""Pregenerates random terrain/starfield "half bands" into src/terrain_generated.h.

terrain.c used to roll clump shapes and starfield stars at runtime, every
time a half of the scrolling plane needed to be varied -- cheap as a single
call, but each reroll meant thousands of random()+VDP_setTileMapXY calls
(one full clump/star scan per cell). Rolling a fixed set of variations here
instead, once, at build time, means terrain.c only has to pick one at random
and stamp its (small, precomputed) clump/star list -- no per-cell random()
calls left at runtime at all.

Re-run after changing any of the constants below (must also be kept in sync
with terrain.c's own copies -- see the comments there):
    python generate_terrain.py
"""

import math
import os
import random

SEED = 20240521  # fixed, so regenerating without changing anything is a no-op

# -- must match terrain.c --
BAND_ROWS = 32               # PLANE_H_TILES / 2
VISIBLE_COLS = 40            # SCREEN_W / 8 -- columns past this never scroll into view
CLUMP_SPACING = 19  # must stay > CLUMP_MAX_SIZE (anchors packed as tight as that allows)
CLUMP_MAX_SIZE = 18
ANCHORS_PER_BAND = (VISIBLE_COLS + CLUMP_SPACING - 1) // CLUMP_SPACING
TERRAIN_BASE_TILE = 16       # TILE_USER_INDEX
STARFIELD_BASE_TILE = 32     # TILE_USER_INDEX + 16
PAL_TERRA = 3                # PAL3

TERRAIN_MAP_COUNT = 50
STARFIELD_MAP_COUNT = 50
STARFIELD_MAP_MAX_STARS = 128  # >5 stddev above the ~80 expected stars/band

OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "src", "terrain_generated.h")


def smoothstep(t):
    return t * t * (3 - 2 * t)


NOISE_CELL_SIZE = 4  # lattice spacing in output cells -- see value_noise_2d()


def value_noise_2d(rnd, w, h, cell_size=NOISE_CELL_SIZE):
    """Returns a w x h grid of smoothly-varying values in [0,1) -- "value
    noise": a coarse lattice of independent random values (spaced cell_size
    output-cells apart) bilinearly interpolated with smoothstep easing.
    Simpler than true Perlin noise (no gradient vectors, just scalars) but
    gives the same organic, blobby look at the small sizes clumps use here,
    with no external dependency (stdlib random only, keeps regeneration
    reproducible from SEED alone).
    """
    lattice_w = (w // cell_size) + 2
    lattice_h = (h // cell_size) + 2
    lattice = [[rnd.random() for _ in range(lattice_w)] for _ in range(lattice_h)]

    grid = [[0.0] * w for _ in range(h)]
    for y in range(h):
        gy = y / cell_size
        iy = int(gy)
        ty = smoothstep(gy - iy)
        for x in range(w):
            gx = x / cell_size
            ix = int(gx)
            tx = smoothstep(gx - ix)

            top = lattice[iy][ix] + (lattice[iy][ix + 1] - lattice[iy][ix]) * tx
            bottom = lattice[iy + 1][ix] + (lattice[iy + 1][ix + 1] - lattice[iy + 1][ix]) * tx
            grid[y][x] = top + (bottom - top) * ty

    return grid


# Threshold applied to the noise*falloff density field below -- tuned by eye
# (see the __main__ density printout) to land roughly the same overall
# solid-cell density the old corner-carve approach had (that only ever
# carved the 4 corner cells, ~90%+ solid), while still letting noise cut
# real gaps through the interior instead of leaving it a solid rectangle.
DENSITY_THRESHOLD = 0.20
# How strongly the radial falloff dominates at the box corners (1.0 = the
# corner's density is forced to 0 regardless of noise; lower keeps corners
# noise-influenced too, giving raggeder, less perfectly-round edges).
FALLOFF_STRENGTH = 0.75


def gen_terrain_map(rnd):
    clumps = []
    for a in range(ANCHORS_PER_BAND):
        cx = a * CLUMP_SPACING

        if (rnd.getrandbits(4)) > 12:  # skip some anchors, but keep chunks fairly dense
            clumps.append(None)
            continue

        w = 1 + rnd.randrange(CLUMP_MAX_SIZE)
        h = 1 + rnd.randrange(CLUMP_MAX_SIZE)
        ox = min(cx + rnd.randrange(CLUMP_SPACING - w), VISIBLE_COLS - w)
        oy = rnd.randrange(BAND_ROWS - h)

        # Blend value noise with an inverted radial falloff from the box's
        # center: falloff dominates near the corners (keeps the blob roughly
        # anchored inside its box instead of noise spilling a tendril clean
        # off the edge), noise dominates near the middle (carves an organic,
        # non-rectangular silhouette instead of a solid block) -- pure noise
        # alone can carve all the way through and leave a fragmented mess,
        # pure falloff alone is just a soft-edged circle.
        noise = value_noise_2d(rnd, w, h)
        centerX, centerY = (w - 1) / 2, (h - 1) / 2
        maxDist = math.hypot(centerX, centerY) or 1  # avoid /0 for a 1x1 clump

        # Exactly w*h values, no padding -- the decoder in terrain.c knows
        # to stop once it has produced w*h cells (see TerrainMapClump).
        cells = [0] * (w * h)
        for dy in range(h):
            for dx in range(w):
                dist = math.hypot(dx - centerX, dy - centerY) / maxDist  # 0 center .. 1 corner
                density = noise[dy][dx] * (1 - dist * FALLOFF_STRENGTH)

                # Always keep the center solid -- guards against an unlucky
                # noise roll leaving a whole anchor slot invisible (a wasted
                # slot, not a gameplay bug -- terrain is decorative -- but
                # visibly odd), same spirit as the old code never being able
                # to carve every cell of anything bigger than 1x1.
                isCenter = dx == round(centerX) and dy == round(centerY)
                if density < DENSITY_THRESHOLD and not isCenter:
                    continue  # gap -- see terrain.c's cell==0 convention

                variant = rnd.getrandbits(2)  # 0..3
                cells[dy * w + dx] = variant + 1  # +1: 0 is reserved for "gap"

        clumps.append((ox, oy, w, h, cells))

    return clumps


def gen_starfield_map(rnd):
    stars = []
    for y in range(BAND_ROWS):
        for x in range(VISIBLE_COLS):
            roll = rnd.getrandbits(5)  # 0..31
            if roll == 0:
                stars.append((x, y, 1))
            elif roll == 1:
                stars.append((x, y, 2))

    if len(stars) > STARFIELD_MAP_MAX_STARS:
        raise ValueError(
            f"starfield map rolled {len(stars)} stars, exceeds "
            f"STARFIELD_MAP_MAX_STARS ({STARFIELD_MAP_MAX_STARS}) -- raise the cap"
        )

    return stars


def emit_header(terrainMaps, starfieldMaps):
    lines = []
    lines.append("// AUTO-GENERATED by res/generate_terrain.py -- do not edit directly;")
    lines.append("// re-run the generator instead.")
    lines.append("#ifndef TERRAIN_GENERATED_H")
    lines.append("#define TERRAIN_GENERATED_H")
    lines.append("")
    lines.append("#include \"game.h\"")
    lines.append("")
    lines.append(f"#define TERRAIN_MAP_BAND_ROWS {BAND_ROWS}")
    lines.append(f"#define TERRAIN_MAP_COLS      {VISIBLE_COLS}")
    lines.append(f"#define TERRAIN_MAP_ANCHORS   {ANCHORS_PER_BAND}")
    lines.append(f"#define TERRAIN_MAP_COUNT     {TERRAIN_MAP_COUNT}")
    lines.append(f"#define STARFIELD_MAP_COUNT   {STARFIELD_MAP_COUNT}")
    lines.append(f"#define STARFIELD_MAP_MAX_STARS {STARFIELD_MAP_MAX_STARS}")
    lines.append("")
    lines.append("// tileW == 0 means this anchor rolled empty (see terrain.c). Otherwise")
    lines.append("// cells holds exactly tileW*tileH row-major values, 0 = gap, else tile")
    lines.append("// variant + 1 -- stored at its exact size (no fixed-box padding: an RLE")
    lines.append("// encoding was tried here and measured *worse* than this, since these")
    lines.append("// values are close to uniformly random -- see git history if revisiting).")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    u8 tileX, tileY, tileW, tileH;")
    lines.append("    const u8 *cells;")
    lines.append("} TerrainMapClump;")
    lines.append("")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    TerrainMapClump clumps[TERRAIN_MAP_ANCHORS];")
    lines.append("} TerrainMap;")
    lines.append("")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    u8 x, y, variant; // variant: 1 or 2 (see STARFIELD_BASE_TILE)")
    lines.append("} StarfieldStar;")
    lines.append("")
    lines.append("typedef struct")
    lines.append("{")
    lines.append("    u16 count;")
    lines.append("    StarfieldStar stars[STARFIELD_MAP_MAX_STARS];")
    lines.append("} StarfieldMap;")
    lines.append("")

    # Each non-empty clump's cells get their own exactly-sized array (no
    # fixed-box padding) -- emitted before terrainMaps so it can point at them.
    for mi, clumps in enumerate(terrainMaps):
        for ai, c in enumerate(clumps):
            if c is None:
                continue
            _, _, _, _, cells = c
            cell_str = ", ".join(str(v) for v in cells)
            lines.append(f"static const u8 terrainCells_{mi}_{ai}[] = {{{cell_str}}};")
    lines.append("")

    lines.append("static const TerrainMap terrainMaps[TERRAIN_MAP_COUNT] =")
    lines.append("{")
    for mi, clumps in enumerate(terrainMaps):
        lines.append("    {{")
        for ai, c in enumerate(clumps):
            if c is None:
                lines.append("        { 0, 0, 0, 0, NULL },")
            else:
                ox, oy, w, h, _ = c
                lines.append(f"        {{ {ox}, {oy}, {w}, {h}, terrainCells_{mi}_{ai} }},")
        lines.append("    }},")
    lines.append("};")
    lines.append("")

    lines.append("static const StarfieldMap starfieldMaps[STARFIELD_MAP_COUNT] =")
    lines.append("{")
    for stars in starfieldMaps:
        padded = list(stars) + [(0, 0, 0)] * (STARFIELD_MAP_MAX_STARS - len(stars))
        star_str = ", ".join(f"{{{x}, {y}, {v}}}" for x, y, v in padded)
        lines.append(f"    {{ {len(stars)}, {{{star_str}}} }},")
    lines.append("};")
    lines.append("")
    lines.append("#endif // TERRAIN_GENERATED_H")
    lines.append("")
    return "\n".join(lines)


if __name__ == "__main__":
    rnd = random.Random(SEED)

    terrainMaps = [gen_terrain_map(rnd) for _ in range(TERRAIN_MAP_COUNT)]
    starfieldMaps = [gen_starfield_map(rnd) for _ in range(STARFIELD_MAP_COUNT)]

    header = emit_header(terrainMaps, starfieldMaps)
    with open(OUT_PATH, "w") as f:
        f.write(header)

    total_cells = 0
    solid_cells = 0
    for clumps in terrainMaps:
        for c in clumps:
            if c is None:
                continue
            _, _, _, _, cells = c
            total_cells += len(cells)
            solid_cells += sum(1 for v in cells if v != 0)

    print(f"wrote {OUT_PATH} ({TERRAIN_MAP_COUNT} terrain maps, {STARFIELD_MAP_COUNT} starfield maps)")
    print(f"terrain clump density: {solid_cells}/{total_cells} cells solid ({100 * solid_cells // total_cells}%)")
