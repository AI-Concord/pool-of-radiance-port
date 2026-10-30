#!/bin/sh
# usage: replay.sh <recording folder> [orig|port]
# Plays a session the app recorded (Debug menu; ~/Library/Application Support/Pool of Radiance Recordings/...)
# through the untouched original and through the port, starting from the saved games as they were at launch, with
# the same keys at the same keyboard polls and the same clock readings. Frames are kept at every battle round and
# player mark (r_orig/, r_port/, also as PNG) with a memory dump at each mark, and compared like cmp.sh: the first
# frame or dump that differs shows where the port went wrong. With "orig" or "port" only that build is run.
# The port should be the build the recording names (keys.txt line 1); check it out first if it is older.
D=$(cd "$(dirname "$0")" && pwd); P=$D/../../port; R=${1:?usage: replay.sh <recording folder> [orig|port]}
[ -f "$R/keys.txt" ] && [ -f "$R/clock.txt" ] || { echo "no keys.txt/clock.txt in $R"; exit 1; }
head -1 "$R/keys.txt"
(cd "$P" && make -s) || exit 1
# The app skips the code wheel question unless POOLRAD_WHEEL was set; the original is then made to skip it too.
if head -1 "$R/keys.txt" | grep -q "code wheel asked"; then export POOLRAD_WHEEL=1; else export POOLRAD_SKIP_WHEEL=1; fi
# The app fixes the original's bugs unless told not to (docs: Bugs found); the port replays with the same setting.
# Where a fix comes into play the original then legitimately differs from the port, and from that point on the
# comparison shows the effect of the fix rather than a fault in the port.
head -1 "$R/keys.txt" | grep -q "fixes on" && export POOLRAD_FIXES=1
[ -d "$D/runf" ] || { cp -R "$D/../../original" "$D/runf" && cp "$D/../out/POOLFLAT.EXE" "$D/runf/"; }
run() {   # run <name> <emu flags...>
  n=$1; shift
  rm -rf "${D:?}/r_$n" "${D:?}/r_$n.run"; mkdir -p "$D/r_$n"; cp -R "$D/runf" "$D/r_$n.run"
  rm -rf "${D:?}/r_$n.run/SAVE"; mkdir -p "$D/r_$n.run/SAVE"; cp "$R"/start-saves/* "$D/r_$n.run/SAVE/" 2>/dev/null
  POOLRAD_ARROWS=1 "$P/emu_h" "$@" -d "$D/r_$n.run" -S "$D/r_$n.run/SAVE" -o "$D/r_$n" -s "$R/keys.txt" -C "$R/clock.txt" \
    -n 900000000000 "$D/r_$n.run/POOLFLAT.EXE" 2>"$D/r_$n/log.txt"
  tail -1 "$D/r_$n/log.txt"
  ls "$D"/r_$n/*.ppm >/dev/null 2>&1 && python3 -I "$D/ppm2png.py" "$D"/r_$n/*.ppm
}
[ "$2" != port ] && run orig -H
[ "$2" != orig ] && run port
[ -n "$2" ] && exit 0
bad=0; n=0; first=""
for f in $(ls "$D"/r_orig/*.ppm 2>/dev/null | sort); do b=$(basename "$f"); n=$((n+1))
  if ! cmp -s "$f" "$D/r_port/$b"; then bad=$((bad+1)); [ -z "$first" ] && first=$b; fi; done
mb=0; mn=0
for f in "$D"/r_orig/*.mem; do [ -f "$f" ] || continue; mn=$((mn+1)); python3 -I "$D/memdiff.py" "$f" "$D/r_port/$(basename "$f")" || mb=$((mb+1)); done
echo "replay: $n frames compared, $bad differ${first:+ (first $first)}; $mn memory dumps, $mb differ"
