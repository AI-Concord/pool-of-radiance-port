#ifndef SCENE_H
#define SCENE_H
#include "poolrad.h"
void scene_draw(farptr pic, u8 through_buffer, s16 row, s16 col);
void scene_draw_portrait(u8 with_body, u8 row, u8 col);
void scene_free_anim(farptr rec);
int scene_load_anim(farptr rec, u8 flags, u8 id, const char *name);
void scene_load_portrait(u8 body, u8 head);
int scene_draw_anim_frame(farptr rec, u8 n);
void scene_show_character_portrait(void);
#endif
void anim_play(u8 loops, u8 id, u16 row, u16 col);
void finale(void);
void map_reveal(u8 show);
