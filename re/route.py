#!/usr/bin/env python3
# usage: route.py dump.mem X Y [extra-steps] -> script lines (key presses) walking the party to X,Y.
# Shortest path over the level in memory; event squares are allowed but expensive.
import sys, struct
import heapq
m = open(sys.argv[1], 'rb').read(); D = 0x3dec0
tx, ty = int(sys.argv[2]), int(sys.argv[3]); extra = int(sys.argv[4]) if len(sys.argv) > 4 else 0
off, seg = struct.unpack('<HH', m[D + 0x6a5c:D + 0x6a60]); g = seg * 16 + off
def wall(x, y, d):
    b = m[g + (0x100 if d >= 2 else 0) + y * 16 + x]; return (b >> 4) if d in (0, 2) else (b & 15)   # d: 0 N, 1 E, 2 S, 3 W
def door(x, y, d): return (m[g + 0x300 + y * 16 + x] >> (d * 2)) & 3
DX = (0, 1, 0, -1); DY = (-1, 0, 1, 0)
px, py, pd = m[D + 0x6aad], m[D + 0x6aae], m[D + 0x6aaf] // 2
prev = {(px, py): None}; dist = {(px, py): 0}; q = [(0, px, py)]
while q:
    c, x, y = heapq.heappop(q)
    if c > dist[(x, y)]: continue
    for d in range(4):
        nx, ny = x + DX[d], y + DY[d]
        if not (0 <= nx < 16 and 0 <= ny < 16): continue
        if wall(x, y, d) and door(x, y, d) != 1: continue          # 1 = ordinary door, 2 = locked
        nc = c + 1 + (40 if m[g + 0x200 + ny * 16 + nx] and (nx, ny) != (tx, ty) else 0)   # event squares are costly
        if nc < dist.get((nx, ny), 1 << 30): dist[(nx, ny)] = nc; prev[(nx, ny)] = (x, y, d); heapq.heappush(q, (nc, nx, ny))
if (tx, ty) not in prev: sys.exit('no route')
steps = []; c = (tx, ty)
while prev[c]: x, y, d = prev[c]; steps.append(d); c = (x, y)
out = []
for d in reversed(steps):
    t = (d - pd) % 4
    out += {0: [], 1: ['right'], 2: ['right', 'right'], 3: ['left']}[t] + ['up']; pd = d
out += ['up'] * extra
print(''.join('key %s\npos %s\n' % (k, k) for k in out), end='')
