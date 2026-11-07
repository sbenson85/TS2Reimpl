// Differential tests for cTSEffectsParser (lane WU, rendering E12; src/ts/cTSEffectsParser.h, src/hook/tables/cTSEffectsParser.cpp).
//
// The same method as tests/sg_material_parser.cpp. The constructor (0xbcce40) runs cBaseParser's constructor for real on both
// sides and is compared as the parser Form of the whole object plus the members' raw bytes (the containers' head pointers
// blanked, their head nodes compared by their words). The destructors, Reset, Initialise, QueryInterface, the setters, the
// lookup and the apply run on objects built of the same junk and random members on both sides (the containers' layouts are the
// game's; the counted pointers are fakes that log their calls). cBaseParser's own bodies they call are stood in for (the image's
// on the original's side, our member on ours, each logging the cBaseParser `this` as an offset); the registrars Initialise
// calls run for real on both, registering through a logging fake of the face's slots. The faces' thunks are checked like the
// material parser's. Last, a whole parser built by the image and one by our code are initialised through the real cBaseParser
// and compared by their registered commands.
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gz_base_parser_form.h"
#include "shape.h"

#include "gz/CommandParser.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "hook/hooks.h"
#include "ts/cTSEffectsParser.h"

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

namespace {

// New memory is filled with a pattern, so that words nobody writes are the same on both sides.
void *__cdecl GameNew(size_t n)
{
    void *p = ::operator new(n);
    std::memset(p, 0xa5, n);
    return p;
}
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

struct Log : std::string { // never shrinks or regrows between runs: the sides' heap traffic must not depend on earlier logs
    Log() { reserve(1 << 22); }
};
Log gLog;
const uint8_t *gObj = nullptr; // the object of the side running: `this` pointers are logged as offsets in it
uint32_t gBaseResult = 0;

std::string Offset(const void *p) { return Hex(P(p) - P(gObj)); }

// ---- fake counted objects: slot 1 adds, slot 2 releases, slot 3 applies a value, slot 4 lets go (logged by id) -----------------

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
void __fastcall ObjApply(void *self, void *, void *value)
{
    gLog += "apply " + Hex(static_cast<FakeObj *>(self)->id) + " " + Hex(P(value)) + "\n";
}
void __fastcall ObjLetGo(void *self, void *, uint32_t flag)
{
    gLog += "letgo " + Hex(static_cast<FakeObj *>(self)->id) + " " + Hex(flag) + "\n";
}
void *gObjVt[5] = {nullptr, reinterpret_cast<void *>(&ObjAddRef), reinterpret_cast<void *>(&ObjRelease), reinterpret_cast<void *>(&ObjApply),
                   reinterpret_cast<void *>(&ObjLetGo)};
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

// ---- an object for the body tests --------------------------------------------------------------------------------------------
// kSize + 0x80 bytes of junk with what the bodies read: the vbtable (+8, the game's), the vtordisps zero, the faces' vtables
// (the cIBaseParser and cIGZUnknown ones copies of the game's with some slots replaced), and random members.

struct FaceVTable {
    void *slots[51];
    explicit FaceVTable(difftest::Context &ctx) { std::memcpy(slots, ctx.image.At(cTSEffectsParser::kParserVTable), sizeof(slots)); }
};
struct UnknownVTable {
    void *slots[3];
    explicit UnknownVTable(difftest::Context &ctx) { std::memcpy(slots, ctx.image.At(cTSEffectsParser::kUnknownVTable), sizeof(slots)); }
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

// The keys the lookup is asked for (lower-cased "name:number").
const char *const kKeys[] = {"fire:1", "fire:2", "water:0", "a:5", "zeta:-1", ":0"};

void FillMembers(cTSEffectsParser *o, std::mt19937 &g, Pop &pop)
{
    o->mObject1ac = pop.MaybeFake(g);
    o->mDescriptions = pop.MaybeFake(g);
    o->mObject1b4 = pop.MaybeFake(g);
    o->mObject1b8 = pop.MaybeFake(g);
    o->mObject1bc = pop.MaybeFake(g);
    Junk(g, o->mZero1c0, sizeof(o->mZero1c0));
    o->mFlag1d0 = static_cast<uint8_t>(g());
    std::map<std::string, uint32_t> *maps[] = {&o->mMap1d4, &o->mMap1dc, &o->mMap1e4, &o->mMap1ec};
    for (auto *m : maps) {
        ::new (m) std::map<std::string, uint32_t>();
        for (size_t i = Count(g, 4); i > 0; --i) {
            std::string key;
            FillText(key, g);
            m->emplace(key, g());
        }
    }
    o->mText1f4.Construct();
    FillText(o->mText1f4.mString, g);
    o->mZero214 = g();
    o->mWord218 = static_cast<uint16_t>(g());
    Junk(g, o->mZero21c, sizeof(o->mZero21c));
    ::new (&o->mList230) std::vector<EffectsRecord88>();
    FillVec(o->mList230, g, [&] {
        EffectsRecord88 r;
        FillText(r.text, g);
        Junk(g, r.rest, sizeof(r.rest));
        return r;
    });
    o->mText23c.Construct();
    FillText(o->mText23c.mString, g);
    Junk(g, o->mUntouched25c, sizeof(o->mUntouched25c));
    o->mWord276 = static_cast<uint16_t>(g());
    ::new (&o->mSet278) std::set<EffectsBlob56, EffectsBlob56Less>();
    for (size_t i = Count(g, 4); i > 0; --i) {
        EffectsBlob56 b;
        FillText(b.first, g);
        FillText(b.second, g);
        b.middle = g();
        b.last = g();
        o->mSet278.insert(b);
    }
    ::new (&o->mMap280) std::map<std::string, std::string>();
    for (size_t i = Count(g, 5); i > 0; --i) {
        std::string value;
        FillText(value, g);
        o->mMap280.emplace(kKeys[g() % 6], value);
    }
    FillRefs(o->mRefs288, g, pop);
    ::new (&o->mValues294) std::vector<void *>();
    const size_t refs = o->mRefs288.mLast - o->mRefs288.mFirst;
    const size_t cap = g() % 6 == 0 ? 0x400 : refs + g() % 3;
    if (cap) {
        o->mValues294.assign(cap, nullptr);
        o->mValues294.resize(refs + g() % 2);
        for (void *&v : o->mValues294) v = reinterpret_cast<void *>(static_cast<uintptr_t>(g()));
    }
}

cTSEffectsParser *NewObject(std::mt19937 &g, const FaceVTable *face, const UnknownVTable *unknown, Pop &pop)
{
    auto *o = static_cast<cTSEffectsParser *>(::operator new(cTSEffectsParser::kSize + 0x80));
    Junk(g, o, cTSEffectsParser::kSize + 0x80);
    *reinterpret_cast<const void **>(o->mBase + 8) = GameAddress<void>(cTSEffectsParser::kVbTable);
    o->mUnknownVtorDisp = 0;
    o->mParserVtorDisp = 0;
    o->mEffectsVtorDisp = 0;
    o->mUnknownFaceVTable = unknown ? static_cast<const void *>(unknown->slots) : GameAddress<void>(cTSEffectsParser::kUnknownVTable);
    o->mParserFaceVTable = face ? static_cast<const void *>(face->slots) : GameAddress<void>(cTSEffectsParser::kParserVTable);
    o->mEffectsFaceVTable = GameAddress<void>(cTSEffectsParser::kEffectsVTable);
    o->mFace2VbTable = GameAddress<const int32_t>(cTSEffectsParser::kFace2VbTable);
    o->mEffectsFaceVbTable = GameAddress<const int32_t>(cTSEffectsParser::kEffectsFaceVbTable);
    FillMembers(o, g, pop);
    return o;
}

void DestroyMembers(cTSEffectsParser *o)
{
    std::destroy_at(&o->mValues294);
    if (o->mRefs288.mFirst) std::allocator<void *>().deallocate(o->mRefs288.mFirst, static_cast<size_t>(o->mRefs288.mEnd - o->mRefs288.mFirst));
    std::destroy_at(&o->mMap280);
    std::destroy_at(&o->mSet278);
    o->mText23c.Destruct();
    std::destroy_at(&o->mList230);
    o->mText1f4.Destruct();
    std::destroy_at(&o->mMap1ec);
    std::destroy_at(&o->mMap1e4);
    std::destroy_at(&o->mMap1dc);
    std::destroy_at(&o->mMap1d4);
}

void FreeObject(cTSEffectsParser *o)
{
    DestroyMembers(o);
    ::operator delete(o);
}

// Two objects made from the same junk and members (one generator state each).
struct Pair {
    cTSEffectsParser *o[2];
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

// ---- what an object holds, as text -----------------------------------------------------------------------------------------

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

// A tree container's head node: its words as "self" or "node", the flag word; then each node's colour/nil bytes and contents.
template <class M> void DescribeHead(std::string &s, const M &m)
{
    const uint8_t *head = *reinterpret_cast<const uint8_t *const *>(&m);
    s += " head";
    for (int w = 0; w < 3; ++w) {
        const uint32_t word = *reinterpret_cast<const uint32_t *>(head + 4 * w);
        s += word == P(head) ? " self" : " node";
    }
    s += " " + Hex(head[0xc]) + "/" + Hex(head[0xd]) + " size " + std::to_string(m.size());
}

void DescribeMapU32(std::string &s, const std::map<std::string, uint32_t> &m)
{
    DescribeHead(s, m);
    for (const auto &kv : m) {
        const uint8_t *node = reinterpret_cast<const uint8_t *>(&kv) - 0x10;
        s += " [" + Hex(node[0xc]) + "/" + Hex(node[0xd]);
        DescribeText(s, kv.first);
        s += " " + Hex(kv.second) + "]";
    }
}

std::string Describe(const cTSEffectsParser *o)
{
    std::vector<uint8_t> raw(reinterpret_cast<const uint8_t *>(o), reinterpret_cast<const uint8_t *>(o) + cTSEffectsParser::kSize);
    auto blank = [&](size_t off, size_t n) { std::memset(raw.data() + off, 0, n); };
    blank(0x1ac, 20);   // the five counted pointers (described below)
    blank(0x1d4, 4);
    blank(0x1dc, 4);
    blank(0x1e4, 4);
    blank(0x1ec, 4);
    blank(0x1f8, 24);   // the strings
    blank(0x230, 12);
    blank(0x240, 24);
    blank(0x278, 4);
    blank(0x280, 4);
    blank(0x288, 12);
    blank(0x294, 12);
    std::string s;
    for (uint8_t b : raw) s += Hex(b) + " ";
    s += "\nobjects " + FakeText(o->mObject1ac) + " " + FakeText(o->mDescriptions) + " " + FakeText(o->mObject1b4) + " " +
         FakeText(o->mObject1b8) + " " + FakeText(o->mObject1bc);
    s += "\nmaps";
    DescribeMapU32(s, o->mMap1d4);
    DescribeMapU32(s, o->mMap1dc);
    DescribeMapU32(s, o->mMap1e4);
    DescribeMapU32(s, o->mMap1ec);
    s += "\ntexts";
    DescribeText(s, o->mText1f4.mString);
    DescribeText(s, o->mText23c.mString);
    s += "\nrecords";
    s += " vec " + std::to_string(o->mList230.size()) + "/" + std::to_string(o->mList230.capacity()) + ":";
    for (const EffectsRecord88 &r : o->mList230) {
        DescribeText(s, r.text);
        for (uint8_t b : r.rest) s += " " + Hex(b);
        s += ";";
    }
    s += "\nset";
    DescribeHead(s, o->mSet278);
    for (const EffectsBlob56 &b : o->mSet278) {
        const uint8_t *node = reinterpret_cast<const uint8_t *>(&b) - 0x10;
        s += " [" + Hex(node[0xc]) + "/" + Hex(node[0xd]);
        DescribeText(s, b.first);
        DescribeText(s, b.second);
        s += " " + Hex(b.middle) + " " + Hex(b.last);
        s += "]";
    }
    s += "\nmap280";
    DescribeHead(s, o->mMap280);
    for (const auto &kv : o->mMap280) {
        const uint8_t *node = reinterpret_cast<const uint8_t *>(&kv) - 0x10;
        s += " [" + Hex(node[0xc]) + "/" + Hex(node[0xd]);
        DescribeText(s, kv.first);
        DescribeText(s, kv.second);
        s += "]";
    }
    s += "\nrefs " + std::to_string(o->mRefs288.mLast - o->mRefs288.mFirst) + "/" + std::to_string(o->mRefs288.mEnd - o->mRefs288.mFirst) + ":";
    for (void **p = o->mRefs288.mFirst; p < o->mRefs288.mEnd; ++p) s += " " + ((P(*p) >> 16) == 0xdead ? Hex(P(*p)) : FakeText(*p));
    s += "\nvalues";
    DescribeVec(s, o->mValues294);
    return s;
}

// ---- stand-ins for the cBaseParser calls -------------------------------------------------------------------------------------
// The image's bodies take the face cBaseParser has (+0x1b0 / +0x1b8 of its part); ours are members on the part itself.

const cTSEffectsParser *AsObject(const cBaseParser *p) { return reinterpret_cast<const cTSEffectsParser *>(p); }

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
void __fastcall ImageBaseReset(void *face, void *)
{
    const cBaseParser *p = cBaseParser::FromParserFace(face);
    gLog += "base Reset this " + Offset(p) + "\n" + Describe(AsObject(p)) + "\n";
}
void __fastcall OurBaseReset(cBaseParser *self, void *)
{
    gLog += "base Reset this " + Offset(self) + "\n" + Describe(AsObject(self)) + "\n";
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
    gLog += "base Destruct this " + Offset(self) + " vtables " + vt(0, cTSEffectsParser::kOwnVTable, cTSEffectsParser::kVTable) + " " +
            vt(0x2a4, cTSEffectsParser::kOwnUnknownVTable, cTSEffectsParser::kUnknownVTable) + " " +
            vt(0x2ac, cTSEffectsParser::kOwnParserVTable, cTSEffectsParser::kParserVTable) + " " +
            vt(0x2b8, cTSEffectsParser::kOwnEffectsVTable, cTSEffectsParser::kEffectsVTable) + " vtordisps " + Hex(w[0x2a0 / 4]) + " " +
            Hex(w[0x2a8 / 4]) + " " + Hex(w[0x2b4 / 4]) + "\n";
}

// Face slot 2 (SetVersionRange) and 4 (RegisterCommand): logged; the objects freed.
void __fastcall FakeSetVersionRange(void *face, void *, int32_t low, int32_t high)
{
    gLog += "range on " + Offset(face) + " " + std::to_string(low) + " " + std::to_string(high) + "\n";
}
bool InImage(uint32_t p) { return p - P(GameAddress<void>(0)) < 0x1800000u; }
void __fastcall FakeRegisterCommand(void *face, void *, void *command)
{
    gLog += "command on " + Offset(face);
    if (command) {
        const uint32_t *w = static_cast<const uint32_t *>(command);
        const size_t size = _msize(command);
        gLog += " vtable " + Hex(w[0] - P(GameAddress<void>(0))) + " parser " + Hex(w[1]) + " count " + Hex(w[2]) + " block " + std::to_string(size);
        if (size == 0x28) {
            const std::string *name = reinterpret_cast<const std::string *>(static_cast<const char *>(command) + 0x10);
            gLog += " name " + *name;
        }
        if (size >= 0x40) {
            gLog += " value " + Hex(w[3]);
            if (size >= 0x48 && InImage(w[0x11])) gLog += std::string(" name ") + reinterpret_cast<const char *>(static_cast<uintptr_t>(w[0x11]));
            // Only the resource block clears the word at +0x48 (the other 0x4c-byte block leaves the memory's own bytes).
            if (size >= 0x4c && InImage(w[0x11]) && std::strcmp(reinterpret_cast<const char *>(static_cast<uintptr_t>(w[0x11])), "resource") == 0)
                gLog += " w48 " + Hex(w[0x12]);
            static_cast<cp::cBlockCommandBase *>(command)->Destruct();
        }
        ::operator delete(command);
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
    size_t col = i - start;
    auto cut = [&](std::string l) { return col > 60 ? "..." + l.substr(std::min(col - 60, l.size())) : l; };
    return "line " + std::to_string(line) + " col " + std::to_string(col) + "\n  A: " + cut(lineAt(a)) + "\n  B: " + cut(lineAt(b));
}

int32_t Vtordisp(std::mt19937 &g, int round) { return round % 4 == 3 ? 0x20 + 4 * static_cast<int32_t>(g() % 8) : 0; }

} // namespace

// 0xbcce40 (__thiscall, ret 4): the whole parser (flag 1, and flags with other bits) and the parser as a base (flag 0, the
// virtual bases' words then left as the caller had them). Compared: the result and the parser Form of the whole object with the
// containers' head pointers blanked and their head nodes compared by their words.
DIFF_TEST(EffectsParser_Construct, 0xbcce40)
{
    Allocators allocators;
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbcce40);
    std::mt19937 g(0xbcce40);
    const size_t kHeads[] = {0x1d4, 0x1dc, 0x1e4, 0x1ec, 0x278, 0x280};
    for (int round = 0; round < 40; ++round) {
        const uint32_t flag = round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() | 1u);
        alignas(16) uint8_t a[cTSEffectsParser::kSize], b[cTSEffectsParser::kSize];
        Junk(g, a, sizeof(a));
        if (!flag) {
            const uint32_t vb = P(GameAddress<void>(cTSEffectsParser::kVbTable));
            const uint32_t face2 = P(GameAddress<void>(cTSEffectsParser::kFace2VbTable));
            const uint32_t faceE = P(GameAddress<void>(cTSEffectsParser::kEffectsFaceVbTable));
            std::memcpy(a + 8, &vb, 4);
            std::memcpy(a + 0x2b0, &face2, 4);
            std::memcpy(a + 0x2bc, &faceE, 4);
        }
        std::memcpy(b, a, sizeof(a));
        void *ra = orig(a, flag);
        void *rb = cTSEffectsParserAdapters::Construct(reinterpret_cast<cTSEffectsParser *>(b), nullptr, flag);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("round %d: returned %p / %p, expected this", round, ra, rb);
        std::string heads[2];
        std::vector<uint8_t> forms[2];
        uint8_t *objects[2] = {a, b};
        for (int side = 0; side < 2; ++side) {
            std::vector<uint8_t> copy(objects[side], objects[side] + cTSEffectsParser::kSize);
            for (size_t off : kHeads) {
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
                {0x000, GameAddress<void>(cTSEffectsParser::kVTable), cTSEffectsParser::kOwnVTable},
                {0x2a4, GameAddress<void>(cTSEffectsParser::kUnknownVTable), cTSEffectsParser::kOwnUnknownVTable},
                {0x2ac, GameAddress<void>(cTSEffectsParser::kParserVTable), cTSEffectsParser::kOwnParserVTable},
                {0x2b8, GameAddress<void>(cTSEffectsParser::kEffectsVTable), cTSEffectsParser::kOwnEffectsVTable},
                {0x008, GameAddress<void>(cTSEffectsParser::kVbTable), cTSEffectsParser::kOwnVbTable},
                {0x2b0, GameAddress<void>(cTSEffectsParser::kFace2VbTable), cTSEffectsParser::kOwnFace2VbTable},
                {0x2bc, GameAddress<void>(cTSEffectsParser::kEffectsFaceVbTable), cTSEffectsParser::kOwnEffectsFaceVbTable},
            };
            for (const Word &w : words) {
                // Flag 0 leaves the virtual base table words as the caller had them (the image's on both sides).
                const bool vbWord = w.off == 0x008 || w.off == 0x2b0 || w.off == 0x2bc;
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
        const auto *oa = reinterpret_cast<const cTSEffectsParser *>(a);
        if (heads[0].find("self self") == std::string::npos || oa->mWord218 != 1 || oa->mZero214 != 0 || oa->mObject1ac != nullptr)
            ctx.Fail("round %d: the original's object does not have the layout's fields (%s)", round, heads[0].c_str());
        for (uint8_t *object : objects) {
            for (size_t off : kHeads) ::operator delete(const_cast<uint8_t *>(parserform::Ptr(parserform::Word(object, off))));
            parserform::TearDownParser(*reinterpret_cast<cBaseParser *>(object));
        }
    }
}

namespace {

// The members for the destructor tests: both sides' objects destroyed by the side's destructor (the base destructor, 0xafe1b0,
// stood in for on both), compared by the log (releases, the base call), the sizes of the blocks freed and whether the object
// itself was freed.
template <class Run> void RunDestroy(difftest::Context &ctx, uint32_t seed, Run run)
{
    Allocators allocators;
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
            cTSEffectsParser *o = NewObject(og, nullptr, nullptr, pop);
            gObj = reinterpret_cast<const uint8_t *>(o);
            gLog.clear();
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

// 0xbcd2e0 (__thiscall, plain ret): the members destroyed in the original's order. The object is not freed.
DIFF_TEST(EffectsParser_Destruct, 0xbcd2e0)
{
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0xbcd2e0);
    RunDestroy(ctx, 0xbcd2e0, [&](int side, cTSEffectsParser *o, uint32_t) -> void * {
        if (side == 0) orig(o);
        else cTSEffectsParserAdapters::Destruct(o, nullptr);
        return o;
    });
}

// Primary slot 0 (0xbcd7e0, ret 4): flags 0, 1 and others; the memory freed when bit 0 is set.
DIFF_TEST(EffectsParser_DeletingDestructor, 0xbcd7e0)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0xbcd7e0);
    RunDestroy(ctx, 0xbcd7e0, [&](int side, cTSEffectsParser *o, uint32_t flags) -> void * {
        if (side == 0) return orig(o, flags);
        return cTSEffectsParserAdapters::DeletingDestructor(o, nullptr, flags);
    });
}

namespace {

// Runs a body through its thunk (the original's) or our forwarder, on the face moved by a vtordisp (the thunk takes it off).
template <auto Adapter> struct RetOf;
template <class R, class... X, R(__fastcall *Adapter)(void *, void *, X...)> struct RetOf<Adapter> { using Type = R; };
template <auto Adapter> const void *ThunkOf() { return reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<Adapter>::Call); }

template <auto Adapter, class... A>
std::string RunFace(int side, uint32_t thunk, difftest::Context &ctx, cTSEffectsParser *o, uint32_t faceOffset, int32_t vtordisp, A... args)
{
    const void *ours = ThunkOf<Adapter>();
    using R = typename RetOf<Adapter>::Type;
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

} // namespace

// Slot 0 of the cIBaseParser face (thunk 0xbcf816 -> 0xbcf820, ret 4): the case flag (with junk in the argument's upper bytes),
// the base's result 0 or 1, vtordisps 0 and others. Logged: the base call (its `this`, flag, the object then), the version
// range, and the fourteen commands registered through the face (their words, names and blocks' sizes), then those of the
// registrars the image's own code runs (for real, on both sides).
DIFF_TEST(EffectsParser_Initialise, 0xbcf820)
{
    Allocators allocators;
    FaceVTable face(ctx);
    face.slots[2] = reinterpret_cast<void *>(&FakeSetVersionRange);
    face.slots[4] = reinterpret_cast<void *>(&FakeRegisterCommand);
    shape::StandIn base(0xb02880u, &cBaseParser::Initialise, &ImageBaseInitialise);
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::Initialise), shape::CodeOf(&OurBaseInitialise));
    std::mt19937 g(0xbcf820);
    for (int round = 0; round < 24; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        const uint32_t arg = (g() & 0xffffff00u) | (round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() & 0xff));
        gBaseResult = round % 5 == 4 ? 0 : 1;
        Pair pair(g(), &face);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cTSEffectsParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            const std::string r = RunFace<&cTSEffectsParserAdapters::Initialise>(side, 0xbcf816, ctx, o, 0x2ac, vtordisp, arg);
            got[side] = gLog + r + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d arg %08x base %u): %s", round, vtordisp, arg, gBaseResult, Diff(got[0], got[1]).c_str());
        if (round == 0 && got[0].find("command on") == std::string::npos) ctx.Fail("the original registered no command:\n%s", got[0].c_str());
    }
}

// Slot 9 of the cIBaseParser face (thunk 0xbcdafa -> 0xbcdb10): the base's Reset (0xaff120, stood in for), the set emptied, the
// counted list told to let go (slot 4, 1), released (slot 2) and emptied, the pointer vector and the string emptied, the counted
// pointer at +0x1b8 nulled then released, the word at +0x276 zeroed and the map cleared.
DIFF_TEST(EffectsParser_Reset, 0xbcdb10)
{
    Allocators allocators;
    shape::StandIn base(0xaff120u, &cBaseParser::Reset, &ImageBaseReset);
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::Reset), shape::CodeOf(&OurBaseReset));
    std::mt19937 g(0xbcdb10);
    for (int round = 0; round < 120; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        Pair pair(g());
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cTSEffectsParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            difftest::FreeRecorder frees;
            const void *storage[3] = {o->mValues294.data(), o->mText1f4.mString.data(), o->mRefs288.mFirst};
            const std::string r = RunFace<&cTSEffectsParserAdapters::Reset>(side, 0xbcdafa, ctx, o, 0x2ac, vtordisp);
            frees.Stop();
            const void *after[3] = {o->mValues294.data(), o->mText1f4.mString.data(), o->mRefs288.mFirst};
            got[side] = gLog + r + "\n" + FreedSizes(frees) + "\n" + Describe(o);
            for (int i = 0; i < 3; ++i) got[side] += storage[i] == after[i] ? " same" : " moved";
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d): %s", round, vtordisp, Diff(got[0], got[1]).c_str());
    }
}

// Slot 0 of the cIGZUnknown face (thunk 0xbd199c, body 0xbd19b0 inside it, ret 8): the interface id of the effects face (the face
// handed out and added through the unknown face's slot 1), others (cBaseParser's QueryInterface, stood in for, called with the
// argument), and a null out pointer.
DIFF_TEST(EffectsParser_QueryInterface, 0xbd199c)
{
    Allocators allocators;
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
    std::mt19937 g(0xbd199c);
    static const uint32_t kIds[] = {cTSEffectsParser::kEffectsIID, cTSEffectsParser::kEffectsIID, 1u, 0x95035f4u, 0u, 0x6956babdu, 0x6956babbu};
    for (int round = 0; round < 100; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        const uint32_t iid = round < 14 ? kIds[round % 7] : round % 2 ? cTSEffectsParser::kEffectsIID : g();
        const bool nullOut = round % 5 == 4;
        gBaseResult = g() & 1;
        Pair pair(g(), nullptr, &unknown);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cTSEffectsParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            void *out = reinterpret_cast<void *>(0x12345678);
            const std::string r = RunFace<&cTSEffectsParserAdapters::QueryInterface>(side, 0xbd199c, ctx, o, 0x2a4, vtordisp, iid,
                                          nullOut ? nullptr : &out);
            got[side] = gLog + r + " out " + (nullOut ? std::string("-") : out == reinterpret_cast<void *>(0x12345678) ? "untouched" : Offset(out)) + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1])
            ctx.Fail("round %d (vtordisp %d iid %x null %d base %u): %s", round, vtordisp, iid, nullOut, gBaseResult, Diff(got[0], got[1]).c_str());
    }
}

namespace {

// The setter tests: the argument none, the pointer already held, or a new fake.
template <auto Adapter, class... Extra>
void RunSetter(difftest::Context &ctx, uint32_t thunk, uint32_t seed, int which)
{
    Allocators allocators;
    std::mt19937 g(seed);
    for (int round = 0; round < 120; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        Pair pair(g());
        const int pick = round % 3;
        gPool.push_back({gObjVt, 0x100u + static_cast<uint32_t>(round)});
        void *fresh = &gPool.back();
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cTSEffectsParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            void *held[] = {o->mObject1ac, o->mDescriptions, o->mObject1b4, o->mObject1bc};
            void *arg = pick == 0 ? nullptr : pick == 1 ? held[which] : fresh;
            const std::string r = RunFace<Adapter>(side, thunk, ctx, o, 0x2b8, vtordisp, arg);
            got[side] = gLog + r + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d pick %d): %s", round, vtordisp, pick, Diff(got[0], got[1]).c_str());
    }
}

} // namespace

// Slots 0..3 of the cITSEffectsParser face (thunks 0xbd1d9d, 0xbd1d3c, 0xbd1dfa, 0xbd1e45 -> 0xbd1db0, 0xbd1d50, 0xbd1e10, 0xbd1e50):
// the counted pointer replaced (the new one counted when different and not null, the old released); slot 0 also clears the byte at
// +0x1d0.
DIFF_TEST(EffectsParser_SetPointer1ac, 0xbd1db0) { RunSetter<&cTSEffectsParserAdapters::SetPointer1ac>(ctx, 0xbd1d9d, 0xbd1db0, 0); }
DIFF_TEST(EffectsParser_SetPointer1b0, 0xbd1d50) { RunSetter<&cTSEffectsParserAdapters::SetPointer1b0>(ctx, 0xbd1d3c, 0xbd1d50, 1); }
DIFF_TEST(EffectsParser_SetPointer1b4, 0xbd1e10) { RunSetter<&cTSEffectsParserAdapters::SetPointer1b4>(ctx, 0xbd1dfa, 0xbd1e10, 2); }
DIFF_TEST(EffectsParser_SetPointer1bc, 0xbd1e50) { RunSetter<&cTSEffectsParserAdapters::SetPointer1bc>(ctx, 0xbd1e45, 0xbd1e50, 3); }

// Slot 4 (0xbd1d85, ret 4: thunk and body in one): the byte at +0x1d0 stored.
DIFF_TEST(EffectsParser_SetFlag1d0, 0xbd1d85)
{
    Allocators allocators;
    std::mt19937 g(0xbd1d85);
    for (int round = 0; round < 60; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        Pair pair(g());
        const uint32_t arg = (g() & 0xffffff00u) | (round % 4 == 0 ? 0u : round % 4 == 1 ? 1u : (g() & 0xff));
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cTSEffectsParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            const std::string r = RunFace<&cTSEffectsParserAdapters::SetFlag1d0>(side, 0xbd1d85, ctx, o, 0x2b8, vtordisp, static_cast<uint8_t>(arg));
            got[side] = gLog + r + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d arg %02x): %s", round, vtordisp, arg & 0xff, Diff(got[0], got[1]).c_str());
    }
}

// Slot 5 (thunk 0xbcdc67 -> 0xbcdc70, ret 8): the text registered for the lower-cased "name:number" key. Names in mixed case,
// numbers negative, zero and positive; keys present and not. Compared: the result as the text (the pointer is a member's), the
// blocks freed and the object.
DIFF_TEST(EffectsParser_Lookup, 0xbcdc70)
{
    Allocators allocators;
    std::mt19937 g(0xbcdc70);
    static const char *const kNames[] = {"Fire", "FIRE", "water", "zeta", "A", "", "fire", "a", "ThisNameIsLongerThanSixteenCharacters"};
    static const int32_t kNumbers[] = {0, 1, 2, 5, -1, 100, INT32_MIN};
    for (int round = 0; round < 200; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        Pair pair(g());
        const char *name = kNames[g() % 9];
        const int32_t number = kNumbers[g() % 7];
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cTSEffectsParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            difftest::FreeRecorder frees;
            const char *text = nullptr;
            {
                // The result pointer is read through the same call; shown as the text it points at.
                using Sig = const char *(__fastcall *)(void *, void *, const char *, int32_t);
                uint8_t *faceAt = reinterpret_cast<uint8_t *>(o) + 0x2b8 + vtordisp;
                alignas(4) uint8_t saved[8];
                std::memcpy(saved, faceAt - 4, 8);
                std::memcpy(faceAt - 4, &vtordisp, 4);
                const Sig fn = side == 0 ? ctx.Original<Sig>(0xbcdc67) : reinterpret_cast<Sig>(ThunkOf<&cTSEffectsParserAdapters::Lookup>());
                text = fn(faceAt, nullptr, name, number);
                std::memcpy(faceAt - 4, saved, 8);
            }
            frees.Stop();
            std::string shown = text ? "text:" + std::string(text) : "null";
            bool member = false;
            for (const auto &kv : o->mMap280) member |= kv.second.c_str() == text;
            got[side] = shown + (member ? " (a member)" : " (not a member)") + "\n" + FreedSizes(frees) + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d, \"%s\":%d): %s", round, vtordisp, name, number, Diff(got[0], got[1]).c_str());
        if (round == 0) {
            // The keys exist in the maps often enough for the lookups to find something in the first rounds.
        }
    }
}

// Slot 6 (thunk 0xbd1f03 -> 0xbd1f10): each counted object's slot 3 called with the value at the same index.
DIFF_TEST(EffectsParser_Apply, 0xbd1f10)
{
    Allocators allocators;
    std::mt19937 g(0xbd1f10);
    bool sawCall = false;
    for (int round = 0; round < 120; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        Pair pair(g());
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cTSEffectsParser *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            const std::string r = RunFace<&cTSEffectsParserAdapters::Apply>(side, 0xbd1f03, ctx, o, 0x2b8, vtordisp);
            got[side] = gLog + r + "\n" + Describe(o);
        }
        ++ctx.cases;
        sawCall |= got[0].find("apply ") != std::string::npos;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d): %s", round, vtordisp, Diff(got[0], got[1]).c_str());
    }
    if (!sawCall) ctx.Fail("no round applied anything");
}

namespace {

// ---- the faces' shifted thunks ----------------------------------------------------------------------------------------------

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

// A thunk `sub ecx, [ecx-4]; sub ecx, 0xf4; jmp body` whose body is one of cBaseParser's (its registered hook the adapter): the
// image's thunk and our hook of it called on a face with a vtordisp, the body stood in for on both sides.
template <auto Adapter> struct Check;
template <class R, class... Args, R(__fastcall *Adapter)(void *, void *, Args...)> struct Check<Adapter> {
    static void Run(difftest::Context &ctx, uint32_t thunk, const char *name, std::mt19937 &g)
    {
        using Fn = R(__fastcall *)(void *, void *, Args...);
        const uint8_t *code = ctx.image.At<uint8_t>(thunk);
        static const uint8_t kPrefix[] = {0x2b, 0x49, 0xfc, 0x81, 0xe9, 0xf4, 0x00, 0x00, 0x00, 0xe9};
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
            if (got[0] != got[1]) ctx.Fail("%s %x vtordisp %d: %s", name, thunk, d, Diff(got[0], got[1]).c_str());
        }
    }
};

// A `sub ecx, [ecx-4]; jmp body` thunk: its body, and the thunk hooked to the forwarder `ours`.
void CheckOwnThunk(difftest::Context &ctx, uint32_t thunk, uint32_t body, const void *ours, const char *name, bool bodyHooked = true)
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
    if (bodyHooked && !HookAt(body)) ctx.Fail("%s %x: the body %x is not hooked", name, thunk, body);
}

} // namespace

// Every shifted thunk of the faces (+0x2a4: slots 1, 2; +0x2ac: all but 0, 9, 10 and the folded 11, 45; 33 shares 32's), each with
// its cBaseParser body's adapter; the own thunks (the cIGZUnknown face's slot 0, the cIBaseParser face's 0 and 9, the cITSEffectsParser
// face's 0..6: their bodies and forwarders); the cIBaseParser face's slot 10 (thunk 0xbd054b -> 0xbd0560, `add ecx, -0xf4; jmp
// 0xb03e50`) and slot 11 (0xbcdc33, a no-op); slot 45 (a shifted thunk into a folded body), called on random buffers.
DIFF_TEST(EffectsParser_FaceThunks, 0xbcdaec)
{
    std::mt19937 g(0xbcdaec);
#define X(rva, adapter) Check<&cBaseParserAdapters::adapter>::Run(ctx, rva, #adapter, g);
    X(0xbcdaec, AddRef)
    X(0xbcdc17, CommandAt)
    X(0xbcdc25, HasCommand)
    X(0xbcdc4b, GetNamespacePath)
    X(0xbcdc59, HasDefinition)
    X(0xbcddaf, PopMetaBlock)
    X(0xbcf792, GetFunctions)
    X(0xbd0048, IsCaseSensitive)
    X(0xbd0056, IsInitialised)
    X(0xbd0064, GetText)
    X(0xbd0072, CommandCount)
    X(0xbd07f9, EvalBool)
    X(0xbd0807, RunLine)
    X(0xbd1691, RunFile)
    X(0xbd169f, GetLine)
    X(0xbd16ad, GetFileName)
    X(0xbd16bb, RunText)
    X(0xbd16c9, RunFiles)
    X(0xbd16d7, EvalFloat)
    X(0xbd16e5, EvalInt)
    X(0xbd16f3, EvalColorRGBA)
    X(0xbd1701, EvalColorARGB)
    X(0xbd170f, EvalColorRGB)
    X(0xbd171d, EvalVector2)
    X(0xbd172b, EvalVector3)
    X(0xbd1739, EvalVector4)
    X(0xbd1747, PopNamespace)
    X(0xbd1980, PurgeNamespace)
    X(0xbd198e, PushNamespace)
    X(0xbd1a10, RegisterCommand)
    X(0xbd1a1e, RegisterMetaCommand)
    X(0xbd1a2c, Release)
    X(0xbd1a3a, RestartViaReset)
    X(0xbd1dec, SetVersionRange)
    X(0xbd1e85, SetSource)
    X(0xbd1e93, SetContextState)
    X(0xbd1ea1, SetOutput)
    X(0xbd1eaf, SetHandler)
    X(0xbd1ebd, RunExpandedText)
    X(0xbd1ecb, Shutdown)
    X(0xbd1ed9, GetSource)
    X(0xbd1ee7, PushOpenBlock)
    X(0xbd1ef5, PushMetaBlock)
    X(0xbd1f46, ExpandVariables)
    X(0xbd1f54, UnregisterCommandObject)
    X(0xbd1f62, UnregisterCommand)
    X(0xbd1f70, FindVariable)
#undef X
    CheckOwnThunk(ctx, 0xbd199c, 0xbd19b0, ThunkOf<&cTSEffectsParserAdapters::QueryInterface>(), "QueryInterface", false);
    CheckOwnThunk(ctx, 0xbcf816, 0xbcf820, ThunkOf<&cTSEffectsParserAdapters::Initialise>(), "Initialise");
    CheckOwnThunk(ctx, 0xbcdafa, 0xbcdb10, ThunkOf<&cTSEffectsParserAdapters::Reset>(), "Reset");
    CheckOwnThunk(ctx, 0xbd1d9d, 0xbd1db0, ThunkOf<&cTSEffectsParserAdapters::SetPointer1ac>(), "SetPointer1ac");
    CheckOwnThunk(ctx, 0xbd1d3c, 0xbd1d50, ThunkOf<&cTSEffectsParserAdapters::SetPointer1b0>(), "SetPointer1b0");
    CheckOwnThunk(ctx, 0xbd1dfa, 0xbd1e10, ThunkOf<&cTSEffectsParserAdapters::SetPointer1b4>(), "SetPointer1b4");
    CheckOwnThunk(ctx, 0xbd1e45, 0xbd1e50, ThunkOf<&cTSEffectsParserAdapters::SetPointer1bc>(), "SetPointer1bc");
    CheckOwnThunk(ctx, 0xbcdc67, 0xbcdc70, ThunkOf<&cTSEffectsParserAdapters::Lookup>(), "Lookup");
    CheckOwnThunk(ctx, 0xbd1f03, 0xbd1f10, ThunkOf<&cTSEffectsParserAdapters::Apply>(), "Apply");
    {
        const HookSpec *h = HookAt(0xbd1d85);
        if (!h || h->replacement != ThunkOf<&cTSEffectsParserAdapters::SetFlag1d0>()) ctx.Fail("SetFlag1d0 0xbd1d85: not hooked to its forwarder");
        ++ctx.cases;
    }

    // Slot 10: the thunk (`sub ecx, [ecx-4]; jmp 0xbd0560`) reaches cBaseParser's ResetBlocks (0xb03e50) with the face less 0xf4.
    {
        const HookSpec *thunkHook = HookAt(0xbd054b);
        const uint8_t *code = ctx.image.At<uint8_t>(0xbd054b);
        static const uint8_t kThunk[] = {0x2b, 0x49, 0xfc, 0xe9};
        int32_t rel;
        std::memcpy(&rel, code + 4, 4);
        const uint32_t middle = 0xbd054b + 8 + rel;
        const uint8_t *tail = ctx.image.At<uint8_t>(middle);
        int32_t rel2;
        std::memcpy(&rel2, tail + 7, 4);
        static const uint8_t kShift[] = {0x81, 0xc1, 0x0c, 0xff, 0xff, 0xff, 0xe9};
        if (!thunkHook || std::memcmp(code, kThunk, 4) != 0 || std::memcmp(tail, kShift, 7) != 0 || middle + 11 + rel2 != 0xb03e50u) {
            ctx.Fail("slot 10: the thunk 0xbd054b does not have the expected shape (or is not hooked)");
        } else {
            using Fn = void(__fastcall *)(void *, void *);
            shape::StandIn stand(0xb03e50u, &cBaseParserAdapters::ResetBlocks, &Recorder<&cBaseParserAdapters::ResetBlocks>::Call);
            for (int i = 0; i < 24; ++i) {
                const int32_t d = ThunkVtordisp(g, i);
                uint8_t *face = gBuffer + 0x200;
                std::memcpy(face - 4, &d, 4);
                std::string got[2];
                for (int side = 0; side < 2; ++side) {
                    gSeen = "not reached";
                    (side == 0 ? ctx.Original<Fn>(0xbd054b) : reinterpret_cast<Fn>(thunkHook->replacement))(face, nullptr);
                    got[side] = gSeen;
                }
                ++ctx.cases;
                if (got[0] != got[1]) ctx.Fail("slot 10 vtordisp %d: %s", d, Diff(got[0], got[1]).c_str());
            }
        }
    }

    // Slots 11 and 45: no stand-in (the folded bodies are run), the buffer's words random.
    const HookSpec *noOp = HookAt(0xbcdc33), *version = HookAt(0xbd1f7e);
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
        const int32_t a = ctx.Original<VersionFn>(0xbd1f7e)(face, nullptr);
        const int32_t b = reinterpret_cast<VersionFn>(version->replacement)(face, nullptr);
        using NoOpFn = void(__fastcall *)(void *, void *);
        std::vector<uint8_t> before(gBuffer, gBuffer + sizeof(gBuffer));
        ctx.Original<NoOpFn>(0xbcdc33)(face, nullptr);
        reinterpret_cast<NoOpFn>(noOp->replacement)(face, nullptr);
        ++ctx.cases;
        if (a != b) ctx.Fail("slot 45 vtordisp %d: %d / %d", d, a, b);
        if (std::memcmp(before.data(), gBuffer, sizeof(gBuffer)) != 0) ctx.Fail("slot 11 wrote to the buffer");
    }
}

// A whole parser: the original's built (0xbcce40) and initialised (thunk 0xbcf816) by the image, ours by our code, both on the real
// cBaseParser (the real face vtable, the commands registered for real). Compared: the command count (face slot 40), HasCommand
// (slot 49) for the names the class registers and one it does not, and the effects face's lookup of an empty parser.
DIFF_TEST(EffectsParser_Whole, 0xbcf820)
{
    Allocators allocators;
    using CountFn = uint32_t(__thiscall *)(void *);
    using HasFn = uint32_t(__thiscall *)(void *, const char *);
    std::vector<std::string> names;
    for (int i = 0; i < 14; ++i) names.push_back(*GameAddress<const char *const>(cTSEffectsParser::kCommandNames + 4 * i));
    names.push_back("NoSuchCommand");
    for (int round = 0; round < 6; ++round) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            auto *o = static_cast<cTSEffectsParser *>(::operator new(cTSEffectsParser::kSize));
            void *face;
            if (side == 0) {
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbcce40)(o, 1);
                face = reinterpret_cast<uint8_t *>(o) + 0x2ac;
                ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xbcf816)(face, round & 1);
            } else {
                o->Construct(1);
                face = o->ParserFace();
                o->Initialise(static_cast<uint8_t>(round & 1));
            }
            auto **vtable = *reinterpret_cast<void ***>(face);
            got[side] = "commands " + std::to_string(reinterpret_cast<CountFn>(vtable[40])(face));
            for (const std::string &name : names) got[side] += " " + name + "=" + std::to_string(reinterpret_cast<HasFn>(vtable[49])(face, name.c_str()) & 0xff);
            if (side == 0) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbcd7e0)(o, 1);
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
DIFF_TEST(EffectsParser_VTables, 0xe8db8c)
{
    struct Table { const char *name; uint32_t image; const void *const *ours; size_t count; };
    const Table tables[] = {
        {"primary", cTSEffectsParser::kVTable, cTSEffectsParser::kOwnVTable, std::size(cTSEffectsParser::kOwnVTable)},
        {"cIGZUnknown", cTSEffectsParser::kUnknownVTable, cTSEffectsParser::kOwnUnknownVTable, std::size(cTSEffectsParser::kOwnUnknownVTable)},
        {"cIBaseParser", cTSEffectsParser::kParserVTable, cTSEffectsParser::kOwnParserVTable, std::size(cTSEffectsParser::kOwnParserVTable)},
        {"cITSEffectsParser", cTSEffectsParser::kEffectsVTable, cTSEffectsParser::kOwnEffectsVTable, std::size(cTSEffectsParser::kOwnEffectsVTable)},
    };
    const uint32_t base = P(GameAddress<void>(0));
    for (const Table &t : tables) {
        const uint32_t *image = GameAddress<uint32_t>(t.image);
        for (size_t i = 0; i < t.count; ++i) {
            ++ctx.cases;
            // Primary slots 2 and 3 are the folded setter and getter (0x960960, 0x5b1920 shared by unrelated classes, not hookable):
            // ours are this class's own adapters, checked by what they do below.
            if (t.ours == cTSEffectsParser::kOwnVTable && (i == 2 || i == 3)) continue;
            const HookSpec *h = HookAt(image[i] - base);
            if (!h) ctx.Fail("%s slot %zu: the image's entry %x is not hooked", t.name, i, image[i] - base);
            else if (h->replacement != t.ours[i]) ctx.Fail("%s slot %zu: ours is %p, the hook of %x installs %p", t.name, i, t.ours[i], image[i] - base, h->replacement);
            if (!t.ours[i]) ctx.Fail("%s slot %zu: empty", t.name, i);
        }
    }
    ++ctx.cases;
    if (std::memcmp(GameAddress<int32_t>(cTSEffectsParser::kVbTable), cTSEffectsParser::kOwnVbTable, sizeof(cTSEffectsParser::kOwnVbTable)) != 0)
        ctx.Fail("the cBaseParser part's virtual base table differs from the image's");
    if (std::memcmp(GameAddress<int32_t>(cTSEffectsParser::kFace2VbTable), cTSEffectsParser::kOwnFace2VbTable, sizeof(cTSEffectsParser::kOwnFace2VbTable)) != 0)
        ctx.Fail("the cIBaseParser face's virtual base table differs from the image's");
    if (std::memcmp(GameAddress<int32_t>(cTSEffectsParser::kEffectsFaceVbTable), cTSEffectsParser::kOwnEffectsFaceVbTable, sizeof(cTSEffectsParser::kOwnEffectsFaceVbTable)) != 0)
        ctx.Fail("the cITSEffectsParser face's virtual base table differs from the image's");

    // Dispatch on whole objects (the original's built by the image, ours by our constructor), both initialised.
    Allocators allocators;
    using Fn0 = uint32_t(__thiscall *)(void *);
    using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
    using Fn2 = uint32_t(__thiscall *)(void *, const char *, int32_t);
    for (int round = 0; round < 4; ++round) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            auto *o = static_cast<cTSEffectsParser *>(::operator new(cTSEffectsParser::kSize));
            if (side == 0) {
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbcce40)(o, 1);
                ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xbcf816)(reinterpret_cast<uint8_t *>(o) + 0x2ac, round & 1);
            } else {
                o->Construct(1);
                o->Initialise(static_cast<uint8_t>(round & 1));
            }
            uint8_t *unknown = reinterpret_cast<uint8_t *>(o) + 0x2a4, *parser = reinterpret_cast<uint8_t *>(o) + 0x2ac, *effects = reinterpret_cast<uint8_t *>(o) + 0x2b8;
            auto **uv = *reinterpret_cast<void ***>(unknown);
            auto **pv = *reinterpret_cast<void ***>(parser);
            auto **ev = *reinterpret_cast<void ***>(effects);
            std::string &s = got[side];
            auto word = [&](uint32_t v) { s += " " + Hex(v); };
            word(reinterpret_cast<Fn0>(pv[40])(parser));            // CommandCount
            word(reinterpret_cast<Fn0>(pv[42])(parser) & 0xff);     // IsInitialised
            word(reinterpret_cast<Fn0>(pv[43])(parser) & 0xff);     // IsCaseSensitive
            word(reinterpret_cast<Fn0>(pv[45])(parser));            // GetVersion
            word(reinterpret_cast<Fn1>(pv[49])(parser, P("effect")) & 0xff); // HasCommand
            word(reinterpret_cast<Fn0>(uv[1])(unknown));            // AddRef
            word(reinterpret_cast<Fn0>(uv[1])(unknown));
            word(reinterpret_cast<Fn0>(uv[2])(unknown));            // Release
            word(reinterpret_cast<Fn2>(ev[5])(effects, "x", 1));  // Lookup on an empty parser: null
            reinterpret_cast<Fn1>(ev[4])(effects, 7);                // the flag byte
            word(o->mFlag1d0);
            reinterpret_cast<Fn1>(ev[0])(effects, 0);                // SetPointer1ac(null): clears the flag byte
            word(o->mFlag1d0);
            reinterpret_cast<Fn0>(ev[6])(effects);                   // Apply on an empty list
            // The primary vtable's folded setter and getter (slots 2, 3).
            auto **vt = *reinterpret_cast<void ***>(o);
            for (uint32_t value : {0x1234u + round, 0u, 0xffffffffu}) {
                reinterpret_cast<Fn1>(vt[2])(o, value);
                word(reinterpret_cast<Fn0>(vt[3])(o));
                word(o->Parser()->mField158);
            }
            if (side == 0) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbcd7e0)(o, 1);
            else reinterpret_cast<Fn1>(vt[0])(o, 1);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d: %s / %s", round, got[0].c_str(), got[1].c_str());
    }
}
