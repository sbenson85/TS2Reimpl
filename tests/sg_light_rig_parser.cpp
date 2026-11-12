// Differential tests for nTSSG::cLightRigAndCameraParserState and its five commands (lane 261109d, scenegraph LG7g;
// src/sg/cLightRigAndCameraParserState.h, src/hook/tables/cLightRigAndCameraParserState.cpp).
//
// The state is tested as tests/ts_effects_parser.cpp tests cTSEffectsParser (the same shape): the constructor runs cBaseParser's
// constructor for real on both sides and is compared as the parser Form of the whole object (the map's head pointer blanked, its
// head node compared by its words); the destructors, Initialise and ResetBlocks run on objects built of the same junk and random
// members on both sides, with cBaseParser's own bodies they call stood in for (the image's on the original's side, our member on
// ours, each logging); the faces' thunks are checked against their hooks; a whole parser built and initialised by the image is
// compared with ours by its registered commands; our vtables are compared with the hooks of the image's entries.
//
// The commands and AddRig run on a world of real containers built twice from one seed: the state (its strings and animation map
// random, its cIBaseParser face a logging fake reached through the object's own virtual base table, which is the game's or one
// that moves the face), a manager whose rig map holds random records (some named as the state's rig), and a fake argument list.
// Compared: the calls the command made (logged), what it threw, the state's members and raw bytes, and the manager's records.
#define NOMINMAX
#include "code_patch.h"
#include "command_parser.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gz_base_parser_form.h"
#include "shape.h"

#include "gz/CommandParser.h"
#include "gz/cBaseParser.h"
#include "gz/cParserThunks.h"
#include "hook/hooks.h"
#include "sg/cLightRigAndCameraParserState.h"
#include "sg/cLightRigManager.h"

#include <malloc.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
using nTSSG::cLightRigAndCameraParserState;
using nTSSG::cLightRigAndCameraParserStateAdapters;
using nTSSG::cLightRigManager;
using nTSSG::LightRig;
using State = cLightRigAndCameraParserState;

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

uint32_t ImageBase() { return P(GameAddress<void>(0)); }
bool InImage(uint32_t p) { return p - ImageBase() < 0x1800000u; }
std::string WordText(uint32_t w) { return InImage(w) ? "i" + Hex(w - ImageBase()) : Hex(w); }

// The texts the members, arguments and keys are drawn from: empty, short, the same name in two cases, one past the inline
// buffer and one of 5000 characters (a block of 4 KB and more).
const std::string &Long5000()
{
    static const std::string s = [] {
        std::string t(5000, 'x');
        for (size_t i = 0; i < t.size(); ++i) t[i] = static_cast<char>('a' + i % 23);
        return t;
    }();
    return s;
}
const char *PoolText(uint32_t i)
{
    static const char *const kPool[] = {"", "a", "rigA", "rigB", "RigA", "a rig name longer than the inline buffer"};
    i %= 7;
    return i < 6 ? kPool[i] : Long5000().c_str();
}

void FillText(std::string &s, std::mt19937 &g)
{
    if (g() % 3 == 0) {
        s = PoolText(g());
        return;
    }
    static const size_t kLens[] = {0, 3, 15, 16, 33, 100};
    const size_t len = kLens[g() % 6];
    const size_t cap = g() % 5 == 0 ? 5000 : len;
    s.assign(cap, static_cast<char>('a' + g() % 26));
    s.resize(len);
    for (size_t i = 0; i < len; ++i) s[i] = static_cast<char>('a' + g() % 26);
}

void FillAnimations(nTSSG::AnimationMap &m, std::mt19937 &g)
{
    for (size_t i = g() % 5; i > 0; --i) {
        std::string value;
        FillText(value, g);
        m.emplace(PoolText(g()), value);
    }
}

void DescribeText(std::string &s, const std::string &t)
{
    s += " text " + std::to_string(t.size()) + "/" + std::to_string(t.capacity()) + ":";
    for (size_t i = 0; i <= t.size(); ++i) s += " " + Hex(static_cast<uint8_t>(t.data()[i]));
}

// A tree container's head node: its words as "self" or "node", the colour/nil bytes and the size; then each node's colour/nil
// bytes and contents.
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
template <class V> const uint8_t *NodeOf(const V &kv) { return reinterpret_cast<const uint8_t *>(&kv) - 0x10; }

void DescribeAnimations(std::string &s, const nTSSG::AnimationMap &m)
{
    DescribeHead(s, m);
    for (const auto &kv : m) {
        const uint8_t *node = NodeOf(kv);
        s += " [" + Hex(node[0xc]) + "/" + Hex(node[0xd]);
        DescribeText(s, kv.first);
        DescribeText(s, kv.second);
        s += "]";
    }
}

// The state's bytes (the strings and the map blanked), then its strings and map.
std::string Describe(const State *o)
{
    std::vector<uint8_t> raw(reinterpret_cast<const uint8_t *>(o), reinterpret_cast<const uint8_t *>(o) + State::kSize);
    std::memset(raw.data() + 0x1b0, 0, 0x78); // the five strings
    std::memset(raw.data() + 0x22c, 0, 8);    // the map
    std::string s;
    for (uint8_t b : raw) s += Hex(b) + " ";
    s += "\ntexts";
    DescribeText(s, o->mRigName);
    DescribeText(s, o->mResourceName);
    DescribeText(s, o->mRootTag);
    DescribeText(s, o->mText1f8);
    DescribeText(s, o->mAutoRigName);
    s += "\nanimations";
    DescribeAnimations(s, o->mAnimations);
    return s;
}

void FillMembers(State *o, std::mt19937 &g)
{
    o->mManager = reinterpret_cast<cLightRigManager *>(static_cast<uintptr_t>(g()));
    std::string *texts[] = {&o->mRigName, &o->mResourceName, &o->mRootTag, &o->mText1f8, &o->mAutoRigName};
    for (std::string *t : texts) {
        ::new (t) std::string();
        FillText(*t, g);
    }
    if (g() % 3 == 0) o->mRigName.clear();
    o->mAutoRig = static_cast<bool>(g() & 1);
    ::new (&o->mAnimations) nTSSG::AnimationMap();
    FillAnimations(o->mAnimations, g);
}

void DestroyMembers(State *o)
{
    std::destroy_at(&o->mAnimations);
    std::destroy_at(&o->mAutoRigName);
    std::destroy_at(&o->mText1f8);
    std::destroy_at(&o->mRootTag);
    std::destroy_at(&o->mResourceName);
    std::destroy_at(&o->mRigName);
}

// ---- an object for the body tests --------------------------------------------------------------------------------------------
// kSize + 0x80 bytes of junk with what the bodies read: the vbtable (+8, the game's), the vtordisps zero, the faces' vtables (the
// cIBaseParser one a copy of the game's with some slots replaced), and random members.

struct FaceVTable {
    void *slots[51];
    explicit FaceVTable(difftest::Context &ctx) { std::memcpy(slots, ctx.image.At(State::kParserVTable), sizeof(slots)); }
};

State *NewObject(std::mt19937 &g, const FaceVTable *face)
{
    auto *o = static_cast<State *>(::operator new(State::kSize + 0x80));
    Junk(g, o, State::kSize + 0x80);
    *reinterpret_cast<const void **>(o->mBase + 8) = GameAddress<void>(State::kVbTable);
    o->mUnknownVtorDisp = 0;
    o->mParserVtorDisp = 0;
    o->mUnknownFaceVTable = GameAddress<void>(State::kUnknownVTable);
    o->mParserFaceVTable = face ? static_cast<const void *>(face->slots) : GameAddress<void>(State::kParserVTable);
    o->mFace2VbTable = GameAddress<const int32_t>(State::kFace2VbTable);
    FillMembers(o, g);
    return o;
}

void FreeObject(State *o)
{
    DestroyMembers(o);
    ::operator delete(o);
}

// Two objects made from the same junk and members (one generator state each).
struct Pair {
    State *o[2];
    explicit Pair(uint32_t seed, const FaceVTable *face = nullptr)
    {
        for (int side = 0; side < 2; ++side) {
            std::mt19937 g(seed);
            o[side] = NewObject(g, face);
        }
    }
    ~Pair()
    {
        FreeObject(o[0]);
        FreeObject(o[1]);
    }
};

// ---- stand-ins for the cBaseParser calls -------------------------------------------------------------------------------------
// The image's bodies take the face cBaseParser has (+0x1b8 of its part); ours are members on the part itself.

const State *AsObject(const cBaseParser *p) { return reinterpret_cast<const State *>(p); }

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
// The destructor's base call (0xafe1b0) takes the part itself on both sides.
void __fastcall BaseDestruct(cBaseParser *self, void *)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(self);
    // Ours are named by the image's table they stand for (the original's object holds the image's, ours holds our tables).
    auto vt = [&](size_t off, const void *own, uint32_t image) { return Hex(w[off / 4] == P(own) ? P(GameAddress<void>(image)) : w[off / 4]); };
    gLog += "base Destruct this " + Offset(self) + " vtables " + vt(0, State::kOwnVTable, State::kVTable) + " " +
            vt(0x238, State::kOwnUnknownVTable, State::kUnknownVTable) + " " + vt(0x240, State::kOwnParserVTable, State::kParserVTable) +
            " vtordisps " + Hex(w[0x234 / 4]) + " " + Hex(w[0x23c / 4]) + "\n";
}

// Face slot 2 (SetVersionRange) and 4 (RegisterCommand): logged; the commands freed.
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
        // The words the constructors write (cCommand's: the vtable, parser and count; cBlockCommandBase's also the value).
        gLog += " vtable " + WordText(w[0]) + " parser " + Hex(w[1]) + " count " + Hex(w[2]) + " block " + std::to_string(size);
        if (size >= 0x44) {
            gLog += " value " + Hex(w[3]);
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

// 0x2c1800 (__thiscall, ret 8): the whole parser (flag 1, and flags with other bits) and the parser as a base (flag 0, the virtual
// bases' words then left as the caller had them), with random manager pointers. Compared: the result and the parser Form of the
// whole object with the map's head pointer blanked and its head node compared by its words.
DIFF_TEST(LightRigParser_Construct, 0x2c1800)
{
    Allocators allocators;
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0x2c1800);
    std::mt19937 g(0x2c1800);
    const size_t kHeads[] = {0x22c};
    for (int round = 0; round < 40; ++round) {
        const uint32_t flag = round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() | 1u);
        auto *manager = reinterpret_cast<cLightRigManager *>(static_cast<uintptr_t>(round % 5 == 0 ? 0u : g()));
        alignas(16) uint8_t a[State::kSize], b[State::kSize];
        Junk(g, a, sizeof(a));
        if (!flag) {
            const uint32_t vb = P(GameAddress<void>(State::kVbTable));
            const uint32_t face2 = P(GameAddress<void>(State::kFace2VbTable));
            std::memcpy(a + 8, &vb, 4);
            std::memcpy(a + 0x244, &face2, 4);
        }
        std::memcpy(b, a, sizeof(a));
        void *ra = orig(a, manager, flag);
        void *rb = cLightRigAndCameraParserStateAdapters::Construct(reinterpret_cast<State *>(b), nullptr, manager, flag);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("round %d: returned %p / %p, expected this", round, ra, rb);
        std::string heads[2];
        std::vector<uint8_t> forms[2];
        uint8_t *objects[2] = {a, b};
        for (int side = 0; side < 2; ++side) {
            std::vector<uint8_t> copy(objects[side], objects[side] + State::kSize);
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
            // The vtable and virtual base table words differ by design (the original's in the image, ours in our tables): each
            // side's checked against its own, then blanked.
            struct Word { size_t off; const void *original, *ours; };
            const Word words[] = {
                {0x000, GameAddress<void>(State::kVTable), State::kOwnVTable},
                {0x238, GameAddress<void>(State::kUnknownVTable), State::kOwnUnknownVTable},
                {0x240, GameAddress<void>(State::kParserVTable), State::kOwnParserVTable},
                {0x008, GameAddress<void>(State::kVbTable), State::kOwnVbTable},
                {0x244, GameAddress<void>(State::kFace2VbTable), State::kOwnFace2VbTable},
            };
            for (const Word &w : words) {
                // Flag 0 leaves the virtual base table words as the caller had them (the image's on both sides).
                const bool vbWord = w.off == 0x008 || w.off == 0x244;
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
        const auto *oa = reinterpret_cast<const State *>(a);
        if (heads[0].find("self self") == std::string::npos || oa->mManager != manager || oa->mRigName.capacity() != 15)
            ctx.Fail("round %d: the original's object does not have the layout's fields (%s)", round, heads[0].c_str());
        for (uint8_t *object : objects) {
            for (size_t off : kHeads) ::operator delete(const_cast<uint8_t *>(parserform::Ptr(parserform::Word(object, off))));
            parserform::TearDownParser(*reinterpret_cast<cBaseParser *>(object));
        }
    }
}

namespace {

// The members for the destructor tests: both sides' objects destroyed by the side's destructor (the base destructor, 0xafe1b0,
// stood in for on both), compared by the log (the base call), the sizes of the blocks freed and whether the object itself was
// freed.
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
            State *o = NewObject(og, nullptr);
            // The words the destructor stores again start as junk (the primary vtable word is already), so a store left out
            // shows in the base call's log.
            o->mUnknownFaceVTable = reinterpret_cast<const void *>(static_cast<uintptr_t>(og()));
            o->mParserFaceVTable = reinterpret_cast<const void *>(static_cast<uintptr_t>(og()));
            o->mUnknownVtorDisp = static_cast<int32_t>(og());
            o->mParserVtorDisp = static_cast<int32_t>(og());
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

// 0x2c1cb0 (__thiscall, plain ret): the vtables and vtordisps stored again, the members destroyed in the original's order, the
// base destructor. The object is not freed.
DIFF_TEST(LightRigParser_Destruct, 0x2c1cb0)
{
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x2c1cb0);
    RunDestroy(ctx, 0x2c1cb0, [&](int side, State *o, uint32_t) -> void * {
        if (side == 0) orig(o);
        else cLightRigAndCameraParserStateAdapters::Destruct(o, nullptr);
        return o;
    });
}

// Primary slot 0 (0x2c2130, ret 4): flags 0, 1 and others; the memory freed when bit 0 is set.
DIFF_TEST(LightRigParser_DeletingDestructor, 0x2c2130)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x2c2130);
    RunDestroy(ctx, 0x2c2130, [&](int side, State *o, uint32_t flags) -> void * {
        if (side == 0) return orig(o, flags);
        return cLightRigAndCameraParserStateAdapters::DeletingDestructor(o, nullptr, flags);
    });
}

namespace {

// Runs a body through its thunk (the original's) or our forwarder, on the face moved by a vtordisp (the thunk takes it off).
template <auto Adapter> struct RetOf;
template <class R, class... X, R(__fastcall *Adapter)(void *, void *, X...)> struct RetOf<Adapter> { using Type = R; };
template <auto Adapter> const void *ThunkOf() { return reinterpret_cast<const void *>(&nGZCommandParser::VtordispThunk<Adapter>::Call); }

template <auto Adapter, class... A>
std::string RunFace(int side, uint32_t thunk, difftest::Context &ctx, State *o, uint32_t faceOffset, int32_t vtordisp, A... args)
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
        result = "-> " + Hex(static_cast<uint32_t>(r) & (std::is_same_v<R, bool> ? 0xffu : 0xffffffffu));
    }
    return result;
}

} // namespace

// Slot 0 of the cIBaseParser face (thunk 0x2c2b56 -> 0x2c2b60, ret 4): the case flag (with junk in the argument's upper bytes), the
// base's result 0 or 1, vtordisps 0 and others. Logged: the base call (its `this`, flag, the object then), the version range, and
// the five commands registered through the face (their words and blocks' sizes).
DIFF_TEST(LightRigParser_Initialise, 0x2c2b60)
{
    Allocators allocators;
    FaceVTable face(ctx);
    face.slots[2] = reinterpret_cast<void *>(&FakeSetVersionRange);
    face.slots[4] = reinterpret_cast<void *>(&FakeRegisterCommand);
    shape::StandIn base(0xb02880u, &cBaseParser::Initialise, &ImageBaseInitialise);
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::Initialise), shape::CodeOf(&OurBaseInitialise));
    std::mt19937 g(0x2c2b60);
    for (int round = 0; round < 24; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        const uint32_t arg = (g() & 0xffffff00u) | (round % 3 == 0 ? 0u : round % 3 == 1 ? 1u : (g() & 0xff));
        gBaseResult = round % 5 == 4 ? 0 : 1;
        Pair pair(g(), &face);
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            State *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            const std::string r = RunFace<&cLightRigAndCameraParserStateAdapters::Initialise>(side, 0x2c2b56, ctx, o, 0x240, vtordisp, arg);
            got[side] = gLog + r + "\n" + Describe(o);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d arg %08x base %u): %s", round, vtordisp, arg, gBaseResult, Diff(got[0], got[1]).c_str());
        if (round == 0 && got[0].find("command on") == std::string::npos) ctx.Fail("the original registered no command:\n%s", got[0].c_str());
    }
}

// Slot 10 of the cIBaseParser face (thunk 0x2c3b8e -> 0x2c3ba0): the base's ResetBlocks (0xb03e50, stood in for), then the rig
// being read emptied. Compared besides the object: the blocks freed and whether the strings kept their storage.
DIFF_TEST(LightRigParser_ResetBlocks, 0x2c3ba0)
{
    Allocators allocators;
    shape::StandIn base(0xb03e50u, &cBaseParser::ResetBlocks, &ImageBaseResetBlocks);
    shape::CodePatch ourBase(shape::MemberCode(&cBaseParser::ResetBlocks), shape::CodeOf(&OurBaseResetBlocks));
    std::mt19937 g(0x2c3ba0);
    for (int round = 0; round < 100; ++round) {
        const int32_t vtordisp = Vtordisp(g, round);
        Pair pair(g());
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            State *o = pair.o[side];
            gObj = reinterpret_cast<const uint8_t *>(o);
            difftest::FreeRecorder frees;
            const void *storage[5] = {o->mRigName.data(), o->mResourceName.data(), o->mRootTag.data(), o->mText1f8.data(), o->mAutoRigName.data()};
            const std::string r = RunFace<&cLightRigAndCameraParserStateAdapters::ResetBlocks>(side, 0x2c3b8e, ctx, o, 0x240, vtordisp);
            frees.Stop();
            const void *after[5] = {o->mRigName.data(), o->mResourceName.data(), o->mRootTag.data(), o->mText1f8.data(), o->mAutoRigName.data()};
            got[side] = gLog + r + "\n" + FreedSizes(frees) + "\n" + Describe(o);
            for (int i = 0; i < 5; ++i) got[side] += storage[i] == after[i] ? " same" : " moved";
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d (vtordisp %d): %s", round, vtordisp, Diff(got[0], got[1]).c_str());
    }
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

// A thunk `sub ecx, [ecx-4]; sub ecx, 0x88; jmp body` whose body is one of cBaseParser's (its registered hook the adapter): the
// image's thunk and our hook of it called on a face with a vtordisp, the body stood in for on both sides.
template <auto Adapter> struct Check;
template <class R, class... Args, R(__fastcall *Adapter)(void *, void *, Args...)> struct Check<Adapter> {
    static void Run(difftest::Context &ctx, uint32_t thunk, const char *name, std::mt19937 &g)
    {
        using Fn = R(__fastcall *)(void *, void *, Args...);
        const uint8_t *code = ctx.image.At<uint8_t>(thunk);
        static const uint8_t kPrefix[] = {0x2b, 0x49, 0xfc, 0x81, 0xe9, 0x88, 0x00, 0x00, 0x00, 0xe9};
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
    if (!HookAt(body)) ctx.Fail("%s %x: the body %x is not hooked", name, thunk, body);
}

} // namespace

// Every shifted thunk of the faces (+0x238: slots 0..2; +0x240: all but 0, 10 and the folded-body 11, 45; 33 shares 32's), each
// with its cBaseParser body's adapter; the own thunks (the cIBaseParser face's slots 0 and 10: their bodies and forwarders); slots
// 11 (into the folded no-op 0x140fe0) and 45 (into the folded getter 0x9d6630), called unstood on random buffers.
DIFF_TEST(LightRigParser_FaceThunks, 0x2c2507)
{
    std::mt19937 g(0x2c2507);
#define X(rva, adapter) Check<&cBaseParserAdapters::adapter>::Run(ctx, rva, #adapter, g);
    X(0x2c400b, QueryInterface)
    X(0x2c2507, AddRef)
    X(0x2c4065, Release)
    X(0x2c42b7, Shutdown)
    X(0x2c4263, SetVersionRange)
    X(0x2c427f, SetContextState)
    X(0x2c4049, RegisterCommand)
    X(0x2c4378, UnregisterCommand)
    X(0x2c436a, UnregisterCommandObject)
    X(0x2c4057, RegisterMetaCommand)
    X(0x2c41dd, RestartViaReset)
    X(0x2c2634, Reset)
    X(0x2c3f2b, RunFile)
    X(0x2c3f63, RunFiles)
    X(0x2c3f55, RunText)
    X(0x2c3f1d, RunLine)
    X(0x2c4340, PushOpenBlock)
    X(0x2c434e, PushMetaBlock)
    X(0x2c26a1, PopMetaBlock)
    X(0x2c42a9, RunExpandedText)
    X(0x2c3ffd, PushNamespace)
    X(0x2c3fe1, PopNamespace)
    X(0x2c3fef, PurgeNamespace)
    X(0x2c2ac5, GetFunctions)
    X(0x2c428d, SetOutput)
    X(0x2c429b, SetHandler)
    X(0x2c4271, SetSource)
    X(0x2c4386, FindVariable)
    X(0x2c2685, GetNamespacePath)
    X(0x2c435c, ExpandVariables)
    X(0x2c3f0f, EvalBool)
    X(0x2c3f71, EvalFloat)
    X(0x2c3f7f, EvalInt)
    X(0x2c3fb7, EvalVector2)
    X(0x2c3fc5, EvalVector3)
    X(0x2c3fd3, EvalVector4)
    X(0x2c3f8d, EvalColorRGBA)
    X(0x2c3f9b, EvalColorARGB)
    X(0x2c3fa9, EvalColorRGB)
    X(0x2c3324, CommandCount)
    X(0x2c2642, CommandAt)
    X(0x2c2f52, IsInitialised)
    X(0x2c2ea6, IsCaseSensitive)
    X(0x2c2ff3, GetText)
    X(0x2c3f47, GetFileName)
    X(0x2c3f39, GetLine)
    X(0x2c2693, HasDefinition)
    X(0x2c2650, HasCommand)
    X(0x2c4332, GetSource)
#undef X
    CheckOwnThunk(ctx, 0x2c2b56, 0x2c2b60, ThunkOf<&cLightRigAndCameraParserStateAdapters::Initialise>(), "Initialise");
    CheckOwnThunk(ctx, 0x2c3b8e, 0x2c3ba0, ThunkOf<&cLightRigAndCameraParserStateAdapters::ResetBlocks>(), "ResetBlocks");

    // Slots 11 and 45: shifted thunks into folded bodies (not hookable); no stand-in (the bodies are run), the buffer's words random.
    static const uint8_t kPrefix[] = {0x2b, 0x49, 0xfc, 0x81, 0xe9, 0x88, 0x00, 0x00, 0x00, 0xe9};
    const HookSpec *noOp = HookAt(0x2c265e), *version = HookAt(0x2c4394);
    if (!noOp || !version || std::memcmp(ctx.image.At<uint8_t>(0x2c265e), kPrefix, 10) != 0 ||
        std::memcmp(ctx.image.At<uint8_t>(0x2c4394), kPrefix, 10) != 0) {
        ctx.Fail("slots 11 / 45: not hooked, or not shifted thunks");
        return;
    }
    for (int i = 0; i < 64; ++i) {
        Junk(g, gBuffer, sizeof(gBuffer));
        const int32_t d = ThunkVtordisp(g, i % 24);
        uint8_t *face = gBuffer + 0x200;
        std::memcpy(face - 4, &d, 4);
        using VersionFn = int32_t(__fastcall *)(void *, void *);
        const int32_t a = ctx.Original<VersionFn>(0x2c4394)(face, nullptr);
        const int32_t b = reinterpret_cast<VersionFn>(version->replacement)(face, nullptr);
        using NoOpFn = void(__fastcall *)(void *, void *);
        std::vector<uint8_t> before(gBuffer, gBuffer + sizeof(gBuffer));
        ctx.Original<NoOpFn>(0x2c265e)(face, nullptr);
        reinterpret_cast<NoOpFn>(noOp->replacement)(face, nullptr);
        ++ctx.cases;
        if (a != b) ctx.Fail("slot 45 vtordisp %d: %d / %d", d, a, b);
        if (std::memcmp(before.data(), gBuffer, sizeof(gBuffer)) != 0) ctx.Fail("slot 11 wrote to the buffer");
    }
}

// A whole parser: the original's built (0x2c1800) and initialised (thunk 0x2c2b56) by the image, ours by our code, both on the real
// cBaseParser (the real face vtable, the commands registered for real). Compared: the command count (face slot 40) and HasCommand
// (slot 49) for the names the class registers (in two cases) and one it does not.
DIFF_TEST(LightRigParser_Whole, 0x2c2b56)
{
    Allocators allocators;
    using CountFn = uint32_t(__thiscall *)(void *);
    using HasFn = uint32_t(__thiscall *)(void *, const char *);
    const char *const names[] = {"lightrig", "resource", "root", "anim", "autoRig", "LIGHTRIG", "AutoRig", "camera", ""};
    auto *manager = reinterpret_cast<cLightRigManager *>(static_cast<uintptr_t>(0x5a5a1230u));
    for (int round = 0; round < 6; ++round) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            auto *o = static_cast<State *>(::operator new(State::kSize));
            void *face;
            if (side == 0) {
                ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0x2c1800)(o, manager, 1);
                face = reinterpret_cast<uint8_t *>(o) + 0x240;
                ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x2c2b56)(face, round & 1);
            } else {
                o->Construct(manager, 1);
                face = o->ParserFace();
                o->Initialise(static_cast<uint8_t>(round & 1));
            }
            auto **vtable = *reinterpret_cast<void ***>(face);
            got[side] = "commands " + std::to_string(reinterpret_cast<CountFn>(vtable[40])(face));
            for (const char *name : names) got[side] += std::string(" ") + name + "=" + std::to_string(reinterpret_cast<HasFn>(vtable[49])(face, name) & 0xff);
            got[side] += " manager " + Hex(P(o->mManager));
            if (side == 0) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x2c2130)(o, 1);
            else o->DeletingDestructor(1);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d: %s", round, Diff(got[0], got[1]).c_str());
        if (round == 0 && got[0].find("commands 0") == 0) ctx.Fail("the original's parser took no commands: %s", got[0].c_str());
    }
}

// Our vtables against the image's: every slot of the primary vtable and the two faces holds the forwarder that the hook of the
// image's entry installs (so the object's slot reaches the same adapter the image's thunk did, which the other tests check against
// the original), the virtual base tables' words are the image's, and the object our constructor builds dispatches slots through
// them: the same results as the original's object, slot for slot.
DIFF_TEST(LightRigParser_VTables, 0xdb5880)
{
    struct Table { const char *name; uint32_t image; const void *const *ours; size_t count; };
    const Table tables[] = {
        {"primary", State::kVTable, State::kOwnVTable, std::size(State::kOwnVTable)},
        {"cIGZUnknown", State::kUnknownVTable, State::kOwnUnknownVTable, std::size(State::kOwnUnknownVTable)},
        {"cIBaseParser", State::kParserVTable, State::kOwnParserVTable, std::size(State::kOwnParserVTable)},
    };
    const uint32_t base = ImageBase();
    for (const Table &t : tables) {
        const uint32_t *image = GameAddress<uint32_t>(t.image);
        for (size_t i = 0; i < t.count; ++i) {
            ++ctx.cases;
            // Primary slots 2 and 3 are the folded setter and getter (0x960960, 0x5b1920 shared by unrelated classes, not hookable):
            // ours are this class's own adapters, checked by what they do below.
            if (t.ours == State::kOwnVTable && (i == 2 || i == 3)) continue;
            const HookSpec *h = HookAt(image[i] - base);
            if (!h) ctx.Fail("%s slot %zu: the image's entry %x is not hooked", t.name, i, image[i] - base);
            else if (h->replacement != t.ours[i]) ctx.Fail("%s slot %zu: ours is %p, the hook of %x installs %p", t.name, i, t.ours[i], image[i] - base, h->replacement);
            if (!t.ours[i]) ctx.Fail("%s slot %zu: empty", t.name, i);
        }
    }
    ++ctx.cases;
    if (std::memcmp(GameAddress<int32_t>(State::kVbTable), State::kOwnVbTable, sizeof(State::kOwnVbTable)) != 0)
        ctx.Fail("the cBaseParser part's virtual base table differs from the image's");
    if (std::memcmp(GameAddress<int32_t>(State::kFace2VbTable), State::kOwnFace2VbTable, sizeof(State::kOwnFace2VbTable)) != 0)
        ctx.Fail("the cIBaseParser face's virtual base table differs from the image's");

    // Dispatch on whole objects (the original's built by the image, ours by our constructor), both initialised.
    Allocators allocators;
    using Fn0 = uint32_t(__thiscall *)(void *);
    using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
    for (int round = 0; round < 4; ++round) {
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            auto *o = static_cast<State *>(::operator new(State::kSize));
            if (side == 0) {
                ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0x2c1800)(o, nullptr, 1);
                ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x2c2b56)(reinterpret_cast<uint8_t *>(o) + 0x240, round & 1);
            } else {
                o->Construct(nullptr, 1);
                o->Initialise(static_cast<uint8_t>(round & 1));
            }
            uint8_t *unknown = reinterpret_cast<uint8_t *>(o) + 0x238, *parser = reinterpret_cast<uint8_t *>(o) + 0x240;
            auto **uv = *reinterpret_cast<void ***>(unknown);
            auto **pv = *reinterpret_cast<void ***>(parser);
            std::string &s = got[side];
            auto word = [&](uint32_t v) { s += " " + Hex(v); };
            word(reinterpret_cast<Fn0>(pv[40])(parser));                          // CommandCount
            word(reinterpret_cast<Fn0>(pv[42])(parser) & 0xff);                   // IsInitialised
            word(reinterpret_cast<Fn0>(pv[43])(parser) & 0xff);                   // IsCaseSensitive
            word(reinterpret_cast<Fn0>(pv[45])(parser));                          // GetVersion
            word(reinterpret_cast<Fn1>(pv[49])(parser, P("lightrig")) & 0xff);    // HasCommand
            word(reinterpret_cast<Fn0>(uv[1])(unknown));                          // AddRef
            word(reinterpret_cast<Fn0>(uv[1])(unknown));
            word(reinterpret_cast<Fn0>(uv[2])(unknown));                          // Release
            reinterpret_cast<Fn0>(pv[10])(parser);                                // ResetBlocks on the empty rig
            word(static_cast<uint32_t>(o->mRigName.size() + o->mAnimations.size()));
            // The primary vtable's folded setter and getter (slots 2, 3).
            auto **vt = *reinterpret_cast<void ***>(o);
            for (uint32_t value : {0x1234u + round, 0u, 0xffffffffu}) {
                reinterpret_cast<Fn1>(vt[2])(o, value);
                word(reinterpret_cast<Fn0>(vt[3])(o));
                word(o->Parser()->mField158);
            }
            if (side == 0) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x2c2130)(o, 1);
            else reinterpret_cast<Fn1>(vt[0])(o, 1);
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("round %d: %s / %s", round, got[0].c_str(), got[1].c_str());
    }
}

// ---- the commands and AddRig --------------------------------------------------------------------------------------------------

namespace {

// The argument list: slot 4 Argument(i) (ret 4), 7 Count, 10 Positional(count *, least, most) (ret 0xc). Logged.
struct FakeArgs {
    void **vt;
    const char *argv[4];
    uint32_t count;    // what Count says
    uint32_t posCount; // what Positional stores
};
const char *__fastcall ArgsArgument(FakeArgs *self, void *, uint32_t i)
{
    gLog += "argument " + Hex(i) + "\n";
    return i < 4 ? self->argv[i] : "?";
}
uint32_t __fastcall ArgsCount(FakeArgs *self, void *)
{
    gLog += "count\n";
    return self->count;
}
const char *const *__fastcall ArgsPositional(FakeArgs *self, void *, uint32_t *count, uint32_t least, uint32_t most)
{
    gLog += "positional " + Hex(least) + " " + Hex(most) + " out " + (count ? "set" : "null") + "\n";
    if (count) *count = self->posCount;
    return self->argv + 1;
}
void *gArgsVt[16] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&ArgsArgument), nullptr, nullptr,
                     reinterpret_cast<void *>(&ArgsCount), nullptr, nullptr, reinterpret_cast<void *>(&ArgsPositional)};

// The parser face: slot 16 PushOpenBlock(block), 20 PushNamespace(name), 21 PopNamespace. Logged (the name's text and whether it
// is the argument list's own pointer).
const uint8_t *gCommand = nullptr;
const FakeArgs *gArgs = nullptr;
void __fastcall FacePushOpenBlock(void *face, void *, void *block)
{
    gLog += "open block on " + Offset(face) + " block " + (block == gCommand ? std::string("command") : Hex(P(block))) + "\n";
}
void __fastcall FacePushNamespace(void *face, void *, const char *name)
{
    int which = -1;
    for (int i = 0; i < 4 && gArgs; ++i)
        if (name == gArgs->argv[i]) which = i;
    gLog += "namespace on " + Offset(face) + " argument " + std::to_string(which) + " '" + std::string(name).substr(0, 64) + "'\n";
}
void __fastcall FacePopNamespace(void *face, void *) { gLog += "pop namespace on " + Offset(face) + "\n"; }
struct FakeFace {
    void *slots[51] = {};
    FakeFace()
    {
        slots[16] = reinterpret_cast<void *>(&FacePushOpenBlock);
        slots[20] = reinterpret_cast<void *>(&FacePushNamespace);
        slots[21] = reinterpret_cast<void *>(&FacePopNamespace);
    }
};
FakeFace gFace;
// A virtual base table that puts the cIBaseParser face 0x30 bytes past where the class has it (inside the object's slack).
const int32_t kMovedVbTable[3] = {-8, 0x230, 0x268};

void FillRig(LightRig &rig, std::mt19937 &g)
{
    FillText(rig.mResourceName.value.mString, g);
    FillText(rig.mRootTag.value.mString, g);
    FillText(rig.mAutoRigName.value.mString, g);
    FillAnimations(rig.mAnimations, g);
    for (size_t i = g() % 3; i > 0; --i) rig.mNamedNodes.emplace(PoolText(g()), reinterpret_cast<void *>(static_cast<uintptr_t>(g())));
    rig.mLoaded = static_cast<bool>(g() & 1);
    rig.mRootMissing = static_cast<bool>(g() & 1);
    rig.mAutoRig = static_cast<bool>(g() & 1);
}

void DescribeFileString(std::string &s, const nGZCommandParser::FileString &f)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(&f);
    s += " [" + WordText(w[0]);
    DescribeText(s, f.value.mString);
    s += " count " + Hex(w[7]) + "]";
}

std::string DescribeRigs(const cLightRigManager::RigMap &rigs)
{
    std::string s = "rigs";
    DescribeHead(s, rigs);
    for (const auto &kv : rigs) {
        const uint8_t *node = NodeOf(kv);
        const LightRig &rig = kv.second;
        s += "\n [" + Hex(node[0xc]) + "/" + Hex(node[0xd]);
        DescribeText(s, kv.first);
        s += " root " + Hex(P(rig.mRoot.p)) + " named";
        DescribeHead(s, rig.mNamedNodes);
        for (const auto &n : rig.mNamedNodes) s += " " + n.first.substr(0, 16) + "=" + Hex(P(n.second));
        DescribeFileString(s, rig.mResourceName);
        DescribeFileString(s, rig.mRootTag);
        DescribeFileString(s, rig.mAutoRigName);
        s += " animations";
        DescribeAnimations(s, rig.mAnimations);
        s += " refs";
        DescribeHead(s, rig.mAnimRefs);
        s += " flags " + Hex(*reinterpret_cast<const uint8_t *>(&rig.mLoaded)) + " " + Hex(*reinterpret_cast<const uint8_t *>(&rig.mRootMissing)) +
             " " + Hex(*reinterpret_cast<const uint8_t *>(&rig.mAutoRig)) + "]";
    }
    return s;
}

// One side's world: the state (kSize + 0x80, its face a logging fake), the manager's rig map, the command and the arguments.
struct World {
    alignas(16) uint8_t managerBytes[sizeof(cLightRigManager)];
    State *o = nullptr;
    alignas(16) uint8_t command[0x48];
    FakeArgs args{};
    cLightRigManager *Manager() { return reinterpret_cast<cLightRigManager *>(managerBytes); }

    // `commandSize` 0x44 (the rig block, state at +0x40) or 16 (state at +0xc); `argc` the arguments given.
    World(uint32_t seed, uint32_t vtable, size_t commandSize, uint32_t argc)
    {
        std::mt19937 g(seed);
        Junk(g, managerBytes, sizeof(managerBytes));
        ::new (&Manager()->mRigs) cLightRigManager::RigMap();
        for (size_t i = g() % 4; i > 0; --i) FillRig(Manager()->mRigs[PoolText(g())], g);
        o = static_cast<State *>(::operator new(State::kSize + 0x80));
        Junk(g, o, State::kSize + 0x80);
        FillMembers(o, g);
        o->mManager = Manager();
        const bool moved = g() % 4 == 0;
        *reinterpret_cast<const void **>(o->mBase + 8) = moved ? static_cast<const void *>(kMovedVbTable) : GameAddress<void>(State::kVbTable);
        *reinterpret_cast<void **>(o->mBase + 8 + (moved ? kMovedVbTable[2] : 0x238)) = gFace.slots;
        Junk(g, command, sizeof(command));
        *reinterpret_cast<const void **>(command) = GameAddress<void>(vtable);
        *reinterpret_cast<State **>(command + (commandSize == 0x44 ? 0x40 : 0xc)) = o;
        args.vt = gArgsVt;
        for (uint32_t i = 0; i < 4; ++i) args.argv[i] = PoolText(g());
        args.argv[0] = "cmd";
        // The count asked for: mostly the arguments given, sometimes any other.
        args.count = g() % 4 == 0 ? g() % 5 : argc;
        args.posCount = g() % 3;
        if (g() % 4 == 0) args.posCount = g() % 2;
    }
    ~World()
    {
        FreeObject(o);
        std::destroy_at(&Manager()->mRigs);
    }
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    std::string Describe()
    {
        std::string s = ::Describe(o) + "\n" + DescribeRigs(Manager()->mRigs) + "\ncommand";
        // The command's state pointer (each side's own object) as a name; the rest of its bytes as they are.
        for (size_t i = 0; i < sizeof(command); i += 4) {
            uint32_t word;
            std::memcpy(&word, command + i, 4);
            s += " " + (word == P(o) ? std::string("state") : Hex(word));
        }
        return s;
    }
};

// Runs a command slot on two worlds from one seed (the original's slot, then ours) and compares what they did.
template <class RunOriginal, class RunOurs>
void RunCommand(difftest::Context &ctx, const char *name, uint32_t seed, uint32_t vtable, size_t commandSize, uint32_t argc, int rounds,
                RunOriginal runOriginal, RunOurs runOurs, int &threwCount)
{
    Allocators allocators;
    std::mt19937 g(seed);
    for (int round = 0; round < rounds; ++round) {
        const uint32_t worldSeed = g();
        std::string got[2];
        for (int side = 0; side < 2; ++side) {
            World w(worldSeed, vtable, commandSize, argc);
            gObj = reinterpret_cast<const uint8_t *>(w.o);
            gCommand = w.command;
            gArgs = &w.args;
            gLog.clear();
            cptest::Outcome outcome;
            {
                difftest::FreeRecorder frees;
                outcome = side == 0 ? cptest::RunGuarded([&] { return runOriginal(w); }) : cptest::RunGuarded([&] { return runOurs(w); });
                frees.Stop();
                got[side] = gLog + "-> " + Hex(outcome.ret) + (outcome.threw ? " threw " + outcome.detail : std::string()) + "\n" + FreedSizes(frees) +
                            "\n" + w.Describe();
            }
            if (side == 0 && outcome.threw) ++threwCount;
            gArgs = nullptr;
        }
        ++ctx.cases;
        if (got[0] != got[1]) ctx.Fail("%s round %d: %s", name, round, Diff(got[0], got[1]).c_str());
    }
}

template <class Command> void RunExecute(difftest::Context &ctx, uint32_t rva, size_t commandSize, uint32_t argc, int expectThrows = 1)
{
    using Fn = void(__thiscall *)(void *, void *);
    auto orig = ctx.Original<Fn>(rva);
    int threw = 0;
    RunCommand(ctx, "Execute", rva, Command::kVTable, commandSize, argc, 200,
               [&](World &w) { orig(w.command, &w.args); },
               [&](World &w) { reinterpret_cast<Command *>(w.command)->Execute(reinterpret_cast<cp::cIArguments *>(&w.args)); }, threw);
    ++ctx.cases;
    if (threw < expectThrows || threw > 190) ctx.Fail("%x: the original threw in %d of 200 rounds", rva, threw);
}

} // namespace

// cLightRigCommand slot 4 (0x2c2830, ret 4): the count (2), the open rig name (a throw when set), the block and its namespace
// pushed, the rig's name taken and the rest of the rig emptied.
DIFF_TEST(LightRigParser_LightRigExecute, 0x2c2830) { RunExecute<nTSSG::cLightRigCommand>(ctx, 0x2c2830, 0x44, 2); }
// cLightRigResNameCommand slot 4 (0x2c2990): the count (2), the rig name (a throw when empty), the resource name.
DIFF_TEST(LightRigParser_ResNameExecute, 0x2c2990) { RunExecute<nTSSG::cLightRigResNameCommand>(ctx, 0x2c2990, 16, 2); }
// cRootTagCommand slot 4 (0x2c2a30): the count (2), the rig name, the root tag.
DIFF_TEST(LightRigParser_RootTagExecute, 0x2c2a30) { RunExecute<nTSSG::cRootTagCommand>(ctx, 0x2c2a30, 16, 2); }
// cAnimNameCommand slot 4 (0x2c26b0): the count (3), the rig name, the animation by key (made or replaced).
DIFF_TEST(LightRigParser_AnimNameExecute, 0x2c26b0) { RunExecute<nTSSG::cAnimNameCommand>(ctx, 0x2c26b0, 16, 3); }
// cAutoRigCommand slot 4 (0x2c27a0): the rig name, the positional words (0..1), the flag and the name when one was given.
DIFF_TEST(LightRigParser_AutoRigExecute, 0x2c27a0) { RunExecute<nTSSG::cAutoRigCommand>(ctx, 0x2c27a0, 16, 2); }

// cLightRigCommand slot 9 (0x2c3b20, plain ret): the namespace popped, the rig added (AddRig, the original's on its side and ours on
// ours) when a name is open, the name emptied, a throw when nothing was added.
DIFF_TEST(LightRigParser_LightRigEndBlock, 0x2c3b20)
{
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x2c3b20);
    int threw = 0;
    RunCommand(ctx, "EndBlock", 0x2c3b20, nTSSG::cLightRigCommand::kVTable, 0x44, 0, 200, [&](World &w) { orig(w.command); },
               [&](World &w) { reinterpret_cast<nTSSG::cLightRigCommand *>(w.command)->EndBlock(); }, threw);
    ++ctx.cases;
    if (threw < 20 || threw > 180) ctx.Fail("the original threw in %d of 200 rounds", threw);
}

// 0x2c21a0 (__thiscall, returns true): the manager's record for the open rig's name made or found and given the rig's names,
// animations and auto-rig flag; its loaded and root-missing flags cleared. The other records untouched.
DIFF_TEST(LightRigParser_AddRig, 0x2c21a0)
{
    using Fn = uint32_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x2c21a0);
    int threw = 0;
    RunCommand(ctx, "AddRig", 0x2c21a0, nTSSG::cLightRigCommand::kVTable, 0x44, 0, 200, [&](World &w) { return orig(w.o) & 0xff; },
               [&](World &w) { return static_cast<uint32_t>(w.o->AddRig()); }, threw);
    ++ctx.cases;
    if (threw) ctx.Fail("the original threw in %d rounds", threw);
}

// The commands' slot 5 (GetName): the image's text, the same pointer.
DIFF_TEST(LightRigParser_Names, 0x2c3030)
{
    using Fn = const char *(__thiscall *)(const void *);
    struct Name { uint32_t rva; const char *(*ours)(const void *); const char *text; };
    const Name names[] = {
        {0x2c3030, [](const void *c) { return static_cast<const nTSSG::cLightRigCommand *>(c)->GetName(); }, "lightrig"},
        {0x2c3040, [](const void *c) { return static_cast<const nTSSG::cLightRigResNameCommand *>(c)->GetName(); }, "resource"},
        {0x2c3050, [](const void *c) { return static_cast<const nTSSG::cRootTagCommand *>(c)->GetName(); }, "root"},
        {0x2c3010, [](const void *c) { return static_cast<const nTSSG::cAnimNameCommand *>(c)->GetName(); }, "anim"},
        {0x2c3020, [](const void *c) { return static_cast<const nTSSG::cAutoRigCommand *>(c)->GetName(); }, "autoRig"},
    };
    alignas(16) uint8_t command[0x48];
    std::mt19937 g(0x2c3030);
    for (const Name &n : names) {
        for (int round = 0; round < 4; ++round) {
            Junk(g, command, sizeof(command));
            const char *a = ctx.Original<Fn>(n.rva)(command);
            const char *b = n.ours(command);
            ++ctx.cases;
            if (a != b || std::strcmp(a, n.text) != 0) ctx.Fail("%x: '%s' (%p) / '%s' (%p)", n.rva, a, a, b, b);
        }
    }
}
