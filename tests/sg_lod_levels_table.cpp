// Differential tests for nGZSceneGraph::cLODLevelsTable and its LOD command (lane QS; src/sg/cLODLevels.h,
// src/hook/tables/cLODLevels.cpp).
//
// The constructor (0xab6b00) and the factory (0xab6fa0) run cBaseParser's constructor for real on both sides (the image's
// 0xafd800 on the original's, ours on ours: verified equal by tests/gz_base_parser.cpp) and are compared as the parser
// Form (tests/gz_base_parser_form.h) of the whole 0x1cc bytes. The bodies of slots 0, 1 and 9 and the destructor stand in
// for the cBaseParser call they make (the image's body on the original's side, our member on ours, each logging the
// cBaseParser `this` as an offset in the table) and are reached through the image's vtordisp thunks and our forwarders.
// The LOD command runs on a table whose cIBaseParser face has a fake vtable (slot 32, EvalInt, answers from a list).
// The faces' shifted thunks are checked like tests/gz_parser_thunks.cpp checks cBaseParser's. Last, a whole table runs
// synthetic LOD text through the image's RunText: the original's table built and initialised by the image, ours by our code
// with our command body patched over the image's.
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gz_base_parser_form.h"
#include "shape.h"

#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "hook/hooks.h"
#include "rz/RZMemory.h"
#include "sg/cLODLevels.h"

#include <malloc.h>

#include <climits>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace cp = nGZCommandParser;
using cp::cBaseParser;
using cp::cBaseParserAdapters;
using nGZSceneGraph::cLODCommand;
using nGZSceneGraph::cLODLevelsTable;
using nGZSceneGraph::cLODLevelsTableAdapters;
using nGZSceneGraph::LODLevel;

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
const uint8_t *gTable = nullptr; // the table of the side running: `this` pointers are logged as offsets in it
uint32_t gBaseResult = 0;

std::string Offset(const void *p) { return Hex(P(p) - P(gTable)); }

// ---- a table for the body tests --------------------------------------------------------------------------------------
//
// 0x1cc bytes of junk with what the bodies read: the vbtable (+8, the game's), both vtordisps, a copy of the game's
// cIBaseParser face vtable (some slots replaced) at +0x1c4, and an std::vector of levels at +0x1ac. The block has 0x40
// bytes more (junk too): a face moved by a vtordisp has its vtordisp word past the table.

struct FaceVTable {
    void *slots[51];
    explicit FaceVTable(difftest::Context &ctx) { std::memcpy(slots, ctx.image.At(cLODLevelsTable::kParserVTable), sizeof(slots)); }
};

cLODLevelsTable *NewTable(std::mt19937 &g, const FaceVTable &face, const std::vector<LODLevel> &levels, size_t capacity)
{
    auto *t = static_cast<cLODLevelsTable *>(::operator new(cLODLevelsTable::kSize + 0x40));
    Junk(g, t, cLODLevelsTable::kSize + 0x40);
    *reinterpret_cast<const void **>(t->mBase + 8) = GameAddress<void>(cLODLevelsTable::kVbTable);
    t->mUnknownVtorDisp = 0;
    t->mParserVtorDisp = 0;
    t->mUnknownFaceVTable = GameAddress<void>(cLODLevelsTable::kUnknownVTable);
    t->mParserFaceVTable = face.slots;
    ::new (&t->mLevels) std::vector<LODLevel>();
    t->mLevels.reserve(capacity);
    t->mLevels.assign(levels.begin(), levels.end());
    return t;
}

void FreeTable(cLODLevelsTable *t)
{
    std::destroy_at(&t->mLevels);
    ::operator delete(t);
}

// Two tables made from the same junk (one generator state each).
struct Pair {
    cLODLevelsTable *t[2];
    Pair(uint32_t seed, const FaceVTable &face, const std::vector<LODLevel> &levels, size_t capacity)
    {
        for (int side = 0; side < 2; ++side) {
            std::mt19937 g(seed);
            t[side] = NewTable(g, face, levels, capacity);
        }
    }
    ~Pair()
    {
        FreeTable(t[0]);
        FreeTable(t[1]);
    }
};

// The table's bytes with the vector's three words replaced by its size, capacity and contents.
std::string TableBytes(const cLODLevelsTable *t)
{
    std::vector<uint8_t> bytes(reinterpret_cast<const uint8_t *>(t), reinterpret_cast<const uint8_t *>(t) + cLODLevelsTable::kSize);
    std::memset(bytes.data() + 0x1ac, 0, 12);
    std::string s(bytes.begin(), bytes.end());
    s += " size " + std::to_string(t->mLevels.size()) + " capacity " + std::to_string(t->mLevels.capacity()) + ":";
    for (const LODLevel &l : t->mLevels) s += " " + std::to_string(l.size) + "/" + std::to_string(l.level);
    return s;
}

std::string Levels(const cLODLevelsTable *t)
{
    std::string s = "size " + std::to_string(t->mLevels.size()) + " capacity " + std::to_string(t->mLevels.capacity()) + ":";
    for (const LODLevel &l : t->mLevels) s += " " + std::to_string(l.size) + "/" + std::to_string(l.level);
    return s;
}

std::vector<LODLevel> SomeLevels(std::mt19937 &g, size_t n)
{
    std::vector<LODLevel> v(n);
    for (LODLevel &l : v) l = {static_cast<int32_t>(g()), static_cast<int32_t>(g() % 40)};
    return v;
}

// ---- stand-ins for the cBaseParser calls ---------------------------------------------------------------------------
// The image's bodies take the face cBaseParser has (+0x1b8 of its part); ours are members on the part itself.

std::string Snapshot(const cBaseParser *parser)
{
    // What the body has done to the table by the time it calls the base: the levels.
    return " levels " + Levels(reinterpret_cast<const cLODLevelsTable *>(parser));
}

bool __fastcall ImageBaseInitialise(void *face, void *, uint32_t caseSensitive)
{
    const cBaseParser *p = cBaseParser::FromParserFace(face);
    gLog += "base Initialise this " + Offset(p) + " case " + Hex(caseSensitive & 0xff) + Snapshot(p) + "\n";
    return (gBaseResult & 1) != 0;
}
bool __fastcall OurBaseInitialise(cBaseParser *self, void *, uint32_t caseSensitive)
{
    gLog += "base Initialise this " + Offset(self) + " case " + Hex(caseSensitive & 0xff) + Snapshot(self) + "\n";
    return (gBaseResult & 1) != 0;
}
bool __fastcall ImageBaseShutdown(void *face, void *)
{
    const cBaseParser *p = cBaseParser::FromParserFace(face);
    gLog += "base Shutdown this " + Offset(p) + Snapshot(p) + "\n";
    return (gBaseResult & 1) != 0;
}
bool __fastcall OurBaseShutdown(cBaseParser *self, void *)
{
    gLog += "base Shutdown this " + Offset(self) + Snapshot(self) + "\n";
    return (gBaseResult & 1) != 0;
}
void __fastcall ImageBaseReset(void *face, void *)
{
    const cBaseParser *p = cBaseParser::FromParserFace(face);
    gLog += "base Reset this " + Offset(p) + Snapshot(p) + "\n";
}
void __fastcall OurBaseReset(cBaseParser *self, void *) { gLog += "base Reset this " + Offset(self) + Snapshot(self) + "\n"; }
// The destructor's base call (0xafe1b0) takes the part itself on both sides.
void __fastcall BaseDestruct(cBaseParser *self, void *)
{
    const auto *t = reinterpret_cast<const cLODLevelsTable *>(self);
    const uint32_t *w = reinterpret_cast<const uint32_t *>(t);
    gLog += "base Destruct this " + Offset(self) + " vtables " + Hex(w[0]) + " " + Hex(w[0x1bc / 4]) + " " + Hex(w[0x1c4 / 4]) +
            " vector words " + Hex(w[0x1ac / 4]) + " " + Hex(w[0x1b0 / 4]) + " " + Hex(w[0x1b4 / 4]) + "\n";
}

// Face slot 4 (RegisterCommand): the command by its words (+0xc, the state, is written by the parser at registration,
// not by the constructor), then freed.
void __fastcall FakeRegisterCommand(void *face, void *, void *command)
{
    gLog += "register on " + Offset(face);
    if (command) {
        const uint32_t *w = static_cast<const uint32_t *>(command);
        gLog += " vtable " + Hex(w[0] - P(GameAddress<void>(0))) + " parser " + Hex(w[1]) + " count " + Hex(w[2]) +
                " block " + std::to_string(_msize(command));
        ::operator delete(command);
    } else {
        gLog += " null";
    }
    gLog += "\n";
}

} // namespace

// 0xab6b00 (__thiscall, ret 4): the whole table (flag 1, and flags with other bits) and the table as a base (flag 0, the
// virtual bases' words then left as the caller had them, the vbtable at +8 the table's own). Compared: the result and
// the parser Form of all 0x1cc bytes (cBaseParser's heap members by what they hold, the vector words as bytes).
DIFF_TEST(LODLevelsTable_Construct, 0xab6b00)
{
    Allocators allocators;
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xab6b00);
    std::mt19937 g(0xab6b00);
    for (int round = 0; round < 40; ++round) {
        const uint32_t flag = round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() | 1u);
        alignas(16) uint8_t a[cLODLevelsTable::kSize], b[cLODLevelsTable::kSize];
        Junk(g, a, sizeof(a));
        if (!flag) {
            const uint32_t vb = P(GameAddress<void>(cLODLevelsTable::kVbTable));
            const uint32_t faceVb = P(GameAddress<void>(cLODLevelsTable::kParserFaceVbTable));
            std::memcpy(a + 8, &vb, 4);
            std::memcpy(a + 0x1c8, &faceVb, 4);
        }
        std::memcpy(b, a, sizeof(a));
        void *ra = orig(a, flag);
        void *rb = cLODLevelsTableAdapters::Construct(reinterpret_cast<cLODLevelsTable *>(b), nullptr, flag);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("round %d: returned %p / %p, expected this", round, ra, rb);
        const auto fa = parserform::ParserForm(a, sizeof(a)), fb = parserform::ParserForm(b, sizeof(b));
        if (fa != fb) ctx.Fail("round %d (flag %08x): the tables differ from byte %ld", round, flag, parserform::FirstDiff(fa, fb));
        // The layout on the original's side.
        const auto *ta = reinterpret_cast<const cLODLevelsTable *>(a);
        if (!ta->mLevels.empty() || ta->mLevels.capacity() != 0 || P(*reinterpret_cast<void *const *>(a)) != P(GameAddress<void>(cLODLevelsTable::kVTable)) ||
            P(ta->mParserFaceVTable) != P(GameAddress<void>(cLODLevelsTable::kParserVTable)) || ta->mParserVtorDisp != 0 || ta->mUnknownVtorDisp != 0)
            ctx.Fail("round %d: the original's table does not have the layout's fields", round);
        parserform::TearDownParser(*reinterpret_cast<cBaseParser *>(a));
        parserform::TearDownParser(*reinterpret_cast<cBaseParser *>(b));
    }
}

// 0xab6fa0 (__cdecl): the face returned (as an offset in its block) and the new table's Form (the bytes no constructor
// writes left out: the two blocks are different heap memory). Then each table freed by the original's deleting destructor
// with the base destructor stood in, so only the vector and the block go.
DIFF_TEST(LODLevelsTable_Create, 0xab6fa0)
{
    Allocators allocators;
    for (int round = 0; round < 10; ++round) {
        void *fa = ctx.Original<void *(__cdecl *)()>(0xab6fa0)();
        void *fb = cLODLevelsTable::Create();
        ++ctx.cases;
        if (!fa || !fb) {
            ctx.Fail("round %d: null face %p / %p", round, fa, fb);
            return;
        }
        auto *a = static_cast<uint8_t *>(fa) - 0x1bc, *b = static_cast<uint8_t *>(fb) - 0x1bc;
        if (_msize(a) != cLODLevelsTable::kSize || _msize(b) != cLODLevelsTable::kSize) ctx.Fail("round %d: blocks of %zu / %zu bytes", round, _msize(a), _msize(b));
        auto form = [](const uint8_t *t) {
            std::vector<uint8_t> f = parserform::ParserForm(t, cLODLevelsTable::kSize, true);
            return f;
        };
        const auto ga = form(a), gb = form(b);
        if (ga != gb) ctx.Fail("round %d: the tables differ from byte %ld", round, parserform::FirstDiff(ga, gb));
        for (uint8_t *t : {a, b}) {
            parserform::TearDownParser(*reinterpret_cast<cBaseParser *>(t));
            std::destroy_at(&reinterpret_cast<cLODLevelsTable *>(t)->mLevels);
            ::operator delete(t);
        }
    }
}

// Slot 0 of the cIBaseParser face (thunk 0xab7203 -> 0xab7210, ret 4): the case flag (with junk in the argument's upper
// bytes), the base's result 0 or 1, vtordisps 0 and others. Logged: the base call (its `this`, flag), the command
// registered through face slot 4 (its words; the block's size), the result.
DIFF_TEST(LODLevelsTable_Initialise, 0xab7210)
{
    Allocators allocators;
    FaceVTable face(ctx);
    face.slots[4] = reinterpret_cast<void *>(&FakeRegisterCommand);
    shape::CodePatch imageBase(0xb02880u, shape::CodeOf(&ImageBaseInitialise));
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::Initialise), shape::CodeOf(&OurBaseInitialise));
    using Fn = uint32_t(__fastcall *)(void *, void *, uint32_t);
    const Fn sides[2] = {ctx.Original<Fn>(0xab7203), reinterpret_cast<Fn>(&nGZCommandParser::VtordispThunk<&cLODLevelsTableAdapters::Initialise>::Call)};
    std::mt19937 g(0xab7210);
    for (int round = 0; round < 40; ++round) {
        const int32_t vtordisp = round % 4 == 3 ? 0xc + 4 * static_cast<int32_t>(g() % 12) : 0;
        const uint32_t arg = (g() & 0xffffff00u) | (round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() & 0xff));
        gBaseResult = g() & 1;
        Pair pair(g(), face, SomeLevels(g, round % 5), 8);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cLODLevelsTable *t = pair.t[side];
            gTable = reinterpret_cast<const uint8_t *>(t);
            // The face moved by the vtordisp (the thunk takes it off again).
            uint8_t *faceAt = reinterpret_cast<uint8_t *>(t) + 0x1c4 + vtordisp;
            alignas(4) uint8_t saved[8];
            std::memcpy(saved, faceAt - 4, 8);
            std::memcpy(faceAt - 4, &vtordisp, 4);
            gLog.clear();
            const uint32_t r = sides[side](faceAt, nullptr, arg) & 0xff;
            std::memcpy(faceAt - 4, saved, 8);
            got[side] = gLog + "-> " + Hex(r) + "\n" + TableBytes(t);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d arg %08x base %u):\n%s\n----\n%s", round, vtordisp, arg, gBaseResult, got[0].c_str(), got[1].c_str());
    }
}

// Slots 1 (thunk 0xab794c -> 0xab7960) and 9 (thunk 0xab6cfd -> 0xab6d10): levels of 0..40 rows with spare capacity or
// none, the base's result 0 or 1. Logged: the base call and the levels when it is made, the result (slot 1), then the
// table's bytes, the vector's size, capacity and rows.
template <bool IsShutdown> void RunEmptying(difftest::Context &ctx, uint32_t thunk, uint32_t seed)
{
    Allocators allocators;
    FaceVTable face(ctx);
    shape::CodePatch imageBase(IsShutdown ? 0xb06760u : 0xaff120u,
                               IsShutdown ? shape::CodeOf(&ImageBaseShutdown) : shape::CodeOf(&ImageBaseReset));
    shape::CodePatch ourBase(IsShutdown ? shape::MemberCode(&cBaseParser::Shutdown) : shape::MemberCode(&cBaseParser::Reset),
                             IsShutdown ? shape::CodeOf(&OurBaseShutdown) : shape::CodeOf(&OurBaseReset));
    using Fn = uint32_t(__fastcall *)(void *, void *);
    Fn ours;
    if constexpr (IsShutdown) ours = reinterpret_cast<Fn>(&nGZCommandParser::VtordispThunk<&cLODLevelsTableAdapters::Shutdown>::Call);
    else ours = reinterpret_cast<Fn>(&nGZCommandParser::VtordispThunk<&cLODLevelsTableAdapters::Reset>::Call);
    const Fn sides[2] = {ctx.Original<Fn>(thunk), ours};
    std::mt19937 g(seed);
    for (int round = 0; round < 60; ++round) {
        const int32_t vtordisp = round % 4 == 3 ? 0xc + 4 * static_cast<int32_t>(g() % 12) : 0;
        const size_t n = round < 10 ? static_cast<size_t>(round) : g() % 41;
        const size_t capacity = n + (round % 2 ? g() % 9 : 0);
        gBaseResult = g() & 1;
        Pair pair(g(), face, SomeLevels(g, n), capacity);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cLODLevelsTable *t = pair.t[side];
            const LODLevel *data = t->mLevels.data();
            gTable = reinterpret_cast<const uint8_t *>(t);
            uint8_t *faceAt = reinterpret_cast<uint8_t *>(t) + 0x1c4 + vtordisp;
            alignas(4) uint8_t saved[8];
            std::memcpy(saved, faceAt - 4, 8);
            std::memcpy(faceAt - 4, &vtordisp, 4);
            gLog.clear();
            const uint32_t r = sides[side](faceAt, nullptr);
            std::memcpy(faceAt - 4, saved, 8);
            got[side] = gLog + (IsShutdown ? "-> " + Hex(r & 0xff) : std::string("-> void")) + (t->mLevels.data() == data ? " same storage\n" : " moved\n") + TableBytes(t);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d n %zu capacity %zu base %u):\n%s\n----\n%s", round, vtordisp, n, capacity, gBaseResult, got[0].c_str(), got[1].c_str());
    }
}

DIFF_TEST(LODLevelsTable_Shutdown, 0xab7960) { RunEmptying<true>(ctx, 0xab794c, 0xab7960); }
DIFF_TEST(LODLevelsTable_Reset, 0xab6d10) { RunEmptying<false>(ctx, 0xab6cfd, 0xab6d10); }

// Primary slot 0 (0xab6c50, ret 4): flags 0, 1 and others; levels 0..20 rows. The base destructor (0xafe1b0) stood in on
// both sides logs the vtables and the vector's words when it is called. Compared: the result, the log, which blocks were
// freed (the storage, the table), and with the table kept, its bytes afterwards.
DIFF_TEST(LODLevelsTable_DeletingDestructor, 0xab6c50)
{
    Allocators allocators;
    FaceVTable face(ctx);
    shape::StandIn base(0xafe1b0u, &cBaseParser::Destruct, &BaseDestruct);
    using Fn = void *(__fastcall *)(void *, void *, uint32_t);
    const Fn sides[2] = {ctx.Original<Fn>(0xab6c50), reinterpret_cast<Fn>(&cLODLevelsTableAdapters::DeletingDestructor)};
    std::mt19937 g(0xab6c50);
    for (int round = 0; round < 40; ++round) {
        const uint32_t flags = round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : g();
        const size_t n = round % 7 == 0 ? 0 : g() % 21;
        const uint32_t seed = g();
        const std::vector<LODLevel> levels = SomeLevels(g, n);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 tg(seed);
            cLODLevelsTable *t = NewTable(tg, face, levels, n);
            const LODLevel *data = t->mLevels.data();
            gTable = reinterpret_cast<const uint8_t *>(t);
            gLog.clear();
            void *r;
            bool dataFreed, tableFreed;
            {
                difftest::FreeRecorder frees;
                r = sides[side](t, nullptr, flags);
                frees.Stop();
                dataFreed = data && frees.Freed(data);
                tableFreed = frees.Freed(t);
            }
            got[side] = gLog + "-> " + Offset(r) + " storage freed " + std::to_string(dataFreed) + " table freed " + std::to_string(tableFreed);
            if (!tableFreed) {
                got[side] += "\n" + std::string(reinterpret_cast<const char *>(t), cLODLevelsTable::kSize);
                ::operator delete(t);
            }
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (flags %08x n %zu):\n%s\n----\n%s", round, flags, n, got[0].c_str(), got[1].c_str());
    }
}

namespace {

// ---- the LOD command ------------------------------------------------------------------------------------------------

// The argument list: slot 15 prepares, slot 11 hands out the texts.
const char *gArgv[2];
void __fastcall ArgsPrepare(void *, void *) { gLog += "args prepare\n"; }
const char *const *__fastcall ArgsGet(void *, void *, uint32_t count)
{
    gLog += "args get " + Hex(count) + "\n";
    return gArgv;
}
void *gArgsVTable[16];
struct FakeArgs {
    void **vtable = gArgsVTable;
};

// EvalInt (face slot 32): the text logged, its value from the list (by the text, so the order of the calls shows).
std::vector<std::pair<std::string, int32_t>> gValues;
int32_t __fastcall FakeEvalInt(void *face, void *, const char *text)
{
    gLog += "EvalInt on " + Offset(face) + " '" + text + "'\n";
    for (const auto &[t, v] : gValues)
        if (t == text) return v;
    return -12345;
}

} // namespace

// Slot 4 of the command (0xab6e90, ret 4): levels from a list with the edges (negative, 29, 30, 31, INT_MIN, INT_MAX)
// and random ones, sizes random, appended to vectors of 0..40 rows with spare capacity or none (so the storage grows by
// the toolchain's rule, the game's operator new being ours on both sides). Compared: the log, the table's bytes, size,
// capacity and rows.
DIFF_TEST(LODLevelsTable_CommandExecute, 0xab6e90)
{
    Allocators allocators;
    FaceVTable face(ctx);
    face.slots[32] = reinterpret_cast<void *>(&FakeEvalInt);
    for (int i = 0; i < 16; ++i) gArgsVTable[i] = reinterpret_cast<void *>(&ArgsPrepare);
    gArgsVTable[11] = reinterpret_cast<void *>(&ArgsGet);
    using Fn = void(__fastcall *)(void *, void *, void *);
    const Fn sides[2] = {ctx.Original<Fn>(0xab6e90), reinterpret_cast<Fn>(&cLODLevelsTableAdapters::Execute)};
    static const int32_t kEdges[] = {0, 1, -1, 29, 30, 31, 100, INT_MIN, INT_MAX, -30};
    std::mt19937 g(0xab6e90);
    for (int round = 0; round < 300; ++round) {
        const size_t n = round < 20 ? static_cast<size_t>(round) : g() % 41;
        const size_t capacity = n + (round % 2 ? g() % 5 : 0);
        const int32_t level = round < 40 ? kEdges[round % std::size(kEdges)] : static_cast<int32_t>(g() % 80) - 20;
        const int32_t size = round % 9 == 0 ? level : static_cast<int32_t>(g());
        gValues = {{"lvl", level}, {"sz", size}};
        gArgv[0] = "lvl";
        gArgv[1] = "sz";
        Pair pair(g(), face, SomeLevels(g, n), capacity);
        const uint32_t refCount = g();
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            cLODLevelsTable *t = pair.t[side];
            gTable = reinterpret_cast<const uint8_t *>(t);
            cLODCommand command{GameAddress<void>(cLODCommand::kVTable), nullptr, static_cast<int32_t>(refCount), t};
            FakeArgs args;
            gLog.clear();
            sides[side](&command, nullptr, &args);
            got[side] = gLog + TableBytes(t) + " command " + Hex(P(command.mParser)) + " " + Hex(static_cast<uint32_t>(command.mRefCount)) + " " + Offset(command.mTable);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (level %d size %d n %zu):\n%s\n----\n%s", round, level, size, n, got[0].c_str(), got[1].c_str());
    }
}

// Slot 5 of the command (0xab7320): the name.
DIFF_TEST(LODLevelsTable_CommandGetName, 0xab7320)
{
    using Fn = const char *(__fastcall *)(void *, void *);
    alignas(4) uint8_t junk[16] = {};
    const char *a = ctx.Original<Fn>(0xab7320)(junk, nullptr);
    const char *b = reinterpret_cast<Fn>(&cLODLevelsTableAdapters::GetName)(junk, nullptr);
    ++ctx.cases;
    if (a != b) ctx.Fail("name %p / %p", static_cast<const void *>(a), static_cast<const void *>(b));
    if (!a || std::strcmp(a, "LOD") != 0) ctx.Fail("the original's name is not \"LOD\"");
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
int32_t Vtordisp(std::mt19937 &g, int i) { return i < 8 ? kFixedVtordisps[i] : static_cast<int32_t>(g() % 129) * 4 - 0x100; }

// A thunk `sub ecx, [ecx-4]; sub ecx, 0xc; jmp body` whose body is one of cBaseParser's (its registered hook the adapter):
// the image's thunk and our hook of it called on a face with a vtordisp, the body stood in for on both sides.
template <auto Adapter> struct Check;
template <class R, class... Args, R(__fastcall *Adapter)(void *, void *, Args...)> struct Check<Adapter> {
    static void Run(difftest::Context &ctx, uint32_t thunk, const char *name, std::mt19937 &g)
    {
        using Fn = R(__fastcall *)(void *, void *, Args...);
        const uint8_t *code = ctx.image.At<uint8_t>(thunk);
        static const uint8_t kPrefix[] = {0x2b, 0x49, 0xfc, 0x83, 0xe9, 0x0c, 0xe9};
        if (std::memcmp(code, kPrefix, sizeof(kPrefix)) != 0) {
            ctx.Fail("%s %x: not a shifted vtordisp thunk", name, thunk);
            return;
        }
        int32_t rel;
        std::memcpy(&rel, code + 7, 4);
        const uint32_t body = thunk + 11 + rel;
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
            const int32_t d = Vtordisp(g, i);
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
            if (got[0] != got[1]) ctx.Fail("%s %x vtordisp %d: %s / %s", name, thunk, d, got[0].c_str(), got[1].c_str());
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
    const HookSpec *b = HookAt(body);
    if (!b) ctx.Fail("%s %x: the body %x is not hooked", name, thunk, body);
}

} // namespace

// Every shifted thunk of the two faces (+0x1bc: 0xe767d0 slots 0..2; +0x1c4: 0xe767e0, all but 0, 1, 9 and the folded
// 11, 45; 33 shares 32's), each with its cBaseParser body's adapter; the three own thunks (slots 0, 1, 9: their bodies
// and forwarders); slots 11 and 45 (shifted thunks into folded bodies: a bare `ret` and the version word) called on
// random buffers.
DIFF_TEST(LODLevelsTable_FaceThunks, 0xab7448)
{
    std::mt19937 g(0xab7448);
#define X(rva, adapter) Check<&cBaseParserAdapters::adapter>::Run(ctx, rva, #adapter, g);
    X(0xab7448, QueryInterface)
    X(0xab6cd2, AddRef)
    X(0xab7765, Release)
    X(0xab77ce, SetVersionRange)
    X(0xab77e4, SetContextState)
    X(0xab774f, RegisterCommand)
    X(0xab79b1, UnregisterCommand)
    X(0xab79a6, UnregisterCommandObject)
    X(0xab775a, RegisterMetaCommand)
    X(0xab7770, RestartViaReset)
    X(0xab7331, ResetBlocks)
    X(0xab7398, RunFile)
    X(0xab73c4, RunFiles)
    X(0xab73b9, RunText)
    X(0xab738d, RunLine)
    X(0xab7985, PushOpenBlock)
    X(0xab7990, PushMetaBlock)
    X(0xab6e76, PopMetaBlock)
    X(0xab7805, RunExpandedText)
    X(0xab743d, PushNamespace)
    X(0xab7427, PopNamespace)
    X(0xab7432, PurgeNamespace)
    X(0xab6f2f, GetFunctions)
    X(0xab77ef, SetOutput)
    X(0xab77fa, SetHandler)
    X(0xab77d9, SetSource)
    X(0xab79bc, FindVariable)
    X(0xab6d49, GetNamespacePath)
    X(0xab799b, ExpandVariables)
    X(0xab7382, EvalBool)
    X(0xab73cf, EvalFloat)
    X(0xab73da, EvalInt)
    X(0xab7406, EvalVector2)
    X(0xab7411, EvalVector3)
    X(0xab741c, EvalVector4)
    X(0xab73e5, EvalColorRGBA)
    X(0xab73f0, EvalColorARGB)
    X(0xab73fb, EvalColorRGB)
    X(0xab7326, CommandCount)
    X(0xab6d28, CommandAt)
    X(0xab72a0, IsInitialised)
    X(0xab7295, IsCaseSensitive)
    X(0xab7314, GetText)
    X(0xab73ae, GetFileName)
    X(0xab73a3, GetLine)
    X(0xab6d54, HasDefinition)
    X(0xab6d33, HasCommand)
    X(0xab797a, GetSource)
#undef X
    CheckOwnThunk(ctx, 0xab7203, 0xab7210, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cLODLevelsTableAdapters::Initialise>::Call), "Initialise");
    CheckOwnThunk(ctx, 0xab794c, 0xab7960, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cLODLevelsTableAdapters::Shutdown>::Call), "Shutdown");
    CheckOwnThunk(ctx, 0xab6cfd, 0xab6d10, reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<&cLODLevelsTableAdapters::Reset>::Call), "Reset");

    // Slots 11 and 45: no stand-in (the folded bodies are run), the buffer's words random.
    const HookSpec *noOp = HookAt(0xab6d3e), *version = HookAt(0xab79c7);
    if (!noOp || !version) {
        ctx.Fail("slots 11 / 45 not hooked");
        return;
    }
    for (int i = 0; i < 64; ++i) {
        Junk(g, gBuffer, sizeof(gBuffer));
        const int32_t d = Vtordisp(g, i % 24);
        uint8_t *face = gBuffer + 0x200;
        std::memcpy(face - 4, &d, 4);
        using VersionFn = int32_t(__fastcall *)(void *, void *);
        const int32_t a = ctx.Original<VersionFn>(0xab79c7)(face, nullptr);
        const int32_t b = reinterpret_cast<VersionFn>(version->replacement)(face, nullptr);
        using NoOpFn = void(__fastcall *)(void *, void *);
        std::vector<uint8_t> before(gBuffer, gBuffer + sizeof(gBuffer));
        ctx.Original<NoOpFn>(0xab6d3e)(face, nullptr);
        reinterpret_cast<NoOpFn>(noOp->replacement)(face, nullptr);
        ++ctx.cases;
        if (a != b) ctx.Fail("slot 45 vtordisp %d: %d / %d", d, a, b);
        if (std::memcmp(before.data(), gBuffer, sizeof(gBuffer)) != 0) ctx.Fail("slot 11 wrote to the buffer");
    }
}

namespace {

// The allocator service (cache 0x10c95d4, guard 0x10c95d8 as after its first use; tests/gz_parser_run_text.cpp's).
uint32_t __fastcall ServiceNop(void *, void *) { return 0; }
void *__fastcall ServiceAlloc(void *, void *, uint32_t n) { return std::malloc(n ? n : 1); }
void __fastcall ServiceFree(void *, void *, void *p) { std::free(p); }
void *gServiceVTable[5] = {reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop), reinterpret_cast<void *>(&ServiceNop),
                           reinterpret_cast<void *>(&ServiceAlloc), reinterpret_cast<void *>(&ServiceFree)};
void *gServiceObject = gServiceVTable;

struct ServiceFake {
    void **cache;
    int32_t *guard;
    void *savedCache;
    int32_t savedGuard;
    explicit ServiceFake(difftest::Context &ctx)
        : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int32_t>(0x10c95d8)), savedCache(*cache), savedGuard(*guard)
    {
        *cache = &gServiceObject;
        *guard = INT32_MIN;
    }
    ~ServiceFake()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

// The RZ heap the parser's line and argument code allocates from (tests/gz_parser_run.cpp's stand-ins).
void *__cdecl TestRzMalloc(uint32_t bytes) { return std::malloc(bytes ? bytes : 1); }
void __cdecl TestRzFree(void *p) { std::free(p); }
struct RzHeap {
    shape::StandIn rzMalloc{0x1be3b0, &RZMemory::Malloc, &TestRzMalloc};
    shape::StandIn rzFree{0x1be3a1, &RZMemory::Free, &TestRzFree};
};

} // namespace

// A whole table running synthetic LOD text: 1..30 lines "LOD <level> <size>" (levels -5..40, sizes 0..2000, some written
// with a leading zero), on the original's side a table built (0xab6b00), initialised (thunk 0xab7203) and run by the
// image; on ours built and initialised by our code with our command body patched over the image's 0xab6e90. The text
// runs through the game's face vtable (slot 14, the image's RunText and evaluator on both sides). Compared: the rows and
// the command count (face slot 40); each table then destroyed by its own side's deleting destructor.
DIFF_TEST(LODLevelsTable_RunText, 0xab6e90)
{
    Allocators allocators;
    ServiceFake service(ctx);
    RzHeap heap;
    std::mt19937 g(0x10d);
    using RunTextFn = void(__thiscall *)(void *, char *, uint32_t, const char *);
    using CountFn = uint32_t(__thiscall *)(void *);
    for (int round = 0; round < 60; ++round) {
        std::string text;
        const int lines = 1 + static_cast<int>(g() % 30);
        for (int i = 0; i < lines; ++i) {
            const int level = static_cast<int>(g() % 46) - 5;
            const int size = static_cast<int>(g() % 2001);
            text += "LOD " + std::to_string(level) + " " + (g() % 5 == 0 ? "0" : "") + std::to_string(size) + "\n";
        }
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            auto *t = static_cast<cLODLevelsTable *>(::operator new(cLODLevelsTable::kSize));
            void *face;
            if (side == 0) {
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xab6b00)(t, 1);
                face = reinterpret_cast<uint8_t *>(t) + 0x1c4;
                ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xab7203)(face, 1);
            } else {
                t->Construct(1);
                face = t->ParserFace();
                t->Initialise(1);
            }
            std::vector<char> buffer(text.begin(), text.end());
            buffer.push_back(0);
            {
                std::unique_ptr<shape::CodePatch> ours;
                if (side == 1) ours = std::make_unique<shape::CodePatch>(0xab6e90u, shape::CodeOf(&cLODLevelsTableAdapters::Execute));
                const auto run = (*reinterpret_cast<RunTextFn *const *>(face))[14];
                run(face, buffer.data(), static_cast<uint32_t>(buffer.size() - 1), "lod.txt");
            }
            const uint32_t commands = (*reinterpret_cast<CountFn *const *>(face))[40](face);
            got[side] = Levels(t) + " commands " + std::to_string(commands);
            if (side == 0) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xab6c50)(t, 1);
            else t->DeletingDestructor(1);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d:\n%s\n%s\n----\n%s", round, text.c_str(), got[0].c_str(), got[1].c_str());
        if (round == 0 && got[0].find("size 0 ") == 0) ctx.Fail("the original's table took no rows:\n%s", text.c_str());
    }
}
