// Differential tests for nTSSG::cLotImposterManager (lane 261108j, src/sg/cLotImposterManager.h): the life cycle, the
// COM slots, Init / Shutdown, the message face and the definitions, on the image's vtables (hook shape: the original
// runs on an object image built the same way as ours).
//
// LotImposterManager_Life: a manager built by the original constructor (0x362a10, over 0xc5 bytes) or factory (0x367790)
// against one built by ours, then the same random members of the population a running manager has (message ids, the
// placement service, the counted members and their vectors, the definitions with their string-pair maps, the +0x1bc map,
// the +0x80 string) put in by test code on both sides; then random operations: Init (0x369140), Shutdown (0x36c630),
// DoMessage (0x3668d0 on the +4 face: a null message, message 0xdc or others, the generate flag +0x173 on and off, a
// holder or none), QueryInterface (0x369530: ids 1, 0xbc4a393, 0x452294aa, others; a null out), Release (0x3697e0:
// counts 1, 2, 5), LoadDefinitions (0x369590: 0..3 files, some missing, short and long names; the parser's command
// adding definitions or not), the definitions' Clear (0x3f7a20, through the map erase 0x362470), the initialised and
// generate bytes set; then the destructor (0x362de0, the erase again) or the deleting destructor (0x3630f0, flags 0, 1,
// 3, random) under a delete recorder.
//
// The object's word 0 is a test vtable after construction (slot 1 AddRef, 5 generate, 8 the deleting destructor, each
// logged), as the generation slots are row LT2b2's. Every object the manager reaches is a recording fake
// (tests/ts_services_fixture.h): the framework (the director's slot 12), the placement service (0x20e0b2 patched), the
// parser (a two-word box: its own logging vtable, and a virtual-base table whose entry 1 reaches a fake that counts the
// release), the message and its holder, the counted members. Stood in on both sides: the message-id walks 0x1c0458 /
// 0x1c081e, cRZFile::PathExists 0x1c1242 (a name exists unless it starts with 'x'), cTSAppCommonImpl slot 5 0x1d0c60
// (the seed's file list), cTSSliceImposterCreator's constructor 0xb51100 (fills its 0x88 bytes) and destructor 0xb51300,
// Shutdown's generation helpers 0x369800 / 0x3698f0. Compared after the build and every operation: the result and out
// word, every field (the padding bytes too on the constructor route), the message ids, the definitions (names, values,
// pairs), the +0x1bc map, the event log, every fake's count; at the end the deletes in order (block, size).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "sg/cLotImposterManager.h"
#include "ts/cTSCatalog.h"

#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace lotimpmgr_test {

using namespace svcfx;
using nTSSG::cImposterDefinition;
using nTSSG::cImposterString;
using nTSSG::cImposterStringPair;
using nTSSG::cLotImposterManager;
using Mgr = cLotImposterManager;

enum : unsigned { fFramework = 0, fPlacement, fPlacementOld, fParserBase, fVBase, fMessage, fHolder, fMaterials, fRef0 };
enum : uint32_t {
    evSelf = 0x5100, evWalk5, evWalk6, evPlacement, evSliceCtor, evSliceDtor, evGenA, evGenB, evExists, evList, evParser,
    evCommand, evText,
};

// The definitions map and its pairs with the test's comparators (the same nodes as the class's maps).
struct KeyLess {
    bool operator()(const cImposterString &a, const cImposterString &b) const { return a.s.mString < b.s.mString; }
};
struct PairLess {
    bool operator()(const cImposterStringPair &a, const cImposterStringPair &b) const
    {
        return std::tie(a.first.s.mString, a.second.s.mString) < std::tie(b.first.s.mString, b.second.s.mString);
    }
};
using TestDefs = std::map<cImposterString, cImposterDefinition, KeyLess>;
using TestPairs = std::map<cImposterStringPair, cImposterStringPair, PairLess>;
static_assert(sizeof(TestDefs) == 8 && sizeof(TestPairs) == 8);

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}
uint32_t Hash(const cImposterString &s) { return TextHash(s.s.mString.c_str()); }

// ---- the world for a seed ----
struct World {
    std::vector<std::string> files;
    bool placement, holder, commandAdds;
    uint32_t type;
    uint8_t slicePattern;
};
World gWorld;
void *gDefs; // what the parser's slot 3 was given
std::vector<void *> gCommands;

// ---- the test vtable on the object ----
uint32_t __fastcall SelfAddRef(void *self, void *)
{
    Ev(evSelf), Ev(1), Ev(Norm(P(self)));
    return 0x51;
}
uint32_t __fastcall SelfGenerate(void *self, void *)
{
    Ev(evSelf), Ev(5), Ev(Norm(P(self)));
    return 0x55;
}
void *__fastcall SelfDelete(void *self, void *, uint32_t flags)
{
    Ev(evSelf), Ev(8), Ev(Norm(P(self))), Ev(flags);
    return self;
}
void *gSelfVT[10];

// ---- boxes: an object whose virtual base (vbptr at +4, entry 1) is a fake ----
struct Box {
    void **vt;
    const int32_t *vbtable;
};
Box gParser, gVBase;
int32_t gParserTable[2], gVBaseTable[2];
void PointBox(Box &box, int32_t (&table)[2], unsigned fake)
{
    table[0] = 0;
    table[1] = int32_t(P(&F(fake)) - (P(&box) + 4));
    box.vbtable = table;
}

uint32_t __fastcall ParserSlot0(void *self, void *, uint32_t a)
{
    Ev(evParser), Ev(0), Ev(Norm(P(self))), Ev(a);
    return 0;
}
uint32_t __fastcall ParserSlot1(void *self, void *)
{
    Ev(evParser), Ev(1), Ev(Norm(P(self)));
    return 0;
}
uint32_t __fastcall ParserSlot3(void *self, void *, void *defs)
{
    Ev(evParser), Ev(3), Ev(Norm(P(self))), Ev(Norm(P(defs)));
    gDefs = defs;
    return 1;
}
uint32_t __fastcall ParserSlot4(void *self, void *, void *command)
{
    Ev(evParser), Ev(4), Ev(Norm(P(self))), Ev(Norm(P(command)));
    if (command) {
        const auto *w = static_cast<const uint32_t *>(command);
        Ev(Norm(w[0])), Ev(w[1]), Ev(w[2]); // (+0xc, the state, is the parser's to set: left as the heap had it)
        gCommands.push_back(command);
    }
    return 1;
}
void AddDefinition(void *defs, const char *key, unsigned pairs)
{
    auto &map = *reinterpret_cast<TestDefs *>(static_cast<uint8_t *>(defs) + 4);
    auto [it, added] = map.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple());
    if (!added) return;
    it->second.name.s.mString.assign(pairs % 2 ? "name" : "a longer definition name");
    auto &entries = *reinterpret_cast<TestPairs *>(&it->second.entries);
    static const char *const kText[] = {"lod", "shape", "cres", "x"};
    for (unsigned i = 0; i < pairs; ++i)
        entries.emplace(std::piecewise_construct, std::forward_as_tuple(kText[i % 4], kText[(i + 1) % 4]),
                        std::forward_as_tuple(kText[(i + 2) % 4], key));
}
uint32_t __fastcall ParserSlot12(void *self, void *, const char *text, uint32_t flag)
{
    Ev(evParser), Ev(12), Ev(Norm(P(self))), Ev(TextHash(text)), Ev(flag);
    if (gWorld.commandAdds && gDefs) {
        auto &map = *reinterpret_cast<TestDefs *>(static_cast<uint8_t *>(gDefs) + 4);
        if (map.size() < 3) {
            char key[16];
            std::snprintf(key, sizeof(key), "d%08x", TextHash(text));
            AddDefinition(gDefs, key, TextHash(text) % 2);
        }
    }
    return 1;
}
void *gParserVT[13];

// ---- fake slots ----
uint32_t FrameworkGet(Obj *, uint32_t *args) // slot 3 (class, iid, &out): the parser box, counted on its base
{
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &gParser;
    ++F(fParserBase).refs;
    return 1;
}
uint32_t MessageQuery(Obj *, uint32_t *args) // slot 0 (iid, &holder)
{
    void *holder = gWorld.holder ? &F(fHolder) : nullptr;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = holder;
    if (holder) ++F(fHolder).refs;
    return holder != nullptr;
}

// ---- stand-ins ----
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(evWalk5), Ev(Norm(P(self))), Ev(Norm(arg)), Ev(Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
    return 0;
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(evWalk6), Ev(Norm(P(self))), Ev(Norm(arg)), Ev(Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}
void *__cdecl StandPlacement()
{
    Ev(evPlacement);
    return gWorld.placement ? &F(fPlacement) : nullptr;
}
void *__fastcall StandSliceCtor(void *self, void *)
{
    Ev(evSliceCtor), Ev(Norm(P(self)));
    auto *bytes = static_cast<uint8_t *>(self);
    for (uint32_t i = 0; i < 0x88; ++i) bytes[i] = uint8_t(i * 13 + gWorld.slicePattern);
    return self;
}
void __fastcall StandSliceDtor(void *self, void *) { Ev(evSliceDtor), Ev(Norm(P(self))); }
void __fastcall StandGenA(void *self, void *) { Ev(evGenA), Ev(Norm(P(self))); }
void __fastcall StandGenB(void *self, void *) { Ev(evGenB), Ev(Norm(P(self))); }
bool __cdecl StandPathExists(const cIGZString &path)
{
    const std::string &s = reinterpret_cast<const cRZString &>(path).mString;
    Ev(evExists), Ev(TextHash(s.c_str()));
    return !s.empty() && s[0] != 'x';
}
uint32_t __cdecl StandCommonImpl5(uint32_t option, uint32_t list, uint32_t c, uint32_t d, uint32_t e)
{
    Ev(evList), Ev(option), Ev(c), Ev(d), Ev(e);
    auto *files = reinterpret_cast<GZStringList *>(uintptr_t(list));
    files->clear();
    for (const auto &name : gWorld.files) files->push_back(GZString(name.c_str()));
    return 1;
}

// ---- the object's dump ----
uint32_t W(const uint8_t *o, uint32_t off)
{
    uint32_t w;
    std::memcpy(&w, o + off, 4);
    return w;
}
// masked: the constructor's padding bytes are the heap's (the factory route); destroyed: after the destructor.
void DumpObject(Blob &b, const uint8_t *o, bool masked, bool destroyed)
{
    auto d = [&](uint32_t off) { b.push_back(W(o, off)); };
    auto byte = [&](uint32_t off) { b.push_back(masked ? W(o, off) & 0xff : W(o, off)); };
    auto ptr = [&](uint32_t off) { b.push_back(Norm(W(o, off))); };
    auto head = [&](uint32_t off) { b.push_back(destroyed ? W(o, off) != 0 : Norm(W(o, off))); };
    auto vec = [&](uint32_t off) {
        const uint32_t first = W(o, off), last = W(o, off + 4), end = W(o, off + 8);
        b.push_back(Norm(first)), b.push_back(last - first), b.push_back(end - first);
    };
    auto str = [&](uint32_t off) { // a std::string: size, capacity, then the buffer or the block
        const uint32_t cap = W(o, off + 0x14);
        d(off + 0x10), d(off + 0x14);
        if (destroyed) b.push_back(W(o, off) & 0xff);
        else if (cap > 15) ptr(off);
        else for (uint32_t k = 0; k < 16; k += 4) d(off + k);
    };
    ptr(0), ptr(4), byte(8), d(0xc), head(0x10), d(0x14), ptr(0x18), byte(0x1c);
    for (uint32_t off = 0x20; off < 0x38; off += 4) d(off);
    ptr(0x38);
    for (uint32_t off = 0x3c; off < 0x5c; off += 4) ptr(off);
    vec(0x5c), vec(0x68), vec(0x74);
    ptr(0x80), str(0x84), d(0x9c);
    for (uint32_t off = 0xa0; off < 0x140; off += 4) d(off); // +0xa0..+0xac, the slice creator, +0x138, +0x13c
    vec(0x140), d(0x14c), d(0x150), vec(0x154);
    for (uint32_t off = 0x160; off < 0x174; off += 4) d(off);
    byte(0x174);
    for (uint32_t off = 0x178; off < 0x190; off += 4) d(off);
    head(0x190), d(0x194), d(0x198), d(0x19c), d(0x1a0), str(0x1a4), head(0x1bc), d(0x1c0);
}
void DumpContents(Blob &b, uint8_t *o)
{
    b.push_back(0xc0c0);
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(o + 0x10)) b.push_back(id);
    const auto &defs = *reinterpret_cast<TestDefs *>(o + 0x190);
    b.push_back(uint32_t(defs.size()));
    for (const auto &[key, def] : defs) {
        b.push_back(Hash(key)), b.push_back(Hash(def.name));
        const auto &pairs = *reinterpret_cast<const TestPairs *>(&def.entries);
        b.push_back(uint32_t(pairs.size()));
        for (const auto &[k, v] : pairs)
            b.push_back(Hash(k.first)), b.push_back(Hash(k.second)), b.push_back(Hash(v.first)), b.push_back(Hash(v.second));
    }
    for (int32_t index : *reinterpret_cast<std::vector<int32_t> *>(o + 0x74)) b.push_back(uint32_t(index));
    for (const auto &e : *reinterpret_cast<std::vector<nGZGraphic4::cTextureAtlas> *>(o + 0x154))
        b.push_back(e.mRegions), b.push_back(uint32_t(e.mFreeArea)), b.push_back(uint32_t(e.mNodes.size())),
            b.push_back(uint32_t(e.mFreeNodes.size()));
    const auto &extra = *reinterpret_cast<std::map<int32_t, std::array<uint8_t, 8>> *>(o + 0x1bc);
    b.push_back(uint32_t(extra.size()));
    for (const auto &[k, v] : extra) {
        b.push_back(uint32_t(k));
        uint32_t w[2];
        std::memcpy(w, v.data(), 8);
        b.push_back(w[0]), b.push_back(w[1]);
    }
}

// ---- the population a running manager has, put in by test code on both sides ----
struct Population {
    uint32_t ids[3];
    unsigned idCount;
    bool placementOld, vbase;
    uint8_t counted;          // bit i: +0x3c + 4i holds fake fRef0 + i
    unsigned vecCounts[5];    // +0x5c, +0x68, +0x74, +0x140, +0x154
    unsigned text;            // +0x80: 0 kept, 1 short, 2 long
    unsigned defs, defPairs;  // the definitions
    unsigned extra;           // +0x1bc entries
};
void Populate(uint8_t *o, const Population &p)
{
    auto &ids = *reinterpret_cast<std::set<uint32_t> *>(o + 0x10);
    for (unsigned i = 0; i < p.idCount; ++i) ids.insert(p.ids[i]);
    if (p.placementOld) {
        void *old = &F(fPlacementOld);
        std::memcpy(o + 0x18, &old, 4);
    }
    if (p.vbase) {
        void *box = &gVBase;
        std::memcpy(o + 0x38, &box, 4);
    }
    for (unsigned i = 0; i < 8; ++i)
        if (p.counted >> i & 1) {
            void *ref = &F(fRef0 + i);
            std::memcpy(o + 0x3c + 4 * i, &ref, 4);
        }
    for (unsigned v = 0; v < 2; ++v) {
        auto &refs = *reinterpret_cast<std::vector<uint32_t> *>(o + 0x5c + 0xc * v);
        for (unsigned i = 0; i < p.vecCounts[v]; ++i) refs.push_back(P(&F(fRef0 + (i * 3 + v) % 8)));
    }
    reinterpret_cast<std::vector<std::array<uint8_t, 0x50>> *>(o + 0x140)->resize(p.vecCounts[3]);
    auto &handles = *reinterpret_cast<std::vector<int32_t> *>(o + 0x74); // the handles go in raw (counted by the fake only when dropped)
    for (unsigned i = 0; i < p.vecCounts[2]; ++i) handles.push_back(i % 3 == 1 ? -1 : int32_t(0x40 + i));
    auto &slices = *reinterpret_cast<std::vector<nGZGraphic4::cTextureAtlas> *>(o + 0x154);
    for (unsigned i = 0; i < p.vecCounts[4] % 3; ++i) {
        nGZGraphic4::cTextureAtlas &e = slices.emplace_back();
        e.mRegions = 0x100 + i, e.mFreeArea = int32_t(i * 5);
        e.mNodes.resize(i);
        e.mFreeNodes.assign((i + 1) % 2, 0x77u);
    }
    auto &text = reinterpret_cast<cRZString *>(o + 0x80)->mString;
    if (p.text == 1) text.assign("lots");
    if (p.text == 2) text.assign("a lot imposter path longer than sixteen");
    static const char *const kKeys[] = {"bed", "a chair key longer than sixteen", "tree"};
    for (unsigned i = 0; i < p.defs; ++i) AddDefinition(o + 0x18c, kKeys[i], (p.defPairs + i) % 2);
    auto &extra = *reinterpret_cast<std::map<int32_t, std::array<uint8_t, 8>> *>(o + 0x1bc);
    for (unsigned i = 0; i < p.extra; ++i) extra[int32_t(i * 7) - 3] = {uint8_t(i), 2, 3, 4, 5, 6, 7, uint8_t(0x80 + i)};
}

enum Op : unsigned { opInit, opShutdown, opMessage, opQuery, opRelease, opLoad, opClear, opSetInit, opSetGenerate, kOps };
const char *const kOpNames[] = {"init", "shutdown", "message", "query", "release", "load", "clear", "set_init", "set_generate"};
const char *const kMarks[2][kOps + 1] = {
    {"original init", "original shutdown", "original message", "original query", "original release", "original load",
     "original clear", "original set_init", "original set_generate", "original end"},
    {"ours init", "ours shutdown", "ours message", "ours query", "ours release", "ours load", "ours clear", "ours set_init",
     "ours set_generate", "ours end"},
};
struct Step {
    Op op;
    uint32_t a, b;
};

uint32_t gOut[2];

void BeginSide()
{
    ResetAll();
    gEv.reserve(1 << 16);
    gDefs = nullptr;
    gCommands.clear();
    F(fFramework).Set(3, "ddp", 1u, &FrameworkGet);
    F(fMessage).Set(0, "dp", 0u, &MessageQuery);
    F(fMessage).Set(4, 0u, gWorld.type);
    F(fMaterials).Set(21, "d", 0u);
    gOut[0] = gOut[1] = 0x5e5e5e5e;
}

Blob Take(uint32_t result)
{
    Blob o = gEv;
    gEv.clear();
    o.push_back(0xeeee);
    o.push_back(result);
    return o;
}

} // namespace lotimpmgr_test

using namespace lotimpmgr_test;

DIFF_TEST(LotImposterManager_Life, 0x362a10)
{
    AllocatorScope allocator;
    Globals globals;
    globals.Set(0x10f8fcc, &F(fMaterials)); // GetCachedQueriedObject: the material manager (the handles' slot 21)
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch placement(0x20e0b2, shape::CodeOf(&StandPlacement));
    shape::CodePatch sliceCtor(0xb51100, shape::CodeOf(&StandSliceCtor));
    shape::CodePatch sliceDtor(0xb51300, shape::CodeOf(&StandSliceDtor));
    shape::StandIn genA(0x369800, &cLotImposterManager::ReleaseGenerationState, &StandGenA);
    shape::StandIn genB(0x3698f0, &cLotImposterManager::ReleaseGenerationServices, &StandGenB);
    shape::StandIn walk5(0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5);
    shape::StandIn walk6(0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6);
    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    shape::StandIn list(0x1d0c60, &appinit::CommonImplSlot5, &StandCommonImpl5);
    shape::DeleteRecorder deletes; // after the delete patch: they unwind in reverse

    gSelfVT[1] = reinterpret_cast<void *>(&SelfAddRef);
    gSelfVT[5] = reinterpret_cast<void *>(&SelfGenerate);
    gSelfVT[8] = reinterpret_cast<void *>(&SelfDelete);
    gParserVT[0] = reinterpret_cast<void *>(&ParserSlot0);
    gParserVT[1] = reinterpret_cast<void *>(&ParserSlot1);
    gParserVT[3] = reinterpret_cast<void *>(&ParserSlot3);
    gParserVT[4] = reinterpret_cast<void *>(&ParserSlot4);
    gParserVT[12] = reinterpret_cast<void *>(&ParserSlot12);
    gParser.vt = gParserVT;
    gVBase.vt = nullptr;
    PointBox(gParser, gParserTable, fParserBase);
    PointBox(gVBase, gVBaseTable, fVBase);

    auto ctorO = ctx.Original<void *(__thiscall *)(void *)>(0x362a10);
    auto createO = ctx.Original<void *(__cdecl *)()>(0x367790);
    auto dtorO = ctx.Original<void(__thiscall *)(void *)>(0x362de0);
    auto deletingO = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x3630f0);
    auto queryO = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x369530);
    auto releaseO = ctx.Original<uint32_t(__thiscall *)(void *)>(0x3697e0);
    auto initO = ctx.Original<bool(__thiscall *)(void *)>(0x369140);
    auto shutdownO = ctx.Original<bool(__thiscall *)(void *)>(0x36c630);
    auto messageO = ctx.Original<bool(__thiscall *)(void *, void *)>(0x3668d0);
    auto loadO = ctx.Original<void(__thiscall *)(void *)>(0x369590);
    auto clearO = ctx.Original<void(__thiscall *)(void *)>(0x3f7a20);

    static const char *const kFiles[] = {"objectimposters.txt", "xmissing.txt", "imposter definitions for every lot.txt",
                                         "a", "xa much longer missing file name"};
    static const uint32_t kIids[] = {1, Mgr::kIid, Mgr::kIidMessageTarget2, 0x452294ab, 0, 0xbc4a392};
    static const uint32_t kTypes[] = {0xdc, 0xdc, 0xdd, 0x1dc, 0};
    static const uint32_t kIds[] = {0x10, 0xdc, 0x99, 0x7fffffff, 0};
    static const uint32_t kCounts[] = {1, 2, 5};
    static const uint32_t kFlags[] = {0, 1, 3, 0x80000002};

    for (uint32_t seed = 0; seed < 500; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x362a10u);
        auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
        gWorld = World{};
        for (unsigned i = draw(4); i--;) gWorld.files.push_back(kFiles[draw(5)]);
        gWorld.placement = draw(4) != 0;
        gWorld.holder = draw(2) != 0;
        gWorld.commandAdds = draw(2) != 0;
        gWorld.type = kTypes[draw(5)];
        gWorld.slicePattern = uint8_t(gen());
        const bool create = draw(3) == 0;
        Population pop{};
        if (draw(4)) {
            pop.idCount = draw(3);
            for (uint32_t &id : pop.ids) id = kIds[draw(5)];
            pop.placementOld = draw(2) != 0;
            pop.vbase = draw(2) != 0;
            pop.counted = uint8_t(gen());
            for (unsigned &n : pop.vecCounts) n = draw(4);
            pop.text = draw(3);
            pop.defs = draw(3);
            pop.defPairs = draw(3);
            pop.extra = draw(3);
        }
        std::vector<Step> steps(2 + draw(9));
        for (Step &s : steps) s = {Op(draw(kOps)), uint32_t(gen()), uint32_t(gen())};
        const unsigned ending = draw(5); // 0 the destructor, else the deleting destructor with kFlags[ending - 1]

        std::vector<Blob> outcome[2];
        for (int side = 0; side < 2; ++side) {
            BeginSide();
            ctx.Mark(side ? "ours" : "original");
            uint8_t *o;
            if (create) {
                o = static_cast<uint8_t *>(side == 0 ? createO() : static_cast<void *>(Mgr::Create()));
            } else {
                o = static_cast<uint8_t *>(::operator new(Mgr::kSize));
                std::memset(o, 0xc5, Mgr::kSize);
                if (side == 0) ctorO(o);
                else new (o) Mgr();
            }
            Blob built = Take(Norm(P(o)));
            DumpObject(built, o, create, false);
            DumpContents(built, o);
            CaptureFakes(built);
            outcome[side].push_back(built);

            void *vt = gSelfVT;
            std::memcpy(o, &vt, 4);
            Populate(o, pop);
            for (const Step &s : steps) {
                uint32_t result = 0;
                ctx.Mark(kMarks[side][s.op]);
                switch (s.op) {
                case opInit: result = (side == 0 ? initO(o) : reinterpret_cast<Mgr *>(o)->Init()) & 0xff; break;
                case opShutdown: result = (side == 0 ? shutdownO(o) : reinterpret_cast<Mgr *>(o)->Shutdown()) & 0xff; break;
                case opMessage: {
                    void *message = s.a % 6 ? &F(fMessage) : nullptr;
                    auto *face = reinterpret_cast<Mgr::MessageFace *>(o + 4);
                    result = (side == 0 ? messageO(face, message) : face->DoMessage(message)) & 0xff;
                    break;
                }
                case opQuery: {
                    const uint32_t iid = kIids[s.a % 6];
                    void **out = s.b % 5 ? reinterpret_cast<void **>(gOut) : nullptr;
                    gOut[0] = 0x5e5e5e5e;
                    result = (side == 0 ? queryO(o, iid, out) : reinterpret_cast<Mgr *>(o)->QueryInterface(iid, out)) & 0xff;
                    Ev(NormOut(gOut[0]));
                    break;
                }
                case opRelease: {
                    const uint32_t count = kCounts[s.a % 3];
                    std::memcpy(o + 0xc, &count, 4);
                    result = side == 0 ? releaseO(o) : reinterpret_cast<Mgr *>(o)->Release();
                    break;
                }
                case opLoad:
                    if (side == 0) loadO(o);
                    else reinterpret_cast<Mgr *>(o)->LoadDefinitions();
                    break;
                case opClear:
                    if (side == 0) clearO(o + 0x18c);
                    else reinterpret_cast<Mgr *>(o)->mDefinitions.Clear();
                    break;
                case opSetInit: o[8] = uint8_t(s.a & 1); break;
                case opSetGenerate: o[0x173] = uint8_t(s.a & 1); break;
                default: break;
                }
                Blob b = Take(result);
                DumpObject(b, o, create, false);
                DumpContents(b, o);
                CaptureFakes(b);
                outcome[side].push_back(b);
            }

            ctx.Mark(kMarks[side][kOps]);
            deletes.Begin();
            if (ending == 0) {
                if (side == 0) dtorO(o);
                else reinterpret_cast<Mgr *>(o)->~Mgr();
            } else {
                const uint32_t flags = kFlags[ending - 1];
                void *r = side == 0 ? deletingO(o, flags) : static_cast<void *>(reinterpret_cast<Mgr *>(o)->DeletingDestructor(flags));
                Ev(Norm(P(r)));
            }
            deletes.End();
            if (deletes.Overflowed()) { // the later blocks were freed (the object among them): the population is too big
                ctx.Fail("seed %u (%s): more than %d deletes", seed, side ? "ours" : "original", shape::DeleteRecorder::kMax);
                return;
            }
            Blob end = Take(0);
            end.push_back(deletes.Overflowed());
            bool objectFreed = false;
            for (int i = 0; i < deletes.Count(); ++i) {
                end.push_back(Norm(P(deletes.At(i).block)));
                end.push_back(deletes.At(i).size);
                objectFreed |= deletes.At(i).block == o;
            }
            DumpObject(end, o, create, true);
            CaptureFakes(end);
            outcome[side].push_back(end);
            deletes.Release();
            if (!objectFreed) ::operator delete(o);
            for (void *command : gCommands) ::operator delete(command);
            gCommands.clear();
        }
        const size_t n = outcome[0].size() < outcome[1].size() ? outcome[0].size() : outcome[1].size();
        bool same = outcome[0].size() == outcome[1].size();
        for (size_t i = 0; i < n && same; ++i) {
            const char *what = i == 0 ? "build" : i == n - 1 ? "end" : kOpNames[steps[i - 1].op];
            same = Same(ctx, outcome[0][i], outcome[1][i], what, seed);
        }
        if (outcome[0].size() != outcome[1].size()) ctx.Fail("seed %u: step counts differ", seed);
    }
}

// ---- LotImposterManager_Leaves (lane 261111q): the generation path's leaves on a manager both sides lay out alike ----
// ReleaseGenerationState 0x369800, ReleaseGenerationServices 0x3698f0, SliceCount 0x368f50, SliceCountF 0x368f70,
// HasSliceBorder 0x366fc0, SnapshotName 0x368fa0. The manager is built by our constructor on both sides (the leaves
// are what is compared), populated as in the life test, then random steps. Each release of a counted member (the eight
// slots, the vectors' entries, the virtual-base box's base) logs, as it happens, the eight slots, +0x38 and the three
// vectors' bounds and entries, so the order of the emptying shows (a slot nulled before or after its release, a vector's
// end moved before or after its entries go). cTSSliceImposterCreator's release 0xb518a0 is stood in (logged with the same
// dump); the material handles' destructor (0xae8870, ours) reaches the material manager fake's slot 21.
namespace lotimpmgr_test {

uint8_t *gLeafObject; // the manager whose state each release logs

void LeafState()
{
    const uint8_t *o = gLeafObject;
    if (!o) return;
    Ev(0x1eaf);
    Ev(Norm(W(o, 0x38)));
    for (uint32_t off = 0x3c; off < 0x5c; off += 4) Ev(Norm(W(o, off)));
    for (uint32_t off = 0x5c; off < 0x80; off += 0xc) {
        const uint32_t first = W(o, off), last = W(o, off + 4), end = W(o, off + 8);
        Ev(Norm(first)), Ev(last - first), Ev(end - first);
        if (off != 0x74)
            for (uint32_t at = first; at < last; at += 4) Ev(Norm(*reinterpret_cast<const uint32_t *>(uintptr_t(at))));
        else
            for (uint32_t at = first; at < last; at += 4) Ev(*reinterpret_cast<const uint32_t *>(uintptr_t(at)));
    }
    for (unsigned i = 0; i < kFakes; ++i) Ev(uint32_t(F(i).refs));
}
uint32_t LeafRelease(Obj *self, uint32_t *)
{
    const uint32_t left = uint32_t(--self->refs);
    LeafState();
    return left;
}
void __fastcall StandSliceRelease(void *self, void *)
{
    Ev(0x51ce), Ev(Norm(P(self)));
    LeafState();
}

} // namespace lotimpmgr_test

DIFF_TEST(LotImposterManager_Leaves, 0x369800)
{
    AllocatorScope allocator;
    Globals globals;
    globals.Set(0x10f8fcc, &F(fMaterials));
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch sliceCtor(0xb51100, shape::CodeOf(&StandSliceCtor));
    shape::CodePatch sliceDtor(0xb51300, shape::CodeOf(&StandSliceDtor));
    shape::CodePatch sliceRelease(0xb518a0, shape::CodeOf(&StandSliceRelease));
    gVBase.vt = nullptr;
    PointBox(gVBase, gVBaseTable, fVBase);

    auto stateO = ctx.Original<void(__thiscall *)(void *)>(0x369800);
    auto servicesO = ctx.Original<void(__thiscall *)(void *)>(0x3698f0);
    auto countO = ctx.Original<int32_t(__thiscall *)(void *)>(0x368f50);
    auto countFO = ctx.Original<float(__thiscall *)(void *)>(0x368f70);
    auto borderO = ctx.Original<uint32_t(__thiscall *)(void *)>(0x366fc0);
    auto nameO = ctx.Original<void *(__cdecl *)(void *, int32_t, int32_t)>(0x368fa0);

    enum : unsigned { lsState, lsServices, lsCount, lsCountF, lsBorder, lsName, lsRefill, kLeafOps };
    static const char *const kLeafNames[] = {"state", "services", "count", "count_f", "border", "name", "refill"};
    static const int32_t kNumbers[] = {0, 1, -1, 7, 255, 0x7fffffff, int32_t(0x80000000), 12345, -98765};
    static const int32_t kWidths[] = {0x80, 0x40, 0, -0x80, 0x7fffffff, int32_t(0x80000000), 3};
    static const int32_t kDivisors[] = {8, 1, -1, 3, 0x40, -7, 0x7fffffff};
    int reached[kLeafOps] = {};

    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 2246822519u + 0x369800u);
        auto draw = [&](uint32_t n) { return uint32_t(gen() % n); };
        gWorld = World{};
        gWorld.slicePattern = uint8_t(gen());
        Population pop{};
        pop.vbase = draw(3) != 0;
        pop.counted = uint8_t(gen());
        for (unsigned &n : pop.vecCounts) n = draw(4);
        std::vector<std::array<uint32_t, 4>> steps(1 + draw(6));
        for (auto &s : steps) s = {draw(kLeafOps), uint32_t(gen()), uint32_t(gen()), uint32_t(gen())};

        std::vector<Blob> outcome[2];
        for (int side = 0; side < 2; ++side) {
            BeginSide();
            for (unsigned i = 0; i < 8; ++i) F(fRef0 + i).Set(2, 0, 0, &LeafRelease);
            F(fVBase).Set(2, 0, 0, &LeafRelease);
            ctx.Mark(side ? "ours" : "original");
            auto *o = static_cast<uint8_t *>(::operator new(Mgr::kSize));
            std::memset(o, 0xc5, Mgr::kSize);
            auto *m = new (o) Mgr();
            void *vt = gSelfVT;
            std::memcpy(o, &vt, 4);
            Populate(o, pop);
            gEv.clear();
            gLeafObject = o;
            for (const auto &s : steps) {
                uint32_t result = 0;
                Blob extra;
                switch (s[0]) {
                case lsState:
                    if (side == 0) stateO(o);
                    else m->ReleaseGenerationState();
                    break;
                case lsServices:
                    if (side == 0) servicesO(o);
                    else m->ReleaseGenerationServices();
                    break;
                case lsCount:
                case lsCountF: {
                    const int32_t width = kWidths[s[1] % 7], border = kNumbers[s[2] % 9] % 0x40,
                                  divisor = kDivisors[s[3] % 7];
                    std::memcpy(o + 0xa4, &width, 4), std::memcpy(o + 0x150, &border, 4), std::memcpy(o + 0x14c, &divisor, 4);
                    const int64_t top = int64_t(int32_t(uint32_t(width) - 2u * uint32_t(border)));
                    if (top == INT32_MIN && divisor == -1) break; // the division faults alike: not a case
                    if (s[0] == lsCount) {
                        result = uint32_t(side == 0 ? countO(o) : m->SliceCount());
                    } else {
                        const float f = side == 0 ? countFO(o) : m->SliceCountF();
                        result = std::bit_cast<uint32_t>(f);
                    }
                    break;
                }
                case lsBorder: {
                    o[0x198 + s[1] % 4] = uint8_t(s[2] % 3 == 0 ? 0 : s[2] >> 8);
                    const int32_t border = kNumbers[s[3] % 9];
                    std::memcpy(o + 0x150, &border, 4);
                    result = (side == 0 ? borderO(o) : uint32_t(m->HasSliceBorder())) & 0xff;
                    break;
                }
                case lsName: {
                    alignas(4) uint8_t out[sizeof(cRZString)];
                    std::memset(out, 0xd7, sizeof(out));
                    const int32_t n = kNumbers[s[1] % 9], direction = int32_t(s[2] % 7) - 1;
                    void *r = side == 0 ? nameO(out, n, direction)
                                        : static_cast<void *>(Mgr::SnapshotName(reinterpret_cast<cRZString *>(out), n, direction));
                    auto *text = reinterpret_cast<cRZString *>(out);
                    extra.push_back(r == out), extra.push_back(W(out, 0));
                    extra.push_back(TextHash(text->mString.c_str())), extra.push_back(uint32_t(text->mString.size()));
                    extra.push_back(uint32_t(text->mString.capacity()));
                    std::destroy_at(&text->mString);
                    break;
                }
                case lsRefill: Populate(o, pop); break;
                default: break;
                }
                ++reached[s[0]];
                Blob b = Take(result);
                b.insert(b.end(), extra.begin(), extra.end());
                LeafState();
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                DumpObject(b, o, false, false);
                DumpContents(b, o);
                CaptureFakes(b);
                outcome[side].push_back(b);
            }
            gLeafObject = nullptr;
            m->~Mgr();
            ::operator delete(o);
            gEv.clear();
        }
        for (size_t i = 0; i < steps.size(); ++i)
            if (!Same(ctx, outcome[0][i], outcome[1][i], kLeafNames[steps[i][0]], seed)) break;
        if (ctx.failures.size() >= 6) return;
    }
    for (unsigned k = 0; k < kLeafOps; ++k)
        if (reached[k] < 100) ctx.Fail("coverage: %s ran %d times", kLeafNames[k], reached[k]);
}
