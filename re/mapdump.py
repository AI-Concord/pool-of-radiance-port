#!/usr/bin/env python3
# usage: mapdump.py dump.mem -> ASCII drawing of the 16x16 level in memory (walls, doors, event squares)
import sys, struct
m = open(sys.argv[1], 'rb').read(); D = 0x3dec0
off, seg = struct.unpack('<HH', m[D + 0x6a5c:D + 0x6a60]); g = seg * 16 + off
N = lambda x, y: m[g + y * 16 + x] >> 4
E = lambda x, y: m[g + y * 16 + x] & 15
S = lambda x, y: m[g + 0x100 + y * 16 + x] >> 4
W = lambda x, y: m[g + 0x100 + y * 16 + x] & 15
ev = lambda x, y: m[g + 0x200 + y * 16 + x]
dr = lambda x, y: m[g + 0x300 + y * 16 + x]
px, py = m[D + 0x6aad], m[D + 0x6aae]
ch = lambda w, d: ' ' if w == 0 else ('D' if d else '#')
print('    ' + ''.join('%-3x' % x for x in range(16)))
for y in range(16):
    print('   +' + ''.join((('--' if N(x, y) and not (dr(x, y) & 3) else '==' if N(x, y) else '  ') + '+') for x in range(16)))
    row = ''
    for x in range(16):
        w = W(x, y); d = (dr(x, y) >> 6) & 3
        row += ('|' if w and not d else ':' if w else ' ') + ('@@' if (x, y) == (px, py) else '%02x' % ev(x, y) if ev(x, y) else '  ')
    print('%2d ' % y + row + ('|' if E(15, y) else ' '))
print('party at', px, py, ' (== or : marks a wall that has a door; numbers are event ids)')
