#include "fixes.h"
// Encamp (original overlay unit at 32FD: the camp menu; its Magic, Rest and Alter screens are
// still original code).
#include <stdio.h>
#include "poolrad.h"
#include "chr.h"
#include "fight.h"
#include "menu.h"
#include "text.h"
#include "hud.h"
#include "rtl.h"
#include "rest.h"
#include "sheet.h"
#include "camp.h"

// 32FD:0000  How many minutes of rest this character needs to learn the spells picked for
// memorising (a spell slot with its top bit set is "picked, not yet learned") and to copy the
// scroll spells picked for scribing. Fifteen minutes per spell level, on top of four hours of
// sleep, or six if any of the spells is third level or higher. The hours are noted in byte 2Ch.
// The two totals are bytes in the original and wrap at 256; that is kept.
u16 camp_rest_needed(farptr ch) {
    u8 top_memo = 0, top_scribe = 0, sum_memo = 0, sum_scribe = 0, hours = 0;
    for (int i = 0; i <= 0x14; i++) {
        u8 slot = P8(ch, 0x17 + i);
        if (slot > 0x7f) { u8 level = G8(0x31fb + (slot & 0x7f) * 16); if (level > top_memo) top_memo = level; sum_memo = (u8)(sum_memo + level); }
    }
    for (farptr item = P32(ch, 0xc8); item; item = P32(item, 0x2a)) {
        if (!item_is_scroll(item)) continue;   // a scroll this character can read
        for (int i = 1; i <= 3; i++) {
            u8 slot = P8(item, 0x3b + i);
            if (slot > 0x7f) { u8 level = G8(0x31fb + (slot & 0x7f) * 16); if (level > top_scribe) top_scribe = level; sum_scribe = (u8)(sum_scribe + level); }
        }
    }
    if (sum_memo || sum_scribe) hours = 4;
    if (top_memo > 2 || top_scribe > 2) hours = 6;
    P8(ch, 0x2c) = hours;
    return (u16)(hours * 60 + sum_memo * 15 + sum_scribe * 15);
}

// 32FD:077A  Camp menu, Rest: propose the longest time anyone in the party needs (as hours, tens
// of minutes and minutes), run the rest screen, and report whether the rest was interrupted.
void camp_rest(farptr interrupted) {
    u16 longest = 0;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) { u16 t = camp_rest_needed(ch); if (t > longest) longest = t; }
    SETG16(0x6dc8, longest / 60);
    SETG16(0x6dc6, (u16)(longest - G16(0x6dc8) * 60) / 10);
    SETG16(0x6dc4, longest % 10);
    P8(interrupted, 0) = rest_screen();
    memset(&G8(0x6dc2), 0, 14);
    hud_draw_status();
}

static void put(const char *t, u8 color, u8 row, u8 col) {
    u8 p[64]; size_t n = strlen(t);
    p[0] = (u8)n; memcpy(p + 1, t, n);
    text_put_pstr(p, color, row, col);
}
static void char_message(const char *text, u8 wait) {   // "<name> <text>" for the selected character
    u8 p[256] = {0}; size_t n = strlen(text);
    p[0] = (u8)n; memcpy(p + 1, text, n);
    ui_char_message(wait, 10, p, G32(0x5d92));
}

// 32FD:036C  May the selected character cast (1), memorise (2) or scribe (3) right now? If not,
// say why. Casting can also be forbidden by the area (word 1CAh of the game's own data block).
u8 camp_magic_allowed(u8 what) {
    char msg[48] = "";
    farptr sel = G32(0x5d92);
    if (what == 1 && P16(G32(0x49d2), 0x1ca) != 0) strcpy(msg, "cannot cast spells in this area");
    else if (P8(sel, 0x10c) == 1 || P8(sel, 0x10d) == 0) {
        strcpy(msg, "is in no condition to ");
        if (what == 1) strcat(msg, "cast any spells");
        else if (what == 2) strcat(msg, "memorize spells");
        else if (what == 3) strcat(msg, "scribe any scrolls");
    }
    if (!msg[0]) return 1;
    msg[40] = 0;
    char_message(msg, 1);
    return 0;
}

// 32FD:025C  How many more spells of this class and level the selected character may pick:
// the allowance (byte B1h + class * 3 + level) less those already in the slots, picked or learned.
// Each spell has a 16-byte entry at DS:31FA: its class first, then its level.
s16 camp_slots_free(s8 spell_class, s8 level) {
    farptr sel = G32(0x5d92); u8 used = 0;
    for (int i = 0; i <= 0x14; i++) {
        u8 slot = P8(sel, 0x17 + i);
        if (slot && (s8)G8(0x31fb + (slot & 0x7f) * 16) == level && (s8)G8(0x31fa + (slot & 0x7f) * 16) == spell_class) used++;
    }
    return (s16)(P8(sel, 0xb1 + spell_class * 3 + level) - used);
}

// 32FD:0159 and 019C  Forget what was picked: empty the slots holding spells picked but not yet
// learned (and the sleep still owed), and un-pick the spells marked on scrolls.
void camp_unpick_memory(farptr ch) {
    for (int i = 0; i <= 0x14; i++) if (P8(ch, 0x17 + i) > 0x7f) P8(ch, 0x17 + i) = 0;
    P8(ch, 0x2c) = 0;
}
void camp_unpick_scrolls(farptr ch) {
    for (farptr item = P32(ch, 0xc8); item; item = P32(item, 0x2a))
        if (item_is_scroll(item))
            for (int k = 1; k <= 3; k++) P8(item, 0x3b + k) &= 0x7f;
}
// 32FD:0214  The same for the whole party.
void camp_unpick_party(void) {
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) { camp_unpick_memory(ch); camp_unpick_scrolls(ch); }
}

// 32FD:04A1  Camp, Magic, Cast: show the selected character's memorised spells and cast the one
// chosen, again and again until Exit. The list and the casting are still original code.
void camp_cast(void) {
    u8 redraw = 0, spell = camp_magic_allowed(1);
    farptr v = scratch_push(6);                                           // three VAR parameters: a word, and two bytes
    SETP16(v, 0, 0xffff);
    SETG16(0x6db5, 0); SETG16(0x6db7, 0);
    while (spell) {
        spell = (u8)call_original(0x3c10, 0x299c, 6, 0, 1, FP_SEG(v), FP_OFF(v), FP_SEG(v), (u16)(FP_OFF(v) + 2));
        if (spell == 0) {
            if (P8(v, 2) == 0) char_message("has no spells memorized", 1); else redraw = 1;
        } else {
            redraw = 1;
            ui_border(0, 0x16, 0x26, 0x11, 1);
            text_clear_box(1, 0x11, 0x26, 0x16);
            call_original(0x41cf, 0x0c20, 5, spell, 0, 1, FP_SEG(v), (u16)(FP_OFF(v) + 4));
            SETP16(v, 0, 0xffff);
        }
    }
    scratch_pop(6);
    if (redraw) hud_draw_screen();
}

// 32FD:0824  Put the selected character's spell slots in order of spell number (picked or not),
// which also moves the empty slots to the front.
void camp_sort_slots(void) {
    farptr sel = G32(0x5d92);
    for (int i = 0; i <= 0x13; i++)
        for (int j = i; j <= 0x14; j++)
            if ((P8(sel, 0x17 + j) & 0x7f) < (P8(sel, 0x17 + i) & 0x7f)) { u8 t = P8(sel, 0x17 + i); P8(sel, 0x17 + i) = P8(sel, 0x17 + j); P8(sel, 0x17 + j) = t; }
}

// 32FD:05A4  "<name> can memorize:" and, for each kind of spell the character has any allowance
// for, how many more of levels 1 to 3 may be picked. Returns 0 if they have no allowance at all.
u8 camp_show_slots(void) {
    farptr sel = G32(0x5d92);
    char count[2][4][4]; u8 have[2] = {0, 0}, any = 0;
    for (s8 c = 0; c <= 1; c++)
        for (s8 level = 1; level <= 3; level++) {
            snprintf(count[c][level], 4, "%u", (u8)camp_slots_free(c, level)); count[c][level][2] = 0;
            if (P8(sel, 0xb1 + c * 3 + level) == 0) strcpy(count[c][level], " "); else have[c] = any = 1;
        }
    if (!any) return 0;
    char_message("can memorize:", 0);
    u8 row = 3;
    for (s8 c = 0; c <= 1; c++) {
        if (!have[c]) continue;
        put(c == 0 ? "    Cleric Spells:" : "Magic-User Spells:", 10, (u8)(row + 0x11), 1);
        for (s8 level = 1; level <= 3; level++) put(count[c][level], 10, (u8)(row + 0x11), (u8)(0x14 + (level - 1) * 3));
        row++;
    }
    return 1;
}

static u8 spell_list(u8 kind, u8 mode, farptr v) {   // the spell list screen (3C10:299C, still original code); v: a word, then a flag byte
    return (u8)call_original(0x3c10, 0x299c, 6, kind, mode, FP_SEG(v), FP_OFF(v), FP_SEG(v), (u16)(FP_OFF(v) + 2));
}

// 32FD:090A  Camp, Magic, Memorize. If spells are already picked, show them and ask whether to
// keep them. Otherwise pick from the spell book for as long as there is room: a picked spell
// goes into the first empty slot with its top bit set, and the slots are re-sorted. On the way
// out the picks are shown and confirmed once more; No throws them all away.
void camp_memorize(void) {
    if (!camp_magic_allowed(2)) return;
    farptr sel = G32(0x5d92), v = scratch_push(4);
    u8 done = 0, redraw = 1, spell;
    SETP16(v, 0, 0xffff);
    spell_list(5, 0, v);
    if (P8(v, 2) == 0) redraw = 0;
    else if (menu_yes_no(15, 10, 14, FARPTR(RSEG(0x32fd), 0x08bf)) == 'N') camp_unpick_memory(sel);   // "Memorize These Spells? "
    else done = 1;
    SETP16(v, 0, 0xffff);
    while (!done) {
        if (!camp_show_slots()) { done = 1; char_message("cannot memorize any spells", 1); continue; }
        spell = spell_list(1, 2, v);
        redraw = 1;
        if (spell == 0) done = 1;
        else if ((u8)camp_slots_free((s8)G8(0x31fa + spell * 16), (s8)G8(0x31fb + spell * 16)) != 0) {
            u8 k = 0;
            while (P8(sel, 0x17 + k) != 0) k++;
            P8(sel, 0x17 + k) = (u8)(spell + 0x80);
            camp_sort_slots();
        }
    }
    if (P16(v, 0) != 0xffff) {
        SETP16(v, 0, 0xffff);
        spell_list(5, 0, v);
        if (P8(v, 2) != 0 && menu_yes_no(15, 10, 14, FARPTR(RSEG(0x32fd), 0x08f2)) == 'N') camp_unpick_memory(sel);   // "Memorize these spells? "
    }
    scratch_pop(4);
    if (redraw) hud_draw_screen();
}

static void notice(const char *text) {
    u8 p[64]; size_t n = strlen(text);
    p[0] = (u8)n; memcpy(p + 1, text, n);
    ui_notice(p);
}

// 32FD:0B5B  Camp, Magic, Scribe: mark spells on scrolls to be copied into the spell book
// during the next rest. It follows Memorize: confirm any marks already made, then choose from
// the spells on the character's scrolls. A spell is refused if it is already in the book, is
// already marked, or is of a class and level the character has no allowance for. The message for
// the second case is misspelt in the original ("scibing") and is kept that way (corrected when FIX is on).
void camp_scribe(void) {
    if (!camp_magic_allowed(3)) return;
    farptr sel = G32(0x5d92), v = scratch_push(4);
    u8 done = 0, redraw = 1, spell;
    SETP16(v, 0, 0xffff);
    spell_list(6, 0, v);
    if (P8(v, 2) == 0) redraw = 0;
    else if (menu_yes_no(15, 10, 14, FARPTR(RSEG(0x32fd), 0x0ab9)) == 'N') camp_unpick_scrolls(sel);   // "Scribe These Spells? "
    else done = 1;
    SETP16(v, 0, 0xffff);
    while (!done) {
        spell = spell_list(3, 3, v);
        if (spell == 0) {
            done = 1;
            if (P8(v, 2) == 0) char_message("has no copyable scrolls", 1); else redraw = 1;
            continue;
        }
        redraw = 1;
        if (P8(sel, 0x32 + spell) != 0) { notice("You already know that spell"); continue; }
        u8 found = 0;
        for (farptr item = P32(sel, 0xc8); item && !found; item = P32(item, 0x2a)) {
            if (!item_is_scroll(item)) continue;
            for (int k = 1; k <= 3; k++)
                if (P8(item, 0x3b + k) > 0x7f && (P8(item, 0x3b + k) & 0x7f) == spell) { notice(FIX ? "You are already scribing that spell" : "You are already scibing that spell"); found = 1; }
        }
        if (found) continue;
        if (P8(sel, 0xb1 + (s8)G8(0x31fa + spell * 16) * 3 + (s8)G8(0x31fb + spell * 16)) == 0) { notice("You can not scribe that spell."); continue; }
        for (farptr item = P32(sel, 0xc8); item && !found; item = P32(item, 0x2a)) {
            int k = 1;
            do { if (P8(item, 0x3b + k) == spell) { P8(item, 0x3b + k) |= 0x80; found = 1; } k++; } while (k < 4 && !found);
        }
    }
    if (P16(v, 0) != 0xffff) {
        SETP16(v, 0, 0xffff);
        spell_list(6, 0, v);
        if (P8(v, 2) != 0 && menu_yes_no(15, 10, 14, FARPTR(RSEG(0x32fd), 0x0b45)) == 'N') camp_unpick_scrolls(sel);   // "Scribe these spells? "
    }
    scratch_pop(4);
    if (redraw) hud_draw_screen();
}

// 32FD:0E37 (a procedure nested in the next one)  Add a line to the end of the list.
static farptr display_add(farptr tail, const char *text, u8 heading) {
    size_t n = strlen(text); if (n > 40) n = 40;
    farptr node = rtl_getmem(0x2e);
    SETP32(tail, 0x2a, node);
    SETP32(node, 0x2a, 0);
    P8(node, 0) = (u8)n; memcpy(&P8(node, 1), text, n);
    P8(node, 0x29) = heading;
    return node;
}

// The name shown for a status effect, or NULL for the ones that are not shown. Most effects are
// the lasting result of a spell and take the spell's name (the spell table has the effect each
// spell leaves at byte 0Ah of its entry); fourteen have names of their own; and an effect that
// should have a spell but has none is shown as "Funky--" and its number.
static const char *effect_name(u8 kind, char *buf) {
    static const u8 from_spell[] = {1, 2, 5, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 21, 22, 23, 24, 25, 0x1c, 0x1d, 0x20, 0x21, 0x22,
                                    0x24, 0x25, 0x26, 0x27, 0x29, 0x2a, 0x2d, 0x2e, 0x31, 0x33, 0x34, 0x35};
    for (size_t i = 0; i < sizeof from_spell; i++) {
        if (from_spell[i] != kind) continue;
        for (u8 spell = 1; spell < 0x39; spell++)
            if (G8(0x3204 + spell * 16) == kind) return pstr(FARPTR(DATA_SEG, 0x28e9 + spell * 0x29));
        snprintf(buf, 16, "Funky--%u", kind);
        return buf;
    }
    switch (kind) {
    case 0x04: return "Studying Manual of Bodily Health";
    case 0x07: return "Training with Manual of Bodily Health";
    case 0x1b: return "Feather Fall";
    case 0x1f: return "Helpless";
    case 0x23: return "Prayer";
    case 0x2c: return "Cause Disease";
    case 0x32: return "Dreaded Mummy Disease";
    case 0x36: return "Repulsed";
    case 0x37: return "Poisoned";
    case 0x3b: return "Regenerating";
    case 0x3d: return "Fire Resistance";
    case 0x47: return "Invisible";
    case 0x48: return "Camouflaged";
    case 0x59: return "Displaced";
    }
    return NULL;
}

// 32FD:0FBC  Camp, Magic, Display: a scrolling list of every character and the spell effects on
// them, or "<No Spell Effects>".
void camp_display(void) {
    static const u8 none[] = {0};
    farptr head = rtl_getmem(0x2e), tail = head;
    P8(head, 0x29) = 0;
    P8(head, 0) = 1; P8(head, 1) = ' ';
    SETP32(head, 0x2a, 0);
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) {
        u8 shown = 0;
        tail = display_add(tail, pstr(ch), 1);
        for (farptr e = P32(ch, 0x7f); e; e = P32(e, 5)) {
            char num[16], line[64];
            const char *name = effect_name(P8(e, 0), num);
            if (!name) continue;
            shown++;
            snprintf(line, sizeof line, " %.40s", name);
            tail = display_add(tail, line, 0);
        }
        if (!shown) tail = display_add(tail, " <No Spell Effects>", 0);
        tail = display_add(tail, " ", 0);
    }
    u8 redraw = 1; s16 index = 0;
    ui_frame(FARPTR(RSEG(0x32fd), 0x0fb3), 15, 0, 0x16, 0x26, 1, 1);         // "Display"
    menu_list(&tail, &index, &redraw, 1, head, 0x16, 0x26, 4, 1, 15, 10, 11, none, none);
    menu_list_dispose(head);
    hud_draw_screen();
}

// 32FD:13F1  Camp, Magic: "Cast Memorize Scribe Display Rest Exit". Other keys move the party
// selection. Memorize, Scribe and Display are still original code.
void camp_magic(farptr interrupted) {
    static const u8 none[] = {0};
    u8 key = ' ', special;
    while (P8(interrupted, 0) == 0 && key != 0 && key != 'E') {
        key = menu_horizontal(&special, 1, 1, 15, 10, 13, &G8(0x0531), none);
        if (special) { party_select_move(key); hud_draw_roster(G32(0x5d92)); }
        else if (key == 'C') camp_cast();
        else if (key == 'M') camp_memorize();
        else if (key == 'S') camp_scribe();
        else if (key == 'D') camp_display();
        else if (key == 'R') camp_rest(interrupted);
    }
}

// 32FD:14A9  Move the selected character one place towards the front of the marching order
// (the party is a singly linked list from DS:5D96); the leader goes round to the back.
void camp_order_up(void) {
    farptr sel = G32(0x5d92), first = G32(0x5d96), p = first, before = 0, next = P32(sel, 0x104);
    if (first == sel) {
        while (P32(p, 0x104)) p = P32(p, 0x104);
        if (next) SETG32(0x5d96, next);
        SETP32(p, 0x104, sel);
        SETP32(sel, 0x104, 0);
        return;
    }
    while (P32(p, 0x104) != sel) { before = p; p = P32(p, 0x104); }
    SETP32(p, 0x104, next);
    SETP32(sel, 0x104, p);
    if (p != first && before) SETP32(before, 0x104, sel); else SETG32(0x5d96, sel);
}

// 32FD:1601  The same towards the back; the last character goes round to the front.
void camp_order_down(void) {
    farptr sel = G32(0x5d92), first = G32(0x5d96), p = first, next = P32(sel, 0x104);
    if (first != sel) while (P32(p, 0x104) != sel) p = P32(p, 0x104);
    if (!next) {
        SETP32(sel, 0x104, first);
        SETG32(0x5d96, sel);
        SETP32(p, 0x104, 0);
        return;
    }
    SETP32(sel, 0x104, P32(next, 0x104));
    if (first == sel) SETG32(0x5d96, next);
    if (p != sel) SETP32(p, 0x104, next);
    SETP32(next, 0x104, sel);
}

// 32FD:1780  Alter, Order. "Select Exit": the arrow keys move the party selection; Select picks
// the character up ("has been selected"), and then the menu is "Place Exit", where Home and End
// carry the character up and down the order until Place (or Enter) puts them down.
void camp_order(void) {
    u8 held = 0, key = ' ', special;
    while (key != 0 && key != 'E') {
        key = menu_horizontal(&special, 1, 1, 15, 10, 13, &G8(0x0584 + held * 0x29), &mem[LIN(FARPTR(RSEG(0x32fd), 0x1740))]);
        if (!special) {
            if (key == '\r' || key == 'P' || key == 'S') {
                held = !held;
                if (held) char_message("has been selected", 0); else ui_clear_message();
            }
        }
        else if (!held) { party_select_move(key); hud_draw_roster(G32(0x5d92)); }
        else {
            if (key == 'G') camp_order_up(); else if (key == 'O') camp_order_down();
            hud_draw_roster(G32(0x5d92));
        }
    }
}

// 32FD:18E5  Alter, Drop: remove the selected character from the party for good. With only one
// character left it offers to quit instead.
void camp_drop_character(void) {
    farptr sel = G32(0x5d92);
    if (P32(sel, 0x104) == 0 && sel == G32(0x5d96)) {
        if (menu_yes_no(15, 10, 14, FARPTR(RSEG(0x32fd), 0x1877)) == 'Y') {     // "quit TO DOS: "
            party_remove(0, 1);
            game_quit();
        }
        return;
    }
    char_message("will be gone", 0);
    if (menu_yes_no(15, 10, 14, FARPTR(RSEG(0x32fd), 0x1892)) != 'Y') { char_message("Breathes A sigh of relief", 1); return; }   // "Drop from party? "
    char_message(P8(sel, 0x10d) == 0 ? "is dumped in a ditch" : "bids you farewell", 1);
    party_remove(0, 1);                                // take the character out of the party
    text_clear_box(0x11, 1, 0x26, 0x0b);
    hud_draw_roster(G32(0x5d92));
}

// 32FD:1A97  Alter, Speed: the game speed (DS:49E2), 0 fastest to 9 slowest. Faster and Slower
// are only offered when there is room; the Down and Up arrows do the same within the limits.
void camp_speed(void) {
    u8 key, special;
    do {
        char t[64]; u8 items[48]; int n = 0;
        snprintf(t, sizeof t, "Game Speed = %u (0=fastest 9=slowest)", G8(0x49e2));
        put(t, 10, 0x12, 1);
        if (G8(0x49e2) != 0) n += sprintf((char *)items + 1 + n, " Faster");
        if (G8(0x49e2) < 9) n += sprintf((char *)items + 1 + n, " Slower");
        n += sprintf((char *)items + 1 + n, " Exit");
        items[0] = (u8)n;
        key = menu_horizontal(&special, 1, 1, 15, 10, 13, items, (const u8 *)"\013Game Speed:");
        if (!special) { if (key == 'F') G8(0x49e2)--; else if (key == 'S') G8(0x49e2)++; }
        else if (key == 'P') { if (G8(0x49e2) != 0) G8(0x49e2)--; }
        else if (key == 'H') { if (G8(0x49e2) < 9) G8(0x49e2)++; }
    } while (key != 0 && key != 'E');
    ui_clear_message();
}

// 32FD:1C59  Camp, Alter: "Order Drop Speed Icon Pics Exit". Pics switches the monster pictures
// (DS:49F6) and the character portraits (DS:49F5) on and off. The icon editor is still
// original code.
void camp_alter(void) {
    static const u8 none[] = {0};
    u8 key = ' ', special;
    while (key != 0 && key != 'E') {
        key = menu_horizontal(&special, 1, 1, 15, 10, 13, &G8(0x055a), &mem[LIN(FARPTR(RSEG(0x32fd), 0x1c10))]);
        if (special) { party_select_move(key); hud_draw_roster(G32(0x5d92)); }
        else if (key == 'O') camp_order();
        else if (key == 'D') camp_drop_character();
        else if (key == 'S') camp_speed();
        else if (key == 'I') { call_original(0x34fb, 0x382d, 0); hud_draw_screen(); }
        else if (key == 'P') {
            u8 k;
            do {
                u8 items[48];
                int n = sprintf((char *)items + 1, "%s%sExit", G8(0x49f6) ? "Monsters on  " : "Monsters off  ", G8(0x49f5) ? "Portraits on  " : "Portraits off  ");
                items[0] = (u8)n;
                k = menu_horizontal(&special, 1, 0, 15, 10, 13, items, none);
                if (k == 'M') G8(0x49f6) = G8(0x49f6) == 0;
                else if (k == 'P') G8(0x49f5) = G8(0x49f5) == 0;
            } while (k != 0 && k != 'E');
        }
    }
}

// 32FD:1E6F  The camp screen: "Save View Magic Rest Alter Exit". It ends on Exit or Esc, or when
// something sets *interrupted (a rest that was disturbed, for instance), which the caller uses to
// decide whether to run the area's after-camp script.
//   Save   saves the game, then asks "Quit to DOS" (the only way to leave the game from here)
//   View   the character sheet          Magic  memorise, scribe, cast
//   Rest   pass time and heal           Alter  party order, game speed and the like
void camp_menu(farptr interrupted) {
    u8 saved_display = G8(0x49f3), special, key = ' ';
    farptr sel;
    G8(0x49f3) = 2;
    SETG16(0x6dd8, 0);
    memset(&G8(0x6dc2), 0, 14);                          // the rest-time counters
    hud_draw_screen();
    text_clear_box(1, 17, 38, 22);
    text_put_string(FARPTR(RSEG(0x32fd), 0x1e23), 10, 18, 1);       // "The party makes camp..."
    call_original(0x32fd, 0x0214, 0);
    P8(interrupted, 0) = 0;
    while (P8(interrupted, 0) == 0 && key != 0 && key != 'E') {
        key = menu_horizontal(&special, 1, 1, 15, 10, 13, &G8(0x0508), &mem[LIN(FARPTR(RSEG(0x32fd), 0x1e5b))]);
        if (special) {
            party_select_move(key);
            sel = G32(0x5d92); hud_draw_roster(sel);
        }
        else if (key == 'S') {
            call_original(0x3941, 0x1d58, 0);
            if (menu_yes_no(15, 10, 14, FARPTR(RSEG(0x32fd), 0x1e62)) == 'Y') game_quit();   // "Quit TO DOS "
        }
        else if (key == 'V') call_original_scratch(0x3c10, 0x0ab3, 64);
        else if (key == 'M') camp_magic(interrupted);
        else if (key == 'R') camp_rest(interrupted);
        else if (key == 'A') camp_alter();
    }
    call_original(0x32fd, 0x0214, 0);
    SETG32(0x6db5, 0);
    G8(0x49f3) = saved_display;
    hud_draw_status();
    ui_clear_message();
    menu_clear_line();
}
