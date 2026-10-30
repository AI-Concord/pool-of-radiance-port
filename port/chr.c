#include "fixes.h"
// Character helpers from the resident unit at 10BA: the ability-score tables and other small things.
#include <stdio.h>
#include "poolrad.h"
#include "scene.h"
#include "rtl.h"
#include "hud.h"
#include "view.h"
#include "text.h"
#include "menu.h"
#include "chr.h"
#include "rules.h"
#include "input.h"
#include "gfx.h"
#include "effects.h"
#include "fight.h"
#include "sheet.h"
#include "map.h"
#include "combat.h"

// 10BA:11EA  Strength as a single scale: 3..17 as they are, 18 with its percentage becomes
// 18 (none), 19 (01-50), 20 (51-75), 21 (76-90), 22 (91-99), 23 (00); 19 to 25 become 24 to 30.
// For impossible values (18 with more than 100, or above 25) the original returns whatever was in a register; they
// are taken as 18/00 and 25 here.
int chr_strength_index(farptr ch) {
    u8 st = P8(ch, 0x10), pct = P8(ch, 0x16);
    if (st < 18) return st;
    if (st == 18) return pct == 0 ? 18 : pct <= 50 ? 19 : pct <= 75 ? 20 : pct <= 90 ? 21 : pct <= 99 ? 22 : 23;
    if (st < 26) return st + 5;
    return 30;
}

// 10BA:1282  Bonus to hit from Strength, for those it applies to (byte AAh of the record).
s8 chr_strength_hit_bonus(farptr ch) {
    int i = chr_strength_index(ch);
    if (!P8(ch, 0xaa)) return 0;
    if (i >= 1 && i <= 3) return -3;
    if (i >= 4 && i <= 5) return -2;
    if (i >= 6 && i <= 7) return -1;
    if (i >= 17 && i <= 19) return 1;
    if (i >= 20 && i <= 22) return 2;
    if (i >= 23 && i <= 25) return 3;
    if (i >= 26 && i <= 27) return 4;
    if (i >= 28 && i <= 30) return (s8)(i - 23);
    return 0;
}

// 10BA:1325  Bonus to damage from Strength.
s8 chr_strength_damage_bonus(farptr ch) {
    int i = chr_strength_index(ch);
    if (!P8(ch, 0xaa)) return 0;
    if (i >= 1 && i <= 2) return -2;
    if (i >= 3 && i <= 5) return -1;
    if (i == 16) return 1;
    if (i >= 17 && i <= 19) return (s8)(i - 16);
    if (i >= 20 && i <= 29) return (s8)(i - 17);
    if (i == 30) return 14;
    return 0;
}

// 10BA:10E6  Armour class adjustment from Dexterity (byte 13h).
s8 chr_dex_defence(farptr ch) {
    u8 d = P8(ch, 0x13);
    if (d >= 1 && d <= 3) return -4;
    if (d >= 4 && d <= 6) return (s8)(d - 7);
    if (d >= 15 && d <= 18) return (s8)(d - 14);
    if (d >= 19 && d <= 20) return 4;
    if (d >= 21 && d <= 23) return 5;
    if (d >= 24 && d <= 25) return 6;
    return 0;
}

// 10BA:1168  Missile attack adjustment from Dexterity.
s8 chr_dex_missile(farptr ch) {
    u8 d = P8(ch, 0x13);
    if (d < 3) return -4;
    if (d <= 5) return (s8)(d - 6);
    if (d >= 16 && d <= 18) return (s8)(d - 15);
    if (d >= 19 && d <= 20) return 3;
    if (d >= 21 && d <= 23) return 4;
    if (d >= 24 && d <= 25) return 5;
    return 0;
}

// 10BA:0B73  Does the character carry any of the four effects listed at DS:286C?
u8 chr_has_listed_effect(farptr ch) {
    u8 any = 0;
    for (int i = 1; i <= 4; i++) if (char_has_effect(G8(0x286b + i), ch)) any = 1;
    return any;
}

// 10BA:148B  Remove one memorised copy of a spell from the selected character.
void chr_forget_spell(u8 spell) {
    farptr sel = G32(0x5d92);
    for (int i = 0; i < 0x15; i++) if (P8(sel, 0x17 + i) == spell) { P8(sel, 0x17 + i) = 0; break; }
}

// 10BA:1037, 1073, 10AD  Number to text (a byte, a word, a signed 32-bit number).
void chr_number_text(farptr dest, s32 value) {
    char t[16]; int n = snprintf(t, sizeof t, "%d", (int)value);
    P8(dest, 0) = (u8)n; memcpy(&P8(dest, 1), t, (size_t)n);
}

// 10BA:13AB  How Strength changes the weight a character can carry, in coin-weights: from
// -350 for the weakest, +500 at 17, +3000 at 18/00 (the scale is chr_strength_index's).
s16 chr_strength_carry(farptr ch) {
    int i = chr_strength_index(ch);
    if (i >= 1 && i <= 3) return -350;
    if (i >= 4 && i <= 5) return -250;
    if (i >= 6 && i <= 7) return -150;
    if (i >= 12 && i <= 13) return 100;
    if (i >= 14 && i <= 15) return 200;
    if (i == 16) return 350;
    if (i >= 17 && i <= 21) return (s16)((i - 17) * 250 + 500);
    if (i >= 22 && i <= 26) return (s16)((i - 22) * 1000 + 2000);
    if (i == 27) return 7500;
    if (i >= 28 && i <= 30) return (s16)((i - 28) * 3000 + 9000);
    return 0;
}

// 10BA:23B7  Is this one of the party's side? (Byte 10Eh: 0 the party, 1 the enemy.)
u8 chr_on_party_side(farptr ch) { return P8(ch, 0x10e) == 0; }

// 10BA:23DB  Count who can still act on each side: DS:6814 the party, DS:6815 the enemy.
void chr_count_sides(void) {
    G8(0x6814) = 0; G8(0x6815) = 0;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) if (P8(ch, 0x10d)) G8(0x6814 + (s8)P8(ch, 0x10e))++;
}

// 10BA:2637  Wipe a combatant's action record (far pointer at 108h): nothing chosen.
u8 chr_clear_action(farptr ch) {
    farptr a = P32(ch, 0x108);
    P8(a, 3) = 0; P8(a, 0) = 0; P8(a, 7) = 0; P8(a, 6) = 0;
    return 1;
}

// 10BA:2686  The Guard command in combat.
u8 chr_guard(farptr ch) {
    u8 r = chr_clear_action(ch);
    P8(P32(ch, 0x108), 7) = 1;
    ui_notice((const u8 *)"\010Guarding");
    return r;
}

// 10BA:26C2  The level a spell is cast at by the selected character: cleric level for a
// cleric spell, magic-user level for a magic-user spell, 12 for the third kind (items and
// monsters, by the look of it); but always 6 when DS:6DBF is set, which seems to mean "cast
// from a scroll". A class beyond 2 does not occur in the spell table (the original would return an unset value);
// it is treated like 2.
int chr_caster_level(u8 spell) {
    u8 c = G8(0x31fa + spell * 16); int level;
    farptr sel = G32(0x5d92);
    if (c == 0) level = P8(sel, 0x96); else if (c == 1) level = P8(sel, 0x9b); else level = 12;
    if (G8(0x6dbf) != 0 && c != 2) level = 6;
    return level;
}

// 10BA:2B5E  Put the exploring screen back (after a shop, a fight, and so on).
void chr_back_to_exploring(void) {
    G8(0x49f3) = 3;
    hud_explore_frames();
    view_redraw();
    view_draw_marker();
    hud_draw_status();
    hud_draw_roster(G32(0x5d92));
}

// 10BA:2E03 and 2E46  Is the readied weapon a missile weapon (item table byte 0Ch of its type
// at DS:5582 is 2 or more), and is it one of those with both of bits 14h set in byte 0Eh?
u8 chr_has_missile_weapon(farptr ch) {
    farptr w = P32(ch, 0xcc);
    return w && G8(0x558e + P8(w, 0x2e) * 16) >= 2;
}
u8 chr_has_missile_weapon_14(farptr ch) {
    return chr_has_missile_weapon(ch) && (G8(0x5590 + P8(P32(ch, 0xcc), 0x2e) * 16) & 0x14) == 0x14;
}

// 10BA:234A  "<name> is fully healed" or "is partially healed".
void chr_healed_message(farptr ch) {
    u8 p[256] = {0};
    const char *t = P8(ch, 0x11b) == P8(ch, 0x32) ? "is fully healed" : "is partially healed";
    p[0] = (u8)strlen(t); memcpy(p + 1, t, p[0]);
    ui_char_message(1, 10, p, ch);
    if (G8(0x49f3) != 5) hud_draw_roster(G32(0x5d92));
}

// ---- Derived statistics ----
// Item types have a 16-byte entry at DS:5582: byte 0 the equipment slot (0 weapon, 1 shield,
// 2 armour, ... 9 ring), byte 1 hands needed, byte 6 the armour value (top bit set = "this is
// protective gear"), bytes 9 and 0Ah damage dice and sides, 0Bh damage bonus, 0Eh flags
// (1 uses ammunition from slot F8h, 2 Dexterity applies, 4 Strength applies, 80h uses slot FCh).
#define ITYPE(item, field) G8(0x5582 + P8(item, 0x2e) * 16 + (field))

// 10BA:0000  To-hit, damage dice and damage bonus with the readied weapon. An elf gets +1 to hit
// with types 24h, 25h and 29h-2Ch (the swords and bows of the rulebook).
void chr_weapon_stats(farptr ch) {
    farptr w = P32(ch, 0xcc);
    if (!w) return;
    u8 t = P8(w, 0x2e), flags = ITYPE(w, 0x0e);
    P8(ch, 0x110) = P8(ch, 0x2d);
    if (flags & 2) P8(ch, 0x110) += (u8)chr_dex_missile(ch);
    P8(ch, 0x119) = ITYPE(w, 0x0b);
    if (flags & 4) { P8(ch, 0x110) += (u8)chr_strength_hit_bonus(ch); P8(ch, 0x119) += (u8)chr_strength_damage_bonus(ch); }
    u8 plus = P8(w, 0x32);
    if (flags > 0x7f && P32(ch, 0xfc)) plus += P8(P32(ch, 0xfc), 0x32);
    if ((flags & 1) && P32(ch, 0xf8)) plus += P8(P32(ch, 0xf8), 0x32);
    P8(ch, 0x119) += plus;
    if (P8(ch, 0x2e) == 2 && ((t > 0x28 && t < 0x2d) || t == 0x25 || t == 0x24)) plus++;
    P8(ch, 0x110) += plus;
    P8(ch, 0x115) = ITYPE(w, 9);
    P8(ch, 0x117) = ITYPE(w, 0x0a);
}

// 10BA:01FB  Movement in armour: up to 150 in weight leaves it alone, up to 399 makes it 9,
// heavier makes it 6; magic armour gives 3 back.
void chr_armour_movement(farptr item, farptr ch) {
    if (ITYPE(item, 0) != 2) return;
    u16 w = P16(item, 0x37);
    P8(ch, 0x11c) = w < 0x97 ? P8(ch, 0x72) : w <= 399 ? 9 : 6;
    if (P8(item, 0x32) != 0 && P8(ch, 0x11c) < 10) P8(ch, 0x11c) += 3;
}

// 10BA:0282  One readied item's part in armour class. ac[1] is the shield, ac[2] other magic
// that adds up, ac[3] the best ring, ac[4] the best body armour. Magic body armour sets *magic,
// which later cancels the ring: the rulebook's "rings of protection do not work with magic armour".
void chr_item_armour(u8 *magic, s8 *ac, farptr item, farptr ch) {
    u8 v = ITYPE(item, 6);
    if (v <= 0x7f) return;
    v &= 0x7f;
    u8 slot = ITYPE(item, 0); s8 plus = (s8)P8(item, 0x32);
    if (slot == 1) ac[1] = (s8)(v + plus);
    else if (v == 0) {
        if (slot == 9) { if (ac[3] < plus) ac[3] = plus; } else ac[2] = (s8)(ac[2] + plus);
        P8(ch, 0x101) += P8(item, 0x33);
    }
    else if ((int)ac[4] < (int)v + plus) {
        ac[4] = (s8)(v + plus);
        if (plus > 0 && slot == 2) *magic = 1;
    }
}

// 10BA:03A0  Movement under load: what is carried beyond the Strength allowance slows the
// character to 9 (over 512), 6 (over 768) or 3 (over 1024).
void chr_load_movement(farptr ch) {
    s16 over = (s16)(P16(ch, 0x102) - (u16)chr_strength_carry(ch));
    if (over < 0) over = 0;
    u8 limit = over <= 0x200 ? P8(ch, 0x11c) : over <= 0x300 ? 9 : over <= 0x400 ? 6 : 3;
    if (limit < P8(ch, 0x11c)) P8(ch, 0x11c) = limit;
}

// 10BA:0BB8  Work a character's fighting numbers out again from scratch: which item is in which
// of the thirteen equipment slots (far pointers from CCh), how many items (C7h), hands in use
// (100h), weight carried (102h), attacks (115h-11Ah), to-hit (110h), movement (11Ch) and armour
// class (111h, with 112h the class as seen from behind: no shield, no Dexterity).
void chr_recalculate(farptr ch) {
    u8 lightens = 0, magic_armour = 0;
    s8 ac[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i <= 12; i++) SETP32(ch, 0xcc + i * 4, 0);
    P8(ch, 0xc7) = 0; P8(ch, 0x100) = 0; SETP16(ch, 0x102, 0); SETG16(0x4846, 0);
    for (farptr item = P32(ch, 0xc8); item; item = P32(item, 0x2a)) {
        P8(ch, 0xc7)++;
        u16 w = P16(item, 0x37);
        if (P8(item, 0x39)) w = (u16)(P8(item, 0x39) * w);
        SETP16(ch, 0x102, (u16)(P16(ch, 0x102) + w));
        if (!P8(item, 0x34)) continue;
        SETG16(0x4846, (u16)(G16(0x4846) + w));
        u8 slot = ITYPE(item, 0);
        if (slot < 9) SETP32(ch, 0xcc + (s8)slot * 4, item);
        else if (slot == 9) { if (!P32(ch, 0xf0)) SETP32(ch, 0xf0, item); else if (!P32(ch, 0xf4)) SETP32(ch, 0xf4, item); }
        if (P8(item, 0x2e) == 0x49) SETP32(ch, 0xf8, item);
        if (P8(item, 0x2e) == 0x1c) SETP32(ch, 0xfc, item);
        if (P8(item, 0x2f) == 0xba) lightens = 1;
        P8(ch, 0x100) += ITYPE(item, 1);
    }
    for (int k = 0; k <= 6; k++) SETP16(ch, 0x102, (u16)(P16(ch, 0x102) + P16(ch, 0x88 + k * 2)));
    for (int k = 1; k <= 2; k++) { P8(ch, 0x114 + k) = P8(ch, 0xa2 + k); P8(ch, 0x116 + k) = P8(ch, 0xa4 + k); P8(ch, 0x118 + k) = P8(ch, 0xa6 + k); }
    P8(ch, 0x101) = 0;
    P8(ch, 0x111) = P8(ch, 0xa9);
    P8(ch, 0x11c) = P8(ch, 0x72);
    P8(ch, 0x110) = P8(ch, 0x2d);
    ac[0] = chr_dex_defence(ch);
    if (!P32(ch, 0xcc)) { P8(ch, 0x110) += (u8)chr_strength_hit_bonus(ch); P8(ch, 0x119) += (u8)chr_strength_damage_bonus(ch); }
    chr_weapon_stats(ch);
    for (farptr item = P32(ch, 0xc8); item; item = P32(item, 0x2a))
        if (P8(item, 0x34)) { chr_armour_movement(item, ch); chr_item_armour(&magic_armour, ac, item, ch); }
    if (magic_armour) ac[3] = 0;
    if (lightens) {                                                         // an item whose first name word is BAh takes 5000 off the load
        SETP16(ch, 0x102, P16(ch, 0x102) < 5000 ? 0 : (u16)(P16(ch, 0x102) - 5000));
        if (P16(ch, 0x102) < G16(0x4846)) SETP16(ch, 0x102, G16(0x4846));
    }
    chr_load_movement(ch);
    if ((int)ac[4] < (int)P8(ch, 0x111)) ac[4] = (s8)P8(ch, 0x111);
    P8(ch, 0x111) = 0;
    for (int i = 0; i <= 4; i++) P8(ch, 0x111) += (u8)ac[i];
    P8(ch, 0x112) = (u8)(ac[4] + ac[2] + ac[3] - 2);
    P8(ch, 0x6b) = ((s8)P8(ch, 0x98) < 1 || (s8)P8(ch, 0x2e) < 1) ? 1 : P8(ch, 0x98);
}

// 10BA:0442  Build an item's display name into the item record itself (the first 41 bytes) and
// optionally print it. The name is put together from up to three words (codes at 2Fh-31h, into
// the word table at DS:10A7, last word first), leaving out the words still hidden (bits of byte
// 35h: an unidentified "Long Sword +1" is just "Long Sword"). In front go " Yes  " or " No   "
// for readied (item lists), a "*" if anyone in the party has Detect Magic running and the item
// is magical, and the count for a stack, in which case one of the words gets an "s".
void chr_item_name(u8 draw, u8 show_readied, u8 row, u8 col, farptr item) {
    char name[400] = "";
#define ADD(t) do { strcat(name, t); name[40] = 0; } while (0)
    P8(item, 0) = 0;
    if (show_readied) ADD(P8(item, 0x34) ? " Yes  " : " No   ");
    int detect = 0;
    for (farptr ch = G32(0x5d96); ch && !detect; ch = P32(ch, 0x104)) if (char_has_effect(5, ch)) detect = 1;
    if (detect && ((s8)P8(item, 0x32) > 0 || (s8)P8(item, 0x33) > 0 || P8(item, 0x36) != 0)) ADD("* ");
    if (P8(item, 0x39) != 0) { char n[8]; snprintf(n, sizeof n, "%u ", P8(item, 0x39)); ADD(n); }
    u8 shown = 0, type = P8(item, 0x2e), plural_done = 0;
    for (int k = 1; k <= 3; k++) if (P8(item, 0x2e + k) != 0 && ((P8(item, 0x35) >> (3 - k)) & 1) == 0) shown = (u8)(shown + (1 << (k - 1)));
    for (int k = 3; k >= 1; k--) {
        if (!((shown >> (k - 1)) & 1)) continue;
        ADD(pstr(FARPTR(DATA_SEG, 0x10a7 + P8(item, 0x2e + k) * 21)));
        if (P8(item, 0x39) < 2 || plural_done) ADD(" ");
        else if ((1 << (k - 1)) == shown || (k == 1 && shown > 4 && type != 0x56) || (k == 2 && !(shown & 1)) || (k == 3 && type == 0x56)
                 || ((type == 0x49 || type == 0x1c) && P8(item, 0x31) != 0xb1)) { ADD("s "); plural_done = 1; }
        else ADD(" ");
    }
#undef ADD
    size_t n = strlen(name);
    if (n) { P8(item, 0) = (u8)n; memcpy(&P8(item, 1), name, n); }
    if (draw) text_put_string(item, 10, row, col);
}

// 10BA:2C48  "Trade to? Select Exit" and its like: let the player move a highlight up and down
// the party (Home and End, wrapping at both ends) and choose with Select or Enter. *target comes
// in as the starting character and goes out as the choice, or nil for Exit.
void chr_pick_character(farptr target_var, u8 with_exit, const u8 *prompt) {
    u8 items[16], ask[64], key = ' ', special;
    u8 n = prompt[0] > 40 ? 40 : prompt[0];
    memcpy(ask + 1, prompt + 1, n); ask[n + 1] = ' '; ask[0] = (u8)(n + 1);
    items[0] = (u8)sprintf((char *)items + 1, "Select%s", with_exit ? " Exit" : "");
    while (key != '\r' && key != 27 && key != 'E' && key != 'S') {
        hud_draw_roster(P32(target_var, 0));
        key = menu_horizontal(&special, G8(0x49f3) == 2 || G8(0x49f3) == 6, 1, 15, 10, 13, items, ask);
        farptr cur = P32(target_var, 0), first = G32(0x5d96);
        if (!special) { if (key == 'E' || key == 0) cur = 0; }
        else if (key == 'O') { cur = P32(cur, 0x104); if (!cur) cur = first; }
        else if (key == 'G') {
            farptr was = cur;
            cur = first;
            if (was == first) while (P32(cur, 0x104)) cur = P32(cur, 0x104);
            else while (P32(cur, 0x104) != was) cur = P32(cur, 0x104);
        }
        SETP32(target_var, 0, cur);
    }
}

// 10BA:2228  Take damage off a character and settle what state that leaves them in. Damage that
// exactly uses up the hit points: Unconscious (status 4). One to nine points more: Dying (5),
// with the excess kept as a bleeding count in the action record during a fight. Ten or more
// beyond, or anything that empties an Animated creature (status 1): Dead (6). Anyone left in
// a state other than Okay or Animated can no longer act (10Dh), has no hit points, and in a
// fight comes off their side's count of those still standing.
void chr_apply_damage(u8 damage, farptr ch) {
    u8 hp = P8(ch, 0x11b), over = hp < damage ? (u8)(damage - hp) : 0, left = hp < damage ? 0 : (u8)(hp - damage);
    if (over < 10 && (left != 0 || P8(ch, 0x10c) != 1)) {
        if (over == 0) { if (left == 0) P8(ch, 0x10c) = 4; }
        else { P8(ch, 0x10c) = 5; if (G8(0x49f3) == 5) P8(P32(ch, 0x108), 0x0e) = over; }
    } else P8(ch, 0x10c) = 6;
    if (P8(ch, 0x10c) <= 1) { P8(ch, 0x11b) = left; return; }
    P8(ch, 0x10d) = 0;
    P8(ch, 0x11b) = 0;
    if (G8(0x49f3) == 5) { G8(0x6814 + (s8)P8(ch, 0x10e))--; P8(P32(ch, 0x108), 3) = 0; }
}

// 10BA:2F57  Has the character reached its race's limit in class `cls`? Only fighting (class 2) is limited, by
// Strength (17, or less than 17, lowering the limit), plus clerics of race 4 at level 5:
//   race 1: 7, or 8 with Strength 17 (18 and up: no limit)    race 2: 5, 6 with 17, else 7
//   race 3: 5 below 18, else 6                                race 4: 6, 7 with 17, else 8
//   race 5: 4, 5 with 17, else 6
u8 chr_level_capped(u8 cls, farptr ch) {
    s8 lv = (s8)P8(ch, 0x96 + cls);
    u8 str = P8(ch, 0x10), race = P8(ch, 0x2e);
    if (lv <= 0) return 0;
    switch (race) {
    case 1: return cls == 2 && ((lv == 8 && str == 17) || (lv == 7 && str < 17));
    case 2: return cls == 2 && (lv == 7 || (lv == 6 && str == 17) || (lv == 5 && str < 17));
    case 3: return cls == 2 && (lv == 6 || (lv == 5 && str < 18));
    case 4: if (cls == 0 && lv == 5) return 1;
            return cls == 2 && (lv == 8 || (lv == 7 && str == 17) || (lv == 6 && str < 17));
    case 5: return cls == 2 && (lv == 6 || (lv == 5 && str == 17) || (lv == 4 && str < 17));
    }
    return 0;
}

// 34FB:0000  Frees a character entirely: its action record (22 bytes), every item (63), every status effect (9),
// and the 285-byte record itself.
void chr_free(farptr ch) {
    if (P32(ch, 0x108)) rtl_freemem(P32(ch, 0x108), 0x16);
    for (farptr it = P32(ch, 0xc8); it; ) { farptr n = P32(it, 0x2a); rtl_freemem(it, 0x3f); it = n; }
    for (farptr fx = P32(ch, 0x7f); fx; ) { farptr n = P32(fx, 5); rtl_freemem(fx, 9); fx = n; }
    rtl_freemem(ch, 0x11d);
}

// 454B:0329  Thief skills (bytes 77h-7Eh): the base for the thief level (DS:3CF3, eight per level) plus the race's
// adjustment (DS:3D3B, eight per race), zero if a negative adjustment is bigger than the base; the first five also get
// the Dexterity adjustment (DS:3D4E, five per point of byte 13h).
void chr_thief_skills(farptr ch) {
    for (int k = 1; k <= 8; k++) {
        s8 adj = (s8)G8((u16)((s16)(s8)P8(ch, 0x2e) * 8 + k + 0x3d3b));
        u8 base = G8((u16)((s16)(s8)P8(ch, 0x9c) * 8 + k + 0x3cf3));
        if (adj < 0 && (int)base < -(int)adj) P8(ch, 0x76 + k) = 0;
        else {
            P8(ch, 0x76 + k) = (u8)(base + adj);
            if (k < 6) P8(ch, 0x76 + k) = (u8)(P8(ch, 0x76 + k) + G8(P8(ch, 0x13) * 5 + k + 0x3d4e));
        }
    }
}

// 454B:0255  The five saving throws (bytes 6Dh-71h): 20, lowered to the best value any of the character's classes
// gives at its level in that class (DS:426E, 45 bytes per class, 5 per level).
void chr_saving_throws(farptr ch) {
    for (int s = 0; s <= 4; s++) {
        P8(ch, 0x6d + s) = 0x14;
        for (int k = 0; k <= 7; k++) {
            s8 lv = (s8)P8(ch, 0x96 + k);
            if (lv <= 0) continue;
            u8 v = G8((u16)(k * 0x2d + lv * 5 + s + 0x426e));
            if (v < P8(ch, 0x6d + s)) P8(ch, 0x6d + s) = v;
        }
    }
}

// 3941:1446  Adds a character to the party: appended to the party list (or starting it), selected, given the first
// free party position (byte BFh, 0-7), and counted in the area (word 67Ch); then 454B:0000 (still original).
void chr_join_party(farptr ch) {
    P8(ch, 0xbf) = 0xff;
    if (!G32(0x5d96)) { SETP32(FARPTR(DATA_SEG, 0), 0x5d96, ch); SETP32(FARPTR(DATA_SEG, 0), 0x5d92, ch); }
    else {
        farptr p = G32(0x5d96);
        while (P32(p, 0x104)) p = P32(p, 0x104);
        SETP32(p, 0x104, ch);
    }
    u8 used[8] = {0};
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) if (P8(c, 0xbf) < 8) used[P8(c, 0xbf)] = 1;
    SETP32(FARPTR(DATA_SEG, 0), 0x5d92, ch);
    P8(ch, 0xbf) = 0;
    while (P8(ch, 0xbf) < 8 && used[P8(ch, 0xbf)]) P8(ch, 0xbf)++;
    wr16(LIN(G32(0x49d6)) + 0x67c, (u16)(P16(G32(0x49d6), 0x67c) + 1));
    chr_recalc_levels(ch);
}

// 3FA9:0000  How much a character can carry: its Strength allowance (10BA:13AB) plus 1500.
u16 chr_max_load(farptr ch) {
    u16 a = chr_strength_index(ch) < 0 ? (u16)call_original(0x10ba, 0x13ab, 2, FP_SEG(ch), FP_OFF(ch)) : (u16)chr_strength_carry(ch);
    return (u16)(a + 0x5dc);
}
// 3FA9:0020 / 003A  Weight carried (word 102h) goes down / up.
void chr_load_sub(u16 amount, farptr ch) { wr16(LIN(ch) + 0x102, (u16)(P16(ch, 0x102) - amount)); }
void chr_load_add(u16 amount, farptr ch) { wr16(LIN(ch) + 0x102, (u16)(P16(ch, 0x102) + amount)); }
// 3FA9:0054  Would `amount` more be too heavy? If so *room gets what still fits.
u8 chr_too_heavy(u16 *room, u16 amount, farptr ch) {
    u16 max = chr_max_load(ch);
    if ((u16)(P16(ch, 0x102) + amount) > max) { *room = (u16)(chr_max_load(ch) - P16(ch, 0x102)); return 1; }
    *room = 0; return 0;
}
// 3FA9:0A01  A character gives up `amount` coins of one kind (word 88h + 2 * kind), which in camp or on the treasure
// screen (modes 1 and 6) go into the pool (DS:67F4).
void chr_drop_coins(s8 kind, u16 amount, farptr ch) {
    wr16(LIN(ch) + 0x88 + kind * 2, (u16)(P16(ch, 0x88 + kind * 2) - amount));
    chr_load_sub(amount, ch);
    if (G8(0x49f3) == 6 || G8(0x49f3) == 1) wr32(DS_LIN + 0x67f4 + kind * 4, rd32(DS_LIN + 0x67f4 + kind * 4) + amount);
}
// 3FA9:0FD2  1d20: 1-14 gives 1, 15-19 gives 2, 20 gives 3.
u8 roll_one_two_three(void) {
    u8 r = roll_dice(20, 1);
    if (r >= 1 && r <= 14) return 1;
    if (r >= 15 && r <= 19) return 2;
    return 3;
}

// ---- 1802:0356  Removing every occurrence of a pattern from a string (a string function) ----
// Positions are tried from 1 to len(s)-len(pat)+1 as counted at the start (a byte, so it wraps when the pattern is longer).
void str_remove_all(farptr result, farptr src, farptr pat) {
    u8 t[256], q[256]; memcpy(t, &mem[LIN(src)], 256); memcpy(q, &mem[LIN(pat)], 256);
    u8 plen = q[0], end = (u8)(t[0] - q[0] + 1);
    if (end) for (u8 pos = 1; ; pos++) {
        u8 n = 0;                                           // Copy(t, pos, plen)
        if (pos <= t[0]) { n = plen; if (pos + n > t[0] + 1) n = (u8)(t[0] - pos + 1); }
        if (n == plen && !memcmp(t + pos, q + 1, n)) {      // Delete(t, pos, plen)
            if (pos <= t[0]) { u8 d = plen; if (pos + d > t[0] + 1) d = (u8)(t[0] - pos + 1); memmove(t + pos, t + pos + d, t[0] - pos - d + 1); t[0] = (u8)(t[0] - d); }
        }
        if (pos == end) break;
    }
    memcpy(&mem[LIN(result)], t, t[0] + 1u);
}
// ---- 1802:0425  A DOS file name from a character name: ten forbidden characters removed, eight kept, upper case ----
void str_dos_name(farptr dest, farptr src) {
    farptr sc = scratch_push(512), a = sc, c = sc + 256;
    memcpy(&mem[LIN(a)], &mem[LIN(src)], mem[LIN(src)] + 1u);
    for (int i = 1; i <= 10; i++) { mem[LIN(c)] = 1; mem[LIN(c) + 1] = G8(i + 0x0d19); str_remove_all(a, a, c); }
    u8 *t = &mem[LIN(a)];
    if (t[0] > 8) t[0] = 8;
    for (int i = 1; i <= t[0]; i++) if (t[i] >= 'a' && t[i] <= 'z') t[i] -= 0x20;
    u8 n = t[0] > 0x28 ? 0x28 : t[0]; mem[LIN(dest)] = n; memcpy(&mem[LIN(dest)] + 1, t + 1, n);
    scratch_pop(512);
}
// ---- 1802:02EB  Whether a file exists: open it for reading and look at IOResult (also left in DS:49CE) ----
int file_exists(farptr name) {
    farptr sc = scratch_push(0x80 + 0x29), f = sc, nm = sc + 0x80;
    u8 n = mem[LIN(name)] > 0x28 ? 0x28 : mem[LIN(name)]; mem[LIN(nm)] = n; memcpy(&mem[LIN(nm)] + 1, &mem[LIN(name)] + 1, n);
    file_assign(f, nm);
    file_reset(f, 0x80);
    file_close(f);
    u16 r = (u16)rtl_ioresult();
    wr16(LIN(FARPTR(DATA_SEG, 0x49ce)), r);
    scratch_pop(0x80 + 0x29);
    return r == 0 && n != 0;
}
// ---- 1802:0193  Showing a message on the bottom line until a key is pressed ----
void file_disk_prompt(farptr msg, u8 color) {
    farptr sc = scratch_push(0x2a);
    u8 n = mem[LIN(msg)] > 0x28 ? 0x28 : mem[LIN(msg)]; mem[LIN(sc)] = n; memcpy(&mem[LIN(sc)] + 1, &mem[LIN(msg)] + 1, n);
    text_bios_bar(0x28, 0, 0x18, 0);
    text_bios_string(sc, color, 0x18, 0);
    call_original(0x1899, 0x0059, 0);
    text_bios_bar(0x28, 0, 0x18, 0);
    scratch_pop(0x2a);
}
// ---- 1802:025D  Opening a file for reading, asking for the disk until it is there (unless may_fail) ----
int file_open(farptr f, u8 may_fail, farptr prompt, farptr name) {
    farptr sc = scratch_push(0x52), nm = sc, pr = sc + 0x29;
    u8 n = mem[LIN(name)] > 0x28 ? 0x28 : mem[LIN(name)]; mem[LIN(nm)] = n; memcpy(&mem[LIN(nm)] + 1, &mem[LIN(name)] + 1, n);
    n = mem[LIN(prompt)] > 0x28 ? 0x28 : mem[LIN(prompt)]; mem[LIN(pr)] = n; memcpy(&mem[LIN(pr)] + 1, &mem[LIN(prompt)] + 1, n);
    int e;
    for (;;) {
        e = file_exists(nm);
        if (e || may_fail) break;
        file_disk_prompt(pr, 0x0e);
    }
    if (e) {
        file_assign(f, nm);
        file_reset(f, 1);
    }
    scratch_pop(0x52);
    return e;
}

// ---- Character files: 3941:0AEC (load) and 3941:069E (save) ----
// A character is three files: NAME.CHA (or .SAV inside a saved game) with the 11Dh-byte record, NAME.ITM with its
// items (3Fh bytes each, chained through +2Ah) and NAME.SPC with its effects (9 bytes each, chained through +5).
// File handling goes through the original Turbo Pascal runtime with the file record on the DOS stack.
#define CF_CS(o) FARPTR(RSEG(0x3941), (o))
static u8 *hp(farptr p) { return &mem[LIN(p)]; }
static void ps_set(farptr d, farptr s, u8 max) { u8 n = hp(s)[0] > max ? max : hp(s)[0]; memmove(hp(d) + 1, hp(s) + 1, n); hp(d)[0] = n; }
static void ps_cat(farptr d, farptr s, u8 max) { u8 n = hp(d)[0], k = hp(s)[0]; if (n + k > max) k = (u8)(max - n); memmove(hp(d) + 1 + n, hp(s) + 1, k); hp(d)[0] = (u8)(n + k); }
static void ps_chr(farptr d, u8 c, u8 max) { if (hp(d)[0] < max) { hp(d)[++hp(d)[0]] = c; } }
static void f_assign(farptr f, farptr name) { file_assign(f, name); }
static void f_close(farptr f) { file_close(f); }
static int f_eof(farptr f) { return (u8)file_eof(f) != 0; }
static void f_read(farptr f, farptr buf, u16 n, farptr res) { file_block_read(f, buf, n, res); }
static void f_write(farptr f, farptr buf, u16 n) { file_block_write(f, buf, n, 0); }
static void f_rewrite(farptr f) { file_rewrite(f, 1); }
static int f_exists(farptr name) { return file_exists(name); }
static void f_open(farptr name, farptr prompt, farptr f) { file_open(f, 0, prompt, name); }
static void f_dosname(farptr d, farptr s) { str_dos_name(d, s); }
enum { CF_F = 0, CF_NAME = 0x80, CF_PATH = 0x180, CF_TMP = 0x280, CF_PROMPT = 0x380, CF_STEM = 0x3b0, CF_RES = 0x3e0, CF_SIZE = 0x3e4 };
static void cf_chain_read(farptr f, farptr name, farptr prompt, farptr res, farptr *head, u16 size, u16 link) {
    farptr p = rtl_getmem(size); *head = p;
    f_open(name, prompt, f);
    for (;;) {
        f_read(f, p, size, res);
        wr32(LIN(p) + link, 0);
        if (!f_eof(f)) { farptr q = rtl_getmem(size); wr32(LIN(p) + link, q); p = q; }
        if (f_eof(f)) break;
    }
    f_close(f);
}
void chr_load_file(u8 in_game_dir, u8 saved, farptr rec, farptr name) {
    farptr s = scratch_push(CF_SIZE), f = s + CF_F, nm = s + CF_NAME, path = s + CF_PATH, tmp = s + CF_TMP, pr = s + CF_PROMPT, ext = s + CF_STEM, res = s + CF_RES;
    ps_set(nm, name, 255);
    if (!in_game_dir) {
        ps_set(pr, FARPTR(DATA_SEG, G8(0x5376) * 0x1e + 0x2614), 40); ps_chr(pr, G8(0x547d), 40); ps_cat(pr, CF_CS(0x0ad6), 40);
    } else {
        f_dosname(tmp, nm); ps_set(path, FARPTR(DATA_SEG, 0x537a), 255); ps_cat(path, tmp, 255); ps_set(nm, path, 255);
        ps_set(pr, CF_CS(0x0ac4), 40); ps_chr(pr, G8(0x537b), 40); ps_cat(pr, CF_CS(0x0ad6), 40);
    }
    ps_set(ext, saved ? CF_CS(0x0ad8) : CF_CS(0x0add), 4);
    ps_set(path, nm, 255); ps_cat(path, ext, 255);
    f_open(path, pr, f);
    f_read(f, rec, 0x11d, res);
    f_close(f);
    static const u16 zero[] = { 0x104, 0x106, 200, 0xca, 0x7f, 0x81, 0x108, 0x10a };
    for (int i = 0; i < 8; i++) wr16(LIN(rec) + zero[i], 0);
    farptr head;
    ps_set(path, nm, 255); ps_cat(path, CF_CS(0x0ae2), 255);
    if (f_exists(path)) { cf_chain_read(f, path, pr, res, &head, 0x3f, 0x2a); wr32(LIN(rec) + 200, head); }
    ps_set(path, nm, 255); ps_cat(path, CF_CS(0x0ae7), 255);
    if (f_exists(path)) { cf_chain_read(f, path, pr, res, &head, 9, 5); wr32(LIN(rec) + 0x7f, head); }
    scratch_pop(CF_SIZE);
    keys_flush();
}
static void cf_chain_write(farptr f, farptr path, farptr stem, u16 ext, farptr head, u16 size, u16 link) {
    ps_set(path, stem, 255); ps_cat(path, CF_CS(ext), 255);
    if (f_exists(path)) { f_assign(f, path); file_erase(f); }      // Erase
    if (!head) return;
    f_assign(f, path); f_rewrite(f);
    for (farptr p = head; p; p = rd32(LIN(p) + link)) f_write(f, p, size);
    f_close(f);
}
void chr_save_file(farptr rec, farptr filename) {
    farptr s = scratch_push(CF_SIZE + 4), f = s + CF_F, nm = s + CF_NAME, path = s + CF_PATH, tmp = s + CF_TMP, pr = s + CF_PROMPT, stem = s + CF_STEM, ext = s + CF_RES, list = s + CF_SIZE;
    ps_set(nm, filename, 8);
    if (hp(nm)[0] == 0) {
        // a new character: add its name to the roster file, then derive a DOS file name from it
        wr32(LIN(list), 0);
        roster_read(list);
        farptr last = rd32(LIN(list));
        if (last) while (rd32(LIN(last) + 0x2a)) last = rd32(LIN(last) + 0x2a);
        farptr node = rtl_getmem(0x2e);
        memset(hp(node), 0, 0x2e);
        ps_set(node, rec, 0x28);
        if (!last) wr32(LIN(list), node); else wr32(LIN(last) + 0x2a, node);
        roster_write(rd32(LIN(list)));
        menu_list_free(list);
        ps_set(ext, CF_CS(0x0674), 4);
        str_remove_all(tmp, rec, FARPTR(RSEG(0x3941), 0x0679));
        f_dosname(path, tmp);
        ps_set(stem, FARPTR(DATA_SEG, 0x537a), 40); ps_cat(stem, path, 40);
    } else {
        save_dir_make();
        ps_set(ext, CF_CS(0x067b), 4);
        ps_set(stem, FARPTR(DATA_SEG, 0x537a), 40); ps_cat(stem, nm, 40);
    }
    for (;;) {
        ps_set(path, stem, 255); ps_cat(path, ext, 255);
        f_assign(f, path); f_rewrite(f);
        if ((u16)rtl_ioresult() == 0) break;      // IOResult
        ps_set(pr, CF_CS(0x0680), 255); ps_chr(pr, G8(0x537b), 255); ps_cat(pr, CF_CS(0x0692), 255);
        text_prompt(pr, 0x0e);
    }
    f_write(f, rec, 0x11d);
    f_close(f);
    cf_chain_write(f, path, stem, 0x0694, rd32(LIN(rec) + 200), 0x3f, 0x2a);
    cf_chain_write(f, path, stem, 0x0699, rd32(LIN(rec) + 0x7f), 9, 5);
    scratch_pop(CF_SIZE + 4);
}
// ---- 3941:02AE  Making sure the save directory exists: MkDir of the save path without its last character ----
// IOResult 0 (made) or 5 (already there) ends it; anything else asks for the save disk and tries again.
void save_dir_make(void) {
    farptr sc = scratch_push(0x100);
    for (;;) {
        u8 *src = &mem[LIN(FARPTR(DATA_SEG, 0x537a))], cnt = (u8)(src[0] - 1), n = 0;    // Copy(path, 1, len-1)
        if (src[0] >= 1) n = cnt > src[0] ? src[0] : cnt;
        hp(sc)[0] = n; memcpy(hp(sc) + 1, src + 1, n);
        file_mkdir(sc);          // MkDir
        u16 r = (u16)rtl_ioresult();
        if (r == 0 || r == 5) break;
        ps_set(sc, CF_CS(0x029a), 255); ps_chr(sc, G8(0x537b), 255); ps_cat(sc, CF_CS(0x02ac), 255);
        text_prompt(sc, 0x0e);
    }
    scratch_pop(0x100);
}
// ---- 3941:0010 / 01B8  The roster file CHARLIST.TXT: one character name per line ----
// Reading builds a list of 2Eh-byte nodes (name, link at +2Ah) and marks the names of the current party with "* ";
// writing skips those marked lines. Text-file I/O goes through the original runtime (TextRec on the DOS stack);
// Read and Write leave the file on the stack for the ReadLn/WriteLn that follows, hence emu_pcall_strfn.
static void t_assign(farptr t, farptr name) { text_assign(t, name); }
void roster_read(farptr listvar) {
    wr32(LIN(listvar), 0);
    save_dir_make();
    farptr sc = scratch_push(0x200), t = sc, nm = sc + 0x100;
    ps_set(nm, FARPTR(DATA_SEG, 0x537a), 255); ps_cat(nm, CF_CS(0x0000), 255);
    if (f_exists(nm)) {
        ps_set(nm, FARPTR(DATA_SEG, 0x537a), 255); ps_cat(nm, CF_CS(0x0000), 255);
        t_assign(t, nm);
        text_reset(t);                               // Reset
        farptr last = 0;
        while (!(u8)text_eof(t)) {                  // Eof
            farptr node = rtl_getmem(0x2e);
            memset(hp(node), 0, 0x2e);
            if (!rd32(LIN(listvar))) wr32(LIN(listvar), node); else wr32(LIN(last) + 0x2a, node);
            last = node;
            text_read_str(t, node, 0x28);   // Read(t, name)
            text_readln(t);                            // ReadLn
            farptr e = G32(0x5d96);
            while (e && !(hp(e)[0] == hp(node)[0] && !memcmp(hp(e) + 1, hp(node) + 1, hp(node)[0]))) e = rd32(LIN(e) + 0x104);
            if (e) { ps_set(nm, CF_CS(0x000d), 255); ps_cat(nm, node, 255); ps_set(node, nm, 0x28); }
        }
        text_close(t);                               // Close
    }
    scratch_pop(0x200);
}
void roster_write(farptr list) {
    save_dir_make();
    farptr sc = scratch_push(0x200), t = sc, nm = sc + 0x100;
    for (;;) {
        ps_set(nm, FARPTR(DATA_SEG, 0x537a), 255); ps_cat(nm, CF_CS(0x0197), 255);
        t_assign(t, nm);
        text_rewrite(t);                               // Rewrite
        if ((u16)rtl_ioresult() == 0) break;
        ps_set(nm, CF_CS(0x01a4), 255); ps_chr(nm, G8(0x537b), 255); ps_cat(nm, CF_CS(0x01b6), 255);
        text_prompt(nm, 0x0e);
    }
    for (farptr p = list; p; p = rd32(LIN(p) + 0x2a)) {
        if (hp(p)[1] == '*') continue;
        text_write_str(t, p, 0);   // Write(t, name)
        text_writeln(t);                                // WriteLn
    }
    text_close(t);
    scratch_pop(0x200);
}
// ---- 34FB:306D  Main menu, Add Character: a list of the roster; Add loads the character and joins it to the party
// unless it is already there or the party is full (6 living, or DS:49D6->67Ch 8 in all). Added names get "* ".
// Two locals are never set in the original: the list's "with Exit" flag (the menu text already says "Add Exit"; the
// stack byte is 0 when the original runs, so 0 here) and the living count before the first Add (0 here too).
void party_add_menu(void) {
    text_clear_box(1, 1, 0x26, 0x16);
    farptr lv = scratch_push(4);
    roster_read(lv);
    farptr list = rd32(LIN(lv));
    scratch_pop(4);
    if (!list) return;
    s16 index = 0; u8 redraw = 1, key, alive = 0;
    farptr cur = list;
    do {
        key = menu_list(&cur, &index, &redraw, 0, list, 0x16, 0x26, 2, 1, 0x0f, 10, 0x0d,
                        &mem[LIN(FARPTR(RSEG(0x34fb), 0x3061))], &mem[LIN(FARPTR(RSEG(0x34fb), 0x304f))]);
        if ((key == 0x0d || key == 'A') && hp(cur)[1] != '*') {
            farptr ch = rtl_getmem(0x11d);
            chr_load_file(1, 0, ch, cur);
            farptr sc = scratch_push(0x100);
            ps_set(sc, FARPTR(RSEG(0x34fb), 0x306a), 255); ps_cat(sc, cur, 255); ps_set(cur, sc, 0x28);
            scratch_pop(0x100);
            alive = 0;
            farptr pa = G32(0x49d6);
            if (!G32(0x5d96)) {
                wr16(LIN(pa) + 0x67c, 0);
                chr_join_party(ch); chr_build_icon(1);
            } else {
                farptr e = G32(0x5d96);
                for (; e; e = rd32(LIN(e) + 0x104)) {
                    if (hp(e)[0] == hp(ch)[0] && !memcmp(hp(e) + 1, hp(ch) + 1, hp(ch)[0]) && P8(e, 0xab) == P8(ch, 0xab)) break;
                    if (P8(e, 0x84) < 0x80) alive++;
                }
                if (!e && ((P8(ch, 0x84) < 0x80 && alive < 6) || (P8(ch, 0x84) >= 0x80 && P16(pa, 0x67c) < 8))) {
                    chr_join_party(ch); chr_build_icon(1);
                    if (P8(ch, 0x84) < 0x80) alive++;
                } else rtl_freemem(ch, 0x11d);
            }
        }
    } while (key != 'E' && key != 0 && alive < 6 && P16(G32(0x49d6), 0x67c) < 8);
    roster_write(list);
    lv = scratch_push(4); wr32(LIN(lv), list); menu_list_free(lv); scratch_pop(4);
}
// ---- 3941:1D58  Saving the game: pick a slot A-J, write SAVGAMx.DAT, then each party member as CHRDATxn ----
// SAVGAMx.DAT holds the area number (DS:5376), four blocks of game state, a few globals, the party count and a table of
// eight 29h-byte file names. Unused table entries are whatever was on the stack, in the original as here, so that part
// of the file is not comparable. A party member's stand-alone .CHA/.ITM/.SPC files are deleted once saved in the game.
void game_save(void) {
    static const u8 none[] = "";
    u8 special, key;
    for (;;) {
        special = 0;
        key = menu_horizontal(&special, G8(0x49f3) == 2, 0, 0x0f, 10, 0x0d, &mem[LIN(CF_CS(0x1cd7))], &mem[LIN(CF_CS(0x1cc5))]);
        if (key == 0 || (key >= 'A' && key <= 'J')) break;
    }
    if (key == 0) return;
    save_dir_make();
    farptr sc = scratch_push(0x600), f = sc, path = sc + 0x80, pr = sc + 0x180, stem = sc + 0x1b0, tmp = sc + 0x1e0, tmp2 = sc + 0x2e0, tab = sc + 0x3e0, cnt = sc + 0x5a0;
    for (;;) {
        ps_set(path, FARPTR(DATA_SEG, 0x537a), 255); ps_cat(path, CF_CS(0x1d0b), 255); ps_chr(path, key, 255); ps_cat(path, CF_CS(0x1d12), 255);
        f_assign(f, path); f_rewrite(f);
        if ((u16)rtl_ioresult() == 0) break;
        ps_set(pr, CF_CS(0x1d17), 255); ps_chr(pr, G8(0x537b), 255); ps_cat(pr, CF_CS(0x1d29), 255);
        text_prompt(pr, 0x0e);
    }
    menu_clear_line();
    text_put_string(CF_CS(0x1d2b), 10, 0x18, 0);              // "Saving...Please Wait"
    farptr a = G32(0x49d2);
    for (int k = 1; k <= 3; k++) { wr16(LIN(a) + 0x3f2 + 2 * k, G16(k * 4 + 0x6aae)); wr16(LIN(a) + 0x3f8 + 2 * k, G16(k * 4 + 0x6ab0)); }
    wr16(LIN(a) + 0x1f8, G8(0x49e2));
    wr16(LIN(a) + 0x1fe, (u16)(G8(0x49f6) * 2 + G8(0x49f5)));
    wr16(LIN(G32(0x49d6)) + 0x624, G8(0x5376));
    f_write(f, FARPTR(DATA_SEG, 0x5376), 1);
    f_write(f, G32(0x49d2), 0x800); f_write(f, G32(0x49d6), 0x800); f_write(f, G32(0x49da), 0x400); f_write(f, G32(0x49de), 0x1e00);
    f_write(f, FARPTR(DATA_SEG, 0x6aad), 5); f_write(f, FARPTR(DATA_SEG, 0x49fa), 1); f_write(f, FARPTR(DATA_SEG, 0x49f3), 1);
    u8 n = 0;
    for (farptr ch = G32(0x5d96); ch; ch = rd32(LIN(ch) + 0x104)) {
        n++;
        farptr e = tab + (u16)((n - 1) * 0x29);
        ps_set(tmp, CF_CS(0x1d40), 255); ps_chr(tmp, key, 255); ps_chr(tmp, (u8)('0' + n % 10), 255);    // Str(n) of one character
        ps_set(e, tmp, 0x28);
    }
    hp(cnt)[0] = n;
    f_write(f, cnt, 1); f_write(f, tab, 0x148);
    f_close(f);
    n = 0;
    for (farptr ch = G32(0x5d96); ch; ch = rd32(LIN(ch) + 0x104)) {
        n++;
        ps_set(tmp, CF_CS(0x1d40), 255); ps_chr(tmp, key, 255); ps_chr(tmp, (u8)('0' + n % 10), 255);
        chr_save_file(ch, tmp);
        str_remove_all(tmp, ch, CF_CS(0x1d47)); str_dos_name(tmp2, tmp);
        ps_set(stem, FARPTR(DATA_SEG, 0x537a), 0x28); ps_cat(stem, tmp2, 0x28);
        static const u16 ext[3] = { 0x1d49, 0x1d4e, 0x1d53 };
        for (int i = 0; i < 3; i++) {
            ps_set(path, stem, 255); ps_cat(path, CF_CS(ext[i]), 255);
            if (f_exists(path)) { f_assign(f, path); file_erase(f); }
        }
    }
    scratch_pop(0x600);
    P8(FARPTR(DATA_SEG, 0), 0x557d) = 1;
    menu_clear_line();
}
// ---- 3941:1602  Loading a game: a menu of the slots A-J that have a SAVGAMx.DAT; read the game state back, load the
// party (CHRDATxn.SAV), delete those members' stand-alone files, rebuild their icons, then reload the area's map and
// wall sets (or the ICON sprite outside the dungeon view) and, when the game state asks for it, 3B80:06B3.
static void chr_dos_stem(farptr dst, farptr name, farptr t1, farptr t2) {    // DOS file name of a character name
    str_remove_all(t1, name, CF_CS(0x15e7)); str_dos_name(t2, t1); ps_set(dst, t2, 0x28);
}
void game_load(void) {
    save_dir_make();
    farptr sc = scratch_push(0x700), f = sc, path = sc + 0x80, menu = sc + 0x180, t1 = sc + 0x1a0, t2 = sc + 0x2a0, stem = sc + 0x3a0, tab = sc + 0x3d0, pr = sc + 0x5a0, res = sc + 0x6f0, cnt = sc + 0x6f2;
    #define SAVEPATH(c) do { ps_set(path, FARPTR(DATA_SEG, 0x537a), 255); ps_cat(path, CF_CS(0x157f), 255); ps_chr(path, (c), 255); ps_cat(path, CF_CS(0x1586), 255); } while (0)
    hp(menu)[0] = 0;
    for (u8 c = 'A'; c <= 'J'; c++) { SAVEPATH(c); if (f_exists(path)) { ps_chr(menu, c, 0x14); ps_cat(menu, CF_CS(0x158b), 0x14); } }
    if (hp(menu)[0] == 0) { scratch_pop(0x700); return; }
    u8 key;
    do {
        u8 special = 0;
        key = menu_horizontal(&special, 0, 0, 0x0f, 10, 0x0d, hp(menu), &mem[LIN(CF_CS(0x158d))]);     // "Load Which Game: "
        if (key >= 'A' && key <= 'J') { SAVEPATH(key); if (f_exists(path)) break; }
    } while (key != 0);
    if (key != 0) {
        SAVEPATH(key);
        ps_set(pr, CF_CS(0x15bf), 255); ps_cat(pr, FARPTR(DATA_SEG, 0x537a), 255);                       // "Put save disk in " + path
        file_open(f, 1, pr, path);
        menu_clear_line();
        text_put_string(CF_CS(0x15d1), 10, 0x18, 0);                                                     // "Loading...Please Wait"
        f_read(f, FARPTR(DATA_SEG, 0x5376), 1, res);
        f_read(f, G32(0x49d2), 0x800, res); f_read(f, G32(0x49d6), 0x800, res); f_read(f, G32(0x49da), 0x400, res); f_read(f, G32(0x49de), 0x1e00, res);
        f_read(f, FARPTR(DATA_SEG, 0x6aad), 5, res); f_read(f, FARPTR(DATA_SEG, 0x49fa), 1, res); f_read(f, FARPTR(DATA_SEG, 0x49f3), 1, res);
        f_read(f, cnt, 1, res); f_read(f, tab, 0x148, res);
        f_close(f);
        farptr g = G32(0x49d2);
        G8(0x49f6) = (u8)(P16(g, 0x1fe) >> 1); G8(0x49f5) = (u8)(P16(g, 0x1fe) & 1); G8(0x49e2) = P8(g, 0x1f8);
        wr16(LIN(G32(0x49d6)) + 0x67c, 0);
        u8 n = hp(cnt)[0];
        for (u8 k = 1; n && k <= n; k++) {
            chr_dos_stem(stem, tab + (u16)((k - 1) * 0x29), t1, t2);
            ps_set(path, FARPTR(DATA_SEG, 0x537a), 255); ps_cat(path, stem, 255); ps_cat(path, CF_CS(0x15e9), 255);
            if (f_exists(path)) {
                farptr rec = rtl_getmem(0x11d);
                chr_load_file(1, 1, rec, stem);
                chr_join_party(rec);
                save_dir_make();
            }
        }
        for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) {
            chr_dos_stem(t2 + 0x80, ch, t1, t2);
            ps_set(stem, FARPTR(DATA_SEG, 0x537a), 0x28); ps_cat(stem, t2 + 0x80, 0x28);
            ps_set(path, stem, 255); ps_cat(path, CF_CS(0x15ee), 255);
            if (f_exists(path)) {
                f_assign(f, path); file_erase(f);
                for (u16 e = 0x15f3; e <= 0x15f8; e += 5) {
                    ps_set(path, stem, 255); ps_cat(path, CF_CS(e), 255);
                    if (f_exists(path)) { f_assign(f, path); file_erase(f); }
                }
            }
        }
        for (SETG32(0x5d92, G32(0x5d96)); G32(0x5d92); SETG32(0x5d92, P32(G32(0x5d92), 0x104))) chr_build_icon(1);
        SETG32(0x5d92, G32(0x5d96));
        G8(0x5376) = (u8)P16(G32(0x49d6), 0x624);
        if (G8(0x49fa) < 2) {
            call_original(0x491c, 0x10c7, 1, P8(G32(0x49d2), 0x18a));
            for (int k = 1; k <= 3; k++) {
                SETG16(k * 4 + 0x6aae, P16(G32(0x49d2), 0x3f2 + 2 * k));
                SETG16(k * 4 + 0x6ab0, P16(G32(0x49d2), 0x3f8 + 2 * k));
                if ((s16)G16(k * 4 + 0x6aae) >= 0) map_load_part(G8(k * 4 + 0x6ab0), G8(k * 4 + 0x6aae));
            }
        } else sprite_load(CF_CS(0x15fd), 0, 0x0b);                                                    // "ICON"
        if (P16(G32(0x49d2), 0x344) != 0) map_reveal(0);
        keys_flush();
        menu_clear_line();
    }
    #undef SAVEPATH
    scratch_pop(0x700);
}
#undef CF_CS

// ---- 454B:0000  Recomputing what a character's class levels give: the best of each class's figure at 2Dh (from
// 3C7C), the highest level at 73h, attacks at A1h (class 2 gets 3 from level 7), cleric spell slots at B2h-B4h with
// the Wisdom bonus and the spells known for them, magic-user slots at B5h-B7h, saving throws and thief skills ----
void chr_recalc_levels(farptr ch) {
    P8(ch, 0x2d) = 0;
    for (int k = 0; k <= 7; k++) {
        s8 lv = (s8)P8(ch, k + 0x96);
        u8 v = G8((u16)(k * 0x0b + lv + 0x3c7c));
        if (P8(ch, 0x2d) < v) P8(ch, 0x2d) = v;
        if ((s8)P8(ch, 0x73) < lv) P8(ch, 0x73) = (u8)lv;
        if (k == 2) P8(ch, 0xa1) = lv < 7 ? 2 : 3;
        else if (k == 0) {
            if (lv > 1) {
                for (int i = 1; i <= 3; i++) P8(ch, i + 0xb1) = G8((u16)(lv * 3 + i + 0x40bc));
                chr_wisdom_spells(ch);
                for (int sp = 1; sp <= 0x38; sp++)
                    if (G8(sp * 16 + 0x31fa) == 0 && P8(ch, (s8)G8(sp * 16 + 0x31fb) + 0xb1) != 0) P8(ch, sp + 0x32) = 1;
            }
        } else if (k == 5 && lv > 1) for (int i = 1; i <= 3; i++) P8(ch, i + 0xb4) = G8((u16)(lv * 3 + i + 0x41d4));
    }
    chr_saving_throws(ch);
    if ((s8)P8(ch, 0x9c) > 0) chr_thief_skills(ch);
}
// ---- 34FB:3291  Taking the selected character (DS:5D92) out of the party and freeing it; the selection moves to
// the one before (or the new first). free_icon also frees its combat icon slot; keep_count leaves the party count
// at DS:49D6->67Ch alone ----
void party_remove(u8 keep_count, u8 free_icon) {
    farptr head = G32(0x5d96), sel = G32(0x5d92);
    if (!head) return;
    farptr pa = G32(0x49d6);
    if (head == sel) {
        if (free_icon) icon_free_slot(P8(sel, 0xbf));
        SETG32(0x5d96, P32(sel, 0x104));
        farptr next = P32(sel, 0x104);
        chr_free(sel);
        SETG32(0x5d92, next);
    } else {
        farptr p = head;
        while (P32(p, 0x104) != sel && P32(p, 0x104)) p = P32(p, 0x104);
        if (!P32(p, 0x104)) return;
        if (free_icon) icon_free_slot(P8(sel, 0xbf));
        SETP32(p, 0x104, P32(sel, 0x104));
        chr_free(sel);
        SETG32(0x5d92, p);
    }
    if (!keep_count) wr16(LIN(pa) + 0x67c, (u16)(P16(pa, 0x67c) - 1));
}

// ---- 142B:00A4  Start-up settings. Waits until pool.cfg can be opened (the start-up disk), starts the overlay
// manager on GAME.OVR, then reads POOL.CFG one line at a time: the display (E = EGA mode 0Dh, T = Tandy mode 9 with
// 400h paragraphs taken off the top of the heap, anything else CGA mode 4), the sound (T Tandy, P PC speaker, else
// none; the heap top comes down another 400h paragraphs whatever the answer), the save path DS:547C, the game path
// DS:537A and a last letter (F sets DS:49D1). In between it checks that memory holds the 18800h-byte overlay buffer.
// All file, overlay and console work goes through the original runtime. ----
#define CFG_CS(o) FARPTR(RSEG(0x142b), (o))
static void cfg_say(u16 msg) {
    text_write_str(FARPTR(DATA_SEG, 0x8608), FARPTR(FP_SEG(CFG_CS(msg)), msg), 0);     // Write(Output, msg)
}
static void cfg_halt(void) { text_writeln(FARPTR(DATA_SEG, 0x8608)); rtl_halt(0); }
static u8 cfg_letter(void) {
    u8 c = (u8)text_read_char(FARPTR(DATA_SEG, 0x484d));                       // Read(f, c)
    text_readln(FARPTR(DATA_SEG, 0x484d));                                     // ReadLn(f)
    return c;
}
static void cfg_heap_down(void) {
    wr16(DS_LIN + 0x4848, G16(0x4408)); wr16(DS_LIN + 0x484a, (u16)(G16(0x440a) - 0x400));
    wr16(DS_LIN + 0x4408, G16(0x4848)); wr16(DS_LIN + 0x440a, G16(0x484a));
}
static void cfg_path(u16 var) {
    text_read_str(FARPTR(DATA_SEG, 0x484d), FARPTR(DATA_SEG, var), 0xff);              // Read(f, path)
    text_readln(FARPTR(DATA_SEG, 0x484d));
    // the original then stores the string onto itself (1AF8:0B25 with the same source and destination): no change
}
// 1802:0212  Waits until game.ovr can be found (the overlay disk), asking for it and for a key each time.
void overlay_disk_wait(void) {
    farptr nm = scratch_push(12); u8 ok;
    do {
        ps_set(nm, FARPTR(RSEG(0x1802), 0x1ed), 255);
        ok = (u8)f_exists(nm);
        if (!ok) {
            text_write_str(FARPTR(DATA_SEG, 0x8608), FARPTR(RSEG(0x1802), 0x1f6), 0);   // Write(Output, ...)
            text_writeln(FARPTR(DATA_SEG, 0x8608));
            input_get_key();
        }
    } while (!ok);
    scratch_pop(12);
}
void config_read(void) {
    overlay_disk_wait();
    farptr nm = scratch_push(10);
    ps_set(nm, CFG_CS(0x0000), 255);
    ovr_init(nm);                                  // OvrInit('GAME.OVR')
    scratch_pop(10);
    do {
        file_assign(FARPTR(DATA_SEG, 0x494d), FARPTR(FP_SEG(CFG_CS(9)), 9));             // Assign(f, 'pool.cfg')
        file_reset(FARPTR(DATA_SEG, 0x494d), 0x80);                             // Reset(f, 128)
        G8(0x49cd) = (u8)rtl_ioresult();                                    // IOResult
        if (G8(0x49cd)) {
            cfg_say(0x12); text_writeln(FARPTR(DATA_SEG, 0x8608));
            G8(0x484c) = (u8)call_original(0x1899, 0x0059, 0);
        }
    } while (G8(0x49cd));
    file_close(FARPTR(DATA_SEG, 0x494d));                                       // Close(f)
    G8(0x84f0) = 0;
    text_assign(FARPTR(DATA_SEG, 0x484d), FARPTR(FP_SEG(CFG_CS(0x2e)), 0x2e));           // Assign(t, 'POOL.CFG')
    text_reset(FARPTR(DATA_SEG, 0x484d));                                       // Reset(t)
    if ((u16)rtl_ioresult()) { cfg_say(0x37); cfg_halt(); }
    G8(0x484c) = cfg_letter();
    G8(0x4a03) = 0x0f;
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));                                // Intr(10h): video mode
    G8(0x4a1c) = G8(0x4a02); G8(0x4a1d) = G8(0x4a1c);
    // 1464:00CF is entered with this routine's frame (BP, 0Ah of locals), its argument and a far return address
    // below the return address of this one: 12h bytes; 1464:00FF likewise from 10h bytes down.
    u16 sp = emu_sp();
    if (G8(0x484c) == 'E') {
        G8(0x4a1e) = 1; gfx_set_mode_at(0x0d, (u16)(sp - 0x12));
        G8(0x4a1b) = 0; G8(0x4a1a) = 0;
        text_show_page(G8(0x4a1b)); call_original(0x1709, 0x00a0, 1, G8(0x4a1a));
    } else if (G8(0x484c) == 'T') {
        G8(0x4a1e) = 2; cfg_heap_down(); gfx_set_mode_at(9, (u16)(sp - 0x12));
    } else { G8(0x4a1e) = 0; gfx_set_mode_at(4, (u16)(sp - 0x12)); }
    s32 avail = rtl_memavail();                                        // MemAvail
    s32 need = 0x18800 - ovr_get_buf();                              // less OvrGetBuf
    if (need <= avail) ovr_set_buf(0x18800);                     // OvrSetBuf(18800h)
    else {
        gfx_restore_mode_at((u16)(emu_sp() - 0x10));
        cfg_say(0x4f);
        u32 b = (u32)ovr_get_buf(), m = 0x18800 - b;
        text_write_long(FARPTR(DATA_SEG, 0x8608), (s32)m, 0);       // Write(Output, need)
        cfg_say(0x79); cfg_halt();
    }
    if (G16(0x43dc)) { gfx_restore_mode_at((u16)(emu_sp() - 0x10)); cfg_say(0x86); cfg_halt(); }       // OvrResult
    u8 c = cfg_letter();
    G8(0x484c) = c;
    G8(0x5372) = c == 'T' ? 0 : c == 'P' ? 1 : 2;
    cfg_heap_down();
    sound_resume();
    cfg_path(0x547c);
    cfg_path(0x537a);
    G8(0x484c) = cfg_letter();
    G8(0x49d1) = G8(0x484c) == 'F';
    text_close(FARPTR(DATA_SEG, 0x484d));                                       // Close(t)
}

// ---- 2A3D:002D  Starting the game: allocate the game-state blocks (DS:49D2/49D6/49DA/49DE) and work buffers, the
// screen buffers (1464:248E), screen-row and sprite address tables (DS:4EC0, 5050, 51E0), default state (party at
// 15,1 facing 6, area 1, ...), then show "Loading...", load the symbol sets, the "sqrpaci" pictures and the combat
// sprites, read the item table from ITEMS (2 bytes in, 810h bytes to DS:5582), Randomize, and set up the spell and
// effect handler tables. DS:465C is the loop counter throughout, as in the original. ----
void game_start(void) {
    SETG32(0x49d2, rtl_getmem(0x800)); SETG32(0x49d6, rtl_getmem(0x800)); SETG32(0x49da, rtl_getmem(0x400));
    SETG32(0x49de, rtl_getmem(0x1e00)); SETG32(0x5e20, rtl_getmem(0x588)); SETG32(0x6a58, rtl_getmem(0x924));
    SETG32(0x6a5c, rtl_getmem(0x400));
    call_original(0x1464, 0x248e, 6, 8, 1, 1, 0, DATA_SEG, 0x5e4e);
    call_original(0x1464, 0x248e, 6, 0xa8, 0x15, 1, 0, DATA_SEG, 0x4a20);
    call_original(0x1464, 0x248e, 6, 0x18, 3, 1, 0, DATA_SEG, 0x4a24);
    memset(&G8(0x4a28), 0, 0xa8); memset(&G8(0x4ad0), 0, 0x150); memset(&G8(0x4c20), 0xff, 0x150); memset(&G8(0x4d70), 0, 0x150);
    static const s16 band[4] = { -0x4800, -0x4600, -0x4400, -0x4200 };
    for (G8(0x465c) = 0; ; G8(0x465c)++) {
        u8 i = G8(0x465c);
        SETG16(i * 2 + 0x4ec0, (u16)((i >> 1) * 5 + ((i & 1) ? -0x4600 : -0x4800)));
        SETG16(i * 2 + 0x5050, (u16)(i * 0x28));
        SETG16(i * 2 + 0x51e0, (u16)((i >> 2) * 10 + band[i % 4]));
        if (i == 0xc7) break;
    }
    memset(&G8(0x5e3a), 0, 0x14); memset(&G8(0x5e52), 0, 8);
    memset(&mem[LIN(G32(0x49d2))], 0, 0x800); memset(&mem[LIN(G32(0x49d6))], 0, 0x800);
    memset(&mem[LIN(G32(0x49da))], 0, 0x400); memset(&mem[LIN(G32(0x49de))], 0, 0x1e00);
    memset(&G8(0x5e5a), 0, 0xd0);
    for (G8(0x465c) = 1; ; G8(0x465c)++) { u8 k = G8(0x465c); G8(k * 5 + 0x6afb) = 0xff; SETG16(k * 5 + 0x6afc, 0); SETG16(k * 5 + 0x6afe, 0); if (k == 2) break; }
    memset(&G8(0x6abe), 0, 0x42);
    SETG32(0x6810, 0);
    memset(&G8(0x6aad), 0, 5); G8(0x6aad) = 0x0f; G8(0x6aae) = 1; G8(0x6aaf) = 6;
    memset(&G8(0x6dde), 1, 3);
    SETG16(0x6ab2, 0); SETG16(0x6ab4, 1);
    for (G8(0x465c) = 2; ; G8(0x465c)++) { u8 k = G8(0x465c); SETG16(k * 4 + 0x6aae, 0xffff); SETG16(k * 4 + 0x6ab0, 0xffff); if (k == 3) break; }
    G8(0x49f5) = 1; G8(0x49f6) = 1; G8(0x49f7) = 0; G8(0x4a00) = 1; G8(0x49f8) = 0; SETG16(0x49fb, 0);
    SETG32(0x5d96, 0); SETG32(0x5d92, 0); SETG16(0x49ed, 0x9900);
    G8(0x49e2) = 1; G8(0x49f4) = 0; G8(0x5376) = 1; G8(0x5377) = 1; SETG16(0x547a, 1); SETG16(0x5378, 1);
    G8(0x6aac) = 0; G8(0x49f9) = 1; G8(0x49fa) = 1;
    wr16(LIN(G32(0x49d6)) + 0x67c, 0);
    G8(0x6ddc) = 0; SETG16(0x6b29, 1); G8(0x84d4) = 0; G8(0x6dda) = 0; G8(0x84dd) = 0; G8(0x84de) = 0; G8(0x84df) = 0;
    G8(0x84e0) = 0; G8(0x49ff) = 0; G8(0x5370) = 1; G8(0x84e3) = 0; G8(0x557c) = 0; G8(0x6b0a) = 0; G8(0x6b1c) = 0xff;
    G8(0x557d) = 0; G8(0x6f69) = 0xe8; G8(0x6f68) = 0xdb; G8(0x84e9) = 0; SETG16(0x5374, 0);
    sound_music(100);
    ps_set(FARPTR(DATA_SEG, 0x4661), FARPTR(DATA_SEG, 0x547c), 0x28);
    G8(0x547c) = 0;
    text_load_font();
    menu_clear_line();
    #define SCS(o) FARPTR(RSEG(0x2a3d), (o))
    text_put_string(SCS(0x0000), 10, 0x18, 0);                                                   // "Loading...Please Wait"
    call_original(0x4ca0, 0x003b, 2, 0xca, 4);
    call_original(0x4ca0, 0x003b, 2, 0xcb, 0);
    call_original(0x1464, 0x0111, 7, RSEG(0x2a3d), 0x0016, 1, 0, 0, DATA_SEG, 0x5e52);            // "sqrpaci"
    call_original(0x1464, 0x0111, 7, RSEG(0x2a3d), 0x0016, 2, 0, 0, DATA_SEG, 0x5e56);
    for (G8(0x465c) = 0; ; G8(0x465c)++) { u8 k = G8(0x465c); sprite_load(SCS(0x001e), k, (u8)(k + 0x0d)); if (k == 0x0b) break; }   // "COMSPR"
    sprite_load(SCS(0x001e), 0x19, 0x19);
    farptr sc = scratch_push(0x80), pr = sc;
    ps_set(pr, FARPTR(DATA_SEG, G8(0x5376) * 0x1e + 0x2614), 255); ps_chr(pr, G8(0x547d), 255); ps_cat(pr, SCS(0x002b), 255);
    G8(0x4660) = (u8)file_open(FARPTR(DATA_SEG, 0x468c), 0, pr, SCS(0x0025));                         // "ITEMS"
    scratch_pop(0x80);
    file_reset(FARPTR(DATA_SEG, 0x468c), 1);                                        // Reset(f, 1)
    file_seek(FARPTR(DATA_SEG, 0x468c), 2);                                     // Seek(f, 2)
    f_read(FARPTR(DATA_SEG, 0x468c), FARPTR(DATA_SEG, 0x5582), 0x810, FARPTR(DATA_SEG, 0x5e1e));
    f_close(FARPTR(DATA_SEG, 0x468c));
    ps_set(FARPTR(DATA_SEG, 0x547c), FARPTR(DATA_SEG, 0x4661), 255);
    menu_clear_line();
    rtl_randomize();
    spell_table_init();
    effect_table_init();
    #undef SCS
}

// ---- 3941:22D5  Making sure a data file can be found: if not, "Save and exit  Insert disk #<area>"; S steps the
// party back one square (wrapping on the 16x16 map), and saves and quits. (Not reached by the tests: the files are
// always there.) ----
void need_file(farptr name) {
    farptr sc = scratch_push(0x60), nm = sc, msg = sc + 0x30;
    ps_set(nm, name, 0x28);
    if (file_exists(nm)) { scratch_pop(0x60); return; }
    char a[8]; snprintf(a, sizeof a, "%u", G8(0x5376));
    ps_set(msg, FARPTR(RSEG(0x3941), 0x22b7), 0x28); ps_chr(msg, (u8)a[0], 0x28);
    static const u8 none[] = "";
    u8 special = 0;
    u8 k = menu_horizontal(&special, 0, 0, 0x0f, 10, 0x0d, hp(msg), none);
    scratch_pop(0x60);
    if (special || k != 'S') return;
    s8 x = (s8)(G8(0x6aad) - G8(G8(0x6aaf) + 0x2736)); if (x < 0) x = 0x0f; if (x > 0x0f) x = 0; G8(0x6aad) = (u8)x;
    s8 y = (s8)(G8(0x6aae) - G8(G8(0x6aaf) + 0x273f)); if (y < 0) y = 0x0f; if (y > 0x0f) y = 0; G8(0x6aae) = (u8)y;
    file_set_restore();
    game_save();
    game_quit();
}

// ---- 3FA9:1010  Making a random item of a type: scrolls (3Dh cleric, 3Eh magic-user) get 1-3 random spells by level;
// weapons and armour (types 1-3Bh, 49h, 4Dh, 5Dh) a +1..+3 bonus (3FA9:0FD2) that sets the name parts, value and,
// for some, a special kind; the other special types pick a template from the table at DS:0970 by a die roll. A bug
// kept: type 4Eh rolls 1-3 but tests for 0, 1 and 2, so a 3 gives no template (fixed when FIX is on). ----
static u16 w970(int i) { return G16((u16)(i * 2 + 0x970)); }
void item_make(farptr it, u8 type) {
    memset(&mem[LIN(it)], 0, 0x3f);
    u16 tpl = 0;
    memset(&mem[LIN(it) + 0x3c], 0, 3);
    wr32(LIN(it) + 0x2a, 0); P8(it, 0x34) = 0; P8(it, 0x35) = 6; P8(it, 0x36) = 0; P8(it, 0x2e) = type;
    memset(&mem[LIN(it) + 0x2f], 0, 3);
    u8 t = type;
    if (!((t == 0 || t > 0x3b) && t != 0x49 && t != 0x4d && t != 0x5d)) {
        P8(it, 0x32) = roll_one_two_three();
        u8 plus = P8(it, 0x32);
        if (t == 0x15) { if (roll_dice(5, 1) == 5) tpl = 0x90; P8(it, 0x31) = 0x15; P8(it, 0x30) = (u8)(plus - 0x5f); }
        else if (t == 0x1c) { P8(it, 0x31) = 0x1c; P8(it, 0x30) = (u8)(plus - 0x5f); }
        else if (t == 0x32 || t == 0x33) { P8(it, 0x31) = t; P8(it, 0x30) = 0x31; P8(it, 0x2f) = (u8)(plus - 0x5f); P8(it, 0x35) = 4; }
        else if (t == 0x34) { P8(it, 0x31) = t; P8(it, 0x30) = 0x32; P8(it, 0x2f) = (u8)(plus - 0x5f); P8(it, 0x35) = 4; }
        else if (t >= 0x35 && t <= 0x3a) { P8(it, 0x31) = t; P8(it, 0x30) = 0x30; P8(it, 0x2f) = (u8)(plus - 0x5f); P8(it, 0x35) = 4; }
        else if (t == 0x49) { P8(it, 0x31) = 0x3d; P8(it, 0x30) = (u8)(plus - 0x5f); }
        else if (t == 0x4d) {
            P8(it, 0x31) = 0x4f; P8(it, 0x30) = 0xa7; P8(it, 0x32) = (u8)(plus * 2 + 2);
            if (P8(it, 0x32) == 4) P8(it, 0x2f) = 0xdd; else if (P8(it, 0x32) == 6) P8(it, 0x2f) = 0xde; else if (P8(it, 0x32) == 8) P8(it, 0x2f) = 0xdc;
        }
        else if (t == 0x5d) { P8(it, 0x31) = 0x42; P8(it, 0x30) = 0xe0; P8(it, 0x2f) = (u8)(plus - 0x5f); }
        else { P8(it, 0x31) = t; P8(it, 0x30) = (u8)(plus - 0x5f); }
        P8(it, 0x33) = 0; P8(it, 0x39) = 0;
        u16 w; u8 c = t;
        if (c == 1 || c == 0x0d || c == 0x0e || c == 0x23) w = 0x4b;
        else if (c == 2 || c == 0x14 || c == 0x1d || c == 0x1f || c == 0x20 || c == 0x21 || c == 0x27 || c == 0x2a || c == 0x2c || c == 0x2e || c == 0x3b) w = 0x32;
        else if (c == 3 || c == 0x18 || c == 0x28) w = 0x7d;
        else if (c == 4 || c == 0x0f || c == 0x17 || c == 0x22 || c == 0x2b || c == 0x2d || c == 0x33) w = 100;
        else if (c == 5 || c == 0x0c || c == 0x11 || c == 0x13 || c == 0x32) w = 0x96;
        else if (c == 6) w = 0x0f;
        else if (c == 7) w = 0x1e;
        else if (c == 8 || c == 0x4d) w = 10;
        else if (c == 9) { w = 0x19; P8(it, 0x39) = 5; }
        else if (c == 0x0a || c == 0x1a || c == 0x24) w = 0x3c;
        else if (c == 0x0b || c == 0x10 || c == 0x19 || c == 0x1b || c == 0x29 || c == 0x2f) w = 0x50;
        else if (c == 0x12) w = 0xaf;
        else if (c == 0x15) w = 0x14;
        else if (c == 0x16 || c == 0x1e) w = 0x28;
        else if (c == 0x25) w = 0x23;
        else if (c == 0x26 || c == 0x35) w = 0xfa;
        else if (c == 0x34) w = 200;
        else if (c == 0x36 || c == 0x38) w = 400;
        else if (c == 0x37) w = 300;
        else if (c == 0x39) w = 0x15e;
        else if (c == 0x3a) w = 0x1c2;
        else if (c == 0x2f || c == 0x5d) w = 1;
        else { w = 0x28; P8(it, 0x39) = 10; }
        wr16(LIN(it) + 0x37, w);
        s16 p = (s8)P8(it, 0x32), m;
        if (c == 0x3b) m = 0x9c4; else if (c == 0x49 || c == 0x1c) m = 0x96; else if (c == 0x35 || c == 0x36) m = 3000;
        else if (c == 0x37 || c == 0x38) m = 0xdac; else if (c == 0x39) m = 4000; else if (c == 0x3a) m = 5000; else if (c == 0x4d) m = 3000; else m = 2000;
        wr16(LIN(it) + 0x3a, (u16)(p * m));
    } else if (t == 0x3d || t == 0x3e) {
        u8 n = roll_dice(3, 1);
        P8(it, 0x31) = t == 0x3d ? 0xd1 : 0xd0;
        P8(it, 0x30) = (u8)(n - 0x2f); P8(it, 0x2f) = 0; P8(it, 0x32) = 1; wr16(LIN(it) + 0x37, 0x19); P8(it, 0x39) = 0; wr16(LIN(it) + 0x3a, 0);
        for (u8 i = 1; n && i <= n; i++) {
            u8 lv = roll_dice(3, 1), sp = 0;
            if (t == 0x3d) { if (lv == 1) sp = (u8)(roll_dice(0x0d, 1) + 8); else if (lv == 2) sp = (u8)(roll_dice(7, 1) + 0x1c); else if (lv == 3) sp = (u8)(roll_dice(0x0b, 1) + 0x2c); }
            else { if (lv == 1) sp = roll_dice(8, 1); else if (lv == 2) sp = (u8)(roll_dice(7, 1) + 0x15); else if (lv == 3) sp = (u8)(roll_dice(9, 1) + 0x23); }
            P8(it, i + 0x3b) = sp;
            wr16(LIN(it) + 0x3a, (u16)(lv * 300 + P16(it, 0x3a)));
        }
    } else if (t == 0x3f) tpl = 0x49;
    else if (t == 0x43) tpl = 0x91;
    else if (t == 0x45) { u8 r = roll_dice(10, 1); if (r == 1) tpl = 0x29; else if (r >= 2 && r <= 6) tpl = 0x79; else if (r >= 7 && r <= 10) tpl = 0x81; }
    else if (t == 0x4e) { u8 r = roll_dice(3, 1); if (FIX) r--; if (r == 0) tpl = 0x31; else if (r == 1) tpl = 0x41; else if (r == 2) tpl = 0x71; }
    else if (t == 0x4f) tpl = 0x39;
    else if (t == 0x54) tpl = 9;
    else if (t == 0x5c) tpl = 0x61;
    else if (t == 0x47) { u8 r = roll_dice(0x0b, 1); if (r >= 1 && r <= 5) tpl = 0x11; else if (r >= 6 && r <= 8) tpl = 1; else if (r == 9) tpl = 0x19; else if (r == 10) tpl = 0x51; }
    else if (t == 0x46) { u8 r = roll_dice(10, 1); if (r >= 1 && r <= 7) tpl = 0x59; else if (r >= 8 && r <= 10) tpl = 0x89; }
    if (tpl) {
        for (int k = 0; k <= 2; k++) P8(it, 0x2f + k) = (u8)w970(tpl + k);
        P8(it, 0x32) = 1; P8(it, 0x33) = 1;
        if (tpl == 0x61) { P8(it, 0x32) = 2; P8(it, 0x33) = 2; }
        wr16(LIN(it) + 0x37, w970(tpl + 3)); P8(it, 0x39) = 0; wr16(LIN(it) + 0x3a, w970(tpl + 4));
        for (int k = 1; k <= 3; k++) P8(it, 0x3b + k) = (u8)w970(tpl + 4 + k);
    }
}
