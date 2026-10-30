// Turbo Pascal's untyped files (System unit, 1AF8): Assign, Reset, Rewrite, Close, BlockRead, BlockWrite, Seek, Eof,
// Erase, MkDir and IOResult. A file variable is TP's 128-byte FileRec in the game's memory (+0 handle, +2 mode
// D7B0h closed / D7B3h open, +4 record size, +30h the name, zero-terminated); errors go to InOutRes (DS:4426).
// DOS itself is the platform layer (emu_host_call), so these do exactly what the originals ask of it.
#include "rtl.h"
#include "hybrid.h"

#define IORES 0x4426
#define FM_CLOSED 0xd7b0
#define FM_INOUT 0xd7b3

// INT 21h with the given registers (DS:DX for names and buffers); returns the carry flag.
static int dos21(u16 ax, u16 bx, u16 cx, u16 dx, u16 ds) {
    u16 sds = SG[sDS];
    R[rAX] = ax; R[rBX] = bx; R[rCX] = cx; R[rDX] = dx; SG[sDS] = ds;
    u16 f = emu_host_call(0x21);
    SG[sDS] = sds;
    return f & 1;
}
static int is_open(farptr f) { if (P16(f, 2) == FM_INOUT) return 1; wr16(DS_LIN + IORES, 0x67); return 0; }

// 1AF8:04A2  IOResult: the last error, cleared.
u16 rtl_ioresult(void) { u16 r = G16(IORES); wr16(DS_LIN + IORES, 0); return r; }

// 1AF8:13E2  Assign(f, name): the record cleared (closed), the name copied (at most 79 characters).
void file_assign(farptr f, farptr name) {
    u32 r = LIN(f);
    wr16(r, 0); wr16(r + 2, FM_CLOSED); memset(&mem[r + 4], 0, 0x2c);
    u8 n = P8(name, 0); if (n > 0x4f) n = 0x4f;
    memcpy(&mem[r + 0x30], &mem[LIN(name) + 1], n); mem[r + 0x30 + n] = 0;
}

// 1AF8:1491  Close(f). Handles 0-4 (the standard devices) are left open.
void file_close(farptr f) {
    if (!is_open(f)) return;
    u16 h = P16(f, 0);
    if (h > 4 && dos21(0x3e00 | (R[rAX] & 0xff), h, R[rCX], R[rDX], SG[sDS])) wr16(DS_LIN + IORES, R[rAX]);
    wr16(LIN(f) + 2, FM_CLOSED);
}

// 1AF8:1410 Reset / 1AF8:1419 Rewrite (f, recsize): an open file is closed first; one not assigned is error 66h.
// An empty name means standard input (Reset) or output (Rewrite). Reset opens with FileMode (DS:442C).
static void file_open(farptr f, u16 ax, u16 std, u16 recsize) {
    if (P16(f, 2) == FM_INOUT) file_close(f);
    else if (P16(f, 2) != FM_CLOSED) { wr16(DS_LIN + IORES, 0x66); return; }
    u16 h = std;
    if (P8(f, 0x30)) {
        if (dos21(ax, R[rBX], 0, FP_OFF(f) + 0x30, FP_SEG(f))) { wr16(DS_LIN + IORES, R[rAX]); return; }
        h = R[rAX];
    }
    wr16(LIN(f) + 2, FM_INOUT); wr16(LIN(f) + 0, h); wr16(LIN(f) + 4, recsize);
}
void file_reset(farptr f, u16 recsize) { file_open(f, 0x3d00 | G8(0x442c), 0, recsize); }
void file_rewrite(farptr f, u16 recsize) { file_open(f, 0x3c00, 1, recsize); }

// 1AF8:14FB BlockRead / 1AF8:1502 BlockWrite (f, buf, count, result): count records. With a result variable the
// number done is stored there; without one, falling short is error 64h (read) or 65h (write).
static void file_block(farptr f, farptr buf, u16 count, farptr result, u8 fn, u16 shortfall) {
    u16 n = 0;
    if (!is_open(f)) goto none;
    if (count) {
        u16 rs = P16(f, 4);
        if (dos21((u16)(fn << 8) | (R[rAX] & 0xff), P16(f, 0), (u16)(count * rs), FP_OFF(buf), FP_SEG(buf))) { wr16(DS_LIN + IORES, R[rAX]); goto none; }
        n = (u16)(R[rAX] / rs);
    }
    if (result) wr16(LIN(result), n);
    else if (n != count) wr16(DS_LIN + IORES, shortfall);
    return;
none:
    if (result) wr16(LIN(result), 0);
}
void file_block_read(farptr f, farptr buf, u16 count, farptr result) { file_block(f, buf, count, result, 0x3f, 0x64); }
void file_block_write(farptr f, farptr buf, u16 count, farptr result) { file_block(f, buf, count, result, 0x40, 0x65); }

// 1AF8:1563  Seek(f, n): to record n (the byte offset taken modulo 2^32).
void file_seek(farptr f, s32 n) {
    if (!is_open(f)) return;
    u32 b = (u32)n * P16(f, 4);
    if (dos21(0x4200, P16(f, 0), (u16)(b >> 16), (u16)b, SG[sDS])) wr16(DS_LIN + IORES, R[rAX]);
}

// 1AF8:15C5  Eof(f): the position (asked of DOS, with a seek to the end and back) is at or past the size.
int file_eof(farptr f) {
    if (!is_open(f)) return 0;
    u16 h = P16(f, 0);
    dos21(0x4201, h, 0, 0, SG[sDS]); u32 pos = ((u32)R[rDX] << 16) | R[rAX];
    dos21(0x4202, h, 0, 0, SG[sDS]); u32 size = ((u32)R[rDX] << 16) | R[rAX];
    dos21(0x4200, h, (u16)(pos >> 16), (u16)pos, SG[sDS]);
    return pos >= size;
}

// 1AF8:161F  Erase(f).
void file_erase(farptr f) { if (dos21(0x4100 | (R[rAX] & 0xff), R[rBX], R[rCX], FP_OFF(f) + 0x30, FP_SEG(f))) wr16(DS_LIN + IORES, R[rAX]); }

// 1AF8:17D6  MkDir(s): the name (at most 127 characters) zero-terminated in a buffer on the stack.
void file_mkdir(farptr s) {
    farptr b = scratch_push(0x80);
    u8 n = P8(s, 0); if (n > 0x7f) n = 0x7f;
    memcpy(&mem[LIN(b)], &mem[LIN(s) + 1], n); mem[LIN(b) + n] = 0;
    if (dos21(0x3900 | (R[rAX] & 0xff), R[rBX], R[rCX], FP_OFF(b), FP_SEG(b))) wr16(DS_LIN + IORES, R[rAX]);
    scratch_pop(0x80);
}

static int h_ioresult(void) { RET16(rtl_ioresult()); return 0; }
static int h_assign(void) { file_assign(FARG(2), FARG(0)); return 8; }
static int h_close(void) { file_close(FARG(0)); return 4; }
static int h_reset(void) { file_reset(FARG(1), ARG(0)); return 6; }
static int h_rewrite(void) { file_rewrite(FARG(1), ARG(0)); return 6; }
static int h_block_read(void) { file_block_read(FARG(5), FARG(3), ARG(2), FARG(0)); return 14; }
static int h_block_write(void) { file_block_write(FARG(5), FARG(3), ARG(2), FARG(0)); return 14; }
static int h_seek(void) { file_seek(FARG(2), (s32)(((u32)ARG(1) << 16) | ARG(0))); return 8; }
static int h_eof(void) { RET8(file_eof(FARG(0))); return 4; }
static int h_erase(void) { file_erase(FARG(0)); return 4; }
static int h_mkdir(void) { file_mkdir(FARG(0)); return 4; }
void rtl_file_hooks(void) {
    emu_hook(0x1af8, 0x04a2, h_ioresult, 1, "rtl_ioresult");
    emu_hook(0x1af8, 0x13e2, h_assign, 1, "file_assign");
    emu_hook(0x1af8, 0x1491, h_close, 1, "file_close");
    emu_hook(0x1af8, 0x1410, h_reset, 1, "file_reset");
    emu_hook(0x1af8, 0x1419, h_rewrite, 1, "file_rewrite");
    emu_hook(0x1af8, 0x14fb, h_block_read, 1, "file_block_read");
    emu_hook(0x1af8, 0x1502, h_block_write, 1, "file_block_write");
    emu_hook(0x1af8, 0x1563, h_seek, 1, "file_seek");
    emu_hook(0x1af8, 0x15c5, h_eof, 1, "file_eof");
    emu_hook(0x1af8, 0x161f, h_erase, 1, "file_erase");
    emu_hook(0x1af8, 0x17d6, h_mkdir, 1, "file_mkdir");
}
