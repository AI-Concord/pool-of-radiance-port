#!/bin/sh
# usage: cmps.sh NAME script.txt   -> like cmp.sh, but both builds start from the snapshot re/emu/snaps/NAME.snap
# instead of from the title screen, so a test of one situation takes seconds. The script supplies only the
# keys (and memory edits) from that moment on.
D=$(cd "$(dirname "$0")" && pwd); P=$D/../../port
(cd "$P" && make -s) || exit 1
python3 -I "$D/../checkhooks.py" >/dev/null || { python3 -I "$D/../checkhooks.py"; exit 1; }
[ -f "$D/snaps/$1.snap" ] || { echo "no snapshot $1 (make it with mksnap.sh)"; exit 1; }
rm -rf "$D/c_orig" "$D/c_port" "$D/c_orig.run" "$D/c_port.run"; mkdir -p "$D/c_orig" "$D/c_port"
for r in c_orig.run c_port.run; do cp -R "$D/runf" "$D/$r"; rm -rf "$D/$r/SAVE"; cp -R "$D/snaps/$1.save" "$D/$r/SAVE"; done
"$P/emu_h" -H -R "$D/snaps/$1.snap" -d "$D/c_orig.run" -S "$D/c_orig.run/SAVE" -o "$D/c_orig" -s "$2" -n 9000000000 "$D/c_orig.run/POOLFLAT.EXE" 2>"$D/c_orig/log.txt"
POOLRAD_WHEEL=1 HOOKSTATS=1 "$P/emu_h" -R "$D/snaps/$1.snap" -d "$D/c_port.run" -S "$D/c_port.run/SAVE" -o "$D/c_port" -s "$2" -n 9000000000 "$D/c_port.run/POOLFLAT.EXE" 2>"$D/c_port/log.txt"
bad=0; n=0; first=""
for f in "$D"/c_orig/*.ppm; do b=$(basename "$f"); n=$((n+1))
  if ! cmp -s "$f" "$D/c_port/$b"; then bad=$((bad+1)); [ -z "$first" ] && first=$b; fi; done
mb=0; mn=0
for f in "$D"/c_orig/*.mem; do [ -f "$f" ] || continue; mn=$((mn+1)); python3 -I "$D/memdiff.py" "$f" "$D/c_port/$(basename "$f")" || mb=$((mb+1)); done
sb=0; for f in "$D"/c_orig.run/SAVE/*; do cmp -s "$f" "$D/c_port.run/SAVE/$(basename "$f")" || sb=$((sb+1)); done
grep -h "DECLINED" "$D/c_port/log.txt"
echo "$1 + $(basename "$2"): $n frames compared, $bad differ${first:+ (first $first)}; $mn memory dumps, $mb differ; $sb save files differ"
