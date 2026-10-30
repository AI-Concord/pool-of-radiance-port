#!/usr/bin/env python3
# usage: spk2wav.py spk.tsv outprefix -> one WAV per burst of speaker activity in an emulator SPKLOG
# The headless emulator fires the game's timer every 20000 * divisor / 65536 instructions; the game
# runs it at PIT divisor 13B1h (236.7 Hz), which fixes the instruction-count-to-seconds scale.
import sys, struct, wave
RATE = 44100; INSN_PER_S = (20000 * 0x13b1 / 65536 + 1) * (1193182 / 0x13b1)
rows = [tuple(map(int, l.split())) for l in open(sys.argv[1])]
bursts = []; cur = []
for t, hz in rows:
    if cur and t - cur[-1][0] > 400000: bursts.append(cur); cur = []
    cur.append((t, hz))
if cur: bursts.append(cur)
seen = {}; k = 0
for b in bursts:
    sig = tuple(hz for t, hz in b)
    if len(b) < 10 or sig in seen: continue
    seen[sig] = 1; k += 1
    t0 = b[0][0]; ev = [((t - t0) / INSN_PER_S, hz) for t, hz in b] ; dur = ev[-1][0] + 0.02
    out = bytearray(); phase = 0.0; i = 0; hz = 0; level = 0.0
    for n in range(int(dur * RATE)):
        ts = n / RATE
        while i < len(ev) and ev[i][0] <= ts: hz = ev[i][1]; i += 1
        target = 0.0
        if hz > 30: phase = (phase + hz / RATE) % 1.0; target = 0.25 if phase < 0.5 else -0.25
        level += (target - level) * 0.35
        out += struct.pack('<h', int(level * 32767))
    w = wave.open('%s%d.wav' % (sys.argv[2], k), 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(bytes(out)); w.close()
    print('%s%d.wav  %.2f s, %d tone changes, %d..%d Hz' % (sys.argv[2], k, dur, len(b), min(h for t, h in b if h > 30), max(h for t, h in b)))
