// Fixes of bugs in the 1988 game (docs: Bugs found). Each is used as  if (FIX) { corrected } else { original }.
// On in the Mac and Windows apps (POOLRAD_ORIGINAL=1 turns them off); off in the headless emulator unless
// POOLRAD_FIXES=1, so the comparison tests still check the port against the original byte for byte.
#ifndef FIXES_H
#define FIXES_H
int game_fixes(void);
#define FIX (game_fixes())
#endif
