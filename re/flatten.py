#!/usr/bin/env python3
# Builds a "flat" Pool of Radiance executable: START.EXE (unpacked) with every overlay from GAME.OVR
# linked in at a fixed segment, between the resident code and the data segment. The overlay stubs
# become plain far jumps, so no code ever moves and every routine has one address.
# usage: flatten.py START_UNPACKED.EXE GAME.OVR POOLFLAT.EXE map.tsv
import struct, sys
exe = open(sys.argv[1], 'rb').read(); ovr = open(sys.argv[2], 'rb').read()
(lastpage, pages, nrel, hdrp, minalloc, maxalloc, ss, sp, csum, ip, cs, reloff, ovl) = struct.unpack('<13H', exe[2:28])
img = bytearray(exe[hdrp * 16:])
rel = [struct.unpack('<HH', exe[reloff + 4 * i:reloff + 4 * i + 4]) for i in range(nrel)]
assert ovr[:4] == b'FBOV'
DATA = struct.unpack('<H', img[0xaf8 * 16 + 1:0xaf8 * 16 + 3])[0]      # System start-up: mov dx, seg DATA
stubs = []
for seg in range(DATA):
    o = seg * 16
    if img[o:o + 2] == b'\xcd\x3f':
        memswap, fofs, csize, fsize, njmp = struct.unpack('<HIHHH', img[o + 2:o + 14])
        if 8 <= fofs and fofs + csize + fsize <= len(ovr) and csize and 0 < njmp < 600 and img[o + 0x20:o + 0x22] == b'\xcd\x3f':
            stubs.append((seg, fofs, csize, fsize, njmp))
stubs.sort(key=lambda s: s[1])
pos = 8
for s in stubs: assert s[1] == pos, 'gap in overlay file'; pos += s[2] + s[3]
assert pos == len(ovr)
# lay the overlay code out
block = bytearray(); newrel = []; units = []
for seg, fofs, csize, fsize, njmp in stubs:
    fseg = DATA + len(block) // 16
    code = ovr[fofs:fofs + csize]; block += code + b'\0' * (-csize % 16)
    for i in range(fsize // 2):
        newrel.append((struct.unpack('<H', ovr[fofs + csize + 2 * i:fofs + csize + 2 * i + 2])[0], fseg))
    ents = []
    for i in range(njmp):
        e = seg * 16 + 0x20 + 5 * i
        assert img[e:e + 2] == b'\xcd\x3f'
        off = struct.unpack('<H', img[e + 2:e + 4])[0]; ents.append(off)
        img[e:e + 5] = b'\xea' + struct.pack('<HH', off, fseg)              # jmp far fseg:off
        newrel.append((0x20 + 5 * i + 3, seg))
    units.append((seg, fseg, csize, ents))
K = len(block) // 16
flat = bytearray(img[:DATA * 16] + block + img[DATA * 16:])
def shift(s): return s + K if s >= DATA else s
out_rel = []
for off, sg in rel:                       # original relocations: locations in the data segment move up
    lin = sg * 16 + off
    out_rel.append((off, sg + K) if lin >= DATA * 16 else (off, sg))
for off, sg in newrel:                    # overlay fix-ups and stub jumps (already in flat coordinates, below DATA or in block)
    out_rel.append((off, sg))
jumps = set((0x20 + 5 * i + 3, u[0]) for u in units for i in range(len(u[3])))
for off, sg in out_rel:                   # segment values at or above DATA move up
    a = sg * 16 + off; v = struct.unpack('<H', flat[a:a + 2])[0]
    if (off, sg) in jumps: continue       # already final
    struct.pack_into('<H', flat, a, shift(v))
nh = (0x1c + 4 * len(out_rel) + 511) // 512 * 512; total = nh + len(flat)
head = struct.pack('<2s13H', b'MZ', total % 512, (total + 511) // 512, len(out_rel), nh // 16, minalloc, maxalloc, shift(ss), sp, 0, ip, cs, 0x1c, 0)
open(sys.argv[3], 'wb').write((head + b''.join(struct.pack('<HH', o, s) for o, s in out_rel)).ljust(nh, b'\0') + flat)
with open(sys.argv[4], 'w') as f:
    f.write('# kind\tseg\tsize\tstubseg\tentries\n')
    for n, (seg, fseg, csize, ents) in enumerate(units):
        f.write('ovr%02d\t%04x\t%04x\t%04x\t%s\n' % (n, fseg, csize, seg, ','.join('%04x' % e for e in ents)))
    f.write('data\t%04x\n' % (DATA + K))
print('flat image %d bytes (%d overlays, %d paragraphs inserted at %04x), data seg %04x, ss %04x, %d relocations' % (len(flat), len(units), K, DATA, DATA + K, shift(ss), len(out_rel)))
