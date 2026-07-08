// rescomp resource declarations for placeholder art.
// Verified against C:/sgdk/bin/rescomp.txt (SGDK's ResComp 3.95).
//
// SPRITE width/height are in TILES (8px each), not pixels:
//   SPRITE name img_file width height [compression [time [collision ...]]]

// -- palettes -- 4 hardware palettes, one per consolidated group (see
// generate_placeholders.py's module docstring for the full design). Title's
// own palette (below) briefly borrows PAL0 before title.c's
// title_fadeInGame() overwrites it with palette_player's real colors, once
// gameplay starts.
PALETTE palette_player "gfx/player_ship.png"
PALETTE palette_enemy  "gfx/enemy_bee.png"
PALETTE palette_environment "gfx/terrain_tiles.png"

// Boss body/weak-spots/homing bullet (see boss.c). Only this image's
// PALETTE resource is ever loaded onto hardware (PAL3) -- every other
// boss-related image below reuses these same indices (see
// generate_placeholders.py's BOSS_PAL).
PALETTE palette_boss "gfx/boss_body.png"

// -- sprites (all single-frame placeholders for milestone 1)
// 16x16px = 2x2 tiles, 8x8px = 1x1 tile
SPRITE spr_player         "gfx/player_ship.png"    2 2 BEST
SPRITE spr_enemy_bee      "gfx/enemy_bee.png"      2 2 BEST
SPRITE spr_enemy_special  "gfx/enemy_special.png"  2 2 BEST
SPRITE spr_enemy_big      "gfx/enemy_big.png"      4 4 BEST

// Shared by enemy and player death; drawn with PAL_ENEMY (see explosion.c).
// No PALETTE entry -- it reuses palette_enemy's already-loaded colors.
SPRITE spr_explosion      "gfx/explosion.png"      2 2 BEST
SPRITE spr_bullet_player  "gfx/bullet_player.png"  1 1 BEST
SPRITE spr_bullet_enemy   "gfx/bullet_enemy.png"   1 1 BEST

// Powerups share PAL_PLAYER (see generate_placeholders.py's PLAYER_PAL) --
// no PALETTE entry, they reuse palette_player's already-loaded colors.
SPRITE spr_powerup_spread "gfx/powerup_spread.png" 2 2 BEST
SPRITE spr_powerup_speed  "gfx/powerup_speed.png"  2 2 BEST

// Ground turret (see turret.c); shares PAL_ENEMY with the rest of the
// enemies. No PALETTE entry -- it reuses palette_enemy's already-loaded
// colors (see generate_placeholders.py's ENEMY_PAL).
SPRITE spr_turret         "gfx/turret.png"         2 2 BEST

// Inter-wave "waver" enemies (see enemy.c's ENEMY_STATE_WAVING/formation.c's
// beginInterwave()); also share PAL_ENEMY (ENEMY_PAL's indices 9/10/11).
SPRITE spr_enemy_waver_a  "gfx/enemy_waver_a.png"  2 2 BEST
SPRITE spr_enemy_waver_b  "gfx/enemy_waver_b.png"  2 2 BEST
SPRITE spr_enemy_waver_c  "gfx/enemy_waver_c.png"  2 2 BEST

// Boss enemy (see boss.c) -- a 64x64 body built from 4 quadrants, all
// created from this single 2-frame sheet (frame 0 = top-left, frame 1 =
// bottom-left; the right-hand quadrants reuse the same tiles horizontally
// mirrored at runtime, not separate art -- see
// generate_placeholders.py's boss_body()), plus 2 independently
// destructible 16x16 weak-spot pods (3-frame sheet: normal/hit-flash/
// destroyed) and a dedicated 16x16 homing bullet (2-frame sheet:
// normal/hit-flash). All share palette_boss (PAL3).
SPRITE spr_boss_body      "gfx/boss_body.png"      4 4 BEST
SPRITE spr_boss_weakspot  "gfx/boss_weakspot.png"  2 2 BEST
SPRITE spr_bullet_homing  "gfx/bullet_homing.png"  2 2 BEST

// -- title screen (see title.c). Bundles its own palette (loaded onto PAL0
// when drawn) -- not one of the 3 shared gameplay hardware palettes.
IMAGE title_image "gfx/title.png"

// Title screen music (see title.c) -- played through the XGM2 Z80 driver,
// loaded only while the title screen is up and unloaded again once Start is
// pressed, so it never contends with sfx.c's direct PSG writes during
// actual gameplay (see title.c's comment on this).
XGM2 title_music "music/title.vgm"

// -- tilesets for scrolling backgrounds (BG_A = terrain, BG_B = starfield)
// opt=NONE keeps every tile distinct/in-order, since terrain.c indexes
// tiles by their position in the source sheet.
TILESET terrain_tileset   "gfx/terrain_tiles.png"   BEST NONE
TILESET starfield_tileset "gfx/starfield_tiles.png" BEST NONE

// Single opaque tile used to back the HUD side panel (see score.c). Drawn
// with PAL_PLAYER at runtime, so no PALETTE entry is declared for it.
TILESET hud_fill_tileset      "gfx/hud_fill.png"      BEST NONE

// Red divider line between the playfield and the HUD side panel (see
// score.c). Drawn with PAL_PLAYER at runtime (index 9, see PLAYER_PAL).
TILESET hud_separator_tileset "gfx/hud_separator.png" BEST NONE

// Drop-in replacement for SGDK's default font (font_default), with an
// opaque black background baked into every glyph tile instead of a
// transparent one -- loaded once via VDP_loadFont() (see main.c). opt=NONE
// is required here (not just conventional): VDP_loadFont() maps ASCII code
// to tile position positionally (tile[i] = character 32+i), so any
// tile-deduplication would break that mapping.
TILESET banner_font_tileset "gfx/banner_font.png" BEST NONE
