// The level map and the 3D renderer (original overlay unit at 491C).
//
// A level is a 16x16 grid (far pointer DS:6A5C; see docs for the four tables). The picture window
// is an 11x11 grid of 8x8 tiles. Nothing is scaled: a wall type (1-15) is a recipe of 156 tile
// numbers (far pointer DS:6A58, loaded from WALLDEF) holding ten ready-made shapes, and the
// renderer stamps those shapes back to front.
//
//   piece  shape (tiles)   what it is
//     0      1 x 2         wall facing the party, three squares away
//     1, 2   1 x 4         side wall on the left / right, far
//     3      3 x 4         facing wall, two squares away
//     4, 5   2 x 8         side wall left / right, middle distance
//     6      7 x 8         facing wall, right in front
//     7, 8   2 x 11        side wall left / right, beside the party
//     9      1 x 2         filler where a far facing wall ends
#include "poolrad.h"
#include "gfx.h"
#include "rtl.h"
#include "map.h"
#include <stdio.h>
#include "dax.h"

#define DX(d) ((s8)G8(0x2736 + (d)))          // step east for each of the 8 compass directions
#define DY(d) ((s8)G8(0x273f + (d)))          // step south

// 491C:031F
int map_in_bounds(s8 a, s8 b) { return b < 16 && b >= 0 && a < 16 && a >= 0; }

static int map_wraps(void) { return !(G8(0x84dc) == 0 || G8(0x84dc) == 10); }   // every area but 0 and 10 wraps round at its edges

// 491C:047B  Wall type on side `dir` (0 N, 2 E, 4 S, 6 W) of square (x, y); 0 is open. Outside
// the grid the answer is 0 in areas 0 and 10, and elsewhere the map wraps round.
u8 map_wall(u8 dir, s8 y, s8 x) {
    if (!map_in_bounds(y, x) && !map_wraps()) return 0;
    if (x > 15) x = 0; if (x < 0) x = 15;
    if (y > 15) y = 0; if (y < 0) y = 15;
    farptr map = G32(0x6a5c); u16 i = (u16)(y * 16 + x);
    switch (dir) {
    case 0: return P8(map, i) >> 4;
    case 2: return P8(map, i) & 15;
    case 4: return P8(map, 0x100 + i) >> 4;
    case 6: return P8(map, 0x100 + i) & 15;
    }
    return 0;
}

// 491C:057A  Event number of square (x, y), with the same edge rules.
u8 map_event(s8 y, s8 x) {
    if (!map_in_bounds(y, x) && !map_wraps()) return 0;
    if (x > 15) x = 0; if (x < 0) x = 15;
    if (y > 15) y = 0; if (y < 0) y = 15;
    return P8(G32(0x6a5c), 0x200 + y * 16 + x);
}

// 4CA0:015D  Put one 8x8 symbol at tile position (row, col). Symbols live in five sets, each a
// picture with one frame per symbol (far pointers at DS:5E3A): numbers 1-45, 46-115, 116-185 and
// 186-255 are the four loadable sets (wall tiles), 256-286 the fixed set used by the Area map.
// With `to_buffer` the symbol is merged into the back buffer (`flags` bit 0 = honour transparency);
// otherwise it is copied to a one-frame scratch picture and drawn straight on the screen.
// Returns 0 for a symbol number outside every set (the original stops with an error message).
int sym_put(u8 flags, u8 to_buffer, u16 sym, s8 row, s8 col) {
    u8 set;
    if (sym != 0 && sym <= 0x2d) set = 0;
    else if (sym >= 0x2e && sym <= 0x73) set = 1;
    else if (sym >= 0x74 && sym <= 0xb9) set = 2;
    else if (sym >= 0xba && sym <= 0xff) set = 3;
    else if (sym >= 0x100 && sym <= 0x11e) set = 4;
    else return 0;
    farptr pic = G32(0x5e3a + set * 4);
    if (!pic) return 1;
    u16 frame = (u16)(sym - G16(0x2722 + set * 2));
    if (!to_buffer) {
        farptr scratch = G32(0x5e4e);
        memmove(&mem[LIN(scratch) + 0x17], &mem[LIN(pic) + 0x17 + (u16)(frame * P16(pic, 0x11))], P16(pic, 0x11));
        gfx_draw_pic(scratch, row, col);
    } else gfx_merge_pic(G32(0x4a20), pic, flags, (u8)frame, row, col);
    return 1;
}

static void put_tile(u8 flags, u16 tile, s16 row, s16 col) {
    if (!sym_put(flags, 1, tile, (s8)row, (s8)col)) call_original(0x4ca0, 0x015d, 5, (u16)col, (u16)row, tile, 1, flags);
}

// 491C:0174  Paint the empty view into the back buffer: sky, two horizon bands, floor.
void view_draw_background(void) {
    farptr back = G32(0x4a20);
    gfx_fill_pic(back, G8(0x6a60), 0x2b, 11, 0x10, 2);
    gfx_fill_pic(back, G8(0x6a61), 1, 11, 0x3b, 2);
    gfx_fill_pic(back, G8(0x6a62), 2, 11, 0x3c, 2);
    gfx_fill_pic(back, G8(0x6a63), 0x2a, 11, 0x3e, 2);
}

// 491C:0211  Stamp one piece of wall type `type` with its top-left tile at (row, col) of the
// 11x11 view, skipping tiles that fall outside it and tile number 0 (see-through).
void view_draw_piece(u8 piece, u8 type, s8 row, s8 col) {
    u8 idx = G8(0x0c8c + piece);
    s16 col_end = (s16)(G8(0x0c96 + piece) + col - 1), row_end = (s16)(G8(0x0ca0 + piece) + row - 1);
    farptr walls = G32(0x6a58);
    for (s16 r = row; r <= row_end; r++)
        for (s16 c = col; c <= col_end; c++) {
            if (r >= 0 && r < 11 && c >= 0 && c < 11) {
                u8 tile = mem[((u32)FP_SEG(walls) << 4) + (u16)(FP_OFF(walls) + type * 0x9c + idx - 0x9c)];
                if (tile) put_tile(1, tile, (s16)(r + 2), (s16)(c + 2));
            }
            idx++;
        }
}

// 491C:0000  The overhead Area map: an 11x11 window of the level around the party, one tile per
// square chosen by which of its four sides have walls, and an arrow for the party.
void view_draw_area_map(u8 dir, s8 y, s8 x) {
    s8 x0 = (s8)(x - 5); if (x0 < 0) x0 = 0; if (x0 > 5) x0 = 5;
    s8 y0 = (s8)(y - 5); if (y0 < 0) y0 = 0; if (y0 > 5) y0 = 5;
    for (s8 r = 0; r <= 10; r++)
        for (s8 c = 0; c <= 10; c++) {
            s8 yy = (s8)(r + y0), xx = (s8)(c + x0);
            u16 tile = map_wall(0, yy, xx) ? 0x105 : 0x104;
            if (map_wall(2, yy, xx)) tile += 2;
            if (map_wall(4, yy, xx)) tile += 4;
            if (map_wall(6, yy, xx)) tile += 8;
            put_tile(0, tile, (s16)(r + 2), (s16)(c + 2));
        }
    put_tile(0, (u16)((dir >> 1) + 0x100), (s16)((s8)(y - y0) + 2), (s16)((s8)(x - x0) + 2));
    gfx_flush_back_buffer();                                    // (through the one-line 491C:05F3 in the original)
}

// 491C:05FD  Draw the view from square (x, y) facing `dir`: the Area map if it is switched on
// (DS:6AAC), otherwise the 3D view. Three rows of squares are drawn, farthest first. In each row
// the walls facing the party go down first, then the side walls, so anything nearer overdraws
// what is behind it. Positions come from two small tables: a column (DS:0CAA) and a row (DS:0CBE)
// for each of the ten pieces.
void view_render(u8 dir, s8 y, s8 x) {
    if (G8(0x6aac)) { view_draw_area_map(dir, y, x); return; }
    view_draw_background();
    u8 left = (u8)((dir + 6) % 8), back = (u8)((dir + 4) % 8), right = (u8)((dir + 2) % 8);
    s16 bx = (s16)(x + DX(dir) * 2), by = (s16)(y + DY(dir) * 2);        // the square two steps ahead
#define COL(p) ((s16)G16(0x0caa + (p) * 2))
#define ROW(p) ((s8)G8(0x0cbe + (p) * 2))
#define WALL(d, yy, xx) map_wall((d), (s8)(yy), (s8)(xx))
    for (s8 depth = 2; depth >= 0; depth--) {
        s16 fx, fy, col; u8 w, prev;
        if (depth == 2) {
            // facing walls three squares away, from straight ahead outwards to the left, then to the right
            for (int side = 0; side < 2; side++) {
                u8 out = side ? right : left, in = side ? left : right; s16 step = side ? 2 : -2, edge = side ? -1 : 1;
                fx = bx; fy = by; col = 0; prev = 0;
                for (int n = 0; n < 4; n++) {
                    w = WALL(dir, fy, fx);
                    if (!map_in_bounds((s8)fy, (s8)fx) && WALL(in, fy, fx) == 0) prev = 0;
                    if (w == 0) {
                        if (prev && WALL(out, fy - DY(out), fx - DX(out)) != 0) view_draw_piece(9, prev, ROW(9), (s8)(COL(9) + col + edge));
                        prev = 0;
                    } else {
                        if (prev) view_draw_piece(9, prev, ROW(9), (s8)(COL(9) + col + edge));
                        view_draw_piece(0, w, ROW(0), (s8)(COL(0) + col));
                        prev = w;
                    }
                    col = (s16)(col + step); fx = (s16)(fx + DX(out)); fy = (s16)(fy + DY(out));
                }
            }
            // far side walls, left then right
            fx = bx; fy = by; col = 0;
            for (int n = 0; n < 3; n++) {
                if ((w = WALL(left, fy, fx)) != 0) view_draw_piece(1, w, ROW(1), (s8)(COL(1) + col - (n ? 1 : 0)));
                col = (s16)(col - 2); fx = (s16)(fx + DX(left)); fy = (s16)(fy + DY(left));
            }
            fx = bx; fy = by; col = 0;
            for (int n = 0; n < 3; n++) {
                if ((w = WALL(right, fy, fx)) != 0) view_draw_piece(2, w, ROW(2), (s8)(COL(2) + col + (n ? 1 : 0)));
                col = (s16)(col + 2); fx = (s16)(fx + DX(right)); fy = (s16)(fy + DY(right));
            }
        } else if (depth == 1) {
            // two squares away: from the far left inwards, then from the far right inwards
            fx = (s16)(bx + DX(left) * 2); fy = (s16)(by + DY(left) * 2); col = -6;
            for (int n = 0; n < 3; n++) {
                if ((w = WALL(dir, fy, fx)) != 0) view_draw_piece(3, w, ROW(3), (s8)(COL(3) + col));
                if ((w = WALL(left, fy, fx)) != 0) view_draw_piece(4, w, ROW(4), (s8)(COL(4) + col));
                col = (s16)(col + 3); fx = (s16)(fx + DX(right)); fy = (s16)(fy + DY(right));
            }
            fx = (s16)(bx + DX(right) * 2); fy = (s16)(by + DY(right) * 2); col = 6;
            for (int n = 0; n < 3; n++) {
                if ((w = WALL(dir, fy, fx)) != 0) view_draw_piece(3, w, ROW(3), (s8)(COL(3) + col));
                if ((w = WALL(right, fy, fx)) != 0) view_draw_piece(5, w, ROW(5), (s8)(COL(5) + col));
                col = (s16)(col - 3); fx = (s16)(fx + DX(left)); fy = (s16)(fy + DY(left));
            }
        } else {
            // the party's own row
            fx = (s16)(bx + DX(left)); fy = (s16)(by + DY(left)); col = -7;
            for (int n = 0; n < 2; n++) {
                if ((w = WALL(dir, fy, fx)) != 0) view_draw_piece(6, w, ROW(6), (s8)(COL(6) + col));
                if ((w = WALL(left, fy, fx)) != 0) view_draw_piece(7, w, ROW(7), (s8)(COL(7) + col));
                col = (s16)(col + 7); fx = (s16)(fx + DX(right)); fy = (s16)(fy + DY(right));
            }
            fx = (s16)(bx + DX(right)); fy = (s16)(by + DY(right)); col = 7;
            for (int n = 0; n < 2; n++) {
                if ((w = WALL(dir, fy, fx)) != 0) view_draw_piece(6, w, ROW(6), (s8)(COL(6) + col));
                if ((w = WALL(right, fy, fx)) != 0) view_draw_piece(8, w, ROW(8), (s8)(COL(8) + col));
                col = (s16)(col - 7); fx = (s16)(fx + DX(left)); fy = (s16)(fy + DY(left));
            }
        }
        bx = (s16)(bx + DX(back)); by = (s16)(by + DY(back));               // one row nearer
    }
#undef COL
#undef ROW
#undef WALL
    gfx_flush_back_buffer();
}

// 4CA0:003B  Load symbol set `set` (0-4) from block `id` of the area's tile archive 8X8D<n>.dax.
// Returns 0 if it could not be loaded here (the caller then lets the original deal with it, which
// reports the error and stops).
int sym_load_set(u8 set, u8 id) {
    if (set > 4) return 1;
    char name[16]; snprintf(name, sizeof name, "8X8D%u", G8(0x5376)); name[5] = 0;
    farptr var = FARPTR(DATA_SEG, 0x5e3a + set * 4);
    if (!gfx_load_pic(var, 13, 1, id, name)) return 0;
    return P32(var, 0) != 0;
}

// 4C2B:0022  Load picture 1 of <name>.dax and copy its frames into the shared icon sheet
// (DS:5E52) starting at frame `slot`. Returns 0 if it could not be done here.
int icon_load(u8 slot, const char *name) {
    if (slot > 0x7f) return 0;
    char path[32]; snprintf(path, sizeof path, "%s.dax", name);
    FILE *f = plat_fopen(path, "rb"); if (!f) return 0;
    fclose(f);
    farptr pic; u16 size;
    if (!gfx_load_pic_native(&pic, 0, 0, 1, name)) return 0;
    (void)size;
    farptr sheet = G32(0x5e52);
    if (sheet) memmove(&mem[LIN(sheet) + 0x17 + (u16)(slot * P16(pic, 0x11))], &mem[LIN(pic) + 0x17], (u16)(P8(pic, 8) * P16(pic, 0x11)));
    gfx_dispose_pic(pic);
    return 1;
}

// 4C2B:00F1  Merge icon `icon` into the back buffer at tile (row, col): from the icon sheet at
// DS:5E52, or for numbers of 128 and up from the second sheet at DS:5E56.
void icon_draw(u8 icon, s8 row, s8 col) {
    if (icon < 0x80) gfx_merge_pic(G32(0x4a20), G32(0x5e52), 0, icon, row, col);
    else gfx_merge_pic(G32(0x4a20), G32(0x5e56), 0, icon & 0x7f, row, col);
}

// 4C2B:014E  Free the pair of pictures in combat-figure slot n (DS:5E5A + n*8).
void icon_free_slot(u8 n) {
    farptr a = FARPTR(DATA_SEG, 0x5e5a + n * 8), b = FARPTR(DATA_SEG, 0x5e5e + n * 8);
    if (P32(a, 0)) gfx_free_pic(a);
    if (P32(b, 0)) gfx_free_pic(b);
}

// 491C:0350  Can the party pass through side `dir` of square (x, y)? 1 yes (no wall, or a door),
// 0 no (a plain wall, or off the map where the map does not wrap), 2 a locked door.
u8 map_passage(u8 dir, s8 y, s8 x) {
    if (!map_in_bounds(y, x) && !map_wraps()) return 0;
    if (x > 15) x = 0; if (x < 0) x = 15;
    if (y > 15) y = 0; if (y < 0) y = 15;
    if (map_wall(dir, y, x) == 0) return 1;
    u8 doors = P8(G32(0x6a5c), 0x300 + y * 16 + x);
    switch (dir) {
    case 0: return doors & 3;
    case 2: return (doors >> 2) & 3;
    case 4: return (doors >> 4) & 3;
    case 6: return doors >> 6;
    }
    return 1;
}

// 491C:10C7  Load level map `id` from GEO<n>.dax: a 1,026-byte block, two header bytes and the
// four 256-byte tables. Returns 0 if it could not be done here.
int map_load(u8 id) {
    char name[16]; snprintf(name, sizeof name, "GEO%u", G8(0x5376)); name[4] = 0;
    farptr buf; u16 size;
    if (!dax_load_native(&buf, &size, id, ".dax", name)) return 0;
    if (size != 0x402) { if (size) rtl_freemem(buf, size); return 0; }
    memmove(&mem[LIN(G32(0x6a5c))], &mem[LIN(buf) + 2], 0x400);
    rtl_freemem(buf, size);
    SETP16(G32(0x49d2), 0x18a, id);
    return 1;
}

// 491C:0E08  Load wall recipes into wall-set slot `slot` (1-3) from block `id` of WALLDEF<n>.dax.
// A block holds one or more groups of five wall types (5 x 156 = 780 bytes each); group k goes to
// slot + k - 1, so slot 1 holds wall types 1-5, slot 2 types 6-10, slot 3 types 11-15. The tile
// numbers in the file assume the first loadable tile set, so they are shifted to the numbers of
// this slot's own set, and that tile set is then loaded from the 8X8D archive.
// Returns 0 if it could not be done here.
int map_load_walls(u8 slot, u8 id) {
    if (slot == 0 || slot >= 4) return 1;
    char name[16]; snprintf(name, sizeof name, "WALLDEF%u", G8(0x5376)); name[8] = 0;
    farptr buf; u16 size;
    if (!dax_load_native(&buf, &size, id, ".dax", name)) return 0;
    if (size == 0 || size / 0x30c + slot > 4) { if (size) rtl_freemem(buf, size); return 0; }
    u8 shift = (u8)(G16(0x2722 + slot * 2) - G16(0x2724));
    farptr walls = G32(0x6a58);
#define WALLS(off) mem[((u32)FP_SEG(walls) << 4) + (u16)(FP_OFF(walls) + (off))]
    u16 pos = 0, groups = 0;
    do {
        memmove(&WALLS((slot + groups) * 0x30c - 0x30c), &mem[LIN(buf) + pos], 0x30c);
        pos = (u16)(pos + 0x30c); groups++;
    } while (pos < size);
    rtl_freemem(buf, size);
    u8 n = (u8)groups;
    for (u16 k = 1; k <= n; k++) {
        u16 s = (u16)(slot + k - 1);
        if (s < 1 || s > 3) continue;
        SETG16(0x6aae + s * 4, 0xffff); SETG16(0x6ab0 + s * 4, 0xffff);
        for (u8 type = 1; type <= 5; type++)
            for (u16 i = 0; i <= 0x9b; i++) {
                u8 *t = &WALLS(s * 0x30c + type * 0x9c + i - 0x3a8);
                if (*t >= G16(0x272c)) *t = (u8)(*t + shift);
            }
        u8 tiles;
        if (n < 2) tiles = id;
        else { if (id == 0) id = 10; tiles = (u8)(id * 10 + k); }
        if (!sym_load_set((u8)s, tiles)) call_original(0x4ca0, 0x003b, 2, s, tiles);
    }
#undef WALLS
    if (id == 10) id = 0;
    SETG16(0x6aae + slot * 4, id);
    SETG16(0x6ab0 + slot * 4, slot);
    return 1;
}
