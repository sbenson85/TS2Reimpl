#include "simantics/PrimAACommon.h"

#include "gz/cGZPropertySet.h"
#include "rz/cGZAllocatorService.h"

#include <windows.h>

#include <stdexcept>

// The thread-safe-statics helpers of our CRT, used on the game's guard variable (as the allocator service's own code
// does).
extern "C" __declspec(thread) int _Init_thread_epoch;

namespace primAA {

namespace {

constexpr size_t kShortMax = 0x7fffffff;  // 0x141210
constexpr size_t kAttrMax = 0x0ccccccc;   // 0x5a9d90: the largest element count of the 0x14-byte vector

[[noreturn]] void ThrowTooLong()
{
    // The original throws std::range_error through 0x8211b4 with a string constant (0x1443b0, 0x5a9820).
    throw std::range_error("vector too long");
}

} // namespace

void *AllocateThroughAllocatorService(size_t bytes)
{
    int *guard = &ts2_rva_10c95d8;
    void **service = &ts2_rva_10c95d4;
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            try {
                *service = GetAllocatorService();
            } catch (...) {
                _Init_thread_abort(guard);
                throw;
            }
            _Init_thread_footer(guard);
        }
    }
    using AllocFn = void *(__thiscall *)(void *, uint32_t);
    return VirtualCall<AllocFn, 3>(*service, static_cast<uint32_t>(bytes));
}

// 0x5799b0 -> 0x5798a0 (room left) or 0x47b780 (grow): the new buffer has max(capacity * 1.5, size + 1) elements.
void ShortVec::PushBack(const int16_t &v)
{
    if (last != end) {
        *last = v;
        ++last;
        return;
    }
    const size_t size = static_cast<size_t>(last - first);
    if (size == kShortMax) ThrowTooLong();
    const size_t cap = static_cast<size_t>(end - first);
    const size_t newSize = size + 1;
    size_t newCap;
    if (cap > kShortMax - (cap >> 1)) {
        newCap = kShortMax;
    } else {
        newCap = cap + (cap >> 1);
        if (newCap < newSize) newCap = newSize;
    }
    const int16_t value = v; // read before the old buffer goes away
    int16_t *buffer = static_cast<int16_t *>(AllocateThroughAllocatorService(newCap * 2));
    buffer[size] = value;
    for (size_t i = 0; i < size; ++i) buffer[i] = first[i];
    if (first) FreeThroughAllocatorService(first, static_cast<uint32_t>(cap));
    first = buffer;
    last = buffer + newSize;
    end = buffer + newCap;
}

// 0x5a9480: frees the buffer through the allocator service (the second argument is unused) and zeroes the three words.
void ShortVec::Tidy()
{
    if (first) {
        FreeThroughAllocatorService(first, static_cast<uint32_t>(end - first));
        first = nullptr;
        last = nullptr;
        end = nullptr;
    }
}

// 0x579a10 -> 0x579920 (room left) or 0x579a70 (grow, then 0x5a9270 swaps the buffers in).
void AttrVec::PushBack(const Attr &v)
{
    if (last != end) {
        *last = v;
        ++last;
        return;
    }
    const size_t size = static_cast<size_t>(last - first);
    if (size == kAttrMax) ThrowTooLong();
    const size_t cap = static_cast<size_t>(end - first);
    const size_t newSize = size + 1;
    size_t newCap;
    if (cap > kAttrMax - (cap >> 1)) {
        newCap = kAttrMax;
    } else {
        newCap = cap + (cap >> 1);
        if (newCap < newSize) newCap = newSize;
    }
    const Attr value = v;
    Attr *buffer = static_cast<Attr *>(AllocateThroughAllocatorService(newCap * sizeof(Attr)));
    buffer[size] = value;
    for (size_t i = 0; i < size; ++i) buffer[i] = first[i];
    if (first) FreeThroughAllocatorService(first, static_cast<uint32_t>(cap));
    first = buffer;
    last = buffer + newSize;
    end = buffer + newCap;
}

// 0x57c590 -> 0x5796a0: the elements of `other` in a buffer of exactly that many (nothing when it is empty).
void AttrVec::CopyFrom(const AttrVec &other)
{
    const size_t count = static_cast<size_t>(other.last - other.first);
    if (count == 0) return;
    if (count > kAttrMax) ThrowTooLong();
    Attr *buffer = static_cast<Attr *>(AllocateThroughAllocatorService(count * sizeof(Attr)));
    for (size_t i = 0; i < count; ++i) buffer[i] = other.first[i];
    first = buffer;
    last = buffer + count;
    end = buffer + count;
}

// 0x5a9640: frees the buffer through the allocator service and zeroes the three words.
void AttrVec::Tidy()
{
    if (first) {
        FreeThroughAllocatorService(first, static_cast<uint32_t>(end - first));
        first = nullptr;
        last = nullptr;
        end = nullptr;
    }
}

void *NewPropertySet()
{
    // The original inlines the base constructor 0x1bec45 and stores the vtable 0xd9f718 itself (an empty set, count 0);
    // lane EU: ours builds our class (src/gz/cGZPropertySet.h), so the set carries our vtable.
    return static_cast<cIGZPropertySetString *>(new cGZPropertySetString);
}

PropInt::PropInt(int32_t v) : vtable(GameAddress<void>(0xda3d48)), value(static_cast<uint32_t>(v)) {}

PropBool::PropBool(bool v) : vtable(GameAddress<void>(0xd9c670)), value(v ? 1 : 0) {}

cRZString *NewStringCopy(cRZString &source)
{
    void *block = ::operator new(sizeof(cRZString));
    return static_cast<cRZString *>(block)->Construct(source, 0, 0xffffffffu);
}

} // namespace primAA
