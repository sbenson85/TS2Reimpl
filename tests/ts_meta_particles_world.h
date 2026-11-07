#pragma once
// The meta-particles effect's test world (rendering rows E7..E9): the effects world's owner and its slots, the effect fakes the
// particles and the spawned list hold, the surface and the colliders, random descriptions, the effect's whole-object fingerprint
// and the particle pool. Every test of cTSMetaParticlesEffect's code includes this and adds its own operations (the slots test is
// tests/ts_meta_particles_effect.cpp; its Step shows how a helper is called on both sides).
// Everything is in the including file's anonymous namespace: each test file has its own copy of the globals.

#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "rz/RZMemory.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "shape.h"
#include "ts/cTSMetaParticlesEffect.h"
#include "ts/cTSParticlesDescription.h"

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace {

using namespace fxtest;
using fx::EffectsTransform;
using fx::Vector3;
using Effect = cTSMetaParticlesEffect;
using Description = cTSMetaParticlesDescription;
using Particle = cTSMetaParticle;

constexpr uint32_t kGameTables[2] = {0xe91284, 0xe912c0};
constexpr uint32_t kGameBaseTables[2] = {0xe8bd68, 0xda2fa8};
constexpr uint32_t kCreate = 0xc03910, kConstruct = 0xbfd4f0;
constexpr uint32_t kPoolElementCell = 0x107eef0, kPoolBlockCell = 0x107eef4, kPoolListCell = 0x107eef8, kPoolFreeCell = 0x107eefc;
constexpr uint32_t kRandomCell = 0x10fd138;
constexpr uint32_t kSprintf = 0x1ad6f7, kMalloc = 0x1be3b0;

using CreateFn = void *(__cdecl *)(const void *);
using ConstructFn = void *(__thiscall *)(void *, const void *);

enum : uint32_t {
    evSettings = 0xc4000000,
    evSource = 0xc5000000,
    evSurface = 0xc6000000,   // | slot
    evCollider = 0xc7000000,  // | slot
    evMake = 0xc9000000,
    evEffect = 0xca000000,    // | slot; the fake's id, then the arguments
    evQuery = 0xcb000000,
    evDescription = 0xcc000000,
    evText = 0xcd000000,
    evKnows = 0xce000000,
    evHold = 0xcf000000,
    evAlloc = 0xd0000000,
    evWorld = 0xd3000000,     // the owner's world object: | slot
};

difftest::Context *gCtx = nullptr;

// ---- this run's world (each side's run sets it up alike) ----

constexpr uint32_t kArenaBytes = 0x200000;
alignas(16) uint8_t gArena[kArenaBytes];
uint32_t gArenaUsed = 0;
uint32_t ArenaOffset(const void *p) { return uint32_t(static_cast<const uint8_t *>(p) - gArena); }

cTSParticlesSettings gSettings;
FakeNode gSource, gSurface, gDescriptionFace, gColliders[4], gEffectFakes[6];
void *gSourceVT[kFakeNodeSlots], *gSurfaceVT[kFakeNodeSlots], *gColliderVT[kFakeNodeSlots], *gEffectVT[kFakeNodeSlots],
    *gDescriptionVT[kFakeNodeSlots];
std::mt19937 gAnswers; // what the fakes answer: the same on both sides while the two make the same calls
float gSurfaceWords[5];
bool gHaveSurface;
Description *gDescriptions[3];
Description *gNextDescription;
const char *gScratchBase;

void LogText(const char *text)
{
    Ev({evText, uint32_t(std::strlen(text))});
    for (const char *p = text; *p; ++p) gEv.push_back(uint8_t(*p));
}

uint32_t DescriptionId(const void *d)
{
    for (uint32_t i = 0; i < 3; ++i)
        if (d == gDescriptions[i]) return i + 1;
    return d ? 0xbad : 0;
}

// The owner's slots. Only al counts in what 12 and 14 answer, and the rest of eax says so.
const cTSParticlesSettings *__fastcall OwnerSettings(FakeOwner *o, void *)
{
    Ev({evSettings, o->id});
    return &gSettings;
}
FakeNode *__fastcall OwnerSource(FakeOwner *o, void *)
{
    Ev({evSource, o->id});
    return &gSource;
}
uint32_t __fastcall OwnerKnows(FakeOwner *o, void *, const char *name)
{
    Ev({evKnows, o->id});
    LogText(name);
    return gAnswers() % 4 ? 0xabcdef01u : 0xabcdef00u;
}
uint32_t __fastcall OwnerMake(FakeOwner *o, void *, const char *name, FakeNode **out)
{
    Ev({evMake, o->id});
    LogText(name);
    const uint32_t pick = gAnswers();
    if ((pick & 1) || (pick & 6)) { // a true answer with no effect runs off the null on both sides; a false one may still fill it
        FakeNode *const effect = &gEffectFakes[(pick >> 3) % 6];
        ++effect->refs;
        *out = effect;
    }
    return (pick & 6 ? 0xabcdef01u : 0xabcdef00u);
}
// The step places the colliders it holds through their objects, and a missing one crashes the game too: a test that runs the step
// has the owner find every collider.
bool gCollidersFound = false;
FakeNode *__fastcall OwnerCollider(FakeOwner *o, void *, const char *name)
{
    Ev({evCollider, o->id});
    LogText(name);
    const uint32_t pick = gAnswers() % 6;
    if (gCollidersFound) return &gColliders[pick % 4];
    return pick < 4 ? &gColliders[pick] : nullptr;
}
uint32_t __fastcall OwnerHold(FakeOwner *o, void *, uint32_t what)
{
    Ev({evHold, o->id, what});
    return gAnswers() % 8 ? 0xabcdef00u : 0xabcdef01u;
}

FakeNode *__fastcall SourceSlot7(FakeNode *n, void *, uint32_t a)
{
    Ev({evSource | 7, n->id, a});
    return gHaveSurface ? &gSurface : nullptr;
}
void __fastcall SurfaceSlot3(FakeNode *n, void *, float *a, float *b, float *c, float *d)
{
    Ev({evSurface | 3, n->id});
    *a = gSurfaceWords[0];
    *b = gSurfaceWords[1];
    *c = gSurfaceWords[2];
    *d = gSurfaceWords[3];
}
float __fastcall SurfaceSlot7(FakeNode *n, void *)
{
    Ev({evSurface | 7, n->id});
    return gSurfaceWords[4];
}
float __fastcall SurfaceSlot5(FakeNode *n, void *, float x, float y)
{
    Ev({evSurface | 5, n->id, Bits(x), Bits(y)});
    return gSurfaceWords[0];
}
Vector3 *__fastcall SurfaceSlot6(FakeNode *n, void *, Vector3 *out, float x, float y)
{
    Ev({evSurface | 6, n->id, Bits(x), Bits(y)});
    const uint32_t a = gAnswers(), b = gAnswers(), c = gAnswers();
    const auto part = [](uint32_t v) { return float(int32_t(v % 2049) - 1024) / 1024.0f; };
    *out = {part(a), part(b), part(c)};
    return out;
}
uint32_t __fastcall ColliderSlot3(FakeNode *n, void *)
{
    Ev({evCollider | 3, n->id});
    return n->id & 7;
}
uint32_t StateOffset(const char *state); // into the buffer of whichever test effect holds it
// Slot 4, the step's one placing of a collider: its description, the effect's two places and the collider's state.
void __fastcall ColliderPlace(FakeNode *n, void *, const void *, const EffectsTransform *local, const EffectsTransform *world, char *state)
{
    Ev({evCollider | 4, n->id, StateOffset(state), Bits(local->scale), Bits(world->scale), Bits(local->translation.x),
        Bits(world->translation.x)});
}
uint32_t __fastcall ColliderHit(FakeNode *n, void *, const cTSParticleCollider *, char *state, float distance, const Vector3 *vector,
                                Vector3 *position, Vector3 *normal)
{
    Ev({evCollider | 5, n->id, StateOffset(state), Bits(distance), Bits(vector->x), Bits(vector->y), Bits(vector->z),
        Bits(position->x), Bits(position->y), Bits(position->z)});
    const uint32_t pick = gAnswers();
    normal->x = 1.0f;
    normal->y = 0.0f;
    normal->z = 0.0f;
    return pick % 3 ? 0xabcdef00u : 0xabcdef01u;
}

// The effects a particle or the spawned list holds, by the visual-effect face's slots.
void PushPlace(const EffectsTransform *t)
{
    // Byte +1 the stack's: the game's helper leaves it unset.
    gEv.push_back(*reinterpret_cast<const uint32_t *>(t) & 0xffff00ffu);
    const auto *w = reinterpret_cast<const uint32_t *>(t);
    gEv.insert(gEv.end(), w + 1, w + sizeof(EffectsTransform) / 4);
}
void __fastcall EffectStart(FakeNode *n, void *, uint32_t a) { Ev({evEffect | 3, n->id, a}); }
void __fastcall EffectStop(FakeNode *n, void *, uint32_t a) { Ev({evEffect | 4, n->id, a}); }
uint32_t __fastcall EffectRunning(FakeNode *n, void *)
{
    Ev({evEffect | 5, n->id});
    return gAnswers() % 3 ? 0xabcdef01u : 0xabcdef00u;
}
void __fastcall EffectHide(FakeNode *n, void *, uint32_t hide) { Ev({evEffect | 8, n->id, hide & 0xff}); }
void __fastcall EffectParent(FakeNode *n, void *, const EffectsTransform *t)
{
    Ev({evEffect | 9, n->id});
    PushPlace(t);
}
void __fastcall EffectLocal(FakeNode *n, void *, const EffectsTransform *t)
{
    Ev({evEffect | 10, n->id});
    PushPlace(t);
}
uint32_t __fastcall EffectEvent(FakeNode *n, void *, uint32_t kind, const float *values, uint32_t count)
{
    Ev({evEffect | 13, n->id, kind, Bits(*values), count});
    return 0xabcdef01u;
}
void __fastcall EffectQuery(FakeNode *n, void *, uint32_t key, const void *value)
{
    Ev({evEffect | 14, n->id, key});
    if (key == 0 && value) LogText(static_cast<const cRZString *>(value)->mString.c_str()); // the override name, a new string
    else Ev({IdOf(value)});
}
void FillPlace(EffectsTransform *t)
{
    std::mt19937 r(gAnswers());
    RandomTransform(*t, r);
}
void __fastcall EffectReadParent(FakeNode *n, void *, EffectsTransform *t)
{
    Ev({evEffect | 18, n->id});
    FillPlace(t);
}
void __fastcall EffectReadLocal(FakeNode *n, void *, EffectsTransform *t)
{
    Ev({evEffect | 19, n->id});
    FillPlace(t);
}
Description *__fastcall DescriptionFaceSlot3(FakeNode *n, void *)
{
    Ev({evDescription, n->id});
    return gNextDescription;
}

// The colour the draw tick hands a particle's effect (slot 12: the kind, three floats, a count).
void __fastcall EffectColor(FakeNode *n, void *, uint32_t kind, const float *values, uint32_t count)
{
    Ev({evEffect | 12, n->id, kind, Bits(values[0]), Bits(values[1]), Bits(values[2]), count});
}

// The owner's world object (owner slot 43), which the draw tick asks where a point lands on the screen: slot 143 writes three
// floats, mostly inside -1..1, now and then past it, a NaN or a zero.
struct FakeWorldObject {
    void *const *vt;
};
constexpr unsigned kWorldSlots = 150;
void *gWorldVT[kWorldSlots];
FakeWorldObject gWorldObject{gWorldVT};
uint32_t __fastcall WorldOther(FakeWorldObject *, void *)
{
    Ev({evWorld | 0xffff});
    return 0;
}
void __fastcall WorldProject(FakeWorldObject *, void *, float *out, const Vector3 *point, uint32_t flag)
{
    Ev({evWorld | 143, Bits(point->x), Bits(point->y), Bits(point->z), flag});
    for (int i = 0; i < 3; ++i) {
        const uint32_t a = gAnswers();
        float v = float(int32_t(a % 2049) - 1024) / 700.0f;
        if (a % 23 == 0) v = std::bit_cast<float>(0x7fc00000u | ((a >> 8) & 0x3fffffu) | (a << 31));
        else if (a % 19 == 0) v = a % 2 ? 0.0f : -0.0f;
        else if (a % 29 == 0) v = a % 2 ? 1.0f : -1.0f;
        else if (a % 11 == 0) v *= 3.0f;
        out[i] = v;
        Ev({Bits(v)});
    }
}
FakeWorldObject *__fastcall OwnerWorld(FakeOwner *o, void *)
{
    Ev({evWorld, o->id});
    return &gWorldObject;
}

// The fakes' tables, restored at the end.
struct Overrides {
    void *owner[kFakeOwnerSlots], *node[kFakeNodeSlots];
    template <class F> static void Set(void **table, unsigned slot, F f) { table[slot] = reinterpret_cast<void *>(f); }
    Overrides()
    {
        std::memcpy(owner, gFakeOwnerVT, sizeof(owner));
        std::memcpy(node, gFakeNodeVT, sizeof(node));
        Set(gFakeOwnerVT, 8, &OwnerHold);
        Set(gFakeOwnerVT, 12, &OwnerKnows);
        Set(gFakeOwnerVT, 14, &OwnerMake);
        Set(gFakeOwnerVT, 21, &OwnerSettings);
        Set(gFakeOwnerVT, 24, &OwnerSource);
        Set(gFakeOwnerVT, 28, &OwnerCollider);
        Set(gFakeOwnerVT, 43, &OwnerWorld);
        for (void *&slot : gWorldVT) slot = reinterpret_cast<void *>(&WorldOther);
        gWorldVT[143] = reinterpret_cast<void *>(&WorldProject);
        for (void **table : {gSourceVT, gSurfaceVT, gColliderVT, gEffectVT, gDescriptionVT})
            std::memcpy(table, gFakeNodeVT, sizeof(gFakeNodeVT));
        Set(gSourceVT, 7, &SourceSlot7);
        Set(gSurfaceVT, 3, &SurfaceSlot3);
        Set(gSurfaceVT, 5, &SurfaceSlot5);
        Set(gSurfaceVT, 6, &SurfaceSlot6);
        Set(gSurfaceVT, 7, &SurfaceSlot7);
        Set(gColliderVT, 3, &ColliderSlot3);
        Set(gColliderVT, 4, &ColliderPlace);
        Set(gColliderVT, 5, &ColliderHit);
        Set(gEffectVT, 3, &EffectStart);
        Set(gEffectVT, 4, &EffectStop);
        Set(gEffectVT, 5, &EffectRunning);
        Set(gEffectVT, 8, &EffectHide);
        Set(gEffectVT, 9, &EffectParent);
        Set(gEffectVT, 10, &EffectLocal);
        Set(gEffectVT, 12, &EffectColor);
        Set(gEffectVT, 13, &EffectEvent);
        Set(gEffectVT, 14, &EffectQuery);
        Set(gEffectVT, 18, &EffectReadParent);
        Set(gEffectVT, 19, &EffectReadLocal);
        Set(gDescriptionVT, 3, &DescriptionFaceSlot3);
    }
    ~Overrides()
    {
        std::memcpy(gFakeOwnerVT, owner, sizeof(owner));
        std::memcpy(gFakeNodeVT, node, sizeof(node));
    }
};

// 0x1ad6f7 ends in the CRT's formatter: a stand-in on both sides, which logs the format and the text.
cRZString *__cdecl StandSprintf(cRZString *self, const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    LogText(format);
    LogText(text);
    self->mString = text;
    return self;
}

void *__cdecl ArenaMalloc(uint32_t bytes)
{
    Ev({evAlloc, bytes});
    if (gArenaUsed + bytes > kArenaBytes) {
        gCtx->Fail("arena out: %u bytes wanted, %u used", bytes, gArenaUsed);
        std::abort();
    }
    void *const p = gArena + gArenaUsed;
    gArenaUsed += bytes;
    return p;
}

// What every test of the effect's code keeps alive around its seeds.
struct MetaWorld {
    Overrides overrides;
    shape::StandIn sprintfStand{kSprintf, &cRZString::Sprintf, &StandSprintf};
    shape::StandIn alloc{kMalloc, &RZMemory::Malloc, &ArenaMalloc};
};

void ResetWorld(uint32_t seed, EffectsWorld &w)
{
    gArenaUsed = 0;
    std::memset(gArena, 0xcd, sizeof(gArena));
    gSettings = {};
    gSettings.mRateFactor = 1.0f;
    gSettings.mSizeFactor = float(1 + seed % 3) * 0.5f;
    gSettings.mDetail = float(seed % 5) * 0.25f;
    gSource = FakeNode{gSourceVT, 0x40, 0, nullptr};
    gSurface = FakeNode{gSurfaceVT, 0x41, 0, nullptr};
    gDescriptionFace = FakeNode{gDescriptionVT, 0x44, 0, nullptr};
    for (uint32_t i = 0; i < 4; ++i) gColliders[i] = FakeNode{gColliderVT, 0x60 + i, 0, nullptr};
    for (uint32_t i = 0; i < 6; ++i) gEffectFakes[i] = FakeNode{gEffectVT, 0x68 + i, 0, nullptr};
    gAnswers.seed(seed + 99);
    gHaveSurface = false;
    gDescriptions[0] = gDescriptions[1] = gDescriptions[2] = nullptr;
    gNextDescription = nullptr;
    gScratchBase = nullptr;
    w.owners[0].refs = 0;
    auto *random = GameAddress<cRZRandom>(kRandomCell);
    random->mIntegerSeed = seed * 2654435761u + 12345u;
    random->mRealSeed = int32_t(seed * 40503u + 77u);
    *GameAddress<void *>(kPoolFreeCell) = nullptr;
    *GameAddress<void *>(kPoolListCell) = nullptr;
    *GameAddress<uint32_t>(kPoolElementCell) = sizeof(Particle);
    // A small block too, so the free list runs out, and bytes that don't divide evenly.
    *GameAddress<uint32_t>(kPoolBlockCell) = seed % 3 ? uint32_t(sizeof(Particle)) * (2 + seed % 5) + seed % 7 : 0x3e00;
}

// ---- random inputs ----

float Nan(std::mt19937 &r) { return std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu) | (r() << 31)); }
float Wild(std::mt19937 &r)
{
    switch (r() % 9) {
    case 0: return 0.0f;
    case 1: return 0.5f;
    case 2: return 1.0f;
    case 3: return -1.0f;
    case 4: return 123456.7f;
    case 5: return Nan(r);
    default: return testhelp::FewOrUniform(r, -3.0f, 3.0f);
    }
}
float Tame(std::mt19937 &r) { return float(int32_t(r() % 4001) - 2000) / 512.0f; }
float Positive(std::mt19937 &r) { return float(1 + r() % 1024) / 512.0f; }

void RandomCurve(cTSEffectCurve &curve, std::mt19937 &r, bool tame)
{
    curve.mKind = cTSEffectCurve::kList;
    curve.mValues.clear();
    const unsigned n = 1 + r() % 4;
    for (unsigned i = 0; i < n; ++i) curve.mValues.push_back(tame ? Positive(r) : Wild(r));
}

static const char *const kNames[] = {"", "meta", "a_name_long_enough_to_leave_the_small_buffer"};
// `n` more colliders on a description.
void RandomColliders(Description &d, std::mt19937 &r, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        cTSParticleCollider c;
        c.mName = kNames[1 + r() % 2];
        c.m50 = float(r() % 3) * 0.5f; // the chance a hit ends the particle: never, a draw, always
        if (r() % 2) c.m54 = kNames[1 + r() % 2];
        if (r() % 2) c.m6C = kNames[1 + r() % 2];
        d.mColliders.push_back(c);
    }
}

// `tame`: what the game's emitters and step can run on (nothing goes on for ever, nothing leaves the pool). `low` and `high`: the
// flag words.
void RandomDescription(Description &d, std::mt19937 &r, bool tame, uint32_t low, uint32_t high)
{
    const auto value = [&] { return tame ? Tame(r) : Wild(r); };
    d.mLife = tame ? fxdesc::Vector2{0.25f, 1.5f} : fxdesc::Vector2{value(), value()};
    d.mPreroll = tame ? float(r() % 3) * 0.25f : value();
    d.mRateVary = r() % 4 == 0 ? (r() % 2 ? 0.0f : 1e-6f) : tame ? Positive(r) : Wild(r);
    if (tame && d.mRateVary < 0.5f) d.mRateVary = 0.5f;
    d.mRateMode = r() % 3 == 0 ? r() % 3 : 0;
    if (tame) {
        RandomCurve(d.mRate, r, true);
        for (float &v : d.mRate.mValues) v *= 16.0f;
    }
    d.m28 = {r() % 3 ? -1.0f : value(), value()};
    d.m30 = {r() % 3 ? -1.0f : value(), value()};
    if (tame && d.m30.x >= 0.0f) { // a negative pause never ends the emitter's loop, in the game too
        d.m30.x = Positive(r);
        d.m30.y = d.m30.x + Positive(r);
    }
    d.m38 = {value(), value(), value()};
    d.m44 = {value(), value(), value()};
    d.mEmitVolume = {value(), value()};
    d.mSourceMin = {value(), value(), value()};
    d.mSourceMax = {value(), value(), value()};
    d.mForce = {value(), value(), value()};
    RandomCurve(d.mSize, r, tame);
    RandomCurve(d.mAlpha, r, tame);
    d.mFlags = uint64_t(high) << 32 | low;
    d.mEffect = kNames[r() % 3];
    for (cTSParticleWalk &walk : d.mWalks) {
        // A delay of a tick or more, or the clock's loop never ends.
        walk.mDelay = {0.0625f + Positive(r), 0.0625f + Positive(r)};
        walk.mStrength = {value(), value()};
        walk.mTurn[0] = value();
        walk.mTurn[1] = value();
        static const float kMixes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        walk.mMix = r() % 8 == 0 ? value() : kMixes[r() % 5];
    }
    switch (r() % 4) {
    case 0: d.mPreferDir = {0.0f, 0.0f, 0.0f}; break;
    case 1: // around the cells it is compared with, by a few steps of the float
        d.mPreferDir.x = std::bit_cast<float>(*GameAddress<uint32_t>(0x10fd35c) + (r() % 3 ? 0u : uint32_t(r() % 64) - 32u));
        d.mPreferDir.y = std::bit_cast<float>(*GameAddress<uint32_t>(0x10fd360) + (r() % 3 ? 0u : uint32_t(r() % 64) - 32u));
        d.mPreferDir.z = std::bit_cast<float>(*GameAddress<uint32_t>(0x10fd364) + (r() % 3 ? 0u : uint32_t(r() % 64) - 32u));
        break;
    default: d.mPreferDir = {value(), value(), value()}; break;
    }
    d.mAttractors.clear();
    const unsigned attractors = r() % 3;
    for (unsigned i = 0; i < attractors; ++i) {
        cTSParticleAttractor a;
        a.mHasCentre = uint8_t(r() % 3);
        a.mCentre = {value(), value(), value()};
        a.mStrength = value();
        d.mAttractors.push_back(a);
    }
    d.mColliders.clear();
    if (!tame) RandomColliders(d, r, r() % 4);
}

// What the draw tick and the particle's place read: the colours, the bloom bytes, the screw, how the particle faces, its damping,
// bank and warps, and the curves. `wild`: any value; else values the game would have.
void RandomLook(Description &d, std::mt19937 &r, bool wild)
{
    const auto value = [&] { return wild ? Wild(r) : Tame(r); };
    const auto unit = [&] { return wild && r() % 4 == 0 ? Wild(r) : float(r() % 1025) / 1024.0f; };
    d.mColors.clear();
    // ORIGINAL-BUG: with no colours the draw tick reads the vector far out of range, so there is always one
    const unsigned colors = 1 + r() % 4;
    for (unsigned i = 0; i < colors; ++i) d.mColors.push_back(fxdesc::Color{unit(), unit(), unit()});
    for (uint8_t &byte : d.mBloom) byte = r() % 3 == 0 ? 0 : uint8_t(r());
    if (r() % 3 == 0) d.mBloom[0] = d.mBloom[2] = 0; // no bloom
    d.mAlignment = uint8_t(r() % 7); // 0 and 1 spin only, 2..4 the three bases, more are spin only again
    d.mScrew = r() % 3 == 0 ? 0.0f : value();
    d.mDamp = r() % 3 == 0 ? 0.0f : (wild && r() % 4 == 0 ? Wild(r) : Positive(r));
    d.mBank = {r() % 2 ? 0.0f : value(), value()};
    d.mWarps.clear();
    const unsigned warps = r() % 3 == 0 ? 0 : r() % 3;
    for (unsigned i = 0; i < warps; ++i) {
        cTSParticleWarp w;
        w.mValue = value();
        w.mFirst = {value(), value(), value()};
        w.mSecond = {value(), value(), value()};
        d.mWarps.push_back(w);
    }
    RandomCurve(d.mRotateCurve, r, !wild);
    RandomCurve(d.mAspect, r, !wild);
    RandomCurve(d.mSize, r, !wild);
}

// The emitters run for real on both sides, and a wild description's need not end (a count below the live ones, a rate with no
// bound, a pause that never passes). For one call of them the emission's own fields are made ones they end on, and put back
// after: the description's rate, life, preroll, spread, limit and pause, and the effect's clocks and scales. A once-only emitter's
// count is above any the callers keep alive (they ask only while the effect holds 100 or fewer).
struct EmissionSave {
    fxdesc::Vector2 life, pause;
    float preroll, vary;
    uint32_t rateMode;
    cTSEffectCurve rate;
};
EmissionSave TameEmission(Effect *t, std::mt19937 &r)
{
    auto *const d = const_cast<Description *>(t->mDescription);
    EmissionSave saved{d->mLife, d->m30, d->mPreroll, d->mRateVary, d->mRateMode, d->mRate};
    RandomCurve(d->mRate, r, true);
    if (static_cast<uint32_t>(d->mFlags) & 2)
        for (float &v : d->mRate.mValues) v = float(101 + r() % 20);
    d->mLife = {0.25f, 1.5f};
    d->mPreroll = float(r() % 3) * 0.25f;
    d->mRateVary = 0.5f + Positive(r);
    d->mRateMode = r() % 3 == 0 ? r() % 3 : 0;
    d->m30 = {-1.0f, 0.0f};
    if (r() % 2) d->m30 = {r() % 3 == 0 ? 0.0f : Positive(r), 1.0f + Positive(r)};
    t->mEmitPeriod = 0.25 + double(Positive(r));
    t->mEmitClock = double(Positive(r)) * 0.5;
    t->mEmitted = int32_t(r() % 3);
    t->mEmitRemainder = r() % 2 ? 0.0f : Positive(r);
    t->mRateScale = Positive(r) * 0.5f;
    t->mEvent101[0] = Positive(r) * 0.5f;
    return saved;
}
void RestoreEmission(Effect *t, const EmissionSave &s)
{
    auto *const d = const_cast<Description *>(t->mDescription);
    d->mLife = s.life;
    d->m30 = s.pause;
    d->mPreroll = s.preroll;
    d->mRateVary = s.vary;
    d->mRateMode = s.rateMode;
    d->mRate = s.rate;
}

// ---- reading an effect ----

struct OurEffectTables {
    EffectVt live, destroyed;
};
const OurEffectTables &OurTables()
{
    static OurEffectTables t = [] {
        OurEffectTables v{};
        alignas(8) static uint8_t block[sizeof(Effect)];
        ::new (block) Effect(nullptr);
        v.live = EffectVt::Of(block);
        CallSlot<void *>(block + 4, 0, uint32_t(0));
        v.destroyed = EffectVt::Of(block);
        return v;
    }();
    return t;
}
uint32_t TableCode(uint32_t word)
{
    const OurEffectTables &ours = OurTables();
    const unsigned live = shape::FaceOf(word, 2, shape::GameTables{kGameTables}, ours.live);
    if (live != ~0u) return shape::kFaceCode | live;
    const unsigned destroyed = shape::FaceOf(word, 2, shape::GameTables{kGameBaseTables}, ours.destroyed);
    if (destroyed != ~0u) return shape::kFaceCode | 0x10 | destroyed;
    return word;
}

Effect *gEffects[2];
uint32_t StateOffset(const char *state)
{
    // Inside a test effect's buffer, or on its end (a collider whose state is empty), counted from it: the heap's layout differs
    // between the two runs.
    for (uint32_t i = 0; i < 2; ++i) {
        const Effect *const e = gEffects[i];
        if (!e) continue;
        const std::ptrdiff_t d = state - e->mColliderState.data();
        if (d >= 0 && d <= std::ptrdiff_t(e->mColliderState.size())) return (i + 1) << 24 | uint32_t(d);
    }
    return gScratchBase ? uint32_t(state - gScratchBase) : 0xbad;
}

uint32_t EffectId(const void *p)
{
    if (!p) return 0;
    for (uint32_t i = 0; i < 2; ++i)
        if (static_cast<const cITSComponentEffect *>(gEffects[i]) == p) return i + 1;
    return 0xbad;
}

// A particle: its words, the free list's link left out of one in the pool. The effect word is compared as it is: the fakes are
// statics, at the same address on both sides.
void PushParticle(Blob &b, const Particle *p, bool first)
{
    b.push_back(ArenaOffset(p));
    const auto *w = reinterpret_cast<const uint32_t *>(p);
    b.insert(b.end(), w + (first ? 0 : 1), w + sizeof(Particle) / 4);
}

void PushVectorShape(Blob &b, size_t size, size_t capacity)
{
    b.push_back(uint32_t(size));
    b.push_back(uint32_t(capacity));
}

// An element of a description's attractors or colliders: which description, which element.
template <class Member> uint32_t ElementId(const void *element, Member member)
{
    if (!element) return 0;
    for (uint32_t i = 0; i < 3; ++i) {
        if (!gDescriptions[i]) continue;
        const auto &list = gDescriptions[i]->*member;
        for (size_t k = 0; k < list.size(); ++k)
            if (element == &list[k]) return (i + 1) << 16 | uint32_t(k);
    }
    return 0xbad;
}

// True while chasing a difference: the report then names the field (marker 0b000080 + offset / 4).
constexpr bool kOffsetMarkers = false;

// Every byte of the effect: the words that hold pointers by what they point at, the rest as they are.
void PushEffectState(Blob &b, const Effect *e)
{
    const auto *w = reinterpret_cast<const uint32_t *>(e);
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    for (uint32_t o = 8; o < Effect::kSize; o += 4) {
        if (kOffsetMarkers) b.push_back(0x0b000080u + o / 4);
        switch (o) {
        case 0x10: break; // the list's head node
        case 0x14:
            b.push_back(uint32_t(e->mParticles.size()));
            for (const Particle *p : e->mParticles) PushParticle(b, p, true);
            break;
        case 0x1c: b.push_back(DescriptionId(e->mDescription)); break;
        case 0x20: b.push_back(IdOf(e->mOwner.Get())); break;
        case 0x24: b.push_back(uint32_t(e->mSettings == &gSettings)); break;
        case 0x104:
            PushVectorShape(b, e->mAttractors.size(), e->mAttractors.capacity());
            for (const cTSParticleAttractorState &a : e->mAttractors) {
                b.insert(b.end(), {Bits(a.mCentre.x), Bits(a.mCentre.y), Bits(a.mCentre.z), Bits(a.mStrength)});
                b.push_back(ElementId(a.mDescription, &cTSParticlesDescription::mAttractors));
            }
            break;
        case 0x108:
        case 0x10c: break;
        case 0x14c: b.push_back(IdOf(e->mSurface.Get())); break;
        case 0x17c:
            PushVectorShape(b, e->mColliders.size(), e->mColliders.capacity());
            for (const cTSParticleColliderState &c : e->mColliders) {
                b.push_back(IdOf(c.mObject));
                b.push_back(ElementId(c.mDescription, &cTSParticlesDescription::mColliders));
                b.push_back(c.mStateOffset);
            }
            break;
        case 0x180:
        case 0x184: break;
        case 0x18c:
            PushVectorShape(b, e->mColliderState.size(), e->mColliderState.capacity());
            for (char c : e->mColliderState) b.push_back(uint8_t(c));
            break;
        case 0x190:
        case 0x194: break;
        case 0x19c: b.push_back(IdOf(e->mSpawnArgument.Get())); break;
        case 0x1a4: { // the string's words: its text, and its count
            const cRZString &text = e->mNameOverride.mString;
            PushString(b, text.mString);
            b.push_back(uint32_t(text.mString.capacity()));
            b.push_back(text.mRefCount);
            o = 0x1c0;
            break;
        }
        case 0x1c8: b.push_back(EffectId(e->mFollower.Get())); break;
        case 0x1cc:
            PushVectorShape(b, e->mPath.size(), e->mPath.capacity());
            for (const cTSParticlePathPoint &p : e->mPath) {
                const auto *words = reinterpret_cast<const uint32_t *>(&p);
                b.insert(b.end(), words, words + sizeof(p) / 4);
            }
            break;
        case 0x1d0:
        case 0x1d4: break;
        case 0x1dc:
            PushVectorShape(b, e->mSpawned.size(), e->mSpawned.capacity());
            for (const fx::CountedRef<cIGZUnknown> &s : e->mSpawned) b.push_back(IdOf(s.Get()));
            break;
        case 0x1e0:
        case 0x1e4: break;
        case 0x1e8: b.push_back(IdOf(e->mNode.Get())); break;
        default: b.push_back(w[o / 4]);
        }
    }
}

void PushPool(Blob &b)
{
    b.push_back(0x0b0000e2u);
    for (const void *p = *GameAddress<void *>(kPoolFreeCell); p; p = *static_cast<void *const *>(p))
        PushParticle(b, static_cast<const Particle *>(p), false);
    b.push_back(0x0a0000ffu);
    auto *random = GameAddress<cRZRandom>(kRandomCell);
    b.push_back(random->mIntegerSeed);
    b.push_back(uint32_t(random->mRealSeed));
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

void Fingerprint(Blob &b)
{
    Drain(b);
    for (const Effect *e : gEffects)
        if (e) {
            b.push_back(e == gEffects[0] ? 0x0b0000e0u : 0x0b0000e1u); // a difference is reported from here
            PushEffectState(b, e);
        }
    PushPool(b);
}

// What the constructor leaves unset, offset and bytes: the same on both sides from MakeEffect on (Meta_Effect_Construct compares
// them over random bytes).
struct Unset {
    uint16_t offset, bytes;
};
constexpr Unset kUnset[] = {{0x29, 1},    {0x61, 1},    {0x98, 16},   {0xb0, 8},    {0xc0, 4},    {0xd0, 0x28},  {0xf9, 3},
                            {0x11c, 12},  {0x144, 8},   {0x174, 1},   {0x179, 3},   {0x188, 4},   {0x199, 3},    {0x1ec, 4}};

void *MakeEffect(int mode, Description *d)
{
    void *const e = mode ? static_cast<void *>(Effect::Create(d)) : gCtx->Original<CreateFn>(kCreate)(d);
    auto *bytes = static_cast<uint8_t *>(e);
    for (const Unset &u : kUnset) std::memset(bytes + u.offset, 0x5a, u.bytes);
    return e;
}

// The pool as the emitters would leave it: `count` free particles from the arena.
void FillPool(uint32_t count, std::mt19937 &r)
{
    for (uint32_t i = 0; i < count; ++i) {
        auto *p = reinterpret_cast<Particle *>(gArena + gArenaUsed);
        gArenaUsed += sizeof(Particle);
        auto *w = reinterpret_cast<uint32_t *>(p);
        for (size_t k = 0; k < sizeof(Particle) / 4; ++k) w[k] = Bits(Wild(r)) | 1; // no zero word, so the constructor's reach shows
        void **const cell = GameAddress<void *>(kPoolFreeCell);
        *reinterpret_cast<void **>(p) = *cell;
        *cell = p;
    }
}
// A particle out of the pool, with an effect sometimes (one more reference on the fake, as the emitters' make would leave it).
Particle *TakeParticle(std::mt19937 &r)
{
    void **const cell = GameAddress<void *>(kPoolFreeCell);
    auto *p = static_cast<Particle *>(*cell);
    if (!p) return nullptr;
    *cell = *reinterpret_cast<void **>(p);
    *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(p) + offsetof(Particle, mEffect)) = nullptr;
    p->mAlive = uint8_t(r() % 2);
    p->mLife = r() % 2 ? 1.0f : Wild(r);
    p->mAge = r() % 2 ? 2.0f : Wild(r);
    p->mPosition = {Wild(r), Wild(r), Wild(r)};
    if (r() % 2) {
        FakeNode *const effect = &gEffectFakes[r() % 6];
        ++effect->refs;
        *reinterpret_cast<FakeNode **>(reinterpret_cast<uint8_t *>(p) + offsetof(Particle, mEffect)) = effect;
    }
    return p;
}

template <class Fn, class... A> void Helper(uint32_t rva, void *self, A... args)
{
    gCtx->Original<Fn>(rva)(self, args...);
}

struct Precision {
    difftest::X87Precision bits24{difftest::X87Pc::Bits24};
};

} // namespace
