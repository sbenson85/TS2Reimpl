#pragma once
// Small objects the image's static initialisers construct, whose classes are otherwise unknown (lane HQ; the owned
// entries are lane HD's src/app/StaticInitConstructed.cpp, slots 110, 134, 212, 509-511, 637, 638, and lane HF's slot
// 512). Each constructor is hooked at its RVA (the statics' are the only callers of most; 0xc0a664 has six more) and the
// owned entries call it. Names are by layout and behaviour; the classes' own names are not known.
#include "app/StaticInitContainers.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

namespace startupobj {

// Slot 212's object (and six other callers' locals): a vector of 20-byte elements nothing destroys one by one, and a
// word after it.
struct Element20 {
    uint32_t words[5];
};
struct ZeroedVector20 {
    std::vector<Element20> items; // +0 (first, last, end)
    uint32_t extra;               // +0xc
    ZeroedVector20 *Construct();  // 0xc0a664 (thiscall, returns this): the four words zero
};
static_assert(sizeof(ZeroedVector20) == 16);

// The fixed-size pools (slots 509-511, 637, 638: the windowing code's): RZMalloc'd chunks of `chunkBytes` linked through
// their first word (the image's exit thunk 0x940e4e frees them).
struct PoolChunk {
    PoolChunk *next;
};
struct FixedPool {
    uint32_t elementSize; // +0: at least 4
    uint32_t chunkBytes;  // +4: elementSize * (128 below 0x200, 32 below 0x800, else 16)
    PoolChunk *chunks;    // +8: not written by the constructor (zero in the file)
    // 0x945bd6 (thiscall, ret 4): the two sizes. Returns nothing (eax is the chunk bytes, which no caller reads).
    void Construct(uint32_t elementSize);
    static uint32_t ChunkBytes(uint32_t elementSize);
};

// Slot 110's object (network-web): a std::list<uint32_t> and a lock.
struct ListAndLock {
    std::list<uint32_t> items; // +0 (head, size)
    cRZCriticalSection lock;   // +8
    ListAndLock *Construct();  // 0x1ce3f3 (thiscall, returns this): the list (the library constructor), then the lock
};
static_assert(sizeof(std::list<uint32_t>) == 8 && offsetof(ListAndLock, lock) == 8);

// Slot 512's object at 0x10f4eb8: an unordered set of 36-byte {cRZString, dword} records (lane HF's NamedWordHash36).
struct NamedWordHashSet {
    staticinit::NamedWordHash36 set;
    // 0x97df6d (thiscall, ret 8; std::_Hash's constructor from the traits and the allocator): max load factor 1.0f (the
    // traits' word is copied first, then overwritten), the list (a head node), 16 bucket entries at the list's end, mask
    // 7, max index 8. The arguments carry nothing else (an empty comparator and allocator).
    NamedWordHashSet *Construct(const void *traits, const void *allocator);
};
static_assert(sizeof(NamedWordHashSet) == 0x20);

// Slot 134's object at 0x10cca4e (empty; only its constructor runs): it resets the module's record pool before slot 135
// constructs the 401 records in it.
struct RecordPoolReset {
    static constexpr uint32_t kRecords = 0x10cca58;     // 401 records of 0x7c bytes (slot 135 constructs them)
    static constexpr uint32_t kRecordBytes = 0xc23c;
    static constexpr uint32_t kCount = 0x10cca40;       // dword, zeroed
    static constexpr uint32_t kReady = 0xff8ba8;        // byte, set to 1
    static constexpr uint32_t kObjects = 0x10d8ca0;     // a vector of 0x20-byte objects: {first, last, ...}
    // 0x26bbb0 (thiscall, returns this): the records zeroed, the count zero, the flag set, then each object in the
    // vector destroyed in place (its slot 33, the vector deleting destructor, with flags 0) and the vector emptied
    // (last = first; nothing freed).
    RecordPoolReset *Construct();
};

} // namespace startupobj
