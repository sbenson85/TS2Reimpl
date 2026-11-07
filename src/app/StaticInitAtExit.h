#pragma once
// The "atexit-only" XC entries (lane HC, 109 of 122; docs/subsystems/startup.md "Done in lane HC"): objects the image
// constant- or zero-initialises in .data, whose entry only registers the destructor thunk (`push thunk; call atexit;
// pop ecx; ret`, 12 bytes). The thunk is the object's destructor, inlined. Ours registers our own thunk, which runs our
// destructor of the same object, through BoundAtExit() in the same slot; the object's bytes stay the image's (nothing
// to construct). Standalone, each becomes a constinit static whose destructor is ours.
//
// Owned only where that destructor is ours: library code our toolchain provides (std::string, std::vector of a
// trivially destructible element), our verified RZMemory::Free, or a reference / owning pointer whose release is a
// virtual call on the object it holds. The 13 entries left (destructors not ours yet) are listed in startup.md.
#include "app/WinMainStart.h"

#include <cstddef>
#include <cstdint>

namespace staticinit {

enum class AtExitKind : uint8_t {
    String,      // a std::string (24 bytes): its destructor (a heap buffer freed when capacity > 15; size 0, capacity
                 // 15, the first character zeroed). Thunk e.g. 0xd90d40.
    Vector4,     // a std::vector of 4-byte elements (12 bytes): its destructor (the buffer freed, the three pointers
                 // zeroed; nothing when the first is null). Thunk 0xd92ce0.
    Vector8,     // the same with 8-byte elements. Thunk e.g. 0xd98730.
    PoolChunks,  // a fixed-size pool's chunk list (the word at the object; the pool's element size and chunk bytes sit
                 // at -8 / -4, its free list at +4): each chunk unlinked, then freed with RZMemory::Free (first word =
                 // the next chunk). Thunk e.g. 0xd8e740; the pool's allocation e.g. 0x9e0bb0.
    Release,     // a reference to a cIGZUnknown: Release (slot 2) when not null. Thunk e.g. 0xd9490d.
    ReleaseVirtualBase, // a reference to a class whose cIGZUnknown is a virtual base: the pointer adjusted by the
                 // vbtable's second word, then Release. Thunk e.g. 0xd953e0.
    Delete,      // an owning pointer: the deleting destructor (slot 0, flags 1) when not null. Thunk 0xd98904.
    Variant,     // a VARIANT: VariantClear (oleaut32). Thunk 0xd989f0.
};

struct AtExitStatic {
    uint32_t slot;
    uint32_t imageRva; // the image's initialiser
    uint32_t storage;  // the object (RVA)
    uint32_t thunk;    // the image's destructor thunk (RVA): what its entry registers
    AtExitKind kind;
};

size_t AtExitObjectSize(AtExitKind kind); // the bytes the thunk reads or writes from `storage`

void __cdecl InitAtExit(const void *atExitStatic);
const AtExitStatic *AtExitStatics(size_t *count);
const winmainstart::OwnedInitialiser *OwnedAtExits(size_t *count);

} // namespace staticinit
