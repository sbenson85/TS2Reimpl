// Differential tests for cDiffusionManager's portal lights (lane 261110l, scenegraph LD1b; src/sg/cLightingManagerPortalLight.cpp,
// src/sg/cDiffusionManager.cpp): the helpers 0x3c3a70 (the light's corners), 0x3c5c30, 0x3c5a60 and 0x3c3870 and slots
// 8-11 (0x3c4f20, 0x3c5050, 0x3c48a0, 0x3c4b30). Hook shape: the original runs on the same image as ours.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_common.h"

#include "hook/hooks.h"
#include "sg/cDiffusionManager.h"
#include "misc/SmallVirtuals.h"
#include "sg/cLightingManager.h"
#include "world/WorldHeights.h"
#include "world/WorldTileMath.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

using nTSSG::cDiffusionManager;
using nTSSG::cPortalLight;

// ---- the fake scene (helpers 0x3c3870, 0x3c5a60, 0x3c5c30) ----
//
// Nodes (slot 0 the interface query, 21 the first extension, 34 the parent), extensions (slot 0 the query, 20 the next
// one) and diffusion faces / portal faces (a vbptr at +4 and the virtual base at +0xc: slot 1 AddRef, 2 Release; the
// faces' slots 21-30 the light's getters) log each call and its arguments. A query's out reference is logged on the way
// in; the extension's query hits, or writes its face and says no (the original leaves the reference for the next
// release), or does neither.
constexpr int kNodes = 5, kExts = 8, kFaces = 4;

struct Face {
    void **vt;
    const int32_t *vbptr; // +4: the virtual base at +0xc
    uint32_t id;
    void **baseVt;        // +0xc
    float min[2], max[2], intensity, color[3];
    uint32_t directional, falloff[5];
};
struct Ext {
    void **vt;
    uint32_t id;
    Ext *next;
    Face *face;
    uint32_t mode; // bit 0: the query hits; bit 1: it writes the face either way
};
struct Node {
    void **vt;
    uint32_t id;
    Node *parent;
    Ext *first;
    Face *face;    // the portal face the query gives
    uint32_t mode; // 1: hits; 2: writes the face and says no
};

Node gNodes[kNodes];
Ext gExts[kExts];
Face gFaces[kFaces];
void *gNodeVT[60], *gExtVT[24], *gFaceVT[124], *gBaseVT[4];
const int32_t kVbTable[2] = {-4, 8};
uint32_t gJunk;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kNodes; ++i)
        if (p == &gNodes[i]) return 0x100 + i;
    for (int i = 0; i < kExts; ++i)
        if (p == &gExts[i]) return 0x200 + i;
    for (int i = 0; i < kFaces; ++i)
        if (p == &gFaces[i]) return 0x300 + i;
    return 0xbad00000u | (P(p) & 0xfffff);
}

uint32_t __fastcall NodeQuery(Node *self, void *, uint32_t iid, void **out)
{
    L(0x10), L(Id(self)), L(iid), L(Id(*out));
    if (self->mode & 3) *out = self->face;
    return (gJunk & ~0xffu) | (self->mode & 1);
}
Ext *__fastcall NodeFirst(Node *self, void *) { return L(0x11), L(Id(self)), self->first; }
Node *__fastcall NodeParent(Node *self, void *) { return L(0x12), L(Id(self)), self->parent; }
uint32_t __fastcall ExtQuery(Ext *self, void *, uint32_t iid, void **out)
{
    L(0x20), L(Id(self)), L(iid), L(Id(*out));
    if (self->mode & 3) *out = self->face;
    return (gJunk & ~0xffu) | (self->mode & 1);
}
Ext *__fastcall ExtNext(Ext *self, void *) { return L(0x21), L(Id(self)), self->next; }
void __fastcall BaseAddRef(uint8_t *base, void *) { L(0x1001), L(Id(base - 0xc)); }
void __fastcall BaseRelease(uint8_t *base, void *) { L(0x1002), L(Id(base - 0xc)); }
const float *__fastcall FaceMin(Face *self, void *) { return L(0x30), L(Id(self)), self->min; }
const float *__fastcall FaceMax(Face *self, void *) { return L(0x31), L(Id(self)), self->max; }
float __fastcall FaceIntensity(Face *self, void *) { return L(0x32), L(Id(self)), self->intensity; }
const float *__fastcall FaceColor(Face *self, void *) { return L(0x33), L(Id(self)), self->color; }
uint32_t __fastcall FaceDirectional(Face *self, void *) { return L(0x34), L(Id(self)), self->directional; }
template <int K> float __fastcall FaceFalloff(Face *self, void *)
{
    L(0x35 + K), L(Id(self));
    return Fl(self->falloff[K]);
}

void FillFakeTables()
{
    gNodeVT[0] = reinterpret_cast<void *>(&NodeQuery);
    gNodeVT[21] = reinterpret_cast<void *>(&NodeFirst);
    gNodeVT[34] = reinterpret_cast<void *>(&NodeParent);
    gExtVT[0] = reinterpret_cast<void *>(&ExtQuery);
    gExtVT[20] = reinterpret_cast<void *>(&ExtNext);
    gBaseVT[1] = reinterpret_cast<void *>(&BaseAddRef);
    gBaseVT[2] = reinterpret_cast<void *>(&BaseRelease);
    gFaceVT[21] = reinterpret_cast<void *>(&FaceMin);
    gFaceVT[22] = reinterpret_cast<void *>(&FaceMax);
    gFaceVT[23] = reinterpret_cast<void *>(&FaceIntensity);
    gFaceVT[24] = reinterpret_cast<void *>(&FaceColor);
    gFaceVT[25] = reinterpret_cast<void *>(&FaceDirectional);
    gFaceVT[26] = reinterpret_cast<void *>(&FaceFalloff<0>);
    gFaceVT[27] = reinterpret_cast<void *>(&FaceFalloff<1>);
    gFaceVT[28] = reinterpret_cast<void *>(&FaceFalloff<2>);
    gFaceVT[29] = reinterpret_cast<void *>(&FaceFalloff<3>);
    gFaceVT[30] = reinterpret_cast<void *>(&FaceFalloff<4>);
}

// A graph from one draw: nodes whose parents sit further along (or none), each with 0..3 extensions in a chain, each
// extension with a face (or none) and a hit mode; the faces with drawn numbers.
void DrawScene(std::mt19937 &gen, bool wide)
{
    FillFakeTables();
    gJunk = gen();
    auto pick = [&](float scale) { return wide ? Wide(gen, scale) : Nice(gen, scale); };
    for (int i = 0; i < kFaces; ++i) {
        Face &f = gFaces[i];
        f = {gFaceVT, kVbTable, uint32_t(0x300 + i), gBaseVT, {}, {}, 0, {}, 0, {}};
        for (float &v : f.min) v = pick(2.0f);
        for (float &v : f.max) v = pick(2.0f);
        for (float &v : f.color) v = pick(2.0f);
        f.intensity = pick(3.0f);
        f.directional = gen() % 3 ? (gen() & 0xffffff00u) | (gen() % 2) : gen();
        for (uint32_t &v : f.falloff) v = U(pick(2.0f));
    }
    int next = 0;
    for (int i = 0; i < kExts; ++i) gExts[i] = {gExtVT, uint32_t(0x200 + i), nullptr, nullptr, 0};
    for (int i = 0; i < kExts; ++i) {
        Ext &e = gExts[i];
        e.face = gen() % 6 ? &gFaces[gen() % kFaces] : nullptr;
        const uint32_t m = gen() % 8;
        e.mode = m < 3 ? 1 : m < 5 ? 2 : 0;
    }
    for (int i = 0; i < kNodes; ++i) {
        Node &n = gNodes[i];
        n = {gNodeVT, uint32_t(0x100 + i), nullptr, nullptr, nullptr, 0};
        if (i + 1 < kNodes && gen() % 5) n.parent = &gNodes[i + 1 + gen() % (kNodes - i - 1)];
        n.face = gen() % 5 ? &gFaces[gen() % kFaces] : nullptr;
        const uint32_t m = gen() % 8;
        n.mode = m < 3 ? 1 : m < 5 ? 2 : 0;
        const int length = gen() % 4;
        Ext *previous = nullptr;
        for (int k = 0; k < length && next < kExts; ++k, ++next) {
            if (previous) previous->next = &gExts[next];
            else n.first = &gExts[next];
            previous = &gExts[next];
        }
    }
}

} // namespace

// 0x3c3a70: the box (+0x48..+0x54) turned by the angle (+0x30) and placed about the position (+0xc, +0x10). Drawn: all
// seven floats from the wide families (NaNs, infinities, zeros, denormals) and from ordinary angles, so each comparison
// of the corner pairs goes both ways and the equal case too.
DIFF_TEST(sg_diffusion_corners, 0x3c3a70)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x3c3a70);
    alignas(16) uint8_t object[sizeof(cPortalLight)];
    SidesAtBothPrecisions(ctx, "portal light corners", 12000, false, [&](uint32_t draw, int side) {
        std::mt19937 gen(draw * 2654435761u + 0x3c3a70u);
        for (auto &b : object) b = uint8_t(gen());
        const bool wide = gen() % 3 == 0;
        auto pick = [&](float scale) { return wide ? Wide(gen, scale) : Nice(gen, scale); };
        PutFloat(object, 0x30, pick(8.0f));
        for (uint32_t at : {0x48u, 0x4cu, 0x50u, 0x54u, 0x0cu, 0x10u}) PutFloat(object, at, pick(4.0f));
        if (gen() % 4 == 0) { // the box's two ends level with each other
            PutFloat(object, 0x50, Word<float>(object, 0x48));
            if (gen() % 2) PutFloat(object, 0x54, Word<float>(object, 0x4c));
        }
        if (gen() % 2 == 0) { // zeros of both signs: tied corners differing only in the sign of the zero
            for (uint32_t at : {0x48u, 0x4cu, 0x50u, 0x54u, 0x0cu, 0x10u}) PutFloat(object, at, gen() % 2 ? 0.0f : -0.0f);
        }
        ctx.Mark(side ? "ours" : "original");
        if (side == 0) original(object);
        else reinterpret_cast<cPortalLight *>(object)->PlaceCorners();
        Blob outcome;
        for (uint32_t k = 0; k < sizeof(object); k += 4) outcome.push_back(Word<uint32_t>(object, k));
        return outcome;
    });
}

// 0x3c3870: the light takes what the first diffusion extension of the node, or of the nearest ancestor with one, gives.
// Compared: the log (every call, in order), the light's bytes.
DIFF_TEST(sg_diffusion_fill, 0x3c3870)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x3c3870);
    alignas(16) uint8_t object[sizeof(cPortalLight)];
    SidesAtBothPrecisions(ctx, "portal light fill", 12000, false, [&](uint32_t draw, int side) {
        std::mt19937 gen(draw * 2654435761u + 0x3c3870u);
        for (auto &b : object) b = uint8_t(gen());
        DrawScene(gen, gen() % 3 == 0);
        Node *const start = gen() % 12 == 0 ? nullptr : &gNodes[gen() % kNodes];
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        if (side == 0) original(object, start);
        else reinterpret_cast<cPortalLight *>(object)->FillFromExtension(reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(start));
        Blob outcome = gLog;
        for (uint32_t k = 0; k < sizeof(object); k += 4) outcome.push_back(Word<uint32_t>(object, k));
        return outcome;
    });
}

// 0x3c5a60: the portal face of the node or its nearest ancestor that answers one (the reference released afterwards).
DIFF_TEST(sg_diffusion_portal_of, 0x3c5a60)
{
    auto original = ctx.Original<void *(__cdecl *)(void *)>(0x3c5a60);
    SidesAtBothPrecisions(ctx, "portal of", 6000, false, [&](uint32_t draw, int side) {
        std::mt19937 gen(draw * 2654435761u + 0x3c5a60u);
        DrawScene(gen, false);
        Node *const start = gen() % 10 == 0 ? nullptr : &gNodes[gen() % kNodes];
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        void *found = side == 0 ? original(start)
                                : cDiffusionManager::PortalOf(reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(start));
        Blob outcome = gLog;
        outcome.push_back(Id(found));
        return outcome;
    });
}

namespace {

// 0x3c5c30's world: an object (slot 42 writes a position through the pointer it is given, three floats, and answers that
// pointer, or now and then another), the partitions (slot 80, the level at a position, with junk above the low 16 bits),
// the system 0x20defb (slot 49 the lighting manager) and the lighting manager (slot 36, the lot value).
struct Thing {
    void **vt;
};
float gOther[3];
const float *gLastOut;
uint32_t gLevelSalt, gLotSalt;
void *gThingVT[100], *gPartVT[100], *gSysVT[60], *gLightVT[40];
Thing gThing, gPart, gSys, gLightMgr;

// Slot 42 takes one argument (ret 4): the original pushes the next call's last two arguments before it, and they stay
// for slot 80 (ret 0xc: the position, 0, 1).
float *__fastcall ThingPosition(Thing *, void *, float *out)
{
    L(0x42);
    const float v[3] = {Fl(Mix2(gLevelSalt, 1)), Fl(Mix2(gLevelSalt, 2)), Fl(Mix2(gLevelSalt, 3))};
    std::memcpy(out, v, 12); // all twelve bytes
    gLastOut = out;
    if (gLevelSalt & 0x10000) {
        const float w[3] = {Fl(Mix2(gLevelSalt, 4)), Fl(Mix2(gLevelSalt, 5)), Fl(Mix2(gLevelSalt, 6))};
        std::memcpy(gOther, w, 12);
        return gOther;
    }
    return out;
}
uint32_t __fastcall PartLevel(Thing *, void *, const float *position, uint32_t a, uint32_t b)
{
    L(0x80), L(position == gLastOut), L(position == gOther), L(a), L(b);
    LF(position, 3);
    return (Mix2(gLevelSalt, 7) & ~0xffffu) | (gLevelSalt & 0xff);
}
void *__cdecl StandSystem()
{
    L(0x20de);
    return &gSys;
}
void *__fastcall SysLighting(Thing *, void *)
{
    L(0x49);
    return &gLightMgr;
}
uint32_t __fastcall LightLotValue(Thing *, void *, uint32_t level)
{
    L(0x36), L(level);
    static const uint32_t values[] = {99, 100, 101, 0, 0xffff, 0x8000, 0x7fff, 0x10063, 0x10064, 0xffff0063u, 0x80000064u};
    return values[gLotSalt % 11];
}

} // namespace

// 0x3c5c30: is the lot value at the object's position (its level through the partitions) below 100, as an int16.
DIFF_TEST(sg_diffusion_lot_value, 0x3c5c30)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x3c5c30);
    gThingVT[42] = reinterpret_cast<void *>(&ThingPosition);
    gPartVT[80] = reinterpret_cast<void *>(&PartLevel);
    gSysVT[49] = reinterpret_cast<void *>(&SysLighting);
    gLightVT[36] = reinterpret_cast<void *>(&LightLotValue);
    gThing.vt = gThingVT, gPart.vt = gPartVT, gSys.vt = gSysVT, gLightMgr.vt = gLightVT;
    shape::CodePatch system{0x20defbu, shape::CodeOf(&StandSystem)};
    alignas(16) uint8_t manager[sizeof(cDiffusionManager)];
    SidesAtBothPrecisions(ctx, "lot value below 100", 4000, false, [&](uint32_t draw, int side) {
        std::mt19937 gen(draw * 2654435761u + 0x3c5c30u);
        for (auto &b : manager) b = uint8_t(gen());
        void *const partitions = &gPart;
        std::memcpy(manager + 0x24, &partitions, 4);
        gLevelSalt = gen(), gLotSalt = gen();
        std::memset(gOther, 0, sizeof gOther);
        gLastOut = nullptr;
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        const bool below = side == 0 ? (original(manager, &gThing) & 0xff) != 0
                                     : reinterpret_cast<cDiffusionManager *>(manager)->LotValueBelow100(&gThing);
        Blob outcome = gLog;
        outcome.push_back(below);
        return outcome;
    });
}

namespace {

// ---- slots 8 and 9 (0x3c4f20, 0x3c5050): the scene's portal nodes into slot 10 ----
//
// The manager is a buffer whose vtable holds slot 10 (it logs the node and the vector it is given, and sometimes adds a
// light, as the real one does); the system 0x20defb answers the scene manager (slot 33), whose slot 17 is the scene (slot
// 1 / 2 counted, slot 44 the range of three iterators); the nodes answer slot 120, the portal kind objects, whose slot
// 99 is a flag and slot 42 the position (0x3c5c30's world above).
constexpr int kSNodes = 4, kSKinds = 3;
struct SKind {
    void **vt;
    uint32_t id, flag;
};
struct SNode {
    void **vt;
    uint32_t id;
    SKind *kind;
};
struct SIter {
    void **vt;
    uint32_t id, pos;
};
SKind gSKinds[kSKinds];
SNode gSNodes[kSNodes];
SIter gSIters[3];
Thing gSceneMgr, gScene;
void *gSItems[5];
uint32_t gSCount, gEndJunk, gFlagJunk, gMgrAdd;
void *gSKindVT[100], *gSNodeVT[124], *gSceneMgrVT[20], *gSceneVT[48], *gSIterVT[8], *gMgrVT[16];

uint32_t Tag(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kSNodes; ++i)
        if (p == &gSNodes[i]) return 0x100 + i;
    for (int i = 0; i < kSKinds; ++i)
        if (p == &gSKinds[i]) return 0x200 + i;
    for (int i = 0; i < 3; ++i)
        if (p == &gSIters[i]) return 0x500 + i;
    if (p == &gScene) return 0x300;
    if (p == &gSceneMgr) return 0x400;
    return 0xbad00000u | (P(p) & 0xfffff);
}

void *__fastcall SysSceneManager(Thing *, void *) { return L(0x33), &gSceneMgr; }
void *__fastcall SceneManagerScene(Thing *, void *) { return L(0x17), &gScene; }
void __fastcall SceneAddRef(Thing *, void *) { L(0x101); }
void __fastcall SceneRelease(Thing *, void *) { L(0x102); }
void __fastcall SceneRange(Thing *, void *, void **range, uint32_t a, const uint32_t *filter)
{
    L(0x12c), L(a), L(filter[0] == P(GameAddress<const void>(nRZSceneGraph::cBranchNodesIIDFilter::kVTable))), L(filter[1]);
    for (int i = 0; i < 3; ++i) gSIters[i].pos = 0, range[i] = &gSIters[i];
}
uint32_t __fastcall IterEqualTo(SIter *self, void *, const SIter *other)
{
    L(0x704), L(Tag(self)), L(self->pos), L(Tag(other));
    return (gEndJunk & ~0xffu) | (self->pos >= gSCount ? 1u : 0u);
}
void *__fastcall IterGetNode(SIter *self, void *) { return L(0x705), L(Tag(self)), L(self->pos), gSItems[self->pos]; }
void __fastcall IterAdvance(SIter *self, void *) { L(0x706), L(Tag(self)), ++self->pos; }
void __fastcall IterDrop(SIter *self, void *) { L(0x702), L(Tag(self)); }
SKind *__fastcall NodeKind(SNode *self, void *) { return L(0x78), L(Tag(self)), self->kind; }
uint32_t __fastcall KindFlag(SKind *self, void *) { return L(0x63), L(Tag(self)), (gFlagJunk & ~0xffu) | self->flag; }
float *__fastcall KindPosition(SKind *self, void *, float *out)
{
    L(0x4242), L(Tag(self));
    return ThingPosition(nullptr, nullptr, out);
}
uint32_t __fastcall ManagerSlot10(void *, void *, SNode *node, std::vector<cPortalLight> *out)
{
    L(0xa), L(Tag(node)), L(out->size()), L(out->empty() ? 0u : *reinterpret_cast<const uint32_t *>(&out->front()));
    if (gMgrAdd & 1) {
        cPortalLight light;
        std::memset(&light, 0x5a, sizeof light);
        std::memcpy(&light, &node->id, 4);
        out->push_back(light);
    }
    gMgrAdd >>= 1;
    return gMgrAdd;
}

void FillSlotFakes()
{
    gThingVT[42] = reinterpret_cast<void *>(&ThingPosition);
    gPartVT[80] = reinterpret_cast<void *>(&PartLevel);
    gSysVT[49] = reinterpret_cast<void *>(&SysLighting);
    gLightVT[36] = reinterpret_cast<void *>(&LightLotValue);
    gSysVT[33] = reinterpret_cast<void *>(&SysSceneManager);
    gSceneMgrVT[17] = reinterpret_cast<void *>(&SceneManagerScene);
    gSceneVT[1] = reinterpret_cast<void *>(&SceneAddRef);
    gSceneVT[2] = reinterpret_cast<void *>(&SceneRelease);
    gSceneVT[44] = reinterpret_cast<void *>(&SceneRange);
    gSIterVT[2] = reinterpret_cast<void *>(&IterDrop);
    gSIterVT[4] = reinterpret_cast<void *>(&IterEqualTo);
    gSIterVT[5] = reinterpret_cast<void *>(&IterGetNode);
    gSIterVT[6] = reinterpret_cast<void *>(&IterAdvance);
    gSNodeVT[120] = reinterpret_cast<void *>(&NodeKind);
    gSKindVT[42] = reinterpret_cast<void *>(&KindPosition);
    gSKindVT[99] = reinterpret_cast<void *>(&KindFlag);
    gMgrVT[10] = reinterpret_cast<void *>(&ManagerSlot10);
    gThing.vt = gThingVT, gPart.vt = gPartVT, gSys.vt = gSysVT, gLightMgr.vt = gLightVT;
    gSceneMgr.vt = gSceneMgrVT, gScene.vt = gSceneVT;
}

void DrawSlotWorld(std::mt19937 &gen)
{
    gLevelSalt = gen(), gLotSalt = gen(), gEndJunk = gen(), gFlagJunk = gen(), gMgrAdd = gen();
    std::memset(gOther, 0, sizeof gOther);
    gLastOut = nullptr;
    for (int i = 0; i < kSKinds; ++i) gSKinds[i] = {gSKindVT, uint32_t(0x200 + i), uint32_t(gen() % 2)};
    for (int i = 0; i < kSNodes; ++i)
        gSNodes[i] = {gSNodeVT, uint32_t(0x100 + i), gen() % 4 ? &gSKinds[gen() % kSKinds] : nullptr};
    for (int i = 0; i < 3; ++i) gSIters[i] = {gSIterVT, uint32_t(0x500 + i), 0};
    gSCount = gen() % 6;
    for (uint32_t i = 0; i < 5; ++i) gSItems[i] = gen() % 9 ? &gSNodes[gen() % kSNodes] : nullptr;
}

template <typename Ours> void RunSlot(difftest::Context &ctx, const char *name, uint32_t rva, int draws, Ours ours)
{
    static_assert(sizeof(std::vector<cPortalLight>) == 12, "the game's vector is three pointers");
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(rva);
    FillSlotFakes();
    shape::CodePatch system{0x20defbu, shape::CodeOf(&StandSystem)};
    alignas(16) uint8_t manager[sizeof(cDiffusionManager)];
    SidesAtBothPrecisions(ctx, name, draws, false, [&](uint32_t draw, int side) {
        std::mt19937 gen(draw * 2654435761u + rva);
        for (auto &b : manager) b = uint8_t(gen());
        void *const partitions = &gPart;
        void **const table = gMgrVT;
        std::memcpy(manager + 0x24, &partitions, 4);
        std::memcpy(manager, &table, 4);
        DrawSlotWorld(gen);
        std::vector<cPortalLight> out(gen() % 4);
        for (cPortalLight &l : out) std::memset(&l, int(gen()), sizeof l);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        if (side == 0) original(manager, &out);
        else ours(reinterpret_cast<cDiffusionManager *>(manager), &out);
        Blob outcome = gLog;
        outcome.push_back(uint32_t(out.size()));
        for (const cPortalLight &l : out) outcome.push_back(*reinterpret_cast<const uint32_t *>(&l));
        return outcome;
    });
}

} // namespace

// 0x3c4f20: every node of the scene with a portal kind object goes to slot 10.
DIFF_TEST(sg_diffusion_slot8, 0x3c4f20)
{
    RunSlot(ctx, "slot 8", 0x3c4f20, 6000,
            [](cDiffusionManager *m, std::vector<cPortalLight> *out) { m->cDiffusionManager::PortalLightsOfScene(out); });
}

// 0x3c5050: as slot 8, but only a node whose kind answers no to its slot 99 and whose lot value is not below 100.
DIFF_TEST(sg_diffusion_slot9, 0x3c5050)
{
    RunSlot(ctx, "slot 9", 0x3c5050, 6000,
            [](cDiffusionManager *m, std::vector<cPortalLight> *out) { m->cDiffusionManager::PortalLightsOfScenePublicLots(out); });
}

namespace {

// ---- slot 10 (0x3c48a0): the lights of one portal node's children ----
//
// The portal node is one of the fake scene's faces (PortalOf answers faces, and the node is compared with them), whose
// slot 44 makes the range of three iterators over the scene's nodes (the children), slot 120 its object (slot 42 the
// position, one argument, the level's bits in the third word; slot 101 the tile bytes, one argument). The world (+0x20)
// answers slot 69; the partitions (+0x24) slot 80; the manager's slot 11 takes (node, the child's queried reference, the
// light) and says whether the light stays. 0x7dcb90 (the tile bytes to a position) is stood in on both sides.
struct SObj {
    void **vt;
    uint32_t id;
};
SObj gSObj;
Thing gWorldThing;
void *gSObjVT[110], *gWorldThingVT[80];
uint32_t gTileSalt, gWorldSalt, gSlot11Salt, gSlot11Junk;
int8_t *gTileBuffer;

void *__fastcall FaceObject(Face *self, void *) { return L(0x78), L(Id(self)), &gSObj; }
void __fastcall FaceRelease(Face *self, void *) { L(0x2002), L(Id(self)); }
void __fastcall PortalRange(Face *self, void *, void **range, uint32_t a, const uint32_t *filter)
{
    L(0x2c), L(Id(self)), L(a), L(filter[0] == P(GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable)));
    L(filter[1]), L(filter[2]);
    for (int i = 0; i < 3; ++i) gSIters[i].pos = 0, range[i] = &gSIters[i];
}
float *__fastcall ObjPosition(SObj *, void *, float *out)
{
    L(0x2a);
    const uint32_t v[3] = {Mix2(gTileSalt, 1), Mix2(gTileSalt, 2), (gTileSalt & 1) ? Mix2(gTileSalt, 3) : gTileSalt % 9};
    std::memcpy(out, v, 12); // all twelve bytes
    gLastOut = out;
    // Now and then the answer is another buffer: the level is read through the pointer that comes back (0x3c48e7).
    if (gTileSalt & 0x200) {
        const uint32_t w[3] = {Mix2(gTileSalt, 10), Mix2(gTileSalt, 11), (gTileSalt & 2) ? Mix2(gTileSalt, 12) : gTileSalt % 7};
        std::memcpy(gOther, w, 12);
        return gOther;
    }
    return out;
}
int8_t *__fastcall ObjTile(SObj *, void *, int8_t *out)
{
    L(0x65);
    out[0] = int8_t(Mix2(gTileSalt, 4)), out[1] = int8_t(Mix2(gTileSalt, 5)), out[2] = int8_t(Mix2(gTileSalt, 6));
    gTileBuffer = out;
    return out;
}
float *__cdecl StandTile(float *out, const int8_t *bytes)
{
    const float v[3] = {Fl(Mix2(gTileSalt, 7)), Fl(Mix2(gTileSalt, 8)), Fl(Mix2(gTileSalt, 9))};
    std::memcpy(out, v, 12); // all twelve bytes
    // The bytes are noted after the write: a vector buffer too small for it would have overwritten them.
    L(0x7dcb), L(bytes == gTileBuffer), L(uint8_t(bytes[0])), L(uint8_t(bytes[1])), L(uint8_t(bytes[2]));
    return out;
}
uint32_t __fastcall WorldInside(Thing *, void *, const float *point)
{
    L(0x45), L(U(point[0])), L(U(point[1])), L(U(point[2]));
    return (gWorldSalt & ~0xffu) | ((gWorldSalt >> 8) % 4 != 0 ? 1u : 0u);
}
// The light's words without the bytes of padding (a stack leftover in the original, +0x25..0x27 and +0x6a, +0x6b).
uint32_t PaddedWord(int index, uint32_t word)
{
    return index == 9 ? word & 0xffu : index == 26 ? word & 0xffffu : word;
}
uint32_t __fastcall ManagerSlot11(void *, void *, void *node, void *ref, uint32_t *light)
{
    L(0xb), L(Id(node)), L(Id(ref));
    for (int i = 0; i < 27; ++i) L(PaddedWord(i, light[i]));
    for (int i = 0; i < 27; ++i)
        if ((Mix2(gSlot11Salt, 20 + i) & 3) == 0) light[i] = Mix2(gSlot11Salt, i);
    gSlot11Salt = gSlot11Salt * 1664525u + 1013904223u;
    return (gSlot11Junk & ~0xffu) | ((gSlot11Salt >> 16) % 3 != 0 ? 1u : 0u);
}

} // namespace

// 0x3c48a0: for the node's own children (their portal face is the node) the light slot 11 fills, kept when it says yes.
DIFF_TEST(sg_diffusion_slot10, 0x3c48a0)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x3c48a0);
    FillSlotFakes();
    gFaceVT[2] = reinterpret_cast<void *>(&FaceRelease);
    gFaceVT[44] = reinterpret_cast<void *>(&PortalRange);
    gFaceVT[120] = reinterpret_cast<void *>(&FaceObject);
    gSObjVT[42] = reinterpret_cast<void *>(&ObjPosition);
    gSObjVT[101] = reinterpret_cast<void *>(&ObjTile);
    gWorldThingVT[69] = reinterpret_cast<void *>(&WorldInside);
    gMgrVT[11] = reinterpret_cast<void *>(&ManagerSlot11);
    shape::StandIn tile(0x7dcb90, &worldmath::TileBytesToPosition, &StandTile);
    alignas(16) uint8_t manager[sizeof(cDiffusionManager)];
    SidesAtBothPrecisions(ctx, "slot 10", 6000, false, [&](uint32_t draw, int side) {
        std::mt19937 gen(draw * 2654435761u + 0x3c48a0u);
        for (auto &b : manager) b = uint8_t(gen());
        DrawScene(gen, false);
        FillSlotFakes();
        gFaceVT[2] = reinterpret_cast<void *>(&FaceRelease);
        gFaceVT[44] = reinterpret_cast<void *>(&PortalRange);
        gFaceVT[120] = reinterpret_cast<void *>(&FaceObject);
        gSObj = {gSObjVT, 0x400};
        gWorldThing.vt = gWorldThingVT;
        void *const partitions = &gPart, *const world = &gWorldThing;
        void **const table = gMgrVT;
        std::memcpy(manager + 0x20, &world, 4);
        std::memcpy(manager + 0x24, &partitions, 4);
        std::memcpy(manager, &table, 4);
        gLevelSalt = gen(), gTileSalt = gen(), gWorldSalt = gen(), gSlot11Salt = gen(), gSlot11Junk = gen(), gEndJunk = gen();
        gLastOut = nullptr, gTileBuffer = nullptr;
        for (int i = 0; i < 3; ++i) gSIters[i] = {gSIterVT, uint32_t(0x500 + i), 0};
        gSCount = gen() % 6;
        for (uint32_t i = 0; i < 5; ++i) gSItems[i] = gen() % 9 ? &gNodes[gen() % kNodes] : nullptr;
        Face *const node = &gFaces[gen() % kFaces];
        std::vector<cPortalLight> out;
        out.reserve(12); // the original's growth is the game's allocator (0x2c9a10), not run here
        for (uint32_t n = gen() % 3; n > 0; --n) {
            cPortalLight l;
            std::memset(&l, int(gen()), sizeof l);
            out.push_back(l);
        }
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        const uint32_t count = side == 0 ? original(manager, node, &out)
                                         : reinterpret_cast<cDiffusionManager *>(manager)->cDiffusionManager::PortalLightsOfNode(
                                               reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(node), &out);
        Blob outcome = gLog;
        outcome.push_back(count);
        outcome.push_back(uint32_t(out.size()));
        for (const cPortalLight &l : out)
            for (uint32_t k = 0; k < sizeof l; k += 4) {
                const uint32_t w = *reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(&l) + k);
                outcome.push_back(PaddedWord(int(k / 4), w));
            }
        return outcome;
    });
}

namespace {

// ---- slot 11 (0x3c4b30): one light reference node's light ----
//
// The portal node (slot 120 its owner, logged and not used; slot 54 its world transform, all 0x1c bytes through the
// hidden pointer, which it answers) and the reference (one of the fake scene's nodes, so the light's fill walks its
// extensions and parents; slot 54 its world transform likewise). The world (+0x20) answers slot 71 (the light's cell as
// a tile), the partitions (+0x24) slot 80 (PartLevel above). 0x7ddd90 (the level of the ground under a point) is stood
// in on both sides: all twelve bytes written, the point logged after the write, and now and then another buffer
// answered (the original reads the level through what it answers). RotateVector 0x19c800 and the CRT's atan2 and fmod
// run as they are. The three .data vectors (the preset colour, the y and z axes) are drawn and restored.
constexpr uint32_t kPreset = 0x10da3d0, kYAxis = 0x100e7cc, kZAxis = 0x100e7d8;
struct PNode {
    void **vt;
    uint32_t id;
};
PNode gPNode;
void *gPNodeVT[124];
float gPlaced[7], gOwn[7];
uint32_t gInsideSalt, gGroundSalt, gGroundCalls;
int32_t gGroundOther[3];

void *__fastcall PNodeOwner(PNode *self, void *) { return L(0x2078), L(self->id), reinterpret_cast<void *>(uintptr_t(gJunk)); }
float *__fastcall PNodeTransform(PNode *self, void *, float *out)
{
    L(0x2036), L(self->id);
    std::memcpy(out, gOwn, 0x1c); // all 0x1c bytes
    return out;
}
float *__fastcall RefTransform(Node *self, void *, float *out)
{
    L(0x1036), L(Id(self));
    std::memcpy(out, gPlaced, 0x1c); // all 0x1c bytes
    return out;
}
uint32_t __fastcall WorldInsideTile(Thing *, void *, const uint8_t *tile)
{
    L(0x47), L(Word<uint32_t>(tile, 0)), L(Word<uint32_t>(tile, 4)), L(Word<uint32_t>(tile, 8)), L(tile[12]);
    return (gInsideSalt & ~0xffu) | ((gInsideSalt >> 8) % 5 != 0 ? 1u : 0u);
}
int32_t *__cdecl StandGround(int32_t *out, const float *in)
{
    const uint32_t call = gGroundCalls++;
    const int32_t v[3] = {int32_t(Mix2(gGroundSalt, 10 * call + 1)), int32_t(Mix2(gGroundSalt, 10 * call + 2)),
                          int32_t(Mix2(gGroundSalt, 10 * call + 3) % 7)};
    std::memcpy(out, v, 12); // all twelve bytes
    gLastOut = reinterpret_cast<const float *>(out);
    // The point is noted after the write: an out buffer too small for it would have overwritten it.
    L(0x7ddd), L(call), LF(in, 3);
    if (Mix2(gGroundSalt, 10 * call + 4) % 4 == 0) {
        gGroundOther[0] = 0x51, gGroundOther[1] = 0x52, gGroundOther[2] = int32_t(Mix2(gGroundSalt, 10 * call + 5));
        return gGroundOther;
    }
    return out;
}

// A float `k` ulps along from f, across zero as the ulp test counts (-0 and +0 the same).
float Ulps(float f, int k)
{
    const uint32_t bits = U(f);
    const int64_t ordered = (bits & 0x80000000u) ? -int64_t(bits & 0x7fffffffu) : int64_t(bits);
    const int64_t moved = ordered + k;
    return Fl(moved < 0 ? 0x80000000u | uint32_t(-moved) : uint32_t(moved));
}

// A rigid transform: a translation (each part now and then a signed zero), then a unit quaternion tilted from the z axis by an angle whose cosine is drawn
// around +-0.99 (where slot 11 turns to the node's y axis), or anywhere; now and then every float from the wide families.
void DrawRigid(std::mt19937 &gen, float *out)
{
    for (int i = 0; i < 3; ++i) {
        out[i] = gen() % 6 ? Nice(gen, 64.0f) : Wide(gen, 64.0f);
        if (gen() % 5 == 0) out[i] = gen() % 2 ? 0.0f : -0.0f; // the reach's zero sign shows on a zero translation
    }
    const double edge = std::acos(0.99);
    double tilt;
    switch (gen() % 6) {
    case 0: tilt = edge + (double(int(gen() % 2001) - 1000) * 1e-9); break;
    case 1: tilt = 3.141592653589793 - edge + (double(int(gen() % 2001) - 1000) * 1e-9); break;
    case 2: tilt = gen() % 2 ? 0.0 : 3.141592653589793; break;
    default: tilt = double(gen() % 100000) / 100000.0 * 3.141592653589793; break;
    }
    const double turn = double(gen() % 100000) / 100000.0 * 6.283185307179586;
    const double s = std::sin(tilt / 2), c = std::cos(tilt / 2);
    out[3] = float(s * std::cos(turn)), out[4] = float(s * std::sin(turn)), out[5] = 0.0f, out[6] = float(c);
    if (gen() % 4 == 0) { // a spin about z as well
        const double spin = double(gen() % 1000) / 1000.0 * 3.141592653589793;
        const double a = std::cos(spin), b = std::sin(spin);
        const float x = out[3], y = out[4], w = out[6];
        out[3] = float(x * a - y * b), out[4] = float(x * b + y * a), out[5] = float(w * b), out[6] = float(w * a);
    }
    if (gen() % 12 == 0)
        for (int i = 3; i < 7; ++i) out[i] = Wide(gen, 1.0f);
}

} // namespace

// 0x3c4b30: the light of one reference node: the preset colour, the extension's fill (none near the preset: no light),
// the reference's placement and the ground's level under it, the world's tile test, the facing (the turned down vector,
// or the node's turned y axis near vertical), its angle (x87 atan2 and fmod), the box's reach along the facing and the
// partitions' level there, the corners. Compared: the log (every call and what it is given, in order), the answer's low
// byte, the light's words (the padding masked), the three .data vectors.
DIFF_TEST(sg_diffusion_slot11, 0x3c4b30)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *, void *)>(0x3c4b30);
    SavedGlobals globals;
    globals.Save(kPreset, 12), globals.Save(kYAxis, 12), globals.Save(kZAxis, 12);
    shape::StandIn ground{0x7ddd90, &worldheights::WithLevelOfGround, &StandGround};
    alignas(16) uint8_t manager[sizeof(cDiffusionManager)];
    alignas(16) uint8_t light[sizeof(cPortalLight)];
    SidesAtBothPrecisions(ctx, "slot 11", 12000, false, [&](uint32_t draw, int side) {
        std::mt19937 gen(draw * 2654435761u + 0x3c4b30u);
        for (auto &b : manager) b = uint8_t(gen());
        for (auto &b : light) b = uint8_t(gen());
        const bool wide = gen() % 4 == 0;
        auto pick = [&](float scale) { return wide ? Wide(gen, scale) : Nice(gen, scale); };
        DrawScene(gen, wide);
        FillSlotFakes();
        gNodeVT[54] = reinterpret_cast<void *>(&RefTransform);
        gPNodeVT[54] = reinterpret_cast<void *>(&PNodeTransform);
        gPNodeVT[120] = reinterpret_cast<void *>(&PNodeOwner);
        gWorldThingVT[71] = reinterpret_cast<void *>(&WorldInsideTile);
        gPNode = {gPNodeVT, 0x600};
        gWorldThing.vt = gWorldThingVT;
        void *const partitions = &gPart, *const world = &gWorldThing;
        std::memcpy(manager + 0x20, &world, 4);
        std::memcpy(manager + 0x24, &partitions, 4);
        // The .data vectors: the preset zero (as loaded) or drawn; the axes as loaded or drawn.
        float preset[3] = {0.0f, 0.0f, 0.0f};
        if (gen() % 3 == 0)
            for (float &v : preset) v = pick(2.0f);
        float yAxis[3] = {0.0f, 1.0f, 0.0f}, zAxis[3] = {0.0f, 0.0f, 1.0f};
        if (gen() % 8 == 0)
            for (float &v : yAxis) v = pick(1.0f);
        if (gen() % 8 == 0)
            for (float &v : zAxis) v = pick(1.0f);
        std::memcpy(GameAddress<uint8_t>(kPreset), preset, 12);
        std::memcpy(GameAddress<uint8_t>(kYAxis), yAxis, 12);
        std::memcpy(GameAddress<uint8_t>(kZAxis), zAxis, 12);
        // Now and then every face's colour within a few ulps of the preset (intensity 1), across the 32-ulp edge.
        if (gen() % 3 == 0)
            for (Face &f : gFaces) {
                f.intensity = 1.0f;
                for (int i = 0; i < 3; ++i) {
                    const int spread = gen() % 4 == 0 ? 2 : 40;
                    f.color[i] = Ulps(preset[i], int(gen() % (2 * spread + 1)) - spread);
                }
            }
        // The box's min y (the reach): its sign, zero, NaN.
        if (gen() % 3 == 0)
            for (Face &f : gFaces) {
                const uint32_t m = gen() % 6;
                f.min[1] = m == 0 ? 0.0f : m == 1 ? -0.0f : m == 2 ? NaN(gen) : f.min[1];
            }
        DrawRigid(gen, gPlaced);
        DrawRigid(gen, gOwn);
        gInsideSalt = gen(), gGroundSalt = gen(), gLevelSalt = gen(), gJunk = gen();
        gGroundCalls = 0, gLastOut = nullptr;
        Node *const ref = &gNodes[gen() % kNodes];
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        const uint32_t made = side == 0 ? original(manager, &gPNode, ref, light) & 0xffu
                                        : uint32_t(reinterpret_cast<cDiffusionManager *>(manager)->cDiffusionManager::MakePortalLight(
                                              reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(&gPNode),
                                              reinterpret_cast<nGZSceneGraph::cILightRefNode *>(ref),
                                              reinterpret_cast<cPortalLight *>(light)));
        Blob outcome = gLog;
        outcome.push_back(made);
        for (uint32_t k = 0; k < sizeof light; k += 4) outcome.push_back(PaddedWord(int(k / 4), Word<uint32_t>(light, k)));
        globals.Append(outcome);
        return outcome;
    });
}
