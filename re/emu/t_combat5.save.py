# t_combat4's party (which is known to get hit a good deal), but the leader can be hurt, has one
# hit point and the troll's trick (64h): he should fall and get back up.
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mksave import *
s = Save(sys.argv[1])
s.add_effect(1, 0x01)   # Thrender: Bless
s.add_effect(2, 0x24)   # Bakshi: Bestow Curse
s.add_effect(3, 0x47)   # Rhiannon: Invisible
s.add_effect(4, 0x2a)   # Brother Sean: Slow
s.add_effect(5, 0x23)   # Darkstar: Prayer (friend)
s.add_effect(6, 0x27)   # Phineas: Haste
s.add_effect(1, 0x02)   # Thrender: Curse too
s.add_effect(2, 0x1d)   # Bakshi: Ray of Enfeeblement
s.add_effect(3, 0x25)   # Rhiannon: Blink
s.add_effect(5, 0x3a)   # Darkstar: rooted
s.add_effect(6, 0x0a); s.add_effect(6, 0x14)   # Phineas: Resist Cold and Fire
s.add_effect(1, 0x11); s.add_effect(1, 0x64)   # Thrender: Shield, hit only by magic weapons
s.add_effect(2, 0x21); s.add_effect(2, 0x68)   # Bakshi: blinded, avoids missiles
s.add_effect(3, 0x69); s.add_effect(3, 0x6b)   # Rhiannon: magic resistance, elf resistance
s.add_effect(4, 0x74); s.add_effect(4, 0x75)   # Brother Sean: half from magic weapons, type-55 damage
s.add_effect(5, 0x6e); s.add_effect(5, 0x70)   # Darkstar: immune to cold and fire
# Make the party easy to hit and hard to kill, so the effects that react to being hit get used.
for n in range(1, 7):
    for i in range(len(s.read(n, 'ITM')) // 63):
        if s.read(n, 'ITM')[i * 63 + 0x2e] in range(0x32, 0x3c): s.poke_item(n, i, 0x34, 0)   # armour and shields unreadied
    s.poke(n, 0x11b, 90); s.poke(n, 0x32, 90)
s.add_effect(1, 0x40); s.add_effect(4, 0x43); s.add_effect(6, 0x41)   # poison and paralysis attacks
# A second kind of attack (as monsters have: a bite as well as claws), which is what carries poison and paralysis.
for n in (1, 4, 6):
    s.poke(n, 0xa2, 2); s.poke(n, 0xa4, 1); s.poke(n, 0xa6, 4)   # rate 2 (one a round), 1 die, 4 sides
s.add_effect(2, 0x08); s.add_effect(2, 0x62)   # Bakshi: Protection from Evil, regeneration
s.add_effect(3, 0x19); s.add_effect(3, 0x7b)   # Rhiannon: Invisibility, silver-or-magic
s.add_effect(5, 0x09); s.add_effect(5, 0x7d)   # Darkstar: Protection from Good, undead immunities
s.add_effect(4, 0x44); s.add_effect(6, 0x03)   # Sean: paralysing touch; Phineas: bonus against type 4
s.add_effect(1, 0x4f); s.add_effect(6, 0x50)   # Thrender: fire touch; Phineas: acid touch (riders on the second attack)
s.poke(1, 0x11b, 1)   # the leader: one hit point
