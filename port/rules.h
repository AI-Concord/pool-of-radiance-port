#ifndef RULES_H
#define RULES_H
#include "poolrad.h"
u8 roll_dice(u8 sides, u8 count);
u8 roll_dice_noted(u8 sides, u8 count);
void effects_remove_invisibility(farptr ch);
void effects_remove_list16(farptr ch);
void effects_remove_list2(farptr ch);
u8 rules_encode(u8 a, u8 b);
void effect_remove(farptr node, u8 kind, farptr ch);
void effect_check(u8 kind, farptr ch);
void effects_event(farptr ch, u8 event);
u8 rules_hit_unaimed(u8 bonus, farptr ch);       // 4592:0C4F (fight.c)
void effect_add(u8 notify, u8 b3, u16 time, u8 kind, farptr ch);
u8 rules_saving_throw(s8 adjust, s8 kind, farptr ch);
void rules_damage(u8 save_applies, u8 save, u8 amount, farptr ch);
u8 rules_get_up(u8 hp, farptr ch);
u8 rules_heal(u8 natural, u8 amount, farptr ch);
void effect_run(u8 arg, farptr node, farptr ch, u8 kind);
void rules_spell_effect(const u8 *message, u8 saved, u8 save_counts, u8 notify, u8 b3, u16 time, u8 kind, farptr ch);
void rules_strength_decode(u8 *pct, u8 *strength, farptr node);
#endif
