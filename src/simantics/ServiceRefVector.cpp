#include "simantics/ServiceRefVector.h"

#include "game/vtable.h"
#include "rz/RZMemory.h"

#include <cstddef>
#include <stdexcept>

namespace {

constexpr int kAddRefSlot = 1;
constexpr int kReleaseSlot = 2;
// The most elements a vector of 4-byte pointers can hold (std::vector::max_size here).
constexpr size_t kMaxElements = 0x3fffffff;

void AddRef(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), kAddRefSlot>(object);
}

void Release(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), kReleaseSlot>(object);
}

// Releases the elements [first, last) (null ones are skipped).
void ReleaseRange(void *const *first, void *const *last)
{
    for (void *const *element = first; element != last; ++element) {
        if (*element) Release(*element);
    }
}

// 0x14b8a0 (the vector's uninitialized copy): copies the pointers [first, last) to `destination` and counts each copy. If
// counting throws, the copies made so far are released (the one being made is not) and the error goes on. Returns the end of the copies.
void **CopyCounted(void *const *first, void *const *last, void **destination)
{
    void **copied = destination;
    try {
        for (void *const *source = first; source != last; ++source) {
            *copied = *source;
            if (*copied) AddRef(*copied);
            ++copied;
        }
    } catch (...) {
        ReleaseRange(destination, copied);
        throw;
    }
    return copied;
}

} // namespace

void ServiceRefVector::PushBack(void *object)
{
    if (mLast != mEnd) {
        *mLast = object;
        if (object) AddRef(object);
        ++mLast;
        return;
    }
    InsertGrown(mLast, &object);
}

void **ServiceRefVector::InsertGrown(void **where, void *const *value)
{
    const size_t whereIndex = static_cast<size_t>(where - mFirst);
    const size_t size = static_cast<size_t>(mLast - mFirst);
    if (size == kMaxElements) std::_Xlength_error("vector too long");
    const size_t newSize = size + 1;

    // The new capacity: one and a half times the old one, at least the new size, at most the maximum.
    const size_t capacity = static_cast<size_t>(mEnd - mFirst);
    size_t newCapacity;
    if (capacity > kMaxElements - capacity / 2) {
        newCapacity = kMaxElements;
    } else {
        newCapacity = capacity + capacity / 2;
        if (newCapacity < newSize) newCapacity = newSize;
    }

    void **const newBlock = static_cast<void **>(RZMemory::Malloc(static_cast<uint32_t>(newCapacity * sizeof(void *))));
    void **const newElement = newBlock + whereIndex;
    // What the new block holds while it is filled (released again if counting throws): the new element, then also the
    // elements in front of it once they are copied.
    void **heldBegin = newElement + 1;
    void **const heldEnd = newElement + 1;
    try {
        *newElement = *value;
        if (*newElement) AddRef(*newElement);
        heldBegin = newElement;
        if (where == mLast) {
            CopyCounted(mFirst, mLast, newBlock);
        } else {
            CopyCounted(mFirst, where, newBlock);
            heldBegin = newBlock;
            CopyCounted(where, mLast, newElement + 1);
        }
    } catch (...) {
        ReleaseRange(heldBegin, heldEnd);
        RZMemory::Free(newBlock);
        throw;
    }

    // The new block replaces the old one (std::vector::_Change_array): the old elements are released and their block freed.
    if (mFirst) {
        ReleaseRange(mFirst, mLast);
        RZMemory::Free(mFirst);
    }
    mFirst = newBlock;
    mLast = newBlock + newSize;
    mEnd = newBlock + newCapacity;
    return newElement;
}

void ServiceRefVector::ReleaseAndFree() noexcept
{
    if (!mFirst) return;
    ReleaseRange(mFirst, mLast);
    RZMemory::Free(mFirst);
    mFirst = nullptr;
    mLast = nullptr;
    mEnd = nullptr;
}

void ServiceBlockVector::FreeBlock() noexcept
{
    if (!mFirst) return;
    RZMemory::Free(mFirst);
    mFirst = nullptr;
    mLast = nullptr;
    mEnd = nullptr;
}
