#!/usr/bin/env python3
# usage: ppm2png.py [-x scale] file.ppm... -> file.png next to each input
import sys, zlib, struct
def conv(path, scale):
    d = open(path, 'rb').read(); parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split()); px = parts[3]
    rows = []
    for y in range(h):
        row = px[y*w*3:(y+1)*w*3]
        if scale > 1: row = b''.join(row[i:i+3] * scale for i in range(0, len(row), 3))
        rows.extend([b'\0' + row] * scale)
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w*scale, h*scale, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b'')
    open(path[:-4] + '.png', 'wb').write(png)
a = sys.argv[1:]; scale = 1
if a and a[0] == '-x': scale = int(a[1]); a = a[2:]
for p in a: conv(p, scale)
