// Resting in camp (original unit at 3EC2, the half that is not the game clock).
// The time still to rest is kept like the game clock: words at DS:6DC2 + 2*unit, where unit 1 is
// minutes, 2 tens of minutes, 3 hours, 4 days (5 and 6, months and years, only catch overflow).
#include <stdio.h>
#include "poolrad.h"
#include "text.h"
#include "menu.h"
#include "hud.h"
#include "gametime.h"
#include "rtl.h"
#include "rest.h"
#include "fight.h"

#define LEFT(unit) (0x6dc2 + (unit) * 2)
static void put(const char *t, u8 color, u8 row, u8 col) {
    u8 s[64]; size_t n = strlen(t);
    s[0] = (u8)n; memcpy(s + 1, t, n);
    text_put_pstr(s, color, row, col);
}
static void put2(u8 value, u8 color, u8 col) { char t[8]; snprintf(t, sizeof t, "%02u", value); t[2] = 0; put(t, color, 0x11, col); }   // 3EC2:0565: two digits, cut to two characters

// 3EC2:05F0  "Rest Time: DD:HH:MM", with the unit being edited (2 minutes, 3 hours, 4 days) in white.
void rest_show_time(s8 selected) {
    u8 color[7] = {10, 10, 10, 10, 10, 10, 10};
    color[selected] = 15;
    put("Rest Time:", 10, 0x11, 1);
    put2(G8(LEFT(4)), color[4], 12); put(":", 10, 0x11, 14);
    put2(G8(LEFT(3)), color[3], 15); put(":", 10, 0x11, 17);
    put2((u8)(G16(LEFT(2)) * 10 + G16(LEFT(1))), color[2], 18);
}

// 3EC2:03A3  Tidy the counters after a change: carry as the clock does, turn any months into
// thirty days each, and stop at 99 days.
void rest_normalise(void) {
    time_carry(&G8(0x6dc2));
    if (G16(LEFT(5)) != 0) {
        SETG16(LEFT(4), (u16)(G16(LEFT(4)) + G16(0x3642) * G16(LEFT(5))));
        SETG16(LEFT(5), 0);
        if (G16(LEFT(4)) > 99) SETG16(LEFT(4), 99);
    }
}

// 3EC2:047A  Take `count` of a unit off the time left, borrowing from the larger units when the
// unit runs short (an hour becomes 6 tens of minutes, and so on); if there is nothing larger to
// borrow from, the time left becomes zero.
void rest_subtract(s8 unit, u8 count) {
    if (!G16(LEFT(4)) && !G16(LEFT(3)) && !G16(LEFT(2)) && !G16(LEFT(1))) return;
    while (G16(LEFT(unit)) < count) {
        s8 from = unit;
        do { from++; if (G16(LEFT(from)) != 0) break; } while (from < 5);
        if (from == 5) { memset(&G8(0x6dc2), 0, 14); count = 0; }
        else for (s8 k = from; k >= unit + 1; k--) {
            SETG16(LEFT(k), (u16)(G16(LEFT(k)) - 1));
            SETG16(LEFT(k - 1), (u16)(G16(LEFT(k - 1)) + G16(0x363a + (k - 1) * 2)));
        }
    }
    SETG16(LEFT(unit), (u16)(G16(LEFT(unit)) - count));
    rest_normalise();
}

// 3EC2:0721  The menu for setting the time: Y, H, M pick days, hours or minutes; Inc and Dec (or
// the Up and Down arrows) change it, minutes in fives; Left and Right move between the units.
// Returns 1 for Rest (or Enter) and 0 for Exit.
u8 rest_menu(void) {
    static const u8 items[] = "\047Rest   daYs Hours Mins   Inc Dec   Exit", none[] = {0};
    u8 go = 0, special, key; s8 sel = 2;
    do {
        rest_show_time(sel);
        key = menu_horizontal(&special, 0, 1, 15, 10, 13, items, none);
        if (special) {
            if (key == 'H') key = 'I';
            else if (key == 'P') key = 'D';
            else if (key == 'K') { if (++sel > 4) sel = 2; key = 'X'; }
            else if (key == 'M') { if (--sel < 2) sel = 4; key = 'X'; }
            else key = 'X';
        }
        if (key == '\r') key = 'R';
        if (key == 'R') go = 1;
        else if (key == 'Y') sel = 4;
        else if (key == 'H') sel = 3;
        else if (key == 'M') sel = 2;
        else if (key == 'I') {
            if (sel == 2) SETG16(LEFT(1), (u16)(G16(LEFT(1)) + 5)); else SETG16(LEFT(sel), (u16)(G16(LEFT(sel)) + 1));
            rest_normalise();
        }
        else if (key == 'D') { if (sel == 2) rest_subtract(1, 5); else rest_subtract(sel, 1); }   // (the original tidies once more here; it changes nothing)
    } while (key != 0 && key != 'E' && key != 'R');
    return go;
}

// 3EC2:0871  Once in every 288 steps (a full day of rest) everyone gets their natural healing.
void rest_daily_healing(void) {
    SETG16(0x6dd8, (u16)(G16(0x6dd8) + 1));
    if (G16(0x6dd8) < 0x120) return;
    int healed = 0;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104))
        if ((u8)call_original(0x4592, 0x175d, 4, FP_SEG(ch), FP_OFF(ch), 1, 0)) healed = 1;   // heal one character (still original code)
    rest_show_time(0);
    put("The Whole Party Is Healed", 10, 0x13, 1);
    if (healed) hud_draw_roster(G32(0x5d92));
    text_pause();
    ui_clear_message();
    SETG16(0x6dd8, 0);
}

// 3EC2:0925  Memorising. Called with *done = 0 when a spell's study time is up: the first
// picked spell loses its "picked" bit, "<name> has memorized <spell>" is shown, and the level of
// the next picked spell is returned (0 if there is none). Called with *done = 1 it only reports.
u8 rest_memorise_next(u8 *done, farptr ch) {
    u8 level = 0;
    for (u8 i = 0; i < 0x15 && level == 0; i++) {
        if (P8(ch, 0x17 + i) <= 0x7f) continue;
        if (*done == 0) {
            P8(ch, 0x17 + i) -= 0x80;
            rest_show_time(0);
            call_original(0x41cf, 0x331a, 5, FP_SEG(ch), FP_OFF(ch), RSEG(0x3ec2), 0x0917, P8(ch, 0x17 + i));   // the message line (still original code)
            *done = 1;
        }
        else level = G8(0x31fb + (P8(ch, 0x17 + i) & 0x7f) * 16);
    }
    return level;
}

// 3EC2:09E7  Scribing, the same idea for spells picked on scrolls: the spell goes into the
// spell book (byte 32h + spell number), is removed from the scroll, and "has scribed" is shown.
// The test here is "above 80h" where memorising uses "above 7Fh"; spell 0 does not exist, so it
// makes no difference. The scroll may be used up and freed by the removal, and the original
// goes on reading its remaining slots from the freed block; so does this.
u8 rest_scribe_next(u8 *done, farptr ch) {
    u8 level = 0;
    for (farptr item = P32(ch, 0xc8); item && level == 0; item = P32(item, 0x2a)) {
        if (!item_is_scroll(item)) continue;
        for (u8 k = 1; k < 4 && level == 0; k++) {
            if (P8(item, 0x3b + k) <= 0x80) continue;
            if (*done == 0) {
                u8 spell = P8(item, 0x3b + k) & 0x7f;
                P8(ch, 0x32 + spell) = 1;
                call_original(0x41cf, 0x328a, 5, FP_SEG(ch), FP_OFF(ch), FP_SEG(item), FP_OFF(item), spell);
                rest_show_time(0);
                call_original(0x41cf, 0x331a, 5, FP_SEG(ch), FP_OFF(ch), RSEG(0x3ec2), 0x09db, spell);
                *done = 1;
            }
            else level = G8(0x31fb + (P8(item, 0x3b + k) & 0x7f) * 16);
        }
    }
    return level;
}

static u8 learn_next(farptr ch, u8 first) {   // finish the spell just studied (unless `first`) and return the level of the next, scrolls before memory
    u8 done = first, level = rest_scribe_next(&done, ch);
    if (!level) level = rest_memorise_next(&done, ch);
    return level;
}

// 3EC2:0B0A  Each step: a character who has had their sleep (byte 2Ch is zero) counts down the
// spell being learned (DS:6DCF + party position); when it is done the next one starts, three
// steps, which is fifteen minutes, per spell level.
void rest_study(void) {
    u8 i = 1;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104), i++) {
        G8(0x6dcf + i)--;
        if (G8(0x6dcf + i) == 0 && P8(ch, 0x2c) == 0) G8(0x6dcf + i) = (u8)(learn_next(ch, 0) * 3);
    }
}

// 3EC2:0BB2  Every twelfth step (an hour) each character's hours of sleep still needed go down
// by one; the hour it reaches zero they start on their first spell.
void rest_sleep_hour(u8 *steps) {
    if (++*steps < 12) return;
    *steps = 0;
    u8 i = 1;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104), i++)
        if ((s8)P8(ch, 0x2c) > 0 && --P8(ch, 0x2c) == 0) G8(0x6dcf + i) = (u8)(learn_next(ch, 1) * 3);
}

// 3EC2:0C9A  The rest screen. Set the time, then rest in five-minute steps until the time runs
// out, a key is pressed and "Stop Resting?" answered Yes, or the party is interrupted. The area's
// script data says how often to check for that (word 5A4h, in steps) and the chance in a
// hundred (word 5A6h). Returns 1 if the rest was interrupted.
u8 rest_screen(void) {
    u8 interrupted = 0, steps = 0, since_redraw = 0, stop, i = 1;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) G8(0x6dcf + i++) = 0;
    memset(&G8(0x471e), 1, 0x48);
    text_clear_box(1, 0x11, 0x26, 0x16);
    rest_show_time(0);
    G8(0x6dda) = 1;
    stop = !rest_menu();
    while (!stop && (G16(LEFT(4)) || G16(LEFT(3)) || G16(LEFT(2)) || G16(LEFT(1)))) {
        if (crt_key_pressed()) {
            rest_show_time(0);
            if (menu_yes_no(15, 10, 13, FARPTR(RSEG(0x3ec2), 0x0c6a)) == 'Y') stop = 1;   // "Stop Resting? "
            else menu_clear_line();
        }
        if (stop) continue;
        rest_subtract(1, 5);
        if (++since_redraw >= 5) { rest_show_time(0); since_redraw = 0; }
        time_advance(1, 5);
        rest_daily_healing();
        rest_study();
        rest_sleep_hour(&steps);
        farptr area = G32(0x49d6);
        if (P16(area, 0x5a4) != 0) {
            SETG16(0x49fb, (u16)(G16(0x49fb) + 1));
            if (G16(0x49fb) >= P16(area, 0x5a4)) {
                SETG16(0x49fb, 0);
                if ((u8)call_original(0x4592, 0x0de3, 2, 1, 100) <= P16(area, 0x5a6)) {     // one roll of a hundred-sided die (still original code)
                    ui_clear_message();
                    rest_show_time(0);
                    put("The Party is rudely interrupted!", 15, 0x13, 1);
                    stop = 1; interrupted = 1;
                    text_pause();
                }
            }
        }
    }
    text_clear_box(1, 0x11, 0x26, 0x16);
    G8(0x6dda) = 0;
    return interrupted;
}
