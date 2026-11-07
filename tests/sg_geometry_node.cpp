// Differential tests of nGZSceneGraph::cGeometryNode (lane QU, scenegraph D3 part 4; src/sg/cGeometryNode.h): the game's
// node (its factory 0xa42590, constructor 0xa3dd80 and the bodies behind its vtables 0xe6987c / 0xe6994c / 0xe6996c /
// 0xe699a4 / 0xe699cc) against ours (Create, our vtables), each side building its own nodes. The tests drive the node
// through all its faces (the node, the +8 persist face, the cSGResource part +0x14, the render face +0x40 and the
// virtual base +0x130) and compare the calls it makes on the fakes, the notes, every node word by its kind (kWordKinds),
// the members through the class's layout, the pool's counts, the fakes' references and the blocks freed when the nodes go.
//
//   SG_GeometryNode_Factory    a new node: words, vbtable, contents.
//   SG_GeometryNode_Construct  the constructor 0xa3dd80 and destructor 0xa3e320 on a raw buffer (the base-part path the
//                              derived classes take), against ours (the hooked symbol and Adapters::Destruct).
//   SG_GeometryNode_Helpers    Reset 0xa3ee00 and ReleaseElementResources 0xa3ed70 (hooked by RVA) on built nodes.
//   SG_GeometryNode_Slots      random operations through every face: Release 0xa43040, QueryInterface 0xa42b20, IsKindOf
//                              0xa43860, GetRefCount 0xa42640, OnEvent 0xa42760, NotifyListeners 0xa43410,
//                              AskListeners 0xa42510, Connect 0xa3f0e0, Disconnect 0xa423a0, GetEventCount 0xa42880,
//                              GetEventIds 0xa3e890, GetEventAttribute 0xa3eb00, GetRender 0xa3e880, AddPart 0xa3e790,
//                              ClearParts 0xa430c0, Rebuild 0xa3ebe0, AddListener 0xa3e830, RemoveListener 0xa43160,
//                              GetContainer 0xa42600, SetBounds 0xa43470, CountSubset 0xa428d0, GetTypeInfo 0xa43890,
//                              Write 0xa43950.
//   SG_GeometryNode_Persist    Read 0xa42bb0 tapes of versions 1-12 through the +8 face or the virtual base, then Write.
//   SG_GeometryNode_Subsets    GetSubsetNames 0xa436a0 / GetSubsetParts 0xa43770 and the subset iterators (each side its
//                              own: the game's 0xa3e070 / 0xa3e0c0 from the pools 0xa3e540 / 0xa3e570, ours), from test
//                              pool pages; each range walked through the iterator slots: names (copy 0xa3e040, Free
//                              0xa3e5a0, Clone 0xa3f080, Get 0xa42340, Next 0xa42850, destructors 0xa3e710 / 0xa3e520),
//                              parts (copy 0xa3e0a0, Free 0xa3e620, Clone 0xa3f0b0, Get 0xa42350, destructors 0xa3e750
//                              / 0xa3e530).
//   SG_GeometryNode_Build      BuildGeometry 0xa3f180 over random containers.
//   SG_GeometryNode_Render     the render face (IsSupported 0xa41ee0, Prepare 0xa429b0, Render 0xa435d0) and
//                              ResetDeviceState 0xa419e0 over the devfx device fakes (sg_geometry_node_device_fx.h).
//   SG_GeometryNode_Registry   the event registry 0xa42f30 (sg_geometry_node_registry.cpp).
//
// Stood in on both sides (StandIn: the image and ours): the geometry build 0xa3f180 (StandBuild fills elements, subsets
// and device objects from a counter), the device-state reset 0xa419e0 (noted) and the registry 0xa42f30 (a test map),
// except in the tests of those functions. The reset and the registry stay stood in here although both sides now have
// real code: the stand-in notes the flags each caller passes (OnEvent's (0, 0), the build's), which the real reset over
// counter-built device objects would not show; the real chain (build, then reset over device fakes) is the Render test.
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "misc/SmallVirtuals.h"
#include "sg_geometry_node_device_fx.h"
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "gz/cIGZPropertySet.h"
#include "gz/cVertexFormat.h"
#include "sg/cGeometryBuilder.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cGeometryNode.h"
#include "sg/cGeometryNodeFormats.h"
#include "sg/cGeometryNodeIterators.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <malloc.h>
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

constexpr uint32_t kFactory = 0xa42590;
constexpr uint32_t kSize = cGeometryNode::kSize;
constexpr unsigned kSeeds = 64;

constexpr uint32_t kGraphicsService = 0x85522803, kGraphicsIid = 0xa55227bb;
constexpr uint32_t kSettingsService = 0x0ac1887e, kSettingsIid = 0x4bb0d269;
constexpr uint32_t kContainerClass = 0xac4f8687, kIidContainer = 0x5bafb147;
constexpr uint32_t kOldPartClass = 0x5ce7e026, kIidOldPart = 0x7ce7e022;
constexpr uint32_t kIidPart = 0xbbb79c90, kIidBounds = 0x5ce57549;
constexpr uint32_t kBehaviorIid = 0xfb859324;
constexpr sgfx::RegistryStatics kRegistry{0x10f6074, 0x10f607c, 0x10f6084, 0x10f608c};

// Each word's kind: 'x' left out (vtables, the vbptr: the vbtable's entries are noted instead), 'u' the raw word, 'a' a
// pointer (drawfx::Ptr: a fake's id, a range's tag, a heap block's number), 'h' its low half (the upper half padding:
// +0x3e / +0x3f after the cSGResource part's two flags, +0x12a / +0x12b after the two ready bytes).
constexpr char kWordKinds[] = "xuxauxuu"   // +0x00 .. +0x1c
                              "uuuu"       // +0x20 the part's key
                              "xuah"       // +0x30 vbptr, name, segment, flags
                              "xau"        // +0x40 render face, subsets head, size
                              "uuuvvuuuuuu" // +0x4c render info ([3] and [4] are the built arrays)
                              "vvvvvv"     // +0x78 elements, +0x84 records
                              "uvua"       // +0x90 .. +0x9c (+0x94 the bounds data array)
                              "vvvvvv"     // +0xa0 records24, +0xac bounds
                              "aaaa"       // +0xb8 .. +0xc4
                              "vvvvvv"     // +0xc8 listeners, +0xd4 parts
                              "a"          // +0xe0 container
                              "uuuu"       // +0xe4 key
                              "vvvvvvvvvvvv" // +0xf4 .. +0x118 vectors
                              "uhux";      // +0x124 flags, ready bytes, vtordisp, the virtual base's vtable
static_assert(sizeof(kWordKinds) - 1 == kSize / 4, "a kind per word");

// ---- the test's interface tables (strict: every argument declared) ----
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
constexpr auto kObjectSlots = FaceSlots<3>();
constexpr drawfx::Iface kObject{"Object", 3, kObjectSlots.data(), false, drawfx::kTestTable};

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

constexpr std::array<Slot, 56> MakeContainerSlots()
{
    auto s = FaceSlots<56>();
    s[7] = {0, Ret::Int, true, "GetResourceConst", ""};
    s[8] = {0, Ret::Int, true, "GetResource", ""};
    s[55] = {0, Ret::Int, true, "CheckIntegrity", ""};
    return s;
}
constexpr auto kContainerSlots = MakeContainerSlots();
constexpr drawfx::Iface kContainer{"GeometryDataContainer", 56, kContainerSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 25> MakeResourceSlots()
{
    auto s = FaceSlots<25>();
    s[4] = {1, Ret::Int, true, "GetTypeInfo", "a"};
    s[12] = {2, Ret::Int, true, "AssignAndSubmit", "sa"};
    s[16] = {1, Ret::Int, true, "SetFlag", "b"};
    s[18] = {0, Ret::Int, true, "GetRecord18", ""};
    s[22] = {1, Ret::Int, true, "CopyKeyTo", "a"};
    return s;
}
constexpr auto kResourceSlots = MakeResourceSlots();
constexpr drawfx::Iface kResource{"SGResource", 25, kResourceSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 18> MakePartSlots()
{
    auto s = FaceSlots<18>();
    s[15] = {1, Ret::Int, true, "SetListener", "a"};
    s[16] = {1, Ret::Int, true, "Build", "a"};
    return s;
}
constexpr auto kPartSlots = MakePartSlots();
constexpr drawfx::Iface kPart{"GeometryBuilder", 18, kPartSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 4> MakeBoundsSlots()
{
    auto s = FaceSlots<4>();
    s[3] = {2, Ret::Int, true, "Find", "ua"};
    return s;
}
constexpr auto kBoundsSlots = MakeBoundsSlots();
constexpr drawfx::Iface kBoundsSource{"GeometryBoundsSource", 4, kBoundsSlots.data(), false, drawfx::kTestTable};

// ---- the world ----
// A part's kIidOldPart face (cGeometryBuilderV): a vbptr whose table leads to the part's cIGeometryBuilder (the fake).
struct VFace {
    const int32_t *vbptr;
    int32_t table[2];
    void Bind(Fake &f)
    {
        table[0] = 0;
        table[1] = int32_t(reinterpret_cast<uint8_t *>(&f) - reinterpret_cast<uint8_t *>(this));
        vbptr = table;
        f.owner = this;
    }
};
// The V face's own cIGeometryBuilder (through its vbtable).
cIGZUnknown *Through(void *v)
{
    return reinterpret_cast<cIGZUnknown *>(static_cast<uint8_t *>(v) + (*static_cast<int32_t **>(v))[1]);
}

struct World {
    Fake graphics, device, settings, director, oldPart, typeName, ext;
    Fake containers[4]; // 610-613; container i's resource is resources[i] (key instance 0x100 + i; 3's key type 0)
    Fake resources[4];  // 620-623
    Fake parts[6];      // 630-635 (the odd ones answer kIidOldPart with their V face)
    VFace partFaces[6], oldFace;
    Fake bounds[3];     // 640-642 (0 and 2 answer kIidBounds)
    Fake objects[4];    // 650-653 the build's device objects
    Fake listeners[2];  // 500, 501
    sgfx::Manager manager;
    cIGeometryNode *nodes[8] = {};
    unsigned count = 0;
    unsigned nextContainer = 0;
    unsigned builds = 0;
    uint32_t loads = 0; // manager slot 11's answer
};
World w;
void *gDirectorTable[16] = {};
void *__fastcall DirectorCom(void *, void *) { return &w.director; }

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
// Connect / Disconnect: the target's offset from the node it is (noted by the test).
uint32_t GraphicsConnect(Fake &self, const uint32_t *a)
{
    self.value[0] = a[1];
    return 0;
}
uint32_t GraphicsDevice(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t GraphicsGetDevice(Fake &, const uint32_t *) { return P(&w.device); }
// GetPropertyValue(key, any): the boolean at +4 set to value[0]; true.
uint32_t SettingsGet(Fake &self, const uint32_t *a)
{
    sgfx::NoteText("setting", reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]))->c_str());
    *reinterpret_cast<uint8_t *>(uintptr_t(a[1]) + 4) = uint8_t(self.value[0]);
    return 1;
}
Fake *BufferFor(uint32_t classId, uint32_t iid); // the real-build test's device buffers
uint32_t DirectorGetClassObject(Fake &, const uint32_t *a)
{
    if (Fake *buffer = BufferFor(a[0], a[1])) {
        *reinterpret_cast<void **>(uintptr_t(a[2])) = buffer;
        return 1;
    }
    Fake *f = nullptr;
    if (a[0] == kContainerClass && a[1] == kIidContainer) f = &w.containers[w.nextContainer++ % 4];
    else if (a[0] == kOldPartClass && a[1] == kIidOldPart) f = &w.oldPart;
    if (f) ++f->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = f == &w.oldPart ? static_cast<void *>(&w.oldFace) : f;
    return f != nullptr;
}
// A bounds source's Find(iid, out): 640 and 642 answer kIidBounds with themselves (counted), 641 nothing.
uint32_t BoundsFind(Fake &self, const uint32_t *a)
{
    if (a[0] != kIidBounds || self.id == 641) return 0;
    ++self.refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &self;
    return 1;
}
// A part's QueryInterface: kIidOldPart answers its V face (counted), anything else nothing.
uint32_t PartQuery(Fake &self, const uint32_t *a)
{
    if (a[0] != kIidOldPart || !self.owner) return 0;
    ++self.refs;
    if (a[1]) *reinterpret_cast<void **>(uintptr_t(a[1])) = self.owner;
    return 1;
}
uint32_t ContainerResource(Fake &self, const uint32_t *) { return self.value[0]; }
// GetTypeInfo(out): {the counted type name, the container's class, 1}.
uint32_t ResourceTypeInfo(Fake &, const uint32_t *a)
{
    auto *info = reinterpret_cast<cPersistTypeInfo *>(uintptr_t(a[0]));
    ++w.typeName.refs;
    info->mName = reinterpret_cast<cIGZUnknown *>(&w.typeName);
    info->mWord4 = kContainerClass;
    info->mVersion = 1;
    return a[0];
}
uint32_t ResourceRecord18(Fake &, const uint32_t *)
{
    static const char kSuffix[] = "_gmdc";
    return P(kSuffix);
}
uint32_t ResourceKey(Fake &self, const uint32_t *a)
{
    const uint32_t i = self.id - 620;
    auto *key = reinterpret_cast<cGZPersistResourceKey *>(uintptr_t(a[0]));
    key->type = i == 3 ? 0 : kContainerClass;
    key->group = 0x100 + i;
    key->instance = 0x200 + i;
    key->resource = 0x300 + i;
    return a[0];
}
uint32_t ResourceAssign(Fake &, const uint32_t *) { return 1; }
// The resource manager's load (key, iid, out, 0, 0): the container whose resource has that key (instance 0x200 + i).
uint32_t ManagerLoad(Fake &, const uint32_t *a)
{
    const auto *key = reinterpret_cast<const cGZPersistResourceKey *>(uintptr_t(a[0]));
    Fake *f = &w.containers[key->instance & 3];
    ++f->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = f;
    return 1;
}
uint32_t ManagerCached(Fake &, const uint32_t *) { return w.loads; }

cGeometryNode *AsNode(cIGeometryNode *n) { return static_cast<cGeometryNode *>(n); }
uint8_t *Bytes(cIGeometryNode *n) { return reinterpret_cast<uint8_t *>(n); }
uint32_t CountOf(cIGeometryNode *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
cIGZPersistSerializableReferent *PersistFace(cIGeometryNode *n)
{
    return reinterpret_cast<cIGZPersistSerializableReferent *>(Bytes(n) + 8);
}
cIGZPersistResource *PartFace(cIGeometryNode *n) { return reinterpret_cast<cIGZPersistResource *>(Bytes(n) + 0x14); }
cIGeometryRender *RenderFace(cIGeometryNode *n) { return reinterpret_cast<cIGeometryRender *>(Bytes(n) + 0x40); }
cISGResource *Vbase(cIGeometryNode *n) { return static_cast<cISGResource *>(AsNode(n)); }

void Build(uint32_t seed, drawfx::Cells &cells)
{
    sgfx::gPool.Reset(cells);
    for (const char *t : {"sub0", "sub1", "elem0", "elem1", "elem2", "nameA", "nameB", "lamp_gmnd", "alpha"})
        sgfx::gPool.Handle(t);
    sgfx::gReader.Reset();
    sgfx::gReader.fake.Settle<23, 1>("u");
    sgfx::gReader.fake.Settle<24, 2>("ua");
    sgfx::gReader.fake.On(24, &sgfx::detail::ReaderRef);
    sgfx::gWriter.Reset();
    sgfx::gResources.Reset(cells);
    sgfx::gResources.framework.On(5, &Service);
    sgfx::gResources.manager.Settle<3, 5>("W4uauu");
    sgfx::gResources.manager.On(3, &ManagerLoad);
    sgfx::gResources.manager.Settle<11, 1>("W4");
    sgfx::gResources.manager.On(11, &ManagerCached);
    w.loads = seed % 2;

    w.graphics.Reset<kGraphics>(600);
    w.graphics.On(8, &GraphicsConnect);
    w.graphics.On(9, &GraphicsConnect);
    w.graphics.On(21, &GraphicsDevice);
    w.graphics.On(24, &GraphicsGetDevice);
    w.graphics.value[1] = seed % 3 ? 0 : 0xffffffffu;
    w.device.Reset<kObject>(601);
    w.settings.Reset<kSettings>(602);
    w.settings.On(9, &SettingsGet);
    w.settings.value[0] = seed % 4 == 1;
    w.director.Reset<kDirector>(603);
    w.director.On(3, &DirectorGetClassObject);
    w.typeName.Reset<kObject>(604);
    w.oldPart.Reset<kPart>(605);
    w.oldFace.Bind(w.oldPart);
    w.oldPart.On(0, &PartQuery);
    drawfx::AddRange(&w.oldFace, sizeof(VFace), 0x7d000000);
    sgfx::MakeExtension(w.ext, 200, seed % 2);
    for (unsigned i = 0; i < 4; ++i) {
        w.resources[i].Reset<kResource>(620 + i);
        w.resources[i].On(4, &ResourceTypeInfo);
        w.resources[i].On(12, &ResourceAssign);
        w.resources[i].On(18, &ResourceRecord18);
        w.resources[i].On(22, &ResourceKey);
        w.containers[i].Reset<kContainer>(610 + i);
        w.containers[i].value[0] = P(&w.resources[i]);
        w.containers[i].On(7, &ContainerResource);
        w.containers[i].On(8, &ContainerResource);
        w.objects[i].Reset<kObject>(650 + i);
    }
    for (unsigned i = 0; i < 6; ++i) {
        w.parts[i].Reset<kPart>(630 + i);
        w.parts[i].owner = nullptr;
        if (i % 2) {
            w.partFaces[i].Bind(w.parts[i]);
            drawfx::AddRange(&w.partFaces[i], sizeof(VFace), 0x7d000000 | ((i + 1) << 12));
        }
        w.parts[i].On(0, &PartQuery);
    }
    for (unsigned i = 0; i < 3; ++i) {
        w.bounds[i].Reset<kBoundsSource>(640 + i);
        w.bounds[i].On(3, &BoundsFind);
    }
    sgfx::MakeListener(w.listeners[0], 500, true);
    sgfx::MakeListener(w.listeners[1], 501, seed % 2 != 0);
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cGeometryNodeBehaviorTraits::kEventManagerCell);
    gDirectorTable[12] = reinterpret_cast<void *>(&DirectorCom);
    cells.Set(0x10c9390, P(gDirectorTable));
    cells.Set(0x10c93f4, 0x80000000u);
    cells.Set(0x10ca87c, &w.graphics);
    w.count = 0;
    w.nextContainer = 0;
    w.builds = 0;
}

// ---- the stand-ins ----
// 0xa3f180 (part 5, the build over the container): an element (up to three) named from the pool with four device objects,
// the reference members filled, a subset entry, a name in each name list and the render flags, from a build counter.
void Counted(cGeometryNodeRef<cIGZUnknown> &ref, Fake &f)
{
    if (ref.mPtr) return;
    ++f.refs;
    ref.mPtr = reinterpret_cast<cIGZUnknown *>(&f);
}
uint32_t Cell(const char *text)
{
    const uint32_t handle = sgfx::gPool.Handle(text);
    ++sgfx::gPool.counts[sgfx::gPool.Index(handle)];
    return handle;
}
void __fastcall StandBuild(cGeometryNode *self, void *, void *container)
{
    const unsigned k = w.builds++;
    drawfx::Note("build", {Ptr(self), Ptr(container), k});
    if (self->mElements.size() < 3) {
        static const char *const kNames[] = {"elem0", "elem1", "elem2"};
        cGeometryNodeElement &e = self->mElements.emplace_back();
        e.mWords[0] = 0xe000 + k;
        e.mName.mCell.mValue = Cell(kNames[k % 3]);
        for (unsigned i = 2; i < 6; ++i) Counted(e.mRefs[i], w.objects[(i + k) % 4]);
        e.mPrepared = 1;
    }
    Counted(self->mRefB8, w.objects[k % 4]);
    Counted(self->mRefBC, w.objects[(k + 1) % 4]);
    Counted(self->mRefC0, w.objects[(k + 2) % 4]);
    Counted(self->mRefC4, w.objects[(k + 3) % 4]);
    cGeometryNodeCell subset;
    subset.mCell.mValue = Cell(k % 2 ? "sub1" : "sub0");
    self->mSubsets.emplace(std::move(subset), k);
    if (k % 2 == 0) { // a second part under the same name, so CountSubset counts more than one (coordinator mutant)
        cGeometryNodeCell again;
        again.mCell.mValue = Cell("sub0");
        self->mSubsets.emplace(std::move(again), k + 0x100);
    }
    self->mNamesA.emplace_back().mCell.mValue = Cell("nameA");
    if (k % 2) self->mNamesB.emplace_back().mCell.mValue = Cell("nameB");
    self->mRenderInfo[0] = 0x100 + k;
    self->mDeviceReady = 1;
    self->mResourcesReady = 1;
}
// 0xa419e0 (part 6, the device state reset; its own test is SG_GeometryNode_Render): the flags are bytes.
void __fastcall StandDeviceReset(cGeometryNode *self, void *, uint32_t a, uint32_t b)
{
    drawfx::Note("device reset", {Ptr(self), a & 0xff, b & 0xff});
}
struct Stands {
    shape::StandIn build{0xa3f180, &cGeometryNode::BuildGeometry, &StandBuild};
    shape::StandIn reset{0xa419e0, &cGeometryNode::ResetDeviceState, &StandDeviceReset};
    // The event registry 0xa42f30 (ours since part 6; its own test is tests/sg_geometry_node_registry.cpp): both sides on
    // the map RegistryScope plants.
    shape::StandIn registry{0xa42f30, &cGeometryNodeBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kRegistry.map>};
};

// ---- nodes ----
using Factory = cIGeometryNode *(__cdecl *)();
cIGeometryNode *__cdecl MakeOurs() { return cGeometryNode::Create(); }
Factory Maker(difftest::Context &ctx, bool original) { return original ? ctx.Original<Factory>(kFactory) : &MakeOurs; }

// A new node, held (counted once by the test) and registered.
cIGeometryNode *Make(Factory make)
{
    cIGeometryNode *n = make();
    drawfx::AddRange(n, kSize, sgfx::kNodeTag | (w.count << 12));
    n->AddRef();
    drawfx::Note("connected as", w.graphics.value[0] - P(n));
    w.nodes[w.count++] = n;
    return n;
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

void NoteWords(const char *label, const void *node)
{
    drawfx::Call c;
    c.note = label;
    const uint32_t *words = static_cast<const uint32_t *>(node);
    for (unsigned k = 0; k < kSize / 4; ++k) {
        switch (kWordKinds[k]) {
        case 'u': c.words.push_back(words[k]); break;
        case 'a': c.words.push_back(Ptr(words[k])); break;
        case 'h': c.words.push_back(words[k] & 0xffff); break;
        case 'v': c.words.push_back(words[k] != 0); break; // a vector's pointers: set or not (the heap's addresses)
        default: break;
        }
    }
    const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(static_cast<const uint8_t *>(node) + 0x30);
    c.words.push_back(uint32_t(vbtable[0]));
    c.words.push_back(uint32_t(vbtable[1]));
    EmitChunks(label, c.words);
}

// The words, then the members through the class's layout (the game's STL is ours).
void NoteNode(const char *label, cIGeometryNode *n)
{
    NoteWords(label, n);
    cGeometryNode *g = AsNode(n);
    drawfx::Call c;
    c.note = "contents";
    for (const auto &s : g->mSubsets) c.words.insert(c.words.end(), {s.first.mCell.mValue, s.second});
    c.words.push_back(0xeeee);
    for (const cGeometryNodeElement &e : g->mElements) {
        c.words.insert(c.words.end(), {e.mWords[0], e.mPrepared, e.mName.mCell.mValue});
        for (const auto &r : e.mRefs) c.words.push_back(Ptr(r.mPtr));
    }
    c.words.push_back(0xeeee);
    for (const auto &b : g->mBounds) c.words.push_back(Ptr(b.mPtr));
    for (auto *l : g->mListeners) c.words.push_back(Ptr(l));
    for (const auto &p : g->mParts) c.words.push_back(Ptr(p.mPtr));
    for (const auto &a : g->mNamesA) c.words.push_back(a.mCell.mValue);
    for (const auto &b : g->mNamesB) c.words.push_back(b.mCell.mValue);
    c.words.insert(c.words.end(), {uint32_t(g->mRecords.size()), uint32_t(g->mRecords24.size()),
                                   uint32_t(g->mRecordsF4.size()), uint32_t(g->mRecords100.size())});
    EmitChunks("contents", c.words);
}

void NoteAll(const char *label)
{
    for (unsigned i = 0; i < w.count; ++i) NoteNode(label, w.nodes[i]);
}

// Releases the test's references, noting the blocks freed and what the fakes and the pool hold.
void ReleaseAll()
{
    difftest::FreeRecorder freed;
    for (unsigned i = w.count; i-- > 0;) w.nodes[i]->Release();
    w.count = 0;
    freed.Stop();
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    drawfx::Call r;
    r.note = "fake refs";
    for (const Fake *f : {&w.graphics, &w.device, &w.settings, &w.director, &w.oldPart, &w.typeName, &w.ext})
        r.words.push_back(uint32_t(f->refs));
    for (const Fake &f : w.containers) r.words.push_back(uint32_t(f.refs));
    for (const Fake &f : w.resources) r.words.push_back(uint32_t(f.refs));
    for (const Fake &f : w.parts) r.words.push_back(uint32_t(f.refs));
    for (const Fake &f : w.bounds) r.words.push_back(uint32_t(f.refs));
    for (const Fake &f : w.objects) r.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(r));
    sgfx::gPool.NoteCounts();
    sgfx::gResources.NoteRefs();
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

// Everything a test needs around the sides: the allocator service (the settings key is a service string), the event
// registry, the missing record, the stand-ins.
struct Scope {
    svcfx::AllocatorScope allocator;
    sgfx::RegistryScope registry{kRegistry};
    MissingRecordScope missing;
    Stands stands;
};

// Whether GetContainer / Write can find a container (the original uses a missing one without a check).
bool HasContainerSource(cIGeometryNode *n)
{
    cGeometryNode *g = AsNode(n);
    return g->mContainer.mPtr || g->mKey.type != 0 || !g->mParts.empty();
}

void NoteResource(cISGResource *face)
{
    sgfx::NoteText("name", static_cast<const char *>(face->GetName()));
    cGZPersistResourceKey key{};
    face->CopyKeyTo(&key);
    drawfx::Note("resource", {face->GetFlag(), face->HasKey(), Ptr(face->GetSegment()), key.type, key.group, key.instance,
                              key.resource});
}

cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}

void Operate(difftest::Context &ctx, int step, std::mt19937 &rng)
{
    const unsigned which = rng() % w.count;
    cIGeometryNode *n = w.nodes[which];
    cGeometryNode *g = AsNode(n);
    const unsigned op = rng() % 24;
    drawfx::Note("op", {which, op});
    char mark[32];
    std::snprintf(mark, sizeof(mark), "step %d, op %u", step, op);
    sgfx::MarkStep(ctx, mark);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cIGeometryNode::kIid, cGeometryNode::kIidRender, cGeometryNode::kIidUncounted,
                                 cIObjectGraphNode::kIid, cISGResource::kIid, cGZPersistResource::kIid, kBehaviorIid,
                                 0x3b253dc7, sgfx::kIidTree, 1, 0x12345678};
        const uint32_t iid = iids[rng() % 11];
        const unsigned via = rng() % 5;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(n)
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                          : via == 3 ? static_cast<cIGZUnknown *>(RenderFace(n))
                                     : static_cast<cIGZUnknown *>(Vbase(n));
        const bool withOut = rng() % 8 != 0;
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = on->QueryInterface(iid, withOut ? &out : nullptr);
        drawfx::Note("query", {via, iid, uint8_t(r), Ptr(out), CountOf(n) - before});
        if (out && CountOf(n) != before) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release through any face, the count through the part's
        const unsigned via = rng() % 5;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(n)
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                          : via == 3 ? static_cast<cIGZUnknown *>(RenderFace(n))
                                     : static_cast<cIGZUnknown *>(Vbase(n));
        const uint32_t a = on->AddRef();
        const uint32_t c = PartFace(n)->GetRefCount();
        const uint32_t r = on->Release();
        drawfx::Note("counts", {via, a, c, r});
        break;
    }
    case 2: { // a part
        const unsigned p = rng() % 6;
        n->AddPart(reinterpret_cast<cIGeometryBuilder *>(&w.parts[p]));
        break;
    }
    case 3: n->ClearParts(); break;
    case 4: n->Rebuild(); break;
    case 5: { // FindPart
        const uint32_t iids[] = {kIidOldPart, kIidPart, 0x1234};
        void *out = nullptr;
        const uint8_t r = n->FindPart(iids[rng() % 3], &out);
        drawfx::Note("find part", {r, Ptr(out)});
        if (out) Through(out)->Release();
        break;
    }
    case 6: { // listeners: one added or removed, or both added (either order) and one removed (not always the last)
        const unsigned pick = rng() % 2;
        const unsigned mode = rng() % 3;
        const unsigned remove = rng() % 2;
        auto *l = reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&w.listeners[pick]);
        auto *other = reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&w.listeners[1 - pick]);
        if (mode == 0) {
            n->AddListener(l);
            n->AddListener(other);
            n->RemoveListener(remove ? l : other);
        } else {
            const bool held = std::find(g->mListeners.begin(), g->mListeners.end(), l) != g->mListeners.end();
            if (held && remove)
                n->RemoveListener(l);
            else
                n->AddListener(l);
        }
        drawfx::Call c;
        c.note = "listeners";
        for (auto *held : g->mListeners) c.words.push_back(Ptr(held));
        drawfx::gLog.push_back(std::move(c));
        break;
    }
    case 7: { // a container set
        const unsigned k = rng() % 5;
        n->SetContainer(k < 4 ? reinterpret_cast<cIGeometryDataContainer *>(&w.containers[k]) : nullptr);
        break;
    }
    case 8: { // the container (loaded or built), counted
        if (rng() % 8 == 0) {
            drawfx::Note("container", uint8_t(n->GetContainer(nullptr)));
            break;
        }
        if (!HasContainerSource(n)) break;
        cIGeometryDataContainer *out = nullptr;
        const bool r = n->GetContainer(&out);
        drawfx::Note("container", {uint8_t(r), Ptr(out)});
        if (out) out->Release();
        break;
    }
    case 9: { // bounds
        cIGZUnknown *objects[4] = {};
        const uint32_t count = rng() % 4;
        for (uint32_t i = 0; i < count; ++i) {
            const unsigned k = rng() % 4;
            objects[i] = k < 3 ? reinterpret_cast<cIGZUnknown *>(&w.bounds[k]) : nullptr;
        }
        n->SetBounds(count, rng() % 6 ? objects : nullptr);
        break;
    }
    case 10: { // the build flags (a change rebuilds)
        const uint32_t flags = rng() % 3 ? rng() % 4 : n->GetBuildFlags();
        n->SetBuildFlags(flags);
        drawfx::Note("build flags", n->GetBuildFlags());
        break;
    }
    case 11: { // subsets counted
        const char *names[] = {"sub0", "sub1", "", nullptr, "none"};
        const char *name = names[rng() % 5];
        drawfx::Note("subset count", n->CountSubset(name));
        break;
    }
    case 12: { // the element and name getters
        const uint32_t elements = uint32_t(g->mElements.size());
        for (uint32_t i = 0; i < elements; ++i) {
            sgfx::NoteText("element name", reinterpret_cast<const char *>(uintptr_t(n->GetElementName(i))));
            drawfx::Note("element handle", n->GetElementNameHandle(i));
        }
        for (uint32_t i = 0; i < g->mNamesA.size(); ++i)
            sgfx::NoteText("name A", reinterpret_cast<const char *>(uintptr_t(n->GetNameA(i))));
        for (uint32_t i = 0; i < g->mNamesB.size(); ++i)
            sgfx::NoteText("name B", reinterpret_cast<const char *>(uintptr_t(n->GetNameB(i))));
        drawfx::Note("getters", {Ptr(n->GetBoundsInfo()), Ptr(n->GetRender()), Ptr(n->GetRenderConst())});
        break;
    }
    case 13: { // events to the node: rebuild, the device (back or not), another
        uint32_t data[2] = {0, rng() % 3 ? 1u : 0u};
        const unsigned k = rng() % 3;
        if (k == 2) w.graphics.value[1] = rng() % 3 ? 0 : 0xffffffffu;
        const uint32_t type = k == 0 ? cGeometryNode::kRebuildEvent : k == 1 ? 0x777u : cGeometryNode::kDeviceEvent;
        const uint32_t arg1 = rng() % 4;
        const uint32_t arg3 = rng() % 4;
        n->OnEvent(type, arg1, data, arg3);
        break;
    }
    case 14: { // listeners told and asked
        nGZEvent::cNullEvent event{GameAddress<void>(nGZEvent::cNullEvent::kVTable)};
        const uint32_t type = rng() % 2 ? cGeometryNode::kBuiltEvent : 0x1234u;
        const bool notify = rng() % 2 != 0;
        const uint32_t arg1 = rng() % 4;
        const bool withEvent = rng() % 2 != 0;
        // Sometimes no listeners held but a connection answering yes: the ask then turns on "any listener at all".
        const bool bare = rng() % 3 == 0;
        if (bare) {
            const std::vector<nGZEvent::cIAbstractBehavior *> held(g->mListeners.begin(), g->mListeners.end());
            for (auto *l : held) n->RemoveListener(l);
            n->Connect(type, reinterpret_cast<cIGZUnknown *>(&w.listeners[0]), 0x500);
            drawfx::Note("active", n->IsActive());
        }
        if (notify)
            n->NotifyListeners(type, arg1, withEvent ? &event : nullptr);
        else
            drawfx::Note("asked", uint8_t(n->AskListeners(type, arg1, withEvent ? &event : nullptr)));
        break;
    }
    case 15: { // connections through the event manager
        auto *l = reinterpret_cast<cIGZUnknown *>(&w.listeners[rng() % 2]);
        const uint32_t event = rng() % 2 ? cGeometryNode::kBuiltEvent : 0x1234u;
        if (rng() % 2)
            n->Connect(event, l, 0x500 + rng() % 2);
        else
            n->Disconnect(event, l, 0x500 + rng() % 2);
        drawfx::Note("active", n->IsActive());
        break;
    }
    case 16: { // kinds
        const uint32_t iids[] = {cIGeometryNode::kIid, cIObjectGraphNode::kIid, cISGResource::kIid, kBehaviorIid,
                                 sgfx::kIidTree, cGeometryNode::kIidRender, 1, 0};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 17: { // the virtual base: names, flag, segment
        cISGResource *face = Vbase(n);
        switch (rng() % 3) {
        case 0: {
            const char *names[] = {"lamp_gmnd", "x", "", "alpha"};
            const char *name = names[rng() % 4];
            cIGZUnknown *segment = Segment(rng);
            drawfx::Note("assign", face->AssignAndSubmit(name, segment));
            break;
        }
        case 1: face->SetFlag(uint8_t(rng())); break;
        default: drawfx::Note("set segment", uint8_t(face->SetSegment(Segment(rng)))); break;
        }
        NoteResource(face);
        sgfx::NoteText("record 18", static_cast<const char *>(face->GetRecord18()));
        sgfx::NoteText("record 19", static_cast<const char *>(face->GetRecord19()));
        break;
    }
    case 18: { // the type info, through either persist face
        cPersistTypeInfo info = rng() % 2 ? PersistFace(n)->GetTypeInfo() : Vbase(n)->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        drawfx::Note("owner", {PersistFace(n)->GetOwnerInfo() == Vbase(n)->GetOwnerInfo()});
        break;
    }
    case 19: { // the render face's own slots
        cIGeometryRender *r = RenderFace(n);
        drawfx::Note("render flags", Ptr(r->GetFlags()));
        for (uint32_t i = 0; i < g->mElements.size(); ++i) drawfx::Note("draw info", Ptr(r->GetDrawInfo(i)));
        break;
    }
    case 20: // written, through either persist face
        if (rng() % 2)
            PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            Vbase(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        break;
    case 21: { // the event registry
        drawfx::Note("event count", n->GetEventCount());
        const uint32_t events[] = {sgfx::kRegistryIds[0], sgfx::kRegistryIds[2], 0x5678};
        const uint32_t event = events[rng() % 3];
        const nGZEvent::cEventAttribute a = n->GetEventAttribute(event);
        drawfx::Note("event attribute", {a.mId, a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
        if (event == 0x5678)
            drawfx::Note("missing name", P(a.mName));
        else
            sgfx::NoteText("event name", a.mName);
        break;
    }
    case 22: { // the tag
        const char *texts[] = {"alpha", "", nullptr};
        n->SetTag(texts[rng() % 3]);
        drawfx::Note("tag handle", n->GetTagHandle());
        break;
    }
    default: { // the vbase's resource reached through a QueryInterface of the part face
        drawfx::Note("part key", PartFace(n)->GetRefCount());
        break;
    }
    }
}

// ---- the real build (part 5): fakes for the formats manager and the device buffers, containers from our class ----
constexpr std::array<Slot, 18> MakeFormatSlots()
{
    auto s = FaceSlots<18>();
    s[4] = {0, Ret::Int, true, "GetStreamCount", ""};
    s[9] = {3, Ret::Int, true, "FindElementAny", "aub"};
    s[11] = {1, Ret::Int, true, "GetStreamSize", "u"};
    s[13] = {2, Ret::Int, true, "GetElementOffset", "uu"};
    s[15] = {2, Ret::Int, true, "GetOffsetAny", "ub"};
    return s;
}
constexpr auto kFormatSlots = MakeFormatSlots();
constexpr drawfx::Iface kFormat{"VertexFormat", 18, kFormatSlots.data(), false, drawfx::kTestTable};

constexpr std::array<Slot, 6> MakeFormatsManagerSlots()
{
    auto s = FaceSlots<6>();
    s[3] = {0, Ret::Int, true, "BeginFormat", ""};
    s[4] = {4, Ret::Int, true, "AddElement", "uuub"};
    s[5] = {1, Ret::Int, true, "EndFormat", "a"};
    return s;
}
constexpr auto kFormatsManagerSlots = MakeFormatsManagerSlots();
constexpr drawfx::Iface kFormatsManager{"VertexFormatsManager", 6, kFormatsManagerSlots.data(), false, drawfx::kTestTable};

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

constexpr std::array<Slot, 4> MakeShapeSlots()
{
    auto s = FaceSlots<4>();
    s[3] = {2, Ret::Int, true, "CloneAs", "ua"};
    return s;
}
constexpr auto kShapeSlots = MakeShapeSlots();
constexpr drawfx::Iface kShape{"TriangleSetShape", 4, kShapeSlots.data(), false, drawfx::kTestTable};
// A bounding mesh's CloneAs(iid, out): the fake itself, counted.
uint32_t ShapeClone(Fake &self, const uint32_t *a)
{
    ++self.refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &self;
    return 1;
}

constexpr uint32_t kPositionId =0x5b830781, kNormalId = 0x3b83078b, kTangentId = 0x89d92ba0, kBinormalId = 0x69d92b93;
constexpr uint32_t kUvId = 0xbb8307ab, kBoneIndexId = 0xfbd70111, kBoneWeightId = 0x3bd70105;
constexpr unsigned kBufferCount = 6, kBufferBytes = 0x10000;

struct FormatData {
    std::vector<nGZGraphic4::cVertexElement> streams[2];
};
struct BuildWorld {
    Fake manager;
    Fake formats[8];
    FormatData data[8];
    Fake buffers[kBufferCount];
    Fake meshes[3];
    uint32_t sizes[kBufferCount] = {};
    unsigned nextFormat = 0, nextBuffer = 0;
    std::vector<nGZGraphic4::cVertexElement> pending;
    uint8_t memory[kBufferCount][kBufferBytes];
};
BuildWorld bw;

uint32_t TypeBytes(uint32_t type)
{
    static const uint32_t sizes[] = {4, 8, 12, 16, 4, 4};
    return type < 6 ? sizes[type] : 0;
}
FormatData &DataOf(Fake &self) { return bw.data[&self - bw.formats]; }
uint32_t FormatStreamCount(Fake &self, const uint32_t *)
{
    FormatData &d = DataOf(self);
    return d.streams[1].empty() ? (d.streams[0].empty() ? 0 : 1) : 2;
}
uint32_t FormatFind(Fake &self, const uint32_t *a)
{
    FormatData &d = DataOf(self);
    auto *out = reinterpret_cast<nGZGraphic4::cVertexElement *>(uintptr_t(a[0]));
    for (const auto &stream : d.streams)
        for (const auto &e : stream)
            if (e.usage == a[1] && e.index == uint8_t(a[2])) {
                *out = e;
                return a[0];
            }
    std::memset(out, 0, sizeof *out);
    out->usage = a[1];
    out->index = uint8_t(a[2]);
    out->type = 6;
    out->stream = uint8_t(FormatStreamCount(self, nullptr));
    return a[0];
}
uint32_t FormatStreamSize(Fake &self, const uint32_t *a)
{
    uint32_t total = 0;
    if (a[0] < 2)
        for (const auto &e : DataOf(self).streams[a[0]]) total += TypeBytes(e.type);
    return total;
}
uint32_t FormatElementOffset(Fake &self, const uint32_t *a)
{
    uint32_t total = 0;
    if (a[0] < 2) {
        const auto &stream = DataOf(self).streams[a[0]];
        for (uint32_t i = 0; i < a[1] && i < stream.size(); ++i) total += TypeBytes(stream[i].type);
    }
    return total;
}
uint32_t FormatOffsetAny(Fake &self, const uint32_t *a)
{
    for (const auto &stream : DataOf(self).streams) {
        uint32_t total = 0;
        for (const auto &e : stream) {
            if (e.usage == a[0] && e.index == uint8_t(a[1])) return total;
            total += TypeBytes(e.type);
        }
    }
    return 0;
}
uint32_t ManagerBegin(Fake &, const uint32_t *)
{
    bw.pending.clear();
    return 0;
}
uint32_t ManagerAdd(Fake &, const uint32_t *a)
{
    nGZGraphic4::cVertexElement e{};
    e.usage = a[2];
    e.index = uint8_t(a[3]);
    e.type = a[1];
    e.stream = uint8_t(a[0]);
    bw.pending.push_back(e);
    return 0;
}
uint32_t ManagerEnd(Fake &, const uint32_t *a)
{
    const unsigned i = bw.nextFormat++ % 8;
    FormatData &d = bw.data[i];
    d.streams[0].clear();
    d.streams[1].clear();
    for (auto e : bw.pending) {
        auto &stream = d.streams[e.stream & 1];
        e.position = uint8_t(stream.size());
        stream.push_back(e);
    }
    ++bw.formats[i].refs;
    *reinterpret_cast<void **>(uintptr_t(a[0])) = &bw.formats[i];
    return 0;
}
unsigned BufferIndex(Fake &self) { return unsigned(&self - bw.buffers); }
uint32_t BufferInit(Fake &self, const uint32_t *a)
{
    bw.sizes[BufferIndex(self)] = a[1];
    return 1;
}
uint32_t BufferLock(Fake &self, const uint32_t *) { return P(bw.memory[BufferIndex(self)]); }
Fake *BufferFor(uint32_t classId, uint32_t iid)
{
    if (!((classId == 0x3bffec52 && iid == 0xfbffec42) || (classId == 0x9bfff0ea && iid == 0xdbfff0de))) return nullptr;
    Fake &f = bw.buffers[bw.nextBuffer++ % kBufferCount];
    ++f.refs;
    return &f;
}

void BuildReal(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed, cells);
    for (const char *t : {"@@VisualizeNormals@@", "@@VisualizeTangents@@", "@@VisualizeBinormals@@"}) sgfx::gPool.Handle(t);
    bw.manager.Reset<kFormatsManager>(690);
    bw.manager.On(3, &ManagerBegin);
    bw.manager.On(4, &ManagerAdd);
    bw.manager.On(5, &ManagerEnd);
    for (unsigned i = 0; i < 8; ++i) {
        bw.formats[i].Reset<kFormat>(700 + i);
        bw.formats[i].On(4, &FormatStreamCount);
        bw.formats[i].On(9, &FormatFind);
        bw.formats[i].On(11, &FormatStreamSize);
        bw.formats[i].On(13, &FormatElementOffset);
        bw.formats[i].On(15, &FormatOffsetAny);
        bw.data[i].streams[0].clear();
        bw.data[i].streams[1].clear();
    }
    for (unsigned i = 0; i < kBufferCount; ++i) {
        bw.buffers[i].Reset<kBuffer>(660 + i);
        bw.buffers[i].On(15, &BufferLock);
        bw.buffers[i].On(20, &BufferInit);
        bw.sizes[i] = 0;
        std::memset(bw.memory[i], 0xcd, kBufferBytes);
    }
    for (unsigned i = 0; i < 3; ++i) {
        bw.meshes[i].Reset<kShape>(670 + i);
        bw.meshes[i].On(3, &ShapeClone);
    }
    bw.nextFormat = bw.nextBuffer = 0;
    bw.pending.clear();
    cells.Set(0x10caa24, &bw.manager);
}

// A container whose group lookup tolerates the visualization elements' group number (0xffffffff): an empty range.
class SafeContainer final : public cGeometryDataContainer {
public:
    cGeometryRange<uint16_t> *GetGroupVertexIndices(cGeometryRange<uint16_t> *out, uint32_t index) override
    {
        if (index < mGroups.size()) return cGeometryDataContainer::GetGroupVertexIndices(out, index);
        return FillGeometryRange<uint16_t>(out, new cGeometryNullIterator, new cGeometryNullIterator);
    }
};

float Pick(std::mt19937 &rng, std::initializer_list<float> values) { return *(values.begin() + rng() % values.size()); }

// A random container: 1-3 linkages (a position element first, then a random choice of the others), 0-4 groups of triangle
// and line lists (and other kinds, skipped), some with bones, 0-3 transforms, 0-3 name pairs.
cIGeometryDataContainer *MakeContainer(std::mt19937 &rng, bool viz)
{
    auto *object = new SafeContainer;
    cIGeometryDataContainer *c = object;
    c->AddRef();
    const unsigned linkages = 1 + rng() % 3;
    const unsigned transforms = rng() % 4;
    std::vector<uint32_t> vertices(linkages);
    for (unsigned l = 0; l < linkages; ++l) {
        c->AddLinkage();
        vertices[l] = 1 + rng() % 9;
        c->SetLinkageReferencedSize(l, vertices[l]);
        auto add = [&](uint32_t identity, uint32_t repetition, uint32_t block) {
            const uint32_t index = c->AddElement(identity, repetition, block, 0);
            const uint32_t parts = block == 0 ? 1 : block == 1 ? 2 : block == 2 ? 3 : block == 3 ? 4 : 1;
            std::vector<uint32_t> words(vertices[l] * parts);
            for (uint32_t &x : words) {
                if (block == 4) {
                    x = (rng() % 256) << 8 | rng() % 4;
                } else {
                    const float f = Pick(rng, {-3.0f, -1.5f, 0.0f, 0.5f, 2.0f, 4.25f, 1.0f});
                    std::memcpy(&x, &f, 4);
                }
            }
            c->SetElementData(index, vertices[l], words.data(), uint32_t(words.size() * 4), nullptr, 0);
            c->AddLinkageElement(l, uint16_t(index));
        };
        add(kPositionId, 0, 2);
        if (rng() % 2) add(kNormalId, 0, 2);
        if (rng() % 2) add(kUvId, 0, 1);
        if (rng() % 4 == 0) add(kUvId, 1, 1);
        if (rng() % 3 == 0) add(kTangentId, 0, 2);
        if (rng() % 3 == 0) add(kBinormalId, 0, 2);
        // (bone data without any transform: the original reads a transform that is not there)
        if (rng() % 3 == 0 && transforms) add(kBoneIndexId, 0, 4);
        if (rng() % 4 == 0 && transforms) add(0xdcf2cfdc, 0, 4);
        if (rng() % 3 == 0) add(kBoneWeightId, 0, 3);
        if (rng() % 4 == 0) add(0x00abcdef, 0, 0);
    }
    for (unsigned t = 0; t < transforms; ++t) {
        static const float quats[4][4] = {{0, 0, 0, 1}, {0, 0, 0.70710677f, 0.70710677f}, {0.5f, 0.5f, 0.5f, 0.5f}, {1, 0, 0, 0}};
        cGeometryTransform transform;
        for (float &x : transform.mTranslation) x = Pick(rng, {0.0f, 1.0f, -2.0f, 0.5f});
        std::memcpy(transform.mRotation, quats[rng() % 4], 16);
        c->AddTransform(&transform);
    }
    // (with the visualization on, every linkage needs a format: the original reads a record's format unchecked)
    const unsigned groups = rng() % 5 + (viz ? linkages : 0);
    static const char *const kNames[] = {"sub0", "sub1", "elem0", "elem1", "", "alpha"};
    for (unsigned g = 0; g < groups; ++g) {
        static const uint32_t kinds[] = {0, 2, 2, 2, 1, 3};
        uint32_t prim = kinds[rng() % 6];
        uint32_t link = rng() % linkages;
        uint32_t count = prim == 0 ? 2 * (rng() % 4) : prim == 2 ? 3 * (rng() % 4) : rng() % 5;
        if (rng() % 6 == 0) ++count;
        if (viz && g < linkages) {
            prim = 2;
            link = g;
            count = 3;
        }
        std::vector<uint16_t> indices(count);
        for (uint16_t &x : indices) x = uint16_t(rng() % vertices[link]);
        c->AddGroup(prim, kNames[rng() % 6], link, int32_t(rng() % 7) - 2);
        if (rng() % 2) {
            // (group bones past the transforms are dropped from the element and its count)
            std::vector<uint16_t> bones(rng() % 5);
            for (uint16_t &b : bones) b = uint16_t(rng() % (transforms + 2));
            c->SetGroupVertexIndicesAndBones(g, indices.data(), count, bones.data(), uint32_t(bones.size()));
        } else {
            c->SetGroupVertexIndices(g, indices.data(), count);
        }
    }
    const unsigned pairs = rng() % 4;
    static const char *const kPairNames[] = {"nameA", "nameB", "alpha", ""};
    for (unsigned i = 0; i < pairs; ++i) c->AddNamePair(kPairNames[rng() % 4], kPairNames[rng() % 4]);
    // bounding meshes: the whole node's, or some of the transforms'
    auto mesh = [&] {
        Fake *f = &bw.meshes[rng() % 3];
        ++f->refs;
        return reinterpret_cast<cITriangleSetShape *>(f);
    };
    const unsigned meshes = rng() % 4;
    if (meshes == 1) {
        object->mBoundingMesh = mesh();
    } else if (meshes == 2) {
        object->mSubsetMeshes.resize(transforms);
        for (auto &m : object->mSubsetMeshes)
            if (rng() % 2) m.mMesh = mesh();
    }
    return c;
}

// What a build left on the node: the members through the class's layout (pointers to the node's own vectors only as
// present / absent: their addresses are the heap's), the device objects as fakes.
void NoteBuilt(const char *label, cIGeometryNode *n)
{
    cGeometryNode *g = AsNode(n);
    drawfx::Call c;
    c.note = label;
    auto add = [&](std::initializer_list<uint32_t> list) { c.words.insert(c.words.end(), list); };
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
    add({Ptr(g->mRefB8.mPtr), Ptr(g->mRefBC.mPtr), Ptr(g->mRefC0.mPtr), Ptr(g->mRefC4.mPtr)});
    add({g->mBoundsInfo, g->mBoundsCount, uint32_t(g->mWord94 != 0), uint32_t(g->mBoundsData != 0)});
    add({g->mRenderInfo[0], g->mRenderInfo[1], g->mRenderInfo[2], uint32_t(g->mRenderInfo[3] != 0),
         uint32_t(g->mRenderInfo[4] != 0)});
    c.words.insert(c.words.end(), g->mRenderInfo + 5, g->mRenderInfo + 11);
    EmitChunks(label, c.words);
}

void NoteDeviceBuffers()
{
    for (unsigned i = 0; i < kBufferCount; ++i) {
        drawfx::Call c;
        c.note = "buffer";
        c.words.push_back(i);
        c.words.push_back(bw.sizes[i]);
        const uint32_t used = bw.sizes[i] < kBufferBytes ? bw.sizes[i] : kBufferBytes;
        uint32_t h = 2166136261u;
        for (uint32_t k = 0; k < used; ++k) h = (h ^ bw.memory[i][k]) * 16777619u;
        c.words.push_back(h);
        c.words.push_back(uint32_t(bw.buffers[i].refs));
        drawfx::gLog.push_back(std::move(c));
    }
    drawfx::Note("mesh refs", {uint32_t(bw.meshes[0].refs), uint32_t(bw.meshes[1].refs), uint32_t(bw.meshes[2].refs)});
    drawfx::Note("format refs", {uint32_t(bw.formats[0].refs), uint32_t(bw.formats[1].refs), uint32_t(bw.formats[2].refs),
                                 uint32_t(bw.formats[3].refs), uint32_t(bw.formats[4].refs), uint32_t(bw.formats[5].refs),
                                 uint32_t(bw.formats[6].refs), uint32_t(bw.formats[7].refs)});
}

// 0xa419e0 for the real build: noted, and it succeeds (the build followed by the real reset is SG_GeometryNode_Render).
uint32_t __fastcall StandDeviceResetOk(cGeometryNode *self, void *, uint32_t a, uint32_t b)
{
    drawfx::Note("device reset", {Ptr(self), a & 0xff, b & 0xff});
    return 1;
}

} // namespace

DIFF_TEST(SG_GeometryNode_Factory, kFactory)
{
    Scope scope;
    sgfx::CheckSides(ctx, "factory", 8, Build, [&](bool original, std::mt19937 &) {
        cIGeometryNode *n = Maker(ctx, original)();
        drawfx::Note("count", CountOf(n));
        drawfx::AddRange(n, kSize, sgfx::kNodeTag);
        n->AddRef();
        drawfx::Note("connected as", w.graphics.value[0] - P(n));
        w.nodes[w.count++] = n;
        NoteAll("new node");
        ReleaseAll();
    });
    w.manager.Drop();
}

// The constructor as a base part (most-derived flag 1 and 0) on a zeroed buffer, then the destructor.
DIFF_TEST(SG_GeometryNode_Construct, 0xa3dd80)
{
    Scope scope;
    using Construct = void *(__fastcall *)(void *, void *, uint32_t);
    using Destruct = void(__fastcall *)(cGeometryNodeAdapters::Self *, void *);
    sgfx::CheckSides(ctx, "construct", 8, Build, [&](bool original, std::mt19937 &) {
        uint32_t vbptr = 0; // a derived constructor sets the vbptr before the base part's (flag 0)
        void *buffers[2] = {::operator new(kSize), ::operator new(kSize)}; // apart: a reused address keeps its old tag
        for (uint32_t flag = 1; flag + 1 > 0; --flag) {
            void *buffer = buffers[flag];
            std::memset(buffer, 0, kSize);
            if (!flag) static_cast<uint32_t *>(buffer)[0x30 / 4] = vbptr;
            drawfx::AddRange(buffer, kSize, sgfx::kNodeTag | (flag << 12));
            const Construct construct = original ? ctx.Original<Construct>(0xa3dd80)
                                                 : reinterpret_cast<Construct>(&QU_cGeometryNode_Constructor);
            void *r = construct(buffer, nullptr, flag);
            drawfx::Note("constructed", {Ptr(r), w.graphics.value[0] - P(buffer)});
            if (flag) vbptr = static_cast<uint32_t *>(buffer)[0x30 / 4];
            NoteWords("built", buffer);
            const Destruct destruct = original ? ctx.Original<Destruct>(0xa3e320) : &cGeometryNodeAdapters::Destruct;
            destruct(static_cast<cGeometryNode *>(buffer), nullptr);
            drawfx::Note("refs", {uint32_t(w.graphics.refs), uint32_t(w.settings.refs)});
        }
        sgfx::gPool.NoteCounts();
        ::operator delete(buffers[0]);
        ::operator delete(buffers[1]);
    });
    w.manager.Drop();
}

// Reset and ReleaseElementResources (hooked by RVA; the constructor and the device event reach them too) on nodes with
// built state.
DIFF_TEST(SG_GeometryNode_Helpers, 0xa3ee00)
{
    Scope scope;
    using Helper = void(__fastcall *)(cGeometryNodeAdapters::Self *, void *);
    sgfx::CheckSides(ctx, "helpers", 16, Build, [&](bool original, std::mt19937 &rng) {
        cIGeometryNode *n = Make(Maker(ctx, original));
        n->AddPart(reinterpret_cast<cIGeometryBuilder *>(&w.parts[0]));
        for (int k = 0, builds = 1 + rng() % 4; k < builds; ++k) n->Rebuild();
        NoteAll("built");
        const bool reset = rng() % 2;
        const Helper helper = original ? ctx.Original<Helper>(reset ? 0xa3ee00 : 0xa3ed70)
                                       : reset ? &cGeometryNodeAdapters::Reset
                                               : &cGeometryNodeAdapters::ReleaseElementResources;
        helper(AsNode(n), nullptr);
        NoteAll(reset ? "reset" : "released");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_GeometryNode_Slots, kFactory)
{
    Scope scope;
    sgfx::CheckSides(ctx, "slots", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const Factory make = Maker(ctx, original);
        const unsigned count = 1 + rng() % 3;
        for (unsigned i = 0; i < count; ++i) Make(make);
        NoteAll("made");
        for (int k = 0; k < 24; ++k) Operate(ctx, k, rng);
        NoteAll("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_GeometryNode_Persist, 0xa42bb0)
{
    Scope scope;
    sgfx::CheckSides(ctx, "persist", kSeeds * 2, Build, [&](bool original, std::mt19937 &rng) {
        const Factory make = Maker(ctx, original);
        cIGeometryNode *n = Make(make);
        // Some state before the read: a part, a name, a listener.
        if (rng() % 2) n->AddPart(reinterpret_cast<cIGeometryBuilder *>(&w.parts[4]));
        if (rng() % 2) Vbase(n)->AssignAndSubmit("lamp_gmnd", nullptr);
        if (rng() % 2) n->AddListener(reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&w.listeners[0]));

        sgfx::Reader &r = sgfx::gReader;
        const uint32_t version = 1 + rng() % 12;
        r.Version(version);
        r.Version(4); // the object-graph part
        const uint32_t extensions = rng() % 2;
        r.Word(extensions);
        if (extensions) r.Ref(&w.ext);
        r.Text(rng() % 2 ? "alpha" : "");
        r.Version(rng() % 3); // the cSGResource part
        r.Text(rng() % 3 ? "lamp_gmnd" : "");
        if (version > 6) r.Word(rng() % 2);
        if (version == 6) r.Word(rng() % 2).Word(rng() % 2).Word(rng() % 2);
        if (version < 9) {
            const uint32_t parts = rng() % 3;
            r.Word(parts);
            for (uint32_t i = 0; i < parts; ++i) r.Ref(&w.parts[rng() % 4]);
        }
        r.Ref(&w.containers[rng() % 4]); // read from version 5 on (5-7: with a name and a segment)
        if (rng() % 2)
            PersistFace(n)->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        else
            Vbase(n)->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
        NoteAll("read");
        r.NoteNames();
        NoteResource(Vbase(n));
        if (rng() % 2)
            PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            Vbase(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        NoteAll("written");
        ReleaseAll();
    });
    w.manager.Drop();
}

// Slots 42 / 44: the ranges over the subset map (the iterator objects: the game's on the original side, ours on ours).
// Each iterator's words past the vtable are noted (the count and the map / node it points at; the vtable is each side's
// own class, so it is not compared), then a clone of begin walks to end through the iterator slots (Clone, IsEqual, Get,
// Next, the pool-freeing Release), noting each name's text or each part, and the range is released.
void NoteIterator(const char *label, cIGZUnknown *it, unsigned words)
{
    drawfx::Call c;
    c.note = label;
    if (it)
        for (unsigned k = 1; k < words; ++k) { const uint32_t word = reinterpret_cast<const uint32_t *>(it)[k]; c.words.push_back(k == 1 ? word : Ptr(word)); }
    else
        c.words.push_back(0);
    drawfx::gLog.push_back(std::move(c));
}

void WalkRange(const cGeometrySubsetRange &r, bool names)
{
    auto *begin = reinterpret_cast<cIGeometrySubsetIterator *>(r.mBegin);
    auto *end = reinterpret_cast<cIGeometrySubsetIterator *>(r.mEnd);
    if (!begin || !end) return;
    cIGeometrySubsetIterator *it = begin->Clone();
    NoteIterator("clone", it, names ? 4 : 3);
    if (!it) return;
    drawfx::Note("clone addref", it->AddRef());
    drawfx::Note("begin is end", begin->IsEqual(end));
    for (int k = 0; k < 12 && !it->IsEqual(end); ++k) {
        const uint32_t value = it->Get();
        drawfx::Call c;
        c.note = names ? "name" : "part";
        if (names && value) c.text = std::string(reinterpret_cast<const char *>(value));
        else c.words.push_back(value);
        drawfx::gLog.push_back(std::move(c));
        it->Next();
    }
    drawfx::Note("clone at end", it->IsEqual(end));
    drawfx::Note("clone released", it->Release());
}

void NoteRange(const char *label, const cGeometrySubsetRange &r, unsigned words)
{
    drawfx::Note(label, {Ptr(r.mCursor), Ptr(r.mBegin), Ptr(r.mEnd)});
    NoteIterator("begin", r.mBegin, words);
    NoteIterator("end", r.mEnd, words);
    WalkRange(r, words == 4);
    for (cIGZUnknown *it : {r.mCursor, r.mBegin, r.mEnd})
        if (it) drawfx::Note("iterator released", it->Release());
}

// The iterators' pools (cRZFastFixedAllocator2 instances: the name iterators' at 0x10f6024, the part iterators' at
// 0x10f6030, each a pointer to its current page) point at a test page each: 0x800-aligned (a block's page is its address
// with the low 11 bits cleared), header +8 the free list, +0xc the used count, set far from empty and from full so
// neither side reaches the page bookkeeping (TakeFreePage, ReleaseEmptyPage, ReturnFullPage: game code not under test).
struct IteratorPools {
    static constexpr uint32_t kPools[2] = {0x10f6024, 0x10f6030};
    static constexpr uint32_t kBlocks[2] = {0x10, 0xc};
    static constexpr uint32_t kPage = 0x800, kTags[2] = {0x7b000000, 0x7b100000};
    uint8_t *pages[2];
    uint32_t saved[2];
    IteratorPools()
    {
        for (int i = 0; i < 2; ++i) {
            pages[i] = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
            saved[i] = *GameAddress<uint32_t>(kPools[i]);
            *GameAddress<uint32_t>(kPools[i]) = P(pages[i]);
        }
    }
    ~IteratorPools()
    {
        for (int i = 0; i < 2; ++i) {
            *GameAddress<uint32_t>(kPools[i]) = saved[i];
            _aligned_free(pages[i]);
        }
    }
    // Before each side: the same free list on both sides, the pages registered.
    void Reset()
    {
        for (int i = 0; i < 2; ++i) {
            std::memset(pages[i], 0, kPage);
            uint32_t *header = reinterpret_cast<uint32_t *>(pages[i]);
            header[3] = 1000;
            uint32_t next = 0;
            for (uint32_t at = kPage - kBlocks[i]; at >= 0x10; at -= kBlocks[i]) {
                *reinterpret_cast<uint32_t *>(pages[i] + at) = next;
                next = P(pages[i] + at);
            }
            header[2] = next;
            drawfx::AddRange(pages[i], kPage, kTags[i]);
        }
    }
};

void NotePools(const IteratorPools &pools)
{
    for (int i = 0; i < 2; ++i) {
        const uint32_t *header = reinterpret_cast<const uint32_t *>(pools.pages[i]);
        drawfx::Note("pool", {Ptr(header[2]), header[3]});
    }
}

DIFF_TEST(SG_GeometryNode_Subsets, 0xa436a0)
{
    Scope scope;
    IteratorPools pools;
    const auto build = [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        pools.Reset();
    };
    sgfx::CheckSides(ctx, "subsets", 16, build, [&](bool original, std::mt19937 &rng) {
        cIGeometryNode *n = Make(Maker(ctx, original));
        n->AddPart(reinterpret_cast<cIGeometryBuilder *>(&w.parts[0]));
        const int builds = int(rng() % 4);
        for (int k = 0; k < builds; ++k) n->Rebuild();
        NoteAll("built");
        cGeometrySubsetRange names{};
        n->GetSubsetNames(&names);
        NoteRange("names", names, 4);
        const char *texts[] = {"sub0", "sub1", "none", ""};
        const char *text = texts[rng() % 4];
        cGeometrySubsetRange parts{};
        n->GetSubsetParts(&parts, text);
        NoteRange("parts", parts, 3);
        NotePools(pools);
        NoteAll("after");
        ReleaseAll();
    });
    w.manager.Drop();
}

// BuildGeometry itself: both sides run their own build over the same random containers (formats and device buffers are
// fakes with a layout model); the node's elements, subsets, records, name lists and device objects are compared, with
// the call log.
DIFF_TEST(SG_GeometryNode_Build, 0xa3f180)
{
    svcfx::AllocatorScope allocator;
    sgfx::RegistryScope registry{kRegistry};
    MissingRecordScope missing;
    shape::StandIn reset{0xa419e0, &cGeometryNode::ResetDeviceState, &StandDeviceResetOk};
    shape::StandIn registryStand{0xa42f30, &cGeometryNodeBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kRegistry.map>};
    using BuildFn = void(__fastcall *)(cGeometryNodeAdapters::Self *, void *, cIGeometryDataContainer *);
    sgfx::CheckSides(ctx, "build", 96, BuildReal, [&](bool original, std::mt19937 &rng) {
        cIGeometryNode *n = Make(Maker(ctx, original));
        const BuildFn build = original ? ctx.Original<BuildFn>(0xa3f180) : &cGeometryNodeAdapters::BuildGeometry;
        const unsigned rounds = 1 + rng() % 2;
        // the visualization elements: flag bit 1 of the node's flags word (+0x124)
        const bool viz = rng() % 3 == 0;
        if (viz) *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(AsNode(n)) + 0x124) |= 2;
        for (unsigned k = 0; k < rounds; ++k) {
            std::mt19937 source(rng());
            cIGeometryDataContainer *c = rng() % 16 == 0 ? nullptr : MakeContainer(source, viz);
            build(AsNode(n), nullptr, c);
            NoteBuilt("built", n);
            NoteNode("node", n);
            if (c) c->Release();
        }
        ReleaseAll();
        NoteDeviceBuffers();
    });
    w.manager.Drop();
}

// ---- the render face (part 6) -----------------------------------------------------------------------------------------
//
// IsSupported 0xa41ee0, Prepare 0xa429b0, Render 0xa435d0 and ResetDeviceState 0xa419e0 on nodes the real build made
// (both sides' own build and device reset, nothing stood in), over the devfx device fakes: the device, state, declaration,
// state-block, format and vertex-array call streams are compared, with the node's members after each step and the fakes'
// references at the end (milestone O acceptance (a), the first device call stream). The device format lookup 0x18daa0,
// the format variant 0x18d990 and the vertex copy 0xc627e0 (game code, not ours) are stood in on both sides and noted.
namespace {

devfx::World dw;
Fake *gNoDeviceFormat = nullptr; // 0x18daa0 finds no device format for this one

unsigned FormatIndex(const void *format)
{
    for (unsigned i = 0; i < 8; ++i)
        if (format == &bw.formats[i]) return i;
    return 8;
}

bool __cdecl StandDeviceFormat(void **out, void *device, void *format)
{
    drawfx::Note("device format", {Ptr(P(device)), Ptr(P(format))});
    if (format == gNoDeviceFormat) return false;
    *out = format;
    if (FormatIndex(format) < 8) ++static_cast<Fake *>(format)->refs;
    return true;
}

void *__cdecl StandFormatVariant(void **out, void *format, uint32_t a, uint32_t b)
{
    drawfx::Note("format variant", {Ptr(P(format)), a, b});
    Fake &variant = bw.formats[(FormatIndex(format) + 4) % 8];
    ++variant.refs;
    *out = &variant;
    return out;
}

void *__cdecl StandCopy(void *stream0, uint32_t source, void *sourceFormat, uint32_t sourceFlag, void *stream1, uint32_t start,
                        void *deviceFormat, uint32_t deviceFlag, uint32_t count)
{
    drawfx::Note("copy vertices", {Ptr(P(stream0)), source, Ptr(P(sourceFormat)), sourceFlag, Ptr(P(stream1)), start,
                                   Ptr(P(deviceFormat)), deviceFlag, count});
    return stream0;
}

uint32_t RenderGetDevice(Fake &, const uint32_t *) { return P(&dw.device); }

void RenderReal(uint32_t seed, drawfx::Cells &cells)
{
    BuildReal(seed, cells);
    std::mt19937 rng(seed * 7919u + 13u);
    dw.Reset(rng, 800);
    dw.Plant(cells);
    w.graphics.On(24, &RenderGetDevice);
    w.graphics.value[1] = seed % 5 ? 0 : 0xffffffffu;
    for (unsigned i = 0; i < kBufferCount; ++i) drawfx::AddRange(bw.memory[i], kBufferBytes, 0x79000000u | i << 20);
    gNoDeviceFormat = nullptr;
}

bool HasDeclarations(const cGeometryNode *g)
{
    for (const cGeometryNodeElement &e : g->mElements)
        if (!e.mRefs[2].mPtr || !e.mRefs[3].mPtr) return false;
    return true;
}

void NoteRenderNode(const char *label, cIGeometryNode *n)
{
    const cGeometryNode *g = AsNode(n);
    drawfx::Note(label, {uint32_t(g->mDeviceReady), uint32_t(g->mResourcesReady)});
    NoteBuilt(label, n);
}

} // namespace

DIFF_TEST(SG_GeometryNode_Render, 0xa41ee0)
{
    svcfx::AllocatorScope allocator;
    sgfx::RegistryScope registry{kRegistry};
    MissingRecordScope missing;
    shape::StandIn registryStand{0xa42f30, &cGeometryNodeBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kRegistry.map>};
    // The format helpers are ours too (tests/sg_geometry_formats.cpp): stood in over the image's and over ours.
    shape::StandIn deviceFormat{0x18daa0, &MakeDeviceVertexFormat, &StandDeviceFormat};
    shape::StandIn variant{0x18d990, &MakeStreamVertexFormat, &StandFormatVariant};
    shape::StandIn copy{0xc627e0, &CopyVertexStream, &StandCopy};
    using BuildFn = void(__fastcall *)(cGeometryNodeAdapters::Self *, void *, cIGeometryDataContainer *);
    using ResetFn = bool(__fastcall *)(cGeometryNodeAdapters::Self *, void *, uint32_t, uint32_t);
    sgfx::CheckSides(ctx, "render", 256, RenderReal, [&](bool original, std::mt19937 &rng) {
        cIGeometryNode *n = Make(Maker(ctx, original));
        cGeometryNode *g = AsNode(n);
        const BuildFn build = original ? ctx.Original<BuildFn>(0xa3f180) : &cGeometryNodeAdapters::BuildGeometry;
        const ResetFn reset = original ? ctx.Original<ResetFn>(0xa419e0) : &cGeometryNodeAdapters::ResetDeviceState;
        {
            std::mt19937 source(rng());
            cIGeometryDataContainer *c = MakeContainer(source, false);
            build(g, nullptr, c);
            c->Release();
        }
        NoteRenderNode("built", n);
        cIGeometryRender *r = RenderFace(n);
        const unsigned rounds = 2 + rng() % 5;
        for (unsigned k = 0; k < rounds; ++k) {
            switch (rng() % 12) {
            case 0: dw.Lose(); break;
            case 1: dw.refuse = &bw.formats[rng() % 8]; break;
            case 2: dw.refuse = nullptr; break;
            case 3: gNoDeviceFormat = rng() % 2 ? &bw.formats[rng() % 8] : nullptr; break;
            case 4: dw.ready = rng() % 3 != 0; break;
            case 5: dw.caps_ = rng() % 16; break;
            case 6: g->mDeviceReady = 0; break;
            case 7: g->mResourcesReady = 0; break;
            case 8: // (the +0x35 byte next to the prepared flag: the original stores only the flag's byte)
                if (!g->mElements.empty()) g->mElements[rng() % g->mElements.size()].mPrepared |= uint16_t(rng() % 255 + 1) << 8;
                break;
            default: break;
            }
            const unsigned op = rng() % 4;
            if (op == 0) {
                uint32_t a = rng() % 3 == 0 ? 0x100 : rng() % 2;
                const uint32_t b = rng() % 3 == 0 ? 0x200 : rng() % 2;
                // (state blocks without declarations only once the declarations exist: both read them unchecked)
                if ((b & 0xff) && !(a & 0xff) && !HasDeclarations(g)) a |= 1;
                drawfx::Note("reset device state", reset(g, nullptr, a, b));
            } else if (op == 1 || g->mElements.empty()) {
                const uint32_t what = rng() % 4 == 0 ? 0x100 : rng() % 3;
                drawfx::Note("is supported", r->IsSupported(what));
            } else if (op == 2) {
                const uint32_t index = uint32_t(rng() % g->mElements.size());
                dw.target.value[0] = P(&bw.formats[rng() % 8]);
                // (a full array now and then: the capacity check's failure)
                for (Fake &v : dw.vertices) v.value[0] = rng() % 3 == 0 ? rng() % 0x40 : 0x100000;
                const uint32_t pick = rng() % 4;
                // (a set low byte other than 1 too: the original tests the whole byte; coordinator mutant)
                const uint32_t flag = pick == 0 ? 0x100 : pick == 1 ? 0 : pick == 2 ? 1 : 0x82;
                drawfx::Note("prepare", r->Prepare(index, P(&dw.context), P(&dw.target), flag));
            } else {
                const uint32_t index = uint32_t(rng() % g->mElements.size());
                const uint8_t hardware = uint8_t(rng() % 2);
                const uint32_t flag = rng() % 3 == 0 ? 0x100 : rng() % 2;
                drawfx::Note("render", r->Render(index, &dw.context, hardware, flag));
            }
            NoteRenderNode("after", n);
        }
        ReleaseAll();
        NoteDeviceBuffers();
        dw.NoteRefs();
    });
    w.manager.Drop();
}
