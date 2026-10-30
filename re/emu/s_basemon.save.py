# Donor effects for monster tests: Phineas carries abilities that the test scripts move onto kobolds with movefx.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mksave import *
s = Save(sys.argv[1])
for kind in (0x49, 0x4c, 0x4d, 0x57, 0x40):   # rake, grab, hold, mummy touch, poison
    s.add_effect(6, kind)
for n in range(1, 7):                           # an easy party to hit, hard to kill
    for i in range(len(s.read(n, 'ITM')) // 63):
        if s.read(n, 'ITM')[i * 63 + 0x2e] in range(0x32, 0x3c): s.poke_item(n, i, 0x34, 0)
    s.poke(n, 0x11b, 80); s.poke(n, 0x32, 80)
