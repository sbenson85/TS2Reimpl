#pragma once
// Stack-balance diagnostic (CMake option TS2_ESPCHECK, docs: tools/stackcheck.py). That build compiles
// our code with /Od /RTCs: after every call through a pointer (OriginalFunction, VirtualCall, VirtualSlot,
// the ifaces) and at every function's end, the compiler checks that esp is where the declared types say.
// A call whose declared convention or argument bytes differ from what the callee pops fails that check;
// built without frame pointers (/Oy) the same mismatch corrupts the stack. InstallEspCheckReporter makes
// each failure a line for `sink` (the call stack as module RVAs: ours to look up in the linker map, the
// game's as image RVAs) and lets the program continue. Without TS2_ESPCHECK it does nothing.
#include <cstdint>

void InstallEspCheckReporter(void (*sink)(const char *line), uintptr_t imageBase, uintptr_t imageEnd);
