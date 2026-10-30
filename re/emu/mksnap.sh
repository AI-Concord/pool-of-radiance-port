#!/bin/sh
# usage: mksnap.sh script.txt   -> plays the script through the untouched original and keeps the snapshots its
# "snap NAME" lines take, in re/emu/snaps/NAME.snap, each with the SAVE folder as it was for that run
# (NAME.save/). If script.save.py exists the save is doctored first, as cmp.sh does.
D=$(cd "$(dirname "$0")" && pwd); P=$D/../../port
(cd "$P" && make -s) || exit 1
[ -d "$D/runf" ] || { cp -R "$D/../../original" "$D/runf" && cp "$D/../out/POOLFLAT.EXE" "$D/runf/"; }
rm -rf "$D/s_run" "$D/s_out"; cp -R "$D/runf" "$D/s_run"; mkdir -p "$D/s_out" "$D/snaps"
F="${1%.txt}.save.py"; [ -f "$F" ] && { python3 "$F" "$D/s_run/SAVE" || exit 1; }
"$P/emu_h" -H -d "$D/s_run" -S "$D/s_run/SAVE" -o "$D/s_out" -s "$1" -n 9000000000 "$D/s_run/POOLFLAT.EXE" 2>&1 | grep -E "^\\[snap\\]|script ended|limit"
for f in "$D"/s_out/*.snap; do [ -f "$f" ] || continue; n=$(basename "$f" .snap); mv "$f" "$D/snaps/$n.snap"; rm -rf "$D/snaps/$n.save"; cp -R "$D/s_run/SAVE" "$D/snaps/$n.save"; echo "kept snaps/$n.snap"; done
