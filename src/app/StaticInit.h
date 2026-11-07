#pragma once
// The image's static initialisers (XC entries) our build owns: stage 3 of the standalone build
// (docs/subsystems/startup.md "Stage 3: the static initialisers"; the entries and their kinds are
// db/initializers.jsonl). While the image is hosted, the objects stay in the image's storage, since game code
// reads them there: the host's XC runner (winmainstart::RunInitialisersOwned, under [Startup] OwnStart=1/2)
// calls ours in the owned entry's slot instead of the image's, and ours writes the same values to the same
// globals. In the standalone build each becomes a plain C++ static of the code that reads it.
//
// Owned so far (428 entries): the "key-hash" kind (111, lane GL), keys made from a string constant with the CRC
// functions of StringKeyParser (verified; ours), which also build the CRC tables on their first call, as the
// original's do; the pixel formats, cRZCriticalSection and cRZRandom statics (72), 25 "value" entries and the 85
// "std-string" entries (lane GQ); the 135 cRZString "class-inline" entries (lane GY); the 36 cIGZAny statics
// (lane GZ, StaticInitAny.h); 109 of the 122 "atexit-only" entries (lane HC, StaticInitAtExit.h): 573 in all.
#include "app/WinMainStart.h"

#include <cstddef>
#include <cstdint>

namespace staticinit {

// A key from a name: CRC-24 of the lower-cased name with the top byte set (81 entries, slots 384-464: the UI
// sound names). Image initialiser e.g. 0x13d9ab: `global = Crc24(name, 1) | 0xff000000`.
struct NameKey {
    uint32_t slot;
    uint32_t imageRva; // the image's initialiser
    uint32_t storage;  // the global (RVA)
    const char *name;
};

// A two-word key from a name (30 entries): the first word is CRC-24 of the name as written with the top byte
// set, the second CRC-32 of the lower-cased name. Image initialiser e.g. 0x13852c.
struct NameKeyPair {
    uint32_t slot;
    uint32_t imageRva;
    uint32_t storage; // the first word (RVA); the second follows it
    const char *name;
};

void __cdecl InitNameKey(const void *nameKey);          // runs one NameKey entry
void __cdecl InitNameKeyPair(const void *nameKeyPair);  // runs one NameKeyPair entry

const NameKey *NameKeys(size_t *count);
const NameKeyPair *NameKeyPairs(size_t *count);

// ---- Statics of our classes (lane GQ; src/app/StaticInitObjects.cpp) ----
//
// The image's entry constructs the object in place and registers a destructor thunk with the CRT's atexit; ours
// constructs it with our class (the same constructor the image's entry reaches through its hook) and registers
// our own thunk, one per entry, through BoundAtExit(), in the same slot, so the LIFO order of the exit list is
// unchanged.

// What owned entries register their destructors with: a CRT's atexit. Default: our CRT's (std::atexit, the
// standalone build). While the image is hosted, the host binds the image CRT's atexit under [Startup] OwnStart=1/2
// (as it binds winmaininfra::WinMainSeams::registerAtExit for WinMain's two statics), so the image's `exit` runs
// them where it ran the image's thunks.
using RegisterAtExit = int(__cdecl *)(void(__cdecl *)());
void BindAtExit(RegisterAtExit registerAtExit);
RegisterAtExit BoundAtExit();

// The pixel formats (57 entries, slots 19-75): the 51 static formats the image constructs (initialiser e.g.
// 0x137ca0: push the arguments, `mov ecx, object`, call the class's constructor, atexit(thunk)), the five
// constant-initialised DXT objects (only their name is made here: "DXT" + the version) and the static unknown
// format (only the characters of its name, "UNKNOWN"; size and capacity are in .data). Each thunk (e.g. 0xd8fab0)
// writes the class's cPixelFormatBaseT<> vtable and frees the name: the class's deleting destructor with flags 0.
constexpr uint8_t kUnknownFormatSpec = 0xff; // `spec` of the unknown format's entry
struct PixelFormatStatic {
    uint32_t slot;
    uint32_t imageRva;
    uint32_t storage; // the object (RVA)
    uint8_t spec;     // its index in nGZGraphic4::kStaticFormatSpecs (the order of the table at 0xd9cf78), or kUnknownFormatSpec
};

// cRZCriticalSection statics (10 entries): `mov ecx, object; call 0x1af64e; atexit(thunk)`, the thunk
// `mov ecx, object; jmp 0x1af68a` (the destructor body).
struct LockStatic {
    uint32_t slot;
    uint32_t imageRva;
    uint32_t storage;
};

// cRZRandom statics (5 entries): `push -1; mov ecx, object; call 0x1b8bc8` (seeded from the clock); no destructor.
struct RandomStatic {
    uint32_t slot;
    uint32_t imageRva;
    uint32_t storage;
    uint32_t callSite; // the RVA the image's constructor call returns to (cRZRandom::ConstructAt's seed-control key)
};

void __cdecl InitPixelFormat(const void *pixelFormatStatic);
void __cdecl InitLock(const void *lockStatic);
void __cdecl InitRandom(const void *randomStatic);

const PixelFormatStatic *PixelFormatStatics(size_t *count);
const LockStatic *LockStatics(size_t *count);
const RandomStatic *RandomStatics(size_t *count);

// ---- The "value" kind (25 of 26 entries; src/app/StaticInitValues.cpp) ----
// A global set from constants or other globals, no calls. Not owned: slot 104 (0x1383b4), which calls kernel32
// (a shared memory section) and registers a destructor: one of its own later.
enum class ValueOp : uint8_t {
    StoreConstant,        // target = constant
    StoreAddress,         // target = the address of `source` (a relocated pointer)
    Copy,                 // `constant` dwords from source to target
    Negate,               // `constant` floats, sign flipped
    CopyAndNegateTriples, // per source triple (`constant` of them): the triple, then its negation
    ScaleBySqrt2,         // float target = source * 1.414 (the constant at 0xdcc3b8)
    Length,               // target = strlen(the string `source` points to)
    Box,                  // 9 dwords from two source vectors (+0, +0x18): the second, the first, the second * 0.5
};
struct ValueStatic {
    uint32_t slot;
    uint32_t imageRva;
    uint32_t target;
    uint32_t source;
    ValueOp op;
    uint32_t constant; // the value, or a count
};
void __cdecl InitValue(const void *valueStatic);
const ValueStatic *ValueStatics(size_t *count);

// ---- The "std-string" kind (85 entries; src/app/StaticInitStrings.cpp) ----
enum class StringForm : uint8_t {
    Construct,  // the library constructor in place
    Characters, // only the characters into the inline buffer (size and capacity constant-initialised)
    Heap,       // a heap buffer of capacity + 1: pointer, size and capacity stored
};
struct StringStatic {
    uint32_t slot;
    uint32_t imageRva;
    uint32_t storage; // the std::string (RVA)
    StringForm form;
    const char *text;
    uint32_t size;
    uint32_t capacity;
};
void __cdecl InitString(const void *stringStatic);
const StringStatic *StringStatics(size_t *count);

// ---- The cRZString statics of the "class-inline" kind (135 entries; src/app/StaticInitRZStrings.cpp, lane GY) ----
// A cRZString (or an array of them, or of 36-byte {cRZString, dword} pairs) whose constructor the image inlined.
// Each element is one of these forms (what the image's entry writes, from a junk-filled run of each, two patterns):
enum class RZStringForm : uint8_t {
    None,       // constant-initialised whole (an empty string with the vtable): nothing written
    Characters, // constant-initialised but for the characters (vtable, size, capacity 15 in .data): `size` copied
    Construct,  // cRZString(const char *) (inlined, or called at 0x14116d): vtable, the library string constructor
                // (union zeroed, characters, terminator; a long text in a block of capacity size | 15 + 1), refcount 0
    Heap,       // the long-text constructor inlined without zeroing the union: a block of capacity + 1 bytes with the
                // characters and terminator; pointer, size, capacity, vtable and refcount 0 stored, union bytes 4-15 left
};
struct RZStringElement {
    RZStringForm form;
    uint32_t size;     // characters
    uint32_t capacity; // Heap only
    const char *text;
    uint32_t extra;    // the dword after the string in a 36-byte element, when the entry writes it
    bool hasExtra;
};
struct RZStringStatic {
    uint32_t slot;
    uint32_t imageRva;
    uint32_t storage; // the object, or the array's first element (RVA)
    uint32_t count;   // elements
    uint32_t stride;  // 0x20, or 0x24 for the {cRZString, dword} arrays
    bool array;       // the thunk destroys `count` elements, last first (`eh vector destructor iterator`)
    uint32_t first;   // its first element in RZStringElements()
    uint32_t alias;   // a global (RVA) the entry stores the object's address in, or 0
};
void __cdecl InitRZString(const void *rzStringStatic);
const RZStringStatic *RZStringStatics(size_t *count);
const RZStringElement *RZStringElements(size_t *count);

// The skip-list entries of each kind (in slot order within the kind).
const winmainstart::OwnedInitialiser *OwnedRZStrings(size_t *count);
const winmainstart::OwnedInitialiser *OwnedStrings(size_t *count);
const winmainstart::OwnedInitialiser *OwnedNameKeys(size_t *count);
const winmainstart::OwnedInitialiser *OwnedObjects(size_t *count);
const winmainstart::OwnedInitialiser *OwnedValues(size_t *count);

// Every owned entry, in slot order: the host's skip list.
const winmainstart::OwnedInitialiser *OwnedInitialisers(size_t *count);

} // namespace staticinit
