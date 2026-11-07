// Differential tests of the node-template copies' switch (lane SY, scenegraph F1b): the game's complete constructors of
// cTransformNode (0xa3a000) and cCompositionTreeNode (0xa36900) against our hook adapters (sg/SGNodeCopies.h), which
// build our class in the memory they are given.
//  - SG_NodeCopies_TransformConstruct_*: the transform-node pilot (sg_transform_pilot.h) with our nodes made by the
//    adapter in fresh memory; the original side's factory 0xa3a6c0 runs the constructor 0xa3a000 in its fresh memory, so
//    every node, slot, event and destructor compared is the game's chain (constructor levels, vtables, template copies)
//    against ours.
//  - SG_NodeCopies_*InPlace: each side runs its constructor in fresh memory (the original's at its RVA, ours through the
//    adapter): the node it returns is that memory; a small forest of such nodes is linked, its words (but the vtable words)
//    and shape compared, then released (the blocks freed compared). SG_NodeCopies_ShapeRefInPlace does the same for the
//    game's shape-ref constructor 0xa541e0 against lane SP's Construct, SG_NodeCopies_LightRefInPlace (lane TE) for the
//    light-ref constructor 0xa60c00 against cLightRefNode::Construct (the settings service answering the Impl's bounds
//    inflation).
// The game's instantiation copies these cover (db/status/SGNodeCopies.jsonl, in effect through their callers): the
// original side of each in-place test runs the game's constructor chain, the links, the extension and the release, so:
//  - transform: level constructors 0xa39f10 0xa39f20 0xa39f40 0xa39fa0; destructors 0xa3a070 0xa3a080 0xa3a0c0
//    0xa3a100 0xa3a120; deleting 0xa3a1d0; slots 0xa3a680 0xa3a790 0xa3b0b0; Release 0xa3afa0 0xa3afb0 0xa3afb5;
//  - composition: constructors 0xa353a0 0xa353b0 0xa36870; destructors 0xa36a00 0xa36a10 0xa36b10 0xa36bb0; deleting
//    0xa370b0; slots 0xa378d0 0xa37cb0 0xa38ed0; Release 0xa38d80 0xa38d90 0xa38d95;
//  - shape ref: destructors 0xa54300 0xa54370 0xa54330 0xa54460 0xa54310 0xa543b0;
//  - light ref (lane TE): level constructors 0xa60920 (behaviour) 0xa60a80 (object graph) 0xa60990 (composition) 0xa60b40
//    (transform) 0xa60930 (bounded) 0xa60ae0 (renderable) 0xa60a20 (light ref); destructors 0xa60d70 0xa60e10 0xa60da0
//    0xa60e70 0xa60d80 0xa60e50 0xa60de0 (the same order).
// Held only by a template level's own vtable (live in the game only while a constructor or destructor chain runs; our
// class holds our template's body for the slot, which the class tests compare against the game's class slot): the
// level deleting destructors 0xa3a150 0xa3a170 0xa3a190 0xa3a1b0, 0xa36f60 0xa36f80 0xa37060, 0xa54690 0xa54710
// 0xa546e0 0xa54810 0xa546b0 0xa547b0 0xa547e0; slots 0xa3a2d0, 0xa38f20, 0xa55330 0xa54ce0 0xa54dc0 0xa588a0; Release
// 0xa3afa5 0xa38d85. Light ref (lane TE): the level deleting destructors 0xa60fd0 0xa61080 0xa61020 0xa610e0 0xa60ff0
// 0xa610b0 0xa61050; slots 0xa63690 (object graph's 5), 0xa61540 (composition's 14), 0xa61640 (14 of transform, bounded,
// renderable), 0xa61b30 (3 of composition to renderable); the +8 Release thunk 0xa63505.
// Lane TA (F1b part 2), the Impls of two node classes not ours yet, hooked at their RVAs:
//  - SG_NodeCopies_LightRefImpl: the light reference's Impl constructor 0xa60c20 and destructor 0xa60ec0 (the bounds
//    inflation read from the configuration service, the references released);
//  - SG_NodeCopies_ViewerRefImpl: the viewer reference's Impl constructor 0xa644c0 and destructor 0xa64840 (the entries'
//    vector with their material handles, freed and dropped).
#include "sg_renderable_fx.h"
#include "sg_transform_pilot.h"
#include "sg_viewer_ref_fx.h"

#include "sg/SGNodeCopies.h"
#include "sg/cLightRefNode.h"
#include "sg/cShapeRefNode.h"
#include "sg/cViewerRefNodeImpl.h"
#include "ts_services_fixture.h"

#include <array>
#include <cstring>

namespace {

using namespace nGZSceneGraph;

cICompositionTreeNode *__cdecl MakeTransformByConstruct()
{
    return node_copies::ConstructTransformNode(::operator new(cTransformNode::kSize), nullptr);
}

struct TransformCfg {
    using Interface = cITransformNode;
    static constexpr uint32_t kFactory = 0xa3a6c0;
    static constexpr uint32_t kSize = cTransformNode::kSize;
    static constexpr int kLevel = 0;
    static constexpr uint32_t kIid = cITransformNode::kIid;
    static constexpr uint32_t kEventCell = cTransformNodeBehaviorTraits::kEventManagerCell;
    static cICompositionTreeNode *Create() { return MakeTransformByConstruct(); }
};
using TP = sgtp::Pilot<TransformCfg>;

// ---- in place, both classes ----

using Ctor = void *(__thiscall *)(void *);
using Adapter = void *(__fastcall *)(void *, void *);

struct InPlace {
    uint32_t rva;
    uint32_t size;
    uint32_t cell; // the behaviour's event-manager cell
    Adapter ours;
    uint64_t skip = 0b101;   // the words left out (the vtable words at +0 and +8, and any heap or unwritten words)
    bool renderable = false; // the renderable level's services (names registry, detail) stood in, set up per seed
    void (*more)(uint32_t seed, drawfx::Cells &cells) = nullptr; // a class's own stand-ins, set up per seed
};

struct World {
    sgfx::Nodes nodes;
    drawfx::Fake ext[2]; // ids 200-201
    sgfx::Manager manager;
};
World w;
difftest::Context *gCtx;
const InPlace *gWhich;
bool gOriginal;
unsigned gMade, gInPlace;

cICompositionTreeNode *__cdecl MakeInPlace()
{
    void *const memory = ::operator new(gWhich->size);
    void *const made = gOriginal ? gCtx->Original<Ctor>(gWhich->rva)(memory) : gWhich->ours(memory, nullptr);
    ++gMade;
    gInPlace += made == memory;
    return static_cast<cICompositionTreeNode *>(made);
}

void InPlaceTest(difftest::Context &ctx, const InPlace &which)
{
    gCtx = &ctx;
    gWhich = &which;
    sgfx::RegistryScope node(sgfx::kNodeRegistry);
    sgfx::RegistryScope transform(sgfx::kTransformRegistry);
    sgfx::CheckSides(
        ctx, "in place", 32,
        [&](uint32_t seed, drawfx::Cells &cells) {
            std::mt19937 rng(seed);
            sgfx::gPool.Reset(cells);
            sgfx::gReader.Reset();
            sgfx::gWriter.Reset();
            for (unsigned i = 0; i < 2; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
            w.nodes = sgfx::Nodes{};
            w.manager.Drop();
            w.manager.Use(cells, which.cell);
            if (which.renderable) sgrx::Setup(rng);
            if (which.more) which.more(seed, cells);
        },
        [&](bool original, std::mt19937 &rng) {
            gOriginal = original;
            gMade = gInPlace = 0;
            const unsigned n = 1 + rng() % 6;
            int parents[sgfx::Nodes::kMax];
            parents[0] = -1;
            for (unsigned i = 1; i < n; ++i) parents[i] = rng() % 4 ? int(rng() % i) : -1;
            w.nodes.Make(&MakeInPlace, n, which.size);
            drawfx::Note("made in place", {gMade, gInPlace});
            w.nodes.NoteWords("new nodes", which.skip);
            w.nodes.Link(parents);
            if (rng() % 2) w.nodes[rng() % n]->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[rng() % 2]));
            w.nodes.NoteShape("linked");
            w.nodes.NoteWords("linked", which.skip);
            difftest::FreeRecorder freed;
            w.nodes.Release();
            freed.Stop();
            size_t sizes[32];
            const size_t k = freed.SortedSizes(sizes, 32);
            drawfx::Call c;
            c.note = "freed";
            for (size_t i = 0; i < k; ++i) c.words.push_back(uint32_t(sizes[i]));
            drawfx::gLog.push_back(std::move(c));
            for (const drawfx::Fake &e : w.ext) drawfx::Note("extension", {e.id, uint32_t(e.refs)});
            sgfx::gPool.NoteCounts();
        });
    w.manager.Drop();
}

void *__fastcall OursComposition(void *memory, void *) { return node_copies::ConstructCompositionTreeNode(memory, nullptr); }
void *__fastcall OursTransform(void *memory, void *) { return node_copies::ConstructTransformNode(memory, nullptr); }

const InPlace kComposition{0xa36900, cCompositionTreeNode::kSize, cCompositionTreeNodeBehaviorTraits::kEventManagerCell,
                           &OursComposition};
const InPlace kTransform{0xa3a000, cTransformNode::kSize, cTransformNodeBehaviorTraits::kEventManagerCell, &OursTransform};

// Shape ref: lane SP's Construct (hooked at 0xa541e0). Left out besides the vtable words: word 29 (the detail limit the
// renderable constructor never writes) and the vector words 31-36 / 44-49 (heap addresses).
void *__fastcall OursShapeRef(void *memory, void *) { return static_cast<cShapeRefNode *>(memory)->Construct(); }
const InPlace kShapeRef{0xa541e0, cShapeRefNode::kSize, cShapeRefNodeBehaviorTraits::kEventManagerCell, &OursShapeRef,
                        0b101ull | 1ull << 29 | 0x3full << 31 | 0x3full << 44, true};

// The shape-reference behaviour's registry (0xa585a0, ours since lane TF, stood in over the planted map on both sides):
// its map, guard, holder once byte and guard.
constexpr sgfx::RegistryStatics kShapeRefRegistry{0x10f62d0, 0x10f62d8, 0x10f62e0, 0x10f62e8};

// ---- the light reference's Impl (0xa60c20 / 0xa60ec0) ----

// The configuration service's settings object: slot 9 reads a property into an any (value at +4); the fake answers a
// programmed value (value[0], while value[1]) and logs the key's text, or leaves the any alone.
constexpr drawfx::Slot kSettingsSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"}, {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},          {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {2, drawfx::Ret::Int, true, "GetProperty", "ii"},
};
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots};

struct LightRefWorld {
    sgtx::World<33> impl; // a cLightRefNodeImpl is 0x84 bytes
    drawfx::Fake settings, light, held;
    unsigned asked = 0;
};
LightRefWorld lw;

uint32_t SettingsGet(drawfx::Fake &self, const uint32_t *a)
{
    // The key: a service string (its text pointer first; short ones are not inline in this string class).
    sgfx::NoteText("key", *reinterpret_cast<const char *const *>(uintptr_t(a[0])));
    if (!self.value[1]) return 0;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1]) + 4) = self.value[0];
    return 1;
}
uint32_t SettingsService(drawfx::Fake &self, const uint32_t *a)
{
    if (a[0] == 0x0ac1887e && a[1] == 0x4bb0d269) {
        ++lw.settings.refs;
        ++lw.asked;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &lw.settings;
        return 1;
    }
    return sgfx::detail::GetSystemService(self, a);
}

void LightRefBuild(uint32_t seed, drawfx::Cells &cells)
{
    lw.impl.Build(seed, cells);
    sgfx::gResources.Reset(cells);
    sgfx::gResources.framework.On(5, &SettingsService);
    lw.settings.Reset<kSettings>(480);
    lw.settings.On(9, &SettingsGet);
    // No answer (0), or one of a few values (the top bit set: the unsigned conversion; past 2^24: float rounding).
    const uint32_t values[] = {0, 1, 7, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x01000001u, 0xfedcba98u};
    lw.settings.value[0] = values[seed % 8];
    lw.settings.value[1] = seed % 5 != 0;
    lw.asked = 0;
    lw.light.Reset<sgfx::kUnknown>(481);
    lw.held.Reset<sgfx::kUnknown>(482);
}

// Light ref (lane TE): our Construct (hooked at 0xa60c00). Left out besides the vtable words: word 29 (the detail limit)
// and word 34 (the flag byte and three bytes of padding the constructor never writes). The settings service answers as
// for the Impl test (the bounds inflation, word 32).
void LightRefServices(uint32_t seed, drawfx::Cells &cells)
{
    sgfx::gResources.Reset(cells);
    sgfx::gResources.framework.On(5, &SettingsService);
    lw.settings.Reset<kSettings>(480);
    lw.settings.On(9, &SettingsGet);
    const uint32_t values[] = {0, 1, 7, 0x7fffffffu, 0x80000000u, 0xffffffffu, 0x01000001u, 0xfedcba98u};
    lw.settings.value[0] = values[seed % 8];
    lw.settings.value[1] = seed % 5 != 0;
}
void *__fastcall OursLightRef(void *memory, void *) { return static_cast<cLightRefNode *>(memory)->Construct(); }
const InPlace kLightRef{0xa60c00, cLightRefNode::kSize, 0x10f6380, &OursLightRef, 0b101ull | 1ull << 29 | 1ull << 34,
                        true, &LightRefServices};
constexpr sgfx::RegistryStatics kLightRefRegistry{0x10f6374, 0x10f637c, 0x10f6384, 0x10f638c};

// ---- the viewer reference's Impl (0xa644c0 / 0xa64840) ----

// A viewer Impl block (0x104 bytes and a spare word), beside sg_viewer_ref_fx.h's world, whose stand-ins the viewer
// base's constructor and destructor use.
alignas(8) uint32_t gViewerBlock[0x108 / 4];

// The material manager (GetCachedQueriedObject's cell): the entries' handles drop their indices through slot 21.
constexpr std::array<drawfx::Slot, 22> MakeMaterialManagerSlots()
{
    std::array<drawfx::Slot, 22> a{};
    for (drawfx::Slot &s : a) s = {0, drawfx::Ret::Int, false, "", ""};
    a[0] = {2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, drawfx::Ret::Int, true, "AddRef", ""};
    a[2] = {0, drawfx::Ret::Int, true, "Release", ""};
    a[20] = {1, drawfx::Ret::Int, true, "AddRefIndex", "u"};
    a[21] = {1, drawfx::Ret::Int, true, "ReleaseIndex", "u"};
    return a;
}
constexpr auto kMaterialManagerSlots = MakeMaterialManagerSlots();
constexpr drawfx::Iface kMaterialManager{"MaterialManager", 22, kMaterialManagerSlots.data()};
constexpr uint32_t kMaterialCell = 0x10f8fcc;
drawfx::Fake gMaterials;

void NoteViewerBlock(const char *label)
{
    drawfx::Call c;
    c.note = label;
    for (uint32_t word : gViewerBlock) c.words.push_back(drawfx::Norm(word));
    drawfx::gLog.push_back(std::move(c));
}

} // namespace

DIFF_TEST(SG_NodeCopies_CompositionInPlace, 0xa36900) { InPlaceTest(ctx, kComposition); }
DIFF_TEST(SG_NodeCopies_TransformInPlace, 0xa3a000) { InPlaceTest(ctx, kTransform); }
DIFF_TEST(SG_NodeCopies_ShapeRefInPlace, 0xa541e0)
{
    sgrx::Services services;
    sgfx::RegistryScope shapeRef{kShapeRefRegistry};
    sgtp::RefreshScope refresh;
    InPlaceTest(ctx, kShapeRef);
}
DIFF_TEST(SG_NodeCopies_LightRefInPlace, 0xa60c00)
{
    svcfx::AllocatorScope allocator; // the settings key is a service string: the game's allocator service
    sgrx::Services services;
    sgfx::RegistryScope lightRef{kLightRefRegistry};
    sgtp::RefreshScope refresh;
    InPlaceTest(ctx, kLightRef);
}
DIFF_TEST(SG_NodeCopies_TransformConstruct_Factory, 0xa3a000) { TP::Factory(ctx); }
DIFF_TEST(SG_NodeCopies_TransformConstruct_Slots, 0xa3a000) { TP::Slots(ctx); }
DIFF_TEST(SG_NodeCopies_TransformConstruct_Events, 0xa3a000) { TP::Events(ctx); }
DIFF_TEST(SG_NodeCopies_LightRefImpl, 0xa60c20)
{
    svcfx::AllocatorScope allocator; // the key is a service string: the game's allocator service
    sgrx::Services services;
    using Construct = void *(__thiscall *)(void *);
    using Destruct = void(__thiscall *)(void *);
    sgfx::CheckSides(ctx, "light-ref impl", 32, &LightRefBuild, [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        uint32_t *const block = lw.impl.block;
        cLightRefNodeImpl *const ours = lw.impl.Impl<cLightRefNodeImpl>();
        void *const r = original ? ctx.Original<Construct>(0xa60c20)(block) : ours->Construct();
        drawfx::Note("returns the block", {r == block, lw.asked, uint32_t(lw.settings.refs)});
        drawfx::Note("inflate", {block[0x80 / 4]});
        lw.impl.NoteWorld();
        // What the node holds when it goes: the light and the second reference, each or neither (the Impl's counts).
        if (rng() % 2) {
            block[0x70 / 4] = drawfx::P(&lw.light);
            ++lw.light.refs;
        }
        if (rng() % 2) {
            block[0x74 / 4] = drawfx::P(&lw.held);
            ++lw.held.refs;
        }
        if (original)
            ctx.Original<Destruct>(0xa60ec0)(block);
        else
            ours->Destruct();
        drawfx::Note("held", {uint32_t(lw.light.refs), uint32_t(lw.held.refs), uint32_t(lw.settings.refs)});
        lw.impl.NoteWorld();
    });
}

// The viewer reference's Impl: the original's constructor and destructor at their RVAs against ours, on a block seeded
// with garbage; between them the entries' vector gets 0-4 entries, each with 0-5 material handles (allocated by the
// test, freed by the destructor: the blocks freed compared; each handle -1 or an index, the indices dropped through the
// material manager's slot 21, logged), the words compared before the entries and after the destructor.
DIFF_TEST(SG_NodeCopies_ViewerRefImpl, 0xa644c0)
{
    svx::Stands stands;
    using Construct = void *(__thiscall *)(void *);
    using Destruct = void(__thiscall *)(void *);
    sgfx::CheckSides(
        ctx, "viewer-ref impl", 32,
        [](uint32_t seed, drawfx::Cells &cells) {
            svx::Build(seed, cells);
            gMaterials.Reset<kMaterialManager>(490);
            cells.Set(kMaterialCell, static_cast<const void *>(&gMaterials));
        },
        [&](bool original, std::mt19937 &rng) {
            svx::SeedBase(rng);
            for (uint32_t &word : gViewerBlock) word = 0xcdcdcdcd;
            drawfx::AddRange(gViewerBlock, sizeof(gViewerBlock), 0x7c000000u);
            auto *const ours = reinterpret_cast<cViewerRefNodeImpl *>(gViewerBlock);
            void *const r = original ? ctx.Original<Construct>(0xa644c0)(gViewerBlock) : ours->Construct();
            drawfx::Note("returns the block", r == gViewerBlock);
            NoteViewerBlock("constructed");
            // (reserved first: the handles' copies would count references through the manager)
            const unsigned n = rng() % 5;
            ours->mEntries.reserve(n);
            for (unsigned i = 0; i < n; ++i) {
                cViewerRefEntry &e = ours->mEntries.emplace_back();
                const unsigned k = rng() % 6;
                e.mMaterials.reserve(k);
                for (unsigned j = 0; j < k; ++j)
                    e.mMaterials.emplace_back().mIndex = rng() % 3 == 0 ? -1 : int32_t(rng() % 4 ? rng() % 40 : rng());
            }
            const uint32_t aspectBits = uint32_t(rng());
            std::memcpy(&ours->mAspect, &aspectBits, sizeof aspectBits);
            ours->mTargetWidth = uint32_t(rng());
            ours->mTargetHeight = uint32_t(rng());
            difftest::FreeRecorder freed;
            if (original)
                ctx.Original<Destruct>(0xa64840)(gViewerBlock);
            else
                ours->Destruct();
            freed.Stop();
            size_t sizes[64];
            const size_t k = freed.SortedSizes(sizes, 64);
            drawfx::Call c;
            c.note = "freed";
            for (size_t i = 0; i < k; ++i) c.words.push_back(uint32_t(sizes[i]));
            drawfx::gLog.push_back(std::move(c));
            NoteViewerBlock("destroyed");
            svx::gW.NoteWorld();
            svx::NoteExtras();
            drawfx::Note("manager", {uint32_t(gMaterials.refs)});
        });
}
