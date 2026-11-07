// Differential tests for the pure helpers of the later GenericSimCall cases (src/simantics/PrimGenericSimCallRefs.h and
// PrimGenericSimCallMisc.h): the category bit table (0x23f55f), the family-key test (0x54d830), the GUID comparison (0x4f5de0), the
// scaled distance (0x57edb0) and the float square root (0x5a9eb0). None is hooked; the cases use them inline.
#include "difftest.h"
#include "simantics/PrimGenericSimCallMisc.h"
#include "simantics/PrimGenericSimCallRefs.h"

#include <cstring>
#include <random>

DIFF_TEST(simantics_gsc_category_bit, 0x23f55f)
{
    using Fn = uint32_t(__cdecl *)(uint32_t);
    Fn orig = ctx.Original<Fn>(0x23f55f);
    for (uint32_t index = 0; index < 64; ++index) {
        ++ctx.cases;
        const uint32_t a = orig(index);
        const uint32_t b = gscref::CategoryBit(index);
        if (a != b) ctx.Fail("index %u: original 0x%x, ours 0x%x", index, a, b);
    }
    std::mt19937 rng(0x23f55f);
    for (int i = 0; i < 2000; ++i) {
        const uint32_t index = rng();
        ++ctx.cases;
        const uint32_t a = orig(index);
        const uint32_t b = gscref::CategoryBit(index);
        if (a != b) ctx.Fail("index %u: original 0x%x, ours 0x%x", index, a, b);
    }
}

DIFF_TEST(simantics_gsc_family_key, 0x54d830)
{
    using Fn = uint32_t(__cdecl *)(const Key16 *);
    Fn orig = ctx.Original<Fn>(0x54d830);
    std::mt19937 rng(0x54d830);
    for (int i = 0; i < 20000; ++i) {
        Key16 key;
        for (uint32_t &w : key.w) w = rng();
        // Make both words match often enough that every branch is reached.
        if (rng() % 3 == 0) key.w[0] = 0xac598eacu;
        if (rng() % 3 == 0) key.w[3] = 0xb0858c5fu;
        ++ctx.cases;
        const bool a = (orig(&key) & 0xff) != 0;
        const bool b = gscref::IsFamilyKey(&key);
        if (a != b) ctx.Fail("case %d: key %08x %08x %08x %08x: original %d, ours %d", i, key.w[0], key.w[1], key.w[2], key.w[3], a, b);
    }
}

DIFF_TEST(simantics_gsc_guid_equals, 0x4f5de0)
{
    using Fn = uint32_t(__thiscall *)(const uint8_t *, const uint8_t *);
    Fn orig = ctx.Original<Fn>(0x4f5de0);
    std::mt19937 rng(0x4f5de0);
    for (int i = 0; i < 20000; ++i) {
        uint8_t a[16];
        uint8_t b[16];
        for (uint8_t &v : a) v = static_cast<uint8_t>(rng());
        std::memcpy(b, a, sizeof(b));
        switch (rng() % 4) {
        case 0: b[rng() % 16] ^= static_cast<uint8_t>(1 + rng() % 255); break;
        case 1: for (uint8_t &v : b) v = static_cast<uint8_t>(rng()); break;
        default: break;
        }
        ++ctx.cases;
        const bool x = (orig(a, b) & 0xff) != 0;
        const bool y = gscmisc::GuidEquals(a, b);
        if (x != y) ctx.Fail("case %d: original %d, ours %d", i, x, y);
    }
}

DIFF_TEST(simantics_gsc_scaled_distance, 0x57edb0)
{
    using Fn = float(__thiscall *)(const int32_t *, const int32_t *);
    Fn orig = ctx.Original<Fn>(0x57edb0);
    const float scale = *static_cast<const float *>(ctx.image.At(0xdc87b8));
    std::mt19937 rng(0x57edb0);
    for (int i = 0; i < 40000; ++i) {
        int32_t a[2];
        int32_t b[2];
        for (int32_t &v : a) v = (rng() % 4 == 0) ? int32_t(rng()) : int32_t(rng() % 400001) - 200000;
        for (int32_t &v : b) v = (rng() % 4 == 0) ? int32_t(rng()) : int32_t(rng() % 400001) - 200000;
        if (rng() % 8 == 0) std::memcpy(b, a, sizeof(b));
        ++ctx.cases;
        const float x = orig(a, b);
        const float y = gscmisc::ScaledDistanceSquared(a, b, scale);
        if (std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: (%d,%d) (%d,%d): original %g, ours %g", i, a[0], a[1], b[0], b[1], x, y);
    }
}

DIFF_TEST(simantics_gsc_float_sqrt, 0x5a9eb0)
{
    using Fn = float(__cdecl *)(float);
    Fn orig = ctx.Original<Fn>(0x5a9eb0);
    std::mt19937 rng(0x5a9eb0);
    for (int i = 0; i < 40000; ++i) {
        float v;
        switch (rng() % 5) {
        case 0: { uint32_t bits = rng(); std::memcpy(&v, &bits, 4); break; }
        case 1: v = static_cast<float>(int32_t(rng() % 20001) - 10000) / 16.0f; break;
        case 2: v = static_cast<float>(rng() % 100000); break;
        case 3: v = -static_cast<float>(rng() % 100); break;
        default: v = (rng() % 2) ? 0.0f : -0.0f; break;
        }
        ++ctx.cases;
        const float x = orig(v);
        const float y = gscmisc::FloatSqrt(v);
        const bool bothNan = x != x && y != y;
        // The sign of a NaN result is not reproduced (every caller only compares the result).
        if (!bothNan && std::memcmp(&x, &y, sizeof(x)) != 0) ctx.Fail("case %d: sqrt(%g): original %g, ours %g", i, v, x, y);
    }
}
