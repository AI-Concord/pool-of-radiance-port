#ifndef TEXT_H
#define TEXT_H
#include "poolrad.h"
void text_reset_ega(void);
void text_set_work_page(u8 page);
void text_fill_cells(u8 color, u8 page, s8 row2, s8 col2, s8 row1, s8 col1);
void text_put_char(u8 col, u8 row, u8 color, u8 count, u8 ch, u8 fold);
void text_put_blanks(u8 count, u8 color, u8 row, u8 col);
void text_put_string(farptr str, u8 color, u8 row, u8 col);
void text_put_pstr(const u8 *s, u8 color, u8 row, u8 col);
void text_clear_box(s8 left, s8 top, s8 right, s8 bottom);
void text_prompt(farptr str, u8 color);
void text_print_wrapped(farptr str, u8 clear, u8 color, u8 bottom, u8 right, u8 top, u8 left);
void text_clear_screen(void);
void text_pause(void);
void text_flash_message(u8 color, farptr str);
void text_input_string(u8 maxlen, u8 color, farptr prompt, farptr result);
void text_input_pstr(u8 maxlen, u8 color, const u8 *prompt, u8 *result);
void text_print_wrapped_pstr(const u8 *str, u8 clear, u8 color, u8 bottom, u8 right, u8 top, u8 left);
void text_show_page(u8 n);
void text_load_font(void);
void text_bios_char(u8 c, u8 count, u8 color, u8 row, u8 col);
void text_bios_string(farptr s, u8 color, u8 row, u8 col);
void text_bios_bar(u8 count, u8 color, u8 row, u8 col);
#endif
