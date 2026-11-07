#pragma once
// The cIGZAny statics (lane GZ, 36 XC entries; docs/subsystems/startup.md "Done in lane GZ"): AnyBoolean, AnyUint32
// and AnyFloat32 objects the image constant-initialises in .data (vtable and value in the file), alone or as the value
// of a {name, value} table row. The image's entry only registers the destructor thunk (`push thunk; call atexit; pop
// ecx; ret`, 12 bytes); the thunk (`mov [object], 0xd9c654; ret`) is the inlined destructor: cIGZAny's vtable stored.
// Ours constructs our class in place (our vtable, the same value) and registers our thunk, which runs our
// destructor, through BoundAtExit() in the same slot. Standalone, each becomes a constinit static of its class.
#include "app/WinMainStart.h"

#include <cstddef>
#include <cstdint>

namespace staticinit {

enum class AnyKind : uint8_t {
    Boolean, // gz::AnyBoolean (vtable 0xd9c670): the byte at +4
    Uint32,  // gz::AnyUint32 (0xd9f534)
    Float32, // gz::AnyFloat32 (0xda3d70): the value's bits
};

struct AnyStatic {
    uint32_t slot;
    uint32_t imageRva; // the image's initialiser
    uint32_t storage;  // the object (RVA)
    uint32_t thunk;    // the image's destructor thunk (RVA): what its entry registers
    AnyKind kind;
    uint32_t bits;     // the value as .data holds it (Boolean: the byte)
};

void __cdecl InitAny(const void *anyStatic);
const AnyStatic *AnyStatics(size_t *count);
const winmainstart::OwnedInitialiser *OwnedAnys(size_t *count);

} // namespace staticinit
