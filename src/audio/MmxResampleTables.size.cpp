// The size-profile members of src/audio/MmxResampleTables.h (lane HQ).
#include "audio/MmxResampleTables.h"

#include "game/game.h"

#include <windows.h>
#include <intrin.h>

MmxResampleTables *MmxResampleTables::Construct()
{
    const bool mmx = DetectMmx();
    *GameAddress<uint32_t>(kHaveMmx) = mmx;
    if (mmx) BuildTables();
    return this;
}

bool __cdecl MmxResampleTables::DetectMmx()
{
    bool ok = true;
    int regs[4] = {};
    __try {
        __cpuid(regs, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok || !(regs[3] & 0x800000)) return false;
    __try {
        _mm_empty();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}
