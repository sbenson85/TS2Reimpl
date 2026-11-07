// Differential tests for cTSWantTreeLoader and cTSWantPropertySetLoader (lane IL, src/ts/cTSWantTreeLoader.h).
//
// WantTreeLoader_Load (0x5550c0): LoadTree end to end on a tree manager stand-in (its tree map at +0x34, std::map<uint32_t,
// counted node>). The resource manager, record, stream, framework and parser are recording fakes; the parser's Parse replays
// a random element script onto its current handler (slot 8 sets it, slot 14 pushes it, slot 15 pops it, as a property set
// read hands the end tag back), so the original side runs the game's loader (vtable 0xdd43c0: StartElement 0x553210,
// EndElement 0x552960, Release 0x5560b0; the game's property set loader, vtables 0xdd4370 / 0xdd438c, over the image's
// cGZPropertySetSerializerXmlString handlers and a game-built cGZPropertySetString) and our side ours. The nodes are ours on
// both sides (the original's constructor 0x5b2750 patched to our adapter, as the hook does in game); GetDefinition goes to a
// fake app singleton whose want manager answers a definition fake or nothing by the plan. Compared: every fake call
// (pointers normalised: nodes by their place in the trees, handlers by role), the handler's return values, the loader's
// fields and every node the trees reach (all fields, maps, counts, parent, children) after every event (+0x04..+0x44; the
// property set loader's +0x08 .. +0x60 while there is one), the manager's trees, the unknown-tag maps (the
// image's function-local statics 0x10db040 / 0x10db04c against ours) and the two version flags (0x10db03c / 0x10db03d).
// Scripts start with an <ads> element: the original's constructor (0x551bd0) leaves +0x0c..+0x28 unset, and an <ads> start
// writes all of them (0x5537de..0x5537f7); before it a </bool> or </range> reads them.
// The CRT's strtol / strtoul / atoi and ParseHex32 jump to the library's and our verified ParseHex32 (their image copies use
// the CRT's locale through imports); atexit 0x824cbe is a no-op.
//
// WantTreeLoader_Adapters: the original LoadTree against the original LoadTree with the loader's constructor, Reset and slots
// patched to our adapters (the loaders cTSWantTreeManager::vf8 builds with the game's vtable).
// WantTreeLoader_Slots: QueryInterface, AddRef, slots 5-11, Reset and Release on a game-built loader against ours; the
// constructor's fields.
//
// Covered through callers and vtables in WantTreeLoader_Load / _Adapters (every property set element): the property set
// loader's handler face (vtable 0xdd438c) slot 4 EndElement 0x5528f0, and its deleting destructor 0x552220 (vtable 0xdd4370,
// reached when the tree loader drops the property loader).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPropertySet.h"
#include "misc/HexParse.h"
#include "rz/cRZString.h"
#include "ts/cTSWantNode.h"
#include "ts/cTSWantTreeLoader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace wanttree {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{11, "d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using Loader = cTSWantTreeLoader;

uint32_t Hash(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ uint8_t(s[i])) * 16777619u;
    return h;
}
uint32_t Hash(const std::string &s) { return Hash(s.data(), s.size()); }
const char *Text(uint32_t cell) { return *GameAddress<const char *>(cell); }

enum : unsigned { kResources = 0, kRecord = 1, kStream = 2, kFramework = 3, kParser = 4, kOther0 = 5 };
constexpr uint32_t kResourceCell = 0x10cc2d8; // 0x20deb5's cache
constexpr uint32_t kMarkBuffer = 0xb0f00000, kMarkHandler = 0xb0f10000;

// ---- the plan of one case ----
struct Event {
    int kind; // 0 start, 1 end, 2 characters
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
};
struct Plan {
    bool recordSet, found, qiOk, qiSetsOut, createOk, createSetsOut;
    uint32_t streamSize;
    std::vector<Event> script;
    uint32_t keepMask;                                // the want manager's answers (a definition or none) by call
    uint32_t objectType;                              // the definition's object type (slot 16)
    std::vector<std::pair<uint32_t, bool>> preTrees;  // trees the manager has already (id, whether a node is there)
};
Plan gPlan;

// ---- the tree manager stand-in ----
struct Manager {
    alignas(8) uint8_t bytes[0x40];
    std::map<uint32_t, void *> &Trees() { return *reinterpret_cast<std::map<uint32_t, void *> *>(bytes + 0x34); }
};
Manager *gManager;

// ---- the nodes: ours on both sides (ts/cTSWantNode.h; the original side's constructor 0x5b2750 is patched to our adapter) ----
// A node is named by its place in the manager's trees (pre-order over the trees in id order); one no tree reaches is 0xeeee.
// GetDefinition (slot 14, and SetProperties when a set has no subjectType) asks the app singleton (cell 0x10cc288, a fake)
// for its want manager (slot 4, a fake), whose slot 23 answers a definition fake or nothing by the plan's keep mask.
enum : unsigned { kApp = 10, kWants = 11 };
// The definition fake sits on a 256-byte boundary: a pointer whose low byte is 0 (the loader tests the whole pointer).
alignas(256) svcfx::Obj gDefinition;
constexpr uint32_t kAppCell = 0x10cc288;
cTSWantNode *AsNode(void *p) { return static_cast<cTSWantNode *>(static_cast<cITSWantNode *>(p)); }

void Walk(cTSWantNode *node, std::vector<cTSWantNode *> &out)
{
    out.push_back(node);
    for (const WantNodeRef &child : node->mChildren) Walk(AsNode(child.Get()), out);
}
std::vector<cTSWantNode *> Reachable()
{
    std::vector<cTSWantNode *> out;
    if (gManager)
        for (auto &[id, node] : gManager->Trees())
            if (node) Walk(AsNode(node), out);
    return out;
}
uint32_t Idx(const void *p)
{
    if (!p) return 0xffff;
    const std::vector<cTSWantNode *> nodes = Reachable();
    for (size_t i = 0; i < nodes.size(); ++i)
        if (static_cast<cITSWantNode *>(nodes[i]) == p) return uint32_t(i);
    return 0xeeee;
}

uint32_t WantsDefinition(Obj *self, uint32_t *)
{
    const uint32_t call = self->value[0]++;
    return (gPlan.keepMask >> (call % 32)) & 1 ? svcfx::P(&gDefinition) : 0;
}

// ---- the parser: replays the script onto its current handler ----
void *gHandler;
void *gTreeLoader; // the handler slot 8 was first given
std::vector<void *> gHandlerStack;
uint8_t *gBuffer;

uint32_t HandlerMark(const void *h)
{
    if (!h) return 0;
    if (h == gTreeLoader) return 1;
    return 2;
}

void DigestLoader(Blob &d)
{
    if (!gTreeLoader) return;
    auto *l = static_cast<uint8_t *>(gTreeLoader);
    d.push_back(0x720);
    d.push_back(*reinterpret_cast<uint32_t *>(l + 4) != 0);
    d.push_back(*reinterpret_cast<void **>(l + 8) == &F(kParser));
    for (uint32_t off = 0xc; off <= 0x34; off += 4) {
        uint32_t w = *reinterpret_cast<uint32_t *>(l + off);
        if (off == 0x14 || off == 0x1c) w &= 0xff; // bools: the original writes the byte only
        d.push_back(w);
    }
    d.push_back(l[0x38]);
    d.push_back(l[0x39]);
    void *const props = *reinterpret_cast<void **>(l + 0x3c);
    void *const set = *reinterpret_cast<void **>(l + 0x40);
    d.push_back(props != nullptr);
    d.push_back(set != nullptr);
    d.push_back(Idx(*reinterpret_cast<void **>(l + 0x44)));
    if (props) {
        auto *p = static_cast<uint8_t *>(props);
        d.push_back(0x721);
        d.push_back(*reinterpret_cast<uint32_t *>(p + 8));    // refcount
        d.push_back(*reinterpret_cast<uint32_t *>(p + 0xc));  // state
        d.push_back(*reinterpret_cast<uint32_t *>(p + 0x50)); // value type
        d.push_back(*reinterpret_cast<void **>(p + 0x54) != nullptr);
        d.push_back(*reinterpret_cast<void **>(p + 0x58) == set);
        d.push_back(*reinterpret_cast<void **>(p + 0x5c) == &F(kParser));
        d.push_back(*reinterpret_cast<void **>(p + 0x60) == gTreeLoader);
    }
    if (set) d.push_back(VirtualCall<uint32_t(__thiscall *)(void *), 11>(set)), d.push_back(uint32_t(*reinterpret_cast<int32_t *>(static_cast<uint8_t *>(set) + 4)));
}

void DigestNodes(Blob &d);

uint32_t ParserSetHandler(Obj *, uint32_t *args)
{
    gHandler = reinterpret_cast<void *>(uintptr_t(args[0]));
    if (!gTreeLoader && gHandler) {
        // The parser keeps its first handler (the tree loader) alive until the parser goes (ParserRelease).
        gTreeLoader = gHandler;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(gTreeLoader);
    }
    svcfx::gEv.back() = kMarkHandler + HandlerMark(gHandler);
    return 1;
}
// The parser's own count (it starts at the framework's one reference); at 0 it lets go of the tree loader, so a loader
// still holding the parser is never freed (LoadTree's Reset breaks that cycle).
uint32_t ParserRelease(Obj *self, uint32_t *)
{
    if (--self->refs == 0 && gTreeLoader) {
        void *const loader = gTreeLoader;
        gTreeLoader = nullptr;
        Ev(0x7a0);
        Ev(VirtualCall<uint32_t(__thiscall *)(void *), 2>(loader));
    }
    return uint32_t(self->refs);
}

uint32_t ParserPush(Obj *, uint32_t *args)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(args[0]));
    svcfx::gEv.back() = Hash(name, std::strlen(name));
    gHandlerStack.push_back(gHandler);
    return 1;
}
uint32_t ParserPop(Obj *, uint32_t *)
{
    if (!gHandlerStack.empty()) gHandler = gHandlerStack.back(), gHandlerStack.pop_back();
    Ev(HandlerMark(gHandler));
    return 1;
}
uint32_t ParserParse(Obj *, uint32_t *args)
{
    if (reinterpret_cast<uint8_t *>(uintptr_t(args[0])) == gBuffer) svcfx::gEv[svcfx::gEv.size() - 3] = kMarkBuffer;
    for (const Event &e : gPlan.script) {
        Ev(0x540);
        Ev(HandlerMark(gHandler));
        if (!gHandler) continue;
        if (e.kind == 0) {
            std::vector<const char *> attrs;
            for (auto &[k, v] : e.attrs) attrs.push_back(k.c_str()), attrs.push_back(v.c_str());
            attrs.push_back(nullptr);
            attrs.push_back(nullptr);
            const uint32_t count = uint32_t(e.attrs.size() * 2);
            Ev(VirtualCall<bool(__thiscall *)(void *, const char *, const char **, uint32_t, uint32_t), 3>(
                gHandler, e.name.c_str(), attrs.data(), count, 0x77));
        } else if (e.kind == 1) {
            Ev(VirtualCall<bool(__thiscall *)(void *, const char *), 4>(gHandler, e.name.c_str()));
        } else {
            Ev(VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t), 5>(gHandler, e.name.c_str(),
                                                                                   uint32_t(e.name.size())));
        }
        DigestLoader(svcfx::gEv);
        DigestNodes(svcfx::gEv);
    }
    return 1;
}

uint32_t ResourcesFind(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    if (gPlan.recordSet) *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(kRecord);
    return gPlan.found;
}
uint32_t RecordQuery(Obj *, uint32_t *args)
{
    if (gPlan.qiOk || gPlan.qiSetsOut) *reinterpret_cast<void **>(uintptr_t(args[1])) = &F(kStream);
    return gPlan.qiOk;
}
uint32_t StreamRead(Obj *, uint32_t *args)
{
    gBuffer = reinterpret_cast<uint8_t *>(uintptr_t(args[0]));
    svcfx::gEv[svcfx::gEv.size() - 2] = kMarkBuffer;
    std::memset(gBuffer, 0x5c, args[1]);
    return 1;
}
uint32_t FrameworkCreate(Obj *, uint32_t *args)
{
    if (gPlan.createOk || gPlan.createSetsOut) *reinterpret_cast<void **>(uintptr_t(args[2])) = &F(kParser);
    return gPlan.createOk;
}

void SetupFakes()
{
    svcfx::ResetAll();
    gHandler = nullptr;
    gTreeLoader = nullptr;
    gHandlerStack.clear();
    gBuffer = nullptr;
    F(kApp).Set(4, 0, &F(kWants));
    F(kWants).Set(23, 1, 0, &WantsDefinition);
    gDefinition.Reset(0x7d);
    gDefinition.Set(16, 0, gPlan.objectType);
    F(kResources).Set(39, "pp-", 0, &ResourcesFind);
    F(kRecord).Set(0, "dp", 0, &RecordQuery);
    F(kRecord).Set(6, 0);
    F(kStream).Set(52, 0, gPlan.streamSize);
    F(kStream).Set(27, "pd", 0, &StreamRead);
    F(kFramework).Set(3, "ddp", 0, &FrameworkCreate);
    F(kParser).Set(8, "p", 0, &ParserSetHandler);
    F(kParser).Set(2, 0, 0, &ParserRelease);
    F(kParser).refs = 1;
    F(kParser).Set(3, "pdd", 0, &ParserParse);
    F(kParser).Set(14, "d", 0, &ParserPush); // the name: ParserPush logs its hash in place of the word
    F(kParser).Set(15, 0, 0, &ParserPop);
}

// ---- digests ----
// Every node the trees reach, every field (the padding bytes +0x2f and +0x5f left out: nothing writes them).
void DigestNodes(Blob &d)
{
    if (!gManager) return;
    d.push_back(0x730);
    d.push_back(uint32_t(gManager->Trees().size()));
    for (auto &[id, node] : gManager->Trees()) d.push_back(id), d.push_back(Idx(node));
    const std::vector<cTSWantNode *> nodes = Reachable();
    d.push_back(0x731);
    d.push_back(uint32_t(nodes.size()));
    for (cTSWantNode *n : nodes) {
        d.push_back(uint32_t(n->mRefCount));
        for (uint32_t w : n->mUnused08) d.push_back(w);
        d.push_back(n->mNodeId), d.push_back(n->mWantId), d.push_back(n->mSubjectType);
        d.push_back(n->mObjectField), d.push_back(n->mIntegerField);
        d.push_back(n->mVisible), d.push_back(n->mInheritSubject), d.push_back(n->mRegeneration);
        d.push_back(uint32_t(n->mLifetime)), d.push_back(uint32_t(n->mAdFrequency)), d.push_back(uint32_t(n->mScoreOverride));
        for (int32_t w : n->mWeights) d.push_back(uint32_t(w));
        for (uint8_t z : n->mSuperZero) d.push_back(z);
        d.push_back(uint32_t(n->mMinScore)), d.push_back(uint32_t(n->mMaxScore));
        for (const std::map<int32_t, bool> *flags : {&n->mAgeFlags, &n->mAspirationFlags, &n->mPersonalityFlags}) {
            d.push_back(uint32_t(flags->size()));
            for (auto &[k, v] : *flags) d.push_back(uint32_t(k)), d.push_back(v);
        }
        for (const int32_t *range : {n->mMoodRange, n->mRelationshipRange, n->mAspirationScoreRange, n->mPersonDataRange})
            d.push_back(uint32_t(range[0])), d.push_back(uint32_t(range[1]));
        d.push_back(uint32_t(n->mPersonDataIndex));
        d.push_back(n->mId);
        d.push_back(Idx(n->mParent));
        d.push_back(uint32_t(n->mChildren.size()));
        d.push_back(uint32_t(n->mDepth));
    }
}

// The image's std::map<cRZString, uint32_t> at `rva` (head, size), in key order.
void CollectImageMap(const uint8_t *node, const uint8_t *head, std::vector<std::pair<std::string, uint32_t>> &out)
{
    if (node == head || node[0xd]) return;
    CollectImageMap(*reinterpret_cast<const uint8_t *const *>(node), head, out);
    out.push_back({reinterpret_cast<const cRZString *>(node + 0x10)->mString, *reinterpret_cast<const uint32_t *>(node + 0x30)});
    CollectImageMap(*reinterpret_cast<const uint8_t *const *>(node + 8), head, out);
}
void DigestStatics(Blob &d, bool original)
{
    for (int which = 0; which < 2; ++which) {
        std::vector<std::pair<std::string, uint32_t>> tags;
        if (original) {
            const uint8_t *head = *GameAddress<const uint8_t *>(which ? 0x10db04c : 0x10db040);
            if (head) CollectImageMap(*reinterpret_cast<const uint8_t *const *>(head + 4), head, tags);
        } else {
            for (auto &[k, v] : which ? Loader::UnknownEndTags() : Loader::UnknownStartTags()) tags.push_back({k, v});
        }
        std::sort(tags.begin(), tags.end());
        d.push_back(0x740 + which);
        d.push_back(uint32_t(tags.size()));
        for (auto &[k, v] : tags) d.push_back(Hash(k)), d.push_back(v);
    }
    d.push_back(original ? *GameAddress<uint8_t>(0x10db03c) : Loader::sNewVersionSeen);
    d.push_back(original ? *GameAddress<uint8_t>(0x10db03d) : Loader::sOldVersionSeen);
}

// ---- random scripts ----
constexpr uint32_t kTree = 0x1015de4, kWantNode = 0x1015de8, kPropertySet = 0x1015dec, kEditorValues = 0x1015df0,
                   kAds = 0x1015df4, kBool = 0x1015df8, kRange = 0x1015dfc, kChildren = 0x1015e00, kComment = 0x1015e04;
constexpr uint32_t kVersion = 0x1015e08, kKey = 0x1015e0c, kWeight = 0x1015e10, kSuperZero = 0x1015e14,
                   kValue = 0x1015e18, kMin = 0x1015e1c, kMax = 0x1015e20, kIndex = 0x1015e24, kTrue = 0x1015e28,
                   kFalse = 0x1015e2c;
const char *const kNumbers[] = {"0", "1", "-3", "8", "9", "2", "0x10", "077", "abc", "2147483647", "-2147483648", "  5x"};

std::string Pick(std::mt19937 &rng, const char *const *list, size_t n) { return list[rng() % n]; }
std::string Number(std::mt19937 &rng) { return Pick(rng, kNumbers, std::size(kNumbers)); }
std::string TrueFalse(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return Text(kTrue);
    case 1: return Text(kFalse);
    case 2: return "true";
    default: return Text(rng() % 2 ? kTrue : kFalse);
    }
}
std::string FactorName(std::mt19937 &rng)
{
    if (rng() % 8 == 0) return "Age";
    return Text(0x1015e30 + 4 * (rng() % 7));
}
std::string BoolKeyName(std::mt19937 &rng)
{
    if (rng() % 10 == 0) return "Teenager";
    return Text(0x1015e4c + 4 * (rng() % 27));
}
std::string Junk(std::mt19937 &rng)
{
    static const char *const junk[] = {"mystery", "Tree", "wantnode", "", "zebra"};
    return junk[rng() % 5];
}

void AddPropertySet(std::mt19937 &rng, std::vector<Event> &s)
{
    s.push_back({0, Text(kPropertySet), {}});
    for (uint32_t i = 0, n = rng() % 4; i < n; ++i) {
        static const char *const keys[] = {"id", "id", "ID", "other", "id "};
        static const char *const types[] = {"0xeb61e4f7", "0xeb61e4f7", "0x0b8bea18", "0xcba908e1", "zz"};
        static const char *const elements[] = {"AnyUint32", "AnyString", "AnyBoolean"};
        const std::string element = elements[rng() % 3];
        s.push_back({0, element, {{"key", keys[rng() % 5]}, {"type", types[rng() % 5]}}});
        s.push_back({2, rng() % 3 ? Number(rng) : std::string("7"), {}});
        s.push_back({1, element, {}});
    }
    if (rng() % 8) s.push_back({1, Text(kPropertySet), {}});
}

Event RandomStart(std::mt19937 &rng, uint32_t cell)
{
    Event e{0, Text(cell), {}};
    const uint32_t n = rng() % 4;
    for (uint32_t i = 0; i < n; ++i) {
        if (rng() % 10 == 0) {
            e.attrs.push_back({"junk", "1"});
            continue;
        }
        switch (cell) {
        case kTree: e.attrs.push_back({Text(kVersion), Number(rng)}); break;
        case kAds:
            switch (rng() % 3) {
            case 0: e.attrs.push_back({Text(kKey), FactorName(rng)}); break;
            case 1: e.attrs.push_back({Text(kWeight), Number(rng)}); break;
            default: e.attrs.push_back({Text(kSuperZero), TrueFalse(rng)}); break;
            }
            break;
        case kBool:
            if (rng() % 2) e.attrs.push_back({Text(kKey), BoolKeyName(rng)});
            else e.attrs.push_back({Text(kValue), TrueFalse(rng)});
            break;
        case kRange: {
            static const uint32_t names[] = {kMin, kMax, kIndex};
            e.attrs.push_back({Text(names[rng() % 3]), Number(rng)});
            break;
        }
        default: {
            static const uint32_t names[] = {kVersion, kKey, kWeight, kValue, kMin};
            e.attrs.push_back({Text(names[rng() % 5]), Number(rng)});
            break;
        }
        }
    }
    return e;
}

void RandomPlan(std::mt19937 &rng)
{
    Plan &p = gPlan;
    p.found = rng() % 6 != 0;
    p.recordSet = p.found || rng() % 2;
    p.qiOk = rng() % 8 != 0;
    p.qiSetsOut = rng() % 2;
    p.createOk = rng() % 8 != 0;
    p.createSetsOut = rng() % 2;
    p.streamSize = rng() % 4 ? rng() % 200 : 0;
    p.keepMask = rng() % 4 ? rng() | rng() : rng();
    p.objectType = rng() % 3 ? rng() % 8 : rng();
    p.preTrees.clear();
    for (uint32_t i = 0, n = rng() % 3; i < n; ++i) p.preTrees.push_back({rng() % 3 ? rng() % 3 : 7u, rng() % 4 != 0});
    p.script.clear();
    p.script.push_back(RandomStart(rng, kAds));
    static const uint32_t busy[] = {kTree, kWantNode, kWantNode, kWantNode, kAds, kAds, kBool, kBool, kBool, kRange, kRange,
                                    kChildren, kPropertySet, kPropertySet, kPropertySet, kComment, kEditorValues};
    for (uint32_t i = 0, n = rng() % 70; i < n; ++i) {
        const uint32_t r = rng() % 20;
        if (r == 0) {
            p.script.push_back({int(rng() % 3), Junk(rng), {}});
            continue;
        }
        if (r == 1) {
            p.script.push_back({2, "text", {}});
            continue;
        }
        const uint32_t cell = busy[rng() % std::size(busy)];
        if (cell == kEditorValues && rng() % 3) continue;
        if (cell == kPropertySet && rng() % 4) {
            AddPropertySet(rng, p.script);
            continue;
        }
        if (rng() % 5 < 2) p.script.push_back({1, Text(cell), {}});
        else p.script.push_back(RandomStart(rng, cell));
    }
    if (rng() % 2) p.script.push_back({1, Text(kTree), {}});
}

long __cdecl LibStrtol(const char *s, char **end, int base) { return std::strtol(s, end, base); }
unsigned long __cdecl LibStrtoul(const char *s, char **end, int base) { return std::strtoul(s, end, base); }
int __cdecl LibAtoi(const char *s) { return std::atoi(s); }
int __cdecl NoAtexit(void (*)()) { return 0; }

// how: 0 the original, 1 ours, 2 the original LoadTree with our adapters patched in (the caller holds the patches).
Blob RunLoad(difftest::Context &ctx, int how)
{
    SetupFakes();
    *GameAddress<uint8_t>(0x10db03c) = 0;
    *GameAddress<uint8_t>(0x10db03d) = 0;
    Loader::sNewVersionSeen = false;
    Loader::sOldVersionSeen = false;
    Manager m{};
    ::new (&m.Trees()) std::map<uint32_t, void *>();
    for (auto &[id, hasNode] : gPlan.preTrees) {
        cTSWantNode *node = hasNode ? new cTSWantNode : nullptr;
        if (node) node->AddRef();
        m.Trees()[id] = static_cast<cITSWantNode *>(node);
    }
    gManager = &m;
    const uint32_t key[4] = {0x0c, 0x6e5e5ef3, 0x8ec4cb5a, 0x1234};
    if (how == 1) Loader::LoadTree(key, &m);
    else ctx.Original<void(__cdecl *)(const void *, void *)>(0x5550c0)(key, &m);
    Blob d = svcfx::gEv;
    DigestNodes(d);
    svcfx::CaptureFakes(d, kValueKinds);
    DigestStatics(d, how == 0); // the adapters keep their unknown tags and flags in ours
    gManager = nullptr;
    // The manager's references go (each tree takes its subtree with it).
    for (auto &[id, node] : m.Trees())
        if (node) AsNode(node)->Release();
    std::destroy_at(&m.Trees());
    return d;
}

bool Compare(difftest::Context &ctx, uint32_t seed, const Blob &a, const Blob &b, const char *what)
{
    if (svcfx::Same(ctx, a, b, what, seed)) return true;
    if (std::getenv("TREE_DUMP")) {
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
        for (size_t j = i > 40 ? i - 40 : 0; j < i + 20; ++j)
            std::fprintf(stderr, "%4u %08x %08x\n", unsigned(j), j < a.size() ? a[j] : 0, j < b.size() ? b[j] : 0);
    }
    return false;
}

struct Patches {
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch node{0x5b2750, reinterpret_cast<const void *>(&cTSWantNodeAdapters::Construct)};
    shape::CodePatch strtolPatch{0x836d08, reinterpret_cast<const void *>(&LibStrtol)};
    shape::CodePatch strtoulPatch{0x836d4e, reinterpret_cast<const void *>(&LibStrtoul)};
    shape::CodePatch atoiPatch{0x830972, reinterpret_cast<const void *>(&LibAtoi)};
    shape::CodePatch hexPatch{0x1b0e2c, reinterpret_cast<const void *>(&hexparse::ParseHex32)};
    shape::CodePatch atexitPatch{0x824cbe, reinterpret_cast<const void *>(&NoAtexit)};
    Patches()
    {
        globals.Set(kResourceCell, &F(kResources));
        globals.Set(kAppCell, &F(kApp));
        svcfx::gFrameworkIndex = kFramework;
    }
};

} // namespace wanttree

using namespace wanttree;

DIFF_TEST(WantTreeLoader_Load, 0x5550c0)
{
    Patches patches;
    const uint32_t seeds = std::getenv("TREE_SEEDS") ? uint32_t(std::atoi(std::getenv("TREE_SEEDS"))) : 1500;
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        std::mt19937 rng(seed);
        RandomPlan(rng);
        const Blob a = RunLoad(ctx, 0);
        const Blob b = RunLoad(ctx, 1);
        if (!Compare(ctx, seed, a, b, "LoadTree")) break;
    }
}

DIFF_TEST(WantTreeLoader_Adapters, 0x553210)
{
    Patches patches;
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        std::mt19937 rng(seed + 50000);
        RandomPlan(rng);
        const Blob a = RunLoad(ctx, 0);
        Blob b;
        {
            shape::CodePatch construct(0x551bd0, reinterpret_cast<const void *>(&cTSWantTreeLoaderAdapters::Construct));
            shape::CodePatch reset(0x556240, reinterpret_cast<const void *>(&cTSWantTreeLoaderAdapters::Reset));
            shape::CodePatch release(0x5560b0, reinterpret_cast<const void *>(&cTSWantTreeLoaderAdapters::Release));
            shape::CodePatch start(0x553210, reinterpret_cast<const void *>(&cTSWantTreeLoaderAdapters::StartElement));
            shape::CodePatch end(0x552960, reinterpret_cast<const void *>(&cTSWantTreeLoaderAdapters::EndElement));
            b = RunLoad(ctx, 2);
        }
        if (!Compare(ctx, seed, a, b, "Adapters")) break;
    }
}

namespace wanttree {

template <int Slot, class... A> uint32_t Slot32(void *object, A... args)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, A...), Slot>(object, args...);
}

// The slots on one loader: QueryInterface, AddRef, slots 5-11, optionally Reset, then Release down to the last reference.
Blob RunSlots(difftest::Context &ctx, void *object, bool original, std::mt19937 &rng)
{
    Blob d;
    auto *bytes = static_cast<uint8_t *>(object);
    // The constructor's fields (0x551bd0 writes +0x04, +0x08 and +0x2c..+0x44 only; +0x0c..+0x28 are left unset, and at +0x38
    // it stores a word (0x551c15), the padding +0x3a/+0x3b left).
    d.push_back(*reinterpret_cast<uint32_t *>(bytes + 4));
    d.push_back(*reinterpret_cast<uint32_t *>(bytes + 8));
    for (uint32_t off = 0x2c; off < 0x48; off += 4)
        d.push_back(*reinterpret_cast<uint32_t *>(bytes + off) & (off == 0x38 ? 0xffffu : 0xffffffffu));
    *reinterpret_cast<void **>(bytes + 4) = &F(kOther0 + 4); // the manager: not counted
    for (uint32_t off : {8u, 0x3cu, 0x40u, 0x44u})
        if (rng() % 3) *reinterpret_cast<void **>(bytes + off) = &F(kOther0 + off % 7);
    for (uint32_t iid : {1u, 0x4b6b9c9bu, 2u, 0u, 0x4b6b9c9cu, uint32_t(rng())}) {
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(object, iid, &out);
        d.push_back(ok);
        d.push_back(out == object ? 1 : svcfx::P(out));
        d.push_back(*reinterpret_cast<uint32_t *>(bytes + 0x2c));
    }
    d.push_back(Slot32<1>(object));
    d.push_back(Slot32<5>(object, uint32_t(rng()), uint32_t(rng())) & 0xff);
    d.push_back(Slot32<6>(object) & 0xff);
    d.push_back(Slot32<7>(object) & 0xff);
    d.push_back(Slot32<8>(object, uint32_t(rng()), uint32_t(rng())) & 0xff);
    d.push_back(Slot32<9>(object, uint32_t(rng())) & 0xff);
    d.push_back(Slot32<10>(object, uint32_t(rng()), uint32_t(rng())) & 0xff);
    d.push_back(Slot32<11>(object, uint32_t(rng()), uint32_t(rng()), uint32_t(rng()), uint32_t(rng())) & 0xff);
    if (rng() % 2) {
        if (original) ctx.Original<void(__thiscall *)(void *)>(0x556240)(object);
        else static_cast<Loader *>(object)->Reset();
        for (uint32_t off : {4u, 8u, 0x3cu, 0x40u, 0x44u}) d.push_back(svcfx::P(*reinterpret_cast<void **>(bytes + off)));
    }
    for (int32_t refs = *reinterpret_cast<int32_t *>(bytes + 0x2c); refs >= 1; --refs) d.push_back(Slot32<2>(object));
    d.insert(d.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::CaptureFakes(d);
    return d;
}

} // namespace wanttree

DIFF_TEST(WantTreeLoader_Slots, 0x5560b0)
{
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        Blob r[2];
        for (int side = 0; side < 2; ++side) {
            svcfx::ResetAll();
            std::mt19937 rng(seed);
            void *object;
            if (side == 0) {
                void *memory = ctx.Original<void *(__cdecl *)(size_t)>(0x824632)(0x48);
                std::memset(memory, 0xcd, 0x48);
                object = ctx.Original<void *(__thiscall *)(void *)>(0x551bd0)(memory);
            } else {
                object = new Loader;
            }
            r[side] = RunSlots(ctx, object, side == 0, rng);
        }
        if (!svcfx::Same(ctx, r[0], r[1], "Slots", seed)) break;
    }
}

