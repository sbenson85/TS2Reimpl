#pragma once
// The audio mixer's MMX tables (lane HQ): the static at 0x11032dc (XC slot 769, entry 0x1409ae: the constructor, then
// atexit(0xd98a7e), whose body does nothing whichever way its test of the flag goes: `cmp [0x11032d8], 0; jne
// 0x140fe0 (ret); ret`). The constructor asks whether the CPU has MMX and, when it does, lays out the shifted copies
// of two coefficient tables the MMX mixing code reads.
#include <cstdint>

class MmxResampleTables {
public:
    static constexpr uint32_t kStatic = 0x11032dc;    // the (empty) object
    static constexpr uint32_t kHaveMmx = 0x11032d8;   // dword: 1 with MMX, else 0
    static constexpr uint32_t kTables = 0x11033b8;    // 0x4000 bytes of 32-byte rows
    static constexpr uint32_t kSourceA = 0xeb33f0;    // .rdata: 15 rows of 0x40 bytes (the first 32 used), 16 shifts
    static constexpr uint32_t kSourceB = 0xeb37f0;    // .rdata: 17 rows, 16 shifts
    static constexpr uint32_t kTableBytes = 0x4000;

    // 0xc94c78 (thiscall, returns this): kHaveMmx = DetectMmx(); BuildTables() when set.
    MmxResampleTables *Construct();
    // 0xc94db1 (cdecl): cpuid leaf 1 (eax 1, ebx ecx edx 0 before it) under an SEH guard (a CPU without cpuid faults:
    // false), then, when EDX bit 23 (MMX) is set, emms under a second guard (a fault: false); true otherwise.
    static bool __cdecl DetectMmx();
    // 0xc98e00 (cdecl): for each shift s = 0..15 (the source start moved back 2*s bytes), 15 rows of table A, then the
    // same for 17 rows of table B: each row's first 32 bytes (rows 0x40 apart) appended to kTables.
    static void __cdecl BuildTables();
};
