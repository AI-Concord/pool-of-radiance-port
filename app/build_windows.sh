#!/bin/sh
# Cross-compiles Pool of Radiance for Windows 10/11 (x64, Direct3D 12) from macOS with MinGW-w64
# (brew install mingw-w64). Output: build/windows/Pool of Radiance/ (PoolOfRadiance.exe + GameData) and a zip of it.
set -e
cd "$(dirname "$0")"
CC=x86_64-w64-mingw32-gcc; WR=x86_64-w64-mingw32-windres
OUT=../build/windows; APP="$OUT/Pool of Radiance"; OBJ=$OUT/obj
rm -rf "$APP" "$OUT/Pool of Radiance (Windows).zip"; mkdir -p "$APP/GameData" $OBJ; rm -f $OBJ/*.o
[ -f ../re/out/POOLFLAT.EXE ] || (cd ../re && python3 -I unexepack.py ../original/START.EXE out/START_UNPACKED.EXE && python3 -I flatten.py out/START_UNPACKED.EXE ../original/GAME.OVR out/POOLFLAT.EXE out/flat_map.tsv)
BID=$(git -C .. rev-parse --short HEAD 2>/dev/null || echo dev); git -C .. diff --quiet HEAD -- 'port/*.c' 'port/*.h' 're/emu/*.c' 'app/Windows/*' 'app/App/*' 2>/dev/null || BID="$BID+changes"
# Icon: PNG-compressed sizes in an .ico, made from the Mac icon.
if [ ! -f Windows/PoolOfRadiance.ico ] || [ Resources/icon1024.png -nt Windows/PoolOfRadiance.ico ]; then
  T=$(mktemp -d); for s in 16 32 48 64 128 256; do sips -z $s $s Resources/icon1024.png --out "$T/$s.png" >/dev/null; done
  python3 -I - "$T" Windows/PoolOfRadiance.ico <<'PY'
import sys, struct
d, out = sys.argv[1], sys.argv[2]; sizes = [16, 32, 48, 64, 128, 256]
data = [open('%s/%d.png' % (d, s), 'rb').read() for s in sizes]
off = 6 + 16 * len(sizes); hdr = struct.pack('<HHH', 0, 1, len(sizes)); body = b''
for s, b in zip(sizes, data):
    hdr += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(b), off); off += len(b); body += b
open(out, 'wb').write(hdr + body)
PY
  rm -rf "$T"
fi
# Game core: the port in C with the platform layer of re/emu/emu.c, without the 8086 interpreter.
$CC -O2 -w -DEMU_LIB -DNATIVE_ONLY -DBUILD_ID="\"$BID\"" -c ../re/emu/emu.c -o $OBJ/emu.o
PORT_SRC=$(grep '^PORT_SRC' ../port/Makefile | cut -d= -f2)
for f in ega.c hooks.c $PORT_SRC; do $CC -O2 -w -c ../port/$f -o $OBJ/${f%.c}.o; done
# Front end: Win32 window, Direct3D 12, waveOut.
$WR -I Windows Windows/poolrad.rc -O coff -o $OBJ/res.o
$CC -O2 -Wall -Wno-missing-braces -Wno-format-truncation -DPOOLRAD_BUILD="\"$BID\"" -c Windows/winmain.c -o $OBJ/winmain.o
$CC -O2 -mwindows -static $OBJ/*.o -o "$APP/PoolOfRadiance.exe" \
    -ld3d12 -ldxgi -ld3dcompiler -ldxguid -luuid -lwinmm -lshell32 -ladvapi32 -lole32
x86_64-w64-mingw32-strip "$APP/PoolOfRadiance.exe"
cp ../original/*.DAX ../original/ITEMS ../original/GAME.OVR ../original/POOL.CFG ../re/out/POOLFLAT.EXE "$APP/GameData/"
cat > "$APP/README.txt" <<TXT
Pool of Radiance (SSI, 1988), native port for Windows 10 and 11 (Direct3D 12).
Build $BID. Run PoolOfRadiance.exe; keep the GameData folder next to it.

Saved games: %APPDATA%\Pool of Radiance
Session recordings for bug reports: %APPDATA%\Pool of Radiance Recordings
Keys: as on DOS. Alt+Enter full screen, F12 marks a problem in the recording.
TXT
(cd "$OUT" && rm -f "Pool of Radiance (Windows).zip" && zip -qr "Pool of Radiance (Windows).zip" "Pool of Radiance")
echo "built $APP/PoolOfRadiance.exe"
