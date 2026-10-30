#ifndef CAMP_H
#define CAMP_H
#include "poolrad.h"
void camp_menu(farptr interrupted);
u16 camp_rest_needed(farptr ch);
void camp_rest(farptr interrupted);
u8 camp_magic_allowed(u8 what);
s16 camp_slots_free(s8 spell_class, s8 level);
void camp_unpick_memory(farptr ch);
void camp_unpick_scrolls(farptr ch);
void camp_unpick_party(void);
void camp_cast(void);
void camp_magic(farptr interrupted);
void camp_sort_slots(void);
u8 camp_show_slots(void);
void camp_memorize(void);
void camp_scribe(void);
void camp_display(void);
void camp_order_up(void);
void camp_order_down(void);
void camp_order(void);
void camp_drop_character(void);
void camp_speed(void);
void camp_alter(void);
#endif
