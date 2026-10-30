// Menus (original overlay unit at 4726).
#include "poolrad.h"
#include "text.h"
#include "input.h"
#include "rtl.h"
#include "menu.h"
#include "scene.h"

static int is_hotkey(u8 c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'); }   // the set constant at 4726:00AB

// 4726:03BD  The bottom-line menu. `items` is a string such as "Area Cast View Encamp Search Look":
// every capital letter or digit starts a new word and is that word's hot key. `prompt`, if any, is
// drawn first in `prompt_color`. The current word is shown in `hi_color`, other words in `color`
// with their hot keys in `hi_color`.
//
// Keys: a hot key picks its word; , and . move the highlight; Return accepts the highlighted word
// (only if the menu has colours, i.e. a visible highlight); Space returns ' '; Esc returns 0.
// With `allow_moves` set, an extended key (arrows etc.) or one of the digits 1-9 and '\' ends the
// menu as a movement key: *special is set to 1 and the scan code is returned (digits are mapped
// to the number-pad directions through the table at DS:288C).
//
// While waiting, the routine keeps the scene picture's animation running (frame table at DS:6AB8,
// current frame DS:6ABF), and if a time limit is set (DS:6B2E/6B30, in 1/100 s) it gives up and
// returns the default key DS:6B32. The highlighted word is remembered in DS:6B2B between calls.
u8 menu_horizontal(u8 *special, u8 animate, u8 allow_moves, u8 hi_color, u8 color, u8 prompt_color, const u8 *items, const u8 *prompt) {
    u8 menu[41], start[42], end[42], pbuf[41];
    u8 plen = prompt[0] > 40 ? 40 : prompt[0];
    u8 len = items[0] > 40 ? 40 : items[0];
    memcpy(menu + 1, items + 1, len); menu[0] = len;
    memcpy(pbuf + 1, prompt + 1, plen); pbuf[0] = plen;
    G8(0x6b2c) = 0;
    u8 has_default = (color != 0 || hi_color != 0);

    // split the item string into words (nested procedure 4726:00CB)
    memset(start, 0, sizeof start); memset(end, 0, sizeof end);
    u8 count = 1;
    for (u8 i = 1; i <= len; i++)
        if (is_hotkey(menu[i])) {
            if (start[count] == 0) start[count] = i;
            else { end[count] = (u8)(i - 2); count++; start[count] = i; }
        }
    end[count] = len;

    if (G8(0x6b2b) > count) G8(0x6b2b) = 1;
    u8 key = 0, done = 0;
    *special = 0;
    u32 t0 = game_clock();
    game_clock();                                   // the original reads the clock twice here
    if (plen) text_put_pstr(pbuf, prompt_color, 24, 0);
    u8 xoff = plen;

#define DRAW(sel) do { /* nested procedure 4726:01DA */ \
        if (len) { \
            for (u8 i_ = 1; i_ <= len; i_++) { \
                int in_sel = !(i_ < start[sel] || end[sel] < i_ || hi_color == 0); \
                u8 c_ = (in_sel || is_hotkey(menu[i_])) ? hi_color : color; \
                text_put_char((u8)(xoff + i_ - 1), 24, c_, 1, menu[i_], 1); \
            } \
            if (xoff + len < 0x27) text_put_char((u8)(xoff + len), 24, 0, (u8)(0x28 - xoff - len), ' ', 1); \
        } } while (0)
    DRAW(G8(0x6b2b));

    farptr game = G32(0x49d2);
    for (;;) {
        if ((P16(game, 0x3fe) != 0 || animate) && G8(0x6abf) > 0) {
            u8 f = G8(0x6abf);
            scene_draw(G32(0x6abc + f * 8), animate, 3, 3);
            u32 delay = G32(0x6ab8 + f * 8) * 10;
            s32 elapsed = (s32)(game_clock() - t0);
            if (elapsed >= (s32)delay || P16(game, 0x3fe) != 0) {
                G8(0x6abf)++;
                if (G8(0x6abf) > G8(0x6abe)) G8(0x6abf) = 1;
                t0 = game_clock();
            }
        }
        s32 limit = (s32)G32(0x6b2e);
        if (limit > 0 && (s32)(game_clock() - t0) >= limit) { key = G8(0x6b32); done = 1; }
        else if (crt_key_pressed()) {
            key = input_get_key();
            if (key == 0) {
                key = input_get_key();
                if (allow_moves) { *special = 1; done = 1; }
            } else if (key == 0x1b) { done = 1; key = 0; }
            else if (key == 0x0d) { if (has_default) { key = 0x0d; done = 1; } }
            else if (key == ',') { if (G8(0x6b2b) == 1) G8(0x6b2b) = count; else G8(0x6b2b)--; DRAW(G8(0x6b2b)); }
            else if (key == '.') { if (G8(0x6b2b) == count) G8(0x6b2b) = 1; else G8(0x6b2b)++; DRAW(G8(0x6b2b)); }
            else {
                if (key >= 'a' && key <= 'z') key = (u8)(key - 32);
                if (key == ' ' || is_hotkey(key)) {
                    if (key == ' ') done = 1;
                    else for (u8 i = 1; i <= len; i++)
                        if (menu[i] == key) {
                            done = 1;
                            u8 w = 1; while (w < 41 && start[w] != i) w++;
                            G8(0x6b2b) = w;
                            DRAW(G8(0x6b2b));
                        }
                }
                if (allow_moves && ((key >= '1' && key <= '9') || key == '\\')) {
                    key = (key == '\\') ? 0x37 : G8(0x288c + key - 0x31);
                    *special = 1; done = 1;
                }
            }
        }
        if (done) break;
    }
#undef DRAW
    SETP16(game, 0x3fe, 0);
    G8(0x6b2c) = *special;
    return key;
}

// 4726:06FB  Blank the menu line.
void menu_clear_line(void) { text_put_blanks(40, 0, 24, 0); }

// 4726:10D1  Ask a yes/no question on the menu line. The highlight starts on No. Returns 'Y' or 'N'.
u8 menu_yes_no(u8 hi_color, u8 color, u8 prompt_color, farptr prompt) {
    u8 special, k;
    G8(0x6b2b) = 2;
    do k = menu_horizontal(&special, 0, 0, hi_color, color, prompt_color, &mem[LIN(FARPTR(RSEG(0x4726), 0x10aa))], &mem[LIN(prompt)]);   // "Yes No"
    while (k != 'Y' && k != 'N');
    return k;
}

// List menus work on a singly linked list of 46-byte heap nodes:
//   +00 Pascal string (the line of text)   +29 a flag byte   +2A far pointer to the next node
// 4726:1140  Allocate a list of `count` empty nodes (at least one) and store its head in *var.
void menu_list_alloc(farptr var, s16 count) { SETP32(var, 0, menu_list_new(count)); }

farptr menu_list_new(s16 count) {
    farptr head = rtl_getmem(0x2e), node = head;
    SETP32(node, 0x2a, 0); P8(node, 0x29) = 0; P8(node, 0) = 0;
    for (s16 i = 2; i <= count; i++) {
        farptr next = rtl_getmem(0x2e);
        SETP32(node, 0x2a, next);
        node = next;
        SETP32(node, 0x2a, 0); P8(node, 0x29) = 0; P8(node, 0) = 0;
    }
    return head;
}

// 4726:11F3  Free every node of the list *var. (The pointer itself is left as it was.)
void menu_list_free(farptr var) { menu_list_dispose(P32(var, 0)); }

void menu_list_dispose(farptr node) {
    while (node) { farptr next = P32(node, 0x2a); rtl_freemem(node, 0x2e); node = next; }
}

// ---- 4726:0D9A  The scrolling list menu ----
// Shows the linked list `head` in the window (left,top)-(right,bottom), one node per row, with the
// bottom-line menu `items` underneath (plus " Next" / " Prev" when there is more to see, and
// " Exit" if `with_exit`). Nodes whose flag byte is set are headings: drawn in `head_color` and
// skipped by the highlight. *index is the highlighted node (0-based) and is kept by the caller
// between calls; DS:6B29 is the index of the first row shown; *redraw forces the window to be redrawn.
//
// Keys, besides the bottom-line menu's own: Home / End (7 / 1) move the highlight up / down,
// PgUp / PgDn (9 / 3) or P / N change page. Returns the menu key pressed, with *result pointing
// at the chosen node, or 0 with *result nil for Esc, Exit or an empty list.
int menu_arrows_move = 0;      // port addition, on in the Mac app: Up/Down arrows also move the highlight

typedef struct { farptr head; s16 *index; s16 count, rows; u8 width; s8 left, top, right, bottom; u8 hi_color, color, head_color; } listmenu;

farptr list_nth(farptr node, s16 n) {                    // 4726:0000
    s16 c = 0;
    while (node && c != n) { node = P32(node, 0x2a); c++; }
    return c == n ? node : 0;
}
static s16 list_count(farptr node) { s16 n = 0; while (node) { n++; node = P32(node, 0x2a); } return n; }   // 4726:0067
#define SCROLL ((s16)G16(0x6b29))

// 4726:07A4. The original also calls 4726:0711 (the last heading before the first row) and ignores its result.
static void list_draw_page(listmenu *m, s16 first) {
    text_clear_box(m->left, m->top, m->right, m->bottom);
    farptr node = list_nth(m->head, first);
    for (s8 row = m->top; node && row <= m->bottom; row++) {
        text_put_string(node, P8(node, 0x29) ? m->head_color : m->color, (u8)row, (u8)m->left);
        u8 len = P8(node, 0);
        if (len < m->width) text_put_char((u8)(m->left + len), (u8)row, 0, (u8)(m->width - len), ' ', 1);
        node = P32(node, 0x2a);
    }
}
// Draw node `idx` without its leading and trailing blanks (4726:0969 highlighted, 4726:0A08 normal; the first and
// last non-blank are found by 4726:08D7 and 4726:0923 in the original).
static void list_draw_item(listmenu *m, s16 idx, int highlighted) {
    farptr node = list_nth(m->head, idx);
    u8 s[41]; u8 len = P8(node, 0) > 40 ? 40 : P8(node, 0);
    memcpy(s + 1, &mem[LIN(node) + 1], len);
    u8 a = 1; while (a < len && s[a] == ' ') a++;
    u8 b = len; while (b > 1 && s[b] == ' ') b--;
    u8 n = (u8)(b - a + 1), out[42];
    if (a > len) n = 0; else if (n > len - a + 1) n = (u8)(len - a + 1);      // Pascal Copy()
    out[0] = n; memcpy(out + 1, s + a, n);
    u8 color = highlighted ? m->hi_color : P8(node, 0x29) ? m->head_color : m->color;
    text_put_pstr(out, color, (u8)(m->top + (idx - SCROLL)), (u8)(m->left + a - 1));
}
static void list_skip_headings(listmenu *m, int down) {  // 4726:0B03
    for (s16 n = 0; n < m->rows && P8(list_nth(m->head, *m->index), 0x29) != 0; n++) {
        if (!down) {
            (*m->index)--;
            if (*m->index < SCROLL) *m->index = (s16)(SCROLL + m->rows - 1);
            if (m->count - 1 < *m->index) *m->index = (s16)(m->count - 1);
        } else {
            (*m->index)++;
            if (SCROLL + m->rows - 1 < *m->index) *m->index = SCROLL;
            if (m->count - 1 < *m->index) *m->index = SCROLL;
        }
    }
}
static void list_move(listmenu *m, int down) {           // 4726:0CC5
    if (!down) {
        (*m->index)--;
        if (*m->index < SCROLL) *m->index = (s16)(SCROLL + m->rows - 1);
        if (m->count - 1 < *m->index) *m->index = (s16)(m->count - 1);
    } else {
        (*m->index)++;
        if (SCROLL + m->rows - 1 < *m->index) *m->index = SCROLL;
        if (m->count - 1 < *m->index) *m->index = SCROLL;
    }
    list_skip_headings(m, down);
}
static void list_page(listmenu *m, int down) {           // 4726:0C33
    s16 old_index = *m->index, old = SCROLL, sc = old;
    if (!down) { sc = (s16)(sc - m->rows); if (sc < 0) sc = 0; }
    else { sc = (s16)(sc + m->rows); if (m->count - m->rows < sc) sc = (s16)(m->count - m->rows); }
    SETG16(0x6b29, (u16)sc);
    *m->index = (s16)(sc + (old_index - old));
    list_skip_headings(m, down);
    list_draw_page(m, SCROLL);
}
static void pcat(u8 *dst, farptr src) {                  // Pascal string append, result at most 40 characters
    u8 n = P8(src, 0); if (dst[0] + n > 40) n = (u8)(40 - dst[0]);
    memcpy(dst + 1 + dst[0], &mem[LIN(src) + 1], n); dst[0] = (u8)(dst[0] + n);
}

u8 menu_list(farptr *result, s16 *index, u8 *redraw, u8 with_exit, farptr head, s8 bottom, s8 right, s8 top, s8 left,
             u8 hi_color, u8 color, u8 head_color, const u8 *items, const u8 *prompt) {
    u8 pr[41], it[41], menu[41], special, key;
    pr[0] = prompt[0] > 40 ? 40 : prompt[0]; memcpy(pr + 1, prompt + 1, pr[0]);
    it[0] = items[0] > 40 ? 40 : items[0]; memcpy(it + 1, items + 1, it[0]);
    if (!head) { *index = 0; *result = 0; return 0; }
    listmenu m = { head, index, list_count(head), (s16)(bottom - top + 1), (u8)(right - left + 1), left, top, right, bottom, hi_color, color, head_color };
    G8(0x6b2b) = 1;
    s16 leading = 0;                                     // headings at the very top of the list
    for (farptr n = head; n && P8(n, 0x29) != 0; n = P32(n, 0x2a)) leading++;
    if (*index < SCROLL) { SETG16(0x6b29, (u16)*index); *redraw = 1; }
    if (m.count < SCROLL) { SETG16(0x6b29, 0); *redraw = 1; }
    (*index)++;
    list_move(&m, 0);
    if (*redraw) list_draw_page(&m, SCROLL);
    *redraw = 0;
    for (;;) {
        list_draw_item(&m, *index, 1);
        memcpy(menu, it, 41);
        int more_below = SCROLL < m.count - m.rows, more_above = 0;
        if (more_below) pcat(menu, FARPTR(RSEG(0x4726), 0x0d88));           // " Next"
        if (leading < SCROLL) { pcat(menu, FARPTR(RSEG(0x4726), 0x0d8e)); more_above = 1; }   // " Prev"
        if (with_exit) pcat(menu, FARPTR(RSEG(0x4726), 0x0d94));            // " Exit"
        key = menu_horizontal(&special, 0, 1, hi_color, color, head_color, menu, pr);
        list_draw_item(&m, *index, 0);
        if (special) {
            if (key == 0x47 || (menu_arrows_move && key == 0x48)) list_move(&m, 0);        // Home (or Up)
            else if (key == 0x4f || (menu_arrows_move && key == 0x50)) list_move(&m, 1);   // End (or Down)
            else if (key == 0x49) { if (more_above) list_page(&m, 0); }     // PgUp
            else if (key == 0x51 && more_below) list_page(&m, 1);           // PgDn
        }
        else if (key == 'P') list_page(&m, 0);
        else if (key == 'N') list_page(&m, 1);
        else break;
    }
    if (key == 0x1b || key == 'E' || key == 0) { *result = 0; return 0; }
    *result = list_nth(head, *index);
    return key;
}
