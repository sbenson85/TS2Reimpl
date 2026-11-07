// Differential tests for the Create-a-Sim UI functions in src/ui/TSUICAS.*. Each case builds the same object state
// twice, runs the original on one copy and ours on the other, and compares the result (al, or the full eax where
// the original's eax is a defined value), the log of virtual calls (slot, object, arguments), the output pointers
// and the object bytes.
//
// Virtual calls go to recording fakes. The fakes are __fastcall functions, so they pop their stack arguments the
// way a thiscall callee does. The fake objects are shared by both copies (their addresses are the same), so only
// the copies themselves need per-side tags.
#include "difftest.h"
#include "ui/TSUICAS.h"

#include <cstring>
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

// ---------------------------------------------------------------------------------------------------------
// Fake objects. Each has a vtable pointer at +0; the vtables are sized to the slots the code under test calls.

struct FakeObject {
    void *vt;
    uint32_t pad[3];
};

FakeObject gFake[4];   // Release targets; tags 100..103
FakeObject gFake15;    // slot 15 target; tag 110
FakeObject gOwner;     // owner of the modifier's slot 19 call; tag 120

void *gVtSelf[4];   // [1] AddRef, [2] Release: the vtable of the objects under test
void *gVtFake[4];   // [1] AddRef, [2] Release: the vtable of gFake
void *gVtF15[16];   // [15] FakeSlot15
void *gVtSub[8];    // [7] FakeSlot7
void *gVtOwner[20]; // [19] FakeOwnerSlot19

uint32_t gOwnerResult = 0;

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

void __fastcall FakeSlot7(void *self, void *)
{
    Record(7, self);
}

void __fastcall FakeSlot15(void *self, void *, uint32_t a)
{
    Record(15, self, a);
}

// Stores gFake[1] into the output slot (as a QueryInterface would) and returns gOwnerResult.
uint32_t __fastcall FakeOwnerSlot19(void *self, void *, uint32_t flag, uint32_t id, void **out)
{
    Record(19, self, flag, id);
    *out = &gFake[1];
    return gOwnerResult;
}

void InitFakes()
{
    gVtSelf[1] = reinterpret_cast<void *>(&FakeAddRef);
    gVtSelf[2] = reinterpret_cast<void *>(&FakeRelease);
    gVtFake[1] = reinterpret_cast<void *>(&FakeAddRef);
    gVtFake[2] = reinterpret_cast<void *>(&FakeRelease);
    gVtF15[15] = reinterpret_cast<void *>(&FakeSlot15);
    gVtSub[7] = reinterpret_cast<void *>(&FakeSlot7);
    gVtOwner[19] = reinterpret_cast<void *>(&FakeOwnerSlot19);
    for (FakeObject &f : gFake) f.vt = gVtFake;
    gFake15.vt = gVtF15;
    gOwner.vt = gVtOwner;
}

// The fake tags, shared by both copies. `side` is the copy under test, with tag 1.
void SetTags(const void *side)
{
    gTags.clear();
    for (int i = 0; i < 4; ++i) gTags.push_back({&gFake[i], 100 + i});
    gTags.push_back({&gFake15, 110});
    gTags.push_back({&gOwner, 120});
    gTags.push_back({side, 1});
}

// Edge-biased 32-bit values.
uint32_t Edge(std::mt19937 &rng, int i)
{
    static const uint32_t kEdges[] = {0u, 1u, 0xffffffffu, 0x7fffffffu, 0x80000000u, 0x0000ffffu};
    if (i < static_cast<int>(sizeof(kEdges) / sizeof(kEdges[0])) && i % 2 == 0) return kEdges[i];
    return static_cast<uint32_t>(rng());
}

void FillRandom(std::mt19937 &rng, uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(rng());
}

// A value that points into an object buffer becomes an offset from that buffer, so the two copies compare equal.
uint32_t RelTo(uint32_t v, const void *base, size_t size)
{
    const uintptr_t b = reinterpret_cast<uintptr_t>(base);
    if (v >= b && v < b + size) return 0x40000000u | static_cast<uint32_t>(v - b);
    return v;
}

// A riid for a QueryInterface: the matching id, the 1 that the base class also matches, or some other value.
uint32_t PickRiid(std::mt19937 &rng, int i, uint32_t match)
{
    switch (i % 5) {
    case 0: return match;
    case 1: return 1u;
    case 2: return Edge(rng, i);
    default: return static_cast<uint32_t>(rng());
    }
}

// A QueryInterface object: a random buffer whose first dword is the test vtable.
void MakeQiObject(std::mt19937 &rng, uint8_t *buf, size_t size)
{
    FillRandom(rng, buf, size);
    void *vt = gVtSelf;
    std::memcpy(buf, &vt, sizeof(void *));
}

// Runs a QueryInterface pair (original, ours) on identical copies, and compares the bool, the calls, the two
// output pointers and the object bytes.
template <typename Ours>
void CheckQi(difftest::Context &ctx, const char *what, bool (__thiscall *orig)(void *, uint32_t, void **), Ours ours,
             uint32_t riid, bool nullPpv, uint8_t *a, uint8_t *b, size_t size, std::mt19937 &rng)
{
    void *outA[2], *outB[2];
    for (int k = 0; k < 2; ++k) outA[k] = outB[k] = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));

    SetTags(a);
    gLog.clear();
    bool ra = orig(a, riid, nullPpv ? nullptr : &outA[0]);
    std::vector<Event> la = gLog;

    SetTags(b);
    gLog.clear();
    bool rb = ours(b, riid, nullPpv ? nullptr : &outB[0]);
    std::vector<Event> lb = gLog;

    ++ctx.cases;
    if (ra != rb) ctx.Fail("%s riid %x: al original %d, ours %d", what, riid, ra, rb);
    if (!(la == lb)) ctx.Fail("%s riid %x: calls differ (original %zu, ours %zu)", what, riid, la.size(), lb.size());
    if (!nullPpv)
        for (int k = 0; k < 2; ++k)
            if (RelTo(reinterpret_cast<uintptr_t>(outA[k]), a, size) != RelTo(reinterpret_cast<uintptr_t>(outB[k]), b, size))
                ctx.Fail("%s riid %x: out[%d] differs", what, riid, k);
    if (std::memcmp(a, b, size) != 0) ctx.Fail("%s riid %x: object changed", what, riid);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// cTSUICASComponentPetCoatLayerEntry::QueryInterface, 0x602f64. Null ppv: false. riid 0xf15b2506: store this,
// AddRef. Otherwise RZUnknownQueryInterface::QueryInterfaceUnknown (riid 1 matches there).

DIFF_TEST(cTSUICASComponentPetCoatLayerEntry_QueryInterface, 0x602f64)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x602f64);
    InitFakes();
    std::mt19937 rng(9401);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[0x80], b[0x80];
        MakeQiObject(rng, a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        CheckQi(ctx, "PetCoatLayerEntry QI", orig,
                [](void *self, uint32_t riid, void **ppv) {
                    return reinterpret_cast<nTSUICAS::cTSUICASComponentPetCoatLayerEntry *>(self)->QueryInterface(riid, ppv);
                },
                PickRiid(rng, i, 0xf15b2506u), i % 7 == 0, a, b, sizeof(a), rng);
    }
}

// ---------------------------------------------------------------------------------------------------------
// cTSUICASComponentPetCoatLayerThumbnail::QueryInterface, 0x602f92. Null ppv: false. riid 0x115b2117: store this,
// AddRef. Otherwise qi::TSWinProcGridThumbnailQueryInterface (0x75dc5b): riid 0x11670ab9 stores this, anything
// else goes to the cRZWinProc4 fall-through (riid 1 and 0x22e85d8e store this).

DIFF_TEST(cTSUICASComponentPetCoatLayerThumbnail_QueryInterface, 0x602f92)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x602f92);
    InitFakes();
    std::mt19937 rng(9402);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[0x80], b[0x80];
        MakeQiObject(rng, a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        uint32_t riid = PickRiid(rng, i, 0x115b2117u);
        if (i % 4 == 2) riid = (i % 8 == 2) ? 0x11670ab9u : 0x22e85d8eu;
        CheckQi(ctx, "PetCoatLayerThumbnail QI", orig,
                [](void *self, uint32_t riid, void **ppv) {
                    return reinterpret_cast<nTSUICAS::cTSUICASComponentPetCoatLayerThumbnail *>(self)->QueryInterface(riid, ppv);
                },
                riid, i % 7 == 0, a, b, sizeof(a), rng);
    }
}

// ---------------------------------------------------------------------------------------------------------
// cTSFragmentFilterBase::QueryInterface, 0x759042. Null ppv: false. riid 0x50b4ee7f: store this + 8 (this is
// never null here: the original AddRefs it), AddRef this. Otherwise RZUnknownQueryInterface::QueryInterfaceUnknown.

DIFF_TEST(cTSFragmentFilterBase_QueryInterface, 0x759042)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x759042);
    InitFakes();
    std::mt19937 rng(9403);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[0x80], b[0x80];
        MakeQiObject(rng, a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        CheckQi(ctx, "cTSFragmentFilterBase QI", orig,
                [](void *self, uint32_t riid, void **ppv) {
                    return reinterpret_cast<nTSUICAS::cTSFragmentFilterBase *>(self)->QueryInterface(riid, ppv);
                },
                PickRiid(rng, i, 0x50b4ee7fu), i % 7 == 0, a, b, sizeof(a), rng);
    }
}

// ---------------------------------------------------------------------------------------------------------
// cTSUICASComponentPetCoatLayerThumbnail::vf7, 0x603b9f: RZUnknownSlot::Assign(null) on +0x18 and +0x1c, then the
// verified cTSWinProcGridThumbnail::Vf7 (which clears +0x10, +0xc and +8). Only al is the result.

DIFF_TEST(cTSUICASComponentPetCoatLayerThumbnail_Vf7, 0x603b9f)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x603b9f);
    InitFakes();
    std::mt19937 rng(9404);
    static const int kSlots[] = {0x8, 0xc, 0x10, 0x18, 0x1c};
    for (int i = 0; i < 300; ++i) {
        uint8_t a[0x80], b[0x80];
        MakeQiObject(rng, a, sizeof(a));
        // Each slot is null or a fake object (so Release is called on it), and the other bytes are random.
        for (int slot : kSlots) {
            void *value = (rng() % 3 == 0) ? nullptr : static_cast<void *>(&gFake[rng() % 4]);
            std::memcpy(a + slot, &value, sizeof(void *));
        }
        std::memcpy(b, a, sizeof(a));
        uint32_t p = Edge(rng, i), q = static_cast<uint32_t>(rng());

        SetTags(a);
        gLog.clear();
        uint8_t ra = orig(a, p, q);
        std::vector<Event> la = gLog;

        SetTags(b);
        gLog.clear();
        bool rb = reinterpret_cast<nTSUICAS::cTSUICASComponentPetCoatLayerThumbnail *>(b)->Vf7(p, q);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if ((ra & 0xff) != (rb ? 1 : 0)) ctx.Fail("PetCoatLayerThumbnail vf7: al original %u, ours %d", ra, rb);
        if (!(la == lb)) ctx.Fail("PetCoatLayerThumbnail vf7: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("PetCoatLayerThumbnail vf7: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cTSUICASComponentPetBin::vf184, 0x6049a5: when the byte at +0x2c1 is set and the dword at +0x2dc is 2, slot 15
// of the object at +0x184 is called with 0x801. Then slot 7 of the subobject at +0x168. The argument is unused.

DIFF_TEST(cTSUICASComponentPetBin_vf184, 0x6049a5)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x6049a5);
    InitFakes();
    std::mt19937 rng(9405);
    for (int i = 0; i < 300; ++i) {
        uint8_t a[0x400], b[0x400];
        FillRandom(rng, a, sizeof(a));
        // The +0x184 object carries the slot 15 vtable; +0x168 is the subobject whose vtable has slot 7.
        void *obj = &gFake15;
        std::memcpy(a + 0x184, &obj, sizeof(void *));
        void *sub = gVtSub;
        std::memcpy(a + 0x168, &sub, sizeof(void *));
        uint8_t flagByte = (i % 3 == 0) ? 0 : static_cast<uint8_t>(rng() | 1);
        if (i % 4 == 0) flagByte = 0;
        a[0x2c1] = flagByte;
        uint32_t state = (i % 2 == 0) ? 2u : Edge(rng, i);
        std::memcpy(a + 0x2dc, &state, sizeof(state));
        std::memcpy(b, a, sizeof(a));
        uint32_t arg = static_cast<uint32_t>(rng());

        SetTags(a);
        gTags.push_back({a + 0x168, 2});
        gLog.clear();
        orig(a, arg);
        std::vector<Event> la = gLog;

        SetTags(b);
        gTags.push_back({b + 0x168, 2});
        gLog.clear();
        reinterpret_cast<nTSUICAS::cTSUICASComponentPetBin *>(b)->Vf184(arg);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (!(la == lb)) ctx.Fail("PetBin vf184: calls differ (original %zu, ours %zu)", la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("PetBin vf184: object changed");
    }
}

// ---------------------------------------------------------------------------------------------------------
// cTSUICASComponentModifiers::vf656_4, 0x5f8e0a. The subobject is at buf + 0x200. flag == 0: Assign(null) on the
// slot at this + 0x54 (eax is the slot). flag != 0: read the owner at this - 0x110, release and clear the slot
// if set, then slot 19 of the owner with (flag, 0x6c4f3599, slot). eax is that call's result.

DIFF_TEST(cTSUICASComponentModifiers_vf656_4, 0x5f8e0a)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x5f8e0a);
    InitFakes();
    std::mt19937 rng(9406);
    for (int i = 0; i < 300; ++i) {
        uint8_t a[0x400], b[0x400];
        FillRandom(rng, a, sizeof(a));
        // The owner pointer at this - 0x110 and the slot at this + 0x54 (null, or a fake object to release).
        void *owner = &gOwner;
        std::memcpy(a + 0x200 - 0x110, &owner, sizeof(void *));
        void *slotValue = (i % 3 == 0) ? nullptr : static_cast<void *>(&gFake[rng() % 4]);
        std::memcpy(a + 0x200 + 0x54, &slotValue, sizeof(void *));
        std::memcpy(b, a, sizeof(a));
        gOwnerResult = static_cast<uint32_t>(rng());
        uint32_t flag = (i % 4 == 0) ? 0u : Edge(rng, i);
        if (flag == 0 && i % 4 != 0) flag = 1;

        SetTags(a);
        gLog.clear();
        uint32_t ra = orig(a + 0x200, flag);
        std::vector<Event> la = gLog;

        SetTags(b);
        gLog.clear();
        uint32_t rb = reinterpret_cast<nTSUICAS::cTSUICASComponentModifiers *>(b + 0x200)->Vf656_4(flag);
        std::vector<Event> lb = gLog;

        ++ctx.cases;
        if (RelTo(ra, a, sizeof(a)) != RelTo(rb, b, sizeof(b)))
            ctx.Fail("Modifiers vf656_4 flag %u: eax original %x, ours %x", flag, ra, rb);
        if (!(la == lb)) ctx.Fail("Modifiers vf656_4 flag %u: calls differ (original %zu, ours %zu)", flag, la.size(), lb.size());
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("Modifiers vf656_4 flag %u: object changed", flag);
    }
}

// ---------------------------------------------------------------------------------------------------------
// The QueryInterface chain that ends at cTSUICASComponent::QueryInterface (0x75907f) and cTSUIWinHandler::
// QueryInterface (0x755d3c). Each function here stores its own riid (this + offset, with the refcount increment
// inline, or the virtual AddRef in 0x755d3c) and tail-jumps on any other riid. The riids tried are the function's
// own, the ids of the links further down (0x4bb92321, 0x8bd4f230), the ids cGZWin stores (1, 0x22ba0121,
// 0xe98b2f57) and random ones. The cGZWin ids store through ppv with no null check, and the contract requires a
// non-null ppv, so a null ppv is not used with them: the original faults there (a missing check on a required
// argument, not a state a caller reaches).

uint32_t PickChainRiid(std::mt19937 &rng, int i, uint32_t match)
{
    static const uint32_t kChain[] = {0x4bb92321u, 0x8bd4f230u, 1u, 0x22ba0121u, 0xe98b2f57u};
    switch (i % 7) {
    case 0:
    case 1: return match;
    case 2: return kChain[(i / 7) % 5];
    case 3: return Edge(rng, i);
    default: return static_cast<uint32_t>(rng());
    }
}

template <typename Ours>
void RunChain(difftest::Context &ctx, const char *what, uint32_t rva, uint32_t match, uint32_t seed, Ours ours)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(rva);
    InitFakes();
    std::mt19937 rng(seed);
    for (int i = 0; i < 400; ++i) {
        uint8_t a[0x400], b[0x400];
        MakeQiObject(rng, a, sizeof(a));
        std::memcpy(b, a, sizeof(a));
        uint32_t riid = PickChainRiid(rng, i, match);
        bool nullPpv = i % 5 == 0 && riid != 1u && riid != 0x22ba0121u && riid != 0xe98b2f57u;
        CheckQi(ctx, what, orig, ours, riid, nullPpv, a, b, sizeof(a), rng);
    }
}

// cTSUIWinHandler::QueryInterface, 0x755d3c. riid 0x8bd4f230 stores this + 0xf8 and AddRefs (virtual); others go to
// the cGZWin QueryInterface (0x9325d8).
DIFF_TEST(cTSUIWinHandler_QueryInterface, 0x755d3c)
{
    RunChain(ctx, "cTSUIWinHandler QI", 0x755d3c, 0x8bd4f230u, 9400, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<cTSUIWinHandler *>(self)->QueryInterface(riid, ppv);
    });
}

// cTSUICASComponent::QueryInterface, 0x75907f. riid 0x4bb92321 stores this + 0x168 and increments the refcount at +0xc;
// others go to cTSUIWinHandler::QueryInterface.
DIFF_TEST(cTSUICASComponent_QueryInterface, 0x75907f)
{
    RunChain(ctx, "cTSUICASComponent QI", 0x75907f, 0x4bb92321u, 9401, [](void *self, uint32_t riid, void **ppv) {
        return nTSUICAS::cTSUICASComponentQueryInterface(self, nullptr, riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentSimBin_QueryInterface, 0x5f1f44)
{
    RunChain(ctx, "SimBin QI", 0x5f1f44, 0x4c6c9c85u, 9402, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentSimBin *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentBabyMaker_QueryInterface, 0x5f3570)
{
    RunChain(ctx, "BabyMaker QI", 0x5f3570, 0xec76abc6u, 9403, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentBabyMaker *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentArchetypes_QueryInterface, 0x5f67bc)
{
    RunChain(ctx, "Archetypes QI", 0x5f67bc, 0xecb8974au, 9404, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentArchetypes *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentModifiers_QueryInterface, 0x5f8da6)
{
    RunChain(ctx, "Modifiers QI", 0x5f8da6, 0xcca4d188u, 9405, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentModifiers *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentOverlays_QueryInterface, 0x5fb48e)
{
    RunChain(ctx, "Overlays QI", 0x5fb48e, 0x6ca51defu, 9406, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentOverlays *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentBin_QueryInterface, 0x5fcc7a)
{
    RunChain(ctx, "Bin QI", 0x5fcc7a, 0x2caa06e1u, 9407, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentBin *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentSkinTone_QueryInterface, 0x5ff548)
{
    RunChain(ctx, "SkinTone QI", 0x5ff548, 0x0d123d4eu, 9408, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentSkinTone *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentPetCoatColors_QueryInterface, 0x602f00)
{
    RunChain(ctx, "PetCoatColors QI", 0x602f00, 0x713ba27bu, 9409, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentPetCoatColors *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentPetBin_QueryInterface, 0x6051a0)
{
    RunChain(ctx, "PetBin QI", 0x6051a0, 0x91802019u, 9410, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentPetBin *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentBodyOptions_QueryInterface, 0x60610f)
{
    RunChain(ctx, "BodyOptions QI", 0x60610f, 0x719a9d02u, 9411, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentBodyOptions *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentClothing_QueryInterface, 0x5ecf40)
{
    RunChain(ctx, "Clothing QI", 0x5ecf40, 0x8c6bd3edu, 9412, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentClothing *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentPersonality_QueryInterface, 0x5ee467)
{
    RunChain(ctx, "Personality QI", 0x5ee467, 0x0c6bf669u, 9413, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentPersonality *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentGenetics_QueryInterface, 0x5efa4c)
{
    RunChain(ctx, "Genetics QI", 0x5efa4c, 0x8c6c07d7u, 9414, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentGenetics *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentAccessories_QueryInterface, 0x5f1471)
{
    RunChain(ctx, "Accessories QI", 0x5f1471, 0x4c6c0893u, 9415, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentAccessories *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentHair_QueryInterface, 0x5fe291)
{
    RunChain(ctx, "Hair QI", 0x5fe291, 0x0ced66f0u, 9416, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentHair *>(self)->QueryInterface(riid, ppv);
    });
}

DIFF_TEST(cTSUICASComponentPetGenetics_QueryInterface, 0x600f33)
{
    RunChain(ctx, "PetGenetics QI", 0x600f33, 0x11390e39u, 9417, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSUICASComponentPetGenetics *>(self)->QueryInterface(riid, ppv);
    });
}

// cTSHairFilter::QueryInterface, 0x5fe25b. riid 0xd0b524cc stores this and AddRefs (virtual); others go to
// cTSFragmentFilterBase::QueryInterface (0x759042), and a null ppv returns false before the riid is looked at.
DIFF_TEST(cTSHairFilter_QueryInterface, 0x5fe25b)
{
    RunChain(ctx, "HairFilter QI", 0x5fe25b, 0xd0b524ccu, 9418, [](void *self, uint32_t riid, void **ppv) {
        return reinterpret_cast<nTSUICAS::cTSHairFilter *>(self)->QueryInterface(riid, ppv);
    });
}
