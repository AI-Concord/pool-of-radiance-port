// The status effects themselves (original overlay unit at 2A9F). Each is a small routine the
// effects hub calls when its event comes up: (arg, the effect's node, the character). `arg` is
// 0 when the effect is being asked about an event and 1 when it is being removed. They answer
// by adjusting the shared numbers of the moment:
//   DS:6822 the d20 roll     DS:6818 damage        DS:6816 a saving-throw adjustment
//   DS:6825 morale           DS:681A movement / attack rate (in halves)
//   DS:681E "cannot be targeted"
#include "poolrad.h"
#include "chr.h"
#include "rtl.h"
#include "hud.h"
#include "rules.h"
#include "combat.h"
#include "text.h"
#include "fight.h"
#include "effects.h"
#include "sheet.h"

#define ROLL   G8(0x6822)
#define SAVE   G8(0x6816)
#define MORALE G8(0x6825)
#define RATE   G8(0x681a)
#define DAMAGE G8(0x6818)

// 2A9F:319B, 0647, 1355, 135C  Effects that do nothing when asked (kinds 05 0D 13 18 1B 3C 3F
// 5C 7F; 10; 36; 37): they exist to be found, displayed or counted down.
void fx_nothing(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; }

// 2A9F:010D  Bless (01): +5 morale, +1 to hit.
void fx_bless(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; MORALE += 5; ROLL += 1; }

// 2A9F:0C06  Kind 23h (Prayer's effect on a friend, by its place in the lists): +1 to saves, +1 to hit.
void fx_prayer_friend(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; SAVE += 1; ROLL += 1; }

// 2A9F:0C15  Bestow Curse (24h): -4 to hit, -4 to saves.
void fx_bestow_curse(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; ROLL -= 4; SAVE -= 4; }

// 2A9F:10C1  Slow (2Ah): half the movement and attack rate.
void fx_slow(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; RATE >>= 1; }

// 2A9F:171F  Invisible (47h): cannot be chosen as a target, and -4 on a roll against them.
void fx_invisible(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; G8(0x681e) = 1; ROLL -= 4; }

// 2A9F:011D  Curse (02): -5 morale (not below 0), -1 to hit.
void fx_curse(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; if (MORALE < 5) MORALE = 0; else MORALE -= 5; ROLL -= 1; }

// 2A9F:25A2, 2B63, 2A6D  Kinds 5Dh, 76h, 72h: half damage from one kind of attack. DS:6819 says what
// the damage is made of: bit 1 fire, bit 2 cold, bit 4 a third kind (lightning, by elimination).
void fx_half_fire_5d(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; if (G8(0x6819) & 1) DAMAGE >>= 1; }

void fx_half_cold_76(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; if (G8(0x6819) & 2) DAMAGE >>= 1; }

void fx_half_third_72(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; if (G8(0x6819) & 4) DAMAGE >>= 1; }

// 2A9F:0A3D  Ray of Enfeeblement (1Dh): a quarter off the damage the victim deals.
void fx_enfeebled(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; DAMAGE = (u8)(DAMAGE - (DAMAGE >> 2)); }

// 2A9F:03E1  Resist Cold (0Ah): half damage from cold and +3 on the save.
void fx_resist_cold(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; if (G8(0x6819) & 2) { DAMAGE >>= 1; SAVE += 3; } }

// 2A9F:06E4  Resist Fire (14h): half damage from fire and +3 on the save.
void fx_resist_fire(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; if (G8(0x6819) & 1) { DAMAGE >>= 1; SAVE += 3; } }

// 2A9F:0C26  Blink (25h): once the blinker has an action in hand this round (action byte 3),
// it cannot be targeted and a roll against it fails.
void fx_blink(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; if ((s8)P8(P32(ch, 0x108), 3) > 0) { G8(0x681e) = 1; ROLL = 0xff; } }

// 2A9F:1452  Kind 3Ah: rooted to the spot. No movement left, and a zero rate when movement is being asked.
void fx_rooted(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; P8(P32(ch, 0x108), 6) = 0; if (G8(0x681d) != 0) RATE = 0; }

// 2A9F:133F  The helpless states (1Fh Helpless, 33h Snake Charm, 34h Hold Person, 35h Sleep):
// whatever the character meant to do is wiped.
void fx_helpless(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; chr_clear_action(ch); }

// 2A9F:05D6  Friends (0Eh): put Charisma back to the value saved in the effect node.
void fx_restore_charisma(u8 arg, farptr node, farptr ch) { (void)arg; (void)node; (void)ch; P8(ch, 0x15) = P8(node, 3); }

// ---- Helpers the effects share ----
#define SELECTED G32(0x5d92)        // during an attack, the attacker

// 2A9F:0000  Cancel the attack in progress if it is of the given kind (DS:6817 holds the kind,
// a spell or attack-form number), or whatever it is when `kind` is 0: no damage, and the kind is
// cleared so that nothing later reacts to it.
void fx_negate(u8 kind) { if (kind == 0 || G8(0x6817) == kind) { DAMAGE = 0; G8(0x6817) = 0; } }

// 2A9F:0FAC  A chance in a hundred of avoiding a missile: only when the attacker holds a
// weapon and stands more than a square away. "<name> Avoids it"; no damage, the roll fails,
// and one fewer hit is counted (DS:6823).
void fx_missile_deflect(u8 chance, farptr ch) {
    if (!P32(SELECTED, 0xcc) || combat_distance(ch, SELECTED) <= 1 || roll_dice(100, 1) > chance) return;
    u8 msg[256] = "\011Avoids it";
    ui_char_message(1, 10, msg, ch);
    DAMAGE = 0; ROLL = 0xff; G8(0x6823)--;
}

// 2A9F:101D  What a character is actually hitting with: the ammunition if they are shooting,
// else the weapon in hand, else nil.
farptr fx_weapon_in_use(farptr ch) {
    farptr weapon = P32(ch, 0xcc), ammo = 0;
    if (!weapon) return 0;
    farptr v = scratch_push(4);
    u8 found = (u8)call_original(0x10ba, 0x2e8b, 4, FP_SEG(ch), FP_OFF(ch), FP_SEG(v), FP_OFF(v));
    ammo = P32(v, 0);
    scratch_pop(4);
    return (found && ammo) ? ammo : weapon;
}

// 2A9F:2915  Magic resistance: against a spell (DS:6817 set, or bit 8 of the damage kind), a
// chance in a hundred of the spell simply failing. The base chance drops by 5 for every level
// the caster (of spell DS:681B) is above 11, and rises by 5 for every level below.
void fx_magic_resist(u8 chance) {
    u8 level = (u8)call_original(0x10ba, 0x26c2, 1, G8(0x681b));
    chance = (u8)(chance - (11 - level) * 5);
    if ((G8(0x6817) != 0 || (G8(0x6819) & 8) != 0) && roll_dice(100, 1) <= chance) fx_negate(0);
}

// ---- More effects ----
#define FX(name) void name(u8 arg, farptr node, farptr ch)
#define UNUSED (void)arg; (void)node; (void)ch

// 2A9F:2976, 2984  Kinds 69h and 6Ah: magic resistance of 50 and of 15 in a hundred.
FX(fx_resist_magic_50) { UNUSED; fx_magic_resist(0x32); }
FX(fx_resist_magic_15) { UNUSED; fx_magic_resist(0x0f); }
// 2A9F:29CB, 29B6, 29F0  Immunities to particular attacks: kind 6Dh to 34h (paralysis);
// 6Ch to 0Bh and 35h (charm and sleep); 6Fh to 37h (poison) and 34h, and a saving throw of
// kind 0, the poison kind (DS:682A), that cannot fail.
FX(fx_immune_hold) { UNUSED; fx_negate(0x34); }
FX(fx_immune_charm_sleep) { UNUSED; fx_negate(0x0b); fx_negate(0x35); }
FX(fx_immune_6f) { UNUSED; fx_negate(0x37); fx_negate(0x34); if (G8(0x682a) == 0) SAVE = 100; }
// 2A9F:29D9, 2A11  Kinds 6Eh and 70h: no damage at all from cold, or from fire.
FX(fx_immune_cold) { UNUSED; if (G8(0x6819) & 2) fx_negate(0); }
FX(fx_immune_fire) { UNUSED; if (G8(0x6819) & 1) fx_negate(0); }
// 2A9F:2992, 2DE4  Kinds 6Bh and 7Ch: resistance to sleep and charm, 90 in a hundred and 30 in a
// hundred, which are the rulebook's figures for elves and half-elves.
FX(fx_elf_resistance) { UNUSED; if (roll_dice(100, 1) < 0x5b) { fx_negate(0x35); fx_negate(0x0b); } }
FX(fx_half_elf_resistance) { UNUSED; if (roll_dice(100, 1) < 0x1f) { fx_negate(0x0b); fx_negate(0x35); } }
// 2A9F:2901  Kind 68h: avoids missiles 60 times in a hundred.
FX(fx_avoid_missiles) { (void)arg; (void)node; fx_missile_deflect(0x3c, ch); }
// 2A9F:2AE6  Kind 74h: half damage from magical weapons.
FX(fx_half_from_magic_weapons) { UNUSED; farptr w = fx_weapon_in_use(SELECTED); if (w && (s8)P8(w, 0x32) > 0) DAMAGE >>= 1; }
// 2A9F:2B7D  Kind 77h: hit only by magic weapons, unless the attacker is a monster of four hit
// dice or more (race 0 with byte 73h at least 4).
FX(fx_needs_magic_weapon) {
    UNUSED; farptr w = fx_weapon_in_use(SELECTED);
    if ((!w || P8(w, 0x32) == 0) && ((s8)P8(SELECTED, 0x2e) > 0 || (s8)P8(SELECTED, 0x73) < 4)) DAMAGE = 0;
}
// 2A9F:2B20  Kind 75h: a weapon of type 55h does 1d6+1 to this creature whatever its usual damage.
FX(fx_type55_damage) { UNUSED; farptr w = P32(SELECTED, 0xcc); if (w && P8(w, 0x2e) == 0x55) DAMAGE = (u8)(roll_dice_noted(6, 1) + 1); }
// 2A9F:1472, 1363  Kinds 3Bh and 38h hand on other effects: 3Bh puts on 62h; 38h puts on
// Invisibility (19h) for 12 units of time.
FX(fx_grant_62) { (void)arg; (void)node; effect_add(0, 0xff, 0, 0x62, ch); }
FX(fx_grant_invisibility) { (void)arg; (void)node; effect_add(0, 0x0c, 1, 0x19, ch); }
// 2A9F:0BAC  Cause Blindness (21h): -4 to hit, armour class 4 worse front and rear, -4 to saves.
FX(fx_blinded) { (void)arg; (void)node; ROLL -= 4; P8(ch, 0x111) -= 4; P8(ch, 0x112) -= 4; SAVE -= 4; }
// 2A9F:064E  Shield (11h): armour class no worse than 3, +1 to saves, and Magic Missile (spell
// 0Fh) does nothing.
FX(fx_shield) { (void)arg; (void)node; if (P8(ch, 0x111) < 0x39) P8(ch, 0x111) = 0x39; SAVE += 1; if (G8(0x681b) == 0x0f) DAMAGE = 0; }

// 2A9F:0058  Strike a character down with a message: "<name> <text>", then, unless they are
// already dead, stoned or gone, the given status, no hit points, their listed effects removed,
// their effects told (event 0Dh), and the fall shown.
void fx_strike_down(const u8 *text, u8 status, farptr ch) {
    u8 msg[256] = {0}; u8 n = text[0] > 40 ? 40 : text[0];
    msg[0] = n; memcpy(msg + 1, text + 1, n);
    ui_char_message(0, 10, msg, ch);
    if (P8(ch, 0x10c) == 7 || P8(ch, 0x10c) == 6 || P8(ch, 0x10c) == 8) return;
    P8(ch, 0x10c) = status; P8(ch, 0x10d) = 0; P8(ch, 0x11b) = 0;
    effects_remove_list16(ch);
    effects_event(ch, 0x0d);
    if (P8(ch, 0x10d) == 0) combat_fall(ch);
    text_pause();
    ui_clear_message();
    if (G8(0x49f3) != 5) hud_draw_roster(SELECTED);
}

// 2A9F:1545  A poisonous attack. The victim is whoever the attacker is striking (its action
// record's target, also left at DS:6826). They save against poison with `adjust`, or else:
// "is Poisoned", the Poisoned effect (37h) for good, and "is killed".
void fx_poison(s8 adjust, farptr attacker) {
    farptr victim = P32(P32(attacker, 0x108), 0x0a);
    SETG32(0x6826, victim);
    if (rules_saving_throw(adjust, 0, victim)) return;
    u8 msg[256] = "\013is Poisoned";
    ui_char_message(0, 10, msg, G32(0x6826));
    text_pause();
    effect_add(0, 0xff, 0, 0x37, G32(0x6826));
    fx_strike_down((const u8 *)"\011is killed", 6, G32(0x6826));
}
// 2A9F:165D, 1671, 1685, 170B  Kinds 40h, 41h, 42h, 46h: poison with a save at +0, +4, +2 and -2.
FX(fx_poison_0) { (void)arg; (void)node; fx_poison(0, ch); }
FX(fx_poison_plus4) { (void)arg; (void)node; fx_poison(4, ch); }
FX(fx_poison_plus2) { (void)arg; (void)node; fx_poison(2, ch); }
FX(fx_poison_minus2) { (void)arg; (void)node; fx_poison(-2, ch); }

// 2A9F:15EB  A paralysing attack: the victim saves (kind 0, no adjustment) or gets the message
// at 2A9F:15DE (shown by 10BA:1FEB, still original code) and effect 34h for `time`.
void fx_paralyse(u16 time, farptr attacker) {
    farptr victim = P32(P32(attacker, 0x108), 0x0a);
    SETG32(0x6826, victim);
    if (rules_saving_throw(0, 0, victim)) return;
    victim = G32(0x6826);
    call_original(0x10ba, 0x1feb, 5, FP_SEG(victim), FP_OFF(victim), 1, RSEG(0x2a9f), 0x15de);
    effect_add(0, 0x0c, time, 0x34, G32(0x6826));
}
// 2A9F:1699, 16E6  Kinds 43h and 45h: paralysis for 2d8, or for 1d9+10.
FX(fx_paralyse_2d8) { (void)arg; (void)node; fx_paralyse(roll_dice(8, 2), ch); }
FX(fx_paralyse_long) { (void)arg; (void)node; fx_paralyse((u16)(roll_dice(9, 1) + 10), ch); }

// 2A9F:0377, 03AC  Protection from Evil (08h, and 2Dh the 10' Radius kind) and from Good (09h,
// 2Eh): if the attacker's alignment is one of the evil three (2, 5, 8) or the good three
// (0, 3, 6), +2 on the save and -2 on their roll.
FX(fx_protection_from_evil) { UNUSED; u8 al = P8(SELECTED, 0xa0); if (al == 2 || al == 5 || al == 8) { SAVE += 2; ROLL -= 2; } }
FX(fx_protection_from_good) { UNUSED; u8 al = P8(SELECTED, 0xa0); if (al == 0 || al == 3 || al == 6) { SAVE += 2; ROLL -= 2; } }
// 2A9F:0918  Invisibility (19h): cannot be targeted unless the attacker has Detect
// Invisibility (18h); either way -4 on the roll.
FX(fx_invisibility) { UNUSED; if (!char_has_effect(0x18, SELECTED)) G8(0x681e) = 1; ROLL -= 4; }
// 2A9F:2700  Kind 62h: regeneration, 3 hit points a time, up to the maximum.
FX(fx_regenerate) { (void)arg; (void)node; P8(ch, 0x11b) += 3; if (P8(ch, 0x32) < P8(ch, 0x11b)) P8(ch, 0x11b) = P8(ch, 0x32); }
// 2A9F:2E08  Kind 7Dh: immune to charm, sleep, paralysis and poison, and cannot fail a
// poison-kind save. (The undead's immunities, by the list.)
FX(fx_undead_immunities) { UNUSED; fx_negate(0x0b); fx_negate(0x35); fx_negate(0x34); fx_negate(0x37); if (G8(0x682a) == 0) SAVE = 100; }
// 2A9F:1730  Kind 48h: 95 times in a hundred, becomes invisible (effect 19h for 12 units).
FX(fx_turn_invisible) { (void)arg; (void)node; if (roll_dice(100, 1) < 0x60) effect_add(0, 0x0c, 1, 0x19, ch); }
// 2A9F:16BB  Kind 44h: a paralysing touch that does not work on elves (race 2), for 63 units.
FX(fx_paralyse_not_elves) { (void)arg; (void)node; if (P8(P32(P32(ch, 0x108), 0x0a), 0x2e) != 2) fx_paralyse(0x3f, ch); }
// 2A9F:013B  Kind 03h: +2 to hit and to damage against a target whose byte 9Fh is 4 (the
// target is also left at DS:6826).
FX(fx_bonus_vs_type4) { (void)arg; (void)node; SETG32(0x6826, P32(P32(ch, 0x108), 0x0a)); if (P8(G32(0x6826), 0x9f) == 4) { ROLL += 2; DAMAGE += 2; } }
// 2A9F:2D88  Kind 7Bh: no damage from bare hands or from ordinary weapons; half from an
// unenchanted weapon whose first name word is B1h (silver, most likely); full from magic.
FX(fx_silver_or_magic) {
    UNUSED; farptr w = fx_weapon_in_use(SELECTED);
    if (!w) DAMAGE = 0;
    else if (P8(w, 0x32) == 0 && P8(w, 0x31) == 0xb1) DAMAGE >>= 1;
    else if (P8(w, 0x32) == 0) DAMAGE = 0;
}

// 2A9F:1A19, 1A4C  Kinds 4Fh and 50h: an attack that also burns its target for 2d10 (fire, and
// magical: damage kind 9), or eats at it with acid for 1d4 (kind 10h).
FX(fx_fire_touch) { (void)arg; (void)node; G8(0x6819) = 9; farptr t = P32(P32(ch, 0x108), 0x0a); rules_damage(0, 0, roll_dice_noted(10, 2), t); }
FX(fx_acid_touch) { (void)arg; (void)node; G8(0x6819) = 0x10; farptr t = P32(P32(ch, 0x108), 0x0a); rules_damage(0, 0, roll_dice_noted(4, 1), t); }

// 2A9F:001F  Put an effect on (with "tell me when removed" set) unless DS:681F says not to.
// Returns 1 if it was put on.
u8 fx_add_once(u16 time, u8 b3, u8 kind, farptr ch) { if (G8(0x681f) != 0) return 0; effect_add(1, b3, time, kind, ch); return 1; }

// 2A9F:1086  Protection from Normal Missiles (29h): a missile that is not magical never hits.
FX(fx_protection_normal_missiles) { (void)arg; (void)node; farptr w = fx_weapon_in_use(SELECTED); if (w && P8(w, 0x32) == 0) fx_missile_deflect(100, ch); }
// 2A9F:264B  Kind 60h: harmed only by magic or by silver (first name word B1h).
FX(fx_only_silver_or_magic) { UNUSED; farptr w = fx_weapon_in_use(SELECTED); if (!w || (P8(w, 0x32) == 0 && P8(w, 0x31) != 0xb1)) DAMAGE = 0; }
// 2A9F:2A28  Kind 71h: one point less per die from fire, though never less than one a die
// (DS:681C is the number of dice last rolled).
FX(fx_fire_minus_one_per_die) { UNUSED; if ((G8(0x6819) & 1) && G8(0x681c)) for (u8 i = 0; i < G8(0x681c); i++) { DAMAGE--; if (DAMAGE < G8(0x681c)) DAMAGE = G8(0x681c); } }
// 2A9F:2610  Kind 5Fh: when this effect ends its bearer "Falls dead", if still standing.
FX(fx_falls_dead) { (void)arg; P8(node, 4) = 0; if (P8(ch, 0x10d) != 0) fx_strike_down((const u8 *)"\012Falls dead", 6, ch); }
// 2A9F:27D9  Kind 64h, the troll's trick: on going down to anything but fire or acid, effect
// 66h is set to go off in 3d6.
FX(fx_troll_falls) { (void)arg; (void)node; if (!(G8(0x6819) & 1) && !(G8(0x6819) & 0x10)) effect_add(1, 0xff, roll_dice(6, 3), 0x66, ch); }
// 2A9F:2866  Kind 66h: ...and when it goes off the troll gets back up with all its hit points;
// if there is no room to stand, it tries again shortly.
FX(fx_troll_rises) { (void)arg; if (!rules_get_up(P8(ch, 0x32), ch)) fx_add_once(1, P8(node, 3), 0x66, ch); }
// 2A9F:19D9  Kind 4Eh, coming round: an unconscious character gets up with the hit points
// they have, or tries again shortly.
FX(fx_comes_round) { (void)arg; if (!rules_get_up(P8(ch, 0x11b), ch)) fx_add_once(1, P8(node, 3), 0x4e, ch); }
// 2A9F:12FB, 14EB  Kinds 32h and 3Eh renew themselves each time they run out and heal one
// point by natural healing: every 14,400 units under the mummy's disease (32h), every 60 for a
// regenerating creature (3Eh).
FX(fx_slow_healing) { (void)arg; if (fx_add_once(0x3840, P8(node, 3), 0x32, ch) && rules_heal(1, 1, ch)) chr_healed_message(ch); }
FX(fx_fast_healing) { (void)arg; if (fx_add_once(0x3c, P8(node, 3), 0x3e, ch) && rules_heal(1, 1, ch)) chr_healed_message(ch); }

static void say(const char *t, farptr ch) { u8 m[256] = {0}; m[0] = (u8)strlen(t); memcpy(m + 1, t, m[0]); ui_char_message(1, 10, m, ch); }

// 2A9F:12B2  Prayer (31h): the node remembers which side cast it (bit 10h of its byte 3).
// That side gets +1 to hit and on saves; the other side gets -1 on both.
FX(fx_prayer) { (void)arg; if (P8(ch, 0x10e) == ((P8(node, 3) & 0x10) >> 4)) { SAVE += 1; ROLL += 1; } else { ROLL -= 1; SAVE -= 1; } }
// 2A9F:2490  Displaced (59h): the first attack on it each time misses. Bit 10h of the node's
// byte 3 remembers that the miss has been used; it is cleared again when asked with the round
// counter and the roll both zero.
FX(fx_displaced) { (void)arg; (void)ch; if (G8(0x6de3) == 0 && ROLL == 0) P8(node, 3) &= 0x0f; else if (!(P8(node, 3) & 0x10)) { ROLL = 0xff; P8(node, 3) |= 0x10; } }
// 2A9F:25BC  Kind 5Eh: half damage from weapons whose type has either of bits 81h set in byte 7
// of the item table (a class of weapon; which one is not yet known).
FX(fx_half_from_weapon_class) { UNUSED; farptr w = fx_weapon_in_use(SELECTED); if (w && (G8(0x5589 + P8(w, 0x2e) * 16) & 0x81)) DAMAGE >>= 1; }
// 2A9F:2BC8  Kind 78h: avoids weapons of types 57h and 58h half the time.
FX(fx_avoid_types_57_58) { (void)arg; (void)node; farptr w = P32(SELECTED, 0xcc); if (w && (P8(w, 0x2e) == 0x57 || P8(w, 0x2e) == 0x58)) fx_missile_deflect(0x32, ch); }
// 2A9F:2556  Kind 5Bh: Magic Missile (spell 0Fh) does nothing, "is unaffected"; nor does the
// third element (damage-kind bit 4, electricity).
FX(fx_unaffected_by_missile_and_lightning) { (void)arg; (void)node; if (G8(0x681b) == 0x0f) { fx_negate(0); say("is unaffected", ch); } else if (G8(0x6819) & 4) fx_negate(0); }
// 2A9F:2D38  Kind 7Ah: a weapon of type 56h does 3d8 to this creature; and fire or magic does one
// more point per die rolled.
FX(fx_type56_and_fire_bonus) { UNUSED; farptr w = P32(SELECTED, 0xcc); if (w && P8(w, 0x2e) == 0x56) DAMAGE = roll_dice_noted(8, 3); if (G8(0x6819) & 9) DAMAGE += G8(0x681c); }
// 2A9F:2814  Kind 65h: unless it already has 62h or 3Bh, puts on 3Bh for 3 units (which in turn
// puts on 62h, regeneration).
FX(fx_start_regenerating) { (void)arg; (void)node; if (!char_has_effect(0x62, ch) && !char_has_effect(0x3b, ch)) effect_add(1, 0xff, 3, 0x3b, ch); }
// 2A9F:070F  Silence, 15' Radius (15h): no casting and no using items this turn (action bytes
// 1 and 2), with "is silenced" if an item was about to be used.
FX(fx_silenced) { (void)arg; (void)node; farptr act = P32(ch, 0x108); if (P8(act, 2) != 0) say("is silenced", ch); act = P32(ch, 0x108); P8(act, 2) = 0; P8(act, 1) = 0; }
// 2A9F:1490  Fire Resistance (3Dh): against fire, two points less per die (never under one a
// die), +4 on the save, and ordinary (non-magical) fire does nothing at all.
FX(fx_fire_resistance) {
    UNUSED; if (!(G8(0x6819) & 1)) return;
    u8 n = G8(0x681c);
    for (u8 i = 0; i < n; i++) { DAMAGE -= 2; if (DAMAGE < G8(0x681c)) DAMAGE = G8(0x681c); }
    SAVE += 4;
    if (!(G8(0x6819) & 8)) fx_negate(0);
}
// 2A9F:05EB  Kind 0Fh: renews itself every 10 units and costs one hit point each time, though
// never the last one.
FX(fx_slow_loss) {
    (void)arg; if (!fx_add_once(10, P8(node, 3), 0x0f, ch) || P8(ch, 0x11b) <= 1) return;
    G8(0x6819) = 0;
    rules_damage(0, 0, 1, ch);
    if (G8(0x49f3) != 5) hud_draw_roster(SELECTED);
}

static int name_is(u16 table_entry, farptr who) {   // is the character's name the one in this 16-byte table entry? (Pascal string equality)
    const u8 *a = &G8(table_entry), *b = &P8(who, 0);
    return a[0] == b[0] && memcmp(a + 1, b + 1, a[0]) == 0;
}
#define TARGET_OF(ch) P32(P32(ch, 0x108), 0x0a)

// 2A9F:0C4B  Haste (27h): twice the movement and attack rate. The first time it is asked, the
// character "ages": a year is added (word 30h), as the rulebook charges for the spell.
FX(fx_haste) { (void)arg; if (!(P8(node, 3) & 0x10)) { P8(node, 3) += 0x10; say("ages", ch); SETP16(ch, 0x30, (u16)(P16(ch, 0x30) + 1)); } RATE <<= 1; }
// 2A9F:2A87  Kind 73h: half damage from weapons whose type has byte 7 of the item table zero or odd.
FX(fx_half_from_weapon_class_b) { UNUSED; farptr w = fx_weapon_in_use(SELECTED); if (w) { u8 f = G8(0x5589 + P8(w, 0x2e) * 16); if (f == 0 || (f & 1)) DAMAGE >>= 1; } }
// 2A9F:01C3  Kind 06h: a weapon that burns. +1 to hit and damage against creatures of class 10,
// +2 against classes 9 and 12, +3 against class 4, and the damage counts as fire and magic.
FX(fx_burning_weapon) {
    (void)arg; (void)node; SETG32(0x6826, TARGET_OF(ch));
    u8 c = P8(G32(0x6826), 0x9f), b = c == 10 ? 1 : (c == 9 || c == 12) ? 2 : c == 4 ? 3 : 0;
    ROLL += b; DAMAGE += b; G8(0x6819) = 9;
}
// 2A9F:24D9  Kind 5Ah, carried by dwarves and halflings: a bonus on poison-kind saves from
// Constitution, +1 at 4-6, +2 at 7-10, +3 at 11-13, +4 at 14-17, +5 at 18-20. Returns 0 for a
// Constitution outside 4-20, where the original adds whatever happened to be on its stack.
int fx_hardy(farptr ch) {
    if (G8(0x682a) != 0) return 1;
    u8 con = P8(ch, 0x14), b;
    if (con >= 4 && con <= 6) b = 1; else if (con >= 7 && con <= 10) b = 2; else if (con >= 11 && con <= 13) b = 3;
    else if (con >= 14 && con <= 17) b = 4; else if (con >= 18 && con <= 20) b = 5; else return 0;
    SAVE += b;
    return 1;
}
// 2A9F:0775  Kind 16h (Slow Poison, by the spell table): when it ends, anyone still Poisoned
// "dies from poison"; and its companion effect 0Fh is taken off without being told.
FX(fx_slow_poison_ends) {
    (void)arg; (void)node;
    if (char_has_effect(0x37, ch)) fx_strike_down((const u8 *)"\020dies from poison", 6, ch);
    G8(0x681f) = 1; effect_remove(0, 0x0f, ch); G8(0x681f) = 0;
}
// 2A9F:0679, 0947  Kinds 12h and 1Ah, the racial enmities: +1 to hit a man-sized target of
// creature class 1 whose name is on the list. 12h: KOBOLD, KOBOLD LEADER, GOBLIN, GOBLIN
// LEADER. 1Ah: ORC, ORC LEADER, GOBLIN, GOBLIN LEADER, HOBGOBLIN, HOBGOBLIN CHIEF, and two named
// characters, GAGOOL and MACE.
static void enmity(farptr ch, u16 table, u8 count) {
    SETG32(0x6826, TARGET_OF(ch));
    farptr t = G32(0x6826);
    if (P8(t, 0x9f) == 1 && P8(t, 0x6c) == 1) for (u8 i = 1; i <= count; i++) if (name_is((u16)(table + i * 16), t)) ROLL += 1;
}
FX(fx_enmity_kobolds_goblins) { (void)arg; (void)node; enmity(ch, 0x0332, 4); }
FX(fx_enmity_orcs_goblins) { (void)arg; (void)node; enmity(ch, 0x0372, 8); }
// 2A9F:11F9  Kind 2Fh, the other side of it: a large (size 2) attacker of class 1 is at -4 if a
// name on a third list matches (OGRE, OGRE LEADER, TROLL, FIRE GIANT, HILL GIANT). The list has
// five names but the loop runs to eight, comparing against whatever follows; kept.
FX(fx_small_and_hard_to_hit) {
    (void)arg; (void)node; SETG32(0x6826, TARGET_OF(ch));
    if (P8(SELECTED, 0x9f) == 1 && (P8(SELECTED, 0x6c) & 0x7f) == 2)
        for (u8 i = 1; i <= 8; i++) if (name_is((u16)(0x03f2 + i * 16), G32(0x6826))) ROLL -= 4;
}

// 2A9F:18B5  Kind 4Ch: a grab. The attacker gets effect 4Bh (remembering which combatant it
// holds, for CFh units), and the target is rooted (3Ah) at once.
FX(fx_grab) {
    (void)arg; (void)node; SETG32(0x6826, TARGET_OF(ch));
    effect_add(1, combat_slot(G32(0x6826)), 0xcf, 0x4b, ch);
    effect_add(0, 0xff, 0, 0x3a, G32(0x6826));
    effect_run(0, 0, G32(0x6826), 0x3a);
}
// 2A9F:268A  Kind 61h, also carried by the hardy races: the same Constitution bonus as 5Ah, on
// saves of kinds 4 and 2 (magic). Returns 0 where the original has no answer.
int fx_hardy_magic(farptr ch) {
    if (G8(0x682a) != 4 && G8(0x682a) != 2) return 1;
    u8 con = P8(ch, 0x14), b;
    if (con >= 4 && con <= 6) b = 1; else if (con >= 7 && con <= 10) b = 2; else if (con >= 11 && con <= 13) b = 3;
    else if (con >= 14 && con <= 17) b = 4; else if (con >= 18 && con <= 20) b = 5; else return 0;
    SAVE += b;
    return 1;
}
// 2A9F:09C0  Mirror Image (1Ch): the node's byte 3 is the number of images left. A die with
// one more side than that is rolled, always; anything but a 1 means an image took the blow
// (when a spell number is set at DS:681B and DS:6820 is clear): no damage, "lost an image",
// one image fewer, and the effect goes with the last of them.
FX(fx_mirror_image) {
    (void)arg; u8 r = roll_dice((u8)(P8(node, 3) + 1), 1);
    if (!(r > 1 && G8(0x681b) != 0 && G8(0x6820) == 0)) return;
    fx_negate(0);
    say("lost an image", ch);
    if (--P8(node, 3) == 0) effect_remove(0, 0x1c, ch);
}
// 2A9F:10DE  Kind 2Bh: a wasting disease. Every 60 units the victim "is weakened" and loses
// a point of Strength; below 4 they become Helpless instead.
FX(fx_wasting) {
    (void)arg; if (!fx_add_once(0x3c, P8(node, 3), 0x2b, ch)) return;
    if (P8(ch, 0x10) < 4) { if (!char_has_effect(0x1f, ch)) effect_add(0, 0xff, 0, 0x1f, ch); }
    else { say("is weakened", ch); P8(ch, 0x10)--; }
}
// 2A9F:1168  Kind 2Ch (Cause Disease): every 10 units one hit point is lost; with only one
// left the victim becomes Helpless instead.
FX(fx_disease) {
    (void)arg; if (!fx_add_once(10, P8(node, 3), 0x2c, ch)) return;
    if (P8(ch, 0x11b) < 2) { if (!char_has_effect(0x1f, ch)) effect_add(0, 0xff, 0, 0x1f, ch); }
    else { G8(0x6819) = 0; rules_damage(0, 0, 1, ch); if (G8(0x49f3) != 5) hud_draw_roster(SELECTED); }
}
// 2A9F:0B1B  Kind 20h (Animate Dead): when it ends the animated creature "collapses" (unless
// effects are being removed quietly), and the record is put back to an ordinary one: its side
// from the node's byte 3, quick-fight on, and several fields reset.
FX(fx_animation_ends) {
    (void)arg; P8(node, 4) = 0;
    if (G8(0x681f) == 0) fx_strike_down((const u8 *)"\011collapses", 6, ch);
    P8(ch, 0x10e) = P8(node, 3) >> 4; P8(ch, 0x10f) = 1; P8(ch, 0x76) = 0; P8(ch, 0x6b) = P8(ch, 0x98); P8(ch, 0x72) = 0x0c;
    if (P8(ch, 0x84) == 0xb3) P8(ch, 0x84) = 0;
    P8(ch, 0x9f) = 0;
}

// 2A9F:1935  Kind 4Dh: a hold. Unless the target's Strength is 19 or more, the attacker gets
// effect 4Ah (remembering which combatant it holds), the target is rooted, and "is held fast".
FX(fx_hold_fast) {
    (void)arg; (void)node; SETG32(0x6826, TARGET_OF(ch));
    if (P8(G32(0x6826), 0x10) >= 0x13) return;
    effect_add(1, combat_slot(G32(0x6826)), 0, 0x4a, ch);
    effect_add(0, 0xff, 0, 0x3a, G32(0x6826));
    effect_run(0, 0, G32(0x6826), 0x3a);
    say("is held fast", G32(0x6826));
}
// 2A9F:23EC  Kind 57h, the mummy's touch: a target not already diseased gets the Dreaded Mummy
// Disease (32h, which slows healing) and the rot (39h), whose node counts 1d6 stages; "is Diseased".
FX(fx_mummy_touch) {
    (void)arg; (void)node; SETG32(0x6826, TARGET_OF(ch));
    if (char_has_effect(0x32, G32(0x6826))) return;
    effect_add(1, 0xff, 0x3840, 0x32, G32(0x6826));
    effect_add(1, (u8)(roll_dice(6, 1) * 16 + 0x0f), 0xa8c0, 0x39, G32(0x6826));
    say("is Diseased", G32(0x6826));
}
// 2A9F:1399  Kind 39h, the rot itself: each time its long timer runs out the victim "Rots..."
// and loses two points of Charisma (never below 3). Each time one stage is used up; with the
// last one, "Dies rots away" and the character is Gone (status 8).
FX(fx_mummy_rot) {
    (void)arg; if (G8(0x681f) != 0) return;
    say("Rots...", ch);
    if (P8(ch, 0x15) < 3) P8(ch, 0x15) = 0; else P8(ch, 0x15) -= 2;
    if (P8(ch, 0x15) < 3) P8(ch, 0x15) = 3;
    if ((P8(node, 3) >> 4) < 2) fx_strike_down((const u8 *)"\016Dies rots away", 8, ch);
    else { P8(node, 3) -= 0x10; fx_add_once(0xa8c0, P8(node, 3), 0x39, ch); }
}
// 2A9F:2730  Kind 63h: a last effort. Someone Dying whose bleeding count is under 6, or
// Unconscious, gets back up with a few hit points (6 less the count; 6 if unconscious), but
// carries effect 5Fh, "falls dead", for 1d4+1; and this effect is spent.
FX(fx_last_effort) {
    (void)arg; u8 hp = 0;
    if (P8(ch, 0x10c) == 5 && P8(P32(ch, 0x108), 0x0e) < 6) hp = (u8)(6 - P8(P32(ch, 0x108), 0x0e));
    if (P8(ch, 0x10c) == 4) hp = 6;
    if (!hp || !rules_get_up(hp, ch)) return;
    effect_add(1, 0xff, (u16)(roll_dice(4, 1) + 1), 0x5f, ch);
    P8(node, 4) = 0;
    effect_remove(node, 0x63, ch);
}
// 2A9F:0A65  Stinking Cloud (1Eh): "is coughing" if about to use an item; no items and no
// spells this turn; and armour class two worse (never better than 10), the same from in front
// as from behind.
FX(fx_stinking_cloud) {
    (void)arg; (void)node; farptr act = P32(ch, 0x108);
    if (P8(act, 2) != 0) say("is coughing", ch);
    act = P32(ch, 0x108); P8(act, 2) = 0; P8(act, 1) = 0;
    call_original(0x10ba, 0x0bb8, 2, FP_SEG(ch), FP_OFF(ch));
    if (P8(ch, 0x112) < 0x35) P8(ch, 0x112) = 0x32; else P8(ch, 0x112) -= 2;
    P8(ch, 0x111) = P8(ch, 0x112);
    if (ch == SELECTED) call_original(0x10ba, 0x09e3, 2, FP_SEG(ch), FP_OFF(ch));
}
// 2A9F:0400  Charm Person (0Bh). When first asked: the victim changes sides (the node's byte 3
// keeps the old side in bit 40h and the new one in bit 80h), fights on its own, and a player
// character is marked as a temporary NPC (B3h in byte 84h). Its morale cannot fail. When the
// charm is removed the old side comes back.
FX(fx_charm) {
    if (arg != 0) { P8(ch, 0x10e) = (P8(node, 3) & 0x40) >> 6; if (P8(ch, 0x84) == 0xb3) P8(ch, 0x84) = 0; return; }
    if (!(P8(node, 3) & 0x20)) {
        P8(node, 3) = (u8)(P8(node, 3) + 0x20 + P8(ch, 0x10e) * 0x40);
        P8(ch, 0x10e) = P8(node, 3) >> 7;
        P8(ch, 0x10f) = 1;
        if (P8(ch, 0x84) < 0x80) P8(ch, 0x84) = 0xb3;
        SETP32(P32(ch, 0x108), 0x0a, 0);
        chr_count_sides();
    }
    MORALE = 100;
}

// 2A9F:0233  Kind 07h, "Training with Manual of Bodily Health": the character "is hardier", one
// point of Constitution. Up to 19 the extra hit points the higher score allows are worked out
// for every class level and added (only for scores to 16, or for fighters); at 20 the character
// starts regenerating (3Eh).
FX(fx_manual_of_bodily_health) {
    (void)arg; (void)node;
    say("is hardier", ch);
    P8(ch, 0x14)++;
    if (P8(ch, 0x14) >= 0x14) { effect_add(1, 0xff, 0x3c, 0x3e, ch); return; }
    if (P8(ch, 0x14) <= 0x0e) return;
    u8 per = 0;
    for (int c = 0; c <= 7; c++) {
        s8 lv = (s8)P8(ch, 0x96 + c);
        if (lv <= 0) continue;
        if (c == 2) per = (u8)(per + (P8(ch, 0x14) - 0x0f) * P8(ch, 0x98));
        else if (P8(ch, 0x14) < 0x10) per = (u8)(per + lv);
        else per = (u8)(per + lv * 2);
    }
    if (per == 0) per = 1;
    per = (u8)((u8)(P8(ch, 0x32) - P8(ch, 0xb1)) / per);
    if (P8(ch, 0x14) < 0x11 || (s8)P8(ch, 0x98) > 0) { P8(ch, 0x32) += per; P8(ch, 0x11b) += per; }
}
// 2A9F:04BA  Strength and Enlarge (26h, 0Ch) when one of them ends: the character's Strength
// becomes what the ending effect gave, and if another Strength or Enlarge is still running the
// best of those takes over, the one it replaces remembering the character's previous score.
FX(fx_strength_ends) {
    (void)arg;
    if (P8(node, 3) >= 0x80) return;
    u8 pct, st, best_st = 0, best_pct = 0;
    rules_strength_decode(&pct, &st, node);
    P8(ch, 0x10) = st; P8(ch, 0x16) = pct;
    farptr best = 0;
    for (farptr e = P32(ch, 0x7f); e; e = P32(e, 5)) {
        if ((P8(e, 0) != 0x0c && P8(e, 0) != 0x26) || e == node) continue;
        rules_strength_decode(&pct, &st, e);
        if (best_st < st || (best_st == 0x12 && best_pct < pct)) { best_st = st; best_pct = pct; best = e; }
    }
    if (best) { P8(best, 3) = rules_encode(P8(ch, 0x16), P8(ch, 0x10)); P8(ch, 0x10) = best_st; P8(ch, 0x16) = best_pct; }
}
// 2A9F:1AAA  Kind 51h, a fear aura: every enemy standing saves against magic (kind 4); those of
// up to 3 hit dice are "paralyzed with fear" (34h), those of 4 or 5 "weakened by fear" (Curse).
FX(fx_fear_aura) {
    (void)arg; (void)node;
    for (farptr p = G32(0x5d96); p; p = P32(p, 0x104)) {
        if (P8(p, 0x10d) == 0 || chr_on_party_side(ch) != P8(p, 0x10e)) continue;
        u8 saved = rules_saving_throw(0, 4, p);
        s8 hd = (s8)P8(p, 0x73);
        if (hd >= 0 && hd <= 3) rules_spell_effect((const u8 *)"\026is paralyzed with fear", saved, 1, 0, 0x0c, 0, 0x34, p);
        else if (hd == 4 || hd == 5) rules_spell_effect((const u8 *)"\023is weakened by fear", saved, 1, 0, 0x0c, 0, 0x02, p);
    }
}
// 2A9F:1BA8  Kind 52h, a terrifying presence, used once (it removes itself from everyone): each
// enemy standing saves (kind 0) at +1 if their side still has more than five standing and +2 more
// if human, or is "paralyzed with fear" for 1d4.
FX(fx_terror) {
    (void)arg; (void)node;
    for (farptr p = G32(0x5d96); p; p = P32(p, 0x104)) {
        effect_remove(0, 0x52, p);
        if (P8(p, 0x10d) == 0 || chr_on_party_side(ch) != P8(p, 0x10e)) continue;
        s8 adjust = G8(0x6814 + (s8)P8(p, 0x10e)) > 5;
        if (P8(p, 0x2e) == 7) adjust += 2;
        u8 saved = rules_saving_throw(adjust, 0, p);
        rules_spell_effect((const u8 *)"\026is paralyzed with fear", saved, 1, 0, 0x0c, roll_dice(4, 1), 0x34, p);
    }
}

// 2A9F:2E37  Kind 7Eh: this creature cannot attack anyone carrying, readied, an item with name
// word 76h, or with word 98h and FCh as its last word (a ward of some kind; the words are not
// yet identified). Consulted when choosing a target.
FX(fx_warded_off) {
    (void)arg; (void)node; SETG32(0x6826, TARGET_OF(ch));
    for (farptr it = P32(G32(0x6826), 0xc8); it && G8(0x681e) == 0; it = P32(it, 0x2a)) {
        if (P8(it, 0x34) == 0) continue;
        for (int k = 1; k <= 3; k++)
            if ((P8(it, 0x2e + k) == 0x98 && P8(it, 0x31) == 0xfc) || P8(it, 0x2e + k) == 0x76) G8(0x681e) = 1;
    }
}
// 2A9F:176F  Kind 4Bh, the other half of a grab: each turn the holder "Sucks some Blood": 1d4
// damage to the held victim (combatant slot in the node's byte 3), which also ends the holder's
// action. The grip lasts until the victim falls or the damage done adds up to the node's
// counter (time word over 16). When the effect is asked with nothing to do, or is removed, the
// victim is let go (its 3Ah removed) and, if both still stand, the holder's action byte 10h is set.
FX(fx_draining_grip) {
    SETG32(0x6826, G32(0x65b9 + P8(node, 3) * 4));
    if (arg == 0 && P8(ch, 0x10d) != 0 && P8(G32(0x6826), 0x10d) != 0) {
        say("Sucks some Blood", ch);
        u8 d = roll_dice_noted(4, 1);
        G8(0x6819) = 0;
        rules_damage(0, 0, d, G32(0x6826));
        chr_clear_action(ch);
        if (P8(G32(0x6826), 0x10d) == 0 || (P16(node, 1) >> 4) <= d) effect_remove(node, 0x4b, ch);
        else SETP16(node, 1, (u16)(P16(node, 1) - d * 16));
        return;
    }
    effect_remove(0, 0x3a, G32(0x6826));
    if (arg == 0) { P8(node, 4) = 0; effect_remove(node, 0x4b, ch); }
    if (P8(ch, 0x10d) != 0 && P8(G32(0x6826), 0x10d) != 0) P8(P32(ch, 0x108), 0x10) = 1;
}

// 2E2A:39D2  Kind 49h: a creature that lands both its claws in a round (DS:6823 counts hits)
// "Rakes..." with two extra 2d4 attacks of its second kind at the same target.
FX(fx_rake) {
    (void)arg; (void)node;
    if (G8(0x6823) != 2) return;
    farptr t = TARGET_OF(ch);
    if (P8(t, 0x10d) == 0) return;
    P8(P32(ch, 0x108), 4) = 2; P8(ch, 0x114) = 2; P8(ch, 0x116) = 2; P8(ch, 0x118) = 4; P8(ch, 0x113) = 0;
    say("Rakes...", ch);
    u8 done; fight_strike(&done, 0, 0, TARGET_OF(ch), ch);
    chr_clear_action(ch);
}
// 2E2A:3A86  Kind 4Ah, the hug of a creature holding someone (combatant slot in the node's byte 3):
// each turn one attack of 3d4 at the held victim, counted as from behind; when the victim falls,
// or the effect is asked with nothing to do or removed, the victim is let go.
FX(fx_hug) {
    SETG32(0x6826, G32(0x65b9 + P8(node, 3) * 4));
    if (arg == 0 && P8(ch, 0x10d) != 0 && P8(G32(0x6826), 0x10d) != 0) {
        P8(ch, 0x113) = 1; P8(ch, 0x114) = 0; P8(ch, 0x115) = 3; P8(ch, 0x117) = 4;
        u8 done; fight_strike(&done, 0, 1, G32(0x6826), ch);
        chr_clear_action(ch);
        if (P8(G32(0x6826), 0x10d) == 0) effect_remove(node, 0x4a, ch);
        return;
    }
    effect_remove(0, 0x3a, G32(0x6826));
    if (arg == 0) { P8(node, 4) = 0; effect_remove(node, 0x4a, ch); }
}

// 2A9F:31A2  Setting up the status-effect handler table: 138 far pointers to handler stubs (segments 1041 and 1068,
// relocated at load) from DS:682C on. Ghidra splits this routine at 3674, in the middle of an instruction; it runs to
// the RETF at 38A8.
void effect_table_init(void) {
    static const u16 tab[][3] = { {0x682c,0x1041,0x02a}, {0x6830,0x1041,0x02f}, {0x6834,0x1041,0x034}, {0x6838,0x1041,0x039}, {0x683c,0x1041,0x264}, {0x6840,0x1041,0x03e}, {0x6844,0x1041,0x043}, {0x6848,0x1041,0x048}, {0x684c,0x1041,0x04d}, {0x6850,0x1041,0x052}, {0x6854,0x1041,0x057}, {0x6858,0x1041,0x05c}, {0x685c,0x1041,0x264}, {0x6860,0x1041,0x061}, {0x6864,0x1041,0x066}, {0x6868,0x1041,0x06b}, {0x686c,0x1041,0x070}, {0x6870,0x1041,0x075}, {0x6874,0x1041,0x264}, {0x6878,0x1041,0x07a}, {0x687c,0x1041,0x07f}, {0x6880,0x1041,0x084}, {0x6884,0x1041,0x089}, {0x6888,0x1041,0x264}, {0x688c,0x1041,0x08e}, {0x6890,0x1041,0x093}, {0x6894,0x1041,0x264}, {0x6898,0x1041,0x098}, {0x689c,0x1041,0x09d}, {0x68a0,0x1041,0x0a2}, {0x68a4,0x1041,0x0f7}, {0x68a8,0x1041,0x0a7}, {0x68ac,0x1041,0x0ac}, {0x68b0,0x1041,0x0b1}, {0x68b4,0x1041,0x0b6}, {0x68b8,0x1041,0x0bb}, {0x68bc,0x1041,0x0c0}, {0x68c0,0x1041,0x05c}, {0x68c4,0x1041,0x0c5}, {0x68c8,0x1041,0x0ca}, {0x68cc,0x1041,0x0cf}, {0x68d0,0x1041,0x0d4}, {0x68d4,0x1041,0x0d9}, {0x68d8,0x1041,0x0de}, {0x68dc,0x1041,0x048}, {0x68e0,0x1041,0x04d}, {0x68e4,0x1041,0x0e3}, {0x68e8,0x1041,0x0e8}, {0x68ec,0x1041,0x0ed}, {0x68f0,0x1041,0x0f2}, {0x68f4,0x1041,0x0f7}, {0x68f8,0x1041,0x0f7}, {0x68fc,0x1041,0x0f7}, {0x6900,0x1041,0x0fc}, {0x6904,0x1041,0x101}, {0x6908,0x1041,0x106}, {0x690c,0x1041,0x10b}, {0x6910,0x1041,0x110}, {0x6914,0x1041,0x115}, {0x6918,0x1041,0x264}, {0x691c,0x1041,0x11a}, {0x6920,0x1041,0x11f}, {0x6924,0x1041,0x264}, {0x6928,0x1041,0x124}, {0x692c,0x1041,0x129}, {0x6930,0x1041,0x12e}, {0x6934,0x1041,0x133}, {0x6938,0x1041,0x138}, {0x693c,0x1041,0x13d}, {0x6940,0x1041,0x142}, {0x6944,0x1041,0x147}, {0x6948,0x1041,0x14c}, {0x694c,0x1068,0x08e}, {0x6950,0x1068,0x093}, {0x6954,0x1041,0x151}, {0x6958,0x1041,0x156}, {0x695c,0x1041,0x15b}, {0x6960,0x1041,0x160}, {0x6964,0x1041,0x165}, {0x6968,0x1041,0x16a}, {0x696c,0x1041,0x16f}, {0x6970,0x1041,0x174}, {0x6974,0x1041,0x179}, {0x6978,0x1041,0x17e}, {0x697c,0x1041,0x183}, {0x6980,0x1041,0x188}, {0x6984,0x1041,0x18d}, {0x698c,0x1041,0x192}, {0x6990,0x1041,0x197}, {0x6994,0x1041,0x19c}, {0x6998,0x1041,0x264}, {0x699c,0x1041,0x1a1}, {0x69a0,0x1041,0x1a6}, {0x69a4,0x1041,0x1ab}, {0x69a8,0x1041,0x1b0}, {0x69ac,0x1041,0x1b5}, {0x69b0,0x1041,0x1ba}, {0x69b4,0x1041,0x1bf}, {0x69b8,0x1041,0x1c4}, {0x69bc,0x1041,0x1c9}, {0x69c0,0x1041,0x1ce}, {0x69c4,0x1041,0x1d3}, {0x69c8,0x1041,0x1d8}, {0x69cc,0x1041,0x1dd}, {0x69d0,0x1041,0x1e2}, {0x69d4,0x1041,0x1e7}, {0x69d8,0x1041,0x1ec}, {0x69dc,0x1041,0x1f1}, {0x69e0,0x1041,0x1f6}, {0x69e4,0x1041,0x1fb}, {0x69e8,0x1041,0x200}, {0x69ec,0x1041,0x205}, {0x69f0,0x1041,0x20a}, {0x69f4,0x1041,0x20f}, {0x69f8,0x1041,0x214}, {0x69fc,0x1041,0x219}, {0x6a00,0x1041,0x21e}, {0x6a04,0x1041,0x223}, {0x6a08,0x1041,0x228}, {0x6a0c,0x1041,0x22d}, {0x6a10,0x1041,0x232}, {0x6a14,0x1041,0x237}, {0x6a18,0x1041,0x23c}, {0x6a1c,0x1041,0x241}, {0x6a20,0x1041,0x246}, {0x6a24,0x1041,0x264}, {0x6a28,0x1041,0x24b}, {0x6a2c,0x1041,0x24b}, {0x6a30,0x1041,0x24b}, {0x6a34,0x1041,0x250}, {0x6a38,0x1041,0x255}, {0x6a3c,0x1041,0x24b}, {0x6a40,0x1041,0x24b}, {0x6a44,0x1041,0x25a}, {0x6a48,0x1041,0x24b}, {0x6a4c,0x1041,0x25f}, {0x6a50,0x1041,0x24b}, {0x6a54,0x1041,0x24b} };
    for (unsigned i = 0; i < sizeof tab / sizeof tab[0]; i++) SETP32(FARPTR(DATA_SEG, 0), tab[i][0], FARPTR(RSEG(tab[i][1]), tab[i][2]));
}

// ---- 4C90:0004  A debugging aid: with DS:49F7 set, "c , b , a" and a label on the top line until a key. ----
void debug_numbers(u8 a, u8 b, u8 c, farptr label) {
    if (!mem[DS_LIN + 0x49f7]) return;
    farptr t = scratch_push(0x80); char q[64];
    int n = snprintf(q, sizeof q, "%u , %u , %u", c, b, a);
    mem[LIN(t)] = (u8)n; memcpy(&mem[LIN(t)] + 1, q, n);
    text_clear_box(0, 0, 0x27, 0);
    text_put_string(t, 0x0e, 0, 0);
    u8 l = mem[LIN(label)] > 0x28 ? 0x28 : mem[LIN(label)]; mem[LIN(t)] = l; memcpy(&mem[LIN(t)] + 1, &mem[LIN(label)] + 1, l);
    text_prompt(t, 0x0e);
    text_clear_box(0, 0, 0x27, 0);
    scratch_pop(0x80);
}
// ---- 2A9F:2122  Energy drain: "is drained a level" (or "N levels"); someone with status 1 is dead forever; otherwise
// per level: one more drained level (74h), hit points lost in proportion (also counted in 75h), the highest class
// (by level, then by experience - the nested 2A9F:2002) loses a level, or below level 2 the character is dead forever
// (and, drained by two, loses that class altogether); experience becomes that class's threshold. ----
static u8 drain_class(farptr ch, s32 *xp) {
    *xp = 0; s8 best = 0; u8 k = 0;
    for (int i = 0; i <= 7; i++) {
        s8 lv = (s8)P8(ch, i + 0x96);
        s32 t = 0;
        if (lv >= 2) { u16 o = (u16)(i * 0x38 + lv * 4); t = (s32)((u32)G16(o + 0x409d) << 16 | G16(o + 0x409b)); }
        if (best <= lv && (*xp <= t || t == -1)) { *xp = t; k = (u8)i; best = lv; }
    }
    return k;
}
void energy_drain(u8 levels, farptr ch) {
    #define DCS(o) FARPTR(RSEG(0x2a9f), (o))
    farptr sc = scratch_push(0x100), t = sc, n = sc + 0x80;
    if (levels < 2) { u8 *d = &mem[LIN(n)]; const u8 *s2 = &mem[LIN(DCS(0x20e7))]; memcpy(d, s2, s2[0] + 1u); }
    else { chr_number_text(n, levels); u8 *d = &mem[LIN(n)]; const u8 *s2 = &mem[LIN(DCS(0x20df))]; memcpy(d + 1 + d[0], s2 + 1, s2[0]); d[0] = (u8)(d[0] + s2[0]); }
    { u8 *d = &mem[LIN(t)]; const u8 *a = &mem[LIN(DCS(0x20ef))], *b = &mem[LIN(n)]; memcpy(d + 1, a + 1, a[0]); memcpy(d + 1 + a[0], b + 1, b[0]); d[0] = (u8)(a[0] + b[0]); if (d[0] > 0x28) d[0] = 0x28; }
    ui_char_message(1, 10, &mem[LIN(t)], ch);
    scratch_pop(0x100);
    s32 xp = 0;
    if (P8(ch, 0x10c) == 1) { fx_strike_down(&mem[LIN(DCS(0x20fb))], 8, ch); P8(ch, 0x32) = 0; return; }       // "is dead forever"
    for (u8 i = 1; levels && i <= levels; i++) {
        if (P8(ch, 0x10c) == 8) continue;
        P8(ch, 0x74)++;
        u8 total = 0; for (int k = 0; k <= 7; k++) total = (u8)(total + P8(ch, k + 0x96));
        u8 per = (u8)(P8(ch, 0x32) / total);
        debug_numbers(P8(ch, 0x75), P8(ch, 0x11b), P8(ch, 0x32), DCS(0x210b));
        for (u8 j = 1; per && j <= per; j++) {
            if (P8(ch, 0x32)) P8(ch, 0x32)--;
            if (P8(ch, 0xb1)) P8(ch, 0xb1)--;
            if (P8(ch, 0x11b)) P8(ch, 0x11b)--;
            P8(ch, 0x75)++;
        }
        if ((s8)P8(ch, 0x73) < 2) {
            fx_strike_down(&mem[LIN(DCS(0x20fb))], 8, ch); P8(ch, 0x32) = 0;
            u8 k = drain_class(ch, &xp);
            if ((s8)P8(ch, k + 0x96) > 0 && levels == 2) { k = drain_class(ch, &xp); P8(ch, k + 0x96) = 0; }
        } else {
            u8 k = drain_class(ch, &xp);
            P8(ch, k + 0x96)--;
            if (P8(ch, 0x11b) == 0 && P8(ch, 0x10c) != 6) fx_strike_down(&mem[LIN(DCS(0x2118))], 6, ch);          // "is killed"
            chr_recalc_levels(ch);
        }
        u8 k = drain_class(ch, &xp);
        P8(ch, 0x73) = P8(ch, k + 0x96);
        wr16(LIN(ch) + 0xac, (u16)xp); wr16(LIN(ch) + 0xae, (u16)((u32)xp >> 16));
        chr_recalculate(ch);
    }
    #undef DCS
}

// ---- More effect routines (2A9F). ----
#define ECS(o) (&mem[LIN(FARPTR(RSEG(0x2a9f), (o)))])
FX(fx_04_train) {                                               // 2A9F:017F (kind 4): "starts to train", then effect 7 for A8C0h
    (void)arg; (void)node;
    ui_char_message(1, 0x0a, ECS(0x16f), ch);
    effect_add(1, 0xff, 0xa8c0, 7, ch);
}
FX(fx_22_both) {                                                // 2A9F:0BCF (kind 22h): kinds 2Bh and 2Ch in turn
    effect_run(arg, node, ch, 0x2b);
    effect_run(arg, node, ch, 0x2c);
}
FX(fx_30_vs_bugbear) {                                          // 2A9F:1276 (kind 30h): -4 to the roll against a BUGBEAR or GNOLL
    UNUSED;                                                     // of creature class 1 (the one acting, DS:5D92)
    const u8 *n = &mem[LIN(SELECTED)];
    const u8 *b = ECS(0x1268), *g = ECS(0x1270);
    if (P8(SELECTED, 0x9f) == 1 && ((n[0] == b[0] && !memcmp(n + 1, b + 1, b[0])) || (n[0] == g[0] && !memcmp(n + 1, g + 1, g[0])))) ROLL -= 4;
}
FX(fx_55_drain1) { (void)arg; (void)node; energy_drain(1, TARGET_OF(ch)); }   // 2A9F:23A4 (kind 55h): the target loses a level
FX(fx_56_drain2) { (void)arg; (void)node; energy_drain(2, TARGET_OF(ch)); }   // 2A9F:23C2 (kind 56h): two levels
FX(fx_67_gaseous) {                                             // 2A9F:28BF (kind 67h): "Turns gaseous and escapes"
    (void)arg;
    P8(node, 4) = 0;
    combat_redraw_one(0, 3, ch);
    chr_take_out(ch, 3, ECS(0x28a5));
}
FX(fx_79_breath) {                                              // 2A9F:2C1C (kind 79h, with 2C34 that Ghidra split off): one time
    (void)arg;                                                  // in four, at a target within 3 squares: 8d4 (save kind 3 halves),
    farptr t = TARGET_OF(ch);                                   // then the effect and effect 50h are gone
    SETG32(0x6826, t);
    if (roll_dice(0x64, 1) > 0x19) return;
    if (combat_distance(t, ch) >= 4) return;
    chr_clear_action(ch);
    ui_char_message(1, 0x0a, ECS(0x2c11), ch);
    combat_missile_spin(0x17);
    u8 sx = combat_x(ch), sy = combat_y(ch), tx = combat_x(t), ty = combat_y(t);
    combat_missile_anim(0x1e, 1, (s8)ty, (s8)tx, (s8)sy, (s8)sx);
    u8 dmg = roll_dice_noted(4, 8);
    u8 save = rules_saving_throw(0, 3, t);
    rules_damage(save, 2, dmg, t);
    effect_remove(node, 0x79, ch);
    effect_remove(0, 0x50, ch);
}

// ---- Item powers (effect kinds 80h-8Bh, run with arg 0 when the item is readied and 1 when it is put away; the
// node is the item). ----
FX(fx_item_effect) {                                            // 2A9F:2EDA: the effect kind in item +3Dh is on while readied
    u8 k = P8(node, 0x3d);
    if (arg) { effect_remove(0, k, ch); return; }
    effect_add(0, 0x0c, 0, k, ch);
    effect_run(0, 0, ch, k);
}
FX(fx_item_ogre_power) {                                        // 2A9F:2F57: Strength 18/00 while readied (effect 26h)
    if (arg) {                                                  // put away: the first Strength effect that should go
        u8 full = P8(ch, 0x10) == 0x12 && P8(ch, 0x16) == 0x64, done = 0;
        for (farptr e = P32(ch, 0x7f); e && !done; e = P32(e, 5)) {
            if (P8(e, 0) != 0x26) continue;
            u8 pct, str; rules_strength_decode(&pct, &str, e);
            if ((full && P8(e, 0) < 0x80) || (str == 0x12 && pct == 0x64 && !full)) { effect_remove(e, 0x26, ch); done = 1; }
        }
        return;
    }
    u8 saved;
    if (rules_raise_strength(&saved, 0x64, 0x12, ch)) {
        if (G8(0x49f3) == 5) { G8(0x49f3) = 2; ui_char_message(1, 0x0a, ECS(0x2f4b), ch); G8(0x49f3) = 5; }   // "is stronger"
        else ui_char_message(1, 0x0a, ECS(0x2f4b), ch);
    }
    G8(0x6821) = 1;
    effect_add(1, saved, 0, 0x26, ch);
}
FX(fx_item_alignment) {                                         // 2A9F:30A5: an item of another alignment (+3Dh low nibble) is
    if (arg || (P8(node, 0x3d) & 0x0f) == P8(ch, 0xa0)) return; // dropped from the hand and hurts (high nibble)
    P8(node, 0x34) = 0;
    u8 dmg = P8(node, 0x3d) >> 4;
    G8(0x6819) = 8;
    if (G8(0x49f3) == 5) combat_prep_view();
    rules_damage(0, 0, dmg, ch);
    G8(0x6821) = 1;
}
FX(fx_item_giant_str) {                                         // 2A9F:3134: needs Strength 19 or more
    if (arg || P8(ch, 0x10) >= 0x13) return;
    P8(node, 0x34) = 0;
    ui_notice(ECS(0x311b));                                     // "Must have Giant Strength"
}
FX(fx_item_drop_17) { (void)node; if (arg) effect_remove(0, 0x17, ch); }   // 2A9F:317A: put away, effect 17h goes

// 2A9F:07E0 (kind 17h): while it lasts the character carries a special item (type 14h, name word F3h, +1, power 89h
// with effect 17h); taken away when the effect ends, given (if fewer than 16 items) when it starts.
FX(fx_17_gift) {
    (void)node;
    farptr it = P32(ch, 0xc8); u8 found = 0;
    while (it && !found) {
        if (P8(it, 0x2e) == 0x14 && P8(it, 0x31) == 0xf3) found = 1;
        else it = P32(it, 0x2a);
    }
    if (arg && it) item_lose(it, ch);
    if (!arg && !found && P8(ch, 0xc7) < 0x10) {
        it = rtl_getmem(0x3f);
        memset(&mem[LIN(it)], 0, 0x3f);
        P8(it, 0x2e) = 0x14; P8(it, 0x30) = 0x14; P8(it, 0x31) = 0xf3; P8(it, 0x32) = 1; P8(it, 0x3d) = 0x17; P8(it, 0x3e) = 0x89;
        item_give(it, ch);
        rtl_freemem(it, 0x3f);
        ui_char_message(1, 0x0a, ECS(0x7d2), ch);               // "Gains an item"
    }
    chr_recalculate(ch);
}
// 2A9F:1CAD (kind 53h): a stoning gaze at the target; a gazer with effect 7Fh sees it reflected by a readied mirror
// (an item with name word 76h) and suffers it instead. Saving throw kind 1, else "is Stoned" (status 7).
FX(fx_53_stone_gaze) {
    (void)arg; (void)node;
    farptr t = TARGET_OF(ch);
    SETG32(0x6826, t);
    ui_char_message(0, 0x0a, ECS(0x1c8d), ch);                  // "gazes..."
    combat_missile_spin(0x12);
    { u8 sx = combat_x(ch), sy = combat_y(ch), tx = combat_x(t), ty = combat_y(t); combat_missile_anim(0x2d, 4, (s8)ty, (s8)tx, (s8)sy, (s8)sx); }
    if (char_find_effect(0x7f, ch)) {
        u8 found = 0;
        for (farptr it = P32(G32(0x6826), 0xc8); it && !found; it = P32(it, 0x2a)) {
            if (P8(it, 0x34) && (P8(it, 0x2f) == 0x76 || P8(it, 0x30) == 0x76 || P8(it, 0x31) == 0x76)) {
                farptr m = G32(0x6826);
                ui_char_message(0, 0x0c, ECS(0x1c96), m);       // "reflects it!"
                u8 sx = combat_x(m), sy = combat_y(m), tx = combat_x(ch), ty = combat_y(ch);
                combat_missile_anim(0x2d, 4, (s8)ty, (s8)tx, (s8)sy, (s8)sx);
                found = 1;
                SETG32(0x6826, ch);
            }
        }
    }
    if (!rules_saving_throw(0, 1, G32(0x6826))) fx_strike_down(ECS(0x1ca3), 7, G32(0x6826));   // "is Stoned"
}
// 2A9F:1E77 (kind 54h): a charming gaze at a target it can see: saving throw kind 4 at -2, else effect 0Bh (charm,
// with the gazer's side in bit 7 of its byte), which then takes hold at once.
FX(fx_54_charm_gaze) {
    (void)arg; (void)node;
    farptr t = TARGET_OF(ch);
    SETG32(0x6826, t);
    farptr v = scratch_push(4);                                 // the target square (x, y) and the range, as variables
    P8(v, 0) = combat_x(t); P8(v, 1) = combat_y(t);
    u8 ok = fight_can_target(t, ch);
    if (ok) ok = line_of_sight(G32(0x6716), v + 2, v + 1, v, (s8)combat_y(ch), (s8)combat_x(ch));
    scratch_pop(4);
    if (!ok) return;
    ui_char_message(0, 0x0a, ECS(0x1e63), ch);                  // "Gazes..."
    combat_missile_spin(0x12);
    { u8 sx = combat_x(ch), sy = combat_y(ch), tx = combat_x(t), ty = combat_y(t); combat_missile_anim(0x2d, 4, (s8)ty, (s8)tx, (s8)sy, (s8)sx); }
    G8(0x681b) = 0x0a;
    u8 saved = rules_saving_throw((s8)0xfe, 4, TARGET_OF(ch));
    rules_spell_effect(ECS(0x1e6c), saved, 1, 1, (u8)(((s8)P8(ch, 0x10e) << 7) + 0x0c), 0, 0x0b, t);   // "is charmed"
    farptr e = char_find_effect(0x0b, G32(0x6826));
    if (e) effect_run(0, e, G32(0x6826), 0x0b);
}
