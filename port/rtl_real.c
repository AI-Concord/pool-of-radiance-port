// Turbo Pascal's 6-byte reals (System unit, 1AF8): longint to real, multiply, divide and Trunc, which the game uses
// only for ROB's coin arithmetic. A real travels in AX BX DX: AL the exponent (bias 129, 0 = zero), AH BX DX the
// 40-bit mantissa, least significant first, with the sign in DX's top bit and the leading 1 implied. These are
// transliterations of the originals, register by register and carry by carry, so the rounding is the same.
#include "rtl.h"
#include "hybrid.h"

typedef struct { u16 ax, bx, dx; } r48;
#define run_error rtl_run_error
static const r48 ZERO = {0, 0, 0};

// 1AF8:1004..1037  shared tail of multiply and divide: normalise by one bit if needed, take the exponent bias off,
// round at bit 7 of the lowest word, and pack exponent and sign. Underflow gives 0; returns 1 on overflow.
static int finish(u16 ax, u16 bx, u16 dx, u16 cx, r48 *out) {
    if (!(dx & 0x8000)) { u32 c1 = ax >> 15; ax <<= 1; u32 c2 = bx >> 15; bx = (u16)(bx << 1 | c1); dx = (u16)(dx << 1 | c2); cx--; }
    cx -= 0x8081;
    u32 t = (u32)ax + 0x80; ax = (u16)t; t = (u32)bx + (t >> 16); bx = (u16)t; t = (u32)dx + (t >> 16); dx = (u16)t;
    if (t >> 16) { dx = (u16)(0x8000 | dx >> 1); cx++; }
    if (cx & 0x4000) { *out = ZERO; return 0; }
    cx++;
    ax = (u16)((ax & 0xff00) | (cx & 0xff));
    dx ^= (u16)(cx & 0xff00);
    *out = (r48){ax, bx, dx};
    return (cx >> 8) & 1;
}

// 1AF8:0F33  a * b. A short operand (only its top word non-zero) takes the quick three-product path.
static int real_mul(r48 a, r48 b, r48 *out) {
    u16 ax = a.ax, bx = a.bx, dx = a.dx, cx = b.ax, si = b.bx, di = b.dx;
    if (!(ax & 0xff) || !(cx & 0xff)) { *out = ZERO; return 0; }
    u16 bp = dx;
    dx = (dx ^ di) & 0x8000;
    u16 e = (u16)(ax & 0xff) + (cx & 0xff);                 // add dl,cl / adc dh,0
    dx = (u16)((dx & 0xff00) + ((e >> 8) << 8) | (e & 0xff));
    ax &= 0xff00; cx &= 0xff00;
    bp |= 0x8000; di |= 0x8000;
    u16 sexp = dx;
    u16 hi, mid, lo;
    int a_short = !(ax >> 8) && !bx, b_short = !(cx >> 8) && !si;
    if (a_short || b_short) {
        if (!a_short) { u16 t = ax; ax = cx; cx = t; t = bx; bx = si; si = t; t = bp; bp = di; di = t; }
        u16 h = bp >> 8;
        u32 p = (u32)h * (cx >> 8);                          // mov al,ah / mul ch
        u16 bxr = (u16)p;
        p = (u32)bp * si; u32 s = (u32)bxr + (u16)p; bxr = (u16)s; u16 c = (u16)((p >> 16) + (s >> 16));
        p = (u32)bp * di; s = (u32)(u16)p + c; u16 axr = (u16)s; u16 dxr = (u16)((p >> 16) + (s >> 16));
        lo = bxr; mid = axr; hi = dxr;
    } else {
        u16 w[6] = { ax, bx, bp, cx, si, di };               // [bp+0] [bp+2] [bp+4] [bp+6] [bp+8] [bp+10]
        u32 s0 = (u32)(w[0] >> 8) * (w[3] >> 8);              // si
        u16 r_si = (u16)s0, r_di = 0, r_bx = 0; u32 p, t;
        #define ACC3(x, y) p = (u32)(x) * (y); t = (u32)r_si + (u16)p; r_si = (u16)t; t = (u32)r_di + (u16)(p >> 16) + (t >> 16); r_di = (u16)t; r_bx += (u16)(t >> 16);
        ACC3(w[0], w[4]) ACC3(w[1], w[3])
        #undef ACC3
        u16 q_di = r_di, q_bx = r_bx, q_si = 0;
        #define ACC3(x, y) p = (u32)(x) * (y); t = (u32)q_di + (u16)p; q_di = (u16)t; t = (u32)q_bx + (u16)(p >> 16) + (t >> 16); q_bx = (u16)t; q_si += (u16)(t >> 16);
        ACC3(w[0], w[5]) ACC3(w[1], w[4]) ACC3(w[2], w[3])
        #undef ACC3
        u16 z_bx = q_bx, z_si = q_si, z_di = 0;
        #define ACC3(x, y) p = (u32)(x) * (y); t = (u32)z_bx + (u16)p; z_bx = (u16)t; t = (u32)z_si + (u16)(p >> 16) + (t >> 16); z_si = (u16)t; z_di += (u16)(t >> 16);
        ACC3(w[1], w[5]) ACC3(w[2], w[4])
        #undef ACC3
        p = (u32)w[2] * w[5]; t = (u32)(u16)p + z_si; u16 axr = (u16)t; u16 dxr = (u16)((p >> 16) + z_di + (t >> 16));
        lo = z_bx; mid = axr; hi = dxr;
    }
    return finish(lo, mid, hi, sexp, out);
}

// 1AF8:1038  a / b by restoring division, quotient bits collected inverted in three words (16, 16 and 10 bits).
static int real_div(r48 a, r48 b, r48 *out) {
    u16 ax = a.ax, bx = a.bx, dx = a.dx, cx = b.ax, si = b.bx, di = b.dx;
    if (!(ax & 0xff)) { *out = ZERO; return 0; }
    u16 bp = dx | 0x8000;
    dx = (dx ^ di) & 0x8000; di |= 0x8000;
    u16 sx = (u16)(dx | (ax & 0xff));                        // xchg al,dl: dl = exponent, al = 0
    u16 e = (u16)((sx & 0xff) - (cx & 0xff));
    sx = (u16)((((sx >> 8) - ((e >> 8) & 1)) << 8) | (e & 0xff));
    u8 ah = ax >> 8, ch = cx >> 8;
    u16 q[3]; int nq = 0; u8 al = 2; u16 acc = 1; int cf;
    for (;;) {
        // compare (bp:bx:ah) with (di:si:ch); subtract when not below; CF = below
        cf = bp != di ? bp < di : bx != si ? bx < si : ah < ch;
        if (!cf) { u16 b1 = ah < ch; ah = (u8)(ah - ch); u32 t = (u32)bx - si - b1; bx = (u16)t; u16 b2 = (t >> 16) & 1; bp = (u16)(bp - di - b2); }
    shift:;
        int out_c = acc >> 15; acc = (u16)(acc << 1 | cf);
        if (out_c) {
            if (al-- == 0) break;                               // dec al / js
            q[nq++] = acc;
            acc = al ? 1 : 0x40;
        }
        int c1 = ah >> 7; ah <<= 1; int c2 = bx >> 15; bx = (u16)(bx << 1 | c1); int c3 = bp >> 15; bp = (u16)(bp << 1 | c2);
        if (c3) { u16 b1 = ah < ch; ah = (u8)(ah - ch); u32 t = (u32)bx - si - b1; bx = (u16)t; u16 b2 = (t >> 16) & 1; bp = (u16)(bp - di - b2); cf = 0; goto shift; }
    }
    u16 rax = (u16)(acc << 6), rbx = q[1], rdx = q[0];
    rax = (u16)~rax; rbx = (u16)~rbx; rdx ^= 0xffff;
    u16 rcx = sx;
    if (!(rdx & 0x8000)) { u16 c1 = rax >> 15; rax <<= 1; u16 c2 = rbx >> 15; rbx = (u16)(rbx << 1 | c1); rdx = (u16)(rdx << 1 | c2); rcx--; }
    rcx += 0x8080;
    // continue at 1016 (the rounding of finish, without its normalising step and bias)
    u32 t = (u32)rax + 0x80; rax = (u16)t; t = (u32)rbx + (t >> 16); rbx = (u16)t; t = (u32)rdx + (t >> 16); rdx = (u16)t;
    if (t >> 16) { rdx = (u16)(0x8000 | rdx >> 1); rcx++; }
    if (rcx & 0x4000) { *out = ZERO; return 0; }
    rcx++;
    rax = (u16)((rax & 0xff00) | (rcx & 0xff));
    rdx ^= (u16)(rcx & 0xff00);
    *out = (r48){rax, rbx, rdx};
    return (rcx >> 8) & 1;
}

// 1AF8:10D9  longint to real.
static r48 real_from_long(s32 v) {
    u16 ax = (u16)v, dx = (u16)((u32)v >> 16);
    if (!(ax | dx)) return (r48){0, 0, 0};
    u16 sign = dx & 0x8000;
    if (sign) { u32 n = 0u - (u32)v; ax = (u16)n; dx = (u16)(n >> 16); }
    u16 bx = ax; u8 e = 0xa0;
    if (!dx) { dx = bx; bx = 0; e = 0x90; if (!(dx >> 8)) { dx = (u16)(dx << 8); e = 0x88; } }
    while (!(dx & 0x8000)) { e--; u16 c = bx >> 15; bx <<= 1; dx = (u16)(dx << 1 | c); }
    if (!sign) dx &= 0x7fff;
    return (r48){e, bx, dx};
}

// 1AF8:1118  Trunc (round = 0) or Round (round = 1): the 32 top mantissa bits shifted into place. Returns 1 when
// the value does not fit a longint.
static int real_to_long(r48 a, int round, s32 *out) {
    u8 ex = (u8)a.ax; u16 ax = a.bx, dx = a.dx;
    if (ex > 0x9f) return 1;
    u8 cl = (u8)(0x9f - ex);
    if (cl > 0x1f) { *out = 0; return 0; }
    cl++;
    u16 sign = dx & 0x8000; dx |= 0x8000;
    if (cl >= 0x11) { ax = dx; dx = 0; cl -= 0x10; }
    if (cl >= 9) { ax = (u16)((ax >> 8) | (dx << 8)); dx >>= 8; cl -= 8; }
    int c = 0;
    while (cl--) { c = ax & 1; ax = (u16)(ax >> 1 | dx << 15); dx >>= 1; }
    u32 v = (u32)dx << 16 | ax;
    if (c && round) { v++; if (v & 0x80000000u) return 1; }
    if (sign) v = 0u - v;
    *out = (s32)v;
    return 0;
}

real48 rtl_real_long(s32 v) { r48 r = real_from_long(v); return (real48){r.ax, r.bx, r.dx}; }
real48 rtl_real_mul(real48 a, real48 b) { r48 r; if (real_mul((r48){a.ax, a.bx, a.dx}, (r48){b.ax, b.bx, b.dx}, &r)) run_error(0xcd); return (real48){r.ax, r.bx, r.dx}; }
real48 rtl_real_div(real48 a, real48 b) {
    if (!(b.ax & 0xff)) run_error(0xc8);
    r48 r; if (real_div((r48){a.ax, a.bx, a.dx}, (r48){b.ax, b.bx, b.dx}, &r)) run_error(0xcd);
    return (real48){r.ax, r.bx, r.dx};
}
s32 rtl_real_trunc(real48 a) { s32 v; if (real_to_long((r48){a.ax, a.bx, a.dx}, 0, &v)) run_error(0xcf); return v; }

// Register-based entry points (the arguments and the result in registers; nothing on the stack).
static int h_real_long(void) { real48 r = rtl_real_long((s32)((u32)R[rDX] << 16 | R[rAX])); R[rAX] = r.ax; R[rBX] = r.bx; R[rDX] = r.dx; return 0; }
static int h_real_mul(void) { real48 r = rtl_real_mul((real48){R[rAX], R[rBX], R[rDX]}, (real48){R[rCX], R[rSI], R[rDI]}); R[rAX] = r.ax; R[rBX] = r.bx; R[rDX] = r.dx; return 0; }
static int h_real_div(void) { real48 r = rtl_real_div((real48){R[rAX], R[rBX], R[rDX]}, (real48){R[rCX], R[rSI], R[rDI]}); R[rAX] = r.ax; R[rBX] = r.bx; R[rDX] = r.dx; return 0; }
static int h_real_trunc(void) { s32 v = rtl_real_trunc((real48){R[rAX], R[rBX], R[rDX]}); R[rAX] = (u16)v; R[rDX] = (u16)((u32)v >> 16); return 0; }
void rtl_real_hooks(void) {
    emu_hook(0x1af8, 0x119a, h_real_long, 1, "rtl_real_long");
    emu_hook(0x1af8, 0x1186, h_real_mul, 1, "rtl_real_mul");
    emu_hook(0x1af8, 0x118c, h_real_div, 1, "rtl_real_div");
    emu_hook(0x1af8, 0x119e, h_real_trunc, 1, "rtl_real_trunc");
}

// Self-test (script command "selftest real N", in the untouched original -H, where the calls below reach the
// original routines): N random operand pairs through both, every result compared.
static u32 rng = 12345; static u32 rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static r48 orig2(u16 off, r48 a, r48 b) { R[rAX] = a.ax; R[rBX] = a.bx; R[rDX] = a.dx; R[rCX] = b.ax; R[rSI] = b.bx; R[rDI] = b.dx; emu_pcall(0x1af8, off, 0); return (r48){R[rAX], R[rBX], R[rDX]}; }
static r48 rnd_real(void) { u16 lo = (u16)rnd(), mid = (u16)rnd(), hi = (u16)rnd(); u8 e = (u8)(0x70 + rnd() % 0x30); return (r48){(u16)(lo & 0xff00) | e, mid, hi}; }
int rtl_real_selftest(int n) {
    int bad = 0;
    for (int i = 0; i < n; i++) {
        r48 a = rnd_real(), b = rnd_real(), x, y;
        if (i % 7 == 0) b.ax = (u16)(b.ax & 0xff);                  // short operands take the quick path
        if (i % 11 == 0) { a.ax &= 0xff; a.bx = 0; }
        x = orig2(0x1186, a, b); real_mul(a, b, &y);
        if (x.ax != y.ax || x.bx != y.bx || x.dx != y.dx) { if (bad++ < 5) fprintf(stderr, "[selftest] mul %04x %04x %04x * %04x %04x %04x: %04x %04x %04x vs %04x %04x %04x\n", a.ax, a.bx, a.dx, b.ax, b.bx, b.dx, x.ax, x.bx, x.dx, y.ax, y.bx, y.dx); }
        x = orig2(0x118c, a, b); real_div(a, b, &y);
        if (x.ax != y.ax || x.bx != y.bx || x.dx != y.dx) { if (bad++ < 5) fprintf(stderr, "[selftest] div %04x %04x %04x / %04x %04x %04x: %04x %04x %04x vs %04x %04x %04x\n", a.ax, a.bx, a.dx, b.ax, b.bx, b.dx, x.ax, x.bx, x.dx, y.ax, y.bx, y.dx); }
        s32 v = (s32)(rnd() << 8 ^ rnd()); if (i % 3 == 0) v >>= rnd() % 31;
        R[rAX] = (u16)v; R[rDX] = (u16)((u32)v >> 16); emu_pcall(0x1af8, 0x119a, 0); y = real_from_long(v);
        if (R[rAX] != y.ax || R[rBX] != y.bx || R[rDX] != y.dx) { if (bad++ < 5) fprintf(stderr, "[selftest] long %08x\n", (u32)v); }
        r48 t = a; t.ax = (u16)((t.ax & 0xff00) | (0x81 + rnd() % 0x1e));
        R[rAX] = t.ax; R[rBX] = t.bx; R[rDX] = t.dx; emu_pcall(0x1af8, 0x119e, 0); s32 w; real_to_long(t, 0, &w);
        if ((((u32)R[rDX] << 16) | R[rAX]) != (u32)w) { if (bad++ < 5) fprintf(stderr, "[selftest] trunc %04x %04x %04x\n", t.ax, t.bx, t.dx); }
    }
    fprintf(stderr, "[selftest] real: %d cases, %d differ\n", n, bad);
    return bad;
}
