#ifndef MENU_H
#define MENU_H
#include "poolrad.h"
u8 menu_horizontal(u8 *special, u8 animate, u8 allow_moves, u8 hi_color, u8 color, u8 prompt_color, const u8 *items, const u8 *prompt);
void menu_clear_line(void);
u8 menu_yes_no(u8 hi_color, u8 color, u8 prompt_color, farptr prompt);
void menu_list_alloc(farptr var, s16 count);
void menu_list_free(farptr var);
u8 menu_list(farptr *result, s16 *index, u8 *redraw, u8 with_exit, farptr head, s8 bottom, s8 right, s8 top, s8 left,
             u8 hi_color, u8 color, u8 head_color, const u8 *items, const u8 *prompt);
farptr menu_list_new(s16 count);
void menu_list_dispose(farptr node);
extern int menu_arrows_move;
farptr list_nth(farptr node, s16 n);             // 4726:0000
#endif
