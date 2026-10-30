#ifndef MAP_H
#define MAP_H
#include "poolrad.h"
int map_in_bounds(s8 a, s8 b);
u8 map_wall(u8 dir, s8 y, s8 x);
u8 map_event(s8 y, s8 x);
void view_draw_background(void);
void view_draw_piece(u8 piece, u8 type, s8 row, s8 col);
void view_draw_area_map(u8 dir, s8 y, s8 x);
void view_render(u8 dir, s8 y, s8 x);
int sym_put(u8 flags, u8 to_buffer, u16 sym, s8 row, s8 col);
int sym_load_set(u8 set, u8 id);
int icon_load(u8 slot, const char *name);
void icon_draw(u8 icon, s8 row, s8 col);
void icon_free_slot(u8 n);
u8 map_passage(u8 dir, s8 y, s8 x);
int map_load(u8 id);
int map_load_walls(u8 slot, u8 id);
#endif
