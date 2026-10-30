#!/usr/bin/env python3
# usage: prof.py prof.tsv [n] -> most-called routines with their Ghidra names
import sys, os
here = os.path.dirname(os.path.abspath(__file__))
names = {}
for l in open(here + '/ghidra/out/functions.tsv'):
    a, n, sz, cc = l.rstrip('\n').split('\t'); sg, of = a.split(':'); names[int(sg, 16) * 16 + int(of, 16)] = (a, n, int(sz))
rows = sorted(((int(c), int(a, 16)) for a, c in (l.split() for l in open(sys.argv[1]))), reverse=True)
print(len(rows), 'distinct call targets;', sum(1 for c, a in rows if a in names), 'known to ghidra')
for c, a in rows[:int(sys.argv[2]) if len(sys.argv) > 2 else 40]:
    n = names.get(a); print('%8d  %05x  %s' % (c, a, ('%s %s size %d' % n) if n else '?? (not a ghidra function)'))
