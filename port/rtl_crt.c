// The start of the run-time units: System's initialisation (1AF8:0000) and the Crt unit's (1A40:0000) with its text
// driver for Input and Output. Crt's console output and line input only matter for messages before the graphics
// start (a broken POOL.CFG), so its two InOut functions are simplified, as noted below; the rest is exact.
#include "rtl.h"
#include "hybrid.h"

u16 emu_delay_calibration(void);
static u16 bios10(u16 ax, u16 bx, u16 cx, u16 dx) { R[rAX] = ax; R[rBX] = bx; R[rCX] = cx; R[rDX] = dx; emu_host_call(0x10); return R[rAX]; }
static int dos21(u16 ax, u16 bx, u16 dx, u16 ds) {
    u16 sds = SG[sDS]; R[rAX] = ax; R[rBX] = bx; R[rDX] = dx; SG[sDS] = ds;
    u16 f = emu_host_call(0x21); SG[sDS] = sds; return f & 1;
}

// 1AF8:0000  System: PrefixSeg (DS:4422) from ES at entry; the overlay buffer (DS:43F4..43F8) just above the stack
// (SS:SP at entry, rounded up to a paragraph), and the heap after it up to the end of memory (PSP:2); HeapError
// (DS:4414) = the default (1AF8:00A9); the 19 vectors in DS:8708 saved and divide error, Ctrl-C, critical error and
// INT 3Fh taken over; Input and Output assigned to the standard handles and opened.
void sys_init(u16 psp, u16 ss, u16 sp) {
    wr16(DS_LIN + 0x4422, psp);
    u16 ax = (u16)(((u16)(sp + 0x13) >> 4) + ss);
    wr16(DS_LIN + 0x43f4, ax); wr16(DS_LIN + 0x43f6, ax);
    ax = (u16)(ax + G16(0x43ee));
    wr16(DS_LIN + 0x43f8, ax); wr16(DS_LIN + 0x4402, ax); wr16(DS_LIN + 0x4406, ax); wr16(DS_LIN + 0x440e, ax);
    wr16(DS_LIN + 0x440a, rd16(((u32)psp << 4) + 2));
    wr16(DS_LIN + 0x4414, 0x00a9); wr16(DS_LIN + 0x4416, RSEG(0x1af8));
    static const u8 vec[19] = { 0x00, 0x02, 0x1b, 0x21, 0x23, 0x24, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x75 };
    for (int i = 0; i < 19; i++) {
        dos21((u16)(0x3500 | vec[i]), R[rBX], R[rDX], SG[sDS]);
        wr16(DS_LIN + 0x8708 + 4 * i, R[rBX]); wr16(DS_LIN + 0x870a + 4 * i, SG[sES]);
    }
    dos21(0x2500, R[rBX], 0x00df, RSEG(0x1af8)); dos21(0x2523, R[rBX], 0x00e6, RSEG(0x1af8));
    dos21(0x2524, R[rBX], 0x00ae, RSEG(0x1af8)); dos21(0x253f, R[rBX], 0x00d7, RSEG(0x1af8));
    farptr none = FARPTR(RSEG(0x1af8), 0x0218);                  // an empty string
    text_assign(FARPTR(DATA_SEG, 0x8508), none); text_reset(FARPTR(DATA_SEG, 0x8508));
    text_assign(FARPTR(DATA_SEG, 0x8608), none); text_rewrite(FARPTR(DATA_SEG, 0x8608));
}

// 1A40:0331  AssignCrt(t): a closed text file whose Open is Crt's (1A40:035C), with no name.
void crt_assign(farptr t) {
    u32 r = LIN(t);
    wr16(r + 2, 0xd7b0); wr16(r + 4, 0x80); wr16(r + 0xc, FP_OFF(t) + 0x80); wr16(r + 0xe, FP_SEG(t));
    wr16(r + 0x10, 0x035c); wr16(r + 0x12, RSEG(0x1a40)); mem[r + 0x30] = 0;
}
// 1A40:035C  Crt's Open: reading by its line editor (0394), writing by its console output (0441, also the Flush),
// closing does nothing (0472).
u16 crt_drv_open(farptr t) {
    u32 r = LIN(t); u16 io = 0x0394, fl = 0x0472, cl = 0x0472;
    if (rd16(r + 2) != 0xd7b1) { wr16(r + 2, 0xd7b2); io = fl = 0x0441; }
    wr16(r + 0x14, io); wr16(r + 0x16, RSEG(0x1a40)); wr16(r + 0x18, fl); wr16(r + 0x1a, RSEG(0x1a40));
    wr16(r + 0x1c, cl); wr16(r + 0x1e, RSEG(0x1a40));
    return 0;
}
// 1A40:0441  Crt's output: the buffer goes to the screen and is emptied. Simplified: the text is written to the
// log rather than through the BIOS cursor and video memory (only start-up error messages ever come this way).
u16 crt_drv_write(farptr t) {
    u32 r = LIN(t); u16 n = rd16(r + 8); wr16(r + 8, 0);
    u32 b = LIN(FARPTR(rd16(r + 0xe), rd16(r + 0xc)));
    for (u16 i = 0; i < n; i++) fputc(mem[b + i], stderr);
    return 0;
}
// 1A40:0394  Crt's line editor for Read(Input). The game never reads the keyboard this way; simplified to an
// empty line.
u16 crt_drv_read(farptr t) {
    u32 r = LIN(t); u32 b = LIN(FARPTR(rd16(r + 0xe), rd16(r + 0xc)));
    fprintf(stderr, "[crt] Read(Input) not modelled\n");
    mem[b] = 0x0d; mem[b + 1] = 0x0a; wr16(r + 8, 0); wr16(r + 0xa, 2);
    return 0;
}

// 1A40:0097  set a text mode (7, or 3 for anything above 3), with the EGA 8x8-font case of the original not
// reached (the mode asked for has no high byte here).
static void crt_set_mode(u16 ax) {
    mem[0x487] &= 0xfe;
    u8 al = (u8)ax; if (al != 7 && al >= 4) al = 3;
    bios10(al, R[rBX], R[rCX], R[rDX]);
}
// 1A40:00E5  the screen's size and kind from the BIOS: LastMode (DS:84F4, with the 8x8-font flag above it), the
// window (DS:84F8/84FA) and screen size (DS:84FE) to the whole screen, CheckSnow (DS:84F3), DirectVideo on.
static void crt_screen_info(void) {
    u16 m = bios10(0x0f00, R[rBX], R[rCX], R[rDX]);
    bios10(0x1130, (u16)(R[rBX] & 0x00ff), R[rCX], (u16)(R[rDX] & 0xff00));
    u8 dl = (u8)R[rDX], cl = 0;
    if (!dl) { dl = 0x18; if ((u8)m <= 3) cl = 1; }
    u16 dx = (u16)(dl << 8 | (u8)((m >> 8) - 1));
    u16 ax = (u16)((u8)m | (dl > 0x18 ? 0x100 : 0));
    wr16(DS_LIN + 0x84f4, ax); wr16(DS_LIN + 0x84fe, dx); G8(0x84f3) = cl; G8(0x84f2) = 1;
    wr16(DS_LIN + 0x84f8, 0); wr16(DS_LIN + 0x84fa, dx);
}
// 1A40:0000  Crt: a text mode if the current one is graphics, the screen measured, TextAttr from the character
// under the cursor, CheckBreak on, the Delay loop calibrated against the BIOS tick (DS:84FC), Ctrl-Break (INT 1Bh)
// taken over, and Input and Output reassigned to Crt.
void crt_init(void) {
    u16 m = bios10(0x0f00, R[rBX], R[rCX], R[rDX]);
    if ((u8)m != 7 && (u8)m > 3) crt_set_mode(3);
    crt_screen_info();
    u16 a = bios10(0x0800, (u16)(R[rBX] & 0x00ff), R[rCX], R[rDX]);
    u8 attr = (a >> 8) & 0x7f; G8(0x8500) = attr; G8(0x84f6) = attr;
    G8(0x84f1) = 0; G8(0x8501) = 0; G8(0x8502) = 0; G8(0x84f0) = 1;
    wr16(DS_LIN + 0x84fc, emu_delay_calibration());
    dos21(0x251b, R[rBX], 0x012d, RSEG(0x1a40));
    crt_assign(FARPTR(DATA_SEG, 0x8508)); text_reset(FARPTR(DATA_SEG, 0x8508));
    crt_assign(FARPTR(DATA_SEG, 0x8608)); text_rewrite(FARPTR(DATA_SEG, 0x8608));
}

static int h_sys_init(void) { sys_init(SG[sES], SG[sSS], R[rSP]); SG[sDS] = DATA_SEG; R[rBP] = 0; return 0; }   // leaves DS = the data segment, BP = 0
static int h_crt_init(void) { crt_init(); return 0; }
static int h_crt_assign(void) { crt_assign(FARG(0)); return 4; }
static int h_crt_open(void) { RET16(crt_drv_open(FARG(0))); return 4; }
static int h_crt_write(void) { RET16(crt_drv_write(FARG(0))); return 4; }
static int h_crt_read(void) { RET16(crt_drv_read(FARG(0))); return 4; }
static int h_crt_nop(void) { RET16(0); return 4; }
void rtl_crt_hooks(void) {
    emu_hook(0x1af8, 0x0000, h_sys_init, 1, "sys_init");
    emu_hook(0x1a40, 0x0000, h_crt_init, 1, "crt_init");
    emu_hook(0x1a40, 0x0331, h_crt_assign, 1, "crt_assign");
    emu_hook(0x1a40, 0x035c, h_crt_open, 1, "crt_drv_open");
    emu_hook(0x1a40, 0x0441, h_crt_write, 1, "crt_drv_write");
    emu_hook(0x1a40, 0x0394, h_crt_read, 1, "crt_drv_read");
    emu_hook(0x1a40, 0x0472, h_crt_nop, 1, "crt_drv_nop");
}
