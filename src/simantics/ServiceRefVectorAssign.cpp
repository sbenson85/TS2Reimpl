// ServiceRefVector::AssignCounted (0x41d6e0): assigning a range to a vector of counted pointers in allocator-service storage
// (std::vector::assign(first, first + count) of the game's instantiation). Lane FU; its callers are the neighbourhood
// manager's list copies (GetNamedList, Filter, FilterWith), cTSFamily::CountOutfitCombinations and two cEdithObjectModule slots.
#include "simantics/ServiceRefVector.h"

#include "game/vtable.h"
#include "rz/RZMemory.h"

#include <cstddef>
#include <stdexcept>

namespace {

constexpr int kAddRefSlot = 1;
constexpr int kReleaseSlot = 2;
constexpr size_t kMaxElements = 0x3fffffff; // max_size of a vector of 4-byte pointers

void AddRef(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), kAddRefSlot>(object);
}

void Release(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), kReleaseSlot>(object);
}

// The counted pointer's assignment (cRZAutoRefCount::operator=): nothing for itself or the same object; else the new object
// is counted, stored, and the old one released (null ones skipped).
void AssignElement(void **to, void *const *from)
{
    if (from == to) return;
    void *const object = *from;
    void *const old = *to;
    if (object == old) return;
    if (object) AddRef(object);
    *to = object;
    if (old) Release(old);
}

// Releases [first, last) (0x14a460).
void ReleaseRange(void *const *first, void *const *last)
{
    for (void *const *element = first; element != last; ++element)
        if (*element) Release(*element);
}

// Copies `count` pointers from `from` into raw storage at `to`, counting each (0x172b10). If counting throws, the copies made
// so far are released (the one being made is not) and the error goes on. Returns the end of the copies.
void **CopyCounted(void *const *from, uint32_t count, void **to)
{
    void **copied = to;
    try {
        for (; count != 0; --count, ++from) {
            *copied = *from;
            if (*copied) AddRef(*copied);
            ++copied;
        }
    } catch (...) {
        ReleaseRange(to, copied);
        throw;
    }
    return copied;
}

} // namespace

void ServiceRefVector::AssignCounted(void *const *first, uint32_t count)
{
    const size_t capacity = static_cast<size_t>(mEnd - mFirst);
    if (count > capacity) {
        // A new block: one and a half times the old capacity, at least `count`, at most the maximum. The old elements are
        // released and their block freed first.
        if (count > kMaxElements) std::_Xlength_error("vector too long");
        size_t newCapacity;
        if (capacity > kMaxElements - capacity / 2) {
            newCapacity = kMaxElements;
        } else {
            newCapacity = capacity + capacity / 2;
            if (newCapacity < count) newCapacity = count;
        }
        if (mFirst) {
            ReleaseRange(mFirst, mLast);
            RZMemory::Free(mFirst);
            mFirst = nullptr;
            mLast = nullptr;
            mEnd = nullptr;
        }
        void **const block = static_cast<void **>(RZMemory::Malloc(static_cast<uint32_t>(newCapacity * sizeof(void *))));
        mFirst = block;
        mLast = block;
        mEnd = block + newCapacity;
        mLast = CopyCounted(first, count, block);
        return;
    }

    const size_t size = static_cast<size_t>(mLast - mFirst);
    if (count > size) {
        // Room enough: the existing elements are assigned, the rest copied in after them.
        void **to = mFirst;
        for (; to != mLast; ++to, ++first) AssignElement(to, first);
        mLast = CopyCounted(first, count - static_cast<uint32_t>(size), mLast);
        return;
    }

    // No more than the vector holds: the first `count` are assigned, the rest released.
    void **to = mFirst;
    void **const newLast = mFirst + count;
    for (; count != 0; --count, ++to, ++first) AssignElement(to, first);
    ReleaseRange(newLast, mLast);
    mLast = newLast;
}
