# A doctored party for t_fake1: things the normal save never shows.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mksave import *
s = Save(sys.argv[1])
# 1 Thrender: Strength 18(00), every kind of coin, gems and jewellery.
s.poke(1, 0x10, 18); s.poke(1, 0x16, 100)
for kind, amount in enumerate((7, 36, 12, 150, 2, 3, 1)): s.poke16(1, 0x88 + kind * 2, amount)
# 2 Bakshi (cleric/fighter/magic-user): hasted, blessed, diseased, regenerating, and a scroll with
# a spell he can learn (Enlarge), one he knows (Read Magic) and one beyond him (Fireball).
for kind in (0x27, 0x01, 0x32, 0x3b, 0x47): s.add_effect(2, kind)
s.add_item(2, MU_SCROLL, (0, W_3_SPELLS, W_MU_SCROLL), spells=(12, 18, 47))
# 3 Rhiannon: slowed, and marked as a non-player companion.
s.add_effect(3, 0x2a)
s.poke(3, 0x84, 0x80)
# 5 Darkstar: unconscious.
s.poke(5, 0x10c, 4); s.poke(5, 0x10d, 0)
