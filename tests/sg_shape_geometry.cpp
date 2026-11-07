// Differential tests for the slot bodies of nGZSceneGraph::cGeometryNode and nGZSceneGraph::cShape
// (src/sg/SGShapeGeometry.cpp).
//
// Each case builds one identical world (a byte-for-byte copy from one seed) twice: the original runs on the first
// copy and our function on the second. A world holds a cGeometryNode object, a cShape object, the element array,
// dword arrays, a virtual base table, fake element/cache/interface/target objects and the owner of the interface
// used by ForwardElement. Every virtual call the bodies make goes to a fake that records (kind, object, arguments)
// with pointers as offsets from its own world, so the two sides compare equal when they make the same calls.
// Compared per case: the return value (all of eax unless the original's result is only in al), the log of calls,
// and the observed fields (normalised pointers and the written bytes).
//
// The fakes are __fastcall with a dummy edx, so their callee-clean matches the __thiscall callers. Dispatch8
// (0xaeb800) reads the cache global 0x10f9070, which the test points at the world's cache object; the cache's
// slot 8 is a fake.
#include "difftest.h"
#include "game/vtable.h"
#include "sg/SGShapeGeometry.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Node = nGZSceneGraph::cGeometryNodeObject;
using Secondary8 = nGZSceneGraph::cGeometryNodeSecondary8;
using RenderSub = nGZSceneGraph::cGeometryNodeRenderSubobject;
using ShapeSub = nGZSceneGraph::cShapeIShapeSubobject;

constexpr uint32_t kCacheGlobal = 0x10f9070; // GetCachedObjectB's cache (RVA)
constexpr uint32_t kElemSize = 0x7c;

enum Kind : uint32_t { kP34 = 1, kE0, kC8, kI32, kS47, kS48, kAddRef, kRelease };

struct Event {
    uint32_t kind;
    uint32_t self; // offset from the world under test (or 0xffffffff for null)
    uint32_t v[6];
};

std::vector<Event> gLog;
const uint8_t *gBase = nullptr; // the world under test

uint32_t Off(const void *p)
{
    return p == nullptr ? 0xffffffffu : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gBase));
}

void Log(uint32_t kind, const void *self, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0, uint32_t e = 0,
         uint32_t f = 0)
{
    gLog.push_back(Event{kind, Off(self), {a, b, c, d, e, f}});
}

template <typename T> T Load(const void *base, uint32_t off)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(base) + off, sizeof(v));
    return v;
}

template <typename T> void Store(void *base, uint32_t off, T v)
{
    std::memcpy(static_cast<uint8_t *>(base) + off, &v, sizeof(v));
}

// ---- fakes ----

// Slot 34 of the node's primary vtable (called by SetField124).
void __fastcall FakeSlot34(void *self, void *)
{
    Log(kP34, self);
}

// Slot 0 of an element: records (a1, a2); the result (all of eax, the caller reads al) comes from the element.
// An element with a non-zero dword at +0x18 empties its node's element vector (end = begin) during the call, which
// the original's loop must notice by re-reading the end pointer. The node is at +0x10.
uint32_t __fastcall FakeElementSlot0(void *self, void *, uint32_t a1, uint32_t a2)
{
    Log(kE0, self, a1, a2);
    if (Load<uint32_t>(self, 0x18) != 0) {
        uint8_t *node = Load<uint8_t *>(self, 0x10);
        Store<void *>(node, 0xd8, Load<void *>(node, 0xd4));
    }
    return Load<uint32_t>(self, 8);
}

// Slot 8 of the cached object (Dispatch8's callee).
uint32_t __fastcall FakeCacheSlot8(void *self, void *, uint32_t a)
{
    Log(kC8, self, a);
    return Load<uint32_t>(self, 8);
}

// Slot 32 of the render interface (ForwardElement's callee), with six stack arguments.
uint32_t __fastcall FakeInterfaceSlot32(void *self, void *, uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5,
                                        uint32_t p6)
{
    Log(kI32, self, p1, p2, p3, p4, p5, p6);
    return Load<uint32_t>(self, 8);
}

// Slots 47 and 48 of the pair target (QueryPair's callees).
uint32_t __fastcall FakeTargetSlot47(void *self, void *, uint32_t a)
{
    Log(kS47, self, a);
    return Load<uint32_t>(self, 8);
}

// Returns a different word from slot 47's, so that a swapped out[0]/out[1] is visible.
uint32_t __fastcall FakeTargetSlot48(void *self, void *, uint32_t a)
{
    Log(kS48, self, a);
    return Load<uint32_t>(self, 12);
}

// Slots 1 (AddRef) and 2 (Release) of a refcounted object.
uint32_t __fastcall FakeAddRef(void *self, void *)
{
    Log(kAddRef, self);
    return 0x1234;
}

uint32_t __fastcall FakeRelease(void *self, void *)
{
    Log(kRelease, self);
    return 0x5678;
}

void *gVtPrim[64], *gVtElem[4], *gVtCache[16], *gVtIface[64], *gVtTarget[64], *gVtRef[4], *gVtDummy[64];

void InitFakes()
{
    static bool done = false;
    if (done) return;
    done = true;
    gVtPrim[34] = reinterpret_cast<void *>(&FakeSlot34);
    gVtElem[0] = reinterpret_cast<void *>(&FakeElementSlot0);
    gVtCache[8] = reinterpret_cast<void *>(&FakeCacheSlot8);
    gVtIface[32] = reinterpret_cast<void *>(&FakeInterfaceSlot32);
    gVtTarget[47] = reinterpret_cast<void *>(&FakeTargetSlot47);
    gVtTarget[48] = reinterpret_cast<void *>(&FakeTargetSlot48);
    gVtRef[1] = reinterpret_cast<void *>(&FakeAddRef);
    gVtRef[2] = reinterpret_cast<void *>(&FakeRelease);
}

// ---- the world ----

struct World {
    uint8_t obj[0x200];   // cGeometryNode: primary at +0, secondary at +8, render subobject at +0x40
    uint8_t shape[0x100]; // cShape: cIShape subobject at +0x2c
    uint8_t arr[0x400];   // element array, elements 0x7c bytes apart
    uint8_t words[0x40];  // dword arrays for the Dispatch8 bodies (base +0x10c and +0x118)
    uint8_t vbtable[8];   // virtual base table: [0] = 0, [1] = displacement
    uint8_t elems[8][0x20];
    void *elemPtrs[8]; // the +0xd4 vector
    uint8_t cache[0x20];
    uint8_t iface[0x20];
    uint8_t owner[0x300]; // owns the render interface at +0x2a8
    uint8_t target[0x20];
    uint8_t refNew[0x20];
    uint8_t refOld[0x20];
    uint8_t records[4][0x20]; // shape record vector
    uint8_t outBuf[8];
};

// Byte-identical worlds from one seed, then the common pointers: vtables, arrays, and the vector of elements.
void FillWorld(World &w, uint32_t seed)
{
    std::mt19937 gen(seed);
    uint8_t *bytes = reinterpret_cast<uint8_t *>(&w);
    for (size_t i = 0; i < sizeof(w); ++i) bytes[i] = static_cast<uint8_t>(gen());

    Store<void *>(w.obj, 0x00, gVtPrim);
    Store<void *>(w.obj, 0x08, gVtDummy);
    Store<void *>(w.obj, 0x40, gVtDummy);
    // The secondary at +8 reaches the cSGResource secondary subobject at +0x44 through its vbptr at +0x30.
    w.vbtable[0] = 0;
    w.vbtable[1] = 0x80;
    Store<void *>(w.obj, 0x30, w.vbtable);
    Store<void *>(w.obj, 0x78, w.arr);
    Store<void *>(w.obj, 0x10c, w.words);
    Store<void *>(w.obj, 0x118, w.words + 0x20);
    Store<void *>(w.obj, 0xd4, &w.elemPtrs[0]);
    Store<void *>(w.obj, 0xd8, &w.elemPtrs[2]);
    for (int i = 0; i < 8; ++i) {
        w.elemPtrs[i] = w.elems[i];
        Store<void *>(w.elems[i], 0, gVtElem);
        Store<void *>(w.elems[i], 0x10, w.obj); // the node (used only by the emptying element)
        Store<uint32_t>(w.elems[i], 0x18, 0);
    }
    Store<void *>(w.obj, 0xe0, nullptr);

    Store<void *>(w.owner, 0x2a8, w.iface);
    Store<void *>(w.iface, 0, gVtIface);
    Store<void *>(w.cache, 0, gVtCache);
    Store<void *>(w.target, 0, gVtTarget);
    Store<void *>(w.refNew, 0, gVtRef);
    Store<void *>(w.refOld, 0, gVtRef);

    // cIShape: begin at +0x20 (relative to the subobject), end at +0x24.
    Store<void *>(w.shape, 0x2c + 0x20, &w.records[0]);
    Store<void *>(w.shape, 0x2c + 0x24, &w.records[1]);
}

// Result of one side: the return value (as a 64-bit value), the call log and the observed words.
struct Outcome {
    uint64_t ret = 0;
    std::vector<Event> log;
    std::vector<uint32_t> obs;
};

template <typename Setup, typename Call, typename Observe>
Outcome RunSide(difftest::Context &ctx, World &w, uint32_t seed, Setup setup, Call call, Observe observe)
{
    FillWorld(w, seed);
    setup(w);
    gBase = reinterpret_cast<const uint8_t *>(&w);
    *ctx.image.At<void *>(kCacheGlobal) = w.cache;
    gLog.clear();
    Outcome out;
    out.ret = call(w);
    out.log = gLog;
    out.obs = observe(w);
    return out;
}

void Check(difftest::Context &ctx, const char *name, uint32_t seed, const Outcome &a, const Outcome &b)
{
    ++ctx.cases;
    bool same = a.ret == b.ret && a.obs == b.obs && a.log.size() == b.log.size();
    for (size_t i = 0; same && i < a.log.size(); ++i) same = std::memcmp(&a.log[i], &b.log[i], sizeof(Event)) == 0;
    if (!same) {
        ctx.Fail("%s seed %u: ret %llx/%llx, calls %zu/%zu, observed %zu/%zu words", name, seed,
                 static_cast<unsigned long long>(a.ret), static_cast<unsigned long long>(b.ret), a.log.size(), b.log.size(),
                 a.obs.size(), b.obs.size());
    }
}

auto NoObs = [](World &) { return std::vector<uint32_t>(); };

} // namespace

// 0xa431c0 (8 bytes, a thunk: add ecx,0x3c; jmp VirtualBaseIfObject). The secondary at +8 reaches the cSGResource
// subobject at +0x44 and returns its virtual base (object +0x80 past the vbptr's table).
DIFF_TEST(sg_geom_virtual_base_of_resource, 0xa431c0)
{
    InitFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa431c0);
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        World wa, wb;
        auto setup = [](World &) {};
        auto obs = [](World &) { return std::vector<uint32_t>(); };
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) { return static_cast<uint64_t>(Off(orig(w.obj + 8))); }, obs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(Off(reinterpret_cast<Secondary8 *>(w.obj + 8)->VirtualBaseOfResource()));
        }, obs);
        Check(ctx, "sg_geom_virtual_base_of_resource", seed, a, b);
    }
}

// 0xa42980 (`ret 4`, 19 bytes): Dispatch8 on the element's dword at +0x38.
DIFF_TEST(sg_geom_dispatch8_of_element, 0xa42980)
{
    InitFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xa42980);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const uint32_t index = seed % 8;
        World wa, wb;
        auto setup = [](World &) {};
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) { return static_cast<uint64_t>(orig(w.obj, index)); }, NoObs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(reinterpret_cast<Node *>(w.obj)->Dispatch8OfElement(index));
        }, NoObs);
        Check(ctx, "sg_geom_dispatch8_of_element", seed, a, b);
    }
}

// 0xa42800 (`ret 4`, 21 bytes): Dispatch8 on the word at base(+0x10c) + 4 * index.
DIFF_TEST(sg_geom_dispatch8_of_word_10c, 0xa42800)
{
    InitFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xa42800);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const uint32_t index = seed % 8;
        World wa, wb;
        auto setup = [](World &) {};
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) { return static_cast<uint64_t>(orig(w.obj, index)); }, NoObs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(reinterpret_cast<Node *>(w.obj)->Dispatch8OfWordAt10c(index));
        }, NoObs);
        Check(ctx, "sg_geom_dispatch8_of_word_10c", seed, a, b);
    }
}

// 0xa427e0 (`ret 4`, 21 bytes): as above with the base at +0x118.
DIFF_TEST(sg_geom_dispatch8_of_word_118, 0xa427e0)
{
    InitFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xa427e0);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const uint32_t index = seed % 8;
        World wa, wb;
        auto setup = [](World &) {};
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) { return static_cast<uint64_t>(orig(w.obj, index)); }, NoObs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(reinterpret_cast<Node *>(w.obj)->Dispatch8OfWordAt118(index));
        }, NoObs);
        Check(ctx, "sg_geom_dispatch8_of_word_118", seed, a, b);
    }
}

// 0xa429a0 (`ret 4`, 15 bytes): the dword at +0x38 of element `index`.
DIFF_TEST(sg_geom_element_word_38, 0xa429a0)
{
    InitFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xa429a0);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const uint32_t index = seed % 8;
        World wa, wb;
        auto setup = [](World &) {};
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) { return static_cast<uint64_t>(orig(w.obj, index)); }, NoObs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(reinterpret_cast<Node *>(w.obj)->ElementWordAt38(index));
        }, NoObs);
        Check(ctx, "sg_geom_element_word_38", seed, a, b);
    }
}

// 0xa42970 (`ret 4`, 11 bytes), on the cIGeometryRender subobject at +0x40: element `index` as a pointer.
DIFF_TEST(sg_geom_render_element_pointer, 0xa42970)
{
    InitFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xa42970);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const uint32_t index = seed % 8;
        World wa, wb;
        auto setup = [](World &) {};
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) { return static_cast<uint64_t>(Off(orig(w.obj + 0x40, index))); }, NoObs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(Off(reinterpret_cast<RenderSub *>(w.obj + 0x40)->ElementPointer(index)));
        }, NoObs);
        Check(ctx, "sg_geom_render_element_pointer", seed, a, b);
    }
}

// 0xa43510 (`ret 4`, 29 bytes): stores the argument in +0x124 when it differs, then calls the node's slot 34.
DIFF_TEST(sg_geom_set_field_124, 0xa43510)
{
    InitFakes();
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xa43510);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const uint32_t value = seed % 3 == 0 ? 0x5a5a0000u + seed : seed; // some equal to the stored field
        auto setup = [&](World &w) {
            if (seed % 2 == 0) Store<uint32_t>(w.obj, 0x124, value);
        };
        auto obs = [](World &w) { return std::vector<uint32_t>{Load<uint32_t>(w.obj, 0x124)}; };
        World wa, wb;
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) {
            orig(w.obj, value);
            return uint64_t{0};
        }, obs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            reinterpret_cast<Node *>(w.obj)->SetField124(value);
            return uint64_t{0};
        }, obs);
        Check(ctx, "sg_geom_set_field_124", seed, a, b);
    }
}

// 0xa42ac0 (`ret 8`, 75 bytes): the first element whose slot 0 returns non-zero in al. The vector's end is re-read
// after each call, as the original does.
DIFF_TEST(sg_geom_any_element_accepts, 0xa42ac0)
{
    InitFakes();
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(0xa42ac0);
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        const uint32_t count = seed % 9; // 0..8 elements, including empty
        const uint32_t a1 = seed * 0x9e3779b9u, a2 = seed * 0x85ebca6bu;
        // Every fourth case: the first element empties the vector when called, so a loop that kept the old end
        // would make a further call (logged) that the original does not make.
        const bool shrinks = seed % 4 == 0 && count > 1;
        auto setup = [&](World &w) {
            Store<void *>(w.obj, 0xd4, &w.elemPtrs[0]);
            Store<void *>(w.obj, 0xd8, &w.elemPtrs[count]);
            if (shrinks) Store<uint32_t>(w.elems[0], 0x18, 1);
        };
        World wa, wb;
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(orig(w.obj, a1, a2));
        }, NoObs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(reinterpret_cast<Node *>(w.obj)->AnyElementAccepts(a1, a2));
        }, NoObs);
        Check(ctx, "sg_geom_any_element_accepts", seed, a, b);
    }
}

// 0xa43530 (`ret 4`, 103 bytes): replaces the pointer at +0xe0 (AddRef new before the store, Release old after it) and
// zeroes the 16 bytes at +0xe4.
DIFF_TEST(sg_geom_replace_ref_e0, 0xa43530)
{
    InitFakes();
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xa43530);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        // 0 null, 1 refOld, 2 refNew: old and new are picked independently, so equal pairs occur.
        const uint32_t oldKind = seed % 3, newKind = (seed / 3) % 3;
        auto pick = [](World &w, uint32_t kind) -> void * {
            return kind == 0 ? nullptr : kind == 1 ? static_cast<void *>(w.refOld) : static_cast<void *>(w.refNew);
        };
        auto setup = [&](World &w) { Store<void *>(w.obj, 0xe0, pick(w, oldKind)); };
        auto obs = [](World &w) {
            std::vector<uint32_t> v{Off(Load<void *>(w.obj, 0xe0))};
            for (uint32_t i = 0; i < 4; ++i) v.push_back(Load<uint32_t>(w.obj, 0xe4 + 4 * i));
            return v;
        };
        World wa, wb;
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) {
            orig(w.obj, pick(w, newKind));
            return uint64_t{0};
        }, obs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            reinterpret_cast<Node *>(w.obj)->ReplaceRefE0(pick(w, newKind));
            return uint64_t{0};
        }, obs);
        Check(ctx, "sg_geom_replace_ref_e0", seed, a, b);
    }
}

// 0xa423c0 (`ret 0xc`, 46 bytes): slot 32 of owner->+0x2a8 with the element's fields, the argument and a zero.
DIFF_TEST(sg_geom_render_forward_element, 0xa423c0)
{
    InitFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void *, uint32_t)>(0xa423c0);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const uint32_t index = seed % 8, arg = seed * 0x27d4eb2fu;
        World wa, wb;
        auto setup = [](World &) {};
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(orig(w.obj + 0x40, index, w.owner, arg));
        }, NoObs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(reinterpret_cast<RenderSub *>(w.obj + 0x40)->ForwardElement(index, w.owner, arg));
        }, NoObs);
        Check(ctx, "sg_geom_render_forward_element", seed, a, b);
    }
}

// 0xa5d420 (15 bytes, no arguments): the dword at +4 of the record at +0x20 when +0x20 != +0x24, else 0.
DIFF_TEST(sg_shape_first_record_word, 0xa5d420)
{
    InitFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xa5d420);
    for (uint32_t seed = 1; seed <= 100; ++seed) {
        const uint32_t mode = seed % 3; // 0 empty, 1 one record, 2 record with a null end
        auto setup = [&](World &w) {
            Store<void *>(w.shape, 0x2c + 0x20, &w.records[0]);
            Store<void *>(w.shape, 0x2c + 0x24, mode == 0 ? &w.records[0] : mode == 1 ? &w.records[1] : nullptr);
        };
        World wa, wb;
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) { return static_cast<uint64_t>(orig(w.shape + 0x2c)); }, NoObs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(reinterpret_cast<ShapeSub *>(w.shape + 0x2c)->FirstRecordWord());
        }, NoObs);
        Check(ctx, "sg_shape_first_record_word", seed, a, b);
    }
}

// 0xa5e170 (`ret 8`, 54 bytes): out[0] = target slot 47 (arg), out[1] = target slot 48 (arg); returns out.
DIFF_TEST(sg_shape_query_pair, 0xa5e170)
{
    InitFakes();
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0xa5e170);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        const uint32_t arg = seed * 0x6c8e9cf5u;
        auto setup = [](World &w) {
            Store<void *>(w.records[0], 4, w.target); // the record's +4 is the target
            Store<void *>(w.shape, 0x2c + 0x20, &w.records[0]);
        };
        auto obs = [](World &w) {
            return std::vector<uint32_t>{Load<uint32_t>(w.outBuf, 0), Load<uint32_t>(w.outBuf, 4)};
        };
        World wa, wb;
        Outcome a = RunSide(ctx, wa, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(Off(orig(w.shape + 0x2c, w.outBuf, arg)));
        }, obs);
        Outcome b = RunSide(ctx, wb, seed, setup, [&](World &w) {
            return static_cast<uint64_t>(Off(reinterpret_cast<ShapeSub *>(w.shape + 0x2c)->QueryPair(w.outBuf, arg)));
        }, obs);
        Check(ctx, "sg_shape_query_pair", seed, a, b);
    }
}
