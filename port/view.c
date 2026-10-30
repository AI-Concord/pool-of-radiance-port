#include <stdlib.h>
#include <stdio.h>
// The exploring view (original overlay unit at 484B): what is shown in the picture window while
// the party walks around.
#include "poolrad.h"
#include "combat.h"
#include "gfx.h"
#include "rtl.h"
#include "view.h"
#include "map.h"

#define GAME G32(0x49d2)

// Scripts choose the sky and floor through two values (script addresses C05F and C059), which
// still hold the first version's colour codes. These three nested routines turn them into EGA colours.
static u8 band1_color(u8 code) { return (code == 0x08 || code == 0x41) ? 0 : 15; }          // 484B:00C7
static u8 band2_color(u8 code) { return code == 0x08 ? 8 : 7; }                           // 484B:00FB
static u8 floor_color(u8 code) { return code == 0x0b ? 8 : code == 0x06 ? 1 : 6; }          // 484B:012F

// 491C:0155  Set the 3D view's four background colours: sky, the two bands at the horizon, floor.
void view_set_colors(u8 floor, u8 band2, u8 band1, u8 sky) {
    G8(0x6a60) = sky; G8(0x6a61) = band1; G8(0x6a62) = band2; G8(0x6a63) = floor;
}

// 484B:0000  Overhead-map areas: draw the party's marker (a six-step animation, DS:6DDC) at its
// place on the visible part of the map, then show the result.
void view_draw_marker(void) {
    s8 col = (s8)(G8(0x3c76 + G8(0x49fa)) + P16(GAME, 0x186));
    if (col > 1) col = col < 0x2a ? 2 : col < 0x2b ? 3 : 4;
    u16 r = P16(GAME, 0x188);
    u8 row = r < 2 ? (u8)r : r < 0x22 ? 2 : r < 0x23 ? 3 : 4;
    figure_draw((u8)((G8(0x6ddc) >> 1) + 9), G8(0x6ddc) % 2, G8(0x6aaf), (s8)row, (s8)col);
    G8(0x6ddc) = (u8)((G8(0x6ddc) + 1) % 6);
    gfx_flush_back_buffer();
}

// 484B:016D  Redraw the picture window for wherever the party is. Mode (DS:49FA) 1 is the 3D
// view; modes 2-4 are the three areas shown as a scrolling overhead map instead.
void view_redraw(void) {
    u8 mode = G8(0x49fa);
    if (P16(GAME, 0x1cc) != 0) mode = 1;
    if (mode == 1) {
        G8(0x6ab1) = map_event((s8)G8(0x6aae), (s8)G8(0x6aad));                          // event number of this square
        // the sky colour comes from a table; squares with event numbers of 80h and above (indoors) use the area's second entry
        u16 slot = P16(GAME, G8(0x6ab1) < 0x80 ? 0x1fa : 0x1fc);
        G8(0x6a60) = G8(0x0c26 + slot);
        if (G8(0x84e5)) {
            G8(0x6a61) = band1_color(G8(0x6f69));
            G8(0x6a62) = band2_color(G8(0x6f69));
            G8(0x6a63) = floor_color(G8(0x6f68));
        }
        view_set_colors(G8(0x6a63), G8(0x6a62), G8(0x6a61), G8(0x6a60));
        if (P16(GAME, 0x1f6) != 0) G8(0x6aac) = 0;
        view_render(G8(0x6aaf), (s8)G8(0x6aae), (s8)G8(0x6aad));
    } else if (mode > 1 && mode < 5) {
        s8 col = (s8)(G8(0x3c76 + mode) + (u8)P16(GAME, 0x186) - 2);
        if (col < 0) col = 0; if (col > 0x27) col = 0x27;
        s8 row = (s8)((u8)P16(GAME, 0x188) - 2);
        if (row < 0) row = 0; if (row > 0x1f) row = 0x1f;
        view_overhead(row, col);
        view_draw_marker();
    }
}

// 4876:0000  The overhead view: 5x5 tiles from the area map at DS:3648 (2Ch per row) starting at (col, row), each
// drawn as a 3x3 icon. Nine special squares (table at DS:0C30: x, y, tile) show another tile depending on the game
// state at DS:49D2 (doors opened, things done). Not reached by any test yet (the wilderness display modes 2-4).
void view_overhead(s8 row, s8 col) {
    u8 *g = &mem[LIN(G32(0x49d2))];
    #define GW(o) ((u16)(g[o] | g[(o) + 1] << 8))
    for (s8 dy = 0; dy <= 4; dy++)
        for (s8 dx = 0; dx <= 4; dx++) {
            s8 x = (s8)(dx + col), y = (s8)(dy + row);
            s8 t = (s8)(G8((u16)(y * 0x2c + x + 0x3648)) - 1);
            for (int k = 1; k <= 9; k++) {
                if (x < 0 || x != (s16)G16(k * 6 + 0xc30) || y < 0 || y != (s16)G16(k * 6 + 0xc32)) continue;
                if (k == 3) { if (GW(0x318) != 0xff) t = (s8)(G16(0xc46) - 1); }
                else if (k == 4) { if (GW(0x3a4) != 0 || (GW(0x2f8) & 8) || GW(0x33e) == 0) t = (s8)(G16(0xc4c) - 1); }
                else if (k == 5) { if (!(GW(0x340) & 2)) t = (s8)(G16(0xc52) - 1); }
                else if (k == 6) t = (s8)(G16(0xc58) - 1);
                else if (k == 7) { if (!(GW(0x340) & 1)) t = (s8)(G16(0xc5e) - 1); }
                else if ((k == 8 || k == 9) && GW(0x366) > 0xfd) t = (s8)(G16(k * 6 + 0xc34) - 1);
            }
            icon_draw((u8)t, (s8)(dy * 3), (s8)(dx * 3));
        }
    #undef GW
}
