#pragma once
// The original's 56 static pixel formats and its static unknown format, built the way the game builds
// them: by running the image's own static initialisers in the mapped image (the harness doesn't run the
// image's startup). Shared by the pixel format tests that need the original's objects.
#include "difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "gz/PixelFormatStore.h"

namespace pftest {

// The original's static initialisers (RVAs), in the order of the table at 0xd9cf78. Each pushes the
// constructor arguments, loads the object into ecx, calls the constructor and registers the destructor
// with atexit; the five DXT ones (index 32-36) and the unknown format's (kUnknownInitialiser) only name
// objects that are constant-initialised in .data.
inline constexpr uint32_t kInitialisers[nGZGraphic4::kFormatCount] = {
    0x137ca0, 0x137160, 0x137e60, 0x137c70, 0x137dc0, 0x136f90, 0x137070, 0x137c40, 0x1370a0, 0x137130, 0x137df0, 0x136fc0, 0x1370d0, 0x137e20,
    0x137aa0, 0x136fe0, 0x136f50, 0x137110, 0x137ba0, 0x137b60, 0x1370f0, 0x137050, 0x137da0, 0x137b40, 0x137e40, 0x137be0, 0x137d80, 0x137010,
    0x137d50, 0x137cd0, 0x137e90, 0x137b00, 0x1372d0, 0x137460, 0x1375f0, 0x137780, 0x137910, 0x1371f0, 0x137290, 0x1371b0, 0x137230, 0x137270,
    0x137250, 0x1371d0, 0x1372b0, 0x137210, 0x137b20, 0x137bc0, 0x137b80, 0x137c00, 0x137ac0, 0x136f70, 0x137c20, 0x137ae0, 0x137030, 0x137190,
};
inline constexpr uint32_t kUnknownInitialiser = 0x137cf0;
inline constexpr uint32_t kOriginalTable = 0xd9cf78;   // 56 pointers to the static formats
inline constexpr uint32_t kOriginalUnknown = 0xfec094; // the static cPixelFormatUnknown
inline constexpr uint32_t kOriginalCreateIterator = 0x15e860;

inline int __cdecl StubAtexit(void (*)()) { return 0; }

// Runs the initialisers once, with atexit (0x824cbe) stubbed out: registering exit handlers in the
// image's CRT, which the harness doesn't run, would be wrong.
inline void EnsureStatics(const difftest::Context &ctx)
{
    static bool done = false;
    if (done) return;
    done = true;
    uint8_t *site = ctx.image.At<uint8_t>(0x824cbe);
    uint8_t saved[5];
    std::memcpy(saved, site, 5);
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StubAtexit) - (reinterpret_cast<uintptr_t>(site) + 5));
    site[0] = 0xE9;
    std::memcpy(site + 1, &rel, 4);
    for (uint32_t rva : kInitialisers) ctx.Original<void(__cdecl *)()>(rva)();
    ctx.Original<void(__cdecl *)()>(kUnknownInitialiser)();
    std::memcpy(site, saved, 5);
}

// The original's table, read from the mapped image (relocated pointers).
inline std::vector<nGZGraphic4::cPixelFormat *> OriginalTable(const difftest::Context &ctx)
{
    std::vector<nGZGraphic4::cPixelFormat *> v(nGZGraphic4::kFormatCount);
    std::memcpy(v.data(), ctx.image.At(kOriginalTable), sizeof(void *) * nGZGraphic4::kFormatCount);
    return v;
}

inline nGZGraphic4::cPixelFormat *OriginalUnknown(const difftest::Context &ctx) { return ctx.image.At<nGZGraphic4::cPixelFormat>(kOriginalUnknown); }

} // namespace pftest
