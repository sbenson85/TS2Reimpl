// Differential test for cGZScriptableControlSet (lane MC, src/gz/cGZScriptableControlSet.h): the class in the
// standalone shape.
//
// ControlSet_Run: a set built by the original constructor (0x85bfe2, game vtables 0xe481a0 / 0xe481d8; its array's
// constructor 0x85bf91) or the original creator (0x85b2c9) against one built by ours (our constructor, or Create), each
// driven through its own vtables by the same random sequence of operations, then destroyed by the last Release (0x85c3dc;
// the deleting destructor 0x85c055, the destructor 0x85c00f, the array's erase 0x85c127 and destructor 0x85a2a1).
// Operations: QueryInterface on both faces (0x85c388 and its thunk 0x85c3d4), AddRef (folded 0x85c074, thunk 0x85c07c),
// Release (0x85c3dc, thunk 0x85c3f3), GetControls (folded 0x3a4f40), Add (0x85c211, with the array's push 0x85c365 and its
// growth 0x85c238, whose copies are 0x85c4fa and 0x85c107), FindById (0x85c155), GetAt (folded 0x7232e8),
// FindWindowById (0x85c18f), GetWindowAt (0x85c1d0), SetOwnsWindows (0x85c453), GetCount (folded 0x153ad0), BindAll (0x85c481), Render (0x85c3fb), HitTest (0x85c084, with
// the rectangle test 0x85c340).
//
// The controls and their faces are recording fakes (tests/ts_services_fixture.h): a control's QueryInterface hands out
// its legacy face (0x9a4527cb, whose slot 3 is the window), its render face (0x5a356159: Render, GetRect writing a
// rectangle, HitTest) or itself for the iid BindAll asks for, each now and then. Stood in for on both sides: the game's
// operator new / delete (ours). The array's allocator is the game's cGZDefaultAllocator (RZMemory, the allocator service
// over the game's general allocator; ours on our side). Compared after every operation: the result, the event log, every
// field (the array by content: size, capacity, elements, whether it has an allocator), every fake's count.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZScriptableControlSet.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace control_set_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "dd"}, {1, "dd"}, {2, "dd"}, {3, "dd"}, {4, "dd"}, {5, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Set = cGZScriptableControlSet;

constexpr uint32_t kTag = 0x7d000000; // the set
constexpr unsigned kControls = 6;     // fakes 0..5
constexpr unsigned kLegacy = 6;       // 6..8: legacy faces
constexpr unsigned kRender = 9;       // 9..11: render faces
constexpr unsigned kWindow = 12;      // 12..13
constexpr unsigned kContext = 14;
constexpr uint32_t kBindIids[] = {0x1234, 0x5678};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t HandOut(unsigned fake, uint32_t out, uint32_t yes)
{
    if (yes & 1) {
        ++F(fake).refs;
        *reinterpret_cast<void **>(uintptr_t(out)) = &F(fake);
    }
    return yes;
}

// A control's QueryInterface (iid, out): value[0] its legacy face, value[1] its render face (kControls + 99: none).
uint32_t ControlQuery(Obj *self, uint32_t *a)
{
    const bool yes = Draw(6) != 0;
    if (a[0] == 0x9a4527cb && self->value[0] < svcfx::kFakes) return HandOut(self->value[0], a[1], yes ? 0x101 : 0x100);
    if (a[0] == 0x5a356159 && self->value[1] < svcfx::kFakes) return HandOut(self->value[1], a[1], yes ? 0x101 : 0x100);
    if (a[0] == kBindIids[0] || a[0] == kBindIids[1]) return HandOut(self->id - 200, a[1], yes ? 0x101 : 0x100);
    return 0x100;
}
// A render face's GetRect (&rect): writes a rectangle around the origin.
uint32_t RenderRect(Obj *, uint32_t *a)
{
    auto *r = reinterpret_cast<int32_t *>(uintptr_t(a[0]));
    r[0] = int32_t(Draw(20)) - 10;
    r[1] = int32_t(Draw(20)) - 10;
    r[2] = int32_t(Draw(20)) - 10;
    r[3] = int32_t(Draw(20)) - 10;
    return Bool();
}

void BuildFakes(uint32_t seed)
{
    svcfx::ResetAll();
    gRng.seed(seed);
    for (unsigned c = 0; c < kControls; ++c) {
        F(c).value[0] = Draw(4) ? kLegacy + Draw(3) : 99;
        F(c).value[1] = Draw(4) ? kRender + Draw(3) : 99;
        F(c).Set(0, "dp", 0, &ControlQuery);
        F(c).Set(5, 0, Draw(4));
        F(c).Set(7, "d", gRng());
        F(c).Set(8, "pddd", Draw(5) ? 0x101 : 0x100);
    }
    for (unsigned l = kLegacy; l < kLegacy + 3; ++l) F(l).Set(3, 0, Draw(4) ? P(&F(kWindow + Draw(2))) : 0);
    for (unsigned r = kRender; r < kRender + 3; ++r) {
        F(r).Set(3, "p", gRng());
        F(r).Set(5, "p", 0, &RenderRect);
        F(r).Set(6, "pd", Bool());
    }
}

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
        void *object = ours ? static_cast<void *>(Set::Create()) : ctx.Original<void *(__cdecl *)()>(0x85b2c9)();
        b.push_back(object ? 1 : 0);
        return object;
    }
    void *memory = ::operator new(Set::kSize);
    std::memset(memory, 0xc5, Set::kSize);
    void *r = ours ? static_cast<void *>(::new (memory) Set) : ctx.Original<void *(__thiscall *)(void *)>(0x85bfe2)(memory);
    b.push_back(r == memory);
    return memory;
}

constexpr uint32_t kFaceOffsets[2] = {0, 4};

void Dump(Blob &b, void *object, bool ours, const shape::OurTables<2> &tables)
{
    static const uint32_t kGame[2] = {Set::kVTable, Set::kRenderVTable};
    for (unsigned f = 0; f < 2; ++f) {
        const uint32_t w = Word(object, kFaceOffsets[f]);
        b.push_back(ours ? w == P(tables.face[f]) : w == P(GameAddress<void>(kGame[f])));
    }
    const uint32_t begin = Word(object, 8), end = Word(object, 0xc), cap = Word(object, 0x10);
    b.push_back((end - begin) / 4);
    b.push_back((cap - begin) / 4);
    for (uint32_t p = begin; p < end; p += 4) b.push_back(svcfx::Norm(*reinterpret_cast<const uint32_t *>(uintptr_t(p))));
    b.push_back(Word(object, 0x14));
    b.push_back(Word(object, 0x18) != 0);
    b.push_back(Word(object, 0x1c));
    b.push_back(Word(object, 0x20));
}

const uint32_t kIids[] = {1, 0x1a35608a, 0xfa35615f, 0x1a356083, 0};

enum : unsigned {
    opQuery, opAddRef, opControls, opAdd, opAdd2, opFind, opGetAt, opFindWindow, opWindowAt, opOwns, opCount, opBind,
    opRender, opHitTest, opCountOps
};

void RunOp(Blob &b, void *object, unsigned kind)
{
    switch (kind) {
    case opQuery: {
        const uint32_t face = kFaceOffsets[Draw(2)];
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint32_t r = Slot<uint32_t>(Face(object, face), 0, kIids[Draw(5)], &out) & 0xff;
        b.push_back(r);
        b.push_back(svcfx::NormOut(P(out), 0x5a5a5a5a));
        if (r) {
            b.push_back(Slot<uint32_t>(out, 1));
            b.push_back(Slot<uint32_t>(out, 2));
            b.push_back(Slot<uint32_t>(out, 2));
        }
        break;
    }
    case opAddRef: {
        void *face = Face(object, kFaceOffsets[Draw(2)]);
        b.push_back(Slot<uint32_t>(face, 1));
        b.push_back(Slot<uint32_t>(face, 2));
        break;
    }
    case opControls: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 3))); break;
    case opAdd:
    case opAdd2: b.push_back(Slot<uint32_t>(object, 4, Draw(8) ? &F(Draw(kControls)) : nullptr) & 0xff); break;
    case opFind: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 5, Draw(5)))); break;
    case opGetAt: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 6, Draw(10)))); break;
    case opFindWindow: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 7, Draw(5)))); break;
    case opWindowAt: b.push_back(svcfx::Norm(Slot<uint32_t>(object, 8, Draw(10)))); break;
    case opOwns: Slot<void>(object, 9, uint32_t(gRng() & 0xffffff00u) | Draw(2)); break;
    case opCount: b.push_back(Slot<uint32_t>(object, 10)); break;
    case opBind: b.push_back(Slot<uint32_t>(object, 11, kBindIids[Draw(2)], gRng()) & 0xff); break;
    case opRender: b.push_back(Slot<uint32_t>(Face(object, 4), 3, Draw(2) ? &F(kContext) : nullptr) & 0xff); break;
    case opHitTest: {
        int32_t point[2] = {int32_t(Draw(20)) - 10, int32_t(Draw(20)) - 10};
        svcfx::AddRange(point, sizeof(point), 0x7c000000);
        b.push_back(Slot<uint32_t>(Face(object, 4), 4, point, gRng()) & 0xff);
        svcfx::gRanges.pop_back();
        break;
    }
    }
}

} // namespace control_set_test

using namespace control_set_test;

DIFF_TEST(ControlSet_Run, 0x85b2c9)
{
    // Also reached: every RVA in the file comment.
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));

    const shape::OurTables<2> &tables = shape::Ours<Set>(kFaceOffsets);
    svcfx::gEv.clear();

    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 opRng(seed * 7919);
        std::vector<unsigned> ops(1 + opRng() % 24);
        for (unsigned &op : ops) op = opRng() % opCountOps;
        gFactory = seed % 3 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            Blob first;
            void *object = Build(ctx, first, side == 1);
            svcfx::AddRange(object, Set::kSize, kTag);
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
            if (!svcfx::Same(ctx, results[0][i], results[1][i], "ControlSet_Run", seed)) {
                const size_t op = i ? i - 1 : 0;
                ctx.Fail("  at step %u of %u (op %u)", unsigned(i), unsigned(ops.size() + 2),
                         i && op < ops.size() ? ops[op] : 99u);
                seed = 100000;
                break;
            }
        }
    }
}
