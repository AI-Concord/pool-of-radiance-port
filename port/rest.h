#ifndef REST_H
#define REST_H
#include "poolrad.h"
void rest_show_time(s8 selected);
void rest_normalise(void);
void rest_subtract(s8 unit, u8 count);
u8 rest_menu(void);
void rest_daily_healing(void);
void rest_study(void);
void rest_sleep_hour(u8 *steps);
u8 rest_screen(void);
u8 rest_memorise_next(u8 *done, farptr ch);
u8 rest_scribe_next(u8 *done, farptr ch);
#endif
