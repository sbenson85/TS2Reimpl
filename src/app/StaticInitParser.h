#pragma once
// The command parser's statics (lane HP, package H4 in docs/subsystems/startup.md):
//   slot 675 (0x13ff80): the evaluator's name table at 0x10fb30c (EvalFunctionTable's constructor 0xb094b0),
//            atexit thunk 0xd97450 (the vector's destructor, inlined);
//   slot 773 (0x1409f0): the log configuration's parser cGZLogParser at 0x1107510 (constructor 0xcac2b0), atexit
//            thunk 0xd98bc0 (`mov ecx, object; jmp 0xcac4c0`, the destructor).
// Ours builds each object with our constructor in the image's storage, in the entry's slot, and registers our own
// thunk (our destructor) through BoundAtExit() there, so the exit list keeps the original's LIFO order. Standalone,
// each becomes a plain static of the code that reads it.
#include "app/WinMainStart.h"

#include <cstddef>
#include <cstdint>

namespace staticinit {

struct ParserStatic {
    uint32_t slot;
    uint32_t imageRva; // the image's initialiser
    uint32_t storage;  // the object (RVA)
    uint32_t thunk;    // the image's destructor thunk (RVA)
};

void __cdecl InitParserStatic(const void *parserStatic);
const ParserStatic *ParserStatics(size_t *count);
const winmainstart::OwnedInitialiser *OwnedParsers(size_t *count);

// The one-byte flag objects of XC slots 507 and 508 (0x10ecd01, 0x10f4d10; lane HD owns those entries): their
// constructor 0x90a915 (__thiscall, returns this) stores 1.
struct StaticFlag {
    uint8_t mSet;
    StaticFlag *Construct();
};

} // namespace staticinit
