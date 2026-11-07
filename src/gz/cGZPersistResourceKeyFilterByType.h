#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cGZPersistResourceKeyFilterByType (12 bytes, the game's vtable 0xda5cf0): a key filter that accepts keys of one type,
// built by the resource manager's vf17 / vf19 and the basic cache strategy's GetKeysOfType. Ours, with its own vtable of
// the same five slots: QueryInterface (the folded 0x99b760: 0x8865cd89 or 1 answer this, counted), AddRef (0x148fb0),
// Release (0x723ae0: the last reference deletes without counting down and answers 0), IsKeyIncluded (0x1e1fc0: the key's
// type is the filter's) and the deleting destructor (0x1da0c0, 12 bytes). Built as the game builds it: count 0, the caller
// AddRefs it. (Moved here from cGZPersistResourceManager.cpp by lane QQ.)
class cGZPersistResourceKeyFilterByType : public cIGZUnknown {
public:
    explicit cGZPersistResourceKeyFilterByType(uint32_t type) noexcept : mRefCount(0), mType(type) {}
    bool QueryInterface(uint32_t riid, void **out) override
    {
        if (riid != 0x8865cd89 && riid != 1) return false;
        *out = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mRefCount; }
    uint32_t Release() override
    {
        if (mRefCount > 1) return --mRefCount;
        delete this;
        return 0;
    }
    virtual bool IsKeyIncluded(const uint32_t *key) { return key[0] == mType; } // slot 3
    virtual ~cGZPersistResourceKeyFilterByType() = default;                   // slot 4

    uint32_t mRefCount; // +0x04
    uint32_t mType;     // +0x08
};
static_assert(sizeof(cGZPersistResourceKeyFilterByType) == 0xc, "type key filter layout");
