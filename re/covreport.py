# usage: covreport.py <repo> file.cov... -> routines whose original code ran (see re/emu/covall.sh)
import sys, re, glob, bisect
R, files = sys.argv[1], sys.argv[2:]
src = ''.join(open(f).read() for f in glob.glob(R + '/port/*.c'))
hooked = {int(a, 16) * 16 + int(b, 16) for a, b in re.findall(r'emu_hook\(\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)', src)}
ran = set()
for f in files:
    for l in open(f): ran.add(int(l, 16))
fns = []
for l in open(R + '/re/ghidra/out/functions.tsv'):
    a, n, z, cc = l.rstrip('\n').split('\t'); s, o = a.split(':')
    fns.append((int(s, 16) * 16 + int(o, 16), int(z), a, n))
fns.sort(); starts = [f[0] for f in fns]
hit = {}
for lin in ran:
    if lin in hooked: continue          # the entry of a routine replaced by C: only the hook ran
    i = bisect.bisect_right(starts, lin) - 1
    if i >= 0 and lin < fns[i][0] + max(fns[i][1], 1): hit.setdefault(i, 0); hit[i] += 1
    else: hit.setdefault(-1, 0); hit[-1] += 1
tot = 0
for i, c in sorted(hit.items(), key=lambda x: -fns[x[0]][1] if x[0] >= 0 else 0):
    if i < 0: print('%6s        outside any listed routine: %d addresses' % ('', c)); continue
    a, z, name = fns[i][2], fns[i][1], fns[i][3]; tot += z
    print('%6d bytes  %s  %-28s %d addresses ran' % (z, name and a, name, c))
print('total %d bytes in %d routines' % (tot, len([i for i in hit if i >= 0])))
