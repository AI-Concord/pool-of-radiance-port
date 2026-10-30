// Turbo Pascal's System unit (1AF8), the rest the game uses: Val, MemAvail, MaxAvail, Halt and RunError.
#include "rtl.h"
#include "hybrid.h"

// 1AF8:12CE  the number in the n characters at s (an optional sign, then decimal digits or "$" and hex digits) into
// *v; stops at the first other character. Returns 1 on overflow or when there are no digits; *p is where it stopped.
static int val_digits(u32 *p, u16 n, s32 *v) {
    u16 ax = 0, dx = 0; int neg = 0;
    if (!n) return 1;
    if (mem[*p] == '+' || mem[*p] == '-') { neg = mem[*p] == '-'; ++*p; if (!--n) return 1; }
    if (mem[*p] == '$') {
        ++*p; if (!--n) return 1;
        for (; n; n--, ++*p) {
            u8 c = mem[*p]; if (c >= 0x61) c -= 0x20;
            u8 d;
            if ((u8)(c - '0') < 10) d = (u8)(c - '0');
            else if ((u8)(c - 'A') < 6) d = (u8)(c - 'A' + 10);
            else break;
            if (dx & 0xf000) return 1;
            dx = (u16)(dx << 4 | ax >> 12); ax = (u16)(ax << 4 | d);
        }
    } else {
        for (; n; n--, ++*p) {
            u8 d = (u8)(mem[*p] - '0'); if (d > 9) break;
            if (dx & 0xf000) return 1;
            u32 x = ((u32)dx << 16 | ax) * 10u + d;
            ax = (u16)x; dx = (u16)(x >> 16);
            if (dx & 0x8000) return 1;
        }
    }
    u32 x = (u32)dx << 16 | ax;
    if (neg) x = 0u - x;
    *v = (s32)x;
    return n != 0;          // characters left over: the caller reports where
}

// 1AF8:13B1  Val(s, v, code) for a longint: leading spaces skipped; *code = 0, or the 1-based position of the first
// character that does not belong (then the result is 0).
s32 rtl_val(farptr s, farptr code) {
    u32 base = LIN(s), p = base + 1; u16 n = mem[base];
    while (n && mem[p] == ' ') { p++; n--; }
    s32 v = 0;
    if (val_digits(&p, n, &v)) { wr16(LIN(code), (u16)(p - base)); return 0; }
    wr16(LIN(code), 0);
    return v;
}

// 1AF8:029C MemAvail / 02B8 MaxAvail: the free-list blocks (each with its size as paragraphs at +6 and bytes at +4;
// the list ends at a block in HeapPtr's segment) and the space between HeapPtr (DS:4404) and HeapEnd (DS:4408).
static s32 heap_avail(int max) {
    u32 a = (u32)G16(0x4408) - G16(0x4404);
    u16 ax = (u16)a & 0xf, dx = (u16)(G16(0x440a) - G16(0x4406) - ((a >> 16) & 1));
    u16 end = G16(0x4406), off = G16(0x440c), seg = G16(0x440e);
    while (seg != end) {
        u32 b = ((u32)seg << 4) + off; u16 bo = rd16(b + 4), bs = rd16(b + 6);
        if (!max) { u32 t = (u32)ax + bo; ax = (u16)t; if (t >> 16) dx += 0x1000; dx += bs; }
        else if (bs > dx || (bs == dx && bo > ax)) { ax = bo; dx = bs; }
        off = rd16(b); seg = rd16(b + 2);
    }
    u32 r = (u32)dx * 16 + ax;
    return (s32)r;
}
s32 rtl_memavail(void) { return heap_avail(0); }
s32 rtl_maxavail(void) { return heap_avail(1); }

// 1AF8:00E9 Halt(code) / 00E2 RunError at an address: ExitCode (DS:441C) and ErrorAddr (DS:441E, its segment made
// relative to the program); the ExitProc chain (DS:4418, empty in this game) run; Input and Output closed; the
// 19 interrupt vectors saved at start-up (DS:8708) put back; "Runtime error nnn at ssss:oooo." when there is an
// error address; then DOS function 4Ch.
static void dos_char(u8 c) { R[rDX] = c; R[rAX] = 0x0600; emu_host_call(0x21); }
static void dos_hex4(u16 v) { for (int sh = 12; sh >= 0; sh -= 4) { u8 d = (v >> sh) & 0xf; dos_char((u8)(d < 10 ? '0' + d : 'A' + d - 10)); } }
static void dos_cstr(u16 off) { for (u32 a = LIN(FARPTR(RSEG(0x1af8), off)); mem[a]; a++) dos_char(mem[a]); }
void rtl_exit(u16 code, u16 err_off, u16 err_seg) {
    wr16(DS_LIN + 0x441c, code);
    if (err_off | err_seg) err_seg = (u16)(err_seg - G16(0x4422) - 0x10);    // (the overlay list at DS:43FA is empty)
    wr16(DS_LIN + 0x441e, err_off); wr16(DS_LIN + 0x4420, err_seg);
    while (G16(0x4418) | G16(0x441a)) {
        u16 o = G16(0x4418), s = G16(0x441a);
        wr16(DS_LIN + 0x4418, 0); wr16(DS_LIN + 0x441a, 0); wr16(DS_LIN + 0x4426, 0);
        emu_pcall((u16)(s - RSEG(0x1000) + 0x1000), o, 0);
    }
    text_close(FARPTR(DATA_SEG, 0x8508)); text_close(FARPTR(DATA_SEG, 0x8608));
    static const u8 vec[19] = { 0x00, 0x02, 0x1b, 0x21, 0x23, 0x24, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x75 };
    for (int i = 0; i < 19; i++) { u32 a = DS_LIN + 0x8708 + 4 * i; wr16(vec[i] * 4, rd16(a)); wr16(vec[i] * 4 + 2, rd16(a + 2)); }
    if (G16(0x441e) | G16(0x4420)) {
        dos_cstr(0x201);
        u8 e = (u8)G16(0x441c); dos_char((u8)('0' + e / 100)); dos_char((u8)('0' + e / 10 % 10)); dos_char((u8)('0' + e % 10));
        dos_cstr(0x210); dos_hex4(G16(0x4420)); dos_char(':'); dos_hex4(G16(0x441e)); dos_cstr(0x215);
    }
    R[rAX] = (u16)(0x4c00 | (G16(0x441c) & 0xff));
    emu_host_call(0x21);
}
void rtl_halt(u16 code) { rtl_exit(code, 0, 0); }
void rtl_run_error(u16 code) { rtl_exit(code, 0, 0); }      // from C there is no caller address to report

static int h_val(void) { RET32((u32)rtl_val(FARG(2), FARG(0))); return 8; }
static int h_memavail(void) { RET32((u32)rtl_memavail()); return 0; }
static int h_maxavail(void) { RET32((u32)rtl_maxavail()); return 0; }
static int h_halt(void) { rtl_halt(R[rAX]); return 0; }
static int h_run_error(void) { u32 sp = ((u32)SG[sSS] << 4) + R[rSP]; rtl_exit(R[rAX], rd16(sp), rd16(sp + 2)); return 0; }   // error address: the caller's return address
void rtl_sys_hooks(void) {
    emu_hook(0x1af8, 0x13b1, h_val, 1, "rtl_val");
    emu_hook(0x1af8, 0x029c, h_memavail, 1, "rtl_memavail");
    emu_hook(0x1af8, 0x02b8, h_maxavail, 1, "rtl_maxavail");
    emu_hook(0x1af8, 0x00e9, h_halt, 1, "rtl_halt");
    emu_hook(0x1af8, 0x00e2, h_run_error, 1, "rtl_run_error");
}

// Self-test (script "selftest val N", with -H): random strings through the original Val and rtl_val.
int rtl_val_selftest(int n) {
    static const char al[] = " +-$0123456789abcdefABCDEFxg9";
    u32 rng = 777; int bad = 0;
    farptr str = scratch_push(0x20), c1 = scratch_push(2), c2 = scratch_push(2);
    for (int i = 0; i < n; i++) {
        rng = rng * 1103515245u + 12345u; int len = (rng >> 16) % 14;
        P8(str, 0) = (u8)len;
        for (int k = 1; k <= len; k++) { rng = rng * 1103515245u + 12345u; P8(str, k) = (u8)al[(rng >> 16) % (sizeof al - 1)]; }
        if (i % 3 == 0 && len > 1) { P8(str, 1) = (u8)(i % 2 ? '-' : ' '); }
        u32 a = emu_pcall(0x1af8, 0x13b1, 4, FP_SEG(str), FP_OFF(str), FP_SEG(c1), FP_OFF(c1));
        u32 b = (u32)rtl_val(str, c2);
        if (a != b || P16(c1, 0) != P16(c2, 0)) { if (bad++ < 5) fprintf(stderr, "[selftest] val '%.*s': %08x/%u vs %08x/%u\n", len, &mem[LIN(str) + 1], a, P16(c1, 0), b, P16(c2, 0)); }
    }
    scratch_pop(2); scratch_pop(2); scratch_pop(0x20);
    fprintf(stderr, "[selftest] val: %d cases, %d differ\n", n, bad);
    return bad;
}
