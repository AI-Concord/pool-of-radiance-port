# A doctored party for t_fake3: a safe place to sleep, a weakling, and everybody wounded.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mksave import *
s = Save(sys.argv[1])
# (The area's script resets its rest-interruption setting on loading, so the test script clears it with `areaw` instead.)
s.poke(6, 0x10, 3)                   # Phineas: Strength 3
for n in range(1, 7): s.poke(n, 0x11b, 2)   # everyone down to 2 hit points
