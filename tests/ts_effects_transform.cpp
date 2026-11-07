// fx::EffectsTransform (src/ts/EffectsTransform.h): ToLocal 0x3a34b0 (lane WO), Apply 0xbc1010 and Reset 0xbbc9b0 (lane WR):
// original vs ours, on the same random transforms and points (NaNs included), compared bit for bit.
#include "ts_effects_collection.h"

using namespace fxtest;

DIFF_TEST(EffectsTransform_ToLocal, 0x3a34b0)
{
    using Fn = void(__thiscall *)(const fx::EffectsTransform *, fx::Vector3 *);
    const Fn original = ctx.Original<Fn>(0x3a34b0);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 r(seed + 11);
        fx::EffectsTransform t;
        RandomTransform(t, r);
        t.flags |= uint8_t(r() & ~uint32_t(fx::EffectsTransform::kRotated));
        const fx::Vector3 start = {testhelp::FewOrUniform(r, -8.0f, 8.0f), testhelp::FewOrUniform(r, -8.0f, 8.0f),
                                   testhelp::FewOrUniform(r, -8.0f, 8.0f)};
        fx::Vector3 a = start, b = start;
        original(&t, &a);
        t.ToLocal(&b);
        const Blob sa = {Bits(a.x), Bits(a.y), Bits(a.z)}, sb = {Bits(b.x), Bits(b.y), Bits(b.z)};
        Same(ctx, sa, sb, "EffectsTransform_ToLocal", seed);
    }
}

DIFF_TEST(EffectsTransform_Apply, 0xbc1010)
{
    using Fn = void(__thiscall *)(const fx::EffectsTransform *, fx::Vector3 *);
    const Fn original = ctx.Original<Fn>(0xbc1010);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 r(seed + 17);
        fx::EffectsTransform t;
        RandomTransform(t, r);
        t.flags |= uint8_t(r() & ~uint32_t(fx::EffectsTransform::kRotated));
        const fx::Vector3 start = {testhelp::FewOrUniform(r, -8.0f, 8.0f), testhelp::FewOrUniform(r, -8.0f, 8.0f),
                                   testhelp::FewOrUniform(r, -8.0f, 8.0f)};
        fx::Vector3 a = start, b = start;
        original(&t, &a);
        t.Apply(&b);
        const Blob sa = {Bits(a.x), Bits(a.y), Bits(a.z)}, sb = {Bits(b.x), Bits(b.y), Bits(b.z)};
        Same(ctx, sa, sb, "EffectsTransform_Apply", seed);
    }
}

DIFF_TEST(EffectsTransform_Reset, 0xbbc9b0)
{
    using Fn = void(__thiscall *)(fx::EffectsTransform *);
    const Fn original = ctx.Original<Fn>(0xbbc9b0);
    for (uint32_t seed = 0; seed < 64; ++seed) {
        std::mt19937 r(seed + 19);
        // A word either side of the transform: nothing past it is written.
        uint32_t a[sizeof(fx::EffectsTransform) / 4 + 2], b[sizeof(a) / 4];
        for (uint32_t &w : a) w = r();
        std::memcpy(b, a, sizeof(a));
        original(reinterpret_cast<fx::EffectsTransform *>(a + 1));
        reinterpret_cast<fx::EffectsTransform *>(b + 1)->Reset();
        Same(ctx, Blob(std::begin(a), std::end(a)), Blob(std::begin(b), std::end(b)), "EffectsTransform_Reset", seed);
    }
}
