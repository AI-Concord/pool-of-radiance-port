// Virtual EGA adapter: four bit planes plus the graphics-controller state the
// game's drawing code programs directly. The front end converts planes to RGBA.
#ifndef EGA_H
#define EGA_H
#include <stdint.h>
extern uint8_t ega_plane[4][0x10000];
extern uint8_t ega_latch[4];
extern uint8_t ega_gc[16];          // graphics controller registers (port 3CE/3CF)
extern uint8_t ega_seq[8];          // sequencer registers (port 3C4/3C5)
extern uint8_t ega_palette[16];     // 6-bit rgbRGB colour per attribute
extern int ega_mode;                // BIOS video mode (0x0D = 320x200x16)
void ega_set_mode(int mode);
void ega_state_io(void *file, int write);   // snapshot support (test harness)
uint8_t ega_read(uint32_t off);
void ega_write(uint32_t off, uint8_t val);
static inline void ega_gcw(uint16_t ax) { ega_gc[ax & 15] = (uint8_t)(ax >> 8); }   // OUT 3CEh, AX
void ega_out(uint16_t port, uint8_t val);   // sequencer / graphics controller ports 3C4h-3CFh
#endif
