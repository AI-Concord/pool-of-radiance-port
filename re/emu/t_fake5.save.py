# A doctored party for t_fake5: t_fake4's gear, plus Detect Magic running so magic items get their star.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mksave import *
s = Save(sys.argv[1])
RING, SHIELD, LONG_SWORD, W_RING, W_HOLDING, W_BAG = 0x5d, 0x3b, 0x24, 0x42, 0xba, 0x49
s.add_item(1, RING, (0, 0, W_RING), readied=1, plus=2)          # Thrender: a protective ring +2
s.poke_item(2, 1, 0x32, 1)                                      # Bakshi: his banded mail becomes +1 ...
s.add_item(2, RING, (0, 0, W_RING), readied=1, plus=3)          # ... which should cancel this ring +3
s.poke_item(3, 0, 0x2e, LONG_SWORD)                             # Rhiannon the elf: her sword becomes a long sword
s.poke16(4, 0x88 + 3 * 2, 6000)                                 # Brother Sean: 6000 gold and a Bag of Holding
s.add_item(4, 0x49, (W_HOLDING, 0, W_BAG), readied=1)
s.poke16(5, 0x88 + 3 * 2, 6000)                                 # Darkstar: 6000 gold and no bag
s.add_item(6, SHIELD, (0, 0, SHIELD), readied=1, plus=1)        # Phineas: a shield +1
s.add_effect(1, 0x05)                                           # Thrender: Detect Magic
s.poke_item(2, 0, 0x32, 2)                                      # Bakshi: his sword becomes +2
