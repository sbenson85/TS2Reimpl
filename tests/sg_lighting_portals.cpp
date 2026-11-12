// Differential tests for nTSSG::cLightingManager's portal-light builders (lane 261109o, scenegraph LG4f;
// src/sg/cLightingManagerPortals.cpp): the record colours 0x2d6c10 (PortalColors), the lerp-light builder 0x2e0480
// (BuildPortalLights) and the direct one 0x2e1c30 (ApplyPortalLights). Hook shape: the original runs on the same object
// image as ours.
//
// The world is tests/sg_lighting_world.h's object and light sets (Build), with up to four sky samples whose portal
// records (+0x7c, over tests/sg_lighting_build.h's gRecords, 0 to 5 of them) are drawn here: floats with NaNs, zeros and
// negatives, the sample index 0..3 mostly and -1 / -2 / 4 / 5 / 0x7fff / 0x8000 now and then, the direct-scale byte
// zero or not, mObject a fake node, mOther one or null. Each sample's lerp set (+0x88) is a pool fake or null (0x2e0480
// then makes one: operator new and the constructor 0x3c0ec0, a stand-in on both sides). The manager's switches (sky
// +0x45, debug +0x47, shaded colours +0x1e8), scales (+0x54, the jitter +0x74, the shade threshold +0x1e0) and render
// groups (+0x50c) are drawn, and so are the light sets' enabled bits 7 / 11, saturation (+0x1b4, below, at and above
// 1, NaN) and modifiers. The game's zero vector (0x10d9424) and grey (0xffd994) get other values now and then
// (restored after each test).
//
// Fakes, each logging its arguments: the nodes (slots 7 SetAttribute, 10 GetAttribute (writes the scalars' values, the
// colour or leaves it as it came), 24 FindExtension (a drawn answer; now and then it writes the extension and says no),
// 54 GetWorldTransform, 92 DropLightCopy, 95 GetLight (a light or null)), the lights (slot 35, a standard light or
// null), the standard lights (41-44), the diffusion extensions (slots 0, 3, 4, 6, 23, 24, 26; a virtual base whose slot
// 2 logs the release; SetSourceIntensity changes the colour GetSourceColor hands out, so a read after it shows), the
// lit object (+0x18, slot 24: a colour for the segment, returned in the buffer or now and then in another), and the
// lerp sets (refcount slots 1 / 2). Stand-ins on both sides: RandomDoubleRangeUniform 0x1b8c48 (drawn answers, NaN and
// overflow among them), ShadePortals 0x2d71f0 (writes drawn shades), EvaluateAmbientScaled 0x2d66f0 (a drawn colour),
// cRZString::Sprintf 0x1ad6f7, the lerp set's constructor, SetKeys, ClearActive, FadeOutUnused, AddExisting and
// AddPoint, and in 0x2e0480's test PortalColors 0x2d6c10 (logs what it is given, writes a drawn part of its outputs: the
// builder keeps them from one record to the next). 0x3a97d0 (the sample key) is the game's on both sides.
//
// Compared: the log, the object, the light sets, the sky samples (the lerp sets made as their order of making), the
// records, the extensions' colours, 0x2d6c10's outputs, 0x2e0480's light vector (size, capacity, elements less their
// padding) and the heap blocks each side made; at the x87's 53-bit and 24-bit precision (the facing product is x87).
#include "sg_lighting_build.h"
#include "sg_lighting_fakes.h"

#include "free_recorder.h"
#include "stack_out.h"

#include "rz/cRZRandom.h"
#include "rz/cRZString.h"

#include <cstdarg>

namespace {

using nTSSG::cPortalLight;

constexpr uint32_t kZeroVector = 0x10d9424, kGrey = 0xffd994, kRandom = 0x10d93f0;
constexpr uint32_t kScalarEvent = 0xdb07f4, kVectorEvent = 0xdb5e70;
constexpr uint32_t kAttrOne = 0xe9d24d51, kAttrColor = 0x29de3049, kAttrValue = 0x49e37821;

// --- the fakes ---------------------------------------------------------------------------------------------------

struct FakeStd {
    void **vt;
    uint32_t id;
};
struct FakeLight {
    void **vt;
    uint32_t id;
    FakeStd *standard;
};
struct FakeExt {
    void **vt;
    const int32_t *vbptr; // +4: the virtual base at +0xc
    uint32_t id;
    void **baseVt;        // +0xc
    float intensity, color[3], falloff;
};
struct FakeNode {
    void **vt;
    uint32_t id;
    FakeLight *light;
    uint32_t find;   // bit 0: FindExtension's answer; bit 1: it writes the extension even when it says no
    FakeExt *ext;
    uint32_t attrs;  // bit 1: GetAttribute writes the colour (else leaves it); bits 0 / 2 pick the scalars' answers
    float transform[7], one, color[3], value;
};
struct FakeLit {
    void **vt;
};

constexpr int kNodes = 10, kLights = 6, kStds = 4, kExts = 6;
FakeNode gNodes[kNodes];
FakeLight gLights[kLights];
FakeStd gStds[kStds];
FakeExt gExts[kExts];
FakeLit gLit;
void *gNodeVt[100], *gLightVt[40], *gStdVt[48], *gExtVt[32], *gExtBaseVt[4], *gLitVt[28];
const int32_t kExtVbTable[2] = {-4, 8};

uint32_t PoolId(const void *p)
{
    if (!p) return 0;
    for (const auto &o : gNodes) if (p == &o) return o.id;
    for (const auto &o : gLights) if (p == &o) return o.id;
    for (const auto &o : gStds) if (p == &o) return o.id;
    for (const auto &o : gExts) if (p == &o) return o.id;
    return 0xbad00000u | (P(p) & 0xfff);
}

uint32_t EventKind(const void *event)
{
    const uint32_t vt = *static_cast<const uint32_t *>(event);
    if (vt == P(GameAddress<const void>(kScalarEvent))) return 1;
    if (vt == P(GameAddress<const void>(kVectorEvent))) return 2;
    return 0xbad;
}

void __fastcall NodeSetAttribute(FakeNode *self, void *, uint32_t id, uint32_t a, uint32_t event)
{
    const auto *e = reinterpret_cast<const uint32_t *>(uintptr_t(event));
    L(0xa07), L(self->id), L(id), L(a), L(EventKind(e)), L(e[1]);
    if (id == kAttrColor) L(e[2]), L(e[3]);
}
void __fastcall NodeGetAttribute(FakeNode *self, void *, uint32_t event, uint32_t id, uint32_t a)
{
    auto *e = reinterpret_cast<uint32_t *>(uintptr_t(event));
    // 0x2e1c30 leaves the scalar events' values unset (only the vector's are zeroed): the node always writes those, and
    // only the vector's incoming values are logged.
    L(0xa0a), L(self->id), L(id), L(a), L(EventKind(e));
    if (id == kAttrColor) L(e[1]), L(e[2]), L(e[3]);
    switch (id) {
    case kAttrOne:
        e[1] = (self->attrs & 1) ? U(self->one) : U(self->value) ^ 0x80000000u;
        difftest::FillOut(e, 8);
        break;
    case kAttrColor:
        if (self->attrs & 2) e[1] = U(self->color[0]), e[2] = U(self->color[1]), e[3] = U(self->color[2]);
        difftest::FillOut(e, 16);
        break;
    case kAttrValue:
        e[1] = (self->attrs & 4) ? U(self->value) : U(self->one) ^ 0x80000000u;
        difftest::FillOut(e, 8);
        break;
    default: break;
    }
}
uint32_t gFindJunk;
uint32_t __fastcall NodeFind(FakeNode *self, void *, uint32_t iid, void **out, void *start)
{
    L(0xa18), L(self->id), L(iid), L(PoolId(*out)), L(PoolId(start));
    if (self->find & 3) *out = self->ext;
    return (gFindJunk & ~0xffu) | (self->find & 1);
}
uint32_t *__fastcall NodeTransform(FakeNode *self, void *, uint32_t *out)
{
    L(0xa36), L(self->id);
    std::memcpy(out, self->transform, 0x1c);
    difftest::FillOut(out, 0x1c);
    return out;
}
void __fastcall NodeDrop(FakeNode *self, void *) { L(0xa5c), L(self->id); }
FakeLight *__fastcall NodeGetLight(FakeNode *self, void *)
{
    L(0xa5f), L(self->id);
    return self->light;
}

FakeStd *__fastcall LightStandard(FakeLight *self, void *)
{
    L(0xb23), L(self->id);
    return self->standard;
}
void __fastcall StdColor(FakeStd *self, void *, const float *rgb) { L(0xc29), L(self->id), LF(rgb, 3); }
void __fastcall StdIntensity(FakeStd *self, void *, float v) { L(0xc2a), L(self->id), L(U(v)); }
void __fastcall StdFactor(FakeStd *self, void *, float v) { L(0xc2b), L(self->id), L(U(v)); }
void __fastcall StdFalloff(FakeStd *self, void *, float v) { L(0xc2c), L(self->id), L(U(v)); }

void __fastcall ExtReset(FakeExt *self, void *) { L(0xd00), L(self->id); }
void __fastcall ExtSetIntensity(FakeExt *self, void *, float v)
{
    L(0xd03), L(self->id), L(U(v));
    self->color[0] = Fl(Mix(self->id, U(v)) | 0x40000000u); // GetSourceColor's answer changes from here
}
void __fastcall ExtSetColor(FakeExt *self, void *, const float *rgb) { L(0xd04), L(self->id), LF(rgb, 3); }
void __fastcall ExtSetFalloff(FakeExt *self, void *, float v) { L(0xd06), L(self->id), L(U(v)); }
float __fastcall ExtIntensity(FakeExt *self, void *)
{
    L(0xd17), L(self->id);
    return self->intensity;
}
const float *__fastcall ExtColor(FakeExt *self, void *)
{
    L(0xd18), L(self->id);
    return self->color;
}
float __fastcall ExtFalloff(FakeExt *self, void *)
{
    L(0xd1a), L(self->id);
    return self->falloff;
}
void __fastcall ExtBaseRelease(uint8_t *base, void *) { L(0xd82), L(reinterpret_cast<FakeExt *>(base - 0xc)->id); }

uint32_t gLitSalt, gLitCalls, gLitElsewhere;
alignas(16) float gLitOther[4];
float *__fastcall LitColor(FakeLit *, void *, float *out, const float *segment)
{
    L(0xe18), LF(segment, 6);
    const uint32_t n = gLitCalls++;
    // The buffer is always written (the game's callee fills it; 0x2d6c10's lit branch reads it, not the return); some
    // calls return another colour, so the readers of the buffer and of the return differ.
    const bool elsewhere = gLitElsewhere >> (n % 32) & 1;
    for (int c = 0; c < 3; ++c) {
        const uint32_t h = Mix(gLitSalt + n, c), g = Mix(gLitSalt + n, c + 7);
        out[c] = h % 11 == 0 ? Fl(0x7fc00000u | (h & 0x803fffffu)) : h % 13 == 0 ? 0.0f : float(h & 0xffff) / 16384.0f;
        gLitOther[c] = g % 11 == 0 ? Fl(0x7fc00000u | (g & 0x803fffffu)) : float(g & 0xffff) / 8192.0f;
    }
    difftest::FillOut(out, 12);
    return elsewhere ? gLitOther : out;
}

// The lerp sets: the pool (tests/sg_lighting_build.h's gLerps) and the ones 0x2e0480 makes, in their order (the shared
// lerp fakes', tests/sg_lighting_fakes.h). The shared fakes' ids: the lerp sets', else the pools'.
uint32_t LerpRef(const void *p)
{
    if (!p) return 0;
    if (P(p) >= P(gLerps) && P(p) < P(gLerps) + sizeof(gLerps)) return LerpId(p);
    for (uint32_t i = 0; i < lightfx::gNewLerpCount; ++i)
        if (p == lightfx::gNewLerps[i]) return 0x100 + i;
    return PoolId(p);
}

uint32_t gRandomSalt, gRandomCalls;
double __fastcall StandRandom(cRZRandom *self, void *, double min, double max)
{
    uint64_t a, b;
    std::memcpy(&a, &min, 8), std::memcpy(&b, &max, 8);
    L(0x1b8c), L(P(self) == P(GameAddress<void>(kRandom))), L(uint32_t(a)), L(uint32_t(a >> 32)), L(uint32_t(b)), L(uint32_t(b >> 32));
    const uint32_t h = Mix(gRandomSalt, gRandomCalls++);
    switch (h % 16) {
    case 0: return double(Fl(0x7fc00000u | (h & 0x803fffffu)));
    case 1: return 1e300; // beyond float: infinity
    case 2: return 0.0;
    case 3: return -0.0;
    default: return 0.5 + double(h >> 8) / double(1u << 24) + double(h & 0xff) * 0x1p-40;
    }
}

uint32_t gShadeSalt, gShadeCalls;
void __fastcall StandShade(cLightingManager *self, void *, std::vector<cPortalRecord> *records)
{
    auto *const words = reinterpret_cast<uint32_t *>(records);
    const uint32_t n = (words[1] - words[0]) / sizeof(cPortalRecord);
    L(0x71f0), L(P(self) == P(gObject)), L(P(records) - P(gSky)), L(n);
    auto *const r = reinterpret_cast<cPortalRecord *>(uintptr_t(words[0]));
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t h = Mix(gShadeSalt + gShadeCalls, i);
        if (h % 4 == 0) continue;
        r[i].mShade = h % 17 == 0 ? Fl(0x7fc00000u | (h & 0x803fffffu)) : float(h & 0xfff) / 2048.0f;
    }
    ++gShadeCalls;
}

uint32_t gAmbientSalt, gAmbientCalls;
void __fastcall StandAmbient(cLightingManager *self, void *, const float *normal, float *out)
{
    L(0x66f0), L(P(self) == P(gObject)), LF(normal, 3), LF(out, 3);
    for (int c = 0; c < 3; ++c) {
        const uint32_t h = Mix(gAmbientSalt + gAmbientCalls, c);
        out[c] = h % 9 == 0 ? Fl(0x7fc00000u | (h & 0x803fffffu)) : float(h & 0xffff) / 32768.0f;
    }
    ++gAmbientCalls;
    difftest::FillOut(out, 12);
}

// 0x2e0480's PortalColors: logs its inputs (what the builder carries from the record before), writes a drawn part. The
// weights are always written and not logged: 0x2e0480 leaves them unset before the first record (as the game's
// PortalColors always writes them).
uint32_t gColorsSalt, gColorsCalls;
void __fastcall StandColors(cLightingManager *self, void *, int32_t sample, cPortalRecord *record, float *df, float *dc,
                            float *dw, float *sf, float *sc, float *sw)
{
    L(0x6c10), L(P(self) == P(gObject)), L(uint32_t(sample)), L(P(record) - P(gRecords));
    LF(df, 4), LF(dc, 3), LF(sf, 4), LF(sc, 3);
    const uint32_t n = gColorsCalls++;
    std::mt19937 gen(Mix(gColorsSalt, n));
    const uint32_t mask = (gen() % 4 ? ~0u : gen()) | 1u << 7 | 1u << 15;
    float *const outs[] = {df, df + 1, df + 2, df + 3, dc, dc + 1, dc + 2, dw, sf, sf + 1, sf + 2, sf + 3, sc, sc + 1, sc + 2, sw};
    for (unsigned i = 0; i < std::size(outs); ++i)
        if (mask >> i & 1) *outs[i] = AnyFloat(gen, 2.0f);
}

void FillPortalFakes()
{
    gNodeVt[7] = reinterpret_cast<void *>(&NodeSetAttribute);
    gNodeVt[10] = reinterpret_cast<void *>(&NodeGetAttribute);
    gNodeVt[24] = reinterpret_cast<void *>(&NodeFind);
    gNodeVt[54] = reinterpret_cast<void *>(&NodeTransform);
    gNodeVt[92] = reinterpret_cast<void *>(&NodeDrop);
    gNodeVt[95] = reinterpret_cast<void *>(&NodeGetLight);
    gLightVt[35] = reinterpret_cast<void *>(&LightStandard);
    gStdVt[41] = reinterpret_cast<void *>(&StdColor);
    gStdVt[42] = reinterpret_cast<void *>(&StdIntensity);
    gStdVt[43] = reinterpret_cast<void *>(&StdFactor);
    gStdVt[44] = reinterpret_cast<void *>(&StdFalloff);
    gExtVt[0] = reinterpret_cast<void *>(&ExtReset);
    gExtVt[3] = reinterpret_cast<void *>(&ExtSetIntensity);
    gExtVt[4] = reinterpret_cast<void *>(&ExtSetColor);
    gExtVt[6] = reinterpret_cast<void *>(&ExtSetFalloff);
    gExtVt[23] = reinterpret_cast<void *>(&ExtIntensity);
    gExtVt[24] = reinterpret_cast<void *>(&ExtColor);
    gExtVt[26] = reinterpret_cast<void *>(&ExtFalloff);
    gExtBaseVt[2] = reinterpret_cast<void *>(&ExtBaseRelease);
    gLitVt[24] = reinterpret_cast<void *>(&LitColor);
    lightfx::FillShared();
    lightfx::gIdOf = &LerpRef;
    gLit.vt = gLitVt;
}

// The game's zero vector and grey: drawn per case, restored after the test.
struct Globals : SavedGlobals {
    Globals() { Save(kZeroVector, 12), Save(kGrey, 12); }
};

struct Stands {
    shape::StandIn random{0x1b8c48, &cRZRandom::RandomDoubleRangeUniform, &StandRandom};
    shape::StandIn shade{0x2d71f0, &cLightingManager::ShadePortals, &StandShade};
    shape::StandIn ambient{0x2d66f0, &cLightingManager::EvaluateAmbientScaled, &StandAmbient};
    shape::StandIn sprintf{0x1ad6f7, &cRZString::Sprintf, &lightfx::StandSprintf};
    shape::StandIn construct{0x3c0ec0, &cLerpLightSet::Construct, &lightfx::LerpConstruct};
    shape::StandIn setKeys{0x3c2e30, &cLerpLightSet::SetKeys, &lightfx::LerpSetKeys};
    shape::StandIn clearActive{0x3c2570, &cLerpLightSet::ClearActive, &lightfx::LerpClearActive};
    shape::StandIn fadeOut{0x3c25d0, &cLerpLightSet::FadeOutUnused, &lightfx::LerpFadeOut};
    shape::StandIn addExisting{0x3c1a80, &cLerpLightSet::AddExisting, &lightfx::LerpAddExisting};
    shape::StandIn addPoint{0x3c1d60, &cLerpLightSet::AddPoint, &lightfx::LerpAddPoint};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    Globals globals;
    Stands stands;
    Scope()
    {
        FillPortalFakes();
        gLog.reserve(1 << 16);
    }
};

// --- the case --------------------------------------------------------------------------------------------------------

struct LightVector {
    uint8_t *first, *last, *end;
} gLightVector;

alignas(16) float gOut[16]; // 0x2d6c10's outputs: direct factors 4, colour 3, weight, sky factors 4, colour 3, weight
uint32_t gSampleArg, gRecordArg;

float Pick(std::mt19937 &gen, std::initializer_list<float> special, float scale)
{
    if (gen() % 3 == 0) return special.begin()[gen() % special.size()];
    return AnyFloat(gen, scale);
}

uint8_t AnyByte(std::mt19937 &gen) { return gen() % 2 ? 0 : uint8_t(1 + gen() % 255); }

// mode 0: 0x2d6c10, 1: 0x2e0480, 2: 0x2e1c30.
void SetUp(uint32_t seed, int mode)
{
    std::mt19937 gen(seed * 7919u + 0x2e0480u + uint32_t(mode) * 0x10000u);
    Build(gen());
    const float inf = Fl(0x7f800000u);
    // the fakes
    for (int i = 0; i < kStds; ++i) gStds[i] = {gStdVt, 0x40u + i};
    for (int i = 0; i < kLights; ++i) gLights[i] = {gLightVt, 0x30u + i, gen() % 4 ? &gStds[gen() % kStds] : nullptr};
    for (int i = 0; i < kExts; ++i) {
        FakeExt &e = gExts[i];
        e = {gExtVt, kExtVbTable, 0x50u + i, gExtBaseVt, AnyFloat(gen, 2.0f), {AnyFloat(gen, 2.0f), AnyFloat(gen, 2.0f), AnyFloat(gen, 2.0f)}, AnyFloat(gen, 2.0f)};
    }
    for (int i = 0; i < kNodes; ++i) {
        FakeNode &n = gNodes[i];
        n.vt = gNodeVt, n.id = 0x20u + i;
        n.light = gen() % 3 ? &gLights[gen() % kLights] : nullptr;
        n.find = gen() % 8 ? gen() % 2 : 2;
        n.ext = &gExts[gen() % kExts];
        n.attrs = gen() % 4 ? 7 : gen() % 8;
        for (float &f : n.transform) f = AnyFloat(gen, 4.0f);
        n.one = AnyFloat(gen, 2.0f), n.value = AnyFloat(gen, 2.0f);
        for (float &f : n.color) f = AnyFloat(gen, 2.0f);
    }
    gFindJunk = gen();
    for (auto &l : gLerps) {
        const uint32_t vt = P(lightfx::gLerpVT);
        std::memcpy(l, &vt, 4);
    }
    lightfx::gNewLerpCount = 0;
    gLitSalt = gen(), gLitCalls = 0, gLitElsewhere = gen() % 3 ? 0 : gen();
    for (float &f : gLitOther) f = AnyFloat(gen, 1.0f);
    gRandomSalt = gen(), gRandomCalls = 0;
    gShadeSalt = gen(), gShadeCalls = 0;
    gAmbientSalt = gen(), gAmbientCalls = 0;
    gColorsSalt = gen(), gColorsCalls = 0;
    // the light sets (and the default one)
    cLightSet *const sets[] = {&gSets[0], &gSets[1], &gSets[2], reinterpret_cast<cLightSet *>(gObject + 0x2d0)};
    for (cLightSet *set : sets) {
        auto *const bytes = reinterpret_cast<uint8_t *>(set);
        uint32_t enabled = gen();
        if (gen() % 2) enabled &= ~0x800u;
        std::memcpy(bytes + 0x20, &enabled, 4);
        const float saturation = Pick(gen, {0.5f, 1.0f, 0.0f, 2.0f, 0.99999994f, -1.0f, inf, NaN(gen)}, 1.0f);
        std::memcpy(bytes + 0x1b4, &saturation, 4);
        for (uint32_t at = 0xf8; at < 0x158; at += 4) {
            const float f = gen() % 6 ? Nice(gen, 2.0f) : AnyFloat(gen, 2.0f);
            std::memcpy(bytes + at, &f, 4);
        }
        if (gen() % 4 == 0) { // light 0's colour, intensity and direction with NaNs (one, a colour and the intensity, or
                              // more) or an infinite direction (the facing weight infinite, NaN once drawn towards 1)
            const int roll = int(gen() % 4);
            if (roll == 3) {
                const float big = gen() % 2 ? inf : -inf;
                std::memcpy(bytes + 0x24 + 0x10 + 4 * (gen() % 2), &big, 4);
            }
            for (int k = 0; k < (roll == 2 ? 3 : 1); ++k) {
                float nan = NaN(gen);
                std::memcpy(bytes + 0x24 + 4 * (gen() % 7), &nan, 4);
            }
            if (roll == 1) {
                float nan = NaN(gen), nan2 = NaN(gen);
                std::memcpy(bytes + 0x24 + 4 * (gen() % 3), &nan, 4);
                std::memcpy(bytes + 0x24 + 0xc, &nan2, 4);
            }
        }
    }
    // the manager
    Put(0x18, P(&gLit));
    gObject[0x45] = gen() % 4 ? AnyByte(gen) | 1 : 0;
    gObject[0x47] = gen() % 4 ? 0 : AnyByte(gen);
    gObject[0x1e8] = AnyByte(gen);
    PutF(0x54, Pick(gen, {1.0f, 0.0f, 2.0f, NaN(gen)}, 2.0f));
    PutF(0x74, Pick(gen, {0.0f, 0.1f, 0.5f, 1.0f, inf}, 1.0f));
    PutF(0x1e0, Pick(gen, {0.0f, -0.0f, 0.5f, -1.0f, 1.0f}, 1.0f));
    if (gen() % 4 == 0) Put(0x50c + 4 * (gen() % 3), 0);
    // the samples' records and lerp sets
    static const int16_t kIndexes[] = {-1, 0, 1, 2, 3, 4, 5, -2, 0x7fff, int16_t(0x8000)};
    for (int s = 0; s < 4; ++s) {
        uint8_t *const sample = reinterpret_cast<uint8_t *>(gSky[s]);
        for (auto &record : gRecords[s]) {
            for (uint32_t i = 0; i < sizeof(cPortalRecord); i += 4) {
                const float f = AnyFloat(gen, 4.0f);
                std::memcpy(record + i, &f, 4);
            }
            auto *const r = reinterpret_cast<cPortalRecord *>(record);
            const uint32_t object = P(&gNodes[gen() % kNodes]), other = gen() % 3 ? P(&gNodes[gen() % kNodes]) : 0;
            std::memcpy(record, &object, 4), std::memcpy(record + 4, &other, 4);
            r->mSample = gen() % 4 ? int16_t(gen() % 4) : kIndexes[gen() % std::size(kIndexes)];
            record[0x3c] = AnyByte(gen);
            if (gen() % 2) { // a unit normal
                const float angle = float(gen() % 16) * 0.39269908f;
                r->mNormal[0] = std::cos(angle), r->mNormal[1] = std::sin(angle);
            }
            if (gen() % 4 == 0) for (float &c : r->mLight.mColor) c = 0.0f; // nothing appended (unless the zero vector moved)
        }
        PutVector(sample + 0x7c, gRecords[s], gen() % 4 ? gen() % (kRecords + 1) : 0, sizeof(cPortalRecord), kRecords);
        const uint32_t lerp = gen() % 3 ? P(gLerps[gen() % kLerps]) : 0;
        std::memcpy(sample + 0x88, &lerp, 4);
    }
    // the game's zero vector and grey
    static const float kZero[3] = {0.0f, 0.0f, 0.0f};
    float zero[3], grey[3] = {0.5f, 0.5f, 0.5f};
    std::memcpy(zero, kZero, 12);
    if (gen() % 6 == 0) for (float &f : zero) f = gen() % 2 ? 0.0f : AnyFloat(gen, 1.0f);
    if (gen() % 3 == 0) for (float &f : grey) f = AnyFloat(gen, 1.0f);
    std::memcpy(GameAddress<void>(kZeroVector), zero, 12);
    std::memcpy(GameAddress<void>(kGrey), grey, 12);
    // the outputs and arguments
    if (mode == 0) {
        for (float &f : gOut) f = AnyFloat(gen, 4.0f);
        const uint32_t s = gen() % 4 ? gen() % 3 : gen() % 6;
        gSampleArg = gen() % 4 ? s : s | (gen() & 0xffff0000u);
        gRecordArg = gen() % (4 * kRecords);
        if (gen() % 8 == 0) { // the direct weight NaN (an infinite facing drawn towards 1 at saturation 0), then scaled by
                              // a NaN +0x54: the one way both operands of that product are NaN
            for (cLightSet *set : sets) {
                auto *const bytes = reinterpret_cast<uint8_t *>(set);
                bytes[0x20] |= 0x80; // skylight
                const float zero = 0.0f, direction[3] = {inf, 0.0f, 0.0f};
                std::memcpy(bytes + 0x1b4, &zero, 4);
                std::memcpy(bytes + 0x24 + 0x10, direction, 12);
            }
            auto *const r = reinterpret_cast<cPortalRecord *>(gRecords[0][0] + gRecordArg * sizeof(cPortalRecord));
            r->mNormal[0] = -1.0f, r->mNormal[1] = 0.0f, r->mSample = 1;
            reinterpret_cast<uint8_t *>(r)[0x3c] = 1;
            gObject[0x45] = 1;
            PutF(0x54, NaN(gen));
            gSampleArg = 0;
        }
    }
    if (mode == 1) {
        static const uint32_t kCaps[] = {0, 1, 2, 3, 6, 12, 40};
        const uint32_t cap = kCaps[gen() % std::size(kCaps)];
        const uint32_t n = cap ? gen() % (cap + 1) : 0;
        // through the STL's allocator: blocks of 4 KB and more carry its alignment header, which the vectors' frees check
        uint8_t *const buffer = cap ? reinterpret_cast<uint8_t *>(std::allocator<cPortalLight>().allocate(cap)) : nullptr;
        for (uint32_t i = 0; i < cap * sizeof(cPortalLight); ++i) buffer[i] = uint8_t(gen());
        gLightVector = {buffer, buffer + n * sizeof(cPortalLight), buffer + cap * sizeof(cPortalLight)};
    }
}

Blob Snapshot(int mode, long long blocks, long long bytes)
{
    Blob o = End(nullptr, 0);
    o.push_back(0xeeef);
    for (auto &e : gSky) {
        for (size_t i = 0; i < std::size(e); ++i) {
            uint32_t w = U(e[i]);
            if (i == 0x88 / 4) w = LerpRef(reinterpret_cast<void *>(uintptr_t(w)));
            o.push_back(w);
        }
    }
    const auto *r = reinterpret_cast<const uint32_t *>(gRecords);
    for (size_t i = 0; i < sizeof(gRecords) / 4; ++i) o.push_back(r[i]);
    for (const FakeExt &e : gExts) o.push_back(U(e.color[0]));
    o.push_back(lightfx::gNewLerpCount), o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    if (mode == 0) for (float f : gOut) o.push_back(U(f));
    if (mode == 1) {
        const uint32_t n = uint32_t(gLightVector.last - gLightVector.first) / sizeof(cPortalLight);
        o.push_back(n), o.push_back(uint32_t(gLightVector.end - gLightVector.first) / sizeof(cPortalLight));
        for (uint32_t i = 0; i < n; ++i) {
            uint8_t e[sizeof(cPortalLight)];
            std::memcpy(e, gLightVector.first + i * sizeof(cPortalLight), sizeof e);
            // the padding: the game's copy (0x2ca640) copies field by field, not these
            std::memset(e + offsetof(cPortalLight, mPad25), 0, 3);
            std::memset(e + offsetof(cPortalLight, mPad6A), 0, 2);
            for (uint32_t k = 0; k < sizeof e; k += 4) {
                uint32_t w;
                std::memcpy(&w, e + k, 4);
                o.push_back(w);
            }
        }
    }
    return o;
}

void CleanUp(int mode)
{
    if (mode == 1 && gLightVector.first) {
        std::allocator<cPortalLight>().deallocate(reinterpret_cast<cPortalLight *>(gLightVector.first),
                                                  size_t(gLightVector.end - gLightVector.first) / sizeof(cPortalLight));
    }
    if (mode == 1) gLightVector = {};
    for (uint32_t i = 0; i < lightfx::gNewLerpCount; ++i) ::operator delete(lightfx::gNewLerps[i]);
    lightfx::gNewLerpCount = 0;
}

template <class Call> void Run(difftest::Context &ctx, const char *what, uint32_t cases, int mode, Call call)
{
    AtBothPrecisions(ctx, what, cases, [&](uint32_t seed, const char *label) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            SetUp(seed, mode);
            gLog.clear();
            ctx.Mark(side ? "ours" : "original");
            difftest::AllocationCounter heap;
            call(side);
            heap.Stop();
            outcome[side] = Snapshot(mode, heap.Blocks(), heap.Bytes());
            CleanUp(mode);
        }
        ++ctx.cases;
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

float *Out(int i) { return gOut + i; }
cPortalRecord *RecordArg() { return reinterpret_cast<cPortalRecord *>(gRecords[0][0] + gRecordArg * sizeof(cPortalRecord)); }

} // namespace

DIFF_TEST(sg_lighting_portals_colors, 0x2d6c10)
{
    Scope scope;
    using Fn = void(__thiscall *)(void *, uint32_t, void *, float *, float *, float *, float *, float *, float *);
    auto original = ctx.Original<Fn>(0x2d6c10);
    Run(ctx, "portal colours", 6000, 0, [&](int side) {
        if (side == 0) original(gObject, gSampleArg, RecordArg(), Out(0), Out(4), Out(7), Out(8), Out(12), Out(15));
        else Ours()->PortalColors(int32_t(gSampleArg), RecordArg(), Out(0), Out(4), Out(7), Out(8), Out(12), Out(15));
    });
}

DIFF_TEST(sg_lighting_portals_build, 0x2e0480)
{
    Scope scope;
    shape::StandIn colors{0x2d6c10, &cLightingManager::PortalColors, &StandColors};
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x2e0480);
    Run(ctx, "portal lights", 3000, 1, [&](int side) {
        if (side == 0) original(gObject, &gLightVector);
        else Ours()->BuildPortalLights(reinterpret_cast<std::vector<cPortalLight> *>(&gLightVector));
    });
}

DIFF_TEST(sg_lighting_portals_apply, 0x2e1c30)
{
    Scope scope;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2e1c30);
    Run(ctx, "apply portals", 4000, 2, [&](int side) {
        if (side == 0) original(gObject);
        else Ours()->ApplyPortalLights();
    });
}
