// Differential tests for the lighting script's parsers (lane 261110y, scenegraph LG3g and LG7f; src/sg/cLightingParserState.h,
// src/hook/tables/cLightingParserState.cpp): cTSLightingParserState (its destructors, FindState, the state assignment and the room
// settings copy it uses, its faces' own bodies, the timeOfDay command and the help texts of the state's commands) and
// nTSSG::cLightingParserState (its destructor and its parser face's own bodies), then every face thunk of both and both classes'
// vtables.
//
// The bodies run on objects built twice from one seed (junk, the game's virtual base tables, fake face vtables and random members);
// the calls they make to cBaseParser, to each other and to the light settings are stood in for by logging fakes (the image's callee
// on the original's side, our member on ours, each told apart only by the `this` it is given). Each run is measured: the calls in
// order with the heap's live block count at each, what was thrown (std::bad_alloc, the fakes' int, anything else), the allocations
// and the live block count after, and the sizes of the blocks freed in order. Each run is repeated with each of its allocations
// failing in turn (std::bad_alloc from the n-th, the image's operator new and ours both redirected).
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"

#include "gz/CommandParser.h"
#include "gz/LightingCommands.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "hook/hooks.h"
#include "sg/cLightSettings.h"
#include "sg/cLightingParserState.h"

#include <malloc.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
using Base = cTSLightingParserState;
using BaseAdapters = cTSLightingParserStateAdapters;
using Derived = nTSSG::cLightingParserState;
using DerivedAdapters = nTSSG::cLightingParserStateAdapters;

namespace {

// ---- allocation, logging, measuring --------------------------------------------------------------------------------------------

difftest::AllocationCounter *gCounter = nullptr;
int gNews = 0, gFailAt = -1, gQuiet = 0, gLastNews = 0;

// The image's operator new and ours: a pattern in new memory (words nobody writes are the same on both sides); the gFailAt-th
// allocation (counted from the run's start) throws std::bad_alloc. Allocations made while a fake logs (gQuiet) are not counted.
void *__cdecl FailingNew(size_t n)
{
    if (!gQuiet && gNews++ == gFailAt) throw std::bad_alloc();
    void *p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    std::memset(p, 0xa5, n);
    return p;
}
void __cdecl GameDelete(void *p, size_t) { std::free(p); }

using NewFn = void *(__cdecl *)(size_t);
struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&FailingNew)};
    shape::CodePatch ourNew{reinterpret_cast<void *>(static_cast<NewFn>(&::operator new)), shape::CodeOf(&FailingNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

struct Quiet { // a fake's own bookkeeping: not counted, never failed
    difftest::NotCounted notCounted;
    Quiet() { ++gQuiet; }
    ~Quiet() { --gQuiet; }
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

uint32_t Fnv(const void *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(p)[i]) * 16777619u;
    return h;
}

struct Log : std::string { // reserved once: appending while a run is measured never allocates
    Log() { reserve(1 << 22); }
};
Log gLog;
const uint8_t *gObj = nullptr; // the object of the side running: pointers are logged as offsets in it

std::string Offset(const void *p) { return p ? Hex(P(p) - P(gObj)) : std::string("null"); }

uint32_t ImageBase() { return P(GameAddress<void>(0)); }
bool InImage(uint32_t p) { return p - ImageBase() < 0x1800000u; }
std::string WordText(uint32_t w) { return InImage(w) ? "i" + Hex(w - ImageBase()) : Hex(w); }

void L(const char *s) { gLog += s; }
void L(uint32_t v)
{
    gLog += ' ';
    gLog += Hex(v); // at most eight characters: in the string's own buffer
}
void LOffset(const void *p) { L(P(p) - P(gObj)); }
void LBlocks()
{
    L(" blocks");
    L(gCounter ? static_cast<uint32_t>(gCounter->Blocks()) : 0xffffffffu);
    L("\n");
}

// The fakes' throws: gThrow names the fake that throws an int this run (0: none).
enum : int { kThrowNone, kThrowBase, kThrowSettings, kThrowSettingsCtor, kThrowEval };
int gThrow = kThrowNone;
void MaybeThrow(int who)
{
    if (gThrow == who) {
        L(" throws\n");
        throw who;
    }
}

// Runs f with the allocation numbered failAt failing. Returns the log, the outcome, the allocations, the live blocks after and the
// sizes of the blocks freed in order.
template <class F> std::string Measured(int failAt, F &&f)
{
    gLog.clear();
    gNews = 0;
    gFailAt = failAt;
    int thrown = 0, value = 0;
    long long blocks = 0;
    size_t sizes[96];
    size_t count = 0, recorded = 0;
    bool overflow = false;
    {
        difftest::AllocationCounter counter;
        gCounter = &counter;
        difftest::FreeRecorder frees;
        try {
            f();
        } catch (const std::bad_alloc &) {
            thrown = 1;
        } catch (int v) {
            thrown = 2;
            value = v;
        } catch (...) {
            thrown = 3;
        }
        frees.Stop();
        gCounter = nullptr;
        gFailAt = -1;
        blocks = counter.Blocks();
        count = frees.Count();
        recorded = std::min<size_t>(count, 96);
        for (size_t i = 0; i < recorded; ++i) sizes[i] = frees.Size(i);
        overflow = frees.Overflowed();
    }
    gLastNews = gNews;
    static const char *const kOutcome[] = {"returned", "threw bad_alloc", "threw int", "threw something else"};
    std::string s = gLog + kOutcome[thrown] + (thrown == 2 ? " " + std::to_string(value) : std::string()) + " news " +
                    std::to_string(gNews) + " blocks " + std::to_string(blocks) + "\nfrees " + std::to_string(count) +
                    (overflow ? " overflow" : "") + ":";
    for (size_t i = 0; i < recorded; ++i) s += " " + std::to_string(sizes[i]);
    return s + "\n";
}

// The first line where two multi-line texts differ, and that line of each.
std::string Diff(const std::string &a, const std::string &b)
{
    size_t i = 0, line = 0, start = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) {
        if (a[i] == '\n') {
            ++line;
            start = i + 1;
        }
        ++i;
    }
    auto lineAt = [&](const std::string &t) {
        size_t e = t.find('\n', start);
        std::string l = t.substr(start, e == std::string::npos ? std::string::npos : e - start);
        return l.size() > 400 ? l.substr(0, 400) + "..." : l;
    };
    const size_t col = i - start;
    auto cut = [&](std::string l) { return col > 60 ? "..." + l.substr(std::min(col - 60, l.size())) : l; };
    return "line " + std::to_string(line) + " col " + std::to_string(col) + "\n  A: " + cut(lineAt(a)) + "\n  B: " + cut(lineAt(b));
}

// The run once(side, -1), then with each of the original's allocations failing in turn (at most `cap`).
template <class Once> void EachFailure(difftest::Context &ctx, const std::string &label, Once once, int cap = 8)
{
    std::string a = once(0, -1);
    const int n = gLastNews;
    std::string b = once(1, -1);
    ++ctx.cases;
    if (a != b) ctx.Fail("%s: %s", label.c_str(), Diff(a, b).c_str());
    for (int f = 0; f < n && f < cap; ++f) {
        a = once(0, f);
        b = once(1, f);
        ++ctx.cases;
        if (a != b) ctx.Fail("%s, allocation %d failing: %s", label.c_str(), f, Diff(a, b).c_str());
        if (a.find("threw bad_alloc") == std::string::npos) ctx.Fail("%s: allocation %d failing did not throw", label.c_str(), f);
    }
}

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

// ---- texts and members ------------------------------------------------------------------------------------------------------

const std::string &Long5000()
{
    static const std::string s = [] {
        std::string t(5000, 'x');
        for (size_t i = 0; i < t.size(); ++i) t[i] = static_cast<char>('a' + i % 23);
        return t;
    }();
    return s;
}
// Names: empty, short, the same name in two cases, one past the inline buffer, 5000 characters (a block over 4 KB).
const char *PoolText(uint32_t i)
{
    static const char *const kPool[] = {"", "day", "night", "Day", "dawn", "a state name longer than the inline buffer"};
    i %= 7;
    return i < 6 ? kPool[i] : Long5000().c_str();
}

void FillText(std::string &s, std::mt19937 &g)
{
    if (g() % 2 == 0) {
        s = PoolText(g());
        return;
    }
    static const size_t kLens[] = {0, 3, 15, 16, 33, 100};
    const size_t len = kLens[g() % 6];
    const size_t cap = g() % 5 == 0 ? 300 : len;
    s.assign(cap, 'q');
    s.resize(len);
    for (size_t i = 0; i < len; ++i) s[i] = static_cast<char>('a' + g() % 26);
}

void DescribeText(std::string &s, const std::string &t)
{
    s += " text " + std::to_string(t.size()) + "/" + std::to_string(t.capacity()) + " " + Hex(Fnv(t.data(), t.size() + 1));
}
void DescribeRZ(std::string &s, const cRZString &r)
{
    s += " rz " + WordText(P(r.mVTable)) + " refs " + Hex(r.mRefCount);
    DescribeText(s, r.mString);
}
void DescribeState(std::string &s, const LightingState &st)
{
    DescribeRZ(s, st.mName);
    s += " values " + Hex(Fnv(&st.mValues, sizeof(st.mValues)));
}
template <class V> void DescribeVector(std::string &s, const char *name, const V &v)
{
    s += std::string("\n") + name + " " + std::to_string(v.size()) + "/" + std::to_string(v.capacity()) + ":";
}
void DescribeEntry(std::string &s, const LightingSettingsEntry &e)
{
    s += " [config " + Hex(Fnv(&e.mBlock.mConfig, sizeof(e.mBlock.mConfig)));
    DescribeRZ(s, e.mBlock.mText);
    s += "]";
}

// A state built in raw memory: the defaults (ours), a random name and refcount, random values.
void BuildState(LightingState *st, std::mt19937 &g)
{
    st->Construct();
    FillText(st->mName.mString, g);
    st->mName.mRefCount = g() % 3;
    Junk(g, &st->mValues, sizeof(st->mValues));
}

void FillBaseMembers(Base *o, std::mt19937 &g)
{
    ::new (&o->mStates) std::vector<LightingState>();
    const size_t states = g() % 5;
    o->mStates.reserve(states + g() % 3);
    for (size_t i = 0; i < states; ++i) {
        alignas(4) uint8_t raw[sizeof(LightingState)];
        auto *st = reinterpret_cast<LightingState *>(raw);
        BuildState(st, g);
        if (g() % 2) st->mName.mString = PoolText(g());
        o->mStates.push_back(*st);
        st->~LightingState();
    }
    BuildState(&o->mCurrent, g);
    ::new (&o->mRoomSettings) std::vector<LightingSettingsEntry>();
    const size_t rooms = g() % 4;
    o->mRoomSettings.reserve(rooms + g() % 2);
    for (size_t i = 0; i < rooms; ++i) {
        LightingSettingsEntry &e = o->mRoomSettings.emplace_back();
        Junk(g, &e.mBlock.mConfig, sizeof(e.mBlock.mConfig));
        FillText(e.mBlock.mText.mString, g);
    }
    o->m3D8.Construct();
    if (g() % 3 == 0) FillText(o->m3D8.mString, g);
    ::new (&o->mTimesOfDay) std::vector<LightingTimeOfDay>();
    const size_t times = g() % 5;
    o->mTimesOfDay.reserve(times + g() % 3);
    for (size_t i = 0; i < times; ++i) {
        LightingTimeOfDay t;
        const uint32_t a = g(), b = g();
        std::memcpy(&t.mTime, &a, 4);
        std::memcpy(&t.mValue, &b, 4);
        FillText(t.mName, g);
        o->mTimesOfDay.push_back(t);
    }
}

void DestroyBaseMembers(Base *o)
{
    std::destroy_at(&o->mTimesOfDay);
    o->m3D8.Destruct();
    std::destroy_at(&o->mRoomSettings);
    std::destroy_at(&o->mCurrent);
    std::destroy_at(&o->mStates);
}

std::string Raw(const void *p, size_t n, size_t blankFrom, size_t blankTo)
{
    std::string s;
    const auto *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; i += 4) {
        if (i >= blankFrom && i < blankTo) continue;
        uint32_t w;
        std::memcpy(&w, b + i, 4);
        s += Hex(w) + " ";
    }
    return s;
}

std::string DescribeBase(const Base *o)
{
    std::string s = "words " + Raw(o, Base::kSize, 0x1ac, 0x404);
    DescribeVector(s, "states", o->mStates);
    for (const LightingState &st : o->mStates) {
        s += " [";
        DescribeState(s, st);
        s += "]";
    }
    s += "\ncurrent";
    DescribeState(s, o->mCurrent);
    DescribeVector(s, "rooms", o->mRoomSettings);
    for (const LightingSettingsEntry &e : o->mRoomSettings) DescribeEntry(s, e);
    s += "\nm3d8";
    DescribeRZ(s, o->m3D8);
    DescribeVector(s, "times", o->mTimesOfDay);
    for (const LightingTimeOfDay &t : o->mTimesOfDay) {
        uint32_t a, b;
        std::memcpy(&a, &t.mTime, 4);
        std::memcpy(&b, &t.mValue, 4);
        s += " [" + Hex(a) + " " + Hex(b);
        DescribeText(s, t.mName);
        s += "]";
    }
    return s + "\n";
}

// ---- the fake faces -----------------------------------------------------------------------------------------------------------

uint32_t gBaseResult = 1;
void **gAnswerCell = nullptr; // QueryInterface's out cell: FakeAddRef logs what it holds when called (coordinator mutants LY)

uint32_t __fastcall FakeAddRef(void *face, void *)
{
    Quiet q;
    L("addref on");
    LOffset(face);
    if (gAnswerCell) L("cell holds"), LOffset(*gAnswerCell);
    LBlocks();
    return 3;
}
uint32_t __fastcall FakeRelease(void *face, void *)
{
    Quiet q;
    L("release on");
    LOffset(face);
    LBlocks();
    return 2;
}
void __fastcall FakeSetVersionRange(void *face, void *, int32_t low, int32_t high)
{
    Quiet q;
    L("range on");
    LOffset(face);
    L(static_cast<uint32_t>(low));
    L(static_cast<uint32_t>(high));
    LBlocks();
}
// The commands' constructors (cCommand 0xaf7820, cBlockCommandBase 0xaf7750; the image's and ours stood in for, so the bodies'
// own handling of a failing allocation is out of these tests): logged (the block's size), the vtable word a marker, the parser,
// count and value words zero; nothing allocated. kThrowCommand throws from them.
enum : int { kThrowCommand = 9 };
void *__fastcall FakeCommandConstruct(uint32_t *self, void *)
{
    Quiet q;
    L("command constructed, size");
    L(static_cast<uint32_t>(_msize(self)));
    LBlocks();
    MaybeThrow(kThrowCommand);
    self[0] = 0xc0de0000u;
    self[1] = self[2] = 0;
    if (_msize(self) >= 0x44) self[3] = 0;
    return self;
}
struct CommandConstructors {
    shape::StandIn plain{0xaf7820u, &cp::cCommand::Construct, &FakeCommandConstruct};
    shape::StandIn block{0xaf7750u, &cp::cBlockCommandBase::Construct, &FakeCommandConstruct};
};

// Slot 4: the command's words (cCommand's vtable, parser and count; a block command's value), its block's size; then freed.
void __fastcall FakeRegisterCommand(void *face, void *, void *command)
{
    Quiet q;
    L("command on");
    LOffset(face);
    if (command) {
        const uint32_t *w = static_cast<const uint32_t *>(command);
        const size_t size = _msize(command);
        L(" vtable");
        L(InImage(w[0]) ? w[0] - ImageBase() : w[0]);
        L(" parser");
        L(w[1]);
        L(" count");
        L(w[2]);
        L(" size");
        L(static_cast<uint32_t>(size));
        if (size >= 0x44) {
            L(" value");
            L(w[3]);
        }
        LBlocks();
        ::operator delete(command);
    } else {
        L(" null");
        LBlocks();
    }
}

// The faces' tables of one class: the unknown face's a fake; the parser face's a copy of the game's with slots 2 and 4 faked.
struct Faces {
    void *unknown[3] = {nullptr, reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeRelease)};
    void *parser[51];
    Faces(difftest::Context &ctx, uint32_t parserVTable)
    {
        std::memcpy(parser, ctx.image.At<uint8_t>(parserVTable), sizeof(parser));
        parser[2] = reinterpret_cast<void *>(&FakeSetVersionRange);
        parser[4] = reinterpret_cast<void *>(&FakeRegisterCommand);
    }
};

template <class T> void Store(void *at, T v) { std::memcpy(at, &v, sizeof(v)); }
template <class T> T Load(const void *at)
{
    T v;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

// kSize + 0x80 bytes of junk with the game's vbtables, the vtordisps zero, the faces' tables and random members.
Base *NewBase(std::mt19937 &g, const Faces &f)
{
    auto *o = static_cast<Base *>(::operator new(Base::kSize + 0x80));
    Junk(g, o, Base::kSize + 0x80);
    auto *b = reinterpret_cast<uint8_t *>(o);
    Store(b + 8, GameAddress<void>(Base::kVbTable));
    Store(b + 0x414, GameAddress<void>(Base::kParserFaceVbTable));
    Store<uint32_t>(b + 0x404, 0);
    Store<uint32_t>(b + 0x40c, 0);
    Store<const void *>(b + 0x408, f.unknown);
    Store<const void *>(b + 0x410, f.parser);
    FillBaseMembers(o, g);
    return o;
}
void FreeBase(Base *o)
{
    DestroyBaseMembers(o);
    ::operator delete(o);
}

// A face moved by a vtordisp (the thunk takes it off), restored at the end of the scope.
struct FaceShift {
    uint8_t *at;
    alignas(4) uint8_t saved[8];
    void *face;
    FaceShift(void *object, uint32_t offset, int32_t vtordisp)
    {
        uint8_t *const f = static_cast<uint8_t *>(object) + offset + vtordisp;
        at = f - 4;
        std::memcpy(saved, at, 8);
        if (vtordisp) std::memcpy(f, static_cast<uint8_t *>(object) + offset, 4); // the vtable word
        Store(at, vtordisp);
        face = f;
    }
    ~FaceShift() { std::memcpy(at, saved, 8); }
};

int32_t Vtordisp(std::mt19937 &g, int round) { return round % 4 == 3 ? 0x20 + 4 * static_cast<int32_t>(g() % 8) : 0; }

// The face slot `thunk` on side 0 (the image's) or our hook of it on side 1.
template <class Fn> Fn SideFn(difftest::Context &ctx, int side, uint32_t thunk)
{
    if (side == 0) return ctx.Original<Fn>(thunk);
    const HookSpec *h = HookAt(thunk);
    return h ? reinterpret_cast<Fn>(h->replacement) : nullptr;
}

// ---- stand-ins for cBaseParser's bodies -----------------------------------------------------------------------------------------
// The image's take cBaseParser's face (+0x1b8 of its part; QueryInterface the unknown face, +0x1b0); ours are members of the part.

void LogBase(const char *what, const void *part)
{
    L("base ");
    L(what);
    L(" this");
    LOffset(part);
}
bool __fastcall ImageParserInitialise(void *face, void *, uint32_t caseSensitive)
{
    Quiet q;
    LogBase("Initialise", static_cast<uint8_t *>(face) - 0x1b8);
    L(caseSensitive & 0xff);
    LBlocks();
    MaybeThrow(kThrowBase);
    return gBaseResult & 1;
}
bool __fastcall OurParserInitialise(cBaseParser *self, void *, uint32_t caseSensitive)
{
    Quiet q;
    LogBase("Initialise", self);
    L(caseSensitive & 0xff);
    LBlocks();
    MaybeThrow(kThrowBase);
    return gBaseResult & 1;
}
#define PARSER_FAKES(Name, Ret, value)                                                                                             \
    Ret __fastcall ImageParser##Name(void *face, void *)                                                                           \
    {                                                                                                                              \
        Quiet q;                                                                                                                   \
        LogBase(#Name, static_cast<uint8_t *>(face) - 0x1b8);                                                                     \
        LBlocks();                                                                                                                 \
        MaybeThrow(kThrowBase);                                                                                                    \
        return value;                                                                                                              \
    }                                                                                                                              \
    Ret __fastcall OurParser##Name(cBaseParser *self, void *)                                                                     \
    {                                                                                                                              \
        Quiet q;                                                                                                                   \
        LogBase(#Name, self);                                                                                                      \
        LBlocks();                                                                                                                 \
        MaybeThrow(kThrowBase);                                                                                                    \
        return value;                                                                                                              \
    }
PARSER_FAKES(Shutdown, bool, (gBaseResult & 1) != 0)
PARSER_FAKES(Reset, void, void())
PARSER_FAKES(ResetBlocks, void, void())
PARSER_FAKES(RestartViaReset, void, void())
#undef PARSER_FAKES
bool __fastcall ImageParserQueryInterface(void *face, void *, uint32_t iid, void **out)
{
    Quiet q;
    LogBase("QueryInterface", static_cast<uint8_t *>(face) - 0x1b0);
    L(iid);
    LBlocks();
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(0x51500000u + (iid & 0xffff)));
    return gBaseResult & 1;
}
bool __fastcall OurParserQueryInterface(cBaseParser *self, void *, uint32_t iid, void **out)
{
    Quiet q;
    LogBase("QueryInterface", self);
    L(iid);
    LBlocks();
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(0x51500000u + (iid & 0xffff)));
    return gBaseResult & 1;
}

// The image's body and our member of a cBaseParser call, both stood in for.
struct BaseStandIn {
    shape::CodePatch image, ours;
    template <class Member, class ImageFake, class OurFake>
    BaseStandIn(uint32_t rva, Member member, ImageFake imageFake, OurFake ourFake)
        : image(rva, shape::CodeOf(imageFake)), ours(shape::MemberCode(member), shape::CodeOf(ourFake))
    {
    }
};

} // namespace

// ---- cTSLightingParserState: the destructors ------------------------------------------------------------------------------------

namespace {

// The base destructor (0xafe1b0, the part itself on both sides): the words the destructor stored again (ours named by the image's
// table they stand for).
void __fastcall BaseDestruct(cBaseParser *self, void *)
{
    Quiet q;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(self);
    auto vt = [&](size_t off, const void *own, uint32_t image) {
        const uint32_t w = Load<uint32_t>(b + off);
        L(w == P(own) ? image : InImage(w) ? w - ImageBase() : w);
    };
    LogBase("Destruct", self);
    L(" vtables");
    vt(0, Base::kOwnVTable, Base::kVTable);
    vt(0x408, Base::kOwnUnknownVTable, Base::kUnknownVTable);
    vt(0x410, Base::kOwnParserVTable, Base::kParserVTable);
    L(" vtordisps");
    L(Load<uint32_t>(b + 0x404));
    L(Load<uint32_t>(b + 0x40c));
    LBlocks();
}

template <class Run> void RunBaseDestroy(difftest::Context &ctx, uint32_t seed, Run run)
{
    Allocators allocators;
    Faces faces(ctx, Base::kParserVTable);
    shape::StandIn base(0xafe1b0u, &cBaseParser::Destruct, &BaseDestruct);
    std::mt19937 g(seed);
    for (int round = 0; round < 60; ++round) {
        const uint32_t flags = round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : g();
        const uint32_t objectSeed = g();
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 og(objectSeed);
            Base *o = NewBase(og, faces);
            auto *b = reinterpret_cast<uint8_t *>(o);
            // The words the destructor stores again start as junk, so one left out shows in the base call's log.
            for (uint32_t off : {0x0u, 0x404u, 0x408u, 0x40cu, 0x410u}) Store<uint32_t>(b + off, og());
            gObj = b;
            bool freed = false;
            void *r = nullptr;
            std::string m = Measured(-1, [&] { r = run(side, o, flags); });
            {
                // Freed: the object's block among the frees (its size, kSize + 0x80, is unlike any member's).
                freed = m.find(" " + std::to_string(Base::kSize + 0x80)) != std::string::npos;
            }
            got[side] = m + "-> " + Offset(r) + " object freed " + std::to_string(freed);
            if (!freed) ::operator delete(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (flags %08x): %s", round, flags, Diff(got[0], got[1]).c_str());
    }
}

} // namespace

// 0xb3d3e0 (__thiscall): the three vtables and the vtordisps stored again, the members destroyed (times, the m3D8 string, the
// rooms, the current state, the states: each block's size in the order freed), the base destructor. The object is not freed.
DIFF_TEST(LightingParserState_Destruct, 0xb3d3e0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xb3d3e0);
    RunBaseDestroy(ctx, 0xb3d3e0, [&](int side, Base *o, uint32_t) -> void * {
        if (side == 0) orig(o);
        else BaseAdapters::Destruct(o, nullptr);
        return o;
    });
}

// Primary slot 0 (0xb3d860, ret 4): flags 0, 1 and others; the memory freed when bit 0 is set.
DIFF_TEST(LightingParserState_DeletingDestructor, 0xb3d860)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xb3d860);
    RunBaseDestroy(ctx, 0xb3d860, [&](int side, Base *o, uint32_t flags) -> void * {
        if (side == 0) return orig(o, flags);
        return BaseAdapters::DeletingDestructor(o, nullptr, flags);
    });
}

// ---- FindState and the two copies it and Reset use ----------------------------------------------------------------------------

// Primary slot 5 (0xb3eda0, ret 0xc): names in the states, in another case, empty, long, absent; the out state's own name short,
// long or the same text. Compared: the result, the index word, the out state, the parser, and each allocation failing.
DIFF_TEST(LightingParserState_FindState, 0xb3eda0)
{
    Allocators allocators;
    Faces faces(ctx, Base::kParserVTable);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const char *, void *, uint32_t *)>(0xb3eda0);
    std::mt19937 g(0xb3eda0);
    for (int round = 0; round < 120; ++round) {
        const uint32_t seed = g(), pick = g();
        EachFailure(ctx, "round " + std::to_string(round), [&](int side, int failAt) {
            std::mt19937 og(seed);
            Base *o = NewBase(og, faces);
            alignas(4) uint8_t raw[sizeof(LightingState)];
            auto *out = reinterpret_cast<LightingState *>(raw);
            BuildState(out, og);
            uint32_t index = og();
            std::string name = PoolText(pick);
            if (pick % 5 == 0 && !o->mStates.empty()) name = o->mStates[pick % o->mStates.size()].mName.mString;
            if (pick % 11 == 1) name = "absent";
            gObj = reinterpret_cast<uint8_t *>(o);
            uint32_t r = 0;
            std::string s = Measured(failAt, [&] {
                r = side == 0 ? orig(o, name.c_str(), out, &index)
                              : BaseAdapters::FindState(o, nullptr, name.c_str(), out, &index);
            });
            s += "-> " + Hex(r & 0xff) + " index " + Hex(index) + "\nout";
            DescribeState(s, *out);
            s += "\n" + DescribeBase(o);
            out->~LightingState();
            FreeBase(o);
            return s;
        });
    }
}

// 0xb3d510 (__thiscall, ret 4, returns this): another state's name text and values; the state itself (the name kept).
DIFF_TEST(LightingParserState_AssignState, 0xb3d510)
{
    Allocators allocators;
    auto orig = ctx.Original<void *(__thiscall *)(void *, const void *)>(0xb3d510);
    std::mt19937 g(0xb3d510);
    for (int round = 0; round < 150; ++round) {
        const uint32_t seed = g();
        const bool self = round % 9 == 4;
        EachFailure(ctx, "round " + std::to_string(round), [&](int side, int failAt) {
            std::mt19937 og(seed);
            auto *a = static_cast<LightingState *>(::operator new(sizeof(LightingState)));
            auto *b = static_cast<LightingState *>(::operator new(sizeof(LightingState)));
            BuildState(a, og);
            BuildState(b, og);
            LightingState *other = self ? a : b;
            gObj = reinterpret_cast<uint8_t *>(a);
            void *r = nullptr;
            std::string s = Measured(failAt, [&] { r = side == 0 ? orig(a, other) : BaseAdapters::AssignState(a, nullptr, other); });
            s += "-> " + Offset(r) + "\nself";
            DescribeState(s, *a);
            s += "\nother";
            DescribeState(s, *b);
            std::destroy_at(a);
            std::destroy_at(b);
            ::operator delete(a);
            ::operator delete(b);
            return s;
        });
    }
}

// 0xb3d190 (__thiscall, ret 4, returns this): a room settings entry copied into junk memory (the config words, the text's
// characters, a new vtable word and count 0).
DIFF_TEST(LightingParserState_CopySettings, 0xb3d190)
{
    Allocators allocators;
    auto orig = ctx.Original<void *(__thiscall *)(void *, const void *)>(0xb3d190);
    std::mt19937 g(0xb3d190);
    for (int round = 0; round < 120; ++round) {
        const uint32_t seed = g();
        EachFailure(ctx, "round " + std::to_string(round), [&](int side, int failAt) {
            std::mt19937 og(seed);
            auto *other = static_cast<LightingSettingsEntry *>(::operator new(sizeof(LightingSettingsEntry)));
            ::new (other) LightingSettingsEntry();
            Junk(og, &other->mBlock.mConfig, sizeof(other->mBlock.mConfig));
            FillText(other->mBlock.mText.mString, og);
            other->mBlock.mText.mRefCount = og();
            auto *self = static_cast<uint8_t *>(::operator new(sizeof(LightingSettingsEntry)));
            Junk(og, self, sizeof(LightingSettingsEntry));
            gObj = self;
            void *r = nullptr;
            std::string s = Measured(failAt, [&] {
                r = side == 0 ? orig(self, other)
                              : BaseAdapters::CopySettings(reinterpret_cast<LightingSettingsEntry *>(self), nullptr, other);
            });
            const bool made = s.find("returned") != std::string::npos;
            // A copy that threw is not an object (nothing reads it); only its config words are compared then.
            s += "-> " + Offset(r) + "\nconfig " + Raw(self, 0x30, 0, 0);
            if (made) {
                s += "\nself";
                DescribeEntry(s, *reinterpret_cast<LightingSettingsEntry *>(self));
                s += " string words " + Hex(Load<uint32_t>(self + 0x44)) + " " + Hex(Load<uint32_t>(self + 0x48));
                std::destroy_at(reinterpret_cast<LightingSettingsEntry *>(self));
            }
            s += "\nother";
            DescribeEntry(s, *other);
            std::destroy_at(other);
            ::operator delete(other);
            ::operator delete(self);
            return s;
        });
    }
}

// ---- cTSLightingParserState: the faces' own bodies ------------------------------------------------------------------------------

// The unknown face's slot 0 (thunk 0xb3f632 -> 0xb3f640, ret 8): kIID gives the object itself (AddRef on the unknown face); others
// cBaseParser's (0xb05940 at the unknown face less 0x258; stood in for), whatever it answers.
DIFF_TEST(LightingParserState_QueryInterface, 0xb3f640)
{
    Allocators allocators;
    Faces faces(ctx, Base::kParserVTable);
    BaseStandIn qi(0xb05940u, &cBaseParser::QueryInterface, &ImageParserQueryInterface, &OurParserQueryInterface);
    using Fn = uint32_t(__fastcall *)(void *, void *, uint32_t, void **);
    std::mt19937 g(0xb3f640);
    const uint32_t kIids[] = {Base::kIID, 1, 0x95035f4, 0, Base::kIID ^ 1, 0xffffffff};
    for (int round = 0; round < 48; ++round) {
        const uint32_t seed = g();
        const uint32_t iid = round < 6 ? kIids[round] : round % 3 == 0 ? Base::kIID : g();
        const int32_t vtordisp = Vtordisp(g, round);
        gBaseResult = round % 5 == 2 ? 0 : 1;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 og(seed);
            Base *o = NewBase(og, faces);
            gObj = reinterpret_cast<uint8_t *>(o);
            void *out = reinterpret_cast<void *>(static_cast<uintptr_t>(og()));
            uint32_t r = 0;
            std::string s;
            {
                FaceShift shift(o, 0x408, vtordisp);
                const Fn fn = SideFn<Fn>(ctx, side, 0xb3f632);
                gAnswerCell = &out;
                s = Measured(-1, [&] { r = fn(shift.face, nullptr, iid, &out); });
                gAnswerCell = nullptr;
            }
            s += "-> " + Hex(r & 0xff) + " out " + (InImage(P(out)) || (P(out) >> 16) == 0x5150 ? Hex(P(out)) : Offset(out)) + "\n" +
                 DescribeBase(o);
            got[side] = s;
            FreeBase(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (iid %08x vtordisp %d): %s", round, iid, vtordisp, Diff(got[0], got[1]).c_str());
    }
}

// The parser face's slot 0 (thunk 0xb3ee93 -> 0xb3eea0, ret 4): the parser's Initialise (stood in for; its result 0 or 1, ignored),
// versions 4..10, the lightingState and timeOfDay commands made (0x44 bytes each) and registered through the face; true. The case
// flag with junk in its upper bytes; vtordisps; each allocation failing; the base's call throwing.
DIFF_TEST(LightingParserState_Initialise, 0xb3eea0)
{
    Allocators allocators;
    Faces faces(ctx, Base::kParserVTable);
    BaseStandIn init(0xb02880u, &cBaseParser::Initialise, &ImageParserInitialise, &OurParserInitialise);
    CommandConstructors constructors;
    using Fn = uint32_t(__fastcall *)(void *, void *, uint32_t);
    std::mt19937 g(0xb3eea0);
    for (int round = 0; round < 24; ++round) {
        const uint32_t seed = g();
        const uint32_t arg = (g() & 0xffffff00u) | (round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() & 0xff));
        const int32_t vtordisp = Vtordisp(g, round);
        gBaseResult = round % 5 == 4 ? 0 : 1;
        gThrow = round % 7 == 6 ? kThrowBase : round % 7 == 3 ? kThrowCommand : kThrowNone;
        EachFailure(ctx, "round " + std::to_string(round), [&](int side, int failAt) {
            std::mt19937 og(seed);
            Base *o = NewBase(og, faces);
            gObj = reinterpret_cast<uint8_t *>(o);
            uint32_t r = 0;
            std::string s;
            {
                FaceShift shift(o, 0x410, vtordisp);
                const Fn fn = SideFn<Fn>(ctx, side, 0xb3ee93);
                s = Measured(failAt, [&] { r = fn(shift.face, nullptr, arg); });
            }
            s += "-> " + Hex(r & 0xff) + "\n" + DescribeBase(o);
            FreeBase(o);
            return s;
        });
        if (round == 0) {
            std::mt19937 og(seed);
            Base *o = NewBase(og, faces);
            gObj = reinterpret_cast<uint8_t *>(o);
            const std::string s = Measured(-1, [&] { ctx.Original<Fn>(0xb3ee93)(reinterpret_cast<uint8_t *>(o) + 0x410, nullptr, 1); });
            if (s.find("command on") == std::string::npos) ctx.Fail("the original registered no command:\n%s", s.c_str());
            FreeBase(o);
        }
    }
    gThrow = kThrowNone;
    gBaseResult = 1;
}

// The parser face's slot 9 (thunk 0xb3d891 -> 0xb3d8a0): the parser's Reset (0xaff120, stood in for), the states, rooms and times
// emptied (their blocks freed in order), one room of the defaults appended (the room vector's block made when it had none).
DIFF_TEST(LightingParserState_Reset, 0xb3d8a0)
{
    Allocators allocators;
    Faces faces(ctx, Base::kParserVTable);
    BaseStandIn reset(0xaff120u, &cBaseParser::Reset, &ImageParserReset, &OurParserReset);
    using Fn = void(__fastcall *)(void *, void *);
    std::mt19937 g(0xb3d8a0);
    for (int round = 0; round < 60; ++round) {
        const uint32_t seed = g();
        const int32_t vtordisp = Vtordisp(g, round);
        const bool noRooms = round % 4 == 1;
        gThrow = round % 9 == 7 ? kThrowBase : kThrowNone;
        EachFailure(ctx, "round " + std::to_string(round), [&](int side, int failAt) {
            std::mt19937 og(seed);
            Base *o = NewBase(og, faces);
            if (noRooms) std::vector<LightingSettingsEntry>().swap(o->mRoomSettings);
            gObj = reinterpret_cast<uint8_t *>(o);
            std::string s;
            {
                FaceShift shift(o, 0x410, vtordisp);
                const Fn fn = SideFn<Fn>(ctx, side, 0xb3d891);
                s = Measured(failAt, [&] { fn(shift.face, nullptr); });
            }
            s += DescribeBase(o);
            FreeBase(o);
            return s;
        });
    }
    gThrow = kThrowNone;
}

// The parser face's slots 10 (thunk 0xb3f130 -> 0xb3f140, the parser's ResetBlocks 0xb03e50) and 1 (thunk 0xb3f710 -> 0xb3f720,
// the parser's Shutdown 0xb06760, its result returned): the parser's bodies stood in for, nothing else done.
DIFF_TEST(LightingParserState_ResetBlocksShutdown, 0xb3f140)
{
    Allocators allocators;
    Faces faces(ctx, Base::kParserVTable);
    BaseStandIn blocks(0xb03e50u, &cBaseParser::ResetBlocks, &ImageParserResetBlocks, &OurParserResetBlocks);
    BaseStandIn shutdown(0xb06760u, &cBaseParser::Shutdown, &ImageParserShutdown, &OurParserShutdown);
    using Fn = uint32_t(__fastcall *)(void *, void *);
    std::mt19937 g(0xb3f140);
    for (int round = 0; round < 32; ++round) {
        const uint32_t seed = g();
        const int32_t vtordisp = Vtordisp(g, round);
        gBaseResult = round % 3 == 0 ? 0 : 1;
        gThrow = round % 8 == 5 ? kThrowBase : kThrowNone;
        for (uint32_t thunk : {0xb3f130u, 0xb3f710u}) {
            std::string got[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 og(seed);
                Base *o = NewBase(og, faces);
                gObj = reinterpret_cast<uint8_t *>(o);
                uint32_t r = 0;
                std::string s;
                {
                    FaceShift shift(o, 0x410, vtordisp);
                    const Fn fn = SideFn<Fn>(ctx, side, thunk);
                    s = Measured(-1, [&] { r = fn(shift.face, nullptr); });
                }
                got[side] = s + "-> " + (thunk == 0xb3f710u ? Hex(r & 0xff) : std::string("void")) + "\n" + DescribeBase(o);
                FreeBase(o);
            }
            ++ctx.cases;
            if (got[0] != got[1]) ctx.Fail("%x round %d: %s", thunk, round, Diff(got[0], got[1]).c_str());
        }
    }
    gThrow = kThrowNone;
    gBaseResult = 1;
}

// ---- the timeOfDay command and the state's commands' texts ------------------------------------------------------------------

namespace {

// The argument list: slot 10 Positional(count *, least, most) (ret 0xc). Logged.
struct FakeArgs {
    void **vt;
    const char *argv[5];
    uint32_t posCount; // what Positional stores
};
const char *const *__fastcall ArgsPositional(FakeArgs *self, void *, uint32_t *count, uint32_t least, uint32_t most)
{
    Quiet q;
    L("positional");
    L(least);
    L(most);
    L(count ? " out" : " null");
    LBlocks();
    if (count) *count = self->posCount;
    return self->argv + 1;
}
void *gArgsVt[16] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     reinterpret_cast<void *>(&ArgsPositional)};

// The command's parser face: slot 31 EvalFloat(text), logged (the text's length and hash), its value from the text; "throw" throws.
float __fastcall FaceEvalFloat(void *face, void *, const char *text)
{
    Quiet q;
    const size_t n = std::strlen(text);
    L("eval");
    L(static_cast<uint32_t>(n));
    L(Fnv(text, n));
    LBlocks();
    if (std::strcmp(text, "throw") == 0) MaybeThrow(kThrowEval);
    return static_cast<float>(Fnv(text, n) % 100000) / 7.0f;
}
void *gEvalVt[51] = {};
struct FakeFace {
    void **vt;
};

const char *ArgText(uint32_t i)
{
    static const char *const kArgs[] = {"noon", "", "1.5", "throw", "a time name longer than the inline buffer", "dusk"};
    i %= 7;
    return i < 6 ? kArgs[i] : Long5000().c_str();
}

} // namespace

// Slot 4 of cTimeOfDayCommand (0xb3ea80, ret 4): Positional(2, 3); the name (argv[0]) copied; the time evaluated (argv[1]) and the
// value (argv[2]) when Positional gave 3 (else 0); the entry appended to the parser's times. Positional counts 2, 3 and 4; names
// short, long and 5000 characters; times vectors full and not; an evaluation throwing; each allocation failing.
DIFF_TEST(LightingParserState_TimeOfDay, 0xb3ea80)
{
    Allocators allocators;
    Faces faces(ctx, Base::kParserVTable);
    gEvalVt[31] = reinterpret_cast<void *>(&FaceEvalFloat);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xb3ea80);
    std::mt19937 g(0xb3ea80);
    for (int round = 0; round < 100; ++round) {
        const uint32_t seed = g();
        FakeArgs args{gArgsVt, {"timeOfDay", ArgText(g()), ArgText(g()), ArgText(g()), "x"}, 2 + g() % 3};
        if (round % 6 == 0) args.argv[2] = "throw";
        if (round % 6 == 1) args.argv[3] = "throw";
        gThrow = kThrowEval;
        EachFailure(ctx, "round " + std::to_string(round), [&](int side, int failAt) {
            std::mt19937 og(seed);
            Base *o = NewBase(og, faces);
            alignas(4) uint8_t command[0x44];
            Junk(og, command, sizeof(command));
            FakeFace face{gEvalVt};
            Store(command + 4, &face);
            Store(command + 0x40, o);
            gObj = reinterpret_cast<uint8_t *>(o);
            std::string s = Measured(failAt, [&] {
                if (side == 0) orig(command, &args);
                else reinterpret_cast<nTSSG::cTimeOfDayCommand *>(command)->Execute(reinterpret_cast<cp::cIArguments *>(&args));
            });
            s += DescribeBase(o);
            FreeBase(o);
            return s;
        });
    }
    gThrow = kThrowNone;
}

// The text slots of the state's commands: cTimeOfDayCommand's GetName (0xb3f080) and the GetHelp (slot 6) of timeOfDay, control,
// fog, lightMap, transitionTimes and unlitRoomColor, for indices 0..7, -1 and random. Compared: the texts' addresses.
DIFF_TEST(LightingParserState_CommandTexts, 0xb3f080)
{
    using HelpFn = const char *(__thiscall *)(void *, uint32_t);
    using NameFn = const char *(__thiscall *)(void *);
    alignas(4) uint8_t command[0x80];
    std::mt19937 g(0xb3f080);
    Junk(g, command, sizeof(command));
    const HookSpec *name = HookAt(0xb3f080);
    ++ctx.cases;
    if (!name) ctx.Fail("0xb3f080 is not hooked");
    else if (ctx.Original<NameFn>(0xb3f080)(command) != reinterpret_cast<NameFn>(name->replacement)(command))
        ctx.Fail("GetName: %s / %s", ctx.Original<NameFn>(0xb3f080)(command), reinterpret_cast<NameFn>(name->replacement)(command));
    for (uint32_t rva : {0xb3da50u, 0xb3da00u, 0xb3da30u, 0xb3da40u, 0xb3da60u, 0xb3da70u}) {
        const HookSpec *h = HookAt(rva);
        if (!h) {
            ctx.Fail("%x is not hooked", rva);
            continue;
        }
        for (int i = 0; i < 24; ++i) {
            const uint32_t index = i < 8 ? static_cast<uint32_t>(i) : i == 8 ? 0xffffffffu : g();
            const char *a = ctx.Original<HelpFn>(rva)(command, index);
            const char *b = reinterpret_cast<HelpFn>(h->replacement)(command, index);
            ++ctx.cases;
            if (a != b) ctx.Fail("%x index %u: %s / %s", rva, index, a ? a : "(null)", b ? b : "(null)");
        }
    }
}

// ---- nTSSG::cLightingParserState --------------------------------------------------------------------------------------------

namespace {

// The light settings (cILightSettings): AddRef 1, Release 2, Init 3, Shutdown 4, Clear 5, each logged (the fake's name and count);
// the one made by the stood-in constructor (0x3c6a30) is freed by the Release that drops its count to 0.
struct FakeSettings {
    void *const *vt;
    uint32_t id;
    int32_t refs;
    uint32_t heap;
};
FakeSettings gOld; // a settings object the parser already held
FakeSettings *gMade[8];
bool gAlive[8];
int gMadeCount = 0;
const uint8_t *gSettingsOwner = nullptr; // the derived object whose settings word (+0x404) every settings call logs

void LSettings(const FakeSettings *f)
{
    L(" settings");
    L(f->id);
    L(static_cast<uint32_t>(f->refs));
    if (gSettingsOwner) {
        // What the owner holds at the call (pointers compared, never read): none, the old one, a made one by index.
        const FakeSettings *held = *reinterpret_cast<FakeSettings *const *>(gSettingsOwner + 0x404);
        uint32_t which = held == nullptr ? 0u : held == &gOld ? 0x01du : 0xbadu;
        for (int i = 0; i < gMadeCount; ++i)
            if (held == gMade[i]) which = 0x5e770 + static_cast<uint32_t>(i);
        L(" owner holds");
        L(which);
    }
}
uint32_t __fastcall SettingsAddRef(FakeSettings *self, void *)
{
    Quiet q;
    ++self->refs;
    L("addref");
    LSettings(self);
    LBlocks();
    return static_cast<uint32_t>(self->refs);
}
uint32_t __fastcall SettingsRelease(FakeSettings *self, void *)
{
    Quiet q;
    const int32_t refs = --self->refs;
    L("release");
    LSettings(self);
    LBlocks();
    if (refs == 0 && self->heap) {
        for (int i = 0; i < gMadeCount; ++i)
            if (gMade[i] == self) gAlive[i] = false;
        ::operator delete(self);
    }
    return static_cast<uint32_t>(refs);
}
#define SETTINGS_SLOT(Name)                                                                                                        \
    void __fastcall Settings##Name(FakeSettings *self, void *)                                                                     \
    {                                                                                                                              \
        Quiet q;                                                                                                                   \
        L(#Name);                                                                                                                  \
        LSettings(self);                                                                                                           \
        LBlocks();                                                                                                                 \
        MaybeThrow(kThrowSettings);                                                                                                \
    }
SETTINGS_SLOT(Init)
SETTINGS_SLOT(Shutdown)
SETTINGS_SLOT(Clear)
#undef SETTINGS_SLOT
void *const gSettingsVt[6] = {nullptr, reinterpret_cast<void *>(&SettingsAddRef), reinterpret_cast<void *>(&SettingsRelease),
                              reinterpret_cast<void *>(&SettingsInit), reinterpret_cast<void *>(&SettingsShutdown),
                              reinterpret_cast<void *>(&SettingsClear)};

void *__fastcall SettingsConstruct(FakeSettings *self, void *)
{
    Quiet q;
    L("settings constructed, size");
    L(static_cast<uint32_t>(_msize(self)));
    LBlocks();
    MaybeThrow(kThrowSettingsCtor);
    self->vt = gSettingsVt;
    self->id = 0x5e770 + static_cast<uint32_t>(gMadeCount);
    self->refs = 0;
    self->heap = 1;
    if (gMadeCount < 8) {
        gMade[gMadeCount] = self;
        gAlive[gMadeCount++] = true;
    }
    return self;
}

void ResetSettings(int32_t oldRefs)
{
    gOld = FakeSettings{gSettingsVt, 0x01d, oldRefs, 0};
    gMadeCount = 0;
}
void FreeMadeSettings()
{
    for (int i = 0; i < gMadeCount; ++i)
        if (gAlive[i]) ::operator delete(gMade[i]);
    gMadeCount = 0;
}

std::string SettingsText(const void *p)
{
    if (!p) return "null";
    if (p == &gOld) return "old refs " + std::to_string(gOld.refs);
    for (int i = 0; i < gMadeCount; ++i)
        if (gMade[i] == p) return gAlive[i] ? "made " + std::to_string(i) + " refs " + std::to_string(gMade[i]->refs) : "made (freed)";
    return "other " + Hex(P(p));
}

// kSize + 0x80 bytes of junk (the base part stays junk: its calls are stood in for) with the game's vbtables, the vtordisps zero,
// the faces' tables, the light settings (none or gOld) and a random text.
Derived *NewDerived(std::mt19937 &g, const Faces &f, bool settings)
{
    auto *o = static_cast<Derived *>(::operator new(Derived::kSize + 0x80));
    Junk(g, o, Derived::kSize + 0x80);
    auto *b = reinterpret_cast<uint8_t *>(o);
    Store(b + 8, GameAddress<void>(Derived::kVbTable));
    Store(b + 0x438, GameAddress<void>(Derived::kParserFaceVbTable));
    Store<uint32_t>(b + 0x428, 0);
    Store<uint32_t>(b + 0x430, 0);
    Store<const void *>(b + 0x42c, f.unknown);
    Store<const void *>(b + 0x434, f.parser);
    Store<void *>(b + 0x404, settings ? &gOld : nullptr);
    o->mText.Construct();
    FillText(o->mText.mString, g);
    o->mText.mRefCount = g() % 4;
    return o;
}

std::string DescribeDerived(const Derived *o)
{
    std::string s = "words " + Raw(o, Derived::kSize, 0x404, 0x428) + "\nsettings " + SettingsText(Load<void *>(reinterpret_cast<const uint8_t *>(o) + 0x404)) + "\ntext";
    DescribeRZ(s, o->mText);
    return s + "\n";
}
void FreeDerived(Derived *o)
{
    o->mText.Destruct();
    ::operator delete(o);
}

// cTSLightingParserState's bodies the derived ones call: the image's at the base's parser face (+0x410), ours on the object.
void LogTS(const char *what, const void *object)
{
    L("ts ");
    L(what);
    L(" this");
    LOffset(object);
}
bool __fastcall ImageTSInitialise(void *face, void *, uint32_t caseSensitive)
{
    Quiet q;
    LogTS("Initialise", static_cast<uint8_t *>(face) - 0x410);
    L(caseSensitive & 0xff);
    LBlocks();
    MaybeThrow(kThrowBase);
    return gBaseResult & 1;
}
bool __fastcall OurTSInitialise(Base *self, void *, uint32_t caseSensitive)
{
    Quiet q;
    LogTS("Initialise", self);
    L(caseSensitive & 0xff);
    LBlocks();
    MaybeThrow(kThrowBase);
    return gBaseResult & 1;
}
void __fastcall ImageTSReset(void *face, void *)
{
    Quiet q;
    LogTS("Reset", static_cast<uint8_t *>(face) - 0x410);
    LBlocks();
    MaybeThrow(kThrowBase);
}
void __fastcall OurTSReset(Base *self, void *)
{
    Quiet q;
    LogTS("Reset", self);
    LBlocks();
    MaybeThrow(kThrowBase);
}
// The base destructor (0xb3d3e0, the object on both sides): the text as its destructor left it (vtable word, first character,
// size, capacity, count), the settings, the words at the faces.
void __fastcall TSDestruct(Base *self, void *)
{
    Quiet q;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(self);
    LogTS("Destruct", self);
    L(" text");
    L(InImage(Load<uint32_t>(b + 0x408)) ? Load<uint32_t>(b + 0x408) - ImageBase() : Load<uint32_t>(b + 0x408));
    L(b[0x40c]);
    L(Load<uint32_t>(b + 0x41c));
    L(Load<uint32_t>(b + 0x420));
    L(Load<uint32_t>(b + 0x424));
    L(" settings");
    const void *settings = Load<void *>(b + 0x404);
    L(settings == &gOld ? 0x01du : settings ? 0xaaaau : 0u);
    L(" words");
    L(Load<uint32_t>(b));
    L(Load<uint32_t>(b + 0x428));
    L(Load<uint32_t>(b + 0x42c));
    L(Load<uint32_t>(b + 0x430));
    L(Load<uint32_t>(b + 0x434));
    LBlocks();
}

} // namespace

// Primary slot 0 (0x2cbc70, ret 4): the text destroyed (inline), the settings released, cTSLightingParserState's destructor (0xb3d3e0,
// stood in for), the memory freed when bit 0 of the flags is set. The settings none, or held with counts 1 and 2; texts short and long.
DIFF_TEST(LightingParserState_DerivedDeletingDestructor, 0x2cbc70)
{
    Allocators allocators;
    Faces faces(ctx, Derived::kParserVTable);
    shape::StandIn base(0xb3d3e0u, &Base::Destruct, &TSDestruct);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x2cbc70);
    std::mt19937 g(0x2cbc70);
    for (int round = 0; round < 60; ++round) {
        const uint32_t flags = round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : g();
        const uint32_t seed = g();
        const bool settings = round % 4 != 0;
        const int32_t refs = 1 + round % 2;
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            ResetSettings(refs);
            std::mt19937 og(seed);
            Derived *o = NewDerived(og, faces, settings);
            gObj = reinterpret_cast<uint8_t *>(o);
            gSettingsOwner = gObj;
            void *r = nullptr;
            std::string s = Measured(-1, [&] { r = side == 0 ? orig(o, flags) : DerivedAdapters::DeletingDestructor(o, nullptr, flags); });
            const bool freed = s.find(" " + std::to_string(Derived::kSize + 0x80)) != std::string::npos;
            got[side] = s + "-> " + Offset(r) + " freed " + std::to_string(freed) + " old refs " + std::to_string(gOld.refs);
            gSettingsOwner = nullptr;
            if (!freed) ::operator delete(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (flags %08x): %s", round, flags, Diff(got[0], got[1]).c_str());
    }
}

// The parser face's slot 0 (thunk 0x2da25d -> 0x2da270, ret 4): cTSLightingParserState's Initialise (0xb3eea0, stood in for; false
// returns false); a cLightSettings made (0x48 bytes; its constructor 0x3c6a30 stood in for) and held (AddRef, the old one Released),
// its Init; the lightAttribOverride (0x10 bytes) and lightAttributes (0x44) commands made and registered; true. Settings none or held;
// the base false or throwing; the settings' constructor or Init throwing; each allocation failing.
DIFF_TEST(LightingParserState_DerivedInitialise, 0x2da270)
{
    Allocators allocators;
    Faces faces(ctx, Derived::kParserVTable);
    BaseStandIn init(0xb3eea0u, &Base::Initialise, &ImageTSInitialise, &OurTSInitialise);
    shape::StandIn ctor(0x3c6a30u, &cLightSettings_Constructor, &SettingsConstruct);
    CommandConstructors constructors;
    using Fn = uint32_t(__fastcall *)(void *, void *, uint32_t);
    std::mt19937 g(0x2da270);
    for (int round = 0; round < 48; ++round) {
        const uint32_t seed = g();
        const uint32_t arg = (g() & 0xffffff00u) | (round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() & 0xff));
        const int32_t vtordisp = Vtordisp(g, round);
        const bool settings = round % 2 == 1;
        const int32_t refs = 1 + (round / 2) % 2;
        gBaseResult = round % 5 == 4 ? 0 : 1;
        const int throws[] = {kThrowNone, kThrowNone, kThrowBase, kThrowSettingsCtor, kThrowSettings, kThrowCommand};
        gThrow = round < 12 ? kThrowNone : throws[round % 6];
        EachFailure(ctx, "round " + std::to_string(round), [&](int side, int failAt) {
            ResetSettings(refs);
            std::mt19937 og(seed);
            Derived *o = NewDerived(og, faces, settings);
            gObj = reinterpret_cast<uint8_t *>(o);
            gSettingsOwner = gObj;
            uint32_t r = 0;
            std::string s;
            {
                FaceShift shift(o, 0x434, vtordisp);
                const Fn fn = SideFn<Fn>(ctx, side, 0x2da25d);
                s = Measured(failAt, [&] { r = fn(shift.face, nullptr, arg); });
            }
            s += "-> " + Hex(r & 0xff) + " old refs " + std::to_string(gOld.refs) + "\n" + DescribeDerived(o);
            gSettingsOwner = nullptr;
            FreeMadeSettings();
            FreeDerived(o);
            return s;
        });
    }
    gThrow = kThrowNone;
    gBaseResult = 1;
}

// The parser face's slots 1 (thunk 0x2e30d5 -> 0x2e30e0: the settings' Shutdown, the reference dropped, the parser's Shutdown 0xb06760
// with its result), 8 (0x2e02cd -> 0x2e02e0: the parser's RestartViaReset 0xb05ea0, then 0xb41a80 on the parser face, stood in for)
// and 9 (0x2d2805 -> 0x2d2810: cTSLightingParserState's Reset 0xb3d8a0, then the settings' Clear). Settings none or held (counts 1
// and 2); the calls throwing; vtordisps.
namespace {
void __cdecl FakeAppLighting(void *face)
{
    Quiet q;
    L("app lighting on");
    LOffset(face);
    LBlocks();
}
} // namespace

DIFF_TEST(LightingParserState_DerivedFaceBodies, 0x2e30e0)
{
    Allocators allocators;
    Faces faces(ctx, Derived::kParserVTable);
    BaseStandIn shutdown(0xb06760u, &cBaseParser::Shutdown, &ImageParserShutdown, &OurParserShutdown);
    BaseStandIn restart(0xb05ea0u, &cBaseParser::RestartViaReset, &ImageParserRestartViaReset, &OurParserRestartViaReset);
    BaseStandIn reset(0xb3d8a0u, &Base::Reset, &ImageTSReset, &OurTSReset);
    shape::CodePatch app(0xb41a80u, shape::CodeOf(&FakeAppLighting));
    using Fn = uint32_t(__fastcall *)(void *, void *);
    std::mt19937 g(0x2e30e0);
    for (int round = 0; round < 48; ++round) {
        const uint32_t seed = g();
        const int32_t vtordisp = Vtordisp(g, round);
        const bool settings = round % 3 != 0;
        const int32_t refs = 1 + round % 2;
        gBaseResult = round % 5 == 1 ? 0 : 1;
        const int throws[] = {kThrowNone, kThrowNone, kThrowBase, kThrowSettings};
        gThrow = round < 8 ? kThrowNone : throws[round % 4];
        for (uint32_t thunk : {0x2e30d5u, 0x2e02cdu, 0x2d2805u}) {
            std::string got[2];
            for (int side = 0; side < 2; ++side) {
                ResetSettings(refs);
                std::mt19937 og(seed);
                Derived *o = NewDerived(og, faces, settings);
                gObj = reinterpret_cast<uint8_t *>(o);
                gSettingsOwner = gObj;
                uint32_t r = 0;
                std::string s;
                {
                    FaceShift shift(o, 0x434, vtordisp);
                    const Fn fn = SideFn<Fn>(ctx, side, thunk);
                    s = Measured(-1, [&] { r = fn(shift.face, nullptr); });
                }
                got[side] = s + "-> " + (thunk == 0x2e30d5u ? Hex(r & 0xff) : std::string("void")) + " old refs " +
                            std::to_string(gOld.refs) + "\n" + DescribeDerived(o);
                gSettingsOwner = nullptr;
                FreeDerived(o);
            }
            ++ctx.cases;
            if (got[0] != got[1]) ctx.Fail("%x round %d: %s", thunk, round, Diff(got[0], got[1]).c_str());
        }
    }
    gThrow = kThrowNone;
    gBaseResult = 1;
}

// ---- the faces' thunks ------------------------------------------------------------------------------------------------------

namespace {

alignas(16) uint8_t gBuffer[0x800];
std::string gSeen;
uint32_t gResult = 0;

template <class T> uint32_t WordOf(T v)
{
    if constexpr (std::is_pointer_v<T>) return P(v);
    else if constexpr (std::is_same_v<T, float>) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        return bits;
    } else return static_cast<uint32_t>(v);
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

template <class R> std::string ResultText(R r)
{
    if constexpr (std::is_same_v<R, float>) return "f" + Hex(WordOf(r));
    else if constexpr (std::is_same_v<R, bool>) return Hex(r ? 1u : 0u);
    else return Hex(WordOf(r));
}

const int32_t kFixedVtordisps[] = {0, 4, 8, 0x10, -8, -0x14, 0x1b8, -0x100};
int32_t ThunkVtordisp(std::mt19937 &g, int i) { return i < 8 ? kFixedVtordisps[i] : static_cast<int32_t>(g() % 129) * 4 - 0x100; }

// The bytes of a `sub ecx, [ecx-4]; sub ecx, Shift; jmp` thunk up to the jump's displacement.
std::vector<uint8_t> ShiftedPrefix(int32_t shift)
{
    std::vector<uint8_t> p = {0x2b, 0x49, 0xfc};
    if (shift < 0x80) {
        p.insert(p.end(), {0x83, 0xe9, static_cast<uint8_t>(shift)});
    } else {
        p.insert(p.end(), {0x81, 0xe9});
        for (int i = 0; i < 4; ++i) p.push_back(static_cast<uint8_t>(shift >> (8 * i)));
    }
    p.push_back(0xe9);
    return p;
}

// A shifted thunk whose body has `Adapter` as its hook: the image's thunk and our hook of it (LightingShiftedThunk<Shift, Adapter>)
// called on a face with a vtordisp, the body stood in for on both sides.
template <int32_t Shift, auto Adapter> struct Check;
template <int32_t Shift, class R, class... Args, R(__fastcall *Adapter)(void *, void *, Args...)> struct Check<Shift, Adapter> {
    static void Run(difftest::Context &ctx, uint32_t thunk, const char *name, std::mt19937 &g)
    {
        using Fn = R(__fastcall *)(void *, void *, Args...);
        const uint8_t *code = ctx.image.At<uint8_t>(thunk);
        const std::vector<uint8_t> prefix = ShiftedPrefix(Shift);
        if (std::memcmp(code, prefix.data(), prefix.size()) != 0) {
            ctx.Fail("%s %x: not a thunk shifted by %x", name, thunk, Shift);
            return;
        }
        const int32_t rel = Load<int32_t>(code + prefix.size());
        const uint32_t body = thunk + static_cast<uint32_t>(prefix.size()) + 4 + rel;
        const HookSpec *bodyHook = HookAt(body), *thunkHook = HookAt(thunk);
        if (!bodyHook || bodyHook->replacement != reinterpret_cast<void *>(Adapter)) {
            ctx.Fail("%s %x: the body %x is not hooked to this adapter", name, thunk, body);
            return;
        }
        if (!thunkHook || thunkHook->replacement != reinterpret_cast<void *>(&LightingShiftedThunk<Shift, Adapter>::Call)) {
            ctx.Fail("%s %x: the thunk is not hooked to its shifted forwarder", name, thunk);
            return;
        }
        shape::StandIn stand(body, Adapter, &Recorder<Adapter>::Call);
        for (int i = 0; i < 24; ++i) {
            const int32_t d = ThunkVtordisp(g, i);
            uint8_t *face = gBuffer + 0x600;
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

// A `sub ecx, [ecx-4]; jmp body` thunk: its body (hooked unless `bodyHooked` is false), the thunk hooked to the forwarder `ours`.
void CheckOwnThunk(difftest::Context &ctx, uint32_t thunk, uint32_t body, const void *ours, const char *name, bool bodyHooked = true)
{
    const uint8_t *code = ctx.image.At<uint8_t>(thunk);
    if (!(code[0] == 0x2b && code[1] == 0x49 && code[2] == 0xfc && code[3] == 0xe9)) {
        ctx.Fail("%s %x: not a vtordisp thunk", name, thunk);
        return;
    }
    const int32_t rel = Load<int32_t>(code + 4);
    ++ctx.cases;
    if (thunk + 8 + rel != body) ctx.Fail("%s %x: the body is %x, not %x", name, thunk, thunk + 8 + rel, body);
    const HookSpec *h = HookAt(thunk);
    if (!h || h->replacement != ours) ctx.Fail("%s %x: not hooked to its forwarder", name, thunk);
    if (bodyHooked && !HookAt(body)) ctx.Fail("%s %x: the body %x is not hooked", name, thunk, body);
}
template <auto Adapter> const void *OwnThunk() { return reinterpret_cast<const void *>(&cp::VtordispThunk<Adapter>::Call); }

// Slots 11 and 45: shifted thunks into folded bodies (not hookable), run unstood on random buffers.
void CheckFolded(difftest::Context &ctx, uint32_t noOpThunk, uint32_t versionThunk, int32_t shift, std::mt19937 &g)
{
    const std::vector<uint8_t> prefix = ShiftedPrefix(shift);
    const HookSpec *noOp = HookAt(noOpThunk), *version = HookAt(versionThunk);
    if (!noOp || !version || std::memcmp(ctx.image.At<uint8_t>(noOpThunk), prefix.data(), prefix.size()) != 0 ||
        std::memcmp(ctx.image.At<uint8_t>(versionThunk), prefix.data(), prefix.size()) != 0) {
        ctx.Fail("slots 11 / 45 (%x %x): not hooked, or not shifted thunks", noOpThunk, versionThunk);
        return;
    }
    using VersionFn = int32_t(__fastcall *)(void *, void *);
    using NoOpFn = void(__fastcall *)(void *, void *);
    for (int i = 0; i < 48; ++i) {
        Junk(g, gBuffer, sizeof(gBuffer));
        const int32_t d = ThunkVtordisp(g, i % 24);
        uint8_t *face = gBuffer + 0x600;
        std::memcpy(face - 4, &d, 4);
        const int32_t a = ctx.Original<VersionFn>(versionThunk)(face, nullptr);
        const int32_t b = reinterpret_cast<VersionFn>(version->replacement)(face, nullptr);
        std::vector<uint8_t> before(gBuffer, gBuffer + sizeof(gBuffer));
        ctx.Original<NoOpFn>(noOpThunk)(face, nullptr);
        reinterpret_cast<NoOpFn>(noOp->replacement)(face, nullptr);
        ++ctx.cases;
        if (a != b) ctx.Fail("slot 45 %x vtordisp %d: %d / %d", versionThunk, d, a, b);
        if (std::memcmp(before.data(), gBuffer, sizeof(gBuffer)) != 0) ctx.Fail("slot 11 %x wrote to the buffer", noOpThunk);
    }
}

} // namespace

// Every thunk of both classes' faces: the shifted ones into cBaseParser's bodies (0x258 in cTSLightingParserState, 0x27c in the derived
// class) and the derived class's 0x24 ones into cTSLightingParserState's bodies, each with its body's adapter; the own ones (bodies and
// forwarders); the folded slots 11 and 45 run unstood.
DIFF_TEST(LightingParserState_Thunks, 0xb3d883)
{
    std::mt19937 g(0xb3d883);
#define X(base, derived, adapter)                                                                                                  \
    Check<0x258, &cBaseParserAdapters::adapter>::Run(ctx, base, #adapter, g);                                                     \
    Check<0x27c, &cBaseParserAdapters::adapter>::Run(ctx, derived, #adapter, g);
    X(0xb3d883, 0x2d023b, AddRef)
    X(0xb3f6a0, 0x2dfed0, Release)
    X(0xb3f6bc, 0x2e154f, SetVersionRange)
    X(0xb3f6d8, 0x2e29d4, SetContextState)
    X(0xb3f684, 0x2dfe00, RegisterCommand)
    X(0xb400ee, 0x2e36fb, UnregisterCommand)
    X(0xb400e0, 0x2e36ed, UnregisterCommandObject)
    X(0xb3f692, 0x2dfec2, RegisterMetaCommand)
    X(0xb3f552, 0x2dfbc6, RunFile)
    X(0xb3f58a, 0x2dfbfe, RunFiles)
    X(0xb3f57c, 0x2dfbf0, RunText)
    X(0xb3f544, 0x2df88a, RunLine)
    X(0xb3f739, 0x2e31a1, PushOpenBlock)
    X(0xb3f747, 0x2e31af, PushMetaBlock)
    X(0xb3da93, 0x2d4e10, PopMetaBlock)
    X(0xb3f702, 0x2e2a86, RunExpandedText)
    X(0xb3f624, 0x2dfd8a, PushNamespace)
    X(0xb3f608, 0x2dfd6e, PopNamespace)
    X(0xb3f616, 0x2dfd7c, PurgeNamespace)
    X(0xb3ed8e, 0x2d50d3, GetFunctions)
    X(0xb3f6e6, 0x2e29e2, SetOutput)
    X(0xb3f6f4, 0x2e2a78, SetHandler)
    X(0xb3f6ca, 0x2e29c6, SetSource)
    X(0xb400fc, 0x2e5689, FindVariable)
    X(0xb3d9d9, 0x2d3804, GetNamespacePath)
    X(0xb3f755, 0x2e31bd, ExpandVariables)
    X(0xb3f536, 0x2df87c, EvalBool)
    X(0xb3f598, 0x2dfc0c, EvalFloat)
    X(0xb3f5a6, 0x2dfc1a, EvalInt)
    X(0xb3f5de, 0x2dfc52, EvalVector2)
    X(0xb3f5ec, 0x2dfc60, EvalVector3)
    X(0xb3f5fa, 0x2dfc6e, EvalVector4)
    X(0xb3f5b4, 0x2dfc28, EvalColorRGBA)
    X(0xb3f5c2, 0x2dfc36, EvalColorARGB)
    X(0xb3f5d0, 0x2dfc44, EvalColorRGB)
    X(0xb3f0b6, 0x2df38b, CommandCount)
    X(0xb3d9af, 0x2d2a18, CommandAt)
    X(0xb3ef97, 0x2db041, IsInitialised)
    X(0xb3ef89, 0x2db033, IsCaseSensitive)
    X(0xb3efa5, 0x2db04f, GetText)
    X(0xb3f56e, 0x2dfbe2, GetFileName)
    X(0xb3f560, 0x2dfbd4, GetLine)
    X(0xb3d9e7, 0x2d3812, HasDefinition)
    X(0xb3d9bd, 0x2d2a26, HasCommand)
    X(0xb3f72b, 0x2e310e, GetSource)
#undef X
    Check<0x258, &cBaseParserAdapters::RestartViaReset>::Run(ctx, 0xb3f6ae, "RestartViaReset", g);
    Check<0x24, &BaseAdapters::QueryInterface>::Run(ctx, 0x2dfdf5, "QueryInterface (derived)", g);
    Check<0x24, &BaseAdapters::ResetBlocks>::Run(ctx, 0x2df399, "ResetBlocks (derived)", g);

    CheckOwnThunk(ctx, 0xb3f632, 0xb3f640, OwnThunk<&BaseAdapters::QueryInterface>(), "QueryInterface");
    CheckOwnThunk(ctx, 0xb3ee93, 0xb3eea0, OwnThunk<&BaseAdapters::Initialise>(), "Initialise");
    CheckOwnThunk(ctx, 0xb3f710, 0xb3f720, OwnThunk<&BaseAdapters::Shutdown>(), "Shutdown", false);
    CheckOwnThunk(ctx, 0xb3d891, 0xb3d8a0, OwnThunk<&BaseAdapters::Reset>(), "Reset");
    CheckOwnThunk(ctx, 0xb3f130, 0xb3f140, OwnThunk<&BaseAdapters::ResetBlocks>(), "ResetBlocks");
    CheckOwnThunk(ctx, 0x2da25d, 0x2da270, OwnThunk<&DerivedAdapters::Initialise>(), "Initialise (derived)");
    CheckOwnThunk(ctx, 0x2e30d5, 0x2e30e0, OwnThunk<&DerivedAdapters::Shutdown>(), "Shutdown (derived)");
    CheckOwnThunk(ctx, 0x2e02cd, 0x2e02e0, OwnThunk<&DerivedAdapters::RestartViaReset>(), "RestartViaReset (derived)");
    CheckOwnThunk(ctx, 0x2d2805, 0x2d2810, OwnThunk<&DerivedAdapters::Reset>(), "Reset (derived)");

    CheckFolded(ctx, 0xb3d9cb, 0xb4010a, 0x258, g);
    CheckFolded(ctx, 0x2d2a34, 0x2e5697, 0x27c, g);
}

// ---- both classes' vtables ----------------------------------------------------------------------------------------------------

namespace {

struct Table {
    const char *name;
    uint32_t image;
    const void *const *ours;
    size_t count;
    bool primary;
};

// Every slot of a table holds what the hook of the image's entry installs (primary slots 2 and 3, folded bodies shared by unrelated
// classes, are checked by what they do).
void CheckTables(difftest::Context &ctx, const Table *tables, size_t n)
{
    const uint32_t base = ImageBase();
    for (size_t t = 0; t < n; ++t) {
        const uint32_t *image = GameAddress<uint32_t>(tables[t].image);
        for (size_t i = 0; i < tables[t].count; ++i) {
            ++ctx.cases;
            if (tables[t].primary && (i == 2 || i == 3)) continue;
            const HookSpec *h = HookAt(image[i] - base);
            if (!h) ctx.Fail("%s slot %zu: the image's entry %x is not hooked", tables[t].name, i, image[i] - base);
            else if (h->replacement != tables[t].ours[i])
                ctx.Fail("%s slot %zu: ours is %p, the hook of %x installs %p", tables[t].name, i, tables[t].ours[i], image[i] - base,
                         h->replacement);
        }
    }
}

// Slots called on a whole, initialised object through its own tables: counts, commands, references, interfaces, resets, FindState,
// the folded setter and getter.
std::string Dispatch(void *object, uint32_t unknownOffset, uint32_t parserOffset, int round)
{
    using Fn0 = uint32_t(__thiscall *)(void *);
    using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
    using QiFn = uint32_t(__thiscall *)(void *, uint32_t, void **);
    using FindFn = uint32_t(__thiscall *)(void *, const char *, void *, uint32_t *);
    uint8_t *const o = static_cast<uint8_t *>(object);
    uint8_t *const unknown = o + unknownOffset, *const parser = o + parserOffset;
    auto **uv = *reinterpret_cast<void ***>(unknown);
    auto **pv = *reinterpret_cast<void ***>(parser);
    auto **vt = *reinterpret_cast<void ***>(o);
    std::string s;
    auto word = [&](uint32_t v) { s += " " + Hex(v); };
    word(reinterpret_cast<Fn0>(pv[40])(parser)); // CommandCount
    for (const char *name : {"lightingState", "timeOfDay", "LIGHTINGSTATE", "lightAttributes", "lightAttribOverride", ""})
        word(reinterpret_cast<Fn1>(pv[49])(parser, P(name)) & 0xff); // HasCommand
    word(reinterpret_cast<Fn0>(pv[42])(parser) & 0xff);           // IsInitialised
    word(reinterpret_cast<Fn0>(pv[43])(parser) & 0xff);           // IsCaseSensitive
    word(reinterpret_cast<Fn0>(uv[1])(unknown));                  // AddRef
    word(reinterpret_cast<Fn0>(uv[1])(unknown));
    word(reinterpret_cast<Fn0>(uv[2])(unknown)); // Release
    void *out = nullptr;
    word(reinterpret_cast<QiFn>(uv[0])(unknown, Base::kIID, &out) & 0xff);
    s += " qi " + Hex(P(out) - P(o));
    word(reinterpret_cast<Fn0>(uv[2])(unknown));
    out = nullptr;
    word(reinterpret_cast<QiFn>(uv[0])(unknown, 1, &out) & 0xff);
    s += " qi1 " + (out ? Hex(P(out) - P(o)) : std::string("null"));
    // ResetBlocks (slot 10) is not called: cBaseParser's needs the namespace stack a parse leaves (the thunk tests cover it).
    reinterpret_cast<Fn0>(pv[9])(parser); // Reset
    auto *b = reinterpret_cast<Base *>(o);
    s += " sizes " + std::to_string(b->mStates.size()) + " " + std::to_string(b->mRoomSettings.size()) + " " +
         std::to_string(b->mTimesOfDay.size());
    alignas(4) uint8_t raw[sizeof(LightingState)];
    auto *state = reinterpret_cast<LightingState *>(raw);
    state->Construct();
    uint32_t index = 0x77;
    word(reinterpret_cast<FindFn>(vt[5])(o, "day", state, &index) & 0xff);
    word(index);
    state->~LightingState();
    for (uint32_t value : {0x1234u + static_cast<uint32_t>(round), 0u, 0xffffffffu}) {
        reinterpret_cast<Fn1>(vt[2])(o, value);
        word(reinterpret_cast<Fn0>(vt[3])(o));
    }
    return s;
}

} // namespace

// Our vtables against the image's (each slot what the hook of the image's entry installs; the virtual base tables' words the image's),
// and whole objects (the original's built by the image's code, ours by our constructors) dispatching through them alike.
DIFF_TEST(LightingParserState_VTables, 0xe84200)
{
    const Table tables[] = {
        {"base primary", Base::kVTable, Base::kOwnVTable, std::size(Base::kOwnVTable), true},
        {"base cIGZUnknown", Base::kUnknownVTable, Base::kOwnUnknownVTable, std::size(Base::kOwnUnknownVTable), false},
        {"base cIBaseParser", Base::kParserVTable, Base::kOwnParserVTable, std::size(Base::kOwnParserVTable), false},
        {"derived primary", Derived::kVTable, Derived::kOwnVTable, std::size(Derived::kOwnVTable), true},
        {"derived cIGZUnknown", Derived::kUnknownVTable, Derived::kOwnUnknownVTable, std::size(Derived::kOwnUnknownVTable), false},
        {"derived cIBaseParser", Derived::kParserVTable, Derived::kOwnParserVTable, std::size(Derived::kOwnParserVTable), false},
    };
    CheckTables(ctx, tables, std::size(tables));
    ++ctx.cases;
    if (std::memcmp(GameAddress<int32_t>(Base::kVbTable), Base::kOwnVbTable, sizeof(Base::kOwnVbTable)) != 0 ||
        std::memcmp(GameAddress<int32_t>(Base::kParserFaceVbTable), Base::kOwnParserFaceVbTable, sizeof(Base::kOwnParserFaceVbTable)) != 0)
        ctx.Fail("cTSLightingParserState's virtual base tables differ from the image's");
    if (std::memcmp(GameAddress<int32_t>(Derived::kVbTable), Derived::kOwnVbTable, sizeof(Derived::kOwnVbTable)) != 0 ||
        std::memcmp(GameAddress<int32_t>(Derived::kParserFaceVbTable), Derived::kOwnParserFaceVbTable,
                    sizeof(Derived::kOwnParserFaceVbTable)) != 0)
        ctx.Fail("nTSSG::cLightingParserState's virtual base tables differ from the image's");

    Allocators allocators;
    shape::StandIn ctor(0x3c6a30u, &cLightSettings_Constructor, &SettingsConstruct);
    shape::CodePatch app(0xb41a80u, shape::CodeOf(&FakeAppLighting)); // cBaseParser's Initialise restarts through slot 8
    using InitFn = uint32_t(__thiscall *)(void *, uint32_t);
    using DeleteFn = void *(__thiscall *)(void *, uint32_t);
    using ConstructFn = void *(__thiscall *)(void *, uint32_t);
    for (int round = 0; round < 4; ++round) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            auto *o = static_cast<Base *>(::operator new(Base::kSize));
            uint8_t *const b = reinterpret_cast<uint8_t *>(o);
            std::memset(b, 0, Base::kSize); // cBaseParser's once flag (+0x10) is not the constructor's
            gObj = b;
            if (side == 0) ctx.Original<ConstructFn>(0xb3ce70)(o, 1);
            else o->Construct(1);
            auto **pv = *reinterpret_cast<void ***>(b + 0x410);
            gLog.clear();
            got[side] = "init " + Hex(reinterpret_cast<InitFn>(pv[0])(b + 0x410, round & 1) & 0xff) + Dispatch(o, 0x408, 0x410, round);
            auto **vt = *reinterpret_cast<void ***>(o);
            if (side == 0) ctx.Original<DeleteFn>(0xb3d860)(o, 1);
            else reinterpret_cast<DeleteFn>(vt[0])(o, 1);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("base round %d: %s / %s", round, got[0].c_str(), got[1].c_str());
        // A whole cTSLightingParserState (never made by the game: only the derived class is) registers no commands on either side
        // (cBaseParser's Initialise finds its once flag set); only the agreement is checked here, the derived object's below.
    }
    for (int round = 0; round < 4; ++round) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            ResetSettings(1);
            auto *o = static_cast<Derived *>(::operator new(Derived::kSize));
            uint8_t *const b = reinterpret_cast<uint8_t *>(o);
            std::memset(b, 0, Derived::kSize);
            gObj = b;
            if (side == 0) {
                // The constructor as the manager's Init has it inline (src/sg/cLightingManagerInit.cpp).
                Store(b + 8, GameAddress<void>(Derived::kVbTable));
                Store(b + 0x438, GameAddress<void>(Derived::kParserFaceVbTable));
                ctx.Original<ConstructFn>(0xb3ce70)(o, 0);
                Store(b, GameAddress<void>(Derived::kVTable));
                Store(b + 0x42c, GameAddress<void>(Derived::kUnknownVTable));
                Store(b + 0x434, GameAddress<void>(Derived::kParserVTable));
                Store<uint32_t>(b + 0x428, 0);
                Store<uint32_t>(b + 0x430, 0);
                Store<uint32_t>(b + 0x404, 0);
                o->mText.Construct();
            } else {
                o->Construct();
            }
            auto **pv = *reinterpret_cast<void ***>(b + 0x434);
            gLog.clear();
            std::string s = "init " + Hex(reinterpret_cast<InitFn>(pv[0])(b + 0x434, round & 1) & 0xff);
            s += " settings " + SettingsText(Load<void *>(b + 0x404)) + Dispatch(o, 0x42c, 0x434, round);
            auto **vt = *reinterpret_cast<void ***>(o);
            if (side == 0) ctx.Original<DeleteFn>(0x2cbc70)(o, 1);
            else reinterpret_cast<DeleteFn>(vt[0])(o, 1);
            got[side] = s + " after " + SettingsText(gMadeCount ? gMade[0] : nullptr) + "\n" + gLog;
            FreeMadeSettings();
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("derived round %d: %s", round, Diff(got[0], got[1]).c_str());
        if (round == 0 && got[0].find("init 1 settings made 0 refs 1 28 1 1 1 1 1 0") != 0) ctx.Fail("the original derived parser: %s", got[0].c_str());
    }
}
