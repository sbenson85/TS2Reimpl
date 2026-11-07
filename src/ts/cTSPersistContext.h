#pragma once
#include "ts/PersistKey.h"

#include <cstdint>
#include <unordered_map>

// cTSPersistContext, the persistence context. RVAs are 1.18.0.187. The primary object is at +0 and
// its first vtable (0xda7228, 17 slots) is the one vf9 is reached through.
//
// Fields used by the functions reimplemented here (offsets from the object, the `this` they receive):
//   +0x10  sentinel node pointer: the list head. Its +0 is the first node, its +4 the last.
//   +0x14  node count.
//   +0x18  bucket array of cTSPersistContextBucket. Bucket index = (sum of the key dwords) & mask.
//   +0x24  bucket mask.
//
// A list node (0x2c bytes, as allocated and freed by the game):
//   +0x00  next, +0x04 prev, +0x08 key[4] (dwords; key[1] is the field matched), +0x18..+0x23 unused
//          here, +0x24 refcounted object A, +0x28 refcounted object B (both released through their
//          vtable slot 2 before the node is deleted).

struct cTSPersistContextNode {
    cTSPersistContextNode *next;
    cTSPersistContextNode *prev;
    uint32_t key[4];
    uint32_t unused18[3];
    void *refA;
    void *refB;
};
static_assert(sizeof(cTSPersistContextNode) == 0x2c, "persist context node size");

struct cTSPersistContextBucket {
    cTSPersistContextNode *first;
    cTSPersistContextNode *last;
};

class cTSPersistContext {
public:
    // ---- The resource index (lane NN) ----
    //
    // The fields at +0x0c are a MSVC 14.40 std::unordered_map (0x20 bytes, as the game's: its erase 0x1fa500,
    // try_emplace 0x1fa310, clear 0x1f9b80 and destructor 0x1fa5d0 are that library code) from a resource key to an
    // Entry; nodes 0x2c bytes (links, key at +8, entry at +0x18). vf9 above walks the same nodes by hand.
    class Counted {
    public:
        Counted() = default;
        Counted(const Counted &) = delete;
        Counted &operator=(const Counted &) = delete;
        ~Counted() noexcept(false);
        void *Get() const { return mObject; }
        void **Out() { return &mObject; }
        void Hold(void *object); // an empty one takes `object` and AddRefs it

    private:
        void *mObject = nullptr;
    };
    struct Entry {
        uint8_t mDirty = 0;           // +0x18 (node) set by AddResource and SetDirty; Save clears it on success
        uint32_t mPriority = 0;       // +0x1c the order Save writes in (ascending)
        uint32_t mMask = 0xffffffffu; // +0x20 the entry is in a mask's set when (mask & entry mask) == entry mask
        Counted mState;               // +0x24 the resource's 0x456b8543 face: slot 14 dirty?, slot 15 set dirty
        Counted mResource;            // +0x28
    };
    using Index = std::unordered_map<PersistKey, Entry, PersistKeyHash, PersistKeyEqual>;
    Index &Map() { return *reinterpret_cast<Index *>(reinterpret_cast<uintptr_t>(this) + 0x0c); }

    // Slot 0, 0x1fabb0 (ret 8): null out: false. Id 0x105231cf: this, AddRef'd (the count at +8). Others: the cRZUnknown
    // base at +4 answers (0x1b945c: id 1).
    bool QueryInterface(uint32_t iid, void **out);
    // Slot 3, 0x1faaa0 (ret 0xc): FindIndexedResource (slot 7) answers when it can; otherwise the resource manager (cached
    // service 0xb390c0, slot 5 with iid 0x456b8f1d) loads it, it is asked for `iid` and, if it answers, added (slot 4
    // with 0, 0, -1). Returns whether `out` was filled.
    uint8_t GetResource(const PersistKey *key, uint32_t iid, void **out);
    // Slot 4, 0x1fa720 (ret 0x10): null or already indexed (slot 8 with the resource's key): false. Otherwise the
    // key's entry gets dirty, priority, mask and the resource, and its state face (0x456b8543) anew; dirty is or'ed
    // with the face's slot 14. Returns true.
    bool AddResource(void *resource, uint8_t dirty, uint32_t priority, uint32_t mask);
    // Slot 5, 0x1fabf0 (ret 4): drops the key's entry; whether there was one.
    bool RemoveResource(const PersistKey *key);
    // Slot 6, 0x1fab80 (ret 4): RemoveResource (slot 5), then the manager's slot 17 with the key; either result (al).
    uint8_t DeleteResource(const PersistKey *key);
    // Slot 7, 0x1faa00 (ret 0xc): the indexed resource's QueryInterface; false (al) when not indexed.
    uint8_t FindIndexedResource(const PersistKey *key, uint32_t iid, void **out);
    // Slot 8, 0x1faa60 (ret 4): whether the key is indexed (al).
    bool HasResource(const PersistKey *key);
    // Slot 10, 0x1faf50 (ret 8): sets the entry's dirty byte and tells its state face (slot 15). Whether indexed.
    bool SetDirty(const PersistKey *key, uint32_t dirty);
    // Slot 11, 0x1fa990 (ret 8): the entry's dirty byte, or its state face's slot 14 when it has one, into *out.
    bool GetDirty(const PersistKey *key, uint8_t *out);
    // Slot 12, 0x1faf00 (ret 8) and slot 13, 0x1fa940 (ret 8): set / get the entry's mask. Whether indexed.
    bool SetMask(const PersistKey *key, uint32_t mask);
    bool GetMask(const PersistKey *key, uint32_t *out);
    // Slot 14, 0x1fad60 (ret 8): Save(force, mask): the entries in the mask's set that are forced, dirty, or whose
    // state face says so (slot 14), by ascending priority (std::sort), are saved by the resource manager (slot 16
    // with the resource, 0): dirty = !saved, told to the state face (slot 15). Returns whether all saved.
    uint8_t Save(uint8_t force, uint32_t mask);
    // Slot 15, 0x1fa7f0: empties the index (releasing every entry's resource, then its face). Returns true.
    bool Clear();
    // Slot 16, 0x1fafb0 (ret 4): Release(mask): every entry in the mask's set is handed to the object the manager's
    // slot 3 returns (its slot 10 with a copy of the key) and dropped. Returns whether every hand-over said true.
    uint8_t ReleaseResources(uint32_t mask);

    // 0x1fa6e0 (ret 4), the deleting destructor (reached through the +4 face's slot 3 thunk 0x1fa6c9): restores both
    // vtables, destroys the index (each entry's resource released, then its state face), then the cRZUnknown base at
    // +4; frees the 0x2c-byte object when flags & 1. Returns this.
    static constexpr uint32_t kVTable = 0xda7228;
    static constexpr uint32_t kSubVTable = 0xda7270;
    static constexpr uint32_t kObjectSize = 0x2c;
    cTSPersistContext *DeletingDestructor(uint32_t flags);

    // Slot 9, 0x1fac50 (ret 4): drops every entry whose key's second word (the group) is *group, in index order
    // (the entry's resource released, then its state face). Returns whether any was dropped (al).
    bool vf9(const uint32_t *group);
};
