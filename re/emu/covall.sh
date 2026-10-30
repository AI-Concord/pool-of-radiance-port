#!/bin/sh
# usage: covall.sh outdir -> plays every cmpall.sh test (snapshot fights too) through the port with instruction
# coverage on (emu -V), four at a time, and lists every routine whose ORIGINAL code still ran, however it was
# reached (from original code or from C through call_original). That list is what a build without the 8086
# interpreter still has to translate.
D=$(cd "$(dirname "$0")" && pwd); R=$D/../..; O=${1:?outdir}; mkdir -p "$O"
(cd "$R/port" && make -s) || exit 1
one() {   # one <name> <script> [snapshot]
  n=$1; s=$2; snap=$3; w="$O/w_$n"; rm -rf "${w:?}"; cp -R "$D/runf" "$w"; mkdir -p "$w/out"; r=""
  if [ -n "$snap" ]; then rm -rf "${w:?}/SAVE"; cp -R "$D/snaps/$snap.save" "$w/SAVE"; r="-R $D/snaps/$snap.snap"
  elif [ -f "${s%.txt}.save.py" ]; then python3 "${s%.txt}.save.py" "$w/SAVE"; fi
  ( E=${s%.txt}.env; [ -f "$E" ] && { set -a; . "$E"; set +a; }
    POOLRAD_WHEEL=1 "$R/port/emu_h" -V "$O/$n.cov" $r -d "$w" -S "$w/SAVE" -o "$w/out" -s "$s" -n 9000000000 "$w/POOLFLAT.EXE" 2>/dev/null )
  rm -rf "${w:?}"
}
jobs=""
for t in $(grep -o 't_[a-z0-9_]*' "$D/cmpall.sh" | sort -u); do [ -f "$D/$t.txt" ] && jobs="$jobs $t:$D/$t.txt:"; done
for pair in $(grep -o '"[a-z0-9]*:s_[a-z0-9]*:s_[a-z0-9]*"' "$D/cmpall.sh" | tr -d '"'); do
  snap=${pair%%:*}; s=${pair##*:}; jobs="$jobs $snap-$s:$D/$s.txt:$snap"; done
for j in $jobs; do echo "$j"; done | { while IFS=: read n s snap; do
  one "$n" "$s" "$snap" &
  while [ "$(jobs -r | wc -l)" -ge 4 ]; do sleep 1; done
done; wait; }
python3 -I "$R/re/covreport.py" "$R" "$O"/t_*.cov    # the snapshot fights resume inside original routines, so they are left out
