#ifndef GAMETIME_H
#define GAMETIME_H
#include "poolrad.h"
void time_carry(u8 *counters);
void time_advance(s8 unit, s8 count);
#endif
void effects_tick(u8 count, s8 unit);
