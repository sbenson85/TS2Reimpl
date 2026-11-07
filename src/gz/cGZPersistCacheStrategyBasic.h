#pragma once
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZUnknown.h"
#include "ts/PersistKey.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// cGZPersistCacheStrategyBasic, the resource manager's default cache strategy (cIGZPersistCacheStrategy): a map from
// resource key to the cached resource, with no eviction order. The resource manager's constructor (0x9b3f30) puts one on
// its strategy list and cTSPersistSystem::Init (0x1e0d60) makes the sim cache one. RVAs are 1.18.0.187.
//
// Standalone shape: our own C++ vtable (the game's is 0xe60148, 21 slots), built by our constructor (the game's 0x9b35f0).
// Nothing else in the image builds one, so the game's slots are reached only through objects the original constructor
// built, and none is built any more (both callers are ours).
//
// Layout (0x34 bytes, the game's):
//   +0x00  vtable
//   +0x04  std::unordered_map<PersistKey, resource *> (0x20 bytes: max load factor, list head and size, bucket vector,
//          mask, max index; nodes 0x1c: links, key, value). The game's instantiation hashes a key by its third word
//          (the instance): 0x9b3b70 (insert) and the lookups pass key+8 to Find128 0x1f5570, 0x9b3580 (erase) buckets by
//          node+0x10. Equality is all four words. The hash is not noexcept, as the game's: clear() is the bulk 0x1fc2f0.
//   +0x24  int32 access counter (slot 14)
//   +0x28  byte: auto-purge on access (slots 14, 15, 16)
//   +0x2c  key filter (counted; slots 5, 6, 11, 12)
//   +0x30  reference count
class cGZPersistCacheStrategyBasic : public cIGZUnknown {
public:
    using Key = PersistKey;
    struct InstanceHash {
        size_t operator()(const Key &k) const { return k.mWords[2]; }
    };
    using Map = std::unordered_map<Key, void *, InstanceHash, PersistKeyEqual>;
    // The callers' key lists: vectors on the allocator service (the game's growth 0x1d1370 allocates through it).
    using KeyList = std::vector<Key, GZServiceAllocator<Key>>;

    static constexpr uint32_t kObjectSize = 0x34;

    // 0x9b35f0: the empty map (16 bucket entries, mask 7, max index 8, load factor 1), counter 0, flag 0, no filter,
    // count 0 (the caller AddRefs it).
    cGZPersistCacheStrategyBasic() noexcept;
    // The game constructor's shape (ecx: the caller's 0x34 bytes; answers them), for the callers that allocate as the game
    // does: our object built in `memory`. Not inlined, so tests can stand in for it and the image's 0x9b35f0 together
    // (shape::StandIn).
    static cGZPersistCacheStrategyBasic *__fastcall Construct(void *memory, void *unusedEdx = nullptr);

    // Slot 0, the folded 0x1fc060 (ret 8): riid 1 or 0x6bb923e4 (cIGZPersistCacheStrategy) answers this, AddRef'd
    // through slot 1; anything else false.
    bool QueryInterface(uint32_t riid, void **out) override;
    // Slot 1, 0x9b36c0: counts up; answers the count before the increment.
    uint32_t AddRef() override;
    // Slot 2, 0x9b3a90: above 1 counts down and answers the new count; otherwise (not counted down) the deleting
    // destructor through slot 19 with 1, and 0.
    uint32_t Release() override;
    // Slot 3, the folded 0x140fe0: does nothing (an empty function).
    virtual void vf3();
    // Slot 4, 0x9b3ae0: Clear through slot 18, then every remaining entry's resource released (when not null) and the
    // map emptied.
    virtual void Shutdown();
    // Slot 5, 0x9b3ab0 (ret 4): a different filter is AddRef'd, stored, and the old one released.
    virtual void SetKeyFilter(cIGZUnknown *filter);
    // Slot 6, the folded 0x147770: the filter.
    virtual cIGZUnknown *GetKeyFilter();
    // Slot 7, 0x9b39c0 (ret 8): a cached key's resource into *out, AddRef'd (no null test): true; false otherwise.
    virtual bool Get(const Key *key, void **out);
    // Slot 8, 0x9b36d0 (ret 8): a key not cached: the resource is AddRef'd, given the key (its slot 4) and cached: true.
    // A cached key: false, nothing done.
    virtual bool Add(const Key *key, cIGZUnknown *resource);
    // Slot 9, 0x9b3b10 (ret 4): a cached key's resource released (no null test) and its entry erased: true.
    virtual bool Remove(const Key *key);
    // Slot 10, 0x9b3a20 (ret 4): whether the key is cached.
    virtual bool Contains(const Key *key);
    // Slot 11, 0x9b3750 (ret 4): whether the filter accepts the key (its slot 3); true without a filter.
    virtual bool IsKeyAccepted(const Key *key);
    // Slot 12, 0x9b38c0 (ret 8): in map order, each entry's key as its resource reports it (slot 3, into a local); the
    // keys both the strategy's filter and the given one accept (a null one accepts all) are counted and appended to
    // `keys` when given.
    virtual uint32_t GetKeys(KeyList *keys, cIGZUnknown *filter);
    // Slot 13, 0x9b3960 (ret 8): GetKeys through slot 12 with a new type filter (cGZPersistResourceKeyFilterByType),
    // AddRef'd for the call and released after.
    virtual uint32_t GetKeysOfType(KeyList *keys, uint32_t type);
    // Slot 14, 0x9b3a60 (ret 4, the argument unused): with the flag set, counts an access; past 0x80 (signed) the
    // counter is reset and slot 20 runs. True.
    virtual bool OnAccess(uint32_t unused);
    // Slot 15, the folded 0x3730d0: the flag byte.
    virtual uint8_t GetAutoPurge();
    // Slot 16, the folded 0x2bfcb0 (ret 4): stores the argument's low byte as the flag.
    virtual void SetAutoPurge(uint8_t on);
    // Slot 17, 0x9b37a0: slot 20 up to 8 times, stopping at the first false; whether it answered true at least once.
    virtual bool AdvanceGeneration();
    // Slot 18, 0x9b3770: every resource released (when not null), the map emptied.
    virtual void Clear();
    // Slot 19, 0x9b3680 (ret 4): the deleting destructor. Releases the filter and destroys the map (the resources are
    // not released); frees the 0x34 bytes when flags & 1.
    virtual ~cGZPersistCacheStrategyBasic();
    // Slot 20, 0x9b37d0: one pass over the map in order: entries without a resource are erased; a resource with fewer
    // than 2 uses (its slot 11) is released and its entry erased. Whether any resource was released.
    virtual bool PurgeUnused();

    Map mMap;               // +0x04
    int32_t mAccesses;      // +0x24
    uint8_t mAutoPurge;     // +0x28
    cIGZUnknown *mFilter;   // +0x2c
    uint32_t mRefCount;     // +0x30
};
static_assert(sizeof(cGZPersistCacheStrategyBasic::Map) == 0x20, "the game's unordered_map layout");
static_assert(sizeof(cGZPersistCacheStrategyBasic) == cGZPersistCacheStrategyBasic::kObjectSize, "basic strategy layout");
