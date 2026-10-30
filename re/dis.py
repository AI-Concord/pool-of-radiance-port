#!/usr/bin/env python3
# usage: dis.py SEG:OFF LEN -> ndisasm of the flat program at a Ghidra address (for code Ghidra did not disassemble)
import sys, struct, subprocess, os, tempfile
here = os.path.dirname(os.path.abspath(__file__))
s, o = (int(x, 16) for x in sys.argv[1].split(':')); n = int(sys.argv[2], 0)
d = open(here + '/out/POOLFLAT.EXE', 'rb').read(); hdr = struct.unpack('<H', d[8:10])[0] * 16
a = hdr + (s - 0x1000) * 16 + o
with tempfile.NamedTemporaryFile(delete=False) as f: f.write(d[a:a + n]); p = f.name
out = subprocess.run(['ndisasm', '-b16', '-o', str(o), p], capture_output=True, text=True).stdout; os.unlink(p)
for l in out.splitlines(): print('%04x:%s' % (s, l))
