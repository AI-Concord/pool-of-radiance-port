# Pool of Radiance for Mac

A macOS (Apple Silicon, Metal) build of SSI's 1988 Gold Box game, made from the DOS release
(version 1.3) by reverse engineering `START.EXE` and `GAME.OVR`.

**Full documentation: [docs/index.html](docs/index.html)** (open it in a browser).

## Status

Native. Every routine of the original program that runs is hand-translated C (`port/`), and the
app is built without an 8086 interpreter: the game's data keeps its DOS memory layout and a small
platform layer answers its BIOS and DOS calls (files, keyboard, clock, EGA, timer). The reference
emulator (`re/emu/emu.c`, with its interpreter) still runs the untouched original for the test
suite, which compares the two builds frame for frame and byte for byte. See `docs/index.html`
("The native build", "Bugs found").

The apps fix the original's bugs that affect play (Debug > Keep the 1988 Bugs turns that off); the
headless tests keep them off unless `POOLRAD_FIXES=1`. The Windows build runs on Windows 11. Both apps offer
View > Smooth Picture (xBR-style edge smoothing, the default) or Sharp Pixels.

## Run and build

    open "build/Pool of Radiance.app"
    app/build.sh                      # needs only the Xcode command line tools
    app/build_windows.sh              # Windows 10/11 (Direct3D 12) build, cross-compiled; brew install mingw-w64

Saves go to `~/Library/Application Support/Pool of Radiance/` in the DOS formats. The code wheel
question is skipped unless `POOLRAD_WHEEL=1` is set.

Each session is recorded for bug reports in `~/Library/Application Support/Pool of Radiance Recordings/`
(the saves at launch, every key with its timing, the clock readings, a screenshot per battle round; newest 20 kept).
Debug > Mark a Problem (Cmd-B) notes the moment. `re/emu/replay.sh <recording>` replays it through the original and
the port and shows the first place they differ.

## Working on the port

    cd re/emu && make                 # reference emulator
    ./cmp.sh t_walk.txt               # original vs hybrid build: frames, memory, save files
    ./cmpall.sh                       # all thirty-three comparison scripts
    python3 ../fn.py 1464:1951 both   # one routine's decompilation and disassembly
    python3 ../prof.py out4/prof.tsv  # most-called routines of a profiled run (emu -P)
    python3 ../mapdump.py dump.mem    # ASCII drawing of the level in a memory dump
    python3 ../route.py dump.mem X Y  # key presses that walk the party to a square
    python3 ../dax.py pics FILE.DAX d # extract picture blocks from a DAX archive as PNG
    python3 ../ecl.py ECL2.DAX 20     # disassemble an adventure script block (all are in re/out/ecl/)

Regenerating the analysis inputs:

    cd re
    python3 unexepack.py ../original/START.EXE out/START_UNPACKED.EXE
    python3 flatten.py out/START_UNPACKED.EXE ../original/GAME.OVR out/POOLFLAT.EXE out/flat_map.tsv
    # Ghidra (needs JAVA_HOME): analyzeHeadless ghidra/proj poolrad -import out/POOLFLAT.EXE \
    #   -processor "x86:LE:16:Real Mode" -scriptPath ghidra/scripts -postScript DumpAll.java ghidra/out

To port a routine: translate it into `port/`, register it in `port/hooks.c` at the original's
address (Ghidra segment, i.e. flat segment + 1000h), and run `cmp.sh` until nothing differs.

## Game files

This repository holds only this project's own work. The original game (SSI's Pool of Radiance, 1988) is not
included: no game files, no decompilation or disassembly of its code, no extracted pictures or sounds. To build or
test, put the data files from your own copy of the game (the original DOS release) in `original/` (they are read from there by `app/build.sh`, the
Windows build and the emulator). `tools_export_public.sh` makes the published copy from the full working repository.

## Credits

**Pool of Radiance** (1988) was developed and published by Strategic Simulations, Inc. (SSI), the first of its
"Gold Box" Advanced Dungeons & Dragons games, made under license from TSR, Inc. Advanced Dungeons & Dragons, Forgotten
Realms and related marks belong to Wizards of the Coast. The game, its design, text, art, music and code belong to
their creators and rights holders. All credit for the game itself goes to them.

This native port to macOS (Metal) and Windows (Direct3D 12) was made by Claude, an AI model by Anthropic, working
with Nick Behnken. The port is an unofficial fan project, not affiliated with or endorsed by SSI, TSR, Wizards of the
Coast or any other rights holder. The port is fully native: it needs no DOS and no emulator. It does use the original game's data files
(pictures, maps, fonts, text), which are not included here; copy them from your own copy of the game into
`original/` before building.
