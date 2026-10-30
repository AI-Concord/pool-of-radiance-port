#include "fixes.h"
// Combat: the battlefield tables (original overlay units 4AFB and 4A3D, the first small pieces).
// Combatants have slots 1..N (N at DS:5F2A): a far pointer to the character at DS:65B9 + slot*4
// and four bytes at DS:5F27 + slot*4: x, y, an unknown byte, and a byte that looks like size.
#include "poolrad.h"
#include "rtl.h"
#include "gfx.h"
#include "text.h"
#include "map.h"
#include "combat.h"
#include "rules.h"
#include "chr.h"
#include "input.h"
#include "scene.h"
#include "menu.h"
#include "hud.h"
#include "fight.h"
#include "ecl.h"
#include "sheet.h"

// 4AFB:0C5F  Which slot is this character in? 0 if none.
u8 combat_slot(farptr ch) {
    u8 slot = 0;
    do { slot++; if (G32(0x65b9 + slot * 4) == ch) return slot; } while (slot != G8(0x5f2a));
    return 0;
}
// 4AFB:0BE7, 0C0F, 0C37  The character's x, y and size on the battlefield.
u8 combat_x(farptr ch) { return G8(0x5f27 + combat_slot(ch) * 4); }
u8 combat_y(farptr ch) { return G8(0x5f28 + combat_slot(ch) * 4); }
u8 combat_size(farptr ch) { return G8(0x5f2a + combat_slot(ch) * 4); }

// ---- Lines across the battlefield (4A3D) ----
// A line is walked square by square with Bresenham's algorithm. The 26-byte record: words x0, y0,
// x1, y1 at 0, the error term at 8, |dx| and |dy| at 0Ah and 0Ch, the two error increments at 0Eh
// (diagonal step) and 10h (straight step), bytes of step direction in x and y at 12h and 13h,
// the current square at 14h and 16h, the distance covered at 18h in half-squares (a straight
// step costs 2, a diagonal 3) and the compass direction of the last step at 19h.

// 4A3D:0000  The sign of a number: -1, 0 or 1.
s16 combat_sign(s16 v) { return v < 0 ? -1 : v < 1 ? 0 : 1; }

// 4A3D:01A0  Set a line record up from its two end points.
void line_begin(farptr line) {
    s16 dx = (s16)(P16(line, 4) - P16(line, 0)), dy = (s16)(P16(line, 6) - P16(line, 2));
    s16 ax = (s16)(dx < 0 ? -dx : dx), ay = (s16)(dy < 0 ? -dy : dy);
    P8(line, 0x18) = 0;
    SETP16(line, 0x0a, (u16)ax); SETP16(line, 0x0c, (u16)ay);
    P8(line, 0x12) = (u8)combat_sign(dx); P8(line, 0x13) = (u8)combat_sign(dy);
    SETP16(line, 0x14, P16(line, 0)); SETP16(line, 0x16, P16(line, 2));
    s16 minor = ay < ax ? ay : ax, major = ay < ax ? ax : ay;
    SETP16(line, 8, (u16)(minor * 2 - major));
    SETP16(line, 0x0e, (u16)((minor - major) * 2));
    SETP16(line, 0x10, (u16)(minor << 1));
}

// 4A3D:02CC  Take one step along the line. Returns 0 once the end has been reached.
u8 line_step(farptr line) {
    s8 sx = 0, sy = 0; u8 moved = 0;
    int along_x = (s16)P16(line, 0x0c) < (s16)P16(line, 0x0a);
    if (along_x ? P16(line, 0x14) != P16(line, 4) : P16(line, 0x16) != P16(line, 6)) {
        int diagonal = (s16)P16(line, 8) >= 0;
        if (along_x || diagonal) { sx = (s8)P8(line, 0x12); SETP16(line, 0x14, (u16)(P16(line, 0x14) + sx)); }
        if (!along_x || diagonal) { sy = (s8)P8(line, 0x13); SETP16(line, 0x16, (u16)(P16(line, 0x16) + sy)); }
        SETP16(line, 8, (u16)(P16(line, 8) + P16(line, diagonal ? 0x0e : 0x10)));
        P8(line, 0x18) += diagonal ? 3 : 2;
        moved = 1;
    }
    P8(line, 0x19) = G8(0x25c1 + (sy + 1) * 3 + sx);
    return moved;
}

// 4A3D:002E  Sort the "who is in reach" list (DS:671A a count, then three bytes per entry:
// combatant, distance, direction) by distance, nearest first; at equal distance an entry moves
// ahead if its direction is lower and no more odd than the other's.
void reach_list_sort(void) {
    u8 n = G8(0x671a);
    if (n <= 1) return;
    for (u8 i = 1; i <= (u8)(n - 1); i++)
        for (u8 j = (u8)(i + 1); j <= G8(0x671a); j++) {
            u8 *a = &G8(0x6718 + i * 3), *b = &G8(0x6718 + j * 3);
            if (b[1] < a[1] || (b[1] == a[1] && b[2] < a[2] && b[2] % 2 <= a[2] % 2)) { u8 t[3]; memcpy(t, a, 3); memcpy(a, b, 3); memcpy(b, t, 3); }
        }
}

// 4A3D:0422  Line of sight. Walk from (x0, y0) towards (*x1, *y1) across the battlefield map
// (50 squares to a row, tiles from byte 7; byte 6 set means nothing blocks). Each kind of tile
// has four bytes at DS:2745: the height an observer on it looks from, and the height it blocks
// up to. The walk stops when a square blocks higher than the eye level, or when the distance
// passes *range squares. Returns 1 if the far end was reached; otherwise 0 with *x1, *y1 set to
// where it stopped. Either way *range comes back as the distance covered, in half-squares.
// (A second line is stepped alongside to carry the eye level from one end to the other, but
// the original gives it the starting square's height at both ends, so it never changes.)
u8 line_of_sight(farptr map, farptr range_var, farptr y1_var, farptr x1_var, s8 y0, s8 x0) {
    farptr a = scratch_push(52), b = a + 26;
    u16 limit = (u16)(P16(range_var, 0) * 2 + 1);
    u8 result;
    SETP16(a, 0, (u16)(s16)x0); SETP16(a, 2, (u16)(s16)y0);
    SETP16(a, 4, (u16)(s16)(s8)P8(x1_var, 0)); SETP16(a, 6, (u16)(s16)(s8)P8(y1_var, 0));
    line_begin(a);
    u16 eye = G8(0x2745 + P8(map, y0 * 0x32 + x0 + 7) * 4);
    SETP16(b, 0, 0); SETP16(b, 2, eye);
    SETP16(b, 4, (s16)P16(a, 0x0a) > (s16)P16(a, 0x0c) ? P16(a, 0x0a) : P16(a, 0x0c)); SETP16(b, 6, eye);
    line_begin(b);
    for (;;) {
        int blocked = P8(map, 6) == 0 && (s16)G8(0x2746 + P8(map, (u16)(P16(a, 0x16) * 0x32 + P16(a, 0x14) + 7)) * 4) > (s16)P16(b, 0x16);
        if (blocked || P8(a, 0x18) > limit) {
            P8(x1_var, 0) = (u8)P16(a, 0x14); P8(y1_var, 0) = (u8)P16(a, 0x16);
            result = 0; break;
        }
        line_step(b);
        if (!line_step(a)) { result = 1; break; }
    }
    SETP16(range_var, 0, P8(a, 0x18));
    scratch_pop(52);
    return result;
}

// 4AFB:0000  The squares a creature of a given size covers: size 1 one square, 2 two squares
// one above the other, 3 two side by side, 4 a block of four. Returns 0 if this corner (0..3)
// is not used; otherwise the offsets from the creature's own square.
u8 combat_corner(s8 *dy, s8 *dx, u8 corner, u8 size) {
    if (size == 0) return 0;
    *dx = (s8)G8(0x2844 + size * 8 + corner * 2);
    *dy = (s8)G8(0x2845 + size * 8 + corner * 2);
    return *dx > -1;
}

// 4A3D:057B  Is the square (tx, ty) inside the quarter-circle that faces direction `dir`
// (0 north, then clockwise to 7 north-west) from (sx, sy)? The arc is measured from the square
// one step along that direction. Direction 8 (or FFh) means "any direction". Squares off the
// 50 by 25 battlefield are never in view.
u8 combat_in_arc(u8 dir, s8 ty, s8 tx, s8 sy, s8 sx) {
    if (sx < 0 || sx > 0x31 || sy < 0 || sy > 0x18 || tx < 0 || tx > 0x31 || ty < 0 || ty > 0x18) return 0;
    if (dir == 0xff) dir = 8;
    int cx = (s8)(sx + (s8)G8(0x2736 + dir)), cy = (s8)(sy + (s8)G8(0x273f + dir));
    if ((sx == tx && sy == ty) || (cx == tx && cy == ty)) return 1;
    switch (dir) {
    case 0: return !((tx < cx || cx - tx + cy < ty) && (cx < tx || tx - cx + cy < ty));
    case 1: return !((tx < cx || cx - tx + cy < ty) && (tx < cx + cy - ty || cy < ty));
    case 2: return !((tx < cx + cy - ty || cy < ty) && (tx < cx + ty - cy || ty < cy));
    case 3: return !((tx < cx + ty - cy || ty < cy) && (tx < cx || ty < tx - cx + cy));
    case 4: return !((tx < cx || ty < tx - cx + cy) && (cx < tx || ty < cx - tx + cy));
    case 5: return !((cx < tx || ty < cx - tx + cy) && (cx + cy - ty < tx || ty < cy));
    case 6: return !((cx + cy - ty < tx || ty < cy) && (cx + ty - cy < tx || cy < ty));
    case 7: return !((cx + ty - cy < tx || cy < ty) && (cx < tx || tx - cx + cy < ty));
    default: return 1;                                                      // 8; the hooks keep 9..254 away from here
    }
}

// 4A3D:0909  Who can be reached from here? From a creature of `size` at (x, y), looking in
// direction `dir` (FFh for all round) and no farther than `range`, try every square it covers
// against every square each combatant covers; where the target is in the arc and there is a
// clear line, keep the shortest. The result is the reach list at DS:671A (see reach_list_sort):
// combatant, distance in half-squares, and the direction the target lies in.
void combat_reach(farptr map, u8 size, u8 dir, u16 range, s8 y, s8 x) {
    s8 sx[4], sy[4], tx[4], ty[4], dx, dy;
    for (u8 c = 0; c <= 3; c++) { if (combat_corner(&dy, &dx, c, size)) { sx[c] = (s8)(x + dx); sy[c] = (s8)(y + dy); } else sx[c] = -1; }
    G8(0x671a) = 0;
    u8 n = G8(0x5f2a);
    farptr v = scratch_push(4);                                             // the VAR parameters of line_of_sight: range, y, x
    for (u8 slot = 1; slot <= n && n != 0; slot++) {
        u8 tsize = G8(0x5f2a + slot * 4);
        if (tsize == 0) continue;
        u8 any = 0, bi = 0, bj = 0; u16 best = 0xff;
        for (u8 c = 0; c <= 3; c++) { if (combat_corner(&dy, &dx, c, tsize)) { tx[c] = (s8)(G8(0x5f27 + slot * 4) + dx); ty[c] = (s8)(G8(0x5f28 + slot * 4) + dy); } else tx[c] = -1; }
        for (u8 j = 0; j <= 3; j++) {
            if (tx[j] < 0) continue;
            for (u8 i = 0; i <= 3; i++) {
                if (sx[i] < 0 || !combat_in_arc(dir, ty[j], tx[j], sy[i], sx[i])) continue;
                SETP16(v, 0, range); P8(v, 2) = (u8)ty[j]; P8(v, 3) = (u8)tx[j];
                if (line_of_sight(map, v, v + 2, v + 3, sy[i], sx[i])) { any = 1; if (P16(v, 0) < best) { best = P16(v, 0); bi = i; bj = j; } }
            }
        }
        if (!any) continue;
        u8 k = ++G8(0x671a), facing = 0;
        G8(0x6718 + k * 3) = slot;
        G8(0x6719 + k * 3) = (u8)best;
        if (dir < 8) facing = dir; else while (!combat_in_arc(facing, ty[bj], tx[bj], sy[bi], sx[bi])) facing++;
        G8(0x671a + k * 3) = facing;
    }
    scratch_pop(4);
    reach_list_sort();
}

// ---- The battlefield display (4AFB), small pieces ----
// The current map (far pointer at DS:6716) starts with a small header: bytes 2 and 3 are the
// square shown at the top left of the 7 by 7 view, 4 and 5 are set while one creature is being
// drawn specially, 6 is the "nothing blocks" flag, and the tiles follow from byte 7.
#define MAP G32(0x6716)

// 4AFB:0068  Work out where each combatant falls in the view: x at DS:604A + slot, y at DS:6092 + slot.
void combat_view_positions(void) {
    u8 n = G8(0x5f2a);
    for (u8 slot = 1; slot <= n && n != 0; slot++) {
        G8(0x604a + slot) = (u8)(G8(0x5f27 + slot * 4) - P8(MAP, 2));
        G8(0x6092 + slot) = (u8)(G8(0x5f28 + slot * 4) - P8(MAP, 3));
    }
}

// 4AFB:00E3 and 0105  Two settings of a pair of values in the picture unit (1464:2A0D, still
// original code), used when leaving and entering the character sheet during a fight. EGA only.
void combat_gfx_mode_a(void) { if (G8(0x4a1e) == 1) { gfx_palette_reg(0, 8); gfx_palette_reg(8, 0); } }
void combat_gfx_mode_b(void) { if (G8(0x4a1e) == 1) { gfx_palette_reg(0, 0); gfx_palette_reg(8, 8); } }

// 4AFB:04BE  What is at a battlefield square: the tile, and the combatant standing there (from
// a second 50 by 25 grid at DS:60DB), both 0 off the map.
void combat_square(u8 *tile, u8 *who, s8 y, s8 x) {
    if (x < 0 || x > 0x31 || y < 0 || y > 0x18) { *who = 0; *tile = 0; return; }
    *tile = P8(MAP, y * 0x32 + x + 7);
    *who = G8(0x60db + y * 0x32 + x);
}

// 4AFB:06E9  Is this view position inside the 7 by 7 window?
u8 combat_in_view(s8 y, s8 x) { return !(x < 0 || x > 6 || y < 0 || y > 6); }

// 4AFB:1298  Redraw one combatant's square in a special way (`how`, with `arg` passed on to
// the square drawer 4AFB:0996, still original code).
void combat_redraw_one(u8 how, u8 arg, farptr ch) {
    P8(MAP, 4) = how;
    P8(MAP, 5) = combat_size(ch);
    u8 x = combat_x(ch), y = combat_y(ch);
    call_original(0x4afb, 0x0996, 4, x, y, arg, 8);
    P8(MAP, 4) = 0;
    P8(MAP, 5) = 1;
}

static void draw_tile(u8 tile, u8 vy, u8 vx) { call_original(0x4c2b, 0x00f1, 4, (u8)(vx * 3), (u8)(vy * 3), G8(0x2747 + tile * 4), 0); }   // 4C2B:00F1, still original code

// 4AFB:052B  Draw the ground. Called with a view position (who = 0) it draws that one square,
// unless somebody stands there, in which case, or when called with a combatant's slot, it
// draws the ground under every square that combatant covers. Each kind of tile has its picture
// number in the third byte of its entry at DS:2745; a view square is three text cells wide.
void combat_draw_ground(u8 who, s8 vy, s8 vx) {
    if (who == 0) {
        s8 x = (s8)(vx + (s8)P8(MAP, 2)), y = (s8)(vy + (s8)P8(MAP, 3)); u8 tile;
        combat_square(&tile, &who, y, x);
        if (who == 0) { if (combat_in_view(vy, vx)) draw_tile(P8(MAP, y * 0x32 + x + 7), (u8)vy, (u8)vx); return; }
    }
    vx = (s8)G8(0x604a + who); vy = (s8)G8(0x6092 + who);
    s8 x = (s8)(vx + (s8)P8(MAP, 2)), y = (s8)(vy + (s8)P8(MAP, 3)), dx, dy;
    u8 size = G8(0x5f2a + who * 4);
    for (u8 c = 0; c <= 3; c++)
        if (combat_corner(&dy, &dx, c, size) && combat_in_view((s8)(vy + dy), (s8)(vx + dx)))
            draw_tile(P8(MAP, (y + dy) * 0x32 + x + dx + 7), (u8)(vy + dy), (u8)(vx + dx));
}
static void draw_ground(u8 who, u8 vy, u8 vx) { combat_draw_ground(who, (s8)vy, (s8)vx); }
static void draw_figure(u8 figure, u8 b, u8 facing, u8 vy, u8 vx) { figure_draw(figure, b, facing, (s8)vy, (s8)vx); }   // 4C2B:04FC
void combat_step_view(u8 dir, u8 mode, s8 y, s8 x);
void combat_draw_at(s8 y, s8 x);
u8 combat_scroll_view(u8 margin, s8 y, s8 x);
static void scroll_to(u8 x, u8 y, u8 a, u8 b) { combat_step_view(b, a, (s8)y, (s8)x); }

// 4AFB:071A  Is a combatant inside the view? With `whole` set every square it covers must be;
// without it one square is enough.
u8 combat_visible(u8 whole, farptr ch) {
    u8 slot = combat_slot(ch), size = G8(0x5f2a + slot * 4), result = 1;
    s8 dx, dy;
    if (size == 0) return 0;
    for (u8 c = 0; c <= 3; c++) {
        if (!combat_corner(&dy, &dx, c, size)) continue;
        if (!combat_in_view((s8)((s8)G8(0x6092 + slot) + dy), (s8)((s8)G8(0x604a + slot) + dx))) { result = 0; if (whole) return 0; }
        else { result = 1; if (!whole) return 1; }
    }
    return result;
}

// 4AFB:02D5  Redraw one battlefield square: the ground, then whoever stands on it (their
// figure is slot BFh of the record, their facing byte 9 of the action record).
void combat_redraw_square(s8 y, s8 x) {
    u8 tile, who;
    draw_ground(0, (u8)(y - (s8)P8(MAP, 3)), (u8)(x - (s8)P8(MAP, 2)));
    combat_square(&tile, &who, y, x);
    if (!who) return;
    farptr ch = G32(0x65b9 + who * 4);
    if (combat_visible(0, ch)) draw_figure(P8(ch, 0xbf), 0, P8(P32(ch, 0x108), 9), G8(0x6092 + who), G8(0x604a + who));
}

// 4AFB:0AF8  Turn a combatant to face a new direction and show it. If they are not wholly in
// view the view is moved first. Their square is redrawn when the figure actually changes (the
// figures only have a left-facing and a right-facing form, so directions 0-3 and 4-7 look the
// same), or when asked to; `quiet` updates the facing without drawing the figure.
void combat_face(u8 quiet, u8 flag, u8 facing, farptr ch) {
    u8 slot = combat_slot(ch);
    if (!combat_visible(1, ch)) scroll_to(combat_x(ch), combat_y(ch), 3, 8);
    farptr act = P32(ch, 0x108);
    if ((P8(act, 9) >> 2) != (facing >> 2) || flag || quiet) draw_ground(slot, 0, 0);
    if (facing != P8(act, 9)) P8(act, 9) = facing;
    if (!quiet && combat_visible(1, ch)) {
        draw_figure(P8(ch, 0xbf), flag, facing, G8(0x6092 + slot), G8(0x604a + slot));
        gfx_flush_back_buffer();
    }
}

// 4AFB:0996  Something at (x, y) is about to be at the next square in direction `dir` (8 for
// "here"): scroll the view if that square needs it (4AFB:07D4, still original code; `mode`
// is passed on), put every visible combatant back if it scrolled, redraw the old square, and
// centre the cursor business on the new one (4AFB:0127, still original code).
void combat_step_view(u8 dir, u8 mode, s8 y, s8 x) {
    s8 nx = (s8)(x + (s8)G8(0x2736 + dir)), ny = (s8)(y + (s8)G8(0x273f + dir));
    if (combat_scroll_view(mode, ny, nx)) {
        u8 n = G8(0x5f2a);
        for (u8 slot = 1; slot <= n && n != 0; slot++) {
            farptr ch = G32(0x65b9 + slot * 4);
            if (P8(ch, 0x10d) != 0 && G8(0x5f2a + slot * 4) != 0 && combat_visible(0, ch))
                draw_figure(P8(ch, 0xbf), 0, P8(P32(ch, 0x108), 9), G8(0x6092 + slot), G8(0x604a + slot));
        }
    }
    combat_redraw_square(y, x);
    if (!combat_in_view((s8)(ny - (s8)P8(MAP, 3)), (s8)(nx - (s8)P8(MAP, 2)))) {
        if (nx > 0x31) nx = 0x31;
        if (nx < 0) nx = 0;
        if (ny > 0x18) ny = 0x18;
        if (ny < 0) ny = 0;
    }
    combat_draw_at(ny, nx);
    gfx_flush_back_buffer();
}

// 4AFB:0127  Draw whatever is being moved about (a combatant in mid-step, or the aiming
// cursor) at (x, y): the ground under each square it covers (its size is byte 5 of the map
// header), the cursor frame (figure 19h) if byte 4 says so, and then whoever stands there.
void combat_draw_at(s8 y, s8 x) {
    s8 vx = (s8)(x - (s8)P8(MAP, 2)), vy = (s8)(y - (s8)P8(MAP, 3)), dx, dy;
    for (u8 c = 0; c <= 3; c++) {
        if (!combat_corner(&dy, &dx, c, P8(MAP, 5)) || !combat_in_view((s8)(vy + dy), (s8)(vx + dx))) continue;
        draw_tile(P8(MAP, (y + dy) * 0x32 + x + dx + 7), (u8)(vy + dy), (u8)(vx + dx));
        if (P8(MAP, 4) != 0) draw_figure(0x19, 0, 0, (u8)(vy + dy), (u8)(vx + dx));
    }
    u8 tile, who;
    combat_square(&tile, &who, y, x);
    if (!who) return;
    farptr ch = G32(0x65b9 + who * 4);
    if (combat_visible(0, ch)) draw_figure(P8(ch, 0xbf), 0, P8(P32(ch, 0x108), 9), G8(0x6092 + who), G8(0x604a + who));
}

// 4AFB:07D4  Keep (x, y) in sight. `margin` is how far from the centre of the 7 by 7 view it
// may stray before the view moves (FFh: always re-centre). The view then slides, a square at a
// time and never past the edges of the map, until its centre reaches the square; all 49
// ground tiles are redrawn and the combatants' view positions worked out again. Returns 1
// if the view moved.
u8 combat_scroll_view(u8 margin, s8 y, s8 x) {
    s8 cx = (s8)(P8(MAP, 2) + 3), cy = (s8)(P8(MAP, 3) + 3);
    u8 m = margin == 0xff ? 0 : margin;
    s8 top = (s8)(cy - m), bottom = (s8)(cy + m), left = (s8)(cx - m), right = (s8)(cx + m);
    if (!(margin == 0xff || x < left || right < x || y < top || bottom < y)) return 0;
    if (x < left) { while (x < cx && cx > 3) cx--; } else if (right < x) { while (cx < x && cx < 0x2e) cx++; }
    if (y < top) { while (y < cy && cy > 3) cy--; } else if (bottom < y) { while (cy < y && cy < 0x15) cy++; }
    P8(MAP, 2) = (u8)(cx - 3); P8(MAP, 3) = (u8)(cy - 3);
    for (u8 r = 0; r <= 6; r++)
        for (u8 c = 0; c <= 6; c++)
            call_original(0x4c2b, 0x00f1, 4, (u8)(c * 3), (u8)(r * 3), G8(0x2747 + P8(MAP, ((s8)P8(MAP, 3) + r) * 0x32 + (s8)P8(MAP, 2) + c + 7) * 4), 1);
    combat_view_positions();
    return 1;
}

// 4AFB:03A0  Rebuild the "who stands where" grid from the combatants' positions (every square
// each one covers), and their view positions with it.
void combat_place_all(void) {
    memset(&G8(0x60db), 0, 0x4e2);
    u8 n = G8(0x5f2a); s8 dx, dy;
    for (u8 slot = 1; slot <= n && n != 0; slot++) {
        u8 size = G8(0x5f2a + slot * 4);
        if (size == 0) continue;
        for (u8 c = 0; c <= 3; c++)
            if (combat_corner(&dy, &dx, c, size))
                G8(0x60db + ((s8)G8(0x5f28 + slot * 4) + dy) * 0x32 + (s8)G8(0x5f27 + slot * 4) + dx) = slot;
        G8(0x604a + slot) = (u8)(G8(0x5f27 + slot * 4) - P8(MAP, 2));
        G8(0x6092 + slot) = (u8)(G8(0x5f28 + slot * 4) - P8(MAP, 3));
    }
}

// 4AFB:0CB9  What would a combatant meet by stepping in direction `dir`? Looks at the square
// ahead of each square it covers. *who is a combatant in the way (other than itself). *tile is
// the worst ground ahead: 0 if any square is off the map or impassable tile 0, else 1Eh if any
// is tile 1Eh, else the tile with the highest movement cost (first byte of its entry at
// DS:2744); 17h if nothing was looked at.
void combat_look_ahead(u8 *tile, u8 *who, u8 dir, farptr ch) {
    u8 slot = combat_slot(ch), best = 1, t, w;
    s8 x = (s8)G8(0x5f27 + slot * 4), y = (s8)G8(0x5f28 + slot * 4), dx, dy;
    *who = 0; *tile = 0x17;
    for (u8 c = 0; c <= 3; c++) {
        if (!combat_corner(&dy, &dx, c, G8(0x5f2a + slot * 4))) continue;
        combat_square(&t, &w, (s8)(y + dy + (s8)G8(0x273f + dir)), (s8)(x + dx + (s8)G8(0x2736 + dir)));
        if (w == slot) w = 0;
        if (w != 0) *who = w;
        if (t == 0 || *tile == 0) *tile = 0;
        else if (t == 0x1e || *tile == 0x1e) *tile = 0x1e;
        else if (best <= G8(0x2744 + t * 4)) { best = G8(0x2744 + t * 4); *tile = t; }
    }
}

// The fallen: up to eight 7-byte entries at DS:66D6 + n*7 (count at DS:6715): the character,
// where they fell, and the ground tile that was there before the body (tile 1Fh) replaced it.

// 4AFB:0E11  A combatant goes down. In a fight: bring them into view, draw the "slain"
// picture (DS:5F1A) over each square they cover, play the sound, wait for a key, then take them
// off the battlefield. Unless their action record's byte 13h is set, the body stays: the square
// becomes tile 1Fh and an entry is made in the list of the fallen. Outside a fight it is just
// the sound and the pause.
// (It starts with a loop that looks meant to search that list for the character, but it tests
// the same entry, numbered by DS:70C7, eight times over; if that one entry is theirs it returns
// at once, without the closing sound call. Kept, unless FIX is on: see docs, "The list of the fallen".)
void combat_fall(farptr ch) {
    sound_music(0);
    if (G8(0x49f3) != 5) { sound_play(G16(0x261e)); text_pause(); sound_music(10); return; }
    if (FIX) { for (int i = 1; i <= 8; i++) if (G32(0x66d6 + i * 7) == ch) return; }   // (FIX: the list really searched)
    else if (G32(0x66d6 + (s8)G8(0x70c7) * 7) == ch) return;
    u8 slot = combat_slot(ch); s8 x = (s8)combat_x(ch), y = (s8)combat_y(ch), dx, dy;
    if (!combat_visible(1, ch)) combat_step_view(8, 3, y, x);
    combat_draw_ground(slot, 0, 0);
    for (u8 c = 0; c <= 3; c++)
        if (combat_corner(&dy, &dx, c, G8(0x5f2a + slot * 4)))
            gfx_merge_pic_clipped(G32(0x4a20), G32(0x5f1a), 5, 0, (s16)(((s8)G8(0x6092 + slot) + dy) * 3), (s16)(((s8)G8(0x604a + slot) + dx) * 3));
    gfx_flush_back_buffer();
    farptr act = P32(ch, 0x108);
    int reuse = 0;
    if (FIX && G8(0x6715) >= 8) {               // (FIX: a full list reuses a blanked entry, or the body is not listed,
        reuse = -1;                             //  instead of a ninth entry overwriting the battlefield map pointer)
        for (int i = 1; i <= 8 && reuse < 0; i++) if (!G32(0x66d6 + i * 7)) reuse = i;
    }
    if (P8(act, 0x13) == 0 && reuse >= 0) {
        u8 k = reuse ? (u8)reuse : ++G8(0x6715), was = P8(MAP, y * 0x32 + x + 7);
        G8(0x66dc + k * 7) = was;
        if (was != 0x1e) P8(MAP, y * 0x32 + x + 7) = 0x1f;
        SETG32(0x66d6 + k * 7, ch);
        G8(0x66da + k * 7) = (u8)x; G8(0x66db + k * 7) = (u8)y;
    }
    sound_play(G16(0x261e));
    text_pause();
    combat_draw_ground(slot, 0, 0);
    G8(0x5f2a + combat_slot(ch) * 4) = 0;
    combat_place_all();
    combat_step_view(8, 3, (s8)(P8(MAP, 3) + 3), (s8)(P8(MAP, 2) + 3));
    P8(act, 3) = 0; P8(act, 6) = 0; P8(act, 0) = 0; P8(act, 7) = 0;
    sound_music(10);
}

// 4AFB:10A7  Put a combatant on the battlefield at (x, y), with the size from byte 6Ch of the
// record. It fails (and they stay off the field) if someone is already there or the ground
// cannot be stood on (movement cost FFh). With `rise` set, for someone getting back up, their
// entry in the list of the fallen is cleared and the ground is restored to what it was, unless
// another body lies on the same square. Outside a fight it always succeeds.
u8 combat_place(u8 rise, u8 y, u8 x, farptr ch) {
    if (G8(0x49f3) != 5) return 1;
    u8 slot = combat_slot(ch), tile, who;
    G8(0x5f2a + slot * 4) = P8(ch, 0x6c) & 0x7f;
    G8(0x5f27 + slot * 4) = x; G8(0x5f28 + slot * 4) = y;
    combat_look_ahead(&tile, &who, 8, ch);
    if (!(who == 0 && tile != 0 && G8(0x2744 + tile * 4) != 0xff)) { G8(0x5f2a + slot * 4) = 0; return 0; }
    if (rise && P8(P32(ch, 0x108), 0x13) == 0) {
        u8 n = G8(0x6715), shared = 0;
        for (u8 k = 1; k <= n && n != 0; k++)
            if (G32(0x66d6 + k * 7) == ch) { if (G8(0x66dc + k * 7) != 0x1f) tile = G8(0x66dc + k * 7); memset(&G8(0x66d6 + k * 7), 0, 7); }
        for (u8 k = 1; k <= G8(0x6715) && G8(0x6715) != 0; k++)
            if (G32(0x66d6 + k * 7) != 0 && (s8)G8(0x66da + k * 7) == (int)x && (s8)G8(0x66db + k * 7) == (int)y) shared = 1;
        if (!shared) P8(MAP, y * 0x32 + x + 7) = tile;
    }
    combat_place_all();
    return 1;
}

// ---- Building the battlefield from the dungeon (2843), first pieces ----
// 2843:012B  What the dungeon has on one side of a square, as the battlefield builder sees it:
// 0 open, 1 solid, 3 a wall with a way through. Off the 16 by 16 level everything is solid,
// except east and west on the party's own row.
u8 combat_dungeon_side(u8 dir, s8 y, s8 x) {
    if (x < 0 || x > 15 || y < 0 || y > 15) return (y == (s8)G8(0x6aae) && (dir == 2 || dir == 6)) ? 0 : 1;
    if (!map_passage(dir, y, x)) return 1;
    return map_wall(dir, y, x) ? 3 : 0;
}
// 2843:01AD  The same seen from both sides: this square's side, or the facing side of the
// square beyond it.
u8 combat_dungeon_edge(u8 dir, s8 y, s8 x) {
    u8 a = combat_dungeon_side(dir, y, x);
    return a | combat_dungeon_side((u8)((dir + 4) % 8), (s8)(y + (s8)G8(0x273f + dir)), (s8)(x + (s8)G8(0x2736 + dir)));
}

// 10BA:255B  The distance in squares from one combatant to another, ignoring anything in the
// way: a reach scan from `from` with blocking switched off, all round and at any range, then
// that target's entry in the list, halved. (If the target is not in the list at all the last
// entry's distance is returned; kept.) The work area the scan uses is saved and put back.
u8 combat_distance(farptr to, farptr from) {
    u8 saved[0xd8], i = 1;
    memcpy(saved, &G8(0x671b), sizeof saved);
    P8(MAP, 6) = 1;
    combat_reach(MAP, combat_size(from), 0xff, 0xff, (s8)combat_y(from), (s8)combat_x(from));
    P8(MAP, 6) = 0;
    while (G32(0x65b9 + G8(0x6718 + i * 3) * 4) != to && i < G8(0x671a)) i++;
    u8 d = G8(0x6719 + i * 3) >> 1;
    memcpy(&G8(0x671b), saved, sizeof saved);
    return d;
}

// 10BA:2433  The enemies of a combatant within `range` and in line of sight, nearest first: the reach list is cut down to those on the other side, and
// their slot numbers are copied to DS:6DE5 onward. Returns how many.
u8 combat_enemies_in_reach(u8 range, farptr ch) {
    combat_reach(MAP, combat_size(ch), 0xff, range, (s8)combat_y(ch), (s8)combat_x(ch));
    u8 n = G8(0x671a), kept = 0, enemy_side = P8(ch, 0x10e) == 0;
    if (n == 0) return 0;
    for (u8 i = 1; i <= n; i++) {
        farptr other = G32(0x65b9 + G8(0x6718 + i * 3) * 4);
        if (P8(other, 0x10e) != enemy_side) continue;
        kept++;
        memmove(&G8(0x6718 + kept * 3), &G8(0x6718 + i * 3), 3);
    }
    G8(0x671a) = kept;
    for (u8 i = 1; i <= kept; i++) G8(0x6de4 + i) = G8(0x6718 + i * 3);
    return kept;
}

// ---- Building an indoor battlefield from the 3D map (unit 2843) ----
// 2843:0810 walks the 13 by 5 map squares around the party; each becomes a block of battlefield squares, drawn by
// nested routines that share its locals: the offsets dx, dy, the map square and the walls found on its west (6),
// north (0) and east (2) sides.
typedef struct { s8 dx, dy, mx, my; u8 w6, w0, w2; } BfCell;

// 2843:0221  One battlefield square of the current block: column c + 21 + 6dx + 5dy, row r + 10 + 5dy (the
// column also shifts with the row, as in the original), tile t + 1, if it lies on the 50 by 25 field.
static void bf_set(const BfCell *b, u8 t, u8 r, u8 c) {
    s8 x = (s8)(c + b->dx * 6 + b->dy * 5 + 0x15), y = (s8)(r + b->dy * 5 + 0x0a);
    if (x < 0 || x > 0x31 || y < 0 || y > 0x18) return;
    P8(G32(0x6716), (u16)(y * 0x32 + x + 7)) = (u8)(t + 1);
}
// 2843:02BA  The floor (rows 2-4, columns 0-5), and the west wall: a solid wall (1) as a diagonal band, a door (3)
// as two posts.
static void bf_floor_west(const BfCell *b) {
    for (u8 r = 2; r <= 4; r++) for (u8 c = 0; c <= 5; c++) bf_set(b, 0x16, r, c);
    if (b->w6 == 1) for (u8 r = 2; r <= 4; r++) { bf_set(b, 4, r, (u8)(r - 1)); bf_set(b, 3, r, r); bf_set(b, 0x0d, r, (u8)(r + 1)); }
    else if (b->w6 == 3) { bf_set(b, 8, 2, 1); bf_set(b, 0, 4, 5); }
}
// 2843:0377  The north side: wall pieces for a solid wall (1), floor otherwise.
static void bf_north(const BfCell *b) {
    if (b->w0 == 1) { bf_set(b, 5, 0, 3); bf_set(b, 5, 0, 4); bf_set(b, 10, 1, 3); bf_set(b, 10, 1, 4); }
    else { bf_set(b, 0x16, 0, 3); bf_set(b, 0x16, 0, 4); bf_set(b, 0x16, 1, 3); bf_set(b, 0x16, 1, 4); }
}
// 2843:040F  The north-west corner of a block (rows 0-1, columns 1-2), from the north and west walls and whether the
// diagonal neighbours are open. Returns false for wall combinations the original leaves undefined.
static u8 bf_corner_nw(const BfCell *b) {
    u8 diag = combat_dungeon_edge(6, (s8)(b->my - 1), b->mx) == 0 && combat_dungeon_edge(0, b->my, (s8)(b->mx - 1)) == 0;
    int t1 = -1, t2 = -1, t3 = -1, t4 = -1;
    if (b->w0 == 0) { if (b->w6 == 0) t1 = 0x16; else if (b->w6 == 3) t1 = 0x0d; else if (b->w6 == 1) t1 = diag ? 0 : 0x0d; }
    else if (b->w0 == 3 || b->w0 == 1) t1 = b->w6 == 0 ? (diag ? 0x0f : 5) : (diag ? 0x12 : 2);
    if (b->w0 == 0) t2 = 0x16; else if (b->w0 == 3) t2 = 0x11; else if (b->w0 == 1) t2 = 5;
    if (b->w6 == 0) t3 = b->w0 == 0 ? 0x16 : (diag ? 0x10 : 0x0a);
    else if (b->w6 == 3) t3 = diag ? 0x14 : 7;
    else if (b->w6 == 1) t3 = diag ? 1 : 3;
    if (b->w6 == 0 || b->w6 == 3) { if (b->w0 == 0) t4 = 0x16; else if (b->w0 == 3) t4 = 0x17; else if (b->w0 == 1) t4 = 0x0a; }
    else if (b->w6 == 1) { if (b->w0 == 0) t4 = 0x0d; else if (b->w0 == 3) t4 = 0x15; else if (b->w0 == 1) t4 = 6; }
    if (t1 < 0 || t2 < 0 || t3 < 0 || t4 < 0) return 0;
    bf_set(b, (u8)t1, 0, 1); bf_set(b, (u8)t2, 0, 2); bf_set(b, (u8)t3, 1, 1); bf_set(b, (u8)t4, 1, 2);
    return 1;
}
// 2843:0615  The north-east corner (rows 0-1, columns 5-6), from the north and east walls and the walls of the
// neighbours to the north (its east side) and to the east (its north side).
static u8 bf_corner_ne(const BfCell *b) {
    u8 a = combat_dungeon_edge(2, (s8)(b->my - 1), b->mx), c = combat_dungeon_edge(0, b->my, (s8)(b->mx + 1));
    u8 diag = a == 0 && c == 0;
    int t1 = -1, t2 = -1, t3 = -1, t4 = -1;
    if (b->w0 == 0) t1 = a == 1 ? 4 : 0x16; else if (b->w0 == 3) t1 = 0x0f; else if (b->w0 == 1) t1 = 5;
    if (b->w0 == 0) {
        if (a == 0) t2 = 0x16;
        else if (a == 3) t2 = (b->w2 == 0 && c != 0) ? 0x18 : 1;
        else if (a == 1) t2 = (b->w2 == 0 && c != 0) ? 0x0b : 3;
    } else t2 = b->w2 ? 9 : c ? 5 : diag ? 0x11 : 0x13;
    if (b->w0 == 0) t3 = 0x16; else if (b->w0 == 3) t3 = 0x10; else if (b->w0 == 1) t3 = 0x0a;
    if (b->w0 == 0) t4 = a == 0 ? 0x16 : b->w2 ? 4 : c == 0 ? 8 : 0x0c;
    else t4 = b->w2 ? 0x0e : c == 0 ? 0x17 : 0x0a;
    if (t1 < 0 || t2 < 0 || t3 < 0 || t4 < 0) return 0;
    bf_set(b, (u8)t1, 0, 5); bf_set(b, (u8)t2, 0, 6); bf_set(b, (u8)t3, 1, 5); bf_set(b, (u8)t4, 1, 6);
    return 1;
}

// 2843:040F and 0615, the two upper corners, fall back to the original nested routines when undefined: they get a copy of the parent's
// locals in a frame built on the original program's stack.
static void bf_corner_original(const BfCell *b, u16 off) {
    farptr fr = scratch_push(10);
    u16 bp = (u16)(FP_OFF(fr) + 10);
    u8 *f = &mem[LIN(fr)];
    f[10 - 1] = (u8)b->dx; f[10 - 2] = (u8)b->dy; f[10 - 3] = (u8)b->mx; f[10 - 4] = (u8)b->my;
    f[10 - 7] = b->w0; f[10 - 8] = b->w6; f[10 - 9] = b->w2;
    call_original(0x2843, off, 1, bp);
    scratch_pop(10);
}
// 2843:0000  Scatters decorations over plain floor (tile class 16h): d100 98 and 99 give tiles 1Ah and 1Bh; a 100
// followed by a 1 on another d100 gives 1Ch (d10 1-6) or 1Dh (7-10), except in areas of type 10.
void bf_decorate(void) {
    for (u8 x = 0; x <= 0x31; x++)
        for (u8 y = 0; y <= 0x18; y++) {
            u8 *t = &P8(G32(0x6716), (u16)(y * 0x32 + x + 7));
            if (G8(0x2747 + *t * 4) != 0x16) continue;
            u8 r = roll_dice(100, 1);
            if (r == 0 || r > 0x61) {
                if (r == 0x62) *t = 0x1a;
                else if (r == 0x63) *t = 0x1b;
                else if (r == 100 && G8(0x84dc) != 10 && roll_dice(100, 1) == 1) {
                    u8 d = roll_dice(10, 1);
                    if (d >= 1 && d <= 6) *t = 0x1c;
                    else if (d > 6 && d < 0x0b) *t = 0x1d;
                }
            }
        }
}
void bf_build_indoor(void) {                                    // 2843:0810
    for (s8 dy = -2; ; dy++) {
        for (s8 dx = -6; ; dx++) {
            BfCell b; b.dx = dx; b.dy = dy;
            b.mx = (s8)((s8)G8(0x6aad) + dx); b.my = (s8)((s8)G8(0x6aae) + dy);
            b.w6 = combat_dungeon_edge(6, b.my, b.mx);
            b.w0 = combat_dungeon_edge(0, b.my, b.mx);
            b.w2 = combat_dungeon_edge(2, b.my, b.mx);
            bf_floor_west(&b);
            bf_north(&b);
            if (!bf_corner_nw(&b)) bf_corner_original(&b, 0x040f);
            if (!bf_corner_ne(&b)) bf_corner_original(&b, 0x0615);
            if (dx == 6) break;
        }
        if (dy == 2) break;
    }
    bf_decorate();
}

// 2843:08A4  The terrain of an outdoor battle, from the current wall/background picture (DS:465A). Returns -1 for a
// picture in none of the groups, where the original returns an uninitialised value (1, open ground, here).
// Values with bit 7 become (value + 80h) | 1 when the area's word 366h is FFh.
#define IN(a, b) (p >= (a) && p <= (b))
int bf_outdoor_terrain(void) {
    u8 p = G8(0x465a); int v;
    if (!((p == 0 || p > 2) && !IN(0x28, 0x29) && !IN(0x4f, 0x56) && !IN(0x5b, 0x5c) && p != 0x2b && p != 0xf1)) v = 1;
    else if (IN(0xa8, 0xb1)) v = 0x21;
    else if (p == 0xb9 || IN(0xbc, 0xc0)) v = 0x11;
    else if (IN(3, 4) || IN(6, 7) || IN(8, 9) || IN(0x13, 0x15) || IN(0x17, 0x19)) v = 3;
    else if (IN(3, 4) || p == 7 || IN(10, 0x12) || p == 0x16 || IN(0x1a, 0x24) || IN(0xc9, 0xd0)) v = 2;
    else if (IN(0xd1, 0xd5)) v = 0x12;
    else if (p == 0x25 || p == 0x39 || p == 0x40 || IN(0x9a, 0x9e) || IN(0xa4, 0xa5) || p == 0xb4 || p == 0xf3 || IN(0xef, 0xf0) || p == 0xfa || p == 0xfc) v = 4;
    else if (IN(0x27, 0x2c) || p == 0x99 || p == 0x9f) v = 5;
    else if (IN(0x2d, 0x2e) || IN(0x33, 0x38) || IN(0x43, 0x4d) || IN(0x5d, 0x61)) v = 0x44;
    else if (IN(0xb5, 0xb8) || p == 0xba || IN(0xc1, 0xc8)) v = 0x14;
    else if (IN(0x2f, 0x32) || IN(0x62, 0x65) || p == 0xf2) v = 0x40;
    else if (p == 0x26) v = 0x41;
    else if (IN(0xb2, 0xb3)) v = 0x60;
    else if (IN(0x3a, 0x3c) || IN(0x41, 0x42)) v = 0x48;
    else if (IN(0x3d, 0x3f) || IN(0x79, 0x7c) || IN(0x87, 0x89) || p == 0x8b || p == 0x8d || IN(0x90, 0x93) || p == 0x95 || IN(0x97, 0x98) || IN(0xa0, 0xa2) || IN(0xa6, 0xa7) || IN(0xd6, 0xd8) || p == 0xee) v = 8;
    else if (IN(0x57, 0x5a) || p == 0xa3 || p == 0xe0) v = 0x88;
    else if (p == 0x7d || p == 0x7c || p == 0x8a || p == 0x8c || p == 0x8e || p == 0x8f || p == 0x94 || p == 0x96) v = 9;
    else if (IN(0x7e, 0x85) || IN(0xe5, 0xe7)) v = 0x28;
    else if (IN(0xd9, 0xdf)) v = 0x18;
    else if (p == 0x4e || p == 0xf5 || IN(0xe8, 0xe9)) v = 0x20;
    else if (p == 0x6d || IN(0xf4, 0xf6)) v = 0x80;
    else if (IN(0x6e, 0x71) || IN(0x73, 0x78)) v = 0x81;
    else if (IN(0x66, 0x6c) || p == 0x72) v = 0x90;
    else if (IN(0xe1, 0xe4) || IN(0xea, 0xed)) v = 0xa0;
    else v = 1;
    if ((v & 0x80) && P16(G32(0x49d2), 0x366) == 0xff) v = (u8)((u8)(v + 0x80) | 1);
    return v;
}
#undef IN

// 2843:0E9E  Scatters outdoor features over plain ground (tile class 16h). The band widths depend on the terrain
// (2843:08A4): bit 2 more of the second kind, bit 4 more of it again, bit 40h little of the fifth but some of the
// fourth, bit 80h some of the first. One d255 per square picks the band: the first gives 3Ch-3Eh, or on a 4 the
// two-square feature 40h/41h when the square above is plain too; then 37h-39h, 2Ah-2Dh, 39h-3Bh, and 2Eh-31h (no
// 31h unless bit 40h). Returns false, leaving it to the original, when the terrain lookup is undefined.
u8 bf_outdoor_scatter(void) {
    int tv = bf_outdoor_terrain();
    if (tv < 0) return 0;
    u8 terr = (u8)tv;
    u8 a = 0, b = 0x0f, c = 0x28, d = 0;
    if (terr & 2) { b = 0x2d; c = 10; }
    if (terr & 4) b = 0x19;
    if (terr & 0x40) { c = 5; d = 0x1e; }
    if (terr & 0x80) { a = 0x0f; c = 0x19; b = 10; }
    for (u8 x = 0; x <= 0x31; x++)
        for (u8 y = 0; y <= 0x18; y++) {
            u8 *t = &P8(G32(0x6716), (u16)(y * 0x32 + x + 7));
            if (G8(0x2747 + *t * 4) != 0x16) continue;
            u8 r = roll_dice(0xff, 1);
            if (a < r) {
                if ((u8)(a + 6) < r) {
                    if ((u8)(b + a + 6) < r) {
                        if ((unsigned)d + b + a + 6 < r) {
                            if ((unsigned)r <= (unsigned)c + d + b + a + 6) {
                                u8 k = roll_dice(4, 1);
                                if (k == 4 && !(terr & 0x40)) k = roll_dice(3, 1);
                                *t = (u8)(k + 0x2d);
                            }
                        } else *t = (u8)((u16)(roll_dice(10, 1) - 1) / 3 + 0x39);
                    } else *t = (u8)(roll_dice(4, 1) + 0x29);
                } else *t = (u8)(roll_dice(3, 1) + 0x36);
            } else {
                u8 k = roll_dice(4, 1);
                if (k == 4 && y > 0 && G8(0x2747 + P8(G32(0x6716), (u16)((y - 1) * 0x32 + x + 7)) * 4) == 0x16) {
                    P8(G32(0x6716), (u16)((y - 1) * 0x32 + x + 7)) = 0x41;
                    *t = 0x40;
                }
                if (k < 4) *t = (u8)(k + 0x3c);
            }
        }
    return 1;
}

// 2843:0BDE  A road (tiles 32h/33h) running diagonally down an outdoor battlefield: 35% likely on terrain with bit
// 20h, 75% with bit 10h. It starts at column 34 - 5d4, moved left to where (x + 2) is a multiple of 7, and shifts one
// column right per row. Crossing pieces (34h/35h, 2843:0B97) appear on a 1 in 20 and then always in pairs (an odd
// count forces the next); with none at all, a pair is placed around rows 7 to 15. Left to the original (false) when
// the terrain lookup is undefined.
static void bf_crossing(u8 *count, s8 x, s8 y) {                  // 2843:0B97
    P8(G32(0x6716), (u16)((u16)(y * 0x32) + x + 7)) = 0x34;
    P8(G32(0x6716), (u16)((u16)(y * 0x32) + x + 8)) = 0x35;
    (*count)++;
}
u8 bf_outdoor_road(void) {
    int tv = bf_outdoor_terrain();
    if (tv < 0) return 0;
    u8 chance = 0, count = 0;
    if (tv & 0x20) chance = 0x23;
    if (tv & 0x10) chance = 0x4b;
    if (roll_dice(100, 1) > chance) return 1;
    s8 x = (s8)(0x22 - roll_dice(4, 5));
    while ((x + 2) % 7 > 0) x--;
    s8 x0 = x;
    for (s8 y = 0; ; y++) {
        if (x <= 0x31) {
            P8(G32(0x6716), (u16)((u16)(y * 0x32) + x + 7)) = 0x32;
            P8(G32(0x6716), (u16)((u16)(y * 0x32) + x + 8)) = 0x33;
            if (roll_dice(20, 1) == 1 || (count & 1)) bf_crossing(&count, x, y);
            x++;
        }
        if (y == 0x18) break;
    }
    if (count == 0) {
        s8 y = (s8)(0x0c - roll_dice(9, 1) + 4);
        x = (s8)(x0 + y);
        bf_crossing(&count, x, y);
        bf_crossing(&count, (s8)(x + 1), (s8)(y + 1));
    }
    return 1;
}

// 2843:0D13  Paired outdoor features on plain ground with plain ground above: a base tile 24h-29h (d10 1-8 halved
// rounding up, 9-10 giving 5-6; 4-6 on terrain bit 40h) and, for the four smallest, a top piece 20h-23h on the square
// above. Chance per square: 1%, 3% (bit 4), 4% (bit 40h), 7% (bit 8), none (bit 80h).
u8 bf_outdoor_pairs(void) {
    int tv = bf_outdoor_terrain();
    if (tv < 0) return 0;
    u8 chance = 1;
    if (tv & 4) chance = 3;
    if (tv & 0x40) chance = 4;
    if (tv & 8) chance = 7;
    if (tv & 0x80) chance = 0;
    for (u8 x = 0; x <= 0x31; x++)
        for (u8 y = 1; y <= 0x18; y++) {
            u8 *t = &P8(G32(0x6716), (u16)(y * 0x32 + x + 7)), *up = &P8(G32(0x6716), (u16)((y - 1) * 0x32 + x + 7));
            if (G8(0x2747 + *t * 4) != 0x16 || G8(0x2747 + *up * 4) != 0x16 || roll_dice(100, 1) > chance) continue;
            u8 d = roll_dice(10, 1), k;
            k = d < 9 ? (u8)((d + 1) >> 1) : (u8)(roll_dice(2, 1) + 4);
            if (tv & 0x40) k = (u8)(roll_dice(3, 1) + 3);
            *t = (u8)(k + 0x23);
            if (k < 5) *up = (u8)(k + 0x1f);
        }
    return 1;
}

// 2843:11B4  An outdoor battlefield: plain ground everywhere (17h), the terrain picture chosen from the area data
// (DS:3648, row by area byte 188h, column by byte 186h, shifted by 13 or 26 for area types 3 and 4), then a road,
// paired features, scattered features and decorations.
void bf_build_outdoor(void) {
    memset(&P8(G32(0x6716), 7), 0x17, 0x4e2);
    s8 col = (s8)P8(G32(0x49d2), 0x186);
    if (G8(0x49fa) == 3) col = (s8)(col + 13);
    if (G8(0x49fa) == 4) col = (s8)(col + 26);
    G8(0x465a) = G8((u16)(0x3648 + (s16)(s8)P8(G32(0x49d2), 0x188) * 0x2c + col));
    if (!bf_outdoor_road()) call_original(0x2843, 0x0bde, 0);
    if (!bf_outdoor_pairs()) call_original(0x2843, 0x0d13, 0);
    if (!bf_outdoor_scatter()) call_original(0x2843, 0x0e9e, 0);
    bf_decorate();
}

// 2843:1244  Setting up a battlefield: the combat pictures for a dungeon (area type 1) or the wilderness, the
// random-encounter pictures, a new 1257-byte map (DS:6716; byte 5 set), and the field built indoors or outdoors.
void bf_setup(void) {
    if (G8(0x49fa) == 1) call_original(0x4c2b, 0x0022, 4, RSEG(0x2843), 0x122c, 0, 0x18);   // "DungCom"
    else call_original(0x4c2b, 0x0022, 4, RSEG(0x2843), 0x1234, 0, 0x21);                  // "WildCom"
    call_original(0x4c2b, 0x0022, 4, RSEG(0x2843), 0x123c, 0x22, 5);                        // "RandCom"
    farptr m = rtl_getmem(0x4e9);
    SETP32(FARPTR(DATA_SEG, 0), 0x6716, m);
    P8(m, 4) = 0; P8(m, 5) = 1; P8(m, 6) = 0;
    if (G8(0x49fa) == 1) bf_build_indoor(); else bf_build_outdoor();
}

// 2843:12E1  Before a fight: every combatant is recalculated and gets a fresh 22-byte action record. Combatants
// beyond the area's count (word 67Ch) leave no body (action byte 13h); all face the party's direction (DS:2F2 by
// facing / 2), enemies (side 1) the opposite way; and enemies' companions on the party side that leave no body get a
// morale value from the area (word 58Ch) if they had none.
void bf_prepare_combatants(void) {
    u16 n = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        chr_recalculate(c);
        n++;
        farptr act = rtl_getmem(0x16);
        SETP32(c, 0x108, act);
        memset(&P8(act, 0), 0, 0x16);
        if (P16(G32(0x49d6), 0x67c) < n) P8(act, 0x13) = 1;
        P8(act, 9) = G8(0x2f2 + (G8(0x6aaf) >> 1));
        if (P8(c, 0x10e) == 1) P8(act, 9) = (u8)((P8(act, 9) + 4) % 8);
        u8 m = P8(c, 0x84) & 0x7f;
        if (P8(c, 0x10e) == 0 && P8(act, 0x13) == 1 && (m == 0 || m > 0x66)) P8(c, 0x84) = (u8)(P16(G32(0x49d6), 0x58c) - 0x80);
    }
}

// 2843:1434  Tries to place combatant `slot` at cell (col, row) of placement pattern `pat` (DS:4440, 11 by 6 cells per
// pattern, per arrangement DS:4658) inside the block (dx, dy). The cell must still be free in the pattern, and the
// square must be empty, on the field and not solid; then the cell is used up.
u8 bf_try_place(u8 pat, s8 dy, s8 dx, s8 row, s8 col, u8 slot) {
    if (col < 0 || row < 0 || col > 10 || row > 5) return 0;
    u16 cell = (u16)((s16)(s8)G8(0x4658) * 0x108 + pat * 0x42 + row * 0x0b + col + 0x4440);
    if (G8(cell) == 0) return 0;
    G8(0x5f27 + slot * 4) = (u8)(col + dx * 6 + dy * 5 + 0x16);
    G8(0x5f28 + slot * 4) = (u8)(row + dy * 5 + 0x0a);
    u8 tile, who;
    combat_look_ahead(&tile, &who, 8, G32(0x65b9 + slot * 4));
    if (who == 0 && tile != 0 && G8(0x2744 + tile * 4) != 0xff) { G8(cell) = 0; return 1; }
    return 0;
}

// 2843:1E31  A battle begins: the 3D view's pictures are freed, "A battle begins..." is shown, and the combat state
// is reset (Magic off, round 0, no fallen, no clouds, area attack flag cleared, morale capped at 100). Then the
// battlefield is set up, the combatants prepared and placed (2843:19F5, still original), the missile work picture
// allocated, the view put on the first combatant, effect event 8 (fear auras and the like) raised for everyone, and
// combat mode (DS:49F3 = 5) entered.
void bf_battle_begins(void) {
    scene_free_anim(FARPTR(DATA_SEG, 0x6abe));
    gfx_free_pic(FARPTR(DATA_SEG, 0x6b01));
    gfx_free_pic(FARPTR(DATA_SEG, 0x6b06));
    G8(0x6b00) = 0xff; G8(0x6b05) = 0xff;
    menu_clear_line();
    text_pause();
    text_put_string(FARPTR(RSEG(0x2843), 0x1e1e), 10, 0x18, 0);
    G8(0x6e30) = 0; G8(0x6de3) = 0; G8(0x6822) = 0; G8(0x6715) = 0;
    SETP32(FARPTR(DATA_SEG, 0), 0x6dbb, 0);
    SETP32(FARPTR(DATA_SEG, 0), 0x5d9a, 0);
    memset(&G8(0x66dd), 0, 0x38);
    wr16(LIN(G32(0x49d6)) + 0x666, 0);
    if (P16(G32(0x49d6), 0x58c) > 100) wr16(LIN(G32(0x49d6)) + 0x58c, 100);
    bf_setup();
    input_poll();
    bf_prepare_combatants();
    input_poll();
    bf_place_all();
    input_poll();
    gfx_alloc_pic(FARPTR(DATA_SEG, 0x6e36), 1, 4, 3, 0x18);
    farptr first = G32(0x5d96);
    P8(G32(0x6716), 2) = (u8)(combat_x(first) - 3);
    P8(G32(0x6716), 3) = (u8)(combat_y(first) - 3);
    combat_prep_view();
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) effects_event(c, 8);
    enemy_strength();
    G8(0x49f3) = 5;
}

// 2184:04C9  After a fight. The combatants up to the first that leaves no body are the party and its allies.
// DS:443B: someone fled (status 3). DS:49FF: the party lost (nobody on the party side still standing, conscious or
// fled; also set aside when the area forbids it, word 5CCh = 1). DS:84DA: someone of the party is still up, so the
// winners collect treasure (2184:0000 and 0355). DS:84D5 counts the fallen. Then, for a victory or retreat, fled
// members come back and the dying become unconscious (or, after a rout, the area is told, word 58Eh = 81h, and each
// member is handled by 34FB:3291); after a defeat each member goes through 34FB:3291 and the area's monster count is
// cleared. DS:84D4 marks a fight the party cannot lose this way; then only treasure and recovery happen.
void combat_after_fight(void) {
    G8(0x84d5) = 0; G8(0x49ff) = 1; G8(0x443b) = 0;
    #define PARTY_LOOP(c) for (farptr c = G32(0x5d96); c && P8(P32(c, 0x108), 0x13) != 1; c = P32(c, 0x104))
    PARTY_LOOP(c) if (P8(c, 0x10c) == 3) G8(0x443b) = 1;
    if (G8(0x84d4)) G8(0x49ff) = 0;
    G8(0x84da) = 0;
    if (G8(0x84d4) == 0) {
        PARTY_LOOP(c) {
            u8 st = P8(c, 0x10c);
            if ((st == 3 || st == 1 || st == 0 || P16(G32(0x49d6), 0x5cc) == 1) && P8(c, 0x10e) == 0 && P8(c, 0x84) < 0x80) G8(0x49ff) = 0;
            if (st == 1 || st == 0) { G8(0x84da) = 1; G8(0x443b) = 0; }
            if (P8(c, 0x10d) == 0 || st == 1) G8(0x84d5)++;
        }
        if (G8(0x84da)) { combat_treasure_xp(FARPTR(DATA_SEG, 0x84d6)); combat_award_xp((s32)rd32(DS_LIN + 0x84d6)); }
        if (G8(0x49ff) == 0) {
            PARTY_LOOP(c) {
                if (G8(0x443b) == 0) {
                    u8 st = P8(c, 0x10c);
                    if (st == 3) { P8(c, 0x10c) = 0; P8(c, 0x10d) = 1; }
                    else if (st == 5) P8(c, 0x10c) = 4;
                    else if (st == 4 && P8(c, 0x11b) != 0) { P8(c, 0x10c) = 0; P8(c, 0x10d) = 1; }
                } else {
                    wr16(LIN(G32(0x49d6)) + 0x58e, 0x81);
                    if (P8(c, 0x10c) == 3) { P8(c, 0x10c) = 0; P8(c, 0x10d) = 1; }
                    else { SETP32(FARPTR(DATA_SEG, 0), 0x5d92, c); party_remove(0, 1); }
                }
            }
        } else {
            u8 stop = 0;
            for (farptr c = G32(0x5d96); c && !stop; c = P32(c, 0x104)) {
                if (P8(P32(c, 0x108), 0x13) == 1) stop = 1;
                else { SETP32(FARPTR(DATA_SEG, 0), 0x5d92, c); party_remove(0, 1); }
            }
            wr16(LIN(G32(0x49d6)) + 0x67c, 0);
        }
    } else {
        for (farptr c = G32(0x5d96); c; c = P32(c, 0x104))
            if (P8(c, 0x10d) == 1 && P8(c, 0x10c) == 0 && P8(c, 0x10e) != 1) {
                G8(0x84da) = 1;
                combat_treasure_xp(FARPTR(DATA_SEG, 0x84d6));
                combat_award_xp((s32)rd32(DS_LIN + 0x84d6));
            }
        for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
            if (P8(c, 0x10c) == 0 || P8(c, 0x10c) == 1) P8(c, 0x10d) = 1;
            if (P8(c, 0x10c) == 5) P8(c, 0x10c) = 4;
        }
    }
    #undef PARTY_LOOP
}

// 2184:0355  Experience after a won fight: every party member still standing gets `xp`, plus a tenth (Pascal longint
// division) when the class's prime requisite is 16 or more (class 0: byte 12h, 2: Strength, 5: byte 11h, 6: byte
// 13h); multi-classed characters get a half (classes 8, 10-14, 16) or a third (9, 15).
void combat_award_xp(s32 xp) {
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        if (P8(c, 0x10d) == 0 || P8(c, 0x10c) == 1) continue;
        s32 get = xp;
        s8 cls = (s8)P8(c, 0x2f);
        if (cls == 0) { if (P8(c, 0x12) > 0x0f) get = xp / 10 + xp; }
        else if (cls == 2) { if (P8(c, 0x10) > 0x0f) get = xp / 10 + xp; }
        else if (cls == 5) { if (P8(c, 0x11) > 0x0f) get = xp / 10 + xp; }
        else if (cls == 6) { if (P8(c, 0x13) > 0x0f) get = xp / 10 + xp; }
        else if (cls == 8 || (cls >= 10 && cls <= 14) || cls == 0x10) get = xp / 2;
        else if (cls == 9 || cls == 0x0f) get = xp / 3;
        wr32(LIN(c) + 0xac, rd32(LIN(c) + 0xac) + (u32)get);
    }
}

// 2184:11AC  Back from a fight: the area learns how many enemies fell (word 590h). Every enemy and every combatant
// that leaves no body is removed (34FB:3291); the party's action records are freed; the first party member is
// selected again.
void combat_back_to_party(void) {
    wr16(LIN(G32(0x49d6)) + 0x590, 0);
    farptr c = G32(0x5d96);
    while (c) {
        if (P8(P32(c, 0x108), 0x13) == 1 || P8(c, 0x10e) == 1) {
            if (P8(c, 0x10d) != 1) wr16(LIN(G32(0x49d6)) + 0x590, (u16)(P16(G32(0x49d6), 0x590) + 1));
            farptr next = P32(c, 0x104);
            SETP32(FARPTR(DATA_SEG, 0), 0x5d92, c);
            party_remove(P8(P32(c, 0x108), 0x13) == 1 ? 1 : 0, 1);
            c = next;
        } else {
            if (P32(c, 0x108)) { rtl_freemem(P32(c, 0x108), 0x16); SETP32(c, 0x108, 0); }
            c = P32(c, 0x104);
        }
    }
    SETP32(FARPTR(DATA_SEG, 0), 0x5d92, G32(0x5d96));
}

// 2184:0916  The screen after a fight: "The party has found Treasure!" (or "The party has won.", "The party has
// fled." with no experience, and the duel versions "You have won the duel." / "You have lost the duel."), then
// "Each character receives n" (a duel: "The duelist receives n") "experience points.", and a prompt to go on.
void combat_result_screen(s32 xp) {
    #define CSTR(o) FARPTR(RSEG(0x2184), (o))
    ui_frame(CSTR(0x083c), 0x0f, 0, 0x16, 0x26, 1, 1);
    u16 msg;
    if (G8(0x443a) == 0 && G8(0x84d4) == 0) { if (G8(0x443b) == 0) msg = 0x0893; else { msg = 0x087f; xp = 0; } }
    else if (G8(0x84d4) == 0 || G8(0x84da) != 0) msg = G8(0x84d4) == 0 ? 0x086c : 0x0855;
    else { msg = 0x083d; xp = 0; }
    text_put_string(CSTR(msg), 10, 3, 1);
    char num[16]; snprintf(num, sizeof num, "%ld", (long)xp);
    const u8 *head = &mem[LIN(CSTR(G8(0x84d4) == 0 ? 0x08c7 : 0x08b1))];
    farptr b = scratch_push(256);
    u8 *s = &P8(b, 0); int n = head[0]; memcpy(s + 1, head + 1, n);
    int k = (int)strlen(num); if (k > 10) k = 10; if (n + k > 255) k = 255 - n;
    memcpy(s + 1 + n, num, k); s[0] = (u8)(n + k);
    text_put_string(b, 10, 5, 1);
    scratch_pop(256);
    text_put_string(CSTR(0x08e0), 10, 7, 1);
    u8 special = 0; static const u8 none[] = "";
    menu_horizontal(&special, 0, 1, 0x0f, 0x0f, 0x0f, &mem[LIN(CSTR(0x08f3))], none);
    #undef CSTR
}

// 2184:12E8  Hired allies take their cut of the coins found (DS:67F4, seven kinds as longints). Every party member
// counts one share; an ally (morale value set, status 0) counts the shares in the low bits of byte 85h. For each coin
// kind the share (coins / shares, kept as a byte) times the allies' shares is removed (as a signed 16-bit amount,
// as in the original). If anything was taken, each such ally " takes and hides his share.".
void combat_allies_share(void) {
    u8 hench = 0, total = 0, took = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        if (P8(c, 0x84) < 0x80 || P8(c, 0x10c) != 0) total++;
        else { hench = (u8)(hench + (P8(c, 0x85) & 7)); total = (u8)(total + (P8(c, 0x85) & 7)); }
    }
    if (hench) {
        for (int k = 0; k <= 6; k++) {
            s32 coins = (s32)rd32(DS_LIN + 0x67f4 + k * 4);
            if (coins > 0) {
                u8 share = (u8)(coins / (s32)total);
                s32 take = (s16)(u16)(share * hench);
                wr32(DS_LIN + 0x67f4 + k * 4, (u32)(coins - take));
                took = 1;
            }
        }
    }
    if (!took) return;
    #define CSTR(o) FARPTR(RSEG(0x2184), (o))
    ui_frame(CSTR(0x12a8), 0x0f, 0, 0x16, 0x26, 1, 1);
    u8 row = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        if (P8(c, 0x84) <= 0x7f || P8(c, 0x10c) != 0 || P8(c, 0x85) == 0) continue;
        farptr b = scratch_push(256);
        u8 *s = &P8(b, 0); const u8 *tail = &mem[LIN(CSTR(0x12a9))];
        int n = P8(c, 0); memcpy(s + 1, &P8(c, 1), n);
        int k = tail[0]; if (n + k > 255) k = 255 - n;
        memcpy(s + 1 + n, tail + 1, k); s[0] = (u8)(n + k);
        text_print_wrapped(b, 1, 10, 0x16, 0x22, (u8)(row + 5), 5);
        scratch_pop(256);
        row = (u8)(row + 2);
    }
    u8 special = 0;
    menu_horizontal(&special, 0, 1, 0x0f, 0x0f, 0x0f, &mem[LIN(CSTR(0x12c5))], &mem[LIN(CSTR(0x12a8))]);
    #undef CSTR
}

// 2843:1403  Is the cell (col, row) outside the 11 by 6 placement pattern in both directions?
u8 bf_cell_outside(s8 row, s8 col) { return (col < 0 || col > 10) && (row < 0 || row > 5); }

// 2843:1577  Finding a square for one party member. Starting from the party's anchor square (DS:4650/4652 by the
// side in DS:4658), it walks rings outward: each ring starts at a corner from the table at 2F6/2FE and steps along
// two edges alternately (directions from 2E2/2F2), trying each square in the 11x6 window with bf_try_place.
// When a ring runs out it moves out a ring (two when a wall blocks the first ring indoors), and when the window is
// exhausted it moves the anchor through up to three neighbouring directions. Returns 0 if no square was found.
// One quirk is kept: the "placed" flag is not set before the loop, and on a path where the first squares fall
// outside the window it is read uninitialised, so its initial value is taken from the same stack byte the
// original would see ([BP-2] = SS:SP-4 on entry).
u8 bf_place_one(u8 pat, u8 placed_garbage) {
    s8 s = (s8)G8(0x4658);
    u8 first = 1, failed = 0, state = 1, ring = 0, k = 0, step = 0, count = 0, placed = placed_garbage;
    s8 bx = (s8)G8(0x4650 + s), by = (s8)G8(0x4652 + s), x0 = 0, y0 = 0, cx = 0, cy = 0;
    #define DIRB(i) G8((u16)((s8)G8(0x4656 + s) * 4 + (i)))
    do {
        u8 d = DIRB(k + 0x2e2) >> 1;
        if (state == 1) {
            u8 e = G8((d + 2) % 4 + 0x2f2);
            x0 = (s8)(G8((k > 0) * 4 + d + 0x2f6) + ring * (s8)G8(e + 0x2736));
            y0 = (s8)(G8((k > 0) * 4 + d + 0x2fe) + ring * (s8)G8(e + 0x273f));
            cx = x0; cy = y0; step = 1; state = 2; count = 1;
        } else if (state == 2) {
            u8 e = G8((d + 1) % 4 + 0x2f2);
            cx = (s8)(x0 + step * (s8)G8(e + 0x2736)); cy = (s8)(y0 + step * (s8)G8(e + 0x273f));
            state = 3; count++;
        } else if (state == 3) {
            u8 e = G8((d + 3) % 4 + 0x2f2);
            cx = (s8)(x0 + step * (s8)G8(e + 0x2736)); cy = (s8)(y0 + step * (s8)G8(e + 0x273f));
            state = 2; step++; count++;
        }
        u8 out = cx < 0 || cy < 0 || cx > 10 || cy > 5;
        if (state > 1 && ((out && !bf_cell_outside(cy, cx)) || (first && (int)count >= (s8)G8(0x4654 + s)) || (!first && count > 11))) {
            ring++;
            if (G8(0x4658) == 0 && (G8(0x4656) & 1) && k == 0 && ring == 1) {
                s8 ex = (s8)(G8(0x6aad) + G8(0x4650 + s)), ey = (s8)(G8(0x6aae) + G8(0x4652 + s));
                u8 blocked = 0;
                for (int j = 1; j <= 3; j++) {
                    u8 dd = DIRB(j + 0x2d2);
                    if (G8(0x49fa) > 1 || combat_dungeon_edge(dd, ey, ex) != 1) blocked = 1;
                }
                if (blocked) ring++;
            }
            state = 1; first = 0;
        }
        if (out) {
            if (bf_cell_outside(cy, cx)) {
                placed = 0; state = 0;
                while (k < 3 && state != 1) {
                    k++;
                    s8 ex = (s8)(G8(0x6aad) + G8(0x4650 + s)), ey = (s8)(G8(0x6aae) + G8(0x4652 + s));
                    u8 dd = DIRB(k + 0x2d2);
                    if (G8(0x49fa) > 1 || combat_dungeon_edge(dd, ey, ex) != 1) {
                        bx = (s8)(G8(0x4650 + s) + G8(dd + 0x2736)); by = (s8)(G8(0x4652 + s) + G8(dd + 0x273f));
                        ring = 0; state = 1;
                    }
                }
                if (state != 1) failed = 1;
            }
        } else placed = bf_try_place(k, by, bx, cy, cx, pat);
    } while (placed == 0 && failed == 0);
    #undef DIRB
    return !failed;
}

// 2843:19F5  Placing everyone on the battlefield. Each side's start point is offset from the party along its facing
// by the area's spacing (word 582h); the enemies face the other way. Each side's 11 by 6 placement patterns (four per
// side, DS:4440) are switched on from the row ranges of the pattern chosen by facing (DS:0306; pattern 4 for the
// second). Then every combatant gets a slot (its size from byte 6Ch) and is placed by 2843:1577 (still original, a
// spiral search). Someone already down becomes a body on the field (and on the fallen list); one that cannot be
// placed is dropped from the fight if it leaves no body. The combatant list ends after the last one placed.
void bf_place_all(void) {
    chr_count_sides();
    for (int s = 1; s <= 0xff; s++) G8(0x5f2a + s * 4) = 0;
    combat_place_all();
    G8(0x4650) = 0; G8(0x4652) = 0;
    G8(0x4656) = G8(0x6aaf) >> 1;
    u16 spacing = P16(G32(0x49d6), 0x582);
    G8(0x4651) = (u8)((s32)spacing * (s8)G8(0x2736 + G8(0x6aaf)) + (s8)G8(0x4650));
    G8(0x4653) = (u8)((s32)spacing * (s8)G8(0x273f + G8(0x6aaf)) + (s8)G8(0x4652));
    G8(0x4657) = (u8)(((G8(0x6aaf) + 4) % 8) >> 1);
    for (int side = 0; side <= 1; side++) G8(0x4654 + side) = (u8)((G8(0x6814 + side) + 1) >> 1);
    for (int side = 0; side <= 1; side++) {
        G8(0x4658) = (u8)side;
        for (int e = 0; e <= 3; e++) {
            u8 pat = e == 1 ? 4 : G8(0x4656 + side);
            for (s8 r = 0; r <= 5; r++)
                for (s8 c = 0; c <= 10; c++) {
                    s8 lo = (s8)G8(0x306 + pat * 0x0c + r * 2), hi = (s8)G8(0x307 + pat * 0x0c + r * 2);
                    G8((u16)(side * 0x108 + e * 0x42 + r * 0x0b + c + 0x4440)) = (lo > c || hi < c) ? 0 : 1;
                }
        }
    }
    u8 slot = 1;
    G8(0x5f2a) = 1;
    farptr prev = G32(0x5d96), c = G32(0x5d96);
    while (c) {
        input_poll();
        SETP32(FARPTR(DATA_SEG, 0), 0x65b9 + slot * 4, c);
        G8(0x4658) = P8(c, 0x10e);
        G8(0x5f29 + slot * 4) = slot;
        G8(0x5f2a + slot * 4) = P8(c, 0x6c) & 7;
        if (bf_place_one(slot, 0)) {      // only the hook can supply the original stack byte; the first square is in the window anyway
            prev = c;
            if (P8(c, 0x10d) == 0) {
                G8(0x5f2a + slot * 4) = 0;
                if (G8(0x84d4) == 0 && P8(P32(c, 0x108), 0x13) == 0) {
                    s8 x = (s8)G8(0x5f27 + slot * 4), y = (s8)G8(0x5f28 + slot * 4);
                    G8(0x6715)++;
                    u8 n = G8(0x6715);
                    u8 *t = &P8(G32(0x6716), (u16)((s16)y * 0x32 + x + 7));
                    G8(0x66dc + n * 7) = *t;
                    *t = 0x1f;
                    SETP32(FARPTR(DATA_SEG, 0), 0x66d6 + n * 7, c);
                    G8(0x66da + n * 7) = (u8)x; G8(0x66db + n * 7) = (u8)y;
                }
            }
            combat_place_all();
            slot++; G8(0x5f2a)++;
        } else {
            G8(0x5f2a + slot * 4) = 0;
            if (P8(P32(c, 0x108), 0x13) == 1) {
                SETP32(FARPTR(DATA_SEG, 0), 0x65b9 + slot * 4, 0);
                SETP32(FARPTR(DATA_SEG, 0), 0x5d92, c);
                party_remove(1, 0);
                c = prev;
            } else { prev = c; slot++; G8(0x5f2a)++; }
        }
        c = P32(c, 0x104);
    }
    SETP32(prev, 0x104, 0);
}

// 2184:0000  Treasure and experience from a won fight. In a special fight (DS:84D4 with area word 5CCh set) the
// experience is just 100 per hit die of the selected character. Otherwise every enemy that did not flee adds its coins
// to the treasure (seven longints at DS:67F4), its experience value (word B8h plus byte B1h x byte BAh) to the total,
// and copies of its items to the treasure list (DS:6810): every item with a value, and up to eight others, each on
// 1d10 <= 3 (none at all where area word 5C6h = 1). The coins then count as experience (copper / 200, silver / 20,
// electrum / 2, gold, platinum x 5, gems x 250, jewellery x 2200) and every magic item in the treasure 400 per plus.
// The total is divided by the number of party members (area word 67Ch minus the fallen, DS:84D5).
void combat_treasure_xp(farptr out) {
    if (G8(0x84d4) && P16(G32(0x49d6), 0x5cc) != 0) {
        wr32(LIN(out), (u32)(s32)(s16)(u16)((s16)(s8)P8(G32(0x5d92), 0x73) * 100));
        return;
    }
    s32 xp = 0; u8 extra = 0;
    for (farptr c = G32(0x5d96); c; c = P32(c, 0x104)) {
        farptr it = P32(c, 0xc8);
        if (P8(c, 0x10e) != 1 || P8(c, 0x10c) == 3) continue;
        G8(0x443a) = 1;
        for (int k = 0; k <= 6; k++) wr32(DS_LIN + 0x67f4 + k * 4, rd32(DS_LIN + 0x67f4 + k * 4) + P16(c, 0x88 + k * 2));
        xp += (s32)P16(c, 0xb8) + (s32)(s16)(u16)(P8(c, 0xb1) * P8(c, 0xba));
        if (P16(G32(0x49d6), 0x5c6) == 1) continue;
        for (; it; it = P32(it, 0x2a)) {
            if (!(P16(it, 0x3a) > 0 || (extra < 8 && roll_dice(10, 1) <= 3))) continue;
            extra++;
            farptr head = G32(0x6810);
            farptr n = rtl_getmem(0x3f);
            SETP32(FARPTR(DATA_SEG, 0), 0x6810, n);
            chr_item_name(0, 0, 0, 0, it);
            memmove(&P8(n, 0), &P8(it, 0), 0x3f);
            P8(n, 0x34) = 0;
            SETP32(n, 0x2a, head);
        }
    }
    xp += (s32)rd32(DS_LIN + 0x67f4) / 200;
    xp += (s32)rd32(DS_LIN + 0x67f8) / 20;
    xp += (s32)rd32(DS_LIN + 0x67fc) / 2;
    xp += (s32)rd32(DS_LIN + 0x6800);
    xp += (s32)rd32(DS_LIN + 0x6804) * 5;
    xp += (s32)rd32(DS_LIN + 0x6808) * 250;
    xp += (s32)rd32(DS_LIN + 0x680c) * 2200;
    for (farptr t = G32(0x6810); t && t != G32(0x5d9a); t = P32(t, 0x2a))
        if ((s8)P8(t, 0x32) > 0) xp += (s32)(s16)(u16)((s16)(s8)P8(t, 0x32) * 400);
    u16 members = (u16)(P16(G32(0x49d6), 0x67c) - G8(0x84d5));
    wr32(LIN(out), (u32)(xp / (s32)members));
}

// 2184:0ECD  The treasure screen after a fight: "View Take Pool Share" (Share only when there are coins; just "View
// Take Pool" with items only; "View Pool Exit" with nothing), plus "Detect" when the selected character has Detect
// Magic memorized and there are items. Exit with treasure still lying there asks "There is still treasure left. Do you
// want to go back and claim your treasure?". Home and End change the selected character. Taking (2184:0D38), pooling
// and sharing (3FA9) and the treasure check (3FA9:0F7E) are still original.
void combat_treasure_screen(void) {
    #define CS_(o) (&mem[LIN(FARPTR(RSEG(0x2184), (o)))])
    farptr v = scratch_push(4);                     // items (0), coins (1), special (2)
    farptr items = v, coins = FARPTR(FP_SEG(v), FP_OFF(v) + 1), special = FARPTR(FP_SEG(v), FP_OFF(v) + 2);
    static const u8 none[] = "";
    u8 done = 0, spell = 0;
    hud_draw_screen();
    P8(coins, 0) = 0;
    do {
        treasure_left(&P8(items, 0), &P8(coins, 0));
        u8 menu[256]; memcpy(menu, CS_(0x0e2e), CS_(0x0e2e)[0] + 1);
        u8 tail[14]; memcpy(tail, CS_(0x0e3d), CS_(0x0e3d)[0] + 1);
        u8 detect = 0;
        if (P8(items, 0))
            for (int k = 0; k < 0x15 && !detect; k++) {
                u8 sp = P8(G32(0x5d92), 0x17 + k);
                if (sp == 5 || sp == 0x0b) { detect = 1; spell = sp; }
            }
        if (detect) memcpy(tail, CS_(0x0e43), CS_(0x0e43)[0] + 1);
        const u8 *head = 0;
        if (P8(coins, 0)) head = CS_(0x0e50); else if (P8(items, 0)) head = CS_(0x0e65);
        if (head) {
            int n = head[0]; memcpy(menu + 1, head + 1, n);
            int k = tail[0]; if (n + k > 255) k = 255 - n;
            memcpy(menu + 1 + n, tail + 1, k); menu[0] = (u8)(n + k);
        }
        u8 key = menu_horizontal(&P8(special, 0), 1, 1, 0x0f, 10, 0x0d, menu, none);
        if (key == 'V') sheet_view(special);
        else if (key == 'T') combat_take(items, coins);
        else if (key == 'P') { if (P8(special, 0) == 0) treasure_pool(); }
        else if (key == 'S') treasure_share();
        else if (key == 'D') spell_cast(special, 0, 0, spell);
        else if (key == 'E' || key == 0) {
            treasure_left(&P8(items, 0), &P8(coins, 0));
            if (P8(coins, 0) == 0 && P8(items, 0) == 0) done = 1;
            else {
                u8 yn[256]; memcpy(yn, CS_(0x0e74), CS_(0x0e74)[0] + 1);
                farptr b = scratch_push(256);
                memcpy(&P8(b, 0), CS_(0x0e7d), CS_(0x0e7d)[0] + 1);
                text_print_wrapped(b, 1, 10, 0x16, 0x26, 0x11, 1);
                memcpy(&P8(b, 0), CS_(0x0e9d), CS_(0x0e9d)[0] + 1);
                text_print_wrapped(b, 0, 0x0f, 0x16, 0x26, 0x11, 1);
                scratch_pop(256);
                if (ecl_menu(0, 0, 0x0f, 10, 0x0d, yn, none) == 1) done = 1;
                else text_clear_box(1, 0x11, 0x26, 0x16);
            }
        } else if (key == 'G' || key == 'O') {
            party_select_move(key);
            hud_draw_roster(G32(0x5d92));
        }
    } while (!done);
    scratch_pop(4);
    #undef CS_
}

// 3FA9:0F7E  What treasure is left: *coins if any of the seven coin counts is non-zero, *items if the treasure list
// (DS:6810) is not empty.
void treasure_left(u8 *items, u8 *coins) {
    *coins = 0; *items = 0;
    for (int k = 0; k <= 6; k++) if (rd32(DS_LIN + 0x67f4 + k * 4)) *coins = 1;
    if (G32(0x6810)) *items = 1;
}

// 2184:0D38  Take: with only items, the item list (2184:0C06); with only coins, the money screen (3FA9:0CEB); with
// both, "Take: Money Items Exit" until one kind runs out. Home and End change the selected character.
void combat_take(farptr items, farptr coins) {
    if (P8(coins, 0) == 0) { combat_take_items(); return; }
    if (P8(items, 0) == 0) { take_money(); hud_draw_screen(); return; }
    u8 done = 0, special = 0;
    do {
        u8 key = menu_horizontal(&special, 1, 1, 0x0f, 10, 0x0d, &mem[LIN(FARPTR(RSEG(0x2184), 0x0d27))], &mem[LIN(FARPTR(RSEG(0x2184), 0x0d20))]);
        if (key == 'M') { take_money(); hud_draw_screen(); }
        else if (key == 'I') combat_take_items();
        else if (key == 'E' || key == 0) done = 1;
        else if (key == 'G' || key == 'O') party_select_move(key);
        hud_draw_roster(G32(0x5d92));
        treasure_left(&P8(items, 0), &P8(coins, 0));
        if (P8(coins, 0) == 0 || P8(items, 0) == 0) done = 1;
    } while (!done);
}

// 2184:0C06  Taking items from the treasure: the item list (2184:0B14, still original) until Esc; each item taken with
// T or Enter is given to the selected character (22EC:0239, still original), unless that fails (*full), and then
// unlinked from the treasure list and freed. Leaves when the treasure list is empty.
void combat_take_items(void) {
    farptr v = scratch_push(12);                 // key (0), chosen item (2), list (6), position (10)
    farptr key = v, item = FARPTR(FP_SEG(v), FP_OFF(v) + 2), list = FARPTR(FP_SEG(v), FP_OFF(v) + 6), pos = FARPTR(FP_SEG(v), FP_OFF(v) + 10);
    SETP32(item, 0, 0);
    SETP32(list, 0, G32(0x6810));
    wr16(LIN(pos), 0);
    u8 done = 0;
    do {
        call_original(0x2184, 0x0b14, 8, FP_SEG(key), FP_OFF(key), FP_SEG(item), FP_OFF(item), FP_SEG(list), FP_OFF(list), FP_SEG(pos), FP_OFF(pos));
        u8 k = P8(key, 0);
        if (k == 'T' || k == 13) {
            farptr full = scratch_push(2);
            P8(full, 0) = 0;
            farptr it = P32(item, 0);
            call_original(0x22ec, 0x0239, 4, FP_SEG(it), FP_OFF(it), FP_SEG(full), FP_OFF(full));
            u8 f = P8(full, 0);
            scratch_pop(2);
            if (!f) {
                if (G32(0x6810) == it) SETP32(FARPTR(DATA_SEG, 0), 0x6810, P32(it, 0x2a));
                else {
                    farptr p = G32(0x6810);
                    while (P32(p, 0x2a) != it) p = P32(p, 0x2a);
                    SETP32(p, 0x2a, P32(it, 0x2a));
                }
                SETP32(it, 0x2a, 0);
                if (it) rtl_freemem(it, 0x3f);
                if (!G32(0x6810)) done = 1;
            }
        } else done = 1;
    } while (!done);
    scratch_pop(12);
    hud_draw_screen();
}

// 3941:032F  Overlays picture `src` onto `dst`: bitmap bytes (from 17h, as many as src's size, word 11h) are ORed,
// mask bytes (far pointer 13h) ANDed. In display mode 1 (DS:4A1E) the mask is a quarter of the size of the picture at
// DS:5EB2. The loops run until the counter reaches size - 1, as in the original (so at least once).
void pic_overlay(farptr dst, farptr src) {
    u8 mode = G8(0x4a1e);
    if (mode == 0 || mode == 2) {
        u16 n = P16(src, 0x11);
        for (u16 i = 0; ; i++) {
            P8(dst, 0x17 + i) |= P8(src, 0x17 + i);
            P8(P32(dst, 0x13), i) &= P8(P32(src, 0x13), i);
            if (i == (u16)(n - 1)) break;
        }
    } else if (mode == 1) {
        u16 n = P16(src, 0x11);
        for (u16 i = 0; ; i++) { P8(dst, 0x17 + i) |= P8(src, 0x17 + i); if (i == (u16)(n - 1)) break; }
        u16 m = (u16)(P16(G32(0x5eb2), 0x11) >> 2);
        for (u16 i = 0; ; i++) { P8(P32(dst, 0x13), i) &= P8(P32(src, 0x13), i); if (i == (u16)(m - 1)) break; }
    }
}

// 3941:0482  Builds the selected character's combat icon: the head ("CHEAD") and body ("CBODY") pictures for its
// icon set (byte C0h, letter from DS:08C5) are loaded (4C2B:01C8, still original) into icon slot 11 and overlaid on
// its two combat pictures (DS:5E5A by byte BFh). With `recolor` the six colour bytes (C1h-C6h, two nibbles each) are
// applied to both pictures, through the colour positions at DS:3CF6.
void chr_build_icon(u8 recolor) {
    file_set_select(3);
    farptr c = G32(0x5d92);
    farptr name = scratch_push(16);
    static const u16 part[2] = { 0x0476, 0x047c };     // "CHEAD", "CBODY"
    for (int i = 0; i < 2; i++) {
        const u8 *p = &mem[LIN(FARPTR(RSEG(0x3941), part[i]))];
        u8 *s = &P8(name, 0); memcpy(s, p, p[0] + 1);
        s[1 + s[0]] = G8(0x08c5 + P8(c, 0xc0)); s[0]++;
        if (i == 0) sprite_load(FARPTR(FP_SEG(name), FP_OFF(name)), P8(c, 0xbd), 0x0b);
        else sprite_load(FARPTR(FP_SEG(name), FP_OFF(name)), P8(c, 0xbe), P8(c, 0xbf));
    }
    scratch_pop(16);
    u8 slot = P8(c, 0xbf);
    pic_overlay(G32(0x5e5a + slot * 8), G32(0x5eb2));
    pic_overlay(G32(0x5e5e + slot * 8), G32(0x5eb6));
    if (recolor) {
        farptr t = scratch_push(32);              // to (0..15), from (16..31)
        for (int i = 0; i <= 15; i++) { P8(t, i) = (u8)i; P8(t, 16 + i) = (u8)i; }
        for (int k = 1; k <= 6; k++) {
            u8 pos = G8(0x3cf5 + k), v = P8(c, 0xc0 + k);
            P8(t, pos) = v & 0x0f;
            P8(t, pos + 8) = v >> 4;
        }
        farptr from = FARPTR(FP_SEG(t), FP_OFF(t) + 16);
        gfx_recolor_pic(G32(0x5e5a + slot * 8), 0, 0, t, from);
        gfx_recolor_pic(G32(0x5e5e + slot * 8), 0, 0, t, from);
        scratch_pop(32);
    }
    icon_free_slot(0x0b);
    file_set_restore();
    keys_flush();
}

// 4C2B:01C8  Loading a pair of sprites into slot n (DS:5E5A + n*8, and the second half at 5E5E with id + 80h).
// CHEADx/CBODYx (portrait parts) drop their last letter, and a final T adds 40h to the id. COMSPR and ICON load as
// named; ICON is then recoloured with the tables at DS:0CD2/0CF2. Anything else gets the area number appended (as a
// one-character string, so only the first digit) and is recoloured with DS:0CD2/0CE2.
static void sprite_load_one(u16 var, farptr name, u8 id) {
    char c[64]; snprintf(c, sizeof c, "%s", pstr(name));
    if (!gfx_load_pic(FARPTR(DATA_SEG, var), 0, 1, id, c))
        call_original(0x1464, 0x0111, 7, FP_SEG(name), FP_OFF(name), id, 1, 0, DATA_SEG, var);
}
static int pstr_eq(const u8 *a, const u8 *b) { return a[0] == b[0] && !memcmp(a + 1, b + 1, a[0]); }
void sprite_load(farptr name, u8 id, u8 slot) {
    farptr sc = scratch_push(0x100); u8 *t = &mem[LIN(sc)];
    memcpy(t, &mem[LIN(name)], mem[LIN(name)] + 1u);
    const u8 *cs = &mem[LIN(FARPTR(RSEG(0x4c2b), 0))];
    u8 five[6]; five[0] = t[0] < 5 ? t[0] : 5; memcpy(five + 1, t + 1, five[0]);
    u16 a = (u16)(0x5e5a + slot * 8), b = (u16)(0x5e5e + slot * 8);
    if (pstr_eq(five, cs + 0x1b0) || pstr_eq(five, cs + 0x1b6)) {
        u8 last = t[t[0]];
        if (last >= 'a' && last <= 'z') last -= 0x20;
        if (last == 'T') id += 0x40;
        if (t[0]) t[0]--;                                   // Copy(name, 1, len-1)
        sprite_load_one(a, sc, id);
        sprite_load_one(b, sc, (u8)(id + 0x80));
    } else if (pstr_eq(t, cs + 0x1bc) || pstr_eq(t, cs + 0x1c3)) {
        sprite_load_one(a, sc, id);
        sprite_load_one(b, sc, (u8)(id + 0x80));
        if (pstr_eq(t, cs + 0x1c3)) {
            gfx_recolor_pic(G32(a), 0, 0, FARPTR(DATA_SEG, 0x0cf2), FARPTR(DATA_SEG, 0x0cd2));
            gfx_recolor_pic(G32(b), 0, 0, FARPTR(DATA_SEG, 0x0cf2), FARPTR(DATA_SEG, 0x0cd2));
        }
    } else {
        char num[8]; snprintf(num, sizeof num, "%u", G8(0x5376));
        if (t[0] < 255) t[++t[0]] = (u8)num[0];
        sprite_load_one(a, sc, id);
        gfx_recolor_pic(G32(a), 0, 0, FARPTR(DATA_SEG, 0x0ce2), FARPTR(DATA_SEG, 0x0cd2));
        sprite_load_one(b, sc, (u8)(id + 0x80));
        gfx_recolor_pic(G32(b), 0, 0, FARPTR(DATA_SEG, 0x0ce2), FARPTR(DATA_SEG, 0x0cd2));
    }
    scratch_pop(0x100);
    keys_flush();
}

// 2184:1527  After a battle: the aftermath, back to exploring, and unless the whole party died (DS:49FF set and not a
// fled fight, DS:84D4) the allies' share, the result screen with the experience, the treasure, and freeing the
// treasure's item list (DS:6810). A dead party gets "The END!". Either way the battle's leftovers in the area state
// are cleared.
void combat_end(void) {
    farptr a = G32(0x49d6);
    wr16(LIN(a) + 0x58e, 0);
    G8(0x443a) = 0; G8(0x49f3) = 6;
    combat_after_fight();
    combat_back_to_party();
    if (!G8(0x49ff) || G8(0x84d4)) {
        combat_allies_share();
        combat_result_screen((s32)G32(0x84d6));
        combat_treasure_screen();
        for (farptr p = G32(0x6810); p; ) { farptr n = P32(p, 0x2a); rtl_freemem(p, 0x3f); p = n; }
        SETG32(0x6810, 0);
    } else {
        wr16(LIN(a) + 0x58e, 0x80);
        ui_frame(FARPTR(RSEG(0x2184), 0x14ce), 0x0f, 0, 0x16, 0x26, 1, 1);                       // "The END!"
        G8(0x5e24) = 2; G8(0x5e25) = 6;
        text_print_wrapped(FARPTR(RSEG(0x2184), 0x14d7), 1, 10, 0x16, 0x25, 5, 2);
        text_prompt(FARPTR(RSEG(0x2184), 0x150d), 0x0d);                                          // "Press any key to continue"
    }
    G8(0x4a00) = 1;
    wr16(LIN(a) + 0x6e0, 0); wr16(LIN(a) + 0x6e2, 0); wr16(LIN(a) + 0x6e4, 0); wr16(LIN(a) + 0x5c6, 0); wr16(LIN(a) + 0x5cc, 0);
}

// 3941:0EC4  Loading monster `id` into rec: the record from MON<area>CHA.DAX (or "Unable to load monster" and quit),
// its colours rolled afresh (the body colour fixed at C4h), then its effects (MON<area>SPC, 9-byte nodes) and items
// (MON<area>ITM, 3Fh-byte nodes) built into lists. The area is one character of Str(DS:5376).
static u16 mon_dax(farptr name, farptr ext, u8 id, farptr data, farptr size) {
    call_original(0x1802, 0x0716, 9, FP_SEG(name), FP_OFF(name), FP_SEG(ext), FP_OFF(ext), id, FP_SEG(size), FP_OFF(size), FP_SEG(data), FP_OFF(data));
    return P16(size, 0);
}
void monster_load_rec(farptr rec, u8 id) {
    #define MCS(o) FARPTR(RSEG(0x3941), (o))
    farptr sc = scratch_push(0x40), name = sc, data = sc + 0x20, size = sc + 0x24;
    char a[8]; snprintf(a, sizeof a, "%u", G8(0x5376));
    #define MONNAME(kind) do { u8 *t = &P8(name, 0); t[0] = 3; memcpy(t + 1, "MON", 3); t[++t[0]] = (u8)a[0]; const u8 *k = &mem[LIN(MCS(kind))]; memcpy(t + 1 + t[0], k + 1, k[0]); t[0] = (u8)(t[0] + k[0]); } while (0)
    MONNAME(0x0e9c);
    if (!mon_dax(name, MCS(0x0ea0), id, data, size)) { text_prompt(MCS(0x0ea5), 0x0f); game_quit(); }
    memmove(&mem[LIN(rec)], &mem[LIN(P32(data, 0))], 0x11d);
    static const u16 zero[] = { 200, 0xca, 0x7f, 0x81, 0x104, 0x106, 0x108, 0x10a };
    for (int i = 0; i < 8; i++) wr16(LIN(rec) + zero[i], 0);
    u8 r = P8(rec, 0x2e);
    if (r == 1 || r == 3 || r == 5) P8(rec, 0xc0) = 1; else if (r == 2 || r == 4 || r == 6 || r == 7) P8(rec, 0xc0) = 2;
    for (int k = 1; k <= 3; k++) P8(rec, 0xc0 + k) = (u8)(roll_dice(6, 1) * 0x11 - 0x80);
    P8(rec, 0xc4) = 0xc4;
    for (int k = 5; k <= 6; k++) P8(rec, 0xc0 + k) = (u8)(roll_dice(6, 1) * 0x11 - 0x80);
    rtl_freemem(P32(data, 0), P16(size, 0));
    G8(0x557c) = 1;
    static const struct { u16 kind, sz, link, field; } part[2] = { { 0x0ebc, 9, 5, 0x7f }, { 0x0ec0, 0x3f, 0x2a, 200 } };
    for (int pi = 0; pi < 2; pi++) {
        MONNAME(part[pi].kind);
        u16 n = mon_dax(name, MCS(0x0ea0), id, data, size);
        if (!n) continue;
        farptr node = rtl_getmem(part[pi].sz); wr32(LIN(rec) + part[pi].field, node);
        for (u16 off = 0; ; ) {
            memmove(&mem[LIN(node)], &mem[LIN(P32(data, 0) + off)], part[pi].sz);
            wr32(LIN(node) + part[pi].link, 0);
            off = (u16)(off + part[pi].sz);
            if (off < n) { farptr nx = rtl_getmem(part[pi].sz); wr32(LIN(node) + part[pi].link, nx); node = nx; }
            if (off >= n) break;
        }
        rtl_freemem(P32(data, 0), n);
    }
    G8(0x557c) = 0;
    keys_flush();
    scratch_pop(0x40);
    #undef MONNAME
    #undef MCS
}

// 4C2B:04FC  Drawing a figure: slot `figure`, pose b (0 ready, 1 action), facing (4 and up: mirrored), at view square
// (vy, vx) - three columns/rows per square. The sprite is copied into a temporary picture (the pixel data a quarter as
// long in mode 4A1E=1), drawn with 1464:0D71 and freed.
void figure_draw(u8 figure, u8 b, u8 facing, s8 vy, s8 vx) {
    farptr pic = G32(figure * 8 + b * 4 + 0x5e5a);
    if (!pic) return;
    farptr tv = scratch_push(4); SETP32(tv, 0, 0);
    call_original(0x1464, 0x248e, 6, P16(pic, 0), P16(pic, 2), 1, 1, FP_SEG(tv), FP_OFF(tv));
    farptr tmp = P32(tv, 0);
    if (facing > 3) call_original(0x1464, 0x15c7, 4, FP_SEG(pic), FP_OFF(pic), FP_SEG(tmp), FP_OFF(tmp));
    else {
        memmove(&mem[LIN(tmp + 0x17)], &mem[LIN(pic + 0x17)], P16(pic, 0x11));
        u16 n = G8(0x4a1e) == 1 ? (u16)(P16(pic, 0x11) >> 2) : P16(pic, 0x11);
        memmove(&mem[LIN(P32(tmp, 0x13))], &mem[LIN(P32(pic, 0x13))], n);
    }
    call_original(0x1464, 0x0d71, 8, (u16)(vx * 3), (u16)(vy * 3), 0, 1, FP_SEG(tmp), FP_OFF(tmp), G16(0x4a22), G16(0x4a20));
    call_original(0x1464, 0x2595, 2, FP_SEG(tv), FP_OFF(tv));
    scratch_pop(4);
}

// Hook entry points for the nested battlefield routines (called by original code only through the corner fallback):
// the parent's locals are read from its frame, whose BP is the routine's one parameter.
static BfCell bf_cell_from_frame(u16 bp) {
    farptr ss = scratch_push(0); u32 base = ((u32)FP_SEG(ss) << 4);
    BfCell b; b.dx = (s8)mem[base + (u16)(bp - 1)]; b.dy = (s8)mem[base + (u16)(bp - 2)]; b.mx = (s8)mem[base + (u16)(bp - 3)]; b.my = (s8)mem[base + (u16)(bp - 4)];
    b.w0 = mem[base + (u16)(bp - 7)]; b.w6 = mem[base + (u16)(bp - 8)]; b.w2 = mem[base + (u16)(bp - 9)];
    return b;
}
int bf_hook_floor_west(u16 bp) { BfCell b = bf_cell_from_frame(bp); bf_floor_west(&b); return 1; }
int bf_hook_north(u16 bp) { BfCell b = bf_cell_from_frame(bp); bf_north(&b); return 1; }
int bf_hook_corner_nw(u16 bp) { BfCell b = bf_cell_from_frame(bp); return bf_corner_nw(&b); }
int bf_hook_corner_ne(u16 bp) { BfCell b = bf_cell_from_frame(bp); return bf_corner_ne(&b); }

// 2184:0B14  The treasure's item list ("Loot", "Items: Take"): names worked out, the list shown; the key, the current
// item and its index come back through the four VAR parameters.
void loot_list(farptr index_var, farptr cur_var, farptr result, farptr key_out) {
    ui_frame(FARPTR(RSEG(0x2184), 0x0b02), 0x0f, 0, 0x16, 0x26, 1, 1);                                   // "Loot"
    for (farptr p = G32(0x6810); p; p = P32(p, 0x2a)) chr_item_name(0, 0, 0, 0, p);
    farptr cur = P32(cur_var, 0); s16 idx = (s16)P16(index_var, 0); u8 redraw = 1;
    u8 key = menu_list(&cur, &idx, &redraw, 1, G32(0x6810), 0x16, 0x26, 4, 1, 0x0f, 10, 0x0d,
                       &mem[LIN(FARPTR(RSEG(0x2184), 0x0b0f))], &mem[LIN(FARPTR(RSEG(0x2184), 0x0b07))]);
    SETP32(cur_var, 0, cur); wr16(LIN(index_var), (u16)idx);
    P8(key_out, 0) = key;
    SETP32(result, 0, P32(cur_var, 0));
}

// ---- 2366:0411  A new monster: a fresh 11Dh-byte record read from the MON files (3941:0EC4), and its item and
// effect lists (+C8h, +7Fh) handed back as well. ----
void monster_load(u8 id, farptr *rec, farptr *items, farptr *effects) {
    *rec = 0;
    farptr r = rtl_getmem(0x11d);
    *rec = r;
    monster_load_rec(r, id);
    *items = P32(r, 0xc8);
    *effects = P32(r, 0x7f);
}

// ---- 3941:1262  A monster joins the party as a non-player character (at most 8 in all, DS:49D6->67Ch): its record
// is loaded, +ABh gets Random(256), one of its classes with a level (+96..+9C) is drawn (fighter if none), and that
// class and the sex (+9Eh) pick +BCh from one of the 5-entry tables DS:8D7 (cleric), 8EB (magic-user), 8F5 (thief)
// or 8E1 (others) and +BBh from the 8-entry table DS:8C7. Then it joins the party and the icons are rebuilt.
// The drawn list lives in the original's frame just below the record pointer: with all 7 classes the last entry
// lands on the pointer's low byte, which the join then uses. Kept. ----
void npc_add(u8 id) {
    if (P16(G32(0x49d6), 0x67c) >= 8) return;
    farptr r = rtl_getmem(0x11d), rec = r;
    monster_load_rec(r, id);
    P8(r, 0xab) = (u8)rtl_random(0x100);
    u8 list[8] = {0}, n = 0;
    for (u8 c = 0; c <= 6; c++) if ((s8)P8(r, 0x96 + c) > 0) {
        n++; list[n] = c;
        if (n == 7) r = (r & 0xffffff00) | c;
    }
    if (n == 0) { n = 1; list[1] = 2; }
    u8 k = list[roll_dice(n, 1)];
    s16 sex = (s8)P8(rec, 0x9e);
    u16 tab = k == 0 ? 0x8d7 : k == 5 ? 0x8eb : k == 6 ? 0x8f5 : 0x8e1;
    u8 d = roll_dice(5, 1);
    P8(rec, 0xbc) = G8((u16)(sex * 5 + d + tab));
    d = roll_dice(8, 1);
    P8(rec, 0xbb) = G8((u16)(sex * 8 + d + 0x8c7));
    chr_join_party(r);
    chr_build_icon(1);
}
