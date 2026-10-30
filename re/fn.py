#!/usr/bin/env python3
# usage: fn.py SEG:OFF [c|a|both]  -> Ghidra decompilation / disassembly of one function, compacted
import sys, os, re
here = os.path.dirname(os.path.abspath(__file__)); key = sys.argv[1].lower(); mode = sys.argv[2] if len(sys.argv) > 2 else 'c'
def grab(path, mark):
    out = []; on = False
    for l in open(path, errors='replace'):
        if l.startswith(mark):
            on = (' @ ' + key) in l
            if on: out.append(l.rstrip()); continue
        if on: out.append(l.rstrip())
    return out
if mode in ('c', 'both'):
    for l in grab(here + '/ghidra/out/decomp.c', '//// FUNCTION'):
        if re.match(r'\s*(undefined\d?|byte|char|int|uint|bool|ushort|long|ulong|undefined1 \*|.*extraout.*|.*unaff.*)\s+\*?\w+(\s*\[\d+\])?;$', l): continue
        if l.strip(): print(l)
if mode in ('a', 'both'):
    for l in grab(here + '/ghidra/out/disasm.txt', ';;;; FUNCTION'): print(l)
