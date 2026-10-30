#include <string.h>
#include <stdio.h>
#include "ega.h"

uint8_t ega_plane[4][0x10000];
uint8_t ega_latch[4];
uint8_t ega_gc[16];
uint8_t ega_seq[8];
uint8_t ega_palette[16] = {0, 1, 2, 3, 4, 5, 0x14, 7, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};
int ega_mode = 3;

void ega_set_mode(int mode) {
    ega_mode = mode;
    memset(ega_plane, 0, sizeof ega_plane);
    memset(ega_gc, 0, sizeof ega_gc);
    memset(ega_seq, 0, sizeof ega_seq);
    ega_gc[8] = 0xff; ega_seq[2] = 0x0f; ega_gc[7] = 0x0f;
}

uint8_t ega_read(uint32_t off) {
    off &= 0xffff;
    for (int p = 0; p < 4; p++) ega_latch[p] = ega_plane[p][off];
    if (ega_gc[5] & 8) {                    // read mode 1: colour compare
        uint8_t r = 0xff;
        for (int p = 0; p < 4; p++) if (ega_gc[7] & (1 << p)) {
            uint8_t cmp = (ega_gc[2] & (1 << p)) ? 0xff : 0x00;
            r &= (uint8_t)~(ega_latch[p] ^ cmp);
        }
        return r;
    }
    return ega_latch[ega_gc[4] & 3];
}

static uint8_t rotr8(uint8_t v, int n) { n &= 7; return (uint8_t)((v >> n) | (v << (8 - n))); }
static uint8_t logic(uint8_t v, uint8_t l) {
    switch ((ega_gc[3] >> 3) & 3) { case 0: return v; case 1: return v & l; case 2: return v | l; default: return v ^ l; }
}

void ega_write(uint32_t off, uint8_t val) {
    uint8_t mask = ega_gc[8], out[4]; int wm = ega_gc[5] & 3;
    off &= 0xffff;
    if (wm == 0) {
        uint8_t v = rotr8(val, ega_gc[3] & 7);
        for (int p = 0; p < 4; p++) {
            uint8_t d = v;
            if (ega_gc[1] & (1 << p)) d = (ega_gc[0] & (1 << p)) ? 0xff : 0x00;
            d = logic(d, ega_latch[p]);
            out[p] = (d & mask) | (ega_latch[p] & ~mask);
        }
    } else if (wm == 1) {
        for (int p = 0; p < 4; p++) out[p] = ega_latch[p];
    } else if (wm == 2) {
        for (int p = 0; p < 4; p++) {
            uint8_t d = (val & (1 << p)) ? 0xff : 0x00;
            d = logic(d, ega_latch[p]);
            out[p] = (d & mask) | (ega_latch[p] & ~mask);
        }
    } else {
        uint8_t v = rotr8(val, ega_gc[3] & 7), m = mask & v;
        for (int p = 0; p < 4; p++) {
            uint8_t d = (ega_gc[0] & (1 << p)) ? 0xff : 0x00;
            out[p] = (d & m) | (ega_latch[p] & ~m);
        }
    }
    for (int p = 0; p < 4; p++) if (ega_seq[2] & (1 << p)) ega_plane[p][off] = out[p];
}

static uint8_t seq_idx, gc_idx;
void ega_out(uint16_t port, uint8_t v) {
    switch (port) {
    case 0x3c4: seq_idx = v & 7; break;
    case 0x3c5: ega_seq[seq_idx] = v; break;
    case 0x3ce: gc_idx = v & 15; break;
    case 0x3cf: ega_gc[gc_idx] = v; break;
    }
}
uint8_t ega_in(uint16_t port) { return port == 0x3cf ? ega_gc[gc_idx] : port == 0x3c5 ? ega_seq[seq_idx] : 0xff; }

// Snapshot support for the test harness: write or read the whole state of the card model.
void ega_state_io(void *file, int write) {
    FILE *f = (FILE *)file;
#define IO(x) do { if (write) fwrite(&(x), sizeof(x), 1, f); else if (fread(&(x), sizeof(x), 1, f) != 1) return; } while (0)
    IO(ega_plane); IO(ega_latch); IO(ega_gc); IO(ega_seq); IO(ega_palette); IO(ega_mode); IO(seq_idx); IO(gc_idx);
#undef IO
}
