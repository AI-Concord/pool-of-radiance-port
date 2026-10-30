#!/bin/sh
# Copies this project's own work, without any of the original game (no game files, no decompilation or
# disassembly of its code, no extracted pictures, sounds or snapshots, no icon made from its art), into
# ~/github-export/pool-of-radiance-port, the folder that is published. Run, then commit and push there.
set -e
SRC=$(cd "$(dirname "$0")" && pwd); DST=$HOME/github-export/pool-of-radiance-port
mkdir -p "$DST"
find "$DST" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cd "$SRC"
git ls-files | grep -v -E '^(original/|re/out/|re/ghidra/out/|docs/gallery/|docs/audio/|app/Resources/)' \
  | grep -v -E '^re/emu/(run[^/]*|snaps|s_run|s_out|c_orig[^/]*|c_port[^/]*|r_orig[^/]*|r_port[^/]*|out[^/]*)/' \
  | grep -v -E '^(docs/img/wheel\.png|docs/gallery\.html|app/Windows/PoolOfRadiance\.ico|port/emu_h|re/emu/libtest)$' \
  | grep -v -E '\.(ppm|mem|snap|SAV|ITM|SPC|DAX|EXE|OVR|CFG)$' \
  | while IFS= read -r f; do mkdir -p "$DST/$(dirname "$f")"; cp -p "$f" "$DST/$f"; done
echo "exported $(cd "$DST" && find . -type f ! -path './.git/*' | wc -l | tr -d ' ') files to $DST"
