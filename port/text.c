// Text output (original unit at 1709, resident). EGA paths only.
//
// The screen is 40 columns by 25 rows of 8x8 cells. Glyphs come from the font block at DS:5E20
// (8 bytes per character, one bit per pixel). Characters are written to two video segments at
// once, DS:4A16 (the page being displayed) and DS:4A18 (the "work" page, set by text_set_work_page);
// normally both are A000h.
#include "poolrad.h"
#include "ega.h"
#include "text.h"
#include "rtl.h"
#include "input.h"
#include "dax.h"

static inline void vwrite(u16 seg, u16 off, u8 v) { ega_write((u16)(((u16)(seg - 0xa000) << 4) + off), v); }

// 1709:0000  Put the EGA graphics controller and plane mask back to their defaults.
void text_reset_ega(void) {
    static const u8 gc[][2] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {7, 0x0f}, {8, 0xff}};
    for (int i = 0; i < 8; i++) { ega_out(0x3ce, gc[i][0]); ega_out(0x3cf, gc[i][1]); }
    ega_out(0x3c4, 2); ega_out(0x3c5, 0x0f);
}

// 1709:00A0  Select the video page that drawing goes to (page n starts at segment A000h + n * 200h).
void text_set_work_page(u8 page) { SETG16(0x4a18, (u16)(page * 0x200 + 0xa000)); }

// 1709:00B7  Fill the character cells (col1,row1)-(col2,row2) of `page` with a colour.
void text_fill_cells(u8 color, u8 page, s8 row2, s8 col2, s8 row1, s8 col1) {
    u8 saved = G8(0x4a1a);
    u16 off = (u16)(row1 * 0x140 + col1);
    s16 last = (s16)((row2 - row1 + 1) * 8 - 1), n = (s16)(col2 - col1 + 1);
    text_set_work_page(page);
    text_reset_ega();
    ega_out(0x3ce, 5); ega_out(0x3cf, 2);            // write mode 2: the byte written is a colour
    for (s16 line = 0; ; line++) {
        for (u16 i = 0; i < (u16)n; i++) vwrite(G16(0x4a18), (u16)(off + i), color);
        off = (u16)(off + 40);
        if (line == last) break;
    }
    ega_out(0x3ce, 5); ega_out(0x3cf, 0);
    G8(0x4a1a) = saved;
    text_set_work_page(G8(0x4a1a));
}

// 1709:01DF  Draw `count` copies of a character at (col,row) in `color`. With `fold` set the
// character is folded into the font's 64 glyphs: upper-cased, then taken modulo 64.
void text_put_char(u8 col, u8 row, u8 color, u8 count, u8 ch, u8 fold) {
    farptr font = G32(0x5e20);
    if (!(col < 40 && row < 25 && font)) return;
    u8 glyph = ch;
    if (fold) { if (ch >= 'a' && ch <= 'z') ch = (u8)(ch - 32); glyph = ch % 0x40; }
    if (!count) return;
    for (u8 k = 1; ; k++) {
        u16 off = (u16)(row * 0x140 + col);
        ega_out(0x3c4, 2);
        for (u8 line = 0; line < 8; line++) {
            for (u8 e = 0; e < 4; e++) {
                u8 plane = G8(0x25c9 + 3 - e);
                ega_out(0x3c5, plane);
                u8 v = (plane & color) ? P8(font, (u16)(glyph * 8 + line)) : 0;
                vwrite(G16(0x4a16), off, v);
                vwrite(G16(0x4a18), off, v);
            }
            off = (u16)(off + 40);
        }
        ega_out(0x3c5, 0x0f);
        col++;
        if (k == count) break;
    }
}

// 1709:05EB  Draw `count` blanks.
void text_put_blanks(u8 count, u8 color, u8 row, u8 col) {
    if (col < 40 && row < 25) text_put_char(col, row, color, count, ' ', 1);
}

// 1709:0626  Draw a Pascal string.
void text_put_string(farptr str, u8 color, u8 row, u8 col) { text_put_pstr(&mem[LIN(str)], color, row, col); }

// The same for a Pascal string held in a C buffer (length byte first).
void text_put_pstr(const u8 *s, u8 color, u8 row, u8 col) {
    u8 len = s[0];
    if (!(col < 40 && row < 40 && len)) return;
    for (u16 i = 1; i <= len; i++) text_put_char(col++, row, color, 1, s[i], 1);
}

// 1709:0DD3  Blank the character cells (left,top)-(right,bottom) on both video pages.
void text_clear_box(s8 left, s8 top, s8 right, s8 bottom) {
    text_fill_cells(0, 0, bottom, right, top, left);
    text_fill_cells(0, 1, bottom, right, top, left);
}

// 1709:0BF5  Show a message on the bottom line and wait for a key.
void text_prompt(farptr str, u8 color) {
    text_put_blanks(40, 0, 24, 0);
    text_put_string(str, color, 24, 0);
    input_wait_key();
}

// 1709:0768  Print a string word-wrapped inside the window (left,top)-(right,bottom), continuing at
// the text cursor (DS:5E24 column, DS:5E25 row). With `clear` set the window is blanked first.
// When the window fills up, the player is asked to press a key and printing starts again at the top.
// Its two nested helpers are inline below: EMIT is 1709:06A7, the space skip after a line break 1709:0714.
//
// A "word" here is: any leading punctuation, then characters up to the next space or punctuation,
// then any punctuation that directly follows, so that "DEATH!'" never has its "!" pushed to the next line.
static int is_punct(u8 c) { return c >= 32 && c < 64 && (G8(0xd16 + ((c - 32) >> 3)) >> ((c - 32) & 7)) & 1; }   // the set ! , - . : ; ?

void text_print_wrapped(farptr str, u8 clear, u8 color, u8 bottom, u8 right, u8 top, u8 left) { text_print_wrapped_pstr(&mem[LIN(str)], clear, color, bottom, right, top, left); }

void text_print_wrapped_pstr(const u8 *str, u8 clear, u8 color, u8 bottom, u8 right, u8 top, u8 left) {
    u8 s[257]; memset(s, 0, sizeof s); memcpy(s, str, (size_t)str[0] + 1);        // s[0] is the length, text is s[1..]
    if (!(left < 40 && top < 25 && right < 40 && bottom < 40)) return;
    if (G8(0x5e24) < left || right < G8(0x5e24) || G8(0x5e25) < top || bottom < G8(0x5e25)) { G8(0x5e24) = left; G8(0x5e25) = top; }
    if (clear) { text_clear_box((s8)left, (s8)top, (s8)right, (s8)bottom); G8(0x5e24) = left; G8(0x5e25) = top; }
    u8 start = 1, len = s[0], i;
    if (!len) return;
#define EMIT() while (start <= i) { \
        text_put_char(G8(0x5e24), G8(0x5e25), color, 1, s[start], 1); \
        if (G8(0x4a00)) rtl_delay((u16)(G8(0x49e2) * 10));       /* letter-by-letter printing speed */ \
        start++; G8(0x5e24)++; }
    for (;;) {
        i = start;
        while (i < len && is_punct(s[i])) i++;
        while (i < len && !is_punct(s[i]) && s[i] != ' ') i++;
        if (s[i] != ' ') while (i + 1 < len && is_punct(s[i + 1])) i++;
        if (i - start + G8(0x5e24) > right) {                     // the word does not fit on this line
            if (i - start + G8(0x5e24) == right && s[i] == ' ') { i--; EMIT(); }
            G8(0x5e24) = left; G8(0x5e25)++;
            while (start < len && s[start] == ' ') start++;
            if (G8(0x5e25) > bottom && start < len) {             // the window is full
                G8(0x5e24) = left; G8(0x5e25) = top;
                text_prompt(FARPTR(RSEG(0x1709), 0x074e), 13);    // "Press any key to continue"
                text_clear_box((s8)left, (s8)top, (s8)right, (s8)bottom);
                EMIT();
            }
        } else EMIT();
        if (start > len) break;
    }
#undef EMIT
    if (G8(0x5e24) > right) { G8(0x5e24) = left; G8(0x5e25)++; }
}

// 1709:0CAB  Blank the whole screen on both video pages.
void text_clear_screen(void) {
    text_fill_cells(0, 0, 24, 39, 0, 0);
    text_fill_cells(0, 1, 24, 39, 0, 0);
}

// 1709:0DBE  The standard pause after a message; its length follows the game speed setting (DS:49E2).
void text_pause(void) { rtl_delay((u16)(G8(0x49e2) * 0xe1)); }

// 1709:0D68  Show a message on the bottom line for a moment, then erase it.
void text_flash_message(u8 color, farptr str) {
    text_put_blanks(40, 0, 24, 0);
    text_put_string(str, color, 24, 0);
    text_pause();
    text_put_blanks(40, 0, 24, 0);
}

// 1709:09E3  Ask for a line of text on the bottom row: `prompt` in `color`, then up to `maxlen`
// typed characters in white, with Backspace to correct and Return or Esc to finish. The answer is
// upper-cased and stored in `result` (a Pascal string of at most 40 characters).
void text_input_pstr(u8 maxlen, u8 color, const u8 *prompt, u8 *result) {
    u8 buf[256]; u8 len = 0, k;
    text_put_blanks(40, 0, 24, 0);
    text_put_pstr(prompt, color, 24, 0);
    u8 col = prompt[0] > 40 ? 40 : prompt[0];
    do {
        k = input_get_key();
        if (k >= 0x20 && k <= 0x7a) {
            if (len < maxlen) { buf[len++] = k; col++; text_put_char(col, 24, 15, 1, k, 1); }
        } else if (k == 8 && len != 0) {
            len--;
            text_put_blanks(1, 0, 24, col);
            col--;
        }
    } while (k != 13 && k != 27);
    text_put_blanks(40, 0, 24, 0);
    for (u8 i = 0; i < len; i++) if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] = (u8)(buf[i] - 32);
    if (len > 40) len = 40;
    result[0] = len; memcpy(result + 1, buf, len);
}
void text_input_string(u8 maxlen, u8 color, farptr prompt, farptr result) { text_input_pstr(maxlen, color, &mem[LIN(prompt)], &mem[LIN(result)]); }

// 1709:0B66  Ask for a number on the bottom row: up to 6 characters through the string input, read with Val, asked
// again until it is a number from 0 to 65536. 65536 itself passes and comes back as 0, as in the original.
u16 text_input_number(u8 color, farptr prompt) {
    farptr sc = scratch_push(0x170), p = sc, r = sc + 0x30, t = sc + 0x130, code = sc + 0x160;
    u8 n = P8(prompt, 0) > 0x28 ? 0x28 : P8(prompt, 0); memmove(&mem[LIN(p) + 1], &mem[LIN(prompt) + 1], n); P8(p, 0) = n;
    s32 v; u16 c;
    do {
        text_input_string(6, color, p, r);
        n = P8(r, 0) > 0x28 ? 0x28 : P8(r, 0); memmove(&mem[LIN(t) + 1], &mem[LIN(r) + 1], n); P8(t, 0) = n;
        v = rtl_val(t, code);   // Val
        c = rd16(LIN(code));
        if (!c && (v > 0x10000 || v < 0)) c = 1;
    } while (c);
    scratch_pop(0x170);
    return (u16)v;
}

// 1709:0071  Show display page n (Intr 10h, AH 5); DS:4A16 becomes that page's video segment, A000h + n * 200h.
void text_show_page(u8 n) {
    G8(0x4a03) = 5; G8(0x4a02) = n;
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));
    wr16(DS_LIN + 0x4a16, (u16)((n << 9) + 0xa000));
}
// 1709:017C  The character set: block C9h of 8X8d1.dax copied over the font buffer at DS:5E20.
void text_load_font(void) {
    farptr v = scratch_push(6);
    dax_load(v, v + 4, 0xc9, ".dax", "8X8d1");
    u16 size = rd16(LIN(v) + 4); farptr data = rd32(LIN(v));
    if (size) { memmove(&mem[LIN(G32(0x5e20))], &mem[LIN(data)], size); rtl_freemem(data, size); }
    scratch_pop(6);
}

// ---- 1802:0040 / 0105 / 0175  Text through the BIOS (used by the "insert disk" prompt): cursor to (row, col) on
// the work page DS:4A1B, then `count` copies of a character in a colour (mapped through DS:25D5 on CGA, DS:4A1E 0);
// on EGA (DS:4A1E 1) the same again on the display page DS:4A1A. The runtime's Intr saves the flags with the
// registers (DS:4A14); the last call's come from an XOR AH,AH (or an equal compare) in the original, set alike here.
void text_bios_char(u8 c, u8 count, u8 color, u8 row, u8 col) {
    G8(0x4a03) = 2; G8(0x4a05) = G8(0x4a1b); G8(0x4a09) = row; G8(0x4a08) = col;
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));
    G8(0x4a03) = 9; G8(0x4a02) = c; G8(0x4a05) = G8(0x4a1b);
    G8(0x4a04) = G8(0x4a1e) == 0 ? G8(0x25d5 + color) : color;
    wr16(DS_LIN + 0x4a06, count);
    emu_flags_sub16(0, 0);
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));
    if (G8(0x4a1e) != 1) return;
    G8(0x4a03) = 2; G8(0x4a05) = G8(0x4a1a); G8(0x4a09) = row; G8(0x4a08) = col;
    emu_flags_sub16(1, 1);
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));
    G8(0x4a03) = 9; G8(0x4a02) = c; G8(0x4a05) = G8(0x4a1a); G8(0x4a04) = color;
    wr16(DS_LIN + 0x4a06, count);
    emu_flags_sub16(0, 0);
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));
}
void text_bios_string(farptr s, u8 color, u8 row, u8 col) {       // 1802:0105
    u8 b[256]; memcpy(b, &mem[LIN(s)], (size_t)P8(s, 0) + 1);
    for (int i = 1; i <= b[0]; i++) text_bios_char(b[i], 1, color, row, col++);
}
void text_bios_bar(u8 count, u8 color, u8 row, u8 col) { text_bios_char(0xdb, count, color, row, col); }   // 1802:0175
