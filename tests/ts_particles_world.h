#pragma once
// The particle effect's test world (lane WR built it inside tests/ts_particles_effect.cpp, lane WV moved it here): the effects
// world and the owner's fakes, the renderable's place in a logged fake tree, the random descriptions, the effect's whole-object
// fingerprint and the particle pool. Every test of cTSParticlesEffect's code includes this and adds its own operations.
// Everything is in the including file's anonymous namespace: each test file has its own copy of the globals.

#include "ts_child_nodes.h"
#include "ts_effects_collection.h"

#include "drawfx_core.h"
#include "free_recorder.h"
#include "rz/RZMemory.h"
#include "rz/cRZRandom.h"
#include "rz/cRZString.h"
#include "sg_renderable_fx.h"
#include "sgfx.h"
#include "shape.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"
#include "ts/cTSParticlesRenderable.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

namespace {

using namespace fxtest;
using fx::EffectsTransform;
using fx::Vector3;
using Node = nGZSceneGraph::cTSParticlesRenderable;
using Effect = cTSParticlesEffect;

constexpr uint32_t kGameTables[2] = {0xe8bdbc, 0xe8bdf8};
constexpr uint32_t kGameBaseTables[2] = {0xe8bd68, 0xda2fa8};
constexpr uint32_t kCreate = 0xbc0c50, kConstruct = 0xbb4a80;
constexpr uint32_t kPoolElementCell = 0x107cfc8, kPoolBlockCell = 0x107cfcc, kPoolListCell = 0x107cfd0;
constexpr uint32_t kEventCell = 0x10fc820;

using CreateFn = void *(__cdecl *)(const void *);
using ConstructFn = void *(__thiscall *)(void *, const void *);

enum : uint32_t {
    evSetup = 0xc1000000,
    evBuild = 0xc2000000,
    evTree = 0xc3000000,      // | slot; the renderable's id, then the arguments
    evSettings = 0xc4000000,
    evSource = 0xc5000000,
    evSurface = 0xc6000000,
    evCollider = 0xc7000000,
    evConstraint = 0xc8000000,
    evMake = 0xc9000000,
    evDeath = 0xca000000,     // | slot; the fake's id, then the arguments
    evQuery = 0xcb000000,
    evDescription = 0xcc000000,
    evText = 0xcd000000,
    evScene = 0xce000000,
    evInstance = 0xcf000000,
    evAlloc = 0xd0000000,
};

difftest::Context *gCtx = nullptr;

// ---- this run's world (each side's run sets it up alike) ----

constexpr uint32_t kArenaBytes = 0x3e08 * 8;
alignas(16) uint8_t gArena[kArenaBytes];
uint32_t gArenaUsed = 0;
uint32_t ArenaOffset(const void *p) { return uint32_t(static_cast<const uint8_t *>(p) - gArena); }

cTSParticlesSettings gSettings;
FakeNode gSource, gSurface, gConstraint, gShape, gDescriptionFace, gColliders[4], gDeaths[4];
void *gSourceVT[kFakeNodeSlots], *gSurfaceVT[kFakeNodeSlots], *gColliderVT[kFakeNodeSlots], *gDeathVT[kFakeNodeSlots],
    *gDescriptionVT[kFakeNodeSlots], *gConstraintVT[kFakeNodeSlots];
std::mt19937 gAnswers;
float gSurfaceWords[4];
bool gHaveSurface, gGiveShape;
bool gChildNodesLinked = false;  // the test's children are childnodes::ChildNode, kept in sibling order (slot 38)
float gNodeWorld[7];             // what a renderable's slot 54 hands out
uint32_t gNodeParameters[7];     // what Setup leaves in a renderable's shader parameters
cTSParticlesDescription *gDescriptions[3];
cTSParticlesDescription *gNextDescription;

// The renderable's place in the tree, a fake on both sides.
struct Renderable {
    Node *node;
    void *vt[92];
    FakeNode *parent;
    std::vector<FakeNode *> children;
};
Renderable gRenderables[2];

Renderable *RenderableOf(const void *node)
{
    for (Renderable &r : gRenderables)
        if (r.node == node) return &r;
    return nullptr;
}
uint32_t RenderableId(const Renderable *r) { return r ? 0x50 + uint32_t(r - gRenderables) : 0xdead; }

void *__fastcall TreeGetParent(Node *n, void *)
{
    Renderable *r = RenderableOf(n);
    Ev({evTree | 34, RenderableId(r)});
    return r->parent;
}
void *__fastcall TreeGetChildren(Node *n, void *, uint32_t *out)
{
    Renderable *r = RenderableOf(n);
    Ev({evTree | 42, RenderableId(r)});
    out[0] = shape::P(r->children.empty() ? nullptr : r->children.front());
    out[1] = out[2] = 0;
    return out;
}
float *__fastcall TreeGetWorld(Node *n, void *, float *out)
{
    Ev({evTree | 54, RenderableId(RenderableOf(n))});
    std::memcpy(out, gNodeWorld, sizeof(gNodeWorld));
    return out;
}
void __fastcall TreeAddChild(Node *n, void *, FakeNode *child)
{
    Renderable *r = RenderableOf(n);
    Ev({evTree | 45, RenderableId(r), IdOf(child)});
    child->vt[1] ? reinterpret_cast<uint32_t(__fastcall *)(FakeNode *, void *)>(child->vt[1])(child, nullptr) : 0;
    if (gChildNodesLinked) { // the real tree's AddChild links at the head of the sibling list
        r->children.insert(r->children.begin(), child);
        childnodes::ChildNodes::Link(r->children);
    } else {
        r->children.push_back(child);
    }
}
void __fastcall TreeRemoveChild(Node *n, void *, FakeNode *child)
{
    Renderable *r = RenderableOf(n);
    Ev({evTree | 46, RenderableId(r), IdOf(child)});
    for (size_t i = 0; i < r->children.size(); ++i)
        if (r->children[i] == child) {
            r->children.erase(r->children.begin() + i);
            reinterpret_cast<uint32_t(__fastcall *)(FakeNode *, void *)>(child->vt[2])(child, nullptr);
            if (gChildNodesLinked) childnodes::ChildNodes::Link(r->children);
            break;
        }
}
uint32_t __fastcall TreeHasChildren(Node *n, void *)
{
    Renderable *r = RenderableOf(n);
    Ev({evTree | 52, RenderableId(r)});
    return r->children.empty() ? 0xabcdef00u : 0xabcdef01u; // only al counts
}

uint32_t DescriptionId(const void *d)
{
    for (uint32_t i = 0; i < 3; ++i)
        if (d == gDescriptions[i]) return i + 1;
    return d ? 0xbad : 0;
}

void __fastcall StandSetup(Node *n, void *, cTSParticlesDescription *description)
{
    Renderable *r = RenderableOf(n);
    if (!r) {
        r = gRenderables[0].node ? &gRenderables[1] : &gRenderables[0];
        r->node = n;
        std::memcpy(r->vt, *reinterpret_cast<void ***>(n), sizeof(r->vt));
        r->vt[34] = reinterpret_cast<void *>(&TreeGetParent);
        r->vt[42] = reinterpret_cast<void *>(&TreeGetChildren);
        r->vt[45] = reinterpret_cast<void *>(&TreeAddChild);
        r->vt[46] = reinterpret_cast<void *>(&TreeRemoveChild);
        r->vt[52] = reinterpret_cast<void *>(&TreeHasChildren);
        r->vt[54] = reinterpret_cast<void *>(&TreeGetWorld);
        *reinterpret_cast<void ***>(n) = r->vt;
    }
    Ev({evSetup, RenderableId(r), DescriptionId(description)});
    if (gGiveShape && !n->mShape.mObject) {
        ++gShape.refs;
        n->mShape.mObject = reinterpret_cast<cIGZUnknown *>(&gShape);
        std::memcpy(n->mParams, gNodeParameters, sizeof(gNodeParameters));
    }
}
void __fastcall StandBuild(Node *n, void *, cTSParticlesDescription *description)
{
    Ev({evBuild, RenderableId(RenderableOf(n)), DescriptionId(description)});
}

void LogText(const char *text)
{
    Ev({evText, uint32_t(std::strlen(text))});
    for (const char *p = text; *p; ++p) gEv.push_back(uint8_t(*p));
}

// The owner's slots.
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
FakeNode *__fastcall SourceSlot7(FakeNode *n, void *, uint32_t a)
{
    Ev({evSource | 7, n->id, a});
    return gHaveSurface ? &gSurface : nullptr;
}
void __fastcall SurfaceSlot3(FakeNode *n, void *, float *a, float *b, float *c, float *d)
{
    Ev({evSurface, n->id});
    *a = gSurfaceWords[0];
    *b = gSurfaceWords[1];
    *c = gSurfaceWords[2];
    *d = gSurfaceWords[3];
}
FakeNode *__fastcall OwnerCollider(FakeOwner *o, void *, const char *name)
{
    Ev({evCollider, o->id});
    LogText(name);
    const uint32_t pick = gAnswers() % 6;
    return pick < 4 ? &gColliders[pick] : nullptr;
}
uint32_t __fastcall ColliderSlot3(FakeNode *n, void *)
{
    Ev({evCollider | 3, n->id});
    return n->id & 7;
}
FakeNode *__fastcall OwnerConstraint(FakeOwner *o, void *, const char *name)
{
    Ev({evConstraint, o->id});
    LogText(name);
    return gAnswers() % 3 ? nullptr : &gConstraint;
}
uint32_t __fastcall OwnerMake(FakeOwner *o, void *, const char *name, FakeNode **out)
{
    Ev({evMake, o->id});
    LogText(name);
    const uint32_t pick = gAnswers();
    if ((pick & 1) || (pick & 6)) { // a true answer with no effect runs off the null on both sides
        FakeNode *const death = &gDeaths[(pick >> 1) & 3];
        ++death->refs;
        *out = death;
    }
    return (pick & 6 ? 0xabcdef01u : 0xabcdef00u); // only al counts
}
FakeNode *__fastcall OwnerScene(FakeOwner *o, void *)
{
    Ev({evScene, o->id});
    return &gWorld->trees[2];
}
void PushPlace(const EffectsTransform *t)
{
    // Byte +1 the stack's: the game's helper leaves it unset.
    gEv.push_back(*reinterpret_cast<const uint32_t *>(t) & 0xffff00ffu);
    const auto *w = reinterpret_cast<const uint32_t *>(t);
    gEv.insert(gEv.end(), w + 1, w + sizeof(EffectsTransform) / 4);
}
void __fastcall DeathStart(FakeNode *n, void *, uint32_t a) { Ev({evDeath | 3, n->id, a}); }
void __fastcall DeathOwnPlace(FakeNode *n, void *, const EffectsTransform *t)
{
    Ev({evDeath | 9, n->id});
    PushPlace(t);
}
void __fastcall DeathPlace(FakeNode *n, void *, const EffectsTransform *t)
{
    Ev({evDeath | 10, n->id});
    PushPlace(t);
}
void __fastcall ConstraintSlot3(FakeNode *n, void *, const void *local, const void *world, float *position, const Vector3 *offset)
{
    Ev({evConstraint | 3, n->id, Bits(position[0]), Bits(position[1]), Bits(position[2]), Bits(offset->x), Bits(offset->y), Bits(offset->z)});
    (void)local;
    (void)world;
}
void __fastcall InstanceShape(FakeNode *n, void *, FakeNode *shapeObject) { Ev({evInstance, n->id, IdOf(shapeObject)}); }
cTSParticlesDescription *__fastcall DescriptionFaceSlot3(FakeNode *n, void *)
{
    Ev({evDescription, n->id});
    return gNextDescription;
}
void __fastcall TreeAddRenderable(FakeNode *n, void *, Node *child)
{
    Renderable *r = RenderableOf(child);
    Ev({evAddChild | n->id, RenderableId(r)});
    r->parent = n;
}
void __fastcall TreeRemoveRenderable(FakeNode *n, void *, Node *child)
{
    Renderable *r = RenderableOf(child);
    Ev({evRemoveChild | n->id, RenderableId(r)});
    r->parent = nullptr;
}

// The fakes' tables, restored at the end.
struct Overrides {
    void *owner[kFakeOwnerSlots], *node[kFakeNodeSlots];
    template <class F> static void Set(void **table, unsigned slot, F f) { table[slot] = reinterpret_cast<void *>(f); }
    Overrides()
    {
        std::memcpy(owner, gFakeOwnerVT, sizeof(owner));
        std::memcpy(node, gFakeNodeVT, sizeof(node));
        Set(gFakeOwnerVT, 14, &OwnerMake);
        Set(gFakeOwnerVT, 21, &OwnerSettings);
        Set(gFakeOwnerVT, 24, &OwnerSource);
        Set(gFakeOwnerVT, 28, &OwnerCollider);
        Set(gFakeOwnerVT, 30, &OwnerConstraint);
        Set(gFakeOwnerVT, 43, &OwnerScene);
        Set(gFakeNodeVT, 45, &TreeAddRenderable);
        Set(gFakeNodeVT, 46, &TreeRemoveRenderable);
        Set(gFakeNodeVT, 94, &InstanceShape);
        for (void **table : {gSourceVT, gSurfaceVT, gColliderVT, gDeathVT, gDescriptionVT, gConstraintVT})
            std::memcpy(table, gFakeNodeVT, sizeof(gFakeNodeVT));
        Set(gConstraintVT, 3, &ConstraintSlot3);
        Set(gSourceVT, 7, &SourceSlot7);
        Set(gSurfaceVT, 3, &SurfaceSlot3);
        Set(gColliderVT, 3, &ColliderSlot3);
        Set(gDeathVT, 3, &DeathStart);
        Set(gDeathVT, 9, &DeathOwnPlace);
        Set(gDeathVT, 10, &DeathPlace);
        Set(gDescriptionVT, 3, &DescriptionFaceSlot3);
    }
    ~Overrides()
    {
        std::memcpy(gFakeOwnerVT, owner, sizeof(owner));
        std::memcpy(gFakeNodeVT, node, sizeof(node));
    }
};

// What a real renderable's constructor and destructor reach, for the whole test.
struct NodeWorld {
    sgrx::Services services;
    sgfx::RegistryScope nodeRegistry{sgfx::kNodeRegistry};
    sgfx::RegistryScope transformRegistry{sgfx::kTransformRegistry};
    drawfx::Cells cells;
    sgfx::Manager manager;
    shape::StandIn setup{0xbbad30u, &Node::Setup, &StandSetup};
    shape::StandIn build{0xbc03d0u, &Node::BuildMaterial, &StandBuild};
    NodeWorld()
    {
        std::mt19937 r(5);
        sgrx::Setup(r);
        manager.Use(cells, kEventCell);
        cells.Set(nGZSceneGraph::cTransformNodeBehaviorTraits::kEventManagerCell, manager.manager);
    }
    ~NodeWorld() { manager.Drop(); }
};

void ResetWorld(uint32_t seed, EffectsWorld &w)
{
    gArenaUsed = 0;
    std::memset(gArena, 0xcd, sizeof(gArena));
    gSettings = {};
    gSettings.mRenderLayer = int32_t(seed % 3) - 1;
    gSettings.mRateFactor = 1.0f;
    gSettings.mSizeFactor = float(1 + seed % 3) * 0.5f;
    gSettings.mDetail = float(seed % 5) * 0.25f;
    gSource = FakeNode{gSourceVT, 0x40, 0, nullptr};
    gSurface = FakeNode{gSurfaceVT, 0x41, 0, nullptr};
    gConstraint = FakeNode{gConstraintVT, 0x42, 0, nullptr};
    gShape = FakeNode{gFakeNodeVT, 0x43, 0, nullptr};
    gDescriptionFace = FakeNode{gDescriptionVT, 0x44, 0, nullptr};
    for (uint32_t i = 0; i < 4; ++i) gColliders[i] = FakeNode{gColliderVT, 0x60 + i, 0, nullptr};
    for (uint32_t i = 0; i < 4; ++i) gDeaths[i] = FakeNode{gDeathVT, 0x68 + i, 0, nullptr};
    gAnswers.seed(seed + 99);
    for (Renderable &r : gRenderables) {
        r.node = nullptr;
        r.parent = nullptr;
        r.children.clear();
    }
    gHaveSurface = false;
    gGiveShape = false;
    gDescriptions[0] = gDescriptions[1] = gDescriptions[2] = nullptr;
    w.owners[0].refs = 0;
    auto *random = GameAddress<cRZRandom>(0x10fd138);
    random->mIntegerSeed = seed * 2654435761u + 12345u;
    random->mRealSeed = int32_t(seed * 40503u + 77u);
    *GameAddress<void *>(Effect::kPoolFreeCell) = nullptr;
    *GameAddress<void *>(kPoolListCell) = nullptr;
    *GameAddress<uint32_t>(kPoolElementCell) = sizeof(cTSParticle);
    *GameAddress<uint32_t>(kPoolBlockCell) = 0x3e00;
}

// ---- random inputs ----

float Wild(std::mt19937 &r)
{
    switch (r() % 9) {
    case 0: return 0.0f;
    case 1: return 0.5f;
    case 2: return 1.0f;
    case 3: return -1.0f;
    case 4: return 123456.7f;
    case 5: return std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu) | (r() << 31));
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

// `tame`: what the game's step can run on (nothing goes on for ever, nothing leaves the pool).
void RandomDescription(cTSParticlesDescription &d, std::mt19937 &r, bool tame, uint32_t flags)
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
    if (!tame) {
        if (r() % 8 == 0) d.m28.x = Wild(r);
        if (r() % 8 == 0) d.m30.x = Wild(r);
    }
    d.mEmitVolume = {value(), value()};
    d.mSourceMin = {value(), value(), value()};
    d.mSourceMax = {value(), value(), value()};
    d.mForce = {value(), value(), value()};
    d.mSizeVary = value();
    d.mAlphaVary = value();
    RandomCurve(d.mSize, r, tame);
    RandomCurve(d.mAlpha, r, tame);
    d.mFrameStart = uint8_t(r());
    d.mFlags = flags;
    // The walk: a delay of a tick or more, or the clock's loop never ends.
    d.mWalkDelay = {0.0625f + Positive(r), 0.0625f + Positive(r)};
    d.mWalkTurn = {value(), value()};
    d.mWalkStrength = {value(), value()};
    static const char *const kAttached[] = {"", "death", "a_name_long_enough_to_leave_the_small_buffer"};
    d.mDeathEffect = kAttached[r() % 3];
    if (tame) d.mDeathEffect.clear();
    d.mConstrain = tame ? "" : kAttached[r() % 3];
    d.mAttractors.clear();
    const unsigned attractors = r() % 4;
    for (unsigned i = 0; i < attractors; ++i) {
        cTSParticleAttractor a;
        a.mHasCentre = uint8_t(r() % 3);
        a.mCentre = {value(), value(), value()};
        a.mStrength = value();
        d.mAttractors.push_back(a);
    }
    d.mColliders.clear();
    const unsigned colliders = tame ? 0 : r() % 4;
    for (unsigned i = 0; i < colliders; ++i) {
        cTSParticleCollider c;
        c.mName = kAttached[1 + r() % 2];
        d.mColliders.push_back(c);
    }
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

uint32_t EffectId(const void *p)
{
    if (!p) return 0;
    for (uint32_t i = 0; i < 2; ++i)
        if (static_cast<const cITSComponentEffect *>(gEffects[i]) == p) return i + 1;
    return 0xbad;
}

void PushParticle(Blob &b, const cTSParticle *p, bool first)
{
    b.push_back(ArenaOffset(p));
    const auto *w = reinterpret_cast<const uint32_t *>(p);
    b.insert(b.end(), w + (first ? 0 : 1), w + sizeof(cTSParticle) / 4);
}

void PushVectorShape(Blob &b, size_t size, size_t capacity)
{
    b.push_back(uint32_t(size));
    b.push_back(uint32_t(capacity));
}

// Every byte of the effect: the words that hold pointers by what they point at, the rest as they are.
// An element of a description's attractors or colliders (a state outlives the description it came from): which description, which element.
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

void PushEffectState(Blob &b, const Effect *e)
{
    const auto *w = reinterpret_cast<const uint32_t *>(e);
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    for (uint32_t o = 8; o < Effect::kSize; o += 4) {
        switch (o) {
        case 0x14: { // the list
            b.push_back(uint32_t(e->mParticles.size()));
            for (const cTSParticle *p : e->mParticles) PushParticle(b, p, true);
            break;
        }
        case 0x18: break;
        case 0x20: b.push_back(DescriptionId(e->mDescription)); break;
        case 0x24: b.push_back(IdOf(e->mOwner.Get())); break;
        case 0x28: b.push_back(IdOf(e->mParent.Get())); break;
        case 0x2c: b.push_back(IdOf(e->mScene.Get())); break;
        case 0x30: b.push_back(uint32_t(e->mSettings == &gSettings)); break;
        case 0x88:
            PushVectorShape(b, e->mAttractors.size(), e->mAttractors.capacity());
            for (const cTSParticleAttractorState &a : e->mAttractors) {
                                b.insert(b.end(), {Bits(a.mCentre.x), Bits(a.mCentre.y), Bits(a.mCentre.z), Bits(a.mStrength)});
                b.push_back(ElementId(a.mDescription, &cTSParticlesDescription::mAttractors));
            }
            break;
        case 0x8c:
        case 0x90: break;
        case 0x198: b.push_back(IdOf(e->mSurface.Get())); break;
        case 0x1c0: {
            const Node *n = e->mRenderable.Get();
            const Renderable *r = RenderableOf(n);
            b.push_back(RenderableId(n ? r : nullptr));
            if (n) {
                b.push_back(IdOf(n->mShape.mObject));
                b.push_back(shape::P(n->mEffect) == shape::P(e));
            }
            if (r && n) {
                b.push_back(IdOf(r->parent));
                b.push_back(uint32_t(r->children.size()));
                for (const FakeNode *c : r->children) b.push_back(c->id);
            }
            break;
        }
        case 0x1c4: { // the string's words: its text, and its count
            PushString(b, e->TextureOverride());
            b.push_back(uint32_t(e->TextureOverride().capacity()));
            b.push_back(e->mTextureOverride.mString.mRefCount);
            o = 0x1e0;
            break;
        }
        case 0x1e4:
            PushVectorShape(b, e->mColliders.size(), e->mColliders.capacity());
            for (const cTSParticleColliderState &c : e->mColliders) {
                b.push_back(IdOf(c.mObject));
                b.push_back(ElementId(c.mDescription, &cTSParticlesDescription::mColliders));
                b.push_back(c.mStateOffset);
            }
            break;
        case 0x1e8:
        case 0x1ec: break;
        case 0x1f4:
            PushVectorShape(b, e->mColliderState.size(), e->mColliderState.capacity());
            for (char c : e->mColliderState) b.push_back(uint8_t(c));
            break;
        case 0x1f8:
        case 0x1fc: break;
        case 0x200: b.push_back(IdOf(e->mConstrain.Get())); break;
        case 0x204: b.push_back(EffectId(e->mFollower.Get())); break;
        default: b.push_back(w[o / 4]);
        }
    }
}

void PushPool(Blob &b)
{
    b.push_back(0x0b0000e2u);
    for (const void *p = *GameAddress<void *>(Effect::kPoolFreeCell); p; p = *static_cast<void *const *>(p))
        PushParticle(b, static_cast<const cTSParticle *>(p), false);
    b.push_back(0x0a0000ffu);
    // The blocks the pool was carved from (RVA 0xbb5c70 and the other emitters: the list head at 0x107cfd0, each block's first word links on).
    b.push_back(0x0b0000e3u);
    {
        const uint8_t *block = *GameAddress<const uint8_t *>(0x107cfd0);
        for (int n = 0; block && n < 64; ++n) {
            b.push_back(ArenaOffset(block));
            block = *reinterpret_cast<const uint8_t *const *>(block);
        }
    }
    auto *random = GameAddress<cRZRandom>(0x10fd138);
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

void *MakeEffect(int mode, cTSParticlesDescription *d)
{
    void *const e = mode ? static_cast<void *>(Effect::Create(d)) : gCtx->Original<CreateFn>(kCreate)(d);
    auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
    // What the constructor leaves unset: the same bytes on both sides from here on (Particles_Effect_Construct compares them).
    auto *bytes = static_cast<uint8_t *>(e);
    std::memset(bytes + 0x34, 0x5a, 4);
    std::memset(bytes + 0x41, 0x5a, 7);
    std::memset(bytes + 0x54, 0x5a, 4);
    std::memset(bytes + 0xb8, 0x5a, 12);
    bytes[0xc5] = 0x5a;
    bytes[0xfd] = 0x5a;
    std::memset(bytes + 0x1f1, 0x5a, 3);
    std::memset(bytes + 0x209, 0x5a, 7);
    (void)object;
    return e;
}

// The pool as the emitters would leave it: `count` free particles from the arena.
void FillPool(uint32_t count, std::mt19937 &r)
{
    for (uint32_t i = 0; i < count; ++i) {
        auto *p = reinterpret_cast<cTSParticle *>(gArena + gArenaUsed);
        gArenaUsed += sizeof(cTSParticle);
        auto *w = reinterpret_cast<uint32_t *>(p);
        for (size_t k = 0; k < sizeof(cTSParticle) / 4; ++k) w[k] = Bits(Wild(r));
        void **const cell = GameAddress<void *>(Effect::kPoolFreeCell);
        *reinterpret_cast<void **>(p) = *cell;
        *cell = p;
    }
}
cTSParticle *TakeParticle(std::mt19937 &r)
{
    void **const cell = GameAddress<void *>(Effect::kPoolFreeCell);
    auto *p = static_cast<cTSParticle *>(*cell);
    if (!p) return nullptr;
    *cell = *reinterpret_cast<void **>(p);
    p->mAlive = uint8_t(r() % 2);
    p->mLife = r() % 2 ? 1.0f : Wild(r);
    p->mAge = r() % 2 ? 2.0f : Wild(r);
    p->mPosition = {Wild(r), Wild(r), Wild(r)};
    return p;
}

template <class Fn, class... A> void Helper(uint32_t rva, void *self, A... args)
{
    gCtx->Original<Fn>(rva)(self, args...);
}

struct Precision {
    difftest::X87Precision bits24{difftest::X87Pc::Bits24};
};

void *__cdecl ArenaMalloc(uint32_t bytes)
{
    Ev({evAlloc, bytes});
    if (gArenaUsed + bytes > kArenaBytes) std::abort();
    void *const p = gArena + gArenaUsed;
    gArenaUsed += bytes;
    return p;
}

} // namespace
