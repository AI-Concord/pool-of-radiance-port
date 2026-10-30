# t_combat's fight with the leader helpless (effect 1Fh, "Helpless"), to reach the slain-outright rule and a party member's fall.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mksave import *
s = Save(sys.argv[1])
s.add_effect(1, 0x1f)
