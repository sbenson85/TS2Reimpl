#include "simantics/PrimGenericSimCallOriginals.h"
#include "rz/RZAutoRef.h"
#include "simantics/PrimGenericSimCall.h"
#include "simantics/PrimGenericSimCallMath.h"
#include "simantics/PrimXCommon.h"

#include <cstring>
#include <emmintrin.h>
#include <xmmintrin.h>

using primX::Call;
using gscmath::RoundViaMagic;

// GenericSimCall (opcode 0x01) cases, group F: the cases with single-precision arithmetic (62, 66) and the two that move an
// object through the original's placement function 0x7e8c60 (49, 68).

namespace {
constexpr uint32_t kDefaultResult = 1;

} // namespace

// Case 68: places the stack object (none throws 0xa) where it is: its slot 42 fills a 0x60-byte block, its slot 43 gives the
// position; result 1 when the placement function accepts, else 0.
uint32_t GenericSimCall::Case68()
{
    void *const obj = StackObject();
    alignas(16) uint8_t block[0x60] = {};
    Call<void>(obj, 42, block);
    uint32_t position = Call<uint32_t>(obj, 43);
    return gscorig::PlaceObject(obj, block, &position) ? 1 : 0;
}

// Case 49: when the stack object's slot 13 (with 9) already equals argument 0, result 1. Otherwise its slot 224 is set to the
// argument (unsigned word) and slot 222 to 0, the placement of case 68 is tried, and slots 224 / 222 restore the old value (low
// word) and 0. The result is that of the placement (1 / 0). A list-shaped local (0x57c6d0 builds it, 0x1d20f0 frees it) is
// built and dropped without being used.
uint32_t GenericSimCall::Case49()
{
    void *const obj = StackObject();
    const int32_t current = Call<int16_t>(obj, 13, 9u);
    const int16_t wanted = Arg(0);
    if (current == static_cast<int32_t>(wanted)) return 1;

    struct Unused {
        alignas(4) uint8_t mBytes[0x20];
        Unused() { gscorig::ConstructList(mBytes); }
        ~Unused() noexcept { gscorig::DestructList(mBytes); }
    } unused;
    alignas(16) uint8_t block[0x60] = {};
    Call<void>(obj, 42, block);
    uint32_t position = Call<uint32_t>(obj, 43);
    Call<void>(obj, 224, uint32_t(uint16_t(wanted)));
    Call<void>(obj, 222, 0u);
    const uint32_t result = gscorig::PlaceObject(obj, block, &position) ? 1 : 0;
    Call<void>(obj, 224, uint32_t(uint16_t(current)));
    Call<void>(obj, 222, 0u);
    return result;
}

// Case 62: the entry the context's slot-9 list (slot 11) finds for argument 0 (none throws 0x9a) is scaled by argument 4 / 100:
// with argument 1 equal to 0, its slot 53 value is scaled, rounded and split into arguments 2 (quotient) and 3 (remainder) by
// 10000; otherwise arguments 2 * 10000 + 3 are scaled, rounded and handed to the entry's slot 54.
uint32_t GenericSimCall::Case62()
{
    void *const list = ContextSlot(9);
    const int32_t id = Arg(0);
    const bool toArguments = Arg(1) == 0;
    void *const entry = Call<void *>(list, 11, id);
    if (!entry) Throw(0x9a);
    const float scale = _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), static_cast<int32_t>(Arg(4))));
    const float k = 0.01f; // the float at 0xda74e8
    if (toArguments) {
        const int32_t n = Call<int32_t>(entry, 53);
        float f = _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), n));
        f = _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(f), _mm_set_ss(scale)));
        f = _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(f), _mm_set_ss(k)));
        const int32_t r = RoundViaMagic(f);
        SetArg(2, static_cast<uint32_t>(r / 10000));
        SetArg(3, static_cast<uint32_t>(r % 10000));
    } else {
        const int32_t x = Arg(2);
        const int32_t y = Arg(3);
        const int32_t combined = x * 10000 + y;
        float f = _mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), combined));
        f = _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(f), _mm_set_ss(scale)));
        f = _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(f), _mm_set_ss(k)));
        Call<void>(entry, 54, static_cast<uint32_t>(RoundViaMagic(f)));
    }
    return kDefaultResult;
}

// Case 66: the frame's slot 26 (low byte) must be at least 4 (else throws 0x7c); its slot 30 gives four words w0..w3 (indices
// 0..3). w0 must not be negative (else throws 0x1a). When w1 >= -1001, w2 <= 0x3e9 and the object answers the interface query
// 0x5a69d3d5: the held object's slot 10 (w3, 1) gives a scale; the point w1 + (w2 - w1) * scale / 1000 is rounded and passed to
// slot 11 with (w0, 1).
uint32_t GenericSimCall::Case66()
{
    if (static_cast<uint8_t>(Call<uint8_t>(mFrame, 26)) < 4) Throw(0x7c);
    const int16_t w0 = Call<int16_t>(mFrame, 30, 0u);
    const int16_t w1 = Call<int16_t>(mFrame, 30, 1u);
    const int16_t w2 = Call<int16_t>(mFrame, 30, 2u);
    const int16_t w3 = Call<int16_t>(mFrame, 30, 3u);
    if (w0 < 0) Throw(0x1a);
    struct Holder {
        RZAutoRef mRef{nullptr};
        ~Holder() noexcept { mRef.ReleaseUnguarded(); }
    } held;
    if (w1 >= -1001 && w2 <= 0x3e9) {
        held.mRef.Reset();
        if ((Call<uint32_t>(mSelf, 0, 0x5a69d3d5u, &held.mRef.mObject) & 0xff) != 0) {
            const int32_t span = static_cast<int32_t>(w2) - static_cast<int32_t>(w1);
            const int32_t scaleInt = Call<int16_t>(held.mRef.mObject, 10, static_cast<int32_t>(w3), 1u);
            __m128 v = _mm_cvtsi32_ss(_mm_setzero_ps(), span);
            v = _mm_mul_ss(v, _mm_cvtsi32_ss(_mm_setzero_ps(), scaleInt));
            v = _mm_mul_ss(v, _mm_set_ss(0.001f)); // the float at 0xda20dc
            v = _mm_add_ss(v, _mm_cvtsi32_ss(_mm_setzero_ps(), static_cast<int32_t>(w1)));
            const int32_t r = RoundViaMagic(_mm_cvtss_f32(v));
            Call<void>(held.mRef.mObject, 11, static_cast<int32_t>(w0), static_cast<uint32_t>(r), 1u);
        }
    }
    return kDefaultResult;
}
