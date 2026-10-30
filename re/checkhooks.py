#!/usr/bin/env python3
# Checks port/hooks.c against the disassembly: each adapter must pop exactly what the original
# routine's RETF pops. Run after adding hooks.
import re, os, sys
here = os.path.dirname(os.path.abspath(__file__))
src = open(here + '/../port/hooks.c').read()
pops = {}; name = None
for l in src.split('\n'):
    m = re.match(r'static int (h_\w+)\(void\)', l)
    if m: name = m.group(1); pops[name] = set()
    if name:
        for x in re.findall(r'return (\d+);', l) + re.findall(r'\? (\d+) :', l): pops[name].add(int(x))
        if l.rstrip().endswith('}'): name = None
pops = {k: sorted(v) for k, v in pops.items()}
retf = {}; cur = None
for l in open(here + '/ghidra/out/disasm.txt'):
    if l.startswith(';;;; FUNCTION'): cur = l.split(' @ ')[1].strip(); retf[cur] = None
    elif cur and ' RETF' in l: p = l.split('RETF')[1].strip(); retf[cur] = int(p, 16) if p else 0
# Routines the decompiler split into fragments, so the listing does not tie their RETF to their entry.
# Each was read from the assembly by hand: entry -> bytes popped by its RETF.
retf.update({'2e2a:1406': 0x0e,    # attack; RETF 0xe at 2e2a:17f3
             '2e2a:17f6': 0x08,    # note attacker; RETF 0x8 at 2e2a:1881
             '2e2a:0712': 0x06,
             '3fa9:0000': 0x04,    # max load; RETF 0x4 at 3fa9:001d
             '2090:0249': 0x00,    # temple cure blindness; Ghidra misdecodes from 02a7 (02a8 is not a function), RETF at 2090:02ce
             '2090:0013': 0x04,    # temple 'anyway?' helper, called PUSH CS/CALL; RETF 0x4 at 2090:0073
             '3fa9:0508': 0x00,    # treasure pool; Ghidra misdecodes it, plain RETF at 3fa9:05df
             '3c10:2156': 0x00,    # trade money; Ghidra splits it (21d4, 22c3, 2468), RETF at 3c10:2475
             '2a9f:31a2': 0x00,    # effect table setup; Ghidra splits it at 3674, RETF (no args) at 2a9f:38a8
             '3fa9:0294': 0x08,    # amount entry; RETF 0x8 at 3fa9:043e
             '2a9f:0cc3': 0x0a,    # cloud clears; RETF 0xa at 2a9f:0f9f
             '2e2a:1bf8': 0x08,    # heal target; RETF 0x8 at 2e2a:1e13
             '2e2a:0e82': 0x08,    # fighter sweep; RETF 0x8 at 2e2a:1082
             '2e2a:08b2': 0x06,    # free attacks on leaving; RETF 0x6 at 2e2a:0c4c    # combat step; RETF 0x6 at 2e2a:08af
             '2e2a:02f9': 0x10,    # deal damage; not in the function table at all; RETF 0x10 at 2e2a:0624 (ndisasm)
             '2e2a:0191': 0x0a,
             # effect routines whose listing stops early; bytes 5D CA 0A 00 found at 29f0->2a0d, 2ae6->2b1c, 2b20->2b5f, 2b7d->2bc4
             '2a9f:29f0': 0x0a, '2a9f:2ae6': 0x0a, '2a9f:2b20': 0x0a, '2a9f:2b7d': 0x0a,
             '2a9f:2d88': 0x0a,
             '2a9f:2c1c': 0x0a,
             '2e2a:0000': 0x04,   # round start; Ghidra splits it at 000c/0058, RETF 0x4 at 2e2a:0120 (ndisasm)   # kind 79h; Ghidra splits it at 2c34, RETF 0xa at 2a9f:2d35 (ndisasm)
             '2a9f:2e37': 0x0a})   # 2e37 -> RETF 0xa at 2ed7 (ndisasm; Ghidra's function is cut short)   # 2d88 -> 5D CA 0A 00 at 2de0   # damage roll; bytes 5D CA 0A 00 at 2e2a:022E (the listing stops short of it)
bad = 0; n = 0
for m in re.finditer(r'emu_hook\(0x([0-9a-f]+), 0x([0-9a-f]+), (h_\w+), 1, "(\w+)"\)', src):
    key = '%s:%s' % (m.group(1), m.group(2)); n += 1
    want = retf.get(key); got = pops.get(m.group(3))
    if want is None or got != [want]:
        bad += 1; print('MISMATCH %s %-24s original pops %s, hook pops %s' % (key, m.group(4), want, got))
print('%d hooks checked, %d mismatches' % (n, bad)); sys.exit(1 if bad else 0)
