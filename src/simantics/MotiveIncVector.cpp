// The person's motive-increment vector (std::vector<cMotiveInc>, +0x5ec): its reallocating insert. In its own file so
// Prim1D calls it rather than an inlined copy (Prim1D's test stands it in).
#include "simantics/PrimPerson.h"
#include "game/game.h"

#include <memory>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------------
// 0x50da10, std::vector<cMotiveInc>::_Emplace_reallocate
// ---------------------------------------------------------------------------------------------------------------------

namespace {

// cMotiveInc's copy constructor: a fresh vtable (whatever the source holds), the id word and the two floats.
void ConstructMotiveInc(MotiveInc *to, const MotiveInc &from)
{
    to->mVTable = GameAddress<void>(0xdced28);
    to->mMotive = from.mMotive;
    to->mRate = from.mRate;
    to->mTarget = from.mTarget;
}

void CopyMotiveIncs(const MotiveInc *first, const MotiveInc *last, MotiveInc *to)
{
    for (; first != last; ++first, ++to) ConstructMotiveInc(to, *first);
}

} // namespace

MotiveInc *__fastcall MotiveEmplaceReallocate(MotiveIncVector *self, void *, MotiveInc *where, const MotiveInc *value)
{
    constexpr size_t kMaxSize = 0x0fffffff;
    const size_t oldSize = static_cast<size_t>(self->mLast - self->mFirst);
    // Unreachable in a 32-bit process (the size is an arithmetic shift of the byte distance); the library's check.
    if (oldSize == kMaxSize) std::_Xlength_error("vector too long");
    const size_t newSize = oldSize + 1;
    // The library's growth rule: 1.5 times the capacity, at least the new size, at most max_size.
    const size_t oldCapacity = static_cast<size_t>(self->mEnd - self->mFirst);
    size_t newCapacity;
    if (oldCapacity > kMaxSize - oldCapacity / 2) newCapacity = kMaxSize;
    else {
        const size_t geometric = oldCapacity + oldCapacity / 2;
        newCapacity = geometric < newSize ? newSize : geometric;
    }
    std::allocator<MotiveInc> allocator;
    MotiveInc *block = allocator.allocate(newCapacity);
    MotiveInc *inserted = block + (where - self->mFirst);
    ConstructMotiveInc(inserted, *value); // before the old elements move: value may be one of them
    if (where == self->mLast) {
        CopyMotiveIncs(self->mFirst, self->mLast, block);
    } else {
        CopyMotiveIncs(self->mFirst, where, block);
        CopyMotiveIncs(where, self->mLast, inserted + 1);
    }
    // The elements need no destruction (cMotiveInc's destructor does nothing); the old block is freed.
    if (self->mFirst) allocator.deallocate(self->mFirst, oldCapacity);
    self->mFirst = block;
    self->mLast = block + newSize;
    self->mEnd = block + newCapacity;
    return inserted;
}

// ---------------------------------------------------------------------------------------------------------------------
// 0x50f4d0, std::vector<cMotiveInc>::_Reallocate_exactly
// ---------------------------------------------------------------------------------------------------------------------

void __fastcall MotiveReallocateExactly(MotiveIncVector *self, void *, const uint32_t *newCapacity)
{
    std::allocator<MotiveInc> allocator;
    const size_t capacity = *newCapacity;
    const size_t size = static_cast<size_t>(self->mLast - self->mFirst);
    MotiveInc *block = allocator.allocate(capacity); // bad_array_new_length beyond max_size, as the library's
    CopyMotiveIncs(self->mFirst, self->mLast, block);
    if (self->mFirst) allocator.deallocate(self->mFirst, static_cast<size_t>(self->mEnd - self->mFirst));
    self->mFirst = block;
    self->mLast = block + size;
    self->mEnd = block + capacity;
}

// ---------------------------------------------------------------------------------------------------------------------
// 0x524d30, std::vector<cMotiveInc>::insert(where, count, value)
// ---------------------------------------------------------------------------------------------------------------------

namespace {

// A cMotiveInc with the original's special members, so the library's vector can do the work: copying constructs a
// fresh vtable and the fields (never the padding word); assignment copies the fields only (the vtable stays); no
// move members (the copy is used) and a trivial destructor (the old elements are not destroyed one by one).
class MotiveIncValue {
public:
    MotiveIncValue(const MotiveIncValue &other) { ConstructMotiveInc(&mValue, other.mValue); }
    MotiveIncValue &operator=(const MotiveIncValue &other)
    {
        mValue.mMotive = other.mValue.mMotive;
        mValue.mRate = other.mValue.mRate;
        mValue.mTarget = other.mValue.mTarget;
        return *this;
    }

private:
    MotiveInc mValue;
};
static_assert(sizeof(MotiveIncValue) == sizeof(MotiveInc), "MotiveIncValue layout");

// The person's vector seen as the library's (the same three pointers).
using MotiveIncStdVector = std::vector<MotiveIncValue>;
static_assert(sizeof(MotiveIncStdVector) == sizeof(MotiveIncVector), "std::vector layout");

} // namespace

MotiveInc **__fastcall MotiveInsertN(MotiveIncVector *self, void *, MotiveInc **out, MotiveInc *where, uint32_t count, const MotiveInc *value)
{
    auto &vector = *reinterpret_cast<MotiveIncStdVector *>(self);
    const auto inserted = vector.insert(vector.begin() + (where - self->mFirst), count, *reinterpret_cast<const MotiveIncValue *>(value));
    *out = self->mFirst + (inserted - vector.begin());
    return out;
}
