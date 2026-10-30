#include <stdlib.h>
// The character sheet (original overlay unit at 3C10; the display half).
#include <stdio.h>
#include "poolrad.h"
#include "fixes.h"
#include "text.h"
#include "hud.h"
#include "scene.h"
#include "menu.h"
#include "rtl.h"
#include "sheet.h"
static void ps_concat(farptr d, farptr src) { u8 *a = &mem[LIN(d)]; const u8 *b = &mem[LIN(src)]; int c = b[0]; if (a[0] + c > 255) c = 255 - a[0]; memmove(a + 1 + a[0], b + 1, c); a[0] = (u8)(a[0] + c); }
#include "gfx.h"
#include "map.h"
#include "combat.h"
#include "fight.h"
#include "chr.h"
#include "rules.h"
#include "ecl.h"
#include "gametime.h"
#include "input.h"

#define SEL G32(0x5d92)
static void put(const char *t, u8 color, u8 row, u8 col) {
    u8 s[64]; size_t n = strlen(t); if (n > 40) n = 40;
    s[0] = (u8)n; memcpy(s + 1, t, n);
    text_put_pstr(s, color, row, col);
}
static const char *word_from(u16 table, u8 size, u8 index) { return pstr(FARPTR(DATA_SEG, table + index * size)); }   // an entry of one of the name tables

// 3C10:0901  One ability score (0 Strength ... 5 Charisma) on its row of the sheet, in magenta
// when `highlight` is set. A Strength of 18 with a percentage shows it as 18(57); 100 shows as (00).
u8 spell_list_build(u8 kind);
u8 spell_list_choose(farptr index_var, u8 mode);
void sheet_draw_ability(u8 highlight, s8 index) {
    farptr ch = SEL;
    u8 color = highlight ? 13 : 10, row = (u8)(index + 7), v = P8(ch, 0x10 + index);
    char t[16];
    text_clear_box(5, (s8)row, 11, (s8)row);
    snprintf(t, sizeof t, "%u", v);
    put(t, color, row, v < 10 ? 6 : 5);
    if (index == 0 && P8(ch, 0x10) == 18 && P8(ch, 0x16) != 0) {
        u8 pct = P8(ch, 0x16);
        if (pct == 100) snprintf(t, sizeof t, "(00)"); else snprintf(t, sizeof t, "(%02u)", pct);
        put(t, color, 7, 7);
    }
}

// 3C10:050D  The character's money and valuables, one line per kind that they own, most valuable
// first: Jewelry, Gems, Platinum, Gold, Electrum, Silver, Copper (seven words from byte 88h).
void sheet_draw_money(void) {
    farptr ch = SEL; u8 row = 7; char t[16];
    text_clear_box(12, 7, 26, 14);
    for (s8 kind = 6; kind >= 0; kind--) {
        u16 amount = P16(ch, 0x88 + kind * 2);
        if (!amount) continue;
        const char *name = word_from(0x0ffa, 11, (u8)kind);
        put(name, 10, row, (u8)(20 - strlen(name)));
        snprintf(t, sizeof t, "%u", amount);
        put(t, 10, row, 21);
        row++;
    }
}

// 3C10:0608  Armour class, hit points, THAC0, damage, encumbrance and movement. THAC0 is stored
// like armour class, as 60 minus the number shown. Movement is doubled or halved if the
// character carries status effect 2Ah or 27h. The spell table says 27h is what Haste
// leaves and 2Ah what Slow leaves, so this looks the wrong way round in the original; kept (fixed when FIX is on).
void sheet_draw_combat_stats(void) {
    farptr ch = SEL; char t[48]; u8 row = 17;
    call_original(0x10ba, 0x0bb8, 2, FP_SEG(ch), FP_OFF(ch));              // work out the derived numbers first (still original code)
    put("AC    ", 15, row, 1);
    hud_draw_ac(row, 4, ch);
    put("HP    ", 15, (u8)(row + 1), 1);
    hud_draw_hp(0, (u8)(row + 1), 4, ch);
    put("THAC0   ", 15, row, 9);
    snprintf(t, sizeof t, "%u", (u8)(0x3c - P8(ch, 0x110)));
    put(t, 10, row, 15);
    int n = snprintf(t, sizeof t, "%ud%u", P8(ch, 0x115), P8(ch, 0x117));
    s8 bonus = (s8)P8(ch, 0x119);
    if (bonus > 0) snprintf(t + n, sizeof t - (size_t)n, "+%u", (u8)bonus);
    if (bonus < 0) snprintf(t + n, sizeof t - (size_t)n, "-%u", (u8)-bonus);
    put("Damage        ", 15, (u8)(row + 1), 8);
    put(t, 10, (u8)(row + 1), 15);
    put("Encumbrance      ", 15, row, 22);
    snprintf(t, sizeof t, "%u", P16(ch, 0x102));
    put(t, 10, row, 34);
    u8 move = P8(ch, 0x11c);
    if (char_has_effect(FIX ? 0x27 : 0x2a, ch)) move = (u8)(move << 1);      // (FIX: Haste doubles, Slow halves)
    if (char_has_effect(FIX ? 0x2a : 0x27, ch)) move >>= 1;
    put("Movement   ", 15, (u8)(row + 1), 25);
    snprintf(t, sizeof t, "%u", move);
    put(t, 10, (u8)(row + 1), 34);
}

// 3C10:004A  Draw the whole sheet for the selected character.
void sheet_draw(void) {
    static const char *label[6] = {"STR ", "INT ", "WIS ", "DEX ", "CON ", "CHA "};
    farptr ch = SEL; char t[64]; u8 col = 1, row;
    SETG32(0x4716, ch);
    text_clear_screen();
    ui_border(0, 22, 38, 1, 1);
    hud_draw_name(0, 1, 1, ch);
    if (P8(ch, 0x84) > 0x7f) put("(NPC)", 10, 1, (u8)(P8(ch, 0) + 3));
    const char *w = word_from(0x0fec, 7, P8(ch, 0x9e));                     // Male / Female
    put(w, 15, 3, col); col = (u8)(col + strlen(w) + 1);
    w = word_from(0x0f03, 10, P8(ch, 0x2e));                                // race
    put(w, 15, 3, col); col = (u8)(col + strlen(w) + 1);
    snprintf(t, sizeof t, "Age %u", P16(ch, 0x30));
    put(t, 15, 3, col);
    put(word_from(0x0f53, 17, P8(ch, 0xa0)), 15, 4, 1);                     // alignment
    put(word_from(0x0d38, 27, P8(ch, 0x2f)), 15, 5, 1);                     // class
    for (s8 i = 0; i <= 5; i++) { put(label[i], 10, (u8)(i + 7), 1); sheet_draw_ability(0, i); }
    sheet_draw_money();
    put("Level", 15, 15, 1);
    t[0] = 0;
    for (int c = 0; c <= 7; c++)                                            // a level for each class the character has, e.g. 1/1
        if ((s8)P8(ch, 0x96 + c) > 0) snprintf(t + strlen(t), sizeof t - strlen(t), "%s%u", t[0] ? "/" : "", P8(ch, 0x96 + c));
    put(t, 15, 15, 7);
    snprintf(t, sizeof t, "Exp %d", (s32)P32(ch, 0xac));
    put(t, 15, 15, 17);
    sheet_draw_combat_stats();
    row = 20;
    if (P32(ch, 0xcc)) { put("Weapon", 15, row, 1); item_draw_name(P32(ch, 0xcc), ch, row, 8); }
    row++;
    if (P32(ch, 0xd4)) { put("Armor", 15, row, 2); item_draw_name(P32(ch, 0xd4), ch, row, 8); }
    row++;
    put("Status", 15, row, 1);
    put(word_from(0x1047, 13, P8(ch, 0x10c)), 10, row, 8);
    if (G8(0x49f5) && G8(0x5376) == 3) scene_show_character_portrait();
}

// 3C10:0AB3  The View screen: show the sheet and offer Items / Spells / Trade / Drop / Exit, each
// only when it applies (something carried, a spell memorised, money to hand over). `leave` is
// set by the item screen when the caller should drop everything, e.g. the character was removed.
// The item, spell, trade and drop screens themselves are still original code.
void sheet_view(farptr leave) {
    u8 key = ' ', special;
    if (G8(0x49f3) == 5) call_original(0x4afb, 0x0105, 0);                 // in combat: put the battlefield picture away
    P8(leave, 0) = 0;
    SETG32(0x471a, SEL);
    sheet_draw();
    while (key != 0 && key != 'E' && P8(leave, 0) == 0) {
        u8 items[48]; int n = 0, spells = 0, money = 0;
        farptr ch = G32(0x4716);
        for (int i = 0; i <= 0x14; i++) if (P8(ch, 0x17 + i)) spells = 1;
        for (int k = 0; k <= 6; k++) if (P16(ch, 0x88 + k * 2)) money = 1;
        if (P32(ch, 0xc8)) n += sprintf((char *)items + 1 + n, "Items ");
        if (spells) n += sprintf((char *)items + 1 + n, "Spells ");
        if ((P8(ch, 0x84) < 0x80 || P8(ch, 0x10d) == 0 || P8(ch, 0x10c) == 1) && money && G8(0x49f3) != 5)
            n += sprintf((char *)items + 1 + n, "Trade ");
        if (money) n += sprintf((char *)items + 1 + n, "Drop ");
        n += sprintf((char *)items + 1 + n, "Exit");
        items[0] = (u8)n;
        key = menu_horizontal(&special, 0, 0, 15, 10, 13, items, (const u8 *)"\005View:");
        if (key == 'I') sheet_items(leave);
        else if (key == 'S') {
            farptr v = scratch_push(4);                                     // two VAR parameters: the "has spells" flag and a word preset to -1
            P8(v, 0) = (u8)spells; SETP16(v, 2, 0xffff);
            call_original(0x3c10, 0x299c, 6, 0, 0, FP_SEG(v), (u16)(FP_OFF(v) + 2), FP_SEG(v), FP_OFF(v));
            scratch_pop(4);
        }
        else if (key == 'T') sheet_trade_money(1);      // called from C: no original stack byte to imitate; 1 = return
        else if (key == 'D') { sheet_drop_money(); sheet_draw_money(); }
        if (P8(leave, 0) == 0 && (key == 'I' || key == 'S' || key == 'T')) sheet_draw();
    }
    if (G8(0x49f3) == 5) call_original(0x4afb, 0x00e3, 0);
    hud_draw_screen();
}

// 3C10:27DF  Move the party selection: Home ('G', also 7 on the keypad) to the character before
// the selected one, wrapping from the leader to the last; End ('O', or 1) to the one after,
// stopping at the last; any other key back to the leader.
void party_select_move(u8 key) {
    farptr sel = G32(0x5d92), p = G32(0x5d96);
    if (p) {
        if (key == 'G') {
            if (p == sel) while (P32(p, 0x104)) p = P32(p, 0x104);
            else while (p && P32(p, 0x104) != sel) p = P32(p, 0x104);
        }
        else if (key == 'O') { if (P32(sel, 0x104)) p = P32(sel, 0x104); }
    }
    SETG32(0x5d92, p);
}

// 3FA9:0B93  Which kind of coin a money-list line is about, from the first letter after the padding (GE.. gems,
// G.. gold, P platinum, E electrum, S silver, C copper, J jewelry); the kind's name (with a trailing space) goes to
// *name. An unknown letter leaves the result undefined in the original; the lists only ever hold these.
s8 coin_kind(farptr name, farptr line) {
    u8 t[256]; memcpy(t, &mem[LIN(line)], 256);
    u8 i = 1; while (t[i] == ' ') i++;
    u8 c = t[i], n2 = t[i + 1]; if (n2 >= 'a' && n2 <= 'z') n2 -= 0x20;
    static const struct { u8 c; s8 kind; u16 str; } tab[] = { {'P',4,0xb66}, {'E',2,0xb70}, {'S',1,0xb7a}, {'C',0,0xb82}, {'J',6,0xb8a} };
    s8 kind = 0; u16 str = 0;
    if (c == 'G') { if (n2 == 'E') { kind = 5; str = 0xb5a; } else { kind = 3; str = 0xb60; } }
    else for (int k = 0; k < 5; k++) if (tab[k].c == c) { kind = tab[k].kind; str = tab[k].str; }
    if (str) { const u8 *src = &mem[LIN(FARPTR(RSEG(0x3fa9), str))]; memcpy(&mem[LIN(name)], src, src[0] + 1u); }
    return kind;
}
// 3FA9:0294  Typing an amount on the bottom line after a prompt: digits only, Backspace, at most six characters,
// and anything over `max` snaps to `max`. Enter accepts, Esc gives 0. (Backspace blanks the column after the last
// digit rather than the digit itself, a small slip in the original kept here.)
u16 input_amount(u16 max, farptr prompt, u8 color) {
    farptr sc = scratch_push(0x40); u8 *pr = &mem[LIN(sc)], *buf = pr + 0x2a;
    u8 n = mem[LIN(prompt)] > 0x28 ? 0x28 : mem[LIN(prompt)]; pr[0] = n; memcpy(pr + 1, &mem[LIN(prompt)] + 1, n);
    text_put_blanks(0x28, 0, 0x18, 0);
    text_put_string(sc, color, 0x18, 0);
    u8 col0 = pr[0], cur = pr[0];
    char ms[8]; snprintf(ms, sizeof ms, "%u", max);
    buf[0] = 0;
    u8 k;
    do {
        k = (u8)call_original(0x1899, 0x0059, 0);
        if (k < '0' || k > '9') {
            if (k == 8 && buf[0]) { buf[0]--; text_put_blanks(1, 0, 0x18, cur); cur--; }
        } else {
            if (buf[0] < 6) buf[++buf[0]] = k;
            u32 v = 0; for (int i = 1; i <= buf[0]; i++) v = v * 10 + (u32)(buf[i] - '0');
            if (v > 0xffff || v > max) { buf[0] = (u8)strlen(ms); memcpy(buf + 1, ms, buf[0]); cur = (u8)(col0 + buf[0]); }
            else cur++;
            text_put_string(sc + 0x2a, 0x0f, 0x18, col0);
        }
    } while (k != 0x0d && k != 0x1b);
    text_put_blanks(0x28, 0, 0x18, 0);
    u32 v = 0; for (int i = 1; i <= buf[0]; i++) v = v * 10 + (u32)(buf[i] - '0');
    scratch_pop(0x40);
    return k == 0x1b ? 0 : (u16)v;
}
// 3FA9:0A88  Taking coins of one kind from the treasure (DS:67F4, a longint per kind) unless it would overload ch.
void take_coins(s8 kind, u16 amount, farptr ch) {
    u16 room;
    if (chr_too_heavy(&room, amount, ch)) { ui_notice(&mem[LIN(FARPTR(RSEG(0x3fa9), 0x0a7d))]); return; }     // "Overloaded"
    u16 lo = G16(kind * 4 + 0x67f4), hi = G16(kind * 4 + 0x67f6);
    if (hi > 0x7fff || ((s16)hi < 1 && lo < amount)) amount = lo;
    u32 t = ((u32)hi << 16 | lo) - amount;
    wr16(LIN(FARPTR(DATA_SEG, kind * 4 + 0x67f4)), (u16)t); wr16(LIN(FARPTR(DATA_SEG, kind * 4 + 0x67f6)), (u16)(t >> 16));
    wr16(LIN(ch) + 0x88 + kind * 2, (u16)(P16(ch, 0x88 + kind * 2) + amount));
    chr_load_add(amount, ch);
}
static int treasure_coins(int k) { return (s32)((u32)G16(k * 4 + 0x67f6) << 16 | G16(k * 4 + 0x67f4)) > 0; }
// 3FA9:0CEB  Treasure, Take, Money: a list of the coins in the treasure; pick a kind, say how many, repeat until
// none are left or the list is left. DS:6DC0 says whether any coins remain. (A list left with Esc is not freed.)
void take_money(void) {
    static const u8 none[] = "";
    ui_frame(FARPTR(RSEG(0x3fa9), 0x0cba), 10, 0, 0x16, 0x26, 1, 1);
    s16 index = 0; u8 done;
    do {
        u8 redraw = 1; farptr head = 0;
        for (int k = 6; k >= 0; k--) {
            if (!treasure_coins(k)) continue;
            farptr node = rtl_getmem(0x2e);
            SETP32(node, 0x2a, head); head = node;
            const u8 *nm = &G8(k * 0x0b + 0x0ffa);
            char num[16]; int dn = snprintf(num, sizeof num, "%ld", (long)(s32)((u32)G16(k * 4 + 0x67f6) << 16 | G16(k * 4 + 0x67f4)));
            u8 t[300]; int L = nm[0]; memcpy(t, nm + 1, L); t[L++] = ' '; memcpy(t + L, num, dn); L += dn;
            if (L > 0x28) L = 0x28;
            P8(node, 0) = (u8)L; memcpy(&P8(node, 1), t, L); P8(node, 0x29) = 0;
        }
        farptr picked = head, list = head;
        u8 key = menu_list(&picked, &index, &redraw, 1, head, 8, 0x0f, 2, 2, 0x0f, 10, 0x0d, none, &mem[LIN(FARPTR(RSEG(0x3fa9), 0x0cbd))]);
        if (picked == 0 || key == 0) done = 1;
        else {
            farptr sc = scratch_push(0x200), nm = sc, q = sc + 0x100;
            s8 kind = coin_kind(nm, picked);
            const u8 *a = &mem[LIN(FARPTR(RSEG(0x3fa9), 0x0cd1))], *b = &mem[LIN(FARPTR(RSEG(0x3fa9), 0x0cdb))], *m = &mem[LIN(nm)];
            u8 *t = &mem[LIN(q)]; int L = 0;
            memcpy(t + 1, a + 1, a[0]); L = a[0];
            int c = m[0]; if (L + c > 255) c = 255 - L; memcpy(t + 1 + L, m + 1, c); L += c;
            c = b[0]; if (L + c > 255) c = 255 - L; memcpy(t + 1 + L, b + 1, c); L += c; t[0] = (u8)L;
            u16 amount = input_amount(G16(kind * 4 + 0x67f4), q, 10);
            scratch_pop(0x200);
            take_coins(kind, amount, G32(0x5d92));
            farptr lv = scratch_push(4); SETP32(lv, 0, list); menu_list_free(lv); scratch_pop(4);
            G8(0x6dc0) = 0; done = 1;
            for (int k = 0; k <= 6; k++) if (treasure_coins(k)) { G8(0x6dc0) = 1; done = 0; }
        }
    } while (!done);
}

// 3FA9:05E0  How many party members can take a share: status 0 (okay) or B3h.
u8 share_count(void) {
    u8 n = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) if (P8(c, 0x84) == 0 || P8(c, 0x84) == 0xb3) n++;
    return n;
}
// 3FA9:063A  Treasure, Share: each kind of coin is divided by share_count(); everyone not dead (status < 80h) gets
// a share, or what they can carry, and one coin of the remainder while it fits. What is left over is then handed
// out to anyone with room, and the rest stays in the treasure. With no one to share between, the original divides
// by zero and the program stops with run-time error 200; so does this.
void treasure_share(void) {
    if (!share_count()) { if (FIX) return; rtl_run_error(200); }   // (FIX: nothing to share, no crash)
    u8 n = share_count();
    if (n == 0) { call_original(0x3fa9, 0x063a, 0); return; }
    u16 share[7], rem[7], room;
    for (int k = 0; k <= 6; k++) {
        s32 t = (s32)((u32)G16(k * 4 + 0x67f6) << 16 | G16(k * 4 + 0x67f4));
        if (t > 0) { share[k] = (u16)(t / n); rem[k] = (u16)(t % n); } else share[k] = rem[k] = 0;
    }
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        if (P8(c, 0x84) >= 0x80) continue;
        for (int k = 6; k >= 0; k--) {
            u16 at = (u16)(0x88 + k * 2);
            if (!chr_too_heavy(&room, share[k], c)) {
                wr16(LIN(c) + at, (u16)(P16(c, at) + share[k])); chr_load_add(share[k], c);
                if (rem[k] && !chr_too_heavy(&room, rem[k], c)) { wr16(LIN(c) + at, (u16)(P16(c, at) + 1)); chr_load_add(1, c); rem[k]--; }
            } else {
                wr16(LIN(c) + at, (u16)(P16(c, at) + room)); rem[k] = (u16)(rem[k] + share[k] - room); chr_load_add(room, c);
            }
        }
    }
    for (int k = 6; k >= 0; k--) {
        if (!rem[k]) continue;
        for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
            u16 at = (u16)(0x88 + k * 2);
            room = (u16)(chr_max_load(c) - P16(c, 0x102));
            if (!room) continue;
            if (room < rem[k]) { wr16(LIN(c) + at, (u16)(P16(c, at) + room)); chr_load_add(room, c); rem[k] -= room; }
            else { wr16(LIN(c) + at, (u16)(P16(c, at) + rem[k])); chr_load_add(rem[k], c); rem[k] = 0; }
        }
    }
    G8(0x6dc0) = 0;
    for (int k = 0; k <= 6; k++) {
        wr16(LIN(FARPTR(DATA_SEG, k * 4 + 0x67f4)), rem[k]); wr16(LIN(FARPTR(DATA_SEG, k * 4 + 0x67f6)), 0);
        if (rem[k]) G8(0x6dc0) = 1;
    }
}
// 3C10:0D70  Whether an item may leave the character (Trade, Drop, Sell): not while readied; a scroll the character
// was going to scribe from (one of 3Ch-3Eh marked) needs a yes to "is it Okay to lose it?".
u8 item_may_part(farptr item) {
    u8 ok = 0;
    if (P8(item, 0x34)) ui_notice(&mem[LIN(FARPTR(RSEG(0x3c10), 0x0d20))]);          // "Must be unreadied"
    else if (!item_is_scroll(item)) ok = 1;
    else if (P8(item, 0x3c) < 0x80 && P8(item, 0x3d) < 0x80 && P8(item, 0x3e) < 0x80) ok = 1;
    else {
        farptr sel = G32(0x5d92);
        hud_draw_name(0, 0x15, 1, sel);
        G8(0x5e24) = (u8)(P8(sel, 0) + 2); G8(0x5e25) = 0x15;
        text_print_wrapped(FARPTR(RSEG(0x3c10), 0x0d32), 0, 0x0e, 0x16, 0x26, 0x15, 1);
        if (menu_yes_no(0x0f, 10, 0x0d, FARPTR(RSEG(0x3c10), 0x0d58)) == 'Y') ok = 1;
    }
    text_clear_box(1, 0x15, 0x26, 0x16);
    return ok;
}
// 3C10:17ED  Halving a stack (count at 39h): the new item after it gets half, unreadied.
void item_halve(farptr item) {
    u8 half = P8(item, 0x39) >> 1;
    if (!half) { ui_notice(&mem[LIN(FARPTR(RSEG(0x3c10), 0x17dc))]); return; }       // "Can't halve that"
    u8 rest = (u8)(P8(item, 0x39) - half);
    farptr n = rtl_getmem(0x3f);
    memmove(&mem[LIN(n)], &mem[LIN(item)], 0x3f);
    P8(n, 0x39) = half; P8(n, 0x34) = 0;
    SETP32(n, 0x2a, P32(item, 0x2a));
    P8(item, 0x39) = rest;
    SETP32(item, 0x2a, n);
}
// 3C10:189D  Joining every matching stack into this one (up to 255; the excess stays in the other stack, which then
// takes the rest). A quirk of the original: the checks on 3Ch, 3Dh and 3Eh compare the other item with itself, so
// only "3Ch below 2" is really required of those fields; the translation keeps that (fixed when FIX is on).
void item_join(farptr item) {
    farptr e = item; u8 total = P8(item, 0x39);
    farptr p = P32(G32(0x5d92), 0xc8);
    while (p) {
        farptr next = P32(p, 0x2a);
        if (p != e && P8(p, 0x39) && P8(p, 0x2f) == P8(e, 0x2f) && P8(p, 0x30) == P8(e, 0x30) && P8(p, 0x31) == P8(e, 0x31)
            && P8(p, 0x2e) == P8(e, 0x2e) && P8(p, 0x32) == P8(e, 0x32) && P8(p, 0x33) == P8(e, 0x33)
            && P8(p, 0x36) == P8(e, 0x36) && P16(p, 0x37) == P16(e, 0x37) && P8(p, 0x3c) < 2
            && (!FIX || (P8(p, 0x3c) == P8(e, 0x3c) && P8(p, 0x3d) == P8(e, 0x3d) && P8(p, 0x3e) == P8(e, 0x3e)))) {   // (FIX: those three really compared)
            if (total + P8(p, 0x39) <= 0xff) {
                total = (u8)(total + P8(p, 0x39));
                item_lose(p, G32(0x5d92));
            } else {
                P8(e, 0x39) = 0xff;
                P8(p, 0x39) = (u8)(P8(p, 0x39) - (u8)(0xff - total));
                total = P8(p, 0x39);
                e = p;
            }
        }
        p = next;
    }
    P8(e, 0x39) = total;
}

// 3C10:28BE / 3FA9:00A8  A character's (or the treasure pool's, as longints) money in gold: copper to platinum
// times their values (longints at DS:0D24), plus 100, divided by gold's value.
static s32 coin_rate(int k) { return (s32)((u32)G16(k * 4 + 0x0d26) << 16 | G16(k * 4 + 0x0d24)); }
s32 money_of(farptr ch) {
    s32 t = 0;
    for (int k = 0; k <= 4; k++) t += (s32)P16(ch, 0x88 + k * 2) * coin_rate(k);
    return (t + 100) / coin_rate(3);
}
s32 money_of_pool(farptr pool) {
    s32 t = 0;
    for (int k = 0; k <= 4; k++) t += (s32)P32(pool, k * 4) * coin_rate(k);
    return (t + 100) / coin_rate(3);
}
// 34FB:42ED  Paying `gp` gold pieces in coins: the price in coin units (x200) is taken from copper upward, each kind
// at most what is carried, and anything overpaid comes back as change from platinum downward. (Neither loop checks
// its bounds; Train only calls this when the money is there.)
void pay_coins(farptr ch, s32 gp) {
    s32 amt = gp * 200;
    int k = 0;
    while (amt > 0) {
        u16 n = (u16)(amt / coin_rate(k) + 1);
        if (P16(ch, 0x88 + k * 2) < n) n = P16(ch, 0x88 + k * 2);
        amt -= (s32)n * coin_rate(k);
        wr16(LIN(ch) + 0x88 + k * 2, (u16)(P16(ch, 0x88 + k * 2) - n));
        k++;
    }
    if (amt < 0) {
        amt = -amt;
        for (k = 4; amt > 0; k--) {
            u16 n = (u16)(amt / coin_rate(k));
            amt -= (s32)n * coin_rate(k);
            wr16(LIN(ch) + 0x88 + k * 2, (u16)(P16(ch, 0x88 + k * 2) + n));
        }
    }
}
// 3FA9:0130 / 0185  Paying: the character's (or the pool's) coins become what is left, as platinum and gold.
// (FIX) the same without the 16-bit limit: what is left goes back as platinum (and gold for the rest), each count
// capped at what its 16-bit field holds; the pool's counts are 32-bit.
static void pay_from32(u32 left) {
    farptr ch = G32(0x5d92); u32 pl = left / 5; if (pl > 0xffff) pl = 0xffff; u32 gd = left - pl * 5; if (gd > 0xffff) gd = 0xffff;
    for (int k = 0; k <= 4; k++) wr16(LIN(ch) + 0x88 + k * 2, 0);
    wr16(LIN(ch) + 0x90, (u16)pl); wr16(LIN(ch) + 0x8e, (u16)gd);
}
static void pay_from_pool32(u32 left) { for (int k = 0; k <= 4; k++) SETG32(k * 4 + 0x67f4, 0); SETG32(0x6804, left / 5); SETG32(0x6800, left % 5); }
void pay_from(u32 left) { if (FIX) { pay_from32(left); return; } left = (u16)left; farptr ch = G32(0x5d92); for (int k = 0; k <= 4; k++) wr16(LIN(ch) + 0x88 + k * 2, 0); wr16(LIN(ch) + 0x90, (u16)(left / 5)); wr16(LIN(ch) + 0x8e, (u16)(left % 5)); }
void pay_from_pool(u32 left) { if (FIX) { pay_from_pool32(left); return; } left = (u16)left; for (int k = 0; k <= 4; k++) SETG32(k * 4 + 0x67f4, 0); SETG32(0x6804, (u32)(left / 5)); SETG32(0x6800, (u32)(left % 5)); }
// 10BA:1509  Taking an item out of a character's list and freeing it.
void item_lose(farptr item, farptr ch) {
    if (!item) { text_prompt(FARPTR(RSEG(0x10ba), 0x14ce), 0x0e); return; }                          // "Nil Item pointer..."
    farptr p = P32(ch, 0xc8);
    if (p == item) SETP32(ch, 0xc8, P32(item, 0x2a));
    else while (p && P32(p, 0x2a) != item) p = P32(p, 0x2a);
    if (!p) { text_prompt(FARPTR(RSEG(0x10ba), 0x14e2), 0x0e); return; }                            // "Tried to Lose item & couldn't find it!"
    SETP32(p, 0x2a, P32(item, 0x2a));
    SETP32(item, 0x2a, 0);
    rtl_freemem(item, 0x3f);
}
// 10BA:15F7  Giving a copy of an item to a character (at the end of the list).
void item_give(farptr item, farptr ch) {
    farptr n = rtl_getmem(0x3f);
    memmove(&mem[LIN(n)], &mem[LIN(item)], 0x3f);
    SETP32(n, 0x2a, 0);
    farptr p = P32(ch, 0xc8);
    if (!p) { SETP32(ch, 0xc8, n); return; }
    while (P32(p, 0x2a)) p = P32(p, 0x2a);
    SETP32(p, 0x2a, n);
}
// 3C10:275B  Whether ch cannot take the item: 16 items already, or the weight (37h, times the count) would go over
// what the character's strength allows plus 1500.
u8 item_too_much(farptr item, farptr ch) {
    chr_recalculate(ch);
    u8 over = P8(ch, 0xc7) > 0x0f;
    u16 w = P16(item, 0x37); if (P8(item, 0x39)) w = (u16)(P8(item, 0x39) * w);
    s16 cap = (s16)((u16)call_original(0x10ba, 0x13ab, 2, FP_SEG(ch), FP_OFF(ch)) + 0x5dc);
    if ((s32)cap < (s32)(u16)(P16(ch, 0x102) + w)) over = 1;
    return over;
}
// 3C10:1734  Items, Trade: give the item to another member (the last one chosen is offered first).
void item_trade(farptr item) {
    farptr v = scratch_push(4); SETP32(v, 0, G32(0x471a));
    hud_draw_screen();
    chr_pick_character(v, 1, &mem[LIN(FARPTR(RSEG(0x3c10), 0x1718))]);                              // "Trade with Whom?"
    farptr to = P32(v, 0); scratch_pop(4);
    if (!to) return;
    SETG32(0x471a, to);
    if (!item_too_much(item, to)) { item_give(item, to); item_lose(item, G32(0x5d92)); chr_recalculate(to); }
    else ui_notice(&mem[LIN(FARPTR(RSEG(0x3c10), 0x1729))]);                                        // "Overloaded"
}
static void say_item(u16 a, u16 num_or_0, u16 b, farptr item, int with_num, u16 value) {
    farptr t = scratch_push(0x200), n = t + 0x100; P8(t, 0) = 0;
    ps_concat(t, FARPTR(RSEG(0x3c10), a));
    if (with_num) { chr_number_text(n, value); ps_concat(t, n); ps_concat(t, FARPTR(RSEG(0x3c10), b)); }
    (void)num_or_0;
    ps_concat(t, item);
    text_print_wrapped(t, 1, 0x0e, 0x16, 0x26, 0x15, 1);
    scratch_pop(0x200);
}
// 3C10:1CEE  Items, Sell (in a shop): half the item's value, for a stack count*half/20. "Is It a Deal?" Yes: the
// item goes and the money comes as platinum and gold; if that overloads the character, the platinum that does not
// fit goes to the pool. A dead test is kept: the special price for stacks of type "I" requires the type to be both
// 'I' and 1Ch at once, so it never applies.
void item_sell(farptr item) {
    u16 value = P16(item, 0x3a) ? (u16)(P16(item, 0x3a) >> 1) : 0;
    if (P8(item, 0x39) > 1) value = (u16)((u16)(P8(item, 0x39) * value) / 0x14);
    chr_item_name(0, 0, 0, 0, item);
    say_item(0x1c8b, 0, 0x1c9a, item, 1, value);                                                       // "I'll give you N gold pieces for your <item>"
    if (menu_yes_no(0x0f, 10, 0x0d, FARPTR(RSEG(0x3c10), 0x1cb1)) == 'Y') {                             // "Is It a Deal? "
        ui_notice(&mem[LIN(FARPTR(RSEG(0x3c10), 0x1cc0))]);                                            // "Sold!"
        farptr ch = G32(0x5d92);
        item_lose(item, ch);
        u16 plat = value / 5, gold = value % 5, room;
        if (!chr_too_heavy(&room, (u16)(gold + plat), G32(0x5d92))) {
            wr16(LIN(G32(0x5d92)) + 0x90, (u16)(P16(G32(0x5d92), 0x90) + plat));
            wr16(LIN(G32(0x5d92)) + 0x8e, (u16)(P16(G32(0x5d92), 0x8e) + gold));
        } else {
            ui_notice(&mem[LIN(FARPTR(RSEG(0x3c10), 0x1cc6))]);                                        // "Overloaded.  Money will be put in pool."
            if (plat < room) wr16(LIN(G32(0x5d92)) + 0x90, (u16)(P16(G32(0x5d92), 0x90) + plat));
            else { wr16(LIN(G32(0x5d92)) + 0x90, (u16)(P16(G32(0x5d92), 0x90) + room)); SETG32(0x6804, G32(0x6804) + (u16)(plat - room)); }
            wr16(LIN(G32(0x5d92)) + 0x8e, (u16)(P16(G32(0x5d92), 0x8e) + gold));
        }
    }
    text_clear_box(1, 0x15, 0x26, 0x16);
}
// 3C10:1F52  Items, Id (in a shop): 200 gp from the character or else the pool; then either "I can't tell anything new
// about your <item>" or the item loses its "unidentified" mark (35h) and "It looks like some sort of <item>".
void item_identify(farptr redraw, farptr item) {
    u8 paid = 0;
    chr_item_name(0, 0, 0, 0, item);
    say_item(0x1ec8, 0, 0, item, 0, 0);
    if (menu_yes_no(0x0f, 10, 0x0d, FARPTR(RSEG(0x3c10), 0x1ef0)) == 'Y') {
        s32 m = money_of(G32(0x5d92));
        if (m < 200) {
            s32 p = money_of_pool(FARPTR(DATA_SEG, 0x67f4));
            if (p < 200) ui_notice(&mem[LIN(FARPTR(RSEG(0x3c10), 0x1eff))]);                           // "Not Enough Money"
            else { paid = 1; pay_from_pool(FIX ? (u32)(p - 200) : (u16)(p - 200)); }
        } else { paid = 1; pay_from(FIX ? (u32)(m - 200) : (u16)(m - 200)); }
    }
    if (paid) {
        if (P8(item, 0x35) == 0) say_item(0x1f10, 0, 0, item, 0, 0);
        else { P8(item, 0x35) = 0; chr_item_name(0, 0, 0, 0, item); say_item(0x1f36, 0, 0, item, 0, 0); P8(redraw, 0) = 1; }
        text_pause();
    }
    text_clear_box(1, 0x15, 0x26, 0x16);
}

// 3C10:1A8A  Items, Use: a scroll (41CF:323D) asks which of its spells (3C10:299C); an item with a spell (3Dh, while
// 3Eh is below 80h) casts that one, after "<name> uses an item" and the item's name. In combat the caster's action is
// prepared first (10BA:2B88). Afterwards a scroll loses the spell (41CF:328A); otherwise one charge (3Ch) or, for a
// stack, one of the count goes, and an item out of charges is gone.
void item_use(farptr leave, farptr item) {
    u8 spell = 0;
    G8(0x6dbf) = 0;
    farptr v = scratch_push(4); wr16(LIN(v), 0xffff); P8(v, 2) = 0;
    if (!item_is_scroll(item)) {
        if (P8(item, 0x3d) && P8(item, 0x3e) < 0x80) { G8(0x6dbf) = 1; spell = P8(item, 0x3d); }
    } else { SETG32(0x6b80, item); spell = sheet_spell_list(2, 1, v, v + 2); }
    scratch_pop(4);
    if (!spell) P8(leave, 0) = 0;
    else {
        if (G8(0x49f3) == 5 && P8(G32(0x5d92), 0x10f) == 0) combat_prep_view();
        if (G8(0x6dbf)) {
            if (spell > 0x38) spell = (u8)(spell - 0x17);
            ui_char_message(0, 10, &mem[LIN(FARPTR(RSEG(0x3c10), 0x1a77))], G32(0x5d92));               // "uses an item"
            if (G8(0x49f3) == 5) { text_put_string(FARPTR(RSEG(0x3c10), 0x1a84), 10, 0x17, 0); chr_item_name(1, 0, 0x17, 5, item); }   // "Item:"
            else chr_item_name(1, 0, 0x16, 1, item);
            text_pause(); ui_clear_message();
        }
        G8(0x6dbf) = 1;
        call_original(0x41cf, 0x0c20, 5, spell, P8(G32(0x5d92), 0x10f), 0, FP_SEG(leave), FP_OFF(leave));
        G8(0x6dbf) = 0;
        if (G8(0x49f3) == 5 && G8(spell * 16 + 0x3205)) P8(leave, 0) = (u8)call_original(0x10ba, 0x2637, 2, FP_SEG(G32(0x5d92)), FP_OFF(G32(0x5d92)));
    }
    if (P8(leave, 0)) {
        if (item_is_scroll(item))
            call_original(0x41cf, 0x328a, 5, FP_SEG(G32(0x5d92)), FP_OFF(G32(0x5d92)), FP_SEG(item), FP_OFF(item), spell);
        else if (P8(item, 0x3c)) {
            if (P8(item, 0x39) < 2) { if (--P8(item, 0x3c) == 0) item_lose(item, G32(0x5d92)); }
            else P8(item, 0x39)--;
        }
    }
}

// 3C10:0EF8  View, Items: the character's items in a list with Ready, Use, Trade, Drop, Halve, Join and (in the
// shop) Sell and Id, until Exit, Esc, *leave being set, or no items left. The menu line depends on the situation:
// Use outside combat only in camp modes (or a combat turn of someone able to act), Trade/Sell only for someone
// conscious or under the party's control. Drop asks first. The individual actions other than Ready and Drop
// (1734 trade, 1CEE sell, 1F52 identify, 1A8A use, 41CF:323D, 10BA:1509 drop)
// are still original code. If the character has a count of items but no list, the original loops forever.
#define IT_CS(o) FARPTR(RSEG(0x3c10), (o))
static void it_cat(u8 *m, u16 o) { const u8 *t = &mem[LIN(IT_CS(o))]; int c = t[0]; if (m[0] + c > 0x28) c = 0x28 - m[0]; memcpy(m + 1 + m[0], t + 1, c); m[0] = (u8)(m[0] + c); }
void sheet_items(farptr leave) {
    farptr ch = G32(0x5d92), cur = P32(ch, 0xc8);
    u8 key = ' ', redraw = 1, full = 1;
    s16 index = 0;
    u8 st = G8(0x49f3);
    while (key != 0 && key != 'E' && P8(leave, 0) == 0 && P8(ch, 0xc7) > 0) {
        u8 count0 = P8(ch, 0xc7);
        if (P32(ch, 0xc8)) {
            u8 m[41]; m[0] = 0; it_cat(m, 0x0e83);                                   // Ready
            st = G8(0x49f3);
            int able = P8(ch, 0x84) < 0x80 || P8(ch, 0x10d) == 0 || P8(ch, 0x10c) == 1;
            if (P8(ch, 0x10d) && P16(G32(0x49d2), 0x1ca) == 0 &&
                (st == 2 || st == 3 || st == 4 || (st == 5 && P8(P32(ch, 0x108), 2) != 0))) it_cat(m, 0x0e89);   // Use
            if (able && st != 5) it_cat(m, 0x0e8e);                                    // Trade
            it_cat(m, 0x0e95);                                                         // Drop
            if (P8(ch, 0xc7) < 0x10) it_cat(m, 0x0e9b);                                // Halve
            it_cat(m, 0x0ea2);                                                         // Join
            if (able && st == 1) it_cat(m, 0x0ea8);                                    // Sell
            if (st == 1) it_cat(m, 0x0eae);                                            // Id
            for (farptr p = P32(ch, 0xc8); p; p = P32(p, 0x2a)) chr_item_name(0, 1, 0, 0, p);
            if (full || G8(0x6821)) {
                text_clear_screen();
                ui_border(0, 0x16, 0x26, 1, 1);
                hud_draw_name(1, 1, 1, ch);
                text_put_string(IT_CS(0x0eb2), 10, 1, (u8)(P8(ch, 0) + 4));            // "Items"
                ui_frame(IT_CS(0x0e64), 0, 0, 0x16, 0x26, 3, 1);
                text_put_string(IT_CS(0x0eb8), 0x0f, 3, 1);                             // "Ready Item"
                redraw = 1; full = 0; G8(0x6821) = 0;
            }
            key = menu_list(&cur, &index, &redraw, 1, P32(ch, 0xc8), 0x16, 0x26, 5, 1, 0x0f, 10, 0x0d, m, &mem[LIN(IT_CS(0x0e64))]);
            if (cur) {
                u16 cs = FP_SEG(cur), co = FP_OFF(cur);
                if (key == 'R') item_ready_toggle(cur);
                else if (key == 'U') {
                    if (P8(cur, 0x34) == 0) { ui_notice(&mem[LIN(IT_CS(0x0ec3))]); key = ' '; }       // "Must be Readied"
                    else if ((u8)call_original(0x41cf, 0x323d, 2, cs, co) || (P8(cur, 0x3d) && P8(cur, 0x3e) < 0x80)) {
                        item_use(leave, cur);
                        if (G8(0x49f3) != 5) P8(leave, 0) = 0;
                        if (P8(leave, 0) == 0) full = 1;
                    }
                } else if (key == 'T') {
                    if ((u8)item_may_part(cur)) item_trade(cur); else key = ' ';
                    full = 1;
                } else if (key == 'D') {
                    if (!(u8)item_may_part(cur)) key = ' ';
                    else {
                        chr_item_name(0, 0, 0, 0, cur);
                        u8 t[256]; const u8 *a = &mem[LIN(IT_CS(0x0ed3))], *b = &mem[LIN(IT_CS(0x0ed9))], *n = &P8(cur, 0);
                        int L = a[0]; memcpy(t + 1, a + 1, L);
                        int c = n[0]; if (L + c > 255) c = 255 - L; memcpy(t + 1 + L, n + 1, c); L += c;
                        c = b[0]; if (L + c > 255) c = 255 - L; memcpy(t + 1 + L, b + 1, c); L += c; t[0] = (u8)L;
                        text_print_wrapped_pstr(t, 1, 0x0e, 0x16, 0x26, 0x15, 1);           // "Your <item> will be gone forever"
                        if (menu_yes_no(0x0f, 10, 0x0d, IT_CS(0x0eee)) == 'Y') {            // "Drop It? "
                            item_lose(cur, G32(0x5d92));
                            redraw = 1;
                        }
                        text_clear_box(1, 0x15, 0x26, 0x16);
                    }
                } else if (key == 'H') item_halve(cur);
                else if (key == 'J') item_join(cur);
                else if (key == 'S') {
                    if ((u8)item_may_part(cur)) item_sell(cur); else key = ' ';
                } else if (key == 'I') {
                    farptr rv = scratch_push(2); P8(rv, 0) = redraw;
                    item_identify(rv, cur);
                    redraw = P8(rv, 0); scratch_pop(2);
                }
            }
            chr_recalculate(ch);
        }
        if (P8(ch, 0xc7) != count0) redraw = 1;
    }
}
#undef IT_CS

// 3FA9:044C  Moving coins of one kind from *from_var to *to_var unless it overloads the receiver. A bug kept: the
// weight is taken off the giver and then added back to the giver, so the receiver's load does not change (fixed
// when FIX is on).
void coins_transfer(s8 kind, u16 amount, farptr to_var, farptr from_var) {
    farptr to = P32(to_var, 0), from = P32(from_var, 0);
    if (chr_max_load(to) < (u16)(P16(to, 0x102) + amount)) { ui_notice(&mem[LIN(FARPTR(RSEG(0x3fa9), 0x0441))]); return; }   // "Overloaded"
    wr16(LIN(from) + 0x88 + kind * 2, (u16)(P16(from, 0x88 + kind * 2) - amount));
    chr_load_sub(amount, P32(from_var, 0));
    wr16(LIN(to) + 0x88 + kind * 2, (u16)(P16(to, 0x88 + kind * 2) + amount));
    chr_load_add(amount, FIX ? to : P32(from_var, 0));            // (FIX: the weight goes to the receiver)
}
// 3C10:2156  View, Trade (money): "Trade to?" a member, then a list of the coins carried; pick a kind and how many;
// repeat until no coins are left or the list is left, and then start again with "Trade to?" unless the flag at
// [BP-131h] says otherwise. That flag is set when no one was chosen or no coins are left, but leaving the list does
// not set it, so there the original reads a stale stack byte; the hook passes in the byte the original would see.
// Ghidra splits this routine into 2156, 21D4, 22C3 and 2468.
void sheet_trade_money(u8 stale) {
    static const u8 none[] = "";
    u8 quit = stale;
    do {
        farptr v = scratch_push(4); SETP32(v, 0, G32(0x471a));
        hud_draw_screen();
        chr_pick_character(v, 1, &mem[LIN(FARPTR(RSEG(0x3c10), 0x211a))]);                         // "Trade to?"
        farptr to = P32(v, 0);
        if (!to) { scratch_pop(4); quit = 1; break; }
        u8 done;
        do {
            sheet_draw(); sheet_draw_money();
            SETG32(0x471a, P32(v, 0));
            farptr head = 0, prev = 0;                  // [BP-10h]: the list's "current" variable doubles as "previous head"
            farptr sel = G32(0x5d92);
            for (int k = 0; k <= 6; k++) {
                if (P16(sel, 0x88 + k * 2) == 0) continue;
                if (head) prev = head;
                farptr node = rtl_getmem(0x2e);
                SETP32(node, 0x2a, head ? prev : 0); head = node;
                const u8 *nm = &G8(k * 0x0b + 0x0ffa);
                char num[16]; int dn = snprintf(num, sizeof num, "%u", P16(sel, 0x88 + k * 2)); if (dn > 15) dn = 15;
                u8 t[300]; int L = 0; u8 pad = (u8)(8 - nm[0]);
                for (int i = 0; i < pad; i++) t[L++] = ' ';
                memcpy(t + L, nm + 1, nm[0]); L += nm[0]; t[L++] = ' '; memcpy(t + L, num, dn); L += dn;
                if (L > 0x28) L = 0x28;
                P8(node, 0) = (u8)L; memcpy(&P8(node, 1), t, L); P8(node, 0x29) = 0;
            }
            farptr list = head, picked = prev; s16 index = 0; u8 redraw = 1;
            menu_list(&picked, &index, &redraw, 1, head, 0x0d, 0x19, 7, 0x0c, 0x0f, 10, 0x0d, none, &mem[LIN(FARPTR(RSEG(0x3c10), 0x2126))]);
            if (!picked) done = 1;
            else {
                farptr sc = scratch_push(0x200), nm = sc, q = sc + 0x100;
                P8(nm, 0) = 0;
                s8 kind = coin_kind(nm, picked);
                P8(q, 0) = 0; ps_concat(q, FARPTR(RSEG(0x3c10), 0x213b)); ps_concat(q, nm); ps_concat(q, FARPTR(RSEG(0x3c10), 0x2145));
                u16 amount = input_amount(P16(G32(0x5d92), 0x88 + kind * 2), q, 10);
                scratch_pop(0x200);
                coins_transfer(kind, amount, v, FARPTR(DATA_SEG, 0x5d92));
                done = 1; quit = 1;
                for (int k = 0; k <= 6; k++) if (P16(G32(0x5d92), 0x88 + k * 2) > 0) { quit = 0; done = 0; }
            }
            if (list) { farptr lv = scratch_push(4); SETP32(lv, 0, list); menu_list_free(lv); scratch_pop(4); }
        } while (!done);
        scratch_pop(4);
    } while (!quit);
}

// 3FA9:0508  Treasure, Pool: every member with status 0 or B3h puts all coins into the treasure (DS:67F4) and
// carries that much less. DS:6DC0 says there is something in the treasure. (Ghidra decodes this routine from the
// wrong alignment; the raw bytes are straightforward.)
void treasure_pool(void) {
    G8(0x6dc0) = 1;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        if (P8(c, 0x84) != 0 && P8(c, 0x84) != 0xb3) continue;
        for (int k = 0; k <= 6; k++) {
            SETG32(k * 4 + 0x67f4, G32(k * 4 + 0x67f4) + P16(c, 0x88 + k * 2));
            chr_load_sub(P16(c, 0x88 + k * 2), c);
        }
        memset(&P8(c, 0x88), 0, 0x0e);
    }
}

// 3FA9:0203  Adding platinum to the selected character; what would overload them goes to the treasure pool.
void add_platinum(u16 amount) {
    farptr ch = G32(0x5d92); u16 room;
    if (chr_too_heavy(&room, amount, ch)) {
        ui_notice(&mem[LIN(FARPTR(RSEG(0x3fa9), 0x01db))]);                                        // "Overloaded.  Money will be put in Pool."
        wr16(LIN(G32(0x5d92)) + 0x90, (u16)(P16(G32(0x5d92), 0x90) + room)); chr_load_add(room, G32(0x5d92));
        SETG32(0x6804, G32(0x6804) + (u32)amount - room);
    } else { wr16(LIN(G32(0x5d92)) + 0x90, (u16)(P16(G32(0x5d92), 0x90) + amount)); chr_load_add(amount, G32(0x5d92)); }
}
// 3FA9:19B2  Appraise: the selected character's gems (92h) and jewelry (94h) are valued one at a time (a gem by a
// d100 table, 10 to 5000 gp; a jewel at a random amount by a d100 band) and either sold for a fifth of the value in
// platinum or kept as an item, when there is room. Two quirks kept: at "You can :" any key but Keep sells (Esc too),
// and keeping one when the character carries no items builds the item but never attaches it, so it is lost
// (fixed when FIX is on).
static void appraise_one(int jewel, u16 value) {
    farptr sc = scratch_push(0x100), t = sc; u8 can_keep; u16 room;
    char num[8]; snprintf(num, sizeof num, "%u", value);
    P8(t, 0) = 0; ps_concat(t, FARPTR(RSEG(0x3fa9), jewel ? 0x199a : 0x1965));
    u8 *b = &P8(t, 0); int n = (int)strlen(num); memcpy(b + 1 + b[0], num, n); b[0] = (u8)(b[0] + n);
    ps_concat(t, FARPTR(RSEG(0x3fa9), 0x197b)); if (P8(t, 0) > 0x28) P8(t, 0) = 0x28;
    text_put_string(t, 0x0f, 0x0c, 1);
    can_keep = !chr_too_heavy(&room, 1, G32(0x5d92)) && P8(G32(0x5d92), 0xc7) < 0x10;
    u8 special = 0;
    u8 k = menu_horizontal(&special, 0, 1, 0x0f, 10, 0x0d, &mem[LIN(FARPTR(RSEG(0x3fa9), can_keep ? 0x1985 : 0x1980))], &mem[LIN(FARPTR(RSEG(0x3fa9), 0x198f))]);
    scratch_pop(0x100);
    if (k == 'K' && can_keep) {
        farptr it = rtl_getmem(0x3f);
        memset(&mem[LIN(it)], 0, 0x3f);
        P8(it, 0x31) = jewel ? 0xd6 : 0x65; P8(it, 0x2e) = 0x46; wr16(LIN(it) + 0x3a, value); wr16(LIN(it) + 0x37, 1);
        farptr p = P32(G32(0x5d92), 0xc8);
        if (p) { while (P32(p, 0x2a)) p = P32(p, 0x2a); SETP32(p, 0x2a, it); }
        else if (FIX) SETP32(G32(0x5d92), 0xc8, it);                     // (FIX: the first item is not lost)
    } else add_platinum((u16)(value / 5));
}
void appraise(farptr result) {
    farptr ch = G32(0x5d92);
    P8(result, 0) = 1;
    if (!P16(ch, 0x92) && !P16(ch, 0x94)) { ui_notice(&mem[LIN(FARPTR(RSEG(0x3fa9), 0x18e0))]); P8(result, 0) = 0; return; }   // "No Gems or Jewelry"
    for (;;) {
        ch = G32(0x5d92);
        if (!P16(ch, 0x92) && !P16(ch, 0x94)) break;
        farptr sc = scratch_push(0x100), g = sc, j = sc + 0x30, m = sc + 0x60;
        u16 ng = P16(ch, 0x92), nj = P16(ch, 0x94);
        #define NUMTXT(dst, v, one, many) do { if (!(v)) P8(dst, 0) = 0; else { char q[8]; int l = snprintf(q, sizeof q, "%u", (v)); P8(dst, 0) = (u8)l; memcpy(&P8(dst, 1), q, l); ps_concat(dst, FARPTR(RSEG(0x3fa9), (v) == 1 ? (one) : (many))); if (P8(dst, 0) > 0x28) P8(dst, 0) = 0x28; } } while (0)
        NUMTXT(g, ng, 0x18f3, 0x18f8); NUMTXT(j, nj, 0x18fe, 0x1910);
        #undef NUMTXT
        text_clear_box(1, 1, 0x26, 0x16);
        hud_draw_name(0, 1, 1, ch);
        text_put_string(FARPTR(RSEG(0x3fa9), 0x1923), 0x0f, 7, 1);                                  // "You have a fine collection of:"
        text_put_string(g, 0x0f, 9, 1); text_put_string(j, 0x0f, 10, 1);
        P8(m, 0) = 0;
        if (ng) ps_concat(m, FARPTR(RSEG(0x3fa9), 0x1942));
        if (nj) ps_concat(m, FARPTR(RSEG(0x3fa9), 0x1949));
        ps_concat(m, FARPTR(RSEG(0x3fa9), 0x1953));
        u8 special = 0;
        u8 k = menu_horizontal(&special, 0, 1, 0x0f, 10, 0x0d, &P8(m, 0), &mem[LIN(FARPTR(RSEG(0x3fa9), 0x1959))]);   // "Appraise : "
        scratch_pop(0x100);
        if (k == 'G') {
            if (P16(G32(0x5d92), 0x92)) {
                wr16(LIN(G32(0x5d92)) + 0x92, (u16)(P16(G32(0x5d92), 0x92) - 1));
                u8 r = roll_dice(100, 1); u16 v = 0;
                if (r >= 1 && r <= 0x19) v = 10; else if (r >= 0x1a && r <= 0x32) v = 0x32; else if (r >= 0x33 && r <= 0x46) v = 100;
                else if (r >= 0x47 && r <= 0x5a) v = 500; else if (r >= 0x5b && r <= 99) v = 1000; else if (r == 100) v = 5000;
                appraise_one(0, v);
            }
        } else if (k == 'J') {
            if (P16(G32(0x5d92), 0x94)) {
                wr16(LIN(G32(0x5d92)) + 0x94, (u16)(P16(G32(0x5d92), 0x94) - 1));
                u8 r = roll_dice(100, 1); u16 v = 0;
                static const struct { u8 lo, hi; u16 range, base; } band[7] = { {1,10,900,100}, {11,20,1000,200}, {21,40,1500,300}, {41,50,2500,500}, {51,70,5000,1000}, {71,90,6000,2000}, {91,100,10000,2000} };
                for (int i = 0; i < 7; i++) if (r >= band[i].lo && r <= band[i].hi) { v = (u16)(rtl_random(band[i].range) + band[i].base); break; }
                appraise_one(1, v);
            }
        } else if (k == 'E' || k == 0) { chr_recalculate(G32(0x5d92)); break; }
        chr_recalculate(G32(0x5d92));
    }
}

// 3C10:24A7  View, Drop: choose a kind of coin from a list of what the character carries and
// say how many to leave behind; repeat until nothing is left or Exit is chosen. The list is built
// back to front, so the most valuable kind comes first. The question, the number entry and the
// subtraction itself are native too (coin_kind, input_amount, chr_drop_coins).
void sheet_drop_money(void) {
    static const u8 none[] = {0}, prompt[] = "\023Select type of coin";
    u8 done;
    do {
        farptr sel = G32(0x5d92), head = 0, picked = 0;
        sheet_draw_money();
        for (s8 i = 0; i <= 6; i++) {
            if (P16(sel, 0x88 + i * 2) == 0) continue;
            farptr node = rtl_getmem(0x2e);
            SETP32(node, 0x2a, head);
            head = node;
            const char *name = word_from(0x0ffa, 11, (u8)i);
            char t[64];
            int n = snprintf(t, sizeof t, "%*s%s %u", (int)(8 - strlen(name)), "", name, P16(sel, 0x88 + i * 2));
            if (n > 40) n = 40;
            P8(head, 0) = (u8)n; memcpy(&P8(head, 1), t, (size_t)n);
            P8(head, 0x29) = 0;
        }
        s16 index = 0; u8 redraw = 1;
        menu_list(&picked, &index, &redraw, 1, head, 0x0d, 0x19, 7, 0x0c, 15, 10, 13, none, prompt);
        if (!picked) done = 1;
        else {
            farptr buf = scratch_push(258);
            char q[300];
            s8 kind = coin_kind(buf, picked);
            int n = snprintf(q, sizeof q, "How much %swill you drop? ", pstr(buf));
            if (n > 255) n = 255;
            P8(buf, 0) = (u8)n; memcpy(&P8(buf, 1), q, (size_t)n);
            u16 amount = input_amount(P16(sel, 0x88 + kind * 2), buf, 10);
            scratch_pop(258);
            sel = G32(0x5d92);
            chr_drop_coins(kind, amount, sel);
            done = 1;
            for (int i = 0; i <= 6; i++) if (P16(G32(0x5d92), 0x88 + i * 2) > 0) done = 0;
        }
        if (head) menu_list_dispose(head);
    } while (!done);
}

// 3C10:299C  The spell list screen. `kind` picks which spells and the heading: 0 in Memory,
// 1 in Spell Book, 2 on Scroll, 3 on Scrolls, 4 to choose from, 5 to be memorized, 6 to be
// scribed. *any is set if there is anything to list. A negative *index means "first time in":
// draw the frame and "<name>'s Spells <heading>". Building the list (41CF:059C) and choosing
// from it (41CF:008A) are still original code. Returns the spell chosen, or 0.
u8 sheet_spell_list(u8 kind, u8 mode, farptr index_var, farptr any_var) {
    static const char *heading[7] = {"in Memory", "in Spell Book", "on Scroll", "on Scrolls", "to choose from", "to be memorized", "to be scribed"};
    P8(any_var, 0) = spell_list_build(kind);
    if (P8(any_var, 0) == 0) return 0;
    if ((s16)P16(index_var, 0) < 0) {
        farptr sel = G32(0x5d92); char t[48];
        ui_frame(FARPTR(RSEG(0x3c10), 0x2993), 0, 0, 1, 0x26, 1, 1);
        hud_draw_name(1, 1, 1, sel);
        snprintf(t, sizeof t, "Spells %s", kind <= 6 ? heading[kind] : "");
        put(t, 10, 1, (u8)(P8(sel, 0) + 4));
    }
    return spell_list_choose(index_var, mode);
}

// 41CF:059C  Build the list of spells for a spell screen (head at DS:4766, with a 21-byte
// table at DS:476A cleared first). The kinds are those of sheet_spell_list: 0 the spells in
// memory, 5 those picked but not yet learned, 1 the spell book, 4 the spells the character could
// learn and does not know, 2/3/6 scroll spells. Adding one spell to the list (41CF:028D) and
// the scroll scans are still original code. Returns 1 if the list is not empty.
u8 spell_list_build(u8 kind) {
    farptr sel = G32(0x5d92);
#define ADD(spell) call_original(0x41cf, 0x028d, 1, (u8)(spell))
    SETG32(0x4766, 0);
    memset(&G8(0x476a), 0, 21);
    if (kind == 0) { for (int i = 0; i <= 0x14; i++) if (P8(sel, 0x17 + i) != 0 && P8(sel, 0x17 + i) < 0x80) ADD(P8(sel, 0x17 + i)); }
    else if (kind == 5) { for (int i = 0; i <= 0x14; i++) if (P8(sel, 0x17 + i) > 0x7f) ADD(P8(sel, 0x17 + i)); }
    else if (kind == 1) { for (int sp = 1; sp <= 0x37; sp++) if (P8(sel, 0x32 + sp) != 0) ADD(sp); }
    else if (kind == 2) call_original(0x41cf, 0x044e, 1, 0);
    else if (kind == 3) spell_list_scrolls(0);
    else if (kind == 6) spell_list_scrolls(1);
    else if (kind == 4) {
        for (int sp = 1; sp <= 0x38; sp++)
            if (P8(sel, 0xb1 + (s8)G8(0x31fa + sp * 16) * 3 + (s8)G8(0x31fb + sp * 16)) != 0 && P8(sel, 0x32 + sp) == 0) ADD(sp);
    }
#undef ADD
    return G32(0x4766) != 0;
}

// 41CF:008A  Show the spell list built above and let the player choose. `mode` gives the verb on
// the menu line: 1 Cast, 2 Memorize, 3 Scribe, 4 Learn (anything else: just look). Headings in the
// list ("1st Level") are skipped when counting, and the count indexes the table of spell numbers
// at DS:476A. For Scribe the scroll the spell is on is also noted (DS:6B80). The list is freed.
u8 spell_list_choose(farptr index_var, u8 mode) {
    static const u8 verbs[5][10] = {"", "\004Cast", "\010Memorize", "\006Scribe", "\005Learn"}, none[] = {0}, ask[] = "\016Choose Spell: ";
    int listed = mode >= 1 && mode <= 4;
    s8 bottom = mode == 2 ? 0x0f : 0x16;
    s16 index = (s16)P16(index_var, 0);
    u8 redraw = (mode == 4 || mode == 1) ? 1 : index < 0, key, count = 0;
    if (index < 0) { ui_frame(FARPTR(RSEG(0x41cf), 0x001c), 0, 0, (u8)bottom, 0x26, 3, 1); index = 0; SETP16(index_var, 0, 0); }
    farptr picked = 0;
    do {
        key = menu_list(&picked, &index, &redraw, mode != 4, G32(0x4766), bottom, 0x26, 5, 1, 15, 10, 13, listed ? verbs[mode] : none, listed ? ask : none);
        SETP16(index_var, 0, (u16)index);
    } while (key != 0 && key != '\r' && key != 'C' && key != 'E' && key != 'L' && key != 'M' && key != 'S');
    for (farptr n = G32(0x4766); n != picked; n = P32(n, 0x2a)) if (P8(n, 0x29) == 0) count++;
    u8 spell = (key == 0 || key == 'E') ? 0 : G8(0x476a + count);
    if (mode == 3) SETG32(0x6b80, G32(0x477f + count * 4));
    menu_list_free(FARPTR(DATA_SEG, 0x4766));
    return spell;
}

// ---- 34FB:4235  Rolling hit points for each class in the mask: a level-1 class gets at least two thirds of its die ----
u8 chr_roll_hp(farptr ch, u8 mask) {
    u8 sum = 0;
    for (int k = 0; k <= 7; k++) {
        if ((s8)P8(ch, k + 0x96) <= 0 || !(G8(k + 0x5de) & mask)) continue;
        u8 r = roll_dice(G8(k + 0x8be), G8(k + 0x8b6));
        u8 floor = (u8)((G8(k + 0x8be) * 2) / 3);
        if (P8(ch, k + 0x96) == 1 && r < floor) r = floor;
        sum = (u8)(sum + r);
    }
    return sum;
}
// ---- 34FB:3F31  The Constitution hit-point bonus, once per class; class 2 gets up to +2 more at 17 and 18 ----
s8 chr_con_bonus(farptr ch) {
    s8 b = 0;
    u8 con = P8(ch, 0x14);
    for (int k = 0; k <= 7; k++) {
        if ((s8)P8(ch, k + 0x96) <= 0) continue;
        b = (s8)(b + (s8)G8(con + 0x408e));
        if (con > 0x10 && P8(ch, 0x2f) == 2) b++;
        if (con > 0x11 && P8(ch, 0x2f) == 2) b++;
    }
    return b;
}
// ---- 454B:01AF  Bonus cleric spells for high Wisdom: one more first-level at 13 and 14, second at 15 and 16, third at 17 ----
void chr_wisdom_spells(farptr ch) {
    u8 w = P8(ch, 0x12);
    if (w > 0x0c && P8(ch, 0xb2)) P8(ch, 0xb2)++;
    if (w > 0x0d && P8(ch, 0xb2)) P8(ch, 0xb2)++;
    if (w > 0x0e && P8(ch, 0xb3)) P8(ch, 0xb3)++;
    if (w > 0x0f && P8(ch, 0xb3)) P8(ch, 0xb3)++;
    if (w > 0x10 && P8(ch, 0xb4)) P8(ch, 0xb4)++;
}
// ---- 34FB:33E7  Choosing the portrait: Head and Body step through the pictures, Keep accepts ----
void chr_pick_portrait(void) {
    farptr ch = G32(0x5d92);
    u8 head = P8(ch, 0xbb), body = P8(ch, 0xbc), k;
    static const u8 none[] = "";
    do {
        scene_show_character_portrait();
        u8 special = 0;
        k = menu_horizontal(&special, 0, 0, 10, 0x0f, 0, &mem[LIN(FARPTR(RSEG(0x34fb), 0x33d8))], none);
        if (special == 0) {
            if (k == 'H') P8(ch, 0xbb) = P8(ch, 0xbb) < 0x0e ? P8(ch, 0xbb) + 1 : 1;
            else if (k == 'B') P8(ch, 0xbc) = P8(ch, 0xbc) < 0x0c ? P8(ch, 0xbc) + 1 : 1;
            else if (k == 'K') { head = P8(ch, 0xbb); body = P8(ch, 0xbc); }
        }
    } while (k != 'K');
    P8(ch, 0xbb) = head; P8(ch, 0xbc) = body;
    menu_clear_line();
}
// ---- 34FB:056C  Creating a character ----
// Picks race, gender, class and alignment from list menus, derives the class levels, starting age and colours, rolls
// the abilities (best of six 3d6 each, adjusted for age and clamped to the race's and class's limits) and hit points
// until the player keeps the character, then asks for a name and saves it (3941:069E). Esc in a list abandons it.
// A quirk of the original: the racial adjustments to the ability rolls test the race-list loop counter, which is
// left at 7 when they are applied, so no race ever gets them; the translation keeps that (fixed when FIX is on).
static farptr cc_item(farptr head, s16 n) { return list_nth(head, n); }
static void cc_set_text(farptr item, const u8 *a, const u8 *b) {
    u8 t[256]; int n = a[0]; memcpy(t + 1, a + 1, n);
    int k = b ? b[0] : 0; if (n + k > 255) k = 255 - n; if (k) memcpy(t + 1 + n, b + 1, k); t[0] = (u8)(n + k);
    u8 m = t[0] > 40 ? 40 : t[0]; P8(item, 0) = m; memcpy(&P8(item, 1), t + 1, m);
}
#define CC_CS(o) (&mem[LIN(FARPTR(RSEG(0x34fb), (o)))])
static u8 cc_pick(farptr listvar, s16 *idx) {          // returns 0 if Esc, else the chosen line in *idx
    farptr res = P32(listvar, 0); u8 redraw = 1; *idx = 1;
    static const u8 none[] = "";
    u8 k;
    do { k = menu_list(&res, idx, &redraw, 1, P32(listvar, 0), 0x16, 0x26, 2, 1, 0x0f, 10, 0x0d, none, none); if (k == 0) return 0; } while (k != 13);
    return 1;
}
static void cc_list_head(farptr listvar, s16 count, u16 title) {
    menu_list_alloc(listvar, count);
    farptr h = P32(listvar, 0);
    P8(h, 0x29) = 1;
    const u8 *t = CC_CS(title); u8 m = t[0] > 40 ? 40 : t[0]; P8(h, 0) = m; memcpy(&P8(h, 1), t + 1, m);
}
void chr_create(void) {
    farptr ch = rtl_getmem(0x11d);
    memset(&P8(ch, 0), 0, 0x11d);
    for (int k = 1; k <= 6; k++) P8(ch, 0xc0 + k) = (u8)(G8(0x3cf5 + k) + (u8)(G8(0x3cf5 + k) + 8) * 16);
    P8(ch, 0xa9) = 0x32; P8(ch, 0x2d) = 0x28; P8(ch, 0x10c) = 0; P8(ch, 0x10d) = 1;
    P8(ch, 0xbb) = 1; P8(ch, 0xbc) = 1; P8(ch, 0x6c) = 1;
    P8(ch, 0xab) = (u8)rtl_random(0x100);
    farptr lv = scratch_push(4);
    const u8 *sp = CC_CS(0x04fb);
    s16 idx;
    // race
    cc_list_head(lv, 7, 0x04f1);
    u8 racevar = 1;
    for (racevar = 1; ; racevar++) {
        if (racevar >= 1 && racevar <= 5) cc_set_text(cc_item(P32(lv, 0), racevar), sp, &G8(racevar * 10 + 0x0f03));
        else if (racevar == 7) cc_set_text(cc_item(P32(lv, 0), 6), sp, &G8(racevar * 10 + 0x0f03));
        if (racevar == 7) break;
    }
    if (!cc_pick(lv, &idx)) { scratch_pop(4); return; }
    if (idx == 6) idx = 7;
    P8(ch, 0x2e) = (u8)idx;
    u8 race = P8(ch, 0x2e);
    static const u8 fx5[] = { 0x5a, 0x61 }, fx1[] = { 0x5a, 0x61, 0x1a, 0x2f }, fx3[] = { 0x61, 0x12, 0x2f, 0x30 };
    if (race == 5) { P8(ch, 0xc0) = 1; for (int i = 0; i < 2; i++) effect_add(0, 0xff, 0, fx5[i], ch); }
    else if (race == 1) { P8(ch, 0xc0) = 1; for (int i = 0; i < 4; i++) effect_add(0, 0xff, 0, fx1[i], ch); }
    else if (race == 3) { P8(ch, 0xc0) = 1; for (int i = 0; i < 4; i++) effect_add(0, 0xff, 0, fx3[i], ch); }
    else if (race == 2) { P8(ch, 0xc0) = 2; effect_add(0, 0xff, 0, 0x6b, ch); }
    else if (race == 4) { P8(ch, 0xc0) = 2; effect_add(0, 0xff, 0, 0x7c, ch); }
    else P8(ch, 0xc0) = 2;
    menu_list_free(lv);
    // gender
    cc_list_head(lv, 3, 0x04ff);
    for (int k = 0; k <= 1; k++) cc_set_text(cc_item(P32(lv, 0), k + 1), sp, &G8(k * 7 + 0x0fec));
    if (!cc_pick(lv, &idx)) { scratch_pop(4); return; }
    P8(ch, 0x9e) = (u8)(idx - 1);
    menu_list_free(lv);
    // class
    u8 n = G8(race * 0x0c + 0x3e17);
    cc_list_head(lv, n + 1, 0x050b);
    for (int k = 1; k <= n; k++) cc_set_text(cc_item(P32(lv, 0), k), sp, &G8((s8)G8(race * 0x0c + k + 0x3e17) * 0x1b + 0x0d38));
    if (!cc_pick(lv, &idx)) { scratch_pop(4); return; }
    P8(ch, 0x2f) = G8(race * 0x0c + idx + 0x3e17);
    P8(ch, 0x73) = 1;
    s8 cls = (s8)P8(ch, 0x2f);
    static const u8 multi[9][4] = { {0x96,0x98}, {0x96,0x98,0x9b}, {0x96,0x9a}, {0x96,0x9b}, {0x96,0x9c}, {0x98,0x9b}, {0x98,0x9c}, {0x98,0x9b,0x9c}, {0x9b,0x9c} };
    if (cls >= 0 && cls <= 7) P8(ch, cls + 0x96) = 1;
    else if (cls >= 8 && cls <= 16) for (int i = 0; i < 4 && multi[cls - 8][i]; i++) P8(ch, multi[cls - 8][i]) = 1;
    if ((s8)P8(ch, 0x9c) > 0) chr_thief_skills(ch);
    P8(ch, 0xb0) = 0; P8(ch, 0x2d) = 0;
    for (int k = 0; k <= 7; k++) {
        s8 l = (s8)P8(ch, k + 0x96);
        if (l <= 0) continue;
        u8 v = G8((u16)(k * 0x0b + l + 0x3c7c));
        if (P8(ch, 0x2d) < v) P8(ch, 0x2d) = v;
        P8(ch, 0xb0) += G8(k + 0x5d6);
    }
    chr_saving_throws(ch);
    menu_list_free(lv);
    // alignment
    n = G8(cls * 10 + 0x3fe7);
    cc_list_head(lv, n + 1, 0x0516);
    for (int k = 1; k <= n; k++) cc_set_text(cc_item(P32(lv, 0), k), sp, &G8((s8)G8(cls * 10 + k + 0x3fe7) * 0x11 + 0x0f53));
    if (!cc_pick(lv, &idx)) { scratch_pop(4); return; }
    P8(ch, 0xa0) = G8(cls * 10 + idx + 0x3fe7);
    menu_list_free(lv);
    scratch_pop(4);
    // starting age
    if (cls < 8) {
        u16 e = (u16)(race * 0x1c + cls * 4);
        u8 r = roll_dice(G8(e + 0x3e5e), G8(e + 0x3e5d));
        wr16(LIN(ch) + 0x30, (u16)(r + G16(e + 0x3e5b)));
    } else {
        u16 e = 0;
        if (cls == 8 || cls == 9 || cls == 0x0b || cls == 0x0c) e = 0x3e5b;
        else if (cls == 0x0d || cls == 0x0f || cls == 0x10) e = 0x3e6f;
        else if (cls == 0x0e) e = 0x3e63;
        if (e) wr16(LIN(ch) + 0x30, (u16)(G16(race * 0x1c + e) + G8(race * 0x1c + e + 3) * G8(race * 0x1c + e + 2)));
    }
    farptr was = G32(0x5d92);
    SETP32(FARPTR(DATA_SEG, 0), 0x5d92, ch);
    sheet_draw();
    u8 keep;
    do {
        P8(ch, 0x16) = 0;
        for (s8 a = 0; a <= 5; a++) {
            P8(ch, a + 0x10) = 0;
            for (int t = 1; t <= 6; t++) {
                u8 r = roll_dice(6, 3);
                // racial adjustments keyed on racevar, which is 7 here: none apply (FIX: keyed on the chosen race)
                u8 rv = FIX ? race : racevar;
                if (rv == 1) { if (a == 4) r++; else if (a == 5) r--; }
                else if (rv == 2) { if (a == 3) r++; else if (a == 4) r--; }
                else if (rv == 5) { if (a == 0) r--; else if (a == 3) r++; }
                else if (rv == 6) { if (a == 0) r++; else if (a == 4) r++; else if (a == 5) r -= 2; }
                if (P8(ch, a + 0x10) < r) P8(ch, a + 0x10) = r;
            }
            u16 age = P16(ch, 0x30);
            #define AGE(i) G16(race * 10 + 0x3f31 + (i) * 2)
            #define LIM(o) G8(race * 0x10 + (o))
            u8 *v = &P8(ch, a + 0x10);
            if (a == 0) {
                if (*v && AGE(0) < age) (*v)++;
                if (*v && AGE(1) < age) (*v)--;
                if (*v && AGE(2) < age) *v -= 2;
                if (*v && AGE(3) < age) (*v)--;
                if (*v < G8(race * 0x10 + (s8)P8(ch, 0x9e) + 0x3da3)) *v = G8(race * 0x10 + (s8)P8(ch, 0x9e) + 0x3da3);
                if (G8(race * 0x10 + (s8)P8(ch, 0x9e) + 0x3da5) < *v) *v = G8(race * 0x10 + (s8)P8(ch, 0x9e) + 0x3da5);
                if (*v < G8(cls * 6 + 0x3f81)) *v = G8(cls * 6 + 0x3f81);
                if (*v == 0x12 && (s8)P8(ch, 0x98) > 0) {
                    P8(ch, 0x16) = (u8)(rtl_random(100) + 1);
                    u8 cap = G8(race * 0x10 + (s8)P8(ch, 0x9e) + 0x3da7);
                    if (cap < P8(ch, 0x16)) P8(ch, 0x16) = cap;
                }
            } else if (a == 1) {
                if (AGE(1) < age) (*v)++;
                if (AGE(3) < age) (*v)++;
                if (*v < LIM(0x3da9)) *v = LIM(0x3da9);
                if (LIM(0x3daa) < *v) *v = LIM(0x3daa);
                if (*v < G8(cls * 6 + 0x3f82)) *v = G8(cls * 6 + 0x3f82);
            } else if (a == 2) {
                if (age <= AGE(0)) (*v)--;
                if (AGE(1) < age) (*v)++;
                if (AGE(2) < age) (*v)++;
                if (AGE(3) < age) (*v)++;
                if (*v < LIM(0x3dab)) *v = LIM(0x3dab);
                if (LIM(0x3dac) < *v) *v = LIM(0x3dac);
                if (*v < G8(cls * 6 + 0x3f83)) *v = G8(cls * 6 + 0x3f83);
                if (*v < 0x0d && cls >= 8 && cls <= 12) *v = 0x0d;      // set {8..12} at CS:0525: multi-class clerics
            } else if (a == 3) {
                if (AGE(2) < age) *v -= 2;
                if (AGE(3) < age) (*v)--;
                if (*v < LIM(0x3dad)) *v = LIM(0x3dad);
                if (LIM(0x3dae) < *v) *v = LIM(0x3dae);
                if (*v < G8(cls * 6 + 0x3f84)) *v = G8(cls * 6 + 0x3f84);
            } else if (a == 4) {
                if (AGE(1) < age) (*v)--;
                if (AGE(2) < age) (*v)--;
                if (AGE(3) < age) (*v)--;
                if (*v < LIM(0x3daf)) *v = LIM(0x3daf);
                if (LIM(0x3db0) < *v) *v = LIM(0x3db0);
                if (*v < G8(cls * 6 + 0x3f85)) *v = G8(cls * 6 + 0x3f85);
            } else {
                if (*v < LIM(0x3db1)) *v = LIM(0x3db1);
                if (LIM(0x3db2) < *v) *v = LIM(0x3db2);
                if (*v < G8(cls * 6 + 0x3f86)) *v = G8(cls * 6 + 0x3f86);
            }
            #undef AGE
            #undef LIM
            sheet_draw_ability(0, a);
        }
        P8(ch, 0x11b) = P8(ch, 0x32);
        P8(ch, 0xa1) = 2; P8(ch, 0xa3) = 1; P8(ch, 0xa5) = 2; P8(ch, 0xaa) = 1; P8(ch, 0x72) = 0x0c;
        u8 sum = 0, cnt = 0;
        for (int k = 0; k <= 7; k++) {
            if ((s8)P8(ch, k + 0x96) <= 0) continue;
            if (k == 0) P8(ch, 0xb2) = 1; else if (k == 5) P8(ch, 0xb5) = 1;
            sum = (u8)(sum + roll_dice(G8(k * 2 + 0x4264), G8(k * 2 + 0x4263)));
            cnt++;
            if (k == 0) {
                chr_wisdom_spells(ch);
                for (int s = 1; s <= 0x38; s++) if (G8(s * 16 + 0x31fa) == 0 && G8(s * 16 + 0x31fb) == 1) P8(ch, s + 0x32) = 1;
            } else if (k == 5) { P8(ch, 0x3d) = 1; P8(ch, 0x44) = 1; P8(ch, 0x45) = 1; P8(ch, 0x47) = 1; }
        }
        wr16(LIN(ch) + 0x8e, (u16)((u16)(sum * 10) / cnt));
        P8(ch, 0xb1) = chr_roll_hp(ch, 0xff);
        P8(ch, 0x32) = P8(ch, 0xb1);
        int adj = chr_con_bonus(ch);
        if (adj < 0) {
            if (-adj + cnt < (int)P8(ch, 0x32)) P8(ch, 0x32) = (u8)(((int)P8(ch, 0x32) + adj) / (int)cnt);
            else P8(ch, 0x32) = 1;
        } else P8(ch, 0x32) = (u8)(((int)P8(ch, 0x32) + adj) / (int)cnt);
        P8(ch, 0x11b) = P8(ch, 0x32);
        P8(ch, 0xb1) = (u8)(P8(ch, 0xb1) / cnt);
        sheet_draw_combat_stats();
        sheet_draw_money();
        keep = menu_yes_no(0x0f, 10, 0x0d, FARPTR(RSEG(0x34fb), 0x0545));     // "Keep this character? "
    } while (keep != 'Y');
    sheet_draw();
    SETP32(FARPTR(DATA_SEG, 0), 0x5d92, was);
    farptr nm = scratch_push(256);
    do {
        text_input_string(0x0f, 0x0d, FARPTR(RSEG(0x34fb), 0x055b), nm);          // "Character name: "
        u8 l = P8(nm, 0) > 15 ? 15 : P8(nm, 0); P8(ch, 0) = l; memcpy(&P8(ch, 1), &P8(nm, 1), l);
    } while (P8(ch, 0) == 0);
    scratch_pop(256);
    was = G32(0x5d92);
    SETP32(FARPTR(DATA_SEG, 0), 0x5d92, ch);
    chr_pick_portrait();
    call_original(0x34fb, 0x382d, 0);
    chr_save_file(ch, FARPTR(RSEG(0x34fb), 0x04fe));
    SETP32(FARPTR(DATA_SEG, 0), 0x5d92, was);
    rtl_freemem(ch, 0x11d);
}
#undef CC_CS

// ---- 34FB:0161  The party menu (outside the game): Create, Drop, Modify, Train, View, Add, Remove, Load, Save,
// Begin, Exit. The entries live at DS:0619 + k*2Ah (name, then an enabled flag at +29h) and the menu line at DS:05F0.
// Home/End move the selection. Any command but Exit and Save clears "the game is saved" (DS:557D). Load returns once
// a game is loaded; Begin returns with display mode 4. A hidden J: if the game was started with the argument STING it
// answers "I Understand, master..." and sets DS:470C, which enables Train regardless of the usual condition.
void party_menu(void) {
    G8(0x5376) = 3;
    u8 begin_mode = 4, redraw = 1;
    G8(0x470c) = 0; G8(0x49f3) = 0;
    static const u8 *cs0;
    cs0 = &mem[LIN(FARPTR(RSEG(0x34fb), 0))];
    for (;;) {
        if (redraw) {
            ui_frame(FARPTR(RSEG(0x34fb), 0x00e2), 0, 0, 0x16, 0x26, 1, 1);
            if (!G32(0x5d92)) {
                G8(0x66c) = 0; G8(0x696) = 0; G8(0x6c0) = 0; G8(0x6ea) = 0; G8(0x73e) = 0; G8(0x768) = 1; G8(0x792) = 0; G8(0x7bc) = 0;
            } else {
                hud_draw_roster(G32(0x5d92));
                G8(0x66c) = 1; G8(0x696) = 1;
                if (P16(G32(0x49d6), 0x550) != 0 || G8(0x470c)) G8(0x6c0) = 1;
                G8(0x6ea) = 1; G8(0x73e) = 1; G8(0x768) = 0; G8(0x792) = 1; G8(0x7bc) = 1;
            }
            u8 row = 0;
            farptr sc = scratch_push(0x100);
            for (int k = 0; k <= 10; k++) {
                u16 e = (u16)(k * 0x2a + 0x619);
                if (!G8(e + 0x29)) continue;
                P8(sc, 0) = 1; P8(sc, 1) = G8(e + 1);
                text_put_string(sc, 0x0f, (u8)(row + 0x0c), 2);
                u8 n = G8(e) ? (u8)(G8(e) - 1) : 0; P8(sc, 0) = n; memcpy(&P8(sc, 1), &G8(e + 2), n);    // Copy(name, 2, len-1)
                text_put_string(sc, 10, (u8)(row + 0x0c), 3);
                row++;
            }
            scratch_pop(0x100);
            redraw = 0;
        }
        u8 special = 0;
        u8 key = menu_horizontal(&special, 0, 1, 0, 0, 0x0d, &G8(0x05f0), cs0 + 0x00e3);      // "Choose a function "
        menu_clear_line();
        if (special) {
            if (key == 0x47 || key == 0x4f) { party_select_move(key); hud_draw_roster(G32(0x5d92)); }
            continue;
        }
        if (key != 'E' && key != 'S') G8(0x557d) = 0;
        if (key == 'C') { if (G8(0x642)) chr_create(); }
        else if (key == 'D') { if (G8(0x66c)) party_drop(); }
        else if (key == 'M') { if (G8(0x696)) chr_modify(); }
        else if (key == 'T') { if (G8(0x6c0)) chr_train(); }
        else if (key == 'V') {
            if (G8(0x6ea)) { farptr lv = scratch_push(2); P8(lv, 0) = 0; sheet_view(lv); scratch_pop(2); }
        }
        else if (key == 'A') { if (G8(0x714)) party_add_menu(); }
        else if (key == 'R') {
            if (G8(0x73e) && G32(0x5d92)) { chr_save_file(G32(0x5d92), FARPTR(RSEG(0x34fb), 0x00e2)); party_remove(0, 1); }
        }
        else if (key == 'L') {
            if (G8(0x768)) {
                game_load();
                if (G8(0x49f3)) { G8(0x49f8) = 1; return; }
            }
        }
        else if (key == 'S') { if (G8(0x792) && G32(0x5d96)) game_save(); }
        else if (key == 'B') {
            if (G8(0x7bc) && G32(0x5d96)) { G8(0x49f3) = begin_mode; menu_clear_line(); return; }
        }
        else if (key == 'E') {
            if (G8(0x7e6) && menu_yes_no(0x0f, 10, 0x0e, FARPTR(RSEG(0x34fb), 0x0136)) == 'Y') {       // "Quit to DOS "
                while (!G8(0x557d) && G32(0x5d96)) game_save();
                game_quit();
            }
        }
        else if (key == 'J') {
            const char *a = plat_param1();
            if (a && !strcmp(a, "STING")) { G8(0x470c) = 1; ui_notice(cs0 + 0x0149); }                // "I Understand, master..."
        }
        redraw = 1;
    }
}

// ---- 34FB:382D  The icon editor, with its two nested helpers (35BE copy an icon slot, 34DF show the icons) ----
// Slots of icon pictures: DS:5E5A + n*8 (ready pose) and 5E5E + n*8 (action pose); slot 0Ch is the work copy.
// 34FB:35BE: copy slot src's pictures over slot dst's (header from +17h, pixel data at +13h; a quarter of it in mode
// 4A1E=1), then optionally recolour dst with the selected character's six colour pairs (ch+C1h..C6h, low nibble for
// the dark shade, high for the bright, placed by the table at DS:3CF6).
void icon_copy(u8 recolor, u8 dst, u8 src) {
    farptr d0 = G32(dst * 8 + 0x5e5a), s0 = G32(src * 8 + 0x5e5a), d1 = G32(dst * 8 + 0x5e5e), s1 = G32(src * 8 + 0x5e5e);
    u16 n = P16(d0, 0x11), q = G8(0x4a1e) == 1 ? (u16)(n >> 2) : n;
    memmove(&mem[LIN(d0 + 0x17)], &mem[LIN(s0 + 0x17)], n);
    memmove(&mem[LIN(d1 + 0x17)], &mem[LIN(s1 + 0x17)], P16(d1, 0x11));
    memmove(&mem[LIN(P32(d0, 0x13))], &mem[LIN(P32(s0, 0x13))], q);
    memmove(&mem[LIN(P32(d1, 0x13))], &mem[LIN(P32(s1, 0x13))], q);
    if (!recolor) return;
    farptr ch = G32(0x5d92), t = scratch_push(0x20), nw = t, old = t + 0x10;
    for (int i = 0; i <= 15; i++) { P8(old, i) = (u8)i; P8(nw, i) = (u8)i; }
    for (int k = 1; k <= 6; k++) { u8 c = G8(0x3cf5 + k), v = P8(ch, 0xc0 + k); P8(nw, c) = v & 0x0f; P8(nw, c + 8) = v >> 4; }
    gfx_recolor_pic(G32(dst * 8 + 0x5e5a), 0, 0, nw, old);
    gfx_recolor_pic(G32(dst * 8 + 0x5e5e), 0, 0, nw, old);
    scratch_pop(0x20);
}
// 34FB:34DF  Showing the edited icon (slot 0Ch) in both poses at (row, col), over a cleared patch; in mode 4A1E=1 the
// old one (slot 19h) is drawn first.
static void icon_figure(u8 slot, u8 b, u8 row, u8 col) { call_original(0x4c2b, 0x04fc, 5, col, row, 0, b, slot); }
void icon_show(u8 row, u8 col) {
    call_original(0x1464, 0x2632, 9, (u16)(col * 3), (u16)(row * 0x18), 0x0c, 0x18, G8(0x4a1e) == 0 ? 3 : 0, DATA_SEG, 0x260a, G16(0x4a22), G16(0x4a20));
    if (G8(0x4a1e) != 0) { icon_figure(0x19, 0, row, col); icon_figure(0x19, 1, row, (u8)(col + 3)); }
    icon_figure(0x0c, 0, row, col); icon_figure(0x0c, 1, row, (u8)(col + 3));
    call_original(0x1464, 0x1b52, 0);
}
// 34FB:382D  Editing the selected character's icon: Parts (Head and Weapon pictures, Previous/Next/Keep/Exit),
// colours 1 and 2 of each body part (Weapon, Body, Hair/Face, Shield, Arm, Leg), and Size (Large/Small), until Exit and
// a yes to "Is this icon ok?". Keep accepts a change, Exit undoes the unkept ones. The menu lines are DS:07BF + level
// * 29h; for colours the word at position 13 of level 3's line is set to Hair or Face. (The "menu level" byte is not
// set before the first non-special key in the original; 0 here.)
void icon_editor(void) {
    static const u8 none[] = "";
    #define ICS(o) FARPTR(RSEG(0x34fb), (o))
    ui_frame(ICS(0x37c8), 0, 0, 0x16, 0x26, 1, 1);
    call_original(0x4afb, 0x00e3, 0);
    u8 yn;
    do {
        chr_build_icon(0);
        farptr ch = G32(0x5d92);
        u8 state = 1, keep6[6], level = 0, part = 1, which = 0, sub = 0, key = 0;
        memcpy(keep6, &P8(ch, 0xc1), 6);
        u8 slot = P8(ch, 0xbf); P8(ch, 0xbf) = 0x0c; chr_build_icon(0); P8(ch, 0xbf) = slot;
        u8 head = P8(ch, 0xbd), weap = P8(ch, 0xbe), size = P8(ch, 0xc0);
        icon_copy(1, 0x0c, P8(ch, 0xbf));
        icon_show(2, 1);
        text_put_string(ICS(0x37c9), 0x0f, 6, 8);  text_put_string(ICS(0x37cd), 0x0f, 10, 3);      // "old", "ready   action"
        text_put_string(ICS(0x37dc), 0x0f, 0x0c, 8); text_put_string(ICS(0x37cd), 0x0f, 0x10, 3);  // "new"
        for (;;) {
            icon_show(4, 1);
            u8 m[300]; m[0] = 0;
            if (state == 4) { const u8 *x = &mem[LIN(ICS(P8(ch, 0xc0) == 2 ? 0x37e0 : 0x37e6))]; memcpy(m, x, x[0] + 1u); }   // "Small" / "Large" first
            { const u8 *x = &G8(state * 0x29 + 0x7bf); int c = x[0]; if (m[0] + c > 255) c = 255 - m[0]; memcpy(m + 1 + m[0], x + 1, c); m[0] = (u8)(m[0] + c); }
            u8 special = 0;
            key = menu_horizontal(&special, 0, 0, 0x0f, 10, 0x0d, m, none);
            if (!special) {
                if (state == 1) {
                    level = 1;
                    if (key == 'P') state = 2;
                    else if (key == '1' || key == '2') {
                        state = 3; which = (u8)(key == '2');
                        u8 *t = &G8(0x83a); const u8 *w = &mem[LIN(ICS(key == '1' ? 0x37ec : 0x37f1))];     // "Hair" / "Face"
                        if (t[0] >= 0x0d) { u8 cnt = 4; if (0x0d + cnt > t[0] + 1) cnt = (u8)(t[0] - 0x0d + 1); memmove(t + 0x0d, t + 0x0d + cnt, t[0] - 0x0d - cnt + 1); t[0] = (u8)(t[0] - cnt); }
                        u8 at = t[0] + 1 < 0x0d ? (u8)(t[0] + 1) : 0x0d; int c = w[0]; if (t[0] + c > 0x28) c = 0x28 - t[0];
                        if (c > 0) { memmove(t + at + c, t + at, t[0] - at + 1); memcpy(t + at, w + 1, c); t[0] = (u8)(t[0] + c); }
                    }
                    else if (key == 'S') state = 4;
                } else if (state == 2) {
                    level = 2;
                    if (key == 0 || key == 'E') state = 1; else { sub = key; state = 5; }
                } else if (state == 3) {
                    level = 3;
                    part = key == 'W' ? 6 : key == 'B' ? 1 : (key == 'H' || key == 'F') ? 4 : key == 'S' ? 5 : key == 'A' ? 2 : key == 'L' ? 3 : 1;
                    if (key == 0 || key == 'E') state = 1; else state = 5;
                } else if (state == 4) {
                    if (key == 'L') { P8(ch, 0xc0) = 2; chr_build_icon(0); }
                    else if (key == 'S') { P8(ch, 0xc0) = 1; chr_build_icon(0); }
                    else if (key == 'K') { size = P8(ch, 0xc0); state = 1; key = ' '; }
                    else if (key == 'E' || key == 0) { P8(ch, 0xc0) = size; state = 1; key = ' '; }
                    chr_build_icon(0);
                } else if (state == 5) {
                    if (level == 2) {
                        u16 off = 0; u8 max = 0, *saved = 0;
                        if (sub == 'H') { off = 0xbd; max = 0x0d; saved = &head; } else if (sub == 'W') { off = 0xbe; max = 0x1f; saved = &weap; }
                        if (off) {
                            if (key == 'P') P8(ch, off) = P8(ch, off) == 0 ? max : (u8)(P8(ch, off) - 1);
                            else if (key == 'N') P8(ch, off) = P8(ch, off) < max ? (u8)(P8(ch, off) + 1) : 0;
                            else if (key == 'K') { *saved = P8(G32(0x5d92), off); state = level; key = ' '; }
                            else if (key == 'E' || key == 0) { P8(ch, off) = *saved; state = level; key = ' '; }
                            chr_build_icon(0);
                        }
                    } else if (level == 3) {
                        u8 v = P8(ch, 0xc0 + part), lo = v & 0x0f, hi = v >> 4;
                        if (key == 'N') { if (!which) lo = (lo + 1) % 16; else hi = (hi + 1) % 16; P8(ch, 0xc0 + part) = (u8)(hi * 16 + lo); }
                        else if (key == 'P') { if (!which) lo = (lo - 1) & 0x0f; else hi = (hi - 1) & 0x0f; P8(ch, 0xc0 + part) = (u8)(hi * 16 + lo); }
                        else if (key == 'K') { memcpy(keep6, &P8(ch, 0xc1), 6); state = level; key = ' '; }
                        else if (key == 'E' || key == 0) { memcpy(&P8(ch, 0xc1), keep6, 6); state = level; key = ' '; }
                    }
                }
            }
            icon_copy(1, 0x0c, P8(ch, 0xbf));
            if (level == 1 && (key == 0 || key == 'E')) break;
        }
        P8(ch, 0xbd) = head; P8(ch, 0xbe) = weap; P8(ch, 0xc0) = size;
        memcpy(&P8(ch, 0xc1), keep6, 6);
        icon_copy(1, 0x0c, P8(ch, 0xbf));
        icon_copy(0, P8(ch, 0xbf), 0x0c);
        menu_clear_line();
        icon_free_slot(0x0c);
        if (G8(0x49fa) > 1) { sprite_load(ICS(0x3816), 0, 0x0b); sprite_load(ICS(0x3816), 1, 0x0c); }      // "ICON"
        yn = menu_yes_no(0x0f, 10, 0x0d, ICS(0x381b));                                                    // "Is this icon ok? "
    } while (yn != 'Y');
    call_original(0x4afb, 0x0105, 0);
    #undef ICS
}

// ---- 34FB:1F37  Party menu, Drop: "Drop <name> forever?" and "Are you sure?"; then "You dump <name> out back." (or
// "<name> bids you farewell." for a character under the party's control) and the character leaves the party.
// Afterwards, whether dropped or not, "<name> breathes a sigh of relief." - built from the record even when it has
// just been freed (FreeMem's free-list header lands on its first bytes), as in the original.
static void notice3(u16 a, farptr name, u16 b) {
    farptr t = scratch_push(0x100); P8(t, 0) = 0;
    if (a) ps_concat(t, FARPTR(RSEG(0x34fb), a));
    ps_concat(t, name);
    ps_concat(t, FARPTR(RSEG(0x34fb), b));
    ui_notice(&P8(t, 0));
    scratch_pop(0x100);
}
void party_drop(void) {
    farptr ch = G32(0x5d92);
    if (!ch) return;
    farptr t = scratch_push(0x100); P8(t, 0) = 0;
    ps_concat(t, FARPTR(RSEG(0x34fb), 0x1ed2)); ps_concat(t, ch); ps_concat(t, FARPTR(RSEG(0x34fb), 0x1ed8));
    u8 k = menu_yes_no(0x0f, 10, 0x0e, t);                                                  // "Drop <name> forever? "
    scratch_pop(0x100);
    if (k == 'Y' && menu_yes_no(0x0f, 10, 0x0e, FARPTR(RSEG(0x34fb), 0x1ee3)) == 'Y') {     // "Are you sure? "
        if (P8(ch, 0x10d) == 0) notice3(0x1ef2, ch, 0x1efc); else notice3(0, ch, 0x1f07);
        party_remove(0, 1);
    }
    notice3(0, ch, 0x1f1b);
}

// ---- 34FB:3FB3 / 408C  The least and the most hit points a new character may have: per class (levels at 96h-9Dh)
// one per level (classes 4 and 7 one more), or the full die (8, 8, 10, 10, (level+1)*8, 4, 6, (level+1)*4), plus the
// Constitution bonus, divided by the number of classes; a negative bonus that would take it to zero leaves 1. ----
static u8 hp_bound(farptr ch, int most) {
    static const u8 die[8] = { 8, 8, 10, 10, 8, 4, 6, 4 };
    u8 n = 0, sum = 0;
    for (int k = 0; k <= 7; k++) {
        s8 lv = (s8)P8(ch, k + 0x96);
        if (lv <= 0) continue;
        n++;
        if (!most) { sum = (u8)(sum + lv); if (k == 4 || k == 7) sum++; }
        else sum = (u8)(sum + (k == 4 || k == 7 ? lv + 1 : lv) * die[k]);
    }
    s8 adj = chr_con_bonus(most ? G32(0x5d92) : ch);
    if (adj < 0 && !(-adj + n < sum)) return 1;
    return (u8)((sum + adj) / n);
}
u8 hp_least(farptr ch) { return hp_bound(ch, 0); }
u8 hp_most(farptr ch) { return hp_bound(ch, 1); }
// ---- 34FB:209F  Party menu, Modify: only a character without experience. Up/Down pick an ability or hit points,
// Left/Right lower and raise it within the race's limits (exceptional strength for fighters at 18); Exit undoes all,
// Keep ends. A quirk: lowering an ability below either the race's or the class's minimum puts it at the race's
// minimum, even when the class's is higher. Exit restores and leaves; Keep leaves and marks the character (85h).
// The nested 34FB:2033 (highlight the current field) is folded in. ----
static void mod_mark(u8 field, u8 on) {
    if (field < 6) sheet_draw_ability(on, (s8)field);
    else call_original(0x10ba, 0x0973, 5, FP_SEG(G32(0x5d92)), FP_OFF(G32(0x5d92)), 4, 0x12, on);
}
void chr_modify(void) {
    farptr ch = G32(0x5d92);
    if ((s32)P32(ch, 0xac) > 0) {
        farptr t = scratch_push(0x100); P8(t, 0) = 0; ps_concat(t, ch); ps_concat(t, FARPTR(RSEG(0x34fb), 0x206e));
        text_flash_message(0x0e, t); scratch_pop(0x100);                                     // "<name> can't be modified."
        return;
    }
    sheet_draw();
    u8 saved[6]; memcpy(saved, &P8(ch, 0x10), 6);
    u8 exs = P8(ch, 0x16), hp = P8(ch, 0x32), field = 6, key, special;
    mod_mark(field, 0); field = 0; mod_mark(field, 1);
    s8 race = (s8)P8(ch, 0x2e), cls = (s8)P8(ch, 0x2f), sex = (s8)P8(ch, 0x9e);
    #define RLIM(o) G8((u16)(race * 0x10 + (o)))
    do {
        special = 0;
        key = menu_horizontal(&special, 0, 1, 0x0f, 10, 0x0d, &mem[LIN(FARPTR(RSEG(0x34fb), 0x2095))], &mem[LIN(FARPTR(RSEG(0x34fb), 0x2082))]);
        mod_mark(field, 0);
        if (!special) {
            if (key == 'E' || key == 0) {
                memcpy(&P8(ch, 0x10), saved, 6);
                P8(ch, 0x16) = exs; P8(ch, 0x32) = hp; P8(ch, 0x11b) = P8(ch, 0x32);
                chr_recalculate(G32(0x5d92));
                return;                                                          // Exit leaves at once, unmarked
            }
        } else if (key == 'P' || key == 0x0d) { if (++field > 6) field = 0; }
        else if (key == 'H') { if (--field == 0xff) field = 6; }
        else if (key == 'K' || key == 'M') {
            int up = key == 'M';
            if (field < 6) {
                u8 *v = &P8(ch, 0x10 + field);
                *v = (u8)(*v + (up ? 1 : -1));
                u16 lo = field == 0 ? (u16)(race * 0x10 + sex + 0x3da3) : (u16)(race * 0x10 + 0x3da7 + field * 2);
                u16 hi = field == 0 ? (u16)(race * 0x10 + sex + 0x3da5) : (u16)(race * 0x10 + 0x3da8 + field * 2);
                if (!up) {
                    if (field == 0 && P8(ch, 0x16) != 0) { P8(ch, 0x16)--; (*v)++; }
                    else if (*v < G8(lo) || *v < G8((u16)(cls * 6 + 0x3f81 + field))) *v = G8(lo);
                } else if (G8(hi) < *v) {
                    if (field == 0 && *v > 0x12 && (s8)P8(ch, 0x98) > 0 && P8(ch, 0x16) < G8((u16)(race * 0x10 + sex + 0x3da7))) P8(ch, 0x16)++;
                    *v = G8(hi);
                }
                if (field == 2) { if (P8(ch, 0xb2)) P8(ch, 0xb2) = 1; chr_wisdom_spells(G32(0x5d92)); }
                else if (field == 4) {
                    if (!up) { u8 m = hp_most(G32(0x5d92)); if (m < P8(ch, 0x32)) P8(ch, 0x32) = hp_most(G32(0x5d92)); }
                    else { u8 m = hp_least(G32(0x5d92)); if (P8(ch, 0x32) < m) P8(ch, 0x32) = hp_least(G32(0x5d92)); }
                    P8(ch, 0x11b) = P8(ch, 0x32);
                    mod_mark(6, 0);
                }
            } else if (!up) {
                P8(ch, 0x32)--;
                if (P8(ch, 0x32) < hp_least(G32(0x5d92))) P8(ch, 0x32) = hp_least(G32(0x5d92));
                P8(ch, 0x11b) = P8(ch, 0x32);
            } else {
                P8(ch, 0x32)++;
                if (hp_most(G32(0x5d92)) < P8(ch, 0x32)) P8(ch, 0x32) = hp_most(G32(0x5d92));
                P8(ch, 0x11b) = P8(ch, 0x32);
            }
        }
        chr_recalculate(G32(0x5d92));
        sheet_draw_combat_stats();
        mod_mark(field, 1);
    } while (special || key != 'K');
    #undef RLIM
    P8(ch, 0x85) = 1;
}

// ---- 34FB:29AE  Party menu, Train: for a conscious character with 1000 gp (unless the STING switch, DS:470C, is on),
// each class not at its racial limit whose next level's experience (table DS:409B, 38h per class, 4 per level) is
// reached may train, if the training hall (DS:49D6->550h) teaches it. Experience beyond one level is cut to one
// short of the level after. On "Do you wish to train?" Yes: pay, raise the levels (undoing one point of a level
// drain each, 74h/75h), recompute, let a magic-user who gained a level pick a new spell, and roll the hit points.
// A bug kept: when the magic-user has no spell to choose, the "spell" index is 0 and byte 32h+0 - the hit points -
// is set to 1 before the new hit points are added (fixed when FIX is on).
static int race_capped(farptr ch, int k, u8 lv) {
    u8 race = P8(ch, 0x2e), str = P8(ch, 0x10);
    if (race == 1) return k == 2 && ((lv == 8 && str == 0x11) || (lv == 7 && str < 0x11));
    if (race == 2) return k == 2 && (lv == 7 || (lv == 6 && str == 0x11) || (lv == 5 && str < 0x11));
    if (race == 3) return k == 2 && (lv == 6 || (lv == 5 && str < 0x12));
    if (race == 4) return (k == 0 && lv == 5) || (k == 2 && (lv == 8 || (lv == 7 && str == 0x11) || (lv == 6 && str < 0x11)));
    if (race == 5) return k == 2 && (lv == 6 || (lv == 5 && str == 0x11) || (lv == 4 && str < 0x11));
    return 0;
}
static s32 xp_needed(int k, int level) { u16 o = (u16)(k * 0x38 + (s8)level * 4); return (s32)((u32)G16(o + 0x409d) << 16 | G16(o + 0x409b)); }
void chr_train(void) {
    farptr ch = G32(0x5d92);
    #define TCS(o) FARPTR(RSEG(0x34fb), (o))
    if (P8(ch, 0x10c) != 0 && !G8(0x470c)) { text_flash_message(0x0e, TCS(0x28ee)); return; }       // "we only train conscious people"
    s32 money = (s32)call_original(0x3c10, 0x28be, 2, FP_SEG(ch), FP_OFF(ch));
    if (money < 1000 && !G8(0x470c)) { text_flash_message(0x0e, TCS(0x290d)); return; }               // "Training costs 1000 gp."
    u8 can = 0, has = 0; s32 best = 0;
    s32 xp = (s32)P32(ch, 0xac);
    for (int k = 0; k <= 7; k++) {
        if ((s8)P8(ch, k + 0x96) <= 0) continue;
        has = (u8)(has + G8(k + 0x5de));
        u8 lv = P8(ch, k + 0x96);
        if (race_capped(ch, k, lv)) continue;
        s32 need = xp_needed(k, (s8)(lv + 1));
        if (!(need <= xp || G8(0x470c))) continue;
        if (need <= 0) continue;
        can = (u8)(can + G8(k + 0x5de));
        s32 next = xp_needed(k, (s8)(lv + 2));
        if (next > 0 && next <= xp && best < next) best = next - 1;
    }
    if (best > 0) { wr16(LIN(ch) + 0xac, (u16)best); wr16(LIN(ch) + 0xae, (u16)((u32)best >> 16)); }
    u8 hall = (u8)P16(G32(0x49d6), 0x550), mask;
    if (!G8(0x470c)) {
        if (!(has & hall)) { text_flash_message(0x0e, TCS(0x2925)); return; }                         // "We don't train that class here"
        if (!(can & hall)) { text_flash_message(0x0e, TCS(0x2944)); return; }                         // "Not Enough Experience"
        mask = can & hall;
    } else mask = can;
    text_clear_box(1, 1, 0x26, 0x16);
    u8 row = 4;
    hud_draw_name(0, 4, 4, ch);
    text_put_string(TCS(0x295a), 10, 4, (u8)(P8(ch, 0) + 4));                                          // " will become:"
    farptr sc = scratch_push(0x200), line = sc, num = sc + 0x100;
    for (int k = 0; k <= 7; k++) {
        if ((s8)P8(ch, k + 0x96) <= 0 || !(G8(k + 0x5de) & mask)) continue;
        row++;
        P8(line, 0) = 0; ps_concat(line, TCS(row == 5 ? 0x2968 : 0x2977));                              // "    a level " / "and a level "
        chr_number_text(num, (s8)P8(ch, k + 0x96) + 1);
        ps_concat(line, num); ps_concat(line, TCS(0x2975)); ps_concat(line, FARPTR(DATA_SEG, k * 0x1b + 0x0d38));
        text_put_string(line, 10, row, 6);
    }
    scratch_pop(0x200);
    if (menu_yes_no(0x0f, 10, 0x0d, TCS(0x2984)) != 'Y') return;                                         // "Do you wish to train? "
    ui_notice(&mem[LIN(TCS(0x299b))]);                                                                 // "Congratulations..."
    if (!G8(0x470c)) pay_coins(ch, 1000);
    u8 nclass = 0; s8 old_mu = (s8)P8(ch, 0x9b);
    for (int k = 0; k <= 7; k++) {
        if ((s8)P8(ch, k + 0x96) <= 0) continue;
        nclass++;
        if (!(G8(k + 0x5de) & mask)) continue;
        P8(ch, k + 0x96)++;
        if (P8(ch, 0x74)) { P8(ch, 0x75) = (u8)(P8(ch, 0x75) - P8(ch, 0x75) / P8(ch, 0x74)); P8(ch, 0x74)--; }
    }
    chr_recalc_levels(G32(0x5d92));
    if (old_mu < (s8)P8(ch, 0x9b)) {
        farptr v = scratch_push(4); u8 pick;
        wr16(LIN(v), 0xffff);
        do { pick = sheet_spell_list(4, 4, v, v + 2); } while (!pick && P8(v, 2));
        scratch_pop(4);
        if (pick || !FIX) P8(ch, pick + 0x32) = 1;          // (FIX: no spell picked, nothing marked; the original sets the hit points to 1)
    }
    u16 roll = chr_roll_hp(G32(0x5d92), mask);
    u16 g = (u16)(roll / nclass); if (!g) g = 1;
    P8(ch, 0xb1) = (u8)(P8(ch, 0xb1) + g);
    int g2 = (int)(roll + chr_con_bonus(G32(0x5d92))) / (int)nclass; if (g2 < 1) g2 = 1;
    u8 old11b = P8(ch, 0x11b), old32 = P8(ch, 0x32);
    P8(ch, 0x32) = (u8)(P8(ch, 0x32) + g2);
    P8(ch, 0x11b) = (u8)(P8(ch, 0x32) - (u8)(old32 - old11b));
    #undef TCS
}

// ---- The temple (unit 2090): Heal offers priestly services for gold ----
#define TCS2(o) FARPTR(RSEG(0x2090), (o))
// 2090:0013  "<name> <not needed text>" and "cast cure anyway:" yes/no.
u8 temple_anyway(farptr text) {
    farptr t = scratch_push(0x100); P8(t, 0) = 0; ps_concat(t, text);
    ui_char_message(0, 0, &P8(t, 0), G32(0x5d92));
    scratch_pop(0x100);
    u8 k = menu_yes_no(0x0f, 10, 0x0d, TCS2(0x0000));                                                // "cast cure anyway: "
    ui_clear_message();
    return k;
}
// 2090:00BF  "<service> will only cost N gold pieces." and "pay for cure" yes/no: from the character's money (its
// total taken as a 16-bit number, so a fortune of 65536 gp or more can look too small - kept) or else the pool.
u8 temple_pay(u16 cost, farptr service) {
    farptr t = scratch_push(0x200), n = t + 0x100; P8(t, 0) = 0;
    ps_concat(t, service); ps_concat(t, TCS2(0x0076)); chr_number_text(n, cost); ps_concat(t, n); ps_concat(t, TCS2(0x0087));
    text_print_wrapped(t, 1, 10, 0x16, 0x26, 0x11, 1);
    scratch_pop(0x200);
    u8 k = menu_yes_no(10, 0x0f, 0x0d, TCS2(0x0095));                                                // "pay for cure " (colours the other way round here)
    if (k == 'Y') {
        if (FIX) {                                   // (FIX: the money counted in full, not as a 16-bit number)
            s32 m32 = money_of(G32(0x5d92));
            if ((s32)cost > m32) {
                s32 p = money_of_pool(FARPTR(DATA_SEG, 0x67f4));
                if (p < (s32)cost) { ui_notice(&mem[LIN(TCS2(0x00a3))]); k = 'N'; }
                else pay_from_pool32((u32)(p - cost));
            } else pay_from32((u32)(m32 - cost));
        } else {
        u16 m = (u16)money_of(G32(0x5d92));
        if (cost > m) {
            s32 p = money_of_pool(FARPTR(DATA_SEG, 0x67f4));
            if (p < (s32)cost) { ui_notice(&mem[LIN(TCS2(0x00a3))]); k = 'N'; }                       // "Not enough money."
            else pay_from_pool((u16)(p - cost));
        } else pay_from((u16)(m - cost));
        }
    }
    if (k == 'Y') { ui_clear_message(); ui_char_message(1, 0, &mem[LIN(TCS2(0x00b5))], G32(0x5d92)); }  // "is cured."
    return k;
}
static void fx_remove(u8 kind) { call_original(0x4592, 0x0026, 5, FP_SEG(G32(0x5d92)), FP_OFF(G32(0x5d92)), kind, 0, 0); }
void temple_cure_blindness(void) {                                                                   // 2090:0249
    u8 k = 'Y';
    if (!char_find_effect(0x21, G32(0x5d92))) k = temple_anyway(TCS2(0x022c));                       // "is not blind."
    if (k == 'Y' && temple_pay(1000, TCS2(0x023a)) == 'Y') fx_remove(0x21);
}
void temple_cure_disease(void) {                                                                     // 2090:02ED
    u8 has = 0, k = 'Y';
    for (int i = 1; i <= 6; i++) if (char_find_effect(G8(i + 0x113), G32(0x5d92))) has = 1;
    if (!has) k = temple_anyway(TCS2(0x02cf));                                                         // "is not Diseased."
    if (k == 'Y' && temple_pay(1000, TCS2(0x02e0)) == 'Y') {
        G8(0x681f) = 1; for (int i = 1; i <= 6; i++) fx_remove(G8(i + 0x113)); G8(0x681f) = 0;
    }
}
void temple_cure_wounds(u8 how) {                                                                    // 2090:0401: light, serious, critical
    static const u16 name[4] = { 0, 0x03c6, 0x03d8, 0x03ec }, cost[4] = { 0, 100, 0x15e, 600 };
    static const u8 dice[4] = { 0, 1, 2, 3 }, plus[4] = { 0, 0, 1, 3 };
    if (how < 1 || how > 3) return;
    if (temple_pay(cost[how], TCS2(name[how])) == 'Y') {
        u8 r = (u8)(roll_dice(8, dice[how]) + plus[how]);
        rules_heal(0, r, G32(0x5d92));
    }
}
void temple_raise_dead(void) {                                                                       // 2090:0510
    farptr ch = G32(0x5d92);
    u8 dead = P8(ch, 0x10c) == 6 || P8(ch, 0x10c) == 1, k = 'Y';
    if (!dead) k = temple_anyway(TCS2(0x04f8));                                                        // "is not dead."
    if (k != 'Y') return;
    k = temple_pay(0x157c, TCS2(0x0505));                                                              // "Raise Dead", 5500
    if (k != 'Y' || !dead) return;
    G8(0x681f) = 1; call_original(0x4592, 0x0026, 5, FP_SEG(ch), FP_OFF(ch), 0x20, 0, 0); call_original(0x4592, 0x0026, 5, FP_SEG(ch), FP_OFF(ch), 0x37, 0, 0); G8(0x681f) = 0;
    P8(ch, 0x11b) = 1; P8(ch, 0x10c) = 0; P8(ch, 0x10d) = 1;
    if (P8(ch, 0x14)) P8(ch, 0x14)--;
    u8 lose = P8(ch, 0xb1) < P8(ch, 0x32) ? (u8)(P8(ch, 0x32) - P8(ch, 0xb1)) : 0, div = 0;
    if (P8(ch, 0x14) > 0x0d) {
        for (int i = 0; i <= 7; i++) {
            s8 lv = (s8)P8(ch, i + 0x96);
            if (lv <= 0) continue;
            if (i == 2) div = (u8)(div + (P8(ch, 0x14) - 0x0e) * (s8)P8(ch, 0x98));
            else if (P8(ch, 0x14) < 0x10) div = (u8)(div + lv);
            else div = (u8)(div + lv * 2);
        }
        if (div) lose = (u8)(lose / div);
        if (P8(ch, 0x14) < 0x11 || (s8)P8(ch, 0x98) > 0) P8(ch, 0x32) = (u8)(P8(ch, 0x32) - lose);
    }
}
void temple_neutralize_poison(void) {                                                                // 2090:0751
    u8 k = 'Y';
    if (!char_find_effect(0x37, G32(0x5d92))) k = temple_anyway(TCS2(0x072e));                        // "is not poisoned."
    if (k == 'Y' && temple_pay(1000, TCS2(0x073f)) == 'Y') { G8(0x681f) = 1; fx_remove(0x37); fx_remove(0x16); fx_remove(0x0f); G8(0x681f) = 0; }
}
void temple_remove_curse(void) {                                                                     // 2090:083A
    u8 cursed = 0, k = 'Y';
    for (farptr p = P32(G32(0x5d92), 0xc8); p && !cursed; p = P32(p, 0x2a)) if (P8(p, 0x36)) cursed = 1;
    if (!cursed && !char_find_effect(0x24, G32(0x5d92))) k = temple_anyway(TCS2(0x081e));            // "is not cursed."
    if (k == 'Y' && temple_pay(0x0dac, TCS2(0x082d)) == 'Y') { SETG32(0x6c95, G32(0x5d92)); call_original(0x41cf, 0x2540, 0); }
}
void temple_stone_to_flesh(void) {                                                                   // 2090:0928
    u8 k = 'Y';
    if (P8(G32(0x5d92), 0x10c) != 7) k = temple_anyway(TCS2(0x090a));                                  // "is not stoned."
    if (k == 'Y' && temple_pay(2000, TCS2(0x0919)) == 'Y' && P8(G32(0x5d92), 0x10c) == 7) {
        farptr ch = G32(0x5d92); P8(ch, 0x10c) = 0; P8(ch, 0x10d) = 1; P8(ch, 0x11b) = 1;
    }
}
// 2090:09D7  Heal: "<name>, how can we help you?" and a list of the ten services (names at DS:011A + n*29h). The
// list's starting index ([BP-0Eh]) is never set in the original. The leftover there comes from the calls before it,
// several of which are native now and leave nothing on the emulated stack, so it cannot be imitated; 0 (the list
// shown from the top, as the original shows it in the tests) is used.
void temple_heal(s16 stale_index) {
    farptr lv = scratch_push(4); SETP32(lv, 0, 0);
    menu_list_alloc(lv, 10);
    farptr head = P32(lv, 0), cur = head;
    int n = 1;
    for (farptr p = head; p; p = P32(p, 0x2a), n++) { const u8 *src = &G8(n * 0x29 + 0xf1); u8 l = src[0] > 0x28 ? 0x28 : src[0]; P8(p, 0) = l; memcpy(&P8(p, 1), src + 1, l); P8(p, 0x29) = 0; }
    text_clear_box(0, 0x18, 0x27, 0x18);
    u8 redraw = 1, quit = 0; s16 index = stale_index;
    ui_frame(TCS2(0x09b5), 0x0f, 0, 0x16, 0x26, 1, 1);
    ui_frame(TCS2(0x09b5), 0x0f, 0, 1, 0x26, 1, 1);
    do {
        farptr t = scratch_push(0x100); P8(t, 0) = 0; ps_concat(t, G32(0x5d92)); ps_concat(t, TCS2(0x09b6)); if (P8(t, 0) > 0x28) P8(t, 0) = 0x28;
        text_put_string(t, 0x0f, 1, 1);
        scratch_pop(0x100);
        ui_frame(TCS2(0x09b5), 0x0f, 0, 0x16, 0x26, 0x11, 1);
        u8 key = menu_list(&cur, &index, &redraw, 0, head, 0x0f, 0x26, 4, 2, 0x0f, 10, 0x0d, &mem[LIN(TCS2(0x09cd))], &mem[LIN(TCS2(0x09b5))]);
        if (key == 'H' || key == 0x0d) {
            switch (index) {
            case 0: temple_cure_blindness(); break;
            case 1: temple_cure_disease(); break;
            case 2: temple_cure_wounds(1); break;
            case 3: temple_cure_wounds(2); break;
            case 4: temple_cure_wounds(3); break;
            case 5: temple_neutralize_poison(); break;
            case 6: temple_raise_dead(); break;
            case 7: temple_remove_curse(); break;
            case 8: temple_stone_to_flesh(); break;
            case 9: quit = 1; break;
            }
        } else if (key == 0) quit = 1;
    } while (!quit);
    SETP32(lv, 0, head); menu_list_free(lv); scratch_pop(4);
    text_clear_box(0, 0, 0x27, 0x18);
    hud_draw_screen();
    hud_draw_roster(G32(0x5d92));
}
#undef TCS2

// ---- 2090:0D09  The temple's treasure screen: Heal, View, Take, Pool, Share, Appraise, Exit (Take and Share only while
// there are coins). Leaving with coins still there brings the priest's "you have left some money here" and a choice
// to go back. ----
void temple_screen(void) {
    #define HCS(o) FARPTR(RSEG(0x2090), (o))
    G8(0x49f3) = 1;
    hud_draw_screen(); hud_draw_roster(G32(0x5d92));
    memset(&G8(0x67f4), 0, 0x1c); G8(0x6dc0) = 0;
    u8 leave = 0, items, coins, special = 0, apr = 0;
    farptr ap = scratch_push(2);
    do {
        treasure_left(&items, &coins);
        const u8 *m = &mem[LIN(HCS(coins ? 0x0c3f : 0x0c67))];
        u8 key = menu_horizontal(&special, 0, 1, 0x0f, 10, 0x0d, m, &mem[LIN(HCS(0x0c84))]);
        if (key == 'H') { if (!special) temple_heal(0); }
        else if (key == 'V') { farptr v = scratch_push(2); P8(v, 0) = special; sheet_view(v); special = P8(v, 0); scratch_pop(2); }
        else if (key == 'T') take_money();
        else if (key == 'P') { if (!special) treasure_pool(); }
        else if (key == 'S') treasure_share();
        else if (key == 'A') { appraise(ap); apr = P8(ap, 0); }
        else if (key == 'E') {
            treasure_left(&items, &coins);
            if (!coins) leave = 1;
            else {
                text_print_wrapped(HCS(0x0c8e), 1, 10, 0x16, 0x26, 0x11, 1);
                text_print_wrapped(HCS(0x0cd9), 1, 10, 0x16, 0x26, 0x11, 1);
                u8 r = ecl_menu(0, 0, 0x0f, 10, 0x0d, &mem[LIN(HCS(0x0c85))], &mem[LIN(HCS(0x0c84))]);   // "~Yes ~No"
                if (r == 1) leave = 1; else text_clear_box(1, 0x11, 0x26, 0x16);
            }
        }
        else if (key == 'G' || key == 'O') party_select_move(key);
        if (key == 'B' || key == 'T') hud_draw_screen();
        else if (key == 'A' && apr) hud_draw_screen();
        hud_draw_roster(G32(0x5d92));
    } while (!leave);
    scratch_pop(2);
    #undef HCS
}

// ---- The shop (unit 22EC) ----
#define SCS2(o) FARPTR(RSEG(0x22ec), (o))
// 22EC:0030  The Buy list: each item's name is turned into a 30-column line with its price (3Ah, at least 1) at the
// right, the list is shown ("Items: Buy"), and afterwards the names are worked out again.
void shop_list(farptr index_var, farptr cur_var, farptr result, farptr key_out) {
    for (farptr p = G32(0x6810); p; p = P32(p, 0x2a)) {
        u8 line[0x20]; memcpy(line, &mem[LIN(SCS2(0x0000))], 0x1f);                                    // 30 spaces
        if (P16(p, 0x3a) == 0) wr16(LIN(p) + 0x3a, 1);
        char num[17]; int n = snprintf(num, sizeof num, "%u", P16(p, 0x3a)); if (n > 0x10) n = 0x10;
        for (int i = 1; i <= n; i++) line[0x1e + i - n] = (u8)num[i - 1];
        u8 l = P8(p, 0); for (int i = 1; i <= l; i++) line[i] = P8(p, i);
        u8 m = line[0] > 0x28 ? 0x28 : line[0]; P8(p, 0) = m; memcpy(&P8(p, 1), line + 1, m);
        P8(p, 0) = 0x1e;
    }
    ui_frame(SCS2(0x001f), 0x0f, 0, 0x16, 0x26, 1, 1);                                                  // "Shop"
    farptr cur = P32(cur_var, 0); s16 idx = (s16)P16(index_var, 0); u8 redraw = 1;
    u8 key = menu_list(&cur, &idx, &redraw, 1, G32(0x6810), 0x16, 0x26, 4, 1, 0x0f, 10, 0x0d, &mem[LIN(SCS2(0x002c))], &mem[LIN(SCS2(0x0024))]);
    SETP32(cur_var, 0, cur); wr16(LIN(index_var), (u16)idx);
    P8(key_out, 0) = key;
    SETP32(result, 0, P32(cur_var, 0));
    for (farptr p = G32(0x6810); p; p = P32(p, 0x2a)) chr_item_name(0, 0, 0, 0, p);
}
// 22EC:0239  Handing a copy of a shop item to the selected character, unless it overloads them (*failed = 1).
void shop_give(farptr failed, farptr item) {
    if (item_too_much(item, G32(0x5d92))) { ui_notice(&mem[LIN(SCS2(0x022e))]); P8(failed, 0) = 1; return; }   // "OverLoaded"
    P8(failed, 0) = 0;
    farptr n = rtl_getmem(0x3f); memmove(&mem[LIN(n)], &mem[LIN(item)], 0x3f); SETP32(n, 0x2a, 0);
    farptr p = P32(G32(0x5d92), 0xc8);
    if (!p) SETP32(G32(0x5d92), 0xc8, n);
    else { while (P32(p, 0x2a)) p = P32(p, 0x2a); SETP32(p, 0x2a, n); }
    chr_recalculate(G32(0x5d92));
}
// 22EC:0361  Buy: pick from the list until it is left; pay from the character's money (as a 16-bit number) or else
// the pool.
void shop_buy(void) {
    farptr v = scratch_push(0x10), idx = v, cur = v + 2, res = v + 6, key = v + 10, failed = v + 11;
    wr16(LIN(idx), 1); SETP32(cur, 0, G32(0x6810)); SETP32(res, 0, 0);
    for (;;) {
        shop_list(idx, cur, res, key);
        if (P8(key, 0) != 'B' && P8(key, 0) != 0x0d) break;
        farptr item = P32(res, 0);
        u16 price = P16(item, 0x3a), m = (u16)money_of(G32(0x5d92));
        if (FIX) {                                   // (FIX: the money counted in full, not as a 16-bit number)
            s32 m32 = money_of(G32(0x5d92));
            if (m32 < (s32)price) {
                s32 p = money_of_pool(FARPTR(DATA_SEG, 0x67f4));
                if (p < (s32)price) ui_notice(&mem[LIN(FARPTR(RSEG(0x22ec), 0x034f))]);
                else { shop_give(failed, item); if (!P8(failed, 0)) pay_from_pool32((u32)(p - price)); }
            } else { shop_give(failed, item); if (!P8(failed, 0)) pay_from32((u32)(m32 - price)); }
        } else
        if (m < price) {
            s32 p = money_of_pool(FARPTR(DATA_SEG, 0x67f4));
            if (p < 0x10000 && (p < 0 || (u16)p < price)) ui_notice(&mem[LIN(FARPTR(RSEG(0x22ec), 0x034f))]);   // "Not enough Money."
            else { shop_give(failed, item); if (!P8(failed, 0)) pay_from_pool((u16)(p - price)); }
        } else { shop_give(failed, item); if (!P8(failed, 0)) pay_from((u16)(m - price)); }
    }
    scratch_pop(0x10);
}
// 22EC:053E  The shop screen: Buy, View, Take, Pool, Share, Appraise, Exit, like the temple's.
void shop_screen(void) {
    G8(0x49f3) = 1;
    hud_draw_screen(); hud_draw_roster(G32(0x5d92));
    memset(&G8(0x67f4), 0, 0x1c); G8(0x6dc0) = 0;
    for (farptr p = G32(0x6810); p; p = P32(p, 0x2a)) chr_item_name(0, 0, 0, 0, p);
    u8 leave = 0, items, coins, special = 0, apr = 0;
    farptr ap = scratch_push(2);
    do {
        treasure_left(&items, &coins);
        static const u8 none[] = "";
        u8 key = menu_horizontal(&special, 0, 1, 0x0f, 10, 0x0d, &mem[LIN(SCS2(coins ? 0x0474 : 0x049b))], none);
        if (key == 'B') shop_buy();
        else if (key == 'V') { farptr vv = scratch_push(2); P8(vv, 0) = special; sheet_view(vv); special = P8(vv, 0); scratch_pop(2); }
        else if (key == 'T') take_money();
        else if (key == 'P') { if (!special) treasure_pool(); }
        else if (key == 'S') treasure_share();
        else if (key == 'A') { appraise(ap); apr = P8(ap, 0); }
        else if (key == 'E') {
            treasure_left(&items, &coins);
            if (!coins) leave = 1;
            else {
                text_print_wrapped(SCS2(0x04c0), 1, 10, 0x16, 0x26, 0x11, 1);
                text_print_wrapped(SCS2(0x0513), 0, 0x0f, 0x16, 0x26, 0x11, 1);
                u8 r = ecl_menu(0, 0, 0x0f, 10, 0x0d, &mem[LIN(SCS2(0x04b7))], none);
                if (r == 1) leave = 1; else text_clear_box(1, 0x11, 0x26, 0x16);
            }
        }
        else if (key == 'G' || key == 'O') party_select_move(key);
        if (key == 'B' || key == 'T') hud_draw_screen();
        else if (key == 'A' && apr) hud_draw_screen();
        hud_draw_roster(G32(0x5d92));
    } while (!leave);
    scratch_pop(2);
}
#undef SCS2
