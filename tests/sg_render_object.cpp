// Milestone O-b (docs/subsystems/scenegraph.md, "Milestone O"): one object's geometry through the render path, the original
// code against ours, compared by the Direct3D-side call stream at the devfx device fakes (sg_geometry_node_device_fx.h).
//
//   SG_Render_Object   three real objects (gmdcfx: GMDC resources read from the game copy's packages, read only; SKIP
//                      without the copy) x five device setups. Per side: a geometry node (the side's own: the original's
//                      factory 0xa42590, or ours) keyed to the object's GMDC; Rebuild 0xa3ebe0 loads it through the resource
//                      manager (the fake's load decodes the real resource with the side's container: 0xa70aa0 or ours) and
//                      builds it (0xa3f180) with the real vertex formats manager (ours, both sides); then the render face
//                      draws every element: IsSupported 0xa41ee0 (device buffers, the vertex copy 0xc627e0, declarations
//                      and state blocks through ResetDeviceState 0xa419e0, the device format lookup 0x18daa0), then Render
//                      0xa435d0 with state blocks and without (hardware), Prepare 0xa429b0 then Render (software, with the
//                      caller's declaration), a lost device and IsSupported again.
//
// Compared: every call on the fakes in order (the device's ProcessVertices and DrawIndexedPrimitive with their arguments,
// the state's ApplyStateBlock / SetVertexDeclaration / SetStreamSource / SetIndices / CreateStateBlock / Reset, the
// declarations manager's GetDeclaration, the device enum's limits and caps, the dynamic arrays' Lock / Reserve / Unlock,
// the device buffers' Init / Lock / Unlock), the node's members after the build, the device buffers' bytes (vertex and
// index data, hashed over the size each buffer was made with), the blocks freed and every fake's references.
//
// Not reached yet (the geometry slice of the object's path): materials (TXMT), textures, shaders and their constants,
// cShape, the resource node (CRES) and the viewer's display list. So there are no texture or shader calls in the stream
// yet; the next steps are in the doc.
//
// The gauge (reach_census.h): each side's run is armed with a census of the image's function starts, in two phases (load:
// the Rebuild; draw: the render face). Per object it prints how much of the path runs our code in the game: the path is
// every game function either side entered; "still original" is what our side still enters (through OriginalFunction or
// code those reach) and is not hooked; "ours" is the rest. Rows landing move functions from the second to the first.
//
// Stood in on both sides: the event registry 0xa42f30 (a test map, as in sg_geometry_node.cpp) and the director getter
// 0x1ae18d (the director fake; it hands out the device buffers and the triangle sets, each side's own: the factory 0x196560
// or ours, as the game's director with TriangleSetShape_Create). Neither stand-in is in the census.
#include "code_patch.h"
#include "difftest.h"
#include "gmdc_package_fx.h"
#include "gz_registry_fx.h"
#include "reach_census.h"
#include "sg_geometry_node_device_fx.h"
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "compgeom/TriangleSet.h"
#include "gz/cVertexFormat.h"
#include "rz/StaticGetters.h"
#include "gz/cIGZPropertySet.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cGeometryNode.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ptr;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kNodeFactory = 0xa42590, kContainerFactory = 0xa70aa0, kMeshFactory = 0x196560;
constexpr uint32_t kGraphicsService = 0x85522803, kGraphicsIid = 0xa55227bb;
constexpr uint32_t kSettingsService = 0x0ac1887e, kSettingsIid = 0x4bb0d269;
constexpr sgfx::RegistryStatics kRegistry{0x10f6074, 0x10f607c, 0x10f6084, 0x10f608c};
constexpr unsigned kVariants = 5;

// ---- the objects (Base\TSData\Res\Sims3D\Objects03.package, group 0x1c0532fa) ----
struct Object {
    const char *name, *kind;
    uint32_t instance, resource;
};
constexpr uint32_t kGroup = 0x1c0532fa;
constexpr const wchar_t *kPackage = L"\\TSData\\Res\\Sims3D\\Objects03.package";
constexpr Object kObjects[] = {
    {"sculptureChinaVase", "plain", 0xffddec53, 0xf890e1b7},
    {"sculptureUrn", "alpha", 0xff9d1031, 0xd7b02a85},
    {"sculptureVenus", "several materials", 0xff67dab5, 0x63e93daf},
};
constexpr unsigned kObjectCount = sizeof(kObjects) / sizeof(kObjects[0]);
std::vector<uint8_t> gBlocks[kObjectCount]; // each object's GMDC root block

// ---- interface tables (strict: every argument declared) ----
constexpr Slot kUnsettled{0, Ret::Int, false, "", ""};
template <unsigned N> constexpr std::array<Slot, N> FaceSlots()
{
    std::array<Slot, N> s{};
    for (Slot &x : s) x = kUnsettled;
    s[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    s[1] = {0, Ret::Int, true, "AddRef", ""};
    s[2] = {0, Ret::Int, true, "Release", ""};
    return s;
}
constexpr std::array<Slot, 25> MakeGraphicsSlots()
{
    auto s = FaceSlots<25>();
    s[8] = {3, Ret::Int, true, "Connect", "uW0u"};
    s[9] = {3, Ret::Int, true, "Disconnect", "uW0u"};
    s[21] = {0, Ret::Int, true, "GetCurrentDevice", ""};
    s[24] = {1, Ret::Int, true, "GetDevice", "u"};
    return s;
}
constexpr auto kGraphicsSlots = MakeGraphicsSlots();
constexpr drawfx::Iface kGraphics{"GraphicSystem", 25, kGraphicsSlots.data(), false, drawfx::kTestTable};
constexpr std::array<Slot, 10> MakeSettingsSlots()
{
    auto s = FaceSlots<10>();
    s[9] = {2, Ret::Int, true, "GetPropertyValue", "W0W0"};
    return s;
}
constexpr auto kSettingsSlots = MakeSettingsSlots();
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots.data(), false, drawfx::kTestTable};
constexpr std::array<Slot, 4> MakeDirectorSlots()
{
    auto s = FaceSlots<4>();
    s[3] = {3, Ret::Int, true, "GetClassObject", "uua"};
    return s;
}
constexpr auto kDirectorSlots = MakeDirectorSlots();
constexpr drawfx::Iface kDirector{"COMDirector", 4, kDirectorSlots.data(), false, drawfx::kTestTable};
constexpr std::array<Slot, 21> MakeBufferSlots()
{
    auto s = FaceSlots<21>();
    s[15] = {3, Ret::Int, true, "Lock", "uuu"};
    s[16] = {0, Ret::Int, true, "Unlock", ""};
    s[20] = {3, Ret::Int, true, "Init", "auu"};
    return s;
}
constexpr auto kBufferSlots = MakeBufferSlots();
constexpr drawfx::Iface kBuffer{"DeviceBuffer", 21, kBufferSlots.data(), false, drawfx::kTestTable};
// The device's cIDeviceEnum through its slot 15 (the device format lookup 0x18daa0): 20 HasProcessingCap, 21 GetLimit.
constexpr std::array<Slot, 22> MakeEnumSlots()
{
    auto s = FaceSlots<22>();
    s[20] = {1, Ret::Int, true, "HasProcessingCap", "u"};
    s[21] = {1, Ret::Int, true, "GetLimit", "u"};
    return s;
}
constexpr auto kEnumSlots = MakeEnumSlots();
constexpr drawfx::Iface kEnum{"DeviceEnumLimits", 22, kEnumSlots.data(), false, drawfx::kTestTable};

// ---- the world ----
constexpr unsigned kBuffers = 16, kBufferBytes = 0x100000;
struct World {
    Fake graphics, settings, director, limits;
    Fake buffers[kBuffers];
    uint32_t sizes[kBuffers] = {};
    unsigned nextBuffer = 0;
    sgfx::Manager manager;
    nGZGraphic4::cVertexFormatsManager *formats = nullptr;
    std::vector<cITriangleSetShape *> meshes;
    cISGResource *container = nullptr; // the test's hold on the loaded container (the reference it took on creation)
    unsigned object = 0;
    bool original = false;
    difftest::Context *ctx = nullptr;
};
World w;
devfx::World dw;
alignas(16) uint8_t gMemory[kBuffers][kBufferBytes];
void *gDirectorTable[16] = {};
void *__fastcall DirectorCom(void *, void *) { return &w.director; }
void *__cdecl StandFramework() { return &w.director; }

uint32_t Service(Fake &self, const uint32_t *a)
{
    Fake *f = a[0] == kGraphicsService && a[1] == kGraphicsIid   ? &w.graphics
              : a[0] == kSettingsService && a[1] == kSettingsIid ? &w.settings
                                                                 : nullptr;
    if (!f) return sgfx::detail::GetSystemService(self, a);
    ++f->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = f;
    return 1;
}
uint32_t GraphicsConnect(Fake &self, const uint32_t *a)
{
    self.value[0] = a[1];
    return 0;
}
uint32_t GraphicsDevice(Fake &, const uint32_t *) { return 0; }
uint32_t GraphicsGetDevice(Fake &, const uint32_t *) { return P(&dw.device); }
uint32_t SettingsGet(Fake &self, const uint32_t *a)
{
    sgfx::NoteText("setting", reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]))->c_str());
    *reinterpret_cast<uint8_t *>(uintptr_t(a[1]) + 4) = uint8_t(self.value[0]);
    return 1;
}

// The device's enum (slot 15) and its limits: 10 the stream count (value[0]), 0xe the largest stream stride (value[1]).
uint32_t DeviceEnum(Fake &, const uint32_t *) { return P(&w.limits); }
uint32_t Limit(Fake &self, const uint32_t *a) { return a[0] == 10 ? self.value[0] : a[0] == 0xe ? self.value[1] : 0; }

// The device buffers (static vertex and index arrays): Init(device, size, flag) records the size (true if it fits),
// Lock answers the buffer's memory.
unsigned BufferIndex(Fake &self) { return unsigned(&self - w.buffers); }
uint32_t BufferInit(Fake &self, const uint32_t *a)
{
    w.sizes[BufferIndex(self)] = a[1];
    return a[1] <= kBufferBytes;
}
uint32_t BufferLock(Fake &self, const uint32_t *) { return P(gMemory[BufferIndex(self)]); }

using MeshFactoryFn = cIGZUnknown *(__cdecl *)();
// The director: device buffers, triangle sets (the library's factory, both sides; each held once more by the test).
uint32_t DirectorGetClassObject(Fake &, const uint32_t *a)
{
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if ((a[0] == 0x3bffec52 && a[1] == 0xfbffec42) || (a[0] == 0x9bfff0ea && a[1] == 0xdbfff0de)) {
        if (w.nextBuffer == kBuffers) drawfx::Note("out of device buffers", w.nextBuffer);
        Fake &f = w.buffers[w.nextBuffer++ % kBuffers];
        ++f.refs;
        *out = &f;
        return 1;
    }
    if (a[0] == cITriangleSetShape::kClassId) {
        // the side's own triangle set (in the game the director makes ours: TriangleSetShape_Create hooks 0x196560)
        const bool ok = w.original ? w.ctx->Original<MeshFactoryFn>(kMeshFactory)()->QueryInterface(a[1], out)
                                   : nGZCompGeom::cTriangleSetShape::Create()->QueryInterface(a[1], out);
        auto *mesh = static_cast<cITriangleSetShape *>(*out);
        mesh->AddRef();
        difftest::NotCounted quiet;
        drawfx::AddRange(mesh, 0x30, 0x6d000000u | uint32_t(w.meshes.size()) << 12);
        w.meshes.push_back(mesh);
        return ok;
    }
    drawfx::Note("unknown class", {a[0], a[1]});
    *out = nullptr;
    return 0;
}

// The resource manager's load (key, iid, out, 0, 0): the object's GMDC decoded by the side's container (the decode on load
// that the game's resource manager would do), its face into *out.
using ContainerFactoryFn = cISGResource *(__cdecl *)();
uint32_t ManagerLoad(Fake &, const uint32_t *a)
{
    const auto *key = reinterpret_cast<const cGZPersistResourceKey *>(uintptr_t(a[0]));
    const Object &o = kObjects[w.object];
    drawfx::Note("load", {key->type, key->group, key->instance, key->resource, a[1]});
    void **out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (key->type != gmdcfx::kGmdcType || key->group != kGroup || key->instance != o.instance || key->resource != o.resource ||
        a[1] != cIGeometryDataContainer::kIid || w.container) {
        *out = nullptr;
        return 0;
    }
    gmdcfx::Over(&gBlocks[w.object]);
    cISGResource *r = w.original ? w.ctx->Original<ContainerFactoryFn>(kContainerFactory)() : cGeometryDataContainer::Create();
    drawfx::Note("created with refs", r->AddRef()); // the factory's object comes unreferenced; this one is the test's
    void *face = nullptr;
    const bool ok = r->QueryInterface(cIGeometryDataContainer::kIid, &face);
    drawfx::Note("made", uint32_t(ok && face != nullptr));
    if (!face) return 0;
    {
        difftest::NotCounted quiet;
        drawfx::AddRange(static_cast<uint8_t *>(face) - 0x2c, cGeometryDataContainer::kSize, 0x6c000000u);
    }
    static_cast<cIGeometryDataContainer *>(face)->Read(gmdcfx::Reader());
    drawfx::Note("read to", {uint32_t(gmdcfx::gAt), uint32_t(gBlocks[w.object].size())});
    w.container = r;
    *out = face; // the query's reference, the node's
    return 1;
}
uint32_t ManagerCached(Fake &, const uint32_t *) { return 0; }

// The five device setups: caps (devfx: bit 0 hardware, 1 software, 2 / 3 mixed, 4 the 0x18daa0 cap), ready, the limits.
struct Setup {
    uint32_t caps;
    bool ready;
    uint32_t streams, stride;
};
constexpr Setup kSetups[kVariants] = {
    {0x1f, true, 16, 0x100}, // everything
    {0x01, true, 16, 0x100}, // hardware only (no cap 4: element type 4 asked as 5)
    {0x02, true, 16, 0x100}, // software only
    {0x1f, true, 16, 8},     // strides over the limit: no device format
    {0x1f, false, 16, 0x100}, // device not ready
};

void Build(uint32_t seed, drawfx::Cells &cells)
{
    w.object = seed / kVariants;
    const Setup &setup = kSetups[seed % kVariants];
    sgfx::gPool.Reset(cells);
    for (const char *t : {"@@VisualizeNormals@@", "@@VisualizeTangents@@", "@@VisualizeBinormals@@"}) sgfx::gPool.Handle(t);
    sgfx::gResources.Reset(cells);
    sgfx::gResources.framework.On(5, &Service);
    sgfx::gResources.manager.Settle<3, 5>("W4uauu");
    sgfx::gResources.manager.On(3, &ManagerLoad);
    sgfx::gResources.manager.Settle<11, 1>("W4");
    sgfx::gResources.manager.On(11, &ManagerCached);

    w.graphics.Reset<kGraphics>(600);
    w.graphics.On(8, &GraphicsConnect);
    w.graphics.On(9, &GraphicsConnect);
    w.graphics.On(21, &GraphicsDevice);
    w.graphics.On(24, &GraphicsGetDevice);
    w.settings.Reset<kSettings>(602);
    w.settings.On(9, &SettingsGet);
    w.settings.value[0] = 0;
    w.director.Reset<kDirector>(603);
    w.director.On(3, &DirectorGetClassObject);
    w.limits.Reset<kEnum>(604);
    w.limits.On(20, &devfx::HasCap);
    w.limits.On(21, &Limit);
    w.limits.value[0] = setup.streams;
    w.limits.value[1] = setup.stride;
    for (unsigned i = 0; i < kBuffers; ++i) {
        w.buffers[i].Reset<kBuffer>(660 + i);
        w.buffers[i].On(15, &BufferLock);
        w.buffers[i].On(20, &BufferInit);
        w.sizes[i] = 0;
        std::memset(gMemory[i], 0xcd, kBufferBytes);
        drawfx::AddRange(gMemory[i], kBufferBytes, 0x79000000u | i << 20);
    }
    w.nextBuffer = 0;
    {
        difftest::NotCounted quiet;
        w.meshes.clear();
    }
    w.container = nullptr;
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cGeometryNodeBehaviorTraits::kEventManagerCell);
    gDirectorTable[12] = reinterpret_cast<void *>(&DirectorCom);
    cells.Set(0x10c9390, P(gDirectorTable));
    cells.Set(0x10c93f4, 0x80000000u);
    cells.Set(0x10ca87c, &w.graphics);

    std::mt19937 rng(seed * 7919u + 13u);
    dw.Reset(rng, 800);
    dw.Plant(cells);
    dw.device.Settle<15, 0>("");
    dw.device.On(15, &DeviceEnum);
    dw.caps_ = setup.caps;
    dw.ready = setup.ready;
    for (Fake &v : dw.vertices) {
        v.value[0] = 0x100000;
        v.value[1] = 0;
    }
    // The vertex formats: our manager (the game's is a static), fresh for each side.
    w.formats = new nGZGraphic4::cVertexFormatsManager;
    cells.Set(0x10caa24, w.formats);
}

// ---- notes ----
cGeometryNode *AsNode(cIGeometryNode *n) { return static_cast<cGeometryNode *>(n); }
cIGeometryRender *RenderFace(cIGeometryNode *n)
{
    return reinterpret_cast<cIGeometryRender *>(reinterpret_cast<uint8_t *>(n) + 0x40);
}

// A long note as a run of short calls (a mismatch is reported with the first 150 characters of the call).
void EmitChunks(const char *label, const std::vector<uint32_t> &words)
{
    for (size_t at = 0; at < words.size() || at == 0; at += 8) {
        drawfx::Call c;
        c.note = label;
        c.words.push_back(uint32_t(at));
        for (size_t i = at; i < words.size() && i < at + 8; ++i) c.words.push_back(words[i]);
        drawfx::gLog.push_back(std::move(c));
    }
}

// What the build left on the node: its members through the class's layout (the node's own vectors only as present /
// absent), the device objects as fakes, the formats as heap blocks.
void NoteBuilt(const char *label, cIGeometryNode *n)
{
    cGeometryNode *g = AsNode(n);
    drawfx::Call c;
    auto add = [&](std::initializer_list<uint32_t> list) { c.words.insert(c.words.end(), list); };
    add({uint32_t(g->mDeviceReady), uint32_t(g->mResourcesReady)});
    for (const auto &s : g->mSubsets) add({s.first.mCell.mValue, s.second});
    add({0xeeee});
    for (const cGeometryNodeElement &e : g->mElements) {
        for (int i = 0; i < 13; ++i) c.words.push_back(i == 5 ? uint32_t(e.mWords[5] != 0) : i == 3 ? Ptr(e.mWords[3]) : e.mWords[i]);
        add({e.mPrepared, e.mName.mCell.mValue, e.mWord3c, e.mWord4c, e.mWord50});
        c.words.insert(c.words.end(), e.mIndices.begin(), e.mIndices.end());
        add({0xdddd});
        for (const auto &r : e.mRefs) c.words.push_back(Ptr(r.mPtr));
        add({e.mTail[0], e.mWord3c == 0xffffffff ? 0u : e.mTail[1], e.mTail[2], e.mTail[3]});
    }
    add({0xeeee});
    for (const auto &r : g->mRecords) add({Ptr(r.mRef.mPtr), r.mWords[0], r.mWords[1], r.mWords[2]});
    add({0xeeee});
    for (const auto &r : g->mRecords24) c.words.insert(c.words.end(), r.mWords, r.mWords + 6);
    add({0xeeee});
    for (const auto &r : g->mRecordsF4) c.words.insert(c.words.end(), r.mWords, r.mWords + 7);
    add({0xeeee});
    for (const auto &r : g->mRecords100) c.words.insert(c.words.end(), r.mWords, r.mWords + 7);
    add({0xeeee});
    for (const auto &a : g->mNamesA) c.words.push_back(a.mCell.mValue);
    add({0xeeee});
    for (const auto &b : g->mNamesB) c.words.push_back(b.mCell.mValue);
    add({0xeeee});
    for (const auto &b : g->mBounds) c.words.push_back(Ptr(b.mPtr));
    add({Ptr(g->mRefB8.mPtr), Ptr(g->mRefBC.mPtr), Ptr(g->mRefC0.mPtr), Ptr(g->mRefC4.mPtr), Ptr(g->mContainer.mPtr)});
    add({g->mBoundsInfo, g->mBoundsCount, uint32_t(g->mWord94 != 0), uint32_t(g->mBoundsData != 0)});
    add({g->mRenderInfo[0], g->mRenderInfo[1], g->mRenderInfo[2], uint32_t(g->mRenderInfo[3] != 0),
         uint32_t(g->mRenderInfo[4] != 0)});
    c.words.insert(c.words.end(), g->mRenderInfo + 5, g->mRenderInfo + 11);
    EmitChunks(label, c.words);
}

// The device buffers: each one's size, the hash of its bytes (FNV-1a over the size it was made with: the vertex and index
// data the node wrote) and its references.
void NoteBuffers(const char *label)
{
    for (unsigned i = 0; i < kBuffers && i < w.nextBuffer; ++i) {
        const uint32_t used = w.sizes[i] < kBufferBytes ? w.sizes[i] : kBufferBytes;
        uint32_t h = 2166136261u;
        for (uint32_t k = 0; k < used; ++k) h = (h ^ gMemory[i][k]) * 16777619u;
        drawfx::Note(label, {i, w.sizes[i], h, uint32_t(w.buffers[i].refs)});
    }
}

bool HasDeclarations(const cGeometryNode *g)
{
    for (const cGeometryNodeElement &e : g->mElements)
        if (!e.mRefs[2].mPtr || !e.mRefs[3].mPtr) return false;
    return true;
}

// The frame: every element drawn through the render face, each way the renderer calls it.
void Draw(cIGeometryNode *n)
{
    cGeometryNode *g = AsNode(n);
    cIGeometryRender *r = RenderFace(n);
    const bool supported = r->IsSupported(1);
    drawfx::Note("is supported", supported);
    NoteBuilt("supported", n);
    NoteBuffers("buffer");
    if (!supported || !HasDeclarations(g)) return;
    for (uint32_t i = 0; i < g->mElements.size(); ++i) {
        drawfx::Note("element", i);
        drawfx::Note("render blocks", r->Render(i, &dw.context, 1, 1));
        drawfx::Note("render", r->Render(i, &dw.context, 1, 0));
        dw.target.value[0] = g->mElements[i].mWords[3]; // the caller's declaration, of the element's format
        drawfx::Note("prepare", r->Prepare(i, P(&dw.context), P(&dw.target), 1));
        drawfx::Note("render software", r->Render(i, &dw.context, 0, 1));
        drawfx::Note("prepare unflagged", r->Prepare(i, P(&dw.context), P(&dw.target), 0));
        drawfx::Note("render software unflagged", r->Render(i, &dw.context, 0, 0));
    }
    drawfx::Note("is supported again", r->IsSupported(1));
    dw.Lose();
    g->mResourcesReady = 0;
    const bool back = r->IsSupported(1);
    drawfx::Note("is supported after loss", back);
    if (back && HasDeclarations(g) && !g->mElements.empty()) drawfx::Note("render after loss", r->Render(0, &dw.context, 1, 1));
    NoteBuilt("after loss", n);
}

// ---- the census: each object's hits, per side and phase ----
struct Reach {
    census::Hits load[2], draw[2]; // [0] the original side, [1] ours
};
Reach gReach[kObjectCount];
const std::vector<uint32_t> kSkip{0xa42f30, 0x1ae18d}; // the stand-ins

using NodeFactory = cIGeometryNode *(__cdecl *)();
cIGeometryNode *__cdecl MakeOurs() { return cGeometryNode::Create(); }

void Run(bool original)
{
    w.original = original;
    const Object &o = kObjects[w.object];
    const census::Db &db = census::Load();
    const uintptr_t base = uintptr_t(w.ctx->image.Base());
    Reach &reach = gReach[w.object];
    const int side = original ? 0 : 1;

    cIGeometryNode *n = original ? w.ctx->Original<NodeFactory>(kNodeFactory)() : MakeOurs();
    drawfx::AddRange(n, cGeometryNode::kSize, sgfx::kNodeTag);
    n->AddRef();
    drawfx::Note("connected as", w.graphics.value[0] - P(n));
    cGZPersistResourceKey &key = AsNode(n)->mKey;
    key.type = gmdcfx::kGmdcType;
    key.group = kGroup;
    key.instance = o.instance;
    key.resource = o.resource;
    {
        census::Armed armed(db, base, reach.load[side], kSkip);
        n->Rebuild();
    }
    NoteBuilt("built", n);
    NoteBuffers("built buffer");
    {
        census::Armed armed(db, base, reach.draw[side], kSkip);
        Draw(n);
    }
    NoteBuffers("drawn buffer");

    difftest::FreeRecorder freed;
    // Released into a local: a Note inside the recorder window can grow the log and count its free (VH).
    const uint32_t released = n->Release();
    freed.Stop();
    drawfx::Note("node released", released);
    size_t sizes[64];
    const size_t count = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < count; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    if (w.container) drawfx::Note("container released", w.container->Release());
    w.container = nullptr;
    for (cITriangleSetShape *m : w.meshes) drawfx::Note("triangle set released", m->Release());
    delete w.formats;
    w.formats = nullptr;
    drawfx::Call refs;
    refs.note = "fake refs";
    for (const Fake *f : {&w.graphics, &w.settings, &w.director, &w.limits}) refs.words.push_back(uint32_t(f->refs));
    for (const Fake &f : w.buffers) refs.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(refs));
    dw.NoteRefs();
    sgfx::gResources.NoteRefs();
    sgfx::gPool.NoteCounts();
}

struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106a4e0;
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    MissingRecordScope()
    {
        std::memcpy(saved, GameAddress<void>(kAt), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kAt)[i] = 0x5ec0de00 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i; // ours reads its own record
        }
    }
    ~MissingRecordScope() { std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

census::Hits Union(const census::Hits &a, const census::Hits &b)
{
    census::Hits u = a;
    u.insert(b.begin(), b.end());
    return u;
}

} // namespace

DIFF_TEST(SG_Render_Object, 0xa3ebe0)
{
    const std::wstring game = gmdcfx::GameDir();
    if (game.empty()) {
        std::printf("  SKIP SG_Render_Object: no game copy (local/game)\n");
        return;
    }
    {
        gmdcfx::Package package;
        if (!package.Load(game + L"\\Base" + kPackage)) {
            std::printf("  SKIP SG_Render_Object: cannot read Objects03.package\n");
            return;
        }
        for (unsigned i = 0; i < kObjectCount; ++i) {
            const Object &o = kObjects[i];
            std::vector<uint8_t> resource;
            if (!package.Find(gmdcfx::kGmdcType, kGroup, o.instance, o.resource, resource)) {
                ctx.Fail("no GMDC %08x %08x %08x (or a bad QFS stream)", kGroup, o.instance, o.resource);
                return;
            }
            const size_t root = gmdcfx::RootBlock(resource);
            if (root == 0) {
                ctx.Fail("%s: not an RCOL with one GMDC block", o.name);
                return;
            }
            gBlocks[i].assign(resource.begin() + std::ptrdiff_t(root), resource.end());
        }
    }
    const census::Db &db = census::Load();
    if (!db.loaded) std::printf("  census off: %s\n", db.where.c_str());
    for (Reach &r : gReach)
        for (int s = 0; s < 2; ++s) {
            r.load[s].clear();
            r.draw[s].clear();
        }

    w.ctx = &ctx;
    gmdcfx::MakeTable();
    svcfx::AllocatorScope allocator;
    sgfx::RegistryScope registry{kRegistry};
    MissingRecordScope missing;
    shape::StandIn registryStand{0xa42f30, &cGeometryNodeBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kRegistry.map>};
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    sgfx::CheckSides(ctx, "render object", kObjectCount * kVariants, Build,
                     [&](bool original, std::mt19937 &) { Run(original); });
    w.manager.Drop();

    // The gauge, per object (docs/subsystems/scenegraph.md, "Milestone O": what it means).
    for (unsigned i = 0; i < kObjectCount; ++i) {
        const Object &o = kObjects[i];
        const Reach &r = gReach[i];
        std::printf("  object %s (%s): GMDC %08x %08x %08x %08x\n", o.name, o.kind, gmdcfx::kGmdcType, kGroup, o.instance,
                    o.resource);
        if (!db.loaded) continue;
        census::Print("load", census::Measure(db, r.load[0], r.load[1]), db, 0);
        census::Print("draw", census::Measure(db, r.draw[0], r.draw[1]), db, 0);
        census::Print("total", census::Measure(db, Union(r.load[0], r.draw[0]), Union(r.load[1], r.draw[1])), db, 8);
    }
}
