"""Generates src/intro_crawl_generated.h -- the intro scene's crawl text as
a C array of strings, one per line of res/gfx/intro_crawl_text.txt. A blank
line in that file becomes an empty ("") entry in the array; intro.c treats
an empty entry as a directive to leave a CRAWL_GAP_LINES-tall gap rather
than text to draw (see refillCrawl()) -- so gaps are exactly where you put
a blank line in the source, not automatically at every line. There's no
length limit -- intro.c streams INTRO_LINES[] onto the plane incrementally
as the crawl scrolls (see refillCrawl()), not all at once up front, so an
arbitrarily long script works.

Also computes INTRO_TOTAL_FRAMES: an *estimate* of how many frames the
crawl will take to scroll past, mirroring intro.c's own word-wrap/layout
math (see wrap_text()/estimate_total_frames() below). intro_run()'s main
loop does NOT use this to decide when to stop -- that's driven by real
scroll/plane state instead (see its own comment on why: an estimate can
never be allowed to cut text off early). This total is only used to derive
each background slideshow image's on-screen duration (see BG_SLIDE_FRAMES
in intro.c), so slide timing roughly tracks however long the crawl takes
without a separate duration to keep in sync by hand -- if it's slightly
off, slides just end up a bit longer/shorter than the crawl, not text
disappearing early.

No image/tileset is produced for the crawl text itself. intro.c draws it
entirely at runtime, reusing the game's own already-loaded system font
tiles (TILE_FONT_INDEX -- see main.c's VDP_loadFont()) directly -- there is
no font asset dedicated to the intro at all, let alone a pre-rendered
picture of the text itself.

Edit intro_crawl_text.txt, not this script, then re-run:
    python generate_intro.py
"""

import math
import os

TEXT_PATH = os.path.join(os.path.dirname(__file__), "intro_crawl_text.txt")
OUT_HEADER = os.path.join(os.path.dirname(__file__), "..", "..", "src", "intro_crawl_generated.h")

# Mirrors intro.c's own constants of the same name exactly -- keep both
# sides in sync if either changes, or INTRO_TOTAL_FRAMES will drift from
# what intro.c actually does.
CRAWL_MAX_CHARS = 36   # intro.c's CRAWL_MAX_CHARS
CRAWL_LINE_ROWS = 2    # intro.c's CRAWL_LINE_ROWS
CRAWL_GAP_LINES = 6    # intro.c's CRAWL_GAP_LINES
HALF_H = 112           # intro.c's HALF_H (SCREEN_H / 2)
CRAWL_SPEED = 0.25     # intro.c's CRAWL_SPEED, as a plain float px/frame


def load_lines():
    with open(TEXT_PATH, "r", encoding="utf-8") as f:
        raw = f.read()

    lines = [" ".join(line.split()) for line in raw.splitlines()]  # collapse internal whitespace per line

    # Collapse runs of consecutive blank lines down to a single gap marker
    # -- multiple blank lines mean the same thing as one (a single 6-line
    # gap), not a bigger gap -- and trim any leading/trailing blanks so the
    # crawl doesn't start or end with a pointless empty gap.
    collapsed = []
    for line in lines:
        if line == "" and (not collapsed or collapsed[-1] == ""):
            continue
        collapsed.append(line)
    while collapsed and collapsed[0] == "":
        collapsed.pop(0)
    while collapsed and collapsed[-1] == "":
        collapsed.pop()

    return collapsed


def wrap_text(text, max_chars):
    """Exact Python port of intro.c's wrapText() -- same greedy
    "back up to the last space" rule, not textwrap.wrap()'s own (slightly
    different) algorithm, since this needs to match intro.c's real output
    line-for-line to produce an accurate frame count."""
    lines = []
    p = 0
    n = len(text)
    while p < n:
        while p < n and text[p] == " ":
            p += 1
        if p >= n:
            break

        start = p
        last_space = -1
        col = 0
        while p < n and col < max_chars:
            if text[p] == " ":
                last_space = p
            p += 1
            col += 1

        if p < n and text[p] != " " and last_space != -1:
            p = last_space

        lines.append(text[start:p])
    return lines


def estimate_total_frames(lines):
    """Total tile-rows the crawl will occupy (gaps + every wrapped line),
    converted to a scroll distance and then a frame count -- see
    intro_run()'s own totalScroll derivation (HALF_H + content height),
    just computed here once instead of tracked live at runtime."""
    rows = 0
    for line in lines:
        if line == "":
            rows += CRAWL_GAP_LINES * CRAWL_LINE_ROWS
        else:
            rows += len(wrap_text(line, CRAWL_MAX_CHARS)) * CRAWL_LINE_ROWS

    total_scroll_px = HALF_H + rows * 8
    return math.ceil(total_scroll_px / CRAWL_SPEED)


def emit_c_string_array(name, lines):
    out = [f"static const char *{name}[] = {{"]
    for line in lines:
        escaped = line.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'    "{escaped}",')
    out.append("};")
    return "\n".join(out)


if __name__ == "__main__":
    lines = load_lines()
    if not lines:
        raise SystemExit(f"{TEXT_PATH} has no text")

    total_frames = estimate_total_frames(lines)

    with open(OUT_HEADER, "w") as f:
        f.write("// AUTO-GENERATED by res/gfx/generate_intro.py -- do not edit directly;\n")
        f.write("// re-run the generator after changing intro_crawl_text.txt.\n")
        f.write("#ifndef INTRO_CRAWL_GENERATED_H\n")
        f.write("#define INTRO_CRAWL_GENERATED_H\n\n")
        f.write(f"{emit_c_string_array('INTRO_LINES', lines)}\n\n")
        f.write(f"#define INTRO_LINE_COUNT {len(lines)}\n")
        f.write(f"#define INTRO_TOTAL_FRAMES {total_frames}\n\n")
        f.write("#endif // INTRO_CRAWL_GENERATED_H\n")

    gaps = sum(1 for l in lines if l == "")
    print(f"wrote {OUT_HEADER} ({len(lines)} entries, {gaps} gap(s), "
          f"{total_frames} frames (~{total_frames / 60:.1f}s at 60fps))")
