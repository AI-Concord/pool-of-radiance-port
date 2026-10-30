#!/bin/sh
# usage: cmp.sh script -> plays the script through the untouched original and through the hybrid build
# (original + natively ported routines) and compares every captured frame and memory dump.
D=$(cd "$(dirname "$0")" && pwd); P=$D/../../port
(cd "$P" && make -s) || exit 1
python3 -I "$D/../checkhooks.py" >/dev/null || { python3 -I "$D/../checkhooks.py"; exit 1; }      # hook stack cleanup must match the originals
[ -d "$D/runf" ] || { cp -R "$D/../../original" "$D/runf" && cp "$D/../out/POOLFLAT.EXE" "$D/runf/"; }   # scratch copy of the game
rm -rf "$D/c_orig" "$D/c_port" "$D/c_orig.run" "$D/c_port.run"; mkdir -p "$D/c_orig" "$D/c_port"
cp -R "$D/runf" "$D/c_orig.run"; cp -R "$D/runf" "$D/c_port.run"
E="${1%.txt}.env"; [ -f "$E" ] && { set -a; . "$E"; set +a; }   # optional per-test environment (e.g. POOLRAD_STING=1)
F="${1%.txt}.save.py"; [ -f "$F" ] && { python3 "$F" "$D/c_orig.run/SAVE" && python3 "$F" "$D/c_port.run/SAVE" || exit 1; }   # optional doctored save (re/mksave.py)
"$P/emu_h" -H -d "$D/c_orig.run" -S "$D/c_orig.run/SAVE" -o "$D/c_orig" -s "$1" -n 9000000000 "$D/c_orig.run/POOLFLAT.EXE" 2>"$D/c_orig/log.txt"
POOLRAD_WHEEL=1 HOOKSTATS=1 "$P/emu_h" -d "$D/c_port.run" -S "$D/c_port.run/SAVE" -o "$D/c_port" -s "$1" -n 9000000000 "$D/c_port.run/POOLFLAT.EXE" 2>"$D/c_port/log.txt"
bad=0; n=0; first=""
for f in "$D"/c_orig/*.ppm; do b=$(basename "$f"); n=$((n+1))
  if ! cmp -s "$f" "$D/c_port/$b"; then bad=$((bad+1)); [ -z "$first" ] && first=$b; fi; done
mb=0; mn=0
for f in "$D"/c_orig/*.mem; do [ -f "$f" ] || continue; mn=$((mn+1)); python3 -I "$D/memdiff.py" "$f" "$D/c_port/$(basename "$f")" || mb=$((mb+1)); done
sb=0; for f in "$D"/c_orig.run/SAVE/*; do python3 -I "$D/savcmp.py" "$f" "$D/c_port.run/SAVE/$(basename "$f")" || sb=$((sb+1)); done
cb=0; grep "^\[call\]" "$D/c_orig/log.txt" > "$D/c_orig/calls.txt"; grep "^\[call\]" "$D/c_port/log.txt" > "$D/c_port/calls.txt"
cmp -s "$D/c_orig/calls.txt" "$D/c_port/calls.txt" || { cb=1; diff "$D/c_orig/calls.txt" "$D/c_port/calls.txt" | head -4; }   # results of script 'call's
grep -h "DECLINED" "$D/c_port/log.txt"
gi=$(grep -o "[0-9]* of them the game's own" "$D/c_port/log.txt" | tr -dc 0-9); [ "${gi:-0}" != 0 ] && echo "  note: $gi instructions of the game's code were interpreted in the port build"
echo "$(basename "$1"): $n frames compared, $bad differ${first:+ (first $first)}; $mn memory dumps, $mb differ; $sb save files differ$([ $cb = 1 ] && echo "; CALL RESULTS DIFFER")"
