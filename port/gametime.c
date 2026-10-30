#include <stdlib.h>
#include <stdio.h>
// Game time (original overlay unit at 3EC2).
//
// The clock is seven counters kept in script variable space (script addresses 49C6-49CC, which
// are words 18Ch onward of the game record), so that adventure scripts can read it:
//   0 tenths of a minute   1 minutes   2 tens of minutes   3 hours   4 days   5 months   6 years
// Each counter rolls over into the next at the value in the table at DS:363A (10, 10, 6, 24, 30, 12).
#include "poolrad.h"
#include "rtl.h"
#include "gametime.h"

// 3EC2:02F2  Carry any counter that has reached its limit into the next one. Each counter is
// checked once, in order, so one call carries one step per counter. The last counter has no next:
// when it reaches its limit (256) every party member's age (character word 30h) goes up instead.
void time_carry(u8 *counters) {
    for (int i = 0; i <= 6; i++) {
        u16 limit = G16(0x363a + i * 2), v = (u16)(counters[i * 2] | (counters[i * 2 + 1] << 8));
        if (limit > v) continue;
        if (i == 6) { for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) SETP16(ch, 0x30, (u16)(P16(ch, 0x30) + 1)); }
        else {
            u16 next = (u16)((counters[i * 2 + 2] | (counters[i * 2 + 3] << 8)) + 1);
            counters[i * 2 + 2] = (u8)next; counters[i * 2 + 3] = (u8)(next >> 8);
            v = (u16)(v - limit);
            counters[i * 2] = (u8)v; counters[i * 2 + 1] = (u8)(v >> 8);
        }
    }
}

// 3EC2:03D5  Advance the clock by `count` of counter `unit`, then let that much time act on every
// 3EC2:0000  Counting down the party's timed effects after `count` units of time `unit` (converted to the smallest
// unit with the table at DS:363A). Each member has a "has timers" flag at DS:471E + n - 1; outside display mode 2 all
// are set first, and in mode 2 nothing happens unless one is set. Time is applied in steps of at most 10: an effect's
// timer (word at +1; 0 = permanent) above the step is reduced, otherwise the effect ends (4592:0026). The walk stops at
// the effect that was last when it began, and after a removal it restarts from the head when the node it had reached
// was the head. A member's flag stays set while any effect still has time.
void effects_tick(u8 count, s8 unit) {
    if (G8(0x49f3) != 2) memset(&G8(0x471e), 1, 0x48);
    else {
        u8 found = 0, i = 1;
        do { if (G8(0x471d + i)) found = 1; i++; } while (!found && i <= P16(G32(0x49d6), 0x67c));
        if (!found) return;
    }
    u16 amt = count;
    for (s8 u = unit; u > 1; u--) amt = (u16)(G16((u - 1) * 2 + 0x363a) * amt);
    while (amt) {
        u8 step = amt > 10 ? 10 : (u8)amt;
        u8 i = 1;
        for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104), i++) {
            if (!G8(0x471d + i)) continue;
            G8(0x471d + i) = 0;
            farptr e = P32(ch, 0x7f), last = e, scan = e, nxt = rd32(LIN(e) + 5);
            if (P32(ch, 0x7f)) while (P32(last, 5)) last = P32(last, 5);
            u8 done = 0;
            while (e && !done) {
                if (e == last) done = 1;
                if (P16(e, 1) == 0) e = P32(e, 5);
                else if (step < P16(e, 1)) { wr16(LIN(e) + 1, (u16)(P16(e, 1) - step)); G8(0x471d + i) = 1; e = P32(e, 5); }
                else {
                    nxt = P32(e, 5);
                    call_original(0x4592, 0x0026, 5, FP_SEG(ch), FP_OFF(ch), P8(e, 0), FP_SEG(e), FP_OFF(e));
                    e = (scan == P32(ch, 0x7f)) ? P32(ch, 0x7f) : nxt;
                }
                for (scan = P32(ch, 0x7f); scan && scan != e && P32(scan, 5) != e; scan = P32(scan, 5)) ;
            }
            for (; e; e = P32(e, 5)) if (P16(e, 1) > 0) G8(0x471d + i) = 1;
        }
        amt = amt > 10 ? (u16)(amt - 10) : 0;
    }
}

// character's timed status effects (still original code).
void time_advance(s8 unit, s8 count) {
    u8 c[14];
    u8 *clock = &mem[LIN(G32(0x49d2)) + 0x18c];
    memcpy(c, clock, 14);
    for (s8 n = 1; count != 0; n++) {
        u16 v = (u16)((c[unit * 2] | (c[unit * 2 + 1] << 8)) + 1);
        c[unit * 2] = (u8)v; c[unit * 2 + 1] = (u8)(v >> 8);
        time_carry(c);
        if (n == count) break;
    }
    memcpy(clock, c, 14);
    effects_tick((u8)count, unit);
}
