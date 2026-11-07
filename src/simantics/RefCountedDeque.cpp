#include "simantics/RefCountedDeque.h"

#include "game/vtable.h"

#include <stdexcept>
#include <utility>

namespace {

using RefCountFn = uint32_t(__thiscall *)(void *);

constexpr int kAddRefSlot = 1;
constexpr int kReleaseSlot = 2;

void AddRef(void *object)
{
    VirtualCall<RefCountFn, kAddRefSlot>(object);
}

void Release(void *object)
{
    VirtualCall<RefCountFn, kReleaseSlot>(object);
}

} // namespace

void __cdecl ThrowDequeRangeError()
{
    std::_Xout_of_range("invalid deque<T> subscript");
}

void RefCountedDeque::PushBack(void *const *itemRef)
{
    void *const item = *itemRef;
    mItems.push_back(item);
    if (item) AddRef(item);
}

void RefCountedDeque::PopFront() noexcept
{
    void *const front = mItems.front();
    if (front) Release(front);
    mItems.pop_front();
}

void RefCountedDeque::Clear() noexcept
{
    while (!mItems.empty()) {
        void *const back = mItems.back();
        if (back) Release(back);
        mItems.pop_back();
    }
    // Frees the blocks and the map: the original's tidy leaves map and map size at 0.
    std::deque<void *>().swap(mItems);
}

void **RefCountedDeque::At(uint32_t index)
{
    return &mItems.at(index);
}
