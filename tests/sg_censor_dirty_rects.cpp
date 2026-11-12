// Differential test of the censor strategy's dirty-rect part (lane 261108r, scenegraph TS1h;
// src/sg/cDisplayListCensorRenderStrategy.h): the members the dynamic strategy's drawing paths call.
//
//   SG_CensorDirtyRects  a censor-shaped block (its drawn boxes' vector at +0x18, each box's rectangle at +0x18 of its
//       0x30 bytes, the texture word at +0x28; every other word random) and random calls, each side alike:
//       CollectDirtyRects 0x377f50 (CopyBoxRects 0x378140 and the frame-state getter 0x378200 inside) on an `out`
//       vector that already holds some rectangles (more or fewer than the boxes, its capacity reserved past them or
//       not), ListBoxRects 0x378020 on a list that already holds some, and RenderTexture 0x378370 (SetUpTexture
//       0x378400, DrawItems 0x3782c0, DrawOne 0x378350 inside) with item and record vectors of the same length. Between
//       calls the world changes: the drawn boxes, the texture (a fake surface or null), the viewer's frame state (one of
//       three, or none, so the getter asks the COM director for class 0x6d548978 and hands the new one to the viewer's
//       slot 99), the pass (its type 0, 1, 2, 3, 7 or 0xffffffff, its projection, viewport, depth range and draw
//       counts) and the items (sort keys -1, +-9998 and their neighbours, records' drawn and kind-4 bytes 0, 1 or
//       another value). After each call: the result, the vector or list (size, capacity, array, contents), each frame
//       state's rectangles, the pass's draw counts and the block's words. At the end the fakes' counts and the heap.
//
// Fakes: the viewer (slots 99, 116), the COM director (GetDirectorFrameworkObject 0x1ae18d stood in; slot 3 answers a
// counted state), each frame state's cIGZUnknown (embedded; counted), the texture surface (slot 12 Apply), the device
// (slot 27 Clear: the colour read from the game's cell 0x100933c, which the test fills), the device state (slot 12
// ApplyStateCommand, the command's header and the words the device reads; slot 25 SetProjection) and the items'
// material variants (slot 11 Draw). The game's operator new / delete / free are stood in to ours.
#include "sgfx.h"

#include "code_patch.h"
#include "heap_poison.h"

#include "rz/StaticGetters.h"
#include "sg/cDisplayList.h"
#include "sg/cDisplayListCensorRenderStrategy.h"
#include "sg/cDisplayListDynamicRenderStrategy.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <list>
#include <new>
#include <random>
#include <vector>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using Censor = cDisplayListCensorRenderStrategy;
using Rects = std::vector<DynamicWords16>;
using RectList = std::list<DynamicWords16>;
using Items = std::vector<nGZSceneGraph::DrawItem *>;
using Records = std::vector<DynamicRectRecord *>;

// A drawn box as the code reads it: its rectangle at +0x18.
struct Box48 {
    uint32_t w[12];
};
static_assert(sizeof(Box48) == sizeof(CensorBox));
using Boxes = std::vector<Box48>;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}
template <size_t N, size_t K> constexpr std::array<Slot, N> Plain(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}
constexpr const char *kDeclared = drawfx::kTestTable;

// cIViewerRefNodeBase: 99 sets the +0xa8 reference, 116 answers it.
constexpr At kViewerAt[] = {{99, {1, Ret::Int, true, "Slot99", "a"}}, {116, {0, Ret::Int, true, "Slot116", ""}}};
constexpr auto kViewerSlots = Table<117>(kViewerAt);
constexpr drawfx::Iface kViewer{"Viewer", 117, kViewerSlots.data(), false, kDeclared};
constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data(), false, kDeclared};
// cIDeviceDX: 27 Clear (flags, colour, depth, stencil).
constexpr At kDeviceAt[] = {{27, {4, Ret::Int, true, "Clear", "uF4fu"}}};
constexpr auto kDeviceSlots = Table<28>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 28, kDeviceSlots.data(), false, kDeclared};
// cIDeviceState: 12 ApplyStateCommand (the command noted by the handler), 25 SetProjection.
constexpr At kStateAt[] = {{12, {1, Ret::Int, true, "ApplyStateCommand", "W0"}},
                           {25, {1, Ret::Int, true, "SetProjection", "F16"}}};
constexpr auto kStateSlots = Table<26>(kStateAt);
constexpr drawfx::Iface kDeviceState{"DeviceState", 26, kStateSlots.data(), false, kDeclared};
// cITSRenderSurface: 12 Apply (pass, force).
constexpr At kSurfaceAt[] = {{12, {2, Ret::Int, true, "Apply", "ab"}}};
constexpr auto kSurfaceSlots = Plain<13>(kSurfaceAt);
constexpr drawfx::Iface kSurface{"RenderSurface", 13, kSurfaceSlots.data(), false, kDeclared};
// An item's material variant: 11 Draw (pass, item).
constexpr At kVariantAt[] = {{11, {2, Ret::Int, true, "Draw", "aa"}}};
constexpr auto kVariantSlots = Table<12>(kVariantAt);
constexpr drawfx::Iface kVariant{"Variant", 12, kVariantSlots.data(), false, kDeclared};
// The frame state's cIGZUnknown (embedded).
constexpr At kNoneAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kUnknownSlots = Table<3>(kNoneAt);
constexpr drawfx::Iface kUnknown{"Unknown", 3, kUnknownSlots.data(), false, kDeclared};

constexpr uint32_t kPassTag = 0x7c000000, kItemsTag = 0x7b000000, kRecordsTag = 0x7a000000, kCensorTag = 0x79000000;
constexpr uint32_t kClearColorCell = 0x100933c;
constexpr unsigned kStates = 3, kVariants = 3, kMaxItems = 14;

struct World {
    Fake viewer, com, device, deviceState, texture, variants[kVariants], stateFakes[kStates];
    DynamicStrategyState *states[kStates] = {};
    DynamicStrategyState *viewerState = nullptr;
    nGZSceneGraph::Pass pass;
    nGZSceneGraph::DrawItem items[kMaxItems];
    DynamicRectRecord records[kMaxItems];
    alignas(8) uint8_t censor[Censor::kSize];
    uint32_t lcg = 1;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

template <class T> T *Member(void *o, uint32_t at) { return reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }
Boxes &Visible() { return *Member<Boxes>(gw.censor, 0x18); }
Censor *Ours() { return reinterpret_cast<Censor *>(gw.censor); }

Fake *FakeOf(const void *state)
{
    for (unsigned i = 0; i < kStates; ++i)
        if (gw.states[i] == state) return &gw.stateFakes[i];
    return nullptr;
}

// ---- the handlers ----

uint32_t ClassObject(Fake &, const uint32_t *a)
{
    DynamicStrategyState *const made = gw.states[gw.Next() % kStates];
    ++FakeOf(made)->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = made;
    return 1;
}
uint32_t ViewerState(Fake &, const uint32_t *) { return drawfx::P(gw.viewerState); }
uint32_t SetViewerState(Fake &, const uint32_t *a)
{
    auto *const made = reinterpret_cast<DynamicStrategyState *>(uintptr_t(a[0]));
    if (Fake *f = FakeOf(made)) ++f->refs;
    if (Fake *f = FakeOf(gw.viewerState)) --f->refs;
    gw.viewerState = made;
    return 0;
}
// A state command: the header (vtable, id, size) and the words the device state reads for it (the viewport's four
// fractions; the depth range's two).
uint32_t ApplyCommand(Fake &, const uint32_t *a)
{
    difftest::NotCounted bookkeeping;
    const auto *w = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    const uint32_t id = w[1] & 0xff;
    drawfx::Call c;
    c.note = "command";
    c.words = {w[0], id, w[1] >> 16};
    const unsigned payload = id == 1 ? 4 : id == 2 ? 2 : 0;
    for (unsigned i = 0; i < payload; ++i) c.words.push_back(w[2 + i]);
    drawfx::gLog.push_back(std::move(c));
    return 0;
}

void *StandCom() { return &gw.com; }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

struct Stands {
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

// ---- the world ----

DynamicWords16 MakeRect(std::mt19937 &rng)
{
    static const uint32_t kValues[] = {0, 1, 10, 100, 640, 480, 0xffffffffu, 0x80000000u, 0x7fffffffu, 33};
    DynamicWords16 r;
    for (uint32_t &w : r.w) w = rng() % 3 ? rng() % 1024 : kValues[rng() % std::size(kValues)];
    return r;
}
void FillRects(Rects &v, std::mt19937 &rng)
{
    v.clear();
    if (rng() % 3 == 0) v.shrink_to_fit();
    else if (rng() % 2) v.reserve(rng() % 9);
    for (unsigned n = rng() % 3 == 0 ? 0 : rng() % 7; n; --n) v.push_back(MakeRect(rng));
}
void FillVisible(std::mt19937 &rng)
{
    Boxes &boxes = Visible();
    boxes.clear();
    for (unsigned n = rng() % 3 == 0 ? 0 : 1 + rng() % 6; n; --n) {
        Box48 b;
        for (uint32_t &w : b.w) w = rng();
        const DynamicWords16 r = MakeRect(rng);
        std::memcpy(&b.w[6], r.w, sizeof r.w);
        boxes.push_back(b);
    }
}
float PickFloat(std::mt19937 &rng)
{
    static const float kFloats[] = {0.0f, 1.0f, 0.5f, 0.25f, 0.75f, -0.0f, 1e-3f, 0.999f, 2.0f};
    return kFloats[rng() % std::size(kFloats)];
}
void PickPass(std::mt19937 &rng)
{
    static const uint32_t kTypes[] = {0, 0, 1, 2, 3, 3, 3, 7, 0xffffffffu};
    nGZSceneGraph::Pass &p = gw.pass;
    p.mRenderType = kTypes[rng() % std::size(kTypes)];
    for (float &f : p.mViewportMin) f = PickFloat(rng);
    for (float &f : p.mViewportMax) f = PickFloat(rng);
    for (float &f : p.mDepthRange) f = PickFloat(rng);
    for (float &f : p.mMatrices[0]) f = float(int32_t(rng() % 200) - 100) / 8.0f;
    for (uint32_t &n : p.mDrawStats) n = rng() % 3 ? rng() % 50 : rng();
}
uint8_t PickByte(std::mt19937 &rng)
{
    const uint32_t r = rng() % 6;
    return r < 2 ? 0 : r < 5 ? 1 : uint8_t(2 + rng() % 254);
}
void PickItems(std::mt19937 &rng)
{
    static const int32_t kKeys[] = {-1, -1, 9998, -9998, 9998, -9998, 9997, 9999, -9997, -9999, 0, 1, 2, -2, 19996, 4};
    for (unsigned i = 0; i < kMaxItems; ++i) {
        nGZSceneGraph::DrawItem &item = gw.items[i];
        for (unsigned b = 0; b < sizeof item; ++b) reinterpret_cast<uint8_t *>(&item)[b] = uint8_t(rng());
        item.mMaterial = &gw.variants[rng() % kVariants];
        item.mSortKey = rng() % 8 ? kKeys[rng() % std::size(kKeys)] : int32_t(rng());
        DynamicRectRecord &record = gw.records[i];
        for (unsigned b = 0; b < sizeof record; ++b) reinterpret_cast<uint8_t *>(&record)[b] = uint8_t(rng());
        record.mDrawn = PickByte(rng);
        record.mFlag1d = PickByte(rng);
    }
}
void PickViewerState(std::mt19937 &rng)
{
    DynamicStrategyState *const next = rng() % 3 == 0 ? nullptr : gw.states[rng() % kStates];
    if (Fake *f = FakeOf(next)) ++f->refs;
    if (Fake *f = FakeOf(gw.viewerState)) --f->refs;
    gw.viewerState = next;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 5;
    std::mt19937 rng(seed ^ 0x51e7);
    gw.viewer.Reset<kViewer>(900);
    gw.viewer.On(99, &SetViewerState);
    gw.viewer.On(116, &ViewerState);
    gw.com.Reset<kCom>(901);
    gw.com.On(3, &ClassObject);
    gw.device.Reset<kDevice>(902);
    gw.deviceState.Reset<kDeviceState>(903);
    gw.deviceState.On(12, &ApplyCommand);
    gw.texture.Reset<kSurface>(904);
    for (unsigned i = 0; i < kVariants; ++i) gw.variants[i].Reset<kVariant>(910 + i);
    for (unsigned i = 0; i < kStates; ++i) gw.stateFakes[i].Reset<kUnknown>(920 + i);
    std::memset(&gw.pass, 0, sizeof gw.pass);
    drawfx::AddRange(&gw.pass, sizeof gw.pass, kPassTag);
    drawfx::AddRange(gw.items, sizeof gw.items, kItemsTag);
    drawfx::AddRange(gw.records, sizeof gw.records, kRecordsTag);
    drawfx::AddRange(gw.censor, sizeof gw.censor, kCensorTag);
    gw.pass.mViewer = reinterpret_cast<nGZSceneGraph::cIViewerRefNodeBase *>(&gw.viewer);
    gw.pass.mDevice = reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gw.device);
    gw.pass.mDeviceState = reinterpret_cast<nGZGraphic4::cDeviceStateDX *>(&gw.deviceState);
    PickPass(rng);
    PickItems(rng);
    // The clear colour: four distinct floats.
    static const float kColors[] = {0.0f, 1.0f, 0.5f, 0.25f, -1.0f, 0.125f, 2.0f, 0.75f};
    for (unsigned i = 0; i < 4; ++i)
        cells.Set(kClearColorCell + 4 * i, std::bit_cast<uint32_t>(kColors[(seed + 3 * i) % std::size(kColors)] + float(i)));
}

DynamicStrategyState *MakeState(unsigned i, std::mt19937 &rng)
{
    auto *const s = static_cast<DynamicStrategyState *>(::operator new(sizeof(DynamicStrategyState)));
    for (uint32_t b = 0; b < sizeof(DynamicStrategyState); ++b) reinterpret_cast<uint8_t *>(s)[b] = uint8_t(rng());
    gw.stateFakes[i].Embed(s);
    new (&s->mRects54) Rects();
    FillRects(s->mRects54, rng);
    return s;
}
void FreeStates()
{
    for (DynamicStrategyState *&s : gw.states) {
        std::destroy_at(&s->mRects54);
        ::operator delete(s);
        s = nullptr;
    }
    gw.viewerState = nullptr;
}

void MakeCensor(std::mt19937 &rng)
{
    for (uint8_t &b : gw.censor) b = uint8_t(rng());
    new (&Visible()) Boxes();
    FillVisible(rng);
    *Member<void *>(gw.censor, 0x28) = rng() % 4 ? &gw.texture : nullptr;
}

// ---- the notes ----

void NoteRects(const char *label, const Rects &v)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = label;
    c.words = {uint32_t(v.size()), uint32_t(v.capacity()), drawfx::Ptr(v.data())};
    for (const DynamicWords16 &r : v) c.words.insert(c.words.end(), std::begin(r.w), std::end(r.w));
    drawfx::gLog.push_back(std::move(c));
}
void NoteList(const RectList &list)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "list";
    c.words.push_back(uint32_t(list.size()));
    for (const DynamicWords16 &r : list) c.words.insert(c.words.end(), std::begin(r.w), std::end(r.w));
    drawfx::gLog.push_back(std::move(c));
}
void NoteWorld()
{
    difftest::NotCounted bookkeeping;
    {
        drawfx::Call c;
        c.note = "censor";
        drawfx::Words(c.words, gw.censor, Censor::kSize / 4, "uuuuuuaaauauuu");
        c.words.push_back(uint32_t(Visible().size()));
        c.words.insert(c.words.end(), std::begin(gw.pass.mDrawStats), std::end(gw.pass.mDrawStats));
        c.words.push_back(drawfx::Ptr(gw.viewerState));
        drawfx::gLog.push_back(std::move(c));
    }
    for (const DynamicStrategyState *s : gw.states) NoteRects("frame state rects", s->mRects54);
}
void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.viewer, &gw.com, &gw.device, &gw.deviceState, &gw.texture}) c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.variants) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.stateFakes) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

// ---- the run ----

using CollectFn = int32_t(__thiscall *)(void *, nGZSceneGraph::Pass *, Rects *);
using ListFn = int32_t(__thiscall *)(void *, RectList *);
using RenderFn = void(__thiscall *)(void *, nGZSceneGraph::Pass *, const Items *, const Records *);

void RunSeed(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    for (unsigned i = 0; i < kStates; ++i) gw.states[i] = MakeState(i, rng);
    gw.viewerState = nullptr;
    PickViewerState(rng);
    MakeCensor(rng);
    NoteWorld();
    Rects out;
    RectList list;
    Items items;
    Records records;
    const unsigned steps = 3 + rng() % 12;
    for (unsigned step = 0; step < steps; ++step) {
        sgfx::MarkStep(ctx, "step");
        const unsigned what = rng() % 7;
        drawfx::Note("op", what);
        if (what <= 1) {
            FillRects(out, rng);
            const int32_t r = original ? ctx.Original<CollectFn>(0x377f50)(gw.censor, &gw.pass, &out)
                                       : Ours()->CollectDirtyRects(&gw.pass, &out);
            drawfx::Note("collect", uint32_t(r));
            NoteRects("out", out);
        } else if (what == 2) {
            for (unsigned n = rng() % 5; n; --n) list.push_back(MakeRect(rng));
            const int32_t r = original ? ctx.Original<ListFn>(0x378020)(gw.censor, &list) : Ours()->ListBoxRects(&list);
            drawfx::Note("list boxes", uint32_t(r));
            NoteList(list);
        } else if (what <= 4) {
            items.clear();
            records.clear();
            for (unsigned n = rng() % 4 == 0 ? 0 : rng() % (kMaxItems + 1); n; --n) {
                const unsigned at = rng() % kMaxItems;
                items.push_back(&gw.items[at]);
                records.push_back(&gw.records[rng() % 5 ? at : rng() % kMaxItems]);
            }
            if (original) ctx.Original<RenderFn>(0x378370)(gw.censor, &gw.pass, &items, &records);
            else Ours()->RenderTexture(&gw.pass, &items, &records);
            drawfx::Note("render", 0);
        } else {
            const unsigned change = rng() % 6;
            drawfx::Note("change", change);
            if (change == 0) FillVisible(rng);
            else if (change == 1) *Member<void *>(gw.censor, 0x28) = rng() % 4 ? &gw.texture : nullptr;
            else if (change == 2) PickViewerState(rng);
            else if (change == 3) PickPass(rng);
            else if (change == 4) PickItems(rng);
            else FillRects(gw.states[rng() % kStates]->mRects54, rng);
        }
        NoteWorld();
        drawfx::Note("step heap", {what, uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
    }
    sgfx::MarkStep(ctx, "free");
    {
        Rects().swap(out);
        RectList().swap(list);
        Items().swap(items);
        Records().swap(records);
    }
    std::destroy_at(&Visible());
    FreeStates();
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

// ---- SG_DynamicItemDraw: the dynamic strategy's item drawers and clears (row TS1g) ----

using Self = cDisplayListDynamicRenderStrategy;
using A = cDisplayListDynamicRenderStrategyAdapters;

// cITSRenderSurface as the clears use it: 3 GetScale, 4 GetAspect (ST0), 14 ClearRect (pass, the rectangle, flags).
constexpr At kTargetAt[] = {{3, {0, Ret::Float, true, "GetScale", ""}},
                            {4, {0, Ret::Float, true, "GetAspect", ""}},
                            {14, {3, Ret::Int, true, "ClearRect", "aW4u"}}};
constexpr auto kTargetSlots = Plain<15>(kTargetAt);
constexpr drawfx::Iface kTarget{"TargetSurface", 15, kTargetSlots.data(), false, kDeclared};

constexpr uint32_t kStrategyTag = 0x78000000;
constexpr uint32_t kItemScissorCell = 0x10d9d24;
constexpr uint32_t kOffCensor = 0xc, kOffItems = 0x30, kOffRecords = 0x3c, kOffUseScissoring = 0x78;
constexpr uint32_t kOffTrees = 0x98, kOffDebugMode = 0xc0, kOffDisableTrees = 0xc5, kOffState = 0xd4;
constexpr uint32_t kOffTarget = 0xf0, kOffTargetSecond = 0xf4;

struct DrawWorld {
    Fake targets[2];
    alignas(8) uint8_t strategy[Self::kSize];
};
DrawWorld gd;

Self *Strategy() { return reinterpret_cast<Self *>(gd.strategy); }
Items &StrategyItems() { return *Member<Items>(gd.strategy, kOffItems); }
Records &StrategyRecords() { return *Member<Records>(gd.strategy, kOffRecords); }

float PickScale(std::mt19937 &rng)
{
    static const float kScales[] = {1.0f, 1.0f, 1.0f, 0.5f, 2.0f, 0.75f, 1.5f, 1.0000001f, 0.99999994f, 0.0f, -1.0f,
                                    1.3333334f, 0.333f, std::bit_cast<float>(0x7fc00000u)};
    return kScales[rng() % std::size(kScales)];
}
void PickTargets(std::mt19937 &rng)
{
    for (Fake &f : gd.targets) {
        f.ret[3] = std::bit_cast<uint32_t>(PickScale(rng));
        f.ret[4] = std::bit_cast<uint32_t>(PickScale(rng));
    }
}
// Sort keys about the late bound (11000) and the reflection's (-9998).
void PickDrawItems(std::mt19937 &rng)
{
    static const int32_t kKeys[] = {-9998, -9998, -9997, -9999, 9998, 10999, 11000, 11000, 11001, 0, -1, 30000,
                                    INT32_MIN, INT32_MAX, 2};
    for (unsigned i = 0; i < kMaxItems; ++i) {
        gw.items[i].mSortKey = rng() % 8 ? kKeys[rng() % std::size(kKeys)] : int32_t(rng());
        DynamicRectRecord &r = gw.records[i];
        for (uint8_t *b : {&r.mDrawn, &r.mFlag1d, &r.mFlag1f, &r.mFlag20, &r.mUnknown21, &r.mSecond, &r.mFlag1c})
            *b = PickByte(rng);
        const DynamicWords16 rect = MakeRect(rng);
        std::memcpy(r.mRect, rect.w, sizeof r.mRect);
        if (rng() % 3) {
            for (int k = 0; k < 4; ++k) r.mRect[k] = int32_t(rng() % 1024);
            if (r.mRect[0] > r.mRect[2]) std::swap(r.mRect[0], r.mRect[2]);
            if (r.mRect[1] > r.mRect[3]) std::swap(r.mRect[1], r.mRect[3]);
        }
    }
}
void PickStrategyItems(std::mt19937 &rng)
{
    Items &items = StrategyItems();
    Records &records = StrategyRecords();
    items.clear();
    records.clear();
    for (unsigned n = rng() % 5 == 0 ? 0 : rng() % (kMaxItems + 1); n; --n) {
        const unsigned at = rng() % kMaxItems;
        items.push_back(&gw.items[at]);
        records.push_back(&gw.records[rng() % 5 ? at : rng() % kMaxItems]);
    }
}
void PickFrameState(std::mt19937 &rng)
{
    static const uint32_t kModes[] = {0, 1, 1, 2, 2, 3, 0xffffffffu, 0x102};
    static const uint32_t kFrames[] = {0, 1, 2, 2, 3, 0xffffffffu};
    for (DynamicStrategyState *s : gw.states) {
        s->mMode = rng() % 8 ? kModes[rng() % std::size(kModes)] : rng();
        s->mFrames = kFrames[rng() % std::size(kFrames)];
    }
    *Member<DynamicStrategyState *>(gd.strategy, kOffState) = gw.states[rng() % kStates];
}
void PickSwitches(std::mt19937 &rng)
{
    static const uint32_t kDebugModes[] = {0, 0, 1, 2, 3, 3, 4, 5, 0xffffffffu, 0x103};
    gd.strategy[kOffUseScissoring] = PickByte(rng);
    *Member<uint32_t>(gd.strategy, kOffDebugMode) = kDebugModes[rng() % std::size(kDebugModes)];
    gd.strategy[kOffDisableTrees] = rng() % 3 == 0 ? PickByte(rng) : 0;
}
uint8_t PickCellByte(std::mt19937 &rng)
{
    const uint32_t r = rng() % 4;
    return r < 2 ? 0 : r == 2 ? 1 : uint8_t(rng());
}

void BuildDraw(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed, cells);
    std::mt19937 rng(seed ^ 0xd5a1);
    for (unsigned i = 0; i < 2; ++i) gd.targets[i].Reset<kTarget>(930 + i);
    PickTargets(rng);
    PickDrawItems(rng);
    drawfx::AddRange(gd.strategy, sizeof gd.strategy, kStrategyTag);
    const uint32_t cell = *GameAddress<uint32_t>(kItemScissorCell);
    cells.Set(kItemScissorCell, (cell & ~0xffu) | PickCellByte(rng));
}

// A byte argument as the callers push it: 0 or 1, now and then another low byte, random upper bytes now and then.
uint32_t ByteArg(std::mt19937 &rng)
{
    uint32_t v = rng() % 2;
    if (rng() % 6 == 0) v = rng() & 0xff;
    if (rng() % 4 == 0) v |= rng() & 0xffffff00u;
    return v;
}
uint32_t PickMode(std::mt19937 &rng)
{
    static const uint32_t kOdd[] = {8, 9, 0xffffffffu, 0x100, 0x107, 0x80000000u};
    return rng() % 5 ? rng() % 8 : kOdd[rng() % std::size(kOdd)];
}

void NoteDrawWorld()
{
    difftest::NotCounted bookkeeping;
    {
        drawfx::Call c;
        c.note = "records";
        for (const DynamicRectRecord &r : gw.records) drawfx::Words(c.words, &r, sizeof r / 4, "u");
        drawfx::gLog.push_back(std::move(c));
    }
    {
        // The strategy's words but the vectors (their pointers are each side's), the trees (MarkRecords' own, checked by
        // SG_DynamicMarkRecords) and the pointers the test set (never written).
        static const std::string kinds = [] {
            std::string k(Self::kSize / 4, 'u');
            for (uint32_t at = kOffItems; at < kOffItems + 0x18; at += 4) k[at / 4] = '-';
            for (uint32_t at = kOffTrees; at < kOffDebugMode; at += 4) k[at / 4] = '-';
            for (uint32_t at : {kOffCensor, kOffState, kOffTarget, kOffTargetSecond}) k[at / 4] = '-';
            return k;
        }();
        drawfx::Call c;
        c.note = "strategy";
        drawfx::Words(c.words, gd.strategy, Self::kSize / 4, kinds.c_str());
        c.words.push_back(uint32_t(StrategyItems().size()));
        c.words.push_back(uint32_t(StrategyRecords().size()));
        drawfx::gLog.push_back(std::move(c));
    }
    NoteWorld();
}

using DrawOneFn = void(__thiscall *)(void *, nGZSceneGraph::Pass *, nGZSceneGraph::DrawItem *);
using DrawRunFn = void(__thiscall *)(void *, nGZSceneGraph::Pass *, nGZSceneGraph::DrawItem *, uint32_t, uint32_t);
using DrawByteFn = void(__thiscall *)(void *, nGZSceneGraph::Pass *, uint32_t);
using DrawCensorFn = void(__thiscall *)(void *, nGZSceneGraph::Pass *);
using ClearRectsFn = void(__thiscall *)(void *, nGZSceneGraph::Pass *, const RectList *, uint32_t);

void RunDraw(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    for (unsigned i = 0; i < kStates; ++i) gw.states[i] = MakeState(i, rng);
    gw.viewerState = nullptr;
    PickViewerState(rng);
    MakeCensor(rng);
    for (uint8_t &b : gd.strategy) b = uint8_t(rng());
    new (&StrategyItems()) Items();
    new (&StrategyRecords()) Records();
    auto *const trees = new (gd.strategy + kOffTrees) DynamicStrategyTrees;
    *Member<void *>(gd.strategy, kOffCensor) = gw.censor;
    *Member<void *>(gd.strategy, kOffTarget) = &gd.targets[0];
    *Member<void *>(gd.strategy, kOffTargetSecond) = &gd.targets[1];
    PickStrategyItems(rng);
    PickFrameState(rng);
    PickSwitches(rng);
    NoteDrawWorld();
    void *const self = gd.strategy;
    nGZSceneGraph::Pass *const pass = &gw.pass;
    const unsigned steps = 3 + rng() % 12;
    for (unsigned step = 0; step < steps; ++step) {
        sgfx::MarkStep(ctx, "step");
        const unsigned what = rng() % 8;
        drawfx::Note("op", what);
        if (what == 0) {
            nGZSceneGraph::DrawItem *const item = &gw.items[rng() % kMaxItems];
            if (original) ctx.Original<DrawOneFn>(0x3743a0)(self, pass, item);
            else Strategy()->DrawOneItem(pass, item);
        } else if (what == 1) {
            const unsigned start = rng() % kMaxItems;
            const uint32_t count = rng() % (kMaxItems - start + 1);
            const uint32_t late = ByteArg(rng);
            drawfx::Note("run", {start, count, late});
            if (original) ctx.Original<DrawRunFn>(0x374350)(self, pass, &gw.items[start], count, late);
            else A::DrawItemRun(Strategy(), nullptr, pass, &gw.items[start], count, late);
        } else if (what == 2) {
            const uint32_t reflection = ByteArg(rng);
            drawfx::Note("reflection", reflection);
            if (original) ctx.Original<DrawByteFn>(0x374270)(self, pass, reflection);
            else A::DrawReflectionItems(Strategy(), nullptr, pass, reflection);
        } else if (what == 3) {
            const uint32_t mode = PickMode(rng);
            drawfx::Note("mode", mode);
            if (original) ctx.Original<DrawByteFn>(0x374100)(self, pass, mode);
            else Strategy()->DrawItemsBy(pass, mode);
        } else if (what == 4) {
            if (original) ctx.Original<DrawCensorFn>(0x374ad0)(self, pass);
            else Strategy()->DrawCensor(pass);
        } else if (what == 5) {
            RectList rects;
            for (unsigned n = rng() % 4 == 0 ? 0 : rng() % 6; n; --n) rects.push_back(MakeRect(rng));
            const uint32_t second = ByteArg(rng);
            drawfx::Note("clear", second);
            if (original) ctx.Original<ClearRectsFn>(0x373b70)(self, pass, &rects, second);
            else A::ClearRects(Strategy(), nullptr, pass, &rects, second);
        } else {
            const unsigned change = rng() % 9;
            drawfx::Note("change", change);
            if (change == 0) PickDrawItems(rng);
            else if (change == 1) PickStrategyItems(rng);
            else if (change == 2) PickFrameState(rng);
            else if (change == 3) PickSwitches(rng);
            else if (change == 4) PickTargets(rng);
            else if (change == 5) *GameAddress<uint8_t>(kItemScissorCell) = PickCellByte(rng);
            else if (change == 6) FillVisible(rng);
            else if (change == 7) PickViewerState(rng);
            else *Member<void *>(gw.censor, 0x28) = rng() % 4 ? &gw.texture : nullptr;
        }
        NoteDrawWorld();
        drawfx::Note("step heap", {what, uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
    }
    sgfx::MarkStep(ctx, "free");
    trees->~DynamicStrategyTrees();
    std::destroy_at(&StrategyItems());
    std::destroy_at(&StrategyRecords());
    std::destroy_at(&Visible());
    FreeStates();
    NoteFakes();
    drawfx::Note("target refs", {uint32_t(gd.targets[0].refs), uint32_t(gd.targets[1].refs)});
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: CollectDirtyRects 0x377f50, CopyBoxRects 0x378140, GetViewerFrameState 0x378200, ListBoxRects 0x378020,
// RenderTexture 0x378370, SetUpTexture 0x378400, DrawItems 0x3782c0, DrawOne 0x378350.
DIFF_TEST(SG_CensorDirtyRects, 0x377f50)
{
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    {
        // One run first, its log dropped: the first run keeps one block for good (a one-time initialisation outside
        // the code under test), which no later run repeats.
        drawfx::Cells warm;
        drawfx::Side([&] { Build(0, warm); }, [&] {
            std::mt19937 rng(13);
            RunSeed(ctx, true, rng);
        });
    }
    sgfx::CheckSides(ctx, "censor dirty rects", 3000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
                     [&](bool original, std::mt19937 &rng) {
                         RunSeed(ctx, original, rng);
                         ++ctx.cases;
                     });
}

// Verifies: DrawOneItem 0x3743a0, DrawItemRun 0x374350, DrawReflectionItems 0x374270, DrawItemsBy 0x374100, DrawCensor
// 0x374ad0, ClearRects 0x373b70 (the dynamic strategy's, row TS1g).
DIFF_TEST(SG_DynamicItemDraw, 0x374100)
{
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    {
        // A warm-up run, its log dropped, as SG_CensorDirtyRects' (the first run's one-time block).
        drawfx::Cells warm;
        drawfx::Side([&] { BuildDraw(0, warm); }, [&] {
            std::mt19937 rng(13);
            RunDraw(ctx, true, rng);
        });
    }
    sgfx::CheckSides(ctx, "dynamic item draw", 3000,
                     [&](uint32_t seed, drawfx::Cells &cells) { BuildDraw(seed, cells); },
                     [&](bool original, std::mt19937 &rng) {
                         RunDraw(ctx, original, rng);
                         ++ctx.cases;
                     });
}
