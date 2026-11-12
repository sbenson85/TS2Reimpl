// Differential tests of the lot imposter and its desc (src/sg/cLotImposter.*; lane 261108j, scenegraph row LT2a).
//
//   LotImposter_Desc   a desc made by the original factory 0x36d260 and one made by ours, each driven through its own
//       vtables alike: QueryInterface on every face (interface 1, the desc's, cIGZSerializable's, the persist resource's,
//       a random one; a null out), AddRef / Release on every face (the count kept above 0), GetValue / SetValue, Write and
//       Read on stream fakes (their results random), GetGZCLSID, the persist resource slots 5-11 (type, group, instance,
//       count), then the end: the last Release, or the deleting destructor (slot 12 of the +8 face) with flags 1 or 0
//       (the delete recorded: block, size). Every word but the vtables noted after each operation.
//   LotImposter_Imposter  the same for an imposter (factories 0x36d0a0 and ours): QueryInterface on both faces, AddRef /
//       Release, Unload, the getters (slots 5-7), HasImposter, AttachToTerrain (0xaed420 stood in for, its arguments
//       noted) with the node and desc set to fakes between operations (the desc's value random), then the last Release
//       or the deleting destructor (slot 3).
//   LotImposter_Load   Load (slot 3) on both imposters, its callees stood in for on both sides: GetLotGroup 0x401790 (no
//       lot sometimes), LoadImposterResources 0xb40560 (the keys noted; the scene node given or not, its result random),
//       ResolveResourceName 0xae7340 and ParseKey 0xae6e60 (the path without a key list), the resource manager 0x20deb5
//       (its slot 5 handing out the key list or not, slot 3 the scene node or the desc by interface), the design mode
//       node's Set 0x3f7e90, ApplySeason 0x3f8a10 and ApplyDesignModesToTree 0x3f8990; the option mask (the Seasons bit or
//       not) and the scene graph system (its season manager or none) set. The node's slot 44 ranges: each filter probed
//       through its own slot 0 on a probe node (its IsKindOf and GetTag answers random), then three iterator fakes over
//       random elements (shape reference faces or not; objects). The node and desc before Load are fakes or null.
#include "code_patch.h"
#include "sgfx.h"

#include "app/AppInit.h"
#include "rz/StringKeyParser.h"
#include "sg/SGUtilities.h"
#include "sg/cLotImposter.h"
#include "sg/cLotImposterDesignModes.h"
#include "sg/cTextureManager.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using nTSSG::cDesignModeNode;
using nTSSG::cLotImposter;
using nTSSG::cLotImposterDesc;
using nTSSG::ImposterKey;
using nTSSG::ImposterKeyRange;

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

constexpr At kOStreamAt[] = {{7, {1, Ret::Int, true, "SetUint16", "u"}}, {9, {1, Ret::Int, true, "SetUint32", "u"}}};
constexpr auto kOStreamSlots = Table<10>(kOStreamAt);
constexpr drawfx::Iface kOStream{"OStream", 10, kOStreamSlots.data(), false, drawfx::kTestTable};

constexpr At kIStreamAt[] = {{7, {1, Ret::Int, true, "GetUint16", "a"}}, {9, {1, Ret::Int, true, "GetUint32", "a"}}};
constexpr auto kIStreamSlots = Table<10>(kIStreamAt);
constexpr drawfx::Iface kIStream{"IStream", 10, kIStreamSlots.data(), false, drawfx::kTestTable};

constexpr At kDescAt[] = {{3, {0, Ret::Int, true, "GetValue", ""}}};
constexpr auto kDescSlots = Table<5>(kDescAt);
constexpr drawfx::Iface kDesc{"LotImposterDesc", 5, kDescSlots.data(), false, drawfx::kTestTable};

constexpr At kNodeAt[] = {{25, {1, Ret::Int, true, "IsKindOf", "u"}},
                          {26, {0, Ret::Int, true, "GetTag", ""}},
                          {44, {3, Ret::Int, true, "MakeIterator44", "aua"}}};
constexpr auto kNodeSlots = Table<62>(kNodeAt);
constexpr drawfx::Iface kNode{"ResourceNode", 62, kNodeSlots.data(), false, drawfx::kTestTable};

constexpr At kShapeRefAt[] = {{77, {1, Ret::Int, true, "AddLayer", "s"}}};
constexpr auto kShapeRefSlots = Table<96>(kShapeRefAt);
constexpr drawfx::Iface kShapeRef{"ShapeRefNode", 96, kShapeRefSlots.data(), false, drawfx::kTestTable};

constexpr At kIterAt[] = {{4, {1, Ret::Int, true, "IsEqual", "a"}},
                          {5, {0, Ret::Int, true, "Get", ""}},
                          {6, {0, Ret::Int, true, "Next", ""}}};
constexpr auto kIterSlots = Table<7>(kIterAt);
constexpr drawfx::Iface kIter{"SceneNodeIterator", 7, kIterSlots.data(), false, drawfx::kTestTable};

constexpr At kResourcesAt[] = {{3, {5, Ret::Int, true, "GetResource", "W4uauu"}},
                               {5, {5, Ret::Int, true, "GetResourceList", "W4uauu"}}};
constexpr auto kResourcesSlots = Table<6>(kResourcesAt);
constexpr drawfx::Iface kResources{"ResourceManager", 6, kResourcesSlots.data(), false, drawfx::kTestTable};

constexpr At kListAt[] = {{7, {0, Ret::Int, true, "GetKeys", ""}}};
constexpr auto kListSlots = Table<8>(kListAt);
constexpr drawfx::Iface kList{"KeyList", 8, kListSlots.data(), false, drawfx::kTestTable};

constexpr At kSystemAt[] = {{66, {0, Ret::Int, true, "GetSeasonManager", ""}}};
constexpr auto kSystemSlots = Table<67>(kSystemAt);
constexpr drawfx::Iface kSystem{"SGSystem", 67, kSystemSlots.data(), false, drawfx::kTestTable};

constexpr At kPlainAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kPlainSlots = Table<3>(kPlainAt);
constexpr drawfx::Iface kPlain{"Plain", 3, kPlainSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kSystemCell = 0x10cc458;
constexpr uint32_t kResourceNodeIid = 0xe519c92c;
constexpr uint32_t kShapeRefNodeIid = 0xa516cd10;
constexpr int kElems = 5;
constexpr int kKeys = 6;

// ---- calls through an object's own vtable ----
template <class R, class... A> R VCall(void *face, unsigned slot, A... a)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(face))[slot])(face, a...);
}
uint32_t Word(const void *object, uint32_t offset) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(object) + offset); }

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }

// ======================================== the world ========================================

struct World {
    Fake ostream, istream, desc[2], node[2], terrain, probe;
    Fake resources, list, system, seasons, scene, loadedDesc, modelNode;
    Fake elems[2][kElems], refs[kElems], iters[2][3];
    int counts[2] = {};
    int pos[2] = {};
    int ranges = 0;
    uint32_t streamLcg = 1;
    bool lotFound = true, listFound = true, sceneFound = true, descFound = true, resourcesLoaded = true, giveNode = true;
    bool setResults[kElems] = {};
    uint32_t group = 0;
    ImposterKey keys[kKeys] = {};
    ImposterKeyRange range = {};
    uint8_t *object = nullptr; // the imposter under test (its start), for noting pointers into it
    uint32_t Next()
    {
        streamLcg = streamLcg * 1664525u + 1013904223u;
        return streamLcg >> 8;
    }
};
World gw;

uint32_t StreamResult(Fake &, const uint32_t *) { return gw.Next() % 5 != 0; }
uint32_t GetUint16(Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint16_t *>(uintptr_t(a[0])) = uint16_t(gw.Next());
    return gw.Next() % 5 != 0;
}
uint32_t GetUint32(Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gw.Next() % 3 == 0 ? 0 : gw.Next();
    return gw.Next() % 5 != 0;
}

void *__cdecl AttachStand(void *node, void *terrain, const char *name)
{
    drawfx::Note("attach", {drawfx::Ptr(node), drawfx::Ptr(terrain)});
    sgfx::NoteText("attach name", name);
    return nullptr;
}

// ---- Load's stand-ins ----
bool __cdecl LotGroupStand(uint32_t lot, uint32_t *group)
{
    drawfx::Note("lot group", lot);
    if (!gw.lotFound) return false;
    *group = gw.group;
    return true;
}
bool __cdecl LoadResourcesStand(const ImposterKeyRange *keys, void **node)
{
    drawfx::Note("load resources", {uint32_t(keys == &gw.range), uint32_t(keys->mEnd - keys->mBegin),
                                    uint32_t(reinterpret_cast<uint8_t *>(node) - gw.object), drawfx::Ptr(*node)});
    if (gw.giveNode) {
        ++gw.scene.refs;
        *node = &gw.scene;
    }
    return gw.resourcesLoaded;
}
bool __cdecl ResolveStand(cIGZString *out, const char *text, uint32_t group, const char *groupName)
{
    drawfx::Note("resolve", {group, drawfx::P(groupName)});
    sgfx::NoteText("resolve text", text);
    sgfx::NoteText("resolve out before", out->ToChar());
    const std::string name = std::string(text) + "@" + std::to_string(group);
    out->FromChar(name.c_str());
    return true;
}
bool __cdecl ParseStand(StringKeyParser::Result *out, const char *key, uint32_t preset, const char *ext)
{
    drawfx::Note("parse", {preset, drawfx::P(ext), out->value, out->id, out->hash24, out->crc32});
    sgfx::NoteText("parse key", key);
    uint32_t h = 2166136261u;
    for (const char *p = key; *p; ++p) h = (h ^ uint8_t(*p)) * 16777619u;
    *out = {preset, h, h >> 3, h ^ 0x5a5a5a5a};
    return true;
}
void *__cdecl ResourcesStand()
{
    drawfx::Note("resource manager", 0);
    return &gw.resources;
}
bool __fastcall SetStand(cDesignModeNode *self, void *, void *object)
{
    int i = 0;
    while (i < kElems && &gw.elems[1][i] != object) ++i;
    drawfx::Note("design modes set", {drawfx::Ptr(object), drawfx::P(self->mNode.mNode), uint32_t(self->mModes.mModes.size())});
    const bool r = i < kElems && gw.setResults[i];
    if (r) {
        ++gw.modelNode.refs;
        self->mNode.mNode = reinterpret_cast<nGZSceneGraph::cIResourceNode *>(&gw.modelNode);
    }
    return r;
}
void __fastcall SeasonStand(cDesignModeNode *self, void *, void *seasons)
{
    drawfx::Note("design modes season", {drawfx::Ptr(seasons), drawfx::Ptr(self->mNode.mNode)});
}
void __fastcall TreeStand(cDesignModeNode *self, void *)
{
    drawfx::Note("design modes tree", drawfx::Ptr(self->mNode.mNode));
}

uint32_t GetResource(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (a[1] == kResourceNodeIid && gw.sceneFound) {
        ++gw.scene.refs;
        *out = &gw.scene;
        return 1;
    }
    if (a[1] == nTSSG::cILotImposterDesc::kIid && gw.descFound) {
        ++gw.loadedDesc.refs;
        *out = &gw.loadedDesc;
        return 1;
    }
    return 0;
}
uint32_t GetResourceList(Fake &, const uint32_t *a)
{
    if (!gw.listFound) return 0;
    ++gw.list.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.list;
    return 1;
}

// The range's filter probed through its own slot 0, then {cursor, begin, end} written (each counted).
uint32_t MakeIterator(Fake &, const uint32_t *a)
{
    const int r = gw.ranges++ % 2;
    void *const filter = reinterpret_cast<void *>(uintptr_t(a[2]));
    const uint32_t included = VCall<uint32_t>(filter, 0, static_cast<void *>(&gw.probe));
    drawfx::Note("filter", {r == 0 ? Word(filter, 4) : 0u, included});
    gw.pos[r] = 0;
    auto *const out = reinterpret_cast<void **>(uintptr_t(a[0]));
    for (int k = 0; k < 3; ++k) {
        ++gw.iters[r][k].refs;
        out[k] = &gw.iters[r][k];
    }
    return a[0];
}
int RangeOf(const Fake &self)
{
    for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 3; ++k)
            if (&gw.iters[r][k] == &self) return r;
    return 0;
}
uint32_t IterIsEqual(Fake &self, const uint32_t *a)
{
    const int r = RangeOf(self);
    const bool selfEnd = &self == &gw.iters[r][2], otherEnd = a[0] == drawfx::P(&gw.iters[r][2]);
    const int here = selfEnd ? gw.counts[r] : gw.pos[r], there = otherEnd ? gw.counts[r] : gw.pos[r];
    return here == there;
}
uint32_t IterGet(Fake &self, const uint32_t *)
{
    const int r = RangeOf(self);
    return gw.pos[r] < gw.counts[r] ? drawfx::P(&gw.elems[r][gw.pos[r]]) : 0;
}
uint32_t IterNext(Fake &self, const uint32_t *)
{
    ++gw.pos[RangeOf(self)];
    return 0;
}
uint32_t GetKeys(Fake &, const uint32_t *) { return drawfx::P(&gw.range); }

void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 g(seed * 2246822519u + 3u);
    gw.streamLcg = seed * 2654435761u + 11u;
    gw.ostream.Reset<kOStream>(700);
    gw.istream.Reset<kIStream>(701);
    gw.ostream.On(7, &StreamResult);
    gw.ostream.On(9, &StreamResult);
    gw.istream.On(7, &GetUint16);
    gw.istream.On(9, &GetUint32);
    for (int i = 0; i < 2; ++i) {
        gw.desc[i].Reset<kDesc>(702 + i);
        gw.desc[i].Returns(3, g() % 3 == 0 ? 0u : uint32_t(g()));
        gw.node[i].Reset<kNode>(704 + i);
    }
    gw.terrain.Reset<kPlain>(706);
    gw.probe.Reset<kNode>(707);
    gw.probe.Returns(25, g() % 2);
    gw.probe.Returns(26, g() % 2 ? 0u : 0x40u);
    gw.resources.Reset<kResources>(708);
    gw.resources.On(3, &GetResource);
    gw.resources.On(5, &GetResourceList);
    gw.list.Reset<kList>(709);
    gw.list.On(7, &GetKeys);
    gw.system.Reset<kSystem>(710);
    gw.seasons.Reset<kPlain>(711);
    gw.system.Returns(66, g() % 4 == 0 ? nullptr : &gw.seasons);
    gw.scene.Reset<kNode>(712);
    gw.scene.On(44, &MakeIterator);
    gw.loadedDesc.Reset<kDesc>(713);
    gw.loadedDesc.Returns(3, g() % 2);
    gw.modelNode.Reset<kNode>(714);
    for (int k = 0; k < kElems; ++k) {
        gw.elems[0][k].Reset<kNode>(720 + k);
        gw.refs[k].Reset<kShapeRef>(730 + k);
        if (g() % 3 != 0) gw.elems[0][k].AddFace(kShapeRefNodeIid, gw.refs[k]);
        gw.elems[1][k].Reset<kPlain>(740 + k);
        gw.setResults[k] = g() % 3 != 0;
    }
    for (int r = 0; r < 2; ++r) {
        gw.counts[r] = int(g() % (kElems + 1));
        gw.pos[r] = 0;
        for (int k = 0; k < 3; ++k) {
            gw.iters[r][k].Reset<kIter>(750 + r * 3 + k);
            gw.iters[r][k].On(4, &IterIsEqual);
            gw.iters[r][k].On(5, &IterGet);
            gw.iters[r][k].On(6, &IterNext);
        }
    }
    gw.ranges = 0;
    gw.lotFound = g() % 6 != 0;
    gw.listFound = g() % 3 != 0;
    gw.sceneFound = g() % 4 != 0;
    gw.descFound = g() % 4 != 0;
    gw.resourcesLoaded = g() % 4 != 0;
    gw.giveNode = g() % 4 != 0;
    gw.group = g();
    const int keyCount = int(g() % (kKeys + 1));
    for (int k = 0; k < kKeys; ++k)
        gw.keys[k] = {Pick(g, {cLotImposterDesc::kType, 0x49596978, 0x1c4a276c, 0xe519c933, 0x12345678, cLotImposterDesc::kType}),
                      uint32_t(g()), uint32_t(g()), uint32_t(g() % 3)};
    gw.range = {gw.keys, gw.keys + keyCount};
    drawfx::AddRange(gw.keys, sizeof(gw.keys), 0x54000000);
    drawfx::AddRange(&gw.range, sizeof(gw.range), 0x55000000);
    cells.Set(kSystemCell, &gw.system);
    cells.Set(appinit::kOptionMaskRva, Pick(g, {0x80u, 0x1u, 0xffffffffu, 0x81u, 0x7fu}));
}

void NoteRefs()
{
    std::vector<uint32_t> w;
    for (Fake *f : {&gw.desc[0], &gw.desc[1], &gw.node[0], &gw.node[1], &gw.terrain, &gw.probe, &gw.resources, &gw.list,
                    &gw.system, &gw.seasons, &gw.scene, &gw.loadedDesc, &gw.modelNode})
        w.push_back(uint32_t(f->refs));
    for (int k = 0; k < kElems; ++k) w.push_back(uint32_t(gw.elems[0][k].refs + 100 * gw.elems[1][k].refs + 10000 * gw.refs[k].refs));
    for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 3; ++k) w.push_back(uint32_t(gw.iters[r][k].refs));
    drawfx::Call c;
    c.note = "refs";
    c.words = w;
    drawfx::gLog.push_back(std::move(c));
}

// ======================================== desc ========================================

void NoteDesc(uint8_t *p)
{
    std::vector<uint32_t> w;
    for (uint32_t off = 0xc; off < 0x28; off += 4) w.push_back(Word(p, off));
    drawfx::Call c;
    c.note = "desc words";
    c.words = w;
    drawfx::gLog.push_back(std::move(c));
}

void QueryNote(uint8_t *p, void *face, uint32_t iid, bool nullOut)
{
    void *out = reinterpret_cast<void *>(uintptr_t(0x5eed5eed));
    const uint32_t r = VCall<uint32_t>(face, 0, iid, nullOut ? nullptr : &out) & 0xff;
    const uint32_t where = out == reinterpret_cast<void *>(uintptr_t(0x5eed5eed)) ? 0xffffu : uint32_t(static_cast<uint8_t *>(out) - p);
    drawfx::Note("query", {iid, r, where});
}

// The end of an object: its last Release, or its deleting destructor (`dtorSlot` of `dtorFace`) with flags 1 or 0.
void End(uint8_t *p, void *dtorFace, unsigned dtorSlot, uint32_t countOffset, std::mt19937 &g, shape::DeleteRecorder &deletes,
         void *const *faces, int faceCount, uint32_t size)
{
    const unsigned how = g() % 4;
    deletes.Begin();
    uint32_t last = 0;
    if (how < 2) {
        // Release answers 0 when it deletes (the count is left at 1 by cRZUnknown's, so the block is not read again).
        for (unsigned n = 0; n < 64; ++n)
            if ((last = VCall<uint32_t>(faces[g() % faceCount], 2)) == 0) break;
    } else {
        VCall<void *>(dtorFace, dtorSlot, uint32_t(how == 2 ? 1 : 0));
    }
    deletes.End();
    drawfx::Note("end", {how < 2 ? 0u : how, last, Word(p, countOffset), uint32_t(deletes.Count()),
                         deletes.Count() ? uint32_t(static_cast<uint8_t *>(deletes.At(0).block) - p) : 0xffffu,
                         deletes.Count() ? deletes.At(0).size : 0xffffu});
    const bool freed = deletes.Count() > 0;
    deletes.Release();
    if (!freed) std::free(p);
    (void)size;
}

using Factory = void *(__cdecl *)();

} // namespace

// Verifies: 0x36d260 (the factory; ours is the hook's), 0x36d160 / 0x36d1f0 (constructor / destructor), the deleting
// destructor 0x36d220 and thunk 0x36d20b, QueryInterface 0x36d2d0 and thunks 0x36d326 / 0x36d32e, AddRef 0x36d240 and
// thunks 0x36d248 / 0x36d250, Release 0x36d370 and thunks 0x36d378 / 0x36d380, Write 0x36d390, Read 0x36d340,
// GetGZCLSID 0x36d2c0.
DIFF_TEST(LotImposter_Desc, 0x36d260)
{
    shape::DeleteRecorder deletes;
    unsigned ends = 0;
    sgfx::CheckSides(
        ctx, "lot imposter desc", 1500, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
        [&](bool original, std::mt19937 &g) {
            uint8_t *const p = static_cast<uint8_t *>(original ? ctx.Original<Factory>(0x36d260)()
                                                               : static_cast<void *>(cLotImposterDesc::Create()));
            if (!p) {
                ctx.Fail("lot imposter desc: no object");
                return;
            }
            void *const faces[3] = {p, p + 4, p + 8};
            NoteDesc(p);
            const unsigned steps = g() % 16;
            for (unsigned s = 0; s < steps; ++s) {
                switch (g() % 12) {
                case 0: case 1:
                    QueryNote(p, faces[g() % 3], Pick(g, {1u, 0xed8b648bu, 0xe4fda3d4u, 0x456b8f1du, uint32_t(g())}),
                              g() % 10 == 0);
                    break;
                case 2: drawfx::Note("addref", VCall<uint32_t>(faces[g() % 3], 1)); break;
                case 3:
                    if (Word(p, 0xc) > 1) drawfx::Note("release", VCall<uint32_t>(faces[g() % 3], 2));
                    break;
                case 4: drawfx::Note("get value", VCall<uint32_t>(p, 3)); break;
                case 5: drawfx::Note("set value", VCall<uint32_t>(p, 4, uint32_t(g() % 3 ? g() : 0))); break;
                case 6: drawfx::Note("write", VCall<uint32_t>(p + 4, 3, static_cast<void *>(&gw.ostream)) & 0xff); break;
                case 7: drawfx::Note("read", VCall<uint32_t>(p + 4, 4, static_cast<void *>(&gw.istream)) & 0xff); break;
                case 8: drawfx::Note("clsid", VCall<uint32_t>(p + 4, 5)); break;
                case 9: {
                    const unsigned slot = 5 + g() % 4; // GetType, GetGroup, GetInstance, ... GetRefCount (11)
                    drawfx::Note("persist get", {slot, VCall<uint32_t>(p + 8, slot == 8 ? 11 : slot)});
                    break;
                }
                default: {
                    const unsigned slot = 8 + g() % 3; // SetType, SetGroup, SetInstance
                    drawfx::Note("persist set", {slot, VCall<uint32_t>(p + 8, slot, uint32_t(g())) & 0xff});
                }
                }
                NoteDesc(p);
            }
            End(p, p + 8, 12, 0xc, g, deletes, faces, 3, 0x28);
            ends += !original;
            NoteRefs();
            ++ctx.cases;
        });
    if (ends == 0) ctx.Fail("lot imposter desc: nothing ended");
}

// Verifies: 0x36d0a0 (the factory; ours is the hook's), 0x36c9d0 / 0x36ca30 (constructor / destructor), the deleting
// destructor 0x36caa0, QueryInterface 0x36d110 and thunk 0x36d14f, Unload 0x36d090, HasImposter 0x36cac0,
// AttachToTerrain 0x36cae0.
DIFF_TEST(LotImposter_Imposter, 0x36d0a0)
{
    shape::DeleteRecorder deletes;
    shape::StandIn attach(0xaed420, &nGZSceneGraph::ForEachShapeBind, &AttachStand);
    sgfx::CheckSides(
        ctx, "lot imposter", 1500, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
        [&](bool original, std::mt19937 &g) {
            uint8_t *const face = static_cast<uint8_t *>(original ? ctx.Original<Factory>(0x36d0a0)()
                                                                  : static_cast<void *>(cLotImposter::Create()));
            if (!face) {
                ctx.Fail("lot imposter: no object");
                return;
            }
            uint8_t *const p = face - 8;
            void *const faces[2] = {p, face};
            auto noteWords = [&] {
                drawfx::Note("imposter words", {Word(p, 4), drawfx::Ptr(Word(p, 0xc)), drawfx::Ptr(Word(p, 0x10))});
            };
            noteWords();
            const unsigned steps = g() % 16;
            for (unsigned s = 0; s < steps; ++s) {
                switch (g() % 11) {
                case 0: QueryNote(p, faces[g() % 2], Pick(g, {1u, 0xad892b02u, uint32_t(g())}), g() % 10 == 0); break;
                case 1: drawfx::Note("addref", VCall<uint32_t>(faces[g() % 2], 1)); break;
                case 2:
                    if (Word(p, 4) > 1) drawfx::Note("release", VCall<uint32_t>(faces[g() % 2], 2));
                    break;
                case 3: drawfx::Note("unload", VCall<uint32_t>(face, 4) & 0xff); break;
                case 4: drawfx::Note("scene node", drawfx::Ptr(VCall<uint32_t>(face, 5))); break;
                case 5: drawfx::Note("desc", drawfx::Ptr(VCall<uint32_t>(face, 6 + g() % 2))); break;
                case 6: drawfx::Note("has imposter", VCall<uint32_t>(face, 8) & 0xff); break;
                case 7: VCall<void>(face, 9, g() % 4 == 0 ? nullptr : static_cast<void *>(&gw.terrain)); break;
                default: {
                    // the test sets a held reference (a fake, counted) where there is none
                    const uint32_t off = g() % 2 ? 0xc : 0x10;
                    if (!Word(p, off)) {
                        Fake &f = off == 0xc ? gw.node[g() % 2] : gw.desc[g() % 2];
                        ++f.refs;
                        *reinterpret_cast<void **>(p + off) = &f;
                    }
                }
                }
                noteWords();
            }
            End(p, p, 3, 4, g, deletes, faces, 2, 0x14);
            NoteRefs();
            ++ctx.cases;
        });
}

// Verifies: 0x36cb20 (Load).
DIFF_TEST(LotImposter_Load, 0x36cb20)
{
    shape::StandIn lotGroup(0x401790, &nTSSG::GetLotGroup, &LotGroupStand);
    shape::StandIn loadResources(0xb40560, &nTSSG::LoadImposterResources, &LoadResourcesStand);
    shape::StandIn resolve(0xae7340, &nGZSceneGraph::ResolveResourceName, &ResolveStand);
    shape::StandIn parse(0xae6e60, &StringKeyParser::ParseKey, &ParseStand);
    shape::StandIn set(0x3f7e90, &cDesignModeNode::Set, &SetStand);
    shape::StandIn season(0x3f8a10, &cDesignModeNode::ApplySeason, &SeasonStand);
    shape::StandIn tree(0x3f8990, &cDesignModeNode::ApplyDesignModesToTree, &TreeStand);
    shape::CodePatch manager(0x20deb5, reinterpret_cast<const void *>(&ResourcesStand));
    shape::DeleteRecorder deletes;
    unsigned loaded = 0, walked = 0;
    sgfx::CheckSides(
        ctx, "lot imposter load", 3000, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
        [&](bool original, std::mt19937 &g) {
            uint8_t *const face = static_cast<uint8_t *>(original ? ctx.Original<Factory>(0x36d0a0)()
                                                                  : static_cast<void *>(cLotImposter::Create()));
            uint8_t *const p = face - 8;
            gw.object = p;
            void *const faces[2] = {p, face};
            for (uint32_t off : {0xcu, 0x10u})
                if (g() % 2) {
                    Fake &f = off == 0xc ? gw.node[0] : gw.desc[0];
                    ++f.refs;
                    *reinterpret_cast<void **>(p + off) = &f;
                }
            const unsigned loads = 1 + g() % 2;
            for (unsigned i = 0; i < loads; ++i) {
                gw.ranges = 0;
                const uint32_t r = VCall<uint32_t>(face, 3, uint32_t(g() % 4 == 0 ? 0 : g())) & 0xff;
                drawfx::Note("load", {r, drawfx::Ptr(Word(p, 0xc)), drawfx::Ptr(Word(p, 0x10)), uint32_t(gw.ranges)});
                if (!original) {
                    loaded += r;
                    walked += gw.ranges == 2;
                }
            }
            End(p, p, 3, 4, g, deletes, faces, 2, 0x14);
            NoteRefs();
            ++ctx.cases;
        });
    if (loaded == 0 || walked == 0) ctx.Fail("lot imposter load: %u loaded, %u walked both ranges", loaded, walked);
}
