// The shader program constants' sources (scenegraph M3a): see ShaderConstantSources.h.
#include "sg/ShaderConstantSources.h"

#include "compgeom/ConvexMath.h" // MulS, AddS, SubS, DivS: the original operand order for NaN pairs
#include "compgeom/Frames.h"
#include "compgeom/Matrix4.h"
#include "compgeom/Rigid.h"      // QuaternionToMatrix
#include "compgeom/SeparatingAxes.h" // FramePlacement
#include "game/game.h"
#include "game/original.h"
#include "sg/DetailObject.h"
#include "sg/SGLightInterfaces.h" // cIStandardLight

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <emmintrin.h>

namespace nGZSceneGraph {

namespace {

using nGZCompGeom::FramePlacement;
using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::SubS;
using Binding = cApplyShaderProgramConstants::Binding;

// Ten unused slots of an interface (only the numbers matter).
#define TS2_UNUSED_SLOTS(n)                                                                                            \
    virtual void Slot##n##0() = 0;                                                                                     \
    virtual void Slot##n##1() = 0;                                                                                     \
    virtual void Slot##n##2() = 0;                                                                                     \
    virtual void Slot##n##3() = 0;                                                                                     \
    virtual void Slot##n##4() = 0;                                                                                     \
    virtual void Slot##n##5() = 0;                                                                                     \
    virtual void Slot##n##6() = 0;                                                                                     \
    virtual void Slot##n##7() = 0;                                                                                     \
    virtual void Slot##n##8() = 0;                                                                                     \
    virtual void Slot##n##9() = 0;

// What the item's +0xc answers (slot 5): the bone set of a number, whose +0x14 is its bones' transform indices.
struct BoneSet {
    uint8_t mBefore[0x14];
    const int32_t *mIndices; // +0x14: each bone's index into the item's transforms
};
class cIBoneSource {
public:
    virtual void Slot0() = 0;
    virtual void Slot1() = 0;
    virtual void Slot2() = 0;
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual const BoneSet *GetBoneSet(uint16_t set) = 0; // 5 (ret 4)
};

// What answers slot 54 (+0xd8, ret 4): the item's event source and a light holder's node. It returns its placement (a
// rigid transform: 0x19c1c0 makes its 4x4, 0x19c800 turns a vector by its rotation); `scratch` is room for one.
class cIPlacementSource {
public:
    TS2_UNUSED_SLOTS()
    TS2_UNUSED_SLOTS(1)
    TS2_UNUSED_SLOTS(2)
    TS2_UNUSED_SLOTS(3)
    TS2_UNUSED_SLOTS(4)
    virtual void Slot50() = 0;
    virtual void Slot51() = 0;
    virtual void Slot52() = 0;
    virtual void Slot53() = 0;
    virtual const void *GetPlacement(void *scratch) = 0; // 54
};

// The item's event source (+0x2c): slot 10 fills a colour event (as cIShaderEventSource's), slot 54 its placement.
class cIItemEventSource {
public:
    TS2_UNUSED_SLOTS()
    virtual void GetEvent(void *event, uint32_t type, int32_t index) = 0; // 10 (ret 0xc)
    virtual void Slot11() = 0;
    virtual void Slot12() = 0;
    virtual void Slot13() = 0;
    virtual void Slot14() = 0;
    virtual void Slot15() = 0;
    virtual void Slot16() = 0;
    virtual void Slot17() = 0;
    virtual void Slot18() = 0;
    virtual void Slot19() = 0;
    TS2_UNUSED_SLOTS(2)
    TS2_UNUSED_SLOTS(3)
    TS2_UNUSED_SLOTS(4)
    virtual void Slot50() = 0;
    virtual void Slot51() = 0;
    virtual void Slot52() = 0;
    virtual void Slot53() = 0;
    virtual const void *GetPlacement(void *scratch) = 0; // 54
};

// The detail object (GetDetailObject): slot 45 a 4x4 matrix, 46 and 47 a vector (tentative names: the texture
// projection, and two of its vectors).
class cIDetailSource {
public:
    TS2_UNUSED_SLOTS()
    TS2_UNUSED_SLOTS(1)
    TS2_UNUSED_SLOTS(2)
    TS2_UNUSED_SLOTS(3)
    virtual void Slot40() = 0;
    virtual void Slot41() = 0;
    virtual void Slot42() = 0;
    virtual void Slot43() = 0;
    virtual void Slot44() = 0;
    virtual const float *GetMatrix() = 0;  // 45 (+0xb4)
    virtual const float *GetVectorA() = 0; // 46 (+0xb8)
    virtual const float *GetVectorB() = 0; // 47 (+0xbc)
};

// What sits in the item's holder table from nibble 4 to nibble 5 (source 0x1b): slot 42 (+0xa8) is called with no
// arguments and answers (near, far).
class cIDepthRange {
public:
    TS2_UNUSED_SLOTS()
    TS2_UNUSED_SLOTS(1)
    TS2_UNUSED_SLOTS(2)
    TS2_UNUSED_SLOTS(3)
    virtual void Slot40() = 0;
    virtual void Slot41() = 0;
    virtual const float *GetRange() = 0; // 42
};
#undef TS2_UNUSED_SLOTS

// A light's holder in the item's table (as cApplyFFLightStates reads it): the light, its node, and words 2..18 (words
// 9, 12 and 15 the direction, 16..18 the position).
struct LightHolder {
    cIStandardLight *mLight;
    cIPlacementSource *mNode;
    float mWords[17];
    float Word(uint32_t index) const { return mWords[index - 2]; }
};

// The context's fields the sources read beyond cShaderApplyContext's.
struct SourceContext {
    uint8_t mBefore[4];
    uint32_t mFrameNumber; // +4 (tentative)
    float mTime;           // +8
    uint8_t mBeforeFrame[0xdc - 0xc];
    FramePlacement mFrame;        // +0xdc
    FramePlacement mFrameInverse; // +0x110 (tentative: the frame's inverse)
    float mProjection[16];        // +0x144
    float mView[16];              // +0x184
    float mMatrix1c4[16];         // +0x1c4
    uint8_t mBeforeOuter[0x284 - 0x204];
    const SourceContext *mOuter; // +0x284
};
static_assert(offsetof(SourceContext, mTime) == offsetof(cShaderApplyContext, mTime));
static_assert(offsetof(SourceContext, mFrame) == offsetof(cShaderApplyContext, mFrame));
static_assert(offsetof(SourceContext, mFrameInverse) == 0x110);
static_assert(offsetof(SourceContext, mProjection) == offsetof(cShaderApplyContext, mProjection));
static_assert(offsetof(SourceContext, mView) == 0x184);
static_assert(offsetof(SourceContext, mMatrix1c4) == 0x1c4);
static_assert(offsetof(SourceContext, mOuter) == offsetof(cShaderApplyContext, mOuter));

// The item's fields the sources read beyond cShaderApplyItem's.
struct SourceItem {
    uint8_t mBefore[0xc];
    cIBoneSource *mBones; // +0xc
    uint8_t mBetweenBones[4];
    uint16_t mBoneSet; // +0x14
    uint8_t mBeforeValues[0x20 - 0x16];
    const uint8_t *mValues;            // +0x20
    const float *mScalars;             // +0x24: source 0x11's values
    const uint32_t *mTransforms;       // +0x28: 13-word frames (the item's, then its bones' by index)
    cIItemEventSource *const *mEventSource; // +0x2c
    uint32_t mHolderRuns;              // +0x30: nibbles 0..5, each the start of a run in the holder table
    uint8_t mBetweenRuns[4];
    uint32_t mCounts; // +0x38: bits 15..21 the bones, 22..27 the scalars
    void *const *mHolders; // +0x3c: the holder table (each entry points at an interface pointer)
};
static_assert(offsetof(SourceItem, mValues) == offsetof(cShaderApplyItem, mValues));
static_assert(offsetof(SourceItem, mTransforms) == offsetof(cShaderApplyItem, mTransform));
static_assert(offsetof(SourceItem, mEventSource) == offsetof(cShaderApplyItem, mEventSource));
static_assert(offsetof(SourceItem, mHolderRuns) == offsetof(cShaderApplyItem, mModulatorRange));
static_assert(offsetof(SourceItem, mCounts) == 0x38);
static_assert(offsetof(SourceItem, mHolders) == offsetof(cShaderApplyItem, mModulators));

uint32_t Nibble(uint32_t runs, int which) { return runs >> (4 * which) & 0xf; }
// The image's arithmetic on run lengths is unsigned (a run's end below its start wraps).
uint32_t RunLength(uint32_t runs, int which) { return Nibble(runs, which + 1) - Nibble(runs, which); }

const LightHolder *Light(const SourceItem *item, int run, uint32_t i)
{
    return static_cast<const LightHolder *>(item->mHolders[Nibble(item->mHolderRuns, run) + i]);
}

// Light runs: directional (nibbles 0..1), point (1..2), spot (2..3).
enum : int { kDirectional = 0, kPoint = 1, kSpot = 2 };

// A light's eight parameters (slot 50): colour (0..2), intensity 3, then 4..7 (the spot's cone angles in 6 and 7).
struct LightParams {
    float q[8];
};
LightParams Params(const LightHolder *light, const SourceItem *item)
{
    LightParams p;
    light->mLight->GetParams(p.q, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(item)));
    return p;
}

// cos of half an angle, in double as the image (the float's half widened, the cosine narrowed).
float HalfCos(float angle) { return static_cast<float>(std::cos(static_cast<double>(MulS(angle, 0.5f)))); }
// The spot's two cone cosines: of half the sum (outer: 7 + 6) and of half angle 6.
float OuterCos(const LightParams &p) { return HalfCos(AddS(p.q[7], p.q[6])); }
float InnerCos(const LightParams &p) { return HalfCos(p.q[6]); }
float Squared(float v) { return MulS(v, v); }

// The conversions to a file's word.
template <class T> T Cvt(float v);
template <> float Cvt<float>(float v) { return v; }
// The truncating conversion as the instruction (out of range or NaN: 0x80000000), which a plain cast of a value the
// compiler can see came from an integer might fold away.
template <> int32_t Cvt<int32_t>(float v) { return _mm_cvtt_ss2si(_mm_set_ss(v)); }
// A count: a float of it, or the integer itself.
template <class T> T Count(uint32_t v);
template <> float Count<float>(uint32_t v) { return static_cast<float>(v); }
template <> int32_t Count<int32_t>(uint32_t v) { return static_cast<int32_t>(v); }

template <class T> void Put(T *dst, float a, float b, float c, float d)
{
    dst[0] = Cvt<T>(a);
    dst[1] = Cvt<T>(b);
    dst[2] = Cvt<T>(c);
    dst[3] = Cvt<T>(d);
}
template <class T> void Put(T *dst, const float *v) { Put(dst, v[0], v[1], v[2], v[3]); }

float AsFloat(uint32_t w)
{
    float f;
    std::memcpy(&f, &w, 4);
    return f;
}

// A frame's rows as registers: rotation row r's three words as they are, then offset word r. The image loops until
// `count` runs out after each register.
// ORIGINAL-BUG: a count of 0 runs on past the frame (Init lets sources 0..5 through with any count up to 3).
template <class T> T *Rows(T *dst, const uint32_t *frame, uint32_t count)
{
    uint32_t r = 0;
    do {
        std::memcpy(dst, frame + 1 + 3 * r, 12);
        dst[3] = Cvt<T>(AsFloat(frame[10 + r]));
        dst += 4;
        ++r;
    } while (--count != 0);
    return dst;
}
const uint32_t *Words(const FramePlacement *frame) { return reinterpret_cast<const uint32_t *>(frame); }

void Bytes(void *dst, const void *src, uint32_t count) { std::memcpy(dst, src, count * 16); }
template <class T> void Zero(T *dst, int32_t words)
{
    for (int32_t i = 0; i < words; ++i) dst[i] = T(0);
}

// The matrix helpers (src/compgeom/Matrix4.h; ours).
float *FrameToMatrix(const void *frame, float *out) // 0x19c0e0 (returns out)
{
    return static_cast<const nGZCompGeom::FrameMath *>(static_cast<const AffineTransform3 *>(frame))->ToMatrix4(out);
}
float *RigidToMatrix(const void *rigid, float *out) // 0x19c1c0 (returns out)
{
    return static_cast<const nGZCompGeom::RigidTransform3 *>(rigid)->ToMatrix4(out);
}
float *Multiply(float *out, const float *a, const float *b) // 0x1a5eb0 (returns out)
{
    return nGZCompGeom::Matrix4Multiply(out, a, b);
}
float *Inverse(float *out, const float *m) // 0x1a6410 (returns out): zero for a zero determinant
{
    return nGZCompGeom::Matrix4Inverse(out, m);
}
// 0x1a7200: the 4x4 of a 3x3 rotation, a per-axis scale and a translation, into out.
void Compose4x4(const float *rotation, const float *scale, const float *translation, float *out)
{
    nGZCompGeom::Matrix4FromRotationScaleTranslation(rotation, scale, translation, out);
}
// 0x19c800: v turned by the rigid transform's rotation (its quaternion at +0xc), in place.
float *TurnByPlacement(const void *rigid, float *v)
{
    return static_cast<const nGZCompGeom::RigidTransform3 *>(rigid)->RotateVector(v);
}

cIDetailSource *Detail() { return static_cast<cIDetailSource *>(GetDetailObject()); }

// The global light direction (0x106f428, three floats).
const float *LightDirection() { return GameAddress<const float>(0x106f428); }

// Source 0x13: the detail matrix times the texture matrix projecting along the light direction (the quaternion of a
// zero angle about it: the identity unless the direction's length is 0 or not finite, which makes it NaN).
const float *ProjectedLight(float *out)
{
    const float *const m = Detail()->GetMatrix();
    const float *const g = LightDirection();
    const float length = static_cast<float>(std::sqrt(static_cast<double>(
        AddS(AddS(MulS(g[0], g[0]), MulS(g[1], g[1])), MulS(g[2], g[2])))));
    const float inv = DivS(1.0f, length);
    const float s = static_cast<float>(std::sin(0.0));
    const float q[4] = {MulS(MulS(inv, g[0]), s), MulS(MulS(inv, g[1]), s), MulS(MulS(inv, g[2]), s),
                        static_cast<float>(std::cos(0.0))};
    float rotation[9];
    nGZCompGeom::QuaternionToMatrix(rotation, q);
    const float scale[3] = {0.5f, -0.5f, 1.0f};
    const float translation[3] = {0.5f, 0.5f, 0.0f};
    float texture[16];
    Compose4x4(rotation, scale, translation, texture);
    return Multiply(out, m, texture);
}

// floor(t) by the 1.5 * 2^23 rounding trick: t - 0.5 rounded to nearest, one more when that lands on t.
int32_t FloorTrick(float t)
{
    const float rounded = AddS(SubS(t, 0.5f), 12582912.0f);
    int32_t bits;
    std::memcpy(&bits, &rounded, 4);
    const int32_t down = bits - 0x4b400000;
    const int32_t up = down + 1;
    return static_cast<float>(up) == t ? up : down;
}

template <class T> void Fill(T *dst, const Binding *b, const SourceContext *ctx, const SourceItem *item)
{
    const uint32_t count = b->mCount;
    const uint32_t runs = item->mHolderRuns;
    switch (b->mSource) {
    case 0x00:
        Rows(dst, item->mTransforms, count);
        return;
    case 0x01: {
        FramePlacement frame;
        std::memcpy(&frame, item->mTransforms, sizeof frame);
        Rows(dst, Words(frame.Invert()), count);
        return;
    }
    case 0x02:
        Rows(dst, Words(&ctx->mFrameInverse), count);
        return;
    case 0x03:
        Rows(dst, Words(&ctx->mFrame), count);
        return;
    case 0x04: {
        FramePlacement frame;
        std::memcpy(&frame, item->mTransforms, sizeof frame);
        Rows(dst, Words(frame.Compose(&ctx->mFrame)), count);
        return;
    }
    case 0x05: {
        FramePlacement frame;
        std::memcpy(&frame, item->mTransforms, sizeof frame);
        FramePlacement product = *frame.Compose(&ctx->mFrame);
        Rows(dst, Words(product.Invert()), count);
        return;
    }
    case 0x06: {
        float m[16];
        Bytes(dst, Inverse(m, ctx->mProjection), count);
        return;
    }
    case 0x07:
        Bytes(dst, ctx->mProjection, count);
        return;
    case 0x08:
        Bytes(dst, ctx->mMatrix1c4, count);
        return;
    case 0x09: {
        float frame[16], m[16];
        Bytes(dst, Multiply(m, FrameToMatrix(item->mTransforms, frame), ctx->mProjection), count);
        return;
    }
    case 0x0a: {
        const SourceContext *const outer = ctx->mOuter;
        uint8_t scratch[0x40];
        const void *const placement = (*item->mEventSource)->GetPlacement(scratch);
        const SourceContext *const base = outer ? outer : ctx;
        float rigid[16], m[16];
        Bytes(dst, Multiply(m, RigidToMatrix(placement, rigid), base->mView), count);
        return;
    }
    case 0x0b: {
        float frame[16], product[16], m[16];
        Bytes(dst, Inverse(m, Multiply(product, FrameToMatrix(item->mTransforms, frame), ctx->mProjection)), count);
        return;
    }
    case 0x0c:
        Bytes(dst, ctx->mView, count);
        return;
    case 0x0d: {
        float m[16];
        Bytes(dst, Inverse(m, ctx->mView), count);
        return;
    }
    case 0x0e: {
        float m[16];
        std::memcpy(m, ctx->mProjection, sizeof m);
        const float k = 3.0517578e-05f; // 1 / 32768
        m[9] = AddS(MulS(MulS(-b->mValues[0], k), m[13]), m[9]);
        Bytes(dst, m, count);
        return;
    }
    case 0x0f:
    case 0x10: {
        const BoneSet *const bones = item->mBones->GetBoneSet(item->mBoneSet);
        const uint32_t n = (std::min)(item->mCounts >> 15 & 0x7f, count / 3);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t *const f = item->mTransforms + bones->mIndices[i] * 13;
            T *const d = dst + 12 * i;
            if (b->mSource == 0x0f) {
                Rows(d, f, 3);
            } else {
                std::memcpy(d, f + 1, 12);
                std::memcpy(d + 4, f + 4, 12);
                std::memcpy(d + 8, f + 10, 12);
            }
        }
        return;
    }
    case 0x11: {
        T *const d = dst + b->mDimension;
        const uint32_t n = (std::min)(item->mCounts >> 22 & 0x3f, count);
        for (uint32_t i = 0; i < n; ++i) d[4 * i] = Cvt<T>(item->mScalars[i]);
        return;
    }
    case 0x12:
        Bytes(dst, Detail()->GetMatrix(), count);
        return;
    case 0x13: {
        float m[16];
        Bytes(dst, ProjectedLight(m), count);
        return;
    }
    case 0x14: {
        const float t = ctx->mTime;
        const int32_t floor = FloorTrick(t);
        dst[0] = T(0);
        dst[1] = Cvt<T>(SubS(t, static_cast<float>(static_cast<double>(static_cast<uint32_t>(floor)))));
        dst[2] = Cvt<T>(static_cast<float>(static_cast<double>(ctx->mFrameNumber)));
        dst[3] = Cvt<T>(t);
        return;
    }
    case 0x15:
    case 0x17: {
        cIDetailSource *const detail = Detail();
        const float *const v = b->mSource == 0x15 ? detail->GetVectorA() : detail->GetVectorB();
        Put(dst, v[0], v[1], v[2], 0.0f);
        return;
    }
    case 0x16: {
        const float *const table = reinterpret_cast<const float *>(item->mHolders);
        if (!table) Put(dst, 0.0f, 0.0f, 0.0f, 0.0f);
        else Put(dst, table[-3], table[-2], table[-1], 0.0f);
        return;
    }
    case 0x18:
        Put(dst, b->mValues);
        return;
    case 0x19: {
        if (!item->mValues) {
            Put(dst, b->mValues);
            return;
        }
        const float *const p = reinterpret_cast<const float *>(item->mValues + b->mOffset);
        const uint8_t dim = b->mDimension;
        switch (b->mChannel) {
        case 4: dst[3] = Cvt<T>(p[3]); [[fallthrough]];
        case 3: dst[dim + 2] = Cvt<T>(p[2]); [[fallthrough]];
        case 2: dst[dim + 1] = Cvt<T>(p[1]); [[fallthrough]];
        case 1: dst[dim] = Cvt<T>(p[0]); break;
        default: break;
        }
        return;
    }
    case 0x1a: {
        struct {
            const void *mVTable; // cVectorEvent<cVector<4, float>>
            float mScale[4];
        } event{GameAddress<void>(0xdb07d4), {1.0f, 1.0f, 1.0f, 1.0f}};
        (*item->mEventSource)->GetEvent(&event, 0x8c798726, 0);
        Put(dst, event.mScale[1], event.mScale[2], event.mScale[3], event.mScale[0]);
        return;
    }
    case 0x1b: {
        Put(dst, 0.0f, 0.0f, 0.0f, 1.0f);
        const uint32_t at = Nibble(runs, 4);
        if (Nibble(runs, 5) == at) return;
        const float *const r = (*static_cast<cIDepthRange *const *>(item->mHolders[at]))->GetRange();
        dst[0] = Cvt<T>(DivS(-1.0f, SubS(r[1], r[0])));
        dst[1] = Cvt<T>(DivS(r[1], SubS(r[1], r[0])));
        return;
    }
    case 0x1c: {
        // Four colours a modulator (r, g, b and a scale first) while registers are left; the run is signed here (an end
        // below its start gives none).
        const uint32_t first = Nibble(runs, 3);
        const int32_t holders = static_cast<int32_t>(Nibble(runs, 4)) - static_cast<int32_t>(first);
        uint32_t left = count;
        T *d = dst;
        for (int32_t h = 0; h < holders && left != 0; ++h) {
            cIColorModulator *const modulator = *static_cast<cIColorModulator *const *>(item->mHolders[first + h]);
            for (int32_t i = 0; i < 4 && left != 0; ++i) {
                float colour[4]; // the image passes it unset and reads what the modulator left in it
                modulator->GetColor(colour, i);
                --left;
                Put(d, colour[1], colour[2], colour[3], colour[0]);
                d += 4;
            }
        }
        return;
    }
    case 0x1d: {
        // Four registers a light while registers are left: direction, colour, scaled colour, and a fourth by kind.
        int32_t left = static_cast<int32_t>(count);
        T *d = dst;
        for (uint32_t i = 0, n = RunLength(runs, kDirectional); i < n && left > 0; ++i) {
            const LightHolder *const light = Light(item, kDirectional, i);
            const LightParams p = Params(light, item);
            const float row0[4] = {light->Word(9), light->Word(12), light->Word(15), 0.0f};
            const float *const g = LightDirection();
            float v[3] = {-g[0], -g[1], -g[2]};
            uint8_t scratch[0x40];
            TurnByPlacement(light->mNode->GetPlacement(scratch), v);
            Put(d, row0);
            Put(d + 4, p.q[0], p.q[1], p.q[2], 1.0f);
            Put(d + 8, MulS(p.q[0], p.q[3]), MulS(p.q[1], p.q[3]), MulS(p.q[2], p.q[3]), 1.0f);
            const float x = -v[0];
            Put(d + 12, x, -v[1], -v[2], x > 0.0f ? 1.0f : -1.0f);
            d += 16;
            left -= 4;
        }
        for (uint32_t i = 0, n = RunLength(runs, kPoint); i < n && left > 0; ++i) {
            const LightHolder *const light = Light(item, kPoint, i);
            const LightParams p = Params(light, item);
            Put(d + 4, p.q[0], p.q[1], p.q[2], 1.0f);
            Put(d + 8, MulS(p.q[0], p.q[3]), MulS(p.q[1], p.q[3]), MulS(p.q[2], p.q[3]), Squared(p.q[5]));
            Put(d + 12, light->Word(16), light->Word(17), light->Word(18), p.q[4]);
            d += 16;
            left -= 4;
        }
        for (uint32_t i = 0, n = RunLength(runs, kSpot); i < n && left > 0; ++i) {
            const LightHolder *const light = Light(item, kSpot, i);
            const LightParams p = Params(light, item);
            Put(d, light->Word(9), light->Word(12), light->Word(15), HalfCos(p.q[7]));
            const float outer = OuterCos(p);
            const float inner = InnerCos(p);
            // Outer less inner here (the other layouts take inner less outer).
            Put(d + 4, p.q[0], p.q[1], p.q[2], DivS(1.0f, SubS(outer, inner)));
            Put(d + 8, MulS(p.q[0], p.q[3]), MulS(p.q[1], p.q[3]), MulS(p.q[2], p.q[3]), Squared(p.q[5]));
            Put(d + 12, light->Word(16), light->Word(17), light->Word(18), p.q[4]);
            d += 16;
            left -= 4;
        }
        return;
    }
    case 0x1e: {
        // Blocks of registers by part: directions at 0, colours at 8, positions at 16; then words from 24 on: the point
        // and spot lights' parameter 4 at 24 + directional count, parameter 5 squared at 26 + that, and the spots' cone
        // cosines at 28 and 30 + directional and point counts. No register limit.
        const uint32_t nd = RunLength(runs, kDirectional);
        T *a = dst, *c = dst + 0x20, *pos = dst + 0x40;
        for (uint32_t i = 0; i < nd; ++i) {
            const LightHolder *const light = Light(item, kDirectional, i);
            Put(a, light->Word(9), light->Word(12), light->Word(15), 0.0f);
            Put(pos, 0.0f, 0.0f, 0.0f, 0.0f);
            const LightParams p = Params(light, item);
            Put(c, p.q);
            a += 4, pos += 4, c += 4;
        }
        Zero(dst + 0x60, 0x20);
        T *s4 = dst + nd + 0x60, *s5 = dst + nd + 0x68;
        const uint32_t np = RunLength(runs, kPoint);
        for (uint32_t i = 0; i < np; ++i) {
            const LightHolder *const light = Light(item, kPoint, i);
            Put(a, 0.0f, 0.0f, 0.0f, 0.0f);
            Put(pos, light->Word(16), light->Word(17), light->Word(18), 0.0f);
            const LightParams p = Params(light, item);
            Put(c, p.q);
            *s4++ = Cvt<T>(p.q[4]);
            *s5++ = Cvt<T>(Squared(p.q[5]));
            a += 4, pos += 4, c += 4;
        }
        T *outerAt = dst + np + nd + 0x70, *spread = dst + np + nd + 0x78;
        for (uint32_t i = 0, n = RunLength(runs, kSpot); i < n; ++i) {
            const LightHolder *const light = Light(item, kSpot, i);
            Put(a, light->Word(9), light->Word(12), light->Word(15), 0.0f);
            Put(pos, light->Word(16), light->Word(17), light->Word(18), 0.0f);
            const LightParams p = Params(light, item);
            Put(c, p.q);
            *s4++ = Cvt<T>(p.q[4]);
            *s5++ = Cvt<T>(Squared(p.q[5]));
            const float outer = OuterCos(p);
            *outerAt++ = Cvt<T>(outer);
            *spread++ = Cvt<T>(DivS(1.0f, SubS(InnerCos(p), outer)));
            a += 4, pos += 4, c += 4;
        }
        return;
    }
    case 0x1f:
    case 0x20: {
        // A register a light: 0x1f the directions (none for the point lights), 0x20 the positions (none for the
        // directional ones).
        const bool direction = b->mSource == 0x1f;
        int32_t left = static_cast<int32_t>(count);
        T *d = dst;
        for (int run = kDirectional; run <= kSpot; ++run) {
            for (uint32_t i = 0, n = RunLength(runs, run); i < n && left > 0; ++i) {
                const LightHolder *const light = Light(item, run, i);
                if (direction ? run == kPoint : run == kDirectional) Put(d, 0.0f, 0.0f, 0.0f, 0.0f);
                else if (direction) Put(d, light->Word(9), light->Word(12), light->Word(15), 0.0f);
                else Put(d, light->Word(16), light->Word(17), light->Word(18), 0.0f);
                d += 4;
                --left;
            }
        }
        return;
    }
    case 0x21:
    case 0x22: {
        // A register a light: its colour (0x22 halved).
        const float scale = b->mSource == 0x22 ? 0.5f : 1.0f;
        int32_t left = static_cast<int32_t>(count);
        T *d = dst;
        for (int run = kDirectional; run <= kSpot; ++run) {
            for (uint32_t i = 0, n = RunLength(runs, run); i < n && left > 0; ++i) {
                const LightParams p = Params(Light(item, run, i), item);
                --left;
                Put(d, MulS(p.q[0], scale), MulS(p.q[1], scale), MulS(p.q[2], scale), 1.0f);
                d += 4;
            }
        }
        return;
    }
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26: {
        // A word a light from the registers zeroed: 0x23 parameter 5 squared and 0x24 parameter 4 of the point and spot
        // lights (after the directional count), 0x25 the outer cone cosine and 0x26 1 / (inner - outer) of the spots
        // (after the directional and point counts).
        Zero(dst, static_cast<int32_t>(count << 2));
        const bool spotsOnly = b->mSource >= 0x25;
        const uint32_t skip = RunLength(runs, kDirectional) + (spotsOnly ? RunLength(runs, kPoint) : 0);
        T *d = dst + skip;
        int32_t left = static_cast<int32_t>(count * 4 - skip);
        for (int run = spotsOnly ? kSpot : kPoint; run <= kSpot; ++run) {
            for (uint32_t i = 0, n = RunLength(runs, run); i < n && left > 0; ++i) {
                const LightParams p = Params(Light(item, run, i), item);
                --left;
                float v;
                switch (b->mSource) {
                case 0x23: v = Squared(p.q[5]); break;
                case 0x24: v = p.q[4]; break;
                case 0x25: v = OuterCos(p); break;
                default: {
                    const float inner = InnerCos(p);
                    v = DivS(1.0f, SubS(inner, OuterCos(p)));
                    break;
                }
                }
                *d++ = Cvt<T>(v);
            }
        }
        return;
    }
    case 0x27: {
        // A register a light from the registers zeroed: none for the directional ones (skipped, not counted), the
        // point's (0, 0, parameter 4, parameter 5 squared), the spot's (outer cosine, 1 / (inner - outer), the same).
        Zero(dst, static_cast<int32_t>(count << 2));
        int32_t left = static_cast<int32_t>(count);
        T *d = dst;
        for (uint32_t i = 0, n = RunLength(runs, kDirectional); i < n && left > 0; ++i) {
            Put(d, 0.0f, 0.0f, 0.0f, 0.0f);
            d += 4;
        }
        for (uint32_t i = 0, n = RunLength(runs, kPoint); i < n && left > 0; ++i) {
            const LightParams p = Params(Light(item, kPoint, i), item);
            --left;
            Put(d, 0.0f, 0.0f, p.q[4], Squared(p.q[5]));
            d += 4;
        }
        for (uint32_t i = 0, n = RunLength(runs, kSpot); i < n && left > 0; ++i) {
            const LightParams p = Params(Light(item, kSpot, i), item);
            const float outer = OuterCos(p);
            const float inner = InnerCos(p);
            --left;
            Put(d, outer, DivS(1.0f, SubS(inner, outer)), p.q[4], Squared(p.q[5]));
            d += 4;
        }
        return;
    }
    case 0x28:
    case 0x29: {
        // Six registers of light indices from the registers zeroed; each (x, y, 1, 0).
        Zero(dst, static_cast<int32_t>(count << 2));
        const uint32_t nd = RunLength(runs, kDirectional), np = RunLength(runs, kPoint), ns = RunLength(runs, kSpot);
        uint32_t x[6], y[6];
        if (b->mSource == 0x28) {
            // Per kind: whether any (with 0), and the last index and one past (or 0 for none).
            x[0] = nd != 0, y[0] = 0;
            x[1] = nd ? nd - 1 : 0, y[1] = 1;
            x[2] = np != 0, y[2] = np ? nd : 0;
            x[3] = np ? np - 1 : 0, y[3] = np ? nd + 1 : 0;
            x[4] = ns != 0, y[4] = ns ? np + nd : 0;
            x[5] = ns ? ns - 1 : 0, y[5] = ns ? np + nd + 1 : 0;
        } else {
            // Per kind: the count and the first index.
            x[0] = 0, y[0] = 0;
            x[1] = nd, y[1] = 0;
            x[2] = 0, y[2] = 0;
            x[3] = np, y[3] = nd;
            x[4] = 0, y[4] = 0;
            x[5] = ns, y[5] = np + nd;
        }
        for (int r = 0; r < 6; ++r) {
            dst[4 * r] = Count<T>(x[r]);
            dst[4 * r + 1] = Count<T>(y[r]);
            dst[4 * r + 2] = Count<T>(1);
            dst[4 * r + 3] = Count<T>(0);
        }
        return;
    }
    case 0x2a: {
        // Three registers: the first light of each kind's colour (r, g, b, 1), or zeros for a kind with none. The image
        // gathers every light's into a table of eight registers before a ninth of zeros (more run past it).
        float table[48][4];
        std::memset(table[8], 0, sizeof table[8]);
        uint32_t at = 0;
        for (int run = kDirectional; run <= kSpot; ++run) {
            for (uint32_t i = 0, n = RunLength(runs, run); i < n; ++i, ++at) {
                const LightParams p = Params(Light(item, run, i), item);
                table[at][0] = p.q[0], table[at][1] = p.q[1], table[at][2] = p.q[2], table[at][3] = 1.0f;
            }
        }
        const uint32_t nd = RunLength(runs, kDirectional), np = RunLength(runs, kPoint), ns = RunLength(runs, kSpot);
        Put(dst, table[nd ? 0 : 8]);
        Put(dst + 4, table[np ? nd : 8]);
        Put(dst + 8, table[ns ? nd + np : 8]);
        return;
    }
    default:
        return;
    }
}

} // namespace

void __cdecl FillFloatConstants(float *dst, const Binding *binding, const cShaderApplyContext *ctx,
                                const cShaderApplyItem *item)
{
    Fill(dst, binding, reinterpret_cast<const SourceContext *>(ctx), reinterpret_cast<const SourceItem *>(item));
}

void __cdecl FillIntConstants(int32_t *dst, const Binding *binding, const cShaderApplyContext *ctx,
                              const cShaderApplyItem *item)
{
    Fill(dst, binding, reinterpret_cast<const SourceContext *>(ctx), reinterpret_cast<const SourceItem *>(item));
}

} // namespace nGZSceneGraph
