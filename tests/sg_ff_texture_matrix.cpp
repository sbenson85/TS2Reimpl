// Differential tests of cAttributeFFTextureMatrix (lane TK, scenegraph M3b part 3; src/sg/cAttributeFFTextureMatrix.h),
// in the world and with the behaviour script of tests/sg_shader_attr_fx.h, which this file extends: the context's frame
// (+0xdc, 13 words) and the item's transforms are drawn as chosen floats (a turn, a scaled turn, a singular matrix, or
// PickFloat words), and the item's values hold, besides PickFloat words, scales a few units in the last place from 1 so
// that M lands on both sides of 0xab79e0's 31-unit threshold.
#include "sg_shader_attr_fx.h"

#include "sg/cAttributeFFTextureMatrix.h"

#include <cmath>

namespace {

// A float n units in the last place from f.
float Ulps(float f, int32_t n)
{
    uint32_t u = Bits(f) + static_cast<uint32_t>(n);
    float r;
    std::memcpy(&r, &u, 4);
    return r;
}

// 1 or a few units in the last place from it (both sides of 31 and 32), or PickFloat.
float PickScale(std::mt19937 &rng)
{
    static const int32_t ulps[] = {0, 1, -1, 30, 31, 32, 33, -31, -32, -33, 64};
    if (rng() % 2) return Ulps(1.0f, ulps[rng() % 11]);
    return PickFloat(rng);
}

// An angle in turns: none, a tiny one, a quarter, or PickFloat.
float PickAngle(std::mt19937 &rng)
{
    static const float picks[] = {0.0f, 1e-9f, -1e-7f, 0.25f, 0.5f, -0.125f, 1.0f, 3.0f};
    if (rng() % 2) return picks[rng() % 8];
    return PickFloat(rng);
}

// Mostly unindexed, so that the matrix source's Slot9 and the cache are reached.
int32_t PickParamIndex(std::mt19937 &rng)
{
    return rng() % 3 ? -1 : PickIndex(rng);
}

// A 3x3 (then three more words when `offset`): a turn, a scaled turn, a singular one, or PickFloat words.
void DrawFrame(uint32_t *w, std::mt19937 &rng, bool offset)
{
    const unsigned kind = rng() % 4;
    const float a = PickFloat(rng) * 0.01f, k = PickScale(rng);
    const float c = std::cos(a), s = std::sin(a);
    const float turn[9] = {c * k, -s * k, 0.0f, s * k, c * k, 0.0f, 0.0f, 0.0f, k};
    for (int i = 0; i < 9; ++i) {
        float f;
        switch (kind) {
        case 0: f = turn[i]; break;
        case 1: f = i < 3 ? turn[i] : (i < 6 ? turn[i - 3] : PickFloat(rng)); break; // rows 0 and 1 equal
        case 2: f = i % 4 == 0 ? 1.0f : 0.0f; break;
        default: f = PickFloat(rng); break;
        }
        w[i] = Bits(f);
    }
    if (offset)
        for (int i = 9; i < 12; ++i) w[i] = Bits(rng() % 5 ? PickFloat(rng) : 0.0f);
}

void TextureMatrixWorld(std::mt19937 &rng)
{
    // The frame: a word, the 3x3, the offset.
    uint32_t frame[13];
    frame[0] = rng();
    DrawFrame(frame + 1, rng, true);
    std::memcpy(gContext + 0xdc, frame, sizeof frame);
    // The transforms: a word, the 3x3, the offset (source 3's direction: sometimes zero, sometimes along the up axis).
    for (auto &t : gTransforms) {
        if (rng() % 4 == 0) continue; // the base world's words
        DrawFrame(t + 1, rng, true);
        if (rng() % 6 == 0) t[10] = t[11] = Bits(0.0f);
        if (rng() % 8 == 0) t[12] = Bits(0.0f);
    }
    for (int i = 0; i < 72; ++i)
        if (rng() % 3 == 0) gValues[i] = Bits(PickScale(rng));
}

// A bool word: the low byte 0 or 1, the upper bytes set or not.
uint32_t FlagWord(std::mt19937 &rng)
{
    static const uint32_t picks[] = {0, 1, 0, 1, 0xffffff00u, 0x101};
    return picks[rng() % 6];
}

// One of the class's setters (slots 14 to 23) with drawn values.
void SetParts(void *p, std::mt19937 &rng)
{
    static const uint32_t stages[] = {0, 1, 2, 3, 7, 0xffffffffu};
    const cIndexedFloat angle{PickParamIndex(rng), PickAngle(rng)};
    const cIndexedFloat scale{PickParamIndex(rng), PickScale(rng)};
    const cIndexedFloat3 axis{PickParamIndex(rng), {PickFloat(rng), PickFloat(rng), PickFloat(rng)}};
    const cIndexedFloat2 uv{PickParamIndex(rng), {PickScale(rng), PickScale(rng)}};
    const cIndexedFloat2 offset{PickParamIndex(rng), {PickFloat(rng), PickFloat(rng)}};
    switch (rng() % 12) {
    case 0: Slot<void>(p, 14, stages[rng() % 6]); break;
    case 1:
    case 2: {
        // Sources 0 to 5, those above, and the upper half word's bits.
        const uint32_t source = rng() % 8 ? rng() % 8 : rng();
        Slot<void>(p, 15, source);
        break;
    }
    case 3: {
        // The swizzle's nibbles stay 0 to 3 (the original reads its stack for others).
        const uint32_t swizzle = rng() % 3 ? 0x123u : (rng() & 0x3333u);
        Slot<void>(p, 16, swizzle | (rng() % 4 ? 0u : rng() << 16));
        break;
    }
    case 4: Slot<void>(p, 17, &angle); break;
    case 5: Slot<void>(p, 18, &angle, &axis); break;
    case 6: Slot<void>(p, 19, &scale); break;
    case 7: Slot<void>(p, 20, &uv); break;
    case 8: Slot<void>(p, 21, &offset); break;
    case 9: Slot<void>(p, 22, &offset); break;
    default: Slot<void>(p, 23, FlagWord(rng)); break;
    }
}

const Leaf &TextureMatrixLeaf()
{
    static const Leaf leaf = {
        "cAttributeFFTextureMatrix", 0xe77578, 0xac32a0, 0xabdc00, 0xabe2f0, 24,
        {{9, 0xac20b0}, {10, 0xac2b70}, {11, 0xabfd60}, {12, 0xac1a50}, {13, 0x300a0}, {14, 0x1eeb80}, {15, 0xac6600},
         {16, 0xac65f0}, {17, 0xac64d0}, {18, 0xac64a0}, {19, 0xac65b0}, {20, 0xac65d0}, {21, 0xac6700}, {22, 0xac6400},
         {23, 0xac62c0}, {24, 0xabea10}, {25, 0xac35e0}, {26, 0xac5470}},
        reinterpret_cast<Factory>(&cAttributeFFTextureMatrix::Create), sizeof(cAttributeFFTextureMatrix), &SetParts,
        0xffffffffu, [](void *p) { new (p) cAttributeFFTextureMatrix; },
        [](void *p) { static_cast<cAttributeFFTextureMatrix *>(p)->~cAttributeFFTextureMatrix(); },
        0xffu}; // +0x85..+0x87 padding neither constructor writes (the heap's bytes)
    return leaf;
}

// The other part: a cFFDepthOffset (its one setter an offset word).
const Leaf &OtherLeaf()
{
    static const Leaf leaf = {
        "cFFDepthOffset", 0xe77a3c, 0xac32f0, 0xabde10, 0xabe310, 15,
        {{10, 0x18c4d0}, {11, 0xac1550}, {12, 0x1b9a50}, {13, 0xac6800}, {14, 0x1eeb80}, {15, 0xabea60},
         {16, 0xac3610}, {17, 0x15e840}},
        reinterpret_cast<Factory>(&cFFDepthOffset::Create), sizeof(cFFDepthOffset),
        [](void *p, std::mt19937 &rng) { Slot<void>(p, 14, FlagWord(rng) ^ rng() % 3); }, 0xffffffffu,
        [](void *p) { new (p) cFFDepthOffset; }, [](void *p) { static_cast<cFFDepthOffset *>(p)->~cFFDepthOffset(); }};
    return leaf;
}

struct WorldExtra {
    WorldExtra() { gWorldExtra = &TextureMatrixWorld; }
    ~WorldExtra() { gWorldExtra = nullptr; }
};

} // namespace

// The factory 0xac32a0 and every slot through the behaviour script (9 0xac20b0, 10 0xac2b70, Apply 0xabfd60, 12
// 0xac1a50, the setters 0xac6600 0xac65f0 0xac64d0 0xac64a0 0xac65b0 0xac65d0 0xac6700 0xac6400 0xac62c0, the deleting
// destructor 0xabea10, HashParams 0xac35e0, EqualParams 0xac5470, the folded bodies, and through them IsDynamic 0xac4bd0,
// BuildMatrix 0xac2bc0, TextureMatrixDiffers 0xab79e0 and SwizzleTextureRows 0xac6090), against cFFDepthOffset parts.
DIFF_TEST(sg_ff_texture_matrix, 0xac32a0)
{
    WorldExtra extra;
    Behaviour(ctx, TextureMatrixLeaf(), OtherLeaf());
}

// Apply 0xabfd60, Slot9 0xac20b0, Slot10 0xac2b70 and Slot12 0xac1a50 on fresh parts: up to six setter calls, the
// cache filled (Slot10) or not, then each of the four, the commands and the part's words noted.
DIFF_TEST(sg_ff_texture_matrix_apply, 0xabfd60)
{
    WorldExtra extra;
    gImageBase = ctx.image.Base();
    const Leaf &l = TextureMatrixLeaf();
    CheckSides(ctx, "apply", 1500, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        Slot<void>(p, 15, static_cast<uint32_t>(rng() % 7));
        const unsigned calls = rng() % 7;
        for (unsigned n = 0; n < calls; ++n) SetParts(p, rng);
        if (rng() % 2) Note({0x60, Slot<uint32_t>(p, 10)});
        Slot<void>(p, 11, static_cast<void *>(gContext), static_cast<void *>(gItem));
        Note({0x61});
        Slot<void>(p, 9, static_cast<void *>(&gState), static_cast<void *>(gContext));
        Note({0x62});
        Slot<void>(p, 12, static_cast<void *>(&gState), static_cast<void *>(gItem));
        Note({0x63});
        NoteFields(p, l, 0x41);
        Slot<uint32_t>(p, 2);
    });
}

// BuildMatrix 0xac2bc0 and IsDynamic 0xac4bd0 called directly on each side's part (the image's on its own), with and
// without the item's values; the 9 floats compared bit for bit.
DIFF_TEST(sg_ff_texture_matrix_build, 0xac2bc0)
{
    WorldExtra extra;
    gImageBase = ctx.image.Base();
    const Leaf &l = TextureMatrixLeaf();
    using Build = void(__thiscall *)(void *, float *, const void *);
    using Dynamic = bool(__thiscall *)(void *);
    CheckSides(ctx, "build", 3000, [&](bool original, std::mt19937 &rng) {
        const Factory make = original ? ctx.Original<Factory>(l.factory) : l.ours;
        void *const p = make();
        Slot<uint32_t>(p, 1);
        const unsigned calls = 1 + rng() % 6;
        for (unsigned n = 0; n < calls; ++n) SetParts(p, rng);
        for (const void *values : {static_cast<const void *>(gValues), static_cast<const void *>(nullptr)}) {
            float out[9];
            for (float &f : out) f = -7.0f;
            bool dynamic;
            if (original) {
                ctx.Original<Build>(0xac2bc0)(p, out, values);
                dynamic = ctx.Original<Dynamic>(0xac4bd0)(p);
            } else {
                auto *const o = static_cast<cAttributeFFTextureMatrix *>(p);
                o->BuildMatrix(out, static_cast<const uint8_t *>(values));
                dynamic = o->IsDynamic();
            }
            std::vector<uint32_t> w{0x80, uint32_t(dynamic)};
            for (float f : out) w.push_back(Bits(f));
            gLog.insert(gLog.end(), w.begin(), w.end());
            gLog.push_back(0xeeeeeeee);
        }
        Slot<uint32_t>(p, 2);
    });
}

// TextureMatrixDiffers 0xab79e0 alone: pairs of matrices whose elements are equal, a few units in the last place apart
// (both sides of 31 and 32, across zero and the signs), NaNs, infinities and random words.
DIFF_TEST(sg_ff_texture_matrix_differs, 0xab79e0)
{
    using Differs = bool(__cdecl *)(const float *, const float *);
    const Differs original = ctx.Original<Differs>(0xab79e0);
    std::mt19937 rng(91);
    static const int32_t ulps[] = {0, 1, -1, 31, -31, 32, -32, 33, -33, 0x7fffffff, int32_t(0x80000000u)};
    static const uint32_t words[] = {0, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7f800000u, 0xff800000u,
                                     0x7fc00000u, 0xffc00000u, 1, 0x80000001u, 0x0000001fu, 0x80000010u};
    for (int n = 0; n < 40000; ++n) {
        uint32_t a[9], b[9];
        for (int i = 0; i < 9; ++i) {
            a[i] = rng() % 3 ? words[rng() % 12] : (rng() % 2 ? Bits(PickFloat(rng)) : rng());
            b[i] = rng() % 4 ? a[i] : (rng() % 3 ? a[i] + static_cast<uint32_t>(ulps[rng() % 11])
                                                 : (rng() % 2 ? words[rng() % 12] : rng()));
        }
        const float *const fa = reinterpret_cast<const float *>(a);
        const float *const fb = reinterpret_cast<const float *>(b);
        const bool x = original(fa, fb), y = TextureMatrixDiffers(fa, fb);
        ++ctx.cases;
        if (x != y) {
            ctx.Fail("case %d: original %d, ours %d", n, int(x), int(y));
            return;
        }
    }
}

// SwizzleTextureRows 0xac6090 alone: random rows, every swizzle whose nibbles are 0 to 3 (the image reads its stack for
// others), the 16 words compared.
DIFF_TEST(sg_ff_texture_matrix_swizzle, 0xac6090)
{
    using Swizzle = void(__cdecl *)(float *, uint32_t);
    const Swizzle original = ctx.Original<Swizzle>(0xac6090);
    std::mt19937 rng(92);
    for (uint32_t s = 0; s < 0x10000; ++s) {
        if (s & 0xccccu) continue;
        uint32_t rows[2][16];
        for (uint32_t &w : rows[0]) w = rng();
        std::memcpy(rows[1], rows[0], sizeof rows[0]);
        original(reinterpret_cast<float *>(rows[0]), s | (rng() << 16));
        SwizzleTextureRows(reinterpret_cast<float *>(rows[1]), static_cast<uint16_t>(s));
        ++ctx.cases;
        if (std::memcmp(rows[0], rows[1], sizeof rows[0]) != 0) {
            ctx.Fail("swizzle %#x: rows differ", s);
            return;
        }
    }
}

// The constructor 0xabdc00 and destructor 0xabe2f0 alone on a block of pattern words.
DIFF_TEST(sg_ff_texture_matrix_construct_destruct, 0xabdc00)
{
    gImageBase = ctx.image.Base();
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    const Leaf &l = TextureMatrixLeaf();
    CheckSides(ctx, l.name, 16, [&](bool original, std::mt19937 &rng) {
        alignas(8) uint32_t block[40];
        for (uint32_t &w : block) w = rng();
        if (original) {
            void *const r = ctx.Original<Ctor>(l.ctor)(block);
            Note({0x50, uint32_t(r == block), uint32_t(block[0] == uint32_t(ctx.image.Base() + l.vtable))});
        } else {
            l.placement(block);
            Note({0x50, 1, 1});
        }
        gLog.insert(gLog.end(), block + 1, block + 40);
        for (size_t i = 1; i < l.size / 4; ++i) block[i] = rng();
        if (original)
            ctx.Original<Dtor>(l.dtor)(block);
        else
            l.destroy(block);
        gLog.insert(gLog.end(), block + 1, block + 40);
    });
}
