#ifndef GFX_H
#define GFX_H
#include "poolrad.h"
u8 gfx_color_mask(const u8 px[4], u8 color);
u8 gfx_nibble(farptr table, u8 n);
void gfx_draw_pic(farptr pic, s16 row, s16 col);
void gfx_merge_pic(farptr dst, farptr src, u8 flags, u8 frame, s16 row, s16 col);
u8 gfx_reverse_bits(u8 b);
void gfx_flip_pic(farptr dst, farptr src);
void gfx_flush_back_buffer(void);
void gfx_fill_pic(farptr pic, u8 color, u16 h, u16 w, s16 row, s16 col);
void gfx_alloc_pic(farptr var, u8 flags, u8 frames, u16 w, u16 h);
void gfx_free_pic(farptr var);
void gfx_decode_pic(farptr pic, u8 transparent, u16 srcoff, farptr src);
int gfx_load_pic(farptr var, u8 transparent, u8 flags, u8 id, const char *name);
void gfx_merge_pic_clipped(farptr dst, farptr src, u8 flags, u8 frame, s16 row, s16 col);
farptr gfx_new_pic(u8 flags, u8 frames, u16 w, u16 h);
void gfx_dispose_pic(farptr p);
void gfx_recolor_pic(farptr pic, u8 dither, s16 frame, farptr new_tab, farptr old_tab);
int gfx_load_pic_native(farptr *out, u8 transparent, u8 flags, u8 id, const char *name);
void gfx_set_mode_at(u8 mode, u16 entry_sp);
void gfx_restore_mode_at(u16 entry_sp);
u16 gfx_reverse_pairs(u16 v);
u8 gfx_swap_nibbles(u8 b);
void gfx_palette_reg(u8 value, u8 reg);          // 1464:2A0D
#endif
