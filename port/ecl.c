// The adventure script (ECL) interpreter: operand layer (original overlay unit at 2366).
//
// Scripts were written for the game's first version on an 8-bit computer and still address game
// state by that machine's memory addresses. The interpreter maps each address range onto the PC
// program's own structures:
//   4900-4CFF  area and game flags      -> words in the record at DS:49D2
//   6B00-6EFF  party and character data -> words in the record at DS:49D6 (some addresses are
//                                          routed to fields of the selected character instead)
//   9700-98FF  scratch variables        -> words in the record at DS:49DA
//   9900-B6FF  the script itself        -> bytes of the block at DS:49DE
//   anything else: a handful of special addresses (party x, y, facing, ...)
// One old byte address becomes one 16-bit word here.
#include "poolrad.h"
#include "ecl.h"
#include "rtl.h"
#include "dax.h"
#include "text.h"
#include "menu.h"
#include <stdio.h>

#define ECL_IP 0x49ed                                    // script instruction pointer (a script address)
static inline u8 *wrap(farptr base, u16 off) { return &mem[((u32)FP_SEG(base) << 4) + (u16)(FP_OFF(base) + off)]; }   // 16-bit offset arithmetic, as the original
static inline u8 script_byte(u16 addr) { return *wrap(G32(0x49de), (u16)(addr + 0x6700)); }

// 2366:06AF
u16 ecl_word(u8 hi, u8 lo) { return (u16)(hi * 0x100 + lo); }

// 2366:06D3  Script text uses 6-bit codes: 00-1F are '@'..'_' (the capital letters), 20-3F are themselves.
u8 ecl_char(u8 code) { return code < 0x20 ? (u8)(code + 0x40) : code; }

// 2366:06FB  Which address range a script address belongs to (0-3 as listed above, 4 = special).
u8 ecl_addr_class(u16 addr) {
    u8 c = 4;
    if (addr > 0x48ff && addr < 0x4d00) c = 0;
    if (addr > 0x6aff && addr < 0x6f00) c = 1;
    if (addr > 0x96ff && addr < 0x9900) c = 2;
    if (addr > 0x98ff && addr < 0xb700) c = 3;
    return c;
}

// 2366:1153  Unpack `packed` bytes of inline text at the script pointer into string buffer n
// (DS:6FC8 + n*100h). Four 6-bit codes are packed into every three bytes; zero codes are skipped.
void ecl_unpack_text(u8 n, u8 packed) {
    u8 *dst = &G8(0x6fc8 + n * 0x100);
    u8 used = 0, k = 1, cur = 0, prev = 0, c = 0;
    dst[0] = 0;
    while (used < packed) {
        if (k < 4) { SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1)); prev = cur; cur = script_byte(G16(ECL_IP)); used++; }
        if (k == 1) c = cur >> 2;
        else if (k == 2) c = (u8)((u8)(prev * 0x40 + (cur >> 2)) >> 2);
        else if (k == 3) c = (u8)((u8)(prev * 0x10 + (cur >> 4)) >> 2);
        else c = cur & 0x3f;
        k = (k < 4) ? (u8)(k + 1) : 1;
        if (c != 0 && dst[0] < 255) { dst[++dst[0]] = ecl_char(c); }
    }
    if (k == 4) { c = cur & 0x3f; if (c != 0 && dst[0] < 255) dst[++dst[0]] = ecl_char(c); }
}

// 2366:0000  Fetch the n operands of the current instruction into the operand tables
// (type DS:7005+i, low byte DS:7085+i, high byte DS:7045+i; text operands into the string buffers)
// and step the script pointer past the instruction.
void ecl_fetch(u8 n) {
    u8 strings = 0;
    for (u8 i = 1; i <= n && n != 0; i++) {
        u16 ip = G16(ECL_IP);
        G8(0x7005 + i) = script_byte((u16)(ip + 1));
        G8(0x7085 + i) = script_byte((u16)(ip + 2));
        SETG16(ECL_IP, (u16)(ip + 2));
        u8 t = G8(0x7005 + i);
        if (t == 1 || t == 2 || t == 3) {
            SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
            G8(0x7045 + i) = script_byte(G16(ECL_IP));
        } else if (t == 0x80) {
            strings++;
            if (G8(0x7085 + i) == 0) G8(0x6fc8 + strings * 0x100) = 0;
            else ecl_unpack_text(strings, G8(0x7085 + i));
        } else if (t == 0x81) {
            strings++;
            SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
            G8(0x7045 + i) = script_byte(G16(ECL_IP));
            ecl_text_from_var(ecl_word(G8(0x7045 + i), G8(0x7085 + i)), strings);
        }
    }
    SETG16(ECL_IP, (u16)(G16(ECL_IP) + 1));
}

// 2366:0E0C  Read the script variable at `addr`.
u16 ecl_read_var(u16 addr) {
    switch (ecl_addr_class(addr)) {
    case 0: return rd16((u32)(wrap(G32(0x49d2), (u16)(addr * 2 + 0x6e00)) - mem));
    case 1: {
        u8 handled; u16 v = ecl_read_char_field(&handled, addr);
        return handled ? v : rd16((u32)(wrap(G32(0x49d6), (u16)(addr * 2 + 0x2a00)) - mem));
    }
    case 2: return rd16((u32)(wrap(G32(0x49da), (u16)(addr * 2 - 0x2e00)) - mem));
    case 3: return script_byte(addr);
    }
    switch (addr) {
    case 0x00b1: return G16(0x6f6e);
    case 0x00fb: return G16(0x6f6a);
    case 0x00fc: return G16(0x6f6c);
    case 0x033d: return G8(0x6aaf);
    case 0xc04b: return (u16)(s16)(s8)G8(0x6aad);        // party x
    case 0xc04c: return (u16)(s16)(s8)G8(0x6aae);        // party y
    case 0xc04d: return G8(0x6aaf) >> 1;                 // facing 0-3
    case 0xc04e: return G8(0x6ab0);
    case 0xc04f: return G8(0x6ab1);
    }
    return 0;                                            // (the original returns an uninitialised local here)
}

// 2366:0C77  Write `value` to the script variable at `addr`.
void ecl_write_var(u16 value, u16 addr) {
    switch (ecl_addr_class(addr)) {
    case 0:
        wr16((u32)(wrap(G32(0x49d2), (u16)(addr * 2 + 0x6e00)) - mem), value);
        if (addr == 0x49fd || addr == 0x49fe) G8(0x84e8) = 1;
        return;
    case 1:
        wr16((u32)(wrap(G32(0x49d6), (u16)(addr * 2 + 0x2a00)) - mem), value);
        ecl_write_char_field(value, addr);
        return;
    case 2: wr16((u32)(wrap(G32(0x49da), (u16)(addr * 2 - 0x2e00)) - mem), value); return;
    case 3: *wrap(G32(0x49de), (u16)(addr + 0x6700)) = (u8)value; return;
    }
    switch (addr) {
    case 0x03de: SETG16(0x84cc, value); break;
    case 0x00b8: SETG16(0x84ce, value); break;
    case 0x00b9: SETG16(0x84d0, value); break;
    case 0xc04b: G8(0x84e6) = 1; G8(0x6aad) = (u8)value; break;       // moving the party: flag a redraw
    case 0xc04c: G8(0x6aae) = (u8)value; G8(0x84e6) = 1; break;
    case 0xc04d: G8(0x6aaf) = (u8)((value & 3) * 2); G8(0x84e6) = 1; break;
    case 0xc059: G8(0x6f68) = (u8)value; G8(0x84e5) = 1; break;
    case 0xc05f: G8(0x6f69) = (u8)value; G8(0x84e5) = 1; break;
    }
}

// 2366:0136  The value of operand i of the current instruction: a literal, or the variable it names.
u16 ecl_operand(u8 i) {
    u8 t = G8(0x7005 + i), lo = G8(0x7085 + i), hi = G8(0x7045 + i);
    if (t == 0) return lo;
    if (t == 1 || t == 3 || t == 0x80) return ecl_read_var(ecl_word(hi, lo));
    if (t == 2 || t == 0x81) return ecl_word(hi, lo);
    return 0;
}

// ---- Character fields ----
// Script addresses 6B00-6EFF describe "the current character" in the old machine's layout. Most
// are plain words in the DS:49D6 record, but the ones below are redirected to the selected
// character's record (far pointer at DS:5D92), whose layout is the PC version's own. `o` is the
// address minus 6B00h.

// 2366:0756  Position of a character in the party list (head DS:5D96, next pointer at +104h).
static u8 party_index(farptr ch) {
    u8 n = 0;
    for (farptr p = G32(0x5d96); p && p != ch; p = P32(p, 0x104)) n++;
    return n;
}

// 2366:07BC  Read a redirected character field. *handled = 0 if this address is not redirected.
u16 ecl_read_char_field(u8 *handled, u16 addr) {
    farptr ch = G32(0x5d92);
    u16 o = (u16)(addr + 0x9500);
    *handled = 1;
    if (o >= 0xa5 && o <= 0xac) return P8(ch, 0x76 + ((addr + 0x945c) & 0xff));       // eight bytes at +77h
    switch (o) {
    case 0x018: return P8(ch, 0x14);
    case 0x09b: return P8(ch, 0x6e);
    case 0x0a0: return (u16)(s16)(s8)P8(ch, 0x73);
    case 0x0b8: return P8(ch, 0x84);
    case 0x0bb: return P16(ch, 0x88);
    case 0x0bd: return P16(ch, 0x8a);
    case 0x0bf: return P16(ch, 0x8c);
    case 0x0c1: return P16(ch, 0x8e);
    case 0x0c3: return P16(ch, 0x90);
    case 0x0d8: return (u16)(s16)(s8)P8(ch, 0xa0);
    case 0x0f7: return P16(ch, 0xb8);
    case 0x0f9: return P8(ch, 0xba);
    case 0x100: return P8(ch, 0x10d) == 0 ? 0x80 : 1;
    case 0x10c: return (P8(ch, 0x10e) == 0 && P8(ch, 0x10f) != 0) ? 0x80 : P8(ch, 0x10e) == 1 ? 0x81 : 0;
    case 0x10d: return 0;                                // (the original leaves the result unset here)
    case 0x11b: return P8(ch, 0x11c);
    case 0x2b1: case 0x2b4: return party_index(ch);
    case 0x2cf: {                                        // a 3-25 score turned into the old version's 0-60 scale
        u8 v = P8(ch, 0x15);
        static const u8 t[] = {0, 5, 10, 15, 20, 25, 25, 25, 25, 25, 30, 35, 40, 50, 55};   // scores 3..17
        if (v >= 3 && v <= 17) return t[v - 3];
        if (v >= 18 && v <= 25) return 60;
        return 0;                                        // (unset in the original)
    }
    case 0x312: return G8(0x5376);                       // which numbered data-file set is in use
    case 0x33e: return P16(G32(0x49d6), 0x67c);
    }
    *handled = 0;
    return 0;
}

// 2366:0A74  Carry a write to a 6B00-6EFF address over to the character record where needed.
void ecl_write_char_field(u16 value, u16 addr) {
    farptr ch = G32(0x5d92);
    u16 o = (u16)(addr + 0x9500);
    if (addr == 0x6b00) { if (value == 0) G8(0x84d3) = 1; return; }
    if (o >= 0x20 && o <= 0x70) { P8(ch, 0x17 + ((addr + 0x94e1) & 0xff)) = (u8)value; return; }   // 81 bytes at +18h
    switch (o) {
    case 0x0b8: if (value > 0xb2) value = (u16)(value - 0x32); P8(ch, 0x84) = (u8)value; break;
    case 0x0bb: SETP16(ch, 0x88, value); break;
    case 0x0bd: SETP16(ch, 0x8a, value); break;
    case 0x0bf: SETP16(ch, 0x8c, value); break;
    case 0x0c1: SETP16(ch, 0x8e, value); break;
    case 0x0c3: SETP16(ch, 0x90, value); break;
    case 0x0f7: SETP16(ch, 0xb8, value); break;
    case 0x0f9: P8(ch, 0xba) = (u8)value; break;
    case 0x100:
        if (value > 0x7f) { P8(ch, 0x10d) = 0; if (value == 0x87) P8(ch, 0x10c) = 7; }
        if (value == 0) G8(0x84d2) = 1;
        break;
    case 0x10c:
        if (value == 0) { P8(ch, 0x10e) = 0; P8(ch, 0x10f) = 0; }
        else if (value == 0x80) { P8(ch, 0x10e) = 0; P8(ch, 0x10f) = 1; }
        else if (value == 0x81) { P8(ch, 0x10e) = 1; P8(ch, 0x10f) = 1; }
        break;
    case 0x312: game_set_file_set((u8)value); break;
    case 0x322: if (value > 0x80) map_load_part(1, (u8)(value & 0x7f)); break;
    case 0x324: if (value > 0x80) map_load_part(2, (u8)(value & 0x7f)); break;
    case 0x326: if (value > 0x80) map_load_part(3, (u8)(value & 0x7f)); break;
    }
}

// ---- Text variables ----
// A text held in script variables is one character per variable (so one per 16-bit word here, or
// per byte inside the script block), ending with a zero. Address 6B00 as text is special: it is
// the selected character's name.
static u8 *var_cell(u8 cls, u16 addr) {                  // where the variable at `addr` lives (classes 0-3)
    switch (cls) {
    case 0: return wrap(G32(0x49d2), (u16)(addr * 2 + 0x6e00));
    case 1: return wrap(G32(0x49d6), (u16)(addr * 2 + 0x2a00));
    case 2: return wrap(G32(0x49da), (u16)(addr * 2 - 0x2e00));
    default: return wrap(G32(0x49de), (u16)(addr + 0x6700));
    }
}

// 2366:12FB  Copy the text variable at `addr` into string buffer n (DS:6FC8 + n*100h).
void ecl_text_from_var(u16 addr, u8 n) {
    u8 cls = ecl_addr_class(addr);
    u8 *dst = &G8(0x6fc8 + n * 0x100);
    dst[0] = 0;
    if (cls == 4) return;
    if (cls == 1 && addr == 0x6b00) { farptr ch = G32(0x5d92); memmove(dst, &mem[LIN(ch)], P8(ch, 0) + 1); return; }
    for (u8 i = 0; ; i++) {
        u8 *cell = var_cell(cls, (u16)(addr + i));
        if (cls == 3 ? cell[0] == 0 : (cell[0] | cell[1]) == 0) break;
        if (dst[0] < 255) dst[++dst[0]] = cell[0];
    }
}

// 2366:0F53  Store a Pascal string into the text variable at `addr`. (An empty string writes 256
// cells before the terminator, because the original's loop counts to length - 1 in a byte.)
void ecl_store_text(const u8 *str, u16 addr) {
    u8 s[256]; memcpy(s, str, 256);
    u8 cls = ecl_addr_class(addr), len = s[0];
    if (cls == 4) return;
    if (cls == 1 && addr == 0x6b00) {
        farptr ch = G32(0x5d92); u8 n = len > 15 ? 15 : len;
        P8(ch, 0) = n; memcpy(&mem[LIN(ch) + 1], s + 1, n);
        return;
    }
    for (u8 i = 0; ; i++) {
        u8 *cell = var_cell(cls, (u16)(addr + i));
        cell[0] = s[(u8)(1 + i)]; if (cls != 3) cell[1] = 0;
        if (i == (u8)(len - 1)) break;
    }
    u8 *end = var_cell(cls, (u16)(addr + len));
    end[0] = 0; if (cls != 3) end[1] = 0;
}

// ---- Loading a script ----
// 2366:0341  Load script block `id` from ECL<n>.dax into the 7,680-byte script buffer (the old
// machine's 9900-B6FF). The block's first two bytes are a header and are not copied.
void ecl_load_block(u8 id) {
    char name[16]; snprintf(name, sizeof name, "ECL%u", G8(0x5376)); name[4] = 0;
    farptr buf; u16 size;
    char path[32]; snprintf(path, sizeof path, "%s.dax", name);
    FILE *f = plat_fopen(path, "rb");
    if (!f) { call_original(0x2366, 0x0341, 1, id); return; }          // archive missing: the original asks for the disk
    fclose(f);
    memset(&mem[LIN(G32(0x49de))], 0, 0x1e00);
    G8(0x557c) = 0;
    do {
        menu_clear_line();
        text_put_string(FARPTR(RSEG(0x2366), 0x0322), 15, 24, 0);      // "Loading...Please Wait"
        dax_load_native(&buf, &size, id, ".dax", name);
    } while (size < 2);
    memmove(&mem[LIN(G32(0x49de))], &mem[LIN(buf) + 2], (u16)(size - 2));
    rtl_freemem(buf, size);
    menu_clear_line();
}

// 2366:01B6  Get ready to run a freshly loaded script: reset the interpreter's state and read the
// five entry points from the GOTO table at the start of the block into DS:49E3-49EB.
void ecl_init_script(void) {
    G8(0x84e0) = 0; G8(0x84e2) = 0; G8(0x84d2) = 0; G8(0x84d3) = 0;
    G8(0x6f68) = 0x41; G8(0x6f69) = 9; G8(0x84e5) = 1;
    SETG16(0x84c8, 0);
    G8(0x6f83) = 8;
    SETG16(ECL_IP, 0x9900);
    G8(0x70c6) = 0;
    SETG32(0x6f70, 0); SETG32(0x6f74, 0);
    memset(&G8(0x6f78), 0, 6);
    SETP16(G32(0x49d6), 0x5c2, 0xff); SETP16(G32(0x49d6), 0x5a4, 0); SETP16(G32(0x49d6), 0x5a6, 0);
    SETP16(G32(0x49d2), 0x1ca, 0);
    for (int i = 0; i < 5; i++) { ecl_fetch(1); SETG16(0x49e3 + i * 2, ecl_word(G8(0x7046), G8(0x7086))); }
    SETP16(G32(0x49d2), 0x1cc, 1);
    if (G8(0x49f8) == 0) {                               // not resuming a saved game: clear the area's working variables
        for (u16 i = 1; i <= 0x20; i++) { u8 *c = var_cell(0, (u16)(0x49ff + i)); c[0] = c[1] = 0; }
        for (u16 i = 0; i <= 9; i++) { u8 *c = var_cell(1, (u16)(0x6e79 + i)); c[0] = c[1] = 0; }
    } else G8(0x49f8) = 0;
}
