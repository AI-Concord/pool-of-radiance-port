// Turbo Pascal's Overlay unit (1AA2): OvrInit, OvrGetBuf and OvrSetBuf, as the start-up code calls them. In the flat
// program every overlay is already in memory, so nothing is ever loaded from the file; but OvrInit still opens
// GAME.OVR (keeping a DOS handle, which the game's own file handles count from) and the unit's variables are set as
// they were, so that the game's memory stays identical. OvrResult is DS:43DC.
#include "rtl.h"
#include "hybrid.h"

static int dos21(u16 ax, u16 bx, u16 cx, u16 dx, u16 ds) {
    u16 sds = SG[sDS];
    R[rAX] = ax; R[rBX] = bx; R[rCX] = cx; R[rDX] = dx; SG[sDS] = ds;
    u16 f = emu_host_call(0x21);
    SG[sDS] = sds;
    return f & 1;
}

// 1AA2:0188  open dir + name (DOS function 3Dh with OvrFileMode, DS:43E4); returns 1 on failure, the handle in AX.
static int ovr_open(farptr buf, u32 at, farptr name) {
    u8 n = P8(name, 0); memcpy(&mem[at], &mem[LIN(name) + 1], n); mem[at + n] = 0;
    return dos21(0x3d00 | G8(0x43e4), R[rBX], R[rCX], FP_OFF(buf), FP_SEG(buf));
}

// 1AA2:0012  OvrInit(name): the file is looked for as given, then in the program's own directory (DOS 3 and later,
// from the environment), then along PATH. Then its blocks are walked from the start (an EXE's length from its MZ
// header, else 8-byte "FB" headers with a 32-bit length) to the one marked "FBOV", whose position is kept with the
// handle; the overlay read routine and the INT 3Fh handler are installed. OvrResult -1: no overlays or a bad file;
// -2: not found.
void ovr_init(farptr name) {
    if (!G16(0x43ec)) { wr16(DS_LIN + 0x43dc, 0xffff); return; }
    farptr buf = scratch_push(0x82); u32 b = LIN(buf);
    int fail = ovr_open(buf, b, name);
    if (fail) {
        dos21(0x3000, R[rBX], R[rCX], R[rDX], SG[sDS]);
        if ((u8)R[rAX] >= 3) {
            u32 env = (u32)rd16(((u32)G16(0x4422) << 4) + 0x2c) << 4, s = env;
            while (mem[s] || mem[s + 1]) s++;
            s += 4;                                          // the two NULs' second, then the string count word
            u32 d = b, slash = b;
            for (;;) { u8 c = mem[s++]; mem[d++] = c; if (!c) break; if (c == '\\') slash = d; }
            fail = ovr_open(buf, slash, name);
        }
    }
    if (fail) {
        u32 env = (u32)rd16(((u32)G16(0x4422) << 4) + 0x2c) << 4, s = env;
        for (;;) {
            if (!memcmp(&mem[s], "PATH=", 5)) { s += 5; break; }
            while (mem[s]) s++;
            s++; if (!mem[s]) { s = 0; break; }
        }
        while (fail && s && mem[s]) {
            u32 d = b; u8 last = 0;
            while (mem[s] && mem[s] != ';') { last = mem[s]; mem[d++] = mem[s++]; }
            if (mem[s] == ';') s++;
            if (last != ':' && last != '\\') mem[d++] = '\\';
            fail = ovr_open(buf, d, name);
        }
        if (fail) { scratch_pop(0x82); wr16(DS_LIN + 0x43dc, 0xfffe); return; }
    }
    u16 h = R[rAX];
    u32 pos = 0;
    farptr hd = scratch_push(8); u32 hl = LIN(hd);
    for (;;) {
        int bad = dos21(0x4200, h, (u16)(pos >> 16), (u16)pos, SG[sDS]) ||
                  dos21(0x3f00, h, 8, FP_OFF(hd), FP_SEG(hd)) || R[rAX] < 8;
        if (bad) { dos21(0x3e00, h, R[rCX], R[rDX], SG[sDS]); scratch_pop(8); scratch_pop(0x82); wr16(DS_LIN + 0x43dc, 0xffff); return; }
        if (rd16(hl) == 0x5a4d) { pos = (u32)rd16(hl + 4) * 0x200 - ((0u - rd16(hl + 2)) & 0x1ff); continue; }
        if (rd16(hl) != 0x4246) { dos21(0x3e00, h, R[rCX], R[rDX], SG[sDS]); scratch_pop(8); scratch_pop(0x82); wr16(DS_LIN + 0x43dc, 0xffff); return; }
        if (rd16(hl + 2) == 0x564f) break;
        pos += ((u32)rd16(hl + 6) << 16 | rd16(hl + 4)) + 8;
    }
    scratch_pop(8); scratch_pop(0x82);
    wr16(DS_LIN + 0x43fc, h); wr16(DS_LIN + 0x43e8, (u16)pos); wr16(DS_LIN + 0x43ea, (u16)(pos >> 16));
    wr16(DS_LIN + 0x8504, 0x0268); wr16(DS_LIN + 0x8506, RSEG(0x1aa2));
    if (!(G16(0x43f0) | G16(0x43f2))) { wr16(DS_LIN + 0x43f0, 0x02e5); wr16(DS_LIN + 0x43f2, RSEG(0x1aa2)); }
    dos21(0x253f, R[rBX], R[rCX], 0x02e6, RSEG(0x1aa2));
    wr16(DS_LIN + 0x43dc, 0);
}

// 1AA2:01F2  OvrGetBuf: the buffer's size in bytes (its paragraphs, DS:43F8 - DS:43F4).
s32 ovr_get_buf(void) { return (s32)((u32)(u16)(G16(0x43f8) - G16(0x43f4)) * 16); }

// 1AA2:01A3  OvrSetBuf(size): only with the file open and nothing loaded yet (DS:43FA); at least the minimum
// (DS:43EE paragraphs) and within the heap. The heap (HeapOrg, HeapPtr, FreeList) then starts after the buffer.
void ovr_set_buf(s32 size) {
    u16 r = 0xffff;
    if (G16(0x43fc) && !G16(0x43fa)) {
        u16 p = (u16)((u32)size >> 4);
        if (p >= G16(0x43ee)) {
            u32 e = (u32)p + G16(0x43f4);
            if (e > 0xffff || (u16)e > G16(0x440a)) r = 0xfffd;
            else {
                u16 s = (u16)e;
                wr16(DS_LIN + 0x43f8, s); wr16(DS_LIN + 0x4402, s); wr16(DS_LIN + 0x4406, s); wr16(DS_LIN + 0x440e, s);
                wr16(DS_LIN + 0x4404, 0); wr16(DS_LIN + 0x440c, 0);
                r = 0;
            }
        }
    }
    wr16(DS_LIN + 0x43dc, r);
}

static int h_ovr_init(void) { ovr_init(FARG(0)); return 4; }
static int h_ovr_get_buf(void) { RET32((u32)ovr_get_buf()); return 0; }
static int h_ovr_set_buf(void) { ovr_set_buf((s32)((u32)ARG(1) << 16 | ARG(0))); return 4; }
void rtl_ovr_hooks(void) {
    emu_hook(0x1aa2, 0x0012, h_ovr_init, 1, "ovr_init");
    emu_hook(0x1aa2, 0x01f2, h_ovr_get_buf, 1, "ovr_get_buf");
    emu_hook(0x1aa2, 0x01a3, h_ovr_set_buf, 1, "ovr_set_buf");
}
