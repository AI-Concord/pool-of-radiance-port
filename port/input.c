// Keyboard (original unit at 1899, resident).
#include "poolrad.h"
#include "rtl.h"
#include "input.h"

// 1899:0059  Wait for a key and return its character (0 for an extended key, whose scan code is
// then returned by the next call). Also handles the keys that work everywhere:
//   Ctrl-S  sound off / back on (DS:5372 is the sound mode: 0 PC speaker, 1 Tandy, 2 off)
// and, only when the game was started with the argument STING (a developer switch left in the
// shipped program):
//   Ctrl-D  toggle the debug trace flag DS:49F7      Ctrl-C  quit at once
// Finally, anything typed ahead is thrown away except the most recent key.
u8 input_get_key(void) {
    u8 k = crt_read_key();
    if (k == 0x13) {
        if (G8(0x5372) == 2) {
            if (G8(0x5373) <= 1) { G8(0x5372) = G8(0x5373); sound_resume(); }
        } else {
            G8(0x5373) = G8(0x5372); sound_play(G16(0x2612)); G8(0x5372) = 2;
        }
    }
    if (!strcmp(plat_param1(), "STING")) {
        if (k == 4) G8(0x49f7) = G8(0x49f7) == 0;
        if (k == 3) game_quit();
    }
    if (k != 0) while (crt_key_pressed()) k = crt_read_key();
    return k;
}

// 1899:0122  If a key is waiting, read it (so Ctrl-S and friends work during animations).
void input_poll(void) { if (crt_key_pressed()) input_get_key(); }

void input_wait_key(void) { input_get_key(); }
