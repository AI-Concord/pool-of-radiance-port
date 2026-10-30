// Hybrid build: registers every natively ported routine at the address of the original it replaces.
// Each adapter unpacks the Pascal-convention arguments from the emulated stack, calls the C routine
// and returns the number of argument bytes the original would have popped.
#include <stdlib.h>
#include "hybrid.h"
#include "gfx.h"
#include "dax.h"
#include "text.h"
#include "input.h"
#include "menu.h"
#include "scene.h"
#include "ecl.h"
#include "view.h"
#include "map.h"
#include "move.h"
#include "gametime.h"
#include "hud.h"
#include "camp.h"
#include "sheet.h"
#include "chr.h"
#include "rules.h"
#include "combat.h"
#include "fight.h"
#include "effects.h"
#include "rest.h"
#include "rtl.h"

static int h_color_mask(void) { u32 a = ((u32)SG[sSS] << 4) + (u16)(R[rSP] + 4); RET8(gfx_color_mask(&mem[a], (u8)ARG(2))); return 6; }
static int h_nibble(void) { RET8(gfx_nibble(FARG(0), (u8)ARG(2))); return 6; }
static int h_draw_pic(void) { gfx_draw_pic(FARG(0), (s16)ARG(2), (s16)ARG(3)); return 8; }
static int h_merge_pic(void) { gfx_merge_pic(FARG(0), FARG(2), (u8)ARG(4), (u8)ARG(5), (s16)ARG(6), (s16)ARG(7)); return 16; }
static int h_reverse_bits(void) { RET8(gfx_reverse_bits((u8)ARG(0))); return 2; }
static int h_flip_pic(void) { gfx_flip_pic(FARG(0), FARG(2)); return 8; }
static int h_flush(void) { gfx_flush_back_buffer(); return 0; }
static int h_fill_pic(void) { gfx_fill_pic(FARG(0), (u8)ARG(4), ARG(5), ARG(6), (s16)ARG(7), (s16)ARG(8)); return 18; }
static int h_alloc_pic(void) { gfx_alloc_pic(FARG(0), (u8)ARG(2), (u8)ARG(3), ARG(4), ARG(5)); return 12; }
static int h_free_pic(void) { gfx_free_pic(FARG(0)); return 4; }
static int h_dax_load(void) {
    char ext[64], name[64]; snprintf(ext, sizeof ext, "%s", pstr(FARG(5))); snprintf(name, sizeof name, "%s", pstr(FARG(7)));
    return dax_load(FARG(0), FARG(2), (u8)ARG(4), ext, name) ? 18 : -1;      // missing file: let the original ask for the disk
}
static int h_reset_ega(void) { text_reset_ega(); return 0; }
static int h_gfx_set_mode(void) { gfx_set_mode_at((u8)ARG(0), R[rSP]); return 2; }
static int h_gfx_reverse_pairs(void) { R[rAX] = gfx_reverse_pairs(ARG(0)); return 2; }
static int h_gfx_swap_nibbles(void) { R[rAX] = gfx_swap_nibbles((u8)ARG(0)); return 2; }
static int h_gfx_restore_mode(void) { gfx_restore_mode_at(R[rSP]); return 0; }
static int h_text_show_page(void) { text_show_page((u8)ARG(0)); return 2; }
static int h_text_load_font(void) { text_load_font(); return 0; }
static int h_file_set_select(void) { file_set_select((u8)ARG(0)); return 2; }
static int h_file_set_restore(void) { file_set_restore(); return 0; }
static int h_keys_flush(void) { keys_flush(); return 0; }
static int h_game_quit(void) { game_quit(); return 0; }
static int h_heap_error(void) { R[rAX] = heap_error(ARG(0), rd16(((u32)SG[sSS] << 4) + (u16)(R[rSP] - 4))); return 2; }   // [BP-2] lies 4 below the return address
static int h_heap_error_install(void) { heap_error_install(); return 0; }
static int h_text_bios_char(void) { text_bios_char((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4)); return 10; }
static int h_text_bios_string(void) { text_bios_string(FARG(0), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4)); return 10; }
static int h_text_bios_bar(void) { text_bios_bar((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_work_page(void) { text_set_work_page((u8)ARG(0)); return 2; }
static int h_fill_cells(void) { text_fill_cells((u8)ARG(0), (u8)ARG(1), (s8)ARG(2), (s8)ARG(3), (s8)ARG(4), (s8)ARG(5)); return 12; }
static int h_put_char(void) { text_put_char((u8)ARG(5), (u8)ARG(4), (u8)ARG(3), (u8)ARG(2), (u8)ARG(1), (u8)ARG(0)); return 12; }
static int h_put_blanks(void) { text_put_blanks((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_put_string(void) { text_put_string(FARG(0), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4)); return 10; }
static int h_decode_pic(void) { gfx_decode_pic(FARG(0), (u8)ARG(2), ARG(4), FARG(5)); return 14; }
static int h_load_pic(void) { char name[64]; snprintf(name, sizeof name, "%s", pstr(FARG(5))); return gfx_load_pic(FARG(0), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4), name) ? 14 : -1; }
static int h_clear_box(void) { text_clear_box((s8)ARG(3), (s8)ARG(2), (s8)ARG(1), (s8)ARG(0)); return 8; }
static int h_prompt(void) { text_prompt(FARG(0), (u8)ARG(2)); return 6; }
static int h_print_wrapped(void) { text_print_wrapped(FARG(0), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4), (u8)ARG(5), (u8)ARG(6), (u8)ARG(7)); return 16; }
static int h_merge_clipped(void) { gfx_merge_pic_clipped(FARG(0), FARG(2), (u8)ARG(4), (u8)ARG(5), (s16)ARG(6), (s16)ARG(7)); return 16; }
static int h_recolor(void) { gfx_recolor_pic(FARG(0), (u8)ARG(2), (s16)ARG(3), FARG(4), FARG(6)); return 16; }
static int h_get_key(void) { RET8(input_get_key()); return 0; }
static int h_input_poll(void) { input_poll(); return 0; }
static int h_clear_screen(void) { text_clear_screen(); return 0; }
static int h_pause(void) { text_pause(); return 0; }
static int h_flash(void) { text_flash_message((u8)ARG(0), FARG(1)); return 6; }
// A Pascal function returning a string: the caller pushes the address for the result first and that
// address stays on the stack after the return, so only the 8 bytes of real parameters are popped.
static int h_input_number(void) { R[rAX] = text_input_number((u8)ARG(0), FARG(1)); return 6; }
static int h_text_clock(void) { u32 c = sound_clock(); R[rAX] = (u16)c; R[rDX] = (u16)(c >> 16); return 0; }   // 1709:0C3F, a copy of 18AE:0000
static int h_input_string(void) { text_input_string((u8)ARG(0), (u8)ARG(1), FARG(2), FARG(4)); return 8; }
static int h_menu_horizontal(void) { RET8(menu_horizontal(&mem[LIN(FARG(0))], (u8)ARG(2), (u8)ARG(3), (u8)ARG(4), (u8)ARG(5), (u8)ARG(6), &mem[LIN(FARG(7))], &mem[LIN(FARG(9))])); return 22; }
static int h_menu_clear_line(void) { menu_clear_line(); return 0; }
static int h_yes_no(void) { RET8(menu_yes_no((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3))); return 10; }
static int h_list_alloc(void) { menu_list_alloc(FARG(0), (s16)ARG(2)); return 6; }
static int h_list_free(void) { menu_list_free(FARG(0)); return 4; }
static int h_menu_list(void) {
    farptr res; s16 idx = (s16)P16(FARG(2), 0); u8 redraw = P8(FARG(4), 0);
    u8 k = menu_list(&res, &idx, &redraw, (u8)ARG(6), FARG(7), (s8)ARG(9), (s8)ARG(10), (s8)ARG(11), (s8)ARG(12),
                     (u8)ARG(13), (u8)ARG(14), (u8)ARG(15), &mem[LIN(FARG(16))], &mem[LIN(FARG(18))]);
    SETP32(FARG(0), 0, res); SETP16(FARG(2), 0, (u16)idx); P8(FARG(4), 0) = redraw;
    RET8(k); return 40;
}
static int h_scene_draw(void) { scene_draw(FARG(0), (u8)ARG(2), (s16)ARG(3), (s16)ARG(4)); return 10; }
static int h_scene_draw_plain(void) { scene_draw(FARG(0), 0, (s16)(u8)ARG(2), (s16)(u8)ARG(3)); return 8; }
static int h_scene_draw_lower(void) { scene_draw(FARG(0), 0, (s16)(u8)(ARG(2) + 5), (s16)(u8)ARG(3)); return 8; }
static int h_scene_portrait(void) { scene_draw_portrait((u8)ARG(0), (u8)ARG(1), (u8)ARG(2)); return 6; }
static int h_scene_free(void) { scene_free_anim(FARG(0)); return 4; }
static int h_scene_load(void) { char name[16]; snprintf(name, sizeof name, "%.8s", pstr(FARG(4))); return scene_load_anim(FARG(0), (u8)ARG(2), (u8)ARG(3), name) ? 12 : -1; }
static int h_ecl_fetch(void) { ecl_fetch((u8)ARG(0)); return 2; }
static int h_ecl_operand(void) { RET16(ecl_operand((u8)ARG(0))); return 2; }
static int h_ecl_word(void) { RET16(ecl_word((u8)ARG(0), (u8)ARG(1))); return 4; }
static int h_ecl_char(void) { RET8(ecl_char((u8)ARG(0))); return 2; }
static int h_ecl_class(void) { RET8(ecl_addr_class(ARG(0))); return 2; }
static int h_ecl_read(void) { RET16(ecl_read_var(ARG(0))); return 2; }
static int h_ecl_write(void) { ecl_write_var(ARG(0), ARG(1)); return 4; }
static int h_ecl_unpack(void) { ecl_unpack_text((u8)ARG(0), (u8)ARG(1)); return 4; }
static int h_op_goto(void) { op_goto(); return 0; }
static int h_op_gosub(void) { op_gosub(); return 0; }
static int h_op_return(void) { op_return(); return 0; }
static int h_op_compare(void) { op_compare(); return 0; }
static int h_op_compare_and(void) { op_compare_and(); return 0; }
static int h_op_if(void) { op_if(); return 0; }
static int h_op_arith(void) { op_arith(); return 0; }
static int h_op_and_or(void) { op_and_or(); return 0; }
static int h_op_random(void) { op_random(); return 0; }
static int h_op_check_party(void) { op_check_party(); return 0; }
static int h_op_treasure(void) { op_treasure(); return 0; }
static int h_op_rob(void) { op_rob(); return 0; }
static int h_op_damage(void) { op_damage(); return 0; }
static int h_op_clock(void) { op_clock(); return 0; }
static int h_op_spell(void) { op_spell(); return 0; }
static int h_op_save(void) { op_save(); return 0; }
static int h_op_gettable(void) { op_gettable(); return 0; }
static int h_op_savetable(void) { op_savetable(); return 0; }
static int h_ecl_compare(void) { ecl_compare(ARG(0), ARG(1)); return 4; }
static int h_ecl_gosub(void) { ecl_gosub((u8)ARG(0)); return 2; }
static int h_ecl_skip(void) { ecl_skip(); return 0; }
static int h_ecl_read_char(void) { u8 handled; u16 v = ecl_read_char_field(&handled, ARG(2)); mem[LIN(FARG(0))] = handled; RET16(v); return 6; }
static int h_ecl_write_char(void) { ecl_write_char_field(ARG(0), ARG(1)); return 4; }
static int h_op_on_goto(void) { op_on_goto(); return 0; }
static int h_op_print(void) { op_print(); return 0; }
static int h_op_print_return(void) { op_print_return(); return 0; }
static int h_op_clear_box(void) { op_clear_box(); return 0; }
static int h_op_delay(void) { op_delay(); return 0; }
static int h_op_sprite_off(void) { op_sprite_off(); return 0; }
static int h_ecl_text_from_var(void) { ecl_text_from_var(ARG(0), (u8)ARG(1)); return 4; }
static int h_ecl_store_text(void) { ecl_store_text(&mem[LIN(FARG(0))], ARG(2)); return 6; }
static int h_op_exit(void) { op_exit(); return 0; }
static int h_op_input_number(void) { op_input_number(); return 0; }
static int h_op_input_string(void) { op_input_string(); return 0; }
static int h_ecl_step(void) { ecl_step(); return 0; }
static int h_ecl_run(void) { ecl_run(ARG(0)); return 2; }
static int h_op_party_strength(void) { op_party_strength(); return 0; }
static int h_op_load_character(void) { op_load_character(); return 0; }
static int h_op_call(void) { op_call(); return 0; }
static int h_op_picture(void) { op_picture(); return 0; }
static int h_op_newecl(void) { op_newecl(); return 0; }
static int h_op_load_files(void) { op_load_files(); return 0; }
static int h_ecl_load_block(void) { ecl_load_block((u8)ARG(0)); return 2; }
static int h_ecl_init_script(void) { ecl_init_script(); return 0; }
static int h_explore(void) { explore(); return 0; }
static int h_explore_enter(void) { explore_enter_area(); return 0; }
static int h_explore_camp(void) { explore_camp(); return 0; }
static int h_op_clear_monsters(void) { op_clear_monsters(); return 0; }
static int h_op_party_surprise(void) { op_party_surprise(); return 0; }
static int h_op_surprise(void) { op_surprise(); return 0; }
static int h_op_combat(void) { op_combat(); return 0; }
static int h_op_horizontal_menu(void) { op_horizontal_menu(); return 0; }
static int h_ecl_menu(void) { RET8(ecl_menu((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4), &mem[LIN(FARG(5))], &mem[LIN(FARG(7))])); return 18; }
static int h_ecl_menu_prepare(void) { ecl_menu_prepare(&mem[LIN(FARG(0))], &mem[LIN(FARG(2))]); return 8; }
static int h_op_load_monster(void) { op_load_monster(); return 0; }
static int h_op_encounter_menu(void) { op_encounter_menu(); return 0; }
static int h_op_parlay(void) { op_parlay(); return 0; }
static int h_op_vertical_menu(void) { op_vertical_menu(); return 0; }
static int h_op_setup_monster(void) { op_setup_monster(); return 0; }
static int h_op_approach(void) { op_approach(); return 0; }
static int h_op_find_item(void) { op_find_item(); return 0; }
static int h_op_who(void) { op_who(); return 0; }
static int h_op_add_npc(void) { op_add_npc(); return 0; }
static int h_op_program(void) { op_program(); return 0; }
static int h_op_protection(void) { op_protection(); return 0; }
static int h_view_redraw(void) { view_redraw(); return 0; }
static int h_view_marker(void) { view_draw_marker(); return 0; }
static int h_view_colors(void) { view_set_colors((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_map_in_bounds(void) { RET8(map_in_bounds((s8)ARG(0), (s8)ARG(1))); return 4; }
static int h_map_wall(void) { RET8(map_wall((u8)ARG(0), (s8)ARG(1), (s8)ARG(2))); return 6; }
static int h_map_event(void) { RET8(map_event((s8)ARG(0), (s8)ARG(1))); return 4; }
static int h_view_background(void) { view_draw_background(); return 0; }
static int h_view_piece(void) { view_draw_piece((u8)ARG(0), (u8)ARG(1), (s8)ARG(2), (s8)ARG(3)); return 8; }
static int h_view_area_map(void) { view_draw_area_map((u8)ARG(0), (s8)ARG(1), (s8)ARG(2)); return 6; }
static int h_view_render(void) { view_render((u8)ARG(0), (s8)ARG(1), (s8)ARG(2)); return 6; }
static int h_sym_put(void) { return sym_put((u8)ARG(0), (u8)ARG(1), ARG(2), (s8)ARG(3), (s8)ARG(4)) ? 10 : -1; }
static int h_sym_load_set(void) { return sym_load_set((u8)ARG(0), (u8)ARG(1)) ? 4 : -1; }
static int h_icon_load(void) { char name[64]; snprintf(name, sizeof name, "%s", pstr(FARG(2))); return icon_load((u8)ARG(1), name) ? 8 : -1; }
static int h_icon_draw(void) { icon_draw((u8)ARG(1), (s8)ARG(2), (s8)ARG(3)); return 8; }
static int h_icon_free_slot(void) { icon_free_slot((u8)ARG(0)); return 2; }
static int h_map_passage(void) { RET8(map_passage((u8)ARG(0), (s8)ARG(1), (s8)ARG(2))); return 6; }
static int h_map_load(void) { return map_load((u8)ARG(0)) ? 2 : -1; }
static int h_map_load_walls(void) { return map_load_walls((u8)ARG(0), (u8)ARG(1)) ? 4 : -1; }
static int h_explore_command(void) { RET8(explore_command()); return 0; }
static int h_move_step_forward(void) { move_step_forward(); return 0; }
static int h_move_into_square(void) { move_into_square(); return 0; }
static int h_move_overland(void) { move_overland(); return 0; }
static int h_move_party(void) { move_party(); return 0; }
static int h_door_remove(void) { door_remove((u8)ARG(0), (s8)ARG(1), (s8)ARG(2)); return 6; }
static int h_door_unlock(void) { door_unlock((u8)ARG(0), (s8)ARG(1), (s8)ARG(2)); return 6; }
static int h_party_has_class(void) { RET8(party_has_class((s8)ARG(0))); return 2; }
static int h_door_bash(void) { RET8(door_bash()); return 0; }
static int h_door_pick(void) { RET8(door_pick()); return 0; }
static int h_spell_slot(void) { RET8(spell_slot(FARG(0), (u8)ARG(2))); return 6; }
static int h_party_find_spell(void) { RET32(party_find_spell((u8)ARG(0))); return 2; }
static int h_door_knock(void) { RET8(door_knock()); return 0; }
static int h_time_carry(void) { time_carry(&mem[LIN(FARG(0))]); return 4; }
static int h_time_advance(void) { time_advance((s8)ARG(0), (s8)ARG(1)); return 4; }
static int h_hud_status(void) { hud_draw_status(); return 0; }
static int h_hud_name(void) { hud_draw_name((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3)); return 10; }
static int h_hud_ac(void) { hud_draw_ac((u8)ARG(0), (u8)ARG(1), FARG(2)); return 8; }
static int h_hud_hp(void) { hud_draw_hp((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3)); return 10; }
static int h_hud_roster(void) { hud_draw_roster(FARG(0)); return 4; }
static int h_hud_frames(void) { hud_draw_frames((u8)ARG(0)); return 2; }
static int h_hud_screen(void) { hud_draw_screen(); return 0; }
static int h_ui_border(void) { ui_border((u8)ARG(0), (s8)ARG(1), (s8)ARG(2), (s8)ARG(3), (s8)ARG(4)); return 10; }
static int h_ui_frame(void) { ui_frame(FARG(0), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4), (u8)ARG(5), (u8)ARG(6), (u8)ARG(7)); return 16; }
static int h_ui_clear_box(void) { text_clear_box((s8)ARG(3), (s8)ARG(2), (s8)ARG(1), (s8)ARG(0)); return 8; }
static int h_scene_load_portrait(void) { scene_load_portrait((u8)ARG(0), (u8)ARG(1)); return 4; }
static int h_scene_anim_frame(void) { return scene_draw_anim_frame(FARG(0), (u8)ARG(2)) ? 6 : -1; }
static int h_scene_char_portrait(void) { scene_show_character_portrait(); return 0; }
static int h_camp_rest_needed(void) { RET16(camp_rest_needed(FARG(0))); return 4; }
static int h_camp_rest(void) { camp_rest(FARG(0)); return 4; }
static int h_camp_allowed(void) { RET8(camp_magic_allowed((u8)ARG(0))); return 2; }
static int h_camp_slots_free(void) { RET16(camp_slots_free((s8)ARG(0), (s8)ARG(1))); return 4; }
static int h_camp_unpick_memory(void) { camp_unpick_memory(FARG(0)); return 4; }
static int h_camp_unpick_scrolls(void) { camp_unpick_scrolls(FARG(0)); return 4; }
static int h_camp_unpick_party(void) { camp_unpick_party(); return 0; }
static int h_camp_cast(void) { camp_cast(); return 0; }
static int h_camp_magic(void) { camp_magic(FARG(0)); return 4; }
static int h_camp_sort_slots(void) { camp_sort_slots(); return 0; }
static int h_camp_show_slots(void) { RET8(camp_show_slots()); return 0; }
static int h_camp_memorize(void) { camp_memorize(); return 0; }
static int h_camp_scribe(void) { camp_scribe(); return 0; }
static int h_camp_display(void) { camp_display(); return 0; }
static int h_find_effect(void) { farptr e = char_find_effect((u8)ARG(2), FARG(3)); SETP32(FARG(0), 0, e); RET8(e != 0); return 10; }
static int h_camp_order_up(void) { camp_order_up(); return 0; }
static int h_camp_order_down(void) { camp_order_down(); return 0; }
static int h_camp_order(void) { camp_order(); return 0; }
static int h_camp_drop_char(void) { camp_drop_character(); return 0; }
static int h_camp_speed(void) { camp_speed(); return 0; }
static int h_camp_alter(void) { camp_alter(); return 0; }
static int h_ui_clear_message(void) { ui_clear_message(); return 0; }
static int h_ui_notice(void) { ui_notice(&mem[LIN(FARG(0))]); return 4; }
static int h_ui_char_message(void) { ui_char_message((u8)ARG(0), (u8)ARG(1), &mem[LIN(FARG(2))], FARG(4)); return 12; }
static int h_chr_str_index(void) { RET8(chr_strength_index(FARG(0))); return 4; }
static int h_chr_hit_bonus(void) { RET8(chr_strength_hit_bonus(FARG(0))); return 4; }
static int h_chr_dmg_bonus(void) { RET8(chr_strength_damage_bonus(FARG(0))); return 4; }
static int h_chr_dex_defence(void) { RET8(chr_dex_defence(FARG(0))); return 4; }
static int h_chr_dex_missile(void) { RET8(chr_dex_missile(FARG(0))); return 4; }
static int h_chr_listed_effect(void) { RET8(chr_has_listed_effect(FARG(0))); return 4; }
static int h_chr_forget_spell(void) { chr_forget_spell((u8)ARG(0)); return 2; }
static int h_chr_str_byte(void) { chr_number_text(FARG(1), (u8)ARG(0)); return 2; }
static int h_chr_str_word(void) { chr_number_text(FARG(1), (u16)ARG(0)); return 2; }
static int h_chr_str_long(void) { chr_number_text(FARG(2), (s32)(ARG(0) | (u32)ARG(1) << 16)); return 4; }
static int h_chr_carry(void) { RET16(chr_strength_carry(FARG(0))); return 4; }
static int h_chr_party_side(void) { RET8(chr_on_party_side(FARG(0))); return 4; }
static int h_chr_count_sides(void) { chr_count_sides(); return 0; }
static int h_chr_clear_action(void) { RET8(chr_clear_action(FARG(0))); return 4; }
static int h_chr_guard(void) { RET8(chr_guard(FARG(0))); return 4; }
static int h_chr_caster_level(void) { RET8(chr_caster_level((u8)ARG(0))); return 2; }
static int h_chr_back_exploring(void) { chr_back_to_exploring(); return 0; }
static int h_chr_missile(void) { RET8(chr_has_missile_weapon(FARG(0))); return 4; }
static int h_chr_missile14(void) { RET8(chr_has_missile_weapon_14(FARG(0))); return 4; }
static int h_chr_healed(void) { chr_healed_message(FARG(0)); return 4; }
static int h_chr_weapon_stats(void) { chr_weapon_stats(FARG(0)); return 4; }
static int h_chr_armour_move(void) { chr_armour_movement(FARG(0), FARG(2)); return 8; }
static int h_chr_item_armour(void) { chr_item_armour(&mem[LIN(FARG(0))], (s8 *)&mem[LIN(FARG(2))], FARG(4), FARG(6)); return 16; }
static int h_chr_load_move(void) { chr_load_movement(FARG(0)); return 4; }
static int h_chr_recalculate(void) { chr_recalculate(FARG(0)); return 4; }
static int h_chr_item_name(void) { chr_item_name((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3), FARG(4)); return 16; }
static int h_chr_pick(void) { chr_pick_character(FARG(0), (u8)ARG(2), &mem[LIN(FARG(3))]); return 10; }
static int h_roll_dice(void) { RET8(roll_dice((u8)ARG(0), (u8)ARG(1))); return 4; }
static int h_roll_dice_noted(void) { RET8(roll_dice_noted((u8)ARG(0), (u8)ARG(1))); return 4; }
static int h_fx_remove_invis(void) { effects_remove_invisibility(FARG(0)); return 4; }
static int h_fx_remove16(void) { effects_remove_list16(FARG(0)); return 4; }
static int h_fx_remove2(void) { effects_remove_list2(FARG(0)); return 4; }
static int h_rules_encode(void) { RET8(rules_encode((u8)ARG(0), (u8)ARG(1))); return 4; }
static int h_effect_remove(void) { effect_remove(FARG(0), (u8)ARG(2), FARG(3)); return 10; }
static int h_effect_check(void) { effect_check((u8)ARG(0), FARG(1)); return 6; }
static int h_combat_slot(void) { RET8(combat_slot(FARG(0))); return 4; }
static int h_combat_x(void) { RET8(combat_x(FARG(0))); return 4; }
static int h_combat_y(void) { RET8(combat_y(FARG(0))); return 4; }
static int h_combat_size(void) { RET8(combat_size(FARG(0))); return 4; }
static int h_combat_sign(void) { RET16(combat_sign((s16)ARG(0))); return 2; }
static int h_line_begin(void) { line_begin(FARG(0)); return 4; }
static int h_line_step(void) { RET8(line_step(FARG(0))); return 4; }
static int h_reach_sort(void) { reach_list_sort(); return 0; }
static int h_line_of_sight(void) { RET8(line_of_sight(FARG(0), FARG(2), FARG(4), FARG(6), (s8)ARG(8), (s8)ARG(9))); return 20; }
static int h_combat_corner(void) { RET8(combat_corner((s8 *)&mem[LIN(FARG(0))], (s8 *)&mem[LIN(FARG(2))], (u8)ARG(4), (u8)ARG(5))); return 12; }
static int h_combat_in_arc(void) { u8 d = (u8)ARG(0); if (d > 8 && d != 0xff) d = 8; RET8(combat_in_arc(d, (s8)ARG(1), (s8)ARG(2), (s8)ARG(3), (s8)ARG(4))); return 10; }
static int h_combat_reach(void) { u8 d = (u8)ARG(3); if (d > 8 && d != 0xff) d = 8; combat_reach(FARG(0), (u8)ARG(2), d, ARG(4), (s8)ARG(5), (s8)ARG(6)); return 14; }
static int h_combat_view_pos(void) { combat_view_positions(); return 0; }
static int h_combat_gfx_a(void) { combat_gfx_mode_a(); return 0; }
static int h_combat_gfx_b(void) { combat_gfx_mode_b(); return 0; }
static int h_combat_square(void) { combat_square(&mem[LIN(FARG(0))], &mem[LIN(FARG(2))], (s8)ARG(4), (s8)ARG(5)); return 12; }
static int h_combat_in_view(void) { RET8(combat_in_view((s8)ARG(0), (s8)ARG(1))); return 4; }
static int h_combat_redraw_one(void) { combat_redraw_one((u8)ARG(0), (u8)ARG(1), FARG(2)); return 8; }
static int h_combat_visible(void) { RET8(combat_visible((u8)ARG(0), FARG(1))); return 6; }
static int h_combat_redraw_sq(void) { combat_redraw_square((s8)ARG(0), (s8)ARG(1)); return 4; }
static int h_combat_face(void) { combat_face((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3)); return 10; }
static int h_combat_draw_ground(void) { combat_draw_ground((u8)ARG(0), (s8)ARG(1), (s8)ARG(2)); return 6; }
static int h_combat_step_view(void) { combat_step_view((u8)ARG(0), (u8)ARG(1), (s8)ARG(2), (s8)ARG(3)); return 8; }
static int h_combat_draw_at(void) { combat_draw_at((s8)ARG(0), (s8)ARG(1)); return 4; }
static int h_combat_scroll_view(void) { RET8(combat_scroll_view((u8)ARG(0), (s8)ARG(1), (s8)ARG(2))); return 6; }
static int h_combat_place_all(void) { combat_place_all(); return 0; }
static int h_combat_look_ahead(void) { combat_look_ahead(&mem[LIN(FARG(0))], &mem[LIN(FARG(2))], (u8)ARG(4), FARG(5)); return 14; }
static int h_combat_fall(void) { combat_fall(FARG(0)); return 4; }
static int h_combat_place(void) { RET8(combat_place((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3))); return 10; }
static int h_combat_dungeon_side(void) { RET8(combat_dungeon_side((u8)ARG(1), (s8)ARG(2), (s8)ARG(3))); return 8; }
static int h_combat_dungeon_edge(void) { RET8(combat_dungeon_edge((u8)ARG(0), (s8)ARG(1), (s8)ARG(2))); return 6; }
static int h_fight_stand_down(void) { RET8(fight_stand_down(FARG(0))); return 4; }
static int h_fight_turn_undead(void) { RET8(fight_try_turn_undead(FARG(0))); return 4; }
static int h_fight_check_keys(void) { RET8(fight_check_keys(FARG(0))); return 4; }
static int h_fight_can_step(void) { RET8(fight_can_step(&mem[LIN(FARG(0))], (u8)ARG(2), (u8)ARG(3), FARG(4))); return 12; }
static int h_fight_try_item(void) { RET8(fight_try_item(FARG(0))); return 4; }
static int h_fight_try_spell(void) { RET8(fight_try_spell(FARG(0))); return 4; }
static int h_fight_weapon_score(void) { RET8(fight_weapon_score(FARG(0), FARG(2))); return 8; }
static int h_fight_half(void) { RET8(fight_half((u8)ARG(0))); return 2; }
static int h_fight_movement(void) { RET8(fight_movement(FARG(0))); return 4; }
static int h_fight_direction(void) { RET8(fight_direction(FARG(0), FARG(2))); return 8; }
static int h_fight_can_target(void) { RET8(fight_can_target(FARG(0), FARG(2))); return 8; }
static int h_fight_backstab(void) { RET8(fight_is_backstab(FARG(0), FARG(2))); return 8; }
static int h_fight_guards_strike(void) { fight_guards_strike(FARG(0)); return 4; }
static int h_fight_note_attacker(void) { fight_note_attacker(FARG(0), FARG(2)); return 8; }
static int h_fight_attack(void) { fight_attack(&mem[LIN(FARG(0))], (u8)ARG(2), FARG(3), FARG(5)); return 14; }
static int h_fight_hit_roll(void) { RET8(fight_hit_roll((u8)ARG(0), FARG(1), FARG(3))); return 10; }
static int h_fight_range_penalty(void) { fight_range_penalty(&mem[LIN(FARG(0))], FARG(2), FARG(4)); return 12; }
static int h_fight_roll_damage(void) { fight_roll_damage((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fight_deal_damage(void) { fight_deal_damage((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3), FARG(4), FARG(6)); return 16; }
static int h_chr_apply_damage(void) { chr_apply_damage((u8)ARG(0), FARG(1)); return 6; }
static int h_combat_distance(void) { RET8(combat_distance(FARG(0), FARG(2))); return 8; }
static int h_combat_enemies(void) { RET8(combat_enemies_in_reach((u8)ARG(0), FARG(1))); return 6; }
static int h_fight_round_start(void) { fight_round_start(FARG(0)); return 4; }
static int h_fight_set_attacks(void) { fight_set_attacks(FARG(0)); return 4; }
static int h_fight_strike(void) { fight_strike(&mem[LIN(FARG(0))], FARG(2), (u8)ARG(4), FARG(5), FARG(7)); return 18; }
static int h_effects_event(void) { effects_event(FARG(0), (u8)ARG(2)); return 6; }
static int h_fx_04_train(void) { fx_04_train((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_22_both(void) { fx_22_both((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_30_vs_bugbear(void) { fx_30_vs_bugbear((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_55_drain1(void) { fx_55_drain1((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_56_drain2(void) { fx_56_drain2((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_67_gaseous(void) { fx_67_gaseous((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_79_breath(void) { fx_79_breath((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_item_effect(void) { fx_item_effect((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_item_ogre_power(void) { fx_item_ogre_power((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_item_alignment(void) { fx_item_alignment((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_item_giant_str(void) { fx_item_giant_str((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_item_drop_17(void) { fx_item_drop_17((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_17_gift(void) { fx_17_gift((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_53_stone_gaze(void) { fx_53_stone_gaze((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_54_charm_gaze(void) { fx_54_charm_gaze((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_effect_run(void) { effect_run((u8)ARG(0), FARG(1), FARG(3), (u8)ARG(5)); return 12; }
static int h_fx_319b(void) { fx_nothing((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0647(void) { fx_nothing((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1355(void) { fx_nothing((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_135c(void) { fx_nothing((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_010d(void) { fx_bless((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0c06(void) { fx_prayer_friend((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0c15(void) { fx_bestow_curse((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_10c1(void) { fx_slow((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_171f(void) { fx_invisible((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_011d(void) { fx_curse((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_25a2(void) { fx_half_fire_5d((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2b63(void) { fx_half_cold_76((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2a6d(void) { fx_half_third_72((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0a3d(void) { fx_enfeebled((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_03e1(void) { fx_resist_cold((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_06e4(void) { fx_resist_fire((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0c26(void) { fx_blink((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1452(void) { fx_rooted((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_133f(void) { fx_helpless((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_05d6(void) { fx_restore_charisma((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2976(void) { fx_resist_magic_50((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2984(void) { fx_resist_magic_15((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_29cb(void) { fx_immune_hold((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_29b6(void) { fx_immune_charm_sleep((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_29f0(void) { fx_immune_6f((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_29d9(void) { fx_immune_cold((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2a11(void) { fx_immune_fire((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2992(void) { fx_elf_resistance((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2de4(void) { fx_half_elf_resistance((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2901(void) { fx_avoid_missiles((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2ae6(void) { fx_half_from_magic_weapons((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2b7d(void) { fx_needs_magic_weapon((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2b20(void) { fx_type55_damage((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1472(void) { fx_grant_62((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1363(void) { fx_grant_invisibility((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0bac(void) { fx_blinded((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_064e(void) { fx_shield((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_negate(void) { fx_negate((u8)ARG(0)); return 2; }
static int h_fx_missile_deflect(void) { fx_missile_deflect((u8)ARG(0), FARG(1)); return 6; }
static int h_fx_weapon_in_use(void) { RET32(fx_weapon_in_use(FARG(0))); return 4; }
static int h_fx_magic_resist(void) { fx_magic_resist((u8)ARG(0)); return 2; }
static int h_effect_add(void) { effect_add((u8)ARG(0), (u8)ARG(1), ARG(2), (u8)ARG(3), FARG(4)); return 12; }
static int h_fx_165d(void) { fx_poison_0((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1671(void) { fx_poison_plus4((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1685(void) { fx_poison_plus2((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_170b(void) { fx_poison_minus2((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1699(void) { fx_paralyse_2d8((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_16e6(void) { fx_paralyse_long((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_hit_unaimed(void) { RET8(rules_hit_unaimed((u8)ARG(0), FARG(1))); return 6; }
static int h_saving_throw(void) { RET8(rules_saving_throw((s8)ARG(0), (s8)ARG(1), FARG(2))); return 8; }
static int h_fx_strike_down(void) { fx_strike_down(&mem[LIN(FARG(0))], (u8)ARG(2), FARG(3)); return 10; }
static int h_fx_poison(void) { fx_poison((s8)ARG(0), FARG(1)); return 6; }
static int h_fx_paralyse(void) { fx_paralyse(ARG(1), FARG(2)); return 8; }
static int h_fx_0377(void) { fx_protection_from_evil((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_03ac(void) { fx_protection_from_good((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0918(void) { fx_invisibility((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2700(void) { fx_regenerate((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2e08(void) { fx_undead_immunities((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1730(void) { fx_turn_invisible((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_16bb(void) { fx_paralyse_not_elves((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_013b(void) { fx_bonus_vs_type4((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2d88(void) { fx_silver_or_magic((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1a19(void) { fx_fire_touch((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1a4c(void) { fx_acid_touch((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_rules_damage(void) { rules_damage((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3)); return 10; }
static int h_fx_1086(void) { fx_protection_normal_missiles((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_264b(void) { fx_only_silver_or_magic((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2a28(void) { fx_fire_minus_one_per_die((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2610(void) { fx_falls_dead((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_27d9(void) { fx_troll_falls((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2866(void) { fx_troll_rises((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_19d9(void) { fx_comes_round((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_12fb(void) { fx_slow_healing((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_14eb(void) { fx_fast_healing((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_add_once(void) { RET8(fx_add_once(ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3))); return 10; }
static int h_rules_get_up(void) { RET8(rules_get_up((u8)ARG(0), FARG(1))); return 6; }
static int h_rules_heal(void) { RET8(rules_heal((u8)ARG(0), (u8)ARG(1), FARG(2))); return 8; }
static int h_fx_12b2(void) { fx_prayer((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2490(void) { fx_displaced((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_25bc(void) { fx_half_from_weapon_class((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2bc8(void) { fx_avoid_types_57_58((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2556(void) { fx_unaffected_by_missile_and_lightning((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2d38(void) { fx_type56_and_fire_bonus((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2814(void) { fx_start_regenerating((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_070f(void) { fx_silenced((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1490(void) { fx_fire_resistance((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_05eb(void) { fx_slow_loss((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0c4b(void) { fx_haste((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2a87(void) { fx_half_from_weapon_class_b((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_01c3(void) { fx_burning_weapon((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0775(void) { fx_slow_poison_ends((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0679(void) { fx_enmity_kobolds_goblins((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0947(void) { fx_enmity_orcs_goblins((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_11f9(void) { fx_small_and_hard_to_hit((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_24d9(void) { return fx_hardy(FARG(3)) ? 10 : -1; }
static int h_fx_18b5(void) { fx_grab((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_09c0(void) { fx_mirror_image((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_10de(void) { fx_wasting((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1168(void) { fx_disease((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0b1b(void) { fx_animation_ends((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_268a(void) { return fx_hardy_magic(FARG(3)) ? 10 : -1; }
static int h_fx_1935(void) { fx_hold_fast((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_23ec(void) { fx_mummy_touch((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1399(void) { fx_mummy_rot((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_2730(void) { fx_last_effort((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0a65(void) { fx_stinking_cloud((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0400(void) { fx_charm((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_0233(void) { fx_manual_of_bodily_health((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_04ba(void) { fx_strength_ends((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1aaa(void) { fx_fear_aura((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_1ba8(void) { fx_terror((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_rules_spell_effect(void) { rules_spell_effect(&mem[LIN(FARG(0))], (u8)ARG(2), (u8)ARG(3), (u8)ARG(4), (u8)ARG(5), ARG(6), (u8)ARG(7), FARG(8)); return 20; }
static int h_rules_strength_decode(void) { rules_strength_decode(&mem[LIN(FARG(0))], &mem[LIN(FARG(2))], FARG(4)); return 12; }
static int h_fx_2e37(void) { fx_warded_off((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx_176f(void) { fx_draining_grip((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx2_39d2(void) { fx_rake((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_fx2_3a86(void) { fx_hug((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_spell_range(void) { RET8(spell_range((u8)ARG(0))); return 2; }
static int h_chr_take_out(void) { chr_take_out(FARG(3), (u8)ARG(2), &mem[LIN(FARG(0))]); return 10; }
static int h_fight_fastest_pursuer(void) { RET8(fight_fastest_pursuer(FARG(0))); return 4; }
static int h_fight_step(void) { fight_step((u8)ARG(0), FARG(1)); return 6; }
static int h_fight_escape(void) { RET8(fight_escape(FARG(0))); return 4; }
static int h_fight_cloud_check(void) { fight_cloud_check(FARG(0)); return 4; }
static int h_fight_pick_target(void) { RET8(fight_pick_target((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3))); return 10; }
static int h_fight_free_attacks(void) { fight_free_attacks((u8)ARG(0), FARG(1)); return 6; }
static int h_fight_ai_move(void) { fight_ai_move(FARG(0)); return 4; }
static int h_fight_sweep(void) { RET8(fight_sweep(FARG(0), FARG(2))); return 8; }
static int h_chr_missile_ammo(void) { farptr a; u8 r = chr_missile_ammo(&a, FARG(2)); SETP32(FARG(0), 0, a); R[rAX] = (u16)((((FP_OFF(a) | FP_SEG(a)) >> 8) << 8) | r); return 8; }
static int h_fight_ai_arm(void) { fight_ai_arm(FARG(0)); return 4; }
static int h_fight_ai_turn(void) { RET8(fight_ai_turn(FARG(0))); return 4; }
static int h_fight_ai_decide(void) { fight_ai_decide(FARG(0)); return 4; }
static int h_hud_combat_panel(void) { hud_combat_panel(FARG(0)); return 4; }
static int h_item_ready_toggle(void) { item_ready_toggle(FARG(0)); return 4; }
static int h_fight_cast_command(void) { fight_cast_command(FARG(0), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_combat_missile_frame(void) { combat_missile_frame((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_combat_missile_spin(void) { combat_missile_spin((u8)ARG(0)); return 2; }
static int h_fight_missile_flight(void) { fight_missile_flight(FARG(0), FARG(2), FARG(4)); return 12; }
static int h_fight_heal_target(void) { farptr t = fight_heal_target(FARG(2)); SETP32(FARG(0), 0, t); RET8(t != 0); return 8; }
static int h_spell_cast(void) { spell_cast(FARG(0), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4)); return 10; }
static int h_spell_announce(void) { spell_announce((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_spell_duration(void) { R[rAX] = spell_duration((u8)ARG(0)); return 2; }
static int h_spell_affect_targets(void) { spell_affect_targets(&mem[LIN(FARG(0))], (u8)ARG(2), (u8)ARG(3), (u8)ARG(4), (u8)ARG(5), (u8)ARG(6)); return 14; }
static int h_spl_bless(void) { spl_bless(); return 0; }
static int h_spl_curse(void) { spl_curse(); return 0; }
static int h_spl_cause_light(void) { spl_cause_light(); return 0; }
static int h_spl_detect(void) { spl_detect(); return 0; }
static int h_spl_protection(void) { spl_protection(); return 0; }
static int h_spl_resist_cold(void) { spl_resist_cold(); return 0; }
static int h_spl_burning_hands(void) { spl_burning_hands(); return 0; }
static int h_spl_magic_missile(void) { spl_magic_missile(); return 0; }
static int h_spl_shield(void) { spl_shield(); return 0; }
static int h_spl_shocking_grasp(void) { spl_shocking_grasp(); return 0; }
static int h_spl_resist_fire(void) { spl_resist_fire(); return 0; }
static int h_spl_silence(void) { spl_silence(); return 0; }
static int h_spl_invisibility(void) { spl_invisibility(); return 0; }
static int h_spl_knock(void) { spl_knock(); return 0; }
static int h_spl_enfeeble(void) { spl_enfeeble(); return 0; }
static int h_spl_cause_blindness(void) { spl_cause_blindness(); return 0; }
static int h_spl_bestow_curse(void) { spl_bestow_curse(); return 0; }
static int h_spl_blink(void) { spl_blink(); return 0; }
static int h_spl_cause_disease(void) { spl_cause_disease(); return 0; }
static int h_spl_spiritual_hammer(void) { spl_spiritual_hammer(); return 0; }
static int h_spl_mirror_image(void) { spl_mirror_image(); return 0; }
static int h_spl_prayer(void) { spl_prayer(); return 0; }
static int h_spell_affect_side(void) { spell_affect_side(&mem[LIN(FARG(0))], (u8)ARG(2)); return 6; }
static int h_spl_cure_light(void) { spl_cure_light(); return 0; }
static int h_spl_reduce(void) { spl_reduce(); return 0; }
static int h_spl_friends(void) { spl_friends(); return 0; }
static int h_spl_cure_blindness(void) { spl_cure_blindness(); return 0; }
static int h_effect_cure(void) { RET8(effect_cure((u8)ARG(0), FARG(1))); return 6; }
static int h_spl_sleep(void) { spl_sleep(); return 0; }
static int h_spl_charm(void) { spl_charm(); return 0; }
static int h_spl_m_speed(void) { spl_m_speed(); return 0; }
static int h_spl_m_heal(void) { spl_m_heal(); return 0; }
static int h_spl_m_strength(void) { spl_m_strength(); return 0; }
static int h_spl_m_bolt(void) { spl_m_bolt(); return 0; }
static int h_spl_m_paralyze(void) { spl_m_paralyze(); return 0; }
static int h_spl_m_cure(void) { spl_m_cure(); return 0; }
static int h_spl_m_invisible(void) { spl_m_invisible(); return 0; }
static int h_spl_m_damage(void) { spl_m_damage(); return 0; }
static int h_spl_m_none(void) { spl_m_none(); return 0; }
static int h_spl_m_reading(void) { spl_m_reading(); return 0; }

static int h_spl_enlarge(void) { spl_enlarge(); return 0; }
static int h_raise_strength(void) { u8 v = P8(FARG(0), 0); u8 r = rules_raise_strength(&v, (u8)ARG(2), (u8)ARG(3), FARG(4)); P8(FARG(0), 0) = v; RET8(r); return 12; }
static int h_spl_hold_person(void) { spl_hold_person(); return 0; }
static int h_spl_fireball(void) { spl_fireball(); return 0; }
static int h_spl_lightning_bolt(void) { spl_lightning_bolt(); return 0; }
static int h_spl_bolt_square(void) { u8 w; spl_bolt_square(&w, (u8)ARG(2), (u8)ARG(3), (s8)ARG(4), (s8)ARG(5)); P8(FARG(0), 0) = w; return 12; }
static int h_spl_bolt_path(void) { spl_bolt_path((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_spl_haste(void) { spl_haste(); return 0; }
static int h_spl_slow(void) { spl_slow(); return 0; }
static int h_spl_slow_poison(void) { spl_slow_poison(); return 0; }
static int h_spl_snake_charm(void) { spl_snake_charm(); return 0; }
static int h_spl_speed(void) { spl_speed(&mem[LIN(FARG(0))], (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_spl_strength(void) { spl_strength(); return 0; }
static int h_spl_remove_curse(void) { spl_remove_curse(); return 0; }
static int h_spl_dispel(void) { spl_dispel(); return 0; }
static int h_spl_cure_disease_on(void) { RET8(spl_cure_disease_on()); return 0; }
static int h_spl_cure_disease(void) { spl_cure_disease(); return 0; }
static int h_spl_restoration(void) { spl_restoration(); return 0; }
static int h_spl_animate_dead(void) { spl_animate_dead(); return 0; }
static int h_spell_pick_targets(void) { spell_pick_targets(FARG(0), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_spell_sparkle(void) { spell_sparkle(&mem[LIN(FARG(0))], (u8)ARG(2), FARG(3)); return 10; }
static int h_combat_missile_anim(void) { combat_missile_anim((u8)ARG(0), (u8)ARG(1), (s8)ARG(2), (s8)ARG(3), (s8)ARG(4), (s8)ARG(5)); return 12; }
static int h_spell_pick_combat_targets(void) { spell_pick_combat_targets(FARG(0), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_spell_pick_one(void) { RET8(spell_pick_one(FARG(0), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4))); return 10; }
static int h_chr_level_capped(void) { u8 c = (u8)ARG(0); R[rAX] = (u16)(((c >> 7) << 8) | chr_level_capped(c, FARG(1))); return 6; }
static int h_fight_confirm_attack(void) { RET8(fight_confirm_attack(FARG(0), FARG(2))); return 8; }
static int h_fight_target_cursor(void) { fight_target_cursor(FARG(0), FARG(2), (u8)ARG(4), (u8)ARG(5), (u8)ARG(6), FARG(7)); return 18; }
static int h_fight_target_menu(void) { RET8(fight_target_menu((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3), FARG(5))); return 14; }
static int h_fight_target_command(void) { fight_target_command(FARG(0), FARG(2), (u8)ARG(4), FARG(5), FARG(7)); return 18; }
static int h_fight_find_undead(void) { farptr u; u8 r = fight_find_undead(&u, FARG(2)); SETP32(FARG(0), 0, u); RET8(r); return 8; }
static int h_fight_turn_undead_now(void) { fight_turn_undead(FARG(0)); return 4; }
static int h_fx_breathe_lightning(void) { fx_breathe_lightning((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_spell_list_add(void) { spell_list_add((u8)ARG(0)); return 2; }
static int h_item_is_scroll(void) { RET8(item_is_scroll(FARG(0))); return 4; }
static int h_spell_list_scrolls(void) { spell_list_scrolls((u8)ARG(0)); return 2; }
static int h_hud_explore_frames(void) { hud_explore_frames(); return 0; }
static int h_spell_list_item(void) { spell_list_item((u8)ARG(0)); return 2; }
static int h_spl_stinking_cloud(void) { spl_stinking_cloud(); return 0; }
static int h_fight_manual_aim(void) { fight_manual_aim(FARG(0), FARG(2), (u8)ARG(4), (u8)ARG(5), (u8)ARG(6), FARG(7), FARG(9)); return 22; }
static int h_fx_cloud_clears(void) { fx_cloud_clears((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_bf_decorate(void) { bf_decorate(); return 0; }
static int h_bf_build_indoor(void) { bf_build_indoor(); return 0; }
static int h_bf_outdoor_terrain(void) { RET8(bf_outdoor_terrain()); return 0; }
static int h_bf_outdoor_scatter(void) { return bf_outdoor_scatter() ? 0 : -1; }
static int h_bf_outdoor_road(void) { return bf_outdoor_road() ? 0 : -1; }
static int h_bf_outdoor_pairs(void) { return bf_outdoor_pairs() ? 0 : -1; }
static int h_bf_build_outdoor(void) { bf_build_outdoor(); return 0; }
static int h_bf_setup(void) { bf_setup(); return 0; }
static int h_bf_prepare_combatants(void) { bf_prepare_combatants(); return 0; }
static int h_bf_try_place(void) { RET8(bf_try_place((u8)ARG(0), (s8)ARG(1), (s8)ARG(2), (s8)ARG(3), (s8)ARG(4), (u8)ARG(5))); return 12; }
static int h_bf_battle_begins(void) { bf_battle_begins(); return 0; }
static int h_fight_player_turn(void) { fight_player_turn(FARG(0)); return 4; }
static int h_fight_combat_menu(void) { P8(FARG(0), 0) = fight_combat_menu(FARG(2)); return 8; }
static int h_fight_bump_attack(void) { fight_bump_attack(FARG(0), FARG(2), FARG(4)); return 12; }
static int h_fight_move_command(void) { fight_move_command(FARG(0), FARG(2)); return 8; }
static int h_fight_bandage(void) { RET8(fight_bandage((u8)ARG(0))); return 2; }
static int h_fight_set_quick(void) { fight_set_quick(FARG(0)); return 4; }
static int h_fight_done_menu(void) { fight_done_menu(FARG(0), FARG(2)); return 8; }
static int h_combat_after_fight(void) { combat_after_fight(); return 0; }
static int h_fight_next_actor(void) { SETP32(FARG(0), 0, fight_next_actor()); return 4; }
static int h_fight_take_turn(void) { fight_take_turn(FARG(0)); return 4; }
static int h_fight_end_round(void) { fight_end_round(FARG(0)); return 4; }
static int h_fight_cleanup(void) { fight_cleanup(); return 0; }
static int h_fight_battle(void) { fight_battle(); return 0; }
static int h_fight_speed_menu(void) { fight_speed_menu(); return 0; }
static int h_combat_award_xp(void) { combat_award_xp((s32)((u32)ARG(0) | ((u32)ARG(1) << 16))); return 4; }
static int h_combat_back_to_party(void) { combat_back_to_party(); return 0; }
static int h_combat_result_screen(void) { combat_result_screen((s32)((u32)ARG(0) | ((u32)ARG(1) << 16))); return 4; }
static int h_combat_allies_share(void) { combat_allies_share(); return 0; }
static int h_chr_free(void) { chr_free(FARG(0)); return 4; }
static int h_chr_thief_skills(void) { chr_thief_skills(FARG(0)); return 4; }
static int h_bf_cell_outside(void) { RET8(bf_cell_outside((s8)ARG(0), (s8)ARG(1))); return 4; }
static int h_bf_place_all(void) { bf_place_all(); return 0; }
static int h_combat_treasure_xp(void) { combat_treasure_xp(FARG(0)); return 4; }
static int h_combat_treasure_screen(void) { combat_treasure_screen(); return 0; }
static int h_treasure_left(void) { treasure_left(&mem[LIN(FARG(0))], &mem[LIN(FARG(2))]); return 8; }
static int h_combat_take(void) { combat_take(FARG(0), FARG(2)); return 8; }
static int h_combat_take_items(void) { combat_take_items(); return 0; }
static int h_chr_saving_throws(void) { chr_saving_throws(FARG(0)); return 4; }
static int h_chr_join_party(void) { chr_join_party(FARG(0)); return 4; }
static int h_pic_overlay(void) { pic_overlay(FARG(0), FARG(2)); return 8; }
static int h_chr_build_icon(void) { chr_build_icon((u8)ARG(0)); return 2; }
static int h_chr_max_load(void) { R[rAX] = chr_max_load(FARG(0)); return 4; }
static int h_chr_load_sub(void) { chr_load_sub(ARG(0), FARG(1)); return 6; }
static int h_chr_load_add(void) { chr_load_add(ARG(0), FARG(1)); return 6; }
static int h_chr_too_heavy(void) { u16 r; u8 v = chr_too_heavy(&r, ARG(2), FARG(3)); wr16(LIN(FARG(0)), r); RET8(v); return 10; }
static int h_chr_drop_coins(void) { chr_drop_coins((s8)ARG(0), ARG(1), FARG(2)); return 8; }
static int h_roll_one_two_three(void) { RET8(roll_one_two_three()); return 0; }
static int h_chr_create(void) { chr_create(); return 0; }
static int h_wait_seconds_or_key(void) { wait_seconds_or_key(ARG(0)); return 2; }
static int h_title_screen(void) { title_screen(); return 0; }
static int h_chr_load_file(void) { chr_load_file((u8)ARG(0), (u8)ARG(1), FARG(2), FARG(4)); return 12; }
static int h_chr_save_file(void) { chr_save_file(FARG(2), FARG(0)); return 8; }
static int h_chr_roll_hp(void) { RET8(chr_roll_hp(FARG(1), (u8)ARG(0))); return 6; }
static int h_chr_con_bonus(void) { RET8((u8)chr_con_bonus(FARG(0))); return 4; }
static int h_chr_wisdom_spells(void) { chr_wisdom_spells(FARG(0)); return 4; }
static int h_chr_pick_portrait(void) { chr_pick_portrait(); return 0; }
static int h_str_remove_all(void) { str_remove_all(FARG(4), FARG(2), FARG(0)); return 8; }     // the result pointer stays on the stack
static int h_str_dos_name(void) { str_dos_name(FARG(2), FARG(0)); return 4; }
static int h_file_exists(void) { RET8(file_exists(FARG(0))); return 4; }
static int h_file_disk_prompt(void) { file_disk_prompt(FARG(0), (u8)ARG(2)); return 6; }
static int h_file_open(void) { RET8(file_open(FARG(0), (u8)ARG(2), FARG(3), FARG(5))); return 14; }
static int h_game_save(void) { game_save(); return 0; }
static int h_save_dir_make(void) { save_dir_make(); return 0; }
static int h_bf_place_one(void) { RET8(bf_place_one((u8)ARG(0), mem[LIN(scratch_push(0)) - 4])); return 2; }
static int h_sprite_load(void) { sprite_load(FARG(2), (u8)ARG(1), (u8)ARG(0)); return 8; }
static int h_coin_kind(void) { RET8((u8)coin_kind(FARG(0), FARG(2))); return 8; }
static int h_input_amount(void) { R[rAX] = input_amount(ARG(3), FARG(1), (u8)ARG(0)); return 8; }
static int h_take_coins(void) { take_coins((s8)ARG(0), ARG(1), FARG(2)); return 8; }
static int h_take_money(void) { take_money(); return 0; }
static int h_share_count(void) { RET8(share_count()); return 0; }
static int h_treasure_share(void) { treasure_share(); return 0; }
static int h_roster_read(void) { roster_read(FARG(0)); return 4; }
static int h_roster_write(void) { roster_write(FARG(0)); return 4; }
static int h_party_add_menu(void) { party_add_menu(); return 0; }
static int h_chr_recalc_levels(void) { chr_recalc_levels(FARG(0)); return 4; }
static int h_party_remove(void) { party_remove((u8)ARG(1), (u8)ARG(0)); return 4; }
static int h_view_overhead(void) { view_overhead((s8)ARG(0), (s8)ARG(1)); return 4; }
static int h_sheet_items(void) { sheet_items(FARG(0)); return 4; }
static int h_party_menu(void) { party_menu(); return 0; }
static int h_item_may_part(void) { RET8(item_may_part(FARG(0))); return 4; }
static int h_item_halve(void) { item_halve(FARG(0)); return 4; }
static int h_item_join(void) { item_join(FARG(0)); return 4; }
static int h_combat_end(void) { combat_end(); return 0; }
static int h_show_monsters(void) { show_monsters(FARG(0), (u8)ARG(2), (u8)ARG(3), (u8)ARG(4)); return 10; }
static int h_show_closeup(void) { show_closeup((u8)ARG(0), (u8)ARG(1)); return 4; }
static int h_effects_tick(void) { effects_tick((u8)ARG(1), (s8)ARG(0)); return 4; }
static int h_icon_copy(void) { icon_copy((u8)ARG(1), (u8)ARG(2), (u8)ARG(3)); return 8; }
static int h_icon_show(void) { icon_show((u8)ARG(1), (u8)ARG(2)); return 6; }
static int h_icon_editor(void) { icon_editor(); return 0; }
static int h_game_load(void) { game_load(); return 0; }
static int h_party_drop(void) { party_drop(); return 0; }
static int h_hp_least(void) { RET8(hp_least(FARG(0))); return 4; }
static int h_hp_most(void) { RET8(hp_most(FARG(0))); return 4; }
static int h_chr_modify(void) { chr_modify(); return 0; }
static int h_chr_train(void) { chr_train(); return 0; }
static int h_spell_table_init(void) { spell_table_init(); return 0; }
static int h_effect_table_init(void) { effect_table_init(); return 0; }
static int h_game_start(void) { game_start(); return 0; }
static int h_money_of(void) { u32 v = (u32)money_of(FARG(0)); R[rAX] = (u16)v; R[rDX] = (u16)(v >> 16); return 4; }
static int h_money_of_pool(void) { u32 v = (u32)money_of_pool(FARG(0)); R[rAX] = (u16)v; R[rDX] = (u16)(v >> 16); return 4; }
static int h_pay_from(void) { pay_from(ARG(0)); return 2; }
static int h_pay_from_pool(void) { pay_from_pool(ARG(0)); return 2; }
static int h_item_lose(void) { item_lose(FARG(0), FARG(2)); return 8; }
static int h_item_give(void) { item_give(FARG(0), FARG(2)); return 8; }
static int h_item_too_much(void) { RET8(item_too_much(FARG(0), FARG(2))); return 8; }
static int h_item_trade(void) { item_trade(FARG(0)); return 4; }
static int h_item_sell(void) { item_sell(FARG(0)); return 4; }
static int h_item_identify(void) { item_identify(FARG(0), FARG(2)); return 8; }
static int h_coins_transfer(void) { coins_transfer((s8)ARG(0), ARG(1), FARG(2), FARG(4)); return 12; }
static int h_sheet_trade_money(void) { sheet_trade_money(mem[LIN(scratch_push(0)) - 0x133]); return 0; }
static int h_treasure_pool(void) { treasure_pool(); return 0; }
static int h_add_platinum(void) { add_platinum(ARG(0)); return 2; }
static int h_appraise(void) { appraise(FARG(0)); return 4; }
static int h_temple_anyway(void) { RET8(temple_anyway(FARG(0))); return 4; }
static int h_temple_pay(void) { RET8(temple_pay(ARG(0), FARG(1))); return 6; }
static int h_temple_cure_blindness(void) { temple_cure_blindness(); return 0; }
static int h_temple_cure_disease(void) { temple_cure_disease(); return 0; }
static int h_temple_cure_wounds(void) { temple_cure_wounds((u8)ARG(0)); return 2; }
static int h_temple_raise_dead(void) { temple_raise_dead(); return 0; }
static int h_temple_neutralize_poison(void) { temple_neutralize_poison(); return 0; }
static int h_temple_remove_curse(void) { temple_remove_curse(); return 0; }
static int h_temple_stone_to_flesh(void) { temple_stone_to_flesh(); return 0; }
static int h_temple_heal(void) { temple_heal(0); return 0; }
static int h_temple_screen(void) { temple_screen(); return 0; }
static int h_shop_list(void) { shop_list(FARG(0), FARG(2), FARG(4), FARG(6)); return 16; }
static int h_shop_give(void) { shop_give(FARG(0), FARG(2)); return 8; }
static int h_shop_buy(void) { shop_buy(); return 0; }
static int h_shop_screen(void) { shop_screen(); return 0; }
static int h_monster_load(void) {
    farptr r, it, fx; monster_load((u8)ARG(6), &r, &it, &fx);
    SETP32(FARG(4), 0, r); SETP32(FARG(2), 0, it); SETP32(FARG(0), 0, fx); return 14;
}
static int h_coins_scale(void) { coins_scale(FARG(0), (real48){ARG(2), ARG(3), ARG(4)}); return 10; }
static int h_npc_add(void) { npc_add((u8)ARG(0)); return 2; }
static int h_monster_load_rec(void) { monster_load_rec(FARG(0), (u8)ARG(2)); return 6; }
static int h_intro(void) { intro(); return 0; }
static int h_party_measures(void) { u8 a, b; party_measures(&a, &b); P8(FARG(0), 0) = a; P8(FARG(2), 0) = b; return 8; }
static int h_ecl_compare_strings(void) { ecl_compare_strings(&mem[LIN(FARG(2))], &mem[LIN(FARG(0))]); return 8; }
static int h_need_file(void) { need_file(FARG(0)); return 4; }
static int h_figure_draw(void) { figure_draw((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), (s8)ARG(3), (s8)ARG(4)); return 10; }
static int h_sound_play(void) { sound_play(ARG(0)); return 2; }
static int h_sound_clock(void) { u32 c = sound_clock(); R[rAX] = (u16)c; R[rDX] = (u16)(c >> 16); return 0; }
static int h_sound_wait(void) { sound_wait(ARG(0)); return 2; }
static int h_sound_start_pc(void) { sound_start_pc(ARG(0)); return 2; }
static int h_sound_start_tandy(void) { sound_start_tandy(ARG(0)); return 2; }
static int h_sound_select(void) { sound_select((u8)ARG(0)); return 2; }
static int h_sound_resume(void) { sound_resume(); return 0; }
static int h_sound_timer_on(void) { sound_timer_on(); return 0; }
static int h_sound_timer_off(void) { sound_timer_off(); return 0; }
static int h_unit_init(void) { return 0; }
static int h_getmem(void) { farptr r = rtl_getmem(ARG(0)); R[rAX] = FP_OFF(r); R[rDX] = FP_SEG(r); return 2; }
static int h_random(void) { R[rAX] = rtl_random(ARG(0)); return 2; }
static int h_randomize(void) { rtl_randomize(); return 0; }
static int h_str_load(void) { rtl_str_load(FARG(2), FARG(0)); return 4; }           // leaves the destination on the stack
static int h_str_store(void) { rtl_str_store(FARG(3), FARG(1), ARG(0)); return 10; }
static int h_str_copy(void) { rtl_str_copy(FARG(4), FARG(2), (s16)ARG(1), (s16)ARG(0)); return 8; }
static int h_str_concat(void) { rtl_str_concat(FARG(2), FARG(0)); return 4; }
static int h_str_compare(void) { rtl_str_compare(FARG(2), FARG(0)); return 8; }
static int h_char_str(void) { rtl_char_str(FARG(1), (u8)ARG(0)); return 2; }
static int h_move(void) { rtl_move(FARG(3), FARG(1), ARG(0)); return 10; }
static int h_fill(void) { rtl_fill(FARG(2), ARG(1), (u8)ARG(0)); return 8; }
static int h_freemem(void) { rtl_freemem(FARG(1), ARG(0)); return 6; }   // the 40 empty unit initialisations
static int h_sound_music(void) { sound_music(ARG(0)); return 2; }
static int h_item_make(void) { item_make(FARG(0), (u8)ARG(2)); return 6; }
static int h_anim_play(void) { anim_play((u8)ARG(0), (u8)ARG(1), ARG(2), ARG(3)); return 8; }
static int h_finale(void) { finale(); return 0; }
static int h_map_reveal(void) { map_reveal((u8)ARG(0)); return 2; }
static int h_debug_numbers(void) { debug_numbers((u8)ARG(0), (u8)ARG(1), (u8)ARG(2), FARG(3)); return 10; }
static int h_energy_drain(void) { energy_drain((u8)ARG(0), FARG(1)); return 6; }
static int h_bf_floor_west(void) { return bf_hook_floor_west(ARG(0)) ? 2 : -1; }
static int h_bf_north(void) { return bf_hook_north(ARG(0)) ? 2 : -1; }
static int h_bf_corner_nw(void) { return bf_hook_corner_nw(ARG(0)) ? 2 : -1; }
static int h_bf_corner_ne(void) { return bf_hook_corner_ne(ARG(0)) ? 2 : -1; }
static int h_duel_setup(void) { duel_setup((u8)ARG(0)); return 2; }
static int h_items_lose_chance(void) { items_lose_chance(FARG(0), (u8)ARG(2)); return 6; }
static int h_script_damage(void) { script_damage(FARG(0), ARG(2)); return 6; }
static int h_item_use(void) { item_use(FARG(0), FARG(2)); return 8; }
static int h_dist_ahead(void) { RET8(dist_ahead((s8)ARG(2), (s8)ARG(1), (u8)ARG(0))); return 6; }
static int h_step_forward(void) { step_forward(); return 0; }
static int h_scroll_spell_used(void) { scroll_spell_used((u8)ARG(0), FARG(1), FARG(3)); return 10; }
static int h_combat_prep_view(void) { combat_prep_view(); return 0; }
static int h_enemy_strength(void) { enemy_strength(); return 0; }
static int h_code_wheel(void) { code_wheel(); return 0; }
static int h_config_read(void) { config_read(); return 0; }
static int h_overlay_disk_wait(void) { overlay_disk_wait(); return 0; }
static int h_pay_coins(void) { pay_coins(FARG(0), (s32)((u32)ARG(2) | (u32)ARG(3) << 16)); return 8; }
static int h_loot_list(void) { loot_list(FARG(0), FARG(2), FARG(4), FARG(6)); return 16; }
static int h_fight_morale(void) { RET8(fight_morale(FARG(0))); return 4; }
static int h_fight_spell_worth(void) { RET8(fight_spell_worth_casting((u8)ARG(0), (u8)ARG(1), FARG(2))); return 8; }
static int h_camp_menu(void) { camp_menu(FARG(0)); return 4; }
static int h_sheet_ability(void) { sheet_draw_ability((u8)ARG(0), (s8)ARG(1)); return 4; }
static int h_sheet_money(void) { sheet_draw_money(); return 0; }
static int h_sheet_combat(void) { sheet_draw_combat_stats(); return 0; }
static int h_sheet_draw(void) { sheet_draw(); return 0; }
static int h_select_move(void) { party_select_move((u8)ARG(0)); return 2; }
static int h_sheet_drop_money(void) { sheet_drop_money(); return 0; }
static int h_sheet_spell_list(void) { RET8(sheet_spell_list((u8)ARG(5), (u8)ARG(4), FARG(2), FARG(0))); return 12; }
static int h_spell_list_build(void) { RET8(spell_list_build((u8)ARG(0))); return 2; }
static int h_spell_list_choose(void) { RET8(spell_list_choose(FARG(0), (u8)ARG(2))); return 6; }
static int h_sheet_view(void) { sheet_view(FARG(0)); return 4; }
static int h_rest_show_time(void) { rest_show_time((s8)ARG(0)); return 2; }
static int h_rest_normalise(void) { rest_normalise(); return 0; }
static int h_rest_subtract(void) { rest_subtract((s8)ARG(0), (u8)ARG(1)); return 4; }
static int h_rest_menu(void) { RET8(rest_menu()); return 0; }
static int h_rest_healing(void) { rest_daily_healing(); return 0; }
static int h_rest_study(void) { rest_study(); return 0; }
static int h_rest_sleep_hour(void) { rest_sleep_hour(&mem[LIN(FARG(0))]); return 4; }
static int h_rest_memorise(void) { RET8(rest_memorise_next(&mem[LIN(FARG(0))], FARG(2))); return 8; }
static int h_rest_scribe(void) { RET8(rest_scribe_next(&mem[LIN(FARG(0))], FARG(2))); return 8; }
static int h_rest_screen(void) { RET8(rest_screen()); return 0; }
static int h_skip_code_wheel(void) { return 0; }
static int h_list_nth(void) { RET32(list_nth(FARG(0), (s16)ARG(2))); return 6; }
static int h_palette_reg(void) { gfx_palette_reg((u8)ARG(1), (u8)ARG(0)); return 4; }
static int h_sting_kill(void) { RET8(fight_sting_kill()); return 0; }
static int h_paramstr(void) { rtl_paramstr(FARG(1), ARG(0)); return 2; }     // a string function: the result's address stays

// Pieces of the routines hooked below that Ghidra lists as functions of their own (each translated with its routine,
// the entry it belongs to after the arrow): 2090:02A8 > 2090:0249; 2A9F:2A01 > 29F0; 2A9F:2AF4 > 2AE6; 2A9F:2B26 > 2B20;
// 2A9F:2BA3 > 2B7D; 2A9F:2C34 > 2C1C; 2A9F:2DB4 > 2D88; 2A9F:2E50 > 2E37; 2A9F:3674 > 31A2; 2E2A:0221 > 0191 (to 022F;
// after it come string constants, then the start of 02F9); 2E2A:03AD and 2E2A:048B > 02F9; 2E2A:0779 > 0712;
// 2E2A:08EB, 2E2A:0A1D, 2E2A:0A48, 2E2A:0B00, 2E2A:0C3E, 2E2A:0C49 > 08B2; 2E2A:0F01 > 0E82; 2E2A:1679, 2E2A:171B,
// 2E2A:1765, 2E2A:176E, 2E2A:17BA > 1406; 2E2A:186F > 17F6; 2E2A:1CCE, 2E2A:1D24 > 1BF8; 3C10:21D4, 3C10:22C3,
// 3C10:2468 > 3C10:2156; 2E2A:1BF2 (six bytes of padding, then the code of 1BF8, which has no entry of its own)
// > 1BF8; 3FA9:0006 > 3FA9:0000; 3FA9:0408 > 3FA9:0294.
void port_register_hooks(void) {
    menu_arrows_move = getenv("POOLRAD_ARROWS") != NULL;      // set by the Mac app; tests keep the original keys
    emu_hook(0x1464, 0x0000, h_color_mask, 1, "gfx_color_mask");
    emu_hook(0x1464, 0x007b, h_nibble, 1, "gfx_nibble");
    emu_hook(0x1464, 0x1951, h_draw_pic, 1, "gfx_draw_pic");
    emu_hook(0x1464, 0x08d4, h_merge_pic, 1, "gfx_merge_pic");
    emu_hook(0x1464, 0x1484, h_reverse_bits, 1, "gfx_reverse_bits");
    emu_hook(0x1464, 0x15c7, h_flip_pic, 1, "gfx_flip_pic");
    emu_hook(0x1464, 0x1b52, h_flush, 1, "gfx_flush_back_buffer");
    emu_hook(0x1464, 0x2632, h_fill_pic, 1, "gfx_fill_pic");
    emu_hook(0x1464, 0x248e, h_alloc_pic, 1, "gfx_alloc_pic");
    emu_hook(0x1464, 0x2595, h_free_pic, 1, "gfx_free_pic");
    emu_hook(0x1802, 0x0716, h_dax_load, 1, "dax_load");
    emu_hook(0x1709, 0x0000, h_reset_ega, 1, "text_reset_ega");
    emu_hook(0x1709, 0x0071, h_text_show_page, 1, "text_show_page");
    emu_hook(0x1709, 0x017c, h_text_load_font, 1, "text_load_font");
    emu_hook(0x1802, 0x094a, h_file_set_select, 1, "file_set_select");
    emu_hook(0x1802, 0x095d, h_file_set_restore, 1, "file_set_restore");
    emu_hook(0x1899, 0x0104, h_keys_flush, 1, "keys_flush");
    emu_hook(0x1899, 0x0000, h_game_quit, 1, "game_quit");
    emu_hook(0x1c7c, 0x0042, h_heap_error, 1, "heap_error");
    emu_hook(0x1c7c, 0x00ac, h_heap_error_install, 1, "heap_error_install");
    emu_hook(0x1802, 0x0040, h_text_bios_char, 1, "text_bios_char");
    emu_hook(0x1802, 0x0105, h_text_bios_string, 1, "text_bios_string");
    emu_hook(0x1802, 0x0175, h_text_bios_bar, 1, "text_bios_bar");
    emu_hook(0x1464, 0x00cf, h_gfx_set_mode, 1, "gfx_set_mode");
    emu_hook(0x1464, 0x00ff, h_gfx_restore_mode, 1, "gfx_restore_mode");
    emu_hook(0x1464, 0x1509, h_gfx_reverse_pairs, 1, "gfx_reverse_pairs");
    emu_hook(0x1464, 0x1596, h_gfx_swap_nibbles, 1, "gfx_swap_nibbles");
    emu_hook(0x1709, 0x00a0, h_work_page, 1, "text_set_work_page");
    emu_hook(0x1709, 0x00b7, h_fill_cells, 1, "text_fill_cells");
    emu_hook(0x1709, 0x01df, h_put_char, 1, "text_put_char");
    emu_hook(0x1709, 0x05eb, h_put_blanks, 1, "text_put_blanks");
    emu_hook(0x1709, 0x0626, h_put_string, 1, "text_put_string");
    emu_hook(0x1464, 0x02a5, h_decode_pic, 1, "gfx_decode_pic");
    emu_hook(0x1464, 0x0111, h_load_pic, 1, "gfx_load_pic");
    emu_hook(0x1709, 0x0dd3, h_clear_box, 1, "text_clear_box");
    emu_hook(0x1709, 0x0bf5, h_prompt, 1, "text_prompt");
    emu_hook(0x1709, 0x0768, h_print_wrapped, 1, "text_print_wrapped");
    emu_hook(0x1464, 0x0d71, h_merge_clipped, 1, "gfx_merge_pic_clipped");
    emu_hook(0x1464, 0x1e5c, h_recolor, 1, "gfx_recolor_pic");
    emu_hook(0x1899, 0x0059, h_get_key, 1, "input_get_key");
    emu_hook(0x1899, 0x0122, h_input_poll, 1, "input_poll");
    emu_hook(0x1709, 0x0cab, h_clear_screen, 1, "text_clear_screen");
    emu_hook(0x1709, 0x0dbe, h_pause, 1, "text_pause");
    emu_hook(0x1709, 0x0d68, h_flash, 1, "text_flash_message");
    emu_hook(0x1709, 0x09e3, h_input_string, 1, "text_input_string");
    emu_hook(0x1709, 0x0b66, h_input_number, 1, "text_input_number");
    emu_hook(0x1709, 0x0c3f, h_text_clock, 1, "text_clock");
    emu_hook(0x4726, 0x03bd, h_menu_horizontal, 1, "menu_horizontal");
    emu_hook(0x4726, 0x06fb, h_menu_clear_line, 1, "menu_clear_line");
    emu_hook(0x4726, 0x10d1, h_yes_no, 1, "menu_yes_no");
    emu_hook(0x4726, 0x1140, h_list_alloc, 1, "menu_list_alloc");
    emu_hook(0x4726, 0x11f3, h_list_free, 1, "menu_list_free");
    emu_hook(0x4726, 0x0d9a, h_menu_list, 1, "menu_list");
    emu_hook(0x489f, 0x0000, h_scene_draw, 1, "scene_draw");
    emu_hook(0x489f, 0x0050, h_scene_draw_plain, 1, "scene_draw_plain");
    emu_hook(0x489f, 0x0070, h_scene_draw_lower, 1, "scene_draw_lower");
    emu_hook(0x489f, 0x0697, h_scene_portrait, 1, "scene_draw_portrait");
    emu_hook(0x489f, 0x048f, h_scene_free, 1, "scene_free_anim");
    emu_hook(0x489f, 0x00a2, h_scene_load, 1, "scene_load_anim");
    emu_hook(0x2366, 0x0000, h_ecl_fetch, 1, "ecl_fetch");
    emu_hook(0x2366, 0x0136, h_ecl_operand, 1, "ecl_operand");
    emu_hook(0x2366, 0x06af, h_ecl_word, 1, "ecl_word");
    emu_hook(0x2366, 0x06d3, h_ecl_char, 1, "ecl_char");
    emu_hook(0x2366, 0x06fb, h_ecl_class, 1, "ecl_addr_class");
    emu_hook(0x2366, 0x0e0c, h_ecl_read, 1, "ecl_read_var");
    emu_hook(0x2366, 0x0c77, h_ecl_write, 1, "ecl_write_var");
    emu_hook(0x2366, 0x1153, h_ecl_unpack, 1, "ecl_unpack_text");
    emu_hook(0x1d3f, 0x00f4, h_op_goto, 1, "op_goto");
    emu_hook(0x1d3f, 0x0111, h_op_gosub, 1, "op_gosub");
    emu_hook(0x1d3f, 0x0a9f, h_op_return, 1, "op_return");
    emu_hook(0x1d3f, 0x0126, h_op_compare, 1, "op_compare");
    emu_hook(0x1d3f, 0x0aed, h_op_compare_and, 1, "op_compare_and");
    emu_hook(0x1d3f, 0x0b52, h_op_if, 1, "op_if");
    emu_hook(0x1d3f, 0x01a3, h_op_arith, 1, "op_arith");
    emu_hook(0x1d3f, 0x0def, h_op_and_or, 1, "op_and_or");
    emu_hook(0x1d3f, 0x0234, h_op_random, 1, "op_random");
    // Reached only through the native dispatcher's table in play; hooked too so that a test's "call" of the
    // handler's address runs the translation (t_checkparty, t_treasure, t_rob, t_damage).
    emu_hook(0x1d3f, 0x1435, h_op_check_party, 1, "op_check_party");
    emu_hook(0x1d3f, 0x19b0, h_op_treasure, 1, "op_treasure");
    emu_hook(0x1d3f, 0x1db1, h_op_rob, 1, "op_rob");
    emu_hook(0x1d3f, 0x2775, h_op_damage, 1, "op_damage");
    emu_hook(0x1d3f, 0x2ac1, h_op_clock, 1, "op_clock");
    emu_hook(0x1d3f, 0x2c03, h_op_spell, 1, "op_spell");
    emu_hook(0x1d3f, 0x0292, h_op_save, 1, "op_save");
    emu_hook(0x1d3f, 0x0e5e, h_op_gettable, 1, "op_gettable");
    emu_hook(0x1d3f, 0x0ebc, h_op_savetable, 1, "op_savetable");
    emu_hook(0x2366, 0x18eb, h_ecl_compare, 1, "ecl_compare");
    emu_hook(0x2366, 0x1951, h_ecl_gosub, 1, "ecl_gosub");
    emu_hook(0x2366, 0x1ea6, h_ecl_skip, 1, "ecl_skip");
    emu_hook(0x2366, 0x07bc, h_ecl_read_char, 1, "ecl_read_char_field");
    emu_hook(0x2366, 0x0a74, h_ecl_write_char, 1, "ecl_write_char_field");
    emu_hook(0x1d3f, 0x18f8, h_op_on_goto, 1, "op_on_goto");
    emu_hook(0x1d3f, 0x0a09, h_op_print, 1, "op_print");
    emu_hook(0x1d3f, 0x2aeb, h_op_print_return, 1, "op_print_return");
    emu_hook(0x1d3f, 0x2b14, h_op_clear_box, 1, "op_clear_box");
    emu_hook(0x1d3f, 0x271f, h_op_delay, 1, "op_delay");
    emu_hook(0x1d3f, 0x2aa2, h_op_sprite_off, 1, "op_sprite_off");
    emu_hook(0x2366, 0x12fb, h_ecl_text_from_var, 1, "ecl_text_from_var");
    emu_hook(0x2366, 0x0f53, h_ecl_store_text, 1, "ecl_store_text");
    emu_hook(0x1d3f, 0x0001, h_op_exit, 1, "op_exit");
    emu_hook(0x1d3f, 0x0951, h_op_input_number, 1, "op_input_number");
    emu_hook(0x1d3f, 0x0994, h_op_input_string, 1, "op_input_string");
    emu_hook(0x1d3f, 0x2f2d, h_ecl_step, 1, "ecl_step");
    emu_hook(0x1d3f, 0x3171, h_ecl_run, 1, "ecl_run");
    emu_hook(0x1d3f, 0x12c6, h_op_party_strength, 1, "op_party_strength");
    emu_hook(0x1d3f, 0x02e0, h_op_load_character, 1, "op_load_character");
    emu_hook(0x1d3f, 0x2ccf, h_op_call, 1, "op_call");
    emu_hook(0x1d3f, 0x0856, h_op_picture, 1, "op_picture");
    emu_hook(0x1d3f, 0x0bd1, h_op_newecl, 1, "op_newecl");
    emu_hook(0x1d3f, 0x0c74, h_op_load_files, 1, "op_load_files");
    emu_hook(0x2366, 0x0341, h_ecl_load_block, 1, "ecl_load_block");
    emu_hook(0x2366, 0x01b6, h_ecl_init_script, 1, "ecl_init_script");
    emu_hook(0x1d3f, 0x32f8, h_explore, 1, "explore");
    emu_hook(0x1d3f, 0x31b4, h_explore_enter, 1, "explore_enter_area");
    emu_hook(0x1d3f, 0x2dd3, h_explore_camp, 1, "explore_camp");
    emu_hook(0x1d3f, 0x125d, h_op_clear_monsters, 1, "op_clear_monsters");
    emu_hook(0x1d3f, 0x1629, h_op_party_surprise, 1, "op_party_surprise");
    emu_hook(0x1d3f, 0x16c9, h_op_surprise, 1, "op_surprise");
    emu_hook(0x1d3f, 0x179b, h_op_combat, 1, "op_combat");
    emu_hook(0x1d3f, 0x10d1, h_op_horizontal_menu, 1, "op_horizontal_menu");
    emu_hook(0x2366, 0x1684, h_ecl_menu, 1, "ecl_menu");
    emu_hook(0x2366, 0x156d, h_ecl_menu_prepare, 1, "ecl_menu_prepare");
    emu_hook(0x1d3f, 0x0450, h_op_load_monster, 1, "op_load_monster");
    emu_hook(0x1d3f, 0x1ef8, h_op_encounter_menu, 1, "op_encounter_menu");
    emu_hook(0x1d3f, 0x25cb, h_op_parlay, 1, "op_parlay");
    emu_hook(0x1d3f, 0x0f08, h_op_vertical_menu, 1, "op_vertical_menu");
    emu_hook(0x1d3f, 0x03b4, h_op_setup_monster, 1, "op_setup_monster");
    emu_hook(0x1d3f, 0x0817, h_op_approach, 1, "op_approach");
    emu_hook(0x1d3f, 0x266a, h_op_find_item, 1, "op_find_item");
    emu_hook(0x1d3f, 0x2b33, h_op_who, 1, "op_who");
    emu_hook(0x1d3f, 0x2b7e, h_op_add_npc, 1, "op_add_npc");
    emu_hook(0x1d3f, 0x2e10, h_op_program, 1, "op_program");
    emu_hook(0x1d3f, 0x2e7d, h_op_protection, 1, "op_protection");
    emu_hook(0x484b, 0x016d, h_view_redraw, 1, "view_redraw");
    emu_hook(0x484b, 0x0000, h_view_marker, 1, "view_draw_marker");
    emu_hook(0x491c, 0x0155, h_view_colors, 1, "view_set_colors");
    emu_hook(0x491c, 0x031f, h_map_in_bounds, 1, "map_in_bounds");
    emu_hook(0x491c, 0x047b, h_map_wall, 1, "map_wall");
    emu_hook(0x491c, 0x057a, h_map_event, 1, "map_event");
    emu_hook(0x491c, 0x0174, h_view_background, 1, "view_draw_background");
    emu_hook(0x491c, 0x0211, h_view_piece, 1, "view_draw_piece");
    emu_hook(0x491c, 0x0000, h_view_area_map, 1, "view_draw_area_map");
    emu_hook(0x491c, 0x05fd, h_view_render, 1, "view_render");
    emu_hook(0x4ca0, 0x015d, h_sym_put, 1, "sym_put");
    emu_hook(0x4ca0, 0x003b, h_sym_load_set, 1, "sym_load_set");
    emu_hook(0x4c2b, 0x0022, h_icon_load, 1, "icon_load");
    emu_hook(0x4c2b, 0x00f1, h_icon_draw, 1, "icon_draw");
    emu_hook(0x4c2b, 0x014e, h_icon_free_slot, 1, "icon_free_slot");
    emu_hook(0x491c, 0x0350, h_map_passage, 1, "map_passage");
    emu_hook(0x491c, 0x10c7, h_map_load, 1, "map_load");
    emu_hook(0x491c, 0x0e08, h_map_load_walls, 1, "map_load_walls");
    emu_hook(0x31f2, 0x0989, h_explore_command, 1, "explore_command");
    emu_hook(0x31f2, 0x068d, h_move_step_forward, 1, "move_step_forward");
    emu_hook(0x31f2, 0x073b, h_move_into_square, 1, "move_into_square");
    emu_hook(0x31f2, 0x0818, h_move_overland, 1, "move_overland");
    emu_hook(0x31f2, 0x0d6d, h_move_party, 1, "move_party");
    emu_hook(0x31f2, 0x0000, h_door_remove, 1, "door_remove");
    emu_hook(0x31f2, 0x0109, h_door_unlock, 1, "door_unlock");
    emu_hook(0x31f2, 0x0236, h_party_has_class, 1, "party_has_class");
    emu_hook(0x31f2, 0x0294, h_door_bash, 1, "door_bash");
    emu_hook(0x31f2, 0x04df, h_door_pick, 1, "door_pick");
    emu_hook(0x31f2, 0x05ab, h_spell_slot, 1, "spell_slot");
    emu_hook(0x31f2, 0x05ee, h_party_find_spell, 1, "party_find_spell");
    emu_hook(0x31f2, 0x0648, h_door_knock, 1, "door_knock");
    emu_hook(0x3ec2, 0x02f2, h_time_carry, 1, "time_carry");
    emu_hook(0x3ec2, 0x03d5, h_time_advance, 1, "time_advance");
    emu_hook(0x10ba, 0x290b, h_hud_status, 1, "hud_draw_status");
    emu_hook(0x10ba, 0x1816, h_hud_name, 1, "hud_draw_name");
    emu_hook(0x10ba, 0x08ef, h_hud_ac, 1, "hud_draw_ac");
    emu_hook(0x10ba, 0x0973, h_hud_hp, 1, "hud_draw_hp");
    emu_hook(0x10ba, 0x0767, h_hud_roster, 1, "hud_draw_roster");
    emu_hook(0x10ba, 0x272a, h_hud_frames, 1, "hud_draw_frames");
    emu_hook(0x10ba, 0x27d9, h_hud_screen, 1, "hud_draw_screen");
    emu_hook(0x13f1, 0x0000, h_ui_border, 1, "ui_border");
    emu_hook(0x13f1, 0x02e8, h_ui_frame, 1, "ui_frame");
    emu_hook(0x13f1, 0x0137, h_ui_clear_box, 1, "ui_clear_box");
    emu_hook(0x489f, 0x05d9, h_scene_load_portrait, 1, "scene_load_portrait");
    emu_hook(0x489f, 0x0708, h_scene_anim_frame, 1, "scene_draw_anim_frame");
    emu_hook(0x489f, 0x04f9, h_scene_char_portrait, 1, "scene_show_character_portrait");
    emu_hook(0x32fd, 0x0000, h_camp_rest_needed, 1, "camp_rest_needed");
    emu_hook(0x32fd, 0x077a, h_camp_rest, 1, "camp_rest");
    emu_hook(0x32fd, 0x036c, h_camp_allowed, 1, "camp_magic_allowed");
    emu_hook(0x32fd, 0x025c, h_camp_slots_free, 1, "camp_slots_free");
    emu_hook(0x32fd, 0x0159, h_camp_unpick_memory, 1, "camp_unpick_memory");
    emu_hook(0x32fd, 0x019c, h_camp_unpick_scrolls, 1, "camp_unpick_scrolls");
    emu_hook(0x32fd, 0x0214, h_camp_unpick_party, 1, "camp_unpick_party");
    emu_hook(0x32fd, 0x04a1, h_camp_cast, 1, "camp_cast");
    emu_hook(0x32fd, 0x13f1, h_camp_magic, 1, "camp_magic");
    emu_hook(0x32fd, 0x0824, h_camp_sort_slots, 1, "camp_sort_slots");
    emu_hook(0x32fd, 0x05a4, h_camp_show_slots, 1, "camp_show_slots");
    emu_hook(0x32fd, 0x090a, h_camp_memorize, 1, "camp_memorize");
    emu_hook(0x32fd, 0x0b5b, h_camp_scribe, 1, "camp_scribe");
    emu_hook(0x32fd, 0x0fbc, h_camp_display, 1, "camp_display");
    emu_hook(0x10ba, 0x2199, h_find_effect, 1, "char_find_effect");
    emu_hook(0x32fd, 0x14a9, h_camp_order_up, 1, "camp_order_up");
    emu_hook(0x32fd, 0x1601, h_camp_order_down, 1, "camp_order_down");
    emu_hook(0x32fd, 0x1780, h_camp_order, 1, "camp_order");
    emu_hook(0x32fd, 0x18e5, h_camp_drop_char, 1, "camp_drop_character");
    emu_hook(0x32fd, 0x1a97, h_camp_speed, 1, "camp_speed");
    emu_hook(0x32fd, 0x1c59, h_camp_alter, 1, "camp_alter");
    emu_hook(0x10ba, 0x17e3, h_ui_clear_message, 1, "ui_clear_message");
    emu_hook(0x10ba, 0x1693, h_ui_notice, 1, "ui_notice");
    emu_hook(0x10ba, 0x16ec, h_ui_char_message, 1, "ui_char_message");
    emu_hook(0x10ba, 0x11ea, h_chr_str_index, 1, "chr_strength_index");
    emu_hook(0x10ba, 0x1282, h_chr_hit_bonus, 1, "chr_strength_hit_bonus");
    emu_hook(0x10ba, 0x1325, h_chr_dmg_bonus, 1, "chr_strength_damage_bonus");
    emu_hook(0x10ba, 0x10e6, h_chr_dex_defence, 1, "chr_dex_defence");
    emu_hook(0x10ba, 0x1168, h_chr_dex_missile, 1, "chr_dex_missile");
    emu_hook(0x10ba, 0x0b73, h_chr_listed_effect, 1, "chr_has_listed_effect");
    emu_hook(0x10ba, 0x148b, h_chr_forget_spell, 1, "chr_forget_spell");
    emu_hook(0x10ba, 0x1037, h_chr_str_byte, 1, "chr_number_text(byte)");
    emu_hook(0x10ba, 0x1073, h_chr_str_word, 1, "chr_number_text(word)");
    emu_hook(0x10ba, 0x10ad, h_chr_str_long, 1, "chr_number_text(long)");
    emu_hook(0x10ba, 0x13ab, h_chr_carry, 1, "chr_strength_carry");
    emu_hook(0x10ba, 0x23b7, h_chr_party_side, 1, "chr_on_party_side");
    emu_hook(0x10ba, 0x23db, h_chr_count_sides, 1, "chr_count_sides");
    emu_hook(0x10ba, 0x2637, h_chr_clear_action, 1, "chr_clear_action");
    emu_hook(0x10ba, 0x2686, h_chr_guard, 1, "chr_guard");
    emu_hook(0x10ba, 0x26c2, h_chr_caster_level, 1, "chr_caster_level");
    emu_hook(0x10ba, 0x2b5e, h_chr_back_exploring, 1, "chr_back_to_exploring");
    emu_hook(0x10ba, 0x2e03, h_chr_missile, 1, "chr_has_missile_weapon");
    emu_hook(0x10ba, 0x2e46, h_chr_missile14, 1, "chr_has_missile_weapon_14");
    emu_hook(0x10ba, 0x234a, h_chr_healed, 1, "chr_healed_message");
    emu_hook(0x10ba, 0x0000, h_chr_weapon_stats, 1, "chr_weapon_stats");
    emu_hook(0x10ba, 0x01fb, h_chr_armour_move, 1, "chr_armour_movement");
    emu_hook(0x10ba, 0x0282, h_chr_item_armour, 1, "chr_item_armour");
    emu_hook(0x10ba, 0x03a0, h_chr_load_move, 1, "chr_load_movement");
    emu_hook(0x10ba, 0x0bb8, h_chr_recalculate, 1, "chr_recalculate");
    emu_hook(0x10ba, 0x0442, h_chr_item_name, 1, "chr_item_name");
    emu_hook(0x10ba, 0x2c48, h_chr_pick, 1, "chr_pick_character");
    emu_hook(0x4592, 0x0de3, h_roll_dice, 1, "roll_dice");
    emu_hook(0x4592, 0x0e2e, h_roll_dice_noted, 1, "roll_dice_noted");
    emu_hook(0x4592, 0x0fd2, h_fx_remove_invis, 1, "effects_remove_invisibility");
    emu_hook(0x4592, 0x100a, h_fx_remove16, 1, "effects_remove_list16");
    emu_hook(0x4592, 0x1041, h_fx_remove2, 1, "effects_remove_list2");
    emu_hook(0x4592, 0x10e4, h_rules_encode, 1, "rules_encode");
    emu_hook(0x4592, 0x0026, h_effect_remove, 1, "effect_remove");
    emu_hook(0x4592, 0x0160, h_effect_check, 1, "effect_check");
    emu_hook(0x4afb, 0x0c5f, h_combat_slot, 1, "combat_slot");
    emu_hook(0x4afb, 0x0be7, h_combat_x, 1, "combat_x");
    emu_hook(0x4afb, 0x0c0f, h_combat_y, 1, "combat_y");
    emu_hook(0x4afb, 0x0c37, h_combat_size, 1, "combat_size");
    emu_hook(0x4a3d, 0x0000, h_combat_sign, 1, "combat_sign");
    emu_hook(0x4a3d, 0x01a0, h_line_begin, 1, "line_begin");
    emu_hook(0x4a3d, 0x02cc, h_line_step, 1, "line_step");
    emu_hook(0x4a3d, 0x002e, h_reach_sort, 1, "reach_list_sort");
    emu_hook(0x4a3d, 0x0422, h_line_of_sight, 1, "line_of_sight");
    emu_hook(0x4afb, 0x0000, h_combat_corner, 1, "combat_corner");
    emu_hook(0x4a3d, 0x057b, h_combat_in_arc, 1, "combat_in_arc");
    emu_hook(0x4a3d, 0x0909, h_combat_reach, 1, "combat_reach");
    emu_hook(0x4afb, 0x0068, h_combat_view_pos, 1, "combat_view_positions");
    emu_hook(0x4afb, 0x00e3, h_combat_gfx_a, 1, "combat_gfx_mode_a");
    emu_hook(0x4afb, 0x0105, h_combat_gfx_b, 1, "combat_gfx_mode_b");
    emu_hook(0x4afb, 0x04be, h_combat_square, 1, "combat_square");
    emu_hook(0x4afb, 0x06e9, h_combat_in_view, 1, "combat_in_view");
    emu_hook(0x4afb, 0x1298, h_combat_redraw_one, 1, "combat_redraw_one");
    emu_hook(0x4afb, 0x071a, h_combat_visible, 1, "combat_visible");
    emu_hook(0x4afb, 0x02d5, h_combat_redraw_sq, 1, "combat_redraw_square");
    emu_hook(0x4afb, 0x0af8, h_combat_face, 1, "combat_face");
    emu_hook(0x4afb, 0x052b, h_combat_draw_ground, 1, "combat_draw_ground");
    emu_hook(0x4afb, 0x0996, h_combat_step_view, 1, "combat_step_view");
    emu_hook(0x4afb, 0x0127, h_combat_draw_at, 1, "combat_draw_at");
    emu_hook(0x4afb, 0x07d4, h_combat_scroll_view, 1, "combat_scroll_view");
    emu_hook(0x4afb, 0x03a0, h_combat_place_all, 1, "combat_place_all");
    emu_hook(0x4afb, 0x0cb9, h_combat_look_ahead, 1, "combat_look_ahead");
    emu_hook(0x4afb, 0x0e11, h_combat_fall, 1, "combat_fall");
    emu_hook(0x4afb, 0x10a7, h_combat_place, 1, "combat_place");
    emu_hook(0x2843, 0x012b, h_combat_dungeon_side, 1, "combat_dungeon_side");
    emu_hook(0x2843, 0x01ad, h_combat_dungeon_edge, 1, "combat_dungeon_edge");
    emu_hook(0x26b0, 0x0f4f, h_fight_stand_down, 1, "fight_stand_down");
    emu_hook(0x26b0, 0x0203, h_fight_turn_undead, 1, "fight_try_turn_undead");
    emu_hook(0x26b0, 0x0fc1, h_fight_check_keys, 1, "fight_check_keys");
    emu_hook(0x26b0, 0x0675, h_fight_can_step, 1, "fight_can_step");
    emu_hook(0x26b0, 0x03e4, h_fight_try_item, 1, "fight_try_item");
    emu_hook(0x26b0, 0x0545, h_fight_try_spell, 1, "fight_try_spell");
    emu_hook(0x26b0, 0x128d, h_fight_weapon_score, 1, "fight_weapon_score");
    emu_hook(0x2e2a, 0x0e52, h_fight_half, 1, "fight_half");
    emu_hook(0x2e2a, 0x0123, h_fight_movement, 1, "fight_movement");
    emu_hook(0x2e2a, 0x263d, h_fight_direction, 1, "fight_direction");
    emu_hook(0x2e2a, 0x1085, h_fight_can_target, 1, "fight_can_target");
    emu_hook(0x2e2a, 0x2574, h_fight_backstab, 1, "fight_is_backstab");
    emu_hook(0x2e2a, 0x0627, h_fight_guards_strike, 1, "fight_guards_strike");
    emu_hook(0x2e2a, 0x17f6, h_fight_note_attacker, 1, "fight_note_attacker");
    emu_hook(0x2e2a, 0x1406, h_fight_attack, 1, "fight_attack");
    emu_hook(0x4592, 0x0cb7, h_fight_hit_roll, 1, "fight_hit_roll");
    emu_hook(0x4592, 0x0c4f, h_hit_unaimed, 1, "rules_hit_unaimed");
    emu_hook(0x2e2a, 0x1b1e, h_fight_range_penalty, 1, "fight_range_penalty");
    emu_hook(0x2e2a, 0x0191, h_fight_roll_damage, 1, "fight_roll_damage");
    emu_hook(0x2e2a, 0x02f9, h_fight_deal_damage, 1, "fight_deal_damage");
    emu_hook(0x10ba, 0x2228, h_chr_apply_damage, 1, "chr_apply_damage");
    emu_hook(0x10ba, 0x255b, h_combat_distance, 1, "combat_distance");
    emu_hook(0x10ba, 0x2433, h_combat_enemies, 1, "combat_enemies_in_reach");
    emu_hook(0x2e2a, 0x0d23, h_fight_set_attacks, 1, "fight_set_attacks");
    emu_hook(0x2e2a, 0x0000, h_fight_round_start, 1, "fight_round_start");
    emu_hook(0x2e2a, 0x1884, h_fight_strike, 1, "fight_strike");
    emu_hook(0x4592, 0x02fa, h_effects_event, 1, "effects_event");
    emu_hook(0x2a9f, 0x017f, h_fx_04_train, 1, "fx_04_train");
    emu_hook(0x2a9f, 0x0bcf, h_fx_22_both, 1, "fx_22_both");
    emu_hook(0x2a9f, 0x1276, h_fx_30_vs_bugbear, 1, "fx_30_vs_bugbear");
    emu_hook(0x2a9f, 0x23a4, h_fx_55_drain1, 1, "fx_55_drain1");
    emu_hook(0x2a9f, 0x23c2, h_fx_56_drain2, 1, "fx_56_drain2");
    emu_hook(0x2a9f, 0x28bf, h_fx_67_gaseous, 1, "fx_67_gaseous");
    emu_hook(0x2a9f, 0x2c1c, h_fx_79_breath, 1, "fx_79_breath");
    emu_hook(0x2a9f, 0x2eda, h_fx_item_effect, 1, "fx_item_effect");
    emu_hook(0x2a9f, 0x2f57, h_fx_item_ogre_power, 1, "fx_item_ogre_power");
    emu_hook(0x2a9f, 0x30a5, h_fx_item_alignment, 1, "fx_item_alignment");
    emu_hook(0x2a9f, 0x3134, h_fx_item_giant_str, 1, "fx_item_giant_str");
    emu_hook(0x2a9f, 0x317a, h_fx_item_drop_17, 1, "fx_item_drop_17");
    emu_hook(0x2a9f, 0x07e0, h_fx_17_gift, 1, "fx_17_gift");
    emu_hook(0x2a9f, 0x1cad, h_fx_53_stone_gaze, 1, "fx_53_stone_gaze");
    emu_hook(0x2a9f, 0x1e77, h_fx_54_charm_gaze, 1, "fx_54_charm_gaze");
    emu_hook(0x4592, 0x0000, h_effect_run, 1, "effect_run");
    emu_hook(0x2a9f, 0x319b, h_fx_319b, 1, "fx_nothing");
    emu_hook(0x2a9f, 0x0647, h_fx_0647, 1, "fx_nothing");
    emu_hook(0x2a9f, 0x1355, h_fx_1355, 1, "fx_nothing");
    emu_hook(0x2a9f, 0x135c, h_fx_135c, 1, "fx_nothing");
    emu_hook(0x2a9f, 0x010d, h_fx_010d, 1, "fx_bless");
    emu_hook(0x2a9f, 0x0c06, h_fx_0c06, 1, "fx_prayer_friend");
    emu_hook(0x2a9f, 0x0c15, h_fx_0c15, 1, "fx_bestow_curse");
    emu_hook(0x2a9f, 0x10c1, h_fx_10c1, 1, "fx_slow");
    emu_hook(0x2a9f, 0x171f, h_fx_171f, 1, "fx_invisible");
    emu_hook(0x2a9f, 0x011d, h_fx_011d, 1, "fx_curse");
    emu_hook(0x2a9f, 0x25a2, h_fx_25a2, 1, "fx_half_fire_5d");
    emu_hook(0x2a9f, 0x2b63, h_fx_2b63, 1, "fx_half_cold_76");
    emu_hook(0x2a9f, 0x2a6d, h_fx_2a6d, 1, "fx_half_third_72");
    emu_hook(0x2a9f, 0x0a3d, h_fx_0a3d, 1, "fx_enfeebled");
    emu_hook(0x2a9f, 0x03e1, h_fx_03e1, 1, "fx_resist_cold");
    emu_hook(0x2a9f, 0x06e4, h_fx_06e4, 1, "fx_resist_fire");
    emu_hook(0x2a9f, 0x0c26, h_fx_0c26, 1, "fx_blink");
    emu_hook(0x2a9f, 0x1452, h_fx_1452, 1, "fx_rooted");
    emu_hook(0x2a9f, 0x133f, h_fx_133f, 1, "fx_helpless");
    emu_hook(0x2a9f, 0x05d6, h_fx_05d6, 1, "fx_restore_charisma");
    emu_hook(0x2a9f, 0x2976, h_fx_2976, 1, "fx_resist_magic_50");
    emu_hook(0x2a9f, 0x2984, h_fx_2984, 1, "fx_resist_magic_15");
    emu_hook(0x2a9f, 0x29cb, h_fx_29cb, 1, "fx_immune_hold");
    emu_hook(0x2a9f, 0x29b6, h_fx_29b6, 1, "fx_immune_charm_sleep");
    emu_hook(0x2a9f, 0x29f0, h_fx_29f0, 1, "fx_immune_6f");
    emu_hook(0x2a9f, 0x29d9, h_fx_29d9, 1, "fx_immune_cold");
    emu_hook(0x2a9f, 0x2a11, h_fx_2a11, 1, "fx_immune_fire");
    emu_hook(0x2a9f, 0x2992, h_fx_2992, 1, "fx_elf_resistance");
    emu_hook(0x2a9f, 0x2de4, h_fx_2de4, 1, "fx_half_elf_resistance");
    emu_hook(0x2a9f, 0x2901, h_fx_2901, 1, "fx_avoid_missiles");
    emu_hook(0x2a9f, 0x2ae6, h_fx_2ae6, 1, "fx_half_from_magic_weapons");
    emu_hook(0x2a9f, 0x2b7d, h_fx_2b7d, 1, "fx_needs_magic_weapon");
    emu_hook(0x2a9f, 0x2b20, h_fx_2b20, 1, "fx_type55_damage");
    emu_hook(0x2a9f, 0x1472, h_fx_1472, 1, "fx_grant_62");
    emu_hook(0x2a9f, 0x1363, h_fx_1363, 1, "fx_grant_invisibility");
    emu_hook(0x2a9f, 0x0bac, h_fx_0bac, 1, "fx_blinded");
    emu_hook(0x2a9f, 0x064e, h_fx_064e, 1, "fx_shield");
    emu_hook(0x2a9f, 0x0000, h_fx_negate, 1, "fx_negate");
    emu_hook(0x2a9f, 0x0fac, h_fx_missile_deflect, 1, "fx_missile_deflect");
    emu_hook(0x2a9f, 0x101d, h_fx_weapon_in_use, 1, "fx_weapon_in_use");
    emu_hook(0x2a9f, 0x2915, h_fx_magic_resist, 1, "fx_magic_resist");
    emu_hook(0x4592, 0x0e52, h_effect_add, 1, "effect_add");
    emu_hook(0x2a9f, 0x165d, h_fx_165d, 1, "fx_poison_0");
    emu_hook(0x2a9f, 0x1671, h_fx_1671, 1, "fx_poison_plus4");
    emu_hook(0x2a9f, 0x1685, h_fx_1685, 1, "fx_poison_plus2");
    emu_hook(0x2a9f, 0x170b, h_fx_170b, 1, "fx_poison_minus2");
    emu_hook(0x2a9f, 0x1699, h_fx_1699, 1, "fx_paralyse_2d8");
    emu_hook(0x2a9f, 0x16e6, h_fx_16e6, 1, "fx_paralyse_long");
    emu_hook(0x4592, 0x0d63, h_saving_throw, 1, "rules_saving_throw");
    emu_hook(0x2a9f, 0x0058, h_fx_strike_down, 1, "fx_strike_down");
    emu_hook(0x2a9f, 0x1545, h_fx_poison, 1, "fx_poison");
    emu_hook(0x2a9f, 0x15eb, h_fx_paralyse, 1, "fx_paralyse");
    emu_hook(0x2a9f, 0x0377, h_fx_0377, 1, "fx_protection_from_evil");
    emu_hook(0x2a9f, 0x03ac, h_fx_03ac, 1, "fx_protection_from_good");
    emu_hook(0x2a9f, 0x0918, h_fx_0918, 1, "fx_invisibility");
    emu_hook(0x2a9f, 0x2700, h_fx_2700, 1, "fx_regenerate");
    emu_hook(0x2a9f, 0x2e08, h_fx_2e08, 1, "fx_undead_immunities");
    emu_hook(0x2a9f, 0x1730, h_fx_1730, 1, "fx_turn_invisible");
    emu_hook(0x2a9f, 0x16bb, h_fx_16bb, 1, "fx_paralyse_not_elves");
    emu_hook(0x2a9f, 0x013b, h_fx_013b, 1, "fx_bonus_vs_type4");
    emu_hook(0x2a9f, 0x2d88, h_fx_2d88, 1, "fx_silver_or_magic");
    emu_hook(0x2a9f, 0x1a19, h_fx_1a19, 1, "fx_fire_touch");
    emu_hook(0x2a9f, 0x1a4c, h_fx_1a4c, 1, "fx_acid_touch");
    emu_hook(0x4592, 0x1344, h_rules_damage, 1, "rules_damage");
    emu_hook(0x2a9f, 0x1086, h_fx_1086, 1, "fx_protection_normal_missiles");
    emu_hook(0x2a9f, 0x264b, h_fx_264b, 1, "fx_only_silver_or_magic");
    emu_hook(0x2a9f, 0x2a28, h_fx_2a28, 1, "fx_fire_minus_one_per_die");
    emu_hook(0x2a9f, 0x2610, h_fx_2610, 1, "fx_falls_dead");
    emu_hook(0x2a9f, 0x27d9, h_fx_27d9, 1, "fx_troll_falls");
    emu_hook(0x2a9f, 0x2866, h_fx_2866, 1, "fx_troll_rises");
    emu_hook(0x2a9f, 0x19d9, h_fx_19d9, 1, "fx_comes_round");
    emu_hook(0x2a9f, 0x12fb, h_fx_12fb, 1, "fx_slow_healing");
    emu_hook(0x2a9f, 0x14eb, h_fx_14eb, 1, "fx_fast_healing");
    emu_hook(0x2a9f, 0x001f, h_fx_add_once, 1, "fx_add_once");
    emu_hook(0x4592, 0x1869, h_rules_get_up, 1, "rules_get_up");
    emu_hook(0x4592, 0x175d, h_rules_heal, 1, "rules_heal");
    emu_hook(0x2a9f, 0x12b2, h_fx_12b2, 1, "fx_prayer");
    emu_hook(0x2a9f, 0x2490, h_fx_2490, 1, "fx_displaced");
    emu_hook(0x2a9f, 0x25bc, h_fx_25bc, 1, "fx_half_from_weapon_class");
    emu_hook(0x2a9f, 0x2bc8, h_fx_2bc8, 1, "fx_avoid_types_57_58");
    emu_hook(0x2a9f, 0x2556, h_fx_2556, 1, "fx_unaffected_by_missile_and_lightning");
    emu_hook(0x2a9f, 0x2d38, h_fx_2d38, 1, "fx_type56_and_fire_bonus");
    emu_hook(0x2a9f, 0x2814, h_fx_2814, 1, "fx_start_regenerating");
    emu_hook(0x2a9f, 0x070f, h_fx_070f, 1, "fx_silenced");
    emu_hook(0x2a9f, 0x1490, h_fx_1490, 1, "fx_fire_resistance");
    emu_hook(0x2a9f, 0x05eb, h_fx_05eb, 1, "fx_slow_loss");
    emu_hook(0x2a9f, 0x0c4b, h_fx_0c4b, 1, "fx_haste");
    emu_hook(0x2a9f, 0x2a87, h_fx_2a87, 1, "fx_half_from_weapon_class_b");
    emu_hook(0x2a9f, 0x01c3, h_fx_01c3, 1, "fx_burning_weapon");
    emu_hook(0x2a9f, 0x0775, h_fx_0775, 1, "fx_slow_poison_ends");
    emu_hook(0x2a9f, 0x0679, h_fx_0679, 1, "fx_enmity_kobolds_goblins");
    emu_hook(0x2a9f, 0x0947, h_fx_0947, 1, "fx_enmity_orcs_goblins");
    emu_hook(0x2a9f, 0x11f9, h_fx_11f9, 1, "fx_small_and_hard_to_hit");
    emu_hook(0x2a9f, 0x24d9, h_fx_24d9, 1, "fx_hardy");
    emu_hook(0x2a9f, 0x18b5, h_fx_18b5, 1, "fx_grab");
    emu_hook(0x2a9f, 0x09c0, h_fx_09c0, 1, "fx_mirror_image");
    emu_hook(0x2a9f, 0x10de, h_fx_10de, 1, "fx_wasting");
    emu_hook(0x2a9f, 0x1168, h_fx_1168, 1, "fx_disease");
    emu_hook(0x2a9f, 0x0b1b, h_fx_0b1b, 1, "fx_animation_ends");
    emu_hook(0x2a9f, 0x268a, h_fx_268a, 1, "fx_hardy_magic");
    emu_hook(0x2a9f, 0x1935, h_fx_1935, 1, "fx_hold_fast");
    emu_hook(0x2a9f, 0x23ec, h_fx_23ec, 1, "fx_mummy_touch");
    emu_hook(0x2a9f, 0x1399, h_fx_1399, 1, "fx_mummy_rot");
    emu_hook(0x2a9f, 0x2730, h_fx_2730, 1, "fx_last_effort");
    emu_hook(0x2a9f, 0x0a65, h_fx_0a65, 1, "fx_stinking_cloud");
    emu_hook(0x2a9f, 0x0400, h_fx_0400, 1, "fx_charm");
    emu_hook(0x2a9f, 0x0233, h_fx_0233, 1, "fx_manual_of_bodily_health");
    emu_hook(0x2a9f, 0x04ba, h_fx_04ba, 1, "fx_strength_ends");
    emu_hook(0x2a9f, 0x1aaa, h_fx_1aaa, 1, "fx_fear_aura");
    emu_hook(0x2a9f, 0x1ba8, h_fx_1ba8, 1, "fx_terror");
    emu_hook(0x4592, 0x1656, h_rules_spell_effect, 1, "rules_spell_effect");
    emu_hook(0x4592, 0x110d, h_rules_strength_decode, 1, "rules_strength_decode");
    emu_hook(0x2a9f, 0x2e37, h_fx_2e37, 1, "fx_warded_off");
    emu_hook(0x2a9f, 0x176f, h_fx_176f, 1, "fx_draining_grip");
    emu_hook(0x2e2a, 0x39d2, h_fx2_39d2, 1, "fx_rake");
    emu_hook(0x2e2a, 0x3a86, h_fx2_3a86, 1, "fx_hug");
    emu_hook(0x41cf, 0x0729, h_spell_range, 1, "spell_range");
    emu_hook(0x26b0, 0x02eb, h_fight_spell_worth, 1, "fight_spell_worth_casting");
    emu_hook(0x4592, 0x0f06, h_chr_take_out, 1, "chr_take_out");
    emu_hook(0x2e2a, 0x2908, h_fight_fastest_pursuer, 1, "fight_fastest_pursuer");
    emu_hook(0x26b0, 0x10d7, h_fight_morale, 1, "fight_morale");
    emu_hook(0x34fb, 0x056c, h_chr_create, 1, "chr_create");
    emu_hook(0x1c88, 0x0000, h_wait_seconds_or_key, 1, "wait_seconds_or_key");
    emu_hook(0x1c88, 0x0206, h_title_screen, 1, "title_screen");
    emu_hook(0x3941, 0x0aec, h_chr_load_file, 1, "chr_load_file");
    emu_hook(0x3941, 0x069e, h_chr_save_file, 1, "chr_save_file");
    emu_hook(0x34fb, 0x4235, h_chr_roll_hp, 1, "chr_roll_hp");
    emu_hook(0x34fb, 0x3f31, h_chr_con_bonus, 1, "chr_con_bonus");
    emu_hook(0x454b, 0x01af, h_chr_wisdom_spells, 1, "chr_wisdom_spells");
    emu_hook(0x34fb, 0x33e7, h_chr_pick_portrait, 1, "chr_pick_portrait");
    emu_hook(0x1802, 0x0356, h_str_remove_all, 1, "str_remove_all");
    emu_hook(0x1802, 0x0425, h_str_dos_name, 1, "str_dos_name");
    emu_hook(0x1802, 0x02eb, h_file_exists, 1, "file_exists");
    emu_hook(0x1802, 0x0193, h_file_disk_prompt, 1, "file_disk_prompt");
    emu_hook(0x1802, 0x025d, h_file_open, 1, "file_open");
    emu_hook(0x3941, 0x1d58, h_game_save, 1, "game_save");
    emu_hook(0x3941, 0x02ae, h_save_dir_make, 1, "save_dir_make");
    emu_hook(0x2843, 0x1577, h_bf_place_one, 1, "bf_place_one");
    emu_hook(0x4c2b, 0x01c8, h_sprite_load, 1, "sprite_load");
    emu_hook(0x3fa9, 0x0b93, h_coin_kind, 1, "coin_kind");
    emu_hook(0x3fa9, 0x0294, h_input_amount, 1, "input_amount");
    emu_hook(0x3fa9, 0x0a88, h_take_coins, 1, "take_coins");
    emu_hook(0x3fa9, 0x0ceb, h_take_money, 1, "take_money");
    emu_hook(0x3fa9, 0x05e0, h_share_count, 1, "share_count");
    emu_hook(0x3fa9, 0x063a, h_treasure_share, 1, "treasure_share");
    emu_hook(0x3941, 0x0010, h_roster_read, 1, "roster_read");
    emu_hook(0x3941, 0x01b8, h_roster_write, 1, "roster_write");
    emu_hook(0x34fb, 0x306d, h_party_add_menu, 1, "party_add_menu");
    emu_hook(0x454b, 0x0000, h_chr_recalc_levels, 1, "chr_recalc_levels");
    emu_hook(0x34fb, 0x3291, h_party_remove, 1, "party_remove");
    emu_hook(0x4876, 0x0000, h_view_overhead, 1, "view_overhead");
    emu_hook(0x3c10, 0x0ef8, h_sheet_items, 1, "sheet_items");
    emu_hook(0x34fb, 0x0161, h_party_menu, 1, "party_menu");
    emu_hook(0x3c10, 0x0d70, h_item_may_part, 1, "item_may_part");
    emu_hook(0x3c10, 0x17ed, h_item_halve, 1, "item_halve");
    emu_hook(0x3c10, 0x189d, h_item_join, 1, "item_join");
    emu_hook(0x2184, 0x1527, h_combat_end, 1, "combat_end");
    emu_hook(0x2366, 0x054e, h_show_monsters, 1, "show_monsters");
    emu_hook(0x2366, 0x0511, h_show_closeup, 1, "show_closeup");
    emu_hook(0x3ec2, 0x0000, h_effects_tick, 1, "effects_tick");
    emu_hook(0x34fb, 0x35be, h_icon_copy, 1, "icon_copy");
    emu_hook(0x34fb, 0x34df, h_icon_show, 1, "icon_show");
    emu_hook(0x34fb, 0x382d, h_icon_editor, 1, "icon_editor");
    emu_hook(0x3941, 0x1602, h_game_load, 1, "game_load");
    emu_hook(0x34fb, 0x1f37, h_party_drop, 1, "party_drop");
    emu_hook(0x34fb, 0x3fb3, h_hp_least, 1, "hp_least");
    emu_hook(0x34fb, 0x408c, h_hp_most, 1, "hp_most");
    emu_hook(0x34fb, 0x209f, h_chr_modify, 1, "chr_modify");
    emu_hook(0x34fb, 0x29ae, h_chr_train, 1, "chr_train");
    emu_hook(0x41cf, 0x3413, h_spell_table_init, 1, "spell_table_init");
    emu_hook(0x2a9f, 0x31a2, h_effect_table_init, 1, "effect_table_init");
    emu_hook(0x2a3d, 0x002d, h_game_start, 1, "game_start");
    emu_hook(0x3c10, 0x28be, h_money_of, 1, "money_of");
    emu_hook(0x3fa9, 0x00a8, h_money_of_pool, 1, "money_of_pool");
    emu_hook(0x3fa9, 0x0130, h_pay_from, 1, "pay_from");
    emu_hook(0x3fa9, 0x0185, h_pay_from_pool, 1, "pay_from_pool");
    emu_hook(0x10ba, 0x1509, h_item_lose, 1, "item_lose");
    emu_hook(0x10ba, 0x15f7, h_item_give, 1, "item_give");
    emu_hook(0x3c10, 0x275b, h_item_too_much, 1, "item_too_much");
    emu_hook(0x3c10, 0x1734, h_item_trade, 1, "item_trade");
    emu_hook(0x3c10, 0x1cee, h_item_sell, 1, "item_sell");
    emu_hook(0x3c10, 0x1f52, h_item_identify, 1, "item_identify");
    emu_hook(0x3fa9, 0x044c, h_coins_transfer, 1, "coins_transfer");
    emu_hook(0x3c10, 0x2156, h_sheet_trade_money, 1, "sheet_trade_money");
    emu_hook(0x3fa9, 0x0508, h_treasure_pool, 1, "treasure_pool");
    emu_hook(0x3fa9, 0x0203, h_add_platinum, 1, "add_platinum");
    emu_hook(0x3fa9, 0x19b2, h_appraise, 1, "appraise");
    emu_hook(0x2090, 0x0013, h_temple_anyway, 1, "temple_anyway");
    emu_hook(0x2090, 0x00bf, h_temple_pay, 1, "temple_pay");
    emu_hook(0x2090, 0x0249, h_temple_cure_blindness, 1, "temple_cure_blindness");
    emu_hook(0x2090, 0x02ed, h_temple_cure_disease, 1, "temple_cure_disease");
    emu_hook(0x2090, 0x0401, h_temple_cure_wounds, 1, "temple_cure_wounds");
    emu_hook(0x2090, 0x0510, h_temple_raise_dead, 1, "temple_raise_dead");
    emu_hook(0x2090, 0x0751, h_temple_neutralize_poison, 1, "temple_neutralize_poison");
    emu_hook(0x2090, 0x083a, h_temple_remove_curse, 1, "temple_remove_curse");
    emu_hook(0x2090, 0x0928, h_temple_stone_to_flesh, 1, "temple_stone_to_flesh");
    emu_hook(0x2090, 0x09d7, h_temple_heal, 1, "temple_heal");
    emu_hook(0x2090, 0x0d09, h_temple_screen, 1, "temple_screen");
    emu_hook(0x22ec, 0x0030, h_shop_list, 1, "shop_list");
    emu_hook(0x22ec, 0x0239, h_shop_give, 1, "shop_give");
    emu_hook(0x22ec, 0x0361, h_shop_buy, 1, "shop_buy");
    emu_hook(0x22ec, 0x053e, h_shop_screen, 1, "shop_screen");
    emu_hook(0x3941, 0x0ec4, h_monster_load_rec, 1, "monster_load_rec");
    emu_hook(0x3941, 0x1262, h_npc_add, 1, "npc_add");
    emu_hook(0x2366, 0x0411, h_monster_load, 1, "monster_load");
    emu_hook(0x2366, 0x1cc3, h_coins_scale, 1, "coins_scale");
    emu_hook(0x1c88, 0x0627, h_intro, 1, "intro");
    emu_hook(0x2366, 0x1fb1, h_party_measures, 1, "party_measures");
    emu_hook(0x2366, 0x1815, h_ecl_compare_strings, 1, "ecl_compare_strings");
    emu_hook(0x3941, 0x22d5, h_need_file, 1, "need_file");
    emu_hook(0x4c2b, 0x04fc, h_figure_draw, 1, "figure_draw");
    emu_hook(0x18ae, 0x00dc, h_sound_play, 1, "sound_play");
    emu_hook(0x18ae, 0x0000, h_sound_clock, 1, "sound_clock");
    emu_hook(0x18ae, 0x006c, h_sound_wait, 1, "sound_wait");
    emu_hook(0x18ae, 0x1742, h_sound_start_pc, 1, "sound_start_pc");
    emu_hook(0x18ae, 0x17ae, h_sound_start_tandy, 1, "sound_start_tandy");
    emu_hook(0x18ae, 0x181a, h_sound_select, 1, "sound_select");
    emu_hook(0x18ae, 0x017b, h_sound_resume, 1, "sound_resume");
    emu_hook(0x18ae, 0x01a5, h_sound_music, 1, "sound_music");
    emu_hook(0x18ae, 0x137c, h_sound_timer_on, 1, "sound_timer_on");
    emu_hook(0x1af8, 0x023f, h_getmem, 1, "rtl_getmem");
    emu_hook(0x1af8, 0x0254, h_freemem, 1, "rtl_freemem");
    emu_hook(0x1af8, 0x11c0, h_random, 1, "rtl_random");
    emu_hook(0x1af8, 0x0b0b, h_str_load, 1, "rtl_str_load");
    emu_hook(0x1af8, 0x0b25, h_str_store, 1, "rtl_str_store");
    emu_hook(0x1af8, 0x0b57, h_str_copy, 1, "rtl_str_copy");
    emu_hook(0x1af8, 0x0b98, h_str_concat, 1, "rtl_str_concat");
    emu_hook(0x1af8, 0x0c10, h_str_compare, 1, "rtl_str_compare");
    emu_hook(0x1af8, 0x0c3b, h_char_str, 1, "rtl_char_str");
    emu_hook(0x1af8, 0x1685, h_move, 1, "rtl_move");
    emu_hook(0x1af8, 0x16a8, h_fill, 1, "rtl_fill");
    emu_hook(0x1af8, 0x1247, h_randomize, 1, "rtl_randomize");
    emu_hook(0x18ae, 0x13b1, h_sound_timer_off, 1, "sound_timer_off");
    // Every unit's initialisation part is empty (PUSH BP / MOV BP,SP / POP BP / RETF); the program start calls them.
    emu_hook(0x1464, 0x2a4a, h_unit_init, 1, "unit_init");
    emu_hook(0x10ba, 0x309e, h_unit_init, 1, "unit_init");
    emu_hook(0x13d5, 0x0050, h_unit_init, 1, "unit_init");
    emu_hook(0x13e9, 0x0040, h_unit_init, 1, "unit_init");
    emu_hook(0x13f1, 0x0367, h_unit_init, 1, "unit_init");

    emu_hook(0x1709, 0x0f82, h_unit_init, 1, "unit_init");
    emu_hook(0x1802, 0x0968, h_unit_init, 1, "unit_init");
    emu_hook(0x1899, 0x013c, h_unit_init, 1, "unit_init");
    emu_hook(0x18ae, 0x01d2, h_unit_init, 1, "unit_init");
    emu_hook(0x18ae, 0x1830, h_unit_init, 1, "unit_init");
    emu_hook(0x1c88, 0x0809, h_unit_init, 1, "unit_init");
    emu_hook(0x1d09, 0x0359, h_unit_init, 1, "unit_init");
    emu_hook(0x1d3f, 0x3508, h_unit_init, 1, "unit_init");
    emu_hook(0x2090, 0x0f32, h_unit_init, 1, "unit_init");
    emu_hook(0x2184, 0x1671, h_unit_init, 1, "unit_init");
    emu_hook(0x22ec, 0x0798, h_unit_init, 1, "unit_init");
    emu_hook(0x2366, 0x2215, h_unit_init, 1, "unit_init");
    emu_hook(0x2588, 0x127a, h_unit_init, 1, "unit_init");
    emu_hook(0x26b0, 0x1922, h_unit_init, 1, "unit_init");
    emu_hook(0x2843, 0x1f8d, h_unit_init, 1, "unit_init");
    emu_hook(0x2a3d, 0x0615, h_unit_init, 1, "unit_init");
    emu_hook(0x2a9f, 0x38a9, h_unit_init, 1, "unit_init");
    emu_hook(0x2e2a, 0x3c6d, h_unit_init, 1, "unit_init");
    emu_hook(0x31f2, 0x10a7, h_unit_init, 1, "unit_init");
    emu_hook(0x32fd, 0x1fd9, h_unit_init, 1, "unit_init");
    emu_hook(0x34fb, 0x4454, h_unit_init, 1, "unit_init");
    emu_hook(0x3941, 0x23e0, h_unit_init, 1, "unit_init");
    emu_hook(0x3b80, 0x08f6, h_unit_init, 1, "unit_init");
    emu_hook(0x3c10, 0x2b0d, h_unit_init, 1, "unit_init");
    emu_hook(0x3ec2, 0x0e60, h_unit_init, 1, "unit_init");
    emu_hook(0x3fa9, 0x2256, h_unit_init, 1, "unit_init");
    emu_hook(0x41cf, 0x37b0, h_unit_init, 1, "unit_init");
    emu_hook(0x454b, 0x045e, h_unit_init, 1, "unit_init");
    emu_hook(0x4592, 0x1932, h_unit_init, 1, "unit_init");
    emu_hook(0x4726, 0x1245, h_unit_init, 1, "unit_init");
    emu_hook(0x484b, 0x02a0, h_unit_init, 1, "unit_init");
    emu_hook(0x4876, 0x0283, h_unit_init, 1, "unit_init");
    emu_hook(0x489f, 0x07c0, h_unit_init, 1, "unit_init");
    emu_hook(0x491c, 0x1206, h_unit_init, 1, "unit_init");
    emu_hook(0x4a3d, 0x0bd9, h_unit_init, 1, "unit_init");
    emu_hook(0x4afb, 0x12fb, h_unit_init, 1, "unit_init");
    emu_hook(0x4c2b, 0x0640, h_unit_init, 1, "unit_init");
    emu_hook(0x4c90, 0x00f7, h_unit_init, 1, "unit_init");
    emu_hook(0x4ca0, 0x02ad, h_unit_init, 1, "unit_init");

    emu_hook(0x3fa9, 0x1010, h_item_make, 1, "item_make");
    emu_hook(0x3b80, 0x0006, h_anim_play, 1, "anim_play");
    emu_hook(0x3b80, 0x02a4, h_finale, 1, "finale");
    emu_hook(0x3b80, 0x06b3, h_map_reveal, 1, "map_reveal");
    emu_hook(0x4c90, 0x0004, h_debug_numbers, 1, "debug_numbers");
    emu_hook(0x2a9f, 0x2122, h_energy_drain, 1, "energy_drain");
    emu_hook(0x2843, 0x02ba, h_bf_floor_west, 1, "bf_floor_west");
    emu_hook(0x2843, 0x0377, h_bf_north, 1, "bf_north");
    emu_hook(0x2843, 0x040f, h_bf_corner_nw, 1, "bf_corner_nw");
    emu_hook(0x2843, 0x0615, h_bf_corner_ne, 1, "bf_corner_ne");
    emu_hook(0x2366, 0x1a6f, h_duel_setup, 1, "duel_setup");
    emu_hook(0x2366, 0x1df0, h_items_lose_chance, 1, "items_lose_chance");
    emu_hook(0x2366, 0x20cc, h_script_damage, 1, "script_damage");
    emu_hook(0x3c10, 0x1a8a, h_item_use, 1, "item_use");
    emu_hook(0x2366, 0x0482, h_dist_ahead, 1, "dist_ahead");
    emu_hook(0x2366, 0x19db, h_step_forward, 1, "step_forward");
    emu_hook(0x41cf, 0x328a, h_scroll_spell_used, 1, "scroll_spell_used");
    emu_hook(0x10ba, 0x2b88, h_combat_prep_view, 1, "combat_prep_view");
    emu_hook(0x2e2a, 0x2879, h_enemy_strength, 1, "enemy_strength");
    if (getenv("POOLRAD_WHEEL")) emu_hook(0x1d09, 0x00fd, h_code_wheel, 1, "code_wheel");   // otherwise skipped (below)
    emu_hook(0x142b, 0x00a4, h_config_read, 1, "config_read");
    emu_hook(0x1802, 0x0212, h_overlay_disk_wait, 1, "overlay_disk_wait");
    emu_hook(0x34fb, 0x42ed, h_pay_coins, 1, "pay_coins");
    emu_hook(0x2184, 0x0b14, h_loot_list, 1, "loot_list");
    emu_hook(0x3fa9, 0x0000, h_chr_max_load, 1, "chr_max_load");
    emu_hook(0x3fa9, 0x0020, h_chr_load_sub, 1, "chr_load_sub");
    emu_hook(0x3fa9, 0x003a, h_chr_load_add, 1, "chr_load_add");
    emu_hook(0x3fa9, 0x0054, h_chr_too_heavy, 1, "chr_too_heavy");
    emu_hook(0x3fa9, 0x0a01, h_chr_drop_coins, 1, "chr_drop_coins");
    emu_hook(0x3fa9, 0x0fd2, h_roll_one_two_three, 1, "roll_one_two_three");
    emu_hook(0x3941, 0x0482, h_chr_build_icon, 1, "chr_build_icon");
    emu_hook(0x3941, 0x032f, h_pic_overlay, 1, "pic_overlay");
    emu_hook(0x454b, 0x0255, h_chr_saving_throws, 1, "chr_saving_throws");
    emu_hook(0x3941, 0x1446, h_chr_join_party, 1, "chr_join_party");
    emu_hook(0x2184, 0x0c06, h_combat_take_items, 1, "combat_take_items");
    emu_hook(0x3fa9, 0x0f7e, h_treasure_left, 1, "treasure_left");
    emu_hook(0x2184, 0x0d38, h_combat_take, 1, "combat_take");
    emu_hook(0x2184, 0x0ecd, h_combat_treasure_screen, 1, "combat_treasure_screen");
    emu_hook(0x2184, 0x0000, h_combat_treasure_xp, 1, "combat_treasure_xp");
    emu_hook(0x2843, 0x19f5, h_bf_place_all, 1, "bf_place_all");
    emu_hook(0x2843, 0x1403, h_bf_cell_outside, 1, "bf_cell_outside");
    emu_hook(0x454b, 0x0329, h_chr_thief_skills, 1, "chr_thief_skills");
    emu_hook(0x34fb, 0x0000, h_chr_free, 1, "chr_free");
    emu_hook(0x2184, 0x12e8, h_combat_allies_share, 1, "combat_allies_share");
    emu_hook(0x2184, 0x0916, h_combat_result_screen, 1, "combat_result_screen");
    emu_hook(0x2184, 0x0355, h_combat_award_xp, 1, "combat_award_xp");
    emu_hook(0x2184, 0x11ac, h_combat_back_to_party, 1, "combat_back_to_party");
    emu_hook(0x2588, 0x10f6, h_fight_speed_menu, 1, "fight_speed_menu");
    emu_hook(0x2588, 0x0131, h_fight_next_actor, 1, "fight_next_actor");
    emu_hook(0x2588, 0x01f6, h_fight_take_turn, 1, "fight_take_turn");
    emu_hook(0x2588, 0x0880, h_fight_end_round, 1, "fight_end_round");
    emu_hook(0x2588, 0x0000, h_fight_cleanup, 1, "fight_cleanup");
    emu_hook(0x2588, 0x007a, h_fight_battle, 1, "fight_battle");
    emu_hook(0x2184, 0x04c9, h_combat_after_fight, 1, "combat_after_fight");
    emu_hook(0x2588, 0x0fff, h_fight_bandage, 1, "fight_bandage");
    emu_hook(0x2588, 0x122b, h_fight_set_quick, 1, "fight_set_quick");
    emu_hook(0x2588, 0x0e45, h_fight_done_menu, 1, "fight_done_menu");
    emu_hook(0x2588, 0x0d45, h_fight_bump_attack, 1, "fight_bump_attack");
    emu_hook(0x2588, 0x09df, h_fight_move_command, 1, "fight_move_command");
    emu_hook(0x2588, 0x0666, h_fight_combat_menu, 1, "fight_combat_menu");
    emu_hook(0x2588, 0x031a, h_fight_player_turn, 1, "fight_player_turn");
    emu_hook(0x2843, 0x1e31, h_bf_battle_begins, 1, "bf_battle_begins");
    emu_hook(0x2843, 0x12e1, h_bf_prepare_combatants, 1, "bf_prepare_combatants");
    emu_hook(0x2843, 0x1434, h_bf_try_place, 1, "bf_try_place");
    emu_hook(0x2843, 0x0d13, h_bf_outdoor_pairs, 1, "bf_outdoor_pairs");
    emu_hook(0x2843, 0x11b4, h_bf_build_outdoor, 1, "bf_build_outdoor");
    emu_hook(0x2843, 0x1244, h_bf_setup, 1, "bf_setup");
    emu_hook(0x2843, 0x0bde, h_bf_outdoor_road, 1, "bf_outdoor_road");
    emu_hook(0x2843, 0x0e9e, h_bf_outdoor_scatter, 1, "bf_outdoor_scatter");
    emu_hook(0x2843, 0x08a4, h_bf_outdoor_terrain, 1, "bf_outdoor_terrain");
    emu_hook(0x2843, 0x0000, h_bf_decorate, 1, "bf_decorate");
    emu_hook(0x2843, 0x0810, h_bf_build_indoor, 1, "bf_build_indoor");
    emu_hook(0x2a9f, 0x0cc3, h_fx_cloud_clears, 1, "fx_cloud_clears");
    emu_hook(0x2e2a, 0x2dfa, h_fight_manual_aim, 1, "fight_manual_aim");
    emu_hook(0x41cf, 0x1b11, h_spl_stinking_cloud, 1, "spl_stinking_cloud");
    emu_hook(0x41cf, 0x028d, h_spell_list_add, 1, "spell_list_add");
    emu_hook(0x41cf, 0x044e, h_spell_list_item, 1, "spell_list_item");
    emu_hook(0x41cf, 0x323d, h_item_is_scroll, 1, "item_is_scroll");
    emu_hook(0x41cf, 0x051c, h_spell_list_scrolls, 1, "spell_list_scrolls");
    emu_hook(0x4876, 0x0207, h_hud_explore_frames, 1, "hud_explore_frames");
    emu_hook(0x41cf, 0x30d9, h_fx_breathe_lightning, 1, "fx_breathe_lightning");
    emu_hook(0x2e2a, 0x1351, h_fight_find_undead, 1, "fight_find_undead");
    emu_hook(0x2e2a, 0x116a, h_fight_turn_undead_now, 1, "fight_turn_undead");
    emu_hook(0x2e2a, 0x2c3d, h_fight_target_command, 1, "fight_target_command");
    emu_hook(0x2e2a, 0x2aa0, h_fight_target_menu, 1, "fight_target_menu");
    emu_hook(0x2e2a, 0x3558, h_fight_target_cursor, 1, "fight_target_cursor");
    emu_hook(0x2e2a, 0x2998, h_fight_confirm_attack, 1, "fight_confirm_attack");
    emu_hook(0x10ba, 0x2f57, h_chr_level_capped, 1, "chr_level_capped");
    emu_hook(0x2e2a, 0x1e16, h_spell_pick_one, 1, "spell_pick_one");
    emu_hook(0x2e2a, 0x20c3, h_spell_pick_combat_targets, 1, "spell_pick_combat_targets");
    emu_hook(0x10ba, 0x1a29, h_combat_missile_anim, 1, "combat_missile_anim");
    emu_hook(0x41cf, 0x2385, h_spl_dispel, 1, "spl_dispel");
    emu_hook(0x41cf, 0x228a, h_spl_cure_disease_on, 1, "spl_cure_disease_on");
    emu_hook(0x41cf, 0x232f, h_spl_cure_disease, 1, "spl_cure_disease");
    emu_hook(0x41cf, 0x2c43, h_spl_restoration, 1, "spl_restoration");
    emu_hook(0x41cf, 0x206f, h_spl_animate_dead, 1, "spl_animate_dead");
    emu_hook(0x41cf, 0x0a91, h_spell_pick_targets, 1, "spell_pick_targets");
    emu_hook(0x10ba, 0x1feb, h_spell_sparkle, 1, "spell_sparkle");
    emu_hook(0x41cf, 0x1f41, h_spl_strength, 1, "spl_strength");
    emu_hook(0x41cf, 0x2540, h_spl_remove_curse, 1, "spl_remove_curse");
    emu_hook(0x41cf, 0x2892, h_spl_haste, 1, "spl_haste");
    emu_hook(0x41cf, 0x2c09, h_spl_slow, 1, "spl_slow");
    emu_hook(0x41cf, 0x185b, h_spl_slow_poison, 1, "spl_slow_poison");
    emu_hook(0x41cf, 0x190f, h_spl_snake_charm, 1, "spl_snake_charm");
    emu_hook(0x41cf, 0x2764, h_spl_speed, 1, "spl_speed");
    emu_hook(0x41cf, 0x2bb7, h_spl_lightning_bolt, 1, "spl_lightning_bolt");
    emu_hook(0x41cf, 0x28bc, h_spl_bolt_square, 1, "spl_bolt_square");
    emu_hook(0x41cf, 0x295b, h_spl_bolt_path, 1, "spl_bolt_path");
    emu_hook(0x41cf, 0x266b, h_spl_fireball, 1, "spl_fireball");
    emu_hook(0x41cf, 0x1658, h_spl_hold_person, 1, "spl_hold_person");
    emu_hook(0x41cf, 0x11dd, h_spl_charm, 1, "spl_charm");
    emu_hook(0x41cf, 0x2e02, h_spl_m_speed, 1, "spl_m_speed");
    emu_hook(0x41cf, 0x2e4d, h_spl_m_heal, 1, "spl_m_heal");
    emu_hook(0x41cf, 0x2ee5, h_spl_m_strength, 1, "spl_m_strength");
    emu_hook(0x41cf, 0x2f4d, h_spl_m_bolt, 1, "spl_m_bolt");
    emu_hook(0x41cf, 0x2f99, h_spl_m_paralyze, 1, "spl_m_paralyze");
    emu_hook(0x41cf, 0x2fd0, h_spl_m_cure, 1, "spl_m_cure");
    emu_hook(0x41cf, 0x302a, h_spl_m_invisible, 1, "spl_m_invisible");
    emu_hook(0x41cf, 0x3058, h_spl_m_damage, 1, "spl_m_damage");
    emu_hook(0x41cf, 0x3092, h_spl_m_none, 1, "spl_m_none");
    emu_hook(0x41cf, 0x30a2, h_spl_m_reading, 1, "spl_m_reading");

    emu_hook(0x41cf, 0x12a5, h_spl_enlarge, 1, "spl_enlarge");
    emu_hook(0x4592, 0x115c, h_raise_strength, 1, "rules_raise_strength");
    emu_hook(0x41cf, 0x151b, h_spl_sleep, 1, "spl_sleep");
    emu_hook(0x41cf, 0x106b, h_spl_cure_light, 1, "spl_cure_light");
    emu_hook(0x41cf, 0x136e, h_spl_reduce, 1, "spl_reduce");
    emu_hook(0x41cf, 0x13d8, h_spl_friends, 1, "spl_friends");
    emu_hook(0x41cf, 0x2217, h_spl_cure_blindness, 1, "spl_cure_blindness");
    emu_hook(0x4592, 0x1081, h_effect_cure, 1, "effect_cure");
    emu_hook(0x41cf, 0x100f, h_spl_bless, 1, "spl_bless");
    emu_hook(0x41cf, 0x1040, h_spl_curse, 1, "spl_curse");
    emu_hook(0x41cf, 0x10a7, h_spl_cause_light, 1, "spl_cause_light");
    emu_hook(0x41cf, 0x10e9, h_spl_detect, 1, "spl_detect");
    emu_hook(0x41cf, 0x1123, h_spl_protection, 1, "spl_protection");
    emu_hook(0x41cf, 0x1162, h_spl_resist_cold, 1, "spl_resist_cold");
    emu_hook(0x41cf, 0x1190, h_spl_burning_hands, 1, "spl_burning_hands");
    emu_hook(0x41cf, 0x1439, h_spl_magic_missile, 1, "spl_magic_missile");
    emu_hook(0x41cf, 0x1499, h_spl_shield, 1, "spl_shield");
    emu_hook(0x41cf, 0x14c7, h_spl_shocking_grasp, 1, "spl_shocking_grasp");
    emu_hook(0x41cf, 0x17e9, h_spl_resist_fire, 1, "spl_resist_fire");
    emu_hook(0x41cf, 0x1822, h_spl_silence, 1, "spl_silence");
    emu_hook(0x41cf, 0x1a16, h_spl_invisibility, 1, "spl_invisibility");
    emu_hook(0x41cf, 0x1a4f, h_spl_knock, 1, "spl_knock");
    emu_hook(0x41cf, 0x1acc, h_spl_enfeeble, 1, "spl_enfeeble");
    emu_hook(0x41cf, 0x225d, h_spl_cause_blindness, 1, "spl_cause_blindness");
    emu_hook(0x41cf, 0x2604, h_spl_bestow_curse, 1, "spl_bestow_curse");
    emu_hook(0x41cf, 0x263d, h_spl_blink, 1, "spl_blink");
    emu_hook(0x41cf, 0x234c, h_spl_cause_disease, 1, "spl_cause_disease");
    emu_hook(0x41cf, 0x19c3, h_spl_spiritual_hammer, 1, "spl_spiritual_hammer");
    emu_hook(0x41cf, 0x1a8a, h_spl_mirror_image, 1, "spl_mirror_image");
    emu_hook(0x41cf, 0x24d6, h_spl_prayer, 1, "spl_prayer");
    emu_hook(0x41cf, 0x0f4f, h_spell_affect_side, 1, "spell_affect_side");
    emu_hook(0x41cf, 0x07d6, h_spell_duration, 1, "spell_duration");
    emu_hook(0x41cf, 0x08c3, h_spell_affect_targets, 1, "spell_affect_targets");
    emu_hook(0x41cf, 0x0c20, h_spell_cast, 1, "spell_cast");
    emu_hook(0x41cf, 0x331a, h_spell_announce, 1, "spell_announce");
    emu_hook(0x2e2a, 0x1bf8, h_fight_heal_target, 1, "fight_heal_target");
    emu_hook(0x2e2a, 0x26b0, h_fight_missile_flight, 1, "fight_missile_flight");
    emu_hook(0x10ba, 0x1892, h_combat_missile_frame, 1, "combat_missile_frame");
    emu_hook(0x10ba, 0x19de, h_combat_missile_spin, 1, "combat_missile_spin");
    emu_hook(0x2e2a, 0x2414, h_fight_cast_command, 1, "fight_cast_command");
    emu_hook(0x3c10, 0x14d0, h_item_ready_toggle, 1, "item_ready_toggle");
    emu_hook(0x10ba, 0x09e3, h_hud_combat_panel, 1, "hud_combat_panel");
    emu_hook(0x26b0, 0x000f, h_fight_ai_decide, 1, "fight_ai_decide");
    emu_hook(0x26b0, 0x0b3c, h_fight_ai_turn, 1, "fight_ai_turn");
    emu_hook(0x26b0, 0x13ca, h_fight_ai_arm, 1, "fight_ai_arm");
    emu_hook(0x2e2a, 0x0e82, h_fight_sweep, 1, "fight_sweep");
    emu_hook(0x10ba, 0x2e8b, h_chr_missile_ammo, 1, "chr_missile_ammo");
    emu_hook(0x26b0, 0x07f2, h_fight_ai_move, 1, "fight_ai_move");
    emu_hook(0x2e2a, 0x08b2, h_fight_free_attacks, 1, "fight_free_attacks");
    emu_hook(0x4592, 0x0ae7, h_fight_cloud_check, 1, "fight_cloud_check");
    emu_hook(0x2e2a, 0x37e4, h_fight_pick_target, 1, "fight_pick_target");
    emu_hook(0x2e2a, 0x0712, h_fight_step, 1, "fight_step");
    emu_hook(0x2e2a, 0x0c6a, h_fight_escape, 1, "fight_escape");
    emu_hook(0x32fd, 0x1e6f, h_camp_menu, 1, "camp_menu");
    emu_hook(0x3c10, 0x0901, h_sheet_ability, 1, "sheet_draw_ability");
    emu_hook(0x3c10, 0x050d, h_sheet_money, 1, "sheet_draw_money");
    emu_hook(0x3c10, 0x0608, h_sheet_combat, 1, "sheet_draw_combat_stats");
    emu_hook(0x3c10, 0x004a, h_sheet_draw, 1, "sheet_draw");
    emu_hook(0x3c10, 0x27df, h_select_move, 1, "party_select_move");
    emu_hook(0x3c10, 0x24a7, h_sheet_drop_money, 1, "sheet_drop_money");
    emu_hook(0x3c10, 0x299c, h_sheet_spell_list, 1, "sheet_spell_list");
    emu_hook(0x41cf, 0x059c, h_spell_list_build, 1, "spell_list_build");
    emu_hook(0x41cf, 0x008a, h_spell_list_choose, 1, "spell_list_choose");
    emu_hook(0x3c10, 0x0ab3, h_sheet_view, 1, "sheet_view");
    emu_hook(0x3ec2, 0x05f0, h_rest_show_time, 1, "rest_show_time");
    emu_hook(0x3ec2, 0x03a3, h_rest_normalise, 1, "rest_normalise");
    emu_hook(0x3ec2, 0x047a, h_rest_subtract, 1, "rest_subtract");
    emu_hook(0x3ec2, 0x0721, h_rest_menu, 1, "rest_menu");
    emu_hook(0x3ec2, 0x0871, h_rest_healing, 1, "rest_daily_healing");
    emu_hook(0x3ec2, 0x0b0a, h_rest_study, 1, "rest_study");
    emu_hook(0x3ec2, 0x0bb2, h_rest_sleep_hour, 1, "rest_sleep_hour");
    emu_hook(0x3ec2, 0x0925, h_rest_memorise, 1, "rest_memorise_next");
    emu_hook(0x3ec2, 0x09e7, h_rest_scribe, 1, "rest_scribe_next");
    emu_hook(0x3ec2, 0x0c9a, h_rest_screen, 1, "rest_screen");
    // The start-up code wheel question (1D09:00FD). POOLRAD_WHEEL=1 keeps it.
    if (!getenv("POOLRAD_WHEEL")) emu_hook(0x1d09, 0x00fd, h_skip_code_wheel, 1, "skip_code_wheel");
    rtl_dos_hooks();
    rtl_file_hooks();
    rtl_text_hooks();
    rtl_real_hooks();
    rtl_sys_hooks();
    rtl_ovr_hooks();
    rtl_crt_hooks();
    sound_isr_hooks();
    main_hooks();
    emu_hook(0x1464, 0x2a0d, h_palette_reg, 1, "gfx_palette_reg");
    emu_hook(0x2e2a, 0x3b93, h_sting_kill, 1, "fight_sting_kill");
    emu_hook(0x1af8, 0x16bc, h_paramstr, 1, "rtl_paramstr");
    emu_hook(0x4726, 0x0000, h_list_nth, 1, "list_nth");
}
