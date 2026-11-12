// Differential test of nTSSG::cDisplayListCensorRenderStrategy (lane 261108c, scenegraph TS1;
// src/sg/cDisplayListCensorRenderStrategy.h).
//
//   SG_CensorStrategyLife  one strategy's life on each side: built by the constructor 0x3775c0 on a block filled alike,
//       or by the factory 0x377ee0 (its slot start, which neither writes, then set alike), then random calls through its
//       vtables: AddBox (+8 slot 7, 0x377880, growth 0x377190 / 0x377520), BeginPass (+8 slot 3, 0x377920) on a pass
//       whose pixel viewport, size and display list change between calls, Clear (+8 slot 5: the thunk 0x3782b0 to
//       0x377ec0, 0x378560 per list), the empty slots 4, 6, 8, 9, 10, QueryInterface (0x373cd0, through +0 and through
//       the +8 thunk) with each interface id, null `out` too, AddRef / Release / RefCount, and the vector destructor
//       0x3776c0 on either list. After each call the object is noted: the vector words, the material and slot words,
//       each box (its rectangle and flag once BeginPass has written them; the game's AddBox leaves them unset) and each
//       box's geometry (its count and the words BeginPass writes, +0x7c..+0xa8). At the end the strategy is deleted
//       (the deleting destructor 0x377860 with the destructor 0x377790, or the last Release), then the fakes' counts
//       and the heap.
//
// Stood in on both sides: the box projection 0xaf22d0 (nGZSceneGraph::ProjectBox, a StandIn; it notes the box and matrix pointers and answers the
// box's own corners, so the boxes the test picks are the projected corners: exact integers, halves, values next to
// integers, values past 2^22, infinities, NaN), and the game's operator new / delete / free (to ours, so the original's
// blocks are counted and normalised like ours). Cells: the material manager 0x10f8fcc (FindOrLoad answers an index or
// -1), the detail display list 0x10f9028 (ReallocSlots answers a start; FreeSlots noted) and the vertex formats manager
// 0x10caa24 that each box's new cDynamicGeometryRect asks (the original's 0xa43ec0 on the game's side, ours here). The
// pass's display list (Pass::mOwner) is another display-list fake, its AddGeometry noted.
#include "sgfx.h"

#include "code_patch.h"

#include "sg/SGUtilities.h"
#include "sg/cDisplayList.h"
#include "sg/cDisplayListCensorRenderStrategy.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using A = cDisplayListCensorRenderStrategyAdapters;

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

// cIMaterialManager: 19 FindOrLoad, 20 AddRefIndex, 21 ReleaseIndex.
constexpr At kMaterialsAt[] = {{19, {1, Ret::Int, true, "FindOrLoad", "s"}},
                               {20, {1, Ret::Int, true, "AddRefIndex", "u"}},
                               {21, {1, Ret::Int, true, "ReleaseIndex", "u"}}};
constexpr auto kMaterialsSlots = Table<22>(kMaterialsAt);
constexpr drawfx::Iface kMaterials{"MaterialManager", 22, kMaterialsSlots.data()};

// cIDisplayList: 19 ReallocSlots, 20 FreeSlots, 29 AddGeometry.
constexpr At kListAt[] = {{19, {3, Ret::Int, true, "ReallocSlots", "uuu"}},
                          {20, {2, Ret::Int, true, "FreeSlots", "uu"}},
                          {29, {4, Ret::Int, true, "AddGeometry", "auau"}}};
constexpr auto kListSlots = Table<30>(kListAt);
constexpr drawfx::Iface kList{"DisplayList", 30, kListSlots.data()};

// The vertex formats manager (3 BeginFormat, 4 AddElement, 5 EndFormat) and the format it makes (11 GetStreamSize,
// 15 GetOffsetAny), as cDynamicGeometryRect's constructor asks them.
constexpr At kFormatsAt[] = {{3, {0, Ret::Int, true, "BeginFormat", ""}},
                             {4, {4, Ret::Int, true, "AddElement", "uuub"}},
                             {5, {1, Ret::Int, true, "EndFormat", "a"}}};
constexpr auto kFormatsSlots = Table<6>(kFormatsAt);
constexpr drawfx::Iface kFormats{"VertexFormatsManager", 6, kFormatsSlots.data()};
constexpr At kFormatAt[] = {{11, {1, Ret::Int, true, "GetStreamSize", "u"}},
                            {15, {2, Ret::Int, true, "GetOffsetAny", "ub"}}};
constexpr auto kFormatSlots = Table<16>(kFormatAt);
constexpr drawfx::Iface kFormat{"VertexFormat", 16, kFormatSlots.data()};

constexpr uint32_t kMaterialsCell = 0x10f8fcc, kDetailCell = 0x10f9028, kFormatsCell = 0x10caa24;
constexpr uint32_t kPassTag = 0x7e000000;
constexpr uint32_t kSize = cDisplayListCensorRenderStrategy::kSize;
constexpr unsigned kMaxBoxes = 40; // the vectors stay under 4 KB (larger blocks are aligned inside the allocation)

struct World {
    Fake materials, detail, owner, formats, format;
    nGZSceneGraph::Pass pass;
    uint32_t lcg;
    uint32_t Next() { return lcg = lcg * 1664525u + 1013904223u, lcg >> 8; }
};
World gw;

uint32_t FindOrLoad(Fake &, const uint32_t *)
{
    const uint32_t r = gw.Next();
    return r % 4 == 0 ? 0xffffffffu : 3 + r % 5;
}
uint32_t ReallocSlots(Fake &, const uint32_t *a) { return a[2] ? 100 + gw.Next() % 1000 : 0; }
uint32_t EndFormat(Fake &, const uint32_t *a)
{
    ++gw.format.refs;
    *reinterpret_cast<void **>(uintptr_t(a[0])) = &gw.format;
    return 0;
}
uint32_t Word0(Fake &self, const uint32_t *) { return self.value[0]; }

// 0xaf22d0 stood in: the box's corners as the projection.
void __cdecl StandProject(float *lo, float *hi, const nGZCompGeom::BoundBox *box, const float *matrix)
{
    drawfx::Note("project", {drawfx::Ptr(box), drawfx::Ptr(matrix)});
    lo[0] = box->min.x, lo[1] = box->min.y, lo[2] = box->min.z;
    hi[0] = box->max.x, hi[1] = box->max.y, hi[2] = box->max.z;
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

struct Stands {
    shape::StandIn project{0xaf22d0u, &nGZSceneGraph::ProjectBox, &StandProject};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

const float kNan = std::bit_cast<float>(0xffc00000u);
const float kInf = std::bit_cast<float>(0x7f800000u);
const float kOdd[] = {0.0f, -0.0f, 0.5f, -0.5f, 1.5f, 2.5f, 0.49999997f, 0.50000006f, 99.99999f, 100.00001f,
                      4194304.5f, 8388607.0f, 8388608.5f, 16777216.0f, -4194305.5f, 1e10f, -1e10f, kInf, -kInf, kNan,
                      639.5f, 640.0f, 480.0f, 479.99997f, -1.0f, 1e-30f};
const int32_t kRects[][4] = {{0, 0, 640, 480},     {10, 20, 300, 200},    {0, 0, 1, 1},      {100, 100, 50, 50},
                             {5, 5, 5, 300},       {-100, -50, 100, 50},  {0, 0, 1024, 768}, {320, 0, 640, 480},
                             {-2147483647 - 1, -2147483647 - 1, 2147483647, 2147483647}, {0, 0, 0, 0},
                             {2147483600, 0, -2147483600, 480}};
const uint32_t kSides[] = {640, 480, 1024, 768, 1, 0, 0x80000000u, 0xffffffffu, 300};

float Coordinate(std::mt19937 &rng)
{
    const uint32_t r = rng();
    if (r % 5 == 0) return kOdd[(r / 5) % std::size(kOdd)];
    static const float kFractions[] = {0.0f, 0.5f, 0.25f, 0.75f, 0.9999f, 0.000001f, 0.3f};
    const float whole = static_cast<float>(static_cast<int32_t>((r >> 4) % 900) - 150);
    return whole + kFractions[(r >> 16) % std::size(kFractions)];
}

nGZCompGeom::BoundBox MakeBox(std::mt19937 &rng)
{
    nGZCompGeom::BoundBox b;
    b.min.x = Coordinate(rng);
    b.min.y = Coordinate(rng);
    b.max.x = rng() % 4 == 0 ? Coordinate(rng) : b.min.x + static_cast<float>(rng() % 200) + 0.5f * (rng() % 2);
    b.max.y = rng() % 4 == 0 ? Coordinate(rng) : b.min.y + static_cast<float>(rng() % 200) + 0.25f * (rng() % 4);
    static const float kDepths[] = {0.0f, -0.0f, 0.5f, 1.0f, -1.0f, 0.999f, 2.0f, -0.25f};
    b.min.z = rng() % 9 == 0 ? kNan : kDepths[rng() % std::size(kDepths)];
    b.max.z = kDepths[rng() % std::size(kDepths)];
    return b;
}

void ChangePass(std::mt19937 &rng)
{
    nGZSceneGraph::Pass &p = gw.pass;
    std::memcpy(p.mPixelRect, kRects[rng() % std::size(kRects)], sizeof p.mPixelRect);
    p.mWidth = kSides[rng() % std::size(kSides)];
    p.mHeight = kSides[rng() % std::size(kSides)];
}

// A box whose far edge rounds to INT_MIN + 6 (Ceil(-25165830.0f) is -2147483642), with the pass's left edge on the box's
// rounded right edge (or its bottom edge on the box's rounded top edge), give or take one: the overlap test's strict
// comparisons decide these, as the clipped extent wraps round to positive.
constexpr float kFar = -25165830.0f;
constexpr int32_t kFarRounded = -2147483642;
nGZCompGeom::BoundBox EdgeBox(std::mt19937 &rng)
{
    nGZCompGeom::BoundBox b = MakeBox(rng);
    const int32_t d = static_cast<int32_t>(rng() % 3) - 1;
    nGZSceneGraph::Pass &p = gw.pass;
    if (rng() % 2) {
        b.min.x = 100.0f, b.max.x = kFar, b.min.y = 10.0f, b.max.y = 50.0f;
        const int32_t r[4] = {kFarRounded + d, 0, 640, 480};
        std::memcpy(p.mPixelRect, r, sizeof r);
    } else {
        b.min.x = 10.0f, b.max.x = 50.0f, b.min.y = 100.0f, b.max.y = kFar;
        const int32_t r[4] = {0, -2147483647 - 1, 640, 100 + d};
        std::memcpy(p.mPixelRect, r, sizeof r);
    }
    return b;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 11;
    gw.materials.Reset<kMaterials>(760);
    gw.materials.On(19, &FindOrLoad);
    gw.detail.Reset<kList>(761);
    gw.detail.On(19, &ReallocSlots);
    gw.owner.Reset<kList>(762);
    gw.formats.Reset<kFormats>(763);
    gw.formats.On(5, &EndFormat);
    gw.format.Reset<kFormat>(764);
    gw.format.On(11, &Word0);
    gw.format.On(15, &Word0);
    gw.format.value[0] = 20;
    std::memset(&gw.pass, 0, sizeof gw.pass);
    gw.pass.mOwner = reinterpret_cast<nGZSceneGraph::cDisplayList *>(&gw.owner);
    std::mt19937 rng(seed);
    ChangePass(rng);
    for (float &m : gw.pass.mMatrices[4]) m = static_cast<float>(rng() % 17) - 8.0f;
    drawfx::AddRange(&gw.pass, sizeof gw.pass, kPassTag);
    cells.Set(kMaterialsCell, &gw.materials);
    cells.Set(kDetailCell, &gw.detail);
    cells.Set(kFormatsCell, &gw.formats);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Note("fake refs", {uint32_t(gw.materials.refs), uint32_t(gw.detail.refs), uint32_t(gw.owner.refs),
                               uint32_t(gw.formats.refs), uint32_t(gw.format.refs)});
}

// ---- the object ----

uint32_t W(const void *o, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(o) + at); }
uint8_t B(const void *o, uint32_t at) { return static_cast<const uint8_t *>(o)[at]; }
uint8_t *Face(void *o) { return static_cast<uint8_t *>(o) + 8; }
uint32_t BoxCount(const void *o, uint32_t at) { return (W(o, at + 4) - W(o, at)) / 0x30; }

// The boxes of the list at `at`: each box's corners, its rectangle and flag (the first `written` boxes: the game's AddBox
// leaves them unset), its geometry and the geometry's count and the words BeginPass writes.
void NoteBoxes(const char *label, const void *o, uint32_t at, uint32_t written)
{
    const uint32_t count = BoxCount(o, at);
    const auto *first = reinterpret_cast<const uint8_t *>(uintptr_t(W(o, at)));
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *const box = first + i * 0x30;
        drawfx::Call c;
        c.note = label;
        drawfx::Words(c.words, box, 6, "u");
        if (i < written) {
            drawfx::Words(c.words, box + 0x18, 4, "u");
            c.words.push_back(B(box, 0x2c));
        }
        drawfx::Words(c.words, box + 0x28, 1, "a");
        if (const uint8_t *const geometry = reinterpret_cast<const uint8_t *>(uintptr_t(W(box, 0x28)))) {
            c.words.push_back(W(geometry, 4));
            drawfx::Words(c.words, geometry + 0x7c, 12, "u");
        }
        drawfx::gLog.push_back(std::move(c));
    }
}

void NoteObject(const void *o, uint32_t written)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "object";
    drawfx::Words(c.words, o, kSize / 4, "-u-aaaaaauuuuu");
    drawfx::gLog.push_back(std::move(c));
    NoteBoxes("box", o, 0xc, written);
    NoteBoxes("drawn", o, 0x18, BoxCount(o, 0x18));
}

// A call through slot kSlot of the vtable at `self` (each side's own vtable).
template <unsigned kSlot, class R, class... Args> R Virtual(void *self, Args... args)
{
    using F = R(__thiscall *)(void *, Args...);
    return (*reinterpret_cast<F *const *>(self))[kSlot](self, args...);
}
template <unsigned kSlot, class R = uint32_t, class... Args> R Slot8(void *o, Args... args)
{
    return Virtual<kSlot, R>(Face(o), args...);
}
template <unsigned kSlot, class R = uint32_t, class... Args> R Slot0(void *o, Args... args)
{
    return Virtual<kSlot, R>(o, args...);
}

void QueryInterface(void *o, std::mt19937 &rng)
{
    static const uint32_t kIids[] = {1u, 0x0bf077fdu, 0x2ca4c07du, 0x49d02f0bu, 0u};
    const uint32_t iid = kIids[rng() % std::size(kIids)];
    void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
    void **const where = rng() % 6 == 0 ? nullptr : &out;
    const bool viaFace = rng() % 2 != 0;
    const bool found = (viaFace ? Slot8<0, uint8_t>(o, iid, where) : Slot0<0, uint8_t>(o, iid, where)) != 0;
    drawfx::Note("query", {iid, uint32_t(viaFace), uint32_t(found), found && where ? drawfx::Ptr(out) : drawfx::P(out), W(o, 4)});
    if (found) Slot0<2>(o);
}

struct Run {
    difftest::Context &ctx;
    bool original;
    void *o;
    uint32_t written = 0;

    void Step(std::mt19937 &rng)
    {
        const unsigned what = rng() % 20;
        if (what < 7) {
            const unsigned n = 1 + rng() % 3;
            for (unsigned i = 0; i < n && BoxCount(o, 0xc) < kMaxBoxes; ++i) {
                const nGZCompGeom::BoundBox box = MakeBox(rng);
                Slot8<7, void>(o, &box);
            }
        } else if (what < 12) {
            if (rng() % 3 == 0) ChangePass(rng);
            if (rng() % 6 == 0) {
                const nGZCompGeom::BoundBox box = EdgeBox(rng);
                if (BoxCount(o, 0xc) < kMaxBoxes) Slot8<7, void>(o, &box);
            }
            if (rng() % 4 == 0) gw.format.value[0] = rng() % 64;
            Slot8<3, void>(o, &gw.pass);
            if (BoxCount(o, 0xc) != 0 && W(o, 0x24) != 0xffffffffu) written = BoxCount(o, 0xc);
        } else if (what < 14) {
            Slot8<5, void>(o);
            written = 0;
        } else if (what == 14) {
            Slot8<4, void>(o, &gw.pass, static_cast<nGZSceneGraph::DrawItem *>(nullptr), rng() % 4);
            Slot8<6, void>(o);
            Slot8<8, void>(o);
            Slot8<9, void>(o, &gw.owner);
            Slot8<10, void>(o, &gw.owner);
        } else if (what < 18) {
            QueryInterface(o, rng);
        } else if (what == 18) {
            drawfx::Note("addref, release, count", {Slot0<1>(o), Slot0<2>(o), Slot0<5>(o)});
        } else {
            // The vector destructor 0x3776c0 on one list (left empty, its words null).
            const uint32_t at = rng() % 2 ? 0xc : 0x18;
            auto *const list = reinterpret_cast<std::vector<CensorBox> *>(static_cast<uint8_t *>(o) + at);
            if (original) ctx.Original<void(__thiscall *)(void *)>(0x3776c0)(list);
            else A::DestroyBoxes(list, nullptr);
            if (at == 0xc) written = 0;
        }
    }
};

void RunLife(difftest::Context &ctx, uint32_t seed, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *o;
    if (seed % 4 == 0) {
        void *const face = original ? ctx.Original<void *(__cdecl *)()>(0x377ee0)() : static_cast<void *>(A::Create());
        o = static_cast<uint8_t *>(face) - 8;
        drawfx::Note("made", drawfx::Ptr(o));
        // The slot start neither factory writes, set alike.
        *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(o) + 0x30) = rng();
    } else {
        o = ::operator new(kSize);
        for (uint32_t i = 0; i < kSize; ++i) static_cast<uint8_t *>(o)[i] = uint8_t(rng());
        if (original) ctx.Original<void *(__thiscall *)(void *)>(0x3775c0)(o);
        else A::Construct(static_cast<cDisplayListCensorRenderStrategy *>(o), nullptr);
        drawfx::Note("made", drawfx::Ptr(o));
    }
    Run run{ctx, original, o};
    NoteObject(o, 0);
    Slot0<1>(o);
    const unsigned steps = 4 + rng() % 40;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        run.Step(rng);
        NoteObject(o, run.written);
    }
    sgfx::MarkStep(ctx, "delete");
    if (rng() % 2) {
        drawfx::Note("released", Slot0<2>(o));
    } else {
        void *const result = Slot0<3, void *>(o, 1u);
        drawfx::Note("deleted", uint32_t(result == o));
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the constructor 0x3775c0, the factory 0x377ee0, the destructor 0x377790 and deleting destructor 0x377860,
// BeginPass 0x377920, AddBox 0x377880, Clear 0x377ec0 (and its thunk 0x3782b0), the vector code 0x377190, 0x377520,
// 0x3776c0, 0x378560, QueryInterface 0x373cd0 as this class's vtable has it, and the empty slots.
DIFF_TEST(SG_CensorStrategyLife, 0x377920)
{
    Stands stands;
    uint32_t current = 0;
    sgfx::CheckSides(
        ctx, "censor strategy", 3000,
        [&](uint32_t seed, drawfx::Cells &cells) {
            current = seed;
            Build(seed, cells);
        },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, current, original, rng);
            ++ctx.cases;
        });
}
