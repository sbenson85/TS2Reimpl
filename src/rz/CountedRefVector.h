#pragma once
#include "game/vtable.h"
#include "rz/GameAllocator.h" // GZServiceAllocator

#include <cstdint>
#include <vector>

// The game's vectors of counted pointers that grow through the allocator service. Their range insert (0x1d6520) is library
// code (db/library-matches.jsonl: vector::_Insert_counted_range), so this is the toolchain's vector over the same three
// pointers. The element has a copy and no move, as the original's: growing counts the elements again and releases the old
// ones. Slot 1 of the object counts, slot 2 releases; a null is left alone.
struct CountedRefElement {
    using CountFn = uint32_t(__thiscall *)(void *);

    void *mObject;

    CountedRefElement(const CountedRefElement &other) : mObject(other.mObject)
    {
        if (mObject) VirtualCall<CountFn, 1>(mObject);
    }
    CountedRefElement &operator=(const CountedRefElement &other)
    {
        void *const old = mObject;
        if (other.mObject) VirtualCall<CountFn, 1>(other.mObject);
        mObject = other.mObject;
        if (old) VirtualCall<CountFn, 2>(old);
        return *this;
    }
    ~CountedRefElement()
    {
        if (mObject) VirtualCall<CountFn, 2>(mObject);
    }
};
static_assert(sizeof(CountedRefElement) == 4, "a counted pointer");

using CountedServiceVector = std::vector<CountedRefElement, GZServiceAllocator<CountedRefElement>>;
static_assert(sizeof(CountedServiceVector) == 12, "the game's three pointers");

// insert(end, first, first + count) on a vector the game owns (`vector` is its three pointers).
inline void AppendCounted(void *vector, void *const *first, uint32_t count)
{
    auto *const target = static_cast<CountedServiceVector *>(vector);
    const auto *const from = reinterpret_cast<const CountedRefElement *>(first);
    target->insert(target->end(), from, from + count);
}
