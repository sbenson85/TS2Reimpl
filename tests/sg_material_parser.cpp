// Differential tests for nGZSceneGraph::cMaterialParser (lane RH, scenegraph M1b; src/sg/cMaterialParser.h,
// src/hook/tables/cMaterialParser.cpp).
//
// The constructor (0xacec90) runs cBaseParser's constructor for real on both sides (the image's on the original's, ours on
// ours: verified equal by tests/gz_base_parser.cpp) and is compared as the parser Form of the whole 0x304 bytes (the two
// maps' head nodes compared by their words). The destructors, the resets, Initialise, the cell erase and QueryInterface
// run on objects built of the same junk and the same random members on both sides (the containers' layouts are the
// game's; the counted pointers are fakes that log their releases, the service cells close through a fake service object
// that logs). cBaseParser's own bodies they call are stood in for (the image's on the original's side, our member on ours,
// each logging the cBaseParser `this` as an offset); 0xae87d0 (the cell's open) runs for real on both. The faces'
// thunks are checked like tests/sg_lod_levels_table.cpp checks cLODLevelsTable's. Last, a whole parser built by the
// image and one by our code are initialised through the real cBaseParser and compared by their registered commands.
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gz_base_parser_form.h"
#include "shape.h"
#include "ts_services_fixture.h" // svcfx::AllocatorScope

#include "gz/CommandParser.h"
#include "gz/GraphicServices.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZPropertySet.h"
#include "rz/StaticGetters.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "hook/hooks.h"
#include "sg/cLODLevels.h"
#include "sg/cMaterialParser.h"

#include <malloc.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace cp = nGZCommandParser;
using cp::cBaseParser;
using cp::cBaseParserAdapters;
using nGZSceneGraph::Blob12;
using nGZSceneGraph::CellPair;
using nGZSceneGraph::cMaterialParser;
using nGZSceneGraph::cMaterialParserAdapters;
using nGZSceneGraph::OwnedCell;

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}

void Junk(std::mt19937 &g, void *p, size_t n)
{
    auto *b = static_cast<uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(g());
}

std::string gLog;
const uint8_t *gObj = nullptr; // the object of the side running: `this` pointers are logged as offsets in it
uint32_t gBaseResult = 0;

std::string Offset(const void *p) { return Hex(P(p) - P(gObj)); }

constexpr uint32_t kCacheObject = 0x10f9070; // GetCachedObjectB's cache

// ---- the fake service object the cells open and close through ---------------------------------------------------------
// Slot 5 (open: a, b) answers a key from a, slot 6 (use) and slot 7 (close) log.

uint32_t KeyOf(uint32_t a) { return a * 3 + 0x100; }
uint32_t __fastcall SvcOpen(void *, void *, uint32_t a, uint32_t b)
{
    gLog += "open " + Hex(a) + " " + Hex(b) + "\n";
    return KeyOf(a);
}
uint32_t __fastcall SvcUse(void *, void *, uint32_t v)
{
    gLog += "use " + Hex(v) + "\n";
    return 0;
}
uint32_t __fastcall SvcClose(void *, void *, uint32_t v)
{
    gLog += "close " + Hex(v) + "\n";
    return 0;
}
// Slot 8 (a cell's text, Dispatch8): "t<handle>", logged.
std::map<uint32_t, std::string> gCellTexts;
uint32_t __fastcall SvcText(void *, void *, uint32_t v)
{
    gLog += "text " + Hex(v) + "\n";
    std::string &text = gCellTexts[v];
    if (text.empty()) text = "t" + Hex(v);
    return P(text.c_str());
}
void *gSvcVt[9];
void *gSvcObject[1] = {gSvcVt};

struct ServiceFake {
    void **cache;
    void *saved;
    explicit ServiceFake(difftest::Context &ctx) : cache(ctx.image.At<void *>(kCacheObject)), saved(*cache)
    {
        std::memset(gSvcVt, 0, sizeof(gSvcVt));
        gSvcVt[5] = reinterpret_cast<void *>(&SvcOpen);
        gSvcVt[6] = reinterpret_cast<void *>(&SvcUse);
        gSvcVt[7] = reinterpret_cast<void *>(&SvcClose);
        gSvcVt[8] = reinterpret_cast<void *>(&SvcText);
        *cache = gSvcObject;
    }
    ~ServiceFake() { *cache = saved; }
};

// ---- fake counted objects: slot 1 adds, slot 2 releases (logged by id) ------------------------------------------------

struct FakeObj {
    void **vt;
    uint32_t id;
};
uint32_t __fastcall ObjAddRef(void *self, void *)
{
    gLog += "addref " + Hex(static_cast<FakeObj *>(self)->id) + "\n";
    return 1;
}
uint32_t __fastcall ObjRelease(void *self, void *)
{
    gLog += "release " + Hex(static_cast<FakeObj *>(self)->id) + "\n";
    return 0;
}
void *gObjVt[3] = {nullptr, reinterpret_cast<void *>(&ObjAddRef), reinterpret_cast<void *>(&ObjRelease)};
std::deque<FakeObj> gPool;

struct Pop {
    uint32_t next = 0;
    void *Fake()
    {
        gPool.push_back({gObjVt, ++next});
        return &gPool.back();
    }
    void *MaybeFake(std::mt19937 &g) { return g() % 3 == 0 ? nullptr : Fake(); }
};

std::string FakeText(const void *p)
{
    if (!p) return "null";
    return "obj" + Hex(static_cast<const FakeObj *>(p)->id);
}

// ---- an object for the body tests -------------------------------------------------------------------------------------
// kSize + 0x40 bytes of junk with what the bodies read: the vbtable (+8, the game's), the vtordisps zero, the faces'
// vtables (the cIBaseParser and cIGZUnknown ones copies of the game's with some slots replaced), and random members.

struct FaceVTable {
    void *slots[51];
    explicit FaceVTable(difftest::Context &ctx) { std::memcpy(slots, ctx.image.At(cMaterialParser::kParserVTable), sizeof(slots)); }
};
struct UnknownVTable {
    void *slots[3];
    explicit UnknownVTable(difftest::Context &ctx) { std::memcpy(slots, ctx.image.At(cMaterialParser::kUnknownVTable), sizeof(slots)); }
};

void FillText(std::string &s, std::mt19937 &g)
{
    static const size_t kLens[] = {0, 3, 15, 16, 33, 100};
    const size_t len = kLens[g() % 6];
    const size_t cap = g() % 5 == 0 ? 5000 : len;
    s.assign(cap, static_cast<char>('a' + g() % 26));
    s.resize(len);
    for (size_t i = 0; i < len; ++i) s[i] = static_cast<char>('a' + g() % 26);
}

size_t Count(std::mt19937 &g, size_t limit) { return g() % (limit + 1); }

template <class T, class Make> void FillVec(std::vector<T> &v, std::mt19937 &g, Make make)
{
    const size_t n = Count(g, 4);
    const size_t cap = g() % 6 == 0 ? 0x400 : n + g() % 3;
    if (cap) {
        v.assign(cap, make());
        v.resize(n);
        for (T &e : v) e = make();
    }
}

void FillRefs(RefPtrVector &r, std::mt19937 &g, Pop &pop)
{
    const size_t n = Count(g, 3);
    const size_t cap = g() % 6 == 0 ? 0x400 : n + g() % 3;
    r.mFirst = r.mLast = r.mEnd = nullptr;
    if (!cap) return;
    void **mem = std::allocator<void *>().allocate(cap);
    for (size_t i = 0; i < cap; ++i) mem[i] = reinterpret_cast<void *>(0xdead0000u + i);
    for (size_t i = 0; i < n; ++i) mem[i] = pop.Fake();
    r.mFirst = mem;
    r.mLast = mem + n;
    r.mEnd = mem + cap;
}

uint32_t Handle(std::mt19937 &g) { return g() % 5 == 0 ? 0xffffffffu : g() % 0x60 + 1; }

void FillMembers(cMaterialParser *o, std::mt19937 &g, Pop &pop)
{
    FillRefs(o->mRefs, g, pop);
    o->mCellA.cell.mValue = Handle(g);
    o->mCellB.cell.mValue = Handle(g);
    o->mState = g() % 3 == 0 ? 1 : static_cast<int32_t>(g() % 3);
    o->mObject1d8 = pop.MaybeFake(g);
    o->mObject1dc = pop.MaybeFake(g);
    ::new (&o->mList1e0) std::vector<uint32_t>();
    ::new (&o->mList1ec) std::vector<nGZSceneGraph::Record34>();
    FillVec(o->mList1e0, g, [&] { return g(); });
    FillVec(o->mList1ec, g, [&] {
        nGZSceneGraph::Record34 r;
        for (uint32_t &w : r.words) w = g();
        return r;
    });
    o->mFlag1f8 = static_cast<uint8_t>(g());
    o->mObject1fc = pop.MaybeFake(g);
    o->mObject200 = pop.MaybeFake(g);
    o->mObject204 = pop.MaybeFake(g);
    ::new (&o->mText208) std::string();
    ::new (&o->mText22c) std::string();
    ::new (&o->mText244) std::string();
    FillText(o->mText208, g);
    FillText(o->mText22c, g);
    FillText(o->mText244, g);
    ::new (&o->mList220) std::vector<uint32_t>();
    FillVec(o->mList220, g, [&] { return g(); });
    ::new (&o->mList25c) std::vector<Blob12>();
    FillVec(o->mList25c, g, [&] { return Blob12{{g(), g(), g()}}; });
    Junk(g, o->mFlags268, 8);
    o->mFlag270 = static_cast<uint8_t>(g());
    o->mFlag271 = static_cast<uint8_t>(g());
    o->mFlag272 = static_cast<uint8_t>(g());
    o->mField274 = static_cast<int32_t>(g());
    o->mField278 = static_cast<int32_t>(g());
    o->mField27c = static_cast<int32_t>(g());
    o->mField280 = static_cast<int32_t>(g());
    o->mField284 = static_cast<int32_t>(g());
    ::new (&o->mMap2c8) std::map<OwnedCell, std::vector<CellPair>, nGZSceneGraph::CellLess>();
    ::new (&o->mCells2d0) std::vector<CellPair>();
    ::new (&o->mMap2dc) std::map<OwnedCell, OwnedCell, nGZSceneGraph::CellLess>();
    FillVec(o->mCells2d0, g, [&] { return CellPair{OwnedCell(g() % 0x60 + 1), OwnedCell(Handle(g))}; });
    for (size_t i = Count(g, 4); i > 0; --i) {
        std::vector<CellPair> value;
        for (size_t j = Count(g, 2); j > 0; --j) value.push_back(CellPair{OwnedCell(g() % 0x60 + 1), OwnedCell(Handle(g))});
        o->mMap2c8.emplace(OwnedCell(Handle(g)), value);
    }
    for (size_t i = Count(g, 5); i > 0; --i) o->mMap2dc.emplace(OwnedCell(KeyOf(g() % 8)), OwnedCell(g() % 0x60 + 1));
    // The key the erase looks for sometimes is in the map: a handle that opens to a key present.
    if (g() % 2) o->mMap2dc.emplace(OwnedCell(KeyOf(3)), OwnedCell(g() % 0x60 + 1));
    if (g() % 4 == 0) o->mMap2dc.emplace(OwnedCell(0xffffffffu), OwnedCell(g() % 0x60 + 1));
}

cMaterialParser *NewObject(std::mt19937 &g, const FaceVTable *face, const UnknownVTable *unknown, Pop &pop)
{
    auto *o = static_cast<cMaterialParser *>(::operator new(cMaterialParser::kSize + 0x80));
    Junk(g, o, cMaterialParser::kSize + 0x80);
    *reinterpret_cast<const void **>(o->mBase + 8) = GameAddress<void>(cMaterialParser::kVbTable);
    o->mUnknownVtorDisp = 0;
    o->mParserVtorDisp = 0;
    o->mMaterialVtorDisp = 0;
    o->mUnknownFaceVTable = unknown ? static_cast<const void *>(unknown->slots) : GameAddress<void>(cMaterialParser::kUnknownVTable);
    o->mParserFaceVTable = face ? static_cast<const void *>(face->slots) : GameAddress<void>(cMaterialParser::kParserVTable);
    o->mMaterialFaceVTable = GameAddress<void>(cMaterialParser::kMaterialVTable);
    o->mFace2VbTable = GameAddress<const int32_t>(cMaterialParser::kFace2VbTable);
    o->mMaterialFaceVbTable = GameAddress<const int32_t>(cMaterialParser::kMaterialFaceVbTable);
    FillMembers(o, g, pop);
    return o;
}

void DestroyMembers(cMaterialParser *o)
{
    std::destroy_at(&o->mMap2dc);
    std::destroy_at(&o->mCells2d0);
    std::destroy_at(&o->mMap2c8);
    std::destroy_at(&o->mList25c);
    std::destroy_at(&o->mText244);
    std::destroy_at(&o->mText22c);
    std::destroy_at(&o->mList220);
    std::destroy_at(&o->mText208);
    std::destroy_at(&o->mList1ec);
    std::destroy_at(&o->mList1e0);
    if (o->mRefs.mFirst) std::allocator<void *>().deallocate(o->mRefs.mFirst, static_cast<size_t>(o->mRefs.mEnd - o->mRefs.mFirst));
}

void FreeObject(cMaterialParser *o)
{
    DestroyMembers(o);
    ::operator delete(o);
}

// Two objects made from the same junk and members (one generator state each).
struct Pair {
    cMaterialParser *o[2];
    Pair(uint32_t seed, const FaceVTable *face = nullptr, const UnknownVTable *unknown = nullptr)
    {
        for (int side = 0; side < 2; ++side) {
            std::mt19937 g(seed);
            Pop pop;
            o[side] = NewObject(g, face, unknown, pop);
        }
    }
    ~Pair()
    {
        FreeObject(o[0]);
        FreeObject(o[1]);
    }
};

// ---- what an object holds, as text ------------------------------------------------------------------------------------

template <class V> void DescribeVec(std::string &s, const V &v)
{
    s += " vec " + std::to_string(v.size()) + "/" + std::to_string(v.capacity()) + ":";
    if (v.capacity()) {
        const auto *b = reinterpret_cast<const uint8_t *>(v.data());
        for (size_t i = 0; i < v.capacity() * sizeof(typename V::value_type); ++i) s += " " + Hex(b[i]);
    }
}

void DescribeText(std::string &s, const std::string &t)
{
    s += " text " + std::to_string(t.size()) + "/" + std::to_string(t.capacity()) + ":";
    for (size_t i = 0; i <= t.size(); ++i) s += " " + Hex(static_cast<uint8_t>(t.data()[i]));
}

void DescribeTree(std::string &s, const uint8_t *node, const uint8_t *head, bool vectors)
{
    if (node == head) {
        s += " -";
        return;
    }
    s += " (" + Hex(node[12]) + "/" + Hex(P(node) == 0 ? 0 : *reinterpret_cast<const uint32_t *>(node + 0x10));
    if (vectors) {
        const auto *v = reinterpret_cast<const std::vector<CellPair> *>(node + 0x14);
        for (const CellPair &c : *v) s += " " + Hex(c.first.cell.mValue) + ":" + Hex(c.second.cell.mValue);
    } else {
        s += " " + Hex(*reinterpret_cast<const uint32_t *>(node + 0x14));
    }
    s += ")";
    DescribeTree(s, *reinterpret_cast<const uint8_t *const *>(node), head, vectors);
    DescribeTree(s, *reinterpret_cast<const uint8_t *const *>(node + 8), head, vectors);
}

std::string Describe(const cMaterialParser *o)
{
    std::vector<uint8_t> raw(reinterpret_cast<const uint8_t *>(o), reinterpret_cast<const uint8_t *>(o) + cMaterialParser::kSize);
    auto blank = [&](size_t off, size_t n) { std::memset(raw.data() + off, 0, n); };
    blank(0x1ac, 12);
    blank(0x1d8, 8);
    blank(0x1e0, 12);
    blank(0x1ec, 12);
    blank(0x1fc, 12);
    blank(0x208, 24);
    blank(0x220, 12);
    blank(0x22c, 24);
    blank(0x244, 24);
    blank(0x25c, 12);
    blank(0x2c8, 4);
    blank(0x2d0, 12);
    blank(0x2dc, 4);
    std::string s;
    for (uint8_t b : raw) s += Hex(b) + " ";
    s += "\nrefs " + std::to_string(o->mRefs.mLast - o->mRefs.mFirst) + "/" + std::to_string(o->mRefs.mEnd - o->mRefs.mFirst) + ":";
    for (void **p = o->mRefs.mFirst; p < o->mRefs.mEnd; ++p) s += " " + (p < o->mRefs.mLast ? FakeText(*p) : Hex(P(*p)));
    s += "\nobjects " + FakeText(o->mObject1d8) + " " + FakeText(o->mObject1dc) + " " + FakeText(o->mObject1fc) + " " +
         FakeText(o->mObject200) + " " + FakeText(o->mObject204);
    s += "\nlists";
    DescribeVec(s, o->mList1e0);
    DescribeVec(s, o->mList1ec);
    DescribeVec(s, o->mList220);
    DescribeVec(s, o->mList25c);
    DescribeVec(s, o->mCells2d0);
    s += "\ntexts";
    DescribeText(s, o->mText208);
    DescribeText(s, o->mText22c);
    DescribeText(s, o->mText244);
    const uint8_t *head2c8 = *reinterpret_cast<const uint8_t *const *>(&o->mMap2c8);
    const uint8_t *head2dc = *reinterpret_cast<const uint8_t *const *>(&o->mMap2dc);
    s += "\nmap2c8 " + std::to_string(o->mMap2c8.size()) + ":";
    DescribeTree(s, *reinterpret_cast<const uint8_t *const *>(head2c8 + 4), head2c8, true);
    s += "\nmap2dc " + std::to_string(o->mMap2dc.size()) + ":";
    DescribeTree(s, *reinterpret_cast<const uint8_t *const *>(head2dc + 4), head2dc, false);
    return s;
}

// ---- stand-ins for the cBaseParser calls ---------------------------------------------------------------------------
// The image's bodies take the face cBaseParser has (+0x1b0 / +0x1b8 of its part); ours are members on the part itself.

const cMaterialParser *AsObject(const cBaseParser *p) { return reinterpret_cast<const cMaterialParser *>(p); }

bool __fastcall ImageBaseInitialise(void *face, void *, uint32_t caseSensitive)
{
    const cBaseParser *p = cBaseParser::FromParserFace(face);
    gLog += "base Initialise this " + Offset(p) + " case " + Hex(caseSensitive & 0xff) + "\n" + Describe(AsObject(p)) + "\n";
    return (gBaseResult & 1) != 0;
}
bool __fastcall OurBaseInitialise(cBaseParser *self, void *, uint32_t caseSensitive)
{
    gLog += "base Initialise this " + Offset(self) + " case " + Hex(caseSensitive & 0xff) + "\n" + Describe(AsObject(self)) + "\n";
    return (gBaseResult & 1) != 0;
}
void __fastcall ImageBaseResetBlocks(void *face, void *)
{
    const cBaseParser *p = cBaseParser::FromParserFace(face);
    gLog += "base ResetBlocks this " + Offset(p) + "\n" + Describe(AsObject(p)) + "\n";
}
void __fastcall OurBaseResetBlocks(cBaseParser *self, void *)
{
    gLog += "base ResetBlocks this " + Offset(self) + "\n" + Describe(AsObject(self)) + "\n";
}
bool __fastcall ImageBaseQueryInterface(void *face, void *, uint32_t iid, void **out)
{
    const cBaseParser *p = cBaseParser::FromUnknownFace(face);
    gLog += "base QueryInterface this " + Offset(p) + " iid " + Hex(iid) + " out " + (out ? "set" : "null") + "\n";
    return (gBaseResult & 1) != 0;
}
bool __fastcall OurBaseQueryInterface(cBaseParser *self, void *, uint32_t iid, void **out)
{
    gLog += "base QueryInterface this " + Offset(self) + " iid " + Hex(iid) + " out " + (out ? "set" : "null") + "\n";
    return (gBaseResult & 1) != 0;
}
// The destructor's base call (0xafe1b0) takes the part itself on both sides.
void __fastcall BaseDestruct(cBaseParser *self, void *)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(self);
    // Ours are named by the image's table they stand for (the original's object holds the image's, ours holds our tables).
    auto vt = [&](size_t off, const void *own, uint32_t image) { return Hex(w[off / 4] == P(own) ? P(GameAddress<void>(image)) : w[off / 4]); };
    gLog += "base Destruct this " + Offset(self) + " vtables " + vt(0, cMaterialParser::kOwnVTable, cMaterialParser::kVTable) + " " +
            vt(0x2e8, cMaterialParser::kOwnUnknownVTable, cMaterialParser::kUnknownVTable) + " " +
            vt(0x2f0, cMaterialParser::kOwnParserVTable, cMaterialParser::kParserVTable) + " " +
            vt(0x2fc, cMaterialParser::kOwnMaterialVTable, cMaterialParser::kMaterialVTable) + " vtordisps " + Hex(w[0x2e4 / 4]) + " " + Hex(w[0x2ec / 4]) + " " + Hex(w[0x2f8 / 4]) + "\n";
}

// Face slot 2 (SetVersionRange), 4 (RegisterCommand) and 7 (RegisterFunction): logged; the objects freed.
void __fastcall FakeSetVersionRange(void *face, void *, int32_t low, int32_t high)
{
    gLog += "range on " + Offset(face) + " " + std::to_string(low) + " " + std::to_string(high) + "\n";
}
void __fastcall FakeRegisterCommand(void *face, void *, void *command)
{
    gLog += "command on " + Offset(face);
    if (command) {
        const uint32_t *w = static_cast<const uint32_t *>(command);
        const size_t size = _msize(command);
        gLog += " vtable " + Hex(w[0] - P(GameAddress<void>(0))) + " parser " + Hex(w[1]) + " count " + Hex(w[2]) + " block " + std::to_string(size);
        if (size >= 0x40) {
            gLog += " value " + Hex(w[3]);
            static_cast<cp::cBlockCommandBase *>(command)->Destruct();
        }
        ::operator delete(command);
    } else {
        gLog += " null";
    }
    gLog += "\n";
}
void __fastcall FakeRegisterFunction(void *face, void *, void *function)
{
    gLog += "function on " + Offset(face);
    if (function) {
        void *object = static_cast<char *>(function) - 4;
        const uint32_t *w = static_cast<const uint32_t *>(object);
        gLog += " vtable " + Hex(w[0] - P(GameAddress<void>(0))) + " face " + Hex(w[1] - P(GameAddress<void>(0))) + " count " + Hex(w[2]) +
                " block " + std::to_string(_msize(object));
        ::operator delete(object);
    } else {
        gLog += " null";
    }
    gLog += "\n";
}

std::string FreedSizes(const difftest::FreeRecorder &frees)
{
    size_t sizes[512];
    const size_t n = frees.SortedSizes(sizes, 512);
    std::string s = "frees " + std::to_string(frees.Count()) + (frees.Overflowed() ? " overflow" : "") + ":";
    for (size_t i = 0; i < n; ++i) s += " " + std::to_string(sizes[i]);
    return s;
}

// The first line where two multi-line texts differ, and the text up to there (the head of each side's line).
std::string Diff(const std::string &a, const std::string &b)
{
    size_t i = 0, line = 0, start = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) {
        if (a[i] == '\n') { ++line; start = i + 1; }
        ++i;
    }
    auto lineAt = [&](const std::string &t) {
        size_t e = t.find('\n', start);
        std::string l = t.substr(start, e == std::string::npos ? std::string::npos : e - start);
        return l.size() > 400 ? l.substr(0, 400) + "..." : l;
    };
    // Show where in the line: from a little before the difference.
    size_t col = i - start;
    auto cut = [&](std::string l) { return col > 60 ? "..." + l.substr(std::min(col - 60, l.size())) : l; };
    return "line " + std::to_string(line) + " col " + std::to_string(col) + "\n  A: " + cut(lineAt(a)) + "\n  B: " + cut(lineAt(b));
}

int32_t Vtordisp(std::mt19937 &g, int round) { return round % 4 == 3 ? 0x20 + 4 * static_cast<int32_t>(g() % 8) : 0; }

} // namespace

// 0xacec90 (__thiscall, ret 4): the whole parser (flag 1, and flags with other bits) and the parser as a base (flag 0, the
// virtual bases' words then left as the caller had them). Compared: the result and the parser Form of all 0x304 bytes
// (the maps' head pointers blanked and their head nodes compared by their words).
DIFF_TEST(MaterialParser_Construct, 0xacec90)
{
    Allocators allocators;
    ServiceFake service(ctx);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xacec90);
    std::mt19937 g(0xacec90);
    for (int round = 0; round < 40; ++round) {
        const uint32_t flag = round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() | 1u);
        alignas(16) uint8_t a[cMaterialParser::kSize], b[cMaterialParser::kSize];
        Junk(g, a, sizeof(a));
        if (!flag) {
            const uint32_t vb = P(GameAddress<void>(cMaterialParser::kVbTable));
            const uint32_t face2 = P(GameAddress<void>(cMaterialParser::kFace2VbTable));
            const uint32_t faceM = P(GameAddress<void>(cMaterialParser::kMaterialFaceVbTable));
            std::memcpy(a + 8, &vb, 4);
            std::memcpy(a + 0x2f4, &face2, 4);
            std::memcpy(a + 0x300, &faceM, 4);
        }
        std::memcpy(b, a, sizeof(a));
        void *ra = orig(a, flag);
        void *rb = cMaterialParserAdapters::Construct(reinterpret_cast<cMaterialParser *>(b), nullptr, flag);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("round %d: returned %p / %p, expected this", round, ra, rb);
        std::string heads[2];
        std::vector<uint8_t> forms[2];
        uint8_t *objects[2] = {a, b};
        for (int side = 0; side < 2; ++side) {
            std::vector<uint8_t> copy(objects[side], objects[side] + cMaterialParser::kSize);
            for (size_t off : {size_t(0x2c8), size_t(0x2dc)}) {
                const uint32_t head = parserform::Word(copy.data(), off);
                const uint8_t *h = parserform::Ptr(head);
                heads[side] += " head" + Hex(static_cast<uint32_t>(off)) + ":";
                for (int w = 0; w < 4; ++w) {
                    const uint32_t word = parserform::Word(h, 4 * w);
                    heads[side] += " " + (word == head ? std::string("self") : Hex(w == 3 ? (word & 0xffff) : word));
                }
                heads[side] += " msize " + std::to_string(_msize(const_cast<uint8_t *>(h)));
                std::memset(copy.data() + off, 0, 4);
            }
            // The vtable and virtual base table words differ by design (the original's in the image, ours in our tables):
            // each side's checked against its own, then blanked.
            struct Word { size_t off; const void *original, *ours; };
            const Word words[] = {
                {0x000, GameAddress<void>(cMaterialParser::kVTable), cMaterialParser::kOwnVTable},
                {0x2e8, GameAddress<void>(cMaterialParser::kUnknownVTable), cMaterialParser::kOwnUnknownVTable},
                {0x2f0, GameAddress<void>(cMaterialParser::kParserVTable), cMaterialParser::kOwnParserVTable},
                {0x2fc, GameAddress<void>(cMaterialParser::kMaterialVTable), cMaterialParser::kOwnMaterialVTable},
                {0x008, GameAddress<void>(cMaterialParser::kVbTable), cMaterialParser::kOwnVbTable},
                {0x2f4, GameAddress<void>(cMaterialParser::kFace2VbTable), cMaterialParser::kOwnFace2VbTable},
                {0x300, GameAddress<void>(cMaterialParser::kMaterialFaceVbTable), cMaterialParser::kOwnMaterialFaceVbTable},
            };
            for (const Word &w : words) {
                // Flag 0 leaves the virtual base table words as the caller had them (the image's on both sides).
                const bool vbWord = w.off == 0x008 || w.off == 0x2f4 || w.off == 0x300;
                const void *want = side == 0 || (!flag && vbWord) ? w.original : w.ours;
                if (parserform::Word(copy.data(), w.off) != P(want))
                    ctx.Fail("round %d (flag %08x): side %d word %03zx is %08x, expected %08x", round, flag, side, w.off,
                             parserform::Word(copy.data(), w.off), P(want));
                std::memset(copy.data() + w.off, 0, 4);
            }
            forms[side] = parserform::ParserForm(copy.data(), copy.size(), true);
        }
        if (forms[0] != forms[1]) ctx.Fail("round %d (flag %08x): the objects differ from byte %ld", round, flag, parserform::FirstDiff(forms[0], forms[1]));
        if (heads[0] != heads[1]) ctx.Fail("round %d (flag %08x): the heads differ:\n%s\n%s", round, flag, heads[0].c_str(), heads[1].c_str());
        const auto *oa = reinterpret_cast<const cMaterialParser *>(a);
        if (heads[0].find("self self") == std::string::npos || oa->mState != 0 || oa->mFlag271 != 1 || oa->mField274 != 4 || oa->mField27c != -1)
            ctx.Fail("round %d: the original's object does not have the layout's fields (%s)", round, heads[0].c_str());
        for (uint8_t *object : objects) {
            for (size_t off : {size_t(0x2c8), size_t(0x2dc)}) ::operator delete(const_cast<uint8_t *>(parserform::Ptr(parserform::Word(object, off))));
            parserform::TearDownParser(*reinterpret_cast<cBaseParser *>(object));
        }
    }
}

namespace {

// The members for the destructor tests: both sides' objects destroyed by the side's destructor (the base destructor, 0xafe1b0,
// stood in for on both), compared by the log (releases, closes, the base call), the sizes of the blocks freed and whether the
// object itself was freed.
template <class Run> void RunDestroy(difftest::Context &ctx, uint32_t seed, Run run)
{
    Allocators allocators;
    ServiceFake service(ctx);
    shape::StandIn base(0xafe1b0u, &cBaseParser::Destruct, &BaseDestruct);
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::Destruct), shape::CodeOf(&BaseDestruct));
    std::mt19937 g(seed);
    for (int round = 0; round < 80; ++round) {
        const uint32_t flags = round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : g();
        const uint32_t objectSeed = g();
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 og(objectSeed);
            Pop pop;
            cMaterialParser *o = NewObject(og, nullptr, nullptr, pop);
            gObj = reinterpret_cast<const uint8_t *>(o);
            gLog.clear();
            // The members' storage (their sizes) before the call: the same on both sides.
            bool freed;
            std::string sizes;
            void *r;
            {
                difftest::FreeRecorder frees;
                r = run(side, o, flags);
                frees.Stop();
                freed = frees.Freed(o);
                sizes = FreedSizes(frees);
            }
            got[side] = gLog + "-> " + Offset(r) + " object freed " + std::to_string(freed) + " " + sizes;
            if (!freed) ::operator delete(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (flags %08x): %s", round, flags, Diff(got[0], got[1]).c_str());
    }
}

} // namespace

// 0xacf040 (__thiscall, plain ret): the members destroyed in the original's order. The object is not freed.
DIFF_TEST(MaterialParser_Destruct, 0xacf040)
{
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0xacf040);
    RunDestroy(ctx, 0xacf040, [&](int side, cMaterialParser *o, uint32_t) -> void * {
        if (side == 0) orig(o);
        else cMaterialParserAdapters::Destruct(o, nullptr);
        return o;
    });
}

// Primary slot 0 (0xacfd90, ret 4): flags 0, 1 and others; the memory freed when bit 0 is set.
DIFF_TEST(MaterialParser_DeletingDestructor, 0xacfd90)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0xacfd90);
    RunDestroy(ctx, 0xacfd90, [&](int side, cMaterialParser *o, uint32_t flags) -> void * {
        if (side == 0) return orig(o, flags);
        return cMaterialParserAdapters::DeletingDestructor(o, nullptr, flags);
    });
}

namespace {

// Runs a body through its thunk (the original's) or our forwarder, on the face moved by a vtordisp (the thunk takes it off).
template <class Fn, class... A>
std::string RunFace(int side, uint32_t thunk, Fn ours, difftest::Context &ctx, cMaterialParser *o, uint32_t faceOffset, int32_t vtordisp, A... args)
{
    using R = std::invoke_result_t<Fn, void *, void *, A...>;
    using Sig = R(__fastcall *)(void *, void *, A...);
    uint8_t *faceAt = reinterpret_cast<uint8_t *>(o) + faceOffset + vtordisp;
    struct Restore { // also when a throw escapes the body
        uint8_t *at;
        alignas(4) uint8_t saved[8];
        ~Restore() { std::memcpy(at, saved, 8); }
    } restore{faceAt - 4, {}};
    std::memcpy(restore.saved, faceAt - 4, 8);
    std::memcpy(faceAt - 4, &vtordisp, 4);
    gLog.clear();
    std::string result;
    if constexpr (std::is_void_v<R>) {
        (side == 0 ? ctx.Original<Sig>(thunk) : reinterpret_cast<Sig>(ours))(faceAt, nullptr, args...);
        result = "-> void";
    } else {
        const R r = (side == 0 ? ctx.Original<Sig>(thunk) : reinterpret_cast<Sig>(ours))(faceAt, nullptr, args...);
        if constexpr (std::is_pointer_v<R>)
            result = "-> " + Hex(P(r));
        else
            result = "-> " + Hex(static_cast<uint32_t>(r) & (std::is_same_v<R, bool> ? 0xffu : 0xffffffffu));
    }
    return result;
}

// A slot whose body is hooked as well as its thunk: in body rounds the original's body is called with the face (the vtordisp
// field zero) against our adapter as the body's hook installs it; otherwise the thunk against our forwarder.
bool gBodyRound = false;
template <auto Adapter, class... A>
std::string RunSlot(int side, uint32_t thunk, uint32_t body, difftest::Context &ctx, cMaterialParser *o, uint32_t faceOffset, int32_t vtordisp, A... args)
{
    if (gBodyRound) return RunFace(side, body, Adapter, ctx, o, faceOffset, 0, args...);
    return RunFace(side, thunk, &nGZCommandParser::VtordispThunk<Adapter>::Call, ctx, o, faceOffset, vtordisp, args...);
}

} // namespace

// Slot 0 of the cIBaseParser face (thunk 0xad8b8b -> 0xad8ba0, ret 4): the case flag (with junk in the argument's upper
// bytes), the base's result 0 or 1, vtordisps 0 and others. Logged: the base call (its `this`, flag, the object then), the
// version range, the nine commands and fourteen functions registered through the face (their words, the blocks' sizes).
DIFF_TEST(MaterialParser_Initialise, 0xad8ba0)
{
    Allocators allocators;
    ServiceFake service(ctx);
    FaceVTable face(ctx);
    face.slots[2] = reinterpret_cast<void *>(&FakeSetVersionRange);
    face.slots[4] = reinterpret_cast<void *>(&FakeRegisterCommand);
    face.slots[7] = reinterpret_cast<void *>(&FakeRegisterFunction);
    shape::StandIn base(0xb02880u, &cBaseParser::Initialise, &ImageBaseInitialise);
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::Initialise), shape::CodeOf(&OurBaseInitialise));
    std::mt19937 g(0xad8ba0);
    for (int round = 0; round < 40; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        const uint32_t arg = (g() & 0xffffff00u) | (round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() & 0xff));
        gBaseResult = g() & 1;
        Pair pair(g(), &face);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cMaterialParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            const std::string r = RunFace(side, 0xad8b8b, &nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::Initialise>::Call, ctx, o, 0x2f0, vtordisp, arg);
            got[side] = gLog + r + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d arg %08x base %u): %s", round, vtordisp, arg, gBaseResult, Diff(got[0], got[1]).c_str());
        if (round == 0 && got[0].find("command on") == std::string::npos) ctx.Fail("the original registered no command:\n%s", got[0].c_str());
    }
}

// Slot 10 (thunk 0xada9c8 -> 0xada9d0): the per-run state emptied. Random members (counted pointers present or not, lists and
// strings with storage or without, states 0..2). The base's body (0xb03e50) stood in for; logged with the object when
// it is called (the base is called first), then the object's description and whether each container kept its storage.
DIFF_TEST(MaterialParser_ResetBlocks, 0xada9d0)
{
    Allocators allocators;
    ServiceFake service(ctx);
    shape::StandIn base(0xb03e50u, &cBaseParser::ResetBlocks, &ImageBaseResetBlocks);
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::ResetBlocks), shape::CodeOf(&OurBaseResetBlocks));
    std::mt19937 g(0xada9d0);
    for (int round = 0; round < 120; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        Pair pair(g());
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cMaterialParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            const void *storage[7] = {o->mList1e0.data(), o->mList1ec.data(), o->mList220.data(), o->mList25c.data(), o->mText208.data(), o->mText22c.data(), o->mText244.data()};
            const std::string r = RunFace(side, 0xada9c8, &nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::ResetBlocks>::Call, ctx, o, 0x2f0, vtordisp);
            const void *after[7] = {o->mList1e0.data(), o->mList1ec.data(), o->mList220.data(), o->mList25c.data(), o->mText208.data(), o->mText22c.data(), o->mText244.data()};
            got[side] = gLog + r + "\n" + Describe(o);
            for (int i = 0; i < 7; ++i) got[side] += storage[i] == after[i] ? " same" : " moved";
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d): %s", round, vtordisp, Diff(got[0], got[1]).c_str());
    }
}

// Slot 5 of the cIMaterialParser face (thunk 0xad13b4 -> 0xad13c0): the state word cleared, one counted pointer dropped, the
// flag byte cleared and two lists emptied.
DIFF_TEST(MaterialParser_ResetState, 0xad13c0)
{
    Allocators allocators;
    ServiceFake service(ctx);
    std::mt19937 g(0xad13c0);
    for (int round = 0; round < 120; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        Pair pair(g());
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cMaterialParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            const void *storage[2] = {o->mList1e0.data(), o->mList1ec.data()};
            const std::string r = RunFace(side, 0xad13b4, &nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::ResetState>::Call, ctx, o, 0x2fc, vtordisp);
            got[side] = gLog + r + "\n" + Describe(o) + (storage[0] == o->mList1e0.data() ? " same" : " moved") + (storage[1] == o->mList1ec.data() ? " same" : " moved");
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d): %s", round, vtordisp, Diff(got[0], got[1]).c_str());
    }
}

// Slot 8 of the cIMaterialParser face (thunk 0xad8b6c -> 0xad8b80): whether the state word is one (states 0, 1, 2, others).
DIFF_TEST(MaterialParser_IsStateOne, 0xad8b6c)
{
    Allocators allocators;
    ServiceFake service(ctx);
    std::mt19937 g(0xad8b6c);
    static const int32_t kStates[] = {0, 1, 2, -1, 0x100, 0x10001, INT32_MIN};
    for (int round = 0; round < 84; ++round) {
        gBodyRound = round / 7 % 3 == 2; // every state through the body (0xad8b80, hooked too) as well as the thunk
        const int32_t vtordisp = gBodyRound ? (Vtordisp(g, round), 0) : Vtordisp(g, round);
        Pair pair(g());
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cMaterialParser *o = pair.o[side];
            o->mState = kStates[round % 7];
            gObj = reinterpret_cast<const uint8_t *>(o);
            const std::string r = RunSlot<&cMaterialParserAdapters::IsStateOne>(side, 0xad8b6c, 0xad8b80, ctx, o, 0x2fc, vtordisp);
            got[side] = gLog + r + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1])
            ctx.Fail("round %d (%s, vtordisp %d state %d): %s", round, gBodyRound ? "body" : "thunk", vtordisp, kStates[round % 7], Diff(got[0], got[1]).c_str());
    }
    gBodyRound = false;
}

// Slot 3 of the cIMaterialParser face (thunk 0xadc88a -> 0xadc8a0, ret 4): the entry of the second map for the key the handle
// opens to removed (the original's 0xae87d0 opens the cell for real: handle 0 gives the invalid key). Handles whose key is
// in the map and not; maps empty or full. Compared: the log (open, closes of the node's cells, the local cell's close),
// the tree's shape, the blocks freed.
DIFF_TEST(MaterialParser_EraseCell, 0xadc8a0)
{
    Allocators allocators;
    ServiceFake service(ctx);
    std::mt19937 g(0xadc8a0);
    for (int round = 0; round < 200; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        const uint32_t handle = round % 9 == 0 ? 0u : round % 5 == 0 ? 0xffffffffu : g() % 10;
        Pair pair(g());
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cMaterialParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            difftest::FreeRecorder frees;
            const std::string r = RunFace(side, 0xadc88a, &nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::EraseCell>::Call, ctx, o, 0x2fc, vtordisp, handle);
            frees.Stop();
            got[side] = gLog + r + "\n" + FreedSizes(frees) + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d handle %x): %s", round, vtordisp, handle, Diff(got[0], got[1]).c_str());
    }
}

// Slot 0 of the cIGZUnknown face (thunk 0xadc7e9 -> 0xadc800, ret 8): the interface id of the material face (the face handed
// out and added through the unknown face's slot 1), others (the cBaseParser's QueryInterface, stood in for, called with the
// argument), and a null out pointer.
DIFF_TEST(MaterialParser_QueryInterface, 0xadc800)
{
    Allocators allocators;
    ServiceFake service(ctx);
    UnknownVTable unknown(ctx);
    struct Hooks {
        static uint32_t __fastcall AddRef(void *face, void *)
        {
            gLog += "addref on " + Offset(face) + "\n";
            return 1;
        }
    };
    unknown.slots[1] = reinterpret_cast<void *>(&Hooks::AddRef);
    shape::StandIn base(0xb05940u, &cBaseParser::QueryInterface, &ImageBaseQueryInterface);
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::QueryInterface), shape::CodeOf(&OurBaseQueryInterface));
    std::mt19937 g(0xadc800);
    static const uint32_t kIds[] = {cMaterialParser::kMaterialIID, cMaterialParser::kMaterialIID, 1u, 0x95035f4u, 0u, 0x4959875du, 0x4959875bu};
    for (int pass = 0; pass < 160; ++pass) {
        const int round = pass % 80;
        gBodyRound = pass >= 80; // the second 80 through the body (0xadc800, hooked too)
        const int32_t vtordisp = gBodyRound ? (Vtordisp(g, round), 0) : Vtordisp(g, round);
        const uint32_t iid = round < 14 ? kIds[round % 7] : round % 2 ? cMaterialParser::kMaterialIID : g();
        const bool nullOut = round % 5 == 4;
        gBaseResult = g() & 1;
        Pair pair(g(), nullptr, &unknown);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cMaterialParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            void *out = reinterpret_cast<void *>(0x12345678);
            const std::string r = RunSlot<&cMaterialParserAdapters::QueryInterface>(side, 0xadc7e9, 0xadc800, ctx, o, 0x2e8, vtordisp, iid,
                                          nullOut ? nullptr : &out);
            got[side] = gLog + r + " out " + (nullOut ? std::string("-") : out == reinterpret_cast<void *>(0x12345678) ? "untouched" : Offset(out)) + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1])
            ctx.Fail("round %d (%s, vtordisp %d iid %x null %d base %u): %s", round, gBodyRound ? "body" : "thunk", vtordisp, iid, nullOut, gBaseResult,
                     Diff(got[0], got[1]).c_str());
    }
    gBodyRound = false;
}

namespace {

// ---- the faces' shifted thunks --------------------------------------------------------------------------------------

alignas(16) uint8_t gBuffer[0x400];
std::string gSeen;
uint32_t gResult = 0;

template <class T> uint32_t WordOf(T v)
{
    if constexpr (std::is_pointer_v<T>) return P(v);
    else return static_cast<uint32_t>(v);
}

template <class T> T Make(std::mt19937 &g)
{
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<uintptr_t>(g()));
    else if constexpr (std::is_same_v<T, bool>) return (g() & 1) != 0;
    else return static_cast<T>(g());
}

template <class R> R Result()
{
    if constexpr (std::is_void_v<R>) return;
    else if constexpr (std::is_pointer_v<R>) return reinterpret_cast<R>(static_cast<uintptr_t>(gResult));
    else if constexpr (std::is_same_v<R, bool>) return (gResult & 1) != 0;
    else return static_cast<R>(gResult);
}

template <auto Adapter> struct Recorder;
template <class R, class... Args, R(__fastcall *Adapter)(void *, void *, Args...)> struct Recorder<Adapter> {
    static R __fastcall Call(void *self, void *, Args... args)
    {
        gSeen = "this " + Hex(P(self) - P(gBuffer));
        ((gSeen += " " + Hex(WordOf(args))), ...);
        return Result<R>();
    }
};

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

template <class R> std::string ResultText(R r)
{
    if constexpr (std::is_same_v<R, float>) {
        uint32_t bits;
        std::memcpy(&bits, &r, 4);
        return "f" + Hex(bits);
    } else return Hex(WordOf(r));
}

const int32_t kFixedVtordisps[] = {0, 4, 8, 0x10, -8, -0x14, 0x1b8, -0x100};
int32_t ThunkVtordisp(std::mt19937 &g, int i) { return i < 8 ? kFixedVtordisps[i] : static_cast<int32_t>(g() % 129) * 4 - 0x100; }

// A thunk `sub ecx, [ecx-4]; sub ecx, 0x138; jmp body` whose body is one of cBaseParser's (its registered hook the adapter):
// the image's thunk and our hook of it called on a face with a vtordisp, the body stood in for on both sides.
template <auto Adapter> struct Check;
template <class R, class... Args, R(__fastcall *Adapter)(void *, void *, Args...)> struct Check<Adapter> {
    static void Run(difftest::Context &ctx, uint32_t thunk, const char *name, std::mt19937 &g)
    {
        using Fn = R(__fastcall *)(void *, void *, Args...);
        const uint8_t *code = ctx.image.At<uint8_t>(thunk);
        static const uint8_t kPrefix[] = {0x2b, 0x49, 0xfc, 0x81, 0xe9, 0x38, 0x01, 0x00, 0x00, 0xe9};
        if (std::memcmp(code, kPrefix, sizeof(kPrefix)) != 0) {
            ctx.Fail("%s %x: not a shifted vtordisp thunk", name, thunk);
            return;
        }
        int32_t rel;
        std::memcpy(&rel, code + 10, 4);
        const uint32_t body = thunk + 14 + rel;
        const HookSpec *bodyHook = HookAt(body), *thunkHook = HookAt(thunk);
        if (!bodyHook || bodyHook->replacement != reinterpret_cast<void *>(Adapter)) {
            ctx.Fail("%s %x: the body %x is not hooked to this adapter", name, thunk, body);
            return;
        }
        if (!thunkHook) {
            ctx.Fail("%s %x: the thunk is not hooked", name, thunk);
            return;
        }
        shape::StandIn stand(body, Adapter, &Recorder<Adapter>::Call);
        for (int i = 0; i < 24; ++i) {
            const int32_t d = ThunkVtordisp(g, i);
            uint8_t *face = gBuffer + 0x200;
            std::memcpy(face - 4, &d, 4);
            const std::tuple<Args...> args{Make<Args>(g)...};
            gResult = g();
            std::string got[2];
            for (int side = 0; side < 2; ++side) {
                gSeen = "not reached";
                const Fn fn = side == 0 ? ctx.Original<Fn>(thunk) : reinterpret_cast<Fn>(thunkHook->replacement);
                if constexpr (std::is_void_v<R>) {
                    std::apply([&](Args... a) { fn(face, nullptr, a...); }, args);
                    got[side] = gSeen;
                } else {
                    const R r = std::apply([&](Args... a) { return fn(face, nullptr, a...); }, args);
                    got[side] = gSeen + " -> " + ResultText(r);
                }
            }
            ++ctx.cases;
            if (got[0] != got[1]) ctx.Fail("%s %x vtordisp %d: %s / %s", name, thunk, d, Diff(got[0], got[1]).c_str());
        }
    }
};

// A `sub ecx, [ecx-4]; jmp body` thunk: its body, and the thunk hooked to the forwarder `ours`.
void CheckOwnThunk(difftest::Context &ctx, uint32_t thunk, uint32_t body, const void *ours, const char *name)
{
    const uint8_t *code = ctx.image.At<uint8_t>(thunk);
    if (!(code[0] == 0x2b && code[1] == 0x49 && code[2] == 0xfc && code[3] == 0xe9)) {
        ctx.Fail("%s %x: not a vtordisp thunk", name, thunk);
        return;
    }
    int32_t rel;
    std::memcpy(&rel, code + 4, 4);
    ++ctx.cases;
    if (thunk + 8 + rel != body) ctx.Fail("%s %x: the body is %x, not %x", name, thunk, thunk + 8 + rel, body);
    const HookSpec *h = HookAt(thunk);
    if (!h || h->replacement != ours) ctx.Fail("%s %x: not hooked to its forwarder", name, thunk);
    if (thunk != 0xad8b6c && thunk != 0xadc7e9 && !HookAt(body)) ctx.Fail("%s %x: the body %x is not hooked", name, thunk, body);
}

} // namespace

// Every shifted thunk of the two faces (+0x2e8: 0xe79e48 slots 1, 2; +0x2f0: 0xe79e58, all but 0, 9, 10 and the folded 11, 45;
// 33 shares 32's), each with its cBaseParser body's adapter; the own thunks (the cIGZUnknown face's slot 0, the cIBaseParser
// face's 0 and 10, the cIMaterialParser face's 3, 5 and 8: their bodies and forwarders); the cIBaseParser face's slot 9
// (thunk 0xad02ec -> 0xad0300, `add ecx, -0x138; jmp 0xaff120`); slots 11 and 45 (shifted thunks into folded bodies) called
// on random buffers.
DIFF_TEST(MaterialParser_FaceThunks, 0xacfde0)
{
    std::mt19937 g(0xacfde0);
#define XLIST \
    X(0xacfde0, AddRef) \
    X(0xadc87c, Release) \
    X(0xadcdf3, Shutdown) \
    X(0xadcd9f, SetVersionRange) \
    X(0xadcdbb, SetContextState) \
    X(0xadc860, RegisterCommand) \
    X(0xadce47, UnregisterCommand) \
    X(0xadce39, UnregisterCommandObject) \
    X(0xadc86e, RegisterMetaCommand) \
    X(0xadca4e, RestartViaReset) \
    X(0xadc5a6, RunFile) \
    X(0xadc5de, RunFiles) \
    X(0xadc5d0, RunText) \
    X(0xadc598, RunLine) \
    X(0xadce0f, PushOpenBlock) \
    X(0xadce1d, PushMetaBlock) \
    X(0xad13a6, PopMetaBlock) \
    X(0xadcde5, RunExpandedText) \
    X(0xadc7db, PushNamespace) \
    X(0xadc7bf, PopNamespace) \
    X(0xadc7cd, PurgeNamespace) \
    X(0xad8b5e, GetFunctions) \
    X(0xadcdc9, SetOutput) \
    X(0xadcdd7, SetHandler) \
    X(0xadcdad, SetSource) \
    X(0xadce55, FindVariable) \
    X(0xad12c0, GetNamespacePath) \
    X(0xadce2b, ExpandVariables) \
    X(0xadc58a, EvalBool) \
    X(0xadc5ec, EvalFloat) \
    X(0xadc5fa, EvalInt) \
    X(0xadc795, EvalVector2) \
    X(0xadc7a3, EvalVector3) \
    X(0xadc7b1, EvalVector4) \
    X(0xadc608, EvalColorRGBA) \
    X(0xadc616, EvalColorARGB) \
    X(0xadc787, EvalColorRGB) \
    X(0xad9a46, CommandCount) \
    X(0xad03ab, CommandAt) \
    X(0xad93dc, IsInitialised) \
    X(0xad93ce, IsCaseSensitive) \
    X(0xad93ea, GetText) \
    X(0xadc5c2, GetFileName) \
    X(0xadc5b4, GetLine) \
    X(0xad12ce, HasDefinition) \
    X(0xad03b9, HasCommand) \
    X(0xadce01, GetSource)
#define X(rva, adapter) Check<&cBaseParserAdapters::adapter>::Run(ctx, rva, #adapter, g);
    XLIST
#undef X
    CheckOwnThunk(ctx, 0xadc7e9, 0xadc800, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::QueryInterface>::Call), "QueryInterface");
    CheckOwnThunk(ctx, 0xad8b8b, 0xad8ba0, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::Initialise>::Call), "Initialise");
    CheckOwnThunk(ctx, 0xada9c8, 0xada9d0, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::ResetBlocks>::Call), "ResetBlocks");
    CheckOwnThunk(ctx, 0xadc88a, 0xadc8a0, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::EraseCell>::Call), "EraseCell");
    CheckOwnThunk(ctx, 0xadcb84, 0xadcb90, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::SetPair>::Call), "SetPair");
    CheckOwnThunk(ctx, 0xadc919, 0xadc930, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::RemovePair>::Call), "RemovePair");
    CheckOwnThunk(ctx, 0xadca5c, 0xadca70, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::SetAlias>::Call), "SetAlias");
    CheckOwnThunk(ctx, 0xad13b4, 0xad13c0, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::ResetState>::Call), "ResetState");
    CheckOwnThunk(ctx, 0xad8b6c, 0xad8b80, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::IsStateOne>::Call), "IsStateOne");

    // Slot 9: the thunk reaches cBaseParser's Reset (0xaff120) with the face shifted by 0x138.
    {
        const HookSpec *thunkHook = HookAt(0xad02ec);
        if (!thunkHook) {
            ctx.Fail("slot 9: the thunk 0xad02ec is not hooked");
            return;
        }
        const uint8_t *code = ctx.image.At<uint8_t>(0xad02ec);
        static const uint8_t kThunk[] = {0x2b, 0x49, 0xfc, 0xe9};
        static const uint8_t kShift[] = {0x81, 0xc1, 0xc8, 0xfe, 0xff, 0xff, 0xe9};
        int32_t rel;
        std::memcpy(&rel, code + 4, 4);
        const uint8_t *tail = ctx.image.At<uint8_t>(0xad02ec + 8 + rel);
        int32_t rel2;
        std::memcpy(&rel2, tail + 7, 4);
        if (std::memcmp(code, kThunk, 4) != 0 || std::memcmp(tail, kShift, 7) != 0 || 0xad02ec + 8 + rel + 11 + rel2 != 0xaff120u) {
            ctx.Fail("slot 9: the thunk 0xad02ec does not have the expected shape");
        } else {
            using Fn = void(__fastcall *)(void *, void *);
            shape::StandIn stand(0xaff120u, &cBaseParserAdapters::Reset, &Recorder<&cBaseParserAdapters::Reset>::Call);
            for (int i = 0; i < 24; ++i) {
                const int32_t d = ThunkVtordisp(g, i);
                uint8_t *face = gBuffer + 0x200;
                std::memcpy(face - 4, &d, 4);
                std::string got[2];
                for (int side = 0; side < 2; ++side) {
                    gSeen = "not reached";
                    (side == 0 ? ctx.Original<Fn>(0xad02ec) : reinterpret_cast<Fn>(thunkHook->replacement))(face, nullptr);
                    got[side] = gSeen;
                }
                ++ctx.cases;
                if (got[0] != got[1]) ctx.Fail("slot 9 vtordisp %d: %s / %s", d, Diff(got[0], got[1]).c_str());
            }
        }
    }

    // Slots 11 and 45: no stand-in (the folded bodies are run), the buffer's words random.
    const HookSpec *noOp = HookAt(0xad03c7), *version = HookAt(0xadce63);
    if (!noOp || !version) {
        ctx.Fail("slots 11 / 45 not hooked");
        return;
    }
    for (int i = 0; i < 64; ++i) {
        Junk(g, gBuffer, sizeof(gBuffer));
        const int32_t d = ThunkVtordisp(g, i % 24);
        uint8_t *face = gBuffer + 0x200;
        std::memcpy(face - 4, &d, 4);
        using VersionFn = int32_t(__fastcall *)(void *, void *);
        const int32_t a = ctx.Original<VersionFn>(0xadce63)(face, nullptr);
        const int32_t b = reinterpret_cast<VersionFn>(version->replacement)(face, nullptr);
        using NoOpFn = void(__fastcall *)(void *, void *);
        std::vector<uint8_t> before(gBuffer, gBuffer + sizeof(gBuffer));
        ctx.Original<NoOpFn>(0xad03c7)(face, nullptr);
        reinterpret_cast<NoOpFn>(noOp->replacement)(face, nullptr);
        ++ctx.cases;
        if (a != b) ctx.Fail("slot 45 vtordisp %d: %d / %d", d, a, b);
        if (std::memcmp(before.data(), gBuffer, sizeof(gBuffer)) != 0) ctx.Fail("slot 11 wrote to the buffer");
    }
}

// A whole parser: the original's built (0xacec90) and initialised (thunk 0xad8b8b) by the image, ours by our code, both on the
// real cBaseParser (the real face vtable, the commands and functions registered for real). Compared: the command count
// (face slot 40), HasCommand (slot 49) for the nine names, and the parsers' Forms; then each destroyed by its own side's
// deleting destructor (the original's through its vtable slot 0).
DIFF_TEST(MaterialParser_Whole, 0xad8ba0)
{
    Allocators allocators;
    ServiceFake service(ctx);
    using CountFn = uint32_t(__thiscall *)(void *);
    using HasFn = uint32_t(__thiscall *)(void *, const char *);
    static const char *const kNames[] = {"FlushMaterials", "BindMaterials", "EnableSoftwareVertexPrograms", "EnableShaderProgramDebugging",
                                         "MaterialDefinition", "DefaultState", "DefaultMaterial", "ShaderProgram", "Material", "NoSuchCommand"};
    for (int round = 0; round < 6; ++round) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            auto *o = static_cast<cMaterialParser *>(::operator new(cMaterialParser::kSize));
            void *face;
            if (side == 0) {
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xacec90)(o, 1);
                face = reinterpret_cast<uint8_t *>(o) + 0x2f0;
                ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xad8b8b)(face, round & 1);
            } else {
                o->Construct(1);
                face = o->ParserFace();
                o->Initialise(static_cast<uint8_t>(round & 1));
            }
            auto **vtable = *reinterpret_cast<void ***>(face);
            got[side] = "commands " + std::to_string(reinterpret_cast<CountFn>(vtable[40])(face));
            for (const char *name : kNames) got[side] += std::string(" ") + name + "=" + std::to_string(reinterpret_cast<HasFn>(vtable[49])(face, name) & 0xff);
            got[side] += " state " + std::to_string(o->mState) + " " + Describe(o).substr(0, 0);
            if (side == 0) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xacfd90)(o, 1);
            else o->DeletingDestructor(1);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d: %s", round, Diff(got[0], got[1]).c_str());
        if (round == 0 && got[0].find("commands 0") == 0) ctx.Fail("the original's parser took no commands: %s", got[0].c_str());
    }
}

// Our vtables against the image's: every slot of the primary vtable and the three faces holds the forwarder that the hook of the
// image's entry installs (so the object's slot reaches the same adapter the image's thunk did, which the other tests check
// against the original), the slot counts are the originals', the virtual base tables' words are the image's, and the object our
// constructor builds dispatches slots through them: the same results as the original's object, slot for slot.
DIFF_TEST(MaterialParser_VTables, 0xe79e30)
{
    struct Table { const char *name; uint32_t image; const void *const *ours; size_t count; };
    const Table tables[] = {
        {"primary", cMaterialParser::kVTable, cMaterialParser::kOwnVTable, std::size(cMaterialParser::kOwnVTable)},
        {"cIGZUnknown", cMaterialParser::kUnknownVTable, cMaterialParser::kOwnUnknownVTable, std::size(cMaterialParser::kOwnUnknownVTable)},
        {"cIBaseParser", cMaterialParser::kParserVTable, cMaterialParser::kOwnParserVTable, std::size(cMaterialParser::kOwnParserVTable)},
        {"cIMaterialParser", cMaterialParser::kMaterialVTable, cMaterialParser::kOwnMaterialVTable, std::size(cMaterialParser::kOwnMaterialVTable)},
    };
    const uint32_t base = P(GameAddress<void>(0));
    for (const Table &t : tables) {
        const uint32_t *image = GameAddress<uint32_t>(t.image);
        for (size_t i = 0; i < t.count; ++i) {
            ++ctx.cases;
            // Primary slots 2 and 3 are the folded setter and getter (0x960960, 0x5b1920 shared by unrelated classes, not hookable):
            // ours are this class's own adapters, checked by what they do below.
            if (t.ours == cMaterialParser::kOwnVTable && (i == 2 || i == 3)) continue;
            const HookSpec *h = HookAt(image[i] - base);
            if (!h) ctx.Fail("%s slot %zu: the image's entry %x is not hooked", t.name, i, image[i] - base);
            else if (h->replacement != t.ours[i]) ctx.Fail("%s slot %zu: ours is %p, the hook of %x installs %p", t.name, i, t.ours[i], image[i] - base, h->replacement);
            if (!t.ours[i]) ctx.Fail("%s slot %zu: empty", t.name, i);
        }
        // The image's table ends where ours does: the word after it is not another entry of the table (a function entry) --
        // checked as the next vtable's start or a vbtable, which are the neighbouring data.
    }
    ++ctx.cases;
    if (GameAddress<int32_t>(cMaterialParser::kVbTable)[0] != cMaterialParser::kOwnVbTable[0] || std::memcmp(GameAddress<int32_t>(cMaterialParser::kVbTable), cMaterialParser::kOwnVbTable, sizeof(cMaterialParser::kOwnVbTable)) != 0)
        ctx.Fail("the cBaseParser part's virtual base table differs from the image's");
    if (std::memcmp(GameAddress<int32_t>(cMaterialParser::kFace2VbTable), cMaterialParser::kOwnFace2VbTable, sizeof(cMaterialParser::kOwnFace2VbTable)) != 0)
        ctx.Fail("the cIBaseParser face's virtual base table differs from the image's");
    if (std::memcmp(GameAddress<int32_t>(cMaterialParser::kMaterialFaceVbTable), cMaterialParser::kOwnMaterialFaceVbTable, sizeof(cMaterialParser::kOwnMaterialFaceVbTable)) != 0)
        ctx.Fail("the cIMaterialParser face's virtual base table differs from the image's");

    // Dispatch on whole objects (the original's built by the image, ours by our constructor), both initialised.
    Allocators allocators;
    ServiceFake service(ctx);
    using Fn0 = uint32_t(__thiscall *)(void *);
    using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
    for (int round = 0; round < 4; ++round) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            auto *o = static_cast<cMaterialParser *>(::operator new(cMaterialParser::kSize));
            if (side == 0) {
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xacec90)(o, 1);
                ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xad8b8b)(reinterpret_cast<uint8_t *>(o) + 0x2f0, round & 1);
            } else {
                o->Construct(1);
                o->Initialise(static_cast<uint8_t>(round & 1));
            }
            uint8_t *unknown = reinterpret_cast<uint8_t *>(o) + 0x2e8, *parser = reinterpret_cast<uint8_t *>(o) + 0x2f0, *material = reinterpret_cast<uint8_t *>(o) + 0x2fc;
            auto **uv = *reinterpret_cast<void ***>(unknown);
            auto **pv = *reinterpret_cast<void ***>(parser);
            auto **mv = *reinterpret_cast<void ***>(material);
            std::string &s = got[side];
            auto word = [&](uint32_t v) { s += " " + Hex(v); };
            word(reinterpret_cast<Fn0>(pv[40])(parser));            // CommandCount
            word(reinterpret_cast<Fn0>(pv[42])(parser) & 0xff);     // IsInitialised
            word(reinterpret_cast<Fn0>(pv[43])(parser) & 0xff);     // IsCaseSensitive
            word(reinterpret_cast<Fn0>(pv[45])(parser));            // GetVersion
            word(reinterpret_cast<Fn1>(pv[49])(parser, P("FlushMaterials")) & 0xff); // HasCommand
            word(reinterpret_cast<Fn0>(mv[8])(material) & 0xff);    // IsStateOne
            word(reinterpret_cast<Fn0>(uv[1])(unknown));            // AddRef
            word(reinterpret_cast<Fn0>(uv[1])(unknown));
            word(reinterpret_cast<Fn0>(uv[2])(unknown));            // Release
            // The primary vtable's folded setter and getter (slots 2, 3).
            auto **vt = *reinterpret_cast<void ***>(o);
            for (uint32_t value : {0x1234u + round, 0u, 0xffffffffu}) {
                reinterpret_cast<Fn1>(vt[2])(o, value);
                word(reinterpret_cast<Fn0>(vt[3])(o));
                word(o->Parser()->mField158);
            }
            if (side == 0) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xacfd90)(o, 1);
            else reinterpret_cast<Fn1>(vt[0])(o, 1);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d: %s / %s", round, got[0].c_str(), got[1].c_str());
    }
}

// ---- the cell lists (cIMaterialParser slots 0, 1 and 2) --------------------------------------------------------------------

namespace {

// A sorted list of (name key, value) cells: the keys the names 1..5 open to (duplicates, and the empty cell's key last),
// the capacity exactly the size or with spare room (the insert's two paths: in place and reallocating).
void FillList(std::vector<CellPair> &v, std::mt19937 &g)
{
    v = std::vector<CellPair>();
    const size_t n = g() % 6;
    std::vector<uint32_t> keys;
    for (size_t i = 0; i < n; ++i) keys.push_back(g() % 12 == 0 ? 0xffffffffu : KeyOf(1 + g() % 5));
    std::sort(keys.begin(), keys.end());
    const size_t spare = g() % 3 == 0 ? 0 : 1 + g() % 3;
    v.reserve(n + spare);
    for (uint32_t k : keys) v.push_back(CellPair{OwnedCell(k), OwnedCell(Handle(g))});
}

// The unnamed list and the groups 1..3 (each present two times in three) replaced by sorted lists; the second map's entries
// keyed by what the names 1..5 open to.
void PrepareLists(cMaterialParser *o, uint32_t seed)
{
    std::mt19937 g(seed ^ 0x5eed);
    FillList(o->mCells2d0, g);
    o->mMap2c8.clear();
    for (uint32_t k = 1; k <= 3; ++k)
        if (g() % 3) FillList(o->mMap2c8[OwnedCell(KeyOf(k))], g);
    o->mMap2dc.clear();
    for (uint32_t k = 1; k <= 5; ++k)
        if (g() % 2) o->mMap2dc[OwnedCell(KeyOf(k))] = OwnedCell(g() % 0x60 + 1);
    if (g() % 4 == 0) o->mMap2dc[OwnedCell(0xffffffffu)] = OwnedCell(g() % 0x60 + 1);
}

void DescribeList(std::string &s, const std::vector<CellPair> &v)
{
    s += " list " + std::to_string(v.size()) + "/" + std::to_string(v.capacity()) + ":";
    for (const CellPair &c : v) s += " " + Hex(c.first.cell.mValue) + ":" + Hex(c.second.cell.mValue);
}

// What the three slots may change, compared by value (sizes, capacities, handles); the rest of the object as bytes.
std::string DescribeCells(const cMaterialParser *o)
{
    std::vector<uint8_t> raw(reinterpret_cast<const uint8_t *>(o), reinterpret_cast<const uint8_t *>(o) + cMaterialParser::kSize);
    auto blank = [&](size_t off, size_t n) { std::memset(raw.data() + off, 0, n); };
    blank(0x1ac, 12);
    blank(0x1d8, 8);
    blank(0x1e0, 12);
    blank(0x1ec, 12);
    blank(0x1fc, 12);
    blank(0x208, 24);
    blank(0x220, 12);
    blank(0x22c, 24);
    blank(0x244, 24);
    blank(0x25c, 12);
    blank(0x2c8, 0x1c);
    std::string s;
    for (uint8_t b : raw) s += Hex(b) + " ";
    s += "\nunnamed";
    DescribeList(s, o->mCells2d0);
    s += "\ngroups " + std::to_string(o->mMap2c8.size());
    for (const auto &entry : o->mMap2c8) {
        s += "\n " + Hex(entry.first.cell.mValue);
        DescribeList(s, entry.second);
    }
    s += "\naliases " + std::to_string(o->mMap2dc.size()) + ":";
    for (const auto &entry : o->mMap2dc) s += " " + Hex(entry.first.cell.mValue) + "=" + Hex(entry.second.cell.mValue);
    return s;
}

// One slot on prepared lists, through the original's thunk and our forwarder, the choices drawn from `draw`.
template <class Fn, class Draw> void RunListSlot(difftest::Context &ctx, uint32_t seed, uint32_t thunk, Fn ours, const char *what, int rounds, Draw draw)
{
    Allocators allocators;
    ServiceFake service(ctx);
    std::mt19937 g(seed);
    for (int round = 0; round < rounds; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        const uint32_t roundSeed = g();
        Pair pair(roundSeed);
        std::string got[2];
        std::string args;
        for (int side = 0; side < 2; ++side) {
            cMaterialParser *o = pair.o[side];
            PrepareLists(o, roundSeed);
            gObj = reinterpret_cast<const uint8_t *>(o);
            std::mt19937 pick(roundSeed ^ 0xa11);
            difftest::FreeRecorder frees;
            std::string r;
            draw(pick, args, [&](auto... a) { r = RunFace(side, thunk, ours, ctx, o, 0x2fc, vtordisp, a...); });
            frees.Stop();
            got[side] = gLog + r + "\n" + FreedSizes(frees) + "\n" + DescribeCells(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("%s round %d (vtordisp %d args%s): %s", what, round, vtordisp, args.c_str(), Diff(got[0], got[1]).c_str());
    }
}

} // namespace

// Slot 0 (thunk 0xadcb84 -> 0xadcb90, ret 0xc): the value of a name set in a group's list (group 0: the unnamed list; groups
// 1..3 present or not, 4 made). The names 0 (the empty cell, found among the last keys), 1..5 (in the lists, duplicated, or not)
// and 6, values 0 (the empty cell) and others, so the insert runs at the front, in the middle and at the end, in place and
// reallocating, and the assignment of a found entry. Compared: the service log (opens, uses, closes in order), the frees,
// each list's size, capacity and cells, the second map, the rest of the object.
DIFF_TEST(MaterialParser_SetPair, 0xadcb90)
{
    RunListSlot(ctx, 0xadcb90, 0xadcb84, &nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::SetPair>::Call, "SetPair", 600,
                [](std::mt19937 &pick, std::string &args, auto run) {
                    const uint32_t group = pick() % 3 == 0 ? 0 : 1 + pick() % 4;
                    const uint32_t name = pick() % 8 == 0 ? 0 : 1 + pick() % 6;
                    const uint32_t value = pick() % 6 == 0 ? 0 : 1 + pick() % 10;
                    args = " " + Hex(group) + " " + Hex(name) + " " + Hex(value);
                    run(group, name, value);
                });
}

// Slot 1 (thunk 0xadc919 -> 0xadc930, ret 8): a name removed from a group's list (the first of equal keys only), name 0
// emptying the list; groups as above (a group not present is made first).
DIFF_TEST(MaterialParser_RemovePair, 0xadc930)
{
    RunListSlot(ctx, 0xadc930, 0xadc919, &nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::RemovePair>::Call, "RemovePair", 600,
                [](std::mt19937 &pick, std::string &args, auto run) {
                    const uint32_t group = pick() % 3 == 0 ? 0 : 1 + pick() % 4;
                    const uint32_t name = pick() % 6 == 0 ? 0 : 1 + pick() % 6;
                    args = " " + Hex(group) + " " + Hex(name);
                    run(group, name);
                });
}

// Slot 2 (thunk 0xadca5c -> 0xadca70, ret 8): the second map's entry for a name set to another name's handle (made when
// missing; the names 0 as the source gives the empty cell, as the target is opened without the null test).
DIFF_TEST(MaterialParser_SetAlias, 0xadca70)
{
    RunListSlot(ctx, 0xadca70, 0xadca5c, &nGZCommandParser::VtordispThunk<&cMaterialParserAdapters::SetAlias>::Call, "SetAlias", 400,
                [](std::mt19937 &pick, std::string &args, auto run) {
                    const uint32_t from = pick() % 6 == 0 ? 0 : 1 + pick() % 7;
                    const uint32_t to = pick() % 6 == 0 ? 0 : 1 + pick() % 10;
                    args = " " + Hex(from) + " " + Hex(to);
                    run(from, to);
                });
}

// ---- the material builders (cIMaterialParser slots 4, 6 and 7) -------------------------------------------------------------
// The world: a material definition fake (slots 8, 19..22 logged, its answers the round's), the parser face's slots 15, 19..22
// logging fakes (RunLine also acts as a script would: builds a shader into +0x200, sets the state 6, throws), the settings
// service (game global 0x10c9378: its slot 5 hands out a property set whose slot 9 logs the key and the boolean and sets it),
// the current device (0x18ae40 stood in for on both sides: null, unusable or usable) and the director's framework (0x1ae18d
// stood in for: slot 3 hands out the stand-in or the shader fake, logged with where the result goes). The cell opens (the real
// 0xae87d0), uses, closes and texts (Dispatch8) go through the fake service object, logged. The unnamed cell list, the groups
// and the aliases are keyed by what the definition names open to, so the lookups hit and miss.

namespace {

constexpr uint32_t kServiceGlobal = 0x10c9378; // GetServiceObject's

const char *const kDefNames[] = {"matA", "matB", "matC", "matD"};
const char *const kDefTexts[] = {"scriptA", "scriptB"};
const char *const kVarTexts[] = {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"};

std::string Quoted(const char *s) { return s ? std::string("\"") + s + "\"" : std::string("null"); }

struct World {
    uint32_t name;
    const char *text;
    uint32_t count;
    uint32_t pairWords[4][4];
    bool pairInPlace[4];
    uint32_t objectWord;
    uint8_t setting;
    int device; // 0 none, 1 unusable, 2 usable
    struct Run {
        uint32_t shader; // 0: none built, else gShaders[shader]
        bool state6;
        int fail; // 0 none, 1 a cError (the builders catch it), 2 another type (escapes)
    } runs[2];
    int failPush; // as `fail`
    uint32_t runCalls;
    bool alias[4], group[4]; // what PrepareMaterial made, by name
};
World gWorld;

FakeObj gShaders[4];
FakeObj gStandIn, gMadeShader, gSet, gDefObject;
void *gShaderVt[22], *gSetVt[10], *gDefObjectVt[23], *gDefVt[23], *gDeviceVt[36], *gFrameworkVt[4], *gServiceVt[6];
void *gDefinition[1] = {gDefVt};
void *gDevice[1] = {gDeviceVt};
void *gFramework[1] = {gFrameworkVt};
void *gServiceObj[1] = {gServiceVt};
Blob12 gData;
alignas(4) uint8_t gInfo[0x18];
void *gOutObject;

// Where a pointer the code hands out points: the object's members, the caller's out or data, or elsewhere (a local).
std::string Where(const void *p)
{
    const auto *o = reinterpret_cast<const cMaterialParser *>(gObj);
    if (p == &o->mObject1fc) return "member1fc";
    if (p == &o->mObject200) return "member200";
    if (p == &o->mZero1c0[1]) return "member1c4";
    if (p == &gOutObject) return "out";
    if (p == &gData) return "data";
    return p ? "local" : "null";
}

uint32_t __fastcall DefObjectSlot(void *, void *)
{
    gLog += "def object\n";
    return P(&gDefObject);
}
// Slot 22 of the definition's resource: cSGResource::CopyKeyTo, as the game's: the whole 16-byte key (type, group,
// instance, resource) written to out, out returned. (A caller's buffer smaller than the key is overrun, as in the game.)
uint32_t __fastcall DefObjectWords(void *self, void *, uint32_t *out)
{
    gLog += "object key on " + FakeText(self) + " out " + Where(out) + "\n";
    out[0] = 0x1111;
    out[1] = gWorld.objectWord;
    out[2] = gWorld.objectWord ^ 0x5a5a5a5a;
    out[3] = ~gWorld.objectWord;
    return P(out);
}
uint32_t __fastcall DefName(void *, void *)
{
    gLog += "def name\n";
    return P(kDefNames[gWorld.name]);
}
uint32_t __fastcall DefText(void *, void *)
{
    gLog += "def text\n";
    return P(gWorld.text);
}
uint32_t __fastcall DefCount(void *, void *)
{
    gLog += "def count\n";
    return gWorld.count;
}
// Slot 22 (out, index): the pair written to the out, and the out's address answered or another pair's (the fake's own
// words 2 and 3), so that a reader of the out and a reader of the answer differ.
uint32_t __fastcall DefPair(void *, void *, uint32_t *out, uint32_t index)
{
    gLog += "def pair " + Hex(index) + " out " + Where(out) + "\n";
    uint32_t *words = gWorld.pairWords[index & 3];
    out[0] = words[0];
    out[1] = words[1];
    return gWorld.pairInPlace[index & 3] ? P(out) : P(words + 2);
}

// A script's failure: a cError (the type the builders catch, the game's vtable in it) or an int (which they don't).
struct Escaped {
    int code;
};
void Throw(int kind)
{
    if (kind == 1) {
        nGZCommandParser::cError error;
        error.mVTable = GameAddress<void>(nGZCommandParser::cError::kVTable);
        error.mMessage = "script failed";
        throw error;
    }
    if (kind == 2) throw Escaped{kind};
}

void __fastcall FakeRunLine(void *face, void *, const char *line)
{
    auto *o = cMaterialParser::FromParserFace(face);
    gLog += "run on " + Offset(face) + " " + Quoted(line) + " state " + std::to_string(o->mState) + " objects " + FakeText(o->mObject1fc) + " " +
            FakeText(o->mObject200) + "\n";
    const World::Run &run = gWorld.runs[gWorld.runCalls++ & 1];
    if (run.shader) o->mObject200 = &gShaders[run.shader];
    if (run.state6) o->mState = 6;
    Throw(run.fail);
}
void __fastcall FakeSetVariable(void *face, void *, const char *name, const char *value)
{
    gLog += "set on " + Offset(face) + " " + Quoted(name) + " = " + Quoted(value) + "\n";
}
void __fastcall FakePush(void *face, void *, const char *name)
{
    gLog += "push on " + Offset(face) + " " + Quoted(name) + "\n";
    Throw(gWorld.failPush);
}
void __fastcall FakePop(void *face, void *) { gLog += "pop on " + Offset(face) + "\n"; }
void __fastcall FakePurge(void *face, void *, const char *name) { gLog += "purge on " + Offset(face) + " " + Quoted(name) + "\n"; }

void __fastcall ShaderSet(void *self, void *, void *device, uint32_t value)
{
    gLog += "set shader " + FakeText(self) + " device " + (device == gDevice ? "fake" : device ? "other" : "null") + " " + Hex(value) + "\n";
}
uint32_t __fastcall ShaderContext(void *self, void *, uint32_t context, const uint32_t *data, uint32_t last)
{
    gLog += "context " + FakeText(self) + " " + Hex(context) + " data " + Where(data) + " " + Hex(data[0]) + " " + Hex(data[1]) + " " +
            Hex(data[2]) + " " + Hex(last) + "\n";
    return 0xc0de0000u + static_cast<FakeObj *>(self)->id;
}
uint32_t __fastcall SetGet(void *self, void *, const GZServiceString *key, uint8_t *any)
{
    const void *vt = *reinterpret_cast<const void *const *>(any);
    gLog += "setting on " + FakeText(self) + " " + Quoted(key->c_str()) + " any " +
            (vt == GameAddress<void>(gzany::kBooleanVTable) ? std::string("boolean") : Hex(P(vt))) + " " + Hex(any[4]) + "\n";
    any[4] = gWorld.setting;
    return 1;
}
uint32_t __fastcall ServiceGet(void *, void *, uint32_t cls, uint32_t iid, void **out)
{
    gLog += "service " + Hex(cls) + " " + Hex(iid) + " out " + Where(out) + "\n";
    *out = &gSet;
    return 1;
}
uint32_t __fastcall DeviceUsable(void *, void *)
{
    gLog += "device usable\n";
    return gWorld.device == 2 ? 1u : 0u;
}
void *__cdecl FakeCurrentDevice()
{
    gLog += "current device\n";
    return gWorld.device ? gDevice : nullptr;
}
uint32_t __fastcall FrameworkCreate(void *, void *, uint32_t cls, uint32_t iid, void **out)
{
    gLog += "framework create " + Hex(cls) + " " + Hex(iid) + " into " + Where(out) + " (was " + (Where(out) == "local" ? std::string("a local's") : Where(out) == "out" ? Hex(P(*out)) : FakeText(*out)) + ")\n";
    *out = cls == 0x7c17ff25 ? &gStandIn : &gMadeShader;
    return 1;
}
void *FakeFrameworkObject()
{
    gLog += "framework\n";
    return gFramework;
}

void FillBuilderTables()
{
    auto fn = [](auto f) { return reinterpret_cast<void *>(f); };
    std::memset(gShaderVt, 0, sizeof(gShaderVt));
    gShaderVt[1] = fn(&ObjAddRef);
    gShaderVt[2] = fn(&ObjRelease);
    gShaderVt[8] = fn(&ShaderSet);
    gShaderVt[21] = fn(&ShaderContext);
    for (uint32_t i = 0; i < 4; ++i) gShaders[i] = {gShaderVt, 0x200 + i};
    gStandIn = {gShaderVt, 0x300};
    gMadeShader = {gShaderVt, 0x301};
    std::memset(gSetVt, 0, sizeof(gSetVt));
    gSetVt[1] = fn(&ObjAddRef);
    gSetVt[2] = fn(&ObjRelease);
    gSetVt[9] = fn(&SetGet);
    gSet = {gSetVt, 0x400};
    std::memset(gDefObjectVt, 0, sizeof(gDefObjectVt));
    gDefObjectVt[22] = fn(&DefObjectWords);
    gDefObject = {gDefObjectVt, 0x500};
    std::memset(gDefVt, 0, sizeof(gDefVt));
    gDefVt[8] = fn(&DefObjectSlot);
    gDefVt[19] = fn(&DefName);
    gDefVt[20] = fn(&DefText);
    gDefVt[21] = fn(&DefCount);
    gDefVt[22] = fn(&DefPair);
    std::memset(gDeviceVt, 0, sizeof(gDeviceVt));
    gDeviceVt[35] = fn(&DeviceUsable);
    std::memset(gFrameworkVt, 0, sizeof(gFrameworkVt));
    gFrameworkVt[3] = fn(&FrameworkCreate);
    std::memset(gServiceVt, 0, sizeof(gServiceVt));
    gServiceVt[5] = fn(&ServiceGet);
}

uint32_t NameKey(uint32_t name) { return KeyOf(P(kDefNames[name])); }

// The round's world, drawn from `g`.
void DrawWorld(std::mt19937 &g)
{
    World &w = gWorld;
    w.name = g() % 4;
    w.text = g() % 5 == 0 ? nullptr : kDefTexts[g() % 2];
    w.count = g() % 4;
    for (int i = 0; i < 4; ++i) {
        w.pairWords[i][0] = P(kVarTexts[g() % 8]);
        w.pairWords[i][1] = P(kVarTexts[g() % 8]);
        w.pairWords[i][2] = P(kVarTexts[g() % 8]);
        w.pairWords[i][3] = P(kVarTexts[g() % 8]);
        w.pairInPlace[i] = g() % 2 != 0;
    }
    w.objectWord = g();
    w.setting = static_cast<uint8_t>(g() % 3 == 0 ? g() : g() % 2);
    w.device = static_cast<int>(g() % 5 == 0 ? 0 : g() % 4 == 0 ? 1 : 2);
    for (World::Run &run : w.runs) {
        run.shader = g() % 2 ? 1 + g() % 3 : 0;
        run.state6 = g() % 5 == 0;
        run.fail = g() % 8 == 0 ? 1 + static_cast<int>(g() % 3 == 0) : 0;
    }
    w.failPush = g() % 16 == 0 ? 1 + static_cast<int>(g() % 3 == 0) : 0;
    for (uint32_t &word : gData.words) word = g();
    std::memset(gInfo, 0x5a, sizeof(gInfo));
}

// The lists the builders read, keyed by the names' cells; the name cell at +0x1bc sometimes one of the names' (BuildShader
// looks its alias up before it sets the cell).
void PrepareMaterial(cMaterialParser *o, uint32_t seed)
{
    std::mt19937 g(seed ^ 0x3a7e);
    o->mCells2d0.clear();
    for (size_t i = g() % 4; i > 0; --i) o->mCells2d0.push_back(CellPair{OwnedCell(g() % 0x60 + 1), OwnedCell(Handle(g))});
    o->mMap2c8.clear();
    o->mMap2dc.clear();
    for (uint32_t k = 0; k < 4; ++k) {
        gWorld.group[k] = g() % 2 != 0;
        if (gWorld.group[k]) {
            std::vector<CellPair> &cells = o->mMap2c8[OwnedCell(NameKey(k))];
            for (size_t i = 1 + g() % 2; i > 0; --i) cells.push_back(CellPair{OwnedCell(g() % 0x60 + 1), OwnedCell(Handle(g))});
        }
        gWorld.alias[k] = g() % 3 == 0;
        if (gWorld.alias[k]) o->mMap2dc[OwnedCell(NameKey(k))] = OwnedCell(g() % 0x60 + 1);
    }
    if (g() % 2) o->mMap2c8[OwnedCell(KeyOf(7))];
    if (g() % 2) o->mMap2dc[OwnedCell(KeyOf(7))] = OwnedCell(5);
    if (g() % 2) o->mCellB.cell.mValue = NameKey(g() % 4);
}

std::string DescribeInfo(const cMaterialParser *o)
{
    const auto *w = reinterpret_cast<const uint32_t *>(gInfo);
    auto list = [&](uint32_t p, const void *data) { return p == P(data) ? std::string("data") : Hex(p); };
    return "info " + FakeText(reinterpret_cast<const void *>(static_cast<uintptr_t>(w[0]))) + " " + Hex(w[1]) + " " +
           list(w[2], o->mList1e0.data()) + " " + Hex(w[3]) + " " + list(w[4], o->mList1ec.data()) + " " + Hex(w[5]);
}

// One builder on prepared objects, through the original's thunk and our forwarder. `call` runs it and answers the result's
// text; `tags` names what the round reached (checked against `want` at the end).
template <class Call>
void RunBuilder(difftest::Context &ctx, uint32_t seed, const char *what, int rounds, std::vector<std::string> want, Call call)
{
    Allocators allocators;
    svcfx::AllocatorScope allocator;
    ServiceFake service(ctx);
    FillBuilderTables();
    FaceVTable face(ctx);
    face.slots[15] = reinterpret_cast<void *>(&FakeRunLine);
    face.slots[19] = reinterpret_cast<void *>(&FakeSetVariable);
    face.slots[20] = reinterpret_cast<void *>(&FakePush);
    face.slots[21] = reinterpret_cast<void *>(&FakePop);
    face.slots[22] = reinterpret_cast<void *>(&FakePurge);
    shape::StandIn device(0x18ae40u, &nGZGraphic4::GetCurrentDeviceCached, &FakeCurrentDevice);
    shape::StandIn framework(0x1ae18du, &GetDirectorFrameworkObject, &FakeFrameworkObject);
    void **global = ctx.image.At<void *>(kServiceGlobal);
    void *const savedGlobal = *global;
    *global = gServiceObj;
    std::vector<std::string> seen;
    std::mt19937 g(seed);
    for (int round = 0; round < rounds; ++round) {
        gBodyRound = round % 3 == 2;
        const int32_t vtordisp = gBodyRound ? (Vtordisp(g, round), 0) : Vtordisp(g, round);
        const uint32_t roundSeed = g();
        const uint32_t worldSeed = g();
        Pair pair(roundSeed, &face);
        std::string got[2];
        std::vector<std::string> tags;
        for (int side = 0; side < 2; ++side) {
            std::mt19937 wg(worldSeed);
            DrawWorld(wg);
            cMaterialParser *o = pair.o[side];
            PrepareMaterial(o, roundSeed);
            gObj = reinterpret_cast<const uint8_t *>(o);
            gWorld.runCalls = 0;
            gOutObject = reinterpret_cast<void *>(static_cast<uintptr_t>(0x5a5a5a5a));
            std::vector<std::string> sideTags;
            std::string r;
            try {
                r = call(side, o, vtordisp, sideTags);
            } catch (const Escaped &e) {
                r = "escaped " + std::to_string(e.code);
                sideTags.push_back("escaped");
            }
            got[side] = gLog + r + "\n" + Describe(o);
            if (side == 0) tags = sideTags;
            if (side == 0) tags.push_back(gBodyRound ? "body" : "thunk");
        }
        for (const std::string &t : tags)
            if (std::find(seen.begin(), seen.end(), t) == seen.end()) seen.push_back(t);
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("%s round %d (%s, vtordisp %d): %s", what, round, gBodyRound ? "body" : "thunk", vtordisp, Diff(got[0], got[1]).c_str());
    }
    gBodyRound = false;
    want.push_back("body");
    want.push_back("thunk");
    for (const std::string &t : want)
        if (std::find(seen.begin(), seen.end(), t) == seen.end()) ctx.Fail("%s: no round reached '%s'", what, t.c_str());
    *global = savedGlobal;
}

// The tags of a round's world, from the original's side (the log of the run).
void WorldTags(std::vector<std::string> &tags, const std::string &log)
{
    tags.push_back(gWorld.text ? "text" : "no text");
    tags.push_back(gWorld.alias[gWorld.name] ? "alias" : "no alias");
    tags.push_back(gWorld.group[gWorld.name] ? "group" : "no group");
    tags.push_back("count " + std::to_string(gWorld.count));
    tags.push_back("device " + std::to_string(gWorld.device));
    tags.push_back("runs " + std::to_string(gWorld.runCalls));
    if (log.find("run on") != std::string::npos && (gWorld.runs[0].fail || gWorld.failPush)) tags.push_back("threw");
    if (gWorld.failPush && log.find("push on") != std::string::npos) tags.push_back("push threw");
}

} // namespace

// Slot 4 (thunk 0xacfdee -> 0xacfe00, ret 8): the definition's script run in state 1 and the info handed back. Rounds: the text
// null or not, aliased or not, the material's group present or not, 0..3 pairs (written in place or answered from the fake's
// own words), the script building, failing or throwing (the catch skips the pop). Compared: the log, the result (the info
// buffer, its lists as "data" when they are the object's), the object.
DIFF_TEST(MaterialParser_ParseDefinition, 0xacfe00)
{
    RunBuilder(ctx, 0xacfe00, "ParseDefinition", 500, {"text", "no text", "alias", "no alias", "group", "no group", "count 0", "count 3", "threw", "push threw", "escaped"},
               [&](int side, cMaterialParser *o, int32_t vtordisp, std::vector<std::string> &tags) {
                   auto *info = reinterpret_cast<nGZSceneGraph::MaterialDefinitionInfo *>(gInfo);
                   std::string r = RunSlot<&cMaterialParserAdapters::ParseDefinition>(side, 0xacfdee, 0xacfe00, ctx, o, 0x2fc, vtordisp, info,
                                                                                      static_cast<void *>(gDefinition));
                   WorldTags(tags, gLog);
                   return r + " " + DescribeInfo(o);
               });
}

// Slot 6 (thunk 0xad03d5 -> 0xad03e0, ret 0x10): the shader built for a context. Rounds: no text (2), no device or an unusable
// one (the framework's stand-in), the script building a shader or not, the fallback script (the cell at +0x1b8 empty or not),
// the state 6 (1), throws (3). Compared: the log, the status and the shader handed out, the object.
DIFF_TEST(MaterialParser_BuildShader, 0xad03e0)
{
    RunBuilder(ctx, 0xad03e0, "BuildShader", 600,
               {"status 0", "status 1", "status 2", "status 3", "device 0", "device 1", "device 2", "runs 2", "alias", "group", "threw", "push threw", "escaped"},
               [&](int side, cMaterialParser *o, int32_t vtordisp, std::vector<std::string> &tags) {
                   const uint32_t context = static_cast<uint32_t>(gData.words[0] ^ 0x77);
                   std::string r = RunSlot<&cMaterialParserAdapters::BuildShader>(side, 0xad03d5, 0xad03e0, ctx, o, 0x2fc, vtordisp, &gOutObject, static_cast<void *>(gDefinition), context, static_cast<const Blob12 *>(&gData));
                   WorldTags(tags, gLog);
                   tags.push_back("status " + r.substr(3));
                   return r + " out " + FakeText(gOutObject);
               });
}

// Slot 7 (thunk 0xad0d79 -> 0xad0d90, ret 0x14): the error visualisation for reasons 0..5 (1..4 have scripts), the device
// absent, unusable or usable, the script building a shader or not (then the framework's stand-in), throws.
DIFF_TEST(MaterialParser_BuildErrorShader, 0xad0d90)
{
    RunBuilder(ctx, 0xad0d90, "BuildErrorShader", 600, {"device 0", "device 1", "device 2", "reason 0", "reason 1", "reason 2", "reason 3", "reason 4", "reason 5",
                                                        "built", "stand-in", "threw", "escaped"},
               [&](int side, cMaterialParser *o, int32_t vtordisp, std::vector<std::string> &tags) {
                   const uint32_t context = static_cast<uint32_t>(gData.words[0] ^ 0x77);
                   const int32_t reason = static_cast<int32_t>(gData.words[1] % 6);
                   std::string r = RunSlot<&cMaterialParserAdapters::BuildErrorShader>(side, 0xad0d79, 0xad0d90, ctx, o, 0x2fc, vtordisp, &gOutObject, static_cast<void *>(gDefinition), context, static_cast<const Blob12 *>(&gData), reason);
                   WorldTags(tags, gLog);
                   tags.push_back("reason " + std::to_string(reason));
                   tags.push_back(gLog.find("framework create") != std::string::npos ? "stand-in" : "built");
                   return r + " out " + FakeText(gOutObject);
               });
}
