// Turbo Pascal's text files (System unit, 1AF8): Assign, Reset, Rewrite, Close, Read, ReadLn, Write, WriteLn, Eof,
// with the standard text driver whose four functions a TextRec points at. A text variable is TP's 256-byte TextRec
// in the game's memory: +0 handle, +2 mode (D7B0h closed, D7B1h input, D7B2h output, D7B3h append), +4 buffer size,
// +8 BufPos, +0Ah BufEnd, +0Ch buffer pointer, +10h/14h/18h/1Ch the Open, InOut, Flush and Close functions (far
// pointers into the runtime's code), +30h the name, +80h the 128-byte buffer. Errors go to InOutRes (DS:4426).
#include "rtl.h"
#include "hybrid.h"

#define IORES 0x4426
enum { TM_CLOSED = 0xd7b0, TM_INPUT = 0xd7b1, TM_OUTPUT = 0xd7b2, TM_INOUT = 0xd7b3 };
enum { DRV_OPEN = 0x060e, DRV_READ = 0x06ea, DRV_WRITE_FILE = 0x071a, DRV_WRITE_DEV = 0x073f, DRV_CLOSE = 0x075f };

static int dos21(u16 ax, u16 bx, u16 cx, u16 dx, u16 ds) {
    u16 sds = SG[sDS];
    R[rAX] = ax; R[rBX] = bx; R[rCX] = cx; R[rDX] = dx; SG[sDS] = ds;
    u16 f = emu_host_call(0x21);
    SG[sDS] = sds;
    return f & 1;
}
static void io_error(u16 e) { wr16(DS_LIN + IORES, e); }

// ---- the standard driver (each takes the TextRec and returns 0 or an error code) ----
// 1AF8:060E  Open: Reset opens for reading, Rewrite creates, Append opens read/write and drops a trailing ^Z (0690,
// not used by the game, not translated). No name means the standard handles 0 and 1. Output to a device is
// written through at every WriteLn (Flush = the device writer); to a file only when the buffer fills.
static u16 drv_open(farptr t) {
    u32 r = LIN(t); u16 mode = rd16(r + 2);
    wr16(r, 0);
    u16 ax = 0x3d00;
    if (mode != TM_INPUT) { ax = 0x3d02; wr16(r, 1); if (mode != TM_INOUT) ax = 0x3c02; }
    if (mem[r + 0x30]) { if (dos21(ax, R[rBX], 0, FP_OFF(t) + 0x30, FP_SEG(t))) return R[rAX]; wr16(r, R[rAX]); }
    u16 io = DRV_READ, fl_off = 0, fl_seg = 0;
    if (mode != TM_INPUT) {
        dos21(0x4400, rd16(r), 0, R[rDX], SG[sDS]);
        if (R[rDX] & 0x80) { io = DRV_WRITE_DEV; fl_off = DRV_WRITE_DEV; fl_seg = RSEG(0x1af8); }
        else { if (mode == TM_INOUT) fprintf(stderr, "[rtl] Append not modelled\n"); io = DRV_WRITE_FILE; }
        wr16(r + 2, TM_OUTPUT);
    }
    wr16(r + 0x14, io); wr16(r + 0x16, RSEG(0x1af8)); wr16(r + 0x18, fl_off); wr16(r + 0x1a, fl_seg);
    wr16(r + 0x1c, DRV_CLOSE); wr16(r + 0x1e, RSEG(0x1af8));
    return 0;
}
// 1AF8:06EA  fill the buffer (a read error leaves it empty, unreported).
static u16 drv_read(farptr t) {
    u32 r = LIN(t); farptr b = FARPTR(rd16(r + 0xe), rd16(r + 0xc));
    wr16(r + 0xa, dos21(0x3f00 | (R[rAX] & 0xff), rd16(r), rd16(r + 4), FP_OFF(b), FP_SEG(b)) ? 0 : R[rAX]);
    wr16(r + 8, 0);
    return 0;
}
// 1AF8:071A (file: a short write is error 65h) and 073F (device): write out BufPos bytes and empty the buffer.
static u16 drv_write(farptr t, int file) {
    u32 r = LIN(t); farptr b = FARPTR(rd16(r + 0xe), rd16(r + 0xc));
    u16 n = rd16(r + 8); wr16(r + 8, 0);
    if (dos21(0x4000 | (R[rAX] & 0xff), rd16(r), n, FP_OFF(b), FP_SEG(b))) return R[rAX];
    return (file && R[rAX] != n) ? 0x65 : 0;
}
// 1AF8:075F  Close: handles above 4 only.
static u16 drv_close(farptr t) {
    u16 h = P16(t, 0);
    if (h > 4 && dos21(0x3e00 | (R[rAX] & 0xff), h, R[rCX], R[rDX], SG[sDS])) return R[rAX];
    return 0;
}
// Call the driver function whose far pointer is at +off of the record.
static u16 drv_call(farptr t, u16 off) {
    u16 o = P16(t, off), s = P16(t, off + 2);
    if (s == RSEG(0x1af8)) switch (o) {
    case DRV_OPEN: return drv_open(t);
    case DRV_READ: return drv_read(t);
    case DRV_WRITE_FILE: return drv_write(t, 1);
    case DRV_WRITE_DEV: return drv_write(t, 0);
    case DRV_CLOSE: return drv_close(t);
    }
    if (s == RSEG(0x1a40)) switch (o) {                         // Crt's driver (rtl_crt.c)
    case 0x035c: return crt_drv_open(t);
    case 0x0441: return crt_drv_write(t);
    case 0x0394: return crt_drv_read(t);
    case 0x0472: return 0;
    }
    fprintf(stderr, "[rtl] text driver %04x:%04x not translated\n", s, o);
    return (u16)emu_pcall((u16)(s - RSEG(0x1000) + 0x1000), o, 2, FP_SEG(t), FP_OFF(t));
}
// 1AF8:05FD  ... and an error from it goes to InOutRes.
static void drv_do(farptr t, u16 off) { u16 e = drv_call(t, off); if (e) io_error(e); }

// 1AF8:04F7  Assign(t, name): a closed record with the standard Open function and its own buffer.
void text_assign(farptr t, farptr name) {
    u32 r = LIN(t);
    wr16(r, 0); wr16(r + 2, TM_CLOSED); wr16(r + 4, 0x80); wr16(r + 6, 0); wr16(r + 8, 0); wr16(r + 0xa, 0);
    wr16(r + 0xc, FP_OFF(t) + 0x80); wr16(r + 0xe, FP_SEG(t)); wr16(r + 0x10, DRV_OPEN); wr16(r + 0x12, RSEG(0x1af8));
    memset(&mem[r + 0x14], 0, 0x1c);
    u8 n = P8(name, 0); if (n > 0x4f) n = 0x4f;
    memcpy(&mem[r + 0x30], &mem[LIN(name) + 1], n); mem[r + 0x30 + n] = 0;
}

// 1AF8:05C1  Close(t): output is flushed through InOut first. Not open: error 67h.
void text_close(farptr t) {
    u16 m = P16(t, 2);
    if (m != TM_INPUT && m != TM_OUTPUT) { io_error(0x67); return; }
    if (m == TM_OUTPUT) drv_do(t, 0x14);
    drv_do(t, 0x1c);
    wr16(LIN(t) + 2, TM_CLOSED);
}

// 1AF8:0567 Reset / 056C Rewrite: an open file is closed first; one never assigned is error 66h.
static void text_open(farptr t, u16 mode) {
    u16 m = P16(t, 2);
    if (m == TM_INPUT || m == TM_OUTPUT) text_close(t);
    else if (m != TM_CLOSED) { io_error(0x66); return; }
    u32 r = LIN(t); wr16(r + 2, mode); wr16(r + 8, 0); wr16(r + 0xa, 0);
    u16 e = drv_call(t, 0x10);
    if (e) { io_error(e); wr16(r + 2, TM_CLOSED); }
}
void text_reset(farptr t) { text_open(t, TM_INPUT); }
void text_rewrite(farptr t) { text_open(t, TM_OUTPUT); }

// Reading and writing work on BufPos (kept in a local while busy, as the original keeps it in BX).
// 1AF8:0778 / 07B8: may this file be read / written? Not if an error is pending; a wrong mode is error 68h / 69h.
static int can_io(farptr t, u16 mode, u16 err) {
    if (G16(IORES)) return 0;
    if (P16(t, 2) != mode) { io_error(err); return 0; }
    return 1;
}
// 1AF8:07EB  hand the buffer to the driver's InOut (fill or empty it) and pick up the new BufPos.
static u16 inout(farptr t, u16 pos) { wr16(LIN(t) + 8, pos); drv_do(t, 0x14); return P16(t, 8); }
// 1AF8:079C  the character at pos, refilling when the buffer is used up; ^Z (1Ah) at the end of the file.
static int getc_at(farptr t, u16 *pos) {
    if (*pos == P16(t, 0xa)) { *pos = inout(t, *pos); if (*pos == P16(t, 0xa)) return -1; }
    return mem[LIN(FARPTR(P16(t, 0xe), P16(t, 0xc))) + *pos];
}
// 1AF8:07DC  store a character; a full buffer goes to InOut.
static void putc_at(farptr t, u16 *pos, u8 c) {
    mem[LIN(FARPTR(P16(t, 0xe), P16(t, 0xc))) + *pos] = c;
    if (++*pos == P16(t, 4)) *pos = inout(t, *pos);
}
// 1AF8:087B  after ReadLn/WriteLn: the Flush function, if there is one and no error is pending.
static void flush_after(farptr t) { if (P16(t, 0x1a) && !G16(IORES)) drv_do(t, 0x18); }

// 1AF8:081C  ReadLn(t): skip to just past the next CR (and an LF after it), or to the end.
void text_readln(farptr t) {
    u16 pos = P16(t, 8);
    if (can_io(t, TM_INPUT, 0x68)) for (;;) {
        int c = getc_at(t, &pos); if (c < 0 || c == 0x1a) break;
        pos++;
        if (c == 0xd) { c = getc_at(t, &pos); if (c == 0xa) pos++; break; }
    }
    wr16(LIN(t) + 8, pos);
    flush_after(t);
}
// 1AF8:084A  WriteLn(t): CR LF.
void text_writeln(farptr t) {
    u16 pos = P16(t, 8);
    if (can_io(t, TM_OUTPUT, 0x69)) { putc_at(t, &pos, 0xd); putc_at(t, &pos, 0xa); }
    wr16(LIN(t) + 8, pos);
    flush_after(t);
}
// 1AF8:0898  Read(t, c): a character, ^Z at the end (not consumed).
u8 text_read_char(farptr t) {
    u16 pos = P16(t, 8); int c = 0x1a;
    if (can_io(t, TM_INPUT, 0x68)) { c = getc_at(t, &pos); if (c < 0) c = 0x1a; else pos++; }
    wr16(LIN(t) + 8, pos);
    return (u8)c;
}
// 1AF8:08E1  Read(t, s): up to max characters, stopping before a CR or ^Z.
void text_read_str(farptr t, farptr s, u16 max) {
    u16 pos = P16(t, 8), n = 0; u32 d = LIN(s);
    if (can_io(t, TM_INPUT, 0x68)) for (;;) {
        int c = getc_at(t, &pos); if (c < 0 || c == 0xd || c == 0x1a) break;
        pos++; mem[d + ++n] = (u8)c;
        if (n == max) break;
    }
    mem[d] = (u8)n;
    wr16(LIN(t) + 8, pos);
}
// 1AF8:0919  Write(t, s:width): right-aligned in width.
void text_write_str(farptr t, farptr s, s16 width) {
    u16 pos = P16(t, 8);
    if (can_io(t, TM_OUTPUT, 0x69)) {
        u8 n = P8(s, 0);
        for (s16 k = width - n; k > 0; k--) putc_at(t, &pos, ' ');
        for (u16 i = 1; i <= n; i++) putc_at(t, &pos, P8(s, i));
    }
    wr16(LIN(t) + 8, pos);
}
// 1AF8:09AF  Write(t, v:width) for a longint (digits by 1AF8:1254).
void text_write_long(farptr t, s32 v, s16 width) {
    char b[16]; int n = 0; u32 m = v < 0 ? 0u - (u32)v : (u32)v;
    char d[12]; int k = 0; do { d[k++] = (char)('0' + m % 10); m /= 10; } while (m);
    if (v < 0) b[n++] = '-';
    while (k) b[n++] = d[--k];
    u16 pos = P16(t, 8);
    if (can_io(t, TM_OUTPUT, 0x69)) {
        for (s16 k2 = width - n; k2 > 0; k2--) putc_at(t, &pos, ' ');
        for (int i = 0; i < n; i++) putc_at(t, &pos, (u8)b[i]);
    }
    wr16(LIN(t) + 8, pos);
}
// 1AF8:0A04  Eof(t): the next character is ^Z or there is none. A file that cannot be read is not at its end.
int text_eof(farptr t) {
    u16 pos = P16(t, 8); int r = 0;
    if (can_io(t, TM_INPUT, 0x68)) { int c = getc_at(t, &pos); r = c < 0 || c == 0x1a; }
    wr16(LIN(t) + 8, pos);
    return r;
}

static int h_assign(void) { text_assign(FARG(2), FARG(0)); return 8; }
static int h_reset(void) { text_reset(FARG(0)); return 4; }
static int h_rewrite(void) { text_rewrite(FARG(0)); return 4; }
static int h_close(void) { text_close(FARG(0)); return 4; }
static int h_readln(void) { text_readln(FARG(0)); return 4; }
static int h_writeln(void) { text_writeln(FARG(0)); return 4; }
static int h_read_char(void) { RET8(text_read_char(FARG(0))); return 0; }           // the file stays on the stack
static int h_read_str(void) { text_read_str(FARG(3), FARG(1), ARG(0)); return 6; }
static int h_write_str(void) { text_write_str(FARG(3), FARG(1), (s16)ARG(0)); return 6; }
static int h_write_long(void) { text_write_long(FARG(3), (s32)(((u32)ARG(2) << 16) | ARG(1)), (s16)ARG(0)); return 6; }
static int h_eof(void) { RET8(text_eof(FARG(0))); return 4; }
static int h_drv_open(void) { RET16(drv_open(FARG(0))); return 4; }
static int h_drv_read(void) { RET16(drv_read(FARG(0))); return 4; }
static int h_drv_write_file(void) { RET16(drv_write(FARG(0), 1)); return 4; }
static int h_drv_write_dev(void) { RET16(drv_write(FARG(0), 0)); return 4; }
static int h_drv_close(void) { RET16(drv_close(FARG(0))); return 4; }
void rtl_text_hooks(void) {
    emu_hook(0x1af8, 0x04f7, h_assign, 1, "text_assign");
    emu_hook(0x1af8, 0x0567, h_reset, 1, "text_reset");
    emu_hook(0x1af8, 0x056c, h_rewrite, 1, "text_rewrite");
    emu_hook(0x1af8, 0x05c1, h_close, 1, "text_close");
    emu_hook(0x1af8, 0x081c, h_readln, 1, "text_readln");
    emu_hook(0x1af8, 0x084a, h_writeln, 1, "text_writeln");
    emu_hook(0x1af8, 0x0898, h_read_char, 1, "text_read_char");
    emu_hook(0x1af8, 0x08e1, h_read_str, 1, "text_read_str");
    emu_hook(0x1af8, 0x0919, h_write_str, 1, "text_write_str");
    emu_hook(0x1af8, 0x09af, h_write_long, 1, "text_write_long");
    emu_hook(0x1af8, 0x0a04, h_eof, 1, "text_eof");
    emu_hook(0x1af8, 0x060e, h_drv_open, 1, "text_drv_open");
    emu_hook(0x1af8, 0x06ea, h_drv_read, 1, "text_drv_read");
    emu_hook(0x1af8, 0x071a, h_drv_write_file, 1, "text_drv_write_file");
    emu_hook(0x1af8, 0x073f, h_drv_write_dev, 1, "text_drv_write_dev");
    emu_hook(0x1af8, 0x075f, h_drv_close, 1, "text_drv_close");
}
