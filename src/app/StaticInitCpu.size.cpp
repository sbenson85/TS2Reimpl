// The CPU feature detection slot 765's entry calls (0xee07f, 0xee0a9, 0xee0d1; size-built in the image; declared in
// src/app/StaticInitContainers.h, lane HF's entry stores the result).
#include "app/StaticInitContainers.h"

#include <windows.h>
#include <intrin.h>

namespace staticinit {

uint32_t __cdecl CpuidAvailable()
{
    // The ID flag (bit 21) can be toggled when the CPU has CPUID.
    const uintptr_t flags = __readeflags();
    __writeeflags(flags ^ 0x200000);
    const uintptr_t toggled = __readeflags();
    __writeeflags(flags);
    return static_cast<uint32_t>(toggled ^ flags);
}

void __cdecl CpuidQuery(uint32_t regs[4], uint32_t leaf)
{
    int out[4];
    __cpuid(out, static_cast<int>(leaf));
    for (int i = 0; i < 4; ++i) regs[i] = static_cast<uint32_t>(out[i]);
}

uint32_t CpuFeatureFlags(uint32_t platformId, uint32_t minorVersion)
{
    const bool windows95 = platformId == VER_PLATFORM_WIN32_WINDOWS && minorVersion == 0;
    uint32_t flags = 1;
    if (!CpuidAvailable()) return flags;
    uint32_t regs[4];
    CpuidQuery(regs, 0);
    if (regs[0] < 1) return flags;
    CpuidQuery(regs, 1);
    if ((regs[3] & (1u << 25)) && !windows95) flags = 3;
    CpuidQuery(regs, 0x80000001u);
    if (regs[3] & 0x80000000u) flags |= 4;
    return flags;
}

uint32_t __cdecl DetectCpuFeatures(GetVersionExFn getVersion)
{
    OSVERSIONINFOA os;
    os.dwOSVersionInfoSize = sizeof(os);
    getVersion(&os);
    return CpuFeatureFlags(os.dwPlatformId, os.dwMinorVersion);
}

#pragma warning(suppress : 4996) // GetVersionExA is deprecated; the image's function asks it
uint32_t __cdecl DetectCpuFeatures() { return DetectCpuFeatures(&GetVersionExA); }

} // namespace staticinit
