// Differential tests of the design modes (src/sg/cLotImposterDesignModes.*; lane 261108j, scenegraph row LT2a).
//
//   LotImposterModes_StripLod  StripLod 0x3ac4f0 on random names (level-of-detail tags "SWX" in any case, cut short at
//       the end, next to each other): the count and the text left.
//   LotImposterModes_FindLod   FindLod 0x3f5470: the result, the out string (pre-filled) and the text.
//   LotImposterModes_DesignModes  a cDesignModes driven by random operations, each side with its own functions: Clear
//       0x3f5420, AddDesignMode 0x3f5360 (null key or value too), SetName 0x3f6440 (null, with and without stripping),
//       RewriteForLod 0x3f5550 (names, keys and values built from tags, '_', "lod90" and words), Load 0x3f62f0 (a node
//       fake's tag, the data list finder 0xaeb5b0 stood in for, finding a list fake or none), then the destructor
//       0x335a60 (the deletes counted). The state (both strings, every entry in order) noted after each operation.
//   LotImposterModes_Node      a cDesignModeNode the same way: Reset 0x3f7a00, Set 0x3f7e90 (an object with the resource
//       node face or without, or null), BaseName 0x3f8800, ApplySeason 0x3f8a10 (the season manager given or the scene
//       graph system's (cell 0x10cc458, slot 66, sometimes none); its variant the same name, the same in another case,
//       or another name; the resource manager 0x20defb stood in for, finding the variant's node or not; the node's
//       parent or none) and ApplyDesignModesToTree 0x3f8990, on random trees of node fakes with shape reference faces
//       and shapes (referrer counts 1-3).
//   LotImposterModes_Tree      MakeShapesUnique 0x3f8750, BindDesignModeInTree 0x3f8850 and MakeMaterialsUnique 0xae7af0
//       on random trees.
//
// The shapes' AddRef / Release calls are taken out of the logs (the original's MakeMaterialsUnique keeps its shapes in a
// function-static vector of counted pointers whose growth adds and drops references the first time only); every fake's
// final count is compared instead. Everything else is compared call for call with its arguments.
#include "code_patch.h"
#include "sgfx.h"

#include "rz/cRZString.h"
#include "sg/SGUtilities.h"
#include "sg/cLotImposterDesignModes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <string>

namespace {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using nTSSG::cDesignModeNode;
using nTSSG::cDesignModes;
using nTSWorld::cMapString;

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

constexpr At kNodeAt[] = {{26, {0, Ret::Int, true, "GetTag", ""}},
                          {34, {0, Ret::Int, true, "GetParent", ""}},
                          {38, {0, Ret::Int, true, "GetNextSibling", ""}},
                          {42, {1, Ret::Int, true, "GetChildren", "a"}},
                          {45, {1, Ret::Int, true, "AddChild", "a"}},
                          {46, {1, Ret::Int, true, "RemoveChild", "a"}}};
constexpr auto kNodeSlots = Table<62>(kNodeAt);
constexpr drawfx::Iface kNode{"ResourceNode", 62, kNodeSlots.data(), false, drawfx::kTestTable};

constexpr At kShapeRefAt[] = {{93, {1, Ret::Int, true, "GetShapes", "a"}},
                              {94, {1, Ret::Int, true, "AddShape", "a"}},
                              {95, {1, Ret::Int, true, "RemoveShape", "a"}}};
constexpr auto kShapeRefSlots = Table<96>(kShapeRefAt);
constexpr drawfx::Iface kShapeRef{"ShapeRefNode", 96, kShapeRefSlots.data(), false, drawfx::kTestTable};

constexpr At kShapeAt[] = {{15, {4, Ret::Int, true, "Clone", "uuau"}},
                           {31, {0, Ret::Int, true, "GetReferrerCount", ""}},
                           {34, {2, Ret::Int, true, "BindMaterial", "ss"}}};
constexpr auto kShapeSlots = Table<35>(kShapeAt);
constexpr drawfx::Iface kShape{"Shape", 35, kShapeSlots.data(), false, drawfx::kTestTable};

constexpr At kObjectAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kObjectSlots = Table<3>(kObjectAt);
constexpr drawfx::Iface kObject{"Object", 3, kObjectSlots.data(), false, drawfx::kTestTable};

constexpr At kSeasonsAt[] = {{7, {4, Ret::Int, true, "SeasonName", "SSuu"}}};
constexpr auto kSeasonsSlots = Table<8>(kSeasonsAt);
constexpr drawfx::Iface kSeasons{"SeasonManager", 8, kSeasonsSlots.data(), false, drawfx::kTestTable};

constexpr At kResourcesAt[] = {{23, {6, Ret::Int, true, "FindByName", "suuuau"}}};
constexpr auto kResourcesSlots = Table<24>(kResourcesAt);
constexpr drawfx::Iface kResources{"ResourceManager", 24, kResourcesSlots.data(), false, drawfx::kTestTable};

constexpr At kSystemAt[] = {{66, {0, Ret::Int, true, "GetSeasonManager", ""}}};
constexpr auto kSystemSlots = Table<67>(kSystemAt);
constexpr drawfx::Iface kSystem{"SGSystem", 67, kSystemSlots.data(), false, drawfx::kTestTable};

constexpr At kListAt[] = {{18, {2, Ret::Int, true, "GetString", "sS"}},
                          {22, {1, Ret::Int, true, "GetName", "u"}},
                          {26, {0, Ret::Int, true, "GetCount", ""}}};
constexpr auto kListSlots = Table<27>(kListAt);
constexpr drawfx::Iface kList{"DataList", 27, kListSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kResourceNodeIid = 0xe519c92c;
constexpr uint32_t kShapeRefNodeIid = 0xa516cd10;
constexpr uint32_t kSystemCell = 0x10cc458;
constexpr uint32_t kStaticGuard = 0x10f8f5c; // MakeMaterialsUnique's static vector (0x10f8f50) init guard

constexpr int kNodes = 9; // 0-3 the model's tree (0 its root), 4-7 the variant's (4 its root), 8 the model's parent
constexpr int kShapes = 10;
constexpr int kPerRef = 4;
constexpr int kListMax = 5;

struct World {
    Fake nodes[kNodes], refs[kNodes], shapes[kShapes], copies[kShapes];
    Fake object, bare, seasons, resources, system, list, decoy;
    std::string tags[kNodes];
    int firstChild[kNodes], nextSibling[kNodes], parent[kNodes];
    bool hasRef[kNodes];
    void *shapeLists[kNodes][kPerRef + 1];
    int shapeCount[kNodes];
    uint32_t referrers[kShapes];
    int listCount = 0;
    std::string listKeys[kListMax], listValues[kListMax];
    bool listFound = true;
    int variantMode = 0; // 0 the same name, 1 the same in another case, 2 another name
    std::string variantName;
    bool variantFound = true;
};
World gw;

const char *const kTokens[] = {"SWX01", "swx22", "SwX9", "SWX", "Roof", "wall", "_", "_", "lod90", "LOD90", "_cres",
                               "a", "Zed", "floor_", "x7", "_cres_", "sw", "WX", "_cre", "_CRE"};

std::string RandomText(std::mt19937 &g, unsigned maxTokens = 5)
{
    std::string s;
    const unsigned n = g() % (maxTokens + 1);
    for (unsigned i = 0; i < n; ++i) s += kTokens[g() % std::size(kTokens)];
    return s;
}

int IndexOf(const Fake &self, const Fake *array, int count)
{
    for (int i = 0; i < count; ++i)
        if (&array[i] == &self) return i;
    return -1;
}

uint32_t GetTag(Fake &self, const uint32_t *)
{
    const int i = IndexOf(self, gw.nodes, kNodes);
    return drawfx::P(gw.tags[i].c_str());
}
uint32_t GetParent(Fake &self, const uint32_t *)
{
    const int i = IndexOf(self, gw.nodes, kNodes);
    return gw.parent[i] < 0 ? 0 : drawfx::P(&gw.nodes[gw.parent[i]]);
}
uint32_t GetNextSibling(Fake &self, const uint32_t *)
{
    const int i = IndexOf(self, gw.nodes, kNodes);
    return gw.nextSibling[i] < 0 ? 0 : drawfx::P(&gw.nodes[gw.nextSibling[i]]);
}
// {current, begin, end}: the first child, a decoy where the range's begin is (never a child), null.
uint32_t GetChildren(Fake &self, const uint32_t *a)
{
    const int i = IndexOf(self, gw.nodes, kNodes);
    auto *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = gw.firstChild[i] < 0 ? 0 : drawfx::P(&gw.nodes[gw.firstChild[i]]);
    out[1] = drawfx::P(&gw.decoy);
    out[2] = 0;
    return a[0];
}
// {first, current, end}: current one past the first (a decoy position the loops must not start from).
uint32_t GetShapes(Fake &self, const uint32_t *a)
{
    const int i = IndexOf(self, gw.refs, kNodes);
    auto *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = drawfx::P(&gw.shapeLists[i][0]);
    out[1] = drawfx::P(&gw.shapeLists[i][gw.shapeCount[i] ? 1 : 0]);
    out[2] = drawfx::P(&gw.shapeLists[i][gw.shapeCount[i]]);
    return a[0];
}
uint32_t GetReferrerCount(Fake &self, const uint32_t *)
{
    const int i = IndexOf(self, gw.shapes, kShapes);
    return i < 0 ? 1 : gw.referrers[i];
}
uint32_t Clone(Fake &self, const uint32_t *a)
{
    const int i = IndexOf(self, gw.shapes, kShapes);
    if (i < 0 || (i % 4) == 3) return 0; // some shapes give no copy
    ++gw.copies[i].refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.copies[i];
    return 1;
}
uint32_t SeasonName(Fake &, const uint32_t *a)
{
    auto *const base = reinterpret_cast<cIGZString *>(uintptr_t(a[0]));
    auto *const out = reinterpret_cast<cIGZString *>(uintptr_t(a[1]));
    std::string name = base->ToChar();
    if (gw.variantMode == 1)
        for (char &c : name) c = char(std::isupper(uint8_t(c)) ? std::tolower(uint8_t(c)) : std::toupper(uint8_t(c)));
    else if (gw.variantMode == 2)
        name = gw.variantName;
    out->FromChar(name.c_str());
    return 1;
}
uint32_t FindByName(Fake &, const uint32_t *a)
{
    if (!gw.variantFound) return 0;
    ++gw.nodes[4].refs;
    *reinterpret_cast<void **>(uintptr_t(a[4])) = &gw.nodes[4];
    return 1;
}
uint32_t ListCount(Fake &, const uint32_t *) { return uint32_t(gw.listCount); }
uint32_t ListName(Fake &, const uint32_t *a) { return a[0] < kListMax ? drawfx::P(gw.listKeys[a[0]].c_str()) : 0; }
uint32_t ListString(Fake &, const uint32_t *a)
{
    const char *key = reinterpret_cast<const char *>(uintptr_t(a[0]));
    for (int i = 0; i < gw.listCount; ++i)
        if (key == gw.listKeys[i].c_str()) {
            reinterpret_cast<cIGZString *>(uintptr_t(a[1]))->FromChar(gw.listValues[i].c_str());
            return 1;
        }
    return 0;
}

void *__cdecl FindDataList(void *node, const char *name, void *start)
{
    drawfx::Note("find data list", {drawfx::Ptr(node), drawfx::P(start)});
    sgfx::NoteText("data list name", name);
    return gw.listFound ? &gw.list : nullptr;
}
void *__cdecl ResourceManager()
{
    drawfx::Note("resource manager", 0);
    return &gw.resources;
}

// A random forest: nodes 1-3 below 0, 5-7 below 4 (each under an earlier node of its tree), 8 the parent of 0 or not.
void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 g(seed * 2654435761u + 17u);
    for (int i = 0; i < kNodes; ++i) {
        gw.nodes[i].Reset<kNode>(600 + i);
        gw.refs[i].Reset<kShapeRef>(620 + i);
        gw.nodes[i].On(26, &GetTag);
        gw.nodes[i].On(34, &GetParent);
        gw.nodes[i].On(38, &GetNextSibling);
        gw.nodes[i].On(42, &GetChildren);
        gw.refs[i].On(93, &GetShapes);
        gw.firstChild[i] = gw.nextSibling[i] = gw.parent[i] = -1;
        gw.hasRef[i] = g() % 3 != 0;
        if (gw.hasRef[i]) gw.nodes[i].AddFace(kShapeRefNodeIid, gw.refs[i]);
        gw.shapeCount[i] = int(g() % (kPerRef + 1));
        for (int k = 0; k < gw.shapeCount[i]; ++k) gw.shapeLists[i][k] = &gw.shapes[g() % kShapes];
        gw.shapeLists[i][gw.shapeCount[i]] = nullptr;
        gw.tags[i] = RandomText(g);
    }
    if (g() % 4 == 0) gw.tags[0] = gw.tags[0] + "SWX0" + char('0' + g() % 10);
    auto link = [&](int child, int parent) {
        gw.parent[child] = parent;
        if (gw.firstChild[parent] < 0) gw.firstChild[parent] = child;
        else {
            int last = gw.firstChild[parent];
            while (gw.nextSibling[last] >= 0) last = gw.nextSibling[last];
            gw.nextSibling[last] = child;
        }
    };
    for (int i = 1; i <= 3; ++i) link(i, int(g() % i));
    for (int i = 5; i <= 7; ++i) link(i, 4 + int(g() % (i - 4)));
    if (g() % 3 != 0) link(0, 8);
    for (int i = 0; i < kShapes; ++i) {
        gw.shapes[i].Reset<kShape>(650 + i);
        gw.copies[i].Reset<kShape>(670 + i);
        gw.shapes[i].On(15, &Clone);
        gw.shapes[i].On(31, &GetReferrerCount);
        gw.copies[i].On(31, &GetReferrerCount);
        gw.referrers[i] = 1 + g() % 3;
    }
    gw.object.Reset<kObject>(690);
    gw.object.AddFace(kResourceNodeIid, gw.nodes[0]);
    gw.bare.Reset<kObject>(691);
    gw.seasons.Reset<kSeasons>(692);
    gw.seasons.On(7, &SeasonName);
    gw.resources.Reset<kResources>(693);
    gw.resources.On(23, &FindByName);
    gw.system.Reset<kSystem>(694);
    gw.system.Returns(66, g() % 5 == 0 ? nullptr : &gw.seasons);
    gw.list.Reset<kList>(695);
    gw.list.On(18, &ListString);
    gw.list.On(22, &ListName);
    gw.list.On(26, &ListCount);
    gw.decoy.Reset<kNode>(696);
    gw.listFound = g() % 5 != 0;
    gw.listCount = int(g() % (kListMax + 1));
    for (int i = 0; i < kListMax; ++i) {
        gw.listKeys[i] = RandomText(g, 3);
        gw.listValues[i] = RandomText(g, 4);
    }
    gw.variantMode = int(g() % 3);
    gw.variantName = RandomText(g, 4);
    gw.variantFound = g() % 4 != 0;
    for (int i = 0; i < kNodes; ++i) drawfx::AddRange(gw.shapeLists[i], sizeof(gw.shapeLists[i]), 0x53000000 + (i << 8));
    cells.Set(kSystemCell, &gw.system);
    // MakeMaterialsUnique's function-static vector: its thread-safe guard set past every thread's epoch, so the original
    // takes the (zeroed: empty) vector as constructed instead of registering its destructor with the image's CRT, which
    // the harness never initialised.
    cells.Set(kStaticGuard, 0x80000000u);
}

// The shapes' AddRef / Release calls out of the log (see the header), then every fake's count noted.
void FinishLog()
{
    auto &log = drawfx::gLog;
    log.erase(std::remove_if(log.begin(), log.end(),
                             [](const drawfx::Call &c) { return c.iface == &kShape && (c.slot == 1 || c.slot == 2); }),
              log.end());
    for (int i = 0; i < kNodes; ++i) drawfx::Note("node refs", {uint32_t(gw.nodes[i].refs), uint32_t(gw.refs[i].refs)});
    for (int i = 0; i < kShapes; ++i) drawfx::Note("shape refs", {uint32_t(gw.shapes[i].refs), uint32_t(gw.copies[i].refs)});
    drawfx::Note("other refs", {uint32_t(gw.object.refs), uint32_t(gw.seasons.refs), uint32_t(gw.resources.refs),
                                uint32_t(gw.system.refs), uint32_t(gw.list.refs), uint32_t(gw.decoy.refs)});
}

// Each side's functions.
struct Api {
    difftest::Context &ctx;
    bool original;

    template <class Fn> Fn O(uint32_t rva) { return ctx.Original<Fn>(rva); }
    void Clear(cDesignModes *m)
    {
        if (original) O<void(__thiscall *)(void *)>(0x3f5420)(m);
        else m->Clear();
    }
    bool Load(cDesignModes *m, void *node)
    {
        if (original) return O<bool(__thiscall *)(void *, void *)>(0x3f62f0)(m, node);
        return m->Load(static_cast<nGZSceneGraph::cIResourceNode *>(node));
    }
    void Add(cDesignModes *m, const char *key, const char *value)
    {
        if (original) O<void(__thiscall *)(void *, const char *, const char *)>(0x3f5360)(m, key, value);
        else m->AddDesignMode(key, value);
    }
    void SetName(cDesignModes *m, const char *text, bool strip)
    {
        if (original) O<void(__thiscall *)(void *, const char *, bool)>(0x3f6440)(m, text, strip);
        else m->SetName(text, strip);
    }
    void Rewrite(cDesignModes *m)
    {
        if (original) O<void(__thiscall *)(void *)>(0x3f5550)(m);
        else m->RewriteForLod();
    }
    void Destruct(cDesignModes *m)
    {
        if (original) O<void(__thiscall *)(void *)>(0x335a60)(m);
        else cDesignModes::Destruct(m, nullptr);
    }
    void Reset(cDesignModeNode *n)
    {
        if (original) O<void(__thiscall *)(void *)>(0x3f7a00)(n);
        else n->Reset();
    }
    bool Set(cDesignModeNode *n, void *object)
    {
        if (original) return O<bool(__thiscall *)(void *, void *)>(0x3f7e90)(n, object);
        return n->Set(static_cast<cIGZUnknown *>(object));
    }
    void *BaseName(cDesignModeNode *n, void *out)
    {
        if (original) return O<void *(__thiscall *)(void *, void *)>(0x3f8800)(n, out);
        return n->BaseName(static_cast<cMapString *>(out));
    }
    void ApplySeason(cDesignModeNode *n, void *seasons)
    {
        if (original) O<void(__thiscall *)(void *, void *)>(0x3f8a10)(n, seasons);
        else n->ApplySeason(seasons);
    }
    void ApplyTree(cDesignModeNode *n)
    {
        if (original) O<void(__thiscall *)(void *)>(0x3f8990)(n);
        else n->ApplyDesignModesToTree();
    }
    void MakeShapesUnique(void *node)
    {
        if (original) O<void(__cdecl *)(void *)>(0x3f8750)(node);
        else nTSSG::MakeShapesUnique(static_cast<nGZSceneGraph::cICompositionTreeNode *>(node));
    }
    void Bind(void *node, const cMapString *key, const cMapString *value)
    {
        if (original) O<void(__cdecl *)(void *, const void *, const void *)>(0x3f8850)(node, key, value);
        else nTSSG::BindDesignModeInTree(static_cast<nGZSceneGraph::cICompositionTreeNode *>(node), key, value);
    }
    void MakeMaterialsUnique(void *ref)
    {
        if (original) O<void(__cdecl *)(void *)>(0xae7af0)(ref);
        else nTSSG::MakeMaterialsUnique(static_cast<nGZSceneGraph::cIShapeRefNode *>(ref));
    }
};

void NoteModes(const char *what, const cDesignModes &m)
{
    sgfx::NoteText(what, m.mName.mString.c_str());
    sgfx::NoteText("unused", m.mTag.mString.c_str());
    drawfx::Note("modes", uint32_t(m.mModes.size()));
    for (const auto &e : m.mModes) {
        sgfx::NoteText("key", e.first.mString.c_str());
        sgfx::NoteText("value", e.second.mString.c_str());
        drawfx::Note("refcounts", {e.first.mRefCount, e.second.mRefCount});
    }
}

const char *MaybeNull(std::mt19937 &g, const std::string &s, unsigned oneIn) { return g() % oneIn == 0 ? nullptr : s.c_str(); }

size_t FindNoCase(const std::string &s, const char *what, size_t from = 0)
{
    const size_t n = std::strlen(what);
    for (size_t i = from; i + n <= s.size(); ++i)
        if (_strnicmp(s.c_str() + i, what, n) == 0) return i;
    return std::string::npos;
}
std::string LodOf(const std::string &s)
{
    const size_t at = FindNoCase(s, "SWX");
    return at == std::string::npos ? std::string() : s.substr(at, 5);
}
// RewriteForLod's second insertion into a value can ask for a position before the start (a value with "lod90" whose
// second '_' is fewer than five characters in, the lod empty or short): the string throws out_of_range, on both sides
// alike (the ORIGINAL-BUG note in src/sg/cLotImposterDesignModes.cpp). Names the game ships never do; those states are
// not rewritten.
bool RewriteThrows(const std::string &name, const nTSSG::cDesignModeMap &modes)
{
    if (modes.empty()) return false;
    const std::string lod = LodOf(name);
    if (_stricmp(lod.c_str(), LodOf(modes.begin()->first.mString).c_str()) == 0) return false;
    for (const auto &e : modes) {
        std::string v = e.second.mString;
        for (size_t at = FindNoCase(v, "SWX"); at != std::string::npos; at = FindNoCase(v, "SWX")) v.erase(at, 5);
        const size_t first = v.find('_');
        if (first == std::string::npos) continue;
        v.insert(first, lod);
        const size_t next = v.find('_', first + lod.size() + 1);
        if (next == std::string::npos) continue;
        const size_t back = FindNoCase(v, "lod90") != std::string::npos ? 5 : 0;
        if (next < back) return true;
    }
    return false;
}

uint32_t StripOriginal(difftest::Context &ctx, cMapString &s)
{
    return uint32_t(ctx.Original<int32_t(__cdecl *)(void *)>(0x3ac4f0)(&s));
}

} // namespace

DIFF_TEST(LotImposterModes_StripLod, 0x3ac4f0)
{
    std::mt19937 g(4242);
    for (unsigned i = 0; i < 4000; ++i) {
        std::string text = RandomText(g, 7);
        if (g() % 3 == 0) text += "SWX" + std::string(g() % 3, 'q'); // a tag cut short at the end
        cMapString a, b;
        a.mString = text;
        b.mString = text;
        const uint32_t ra = StripOriginal(ctx, a);
        const uint32_t rb = uint32_t(nTSSG::StripLod(reinterpret_cast<cIGZString *>(&b)));
        if (ra != rb || a.mString != b.mString) {
            ctx.Fail("StripLod(\"%s\"): original %u \"%s\", ours %u \"%s\"", text.c_str(), ra, a.mString.c_str(), rb,
                     b.mString.c_str());
            return;
        }
        ++ctx.cases;
    }
}

DIFF_TEST(LotImposterModes_FindLod, 0x3f5470)
{
    std::mt19937 g(777);
    for (unsigned i = 0; i < 4000; ++i) {
        std::string text = RandomText(g, 6);
        if (g() % 3 == 0) text += "sWx" + std::string(g() % 3, '5');
        const std::string before = RandomText(g, 2);
        cMapString a, b, outA, outB;
        a.mString = b.mString = text;
        outA.mString = outB.mString = before;
        const bool ra = ctx.Original<bool(__cdecl *)(void *, void *)>(0x3f5470)(&a, &outA);
        const bool rb = nTSSG::FindLod(reinterpret_cast<cIGZString *>(&b), reinterpret_cast<cIGZString *>(&outB));
        if (ra != rb || a.mString != b.mString || outA.mString != outB.mString) {
            ctx.Fail("FindLod(\"%s\"): original %d \"%s\", ours %d \"%s\"", text.c_str(), ra, outA.mString.c_str(), rb,
                     outB.mString.c_str());
            return;
        }
        ++ctx.cases;
    }
}

DIFF_TEST(LotImposterModes_DesignModes, 0x3f5550)
{
    shape::DeleteRecorder deletes;
    shape::StandIn finder(0xaeb5b0, &nGZSceneGraph::FindDataList, &FindDataList);
    unsigned loads = 0, rewrites = 0;
    sgfx::CheckSides(
        ctx, "design modes", 1500, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
        [&](bool original, std::mt19937 &g) {
            Api api{ctx, original};
            alignas(cDesignModes) unsigned char storage[sizeof(cDesignModes)];
            auto *const m = new (storage) cDesignModes();
            const unsigned steps = 1 + g() % 14;
            for (unsigned s = 0; s < steps; ++s) {
                const std::string a = RandomText(g, 4), b = RandomText(g, 5);
                switch (g() % 9) {
                case 0: api.Clear(m); break;
                case 1: case 2: case 3:
                    if (m->mModes.size() < 5) api.Add(m, MaybeNull(g, a, 10), MaybeNull(g, b, 10));
                    break;
                case 4: {
                    // Tags that also come in the keys' tokens in another case (SWX01, swx22): RewriteForLod's early
                    // out compares them ignoring case.
                    static const char *const kNameTags[] = {"", "SWX12", "swx12", "SwX01", "swx01", "SWX22"};
                    api.SetName(m, MaybeNull(g, a + kNameTags[g() % 6], 12), g() % 2 != 0);
                    break;
                }
                case 5: case 6:
                    if (RewriteThrows(m->mName.mString, m->mModes)) break;
                    api.Rewrite(m);
                    rewrites += !original;
                    break;
                default: {
                    const bool r = api.Load(m, g() % 8 == 0 ? nullptr : &gw.nodes[g() % kNodes]);
                    drawfx::Note("load", uint32_t(r));
                    loads += !original;
                }
                }
                NoteModes("name", *m);
            }
            deletes.Begin();
            api.Destruct(m);
            deletes.End();
            drawfx::Note("deletes", uint32_t(deletes.Count()));
            deletes.Release();
            FinishLog();
            ++ctx.cases;
        });
    if (loads == 0 || rewrites == 0) ctx.Fail("design modes: %u loads, %u rewrites", loads, rewrites);
}

DIFF_TEST(LotImposterModes_Node, 0x3f8a10)
{
    unsigned seasons = 0;
    shape::StandIn finder(0xaeb5b0, &nGZSceneGraph::FindDataList, &FindDataList);
    shape::CodePatch manager(0x20defb, reinterpret_cast<const void *>(&ResourceManager));
    sgfx::CheckSides(
        ctx, "design mode node", 1500, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
        [&](bool original, std::mt19937 &g) {
            Api api{ctx, original};
            alignas(cDesignModeNode) unsigned char storage[sizeof(cDesignModeNode)];
            auto *const n = new (storage) cDesignModeNode();
            const unsigned steps = 1 + g() % 10;
            for (unsigned s = 0; s < steps; ++s) {
                switch (g() % 8) {
                case 0: api.Reset(n); break;
                case 1: case 2: {
                    const unsigned pick = g() % 6;
                    void *object = pick == 0 ? nullptr : pick == 1 ? static_cast<void *>(&gw.bare) : &gw.object;
                    drawfx::Note("set", uint32_t(api.Set(n, object)));
                    break;
                }
                case 3: {
                    alignas(cMapString) unsigned char out[sizeof(cMapString)];
                    void *const r = api.BaseName(n, out);
                    drawfx::Note("base name result", uint32_t(r == out));
                    sgfx::NoteText("base name", reinterpret_cast<cMapString *>(out)->mString.c_str());
                    reinterpret_cast<cRZString *>(out)->Destruct();
                    break;
                }
                case 4: case 5:
                    if (RewriteThrows(gw.tags[4], n->mModes.mModes)) break; // the variant's name, rewritten for
                    api.ApplySeason(n, g() % 3 == 0 ? nullptr : &gw.seasons);
                    seasons += !original && n->mNode.mNode;
                    break;
                case 6: api.ApplyTree(n); break;
                default: {
                    const std::string a = RandomText(g, 3), b = RandomText(g, 4);
                    api.Add(&n->mModes, a.c_str(), b.c_str());
                }
                }
                drawfx::Note("node", drawfx::Ptr(n->mNode.mNode));
                NoteModes("name", n->mModes);
            }
            api.Destruct(&n->mModes);
            if (n->mNode.mNode) n->mNode.mNode->Release();
            FinishLog();
            ++ctx.cases;
        });
    if (seasons == 0) ctx.Fail("design mode node: no season applied to a node");
}

DIFF_TEST(LotImposterModes_Tree, 0x3f8850)
{
    unsigned binds = 0;
    sgfx::CheckSides(
        ctx, "design mode trees", 2000, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
        [&](bool original, std::mt19937 &g) {
            Api api{ctx, original};
            const unsigned steps = 1 + g() % 5;
            for (unsigned s = 0; s < steps; ++s) {
                const int at = int(g() % kNodes);
                switch (g() % 3) {
                case 0: api.MakeShapesUnique(g() % 10 == 0 ? nullptr : &gw.nodes[at]); break;
                case 1: {
                    cMapString key, value;
                    key.mString = RandomText(g, 2);
                    value.mString = RandomText(g, 3);
                    api.Bind(g() % 10 == 0 ? nullptr : &gw.nodes[at], &key, &value);
                    binds += !original;
                    break;
                }
                default: api.MakeMaterialsUnique(&gw.refs[at]);
                }
            }
            FinishLog();
            ++ctx.cases;
        });
    if (binds == 0) ctx.Fail("design mode trees: nothing bound");
}
