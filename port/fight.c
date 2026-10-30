#include <stdlib.h>
// Combat turn logic (original overlay unit at 26B0), first pieces.
#include <stdio.h>
#include "poolrad.h"
#include "fixes.h"
#include "sheet.h"
#include "rtl.h"
#include "hud.h"
#include "text.h"
#include "chr.h"
#include "combat.h"
#include "rules.h"
#include "fight.h"
#include "gfx.h"
#include "gametime.h"
#include "menu.h"
// The original 10BA:1A29 call order (source x, y, target x, y, frames, delay), mapped to the native routine.
#define combat_missile_anim_args(sx, sy, tx, ty, fr, dl) combat_missile_anim((u8)(dl), (u8)(fr), (s8)(ty), (s8)(tx), (s8)(sy), (s8)(sx))

// 26B0:0F4F  A combatant has nothing more to do this turn. If they still had an action in mind
// (byte 3 of the action record), carry no missile weapon and none of the four listed effects,
// they stand guard; otherwise the action record is simply wiped.
u8 fight_stand_down(farptr ch) {
    ui_clear_message();
    if (!chr_has_listed_effect(ch) && !chr_has_missile_weapon(ch) && P8(P32(ch, 0x108), 3) != 0) return chr_guard(ch);
    return chr_clear_action(ch);
}

// 26B0:0203  A computer-run cleric who has not yet tried this fight (action byte 11h) looks
// for undead to turn (2E2A:1351, still original code) and, finding some, turns them (2E2A:116A).
u8 fight_try_turn_undead(farptr ch) {
    if (P8(P32(ch, 0x108), 0x11) != 0 || (s8)P8(ch, 0x96) <= 0) return 0;
    farptr v = scratch_push(4);
    u8 found = (u8)call_original(0x2e2a, 0x1351, 4, FP_SEG(ch), FP_OFF(ch), FP_SEG(v), FP_OFF(v));
    scratch_pop(4);
    if (!found) return 0;
    call_original(0x2e2a, 0x116a, 2, FP_SEG(ch), FP_OFF(ch));
    return 1;
}

// 26B0:0FC1  While the computer is moving the party's fighters, glance at the keyboard.
// Space takes every player character (not NPCs, not the "animated") off quick-fight, and if
// this one was among them ends its automatic move (action 14h). Alt-M toggles whether the
// computer may cast spells for you ("Magic On" / "Magic Off", DS:6E30). Alt-X calls 2E2A:3B93.
// Returns 1 if this character's automatic move was cut short.
u8 fight_check_keys(farptr ch) {
    u8 cut = 0;
    if (crt_key_pressed()) {
        u8 k = (u8)call_original(0x1899, 0x0059, 0);
        if (k == 0) k = (u8)call_original(0x1899, 0x0059, 0);
        if (k == 0x32) {
            G8(0x6e30) = G8(0x6e30) == 0;
            ui_notice(G8(0x6e30) == 0 ? (const u8 *)"\011Magic Off" : (const u8 *)"\010Magic On");
        } else if (k == ' ') {
            for (farptr p = G32(0x5d96); p; p = P32(p, 0x104)) if (P8(p, 0x84) < 0x80 && P8(p, 0x10c) != 1) P8(p, 0x10f) = 0;
            if (P8(ch, 0x10f) == 0) { P8(P32(ch, 0x108), 3) = 0x14; cut = 1; }
        } else if (k == 0x2d) fight_sting_kill();
    }
    keys_flush();
    return cut;
}

// 26B0:0675  Can the computer step this combatant in a direction? The direction is `base`
// turned by an entry of the detour table at DS:02AE (five per movement style, action byte 15h;
// `attempt` picks which). *blocked is set if that way is off the map or impassable. A step is
// allowed if nobody is in the way and the ground's cost (twice it for a straight step, three
// times for a diagonal) fits in the movement left (action byte 6). Tile 1Eh (a cloud, by the
// look of it) is only entered by those carrying effect 20h, 1Eh, 6Fh or 7Dh.
u8 fight_can_step(u8 *blocked, u8 base, u8 attempt, farptr ch) {
    farptr act = P32(ch, 0x108);
    u8 dir = (u8)((base + G8(0x02ae + P8(act, 0x15) * 5 + attempt)) % 8), tile, who;
    *blocked = 0;
    combat_look_ahead(&tile, &who, dir, ch);
    if (tile == 0) { *blocked = 1; return 0; }
    if (who != 0 || G8(0x2744 + tile * 4) >= P8(act, 6)) return 0;
    u8 cost = (u8)(((dir & 1) ? 3 : 2) * G8(0x2744 + tile * 4));
    if (tile == 0x1e && !char_has_effect(0x20, ch) && !char_has_effect(0x1e, ch) && !char_has_effect(0x6f, ch) && !char_has_effect(0x7d, ch))
        cost = (u8)(P8(act, 6) + 1);
    return cost <= P8(act, 6);
}

// 26B0:03E4  A computer-run combatant considers using a magic item. Only if its action byte 2
// allows, its side still has someone standing and casting is not forbidden here. A seven-sided
// die says how many passes to make over the readied, charged, non-scroll items, asking
// 26B0:02EB (still original code) whether each item's spell is worth using now, with an
// urgency that falls from 7 with each pass. The first item approved is used (3C10:1A8A, still
// original code). The die is rolled whether or not the conditions hold.
u8 fight_try_item(farptr ch) {
    farptr pick = 0;
    s8 urgency = 7;
    u8 rolls = roll_dice(7, 1);
    if (P8(P32(ch, 0x108), 2) != 0 && G8(0x6814 + chr_on_party_side(ch)) != 0 && P16(G32(0x49d2), 0x1ca) == 0 && rolls != 0) {
        for (u8 pass = 1; pass <= rolls; pass++, urgency--) {
            for (farptr item = P32(ch, 0xc8); item && !pick; item = P32(item, 0x2a)) {
                u8 spell = P8(item, 0x3d);
                if (G8(0x5582 + P8(item, 0x2e) * 16) >= 0x0b && G8(0x5582 + P8(item, 0x2e) * 16) <= 0x0d) continue;   // a scroll (41CF:323D)
                if (P8(item, 0x3e) >= 0x80 || P8(item, 0x34) == 0 || spell == 0) continue;
                if (spell > 0x38) spell = (u8)(spell - 0x17);
                if (fight_spell_worth_casting((u8)urgency, spell, ch)) pick = item;
            }
        }
    }
    if (!pick) return 0;
    farptr v = scratch_push(2);
    call_original(0x3c10, 0x1a8a, 4, FP_SEG(pick), FP_OFF(pick), FP_SEG(v), FP_OFF(v));
    scratch_pop(2);
    return 1;
}

// 26B0:0545  A computer-run combatant considers casting. Monsters and NPCs always may; the
// player's own characters only with "Magic On". Up to a seven-sided die's worth of passes,
// three tries a pass: pick one of the memorised spells at random and ask 26B0:02EB whether it is
// worth casting now, with urgency falling from 7. The first approved is cast (2E2A:2414, still
// original code), and whatever that returns is the result.
u8 fight_try_spell(farptr ch) {
    u8 list[21], pick = 0, rolls; u16 n = 0; s8 urgency = 7;
    if (P8(P32(ch, 0x108), 1) != 0) for (int i = 0; i <= 0x14; i++) if (P8(ch, 0x17 + i) != 0) list[n++] = P8(ch, 0x17 + i);
    rolls = roll_dice(7, 1);
    if (n != 0 && (P8(ch, 0x84) > 0x7f || G8(0x6e30) != 0) && G8(0x6814 + chr_on_party_side(ch)) != 0) {
        for (u16 pass = 1; pass <= rolls && !pick; pass++, urgency--)
            for (u8 attempt = 1; attempt < 4 && !pick; attempt++) {
                u8 spell = list[(u8)(roll_dice((u8)n, 1) - 1)];
                if (fight_spell_worth_casting((u8)urgency, spell, ch)) pick = spell;
            }
    }
    if (!pick) return 0;
    farptr v = scratch_push(2);
    call_original(0x2e2a, 0x2414, 4, pick, 1, FP_SEG(v), FP_OFF(v));
    u8 result = P8(v, 0);
    scratch_pop(2);
    return result;
}

// 26B0:128D  How good a weapon looks to the computer: dice times sides, plus 8 per magical plus,
// 2 per point of the type's damage bonus, 2 for each attack beyond the first (types flagged 8),
// and 3 if it leaves a hand free. Type 55h is worth a flat 8 against a target whose byte 76h is
// positive. Worthless (0) if it needs more hands than are free, if it is restricted to another
// alignment, if its byte 3Dh is 53h, or if byte 36h is set (cursed, most likely).
u8 fight_weapon_score(farptr item, farptr ch) {
    const u8 *type = &G8(0x5582 + P8(item, 0x2e) * 16);
    s8 score = (s8)(type[10] * type[9]);
    if ((s8)P8(item, 0x32) > 0) score = (s8)(score + P8(item, 0x32) * 8);
    if ((s8)type[11] > 0) score = (s8)(score + type[11] * 2);
    farptr target = P32(P32(ch, 0x108), 0x0a);
    if (P8(item, 0x2e) == 0x55 && target && (s8)P8(target, 0x76) > 0) score = 8;
    if (type[14] & 8) score = (s8)(score + (type[5] - 1) * 2);
    if (type[1] < 2) score = (s8)(score + 3);
    if ((u16)type[1] + P8(ch, 0x100) > 3) score = 0;
    if (P8(item, 0x3e) == 0x84 && (P8(item, 0x3d) & 0x0f) != P8(ch, 0xa0)) score = 0;
    if (P8(item, 0x3d) == 0x53) score = 0;
    if (P8(item, 0x36) != 0) score = 0;
    return (u8)score;
}

// ---- Attacks (original overlay unit at 2E2A), first pieces ----
// 4592:02FA is the hub of the effects system: "event n is happening to this character; let
// every effect they carry have its say". The effects answer through globals. Still original code.
// (effects_event is now native, in rules.c.)

// 2E2A:0E52  Halve a number, rounding up on odd rounds (DS:6DE3 counts them).
u8 fight_half(u8 v) { if (G8(0x6de3) & 1) v++; return v >> 1; }

// 2E2A:0123  How far a combatant can move this turn, in half-squares: twice their movement
// rate (monsters get the area's adjustment, word 6E4h of the area data, added first), after
// the effects have had their say (event 12h, through DS:681A). Nonsense rates count as 1.
u8 fight_movement(farptr ch) {
    u8 m = P8(ch, 0x11c);
    if (P8(ch, 0x10e) == 0) m = (u8)(m + P16(G32(0x49d6), 0x6e4));
    G8(0x681d) = 1;
    if (m == 0 || m > 0x60) m = 1;
    G8(0x681a) = (u8)(m << 1);
    effects_event(ch, 0x12);
    G8(0x681d) = 0;
    return G8(0x681a);
}

// 2E2A:263D  The compass direction from one combatant to another: the first of the eight
// quarter-circles from `from` that contains `to`.
u8 fight_direction(farptr to, farptr from) {
    s8 sx = (s8)combat_x(from), sy = (s8)combat_y(from), tx = (s8)combat_x(to), ty = (s8)combat_y(to);
    u8 dir = 0;
    while (!combat_in_arc(dir, ty, tx, sy, sx)) dir++;
    return dir;
}

// 2E2A:1085  May `attacker` choose `target`? Always themselves, never nobody. Otherwise the
// target's effects are asked (event 1) and then the attacker's (event 0, with the target put
// in the attacker's action record for the duration); either can veto through DS:681E, which
// is how invisibility and the like work.
u8 fight_can_target(farptr target, farptr attacker) {
    if (!target) return 0;
    if (attacker == target) return 1;
    G8(0x681e) = 0;
    effects_event(target, 1);
    if (G8(0x681e) == 0) {
        farptr act = P32(attacker, 0x108), old = P32(act, 0x0a);
        SETP32(act, 0x0a, target);
        effects_event(attacker, 0);
        SETP32(P32(attacker, 0x108), 0x0a, old);
    }
    return G8(0x681e) == 0;
}

// 2E2A:2574  Is this a backstab? The attacker must have thief levels, wear no armour or leather
// (type 32h), and hold nothing or one of types 7, 8 or 23h-25h (short blades and clubs, by
// the numbers). The target must already have more than one attacker on it this round (action
// byte 0Fh) and be facing directly away: the direction from attacker to target equals the
// target's facing.
u8 fight_is_backstab(farptr target, farptr attacker) {
    farptr weapon = P32(attacker, 0xcc), armour = P32(attacker, 0xd4);
    u8 wt = weapon ? P8(weapon, 0x2e) : 0;
    if (!((s8)P8(attacker, 0x9c) > 0 && (!armour || P8(armour, 0x2e) == 0x32)
          && (!weapon || wt == 7 || wt == 8 || (wt > 0x22 && wt < 0x26)))) return 0;
    farptr tact = P32(target, 0x108);
    return P8(tact, 0x0f) > 1 && fight_direction(target, attacker) == P8(tact, 9);
}

// 2E2A:0627  Guards strike. When a combatant moves, everyone the adjacency scan finds
// (10BA:2433, still original code; it fills the list at DS:6DE4) who is standing guard, and not
// under one of the four listed effects, stops guarding and gets an attack on the mover: the
// view is brought to the mover, then the turn-to-face (2E2A:17F6) and the attack itself
// 2E2A:26B0  Shows a missile flying from attacker to target. Arrows, quarrels, darts, javelins and the like
// (types 9, 15h, 1Ch, 1Fh, 49h) use picture kind 0Dh-10h by direction: a straight one, or for the odd
// (diagonal) directions the second picture of kind 0Eh, mirrored for some. Thrown axes, daggers and hammers
// (types 2, 7, 14h) and two magic items (55h, 56h) spin through four frames; everything else (sling stones,
// magic missiles, type 2Fh with its own picture) alternates two frames. The flight itself (10BA:1A29) is
// still original code.
void fight_missile_flight(farptr item, farptr target, farptr attacker) {
    u8 d = fight_direction(target, attacker), frames = 1, delay = 10, kind = 0x0d;
    u8 t = P8(item, 0x2e);
    if (t == 0x09 || t == 0x15 || t == 0x1c || t == 0x1f || t == 0x49) {
        if (d & 1) {
            if (d == 3 || d == 5) combat_missile_frame(d == 5, 0, 1, (u8)(kind + 1));
            else combat_missile_frame(d == 7, 0, 0, (u8)(kind + 1));
        } else combat_missile_frame(0, 0, (u8)(d >> 2), (u8)(kind + d % 4));
    } else if (t == 0x02 || t == 0x07 || t == 0x14) {
        combat_missile_spin((u8)(kind + 3)); frames = 4; delay = 0x32;
    } else if (t == 0x55 || t == 0x56) {
        combat_missile_spin((u8)(kind + 4)); frames = 4; delay = 0x32;
    } else {
        if (t == 0x2f) kind++;
        combat_missile_frame(0, 0, 0, (u8)(kind + 7));
        combat_missile_frame(0, 1, 1, (u8)(kind + 7));
        frames = 2; delay = 0x14;
    }
    sound_music(0);
    sound_play(G16(0x2620));
    combat_missile_anim_args(combat_x(attacker), combat_y(attacker), combat_x(target), combat_y(target), frames, delay);
    sound_play(G16(0x262c));
    sound_music(10);
}

// (2E2A:1884), both still original code. It stops once the mover is no longer able to act.
void fight_guards_strike(farptr mover) {
    u8 n = combat_enemies_in_reach(1, mover);
    for (u8 i = 1; i <= n && n != 0; i++) {
        if (P8(mover, 0x10d) == 0) continue;
        farptr guard = G32(0x65b9 + G8(0x6de4 + i) * 4);
        if (P8(P32(guard, 0x108), 7) == 0 || chr_has_listed_effect(guard)) continue;
        combat_step_view(8, 2, (s8)combat_y(mover), (s8)combat_x(mover));
        P8(P32(guard, 0x108), 7) = 0;
        fight_note_attacker(mover, guard);
        u8 done;
        fight_strike(&done, 0, 0, mover, guard);
    }
}

void fight_strike(u8 *done, farptr ammo, u8 behind, farptr target, farptr attacker);
u8 fight_hit_roll(u8 ac, farptr target, farptr attacker);
void fight_range_penalty(u8 *ac, farptr target, farptr attacker);
void fight_roll_damage(u8 k, farptr target, farptr attacker);

u8 fight_spell_worth_casting(u8 urgency, u8 spell, farptr ch);

// 2E2A:17F6  Note that `attacker` is going for `target`: one more attacker on the target this
// round (action byte 0Fh), and add how far round from the target's front the attacker stands
// (0 ahead to 4 directly behind) into the target's "surroundedness" (action byte 12h, mod 8).
void fight_note_attacker(farptr target, farptr attacker) {
    farptr tact = P32(target, 0x108);
    P8(tact, 0x0f)++;
    u8 off = (u8)(((int)fight_direction(attacker, target) - P8(tact, 9) + 8) % 8);
    if (off > 4) off = (u8)(8 - off);
    P8(tact, 0x12) = (u8)((P8(tact, 0x12) + off) % 8);
}

// 2E2A:02F9  Report an attack and apply its damage. In the message panel: the attacker's name
// with "Attacks", "-Backstabs-" (kind 2) or "slays helpless" (kind 3); the target's name; then
// "Hitting for N point(s) of damage" (prefixed "(from behind) " for kind 1), "with one cruel
// blow" for kind 3, or "and Misses" when `hit` is 0. `shown` is the number in the message and
// `damage` what is actually taken off (10BA:2228, still original code). Damage breaks the
// target's concentration: a spell being cast (action byte 0) is lost, with a message. If the
// target can no longer act: "goes down", "and is Dying" (status 5) or "is killed" (status 6, 7
// or 8), its sixteen listed effects are removed, its effects are told (event 0Dh), and the
// fall is shown (combat_fall).
// (The lost spell is removed with the routine that works on the *selected* character, not
// necessarily the target; that is how the original does it.)
void fight_deal_damage(u8 hit, u8 shown, u8 damage, u8 kind, farptr target, farptr attacker) {
    u8 msg[256] = {0}, row = 0x0c; char line[200];
#define PSTR(t) (msg[0] = (u8)strlen(t), memcpy(msg + 1, t, msg[0]), msg)
    ui_char_message(0, 10, PSTR(kind == 2 ? "-Backstabs-" : kind == 3 ? "slays helpless" : "Attacks"), attacker);
    hud_draw_name(0, row, 0x17, target);
    row++;
    strcpy(line, kind == 1 ? "(from behind) " : "");
    if (hit) {
        if (kind == 3) strcpy(line, "with one cruel blow");
        else snprintf(line + strlen(line), 100, "Hitting for %u%sof damage", shown, shown == 1 ? " point " : " points ");
        line[0x50] = 0;
        chr_apply_damage(damage, target);
    } else { strcat(line, "and Misses"); line[0x50] = 0; }
    text_print_wrapped_pstr(PSTR(line), 1, 10, (u8)(row + 3), 0x26, row, 0x17);
    row = (u8)(G8(0x5e25) + 1);
    farptr tact = P32(target, 0x108);
    if (damage > 0) {
        P8(tact, 1) = 0;
        if (P8(tact, 0) > 0) {
            text_pause();
            ui_char_message(1, 0x0c, PSTR("lost a spell"), target);
            chr_forget_spell(P8(P32(target, 0x108), 0));
            P8(P32(target, 0x108), 0) = 0;
        } else text_pause();
    } else text_pause();
    if (P8(target, 0x10d) == 0) {
        ui_char_message(0, row, PSTR("goes down"), target);
        row += 2;
        if (P8(target, 0x10c) == 5) { u8 t[] = "\014and is Dying"; text_put_pstr(t, 10, row, 0x17); }
        if (P8(target, 0x10c) >= 6 && P8(target, 0x10c) <= 8) ui_char_message(0, row, PSTR("is killed"), target);
        row += 2;
        effects_remove_list16(target);
        effects_event(target, 0x0d);
        if (P8(target, 0x10d) == 0) combat_fall(target); else text_pause();
    }
#undef PSTR
    ui_clear_message();
}
static void deal_damage(u8 show, u8 a, u16 b, u8 kind, farptr target, farptr attacker) { fight_deal_damage(show, a, (u8)b, kind, target, attacker); }

// 2E2A:1406  One combatant attacks another.
//  - A helpless target (one of the four listed effects) is not rolled against: it takes its
//    hit points plus five, and the attacker's remaining attacks this round are spent.
//  - Against a large target (byte 6Ch) the weapon's large-size damage dice and bonus are used.
//  - The target's numbers are worked out afresh and its effects told (event 0Bh). The armour
//    class to beat is the ordinary one, or the rear one (no shield, no Dexterity) when `behind`
//    is passed in or the target has several attackers on it, this one stands directly behind
//    and its "surroundedness" is over 4. A backstab uses the rear class with two more off.
//    2E2A:1B1E then adjusts it (still original code).
//  - Then every attack the attacker has left (two kinds, counts at 113h and 114h, the second
//    kind first) is rolled with 4592:0CB7. A hit plays the hit sound, works out the damage
//    (2E2A:0191) and deals it; if the target is still up the attacker's effects are told
//    (event = attack kind + 1), and if it is down the attacking stops.
//  - If nothing hit, the miss sound and a "miss" display.
// *done comes back 1 if the attacker has no attacks left (and its action record is then wiped).
void fight_attack(u8 *done, u8 behind, farptr target, farptr attacker) {
    farptr aact = P32(attacker, 0x108);
    u8 hit_any = 0, stop = 0, ac, kind;
    *done = 0;
    G8(0x6823) = 0; G8(0x6824) = 0; G8(0x6e2d) = 0; G8(0x6e2e) = 0; G8(0x6818) = 0;
    P8(aact, 8) = 1;
    if (chr_has_listed_effect(target)) {
        sound_play(G16(0x2622));
        while (P8(attacker, 0x112 + P8(aact, 4)) == 0) P8(aact, 4)--;
        G8(0x6e2c + P8(aact, 4))++;
        deal_damage(1, 1, (u16)(P8(target, 0x11b) + 5), 3, target, attacker);
        effects_remove_invisibility(attacker);
        P8(attacker, 0x113) = 0; P8(attacker, 0x114) = 0;
        *done = chr_clear_action(attacker);
        return;
    }
    farptr weapon = P32(attacker, 0xcc);
    if (weapon && (P8(target, 0x6c) > 0x80 || (P8(target, 0x6c) & 7) > 1)) {
        const u8 *type = &G8(0x5582 + P8(weapon, 0x2e) * 16);
        P8(attacker, 0x115) = type[2];
        P8(attacker, 0x117) = type[3];
        P8(attacker, 0x119) = (u8)((s8)P8(attacker, 0x119) - (s8)type[11] + (s8)type[4]);
    }
    call_original(0x10ba, 0x0bb8, 2, FP_SEG(target), FP_OFF(target));
    effects_event(target, 0x0b);
    farptr tact = P32(target, 0x108);
    if (fight_is_backstab(target, attacker)) ac = (u8)(P8(target, 0x112) - 2);
    else {
        if (P8(tact, 0x0f) > 1 && fight_direction(target, attacker) == P8(tact, 9) && P8(tact, 0x12) > 4) behind = 1;
        ac = P8(target, 0x111 + (behind ? 1 : 0));
    }
    fight_range_penalty(&ac, target, attacker);
    kind = behind ? 1 : 0;
    if (fight_is_backstab(target, attacker)) kind = 2;
    for (u8 k = P8(aact, 4); k >= 1; k--) {
        while (P8(attacker, 0x112 + k) > 0 && !stop) {
            P8(attacker, 0x112 + k)--;
            P8(P32(attacker, 0x108), 4) = k;
            G8(0x6e2c + k)++;
            if (!fight_hit_roll(ac, target, attacker) && !chr_has_listed_effect(target)) continue;
            G8(0x6822 + k)++;
            sound_play(G16(0x2622));
            hit_any = 1;
            fight_roll_damage(k, target, attacker);
            deal_damage(1, G8(0x6818), G8(0x6818), kind, target, attacker);
            if (P8(target, 0x10d) != 0) effects_event(attacker, (u8)(k + 1));
            if (P8(target, 0x10d) == 0) stop = 1;
        }
    }
    if (!hit_any) { sound_play(G16(0x2626)); deal_damage(0, 0, 0, kind, target, attacker); }
    *done = 1;
    for (u8 k = 1; k <= 2; k++) if (P8(attacker, 0x112 + k) > 0) *done = 0;
    P8(P32(attacker, 0x108), 5) = 0;
    if (*done) *done = chr_clear_action(attacker);
}

// 4592:0C4F  A hit with no attacker (a trap or the script's DAMAGE): a d20 kept at DS:6822, where 1 misses and 20
// becomes 100; the target's effects (event 10h) may change it; then it hits if roll + bonus is above the target's
// armour figure (+111h), unless the roll has gone negative.
u8 rules_hit_unaimed(u8 bonus, farptr ch) {
    G8(0x6822) = roll_dice(0x14, 1);
    if ((s8)G8(0x6822) <= 1) return 0;
    if (G8(0x6822) == 0x14) G8(0x6822) = 0x64;
    effects_event(ch, 0x10);
    if ((s8)G8(0x6822) < 0) return 0;
    return (s16)((s8)G8(0x6822) + bonus) > (s16)P8(ch, 0x111);
}

// 4592:0CB7  The to-hit roll. The attacker loses any invisibility by attacking. Roll a d20
// (kept at DS:6822): a 1 misses outright, a 20 becomes 100. The attacker's effects (event 0Ah)
// and the target's (event 10h) may then change the roll. It hits if the roll, plus the
// attacker's to-hit number (110h), plus the area's adjustment for that side (bytes 6E0h for
// the enemy, 6E2h for the party, of the area data), reaches the armour-class figure.
u8 fight_hit_roll(u8 ac, farptr target, farptr attacker) {
    effects_remove_invisibility(attacker);
    G8(0x6822) = roll_dice(20, 1);
    if ((s8)G8(0x6822) <= 1) return 0;
    if (G8(0x6822) == 20) G8(0x6822) = 100;
    effects_event(attacker, 0x0a);
    effects_event(target, 0x10);
    s8 adjust = (s8)P8(G32(0x49d6), P8(attacker, 0x10e) == 0 ? 0x6e2 : 0x6e0);
    return (s8)G8(0x6822) > -1 && (int)ac <= (int)(s8)G8(0x6822) + P8(attacker, 0x110) + adjust;
}

// 2E2A:1B1E  Range. A missile weapon's range (item table byte 0Ch, less one) divided by three
// is its short range; a target beyond that is 2 harder to hit, and beyond twice that 5 harder.
// For anything else the "short range" is the distance itself, so nothing is added.
void fight_range_penalty(u8 *ac, farptr target, farptr attacker) {
    s8 dist = (s8)combat_distance(target, attacker);
    s8 band = dist;
    if (chr_has_missile_weapon(attacker)) band = (s8)((G8(0x558e + P8(P32(attacker, 0xcc), 0x2e) * 16) - 1) / 3);
    if (dist > band) { dist = (s8)(dist - band); *ac += 2; }
    if (dist > band) *ac += 3;
}

// 2E2A:0191  The damage of attack `k` (1 or 2): its dice (count at 114h + k, sides at 116h + k)
// plus its bonus (118h + k), left at DS:6818. A backstab multiplies it by 2 plus a quarter of
// the thief's level. Then the attacker's effects (event 4) and the target's (event 5) may
// change it.
void fight_roll_damage(u8 k, farptr target, farptr attacker) {
    G8(0x6818) = (u8)(P8(attacker, 0x118 + k) + roll_dice_noted(P8(attacker, 0x116 + k), P8(attacker, 0x114 + k)));
    if (fight_is_backstab(target, attacker)) G8(0x6818) = (u8)(G8(0x6818) * (u8)(((u16)(s16)(s8)P8(attacker, 0x9c) >> 2) + 2));
    G8(0x6819) = 0;
    effects_event(attacker, 4);
    effects_event(target, 5);
}

// 2E2A:0D23  How many attacks a combatant gets this round. Attack rates are kept in halves:
// the base rate (A1h) is 2 for one attack a round and 3 for "three attacks every two rounds",
// and fight_half rounds the odd half up on odd rounds. A missile weapon with ammunition
// (10BA:2E8B finds it; still original code) uses its own rate of fire (item table byte 5, at
// least 2) and can fire no more shots than there is ammunition. The effects get a say first
// (event 12h again, through DS:681A). The count already set (113h) is only replaced if the
// combatant has not attacked yet this round, or the new number is lower, or it is less than
// double the old and no missile weapon is involved.
void fight_set_attacks(farptr ch) {
    u8 old = P8(ch, 0x113), missile = 0, n;
    farptr ammo = 0;
    P8(ch, 0x113) = P8(ch, 0xa1);
    if (chr_has_missile_weapon(ch)) {
        farptr v = scratch_push(4);
        missile = (u8)call_original(0x10ba, 0x2e8b, 4, FP_SEG(ch), FP_OFF(ch), FP_SEG(v), FP_OFF(v)) != 0;
        ammo = P32(v, 0);
        scratch_pop(4);
    }
    if (missile) { u8 rate = G8(0x5587 + P8(P32(ch, 0xcc), 0x2e) * 16); G8(0x681a) = rate < 2 ? 2 : rate; }
    else G8(0x681a) = P8(ch, 0x113);
    G8(0x681d) = 0;
    effects_event(ch, 0x12);
    n = fight_half(G8(0x681a));
    if (missile && ammo) { u8 shots = P8(ammo, 0x39) > 1 ? P8(ammo, 0x39) : 1; if (shots < n) n = shots; }
    u8 attacked = P8(P32(ch, 0x108), 8);
    if (attacked == 0 || n < old || (n < old * 2 && !missile)) P8(ch, 0x113) = n;
}

// 2E2A:1884  One combatant strikes at another: the whole business around fight_attack.
//  - The target turns to face the attacker, unless `behind` is set or three or more are already
//    on it (which is what lets a crowd get at someone's back).
//  - The attacker turns to face the target (10BA:09E3 first; still original code), and the
//    target is noted in its action record. A short pause.
//  - A missile is shown flying from attacker to target (2E2A:26B0, still original code), as
//    is a weapon of type 2Fh. (The decompiler labels this call 2090:0050 with one argument;
//    the assembly shows a near call to 26B0 with three far pointers.)
//  - If the attacker has attacks left, fight_attack runs, with the attacker made the selected
//    character for the duration. The ammunition used (DS:6E2D) comes off the stack; when the
//    stack is gone the item is removed (10BA:1509, still original code), but for weapons with
//    both of flag bits 14h a copy is first put on the list at DS:6810, so thrown weapons can
//    be picked up after the fight. Items whose byte 3Eh is 89h are never kept.
//  - The attacker's numbers are worked out again, and its figure redrawn.
void fight_strike(u8 *done, farptr ammo, u8 behind, farptr target, farptr attacker) {
    farptr tact = P32(target, 0x108), aact = P32(attacker, 0x108);
    if (P8(tact, 0x0f) < 3 && !behind) {
        u8 d = fight_direction(attacker, target);
        if (!combat_visible(0, target)) P8(tact, 9) = d; else combat_face(0, 0, d, target);
    }
    u8 d = fight_direction(target, attacker);
    call_original(0x10ba, 0x09e3, 2, FP_SEG(attacker), FP_OFF(attacker));
    combat_face(0, 1, d, attacker);
    SETP32(P32(attacker, 0x108), 0x0a, target);
    rtl_delay(100);
    if (ammo) fight_missile_flight(ammo, target, attacker);
    farptr weapon = P32(attacker, 0xcc);
    if (P8(weapon, 0x2e) == 0x2f) fight_missile_flight(weapon, target, attacker);
    *done = 1;
    if (P8(attacker, 0x113) != 0 || P8(attacker, 0x114) != 0) {
        farptr selected = G32(0x5d92);
        SETG32(0x5d92, attacker);
        fight_attack(done, behind, target, attacker);
        if (ammo) {
            if (P8(ammo, 0x39) != 0) P8(ammo, 0x39) -= G8(0x6e2d);
            if (P8(ammo, 0x39) == 0) {
                if (chr_has_missile_weapon_14(attacker) && P8(ammo, 0x3e) != 0x89) {
                    farptr copy = rtl_getmem(0x3f);
                    memcpy(&P8(copy, 0), &P8(ammo, 0), 0x3f);
                    SETP32(copy, 0x2a, G32(0x6810));
                    SETG32(0x6810, copy);
                    P8(copy, 0x34) = 0;
                    call_original(0x10ba, 0x1509, 4, FP_SEG(attacker), FP_OFF(attacker), FP_SEG(ammo), FP_OFF(ammo));
                    SETG32(0x5d9a, copy);
                } else call_original(0x10ba, 0x1509, 4, FP_SEG(attacker), FP_OFF(attacker), FP_SEG(ammo), FP_OFF(ammo));
            }
        }
        call_original(0x10ba, 0x0bb8, 2, FP_SEG(attacker), FP_OFF(attacker));
        SETG32(0x5d92, selected);
    }
    if (*done) *done = chr_clear_action(attacker);
    (void)aact;
    if (combat_visible(0, attacker)) {
        u8 facing = P8(P32(attacker, 0x108), 9);
        combat_face(1, 1, facing, attacker);
        combat_face(0, 0, P8(P32(attacker, 0x108), 9), attacker);
    }
}

// 41CF:0729  A spell's range in squares: a base plus so much per level of the caster (bytes 2
// and 3 of its entry in the spell table at DS:31FA), or as if cast at level 6 from a scroll
// (DS:6DBF). Spells marked as reaching (byte 6) never have range 0, and 255 counts as 1.
u8 spell_range(u8 spell) {
    u8 r;
    if (G8(0x6dbf) == 0) r = (u8)(G8(0x31fc + spell * 16) + G8(0x31fd + spell * 16) * (u8)call_original(0x10ba, 0x26c2, 1, spell));
    else r = (u8)(G8(0x31fd + spell * 16) * 6 + G8(0x31fc + spell * 16));
    if (r == 0 && G8(0x3200 + spell * 16) != 0) r = 1;
    if (r == 0xff) r = 1;
    return r;
}

// 26B0:0255 (nested in the next routine)  Would an area spell centred on (x, y) catch any of the
// caster's own side? Its radius is byte 0Fh of the spell's entry.
static u8 area_hits_friends(u8 spell, s8 y, s8 x) {
    combat_reach(G32(0x6716), 1, 0xff, G8(0x3209 + spell * 16), y, x);
    u8 n = G8(0x671a), friend = 0;
    for (u8 i = 1; i <= n && n != 0; i++)
        if (P8(G32(0x65b9 + G8(0x6718 + i * 3) * 4), 0x10e) != chr_on_party_side(G32(0x5d92))) friend = 1;
    return friend;
}

// 26B0:02EB  The computer's judgement of whether a spell is worth casting now. A spell is
// considered only if its priority (byte 0Dh of its entry) is at least `urgency`. Cure Light
// Wounds (spell 3) is cast if someone near needs it (2E2A:1BF8, still original code, which also
// looks at fallen allies lying on adjacent squares). Spells with no target flag (byte 0Eh) are
// always acceptable. Others need an enemy within the spell's range; and an area spell (radius
// byte 0Fh) is refused if, centred on any of those enemies, it would also catch one of the
// caster's own side.
u8 fight_spell_worth_casting(u8 urgency, u8 spell, farptr ch) {
    if (G8(0x3207 + spell * 16) < urgency) return 0;
    if (spell == 3 || G8(0x3208 + spell * 16) != 0) {
        if (spell == 3) {
            if (fight_heal_target(ch)) return 1;
        }
        u8 count = combat_enemies_in_reach(spell_range(spell), ch);
        if (count == 0) return 0;
        if (G8(0x3209 + spell * 16) == 0) return 1;
        for (u8 i = 1; i <= count; i++) {
            u8 slot = G8(0x6de4 + i);
            if (area_hits_friends(spell, (s8)G8(0x5f28 + slot * 4), (s8)G8(0x5f27 + slot * 4))) return 0;
        }
        return 1;
    }
    return 1;
}

// 4592:0F06  Takes a combatant out of the fight (fled, surrendered...): announces it, sets its status,
// clears its square and redraws the field.
void chr_take_out(farptr ch, u8 status, const u8 *msg) {
    if (P8(ch, 0x10d) == 0) return;
    u8 m[41]; u8 n = msg[0] > 40 ? 40 : msg[0]; m[0] = n; memcpy(m + 1, msg + 1, n);
    u8 slot = combat_slot(ch);
    combat_redraw_one(0, 3, ch);
    ui_char_message(1, 10, m, ch);
    P8(ch, 0x10d) = 0;
    P8(ch, 0x10c) = status;
    if (status != 3) P8(ch, 0x11b) = 0;
    combat_draw_ground(slot, 0, 0);
    gfx_flush_back_buffer();
    G8(0x5f2a + slot * 4) = 0;
    combat_place_all();
    if (chr_clear_action(ch)) effects_remove_list16(ch);
}

// 2E2A:2908  Half the movement of the fastest combatant who can act on the side chr_on_party_side names.
u8 fight_fastest_pursuer(farptr ch) {
    u8 best = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104))
        if (chr_on_party_side(ch) == P8(c, 0x10e) && P8(c, 0x10d)) {
            u8 v = fight_movement(c) >> 1;
            if (best < v) best = v;
        }
    return best;
}

// 26B0:10D7  Morale for a computer-run combatant. A creature already marked to flee is announced as
// forced to flee. Otherwise only creatures with a morale value (byte 84h bit 7) check: morale is
// (value & 7Fh) * 2 (over 66h counts as 0), passed through effects event 11h, and fails once it is
// below the percentage of hit points lost. Then the side's morale (DS:6E2F, also adjusted) is tested
// against 100 minus the area's figure. A failing creature flees if it is at least twice as fast as
// the fastest pursuer; otherwise, if it is clever enough (byte 11h above 5), it surrenders.
u8 fight_morale(farptr ch) {
    static const u8 flee[] = "\x11" "is forced to flee", surrender[] = "\x0a" "Surrenders";
    u8 r = 0; farptr act = P32(ch, 0x108);
    P8(act, 0x14) = 0;
    effects_remove_list2(ch);
    if (P8(act, 0x10)) { P8(act, 0x14) = 1; ui_char_message(1, 10, flee, ch); return 0; }
    if (P8(ch, 0x84) <= 0x7f) return 0;
    G8(0x6825) = (u8)((P8(ch, 0x84) & 0x7f) << 1);
    if (G8(0x6825) > 0x66) G8(0x6825) = 0;
    effects_event(ch, 0x11);
    int lost = 100 - (int)(s16)((u16)P8(ch, 0x11b) * 100 / P8(ch, 0x32));
    if (!((int)G8(0x6825) < lost || G8(0x6825) == 0)) return 0;
    G8(0x6825) = G8(0x6e2f);
    effects_event(ch, 0x11);
    if (!(G8(0x6825) < (u16)(100 - P16(G32(0x49d6), 0x58c)) || G8(0x6825) == 0 || P8(ch, 0x10e) == 0)) return 0;
    u8 pursuer = fight_fastest_pursuer(ch);
    if (pursuer > (fight_movement(ch) >> 1)) {
        if (P8(ch, 0x11) > 5) { chr_take_out(ch, 4, surrender); r = chr_clear_action(ch); }
    } else {
        P8(act, 0x14) = 1;
        effect_remove(0, 0x4a, ch);
        effect_remove(0, 0x4b, ch);
    }
    return r;
}

// 2E2A:0712  Moves a combatant one square in direction dir (0-7; diagonals, the odd ones, cost 3 half-moves,
// straight steps 2). Quick-fighting combatants scroll the view to follow when they leave it. Moving
// away gives guarding enemies their free strike; if that knocks the mover out, or it is held, its
// movement ends.
void fight_step(u8 dir, farptr ch) {
    u8 cost = (dir & 1) ? 3 : 2;
    farptr act = P32(ch, 0x108);
    if (cost > P8(act, 6)) { P8(act, 6) = 0; return; }
    P8(act, 6) -= cost;
    u8 slot = combat_slot(ch);
    s8 ox = (s8)G8(0x5f27 + slot * 4), oy = (s8)G8(0x5f28 + slot * 4);
    s8 nx = (s8)(ox + (s8)G8(0x2736 + dir)), ny = (s8)(oy + (s8)G8(0x273f + dir));
    u8 how = 1;
    if (P8(ch, 0x10f)) {
        how = 3;
        farptr m = G32(0x6716);
        if (!combat_in_view((s8)(ny - (s8)P8(m, 3)), (s8)(nx - (s8)P8(m, 2)))) combat_step_view(8, 2, oy, ox);
    }
    combat_draw_ground(slot, 0, 0);
    G8(0x5f27 + slot * 4) = (u8)nx;
    G8(0x5f28 + slot * 4) = (u8)ny;
    combat_place_all();
    combat_step_view(8, how, ny, nx);
    P8(act, 0x0f) = 0;
    P8(act, 0x12) = 0;
    sound_play(G16(0x2628));
    fight_guards_strike(ch);
    if (P8(ch, 0x10d) == 0 || chr_has_listed_effect(ch)) P8(act, 6) = 0;
}

// 2E2A:0C6A  A combatant at the edge tries to leave the battlefield. With no enemy in reach it always
// gets away; otherwise it must be faster than the fastest enemy (half movement against half movement),
// and on a tie it is a coin toss.
u8 fight_escape(farptr ch) {
    static const u8 away[] = "\x08" "Got Away", blocked[] = "\x11" "Escape is blocked";
    u8 ok = 0;
    if (combat_enemies_in_reach(0xff, ch) == 0) ok = 1;
    else {
        u8 own = fight_movement(ch) >> 1, chaser = fight_fastest_pursuer(ch);
        if (chaser < own) ok = 1;
        else if (chaser == own && roll_dice(2, 1) == 1) ok = 1;
    }
    if (ok) chr_take_out(ch, 3, away); else ui_notice(blocked);
    return chr_clear_action(ch);
}

// 4592:0AE7  After a step: a combatant standing in a Stinking Cloud square (tile 1Eh) who is not already
// coughing (1Eh) or nauseous (1Fh) and has none of the protections 20h, 6Fh, 7Dh makes a saving throw.
// Made: it coughs for a round. Failed: it is helpless with nausea for 1d4+1 rounds.
void fight_cloud_check(farptr ch) {
    static const u8 cough[] = "\x0f" "starts to cough", nausea[] = "\x1b" "chokes and gags from nausea";
    u8 tile, who;
    if (P8(ch, 0x10d) == 0) return;
    combat_look_ahead(&tile, &who, 8, ch);
    if (tile != 0x1e) return;
    static const u8 guard[] = { 0x1e, 0x1f, 0x20, 0x6f, 0x7d };
    farptr node = 0;
    for (int i = 0; i < 5; i++) if ((node = char_find_effect(guard[i], ch)) != 0) return;
    u8 saved = rules_saving_throw(0, 0, ch);
    if (saved) {
        rules_spell_effect(cough, saved, 0, 0, 0xff, 1, 0x1e, ch);
        effect_run(0, node, ch, 0x1e);
    } else {
        rules_spell_effect(nausea, saved, 0, 0, 0xff, (u16)(roll_dice(4, 1) + 1), 0x1f, ch);
        effect_run(0, node, ch, 0x1f);
    }
}

// 2E2A:37E4  Picks the computer's target. A current target is kept unless renew is set or it has become
// unsuitable (same side, out of the fight, or no longer attackable). Otherwise up to 20 random picks are
// tried among the enemies within range, crossing off each one that cannot be attacked. If none is
// found, a second round is made with the battlefield's "any target" flag (map byte 6) raised, which
// with relaxed set accepts any enemy outright. Returns whether a target was found.
u8 fight_pick_target(u8 renew, u8 relaxed, u8 range, farptr ch) {
    farptr act = P32(ch, 0x108), t = P32(act, 0x0a);
    if (renew || (t && (P8(t, 0x10e) == P8(ch, 0x10e) || P8(t, 0x10d) == 0 || !fight_can_target(t, ch))))
        SETP32(act, 0x0a, 0);
    u8 found = P32(act, 0x0a) != 0, done = 0, second = 0;
    while (!found && !done) {
        done = second;
        if (second && !renew) P8(G32(0x6716), 6) = 1;
        u8 tries = 20, n = combat_enemies_in_reach(range, ch);
        while (tries && !found && n) {
            tries--;
            u8 k = roll_dice(n, 1);
            if (G8(0x6de4 + k) == 0) continue;
            farptr c = G32(0x65b9 + G8(0x6de4 + k) * 4);
            if ((relaxed && P8(G32(0x6716), 6)) || fight_can_target(c, ch)) { found = 1; SETP32(act, 0x0a, c); continue; }
            G8(0x6de4 + k) = 0;
            for (int i = 1; i <= n; i++) if (G8(0x6de4 + i)) found = 1;
            if (found) found = 0; else n = 0;
        }
        if (!second) second = 1;
    }
    P8(G32(0x6716), 6) = 0;
    return found;
}

// 2E2A:08B2  Before a combatant steps in direction dir: every enemy next to it now that will not be next to
// it after the step gets a free strike, unless that enemy is held (listed effect), cannot attack it,
// or is under effect 4Ah/4Bh. An enemy that is busy (no action pending, but already engaged) must also
// have the mover within its arc, checked over five directions around its facing. The strike uses
// whichever attack it still has left (second if any is left there), as if from behind.
void fight_free_attacks(u8 dir, farptr ch) {
    u8 list[12] = {0};
    u8 slot = combat_slot(ch);
    u8 n = combat_enemies_in_reach(1, ch);
    if (n == 0) return;
    for (int i = 1; i <= n; i++) list[i] = G8(0x6de4 + i);
    G8(0x5f27 + slot * 4) += G8(0x2736 + dir);
    G8(0x5f28 + slot * 4) += G8(0x273f + dir);
    u8 after = combat_enemies_in_reach(1, ch);
    G8(0x5f27 + slot * 4) -= G8(0x2736 + dir);
    G8(0x5f28 + slot * 4) -= G8(0x273f + dir);
    for (int i = 1; i <= n; i++) {
        u8 still = 0;
        for (int j = 1; j <= after; j++) if (G8(0x6de4 + j) == list[i]) still = 1;
        if (still) list[i] = 0;
    }
    for (int i = 1; i <= n; i++) {
        if (list[i] == 0 || P8(ch, 0x10d) == 0) continue;
        u8 struck = 0;
        farptr e = G32(0x65b9 + list[i] * 4);
        if (chr_has_listed_effect(e)) continue;
        if (!fight_can_target(ch, e)) continue;
        if (char_find_effect(0x4b, e) || char_find_effect(0x4a, e)) continue;
        farptr ea = P32(e, 0x108);
        u8 last = (u8)(P8(ea, 9) + 10), d = (u8)(P8(ea, 9) + 6);
        if (d > last) continue;
        for (;; d++) {
            if (!struck) {
                if ((s8)P8(ea, 3) > 0 || P8(ea, 0x0f) == 0 ||
                    combat_in_arc(d % 8, (s8)combat_y(ch), (s8)combat_x(ch), (s8)combat_y(e), (s8)combat_x(e))) {
                    u8 k = P8(e, 0xa1) ? 1 : 2;
                    for (int j = 1; j <= 2; j++) if (P8(e, 0x112 + j)) k = (u8)j;
                    if (P8(e, 0x112 + k) == 0) P8(e, 0x112 + k) = 1;
                    P8(ea, 4) = k;
                    farptr keep = P32(ea, 0x0a);
                    fight_strike(&G8(0x6e31), 0, 1, ch, e);
                    struck = 1;
                    SETP32(ea, 0x0a, keep);
                    if (P8(ch, 0x10d)) hud_combat_panel(ch);
                }
            }
            if (d == last) break;
        }
    }
}

// 26B0:07F2  One step of a computer-run combatant's movement. (It first prints a leftover debugging line,
// "Move/Attack, Move Left = n", on the bottom row, which the game draws over at once.) A combatant out of
// movement, out of actions, or one that is not fleeing and has class byte 5 stands down. Otherwise it
// heads for its target, or, when fleeing, picks one of two directions away from the party's side of the
// field. Up to five alternatives from a table (DS:2AE, five per pattern in byte 15h) are tried in turn;
// a fleeing combatant blocked at the edge tries to escape. Turning straight back the way it came (DS:443C)
// or finding no way counts as being stuck (DS:443D): the second time it drops its target and picks
// another, the third time it stops.
void fight_ai_move(farptr ch) {
    farptr act = P32(ch, 0x108);
    {
        static const char head[] = "Move/Attack, Move Left = ";
        farptr buf = scratch_push(256);
        chr_number_text(buf, P8(act, 6) >> 1);
        u8 num[256]; memcpy(num, &mem[LIN(buf)], 256);
        u8 *s = &mem[LIN(buf)]; int n = (int)strlen(head);
        memcpy(s + 1, head, n);
        int m = num[0]; if (n + m > 255) m = 255 - n;
        memcpy(s + 1 + n, num + 1, m); n += m;
        if (n < 255) s[1 + n++] = ' ';
        s[0] = (u8)n;
        text_put_string(buf, 10, 0x18, 0);
        scratch_pop(256);
    }
    if (fight_check_keys(ch)) return;
    if ((P8(act, 6) >> 1) == 0 || (s8)P8(act, 3) <= 0 || (P8(act, 0x14) == 0 && P8(ch, 0x2f) == 5)) { fight_stand_down(ch); return; }
    u8 dir;
    if (P8(act, 0x14) == 0) dir = fight_direction(P32(act, 0x0a), ch);
    else {
        P8(act, 0x15) = roll_dice(2, 1);
        u8 f = G8(0x6aaf);
        dir = (u8)(f - ((f + 2) % 4 >> 1));
        if (P8(ch, 0x10e) == 0) dir = (u8)(dir + 4);
        dir %= 8;
    }
    u8 edge = 0, done = 0, attempt = 1, way = 0;
    while (attempt < 6 && !done) {
        if (fight_can_step(&edge, dir, attempt, ch)) break;
        if (P8(act, 0x14) && edge) done = fight_escape(ch);
        else attempt++;
    }
    if (done) {
        P8(act, 6) = 0;
        P8(act, 0x14) = 0;
        chr_clear_action(ch);
        return;
    }
    way = (u8)((dir + G8(0x2ae + P8(act, 0x15) * 5 + attempt)) % 8);
    if (attempt == 6 || G8(0x443c) == (way + 4) % 8) {
        G8(0x443d)++;
        P8(act, 0x15) = (u8)(P8(act, 0x15) % 6 + 1);
        if (G8(0x443d) > 1) {
            SETP32(act, 0x0a, 0);
            if (G8(0x443d) > 2) { P8(act, 6) = 0; done = 1; }
            else if (!fight_pick_target(0, 1, 0xff, ch)) done = fight_stand_down(ch);
        }
    }
    if (attempt < 6) G8(0x443c) = way; else done = 1;
    if (done) return;
    combat_face(0, 0, way, ch);
    fight_free_attacks(P8(act, 9), ch);
    if (P8(ch, 0x10d) == 0) { chr_clear_action(ch); return; }
    fight_step(P8(act, 9), ch);
    if (P8(ch, 0x10d) == 0) chr_clear_action(ch);
    fight_cloud_check(ch);
}

// 2E2A:0E82  The AD&D "sweep": a fighter facing creatures of less than one hit die (byte 73h = 0) gets one
// attack per level (action byte 5) against them. It applies when the chosen target is such a creature
// standing next to the attacker, and more than one of them is in reach. The chosen target is moved to the
// front of the reach list, and then up to that many of the small creatures are attacked in turn.
u8 fight_sweep(farptr target, farptr ch) {
    static const u8 sweeps[] = "\x06" "sweeps";
    farptr act = P32(ch, 0x108);
    if (!(P8(ch, 0x113) < P8(act, 5))) return 0;
    if (P8(target, 0x73) != 0) return 0;
    if (combat_distance(target, ch) != 1) return 0;
    u8 n = combat_enemies_in_reach(1, ch), small = 0, at = 0;
    for (int i = 1; i <= n; i++) {
        farptr c = G32(0x65b9 + G8(0x6de4 + i) * 4);
        if (c == target) at = (u8)i;
        if (P8(c, 0x73) == 0) small++;
    }
    if (!(small > P8(ch, 0x113))) return 0;
    if (small > P8(act, 5)) small = P8(act, 5);
    ui_char_message(1, 10, sweeps, ch);
    if (G32(0x65b9 + G8(0x6de5) * 4) != target) {
        G8(0x6de4 + at) = G8(0x6de5);
        G8(0x6de5) = combat_slot(target);
    }
    for (int i = 1; i <= n; i++) {
        if (small == 0 || P8(G32(0x65b9 + G8(0x6de4 + i) * 4), 0x73) != 0) continue;
        farptr e = G32(0x65b9 + G8(0x6de4 + i) * 4);
        fight_note_attacker(e, ch);
        P8(ch, 0x113) = 1;
        fight_strike(&G8(0x6e31), 0, 0, e, ch);
        small--;
    }
    return 1;
}

// 10BA:2E8B  What a combatant would fire: for a weapon whose type flags (DS:5590) have bit 10h, the weapon
// itself (thrown); for one with bit 8h, the arrows (bit 1, slot F8h) or quarrels (bit 80h, slot FCh) it has
// readied. True if there is something to fire, or the flags are exactly 0Ah (a weapon needing no ammunition).
u8 chr_missile_ammo(farptr *ammo, farptr ch) {
    farptr w = P32(ch, 0xcc); u8 flags = 0;
    *ammo = 0;
    if (w) {
        flags = G8(0x5590 + P8(w, 0x2e) * 16);
        if (flags & 0x10) *ammo = w;
        if (flags & 0x08) {
            if (flags & 0x01) *ammo = P32(ch, 0xf8);
            if (flags & 0x80) *ammo = P32(ch, 0xfc);
        }
    }
    return (*ammo != 0 || flags == 10);
}

#define ITYPE(item, o) G8(0x5582 + P8(item, 0x2e) * 16 + (o))

// 3C10:14D0  The item menu's Ready command for the selected character (DS:5D92): readies or puts away an item.
// A readied cursed item cannot be put away. Readying needs a free hand (hands in use plus the item's must not
// exceed 2), a free slot of the item's kind (arrows and quarrels have their own), and the right class. Items with
// a power (byte 3Eh bit 7) switch their effect on and off with it. In combat the "hands are full" message is
// left out for quick-fighting characters.
void item_ready_toggle(farptr item) {
    u8 power = P8(item, 0x3e) > 0x7f;
    farptr ch = G32(0x5d92);
    if (P8(item, 0x34)) {
        if (P8(item, 0x36)) { static const u8 m[] = "\x0b" "It's Cursed"; ui_notice(m); return; }
        P8(item, 0x34) = 0;
        if (power) effect_run(1, item, ch, P8(item, 0x3e));
        return;
    }
    u8 why = 0;
    if ((int)P8(ch, 0x100) + ITYPE(item, 1) > 2) why = 3;
    u8 slot = ITYPE(item, 0);
    if (slot <= 8) { if (P32(ch, 0xcc + (s8)slot * 4)) why = 2; }
    else if (slot == 9 && P32(ch, 0xf4)) why = 2;
    if (P8(item, 0x2e) == 0x49 && P32(ch, 0xf8)) { why = 2; slot = 0x0b; }
    if (P8(item, 0x2e) == 0x1c && P32(ch, 0xfc)) { why = 2; slot = 0x0c; }
    if (!(ITYPE(item, 0xd) & P8(ch, 0xb0))) why = 1;
    if (why == 0) {
        P8(item, 0x34) = 1;
        if (power) effect_run(0, item, ch, P8(item, 0x3e));
    } else if (why == 1) {
        static const u8 m[] = "\x0b" "Wrong Class"; ui_notice(m);
    } else if (why == 2) {
        farptr other = P32(ch, 0xcc + (s8)slot * 4);
        chr_item_name(0, 0, 0, 0, other);
        u8 m[256]; static const char head[] = "already using ";
        int n = (int)strlen(head); memcpy(m + 1, head, n);
        int k = P8(other, 0); if (n + k > 255) k = 255 - n;
        memcpy(m + 1 + n, &P8(other, 1), k); m[0] = (u8)(n + k);
        ui_notice(m);
    } else if (G8(0x49f3) != 5 || P8(ch, 0x10f) == 0) {
        static const u8 m[] = "\x14" "Your hands are full!"; ui_notice(m);
    }
}
static void ready_toggle(farptr item) { item_ready_toggle(item); }

// 26B0:13CA  The computer arms a combatant: among the items its class may use it finds the best missile weapon
// and the best hand weapon (scored by fight_weapon_score; the natural attack, dice times sides plus twice the
// bonus, is the hand weapon to beat) and the best shield (bonus plus one). It takes the missile weapon if that
// scores more than half the hand weapon, has something to fire, and either needs both hands anyway or no
// enemy is adjacent. Cursed items (byte 36h) stay where they are. Then it fits a shield into a free hand.
void fight_ai_arm(farptr ch) {
    if (P32(ch, 0xcc)) P8(ch, 0x100) -= ITYPE(P32(ch, 0xcc), 1);
    if (P32(ch, 0xd0)) P8(ch, 0x100) -= ITYPE(P32(ch, 0xd0), 1);
    farptr missile = 0, melee = 0, shield = 0;
    u8 m = 1, w = (u8)(P8(ch, 0xa5) * P8(ch, 0xa3)), s = 0;
    if ((s8)P8(ch, 0xa7) > 0) w = (u8)(w + (s8)P8(ch, 0xa7) * 2);
    for (farptr it = P32(ch, 0xc8); it; it = P32(it, 0x2a)) {
        if (ITYPE(it, 0) == 0 && (ITYPE(it, 0xd) & P8(ch, 0xb0))) {
            u8 sc = fight_weapon_score(it, ch);
            if (((ITYPE(it, 0xe) & 8) || (ITYPE(it, 0xe) & 0x10)) && m < sc) { missile = it; m = sc; }
            if (!(ITYPE(it, 0xe) & 8) && w < sc) { melee = it; w = sc; }
        }
        if (ITYPE(it, 0) == 1 && (ITYPE(it, 0xd) & P8(ch, 0xb0))) {
            u8 v = (s8)P8(it, 0x32) < 0 ? 0 : (u8)(P8(it, 0x32) + 1);
            if (s < v) { shield = it; s = v; }
        }
    }
    u8 both = missile && ITYPE(missile, 0xc) >= 2 && (ITYPE(missile, 0xe) & 0x14) == 0x14;
    farptr ammo = 0; u8 flags = 0;
    if (missile) {
        flags = ITYPE(missile, 0xe);
        if (flags & 0x10) ammo = missile;
        if (flags & 0x08) {
            if (flags & 0x01) ammo = P32(ch, 0xf8);
            if (flags & 0x80) ammo = P32(ch, 0xfc);
        }
    }
    u8 can_fire = ammo != 0 || flags == 10;
    farptr pick = (missile && (int)m > (int)(w >> 1) && can_fire && (both || combat_enemies_in_reach(1, ch) == 0)) ? missile : melee;
    u8 changed = 0, swap = 1;
    farptr cur = P32(ch, 0xcc);
    if (cur && (cur == pick || P8(cur, 0x36))) swap = 0;
    if (swap) {
        if (P32(ch, 0xcc)) ready_toggle(P32(ch, 0xcc));
        chr_recalculate(ch);
        if (P32(ch, 0xd0) && P8(P32(ch, 0xd0), 0x36) == 0) P8(ch, 0x100) -= ITYPE(P32(ch, 0xd0), 1);
        if (pick) ready_toggle(pick);
        changed = 1;
    }
    chr_recalculate(ch);
    fight_set_attacks(ch);
    swap = 1;
    cur = P32(ch, 0xd0);
    if (cur && (cur == shield || P8(cur, 0x36))) swap = 0;
    if (P8(ch, 0x100) > 2) {
        if (P32(ch, 0xd0) == 0 || P8(P32(ch, 0xd0), 0x36)) ready_toggle(pick);
        else ready_toggle(P32(ch, 0xd0));
        changed = 1;
    } else if (P8(ch, 0x100) < 2 && swap) {
        if (P32(ch, 0xd0)) ready_toggle(P32(ch, 0xd0));
        chr_recalculate(ch);
        if (shield) ready_toggle(shield);
        changed = 1;
    }
    chr_recalculate(ch);
    if (changed) hud_combat_panel(ch);
}

// 26B0:0B3C  A computer-run combatant's turn: move while fleeing, then repeatedly choose and attack. If the
// current target can be attacked and is in sight within weapon reach it is attacked; otherwise a random enemy in
// reach is chosen (a missile user with an enemy adjacent re-arms instead), and with none in reach it picks a
// new target and walks towards it, or stands down. Against small creatures a fighter may sweep. More than 20
// rounds of this and it gives up. Returns true when the combatant has finished acting.
u8 fight_ai_turn(farptr ch) {
    farptr act = P32(ch, 0x108);
    G8(0x443c) = 8; G8(0x443d) = 0; G8(0x443e) = 0;
    u8 count = 0, go = 1, stop = 0;
    effects_event(ch, 0x0e);
    if (P8(act, 3) == 0) go = 0;
    while (!stop && go) {
        if (P8(act, 0x14))
            while (P8(act, 6) > 0 && (s8)P8(act, 3) > 0 && (s8)P8(act, 3) < 0x14) fight_ai_move(ch);
        if (P8(act, 3) == 0 || P8(act, 3) == 0x14) go = 0; else stop = 0;
        if (stop || !go) continue;
        if (++count > 20) { stop = 1; go = fight_stand_down(ch) == 0; }
        u8 attack = 0, reach = 1;
        if (P32(ch, 0xcc)) reach = (u8)(ITYPE(P32(ch, 0xcc), 0xc) - 1);
        if (reach == 0 || reach == 0xff) reach = 1;
        farptr t = P32(act, 0x0a);
        if (P8(t, 0x10d) == 0 || P8(t, 0x10e) == 0) t = 0;
        if (t && fight_can_target(t, ch)) {
            farptr v = scratch_push(4);
            wr16(LIN(v), reach); P8(v, 2) = combat_y(t); P8(v, 3) = combat_x(t);
            u8 seen = line_of_sight(G32(0x6716), v, FARPTR(FP_SEG(v), FP_OFF(v) + 2), FARPTR(FP_SEG(v), FP_OFF(v) + 3),
                                    (s8)combat_y(ch), (s8)combat_x(ch));
            u16 dist = rd16(LIN(v));
            scratch_pop(4);
            if (seen && (dist >> 1) <= reach) attack = 1;
        }
        if (!attack) {
            u8 n = combat_enemies_in_reach(reach, ch);
            if (n == 0) {
                if (fight_pick_target(0, 0, 0xff, ch)) fight_ai_move(ch);
                else stop = fight_stand_down(ch);
            } else {
                u8 k = roll_dice(n, 1);
                t = G32(0x65b9 + G8(0x6de4 + k) * 4);
                if (chr_has_missile_weapon(ch) && !chr_has_missile_weapon_14(ch) && combat_enemies_in_reach(1, ch) > 0) {
                    fight_ai_arm(ch);
                    stop = 1;
                } else if (combat_distance(t, ch) == 1 || fight_can_target(t, ch)) attack = 1;
            }
        }
        if (attack) combat_step_view(fight_direction(t, ch), 2, (s8)combat_y(ch), (s8)combat_x(ch));
        if (!attack) continue;
        if (fight_sweep(t, ch)) { stop = chr_clear_action(ch); continue; }
        fight_note_attacker(t, ch);
        farptr ammo = 0;
        if (chr_has_missile_weapon(ch)) {
            chr_missile_ammo(&ammo, ch);
            if (chr_has_missile_weapon_14(ch) && combat_distance(t, ch) == 1) ammo = 0;
        }
        fight_strike(&stop, ammo, 0, t, ch);
        if (stop) go = 0;
        else if (P8(t, 0x10d) == 0) stop = 1;
    }
    return go == 0;
}

// 26B0:000F  A computer-run combatant's whole decision. After the usual checks it picks a movement pattern
// for this round (action byte 15h: kept if 1-4 three times in four, otherwise 1-4, or 5-6 one time in eight),
// then tests morale; a newly panicked creature "flees in panic". It then tries, in order: using an item,
// finishing a spell already being cast, turning undead, and casting a spell. Failing all of these it arms
// itself and fights (fight_ai_turn) until done, standing down if it finds no target.
void fight_ai_decide(farptr ch) {
    static const u8 panic[] = "\x0e" "flees in panic";
    farptr act = P32(ch, 0x108);
    u8 done = fight_check_keys(ch);
    menu_clear_line();
    ui_clear_message();
    if (P8(ch, 0x10d) == 0) done = chr_clear_action(ch);
    u8 p = P8(act, 0x15);
    if (p == 0 || p > 4 || roll_dice(4, 1) == 1) {
        p = roll_dice(8, 1);
        if (p == 8) p = (u8)(roll_dice(2, 1) + 4);
        else p = roll_dice(4, 1);
    }
    P8(act, 0x15) = p;
    if (!done) done = fight_morale(ch);
    if (P8(act, 0x14) && P8(act, 0x10) == 0) ui_char_message(1, 10, panic, ch);
    if (done) return;
    if (fight_try_item(ch)) { chr_clear_action(ch); return; }
    if (P8(act, 0) > 0) {
        farptr v = scratch_push(2);
        call_original(0x41cf, 0x0c20, 5, P8(act, 0), 1, 1, FP_SEG(v), FP_OFF(v));
        scratch_pop(2);
        chr_clear_action(ch);
        return;
    }
    if (fight_try_turn_undead(ch)) { chr_clear_action(ch); return; }
    if (fight_try_spell(ch)) return;
    fight_ai_arm(ch);
    done = fight_check_keys(ch);
    while (!done) {
        if (fight_pick_target(0, 1, 0xff, ch) && (s8)P8(act, 3) > 0) done = fight_ai_turn(ch);
        else done = fight_stand_down(ch);
    }
}

// 2E2A:2414  The Cast command in combat for the selected character (DS:5D92). With spell 0 the player picks
// one from the character's list (3C10:299C). Spells without the combat flag (DS:3205) are refused as "Camp Only
// Spell". Unless quiet, the caster is highlighted and the side panel redrawn. A spell whose casting time
// (DS:3206) is under three segments goes off at once; a longer one "Begins Casting": it is stored in the action
// record and takes time/3 from the actions left (leaving at least one), to be finished on a later turn.
void fight_cast_command(farptr done, u8 quiet, u8 spell) {
    farptr ch = G32(0x5d92);
    P8(done, 0) = 0;
    if (spell == 0) {
        farptr v = scratch_push(4);
        P8(v, 0) = 1; wr16(LIN(v) + 2, 0xffff);
        spell = (u8)call_original(0x3c10, 0x299c, 6, 0, 1, FP_SEG(v), (u16)(FP_OFF(v) + 2), FP_SEG(v), FP_OFF(v));
        scratch_pop(4);
    }
    if (spell && G8(0x3205 + spell * 16) == 0) {
        static const u8 m[] = "\x0f" "Camp Only Spell";
        ui_notice(m);
        spell = 0;
    }
    if (!quiet) {
        call_original(0x10ba, 0x2b88, 0);
        combat_redraw_one(1, 3, ch);
        hud_combat_panel(ch);
    }
    if (!spell) return;
    u8 t = (u8)(G8(0x3206 + spell * 16) / 3);
    if (t == 0) {
        call_original(0x41cf, 0x0c20, 5, spell, quiet, 1, FP_SEG(done), FP_OFF(done));
        P8(done, 0) = chr_clear_action(ch);
        return;
    }
    P8(done, 0) = 1;
    static const u8 begins[] = "\x0e" "Begins Casting";
    ui_char_message(1, 10, begins, ch);
    farptr act = P32(ch, 0x108);
    P8(act, 0) = spell;
    if ((s8)P8(act, 3) > (int)t) P8(act, 3) -= t; else P8(act, 3) = 1;
}

// 10BA:1892  Copies one missile picture (DS:5E5A, two per kind) into frame `frame` of the flight animation work
// picture (DS:6E36), mirrored if flip is set. Each frame is the picture's bitmap (size in work byte 11h) followed
// separately by its mask, which is a quarter of the size in one of the display modes (DS:4A1E = 1).
void combat_missile_frame(u8 flip, u8 frame, u8 sub, u8 kind) {
    farptr work = G32(0x6e36);
    u16 size = P16(work, 0x11), msize = size;
    if (G8(0x4a1e) == 1) msize >>= 2;
    farptr src = G32(0x5e5a + kind * 8 + sub * 4), v = 0;
    if (flip) {
        v = scratch_push(4);
        gfx_alloc_pic(v, 1, 1, 3, 0x18);
        gfx_flip_pic(P32(v, 0), src);
        src = P32(v, 0);
    }
    memmove(&mem[LIN(FARPTR(FP_SEG(work), (u16)(FP_OFF(work) + frame * size + 0x17)))], &mem[LIN(FARPTR(FP_SEG(src), (u16)(FP_OFF(src) + 0x17)))], size);
    farptr wm = P32(work, 0x13), sm = P32(src, 0x13);
    memmove(&mem[LIN(FARPTR(FP_SEG(wm), (u16)(FP_OFF(wm) + frame * msize)))], &mem[LIN(sm)], msize);
    if (flip) { gfx_free_pic(v); scratch_pop(4); }
}

// 10BA:19DE  Builds the four-frame spinning animation of a thrown missile from its two pictures: as is,
// mirrored, the second mirrored, and the second as is.
void combat_missile_spin(u8 kind) {
    combat_missile_frame(0, 0, 0, kind);
    combat_missile_frame(1, 1, 0, kind);
    combat_missile_frame(1, 2, 1, kind);
    combat_missile_frame(0, 3, 1, kind);
}

// 2E2A:1BF8  Whom should a healer heal? Looks at its own square and the eight around it. Among wounded allies
// standing there it picks the one with the fewest hit points, but heals itself only once below half. If the best
// candidate has 8 or more hit points and an ally lies fallen (body tile 1Fh) on one of those squares, not dead,
// stoned or gone, the fallen ally comes first. True if there is someone to heal.
farptr fight_heal_target(farptr ch) {
    farptr best = 0; u8 fallen = 0, low = 0xff;
    for (u8 d = 0; ; d++) {
        s8 x = (s8)((s8)combat_x(ch) + (s8)G8(0x2736 + d)), y = (s8)((s8)combat_y(ch) + (s8)G8(0x273f + d));
        u8 tile, who;
        combat_square(&tile, &who, y, x);
        if (who > 0) {
            farptr c = G32(0x65b9 + who * 4);
            if (P8(c, 0x10e) == P8(ch, 0x10e) && P8(c, 0x11b) < P8(c, 0x32) &&
                (P8(c, 0x11b) < low || (c == ch && (int)P8(c, 0x11b) < (int)(P8(c, 0x32) >> 1)))) {
                best = c; low = P8(c, 0x11b);
            }
        } else if (tile == 0x1f) {
            u8 n = G8(0x6715);
            for (int k = 1; k <= n; k++) {
                farptr f = G32(0x66d6 + k * 7);
                if (!f) continue;
                u8 st = P8(f, 0x10c);
                if (st == 6 || st == 7 || st == 8) continue;
                if (G8(0x66da + k * 7) == (u8)x && G8(0x66db + k * 7) == (u8)y) fallen = (u8)k;
            }
        }
        if (d == 8) break;
    }
    if (low < 8 || fallen == 0) return best;
    return G32(0x66d6 + fallen * 7);
}

// 41CF:331A  Announces a spell. In combat: "Casts a Spell" beside the caster and "Spell: <name>" on the bottom
// line. In camp: the caster's name, the verb (for example "casts") and the spell name, then a pause.
void spell_announce(u8 spell, farptr verb, farptr ch) {
    u8 v[41]; u8 n = P8(verb, 0) > 40 ? 40 : P8(verb, 0); v[0] = n; memcpy(v + 1, &P8(verb, 1), n);
    farptr name = FARPTR(DATA_SEG, (u16)(0x28e9 + spell * 0x29));
    if (G8(0x49f3) == 5) {
        static const u8 casts[] = "\x0d" "Casts a Spell";
        ui_char_message(1, 10, casts, ch);
        text_clear_box(0, 0x17, 0x27, 0x17);
        farptr b = scratch_push(256);
        u8 *s = &P8(b, 0); int k = P8(name, 0); if (6 + k > 255) k = 255 - 6;
        memcpy(s + 1, "Spell:", 6); memcpy(s + 7, &P8(name, 1), k); s[0] = (u8)(6 + k);
        text_put_string(b, 10, 0x17, 0);
        scratch_pop(256);
    } else {
        text_clear_box(1, 0x12, 0x26, 0x16);
        hud_draw_name(0, 0x13, 1, ch);
        farptr b = scratch_push(42);
        memcpy(&P8(b, 0), v, (size_t)n + 1);
        text_put_string(b, 10, 0x13, (u8)(P8(ch, 0) + 2));
        scratch_pop(42);
        text_put_string(name, 10, 0x14, 1);
        text_pause();
        ui_clear_message();
    }
}

static u32 spell_handler(u8 n) { u16 off = G16(0x6b84 + n * 4), seg = G16(0x6b86 + n * 4); return ((u32)(u16)(seg - LOAD_SEG + 0x1000) << 16) | off; }

// 41CF:0C20  Casts spell for the selected character (DS:5D92), or uses an item's spell if DS:6DBF is set.
// Outside combat a combat-only spell (spell byte 7 = 0) only offers to "Lose it?" (an item: "Use it?").
// Otherwise the spell's targeting handler (table DS:6B84, entry 0) is called until a target is chosen; then
// in combat the caster turns towards the target and a spell bolt flies, the spell leaves memory and its own
// handler (entry `spell`) runs. If no target is chosen in combat the player is asked "Abort Spell?" (unless
// `noask`), and aborting also loses the spell.
void spell_cast(farptr done, u8 announce, u8 noask, u8 spell) {
    farptr ch = G32(0x5d92);
    u8 go = 1;
    if (G8(0x49f3) != 5 && G8(0x3201 + spell * 16) == 0) {
        if (G8(0x6dbf) == 0) {
            text_put_string(FARPTR(DATA_SEG, (u16)(0x28e9 + spell * 0x29)), 10, 0x13, 1);
            text_put_string(FARPTR(RSEG(0x41cf), 0x0bae), 10, 0x14, 1);              // "is a combat-only spell..."
            if (menu_yes_no(10, 0xf, 0xd, FARPTR(RSEG(0x41cf), 0x0bc8)) == 'Y') chr_forget_spell(spell);   // "Lose it? "
        } else {
            text_put_string(FARPTR(RSEG(0x41cf), 0x0bd2), 10, 0x13, 1);              // "That Item"
            text_put_string(FARPTR(RSEG(0x41cf), 0x0bdc), 10, 0x14, 1);              // "is a combat-only item..."
            if (menu_yes_no(10, 0xf, 0xd, FARPTR(RSEG(0x41cf), 0x0bf5)) == 'Y') P8(done, 0) = 1;           // "Use it? "
        }
        announce = 0; go = 0;
    }
    if (announce && G8(0x6dbf) == 0) spell_announce(spell, FARPTR(RSEG(0x41cf), 0x0bfe), ch);   // "casts"
    while (go) {
        u32 h = spell_handler(0);
        emu_pcall((u16)(h >> 16), (u16)h, 4, spell, noask, FP_SEG(done), FP_OFF(done));
        if (P8(done, 0)) {
            go = 0;
            if (G8(0x49f3) == 5) {
                combat_missile_spin(0x12);
                u8 x = combat_x(ch), y = combat_y(ch), d = 0;
                while (!combat_in_arc(d, (s8)G8(0x6dba), (s8)G8(0x6db9), (s8)y, (s8)x)) d++;
                combat_face(0, 1, d, ch);
                sound_music(0);
                sound_play(G16(spell == 0x2f ? 0x262a : spell == 0x33 ? 0x2624 : 0x2618));
                combat_missile_anim_args(x, y, G8(0x6db9), G8(0x6dba), 4, 0x1e);
                sound_music(10);
                if (combat_visible(0, ch)) {
                    combat_face(1, 1, P8(P32(ch, 0x108), 9), ch);
                    combat_face(0, 0, P8(P32(ch, 0x108), 9), ch);
                }
            }
            effects_remove_invisibility(ch);
            if (G8(0x6dbf) == 0) chr_forget_spell(spell);
            G8(0x681b) = spell;
            h = spell_handler(G8(0x681b));
            emu_pcall((u16)(h >> 16), (u16)h, 0);
            G8(0x681b) = 0;
            G8(0x6820) = 0;
        } else if (G8(0x49f3) != 5) go = 0;
        else if (noask || menu_yes_no(0xf, 10, 0xe, FARPTR(RSEG(0x41cf), 0x0c04)) == 'Y') {   // "Abort Spell? "
            static const u8 aborted[] = "\x0d" "Spell Aborted";
            ui_notice(aborted);
            if (G8(0x6dbf) == 0) chr_forget_spell(spell);
            go = 0;
        }
    }
    ui_clear_message();
    if (G8(0x49f3) == 5) text_clear_box(0, 0x17, 0x27, 0x17);
}

// 41CF:07D6  How long a spell lasts: a few spells have their own rolls (28h: 1d6 x 10; 39h, 3Dh: 5d4;
// 3Bh: 40 + 1d4 x 10; 3Fh: 2d10 x 10 in combat, (1d10 + 10) x 10 otherwise; 43h: 1440); the rest last the
// spell's base time plus its time per level times the caster's level (spell bytes 4 and 5).
u16 spell_duration(u8 spell) {
    if (spell == 0x28) return (u16)(roll_dice(6, 1) * 10);
    if (spell == 0x39 || spell == 0x3d) return roll_dice(4, 5);
    if (spell == 0x3b) return (u16)(roll_dice(4, 1) * 10 + 0x28);
    if (spell == 0x3f) return (u16)((G8(0x49f3) == 5 ? roll_dice(10, 2) : roll_dice(10, 1) + 10) * 10);
    if (spell == 0x43) return 0x5a0;
    u8 lv = (u8)call_original(0x10ba, 0x26c2, 1, spell);
    return (u16)(G8(0x31fe + spell * 16) + G8(0x31ff + spell * 16) * lv);
}

// 41CF:08C3  Applies a spell to every target chosen (DS:6C94 targets, pointers from DS:6C95). Each target
// may save (spell byte 8 says whether, byte 9 against what). Touch spells (range byte FFh) need a hit roll
// against the target's AC first; a miss means no damage, here and for the remaining targets, and counts as
// saved. Then damage of the given kind is dealt, and the spell's status effect (byte 0Ah) is added for its
// duration with the message, at the caster level given (or the caster's own).
void spell_affect_targets(const u8 *msg, u8 kind, u8 damage, u8 notify, u8 level, u8 spell) {
    u8 m[41]; u8 n = msg[0] > 40 ? 40 : msg[0]; m[0] = n; memcpy(m + 1, msg + 1, n);
    G8(0x6819) = damage ? kind : 0;
    if (G8(0x6c94) == 0) return;
    u8 lv = level ? level : (u8)call_original(0x10ba, 0x26c2, 1, spell);
    u8 count = G8(0x6c94);
    for (int k = 1; k <= count; k++) {
        farptr t = G32(0x6c91 + k * 4);
        if (!t) continue;
        u8 saved = 0;
        if (G8(0x3202 + spell * 16)) saved = rules_saving_throw(0, (s8)G8(0x3203 + spell * 16), t);
        if (G8(0x31fc + G8(0x681b) * 16) == 0xff) {
            chr_recalculate(t);
            effects_event(t, 0x0b);
            if (!fight_hit_roll(P8(t, 0x111), t, G32(0x5d92))) { damage = 0; saved = 1; }
        }
        if (damage) rules_damage(saved, G8(0x3202 + spell * 16), damage, t);
        if (G8(0x3204 + spell * 16))
            rules_spell_effect(m, saved, G8(0x3202 + spell * 16), notify, lv, spell_duration(spell), G8(0x3204 + spell * 16), t);
    }
    G8(0x6819) = 0;
}

// 41CF:0F4F  Applies the current spell (DS:681B) to the targets on one side only. Bless in combat also leaves
// out anyone already in melee (an enemy within reach), as the rulebook says it cannot help those already fighting.
void spell_affect_side(const u8 *msg, u8 side) {
    u8 m[41]; u8 n = msg[0] > 40 ? 40 : msg[0]; m[0] = n; memcpy(m + 1, msg + 1, n);
    G8(0x6820) = 1;
    u8 count = G8(0x6c94);
    for (int k = 1; k <= count; k++) {
        farptr t = G32(0x6c91 + k * 4);
        if (P8(t, 0x10e) == side && !(G8(0x681b) == 1 && G8(0x49f3) == 5 && combat_enemies_in_reach(1, t) > 0)) continue;
        SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + k * 4, 0);
    }
    spell_affect_targets(m, 0, 0, 0, 0, G8(0x681b));
}

// Spell effect handlers (table DS:6B84, entry = spell number); the current spell is in DS:681B. Most hand the
// targets to spell_affect_targets with a message and, for attack spells, damage and its kind (8 = magic,
// 1 = fire, 4 = electricity).
#define SPL_MSG(off) (&mem[LIN(FARPTR(RSEG(0x41cf), (off)))])
#define SPL G8(0x681b)
static u8 spell_level(u8 spell) { return (u8)call_original(0x10ba, 0x26c2, 1, spell); }
static void spell_simple(u16 msg) { spell_affect_targets(SPL_MSG(msg), 0, 0, 0, 0, SPL); }
void spl_bless(void) { spell_affect_side(SPL_MSG(0x1004), P8(G32(0x5d92), 0x10e)); }            // 41CF:100F
void spl_curse(void) { spell_affect_side(SPL_MSG(0x1036), chr_on_party_side(G32(0x5d92))); }    // 41CF:1040
void spl_cause_light(void) { spell_affect_targets(SPL_MSG(0x10a6), 8, roll_dice_noted(8, 1), 0, 0, SPL); }   // 41CF:10A7: 1d8
void spl_detect(void) { spell_simple(0x10dd); }                 // 41CF:10E9 (Detect Magic, Read Magic, Find Traps, Detect Invisibility)
void spl_protection(void) { spell_simple(0x1116); }             // 41CF:1123 (Protection from Evil/Good, Normal Missiles)
void spl_resist_cold(void) { spell_simple(0x1150); }            // 41CF:1162
void spl_burning_hands(void) { spell_affect_targets(SPL_MSG(0x118f), 9, spell_level(SPL), 0, 0, SPL); }      // 41CF:1190: 1 per level, fire
void spl_magic_missile(void) {                                  // 41CF:1439: level/2 missiles of 1d4+1
    u8 lv = spell_level(SPL), half = lv >> 1;
    spell_affect_targets(SPL_MSG(0x1438), 8, (u8)(roll_dice_noted(4, half) + half), 0, 0, SPL);
}
void spl_shield(void) { spell_simple(0x148d); }                 // 41CF:1499
void spl_shocking_grasp(void) {                                 // 41CF:14C7: 1d8 + 1 per level, electricity
    u8 lv = spell_level(SPL);
    spell_affect_targets(SPL_MSG(0x14c6), 0x0c, (u8)(roll_dice_noted(8, 1) + lv), 0, 0, SPL);
}
void spl_resist_fire(void) { spell_simple(0x17d7); }            // 41CF:17E9
void spl_silence(void) { spell_simple(0x1816); }                // 41CF:1822
void spl_invisibility(void) { spell_simple(0x1a09); }           // 41CF:1A16
void spl_knock(void) { spell_simple(0x1a43); }                  // 41CF:1A4F
void spl_enfeeble(void) { spell_simple(0x1ac0); }               // 41CF:1ACC
void spl_cause_blindness(void) { spell_simple(0x2254); }        // 41CF:225D
void spl_bestow_curse(void) { spell_simple(0x25f3); }           // 41CF:2604
void spl_blink(void) { spell_simple(0x2631); }                  // 41CF:263D
void spl_cause_disease(void) { spell_affect_targets(SPL_MSG(0x2340), 0, 0, 1, 0, SPL); }   // 41CF:234C
void spl_spiritual_hammer(void) {                               // 41CF:19C3: then the hammer's effect starts at once
    spell_affect_targets(SPL_MSG(0x19c2), 0, 0, 1, 0, SPL);
    effect_run(0, 0, G32(0x6c95), 0x17);
}
void spl_mirror_image(void) { spell_affect_targets(SPL_MSG(0x1a7c), 0, 0, 0, roll_dice(4, 1), SPL); }   // 41CF:1A8A: 1d4 images
void spl_prayer(void) {                                         // 41CF:24D6: the caster's side is kept in the level byte
    u8 lv = spell_level(SPL);
    spell_affect_targets(SPL_MSG(0x24cb), 0, 0, 0, (u8)(lv + (s8)P8(G32(0x5d92), 0x10e) * 16), SPL);
}

// 4592:1081  Cures one status effect: if the character has it, "is Cured" and the effect is removed.
u8 effect_cure(u8 kind, farptr ch) {
    farptr node = char_find_effect(kind, ch);
    if (!node) return 0;
    static const u8 cured[] = "\x08" "is Cured";
    ui_char_message(1, 10, cured, ch);
    effect_remove(node, kind, ch);
    return 1;
}

void spl_cure_light(void) {                                     // 41CF:106B: 1d8 hit points
    if (G8(0x6c94) == 0) return;
    farptr t = G32(0x6c95);
    if (rules_heal(0, roll_dice(8, 1), t)) chr_healed_message(t);
}
void spl_reduce(void) {                                         // 41CF:136E: undoes Enlarge (0Ch) unless the target saves
    if (G8(0x6c94) == 0) return;
    farptr t = G32(0x6c95);
    if (rules_saving_throw(0, 4, t)) return;
    if (effect_cure(0x0c, t)) { static const u8 m[] = "\x10" "has been reduced"; ui_char_message(1, 10, m, t); }
}
void spl_friends(void) {                                        // 41CF:13D8: Charisma + 2d4 (at most 25) for the duration
    farptr t = G32(0x6c95);
    u8 old = P8(t, 0x15);
    P8(t, 0x15) += roll_dice(4, 2);
    if (P8(t, 0x15) > 0x19) P8(t, 0x15) = 0x19;
    spell_affect_targets(SPL_MSG(0x13cc), 0, 0, 1, old, SPL);   // the old value is kept in the effect, to be restored
}
void spl_cure_blindness(void) {                                 // 41CF:2217
    farptr t = G32(0x6c95);
    if (effect_cure(0x21, t)) call_original(0x10ba, 0x1feb, 5, FP_SEG(t), FP_OFF(t), 1, RSEG(0x41cf), 0x220f);   // "can see"
}
void spl_sleep(void) {                                          // 41CF:151B
    // 4d4 "hit dice" of sleep (DS:4844) are spent on the targets in order. A creature costs 1 for up to 1 HD (byte
    // 73h), 2 for 2, 4 for 3, 6 for 4, 10 for 5 (20 if not of race 0), and 20 for anything bigger. Those in status 1,
    // those protected by effect 35h, and those the remaining dice cannot pay for are dropped from the targets.
    G8(0x6820) = 1;
    G8(0x4844) = roll_dice(4, 4);
    u8 count = G8(0x6c94);
    for (int k = 1; k <= count; k++) {
        farptr t = G32(0x6c91 + k * 4);
        s8 hd = (s8)P8(t, 0x73);
        u8 cost;
        if (hd >= 0 && hd <= 1) cost = 1;
        else if (hd == 2) cost = 2;
        else if (hd == 3) cost = 4;
        else if (hd == 4) cost = 6;
        else if (hd == 5) cost = P8(t, 0x2e) == 0 ? 10 : 20;
        else cost = 20;
        G8(0x4845) = cost;
        if (P8(t, 0x10c) != 1 && !char_find_effect(0x35, t) && G8(0x4844) >= G8(0x4845)) G8(0x4844) -= G8(0x4845);
        else SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + k * 4, 0);
    }
    spell_affect_targets(SPL_MSG(0x150e), 0, 0, 0, 0, SPL);
}

// 4592:115C  Raises Strength to str (with percentile pct for 18). If the character is already stronger, nothing
// changes and *saved gets the new value marked with bit 7 (inactive). Otherwise, if an Enlarge (0Ch) or
// Strength (26h) effect is already holding an original value, that original is taken over (and the old effect
// now holds the current value, inactive); the new Strength is set, and *saved gets the value to restore later.
u8 rules_raise_strength(u8 *saved, u8 pct, u8 str, farptr ch) {
    u8 cur = P8(ch, 0x10), curpct = P8(ch, 0x16);
    if (str < cur || (str == 0x12 && pct < curpct)) { *saved = (u8)(rules_encode(pct, str) - 0x80); return 0; }
    farptr found = 0;
    for (farptr node = P32(ch, 0x7f); node && !found; node = P32(node, 5))
        if ((P8(node, 0) == 0x0c || P8(node, 0) == 0x26) && P8(node, 3) < 0x80) found = node;
    if (found) {
        u8 c = rules_encode(curpct, cur);
        rules_strength_decode(&curpct, &cur, found);
        P8(found, 3) = (u8)(c - 0x80);
    }
    P8(ch, 0x10) = str;
    P8(ch, 0x16) = pct;
    chr_recalculate(ch);
    *saved = rules_encode(curpct, cur);
    return 1;
}

void spl_charm(void) {                                          // 41CF:11DD
    // Works only on people: creature class (9Fh) and size (6Ch) under 2. The effect remembers the caster's side
    // (bit 7 of its level byte) so the charmed creature changes sides, which the effect does at once.
    farptr t = G32(0x6c95);
    if (P8(t, 0x9f) < 2 && P8(t, 0x6c) < 2) {
        u8 lv = (u8)((s8)P8(G32(0x5d92), 0x10e) * 0x80 + spell_level(SPL));
        spell_affect_targets(SPL_MSG(0x11d2), 0, 0, 1, lv, SPL);
        farptr node = char_find_effect(0x0b, t);
        if (node) effect_run(0, node, G32(0x6c95), 0x0b);
    } else {
        static const u8 m[] = "\x0d" "is unaffected";
        ui_char_message(1, 10, m, t);
    }
}

void spl_enlarge(void) {                                        // 41CF:12A5: Strength 18 with a percentile by caster level
    static const u8 pct[] = { 0, 0, 1, 0x33, 0x4c, 0x5b, 100 };
    G8(0x4844) = 0x12;
    u8 lv = spell_level(SPL);
    if (lv >= 1 && lv <= 6) G8(0x4845) = pct[lv];
    u8 saved;
    if (rules_raise_strength(&saved, G8(0x4845), G8(0x4844), G32(0x6c95))) {
        static const u8 m[] = "\x0b" "is stronger";
        ui_char_message(1, 10, m, G32(0x6c95));
    }
    farptr t = G32(0x6c95);
    effect_add(1, saved, spell_duration(SPL), 0x0c, t);
}

// 41CF:1658  Hold Person, cleric (17h) or magic-user (31h). Fewer targets mean a harder save: one target saves at
// -2 against the cleric spell or -3 against the magic-user's, two at -1, three or four at no penalty. Each target
// after the first gets its own bolt. Anything that is not a person (creature class or size over 1) always saves.
// For more than four targets the original reads an uninitialised penalty; no penalty is taken here (as for three or
// four), one of the few places where the port has to choose.
u8 spl_hold_person(void) {
    u8 count = G8(0x6c94); s8 adj;
    if (count == 1) adj = SPL == 0x17 ? -2 : -3;
    else if (count == 2) adj = -1;
    else adj = 0;
    for (u8 k = count; k >= 1; k--) {
        farptr t = G32(0x6c91 + k * 4);
        if (t) {
            if (k < count) {
                sound_music(0);
                sound_play(G16(0x2618));
                combat_missile_spin(0x12);
                farptr c = G32(0x5d92);
                combat_missile_anim_args(combat_x(c), combat_y(c), combat_x(t), combat_y(t), 4, 0x1e);
                sound_music(10);
            }
            u8 saved = rules_saving_throw(adj, (s8)G8(0x3203 + SPL * 16), t);
            if (P8(t, 0x9f) > 1 || P8(t, 0x6c) > 1) saved = 1;
            u16 dur = spell_duration(SPL);
            u8 lv = spell_level(SPL);
            rules_spell_effect(SPL_MSG(0x1650), saved, G8(0x3202 + SPL * 16), 0, lv, dur, G8(0x3204 + SPL * 16), t);
        }
        if (k == 1) break;
    }
    return 1;
}

// 41CF:266B  Fireball: 1d6 per caster level of fire (kind 9). Spell 40h (the same blast from another source) uses
// 1d3 x 2 + 1 dice instead. Where the area's word 1CCh is zero the targets are gathered afresh: everyone within two
// squares of the blast point (DS:6DB9 x, 6DBA y). The view is centred on the blast first.
void spl_fireball(void) {
    G8(0x6820) = 1;
    u8 dice = SPL == 0x40 ? (u8)(roll_dice(3, 1) * 2 + 1) : spell_level(SPL);
    if (P16(G32(0x49d2), 0x1cc) == 0) {
        combat_reach(G32(0x6716), 1, 0xff, 2, (s8)G8(0x6dba), (s8)G8(0x6db9));
        u8 n = G8(0x671a);
        for (int k = 1; k <= n; k++) SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + k * 4, G32(0x65b9 + G8(0x6718 + k * 3) * 4));
        G8(0x6c94) = G8(0x671a);
    }
    combat_step_view(8, 0, (s8)G8(0x6dba), (s8)G8(0x6db9));
    spell_affect_targets(SPL_MSG(0x266a), 9, roll_dice_noted(6, dice), 0, 0, SPL);
}

// 41CF:28BC  One square of a lightning bolt: *wall is set if the square is solid (cost FFh), and whoever stands
// there takes the damage as electricity (kind 0Ch), halved on a save against save_kind.
void spl_bolt_square(u8 *wall, u8 save_kind, u8 damage, s8 y, s8 x) {
    u8 tile, who;
    *wall = 0;
    combat_square(&tile, &who, y, x);
    if (tile && G8(0x2744 + tile * 4) == 0xff) *wall = 1;
    if (who) {
        farptr t = G32(0x65b9 + who * 4);
        G8(0x6819) = 0x0c;
        u8 saved = rules_saving_throw(0, (s8)save_kind, t);
        rules_damage(saved, 2, damage, t);
        combat_missile_spin(0x13);
        G8(0x6819) = 0;
    }
}

// 41CF:295B  The path of a bolt from the caster through the target point (DS:6DB9 x, 6DBA y), length*2 squares.
// The bolt stops at each new creature, at walls and at the end of its length; each leg is animated and the square
// it stops on is struck. On a wall it bounces: the line is redrawn from the wall in the opposite direction, and the
// first bounce off a wall within 8 squares of the caster costs 8 squares extra. A creature can be struck twice.
void spl_bolt_path(u8 first, u8 save_kind, u8 damage, u8 length) {
    u8 tile = 0, who = 0, prev = 0, wall = 0;
    combat_missile_spin(0x13);
    combat_square(&tile, &prev, (s8)G8(0x6dba), (s8)G8(0x6db9));
    s8 sign = 1;
    farptr c = G32(0x5d92);
    s8 cx = (s8)combat_x(c), cy = (s8)combat_y(c);
    if (cx == (s8)G8(0x6db9) && cy == (s8)G8(0x6dba)) return;
    u8 len = (u8)(length * 2);
    G8(0x6820) = 1;
    farptr L = scratch_push(26), L2 = scratch_push(26);
    while (len > 0) {
        memset(&P8(L, 0), 0, 26);
        s16 tx = (s8)G8(0x6db9), ty = (s8)G8(0x6dba);
        wr16(LIN(L), (u16)tx); wr16(LIN(L) + 2, (u16)ty);
        wr16(LIN(L) + 4, (u16)(tx + (u16)((tx - cx) * sign) * len));
        wr16(LIN(L) + 6, (u16)(ty + (u16)((ty - cy) * sign) * len));
        line_begin(L);
        do {
            u8 px = P8(L, 0x14), py = P8(L, 0x16);
            if (P16(L, 0) != P16(L, 4) || P16(L, 2) != P16(L, 6)) {
                for (;;) {
                    u8 moved = line_step(L);
                    combat_square(&tile, &who, (s8)P8(L, 0x16), (s8)P8(L, 0x14));
                    if (!moved) break;
                    if (who > 0 && who != prev) break;
                    if (tile == 0) break;
                    if (G8(0x2744 + tile * 4) > 1) break;
                    if (!(P8(L, 0x18) < len)) break;
                }
            }
            prev = who;
            if (tile == 0) len = 0;
            combat_missile_anim_args(px, py, P8(L, 0x14), P8(L, 0x16), 4, 0x32);
            spl_bolt_square(&wall, save_kind, damage, (s8)P8(L, 0x16), (s8)P8(L, 0x14));
            if (wall) {
                G8(0x6db9) = P8(L, 0x14);
                G8(0x6dba) = P8(L, 0x16);
                memset(&P8(L2, 0), 0, 26);
                wr16(LIN(L2), (u16)(s16)(s8)G8(0x6db9)); wr16(LIN(L2) + 2, (u16)(s16)(s8)G8(0x6dba));
                wr16(LIN(L2) + 4, (u16)(s16)cx); wr16(LIN(L2) + 6, (u16)(s16)cy);
                line_begin(L2);
                while (line_step(L2)) ;
                if (first && P8(L2, 0x18) <= 8) P8(L, 0x18) += 8;
                sign = (s8)-sign;
                first = 0;
                prev = 0;
            }
            if (P8(L, 0x18) < len) len -= P8(L, 0x18); else len = 0;
        } while (!wall && len != 0);
    }
    scratch_pop(26); scratch_pop(26);
    G8(0x6820) = 0;
}

void spl_lightning_bolt(void) {                                 // 41CF:2BB7: 1d6 per level, bolt 8 long (16 squares)
    u8 lv = spell_level(SPL);
    u8 dmg = roll_dice(6, lv), wall;
    spl_bolt_square(&wall, 4, dmg, (s8)G8(0x6dba), (s8)G8(0x6db9));
    spl_bolt_path(1, 4, dmg, 8);
}

// 41CF:2764  Haste and Slow: up to one creature per caster level on `side` is affected. A target already under
// the opposite effect (`undo`: Slow 2Ah for Haste, Haste 27h for Slow) has that cancelled instead and is left out.
// The rest receive the spell, and the speed-change event (12h) is raised for each.
void spl_speed(const u8 *msg, u8 side, u8 undo) {
    u8 m[41]; u8 n = msg[0] > 40 ? 40 : msg[0]; m[0] = n; memcpy(m + 1, msg + 1, n);
    G8(0x6820) = 1;
    u8 left = spell_level(SPL), count = G8(0x6c94);
    for (int k = 1; k <= count; k++) {
        farptr t = G32(0x6c91 + k * 4);
        if (P8(t, 0x10e) == side && left != 0) {
            left--;
            if (effect_cure(undo, G32(0x6c91 + k * 4))) SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + k * 4, 0);
        } else SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + k * 4, 0);
    }
    spell_affect_targets(m, 0, 0, 0, 0, SPL);
    count = G8(0x6c94);
    for (int k = 1; k <= count; k++) if (G32(0x6c91 + k * 4)) effects_event(G32(0x6c91 + k * 4), 0x12);
}
void spl_haste(void) { spl_speed(SPL_MSG(0x2888), P8(G32(0x5d92), 0x10e), 0x2a); }          // 41CF:2892
void spl_slow(void) { spl_speed(SPL_MSG(0x2bff), chr_on_party_side(G32(0x5d92)), 0x27); }    // 41CF:2C09
void spl_slow_poison(void) {                                    // 41CF:185B
    // Only for the poisoned (effect 37h): a poisoned character at 0 hit points is raised to 1, the spell's effect
    // is added, effect 4Eh is run once and effect 0Fh is added for 10.
    farptr t = G32(0x6c95);
    if (P8(t, 0x10c) == 1) { SETP32(FARPTR(DATA_SEG, 0), 0x6c95, 0); return; }
    if (!char_find_effect(0x37, t)) return;
    if (P8(t, 0x11b) == 0) P8(t, 0x11b) = 1;
    spell_affect_targets(SPL_MSG(0x184f), 0, 0, 1, 0xff, SPL);
    effect_run(1, 0, t, 0x4e);
    effect_add(1, 0xff, 10, 0x0f, t);
}
void spl_snake_charm(void) {                                    // 41CF:190F
    // Charms snakes (creature class 0Eh) whose hit points together fit within the caster's current hit points.
    G8(0x4844) = P8(G32(0x5d92), 0x11b);
    G8(0x6c94) = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104))
        if (P8(c, 0x9f) == 0x0e && P8(c, 0x11b) <= G8(0x4844)) {
            G8(0x4844) -= P8(c, 0x11b);
            G8(0x6c94)++;
            SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + G8(0x6c94) * 4, c);
        }
    spell_affect_targets(SPL_MSG(0x1904), 0, 0, 0, 0, SPL);
}

// 41CF:1F41  Strength: +1d4, +1d6 or +1d8 depending on the target's classes (levels at 9Bh; 96h or 9Ch; 98h, the
// last that applies winning). Above 18 only a fighter goes on, at ten percentile points per point (up to 18/00).
// For a target with none of those classes the original adds an uninitialised value; nothing is added here.
u8 spl_strength(void) {
    farptr t = G32(0x6c95);
    u8 bonus = 0, have = 0, pct = 0;
    if ((s8)P8(t, 0x9b) > 0) { bonus = roll_dice(4, 1); have = 1; }
    if ((s8)P8(t, 0x96) > 0 || (s8)P8(t, 0x9c) > 0) { bonus = roll_dice(6, 1); have = 1; }
    if ((s8)P8(t, 0x98) > 0) { bonus = roll_dice(8, 1); have = 1; }
    (void)have;
    u16 sum = (u16)(P8(t, 0x10) + bonus);
    u8 str = (u8)sum;
    if (str > 0x12) {
        if ((s8)P8(t, 0x98) >= 1) {
            pct = (u8)(P8(t, 0x16) + ((sum & 0xff) - 0x12) * 10);
            if (pct > 100) pct = 100;
        }
        str = 0x12;
    }
    u8 saved;
    if (rules_raise_strength(&saved, pct, str, t)) ui_char_message(1, 10, SPL_MSG(0x1f35), t);
    effect_add(1, saved, spell_duration(SPL), 0x26, t);
    return 1;
}

void spl_remove_curse(void) {                                   // 41CF:2540
    // Lifts Bestow Curse (effect 24h); failing that, the first cursed item the target carries stops being cursed.
    farptr t = G32(0x6c95);
    if (effect_cure(0x24, t)) { call_original(0x10ba, 0x1feb, 5, FP_SEG(t), FP_OFF(t), 1, RSEG(0x41cf), 0x251e); return; }
    u8 done = 0;
    for (farptr it = P32(t, 0xc8); it && !done; it = P32(it, 0x2a))
        if (P8(it, 0x36)) { done = 1; P8(it, 0x36) = 0; }
    if (done) call_original(0x10ba, 0x1feb, 5, FP_SEG(t), FP_OFF(t), 1, RSEG(0x41cf), 0x252b);
}

// 41CF:2385  Dispel Magic: every effect that is not permanent (byte 3 = FFh) is removed on a d100 roll at or under
// 50%, +5% per caster level above the effect's level (low nibble of byte 3), -2% per level below. The loop runs once
// per target, but every pass looks at the first target, so in the original only the first target is ever dispelled
// (fixed when FIX is on).
void spl_dispel(void) {
    G8(0x6820) = 1;
    u8 lv = spell_level(SPL), count = G8(0x6c94);
    for (int k = 1; k <= count; k++) {
        u8 any = 0;
        farptr t = FIX ? G32(0x6c91 + k * 4) : G32(0x6c95);          // (FIX: each target in turn)
        if (!t) continue;
        for (farptr node = P32(t, 0x7f); node; ) {
            farptr next = P32(node, 5);
            if (P8(node, 3) != 0xff) {
                G8(0x4845) = P8(node, 3) & 0x0f;
                if (G8(0x4845) < lv) G8(0x4844) = (u8)((lv - G8(0x4845)) * 5 + 0x32);
                else if (lv < G8(0x4845)) G8(0x4844) = (u8)(0x32 - (G8(0x4845) - lv) * 2);
                else G8(0x4844) = 0x32;
                if (roll_dice(100, 1) <= G8(0x4844)) { effect_remove(node, P8(node, 0), t); any = 1; }
            }
            node = next;
        }
        if (any) call_original(0x10ba, 0x1feb, 5, FP_SEG(t), FP_OFF(t), 1, RSEG(0x41cf), 0x2379);
    }
}

// 41CF:228A  Cures disease: effects 22h, 2Bh (with its companions 2Ch and 1Fh) and 32h (with 39h). DS:681F asks
// for the removals to be quiet. True if anything was cured.
u8 spl_cure_disease_on(void) {
    G8(0x681f) = 1;
    u8 a = effect_cure(0x22, G32(0x6c95));
    u8 b = effect_cure(0x2b, G32(0x6c95));
    if (b) { effect_remove(0, 0x2c, G32(0x6c95)); effect_remove(0, 0x1f, G32(0x6c95)); }
    u8 r = a || b;
    if (effect_cure(0x32, G32(0x6c95))) { r = 1; effect_remove(0, 0x39, G32(0x6c95)); }
    G8(0x681f) = 0;
    return r;
}
void spl_cure_disease(void) { spl_cure_disease_on(); }         // 41CF:232F

// 41CF:2C43  Restoration: undoes one level drain. The hit points held back for drained levels (75h, spread over
// 74h levels) are given back for one level, then the class whose next level needs the fewest experience points
// (table DS:409B, 8 classes x 14 longints) gains a level, unless the race limit (10BA:2F57) forbids it, and the
// experience is raised to that level's minimum if lower. When no class qualifies the original would use an
// uninitialised class index; here the spell then does nothing.
static s32 xp_needed(u8 cls, s8 lv) { return (s32)rd32(DS_LIN + (u16)(0x409b + (u16)(cls * 0x38) + (s16)(s8)(lv + 1) * 4)); }
static s8 restoration_pick(farptr t, s32 *best_out) {
    s8 cap = 0x0d, pick = -1; s32 best = 0x989680;
    for (u8 c = 0; c <= 7; c++) {
        s8 lv = (s8)P8(t, 0x96 + c);
        if (lv > 0 && lv <= cap) {
            s32 need = xp_needed(c, lv);
            if (need > 0 && need < best && !chr_level_capped(c, t)) { cap = lv; pick = (s8)c; best = need; }
        }
    }
    *best_out = best;
    return pick;
}
u8 spl_restoration(void) {
    farptr t = G32(0x6c95);
    if (P8(t, 0x74) == 0) return 1;
    s32 best; s8 pick = restoration_pick(t, &best);
    if (pick < 0) return 1;
    u8 per = (u8)(P8(t, 0x75) / P8(t, 0x74));
    P8(t, 0x32) += per; P8(t, 0x11b) += per; P8(t, 0xb1) += per; P8(t, 0x75) -= per; P8(t, 0x74)--;
    P8(t, 0x96 + pick)++;
    if ((s32)rd32(LIN(t) + 0xac) < best) wr32(LIN(t) + 0xac, (u32)best);
    call_original(0x454b, 0x0000, 2, FP_SEG(t), FP_OFF(t));
    ui_char_message(1, 10, SPL_MSG(0x2c37), t);                  // "is restored"
    return 1;
}

// 41CF:206F  Animate Dead: up to one corpse per caster level. Each dead human-type body (status 6, creature class 0)
// that can be placed back on the field rises on the caster's side as undead (class 4): quick-fighting, movement 6,
// its memorized spells gone, and a new morale value. It stands up at full hit points, and effect 20h records its
// old side and the caster level.
void spl_animate_dead(void) {
    G8(0x6820) = 1;
    u8 left = spell_level(SPL);
    G8(0x6c94) = 0;
    for (farptr c = G32(0x5d96); c && left > 0; c = P32(c, 0x104)) {
        if (P8(c, 0x10c) != 6 || P8(c, 0x9f) != 0) continue;
        if (!combat_place(1, combat_y(c), combat_x(c), c)) continue;
        u8 b3 = (u8)(P8(c, 0x10e) * 16 + spell_level(SPL));
        P8(c, 0x10e) = P8(G32(0x5d92), 0x10e);
        P8(c, 0x10f) = 1; P8(c, 0x76) = 2; P8(c, 0x6b) = 0; P8(c, 0x72) = 6;
        for (int i = 0; i <= 0x14; i++) P8(c, 0x17 + i) = 0;
        P8(c, 0x84) = P8(c, 0x84) < 0x80 ? 0xb3 : 0xb2;
        P8(c, 0x9f) = 4;
        if (G8(0x49f3) == 5) SETP32(P32(c, 0x108), 0x0a, 0);
        left--;
        if (rules_get_up(P8(c, 0x32), c)) {
            rules_spell_effect(SPL_MSG(0x2063), 0, 0, 1, b3, 0, 0x20, c);
            P8(c, 0x10c) = 1;
        }
    }
}

// 41CF:0A91  Choosing targets outside combat (entry 0 of the handler table). Spell byte 7 says whom: 1 the caster,
// 2 one character picked by the player ("Cast Spell on whom", remembered at DS:6DB5), 4 the whole party; anything
// else cannot be cast here.
void spell_pick_targets(farptr done, u8 noask, u8 spell) {
    (void)noask;
    if (G32(0x6db5) == 0) SETP32(FARPTR(DATA_SEG, 0), 0x6db5, G32(0x5d92));
    SETP32(FARPTR(DATA_SEG, 0), 0x6c95, G32(0x5d92));
    G8(0x6c94) = 1;
    P8(done, 0) = 1;
    u8 how = G8(0x3201 + spell * 16);
    if (how == 1) return;
    if (how == 2) {
        hud_draw_screen();
        static const u8 whom[] = "\x12" "Cast Spell on whom";
        chr_pick_character(FARPTR(DATA_SEG, 0x6db5), 1, whom);
        if (G32(0x6db5) == 0) { SETP32(FARPTR(DATA_SEG, 0), 0x6c95, 0); G8(0x6c94) = 0; P8(done, 0) = 0; }
        else SETP32(FARPTR(DATA_SEG, 0), 0x6c95, G32(0x6db5));
    } else if (how == 4) {
        SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + G8(0x6c94) * 4, G32(0x5d96));
        while (P32(G32(0x6c91 + G8(0x6c94) * 4), 0x104)) {
            farptr next = P32(G32(0x6c91 + G8(0x6c94) * 4), 0x104);
            SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + (G8(0x6c94) + 1) * 4, next);
            G8(0x6c94)++;
        }
    } else P8(done, 0) = 0;
}

// 10BA:1FEB  A spell's visible result on one combatant. In combat: a four-frame sparkle (pictures 16h for the big
// version, 17h otherwise) drawn over the combatant's square, with its sound, the message beside the combatant, the
// view scrolled to it if needed; the big version repeats the sparkle DS:49E2 more times, the small one waits for a
// key afterwards. Outside combat only the message.
void spell_sparkle(const u8 *msg, u8 big, farptr ch) {
    u8 m[41]; u8 n = msg[0] > 40 ? 40 : msg[0]; m[0] = n; memcpy(m + 1, msg + 1, n);
    if (G8(0x49f3) != 5) { ui_char_message(1, 10, m, ch); return; }
    sound_music(0);
    combat_missile_spin(big ? 0x16 : 0x17);
    if (!combat_visible(1, ch)) combat_step_view(8, 3, (s8)combat_y(ch), (s8)combat_x(ch));
    sound_play(G16(big ? 0x261c : 0x261a));
    ui_char_message(0, 10, m, ch);
    u8 reps = big ? G8(0x49e2) : 0;
    u8 slot = combat_slot(ch);
    s8 px = (s8)((s8)G8(0x604a + slot) * 3), py = (s8)((s8)G8(0x6092 + slot) * 3);
    for (u8 r = 0; ; r++) {
        for (u8 f = 0; f <= 3; f++) {
            gfx_merge_pic_clipped(G32(0x4a20), G32(0x6e36), 5, f, py, px);
            gfx_flush_back_buffer();
            rtl_delay(0x46);
            gfx_merge_pic_clipped(G32(0x4a20), G32(0x4a24), 0, 0, py, px);
        }
        if (r == reps) break;
    }
    sound_music(10);
    gfx_flush_back_buffer();
    if (reps == 0) text_pause();
}

// 10BA:1A29  Animates a missile (picture frames already in the work picture DS:6E36) from (sx, sy) to (tx, ty).
// The path is a Bresenham line in thirds of a square; its step directions are recorded first. The view (7 by 7
// squares) is centred between the two ends if both fit, otherwise it follows the missile, scrolling three squares
// at a time when it reaches an edge. The missile is drawn at every step when `delay` is set, otherwise only where
// it crosses a square's edge, cycling through `frames` pictures, and finally drawn on the target.
void combat_missile_anim(u8 delay, u8 frames, s8 ty, s8 tx, s8 sy, s8 sx) {
    u8 dirs[0x94]; memset(dirs, 8, sizeof dirs);
    farptr L = scratch_push(0x1a);
    memset(&P8(L, 0), 0, 0x1a);
    wr16(LIN(L), (u16)(sx * 3)); wr16(LIN(L) + 2, (u16)(sy * 3)); wr16(LIN(L) + 4, (u16)(tx * 3)); wr16(LIN(L) + 6, (u16)(ty * 3));
    line_begin(L);
    u8 n = 0, more;
    do { more = line_step(L); dirs[n] = P8(L, 0x19); n++; } while (more);
    scratch_pop(0x1a);
    u8 frame = 0;
    if (n <= 1) return;
    s8 dx = (s8)(tx - sx), dy = (s8)(ty - sy), vx, vy;
    #define MX ((s8)P8(G32(0x6716), 2))
    #define MY ((s8)P8(G32(0x6716), 3))
    u8 center;
    if (!combat_in_view((s8)(sy - MY), (s8)(sx - MX)) || !combat_in_view((s8)(ty - MY), (s8)(tx - MX))) {
        if (abs(dx) < 7 && abs(dy) < 7) { center = 1; vx = (s8)(sx + dx / 2); vy = (s8)(sy + dy / 2); }
        else { center = 0; vx = (s8)(MX + 3); vy = (s8)(MY + 3); }
    } else { center = 1; vx = (s8)(MX + 3); vy = (s8)(MY + 3); }
    combat_step_view(8, 0xff, vy, vx);
    u8 k = 0; s8 ox = 0, oy = 0;
    do {
        s8 csx = sx, csy = sy;
        s8 px = (s8)((sx - MX) * 3 + ox), py = (s8)((sy - MY) * 3 + oy);
        u8 stop = 0;
        do {
            if (delay || px % 3 == 0 || py % 3 == 0) {
                gfx_merge_pic_clipped(G32(0x4a20), G32(0x6e36), 5, frame, py, px);
                gfx_flush_back_buffer();
                rtl_delay(delay);
                gfx_merge_pic_clipped(G32(0x4a20), G32(0x4a24), 0, 0, py, px);
                frame++;
                if (frames <= frame) frame = 0;
            }
            k++;
            px = (s8)(px + (s8)G8(0x2736 + dirs[k]));
            py = (s8)(py + (s8)G8(0x273f + dirs[k]));
            if (px < 0 || px > 0x12 || py < 0 || py > 0x12) stop = 1;
            if (!stop && k < n) {
                ox = (s8)(ox + (s8)G8(0x2736 + dirs[k]));
                oy = (s8)(oy + (s8)G8(0x273f + dirs[k]));
                if (abs(ox) == 3) { s8 s = (s8)combat_sign(ox); sx = (s8)(sx + s); vx = (s8)(vx + s); ox = 0; }
                if (abs(oy) == 3) { s8 s = (s8)combat_sign(oy); sy = (s8)(sy + s); vy = (s8)(vy + s); oy = 0; }
            }
        } while (k < n && !stop);
        if (k < n) {
            s8 ddx = (s8)combat_sign(csx - sx), ddy = (s8)combat_sign(csy - sy);
            if (ddx) { if (sx < MX + 6) { if (sx <= MX) vx = (s8)(sx - 3); } else vx = (s8)(sx + 3); }
            if (ddy) { if (sy < MY + 6) { if (sy <= MY) vy = (s8)(sy - 3); } else vy = (s8)(sy + 3); }
            combat_step_view(8, 0xff, vy, vx);
        } else {
            center = 1;
            if (!combat_in_view((s8)(ty - MY), (s8)(tx - MX))) combat_step_view(8, 3, ty, tx);
            s8 fx = (s8)((tx - MX) * 3), fy = (s8)((ty - MY) * 3);
            gfx_merge_pic_clipped(G32(0x4a20), G32(0x6e36), 5, frame, fy, fx);
            if (delay) {
                gfx_flush_back_buffer();
                rtl_delay(delay);
                gfx_merge_pic_clipped(G32(0x4a20), G32(0x4a24), 0, 0, fy, fx);
            }
        }
    } while (!center);
    #undef MX
    #undef MY
    gfx_flush_back_buffer();
}

// 2E2A:20C3  Choosing targets in combat (entry 0 of the spell table during a fight). The low nibble of spell byte 6
// says how: 0 the caster; 0Fh one creature, or if a square was chosen instead, whoever stands on it (the radius
// computed is (nibble >> 4), always 0); 8 to 0Eh a point, with everyone within (byte & 7) squares of it; anything
// else up to (byte & 3) + 1 separate creatures, each chosen once ("Already been targeted"). Choosing a creature or
// point is still done by the original 2E2A:1E16.
u8 spell_pick_one(farptr out, u8 mode, u8 computer, u8 spell);
static u8 pick_one(farptr v, u8 mode, u8 quiet, u8 spell) {
    return spell_pick_one(v, mode, quiet, spell);
}
static void targets_from_reach(void) {
    u8 n = G8(0x671a);
    for (int k = 1; k <= n; k++) SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + k * 4, G32(0x65b9 + G8(0x6718 + k * 3) * 4));
    G8(0x6c94) = G8(0x671a);
    G8(0x6820) = 1;
}
void spell_pick_combat_targets(farptr done, u8 quiet, u8 spell) {
    farptr me = G32(0x5d92);
    P8(done, 0) = 1;
    G8(0x6c94) = 0;
    G8(0x6820) = 0;
    G8(0x6db9) = combat_x(me);
    G8(0x6dba) = combat_y(me);
    u8 how = G8(0x3200 + spell * 16) & 0x0f;
    farptr v = scratch_push(6);
    if (how == 0) {
        SETP32(FARPTR(DATA_SEG, 0), 0x6c95, me);
        G8(0x6c94) = 1;
    } else if (how == 0x0f) {
        if (!pick_one(v, 0, quiet, spell)) P8(done, 0) = 0;
        else if (P32(P32(G32(0x5d92), 0x108), 0x0a)) {
            SETP32(FARPTR(DATA_SEG, 0), 0x6c95, P32(P32(G32(0x5d92), 0x108), 0x0a));
            G8(0x6c94) = 1;
        } else {
            combat_reach(G32(0x6716), 1, 0xff, (u16)(how >> 4), (s8)G8(0x6dba), (s8)G8(0x6db9));
            targets_from_reach();
        }
    } else if (how >= 8 && how <= 0x0e) {
        if (!pick_one(v, 1, quiet, spell)) P8(done, 0) = 0;
        else {
            combat_reach(G32(0x6716), 1, 0xff, (u16)(G8(0x3200 + spell * 16) & 7), (s8)G8(0x6dba), (s8)G8(0x6db9));
            targets_from_reach();
        }
    } else {
        u8 left = (u8)((G8(0x3200 + spell * 16) & 3) + 1), n = 0;
        while (left) {
            if (!pick_one(v, 0, quiet, spell)) { left--; continue; }
            farptr t = P32(v, 0);
            u8 dup = 0;
            for (int k = 1; k <= n; k++) if (G32(0x6c91 + k * 4) == t) dup = 1;
            if (!dup) { n++; SETP32(FARPTR(DATA_SEG, 0), 0x6c91 + n * 4, t); left--; }
            else if (!quiet) { static const u8 m[] = "\x15" "Already been targeted"; ui_notice(m); }
            else left--;
            farptr last = G32(0x6c91 + n * 4);
            combat_redraw_square((s8)combat_y(last), (s8)combat_x(last));
        }
        G8(0x6c94) = n;
        if (n == 0) P8(done, 0) = 0;
    }
    scratch_pop(6);
}

// 2E2A:1E16  Choosing one creature or point for a spell. *out is 7 bytes: the creature, then its x and y. A player
// uses the targeting cursor (2E2A:3558, still original). The computer targets itself for spells that are not aimed
// (Cure Light Wounds then needs someone to heal); for aimed spells it makes up to 20 attempts with fight_pick_target
// and rejects a choice whose area (spell byte 0Fh) would include anyone on its own side, or a creature already
// incapacitated when the spell's effect is one of the four in DS:286C. On success the point is stored in DS:6DB9.
u8 spell_pick_one(farptr out, u8 mode, u8 computer, u8 spell) {
    farptr me = G32(0x5d92);
    u8 r = 0;
    if (!computer) {
        farptr res = scratch_push(2); P8(res, 0) = 0;
        fight_target_cursor(out, res, mode, 0, spell_range(spell), me);
        r = P8(res, 0);
        scratch_pop(2);
        SETP32(P32(G32(0x5d92), 0x108), 0x0a, P32(out, 0));
    } else if (G8(0x3208 + spell * 16) == 0) {
        SETP32(out, 0, G32(0x5d92));
        u8 ok = 1;
        if (spell == 3) { farptr t = fight_heal_target(G32(0x5d92)); SETP32(out, 0, t); ok = t != 0; }
        if (ok) { P8(out, 4) = combat_x(P32(out, 0)); P8(out, 5) = combat_y(P32(out, 0)); r = 1; }
    } else {
        for (u8 tries = 0x14; tries > 0 && !r; tries--) {
            u8 good = 1;
            if (!fight_pick_target(1, 0, spell_range(spell), G32(0x5d92))) continue;
            farptr t = P32(P32(G32(0x5d92), 0x108), 0x0a);
            if (G8(0x3209 + spell * 16) > 0) {
                combat_reach(G32(0x6716), 1, 0xff, G8(0x3209 + spell * 16), (s8)combat_y(t), (s8)combat_x(t));
                u8 n = G8(0x671a);
                for (int k = 1; k <= n; k++)
                    if (P8(G32(0x65b9 + G8(0x6718 + k * 3) * 4), 0x10e) != chr_on_party_side(G32(0x5d92))) good = 0;
            }
            if (chr_has_listed_effect(t))
                for (int k = 1; k <= 4; k++) if (G8(0x3204 + spell * 16) == G8(0x286b + k)) good = 0;
            if (good) {
                SETP32(out, 0, P32(P32(G32(0x5d92), 0x108), 0x0a));
                P8(out, 4) = combat_x(P32(out, 0));
                P8(out, 5) = combat_y(P32(out, 0));
                r = 1;
            }
        }
    }
    if (r) { G8(0x6db9) = P8(out, 4); G8(0x6dba) = P8(out, 5); }
    else memset(&P8(out, 0), 0, 7);
    return r;
}

// 2E2A:2998  May `attacker` go for `target`? Always for enemies and quick-fighting creatures; otherwise the player
// is asked "Attack Ally: ". Yes turns every neutral creature (status 0 with a morale value) hostile, notes in the
// area (word 666h) that the party attacked, and recounts the sides.
u8 fight_confirm_attack(farptr attacker, farptr target) {
    if (chr_on_party_side(attacker) == P8(target, 0x10e) || P8(target, 0x10f)) return 1;
    if (menu_yes_no(0x0f, 10, 0x0d, FARPTR(RSEG(0x2e2a), 0x298a)) != 'Y') return 0;
    G8(0x6de2) = 1;
    wr16(LIN(G32(0x49d6)) + 0x666, 1);
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104))
        if (P8(c, 0x10c) == 0 && P8(c, 0x84) > 0x7f) { P8(c, 0x10e) = 1; SETP32(P32(c, 0x108), 0x0a, 0); }
    chr_count_sides();
    return 1;
}

// 2E2A:3558  The player's targeting cursor. It lists every combatant in sight (2E2A:32E6: the reach list from the
// chooser's square, range 127) and moves between them (2E2A:3389), sliding the cursor picture (missile kind 19h)
// from one to the next. A small menu (2E2A:2AA0, still original) reads the keys: Next, Prev, Manual aim (2DFA),
// Target (2C3D), Centre the view, or leave (Esc or E). Combatants that cannot be attacked are skipped.
typedef struct { u8 count, idx; u8 slot[0x48]; s8 px, py; } TargetList;
static void tlist_fill(TargetList *L, farptr ch) {                                  // 2E2A:32E6
    combat_reach(G32(0x6716), combat_size(ch), 0xff, 0x7f, (s8)combat_y(ch), (s8)combat_x(ch));
    L->count = G8(0x671a);
    for (int k = 1; k <= L->count && k < 0x48; k++) L->slot[k] = G8(0x6718 + k * 3);
}
static farptr tlist_move(TargetList *L, u8 slide, s8 step) {                       // 2E2A:3389
    if (!slide) combat_redraw_square(L->py, L->px);
    else { L->px = (s8)G8(0x5f27 + L->slot[L->idx] * 4); L->py = (s8)G8(0x5f28 + L->slot[L->idx] * 4); }
    L->idx = (u8)(L->idx + step);
    if (L->idx == 0) L->idx = L->count;
    if (L->count < L->idx) L->idx = 1;
    farptr t = G32(0x65b9 + L->slot[L->idx] * 4);
    s8 cx = (s8)G8(0x5f27 + L->slot[L->idx] * 4), cy = (s8)G8(0x5f28 + L->slot[L->idx] * 4);
    if (slide) { combat_missile_anim(0, 1, cy, cx, L->py, L->px); L->px = cx; L->py = cy; }
    return t;
}
void fight_target_cursor(farptr out, farptr done, u8 p3, u8 p4, u8 reach, farptr ch) {
    TargetList L; memset(&L, 0, sizeof L);
    combat_missile_frame(0, 0, 0, 0x19);
    memset(&P8(out, 0), 0, 7);
    P8(done, 0) = 0;
    if (reach == 0xff) reach = P32(ch, 0xcc) ? (u8)(G8(0x558e + P8(P32(ch, 0xcc), 0x2e) * 16) - 1) : 1;
    if (reach == 0 || reach == 0xff) reach = 1;
    tlist_fill(&L, ch);
    L.idx = 1;
    s8 step = 0, next = 0;
    farptr t = tlist_move(&L, 1, step);
    step = 1;
    u8 key = ' ';
    while (P8(done, 0) == 0 && key != 0 && key != 'E') {
        if (!fight_can_target(t, ch)) { t = tlist_move(&L, 0, step); continue; }
        key = fight_target_menu(p3, p4, reach, t, ch);
        if (key == 'N') { step = 1; next = 1; }
        else if (key == 'P') { step = -1; next = -1; }
        else if (key == 'M') {
            fight_manual_aim(out, done, p3, p4, reach, t, ch);
            combat_missile_frame(0, 0, 0, 0x19);
            tlist_fill(&L, ch);
            next = 0;
        } else if (key == 'T') {
            fight_target_command(out, done, p4, t, ch);
            combat_missile_frame(0, 0, 0, 0x19);
            tlist_fill(&L, ch);
            next = 0;
        } else if (key == 'C') {
            combat_step_view(8, 0, (s8)combat_y(t), (s8)combat_x(t));
            next = 0;
        }
        combat_redraw_square((s8)combat_y(t), (s8)combat_x(t));
        u8 slide = (P8(done, 0) == 0 && key != 0 && key != 'E') ? 1 : 0;
        t = tlist_move(&L, slide, next);
    }
    if (p4) text_clear_box(0, 0x17, 0x27, 0x17);
}

// 2E2A:2AA0  The targeting cursor's menu, for `ch` looking at `t`: "Next Prev Manual [Target] Center Exit" after
// "Aim:". Target is offered only within reach: always for a spell (show_range 0), and for an attack only on someone
// else, by a hand-weapon user, or by a missile user with something to fire and no enemy adjacent (or a weapon
// usable either way, the "14" check). With show_range the distance is shown as "Range = n". Returns the key.
u8 fight_target_menu(u8 unused, u8 show_range, u8 reach, farptr t, farptr ch) {
    (void)unused;
    u8 tgt[41] = {0};
    u8 dist = combat_distance(t, ch);
    fight_direction(t, ch);
    if (show_range) {
        farptr b = scratch_push(256);
        u8 *s = &P8(b, 0);
        static const char rng[] = "Range = ";
        farptr num = scratch_push(256);
        chr_number_text(num, dist);
        int n = 8; memcpy(s + 1, rng, 8);
        int k = P8(num, 0); if (n + k > 255) k = 255 - n;
        memcpy(s + 1 + n, &P8(num, 1), k); n += k;
        if (n + 2 <= 255) { s[1 + n] = ' '; s[2 + n] = ' '; n += 2; }
        s[0] = (u8)n;
        scratch_pop(256);
        text_put_string(b, 10, 0x17, 0);
        scratch_pop(256);
    }
    static const u8 target[] = "\x07" "Target ";
    if (dist <= reach) {
        if (!show_range) memcpy(tgt, target, 8);
        else if (t != ch) {
            farptr ammo;
            if (!chr_has_missile_weapon(ch)) memcpy(tgt, target, 8);
            else if (chr_missile_ammo(&ammo, ch) && (combat_enemies_in_reach(1, ch) == 0 || chr_has_missile_weapon_14(ch))) memcpy(tgt, target, 8);
        }
    }
    u8 menu[41]; static const char a[] = "Next Prev Manual ", b2[] = "Center Exit";
    u8 tmp[256]; int n = 17; memcpy(tmp + 1, a, 17);
    memcpy(tmp + 1 + n, tgt + 1, tgt[0]); n += tgt[0];
    memcpy(tmp + 1 + n, b2, 11); n += 11;
    if (n > 40) n = 40;
    menu[0] = (u8)n; memcpy(menu + 1, tmp + 1, n);
    combat_redraw_one(1, 3, t);
    hud_combat_panel(t);
    static const u8 aim[] = "\x04" "Aim:";
    u8 idx = 0;
    return menu_horizontal(&idx, 0, 0, 0x0f, 10, 0x0d, menu, aim);
}

// 2E2A:2C3D  The cursor's Target command. In attack mode (mode 1) attacking someone not hostile needs "Attack Ally"
// confirmed. The target and its square go into *out, the view is re-centred, and in attack mode the attack happens
// at once: a fighter's sweep if it applies, otherwise a strike, with missiles fired if the weapon has ammunition
// (and not one usable in melee against an adjacent target).
void fight_target_command(farptr out, farptr done, u8 mode, farptr t, farptr ch) {
    P8(done, 0) = 1;
    if (mode == 1 && !fight_confirm_attack(t, ch)) P8(done, 0) = 0;
    if (P8(done, 0) == 0) { memset(&P8(out, 0), 0, 7); return; }
    SETP32(out, 0, t);
    P8(out, 4) = combat_x(t);
    P8(out, 5) = combat_y(t);
    farptr m = G32(0x6716);
    P8(m, 4) = 0;
    combat_step_view(8, 3, (s8)((s8)P8(m, 3) + 3), (s8)((s8)P8(m, 2) + 3));
    if (mode != 1) return;
    if (fight_sweep(t, ch)) { P8(done, 0) = chr_clear_action(ch); return; }
    fight_note_attacker(t, ch);
    farptr ammo = 0;
    if (chr_has_missile_weapon(ch) && chr_missile_ammo(&ammo, ch) && chr_has_missile_weapon_14(ch) && combat_distance(t, ch) == 1) ammo = 0;
    u8 d = P8(done, 0);
    fight_strike(&d, ammo, 0, t, ch);
    P8(done, 0) = d;
}

// 2E2A:1351  The weakest undead in sight that is not already fleeing: the lowest undead type (byte 76h, above 0).
u8 fight_find_undead(farptr *out, farptr ch) {
    s8 best = 0x0d; u8 r = 0;
    *out = 0;
    u8 n = combat_enemies_in_reach(0xff, ch);
    for (int k = 1; k <= n; k++) {
        farptr c = G32(0x65b9 + G8(0x6de4 + k) * 4);
        if (P8(P32(c, 0x108), 0x10) == 0 && (s8)P8(c, 0x76) > 0 && (s8)P8(c, 0x76) < best) { best = (s8)P8(c, 0x76); *out = c; r = 1; }
    }
    return r;
}

// 2E2A:116A  Turn Undead. 1d12 undead can be affected and one d20 is rolled for all of them; the table at DS:0447
// (ten columns per undead type, by cleric level 1-8, 9-13 or higher) gives the number needed, positive to turn, negative
// to destroy outright. Turned creatures flee; destroyed ones fall. The weakest are taken first; after a destruction
// up to six more may follow. A failed roll ends it; with nothing affected, "Nothing Happens...".
void fight_turn_undead(farptr ch) {
    static const u8 turns[] = "\x0f" "turns undead...", destroyed[] = "\x0c" "Is destroyed", nothing[] = "\x12" "Nothing Happens...";
    ui_char_message(0, 10, turns, ch);
    menu_clear_line();
    text_pause();
    u8 any = 0, stop = 0;
    P8(P32(ch, 0x108), 0x11) = 1;
    u8 extra = 6;
    u8 count = roll_dice(12, 1);
    u8 roll = roll_dice(20, 1);
    s8 lv = (s8)P8(ch, 0x96), col = (lv >= 1 && lv <= 8) ? lv : (lv >= 9 && lv <= 13) ? 9 : 10;
    farptr u;
    while (fight_find_undead(&u, ch) && count > 0 && !stop) {
        s8 v = (s8)G8(0x447 + (u16)((s16)(s8)P8(u, 0x76) * 10) + col);
        if ((s16)roll >= (v < 0 ? -v : v)) {
            any = 1;
            combat_redraw_one(0, 3, u);
            hud_combat_panel(u);
            if (v > 0) {
                P8(P32(u, 0x108), 0x10) = 1;
                call_original(0x10ba, 0x1feb, 5, FP_SEG(u), FP_OFF(u), 1, RSEG(0x2e2a), 0x1140);   // "is turned"
            } else {
                ui_char_message(0, 10, destroyed, u);
                combat_fall(u);
                P8(u, 0x10c) = 8;
                P8(u, 0x10d) = 0;
            }
            if (extra > 0) extra--;
            count--;
            if (count == 0 && extra > 0 && v < 0) count++;
            ui_clear_message();
        } else stop = 1;
    }
    if (!any) ui_notice(nothing);
    chr_count_sides();
    G8(0x6e31) = chr_clear_action(ch);
    ui_clear_message();
}

// 41CF:30D9  Effect 58h, a creature breathing lightning: at the start of a fight, and then half the time, it aims as
// for a Lightning Bolt (spell 33h) and "Breathes!": a bolt 10 long starting next to it (two squares away in the
// positive directions, a quirk of the original), doing its maximum hit points in damage (save 3 for half). The effect
// counts its uses in byte 3 and is removed when they run out.
void fx_breathe_lightning(u8 arg, farptr node, farptr ch) {
    (void)arg;
    if (G8(0x6de3) != 0 && roll_dice(100, 1) <= 0x32) return;
    s8 x = (s8)combat_x(ch), y = (s8)combat_y(ch);
    static const u8 m[] = "\x09" "Breathes!";
    ui_char_message(1, 10, m, ch);
    u32 h = spell_handler(0);
    emu_pcall((u16)(h >> 16), (u16)h, 4, 0x33, 1, DATA_SEG, 0x70c6);
    G8(0x6db9) = (u8)(x + (s8)combat_sign((s16)(s8)G8(0x6db9) - x));
    G8(0x6dba) = (u8)(y + (s8)combat_sign((s16)(s8)G8(0x6dba) - y));
    if ((s8)G8(0x6db9) == x + 1) G8(0x6db9)++;
    if ((s8)G8(0x6dba) == y + 1) G8(0x6dba)++;
    effects_remove_invisibility(ch);
    combat_missile_spin(0x13);
    combat_missile_anim(0x32, 4, (s8)G8(0x6dba), (s8)G8(0x6db9), y, x);
    u8 wall;
    spl_bolt_square(&wall, 3, P8(ch, 0x32), (s8)G8(0x6dba), (s8)G8(0x6db9));
    spl_bolt_path(0, 3, P8(ch, 0x32), 10);
    if (P8(node, 3) < 0xfe) effect_remove(node, 0x58, ch);
    else P8(node, 3)--;
    chr_clear_action(ch);
}

// 41CF:028D  Adds a spell to the list being built for a spell menu: a chain of 46-byte nodes from DS:4766 (text up
// to 40 characters, byte 29h = header line, next at 2Ah). A "Level n" header (DS:0A79) is inserted when the level
// changes; the entry is the spell's name after a two-character prefix (CS:028A, or CS:0287 for bit 7 set). The spell
// numbers are kept in order at DS:476A.
static void pstr_set40(farptr dst, const u8 *src) { u8 n = src[0] > 40 ? 40 : src[0]; P8(dst, 0) = n; memcpy(&P8(dst, 1), src + 1, n); }
void spell_list_add(u8 spell) {
    farptr p = G32(0x4766);
    u8 prev = 0, k = 0;
    if (!p) { p = rtl_getmem(0x2e); SETP32(FARPTR(DATA_SEG, 0), 0x4766, p); }
    else {
        k = 1;
        while (P32(p, 0x2a)) { if (P8(p, 0x29) == 0) k++; p = P32(p, 0x2a); }
        prev = G8(0x31fb + G8(0x4769 + k) * 16);
        farptr n = rtl_getmem(0x2e);
        SETP32(p, 0x2a, n);
        p = n;
    }
    SETP32(p, 0x2a, 0);
    u8 lv = G8(0x31fb + (spell & 0x7f) * 16);
    if (lv != prev) {
        pstr_set40(p, &G8(0x0a79 + lv * 0x29));
        P8(p, 0x29) = 1;
        farptr n = rtl_getmem(0x2e);
        SETP32(p, 0x2a, n);
        p = n;
        SETP32(p, 0x2a, 0);
    }
    pstr_set40(p, &mem[LIN(FARPTR(RSEG(0x41cf), spell < 0x80 ? 0x28a : 0x287))]);   // prefixes live in the code segment
    u8 t[256]; u8 a = P8(p, 0); memcpy(t + 1, &P8(p, 1), a);
    const u8 *nm = &G8(0x28e9 + (spell & 0x7f) * 0x29);
    int m = nm[0]; if (a + m > 255) m = 255 - a;
    memcpy(t + 1 + a, nm + 1, m); t[0] = (u8)(a + m);
    pstr_set40(p, t);
    P8(p, 0x29) = 0;
    G8(0x476a + k) = spell & 0x7f;
}

// 41CF:044E  Lists the spells stored in the current item (DS:6B80, three at 3Ch). Only those marked with bit 7 when
// `marked` is set, otherwise all. An item's power is unusable (byte 35h cleared) for a character under effect 10h, or
// for a cleric with an item of slot type 0Ch.
void spell_list_item(u8 marked) {
    farptr it = G32(0x6b80), sel = G32(0x5d92);
    if (char_find_effect(0x10, sel) || ((s8)P8(sel, 0x96) > 0 && G8(0x5582 + P8(it, 0x2e) * 16) == 0x0c)) P8(it, 0x35) = 0;
    if (P8(G32(0x6b80), 0x35) != 0) return;
    for (int k = 1; k <= 3; k++) {
        u8 v = P8(G32(0x6b80), 0x3b + k);
        if ((marked && v > 0x80) || (!marked && v != 0)) {
            spell_list_add(v);
            SETP32(FARPTR(DATA_SEG, 0), 0x477f + G8(0x4843) * 4, G32(0x6b80));
            G8(0x4843)++;
        }
    }
}

// 41CF:1B11  Stinking Cloud. Each casting gets a 19-byte heap record in the chain at DS:6DBB: caster (0), next (4),
// the tile found under each of the four squares (7+k), whether each square is covered (0Bh+k), the point (10h, 11h)
// and how many clouds this caster already has (12h). The four squares around the point (directions DS:290E..) that
// are on the map and not solid are covered with the cloud tile 1Eh; the tile remembered under a square that is already
// a cloud comes from the overlapping cloud's record, under a body from the fallen list. The caster gets effect 28h
// (which ends the cloud) and anyone standing in it makes the cloud check at once.
void spl_stinking_cloud(void) {
    G8(0x6820) = 1;
    u8 lv = spell_level(SPL), idx = 0;
    farptr rec = G32(0x6dbb);
    if (!rec) { rec = rtl_getmem(0x13); SETP32(FARPTR(DATA_SEG, 0), 0x6dbb, rec); }
    else {
        while (P32(rec, 4)) { if (P32(rec, 0) == G32(0x5d92)) idx++; rec = P32(rec, 4); }
        farptr n = rtl_getmem(0x13);
        SETP32(rec, 4, n);
        rec = n;
    }
    memset(&P8(rec, 0), 0, 0x13);
    effect_add(1, (u8)(lv + idx * 16), lv, 0x28, G32(0x5d92));
    SETP32(rec, 0, G32(0x5d92));
    P8(rec, 0x12) = idx;
    P8(rec, 0x10) = G8(0x6db9);
    P8(rec, 0x11) = G8(0x6dba);
    u8 who[5] = {0};
    for (int k = 1; k <= 4; k++) {
        u8 d = G8(0x290d + k), tile;
        s8 sx = (s8)((s8)G8(0x6db9) + (s8)G8(0x2736 + d)), sy = (s8)((s8)G8(0x6dba) + (s8)G8(0x273f + d));
        combat_square(&tile, &who[k], sy, sx);
        P8(rec, 0x0b + k) = (tile == 0 || G8(0x2744 + tile * 4) == 0xff) ? 0 : 1;
        if (tile == 0x1e) {
            for (farptr o = G32(0x6dbb); o; o = P32(o, 4)) {
                if (o == rec) continue;
                for (int f = 1; f <= 4; f++) {
                    u8 fd = G8(0x290d + f);
                    if (P8(o, 0x0b + f) && (s8)P8(o, 0x10) + (s8)G8(0x2736 + fd) == sx && (s8)P8(o, 0x11) + (s8)G8(0x273f + fd) == sy
                        && P8(o, 7 + f) != 0x1e) tile = P8(o, 7 + f);
                }
            }
        } else if (tile == 0x1f) {
            u8 n = G8(0x6715);
            for (int f = 1; f <= n; f++)
                if ((s8)G8(0x66da + f * 7) == sx && (s8)G8(0x66db + f * 7) == sy) tile = G8(0x66dc + f * 7);
        }
        P8(rec, 7 + k) = tile;
        if (P8(rec, 0x0b + k)) P8(G32(0x6716), (u16)(sy * 0x32 + sx + 7)) = 0x1e;
    }
    ui_char_message(0, 10, SPL_MSG(0x1af9), G32(0x5d92));
    combat_step_view(8, 0xff, (s8)G8(0x6dba), (s8)G8(0x6db9));
    text_pause();
    ui_clear_message();
    for (int k = 1; k <= 4; k++) if (who[k]) fight_cloud_check(G32(0x65b9 + who[k] * 4));
}

// 2E2A:2DFA  Manual aim: a free cursor moved with the eight direction keys over the 50 by 25 field, starting at `t`.
// For each square it traces the line of sight from the attacker and (when attacking) shows "Range = n"; the panel shows
// whoever stands there (or lies there, for a body). "Target" is offered only for a square in sight, within reach and not
// solid, holding an attackable creature (when attacking: not the attacker, not a body, and a missile user needs
// something to fire and no adjacent enemy unless the weapon also works in melee), or an empty square for spells that
// allow it (p3). Enter or T confirms (in attack mode through the Target command), Esc or E cancels, C centres the view.
void fight_manual_aim(farptr out, farptr done, u8 p3, u8 p4, u8 reach, farptr t, farptr ch) {
    memset(&P8(out, 0), 0, 7);
    s8 cx = (s8)combat_x(t), cy = (s8)combat_y(t);
    u8 key = ' ', dir = 8;
    P8(done, 0) = 0;
    P8(G32(0x6716), 4) = 1;
    P8(G32(0x6716), 5) = 1;
    while (key != 0 && key != 13 && key != 'E' && key != 'T') {
        combat_step_view(dir, 3, cy, cx);
        cx = (s8)(cx + (s8)G8(0x2736 + dir));
        cy = (s8)(cy + (s8)G8(0x273f + dir));
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx > 0x31) cx = 0x31;
        if (cy > 0x18) cy = 0x18;
        u8 tile, who;
        combat_square(&tile, &who, cy, cx);
        keys_flush();
        u8 ok = 0;
        farptr v = scratch_push(4);
        wr16(LIN(v), 0xff); P8(v, 2) = (u8)cy; P8(v, 3) = (u8)cx;
        u8 seen = line_of_sight(G32(0x6716), v, FARPTR(FP_SEG(v), FP_OFF(v) + 2), FARPTR(FP_SEG(v), FP_OFF(v) + 3), (s8)combat_y(ch), (s8)combat_x(ch));
        u16 dist = rd16(LIN(v));
        scratch_pop(4);
        if (seen) {
            ok = 1;
            if (p4) {
                farptr b = scratch_push(256), num = scratch_push(256);
                chr_number_text(num, (u16)(dist >> 1));
                u8 *s = &P8(b, 0); int n = 8; memcpy(s + 1, "Range = ", 8);
                int k = P8(num, 0); if (n + k > 255) k = 255 - n;
                memcpy(s + 1 + n, &P8(num, 1), k); n += k;
                if (n + 2 <= 255) { s[1 + n] = ' '; s[2 + n] = ' '; n += 2; }
                s[0] = (u8)n;
                scratch_pop(256);
                text_put_string(b, 10, 0x17, 0);
                scratch_pop(256);
            }
        } else if (p4) text_clear_box(0, 0x17, 0x27, 0x17);
        dist >>= 1;
        t = 0;
        if (ok) {
            if (who > 0) t = G32(0x65b9 + who * 4);
            else if (tile == 0x1f) {
                u8 n = G8(0x6715);
                for (int f = 1; f <= n; f++)
                    if ((s8)G8(0x66da + f * 7) == cx && (s8)G8(0x66db + f * 7) == cy) t = G32(0x66d6 + f * 7);
            }
        }
        if (t) hud_combat_panel(t); else text_clear_box(0x17, 1, 0x26, 0x15);
        if ((u16)reach < dist || G8(0x2744 + tile * 4) == 0xff) ok = 0;
        if (t) {
            if (!fight_can_target(t, ch)) ok = 0;
            if (p4 == 1) {
                if (ch == t || (who == 0 && tile == 0x1f)) ok = 0;
                else if (chr_has_missile_weapon(ch)) {
                    farptr ammo;
                    if (!chr_missile_ammo(&ammo, ch)) ok = 0;
                    else if (combat_enemies_in_reach(1, ch) > 0 && !chr_has_missile_weapon_14(ch)) ok = 0;
                }
            }
        } else if (p3 == 0) ok = 0;
        u8 items[41]; static const char ce[] = "Center Exit", tg[] = "Target ";
        if (ok) { items[0] = 18; memcpy(items + 1, tg, 7); memcpy(items + 8, ce, 11); }
        else { items[0] = 11; memcpy(items + 1, ce, 11); }
        static const u8 prompt[] = "\x12" "(Use Cursor keys) ";
        key = menu_horizontal(&G8(0x6e31), 0, 1, 0x0f, 10, 0x0d, items, prompt);
        if (key == 13 || key == 'T') {
            P8(G32(0x6716), 4) = 0;
            if (ok) {
                P8(out, 4) = (u8)cx; P8(out, 5) = (u8)cy;
                SETP32(out, 0, t);
                if (p4 == 1) fight_target_command(out, done, p4, P32(out, 0), ch);
                else P8(done, 0) = 1;
            }
            if (!ok || P8(done, 0) == 0) {
                combat_redraw_square(cy, cx);
                P8(done, 0) = 0;
                memset(&P8(out, 0), 0, 7);
            }
        } else if (key == 0x48) dir = 0;
        else if (key == 0x49) dir = 1;
        else if (key == 0x4d) dir = 2;
        else if (key == 0x51) dir = 3;
        else if (key == 0x50) dir = 4;
        else if (key == 0x4f) dir = 5;
        else if (key == 0x4b) dir = 6;
        else if (key == 0x47) dir = 7;
        else if (key == 0 || key == 'E') {
            combat_redraw_square(cy, cx);
            memset(&P8(out, 0), 0, 7);
            P8(done, 0) = 0;
        } else if (key == 'C') { combat_step_view(8, 0, cy, cx); dir = 8; }
        else dir = 8;
    }
}

// 2A9F:0CC3  Effect 28h ends: one of this caster's Stinking Clouds (number in the high nibble of byte 3) clears. "The
// air clears a little...": each square it covered gets back the tile that was under it, or the body tile if someone
// has fallen there since; the record is unlinked and freed, and the remaining clouds are painted again (they may
// overlap the squares just cleared).
void fx_cloud_clears(u8 arg, farptr node, farptr ch) {
    (void)arg;
    u8 idx = P8(node, 3) >> 4;
    farptr rec = G32(0x6dbb);
    while (rec && !(P32(rec, 0) == ch && P8(rec, 0x12) == idx)) rec = P32(rec, 4);
    if (!rec) return;
    static const u8 m[] = "\x1a" "The air clears a little...";
    ui_notice(m);
    for (int k = 1; k <= 4; k++) {
        if (!P8(rec, 0x0b + k)) continue;
        u8 d = G8(0x290d + k);
        u8 ex = (u8)((s8)P8(rec, 0x10) + (s8)G8(0x2736 + d)), ey = (u8)((s8)P8(rec, 0x11) + (s8)G8(0x273f + d));
        u8 body = 0, n = G8(0x6715);
        for (int f = 1; f <= n; f++)
            if (G32(0x66d6 + f * 7) && (int)(s8)G8(0x66da + f * 7) == (int)ex && (int)(s8)G8(0x66db + f * 7) == (int)ey) body = 1;
        P8(G32(0x6716), (u16)(ey * 0x32 + ex + 7)) = body ? 0x1f : P8(rec, 7 + k);
    }
    farptr p = G32(0x6dbb);
    if (p == rec) SETP32(FARPTR(DATA_SEG, 0), 0x6dbb, P32(rec, 4));
    else {
        while (P32(p, 4) != rec) p = P32(p, 4);
        SETP32(p, 4, P32(rec, 4));
    }
    rtl_freemem(rec, 0x13);
    for (farptr o = G32(0x6dbb); o; o = P32(o, 4))
        for (int k = 1; k <= 4; k++)
            if (P8(o, 0x0b + k)) {
                u8 d = G8(0x290d + k);
                s16 x = (s8)P8(o, 0x10) + (s8)G8(0x2736 + d), y = (s8)P8(o, 0x11) + (s8)G8(0x273f + d);
                P8(G32(0x6716), (u16)(y * 0x32 + x + 7)) = 0x1e;
            }
}

// 2588:031A  A party member's turn under the player's control. A spell begun earlier is finished first. Otherwise
// the combat menu (2588:0666) is shown until an action ends the turn: Move (09DF), View or Use (the item screens,
// after which attacks are recounted), Aim (the targeting cursor, as an attack), Cast, Turn undead, Quick (the computer
// takes over this character: 122B, then a pause and fight_ai_decide), Done (0E45), 2 (Magic on/off for quick fight),
// Space (everyone back under player control), Ctrl-P (end the turn and put everyone on quick), and "-" (2E2A:3B93,
// "That doesn't work" when it fails).
void fight_player_turn(farptr ch) {
    if (P8(ch, 0x10d) == 0) { chr_clear_action(ch); return; }
    farptr act = P32(ch, 0x108);
    farptr v = scratch_push(10);                 // done (0), key (1), aim result (2..8)
    if (P8(act, 0) != 0) {
        u8 spell = P8(act, 0);
        P8(act, 0) = 0;
        spell_cast(v, 1, 0, spell);
        chr_clear_action(ch);
        scratch_pop(10);
        return;
    }
    P8(v, 0) = 0;
    farptr done = v, key = FARPTR(FP_SEG(v), FP_OFF(v) + 1), aim = FARPTR(FP_SEG(v), FP_OFF(v) + 2);
    while (P8(done, 0) == 0) {
        u8 k = fight_combat_menu(ch);
        P8(key, 0) = k;
        if (k == 'M') fight_move_command(done, ch);
        else if (k == 'V' || k == 'U') {
            call_original(0x3c10, k == 'V' ? 0x0ab3 : 0x0ef8, 2, FP_SEG(done), FP_OFF(done));
            fight_set_attacks(ch);
            if (P8(done, 0) == 0) call_original(0x10ba, 0x2b88, 0);
        } else if (k == 'A') fight_target_cursor(aim, done, 0, 1, 0xff, ch);
        else if (k == 'C') fight_cast_command(done, 0, 0);
        else if (k == 'T') { fight_turn_undead(ch); P8(done, 0) = chr_clear_action(ch); }
        else if (k == 'Q') {
            fight_set_quick(ch);
            menu_clear_line();
            keys_flush();
            rtl_delay(200);
            P8(done, 0) = 1;
            fight_ai_decide(ch);
        } else if (k == 'D') call_original(0x2588, 0x0e45, 4, FP_SEG(ch), FP_OFF(ch), FP_SEG(done), FP_OFF(done));
        else if (k == '2') {
            G8(0x6e30) = G8(0x6e30) == 0;
            ui_notice(&mem[LIN(FARPTR(RSEG(0x2588), G8(0x6e30) ? 0x02f5 : 0x02fe))]);   // "Magic On" / "Magic Off"
        } else if (k == ' ') {
            for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) if (P8(c, 0x84) < 0x80) P8(c, 0x10f) = 0;
        } else if (k == 0x10) {
            P8(act, 3) = 0x14;
            for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) fight_set_quick(c);
            menu_clear_line();
            rtl_delay(200);
            P8(done, 0) = 1;
        } else if (k == '-') {
            if (!fight_sting_kill()) ui_notice(&mem[LIN(FARPTR(RSEG(0x2588), 0x0308))]);   // "That doesn't work"
            else { combat_redraw_one(0, 3, ch); P8(done, 0) = 1; }
        }
        if (P8(done, 0) == 0) { combat_redraw_one(1, 2, ch); hud_combat_panel(ch); }
    }
    scratch_pop(10);
}

// 2588:0666  The combat menu for a party member under the player's control: "Move " (if movement is left) "View Aim "
// "Use " (if carrying anything) "Cast " (with a spell memorized, casting allowed this round and the area permitting
// it, word 1CAh = 0) "Turn " (a cleric who has not turned yet this fight) "Quick Done". Raises effect event 0Fh first.
// Extended keys other than Ctrl-P, Ctrl-S, "-" and "2" are ignored; the menu repeats until one of the accepted keys.
static void pcat40(u8 *dst, const u8 *src) { int n = dst[0], k = src[0]; if (n + k > 40) k = 40 - n; if (k > 0) { memcpy(dst + 1 + n, src + 1, k); dst[0] = (u8)(n + k); } }
u8 fight_combat_menu(farptr ch) {
    effects_event(ch, 0x0f);
    farptr act = P32(ch, 0x108);
    #define CSTR(o) (&mem[LIN(FARPTR(RSEG(0x2588), (o)))])
    u8 items[41] = {0};
    if (P8(act, 6)) pcat40(items, CSTR(0x5f9));
    pcat40(items, CSTR(0x5ff));
    if (P8(ch, 0xc7)) pcat40(items, CSTR(0x609));
    u8 special = 0;
    for (int i = 0; i <= 0x14; i++) if (P8(ch, 0x17 + i)) special = 1;
    if (special && P8(act, 1) && P16(G32(0x49d2), 0x1ca) == 0) pcat40(items, CSTR(0x60e));
    if ((s8)P8(ch, 0x96) > 0 && P8(act, 0x11) == 0) pcat40(items, CSTR(0x614));
    pcat40(items, CSTR(0x61a));
    static const u8 accept[] = { 0x10, 0x13, ' ', '-', '2', 'A', 'C', 'D', 'M', 'Q', 'T', 'U', 'V' };
    u8 key, ok;
    do {
        key = menu_horizontal(&special, 0, 1, 0x0f, 10, 0x0d, items, CSTR(0x625));
        if (special && !(key == 0x10 || key == 0x13 || key == '-' || key == '2')) key = 0;
        ok = 0;
        for (unsigned i = 0; i < sizeof accept; i++) if (key == accept[i]) ok = 1;
    } while (!ok);
    #undef CSTR
    return key;
}

// 2588:0D45  Moving into another combatant attacks it, unless the attacker holds a missile weapon that cannot be used
// in melee; attacking someone not hostile needs "Attack Ally" confirmed. A fighter's sweep ends the turn by itself.
void fight_bump_attack(farptr done, farptr t, farptr ch) {
    if (chr_has_missile_weapon(ch) && !chr_has_missile_weapon_14(ch)) { ui_notice(&mem[LIN(FARPTR(RSEG(0x2588), 0x0d30))]); return; }
    if (!fight_confirm_attack(t, ch)) return;
    SETP32(P32(ch, 0x108), 0x0a, t);
    if (fight_sweep(t, ch)) { P8(done, 0) = 1; return; }
    fight_note_attacker(t, ch);
    u8 d = P8(done, 0);
    fight_strike(&d, 0, 0, t, ch);
    P8(done, 0) = d;
}

// 2588:09DF  The Move command: the eight direction keys step (moving into someone attacks it, off the field offers
// "Flee:", terrain costing more than the movement left is "Blocked"); Esc undoes the whole move and puts the character
// back where it started, facing as before; Enter ends moving. The prompt shows the movement left in squares.
void fight_move_command(farptr done, farptr ch) {
    farptr act = P32(ch, 0x108);
    u8 saved_move = P8(act, 6), saved_face = P8(act, 9);
    u8 sx = combat_x(ch), sy = combat_y(ch);
    u8 key = ' ', dir = 8;
    P8(done, 0) = 0;
    while (P8(act, 6) > 1 && key != 0 && key != 13) {
        text_clear_box(0x18, 0x18, 0x27, 0x18);
        u8 prompt[256]; static const char head[] = "Move/Attack, Move Left = ";
        farptr num = scratch_push(256);
        chr_number_text(num, P8(act, 6) >> 1);
        int n = 25; memcpy(prompt + 1, head, n);
        int k = P8(num, 0); if (n + k > 255) k = 255 - n;
        memcpy(prompt + 1 + n, &P8(num, 1), k); n += k;
        if (n < 255) prompt[1 + n++] = ' ';
        prompt[0] = (u8)n;
        scratch_pop(256);
        static const u8 none[] = "";
        key = menu_horizontal(&G8(0x6e31), 0, 1, 0x0f, 10, 10, none, prompt);
        if (key == 0) {
            P8(act, 6) = saved_move;
            combat_draw_ground(combat_slot(ch), 0, 0);
            P8(done, 0) = combat_place(0, sy, sx, ch) == 0;
            combat_step_view(8, 0, (s8)combat_y(ch), (s8)combat_x(ch));
            P8(act, 9) = saved_face;
            dir = 8;
        } else if (key == 'H') dir = 0;
        else if (key == 'I') dir = 1;
        else if (key == 'M') dir = 2;
        else if (key == 'Q') dir = 3;
        else if (key == 'P') dir = 4;
        else if (key == 'O') dir = 5;
        else if (key == 'K') dir = 6;
        else if (key == 'G') dir = 7;
        else dir = 8;
        if (dir >= 8) continue;
        combat_face(0, 0, dir, ch);
        u8 tile, who;
        combat_look_ahead(&tile, &who, dir, ch);
        if (who) { fight_bump_attack(done, G32(0x65b9 + who * 4), ch); continue; }
        if (tile == 0) {
            u8 a = menu_yes_no(0x0f, 10, 0x0d, FARPTR(RSEG(0x2588), 0x09d1));     // "Flee:"
            if (a == 'Y') P8(done, 0) = fight_escape(ch);
            else if (a == 'N') P8(done, 0) = 0;
        } else if (P8(act, 6) < G8(0x2744 + tile * 4)) ui_notice(&mem[LIN(FARPTR(RSEG(0x2588), 0x09d7))]);   // "Blocked"
        else {
            fight_free_attacks(dir, ch);
            if (P8(ch, 0x10d) == 0) P8(done, 0) = chr_clear_action(ch);
            else {
                fight_step(dir, ch);
                if (P8(ch, 0x10d) == 0) P8(done, 0) = chr_clear_action(ch);
                fight_cloud_check(ch);
                if (chr_has_listed_effect(ch)) P8(done, 0) = chr_clear_action(ch);
            }
        }
    }
    if (P8(act, 6) < 2) P8(act, 6) = 0;
}

// 2588:0FFF  Is an ally dying (status 5, a body, party side)? With `do_it`, the first one found is bandaged: back to
// unconscious (status 4), no longer bleeding, "is bandaged".
u8 fight_bandage(u8 do_it) {
    u8 found = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        if (P8(P32(c, 0x108), 0x13) != 0 || P8(c, 0x10e) != 0 || P8(c, 0x10c) != 5) continue;
        found = 1;
        if (!do_it) continue;
        P8(c, 0x10c) = 4;
        P8(P32(c, 0x108), 0x0e) = 0;
        ui_char_message(1, 10, &mem[LIN(FARPTR(RSEG(0x2588), 0x0ff3))], c);   // "is bandaged"
        do_it = 0;
    }
    return found;
}

// 2588:122B  Quick: the computer takes over this combatant; a target on its own side is forgotten.
void fight_set_quick(farptr ch) {
    P8(ch, 0x10f) = 1;
    farptr t = P32(P32(ch, 0x108), 0x0a);
    if (t && P8(t, 0x10e) == P8(ch, 0x10e)) SETP32(P32(ch, 0x108), 0x0a, 0);
}

// 2588:0E45  The Done menu: "Guard " (unless holding a missile weapon that cannot be used in melee) "Delay Quit "
// "Bandage " (when an ally is dying) "Speed Exit". Delay ends the turn keeping one action for later; Quit ends it;
// Bandage helps the first dying ally and ends the turn; Speed (2588:10F6, still original) sets the game speed.
void fight_done_menu(farptr done, farptr ch) {
    #define CSTR(o) (&mem[LIN(FARPTR(RSEG(0x2588), (o)))])
    P8(done, 0) = 0;
    u8 items[41] = {0};
    if (!chr_has_missile_weapon(ch) || chr_has_missile_weapon_14(ch)) pcat40(items, CSTR(0x0dfe));
    pcat40(items, CSTR(0x0e05));
    if (fight_bandage(0)) pcat40(items, CSTR(0x0e11));
    pcat40(items, CSTR(0x0e1a));
    u8 key = ' ';
    while (key != 0 && P8(done, 0) == 0) {
        key = menu_horizontal(&G8(0x6e31), 0, 0, 0x0f, 10, 0x0d, items, CSTR(0x0e26));
        if (key == 'G') P8(done, 0) = chr_guard(ch);
        else if (key == 'D') { P8(P32(ch, 0x108), 3) = 1; P8(done, 0) = 1; }
        else if (key == 'Q') P8(done, 0) = chr_clear_action(ch);
        else if (key == 'B') { P8(done, 0) = fight_bandage(1); P8(done, 0) = chr_clear_action(ch); }
        else if (key == 'S') fight_speed_menu();
    }
    #undef CSTR
}

// 2588:0131  Who acts next: the combatant with the most actions left (action byte 3); a d100 is rolled for every
// combatant in turn and settles ties (the later of equals wins on an equal or higher roll). Nil when nobody has any.
farptr fight_next_actor(void) {
    u8 best = 0, best_roll = 0; farptr pick = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        u8 r = roll_dice(100, 1);
        s8 a = (s8)P8(P32(c, 0x108), 3);
        if ((int)best < (int)a) best_roll = r;
        if ((int)best <= (int)a && best_roll <= r) { best = (u8)a; pick = c; best_roll = r; }
    }
    return best ? pick : 0;
}

// 2588:01F6  One combatant's turn: attackers and guarding reset, effect event 7; if it still has actions (20 means
// "all", lowered to 19), it becomes the selected character, is highlighted and recalculated, event 0Fh, and then the
// player or the computer acts for it.
void fight_take_turn(farptr ch) {
    farptr act = P32(ch, 0x108);
    P8(act, 0x0f) = 0; P8(act, 0x12) = 0; P8(act, 7) = 0;
    effects_event(ch, 7);
    if ((s8)P8(act, 3) <= 0) return;
    if (P8(act, 3) == 0x14) P8(act, 3) = 0x13;
    SETP32(FARPTR(DATA_SEG, 0), 0x5d92, ch);
    combat_redraw_one(1, 2, ch);
    chr_recalculate(ch);
    hud_combat_panel(ch);
    effects_event(ch, 0x0f);
    if ((s8)P8(act, 3) > 0) { if (P8(ch, 0x10f) == 0) fight_player_turn(ch); else fight_ai_decide(ch); }
    combat_redraw_square((s8)combat_y(ch), (s8)combat_x(ch));
}

// 2588:0880  The end of a round: a round's time passes, the round counter goes up, then for everyone effect event
// 13h and the cloud check; the dying bleed (dead after ten rounds). "Your Teammate is Dying" if one is. The sides are
// recounted and the view centred; the fight is over when a side is gone, unless the enemies are gone and the player
// answers yes to "Continue Battle:" (not offered where DS:49F4 is set).
void fight_end_round(farptr done) {
    time_advance(1, 1);
    G8(0x6de3)++;
    call_original(0x2e2a, 0x2879, 0);
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        effects_event(c, 0x13);
        fight_cloud_check(c);
        if (P8(c, 0x10c) == 5 && ++P8(P32(c, 0x108), 0x0e) > 9) P8(c, 0x10c) = 6;
    }
    if (fight_bandage(0)) ui_notice(&mem[LIN(FARPTR(RSEG(0x2588), 0x0858))]);
    chr_count_sides();
    combat_step_view(8, 0xff, (s8)((s8)P8(G32(0x6716), 3) + 3), (s8)((s8)P8(G32(0x6716), 2) + 3));
    if (G8(0x6814) == 0 || G8(0x6815) == 0) P8(done, 0) = 1;
    if (G8(0x6814) != 0 && G8(0x6815) == 0 && G8(0x49f4) == 0)
        if (menu_yes_no(0x0f, 10, 0x0d, FARPTR(RSEG(0x2588), 0x086f)) == 'Y') P8(done, 0) = 0;
}

// 2588:0000  After a fight: the clouds, the map and the missile work picture are freed, the graphics mode restored,
// and the spell targeting entry set back to the one used outside combat.
void fight_cleanup(void) {
    while (G32(0x6dbb)) { farptr n = P32(G32(0x6dbb), 4); rtl_freemem(G32(0x6dbb), 0x13); SETP32(FARPTR(DATA_SEG, 0), 0x6dbb, n); }
    rtl_freemem(G32(0x6716), 0x4e9);
    SETP32(FARPTR(DATA_SEG, 0), 0x6716, 0);
    gfx_free_pic(FARPTR(DATA_SEG, 0x6e36));
    combat_gfx_mode_b();
    wr16(DS_LIN + 0x6b84, 0x34); wr16(DS_LIN + 0x6b86, RSEG(0x1096));   // a relocated segment constant in the original
}

// 2E2A:0000  A combatant's start of round: its action record (+108h) is reset (+0 0, +1 and +2 may cast and use
// items, +8 0, +4 attack kind 2), its attacks counted (2E2A:0D23), its movement rate (+A2h, after its effects' say,
// event 12h) halved or not into +114h, +5 from +6Bh, initiative +3 = d6 + Dexterity adjustment for the living
// (at least 1; 6 less for a side that hesitates, the area's flags +596h; out of 0..20 means 0), +6 movement left.
// 2E2A:000C and 2E2A:0058 are pieces of it that Ghidra lists separately.
void fight_round_start(farptr ch) {
    farptr act = P32(ch, 0x108);
    P8(act, 0) = 0; P8(act, 1) = 1; P8(act, 2) = 1; P8(act, 8) = 0; P8(act, 4) = 2;
    fight_set_attacks(ch);
    G8(0x681a) = P8(ch, 0xa2); G8(0x681d) = 0;
    effects_event(ch, 0x12);
    P8(ch, 0x114) = fight_half(G8(0x681a));
    P8(act, 5) = P8(ch, 0x6b);
    if (P8(ch, 0x10d)) {
        s8 dex = (s8)chr_dex_missile(ch);
        P8(act, 3) = (u8)(roll_dice(6, 1) + dex);
        if ((s8)P8(act, 3) < 1) P8(act, 3) = 1;
        if ((u8)(((s8)P8(ch, 0x10e) + 1) & P16(G32(0x49d6), 0x596))) P8(act, 3) = (u8)(P8(act, 3) - 6);
        if ((s8)P8(act, 3) < 0 || (s8)P8(act, 3) > 0x14) P8(act, 3) = 0;
    } else P8(act, 3) = 0;
    P8(act, 6) = fight_movement(ch);
}

// 2588:007A  A whole fight: combat mode, the spell targeting entry switched to the combat one, the battle set up;
// then round after round until a side is gone: the sides counted, every combatant's start-of-round upkeep
// (2E2A:0000), and turns taken in order of actions left until nobody has any; then the end of the round.
void fight_battle(void) {
    G8(0x49f3) = 5;
    wr16(DS_LIN + 0x6b84, 0x66); wr16(DS_LIN + 0x6b86, RSEG(0x1068));
    bf_battle_begins();
    farptr d = scratch_push(2);
    P8(d, 0) = (G8(0x6814) == 0 || G8(0x6815) == 0) ? 1 : 0;
    emu_note("battle");
    while (P8(d, 0) == 0) {
        chr_count_sides();
        emu_note("round");
        for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) fight_round_start(c);
        wr16(LIN(G32(0x49d6)) + 0x596, 0);
        for (farptr c = fight_next_actor(); c; c = fight_next_actor()) fight_take_turn(c);
        fight_end_round(d);
    }
    scratch_pop(2);
    fight_cleanup();
    emu_note("battle-over");
}

// 2588:10F6  The Speed menu (from Done): "GameSpeed (n) :" with "Slower" (up to 9) and "Faster" (down to 0); the
// game speed is DS:49E2. Esc or Exit leaves.
void fight_speed_menu(void) {
    #define CSTR(o) (&mem[LIN(FARPTR(RSEG(0x2588), (o)))])
    u8 key = ' ';
    while (key != 0 && key != 'E') {
        u8 prompt[41]; u8 t[256];
        farptr num = scratch_push(256);
        chr_number_text(num, G8(0x49e2));
        const u8 *a = CSTR(0x10cf), *b = CSTR(0x10db);
        int n = a[0]; memcpy(t + 1, a + 1, n);
        int k = P8(num, 0); if (n + k > 255) k = 255 - n; memcpy(t + 1 + n, &P8(num, 1), k); n += k;
        k = b[0]; if (n + k > 255) k = 255 - n; memcpy(t + 1 + n, b + 1, k); n += k;
        scratch_pop(256);
        if (n > 40) n = 40;
        prompt[0] = (u8)n; memcpy(prompt + 1, t + 1, n);
        u8 items[41] = {0};
        pcat40(items, CSTR(0x10df));
        if (G8(0x49e2) < 9) pcat40(items, CSTR(0x10e1));
        if (G8(0x49e2) != 0) pcat40(items, CSTR(0x10e9));
        pcat40(items, CSTR(0x10f1));
        key = menu_horizontal(&G8(0x6e31), 0, 0, 0x0f, 10, 0x0d, items, prompt);
        if (key == 'S') G8(0x49e2)++;
        else if (key == 'F') G8(0x49e2)--;
    }
    #undef CSTR
}

// 41CF:3413  Setting up the spell handler table: DS:6B84 + n*4 gets a far pointer to the n-th spell's handler stub
// (segment 1096, which the loader relocates), the last one goes to DS:6988, and the casting state is reset.
void spell_table_init(void) {
    static const u16 tab[][2] = { {0x6b84,0x034}, {0x6b88,0x057}, {0x6b8c,0x05c}, {0x6b90,0x061}, {0x6b94,0x066}, {0x6b98,0x06b}, {0x6b9c,0x070}, {0x6ba0,0x070}, {0x6ba4,0x075}, {0x6ba8,0x07a}, {0x6bac,0x07f}, {0x6bb0,0x06b}, {0x6bb4,0x084}, {0x6bb8,0x089}, {0x6bbc,0x08e}, {0x6bc0,0x093}, {0x6bc4,0x070}, {0x6bc8,0x070}, {0x6bcc,0x06b}, {0x6bd0,0x098}, {0x6bd4,0x09d}, {0x6bd8,0x0a2}, {0x6bdc,0x06b}, {0x6be0,0x0a7}, {0x6be4,0x0ac}, {0x6be8,0x0b1}, {0x6bec,0x0b6}, {0x6bf0,0x0bb}, {0x6bf4,0x0c0}, {0x6bf8,0x06b}, {0x6bfc,0x0c5}, {0x6c00,0x0ca}, {0x6c04,0x0cf}, {0x6c08,0x0d4}, {0x6c0c,0x0d9}, {0x6c10,0x0de}, {0x6c14,0x0e3}, {0x6c18,0x0e8}, {0x6c1c,0x0ed}, {0x6c20,0x0f2}, {0x6c24,0x0f7}, {0x6c28,0x0fc}, {0x6c2c,0x101}, {0x6c30,0x04d}, {0x6c34,0x106}, {0x6c38,0x10b}, {0x6c3c,0x0fc}, {0x6c40,0x110}, {0x6c44,0x115}, {0x6c48,0x0a7}, {0x6c4c,0x0c5}, {0x6c50,0x11a}, {0x6c54,0x070}, {0x6c58,0x070}, {0x6c5c,0x070}, {0x6c60,0x11f}, {0x6c64,0x124}, {0x6c68,0x129}, {0x6c6c,0x12e}, {0x6c70,0x133}, {0x6c74,0x138}, {0x6c78,0x13d}, {0x6c7c,0x142}, {0x6c80,0x147}, {0x6c84,0x110}, {0x6c88,0x14c}, {0x6c8c,0x151}, {0x6c90,0x156}, {0x6988,0x15b} };
    for (unsigned i = 0; i < sizeof tab / sizeof tab[0]; i++) SETP32(FARPTR(DATA_SEG, 0), tab[i][0], FARPTR(RSEG(0x1096), tab[i][1]));
    G8(0x681f) = 0; G8(0x6dbf) = 0; SETG32(0x6db5, 0); G8(0x6821) = 1;
}

// 41CF:328A  A scroll spell used: the matching spell slot (3Ch-3Eh, last match) is cleared and the count in 30h
// drops; a scroll left with none (below D2h) is gone.
void scroll_spell_used(u8 spell, farptr item, farptr ch) {
    u8 slot = 0;
    for (u8 k = 1; k <= 3; k++) if ((P8(item, k + 0x3b) & 0x7f) == (spell & 0x7f)) slot = k;
    if (!slot) return;
    P8(item, slot + 0x3b) = 0;
    P8(item, 0x30)--;
    if (P8(item, 0x30) < 0xd2) item_lose(item, ch);
}
// 10BA:2B88  Preparing the combat screen for a spell or item outside the combat menu: graphics mode, the two frames,
// the bottom lines cleared, and the battle view centred again (4AFB:0996).
void combat_prep_view(void) {
    call_original(0x4afb, 0x00e3, 0);
    ui_frame(FARPTR(RSEG(0x10ba), 0x2b87), 0, 0, 0x15, 0x15, 1, 1);
    ui_frame(FARPTR(RSEG(0x10ba), 0x2b87), 0, 0, 0x15, 0x26, 1, 0x17);
    text_clear_box(0, 0x17, 0x27, 0x18);
    farptr m = G32(0x6716);
    call_original(0x4afb, 0x0996, 4, (u16)((s8)P8(m, 2) + 3), (u16)((s8)P8(m, 3) + 3), 0xff, 8);
}
// 2E2A:2879  The enemies' remaining strength in percent (DS:6E2F): their current hit points (of those still in the
// fight) against all their maximums, in fifths... as (cur*20/max)*5.
void enemy_strength(void) {
    u16 cur = 0, max = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        if (P8(c, 0x10e) != 1) continue;
        if (P8(c, 0x10d)) cur = (u16)(cur + P8(c, 0x11b));
        max = (u16)(max + P8(c, 0x32));
    }
    if (max) G8(0x6e2f) = (u8)((u8)((u32)(u16)(cur * 0x14) / max) * 5);
}

// ---- The monster-only spells (table entries 57-67, 41CF:2E02..30A2). The target is DS:6C95, the spell DS:681B. ----
#define MS(o) (&mem[LIN(FARPTR(RSEG(0x41cf), (o)))])
void spl_m_speed(void) {                                        // 41CF:2E02: unless a Slow (2Ah) was cured instead
    if (!effect_cure(0x2a, G32(0x6c95))) spell_affect_targets(MS(0x2df8), 0, 0, 0, 0, G8(0x681b));   // "is Speedy"
}
void spl_m_heal(void) {                                         // 41CF:2E4D: undoes effect 37h, or cures disease, or heals d4+8
    farptr t = G32(0x6c95);
    if (effect_cure(0x37, t)) { G8(0x681f) = 1; effect_remove(0, 0x16, t); G8(0x681f) = 0; return; }
    if (spl_cure_disease_on()) return;
    if (rules_heal(0, (u8)(roll_dice(4, 1) + 8), t)) spell_sparkle(MS(0x2e43), 1, t);   // "is Healed"
}
void spl_m_strength(void) {                                     // 41CF:2EE5: Strength 21 for a while (effect 26h)
    farptr t = G32(0x6c95); u8 saved;
    if (rules_raise_strength(&saved, 0, 0x15, t)) ui_char_message(1, 0x0a, MS(0x2ed9), t);   // "is stronger"
    effect_add(1, saved, spell_duration(G8(0x681b)), 0x26, t);
}
void spl_m_bolt(void) {                                         // 41CF:2F4D: d6+20 on the first square, 20 along 3 more
    u8 wall;
    spl_bolt_square(&wall, 4, (u8)(roll_dice(6, 1) + 0x14), (s8)G8(0x6dba), (s8)G8(0x6db9));
    spl_bolt_path(0, 4, 0x14, 3);
}
void spl_m_paralyze(void) { spell_affect_targets(MS(0x2f8c), 0, 0, 0, 0, G8(0x681b)); }   // 41CF:2F99 "is paralyzed"
void spl_m_cure(void) {                                         // 41CF:2FD0: heals 2d4+2
    farptr t = G32(0x6c95);
    if (rules_heal(0, (u8)(roll_dice(4, 2) + 2), t)) spell_sparkle(MS(0x2fc6), 1, t);   // "is Healed"
}
void spl_m_invisible(void) { spell_affect_targets(MS(0x301d), 0, 0, 0, 0, G8(0x681b)); }  // 41CF:302A "is invisible"
void spl_m_damage(void) { spell_affect_targets(MS(0x3057), 8, (u8)(roll_dice_noted(4, 2) + 2), 0, 0, G8(0x681b)); }   // 41CF:3058: 2d4+2
void spl_m_none(void) {}                                        // 41CF:3092
void spl_m_reading(void) { spell_affect_targets(MS(0x3097), 0, 0, 1, 0xff, G8(0x681b)); }  // 41CF:30A2 "is Reading"
#undef MS

// 41CF:323D  Whether an item is a scroll (its type's slot byte, DS:5582 + type*16, is 0Bh-0Dh).
u8 item_is_scroll(farptr it) {
    if (!it) return 0;
    u8 slot = G8(0x5582 + P8(it, 0x2e) * 16);
    return slot >= 0x0b && slot <= 0x0d;
}
// 41CF:051C  The spell list of the selected character's scrolls (for reading or scribing, as `marked` says): the
// 49 list entries at DS:477F are cleared, then each scroll (walked with DS:6B80) adds its spells (41CF:044E).
void spell_list_scrolls(u8 marked) {
    for (int i = 0; i <= 0x30; i++) SETG32(0x477f + i * 4, 0);
    G8(0x4843) = 0;
    for (SETG32(0x6b80, P32(G32(0x5d92), 0xc8)); G32(0x6b80); SETG32(0x6b80, P32(G32(0x6b80), 0x2a)))
        if (item_is_scroll(G32(0x6b80))) spell_list_item(marked);
}

// 2E2A:3B93  The developers' combat key "-": when the game was started as START STING (ParamStr(1)), "The Gods
// intervene!" and every enemy (side 1) falls dead on the spot (+10Dh 0, status 6, its square on the field cleared);
// every combatant's action is cleared, and the view recentred on the field. Returns whether it worked.
u8 fight_sting_kill(void) {
    farptr p = scratch_push(0x100); rtl_paramstr(p, 1);
    int ok = P8(p, 0) == 5 && !memcmp(&mem[LIN(p) + 1], "STING", 5);
    scratch_pop(0x100);
    if (!ok) return 0;
    ui_notice(&mem[LIN(FARPTR(RSEG(0x2e2a), 0x3b7f))]);
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        if (P8(c, 0x10e) == 1) { P8(c, 0x10d) = 0; P8(c, 0x10c) = 6; G8(combat_slot(c) * 4 + 0x5f2a) = 0; }
        chr_clear_action(c);
    }
    farptr m = G32(0x6716);
    combat_step_view(8, 0xff, (s8)((s8)P8(m, 3) + 3), (s8)((s8)P8(m, 2) + 3));
    return 1;
}
