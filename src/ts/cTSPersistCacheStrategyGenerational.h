#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "ts/PersistKey.h"

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

// cTSPersistCacheStrategyGenerational, a resource cache eviction strategy (cIGZPersistCacheStrategy,
// cIGZUnknown and cRZRefCountMT bases). RVAs are 1.18.0.187.
//
// Subobjects of the object (offsets from the object at +0):
//   +0x000  primary vtable 0xda7364 (24 slots)
//   +0x004  cRZRefCountMT subobject, vtable 0xda73c8 (3 slots). Its refcount is at +0x008.
//
// Fields read or written by the functions reimplemented here (offsets from the object):
//   +0x04c  object P. vf11 calls its slot 3 (no null check when P is set; null P means "yes").
//   +0x050  uint32 counter, incremented by vf17.
//   +0x05c  pointer to a config table T (set by vf21, read by vf22):
//             T+0x04  uint32, the third argument of the slot 23 call made by vf22
//             T+0x08..T+0x14  uint32 limits[4] for the four generation counters
//   +0x060..+0x06c  uint32 counters[4] (generations), used by vf22.
//   +0x070  byte flag, read by vf14.
//
// vf4, vf17 and vf22 call slot 23 (Evict) and slot 18 (Clear) through the object's vtable, as the original does.
class cTSPersistCacheStrategyGenerational {
public:
    static constexpr uint32_t kVTable = 0xda7364;
    static constexpr uint32_t kSubVTable = 0xda73c8;

    // Slot 2 (IUnknown Release), 0x1fc090: `add ecx, 4` then a jump to cRZRefCountMT::Release on the
    // subobject at +4. Returns the count the subobject's Release returns.
    uint32_t Release();

    // Slot 4, 0x1fc140: calls its slot 23 with (4, 1, -1), then tail-jumps to its slot 18 (no
    // arguments). Returns slot 18's eax unchanged.
    uint32_t vf4();

    // Slot 11, 0x1fb790: one argument (ret 4). If the object at +0x4c is null returns 1;
    // otherwise returns 1 if P's slot 3 (given the argument) returned nonzero in al, else 0. Only al
    // is defined.
    uint8_t vf11(uint32_t a);

    // Slot 14, 0x1fc040: one unused argument (ret 4). If the byte at +0x70 is nonzero calls its slot
    // 22 with 0. Returns 1 in al either way.
    uint8_t vf14(uint32_t unused);

    // Slot 17, 0x1fb830: no arguments. Increments the counter at +0x50, then calls its slot 23 with
    // (4, 1, -1) up to eight times, stopping at the first call that returns zero in al. Returns al =
    // (at least one call returned nonzero). Only al is defined: the upper bits of eax hold the last
    // slot 23 result.
    uint8_t vf17();

    // Slot 21, 0x1fc0a0: one argument (ret 4). Sets the config-table pointer at +0x5c to one of three
    // static tables when the argument is 0, 1 or 2 (ignored otherwise).
    void vf21(uint32_t which);

    // Slot 22, 0x1fbd90: one argument (ret 4) whose low byte is the "force" flag (only al is tested).
    // Advances the generation counters: each counter is incremented, and counters that reach their
    // limit (and counter 0 when the flag is set) are reset to zero, moving on to the next. The
    // first counter that is not reset stops the scan, and slot 23 is called with (level, 0, T+4)
    // where level is that counter's index plus one (5 if all four were reset). Returns eax of that
    // slot 23 call unchanged.
    uint32_t vf22(uint32_t force);

    // ---- The cache itself (lane NN) ----
    //
    // Four generations, each a std::list of entries (+0x0c + 8 * g: head, size; nodes 0x14 bytes), and an index from
    // resource key to the entry's list node: a std::unordered_map (+0x2c, 0x20 bytes; nodes 0x1c: links, key, node
    // pointer). Both are MSVC 14.40 containers as the game's (the same code: list splice and erase 0x1fc360, map erase
    // 0x1fb250, rehash 0x1fc260, insert 0x1fc200, clear 0x1fc2f0 are its library instantiations), so the fields are
    // used as those types.
    using Key = PersistKey; // ts/PersistKey.h: the hash is the sum of the four words, not noexcept
    struct Entry {
        void *mResource;      // +0 counted; slot 11 its use count, slot 3 its key
        uint8_t mGeneration;  // +4 the list it is in
        uint8_t mAge;         // +5 evictions survived in this generation (promoted at the table's threshold)
        uint8_t mRecent;      // +6 used since its stamp was last set
        uint8_t mPad;         // +7 (Add leaves an uninitialised byte here)
        uint32_t mStamp;      // +8 the tick it was last used
    };
    using Generation = std::list<Entry>;
    using Index = std::unordered_map<Key, Generation::iterator, PersistKeyHash, PersistKeyEqual>;
    // The callers' key lists: vectors on the allocator service (the game's growth 0x1d1370 allocates through it).
    using KeyList = std::vector<Key, GZServiceAllocator<Key>>;

    Generation &Gen(uint32_t g) { return *reinterpret_cast<Generation *>(reinterpret_cast<uintptr_t>(this) + 0x0c + 8 * g); }
    Index &Map() { return *reinterpret_cast<Index *>(reinterpret_cast<uintptr_t>(this) + 0x2c); }

    // Slot 7, 0x1fbf00 (ret 8): Get(key, out). An indexed entry with a resource is touched (stamp = tick, recent),
    // moved to the back of its generation, and its resource handed out AddRef'd: returns true (al;
    // the rest of eax is not defined, as for Add and Remove). An entry without one is
    // dropped (list, count, index): returns false, as for a key not indexed.
    bool Get(const Key *key, void **out);
    // Slot 8, 0x1fb580 (ret 8): Add(key, resource). An indexed key returns 0. Otherwise the resource is AddRef'd and
    // given the key (its slot 4), appended to generation 0 (recent, stamped with the tick), counted and indexed: 1.
    bool Add(const Key *key, void *resource);
    // Slot 9, 0x1fc160 (ret 4): Remove(key): drops the indexed entry (index first, then the resource's release, then
    // the list node) and returns 1; 0 when not indexed.
    bool Remove(const Key *key);
    // Slot 10, 0x1fbff0 (ret 4): whether the key is indexed (al; the rest of eax is not defined).
    bool Contains(const Key *key);
    // Slot 12, 0x1fbde0 (ret 8): counts the indexed resources whose key (their slot 3) the filter accepts (its
    // slot 3; no filter: all), in index order, appending the keys to `keys` (a KeyList) when given.
    uint32_t GetKeys(KeyList *keys, void *filter);
    // Slot 13, 0x1fbe70 (ret 8): GetKeys through slot 12 with a new cGZPersistResourceKeyFilterByType(type).
    uint32_t GetKeysOfType(KeyList *keys, uint32_t type);
    // Slot 18, 0x1fb7b0: releases every entry's resource (no null test) generation by generation, empties the
    // lists and the index, and zeroes the count.
    void Clear();
    // Slot 23, 0x1fb870 (ret 0xc): Evict(levels, force, budget). Without force, nothing happens while the count is
    // within the limit (returns false; the tick is not advanced). Otherwise generations 0 .. min(levels, 4) - 1 are
    // walked front to back, at most `budget` entries each (the walk rotates the list to resume at the next entry).
    // An entry whose resource has fewer than 2 uses is restamped if recent, and evicted when forced or older than
    // the table's minimum age (+0): its index entry is erased (found by its key, or by a scan of the index when the
    // key does not lead to it), the resource released, the node and the count dropped; without force the walk stops
    // once the count is within the limit. Any other entry is marked recent if in use, and in generations 0..2 ages;
    // at the generation's threshold (table +0x18 + 4 * g, low byte) it moves to the back of the next generation and
    // is indexed there. Advances the tick; returns whether anything was evicted (al).
    bool Evict(int32_t levels, uint8_t force, uint32_t budget);

    // 0x1fb4f0 (ret 4), the deleting destructor (reached through the +4 face's slot 0 thunk 0x1fb4e5): restores the
    // vtables, releases the object at +0x4c, destroys the index and the four generations (nodes freed; the
    // resources are not released), then the cRZRefCountMT base at +4; frees the 0x78-byte object when flags & 1.
    static constexpr uint32_t kObjectSize = 0x78;
    cTSPersistCacheStrategyGenerational *DeletingDestructor(uint32_t flags);
};
