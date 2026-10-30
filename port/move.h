#ifndef MOVE_H
#define MOVE_H
#include "poolrad.h"
u8 explore_command(void);
void move_step_forward(void);
void move_into_square(void);
void move_overland(void);
int move_party(void);
void door_remove(u8 dir, s8 y, s8 x);
void door_unlock(u8 dir, s8 y, s8 x);
u8 party_has_class(s8 n);
u8 door_bash(void);
u8 door_pick(void);
u8 spell_slot(farptr ch, u8 spell);
farptr party_find_spell(u8 spell);
u8 door_knock(void);
#endif
