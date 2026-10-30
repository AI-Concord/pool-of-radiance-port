#include <stdio.h>
#include <stdlib.h>
// Dice and status-effect rules (original overlay unit at 4592).
#include "poolrad.h"
#include "rtl.h"
#include "combat.h"
#include "chr.h"
#include "text.h"
#include "hud.h"
#include "rules.h"

// 4592:0DE3  Roll `count` dice of `sides` sides and add them up. The total is a byte and wraps.
u8 roll_dice(u8 sides, u8 count) {
    u8 total = 0;
    for (u8 i = 0; i < count; i++) total = (u8)(total + rtl_random(sides) + 1);
    return total;
}

// 4592:0E2E  The same, noting the number of dice at DS:681C for whoever asks afterwards.
u8 roll_dice_noted(u8 sides, u8 count) { G8(0x681c) = count; return roll_dice(sides, count); }

// 4592:0000  Call the routine registered for this kind of effect: the far pointer at DS:6828 + kind*4 (a jump into
// the effect unit), with (arg, node, ch) as its parameters.
static void effect_handler(u8 arg, farptr node, farptr ch, u8 kind) {
    farptr h = G32(0x6828 + kind * 4);
    call_original((u16)(FP_SEG(h) - LOAD_SEG + 0x1000), FP_OFF(h), 5, FP_SEG(ch), FP_OFF(ch), FP_SEG(node), FP_OFF(node), arg);
}

// 4592:0026  Take a status effect off a character. Pass the node, or nil to remove the first of
// that kind. If the node's byte 4 is set the effect's own routine is told first (so that, say,
// a Strength spell can give the old score back); then the node is unlinked and freed.
void effect_remove(farptr node, u8 kind, farptr ch) {
    if (!node) for (node = P32(ch, 0x7f); node && P8(node, 0) != kind; node = P32(node, 5)) ;
    if (!node) return;
    if (P8(node, 4) != 0) effect_handler(1, node, ch, kind);
    if (P32(ch, 0x7f) == node) SETP32(ch, 0x7f, P32(node, 5));
    else {
        farptr p = P32(ch, 0x7f);
        while (p && P32(p, 5) != node) p = P32(p, 5);
        SETP32(p, 5, P32(node, 5));
    }
    rtl_freemem(node, 9);
}
static void remove_effect(farptr node, u8 kind, farptr ch) { effect_remove(node, kind, ch); }

// 4592:0160  "Does this effect apply to this character right now?" If so, run the effect's
// routine. The character's own effects always count. Four effects work on an area instead
// (15h Silence 15' Radius, 2Dh and 2Eh the 10' Radius protections, 31h Prayer): for those any
// party member carrying it will do, and in combat only if this character stands within reach:
// six squares for Prayer, one for the others. (The reach test fills a list at DS:6718 and uses
// a 216-byte work area at DS:671B, which is saved and put back.)
void effect_check(u8 kind, farptr ch) {
    static u32 seen[8]; if (getenv("FXDEBUG") && !(seen[kind >> 5] >> (kind & 31) & 1)) { seen[kind >> 5] |= 1u << (kind & 31); fprintf(stderr, "[fx] kind %02x asked\n", kind); }
    farptr node = char_find_effect(kind, ch);
    int found = node != 0;
    if (!found && (kind == 0x15 || kind == 0x2d || kind == 0x2e || kind == 0x31)) {
        u8 saved[0xd8];
        memcpy(saved, &G8(0x671b), sizeof saved);
        for (farptr p = G32(0x5d96); p && !found; p = P32(p, 0x104)) {
            node = char_find_effect(kind, p);
            if (!node) continue;
            if (G8(0x49f3) != 5) { found = 1; continue; }
            u8 x = combat_x(p), y = combat_y(p), size = combat_size(p);
            combat_reach(G32(0x6716), size, 0xff, kind == 0x31 ? 6 : 1, (s8)y, (s8)x);
            u8 n = G8(0x671a), me = combat_slot(ch);
            if (getenv("FXDEBUG")) fprintf(stderr, "[fx] reach kind %02x list %u me %u\n", kind, n, me);
            for (u8 i = 1; i <= n && n != 0; i++) if (G8(0x6718 + i * 3) == me) found = 1;
        }
        memcpy(&G8(0x671b), saved, sizeof saved);
    }
    if (found) effect_handler(0, node, ch, kind);
}

// 4592:0FD2  Take every copy of effect 19h (Invisibility, by the spell table) off a character.
void effects_remove_invisibility(farptr ch) {
    farptr node;
    while ((node = char_find_effect(0x19, ch)) != 0) remove_effect(node, 0x19, ch);
}

// 4592:100A and 1041  Take off the sixteen effects listed at DS:0C14, or the two at DS:0C24.
void effects_remove_list16(farptr ch) { for (int i = 1; i <= 16; i++) remove_effect(0, G8(0x0c13 + i), ch); }
void effects_remove_list2(farptr ch) { for (int i = 1; i <= 2; i++) remove_effect(0, G8(0x0c23 + i), ch); }

// 4592:10E4  A small encoding: 100 + b, except that b = 18 gives a + 1.
u8 rules_encode(u8 a, u8 b) { return b == 0x12 ? (u8)(a + 1) : (u8)(b + 100); }

// 4592:02FA  The hub of the effects system: an event is happening to a character, and each
// effect on the list for that event is asked in turn (effect_check) whether it applies. The
// effects answer by changing globals: the d20 roll at DS:6822, the damage at DS:6818, the
// "cannot be targeted" flag at DS:681E, the movement at DS:681A, and so on. The lists are fixed
// tables in the original's code, twenty events and 140 checks, extracted here mechanically from
// the assembly. What the events are, as far as their callers show:
//   00 choosing a target       01 being chosen as a target    04 attacker's damage
//   05 damage being taken      0A attacker's to-hit roll      0B about to be attacked
//   0D going down              10 target's side of the to-hit roll
//   11 morale                  12 movement and attacks per round
void effects_event(farptr ch, u8 event) {
    static const u8 list[20][17] = {
        /*00*/ {0x7e},
        /*01*/ {0x3f, 0x25, 0x19, 0x47},
        /*02*/ {0x55, 0x56, 0x57, 0x44, 0x4f, 0x50, 0x4c, 0x49, 0x4d},
        /*03*/ {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x4f, 0x55, 0x56, 0x57},
        /*04*/ {0x1d, 0x03, 0x06},
        /*05*/ {0x1c, 0x29, 0x68, 0x78, 0x65, 0x73, 0x74, 0x77, 0x7b, 0x60, 0x5e, 0x3c, 0x7a, 0x75},
        /*06*/ {0x71, 0x3d, 0x7a, 0x3c, 0x5b, 0x0a, 0x14, 0x69, 0x6a, 0x70, 0x72, 0x76, 0x11, 0x5d, 0x65, 0x1c},
        /*07*/ {0x33, 0x34, 0x35, 0x1f},
        /*08*/ {0x63, 0x51, 0x52, 0x59, 0x48, 0x38},
        /*09*/ {0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x7c, 0x7d},
        /*0A*/ {0x01, 0x02, 0x21, 0x24, 0x31, 0x03, 0x06, 0x12, 0x1a},
        /*0B*/ {0x21, 0x11, 0x08, 0x09, 0x2d, 0x2e, 0x1e},
        /*0C*/ {0x08, 0x09, 0x0a, 0x11, 0x14, 0x21, 0x24, 0x2d, 0x2e, 0x31, 0x3d, 0x6f, 0x7d, 0x5a, 0x61},
        /*0D*/ {0x63, 0x64, 0x67, 0x4b, 0x4a},
        /*0E*/ {0x53, 0x54, 0x58, 0x79},
        /*0F*/ {0x15, 0x1e, 0x4a, 0x4b, 0x0b},
        /*10*/ {0x19, 0x47, 0x25, 0x2f, 0x30, 0x59},
        /*11*/ {0x01, 0x02, 0x0b},
        /*12*/ {0x27, 0x2a, 0x3a},
        /*13*/ {0x62, 0x17, 0x48, 0x38, 0x0b},
    };
    if (event > 0x13) return;
    for (int i = 0; i < 17 && list[event][i]; i++) effect_check(list[event][i], ch);
}

// 4592:0E52  Put a status effect on a character: a new 9-byte node (kind, a word of time left,
// a byte, and the "tell the effect when it is removed" byte) goes on the end of their list.
void effect_add(u8 notify, u8 b3, u16 time, u8 kind, farptr ch) {
    farptr node = rtl_getmem(9);
    if (!P32(ch, 0x7f)) SETP32(ch, 0x7f, node);
    else { farptr last = P32(ch, 0x7f); while (P32(last, 5)) last = P32(last, 5); SETP32(last, 5, node); }
    SETP32(node, 5, 0);
    P8(node, 0) = kind; SETP16(node, 1, time); P8(node, 4) = notify; P8(node, 3) = b3;
}

// 4592:0D63  A saving throw. Roll a d20 into DS:6816: a 1 fails and a 20 succeeds whatever
// else is true. Otherwise add the character's bonus from protective items (101h) and `adjust`,
// note which kind of save it is (DS:682A; kind 0 is the one poison uses), let the character's
// effects change the total (event 0Ch), and compare with their target number for that kind
// (five bytes from 6Dh of the record). Returns 1 if the save is made.
u8 rules_saving_throw(s8 adjust, s8 kind, farptr ch) {
    G8(0x6816) = roll_dice(20, 1);
    if (G8(0x6816) == 1) return 0;
    if (G8(0x6816) == 20) return 1;
    G8(0x6816) = (u8)(G8(0x6816) + (s8)P8(ch, 0x101) + adjust);
    G8(0x682a) = (u8)kind;
    effects_event(ch, 0x0c);
    return G8(0x6816) >= P8(ch, 0x6d + kind);
}

// 4592:1344  Damage that is not a weapon blow: spells, breath, poison gas, traps. `amount` goes
// into DS:6818 and the victim's effects get their say (event 6: resistances, immunities). If a
// saving throw applies, `save` says what it achieved: 1 no damage, 2 half. Whatever is left is
// reported ("takes 12 points of damage from Fire"; the source is read from the damage-kind bits
// at DS:6819: 1 Fire, 2 Cold, 4 Electricity, 10h Acid, and 8 or nothing at all Magic) and taken
// off. In a fight a spell being cast is lost. If the victim can no longer act: "Goes Down",
// ", and is Dying", or "is killed", and in a fight the fall is shown.
void rules_damage(u8 save_applies, u8 save, u8 amount, farptr ch) {
    G8(0x6818) = amount;
    effects_event(ch, 6);
    if (save_applies) { if (save == 1) G8(0x6818) = 0; else if (save == 2) G8(0x6818) >>= 1; }
    if (G8(0x6818) == 0) return;
    char line[160]; u8 msg[256];
#define PSTR(t) (memset(msg, 0, sizeof msg), msg[0] = (u8)strlen(t), memcpy(msg + 1, t, msg[0]), msg)
    if (G8(0x6818) == 1) strcpy(line, "takes 1 point of damage "); else snprintf(line, sizeof line, "takes %u points of damage ", G8(0x6818));
    u8 kind = G8(0x6819) & 0xf7;
    if (kind == 1) strcat(line, "from Fire"); else if (kind == 2) strcat(line, "from Cold");
    else if (kind == 4) strcat(line, "from Electricity"); else if (kind == 0x10) strcat(line, "from Acid");
    if ((G8(0x6819) & 8) == G8(0x6819)) strcat(line, "from Magic");
    farptr v = scratch_push(258);
    P8(v, 0) = (u8)strlen(line); memcpy(&P8(v, 1), line, strlen(line));
    call_original(0x10ba, 0x1feb, 5, FP_SEG(ch), FP_OFF(ch), 0, FP_SEG(v), FP_OFF(v));   // show it (still original code)
    scratch_pop(258);
    chr_apply_damage(G8(0x6818), ch);
    if (G8(0x49f3) == 5) {
        farptr act = P32(ch, 0x108);
        P8(act, 1) = 0;
        if (P8(act, 0) != 0) {
            ui_char_message(1, 0x0c, PSTR("lost a spell"), ch);
            chr_forget_spell(P8(P32(ch, 0x108), 0));
            P8(P32(ch, 0x108), 0) = 0;
        }
    }
    if (P8(ch, 0x10d) == 0) {
        strcpy(line, "Goes Down");
        if (P8(ch, 0x10c) == 5) strcat(line, ", and is Dying");
        if (P8(ch, 0x10c) != 5 && P8(ch, 0x10c) != 8) strcpy(line, "is killed");
        ui_char_message(0, (u8)(G8(0x5e25) + 1), PSTR(line), ch);
        if (G8(0x49f3) == 5) {
            effects_remove_list16(ch);
            effects_event(ch, 0x0d);
            if (P8(ch, 0x10d) == 0) combat_fall(ch); else text_pause();
        } else text_pause();
    }
#undef PSTR
    ui_clear_message();
}

// 4592:1869  Get a fallen combatant back on its feet with `hp` hit points. It has to fit back
// on the field where it fell (combat_place); if so it is Okay and able to act again, its square
// is redrawn, and "<name> gets back up", or for the other side "stands up and grins".
u8 rules_get_up(u8 hp, farptr ch) {
    u8 x = combat_x(ch), y = combat_y(ch), msg[256] = {0};
    if (!combat_place(1, y, x, ch)) return 0;
    P8(ch, 0x10c) = 0; P8(ch, 0x10d) = 1; P8(ch, 0x11b) = hp;
    if (G8(0x49f3) == 5) combat_redraw_one(0, 3, ch);
    const char *t = P8(ch, 0x10e) == 1 ? "stands up and grins" : "gets back up";
    msg[0] = (u8)strlen(t); memcpy(msg + 1, t, msg[0]);
    ui_char_message(1, 10, msg, ch);
    chr_count_sides();
    return 1;
}

// 4592:175D  Heal a character by `amount`, if they are Okay, Animated, Unconscious or Dying.
// `natural` healing (rest) applies to anyone below their maximum. Other healing (spells,
// potions) is refused to anyone carrying effect 32h (the Dreaded Mummy Disease) and otherwise
// always applies. Hit points stop at the maximum. Someone who was down stops Dying and becomes
// Unconscious, and outside a fight the "coming round" effect (4Eh) is run at once.
// Returns 1 if anything was done.
u8 rules_heal(u8 natural, u8 amount, farptr ch) {
    u8 st = P8(ch, 0x10c);
    if (!(st == 0 || st == 1 || st == 4 || st == 5)) return 0;
    if (!((P8(ch, 0x11b) < P8(ch, 0x32) && natural) || (!char_has_effect(0x32, ch) && !natural))) return 0;
    P8(ch, 0x11b) += amount;
    if (P8(ch, 0x32) < P8(ch, 0x11b)) P8(ch, 0x11b) = P8(ch, 0x32);
    if (P8(ch, 0x10d) == 0) {
        if (P8(ch, 0x10c) == 5) P8(ch, 0x10c) = 4;
        if (P8(ch, 0x10c) == 4 && G8(0x49f3) != 5) effect_handler(1, 0, ch, 0x4e);
    }
    return 1;
}

// Run one effect's routine directly (the dispatch at 4592:0000).
void effect_run(u8 arg, farptr node, farptr ch, u8 kind) { effect_handler(arg, node, ch, kind); }

// 4592:1656  Put a spell's effect on a character, with its message. The kind is announced (DS:6817)
// and the character's effects consulted (event 9), so that an immunity can cancel it. If it is
// cancelled, or a save was allowed and made, "is Unaffected". Otherwise an effect of the same
// kind already there is replaced when it would run out sooner (or the new one is permanent),
// the new one is added, and `message` (if any) is shown under the name.
void rules_spell_effect(const u8 *message, u8 saved, u8 save_counts, u8 notify, u8 b3, u16 time, u8 kind, farptr ch) {
    u8 msg[41]; u8 n = message[0] > 40 ? 40 : message[0];
    msg[0] = n; memcpy(msg + 1, message + 1, n);
    G8(0x6817) = kind;
    effects_event(ch, 9);
    if (G8(0x6817) == 0 || (saved && save_counts == 1)) {
        u8 m[256] = "\015is Unaffected";
        ui_char_message(1, 10, m, ch);
        return;
    }
    farptr old = char_find_effect(kind, ch);
    if (old && ((P16(old, 1) != 0 && P16(old, 1) < time) || time == 0)) effect_remove(old, kind, ch);
    effect_add(notify, b3, time, kind, ch);
    if (msg[0]) {
        farptr v = scratch_push(42);
        memcpy(&P8(v, 0), msg, (size_t)msg[0] + 1);
        call_original(0x10ba, 0x1feb, 5, FP_SEG(ch), FP_OFF(ch), 1, FP_SEG(v), FP_OFF(v));
        scratch_pop(42);
        ui_clear_message();
    }
}

// 4592:110D  The Strength a Strength or Enlarge effect gives, packed in the node's byte 3: below
// 66h it is 18 with a percentage (value - 1); from 66h on it is value - 100 (19 to 25).
void rules_strength_decode(u8 *pct, u8 *strength, farptr node) {
    *pct = 0;
    *strength = P8(node, 3) & 0x7f;
    if (*strength < 0x66) { *pct = (u8)(*strength - 1); *strength = 0x12; }
    else *strength = (u8)(*strength + 0x9c);
}
