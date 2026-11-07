// Owned XC entries that build statics of our classes (lane GQ): the pixel formats, cRZCriticalSection and
// cRZRandom (docs/subsystems/startup.md "Stage 3: the static initialisers"). Hosted, the objects stay in the
// image's storage, where game code reads them; each entry runs in its own slot (the host's skip list), and the
// destructors go to BoundAtExit() in that slot. Standalone, each becomes a plain static of its class.
#include "app/StaticInit.h"

#include "game/game.h"
#include "gz/PixelFormat.h"
#include "gz/PixelFormatStore.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZRandom.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

namespace staticinit {

namespace {

using nGZGraphic4::cPixelFormat;
using nGZGraphic4::kStaticFormatSpecs;
using nGZGraphic4::StaticKind;

int __cdecl OurAtExit(void(__cdecl *function)())
{
    return std::atexit(function);
}

RegisterAtExit gRegisterAtExit = &OurAtExit;

// Slot order. Initialiser, object and table index from db/initializers.jsonl (the "PixelFormat" class-inline
// entries), the fixture's initialiser list (tests/pixel_format_fixture.h) and the image store's object list
// (src/gz/PixelFormatImageStore.cpp); the tests check each against the image's slot and initialiser.
constexpr PixelFormatStatic kPixelFormats[] = {
    {19, 0x137cf0, 0xfec094, kUnknownFormatSpec}, // atexit 0xd8fb70
    {20, 0x137ca0, 0x10c9790, 0},  // atexit 0xd8fab0
    {21, 0x137160, 0x10c97c4, 1},  // atexit 0xd8ef10
    {22, 0x137e60, 0x10c97f8, 2},  // atexit 0xd8fe70
    {23, 0x137c70, 0x10c982c, 3},  // atexit 0xd8fa50
    {24, 0x137dc0, 0x10c9860, 4},  // atexit 0xd8fcf0
    {25, 0x136f90, 0x10c9894, 5},  // atexit 0xd8ea90
    {26, 0x137070, 0x10c98c8, 6},  // atexit 0xd8ecd0
    {27, 0x137c40, 0x10c98fc, 7},  // atexit 0xd8f9f0
    {28, 0x1370a0, 0x10c9930, 8},  // atexit 0xd8ed30
    {29, 0x137130, 0x10c9964, 9},  // atexit 0xd8eeb0
    {30, 0x137df0, 0x10c9998, 10}, // atexit 0xd8fd50
    {31, 0x136fe0, 0x10c99cc, 15}, // atexit 0xd8eb50
    {32, 0x137b60, 0x10c9a00, 19}, // atexit 0xd8f750
    {33, 0x137b20, 0x10c9a2c, 46}, // atexit 0xd8f690
    {34, 0x1370f0, 0x10c9a58, 20}, // atexit 0xd8edf0
    {35, 0x137050, 0x10c9a84, 21}, // atexit 0xd8ec70
    {36, 0x137c00, 0x10c9ab0, 49}, // atexit 0xd8f930
    {37, 0x137ac0, 0x10c9ae0, 50}, // atexit 0xd8f570
    {38, 0x136f70, 0x10c9b10, 51}, // atexit 0xd8ea30
    {39, 0x137c20, 0x10c9b40, 52}, // atexit 0xd8f990
    {40, 0x137ae0, 0x10c9b70, 53}, // atexit 0xd8f5d0
    {41, 0x137030, 0x10c9ba0, 54}, // atexit 0xd8ec10
    {42, 0x137d50, 0x10c9bd0, 28}, // atexit 0xd8fbd0
    {43, 0x137e90, 0x10c9bf8, 30}, // atexit 0xd8fed0
    {44, 0x1372d0, 0xfec0b4, 32},  // atexit 0xd8f330 (DXT1, constant-initialised)
    {45, 0x137460, 0xfec0e0, 33},  // atexit 0xd8f390 (DXT2)
    {46, 0x1375f0, 0xfec10c, 34},  // atexit 0xd8f3f0 (DXT3)
    {47, 0x137780, 0xfec138, 35},  // atexit 0xd8f450 (DXT4)
    {48, 0x137910, 0xfec164, 36},  // atexit 0xd8f4b0 (DXT5)
    {49, 0x137290, 0x10c9c20, 38}, // atexit 0xd8f270
    {50, 0x1371b0, 0x10c9c50, 39}, // atexit 0xd8efd0
    {51, 0x137230, 0x10c9c80, 40}, // atexit 0xd8f150
    {52, 0x137270, 0x10c9cb0, 41}, // atexit 0xd8f210
    {53, 0x137250, 0x10c9ce0, 42}, // atexit 0xd8f1b0
    {54, 0x1371d0, 0x10c9d10, 43}, // atexit 0xd8f030
    {55, 0x137210, 0x10c9d40, 45}, // atexit 0xd8f0f0
    {56, 0x137110, 0x10c9d70, 17}, // atexit 0xd8ee50
    {57, 0x137ba0, 0x10c9d90, 18}, // atexit 0xd8f810
    {58, 0x1371f0, 0x10c9db0, 37}, // atexit 0xd8f090
    {59, 0x1372b0, 0x10c9dd0, 44}, // atexit 0xd8f2d0
    {60, 0x137b80, 0x10c9df0, 48}, // atexit 0xd8f7b0
    {61, 0x137190, 0x10c9e10, 55}, // atexit 0xd8ef70
    {62, 0x136fc0, 0x10c9e30, 11}, // atexit 0xd8eaf0
    {63, 0x1370d0, 0x10c9e50, 12}, // atexit 0xd8ed90
    {64, 0x137e20, 0x10c9e70, 13}, // atexit 0xd8fdb0
    {65, 0x137aa0, 0x10c9e90, 14}, // atexit 0xd8f510
    {66, 0x136f50, 0x10c9eb0, 16}, // atexit 0xd8e9d0
    {67, 0x137da0, 0x10c9ed0, 22}, // atexit 0xd8fc90
    {68, 0x137b40, 0x10c9ef0, 23}, // atexit 0xd8f6f0
    {69, 0x137e40, 0x10c9f10, 24}, // atexit 0xd8fe10
    {70, 0x137be0, 0x10c9f30, 25}, // atexit 0xd8f8d0
    {71, 0x137d80, 0x10c9f50, 26}, // atexit 0xd8fc30
    {72, 0x137010, 0x10c9f70, 27}, // atexit 0xd8ebb0
    {73, 0x137cd0, 0x10c9f90, 29}, // atexit 0xd8fb10
    {74, 0x137b00, 0x10c9fb0, 31}, // atexit 0xd8f630
    {75, 0x137bc0, 0x10c9fd0, 47}, // atexit 0xd8f870
};

constexpr LockStatic kLocks[] = {
    {2, 0x138342, 0x10cb090},   // atexit 0xd9071c
    {9, 0x13836f, 0x10cb5cc},   // atexit 0xd90741
    {114, 0x1384f6, 0x10cc3f4}, // atexit 0xd90cd4
    {126, 0x138754, 0x10cc5e8}, // atexit 0xd90d12
    {501, 0x13e0ba, 0x10e6918}, // atexit 0xd95120
    {528, 0x13e3ef, 0x10f4ff8}, // atexit 0xd952b0
    {535, 0x13e4a8, 0x10f59e0}, // atexit 0xd953c0
    {634, 0x13f5ac, 0x10fab88}, // atexit 0xd96954
    {639, 0x13f5da, 0x10fac80}, // atexit 0xd96992
    {642, 0x13f632, 0x10fada0}, // atexit 0xd969cf
};

constexpr RandomStatic kRandoms[] = {
    {95, 0x1382d8, 0x10cafc0, 0x1382e4},
    {185, 0x1392e0, 0x10d93f0, 0x1392ec},
    {485, 0x13de2c, 0x10deeb0, 0x13de38},
    {495, 0x13dfc0, 0x10df4b8, 0x13dfcc},
    {703, 0x1401b0, 0x10fd138, 0x1401bc},
};

// What the image's thunk for a pixel format does: the class's deleting destructor without the free (its BaseT
// vtable, then the name emptied, its heap buffer freed).
void DestroyPixelFormat(const PixelFormatStatic &e)
{
    using namespace nGZGraphic4;
    void *object = GameAddress<void>(e.storage);
    if (e.spec == kUnknownFormatSpec) {
        static_cast<cPixelFormatUnknown *>(object)->DeletingDestructor(0);
        return;
    }
    switch (kStaticFormatSpecs[e.spec].kind) {
    case StaticKind::Base: static_cast<cPixelFormat *>(object)->DeletingDestructor(0); break;
    case StaticKind::ARGB: static_cast<cPixelFormatARGB *>(object)->DeletingDestructor(0); break;
    case StaticKind::AlphaLuminance: static_cast<cPixelFormatAlphaLuminance *>(object)->DeletingDestructor(0); break;
    case StaticKind::YUV: static_cast<cPixelFormatYUV *>(object)->DeletingDestructor(0); break;
    case StaticKind::DepthStencil: static_cast<cPixelFormatDepthStencil *>(object)->DeletingDestructor(0); break;
    case StaticKind::ABGRFloat: static_cast<cPixelFormatABGRFloat *>(object)->DeletingDestructor(0); break;
    case StaticKind::DXT: static_cast<cPixelFormatDXT *>(object)->DeletingDestructor(0); break;
    }
}

void DestroyLock(const LockStatic &e)
{
    GameAddress<cRZCriticalSection>(e.storage)->Destruct();
}

// atexit takes no argument: one thunk per entry, by its index in the table.
template <size_t I> void __cdecl PixelFormatThunk() { DestroyPixelFormat(kPixelFormats[I]); }
template <size_t I> void __cdecl LockThunk() { DestroyLock(kLocks[I]); }

using Thunk = void(__cdecl *)();
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> PixelFormatThunks(std::index_sequence<I...>) { return {&PixelFormatThunk<I>...}; }
template <size_t... I> constexpr std::array<Thunk, sizeof...(I)> LockThunks(std::index_sequence<I...>) { return {&LockThunk<I>...}; }
constexpr auto kPixelFormatThunks = PixelFormatThunks(std::make_index_sequence<std::size(kPixelFormats)>());
constexpr auto kLockThunks = LockThunks(std::make_index_sequence<std::size(kLocks)>());

// The skip-list entries of this file (OwnedInitialisers sorts all kinds by slot).
constexpr size_t kOwnedCount = std::size(kPixelFormats) + std::size(kLocks) + std::size(kRandoms);
constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, kOwnedCount> out{};
    size_t n = 0;
    for (const auto &e : kPixelFormats) out[n++] = {e.slot, e.imageRva, &InitPixelFormat, &e};
    for (const auto &e : kLocks) out[n++] = {e.slot, e.imageRva, &InitLock, &e};
    for (const auto &e : kRandoms) out[n++] = {e.slot, e.imageRva, &InitRandom, &e};
    return out;
}();

} // namespace

void BindAtExit(RegisterAtExit registerAtExit)
{
    gRegisterAtExit = registerAtExit;
}

RegisterAtExit BoundAtExit()
{
    return gRegisterAtExit;
}

void __cdecl InitPixelFormat(const void *pixelFormatStatic)
{
    const auto &e = *static_cast<const PixelFormatStatic *>(pixelFormatStatic);
    auto *format = GameAddress<cPixelFormat>(e.storage);
    if (e.spec == kUnknownFormatSpec) {
        // 0x137cf0: the object is constant-initialised in .data (vtable, id, the name's size 7 and capacity 15);
        // the entry copies the name's seven characters into the string's inline buffer (the first member of an
        // MSVC std::string), nothing else (the terminator is .data's zero).
        std::memcpy(reinterpret_cast<char *>(&format->mName), "UNKNOWN", 7);
    } else if (kStaticFormatSpecs[e.spec].kind == StaticKind::DXT) {
        // 0x1372d0 and the next four: the object is constant-initialised; the entry move-assigns the name
        // "DXT" + Sprintf("%u", version) (the old name freed first, as std::string's move assignment does).
        format->mName = std::string("DXT") + std::to_string(kStaticFormatSpecs[e.spec].args[0]);
    } else {
        nGZGraphic4::ConstructStaticFormat(format, kStaticFormatSpecs[e.spec]);
    }
    gRegisterAtExit(kPixelFormatThunks[&e - kPixelFormats]);
}

void __cdecl InitLock(const void *lockStatic)
{
    const auto &e = *static_cast<const LockStatic *>(lockStatic);
    GameAddress<cRZCriticalSection>(e.storage)->Construct();
    gRegisterAtExit(kLockThunks[&e - kLocks]);
}

void __cdecl InitRandom(const void *randomStatic)
{
    const auto &e = *static_cast<const RandomStatic *>(randomStatic);
    GameAddress<cRZRandom>(e.storage)->ConstructAt(UINT32_MAX, e.callSite);
}

const PixelFormatStatic *PixelFormatStatics(size_t *count)
{
    *count = std::size(kPixelFormats);
    return kPixelFormats;
}

const LockStatic *LockStatics(size_t *count)
{
    *count = std::size(kLocks);
    return kLocks;
}

const RandomStatic *RandomStatics(size_t *count)
{
    *count = std::size(kRandoms);
    return kRandoms;
}

const winmainstart::OwnedInitialiser *OwnedObjects(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
