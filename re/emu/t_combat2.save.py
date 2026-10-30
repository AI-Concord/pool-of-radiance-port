# t_combat's fight with area effects running: Prayer on Bakshi, Protection From Evil 10' Radius on Rhiannon.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mksave import *
s = Save(sys.argv[1])
s.add_effect(2, 0x31)
s.add_effect(3, 0x2d)
