// Glue between natively ported routines and the reference emulator (hybrid build only).
// A ported routine is registered at the address of the original; the emulator calls the C version
// instead. Arguments are read from the emulated stack, results go back in AX (DX:AX for 32 bits).
#ifndef HYBRID_H
#define HYBRID_H
#include "poolrad.h"
extern u16 R[8], SG[4];
enum { rAX, rCX, rDX, rBX, rSP, rBP, rSI, rDI }; enum { sES, sCS, sSS, sDS };
typedef int (*native_fn)(void);
void emu_hook(u16 gseg, u16 off, native_fn fn, int far_ret, const char *name);
u32 emu_pcall(u16 gseg, u16 off, int nargs, ...);
// Word i of the arguments of a far Pascal routine at its entry; i = 0 is the LAST parameter pushed.
static inline u16 ARG(int i) { return rd16(((u32)SG[sSS] << 4) + (u16)(R[rSP] + 4 + 2 * i)); }
static inline u16 NARG(int i) { return rd16(((u32)SG[sSS] << 4) + (u16)(R[rSP] + 2 + 2 * i)); }   // near routine
#define FARG(i) FARPTR(ARG((i) + 1), ARG(i))                                                    // far pointer argument
#define RET8(v) (R[rAX] = (R[rAX] & 0xff00) | (u8)(v))
#define RET16(v) (R[rAX] = (u16)(v))
#define RET32(v) do { u32 v_ = (v); R[rAX] = (u16)v_; R[rDX] = (u16)(v_ >> 16); } while (0)
#endif
