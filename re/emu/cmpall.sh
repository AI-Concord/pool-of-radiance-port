#!/bin/sh
# Runs every comparison script (original vs hybrid build).
D=$(cd "$(dirname "$0")" && pwd)
for t in t_wheel t_lists t_items t_items2 t_items3 t_shop t_trademoney t_walk t_combat t_create2 t_create3 t_icon t_save t_take t_share t_pool t_hoard2 t_temple t_shop2 t_itemgen t_finale t_reveal t_drain t_duel t_loseitems t_scriptdmg t_useitem t_paycoins t_loot t_npc t_numin t_gfxfn t_rob t_damage t_checkparty t_treasure t_mspells t_powers t_gift t_bios t_quit t_strfn t_add t_dropc t_modify t_train t_phlan t_enc_wait t_enc_flee t_enc_parlay t_clerk t_pick t_view t_rest t_rest2 t_rest3 t_memo t_scribe t_fake1 t_fake2 t_fake3 t_alter t_alter2 t_drop t_fake4 t_fake5 t_combat2 t_combat3 t_combat4 t_combat5 t_combat6 t_combat7 t_edit t_ready t_spell t_clock; do "$D/cmp.sh" "$D/$t.txt"; done
# Tests that start from snapshots (re/emu/snaps, not kept in git): rebuilt from their base scripts when missing.
for pair in "fight1:s_base:s_fight" "fx0:s_basefx:s_fight0" "fx1:s_basefx:s_fight" "mon0:s_basemon:s_mon" "fight1:s_base:s_morale" "fight1:s_base:s_escape" "fight1:s_base:s_cloud" "fight0:s_base:s_heal" "fight0:s_base:s_spells" "fight0:s_base:s_sleep" "fight0:s_base:s_charm" "fight0:s_base:s_hold" "fight0:s_base:s_fireball" "fight0:s_base:s_lightning" "fight0:s_base:s_haste" "fight0:s_base:s_magic" "fight0:s_base:s_mspells" "fight0:s_base:s_mspells2" "fight0:s_base:s_fx2" "fight0:s_base:s_fx3" "fight0:s_base:s_fx4" "fight0:s_base:s_sting"; do
  snap=${pair%%:*}; rest=${pair#*:}; base=${rest%%:*}; script=${rest#*:}
  [ -f "$D/snaps/$snap.snap" ] || "$D/mksnap.sh" "$D/$base.txt" >/dev/null
  "$D/cmps.sh" "$snap" "$D/$script.txt"
done

