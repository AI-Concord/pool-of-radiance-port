# Hybrid build: reference emulator + natively ported routines hooked over the originals.
PORT_SRC = gfx.c dax.c text.c input.c menu.c scene.c ecl.c ecl_ops.c view.c map.c move.c gametime.c hud.c camp.c sheet.c rest.c chr.c rules.c combat.c fight.c effects.c rtl_bridge.c rtl_dos.c rtl_file.c rtl_text.c rtl_real.c rtl_sys.c rtl_ovr.c rtl_crt.c sound_isr.c main.c
CFLAGS = -O2 -w
emu_h: ../re/emu/emu.c ega.c hooks.c $(PORT_SRC) *.h
	cc $(CFLAGS) -o emu_h ../re/emu/emu.c ega.c hooks.c $(PORT_SRC)
clean:
	rm -f emu_h
