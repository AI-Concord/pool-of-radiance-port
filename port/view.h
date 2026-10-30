#ifndef VIEW_H
#define VIEW_H
#include "poolrad.h"
void view_set_colors(u8 floor, u8 band2, u8 band1, u8 sky);
void view_draw_marker(void);
void view_redraw(void);
#endif
void view_overhead(s8 row, s8 col);
