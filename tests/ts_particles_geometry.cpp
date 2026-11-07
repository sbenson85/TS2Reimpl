// Differential tests of cTSParticlesGeometry (lane WQ, rendering row E3; src/ts/cTSParticlesGeometry.h) and its two leaves.
//
// Each side builds its own geometry: the game's constructor 0xbb4e90 on a block of 0x78 bytes, and ours (the hook's adapter).
// The world is the same on both sides: a real cTSParticlesDescription (read only by the code under test), a plain block as
// the effect (cTSParticlesEffect), particles on a list, and the fakes of tests/drawfx_core.h for the format, the managers,
// the device state, the device and four dynamic arrays. Every Lock fills its 64 KB buffer with 0xcd and every Unlock logs the
// first 16 KB word for word and a hash of the rest, so each batch is compared bit for bit and a write outside the requested
// vertices shows. Worlds holding NaNs, infinities and denormals keep their key lists to one entry when a particle's t can
// leave [0, 1]: the original reads past a longer list (// ORIGINAL-BUG in the source), and what lies there is not comparable.
//
// Precision: Particles_Render runs every seed at 53 bits and at 24 bits (the two x87 sites of the body, the screw and the
// warp / stretch dots, follow the control word); the harness's default is 53, the game's device thread 24.
//
//   Particles_Construct           0xbb4e90 and the object it leaves; the format built; the destructor after it
//   Particles_Destroy             0xbb5490 with and without a format / declaration
//   Particles_DeletingDestructor  0xbb5890 (flags without bit 0: the original's delete is the CRT's, not run)
//   Particles_IsSupported         0xbb9460: declaration missing, lost, refused
//   Particles_Render              0xbbdd50: the worlds above, both precisions, and again with small bloom slopes (the
//                                 screen test's ulps reach the vertices); result, object, effect, every call in order
//   Particles_SetBounds           0xbc10f0
//   Particles_Slots               the folded slots through the vtable: 0x3af660, 0x148fb0, 0x153f50, 0x142840, 0xa441a0,
//                                 0x18a5b0, 0x377e0
//   Particles_ReleaseLast         ours only: the last Release deletes (0x153f50 calls the deleting destructor with 1)
//   Particles_Curve               0xbb5620 directly, both precisions: exact keys, one ulp beside them, out of range, kinds
//   Particles_Rotate              0xbc0f80 directly
#include "difftest.h"
#include "game/vtable.h"
#include "sgfx.h"
#include "test_helpers.h"

#include "ts/ParticleCurve.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"
#include "ts/cTSParticlesGeometry.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <utility>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kObjectTag = 0x7d000000;
constexpr uint32_t kUsagePosition = 0x5b830781;
constexpr uint32_t kUsageTexture = 0xbb8307ab;
constexpr uint32_t kUsageColor = 0xdb830795;
constexpr uint32_t kBufferSize = 0x10000;
constexpr uint32_t kCompared = 0x4000;
constexpr unsigned kMaxParticles = 48;

#define PG_UNKNOWN_SLOTS {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""}
#define U {0, Ret::Int, false, "", ""}
#define U2 U, U
#define U4 U2, U2
#define U8 U4, U4

inline constexpr Slot kFormatSlots[] = {PG_UNKNOWN_SLOTS, U8, // 3-10
                                        {1, Ret::Int, true, "GetStreamSize", "u"}, // 11
                                        U, U, U,                                   // 12-14
                                        {2, Ret::Int, true, "GetOffsetAny", "ub"}}; // 15
inline constexpr drawfx::Iface kFormat{"VertexFormat", 16, kFormatSlots};

inline constexpr Slot kFormatsSlots[] = {PG_UNKNOWN_SLOTS, {0, Ret::Int, true, "BeginFormat", ""},
                                         {4, Ret::Int, true, "AddElement", "uuub"}, {1, Ret::Int, true, "EndFormat", "i"}};
inline constexpr drawfx::Iface kFormats{"VertexFormatsManager", 6, kFormatsSlots};

inline constexpr Slot kDeclsSlots[] = {PG_UNKNOWN_SLOTS, {3, Ret::Int, true, "GetDeclaration", "iii"}};
inline constexpr drawfx::Iface kDecls{"VertexDeclarationsManager", 4, kDeclsSlots};

inline constexpr Slot kDeclSlots[] = {PG_UNKNOWN_SLOTS, U, U, U, {0, Ret::Int, true, "IsCreated", ""}};
inline constexpr drawfx::Iface kDecl{"VertexDeclaration", 7, kDeclSlots};

inline constexpr Slot kStateSlots[] = {PG_UNKNOWN_SLOTS, U8, // 3-10
                                       U,                   // 11
                                       {1, Ret::Int, true, "ApplyStateCommand", "i"}, // 12
                                       U, U, U,                                       // 13-15
                                       {1, Ret::Int, true, "SetVertexDeclaration", "i"},
                                       {4, Ret::Int, true, "SetStreamSource", "uiuu"},
                                       {1, Ret::Int, true, "SetIndices", "i"}};
inline constexpr drawfx::Iface kState{"DeviceState", 19, kStateSlots};

inline constexpr Slot kDeviceSlots[] = {PG_UNKNOWN_SLOTS, U8, U8, U8, U4, // 3-30
                                        {3, Ret::Int, true, "DrawPrimitive", "uuu"},
                                        {6, Ret::Int, true, "DrawIndexedPrimitive", "uuuuuu"}};
inline constexpr drawfx::Iface kDevice{"Device", 33, kDeviceSlots};

inline constexpr Slot kArraySlots[] = {PG_UNKNOWN_SLOTS, U8, U4, // 3-14
                                       {3, Ret::Int, true, "Lock", "uuu"},
                                       {0, Ret::Int, true, "Unlock", ""},
                                       U, U, U,
                                       {2, Ret::Int, true, "Reserve", "uu"},
                                       {0, Ret::Int, true, "GetCapacity", ""}};
inline constexpr drawfx::Iface kArray{"DynamicArray", 22, kArraySlots};

inline constexpr Slot kSystemSlots[] = {PG_UNKNOWN_SLOTS, U8, U8, U2, // 3-20
                                        {0, Ret::Int, true, "GetCurrentDeviceIndex", ""},
                                        U, U,
                                        {1, Ret::Int, true, "GetDevice", "u"}};
inline constexpr drawfx::Iface kSystem{"GraphicSystem", 25, kSystemSlots};

#undef PG_UNKNOWN_SLOTS
#undef U8
#undef U4
#undef U2
#undef U

enum : unsigned { kStride, kPosition, kTexture, kColor, kOther };

Fake gFormat, gFormats, gDecls, gDeclPre, gDeclNew[4], gState, gDevice, gSystem, gArrays[4];
uint8_t gBuffers[4][kBufferSize];
cGeometryRenderContext gContext;
unsigned gNextDecl;

alignas(16) uint8_t gEffectBytes[sizeof(cTSParticlesEffect)]; // fields set by hand; never constructed
cTSParticlesEffect &gEffect = reinterpret_cast<cTSParticlesEffect &>(gEffectBytes);
cTSParticlesSettings gSettings;
cTSParticle gParticles[kMaxParticles];
struct cTSParticleNode { // a node of the effect's std::list, written by hand: next, prev, the particle
    cTSParticleNode *mNext;
    uint32_t mWord4;
    cTSParticle *mParticle;
};
cTSParticleNode gNodes[kMaxParticles];
cTSParticleNode *gHead;
std::unique_ptr<cTSParticlesDescription> gDescription;

uint32_t Hash(const uint8_t *p, unsigned n)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

uint32_t FormatStreamSize(Fake &, const uint32_t *) { return gFormat.value[kStride]; }
uint32_t FormatOffset(Fake &, const uint32_t *a)
{
    return a[0] == kUsagePosition  ? gFormat.value[kPosition]
           : a[0] == kUsageTexture ? gFormat.value[kTexture]
           : a[0] == kUsageColor   ? gFormat.value[kColor]
                                   : gFormat.value[kOther];
}
uint32_t EndFormat(Fake &, const uint32_t *a)
{
    ++gFormat.refs;
    *reinterpret_cast<void **>(uintptr_t(a[0])) = &gFormat;
    return 0;
}
// value[0]: answer a declaration (counted); value[1]: the result.
uint32_t GetDeclaration(Fake &self, const uint32_t *a)
{
    if (self.value[0]) {
        Fake &d = gDeclNew[gNextDecl++ & 3];
        ++d.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &d;
    }
    return self.value[1];
}
uint32_t IsCreated(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t CurrentIndex(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t GetDevice(Fake &, const uint32_t *) { return P(&gDevice); }

// The command the render builds on its stack: the words it writes (+5 is the header's padding, never written; the alpha test
// state's last two bytes are not written either).
uint32_t ApplyState(Fake &, const uint32_t *a)
{
    const uint8_t *const c = reinterpret_cast<const uint8_t *>(uintptr_t(a[0]));
    uint32_t vtable, tail;
    uint16_t size;
    std::memcpy(&vtable, c, 4);
    std::memcpy(&size, c + 6, 2);
    std::memcpy(&tail, c + 8, 4);
    if (c[4] == 0x12) tail &= 0xffff;
    drawfx::Note("state command", {vtable, uint32_t(c[4]), uint32_t(size), tail});
    return 0;
}

unsigned BufferOf(const Fake &self) { return unsigned(&self - gArrays); }

// value[0]: Reserve's answer (the next one differs); the buffer comes back full of the sentinel.
uint32_t Lock(Fake &self, const uint32_t *)
{
    std::memset(gBuffers[BufferOf(self)], 0xcd, kBufferSize);
    return P(gBuffers[BufferOf(self)]);
}
uint32_t Unlock(Fake &self, const uint32_t *)
{
    difftest::NotCounted bookkeeping;
    const uint8_t *const buffer = gBuffers[BufferOf(self)];
    drawfx::Call c;
    c.note = "buffer";
    c.words.resize(kCompared / 4 + 2);
    c.words[0] = BufferOf(self);
    std::memcpy(&c.words[1], buffer, kCompared);
    c.words[kCompared / 4 + 1] = Hash(buffer + kCompared, kBufferSize - kCompared);
    drawfx::gLog.push_back(std::move(c));
    return 0;
}
uint32_t Reserve(Fake &self, const uint32_t *) { return self.value[0]++; }

template <class T> T Pick(std::mt19937 &rng, const T *list, unsigned n) { return list[rng() % n]; }

const float kNan = std::bit_cast<float>(0xffc00000u);
const float kNan2 = std::bit_cast<float>(0x7fc12345u);
const float kInf = std::bit_cast<float>(0x7f800000u);
const float kWild[] = {0, 0, -0.0f, 1, -1, 0.5f, 2, 1e-7f, -1e-7f, 1e-5f, 1e-40f, -1e-40f, 1e30f, -1e30f, kInf, -kInf, kNan, kNan2, 3.1415927f, 100};

// A float of the world: wild worlds draw from the table, plain ones are finite with exact zeros among them.
struct Draw {
    std::mt19937 &rng;
    bool wild;
    float operator()()
    {
        if (wild && rng() % 3 == 0) return Pick(rng, kWild, 20);
        if (rng() % 8 == 0) return 0.0f;
        return float(int32_t(rng() % 16001) - 8000) / 1000.0f;
    }
    // In [0, 1], as colours and alphas mostly are.
    float Unit()
    {
        if (wild && rng() % 3 == 0) return Pick(rng, kWild, 20);
        switch (rng() % 8) {
        case 0: return 0.0f;
        case 1: return 1.0f;
        default: return float(rng() % 1025) / 1024.0f;
        }
    }
};

void MakeCurve(cTSEffectCurve &curve, std::mt19937 &rng, Draw &f)
{
    curve.mValues.clear();
    const unsigned shape = rng() % 8;
    switch (shape) {
    case 0:
    case 1:
    case 2:
        curve.mKind = cTSEffectCurve::kList;
        curve.mValues.push_back(f());
        break;
    case 3:
    case 4:
    case 5: {
        curve.mKind = cTSEffectCurve::kList;
        const unsigned n = 2 + rng() % 5;
        for (unsigned i = 0; i < n; ++i) curve.mValues.push_back(f());
        break;
    }
    case 6:
        curve.mKind = cTSEffectCurve::kWave;
        for (unsigned i = 0; i < 4; ++i) curve.mValues.push_back(f());
        break;
    default: {
        static const uint32_t kinds[] = {0x12000000u, 0x81000001u, 1u, 0x80000000u};
        curve.mKind = Pick(rng, kinds, 4);
        for (unsigned i = 0; i < 4; ++i) curve.mValues.push_back(f());
        break;
    }
    }
}

void MakeTransform(fx::EffectsTransform &t, std::mt19937 &rng, Draw &f)
{
    static const uint8_t flags[] = {0, 2, 2, 3, 1, 0xfd, 0xff};
    std::memset(&t, 0xa5, sizeof t);
    t.flags = Pick(rng, flags, 7);
    for (auto &row : t.m)
        for (float &cell : row) cell = f();
    static const float scales[] = {1, 1, 0.5f, 2, 0.1f, 3.25f, 0};
    t.scale = rng() % 4 ? Pick(rng, scales, 7) : f();
    t.translation = {f(), f(), f()};
}

// What a seed's render sees.
bool gGentleBloom = false;

void MakeWorld(std::mt19937 &rng)
{
    const bool wild = rng() % 3 == 0;
    Draw f{rng, wild};

    gDescription = std::make_unique<cTSParticlesDescription>();
    cTSParticlesDescription &d = *gDescription;

    const unsigned lists = wild ? 1 : 1 + rng() % 5; // key entries: wild worlds' t leaves [0, 1]
    d.mColors.clear();
    for (unsigned i = 0; i < lists; ++i) d.mColors.push_back({f.Unit(), f.Unit(), f.Unit()});
    const unsigned pivots = wild ? 1 : 1 + rng() % 5;
    d.mPivots.clear();
    for (unsigned i = 0; i < pivots; ++i) d.mPivots.push_back({f(), f(), f()});

    MakeCurve(d.mSize, rng, f);
    MakeCurve(d.mAspect, rng, f);
    MakeCurve(d.mAlpha, rng, f);
    MakeCurve(d.mRotateCurve[0], rng, f);
    MakeCurve(d.mRotateCurve[1], rng, f);
    MakeCurve(d.mScrewRadius, rng, f);
    MakeCurve(d.mScrewAngle, rng, f);
    if (rng() % 3 == 0) { // no radius: the effect's own screw
        d.mScrewRadius.mKind = cTSEffectCurve::kList;
        d.mScrewRadius.mValues.assign(1, rng() % 4 ? 0.0f : -0.0f);
    }
    d.mRotateOffset[0] = rng() % 3 ? 0.0f : f();
    d.mRotateOffset[1] = f();

    static const uint8_t alignments[] = {0, 0, 1, 1, 5, 5, 6, 6, 2, 3, 4, 7, 0xff};
    d.mAlignment = Pick(rng, alignments, 13);
    d.m38 = {f(), f(), f()};
    d.m44 = {f(), f(), f()};
    if (rng() % 5 == 0) { // nothing left in the plane after the half sum: the fallback axes
        d.m44.x = -d.m38.x;
        d.m44.y = -d.m38.y;
    } else if (rng() % 4 == 0) { // a little left: lengths either side of the alignment epsilon (1e-4)
        static const float lengths[] = {5e-5f, 9.9e-5f, 1e-4f, 1.01e-4f, 2e-4f, 9e-4f, 1.1e-3f};
        d.m38 = {0.0f, 0.0f, f()};
        d.m44 = {2.0f * Pick(rng, lengths, 7), 0.0f, f()};
        if (rng() % 2) std::swap(d.m44.x, d.m44.y);
    }
    d.mScrew = rng() % 2 ? 0.0f : f();
    d.mStretch = rng() % 2 ? 0.0f : f();
    d.mFlags = uint64_t(rng() % 3 ? rng() % 2 : rng()) << 32 | rng();
    d.mScrewRadiusMax = rng() % 2 ? 100.0f : f();
    d.mScrewRadiusToSpeed = uint8_t(rng() % 4);
    d.mScrewAngleToSpeed = uint8_t(rng() % 4);

    d.mWarps.clear();
    const unsigned warps = rng() % 2 ? 0 : 1 + rng() % 3;
    for (unsigned i = 0; i < warps; ++i) {
        cTSParticleWarp warp;
        warp.mValue = f();
        warp.mFirst = {f(), f(), f()};
        warp.mSecond = {f(), f(), f()};
        d.mWarps.push_back(warp);
    }

    const unsigned bloom = rng() % 3;
    for (uint8_t &b : d.mBloom) b = uint8_t(rng());
    if (bloom == 0) d.mBloom[0] = d.mBloom[2] = 0;
    if (bloom == 1 && rng() % 2) d.mBloom[rng() % 2 * 2] = 0;
    if (gGentleBloom) { // slopes and floors that keep the grown size under its clamp, so one ulp of the screen test shows
        d.mBloom[0] = uint8_t(1 + (d.mBloom[0] & 3));
        d.mBloom[1] &= 7;
        d.mBloom[2] = uint8_t(1 + (d.mBloom[2] & 3));
        d.mBloom[3] &= 7;
    }

    static const uint8_t tiles[] = {1, 1, 1, 2, 2, 4, 8, 16, 3, 6, 0, 200};
    d.mTile[0] = Pick(rng, tiles, 12);
    d.mTile[1] = Pick(rng, tiles, 12);
    d.mFrameSpeed = f();
    d.mFrameCount = rng() % 2 ? 0 : uint8_t(rng());

    std::memset(&gEffect, 0xa5, sizeof gEffect);
    std::memset(&gSettings, 0xa5, sizeof gSettings);
    gSettings.mSetsBlend = rng() % 2 ? 0 : uint8_t(rng() | 1);
    *reinterpret_cast<cTSParticleNode ***>(&gEffect.mParticles) = &gHead; // the list's head word: its next is the first node
    gEffect.mDescription = gDescription.get();
    gEffect.mSettings = &gSettings;
    static const double times[] = {0, 1, 0.5, 2.75, 1234.5678, 1e9, -3.5, 0.1};
    gEffect.mTime = Pick(rng, times, 8);
    MakeTransform(gEffect.mLocal, rng, f);
    MakeTransform(gEffect.mWorld, rng, f);
    static const uint16_t drawn[] = {0, 1, 0xffff, 0x7fff};
    gEffect.mFramesDrawn = Pick(rng, drawn, 4);
    static const uint32_t firstFrames[] = {0, 0, 1, 2, 7, 100, 0xffffffffu};
    gEffect.mFirstFrame = Pick(rng, firstFrames, 7);
    gEffect.mSizeScale = rng() % 2 ? 1.0f : f();
    gEffect.mAlphaScale = rng() % 2 ? 1.0f : f();
    gEffect.mColorScale = {f.Unit(), f.Unit(), f.Unit()};

    static const int32_t counts[] = {0, 1, 1, 2, 3, 4, 5, 8, 13, 21, 40};
    gEffect.mCount = Pick(rng, counts, 11);
    const unsigned listed = unsigned(gEffect.mCount) + rng() % 3; // nodes past the count are left alone
    gHead = listed ? gNodes : nullptr;
    for (unsigned i = 0; i < kMaxParticles; ++i) {
        cTSParticleNode &node = gNodes[i];
        node.mNext = i + 1 < listed ? &gNodes[i + 1] : nullptr;
        node.mWord4 = 0xa5a5a5a5;
        node.mParticle = &gParticles[i];

        cTSParticle &p = gParticles[i];
        std::memset(&p, 0x5a, sizeof p);
        p.mFrameOffset = uint8_t(rng());
        static const float lifes[] = {1, 2, 0.5f, 3, 0.1f, 7};
        p.mLife = Pick(rng, lifes, 6);
        const unsigned eighths = rng() % 9;
        p.mAge = eighths == 8 ? p.mLife : p.mLife * float(rng() % 1025) / 1024.0f;
        if (eighths == 0) p.mAge = 0.0f;
        if (eighths == 1) p.mAge = p.mLife * 0.5f;
        if (wild && rng() % 3 == 0) { // t anything: no life, past it, before it
            p.mAge = f();
            p.mLife = f();
        }
        p.mPosition = {f(), f(), f()};
        p.mVelocity = {f(), f(), f()};
        switch (rng() % 5) {
        case 0: p.mVelocity.x = p.mVelocity.y = 0.0f; break; // along the z axis: the screw's fixed side
        case 1: p.mVelocity = {0.0f, 0.0f, 0.0f}; break;     // too slow for the screw and the stretch
        default: break;
        }
        p.mSize = f();
        p.mAspect = f();
        p.mRotation = rng() % 3 ? 0.0f : f();
        p.mPivot[0] = f();
        p.mPivot[1] = f();
        p.mAlpha = f.Unit();
        p.mColor = {f.Unit(), f.Unit(), f.Unit()};
        p.mScrewRadius = f();
        p.mScrewAngle = f();
    }

    std::memset(&gContext, 0, sizeof gContext);
    gContext.mWidth = rng();
    gContext.mHeight = rng();
    std::memset(&gContext.mView, 0xa5, sizeof gContext.mView);
    std::memset(&gContext.mViewInverse, 0xa5, sizeof gContext.mViewInverse);
    for (float &cell : gContext.mView.mMatrix) cell = f();
    for (float &cell : gContext.mViewInverse.mMatrix) cell = f();
    for (float &cell : gContext.mViewInverse.mOffset) cell = f();
    for (float &cell : gContext.mProjection) cell = f();
    gContext.mDevice = reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gDevice);
    gContext.mState = reinterpret_cast<nGZGraphic4::cIDeviceState *>(&gState);
    gContext.mVertices = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&gArrays[0]);
    gContext.mVerticesB = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&gArrays[1]);
    gContext.mIndices = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&gArrays[2]);
    gContext.mIndicesB = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&gArrays[3]);

    // Room for at least one quad when there is anything to draw (none: the original would never finish), the room possibly
    // smaller than the particles, and capacities that do not divide evenly.
    const uint32_t stride = gFormat.value[kStride];
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t room = gEffect.mCount || rng() % 2 ? 1 + rng() % 9 : 0;
        const uint32_t unit = i < 2 ? stride * 4 : 12;
        gArrays[i].Returns(21, room * unit + rng() % unit);
    }
}

void ResetFakes(std::mt19937 &rng)
{
    gFormat.Reset<kFormat>(700);
    gFormats.Reset<kFormats>(701);
    gDecls.Reset<kDecls>(702);
    gDeclPre.Reset<kDecl>(703);
    for (unsigned i = 0; i < 4; ++i) gDeclNew[i].Reset<kDecl>(704 + i);
    gState.Reset<kState>(710);
    gDevice.Reset<kDevice>(711);
    gSystem.Reset<kSystem>(712);
    for (unsigned i = 0; i < 4; ++i) gArrays[i].Reset<kArray>(720 + i);
    gNextDecl = 0;

    gFormat.On(11, &FormatStreamSize);
    gFormat.On(15, &FormatOffset);
    static const uint32_t strides[] = {24, 24, 24, 28, 32, 40, 64, 20, 16, 12, 8, 4};
    gFormat.value[kStride] = Pick(rng, strides, 12);
    const uint32_t stride = gFormat.value[kStride];
    gFormat.value[kPosition] = (rng() % 4) * 4;
    switch (rng() % 4) {
    case 0: // laid out one after the other
        gFormat.value[kPosition] = 0;
        gFormat.value[kColor] = 12;
        gFormat.value[kTexture] = 16;
        break;
    case 1: // the same word for two: the write order shows
        gFormat.value[kColor] = gFormat.value[kPosition];
        gFormat.value[kTexture] = gFormat.value[kPosition] + 4;
        break;
    default:
        gFormat.value[kColor] = (rng() % (stride / 4 + 2)) * 4;
        gFormat.value[kTexture] = (rng() % (stride / 4 + 2)) * 4;
        break;
    }
    gFormat.value[kOther] = 0x1234;
    gFormats.On(5, &EndFormat);
    gDecls.On(3, &GetDeclaration);
    gDecls.value[0] = rng() % 4 != 0;
    gDecls.value[1] = rng() % 3 != 0;
    gDeclPre.On(6, &IsCreated);
    for (Fake &d : gDeclNew) d.On(6, &IsCreated), d.value[0] = rng() % 2;
    gSystem.On(21, &CurrentIndex);
    gSystem.On(24, &GetDevice);
    gSystem.value[0] = rng() % 4;
    gState.On(12, &ApplyState);
    for (unsigned i = 0; i < 4; ++i) {
        gArrays[i].On(15, &Lock);
        gArrays[i].On(16, &Unlock);
        gArrays[i].On(20, &Reserve);
        gArrays[i].value[0] = 100 * i + rng() % 7;
        std::memset(gBuffers[i], 0xcd, kBufferSize);
    }
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed * 2654435761u + 23u);
    ResetFakes(rng);
    MakeWorld(rng);
    cells.Set(0x10caa24, &gFormats); // the cached vertex formats manager
    cells.Set(0x10caa28, &gDecls);   // the cached vertex declarations manager
    cells.Set(0x10ca87c, &gSystem);  // the cached graphic system (the current device)
}

// Everything but the vtable and what the constructor leaves unwritten (+0x3c, +0x69..0x6b).
void NoteObject(const char *label, const uint8_t *mem)
{
    drawfx::Call c;
    c.note = label;
    const uint32_t *w = reinterpret_cast<const uint32_t *>(mem);
    for (unsigned i = 1; i < 0x78 / 4; ++i) {
        if (i == 0x3c / 4) continue;
        uint32_t v = w[i];
        if (i == 0x68 / 4) v &= 0xff;
        if (i == 0x40 / 4 || i == 0x6c / 4 || i == 0x70 / 4 || i == 0x74 / 4) v = drawfx::Norm(v);
        c.words.push_back(v);
    }
    drawfx::gLog.push_back(std::move(c));
}

void NoteRefs()
{
    drawfx::Note("refs", {uint32_t(gFormat.refs), uint32_t(gDeclPre.refs), uint32_t(gDeclNew[0].refs),
                          uint32_t(gDeclNew[1].refs), uint32_t(gDeclNew[2].refs), uint32_t(gDeclNew[3].refs)});
}

// What the render may change outside the object: the frames-drawn counter, nothing else.
void NoteWorld()
{
    // The description is a fresh block per side, so its address is left out of the hash.
    auto effect = std::make_unique<uint8_t[]>(sizeof gEffect);
    std::memcpy(effect.get(), &gEffect, sizeof gEffect);
    std::memset(effect.get() + offsetof(cTSParticlesEffect, mDescription), 0, 4);
    drawfx::Note("world", {uint32_t(gEffect.mFramesDrawn), Hash(effect.get(), sizeof gEffect),
                           Hash(reinterpret_cast<const uint8_t *>(gParticles), sizeof gParticles),
                           Hash(reinterpret_cast<const uint8_t *>(gNodes), sizeof gNodes)});
}

using CtorFn = void *(__fastcall *)(void *, void *, void *);
using DtorFn = void(__fastcall *)(void *, void *);
using DeletingFn = void *(__fastcall *)(void *, void *, uint32_t);
using SupportedFn = uint32_t(__fastcall *)(void *, void *, uint32_t);
using RenderFn = uint32_t(__fastcall *)(void *, void *, uint32_t, void *, uint32_t, uint32_t);
using BoundsFn = void(__fastcall *)(void *, void *, const float *);
using CurveFn = float(__fastcall *)(const void *, void *, float);
using RotateFn = void(__fastcall *)(const void *, void *, void *);
using Adapters = cTSParticlesGeometryAdapters;

uint8_t *NewBlock()
{
    uint8_t *const mem = static_cast<uint8_t *>(::operator new(sizeof(cTSParticlesGeometry)));
    std::memset(mem, 0xa5, sizeof(cTSParticlesGeometry));
    drawfx::AddRange(mem, sizeof(cTSParticlesGeometry), kObjectTag);
    return mem;
}

cTSParticlesGeometry *Construct(difftest::Context &ctx, bool original, uint8_t *mem)
{
    if (original) return static_cast<cTSParticlesGeometry *>(ctx.Original<CtorFn>(0xbb4e90)(mem, nullptr, &gEffect));
    return Adapters::Construct(reinterpret_cast<cTSParticlesGeometry *>(mem), nullptr, &gEffect);
}

void Destruct(difftest::Context &ctx, bool original, cTSParticlesGeometry *geometry)
{
    if (original)
        ctx.Original<DtorFn>(0xbb5490)(geometry, nullptr);
    else
        Adapters::Destruct(geometry, nullptr);
}

nGZGraphic4::cIVertexDeclarationDX *AsDeclaration(Fake &fake)
{
    return reinterpret_cast<nGZGraphic4::cIVertexDeclarationDX *>(&fake);
}

void RenderSides(difftest::Context &ctx, const char *what, unsigned seeds)
{
    sgfx::CheckSides(ctx, what, seeds, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cTSParticlesGeometry *const geometry = Construct(ctx, original, mem);
        switch (rng() % 3) {
        case 0: geometry->mDeclaration = nullptr; break;
        default: geometry->mDeclaration = AsDeclaration(gDeclPre); break;
        }
        const unsigned renders = 1 + rng() % 2;
        for (unsigned k = 0; k < renders; ++k) {
            static const uint32_t flags[] = {0, 1, 1, 0, 2, 0x80, 0xff, 0x100, 0xffffff00u};
            const uint32_t flag = Pick(rng, flags, 9);
            const uint32_t unused0 = rng(), unused3 = rng();
            const bool viaSlot = rng() % 2 != 0;
            char step[32];
            std::snprintf(step, sizeof step, "render %u", k);
            sgfx::MarkStep(ctx, step);
            uint32_t r;
            if (original) {
                r = ctx.Original<RenderFn>(0xbbdd50)(geometry, nullptr, unused0, &gContext, flag, unused3);
            } else if (viaSlot) {
                r = static_cast<cIGeometryRender *>(geometry)->Render(unused0, &gContext, uint8_t(flag), unused3);
            } else {
                r = geometry->cTSParticlesGeometry::Render(unused0, &gContext, uint8_t(flag), unused3);
            }
            drawfx::Note("render result", r);
            NoteObject("object", mem);
            NoteWorld();
            NoteRefs();
        }
        geometry->mDeclaration = nullptr;
        Destruct(ctx, original, geometry);
        ::operator delete(mem);
    });
    gDescription.reset();
}

} // namespace

DIFF_TEST(Particles_Construct, 0xbb4e90)
{
    sgfx::CheckSides(ctx, "construct", 64, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cTSParticlesGeometry *const geometry = Construct(ctx, original, mem);
        drawfx::Note("returned", drawfx::Norm(P(geometry)));
        NoteObject("object", mem);
        NoteRefs();
        if (rng() % 2) geometry->mDeclaration = AsDeclaration(gDeclPre);
        Destruct(ctx, original, geometry);
        NoteObject("destroyed", mem);
        NoteRefs();
        ::operator delete(mem);
    });
    gDescription.reset();
}

DIFF_TEST(Particles_Destroy, 0xbb5490)
{
    sgfx::CheckSides(ctx, "destroy", 64, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cTSParticlesGeometry *const geometry = Construct(ctx, original, mem);
        const unsigned shape = rng() % 4;
        if (shape & 1) geometry->mFormat = nullptr;
        if (shape & 2) geometry->mDeclaration = AsDeclaration(gDeclPre);
        Destruct(ctx, original, geometry);
        NoteObject("destroyed", mem);
        NoteRefs();
        ::operator delete(mem);
    });
    gDescription.reset();
}

DIFF_TEST(Particles_DeletingDestructor, 0xbb5890)
{
    sgfx::CheckSides(ctx, "deleting destructor", 32, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cTSParticlesGeometry *const geometry = Construct(ctx, original, mem);
        if (rng() % 2) geometry->mDeclaration = AsDeclaration(gDeclPre);
        // Flags without bit 0: the original's delete is the CRT's and not run; the bit-0 case is ours only (below).
        const uint32_t flags = (rng() % 4) & 2;
        void *const result = original ? ctx.Original<DeletingFn>(0xbb5890)(geometry, nullptr, flags)
                                      : VirtualCall<void *(__thiscall *)(void *, uint32_t), 9>(geometry, flags);
        drawfx::Note("returned", drawfx::Norm(P(result)));
        NoteObject("destroyed", mem);
        NoteRefs();
        ::operator delete(mem);
    });
    gDescription.reset();
}

DIFF_TEST(Particles_IsSupported, 0xbb9460)
{
    sgfx::CheckSides(ctx, "is supported", 200, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cTSParticlesGeometry *const geometry = Construct(ctx, original, mem);
        const unsigned calls = 1 + rng() % 4;
        for (unsigned k = 0; k < calls; ++k) {
            switch (rng() % 4) {
            case 0: break; // what the last call left
            case 1:
                if (!geometry->mDeclaration) {
                    gDeclPre.value[0] = 1;
                    geometry->mDeclaration = AsDeclaration(gDeclPre);
                }
                break;
            case 2:
                if (!geometry->mDeclaration) {
                    gDeclPre.value[0] = 0x100; // al clear, the rest set
                    ++gDeclPre.refs;
                    geometry->mDeclaration = AsDeclaration(gDeclPre);
                }
                break;
            default: gDecls.value[0] = rng() % 2; break;
            }
            const uint32_t what = rng();
            const uint8_t r = uint8_t(original ? ctx.Original<SupportedFn>(0xbb9460)(geometry, nullptr, what)
                                               : uint32_t(geometry->cTSParticlesGeometry::IsSupported(what)));
            drawfx::Note("supported", r);
            NoteObject("object", mem);
            NoteRefs();
        }
        geometry->mDeclaration = nullptr;
        Destruct(ctx, original, geometry);
        ::operator delete(mem);
    });
    gDescription.reset();
}

DIFF_TEST(Particles_Render, 0xbbdd50)
{
    {
        difftest::X87Precision precision(difftest::X87Pc::Bits53);
        RenderSides(ctx, "render at 53 bits", 1500);
    }
    {
        difftest::X87Precision precision(difftest::X87Pc::Bits24);
        RenderSides(ctx, "render at 24 bits", 1500);
    }
    {
        difftest::X87Precision precision(difftest::X87Pc::Bits53);
        gGentleBloom = true;
        RenderSides(ctx, "render, gentle bloom", 500);
        gGentleBloom = false;
    }
}

DIFF_TEST(Particles_SetBounds, 0xbc10f0)
{
    sgfx::CheckSides(ctx, "set bounds", 32, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cTSParticlesGeometry *const geometry = Construct(ctx, original, mem);
        float bounds[8];
        for (float &b : bounds) b = std::bit_cast<float>(uint32_t(rng()));
        if (original)
            ctx.Original<BoundsFn>(0xbc10f0)(geometry, nullptr, bounds);
        else
            geometry->SetBounds(bounds);
        NoteObject("object", mem);
        Destruct(ctx, original, geometry);
        ::operator delete(mem);
    });
    gDescription.reset();
}

DIFF_TEST(Particles_Slots, 0x3af660)
{
    static const uint32_t iids[] = {1, 0x89d39f12, 0, 2, 0x12345678, 0xffffffffu, 0x89d39f13, 0x89d39f11};
    sgfx::CheckSides(ctx, "slots", 64, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cTSParticlesGeometry *const geometry = Construct(ctx, original, mem);
        auto *const g = static_cast<cIGeometryRender *>(geometry);
        geometry->mRefs = 1 + rng() % 5;
        const unsigned ops = 20 + rng() % 30;
        for (unsigned k = 0; k < ops; ++k) {
            const unsigned op = rng() % 8;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof step, "step %u, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            switch (op) {
            case 0: {
                const uint32_t iid = Pick(rng, iids, 8);
                void *out = reinterpret_cast<void *>(uintptr_t(0x77777777));
                const bool r = g->QueryInterface(iid, rng() % 5 ? &out : nullptr);
                drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
                break;
            }
            case 1: drawfx::Note("add ref", g->AddRef()); break;
            case 2:
                if (geometry->mRefs >= 2) drawfx::Note("release", g->Release());
                break;
            case 3: {
                uint32_t *const flags = g->GetFlags();
                drawfx::Note("flags", {drawfx::Norm(P(flags)), *flags});
                if (rng() % 2) *flags = rng();
                break;
            }
            case 4: {
                uint32_t *const info = g->GetDrawInfo(rng());
                drawfx::Note("draw info", {drawfx::Norm(P(info)), info[0], info[1]});
                if (rng() % 2) info[rng() % 2] = rng();
                break;
            }
            case 5: {
                const uint32_t a = rng(), b = rng(), c = rng(), d = rng();
                drawfx::Note("prepare", g->Prepare(a, b, c, d));
            } break;
            case 6: g->Begin(rng(), &gContext, rng()); break;
            default: NoteObject("object", mem); break;
            }
        }
        NoteObject("object", mem);
        NoteRefs();
        Destruct(ctx, original, geometry);
        ::operator delete(mem);
    });
    gDescription.reset();
}

// Ours only (the original's last release calls the CRT's delete): the last Release answers 0, deletes the object and
// releases its format and declaration (0x153f50 calls the deleting destructor with 1 when the count reaches 0).
DIFF_TEST(Particles_ReleaseLast, 0x153f50)
{
    drawfx::Cells cells;
    for (uint32_t seed = 0; seed < 8; ++seed) {
        drawfx::ResetAll();
        Build(seed, cells);
        uint8_t *const mem = static_cast<uint8_t *>(::operator new(sizeof(cTSParticlesGeometry)));
        cTSParticlesGeometry *const geometry = Adapters::Construct(reinterpret_cast<cTSParticlesGeometry *>(mem), nullptr, &gEffect);
        const bool withDeclaration = seed % 2;
        if (withDeclaration) geometry->mDeclaration = AsDeclaration(gDeclPre);
        const int32_t formatRefs = gFormat.refs, declarationRefs = gDeclPre.refs;
        auto *const g = static_cast<cIGeometryRender *>(geometry);
        geometry->mRefs = 1;
        ++ctx.cases;
        const uint32_t r = g->Release();
        if (r != 0) ctx.Fail("release to zero answered %u, expected 0 (seed %u)", r, seed);
        if (gFormat.refs != formatRefs - 1) ctx.Fail("release to zero: format refs %d, expected %d (seed %u)", gFormat.refs, formatRefs - 1, seed);
        if (gDeclPre.refs != (withDeclaration ? declarationRefs - 1 : declarationRefs))
            ctx.Fail("release to zero: declaration refs %d, expected %d (seed %u)", gDeclPre.refs, withDeclaration ? declarationRefs - 1 : declarationRefs, seed);
        drawfx::ResetAll();
    }
    gDescription.reset();
}

DIFF_TEST(Particles_Curve, 0xbb5620)
{
    static const uint32_t precisions[] = {uint32_t(difftest::X87Pc::Bits53), uint32_t(difftest::X87Pc::Bits24)};
    for (const uint32_t pc : precisions) {
        difftest::X87Precision precision{difftest::X87Pc(pc)};
        sgfx::CheckSides(ctx, "curve", 400, [](uint32_t, drawfx::Cells &) { drawfx::ResetAll(); }, [&](bool original, std::mt19937 &rng) {
            Draw f{rng, rng() % 2 == 0};
            cTSEffectCurve curve;
            MakeCurve(curve, rng, f);
            for (unsigned k = 0; k < 24; ++k) {
                float t;
                const uint32_t count = uint32_t(curve.mValues.size());
                switch (rng() % 6) {
                case 0: t = float(rng() % 1025) / 1024.0f; break;
                case 1: t = float(rng() % count) / float(count > 1 ? count - 1 : 1); break; // on a key
                case 2: {                                                                  // a bit beside it
                    const float key = float(rng() % count) / float(count > 1 ? count - 1 : 1);
                    t = std::bit_cast<float>(std::bit_cast<uint32_t>(key) + (rng() % 5) - 2);
                    break;
                }
                case 3: t = Pick(rng, kWild, 20); break;
                case 4: t = rng() % 2 ? 1.0f : 0.0f; break;
                default: t = f(); break;
                }
                if (!(t >= 0.0f) && std::bit_cast<uint32_t>(t) == 0x80000001u) t = 0.0f;
                const float r = original ? ctx.Original<CurveFn>(0xbb5620)(&curve, nullptr, t) : Adapters::EvaluateCurve(&curve, nullptr, t);
                drawfx::Note("curve", {curve.mKind, count, std::bit_cast<uint32_t>(t), std::bit_cast<uint32_t>(r)});
            }
        });
    }
}

DIFF_TEST(Particles_Rotate, 0xbc0f80)
{
    sgfx::CheckSides(ctx, "rotate", 400, [](uint32_t, drawfx::Cells &) { drawfx::ResetAll(); }, [&](bool original, std::mt19937 &rng) {
        Draw f{rng, rng() % 2 == 0};
        fx::EffectsTransform transform;
        MakeTransform(transform, rng, f);
        fx::Vector3 vector{f(), f(), f()};
        const fx::Vector3 before = vector;
        if (original)
            ctx.Original<RotateFn>(0xbc0f80)(&transform, nullptr, &vector);
        else
            Adapters::Rotate(&transform, nullptr, &vector);
        drawfx::Note("rotate", {std::bit_cast<uint32_t>(before.x), std::bit_cast<uint32_t>(before.y), std::bit_cast<uint32_t>(before.z),
                                std::bit_cast<uint32_t>(vector.x), std::bit_cast<uint32_t>(vector.y), std::bit_cast<uint32_t>(vector.z)});
    });
}
