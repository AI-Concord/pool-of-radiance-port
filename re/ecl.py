#!/usr/bin/env python3
# Disassembler for Gold Box ECL adventure scripts (Pool of Radiance).
#   ecl.py ECLn.DAX            -> list script blocks
#   ecl.py ECLn.DAX ID         -> disassemble block ID
# Encoding (from the interpreter at 1D3F:2F2D and the operand fetch at 2366:0000):
#   opcode byte, then operands: a type byte followed by
#     00 b      byte literal            01 lo hi  value of the game variable at that address
#     02 lo hi  word literal            03 lo hi  as 01
#     80 n ...  inline text, n packed bytes (6 bits per character, 4 characters per 3 bytes)
#     81 lo hi  text stored at a variable address
# Script addresses are virtual: the block is mapped so that its first byte is at BASE.
import sys, struct
sys.path.insert(0, __import__('os').path.dirname(__file__))
import dax
BASE = 0x9900 - 2          # the block starts with a 2-byte header word; code follows at 9900h
NAMES = {0x00: 'EXIT', 0x01: 'GOTO', 0x02: 'GOSUB', 0x03: 'COMPARE', 0x04: 'ADD', 0x05: 'SUBTRACT', 0x06: 'DIVIDE', 0x07: 'MULTIPLY',
         0x08: 'RANDOM', 0x09: 'SAVE', 0x0a: 'LOAD CHARACTER', 0x0b: 'LOAD MONSTER', 0x0c: 'SETUP MONSTER', 0x0d: 'APPROACH',
         0x0e: 'PICTURE', 0x0f: 'INPUT NUMBER', 0x10: 'INPUT STRING', 0x11: 'PRINT', 0x12: 'PRINTCLEAR', 0x13: 'RETURN',
         0x14: 'COMPARE AND', 0x15: 'VERTICAL MENU', 0x16: 'IF =', 0x17: 'IF <>', 0x18: 'IF <', 0x19: 'IF >', 0x1a: 'IF <=', 0x1b: 'IF >=',
         0x1c: 'CLEARMONSTERS', 0x1d: 'PARTYSTRENGTH', 0x1e: 'CHECKPARTY', 0x20: 'NEWECL', 0x21: 'LOAD FILES', 0x22: 'PARTY SURPRISE',
         0x23: 'SURPRISE', 0x24: 'COMBAT', 0x25: 'ON GOTO', 0x26: 'ON GOSUB', 0x27: 'TREASURE', 0x28: 'ROB', 0x29: 'ENCOUNTER MENU',
         0x2a: 'GETTABLE', 0x2b: 'HORIZONTAL MENU', 0x2c: 'PARLAY', 0x2d: 'CALL', 0x2e: 'DAMAGE', 0x2f: 'AND', 0x30: 'OR',
         0x31: 'SPRITE OFF', 0x32: 'FIND ITEM', 0x33: 'PRINT RETURN', 0x34: 'ECL CLOCK', 0x35: 'SAVE TABLE', 0x36: 'ADD NPC',
         0x37: 'LOAD PIECES', 0x38: 'PROGRAM', 0x39: 'WHO', 0x3a: 'DELAY', 0x3b: 'SPELL', 0x3c: 'PROTECTION', 0x3d: 'CLEAR BOX'}
ARGS = {0x00: 0, 0x01: 1, 0x02: 1, 0x03: 2, 0x04: 3, 0x05: 3, 0x06: 3, 0x07: 3, 0x08: 2, 0x09: 2, 0x0a: 1, 0x0b: 3, 0x0c: 3, 0x0d: 0,
        0x0e: 1, 0x0f: 2, 0x10: 2, 0x11: 1, 0x12: 1, 0x13: 0, 0x14: 4, 0x15: 3, 0x16: 0, 0x17: 0, 0x18: 0, 0x19: 0, 0x1a: 0, 0x1b: 0,
        0x1c: 0, 0x1d: 1, 0x1e: 6, 0x20: 1, 0x21: 3, 0x22: 2, 0x23: 4, 0x24: 0, 0x25: 2, 0x26: 2, 0x27: 8, 0x28: 3, 0x29: 14,
        0x2a: 3, 0x2b: 2, 0x2c: 6, 0x2d: 1, 0x2e: 5, 0x2f: 3, 0x30: 3, 0x31: 0, 0x32: 1, 0x33: 0, 0x34: 1, 0x35: 3, 0x36: 2,
        0x37: 3, 0x38: 1, 0x39: 1, 0x3a: 0, 0x3b: 3, 0x3c: 1, 0x3d: 0}
EXTRA = {0x15, 0x25, 0x26, 0x2b}      # the last fixed operand is a count of further operands
def text(b):
    out = ''
    for i in range(0, len(b), 3):
        t = b[i:i + 3] + b'\0\0'; n = len(b[i:i + 3])
        cs = [t[0] >> 2, ((t[0] & 3) << 4) | (t[1] >> 4), ((t[1] & 15) << 2) | (t[2] >> 6), t[2] & 63][:n + 1 if n < 3 else 4]
        out += ''.join(chr(c + 0x40 if c < 0x20 else c) for c in cs if c)
    return out
def operand(b, p):
    t = b[p]
    if t == 0x00: return '%d' % b[p + 1], p + 2, b[p + 1]
    if t in (1, 3): v = b[p + 1] | (b[p + 2] << 8); return '[%04X]' % v, p + 3, None
    if t == 2: v = b[p + 1] | (b[p + 2] << 8); return '%04Xh' % v, p + 3, v
    if t == 0x80: n = b[p + 1]; return '"%s"' % text(b[p + 2:p + 2 + n]), p + 2 + n, None
    if t == 0x81: v = b[p + 1] | (b[p + 2] << 8); return 'text[%04X]' % v, p + 3, None
    raise ValueError('operand type %02x' % t)
def disasm(b, start=2, end=None):
    p = start; end = end or len(b); lines = []
    while p < end:
        a = p; op = b[p]; p += 1
        if op not in NAMES: lines.append((a, 'db %02X' % op)); continue
        try:
            ops = []; last = None
            for i in range(ARGS[op]): s, p, last = operand(b, p); ops.append(s)
            if op in EXTRA and last is not None:
                for i in range(last): s, p, _ = operand(b, p); ops.append(s)
        except (ValueError, IndexError): lines.append((a, 'db %02X' % op)); p = a + 1; continue
        if op in (1, 2) and ops[0].startswith('['): ops[0] = ops[0][1:5] + 'h'      # jump targets are plain addresses
        lines.append((a, NAMES[op] + (' ' + ', '.join(ops) if ops else '')))
    return lines
if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    if len(sys.argv) < 3:
        for i, off, raw, comp in dax.entries(d)[0]: print('block %3d  %5d bytes' % (i, raw))
    else:
        b = dax.block(d, int(sys.argv[2]))
        for a, s in disasm(b): print('%04X  %s' % (a + BASE, s))
