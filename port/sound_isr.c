#include <stdlib.h>
// The sound unit's timer interrupt (18AE:13EA, installed on INT 8 by sound_timer_on at 236.7 Hz): a small music
// sequencer for four voices, driving the PC speaker or the Tandy sound chip. Its state lives in the unit's own code
// segment (18AE); offsets below are within it. A transliteration of the original, including a dormant fake crash.
#include "rtl.h"
#include "hybrid.h"
void sound_timer_on(void); void sound_timer_off(void); void sound_start_pc(u16 n); void sound_start_tandy(u16 n);

static u32 sg;
#define B(o) mem[sg + (u16)(o)]
#define W(o) rd16(sg + (u16)(o))
#define WW(o, v) wr16(sg + (u16)(o), (u16)(v))
// 0x1D7 ticks until the BIOS's own tick, 0x1D8 busy, 0x1D9 voice loop counter, 0x1DA voice record size, 0x1DB the
// fake-crash countdown, 0x1FF the tune playing (0 none), 0x201 the voice being read, 0x203 the voice records (PC
// speaker 0x22D, Tandy 0x3C7), 0x205 the note frequency table (0x3AD / 0x547), 0x207 a subroutine's return, 0x209
// the tune of each voice, 0x20D sound on, 0x3C5 the voice heard on the PC speaker, 0x1378 the previous INT 8 vector.
// A voice record (0x30 bytes): +0 ticks left of the note, +2 next event, +4 pitch and +6 its slide, +8 the pitch
// sounded, +0Ah volume and +0Ch its slide, +0Eh tempo, +12h transposition, +14h ticks to the release, +16h envelope
// table, +18h position in it, +1Ah ticks to its next step, +1Ch vibrato table, +1Eh phase, +20h speed, +22h depth,
// +24h length.

static void voice_clear(u16 si) {
    static const u8 f[] = { 4, 6, 8, 0xa, 0xc, 0x10, 0x12, 0x16, 0x18, 0x1a, 0x1c, 0x1e, 0x20, 0x22, 0x24 };
    for (unsigned i = 0; i < sizeof f; i++) WW(si + f[i], 0);
}

// 18AE:160B  the voice's next events: commands FAh-FFh, or notes (top 3 bits: the voice the note is for; then the
// note, top bit = last of a chord, and its length). A note sets the length of the voice being read; only one for
// voice 0 sets a pitch, octave by octave from the frequency table.
static void voice_events(u16 si) {
    u16 di = W(si + 2);
    if (!di) return;
    WW(0x201, si);
    for (;;) {
        u8 bl = B(di++);
        if (bl >= 0xfa) {
            switch (bl) {
            case 0xfa: voice_clear(si); continue;
            case 0xfb: di = W(0x207); continue;
            case 0xfc: { u16 to = W(di); di += 2; WW(0x207, di); di = to; continue; }
            case 0xfd: si = (u16)(W(di) + W(0x203)); di += 2; voice_clear(si); continue;
            case 0xfe: {
                u8 f = B(di); u16 to = W(di + 1); di += 3;
                if (W(si + f)) { WW(si + f, W(si + f) - 1); if (!W(si + f)) continue; }
                di = to; continue;
            }
            default: {      // 0xff
                u8 f = B(di); u16 v = W(di + 1); di += 3;
                WW(si + f, v);
                if (f) continue;
                goto end;
            }
            }
        }
        for (;;) {                                   // 16C4: a note (and those chorded with it)
            u16 p = di;
            u8 cl = bl >> 5;
            u16 vd = (u16)(cl * B(0x1da) + W(0x203));
            u16 t = W(si + 0xe);
            WW(si, (u8)t <= 1 ? B(p + 1) : (u16)((u8)t * B(p + 1)));
            u16 a = B(p) & 0x7f;
            if (a != 0x7f && cl == 0) {
                u16 len = W(si); WW(vd, len); WW(vd + 0x14, len);
                a = (u16)(a + W(vd + 0x12));
                WW(vd + 0x18, 0); WW(vd + 0x1a, 1);
                u8 oct = 0xff; int borrow;
                do { oct++; borrow = a < 12; a = (u16)(a - 12); } while (!borrow);
                a = (u16)(a + 12);
                u16 fr = (u16)(W((u16)(a * 2 + W(0x205))) >> oct);
                WW(vd + 4, fr); WW(vd + 8, fr);
            }
            u8 al = B(p); di = (u16)(p + 2);
            if (al & 0x80) goto end;
            bl = B(di++);
        }
    }
end:
    si = W(0x201);
    if (!W(si)) { B(0x209 + (u8)(4 - B(0x1d9))) = 0; di = 0; }
    WW(si + 2, di);
}

// 18AE:157A  one tick of a voice: the volume and pitch slides, vibrato added to the pitch, the release and the
// envelope's steps, and the next events when the note is over.
static void voice_tick(u16 si) {
    WW(si + 0xa, W(si + 0xa) + W(si + 0xc));
    WW(si + 4, W(si + 4) + W(si + 6));
    u16 dx = 0, ax = (u16)(W(si + 0x1e) + W(si + 0x20));
    if (ax) {
        if (ax >= W(si + 0x24)) ax = (u16)(ax - W(si + 0x24));
        WW(si + 0x1e, ax);
        u16 di = (u16)(((s16)ax >> 4) + W(si + 0x1c));
        s32 pr = (s32)(s16)(u16)(B(di) << 8) * (s16)W(si + 0x22);
        dx = (u16)((u32)pr >> 16);
    }
    WW(si + 8, dx + W(si + 4));
    if (W(si + 0x14)) { WW(si + 0x14, W(si + 0x14) - 1); if (!W(si + 0x14)) { WW(si + 0x18, 0x10); WW(si + 0x1a, 1); } }
    WW(si, W(si) - 1);
    if (!W(si)) voice_events(si);
    if (W(si + 0x1a)) {
        WW(si + 0x1a, W(si + 0x1a) - 1);
        if (!W(si + 0x1a)) for (;;) {
            u16 bx = (u16)(W(si + 0x16) + W(si + 0x18));
            if (W(bx + 2) == 0xffff) {
                u16 v = W(bx); WW(si + 0xa, v); if (!v) WW(si + 0xc, 0);
                WW(si + 0x18, W(si + 0x18) + 4); continue;
            }
            WW(si + 0xc, W(bx)); WW(si + 0x1a, W(bx + 2)); WW(si + 0x18, W(si + 0x18) + 4);
            break;
        }
    }
}

// 18AE:1477  the PC speaker: the four voices ticked; the first audible one is played on timer channel 2.
static void pc_speaker(void) {
    u8 bl = 0;
    if (W(0x1ff)) {
        WW(0x3c5, 0); B(0x1d9) = 4; WW(0x205, 0x3ad); WW(0x203, 0x22d);
        for (u16 si = 0x22d; ; si += 0x30) {
            if (W(si)) { voice_tick(si); if (!W(0x3c5) && W(si + 0xa) && W(si)) WW(0x3c5, si); }
            if (!--B(0x1d9)) break;
        }
        if (B(0x20d) && W(0x3c5)) {
            u16 si = W(0x3c5);
            port_out(0x42, (u8)W(si + 8)); port_out(0x42, (u8)(W(si + 8) >> 8));
            bl = W(si + 0xa) & 3;
        }
    }
    port_out(0x61, (u8)((emu_port_in(0x61) & 0xfc) | bl));
}
static void tandy_silence(void) { port_out(0xc0, 0x9f); port_out(0xc0, 0xbf); port_out(0xc0, 0xdf); port_out(0xc0, 0xff); }
// 18AE:14EC  the Tandy chip: the four voices ticked, then each one's frequency and volume written.
static void tandy(void) {
    if (!W(0x1ff)) { tandy_silence(); return; }
    B(0x1d9) = 4; WW(0x205, 0x547); WW(0x203, 0x3c7);
    for (u16 si = 0x3c7; ; si += 0x30) { if (W(si)) voice_tick(si); if (!--B(0x1d9)) break; }
    if (!B(0x20d)) { tandy_silence(); return; }
    B(0x1d9) = 4; u8 dl = 0x80;
    for (u16 si = 0x3c7; ; si += 0x30) {
        u16 ax = (u16)(W(si + 8) >> 2);
        port_out(0xc0, (u8)(((u8)ax >> 4) ^ dl));
        if (dl != 0xe0) port_out(0xc0, (u8)(ax >> 8));
        u8 v = (u8)((u16)(0xffff - W(si + 0xa)) >> 12);
        port_out(0xc0, (u8)((v ^ dl) + 0x10));
        dl = (u8)(dl + 0x20);
        if (!--B(0x1d9)) break;
    }
}

// 18AE:13EA  the interrupt. If the countdown at 0x1DB is running (nothing in the game ever starts it), its end
// switches to text mode, shows "Run-time  Error 209 at 0BAD:0038" (kept with the top bits set) and quits with code
// 255: a fake crash, apparently left from a copy protection. Otherwise the voices are played (unless the previous
// tick is still at it), and every 13th tick the BIOS's own timer handler is chained to.
extern void (*port_trace)(u16 port, u8 v);
void emu_ncall_original(u16 gseg, u16 off);
static u16 sh_buf[2][4096]; static int sh_n[2], sh_k;
static void sh_trace(u16 port, u8 v) { if (sh_n[sh_k] < 4096) sh_buf[sh_k][sh_n[sh_k]++] = (u16)(port << 8 | v); }
// Debugging (SNDSHADOW=1): every tick done by the original code too (1477, 14EC with hooks off) from the same state;
// differences in port writes or state reported. The C result is kept.
static int shadow_check(void) {
    static int on = -1; if (on < 0) on = getenv("SNDSHADOW") != NULL;
    if (!on || B(0x1d8)) return 0;
    static u8 before[0x800], orig[0x800]; static long tick; tick++;
    memcpy(before, &mem[sg], sizeof before);
    sh_k = 1; sh_n[1] = 0; port_trace = sh_trace;
    u16 ds = SG[sDS]; SG[sDS] = RSEG(0x18ae);
    B(0x1d8) = 1; emu_ncall_original(0x18ae, 0x1477); emu_ncall_original(0x18ae, 0x14ec); B(0x1d8) = 0;
    SG[sDS] = ds;
    memcpy(orig, &mem[sg], sizeof orig); memcpy(&mem[sg], before, sizeof before);
    sh_k = 0; sh_n[0] = 0;
    B(0x1d8) = 1; pc_speaker(); tandy(); B(0x1d8) = 0;
    port_trace = 0;
    if (sh_n[0] != sh_n[1] || memcmp(sh_buf[0], sh_buf[1], sh_n[0] * 2) || memcmp(orig + 0x1d7, &mem[sg + 0x1d7], 0x800 - 0x1d7)) {
        int k = 0x1d7; while (k < 0x800 && orig[k] == mem[sg + k]) k++;
        fprintf(stderr, "[sndshadow] tick %ld differs: %d/%d port writes, state at %03x (orig %02x, C %02x)\n", tick, sh_n[1], sh_n[0], k, orig[k], mem[sg + k]);
    }
    return 1;
}
static void sound_isr(void) {
    sg = (u32)RSEG(0x18ae) << 4;
    if (W(0x1db)) {
        WW(0x1db, W(0x1db) - 1);
        if (!W(0x1db)) {
            R[rAX] = 0x0f00; emu_host_call(0x10); R[rAX] &= 0x00ff; emu_host_call(0x10);
            for (int i = 0; i < 0x21; i++) B(0x1dd + i) &= 0x7f;
            R[rAX] = 0x1301; R[rBX] = 0x0007; R[rCX] = 0x21; R[rDX] = 0; SG[sES] = RSEG(0x18ae); R[rBP] = 0x1dd; emu_host_call(0x10);
            R[rAX] = 0x4cff; emu_host_call(0x21);
        }
    }
    if (shadow_check()) {}
    else if (!B(0x1d8)) { B(0x1d8) = 1; pc_speaker(); tandy(); B(0x1d8) = 0; }
    if (!--B(0x1d7)) {
        B(0x1d7) = 0x0d;
        SG[sCS] = W(0x137a); emu_set_ip(W(0x1378));        // chain: the old handler returns from the interrupt
        return;
    }
    port_out(0x20, 0x20);
    u32 st = (u32)SG[sSS] << 4;
    u16 ip = rd16(st + R[rSP]), cs = rd16(st + (u16)(R[rSP] + 2)), fl = rd16(st + (u16)(R[rSP] + 4));
    R[rSP] = (u16)(R[rSP] + 6);
    SG[sCS] = cs; emu_set_ip(ip); emu_flags_set(fl);
}
static int h_sound_isr(void) { sound_isr(); return 0x7fff; }    // EMU_HOOK_JUMPED
void sound_isr_hooks(void) { emu_hook(0x18ae, 0x13ea, h_sound_isr, 1, "sound_isr"); }

// Self-test (script "selftest sound N" with -H, while a tune plays): N ticks, each run by the original handler and
// by this one from the same state; the port writes and the unit's state (0x1D7..0x6FF) must agree. The original's
// result is kept for the next tick.
extern void (*port_trace)(u16 port, u8 v);
static u16 trace_buf[4096]; static int trace_n;
static void trace(u16 port, u8 v) { if (trace_n < 4096) trace_buf[trace_n++] = (u16)(port << 8 | v); }
int sound_isr_selftest(int n) {
    sg = (u32)RSEG(0x18ae) << 4;
    static u8 before[0x800], after[0x800]; static u16 tr1[4096]; int bad = 0, busy = 0;
    int ownplayer = rd16(8 * 4) != 0x13ea;
    if (ownplayer) sound_timer_on();                         // install the player and play each effect in turn
    int effect = 0, dev = 0, since = 0, effects_done = 0;
    for (int i = 0; i < n; i++) {
        if (ownplayer && (!W(0x1ff) || ++since > 3000)) {        // nothing playing (or 3000 ticks of it): the next effect, PC speaker then Tandy
            since = 0; effects_done++;
            if (++effect > 40) { effect = 1; if (++dev > 1) break; }
            if (dev == 0) sound_start_pc((u16)effect); else sound_start_tandy((u16)effect);
            sg = (u32)RSEG(0x18ae) << 4;
        }
        memcpy(before, &mem[sg], sizeof before);
        u8 tickb[4]; memcpy(tickb, &mem[0x46c], 4);
        trace_n = 0; port_trace = trace; emu_run_int(8); port_trace = 0;
        int n1 = trace_n; memcpy(tr1, trace_buf, sizeof tr1); memcpy(after, &mem[sg], sizeof after);
        u8 ticka[4]; memcpy(ticka, &mem[0x46c], 4);
        memcpy(&mem[sg], before, sizeof before);
        trace_n = 0; port_trace = trace;
        if (!B(0x1d8)) { B(0x1d8) = 1; pc_speaker(); tandy(); B(0x1d8) = 0; }
        if (!--B(0x1d7)) B(0x1d7) = 0x0d; else port_out(0x20, 0x20);
        port_trace = 0;
        if (W(0x1ff)) busy++;
        if (n1 != trace_n || memcmp(tr1, trace_buf, n1 * 2) || memcmp(after + 0x1d7, &mem[sg + 0x1d7], 0x800 - 0x1d7)) {
            if (bad++ < 5) { int k = 0x1d7; while (k < 0x800 && after[k] == mem[sg + k]) k++; fprintf(stderr, "[selftest] sound tick %d: %d/%d port writes, state first differs at %03x\n", i, n1, trace_n, k); }
        }
        memcpy(&mem[sg], after, sizeof after); memcpy(&mem[0x46c], ticka, 4); (void)tickb;
    }
    if (ownplayer) { sound_start_pc(0); sound_start_tandy(0); sound_timer_off(); }
    fprintf(stderr, "[selftest] sound: %d ticks (%d with a tune playing, %d effects started), %d differ\n", n, busy, effects_done, bad);
    return bad;
}
