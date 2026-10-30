#include "fixes.h"
// The adventure script (ECL) interpreter: opcode handlers (original overlay unit at 1D3F) and the
// helpers they share (unit at 2366). The current opcode is in DS:6F7F, the script pointer in
// DS:49ED; ecl_fetch() reads an instruction's operands and steps past it.
#include "gametime.h"
#include "poolrad.h"
#include "rules.h"
#include "sheet.h"
#include "chr.h"
#include "combat.h"
#include "ecl.h"
#include "rtl.h"
#include "hud.h"
#include "text.h"
#include "scene.h"
#include "menu.h"
#include "map.h"
#include "move.h"
#include "camp.h"
#include "dax.h"
#include <stdio.h>
#include <stdlib.h>

#define ECL_IP 0x49ed
#define OPCODE G8(0x6f7f)
#define OP_TYPE(i) G8(0x7005 + (i))
#define OP_ADDR(i) ecl_word(G8(0x7045 + (i)), G8(0x7085 + (i)))      // operand i taken as an address / word
#define GAME G32(0x49d2)
#define AREA G32(0x49d6)
enum { F_EQ = 0x6f78, F_NE, F_LT, F_GT, F_LE, F_GE };               // the six result flags of the last comparison

// 2366:18EB  Compare a with b (unsigned) and set the six flags.
void ecl_compare(u16 b, u16 a) {
    memset(&G8(F_EQ), 0, 6);
    if (a == b) G8(F_EQ) = 1; else G8(F_NE) = 1;
    if (a < b) G8(F_LT) = 1;
    if (b < a) G8(F_GT) = 1; else G8(F_LE) = 1;
    if (b <= a) G8(F_GE) = 1;
}

// 2366:1951  Push the script pointer on the GOSUB stack (a linked list of 6-byte heap nodes whose
// top is DS:6F70) and jump to the address in operand i.
void ecl_gosub(u8 i) {
    farptr below = G32(0x6f70);
    if (below) SETG32(0x6f74, below);
    farptr node = rtl_getmem(6);
    SETG32(0x6f70, node);
    SETP32(node, 2, below);
    SETP16(node, 0, G16(ECL_IP));
    SETG16(ECL_IP, OP_ADDR(i));
}

// 2366:1EA6  Skip the next instruction (what a false IF does). The operand counts used here are
// the original's own table; opcodes it does not list are stepped over as a single byte.
void ecl_skip(void) {
    static const u8 n[0x40] = {
        [0x01] = 1, [0x02] = 1, [0x0a] = 1, [0x0e] = 1, [0x11] = 1, [0x12] = 1, [0x1d] = 1, [0x20] = 1, [0x2d] = 1, [0x32] = 1,
        [0x34] = 1, [0x36] = 1, [0x38] = 1, [0x39] = 1, [0x3c] = 1,
        [0x03] = 2, [0x08] = 2, [0x09] = 2, [0x0c] = 2, [0x0f] = 2, [0x10] = 2, [0x1f] = 2, [0x22] = 2,
        [0x04] = 3, [0x05] = 3, [0x06] = 3, [0x07] = 3, [0x0b] = 3, [0x21] = 3, [0x28] = 3, [0x2a] = 3, [0x2f] = 3, [0x30] = 3,
        [0x35] = 3, [0x37] = 3, [0x3b] = 3,
        [0x14] = 4, [0x23] = 4, [0x2e] = 5, [0x1e] = 6, [0x2c] = 6, [0x27] = 8, [0x29] = 14 };
    farptr block = G32(0x49de);
    OPCODE = mem[((u32)FP_SEG(block) << 4) + (u16)(FP_OFF(block) + G16(ECL_IP) + 0x6700)];
    if (OPCODE < 0x40 && n[OPCODE]) ecl_fetch(n[OPCODE]);
    else SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
}

// 1D3F:00F4  GOTO addr
// 2366:1CC3  Scale the seven coin counts (+88h..+94h) of a character by a real factor, truncating.
void coins_scale(farptr ch, real48 f) {
    for (u16 o = 0x88; o <= 0x94; o += 2) SETP16(ch, o, (u16)rtl_real_trunc(rtl_real_mul(rtl_real_long(P16(ch, o)), f)));
}
// 1D3F:1DB1  ROB who, percent: the selected character (who = 0) or everyone loses `percent` of each kind of coin
// (kept: Trunc(coins * ((100 - percent) / 100)) in reals), then items by the same percent chance (2366:1DF0).
// 1D3F:2AC1  ECL CLOCK n: the game clock (unit 1) advanced by n. The original asks for the value of "operand number
// k" where k is a byte of the stack it never sets; the one script that uses CLOCK (ECL7 block 17, "ECL CLOCK 1")
// evidently means operand 1, which is what is taken here. (The only place the port does what the code plainly
// meant instead of what leftover memory made it do.)
void op_clock(void) {
    ecl_fetch(1);
    time_advance(1, (s8)(u8)ecl_operand(1));
}

// 1D3F:2C03  SPELL s, slot, count: whether the party has spell s memorised. Kept with its faults: the search never
// starts its spell-slot counter again for the next character, so only the first character is really looked at
// (running out of slots on them ends it, slot FFh); the count goes up by one even then; and the two result
// addresses are cut to their low byte (stored as bytes), so the results land in script variables 7Dh/7Eh-style
// addresses rather than the ones named. The "found" flag is never cleared in the original (stack leftovers); it is
// taken as clear here, as plainly meant. With FIX on, it does what it was meant to.
void op_spell(void) {
    ecl_fetch(3);
    u8 want = (u8)ecl_operand(1);
    u8 a_slot = (u8)ecl_word(G8(0x7047), G8(0x7087)), a_count = (u8)ecl_word(G8(0x7048), G8(0x7088));
    if (FIX) {                                   // (FIX: every character searched; the results to the variables named)
        u8 slot = 0xff, count = 0;
        for (farptr c = G32(0x5d96); c && slot == 0xff; c = P32(c, 0x104)) {
            count++;
            for (u8 k = 1; k <= 0x52; k++) if (P8(c, k + 0x17) == want) { slot = k; break; }
        }
        ecl_write_var(slot, ecl_word(G8(0x7047), G8(0x7087)));
        ecl_write_var(count, ecl_word(G8(0x7048), G8(0x7088)));
        return;
    }
    u8 slot = 1, count = 0, found = 0;
    for (farptr c = G32(0x5d96); c && !found; c = P32(c, 0x104), count++) {
        do {
            if (P8(c, slot + 0x17) == want) found = 1;
            else if (slot <= 0x51) slot++;
            else { slot = 0xff; found = 1; }
        } while (!found);
    }
    ecl_write_var(slot, a_slot);
    ecl_write_var(count, a_count);
}

// The percent operand is read twice, as in the original.
void op_rob(void) {
    ecl_fetch(3);
    u8 who = (u8)ecl_operand(1), pct = (u8)ecl_operand(2);
    real48 hundred = {0x0087, 0x0000, 0x4800};
    real48 keep = rtl_real_div(rtl_real_long((s16)(0x64 - pct)), hundred);
    u8 chance = (u8)ecl_operand(2);
    if (who == 0) { coins_scale(G32(0x5d92), keep); items_lose_chance(G32(0x5d92), chance); return; }
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) { coins_scale(c, keep); items_lose_chance(c, chance); }
}

// 1D3F:2775  DAMAGE flags, count, sides, bonus, kind: damage of count d sides + bonus (rolled afresh after each
// victim in the random mode). Flags 80h clear: `flags` times a random party member (one of DS:49D6->67Ch) is hit
// if 4592:0C4F says so (bonus `kind`). Flags 80h set, with save adjustment flags & 1Fh and saving-throw kind
// kind & 7: with 40h everyone (20h: no saving throw); else with kind 80h the selected character (saving throw
// kind - 1, none when kind & 7 is 0); else one random member. If no one is left standing (+10Dh all 0): "The END!".
// Ends with "press <enter>" and the selected character restored.
void op_damage(void) {
    farptr saved = G32(0x5d92), c = G32(0x5d96);
    u8 all = 0, pick = 0;
    ecl_fetch(5);
    u8 flags = (u8)ecl_operand(1), count = (u8)ecl_operand(2), sides = (u8)ecl_operand(3), bonus = (u8)ecl_operand(4), kind = (u8)ecl_operand(5);
    u16 dmg = (u16)(bonus + roll_dice(sides, count));
    if (flags & 0x40) all = 1; else pick = roll_dice(P8(G32(0x49d6), 0x67c), 1);
    if (flags & 0x80) {
        s8 adj = flags & 0x1f, sk = kind & 7;
        if (all) {
            for (; c; c = P32(c, 0x104))
                if ((flags & 0x20) || !rules_saving_throw(adj, sk, c)) script_damage(c, dmg);
        } else if (kind & 0x80) {
            if (!(sk && rules_saving_throw(adj, (s8)(sk - 1), G32(0x5d92)))) script_damage(G32(0x5d92), dmg);
        } else {
            for (u8 i = 2; i <= pick; i++) c = P32(c, 0x104);
            if (!rules_saving_throw(adj, sk, c)) script_damage(c, dmg);
        }
    } else if (flags >= 1) {
        for (u8 i = 1;; i++) {
            c = G32(0x5d96);
            pick = roll_dice(P8(G32(0x49d6), 0x67c), 1);
            for (u8 k = 1; k < pick; k++) c = P32(c, 0x104);
            if (rules_hit_unaimed(kind, c)) script_damage(c, dmg);
            dmg = (u16)(bonus + roll_dice(sides, count));
            if (i == flags) break;
        }
    }
    G8(0x49ff) = 1;
    for (farptr p = G32(0x5d96); p; p = P32(p, 0x104)) if (P8(p, 0x10d)) G8(0x49ff) = 0;
    if (G8(0x49ff)) {
        ui_frame(FARPTR(RSEG(0x1d3f), 0x272d), 0x0f, 0, 0x16, 0x26, 1, 1);
        G8(0x5e24) = 2; G8(0x5e25) = 2;
        text_print_wrapped(FARPTR(RSEG(0x1d3f), 0x2736), 1, 0x0a, 0x16, 0x26, 1, 1);
        rtl_delay(3000);
    }
    SETG32(0x5d92, saved);
    text_prompt(FARPTR(RSEG(0x1d3f), 0x2752), 0x0f);
}

// 1D3F:1435  CHECKPARTY what, kind, min, max, avg, found: party-wide figures written to the four variables. `what`
// is taken as an address when given as a variable (type 1), otherwise as its value: 0 asks whether anyone has
// status effect `kind` (min/max/avg 0, found 1 or 0); 6BA7h and 6C1Bh (the character fields +79h and +11Ch, by
// address) give their least, greatest and average over the party (found 0). Anything else writes nothing.
// The nested 1D3F:13D7 does the four writes.
void op_check_party(void) {
    ecl_fetch(6);
    farptr c = G32(0x5d96);
    u8 found = 0;
    u16 what = G8(0x7006) == 1 ? ecl_word(G8(0x7046), G8(0x7086)) : ecl_operand(1);
    u8 kind = (u8)ecl_operand(2);
    u16 vmin = ecl_word(G8(0x7048), G8(0x7088)), vmax = ecl_word(G8(0x7049), G8(0x7089));
    u16 vavg = ecl_word(G8(0x704a), G8(0x708a)), vfound = ecl_word(G8(0x704b), G8(0x708b));
    u16 sum = 0; u8 n = 0, lo = 0xff, hi = 0, avg = 0;
    if (what == 0) {
        for (; c && !found; c = P32(c, 0x104)) found = char_find_effect(kind, c) != 0;
        lo = hi = avg = 0;
    } else if (what == 0x6ba7 || what == 0x6c1b) {
        u16 f = what == 0x6ba7 ? 0x79 : 0x11c;
        for (; c; c = P32(c, 0x104)) {
            n++; u8 v = P8(c, f);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            sum = (u16)(sum + v);
        }
        avg = (u8)(sum / n);
    } else return;
    ecl_write_var(lo, vmin); ecl_write_var(hi, vmax); ecl_write_var(avg, vavg); ecl_write_var((u16)(s16)(s8)found, vfound);
}

// 1D3F:19B0  TREASURE copper, silver, electrum, gold, platinum, gems, jewelry, items: the seven amounts go to the
// treasure's coin table (DS:67F4, dwords), then items onto the treasure list DS:6810 (newest first, linked at +2Ah):
// below 80h a block of 3Fh-byte items from ITEM<n>.dax (n the first digit of the area number DS:5376);
// 80h + k: k items of random type, each from a d100 table (some through a second d100, d10 or d16), made by
// 3FA9:1010, after which every item on the list has its name built; FFh: none.
// Original slip: second rolls of 30h and 31h set no type, so the item takes the previous one's; for the first item
// that is an uninitialised stack byte, which cannot be reproduced under a native dispatcher (0 here).
static void treasure_push(farptr local) {
    farptr prev = G32(0x6810), it = rtl_getmem(0x3f);
    SETG32(0x6810, it);
    memmove(&mem[LIN(it)], &mem[LIN(local)], 0x3f);
    SETP32(it, 0x2a, prev);
}
void op_treasure(void) {
    ecl_fetch(8);
    for (u8 i = 0; i <= 6; i++) SETG32(0x67f4 + i * 4, ecl_operand((u8)(i + 1)));
    u8 what = (u8)ecl_operand(8);
    farptr local = scratch_push(0x40);
    if (what < 0x80) {
        char name[8], num[8]; snprintf(num, sizeof num, "%u", G8(0x5376)); snprintf(name, sizeof name, "ITEM%c", num[0]);
        farptr v = scratch_push(6);
        dax_load(v, v + 4, what, ".dax", name);
        u16 size = rd16(LIN(v) + 4); farptr data = rd32(LIN(v));
        scratch_pop(6);
        if (size == 0) { text_prompt(FARPTR(RSEG(0x1d3f), 0x1997), 0x0f); call_original(0x1899, 0x0000, 0); }
        u16 off = 0;
        do {
            memmove(&mem[LIN(local)], &mem[LIN(data) + off], 0x3f);
            SETP32(local, 0x2a, 0);
            off = (u16)(off + 0x3f);
            treasure_push(local);
        } while (off < size);
        rtl_freemem(data, size);
    } else if (what != 0xff) {
        u8 n = (u8)(what - 0x80), type = 0;
        for (u8 i = 1; i <= n && n >= 1; i++) {
            u8 r = roll_dice(0x64, 1);
            if (r >= 1 && r <= 0x3c) {
                u8 r2 = roll_dice(0x64, 1);
                if ((r2 >= 1 && r2 <= 0x2f) || (r2 >= 0x32 && r2 <= 0x3b)) type = r2 == 0x2d ? 0x3b : r2;
                else if (r2 >= 0x3c && r2 <= 0x5a) {
                    u8 r3 = roll_dice(0x0a, 1);
                    if (r3 >= 1 && r3 <= 4) type = 0x24; else if (r3 >= 5 && r3 <= 7) type = 0x23;
                    else if (r3 == 8) type = 0x22; else if (r3 == 9) type = 0x25; else if (r3 == 10) type = 0x26;
                } else if (r2 >= 0x5b && r2 <= 0x5e) type = 0x49;
                else if (r2 >= 0x5f && r2 <= 0x61) type = 0x5d;
                else if (r2 >= 0x62 && r2 <= 0x64) type = 0x4d;
            } else if (r >= 0x3d && r <= 0x55) type = 0x3d;
            else if (r >= 0x56 && r <= 0x5c) type = 0x3e;
            else if (r >= 0x5b && r <= 0x62) {
                u8 r4 = roll_dice(0x10, 1);
                if (r4 >= 1 && r4 <= 7) type = 0x47; else if (r4 >= 8 && r4 <= 9) type = 0x46; else if (r4 == 10) type = 0x54;
                else if (r4 == 11 || r4 == 12) type = 0x4e; else if (r4 == 13 || r4 == 14) type = 0x4f;
                else if (r4 == 15) type = 0x5c; else if (r4 == 16) type = 0x43;
            } else if (r == 0x63 || r == 0x64) type = 0x45;
            item_make(local, type);
            treasure_push(local);
        }
        for (farptr p = G32(0x6810); p; p = P32(p, 0x2a)) chr_item_name(0, 0, 0, 0, p);
    }
    scratch_pop(0x40);
}

void op_goto(void) { ecl_fetch(1); SETG16(ECL_IP, OP_ADDR(1)); }

// 1D3F:0111  GOSUB addr
void op_gosub(void) { ecl_fetch(1); ecl_gosub(1); }

// 1D3F:0A9F  RETURN: pop the GOSUB stack; with an empty stack it does nothing but step on.
void op_return(void) {
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
    farptr node = G32(0x6f70);
    if (!node) return;
    SETG16(ECL_IP, P16(node, 0));
    farptr below = P32(node, 2);
    SETG32(0x6f74, below);
    rtl_freemem(node, 6);
    SETG32(0x6f70, below);
}

// 1D3F:0126  COMPARE a, b: numbers, or two texts if either operand is text.
void op_compare(void) {
    ecl_fetch(2);
    if (OP_TYPE(1) < 0x80 && OP_TYPE(2) < 0x80) { u16 a = ecl_operand(1), b = ecl_operand(2); ecl_compare(b, a); }
    else ecl_compare_text();
}

// 1D3F:0AED  COMPARE AND a, b, c, d: "equal" only if a = b and c = d.
void op_compare_and(void) {
    u16 v[5];
    memset(&G8(F_EQ), 0, 6);
    ecl_fetch(4);
    for (u8 i = 1; i <= 4; i++) v[i] = ecl_operand(i);
    if (v[1] == v[2] && v[3] == v[4]) G8(F_EQ) = 1; else G8(F_NE) = 1;
}

// 1D3F:0B52  IF =, <>, <, >, <=, >= : run the next instruction only if that flag is set.
void op_if(void) {
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
    if (OPCODE >= 0x16 && OPCODE <= 0x1b && G8(F_EQ + (OPCODE - 0x16)) == 0) ecl_skip();
}

// 1D3F:01A3  ADD / SUBTRACT / DIVIDE / MULTIPLY a, b, dest (16-bit unsigned; SUBTRACT gives b - a,
// DIVIDE gives a / b).
void op_arith(void) {
    ecl_fetch(3);
    u16 a = ecl_operand(1), b = ecl_operand(2), dest = OP_ADDR(3), r = 0;
    switch (OPCODE) {
    case 4: r = (u16)(a + b); break;
    case 5: r = (u16)(b - a); break;
    case 6: r = b ? (u16)(a / b) : 0; break;             // the original stops with a runtime error on b = 0
    case 7: r = (u16)(a * b); break;
    }
    ecl_write_var(r, dest);
}

// 1D3F:0DEF  AND / OR a, b, dest (bytes); also sets the comparison flags against zero.
void op_and_or(void) {
    ecl_fetch(3);
    u8 a = (u8)ecl_operand(1), b = (u8)ecl_operand(2);
    u16 dest = OP_ADDR(3);
    u8 r = (OPCODE == 0x2f) ? (a & b) : (a | b);
    ecl_compare(r, 0);
    ecl_write_var(r, dest);
}

// 1D3F:0234  RANDOM n, dest: a random number from 0 to n (n = 255 gives 0 to 254).
void op_random(void) {
    ecl_fetch(2);
    u8 n = (u8)ecl_operand(1);
    if (n < 0xff) n++;
    u16 dest = OP_ADDR(2);
    ecl_write_var((u8)rtl_random(n), dest);
}

// 1D3F:0292  SAVE value, dest (a number, or a text into a text variable).
void op_save(void) {
    ecl_fetch(2);
    u16 dest = OP_ADDR(2);
    if (OP_TYPE(1) < 0x80) ecl_write_var(ecl_operand(1), dest);
    else ecl_store_text(&G8(0x70c8), dest);
}

// 1D3F:0E5E  GETTABLE table, index, dest: dest = the variable at table + index.
void op_gettable(void) {
    ecl_fetch(3);
    u16 table = OP_ADDR(1), index = ecl_operand(2) & 0xff, dest = OP_ADDR(3);
    ecl_write_var(ecl_read_var((u16)(table + index)), dest);
}

// 1D3F:0EBC  SAVE TABLE value, table, index: the variable at table + index = value.
void op_savetable(void) {
    ecl_fetch(3);
    u16 value = ecl_operand(1), table = OP_ADDR(2), index = ecl_operand(3);
    ecl_write_var(value, (u16)(table + index));
}

// 1D3F:18F8  ON GOTO / ON GOSUB selector, n, addr0 ... addr(n-1): branch to the selector-th
// address; if the selector is n or more, carry on with the next instruction.
void op_on_goto(void) {
    ecl_fetch(2);
    u8 sel = (u8)ecl_operand(1), n = (u8)ecl_operand(2);
    SETG16(ECL_IP, (u16)(G16(ECL_IP) - 1));              // the address list is fetched as a second set of operands
    ecl_fetch(n);
    if (sel < n) {
        if (OPCODE == 0x25) SETG16(ECL_IP, OP_ADDR(sel + 1));
        else ecl_gosub((u8)(sel + 1));
    }
}

// 1D3F:0A09  PRINT / PRINTCLEAR text-or-number: word-wrapped into the text window (columns 1-38,
// rows 17-22) in light green, one letter at a time. PRINTCLEAR empties the window first.
void op_print(void) {
    ecl_fetch(1);
    G8(0x84e4) = 0;
    G8(0x4a00) = 1;                                      // letter-by-letter printing on
    if (OP_TYPE(1) < 0x80) {                             // a number: print its decimal digits
        char d[8]; int n = snprintf(d, sizeof d, "%u", ecl_operand(1));
        G8(0x70c8) = (u8)n; memcpy(&G8(0x70c9), d, (size_t)n);
    }
    if (OPCODE == 0x11) text_print_wrapped(FARPTR(DATA_SEG, 0x70c8), 0, 10, 22, 38, 17, 1);
    else { G8(0x5e25) = 17; G8(0x5e24) = 1; text_print_wrapped(FARPTR(DATA_SEG, 0x70c8), 1, 10, 22, 38, 17, 1); }
    G8(0x4a00) = 0;
}

// 1D3F:2AEB  PRINT RETURN: move the text cursor to the start of the next line.
void op_print_return(void) {
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
    G8(0x5e24) = 1; G8(0x5e25)++;
    G8(0x84df) = 0;
}

// 1D3F:2B14  CLEAR BOX: blank the picture window (cells 1,1 to 15,15).
void op_clear_box(void) {
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
    text_clear_box(1, 1, 15, 15);
    G8(0x84de) = 1;
}

// 1D3F:271F  DELAY: the standard short pause.
void op_delay(void) { SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1)); text_pause(); }

// 1D3F:2AA2  SPRITE OFF: if a monster picture is covering the 3D view, redraw the view.
void op_sprite_off(void) {
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
    if (G8(0x84e3)) { view_redraw(); G8(0x84e3) = 0; G8(0x84e0) = 0; }
}

// 1D3F:0001  EXIT: end this run of the script. Restores the screen frame if a script changed it,
// puts back the selected character, empties the GOSUB stack and homes the text cursor.
void op_exit(void) {
    if (G8(0x84e2)) {
        ui_frame(FARPTR(RSEG(0x1d3f), 0), 15, 0, 15, 15, 1, 1);
        if (P16(G32(0x49d2), 0x1cc) != 0) ui_frame(FARPTR(RSEG(0x1d3f), 0), 15, 0, 13, 13, 3, 3);
    }
    if (G8(0x4430)) { SETG32(0x5d92, G32(0x4436)); G8(0x4430) = 0; }
    SETG16(0x84c8, 0);
    G8(0x84e0) = 0; G8(0x84e2) = 0;
    G8(0x442e) = 1;                                      // tells the run loop to stop
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
    while (G32(0x6f70)) {
        farptr node = G32(0x6f70), below = P32(node, 2);
        SETG32(0x6f74, below);
        rtl_freemem(node, 6);
        SETG32(0x6f70, below);
    }
    G8(0x5e25) = 17; G8(0x5e24) = 1;
    G8(0x84de) = 0;
}

// 1D3F:0951  INPUT NUMBER ?, dest: ask for a number on the bottom line.
void op_input_number(void) {
    ecl_fetch(2);
    u16 dest = OP_ADDR(2);
    ecl_write_var(text_input_number(10, FARPTR(RSEG(0x1d3f), 0)), dest);
}

// 1D3F:0994  INPUT STRING ?, dest: ask for a line of text; an empty answer becomes a single space.
void op_input_string(void) {
    u8 empty = 0, answer[64];
    ecl_fetch(2);
    u16 dest = OP_ADDR(2);
    text_input_pstr(40, 10, &empty, answer);
    if (answer[0] == 0) { answer[0] = 1; answer[1] = ' '; }
    ecl_store_text(answer, dest);
}

// 1D3F:12C6  PARTYSTRENGTH dest: a rough measure of how strong the party is, summed over its
// members from five fields of each character record (kept in a byte, so it can wrap).
void op_party_strength(void) {
    ecl_fetch(1);
    u8 total = 0;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) {
        u8 a = P8(ch, 0x111) < 0x3d ? 0 : (u8)(P8(ch, 0x111) - 0x3c);
        u8 b = P8(ch, 0x110) < 0x28 ? 0 : (u8)(P8(ch, 0x110) - 0x27);
        total = (u8)(total + (s16)(P8(ch, 0x96) * 4 + P8(ch, 0x11b) + a * 5 + b * 5 + P8(ch, 0x9b) * 8) / 10);
    }
    ecl_write_var(total, OP_ADDR(1));
}

// 1D3F:02E0  LOAD CHARACTER n: make the n-th party member (0-based, low 7 bits) the selected
// character. With bit 7 set, and if the script has just zeroed both of that character's status
// addresses, the character is removed from the party.
void op_load_character(void) {
    G8(0x4430) = 1;                                      // EXIT will put the previous selection back
    ecl_fetch(1);
    u8 v = (u8)ecl_operand(1), n = v & 0x7f;
    farptr ch = G32(0x5d96);
    for (u8 i = 0; i < n && ch; i++) ch = P32(ch, 0x104);
    if (ch) SETG32(0x5d92, ch);
    if ((v & 0x80) && G8(0x84d2) && G8(0x84d3)) {
        if (G32(0x4436) == G32(0x5d92)) G8(0x4430) = 0;
        party_remove(0, 1);
        hud_draw_roster(G32(0x5d92));
        G8(0x84d2) = 0; G8(0x84d3) = 0;
    }
}

// 1D3F:2CCF  CALL addr. In the game's first version this called a machine-code routine at that
// address. Here the six addresses the scripts use are recognised and their effect is carried out
// directly; any other address does nothing.
void op_call(void) {
    ecl_fetch(1);
    switch (OP_ADDR(1)) {
    case 0x2c90:                                         // note the event number of the party's square, then refresh the view if anything changed
        G8(0x6ab1) = map_event((s8)G8(0x6aae), (s8)G8(0x6aad));
        if (G8(0x4431) && (G8(0x84e0) || G8(0x84e3) || G8(0x84e5) || G8(0x84e6) || G8(0x84e8))) {
            view_redraw();
            hud_draw_status();
            G8(0x84e8) = 0; G8(0x84e5) = 0; G8(0x84e6) = 0; G8(0x84e0) = 0; G8(0x84e3) = 0;
        }
        break;
    case 0x8000: duel_setup(1); break;
    case 0x8001: duel_setup(0); break;
    case 0xba03:                                         // a sound effect chosen by script variable 03DE
        if (G16(0x84cc) == 8) sound_play(G16(0x2628)); else if (G16(0x84cc) == 10) sound_play(G16(0x262a));
        break;
    case 0xc01e: step_forward(); break;
    case 0xc018:                                         // note the wall type directly ahead of the party
        if (G8(0x49fa) == 1) G8(0x6ab0) = map_wall(G8(0x6aaf), (s8)G8(0x6aae), (s8)G8(0x6aad));
        break;
    }
}

// 1D3F:0856  PICTURE n: show scene picture n from the PIC archive in the picture window (or hand
// over to the sprite routine when a monster set is active). n = 255 takes the picture away again.
void op_picture(void) {
    ecl_fetch(1);
    u8 pic = (u8)ecl_operand(1);
    if (P16(G32(0x49d2), 0x1cc) == 0 && pic != 0xff) ui_frame(FARPTR(RSEG(0x1d3f), 0x0851), 15, 0, 13, 13, 3, 3);
    if (pic == 0xff) {
        if ((G8(0x49f9) > 1 || G8(0x49fa) == 1) && (G8(0x84e0) || G8(0x84e3))) { view_redraw(); G8(0x84e0) = 0; G8(0x84e3) = 0; G8(0x84e1) = 1; }
        G8(0x84c8) = 0; G8(0x84c9) = 0;
        return;
    }
    G8(0x84c9) = 1; G8(0x84e0) = 1;
    if (P16(G32(0x49d6), 0x5c2) == 0xff) {
        if (!scene_load_anim(FARPTR(DATA_SEG, 0x6abe), 0, pic, "PIC"))      // archive missing: let the original ask for the disk
            call_original(0x489f, 0x00a2, 6, RSEG(0x1d3f), 0x0852, pic, 0, DATA_SEG, 0x6abe);
        scene_draw(G32(0x6ac4), 1, 3, 3);
    } else call_original(0x2366, 0x0511, 2, (u8)P16(G32(0x49d6), 0x5c2), pic);
}

// 1D3F:0BD1  NEWECL n: leave this area's script for script block n (moving to another area).
// The block is loaded and initialised, and both flags are set that make the run loop stop and the
// exploration loop start the new script from its entry point.
void op_newecl(void) {
    ecl_fetch(1);
    SETP16(G32(0x49d2), 0x1e4, G8(0x84dc));              // remember the area being left
    u8 id = (u8)ecl_operand(1);
    G8(0x84dc) = id;
    u8 path[64]; int n = snprintf((char *)path + 1, sizeof path - 1, "%sECL%c.dax", pstr(FARPTR(DATA_SEG, 0x547c)), '0' + G8(0x5376) % 10);
    path[0] = (u8)n;
    call_original_pstr(0x3941, 0x22d5, path);            // make sure the archive can be found
    ecl_load_block(id);
    ecl_init_script();
    G8(0x442e) = 1; G8(0x442f) = 1; G8(0x4431) = 0;
}

// 1D3F:0C74  LOAD FILES a, b, c  /  LOAD PIECES a, b, c: bring in the art an area needs. LOAD FILES
// names the area's 3D wall set (a) and whether the standard frame pictures are wanted (c);
// LOAD PIECES names up to three sets of map pieces. 255 means "none", 127 "the default".
void op_load_files(void) {
    u8 v[4];
    ecl_fetch(3);
    G8(0x4431) = 1;
    for (u8 i = 1; i <= 3; i++) v[i] = (u8)ecl_operand(i);
    farptr game = G32(0x49d2);
    if (OPCODE == 0x21) {
        G8(0x4434) = 1;
        if (v[1] != 0xff && v[1] != 0x7f && P16(game, 0x1cc) != 0) {
            SETP16(game, 0x18a, v[1]);
            call_original(0x491c, 0x10c7, 1, v[1]);
            SETP16(G32(0x49d6), 0x592, 0);
        }
        if (v[3] != 0xff && P16(game, 0x1cc) == 0) call_original(0x4c2b, 0x0022, 4, RSEG(0x1d3f), 0x0c6d, 0, 0x28);   // "BACPAC"
    } else {
        G8(0x4433) = 1;
        if (v[1] == 0x7f) map_load_part(1, 0);
        else if (P16(game, 0x1ce) == 0 || P16(game, 0x1d0) == 0) {
            for (u8 i = 1; i <= 3; i++) {
                if (v[i] == 0xff) { SETG16(0x6aae + i * 4, 0xffff); SETG16(0x6ab0 + i * 4, 0xffff); }
                else map_load_part(i, v[i]);
            }
        } else { map_load_part(1, v[1]); map_load_part(3, v[3]); }
    }
    if (G8(0x4432) && G8(0x4433) && G8(0x4434)) {
        hud_draw_screen();
        hud_draw_roster(G32(0x5d92));
        G8(0x4432) = 0;
    }
}

// 1D3F:125D  CLEARMONSTERS: forget the monsters set up for the next encounter (a 28-byte block of
// counters and the linked list of 63-byte monster entries at DS:6810).
void op_clear_monsters(void) {
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
    G8(0x4435) = 0; G8(0x84e7) = 0; G8(0x6f83) = 8;
    memset(&G8(0x67f4), 0, 0x1c);
    while (G32(0x6810)) {
        farptr node = G32(0x6810), next = P32(node, 0x2a);
        rtl_freemem(node, 0x3f);
        SETG32(0x6810, next);
    }
}

// 1D3F:1629  PARTY SURPRISE a, b: a = 1 if anyone in the party is a Ranger or a Cleric/Ranger
// (class numbers 4 and 10 in character byte 2Fh), else 0; b = 0. This is the rule that a ranger
// makes the party harder to surprise.
void op_party_surprise(void) {
    ecl_fetch(2);
    u8 flag = 0;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) if (P8(ch, 0x2f) == 4 || P8(ch, 0x2f) == 10) flag = 1;
    u16 a = OP_ADDR(1), b = OP_ADDR(2);
    ecl_write_var(flag, a);
    ecl_write_var(0, b);
}

// 1D3F:16C9  SURPRISE a, b, c, d: roll a six-sided die for each side and store who is surprised
// in script variable 6DCB: 0 nobody, 1 or 3 one side, 2 the other. The four operands shift the odds.
void op_surprise(void) {
    s8 v[5];
    ecl_fetch(4);
    for (u8 i = 1; i <= 4; i++) v[i] = (s8)ecl_operand(i);
    s8 first = (s8)(v[4] + 2 - v[1]);
    s8 second = (s8)((u8)v[2] + 2 - (u8)v[3]);
    u8 roll1 = (u8)call_original(0x4592, 0x0de3, 2, 1, 6);            // 1d6
    u8 roll2 = (u8)call_original(0x4592, 0x0de3, 2, 1, 6);
    u8 result = 0;
    if ((s16)roll1 <= (s16)first) result = ((s16)second < (s16)roll2) ? 1 : 3;
    if ((s16)roll2 <= (s16)second) result = 2;
    ecl_write_var(result, 0x6dcb);
}

// 1D3F:179B  COMBAT: fight the monsters that have been set up. The fight itself, and the two
// special kinds of battle, are still original code.
void op_combat(void) {
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
    if (!G8(0x84e7) && !G8(0x84d4)) {
        if (P16(AREA, 0x6d8) == 1) { SETP16(AREA, 0x6d8, 0); call_original(0x22ec, 0x053e, 0); }
        else if (P16(AREA, 0x5c4) == 1) { SETP16(AREA, 0x5c4, 0); call_original(0x2090, 0x0d09, 0); }
        else combat_end();
    } else {
        u16 d = (u16)dist_ahead((s8)G8(0x6aad), (s8)G8(0x6aae), G8(0x6aaf)) & 0xff;
        if (d < P16(AREA, 0x582)) SETP16(AREA, 0x582, d);
        call_original(0x2588, 0x007a, 0);
        combat_end();
        if (P16(GAME, 0x1cc) == 0) {                     // back from the battle map: reload the 3D view's pictures
            call_original(0x4c2b, 0x0022, 4, RSEG(0x1d3f), 0x178f, 0, 0x28);
            sprite_load(FARPTR(RSEG(0x1d3f), 0x1796), 0, 9);
            sprite_load(FARPTR(RSEG(0x1d3f), 0x1796), 1, 10);
            sprite_load(FARPTR(RSEG(0x1d3f), 0x1796), 2, 11);
        }
        if (G8(0x84d4)) G8(0x84d4) = 0;
    }
    G8(0x49f3) = (G8(0x49fa) == 1) ? 4 : 3;
    SETP16(AREA, 0x594, P16(AREA, 0x594) & 1);
    SETG16(0x84c8, 0);
    G8(0x84e0) = 0;
    hud_draw_screen();
}

// 1D3F:0450  LOAD MONSTER id, count, icon: add `count` copies of monster `id` to the coming fight.
// A monster is the same 285-byte record as a player character, and monsters are appended to the
// party's own linked list (next pointer at +104h). The first copy is the record the loader returns;
// each further copy is cloned from it, together with its item list (63-byte nodes from +C8h,
// linked at +2Ah) and its status-effect list (9-byte nodes from +7Fh, linked at +5). At most 63
// combatants are created (counter DS:4435). `icon` picks the combat figure, loaded into picture
// slot DS:6F83, which then moves on to the next slot.
void op_load_monster(void) {
    farptr selected = G32(0x5d92);
    u8 template[0x11d];
    ecl_fetch(3);
    if (G8(0x4435) >= 0x3f) return;
    u8 id = (u8)ecl_operand(1);
    farptr rec, items, effects;
    monster_load(id, &rec, &items, &effects);
    memcpy(template, &mem[LIN(rec)], sizeof template);
    u8 count = (u8)ecl_operand(2); if (count == 0) count = 1;
    u8 icon = (u8)ecl_operand(3);
    farptr tail = G32(0x5d96);
    sprite_load(FARPTR(RSEG(0x1d3f), 0x044b), icon, G8(0x6f83));          // "CPIC"
    while (P32(tail, 0x104)) tail = P32(tail, 0x104);
    SETP32(tail, 0x104, rec);
    tail = rec;
    P8(tail, 0xbf) = G8(0x6f83);
    SETP32(tail, 0x104, 0);
    G8(0x4435)++;
    for (u8 n = 2; n <= count && G8(0x4435) < 0x3f; n++) {
        farptr clone = rtl_getmem(0x11d);
        SETP32(tail, 0x104, clone);
        tail = clone;
        memcpy(&mem[LIN(clone)], template, sizeof template);
        P8(clone, 0xbf) = G8(0x6f83);
        SETP32(clone, 0x104, 0); SETP32(clone, 0x7f, 0); SETP32(clone, 0xc8, 0);
        G8(0x4435)++;
        for (farptr it = items; it; it = P32(it, 0x2a)) {            // each copy pushes on the front, so the list comes out reversed
            farptr below = P32(clone, 0xc8), node = rtl_getmem(0x3f);
            SETP32(clone, 0xc8, node);
            memcpy(&mem[LIN(node)], &mem[LIN(it)], 0x3f);
            SETP32(node, 0x2a, below);
        }
        for (farptr ef = effects; ef; ef = P32(ef, 5)) {
            farptr below = P32(clone, 0x7f), node = rtl_getmem(9);
            SETP32(clone, 0x7f, node);
            memcpy(&mem[LIN(node)], &mem[LIN(ef)], 9);
            SETP32(node, 5, below);
        }
    }
    G8(0x6f83)++;
    G8(0x84e7) = 1;
    SETG32(0x5d92, selected);
}

// ---- Menus put up by scripts ----
// Script menus arrive as upper-case words, each with a ~ in front of the letter that selects it
// ("~COMBAT ~WAIT ~FLEE"). The bottom-line menu wants the opposite convention, a capital for the
// hot key and lower case for the rest ("Combat Wait Flee"), so the text is converted first.

// 2366:156D  Convert `s` in place and collect the hot keys, in order, in `keys`.
void ecl_menu_prepare(u8 *keys, u8 *s) {
    keys[0] = 0;
    u8 len = s[0];
    for (u8 i = 1; i <= len && len != 0; i++) {
        if ((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'Z')) s[i] = (u8)(s[i] + 0x20);
        if (s[i] == '~') {
            if (i <= s[0]) { memmove(s + i, s + i + 1, (size_t)(s[0] - i)); s[0]--; }   // Delete(s, i, 1)
            if (s[i] >= 'a' && s[i] <= 'z') s[i] = (u8)(s[i] - 0x20);
            if (keys[0] < 40) keys[++keys[0]] = s[i];
        }
        if (i == len) break;
    }
}

// 2366:1684  Show a script menu on the bottom line and return the number (from 0) of the item
// chosen, 0 for Return when that is allowed, or 255 for a key that is not one of the items.
// Movement keys pressed meanwhile are passed to the party-order routine and otherwise ignored.
u8 ecl_menu(u8 animate, u8 allow_return, u8 hi_color, u8 color, u8 prompt_color, const u8 *items_in, const u8 *prompt_in) {
    u8 prompt[41], items[256], keys[42], special, k;
    prompt[0] = prompt_in[0] > 40 ? 40 : prompt_in[0]; memcpy(prompt + 1, prompt_in + 1, prompt[0]);
    memcpy(items, items_in, 256);
    ecl_menu_prepare(keys, items);
    for (;;) {
        k = menu_horizontal(&special, animate, 1, hi_color, color, prompt_color, items, prompt);
        if (special) {
            call_original(0x3c10, 0x27df, 1, k);
            hud_draw_roster(G32(0x5d92));
            k = 0;
        }
        if ((k >= '0' && k <= '9') || (k >= 'A' && k <= 'Z')) break;
        if (k == 0x0d && allow_return) break;
    }
    if (k == 0x0d) return 0;
    for (u8 i = 1; i <= keys[0]; i++) if (keys[i] == k) return (u8)(i - 1);
    return 0xff;
}

// 1D3F:10D1  HORIZONTAL MENU dest, n, text1 ... textn: offer n choices on the bottom line and store
// the number of the one picked. A menu of one item is a "press a key" pause, and for that case the
// old wording "PRESS <RETURN> OR BUTTON TO CONTINUE" is swapped for the PC's own.
void op_horizontal_menu(void) {
    u8 items[256], empty = 0;
    ecl_fetch(2);
    u16 dest = OP_ADDR(1);
    u8 n = (u8)ecl_operand(2);
    SETG16(ECL_IP, (u16)(G16(ECL_IP) - 1));
    ecl_fetch(n);
    u8 allow_return, color;
    if (n == 1) {
        allow_return = 1; color = 15;
        u8 *t = &G8(0x70c8), *old = &mem[LIN(FARPTR(RSEG(0x1d3f), 0x1085))], *pc = &mem[LIN(FARPTR(RSEG(0x1d3f), 0x10aa))];
        if (t[0] == old[0] && !memcmp(t + 1, old + 1, old[0])) memcpy(t, pc, (size_t)pc[0] + 1);
    } else { allow_return = 0; color = 10; }
    u8 animate = (G8(0x84e0) && G8(0x84e1)) ? 1 : 0;
    items[0] = 0;
#define APPEND(ch) do { if (items[0] < 50) items[++items[0]] = (ch); } while (0)
    for (u8 i = 1; i <= n && n != 0; i++) {
        const u8 *t = &G8(0x6fc8 + i * 0x100);
        APPEND('~');
        for (u16 j = 1; j <= t[0]; j++) APPEND(t[j]);
        if (i < n) APPEND(' ');
    }
#undef APPEND
    u8 r = ecl_menu(animate, allow_return, 15, color, 13, items, &empty);
    ecl_write_var(r, dest);
    text_clear_box(0, 24, 39, 24);
}

// 1D3F:1EF8  ENCOUNTER MENU sprite, max distance, picture, dest, r0..r4, text0, text1, text2, x, y
// The meeting with monsters before any fight. The monsters are shown at their distance (0 to 2
// squares, limited by how far the party can see ahead) with the description for that distance, and
// the player picks Combat, Wait, Flee, or Advance (Parlay once they are adjacent). r0..r4 say how
// these monsters respond to each of the five choices; the table below is that logic. Waiting and
// advancing bring the monsters a step closer and ask again. The outcome goes to `dest`:
// 0 the monsters ran, 1 a fight, 2 the party got away, 3 a parley.
void op_encounter_menu(void) {
    u8 react[5], text[4][256], shown[256], menu[41], empty = 0;
    static const u8 both_wait[] = "\x10" "Both sides wait.", monsters_flee[] = "\x12" "The monsters flee.";
    u8 party_a, party_b;
    G8(0x84e9) = 1; G8(0x84e4) = 0; G8(0x4a00) = 1;
    party_measures(&party_a, &party_b);
    ecl_fetch(14);
    G8(0x6f81) = (u8)ecl_operand(1);
    SETP16(AREA, 0x580, ecl_operand(2));
    G8(0x6f82) = (u8)ecl_operand(3);
    u16 dest = OP_ADDR(4);
    for (u8 i = 0; i <= 4; i++) react[i] = (u8)ecl_operand((u8)(i + 5));
    for (u8 i = 1; i <= 3; i++) memcpy(text[i], &G8(0x6fc8 + i * 0x100), 256);
    u8 limit_a = (u8)ecl_operand(13), limit_b = (u8)ecl_operand(14);
    shown[0] = 0;
    SETP16(AREA, 0x582, (u16)dist_ahead((s8)G8(0x6aad), (s8)G8(0x6aae), G8(0x6aaf)) & 0xff);   // how far ahead is clear
    if (P16(AREA, 0x580) < P16(AREA, 0x582)) SETP16(AREA, 0x582, P16(AREA, 0x580));
#define DIST P16(AREA, 0x582)
#define SHOW_MONSTERS() show_monsters(FARPTR(DATA_SEG, 0x84c8), (u8)DIST, G8(0x6f82), G8(0x6f81))
#define SAY(pstr) text_print_wrapped_pstr(pstr, 1, 10, 22, 38, 17, 1)
#define CLOSER() do { SETP16(AREA, 0x582, (u16)(DIST - 1)); SHOW_MONSTERS(); } while (0)
    SHOW_MONSTERS();
    for (;;) {
        u8 animate = (G8(0x84e0) && G8(0x84e1)) ? 1 : 0, again = 0;
        G8(0x5e24) = 1; G8(0x5e25) = 17;
        if (DIST <= 2) {                                 // the description for this distance, or failing that the next one that exists
            u8 start = (u8)(DIST + 1), i = start;
            do {
                memcpy(shown, text[i], 256);
                i++;
                if (DIST == 0) { if (shown[0] != 0 || i >= 4) break; }
                else { if (i > 3) i = 1; if (shown[0] != 0 || i == start) break; }
            } while (1);
        }
        text_print_wrapped_pstr(shown, shown[0] != 0, 10, 22, 38, 17, 1);
        int adjacent = (DIST == 0 || P16(GAME, 0x1cc) == 0);
        const u8 *items = &mem[LIN(FARPTR(RSEG(0x1d3f), adjacent ? 0x1e9b : 0x1eb7))];   // "...~PARLAY" or "...~ADVANCE"
        memcpy(menu, items, (size_t)items[0] + 1);
        u8 choice = ecl_menu(animate, 0, 15, 10, 13, menu, &empty);     // 0 Combat, 1 Wait, 2 Flee, 3 Advance
        if (adjacent && choice == 3) choice = 4;                        // 4 Parlay
        u8 r = choice <= 4 ? react[choice] : 0xff;
        switch (r) {
        case 0:
            if (choice == 2) ecl_write_var(party_a < limit_a ? 1 : 2, dest);
            else ecl_write_var(1, dest);
            break;
        case 1:
            if (choice == 0) ecl_write_var(1, dest);
            else if (choice == 1) { again = 1; SAY(both_wait); }
            else if (choice == 2) ecl_write_var(2, dest);
            else if (choice == 3) { if (DIST == 0) SAY(both_wait); else CLOSER(); again = 1; }
            else if (choice == 4) { if (DIST == 0) ecl_write_var(3, dest); else { CLOSER(); again = 1; } }
            break;
        case 2:
            if (choice == 0 && !(party_b < limit_b)) ecl_write_var(1, dest);
            else if (choice <= 4) { ecl_write_var(0, dest); G8(0x5e24) = 1; G8(0x5e25) = 17; SAY(monsters_flee); }
            break;
        case 3:
            if (choice == 0) ecl_write_var(1, dest);
            else if (choice == 1 || choice == 3) { if (DIST == 0) SAY(both_wait); else CLOSER(); again = 1; }
            else if (choice == 2) ecl_write_var(2, dest);
            else if (choice == 4) { if (DIST == 0) ecl_write_var(3, dest); else { CLOSER(); again = 1; } }
            break;
        case 4:
            if (choice == 0) ecl_write_var(1, dest);
            else if (choice == 1 || choice == 3 || choice == 4) { if (DIST == 0) ecl_write_var(3, dest); else { CLOSER(); again = 1; } }
            else if (choice == 2) ecl_write_var(2, dest);
            break;
        }
        if (!again) break;
    }
#undef DIST
#undef SHOW_MONSTERS
#undef SAY
#undef CLOSER
    text_clear_box(0, 24, 39, 24);
    G8(0x4a00) = 0; G8(0x84e9) = 0;
}

// 1D3F:25CB  PARLAY v0, v1, v2, v3, v4, dest: ask what manner the party takes (Haughty, Sly, Nice,
// Meek or Abusive) and store the script's value for that choice.
void op_parlay(void) {
    static const u8 blank[] = "\x01 ";
    u8 v[5];
    ecl_fetch(6);
    for (u8 i = 0; i <= 4; i++) v[i] = (u8)ecl_operand((u8)(i + 1));
    u8 choice = ecl_menu(0, 0, 15, 10, 13, &mem[LIN(FARPTR(RSEG(0x1d3f), 0x25a8))], blank);   // "~HAUGHTY ~SLY ~NICE ~MEEK ~ABUSIVE"
    ecl_write_var(choice <= 4 ? v[choice] : 0, OP_ADDR(6));
}

// 1D3F:0F08  VERTICAL MENU dest, title, n, item1 ... itemn: print `title` in the text window, list
// the items under it and store the number (from 0) of the one picked.
// The original frees the list starting from the picked item instead of from its head, so the
// items above the choice are never freed. That leak is reproduced here on purpose: the heap has
// to stay identical to the original's while the two are being compared (fixed when FIX is on).
void op_vertical_menu(void) {
    u8 title[256], keys[42], items[256], empty = 0, redraw = 1;
    G8(0x84e4) = 0;
    ecl_fetch(3);
    u16 dest = OP_ADDR(1);
    memcpy(title, &G8(0x70c8), 256);
    u8 n = (u8)ecl_operand(3);
    SETG16(ECL_IP, (u16)(G16(ECL_IP) - 1));
    ecl_fetch(n);
    farptr head = menu_list_new(n);
    G8(0x5e24) = 1; G8(0x5e25) = 17;
    text_print_wrapped_pstr(title, 1, 10, 22, 38, 17, 1);
    u8 i = 1;
    for (farptr node = head; node; node = P32(node, 0x2a), i++) {
        const u8 *t = &G8(0x6fc8 + i * 0x100); u8 len = t[0] > 40 ? 40 : t[0];
        P8(node, 0) = len; memcpy(&mem[LIN(node) + 1], t + 1, len);
        P8(node, 0x29) = 0;
    }
    s16 index = 0;
    farptr picked;
    items[0] = 0;
    ecl_menu_prepare(keys, items);                       // these two lines are the original's wrapper 2366:1788
    menu_list(&picked, &index, &redraw, 0, head, 22, 38, (s8)(G8(0x5e25) + 1), 1, 15, 10, 13, items, &empty);
    ecl_write_var((u16)index, dest);
    menu_list_dispose(FIX ? head : picked);              // (see above: not `head`; FIX: the whole list freed)
    text_clear_box(1, 17, 38, 22);
}

// 1D3F:03B4  SETUP MONSTER sprite, max distance, picture: show monsters ahead of the party without
// asking anything, at most as far away as the party can see.
void op_setup_monster(void) {
    ecl_fetch(3);
    G8(0x6f81) = (u8)ecl_operand(1);
    SETP16(AREA, 0x580, ecl_operand(2));
    G8(0x6f82) = (u8)ecl_operand(3);
    SETP16(AREA, 0x582, (u16)dist_ahead((s8)G8(0x6aad), (s8)G8(0x6aae), G8(0x6aaf)) & 0xff);
    if (P16(AREA, 0x580) < P16(AREA, 0x582)) SETP16(AREA, 0x582, P16(AREA, 0x580));
    show_monsters(FARPTR(DATA_SEG, 0x84c8), (u8)P16(AREA, 0x582), G8(0x6f82), G8(0x6f81));
}


// 2366:0511  Showing a close-up picture by number (the area's picture set): DS:49FD/49FE remember it.
void show_closeup(u8 pic, u8 set) {
    G8(0x84e1) = 0; G8(0x49fd) = set; G8(0x49fe) = pic;
    call_original(0x489f, 0x05d9, 2, set, pic);
    call_original(0x489f, 0x0697, 3, 3, 3, 1);
}
// 2366:054E  Showing the monsters of an encounter at `dist` squares. flags (DS:84C8): [0] sprite loaded, [1] close-up
// shown. The sprite (SPRITnn, `sprite`) is drawn in the 3D view at a size for the distance (always the middle one
// outside the dungeon view); at distance 0 in the dungeon view the close-up picture (PICnn, `pic`, or the area's own
// picture DS:49D6->5C2h when that is not FFh) replaces it, once per picture.
void show_monsters(farptr flags, u8 dist, u8 pic, u8 sprite) {
    farptr a = G32(0x49d6);
    if (P8(flags, 1) == 0 && P8(flags, 0) != 0) view_redraw();
    if (P8(flags, 1) == 0) {
        if (P8(flags, 0) == 0) {
            if (G8(0x6aac)) { G8(0x6aac) = 0; view_redraw(); }
            call_original(0x489f, 0x00a2, 6, RSEG(0x2366), 0x0544, sprite, 1, DATA_SEG, 0x6abe);          // "SPRIT"
            P8(flags, 0) = 1; G8(0x84e3) = 1;
        }
        call_original(0x489f, 0x0708, 3, G8(0x49fa) == 1 ? (u8)(dist + 1) : 2, DATA_SEG, 0x6abe);
    }
    if ((P8(flags, 1) == 0 || G8(0x84ea) != P16(a, 0x5c2)) && dist == 0 && G8(0x49fa) == 1 && (G8(0x84e9) == 0 || G8(0x49f6) != 0)) {
        G8(0x84ea) = (u8)P16(a, 0x5c2); G8(0x84e0) = 1;
        if (P16(a, 0x5c2) == 0xff) {
            call_original(0x489f, 0x00a2, 6, RSEG(0x2366), 0x054a, pic, 0, DATA_SEG, 0x6abe);              // "PIC"
            P8(flags, 1) = 1;
            scene_draw(G32(0x6ac4), 1, 3, 3);
        } else {
            show_closeup(pic, (u8)P16(a, 0x5c2));
            P8(flags, 1) = 1; G8(0x84e1) = 0;
        }
    }
}

// 1D3F:0817  APPROACH: bring the monsters one square closer and redraw them.
void op_approach(void) {
    if (P16(AREA, 0x582) != 0) {
        SETP16(AREA, 0x582, (u16)(P16(AREA, 0x582) - 1));
        show_monsters(FARPTR(DATA_SEG, 0x84c8), (u8)P16(AREA, 0x582), G8(0x6f82), G8(0x6f81));
    }
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
}

// 1D3F:266A  FIND ITEM type: sets "equal" if anyone in the party carries an item of that type
// (byte 2Eh of an item record), otherwise "not equal".
void op_find_item(void) {
    ecl_fetch(1);
    u8 type = (u8)ecl_operand(1);
    memset(&G8(F_EQ), 0, 6);
    G8(F_NE) = 1;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104))
        for (farptr it = P32(ch, 0xc8); it; it = P32(it, 0x2a))
            if (P8(it, 0x2e) == type) { G8(F_EQ) = 1; G8(F_NE) = 0; return; }
}

// 1D3F:2B33  WHO prompt: clear the text window and let the player pick a party member, who
// becomes the selected character.
void op_who(void) {
    ecl_fetch(1);
    text_clear_box(1, 17, 38, 22);
    call_original(0x10ba, 0x2c48, 5, DATA_SEG, 0x70c8, 0, DATA_SEG, 0x5d92);
}

// 1D3F:2B7E  ADD NPC id, share: load non-player character `id` into the party. `share` (halved,
// with the top bit set) goes in the character's byte 84h; NPC 24 gets a flag of its own.
void op_add_npc(void) {
    ecl_fetch(2);
    u16 id = ecl_operand(1);
    call_original(0x3941, 0x1262, 1, id);
    u8 share = (u8)ecl_operand(2);
    farptr ch = G32(0x5d92);
    P8(ch, 0x84) = (u8)((share >> 1) | 0x80);
    P8(ch, 0x10e) = ((u8)id == 0x18) ? 1 : 0;
    call_original(0x10ba, 0x0bb8, 2, FP_SEG(ch), FP_OFF(ch));
    hud_draw_roster(ch);
}

// 1D3F:2E10  PROGRAM n: run one of three built-in screens: 0, 8 (both still original code), or
// 9, which is Encamp followed by the end of the script.
void op_program(void) {
    if (G8(0x4430)) { SETG32(0x5d92, G32(0x4436)); G8(0x4430) = 0; }
    ecl_fetch(1);
    u8 n = (u8)ecl_operand(1);
    if (n == 0) { party_menu(); hud_draw_screen(); }
    else if (n == 8) call_original(0x3b80, 0x02a4, 0);
    else if (n == 9) {
        u16 ip = G16(ECL_IP);
        explore_camp();
        SETG16(ECL_IP, ip);
        op_exit();
    }
}

// 1D3F:2E7D  PROTECTION table: draw a row of raw font glyphs at the text cursor, one per script
// variable starting at `table`, stopping at a value of 0 or 255. (Glyph number = value + 1; these
// are the rune symbols, which is presumably where the opcode's name comes from.)
void op_protection(void) {
    SETG16(0x84c8, 0);
    G8(0x84e0) = 0;
    ecl_fetch(1);
    u16 table = OP_ADDR(1);
    if (G8(0x84de) && G8(0x5e25) > 13) { G8(0x5e25) = 5; G8(0x5e24) = 2; G8(0x84e2) = 1; }
    G8(0x84df) = 1;
    for (u8 i = 0; ; i++) {
        u8 glyph = (u8)(ecl_read_var((u16)(table + i)) + 1);
        if (glyph < 2) break;
        text_put_char(G8(0x5e24), G8(0x5e25), 15, 1, glyph, 0);
        G8(0x5e24)++;
    }
    G8(0x5e25)++;
}

// ---- The interpreter loop ----
// 1D3F:2F2D  Run the instruction whose opcode is in DS:6F7F. Opcodes not yet translated are still
// run by the original handlers at the addresses in this table.
static const struct { u16 handler; void (*fn)(void); } optab[0x40] = {
    [0x00] = {0x0001, op_exit}, [0x01] = {0x00f4, op_goto}, [0x02] = {0x0111, op_gosub}, [0x03] = {0x0126, op_compare},
    [0x04] = {0x01a3, op_arith}, [0x05] = {0x01a3, op_arith}, [0x06] = {0x01a3, op_arith}, [0x07] = {0x01a3, op_arith},
    [0x08] = {0x0234, op_random}, [0x09] = {0x0292, op_save}, [0x0a] = {0x02e0, op_load_character}, [0x0b] = {0x0450, op_load_monster}, [0x0c] = {0x03b4, op_setup_monster},
    [0x0d] = {0x0817, op_approach}, [0x0e] = {0x0856, op_picture}, [0x0f] = {0x0951, op_input_number}, [0x10] = {0x0994, op_input_string},
    [0x11] = {0x0a09, op_print}, [0x12] = {0x0a09, op_print}, [0x13] = {0x0a9f, op_return}, [0x14] = {0x0aed, op_compare_and},
    [0x15] = {0x0f08, op_vertical_menu}, [0x16] = {0x0b52, op_if}, [0x17] = {0x0b52, op_if}, [0x18] = {0x0b52, op_if}, [0x19] = {0x0b52, op_if},
    [0x1a] = {0x0b52, op_if}, [0x1b] = {0x0b52, op_if}, [0x1c] = {0x125d, op_clear_monsters}, [0x1d] = {0x12c6, op_party_strength}, [0x1e] = {0x1435, op_check_party},
    [0x20] = {0x0bd1, op_newecl}, [0x21] = {0x0c74, op_load_files}, [0x22] = {0x1629, op_party_surprise}, [0x23] = {0x16c9, op_surprise}, [0x24] = {0x179b, op_combat},
    [0x25] = {0x18f8, op_on_goto}, [0x26] = {0x18f8, op_on_goto}, [0x27] = {0x19b0, op_treasure}, [0x28] = {0x1db1, op_rob}, [0x29] = {0x1ef8, op_encounter_menu},
    [0x2a] = {0x0e5e, op_gettable}, [0x2b] = {0x10d1, op_horizontal_menu}, [0x2c] = {0x25cb, op_parlay}, [0x2d] = {0x2ccf, op_call}, [0x2e] = {0x2775, op_damage},
    [0x2f] = {0x0def, op_and_or}, [0x30] = {0x0def, op_and_or}, [0x31] = {0x2aa2, op_sprite_off}, [0x32] = {0x266a, op_find_item},
    [0x33] = {0x2aeb, op_print_return}, [0x34] = {0x2ac1, op_clock}, [0x35] = {0x0ebc, op_savetable}, [0x36] = {0x2b7e, op_add_npc},
    [0x37] = {0x0c74, op_load_files}, [0x38] = {0x2e10, op_program}, [0x39] = {0x2b33, op_who}, [0x3a] = {0x271f, op_delay}, [0x3b] = {0x2c03, op_spell},
    [0x3c] = {0x2e7d, op_protection}, [0x3d] = {0x2b14, op_clear_box} };

// ECLSTATS=1 in the environment prints, at exit, how often each opcode ran and whether natively.
static unsigned long op_count[0x40];
static void op_stats(void) {
    for (int op = 0; op < 0x40; op++) if (op_count[op]) fprintf(stderr, "[ecl] opcode %02X %-8s %lu\n", op, optab[op].fn ? "native" : "ORIGINAL", op_count[op]);
}

void ecl_step(void) {
    u8 op = OPCODE;
    static int stats = -1;
    if (stats < 0) { stats = getenv("ECLSTATS") != NULL; if (stats) atexit(op_stats); }
    if (stats && op < 0x40) op_count[op]++;
    if (op >= 0x40 || !optab[op].handler) return;        // unknown opcode: the original does nothing either
    if (optab[op].fn) optab[op].fn(); else ecl_original_handler(optab[op].handler);
}

// 1D3F:3171  Run the script from `addr` until an EXIT (or until the game is being abandoned, DS:49FF).
void ecl_run(u16 addr) {
    SETG16(ECL_IP, addr);
    G8(0x442e) = 0;
    while (!G8(0x442e) && !G8(0x49ff)) {
        farptr block = G32(0x49de);
        G8(0x6f7e) = OPCODE;                             // previous opcode
        OPCODE = mem[((u32)FP_SEG(block) << 4) + (u16)(FP_OFF(block) + G16(ECL_IP) + 0x6700)];
        ecl_step();
    }
    G8(0x442e) = 0;
}

// ---- Exploring ----
// Every script begins with five GOTOs, read into DS:49E3-49EB by ecl_init_script. They are the
// area's entry points, and these routines are what call them:
//   49EB  on entering the area            49E7  when the party makes camp
//   49E3  before each move                49E9  after camping, if something happened during it
//   49E5  after each move

// 1D3F:31B4  Arrive in an area (also after a NEWECL): note the square's event number, run the
// area's entry script, set the view mode for the three special areas, redraw, then run the
// "before move" and "after move" scripts once. Repeats if a script moved the party on again.
void explore_enter_area(void) {
    do {
        G8(0x6ab1) = map_event((s8)G8(0x6aae), (s8)G8(0x6aad));
        SETP16(AREA, 0x5aa, 0);
        farptr selected = G32(0x5d92);
        G8(0x49f9) = G8(0x49fa); G8(0x49fa) = 1;
        ecl_run(G16(0x49eb));
        G8(0x49f3) = 4;
        if (P16(GAME, 0x1cc) == 0) {
            switch (G8(0x84dc)) {
            case 0x19: G8(0x49fa) = 2; G8(0x49f3) = 3; break;
            case 0x1a:
                G8(0x49fa) = 3; G8(0x49f3) = 3;
                if (P16(GAME, 0x366) == 0xfe && P16(GAME, 0x344) == 0) map_reveal(1);
                break;
            case 0x1b: G8(0x49fa) = 4; G8(0x49f3) = 3; break;
            }
        }
        if (G8(0x49f9) > 1 && G8(0x49fa) == 1) ui_frame(FARPTR(RSEG(0x1d3f), 0x31b3), 10, 0, 13, 13, 3, 3);
        if (((G8(0x49f9) > 1 || G8(0x49fa) == 1) && G8(0x4431)) || (G8(0x49f9) == 1 && G8(0x49fa) == 1)) view_redraw();
        G8(0x442f) = 0;
        ecl_run(G16(0x49e3));
        if (!G8(0x442f)) {
            ecl_run(G16(0x49e5));
            SETG32(0x5d92, selected);
            hud_draw_roster(selected);
        }
    } while (G8(0x442f));
}

// 1D3F:2DD3  Encamp: run the area's camp script, show the camp menu, and if the party rested run
// the after-camp script (this is where a night's rest can be interrupted).
void explore_camp(void) {
    ecl_run(G16(0x49e7));
    farptr flag = scratch_push(2);
    camp_menu(flag);
    int interrupted = P8(flag, 0);
    scratch_pop(2);
    if (interrupted) { hud_draw_screen(); ecl_run(G16(0x49e9)); }
    view_redraw();
    G8(0x557d) = 0;
}

// 1D3F:32F8  The exploration loop: everything that happens between loading a game (or leaving a
// fight) and the next thing that ends exploring. Each turn it gets a command from the 3D view's
// menu, runs the before-move script, moves the party, plays the step sound if the party actually
// moved, and runs the after-move script.
void explore(void) {
    G8(0x4433) = 0; G8(0x4434) = 0; G8(0x4430) = 0; G8(0x4431) = 0; G8(0x4432) = 1;
    G8(0x49f3) = 4; G8(0x442f) = 0;
    G8(0x84dc) = (u8)P16(GAME, 0x1e4);
    if ((G8(0x84dc) == 0x19 || G8(0x84dc) == 0x1a || G8(0x84dc) == 0x1b) && P16(GAME, 0x1cc) == 0) G8(0x49f3) = 3;
    if (G8(0x49f8) == 0) ecl_load_block(G8(0x84dc));
    ecl_init_script();
    ecl_run(G16(0x49eb));
    if (G8(0x49f3) != 3) {
        if (G8(0x4432) == 1) { hud_draw_screen(); G8(0x4432) = 0; }
        view_redraw();
    }
    SETP16(GAME, 0x1e4, G8(0x84dc));
    for (;;) {
        u8 key = explore_command();                 // the 3D view's command menu
        SETG32(0x4436, G32(0x5d92));
        SETP16(GAME, 0x1e4, G8(0x84dc));
        while ((P16(AREA, 0x594) > 1 || key == 'E' || key == 'e') && !G8(0x49ff)) {
            if (key == 'E' || key == 'e') explore_camp();
            else {
                G8(0x84dd) = (u8)(P16(AREA, 0x594) & 1);
                SETP16(AREA, 0x594, 1);
                view_redraw();
                ecl_run(G16(0x49e5));
                if (G8(0x442f)) explore_enter_area();
                SETP16(AREA, 0x594, G8(0x84dd));
            }
            if (!G8(0x49ff)) { key = explore_command(); SETG32(0x4436, G32(0x5d92)); }
        }
        if (!G8(0x49ff)) ecl_run(G16(0x49e3));
        if (!G8(0x442f)) {
            if (!G8(0x49ff)) {
                s8 x = (s8)G8(0x6aad), y = (s8)G8(0x6aae);
                SETP16(GAME, 0x1e0, (u16)(s16)x); SETP16(GAME, 0x1e2, (u16)(s16)y);
                if (!move_party()) call_original(0x31f2, 0x0d6d, 0);   // carry out the move (locked doors: still original)
                view_redraw();
                x = (s8)G8(0x6aad); y = (s8)G8(0x6aae);
                if (x < 0 || (u16)(s16)x != P16(GAME, 0x1e0) || y < 0 || (u16)(s16)y != P16(GAME, 0x1e2)) sound_play(G16(0x2628));
                G8(0x84e0) = 0; G8(0x84e1) = 1;
                ecl_run(G16(0x49e5));
                if (G8(0x442f)) explore_enter_area();
            }
        } else explore_enter_area();
        if (G8(0x49ff)) { G8(0x49ff) = 0; return; }
    }
}

// 2366:1A6F  Script opcodes 8000h/8001h: a duel. Everyone but the selected character (matched by name) sits it out
// (10Dh = 0); with `clone`, a copy of the selected character named ROLF joins the other side (status B2h, the CPIC
// icon in slot DS:6F83) with copies of all its items - prepended one by one, so in reverse order.
void duel_setup(u8 clone) {
    G8(0x84d4) = 1;
    SETP16(AREA, 0x5cc, (u16)(s16)(s8)clone);
    farptr sel = G32(0x5d92);
    for (farptr p = G32(0x5d96); p; p = P32(p, 0x104)) {
        if (!(P8(p, 0) == P8(sel, 0) && !memcmp(&P8(p, 1), &P8(sel, 1), P8(sel, 0)))) P8(p, 0x10d) = 0;
    }
    if (!clone) return;
    sprite_load(FARPTR(RSEG(0x2366), 0x1a65), 0x0b, G8(0x6f83));                                       // "CPIC"
    farptr last = G32(0x5d96);
    while (P32(last, 0x104)) last = P32(last, 0x104);
    farptr c = rtl_getmem(0x11d);
    memmove(&mem[LIN(c)], &mem[LIN(sel)], 0x11d);
    P8(c, 0x10d) = 1; SETP32(c, 0x104, 0);
    { const u8 *r = &mem[LIN(FARPTR(RSEG(0x2366), 0x1a6a))]; u8 n = r[0] > 0x0f ? 0x0f : r[0]; P8(c, 0) = n; memcpy(&P8(c, 1), r + 1, n); }   // "ROLF"
    P8(c, 0x10f) = 1; P8(c, 0x10e) = 1; P8(c, 0x84) = 0xb2; P8(c, 0xbf) = G8(0x6f83);
    SETP32(c, 0x7f, 0); SETP32(c, 0xc8, 0);
    SETP32(last, 0x104, c);
    for (farptr it = P32(sel, 0xc8); it; it = P32(it, 0x2a)) {
        farptr n = rtl_getmem(0x3f), old = P32(c, 0xc8);
        SETP32(c, 0xc8, n);
        memmove(&mem[LIN(n)], &mem[LIN(it)], 0x3f);
        SETP32(n, 0x2a, old);
    }
}

// 2366:1DF0  Losing items by chance: each of ch's items goes on a d100 at or under `pct`; an item weighing 25 or more
// takes 50 off the chance first, 256 or more 90 - and those reductions stay for the following items (as the original).
void items_lose_chance(farptr ch, u8 pct) {
    for (farptr it = P32(ch, 0xc8); it; ) {
        u16 w = P16(it, 0x37);
        if (w >= 0x100) pct = pct < 0x5b ? 0 : (u8)(pct - 0x5a);
        else if (w > 0x18) pct = pct < 0x33 ? 0 : (u8)(pct - 0x32);
        u8 r = roll_dice(100, 1);
        farptr next = P32(it, 0x2a);
        if (r <= pct) item_lose(it, ch);
        it = next;
    }
}

// 2366:20CC  A script hurts someone: "<name> dies." (when the damage exceeds current hit points + 10) or "<name> is hit
// FOR N points of Damage." added to the message area (a page break with "press <enter>/<return> to continue" once it
// reaches row 23), then the damage is applied and the roster redrawn. Nothing for the dead (status 6).
void script_damage(farptr ch, u16 dmg) {
    if (P8(ch, 0x10c) == 6) return;
    #define DCS2(o) FARPTR(RSEG(0x2366), (o))
    farptr t = scratch_push(0x100); u8 *m = &mem[LIN(t)]; m[0] = 0;
    #define CAT(src) do { const u8 *q = (src); int c = q[0]; if (m[0] + c > 255) c = 255 - m[0]; memcpy(m + 1 + m[0], q + 1, c); m[0] = (u8)(m[0] + c); } while (0)
    CAT(&mem[LIN(DCS2(0x207e))]); CAT(&P8(ch, 0));
    if ((u16)(P8(ch, 0x11b) + 10) < dmg) CAT(&mem[LIN(DCS2(0x2081))]);
    else {
        CAT(&mem[LIN(DCS2(0x2089))]);
        char q[8]; int n = snprintf(q, sizeof q, "%u", dmg); if (n > 3) n = 3; u8 num[4]; num[0] = (u8)n; memcpy(num + 1, q, n); CAT(num);
        CAT(&mem[LIN(DCS2(0x2096))]);
    }
    #undef CAT
    u8 clear = 0;
    if (G8(0x5e25) >= 0x17) { G8(0x5e25) = 0x11; clear = 1; text_prompt(DCS2(0x20a9), 0x0f); }
    G8(0x5e24) = 0x26;
    text_print_wrapped(t, clear, 0x0f, 0x16, 0x26, 0x11, 1);
    scratch_pop(0x100);
    call_original(0x10ba, 0x2228, 3, FP_SEG(ch), FP_OFF(ch), (u8)dmg);
    text_clear_box(0x11, 1, 0x26, 0x0f);
    hud_draw_roster(G32(0x5d92));
    #undef DCS2
}

// 2366:0482  How many squares ahead (0-2) are open from (x, y) facing dir: 2 at once outside the dungeon view
// (game state 1CCh = 0, also noted in the area's 582h).
u8 dist_ahead(s8 x, s8 y, u8 dir) {
    u8 n = 0, blocked = 0;
    if (P16(GAME, 0x1cc) == 0) { SETP16(AREA, 0x582, 2); return 2; }
    while (n < 2 && !blocked) {
        if (map_wall(dir, y, x) == 0) {
            n++;
            if (dir == 0) y--; else if (dir == 2) x++; else if (dir == 4) y++; else if (dir == 6) x--;
        } else blocked = 1;
    }
    return n;
}
// 2366:19DB  Script opcode C01Eh: the party steps forward one square (wrapping on the 16x16 map), and the new
// square's event (DS:6AB1) and the wall ahead (DS:6AB0) are looked up.
void step_forward(void) {
    u8 d = G8(0x6aaf); s8 x = (s8)G8(0x6aad), y = (s8)G8(0x6aae);
    if (d == 0) y = y < 1 ? 0x0f : (s8)(y - 1);
    else if (d == 2) x = x < 0x0f ? (s8)(x + 1) : 0;
    else if (d == 4) y = y < 0x0f ? (s8)(y + 1) : 0;
    else if (d == 6) x = x < 1 ? 0x0f : (s8)(x - 1);
    G8(0x6aad) = (u8)x; G8(0x6aae) = (u8)y;
    G8(0x6ab1) = map_event((s8)G8(0x6aae), (s8)G8(0x6aad));
    G8(0x6ab0) = map_wall(G8(0x6aaf), (s8)G8(0x6aae), (s8)G8(0x6aad));
}
