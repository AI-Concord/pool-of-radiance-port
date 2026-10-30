// Moving around while exploring (original overlay unit at 31F2).
#include "poolrad.h"
#include "combat.h"
#include "menu.h"
#include "text.h"
#include "map.h"
#include "view.h"
#include "rtl.h"
#include "hud.h"
#include "move.h"
#include "gfx.h"
#include "scene.h"
#include "gametime.h"

#define GAME G32(0x49d2)
#define AREA G32(0x49d6)
#define DIR G8(0x6aaf)
#define PX ((s8)G8(0x6aad))
#define PY ((s8)G8(0x6aae))

static void turn(u8 by, int with_sound) {
    DIR = (u8)((DIR + by) % 8);
    if (with_sound) sound_play(G16(0x2628));
    G8(0x6ab0) = map_wall(DIR, PY, PX);              // the wall now straight ahead
    view_render(DIR, PY, PX);
}

// The commands shared by both kinds of area. Returns 1 if the command ends the menu.
static int common_command(u8 key, int cast_needs_healthy) {
    switch (key) {
    case 'C': if (!cast_needs_healthy || P8(G32(0x5d92), 0x10c) == 0) call_original(0x32fd, 0x04a1, 0); return 0;
    case 'V': call_original_scratch(0x3c10, 0x0ab3, 64); return 0;
    case 'E': return 1;
    case 'S': SETP16(AREA, 0x594, P16(AREA, 0x594) ^ 1); return 0;                 // searching on / off
    case 'L':                                                                      // look: a careful search of this square
        SETP16(AREA, 0x594, P16(AREA, 0x594) | 2);
        time_advance(2, 1);                                                        // it takes ten minutes
        SETG16(0x49ed, G16(0x49e5));
        return 1;
    }
    return 0;
}

// 31F2:0989  Ask for the next exploring command and carry out everything except an actual step.
// Returns the key that ended the menu: 'E' for Encamp, 'L' for Look, or a movement key.
//   3D areas:   Up steps forward, Down turns round, Left / Right turn (with a step sound), and
//               A toggles the Area map, unless the area forbids it ("Not Here").
//   overhead-map areas: the four arrows set the direction of travel directly.
u8 explore_command(void) {
    u8 special, key = 0, done = 0, mode = G8(0x49fa);
    const u8 *no_prompt = &mem[LIN(FARPTR(RSEG(0x31f2), 0x097a))];
    SETP16(AREA, 0x592, 0);
    if (mode == 1) {
        do {
            key = menu_horizontal(&special, 0, 1, 15, 10, 13, &G8(0x04b6), no_prompt);     // "Area Cast View Encamp Search Look"
            if (!special) {
                if (key == 'A') {
                    if (P16(GAME, 0x1f6) == 0) { G8(0x6aac) = G8(0x6aac) ? 0 : 1; view_render(DIR, PY, PX); }
                    else text_flash_message(14, FARPTR(RSEG(0x31f2), 0x097b));              // "Not Here"
                } else done = (u8)common_command(key, 1);
            }
            else if (key == 0x48) { move_step_forward(); done = 1; }
            else if (key == 0x50) turn(4, 0);
            else if (key == 0x4b) turn(6, 1);
            else if (key == 0x4d) turn(2, 1);
            else { call_original(0x3c10, 0x27df, 1, key); hud_draw_roster(G32(0x5d92)); }
            hud_draw_status();
        } while (!done);
    } else if (mode > 1 && mode < 5) {
        if (G8(0x49f9) < 2) {                                                               // just arrived: load the map-marker icons
            sprite_load(FARPTR(RSEG(0x31f2), 0x0984), 0, 9);
            sprite_load(FARPTR(RSEG(0x31f2), 0x0984), 1, 10);
            sprite_load(FARPTR(RSEG(0x31f2), 0x0984), 2, 11);
            call_original(0x10ba, 0x2b5e, 0);
        }
        do {
            key = menu_horizontal(&special, 0, 1, 15, 10, 13, &G8(0x04df), no_prompt);
            if (!special) done = (u8)common_command(key, 0);
            else {
                done = 1;
                if (key == 0x48) DIR = 0;
                else if (key == 0x4d) DIR = 2;
                else if (key == 0x50) DIR = 4;
                else if (key == 0x4b) DIR = 6;
                else { call_original(0x3c10, 0x27df, 1, key); hud_draw_roster(G32(0x5d92)); done = 0; }
            }
            hud_draw_status();
        } while (!done);
    }
    G8(0x49f9) = G8(0x49fa);
    if (!G8(0x84e4)) { text_clear_box(1, 17, 38, 22); G8(0x84e4) = 1; }
    return key;
}

#define DX(d) ((s8)G8(0x2736 + (d)))
#define DY(d) ((s8)G8(0x273f + (d)))

// 31F2:068D  The player pressed "forward". If the way is open and the step would leave the 16x16
// level, pin the party at the edge and set the "left the map" flag for the area's script.
void move_step_forward(void) {
    s8 x = PX, y = PY; u8 dir = DIR;
    SETP16(AREA, 0x5aa, 0);
    if (map_passage(dir, y, x) == 0) return;
    s16 nx = (s16)(x + DX(dir)), ny = (s16)(y + DY(dir));
    if (nx > 15) { G8(0x6aad) = 15; SETP16(AREA, 0x5aa, 1); }
    if (nx < 0) { G8(0x6aad) = 0; SETP16(AREA, 0x5aa, 1); }
    if (ny > 15) { G8(0x6aae) = 15; SETP16(AREA, 0x5aa, 1); }
    if (ny < 0) { G8(0x6aae) = 0; SETP16(AREA, 0x5aa, 1); }
}

// 31F2:073B  Step into the next square: play the step sound, move (wrapping at the edges), note
// the wall ahead and the event number here, allow Bash / Pick / Knock again, and let game time
// pass: one minute normally, ten minutes while searching.
void move_into_square(void) {
    sound_play(G16(0x2628));
    s8 x = (s8)(PX + DX(DIR)), y = (s8)(PY + DY(DIR));
    if (x < 0) x = 15; if (x > 15) x = 0;
    if (y < 0) y = 15; if (y > 15) y = 0;
    G8(0x6aad) = (u8)x; G8(0x6aae) = (u8)y;
    G8(0x6ab0) = map_wall(DIR, y, x);
    memset(&G8(0x6dde), 1, 3);
    G8(0x6ab1) = map_event(y, x);
    time_advance((P16(AREA, 0x594) & 1) ? 2 : 1, 1);
}

// 31F2:0818  Move one square on an overhead map. The map is a 44-column grid of tile numbers in
// the data segment (DS:3648, the party's position in the game record at +186h / +188h). Some
// tiles cannot be entered at all; tiles 66h-6Ch and 72h can only be entered once game flag +344h
// is set, otherwise the party is turned back with a message. A square takes twelve hours.
void move_overland(void) {
    s8 x = (s8)(P16(GAME, 0x186) + DX(DIR)), y = (s8)(P16(GAME, 0x188) + DY(DIR));
    if (x < 0) x = 0; if (x > 15) x = 15;
    if (y < 0) y = 0; if (y > 35) y = 35;
    s8 col = (s8)(x + G8(0x3c76 + G8(0x49fa)));
    u8 t = G8(0x3648 + y * 0x2c + col);
    if (t == 0x4e || (t >= 0x7e && t <= 0x80) || (t >= 0x82 && t <= 0x84) || (t >= 0xa8 && t <= 0xac) || (t >= 0xae && t <= 0xb3) ||
        t == 0xbc || (t >= 0xdd && t <= 0xde) || (t >= 0xe1 && t <= 0xe9) || t == 0xf5) return;                  // impassable
    if (((t >= 0x66 && t <= 0x6c) || t == 0x72) && P16(GAME, 0x344) == 0) {
        text_flash_message(14, FARPTR(RSEG(0x31f2), 0x07f4));                                                   // "The river's stench drives you back."
        return;
    }
    SETP16(GAME, 0x186, (u16)(s16)x); SETP16(GAME, 0x188, (u16)(s16)y);
    time_advance(3, 12);
}

// ---- Doors ----
static u8 dice(u8 count, u8 sides) { return (u8)call_original(0x4592, 0x0de3, 2, count, sides); }   // still original code
static u8 *door_byte(s8 y, s8 x) { return &mem[LIN(G32(0x6a5c)) + 0x300 + y * 16 + x]; }
static const u8 door_keep[4] = {0xfc, 0xf3, 0xcf, 0x3f};      // mask keeping the other three sides' door bits, by dir / 2

// 31F2:0000  Remove the door on side `dir` of square (x, y) altogether, leaving a plain wall.
// Used when a locked door has nothing left to try.
void door_remove(u8 dir, s8 y, s8 x) { if (!(dir & 1) && dir < 8) *door_byte(y, x) &= door_keep[dir / 2]; }

// 31F2:0109  Unlock the door on side `dir` of square (x, y), if that square is on the map.
void door_unlock(u8 dir, s8 y, s8 x) {
    if (x < 0 || x > 15 || y < 0 || y > 15 || (dir & 1) || dir >= 8) return;
    u8 *d = door_byte(y, x);
    *d = (u8)((*d & door_keep[dir / 2]) | (1 << dir));        // door state 1 in that side's two bits
}
static void unlock_both_sides(void) {                          // a door is stored once on each of the two squares it joins
    door_unlock(DIR, PY, PX);
    door_unlock((u8)((DIR + 4) % 8), (s8)(PY + DY(DIR)), (s8)(PX + DX(DIR)));
}

// 31F2:0236  Does anyone in the party have a level in class `n`? Character bytes 96h onward hold
// the level in each single class, in the order Cleric, Druid, Fighter, Paladin, Ranger,
// Magic-User (5), Thief (6), Monk.
u8 party_has_class(s8 n) {
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) if ((s8)P8(ch, 0x96 + n) > 0) return 1;
    return 0;
}

// 31F2:0294  Bash. Each party member in turn rolls against Strength (character byte 10h, with the
// percentage for an 18 in byte 16h) until someone succeeds: the "open doors" table of the rules.
//   ordinary locked door, d6:  Str 3-7 on a 1;  8-15 on 1-2;  16-17 on 1-3;
//                              18 on 1-3, 18/51-99 on 1-4, 18/00 on 1-5
//   the sterner kind:          only 18/91 and up may try: 18/91-99 on a 1, 18/00 on 1-2, 19-20 on 1-3,
//                              21-22 on 1-4, 23 on 1-5, 24 on 1-7 of a d8, 25 always.
//                              Anyone weaker ends the party's chance to bash this door.
u8 door_bash(void) {
    u8 ok = 0;
    for (farptr ch = G32(0x5d96); ch && !ok; ch = P32(ch, 0x104)) {
        u8 str = P8(ch, 0x10), pct = P8(ch, 0x16);
        if (map_passage(DIR, PY, PX) == 3) {
            if (str == 18) {
                if (pct >= 91 && pct <= 99) { if (dice(1, 6) == 1) ok = 1; }
                else if (pct == 100) { if (dice(1, 6) < 3) ok = 1; }
                else G8(0x6dde) = 0;
            }
            else if (str == 19 || str == 20) { if (dice(1, 6) < 4) ok = 1; }
            else if (str == 21 || str == 22) { if (dice(1, 6) < 5) ok = 1; }
            else if (str == 23) { if (dice(1, 6) < 6) ok = 1; }
            else if (str == 24) { if (dice(1, 8) < 8) ok = 1; }
            else if (str == 25) ok = 1;
            else G8(0x6dde) = 0;
        } else {
            if (str >= 3 && str <= 7) { if (dice(1, 6) == 1) ok = 1; }
            else if (str >= 8 && str <= 15) { if (dice(1, 6) < 3) ok = 1; }
            else if (str >= 16 && str <= 17) { if (dice(1, 6) < 4) ok = 1; }
            else if (str == 18) {
                if (pct < 51) { if (dice(1, 6) < 4) ok = 1; }
                else if (pct <= 99) { if (dice(1, 6) < 5) ok = 1; }
                else if (pct == 100) { if (dice(1, 6) < 6) ok = 1; }
            }
        }
    }
    if (ok) unlock_both_sides();
    return ok;
}

// 31F2:04DF  Pick. Each member in turn rolls percentile dice against the open-locks skill
// (character byte 78h) and must be in good health (byte 10Ch = 0). One attempt per door.
u8 door_pick(void) {
    u8 ok = 0;
    for (farptr ch = G32(0x5d96); ch && !ok; ch = P32(ch, 0x104))
        if (dice(1, 100) <= P8(ch, 0x78) && P8(ch, 0x10c) == 0) ok = 1;
    G8(0x6ddf) = 0;
    if (ok) unlock_both_sides();
    return ok;
}

// 31F2:05AB  Which of the character's 21 memorised-spell slots (from byte 17h) holds `spell`; 255 if none.
u8 spell_slot(farptr ch, u8 spell) {
    for (u8 i = 0; i < 0x15; i++) if (P8(ch, 0x17 + i) == spell) return i;
    return 0xff;
}

// 31F2:05EE  The first party member who has `spell` memorised, or nil.
farptr party_find_spell(u8 spell) {
    farptr ch = G32(0x5d96);
    while (ch && spell_slot(ch, spell) == 0xff) ch = P32(ch, 0x104);
    return ch;
}

// 31F2:0648  Knock: if someone has the spell (number 1Fh) memorised, use it up. It always works.
u8 door_knock(void) {
    farptr ch = party_find_spell(0x1f);
    if (!ch) return 0;
    P8(ch, 0x17 + spell_slot(ch, 0x1f)) = 0;
    return 1;
}

// A locked door (way = 2) or its sterner variant (way = 3): offer whatever the party may still try.
// "Bash", " Pick" and " Knock" each disappear once tried on this square (flags DS:6DDE-6DE0, reset
// by every step); Pick also needs someone able to pick locks and Knock the means to cast it. The
// Returns 1 if the door was opened.
static u8 locked_door(u8 way) {
    u8 menu[64], special;
    static const u8 locked[] = "\x08Locked. ";
#define ADD(str) do { const char *a_ = (str); while (*a_ && menu[0] < 40) menu[++menu[0]] = (u8)*a_++; } while (0)
    menu[0] = 0;
    if (G8(0x6dde)) ADD("Bash");
    if (G8(0x6ddf) && party_has_class(6)) ADD(" Pick");
    if (G8(0x6de0) && (way == 2 ? party_find_spell(0x1f) != 0 : party_has_class(5) != 0)) ADD(" Knock");
    int nothing_to_try = (menu[0] == 0);
    ADD(" Exit");
#undef ADD
    if (nothing_to_try) { door_remove(DIR, PY, PX); return 0; }
    u8 key = menu_horizontal(&special, 0, 0, 15, 10, 13, menu, locked);
    if (key == 'B') return door_bash();
    if (key == 'P') { if (way == 2) return door_pick(); G8(0x6ddf) = 0; return 0; }
    if (key == 'K') return door_knock();
    return 0;
}

// 31F2:0D6D  Carry out the move chosen by explore_command: step if the way is open, bump (with a
// thud) if it is not, deal with a locked door, and drop whatever scene picture and portrait were
// on show.
int move_party(void) {
    u8 mode = G8(0x49fa);
    if (mode == 1) {
        if (P16(AREA, 0x592) < 0xff) {
            u8 way = map_passage(DIR, PY, PX), moved = 0;
            if (way == 1) moved = 1;
            else if (way == 2 || way == 3) moved = locked_door(way);
            if (moved) move_into_square(); else sound_play(G16(0x2622));
            hud_draw_status();
        } else SETP16(AREA, 0x592, 0);
    } else if (mode > 1 && mode < 5) {
        if (P16(AREA, 0x592) < 0xff) { move_overland(); hud_draw_status(); }
        else SETP16(AREA, 0x592, 0);
    }
    scene_free_anim(FARPTR(DATA_SEG, 0x6abe));
    gfx_free_pic(FARPTR(DATA_SEG, 0x6b01));
    gfx_free_pic(FARPTR(DATA_SEG, 0x6b06));
    G8(0x6b00) = 0xff; G8(0x6b05) = 0xff;
    return 1;
}
