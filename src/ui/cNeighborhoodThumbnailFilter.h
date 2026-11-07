#pragma once
// cNeighborhoodThumbnailFilter (standalone shape; type descriptor 0x10ae21c, game vtable 0xdf39d4, 6 slots, 0x10
// bytes): a resource key filter holding a set of keys; a key passes when it is in the set. cTSUIThumbnailService's
// slot 52 (0x726dc2) builds one through the constructor 0x725594 (hooked to ours), fills it and hands it on.
#include "gz/cIGZUnknown.h"
#include "ts/cTSCatalog.h"

#include <cstddef>
#include <cstdint>
#include <set>

namespace thumbfilter {

// A resource key (type, group, instance, resource) as the set orders it: type, group, resource, instance, unsigned
// (the lower-bound search 0x1d5d10).
struct Key {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
    uint32_t resource;
};
static_assert(sizeof(Key) == 0x10, "a resource key");

struct KeyLess {
    bool operator()(const Key &a, const Key &b) const
    {
        if (a.type != b.type) return a.type < b.type;
        if (a.group != b.group) return a.group < b.group;
        if (a.resource != b.resource) return a.resource < b.resource;
        return a.instance < b.instance;
    }
};

using KeySet = std::set<Key, KeyLess>;
static_assert(sizeof(KeySet) == 8, "the game's std::set: head node, size");

} // namespace thumbfilter

class cNeighborhoodThumbnailFilter : public cIGZPersistResourceKeyFilter {
public:
    static constexpr uint32_t kSize = 0x10;
    static constexpr uint32_t kInterfaceId = 0x8865cd89; // cIGZPersistResourceKeyFilter

    cNeighborhoodThumbnailFilter() = default;                    // 0x725594
    ~cNeighborhoodThumbnailFilter() override = default;          // slot 4, the deleting destructor 0x7258e3
    cNeighborhoodThumbnailFilter(const cNeighborhoodThumbnailFilter &) = delete;
    cNeighborhoodThumbnailFilter &operator=(const cNeighborhoodThumbnailFilter &) = delete;

    bool QueryInterface(uint32_t riid, void **out) override;     // slot 0, folded 0x72baec
    uint32_t AddRef() override;                                  // slot 1, folded 0x148fb0
    uint32_t Release() override;                                 // slot 2, folded 0x72c480
    // Whether the key (four words) is in the set.
    bool IsKeyIncluded(const uint32_t *key) override;            // slot 3, 0x72baaf
    // The key (four words) added to the set (once).
    virtual void AddKey(const uint32_t *key);                    // slot 5, 0x725957

    uint32_t mRefCount = 0;   // +0x04
    thumbfilter::KeySet mKeys; // +0x08 head node, +0x0c size
};

static_assert(sizeof(cNeighborhoodThumbnailFilter) == cNeighborhoodThumbnailFilter::kSize,
              "cNeighborhoodThumbnailFilter size");
static_assert(offsetof(cNeighborhoodThumbnailFilter, mRefCount) == 4 && offsetof(cNeighborhoodThumbnailFilter, mKeys) == 8,
              "cNeighborhoodThumbnailFilter layout");

// The constructor hook's entry point (`this` in ecx, the dummy edx).
struct cNeighborhoodThumbnailFilterAdapters {
    static cNeighborhoodThumbnailFilter *__fastcall Construct(void *memory, void *edx); // 0x725594
};
