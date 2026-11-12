// Differential tests of nTSSG::cLightRigManager (lane 261109b, scenegraph LG7c; src/sg/cLightRigManager.h).
//
//   SG_LightRigManagerLife  one light rig manager's life on each side: made by the factory 0x2c2af0 (ours: Create), then
//       random calls through its vtable: QueryInterface (its own id, 1, another, 0; the out pointer null or not), AddRef /
//       Release (never the last), Init (twice too), Shutdown, AddRigFile, SetDirty, Update, LoadRig, SetCurrentRig,
//       AddRigToScene / RemoveRigFromScene, GetCurrentRigName, GetRigRoot, GetNamedNode (node names in either case, long,
//       empty), GetAnimation (keys present and missing), HasRig (names present and not), IsRootMissing and GetAutoRig.
//       The file parser's RunFiles (a fake) does what the rig file's commands do to the manager's map: for some of the
//       world's rig names it makes or finds the record (our map code: in the game the parser's own inserts make records
//       through the hooked record constructor) and sets the resource name, the root tag, the animations, the auto-rig
//       name and flag, and clears the loaded flags. Each seed's world answers by name: a resource (slot 23 of the scene
//       graph system) or none, a tagged node in it (0xae7820) or none (each tagged node starts as a child of a resource
//       and holds 0-2 shape ref nodes, whose shape lists the rig load empties), an animation (slot 26: found or not,
//       answered true or false), a named node (0xae7490) or none, and each root's animated face (0xae1ed0) or none.
//       After each call the manager is noted: its count, parser and parser state, then each rig: its name, root, named
//       nodes, the three names, the animations and the animation faces found, the two load flags, the auto-rig flag
//       when it is defined, then the current name and whether the current rig is the record of that name (when it is
//       valid). Pointers the slots return into the map are noted as "the record the test finds". At the end the manager
//       is deleted (the last Release, or slot 19 with flags 1, or with 0 and the block freed by the test), then the
//       fakes' counts and the heap. LoadRig / SetCurrentRig on a name with no record make one in Load: the original's
//       record constructor 0x2c19b0 against ours (LightRig::LightRig, the hook's target).
//
// Held constant by the fixture (the original's bugs: a test that took these paths would crash or read uninitialised
// memory on both sides): a name with no rig is passed only to HasRig, to IsRootMissing (which reads the map head
// node's never-constructed value: the test fills that value with seeded bytes alike on both sides first), and to
// LoadRig / SetCurrentRig only in a seed where
// the empty resource name and the empty tag both resolve (so the new record's root is found and its uninitialised
// auto-rig flag is never read); the COM director always makes the parser, roots, lights and their extensions; every
// scene has the extension; Shutdown and AddRigFile only after Init. The auto-rig flag is noted only for records the
// parser or Update set it on.
//
// Stood in on both sides: the COM director 0x1ae18d (the COM fake), the game's operator new 0x824632 / delete 0x824624 /
// free 0x82540f (ours, so the original's blocks are counted like ours), the image's atexit 0x824cbe (a no-op), the parser
// state's constructor 0x2c1800 and ours (a block whose vbtable leads to two fakes: the parser face and its cIGZUnknown;
// the state has its own test, tests/sg_light_rig_parser.cpp), the animated-face finder 0xae1ed0, the scene graph system
// 0x20defb and the service 0x20deb5 (fakes), the tagged-node finder 0xae7820 and the named-node finder 0xae7490.
#include "sgfx.h"

#include "code_patch.h"

#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"
#include "sg/cLightRigAndCameraParserState.h"
#include "sg/cLightRigManager.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

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

// cIGZCOM: 3 GetClassObject (class, iid, out).
constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data(), false, drawfx::kTestTable};

// nGZCommandParser::cIFileParser (src/gz/cFileParser.h).
constexpr At kParserAt[] = {{3, {0, Ret::Int, true, "Init", ""}},
                            {4, {0, Ret::Int, true, "Shutdown", ""}},
                            {5, {2, Ret::Int, true, "SetParser", "au"}},
                            {6, {2, Ret::Int, true, "AddFile", "su"}},
                            {7, {0, Ret::Int, true, "RunFiles", ""}},
                            {8, {1, Ret::Int, true, "SetDirty", "b"}},
                            {10, {0, Ret::Int, true, "IsDirty", ""}}};
constexpr auto kParserSlots = Table<11>(kParserAt);
constexpr drawfx::Iface kParser{"FileParser", 11, kParserSlots.data(), false, drawfx::kTestTable};

// A plain cIGZUnknown (the parser face, the virtual bases, shape ref holders, named nodes).
constexpr At kPlainAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kPlainSlots = Table<3>(kPlainAt);
constexpr drawfx::Iface kPlain{"Plain", 3, kPlainSlots.data(), false, drawfx::kTestTable};

// The tree nodes (roots, resources, tagged nodes): kNode's slots with the ones the rig code calls declared.
constexpr std::array<Slot, 62> MakeNodeSlots()
{
    std::array<Slot, 62> t{};
    for (unsigned i = 0; i < 62; ++i) t[i] = sgfx::kNodeSlots[i];
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[42] = {1, Ret::Int, true, "GetChildren", "a"};
    t[44] = {3, Ret::Int, true, "MakeIterator44", "auW2"};
    t[45] = {1, Ret::Int, true, "AddChild", "a"};
    t[46] = {1, Ret::Int, true, "RemoveChild", "a"};
    return t;
}
constexpr auto kNodeSlots = MakeNodeSlots();
constexpr drawfx::Iface kTreeNode{"TreeNode", 62, kNodeSlots.data(), false, drawfx::kTestTable};

// cISceneNodeIterator: 4 IsEqual, 5 Get, 6 Next.
constexpr At kIterAt[] = {{4, {1, Ret::Int, true, "IsEqual", "a"}},
                          {5, {0, Ret::Int, true, "Get", ""}},
                          {6, {0, Ret::Int, true, "Next", ""}}};
constexpr auto kIterSlots = Table<7>(kIterAt);
constexpr drawfx::Iface kIter{"Iterator", 7, kIterSlots.data(), false, drawfx::kTestTable};

// cIShapeRefNode: 93 GetShapes (hidden out), 95 RemoveShape, 98 ShapeCount.
constexpr At kShapeRefAt[] = {{93, {1, Ret::Int, true, "GetShapes", "a"}},
                              {95, {1, Ret::Int, true, "RemoveShape", "u"}},
                              {98, {0, Ret::Int, true, "ShapeCount", ""}}};
constexpr auto kShapeRefSlots = Table<100>(kShapeRefAt);
constexpr drawfx::Iface kShapeRef{"ShapeRef", 100, kShapeRefSlots.data(), false, drawfx::kTestTable};

// A root's animated face (0xae1ed0's answer): 17 its clock, 30 its mode, 40 end, 41 begin.
constexpr At kAnimatedAt[] = {{17, {1, Ret::Int, true, "SetClock", "a"}},
                              {30, {1, Ret::Int, true, "SetMode", "u"}},
                              {40, {1, Ret::Int, true, "End", "u"}},
                              {41, {0, Ret::Int, true, "Begin", ""}}};
constexpr auto kAnimatedSlots = Table<42>(kAnimatedAt);
constexpr drawfx::Iface kAnimated{"Animated", 42, kAnimatedSlots.data(), false, drawfx::kTestTable};

// The scene graph system (0x20defb): 23 the resource by name, 26 the animation by name.
constexpr At kSystemAt[] = {{23, {6, Ret::Int, true, "FindResource", "suuuau"}},
                            {26, {3, Ret::Int, true, "FindAnimation", "sau"}}};
constexpr auto kSystemSlots = Table<27>(kSystemAt);
constexpr drawfx::Iface kSystem{"SceneGraphSystem", 27, kSystemSlots.data(), false, drawfx::kTestTable};

// The service 0x20deb5 answers: Update calls its slot 45.
constexpr At kServiceAt[] = {{45, {0, Ret::Int, true, "Slot45", ""}}};
constexpr auto kServiceSlots = Table<46>(kServiceAt);
constexpr drawfx::Iface kService{"Service", 46, kServiceSlots.data(), false, drawfx::kTestTable};

// The scene manager AddRigToScene / RemoveRigFromScene take: 7 add, 9 remove, 17 its scene.
constexpr At kSceneMgrAt[] = {{7, {1, Ret::Int, true, "AddNode", "a"}},
                              {9, {1, Ret::Int, true, "RemoveNode", "a"}},
                              {17, {0, Ret::Int, true, "GetScene", ""}}};
constexpr auto kSceneMgrSlots = Table<18>(kSceneMgrAt);
constexpr drawfx::Iface kSceneMgr{"SceneManager", 18, kSceneMgrSlots.data(), false, drawfx::kTestTable};

// The scene: 24 FindExtension (iid, out, 0).
constexpr At kSceneAt[] = {{24, {3, Ret::Int, true, "FindExtension", "uau"}}};
constexpr auto kSceneSlots = Table<25>(kSceneAt);
constexpr drawfx::Iface kScene{"Scene", 25, kSceneSlots.data(), false, drawfx::kTestTable};

// The scene's extension 0x3afeef61 (an embedded face): 6 add, 7 remove an animated face.
constexpr At kExtFaceAt[] = {{6, {1, Ret::Int, true, "AddAnimated", "a"}}, {7, {1, Ret::Int, true, "RemoveAnimated", "a"}}};
constexpr auto kExtFaceSlots = Table<8>(kExtFaceAt);
constexpr drawfx::Iface kExtFace{"SceneExtension", 8, kExtFaceSlots.data(), false, drawfx::kTestTable};

// The default light (class 0x253d2018): 78 layer bits, 91 kind, 93 extension.
constexpr At kLightAt[] = {{78, {1, Ret::Int, true, "SetLayers", "u"}},
                           {91, {1, Ret::Int, true, "SetKind", "u"}},
                           {93, {1, Ret::Int, true, "SetExtension", "a"}}};
constexpr auto kLightSlots = Table<94>(kLightAt);
constexpr drawfx::Iface kLight{"Light", 94, kLightSlots.data(), false, drawfx::kTestTable};

// The default light's extension (class 0xc9c81b9b): 41 colour, 42 the word after.
constexpr At kLightExtAt[] = {{41, {1, Ret::Int, true, "SetColor", "F4"}}, {42, {1, Ret::Int, true, "SetWord", "u"}}};
constexpr auto kLightExtSlots = Table<43>(kLightExtAt);
constexpr drawfx::Iface kLightExt{"LightExtension", 43, kLightExtSlots.data(), false, drawfx::kTestTable};

// cIGZString: 4 FromChar.
constexpr At kStringAt[] = {{4, {1, Ret::Int, true, "FromChar", "s"}}};
constexpr auto kStringSlots = Table<5>(kStringAt);
constexpr drawfx::Iface kString{"GZString", 5, kStringSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kParserClass = 0x5cf9459a;
constexpr uint32_t kRootClass = 0x65246462;
constexpr uint32_t kClockClass = 0xa9de6c9c;
constexpr uint32_t kLightClass = 0x253d2018;
constexpr uint32_t kLightExtClass = 0xc9c81b9b;
constexpr uint32_t kShapeRefIid = 0xa516cd10;

constexpr unsigned kRoots = 40;
constexpr unsigned kResources = 4;
constexpr unsigned kTagNodes = 6;
constexpr unsigned kShapeNodes = 8;
constexpr unsigned kNamed = 4;
constexpr unsigned kAnimFaces = 3;
constexpr unsigned kKidLists = kRoots + kResources + kTagNodes;
constexpr unsigned kMaxKids = 16;
constexpr unsigned kListWord = 6;   // a tree node's child list (World::kids)
constexpr unsigned kParentWord = 7; // a node's parent (the light's too)

// A face with a virtual base (MSVC's `public virtual cIGZUnknown`): its own vtable, a vbptr whose second entry leads to
// the cIGZUnknown fake.
struct VbFace {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[2];
};
void MakeVbFace(VbFace &face, Fake &unknown)
{
    face.vt = nullptr;
    face.vbptr = face.vbtable;
    face.vbtable[0] = -4;
    face.vbtable[1] = int32_t(drawfx::P(&unknown) - drawfx::P(&face.vbptr));
}

struct Kids {
    Fake *items[kMaxKids];
    unsigned n;
};

struct World {
    Fake com, parser, baseParser, stateUnknown, system, service, sceneMgr, scene, extFaceFake, extUnknown, light, lightExt,
        gzString, clockUnknown, animA, animB;
    Fake roots[kRoots], resources[kResources], tagNodes[kTagNodes], shapeNodes[kShapeNodes], shapeRefs[kShapeNodes],
        named[kNamed], iters[3], animUnknowns[kAnimFaces];
    VbFace extFace, clockFace, animFaces[kAnimFaces];
    Kids kids[kKidLists];
    uint32_t shapes[kShapeNodes][4];
    uint8_t shapeKids[kTagNodes][2]; // shape node indices + 1 (0 none)
    Fake *iterList[kTagNodes * 2 * kMaxKids];
    unsigned iterCount = 0;
    unsigned rootsUsed = 0;
    uint32_t seed = 0;
    uint32_t sequence = 0;
    uint32_t defined = 0; // the names (bits by kAllNames) whose records hold a defined auto-rig flag
    bool currentValid = false;
    cLightRigManager *mgr = nullptr;

    uint32_t Next()
    {
        sequence = sequence * 1664525u + 1013904223u;
        return sequence >> 8;
    }
};
World gw;

const char *const kAllNames[] = {"day", "Night", "night", "sunset", "a_long_light_rig_name_past_16", "", "dusk", "DAY"};
constexpr unsigned kRigNames = 6; // the first ones: the names the rig file defines
const char *const kResourceNames[] = {"lights_day", "lights_night", "", "missing_resource_name_long_x", "studio"};
const char *const kTags[] = {"Lamp", "SUN", "", "key_light_tag_longer_than_16", "fill"};
const char *const kAnimKeys[] = {"on", "off", "flicker", "a_long_animation_key_x", "missing"};
const char *const kAnimValues[] = {"lamp_on", "Lamp_Off", "", "a_long_animation_value_name"};
const char *const kAutoNames[] = {"", "auto_day", "AUTO", "a_long_auto_rig_name_past_16"};
const char *const kNodeNames[] = {"Lamp", "lamp", "KEY", "fill_node", "a_long_named_node_name_past_16", ""};
const char *const kPaths[] = {"Lights/LightRig.txt", "C:/a/long/path/to/Lights/LightRig_override.txt", ""};

uint32_t Hash(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; ++text) h = (h ^ uint8_t(std::tolower(uint8_t(*text)))) * 16777619u;
    return (h ^ (gw.seed * 0x9e3779b9u)) * 0x85ebca6bu;
}
unsigned NameIndex(const char *name)
{
    for (unsigned i = 0; i < std::size(kAllNames); ++i)
        if (!std::strcmp(kAllNames[i], name)) return i;
    return 31;
}
Fake *ResourceFor(const char *name)
{
    const uint32_t h = Hash(name);
    return h % 4 ? &gw.resources[(h >> 8) % kResources] : nullptr;
}
Fake *TagNodeFor(const char *tag)
{
    const uint32_t h = Hash(tag) * 3u + 1u;
    return h % 3 ? &gw.tagNodes[(h >> 8) % kTagNodes] : nullptr;
}
bool EmptyResolves() { return ResourceFor("") && TagNodeFor(""); }
template <class T> unsigned IndexIn(const Fake *f, const T &array)
{
    for (unsigned i = 0; i < std::size(array); ++i)
        if (&array[i] == f) return i;
    return ~0u;
}

// ---- stand-ins ----
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }
int __cdecl NoAtexit(void (*)()) { return 0; }
void *__cdecl StandCom()
{
    drawfx::Note("com", 0);
    return &gw.com;
}
// The parser state (0x2c1800, thiscall (manager, 1), ret 8): the block's vbptr at +8 leads (its vbtable at +0x10) to the
// state's cIGZUnknown (entry 1) and its cIBaseParser face (entry 2).
void *__fastcall StandState(void *memory, void *, void *manager, int flag)
{
    drawfx::Note("parser state", {drawfx::Ptr(memory), drawfx::Ptr(manager), uint32_t(flag)});
    uint8_t *const m = static_cast<uint8_t *>(memory);
    int32_t *const vbtable = reinterpret_cast<int32_t *>(m + 0x10);
    vbtable[0] = -8;
    vbtable[1] = int32_t(drawfx::P(&gw.stateUnknown) - drawfx::P(m + 8));
    vbtable[2] = int32_t(drawfx::P(&gw.baseParser) - drawfx::P(m + 8));
    *reinterpret_cast<int32_t **>(m + 8) = vbtable;
    *reinterpret_cast<void **>(m) = nullptr;
    return memory;
}
void *__cdecl StandAnimated(void *root)
{
    drawfx::Note("animated face of", drawfx::Ptr(root));
    const unsigned i = IndexIn(static_cast<Fake *>(root), gw.roots);
    if (i == ~0u) return nullptr;
    switch ((i + gw.seed) % 3) {
    case 0: return nullptr;
    case 1: return &gw.animA;
    default: return &gw.animB;
    }
}
void *__cdecl StandSystem() { return &gw.system; }
void *__cdecl StandService() { return &gw.service; }
void __cdecl StandFindTag(const char *tag, void *resource, uint32_t depth, uint32_t iid, void **out)
{
    sgfx::NoteText("find tag", tag);
    drawfx::Note("find tag in", {drawfx::Ptr(resource), depth, iid});
    Fake *const node = TagNodeFor(tag);
    if (node) ++node->refs;
    *out = node;
}
void *__cdecl StandFindNamed(const char *name, void *root, uint32_t depth)
{
    sgfx::NoteText("find named", name);
    drawfx::Note("find named in", {drawfx::Ptr(root), depth});
    const uint32_t h = Hash(name);
    return h % 3 ? &gw.named[(h >> 8) % kNamed] : nullptr;
}

// ---- fake handlers ----
uint32_t ComClassObject(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    Fake *made = nullptr;
    void *face = nullptr;
    switch (a[0]) {
    case kParserClass: made = &gw.parser; break;
    case kRootClass:
        if (gw.rootsUsed < kRoots) made = &gw.roots[gw.rootsUsed++];
        break;
    case kClockClass:
        if (gw.Next() % 3) made = &gw.clockUnknown, face = &gw.clockFace;
        break;
    case kLightClass: made = &gw.light; break;
    case kLightExtClass: made = &gw.lightExt; break;
    default: break;
    }
    if (made) ++made->refs;
    *out = face ? face : made;
    return made != nullptr;
}
void AddKid(Fake &parent, Fake *child)
{
    Kids &k = gw.kids[parent.value[kListWord]];
    if (k.n == kMaxKids) {
        drawfx::Note("child list full", parent.id);
        return;
    }
    for (unsigned i = k.n; i > 0; --i) k.items[i] = k.items[i - 1];
    k.items[0] = child;
    ++k.n;
    child->value[kParentWord] = drawfx::P(&parent);
}
uint32_t NodeAddChild(Fake &self, const uint32_t *a)
{
    AddKid(self, reinterpret_cast<Fake *>(uintptr_t(a[0])));
    return 0;
}
uint32_t NodeRemoveChild(Fake &self, const uint32_t *a)
{
    Fake *const child = reinterpret_cast<Fake *>(uintptr_t(a[0]));
    Kids &k = gw.kids[self.value[kListWord]];
    for (unsigned i = 0; i < k.n; ++i) {
        if (k.items[i] != child) continue;
        for (unsigned j = i + 1; j < k.n; ++j) k.items[j - 1] = k.items[j];
        --k.n;
        child->value[kParentWord] = 0;
        break;
    }
    return 0;
}
uint32_t NodeParent(Fake &self, const uint32_t *) { return self.value[kParentWord]; }
uint32_t NodeHasChildren(Fake &self, const uint32_t *) { return gw.kids[self.value[kListWord]].n != 0; }
uint32_t NodeChildren(Fake &self, const uint32_t *a)
{
    const Kids &k = gw.kids[self.value[kListWord]];
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = k.n ? drawfx::P(k.items[0]) : 0;
    out[2] = 0;
    return a[0];
}
// The shape ref nodes below the node's tagged children, through three counted iterators.
uint32_t NodeIterator(Fake &self, const uint32_t *a)
{
    const Kids &k = gw.kids[self.value[kListWord]];
    gw.iterCount = 0;
    for (unsigned i = 0; i < k.n; ++i) {
        const unsigned t = IndexIn(k.items[i], gw.tagNodes);
        if (t == ~0u) continue;
        for (uint8_t s : gw.shapeKids[t])
            if (s) gw.iterList[gw.iterCount++] = &gw.shapeNodes[s - 1];
    }
    gw.iters[0].value[0] = 0;
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 3; ++i) {
        ++gw.iters[i].refs;
        out[i] = drawfx::P(&gw.iters[i]);
    }
    return a[0];
}
uint32_t IterEqual(Fake &self, const uint32_t *) { return self.value[0] >= gw.iterCount; }
uint32_t IterGet(Fake &self, const uint32_t *) { return drawfx::P(gw.iterList[self.value[0]]); }
uint32_t IterNext(Fake &self, const uint32_t *)
{
    ++self.value[0];
    return 0;
}
uint32_t ShapeGet(Fake &self, const uint32_t *a)
{
    uint32_t *const list = gw.shapes[self.value[1]];
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = drawfx::P(list);
    out[2] = drawfx::P(list + self.value[0]);
    return a[0];
}
uint32_t ShapeRemove(Fake &self, const uint32_t *a)
{
    uint32_t *const list = gw.shapes[self.value[1]];
    for (unsigned i = 0; i < self.value[0]; ++i) {
        if (list[i] != a[0]) continue;
        for (unsigned j = i + 1; j < self.value[0]; ++j) list[j - 1] = list[j];
        --self.value[0];
        break;
    }
    return 0;
}
uint32_t ShapeCount(Fake &self, const uint32_t *) { return self.value[0]; }
void FillShapes()
{
    for (unsigned i = 0; i < kShapeNodes; ++i) {
        gw.shapeRefs[i].value[0] = gw.Next() % 4;
        for (unsigned k = 0; k < 4; ++k) gw.shapes[i][k] = 0x5100u + i * 16u + k;
    }
}
uint32_t SystemResource(Fake &, const uint32_t *a)
{
    const char *const name = reinterpret_cast<const char *>(uintptr_t(a[0]));
    void **const out = reinterpret_cast<void **>(uintptr_t(a[4]));
    Fake *const r = ResourceFor(name);
    if (r) ++r->refs;
    *out = r;
    return r != nullptr;
}
uint32_t SystemAnimation(Fake &, const uint32_t *a)
{
    const char *const name = reinterpret_cast<const char *>(uintptr_t(a[0]));
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    const uint32_t h = Hash(name);
    void *found = nullptr;
    if (h % 4 < 2) {
        const unsigned i = (h >> 8) % kAnimFaces;
        ++gw.animUnknowns[i].refs;
        found = &gw.animFaces[i];
    }
    *out = found;
    return (h >> 4) % 4 != 0;
}
uint32_t SceneFind(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    ++gw.extUnknown.refs;
    *out = &gw.extFace;
    return 1;
}
uint32_t ParserSetDirty(Fake &self, const uint32_t *a)
{
    self.value[0] = a[0] & 0xff;
    return 0;
}
uint32_t ParserIsDirty(Fake &self, const uint32_t *) { return self.value[0]; }
// What the rig file's commands do: records made or found by name, their names, animations and flags set.
uint32_t ParserRun(Fake &self, const uint32_t *)
{
    const unsigned count = gw.Next() % 4;
    for (unsigned k = 0; k < count; ++k) {
        const unsigned index = gw.Next() % kRigNames;
        LightRig &rig = gw.mgr->mRigs[kAllNames[index]];
        rig.mResourceName.value.mString = kResourceNames[gw.Next() % std::size(kResourceNames)];
        rig.mRootTag.value.mString = kTags[gw.Next() % std::size(kTags)];
        rig.mAnimations.clear();
        for (unsigned n = gw.Next() % 4; n > 0; --n)
            rig.mAnimations[kAnimKeys[gw.Next() % 4]] = kAnimValues[gw.Next() % std::size(kAnimValues)];
        rig.mLoaded = false;
        rig.mRootMissing = false;
        rig.mAutoRig = gw.Next() % 2 != 0;
        rig.mAutoRigName.value.mString = kAutoNames[gw.Next() % std::size(kAutoNames)];
        gw.defined |= 1u << index;
    }
    if (gw.Next() % 3 == 0) FillShapes();
    self.value[0] = 0;
    return gw.Next() % 2;
}

void MakeTreeNode(Fake &f, uint32_t id, unsigned list)
{
    f.Reset<kTreeNode>(id);
    f.value[kListWord] = list;
    f.On(34, &NodeParent);
    f.On(42, &NodeChildren);
    f.On(44, &NodeIterator);
    f.On(45, &NodeAddChild);
    f.On(46, &NodeRemoveChild);
    f.On(52, &NodeHasChildren);
}

void Build(uint32_t seed)
{
    gw.seed = seed;
    gw.sequence = seed * 2654435761u + 5u;
    gw.rootsUsed = 0;
    gw.iterCount = 0;
    gw.defined = 0;
    gw.currentValid = false;
    gw.mgr = nullptr;
    for (Kids &k : gw.kids) k.n = 0;

    gw.com.Reset<kCom>(460);
    gw.com.On(3, &ComClassObject);
    gw.parser.Reset<kParser>(470);
    gw.parser.On(7, &ParserRun);
    gw.parser.On(8, &ParserSetDirty);
    gw.parser.On(10, &ParserIsDirty);
    gw.baseParser.Reset<kPlain>(471);
    gw.stateUnknown.Reset<kPlain>(472);
    gw.system.Reset<kSystem>(480);
    gw.system.On(23, &SystemResource);
    gw.system.On(26, &SystemAnimation);
    gw.service.Reset<kService>(481);
    gw.sceneMgr.Reset<kSceneMgr>(482);
    gw.scene.Reset<kScene>(483);
    gw.sceneMgr.Returns(17, &gw.scene);
    gw.scene.On(24, &SceneFind);
    gw.extUnknown.Reset<kPlain>(484);
    gw.extFaceFake.Reset<kExtFace>(485);
    MakeVbFace(gw.extFace, gw.extUnknown);
    gw.extFaceFake.Embed(&gw.extFace);
    drawfx::AddRange(&gw.extFace, sizeof(gw.extFace), 0x7d000000u | (486u << 8));
    gw.light.Reset<kLight>(490);
    gw.lightExt.Reset<kLightExt>(491);
    gw.gzString.Reset<kString>(492);
    gw.clockUnknown.Reset<kPlain>(493);
    MakeVbFace(gw.clockFace, gw.clockUnknown);
    drawfx::AddRange(&gw.clockFace, sizeof(gw.clockFace), 0x7d000000u | (494u << 8));
    gw.animA.Reset<kAnimated>(320);
    gw.animB.Reset<kAnimated>(321);
    for (unsigned i = 0; i < kAnimFaces; ++i) {
        gw.animUnknowns[i].Reset<kPlain>(300 + i);
        MakeVbFace(gw.animFaces[i], gw.animUnknowns[i]);
        drawfx::AddRange(&gw.animFaces[i], sizeof(VbFace), 0x7d000000u | ((303u + i) << 8));
    }
    for (unsigned i = 0; i < 3; ++i) {
        gw.iters[i].Reset<kIter>(260 + i);
        gw.iters[i].On(4, &IterEqual);
        gw.iters[i].On(5, &IterGet);
        gw.iters[i].On(6, &IterNext);
    }
    for (unsigned i = 0; i < kRoots; ++i) MakeTreeNode(gw.roots[i], 100 + i, i);
    for (unsigned i = 0; i < kResources; ++i) MakeTreeNode(gw.resources[i], 140 + i, kRoots + i);
    for (unsigned i = 0; i < kShapeNodes; ++i) {
        gw.shapeRefs[i].Reset<kShapeRef>(190 + i);
        gw.shapeRefs[i].value[1] = i;
        gw.shapeRefs[i].On(93, &ShapeGet);
        gw.shapeRefs[i].On(95, &ShapeRemove);
        gw.shapeRefs[i].On(98, &ShapeCount);
        gw.shapeNodes[i].Reset<kPlain>(180 + i);
        gw.shapeNodes[i].AddFace(kShapeRefIid, gw.shapeRefs[i]);
    }
    FillShapes();
    for (unsigned i = 0; i < kTagNodes; ++i) {
        MakeTreeNode(gw.tagNodes[i], 160 + i, kRoots + kResources + i);
        for (uint8_t &s : gw.shapeKids[i]) s = uint8_t(gw.Next() % 3 ? 1 + gw.Next() % kShapeNodes : 0);
        if (gw.Next() % 4) AddKid(gw.resources[gw.Next() % kResources], &gw.tagNodes[i]);
    }
    for (unsigned i = 0; i < kNamed; ++i) gw.named[i].Reset<kPlain>(270 + i);
}

// ---- calls ----
uint8_t *Bytes(void *p) { return static_cast<uint8_t *>(p); }
template <unsigned Slot, class R, class... A> R Virtual(void *face, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(face))[Slot])(face, args...);
}

void NoteManager()
{
    cLightRigManager *const m = gw.mgr;
    drawfx::Note("manager", {m->mRefs, drawfx::Ptr(m->mParser.p), drawfx::Ptr(m->mParserState.p)});
    drawfx::Note("rigs", uint32_t(m->mRigs.size()));
    for (const auto &[name, rig] : m->mRigs) {
        sgfx::NoteText("rig", name.c_str());
        drawfx::Note("root", drawfx::Ptr(rig.mRoot.p));
        drawfx::Note("named nodes", uint32_t(rig.mNamedNodes.size()));
        for (const auto &[key, node] : rig.mNamedNodes) {
            sgfx::NoteText("named", key.c_str());
            drawfx::Note("named node", drawfx::Ptr(node));
        }
        sgfx::NoteText("resource", rig.mResourceName.value.mString.c_str());
        sgfx::NoteText("root tag", rig.mRootTag.value.mString.c_str());
        sgfx::NoteText("auto-rig name", rig.mAutoRigName.value.mString.c_str());
        drawfx::Note("animations", uint32_t(rig.mAnimations.size()));
        for (const auto &[key, value] : rig.mAnimations) {
            sgfx::NoteText("animation key", key.c_str());
            sgfx::NoteText("animation", value.c_str());
        }
        drawfx::Note("animation faces", uint32_t(rig.mAnimRefs.size()));
        for (const auto &[key, face] : rig.mAnimRefs) {
            sgfx::NoteText("animation face key", key.c_str());
            drawfx::Note("animation face", drawfx::Ptr(face.p));
        }
        drawfx::Note("flags", {uint32_t(rig.mLoaded), uint32_t(rig.mRootMissing)});
        if (gw.defined >> NameIndex(name.c_str()) & 1) drawfx::Note("auto rig", uint32_t(rig.mAutoRig));
    }
    sgfx::NoteText("current name", m->mCurrentName.c_str());
    if (gw.currentValid) {
        const auto found = m->mRigs.find(m->mCurrentName);
        drawfx::Note("current", uint32_t(found != m->mRigs.end() && m->mCurrent == &found->second));
    }
}
void NoteFakes()
{
    std::vector<uint32_t> refs;
    for (Fake *f : {&gw.parser, &gw.stateUnknown, &gw.extUnknown, &gw.light, &gw.lightExt, &gw.clockUnknown, &gw.com})
        refs.push_back(uint32_t(f->refs));
    for (unsigned i = 0; i < gw.rootsUsed; ++i) refs.push_back(uint32_t(gw.roots[i].refs));
    for (const Fake &f : gw.resources) refs.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.tagNodes) refs.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.shapeRefs) refs.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.iters) refs.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.animUnknowns) refs.push_back(uint32_t(f.refs));
    drawfx::Note("fake refs", uint32_t(refs.size()));
    for (uint32_t r : refs) drawfx::Note("ref", r);
    for (const Kids &k : gw.kids) {
        drawfx::Note("kids", k.n);
        for (unsigned i = 0; i < k.n; ++i) drawfx::Note("kid", drawfx::Ptr(k.items[i]));
    }
    for (unsigned i = 0; i < kShapeNodes; ++i) drawfx::Note("shapes", gw.shapeRefs[i].value[0]);
}

bool Has(const char *name) { return gw.mgr->mRigs.count(name) != 0; }
// A name with a record (or null when there is none).
const char *KnownName(std::mt19937 &rng)
{
    const auto &rigs = gw.mgr->mRigs;
    if (rigs.empty()) return nullptr;
    auto it = rigs.begin();
    for (unsigned n = rng() % rigs.size(); n > 0; --n) ++it;
    return it->first.c_str();
}
// A name LoadRig / SetCurrentRig may take: one with a record, or a new one when the new record's root is found.
const char *LoadableName(std::mt19937 &rng)
{
    const char *const name = kAllNames[rng() % std::size(kAllNames)];
    if (Has(name)) return name;
    if (EmptyResolves() && gw.rootsUsed + 2 < kRoots) return name;
    return KnownName(rng);
}
std::string Lower(const char *text)
{
    std::string s(text);
    for (char &c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

void Step(difftest::Context &ctx, void *o, std::mt19937 &rng)
{
    cLightRigManager *const m = gw.mgr;
    const uint32_t op = rng() % 24;
    char mark[48];
    std::snprintf(mark, sizeof mark, "op %u", op);
    sgfx::MarkStep(ctx, mark);
    switch (op) {
    case 0: {
        static const uint32_t kIids[] = {cILightRigManager::kIid, 1, 0x696d3daf, 0};
        const uint32_t iid = kIids[rng() % std::size(kIids)];
        int untouched = 0;
        void *out = &untouched;
        const bool nullOut = rng() % 5 == 0;
        const bool found = Virtual<0, bool>(o, iid, nullOut ? nullptr : &out); // receiver: cLightRigManager slot 0
        drawfx::Note("query", {iid, uint32_t(nullOut), uint32_t(found), out == o ? 1u : out == &untouched ? 2u : 0xbadu});
        break;
    }
    case 1: drawfx::Note("addref", Virtual<1, uint32_t>(o)); break;
    case 2:
        if (m->mRefs > 1) drawfx::Note("release", Virtual<2, uint32_t>(o));
        break;
    case 3: drawfx::Note("init", uint32_t(Virtual<3, bool>(o))); break;
    case 4:
        if (m->mParser.p && gw.rootsUsed + std::size(kAllNames) + 2 <= kRoots) {
            drawfx::Note("shutdown", uint32_t(Virtual<4, bool>(o)));
            gw.defined = 0;
            gw.currentValid = false;
        }
        break;
    case 5:
    case 6:
        if (m->mParser.p) drawfx::Note("add rig file", uint32_t(Virtual<5, bool>(o, kPaths[rng() % std::size(kPaths)])));
        break;
    case 7: Virtual<6, void>(o); break;
    case 8:
    case 9: {
        const bool runs = m->mParser.p && m->mParser.p == static_cast<void *>(&gw.parser) && gw.parser.value[0];
        if (runs && gw.rootsUsed + std::size(kAllNames) + 2 > kRoots) break;
        Virtual<7, void>(o);
        if (runs) {
            for (const auto &entry : m->mRigs) gw.defined |= 1u << NameIndex(entry.first.c_str());
        }
        break;
    }
    case 10:
    case 11: {
        const char *const name = LoadableName(rng);
        if (!name) break;
        const std::string key(name);
        LightRig *const rig = op == 10 ? Virtual<8, LightRig *>(o, name) : Virtual<9, LightRig *>(o, name);
        const auto found = m->mRigs.find(key);
        drawfx::Note(op == 10 ? "load rig" : "set current rig", uint32_t(found != m->mRigs.end() && rig == &found->second));
        if (op == 11) gw.currentValid = true;
        break;
    }
    case 12:
    case 13: {
        const char *const name = KnownName(rng);
        if (!name) break;
        if (op == 12) Virtual<10, void>(o, name, static_cast<void *>(&gw.sceneMgr));
        else Virtual<11, void>(o, name, static_cast<void *>(&gw.sceneMgr));
        break;
    }
    case 14: sgfx::NoteText("current rig name", Virtual<12, const char *>(o)); break;
    case 15: {
        const char *const name = KnownName(rng);
        if (name) drawfx::Note("rig root", drawfx::Ptr(Virtual<13, void *>(o, name)));
        break;
    }
    case 16:
    case 17: {
        const char *const name = KnownName(rng);
        if (!name) break;
        const std::string rigName(name);
        const char *const node = kNodeNames[rng() % std::size(kNodeNames)];
        void **const slot = Virtual<14, void **>(o, name, node);
        const LightRig &rig = m->mRigs.find(rigName)->second;
        const auto entry = rig.mNamedNodes.find(Lower(node));
        drawfx::Note("named node", {uint32_t(entry != rig.mNamedNodes.end() && slot == &entry->second), drawfx::Ptr(*slot)});
        break;
    }
    case 18: {
        const char *const name = KnownName(rng);
        if (name) drawfx::Note("animation", drawfx::Ptr(Virtual<15, void *>(o, name, kAnimKeys[rng() % std::size(kAnimKeys)])));
        break;
    }
    case 19: {
        const char *const name = kAllNames[rng() % std::size(kAllNames)];
        drawfx::Note("has rig", uint32_t(Virtual<16, bool>(o, name)));
        break;
    }
    case 20: {
        const char *name = KnownName(rng);
        if (rng() % 3 == 0) { // a name with no record: the head node's value answers (the seeded bytes)
            const char *const other = kAllNames[rng() % std::size(kAllNames)];
            if (!Has(other)) name = other;
        }
        if (name) drawfx::Note("root missing", uint32_t(Virtual<17, bool>(o, name)));
        break;
    }
    default: {
        const char *const name = KnownName(rng);
        if (!name) break;
        const unsigned index = NameIndex(name);
        const bool autoRig = Virtual<18, bool>(o, name, static_cast<void *>(&gw.gzString));
        if (gw.defined >> index & 1) drawfx::Note("auto rig", uint32_t(autoRig));
        break;
    }
    }
}

void RunLife(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *const o = original ? ctx.Original<void *(__cdecl *)()>(0x2c2af0)() : static_cast<void *>(cLightRigManager::Create());
    gw.mgr = static_cast<cLightRigManager *>(static_cast<cILightRigManager *>(o));
    {
        // The map head node's value is allocated but never constructed: give it seeded bytes, alike on both sides.
        uint8_t *const head = *reinterpret_cast<uint8_t **>(&gw.mgr->mRigs);
        for (size_t i = 0; i < sizeof(LightRig); ++i) head[0x28 + i] = uint8_t(rng() & 1); // 0 or 1: the bools read from it stay valid
    }
    NoteManager();
    drawfx::Note("addref", Virtual<1, uint32_t>(o));
    if (rng() % 4) {
        drawfx::Note("init", uint32_t(Virtual<3, bool>(o)));
        drawfx::Note("add rig file", uint32_t(Virtual<5, bool>(o, kPaths[0])));
    }
    NoteManager();
    const unsigned steps = 10 + rng() % 70;
    for (unsigned i = 0; i < steps; ++i) {
        Step(ctx, o, rng);
        NoteManager();
    }
    sgfx::MarkStep(ctx, "delete");
    switch (rng() % 3) {
    case 0:
        while (gw.mgr->mRefs > 1) Virtual<2, uint32_t>(o);
        drawfx::Note("released", Virtual<2, uint32_t>(o));
        break;
    case 1: drawfx::Note("deleted", uint32_t(Virtual<19, void *>(o, 1u) == o)); break;
    default:
        drawfx::Note("destroyed", uint32_t(Virtual<19, void *>(o, 0u) == o));
        ::operator delete(o);
        break;
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the factory 0x2c2af0, the constructor 0x2c1ab0, the destructor 0x2c2060 with the deleting destructor
// 0x2c2180, QueryInterface 0x2c4020, slots 3-18 (0x2c2d30, 0x2c42d0, 0x2c41f0, 0x2c2ae0, 0x2c3c70, 0x2c23b0 -> Load
// 0x2c3340, 0x2c4230, 0x2c23c0, 0x2c40a0, 0x2c2670, 0x2c3060, 0x2c30f0, 0x2c2520, 0x2c2f60, 0x2c2ec0, 0x2c2e00), the
// record's destructor 0x2c1ed0 and the map pair's 0x2c1c10.
DIFF_TEST(SG_LightRigManagerLife, 0x2c3340)
{
    shape::StandIn com{0x1ae18du, &GetDirectorFrameworkObject, &StandCom};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
    shape::CodePatch atexitPatch{0x824cbeu, shape::CodeOf(&NoAtexit)};
    shape::StandIn state{0x2c1800u, &nTSSG::cLightRigAndCameraParserState::Construct, &StandState};
    shape::StandIn animated{0xae1ed0u, &nGZSceneGraph::AnimatedFaceOf, &StandAnimated};
    shape::CodePatch system{0x20defbu, shape::CodeOf(&StandSystem)};
    shape::CodePatch service{0x20deb5u, shape::CodeOf(&StandService)};
    shape::StandIn findTag{0xae7820u, &nGZSceneGraph::FindNodeOfKindByName, &StandFindTag};
    shape::StandIn findNamed{0xae7490u, &nGZSceneGraph::FindNodeByName, &StandFindNamed};
    sgfx::CheckSides(
        ctx, "light rig manager", 400, [&](uint32_t seed, drawfx::Cells &) { Build(seed); },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, original, rng);
            ++ctx.cases;
        });
}
