#!/usr/bin/env python3
# usage: sheet.py outdir [cols] -> outdir/sheet.png : contact sheet of all shots in script order
import sys, zlib, struct, glob, os
d = sys.argv[1]; cols = int(sys.argv[2]) if len(sys.argv) > 2 else 3
fs = sorted(glob.glob(d + '/*.ppm'), key=os.path.getmtime)
fs = [f for f in fs if not f.endswith('final.ppm') and not f.endswith('crash.ppm')]
ims = []
for p in fs:
    b = open(p, 'rb').read().split(b'\n', 3); w, h = map(int, b[1].split())
    if (w, h) == (320, 200): ims.append(b[3])
W, H, G = 320, 200, 4
rows = (len(ims) + cols - 1) // cols
sw = cols * (W + G); out = []
for r in range(rows):
    for y in range(H):
        line = b''
        for c in range(cols):
            i = r * cols + c
            line += (ims[i][y * W * 3:(y + 1) * W * 3] if i < len(ims) else b'\0' * W * 3) + b'\x50\x50\x50' * G
        out.append(b'\0' + line)
    out.extend([b'\0' + b'\x50\x50\x50' * sw] * G)
def ch(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
open(d + '/sheet.png', 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', sw, len(out), 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(b''.join(out))) + ch(b'IEND', b''))
print(len(ims), 'frames ->', d + '/sheet.png')
