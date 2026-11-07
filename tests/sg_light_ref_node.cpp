// Differential tests of nGZSceneGraph::cLightRefNode (lane TE, scenegraph N2r part 2): the game's light-reference node
// (its factory 0xa61a80, its vtables 0xe70334 / 0xe704c4 and the bodies behind them) against ours (Create, our vtables),
// each side building its own nodes. In half the seeds our side builds through the constructor hook's Construct in fresh
// memory (0xa60c00's replacement) instead of Create.
//  - Factory: a fresh node's words, flags and count, then let go of by Release (the deleting destructor 0xa61150, the
//    destructor 0xa60ea0, the light-reference level 0xa60de0 and the levels below), or by the destructor 0xa60ea0 against
//    Destruct, or the deleting destructor 0xa61150 against DeletingDestructor; the constructor 0xa60c00 in place against
//    Construct (the light-reference level 0xa60a20 and the levels below, the Impl's 0xa60c20).
//  - Slots: trees of nodes driven through QueryInterface 0xa63360 (and through the +8 face: its QueryInterface and
//    Release thunks 0xa63392 / 0xa63515), IsKindOf 0xa62e20 (slot 25 0xa636d0), Accept /
//    AcceptReadOnly 0xa636e0, slot 70 0xa631d0, the light-reference slots 87-97 (DropLightCopy 0xa63520, SetLight
//    0xa636c0 -> 0xa62d80, the shared getters and setters), GetEventCount 0xa63300, GetEventIds 0xa611f0,
//    GetEventAttribute 0xa61620 -> 0xa62230 (the registry 0xa633d0 with the light attributes planted), the events 3
//    0xa61b70 -> 0xa623a0 (heard by a listener on 0xe9dd42cf and 0x1d05597b), the attributes 7 0xa63710 -> 0xa62ef0
//    and 10 0xa633a0 -> 0xa62a80, the listeners told 5 (0xa63640) and asked 6 (0xa61a40), against light fakes
//    (cILight 0x9c5ed164 with the standard face, or without).
//  - Clone: 15 (0xa61720, the copying 0xa631f0) with and without a clone context, in each mode, of nodes with lights and
//    copies.
//  - Persist: Write 0xa63730 -> 0xa63120, Read 0xa633b0 -> 0xa62c00 (versions before 10 and from 10), GetTypeInfo
//    0xa636f0 -> 0xa62e40.
//  - Bounds: nodes with lights (with or without a bound shape, and copies) under transforms: the boxes 71 / 72 (0xa632c0
//    / 0xa632e0 -> 0xa628f0 / 0xa62730), the hits 73 / 74 / 75 (0xa63260 / 0xa63280 / 0xa63290 -> 0xa624e0 / 0xa625a0 /
//    0xa62650), the draw 82 (0xa611a0 -> 0xa61d70) and the cull 83 (0xa61180 -> 0xa61bb0) against a view (the viewer's
//    view record: a fake display list, a frustum, a frame).
// Left out of the comparison: the vtable words 0 and 2 (each side's own) and word 29 (the Impl's +0x68, the detail limit
// the renderable constructor never writes: planted by the test).
#include "sg_renderable_fx.h"
#include "sg_transform_pilot.h"
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "sg/cLightRefNode.h"
#include "sg/cNullArrayAttributeEntryIterator.h"
#include "sg/cTransformNode.h"
#include "sg/cViewerRefNodeBaseImpl.h"

#include <array>
#include <bit>
#include <cfloat>
#include <cstring>
#include <malloc.h>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kFactory = 0xa61a80;
constexpr uint32_t kSize = cLightRefNode::kSize;
// Word 34 is the flag byte and three bytes of padding nobody writes: its byte is noted on its own (NoteNodes).
constexpr uint64_t kSkipWords = 0b101ull | 1ull << 29 | 1ull << 34;
constexpr uint32_t kLightIid = 0x9c5ed164;
constexpr uint32_t kEventCell = 0x10f6380;
constexpr uint32_t kTransformTag = 0x7a000000;
constexpr uint32_t kViewTag = 0x79000000;

// The light-reference behaviour's registry (0xa633d0, game code on both sides): its map 0x10f6374 and guard 0x10f637c,
// its holder 0xa606d0's once byte 0x10f6384 and guard 0x10f638c.
constexpr sgfx::RegistryStatics kLightRefRegistry{0x10f6374, 0x10f637c, 0x10f6384, 0x10f638c};

constexpr uint32_t kLightAttributes[] = {cLightRefNodeImpl::kConeFalloffAngle, cLightRefNodeImpl::kRange,
                                         cLightRefNodeImpl::kColor,            cLightRefNodeImpl::kConeAngle,
                                         cLightRefNodeImpl::kFalloff,          cLightRefNodeImpl::kFactor,
                                         cLightRefNodeImpl::kIntensity};

template <unsigned N> constexpr std::array<Slot, N> BaseSlots()
{
    std::array<Slot, N> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    return a;
}

// A light (cILight with cIStandardLight's slots): the setters change what the getters answer.
constexpr std::array<Slot, 58> MakeLightSlots()
{
    std::array<Slot, 58> a = BaseSlots<58>();
    a[5] = {3, Ret::Int, true, "NotifyListeners", "uua"};
    a[15] = {4, Ret::Int, true, "Clone", "uuaa"};
    a[17] = {0, Ret::Int, true, "GetReferent", ""};
    a[29] = {1, Ret::Int, true, "AddReferrer", "a"};
    a[30] = {1, Ret::Int, true, "RemoveReferrer", "a"};
    a[35] = {0, Ret::Int, true, "GetStandardLight", ""};
    a[37] = {0, Ret::Int, true, "GetBoundShape", ""};
    a[41] = {1, Ret::Int, true, "SetColor", "F3"};
    a[49] = {0, Ret::Int, true, "GetLightName", ""};
    a[42] = {1, Ret::Int, true, "SetIntensity", "f"};
    a[43] = {1, Ret::Int, true, "SetFactor", "f"};
    a[44] = {1, Ret::Int, true, "SetFalloff", "f"};
    a[45] = {1, Ret::Int, true, "SetRange", "f"};
    a[47] = {1, Ret::Int, true, "SetConeAngle", "f"};
    a[48] = {1, Ret::Int, true, "SetConeFalloffAngle", "f"};
    a[51] = {1, Ret::Int, true, "GetColor", "i"};
    a[52] = {0, Ret::Float, true, "GetIntensity", ""};
    a[53] = {0, Ret::Float, true, "GetFactor", ""};
    a[54] = {0, Ret::Float, true, "GetFalloff", ""};
    a[55] = {0, Ret::Float, true, "GetRange", ""};
    a[56] = {0, Ret::Float, true, "GetConeAngle", ""};
    a[57] = {0, Ret::Float, true, "GetConeFalloffAngle", ""};
    return a;
}
constexpr auto kLightSlots = MakeLightSlots();
constexpr drawfx::Iface kLight{"Light", 58, kLightSlots.data()};

// A bound (cIBound): 20 the placed test, 37-39 the boxes (six floats written, the box returned).
constexpr std::array<Slot, 40> MakeBoundSlots()
{
    std::array<Slot, 40> a = BaseSlots<40>();
    a[20] = {3, Ret::Int, true, "IntersectsBoundInAt", "F13aF13"};
    a[37] = {2, Ret::Int, true, "GetBoundBoxIn", "iF13"};
    a[38] = {2, Ret::Int, true, "GetBoundBoxRigid", "iF7"};
    a[39] = {1, Ret::Int, true, "GetBoundBox", "i"};
    return a;
}
constexpr auto kBoundSlots = MakeBoundSlots();
constexpr drawfx::Iface kBound{"Bound", 40, kBoundSlots.data()};

constexpr std::array<Slot, 30> MakeLinearSlots()
{
    std::array<Slot, 30> a = BaseSlots<30>();
    a[18] = {2, Ret::Int, true, "IntersectsBoundIn", "aF13"};
    a[29] = {3, Ret::Int, true, "FirstHitBoundIn", "iaF13"};
    return a;
}
constexpr auto kLinearSlots = MakeLinearSlots();
constexpr drawfx::Iface kLinear{"Linear", 30, kLinearSlots.data()};

// The display list a view holds: 26 the node told of with the plane mask, 30 a light with its frame in the view.
constexpr std::array<Slot, 31> MakeDrawerSlots()
{
    std::array<Slot, 31> a = BaseSlots<31>();
    a[26] = {2, Ret::Int, true, "AddNode", "au"};
    a[30] = {2, Ret::Int, true, "AddLight", "aF13"};
    return a;
}
constexpr auto kDrawerSlots = MakeDrawerSlots();
constexpr drawfx::Iface kDrawer{"DisplayList", 31, kDrawerSlots.data()};

// The configuration service's settings object (the Impl's constructor reads "lightBoundsInflate" through slot 9).
constexpr std::array<Slot, 10> MakeSettingsSlots()
{
    std::array<Slot, 10> a = BaseSlots<10>();
    a[9] = {2, Ret::Int, true, "GetProperty", "ii"};
    return a;
}
constexpr auto kSettingsSlots = MakeSettingsSlots();
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots.data()};

struct World {
    sgfx::Nodes nodes;
    Fake traversal, context, listener, doubter; // doubter: a second listener whose answer (4) is per seed
    Fake lights[3];  // 600-602
    Fake copies[8];  // 610-617: what the lights' Clone (and the reader's slot 24) hand out, in turn
    unsigned nextCopy = 0;
    Fake shapes[2];  // 630, 631: the lights' bound shapes
    Fake bound, linear, drawer, settings;
    float shapeBox[2][6];
    sgfx::Manager manager;
    cICompositionTreeNode *xf[2] = {};
    unsigned xfCount = 0;
};
World w;
alignas(16) cViewerView gView;

cICompositionTreeNode *__cdecl MakeOurs() { return cLightRefNode::Create(); }
cICompositionTreeNode *__cdecl MakeByConstruct()
{
    return reinterpret_cast<cLightRefNode *>(::operator new(kSize))->Construct();
}
cILightRefNode *As(cICompositionTreeNode *n) { return static_cast<cILightRefNode *>(static_cast<cIRenderableNode *>(n)); }
cIBoundedNode *Bounded(cICompositionTreeNode *n) { return As(n); }
uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
uint32_t *Words(cICompositionTreeNode *n) { return reinterpret_cast<uint32_t *>(n); }
cILight *Light(unsigned i) { return reinterpret_cast<cILight *>(&w.lights[i]); }

// ---- the fakes' handlers ----

void Hand(Fake &copy, const Fake &from)
{
    for (unsigned k = 0; k < 6; ++k) copy.value[k] = from.value[k];
    for (unsigned s = 52; s <= 57; ++s) copy.ret[s] = from.ret[s];
}
Fake &NextCopy() { return w.copies[w.nextCopy++ % 8]; }
uint32_t LightClone(Fake &self, const uint32_t *a)
{
    Fake &copy = NextCopy();
    Hand(copy, self);
    ++copy.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &copy;
    return 1;
}
uint32_t LightSelf(Fake &self, const uint32_t *) { return P(&self); }
// The standard light's name (slot 49): one of a few texts or none, by value[5].
const char *const kLightNames[] = {nullptr, "sunGroup", "", "a longer group name"};
uint32_t LightName(Fake &self, const uint32_t *) { return P(kLightNames[self.value[5] % 4]); }
uint32_t LightStandard(Fake &self, const uint32_t *) { return self.value[0] ? P(&self) : 0; }
uint32_t LightShape(Fake &self, const uint32_t *) { return self.value[1] ? P(&w.shapes[self.value[1] - 1]) : 0; }
template <unsigned Getter> uint32_t LightSet(Fake &self, const uint32_t *a)
{
    self.ret[Getter] = a[0];
    return 0;
}
uint32_t LightSetColor(Fake &self, const uint32_t *a)
{
    std::memcpy(&self.value[2], reinterpret_cast<const void *>(uintptr_t(a[0])), 12);
    return 0;
}
uint32_t LightGetColor(Fake &self, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), &self.value[2], 12);
    return a[0];
}

void MakeLight(Fake &f, uint32_t id, std::mt19937 &rng)
{
    f.Reset<kLight>(id);
    f.On(15, &LightClone);
    f.On(17, &LightSelf);
    f.On(35, &LightStandard);
    f.On(37, &LightShape);
    f.On(41, &LightSetColor);
    f.On(49, &LightName);
    f.On(42, &LightSet<52>);
    f.On(43, &LightSet<53>);
    f.On(44, &LightSet<54>);
    f.On(45, &LightSet<55>);
    f.On(47, &LightSet<56>);
    f.On(48, &LightSet<57>);
    f.On(51, &LightGetColor);
    f.value[0] = rng() % 4 != 0;
    f.value[1] = rng() % 3;
    f.value[5] = rng() % 4;
    for (unsigned k = 2; k < 5; ++k) f.value[k] = std::bit_cast<uint32_t>(sgtx::RandomFloat(rng));
    for (unsigned s = 52; s <= 57; ++s) f.ReturnsFloat(s, sgtx::RandomFloat(rng));
}

uint32_t Answer(Fake &self, const uint32_t *)
{
    const uint32_t k = self.value[2]++;
    return self.value[1] >> (k % 32) & 1;
}
uint32_t AnswerHit(Fake &self, const uint32_t *a)
{
    const uint32_t k = self.value[2]++;
    *reinterpret_cast<float *>(uintptr_t(a[0])) = 0.5f * float((k * 7 + self.value[3]) % 13);
    return self.value[1] >> (k % 32) & 1;
}
void RandomAnswers(Fake &f, std::mt19937 &rng)
{
    f.value[1] = rng() % 4 ? rng() : rng() % 2 ? 0u : ~0u;
    f.value[3] = rng() % 13;
}
uint32_t ShapeBox(Fake &self, const uint32_t *a)
{
    const float *box = w.shapeBox[self.value[0]];
    float *out = reinterpret_cast<float *>(uintptr_t(a[0]));
    for (unsigned k = 0; k < 6; ++k) out[k] = box[k] + 0.25f * float(self.value[2]++ % 3);
    return a[0];
}
void RandomShapeBox(unsigned i, std::mt19937 &rng)
{
    for (unsigned k = 0; k < 3; ++k) {
        w.shapeBox[i][k] = float(int(rng() % 9) - 4) * 0.75f;
        w.shapeBox[i][k + 3] = w.shapeBox[i][k] + float(rng() % 5) * 0.5f;
    }
}

uint32_t SettingsGet(Fake &self, const uint32_t *a)
{
    sgfx::NoteText("key", *reinterpret_cast<const char *const *>(uintptr_t(a[0])));
    if (!self.value[1]) return 0;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1]) + 4) = self.value[0];
    return 1;
}
uint32_t SettingsService(Fake &self, const uint32_t *a)
{
    if (a[0] == cLightRefNodeImpl::kSettingsService && a[1] == cLightRefNodeImpl::kSettingsIid) {
        ++w.settings.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &w.settings;
        return 1;
    }
    return sgfx::detail::GetSystemService(self, a);
}

// The reader's slot 24 (the copy read inline): the next copy, counted, written to `out`.
uint32_t ReaderCopy(Fake &, const uint32_t *a)
{
    Fake &copy = NextCopy();
    Hand(copy, w.lights[0]);
    copy.value[0] = 1; // a stored copy is a standard light's
    ++copy.refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &copy;
    return 1;
}
// The writer's WriteObjectRef (26): the object noted.
uint32_t WriterRef(Fake &, const uint32_t *a)
{
    drawfx::Note("written ref", drawfx::Norm(a[0]));
    return 1;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gResources.Reset(cells);
    sgfx::gResources.framework.On(5, &SettingsService);
    w.settings.Reset<kSettings>(480);
    w.settings.On(9, &SettingsGet);
    w.settings.value[0] = rng() % 4;
    w.settings.value[1] = rng() % 4 != 0;
    sgfx::MakeTraversal(w.traversal, 450, rng() % 3);
    w.traversal.Settle<9, 1>("a"); // Visit(light-reference node): the node (both sides normalise it)
    w.traversal.Returns(9, 0x90 + w.traversal.ret[4]);
    sgfx::MakeListener(w.listener, 500);
    sgfx::MakeListener(w.doubter, 505, rng() % 2 != 0);
    for (unsigned i = 0; i < 3; ++i) MakeLight(w.lights[i], 600 + i, rng);
    for (unsigned i = 0; i < 8; ++i) MakeLight(w.copies[i], 610 + i, rng);
    w.nextCopy = 0;
    for (unsigned i = 0; i < 2; ++i) {
        Fake &s = w.shapes[i];
        s.Reset<kBound>(630 + i);
        s.value[0] = i;
        s.On(20, &Answer);
        s.On(37, &ShapeBox);
        s.On(38, &ShapeBox);
        s.On(39, &ShapeBox);
        RandomAnswers(s, rng);
        RandomShapeBox(i, rng);
    }
    w.bound.Reset<kBound>(640);
    w.linear.Reset<kLinear>(641);
    w.linear.On(18, &Answer);
    w.linear.On(29, &AnswerHit);
    RandomAnswers(w.linear, rng);
    w.drawer.Reset<kDrawer>(642);
    std::memset(&gView, 0, sizeof(gView));
    gView.mDrawer = &w.drawer;
    drawfx::AddRange(&gView, sizeof(gView), kViewTag);
    w.nodes = sgfx::Nodes{};
    w.xfCount = 0;
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, kEventCell);
    cells.Set(cTransformNodeBehaviorTraits::kEventManagerCell, w.manager.manager);
    sgrx::Setup(rng);
}

// ---- the trees ----

void NoteNodes(const char *label)
{
    w.nodes.NoteShape(label);
    // The light-reference words first (so a difference there reads on its own), then every word.
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        const uint32_t *words = Words(w.nodes[i]);
        drawfx::Note("light words", {drawfx::Norm(words[31]), drawfx::Norm(words[32]), words[33], words[34] & 0xff, words[35]});
    }
    w.nodes.NoteWords(label, kSkipWords);
}

void Hold(cICompositionTreeNode *n)
{
    n->AddRef();
    drawfx::AddRange(n, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
    w.nodes.at[w.nodes.count++] = n;
}
// A new node a clone made, registered like the others (held by the test's reference from the clone).
void Adopt(cICompositionTreeNode *n)
{
    drawfx::AddRange(n, kSize, sgfx::kNodeTag | (w.nodes.count << 8));
    w.nodes.at[w.nodes.count++] = n;
    Words(n)[29] = 0x1234;
}

// The children a deep clone made (new nodes, all light references here), held and registered like the others.
void HoldSubtree(cICompositionTreeNode *root)
{
    for (cICompositionTreeNode *c = root->GetFirstChild(); c; c = c->GetNextSibling()) {
        if (drawfx::Norm(P(c)) == P(c) && w.nodes.count < sgfx::Nodes::kMax) {
            Hold(c);
            Words(c)[29] = 0x4321;
        }
        HoldSubtree(c);
    }
}

void MakeTree(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    const bool construct = rng() % 2;
    const sgfx::Factory make = original ? ctx.Original<sgfx::Factory>(kFactory) : construct ? &MakeByConstruct : &MakeOurs;
    const unsigned n = 1 + rng() % 4;
    int parents[sgfx::Nodes::kMax];
    parents[0] = -1;
    for (unsigned i = 1; i < n; ++i) parents[i] = rng() % 3 ? int(rng() % i) : -1;
    w.nodes.Make(make, n, kSize);
    w.nodes.Link(parents);
    for (unsigned i = 0; i < n; ++i) Words(w.nodes[i])[29] = uint32_t(rng());
}

void NoteLights()
{
    for (const Fake &f : w.lights) drawfx::Note("light refs", {f.id, uint32_t(f.refs)});
    for (const Fake &f : w.copies) drawfx::Note("copy refs", {f.id, uint32_t(f.refs)});
}

void ReleaseAll()
{
    difftest::FreeRecorder freed;
    w.nodes.Release();
    for (unsigned i = 0; i < w.xfCount; ++i) w.xf[i]->Release();
    w.xfCount = 0;
    freed.Stop();
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    NoteLights();
    drawfx::Note("settings refs", uint32_t(w.settings.refs));
}

cILight *RandomLight(std::mt19937 &rng) { return rng() % 5 ? Light(rng() % 3) : nullptr; }

// A walked and released id range.
void NoteIdRange(nGZEvent::cAttributeIDRange range)
{
    unsigned steps = 0;
    while (steps < 12 && !range.mCurrent->IsEqual(range.mEnd)) {
        drawfx::Note("id", range.mCurrent->Get());
        range.mCurrent->Next();
        ++steps;
    }
    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
}

void NoteAttribute(const char *label, const cAttributeValue &v)
{
    drawfx::Note(label, {v.mType, std::bit_cast<uint32_t>(v.mValues[0]), std::bit_cast<uint32_t>(v.mValues[1]),
                         std::bit_cast<uint32_t>(v.mValues[2]), std::bit_cast<uint32_t>(v.mValues[3]),
                         std::bit_cast<uint32_t>(v.mValues[4]), std::bit_cast<uint32_t>(v.mValues[5]),
                         std::bit_cast<uint32_t>(v.mValues[6])});
}

void Mark(difftest::Context &ctx, int k, unsigned op)
{
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
}

// The operations every test shares (the light, the copy, the attributes, the tree).
void OperateCommon(unsigned op, cICompositionTreeNode *n, std::mt19937 &rng)
{
    cILightRefNode *s = As(n);
    switch (op) {
    case 0: // 93
        s->SetLight(RandomLight(rng));
        break;
    case 1: // 92
        s->DropLightCopy();
        break;
    case 2: { // 7: an attribute set
        const uint32_t ids[] = {kLightAttributes[rng() % 7], kLightAttributes[rng() % 7], 0x1234, uint32_t(rng())};
        cAttributeValue v{uint32_t(rng()), {}};
        for (float &f : v.mValues) f = sgtx::RandomFloat(rng);
        const uint32_t id = ids[rng() % 4];
        drawfx::Note("set", id);
        if (rng() % 2) n->SetFlag(12, false); // fresh, so the set's propagation shows in the flags
        n->SetAttribute(id, rng() % 4, P(&v));
        drawfx::Note("flags", n->GetFlags());
        break;
    }
    case 3: { // 10: an attribute read
        const uint32_t ids[] = {kLightAttributes[rng() % 7], kLightAttributes[rng() % 7], 0x1234, uint32_t(rng())};
        cAttributeValue v{0x5a5a5a5a, {}};
        for (float &f : v.mValues) f = -7.5f;
        const uint32_t id = ids[rng() % 4];
        n->GetAttribute(P(&v), id, rng() % 4);
        drawfx::Note("get", id);
        NoteAttribute("attribute", v);
        break;
    }
    case 4: { // a node moved
        cICompositionTreeNode *c = w.nodes[rng() % w.nodes.count];
        if (cICompositionTreeNode *p = c->GetParent()) p->RemoveChild(c);
        if (w.xfCount && rng() % 2) {
            w.xf[rng() % w.xfCount]->AddChild(c);
            break;
        }
        cICompositionTreeNode *to = w.nodes[rng() % w.nodes.count];
        bool inside = false;
        for (cICompositionTreeNode *a = to; a; a = a->GetParent()) inside |= a == c;
        if (!inside) to->AddChild(c);
        break;
    }
    case 5: { // a light's shape changed (its kind, standard or not, stays: the copies' setters assume it)
        Fake &f = rng() % 2 ? w.lights[rng() % 3] : w.copies[rng() % 8];
        f.value[1] = rng() % 3;
        break;
    }
    }
}

void Operate(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    cILightRefNode *s = As(n);
    const unsigned op = rng() % 16;
    Mark(ctx, k, op);
    switch (op) {
    case 0: { // QueryInterface
        const uint32_t iids[] = {cILightRefNode::kIid, 0x2c5ea4c1, sgfx::kIidTree, sgfx::kIidGraph, 0xdb01d4b2,
                                 0x5b0aaa74, 0xbb11a53e, 1, 0xe51c10f5, 0x7d14eb66, 0x7d3a2385, kLightIid};
        const uint32_t iid = iids[rng() % 12];
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        // A quarter through the +8 face (its QueryInterface / Release adjustor thunks 0xa63392 / 0xa63515).
        cIGZUnknown *const face8 = reinterpret_cast<cIGZUnknown *>(reinterpret_cast<char *>(n) + 8);
        cIGZUnknown *const via = rng() % 4 ? static_cast<cIGZUnknown *>(n) : face8;
        const bool r = rng() % 8 ? via->QueryInterface(iid, &out) : via->QueryInterface(iid, nullptr);
        drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (CountOf(n) != before) via->Release();
        break;
    }
    case 1: { // kinds
        const uint32_t iids[] = {cILightRefNode::kIid, 0x2c5ea4c1, sgfx::kIidTree, sgfx::kIidGraph, 0x3b253dc7,
                                 0xbb11a53e, 0x5b0aaa74, 1, 0, 0xdb01d4b2, 0xe51c10f5, 0x7d14eb66, kLightIid};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 2: { // traversals, slot 70
        auto *t = reinterpret_cast<cICompositionTreeTraversal *>(&w.traversal);
        drawfx::Note("accept", {n->Accept(t), n->AcceptReadOnly(t), uint8_t(Bounded(n)->IsEverywhere())});
        break;
    }
    case 3: { // 87-91, 94-97
        if (rng() % 2) s->SetLightFlag(uint8_t(rng() % 3 ? rng() % 2 : rng()));
        if (rng() % 2) s->SetWord6c(rng() % 2 ? 0 : uint32_t(rng()));
        if (rng() % 2) s->SetWord78(int32_t(rng()));
        drawfx::Note("words", {s->GetLightFlag(), s->GetDetailLimit(), s->GetWord6c(), drawfx::Norm(P(s->GetLightConst())),
                               drawfx::Norm(P(s->GetLight())), uint32_t(s->GetWord78())});
        break;
    }
    case 4: { // the registry: 12, 13, 14
        drawfx::Note("events", n->GetEventCount());
        NoteIdRange(n->GetEventIds());
        const uint32_t ids[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[1], sgfx::kRegistryIds[2],
                                kLightAttributes[rng() % 7], kLightAttributes[rng() % 7], uint32_t(rng())};
        const nGZEvent::cEventAttribute a = n->GetEventAttribute(ids[rng() % 6]);
        drawfx::Note("attribute", {a.mId, a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        sgfx::NoteText("attribute name", a.mName ? a.mName : "(null)");
        break;
    }
    case 6: { // the listeners told (5, 0xa63640) or asked (6, 0xa61a40)
        const uint32_t event = rng() % 2 ? cLightRefNodeImpl::kLightChangedEvent : cLightRefNodeImpl::kSubordinateEvent;
        drawfx::Note("event", event);
        if (rng() % 2) {
            cSubordinateListChanged body(rng() % 4, rng() % 3 ? kLightIid : sgfx::kIidTree);
            n->NotifyListeners(event, rng(), &body);
        } else {
            drawfx::Note("asked", uint8_t(n->AskListeners(event, rng(), nullptr)));
        }
        break;
    }
    case 5: { // an event told (3)
        const uint32_t events[] = {cLightRefNodeImpl::kSubordinateEvent, cLightRefNodeImpl::kSubordinateEvent,
                                   cLightRefNodeImpl::kDetailChangedEvent, cLightRefNodeImpl::kTransformEvent,
                                   cLightRefNodeImpl::kLightChangedEvent, 0x1234};
        cSubordinateListChanged body(rng() % 4, rng() % 3 ? kLightIid : sgfx::kIidTree);
        const uint32_t event = events[rng() % 6];
        drawfx::Note("event", event);
        // (a node is stale nearly always; fresh, the transform event's propagation shows in the flags)
        if (rng() % 2) n->SetFlag(12, false);
        n->OnEvent(event, rng(), &body, 0);
        drawfx::Note("flags", n->GetFlags());
        break;
    }
    case 7: // the detail limit planted again
        Words(n)[29] = rng() % 2 ? 0x7fffffffu : uint32_t(rng());
        break;
    case 8: { // flags
        uint32_t bit = rng() % 40;
        // (not 6, the partition flag: on a node that is not a partition the tree code's walk would call slot 77 as
        // cIPartitionNode::Clear, the renderable node's AddLayer with no name, undefined on both sides)
        if ((bit & 31) == 1 || (bit & 31) == 6 || (bit & 31) == 12) bit = 0;
        n->SetFlag(bit, rng() % 2);
        drawfx::Note("flags", {uint8_t(n->TestFlag(bit)), n->GetFlags()});
        break;
    }
    default:
        OperateCommon(op - 9, n, rng); // 9-14
        if (op == 15) OperateCommon(0, n, rng);
        break;
    }
}

// ---- transforms over the nodes (Bounds) ----

cICompositionTreeNode *__cdecl MakeOurTransform() { return cTransformNode::Create(); }
cITransformNode *Xf(cICompositionTreeNode *n) { return static_cast<cITransformNode *>(n); }

void RandomPlace(cITransformNode *x, std::mt19937 &rng)
{
    const float t[3] = {float(int(rng() % 9) - 4), float(rng() % 5), 0.5f * float(rng() % 3)};
    x->SetTranslation(t);
    if (rng() % 2) {
        const float q[4] = {0.0f, 0.6f, 0.0f, 0.8f};
        x->SetRotation(q);
    }
}

void MakeTransforms(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    if (rng() % 3 == 0) return;
    const sgfx::Factory make = original ? ctx.Original<sgfx::Factory>(0xa3a6c0) : &MakeOurTransform;
    const unsigned count = 1 + rng() % 2;
    for (unsigned i = 0; i < count; ++i) {
        cICompositionTreeNode *x = make();
        x->AddRef();
        drawfx::AddRange(x, cTransformNode::kSize, kTransformTag | (i << 8));
        w.xf[w.xfCount++] = x;
        RandomPlace(Xf(x), rng);
        if (i) w.xf[0]->AddChild(x);
    }
    for (unsigned i = 0; i < w.nodes.count; ++i)
        if (!w.nodes[i]->GetParent() && rng() % 4) w.xf[rng() % w.xfCount]->AddChild(w.nodes[i]);
}

AffineTransform3 RandomFrame(std::mt19937 &rng)
{
    AffineTransform3 f{};
    const float turned[4] = {0.0f, 0.6f, 0.0f, 0.8f}, level[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    game_code::SetFrameRotation(&f, rng() % 2 ? turned : level);
    if (rng() % 3) f.mUnknown0 |= 1;
    for (float &o : f.mOffset) o = float(int(rng() % 9) - 4) * 0.5f;
    return f;
}
void NoteBox(const char *label, const cBoundBox &b)
{
    drawfx::Note(label, {std::bit_cast<uint32_t>(b.mMin[0]), std::bit_cast<uint32_t>(b.mMin[1]),
                         std::bit_cast<uint32_t>(b.mMin[2]), std::bit_cast<uint32_t>(b.mMax[0]),
                         std::bit_cast<uint32_t>(b.mMax[1]), std::bit_cast<uint32_t>(b.mMax[2])});
}

// The view: its frame and six planes, each an axis (either way) at a distance.
void RandomView(std::mt19937 &rng)
{
    const AffineTransform3 f = RandomFrame(rng);
    std::memcpy(&gView.mFrame, &f, sizeof(gView.mFrame));
    for (auto &plane : gView.mFrustum.plane) {
        for (float &c : plane) c = 0.0f;
        plane[rng() % 3] = rng() % 2 ? 1.0f : -1.0f;
        plane[3] = float(int(rng() % 13) - 4);
    }
}

void OperateBounds(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    const unsigned op = rng() % 16;
    Mark(ctx, k, op);
    switch (op) {
    case 0: // 72
        NoteBox("box", Bounded(n)->GetBoundBox());
        break;
    case 1: { // 71
        const AffineTransform3 f = RandomFrame(rng);
        NoteBox("box in", Bounded(n)->GetBoundBoxIn(reinterpret_cast<const cTransform34 *>(&f)));
        break;
    }
    case 2: { // 73
        const AffineTransform3 f = RandomFrame(rng);
        drawfx::Note("73", uint8_t(Bounded(n)->IntersectsBound(P(&w.bound), P(&f))));
        break;
    }
    case 3: // 74
        drawfx::Note("74", uint8_t(Bounded(n)->IntersectsLinear(reinterpret_cast<nGZCompGeom::cILinear3 *>(&w.linear))));
        break;
    case 4: { // 75
        float t = rng() % 4 ? 0.5f * float(rng() % 13) : FLT_MAX;
        const bool hit = Bounded(n)->FirstHitLinear(&t, reinterpret_cast<nGZCompGeom::cILinear3 *>(&w.linear));
        drawfx::Note("75", {uint8_t(hit), std::bit_cast<uint32_t>(t)});
        break;
    }
    case 5: // 82
        As(n)->Draw(&gView, rng() % 2 ? 0x3fu : uint32_t(rng()));
        break;
    case 6:
    case 7: { // 83: the mask a byte (the word's other bits noise)
        const uint32_t masks[] = {0, 0x3f, 0x15, uint32_t(rng() % 64)};
        const uint32_t mask = masks[rng() % 4] | (rng() % 2 ? uint32_t(rng()) & 0xffffff00u : 0u);
        As(n)->Slot83(&gView, reinterpret_cast<void *>(uintptr_t(mask)));
        break;
    }
    case 8: // the view moved
        RandomView(rng);
        break;
    case 9: // a transform moved
        if (w.xfCount) RandomPlace(Xf(w.xf[rng() % w.xfCount]), rng);
        break;
    case 10: // the answers or a shape's box changed
        if (rng() % 2) RandomAnswers(rng() % 2 ? w.linear : w.shapes[rng() % 2], rng);
        else RandomShapeBox(rng() % 2, rng);
        break;
    case 11: // the words
        w.nodes.NoteWords("words", kSkipWords);
        break;
    default:
        OperateCommon(op == 15 ? 4 : op - 12 + (op == 14 ? 1 : 0), n, rng); // 12: SetLight, 13: DropLightCopy, 14: set
        break;
    }
}

// ---- persist ----

void QueueRead(std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    r.versionCount = r.versionAt = r.wordCount = r.wordAt = r.refCount = r.refAt = r.textCount = r.textAt = 0;
    const uint32_t versions[] = {10, 10, 11, 9, 7};
    const uint32_t version = versions[rng() % 5];
    r.Version(version);
    // The renderable part.
    for (int i = 0; i < 4; ++i) r.Version(rng() % 16);
    r.Version(4 + rng() % 2);
    r.Word(0);
    r.Text(rng() % 2 ? "alpha" : "beta");
    r.Word(0);
    for (int i = 0; i < 7; ++i) r.Float(sgtx::RandomFloat(rng));
    r.Word(rng() % 2 ? 0x7fffffffu : uint32_t(rng()));
    r.Word(rng() % 2);
    r.Word(rng() % 3 == 0);
    const unsigned groups = rng() % 2;
    r.Word(groups);
    for (unsigned i = 0; i < groups; ++i) r.Text(rng() % 2 ? "gamma" : "delta");
    r.Word(uint32_t(rng()));
    r.Word(rng() % 2);
    // The light-reference part.
    if (version < 10) {
        const uint32_t count = rng() % 4 ? rng() % 2 : 2;
        r.Word(count);
        if (count == 1) r.Ref(RandomLight(rng));
        r.Word(rng() % 3 ? rng() % 2 : rng());
        r.Word(rng() % 2);
    } else {
        r.Ref(RandomLight(rng));
        r.Word(rng() % 3 ? rng() % 2 : rng());
        r.Word(rng() % 2);
    }
    drawfx::Note("queued", {version, r.wordCount, r.refCount});
}

void BuildPersist(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed, cells);
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    Fake &r = sgfx::gReader.fake;
    r.Settle<6, 0>();
    r.On(6, &sgfx::detail::ReaderUint32);
    r.Settle<24, 2>("ua");
    r.On(24, &ReaderCopy);
    Fake &wr = sgfx::gWriter.fake;
    wr.Settle<6, 1>("b");
    wr.Settle<25, 1>("a");
    wr.On(26, &WriterRef);
}

void OperatePersist(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = w.nodes;
    cICompositionTreeNode *n = ns[rng() % ns.count];
    const unsigned op = rng() % 10;
    Mark(ctx, k, op);
    switch (op) {
    case 0:
    case 1: { // written
        difftest::FreeRecorder freed;
        n->GetReferent()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        freed.Stop();
        drawfx::Note("write frees", uint32_t(freed.Count()));
        break;
    }
    case 2:
    case 3: { // read
        QueueRead(rng);
        n->GetReferent()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&sgfx::gReader.fake));
        drawfx::Note("read", {sgfx::gReader.wordAt, sgfx::gReader.refAt, sgfx::gReader.textAt});
        break;
    }
    case 4: { // the type
        cPersistTypeInfo info = n->GetReferent()->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 5: // the flag
        As(n)->SetLightFlag(uint8_t(rng()));
        break;
    default:
        OperateCommon(op - 6, n, rng); // light, copy dropped, attribute set or read
        break;
    }
}

struct Scopes {
    svcfx::AllocatorScope allocator; // the settings key is a service string: the game's allocator service
    sgrx::Services services;
    sgfx::RegistryScope node{sgfx::kNodeRegistry};
    sgfx::RegistryScope transform{sgfx::kTransformRegistry};
    sgfx::RegistryScope lightRef{kLightRefRegistry};
    sgtp::RefreshScope refresh;
    Scopes()
    {
        // The light attributes' records (the node says in word +8 whether it has a light).
        auto &map = *GameAddress<nGZEvent::cEventAttributeMap>(kLightRefRegistry.map);
        for (uint32_t i = 0; i < 7; ++i)
            map[kLightAttributes[i]] = {kLightAttributes[i], "light attribute", 0x777u + i, 0x500u + i, 0x600u + i, 4};
    }
};

void ListenAll(std::mt19937 &rng)
{
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        if (rng() % 3) w.manager.Listen(w.nodes[i], cLightRefNodeImpl::kLightChangedEvent, w.listener, 0x501);
        if (rng() % 2) w.manager.Listen(w.nodes[i], cLightRefNodeImpl::kSubordinateEvent, w.listener, 0x502);
        if (rng() % 2) w.manager.Listen(w.nodes[i], cLightRefNodeImpl::kLightChangedEvent, w.doubter, 0x505);
    }
}
void GiveLights(std::mt19937 &rng)
{
    for (unsigned i = 0; i < w.nodes.count; ++i) {
        if (rng() % 4) As(w.nodes[i])->SetLight(Light(rng() % 3));
        if (rng() % 3 == 0) {
            cAttributeValue v{0, {sgtx::RandomFloat(rng), 0.5f, 0.25f}};
            w.nodes[i]->SetAttribute(kLightAttributes[rng() % 7], 0, P(&v)); // a copy made
        }
    }
}

// The name iterators' pool (a cRZFastFixedAllocator2<0x14> at 0x10f6340: a pointer to its current page at +4) on a test
// page, as tests/sg_shape_ref_node.cpp's NamePool.
struct NamePool {
    static constexpr uint32_t kCell = 0x10f6344, kPage = 0x800, kBlock = 0x14, kTag = 0x7b300000;
    uint8_t *page;
    uint32_t saved;
    NamePool()
    {
        page = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
        saved = *GameAddress<uint32_t>(kCell);
        *GameAddress<uint32_t>(kCell) = P(page);
        Reset(false);
    }
    ~NamePool()
    {
        *GameAddress<uint32_t>(kCell) = saved;
        _aligned_free(page);
    }
    void Reset(bool registered = true)
    {
        std::memset(page, 0, kPage);
        uint32_t *header = reinterpret_cast<uint32_t *>(page);
        header[3] = 1000;
        uint32_t next = 0;
        for (uint32_t at = kPage - kBlock; at >= 0x10; at -= kBlock) {
            *reinterpret_cast<uint32_t *>(page + at) = next;
            next = P(page + at);
        }
        header[2] = next;
        if (registered) drawfx::AddRange(page, kPage, kTag);
    }
    void Note() const
    {
        const uint32_t *header = reinterpret_cast<const uint32_t *>(page);
        drawfx::Note("pool", {drawfx::Ptr(header[2]), header[3]});
    }
};

// An iterator's words (count, position, the texts) and what it reads as.
void NoteIterator(const char *label, cIArrayAttributeEntryIterator *it, bool full)
{
    if (!it) {
        drawfx::Note(label, 0u);
        return;
    }
    const uint32_t *words = reinterpret_cast<const uint32_t *>(it);
    drawfx::Note(label, {full ? drawfx::Norm(P(it)) : 0u, words[1], full ? words[2] : 0u}); // (the empty range's are heap objects)
    const cArrayAttributeEntryName name = it->Get();
    sgfx::NoteText("group", name.mWord0 ? reinterpret_cast<const char *>(uintptr_t(name.mWord0)) : "(null)");
    sgfx::NoteText("name", name.mWord1 ? reinterpret_cast<const char *>(uintptr_t(name.mWord1)) : "(null)");
}

// Slot 51: the ranges of the seven light attributes (and other ids: the empty range), nodes with and without a light,
// the light with and without a standard face and a name; each iterator read, moved and compared, then released.
void OperateNames(difftest::Context &ctx, int k, NamePool &pool, std::mt19937 &rng)
{
    cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
    Mark(ctx, k, 0);
    if (rng() % 3 == 0) As(n)->SetLight(RandomLight(rng));
    if (rng() % 4 == 0) w.lights[rng() % 3].value[5] = rng() % 4;
    const uint32_t id = rng() % 5 ? kLightAttributes[rng() % 7] : rng() % 2 ? 0x1234 : uint32_t(rng());
    drawfx::Note("id", id);
    cArrayAttributeEntryRange range{};
    const void *const returned = n->MakeIterator51(&range, id);
    drawfx::Note("returns the range", uint8_t(returned == &range));
    const bool full = cLightRefNodeImpl::IsLightAttribute(id);
    NoteIterator("current", range.mCurrent, full);
    NoteIterator("begin", range.mBegin, full);
    NoteIterator("end", range.mEnd, full);
    pool.Note();
    cIArrayAttributeEntryIterator *const begin = range.mBegin, *const end = range.mEnd;
    drawfx::Note("equal", {uint8_t(range.mCurrent->IsEqual(begin)), uint8_t(begin->IsEqual(end)), uint8_t(end->IsEqual(begin)),
                           uint8_t(range.mCurrent->IsEqual(end))});
    // Walk the current iterator as a range does: until it reaches the end, but at most a few steps.
    for (int step = 0; step < 4 && !range.mCurrent->IsEqual(end); ++step) {
        const cArrayAttributeEntryName name = range.mCurrent->Get();
        drawfx::Note("walked", {uint32_t(name.mWord0 != 0), uint32_t(name.mWord1 != 0)});
        range.mCurrent->Next();
    }
    NoteIterator("walked to", range.mCurrent, full);
    range.mCurrent->Previous();
    drawfx::Note("previous", uint8_t(range.mCurrent->IsEqual(begin)));
    range.mCurrent->Advance(int32_t(rng() % 5) - 2);
    drawfx::Note("advanced", {uint8_t(range.mCurrent->IsEqual(begin)), uint8_t(range.mCurrent->IsEqual(end))});
    const cArrayAttributeEntryName at = range.mCurrent->At(int32_t(rng()));
    drawfx::Note("at", {uint32_t(at.mWord0 != 0), uint32_t(at.mWord1 != 0)});
    // Clones are new counted-zero iterators at the same place.
    cIArrayAttributeEntryIterator *const clone = range.mEnd->Clone();
    NoteIterator("clone", clone, full);
    pool.Note();
    void *queried = nullptr;
    drawfx::Note("query", {uint8_t(begin->QueryInterface(1, &queried)), uint8_t(queried == begin), uint8_t(begin->QueryInterface(2, &queried)),
                           uint8_t(begin->QueryInterface(1, nullptr))});
    if (queried) begin->Release();
    drawfx::Note("refs", {uint32_t(begin->AddRef()), uint32_t(begin->Release())});
    if (clone) clone->AddRef(), clone->Release();
    for (cIGZUnknown *it : {static_cast<cIGZUnknown *>(range.mCurrent), static_cast<cIGZUnknown *>(range.mBegin),
                            static_cast<cIGZUnknown *>(range.mEnd)})
        drawfx::Note("released", it->Release());
    pool.Note();
}

} // namespace

DIFF_TEST(SG_LightRefNode_Factory, kFactory)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "factory", 24, Build, [&](bool original, std::mt19937 &rng) {
        const unsigned make = rng() % 2;
        cICompositionTreeNode *n;
        if (make) { // 0xa60c00 against Construct, in fresh memory
            void *const memory = ::operator new(kSize);
            cLightRefNode *const made = original ? ctx.Original<cLightRefNode *(__thiscall *)(void *)>(0xa60c00)(memory)
                                                 : static_cast<cLightRefNode *>(memory)->Construct();
            drawfx::Note("in place", uint8_t(static_cast<void *>(made) == memory));
            n = reinterpret_cast<cICompositionTreeNode *>(made);
        } else {
            n = original ? ctx.Original<sgfx::Factory>(kFactory)() : MakeOurs();
        }
        drawfx::Note("count", CountOf(n));
        Hold(n);
        w.nodes.size = kSize;
        NoteNodes("new node");
        Words(n)[29] = uint32_t(rng());
        drawfx::Note("limit", As(n)->GetDetailLimit());
        if (rng() % 2) As(n)->SetLight(Light(rng() % 3));
        if (rng() % 3 == 0) {
            cAttributeValue v{0, {1.5f, 0.5f, 0.25f}};
            n->SetAttribute(cLightRefNodeImpl::kIntensity, 0, P(&v));
        }
        NoteNodes("held");
        const unsigned end = rng() % 3;
        drawfx::Note("end", end);
        if (end == 0) {
            ReleaseAll();
            return;
        }
        w.nodes.count = 0;
        difftest::FreeRecorder freed;
        if (end == 1) { // 0xa60ea0 against Destruct, then the memory freed
            if (original) ctx.Original<void(__thiscall *)(void *)>(0xa60ea0)(n);
            else reinterpret_cast<cLightRefNode *>(n)->Destruct();
            ::operator delete(n);
        } else { // 0xa61150 against DeletingDestructor (bit 0: freed)
            void *r = original ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xa61150)(n, 1)
                               : reinterpret_cast<cLightRefNode *>(n)->DeletingDestructor(1);
            drawfx::Note("returns this", uint8_t(r == n));
        }
        freed.Stop();
        size_t sizes[64];
        const size_t count = freed.SortedSizes(sizes, 64);
        drawfx::Call c;
        c.note = "freed";
        for (size_t i = 0; i < count; ++i) c.words.push_back(uint32_t(sizes[i]));
        drawfx::gLog.push_back(std::move(c));
        NoteLights();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_LightRefNode_Slots, 0xa63360)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "slots", 64, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        ListenAll(rng);
        GiveLights(rng);
        NoteNodes("built");
        for (int k = 0; k < 32; ++k) Operate(ctx, k, rng);
        NoteNodes("after");
        NoteLights();
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_LightRefNode_Clone, 0xa61720)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "clone", 64, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveLights(rng);
        for (int round = 0; round < 3; ++round) {
            cICompositionTreeNode *n = w.nodes[rng() % w.nodes.count];
            cILightRefNode *s = As(n);
            if (rng() % 2) s->SetLightFlag(uint8_t(rng()));
            if (rng() % 2) s->SetWord6c(uint32_t(rng()));
            if (rng() % 2) s->SetWord78(int32_t(rng()));
            const uint32_t mode = rng() % 3;
            cINodeCloneContext *context = nullptr;
            const bool viaConstruct = rng() % 2;
            if (rng() % 3 == 0 && mode != 1 && w.nodes.count < sgfx::Nodes::kMax) {
                cICompositionTreeNode *prepared =
                    original ? ctx.Original<sgfx::Factory>(kFactory)() : viaConstruct ? MakeByConstruct() : MakeOurs();
                Hold(prepared);
                Words(prepared)[29] = uint32_t(rng());
                if (rng() % 2) As(prepared)->SetLight(Light(rng() % 3));
                sgfx::MakeCloneContext(w.context, prepared);
                context = reinterpret_cast<cINodeCloneContext *>(&w.context);
            }
            const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, cILightRefNode::kIid, 0x12345678};
            void *out = nullptr;
            n->Clone(mode, iids[rng() % 4], &out, context);
            const bool isNew = out && drawfx::Norm(P(out)) == P(out);
            drawfx::Note("clone", {mode, out != nullptr, uint8_t(isNew), isNew ? 0u : drawfx::Norm(P(out))});
            if (isNew && w.nodes.count < sgfx::Nodes::kMax) {
                cICompositionTreeNode *made = static_cast<cICompositionTreeNode *>(out);
                Adopt(made);
                HoldSubtree(made);
                NoteNodes("cloned");
            } else if (out) {
                static_cast<cICompositionTreeNode *>(out)->Release();
            }
            NoteLights();
        }
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_LightRefNode_Persist, 0xa62c00)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "persist", 64, BuildPersist, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveLights(rng);
        NoteNodes("built");
        for (int k = 0; k < 24; ++k) OperatePersist(ctx, k, rng);
        NoteNodes("after");
        sgfx::gReader.NoteNames();
        ReleaseAll();
        sgfx::gPool.NoteCounts();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_LightRefNode_Bounds, 0xa628f0)
{
    Scopes scopes;
    sgfx::CheckSides(ctx, "bounds", 64, Build, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        MakeTransforms(ctx, original, rng);
        GiveLights(rng);
        RandomView(rng);
        NoteNodes("built");
        for (int k = 0; k < 40; ++k) OperateBounds(ctx, k, rng);
        NoteNodes("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

// Slot 51 (0xa611d0, the node's attribute-name range) is the Impl's MakeIterator51 (0xa61d90), tested here with the iterators it hands
// out (cLightRefAttributeNameIterator: constructors 0xa60bd0 / 0xa60ba0, Clone 0xa61810, deleting destructor 0xa61110 (and the destructor 0xa60e90 it folds),
// the pool allocation 0xa60f20).
DIFF_TEST(SG_LightRefNode_Names, 0xa61d90)
{
    Scopes scopes;
    NamePool pool;
    sgfx::CheckSides(ctx, "names", 64, [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        pool.Reset();
    }, [&](bool original, std::mt19937 &rng) {
        MakeTree(ctx, original, rng);
        GiveLights(rng);
        NoteNodes("built");
        for (int k = 0; k < 24; ++k) OperateNames(ctx, k, pool, rng);
        NoteNodes("after");
        NoteLights();
        ReleaseAll();
    });
    w.manager.Drop();
}
