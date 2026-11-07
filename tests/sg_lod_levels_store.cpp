// Differential tests for nGZSceneGraph::cLODLevelsStore (lane QS; src/sg/cLODLevels.{h,cpp}). The store is standalone
// shaped: the original's side is a store the image's factory (0xab6f40) made, run by the image's bodies at their RVAs;
// ours is our class, run through our virtuals. Both layouts are the same from +8 on, so the stores' words compare.
//
// Everything the store talks to is a logging fake: the framework (the service cell 0x10c9378, slot 5), the settings
// service (slots 4 / 5), the COM director (0x10c9390, slot 12) and its GetClassObject (slot 3), the file parser, the
// messages (cIGZMessage2Standard), the scene graph's node walk (iterators, nodes, extensions and the items told the
// settings changed) and the extension walk 0xaea060 (stood in for on both sides: the image's and ours). Pointers are logged by name: the store and
// its faces, the table (+offset) and the fakes.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "game/game.h"
#include "sg/cLODLevels.h"
#include "sg/cSceneGraphSystem.h"
#include "sg/cSkinExtension.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace nGZSceneGraph;

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

// The allocator service (cache 0x10c95d4, guard 0x10c95d8 as after its first use) the settings keys' strings use.
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n) { return std::malloc(n ? n : 1); }
void __fastcall ServiceFree(void *, void *, void *p) { std::free(p); }
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceAlloc), reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

// Sets game cells for the scope, restoring them after.
struct Cells {
    std::vector<std::pair<uint32_t *, uint32_t>> saved;
    void Set(uint32_t rva, uint32_t value)
    {
        uint32_t *cell = GameAddress<uint32_t>(rva);
        saved.push_back({cell, *cell});
        *cell = value;
    }
    ~Cells()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->first = it->second;
    }
};

std::string gLog;
void Log(const char *format, ...)
{
    char buffer[400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    gLog += buffer;
    gLog += '\n';
}

std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}

// ---- fakes ----

constexpr int kSlots = 100;
struct Fake {
    void **mVTable;
    const char *mName;
    int mRefs;
    uint32_t mWord[4]; // per kind
};

uint8_t *gStore = nullptr;           // this side's store
const uint8_t *gFakeTable = nullptr; // this side's fake table (named even once the store has let go of it)

std::deque<Fake> gIterators; // this side's iterators, by creation order
struct Named {
    const void *p;
    std::string name;
};
std::vector<Named> gNamed; // fakes and blocks by name

std::string Name(const void *p)
{
    if (!p) return "null";
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (gStore && b >= gStore && b < gStore + 0x1c) return "store+" + Hex(static_cast<uint32_t>(b - gStore));
    const uint8_t *const tables[2] = {gStore ? *reinterpret_cast<uint8_t *const *>(gStore + 0x18) : nullptr, gFakeTable};
    for (const uint8_t *t : tables)
        if (t && b >= t && b < t + cLODLevelsTable::kSize) return "table+" + Hex(static_cast<uint32_t>(b - t));
    for (size_t i = 0; i < gIterators.size(); ++i)
        if (p == &gIterators[i]) return "it" + std::to_string(i);
    for (const Named &n : gNamed)
        if (n.p == p) return n.name;
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (a >= kGameBase && a < kGameBase + 0x2000000) return "rva " + Hex(static_cast<uint32_t>(a - kGameBase));
    return "?";
}

uint32_t __fastcall FakeAddRef(Fake *f, void *)
{
    Log("%s.AddRef", Name(f).c_str());
    return static_cast<uint32_t>(++f->mRefs);
}
uint32_t __fastcall FakeRelease(Fake *f, void *)
{
    Log("%s.Release", Name(f).c_str());
    return static_cast<uint32_t>(--f->mRefs);
}

struct VTable {
    void *slots[kSlots] = {};
    VTable()
    {
        slots[1] = reinterpret_cast<void *>(&FakeAddRef);
        slots[2] = reinterpret_cast<void *>(&FakeRelease);
    }
    template <class F> void Set(int slot, F f) { slots[slot] = reinterpret_cast<void *>(f); }
};

// The framework: slot 5 GetSystemService(class, iid, out).
Fake gSettings{}, gSceneGraph{}, gFramework{}, gCom{}, gFileParser{}, gPath{}, gMessage{}, gOtherMessage{};
bool gHaveSettings = true, gHaveSceneGraph = false;
bool __fastcall FrameworkGetService(Fake *, void *, uint32_t cls, uint32_t iid, void **out)
{
    Log("framework.GetSystemService %x %x", cls, iid);
    if (cls == 0x0ac1887e && gHaveSettings) {
        *out = &gSettings;
        ++gSettings.mRefs;
        return true;
    }
    if (cls == 0xe5074b6b && gHaveSceneGraph) {
        *out = &gSceneGraph;
        ++gSceneGraph.mRefs;
        return true;
    }
    return false;
}


// A settings key: a string of the game's std::string layout (16 inline bytes or a pointer, the size at +0x10, the
// capacity at +0x14).
std::string KeyText(const void *key)
{
    if (!key) return "(null)";
    const uint8_t *k = static_cast<const uint8_t *>(key);
    const uint32_t size = *reinterpret_cast<const uint32_t *>(k + 0x10), cap = *reinterpret_cast<const uint32_t *>(k + 0x14);
    if (size > cap || size > 0x1000) return "(bad string)";
    const char *text = cap > 15 ? *reinterpret_cast<const char *const *>(k) : reinterpret_cast<const char *>(k);
    return std::string(text, size) + " cap " + std::to_string(cap);
}

// The settings service: slot 4 (target, id, key, default value), slot 5 (target, id, key).
void __fastcall SettingsWatch(Fake *, void *, void *target, uint32_t id, void *key, const uint32_t *value)
{
    const uint32_t vtable = static_cast<uint32_t>(value[0] - kGameBase);
    Log("settings.Watch %s %x '%s' value vtable %x word %x", Name(target).c_str(), id, KeyText(key).c_str(), vtable,
        vtable == 0xd9c670 ? value[1] & 0xff : value[1]);
}
void __fastcall SettingsUnwatch(Fake *, void *, void *target, uint32_t id, void *key)
{
    Log("settings.Unwatch %s %x '%s'", Name(target).c_str(), id, KeyText(key).c_str());
}

// The COM director's storage (its first word a vtable: slot 12 the COM object) and the COM's slot 3.
void *__fastcall DirectorCom(void *, void *) { return &gCom; }
void *gDirectorVTable[16] = {};
bool gHaveFileParser = true;
uint32_t __fastcall ComGetClassObject(Fake *, void *, uint32_t cls, uint32_t iid, void **out)
{
    Log("com.GetClassObject %x %x out was %s", cls, iid, Name(*out).c_str());
    if (!gHaveFileParser) return 0;
    *out = &gFileParser;
    ++gFileParser.mRefs;
    return 1;
}

// The file parser: 3 Init, 4 Shutdown, 5 SetParser, 6 AddFile, 7 RunFiles (appends gRows to the table), 10 IsDirty.
std::vector<LODLevel> gRows;
bool gDirty = false;
bool __fastcall ParserInit(Fake *, void *)
{
    Log("fileParser.Init");
    return true;
}
bool __fastcall ParserShutdown(Fake *, void *)
{
    Log("fileParser.Shutdown");
    return true;
}
void __fastcall ParserSetParser(Fake *, void *, void *parser, uint32_t flag)
{
    Log("fileParser.SetParser %s %x", Name(parser).c_str(), flag);
}
void __fastcall ParserAddFile(Fake *, void *, const char *path, uint32_t flags)
{
    Log("fileParser.AddFile '%s' %x", path ? path : "(null)", flags);
}
bool __fastcall ParserRunFiles(Fake *, void *)
{
    Log("fileParser.RunFiles");
    auto *table = *reinterpret_cast<cLODLevelsTable **>(gStore + 0x18);
    for (const LODLevel &row : gRows) table->mLevels.push_back(row);
    return true;
}
bool __fastcall ParserIsDirty(Fake *, void *)
{
    Log("fileParser.IsDirty");
    return gDirty;
}

// A path string: slot 5 its text.
const char *__fastcall PathText(Fake *f, void *) { return reinterpret_cast<const char *>(f->mWord[0]); }

// A message: slot 0 QueryInterface (answers by mWord[0]: 0 no, 1 yes with itself, 2 no but writes itself),
// 4 GetType (mWord[1]), 13 HasData2 (mWord[2]), 15 GetVoid2 (mWord[3]).
bool __fastcall MessageQuery(Fake *f, void *, uint32_t iid, void **out)
{
    Log("%s.QueryInterface %x", Name(f).c_str(), iid);
    if (f->mWord[0] == 0) return false;
    *out = f;
    ++f->mRefs;
    return f->mWord[0] == 1;
}
uint32_t __fastcall MessageType(Fake *f, void *)
{
    Log("%s.GetType", Name(f).c_str());
    return f->mWord[1];
}
bool __fastcall MessageHasData2(Fake *f, void *)
{
    Log("%s.HasData2", Name(f).c_str());
    return f->mWord[2] != 0;
}
void *__fastcall MessageVoid2(Fake *f, void *)
{
    Log("%s.GetVoid2", Name(f).c_str());
    return reinterpret_cast<void *>(f->mWord[3]);
}

// ---- the scene graph walk ----

// A node list: what an iterator walks (mWord[0] the list, mWord[1] the position).
struct NodeList {
    std::vector<void *> nodes;
};
VTable gIteratorVT;
Fake *NewIterator(NodeList *list, uint32_t at)
{
    gIterators.push_back(Fake{gIteratorVT.slots, "it", 1, {reinterpret_cast<uint32_t>(list), at, 0, 0}});
    return &gIterators.back();
}
bool __fastcall IteratorIsEqual(Fake *f, void *, const Fake *other)
{
    Log("%s.IsEqual %s", Name(f).c_str(), Name(other).c_str());
    return f->mWord[0] == other->mWord[0] && f->mWord[1] == other->mWord[1];
}
void *__fastcall IteratorGet(Fake *f, void *)
{
    Log("%s.Get", Name(f).c_str());
    return reinterpret_cast<NodeList *>(f->mWord[0])->nodes.at(f->mWord[1]);
}
void __fastcall IteratorNext(Fake *f, void *)
{
    Log("%s.Next", Name(f).c_str());
    ++f->mWord[1];
}
void FillRange(void **range, NodeList *list)
{
    range[0] = NewIterator(list, 0);
    range[1] = NewIterator(list, 0);
    range[2] = NewIterator(list, static_cast<uint32_t>(list->nodes.size()));
}

NodeList gTopNodes;
void *__fastcall SceneGraphNodes(Fake *, void *, void **out)
{
    Log("sceneGraph.GetSceneNodes");
    FillRange(out, &gTopNodes);
    return out;
}

// A top node: slot 44 (range out, 0, filter) gives its children of the filter's interface (mWord[0] the shape children,
// mWord[1] the extension children).
void __fastcall NodeChildren(Fake *f, void *, void **out, uint32_t zero, const uint32_t *filter)
{
    Log("%s.Children %x filter vtable %x iid %x", Name(f).c_str(), zero, static_cast<uint32_t>(filter[0] - kGameBase), filter[1]);
    FillRange(out, reinterpret_cast<NodeList *>(filter[1] == 0xa516cd10 ? f->mWord[0] : filter[1] == 0x7d14eb66 ? f->mWord[1] : 0));
}
// A shape child: slot 93 (out: cursor, middle, end) over its items (mWord[0] a NodeList).
void __fastcall ShapeItems(Fake *f, void *, void **out)
{
    Log("%s.Items", Name(f).c_str());
    auto *list = reinterpret_cast<NodeList *>(f->mWord[0]);
    out[0] = list->nodes.data();
    out[1] = reinterpret_cast<void *>(0x5eed);
    out[2] = list->nodes.data() + list->nodes.size();
}
// An item: slot 44 told.
void __fastcall ItemTold(Fake *f, void *) { Log("%s.LODChanged", Name(f).c_str()); }
// An extension child: the walk 0xaea060 gives its extensions (mWord[0] a NodeList) after `previous`.
void *__cdecl NextExtension(Fake *node, void *previous)
{
    Log("NextExtension %s after %s", Name(node).c_str(), Name(previous).c_str());
    auto *list = reinterpret_cast<NodeList *>(node->mWord[0]);
    size_t i = 0;
    if (previous) {
        while (i < list->nodes.size() && list->nodes[i] != previous) ++i;
        ++i;
    }
    return i < list->nodes.size() ? list->nodes[i] : nullptr;
}
// An extension: 15 its count (mWord[0] a NodeList), 5 the item at an index (null entries stay null).
uint32_t __fastcall ExtensionCount(Fake *f, void *)
{
    Log("%s.Count", Name(f).c_str());
    return static_cast<uint32_t>(reinterpret_cast<NodeList *>(f->mWord[0])->nodes.size());
}
void *__fastcall ExtensionItem(Fake *f, void *, uint32_t i)
{
    Log("%s.Item %u", Name(f).c_str(), i);
    return reinterpret_cast<NodeList *>(f->mWord[0])->nodes.at(i);
}

VTable gFrameworkVT, gSettingsVT, gComVT, gFileParserVT, gPathVT, gMessageVT, gSceneGraphVT, gNodeVT, gShapeVT, gItemVT,
    gExtensionChildVT, gExtensionVT, gTableUnknownVT, gTableParserVT;

// A fake table (for everything but Init): the vbtable at +8, the vector, the cIGZUnknown face (+0x1bc, Release logged)
// and the cIBaseParser face (+0x1c4, slot 1 Shutdown logged).
const int32_t gFakeVbTable[3] = {-8, 0x1b4, 0x1bc};
bool __fastcall TableShutdown(void *face, void *)
{
    Log("%s.Shutdown", Name(face).c_str());
    return true;
}
uint32_t __fastcall TableRelease(void *face, void *)
{
    Log("%s.Release", Name(face).c_str());
    return 1;
}
cLODLevelsTable *NewFakeTable(const std::vector<LODLevel> &rows)
{
    auto *t = static_cast<cLODLevelsTable *>(std::calloc(1, cLODLevelsTable::kSize));
    *reinterpret_cast<const int32_t **>(t->mBase + 8) = gFakeVbTable;
    new (&t->mLevels) std::vector<LODLevel>(rows);
    t->mUnknownFaceVTable = gTableUnknownVT.slots;
    t->mParserFaceVTable = gTableParserVT.slots;
    gFakeTable = reinterpret_cast<const uint8_t *>(t);
    return t;
}
void FreeFakeTable(cLODLevelsTable *t)
{
    gFakeTable = nullptr;
    if (!t) return;
    t->mLevels.~vector();
    std::free(t);
}

void InitFakes()
{
    static bool done = false;
    if (done) return;
    done = true;
    gFrameworkVT.Set(5, &FrameworkGetService);
    gSettingsVT.Set(4, &SettingsWatch);
    gSettingsVT.Set(5, &SettingsUnwatch);
    gComVT.Set(3, &ComGetClassObject);
    gFileParserVT.Set(3, &ParserInit);
    gFileParserVT.Set(4, &ParserShutdown);
    gFileParserVT.Set(5, &ParserSetParser);
    gFileParserVT.Set(6, &ParserAddFile);
    gFileParserVT.Set(7, &ParserRunFiles);
    gFileParserVT.Set(10, &ParserIsDirty);
    gPathVT.Set(5, &PathText);
    gMessageVT.Set(0, &MessageQuery);
    gMessageVT.Set(4, &MessageType);
    gMessageVT.Set(13, &MessageHasData2);
    gMessageVT.Set(15, &MessageVoid2);
    gSceneGraphVT.Set(3, &SceneGraphNodes);
    gIteratorVT.Set(4, &IteratorIsEqual);
    gIteratorVT.Set(5, &IteratorGet);
    gIteratorVT.Set(6, &IteratorNext);
    gNodeVT.Set(44, &NodeChildren);
    gShapeVT.Set(93, &ShapeItems);
    gItemVT.Set(44, &ItemTold);
    gExtensionVT.Set(15, &ExtensionCount);
    gExtensionVT.Set(5, &ExtensionItem);
    gTableUnknownVT.Set(2, &TableRelease);
    gTableParserVT.Set(1, &TableShutdown);
    gDirectorVTable[12] = reinterpret_cast<void *>(&DirectorCom);
}

void ResetFakes()
{
    auto reset = [](Fake &f, VTable &vt, const char *name) { f = Fake{vt.slots, name, 1, {}}; };
    reset(gSettings, gSettingsVT, "settings");
    reset(gSceneGraph, gSceneGraphVT, "sceneGraph");
    reset(gFramework, gFrameworkVT, "framework");
    reset(gCom, gComVT, "com");
    reset(gFileParser, gFileParserVT, "fileParser");
    reset(gPath, gPathVT, "path");
    reset(gMessage, gMessageVT, "message");
    reset(gOtherMessage, gMessageVT, "message2");
    gNamed.clear();
    for (Fake *f : {&gSettings, &gSceneGraph, &gFramework, &gCom, &gFileParser, &gPath, &gMessage, &gOtherMessage})
        gNamed.push_back({f, f->mName});
    gIterators.clear();
    gLog.clear();
}

// The game globals the fakes are plugged into: the framework cell, the COM director's storage and its guard, the
// allocator service.
struct Globals {
    Cells cells;
    void *director = gDirectorVTable;
    explicit Globals(bool framework = true)
    {
        InitFakes();
        cells.Set(0x10c9378, framework ? reinterpret_cast<uint32_t>(&gFramework) : 0);
        cells.Set(0x10c9390, reinterpret_cast<uint32_t>(gDirectorVTable));
        cells.Set(0x10c93f4, 0x80000000u);
        cells.Set(0x10c95d4, reinterpret_cast<uint32_t>(&gServiceObject));
        cells.Set(0x10c95d8, 0x80000000u);
    }
};

// ---- the two sides ----

struct Store {
    uint8_t *p;
    bool original;
};

// The factory (0xab6f40) runs the constructor (0xab6aa0); every test dumps a fresh store from it.
Store NewStore(difftest::Context &ctx, bool original)
{
    uint8_t *p = original ? static_cast<uint8_t *>(ctx.Original<void *(__cdecl *)()>(0xab6f40)())
                          : reinterpret_cast<uint8_t *>(static_cast<cLODLevelsStore *>(cLODLevelsStore::Create()));
    gStore = p;
    return {p, original};
}
cLODLevelsStore *Ours(const Store &s) { return reinterpret_cast<cLODLevelsStore *>(s.p); }

template <class T> T &Word(const Store &s, uint32_t off) { return *reinterpret_cast<T *>(s.p + off); }

// The store's words from +8 (pointers by name) and the table's rows.
std::string Dump(const Store &s)
{
    // (+0xd..+0xf: padding neither side writes)
    std::string out = "refs " + std::to_string(Word<int32_t>(s, 8)) + " use " + Hex(Word<uint8_t>(s, 0xc)) +
                      " override " + std::to_string(Word<int32_t>(s, 0x10)) +
                      " fileParser " + Name(Word<void *>(s, 0x14)) + " table " + (Word<void *>(s, 0x18) ? "set" : "null");
    if (auto *t = Word<cLODLevelsTable *>(s, 0x18)) {
        out += " rows";
        for (const LODLevel &r : t->mLevels) out += " " + std::to_string(r.size) + ":" + std::to_string(r.level);
    }
    return out;
}

// Frees a store whatever its side (through its own deleting destructor; its table and file parser cleared first).
void FreeStore(difftest::Context &ctx, Store &s)
{
    Word<void *>(s, 0x14) = nullptr;
    Word<void *>(s, 0x18) = nullptr;
    if (s.original) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xab6c30)(s.p, 1);
    else delete Ours(s);
    gStore = nullptr;
}

void Compare(difftest::Context &ctx, const std::string &what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a == b) return;
    // From the line before the first difference, a few lines of each.
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    size_t start = a.rfind('\n', at == 0 ? 0 : at - 1);
    start = start == std::string::npos || start == 0 ? 0 : a.rfind('\n', start - 1);
    start = start == std::string::npos ? 0 : start + 1;
    ctx.Fail("%s:\n--- original\n%.600s\n--- ours\n%.600s", what.c_str(), a.c_str() + start, b.c_str() + (std::min)(start, b.size()));
}

std::vector<LODLevel> RandomRows(std::mt19937 &g, size_t n, int sizes)
{
    std::vector<LODLevel> rows(n);
    for (LODLevel &r : rows) r = {static_cast<int32_t>(g() % sizes) - (g() % 9 == 0 ? 50 : 0), static_cast<int32_t>(g() % 31)};
    return rows;
}

} // namespace

// QueryInterface (0xab7460, and through the message target face, 0xab7489), AddRef (folded 0x145260) and Release (folded
// 0x313300): ids 1, the interface, the class id and others, a null out; Release to zero destroys the store (0xab6c30 / ours; the destructor 0xab6bc0), which releases a file parser and a
// table it holds (each present or not).
DIFF_TEST(LODLevelsStore_QueryInterface, 0xab7460)
{
    Allocators allocators;
    Globals globals;
    std::mt19937 g(0xab7460);
    static const uint32_t kIds[] = {1, 0x2c674b25, 0x2c674b2c, 0, 0x65297976, 0x2c674b26};
    for (int round = 0; round < 200; ++round) {
        const uint32_t iid = kIds[g() % std::size(kIds)];
        const bool nullOut = g() % 6 == 0;
        const int refs = static_cast<int>(g() % 3);
        const bool parser = g() % 2, table = g() % 2;
        const int releases = static_cast<int>(g() % 4);
        std::string got[2];
        const uint32_t faceSeed = g();
        std::mt19937 g2[2] = {std::mt19937(faceSeed), std::mt19937(faceSeed)};
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            Store s = NewStore(ctx, side == 0);
            Word<int32_t>(s, 8) = refs;
            cLODLevelsTable *t = table ? NewFakeTable({}) : nullptr;
            Word<void *>(s, 0x14) = parser ? &gFileParser : nullptr;
            Word<void *>(s, 0x18) = t;
            void *out = reinterpret_cast<void *>(0x5e5e5e5e);
            const bool ok = s.original ? ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0xab7460)(s.p, iid, nullOut ? nullptr : &out)
                                       : Ours(s)->QueryInterface(iid, nullOut ? nullptr : &out);
            Log("QueryInterface %x -> %d out %s", iid, ok, out == reinterpret_cast<void *>(0x5e5e5e5e) ? "untouched" : Name(out).c_str());
            // The cIGZMessageTarget2 face's slot 0 (the image's adjustor thunk 0xab7489; ours our compiler's, through our
            // vtable).
            const uint32_t faceIid = kIds[g2[side]() % std::size(kIds)];
            out = reinterpret_cast<void *>(0x5e5e5e5e);
            const bool faceOk = s.original ? ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0xab7489)(s.p + 4, faceIid, &out)
                                           : static_cast<cIGZMessageTarget2 *>(Ours(s))->QueryInterface(faceIid, &out);
            Log("face QueryInterface %x -> %d out %s", faceIid, faceOk, out == reinterpret_cast<void *>(0x5e5e5e5e) ? "untouched" : Name(out).c_str());
            const uint32_t added = s.original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x145260)(s.p) : Ours(s)->AddRef();
            Log("AddRef -> %u", added);
            bool destroyed = false;
            for (int i = 0; i < releases && !destroyed; ++i) {
                const int before = Word<int32_t>(s, 8);
                const uint32_t left = s.original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x313300)(s.p) : Ours(s)->Release();
                Log("Release -> %u", left);
                destroyed = before == 1;
            }
            if (!destroyed) {
                Log("%s", Dump(s).c_str());
                FreeStore(ctx, s);
            }
            gStore = nullptr;
            FreeFakeTable(t);
            got[side] = gLog;
        }
        Compare(ctx, "round " + std::to_string(round), got[0], got[1]);
    }
}

// Init (0xab7030) and Shutdown (0xab7810): the settings watched (keys and default values) and unwatched, a real table
// made (the image's constructor on the original's side, ours on ours) and handed to the file parser the COM made; Init
// run once or twice (the second replaces the table and the file parser); Shutdown on a store never initialised too.
// The table is released by Shutdown (destroyed by its own side's code).
DIFF_TEST(LODLevelsStore_InitShutdown, 0xab7030)
{
    Allocators allocators;
    Globals globals;
    std::mt19937 g(0xab7030);
    for (int round = 0; round < 40; ++round) {
        const int inits = static_cast<int>(g() % 3);
        const bool shutdown = g() % 4 != 0 || inits == 0;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            Store s = NewStore(ctx, side == 0);
            for (int i = 0; i < inits; ++i) {
                const bool ok = s.original ? ctx.Original<bool(__thiscall *)(void *)>(0xab7030)(s.p) : Ours(s)->Init();
                Log("Init -> %d; %s", ok, Dump(s).c_str());
            }
            if (shutdown) {
                const bool ok = s.original ? ctx.Original<bool(__thiscall *)(void *)>(0xab7810)(s.p) : Ours(s)->Shutdown();
                Log("Shutdown -> %d; %s", ok, Dump(s).c_str());
            } else if (Word<void *>(s, 0x18)) { // the table released by its own side
                auto *t = Word<cLODLevelsTable *>(s, 0x18);
                Word<void *>(s, 0x18) = nullptr;
                if (s.original) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xab6c50)(t, 1);
                else t->DeletingDestructor(1);
            }
            Log("refs settings %d com %d fileParser %d", gSettings.mRefs, gCom.mRefs, gFileParser.mRefs);
            FreeStore(ctx, s);
            got[side] = gLog;
        }
        Compare(ctx, "round " + std::to_string(round) + " inits " + std::to_string(inits), got[0], got[1]);
    }
}

// LoadFile (0xab7780) and Update (0xab7340): the path's text added (flags 5) and the files run (the fake appends rows to
// the table), the rows sorted by size (0..40 rows, few distinct sizes so many equal, some negative); Update only when the
// file parser says it is dirty. Then IsEnabled (0xab6ce0), GetOverride (folded 0x149cf0) and GetLevel (0xab72b0) for
// sizes around every row's (widths and heights either way round, fractions, negatives), never on an empty table.
DIFF_TEST(LODLevelsStore_LoadAndQuery, 0xab7780)
{
    Allocators allocators;
    Globals globals;
    std::mt19937 g(0xab7780);
    static const char *const kPaths[] = {"Config\\LODLevels.txt", "", "a long path with spaces\\and\\more\\LODs.cfg"};
    for (int round = 0; round < 300; ++round) {
        const size_t before = g() % 3 == 0 ? g() % 6 : 0;
        const size_t n = g() % 5 == 0 ? 33 + g() % 8 : g() % 12;
        const int sizes = g() % 2 ? 6 : 2000;
        const std::vector<LODLevel> initial = RandomRows(g, before, sizes);
        const std::vector<LODLevel> rows = RandomRows(g, n, sizes);
        const char *path = kPaths[g() % std::size(kPaths)];
        const int op = static_cast<int>(g() % 3); // 0 LoadFile, 1 Update clean, 2 Update dirty
        const uint8_t use = static_cast<uint8_t>(g() % 3);
        const int32_t override = static_cast<int32_t>(g() % 40) - 1;
        std::vector<std::pair<float, float>> queries;
        for (int i = 0; i < 12; ++i) {
            float a = static_cast<float>(static_cast<int>(g() % 2100) - 50) + static_cast<float>(g() % 4) * 0.25f;
            float b = static_cast<float>(static_cast<int>(g() % 2100) - 50) + static_cast<float>(g() % 4) * 0.25f;
            if (!rows.empty() && g() % 2) a = static_cast<float>(rows[g() % rows.size()].size) + static_cast<float>(g() % 3) - 1.0f;
            queries.push_back({a, b});
        }
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            gNamed.push_back({&gPath, "path"});
            gRows = rows;
            gDirty = op == 2;
            gPath.mWord[0] = reinterpret_cast<uint32_t>(path);
            Store s = NewStore(ctx, side == 0);
            cLODLevelsTable *t = NewFakeTable(initial);
            Word<void *>(s, 0x14) = &gFileParser;
            Word<void *>(s, 0x18) = t;
            Word<uint8_t>(s, 0xc) = use;
            Word<int32_t>(s, 0x10) = override;
            if (op == 0) {
                if (s.original) ctx.Original<void(__thiscall *)(void *, void *)>(0xab7780)(s.p, &gPath);
                else Ours(s)->LoadFile(reinterpret_cast<cIGZString *>(&gPath));
            } else {
                if (s.original) ctx.Original<void(__thiscall *)(void *)>(0xab7340)(s.p);
                else Ours(s)->Update();
            }
            Log("%s", Dump(s).c_str());
            const bool enabled = s.original ? ctx.Original<bool(__thiscall *)(void *)>(0xab6ce0)(s.p) : Ours(s)->IsEnabled();
            const int32_t over = s.original ? ctx.Original<int32_t(__thiscall *)(void *)>(0x149cf0)(s.p) : Ours(s)->GetOverride();
            Log("enabled %d override %d", enabled, over);
            if (!t->mLevels.empty())
                for (const auto &q : queries) {
                    const int32_t level = s.original ? ctx.Original<int32_t(__thiscall *)(void *, float, float)>(0xab72b0)(s.p, q.first, q.second)
                                                     : Ours(s)->GetLevel(q.first, q.second);
                    Log("level %g %g -> %d", q.first, q.second, level);
                }
            FreeStore(ctx, s);
            FreeFakeTable(t);
            got[side] = gLog;
        }
        Compare(ctx, "round " + std::to_string(round) + " op " + std::to_string(op), got[0], got[1]);
    }
}

// DoMessage (0xab6d60, the cIGZMessageTarget2 face at +4): a null message; a message that is not a
// cIGZMessage2Standard (answering no, or no while writing itself, which is released all the same); the two settings'
// types and others; HasData2 no; GetVoid2 null; the override's value 0, small and large; the switch's byte 0, 1 and 2
// (stored as it came), the same as the store's or not. The refresh it calls (primary slot 13) asks the framework for
// the scene graph and finds none.
DIFF_TEST(LODLevelsStore_DoMessage, 0xab6d60)
{
    Allocators allocators;
    Globals globals;
    std::mt19937 g(0xab6d60);
    static const uint32_t kTypes[] = {0x8c7df62a, 0xcc6887ab, 0, 0x8c7df62b, 0x2c674b25};
    for (int round = 0; round < 400; ++round) {
        const bool nullMessage = g() % 12 == 0;
        const uint32_t answer = g() % 6 == 0 ? static_cast<uint32_t>(g() % 2 * 2) : 1;
        const uint32_t type = kTypes[g() % std::size(kTypes)];
        const uint32_t has = g() % 5 != 0;
        const bool nullData = g() % 6 == 0;
        uint32_t data[2] = {0x5e5e5e5e, 0};
        switch (g() % 4) {
        case 0: data[1] = 0; break;
        case 1: data[1] = static_cast<uint32_t>(g() % 40); break;
        case 2: data[1] = g(); break;
        default: data[1] = 0x5e5e5e00u | (g() % 3); break; // the switch's byte with junk above it
        }
        const uint8_t use = static_cast<uint8_t>(g() % 3);
        const int32_t override = static_cast<int32_t>(g() % 40) - 1;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            gHaveSceneGraph = false;
            gMessage.mWord[0] = answer;
            gMessage.mWord[1] = type;
            gMessage.mWord[2] = has;
            gMessage.mWord[3] = nullData ? 0 : reinterpret_cast<uint32_t>(data);
            Store s = NewStore(ctx, side == 0);
            Word<uint8_t>(s, 0xc) = use;
            Word<int32_t>(s, 0x10) = override;
            void *message = nullMessage ? nullptr : &gMessage;
            const bool handled = s.original ? ctx.Original<bool(__thiscall *)(void *, void *)>(0xab6d60)(s.p + 4, message)
                                            : static_cast<cIGZMessageTarget2 *>(Ours(s))->DoMessage(message);
            Log("handled %d; %s; message refs %d", handled, Dump(s).c_str(), gMessage.mRefs);
            FreeStore(ctx, s);
            got[side] = gLog;
        }
        Compare(ctx, "round " + std::to_string(round) + " type " + Hex(type), got[0], got[1]);
    }
}

// Refresh (0xab74a0): no framework, no scene graph, or a scene graph of 0..3 nodes, each with 0..2 shape children of
// 0..3 items (told) and 0..2 extension children with 0..2 extensions of 0..3 items (some null, not told). Every
// iterator's calls and releases are logged.
DIFF_TEST(LODLevelsStore_Refresh, 0xab74a0)
{
    Allocators allocators;
    std::mt19937 g(0xab74a0);
    // Ours calls our walk (lane VS: 0xaea060 is ours, tests/sg_indexed_iterator.cpp), so the stand-in covers both.
    shape::StandIn walk(0xaea060u, &nGZSceneGraph::NextSkinExtension,&NextExtension);
    for (int round = 0; round < 150; ++round) {
        const bool framework = g() % 10 != 0;
        const bool sceneGraph = g() % 8 != 0;
        // The scene: fakes and lists built once per round, shared by both sides.
        std::deque<Fake> fakes;
        std::deque<NodeList> lists;
        auto make = [&](VTable &vt, const char *kind) -> Fake * {
            fakes.push_back(Fake{vt.slots, kind, 1, {}});
            return &fakes.back();
        };
        gTopNodes.nodes.clear();
        for (unsigned n = 0, nodes = g() % 4; n < nodes; ++n) {
            Fake *node = make(gNodeVT, "node");
            lists.emplace_back();
            NodeList &shapes = lists.back();
            lists.emplace_back();
            NodeList &extensionChildren = lists.back();
            node->mWord[0] = reinterpret_cast<uint32_t>(&shapes);
            node->mWord[1] = reinterpret_cast<uint32_t>(&extensionChildren);
            for (unsigned c = 0, k = g() % 3; c < k; ++c) {
                Fake *shape = make(gShapeVT, "shape");
                lists.emplace_back();
                NodeList &items = lists.back();
                shape->mWord[0] = reinterpret_cast<uint32_t>(&items);
                for (unsigned i = 0, m = g() % 4; i < m; ++i) items.nodes.push_back(make(gItemVT, "item"));
                shapes.nodes.push_back(shape);
            }
            for (unsigned c = 0, k = g() % 3; c < k; ++c) {
                Fake *child = make(gExtensionChildVT, "extensionChild");
                lists.emplace_back();
                NodeList &extensions = lists.back();
                child->mWord[0] = reinterpret_cast<uint32_t>(&extensions);
                for (unsigned e = 0, m = g() % 3; e < m; ++e) {
                    Fake *extension = make(gExtensionVT, "extension");
                    lists.emplace_back();
                    NodeList &items = lists.back();
                    extension->mWord[0] = reinterpret_cast<uint32_t>(&items);
                    for (unsigned i = 0, q = g() % 4; i < q; ++i) items.nodes.push_back(g() % 4 == 0 ? nullptr : make(gItemVT, "item"));
                    extensions.nodes.push_back(extension);
                }
                extensionChildren.nodes.push_back(child);
            }
            gTopNodes.nodes.push_back(node);
        }
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            Globals globals(framework);
            ResetFakes();
            for (size_t i = 0; i < fakes.size(); ++i) {
                fakes[i].mRefs = 1;
                gNamed.push_back({&fakes[i], std::string(fakes[i].mName) + std::to_string(i)});
            }
            gHaveSceneGraph = sceneGraph;
            Store s = NewStore(ctx, side == 0);
            if (s.original) ctx.Original<void(__thiscall *)(void *)>(0xab74a0)(s.p);
            else Ours(s)->Refresh();
            std::string refs = "refs sceneGraph " + std::to_string(gSceneGraph.mRefs) + " iterators";
            for (const Fake &it : gIterators) refs += " " + std::to_string(it.mRefs);
            Log("%s", refs.c_str());
            FreeStore(ctx, s);
            got[side] = gLog;
        }
        gHaveSceneGraph = false;
        Compare(ctx, "round " + std::to_string(round), got[0], got[1]);
        if (round == 0 && got[0].find("LODChanged") == std::string::npos && got[0].find("GetSceneNodes") == std::string::npos)
            ctx.Fail("round 0 never reached the scene graph");
    }
}
