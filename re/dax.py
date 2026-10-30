#!/usr/bin/env python3
# Reads Gold Box .DAX archives.
#   dax.py list FILE.DAX             -> block ids and sizes
#   dax.py raw FILE.DAX ID out.bin   -> one unpacked block
#   dax.py pics FILE.DAX outdir      -> every block that parses as a picture, as PNG (all frames side by side)
import sys, struct, zlib, os
EGA = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
       (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
def entries(d):
    n = struct.unpack('<H', d[:2])[0]
    return [struct.unpack('<BIHH', d[2 + i:11 + i]) for i in range(0, n, 9)], n + 2
def unpack(b):
    out = bytearray(); i = 0
    while i < len(b):
        c = b[i]
        if c <= 0x7e: out += b[i + 1:i + 2 + c]; i += c + 2          # literal run of c+1 bytes
        elif c >= 0x81: out += bytes([b[i + 1]]) * (256 - c); i += 2  # repeat next byte (256-c) times
        else: break
    return bytes(out)
def block(d, ident):
    es, base = entries(d)
    for i, off, raw, comp in es:
        if i == ident: return unpack(d[base + off:base + off + comp])[:raw]
def png(path, w, h, px):
    rows = b''.join(b'\0' + b''.join(bytes(EGA[c]) for c in px[y * w:(y + 1) * w]) for y in range(h))
    ch = lambda t, b: struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(rows)) + ch(b'IEND', b''))
def picture(b):
    # 17-byte header: height, width (in 8-pixel columns), x, y (words), frame count (byte), 8 bytes; then 4 bits per pixel
    if len(b) < 17: return None
    h, w, x, y, n = struct.unpack('<HHHHB', b[:9])
    if not (0 < h <= 200 and 0 < w <= 40 and 0 < n <= 64) or len(b) < 17 + n * h * w * 4: return None
    W = w * 8; px = [0] * (W * n * h)
    for f in range(n):
        for i in range(h * w * 4):
            v = b[17 + f * h * w * 4 + i]; yy, xx = divmod(i * 2, W)
            px[yy * W * n + f * W + xx] = v >> 4; px[yy * W * n + f * W + xx + 1] = v & 15
    return W * n, h, px
def animation(b, delta):
    # frame count, then per frame: delay (4 bytes), height, width in columns, x, y (words), a spare byte,
    # 8 bytes, pixels. With `delta` (the PIC archives) frames after the first are XORed with frame one.
    if len(b) < 22 or not 0 < b[0] <= 32: return None
    p = 1; frames = []; first = None
    for f in range(b[0]):
        if p + 21 > len(b): return None
        h, w = struct.unpack('<HH', b[p + 4:p + 8])
        if not (0 < h <= 200 and 0 < w <= 40): return None
        p += 21; n = h * w * 4
        if p + n > len(b): return None
        data = bytearray(b[p:p + n]); p += n
        if delta:
            if first is None: first = bytes(data)
            elif len(first) == n: data = bytearray(x ^ y for x, y in zip(data, first))
        frames.append((w * 8, h, data))
    W = sum(f[0] for f in frames); H = max(f[1] for f in frames); px = [0] * (W * H); x0 = 0
    for w, h, data in frames:
        for i, v in enumerate(data):
            yy, xx = divmod(i * 2, w); px[yy * W + x0 + xx] = v >> 4; px[yy * W + x0 + xx + 1] = v & 15
        x0 += w
    return W, H, px
if __name__ == '__main__':
    d = open(sys.argv[2], 'rb').read()
    if sys.argv[1] == 'list':
        for i, off, raw, comp in entries(d)[0]: print('id %3d  offset %6d  unpacked %5d  packed %5d' % (i, off, raw, comp))
    elif sys.argv[1] == 'raw': open(sys.argv[4], 'wb').write(block(d, int(sys.argv[3])))
    elif sys.argv[1] == 'pics':
        os.makedirs(sys.argv[3], exist_ok=True); name = os.path.basename(sys.argv[2]).split('.')[0]
        for i, off, raw, comp in entries(d)[0]:
            blk = block(d, i)
            p = picture(blk) or animation(blk, name.upper().startswith(('PIC', 'FINAL')))
            if p: png('%s/%s_%03d.png' % (sys.argv[3], name, i), *p)
