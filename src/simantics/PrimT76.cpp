#include "app/AppServices2.h"
#include "rz/cGZAllocatorService.h"
#include "simantics/PrimTCommon.h"

#include <algorithm>
#include <functional>
#include <new>
#include <stdexcept>

using namespace primT;

// ---------------------------------------------------------------------------------------------------------
// The array the object keeps: three pointers (first, last, end of storage) of 16-bit values, a std::vector<short>
// laid out the same way whose storage comes from the allocator service. The game's own (unoptimised) copies of the
// vector operations are reproduced here, because their growth rule is visible in the capacity: 1.5x, at least the
// size needed (0x47b780, 0x52bf90).
// ---------------------------------------------------------------------------------------------------------

namespace {

struct ShortVec {
    int16_t *mFirst; // +0
    int16_t *mLast;  // +4
    int16_t *mEnd;   // +8

    uint32_t Size() const { return static_cast<uint32_t>(mLast - mFirst); }
    uint32_t Capacity() const { return static_cast<uint32_t>(mEnd - mFirst); }
};

constexpr uint32_t kMaxCount = 0x7fffffff;

// The grown capacity: 1.5 times the old one (the maximum when that would overflow), at least `needed`.
uint32_t GrownCapacity(uint32_t capacity, uint32_t needed)
{
    const uint32_t half = capacity >> 1;
    if (capacity > kMaxCount - half) return kMaxCount;
    const uint32_t grown = capacity + half;
    return grown < needed ? needed : grown;
}

int16_t *AllocateShorts(uint32_t count)
{
    return static_cast<int16_t *>(as2::AllocateViaAllocatorService(count * 2, 0));
}

void ReleaseOld(ShortVec &v)
{
    if (v.mFirst) FreeThroughAllocatorService(v.mFirst, v.Capacity());
}

// 0x47b780 (__thiscall, one stack argument for the position, one for the value pointer): insertion into a full
// vector. Returns the new position of the element.
int16_t *InsertGrow(ShortVec &v, int16_t *pos, int16_t value)
{
    const uint32_t size = v.Size();
    if (size == kMaxCount) std::_Xlength_error("vector too long");
    const uint32_t newCapacity = GrownCapacity(v.Capacity(), size + 1);
    int16_t *fresh = AllocateShorts(newCapacity);
    int16_t *placed = fresh + (pos - v.mFirst);
    *placed = value;
    int16_t *out = fresh;
    for (int16_t *in = v.mFirst; in != pos; ++in) *out++ = *in;
    if (pos != v.mLast) {
        out = placed + 1;
        for (int16_t *in = pos; in != v.mLast; ++in) *out++ = *in;
    }
    ReleaseOld(v);
    v.mFirst = fresh;
    v.mLast = fresh + size + 1;
    v.mEnd = fresh + newCapacity;
    return placed;
}

// 0x57bc90: insert(pos, value).
void Insert(ShortVec &v, int16_t *pos, int16_t value)
{
    if (v.mLast == v.mEnd) {
        InsertGrow(v, pos, value);
        return;
    }
    if (pos == v.mLast) {
        *v.mLast = value;
        ++v.mLast;
        return;
    }
    const int16_t copy = value;
    int16_t *oldLast = v.mLast;
    *oldLast = oldLast[-1];
    ++v.mLast;
    std::memmove(pos + 1, pos, static_cast<size_t>(oldLast - 1 - pos) * 2);
    *pos = copy;
}

// 0x5799b0: push_back.
void PushBack(ShortVec &v, int16_t value)
{
    if (v.mLast != v.mEnd) {
        *v.mLast = value;
        ++v.mLast;
    } else {
        InsertGrow(v, v.mLast, value);
    }
}

// 0x5a9ca0: erase(pos) of one element.
void Erase(ShortVec &v, int16_t *pos)
{
    std::memmove(pos, pos + 1, static_cast<size_t>(v.mLast - (pos + 1)) * 2);
    --v.mLast;
}

// 0x57b6f0: resize(count); new elements are zero (0x52bf90 and 0x57bbb0 both clear them: the value argument the
// caller passes, a stack byte it never set, is ignored).
void Resize(ShortVec &v, uint32_t count)
{
    const uint32_t size = v.Size();
    if (count < size) {
        v.mLast = v.mFirst + count;
    } else if (count > size) {
        if (count > v.Capacity()) {
            if (count >= 0x80000000u) std::_Xlength_error("vector too long");
            const uint32_t newCapacity = GrownCapacity(v.Capacity(), count);
            int16_t *fresh = AllocateShorts(newCapacity);
            std::memset(fresh + size, 0, static_cast<size_t>(count - size) * 2);
            for (uint32_t i = 0; i < size; ++i) fresh[i] = v.mFirst[i];
            ReleaseOld(v);
            v.mFirst = fresh;
            v.mLast = fresh + count;
            v.mEnd = fresh + newCapacity;
        } else {
            std::memset(v.mLast, 0, static_cast<size_t>(count - size) * 2);
            v.mLast += count - size;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Opcode 0x76
// ---------------------------------------------------------------------------------------------------------

uint32_t PrimTObject::Prim76(void *frame, const PrimParamView *param)
{
    (void)frame;
    void *table = Call<void *>(mContext, 26);
    void *target = this;
    if (Byte(param, 6) != 0) {
        void *sub = &mSecondary;
        void *source = Call<void *>(sub, 20);
        const int16_t id = Call<int16_t>(source, 12);
        target = Call<void *>(table, 29, static_cast<int32_t>(id));
        if (!target) Raise(8);
    }

    const int16_t number = static_cast<int16_t>(Word(param, 7));
    const int16_t count = static_cast<int16_t>(Call<uint32_t>(target, 266));
    if (number < 0 || count <= number) Raise(0x6c);
    ShortVec *vec = Call<ShortVec *>(target, 265, static_cast<uint32_t>(static_cast<uint16_t>(number)));
    if (!vec) Raise(0x6c);

    SimDataRef firstDest;
    firstDest.Reset();
    const int16_t value = Read(this, Byte(param, 9), Word(param, 0xa), &firstDest);

    switch (Byte(param, 5)) {
    case 0: // clear
        vec->mLast = vec->mFirst;
        return 1;
    case 1: { // length
        if (!firstDest.HasData()) Raise(0x13);
        if (!firstDest.Set(static_cast<int32_t>(vec->Size()))) Raise(0x14);
        return 1;
    }
    case 2: // resize
        Resize(*vec, static_cast<uint32_t>(static_cast<int32_t>(value)));
        return 1;
    case 3: // fill
        for (int16_t *p = vec->mFirst; p != vec->mLast; ++p) *p = value;
        return 1;
    case 4: // insert at the front
        Insert(*vec, vec->mFirst, value);
        return 1;
    case 5: // append
        PushBack(*vec, value);
        return 1;
    case 6: { // insert before an index
        const int16_t index = Read(this, Byte(param, 0xc), Word(param, 0xd));
        if (index < 0 || static_cast<uint32_t>(static_cast<int32_t>(index)) >= vec->Size()) Raise(0x6d);
        Insert(*vec, vec->mFirst + index, value);
        return 1;
    }
    case 7: // erase the front
        if (vec->Size() == 0) Raise(0x6d);
        Erase(*vec, vec->mFirst);
        return 1;
    case 8: // erase the back
        if (vec->Size() == 0) Raise(0x6d);
        --vec->mLast;
        return 1;
    case 9: { // erase at an index
        const int16_t index = Read(this, Byte(param, 0xc), Word(param, 0xd));
        if (index < 0 || static_cast<uint32_t>(static_cast<int32_t>(index)) >= vec->Size()) Raise(0x6d);
        Erase(*vec, vec->mFirst + index);
        return 1;
    }
    case 10: { // find
        SimDataRef foundDest;
        foundDest.Reset();
        const int16_t start = Read(this, Byte(param, 0xc), Word(param, 0xd), &foundDest);
        if (start < 0 || static_cast<uint32_t>(static_cast<int32_t>(start)) >= vec->Size()) return 0;
        int16_t *found = std::find(vec->mFirst + start, vec->mLast, value);
        if (found == vec->mLast) return 0;
        if (!foundDest.HasData()) Raise(0x13);
        if (!foundDest.Set(static_cast<int32_t>(found - vec->mFirst))) Raise(0x14);
        return 1;
    }
    case 11: { // swap
        const int16_t other = Read(this, Byte(param, 0xc), Word(param, 0xd));
        if (value < 0 || static_cast<uint32_t>(static_cast<int32_t>(value)) >= vec->Size()) Raise(0x6d);
        if (other < 0 || static_cast<uint32_t>(static_cast<int32_t>(other)) >= vec->Size()) Raise(0x6d);
        std::swap(vec->mFirst[value], vec->mFirst[other]);
        return 1;
    }
    case 12: // sort, descending
        std::sort(vec->mFirst, vec->mLast, std::greater<int16_t>());
        return 1;
    case 13: // sort, ascending
        std::sort(vec->mFirst, vec->mLast, std::less<int16_t>());
        return 1;
    default:
        Raise(1);
    }
}
