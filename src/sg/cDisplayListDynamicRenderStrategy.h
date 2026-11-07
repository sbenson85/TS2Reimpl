// nTSSG::cDisplayListDynamicRenderStrategy (lane IR): the scene graph's dynamic display-list render strategy. A cRZUnknown
// (vtable 0xdbbadc, reference count at +4) with the display-list strategy interface at +8 (vtable 0xdbbafc, 11 slots).
// 0x194 bytes (the deleting destructor 0x36fb80 frees that much); constructor 0x36e400, destructor 0x36f220 (both still
// the original's). It wraps a cDisplayListCensorRenderStrategy (0x38 bytes, built by 0x3775c0) at +0xc, to whose +8
// interface slots 3, 6 and 7 go (tail calls: whatever they return is returned).
//
// Only the members the replaced slots touch are named; the rest is described from the constructor and destructor.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nTSSG {

class cDisplayListDynamicRenderStrategy {
public:
    // 0x36fb80 slot 3 (ret 4): the destructor (0x36f220, the original's), then the memory freed when `flags` bit 0 is
    // set. Returns the object.
    cDisplayListDynamicRenderStrategy *DeletingDestructor(uint32_t flags);

    const void *mVTable;     // 0x000 0xdbbadc
    uint32_t mRefCount;      // 0x004
    const void *mVTable8;    // 0x008 0xdbbafc (the strategy interface)
    void *mInner;            // 0x00c the wrapped cDisplayListCensorRenderStrategy (reference: AddRef / Release)
    void *mObject10;         // 0x010 framework class 0xdc012582 (its slot 5 (1) at construction)
    void *mObject14;         // 0x014 framework class 0x9c1d3c77
    void *mHelper;           // 0x018 a 0x28-byte object (0x36ed40 builds it, 0x36fa10 ends it)
    uint32_t mIndices[4];    // 0x01c set invalid at construction
    uint32_t mInnerChanged;  // 0x02c 0 at construction; slot 6 sets it to 1
    uint8_t mFields30[0x138 - 0x30]; // 0x030 two 4-byte vectors (0x30, 0x3c), two lists (0x48, 0x50), render states,
                                     // the references 0x104..0x134 the destructor ends (slot 21 (1))
    std::vector<void *> mRegistered; // 0x138 slots 9 / 10 add (once) / remove
    uint8_t mFields144[0x194 - 0x144]; // 0x144 a list (0x148), a viewport state (0x174)
};
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mInner) == 0xc);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mInnerChanged) == 0x2c);
static_assert(offsetof(cDisplayListDynamicRenderStrategy, mRegistered) == 0x138);
static_assert(sizeof(cDisplayListDynamicRenderStrategy) == 0x194);

// The strategy interface at +8: `this` is the object + 8.
class cDisplayListDynamicRenderStrategyFace {
public:
    // 0x3703e0 slot 3 (ret 4): the wrapped strategy's interface slot 3 with the same argument.
    uint32_t ForwardSlot3(void *argument);
    // 0x373690 slot 6: mInnerChanged = 1, then the wrapped strategy's interface slot 6.
    uint32_t ForwardSlot6();
    // 0x3703d0 slot 7 (ret 4): the wrapped strategy's interface slot 7 with the same argument.
    uint32_t ForwardSlot7(void *argument);
    // 0x370380 slot 9 (ret 4): adds `item` to mRegistered unless it is there already.
    void Register(void *item);
    // 0x373de0 slot 10 (ret 4): removes the first `item` from mRegistered, if any.
    void Unregister(void *item);

private:
    cDisplayListDynamicRenderStrategy *Owner()
    {
        return reinterpret_cast<cDisplayListDynamicRenderStrategy *>(reinterpret_cast<uint8_t *>(this) - 8);
    }
};

} // namespace nTSSG
