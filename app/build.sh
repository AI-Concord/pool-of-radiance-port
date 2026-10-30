#!/bin/sh
# Builds "Pool of Radiance.app" into ../build using the command line tools only.
set -e
cd "$(dirname "$0")"
OUT=../build; APP="$OUT/Pool of Radiance.app"; GD="$APP/Contents/Resources/GameData"
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$GD" $OUT/obj
rm -f $OUT/obj/*.o
# Game core: the port in C (port/), with the platform layer of re/emu/emu.c (memory image, BIOS/DOS services, EGA,
# timer, keyboard); the program file is loaded only as the game's initial data.
[ -f ../re/out/POOLFLAT.EXE ] || (cd ../re && python3 -I unexepack.py ../original/START.EXE out/START_UNPACKED.EXE && python3 -I flatten.py out/START_UNPACKED.EXE ../original/GAME.OVR out/POOLFLAT.EXE out/flat_map.tsv)
# The commit this was built from, kept in session recordings so a replay can use the same port.
BID=$(git -C .. rev-parse --short HEAD 2>/dev/null || echo dev); git -C .. diff --quiet HEAD -- 'port/*.c' 'port/*.h' 're/emu/*.c' 'app/Windows/*' 'app/App/*' 2>/dev/null || BID="$BID+changes"
# NATIVE_ONLY: without the 8086 interpreter; the game is entirely C (see docs, "The native build").
cc -O2 -w -DEMU_LIB -DNATIVE_ONLY -DBUILD_ID="\"$BID\"" -c ../re/emu/emu.c -o $OUT/obj/emu.o
PORT_SRC=$(grep '^PORT_SRC' ../port/Makefile | cut -d= -f2)
for f in ega.c hooks.c $PORT_SRC; do cc -O2 -w -c ../port/$f -o $OUT/obj/${f%.c}.o; done
swiftc -O -swift-version 5 -import-objc-header Engine/engine.h App/*.swift $OUT/obj/*.o \
  -framework AppKit -framework Metal -framework MetalKit -framework AVFoundation -o "$APP/Contents/MacOS/PoolOfRadiance"
cp ../original/*.DAX ../original/ITEMS ../original/GAME.OVR ../original/POOL.CFG ../re/out/POOLFLAT.EXE "$GD/"
[ -f Resources/PoolOfRadiance.icns ] && cp Resources/PoolOfRadiance.icns "$APP/Contents/Resources/"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>Pool of Radiance</string>
<key>CFBundleDisplayName</key><string>Pool of Radiance</string>
<key>CFBundleIdentifier</key><string>io.github.ai-concord.poolofradiance</string>
<key>CFBundleExecutable</key><string>PoolOfRadiance</string>
<key>CFBundleIconFile</key><string>PoolOfRadiance</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>0.1</string>
<key>CFBundleVersion</key><string>1</string>
<key>PoolradBuild</key><string>$BID</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSHighResolutionCapable</key><true/>
<key>NSPrincipalClass</key><string>NSApplication</string>
<key>LSApplicationCategoryType</key><string>public.app-category.role-playing-games</string>
</dict></plist>
PLIST
codesign --force --sign - "$APP" >/dev/null 2>&1 || true
echo "built $APP"
