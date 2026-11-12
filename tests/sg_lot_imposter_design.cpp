// Differential tests for ApplyDefinition's design-mode callees (lane 261111u, scenegraph row LT2a2b;
// src/sg/cLotImposterDesignModes.h): cDesignModes::LoadFromObject 0x3f5b80 and cDesignModeNode's LoadByName 0x3f7b10,
// LoadForObject 0x3f7a50, Take 0x3f7c80 and SetFromObject 0x3f7f40.
//
// Each side runs its own chain (the original's callers call the original's members, ours call ours). Stood in for on
// both sides (shape::StandIn, the image's RVA and ours): the skin extension walk NextSkinExtension 0xaea060, DesignMode's
// ListedSlaveSubsets 0x3ab1d0, FormatMaterialName 0x3ab7a0 and CollectSubsetNames 0x3aa420, GetOrAddDataList 0xaeb3f0 and
// ApplyDesignModesToTree 0x3f8990 (it notes the node and the modes it is handed). Patched for both sides (not ours): the
// object check 0x3a97f0, the scene graph system 0x20defb (its slot 23 finds a resource node by name), and the slave
// material name builder 0x3ac300 / 0x3ac840 / 0x3ac3e0 (four strings: the material, the slave set into part 1, joined
// with '|'). The game's new / delete (0x824632 / 0x824624) are patched to the test's heap; the allocator service (cell
// 0x10c95d4, its guard set) frees the subset strings' heap buffers (slot 4).
//
// The game object is a fake with its node tag (slot 26), model (slot 120) and model name (slot 124); the model is a
// hand-made object (its slots 291 / 293 are past drawfx's table) whose count, element and owner (slot 61, whose slot 76
// gives the group word) slots note each call. The count changes after its first read, the model is gone after its first
// read in some worlds, and an element's material holder (slot 3) alternates between two holders on each call: values
// the code re-reads show. Element subsets are written by value (std::string layout, heap buffers past 15 characters).
//
// Compared: every call (ordered, with arguments), the result, the node held, the modes (tag, name, every entry and its
// strings' refcounts) after each call, every fake's final count, and the heap (blocks left, blocks allocated).
//
// LotImposterDesign_LoadFromObject: a pre-filled cDesignModes; the object null, without a model or with one.
// LotImposterDesign_LoadByName: names with level-of-detail tags, null and empty, group null or a word; a node held before.
// LotImposterDesign_LoadForObject: the formatted name found or not, then the model name.
// LotImposterDesign_Take: a mapped set with zero to four modes, or the node's own modes (the alias).
// LotImposterDesign_SetFromObject: the object check, the local load, the model found, the tree's subset names.
#include "code_patch.h"
#include "sgfx.h"

#include "app/AppServices2.h" // as2::RawString
#include "rz/cRZString.h"
#include "sg/DesignMode.h"
#include "sg/SGUtilities.h"
#include "sg/cLotImposterDesignModes.h"
#include "sg/cSkinExtension.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace lotimpdesign_test {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using nTSSG::cDesignModeNode;
using nTSSG::cDesignModes;
using nTSWorld::cMapString;

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t Hash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ uint8_t(c)) * 16777619u;
    return h;
}

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

constexpr At kObjectAt[] = {{26, {0, Ret::Int, true, "GetTag", ""}},
                            {120, {0, Ret::Int, true, "GetModel", ""}},
                            {124, {0, Ret::Int, true, "GetModelName", ""}}};
constexpr auto kObjectSlots = Table<125>(kObjectAt);
constexpr drawfx::Iface kObject{"GameObject", 125, kObjectSlots.data(), false, drawfx::kTestTable};

constexpr At kOwnerAt[] = {{76, {0, Ret::Int, true, "GroupWord", ""}}};
constexpr auto kOwnerSlots = Table<77>(kOwnerAt);
constexpr drawfx::Iface kOwner{"ModelOwner", 77, kOwnerSlots.data(), false, drawfx::kTestTable};

constexpr At kElementAt[] = {{3, {0, Ret::Int, true, "Holder", ""}}, {6, {1, Ret::Int, true, "Subset", "a"}}};
constexpr auto kElementSlots = Table<7>(kElementAt);
constexpr drawfx::Iface kElement{"ModelElement", 7, kElementSlots.data(), false, drawfx::kTestTable};

constexpr At kHolderAt[] = {{13, {0, Ret::Int, true, "Material", ""}}};
constexpr auto kHolderSlots = Table<14>(kHolderAt);
constexpr drawfx::Iface kHolder{"MaterialHolder", 14, kHolderSlots.data(), false, drawfx::kTestTable};

constexpr At kPlainAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kPlainSlots = Table<3>(kPlainAt);
constexpr drawfx::Iface kPlain{"Unknown", 3, kPlainSlots.data(), false, drawfx::kTestTable};

constexpr At kSystemAt[] = {{23, {6, Ret::Int, true, "FindByName", "sW1uuau"}}};
constexpr auto kSystemSlots = Table<24>(kSystemAt);
constexpr drawfx::Iface kSystem{"SGSystem", 24, kSystemSlots.data(), false, drawfx::kTestTable};

constexpr At kAllocatorAt[] = {{4, {1, Ret::Int, true, "Free", "a"}}};
constexpr auto kAllocatorSlots = Table<5>(kAllocatorAt);
constexpr drawfx::Iface kAllocator{"Allocator", 5, kAllocatorSlots.data(), false, drawfx::kTestTable};

constexpr At kListAt[] = {{6, {2, Ret::Int, true, "SetString", "ss"}}, {11, {0, Ret::Int, true, "Clear", ""}}};
constexpr auto kListSlots = Table<12>(kListAt);
constexpr drawfx::Iface kList{"DataList", 12, kListSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kAllocatorCell = 0x10c95d4, kAllocatorGuard = 0x10c95d8;
constexpr int kElements = 4, kExtensions = 3, kNodes = 2;

// Texts with level-of-detail tags ("SWX" and two characters, any case), the empty text and the 15 / 16 character
// boundary of the string's inline buffer.
const char *const kNames[] = {"",          "chair",           "chairSWX01",       "Chair_swx22_cres", "tableSWX9",
                              "fifteen_chars_x", "sixteen_chars_xx", "a_much_longer_model_nameSWX01x"};
const char *const kSubsets[] = {"",     "seat",            "Seat",             "frameSWX01",
                                "legs", "fifteen_chars_x", "sixteen_chars_xx", "a_much_longer_subset_name"};
const char *const kMaterials[] = {"", "wood", "wood_dark_SWX01", "fifteen_chars_x", "a_much_longer_material_name"};
const char *const kSlaves[] = {"seat", "SEAT", "legsSWX01", "legs", "sixteen_chars_xx", "", "armswx22", "frame"};
const char *const kTags[] = {"", "objtag", "ObjTagLongerThanSixteen"};

struct ModelObject {
    void **vt;
};
struct ListFace {
    void *vt;              // +0: the list fake's embedded table
    const int32_t *vbptr;  // +4: entry 1 the offset from here to the virtual base (AddRef / Release)
};

struct World {
    Fake object, owner, elements[kElements], holders[2 * kElements], extensions[kExtensions], nodes[kNodes];
    Fake system, allocator, list, listBase;
    ModelObject model;
    void *modelVt[294];
    ListFace listFace;
    int32_t vbtable[2];
    std::string tag, name, subsets[kElements], materials[2 * kElements], formatted;
    bool nameNull = false, hasModel = true, modelGone = false, systemNull = false, accept = true;
    bool elementNull[kElements], hasHolder[kElements];
    uint32_t count = 0, countCalls = 0, modelCalls = 0, group = 0, salt = 0;
    int extensionCount = 0;
    std::vector<std::string> collected; // the names CollectSubsetNames gives
    bool ours = false;
};
World gw;

int IndexOf(const Fake &self, const Fake *array, int count)
{
    for (int i = 0; i < count; ++i)
        if (&array[i] == &self) return i;
    return -1;
}

void NoteText(const char *label, const char *text)
{
    difftest::NotCounted bookkeeping;
    sgfx::NoteText(label, text);
}

// ---- the fakes' handlers ----

uint32_t ObjectTag(Fake &, const uint32_t *) { return drawfx::P(gw.tag.c_str()); }
uint32_t ObjectModel(Fake &, const uint32_t *)
{
    const bool gone = gw.modelGone && gw.modelCalls > 0;
    ++gw.modelCalls;
    return gw.hasModel && !gone ? drawfx::P(&gw.model) : 0;
}
uint32_t ObjectName(Fake &, const uint32_t *) { return gw.nameNull ? 0 : drawfx::P(gw.name.c_str()); }

// The holder changes on every call (two holders per element).
uint32_t ElementHolder(Fake &self, const uint32_t *)
{
    const int i = IndexOf(self, gw.elements, kElements);
    const uint32_t turn = self.value[0]++;
    return gw.hasHolder[i] ? drawfx::P(&gw.holders[2 * i + int(turn % 2)]) : 0;
}
// The subset by value: the std::string's layout written into the caller's out, a heap buffer past 15 characters.
uint32_t ElementSubset(Fake &self, const uint32_t *a)
{
    const int i = IndexOf(self, gw.elements, kElements);
    auto *const out = reinterpret_cast<as2::RawString *>(uintptr_t(a[0]));
    const std::string &t = gw.subsets[i];
    out->mSize = uint32_t(t.size());
    if (t.size() > 15) {
        out->mCapacity = uint32_t(t.size());
        out->mPtr = static_cast<char *>(::operator new(t.size() + 1));
        std::memcpy(out->mPtr, t.c_str(), t.size() + 1);
    } else {
        out->mCapacity = 15;
        std::memcpy(out->mBuf, t.c_str(), t.size() + 1);
    }
    return a[0];
}
uint32_t HolderMaterial(Fake &self, const uint32_t *)
{
    const int h = IndexOf(self, gw.holders, 2 * kElements);
    return drawfx::P(gw.materials[h].c_str());
}
uint32_t AllocatorFree(Fake &, const uint32_t *a)
{
    ::operator delete(reinterpret_cast<void *>(uintptr_t(a[0])));
    return 0;
}
// Found when the text's hash (with the world's salt) says so; one of the two nodes, counted.
uint32_t FindByName(Fake &, const uint32_t *a)
{
    const std::string text = reinterpret_cast<const char *>(uintptr_t(a[0]));
    const uint32_t h = Hash(text) ^ gw.salt;
    if (h % 3 == 0) return 0;
    Fake &node = gw.nodes[(h >> 4) % kNodes];
    ++node.refs;
    *reinterpret_cast<void **>(uintptr_t(a[4])) = &node;
    return 1;
}

// The model (hand-made: slots 291 / 293 are past drawfx's tables). Each call noted.
void *__fastcall ModelOwner(void *, void *)
{
    drawfx::Note("model owner", 0u);
    return &gw.owner;
}
uint32_t __fastcall ModelCount(void *, void *)
{
    const uint32_t calls = gw.countCalls++;
    const uint32_t n = calls == 0 || gw.count == 0 ? gw.count : gw.count - 1; // changes after the first read
    drawfx::Note("model count", {calls, n});
    return n;
}
void *__fastcall ModelElement(void *, void *, uint32_t i)
{
    drawfx::Note("model element", i);
    return i < uint32_t(kElements) && !gw.elementNull[i] ? &gw.elements[i] : nullptr;
}

// ---- patched callees (not ours) ----

bool __cdecl AcceptObject(cIGZUnknown *object, int32_t lod)
{
    drawfx::Note("accept object", {drawfx::Ptr(object), uint32_t(lod)});
    return gw.accept;
}
void *__cdecl SceneGraphSystem()
{
    drawfx::Note("scene graph system", 0u);
    return gw.systemNull ? nullptr : &gw.system;
}
cRZString *Part(void *parts, int i) { return reinterpret_cast<cRZString *>(static_cast<uint8_t *>(parts) + i * 0x20); }
void __fastcall PartsBuild(void *parts, void *, const char *material)
{
    NoteText("parts from", material);
    Part(parts, 0)->Construct(material);
    Part(parts, 1)->Construct("#");
    Part(parts, 2)->Construct("");
    Part(parts, 3)->Construct("tail_text_over_sixteen");
}
bool __fastcall PartsSet(void *parts, void *, int32_t which, const char *text)
{
    drawfx::Note("parts set", uint32_t(which));
    NoteText("parts set text", text);
    Part(parts, which)->FromChar(text);
    return true;
}
cRZString *__fastcall PartsJoin(void *parts, void *, cRZString *out)
{
    std::string joined;
    for (int i = 0; i < 4; ++i) joined += (i ? "|" : "") + Part(parts, i)->mString;
    NoteText("parts joined", joined.c_str());
    out->Construct(joined.c_str());
    return out;
}

// ---- stand-ins for our callees (both sides) ----

void NoteModes(const char *what, const cDesignModes &m)
{
    NoteText(what, m.mName.mString.c_str());
    NoteText("tag", m.mTag.mString.c_str());
    drawfx::Note("modes", uint32_t(m.mModes.size()));
    for (const auto &e : m.mModes) {
        NoteText("key", e.first.mString.c_str());
        NoteText("value", e.second.mString.c_str());
        drawfx::Note("refcounts", {e.first.mRefCount, e.second.mRefCount});
    }
}

nGZSceneGraph::cISkinExtension *__cdecl NextExtension(nGZSceneGraph::cIObjectGraphNode *node,
                                                      nGZSceneGraph::cISkinExtension *previous)
{
    drawfx::Note("next extension", {drawfx::Ptr(node), previous ? drawfx::Ptr(previous) : 0u});
    const int at = previous ? IndexOf(*reinterpret_cast<Fake *>(previous), gw.extensions, kExtensions) + 1 : 0;
    return at < gw.extensionCount ? reinterpret_cast<nGZSceneGraph::cISkinExtension *>(&gw.extensions[at]) : nullptr;
}
void ListedSlaves(cIGZUnknown *shapes, const char *subset, DesignMode::SubsetList *out)
{
    drawfx::Note("listed slaves", {drawfx::Ptr(shapes), uint32_t(out->size())});
    NoteText("listed slaves subset", subset);
    const int e = IndexOf(*reinterpret_cast<Fake *>(shapes), gw.extensions, kExtensions);
    const uint32_t h = Hash(subset ? subset : "") + uint32_t(e) * 7u + gw.salt;
    for (uint32_t k = 0; k < h % 4; ++k) {
        cRZString &s = out->emplace_back();
        s.mVTable = cRZString::VTable();
        s.mString = kSlaves[(h / 4 + k * 3) % std::size(kSlaves)];
        s.mRefCount = 0x40 + k;
    }
}
cRZString *FormatName(cRZString *out, cIGZUnknown *object, int32_t lod, bool cres)
{
    drawfx::Note("format material name", {drawfx::Ptr(object), uint32_t(lod), uint32_t(cres)});
    out->Construct(gw.formatted.c_str());
    return out;
}
void CollectNames(nGZSceneGraph::cICompositionTreeNode *node, DesignMode::SubsetNameSet *names)
{
    drawfx::Note("collect subset names", {drawfx::Ptr(node), uint32_t(names->size())});
    for (const std::string &text : gw.collected) {
        cRZString s;
        s.mVTable = cRZString::VTable();
        s.mString = text;
        s.mRefCount = 0;
        names->insert(s);
    }
}
nGZSceneGraph::cIDataListExtension *GetList(nGZSceneGraph::cICompositionTreeNode *node, const char *name, bool reuse)
{
    drawfx::Note("get or add data list", {drawfx::Ptr(node), uint32_t(reuse)});
    NoteText("data list name", name);
    return reinterpret_cast<nGZSceneGraph::cIDataListExtension *>(&gw.listFace);
}
void __fastcall ApplyTree(cDesignModeNode *self, void *)
{
    drawfx::Note("apply to tree", self->mNode.mNode ? drawfx::Ptr(self->mNode.mNode) : 0u);
    NoteModes("applied name", self->mModes);
}

// ---- the world ----

void BuildWorld(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 g(seed * 2654435761u + 91u);
    auto pick = [&](auto &pool) { return std::string(pool[g() % std::size(pool)]); };
    gw.object.Reset<kObject>(710);
    gw.object.On(26, &ObjectTag);
    gw.object.On(120, &ObjectModel);
    gw.object.On(124, &ObjectName);
    gw.owner.Reset<kOwner>(711);
    gw.group = g() % 3 == 0 ? 0u : 0x1000u + g() % 50;
    gw.owner.Returns(76, gw.group);
    for (int i = 0; i < kElements; ++i) {
        gw.elements[i].Reset<kElement>(720 + i);
        gw.elements[i].On(3, &ElementHolder);
        gw.elements[i].On(6, &ElementSubset);
        gw.elementNull[i] = g() % 6 == 0;
        gw.hasHolder[i] = g() % 5 != 0;
        gw.subsets[i] = pick(kSubsets);
        for (int k = 0; k < 2; ++k) {
            gw.holders[2 * i + k].Reset<kHolder>(730 + 2 * i + k);
            gw.holders[2 * i + k].On(13, &HolderMaterial);
            gw.materials[2 * i + k] = pick(kMaterials);
        }
    }
    for (int i = 0; i < kExtensions; ++i) gw.extensions[i].Reset<kPlain>(740 + i);
    for (int i = 0; i < kNodes; ++i) gw.nodes[i].Reset<kPlain>(700 + i);
    gw.system.Reset<kSystem>(750);
    gw.system.On(23, &FindByName);
    gw.allocator.Reset<kAllocator>(751);
    gw.allocator.On(4, &AllocatorFree);
    gw.list.Reset<kList>(752);
    gw.listBase.Reset<kPlain>(753);
    gw.list.Embed(&gw.listFace);
    gw.vbtable[0] = -4;
    gw.vbtable[1] = int32_t(drawfx::P(&gw.listBase) - drawfx::P(&gw.listFace.vbptr));
    gw.listFace.vbptr = gw.vbtable;

    for (void *&slot : gw.modelVt) slot = nullptr;
    gw.modelVt[61] = reinterpret_cast<void *>(&ModelOwner);
    gw.modelVt[291] = reinterpret_cast<void *>(&ModelCount);
    gw.modelVt[293] = reinterpret_cast<void *>(&ModelElement);
    gw.model.vt = gw.modelVt;
    drawfx::AddRange(&gw.model, sizeof(gw.model), 0x5d000000);

    gw.tag = pick(kTags);
    gw.name = pick(kNames);
    gw.nameNull = g() % 10 == 0;
    gw.hasModel = g() % 6 != 0;
    gw.modelGone = g() % 5 == 0;
    gw.systemNull = g() % 10 == 0;
    gw.accept = g() % 6 != 0;
    gw.count = g() % (kElements + 2); // past the elements: null ones
    gw.countCalls = gw.modelCalls = 0;
    gw.salt = g();
    gw.extensionCount = int(g() % (kExtensions + 1));
    gw.formatted = g() % 6 == 0 ? std::string() : pick(kNames) + (g() % 2 ? "_cres" : "");
    gw.collected.clear();
    for (const char *s : kSubsets)
        if (g() % 2) gw.collected.push_back(s);
    for (const char *s : kSlaves)
        if (g() % 2) {
            std::string t = s;
            if (g() % 3 == 0)
                for (char &c : t) c = char(c >= 'a' && c <= 'z' ? c - 32 : c); // the case-blind set
            gw.collected.push_back(t);
        }
    cells.Set(kAllocatorCell, &gw.allocator);
    cells.Set(kAllocatorGuard, 0x80000000u);
}

void FinishLog()
{
    drawfx::Note("object refs", {uint32_t(gw.object.refs), uint32_t(gw.owner.refs)});
    for (int i = 0; i < kElements; ++i)
        drawfx::Note("element refs", {uint32_t(gw.elements[i].refs), uint32_t(gw.holders[2 * i].refs),
                                      uint32_t(gw.holders[2 * i + 1].refs)});
    for (int i = 0; i < kExtensions; ++i) drawfx::Note("extension refs", uint32_t(gw.extensions[i].refs));
    for (int i = 0; i < kNodes; ++i) drawfx::Note("node refs", uint32_t(gw.nodes[i].refs));
    drawfx::Note("other refs", {uint32_t(gw.system.refs), uint32_t(gw.allocator.refs), uint32_t(gw.list.refs),
                                uint32_t(gw.listBase.refs)});
}

struct Api {
    difftest::Context &ctx;
    bool original;

    template <class Fn> Fn O(uint32_t rva) { return ctx.Original<Fn>(rva); }
    bool LoadFromObject(cDesignModes *m, void *object)
    {
        if (original) return O<bool(__thiscall *)(void *, void *)>(0x3f5b80)(m, object);
        return m->LoadFromObject(static_cast<cIGZUnknown *>(object));
    }
    bool LoadByName(cDesignModeNode *n, const char *name, const void *group)
    {
        if (original) return O<bool(__thiscall *)(void *, const char *, const void *)>(0x3f7b10)(n, name, group);
        return n->LoadByName(name, group);
    }
    bool LoadForObject(cDesignModeNode *n, void *object, int32_t lod, const void *group)
    {
        if (original)
            return O<bool(__thiscall *)(void *, void *, int32_t, const void *)>(0x3f7a50)(n, object, lod, group);
        return n->LoadForObject(static_cast<cIGZUnknown *>(object), lod, group);
    }
    bool Take(cDesignModeNode *n, cDesignModes *mapped)
    {
        if (original) return O<bool(__thiscall *)(void *, void *)>(0x3f7c80)(n, mapped);
        return n->Take(mapped);
    }
    bool SetFromObject(cDesignModeNode *n, void *object)
    {
        if (original) return O<bool(__thiscall *)(void *, void *)>(0x3f7f40)(n, object);
        return n->SetFromObject(static_cast<cIGZUnknown *>(object));
    }
    void Destruct(cDesignModes *m)
    {
        if (original) O<void(__thiscall *)(void *)>(0x335a60)(m);
        else cDesignModes::Destruct(m, nullptr);
    }
};

// Modes from the pools (the same on both sides: our verified AddDesignMode / SetName).
void Fill(cDesignModes &m, std::mt19937 &g, unsigned count)
{
    reinterpret_cast<cRZString *>(&m.mTag)->FromChar(kTags[g() % std::size(kTags)]);
    m.SetName(kNames[g() % std::size(kNames)], false);
    for (unsigned i = 0; i < count; ++i) {
        const std::string key = g() % 2 ? kSubsets[g() % std::size(kSubsets)] : kSlaves[g() % std::size(kSlaves)];
        m.AddDesignMode(key.c_str(), kMaterials[g() % std::size(kMaterials)]);
    }
    uint32_t r = 0x500;
    for (auto &e : m.mModes) {
        const_cast<cMapString &>(e.first).mRefCount = r++;
        e.second.mRefCount = r++;
    }
}

// A node holding one of the node fakes (counted) or none, with filled modes.
cDesignModeNode *MakeNode(void *storage, std::mt19937 &g)
{
    auto *const n = new (storage) cDesignModeNode();
    if (g() % 2) {
        Fake &node = gw.nodes[g() % kNodes];
        ++node.refs;
        n->mNode.mNode = reinterpret_cast<nGZSceneGraph::cIResourceNode *>(&node);
    }
    Fill(n->mModes, g, g() % 4);
    return n;
}
void NoteNode(const char *what, const cDesignModeNode &n, bool result)
{
    drawfx::Note(what, uint32_t(result));
    drawfx::Note("node", n.mNode.mNode ? drawfx::Ptr(n.mNode.mNode) : 0u);
    NoteModes("name", n.mModes);
}
void EndNode(Api &api, cDesignModeNode *n)
{
    api.Destruct(&n->mModes);
    if (n->mNode.mNode) n->mNode.mNode->Release();
}

// Every stand-in and patch, alive for the test.
struct Patches {
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624, shape::CodeOf(&GameDelete)};
    shape::CodePatch accept{0x3a97f0, shape::CodeOf(&AcceptObject)};
    shape::CodePatch system{0x20defb, shape::CodeOf(&SceneGraphSystem)};
    shape::CodePatch partsBuild{0x3ac300, shape::CodeOf(&PartsBuild)};
    shape::CodePatch partsSet{0x3ac840, shape::CodeOf(&PartsSet)};
    shape::CodePatch partsJoin{0x3ac3e0, shape::CodeOf(&PartsJoin)};
    shape::StandIn next{0xaea060, &nGZSceneGraph::NextSkinExtension, &NextExtension};
    shape::StandIn slaves{0x3ab1d0, &DesignMode::ListedSlaveSubsets, &ListedSlaves};
    shape::StandIn format{0x3ab7a0, &DesignMode::FormatMaterialName, &FormatName};
    shape::StandIn collect{0x3aa420, &DesignMode::CollectSubsetNames, &CollectNames};
    shape::StandIn list{0xaeb3f0, &nGZSceneGraph::GetOrAddDataList, &GetList};
    shape::StandIn apply{0x3f8990, &cDesignModeNode::ApplyDesignModesToTree, &ApplyTree};
};

// Runs `body` on both sides for `seeds` worlds, the heap noted at the end.
template <class Body> void Check(difftest::Context &ctx, const char *what, unsigned seeds, Body &&body)
{
    Patches patches;
    sgfx::CheckSides(ctx, what, seeds, [&](uint32_t seed, drawfx::Cells &cells) { BuildWorld(seed, cells); },
                     [&](bool original, std::mt19937 &g) {
                         gw.ours = !original;
                         Api api{ctx, original};
                         difftest::AllocationCounter heap;
                         body(api, g);
                         drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Allocations())});
                         FinishLog();
                         ++ctx.cases;
                     });
}

} // namespace lotimpdesign_test

using namespace lotimpdesign_test;

DIFF_TEST(LotImposterDesign_LoadFromObject, 0x3f5b80)
{
    unsigned full = 0, slaves = 0, failed = 0;
    Check(ctx, "load from object", 1500, [&](Api &api, std::mt19937 &g) {
        alignas(cDesignModes) unsigned char storage[sizeof(cDesignModes)];
        auto *const m = new (storage) cDesignModes();
        Fill(*m, g, g() % 4);
        void *const object = g() % 10 == 0 ? nullptr : &gw.object;
        const bool r = api.LoadFromObject(m, object);
        drawfx::Note("result", uint32_t(r));
        NoteModes("name", *m);
        if (gw.ours) {
            failed += !r;
            full += r && m->mModes.size() > 1;
            slaves += r && gw.extensionCount > 0 && m->mModes.size() > 2;
        }
        api.Destruct(m);
    });
    if (full < 300 || slaves < 150 || failed < 100)
        ctx.Fail("load from object: %u with modes, %u with slaves, %u failed", full, slaves, failed);
}

DIFF_TEST(LotImposterDesign_LoadByName, 0x3f7b10)
{
    unsigned found = 0, missed = 0;
    Check(ctx, "load by name", 1500, [&](Api &api, std::mt19937 &g) {
        alignas(cDesignModeNode) unsigned char storage[sizeof(cDesignModeNode)];
        cDesignModeNode *const n = MakeNode(storage, g);
        const unsigned steps = 1 + g() % 3;
        for (unsigned s = 0; s < steps; ++s) {
            const unsigned pick = g() % (std::size(kNames) + 1);
            const char *const name = pick == std::size(kNames) ? nullptr : kNames[pick];
            const uint32_t word = 0x2000 + g() % 9;
            const bool r = api.LoadByName(n, name, g() % 3 == 0 ? nullptr : &word);
            NoteNode("load by name", *n, r);
            if (gw.ours) found += r, missed += !r;
        }
        EndNode(api, n);
    });
    if (found < 500 || missed < 300) ctx.Fail("load by name: %u found, %u missed", found, missed);
}

DIFF_TEST(LotImposterDesign_LoadForObject, 0x3f7a50)
{
    unsigned found = 0, missed = 0;
    Check(ctx, "load for object", 1500, [&](Api &api, std::mt19937 &g) {
        alignas(cDesignModeNode) unsigned char storage[sizeof(cDesignModeNode)];
        cDesignModeNode *const n = MakeNode(storage, g);
        const unsigned steps = 1 + g() % 2;
        for (unsigned s = 0; s < steps; ++s) {
            static const int32_t kLods[] = {0x5a, 0, 3, -1};
            const uint32_t word = 0x3000 + g() % 9;
            const bool r = api.LoadForObject(n, &gw.object, kLods[g() % 4], g() % 3 == 0 ? nullptr : &word);
            NoteNode("load for object", *n, r);
            if (gw.ours) found += r, missed += !r;
        }
        EndNode(api, n);
    });
    if (found < 500 || missed < 100) ctx.Fail("load for object: %u found, %u missed", found, missed);
}

DIFF_TEST(LotImposterDesign_Take, 0x3f7c80)
{
    unsigned taken = 0, aliased = 0, failed = 0;
    Check(ctx, "take", 1500, [&](Api &api, std::mt19937 &g) {
        alignas(cDesignModeNode) unsigned char storage[sizeof(cDesignModeNode)];
        cDesignModeNode *const n = MakeNode(storage, g);
        alignas(cDesignModes) unsigned char mappedStorage[sizeof(cDesignModes)];
        auto *const own = new (mappedStorage) cDesignModes();
        Fill(*own, g, g() % 5);
        const bool alias = g() % 8 == 0;
        cDesignModes *const mapped = alias ? &n->mModes : own;
        const bool r = api.Take(n, mapped);
        NoteNode("take", *n, r);
        NoteModes("mapped name", *own);
        if (gw.ours) taken += r && n->mModes.mModes.size() > 0, aliased += alias, failed += !r;
        api.Destruct(own);
        EndNode(api, n);
    });
    if (taken < 300 || aliased < 100 || failed < 100)
        ctx.Fail("take: %u taken with modes, %u aliased, %u failed", taken, aliased, failed);
}

DIFF_TEST(LotImposterDesign_SetFromObject, 0x3f7f40)
{
    unsigned applied = 0, failed = 0;
    Check(ctx, "set from object", 2000, [&](Api &api, std::mt19937 &g) {
        alignas(cDesignModeNode) unsigned char storage[sizeof(cDesignModeNode)];
        cDesignModeNode *const n = MakeNode(storage, g);
        void *const object = g() % 12 == 0 ? nullptr : &gw.object;
        const bool r = api.SetFromObject(n, object);
        NoteNode("set from object", *n, r);
        if (gw.ours) applied += r && n->mModes.mModes.size() > 0, failed += !r;
        EndNode(api, n);
    });
    if (applied < 200 || failed < 300) ctx.Fail("set from object: %u applied, %u failed", applied, failed);
}
