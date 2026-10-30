// Turbo Pascal runtime services the game code relies on (heap for now).
#ifndef RTL_H
#define RTL_H
#include "poolrad.h"
farptr rtl_getmem(u16 size);              // System.GetMem  (1AF8:023F)
void rtl_freemem(farptr p, u16 size);     // System.FreeMem (1AF8:0254)
u16 rtl_random(u16 range);
void rtl_randomize(void);
void rtl_str_load(farptr dst, farptr src);
void rtl_str_store(farptr src, farptr dst, u16 max);
void rtl_str_copy(farptr dst, farptr src, s16 index, s16 count);
void rtl_str_concat(farptr dst, farptr src);
void rtl_str_compare(farptr a, farptr b);
void rtl_char_str(farptr dst, u8 c);
void rtl_move(farptr src, farptr dst, u16 n);
void rtl_fill(farptr dst, u16 n, u8 v);                         // System.Randomize (1AF8:1247)                 // System.Random (1AF8:11C0)
void rtl_delay(u16 ms);                     // Crt.Delay (1A40:029C)
void input_wait_key(void);                  // wait for a key press (input.c)
u8 crt_read_key(void);                      // Crt.ReadKey (1A40:030F)
int crt_key_pressed(void);                  // Crt.KeyPressed (1A40:02FD)
void sound_play(u16 id);                    // start a sound (18AE:00DC)
void sound_resume(void);                    // 18AE:017B
void sound_music(u16 n);                    // 18AE:01A5
u32 sound_clock(void);                      // 18AE:0000
void sound_wait(u16 n);                     // 18AE:006C
void sound_start_pc(u16 n);                 // 18AE:1742
void sound_start_tandy(u16 n);              // 18AE:17AE
void sound_select(u8 n);                    // 18AE:181A
void sound_timer_on(void);                  // 18AE:137C
void sound_timer_off(void);                 // 18AE:13B1
u32 game_clock(void);                       // time of day in 1/100 s (1709:0C3F); still original code
s32 rtl_maxavail(void);                     // System.MaxAvail (1AF8:02B8)
void game_set_file_set(u8 n);               // 1802:094A; still original code
void map_load_part(u8 which, u8 id);        // 491C:0E08; still original code
void view_redraw(void);                     // redraw the exploring view (view.c)
void ui_frame(farptr title, u8 title_color, u8 style, u8 bottom, u8 right, u8 top, u8 left);   // a framed window (hud.c)
u16 text_input_number(u8 color, farptr prompt);                    // 1709:0B66 (text.c)
// Call a routine that is still original code (Ghidra segment, offset, then its parameters in declaration order).
u32 emu_pcall(u16 gseg, u16 off, int nargs, ...);
#define call_original emu_pcall
void emu_note(const char *what);      // a point worth marking in the app's session recording (battle rounds)
void emu_flags_cmp8(u8 a, u8 b);            // set the emulated flags as CMP a,b would
void emu_flags_sub16(u16 a, u16 b);
void emu_flags_add16(u16 a, u16 b);         // ... as ADD a,b would         // ... as SUB a,b would
u16 emu_flags_get(void); void emu_flags_set(u16 f);
u16 emu_host_call(int n);                    // BIOS/DOS service n with the registers in R/SG, as INT n would; returns the flags
void emu_idle_ms(unsigned ms);
void emu_run_int(int n);                      // a game-installed interrupt handler run to completion
void emu_delay_count(void);
void emu_set_ip(u16 v);                       // (with a hook result of 0x7FFF: the hook returned or jumped itself)
void port_out(u16 port, u8 v); u8 emu_port_in(u16 port);
void sound_isr_hooks(void); void main_hooks(void); void game_main(void);   // main.c
void dos_intr(u8 n, farptr regs); void dos_get_time(farptr h, farptr m, farptr s, farptr hs);   // rtl_dos.c
void rtl_dos_hooks(void);
u16 rtl_ioresult(void); void file_assign(farptr f, farptr name); void file_close(farptr f);       // rtl_file.c
void file_reset(farptr f, u16 recsize); void file_rewrite(farptr f, u16 recsize);
void file_block_read(farptr f, farptr buf, u16 count, farptr result); void file_block_write(farptr f, farptr buf, u16 count, farptr result);
void file_seek(farptr f, s32 n); int file_eof(farptr f); void file_erase(farptr f); void file_mkdir(farptr s);
void rtl_file_hooks(void);
void text_assign(farptr t, farptr name); void text_reset(farptr t); void text_rewrite(farptr t); void text_close(farptr t);   // rtl_text.c
void text_readln(farptr t); void text_writeln(farptr t); u8 text_read_char(farptr t); void text_read_str(farptr t, farptr s, u16 max);
void text_write_str(farptr t, farptr s, s16 width); void text_write_long(farptr t, s32 v, s16 width); int text_eof(farptr t);
void rtl_text_hooks(void);
void rtl_real_hooks(void);
s32 rtl_val(farptr s, farptr code); s32 rtl_memavail(void); void rtl_run_error(u16 code);   // rtl_sys.c
void rtl_exit(u16 code, u16 err_off, u16 err_seg); void rtl_sys_hooks(void);
void sys_init(u16 psp, u16 ss, u16 sp); void crt_init(void); void crt_assign(farptr t);   // rtl_crt.c
u16 crt_drv_open(farptr t); u16 crt_drv_write(farptr t); u16 crt_drv_read(farptr t); void rtl_crt_hooks(void);
void ovr_init(farptr name); s32 ovr_get_buf(void); void ovr_set_buf(s32 size); void rtl_ovr_hooks(void);   // rtl_ovr.c
void rtl_paramstr(farptr dst, u16 n);            // 1AF8:16BC                  // Delay was called (session recordings place marks by it)               // wait ms (app: real time, timer interrupts served; headless: none)
u16 emu_sp(void);                           // the emulated stack pointer (in a hook: the return address's place)
u32 emu_pcall_strfn(u16 gseg, u16 off, int nargs, ...);   // the same for a string function (result buffer pushed first)
real48 rtl_real_long(s32 v);                     // 1AF8:119A
real48 rtl_real_mul(real48 a, real48 b);         // 1AF8:1186
real48 rtl_real_div(real48 a, real48 b);         // 1AF8:118C
s32 rtl_real_trunc(real48 a);                    // 1AF8:119E
void rtl_halt(u16 code);
void file_set_select(u8 n);                       // 1802:094A
void file_set_restore(void);                      // 1802:095D
void keys_flush(void);                            // 1899:0104 (does nothing, as in the original)
void game_quit(void);                             // 1899:0000
u16 heap_error(u16 size, u16 stale);              // 1C7C:0042
void heap_error_install(void);                    // 1C7C:00AC                                          // the runtime's Halt (1AF8:00E9)
void call_original_scratch(u16 gseg, u16 off, int bytes);      // a routine with one VAR parameter whose result is not needed
farptr scratch_push(int bytes);                                  // room on the original program's stack for a VAR parameter
void scratch_pop(int bytes);
int call_original_flag(u16 gseg, u16 off);                     // a routine with one VAR boolean parameter; returns what it stored
void call_original_pstr(u16 gseg, u16 off, const u8 *pstr);   // the same for a routine taking one string parameter
void monster_load(u8 id, farptr *rec, farptr *items, farptr *effects);   // read a monster from the MON files (2366:0411, combat.c)
void party_measures(u8 *a, u8 *b);          // two party-wide values used to settle fleeing (2366:1FB1); still original code
farptr char_find_effect(u8 kind, farptr ch);             // the character's status effect of this kind, or nil (10BA:2199)
int char_has_effect(u8 kind, farptr ch);
void item_draw_name(farptr item, farptr ch, u8 row, u8 col);   // print an item's name (10BA:0442); still original code
const char *plat_param1(void);              // first command-line argument of the game
const char *pstr(farptr p);                // Pascal string (length byte first) as a C string; static buffer
#include <stdio.h>
FILE *plat_fopen(const char *dos_path, const char *mode);   // open a game file by its DOS name, ignoring case and drive/directory
#endif
void ecl_compare_strings(const u8 *A, const u8 *B);
