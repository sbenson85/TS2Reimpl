#pragma once
// nGZSceneGraph::cRenderGroupNameIterator (lane PX, scenegraph N3 step 1): the names of the render groups a renderable
// node is in. The node keeps a layer mask (one bit per group; cRenderableNodeImpl.h); this walks the set bits in order
// and hands out the name the render-group registry (0xaeb370's object) has for each bit, skipping a bit the registry
// has no name for. A cIGZForwardIterator<const char *, cGZElementByValue<const char *>> (game vtable 0xe7c8dc, 8
// slots, 0x10 bytes): +4 the count, +8 the mask, +0xc the bit it stands on (0x20 is the end).
//
// Built only by cRenderableNodeImpl's MakeLayerNames (0xae02f0) and its own Clone (0xae0050), through the two
// constructors 0xadffc0 (mask, first bit) and 0xadff90 (a copy). Those store the game vtable and nothing else does, so
// once the builder is ours no game iterator exists and the bodies below are in effect through it (the shared slots
// 0xe7c8dc holds, QueryInterface 0x178b20, AddRef 0x148fb0, Release 0x1a84c0 and IsEqual 0x178550, stay unclaimed).
#include "gz/cIGZUnknown.h"

#include <cstdint>

namespace nGZSceneGraph {

// cIGZIterator / cIGZForwardIterator<const char *, ...>: slots 3-6 (the deleting destructor is slot 7).
class cIRenderGroupNameIterator : public cIGZUnknown {
public:
    virtual cIRenderGroupNameIterator *Clone() = 0;                    // 3: a new iterator at the same place (count 0)
    virtual bool IsEqual(const cIRenderGroupNameIterator *other) = 0;  // 4: the same bit
    virtual const char *Get() = 0;                                     // 5: the registry's name of the current bit
    virtual void Next() = 0;                                           // 6: to the next set bit with a name
};

class cRenderGroupNameIterator final : public cIRenderGroupNameIterator {
public:
    static constexpr int32_t kEnd = 0x20;

    // 0xadffc0: stands on the first set bit at or after `first` that has a name (or at the end).
    cRenderGroupNameIterator(uint32_t mask, int32_t first);
    cRenderGroupNameIterator(const cRenderGroupNameIterator &) = default; // 0xadff90's fields: mask and bit, count 0
    virtual ~cRenderGroupNameIterator() = default; // 0xae0030 is the deleting destructor (slot 7)

    bool QueryInterface(uint32_t riid, void **out) override; // shared 0x178b20: riid 1 only
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared 0x1a84c0: deletes at 0
    cIRenderGroupNameIterator *Clone() override;             // 0xae0050
    bool IsEqual(const cIRenderGroupNameIterator *other) override; // shared 0x178550
    const char *Get() override;                              // 0xae0080
    void Next() override;                                    // 0xae0680

    uint32_t mRefCount = 0; // +4
    uint32_t mMask;         // +8
    int32_t mBit;           // +0xc
};
static_assert(sizeof(cRenderGroupNameIterator) == 0x10, "cRenderGroupNameIterator size");

// The names' range (0xae02f0 builds it): a cursor (a clone of begin) and the two ends, each counted.
struct cRenderGroupNameRange {
    cIRenderGroupNameIterator *mCurrent;
    cIRenderGroupNameIterator *mBegin;
    cIRenderGroupNameIterator *mEnd;
};
static_assert(sizeof(cRenderGroupNameRange) == 0xc, "cRenderGroupNameRange size");

// The render-group registry (the object 0xaeb370 returns; cRenderGroupManager, sg/cRenderGroupManager.h): a map of names to bits (at most 32).
class cIRenderGroupRegistry : public cIGZUnknown {
public:
    virtual uint32_t BitOfName(const char *name) = 0; // 3: the name's bit (registering it first if new)
    virtual uint32_t Count() = 0;                     // 4: how many groups
    virtual const char *NameOfBit(uint32_t bit) = 0;  // 5: null for a bit without a name
};

// The registry (0xaeb370, __cdecl, ours: GetRenderGroupRegistry in sg/cRenderGroupManager.cpp): cached on first use.
cIRenderGroupRegistry *RenderGroupRegistry();

} // namespace nGZSceneGraph
