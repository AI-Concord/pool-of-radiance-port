// 1000:0006  The main program. Turbo Pascal calls each unit's initialisation in turn (System, Crt, Dos, the sound
// unit, ... the game's own units; in the original most of these go through the overlay stubs), then the program
// body: 2A3D:002D; unless started as START STING, the title pages (1C88:0627) and the code wheel (1D09:00FD); then
// 34FB:0161, the view colours (491C:0155 with 11, 0, 7, 6), the game itself (1D3F:32F8) and its ending
// (1899:0000), and Halt(0). With this the whole program runs as C from its first instruction.
#include "rtl.h"
#include "hybrid.h"

static const u16 unit_inits[][2] = {
    {0x1af8, 0x0000}, {0x1a40, 0x0000}, {0x1a31, 0x0000}, {0x18ae, 0x01d2}, {0x1899, 0x013c}, {0x1802, 0x0968},
    {0x1709, 0x0f82}, {0x1464, 0x2a4a}, {0x142b, 0x00a4}, {0x4ca0, 0x02ad}, {0x13f1, 0x0367}, {0x4c90, 0x00f7},
    {0x13ed, 0x0000}, {0x4c2b, 0x0640}, {0x4afb, 0x12fb}, {0x4a3d, 0x0bd9}, {0x13da, 0x0000}, {0x491c, 0x1206},
    {0x489f, 0x07c0}, {0x4876, 0x0283}, {0x484b, 0x02a0}, {0x4726, 0x1245}, {0x10ba, 0x309e}, {0x4592, 0x1932},
    {0x454b, 0x045e}, {0x41cf, 0x37b0}, {0x3fa9, 0x2256}, {0x3ec2, 0x0e60}, {0x3c10, 0x2b0d}, {0x3b80, 0x08f6},
    {0x3941, 0x23e0}, {0x34fb, 0x4454}, {0x32fd, 0x1fd9}, {0x31f2, 0x10a7}, {0x2e2a, 0x3c6d}, {0x2a9f, 0x38a9},
    {0x2a3d, 0x0615}, {0x2843, 0x1f8d}, {0x26b0, 0x1922}, {0x2588, 0x127a}, {0x2366, 0x2215}, {0x22ec, 0x0798},
    {0x2184, 0x1671}, {0x2090, 0x0f32}, {0x1d3f, 0x3508}, {0x1d09, 0x0359}, {0x1c88, 0x0809}, {0x1c7c, 0x00ac},
};
void game_main(void) {
    for (unsigned i = 0; i < sizeof unit_inits / sizeof unit_inits[0]; i++) emu_pcall(unit_inits[i][0], unit_inits[i][1], 0);
    farptr p = scratch_push(0x100);                     // the body's local string (BP-100h)
    emu_pcall(0x2a3d, 0x002d, 0);
    rtl_paramstr(p, 1);
    u32 sting = LIN(FARPTR(RSEG(0x1000), 0));            // the string constant "STING" at the start of the code
    if (memcmp(&mem[LIN(p)], &mem[sting], mem[sting] + 1)) { emu_pcall(0x1c88, 0x0627, 0); emu_pcall(0x1d09, 0x00fd, 0); }
    emu_pcall(0x34fb, 0x0161, 0);
    emu_pcall(0x491c, 0x0155, 4, 0x0b, 0, 7, 6);
    emu_pcall(0x1d3f, 0x32f8, 0);
    emu_pcall(0x1899, 0x0000, 0);
    scratch_pop(0x100);
    rtl_halt(0);
}
static int h_main(void) { game_main(); return 0; }
void main_hooks(void) { emu_hook(0x1000, 0x0006, h_main, 1, "game_main"); }
