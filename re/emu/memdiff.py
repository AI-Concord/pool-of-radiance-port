#!/usr/bin/env python3
# usage: memdiff.py a.mem b.mem -> exit 1 and a summary if the game's data or heap differ.
# The stack (and whatever stale bytes lie below the stack pointer) is ignored: native routines do not
# leave the same garbage there as the original code.
import sys
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
DS, SS, STACK = 0x3dec0, 0x46620, 0x4000
bad = [i for r in ((DS, SS), (SS + STACK, 0xa0000)) for i in range(*r) if a[i] != b[i]]
if bad:
    print('%s: %d bytes differ; first at %05x (DS:%04x) %02x vs %02x' % (sys.argv[1], len(bad), bad[0], (bad[0] - DS) & 0xffff, a[bad[0]], b[bad[0]]))
    print('  ' + ' '.join('%05x' % i for i in bad[:12]))
    sys.exit(1)
