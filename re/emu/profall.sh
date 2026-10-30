#!/bin/sh
# usage: profall.sh outdir -> plays every cmpall.sh test through the hybrid with the call profile on (one .tsv per
# test) and lists the routines still original that the tests reach, largest first. Calls made from native code
# through call_original are not counted, only calls made by original code.
D=$(cd "$(dirname "$0")" && pwd); R=$D/../..; O=${1:?outdir}; mkdir -p "$O"
(cd "$R/port" && make -s) || exit 1
for t in $(grep -o 't_[a-z0-9_]*' "$D/cmpall.sh" | sort -u); do
  [ -f "$D/$t.txt" ] || continue
  rm -rf "${O:?}/run" "${O:?}/out"; cp -R "$D/runf" "$O/run"; mkdir "$O/out"
  ( E=$D/$t.env; [ -f "$E" ] && { set -a; . "$E"; set +a; }
    F=$D/$t.save.py; [ -f "$F" ] && python3 "$F" "$O/run/SAVE"
    POOLRAD_WHEEL=1 "$R/port/emu_h" -P "$O/$t.tsv" -d "$O/run" -S "$O/run/SAVE" -o "$O/out" -s "$D/$t.txt" -n 9000000000 "$O/run/POOLFLAT.EXE" 2>/dev/null )
done
python3 -I - "$R" "$O" <<'PY'
import sys, re, glob
R, O = sys.argv[1], sys.argv[2]
src = ''.join(open(f).read() for f in glob.glob(R + '/port/*.c'))
hooked = {('%s:%s' % (a, b)).lower() for a, b in re.findall(r'emu_hook\(\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)', src)}
cnt = {}
for f in glob.glob(O + '/*.tsv'):
    for l in open(f): a, c = l.split(); cnt[int(a, 16)] = cnt.get(int(a, 16), 0) + int(c)
rows = []
for l in open(R + '/re/ghidra/out/functions.tsv'):
    a, n, z, cc = l.rstrip('\n').split('\t'); s, o = a.split(':')
    lin = int(s, 16) * 16 + int(o, 16)
    if a not in hooked and lin in cnt: rows.append((int(z), a, cnt[lin]))
for z, a, c in sorted(rows, reverse=True): print('%6d bytes  %s  %d calls' % (z, a, c))
PY
