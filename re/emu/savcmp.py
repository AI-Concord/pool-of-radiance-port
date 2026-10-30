#!/usr/bin/env python3
# usage: savcmp.py a b -> exit 0 if the files match; for SAVGAM?.DAT, ignores the bytes of the trailing 8 x 29h name
# table that the game fills from uninitialised stack (unused entries, and bytes past each name's length)
import sys, os
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read() if os.path.exists(sys.argv[2]) else None
if b is None or len(a) != len(b): sys.exit(1)
if not os.path.basename(sys.argv[1]).upper().startswith('SAVGAM'): sys.exit(a != b)
T = len(a) - 0x148; n = a[T - 1]
for i in range(len(a)):
    if a[i] != b[i]:
        if i >= T and ((i - T) // 0x29 >= n or (i - T) % 0x29 > a[T + (i - T) // 0x29 * 0x29]): continue
        sys.exit(1)
