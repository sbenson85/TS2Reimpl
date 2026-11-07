#pragma once
// The "object" XC entries owned by lane HD (docs/subsystems/startup.md "Done in lane HD"): statics the image builds by
// calling a constructor on the global (`mov ecx, object; call ctor`, arguments pushed first), most registering a
// destructor thunk with the CRT's atexit. Ours builds the same object in the image's storage with our code, in the
// entry's slot, and registers our own thunk (one per row) through BoundAtExit() in that slot, so the exit list keeps
// the original's LIFO order. Standalone, each becomes a plain static of the code that reads it.
//
// Owned only where the constructor and the destructor are ours: our verified functions (NameValueTable::Construct,
// cRZDate, cRZStopwatch, StringKeyParser::CrcTables, cRZString, cRZFileName::Destruct, cRZCriticalSection,
// RZMemory::Free), library code our toolchain provides (std::vector, std::list), or a constructor of a few stores
// written here from the image's code (the flag byte, the fixed-size pool's two sizes, the zeroed vector). The entries
// left (constructors or destructors not ours yet) are listed in startup.md.
#include "app/WinMainStart.h"

#include <cstddef>
#include <cstdint>

namespace staticinit {

enum class ConstructedKind : uint8_t {
    NameValues, // 55: NameValueTable::Construct(entries) (0xaf6560): a vector<pair<std::string, uint32_t>> from the
                // null-name-terminated {name, value} array at `arg`; thunk: the vector's destructor (inlined, e.g.
                // 0xd92430, or `mov ecx, object; jmp 0x3c6bf0`).
    Floats,     // 27: StaticObjectStub::Construct (0x57c3c0: touches nothing), then `arg` floats stored (3: three
                // dword stores; 4: a 16-byte copy from .rdata or xorps zero). No destructor.
    Date,       // 2: cRZDate::ConstructDays(arg, 0) (0x1b96af). No destructor.
    FileName,   // 2: cRZFileName's default constructor (0x1c7f30: five cRZString() at +0, +0x20, ... +0x80); thunk:
                // `mov ecx, object; jmp 0x1c0eb2` (cRZFileName::Destruct).
    Flag,       // 2: a one-byte object whose constructor (0x90a915) stores 1; thunk: an empty destructor (0x140fe0).
    Stopwatch,  // 1: cRZStopwatch::Construct(arg) (0x1c9030); thunk: an empty destructor (0x140fe0).
    CrcTables,  // 1: `mov ecx, object; jmp 0x9cbb60` (StringKeyParser::CrcTables::Initialize). No destructor.
    Vector20,   // 1: 16 bytes zeroed (0xc0a664), a std::vector of 20-byte trivially destructible elements and one more
                // word; thunk 0xd92580: the vector's destructor inlined.
    FixedPool,  // 5: a fixed-size pool (0x945bd6, ret 4): element size max(arg, 4) at +0, chunk bytes at +4 (element
                // size times 128 below 0x200, 32 below 0x800, else 16); its chunk list at +8 (zero in the file). Thunk:
                // `mov ecx, object; jmp 0x940e4e`: each chunk unlinked, then RZFree (first word = the next chunk).
    ListLock,   // 1: a std::list<uint32_t> (library constructor 0x141d1c) and a cRZCriticalSection at +8 (0x1ce3f3);
                // thunk `mov ecx, object; jmp 0x1ce437`: the lock's destructor, then the list's (_Tidy 0x1b2cb6).
};

struct ConstructedStatic {
    uint32_t slot;
    uint32_t imageRva; // the image's initialiser
    uint32_t storage;  // the object (RVA)
    uint32_t thunk;    // the image's destructor thunk (RVA), 0 when the entry registers none
    ConstructedKind kind;
    uint32_t arg;      // NameValues: the entry array (RVA); Floats: 3 or 4; Date: the days; Stopwatch: the mode;
                       // FixedPool: the element size asked for
    float value[4];    // Floats only
};

size_t ConstructedObjectSize(const ConstructedStatic &e); // the bytes the entry and its thunk write from `storage`

void __cdecl InitConstructed(const void *constructedStatic);
const ConstructedStatic *ConstructedStatics(size_t *count);
const winmainstart::OwnedInitialiser *OwnedConstructed(size_t *count);

} // namespace staticinit
