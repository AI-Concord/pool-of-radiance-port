// The exploring screen's fixed displays (original resident unit at 10BA).
#include <stdio.h>
#include "poolrad.h"
#include "text.h"
#include "hud.h"
#include "gfx.h"
#include "chr.h"
#include "scene.h"
#include "view.h"
#include "rtl.h"
#include "map.h"

#define GAME G32(0x49d2)
#define AREA G32(0x49d6)

// 10BA:290B  The status line under the party roster: "15,4 W 10:50", with " search" or " camping"
// after it when that applies. The coordinates are left out in areas that hide the Area map. On the
// overhead maps the position shown is the party's place on the whole wilderness grid.
void hud_draw_status(void) {
    static const char *facing[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    char line[96], *p = line;
    u8 x = G8(0x6aad), y = G8(0x6aae);
    if (G8(0x49fa) > 1) {
        x = (u8)P16(GAME, 0x186); y = (u8)P16(GAME, 0x188);
        if (G8(0x49fa) == 3) x = (u8)(x + 13);
        if (G8(0x49fa) == 4) x = (u8)(x + 26);
    }
    char hours[8], minutes[8];
    snprintf(hours, sizeof hours, "%02u", P16(GAME, 0x192)); hours[2] = 0;
    snprintf(minutes, sizeof minutes, "%02u", (u16)(P16(GAME, 0x190) * 10 + P16(GAME, 0x18e))); minutes[2] = 0;
    if (P16(GAME, 0x1f6) == 0) p += sprintf(p, "%u,%u ", x, y);
    p += sprintf(p, "%s %s:%s", facing[G8(0x6aaf) & 7], hours, minutes);
    if (G8(0x49f3) == 2) p += sprintf(p, " camping");
    else if (P16(AREA, 0x594) & 1) p += sprintf(p, " search");
    u8 s[42]; size_t n = (size_t)(p - line); if (n > 40) n = 40;
    s[0] = (u8)n; memcpy(s + 1, line, n);
    text_clear_box(17, 15, 38, 15);
    text_put_pstr(s, 10, 15, 17);
}

// ---- The party roster ----
static void put_cstr(const char *t, u8 color, u8 row, u8 col) {
    u8 s[64]; size_t n = strlen(t); s[0] = (u8)n; memcpy(s + 1, t, n);
    text_put_pstr(s, color, row, col);
}

// 10BA:1816  A character's name in the colour of their condition: light red if byte 10Dh is zero,
// yellow if byte 10Eh is 1, light cyan otherwise. `mark` adds the marker string after the name.
void hud_draw_name(u8 mark, u8 row, u8 col, farptr ch) {
    u8 color = P8(ch, 0x10d) == 0 ? 12 : P8(ch, 0x10e) == 1 ? 14 : 11;
    text_put_string(ch, color, row, col);
    if (mark) text_put_string(FARPTR(RSEG(0x10ba), 0x1813), color, row, (u8)(col + P8(ch, 0)));
}

// 10BA:08EF  Armour class. It is stored as 60 minus the AC (character byte 111h), so 59 shows as
// 1 and anything above 60 as a negative number.
void hud_draw_ac(u8 row, u8 col, farptr ch) {
    char t[8]; int v = P8(ch, 0x111) - 0x3c;
    snprintf(t, sizeof t, "%s%d", P8(ch, 0x111) > 0x3c ? "-" : "", v < 0 ? -v : v);
    put_cstr(t, 10, row, col);
}

// 10BA:0973  Hit points (character byte 11Bh): green at full strength, yellow when below the
// maximum (byte 32h), magenta when `highlight` is set.
void hud_draw_hp(u8 highlight, u8 row, u8 col, farptr ch) {
    char t[8]; snprintf(t, sizeof t, "%u", P8(ch, 0x11b));
    u8 color = P8(ch, 0x11b) < P8(ch, 0x32) ? 14 : 10;
    if (highlight) color = 13;
    put_cstr(t, color, row, col);
}

// 10BA:0767  The roster: "Name ... AC  HP" and one line per party member, the selected one in
// white. It sits at the left edge on the full-width screens and beside the picture otherwise.
void hud_draw_roster(farptr selected) {
    u8 col = G8(0x49f3) == 0 ? 1 : 17, row = 2;
    put_cstr("Name", 15, row, col);
    put_cstr("AC  HP", 15, row, 33);
    row = 4;
    for (farptr ch = G32(0x5d96); ch; ch = P32(ch, 0x104)) {
        text_clear_box((s8)col, (s8)row, 38, (s8)row);
        if (ch == selected) text_put_string(ch, 15, row, col);
        else hud_draw_name(0, row, col, ch);
        u8 ac = P8(ch, 0x111), pad = ac < 0x33 ? 1 : ac <= 0x3c ? 2 : ac <= 0x45 ? 1 : 0;        // right-align the number
        hud_draw_ac(row, (u8)(pad + 32), ch);
        u8 hp = P8(ch, 0x11b); pad = hp < 10 ? 2 : hp <= 99 ? 1 : 0;
        hud_draw_hp(0, row, (u8)(pad + 36), ch);
        row++;
    }
    text_clear_box((s8)col, (s8)row, 38, (s8)row);
}

// ---- Screen layouts ----
static void frame(u8 bottom, u8 right, u8 top, u8 left) { ui_frame(FARPTR(RSEG(0x10ba), 0x2729), 15, 0, bottom, right, top, left); }

// 10BA:272A  The standard three-pane frame (roster, text window, picture), plus the inner picture
// border when `picture_border` is set.
void hud_draw_frames(u8 picture_border) {
    frame(22, 38, 1, 1);
    frame(22, 38, 17, 1);
    frame(15, 15, 1, 1);
    if (picture_border) frame(13, 13, 3, 3);
}

// 10BA:27D9  Redraw the whole screen for the current kind of display (DS:49F3): 0 a plain frame,
// 1 a portrait, 2 the camp, 3 and 4 exploring (without and with the inner picture border), 6 a
// screen with the first scene picture.
void hud_draw_screen(void) {
    farptr sel = G32(0x5d92);
    switch (G8(0x49f3)) {
    case 0: ui_frame(FARPTR(RSEG(0x10ba), 0x27d4), 15, 0, 22, 38, 1, 1); break;
    case 1:
        hud_draw_frames(1);
        scene_load_portrait(G8(0x49fe), G8(0x49fd));
        scene_draw_portrait(1, 3, 3);
        break;
    case 2: case 6: {
        u8 id = G8(0x49f3) == 2 ? 0x1d : 1;
        hud_draw_frames(1);
        if (!scene_load_anim(FARPTR(DATA_SEG, 0x6abe), 0, id, "PIC"))
            call_original(0x489f, 0x00a2, 6, RSEG(0x10ba), 0x27d5, id, 0, DATA_SEG, 0x6abe);
        hud_draw_roster(sel);
        if (G8(0x49f3) == 2) hud_draw_status();
        break;
    }
    case 3: case 4:
        hud_draw_frames(G8(0x49f3) == 4);
        view_redraw();
        hud_draw_roster(sel);
        hud_draw_status();
        break;
    }
}

// ---- Frames ----
// 13F1:0000  Draw the twisted-cord border just outside the cell rectangle (left,top)-(right,bottom),
// straight onto the screen, from three symbols of the fixed symbol set: corner, vertical and
// horizontal. `style` selects one of the border designs (three symbols each, from number 114h).
void ui_border(u8 style, s8 bottom, s8 right, s8 top, s8 left) {
    left--; top--;
    s8 r = (s8)(right + 1), b = (s8)(bottom + 1);
    u16 base = (u16)(style * 3);
    sym_put(0, 0, (u16)(base + 0x114), top, left);
    sym_put(0, 0, (u16)(base + 0x114), top, r);
    sym_put(0, 0, (u16)(base + 0x114), b, left);
    sym_put(0, 0, (u16)(base + 0x114), b, r);
    for (s8 c = (s8)(left + 1); c <= right; c++) {
        sym_put(0, 0, (u16)(base + 0x116), top, c);
        sym_put(0, 0, (u16)(base + 0x116), b, c);
        if (c == right) break;
    }
    for (s8 y = (s8)(top + 1); y <= bottom; y++) {
        sym_put(0, 0, (u16)(base + 0x115), y, left);
        sym_put(0, 0, (u16)(base + 0x115), y, r);
        if (y == bottom) break;
    }
}

// 13F1:02E8  A framed window: blank the cells, draw the border, and centre `title` (if any) on
// the top row.
void ui_frame(farptr title, u8 title_color, u8 style, u8 bottom, u8 right, u8 top, u8 left) {
    u8 t[41]; u8 n = P8(title, 0) > 40 ? 40 : P8(title, 0);
    t[0] = n; memcpy(t + 1, &mem[LIN(title) + 1], n);
    text_clear_box((s8)left, (s8)top, (s8)right, (s8)bottom);
    ui_border(style, (s8)bottom, (s8)right, (s8)top, (s8)left);
    if (n) text_put_pstr(t, title_color, top, (u8)(((s16)(s8)left + (s16)(s8)right - n) / 2));
}

// 10BA:17E3  Clear the message area: the lower window when exploring, the right-hand panel in combat.
void ui_clear_message(void) {
    if (G8(0x49f3) == 5) text_clear_box(0x17, 10, 0x26, 0x15); else text_clear_box(1, 0x12, 0x26, 0x16);
}

// 10BA:1693  A one-line notice on the bottom row: shown, held until a key, then wiped.
void ui_notice(const u8 *text) {
    u8 s[41]; u8 n = text[0] > 40 ? 40 : text[0];
    s[0] = n; memcpy(s + 1, text + 1, n);
    text_clear_box(0, 0x18, 0x27, 0x18);
    text_put_pstr(s, 10, 0x18, 0);
    text_pause();
    text_clear_box(0, 0x18, 0x27, 0x18);
}

// 10BA:16EC  "<name>" and under it a line of text about that character, in the message area
// (in combat, in the right-hand panel starting at `combat_row`). With `wait` it is held until a
// key and then cleared. While the rest screen is up (DS:6DDA) the window's border is left alone
// and the message starts a row lower.
void ui_char_message(u8 wait, u8 combat_row, const u8 *text, farptr ch) {
    if (G8(0x49f3) == 5) {
        text_clear_box(0x17, (s8)combat_row, 0x26, 0x15);
        hud_draw_name(0, combat_row, 0x17, ch);
        text_print_wrapped_pstr(text, 1, 10, 0x15, 0x26, (u8)(combat_row + 1), 0x17);
    } else {
        u8 row = 0x12;
        if (G8(0x6dda) == 0) { row = 0x11; ui_border(0, 0x16, 0x26, 0x11, 1); }
        text_clear_box(1, (s8)row, 0x26, 0x16);
        hud_draw_name(0, (u8)(row + 1), 1, ch);
        text_print_wrapped_pstr(text, 1, 10, 0x16, 0x26, (u8)(row + 2), 1);
    }
    if (wait) { text_pause(); ui_clear_message(); }
}

// 10BA:09E3  The combat side panel for one combatant: name, hit points, AC, the readied weapon, and a status
// line if it is out of the fight or held ("(Helpless)"). DS:5E25 keeps the row below the AC line.
void hud_combat_panel(farptr ch) {
    static const u8 blank[] = "\x01" " ";
    u8 r = 1;
    text_clear_box(0x17, 1, 0x26, 0x15);
    ui_char_message(0, r, blank, ch);
    r++;
    text_put_string(FARPTR(RSEG(0x10ba), 0x09cb), 10, (u8)(r + 1), 0x17);     // "Hitpoints"
    hud_draw_hp(0, (u8)(r + 1), 0x21, ch);
    r += 2;
    text_put_string(FARPTR(RSEG(0x10ba), 0x09d5), 10, (u8)(r + 1), 0x17);     // "AC"
    hud_draw_ac((u8)(r + 1), 0x1a, ch);
    G8(0x5e25) = (u8)(r + 1);
    farptr w = P32(ch, 0xcc);
    if (w) {
        r += 2;
        chr_item_name(0, 0, 0, 0, w);
        text_print_wrapped(w, 1, 10, (u8)(r + 3), 0x26, (u8)(r + 1), 0x17);
    }
    r = (u8)(G8(0x5e25) + 1);
    if (P8(ch, 0x10d) == 0)
        text_put_string(FARPTR(DATA_SEG, (u16)(0x1047 + (s8)P8(ch, 0x10c) * 13)), 15, (u8)(r + 1), 0x17);   // status name, 13 bytes each
    else if (chr_has_listed_effect(ch))
        text_put_string(FARPTR(RSEG(0x10ba), 0x09d8), 15, (u8)(r + 1), 0x17);  // "(Helpless)"
}

// ---- 1C88:0000  Waiting n seconds or until a key is pressed, then flushing the keyboard ----
void wait_seconds_or_key(u16 n) {
    u32 end = game_clock() + (u16)(n * 100);
    for (;;) {
        u32 now = game_clock();
        if ((s16)(now >> 16) > (s16)(end >> 16) || ((s16)(now >> 16) == (s16)(end >> 16) && (u16)now >= (u16)end)) break;
        if (crt_key_pressed()) break;
    }
    keys_flush();
}

// ---- 1C88:0206  The title page: two frames, the credits in fixed text lines, then a 10-second wait ----
void title_screen(void) {
    static const u8 frames[2][6] = { {1,1,0x26,3,0,0}, {1,5,0x26,0x17,0,0} };     // left, top, right, bottom, style, colour as pushed
    for (int i = 0; i < 2; i++) {
        const u8 *p = frames[i];
        ui_frame(FARPTR(RSEG(0x1c88), 0x4a), p[5], p[4], p[3], p[2], p[1], p[0]);
    }
    static const u16 lines[][4] = {      // string at CS, column, row, colour
        {0x4b,5,1,10}, {0x60,0x1a,1,14}, {0x6a,0xf,2,10}, {0x73,2,3,10}, {0x98,1,5,10}, {0xa9,0x12,5,14},
        {0xbe,0xe,6,11}, {0xca,2,7,14}, {0xd7,0x14,7,14}, {0xe0,2,8,11}, {0xca,0x14,8,14}, {0xed,2,9,11},
        {0xf8,0x14,9,11}, {0x104,2,10,11}, {0xed,0x14,10,11}, {0x10f,2,11,11}, {0x119,2,13,14}, {0x127,0x14,13,14},
        {0x139,2,14,11}, {0x142,0x14,14,11}, {0x14e,2,15,11}, {0x104,0x14,15,11}, {0x159,2,16,11}, {0x167,0x14,16,11},
        {0x175,2,17,11}, {0x184,0x14,17,11}, {0x191,2,19,14}, {0x1a1,0x14,19,14}, {0x167,2,20,11}, {0x1ac,0x14,20,11},
        {0x1be,2,22,14}, {0x1c7,0xb,22,11}, {0x1e2,2,23,11},
    };
    for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; i++)
        text_put_string(FARPTR(RSEG(0x1c88), lines[i][0]), (u8)lines[i][3], (u8)lines[i][2], (u8)lines[i][1]);
    wait_seconds_or_key(10);
}

// ---- 1C88:0627  The opening: the "title" picture (id 1) for 5 seconds or a key; with the option at DS:49D1 also
// the second picture (loaded into the same variable, which the loader frees first), shown for 5 seconds (500 clock
// ticks) or until a key, during which (in mode DS:4A1E=1) random flickers flash the palette; then the title page.
// DS:547C (the data path) is blanked while the pictures load. ----
static void intro_path(farptr saved) { u8 n = mem[LIN(saved)]; G8(0x547c) = n; memcpy(&G8(0x547d), &mem[LIN(saved)] + 1, n); }
void intro(void) {
    farptr sc = scratch_push(0x34), saved = sc, var = sc + 0x30;
    u8 n = G8(0x547c) > 0x28 ? 0x28 : G8(0x547c); mem[LIN(saved)] = n; memcpy(&mem[LIN(saved)] + 1, &G8(0x547d), n);
    SETP32(var, 0, 0);
    G8(0x547c) = 0;
    call_original(0x1464, 0x0111, 7, RSEG(0x1c88), 0x0621, 1, 0, 0, FP_SEG(var), FP_OFF(var));        // "title"
    call_original(0x1464, 0x1951, 4, 0, 0, FP_SEG(P32(var, 0)), FP_OFF(P32(var, 0)));
    wait_seconds_or_key(5);
    if (!G8(0x49d1)) { intro_path(saved); scratch_pop(0x34); return; }
    call_original(0x1464, 0x0111, 7, RSEG(0x1c88), 0x0621, 2, 0, 0, FP_SEG(var), FP_OFF(var));
    intro_path(saved);
    call_original(0x1464, 0x1951, 4, 0, 0, FP_SEG(P32(var, 0)), FP_OFF(P32(var, 0)));
    call_original(0x1464, 0x2595, 2, FP_SEG(var), FP_OFF(var));
    s32 end = (s32)game_clock() + 500;
    u32 acc = 0;
    do {
        if (G8(0x4a1e) == 1) {
            acc += rtl_random(4);
            if ((s32)acc > 50000) {
                acc = 0;
                u8 times = (u8)(rtl_random(3) + 1);
                static const u8 col[6] = { 9, 0x0b, 0x0f, 0x0b, 9, 1 }; static const u16 ms[5] = { 0x96, 100, 0x32, 100, 0x96 };
                for (u8 b = 0; ; b++) {
                    for (int i = 0; i < 6; i++) { gfx_palette_reg(1, col[i]); if (i < 5) rtl_delay(ms[i]); }
                    if (b == times) break;
                }
            }
        }
    } while (!crt_key_pressed() && (s32)game_clock() < end);
    keys_flush();
    scratch_pop(0x34);
    title_screen();
}

// ---- 1D09:00FD  The copy protection: a random one of 13 entries of the code wheel (DS: entry*15h; code word at +2,
// the Espruar runes at +9/+10, the Dethek runes at +11/+12 (FFh = none), the path at +0Dh) is shown, and the code
// word asked for; three wrong answers end the game. ----
void code_wheel(void) {
    #define WCS(o) FARPTR(RSEG(0x1d09), (o))
    for (u8 tries = 1; ; tries++) {
        u8 e = (u8)rtl_random(0x0d);
        u16 b = (u16)(e * 0x15);
        ui_frame(WCS(0x0000), 0, 0, 0x16, 0x26, 1, 1);
        text_put_string(WCS(0x0001), 10, 1, 1); text_put_string(WCS(0x0027), 10, 2, 1); text_put_string(WCS(0x0036), 10, 5, 1);
        text_put_char(8, 6, 0x0c, 1, (u8)(G8(b + 9) + 0x40), 0);
        text_put_char(9, 6, 0x0c, 1, (u8)(G8(b + 10) + 0x40), 0);
        text_put_string(WCS(0x005a), 10, 8, 1);
        text_put_char(8, 9, 0x0c, 1, (u8)(G8(b + 11) + 0x40), 0);
        if (G8(b + 12) != 0xff) text_put_char(9, 9, 0x0c, 1, (u8)(G8(b + 12) + 0x40), 0);
        text_put_string(WCS(0x007c), 10, 0x0b, 1);
        farptr sc = scratch_push(0x200), t = sc, ans = sc + 0x100;
        { u8 *m = &mem[LIN(t)]; const u8 *a = &mem[LIN(WCS(0x00a3))], *p = &G8(b + 0x0d); int n = a[0]; memcpy(m + 1, a + 1, n); int c = p[0]; if (n + c > 255) c = 255 - n; memcpy(m + 1 + n, p + 1, c); m[0] = (u8)(n + c); }
        text_put_string(t, 0x0c, 0x0d, 1);
        text_put_string(WCS(0x00a9), 10, 0x0e, 1);
        keys_flush();
        text_input_string(6, 0x0d, WCS(0x00ce), ans);
        u8 *a = &mem[LIN(ans)]; if (a[0] > 0x28) a[0] = 0x28;
        const u8 *w = &G8(b + 2);
        int ok = a[0] == w[0] && !memcmp(a + 1, w + 1, w[0]);
        scratch_pop(0x200);
        if (ok) return;
        text_flash_message(0x0e, WCS(0x00e4));                                                     // "Sorry, that's incorrect."
        if (tries == 3) game_quit();
    }
    #undef WCS
}

// 4876:0207  The exploring screen's three frames: the 3D view (1,1)-(15,15), the roster (17,1)-(38,15) and the text
// area (1,17)-(38,22), all untitled.
void hud_explore_frames(void) {
    farptr t = FARPTR(RSEG(0x4876), 0x206);                     // an empty title
    ui_frame(t, 0, 0, 0x0f, 0x0f, 1, 1);
    ui_frame(t, 0, 0, 0x0f, 0x26, 1, 0x11);
    ui_frame(t, 0, 0, 0x16, 0x26, 0x11, 1);
}
