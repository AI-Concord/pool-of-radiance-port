#ifndef HUD_H
#define HUD_H
#include "poolrad.h"
void hud_draw_status(void);
void hud_draw_name(u8 mark, u8 row, u8 col, farptr ch);
void hud_draw_ac(u8 row, u8 col, farptr ch);
void hud_draw_hp(u8 highlight, u8 row, u8 col, farptr ch);
void hud_draw_roster(farptr selected);
void hud_draw_frames(u8 picture_border);
void hud_draw_screen(void);
void ui_border(u8 style, s8 bottom, s8 right, s8 top, s8 left);
void ui_clear_message(void);
void ui_notice(const u8 *text);
void ui_char_message(u8 wait, u8 combat_row, const u8 *text, farptr ch);
#endif
void hud_combat_panel(farptr ch);
void wait_seconds_or_key(u16 n);
void title_screen(void);
void intro(void);
void code_wheel(void);
void hud_explore_frames(void);
