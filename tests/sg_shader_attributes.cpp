// Differential tests of the shader's attribute and apply parts (lane TG, scenegraph M3b; src/sg/SGShaderAttributes.h).
// The game's classes against ours in the standalone shape: each side's objects come from its own factory (the image's,
// or ours), and every slot is called through the object's own vtable (the image's vtable is checked to hold the bodies
// the status entries name). The constructors and destructors are also run alone on a block. The world and the script:
// tests/sg_shader_attr_fx.h.
#include "sg_shader_attr_fx.h"

namespace {

uint32_t OffsetParam(std::mt19937 &rng)
{
    static const uint32_t picks[] = {0, 1, 0xffffffffu, 2, 16, 0x7fffffff, 0x80000000u, 0xffff8000u, 100};
    return rng() % 3 ? picks[rng() % 9] : rng();
}
uint32_t FlagParam(std::mt19937 &rng)
{
    static const uint32_t picks[] = {0, 1, 0, 1, 0xffffff00u, 0x101};
    return picks[rng() % 6];
}
int32_t WaveParam(std::mt19937 &rng)
{
    static const int32_t picks[] = {0, 1, 2, 0, 1, 2, 3, -1};
    return rng() % 8 ? picks[rng() % 8] : static_cast<int32_t>(rng());
}

void SetOffset(void *p, std::mt19937 &rng) { Slot<void>(p, 14, OffsetParam(rng)); }
void SetFlag(void *p, std::mt19937 &rng) { Slot<void>(p, 14, FlagParam(rng)); }
void SetAlphaParts(void *p, std::mt19937 &rng)
{
    if (rng() % 2) {
        const uint32_t color = rng();
        Slot<void>(p, 14, &color);
        return;
    }
    // Half the time a plain animation (rate 1, phase 0, from 0 to 1), so that parts often differ in the wave alone.
    const bool plain = rng() % 2;
    const float rate = plain ? 1.0f : PickFloat(rng), phase = plain ? 0.0f : PickFloat(rng);
    const float from = plain ? 0.0f : PickFloat(rng), to = plain ? 1.0f : PickFloat(rng);
    Slot<void>(p, 15, rate, phase, from, to, WaveParam(rng));
}
void SetMaterialParts(void *p, std::mt19937 &rng)
{
    const unsigned slot = 14 + rng() % 10;
    const cIndexedFloat3 color{PickIndex(rng), {PickFloat(rng), PickFloat(rng), PickFloat(rng)}};
    const cIndexedFloat value{PickIndex(rng), PickFloat(rng)};
    if (slot == 23)
        Slot<void>(p, slot, PickIndex(rng));
    else if (slot <= 20 && slot % 2 == 0)
        Slot<void>(p, slot, &color, &value);
    else
        Slot<void>(p, slot, &value);
}
void SetTiledParts(void *p, std::mt19937 &rng)
{
    static const int32_t targets[] = {0, 1, 2, 0, 1, 2, 3, -1};
    static const int32_t counts[] = {1, 2, 4, 4, 3, 8, 16, -1, -3, 5};
    switch (rng() % 3) {
    case 0: Slot<void>(p, 14, targets[rng() % 8], static_cast<uint32_t>(rng() % 8)); break;
    case 1: Slot<void>(p, 15, counts[rng() % 10], counts[rng() % 10]); break;
    default: Slot<void>(p, 16, PickFloat(rng)); break;
    }
}
void SetColorScalarParts(void *p, std::mt19937 &rng)
{
    const cIndexedFloat3 color{PickIndex(rng), {PickFloat(rng), PickFloat(rng), PickFloat(rng)}};
    const cIndexedFloat value{PickIndex(rng), PickFloat(rng)};
    switch (rng() % 4) {
    case 0: Slot<void>(p, 14, &color, &value); break;
    case 1: Slot<void>(p, 15, &value); break;
    case 2: Slot<void>(p, 16, PickIndex(rng)); break;
    default: Slot<void>(p, 17, static_cast<uint32_t>(rng() % 16), PickIndex(rng)); break;
    }
}
void SetTextureMatrixParts(void *p, std::mt19937 &rng)
{
    static const int32_t targets[] = {0, 1, 2, 0, 1, 2, 3, -1};
    const float a[2] = {PickFloat(rng), PickFloat(rng)}, b[2] = {PickFloat(rng), PickFloat(rng)};
    // Half the time a plain animation (rate 1, phase 0), so that parts often differ in one field.
    const bool plain = rng() % 2;
    const float rate = plain ? 1.0f : PickFloat(rng), phase = plain ? 0.0f : PickFloat(rng);
    switch (rng() % 5) {
    case 0: Slot<void>(p, 14, targets[rng() % 8], static_cast<uint32_t>(rng() % 8)); break;
    case 1: Slot<void>(p, 15, rate, phase, a[0], b[0], WaveParam(rng)); break;
    case 2: Slot<void>(p, 16, rate, phase, a, b, WaveParam(rng)); break;
    case 3: Slot<void>(p, 17, rate, phase, a, b, WaveParam(rng)); break;
    default: Slot<void>(p, 18, a); break;
    }
}
void SetScaleParts(void *p, std::mt19937 &rng)
{
    const cIndexedFloat scale{PickIndex(rng), PickFloat(rng)};
    Slot<void>(p, 14, &scale);
}

const std::vector<Leaf> &Leaves()
{
    static const std::vector<Leaf> leaves = {
        {"cFFDepthOffset", 0xe77a3c, 0xac32f0, 0xabde10, 0xabe310, 15,
         {{10, 0x18c4d0}, {11, 0xac1550}, {12, 0x1b9a50}, {13, 0xac6800}, {14, 0x1eeb80}, {15, 0xabea60},
          {16, 0xac3610}, {17, 0x15e840}},
         reinterpret_cast<Factory>(&cFFDepthOffset::Create), sizeof(cFFDepthOffset), &SetOffset, 0xffffffffu,
         [](void *p) { new (p) cFFDepthOffset; }, [](void *p) { static_cast<cFFDepthOffset *>(p)->~cFFDepthOffset(); }},
        {"cFFApplyTransforms", 0xe77130, 0xac32d0, 0xabddf0, 0xabe300, 15,
         {{10, 0x15e510}, {11, 0xac14c0}, {12, 0x1b9a50}, {13, 0x300a0}, {14, 0x1520c0}, {15, 0xabea40},
          {16, 0xac3600}, {17, 0xac54a0}},
         reinterpret_cast<Factory>(&cFFApplyTransforms::Create), sizeof(cFFApplyTransforms), &SetFlag, 0xffu,
         [](void *p) { new (p) cFFApplyTransforms; },
         [](void *p) { static_cast<cFFApplyTransforms *>(p)->~cFFApplyTransforms(); }},
        {"cAnimatedTiledTexture", 0xe7787c, 0xac3100, 0xabd710, 0xabe1e0, 17,
         {{10, 0x15e510}, {11, 0xabecb0}, {12, 0xac1680}, {13, 0x300a0}, {14, 0xac6300}, {15, 0xac6690},
          {16, 0xac62b0}, {17, 0xabe8b0}, {18, 0xac34f0}, {19, 0xac5200}},
         reinterpret_cast<Factory>(&cAnimatedTiledTexture::Create), sizeof(cAnimatedTiledTexture), &SetTiledParts,
         0xffffffffu, [](void *p) { new (p) cAnimatedTiledTexture; },
         [](void *p) { static_cast<cAnimatedTiledTexture *>(p)->~cAnimatedTiledTexture(); }},
        {"cAnimColorScalarAlpha", 0xe77680, 0xac30c0, 0xabd600, 0xabe1c0, 16,
         {{10, 0x15e510}, {11, 0xabeba0}, {12, 0x1b9a50}, {13, 0x300a0}, {14, 0xac61e0}, {15, 0xac61a0},
          {16, 0xabe870}, {17, 0xac34b0}, {18, 0xac51a0}},
         reinterpret_cast<Factory>(&cAnimColorScalarAlpha::Create), sizeof(cAnimColorScalarAlpha), &SetAlphaParts,
         0xffffffffu, [](void *p) { new (p) cAnimColorScalarAlpha; },
         [](void *p) { static_cast<cAnimColorScalarAlpha *>(p)->~cAnimColorScalarAlpha(); }},
        {"cAttributeFFMaterial", 0xe77430, 0xac3260, 0xabd9d0, 0xabe2d0, 24,
         {{9, 0xac1f30}, {10, 0xac2b60}, {11, 0xabf9e0}, {12, 0x1b9a50}, {13, 0x300a0}, {14, 0xac6180},
          {15, 0xac6160}, {16, 0xac6230}, {17, 0xac6250}, {18, 0xac6610}, {19, 0xac6630}, {20, 0xac6270},
          {21, 0xac6290}, {22, 0xac6650}, {23, 0x4daa20}, {24, 0xabe9d0}, {25, 0xac35a0}, {26, 0xac5440}},
         reinterpret_cast<Factory>(&cAttributeFFMaterial::Create), sizeof(cAttributeFFMaterial), &SetMaterialParts,
         0xffffffffu, [](void *p) { new (p) cAttributeFFMaterial; },
         [](void *p) { static_cast<cAttributeFFMaterial *>(p)->~cAttributeFFMaterial(); }},
        {"cAttributeColorScalar", 0xe77224, 0xac3240, 0xabd970, 0xabe2c0, 18,
         {{9, 0xac1df0}, {10, 0xac2b30}, {11, 0xabf7b0}, {12, 0x1b9a50}, {13, 0x300a0}, {14, 0xac6180},
          {15, 0xac6160}, {16, 0x1f9ac0}, {17, 0xac6670}, {18, 0xabe9b0}, {19, 0xac3580}, {20, 0xac5410}},
         reinterpret_cast<Factory>(&cAttributeColorScalar::Create), sizeof(cAttributeColorScalar), &SetColorScalarParts,
         0xffffffffu, [](void *p) { new (p) cAttributeColorScalar; },
         [](void *p) { static_cast<cAttributeColorScalar *>(p)->~cAttributeColorScalar(); }},
        {"cAnimatedTextureMatrix", 0xe77780, 0xac30e0, 0xabd650, 0xabe1d0, 19,
         {{9, 0x1b9a50}, {10, 0x15e510}, {11, 0xabec90}, {12, 0xac1660}, {13, 0x300a0}, {14, 0xac6300},
          {15, 0xac64f0}, {16, 0xac66b0}, {17, 0xac6530}, {18, 0xac63e0}, {19, 0xabe890}, {20, 0xac34d0},
          {21, 0xac51d0}},
         reinterpret_cast<Factory>(&cAnimatedTextureMatrix::Create), sizeof(cAnimatedTextureMatrix),
         &SetTextureMatrixParts, 0xffffffffu, [](void *p) { new (p) cAnimatedTextureMatrix; },
         [](void *p) { static_cast<cAnimatedTextureMatrix *>(p)->~cAnimatedTextureMatrix(); }},
        {"cAttributeFFScaleModelToViewTransform", 0xe7730c, 0xac3280, 0xabdbd0, 0xabe2e0, 15,
         {{10, 0x15e510}, {11, 0xabfc40}, {12, 0x1b9a50}, {13, 0x300a0}, {14, 0xac6590}, {15, 0xabe9f0},
          {16, 0xac35c0}, {17, 0x3a5930}},
         reinterpret_cast<Factory>(&cAttributeFFScaleModelToViewTransform::Create),
         sizeof(cAttributeFFScaleModelToViewTransform), &SetScaleParts, 0xffffffffu,
         [](void *p) { new (p) cAttributeFFScaleModelToViewTransform; },
         [](void *p) {
             static_cast<cAttributeFFScaleModelToViewTransform *>(p)->~cAttributeFFScaleModelToViewTransform();
         }},
    };
    return leaves;
}

const Leaf &LeafNamed(const char *name)
{
    for (const Leaf &l : Leaves())
        if (std::strcmp(l.name, name) == 0) return l;
    return Leaves()[0];
}
} // namespace

// cFFDepthOffset: the factory 0xac32f0 and every slot (Apply 0xac1550, EndState 0xac6800, the deleting destructor
// 0xabea60, HashParams 0xac3610, the folded bodies), against cFFApplyTransforms parts for IsEquivalent.
DIFF_TEST(sg_ff_depth_offset, 0xac32f0) { Behaviour(ctx, LeafNamed("cFFDepthOffset"), LeafNamed("cFFApplyTransforms")); }

// cFFApplyTransforms: the factory 0xac32d0 and every slot (Apply 0xac14c0, the deleting destructor 0xabea40,
// HashParams 0xac3600, EqualParams 0xac54a0, the folded bodies), against cFFDepthOffset parts.
DIFF_TEST(sg_ff_apply_transforms, 0xac32d0) { Behaviour(ctx, LeafNamed("cFFApplyTransforms"), LeafNamed("cFFDepthOffset")); }

// The constructors and destructors alone on a block of pattern words: what they write besides the vtable pointer (the
// image's or ours); the constructors return the object.
// cAnimColorScalarAlpha: the factory 0xac30c0 and every slot (Apply 0xabeba0, SetColor 0xac61e0, SetAnimation
// 0xac61a0, the deleting destructor 0xabe870, HashParams 0xac34b0, EqualParams 0xac51a0, the folded bodies), against
// cAttributeFFScaleModelToViewTransform parts.
DIFF_TEST(sg_anim_color_scalar_alpha, 0xac30c0)
{
    Behaviour(ctx, LeafNamed("cAnimColorScalarAlpha"), LeafNamed("cAttributeFFScaleModelToViewTransform"));
}

// cAttributeFFScaleModelToViewTransform: the factory 0xac3280 and every slot (Apply 0xabfc40, SetScale 0xac6590, the
// deleting destructor 0xabe9f0, HashParams 0xac35c0, the folded bodies), against cAnimColorScalarAlpha parts.
DIFF_TEST(sg_ff_scale_model_to_view, 0xac3280)
{
    Behaviour(ctx, LeafNamed("cAttributeFFScaleModelToViewTransform"), LeafNamed("cAnimColorScalarAlpha"));
}

// cAttributeFFMaterial: the factory 0xac3260 and every slot (9 0xac1f30, 10 0xac2b60, Apply 0xabf9e0, the setters
// 0xac6180-0xac6650, the deleting destructor 0xabe9d0, HashParams 0xac35a0, EqualParams 0xac5440, IsDynamic 0xac4b80
// through slots 9 and 10, the folded bodies), against cAnimColorScalarAlpha parts.
DIFF_TEST(sg_ff_material, 0xac3260)
{
    Behaviour(ctx, LeafNamed("cAttributeFFMaterial"), LeafNamed("cAnimColorScalarAlpha"));
}

// cAnimatedTiledTexture: the factory 0xac3100 and every slot (Apply 0xabecb0 and 12 0xac1680 through ApplyFrame
// 0xac2860, the setters 0xac6300 0xac6690 0xac62b0, the deleting destructor 0xabe8b0, HashParams 0xac34f0,
// EqualParams 0xac5200, the folded bodies), against cAnimColorScalarAlpha parts.
DIFF_TEST(sg_animated_tiled_texture, 0xac3100)
{
    Behaviour(ctx, LeafNamed("cAnimatedTiledTexture"), LeafNamed("cAnimColorScalarAlpha"));
}

// cAttributeColorScalar: the factory 0xac3240 and every slot (9 0xac1df0, 10 0xac2b30, Apply 0xabf7b0, the setters
// 0xac6180 0xac6160 0x1f9ac0 0xac6670, the deleting destructor 0xabe9b0, HashParams 0xac3580, EqualParams 0xac5410, the
// folded bodies), with the colour event source and the item's modulators faked, against cAnimColorScalarAlpha parts.
DIFF_TEST(sg_attribute_color_scalar, 0xac3240)
{
    Behaviour(ctx, LeafNamed("cAttributeColorScalar"), LeafNamed("cAnimColorScalarAlpha"));
}

// AnimationWave 0xac6820 alone: each kind (and others) over chosen and random rates, phases and times, the results
// compared bit for bit.
// cAnimatedTextureMatrix: the factory 0xac30e0 and every slot (the constructor 0xabd650, destructor 0xabe1d0, Apply
// 0xabec90, Slot12 0xac1660, the setters 0xac64f0 / 0xac66b0 / 0xac6530 / 0xac63e0, deleting destructor 0xabe890,
// HashParams 0xac34d0, EqualParams 0xac51d0, the folded bodies), against cAnimColorScalarAlpha parts.
DIFF_TEST(sg_animated_texture_matrix, 0xac30e0)
{
    Behaviour(ctx, LeafNamed("cAnimatedTextureMatrix"), LeafNamed("cAnimColorScalarAlpha"));
}

DIFF_TEST(sg_animation_wave, 0xac6820)
{
    using Wave = float(__cdecl *)(int32_t, float, float, float);
    const Wave original = ctx.Original<Wave>(0xac6820);
    std::mt19937 rng(77);
    for (int n = 0; n < 20000; ++n) {
        const int32_t wave = n < 16000 ? n % 4 : WaveParam(rng);
        float rate, phase, time;
        if (n % 3 == 0) {
            rate = static_cast<float>(static_cast<int32_t>(rng() % 9) - 4) * 0.25f;
            phase = static_cast<float>(static_cast<int32_t>(rng() % 17) - 8) * 0.125f;
            time = static_cast<float>(static_cast<int32_t>(rng() % 33) - 16) * 0.5f;
        } else {
            rate = PickFloat(rng);
            phase = PickFloat(rng);
            time = PickFloat(rng);
        }
        const float a = original(wave, rate, phase, time), b = AnimationWave(wave, rate, phase, time);
        ++ctx.cases;
        if (Bits(a) != Bits(b)) {
            ctx.Fail("wave %d rate %08x phase %08x time %08x: original %08x, ours %08x", wave, Bits(rate), Bits(phase),
                     Bits(time), Bits(a), Bits(b));
            return;
        }
    }
}

DIFF_TEST(sg_shader_attribute_construct_destruct, 0xabde10)
{
    gImageBase = ctx.image.Base();
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    for (const Leaf &l : Leaves())
        CheckSides(ctx, l.name, 16, [&](bool original, std::mt19937 &rng) {
            alignas(8) uint32_t block[40];
            for (uint32_t &w : block) w = rng();
            *reinterpret_cast<uint32_t *>(gImageBase + 0x10f67a0) = rng();
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
