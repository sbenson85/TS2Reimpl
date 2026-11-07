// Differential tests for cGZWin's accessors and field helpers (the window base class). Each case builds one
// random window state, copies it, runs the original on one copy and our function on the other, and compares
// the result, the whole window body, and the output buffer. The output buffer sits inside the case, between
// canaries, so writes outside the four dwords the function may touch are caught.
//
// The functions here make no calls (no vtable or import use), so the window is a plain byte block; the
// original reads its fields at the offsets documented in src/gz/cGZWin.h.
#include "difftest.h"
#include "gz/cGZWin.h"
#include "hook/hooks.h"
#include "rz/RZAutoRef.h"

#include <array>
#include <climits>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <random>
#include <vector>

namespace {

struct Case {
    uint8_t front[32];
    uint8_t obj[0x100]; // the window body; every field used is below +0xf8
    uint8_t back[32];
    uint8_t out[48];    // out[0..16] and out[32..48] are canaries; out[16..32] is the output area
};

uint8_t *OutArea(Case &c)
{
    return c.out + 16;
}

void Randomize(Case &c, std::mt19937 &rng)
{
    for (auto &b : c.obj) b = static_cast<uint8_t>(rng());
    for (auto &b : c.out) b = static_cast<uint8_t>(rng());
    std::memset(c.front, 0xA5, sizeof(c.front));
    std::memset(c.back, 0x5A, sizeof(c.back));
    std::memset(c.out, 0xC3, 16);
    std::memset(c.out + 32, 0x3C, 16);
}

// Writes a 32-bit field at an offset of the window body.
void Set32(Case &c, uint32_t offset, uint32_t value)
{
    std::memcpy(c.obj + offset, &value, sizeof(value));
}

// Interesting values for signed arithmetic: the edges plus random bits.
uint32_t Edge(std::mt19937 &rng, int i)
{
    static const uint32_t kEdges[] = {0u, 1u, 0xffffffffu, 0x7fffffffu, 0x80000000u, 0x80000001u, 0x0000ffffu,
                                      0xff000000u, 0x00ff00ffu};
    if (i < static_cast<int>(sizeof(kEdges) / sizeof(kEdges[0])) && i % 2 == 0) return kEdges[i];
    return static_cast<uint32_t>(rng());
}

bool CanariesIntact(const Case &c)
{
    for (uint8_t b : c.front)
        if (b != 0xA5) return false;
    for (uint8_t b : c.back)
        if (b != 0x5A) return false;
    for (int i = 0; i < 16; ++i) {
        if (c.out[i] != 0xC3 || c.out[32 + i] != 0x3C) return false;
    }
    return true;
}

// Compares a pair of runs: results, whole case (window body, output area, canaries).
void Check(difftest::Context &ctx, const char *name, const Case &a, const Case &b, uint64_t ra, uint64_t rb)
{
    ++ctx.cases;
    if (ra != rb) ctx.Fail("%s: result original %llx, ours %llx", name, static_cast<unsigned long long>(ra),
                           static_cast<unsigned long long>(rb));
    if (std::memcmp(&a, &b, sizeof(Case)) != 0) ctx.Fail("%s: window or output state differs", name);
    if (!CanariesIntact(a)) ctx.Fail("%s: original overwrote a canary", name);
    if (!CanariesIntact(b)) ctx.Fail("%s: our code overwrote a canary", name);
}

} // namespace

// 0x931862: byte at +0x8c = 1
DIFF_TEST(cGZWin_SetFlag8C, 0x931862)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x931862);
    std::mt19937 rng(9001);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        b = a;
        orig(a.obj);
        reinterpret_cast<cGZWin *>(b.obj)->SetFlag8C();
        Check(ctx, "SetFlag8C", a, b, 0, 0);
    }
}

// 0x931727: (+0xf4) | 0xff000000
DIFF_TEST(cGZWin_GetOpaqueF4, 0x931727)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x931727);
    std::mt19937 rng(9002);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        Set32(a, 0xf4, Edge(rng, i));
        b = a;
        uint32_t ra = orig(a.obj);
        uint32_t rb = reinterpret_cast<cGZWin *>(b.obj)->GetOpaqueF4();
        Check(ctx, "GetOpaqueF4", a, b, ra, rb);
    }
}

// 0x9317b7: +0xd0 - +0xc8 (wrapping), 0x93174e: +0xd4 - +0xcc
DIFF_TEST(cGZWin_GetWidth, 0x9317b7)
{
    auto orig = ctx.Original<int32_t(__thiscall *)(void *)>(0x9317b7);
    std::mt19937 rng(9003);
    for (int i = 0; i < 400; ++i) {
        Case a, b;
        Randomize(a, rng);
        Set32(a, 0xc8, Edge(rng, i));
        Set32(a, 0xd0, Edge(rng, i / 2 + 3));
        b = a;
        int32_t ra = orig(a.obj);
        int32_t rb = reinterpret_cast<cGZWin *>(b.obj)->GetWidth();
        Check(ctx, "GetWidth", a, b, static_cast<uint32_t>(ra), static_cast<uint32_t>(rb));
    }
}

DIFF_TEST(cGZWin_GetHeight, 0x93174e)
{
    auto orig = ctx.Original<int32_t(__thiscall *)(void *)>(0x93174e);
    std::mt19937 rng(9004);
    for (int i = 0; i < 400; ++i) {
        Case a, b;
        Randomize(a, rng);
        Set32(a, 0xcc, Edge(rng, i));
        Set32(a, 0xd4, Edge(rng, i / 2 + 3));
        b = a;
        int32_t ra = orig(a.obj);
        int32_t rb = reinterpret_cast<cGZWin *>(b.obj)->GetHeight();
        Check(ctx, "GetHeight", a, b, static_cast<uint32_t>(ra), static_cast<uint32_t>(rb));
    }
}

// 0x933038: +0xf0 = arg
DIFF_TEST(cGZWin_SetF0, 0x933038)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x933038);
    std::mt19937 rng(9005);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        uint32_t value = Edge(rng, i);
        b = a;
        orig(a.obj, value);
        reinterpret_cast<cGZWin *>(b.obj)->SetF0(value);
        Check(ctx, "SetF0", a, b, 0, 0);
    }
}

// 0x931792: *out = +0xf0
DIFF_TEST(cGZWin_GetF0, 0x931792)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0x931792);
    std::mt19937 rng(9006);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        Set32(a, 0xf0, Edge(rng, i));
        b = a;
        orig(a.obj, OutArea(a));
        reinterpret_cast<cGZWin *>(b.obj)->GetF0(reinterpret_cast<uint32_t *>(OutArea(b)));
        Check(ctx, "GetF0", a, b, 0, 0);
    }
}

// 0x931733: setne al of (+0xe8 & mask). Only al is the result (the original's eax keeps the mask's bits).
DIFF_TEST(cGZWin_TestFlagsE8, 0x931733)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x931733);
    std::mt19937 rng(9007);
    for (int i = 0; i < 400; ++i) {
        Case a, b;
        Randomize(a, rng);
        Set32(a, 0xe8, i % 3 == 0 ? 0u : Edge(rng, i));
        uint32_t mask = i % 4 == 0 ? 0u : Edge(rng, i + 1);
        b = a;
        uint8_t ra = orig(a.obj, mask);
        uint8_t rb = reinterpret_cast<cGZWin *>(b.obj)->TestFlagsE8(mask) ? 1 : 0;
        Check(ctx, "TestFlagsE8", a, b, ra, rb);
    }
}

// 0x931716: *out = +0xf4; returns al = 1
DIFF_TEST(cGZWin_GetF4, 0x931716)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x931716);
    std::mt19937 rng(9008);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        Set32(a, 0xf4, Edge(rng, i));
        b = a;
        uint8_t ra = orig(a.obj, OutArea(a));
        uint8_t rb = reinterpret_cast<cGZWin *>(b.obj)->GetF4(reinterpret_cast<uint32_t *>(OutArea(b))) ? 1 : 0;
        Check(ctx, "GetF4", a, b, ra, rb);
    }
}

// 0x9311ac: four dwords from +0x24 to the output
DIFF_TEST(cGZWin_CopyRectA, 0x9311ac)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0x9311ac);
    std::mt19937 rng(9009);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        b = a;
        orig(a.obj, OutArea(a));
        reinterpret_cast<cGZWin *>(b.obj)->CopyRectA(reinterpret_cast<int32_t *>(OutArea(b)));
        Check(ctx, "CopyRectA", a, b, 0, 0);
    }
}

// 0x931197: four dwords from +0xc8 to the output
DIFF_TEST(cGZWin_CopyRectB, 0x931197)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0x931197);
    std::mt19937 rng(9010);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        b = a;
        orig(a.obj, OutArea(a));
        reinterpret_cast<cGZWin *>(b.obj)->CopyRectB(reinterpret_cast<int32_t *>(OutArea(b)));
        Check(ctx, "CopyRectB", a, b, 0, 0);
    }
}

// 0x933327: *x += +0x24 and *y += +0x28 (wrapping); the two pointers are in the output area
DIFF_TEST(cGZWin_AddOffset, 0x933327)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0x933327);
    std::mt19937 rng(9011);
    for (int i = 0; i < 400; ++i) {
        Case a, b;
        Randomize(a, rng);
        Set32(a, 0x24, Edge(rng, i));
        Set32(a, 0x28, Edge(rng, i / 2 + 1));
        uint32_t x = Edge(rng, i + 5), y = Edge(rng, i + 7);
        std::memcpy(OutArea(a), &x, 4);
        std::memcpy(OutArea(a) + 4, &y, 4);
        b = a;
        orig(a.obj, OutArea(a), OutArea(a) + 4);
        reinterpret_cast<cGZWin *>(b.obj)->AddOffset(reinterpret_cast<int32_t *>(OutArea(b)),
                                                      reinterpret_cast<int32_t *>(OutArea(b) + 4));
        Check(ctx, "AddOffset", a, b, 0, 0);
    }
}

// 0x931c61: (hi << 16) | (word)lo. The original reads only the low word of the first stack argument, so the
// test passes dirty high bits there. __stdcall: the callee pops 8 bytes.
DIFF_TEST(cGZWin_PackWords, 0x931c61)
{
    using Fn = uint32_t(__stdcall *)(uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x931c61);
    auto ours = reinterpret_cast<Fn>(&cGZWin::PackWords);
    std::mt19937 rng(9012);
    for (int i = 0; i < 400; ++i) {
        uint32_t lo = Edge(rng, i), hi = Edge(rng, i + 3);
        uint32_t ra = orig(lo, hi);
        uint32_t rb = ours(lo, hi);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("PackWords(%08x, %08x): original %08x, ours %08x", lo, hi, ra, rb);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Functions that make virtual calls and touch containers. The window carries a copy of the original vtable
// with some slots replaced by recording fakes, so the calls the original and ours make are compared as a
// log (slot, object, arguments). Objects are tagged: the window is 0, the others have an id. Fakes use
// __fastcall(self, edx, stack args...), which pops its stack arguments like a thiscall callee.
namespace {

struct Obj {
    void *vtable;
    int id;
    bool flag; // slot 82 of the object returns this
};

struct Event {
    int slot;
    int tag;
    uint32_t a, b;
    bool operator==(const Event &o) const { return slot == o.slot && tag == o.tag && a == o.a && b == o.b; }
};

std::vector<Event> gLog;
const void *gWindow = nullptr;  // the window of the current run (tag 0)
Obj *gChildren[4];              // the window's slot 39 returns gChildren[key % 4] (null entries allowed)

int TagOf(const void *self)
{
    if (self == gWindow) return 0;
    return static_cast<const Obj *>(self)->id;
}

void Record(int slot, const void *self, uint32_t a = 0, uint32_t b = 0)
{
    gLog.push_back({slot, TagOf(self), a, b});
}

uint32_t __fastcall FakeRelease(void *self, void *) // slot 2 of every object
{
    Record(2, self);
    return 0;
}
void __fastcall FakeSlot28(void *self, void *, void *arg) // the parent's slot 28
{
    Record(28, self, static_cast<uint32_t>(TagOf(arg)));
}
bool __fastcall FakeSlot82(void *self, void *, uint32_t mask) // a child's TestFlagsE8
{
    Record(82, self, mask);
    return static_cast<Obj *>(self)->flag;
}
void __fastcall FakeSlot83(void *self, void *, uint32_t a, uint32_t b)
{
    Record(83, self, a, b);
}
void __fastcall FakeSlot105(void *self, void *)
{
    Record(105, self);
}

void __fastcall WinSlot23(void *self, void *)
{
    Record(23, self);
}
void *__fastcall WinSlot39(void *self, void *, uint32_t key)
{
    Record(39, self, key);
    return gChildren[key % 4];
}
void __fastcall WinSlot83(void *self, void *, uint32_t a, uint32_t b)
{
    Record(83, self, a, b);
}
void __fastcall WinSlot85(void *self, void *)
{
    Record(85, self);
}
void __fastcall WinSlot113(void *self, void *, uint32_t a)
{
    Record(113, self, a);
}
void __fastcall WinSlot97(void *self, void *)
{
    Record(97, self);
}
void __fastcall WinSlot109(void *self, void *)
{
    Record(109, self);
}

// The window vtable: a copy of the original with slots 23, 39, 83, 85, 97, 109 and 113 replaced. Slot 82 stays the
// original TestFlagsE8, which is pure. The child vtable has slots 2, 28, 82, 83 and 105.
struct Fakes {
    void *winVt[173];
    void *childVt[110];

    explicit Fakes(const difftest::Context &ctx)
    {
        std::memcpy(winVt, ctx.image.At(cGZWin::kVTable), sizeof(winVt));
        winVt[23] = reinterpret_cast<void *>(&WinSlot23);
        winVt[39] = reinterpret_cast<void *>(&WinSlot39);
        winVt[83] = reinterpret_cast<void *>(&WinSlot83);
        winVt[85] = reinterpret_cast<void *>(&WinSlot85);
        winVt[113] = reinterpret_cast<void *>(&WinSlot113);
        winVt[97] = reinterpret_cast<void *>(&WinSlot97);
        winVt[109] = reinterpret_cast<void *>(&WinSlot109);
        std::memset(childVt, 0, sizeof(childVt));
        childVt[2] = reinterpret_cast<void *>(&FakeRelease);
        childVt[28] = reinterpret_cast<void *>(&FakeSlot28);
        childVt[82] = reinterpret_cast<void *>(&FakeSlot82);
        childVt[83] = reinterpret_cast<void *>(&FakeSlot83);
        childVt[105] = reinterpret_cast<void *>(&FakeSlot105);
    }
};

// Object 0 is the parent (+4), 1 the owner (+0x94), 2..7 the map values and children.
struct Objs {
    std::array<Obj, 8> o;
    explicit Objs(Fakes &f)
    {
        for (int j = 0; j < 8; ++j) o[j] = Obj{f.childVt, j + 1, false};
    }
};

void SetWord(Case &c, uint32_t offset, const void *p)
{
    std::memcpy(c.obj + offset, &p, sizeof(p));
}

// The containers at +0x98 (map), +0xa0 and +0xa8 (lists). Each side builds its own, so clearing one side
// cannot free nodes the other side reads.
struct Plan {
    std::vector<uint32_t> keys;
    std::vector<void *> values;
    std::vector<void *> list0, list1; // reference holders (null allowed)
};
using MapT = std::map<uint32_t, void *>;
using ListT = std::list<RZAutoRef>;

void BuildContainers(uint8_t *obj, const Plan &p)
{
    auto *map = new (obj + 0x98) MapT();
    for (size_t j = 0; j < p.keys.size(); ++j) map->emplace(p.keys[j], p.values[j]);
    auto *l0 = new (obj + 0xa0) ListT();
    for (void *v : p.list0) l0->push_back(RZAutoRef{v});
    auto *l1 = new (obj + 0xa8) ListT();
    for (void *v : p.list1) l1->push_back(RZAutoRef{v});
}

bool ContainersEmpty(const uint8_t *obj)
{
    return reinterpret_cast<const MapT *>(obj + 0x98)->empty() && reinterpret_cast<const ListT *>(obj + 0xa0)->empty() &&
           reinterpret_cast<const ListT *>(obj + 0xa8)->empty();
}

void DestroyContainers(uint8_t *obj)
{
    reinterpret_cast<MapT *>(obj + 0x98)->~MapT();
    reinterpret_cast<ListT *>(obj + 0xa0)->~ListT();
    reinterpret_cast<ListT *>(obj + 0xa8)->~ListT();
}

// Compares two runs: results, the call logs, the window bodies (except [skipFrom, skipTo), where containers
// live and differ only in heap addresses), the output area and the canaries.
void CheckRun(difftest::Context &ctx, const char *name, const Case &a, const Case &b, uint64_t ra, uint64_t rb,
              const std::vector<Event> &la, const std::vector<Event> &lb, uint32_t skipFrom = 0, uint32_t skipTo = 0)
{
    ++ctx.cases;
    if (ra != rb) ctx.Fail("%s: result original %llx, ours %llx", name, static_cast<unsigned long long>(ra),
                           static_cast<unsigned long long>(rb));
    if (la != lb) ctx.Fail("%s: calls differ (original %zu calls, ours %zu)", name, la.size(), lb.size());
    if (std::memcmp(a.obj, b.obj, skipFrom) != 0 || std::memcmp(a.obj + skipTo, b.obj + skipTo, sizeof(a.obj) - skipTo) != 0 ||
        std::memcmp(a.out, b.out, sizeof(a.out)) != 0 || std::memcmp(a.front, b.front, sizeof(a.front)) != 0 ||
        std::memcmp(a.back, b.back, sizeof(a.back)) != 0)
        ctx.Fail("%s: window or output state differs", name);
    if (!CanariesIntact(a)) ctx.Fail("%s: original overwrote a canary", name);
    if (!CanariesIntact(b)) ctx.Fail("%s: our code overwrote a canary", name);
}

uint32_t RandomLow(std::mt19937 &rng)
{
    static const uint8_t kLow[] = {0, 1, 2, 0x80, 0xff};
    return (static_cast<uint32_t>(rng()) & 0xffffff00u) | kLow[rng() % 5];
}

} // namespace

// 0x9330c1: slot 5, Teardown. Covers both early-outs (flag 0x4000 clear), the parent path (flag 0x2000),
// a present or absent owner, and maps and lists of 0 to 6 entries, with null values.
DIFF_TEST(cGZWin_Teardown, 0x9330c1)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(0x9330c1);
    Fakes fakes(ctx);
    Objs objs(fakes);
    std::mt19937 rng(9013);
    for (int i = 0; i < 400; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        SetWord(a, 0x04, &objs.o[0]);
        SetWord(a, 0x94, (i % 3 == 0) ? nullptr : &objs.o[1]);
        uint32_t e8 = static_cast<uint32_t>(rng());
        e8 = (i % 4 == 0) ? (e8 & ~0x4000u) : (e8 | 0x4000u);
        e8 = (i % 2) ? (e8 | 0x2000u) : (e8 & ~0x2000u);
        Set32(a, 0xe8, e8);
        b = a;

        Plan plan;
        int n = static_cast<int>(rng() % 7);
        for (int j = 0; j < n; ++j) {
            plan.keys.push_back(static_cast<uint32_t>(rng() % 50));
            plan.values.push_back((rng() % 4 == 0) ? nullptr : &objs.o[2 + rng() % 6]);
        }
        for (int j = static_cast<int>(rng() % 4); j > 0; --j)
            plan.list0.push_back((rng() % 5 == 0) ? nullptr : &objs.o[2 + rng() % 6]);
        for (int j = static_cast<int>(rng() % 4); j > 0; --j)
            plan.list1.push_back((rng() % 5 == 0) ? nullptr : &objs.o[2 + rng() % 6]);
        BuildContainers(a.obj, plan);
        BuildContainers(b.obj, plan);

        gWindow = a.obj;
        gLog.clear();
        uint8_t ra = orig(a.obj);
        std::vector<Event> la = gLog;
        gLog.clear();
        gWindow = b.obj;
        uint8_t rb = reinterpret_cast<cGZWin *>(b.obj)->Teardown() ? 1 : 0;
        std::vector<Event> lb = gLog;

        // With flag 0x4000 clear both return at once and leave the containers alone (no clear is expected).
        bool tearingDown = (e8 & 0x4000u) != 0;
        if (tearingDown && (!ContainersEmpty(a.obj) || !ContainersEmpty(b.obj)))
            ctx.Fail("Teardown: a container was not cleared");
        CheckRun(ctx, "Teardown", a, b, ra, rb, la, lb, 0x98, 0xb0);
        DestroyContainers(a.obj);
        DestroyContainers(b.obj);
    }
}

// 0x933f35: this->vf83(0x4000, value)
DIFF_TEST(cGZWin_SetFlag4000, 0x933f35)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x933f35);
    Fakes fakes(ctx);
    std::mt19937 rng(9014);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        b = a;
        uint32_t value = Edge(rng, i);
        gWindow = a.obj;
        gLog.clear();
        orig(a.obj, value);
        std::vector<Event> la = gLog;
        gLog.clear();
        gWindow = b.obj;
        reinterpret_cast<cGZWin *>(b.obj)->SetFlag4000(value);
        std::vector<Event> lb = gLog;
        CheckRun(ctx, "SetFlag4000", a, b, 0, 0, la, lb);
    }
}

// 0x933b29: return this->vf82(0x4000). The original's eax is 0x4000 with the low byte set to the result, so
// only al is compared (the callers test al).
DIFF_TEST(cGZWin_TestFlag4000, 0x933b29)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(0x933b29);
    Fakes fakes(ctx);
    std::mt19937 rng(9015);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        // Bit 0x4000 set in about half the cases. Slot 82 is the original TestFlagsE8 here, so this test is
        // about the wrapper's call; slot 82 is tested on its own (cGZWin_TestFlagsE8).
        uint32_t e8 = static_cast<uint32_t>(rng());
        Set32(a, 0xe8, (i % 2) ? (e8 | 0x4000u) : (e8 & ~0x4000u));
        b = a;
        uint8_t ra = orig(a.obj);
        uint8_t rb = reinterpret_cast<cGZWin *>(b.obj)->TestFlag4000() ? 1 : 0;
        CheckRun(ctx, "TestFlag4000", a, b, ra, rb, {}, {});
    }
}

// 0x9352d8 (cdecl): child = window->vf39(key); if child: flag = child->vf82(1); if (flag != low byte of value)
// { child->vf83(1, value); if (low byte of notify) child->vf105(); }. The value's low byte is biased to 0 or 1
// so the equal path is taken often, and the upper bits differ from the flag.
DIFF_TEST(cGZWin_SetChildFlagNotify, 0x9352d8)
{
    auto orig = ctx.Original<uint8_t(__cdecl *)(void *, uint32_t, uint32_t, uint32_t)>(0x9352d8);
    Fakes fakes(ctx);
    Objs objs(fakes);
    std::mt19937 rng(9016);
    for (int i = 0; i < 600; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        b = a;
        for (int j = 0; j < 4; ++j) {
            Obj &child = objs.o[2 + j];
            child.flag = rng() & 1;
            gChildren[j] = (rng() % 5 == 0) ? nullptr : &child;
        }
        uint32_t key = static_cast<uint32_t>(rng() % 8);
        uint32_t value = (i % 3 == 0) ? Edge(rng, i) : RandomLow(rng);
        uint32_t notify = (i % 2) ? RandomLow(rng) : Edge(rng, i + 1);

        gWindow = a.obj;
        gLog.clear();
        uint8_t ra = orig(a.obj, key, value, notify);
        std::vector<Event> la = gLog;
        gLog.clear();
        gWindow = b.obj;
        uint8_t rb = SetChildFlagNotify(b.obj, key, value, notify) ? 1 : 0;
        std::vector<Event> lb = gLog;
        CheckRun(ctx, "SetChildFlagNotify", a, b, ra, rb, la, lb);
    }
}

// 0x9317d9 (slot 4): unless flag 0x4000 is set, SetFlag4000(1), slot 113 with 1 when (+0xe8 & 0x10001) is
// 0x10001, then slot 109. Returns al = 1. The flag bits are random, so every path is taken.
DIFF_TEST(cGZWin_Activate, 0x9317d9)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(0x9317d9);
    Fakes fakes(ctx);
    std::mt19937 rng(9017);
    for (int i = 0; i < 400; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        uint32_t e8 = static_cast<uint32_t>(rng());
        if (i % 3 == 0) e8 &= ~0x4000u;
        else if (i % 3 == 1) e8 |= 0x4000u;
        // Bits 0 and 16 vary independently, so each of the two bits alone is seen (a mask with only one of them
        // must not call slot 113).
        static const uint32_t kBits[] = {0u, 0x10001u, 0x10000u, 0x1u};
        e8 = (e8 & ~0x10001u) | kBits[i % 4];
        Set32(a, 0xe8, e8);
        b = a;
        gWindow = a.obj;
        gLog.clear();
        uint8_t ra = orig(a.obj);
        std::vector<Event> la = gLog;
        gLog.clear();
        gWindow = b.obj;
        uint8_t rb = reinterpret_cast<cGZWin *>(b.obj)->Activate() ? 1 : 0;
        std::vector<Event> lb = gLog;
        CheckRun(ctx, "Activate", a, b, ra, rb, la, lb);
    }
}

// 0x9330b0 (slot 84): this->vf83(1, value)
DIFF_TEST(cGZWin_SetFlag1, 0x9330b0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x9330b0);
    Fakes fakes(ctx);
    std::mt19937 rng(9018);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        b = a;
        uint32_t value = Edge(rng, i);
        gWindow = a.obj;
        gLog.clear();
        orig(a.obj, value);
        std::vector<Event> la = gLog;
        gLog.clear();
        gWindow = b.obj;
        reinterpret_cast<cGZWin *>(b.obj)->SetFlag1(value);
        std::vector<Event> lb = gLog;
        CheckRun(ctx, "SetFlag1", a, b, 0, 0, la, lb);
    }
}

// 0x9317cc (slot 85): this->vf83(1, 0)
DIFF_TEST(cGZWin_ClearFlag1, 0x9317cc)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x9317cc);
    Fakes fakes(ctx);
    std::mt19937 rng(9019);
    for (int i = 0; i < 200; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        b = a;
        gWindow = a.obj;
        gLog.clear();
        orig(a.obj);
        std::vector<Event> la = gLog;
        gLog.clear();
        gWindow = b.obj;
        reinterpret_cast<cGZWin *>(b.obj)->ClearFlag1();
        std::vector<Event> lb = gLog;
        CheckRun(ctx, "ClearFlag1", a, b, 0, 0, la, lb);
    }
}

// 0x932be5 (slot 98, ret 8): +0x90 = value; if the low byte of notify is set, slot 97. Notify's high bits are
// random, so a notify of 0x100 must not call.
DIFF_TEST(cGZWin_SetF90Notify, 0x932be5)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0x932be5);
    Fakes fakes(ctx);
    std::mt19937 rng(9020);
    for (int i = 0; i < 400; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        b = a;
        uint32_t value = Edge(rng, i);
        uint32_t notify = (i % 2) ? RandomLow(rng) : Edge(rng, i + 1);
        gWindow = a.obj;
        gLog.clear();
        orig(a.obj, value, notify);
        std::vector<Event> la = gLog;
        gLog.clear();
        gWindow = b.obj;
        reinterpret_cast<cGZWin *>(b.obj)->SetF90Notify(value, notify);
        std::vector<Event> lb = gLog;
        CheckRun(ctx, "SetF90Notify", a, b, 0, 0, la, lb);
    }
}

// 0x5c8be0 (slot 140): +0xf4 = value
DIFF_TEST(cGZWin_SetF4, 0x5c8be0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x5c8be0);
    Fakes fakes(ctx);
    std::mt19937 rng(9021);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        b = a;
        uint32_t value = Edge(rng, i);
        orig(a.obj, value);
        reinterpret_cast<cGZWin *>(b.obj)->SetF4(value);
        CheckRun(ctx, "SetF4", a, b, 0, 0, {}, {});
    }
}

// 0x932c79 (slot 118): +0xf4 = value; returns al = 1
DIFF_TEST(cGZWin_SetF4Ok, 0x932c79)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x932c79);
    Fakes fakes(ctx);
    std::mt19937 rng(9022);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        b = a;
        uint32_t value = Edge(rng, i);
        uint8_t ra = orig(a.obj, value);
        uint8_t rb = reinterpret_cast<cGZWin *>(b.obj)->SetF4Ok(value) ? 1 : 0;
        CheckRun(ctx, "SetF4Ok", a, b, ra, rb, {}, {});
    }
}

// 0x9316ef (slot 117, ret 0xc): bytes of +0xf6, +0xf5, +0xf4 to the three pointers (in the output area)
DIFF_TEST(cGZWin_GetF4Bytes, 0x9316ef)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *, void *)>(0x9316ef);
    Fakes fakes(ctx);
    std::mt19937 rng(9023);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        Set32(a, 0xf4, Edge(rng, i));
        b = a;
        orig(a.obj, OutArea(a), OutArea(a) + 4, OutArea(a) + 8);
        reinterpret_cast<cGZWin *>(b.obj)->GetF4Bytes(OutArea(b), OutArea(b) + 4, OutArea(b) + 8);
        CheckRun(ctx, "GetF4Bytes", a, b, 0, 0, {}, {});
    }
}

// 0x932c18 (slot 120, ret 0xc): +0xf4 = top << 16 | middle << 8 | bottom from the low bytes of the three stack
// arguments. The arguments are passed as full dwords with random high bits.
DIFF_TEST(cGZWin_SetF4Bytes, 0x932c18)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x932c18);
    auto ours = reinterpret_cast<Fn>(MemberAddress(&cGZWin::SetF4Bytes));
    Fakes fakes(ctx);
    std::mt19937 rng(9024);
    for (int i = 0; i < 300; ++i) {
        Case a, b;
        Randomize(a, rng);
        SetWord(a, 0x00, fakes.winVt);
        b = a;
        uint32_t top = static_cast<uint32_t>(rng()), middle = static_cast<uint32_t>(rng()),
                 bottom = static_cast<uint32_t>(rng());
        orig(a.obj, top, middle, bottom);
        ours(b.obj, top, middle, bottom);
        CheckRun(ctx, "SetF4Bytes", a, b, 0, 0, {}, {});
    }
}
