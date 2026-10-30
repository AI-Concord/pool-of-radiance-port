#!/usr/bin/env python3
"""Doctor a saved game so tests can reach code a normal party never does.

A saved character is three files in SAVE/: CHRDAT<game><n>.SAV (the 285-byte character record),
.ITM (63-byte items) and .SPC (9-byte status effects), plus SAVGAM<game>.DAT for the game and area data. This module edits them in place.
cmp.sh runs "<script>.save.py <SAVE dir>" on both copies of the game before a comparison, so the
original and the translated build always load the same doctored party."""
import os, struct

class Save:
    def __init__(self, save_dir, game='A'): self.dir, self.game = save_dir, game
    def path(self, n, ext): return os.path.join(self.dir, 'CHRDAT%s%d.%s' % (self.game, n, ext))
    def read(self, n, ext):
        p = self.path(n, ext)
        return bytearray(open(p, 'rb').read()) if os.path.exists(p) else bytearray()
    def write(self, n, ext, data): open(self.path(n, ext), 'wb').write(bytes(data))

    def poke(self, n, offset, *values):
        """Set bytes of character n's record."""
        d = self.read(n, 'SAV'); d[offset:offset + len(values)] = bytes(values); self.write(n, 'SAV', d)
    def poke16(self, n, offset, value): self.poke(n, offset, value & 255, value >> 8)

    def _append(self, n, ext, size, node, link):
        d = self.read(n, ext)
        if d: d[len(d) - size + link:len(d) - size + link + 4] = b'\x01\x00\x01\x00'   # the old last node now has a successor
        self.write(n, ext, d + node)
    def add_effect(self, n, kind, minutes=0):
        """Give character n a status effect (see the spell table in the docs for the numbers)."""
        self._append(n, 'SPC', 9, bytearray(struct.pack('<BHBB4x', kind, minutes, 0xff, 0)), 5)
    def add_item(self, n, item_type, words, spells=(0, 0, 0), weight=1, readied=0, plus=0):
        """Give character n an item. words = the three name-word codes, last word first."""
        it = bytearray(63)
        it[0x2e] = item_type; it[0x2f:0x32] = bytes(words); it[0x32] = plus & 255
        it[0x34] = readied; it[0x37:0x39] = struct.pack('<H', weight)
        it[0x3c:0x3f] = bytes(spells)
        self._append(n, 'ITM', 63, it, 0x2a)

    def poke_item(self, n, index, offset, *values):
        """Set bytes of character n's item number `index` (0 = first in the file)."""
        d = self.read(n, 'ITM'); o = index * 63 + offset; d[o:o + len(values)] = bytes(values); self.write(n, 'ITM', d)

    # SAVGAM<game>.DAT: one byte, then the game's own 800h-byte data block (clock, flags), then the
    # current area's 800h-byte script data block.
    def _dat(self, base, offset, values):
        p = os.path.join(self.dir, 'SAVGAM%s.DAT' % self.game)
        d = bytearray(open(p, 'rb').read()); d[base + offset:base + offset + len(values)] = bytes(values); open(p, 'wb').write(bytes(d))
    def poke_game16(self, offset, value): self._dat(1, offset, struct.pack('<H', value))
    def poke_area16(self, offset, value): self._dat(0x801, offset, struct.pack('<H', value))

# Numbers worth having to hand.
MU_SCROLL, CLERIC_SCROLL = 62, 61
W_MU_SCROLL, W_CLERIC_SCROLL, W_1_SPELL, W_2_SPELLS, W_3_SPELLS = 0xd1, 0xd0, 0xd2, 0xd3, 0xd4
