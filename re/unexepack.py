#!/usr/bin/env python3
# Unpacks a Microsoft EXEPACK-compressed MZ executable into a plain MZ with a relocation table.
# usage: unexepack.py in.exe out.exe
import struct, sys
src = open(sys.argv[1], 'rb').read()
h = struct.unpack('<14H', src[2:30])
lastpage, pages, nreloc, hdrparas, minalloc, maxalloc, ss, sp, csum, ip, cs, relocoff, ovl = h[:13]
hdr = hdrparas * 16
img = bytearray(src[hdr:])
ph = cs * 16
real_ip, real_cs, _, epsize, real_sp, real_ss, dest_len, skip_len, sig = struct.unpack('<9H', img[ph:ph + 18])
assert sig == 0x4252, 'not EXEPACK'
out = bytearray(dest_len * 16)
out[:ph] = img[:ph]
s = ph - (skip_len - 1) * 16          # end of packed data
d = dest_len * 16
while img[s - 1] == 0xff: s -= 1      # padding
while True:
    cmd = img[s - 1]; n = img[s - 3] | (img[s - 2] << 8); s -= 3
    if cmd & 0xfe == 0xb0:
        s -= 1; d -= n; out[d:d + n] = bytes([img[s]]) * n
    elif cmd & 0xfe == 0xb2:
        s -= n; d -= n; out[d:d + n] = img[s:s + n]
    else: raise SystemExit('corrupt')
    if cmd & 1: break
# relocation table follows the stub's error message
msg = img.find(b'Packed file is corrupt', ph); p = msg + 22
relocs = []
for hi in range(16):
    cnt = struct.unpack('<H', img[p:p + 2])[0]; p += 2
    for i in range(cnt):
        relocs.append((struct.unpack('<H', img[p:p + 2])[0], hi * 0x1000)); p += 2
nh = (0x1c + 4 * len(relocs) + 511) // 512 * 512
total = nh + len(out)
mem_paras = (len(img) + 15) // 16 + minalloc           # memory the packed program asked for
newmin = max(0, mem_paras - len(out) // 16)
head = struct.pack('<2s13H', b'MZ', total % 512, (total + 511) // 512, len(relocs), nh // 16, newmin, maxalloc, real_ss, real_sp, 0, real_ip, real_cs, 0x1c, 0)
tab = b''.join(struct.pack('<HH', o, sg) for o, sg in relocs)
open(sys.argv[2], 'wb').write((head + tab).ljust(nh, b'\0') + out)
print('unpacked image %d bytes, %d relocations, entry %04x:%04x, ss:sp %04x:%04x, minalloc %04x' % (len(out), len(relocs), real_cs, real_ip, real_ss, real_sp, newmin))
