// Differential tests for nTSSG::cLightingManager's objects' lights (lane 261110w, scenegraph LG3h;
// src/sg/cLightingManagerObjects.cpp): the name comparison 0x2e59d0 (StringsEqual), the extension search up a node's
// tree 0x2ca590 (cExtensionSearch::Construct), slot 11 0x2d14b0 (PrepareObjectLighting) and slot 13 0x2e4c40
// (ApplyObjectLighting). Hook shape: the original runs on the same manager image as ours.
//
// The world (sgobjfx below): the manager is 0x548 bytes of drawn floats with three light sets (0x98 / 0x9c / 0xa0, 0..3
// of them, the index 0xd0 in range or not, else the default set at 0x2d0), mField50 drawn wide and mObjectLightJitter
// (+0x70) drawn at 0, tiny, 1, above 1, NaN and a few others. The object (slot 120 its owner, whose slot 98 answers 8,
// 9, 7, 10, 0x108 or 8 / 9 with junk upper bytes; slot 44 a node range of 0..6 pool nodes, some of which the filter
// rejects (the fake iterator skips a node whose mask misses the filter's bit or whose iid is not the filter's), with
// begin / end iterators null now and then). The pool nodes answer QI (0x1d4ab0's slot 0: themselves or another node,
// AddRef'd), Release, the attributes 7 / 10 (GetAttribute always writes the value; DropLightCopy changes the answers
// and, now and then, GetLight's), the first extension 21, the tag 26 (null, empty, short, 16 or more, with and without
// "_designmodelight", shorter than it, mixed case), the parent 34 (a later node or null), the layer state 86 (its word
// equal to SkySampleKey(0) or not; into the caller's buffer or its own pair) and GetLight 95 (a dummy or null). The
// extensions chain (slot 20) and answer QI (slot 0) yes, no, or a write with a no; the diffusion extensions (vbptr at +4,
// their virtual base at +0xc logging AddRef / Release) log ResetFlags 0, SetSourceIntensity 3, SetSourceFalloff 6 and
// answer GetSourceIntensity 23 / GetSourceFalloff 26 (the falloff changes across SetSourceIntensity).
//
// Stand-ins on both sides for the callees not ours: 0xaeb5b0 (the "ObjectLighting" extension: null or a fake whose slot
// 15 writes "Strength" or not), 0xaead10 (whether the object wants lighting), 0xaeb3f0 (the extension made), 0x3aab80
// (the object's design-mode names: fills its four strings, empty, short or 16 and more) and 0x3ab990 (the design mode
// named: null or a fake whose slot 53 answers a light stem). Every fake call logs the probe: the manager's words (a
// hash), the random generator's state, the fade byte and the heap counts (blocks, bytes) at that moment, so a store,
// allocation or free moved across a call shows. The random generator 0x10d93f0 is seeded alike on both sides (its
// state drawn) and compared after; the fade byte 0xffde34 is drawn clear and set. Compared: the log, the manager's bytes,
// the sets, the pools' reference counts, the globals and the heap blocks each side made; at the x87's 53-bit and 24-bit
// precision.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_common.h"

#include "hook/hooks.h"
#include "misc/SmallVirtuals.h"
#include "rz/cRZString.h"
#include "sg/DesignMode.h"
#include "sg/SGUtilities.h"
#include "sg/cIDiffusionLightExtension.h"
#include "sg/cLightingManager.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace sgobjfx {

using nTSSG::cLightingManager;

constexpr uint32_t kManagerSize = 0x548, kSetSize = 0x214;
constexpr uint32_t kLightIid = 0x53d200a, kDiffIid = 0x8a561e23, kFilterMask = 0xf;
constexpr uint32_t kAttrOne = 0xe9d24d51, kAttrValue = 0x49e37821;
constexpr uint32_t kFade = 0xffde34, kRandom = 0x10d93f0;
constexpr int kNodes = 16, kExts = 10, kDiffs = 4, kObjects = 3, kOwners = 3, kModes = 3;

struct Diff {
    void **vt;
    const int32_t *vbptr; // +4: the virtual base at +0xc
    uint32_t id;
    void **baseVt;        // +0xc
    uint32_t refs;
    float intensity, falloff;
};
struct Ext {
    void **vt;
    uint32_t id;
    Ext *next;
    uint32_t answer; // 0 no, 1 writes and says yes, 2 writes and says no
    Diff *diff;
};
struct Node {
    void **vt;
    uint32_t id, refs;
    Node *parent;
    Ext *first;
    const char *tag;
    uint32_t pair[2];  // GetLayerState's answer
    uint32_t ownPair;  // answers its own pair rather than the caller's buffer
    uint32_t light;    // bit 0: GetLight answers a dummy; bit 1: DropLightCopy flips it
    uint32_t mask, iid; // what the filter sees
    float one, value;
    Node *qi;
};
struct Owner {
    void **vt;
    uint32_t id, kind;
};
struct Object {
    void **vt;
    uint32_t id;
    Owner *owner;
    Node *items[6];
    uint32_t count, rangeNulls;
};
struct Iter {
    void **vt;
    uint32_t id;
    Object *object;
    uint32_t pos;
};
struct Lighting {
    void **vt;
    uint32_t id, writes;
    float strength;
};
struct Mode {
    void **vt;
    uint32_t id, salt;
};

alignas(16) uint8_t gManager[kManagerSize];
alignas(16) uint8_t gSets[3][kSetSize];
Node gNodes[kNodes];
Ext gExts[kExts];
Diff gDiffs[kDiffs];
Object gObjects[kObjects];
Owner gOwners[kOwners];
Iter gIters[3];
Lighting gLighting;
Mode gModes[kModes];
uint32_t gDummyLight;
void *gNodeVT[100], *gExtVT[24], *gDiffVT[28], *gDiffBaseVT[4], *gObjectVT[124], *gOwnerVT[100], *gIterVT[8],
    *gLightingVT[16], *gModeVT[56];
const int32_t kVbTable[2] = {-4, 8};
const uint32_t *gFilter;              // the filter slot 44 was given (still alive while the range is)
difftest::AllocationCounter *gHeap;   // the case's counter (the probe's heap counts)
uint32_t gJunk, gWants, gModeSalt;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kNodes; ++i)
        if (p == &gNodes[i]) return 0x100 + i;
    for (int i = 0; i < kObjects; ++i)
        if (p == &gObjects[i]) return 0x200 + i;
    for (int i = 0; i < kExts; ++i)
        if (p == &gExts[i]) return 0x500 + i;
    for (int i = 0; i < kDiffs; ++i)
        if (p == &gDiffs[i]) return 0x580 + i;
    if (p == &gLighting) return 0x600;
    for (int i = 0; i < 3; ++i)
        if (p == &gIters[i]) return 0x700 + i;
    for (int i = 0; i < kOwners; ++i)
        if (p == &gOwners[i]) return 0x800 + i;
    for (int i = 0; i < kModes; ++i)
        if (p == &gModes[i]) return 0x900 + i;
    if (p == &gDummyLight) return 0x980;
    if (p == gManager) return 0xa00;
    return 0xbad00000u | (P(p) & 0xfffff);
}

// The probe every fake logs: the manager's words, the generator's state, the fade byte, the heap counts.
void Probe()
{
    uint32_t h = 0x811c9dc5u;
    for (uint32_t i = 0; i < kManagerSize; i += 4) h = (h ^ Word<uint32_t>(gManager, i)) * 0x01000193u;
    L(h);
    const auto *const random = GameAddress<const uint32_t>(kRandom);
    L(random[0]), L(random[1]), L(random[2]);
    L(*GameAddress<const uint8_t>(kFade));
    L(gHeap ? uint32_t(gHeap->Blocks()) : 0u), L(gHeap ? uint32_t(gHeap->Bytes()) : 0u);
}
uint32_t Junk(uint32_t answer) { return (Mix2(gJunk, answer) & ~0xffu) | answer; }

uint32_t EventKind(const void *event)
{
    const uint32_t vt = *static_cast<const uint32_t *>(event);
    return vt == P(GameAddress<const void>(0xdb07f4)) ? 1 : 0xbad;
}

// ---- the nodes ----
uint32_t __fastcall NodeQI(Node *self, void *, uint32_t iid, void **out)
{
    L(0x100), L(Id(self)), L(iid), L(Id(*out)), Probe();
    *out = self->qi;
    ++self->qi->refs;
    return Junk(1);
}
uint32_t __fastcall NodeAddRef(Node *self, void *) { return L(0x101), L(Id(self)), Probe(), ++self->refs; }
uint32_t __fastcall NodeRelease(Node *self, void *) { return L(0x102), L(Id(self)), Probe(), --self->refs; }
// The event sink: the event's words and the probe at that moment.
void __fastcall NodeSetAttribute(Node *self, void *, uint32_t id, uint32_t a, uint32_t event)
{
    const auto *e = reinterpret_cast<const uint32_t *>(uintptr_t(event));
    L(0x107), L(Id(self)), L(id), L(a), L(EventKind(e)), L(e[1]), Probe();
}
// The original leaves the event's value unset before the call on its picked path: the node always writes it.
void __fastcall NodeGetAttribute(Node *self, void *, uint32_t event, uint32_t id, uint32_t a)
{
    auto *e = reinterpret_cast<uint32_t *>(uintptr_t(event));
    L(0x10a), L(Id(self)), L(id), L(a), L(EventKind(e)), Probe();
    e[1] = id == kAttrOne ? U(self->one) : id == kAttrValue ? U(self->value) : 0x7fa5a5a5u;
}
Ext *__fastcall NodeFirst(Node *self, void *) { return L(0x115), L(Id(self)), Probe(), self->first; }
const char *__fastcall NodeTag(Node *self, void *) { return L(0x11a), L(Id(self)), Probe(), self->tag; }
Node *__fastcall NodeParent(Node *self, void *) { return L(0x122), L(Id(self)), Probe(), self->parent; }
// 0x2e4c40 hands slot 86 an 8-byte buffer (cILightRefNode::GetLayerState stores {mask, word}).
uint32_t *__fastcall NodeLayer(Node *self, void *, uint32_t *out)
{
    L(0x156), L(Id(self)), Probe();
    if (self->ownPair) return self->pair;
    out[0] = self->pair[0], out[1] = self->pair[1];
    return out;
}
// Dropping the light copy changes what the node answers next (the original reads them before and after).
void __fastcall NodeDrop(Node *self, void *)
{
    L(0x15c), L(Id(self)), Probe();
    self->one = self->one * 0.5f + 0.25f, self->value = -self->value;
    if (self->light & 2) self->light ^= 1;
}
void *__fastcall NodeGetLight(Node *self, void *)
{
    L(0x15f), L(Id(self)), Probe();
    return (self->light & 1) ? &gDummyLight : nullptr;
}

// ---- the extensions ----
uint32_t __fastcall ExtQI(Ext *self, void *, uint32_t iid, void **out)
{
    L(0x500), L(Id(self)), L(iid), L(Id(*out)), Probe();
    if (iid != kDiffIid || self->answer == 0) return Junk(0);
    *out = self->diff;
    ++self->diff->refs;
    return Junk(self->answer == 1 ? 1 : 0);
}
Ext *__fastcall ExtNext(Ext *self, void *) { return L(0x514), L(Id(self)), Probe(), self->next; }

void __fastcall DiffReset(Diff *self, void *) { L(0x580), L(Id(self)), Probe(); }
void __fastcall DiffSetIntensity(Diff *self, void *, float v)
{
    L(0x583), L(Id(self)), L(U(v)), Probe();
    self->falloff = self->falloff * 0.75f - 0.125f; // the falloff read after this changes
}
void __fastcall DiffSetFalloff(Diff *self, void *, float v) { L(0x586), L(Id(self)), L(U(v)), Probe(); }
float __fastcall DiffIntensity(Diff *self, void *) { return L(0x597), L(Id(self)), Probe(), self->intensity; }
float __fastcall DiffFalloff(Diff *self, void *) { return L(0x59a), L(Id(self)), Probe(), self->falloff; }
uint32_t __fastcall DiffBaseAddRef(uint8_t *base, void *)
{
    Diff *const d = reinterpret_cast<Diff *>(base - 0xc);
    return L(0x5a1), L(Id(d)), Probe(), ++d->refs;
}
uint32_t __fastcall DiffBaseRelease(uint8_t *base, void *)
{
    Diff *const d = reinterpret_cast<Diff *>(base - 0xc);
    return L(0x5a2), L(Id(d)), Probe(), --d->refs;
}

// ---- the object, its owner and the range ----
Owner *__fastcall ObjectOwner(Object *self, void *) { return L(0x278), L(Id(self)), Probe(), self->owner; }
uint32_t __fastcall OwnerKind(Owner *self, void *) { return L(0x862), L(Id(self)), Probe(), self->kind; }

bool Accepted(const Node *n) { return (n->mask & gFilter[1]) != 0 && n->iid == gFilter[2]; }
void Skip(Iter *it)
{
    while (it->pos < it->object->count && !Accepted(it->object->items[it->pos])) ++it->pos;
}
void __fastcall ObjectRange(Object *self, void *, void **range, uint32_t a, const uint32_t *filter)
{
    L(0x22c), L(Id(self)), L(a), Probe();
    L(filter[0] == P(GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable))), L(filter[1]),
        L(filter[2]);
    gFilter = filter;
    gIters[0].object = self, gIters[0].pos = 0;
    Skip(&gIters[0]);
    range[0] = &gIters[0];
    range[1] = (self->rangeNulls & 1) ? nullptr : &gIters[1];
    range[2] = (self->rangeNulls & 2) ? nullptr : &gIters[2];
}
uint32_t __fastcall IterEqual(Iter *self, void *, const Iter *other)
{
    L(0x704), L(Id(self)), L(self->pos), L(Id(other)), Probe();
    return Junk(self->object && self->pos < self->object->count ? 0 : 1);
}
Node *__fastcall IterGet(Iter *self, void *) { return L(0x705), L(Id(self)), L(self->pos), Probe(), self->object->items[self->pos]; }
void __fastcall IterNext(Iter *self, void *)
{
    L(0x706), L(Id(self)), L(self->pos), Probe();
    ++self->pos;
    Skip(self);
}
// The filter is still the derived one while the range is released (its destructor comes after).
void __fastcall IterRelease(Iter *self, void *)
{
    L(0x702), L(Id(self)), L(self->pos), Probe();
    L(gFilter && gFilter[0] == P(GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable)));
}

// ---- the stand-ins ----
void *__cdecl StandObjectLighting(void *object, const char *name, void *a)
{
    L(0xeb5b), L(Id(object)), L(Hash(name)), L(Id(a)), Probe();
    return gLighting.vt ? &gLighting : nullptr;
}
uint32_t __fastcall LightingValue(Lighting *self, void *, const char *name, float *out)
{
    L(0x60f), L(Id(self)), L(Hash(name)), L(U(*out)), Probe();
    if (self->writes) *out = self->strength;
    return Junk(self->writes);
}
uint32_t __cdecl StandWants(void *object, uint32_t kind)
{
    L(0xead1), L(Id(object)), L(kind), Probe();
    return Junk(gWants);
}
void *__cdecl StandMake(void *object, const char *name, uint32_t make)
{
    L(0xeb3f), L(Id(object)), L(Hash(name)), L(make & 0xff), Probe();
    return &gExts[0];
}

const char *const kNames[] = {"", "day", "Lamp", "wall_sconce", "night_mode_ceiling_lights", "kitchen_ceiling_fixture",
                              "LAMP", "x", "a_design_mode_name_longer_than_thirty_one_characters"};
uint32_t gNames[4]; // 0x3aab80's four strings, as kNames indexes
void __cdecl StandNames(void *object, cRZString *a, cRZString *b, cRZString *c, cRZString *d)
{
    L(0x3aab), L(Id(object)), Probe();
    cRZString *const out[4] = {a, b, c, d};
    for (int i = 0; i < 4; ++i) {
        L(out[i]->Strlen());
        out[i]->FromChar(kNames[gNames[i]]);
    }
    Probe();
}
void *__cdecl StandMode(void *object, cRZString *name)
{
    L(0x3ab9), L(Id(object)), L(Hash(name->ToChar())), L(name->Strlen()), Probe();
    const uint32_t h = Mix2(gModeSalt, Hash(name->ToChar()));
    return h % 4 == 0 ? nullptr : &gModes[h % kModes];
}
const char *const kStems[] = {"lamp", "LAMP", "wall_sconce", "Kitchen_Ceiling_Fixture", "", "other", "Lamp"};
const char *__fastcall ModeStem(Mode *self, void *, const char *name)
{
    L(0x9d4), L(Id(self)), L(Hash(name)), Probe();
    return kStems[Mix2(self->salt, Hash(name)) % std::size(kStems)];
}

void FillTables()
{
    gNodeVT[0] = reinterpret_cast<void *>(&NodeQI);
    gNodeVT[1] = reinterpret_cast<void *>(&NodeAddRef);
    gNodeVT[2] = reinterpret_cast<void *>(&NodeRelease);
    gNodeVT[7] = reinterpret_cast<void *>(&NodeSetAttribute);
    gNodeVT[10] = reinterpret_cast<void *>(&NodeGetAttribute);
    gNodeVT[21] = reinterpret_cast<void *>(&NodeFirst);
    gNodeVT[26] = reinterpret_cast<void *>(&NodeTag);
    gNodeVT[34] = reinterpret_cast<void *>(&NodeParent);
    gNodeVT[86] = reinterpret_cast<void *>(&NodeLayer);
    gNodeVT[92] = reinterpret_cast<void *>(&NodeDrop);
    gNodeVT[95] = reinterpret_cast<void *>(&NodeGetLight);
    gExtVT[0] = reinterpret_cast<void *>(&ExtQI);
    gExtVT[20] = reinterpret_cast<void *>(&ExtNext);
    gDiffVT[0] = reinterpret_cast<void *>(&DiffReset);
    gDiffVT[3] = reinterpret_cast<void *>(&DiffSetIntensity);
    gDiffVT[6] = reinterpret_cast<void *>(&DiffSetFalloff);
    gDiffVT[23] = reinterpret_cast<void *>(&DiffIntensity);
    gDiffVT[26] = reinterpret_cast<void *>(&DiffFalloff);
    gDiffBaseVT[1] = reinterpret_cast<void *>(&DiffBaseAddRef);
    gDiffBaseVT[2] = reinterpret_cast<void *>(&DiffBaseRelease);
    gObjectVT[44] = reinterpret_cast<void *>(&ObjectRange);
    gObjectVT[120] = reinterpret_cast<void *>(&ObjectOwner);
    gOwnerVT[98] = reinterpret_cast<void *>(&OwnerKind);
    gIterVT[2] = reinterpret_cast<void *>(&IterRelease);
    gIterVT[4] = reinterpret_cast<void *>(&IterEqual);
    gIterVT[5] = reinterpret_cast<void *>(&IterGet);
    gIterVT[6] = reinterpret_cast<void *>(&IterNext);
    gLightingVT[15] = reinterpret_cast<void *>(&LightingValue);
    gModeVT[53] = reinterpret_cast<void *>(&ModeStem);
}

struct Globals : SavedGlobals {
    Globals()
    {
        Save(kFade, 1);
        Save(kRandom, 12);
    }
};

// ---- the drawn world ----

const char *const kTags[] = {nullptr, "", "lamp", "ceilinglight_main", "_designmodelight", "Lamp_DesignModeLight",
                             "kitchen_ceiling_fixture_designmodelight", "designmodelight", "LAMP_DESIGNMODELIGHTx",
                             "wall_sconce_designmodelight", "Night_Mode_Ceiling_Lights_DesignModeLight", "x_designmodelight"};
const char *const kCallNames[] = {"lamp", "LAMP", "Lamp_DesignModeLight", "", "kitchen_ceiling_fixture_designmodelight",
                                  "ceilinglight_MAIN", "wall"};

struct World {
    Object *object;
    const char *name;
};

// Floats from the families the products need (NaN, infinite, zero, negative, denormal, plain).
float Value(std::mt19937 &gen) { return gen() % 3 ? Wide(gen, 2.0f) : float(gen() % 5) * 0.5f; }

World Draw(uint32_t seed, int mode)
{
    std::mt19937 gen(seed * 2654435761u + 0x2e4c40u + uint32_t(mode) * 0x1000000u);
    World w{};
    gJunk = gen(), gWants = gen() % 2, gModeSalt = gen();
    for (uint32_t &n : gNames) n = gen() % 3 ? gen() % std::size(kNames) : 2 + gen() % 2;
    // the manager and its sets
    for (uint32_t i = 0; i < kManagerSize; i += 4) PutFloat(gManager, i, Value(gen));
    for (auto &set : gSets)
        for (uint32_t i = 0; i < kSetSize; i += 4) PutFloat(set, i, Value(gen));
    const uint32_t sets = gen() % 4;
    PutWord(gManager, 0, 0);
    PutWord(gManager, 0x98, P(gSets)), PutWord(gManager, 0x9c, P(gSets) + sets * kSetSize), PutWord(gManager, 0xa0, P(gSets) + 3 * kSetSize);
    static const uint32_t kIndexes[] = {0, 1, 2, 3, 0xffffffff, 0x80000000};
    PutWord(gManager, 0xd0, kIndexes[gen() % std::size(kIndexes)]);
    static const float kJitters[] = {0.0f, 1e-7f, 1.0f, 1.5f, 0.1f, 0.25f, 3.0f, -0.0f};
    PutFloat(gManager, 0x70, gen() % 8 ? kJitters[gen() % std::size(kJitters)] : NaN(gen));
    // the globals
    *GameAddress<uint8_t>(kFade) = gen() % 2 ? 0 : uint8_t(gen() % 3 ? 1 : 0x80);
    uint32_t *const random = GameAddress<uint32_t>(kRandom);
    random[0] = gen(), random[1] = gen() % 4 ? gen() : uint32_t(-int32_t(gen() % 1000)), random[2] = gen();
    // the pools
    for (int i = 0; i < kDiffs; ++i)
        gDiffs[i] = {gDiffVT, kVbTable, 0x580u + i, gDiffBaseVT, gen() % 4, Value(gen), Value(gen)};
    for (int i = kExts - 1; i >= 0; --i) {
        Ext &e = gExts[i];
        e.vt = gExtVT, e.id = 0x500u + i;
        e.next = i + 1 < kExts && gen() % 2 ? &gExts[i + 1 + gen() % (kExts - i - 1)] : nullptr;
        e.answer = gen() % 3;
        e.diff = &gDiffs[gen() % kDiffs];
    }
    static const uint32_t kOwnerKinds[] = {8, 9, 7, 10, 0x108, 0x10008, 0xffff0009u, 8, 9, 0};
    for (int i = 0; i < kOwners; ++i) gOwners[i] = {gOwnerVT, 0x800u + i, kOwnerKinds[gen() % std::size(kOwnerKinds)]};
    const uint32_t key = static_cast<uint32_t>(nTSSG::SkySampleKey(0));
    for (int i = kNodes - 1; i >= 0; --i) {
        Node &n = gNodes[i];
        n.vt = gNodeVT, n.id = 0x100u + i, n.refs = gen() % 4;
        n.parent = i + 1 < kNodes && gen() % 3 ? &gNodes[i + 1 + gen() % (kNodes - i - 1)] : nullptr;
        n.first = gen() % 4 ? &gExts[gen() % kExts] : nullptr;
        n.tag = kTags[gen() % std::size(kTags)];
        n.pair[0] = gen(), n.pair[1] = gen() % 2 ? key : (gen() % 2 ? key ^ 1u : gen());
        n.ownPair = gen() % 4 == 0;
        n.light = gen() % 4;
        n.mask = gen() % 6 ? 1u << (gen() % 4) : 0x10;
        n.iid = gen() % 6 ? kLightIid : 0x4512dd13u;
        n.one = Value(gen), n.value = Value(gen);
    }
    for (int i = 0; i < kNodes; ++i) gNodes[i].qi = gen() % 5 ? &gNodes[i] : &gNodes[gen() % kNodes];
    for (int i = 0; i < kObjects; ++i) {
        Object &o = gObjects[i];
        o.vt = gObjectVT, o.id = 0x200u + i, o.owner = &gOwners[gen() % kOwners];
        for (auto &item : o.items) item = &gNodes[gen() % kNodes];
        static const uint32_t kCounts[] = {0, 1, 1, 2, 3, 4, 6, 6};
        o.count = kCounts[gen() % std::size(kCounts)];
        o.rangeNulls = gen() % 6 ? 0 : 1 + gen() % 3;
    }
    for (int i = 0; i < 3; ++i) gIters[i] = {gIterVT, 0x700u + i, nullptr, 0};
    gLighting = {gen() % 6 ? gLightingVT : nullptr, 0x600, gen() % 4 != 0, Value(gen)};
    for (int i = 0; i < kModes; ++i) gModes[i] = {gModeVT, 0x900u + i, uint32_t(gen())};
    gFilter = nullptr;
    w.object = &gObjects[gen() % kObjects];
    w.name = gen() % 2 ? nullptr : kCallNames[gen() % std::size(kCallNames)];
    // With a name the original compares the tag to it (_stricmp): no null tags then.
    if (w.name)
        for (Node &n : gNodes)
            if (!n.tag) n.tag = "lamp";
    return w;
}

Blob Snapshot(const Globals &globals, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    for (uint32_t i = 0; i < kManagerSize; i += 4) o.push_back(Word<uint32_t>(gManager, i));
    for (const auto &set : gSets)
        for (uint32_t i = 0; i < kSetSize; i += 4) o.push_back(Word<uint32_t>(set, i));
    for (const Node &n : gNodes) o.push_back(n.refs), o.push_back(U(n.one)), o.push_back(U(n.value)), o.push_back(n.light);
    for (const Diff &d : gDiffs) o.push_back(d.refs), o.push_back(U(d.falloff));
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

struct Stands {
    shape::StandIn lighting{0xaeb5b0u, &nGZSceneGraph::FindDataList, &StandObjectLighting};
    shape::StandIn wants{0xaead10u, &nGZSceneGraph::SubtreeHasFlag, &StandWants};
    shape::StandIn make{0xaeb3f0u, &nGZSceneGraph::GetOrAddDataList, &StandMake};
    shape::StandIn names{0x3aab80u, &::DesignMode::GetObjectDesignModeNames, &StandNames};
    shape::StandIn mode{0x3ab990u, &::DesignMode::FindObjectDesignMode, &StandMode};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    Globals globals;
    Stands stands;
    Scope()
    {
        FillTables();
        gLog.reserve(1 << 15);
    }
};

template <class Call> void Run(difftest::Context &ctx, const Scope &scope, const char *what, uint32_t cases, int mode, Call call)
{
    SidesAtBothPrecisions(ctx, what, cases, true, [&](uint32_t draw, int side) {
        const World w = Draw(draw, mode);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        difftest::FreeRecorder frees; // the frees' sizes in order (coordinator mutants LW: two temporaries' destructors swapped)
        gHeap = &heap;
        call(side, w);
        gHeap = nullptr;
        heap.Stop();
        frees.Stop();
        Blob o = Snapshot(scope.globals, heap.Blocks(), heap.Bytes());
        o.push_back(uint32_t(frees.Count())), o.push_back(frees.Overflowed());
        for (size_t i = 0; i < frees.Count(); ++i) o.push_back(uint32_t(frees.Size(i)));
        return o;
    });
}

} // namespace sgobjfx

// 0x2e59d0: two names equal (strcmp), over a pool of names in every pair: equal, a case apart, a prefix, empty, long,
// high bytes.
DIFF_TEST(sg_lighting_objects_strings, 0x2e59d0)
{
    auto original = ctx.Original<uint32_t(__cdecl *)(const char *, const char *)>(0x2e59d0);
    static const char *const kPool[] = {"", "a", "A", "ab", "abc", "abd", "Lamp", "lamp", "lamp_", "\x80x", "\x7fx",
                                        "Night_Mode_Ceiling_Lights", "Night_Mode_Ceiling_Lightz", "night_mode_ceiling_lights"};
    for (uint32_t i = 0; i < std::size(kPool); ++i)
        for (uint32_t j = 0; j < std::size(kPool); ++j) {
            char a[40], b[40];
            std::strcpy(a, kPool[i]), std::strcpy(b, kPool[j]); // separate copies: equal texts at different addresses
            const uint32_t theirs = original(a, b) & 0xff;
            const uint32_t ours = nTSSG::StringsEqual(a, b) ? 1u : 0u;
            ++ctx.cases;
            svcfx::Same(ctx, svcfx::Blob{theirs}, svcfx::Blob{ours}, "strings equal", i * 64 + j);
        }
}

// 0x2ca590: the first diffusion extension up a node's tree (or another interface), with its holder's words.
DIFF_TEST(sg_lighting_objects_search, 0x2ca590)
{
    using namespace sgobjfx;
    Scope scope;
    using Construct = void *(__thiscall *)(void *, void *, uint32_t);
    auto original = ctx.Original<Construct>(0x2ca590);
    auto ours = reinterpret_cast<Construct>(MemberAddress<&nTSSG::cExtensionSearch::Construct>());
    Run(ctx, scope, "extension search", 3000, 0, [&](int side, const World &) {
        std::mt19937 gen(gJunk);
        uint32_t search[2] = {gen(), gen()};
        Node *const node = &gNodes[gen() % kNodes];
        const uint32_t iid = gen() % 5 ? kDiffIid : 0x1c8e36aau;
        void *const back = side == 0 ? original(search, node, iid) : ours(search, node, iid);
        L(back == search), L(Id(reinterpret_cast<void *>(uintptr_t(search[0])))), L(Id(reinterpret_cast<void *>(uintptr_t(search[1])))), Probe();
    });
}

// 0x2d14b0: an object readied for lighting: its lights switched off (owner kinds 8 / 9) or its "ObjectLighting"
// extension made.
DIFF_TEST(sg_lighting_objects_prepare, 0x2d14b0)
{
    using namespace sgobjfx;
    Scope scope;
    using Prepare = void(__thiscall *)(void *, void *);
    auto original = ctx.Original<Prepare>(0x2d14b0);
    auto ours = reinterpret_cast<Prepare>(MemberAddress<&cLightingManager::PrepareObjectLighting>());
    Run(ctx, scope, "prepare object", 3000, 1, [&](int side, const World &w) {
        if (side == 0) original(gManager, w.object);
        else ours(gManager, w.object);
    });
}

// 0x2e4c40: the object's light-reference nodes lit at the "ObjectLighting" strength, or switched off.
DIFF_TEST(sg_lighting_objects_apply, 0x2e4c40)
{
    using namespace sgobjfx;
    Scope scope;
    using Apply = void(__thiscall *)(void *, void *, const char *);
    auto original = ctx.Original<Apply>(0x2e4c40);
    auto ours = reinterpret_cast<Apply>(MemberAddress<&cLightingManager::ApplyObjectLighting>());
    Run(ctx, scope, "apply object lighting", 4000, 2, [&](int side, const World &w) {
        if (side == 0) original(gManager, w.object, w.name);
        else ours(gManager, w.object, w.name);
    });
}
