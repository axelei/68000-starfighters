// rescomp resource declarations for placeholder art.
// Verified against C:/sgdk/bin/rescomp.txt (SGDK's ResComp 3.95).
//
// SPRITE width/height are in TILES (8px each), not pixels:
//   SPRITE name img_file width height [compression [time [collision ...]]]

// -- palettes (one per related sprite/tile group, 4 hardware palettes total)
PALETTE palette_ship  "gfx/player_ship.png"
PALETTE palette_enemy "gfx/enemy_bee.png"
PALETTE palette_pwr   "gfx/powerup_spread.png"
PALETTE palette_terra "gfx/terrain_tiles.png"

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
SPRITE spr_powerup_spread "gfx/powerup_spread.png" 2 2 BEST
SPRITE spr_powerup_speed  "gfx/powerup_speed.png"  2 2 BEST

// Ground turret (see turret.c); terrain-attached, so it's drawn with
// PAL_TERRA. No PALETTE entry -- it reuses palette_terra's already-loaded
// colors (indices 6-8, see generate_placeholders.py's terrain_tiles()).
SPRITE spr_turret         "gfx/turret.png"         2 2 BEST

// -- title screen (see title.c). Bundles its own palette (loaded onto PAL0
// when drawn) -- not one of the 4 shared gameplay hardware palettes.
IMAGE title_image "gfx/title.png"

// -- tilesets for scrolling backgrounds (BG_A = terrain, BG_B = starfield)
// opt=NONE keeps every tile distinct/in-order, since terrain.c indexes
// tiles by their position in the source sheet.
TILESET terrain_tileset   "gfx/terrain_tiles.png"   BEST NONE
TILESET starfield_tileset "gfx/starfield_tiles.png" BEST NONE

// Single opaque tile used to back the HUD side panel (see score.c). Drawn
// with PAL_SHIP at runtime, so no PALETTE entry is declared for it.
TILESET hud_fill_tileset  "gfx/hud_fill.png"         BEST NONE
