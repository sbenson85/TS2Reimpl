// nTSSG::cDisplayListDynamicRenderStrategy (lane IR). See src/sg/cDisplayListDynamicRenderStrategy.h.
#include "sg/cDisplayListDynamicRenderStrategy.h"

#include "game/original.h"
#include "game/vtable.h"

#include <algorithm>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// The wrapped strategy's interface (its +8).
void *InnerFace(const nTSSG::cDisplayListDynamicRenderStrategy *owner)
{
    return static_cast<uint8_t *>(owner->mInner) + 8;
}

} // namespace

namespace nTSSG {

cDisplayListDynamicRenderStrategy *cDisplayListDynamicRenderStrategy::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cDisplayListDynamicRenderStrategy *), 0x36f220>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cDisplayListDynamicRenderStrategy));
    return this;
}

uint32_t cDisplayListDynamicRenderStrategyFace::ForwardSlot3(void *argument)
{
    return Call<3>(InnerFace(Owner()), argument);
}

uint32_t cDisplayListDynamicRenderStrategyFace::ForwardSlot6()
{
    Owner()->mInnerChanged = 1;
    return Call<6>(InnerFace(Owner()));
}

uint32_t cDisplayListDynamicRenderStrategyFace::ForwardSlot7(void *argument)
{
    return Call<7>(InnerFace(Owner()), argument);
}

void cDisplayListDynamicRenderStrategyFace::Register(void *item)
{
    std::vector<void *> &items = Owner()->mRegistered;
    if (std::find(items.begin(), items.end(), item) == items.end()) items.push_back(item);
}

void cDisplayListDynamicRenderStrategyFace::Unregister(void *item)
{
    std::vector<void *> &items = Owner()->mRegistered;
    const auto it = std::find(items.begin(), items.end(), item);
    if (it != items.end()) items.erase(it);
}

} // namespace nTSSG
