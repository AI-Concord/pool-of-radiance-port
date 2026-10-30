// Low-level picture routines (original unit at 1464, resident). EGA paths only: the original also
// carries CGA and Tandy variants selected by the display type in POOL.CFG.
//
// A picture ("pic") is a heap record:
//   +00 word  height in pixels          +08 byte  number of frames
//   +02 word  width in 8-pixel columns  +09 8 bytes colour remap table (CGA)
//   +04 word  x position                +11 word  bytes per frame (height * width * 4 on EGA)
//   +06 word  y position                +13 far   transparency mask, or nil
//   +17 pixel data: for each 8-pixel cell four bytes, one per EGA bit plane
// The mask holds one byte per cell with a 1 bit wherever the picture is transparent.
#include "poolrad.h"
#include "ega.h"
#include "gfx.h"
#include "rtl.h"
#include "dax.h"

// 1464:0000  Which of the 8 pixels packed in four bytes (two 4-bit pixels each) have this colour?
u8 gfx_color_mask(const u8 px[4], u8 color) {
    u8 m = 0, bit = 0x80;
    for (int i = 0; i < 4; i++) {
        if ((px[i] & 0xf0) == (u8)(color << 4)) m += bit;
        bit >>= 1;
        if ((px[i] & 0x0f) == color) m += bit;
        bit >>= 1;
    }
    return m;
}

// 1464:007B  Entry n of a table of sixteen 4-bit values packed two to a byte.
u8 gfx_nibble(farptr table, u8 n) {
    u8 b = P8(table, n >> 1);
    return (n & 1) ? (b & 0x0f) : (b >> 4);
}

// 1464:1951  Draw a picture's first frame on the screen at text row `row`, byte column `col`. No clipping.
void gfx_draw_pic(farptr pic, s16 row, s16 col) {
    if (!pic) return;
    u16 idx = 0;
    s16 y0 = (s16)(row * 8), y1 = (s16)(y0 + P16(pic, 0) - 1), x1 = (s16)(col + P16(pic, 2) - 1);
    ega_out(0x3c4, 2);
    for (s16 y = y0; y <= y1; y++)
        for (s16 x = col; x <= x1; x++)
            for (int plane = 0; plane < 4; plane++) {
                ega_out(0x3c5, G8(0x25f6 + plane));
                ega_write((u16)(G16(0x5050 + y * 2) + x), P8(pic, 0x17 + idx));
                idx++;
            }
    ega_out(0x3c5, 0x0f);
}

// 1464:08D4  Copy frame `frame` of `src` into the picture `dst` at (row, col), honouring the source's
// transparency mask when `flags` bit 0 is set. Records the touched span of each row in the dirty-row
// tables so the screen update can copy only what changed.
void gfx_merge_pic(farptr dst, farptr src, u8 flags, u8 frame, s16 row, s16 col) {
    int masked = (flags & 1) && P32(src, 0x13) != 0;
    u16 y = (u16)(row * 8);
    s16 rows_m1 = (s16)(P16(src, 0) - 1);
    u16 skip = (u16)(P16(dst, 2) - (col + P16(src, 2)));
    u16 n = (u16)(P16(src, 2) * 4);
    u16 s = (u16)(frame * P16(src, 0x11));
    u16 m = s >> 2; u8 mcount = 4;
    u16 d = (u16)(y * P16(dst, 2) * 4);
    farptr mask = P32(src, 0x13);
    for (s16 r = 0; ; r++) {
        G8(0x4a28 + y) = 1;
        d = (u16)(d + col * 4);
        if (d < G16(0x4c20 + y * 2)) { SETG16(0x4ad0 + y * 2, (u16)col); SETG16(0x4c20 + y * 2, d); }
        if (masked) {
            if ((s8)n > 0) {
                for (u8 i = 1; ; i++) {
                    G8(0x70c7) = i;                      // the original's loop counter is a global
                    P8(dst, 0x17 + d) = (P8(dst, 0x17 + d) & P8(mask, m)) | P8(src, 0x17 + s);
                    s++;
                    if (--mcount == 0) { mcount = 4; m++; }
                    d++;
                    if (i == (u8)n) break;
                }
            }
        } else {
            memmove(&mem[LIN(dst) + 0x17 + d], &mem[LIN(src) + 0x17 + s], n);
            s = (u16)(s + n); d = (u16)(d + n);
        }
        if (G16(0x4d70 + y * 2) < (u16)(d - 1)) SETG16(0x4d70 + y * 2, (u16)(d - 1));
        d = (u16)(d + skip * 4);
        y++;
        if (r == rows_m1) break;
    }
}

// 1464:1484  Reverse the bit order of a byte (mirror eight pixels of one plane).
u8 gfx_reverse_bits(u8 b) {
    b = (u8)((b >> 4) | (b << 4)); b = (u8)(((b & 0xcc) >> 2) | ((b & 0x33) << 2));
    return (u8)(((b & 0xaa) >> 1) | ((b & 0x55) << 1));
}

// 1464:15C7  Make `dst` the left-right mirror image of `src` (same size; used for combat figures
// facing the other way).
void gfx_flip_pic(farptr dst, farptr src) {
    if (!src || !dst) return;
    memmove(&mem[LIN(dst) + 9], &mem[LIN(src) + 9], 8);
    u16 h = P16(src, 0), w = P16(src, 2), rowbytes = (u16)(w * 4), last = (u16)(rowbytes - 4), d = 0;
    for (u16 r = 1; h != 0; r++) {
        u16 rowstart = d;
        for (u16 c = 1; w != 0; c++) {
            for (u8 f = 0; f < 4; f++) P8(dst, 0x17 + (u16)(d + f)) = gfx_reverse_bits(P8(src, 0x17 + (u16)(last - d + rowstart + f)));
            d = (u16)(d + 4);
            if (c == w) break;
        }
        last = (u16)(last + rowbytes);
        if (r == h) break;
    }
    farptr sm = P32(src, 0x13), dm = P32(dst, 0x13);
    if (sm) {
        u16 m = 0;
        for (u16 r = 1; h != 0; r++) {
            for (u16 c = 0; ; c++) {
                P8(dm, (u16)(m + w - c - 1)) = gfx_reverse_bits(P8(sm, (u16)(m + c)));
                if (c == (u16)(w - 1)) break;
            }
            m = (u16)(m + w);
            if (r == h) break;
        }
    }
}

// 1464:1B52  Copy the changed rows of the back buffer (the picture at DS:4A20, 168 rows tall, shown
// 8 pixels down and one column in) to the screen. Each changed span is first written plane by plane
// to the hidden page at A200h, then moved to the visible page with the EGA's latch copy so the
// update appears at once.
void gfx_flush_back_buffer(void) {
    farptr back = G32(0x4a20);
    ega_out(0x3c4, 2);
    for (u16 y = 0; y <= 0xa7; y++) {
        if (!G8(0x4a28 + y)) continue;
        u16 base = (u16)(G16(0x5050 + (y + 8) * 2) + G16(0x4ad0 + y * 2) + 1);
        for (int plane = 0; plane < 4; plane++) {
            ega_out(0x3c5, G8(0x25f6 + plane));
            u16 s = (u16)(G16(0x4c20 + y * 2) + plane), p = base;
            do { ega_write((u16)(0x2000 + p), P8(back, 0x17 + s)); s = (u16)(s + 4); p++; } while (s <= G16(0x4d70 + y * 2));
        }
    }
    ega_out(0x3c5, 0x0f);
    u16 p = (u16)(G16(0x5060) + 1);
    ega_out(0x3ce, 5); ega_out(0x3cf, 1);                 // write mode 1: copy through the latches
    for (u16 y = 0; y <= 0xa7; y++) {
        if (G8(0x4a28 + y)) {
            u16 a = (u16)(p + G16(0x4ad0 + y * 2)), n = (u16)(((u16)(G16(0x4d70 + y * 2) - G16(0x4c20 + y * 2)) >> 2) + 1);
            for (u16 i = 0; i < n; i++) { u8 v = ega_read((u16)(0x2000 + a + i)); ega_write((u16)(a + i), v); }
        }
        p = (u16)(p + 40);
    }
    ega_out(0x3cf, 0);
    memset(&G8(0x4a28), 0, 0xa8);            // nothing is dirty any more
    memset(&G8(0x4c20), 0xff, 0x150);        // first changed byte per row: none
    memset(&G8(0x4d70), 0, 0x150);           // last changed byte per row: none
}

// 1464:2632  Fill a rectangle of the picture `pic` with a colour, clipped to the picture. `col` and
// `w` are in 8-pixel columns, `row` and `h` in pixels.
void gfx_fill_pic(farptr pic, u8 color, u16 h, u16 w, s16 row, s16 col) {
    if (!pic) return;
    u16 left, x, right, trail, y, d, last;
    if (col < 0) { left = (u16)-col; x = 0; } else { left = 0; x = (u16)col; }
    if ((s32)col + w <= (s32)P16(pic, 2)) { right = 0; trail = (u16)(P16(pic, 2) - (col + w)); }
    else { right = (u16)(col + w - P16(pic, 2)); trail = 0; }
    if (row < 0) { d = 0; y = 0; last = (u16)(h - (u16)-row); }      // one row more than fits; as in the original
    else {
        y = (u16)row; d = (u16)(row * P16(pic, 2));
        last = ((s32)row + h <= (s32)P16(pic, 0)) ? (u16)(h - 1) : (u16)(P16(pic, 0) - 1 - row);
    }
    u16 n = (u16)(w - left - right), x4 = (u16)(x << 2);
    trail = (u16)(trail << 2); d = (u16)(d << 2);
    u8 pat[4];
    for (int plane = 0; plane < 4; plane++) pat[plane] = (G8(0x25f6 + plane) & color) ? 0xff : 0;
    for (u16 r = 0; ; r++) {
        G8(0x4a28 + y) = 1;
        d = (u16)(d + x4);
        if (d < G16(0x4c20 + y * 2)) { SETG16(0x4ad0 + y * 2, x); SETG16(0x4c20 + y * 2, d); }
        if ((s8)n > 0)
            for (u8 i = 1; ; i++) {
                G8(0x70c7) = i;
                memcpy(&mem[LIN(pic) + 0x17 + d], pat, 4);
                d = (u16)(d + 4);
                if (i == (u8)n) break;
            }
        if (G16(0x4d70 + y * 2) < (u16)(d - 1)) SETG16(0x4d70 + y * 2, (u16)(d - 1));
        d = (u16)(d + trail);
        y++;
        if (r == last) break;
    }
}

// 1464:248E  Allocate a zeroed picture of `frames` frames, `w` columns by `h` rows, and store the
// pointer in *var. Bit 0 of `flags` also allocates the transparency mask (one byte per cell).
void gfx_alloc_pic(farptr var, u8 flags, u8 frames, u16 w, u16 h) { SETP32(var, 0, gfx_new_pic(flags, frames, w, h)); }

farptr gfx_new_pic(u8 flags, u8 frames, u16 w, u16 h) {
    u16 framesize = (u16)(h * w * 4);
    u16 total = (u16)(frames * framesize);
    farptr p = rtl_getmem((u16)(total + 0x17));
    memset(&mem[LIN(p)], 0, (u16)(total + 0x17));
    SETP16(p, 0, h); SETP16(p, 2, w); SETP16(p, 0x11, framesize); P8(p, 8) = frames;
    if (flags & 1) {
        farptr m = rtl_getmem(total >> 2);
        SETP32(p, 0x13, m);
        memset(&mem[LIN(m)], 0, total >> 2);
    }
    return p;
}

// 1464:2595  Free the picture *var (and its mask) and set *var to nil.
void gfx_free_pic(farptr var) {
    if (!P32(var, 0)) return;
    gfx_dispose_pic(P32(var, 0));
    SETP32(var, 0, 0);
}

void gfx_dispose_pic(farptr p) {
    u16 total = (u16)(P8(p, 8) * P16(p, 0x11));
    if (P32(p, 0x13)) rtl_freemem(P32(p, 0x13), total >> 2);
    rtl_freemem(p, (u16)(total + 0x17));
}

// 1464:02A5  Convert packed pixels (4 bits each, two to a byte, starting at src+srcoff) into the
// picture's bit-plane form, for every frame. If the picture has a mask, pixels of colour
// `transparent` are recorded in it and cleared from the planes.
void gfx_decode_pic(farptr pic, u8 transparent, u16 srcoff, farptr src) {
    if (!pic) return;
    u16 out = 0, m = 0;
    u8 frames = P8(pic, 8);
    farptr mask = P32(pic, 0x13);
    for (u8 f = 1; frames != 0; f++) {
        u16 h = P16(pic, 0), w = P16(pic, 2);
        for (u16 r = 0; r < h; r++)
            for (u16 c = 0; c < w; c++) {
                u8 px[4]; memcpy(px, &mem[LIN(src) + (u16)srcoff], 4);
                if (mask) { P8(mask, m) = gfx_color_mask(px, transparent); m++; }
                u8 bit = 0x80;
                for (u8 n = 0; n < 8; n++) {
                    u8 v = (n & 1) ? (px[n >> 1] & 0x0f) : (px[n >> 1] >> 4);
                    for (u8 pl = 0; pl < 4; pl++) if (G8(0x25f6 + pl) & v) P8(pic, 0x17 + (u16)(out + pl)) |= bit;
                    bit >>= 1;
                }
                if (mask) for (u8 pl = 0; pl < 4; pl++) P8(pic, 0x17 + (u16)(out + pl)) &= (u8)~P8(mask, (u16)(m - 1));
                out = (u16)(out + 4); srcoff = (u16)(srcoff + 4);
            }
        if (f == frames) break;
    }
}

// 1464:0111  Load picture block `id` from <name>.dax into *var (freeing whatever *var held).
// Returns 0 without doing anything if the archive cannot be opened.
int gfx_load_pic(farptr var, u8 transparent, u8 flags, u8 id, const char *name) {
    char path[160]; snprintf(path, sizeof path, "%s.dax", name);
    FILE *f = plat_fopen(path, "rb"); if (!f) return 0;
    fclose(f);
    if (P32(var, 0)) gfx_free_pic(var);
    farptr pic = 0;
    if (!gfx_load_pic_native(&pic, transparent, flags, id, name)) return 0;
    if (pic) SETP32(var, 0, pic);
    return 1;
}

// The same without a Pascal VAR parameter: *out is the new picture, or 0 if the block is not in the archive.
int gfx_load_pic_native(farptr *out, u8 transparent, u8 flags, u8 id, const char *name) {
    farptr buf; u16 size;
    *out = 0;
    if (!dax_load_native(&buf, &size, id, ".dax", name)) return 0;
    if (size != 0) {
        u16 h = P16(buf, 0), w = P16(buf, 2), x = P16(buf, 4), y = P16(buf, 6); u8 frames = P8(buf, 8);
        farptr pic = gfx_new_pic(flags, frames, w, h);
        SETP16(pic, 4, x); SETP16(pic, 6, y);
        memmove(&mem[LIN(pic) + 9], &mem[LIN(buf) + 9], 8);
        gfx_decode_pic(pic, transparent, 0x11, buf);
        rtl_freemem(buf, size);
        *out = pic;
    }
    return 1;
}

// 1464:0D71  Like gfx_merge_pic, but clipped to the destination when the clipping switch (DS:5370)
// is on, so a picture may hang off any edge. Flag bit 2 first copies the destination pixels about
// to be covered into the save-under picture at DS:4A24, from where they can be restored later.
void gfx_merge_pic_clipped(farptr dst, farptr src, u8 flags, u8 frame, s16 row, s16 col) {
    if (!src || !dst) return;
    int masked = (flags & 1) && P32(src, 0x13) != 0, save = (flags & 4) != 0;
    u16 dw = P16(dst, 2), dh = P16(dst, 0), sw = P16(src, 2), sh = P16(src, 0);
    u16 left, right, x, trail, sy, y, d, last;
    if (!G8(0x5370)) {
        left = 0; right = 0; sy = 0; y = (u16)(row * 8); d = (u16)(y * dw); last = (u16)(sh - 1);
        x = (u16)col; trail = (u16)(dw - (col + sw));
    } else {
        if (col < 0) { left = (u16)-col; if (sw < left) return; x = 0; }
        else { left = 0; x = (u16)col; if (dw < (u16)col) return; }
        if ((s32)col + sw <= (s32)dw) { right = 0; trail = (u16)(dw - (col + sw)); if (dw < trail) return; }
        else { right = (u16)(col + sw - dw); if (sw < right) return; trail = 0; }
        if (row < 0) {
            sy = (u16)((u16)-row * sw * 8);
            d = 0; y = 0; last = (u16)((sh - 1) - (u16)-row * 8);
            if ((u16)(P16(src, 0x11) - 1) < sy) return;
            if ((u16)(sh - 1) < last) return;
        } else {
            sy = 0; y = (u16)(row * 8); d = (u16)(y * dw);
            last = ((s32)(s16)y + sh <= (s32)dh) ? (u16)(sh - 1) : (u16)((dh - 1) - row * 8);
            if ((u16)(P16(dst, 0x11) - 1) < d) return;
            if ((u16)(sh - 1) < last) return;
        }
    }
    u16 n = (u16)((sw - left - right) * 4);
    u16 u = (u16)(sy * 4);                              // offset in the save-under picture
    u16 s = (u16)(frame * P16(src, 0x11) + u);
    u16 m = s >> 2; u8 mcount = 4;
    farptr mask = P32(src, 0x13), under = G32(0x4a24);
    d = (u16)(d << 2);
    for (u16 r = 0; ; r++) {
        G8(0x4a28 + y) = 1;
        s = (u16)(s + left * 4); m = (u16)(m + (left & 0x3fff)); u = (u16)(u + left * 4); d = (u16)(d + x * 4);
        if (d < G16(0x4c20 + y * 2)) { SETG16(0x4ad0 + y * 2, x); SETG16(0x4c20 + y * 2, d); }
        if (save) memmove(&mem[LIN(under) + 0x17 + u], &mem[LIN(dst) + 0x17 + d], n);
        if (masked) {
            if ((s8)n > 0)
                for (u8 i = 1; ; i++) {
                    G8(0x70c7) = i;
                    P8(dst, 0x17 + d) = (P8(dst, 0x17 + d) & P8(mask, m)) | P8(src, 0x17 + s);
                    s++;
                    if (--mcount == 0) { mcount = 4; m++; }
                    d++;
                    if (i == (u8)n) break;
                }
        } else {
            memmove(&mem[LIN(dst) + 0x17 + d], &mem[LIN(src) + 0x17 + s], n);
            s = (u16)(s + n); d = (u16)(d + n);
        }
        if (G16(0x4d70 + y * 2) < (u16)(d - 1)) SETG16(0x4d70 + y * 2, (u16)(d - 1));
        s = (u16)(s + right * 4); m = (u16)(m + (right & 0x3fff)); u = (u16)(u + n + right * 4); d = (u16)(d + trail * 4);
        y++;
        if (r == last) break;
    }
}

// 1464:1E5C  Recolour a picture in place: every pixel of colour old[k] becomes new[k], for the 16
// table entries in turn. This is how one set of combat figure drawings serves every character:
// the tables map the stock colours of hair, armour and so on to the ones chosen for a character.
// With `dither` set only a random scattering of the matching pixels changes. `frame` < 0 was
// meant to do all frames; the original's loop only ever recolours the first, and so does this.
void gfx_recolor_pic(farptr pic, u8 dither, s16 frame, farptr new_tab, farptr old_tab) {
    u8 old[16], nw[16]; memcpy(old, &mem[LIN(old_tab)], 16); memcpy(nw, &mem[LIN(new_tab)], 16);
    if (!pic) return;
    u8 nframes;
    if (frame < 0) { frame = 0; nframes = P8(pic, 8); } else nframes = 1;
    u16 fsize = P16(pic, 0x11), src0 = (u16)(frame * fsize);
    farptr tmp = gfx_new_pic(0, nframes, P16(pic, 2), P16(pic, 0));
    memmove(&mem[LIN(tmp) + 9], &mem[LIN(pic) + 9], 8);
    memmove(&mem[LIN(tmp) + 0x17], &mem[LIN(pic) + 0x17 + src0], (u16)(P8(tmp, 8) * P16(tmp, 0x11)));
    nframes = 1;                                   // see above
    for (int k = 0; k < 16; k++) {
        if (old[k] == nw[k]) continue;
        u16 s = src0, d = 0;
        u8 xr[4];
        for (int pl = 0; pl < 4; pl++) xr[pl] = (old[k] & G8(0x25f6 + pl)) ? 0 : 0xff;
        s8 fend = (s8)(nframes + frame - 1);
        if ((s8)frame > fend) continue;
        for (s8 f = (s8)frame; ; f++) {
            G8(0x70c7) = (u8)f;
            u16 h = P16(tmp, 0), w = P16(tmp, 2);
            for (u16 r = 0; r < h; r++)
                for (u16 c = 0; c < w; c++) {
                    u8 mk = 0xff;
                    if (dither) { u8 a = (u8)rtl_random(0x100), b = (u8)rtl_random(0x100); mk = b & a; }
                    for (int pl = 0; pl < 4; pl++) mk &= (u8)(P8(pic, 0x17 + (u16)(s + pl)) ^ xr[pl]);   // pixels of colour old[k]
                    for (int pl = 0; pl < 4; pl++) {
                        if (nw[k] & G8(0x25f6 + pl)) P8(tmp, 0x17 + (u16)(d + pl)) |= mk;
                        else P8(tmp, 0x17 + (u16)(d + pl)) &= (u8)~mk;
                    }
                    s = (u16)(s + 4); d = (u16)(d + 4);
                }
            if (f == fend) break;
        }
    }
    memmove(&mem[LIN(pic) + 0x17 + src0], &mem[LIN(tmp) + 0x17], (u16)(nframes * fsize));
    gfx_dispose_pic(tmp);
}

// 1464:00CF  Set a BIOS video mode (Intr 10h, AH 0), remembering the mode before it in DS:4A1C and the new one in 4A1D.
// The flags go to the BIOS call and are saved with its registers (DS:4A14). In the original they come from the
// routine's own SUB SP,2, so they depend on the stack pointer: `entry_sp` is SP on entry (at the return address).
void gfx_set_mode_at(u8 mode, u16 entry_sp) {
    G8(0x4a1c) = G8(0x4a1d);
    G8(0x4a03) = 0; G8(0x4a02) = mode;
    emu_flags_sub16((u16)(entry_sp - 2), 2);
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));
    G8(0x4a1d) = mode;
}
// 1464:00FF  Back to the mode before (DS:4A1C). It calls 1464:00CF with 8 bytes more on the stack.
void gfx_restore_mode_at(u16 entry_sp) { gfx_set_mode_at(G8(0x4a1c), (u16)(entry_sp - 8)); }
// 1464:1509  Reverse the order of the eight 2-bit fields of a word: each field is picked out with its mask from
// DS:25E5..25F3 (DS:25E5 the top field) and moved to the mirrored place.
u16 gfx_reverse_pairs(u16 v) {
    return (u16)(((v & G16(0x25f3)) << 14) + ((v & G16(0x25f1)) << 10) + ((v & G16(0x25ef)) << 6) + ((v & G16(0x25ed)) << 2)
               + ((v & G16(0x25eb)) >> 2) + ((v & G16(0x25e9)) >> 6) + ((v & G16(0x25e7)) >> 10) + ((v & G16(0x25e5)) >> 14));
}
// 1464:1596  Swap the two halves of a byte.
u8 gfx_swap_nibbles(u8 b) { return (u8)((b << 4) + (b >> 4)); }

// 1464:2A0D  One EGA palette register by the video BIOS (function 10h, subfunction 0): register reg (8 and up are
// shifted by 8, to 16 and up) gets value. Through Intr with the shared register record at DS:4A02.
void gfx_palette_reg(u8 value, u8 reg) {
    G8(0x4a03) = 0x10; G8(0x4a02) = 0;
    G8(0x4a05) = reg < 8 ? reg : (u8)(reg + 8);
    G8(0x4a04) = value;
    if (reg < 8) emu_flags_cmp8(reg, 8); else emu_flags_add16(reg, 8);     // Intr keeps the caller's flags (DS:4A14)
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));
}
