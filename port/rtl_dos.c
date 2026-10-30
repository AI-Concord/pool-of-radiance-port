// Turbo Pascal's Dos and Crt units, the parts the game uses: Intr and GetTime (1A32), Delay, KeyPressed and
// ReadKey (1A40). The BIOS and DOS services themselves are the platform layer (emu_host_call); these routines
// only marshal registers exactly as the originals do.
#include <stdlib.h>
#include "rtl.h"
#include "hybrid.h"

// 1A32:000B  Intr(n, regs): the record's AX BX CX DX BP SI DI DS ES are loaded and interrupt n's handler is entered
// as INT would, with the caller's flags in its return frame; afterwards all ten words, Flags last, are written back.
// The original leaves ES:DI just past the record, AX = the flags, and DF clear; BP and DS are restored.
void dos_intr(u8 n, farptr regs) {
    u32 r = LIN(regs);
    u16 bp = R[rBP], ds = SG[sDS], f;
    R[rAX] = rd16(r); R[rBX] = rd16(r + 2); R[rCX] = rd16(r + 4); R[rDX] = rd16(r + 6); R[rBP] = rd16(r + 8);
    R[rSI] = rd16(r + 10); R[rDI] = rd16(r + 12); SG[sDS] = rd16(r + 14); SG[sES] = rd16(r + 16);
    if (rd16(n * 4 + 2) == 0xf000 && rd16(n * 4) == n * 4) f = emu_host_call(n);    // the vector still points at the BIOS/DOS
    else { emu_run_int(n); f = emu_flags_get(); }                                  // a handler the game installed
    wr16(r, R[rAX]); wr16(r + 2, R[rBX]); wr16(r + 4, R[rCX]); wr16(r + 6, R[rDX]); wr16(r + 8, R[rBP]);
    wr16(r + 10, R[rSI]); wr16(r + 12, R[rDI]); wr16(r + 14, SG[sDS]); wr16(r + 16, SG[sES]); wr16(r + 18, f);
    SG[sES] = FP_SEG(regs); R[rDI] = FP_OFF(regs) + 20; R[rAX] = f;
    emu_flags_set(f & ~0x400);
    R[rBP] = bp; SG[sDS] = ds;
}

// 1A32:00A2  GetTime(h, m, s, hs): DOS function 2Ch, each part stored as a word. AX is left as the hour.
void dos_get_time(farptr h, farptr m, farptr s, farptr hs) {
    R[rAX] = 0x2c00 | (R[rAX] & 0xff);
    emu_host_call(0x21);
    wr16(LIN(hs), R[rDX] & 0xff); wr16(LIN(s), R[rDX] >> 8); wr16(LIN(m), R[rCX] & 0xff); wr16(LIN(h), R[rCX] >> 8);
    R[rAX] = R[rCX] >> 8; SG[sES] = FP_SEG(h); R[rDI] = FP_OFF(h) + 2;
}

// 1A40:02FD  KeyPressed: a scan code held back by ReadKey (DS:8501) counts; otherwise BIOS function 1.
int crt_key_pressed(void) {
    if (G8(0x8501)) { R[rAX] = (R[rAX] & 0xff00) | 1; return 1; }
    R[rAX] = 0x0100 | (R[rAX] & 0xff);
    u16 f = emu_host_call(0x16);
    int k = !(f & 0x40);
    R[rAX] = (R[rAX] & 0xff00) | k;
    return k;
}

// 1A40:030F  ReadKey: the held-back scan code if any; otherwise BIOS function 0. An extended key gives 0 now and its
// scan code next time. A key of 0 with scan code 0 (Ctrl-Break) gives 3 after Crt's break check (1A40:0143), which
// acts only on a break flagged by its INT 1Bh handler (DS:8502); this emulator never raises one.
u8 crt_read_key(void) {
    u8 al = G8(0x8501); G8(0x8501) = 0;
    if (al) { R[rAX] = (R[rAX] & 0xff00) | al; return al; }
    R[rAX] &= 0x00ff;
    emu_host_call(0x16);
    al = (u8)R[rAX];
    if (al) return al;
    u8 ah = R[rAX] >> 8; G8(0x8501) = ah;
    if (ah) return 0;
    if (G8(0x8502)) fprintf(stderr, "[crt] Ctrl-Break flagged: not modelled\n");
    R[rAX] = (R[rAX] & 0xff00) | 3; return 3;
}

// 1A40:029C  Delay(ms). The original spins a loop calibrated at start-up (DS:84FC per millisecond).
void rtl_delay(u16 ms) { emu_delay_count(); if (ms) emu_idle_ms(ms); R[rDX] = 0; }

static int h_intr(void) { dos_intr((u8)ARG(2), FARG(0)); return 6; }
static int h_get_time(void) { dos_get_time(FARG(6), FARG(4), FARG(2), FARG(0)); return 16; }
static int h_key_pressed(void) { crt_key_pressed(); return 0; }
static int h_read_key(void) { crt_read_key(); return 0; }
static int h_delay(void) { rtl_delay(ARG(0)); return 2; }
void rtl_dos_hooks(void) {
    emu_hook(0x1a32, 0x000b, h_intr, 1, "dos_intr");
    emu_hook(0x1a32, 0x00a2, h_get_time, 1, "dos_get_time");
    emu_hook(0x1a40, 0x02fd, h_key_pressed, 1, "crt_key_pressed");
    emu_hook(0x1a40, 0x030f, h_read_key, 1, "crt_read_key");
    emu_hook(0x1a40, 0x029c, h_delay, 1, "crt_delay");
}
