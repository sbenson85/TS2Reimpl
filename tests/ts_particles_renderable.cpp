// Differential tests of cTSParticlesRenderable (lane WQ, rendering row E3; src/ts/cTSParticlesRenderable.h). Each side builds
// its own nodes in the same 0xcd-filled blocks: the game's constructor 0xbb5010, or our Construct (the hook's adapter). The
// blocks are filled so the words neither constructor writes (the Impl's +0x68, +0x9c, +0xa0, +0xc8) compare as well.
//
//   Particles_Renderable_Construct  0xbb5010 in place; then 0xbb54f0, the deleting 0xbb58b0 (with and without bit 0) or the
//                                   last Release, on nodes with every field filled: every call in its order
//   Particles_Renderable_Draw       71 0xbbca40, 72 0xbbcae0, 82 0xbb65a0, 83 0xbb58e0 (geometry, mesh, mesh list: the display
//                                   list's item slots made, grown and reused), 60, 43 / 44 / 51, 87-90
//   Particles_Renderable_Setup      0xbbad30 on nodes in any state, twice on some: the shape branch (its load, the parameters of the
//                                   shape's first material), the geometry made, the named material, the built one
//   Particles_Renderable_BuildMaterial  0xbc03d0 alone: every property of every draw mode, layer and override; the name it
//                                   formats and every call on the definition compare by text
//   Particles_Renderable_Pilot_*    the template levels' copies through the pilot (tests/sg_transform_pilot.h, level 4):
//                                   slots, events, persist and clone (the shared 15 makes a plain transform node)
//
// Setup's world: the framework in its cell 0x10c9378 and from the director getter (0x1ae18d stood in for), handing out the
// material definition (slot 3) and the services (slot 5); the material manager in 0x10f8fcc, the texture manager in 0x10f8f94;
// cRZString::Sprintf stood in for on both sides (the image's formatting reaches CRT state the harness has not), with the
// formats from the image, so the name compares.
#include "code_patch.h"
#include "difftest.h"
#include "game/vtable.h"
#include "sg_renderable_fx.h"
#include "sg_transform_pilot.h"
#include "sgfx.h"

#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"
#include "sg/cRenderableNodeImpl.h"
#include "sg/cTransformNode.h"
#include "sg/cViewerRefNodeBaseImpl.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"
#include "ts/cTSParticlesGeometry.h"
#include "ts/cTSParticlesRenderable.h"

#include <array>
#include <bit>
#include <cfloat>
#include <cstdarg>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <string>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

using Node = cTSParticlesRenderable;
constexpr uint32_t kCtor = 0xbb5010, kDtor = 0xbb54f0, kDeleting = 0xbb58b0, kSetup = 0xbbad30, kBuildMaterial = 0xbc03d0;
constexpr uint32_t kWords = Node::kSize / 4;
constexpr uint32_t kCompared = cTransformNode::kSize; // the words the pilot compares: all a clone has; NoteTail the rest
constexpr uint32_t kEventCell = 0x10fc820;
constexpr uint32_t kMaterialCell = 0x10f8fcc;
constexpr uint32_t kEffectTag = 0x7b000000, kDescriptionTag = 0x7b100000, kGeometryTag = 0x7b200000, kViewTag = 0x79000000,
                   kMadeTag = 0x7b300000;
constexpr uint32_t kShapeWord = 0x80 / 4, kMaterialWord = 0x84 / 4, kParamWord = 0x88 / 4, kGeometryWord = 0xa4 / 4,
                   kRunWord = 0xa8 / 4, kMeshWord = 0xac / 4, kVisBoundWord = 0xb0 / 4, kDrawSlotWord = 0xb4 / 4,
                   kMeshesWord = 0xb8 / 4, kVisColliderWord = 0xc4 / 4, kMeshSlotsWord = 0xc8 / 4;

template <unsigned N> constexpr std::array<Slot, N> BaseSlots()
{
    std::array<Slot, N> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    return a;
}

constexpr std::array<Slot, 29> MakeListSlots()
{
    std::array<Slot, 29> a = BaseSlots<29>();
    a[25] = {4, Ret::Int, true, "DrawNode", "aF13F6u"};
    a[28] = {8, Ret::Int, true, "AddGeometrySubsets", "auF1F1F1aau"};
    return a;
}
constexpr auto kListSlots = MakeListSlots();
constexpr drawfx::Iface kList{"DisplayList", 29, kListSlots.data()};

constexpr std::array<Slot, 44> MakeDetailSlots()
{
    std::array<Slot, 44> a = BaseSlots<44>();
    a[18] = {1, Ret::Int, true, "AllocSlots", "u"};
    a[19] = {3, Ret::Int, true, "ReallocSlots", "uuu"};
    a[20] = {2, Ret::Int, true, "FreeSlots", "uu"};
    a[43] = {0, Ret::Int, true, "Limit", ""};
    return a;
}
constexpr auto kDetailSlots = MakeDetailSlots();
constexpr drawfx::Iface kDetail{"Detail", 44, kDetailSlots.data()};

constexpr std::array<Slot, 34> MakeMaterialsSlots()
{
    std::array<Slot, 34> a = BaseSlots<34>();
    a[19] = {1, Ret::Int, true, "FindOrLoad", "s"};
    a[20] = {1, Ret::Int, true, "AddRefIndex", "u"};
    a[21] = {1, Ret::Int, true, "ReleaseIndex", "u"};
    a[23] = {2, Ret::Int, true, "AddMaterial", "au"};
    a[31] = {1, Ret::Int, true, "IsLoaded", "s"};
    a[33] = {2, Ret::Int, true, "GetInfo", "iu"};
    return a;
}
constexpr auto kMaterialsSlots = MakeMaterialsSlots();
constexpr drawfx::Iface kMaterials{"MaterialManager", 34, kMaterialsSlots.data()};

// Setup's fakes.
constexpr std::array<Slot, 6> MakeFrameworkSlots()
{
    std::array<Slot, 6> a = BaseSlots<6>();
    a[3] = {3, Ret::Int, true, "GetClassObject", "uui"};
    a[5] = {3, Ret::Int, true, "GetSystemService", "uui"};
    return a;
}
constexpr auto kFrameworkSlots = MakeFrameworkSlots();
constexpr drawfx::Iface kFramework{"Framework", 6, kFrameworkSlots.data()};

constexpr std::array<Slot, 4> MakeResourcesSlots()
{
    std::array<Slot, 4> a = BaseSlots<4>();
    a[3] = {5, Ret::Int, true, "Load", "W4uauu"};
    return a;
}
constexpr auto kResourcesSlots = MakeResourcesSlots();
constexpr drawfx::Iface kResources{"Resources", 4, kResourcesSlots.data()};

constexpr std::array<Slot, 55> MakeShapeSlots()
{
    std::array<Slot, 55> a = BaseSlots<55>();
    a[54] = {2, Ret::Int, true, "MaterialRange", "iu"};
    return a;
}
constexpr auto kShapeSlots = MakeShapeSlots();
constexpr drawfx::Iface kShape{"Shape", 55, kShapeSlots.data()};

constexpr std::array<Slot, 6> MakeFormatsSlots()
{
    std::array<Slot, 6> a = BaseSlots<6>();
    a[3] = {0, Ret::Int, true, "BeginFormat", ""};
    a[4] = {4, Ret::Int, true, "AddElement", "uuub"};
    a[5] = {1, Ret::Int, true, "EndFormat", "W0"}; // (a word of the new block; NoteMade compares what it gets)
    return a;
}
constexpr auto kFormatsSlots = MakeFormatsSlots();
constexpr drawfx::Iface kFormats{"VertexFormatsManager", 6, kFormatsSlots.data()};

constexpr std::array<Slot, 6> MakeIteratorSlots()
{
    std::array<Slot, 6> a = BaseSlots<6>();
    a[4] = {1, Ret::Int, true, "Equals", "a"};
    a[5] = {1, Ret::Int, true, "Element", "i"};
    return a;
}
constexpr auto kIteratorSlots = MakeIteratorSlots();
constexpr drawfx::Iface kIterator{"Iterator", 6, kIteratorSlots.data()};

constexpr std::array<Slot, 13> MakeParsedSlots()
{
    std::array<Slot, 13> a = BaseSlots<13>();
    a[6] = {0, Ret::Int, true, "IsEmpty", ""};
    a[12] = {3, Ret::Int, true, "Find", "isu"};
    return a;
}
constexpr auto kParsedSlots = MakeParsedSlots();
constexpr drawfx::Iface kParsed{"Parsed", 13, kParsedSlots.data()};

constexpr std::array<Slot, 16> MakeDefinitionSlots()
{
    std::array<Slot, 16> a = BaseSlots<16>();
    a[10] = {1, Ret::Int, true, "SetMaterialName", "s"};
    a[12] = {1, Ret::Int, true, "SetMaterialType", "s"};
    a[13] = {2, Ret::Int, true, "SetProperty", "ss"};
    a[15] = {1, Ret::Int, true, "AddTextureById", "u"};
    return a;
}
constexpr auto kDefinitionSlots = MakeDefinitionSlots();
constexpr drawfx::Iface kDefinition{"MaterialDefinition", 16, kDefinitionSlots.data()};

constexpr std::array<Slot, 26> MakeTexturesSlots()
{
    std::array<Slot, 26> a = BaseSlots<26>();
    a[17] = {1, Ret::Int, true, "FindId", "s"};
    a[18] = {1, Ret::Int, true, "AddRefId", "u"};
    a[19] = {1, Ret::Int, true, "ReleaseId", "u"};
    a[25] = {1, Ret::Int, true, "Has", "s"};
    return a;
}
constexpr auto kTexturesSlots = MakeTexturesSlots();
constexpr drawfx::Iface kTextures{"TextureManager", 26, kTexturesSlots.data()};

// A mesh: slot 30 its geometry.
constexpr std::array<Slot, 31> MakeMeshSlots()
{
    std::array<Slot, 31> a = BaseSlots<31>();
    a[30] = {0, Ret::Int, true, "GetGeometry", ""};
    return a;
}
constexpr auto kMeshSlots = MakeMeshSlots();
constexpr drawfx::Iface kMesh{"Mesh", 31, kMeshSlots.data()};

// A geometry: slot 4 its info, the first word the subset count.
constexpr std::array<Slot, 5> MakeGeometrySlots()
{
    std::array<Slot, 5> a = BaseSlots<5>();
    a[4] = {0, Ret::Int, true, "GetInfo", ""};
    return a;
}
constexpr auto kGeometrySlots = MakeGeometrySlots();
constexpr drawfx::Iface kGeometry{"Geometry", 5, kGeometrySlots.data()};

struct World {
    Fake list, materials, shape;
    Fake dynamicGeometry;  // embedded in gGeometryBlock (SetBounds writes the object's own words)
    Fake meshes[4], meshGeometries[4];
    unsigned nextSlot = 0;
};
World w;
alignas(16) cViewerView gView;
alignas(16) uint8_t gEffectBytes[sizeof(cTSParticlesEffect)]; // fields set by hand; never constructed
cTSParticlesEffect &gEffect = reinterpret_cast<cTSParticlesEffect &>(gEffectBytes);
alignas(16) uint8_t gDescription[0x300];
alignas(16) uint32_t gGeometryBlock[0x78 / 4];
difftest::Context *gCtx;
uint32_t gNodeVtable; // this side's particles node's vtable word (a clone, and what a freed node's block becomes, have others)

// The original's words at the description's flags.
uint64_t &DescriptionFlags() { return *reinterpret_cast<uint64_t *>(gDescription + offsetof(cTSParticlesDescription, mFlags)); }

uint32_t *Words(cICompositionTreeNode *n) { return reinterpret_cast<uint32_t *>(n); }
uint32_t CountOf(const cICompositionTreeNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
cITSParticlesRenderable *As(cICompositionTreeNode *n)
{
    return static_cast<cITSParticlesRenderable *>(static_cast<cIRenderableNode *>(n));
}

void *Filled()
{
    void *const memory = ::operator new(Node::kSize);
    std::memset(memory, 0xcd, Node::kSize);
    return memory;
}
void *MakeOriginalAt(void *memory)
{
    void *const made = gCtx->Original<void *(__thiscall *)(void *, cTSParticlesEffect *)>(kCtor)(memory, &gEffect);
    gNodeVtable = *static_cast<uint32_t *>(memory);
    return made;
}
void *MakeOurAt(void *memory)
{
    void *const made = cTSParticlesRenderableAdapters::Construct(static_cast<Node *>(memory), nullptr, &gEffect);
    gNodeVtable = *static_cast<uint32_t *>(memory);
    return made;
}
cICompositionTreeNode *__cdecl MakeOriginal() { return reinterpret_cast<cICompositionTreeNode *>(MakeOriginalAt(Filled())); }
cICompositionTreeNode *__cdecl MakeOurs() { return reinterpret_cast<cICompositionTreeNode *>(MakeOurAt(Filled())); }

// ---- the fakes' handlers ----

uint32_t ListSlots(Fake &, const uint32_t *) { return 0x40 + w.nextSlot++ * 0x10; }
uint32_t GeometryInfo(Fake &self, const uint32_t *) { return P(&self.value[0]); }
uint32_t MeshGeometry(Fake &self, const uint32_t *) { return self.value[0]; }

float RandomBoundWord(std::mt19937 &rng)
{
    switch (rng() % 12) {
    case 0: return -0.0f;
    case 1: return 0.0f;
    case 2: return std::bit_cast<float>(0x7f800001u); // signalling: the read's addition quiets it
    case 3: return std::bit_cast<float>(0xffc12345u);
    case 4: return std::numeric_limits<float>::infinity();
    case 5: return std::bit_cast<float>(0x00000001u);
    default: return sgtx::RandomFloat(rng);
    }
}

void RandomEffect(std::mt19937 &rng)
{
    const int32_t counts[] = {0, 1, 2, 77, -1, INT32_MIN};
    gEffect.mCount = counts[rng() % 6];
    for (float &f : gEffect.mBound) f = RandomBoundWord(rng);
    if (rng() % 3 == 0) { // a box the frustum can sort
        for (unsigned k = 0; k < 3; ++k) {
            gEffect.mBound[k] = sgtx::RandomFloat(rng);
            gEffect.mBound[k + 3] = gEffect.mBound[k] + float(rng() % 5);
        }
    }
    DescriptionFlags() = rng() % 2 ? 0x800000ull : 0ull;
    if (rng() % 2) DescriptionFlags() |= uint64_t(rng()) << 32 | (rng() & ~0x800000u);
}

// The view: its frame and six planes, each an axis (either way) at a distance.
void RandomView(std::mt19937 &rng)
{
    const cTransform34 frame = sgtx::RandomFrame(rng);
    std::memcpy(&gView.mFrame, &frame, sizeof(frame));
    for (auto &plane : gView.mFrustum.plane) {
        for (float &c : plane) c = 0.0f;
        plane[rng() % 3] = rng() % 2 ? 1.0f : -1.0f;
        plane[3] = float(int(rng() % 13) - 4);
    }
}

// Fills what only Setup and code the game does not have fill: the references are counted, as a holder would.
void Plant(cICompositionTreeNode *n, std::mt19937 &rng)
{
    uint32_t *const words = Words(n);
    if (rng() % 2) {
        ++w.shape.refs;
        words[kShapeWord] = P(&w.shape);
    }
    if (rng() % 2) words[kMaterialWord] = rng() % 50;
    for (unsigned i = 0; i < 7; ++i)
        if (rng() % 2) words[kParamWord + i] = uint32_t(rng());
    if (rng() % 3) {
        ++w.dynamicGeometry.refs;
        words[kGeometryWord] = P(gGeometryBlock);
    }
    if (rng() % 2) words[kRunWord] = rng() % 300;
    if (rng() % 2) {
        Fake &mesh = w.meshes[rng() % 4];
        ++mesh.refs;
        words[kMeshWord] = P(&mesh);
    }
    if (rng() % 2) words[kVisBoundWord] = rng() % 50;
    if (rng() % 2) words[kDrawSlotWord] = rng() % 300;
    if (rng() % 2) {
        const unsigned count = rng() % 5, capacity = count + rng() % 2;
        if (capacity) {
            void **const block = static_cast<void **>(::operator new(capacity * 4));
            for (unsigned i = 0; i < count; ++i) {
                Fake &mesh = w.meshes[rng() % 4];
                ++mesh.refs;
                block[i] = &mesh;
            }
            words[kMeshesWord] = P(block);
            words[kMeshesWord + 1] = P(block + count);
            words[kMeshesWord + 2] = P(block + capacity);
        }
    }
    if (rng() % 2) words[kVisColliderWord] = rng() % 50;
    if (rng() % 2) {
        words[kMeshSlotsWord] = rng() % 300;
        words[kMeshSlotsWord + 1] = rng() % 12;
    }
}

void NoteTail(const char *label, cICompositionTreeNode *n)
{
    drawfx::Call c;
    c.note = label;
    const uint32_t *const words = Words(n);
    for (uint32_t k = kCompared / 4; k < kWords; ++k) {
        // The mesh list's block is each side's own: its shape and its elements are what compare.
        if (k < kMeshesWord || k > kMeshesWord + 2) c.words.push_back(drawfx::Norm(words[k]));
        else if (words[kMeshesWord]) c.words.push_back(words[k] - words[kMeshesWord]);
        else c.words.push_back(words[k]);
        if (c.words.size() == 8 || k + 1 == kWords) { // (a failure prints this much of a note)
            drawfx::gLog.push_back(c);
            c.words.clear();
        }
    }
    if (words[kMeshesWord]) {
        c.note = "meshes";
        for (const uint32_t *at = reinterpret_cast<const uint32_t *>(uintptr_t(words[kMeshesWord]));
             P(at) != words[kMeshesWord + 1]; ++at)
            c.words.push_back(drawfx::Norm(*at));
        drawfx::gLog.push_back(std::move(c));
    }
}

void NoteBlocks()
{
    drawfx::Call c;
    c.note = "geometry words";
    for (uint32_t k = 0; k < 0x78 / 4; ++k)
        if (k) c.words.push_back(gGeometryBlock[k]);
    drawfx::gLog.push_back(std::move(c));
    drawfx::Note("refs", {uint32_t(w.shape.refs), uint32_t(w.dynamicGeometry.refs), uint32_t(w.meshes[0].refs),
                          uint32_t(w.meshes[1].refs), uint32_t(w.meshes[2].refs), uint32_t(w.meshes[3].refs)});
}

// The world of every test (the pilot's Build calls it per seed).
void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed ^ 0x5eedu);
    sgrx::Setup(rng);
    Fake &detail = sgrx::Registry::Get().detail;
    detail.Reset<kDetail>(702);
    detail.Returns(43, uint32_t(rng() % 1000));
    detail.On(18, &ListSlots);
    detail.On(19, &ListSlots);
    w.nextSlot = 0;
    w.materials.Reset<kMaterials>(707);
    cells.Set(kMaterialCell, static_cast<const void *>(&w.materials));
    w.list.Reset<kList>(703);
    w.shape.Reset<sgfx::kUnknown>(704);
    std::memset(gGeometryBlock, 0xee, sizeof(gGeometryBlock));
    w.dynamicGeometry.Reset<kGeometry>(705);
    w.dynamicGeometry.Embed(gGeometryBlock);
    for (unsigned i = 0; i < 4; ++i) {
        Fake &geometry = w.meshGeometries[i];
        geometry.Reset<kGeometry>(730 + i);
        geometry.On(4, &GeometryInfo);
        geometry.value[0] = rng() % 6;
        Fake &mesh = w.meshes[i];
        mesh.Reset<kMesh>(720 + i);
        mesh.On(30, &MeshGeometry);
        mesh.value[0] = P(&geometry);
    }
    std::memset(&gEffect, 0, sizeof(gEffect));
    std::memset(gDescription, 0xab, sizeof(gDescription));
    gEffect.mDescription = reinterpret_cast<cTSParticlesDescription *>(gDescription);
    RandomEffect(rng);
    std::memset(&gView, 0, sizeof(gView));
    gView.mDrawer = &w.list;
    RandomView(rng);
    drawfx::AddRange(&gEffect, sizeof(gEffect), kEffectTag);
    drawfx::AddRange(gDescription, sizeof(gDescription), kDescriptionTag);
    drawfx::AddRange(gGeometryBlock, sizeof(gGeometryBlock), kGeometryTag);
    drawfx::AddRange(&gView, sizeof(gView), kViewTag);
}

struct Scope {
    sgrx::Services services;
};

// ---- the pilot (level 4: a bounded node of another family; see sg_transform_pilot.h) ----

struct Cfg {
    using Interface = cITSParticlesRenderable;
    static constexpr uint32_t kSize = kCompared; // (a clone is a plain transform node: the rest is noted by NoteNode)
    static constexpr int kLevel = 4;
    static constexpr uint32_t kIid = cRenderableNodeImpl::kIid;
    static constexpr uint32_t kEventCell = ::kEventCell;
    using Scope = ::Scope;
    static sgfx::Factory OriginalFactory(difftest::Context &ctx)
    {
        gCtx = &ctx;
        return &MakeOriginal;
    }
    static cICompositionTreeNode *Create() { return MakeOurs(); }
    static void SetupCells(uint32_t seed, drawfx::Cells &cells);
    static void Prepare(cICompositionTreeNode *n, std::mt19937 &rng) { Plant(n, rng); }
    // A leaf, as the game's particle nodes are: a copy is a plain transform node that keeps the source's "bounded" flag, and
    // adding such a copy to a cloned parent reads slot 69 past the end of its vtable (see Clone in the source).
    static cICompositionTreeNode *CloneSource(cICompositionTreeNode *n)
    {
        while (cICompositionTreeNode *const child = n->GetFirstChild()) n = child;
        return n;
    }
    static void NoteNode(const char *label, cICompositionTreeNode *n)
    {
        if (Words(n)[0] == gNodeVtable) NoteTail(label, n); // (a clone is a plain transform node)
    }
};
using Pilot = sgtp::Pilot<Cfg>;

void Cfg::SetupCells(uint32_t seed, drawfx::Cells &cells)
{
    BuildWorld(seed, cells);
    cells.Set(cTransformNodeBehaviorTraits::kEventManagerCell, Pilot::w.manager.manager);
}

// ---- Setup's world ----

struct SetupWorld {
    Fake framework, resources, definition, textures, parsed, iterators[3], formats, format;
    Fake shape; // what a load hands out
    bool loads, definitionUp;
    uint32_t atEnd, nameHash, hasParsed, parsedEmpty, salt, nextAdded;
    int32_t elementMaterial;
    uint32_t madeGeometry; // the block Setup made, once it is in the ranges
};
SetupWorld s;

uint32_t TextHash(const char *text)
{
    uint32_t h = s.salt;
    for (; *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h ^ h >> 15;
}

const char *TextArg(uint32_t word) { return reinterpret_cast<const char *>(uintptr_t(word)); }
uint32_t *OutArg(uint32_t word) { return reinterpret_cast<uint32_t *>(uintptr_t(word)); }

uint32_t SystemService(Fake &, const uint32_t *a)
{
    Fake *const service = a[0] == 0x56b906e && a[1] == 0x656b8efc ? &s.resources : a[0] == 0xe5074b6b ? &w.materials : nullptr;
    if (!service) {
        drawfx::Note("unknown service", {a[0], a[1]});
        return 0;
    }
    ++service->refs;
    *OutArg(a[2]) = P(service);
    return 1;
}

uint32_t ClassObject(Fake &, const uint32_t *a)
{
    ++s.definition.refs;
    *OutArg(a[2]) = P(&s.definition);
    return 1;
}

uint32_t ResourceLoad(Fake &, const uint32_t *a)
{
    if (!s.loads) return 0;
    ++s.shape.refs;
    *OutArg(a[2]) = P(&s.shape);
    return 1;
}

uint32_t MaterialRange(Fake &, const uint32_t *a)
{
    uint32_t *const out = OutArg(a[0]);
    for (unsigned i = 0; i < 3; ++i) {
        ++s.iterators[i].refs;
        out[i] = P(&s.iterators[i]);
    }
    return a[0];
}

uint32_t IteratorEquals(Fake &, const uint32_t *) { return s.atEnd; }

uint32_t IteratorElement(Fake &, const uint32_t *a)
{
    uint32_t *const out = OutArg(a[0]);
    out[0] = 0x51000000u;
    out[1] = uint32_t(s.elementMaterial);
    out[2] = 0x52000000u;
    out[3] = 0x53000000u;
    return a[0];
}

uint32_t Info(Fake &, const uint32_t *a)
{
    uint32_t *const out = OutArg(a[0]);
    out[0] = s.nameHash;
    out[1] = 0x54000000u;
    out[2] = s.hasParsed ? P(&s.parsed) : 0;
    out[3] = 0x55000000u;
    out[4] = 0x56000000u;
    return a[0];
}

// An answer for each name: found most times, of the size its type has most times.
uint32_t ParsedFind(Fake &, const uint32_t *a)
{
    const uint32_t h = TextHash(TextArg(a[1]));
    const uint32_t size = a[2] == 0x7b5b330cu ? 0xc : a[2] == 0x5b5b331au ? 0x10 : a[2] == 0xbb22dd17u ? 4 : 0;
    const uint32_t sizes[] = {size, size, size, 4, 0xc, 0x10, 0, size + 1};
    uint32_t *const out = OutArg(a[0]);
    out[0] = h & 7 ? h >> 3 : 0;
    out[1] = h * 3;
    out[2] = sizes[h >> 8 & 7];
    out[3] = 0x57000000u;
    return a[0];
}

// The named material, or none.
uint32_t NamedMaterial(Fake &, const uint32_t *a)
{
    const uint32_t h = TextHash(TextArg(a[0]));
    return h % 8 ? h % 50 : 0xffffffffu;
}
uint32_t EndFormat(Fake &, const uint32_t *a)
{
    ++s.format.refs;
    *OutArg(a[0]) = P(&s.format);
    return 0;
}
uint32_t AddedMaterial(Fake &, const uint32_t *) { return 100 + s.nextAdded++; }
uint32_t TextureHas(Fake &, const uint32_t *a) { return TextHash(TextArg(a[0])) & 1; }
uint32_t TextureId(Fake &, const uint32_t *a) { return TextHash(TextArg(a[0])) & 0xffff; }

void *StandDirector() { return &s.framework; }

cRZString *__cdecl StandSprintf(cRZString *self, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    // The real Sprintf calls through the string's vtable: a string nobody constructed must not pass here.
    const bool constructed = self->mVTable == cRZString::VTable() || self->mVTable == GameAddress<const void>(cRZString::kVTable);
    self->mString = constructed ? text : "unconstructed cRZString";
    return self;
}

void BuildSetupWorld(uint32_t seed, drawfx::Cells &cells)
{
    Pilot::Build(seed, cells);
    std::mt19937 rng(seed ^ 0x5e70u);
    s.framework.Reset<kFramework>(740);
    s.framework.On(3, &ClassObject);
    s.framework.On(5, &SystemService);
    cells.Set(0x10c9378, static_cast<const void *>(&s.framework));
    s.resources.Reset<kResources>(741);
    s.resources.On(3, &ResourceLoad);
    s.definition.Reset<kDefinition>(742);
    s.textures.Reset<kTextures>(743);
    s.textures.On(17, &TextureId);
    s.textures.On(25, &TextureHas);
    cells.Set(0x10f8f94, static_cast<const void *>(&s.textures));
    s.parsed.Reset<kParsed>(744);
    s.parsed.On(12, &ParsedFind);
    s.shape.Reset<kShape>(745);
    s.shape.On(54, &MaterialRange);
    for (unsigned i = 0; i < 3; ++i) {
        s.iterators[i].Reset<kIterator>(746 + i);
        s.iterators[i].On(4, &IteratorEquals);
        s.iterators[i].On(5, &IteratorElement);
    }
    s.format.Reset<sgfx::kUnknown>(750);
    s.formats.Reset<kFormats>(751);
    s.formats.On(5, &EndFormat);
    cells.Set(0x10caa24, static_cast<const void *>(&s.formats)); // for the geometry Setup makes
    w.materials.On(19, &NamedMaterial);
    w.materials.On(23, &AddedMaterial);
    w.materials.On(33, &Info);
    s.loads = rng() % 4 != 0;
    s.definitionUp = true;
    s.atEnd = rng() % 5 == 0;
    s.nameHash = rng() % 4 ? uint32_t(rng()) | 1 : 0;
    s.hasParsed = rng() % 4 != 0;
    s.parsed.Returns(6, rng() % 4 == 0 ? 1u : 0u);
    s.salt = uint32_t(rng());
    s.nextAdded = 0;
    s.elementMaterial = int32_t(rng() % 50);
    s.madeGeometry = 0;
    w.materials.Returns(31, rng() % 3 == 0 ? 1u : 0u);
}

const char *RandomName(std::mt19937 &rng)
{
    static const char *const kNames[] = {"smoke",
                                         "a",
                                         "fire_01",
                                         "exactly15chars_",
                                         "exactly16chars__",
                                         "a_texture_name_of_more_than_fifteen_characters",
                                         "#12!numbered",
                                         "##0x7f001234!grouped",
                                         "!leading",
                                         "UPPER_case"};
    return kNames[rng() % 10];
}

float RandomLayer(std::mt19937 &rng)
{
    switch (rng() % 16) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return -1.0f;
    case 4: return 0.5f;
    case 5: return 1.5f;
    case 6: return -2.5f;
    case 7: return 2.5f;
    case 8: return 8388608.0f;
    case 9: return 1.0e9f;
    case 10: return std::numeric_limits<float>::infinity();
    case 11: return std::bit_cast<float>(0xffc12345u);
    case 12: return std::bit_cast<float>(0x7f800001u);
    case 13: return float(int(rng() % 41) - 20);
    default: return sgtx::RandomFloat(rng);
    }
}

// The description's texts and numbers; ResetDescription ends the strings. `flags`' bits: the seed's mix of the four.
void RandomDescription(std::mt19937 &rng)
{
    std::string *const material = new (gDescription + offsetof(cTSParticlesDescription, mMaterial)) std::string;
    std::string *const texture = new (&gEffect.mTextureOverride.mString.mString) std::string;
    if (rng() % 8) *material = RandomName(rng);
    if (rng() % 3) *texture = RandomName(rng);
    uint64_t flags = uint64_t(rng()) << 32 | uint32_t(rng());
    flags &= ~uint64_t(0xc00000u);
    if (rng() % 4 == 0) flags |= 0x400000u;
    if (rng() % 4 == 0) flags |= 0x200000u;
    DescriptionFlags() = flags;
    gDescription[offsetof(cTSParticlesDescription, mDraw)] = uint8_t(rng() % 8 ? rng() % 9 : rng());
    *reinterpret_cast<float *>(gDescription + offsetof(cTSParticlesDescription, mLayer)) = RandomLayer(rng);
}

void EndDescription()
{
    using String = std::string;
    std::launder(reinterpret_cast<String *>(gDescription + offsetof(cTSParticlesDescription, mMaterial)))->~String();
    std::launder(reinterpret_cast<String *>(&gEffect.mTextureOverride.mString.mString))->~String();
}

// The geometry Setup made: into the ranges, once, and its words (not its vtable) compared.
void NoteMade(cICompositionTreeNode *n)
{
    const uint32_t made = Words(n)[kGeometryWord];
    if (!made || made == P(gGeometryBlock)) return;
    if (s.madeGeometry != made) {
        s.madeGeometry = made;
        drawfx::AddRange(reinterpret_cast<const void *>(uintptr_t(made)), 0x78, kMadeTag);
    }
    drawfx::Call c;
    c.note = "made geometry";
    const uint32_t *const words = reinterpret_cast<const uint32_t *>(uintptr_t(made));
    for (uint32_t k = 1; k < 0x78 / 4; ++k) {
        if (k == 0x3c / 4) continue; // left unwritten by the constructor, as +0x69..0x6b
        c.words.push_back(k == 0x68 / 4 ? words[k] & 0xff : drawfx::Norm(words[k]));
    }
    drawfx::gLog.push_back(std::move(c));
}

void NoteSetupRefs()
{
    drawfx::Note("setup refs", {uint32_t(s.framework.refs), uint32_t(s.resources.refs), uint32_t(s.definition.refs),
                                uint32_t(s.textures.refs), uint32_t(s.parsed.refs), uint32_t(s.shape.refs),
                                uint32_t(s.iterators[0].refs), uint32_t(s.iterators[1].refs), uint32_t(s.iterators[2].refs),
                                uint32_t(w.materials.refs), uint32_t(s.format.refs)});
}

// A node, planted, for `act`, then everything it has compared and the last reference gone.
template <class Act> void WithNode(bool original, std::mt19937 &rng, Act &&act)
{
    void *const memory = Filled();
    auto *const n = reinterpret_cast<cICompositionTreeNode *>(original ? MakeOriginalAt(memory) : MakeOurAt(memory));
    n->AddRef();
    drawfx::AddRange(n, Node::kSize, sgfx::kNodeTag);
    Plant(n, rng);
    if (rng() % 2 && !Words(n)[kShapeWord]) { // (a shape Setup has to let go of)
        ++w.shape.refs;
        Words(n)[kShapeWord] = P(&w.shape);
    }
    RandomDescription(rng);
    act(n);
    NoteMade(n);
    NoteTail("after", n);
    NoteSetupRefs();
    NoteBlocks();
    n->Release();
    NoteSetupRefs();
    NoteBlocks();
    EndDescription();
}

void CallSetup(bool original, cICompositionTreeNode *n)
{
    auto *const description = reinterpret_cast<cTSParticlesDescription *>(gDescription);
    if (original) gCtx->Original<void(__thiscall *)(void *, void *)>(kSetup)(n, description);
    else cTSParticlesRenderableAdapters::Setup(reinterpret_cast<Node *>(n), nullptr, description);
}

void CallBuildMaterial(bool original, cICompositionTreeNode *n)
{
    auto *const description = reinterpret_cast<cTSParticlesDescription *>(gDescription);
    if (original) gCtx->Original<void(__thiscall *)(void *, void *)>(kBuildMaterial)(n, description);
    else cTSParticlesRenderableAdapters::BuildMaterial(reinterpret_cast<Node *>(n), nullptr, description);
}

void Mark(difftest::Context &ctx, int k, unsigned op)
{
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
}

void NoteBox(const char *label, const cBoundBox &b) { sgtx::NoteFloats(label, b.mMin, 6); }

void OperateDraw(difftest::Context &ctx, int k, std::mt19937 &rng)
{
    sgfx::Nodes &ns = Pilot::w.nodes;
    cICompositionTreeNode *const n = ns[rng() % ns.count];
    cITSParticlesRenderable *const s = As(n);
    const unsigned op = rng() % 12;
    Mark(ctx, k, op);
    switch (op) {
    case 0: // 72
        NoteBox("box", s->GetBoundBox());
        break;
    case 1: { // 71
        const cTransform34 frame = sgtx::RandomFrame(rng);
        NoteBox("box in", s->GetBoundBoxIn(&frame));
        break;
    }
    case 2:
    case 3: // 82
        s->Draw(&gView, rng() % 2 ? 0x3fu : uint32_t(rng()));
        break;
    case 4:
    case 5:
    case 6: // 83
        s->Slot83(&gView, reinterpret_cast<void *>(uintptr_t(rng())));
        NoteBlocks();
        break;
    case 7:
        RandomView(rng);
        break;
    case 8:
        RandomEffect(rng);
        break;
    case 9: { // 87-90
        if (rng() % 2) s->SetWord6c(rng() % 2 ? 0 : uint32_t(rng()));
        drawfx::Note("words", {s->GetRenderableKind(), s->GetDetailLimit(), s->GetWord6c()});
        break;
    }
    case 10: { // 60
        const cRigidTransform t = sgtx::RandomRigid(rng); // (slot 60 here is the Impl's SetWorldTransform 0xae04b0)
        n->OnContentChanged(P(&t), 0, 0);
        const cRigidTransform world = n->GetWorldTransform();
        sgtx::NoteFloats("world", world.mValues, 7);
        break;
    }
    case 11: { // a mesh's subsets changed (the item slots grow only)
        w.meshGeometries[rng() % 4].value[0] = rng() % 6;
        break;
    }
    }
}

void NoteAll(const char *label)
{
    Pilot::NoteNodes(label);
}

} // namespace

DIFF_TEST(Particles_Renderable_Construct, kCtor)
{
    gCtx = &ctx;
    Pilot::Registries registries;
    sgfx::CheckSides(ctx, "construct", 96, Pilot::Build, [&](bool original, std::mt19937 &rng) {
        void *const memory = Filled();
        void *const made = original ? MakeOriginalAt(memory) : MakeOurAt(memory);
        drawfx::Note("in place", uint8_t(made == memory));
        auto *const n = reinterpret_cast<cICompositionTreeNode *>(made);
        drawfx::Note("count", CountOf(n));
        n->AddRef();
        sgfx::Nodes &ns = Pilot::w.nodes;
        ns.at[0] = n;
        ns.count = 1;
        ns.size = kCompared;
        drawfx::AddRange(n, Node::kSize, sgfx::kNodeTag);
        ns.NoteWords("new node", 0b101);
        ns.NoteShape("new node");
        NoteTail("new node", n);
        Plant(n, rng);
        ns.count = 0;
        const unsigned end = rng() % 4;
        drawfx::Note("end", end);
        {
            difftest::FreeRecorder freed;
            switch (end) {
            case 0:
                n->Release();
                break;
            case 1:
                if (original) gCtx->Original<void(__thiscall *)(void *)>(kDtor)(n);
                else cTSParticlesRenderableAdapters::Destruct(reinterpret_cast<Node *>(n), nullptr);
                ::operator delete(n);
                break;
            default: {
                const uint32_t flags = end == 2 ? 1 : 0;
                void *const back = original ? gCtx->Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(n, flags)
                                            : VirtualCall<void *(__thiscall *)(void *, uint32_t), 91>(n, flags);
                drawfx::Note("returns this", uint8_t(back == n));
                if (!flags) ::operator delete(n);
                break;
            }
            }
            freed.Stop();
            size_t sizes[8];
            const size_t count = freed.SortedSizes(sizes, 8);
            drawfx::Call c;
            c.note = "freed";
            for (size_t i = 0; i < count; ++i) c.words.push_back(uint32_t(sizes[i]));
            drawfx::gLog.push_back(std::move(c));
        }
        NoteBlocks();
    });
    Pilot::w.manager.Drop();
}

DIFF_TEST(Particles_Renderable_Draw, 0xbb58e0)
{
    gCtx = &ctx;
    Pilot::Registries registries;
    sgfx::CheckSides(ctx, "draw", 192, Pilot::Build, [&](bool original, std::mt19937 &rng) {
        Pilot::MakeTree(Pilot::Maker(ctx, original), rng);
        NoteAll("built");
        for (int k = 0; k < 40; ++k) OperateDraw(ctx, k, rng);
        NoteAll("after");
        Pilot::ReleaseAll();
        NoteBlocks();
    });
    Pilot::w.manager.Drop();
}

DIFF_TEST(Particles_Renderable_Setup, kSetup)
{
    gCtx = &ctx;
    Pilot::Registries registries;
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &StandDirector};
    shape::StandIn sprintfStand{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
    sgfx::CheckSides(ctx, "setup", 4096, BuildSetupWorld, [&](bool original, std::mt19937 &rng) {
        WithNode(original, rng, [&](cICompositionTreeNode *n) {
            CallSetup(original, n);
            if (rng() % 4 == 0) {
                NoteMade(n);
                CallSetup(original, n); // the geometry stays, the shape goes
            }
        });
    });
    Pilot::w.manager.Drop();
}

DIFF_TEST(Particles_Renderable_BuildMaterial, kBuildMaterial)
{
    gCtx = &ctx;
    Pilot::Registries registries;
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &StandDirector};
    shape::StandIn sprintfStand{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
    sgfx::CheckSides(ctx, "build material", 4096, BuildSetupWorld, [&](bool original, std::mt19937 &rng) {
        WithNode(original, rng, [&](cICompositionTreeNode *n) { CallBuildMaterial(original, n); });
    });
    Pilot::w.manager.Drop();
}

DIFF_TEST(Particles_Renderable_Pilot_Factory, kCtor) { Pilot::Factory(ctx); }
DIFF_TEST(Particles_Renderable_Pilot_Slots, 0xbbce60) { Pilot::Slots(ctx); }
DIFF_TEST(Particles_Renderable_Pilot_Events, 0xbbd020) { Pilot::Events(ctx); }
DIFF_TEST(Particles_Renderable_Pilot_Persist, 0xbb9270) { Pilot::Persist(ctx); }
DIFF_TEST(Particles_Renderable_Pilot_Clone, 0xa3a3b0) { Pilot::Clone(ctx); }
