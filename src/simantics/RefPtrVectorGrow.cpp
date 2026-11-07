#include "game/vtable.h"
#include "simantics/cTSBehaviorTree.h"

#include <corecrt.h>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <xutility>

// RefPtrVector::EmplaceReallocate (0x14aac0): the reallocating insert of the game's std::vector<counted pointer> whose storage
// comes from operator new (ServiceRefVector::InsertGrown is the same algorithm over the allocator service). Lane AJ.

namespace {

constexpr int kAddRefSlot = 1;
constexpr int kReleaseSlot = 2;
// The most elements a vector of 4-byte pointers can hold (std::vector::max_size here).
constexpr uint32_t kMaxElements = 0x3fffffff;
// Blocks of this many bytes and more are allocated 32-byte aligned: the real pointer is stored in the dword before the block.
constexpr uint32_t kBigBlockBytes = 0x1000;
constexpr uint32_t kBigBlockExtra = 0x23;

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

// The vector's allocate (small blocks as they are, big ones padded and aligned).
void **AllocateElements(uint32_t bytes)
{
    if (bytes < kBigBlockBytes) return static_cast<void **>(::operator new(bytes));
    if (bytes + kBigBlockExtra <= bytes) std::_Xbad_alloc();
    void *const raw = ::operator new(bytes + kBigBlockExtra);
    if (!raw) _invalid_parameter_noinfo_noreturn(); // operator new does not return null; the library's check stays
    void **const block = reinterpret_cast<void **>((reinterpret_cast<uintptr_t>(raw) + kBigBlockExtra) & ~static_cast<uintptr_t>(0x1f));
    block[-1] = raw;
    return block;
}

// The vector's deallocate (0x152c10 for the new block, inline for the old one): frees a block of `bytes` made by AllocateElements.
void FreeElements(void **block, uint32_t bytes)
{
    void *raw = block;
    if (bytes >= kBigBlockBytes) {
        raw = block[-1];
        if (reinterpret_cast<uintptr_t>(block) - reinterpret_cast<uintptr_t>(raw) - 4 > 0x1f) _invalid_parameter_noinfo_noreturn();
    }
    ::operator delete(raw);
}

} // namespace

void **RefPtrVector::EmplaceReallocate(void **where, void *const *value)
{
    // The element counts come from arithmetic shifts of the byte distances (a corrupt vector gives negative counts, which the
    // unsigned compares below then reject).
    const uint32_t whereIndex = static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<uintptr_t>(where) - reinterpret_cast<uintptr_t>(mFirst)) >> 2);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<uintptr_t>(mLast) - reinterpret_cast<uintptr_t>(mFirst)) >> 2);
    if (size == kMaxElements) std::_Xlength_error("vector too long"); // never true for a valid vector: the count is a sign-extended shift
    const uint32_t newSize = size + 1;

    // The new capacity: one and a half times the old one, at least the new size.
    const uint32_t capacity = static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<uintptr_t>(mEnd) - reinterpret_cast<uintptr_t>(mFirst)) >> 2);
    if (capacity > kMaxElements - (capacity >> 1)) std::_Xbad_alloc();
    uint32_t newCapacity = capacity + (capacity >> 1);
    if (newCapacity < newSize) newCapacity = newSize;
    if (newCapacity > kMaxElements) std::_Xbad_alloc();

    const uint32_t newBytes = newCapacity * 4;
    void **const newBlock = AllocateElements(newBytes);
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
        FreeElements(newBlock, newBytes);
        throw;
    }

    // The new block replaces the old one: the old elements are released and their block freed.
    if (mFirst) {
        ReleaseRange(mFirst, mLast);
        FreeElements(mFirst, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mEnd) - reinterpret_cast<uintptr_t>(mFirst)) & ~3u);
    }
    mFirst = newBlock;
    mLast = newBlock + newSize;
    mEnd = newBlock + newCapacity;
    return newElement;
}
