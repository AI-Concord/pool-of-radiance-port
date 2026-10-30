#!/usr/bin/env python3
"""Show the game state in a memory dump (.mem) or a snapshot (.snap): where the party is, the clock,
each party member with hit points, status and status effects, and during a fight every combatant
with its position, plus the list of the fallen.   usage: memview.py file [file...]"""
import struct, sys
DS = 0x3dec0
STATUS = ['Okay', 'Animated', 'tempgone', 'Running', 'Unconscious', 'Dying', 'Dead', 'Stoned', 'Gone']
def far(d, a):
    off, seg = struct.unpack_from('<HH', d, a); return seg * 16 + off if seg else 0
def name(d, a): return d[a + 1:a + 1 + min(d[a], 15)].decode('latin1') if a else '-'
def effects(d, c):
    out = []; e = far(d, c + 0x7f)
    while e and len(out) < 40:
        t = struct.unpack_from('<H', d, e + 1)[0]; out.append('%02X' % d[e] + (':%d' % t if t else '')); e = far(d, e + 5)
    return ' '.join(out)
def line(d, c):
    st = d[c + 0x10c]
    return '%-15s hp %3d/%-3d AC %3d  %-11s%s  fx %s' % (name(d, c), d[c + 0x11b], d[c + 0x32], 60 - d[c + 0x111],
        STATUS[st] if st < len(STATUS) else st, '' if d[c + 0x10d] else ' (cannot act)', effects(d, c))
for path in sys.argv[1:]:
    d = open(path, 'rb').read()
    g = far(d, DS + 0x49d2)
    clock = struct.unpack_from('<7H', d, g + 0x18c) if g else (0,) * 7
    print('== %s   state %d   at (%d,%d) facing %s   clock %02d:%d%d   round %d' % (path.split('/')[-1], d[DS + 0x49f3],
        d[DS + 0x6aad], d[DS + 0x6aae], 'N?E?S?W?'[d[DS + 0x6aaf] & 7], clock[3], clock[2], clock[1], d[DS + 0x6de3]))
    c = far(d, DS + 0x5d96); sel = far(d, DS + 0x5d92); n = 0
    while c and n < 12:
        n += 1; print(' %s%d %s' % ('>' if c == sel else ' ', n, line(d, c))); c = far(d, c + 0x104)
    if d[DS + 0x49f3] == 5:
        N = d[DS + 0x5f2a]; print('  combatants: %d   map %04X:%04X   view origin (%d,%d)' % ((N,) + struct.unpack_from('<HH', d, DS + 0x6716)[::-1]
            + ((d[far(d, DS + 0x6716) + 2], d[far(d, DS + 0x6716) + 3]) if far(d, DS + 0x6716) + 3 < len(d) else (0, 0))))
        for s in range(1, N + 1):
            c = far(d, DS + 0x65b9 + s * 4)
            if not c or c + 0x120 > len(d): continue
            x, y, _, size = d[DS + 0x5f27 + s * 4:DS + 0x5f2b + s * 4]
            print('  %2d %s at (%2d,%2d) size %d  %s' % (s, 'P' if d[c + 0x10e] == 0 else 'E', x, y, size, line(d, c)))
        k = d[DS + 0x6715]; print('  fallen: count %d' % k)
        for i in range(1, 9):
            e = DS + 0x66d6 + i * 7; c = far(d, e)
            if c or d[e + 4] or d[e + 5]: print('    entry %d: %-12s at (%d,%d) ground was %02X' % (i, name(d, c) if c + 16 < len(d) else '?', d[e + 4], d[e + 5], d[e + 6]))
