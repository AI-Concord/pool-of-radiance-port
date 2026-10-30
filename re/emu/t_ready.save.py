# Items for t_ready: a shield, a two-handed weapon, a weapon no class may use, and a club, all not readied.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mksave import *
s = Save(sys.argv[1])
s.add_item(1, 0x3b, (0, 0, 59))        # Shield
s.add_item(1, 0x03, (0, 0, 4))         # Bec De Corbin (two hands)
s.add_item(1, 0x58, (0, 0, 46))        # a crossbow type with class mask 0: wrong class for everyone
s.add_item(1, 0x06, (0, 0, 7))         # Club
