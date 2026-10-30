// Pool of Radiance, native port: common definitions.
//
// The game's state lives where the original kept it: in an image of the DOS program's memory.
// Globals are addressed by their offset in the original data segment (G8/G16/G32), and the many
// far pointers the game stores (seg:off pairs) are followed with FARPTR. Original addresses are
// given as SEG:OFF in the flat image as shown by the Ghidra listing (re/ghidra/out).
#ifndef POOLRAD_H
#define POOLRAD_H
#include <stdint.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int8_t s8; typedef int16_t s16; typedef int32_t s32;
typedef struct { u16 ax, bx, dx; } real48;      // a Turbo Pascal Real as the runtime holds it in DX:BX:AX

extern u8 mem[0x110000];                 // conventional memory of the DOS machine
#define LOAD_SEG 0x0120                  // where the program image is loaded
#define RSEG(gseg) ((u16)((gseg) - 0x1000 + LOAD_SEG))      // Ghidra segment -> run-time segment
#define DATA_SEG RSEG(0x4ccc)            // the Turbo Pascal data segment
#define DS_LIN ((u32)DATA_SEG << 4)

static inline u16 rd16(u32 a) { return (u16)(mem[a] | (mem[a + 1] << 8)); }
static inline u32 rd32(u32 a) { return rd16(a) | ((u32)rd16(a + 2) << 16); }
static inline void wr16(u32 a, u16 v) { mem[a] = (u8)v; mem[a + 1] = (u8)(v >> 8); }
static inline void wr32(u32 a, u32 v) { wr16(a, (u16)v); wr16(a + 2, (u16)(v >> 16)); }

typedef u32 farptr;                      // seg:off packed as (seg << 16) | off; 0 is nil
#define FARPTR(seg, off) (((u32)(u16)(seg) << 16) | (u16)(off))
#define FP_SEG(p) ((u16)((p) >> 16))
#define FP_OFF(p) ((u16)(p))
#define LIN(p) (((u32)FP_SEG(p) << 4) + FP_OFF(p))          // linear address of a far pointer
#define P8(p, o) mem[LIN(p) + (o)]
#define P16(p, o) rd16(LIN(p) + (o))
#define P32(p, o) rd32(LIN(p) + (o))
#define SETP16(p, o, v) wr16(LIN(p) + (o), (v))
#define SETP32(p, o, v) wr32(LIN(p) + (o), (v))

#define G8(off) mem[DS_LIN + (off)]      // byte global at DS:off
#define G16(off) rd16(DS_LIN + (off))
#define G32(off) rd32(DS_LIN + (off))
#define SETG16(off, v) wr16(DS_LIN + (off), (v))
#define SETG32(off, v) wr32(DS_LIN + (off), (v))
#endif
