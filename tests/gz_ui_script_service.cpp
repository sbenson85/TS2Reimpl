// Differential test for cGZUIScriptService (lane KS, src/gz/cGZUIScriptService.h): the class in the standalone shape.
//
// UIScript_Run: a service built by the original constructor (0x85b39a, game vtables 0xe47ff8 / 0xe48028 / 0xe48040 /
// 0xe4805c / 0xe48078 / 0xe4808c / 0xe480c4) or the original creator (0x85b2df) against one built by ours (our
// constructor, or Create), each driven through its own vtables by the same random sequence of operations, then destroyed
// by the last Release (the deleting destructor 0x85b48a behind the +0x18 thunk 0x85b482, the destructor 0x85b434, which
// runs Shutdown). Operations: QueryInterface on every face (0x85b818 and its thunks 0x85b897 / 0x85b89f / 0x85b8bf /
// 0x85b8a7 / 0x85b8af / 0x85b8b7; AddRef 0x85b4a9 / Release 0x85b8c7 and their thunks 0x85b4b1 / 0x85b4b9 / 0x85b4d9 /
// 0x85b4c1 / 0x85b4c9 / 0x85b4d1 / 0x85b8cf / 0x85b8d7 / 0x85b8f7 / 0x85b8df / 0x85b8e7 / 0x85b8ef on what it hands
// out), QueryScriptObject (0x85b6ab), GetScriptObject (0x85b581), RunNode6 (0x85b776), RunNode7 (0x85b704), CompileNode
// (0x85b73d), GetScriptNode (0x85b7af), GetContext (folded 0x230120), SetContext (0x85bbaf), PushNewScript (0x85bb0c),
// SetCompilerArgument (0x85bb82), Attach (0x85b8ff), Detach (0x85b956), the target's slot 5 (folded 0x1477b0),
// BeginCompile (0x85b4e1), EndCompile (0x85b6e8), the interpreting target's slot 5 (folded 0x147a70), OnCompile
// (0x85b9ed), Init (0x85b7bc), Shutdown (0x85bbdb); the pending stack's push 0x85b7f1, pop 0x85b7c9 and peek 0x85bf7f
// run inside them.
//
// Every object the service reaches is a recording fake (tests/ts_services_fixture.h); the service object (0x10c9378)
// answers the resource manager (0x20db69's query) and the QueryWithIdsA object (0x20dbd8). Stood in for on both sides:
// the game's operator new / delete (ours), the script node's reader 0x85bc39 and ours (logs, makes the node now and then, answers
// whether there is one), the image's sprintf 0x14487d (CRT state the harness has not set up; ours is our CRT's). Detach is
// called only with an interpreter held (the original releases it unchecked). Compared after every operation: the result,
// the event log, every field (the pending stack by content), every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZUIScriptService.h"
#include "rz/cRZString.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <list>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace ui_script_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{7, "dd"}, {9, "dd"}, {10, "dd"}, {12, "dd"}, {14, "dd"}, {20, "---d"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cGZUIScriptService;

constexpr uint32_t kTag = 0x7d000000; // the service

enum : unsigned {
    kFramework = 0, kService, kManager, kQuery, kSource, kNode, kResult, kCompiler, kCompObj, kScriptA, kScriptB, kSObj,
    kObj2, kLeaf, kFinal, kHost, kTarget, kArgument, kContext, kCaller, kText, kValue, kInterpreter, kResult2,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); } // a bool in al with a busy upper byte
uint32_t Mostly() { return 0x100u | uint32_t(Draw(5) != 0); }

// An out-parameter handed an object (counted) or left alone, the call answering a bool.
uint32_t HandOut(unsigned fake, uint32_t out, uint32_t yes)
{
    if (yes & 1) {
        ++F(fake).refs;
        *reinterpret_cast<void **>(uintptr_t(out)) = &F(fake);
    }
    return yes;
}

// The framework's GetClassObject (class, iid, out): refused now and then.
uint32_t ClassObject(Obj *, uint32_t *a)
{
    unsigned fake;
    switch (a[0]) {
    case 0xfa450242: fake = Draw(2) ? kScriptA : kScriptB; break;
    case 0xba2e7954: fake = kInterpreter; break;
    case 0x5a354595: fake = Draw(2) ? kResult : kResult2; break;
    case 0x1000: fake = kCompiler; break;
    default: return 0x100;
    }
    return HandOut(fake, a[2], Draw(6) == 0 ? 0x100 : 0x101);
}
// The service object's slot 5 (id, iid, out): the resource manager, the QueryWithIdsA object.
uint32_t ServiceQuery(Obj *, uint32_t *a)
{
    if (a[0] == 0x056b906e && a[1] == 0x656b8efc) return HandOut(kManager, a[2], Mostly());
    if (a[0] == 0xa417445e && a[1] == 0x5a4) return HandOut(kQuery, a[2], Mostly());
    return 0x100;
}
// The manager's slot 15 (&key): logs the key, answers.
uint32_t ManagerHas(Obj *, uint32_t *a)
{
    const auto *k = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    for (int i = 0; i < 4; ++i) Ev(k[i]);
    return Bool();
}
// The script node's slot 8 (&key): logs the key it compiles, answers.
uint32_t NodeCompile(Obj *, uint32_t *a)
{
    const auto *k = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    Ev(0x8e8);
    for (int i = 0; i < 4; ++i) Ev(k[i]);
    return gRng();
}
// QueryInterface (iid, out) of fakes with one face of interest: value[0] the iid, value[1] the fake, value[2] the odds.
uint32_t FaceQuery(Obj *self, uint32_t *a)
{
    if (a[0] != self->value[0]) return 0x100;
    return HandOut(self->value[1], a[1], Draw(5) ? 0x101 : 0x100);
}
void SetFace(unsigned fake, uint32_t iid, unsigned to)
{
    F(fake).value[0] = iid;
    F(fake).value[1] = to;
    F(fake).Set(0, "dp", 0, &FaceQuery);
}
// The text source's slot 56 (&string, 0, 0x20): writes its text.
const char *const kTexts[] = {"", "x", "on click: do it", "a source text longer than the small string buffer"};
uint32_t SourceText(Obj *self, uint32_t *a)
{
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString = kTexts[self->value[3] % 4];
    return gRng();
}
uint32_t TextHash(const std::string &t)
{
    uint32_t h = 0x811c9dc5;
    for (char c : t) h = (h ^ uint8_t(c)) * 0x01000193;
    return h;
}
// The compile caller's slots 11 (&string) and 10 (&string, value): the text logged.
uint32_t CallerText(Obj *, uint32_t *a)
{
    const auto *s = reinterpret_cast<const cRZString *>(uintptr_t(a[0]));
    Ev(0x7e57);
    Ev(P(s->mVTable) == P(GameAddress<void>(cRZString::kVTable)));
    Ev(TextHash(s->mString));
    return gRng();
}

// ---- stand-ins ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
int __cdecl StandSprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer, 40, format, args);
    va_end(args);
    return n;
}
bool __fastcall StandReadNode(uint8_t *self, void *)
{
    Ev(0x85bc39);
    Ev(svcfx::Norm(P(self)));
    auto &node = *reinterpret_cast<void **>(self + 0x34);
    if (!node && Draw(3)) {
        ++F(kNode).refs;
        node = &F(kNode);
    }
    return node != nullptr;
}

// ---- the world ----

void BuildFakes(uint32_t seed, svcfx::Globals &globals)
{
    svcfx::ResetAll();
    gRng.seed(seed);
    F(kFramework).Set(3, "ddp", 0, &ClassObject);
    F(kService).Set(5, "ddp", 0, &ServiceQuery);
    F(kManager).Set(15, "p", 0, &ManagerHas);
    F(kQuery).Set(3, 0, Draw(5) ? P(&F(kSource)) : 0);
    F(kSource).Set(49, 0, Draw(2) ? Draw(10000) : gRng());
    F(kSource).Set(50, 0, Draw(2) ? Draw(10000) : gRng());
    F(kNode).Set(3, "p", gRng());
    for (unsigned s : {6u, 7u}) F(kNode).Set(s, "d", gRng());
    F(kNode).Set(8, "p", 0, &NodeCompile);
    for (unsigned r : {kResult, kResult2}) {
        F(r).Set(4, "p", gRng());
        F(r).Set(5, "d", Draw(5) ? P(&F(kObj2)) : 0);
        F(r).Set(9, 1, gRng());
    }
    SetFace(kObj2, 0x9a4527cb, kLeaf);
    F(kLeaf).Set(3, 0, Draw(5) ? P(&F(kFinal)) : 0);
    F(kCompiler).Set(3, "d", gRng());
    F(kCompiler).Set(4, 0, gRng());
    F(kCompiler).Set(9, "p", gRng());
    SetFace(kCompiler, 0x9a4527cb, kCompObj);
    F(kCompObj).Set(6, "p", gRng());
    SetFace(kScriptA, 0x9a4527cb, kSObj);
    SetFace(kScriptB, 0x9a4527cb, kSObj);
    F(kSObj).Set(5, "d", gRng());
    for (unsigned h : {kHost, kTarget}) {
        F(h).Set(13, "d", gRng());
        F(h).Set(14, 0, gRng());
    }
    F(kCaller).Set(11, "p", 0, &CallerText);
    F(kCaller).Set(10, "pp", 0, &CallerText);
    F(kText).Set(3, 0, Draw(4) ? 0x800e : 0x800f);
    F(kText).value[3] = Draw(4);
    F(kText).Set(56, "p-d", 0, &SourceText);
    globals.SetWord(0x10c9378, Draw(8) == 0 ? 0 : P(&F(kService)));
}

// ---- calls through a face's own vtable ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }

bool gFactory;
void *Build(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        void *object = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0x85b2df)();
        b.push_back(object ? 1 : 0);
        auto *m = static_cast<uint8_t *>(object);
        for (uint32_t off : {0x2du, 0x2eu, 0x2fu, 0x31u, 0x32u, 0x33u, 0x3du, 0x3eu, 0x3fu, 0x4du, 0x4eu, 0x4fu})
            m[off] = 0xc5; // padding the constructor leaves as the heap had it
        return object;
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(::new (memory) Sys) : ctx.Original<void *(__thiscall *)(void *)>(0x85b39a)(memory);
    b.push_back(r == memory);
    return memory;
}

constexpr uint32_t kFaceOffsets[7] = {0, 4, 8, 0xc, 0x10, 0x14, 0x18};

// The object's words as Dump logs them (svcfx::Field): 'p' pointer, 'd' data.
constexpr char kObjectKinds[] = "-------d dddddppd --pddppp p";
void Dump(Blob &b, void *object, bool ours, const shape::OurTables<7> &tables)
{
    static const uint32_t kGame[7] = {Sys::kVTable,      Sys::kLegacyVTable,   Sys::kTargetVTable, Sys::kInterpretingVTable,
                                      Sys::kCompilingVTable, Sys::kServiceVTable, Sys::kUnknownVTable};
    for (unsigned f = 0; f < 7; ++f) {
        const uint32_t w = Word(object, kFaceOffsets[f]);
        b.push_back(ours ? w == P(tables.face[f]) : w == P(GameAddress<void>(kGame[f])));
    }
    for (uint32_t off = 0x1c; off < 0x40; off += 4) svcfx::Field(b, Word(object, off), kObjectKinds, off);
    const auto &pending = *reinterpret_cast<const std::list<cIGZUnknown *> *>(Face(object, 0x40));
    b.push_back(uint32_t(pending.size()));
    for (cIGZUnknown *p : pending) b.push_back(svcfx::Norm(P(p)));
    for (uint32_t off = 0x48; off < 0x64; off += 4) svcfx::Field(b, Word(object, off), kObjectKinds, off);
}

const uint32_t kIids[] = {1, 0xfa3562fa, 0x7a4a48ed, 0x1a2bdeb8, 0x1a2bdeb9, 0x1a2bdeba, 0x287fb697, 0xfa3562fb, 0};

enum : unsigned {
    opQuery, opQueryScript, opGetScript, opRun6, opRun7, opCompile, opNode, opGetContext, opSetContext, opPush, opSetArg,
    opAttach, opDetach, opTarget5, opBegin, opEnd, opInterp5, opOnCompile, opInit, opShutdown, opCount
};

void *Pick(std::initializer_list<unsigned> fakes)
{
    const unsigned i = Draw(unsigned(fakes.size()) + 1);
    return i == fakes.size() ? nullptr : &F(fakes.begin()[i]);
}

void RunOp(Blob &b, void *object, unsigned kind)
{
    uint32_t key[4] = {gRng(), Draw(2) ? gRng() : 0, gRng(), gRng()};
    switch (kind) {
    case opQuery: {
        const uint32_t face = kFaceOffsets[Draw(7)];
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t r = Slot<uint32_t>(Face(object, face), 0, kIids[Draw(9)], &out) & 0xff;
        b.push_back(r);
        b.push_back(svcfx::NormOut(P(out), 0x5a5a5a5a));
        if (r) { // through the face handed out: AddRef, then two Releases (the thunks of every face)
            b.push_back(Slot<uint32_t>(out, 1));
            b.push_back(Slot<uint32_t>(out, 2));
            b.push_back(Slot<uint32_t>(out, 2));
        }
        break;
    }
    case opQueryScript: {
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t iid = Draw(2) ? 0x55 : 0x56;
        F(kFinal).Set(0, "dp", 0, &FaceQuery);
        F(kFinal).value[0] = 0x55;
        F(kFinal).value[1] = kValue;
        b.push_back(Slot<uint32_t>(object, 3, key, gRng(), gRng(), iid, &out) & 0xff);
        b.push_back(svcfx::NormOut(P(out), 0x5a5a5a5a));
        break;
    }
    case opGetScript: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 4, key, gRng(), gRng()))); break;
    case opRun6: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 5, gRng()))); break;
    case opRun7: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 6, gRng()))); break;
    case opCompile: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 7, key))); break;
    case opNode: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 8))); break;
    case opGetContext: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 9))); break;
    case opSetContext: Slot<void>(object, 10, Pick({kContext, kArgument})); break;
    case opPush: b.push_back(Slot<uint32_t>(Face(object, 4), 3, gRng()) & 0xff); break;
    case opSetArg: b.push_back(Slot<uint32_t>(Face(object, 4), 4, Pick({kArgument, kContext})) & 0xff); break;
    case opAttach: b.push_back(Slot<uint32_t>(Face(object, 8), 3, &F(kHost)) & 0xff); break;
    case opDetach:
        if (Word(object, 0x38) == 0) break; // the original releases the interpreter unchecked
        b.push_back(Slot<uint32_t>(Face(object, 8), 4, &F(kHost), Bool()) & 0xff);
        break;
    case opTarget5: b.push_back(Slot<uint32_t>(Face(object, 8), 5, gRng(), gRng()) & 0xff); break;
    case opBegin: b.push_back(Slot<uint32_t>(Face(object, 0xc), 3, &F(kTarget), Draw(3) ? 0x1000u : 0x1001u) & 0xff); break;
    case opEnd: b.push_back(Slot<uint32_t>(Face(object, 0xc), 4, &F(kTarget), gRng()) & 0xff); break;
    case opInterp5: b.push_back(Slot<uint32_t>(Face(object, 0xc), 5, gRng(), gRng(), gRng()) & 0xff); break;
    case opOnCompile: {
        const int32_t events[] = {1, 2, 3, 0, 4, -1};
        const int32_t event = events[Draw(6)];
        cGZUIScriptArgs args;
        const unsigned n = Draw(5);
        for (unsigned i = 0; i < n; ++i) {
            const int32_t kinds[] = {4, 5, 3, 6};
            const int32_t k = kinds[Draw(4)];
            args.push_back({k, k == 4 ? (Draw(4) ? reinterpret_cast<cIGZUnknown *>(&F(kText)) : nullptr)
                                      : reinterpret_cast<cIGZUnknown *>(Draw(4) ? &F(kValue) : nullptr)});
        }
        svcfx::AddRange(args.data(), args.size() * 8 + 1, 0x7c000000);
        void *data = event == 3 ? static_cast<void *>(&args) : reinterpret_cast<void *>(uintptr_t(gRng()));
        b.push_back(Slot<uint32_t>(Face(object, 0x10), 3, &F(kCaller), event, data) & 0xff);
        svcfx::gRanges.pop_back();
        break;
    }
    case opInit: b.push_back(Slot<uint32_t>(Face(object, 0x14), 8) & 0xff); break;
    case opShutdown: b.push_back(Slot<uint32_t>(Face(object, 0x14), 9) & 0xff); break;
    }
}

} // namespace ui_script_test

using namespace ui_script_test;

DIFF_TEST(UIScript_Run, 0x85b2df)
{
    // Also reached: every RVA in the file comment.
    svcfx::Globals globals;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::CodePatch sprintfPatch(0x14487d, shape::CodeOf(&StandSprintf));
    shape::StandIn readNode(0x85bc39, &cGZUIScriptService::ReadScriptNode, &StandReadNode); // ours since lane MC

    const shape::OurTables<7> &tables = shape::Ours<Sys>(kFaceOffsets);
    svcfx::gEv.clear();

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 opRng(seed * 7919);
        std::vector<unsigned> ops(1 + opRng() % 16);
        for (unsigned &op : ops) op = opRng() % opCount;
        gFactory = seed % 3 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed, globals);
            Blob first;
            void *object = Build(ctx, first, side == 1);
            svcfx::AddRange(object, Sys::kSize, kTag);
            Dump(first, object, side == 1, tables);
            first.insert(first.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    RunOp(b, object, ops[i]);
                    Dump(b, object, side == 1, tables);
                } else {
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                svcfx::CaptureFakes(b, kValueKinds);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        for (size_t i = 0; i < results[0].size(); ++i) {
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "UIScript_Run", seed)) {
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (op %u)", unsigned(i), unsigned(ops.size() + 2),
                         i && op < ops.size() ? ops[op] : 99u);
                seed = 100000;
                break;
            }
        }
    }
}
