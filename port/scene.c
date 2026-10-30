// Scene pictures (original overlay unit at 489F): the animated pictures shown in the 3D window
// and the character portraits.
//
// An animation record is 66 bytes: byte 0 is the number of frames, byte 1 the current frame, and
// from offset 2 one 8-byte entry per frame: the frame's delay (dword) and a far pointer to its picture.
#include <stdio.h>
#include "poolrad.h"
#include "view.h"
#include "hud.h"
#include "text.h"
#include "gfx.h"
#include "dax.h"
#include "rtl.h"
#include "scene.h"

// 489F:0000  Draw a picture at text row/column (row, col). Normally straight to the screen; with
// `through_buffer` set it is merged into the 3D window's back buffer instead and the buffer flushed.
void scene_draw(farptr pic, u8 through_buffer, s16 row, s16 col) {
    if (!pic) return;
    if (!through_buffer) gfx_draw_pic(pic, row, col);
    else { gfx_merge_pic_clipped(G32(0x4a20), pic, 1, 0, (s16)(row - 1), (s16)(col - 1)); gfx_flush_back_buffer(); }
}

// 489F:0697  Draw the current portrait: the head picture (DS:6B01) and, when `with_body` is set,
// the body picture (DS:6B06) five rows below it.
void scene_draw_portrait(u8 with_body, u8 row, u8 col) {
    scene_draw(G32(0x6b01), 0, row, col);
    if (with_body) scene_draw(G32(0x6b06), 0, (s16)(u8)(row + 5), col);
}

// 489F:048F  Free every frame of an animation record and forget which animation is loaded.
void scene_free_anim(farptr rec) {
    s8 n = (s8)P8(rec, 0);
    for (s8 i = 1; i <= n; i++) { G8(0x70c7) = (u8)i; gfx_free_pic(rec + i * 8 - 2); }
    memset(&mem[LIN(rec)], 0, 0x42);
    G8(0x6b0a) = 0; G8(0x6b1c) = 0xff;
}

// 489F:00A2  Load animation block `id` from <name><n>.dax (n is the current data-file number,
// DS:5376) into `rec`, unless that animation is already loaded (name cached at DS:6B0A, id at
// DS:6B1C). Block layout: frame count, then per frame a delay (dword), height, width, x, y, one
// spare byte, 8 bytes, and the pixels. In the PIC and FINAL archives every frame after the first
// is stored XORed with the first frame's pixels.
// Returns 0 without doing anything if the archive cannot be opened.
int scene_load_anim(farptr rec, u8 flags, u8 id, const char *name) {
    char cached[16], file[24]; u8 n = P8(FARPTR(DATA_SEG, 0x6b0a), 0);
    memcpy(cached, &G8(0x6b0b), n > 8 ? 8 : n); cached[n > 8 ? 8 : n] = 0;
    if (!strcmp(name, cached) && id == G8(0x6b1c)) return 1;
    char digits[8]; snprintf(digits, sizeof digits, "%u", G8(0x5376));
    snprintf(file, sizeof file, "%.8s%c", name, digits[0]);
    char path[40]; snprintf(path, sizeof path, "%s.dax", file);
    FILE *f = plat_fopen(path, "rb"); if (!f) return 0;
    fclose(f);

    scene_free_anim(rec);
    G8(0x6b0a) = (u8)strlen(name); memcpy(&G8(0x6b0b), name, strlen(name));
    G8(0x6b1c) = id;
    int delta = !strcmp(name, "PIC") || !strcmp(name, "FINAL");
    farptr buf; u16 size;
    if (!dax_load_native(&buf, &size, id, ".dax", file) || size == 0) return 1;
    u16 p = 0; int saved_first = 0; farptr saved = 0; u16 last = 0; u8 made = 0;
    P8(rec, 0) = P8(buf, 0);
    if (!strcmp(name, "PIC") && id == 1 && P8(rec, 0) > 4) P8(rec, 0) = 4;
    p++;
    P8(rec, 1) = 1;
    u8 frames = P8(rec, 0);
    for (u8 i = 1; i <= frames; i++) {
        memmove(&mem[LIN(rec) + i * 8 - 6], &mem[LIN(buf) + p], 4); p = (u16)(p + 4);
        u16 h = P16(buf, p); p = (u16)(p + 2);
        u16 w = P16(buf, p); p = (u16)(p + 2);
        if (rtl_maxavail() <= 0xf3b) continue;        // not enough memory for another frame
        made++;
        farptr var = rec + i * 8 - 2;
        gfx_alloc_pic(var, flags, 1, w, h);
        farptr pic = P32(var, 0);
        SETP16(pic, 4, P16(buf, p)); p = (u16)(p + 2);
        SETP16(pic, 6, P16(buf, p)); p = (u16)(p + 3);
        memmove(&mem[LIN(pic) + 9], &mem[LIN(buf) + p], 8); p = (u16)(p + 8);
        last = (u16)(P16(pic, 0x11) - 1);
        if (delta) {
            if (i == 1) { saved = rtl_getmem((u16)(last + 1)); memmove(&mem[LIN(saved)], &mem[LIN(buf) + p], (u16)(last + 1)); saved_first = 1; }
            else for (u16 j = 0; j <= last; j++) P8(buf, (u16)(p + j)) ^= P8(saved, j);
        }
        gfx_decode_pic(pic, 0, p, buf);
        if (flags & 1) gfx_recolor_pic(pic, 0, 0, FARPTR(DATA_SEG, 0x0c7c), FARPTR(DATA_SEG, 0x0c6c));
        p = (u16)(p + last + 1);
    }
    P8(rec, 0) = made;
    if (delta && saved_first) rtl_freemem(saved, (u16)(last + 1));
    rtl_freemem(buf, size);
    return 1;
}

// 489F:05D9  Load the portrait pictures: head `head` from HEAD<n>.dax into DS:6B01 and body `body`
// from BODY<n>.dax into DS:6B06, unless the one asked for is already loaded (numbers remembered in
// DS:6B00 and DS:6B05). 255 means "leave this half alone".
void scene_load_portrait(u8 body, u8 head) {
    char name[16];
    if (head != 0xff && (G8(0x6b00) == 0xff || G8(0x6b00) != head)) {
        snprintf(name, sizeof name, "HEAD%u", G8(0x5376)); name[5] = 0;
        gfx_load_pic(FARPTR(DATA_SEG, 0x6b01), 0, 0, head, name);
        G8(0x6b00) = head;
    }
    if (body != 0xff && (G8(0x6b05) == 0xff || G8(0x6b05) != body)) {
        snprintf(name, sizeof name, "BODY%u", G8(0x5376)); name[5] = 0;
        gfx_load_pic(FARPTR(DATA_SEG, 0x6b06), 0, 0, body, name);
        G8(0x6b05) = body;
    }
}

// 489F:0708  Draw frame n (1-3) of an animation record at the position stored with that frame,
// through the back buffer. Returns 0 for a frame number out of range (an error in the original).
int scene_draw_anim_frame(farptr rec, u8 n) {
    if (n == 0 || n > 3) return 0;
    farptr pic = P32(rec, n * 8 - 2);
    if (pic) {
        gfx_merge_pic_clipped(G32(0x4a20), pic, 1, 0, (s16)(P16(pic, 6) + 2), (s16)(P16(pic, 4) + 2));
        gfx_flush_back_buffer();
    }
    return 1;
}

// 489F:04F9  Show the selected character's portrait in a frame at the top right of the screen.
// A character stores a head choice and a body choice (bytes BBh and BCh, both from 1 upward),
// and two tables turn those choices into picture numbers.
void scene_show_character_portrait(void) {
    farptr ch = G32(0x5d92);
    if (!P8(ch, 0xbb) || !P8(ch, 0xbc)) return;
    ui_frame(FARPTR(RSEG(0x489f), 0x04f8), 0, 0, 11, 38, 1, 28);
    scene_load_portrait(G8(0x287d + P8(ch, 0xbc)), G8(0x286f + P8(ch, 0xbb)));
    scene_draw_portrait(1, 1, 28);
}

// ---- 3B80:0006  Playing an animation (picture `id` from the area's set, flag 2) at (row, col), `loops` times through
// its frames; a frame stays (DS:49E2 + 3) times its delay (longint at frame*8 - 6) in clock ticks. ----
void anim_play(u8 loops, u8 id, u16 row, u16 col) {
    s32 last = (s32)game_clock();
    u8 done = 0;
    farptr rec = scratch_push(0x42);
    memset(&mem[LIN(rec)], 0, 0x42);
    call_original(0x489f, 0x00a2, 6, RSEG(0x3b80), 0x0000, id, 2, FP_SEG(rec), FP_OFF(rec));
    call_original(0x1464, 0x0d71, 8, (u16)(col - 1), (u16)(row - 1), 0, 0, FP_SEG(rd32(LIN(rec) + 6)), FP_OFF(rd32(LIN(rec) + 6)), G16(0x4a22), G16(0x4a20));
    call_original(0x1464, 0x1b52, 0);
    do {
        u8 k = mem[LIN(rec) + 1];
        scene_draw(rd32(LIN(rec) + k * 8 - 2), 1, (s16)row, (s16)col);
        s32 now = (s32)game_clock();
        s32 wait = (s32)(G8(0x49e2) + 3) * (s32)rd32(LIN(rec) + k * 8 - 6);
        if (now - last > wait) {
            k++;
            if (k > mem[LIN(rec)]) { k = 1; done++; }
            mem[LIN(rec) + 1] = k;
            last = now;
        }
    } while (done != loops);
    call_original(0x489f, 0x048f, 2, FP_SEG(rec), FP_OFF(rec));
    scratch_pop(0x42);
}
// ---- 3B80:02A4  The finale: Tyranthraxus, the dragon's death and his last words, with the FINAL<area> pictures (the
// party's figures drawn for as many members as there are) and three animations. ----
void finale(void) {
    #define FCS(o) FARPTR(RSEG(0x3b80), (o))
    char a[8]; snprintf(a, sizeof a, "%u", G8(0x5376)); G8(0x4713) = 1; G8(0x4714) = (u8)a[0];
    G8(0x4712) = G8(0x49f3); G8(0x49f3) = 7; SETG32(0x470e, 0);
    text_clear_box(1, 0x11, 0x26, 0x16);
    text_put_string(FCS(0x0114), 10, 0x11, 1); text_put_string(FCS(0x0138), 10, 0x12, 1); text_put_string(FCS(0x015d), 10, 0x13, 1);
    hud_draw_screen();
    farptr name = scratch_push(0x20);
    const u8 *f = &mem[LIN(FCS(0x0175))]; memcpy(&mem[LIN(name)], f, f[0] + 1u); mem[LIN(name)] = (u8)(f[0] + 1); mem[LIN(name) + f[0] + 1] = (u8)a[0];   // "FINAL" + area
    #define PIC(id, b, c) do { call_original(0x1464, 0x0111, 7, FP_SEG(name), FP_OFF(name), (id), 0, 0, DATA_SEG, 0x470e); \
        call_original(0x1464, 0x0d71, 8, (b), (c), 0, 0, G16(0x4710), G16(0x470e), G16(0x4a22), G16(0x4a20)); } while (0)
    PIC(1, 0, 0);
    u16 speed = (u16)(((10 - G8(0x49e2)) * 5) >> 1);
    anim_play((u8)speed, 2, 5, 3);
    PIC(3, 0, 0);
    u16 n = P16(G32(0x49d6), 0x67c);
    if (n > 1) PIC(4, 0, 0x0b);
    if (n > 2) PIC(5, 0x0d, 9);
    if (n > 3) PIC(6, 0, 9);
    #undef PIC
    scratch_pop(0x20);
    text_clear_box(1, 0x11, 0x26, 0x16);
    static const u16 l1[6] = { 0x017b, 0x019f, 0x01c4, 0x01eb, 0x020b, 0x0230 };
    for (int i = 0; i < 6; i++) text_put_string(FCS(l1[i]), 10, (u8)(0x11 + i), 1);
    speed = (u16)(((10 - G8(0x49e2)) * 5) >> 1);
    anim_play((u8)speed, 7, 1, 3);
    text_clear_box(1, 0x11, 0x26, 0x16);
    static const u16 l2[4] = { 0x0239, 0x0260, 0x0288, 0x029b };
    for (int i = 0; i < 4; i++) text_put_string(FCS(l2[i]), 10, (u8)(0x11 + i), 1);
    anim_play(1, 8, 1, 3);
    text_pause(); text_pause(); text_pause();
    G8(0x49f3) = G8(0x4712);
    hud_draw_screen();
    #undef FCS
}

// ---- 3B80:06B3  Recolouring the overhead-map symbols (lists at DS:0900 and 090B: high bit = second symbol set) with
// the tables at DS:091D/092D/093D; with `show`, the overhead view pans through nine positions (DS:095F/094D)
// meanwhile and ends at (19h, 17h) with two pauses. The colours are always put back at the end (dither 0). ----
static void sym_recolor(u8 dither) {
    for (int k = 0; k <= 10; k++) { u8 v = G8(k + 0x900); gfx_recolor_pic(G32(v < 0x80 ? 0x5e52 : 0x5e56), dither, v & 0x7f, FARPTR(DATA_SEG, 0x92d), FARPTR(DATA_SEG, 0x91d)); }
    for (int k = 0; k <= 0x11; k++) { u8 v = G8(k + 0x90b); gfx_recolor_pic(G32(v < 0x80 ? 0x5e52 : 0x5e56), dither, v & 0x7f, FARPTR(DATA_SEG, 0x93d), FARPTR(DATA_SEG, 0x91d)); }
}
void map_reveal(u8 show) {
    if (show) {
        view_overhead(0x0f, 0x11);
        call_original(0x1464, 0x1b52, 0);
        for (int k = 0; k <= 8; k++) {
            sym_recolor(1);
            view_overhead((s8)G16(k * 2 + 0x95f), (s8)G16(k * 2 + 0x94d));
            call_original(0x1464, 0x1b52, 0);
        }
    }
    sym_recolor(0);
    if (show) { view_overhead(0x19, 0x17); call_original(0x1464, 0x1b52, 0); text_pause(); text_pause(); }
}
