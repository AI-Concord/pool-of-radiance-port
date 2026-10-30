#ifndef ECL_H
#define ECL_H
#include "poolrad.h"
u16 ecl_word(u8 hi, u8 lo);
u8 ecl_char(u8 code);
u8 ecl_addr_class(u16 addr);
void ecl_unpack_text(u8 n, u8 packed);
void ecl_fetch(u8 n);
u16 ecl_read_var(u16 addr);
void ecl_write_var(u16 value, u16 addr);
u16 ecl_operand(u8 i);
void ecl_compare(u16 b, u16 a);
void ecl_gosub(u8 i);
void ecl_skip(void);
void op_goto(void); void op_gosub(void); void op_return(void); void op_compare(void); void op_compare_and(void);
void op_clock(void); void op_spell(void); void op_check_party(void); void op_treasure(void); void op_rob(void); void op_damage(void);   // ecl_ops.c
void op_if(void); void op_arith(void); void op_and_or(void); void op_random(void); void op_save(void);
void op_gettable(void); void op_savetable(void);
void op_on_goto(void); void op_print(void); void op_print_return(void); void op_clear_box(void); void op_delay(void); void op_sprite_off(void);
u16 ecl_read_char_field(u8 *handled, u16 addr);
void ecl_write_char_field(u16 value, u16 addr);
void ecl_text_from_var(u16 addr, u8 n);
void ecl_load_block(u8 id);
void ecl_init_script(void);
void ecl_store_text(const u8 *str, u16 addr);
void op_exit(void); void op_input_number(void); void op_input_string(void);
void op_party_strength(void); void op_load_character(void); void op_call(void); void op_picture(void);
void op_newecl(void); void op_load_files(void);
void explore_enter_area(void); void explore_camp(void); void explore(void);
void op_clear_monsters(void); void op_party_surprise(void); void op_surprise(void); void op_combat(void);
void ecl_menu_prepare(u8 *keys, u8 *s);
u8 ecl_menu(u8 animate, u8 allow_return, u8 hi_color, u8 color, u8 prompt_color, const u8 *items, const u8 *prompt);
void op_horizontal_menu(void);
void op_load_monster(void);
void op_encounter_menu(void);
void op_parlay(void);
void op_vertical_menu(void); void op_setup_monster(void); void op_approach(void);
void op_find_item(void); void op_who(void); void op_add_npc(void); void op_program(void);
void op_protection(void);
void ecl_step(void);
void ecl_run(u16 addr);
// still original code, reached through the bridge:
void ecl_original_handler(u16 offset);                   // an untranslated opcode handler in 1D3F
void ecl_compare_text(void);                             // 2366:1815 on the two text operand buffers

#endif
void show_monsters(farptr flags, u8 dist, u8 pic, u8 sprite);
void show_closeup(u8 pic, u8 set);
void duel_setup(u8 clone);
void items_lose_chance(farptr ch, u8 pct);
void coins_scale(farptr ch, real48 f);
void script_damage(farptr ch, u16 dmg);
u8 dist_ahead(s8 x, s8 y, u8 dir);
void step_forward(void);
