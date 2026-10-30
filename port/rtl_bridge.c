#include <stdlib.h>
// Hybrid build: runtime services are still provided by the original Turbo Pascal System unit,
// called through the emulator. The stand-alone build will replace this file with native versions.
#include "hybrid.h"
#include "rtl.h"
#include "text.h"
#include "chr.h"
// ---- The runtime's heap (Turbo Pascal 6, System unit 1AF8:0308/03D3 behind GetMem 023F and FreeMem 0254). Blocks
// are multiples of 8 bytes at normalised addresses (offset 0 or 8). Free blocks form a list in address order from
// FreeList (DS:440C): a far pointer to the next and the size as paragraphs:offset (offset 0 or 8). The list ends at
// HeapPtr (DS:4404; the end test compares only the segment). HeapOrg DS:4400, HeapEnd DS:4408, HeapError DS:4414. ----
#define HW(a) rd16(a)
static u32 plin(u16 seg, u16 off) { return ((u32)seg << 4) + off; }
static void heap_size8(u16 size, u16 *lo, u16 *hi) {           // 1AF8:0491: rounded up to 8 bytes, as paragraphs:offset
    u32 t = (u32)size + 7; *hi = (u16)(t >> 4); *lo = (u16)(t & 8);
}
static void heap_add(u16 *off, u16 *seg, u16 lo, u16 hi) {    // seg:off + hi:lo, normalised in the low byte only
    u16 a = (u16)(*off + lo), d = (u16)(*seg + hi);             // (ADD AL/CL,F0h; ADC; AND AL/CL,0Fh: the high byte stays)
    if ((u8)a >= 0x10) d++;
    *off = (u16)((a & 0xff00) | (a & 0x0f)); *seg = d;
}
static u16 heap_error_call(u16 size) {                         // through the HeapError pointer
    farptr h = G32(0x4414);
    return (u16)call_original((u16)(FP_SEG(h) - LOAD_SEG + 0x1000), FP_OFF(h), 1, size);
}
// 1AF8:0308  Allocate; nil for size 0. Returns 0 on success (result in *res), 1 when the program must stop (the heap
// error function answered 0).
static int heap_get(u16 size, farptr *res) {
    if (!size) { *res = 0; return 0; }
    for (;;) {
        wr16(DS_LIN + 0x8754, size);
        u16 lo, hi; heap_size8(size, &lo, &hi);
        u16 poff = 0x440c, pseg = DATA_SEG;                     // the link that points at the current block
        for (;;) {
            u16 coff = HW(plin(pseg, poff)), cseg = HW(plin(pseg, poff) + 2);
            if (cseg == HW(DS_LIN + 0x4406)) {                  // the end of the list: take it from the top
                u16 noff = coff, nseg = cseg; heap_add(&noff, &nseg, lo, hi);
                if (nseg < HW(DS_LIN + 0x440a) || (nseg == HW(DS_LIN + 0x440a) && noff <= HW(DS_LIN + 0x4408))) {
                    wr16(DS_LIN + 0x4404, noff); wr16(DS_LIN + 0x4406, nseg);
                    wr16(plin(pseg, poff), noff); wr16(plin(pseg, poff) + 2, nseg);
                    heap_error_call(0);                         // the original tells the heap error function, with 0
                    *res = FARPTR(cseg, coff); return 0;
                }
                break;                                          // no room
            }
            u32 c = plin(cseg, coff);
            u16 slo = HW(c + 4), shi = HW(c + 6);
            if (hi > shi || (hi == shi && lo > slo)) { poff = coff; pseg = cseg; continue; }
            u16 nxo = HW(c), nxs = HW(c + 2);
            if (hi == shi && lo == slo) {                       // exact fit: unlink it
                wr16(plin(pseg, poff), nxo); wr16(plin(pseg, poff) + 2, nxs);
            } else {                                            // split: the rest stays free after it
                u16 rlo = (u16)(slo - lo), rhi = (u16)(shi - hi - (slo < lo)); rlo &= 0x0f;
                u16 roff = coff, rseg = cseg; heap_add(&roff, &rseg, lo, hi);
                u32 r = plin(rseg, roff);
                wr16(r, nxo); wr16(r + 2, nxs); wr16(r + 4, rlo); wr16(r + 6, rhi);
                wr16(plin(pseg, poff), roff); wr16(plin(pseg, poff) + 2, rseg);
            }
            *res = FARPTR(cseg, coff); return 0;
        }
        u8 a = (u8)heap_error_call(G16(0x8754));
        if (a < 1) return 1;
        if (a == 1) { *res = 0; return 0; }
        size = G16(0x8754);
    }
}
// 1AF8:0432  Join block `t` with `next` when it ends where `next` begins (taking over its link and adding its size;
// a block that ends at HeapPtr lowers HeapPtr instead); otherwise just link `t` to `next`.
static void heap_join(u16 toff, u16 tseg, u16 noff, u16 nseg) {
    u32 t = plin(tseg, toff);
    u16 eo = toff, es = tseg; heap_add(&eo, &es, HW(t + 4), HW(t + 6));
    if (es == nseg && eo == noff) {
        if (nseg == HW(DS_LIN + 0x4406) && noff == HW(DS_LIN + 0x4404)) { wr16(DS_LIN + 0x4404, toff); wr16(DS_LIN + 0x4406, tseg); return; }
        u32 n = plin(nseg, noff);
        wr16(t, HW(n)); wr16(t + 2, HW(n + 2));
        u16 so = HW(n + 4), ss = HW(n + 6); heap_add(&so, &ss, HW(t + 4), HW(t + 6));
        wr16(t + 4, so); wr16(t + 6, ss);
        return;
    }
    wr16(t, noff); wr16(t + 2, nseg);
}
// 1AF8:03D3  Free; 1 for a pointer that cannot be a block (not normalised, outside the heap, or already free).
static int heap_free(farptr p, u16 size) {
    if (!size) return 0;
    u16 boff = FP_OFF(p), bseg = FP_SEG(p);
    if (boff & 0xfff7) return 1;
    if (bseg < HW(DS_LIN + 0x4402) || (bseg == HW(DS_LIN + 0x4402) && boff < HW(DS_LIN + 0x4400))) return 1;
    if (bseg > HW(DS_LIN + 0x4406) || (bseg == HW(DS_LIN + 0x4406) && boff >= HW(DS_LIN + 0x4404))) return 1;
    u16 lo, hi; heap_size8(size, &lo, &hi);
    wr16(plin(bseg, boff) + 4, lo); wr16(plin(bseg, boff) + 6, hi);
    u16 poff = 0x440c, pseg = DATA_SEG;
    for (;;) {
        u16 coff = HW(plin(pseg, poff)), cseg = HW(plin(pseg, poff) + 2);
        if (bseg > cseg || (bseg == cseg && boff > coff)) { poff = coff; pseg = cseg; continue; }
        if (bseg == cseg && boff == coff) return 1;
        heap_join(boff, bseg, coff, cseg);
        heap_join(poff, pseg, boff, bseg);
        return 0;
    }
}
#define run_error rtl_run_error
farptr rtl_getmem(u16 size) { farptr r; if (heap_get(size, &r)) run_error(0xcb); return r; }   // 1AF8:023F GetMem
void rtl_freemem(farptr p, u16 size) { if (heap_free(p, size)) run_error(0xcc); }              // 1AF8:0254 FreeMem
const char *pstr(farptr p) { static char b[4][256]; static int k; char *o = b[k++ & 3]; u8 n = P8(p, 0); memcpy(o, &mem[LIN(p) + 1], n); o[n] = 0; return o; }
FILE *open_ci(const char *name, const char *mode);
FILE *plat_fopen(const char *dos_path, const char *mode) { return open_ci(dos_path, mode); }
// ---- The sound unit (18AE). Its timer-interrupt player (18AE:13EA, installed by 137C and removed by 13B1, which also
// silences both devices) and the sound data stay original; these are the routines around it. Its variables live in
// its own code segment. ----
#define SND(o) mem[((u32)RSEG(0x18ae) << 4) + (o)]
void port_out(u16 port, u8 v);
u8 emu_port_in(u16 port);
// 18AE:137C  Start the player: the timer interrupt (8) is saved in the unit's own words 1378h/137Ah and pointed at
// 18AE:13EA, and the timer set to divisor 13B1h (about 240 interrupts a second).
void sound_timer_on(void) {
    u32 sg = (u32)RSEG(0x18ae) << 4;
    wr16(sg + 0x1378, rd16(8 * 4)); wr16(sg + 0x137a, rd16(8 * 4 + 2));          // DOS Get Vector 08h
    wr16(8 * 4, 0x13ea); wr16(8 * 4 + 2, RSEG(0x18ae));                          // DOS Set Vector 08h
    port_out(0x43, 0xb6); port_out(0x40, 0xb1); port_out(0x40, 0x13);
}
// 18AE:13B1  Stop it: the saved timer interrupt back, the timer at its usual rate, the PC speaker off and the four
// voices of the Tandy sound chip silenced.
void sound_timer_off(void) {
    u32 sg = (u32)RSEG(0x18ae) << 4;
    wr16(8 * 4, rd16(sg + 0x1378)); wr16(8 * 4 + 2, rd16(sg + 0x137a));
    port_out(0x40, 0xff); port_out(0x40, 0xff);
    port_out(0x61, emu_port_in(0x61) & 0xfc);
    port_out(0xc0, 0x9f); port_out(0xc0, 0xbf); port_out(0xc0, 0xdf); port_out(0xc0, 0xff);
}
// 18AE:0000  The time of day in hundredths of a second, from GetTime. The minutes are multiplied in 16 bits
// (6000 * minutes, the high word dropped), so past 10 minutes the count wraps; kept as in the original.
u32 sound_clock(void) {
    farptr v = scratch_push(8);
    dos_get_time(v + 6, v + 4, v + 2, v);
    u32 lin = LIN(v); u16 h = rd16(lin + 6), m = rd16(lin + 4), sec = rd16(lin + 2), hs = rd16(lin);
    scratch_pop(8);
    return (u32)h * 360000u + (u16)(m * 6000u) + (u16)(sec * 100u) + hs;
}
// 18AE:006C  Wait n hundredths; a clock that jumps back by more than n ends the wait.
void sound_wait(u16 n) {
    s32 end = (s32)(sound_clock() + n), t;
    do { t = (s32)sound_clock(); if (end - t > (s32)n) t = end + 1; } while (t < end);
}
// 18AE:1742 / 17AE  Start sound n on one of the two devices: the 4 voices' data words at table + n*8 (0 = voice
// unused) go into 30h-byte voice records from base, each cleared first and marked active; DS:209+voice notes n.
// The busy byte 1D8h keeps the interrupt player out while this runs.
static void sound_start(u16 n, u16 table, u16 base) {
    u32 sg = (u32)RSEG(0x18ae) << 4;
    wr16(sg + 0x1ff, n); SND(0x1d8) = 1;
    u16 si = (u16)(n * 8), di = base;
    for (SND(0x1d9) = 4; SND(0x1d9); SND(0x1d9)--, di += 0x30, si += 2) {
        u16 w = rd16(sg + (u16)(si + table));
        if (!w) continue;
        for (int b = 0x2e; b >= 0; b -= 2) wr16(sg + (u16)(di + b), 0);
        wr16(sg + (u16)(di + 2), w);
        SND(0x209 + (u8)(4 - SND(0x1d9))) = (u8)n;
        wr16(sg + di, 1);
    }
    SND(0x1d8) = 0;
}
void sound_start_pc(u16 n) { sound_start(n, 0x55f, 0x22d); }
void sound_start_tandy(u16 n) { sound_start(n, 0x607, 0x3c7); }
void sound_select(u8 n) { SND(0x20d) = n; }                                         // 18AE:181A
// 18AE:017B  Sound back on after a pause: the mode at DS:5372 is copied to 5373, then unless sound is off the
// player is started, both devices are set to sound 0 and the player removed again.
void sound_resume(void) {
    G8(0x5373) = G8(0x5372);
    if (G8(0x5372) == 2) return;
    sound_timer_on(); sound_start_pc(0); sound_start_tandy(0); sound_timer_off();
}
// 18AE:01A5  Music on (n > 0, kept in DS:5374; the player is removed when nothing was playing) or off (n = 0, which
// installs the player). Nothing when sound is off.
void sound_music(u16 n) {
    if (G8(0x5372) == 2) return;
    if (n == 0) sound_timer_on();
    else if (G16(0x5374) == 0) sound_timer_off();
    wr16(DS_LIN + 0x5374, n);
}
// 18AE:00DC  Starting a sound effect by number, by the sound mode at DS:5372 (2 = off; 0 and 1 the two kinds of
// sound hardware): 0 stops everything, 1 and FFh are special, others play on the current device. With music running
// (DS:5374) it is paused around the effect.
void sound_play(u16 id) {
    if (mem[DS_LIN + 0x5372] == 2) return;
    u16 music = (u16)(mem[DS_LIN + 0x5374] | mem[DS_LIN + 0x5375] << 8);
    if (music) sound_timer_on();
    if (id == 0) { sound_start_tandy(0); sound_start_pc(0); sound_select(0); }
    else if (id == 1) sound_select(1);
    else if (id == 0xff) { sound_start_tandy(0); sound_start_pc(0); }
    else if (mem[DS_LIN + 0x5372] == 1) sound_start_pc((u16)(id - 1));
    else if (mem[DS_LIN + 0x5372] == 0) sound_start_tandy((u16)(id - 1));
    if (music) { sound_wait(10); sound_start_tandy(0); sound_start_pc(0); sound_timer_off(); }
}
const char *plat_param1(void) {             // from the command tail in the DOS program segment prefix
    static char b[128]; const u8 *t = &mem[0x1100 + 0x80]; int n = t[0], i = 1, o = 0;
    while (i <= n && t[i] == ' ') i++;
    while (i <= n && t[i] != ' ' && t[i] != 13) b[o++] = (char)t[i++];
    b[o] = 0; return b;
}
// 1AF8:120F / 11C0 / 1247  The runtime's random numbers: RandSeed (DS:4428) = RandSeed * 08088405h + 1, and
// Random(n) is the new seed's high word modulo n (0 for n = 0). Randomize takes the seed from DOS Get Time
// (CX = hours:minutes the low word, DX = seconds:hundredths the high word).
static u32 rand_next(void) { u32 s = rd32(DS_LIN + 0x4428) * 0x08088405u + 1; wr32(DS_LIN + 0x4428, s); return s; }
u16 rtl_random(u16 range) {
    static int tr = -1; if (tr < 0) tr = getenv("RANDTRACE") != NULL;
    if (tr) fprintf(stderr, "[rand] range %u tick %u seed %08x\n", range, rd16(0x46c), rd32(DS_LIN + 0x4428));
    u32 s = rand_next(); return range ? (u16)((s >> 16) % range) : 0;
}
void rtl_randomize(void) {
    farptr v = scratch_push(8);
    dos_get_time(v + 6, v + 4, v + 2, v);
    u32 l = LIN(v);
    wr16(DS_LIN + 0x4428, (u16)((rd16(l + 6) << 8) | (u8)rd16(l + 4)));
    wr16(DS_LIN + 0x442a, (u16)((rd16(l + 2) << 8) | (u8)rd16(l)));
    scratch_pop(8);
}
u32 game_clock(void) { return emu_pcall(0x1709, 0x0c3f, 0); }
// ---- script interpreter routines not yet ported ----
#include "ecl.h"
void game_set_file_set(u8 n) { file_set_select(n); }
void map_load_part(u8 which, u8 id) { emu_pcall(0x491c, 0x0e08, 2, id, which); }
// 2366:1815  Comparing two strings for the script (A at DS:70C8, B at DS:71C8 by the callers): DS:6F78..6F7D get
// A=B, A<>B, A<B, A>B, A<=B, A>=B (Pascal string order: bytes, then length).
void ecl_compare_strings(const u8 *A, const u8 *B) {
    int n = A[0] < B[0] ? A[0] : B[0], c = memcmp(A + 1, B + 1, n);
    if (!c) c = (int)A[0] - (int)B[0];
    u8 *f = &mem[DS_LIN + 0x6f78];
    memset(f, 0, 6);
    if (c == 0) f[0] = 1; else f[1] = 1;
    if (c < 0) f[2] = 1;
    if (c > 0) f[3] = 1;
    if (c <= 0) f[4] = 1;
    if (c >= 0) f[5] = 1;
}
void ecl_compare_text(void) { ecl_compare_strings(&mem[DS_LIN + 0x70c8], &mem[DS_LIN + 0x71c8]); }
void ecl_original_handler(u16 offset) { emu_pcall(0x1d3f, offset, 0); }
void call_original_pstr(u16 gseg, u16 off, const u8 *pstr) {
    u16 sp0 = R[rSP];
    R[rSP] = (u16)(R[rSP] - 258);                        // room for the string on the emulated stack
    u32 a = ((u32)SG[sSS] << 4) + R[rSP];
    memcpy(&mem[a], pstr, (size_t)pstr[0] + 1);
    emu_pcall(gseg, off, 2, SG[sSS], R[rSP]);
    R[rSP] = sp0;
}
int call_original_flag(u16 gseg, u16 off) {
    R[rSP] = (u16)(R[rSP] - 2);
    u16 seg = SG[sSS], o = R[rSP]; u32 a = ((u32)seg << 4) + o;
    mem[a] = 0;
    emu_pcall(gseg, off, 2, seg, o);
    int v = mem[a];
    R[rSP] = (u16)(R[rSP] + 2);
    return v;
}
// 2366:1FB1  The party's slowest and fastest movement (11Ch), doubled for haste (effect 27h) or else halved for slow
// (2Ah), in bytes as the original. a = least, b = most.
void party_measures(u8 *a, u8 *b) {
    farptr c = rd32(DS_LIN + 0x5d96);
    *a = *b = mem[LIN(c) + 0x11c];
    for (; c; c = rd32(LIN(c) + 0x104)) {
        u8 v = mem[LIN(c) + 0x11c];
        if (char_find_effect(0x27, c)) v = (u8)(v << 1); else if (char_find_effect(0x2a, c)) v >>= 1;
        if (*b < v) *b = v;
        if (v < *a) *a = v;
    }
}
void call_original_scratch(u16 gseg, u16 off, int bytes) {
    u16 sp0 = R[rSP];
    R[rSP] = (u16)(R[rSP] - bytes);
    memset(&mem[((u32)SG[sSS] << 4) + R[rSP]], 0, (size_t)bytes);
    emu_pcall(gseg, off, 2, SG[sSS], R[rSP]);
    R[rSP] = sp0;
}
farptr scratch_push(int bytes) {
    R[rSP] = (u16)(R[rSP] - bytes);
    memset(&mem[((u32)SG[sSS] << 4) + R[rSP]], 0, (size_t)bytes);
    return FARPTR(SG[sSS], R[rSP]);
}
void scratch_pop(int bytes) { R[rSP] = (u16)(R[rSP] + bytes); }
// 10BA:2199  Find a status effect of the given kind on a character (nodes linked through byte 5
// from the far pointer at 7Fh). Native; kept here beside its former bridge.
farptr char_find_effect(u8 kind, farptr ch) {
    farptr e = P32(ch, 0x7f);
    while (e && P8(e, 0) != kind) e = P32(e, 5);
    return e;
}
int char_has_effect(u8 kind, farptr ch) { return char_find_effect(kind, ch) != 0; }
void item_draw_name(farptr item, farptr ch, u8 row, u8 col) { emu_pcall(0x10ba, 0x0442, 8, FP_SEG(ch), FP_OFF(ch), FP_SEG(item), FP_OFF(item), col, row, 0, 1); }
// ---- Turbo Pascal 6-byte reals, computed by the runtime's own routines so that every rounding is the original's.
// They take and return reals in DX:BX:AX (a second operand in DI:SI:CX). ----
// The real-number routines are in rtl_real.c.
// ---- Small pieces of the file and keyboard units and the heap-error handler. ----
void file_set_select(u8 n) { G8(0x5377) = G8(0x5376); G8(0x5376) = n; }   // 1802:094A: which area's files (old one kept)
void file_set_restore(void) { G8(0x5376) = G8(0x5377); }                  // 1802:095D
// 1899:0104  Meant to empty the keyboard buffer (while KeyPressed do ReadKey), but the compiled loop is jumped over
// on entry, so it does nothing; kept that way.
void keys_flush(void) {}
// 1899:0000  Leave the game: sound mode restored, the closing sound (DS:2612), the video mode from before (DS:4A1C)
// set again through the BIOS, Halt.
void game_quit(void) {
    G8(0x5372) = G8(0x5373);
    sound_play(G16(0x2612));
    G8(0x4a03) = 0; G8(0x4a02) = G8(0x4a1c);
    dos_intr(0x10, FARPTR(DATA_SEG, 0x4a02));
    rtl_halt(0);
}
// 1C7C:0042  The heap-error function (installed in HeapError, DS:4414, by 1C7C:00AC): asked for `size` bytes that
// are not there, it warns "WARNING: Insufficient Memory" and, in a game (DS:49D0), saves it (3941:1D58) until that
// works (DS:557D); outside a game it says "Insufficient Memory. Exiting Program". Then it quits. Its result is a local
// that is never set; `stale` is what that stack slot holds.
u16 heap_error(u16 size, u16 stale) {
    if (size == 0) return stale;
    u8 done = 0;
    do {
        text_prompt(FARPTR(RSEG(0x1c7c), 0x0000), 0x0e);
        if (G8(0x49d0)) { game_save(); done = G8(0x557d); }
        else { done = 1; text_prompt(FARPTR(RSEG(0x1c7c), 0x001d), 0x0e); }
    } while (!done);
    game_quit();
    return stale;
}
void heap_error_install(void) { wr16(DS_LIN + 0x4414, 0x25); wr16(DS_LIN + 0x4416, RSEG(0x1016)); }   // 1C7C:00AC

// ---- The runtime's string and memory primitives (System unit). Strings are length-prefixed; offsets wrap inside
// their segment as the 8086's string instructions do. The string functions leave the destination pointer on the
// stack for the caller, which the hooks reproduce by popping only the other parameters. ----
void emu_set_df(int on);
static u8 sb(u16 sg, u16 of) { return mem[((u32)sg << 4) + of]; }
static void sbw(u16 sg, u16 of, u8 v) { mem[((u32)sg << 4) + of] = v; }
static void str_copy_n(farptr d, farptr s, u16 n) { emu_set_df(0); for (u16 i = 0; i < n; i++) sbw(FP_SEG(d), (u16)(FP_OFF(d) + i), sb(FP_SEG(s), (u16)(FP_OFF(s) + i))); }
void rtl_str_load(farptr dst, farptr src) { str_copy_n(dst, src, (u16)(sb(FP_SEG(src), FP_OFF(src)) + 1)); }       // 1AF8:0B0B
void rtl_str_store(farptr src, farptr dst, u16 max) {                                                               // 1AF8:0B25
    u8 n = sb(FP_SEG(src), FP_OFF(src)); if (n > (u8)max) n = (u8)max;
    sbw(FP_SEG(dst), FP_OFF(dst), n); str_copy_n(dst + 1, src + 1, n);
}
void rtl_str_copy(farptr dst, farptr src, s16 index, s16 count) {                                                    // 1AF8:0B57 Copy
    s16 len = sb(FP_SEG(src), FP_OFF(src));
    if (index <= 0) index = 1;
    s16 avail = (s16)(len - index);
    u16 n;
    if (len < index) n = 0;                                     // SUB AX,CX borrows: nothing
    else { avail++; if (count < 0) count = 0; n = (u16)avail > (u16)count ? (u16)count : (u16)avail; }
    sbw(FP_SEG(dst), FP_OFF(dst), (u8)n);
    str_copy_n(dst + 1, FARPTR(FP_SEG(src), (u16)(FP_OFF(src) + index)), n);
}
void rtl_str_concat(farptr dst, farptr src) {                                                                       // 1AF8:0B98
    u8 cur = sb(FP_SEG(dst), FP_OFF(dst)), add = sb(FP_SEG(src), FP_OFF(src));
    u16 sum = (u16)(cur + add);
    if (sum > 0xff) { sbw(FP_SEG(dst), FP_OFF(dst), 0xff); add = (u8)~cur; } else sbw(FP_SEG(dst), FP_OFF(dst), (u8)sum);
    str_copy_n(FARPTR(FP_SEG(dst), (u16)(FP_OFF(dst) + cur + 1)), src + 1, add);
}
// 1AF8:0C10  Compare: the result is in the flags, as CMP of the first differing bytes or else of the two lengths.
void rtl_str_compare(farptr a, farptr b) {
    emu_set_df(0);
    u8 la = sb(FP_SEG(a), FP_OFF(a)), lb = sb(FP_SEG(b), FP_OFF(b)), n = la < lb ? la : lb;
    for (u16 i = 1; i <= n; i++) {
        u8 x = sb(FP_SEG(a), (u16)(FP_OFF(a) + i)), y = sb(FP_SEG(b), (u16)(FP_OFF(b) + i));
        if (x != y) { emu_flags_cmp8(x, y); return; }
    }
    emu_flags_cmp8(la, lb);
}
void rtl_char_str(farptr dst, u8 c) { emu_set_df(0); sbw(FP_SEG(dst), FP_OFF(dst), 1); sbw(FP_SEG(dst), (u16)(FP_OFF(dst) + 1), c); }   // 1AF8:0C3B
// 1AF8:1685  Move: forward, or backward (leaving the direction flag set) when the source offset is below the
// destination's; only the offsets are compared.
void rtl_move(farptr src, farptr dst, u16 n) {
    if (FP_OFF(src) >= FP_OFF(dst)) { str_copy_n(dst, src, n); emu_set_df(0); return; }
    for (u16 i = n; i > 0; i--) sbw(FP_SEG(dst), (u16)(FP_OFF(dst) + i - 1), sb(FP_SEG(src), (u16)(FP_OFF(src) + i - 1)));
    emu_set_df(1);
}
void rtl_fill(farptr dst, u16 n, u8 v) { for (u16 i = 0; i < n; i++) sbw(FP_SEG(dst), (u16)(FP_OFF(dst) + i), v); emu_set_df(0); }   // 1AF8:16A8 FillChar

// 1AF8:16BC  ParamStr(n) into dst: word n of the command line in the PSP (PrefixSeg, DS:4422), words being runs of
// characters above space (1AF8:1712). ParamStr(0), the program's path from the environment, is not used by the game.
void rtl_paramstr(farptr dst, u16 n) {
    u32 psp = (u32)G16(0x4422) << 4, di = psp + 0x81, si = di; u16 cx = mem[psp + 0x80], len = 0;
    if (n == 0) { fprintf(stderr, "[rtl] ParamStr(0) not modelled\n"); P8(dst, 0) = 0; return; }
    for (;;) {
        while (cx && mem[di] <= 0x20) { di++; cx--; }
        si = di;
        while (cx && mem[di] > 0x20) { di++; cx--; }
        len = (u16)(di - si);
        if (!len || !--n) break;
    }
    P8(dst, 0) = (u8)len; memcpy(&mem[LIN(dst) + 1], &mem[si], len);
}
