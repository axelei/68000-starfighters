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
SPRITE spr_bullet_player  "gfx/bullet_player.png"  1 1 BEST
SPRITE spr_bullet_enemy   "gfx/bullet_enemy.png"   1 1 BEST
SPRITE spr_powerup_spread "gfx/powerup_spread.png" 2 2 BEST
SPRITE spr_powerup_speed  "gfx/powerup_speed.png"  2 2 BEST

// -- tilesets for scrolling backgrounds (BG_A = terrain, BG_B = starfield)
// opt=NONE keeps every tile distinct/in-order, since terrain.c indexes
// tiles by their position in the source sheet.
TILESET terrain_tileset   "gfx/terrain_tiles.png"   BEST NONE
TILESET starfield_tileset "gfx/starfield_tiles.png" BEST NONE
