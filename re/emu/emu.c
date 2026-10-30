// Minimal 8086 + DOS + EGA emulator used as a reference oracle for the Pool of Radiance port.
// Headless: driven by a key script, dumps EGA frames as PPM and memory snapshots.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int8_t s8; typedef int16_t s16; typedef int32_t s32;

u8 mem[0x110000];
u16 R[8];            // AX CX DX BX SP BP SI DI
u16 SG[4];            // ES CS SS DS
static u16 ip; static u16 flags = 0x0202;   // DOS starts programs with interrupts enabled
enum {AX,CX,DX,BX,SP,BP,SI,DI}; enum {ES,CS,SS,DS};
#define CF 0x001
#define PF 0x004
#define AF 0x010
#define ZF 0x040
#define SF 0x080
#define TF 0x100
#define IF 0x200
#define DF 0x400
#define OF 0x800

void emu_hook_stats(void);
u32 emu_pcall(u16 gseg, u16 off, int nargs, ...);
u32 emu_pcall_strfn(u16 gseg, u16 off, int nargs, ...);
int trace = 0;
static unsigned *prof;   // call profile (-P file): how often each routine is entered
static const char *gamedir = ".";
static const char *savedir = NULL;
static const char *outdir = ".";
#ifdef EMU_LIB
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
static pthread_mutex_t kmx = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t kcv = PTHREAD_COND_INITIALIZER;
static volatile int engine_exited = 0; static volatile int engine_quit = 0;
static void rec_flush(void);
#endif

// ---------------- EGA (shared with the port) ----------------
#include "../../port/ega.h"
#define plane ega_plane
#define latch ega_latch
#define gc ega_gc
#define seq ega_seq
#define video_mode ega_mode
#define ega_pal ega_palette
#define set_mode ega_set_mode
uint8_t ega_in(uint16_t port);
// PIT channels 0 (timer IRQ rate) and 2 (speaker tone), speaker gate on port 61h
unsigned pit_div[3] = {65536, 65536, 65536}; static u8 pit_flip[3]; static u16 pit_lo[3];
volatile unsigned spk_gate = 0, spk_div = 0;
static unsigned long long vwrites = 0; unsigned long long game_icount, interp_icount;
#ifdef EMU_LIB
// Speaker tone changes, time-stamped, for the audio thread (single producer, single consumer).
#include <time.h>
#define SPKQ 4096
static struct { double t; unsigned hz; } spkq[SPKQ]; static volatile unsigned spkq_w, spkq_r;
static double spk_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
#endif
// SPKLOG=file records every change of the speaker tone: instruction count, frequency in Hz (0 = silent)
static unsigned long long icount;
// A standing test edit ("fxtimer"): whenever the n-th party member carries a status effect of the given
// kind with more time left than `time`, cut it to `time`. Checked every 256 instructions, so it also
// catches things that happen while the game is not asking for keys; in the port also at every call of a routine in C
// and every BIOS/DOS call, where few instructions run.
static unsigned rule_n, rule_kind, rule_time;
static unsigned keep_n, keep_off, keep_max;   // "keep": the n-th party member's byte at keep_off is held at or below keep_max
static void rule_apply(void);
extern int hooks_enabled;
static char snap_pending[160];   // "snap <name>": write a snapshot at the next instruction boundary
static void snapshot_save(void);
static void spk_log(void) {
    static FILE *f; static int init; static unsigned last = ~0u;
    if (!init) { init = 1; const char *p = getenv("SPKLOG"); if (p) f = fopen(p, "w"); }
    unsigned hz = ((spk_gate & 3) == 3 && spk_div) ? 1193182u / spk_div : 0;
    if (hz == last) return;
    last = hz;
#ifdef EMU_LIB
    unsigned w = spkq_w; spkq[w % SPKQ].t = spk_now(); spkq[w % SPKQ].hz = hz; spkq_w = w + 1;
#endif
    if (f) fprintf(f, "%llu\t%u\n", icount, hz);
}
void (*port_trace)(u16 port, u8 v);     // a self-test watching the ports
void port_out(u16 port, u8 v) {
    if (port_trace) port_trace(port, v);
    switch (port) {
    case 0x3c4: case 0x3c5: case 0x3ce: case 0x3cf: ega_out(port, v); break;
    case 0x43: pit_flip[(v >> 6) % 3] = 0; break;
    case 0x40: case 0x42: { int c = port - 0x40; if (!pit_flip[c]) { pit_lo[c] = v; pit_flip[c] = 1; } else { unsigned d = pit_lo[c] | (v << 8); pit_div[c] = d ? d : 65536; pit_flip[c] = 0; if (c == 2) { spk_div = pit_div[2]; spk_log(); } } } break;
    case 0x61: spk_gate = v & 3; spk_log(); break;
    default: if (trace) fprintf(stderr, "OUT %04x,%02x\n", port, v);
    }
}
static u8 port_in(u16 port) {
    static u8 t; if (port == 0x3da) { t ^= 9; return t; }
    if (port == 0x61) return spk_gate; if (port == 0x40) { static u8 c; c -= 37; return c; }
    if (port == 0x3cf || port == 0x3c5) return ega_in(port);
    return 0xff;
}
u8 emu_port_in(u16 port) { return port_in(port); }      // for native code
static void save_frame(const char *name) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); return; }
    if (video_mode == 0x10 || video_mode == 0x0e || video_mode == 0x0d) {
        int w = video_mode == 0x0d ? 320 : 640, h = video_mode == 0x10 ? 350 : 200;
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            u32 off = y * (w / 8) + (x >> 3); int bit = 7 - (x & 7); int c = 0;
            for (int p = 0; p < 4; p++) c |= ((plane[p][off] >> bit) & 1) << p;
            u8 e = ega_pal[c];
            u8 r = ((e >> 2) & 1) * 0xaa + ((e >> 5) & 1) * 0x55;
            u8 g = ((e >> 1) & 1) * 0xaa + ((e >> 4) & 1) * 0x55;
            u8 b = ((e >> 0) & 1) * 0xaa + ((e >> 3) & 1) * 0x55;
            fputc(r, f); fputc(g, f); fputc(b, f);
        }
    } else { // text mode dump as tiny image placeholder + text file
        fprintf(f, "P6\n1 1\n255\n"); fputc(0, f); fputc(0, f); fputc(0, f);
        char tp[1024]; snprintf(tp, sizeof tp, "%s/%s.txt", outdir, name); FILE *t = fopen(tp, "w");
        if (t) { for (int y = 0; y < 25; y++) { for (int x = 0; x < 80; x++) { u8 c = mem[0xb8000 + (y * 80 + x) * 2]; fputc(c >= 32 && c < 127 ? c : ' ', t); } fputc('\n', t); } fclose(t); }
    }
    fclose(f);
}

// ---------------- memory access ----------------
static inline u8 rb(u32 a) { a &= 0xfffff; if (a >= 0xa0000 && a < 0xb0000 && video_mode >= 0x0d) return ega_read(a - 0xa0000); return mem[a]; }
static u32 watch_addr = 0xffffffff; static u16 watch_cs, watch_ip; static u16 ip_now(void);
static inline void wb(u32 a, u8 v) { a &= 0xfffff; if (a == watch_addr) { watch_cs = SG[1]; watch_ip = ip_now(); } if (a >= 0xa0000 && a < 0xb0000 && video_mode >= 0x0d) { ega_write(a - 0xa0000, v); return; } mem[a] = v; }
static inline u16 rw(u32 a) { return rb(a) | (rb(a + 1) << 8); }
static inline void ww(u32 a, u16 v) { wb(a, v & 0xff); wb(a + 1, v >> 8); }
#define LIN(seg, off) ((((u32)(seg)) << 4) + (u16)(off))
static inline u8 fetch8(void) { u8 v = mem[LIN(SG[CS], ip)]; ip++; return v; }
static inline u16 fetch16(void) { u16 v = fetch8(); v |= fetch8() << 8; return v; }
static u16 ip_now(void) { return ip; }
void push(u16 v) { R[SP] -= 2; ww(LIN(SG[SS], R[SP]), v); }
u16 pop(void) { u16 v = rw(LIN(SG[SS], R[SP])); R[SP] += 2; return v; }

// ---------------- script / keyboard ----------------
static FILE *script;
u16 keyq[64]; int kq_h, kq_t;
static int idle_polls = 0;
static int script_done = 0;
static int rec_replay, staged_wait = -1, hold_wait = -1; static u16 staged_key;   // replaying a recorded session (need_key)
static unsigned long long clk_reads, clock_hold;   // GetTime calls so far; a recorded mark waiting for that many
static unsigned long long delays, delay_hold; static u32 delay_lin; static int delay_native;   // native: Delay counts itself (emu_delay_count)   // the same for calls of Crt's Delay (1A40:029C)
static int shot_n = 0;
static u16 psp_seg;
static void dump_mem(const char *name) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s.mem", outdir, name);
    FILE *f = fopen(path, "wb"); if (f) { fwrite(mem, 1, 0xa0000, f); fclose(f); }
}
static const char *prof_path;
static const char *cover_path; static u8 *cover;     // -V file: every address that ran as original code (for the native build's to-do list)
static void prof_write(void) {
    if (!prof) return; FILE *f = fopen(prof_path, "w"); if (!f) return;
    for (u32 l = 0; l < 0x100000; l++) if (prof[l]) fprintf(f, "%05x\t%u\n", l - 0x1200 + 0x10000, prof[l]);   // flat linear address with Ghidra's 1000h base
    fclose(f);
}
void finish(int code) {
    prof_write();
    if (cover) { FILE *f = fopen(cover_path, "w"); if (f) { for (u32 l = 0; l < 0x100000; l++) if (cover[l]) fprintf(f, "%05x\n", l - 0x1200 + 0x10000); fclose(f); } }
#ifdef EMU_LIB
    rec_flush();
    engine_exited = 1; pthread_exit(NULL);
#endif
    char n[64]; snprintf(n, sizeof n, "final"); save_frame(n); if (getenv("HOOKSTATS")) emu_hook_stats();
    fprintf(stderr, "[emu] exit code %d after %llu instructions (%llu of them interpreted, %llu of them the game's own), %llu video writes\n", code, icount, interp_icount, game_icount, vwrites);
    exit(0);
}
int key_avail(void) { return kq_h != kq_t; }
static void key_push(u16 k) { keyq[kq_t] = k; kq_t = (kq_t + 1) & 63; }
#ifdef EMU_LIB
// Session recording for bug reports (engine_record). keys.txt is a script for the headless emulator: every key
// the game took, with how many keyboard polls had found nothing since the key before ("key 0x1c0d 37"), plus
// timestamped notes and "shot"/"dump" lines at marks; clock.txt is every GetTime answer, run-length coded.
// Replayed with -C clock.txt the original and the port follow the same path the player did (re/emu/replay.sh).
static FILE *rec_keys, *rec_clock; static unsigned long rec_empty, clk_last = ~0ul, clk_run;
static void rec_clock_flush(void) { if (rec_clock && clk_run) fprintf(rec_clock, "%lu %lu\n", clk_last, clk_run); clk_run = 0; }
static void rec_flush(void) { rec_clock_flush(); clk_last = ~0ul; if (rec_keys) fflush(rec_keys); if (rec_clock) fflush(rec_clock); }
// The front end posts keys to an inbox; only the emulator thread moves them into the game's queue, so whether a
// poll found a key is decided in one place.
static u16 inbox[64]; static int ib_h, ib_t;
static void inbox_drain(void) { pthread_mutex_lock(&kmx); while (ib_h != ib_t && ((kq_t + 1) & 63) != kq_h) { keyq[kq_t] = inbox[ib_h]; kq_t = (kq_t + 1) & 63; ib_h = (ib_h + 1) & 63; } pthread_mutex_unlock(&kmx); }
u16 key_pop(void) {
    u16 k = keyq[kq_h]; kq_h = (kq_h + 1) & 63;
    if (rec_keys) { fprintf(rec_keys, "key 0x%04x %lu\n", k, rec_empty); rec_flush(); }
    rec_empty = 0; return k;
}
#else
u16 key_pop(void) { u16 k = keyq[kq_h]; kq_h = (kq_h + 1) & 63; return k; }
#endif
static u16 named_key(const char *s) {
    if (!strcmp(s, "enter")) return 0x1c0d; if (!strcmp(s, "esc")) return 0x011b; if (!strcmp(s, "space")) return 0x3920;
    if (!strcmp(s, "up")) return 0x4800; if (!strcmp(s, "down")) return 0x5000; if (!strcmp(s, "left")) return 0x4b00; if (!strcmp(s, "right")) return 0x4d00;
    if (!strcmp(s, "tab")) return 0x0f09; if (!strcmp(s, "bksp")) return 0x0e08;
    if (!strcmp(s, "home")) return 0x4700; if (!strcmp(s, "end")) return 0x4f00; if (!strcmp(s, "pgup")) return 0x4900; if (!strcmp(s, "pgdn")) return 0x5100;
    if (s[0] == 'f' && isdigit((unsigned char)s[1])) return (u16)((0x3a + atoi(s + 1)) << 8);
    if (s[0] == '0' && s[1] == 'x') return (u16)strtol(s, 0, 16);
    if (strlen(s) == 1) return (u16)(u8)s[0];
    return 0;
}
u32 emu_pcall(u16 gseg, u16 off, int nargs, ...);
// Run script commands until a key is queued. Returns 0 if script ended.
static int script_advance(void) {
    char line[256]; static char pending[128]; static int pend_i = 0;
    if (pending[pend_i]) { key_push((u8)pending[pend_i++]); return 1; }      // "text" delivers one key per request
    while (script && fgets(line, sizeof line, script)) {
        char cmd[64] = "", arg[128] = "";
        if (sscanf(line, "%63s %127s", cmd, arg) < 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "shot")) { char n[160]; if (arg[0]) snprintf(n, sizeof n, "%s", arg); else snprintf(n, sizeof n, "shot%03d", shot_n); shot_n++; save_frame(n);
            if (rec_replay && getenv("REPLAYDBG")) fprintf(stderr, "[replay] shot %s at %llu clock reads, %llu delays, %d polls\n", n, clk_reads, delays, idle_polls); }
        else if (!strcmp(cmd, "dump")) dump_mem(arg[0] ? arg : "dump");
        else if (!strcmp(cmd, "delaypos")) { unsigned long long k = strtoull(arg, 0, 10); if (rec_replay && delays < k) { delay_hold = k; return 1; } }
        else if (!strcmp(cmd, "clockpos")) { unsigned long long k = strtoull(arg, 0, 10); if (rec_replay && clk_reads < k) { clock_hold = k; return 1; } }   // recorded: ... and this many clock readings
        else if (!strcmp(cmd, "polls")) { if (rec_replay) { hold_wait = atoi(arg); return 1; } }   // recorded: what follows happened after this many empty polls
        else if (!strcmp(cmd, "key")) {
            u16 k = named_key(arg); int w;
            if (rec_replay && sscanf(line, "%*s %*s %d", &w) == 1) { staged_key = k; staged_wait = w; return 1; }   // recorded: key <code> <empty polls first>
            key_push(k); return 1;
        }
        else if (!strcmp(cmd, "text")) { snprintf(pending, sizeof pending, "%s", arg); pend_i = 0; if (pending[0]) { key_push((u8)pending[pend_i++]); return 1; } }
        else if (!strcmp(cmd, "trace")) trace = atoi(arg);
        else if (!strcmp(cmd, "pos")) { u8 *g = mem + 0x3dec0; fprintf(stderr, "[pos] %s x=%d y=%d facing=%c\n", arg, g[0x6aad], g[0x6aae], "N?E?S?W?"[g[0x6aaf] & 7]); }
        else if (!strcmp(cmd, "peek")) { unsigned a, n = 1; sscanf(line, "%*s %x %u", &a, &n); fprintf(stderr, "[peek] DS:%04x", a); for (unsigned i = 0; i < n; i++) fprintf(stderr, " %02x", mem[0x3dec0 + a + i]); fprintf(stderr, "\n"); }
        else if (!strcmp(cmd, "areaw")) {   // areaw <offset> <value>: set a word of the current area's script data block (far pointer at DS:49D6)
            unsigned a, v; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x %x", &a, &v) == 2) { u32 la = ((u32)(g[0x49d8] | g[0x49d9] << 8) << 4) + (g[0x49d6] | g[0x49d7] << 8) + a; mem[la] = v & 0xff; mem[la + 1] = (u8)(v >> 8); }
        }
        // Memory edits for tests. Both builds run in this emulator, so an edit lands identically in each.
        // All numbers are hex. DS is the game's data segment (linear 3DEC0h in the flat program).
        else if (!strcmp(cmd, "tile")) {    // tile <x> <y> <t>: a square of the battle map (far pointer at DS:6716; 50 by 25 squares from byte 7)
            unsigned x, y, t; sscanf(line, "%*s %x %x %x", &x, &y, &t); u8 *g = mem + 0x3dec0;
            u32 m = (u32)(g[0x6718] | g[0x6719] << 8) * 16 + (g[0x6716] | g[0x6717] << 8);
            if (x < 0x32 && y < 0x19) mem[m + 7 + y * 0x32 + x] = (u8)t;
        }
        else if (!strcmp(cmd, "poke")) {    // poke <ds-offset> <byte>
            unsigned a, v; if (sscanf(line, "%*s %x %x", &a, &v) == 2) mem[0x3dec0 + a] = (u8)v;
        }
        else if (!strcmp(cmd, "call") || !strcmp(cmd, "callp") || !strcmp(cmd, "calll") || !strcmp(cmd, "callf") || !strcmp(cmd, "calls")) {   // result logged: call AX, calll DX:AX, callp none
            // call <seg> <off> [word args in push order]: run a routine of the game here (Ghidra
            // segment; in the port build its hook runs). The interrupted code's registers are kept. Keys it asks for
            // come from the following script lines.
            unsigned sg, of, a[8]; int n = 0; char tok[12][32]; int nt = sscanf(line, "%*s %31s %31s %31s %31s %31s %31s %31s %31s %31s %31s", tok[0], tok[1], tok[2], tok[3], tok[4], tok[5], tok[6], tok[7], tok[8], tok[9]);
            if (nt >= 2) { sscanf(tok[0], "%x", &sg); sscanf(tok[1], "%x", &of); n = 2;
                for (int t = 2; t < nt && n < 10; t++) {
                    if (tok[t][0] == 'c' && tok[t][1] == 'h') {          // ch<n>: the n-th party member's far pointer (segment, then offset)
                        unsigned m = (unsigned)atoi(tok[t] + 2); u8 *g = mem + 0x3dec0;
                        u32 off = g[0x5d96] | g[0x5d97] << 8, seg = g[0x5d98] | g[0x5d99] << 8;
                        while (--m && seg) { u8 *c = mem + (seg << 4) + off; off = c[0x104] | c[0x105] << 8; seg = c[0x106] | c[0x107] << 8; }
                        a[n - 2] = seg; n++; if (n < 10) { a[n - 2] = off; n++; }
                    } else if (tok[t][0] == 'c' && tok[t][1] == 'b') {   // cb<n>: combatant slot n's far pointer (hex n, as cmb)
                        unsigned m = 0; sscanf(tok[t] + 2, "%x", &m); u8 *e = mem + 0x3dec0 + 0x65b9 + m * 4;
                        a[n - 2] = e[2] | e[3] << 8; n++; if (n < 10) { a[n - 2] = e[0] | e[1] << 8; n++; }
                    } else { sscanf(tok[t], "%x", &a[n - 2]); n++; }
                }
            }
            if (n >= 2) {
                u16 r[8], g[4], f = flags, i = ip; memcpy(r, R, sizeof r); memcpy(g, SG, sizeof g);
                SG[DS] = (u16)(0x3dec0 >> 4);
                int k = n - 2;
                u32 (*pc)(u16, u16, int, ...) = cmd[4] == 's' ? emu_pcall_strfn : emu_pcall;   // calls: a string function (destination left on the stack)
                u32 res = pc((u16)sg, (u16)of, k, k > 0 ? a[0] : 0, k > 1 ? a[1] : 0, k > 2 ? a[2] : 0, k > 3 ? a[3] : 0, k > 4 ? a[4] : 0, k > 5 ? a[5] : 0, k > 6 ? a[6] : 0, k > 7 ? a[7] : 0);
                if (cmd[4] == 'f') fprintf(stderr, "[call] %04x:%04x -> flags %03x\n", sg, of, flags & 0x8d5);   // callf: CF PF AF ZF SF OF
                else if (cmd[4] != 'p' && cmd[4] != 's') fprintf(stderr, "[call] %04x:%04x -> %08x\n", sg, of, cmd[4] == 'l' ? (unsigned)res : (unsigned)(res & 0xffff));   // compared between the builds by cmp.sh
                memcpy(R, r, sizeof r); memcpy(SG, g, sizeof g); flags = f; ip = i;
            }
        }
        else if (!strcmp(cmd, "shopitems")) {   // shopitems <n>: make the shop/treasure item list (DS:6810) the n-th party member's item list (shared, for tests)
            unsigned n; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x", &n) == 1) {
                u32 off = g[0x5d96] | g[0x5d97] << 8, seg = g[0x5d98] | g[0x5d99] << 8;
                while (--n && seg) { u8 *c = mem + (seg << 4) + off; off = c[0x104] | c[0x105] << 8; seg = c[0x106] | c[0x107] << 8; }
                if (seg) { u8 *c = mem + (seg << 4) + off; memcpy(g + 0x6810, c + 0xc8, 4); }
            }
        }
        else if (!strcmp(cmd, "pokea")) {   // pokea <ptr> <offset> <byte>: a byte in the block the DS far pointer at <ptr> points to (e.g. 49d6 = area state)
            unsigned pa, a, v; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x %x %x", &pa, &a, &v) == 3) { u32 off = g[pa] | g[pa + 1] << 8, seg = g[pa + 2] | g[pa + 3] << 8; mem[(seg << 4) + off + a] = (u8)v; }
        }
        else if (!strcmp(cmd, "item")) {    // item <n> <k> <offset> <byte>: a byte of the k-th item (1 = first) of the n-th party member
            unsigned n, k, a, v; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x %x %x %x", &n, &k, &a, &v) == 4) {
                u32 off = g[0x5d96] | g[0x5d97] << 8, seg = g[0x5d98] | g[0x5d99] << 8;
                while (--n && seg) { u8 *c = mem + (seg << 4) + off; off = c[0x104] | c[0x105] << 8; seg = c[0x106] | c[0x107] << 8; }
                if (seg) { u8 *c = mem + (seg << 4) + off; off = c[0xc8] | c[0xc9] << 8; seg = c[0xca] | c[0xcb] << 8; }
                while (seg && --k) { u8 *t = mem + (seg << 4) + off; off = t[0x2a] | t[0x2b] << 8; seg = t[0x2c] | t[0x2d] << 8; }
                if (seg) mem[(seg << 4) + off + a] = (u8)v; else fprintf(stderr, "[item] no such item\n");
            }
        }
        else if (!strcmp(cmd, "chrptr")) {  // chrptr <ds-offset> <n>: store the n-th party member's far pointer at DS:offset
            unsigned a, n; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x %x", &a, &n) == 2) {
                u32 off = g[0x5d96] | g[0x5d97] << 8, seg = g[0x5d98] | g[0x5d99] << 8;
                while (--n && seg) { u8 *c = mem + (seg << 4) + off; off = c[0x104] | c[0x105] << 8; seg = c[0x106] | c[0x107] << 8; }
                g[a] = (u8)off; g[a + 1] = (u8)(off >> 8); g[a + 2] = (u8)seg; g[a + 3] = (u8)(seg >> 8);
            }
        }
        else if (!strcmp(cmd, "chr")) {     // chr <n> <offset> <byte>: a byte of the n-th party member's record (1 = leader)
            unsigned n, a, v; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x %x %x", &n, &a, &v) == 3) {
                u32 off = g[0x5d96] | g[0x5d97] << 8, seg = g[0x5d98] | g[0x5d99] << 8;
                while (--n && seg) { u8 *c = mem + (seg << 4) + off; off = c[0x104] | c[0x105] << 8; seg = c[0x106] | c[0x107] << 8; }
                if (seg) mem[(seg << 4) + off + a] = (u8)v; else fprintf(stderr, "[chr] no such party member\n");
            }
        }
        else if (!strcmp(cmd, "chrfx")) {   // chrfx <n> <kind> <offset> <byte>: a byte of the n-th party member's status-effect node of that kind (offset 1 = time, low byte); silently nothing if they have none
            unsigned n, k, a, v; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x %x %x %x", &n, &k, &a, &v) == 4) {
                u32 off = g[0x5d96] | g[0x5d97] << 8, seg = g[0x5d98] | g[0x5d99] << 8;
                while (--n && seg) { u8 *c = mem + (seg << 4) + off; off = c[0x104] | c[0x105] << 8; seg = c[0x106] | c[0x107] << 8; }
                if (seg) {
                    u8 *c = mem + (seg << 4) + off; u32 eo = c[0x7f] | c[0x80] << 8, es = c[0x81] | c[0x82] << 8;
                    while (es) { u8 *e = mem + (es << 4) + eo; if (e[0] == k) { e[a] = (u8)v; break; } eo = e[5] | e[6] << 8; es = e[7] | e[8] << 8; }
                }
            }
        }
        else if (!strcmp(cmd, "fxtimer")) { // fxtimer <n> <kind> <time>: standing rule, see rule_apply; n = 0 switches it off
            sscanf(line, "%*s %x %x %x", &rule_n, &rule_kind, &rule_time);
        }
        else if (!strcmp(cmd, "keep")) {    // keep <n> <offset> <max>: standing rule, the n-th party member's byte never exceeds max; n = 0 switches it off
            sscanf(line, "%*s %x %x %x", &keep_n, &keep_off, &keep_max);
        }
        else if (!strcmp(cmd, "movefx")) {  // movefx <n> <kind> <slot>: take the n-th party member's status effect of that kind and give it to combatant <slot>
            // (no memory is allocated: the existing node is unlinked from one list and appended to the other, so monsters can be given effects)
            unsigned n, k, slot; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x %x %x", &n, &k, &slot) == 3) {
                u32 off = g[0x5d96] | g[0x5d97] << 8, seg = g[0x5d98] | g[0x5d99] << 8;
                while (--n && seg) { u8 *c = mem + (seg << 4) + off; off = c[0x104] | c[0x105] << 8; seg = c[0x106] | c[0x107] << 8; }
                u8 *t = g + 0x65b9 + slot * 4; u32 to = t[0] | t[1] << 8, ts = t[2] | t[3] << 8;
                if (!seg || !ts) { fprintf(stderr, "[movefx] no such character or combatant\n"); }
                else {
                    u8 *link = mem + (seg << 4) + off + 0x7f;      // walk the donor's list, keeping the link that points at each node
                    u32 eo = link[0] | link[1] << 8, es = link[2] | link[3] << 8;
                    while (es && mem[(es << 4) + eo] != k) { link = mem + (es << 4) + eo + 5; eo = link[0] | link[1] << 8; es = link[2] | link[3] << 8; }
                    if (!es) fprintf(stderr, "[movefx] donor has no effect %02x\n", k);
                    else {
                        u8 *e = mem + (es << 4) + eo;
                        memcpy(link, e + 5, 4); memset(e + 5, 0, 4);  // unlink
                        u8 *tl = mem + (ts << 4) + to + 0x7f;        // append to the combatant's list
                        while (tl[0] | tl[1] | tl[2] | tl[3]) { u32 o2 = tl[0] | tl[1] << 8, s2 = tl[2] | tl[3] << 8; tl = mem + (s2 << 4) + o2 + 5; }
                        tl[0] = (u8)eo; tl[1] = (u8)(eo >> 8); tl[2] = (u8)es; tl[3] = (u8)(es >> 8);
                    }
                }
            }
        }
        else if (!strcmp(cmd, "cmb")) {     // cmb <slot> <offset> <byte>: a byte of a combatant's record during a fight (slots from 1; monsters too)
            unsigned n, a, v; u8 *g = mem + 0x3dec0;
            if (sscanf(line, "%*s %x %x %x", &n, &a, &v) == 3) {
                u8 *t = g + 0x65b9 + n * 4; u32 off = t[0] | t[1] << 8, seg = t[2] | t[3] << 8;
                if (seg && n <= g[0x5f2a]) mem[(seg << 4) + off + a] = (u8)v; else fprintf(stderr, "[cmb] no such combatant\n");
            }
        }
        else if (!strcmp(cmd, "snap")) {    // snap <name>: freeze the whole machine to <outdir>/<name>.snap, taken just after the NEXT key in the script has been delivered
            if (hooks_enabled) fprintf(stderr, "[snap] ignored: snapshots are only taken from the untouched original (-H)\n");
            else snprintf(snap_pending, sizeof snap_pending, "%s", arg);
        }
        else if (!strcmp(cmd, "selftest")) {   // selftest real <n>: C routines against the originals (run with -H)
            extern int rtl_real_selftest(int) __attribute__((weak)); int n = 1000; sscanf(line, "%*s %*s %d", &n);
            if (!strcmp(arg, "real") && rtl_real_selftest) rtl_real_selftest(n);
            extern int rtl_val_selftest(int) __attribute__((weak)); if (!strcmp(arg, "val") && rtl_val_selftest) rtl_val_selftest(n);
            extern int sound_isr_selftest(int) __attribute__((weak)); if (!strcmp(arg, "sound") && sound_isr_selftest) sound_isr_selftest(n);
        }
        else if (!strcmp(cmd, "pokestk")) {   // pokestk <k> <byte> (hex): the byte k below the stack pointer, where a routine called next will find its locals
            unsigned k, v; if (sscanf(line, "%*s %x %x", &k, &v) == 2) mem[LIN(SG[SS], (u16)(R[SP] - k))] = (u8)v;
        }
        else if (!strcmp(cmd, "cmdline")) {   // cmdline <word>: the DOS command line (PSP tail) from now on, e.g. the developers' STING
            u8 *p = mem + psp_seg * 16; size_t n = strlen(arg); if (n > 120) n = 120;
            p[0x80] = (u8)(n + 1); p[0x81] = ' '; memcpy(p + 0x82, arg, n); p[0x82 + n] = 0x0d;
        }
        else if (!strcmp(cmd, "quit")) finish(0);
    }
    script_done = 1; return 0;
}
static int idle_limit = 3000;
// Replaying: a mark's lines run once its recorded position is reached, then the script stops again at the next key.
static void rec_resume(void) {
    if (getenv("REPLAYDBG")) fprintf(stderr, "[replay] resume at %llu clock reads, %llu delays, %d polls\n", clk_reads, delays, idle_polls);
    while (staged_wait < 0 && hold_wait < 0 && !clock_hold && !delay_hold && script_advance()) ;
}
void emu_delay_count(void);
static void delay_entry(void) {
    delays++;
#ifndef EMU_LIB
    if (delay_hold && delays >= delay_hold) { delay_hold = 0; rec_resume(); }
#endif
}
void emu_delay_count(void) { delay_entry(); }
#ifndef EMU_LIB
void emu_note(const char *what) { (void)what; }     // the app records these (engine_record)
#endif
#ifndef BUILD_ID
#define BUILD_ID "dev"
#endif
#ifdef EMU_LIB
void need_key(int blocking) {
    inbox_drain();
    if (blocking) { pthread_mutex_lock(&kmx); while (ib_h == ib_t && !key_avail() && !engine_quit) pthread_cond_wait(&kcv, &kmx); pthread_mutex_unlock(&kmx); inbox_drain(); }
    if (engine_quit) finish(0);
    if (!key_avail() && !blocking) { rec_empty++; usleep(2000); }
}
#else
// A recorded session (first line "# poolrad recording") gives each key the number of empty polls before it:
// the key is held back ("staged") until that many polls have found nothing, as happened in the app.
void need_key(int blocking) {
    if (key_avail()) { idle_polls = 0; return; }
    if (rec_replay) for (;;) {
        if (key_avail()) { idle_polls = 0; return; }
        if (clock_hold || delay_hold) { if (!blocking) { idle_polls++; return; } clock_hold = delay_hold = 0; continue; }   // waiting for a clock reading or a Delay (rec_resume)
        if (staged_wait < 0 && hold_wait < 0) { if (!script_advance()) { fprintf(stderr, "[emu] script ended\n"); finish(0); } continue; }
        int target = staged_wait >= 0 ? staged_wait : hold_wait;
        if (!blocking && idle_polls < target) { idle_polls++; return; }
        if (staged_wait < 0) { hold_wait = -1; continue; }      // a mark's place reached: on to the lines after it
        if (getenv("REPLAYDBG")) fprintf(stderr, "[replay] key %04x after %d of %d polls%s at %llu\n", staged_key, idle_polls, staged_wait, blocking ? " (blocking read)" : "", icount);
        key_push(staged_key); staged_wait = -1; idle_polls = 0; return;
    }
    if (!blocking && ++idle_polls < idle_limit) return;
    idle_polls = 0;
    if (!script_advance()) { fprintf(stderr, "[emu] script ended\n"); finish(0); }
}
#endif

// ---------------- DOS ----------------
#define MAXFH 20
static FILE *fh[MAXFH];
static char fhname[MAXFH][64]; static char fhmode[MAXFH][4];   // what each handle has open, for snapshots
static u16 heap_top;      // next free segment for 48h
static u16 prog_end;      // end of program block
static struct { u16 seg, size; int used; } blocks[64]; static int nblocks;
unsigned long tick = 0;
#ifndef EMU_LIB
// -C clock.txt: the GetTime answers of a recorded session, "value count" per line; after them the counter takes over.
static FILE *clock_in; static unsigned long clk_val, clk_left;
static int clock_next(unsigned long *t) {
    if (!clk_left && !(clock_in && fscanf(clock_in, "%lu %lu", &clk_val, &clk_left) == 2 && clk_left)) return 0;
    clk_left--; *t = clk_val; return 1;
}
#endif

FILE *open_ci(const char *name, const char *mode) {
    char path[1024]; const char *base = name; const char *p;
    if ((p = strrchr(name, '\\'))) base = p + 1; if ((p = strrchr(base, ':'))) base = p + 1;
    DIR *d; struct dirent *e;
    if (savedir) {
        if (mode[0] == 'w') { snprintf(path, sizeof path, "%s/%s", savedir, base); for (char *q = path + strlen(savedir) + 1; *q; q++) *q = toupper((unsigned char)*q); return fopen(path, mode); }
        d = opendir(savedir);
        if (d) { while ((e = readdir(d))) if (!strcasecmp(e->d_name, base)) { snprintf(path, sizeof path, "%s/%s", savedir, e->d_name); closedir(d); return fopen(path, mode); } closedir(d); }
    }
    d = opendir(gamedir);
    if (d) { while ((e = readdir(d))) if (!strcasecmp(e->d_name, base)) { snprintf(path, sizeof path, "%s/%s", gamedir, e->d_name); closedir(d); return fopen(path, mode); } closedir(d); }
    if (mode[0] == 'w') { snprintf(path, sizeof path, "%s/%s", gamedir, base); return fopen(path, mode); }
    return NULL;
}
static void getstr(u32 a, char *out, int max) { int i = 0; while (i < max - 1) { u8 c = mem[(a + i) & 0xfffff]; if (!c) break; out[i++] = c; } out[i] = 0; }
static void set_cf(int c) { // modify flags in IRET frame on stack
    u32 a = LIN(SG[SS], R[SP] + 4); u16 f = rw(a); f = c ? (f | CF) : (f & ~CF); ww(a, f);
}
static void set_zf_frame(int z) { u32 a = LIN(SG[SS], R[SP] + 4); u16 f = rw(a); f = z ? (f | ZF) : (f & ~ZF); ww(a, f); }

static void dos_int21(void) {
    u8 ah = R[AX] >> 8, al = R[AX] & 0xff; char name[256];
    if (trace) fprintf(stderr, "INT21 AX=%04x BX=%04x CX=%04x DX=%04x DS=%04x ES=%04x\n", R[AX], R[BX], R[CX], R[DX], SG[DS], SG[ES]);
    switch (ah) {
    case 0x02: fputc(R[DX] & 0xff, stderr); break;
    case 0x06: if ((R[DX] & 0xff) == 0xff) { need_key(0); if (key_avail()) { R[AX] = (R[AX] & 0xff00) | (key_pop() & 0xff); set_zf_frame(0); } else { R[AX] &= 0xff00; set_zf_frame(1); } } else fputc(R[DX] & 0xff, stderr); break;
    case 0x07: case 0x08: case 0x01: need_key(1); R[AX] = (R[AX] & 0xff00) | (key_pop() & 0xff); break;
    case 0x09: { u32 a = LIN(SG[DS], R[DX]); while (mem[a] != '$') fputc(mem[a++], stderr); } break;
    case 0x0b: need_key(0); R[AX] = (R[AX] & 0xff00) | (key_avail() ? 0xff : 0); break;
    case 0x0e: R[AX] = (R[AX] & 0xff00) | 3; break;
    case 0x19: R[AX] = (R[AX] & 0xff00) | 2; break;
    case 0x1a: break;
    case 0x25: ww(al * 4, R[DX]); ww(al * 4 + 2, SG[DS]); break;
    case 0x2a: R[CX] = 1990; R[DX] = 0x0101; R[AX] = (R[AX] & 0xff00) | 1; break;
    case 0x2c: {
#ifdef EMU_LIB
        struct timeval tv; gettimeofday(&tv, NULL); unsigned long t = (unsigned long)((tv.tv_sec % 86400) * 100 + tv.tv_usec / 10000);
        static int test_clock = -1; if (test_clock < 0) test_clock = getenv("POOLRAD_TEST_CLOCK") != NULL;
        if (test_clock) { tick += 7; t = tick; }      // test aid: the headless emulator's clock, so the code word asked is the scripted one
        if (rec_clock) { if (t != clk_last) { rec_clock_flush(); clk_last = t; } clk_run++; }
#else
        unsigned long t;
        if (!clock_next(&t)) { tick += 7; t = tick; }
        if (clock_hold && clk_reads >= clock_hold) { clock_hold = 0; rec_resume(); }   // a recorded mark's place: after that many readings
#endif
        clk_reads++;
        R[CX] = (u16)((((t / 360000) % 24) << 8) | ((t / 6000) % 60)); R[DX] = (u16)((((t / 100) % 60) << 8) | (t % 100)); } break;
    case 0x30: R[AX] = 0x0003; R[BX] = 0; R[CX] = 0; break; // DOS 3.0
    case 0x33: R[DX] &= 0xff00; break;
    case 0x35: R[BX] = rw(al * 4); SG[ES] = rw(al * 4 + 2); break;
    case 0x39: R[AX] = 5; set_cf(1); break;      // mkdir: the save directory always exists already
    case 0x3a: R[AX] = 5; set_cf(1); break;
    case 0x3b: set_cf(0); break;
    case 0x3c: case 0x3d: case 0x5b: {
        getstr(LIN(SG[DS], R[DX]), name, sizeof name); int h = -1;
        for (int i = 5; i < MAXFH; i++) if (!fh[i]) { h = i; break; }
        FILE *f = NULL;
        if (ah == 0x3d) { f = open_ci(name, (al & 3) == 0 ? "rb" : "r+b"); if (!f && (al & 3)) f = open_ci(name, "rb"); }
        else f = open_ci(name, "w+b");
        if (trace || getenv("DOSLOG")) fprintf(stderr, "[dos] %s '%s' -> %s\n", ah == 0x3d ? "open" : "create", name, f ? "ok" : "FAIL");
        if (!f || h < 0) { R[AX] = 2; set_cf(1); } else { fh[h] = f; R[AX] = h; set_cf(0); snprintf(fhname[h], sizeof fhname[h], "%s", name); snprintf(fhmode[h], sizeof fhmode[h], "%s", ah == 0x3d ? ((al & 3) == 0 ? "rb" : "r+b") : "r+b"); }
    } break;
    case 0x3e: if (R[BX] < MAXFH && fh[R[BX]]) { fclose(fh[R[BX]]); fh[R[BX]] = NULL; fhname[R[BX]][0] = 0; } set_cf(0); break;
    case 0x3f: {
        u16 h = R[BX], n = R[CX]; u32 a = LIN(SG[DS], R[DX]);
        if (h == 0) { R[AX] = 0; set_cf(0); break; }
        if (h >= MAXFH || !fh[h]) { R[AX] = 6; set_cf(1); break; }
        u16 got = 0; int c; while (got < n && (c = fgetc(fh[h])) != EOF) { wb(a + got, (u8)c); got++; }
        R[AX] = got; set_cf(0);
    } break;
    case 0x40: {
        u16 h = R[BX], n = R[CX]; u32 a = LIN(SG[DS], R[DX]);
        if (h == 1 || h == 2) { for (u16 i = 0; i < n; i++) fputc(mem[(a + i) & 0xfffff], stderr); R[AX] = n; set_cf(0); break; }
        if (h >= MAXFH || !fh[h]) { R[AX] = 6; set_cf(1); break; }
        for (u16 i = 0; i < n; i++) fputc(mem[(a + i) & 0xfffff], fh[h]);
        R[AX] = n; set_cf(0);
    } break;
    case 0x41: R[AX] = 2; set_cf(1); break;
    case 0x42: {
        u16 h = R[BX]; if (h >= MAXFH || !fh[h]) { R[AX] = 6; set_cf(1); break; }
        s32 off = (s32)(((u32)R[CX] << 16) | R[DX]);
        fseek(fh[h], off, al == 0 ? SEEK_SET : al == 1 ? SEEK_CUR : SEEK_END);
        long p = ftell(fh[h]); R[AX] = p & 0xffff; R[DX] = (p >> 16) & 0xffff; set_cf(0);
    } break;
    case 0x43: set_cf(0); R[CX] = 0x20; break;
    case 0x44: if (al == 0) { R[DX] = R[BX] < 3 ? 0x80d3 : 0x0000; set_cf(0); } else { set_cf(0); } break;
    case 0x47: wb(LIN(SG[DS], R[SI]), 0); set_cf(0); break;
    case 0x48: {
        u16 need = R[BX];
        for (int i = 0; i < nblocks; i++) if (!blocks[i].used && blocks[i].size >= need) { blocks[i].used = 1; R[AX] = blocks[i].seg; set_cf(0); if (trace) fprintf(stderr, "[dos] alloc reuse %04x\n", R[AX]); return; }
        if ((u32)heap_top + need + 1 > 0x9fff) { R[AX] = 8; R[BX] = 0x9fff - heap_top - 1; set_cf(1); break; }
        blocks[nblocks].seg = heap_top; blocks[nblocks].size = need; blocks[nblocks].used = 1; nblocks++;
        R[AX] = heap_top; heap_top += need + 1; set_cf(0);
    } break;
    case 0x49: for (int i = 0; i < nblocks; i++) if (blocks[i].seg == SG[ES]) blocks[i].used = 0; set_cf(0); break;
    case 0x4a: {
        if (SG[ES] == psp_seg) {
            u32 want = (u32)psp_seg + R[BX];
            if (want > 0x9fff) { R[AX] = 8; R[BX] = 0x9fff - psp_seg; set_cf(1); break; }
            prog_end = (u16)want; if (heap_top < prog_end + 1 || nblocks == 0) heap_top = prog_end + 1; set_cf(0);
        } else { int ok = 0; for (int i = 0; i < nblocks; i++) if (blocks[i].seg == SG[ES]) { if (R[BX] <= blocks[i].size || i == nblocks - 1) { if (i == nblocks - 1) heap_top = blocks[i].seg + R[BX] + 1; blocks[i].size = R[BX]; ok = 1; } } if (ok) set_cf(0); else { R[AX] = 8; R[BX] = 0; set_cf(1); } }
    } break;
    case 0x4c: finish(al); break;
    case 0x00: finish(0); break;
    default: fprintf(stderr, "[dos] unhandled INT21 AH=%02x at %04x:%04x\n", ah, rw(LIN(SG[SS], R[SP] + 2)), rw(LIN(SG[SS], R[SP]))); set_cf(1); R[AX] = 1; break;
    }
}
static void host_int(int n) {
    switch (n) {
    case 0x10: {
        u8 ah = R[AX] >> 8;
        if (ah == 0) { set_mode(R[AX] & 0x7f); fprintf(stderr, "[bios] set mode %02x\n", R[AX] & 0xff); }
        else if (ah == 0x0f) { R[AX] = (80 << 8) | video_mode; R[BX] &= 0xff; }
        else if (ah == 0x10 && (R[AX] & 0xff) == 0) { ega_pal[R[BX] & 15] = R[BX] >> 8; }
        else if (ah == 0x12) { if ((R[BX] & 0xff) == 0x10) R[BX] = 0x0003; R[CX] = 0x0009; }
        else if (ah == 0x1a) { R[AX] &= 0xff00; } // no VGA
        else if (trace) fprintf(stderr, "[bios] INT10 AX=%04x\n", R[AX]);
    } break;
    case 0x08: { u32 t = mem[0x46c] | (mem[0x46d] << 8) | (mem[0x46e] << 16) | ((u32)mem[0x46f] << 24); t++; mem[0x46c] = t; mem[0x46d] = t >> 8; mem[0x46e] = t >> 16; mem[0x46f] = t >> 24; } break;
    case 0x11: R[AX] = 0x0021; break;
    case 0x12: R[AX] = 640; break;
    case 0x16: {
        u8 ah = R[AX] >> 8;
#ifdef EMU_LIB
        if (ah == 0 || ah == 0x10) inbox_drain();
        if ((ah == 0 || ah == 0x10) && !key_avail()) { if (engine_quit) finish(0); usleep(1000); ip -= 3; flags |= IF; break; }   // retry the trap
#endif
        if (ah == 0 || ah == 0x10) { need_key(1); R[AX] = key_pop(); }
        else if (ah == 1 || ah == 0x11) { need_key(0); if (key_avail()) { R[AX] = keyq[kq_h]; set_zf_frame(0); } else set_zf_frame(1); }
        else if (ah == 2) R[AX] &= 0xff00;
    } break;
    case 0x1a: { tick += 1; if ((R[AX] >> 8) == 0) { R[CX] = (tick >> 16) & 0xffff; R[DX] = tick & 0xffff; R[AX] &= 0xff00; } } break;
    case 0x21: dos_int21(); break;
    case 0x33: R[AX] = 0; break; // no mouse
    default: if (trace) fprintf(stderr, "[int] unhandled INT %02x\n", n);
    }
}

// ---------------- CPU ----------------
static const u8 parity[256] = {
#define P2(n) n, n^1, n^1, n
#define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
    P6(1), P6(0), P6(0), P6(1)
};
static inline void setf(u16 m, int c) { if (c) flags |= m; else flags &= ~m; }
static inline void szp8(u8 v) { setf(ZF, v == 0); setf(SF, v & 0x80); setf(PF, parity[v]); }
static inline void szp16(u16 v) { setf(ZF, v == 0); setf(SF, v & 0x8000); setf(PF, parity[v & 0xff]); }

static int seg_ovr = -1; static int rep = 0;
// call profile (-P file): how often each routine is entered, keyed by flat address
static inline void prof_call(u16 seg, u16 off) { if (prof) { u32 l = LIN(seg, off); if (l < 0x100000) { prof[l]++; } } }
// modrm decoding
static int m_mod, m_reg, m_rm; static u32 m_ea; static u16 m_off; static int m_seg;
static void modrm(void) {
    u8 b = fetch8(); m_mod = b >> 6; m_reg = (b >> 3) & 7; m_rm = b & 7;
    if (m_mod == 3) return;
    u16 o = 0; int sg = DS;
    switch (m_rm) {
    case 0: o = R[BX] + R[SI]; break; case 1: o = R[BX] + R[DI]; break;
    case 2: o = R[BP] + R[SI]; sg = SS; break; case 3: o = R[BP] + R[DI]; sg = SS; break;
    case 4: o = R[SI]; break; case 5: o = R[DI]; break;
    case 6: if (m_mod == 0) o = fetch16(); else { o = R[BP]; sg = SS; } break;
    case 7: o = R[BX]; break;
    }
    if (m_mod == 1) o += (s8)fetch8(); else if (m_mod == 2) o += fetch16();
    if (seg_ovr >= 0) sg = seg_ovr;
    m_off = o; m_seg = sg; m_ea = LIN(SG[sg], o);
}
static inline u8 *reg8p(int r) { return (r < 4) ? (u8 *)&R[r] : ((u8 *)&R[r - 4]) + 1; } // little-endian host
static inline u8 getr8(int r) { return *reg8p(r); }
static inline void setr8(int r, u8 v) { *reg8p(r) = v; }
static inline u8 rm8(void) { return m_mod == 3 ? getr8(m_rm) : rb(m_ea); }
static inline u16 rm16(void) { return m_mod == 3 ? R[m_rm] : (u16)(rb(m_ea) | (rb(LIN(SG[m_seg], m_off + 1)) << 8)); }
static inline void wrm8(u8 v) { if (m_mod == 3) setr8(m_rm, v); else wb(m_ea, v); }
static inline void wrm16(u16 v) { if (m_mod == 3) R[m_rm] = v; else { wb(m_ea, v & 0xff); wb(LIN(SG[m_seg], m_off + 1), v >> 8); } }

static u8 alu8(int op, u8 a, u8 b) {
    u16 r; int c = (flags & CF) ? 1 : 0;
    switch (op) {
    case 0: r = a + b; setf(CF, r > 0xff); setf(AF, (a ^ b ^ r) & 0x10); setf(OF, (~(a ^ b) & (a ^ r)) & 0x80); break;
    case 1: r = a | b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
    case 2: r = a + b + c; setf(CF, r > 0xff); setf(AF, (a ^ b ^ r) & 0x10); setf(OF, (~(a ^ b) & (a ^ r)) & 0x80); break;
    case 3: r = a - b - c; setf(CF, (u16)a < (u16)b + c); setf(AF, (a ^ b ^ r) & 0x10); setf(OF, ((a ^ b) & (a ^ r)) & 0x80); break;
    case 4: r = a & b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
    case 5: case 7: r = a - b; setf(CF, a < b); setf(AF, (a ^ b ^ r) & 0x10); setf(OF, ((a ^ b) & (a ^ r)) & 0x80); break;
    default: r = a ^ b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
    }
    szp8((u8)r); return (u8)r;
}
static u16 alu16(int op, u16 a, u16 b) {
    u32 r; int c = (flags & CF) ? 1 : 0;
    switch (op) {
    case 0: r = a + b; setf(CF, r > 0xffff); setf(AF, (a ^ b ^ r) & 0x10); setf(OF, (~(a ^ b) & (a ^ r)) & 0x8000); break;
    case 1: r = a | b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
    case 2: r = a + b + c; setf(CF, r > 0xffff); setf(AF, (a ^ b ^ r) & 0x10); setf(OF, (~(a ^ b) & (a ^ r)) & 0x8000); break;
    case 3: r = a - b - c; setf(CF, (u32)a < (u32)b + c); setf(AF, (a ^ b ^ r) & 0x10); setf(OF, ((a ^ b) & (a ^ r)) & 0x8000); break;
    case 4: r = a & b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
    case 5: case 7: r = a - b; setf(CF, a < b); setf(AF, (a ^ b ^ r) & 0x10); setf(OF, ((a ^ b) & (a ^ r)) & 0x8000); break;
    default: r = a ^ b; setf(CF, 0); setf(OF, 0); setf(AF, 0); break;
    }
    szp16((u16)r); return (u16)r;
}
// For native code that must leave the flags as an original instruction would before calling original code that
// stores them (the runtime's Intr hands the caller's flags to the BIOS and saves what comes back).
void emu_flags_cmp8(u8 a, u8 b) { alu8(7, a, b); }
void emu_flags_sub16(u16 a, u16 b) { alu16(5, a, b); }
void emu_flags_add16(u16 a, u16 b) { alu16(0, a, b); }
u16 emu_sp(void) { return R[SP]; }
// Fixes of the original's bugs (port/fixes.h): on in the app (off with POOLRAD_ORIGINAL=1), off in the headless
// emulator unless POOLRAD_FIXES=1, so that the test suite keeps comparing with the original exactly.
int game_fixes(void) {
    static int on = -1;
#ifdef EMU_LIB
    if (on < 0) on = getenv("POOLRAD_ORIGINAL") == NULL;
#else
    if (on < 0) on = getenv("POOLRAD_FIXES") != NULL;
#endif
    return on;
}
u16 emu_flags_get(void) { return flags; }
void emu_set_ip(u16 v) { ip = v; }
void emu_flags_set(u16 f) { flags = f; }
// The BIOS/DOS services (host_int) called straight from C, as if by INT n with the current flags: the handler's
// changes to the flags in its return frame (CF, ZF) come back as the new flags, as after IRET. A blocking key read
// waits here rather than by re-running an INT instruction.
static void host_int(int n);
#ifdef EMU_LIB
static void serve_ticks(void);
#endif
void emu_idle_ms(unsigned ms);
u16 emu_host_call(int n) {
    u8 ah = R[AX] >> 8;
    if (rule_n || keep_n) rule_apply();
#ifdef EMU_LIB
    serve_ticks();                            // C code runs no instructions, so due timer ticks are served at its calls
    if (n == 0x16 && (ah == 0 || ah == 0x10)) for (;;) { inbox_drain(); if (key_avail()) break; if (engine_quit) finish(0); emu_idle_ms(1); }
#else
    if (n == 0x16 && (ah == 0 || ah == 0x10)) need_key(1);
#endif
    push(flags | 0xf002); push(0xf000); push((u16)(n * 4));
    flags &= ~(IF | TF);                      // as INT does: the handler runs with interrupts off
    host_int(n);
    pop(); pop(); flags = pop();
    return flags;
}
// An interrupt handler of the game's run to completion from C (the timer tick while C code waits).
static void do_int(int n); void step(void);
static void run_to_sentinel(u16 sp0, int poll);
void emu_run_int(int n) {
    u16 cs = SG[CS], ip0 = ip, sp0 = R[SP];
    SG[CS] = 0xf000; ip = 0x0800; do_int(n);
    run_to_sentinel(sp0, 0);
    SG[CS] = cs; ip = ip0;
}
// Turbo Pascal's Delay done in C: in the app real time passes, with the timer interrupt still served; the
// headless emulator's time is counted in instructions, so there it passes at once.
void emu_idle_ms(unsigned ms);
void emu_set_df(int on) { if (on) flags |= DF; else flags &= ~DF; }
static u8 inc8(u8 a) { u8 r = a + 1; setf(OF, r == 0x80); setf(AF, (r & 15) == 0); szp8(r); return r; }
static u8 dec8(u8 a) { u8 r = a - 1; setf(OF, r == 0x7f); setf(AF, (r & 15) == 15); szp8(r); return r; }
static u16 inc16(u16 a) { u16 r = a + 1; setf(OF, r == 0x8000); setf(AF, (r & 15) == 0); szp16(r); return r; }
static u16 dec16(u16 a) { u16 r = a - 1; setf(OF, r == 0x7fff); setf(AF, (r & 15) == 15); szp16(r); return r; }

static u16 shift(int op, u16 v, int n, int w) {
    int bits = w ? 16 : 8; u16 msb = w ? 0x8000 : 0x80; u16 maskv = w ? 0xffff : 0xff; int c;
    if (n == 0) return v;
    v &= maskv;
    for (int i = 0; i < n; i++) {
        switch (op) {
        case 0: c = (v & msb) != 0; v = ((v << 1) | c) & maskv; setf(CF, c); break;                        // ROL
        case 1: c = v & 1; v = (v >> 1) | (c ? msb : 0); setf(CF, c); break;                                // ROR
        case 2: c = (v & msb) != 0; v = ((v << 1) | ((flags & CF) ? 1 : 0)) & maskv; setf(CF, c); break;    // RCL
        case 3: c = v & 1; v = (v >> 1) | ((flags & CF) ? msb : 0); setf(CF, c); break;                     // RCR
        case 4: case 6: c = (v & msb) != 0; v = (v << 1) & maskv; setf(CF, c); break;                       // SHL
        case 5: c = v & 1; v >>= 1; setf(CF, c); break;                                                     // SHR
        case 7: c = v & 1; v = (v >> 1) | (v & msb); setf(CF, c); break;                                    // SAR
        }
    }
    (void)bits;
    switch (op) {
    case 0: case 2: case 4: case 6: setf(OF, ((v & msb) != 0) ^ ((flags & CF) != 0)); break;
    case 1: case 3: setf(OF, ((v & msb) != 0) ^ ((v & (msb >> 1)) != 0)); break;
    case 5: setf(OF, n == 1 ? ((v & (msb >> 1)) != 0) : 0); break;
    case 7: setf(OF, 0); break;
    }
    if (op >= 4) { if (w) szp16(v); else szp8((u8)v); }
    return v;
}
static void do_int(int n) {
    push(flags | 0xf002); push(SG[CS]); push(ip);
    flags &= ~(IF | TF);
    ip = rw(n * 4); SG[CS] = rw(n * 4 + 2);
}
static int cond(int c) {
    int r;
    switch (c >> 1) {
    case 0: r = flags & OF; break; case 1: r = flags & CF; break; case 2: r = flags & ZF; break;
    case 3: r = flags & (CF | ZF); break; case 4: r = flags & SF; break; case 5: r = flags & PF; break;
    case 6: r = ((flags & SF) != 0) != ((flags & OF) != 0); break;
    default: r = (flags & ZF) || (((flags & SF) != 0) != ((flags & OF) != 0)); break;
    }
    return (c & 1) ? !r : !!r;
}
static void unimpl(u8 op) {
    fprintf(stderr, "[emu] unimplemented opcode %02x at %04x:%04x (icount %llu)\n", op, SG[CS], (u16)(ip - 1), icount);
    save_frame("crash"); exit(2);
}
static void string_op(u8 op) {
    int w = op & 1; int d = (flags & DF) ? -1 : 1; if (w) d *= 2;
    int sseg = seg_ovr >= 0 ? seg_ovr : DS;
    for (;;) {
        if (rep && R[CX] == 0) break;
        switch (op & 0xfe) {
        case 0xa4: if (w) ww(LIN(SG[ES], R[DI]), rw(LIN(SG[sseg], R[SI]))); else wb(LIN(SG[ES], R[DI]), rb(LIN(SG[sseg], R[SI]))); R[SI] += d; R[DI] += d; break;
        case 0xa6: if (w) alu16(7, rw(LIN(SG[sseg], R[SI])), rw(LIN(SG[ES], R[DI]))); else alu8(7, rb(LIN(SG[sseg], R[SI])), rb(LIN(SG[ES], R[DI]))); R[SI] += d; R[DI] += d; break;
        case 0xaa: if (w) ww(LIN(SG[ES], R[DI]), R[AX]); else wb(LIN(SG[ES], R[DI]), R[AX] & 0xff); R[DI] += d; break;
        case 0xac: if (w) R[AX] = rw(LIN(SG[sseg], R[SI])); else setr8(0, rb(LIN(SG[sseg], R[SI]))); R[SI] += d; break;
        case 0xae: if (w) alu16(7, R[AX], rw(LIN(SG[ES], R[DI]))); else alu8(7, R[AX] & 0xff, rb(LIN(SG[ES], R[DI]))); R[DI] += d; break;
        }
        if (!rep) break;
        R[CX]--;
        if ((op & 0xfe) == 0xa6 || (op & 0xfe) == 0xae) { if (rep == 1 && !(flags & ZF)) break; if (rep == 2 && (flags & ZF)) break; }
    }
}
// ---- native hooks: ported functions replace original code at their entry points ----
typedef int (*native_fn)(void);     // returns the number of argument bytes to remove (Pascal: callee cleans)
#define MAXHOOK 2048
static struct { u32 lin; native_fn fn; int far_ret; const char *name; unsigned long calls, declined; } hooktab[MAXHOOK]; static int nhooks;
static u8 hookflag[0x100000];
u16 load_seg = 0x0120;
int hooks_enabled = 1;
void emu_hook(u16 gseg, u16 off, native_fn fn, int far_ret, const char *name) {
    if (gseg == 0x1a40 && off == 0x029c) delay_native = 1;
    u32 lin = ((u32)(gseg - 0x1000 + load_seg) << 4) + off;
    if (nhooks >= MAXHOOK) { fprintf(stderr, "[emu] too many hooks (MAXHOOK %d)\n", MAXHOOK); exit(1); }
    hooktab[nhooks].lin = lin; hooktab[nhooks].fn = fn; hooktab[nhooks].far_ret = far_ret; hooktab[nhooks].name = name; nhooks++; hookflag[lin] = 1;
}
void emu_hook_stats(void) { for (int i = 0; i < nhooks; i++) fprintf(stderr, "[hook] %-28s %lu calls%s\n", hooktab[i].name, hooktab[i].calls, hooktab[i].declined ? "  (DECLINED some calls: original code ran instead)" : ""); }
#define EMU_HOOK_JUMPED 0x7fff
static int run_hook(u32 lin) {
    for (int i = 0; i < nhooks; i++) if (hooktab[i].lin == lin) {
        hooktab[i].calls++;                   // counted on entry: some routines (the exploration loop) rarely return
        if (rule_n || keep_n) rule_apply();    // test rules act as promptly in C code as every 256 instructions in the original
        int n = hooktab[i].fn();
        if (n < 0) { hooktab[i].calls--; hooktab[i].declined++; return 0; }      // the native routine declined: run the original this time
        if (n == EMU_HOOK_JUMPED) return 1;     // an interrupt handler: it has done its IRET or chained on (CS:IP and stack set)
        if (hooktab[i].far_ret) { ip = pop(); SG[CS] = pop(); } else ip = pop();
        R[SP] += n;
        return 1;
    }
    return 0;
}
void step(void);
static void pcall_poll(void);
int rom_exec_public(void);
// The emulator's BIOS stubs at F000 are three kinds of instruction: the host trap 0F FF n (the BIOS/DOS service in C),
// INT n, and IRET. Carried out here directly (counted as the instructions they are, so time runs as before), so that
// no 8086 instruction needs interpreting at all once the game itself is in C.
static void host_int(int n); static void do_int(int n);
static int rom_exec(void) {
    if (SG[CS] != 0xf000) return 0;
    u32 a = LIN(SG[CS], ip);
    if (mem[a] == 0x0f && mem[a + 1] == 0xff) { icount++; ip += 3; host_int(mem[a + 2]); return 1; }
    if (mem[a] == 0xcd) { icount++; ip += 2; do_int(mem[a + 1]); return 1; }
    if (mem[a] == 0xcf) { icount++; ip = pop(); SG[CS] = pop(); flags = (pop() & 0x0fd5) | 0x0002; return 1; }
    return 0;
}
int rom_exec_public(void) { return rom_exec(); }
// Debugging aids: run original code at gseg:off as a NEAR routine (hooks off), and switch hooks off and on.
void emu_ncall_original(u16 gseg, u16 off) { u16 cs = SG[CS], ip0 = ip, sp0 = R[SP]; int h = hooks_enabled; hooks_enabled = 0; push(0xfff0); SG[CS] = gseg - 0x1000 + load_seg; ip = off; while (!(ip == 0xfff0 && R[SP] == sp0)) step(); hooks_enabled = h; SG[CS] = cs; ip = ip0; }        // for interrupt handlers in C that return or chain themselves (EMU_HOOK_JUMPED)
// Run until the routine called returns to the sentinel F000:0800: routines in C called directly (a far jump stub
// followed first), the BIOS stubs done directly, anything else interpreted.
static int run_hook(u32 lin);
static void run_to_sentinel(u16 sp0, int poll) {
    while (!(SG[CS] == 0xf000 && ip == 0x0800 && R[SP] == sp0)) {
        while (mem[LIN(SG[CS], ip)] == 0xea && SG[CS] != 0xf000) { u32 j = LIN(SG[CS], ip); ip = rw(j + 1); SG[CS] = rw(j + 3); }
        u32 lin = LIN(SG[CS], ip);
        if (hooks_enabled && lin < 0x100000 && hookflag[lin] && run_hook(lin)) { if (poll) pcall_poll(); continue; }
        if (!rom_exec()) step();
        if (poll) pcall_poll();
    }
}
// Call original (still emulated) code from native code. Far Pascal: args pushed left to right, callee cleans.
u32 emu_pcall(u16 gseg, u16 off, int nargs, ...) {
    va_list ap; va_start(ap, nargs); for (int i = 0; i < nargs; i++) push((u16)va_arg(ap, int)); va_end(ap);
    u16 save_cs = SG[CS], save_ip = ip; u16 sp0 = R[SP] + nargs * 2;
    push(0xf000); push(0x0800); SG[CS] = gseg - 0x1000 + load_seg; ip = off;
    run_to_sentinel(sp0, 1);
    SG[CS] = save_cs; ip = save_ip;
    return ((u32)R[DX] << 16) | R[AX];
}
// A Turbo Pascal string function: the caller pushes the result buffer first (2 words, part of nargs) and the callee
// returns leaving that pointer on the stack, so wait for SP to be 4 bytes lower and drop it afterwards.
u32 emu_pcall_strfn(u16 gseg, u16 off, int nargs, ...) {
    va_list ap; va_start(ap, nargs); for (int i = 0; i < nargs; i++) push((u16)va_arg(ap, int)); va_end(ap);
    u16 save_cs = SG[CS], save_ip = ip; u16 sp0 = R[SP] + (nargs - 2) * 2;
    push(0xf000); push(0x0800); SG[CS] = gseg - 0x1000 + load_seg; ip = off;
    run_to_sentinel(sp0, 1);
    R[SP] += 4;
    SG[CS] = save_cs; ip = save_ip;
    return ((u32)R[DX] << 16) | R[AX];
}
void step(void) {
    if (snap_pending[0]) snapshot_save();
    if (!delay_native && ((u32)SG[CS] << 4) + ip == delay_lin) delay_entry();
    if (cover) { u32 l = ((u32)SG[CS] << 4) + ip; if (l < 0x100000) cover[l] = 1; }
    { static int tr = -1; static u32 rl; if (tr < 0) { tr = getenv("RANDTRACE") != NULL; rl = ((u32)(0x1af8 - 0x1000 + load_seg) << 4) + 0x11c0; }
      if (tr && !hooks_enabled && ((u32)SG[CS] << 4) + ip == rl) { u32 a = LIN(SG[SS], R[SP] + 4); fprintf(stderr, "[rand] range %u tick %u seed %08x\n", rw(a), rw(0x46c), (u32)rw(0x3dec0 + 0x4428) | (u32)rw(0x3dec0 + 0x442a) << 16); } }
    seg_ovr = -1; rep = 0; u8 op;
    if (hooks_enabled) { u32 lin = LIN(SG[CS], ip); if (lin < 0x100000 && hookflag[lin] && run_hook(lin)) return; }
#ifdef NATIVE_ONLY
    // The app is built without the 8086 interpreter: everything the game runs is C, so this is never reached.
    if (rom_exec_public()) return;
    fprintf(stderr, "[emu] 8086 code reached at %04x:%04x in a build without the interpreter\n", SG[CS], ip);
    finish(3);
}
#else
    for (;;) { // prefixes
        op = fetch8();
        if (op == 0x26) seg_ovr = ES; else if (op == 0x2e) seg_ovr = CS; else if (op == 0x36) seg_ovr = SS; else if (op == 0x3e) seg_ovr = DS;
        else if (op == 0xf3) rep = 1; else if (op == 0xf2) rep = 2; else if (op == 0xf0) ; else break;
    }
    icount++;
    interp_icount++; if (SG[CS] != 0xf000) game_icount++;        // interpreted: all, and those of the game itself (not the BIOS stubs at F000)
    if ((rule_n || keep_n) && !(icount & 0xff)) rule_apply();
    if (op < 0x40 && (op & 7) < 6) { // ALU
        int a = op >> 3; u16 x, y;
        switch (op & 7) {
        case 0: modrm(); x = alu8(a, rm8(), getr8(m_reg)); if (a != 7) wrm8((u8)x); break;
        case 1: modrm(); x = alu16(a, rm16(), R[m_reg]); if (a != 7) wrm16(x); break;
        case 2: modrm(); x = alu8(a, getr8(m_reg), rm8()); if (a != 7) setr8(m_reg, (u8)x); break;
        case 3: modrm(); x = alu16(a, R[m_reg], rm16()); if (a != 7) R[m_reg] = x; break;
        case 4: y = fetch8(); x = alu8(a, R[AX] & 0xff, (u8)y); if (a != 7) setr8(0, (u8)x); break;
        case 5: y = fetch16(); x = alu16(a, R[AX], y); if (a != 7) R[AX] = x; break;
        }
        return;
    }
    switch (op) {
    case 0x06: push(SG[ES]); break; case 0x07: SG[ES] = pop(); break;
    case 0x0e: push(SG[CS]); break;
    case 0x16: push(SG[SS]); break; case 0x17: SG[SS] = pop(); break;
    case 0x1e: push(SG[DS]); break; case 0x1f: SG[DS] = pop(); break;
    case 0x27: { u8 al = R[AX] & 0xff; int cf = flags & CF; if ((al & 15) > 9 || (flags & AF)) { al += 6; setf(AF, 1); } else setf(AF, 0); if ((R[AX] & 0xff) > 0x99 || cf) { al += 0x60; setf(CF, 1); } else setf(CF, 0); setr8(0, al); szp8(al); } break;
    case 0x2f: { u8 al = R[AX] & 0xff, old = al; int cf = flags & CF; if ((al & 15) > 9 || (flags & AF)) { al -= 6; setf(AF, 1); } else setf(AF, 0); if (old > 0x99 || cf) { al -= 0x60; setf(CF, 1); } else setf(CF, 0); setr8(0, al); szp8(al); } break;
    case 0x37: if ((R[AX] & 15) > 9 || (flags & AF)) { R[AX] += 0x106; setf(AF, 1); setf(CF, 1); } else { setf(AF, 0); setf(CF, 0); } R[AX] &= 0xff0f; break;
    case 0x3f: if ((R[AX] & 15) > 9 || (flags & AF)) { R[AX] -= 6; R[AX] -= 0x100; setf(AF, 1); setf(CF, 1); } else { setf(AF, 0); setf(CF, 0); } R[AX] &= 0xff0f; break;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: R[op & 7] = inc16(R[op & 7]); break;
    case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: R[op & 7] = dec16(R[op & 7]); break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x55: case 0x56: case 0x57: push(R[op & 7]); break;
    case 0x54: push(R[SP] - 2); break; // 8086 behaviour
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: R[op & 7] = pop(); break;
    case 0x60: { u16 sp = R[SP]; push(R[AX]); push(R[CX]); push(R[DX]); push(R[BX]); push(sp); push(R[BP]); push(R[SI]); push(R[DI]); } break;
    case 0x61: R[DI] = pop(); R[SI] = pop(); R[BP] = pop(); pop(); R[BX] = pop(); R[DX] = pop(); R[CX] = pop(); R[AX] = pop(); break;
    case 0x68: push(fetch16()); break;
    case 0x6a: push((u16)(s8)fetch8()); break;
    case 0x69: { modrm(); s32 r = (s32)(s16)rm16() * (s16)fetch16(); R[m_reg] = (u16)r; setf(CF | OF, r != (s16)r); } break;
    case 0x6b: { modrm(); s16 v = (s16)rm16(); s32 r = (s32)v * (s8)fetch8(); R[m_reg] = (u16)r; setf(CF | OF, r != (s16)r); } break;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f: { s8 d = (s8)fetch8(); if (cond(op & 15)) ip += d; } break;
    case 0x80: case 0x82: { modrm(); u8 a = rm8(); u8 b = fetch8(); u8 r = alu8(m_reg, a, b); if (m_reg != 7) wrm8(r); } break;
    case 0x81: { modrm(); u16 a = rm16(); u16 b = fetch16(); u16 r = alu16(m_reg, a, b); if (m_reg != 7) wrm16(r); } break;
    case 0x83: { modrm(); u16 a = rm16(); u16 b = (u16)(s8)fetch8(); u16 r = alu16(m_reg, a, b); if (m_reg != 7) wrm16(r); } break;
    case 0x84: modrm(); alu8(4, rm8(), getr8(m_reg)); break;
    case 0x85: modrm(); alu16(4, rm16(), R[m_reg]); break;
    case 0x86: { modrm(); u8 t = rm8(); wrm8(getr8(m_reg)); setr8(m_reg, t); } break;
    case 0x87: { modrm(); u16 t = rm16(); wrm16(R[m_reg]); R[m_reg] = t; } break;
    case 0x88: modrm(); wrm8(getr8(m_reg)); break;
    case 0x89: modrm(); wrm16(R[m_reg]); break;
    case 0x8a: modrm(); setr8(m_reg, rm8()); break;
    case 0x8b: modrm(); R[m_reg] = rm16(); break;
    case 0x8c: modrm(); wrm16(SG[m_reg & 3]); break;
    case 0x8d: modrm(); R[m_reg] = m_off; break;
    case 0x8e: modrm(); SG[m_reg & 3] = rm16(); break;
    case 0x8f: { modrm(); u16 v = pop(); wrm16(v); } break;
    case 0x90: break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: { u16 t = R[AX]; R[AX] = R[op & 7]; R[op & 7] = t; } break;
    case 0x98: R[AX] = (u16)(s8)(R[AX] & 0xff); break;
    case 0x99: R[DX] = (R[AX] & 0x8000) ? 0xffff : 0; break;
    case 0x9a: { u16 o = fetch16(), s = fetch16(); push(SG[CS]); push(ip); ip = o; SG[CS] = s; prof_call(s, o); } break;
    case 0x9b: break;
    case 0x9c: push(flags | 0xf002); break;
    case 0x9d: flags = (pop() & 0x0fd5) | 2; break;
    case 0x9e: flags = (flags & 0xff00) | ((R[AX] >> 8) & 0xd5) | 2; break;
    case 0x9f: setr8(4, (u8)(flags & 0xff)); break;
    case 0xa0: { u16 o = fetch16(); setr8(0, rb(LIN(SG[seg_ovr >= 0 ? seg_ovr : DS], o))); } break;
    case 0xa1: { u16 o = fetch16(); R[AX] = rw(LIN(SG[seg_ovr >= 0 ? seg_ovr : DS], o)); } break;
    case 0xa2: { u16 o = fetch16(); wb(LIN(SG[seg_ovr >= 0 ? seg_ovr : DS], o), R[AX] & 0xff); } break;
    case 0xa3: { u16 o = fetch16(); ww(LIN(SG[seg_ovr >= 0 ? seg_ovr : DS], o), R[AX]); } break;
    case 0xa4: case 0xa5: case 0xa6: case 0xa7: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf: string_op(op); break;
    case 0xa8: alu8(4, R[AX] & 0xff, fetch8()); break;
    case 0xa9: alu16(4, R[AX], fetch16()); break;
    case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7: setr8(op & 7, fetch8()); break;
    case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf: R[op & 7] = fetch16(); break;
    case 0xc0: { modrm(); u8 v = rm8(); int n = fetch8() & 31; wrm8((u8)shift(m_reg, v, n, 0)); } break;
    case 0xc1: { modrm(); u16 v = rm16(); int n = fetch8() & 31; wrm16(shift(m_reg, v, n, 1)); } break;
    case 0xc2: { u16 n = fetch16(); ip = pop(); R[SP] += n; } break;
    case 0xc3: ip = pop(); break;
    case 0xc4: case 0xc5: { modrm(); u16 o = rw(m_ea), s = rw(LIN(SG[m_seg], m_off + 2)); R[m_reg] = o; SG[op == 0xc4 ? ES : DS] = s; } break;
    case 0xc6: { modrm(); wrm8(fetch8()); } break;
    case 0xc7: { modrm(); wrm16(fetch16()); } break;
    case 0xc8: { u16 n = fetch16(); fetch8(); push(R[BP]); R[BP] = R[SP]; R[SP] -= n; } break;
    case 0xc9: R[SP] = R[BP]; R[BP] = pop(); break;
    case 0xca: { u16 n = fetch16(); ip = pop(); SG[CS] = pop(); R[SP] += n; } break;
    case 0xcb: ip = pop(); SG[CS] = pop(); break;
    case 0xcc: do_int(3); break;
    case 0xcd: { u8 n = fetch8(); do_int(n); } break;
    case 0xce: if (flags & OF) do_int(4); break;
    case 0xcf: ip = pop(); SG[CS] = pop(); flags = (pop() & 0x0fd5) | 2; break;
    case 0xd0: { modrm(); wrm8((u8)shift(m_reg, rm8(), 1, 0)); } break;
    case 0xd1: { modrm(); wrm16(shift(m_reg, rm16(), 1, 1)); } break;
    case 0xd2: { modrm(); wrm8((u8)shift(m_reg, rm8(), R[CX] & 0xff, 0)); } break;
    case 0xd3: { modrm(); wrm16(shift(m_reg, rm16(), R[CX] & 0xff, 1)); } break;
    case 0xd4: { u8 b = fetch8(); u8 al = R[AX] & 0xff; if (!b) { do_int(0); break; } setr8(4, al / b); setr8(0, al % b); szp8(R[AX] & 0xff); } break;
    case 0xd5: { u8 b = fetch8(); u8 al = (u8)((R[AX] & 0xff) + (R[AX] >> 8) * b); R[AX] = al; szp8(al); } break;
    case 0xd7: setr8(0, rb(LIN(SG[seg_ovr >= 0 ? seg_ovr : DS], (u16)(R[BX] + (R[AX] & 0xff))))); break;
    case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf: modrm(); break; // FPU ignored
    case 0xe0: { s8 d = (s8)fetch8(); if (--R[CX] && !(flags & ZF)) ip += d; } break;
    case 0xe1: { s8 d = (s8)fetch8(); if (--R[CX] && (flags & ZF)) ip += d; } break;
    case 0xe2: { s8 d = (s8)fetch8(); if (--R[CX]) ip += d; } break;
    case 0xe3: { s8 d = (s8)fetch8(); if (!R[CX]) ip += d; } break;
    case 0xe4: setr8(0, port_in(fetch8())); break;
    case 0xe5: { u8 p = fetch8(); R[AX] = port_in(p) | (port_in(p + 1) << 8); } break;
    case 0xe6: port_out(fetch8(), R[AX] & 0xff); break;
    case 0xe7: { u8 p = fetch8(); port_out(p, R[AX] & 0xff); port_out(p + 1, R[AX] >> 8); } break;
    case 0xe8: { u16 d = fetch16(); push(ip); ip += d; prof_call(SG[CS], ip); } break;
    case 0xe9: { u16 d = fetch16(); ip += d; } break;
    case 0xea: { u16 o = fetch16(), s = fetch16(); ip = o; SG[CS] = s; } break;
    case 0xeb: { s8 d = (s8)fetch8(); ip += d; } break;
    case 0xec: setr8(0, port_in(R[DX])); break;
    case 0xed: R[AX] = port_in(R[DX]) | (port_in(R[DX] + 1) << 8); break;
    case 0xee: port_out(R[DX], R[AX] & 0xff); break;
    case 0xef: port_out(R[DX], R[AX] & 0xff); port_out(R[DX] + 1, R[AX] >> 8); break;
    case 0xf4: fprintf(stderr, "[emu] HLT\n"); finish(0); break;
    case 0xf5: flags ^= CF; break;
    case 0xf6: {
        modrm(); u8 v = rm8();
        switch (m_reg) {
        case 0: case 1: alu8(4, v, fetch8()); break;
        case 2: wrm8(~v); break;
        case 3: { u8 r = alu8(5, 0, v); setf(CF, v != 0); wrm8(r); } break;
        case 4: { u16 r = (u16)(R[AX] & 0xff) * v; R[AX] = r; setf(CF | OF, r > 0xff); } break;
        case 5: { s16 r = (s16)(s8)(R[AX] & 0xff) * (s8)v; R[AX] = (u16)r; setf(CF | OF, r != (s8)r); } break;
        case 6: { if (!v) { do_int(0); break; } u16 a = R[AX]; u16 q = a / v; if (q > 0xff) { do_int(0); break; } setr8(0, (u8)q); setr8(4, (u8)(a % v)); } break;
        case 7: { if (!v) { do_int(0); break; } s16 a = (s16)R[AX]; s16 q = a / (s8)v; if (q > 127 || q < -128) { do_int(0); break; } setr8(0, (u8)q); setr8(4, (u8)(a % (s8)v)); } break;
        }
    } break;
    case 0xf7: {
        modrm(); u16 v = rm16();
        switch (m_reg) {
        case 0: case 1: alu16(4, v, fetch16()); break;
        case 2: wrm16(~v); break;
        case 3: { u16 r = alu16(5, 0, v); setf(CF, v != 0); wrm16(r); } break;
        case 4: { u32 r = (u32)R[AX] * v; R[AX] = (u16)r; R[DX] = r >> 16; setf(CF | OF, R[DX] != 0); } break;
        case 5: { s32 r = (s32)(s16)R[AX] * (s16)v; R[AX] = (u16)r; R[DX] = (u16)(r >> 16); setf(CF | OF, r != (s16)r); } break;
        case 6: { if (!v) { do_int(0); break; } u32 a = ((u32)R[DX] << 16) | R[AX]; u32 q = a / v; if (q > 0xffff) { do_int(0); break; } R[AX] = (u16)q; R[DX] = (u16)(a % v); } break;
        case 7: { if (!v) { do_int(0); break; } s32 a = (s32)(((u32)R[DX] << 16) | R[AX]); s32 q = a / (s16)v; if (q > 32767 || q < -32768) { do_int(0); break; } R[AX] = (u16)q; R[DX] = (u16)(a % (s16)v); } break;
        }
    } break;
    case 0xf8: flags &= ~CF; break; case 0xf9: flags |= CF; break;
    case 0xfa: flags &= ~IF; break; case 0xfb: flags |= IF; break;
    case 0xfc: flags &= ~DF; break; case 0xfd: flags |= DF; break;
    case 0xfe: { modrm(); u8 v = rm8(); if (m_reg == 0) wrm8(inc8(v)); else if (m_reg == 1) wrm8(dec8(v)); else unimpl(op); } break;
    case 0xff: {
        modrm();
        switch (m_reg) {
        case 0: wrm16(inc16(rm16())); break;
        case 1: wrm16(dec16(rm16())); break;
        case 2: { u16 t = rm16(); push(ip); ip = t; prof_call(SG[CS], ip); } break;
        case 3: { u16 o = rw(m_ea), s = rw(LIN(SG[m_seg], m_off + 2)); push(SG[CS]); push(ip); ip = o; SG[CS] = s; prof_call(s, o); } break;
        case 4: ip = rm16(); break;
        case 5: { u16 o = rw(m_ea), s = rw(LIN(SG[m_seg], m_off + 2)); ip = o; SG[CS] = s; } break;
        case 6: push(rm16()); break;
        default: unimpl(op);
        }
    } break;
    case 0x0f: { // host trap: 0F FF nn
        u8 b = fetch8(); if (b != 0xff) unimpl(op); u8 n = fetch8(); host_int(n);
    } break;
    default: unimpl(op);
    }
}
#endif

// ---- timer (IRQ0): INT 8 -> BIOS tick at 0040:006C -> INT 1Ch ----
static unsigned long long next_tick = 0; unsigned tick_insns = 20000;
static void rom_init(void) {
    for (int n = 0; n < 256; n++) { u32 a = 0xf0000 + n * 4; mem[a] = 0x0f; mem[a + 1] = 0xff; mem[a + 2] = n; mem[a + 3] = 0xcf; mem[n * 4] = (n * 4) & 0xff; mem[n * 4 + 1] = (n * 4) >> 8; mem[n * 4 + 2] = 0x00; mem[n * 4 + 3] = 0xf0; }
    static const u8 int8[] = {0x0f, 0xff, 0x08, 0xcd, 0x1c, 0xcf};          // host tick; int 1Ch; iret
    memcpy(mem + 0xf1000, int8, sizeof int8); mem[8 * 4] = 0x00; mem[8 * 4 + 1] = 0x10;
    mem[0xf0000 + 0x1c * 4] = 0xcf;                                          // default int 1Ch: iret
    mem[0x449] = 3; mem[0x44a] = 80; mem[0x484] = 24; mem[0x487] = 0x60; mem[0x410] = 0x21; mem[0x463] = 0xd4; mem[0x464] = 0x03;
}
static void do_int(int n);
#ifdef EMU_LIB
#include <time.h>
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static double tick_due = 0, pace_t0 = 0; static unsigned long long pace_i0 = 0; double emu_ips = 24e6;
// Real-time timer interrupts and a cap on emulated CPU speed.
static void mark_write(void);
static volatile int mark_pending;
static inline void timer_poll(void) {
    if (icount & 0x1ff) return;
    if (mark_pending) mark_write();
    double t = now_s();
    double ahead = (icount - pace_i0) / emu_ips - (t - pace_t0);
    if (ahead > 0.002) { usleep((useconds_t)(ahead * 1e6)); t = now_s(); }
    else if (ahead < -0.1) { pace_t0 = t; pace_i0 = icount; }
    if (t >= tick_due && (flags & IF)) {
        double period = pit_div[0] / 1193182.0;
        tick_due = (t - tick_due > 4 * period) ? t + period : tick_due + period;
        do_int(8);
    }
}
#else
static inline void timer_poll(void) {
    if (icount < next_tick || !(flags & IF)) return;
    next_tick = icount + (unsigned long long)tick_insns * pit_div[0] / 65536 + 1; do_int(8);
}
#endif
static void pcall_poll(void) { timer_poll(); }
void emu_run_int(int n);
#ifdef EMU_LIB
static void serve_ticks(void) {
    if (mark_pending) mark_write();               // a mark from the menu is noted at the next call, as C code runs no instructions
    double t = now_s();
    if (t >= tick_due && (flags & IF)) { double period = pit_div[0] / 1193182.0; tick_due = (t - tick_due > 4 * period) ? t + period : tick_due + period; emu_run_int(8); }
}
void emu_idle_ms(unsigned ms) {
    double end = now_s() + ms / 1000.0;
    for (;;) {
        double t = now_s(); if (t >= end || engine_quit) break;
        serve_ticks();
        double wait = end - t; if (tick_due - t < wait) wait = tick_due - t; if (wait > 0.001) wait = 0.001; if (wait > 0) usleep((useconds_t)(wait * 1e6));
    }
    pace_t0 = now_s(); pace_i0 = icount;      // the CPU pacing does not try to catch up on the wait
}
u16 emu_delay_calibration(void) { return 0x0e8; }   // the app's Delay is timed by the clock; the value only fills Crt's DelayCnt
#else
// Crt's start-up calibration (1A40:0064..0087) in instruction time: wait for the BIOS tick (0040:006C) to change, then
// count turns of Delay's 11-instruction loop until it changes again; DelayCnt = turns / 55. The instruction stream is
// replayed here against the same tick rule as timer_poll, with the tick interrupt really run, so the result, the
// instruction count and the tick count come out as the original's.
static void cal_instr(void) {
    icount++;
    if (icount >= next_tick && (flags & IF)) { next_tick = icount + (unsigned long long)tick_insns * pit_div[0] / 65536 + 1; emu_run_int(8); }
}
u16 emu_delay_calibration(void) {
    u8 al;
    cal_instr(); cal_instr(); cal_instr(); al = mem[0x46c]; cal_instr();           // mov ax,40 / mov es,ax / mov di,6c / mov al,[es:di]
    for (;;) { int z = al == mem[0x46c]; cal_instr(); cal_instr(); if (!z) break; } // cmp al,[es:di] / jz
    al = mem[0x46c]; cal_instr(); cal_instr(); cal_instr();                         // mov al,[es:di] / mov cx,FFFF / call
    u16 cx = 0xffff;
    for (;;) {
        for (int i = 0; i < 9; i++) cal_instr();                                     // mov bx,4 and four dec/jnz
        int z = al == mem[0x46c]; cal_instr(); cal_instr(); cx--;                   // cmp / loope
        if (!z || !cx) break;
    }
    for (int i = 0; i < 7; i++) cal_instr();                                         // ret / mov ax,37 / xchg / not / xor / div / mov
    return (u16)((u16)~cx / 0x37);
}
// Headless time is counted in instructions, and the timer interrupts that fall due while the original Delay spins
// change the game's state (t_combat7), so the C Delay advances the count by exactly what the original's loop spends:
// 9 instructions around it and 11 per iteration of its inner loop, DelayCnt (DS:84FC) iterations and 5 more per
// millisecond, running each timer interrupt due on the way as timer_poll would.
void emu_idle_ms(unsigned ms) {
    u16 cnt = rw(0x3dec0 + 0x84fc);
    unsigned long long target = icount + 9 + (unsigned long long)ms * (11ull * cnt + 5);
    while ((flags & IF) && next_tick <= target) {
        if (icount < next_tick) icount = next_tick;
        next_tick = icount + (unsigned long long)tick_insns * pit_div[0] / 65536 + 1;
        emu_run_int(8);
    }
    if (icount < target) icount = target;
}
#endif      // keep timer interrupts flowing while native code waits on original code
void port_register_hooks(void) __attribute__((weak));
static int skip_wheel(void) { return 0; }     // 1D09:00FD, the start-up code wheel question: straight back (RETF)
static void load_exe(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    static u8 buf[0x100000]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    u16 *h = (u16 *)buf; if (h[0] != 0x5a4d) { fprintf(stderr, "not MZ\n"); exit(1); }
    u32 hdr = h[4] * 16; u32 img = (h[2] - 1) * 512 + (h[1] ? h[1] : 512) - hdr; if (hdr + img > n) img = (u32)n - hdr;
    u16 env_seg = 0x0100; psp_seg = 0x0110; u16 load = psp_seg + 0x10;
    memcpy(mem + load * 16, buf + hdr, img);
    delay_lin = ((u32)(0x1a40 - 0x1000 + load) << 4) + 0x029c;      // Crt.Delay, counted for session recordings
    for (int i = 0; i < h[3]; i++) { u16 o = *(u16 *)(buf + h[12] + i * 4), s = *(u16 *)(buf + h[12] + i * 4 + 2); u32 a = (load + s) * 16 + o; u16 v = mem[a] | (mem[a + 1] << 8); v += load; mem[a] = v & 0xff; mem[a + 1] = v >> 8; }
    // environment
    u8 *e = mem + env_seg * 16; const char *envs = "PATH=C:\\"; strcpy((char *)e, envs); e += strlen(envs) + 1; *e++ = 0; *e++ = 1; *e++ = 0; strcpy((char *)e, "C:\\POOLRAD\\START.EXE");
    // PSP
    u8 *p = mem + psp_seg * 16; p[0] = 0xcd; p[1] = 0x20; p[2] = 0xff; p[3] = 0x9f; p[0x2c] = env_seg & 0xff; p[0x2d] = env_seg >> 8; p[0x80] = 0; p[0x81] = 0x0d;
    if (getenv("POOLRAD_STING")) { p[0x80] = 6; memcpy(p + 0x81, " STING\r", 7); }   // the developer switch: START STING
    prog_end = 0x9fff; heap_top = 0x9fff;
    SG[DS] = SG[ES] = psp_seg; SG[SS] = load + h[7]; R[SP] = h[8]; SG[CS] = load + h[11]; ip = h[10];
    if (port_register_hooks && hooks_enabled) port_register_hooks();
    else if (getenv("POOLRAD_SKIP_WHEEL")) { hooks_enabled = 1; emu_hook(0x1d09, 0x00fd, skip_wheel, 1, "skip_code_wheel"); }   // the original as the app plays it (replay.sh)
    fprintf(stderr, "[emu] loaded %s: image %u bytes at %04x:0000, entry %04x:%04x, ss:sp %04x:%04x\n", path, img, load, SG[CS], ip, SG[SS], R[SP]);
}
#ifdef EMU_LIB
static void cpu_init(void) {
    rom_init();
    set_mode(3);
}
static char g_exe[1024], g_game[1024], g_save[1024];
static void *engine_thread(void *arg) { (void)arg; cpu_init(); load_exe(g_exe); pace_t0 = now_s(); for (;;) { step(); timer_poll(); if (engine_quit) finish(0); } return NULL; }
void engine_start(const char *game_dir, const char *save_dir) {
#ifdef _WIN32
    if (!getenv("POOLRAD_ARROWS")) _putenv_s("POOLRAD_ARROWS", "1");
#else
    setenv("POOLRAD_ARROWS", "1", 0);
#endif        // the app lets Up/Down move the highlight in list menus
    snprintf(g_game, sizeof g_game, "%s", game_dir); snprintf(g_save, sizeof g_save, "%s", save_dir); snprintf(g_exe, sizeof g_exe, "%s/POOLFLAT.EXE", game_dir);
    gamedir = g_game; savedir = g_save; pthread_t t; pthread_create(&t, NULL, engine_thread, NULL); pthread_detach(t);
}
void engine_post_key(unsigned short k) { pthread_mutex_lock(&kmx); if (((ib_t + 1) & 63) != ib_h) { inbox[ib_t] = k; ib_t = (ib_t + 1) & 63; } pthread_cond_signal(&kcv); pthread_mutex_unlock(&kmx); }
static void hhmmss(char *b, size_t n) { time_t now = time(NULL); strftime(b, n, "%H:%M:%S", localtime(&now)); }
int game_fixes(void);
int engine_record(const char *dir) {
    char p[1100]; snprintf(p, sizeof p, "%s/keys.txt", dir); rec_keys = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/clock.txt", dir); rec_clock = fopen(p, "wb");
    if (!rec_keys || !rec_clock) { if (rec_keys) fclose(rec_keys); if (rec_clock) fclose(rec_clock); rec_keys = rec_clock = NULL; return 0; }
    char t[16]; hhmmss(t, sizeof t);
    fprintf(rec_keys, "# poolrad recording, build %s, started %s%s%s. Replay: re/emu/replay.sh <this folder>\n", BUILD_ID, t, getenv("POOLRAD_WHEEL") ? ", code wheel asked" : "", game_fixes() ? ", fixes on" : "");
    return 1;
}
// Marks and notes go into keys.txt from the emulator thread, so they sit between the right keys. A mark (the
// player's "something is wrong") asks the replay for a frame and a memory dump; a note (start of each battle
// round, from the port) for a frame. The front end saves its own screenshot when the note count changes.
static pthread_mutex_t mmx = PTHREAD_MUTEX_INITIALIZER; static char mark_text[200];
static volatile int note_count; static char note_last[64];
void engine_mark(const char *label) { pthread_mutex_lock(&mmx); snprintf(mark_text, sizeof mark_text, "%s", label); mark_pending = 1; pthread_mutex_unlock(&mmx); }
static void mark_write(void) {
    pthread_mutex_lock(&mmx); char t[16]; hhmmss(t, sizeof t); static int n;
    if (rec_keys) { n++; fprintf(rec_keys, "# MARK %d at %s: %s\npolls %lu\nclockpos %llu\ndelaypos %llu\nshot mark%02d\ndump mark%02d\n", n, t, mark_text, rec_empty, clk_reads, delays, n, n); rec_flush(); }
    mark_pending = 0; pthread_mutex_unlock(&mmx);
}
static unsigned char note_frame[320 * 200 * 4];
int engine_copy_frame(unsigned char *bgra);
void emu_note(const char *what) {
    char t[16]; hhmmss(t, sizeof t); int n = note_count + 1;
    pthread_mutex_lock(&mmx); engine_copy_frame(note_frame); snprintf(note_last, sizeof note_last, "%s", what); pthread_mutex_unlock(&mmx);
    if (rec_keys) { fprintf(rec_keys, "# %s at %s\npolls %lu\nclockpos %llu\ndelaypos %llu\nshot note%04d-%s\n", what, t, rec_empty, clk_reads, delays, n, what); rec_flush(); }
    note_count = n;
}
int engine_note_count(void) { return note_count; }
const char *engine_note_last(void) { return note_last; }
void engine_note_frame(unsigned char *bgra) { pthread_mutex_lock(&mmx); memcpy(bgra, note_frame, sizeof note_frame); pthread_mutex_unlock(&mmx); }
int engine_has_exited(void) { return engine_exited; }
void engine_request_quit(void) { engine_quit = 1; pthread_mutex_lock(&kmx); pthread_cond_signal(&kcv); pthread_mutex_unlock(&kmx); }
// Fills a 320x200 BGRA buffer. Returns 1 when the game's graphics mode is active.
int engine_copy_frame(unsigned char *bgra) {
    static unsigned int pal[16];
    for (int c = 0; c < 16; c++) { u8 e = ega_pal[c]; u8 r = ((e >> 2) & 1) * 0xaa + ((e >> 5) & 1) * 0x55, g = ((e >> 1) & 1) * 0xaa + ((e >> 4) & 1) * 0x55, b = ((e >> 0) & 1) * 0xaa + ((e >> 3) & 1) * 0x55; pal[c] = 0xff000000u | (r << 16) | (g << 8) | b; }
    unsigned int *out = (unsigned int *)bgra;
    if (video_mode != 0x0d) { for (int i = 0; i < 320 * 200; i++) out[i] = 0xff000000u; return 0; }
    for (int y = 0; y < 200; y++) for (int xb = 0; xb < 40; xb++) {
        u32 off = y * 40 + xb; u8 p0 = plane[0][off], p1 = plane[1][off], p2 = plane[2][off], p3 = plane[3][off];
        for (int bit = 7; bit >= 0; bit--) { int c = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3); *out++ = pal[c]; }
    }
    return 1;
}
// PC speaker: a square wave at whatever frequency the game last set. Tone changes are replayed from
// the time-stamped queue 50 ms behind real time, so effects that change pitch every timer tick
// (about every 4 ms) come out exactly rather than being sampled once per audio buffer.
void engine_audio_render(float *out, int n, double rate) {
    static double phase = 0, clock = 0; static float level = 0; static unsigned hz = 0;
    double target_clock = spk_now() - 0.05;
    if (clock == 0 || clock > target_clock + 0.03 || clock < target_clock - 0.03) clock = target_clock;   // resynchronise after a stall
    for (int i = 0; i < n; i++) {
        while (spkq_r != spkq_w && spkq[spkq_r % SPKQ].t <= clock) { hz = spkq[spkq_r % SPKQ].hz; spkq_r++; }
        float target = 0;
        if (hz > 30 && hz < 12000) { phase += hz / rate; phase -= (long)phase; target = phase < 0.5 ? 0.12f : -0.12f; }
        level += (target - level) * 0.35f;         // soften the edges a little
        out[i] = level;
        clock += 1.0 / rate;
    }
}
#else
static void rule_apply(void) {   // n = FFh in either rule means every party member
    u8 *g = mem + 0x3dec0;
    u32 off = g[0x5d96] | g[0x5d97] << 8, seg = g[0x5d98] | g[0x5d99] << 8;
    for (unsigned i = 1; seg && i <= 12; i++) {
        u8 *c = mem + (seg << 4) + off;
        if (keep_n && (keep_n == 0xff || keep_n == i) && c[keep_off] > keep_max) c[keep_off] = (u8)keep_max;
        if (rule_n && (rule_n == 0xff || rule_n == i)) {
            u32 eo = c[0x7f] | c[0x80] << 8, es = c[0x81] | c[0x82] << 8; int guard = 64;
            while (es && guard--) {
                u8 *e = mem + (es << 4) + eo;
                if (e[0] == rule_kind && (unsigned)(e[1] | e[2] << 8) > rule_time) { e[1] = (u8)rule_time; e[2] = (u8)(rule_time >> 8); }
                eo = e[5] | e[6] << 8; es = e[7] | e[8] << 8;
            }
        }
        off = c[0x104] | c[0x105] << 8; seg = c[0x106] | c[0x107] << 8;
    }
}

// ---------------- snapshots (test harness) ----------------
// Everything that makes up the machine at an instruction boundary: memory, registers, flags, timer, DOS
// memory blocks, the keys waiting, the video card and which files are open where. Restoring one lets a test
// start in the middle of the game instead of replaying it from the title screen.
extern void ega_state_io(void *file, int write);
static void snap_io(FILE *f, int w) {
#define IO(x) do { if (w) fwrite(&(x), sizeof(x), 1, f); else if (fread(&(x), sizeof(x), 1, f) != 1) { fprintf(stderr, "[snap] short file\n"); exit(1); } } while (0)
    unsigned long long tick_in = next_tick > icount ? next_tick - icount : 0;
    IO(mem); IO(R); IO(SG); IO(ip); IO(flags); IO(pit_div); IO(pit_flip); IO(pit_lo); IO(tick); IO(tick_in);
    IO(heap_top); IO(prog_end); IO(blocks); IO(nblocks); IO(keyq); IO(kq_h); IO(kq_t);
    if (!w) next_tick = icount + tick_in;
    ega_state_io(f, w);
    for (int h = 0; h < MAXFH; h++) {
        long pos = (w && fh[h]) ? ftell(fh[h]) : -1;
        IO(fhname[h]); IO(fhmode[h]); IO(pos);
        if (!w && fhname[h][0] && pos >= 0) { fh[h] = open_ci(fhname[h], fhmode[h]); if (fh[h]) fseek(fh[h], pos, SEEK_SET); else fprintf(stderr, "[snap] cannot reopen %s\n", fhname[h]); }
    }
#undef IO
}
static void snapshot_save(void) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s.snap", outdir, snap_pending);
    snap_pending[0] = 0;
    FILE *f = fopen(path, "wb"); if (!f) { perror(path); return; }
    snap_io(f, 1); fclose(f);
    fprintf(stderr, "[snap] wrote %s\n", path);
}
static void snapshot_load(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    snap_io(f, 0); fclose(f);
}

int main(int argc, char **argv) {
    const char *exe = NULL, *scr = NULL, *restore = NULL; unsigned long long maxi = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d")) gamedir = argv[++i]; else if (!strcmp(argv[i], "-o")) outdir = argv[++i];
        else if (!strcmp(argv[i], "-s")) scr = argv[++i]; else if (!strcmp(argv[i], "-t")) trace = 1; else if (!strcmp(argv[i], "-S")) savedir = argv[++i];
        else if (!strcmp(argv[i], "-P")) { prof_path = argv[++i]; prof = calloc(0x100000, sizeof *prof); }
        else if (!strcmp(argv[i], "-H")) hooks_enabled = 0;
        else if (!strcmp(argv[i], "-V")) { cover_path = argv[++i]; cover = calloc(0x100000, 1); }
        else if (!strcmp(argv[i], "-R")) restore = argv[++i];
        else if (!strcmp(argv[i], "-C")) { clock_in = fopen(argv[++i], "r"); if (!clock_in) { perror(argv[i]); return 1; } }
        else if (!strcmp(argv[i], "-n")) maxi = strtoull(argv[++i], 0, 10); else if (!strcmp(argv[i], "-i")) idle_limit = atoi(argv[++i]);
        else exe = argv[i];
    }
    if (!exe) { fprintf(stderr, "usage: emu [-d gamedir] [-o outdir] [-s script] [-n maxinstr] exe\n"); return 1; }
    if (scr) script = fopen(scr, "r");
    if (script) { char l[64]; if (fgets(l, sizeof l, script) && !strncmp(l, "# poolrad recording", 19)) rec_replay = 1; rewind(script); }
    // IVT -> host trap stubs at F000:n*4
    rom_init();
    set_mode(3);
    load_exe(exe);
    if (restore) snapshot_load(restore);
    for (;;) {
        if (trace > 1) fprintf(stderr, "%04x:%04x AX=%04x BX=%04x CX=%04x DX=%04x SI=%04x DI=%04x BP=%04x SP=%04x DS=%04x ES=%04x F=%04x\n", SG[CS], ip, R[AX], R[BX], R[CX], R[DX], R[SI], R[DI], R[BP], R[SP], SG[DS], SG[ES], flags);
        step(); timer_poll();
        if (maxi && icount >= maxi) { fprintf(stderr, "[emu] instruction limit at %04x:%04x\n", SG[CS], ip); finish(0); }
    }
}
#endif
