// Differential tests for the UI window-procedure functions in src/ui/UIWinProcs.*. Each case builds the same
// object state twice, runs the original on one copy and ours on the other, and compares the result (the width
// the original leaves in eax or al), the log of virtual calls (slot, object, arguments), and the object bytes.
//
// The virtual calls go to recording fakes. Each fake takes __fastcall parameters, so it pops its stack arguments
// the way a thiscall callee does. A slot called with different argument counts gets different vtables. Objects
// are tagged through gTags (the window is 1, the others have their own tags), so the logs compare equal across
// the two copies.
#include "difftest.h"
#include "ui/UIWinProcs.h"

#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

// ---------------------------------------------------------------------------------------------------------
// Recording.

struct Event {
    int slot;
    int tag;
    uint32_t a, b;
    bool operator==(const Event &o) const { return slot == o.slot && tag == o.tag && a == o.a && b == o.b; }
};

struct TagEntry {
    const void *object;
    int tag;
};

std::vector<Event> gLog;
std::vector<TagEntry> gTags;

int TagOf(const void *self)
{
    for (const TagEntry &t : gTags)
        if (t.object == self) return t.tag;
    return -1;
}

void Record(int slot, const void *self, uint32_t a = 0, uint32_t b = 0)
{
    gLog.push_back({slot, TagOf(self), a, b});
}

uint32_t Edge(std::mt19937 &rng, int i)
{
    static const uint32_t kEdges[] = {0u, 1u, 0xffffffffu, 0x7fffffffu, 0x80000000u, 0x0000ffffu};
    if (i < static_cast<int>(sizeof(kEdges) / sizeof(kEdges[0])) && i % 2 == 0) return kEdges[i];
    return static_cast<uint32_t>(rng());
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// cGZWinGrid::TestFlagsFC, 0x936a5c: setne al of (+0xfc & mask). Only al is compared (eax keeps the mask).

DIFF_TEST(cGZWinGrid_TestFlagsFC, 0x936a5c)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x936a5c);
    std::mt19937 rng(9301);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[0x100], b[0x100];
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        uint32_t field = Edge(rng, i);
        std::memcpy(a + 0xfc, &field, 4);
        std::memcpy(b, a, sizeof(a));
        uint32_t mask = (i % 3 == 0) ? (1u << (rng() % 32)) : Edge(rng, i + 1);
        uint8_t ra = orig(a, mask);
        uint8_t rb = reinterpret_cast<cGZWinGrid *>(b)->TestFlagsFC(mask) ? 1 : 0;
        ++ctx.cases;
        if ((ra & 0xff) != rb) ctx.Fail("TestFlagsFC: al original %u, ours %u", ra, rb);
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("TestFlagsFC: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinMsgBoxAt252::ForwardToSlot5, 0x9529e5: the object is at this - 0xfc; its slot 5 is called with the
// object as `this`, and eax is passed through unchanged.

namespace {
uint32_t gMsgBoxSlot5Result = 0;
uint32_t __fastcall FakeMsgBoxSlot5(void *self, void *)
{
    Record(5, self);
    return gMsgBoxSlot5Result;
}
} // namespace

DIFF_TEST(cGZWinMsgBox_ForwardToSlot5, 0x9529e5)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x9529e5);
    // Only slot 5 is read, so the copy of the vtable has just that slot.
    void *vt[6] = {};
    vt[5] = reinterpret_cast<void *>(&FakeMsgBoxSlot5);
    void **vtPtr = vt;
    std::mt19937 rng(9302);
    for (int i = 0; i < 300; ++i) {
        uint8_t a[0x200], b[0x200];
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        std::memcpy(a, &vtPtr, sizeof(void *));
        std::memcpy(b, a, sizeof(a));
        gMsgBoxSlot5Result = Edge(rng, i);

        gTags = {{a, 1}};
        gLog.clear();
        uint32_t ra = orig(a + 0xfc);
        std::vector<Event> la = gLog;

        gTags = {{b, 1}};
        gLog.clear();
        uint32_t rb = reinterpret_cast<cGZWinMsgBoxAt252 *>(b + 0xfc)->ForwardToSlot5();
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (ra != rb) ctx.Fail("ForwardToSlot5: eax original %x, ours %x", ra, rb);
        if (!(la == lb)) ctx.Fail("ForwardToSlot5: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("ForwardToSlot5: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cTSWinProcGridThumbnail::Vf7, 0x75de32: RZUnknownSlot::Assign(null) on +0x10, +0xc and +8; returns al = 1.

namespace {
uint32_t __fastcall FakeAddRef(void *self, void *)
{
    Record(1, self);
    return 2;
}
uint32_t __fastcall FakeRelease(void *self, void *)
{
    Record(2, self);
    return 1;
}
} // namespace

DIFF_TEST(cTSWinProcGridThumbnail_Vf7, 0x75de32)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x75de32);
    // A recording object: its first dword is the vtable pointer; slots 1 and 2 are AddRef and Release.
    void *recVt[3] = {nullptr, reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeRelease)};
    struct Rec {
        void *vtable;
    };
    std::mt19937 rng(9303);
    for (int i = 0; i < 300; ++i) {
        uint8_t fill[0x40];
        for (auto &x : fill) x = static_cast<uint8_t>(rng());
        // Each side gets its own objects; the slots end up null on both sides (Assign(null) stores null).
        Rec recsA[3], recsB[3];
        uint8_t a[0x40], b[0x40];
        std::memcpy(a, fill, sizeof(a));
        std::memcpy(b, fill, sizeof(b));
        for (int k = 0; k < 3; ++k) {
            recsA[k].vtable = recVt;
            recsB[k].vtable = recVt;
        }
        const bool present[3] = {rng() % 4 != 0, rng() % 4 != 0, rng() % 4 != 0};
        void *slotA[3] = {present[0] ? &recsA[0] : nullptr, present[1] ? &recsA[1] : nullptr, present[2] ? &recsA[2] : nullptr};
        void *slotB[3] = {present[0] ? &recsB[0] : nullptr, present[1] ? &recsB[1] : nullptr, present[2] ? &recsB[2] : nullptr};
        for (int k = 0; k < 3; ++k) {
            std::memcpy(a + 8 + 4 * k, &slotA[k], 4);
            std::memcpy(b + 8 + 4 * k, &slotB[k], 4);
        }
        // The recorded objects get the same tags on both sides (by index), so the logs compare equal.
        gTags = {{&recsA[0], 10}, {&recsA[1], 11}, {&recsA[2], 12}};
        gLog.clear();
        uint8_t ra = orig(a, 0, 0);
        std::vector<Event> la = gLog;

        gTags = {{&recsB[0], 10}, {&recsB[1], 11}, {&recsB[2], 12}};
        gLog.clear();
        bool rb = reinterpret_cast<cTSWinProcGridThumbnail *>(b)->Vf7(0, 0);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("Vf7: al original %u, ours %d", ra, rb ? 1 : 0);
        if (!(la == lb)) ctx.Fail("Vf7: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Vf7: object state differs");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cTSWinProcGrid::Vf39, 0x748af4: dispatches on the id at msg+8 to slot 26 (the field at +0x20 +/- 1), or to
// slot 45 with (0, 1) or (1, 1) and then slot 3 of the child at +8. Returns al.

namespace {
void __fastcall FakeWinSlot26(void *self, void *, uint32_t v)
{
    Record(26, self, v);
}
void __fastcall FakeWinSlot45(void *self, void *, uint32_t a, uint32_t b)
{
    Record(45, self, a, b);
}
void __fastcall FakeChildSlot3(void *self, void *)
{
    Record(3, self);
}

// The window's vtable (a copy of the original with slots 26 and 45 replaced) and the child's (slot 3 replaced).
// Both are built once; the window and child objects share them.
struct WinVtables {
    void *window[46];
    void *child[4];
    explicit WinVtables(const difftest::Context &ctx)
    {
        std::memcpy(window, ctx.image.At(0xdf6b08), sizeof(window));
        std::memcpy(child, ctx.image.At(0xdf6bc4), sizeof(child));
        window[26] = reinterpret_cast<void *>(&FakeWinSlot26);
        window[45] = reinterpret_cast<void *>(&FakeWinSlot45);
        child[3] = reinterpret_cast<void *>(&FakeChildSlot3);
    }
};
} // namespace

DIFF_TEST(cTSWinProcGrid_Vf39, 0x748af4)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, const void *)>(0x748af4);
    static WinVtables *tables = nullptr;
    if (tables == nullptr) tables = new WinVtables(ctx);
    // Every id the dispatch tests, first, then random ids and random picks from the same list.
    static const uint32_t kIds[] = {0x3304, 0x3305, 0x3309, 0x5504, 0x5505, 0x5509, 0x1004, 0x1005, 0x1009,
                                    0x2004, 0x2005, 0x2009, 0x3303, 0x3306, 0x3308, 0x3310, 0x5503, 0x5506,
                                    0x5508, 0x550a, 0x1003, 0x1006, 0x1008, 0x100a, 0x2003, 0x2006, 0x2008,
                                    0x200a, 0x0, 0xffffffffu, 0x80000000u, 0x3000};
    const size_t kCount = sizeof(kIds) / sizeof(kIds[0]);
    std::mt19937 rng(9304);
    for (int i = 0; i < 600; ++i) {
        uint8_t winA[0x100], winB[0x100];
        for (auto &x : winA) x = static_cast<uint8_t>(rng());
        // The vtable pointer (+0) and the child's vtable pointer (+8) are the shared copies, so the bytes match.
        void **windowVt = tables->window;
        void **childVt = tables->child;
        std::memcpy(winA, &windowVt, 4);
        std::memcpy(winA + 8, &childVt, 4);
        uint32_t field = Edge(rng, i);
        std::memcpy(winA + 0x20, &field, 4);
        std::memcpy(winB, winA, sizeof(winA));

        uint8_t msg[16];
        for (auto &x : msg) x = static_cast<uint8_t>(rng());
        uint32_t id = (i < static_cast<int>(kCount)) ? kIds[i] : (rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : kIds[rng() % kCount]);
        std::memcpy(msg + 8, &id, 4);

        gTags = {{winA, 1}, {winA + 8, 2}};
        gLog.clear();
        uint8_t ra = orig(winA, 0, msg);
        std::vector<Event> la = gLog;

        gTags = {{winB, 1}, {winB + 8, 2}};
        gLog.clear();
        bool rb = reinterpret_cast<cTSWinProcGrid *>(winB)->Vf39(0, msg);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("Vf39 id %x: al original %u, ours %d", id, ra, rb ? 1 : 0);
        if (!(la == lb)) ctx.Fail("Vf39 id %x: calls differ (original %zu, ours %zu)", id, la.size(), lb.size());
        if (std::memcmp(winA, winB, sizeof(winA)) != 0) ctx.Fail("Vf39 id %x: window changed", id);
    }
}

// ---------------------------------------------------------------------------------------------------------
// nTSUICAS::cTSUICASComponent::Vf360_8, 0x7594a3: when the flag at +0x68 is set and the owner's slot 86 is true,
// walks the list at +0x70. For each component whose slot 9 is true, takes its slot 10 result (o2); if o2's slot
// 86 is true, calls the component's slot 8 with the argument. Returns nothing.
//
// The scene: a block whose subobject `this` sits at +0x200, so the owner (this - 0x168) is at +0x98. The list head
// at this+0x70 points to nodes[0] (a sentinel). Components and o2 objects carry a vtable (slots 7..10, 86) and
// their results; the owner's vtable has slot 86.

namespace {

struct ListNode {
    ListNode *next;
    ListNode *prev;
    void *value;
};

struct Comp {
    void *vt;
    uint8_t ret9;
    uint8_t ret86;
    void *o2;
};

uint8_t __fastcall FakeComp9(void *self, void *)
{
    Record(9, self);
    return static_cast<Comp *>(self)->ret9;
}
void *__fastcall FakeComp10(void *self, void *)
{
    Record(10, self);
    return static_cast<Comp *>(self)->o2;
}
void __fastcall FakeComp8(void *self, void *, uint32_t arg)
{
    Record(8, self, arg);
}
uint8_t __fastcall FakeComp86(void *self, void *)
{
    Record(86, self);
    return static_cast<Comp *>(self)->ret86;
}
uint8_t gOwnerRet86 = 0;
uint8_t __fastcall FakeOwner86(void *self, void *)
{
    Record(86, self);
    return gOwnerRet86;
}

struct Vtables {
    void *comp[200] = {};
    void *owner[200] = {};
    Vtables()
    {
        comp[8] = reinterpret_cast<void *>(&FakeComp8);
        comp[9] = reinterpret_cast<void *>(&FakeComp9);
        comp[10] = reinterpret_cast<void *>(&FakeComp10);
        comp[86] = reinterpret_cast<void *>(&FakeComp86);
        owner[86] = reinterpret_cast<void *>(&FakeOwner86);
    }
};

Vtables &Tables()
{
    static Vtables tables;
    return tables;
}

struct CompPlan {
    bool isNull;
    uint8_t ret9, ret86, o2ret86;
};

struct Plan {
    uint8_t flag;
    uint8_t ownerRet86;
    std::vector<CompPlan> comps;
    uint8_t fill[0x400];
};

Plan RandomPlan(std::mt19937 &rng)
{
    Plan p;
    p.flag = (rng() % 4 == 0) ? 0 : static_cast<uint8_t>(rng());
    p.ownerRet86 = (rng() % 3 == 0) ? 0 : static_cast<uint8_t>(rng());
    const size_t n = rng() % 5;
    for (size_t j = 0; j < n; ++j) {
        CompPlan c;
        c.isNull = rng() % 6 == 0;
        c.ret9 = (rng() % 3 == 0) ? 0 : static_cast<uint8_t>(rng() | 1);
        c.ret86 = (rng() % 3 == 0) ? 0 : static_cast<uint8_t>(rng());
        c.o2ret86 = (rng() % 3 == 0) ? 0 : static_cast<uint8_t>(rng());
        p.comps.push_back(c);
    }
    for (auto &b : p.fill) b = static_cast<uint8_t>(rng());
    return p;
}

struct Scene {
    alignas(16) uint8_t buf[0x400];
    std::vector<Comp> comps;
    std::vector<Comp> o2s;
    std::vector<ListNode> nodes;
    uint8_t *window() { return buf + 0x200; }
    uint8_t *owner() { return buf + 0x98; }
};

void Build(Scene &s, const Plan &p)
{
    std::memcpy(s.buf, p.fill, sizeof(s.buf));
    *reinterpret_cast<void **>(s.owner()) = Tables().owner;
    s.buf[0x200 + 0x68] = p.flag;

    const size_t n = p.comps.size();
    s.comps.reserve(n);
    s.o2s.reserve(n);
    s.nodes.reserve(n + 1);
    for (size_t j = 0; j < n; ++j) {
        const CompPlan &c = p.comps[j];
        s.comps.push_back(Comp{Tables().comp, c.ret9, c.ret86, nullptr});
        s.o2s.push_back(Comp{Tables().comp, 0, c.o2ret86, nullptr});
    }
    for (size_t j = 0; j < n; ++j) s.comps[j].o2 = &s.o2s[j];

    // nodes[0] is the sentinel; nodes[j+1] holds comps[j] (null when the plan says so).
    s.nodes.assign(n + 1, ListNode{nullptr, nullptr, nullptr});
    for (size_t j = 0; j <= n; ++j) {
        const size_t nextIndex = (j + 1) % (n + 1);
        s.nodes[j].next = &s.nodes[nextIndex];
        s.nodes[nextIndex].prev = &s.nodes[j];
        s.nodes[j].value = (j == 0 || p.comps[j - 1].isNull) ? nullptr : &s.comps[j - 1];
    }
    *reinterpret_cast<ListNode **>(s.window() + 0x70) = &s.nodes[0];
}

// Tags for one run: the owner is 1, components 10+j, o2 objects 100+j.
void Register(Scene &s, const Plan &p)
{
    gTags.clear();
    gTags.push_back({s.owner(), 1});
    for (size_t j = 0; j < s.comps.size(); ++j) {
        gTags.push_back({&s.comps[j], 10 + static_cast<int>(j)});
        gTags.push_back({&s.o2s[j], 100 + static_cast<int>(j)});
    }
    gOwnerRet86 = p.ownerRet86;
}

// Compares the two scene blocks, skipping the bytes that hold heap-like list pointers (this+0x70 differs by side).
void CompareScenes(difftest::Context &ctx, const char *name, const Scene &a, const Scene &b)
{
    for (size_t i = 0; i < sizeof(a.buf); ++i) {
        if (i >= 0x270 && i < 0x278) continue;
        if (a.buf[i] != b.buf[i]) {
            ctx.Fail("%s: object byte at %zx differs", name, i);
            break;
        }
    }
}

} // namespace

DIFF_TEST(cTSUICASComponent_Vf360_8, 0x7594a3)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x7594a3);
    std::mt19937 rng(9305);
    for (int i = 0; i < 400; ++i) {
        Plan p = RandomPlan(rng);
        auto a = std::make_unique<Scene>();
        auto b = std::make_unique<Scene>();
        Build(*a, p);
        Build(*b, p);
        uint32_t arg = Edge(rng, i + 3);

        Register(*a, p);
        gLog.clear();
        orig(a->window(), arg);
        std::vector<Event> la = gLog;

        Register(*b, p);
        gLog.clear();
        reinterpret_cast<nTSUICAS::cTSUICASComponent *>(b->window())->Vf360_8(arg);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (!(la == lb)) ctx.Fail("Vf360_8: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        CompareScenes(ctx, "Vf360_8", *a, *b);
    }
}
