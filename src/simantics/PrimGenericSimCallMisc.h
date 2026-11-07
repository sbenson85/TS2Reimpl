#pragma once
// Small helpers of the later GenericSimCall cases (opcode 0x01): outfit rows, handle pairs, GUID tests and the little float
// functions the position cases use. The pure ones are differential-tested (tests/simantics_prims_gsc_misc.cpp).
#include "game/game.h"
#include "simantics/PrimXCommon.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace gscmisc {

// A row of ten words (0x28 bytes): the elements the text service fills into a std::vector (cases 59, 81). The first nine are
// reference-counted pointers; destroying a row releases them (slot 2) from the ninth to the first (the vector destructor
// 0x26b820 does: it never touches the tenth word, which is not a pointer to release; lane Z4 found that ours released it too).
struct RefRow {
    void *mRefs[10] = {};
    RefRow() = default;
    RefRow(const RefRow &) = delete;
    RefRow &operator=(const RefRow &) = delete;
    ~RefRow() noexcept
    {
        for (int i = 8; i >= 0; --i) {
            if (mRefs[i]) primX::Call<uint32_t>(mRefs[i], 2);
        }
    }
};
static_assert(sizeof(RefRow) == 0x28, "RefRow layout");

// Two reference-counted pointers a slot hands out together (8 bytes): destroyed second then first (0x2cbb50), assigned
// pointer by pointer with the holders' rule (0x57cae0 -> 0x142080: AddRef the new, store, Release the old; nothing when equal).
struct HandlePair {
    void *mFirst = nullptr;
    void *mSecond = nullptr;
    HandlePair() = default;
    HandlePair(const HandlePair &) = delete;
    HandlePair &operator=(const HandlePair &) = delete;
    ~HandlePair() noexcept
    {
        if (mSecond) primX::Call<uint32_t>(mSecond, 2);
        if (mFirst) primX::Call<uint32_t>(mFirst, 2);
    }
    static void AssignOne(void *&slot, void *object)
    {
        if (object == slot) return;
        void *const old = slot;
        if (object) primX::Call<uint32_t>(object, 1);
        slot = object;
        if (old) primX::Call<uint32_t>(old, 2);
    }
    void Assign(const HandlePair &other)
    {
        if (&other != this) AssignOne(mFirst, other.mFirst);
        AssignOne(mSecond, other.mSecond);
    }
};
static_assert(sizeof(HandlePair) == 8, "HandlePair layout");

// 0x4f5de0 (__thiscall, one stack argument): the 16 bytes at `a` equal the 16 bytes at `b`.
inline bool GuidEquals(const uint8_t *a, const uint8_t *b)
{
    return std::memcmp(a, b, 16) == 0;
}

// 0x57edb0 (__thiscall, one stack argument, float in st0): the squared distance between two fixed-point pairs, each coordinate
// scaled by the constant at 0xdc87b8 first. Computed in single precision in the original's order.
inline float ScaledDistanceSquared(const int32_t *a, const int32_t *b, float scale)
{
    const float ay = static_cast<float>(a[1]) * scale;
    const float by = static_cast<float>(b[1]) * scale;
    const float bx = static_cast<float>(b[0]) * scale;
    float dy = by - ay;
    const float ax = static_cast<float>(a[0]) * scale;
    dy = dy * dy;
    float dx = bx - ax;
    dx = dx * dx;
    return dx + dy;
}

// 0x5a9eb0 (__cdecl, float in st0): the square root taken in double precision and rounded to float; a negative number goes to the
// CRT's slow path and gives NaN (the sign of that NaN is not reproduced: every caller only compares the result).
inline float FloatSqrt(float x)
{
    if (0.0 > static_cast<double>(x)) return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>(std::sqrt(static_cast<double>(x)));
}

} // namespace gscmisc
