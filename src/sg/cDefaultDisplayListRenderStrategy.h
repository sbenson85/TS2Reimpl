#pragma once
// nGZSceneGraph::cDefaultDisplayListRenderStrategy: the display list's own render strategy (slot 32 makes one when it is
// given none; a viewer may name another, its slot 117). 8 bytes, a vtable and a reference count (the original's vtable
// 0xe754e0, constructor 0xa9d450, destructor 0xa9e080, deleting destructor 0xa9e560).
//
// Ours is a real class: the display list builds it (cDisplayList::SetRenderStrategy), so the game's vtable is never
// installed and its own bodies (Render 0xaa2350 and the three above) are in effect through that caller. Its slots 0-3
// are bodies the original folds with other classes (QueryInterface 0x3cab40, AddRef 0x148fb0, Release 0x2c1170,
// BeginPass 0x300a0).
#include "sg/cIDisplayList.h"

#include <cstdint>

namespace nGZSceneGraph {

class cDefaultDisplayListRenderStrategy final : public cIDisplayListRenderStrategy {
public:
    static constexpr uint32_t kIid = 0xbf077fd; // the strategy interface (QueryInterface also answers 1)

    cDefaultDisplayListRenderStrategy() = default;
    cDefaultDisplayListRenderStrategy(const cDefaultDisplayListRenderStrategy &) = delete;
    cDefaultDisplayListRenderStrategy &operator=(const cDefaultDisplayListRenderStrategy &) = delete;

    bool QueryInterface(uint32_t iid, void **out) override; // 0: kIid or 1 (counted); a null out refused
    uint32_t AddRef() override;                             // 1: the new count
    uint32_t Release() override;                            // 2: the new count; at 0 the object deletes itself, 0
    void BeginPass(Pass *pass) override;                    // 3: nothing
    // 4 (0xaa2350): the pass's device state readied (the view's handle texture's slot 12, or Reset(8) without one),
    // the projection, the viewport and depth range states applied, the target cleared as the viewer says (its slot
    // 104), the count added to the pass's draw stats and each item drawn by its variant (slot 11) in order.
    void Render(Pass *pass, DrawItem *items, uint32_t count) override;
    virtual ~cDefaultDisplayListRenderStrategy() = default; // 5: the deleting destructor (0xa9e560)

private:
    uint32_t mRefCount = 0; // +4
};
static_assert(sizeof(cDefaultDisplayListRenderStrategy) == 8, "the original's 8 bytes");

} // namespace nGZSceneGraph
