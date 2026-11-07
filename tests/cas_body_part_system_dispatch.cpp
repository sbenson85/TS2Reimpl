// Differential tests for the dispatcher and undo functions of nTSCASCore::cTSCASBodyPartSystem's interface subobject
// (vftable 0xeba198): functions whose work is calling other slots of the object and original helpers of the system.
// The slots they call are fakes in a copy of the vtable, and the original helpers are replaced in the mapped image by
// recording fakes (bpt::Redirect), so the original and ours make the same recorded calls. See cas_body_part_system.h.
#include "cas_body_part_system.h"
#include "code_patch.h"

#include <cstdio>

namespace {

using namespace bpt;

// ---- Recording fakes (thiscall emulated with fastcall: ecx = self, edx unused) ---------------------------------------

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

void __fastcall Slot20(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    gEvents.push_back({20, Rel(self), a, b, c});
}
void __fastcall Slot22(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    gEvents.push_back({22, Rel(self), a, b, c, d, e, f});
}
void __fastcall Slot24(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g,
                       uint32_t h, uint32_t i, uint32_t j)
{
    gEvents.push_back({24, Rel(self), a, b, c, d, e, f, g, h, i, j});
}
void __fastcall Slot25(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.push_back({25, Rel(self), a, b, c, d});
}
void __fastcall Slot32(void *self, void *)
{
    gEvents.push_back({32, Rel(self)});
}
void __fastcall Slot33(void *self, void *, const uint8_t *const *items)
{
    // The item vector's first/last pointers differ between sides; its size does not.
    gEvents.push_back({33, Rel(self), static_cast<uint32_t>((reinterpret_cast<uintptr_t>(items[1]) - reinterpret_cast<uintptr_t>(items[0])) / 0x44)});
}
void __fastcall Slot37(void *self, void *, const uint32_t *guid, uint32_t a, uint32_t b)
{
    gEvents.push_back({37, Rel(self), guid[0], guid[1], guid[2], guid[3], a, b});
}
void __fastcall Slot38(void *self, void *, const char *name, uint32_t a, uint32_t b)
{
    gEvents.push_back({38, Rel(self), TextHash(name), a, b});
}
void __fastcall Slot47(void *self, void *, const char *name)
{
    gEvents.push_back({47, Rel(self), TextHash(name)});
}
void __fastcall Slot48(void *self, void *, const char *name, uint32_t weightBits, uint32_t flag)
{
    gEvents.push_back({48, Rel(self), TextHash(name), weightBits, flag});
}

// Original helpers of the whole system (called with this = the system object).
void __fastcall F_ce9530(void *self, void *) { gEvents.push_back({0xce9530, Rel(self)}); }
void __fastcall F_cebb70(void *self, void *) { gEvents.push_back({0xcebb70, Rel(self)}); }
void __fastcall F_cf8670(void *self, void *) { gEvents.push_back({0xcf8670, Rel(self)}); }
void __fastcall F_cf69a0(void *self, void *, uint32_t a) { gEvents.push_back({0xcf69a0, Rel(self), a}); }
void __fastcall F_cf8960(void *self, void *, uint32_t a, uint32_t b) { gEvents.push_back({0xcf8960, Rel(self), a, b}); }
void __fastcall F_cfa2d0(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.push_back({0xcfa2d0, Rel(self), a, b, c, d});
}
void __fastcall F_cfa6e0(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gEvents.push_back({0xcfa6e0, Rel(self), a, b, c, d});
}
void __fastcall F_cf5930(void *self, void *, const char *name, uint32_t weightBits, uint32_t flag)
{
    gEvents.push_back({0xcf5930, Rel(self), TextHash(name), weightBits, flag});
}
void __fastcall F_cf7700(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{
    gEvents.push_back({0xcf7700, Rel(self), a, b, c, d, e, f, g});
}
void __fastcall F_cecea0(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{
    gEvents.push_back({0xcecea0, Rel(self), a, b, c, d, e, f, g});
}

VTable FakeVTable(const difftest::Context &ctx)
{
    VTable v = CopyVTable(ctx);
    v.slots[20] = reinterpret_cast<const void *>(&Slot20);
    v.slots[22] = reinterpret_cast<const void *>(&Slot22);
    v.slots[24] = reinterpret_cast<const void *>(&Slot24);
    v.slots[25] = reinterpret_cast<const void *>(&Slot25);
    v.slots[32] = reinterpret_cast<const void *>(&Slot32);
    v.slots[33] = reinterpret_cast<const void *>(&Slot33);
    v.slots[37] = reinterpret_cast<const void *>(&Slot37);
    v.slots[38] = reinterpret_cast<const void *>(&Slot38);
    v.slots[47] = reinterpret_cast<const void *>(&Slot47);
    v.slots[48] = reinterpret_cast<const void *>(&Slot48);
    return v;
}

// Id lists live in static arrays shared by both sides (the fakes only record the ids).
uint32_t gIdsA[8], gIdsB[8], gIdsC[8];

constexpr int kIface = kIfaceOffset;

template <typename T> void PutAt(Scenario &sc, int ifaceOffset, T value)
{
    std::memcpy(sc.bytes + kIface + ifaceOffset, &value, sizeof(value));
}

void SetLists(Scenario &sc, std::mt19937 &rng, bool a = true, bool b = true, bool c = true)
{
    auto fill = [&](uint32_t *arr, int offFirst, int offLast, bool use) {
        if (!use) return;
        for (int i = 0; i < 8; ++i) arr[i] = static_cast<uint32_t>(rng());
        const int n = static_cast<int>(rng() % 5);
        PutAt<uint32_t *>(sc, offFirst, arr);
        PutAt<uint32_t *>(sc, offLast, arr + n);
    };
    fill(gIdsA, 0x60, 0x64, a);
    fill(gIdsB, 0x6c, 0x70, b);
    fill(gIdsC, 0x78, 0x7c, c);
}

void SetAge(Scenario &sc, std::mt19937 &rng)
{
    PutAt<uint32_t>(sc, 0x118, rng() % 3 == 0 ? 0u : static_cast<uint32_t>(rng() % 5));
}

void SetFlag12A(Scenario &sc, std::mt19937 &rng)
{
    PutAt<uint8_t>(sc, 0x12a, rng() % 3 == 0 ? 0 : static_cast<uint8_t>(rng() % 3 == 0 ? rng() : 1));
}

uint32_t RandomArg(std::mt19937 &rng)
{
    switch (rng() % 4) {
    case 0: return 0;
    case 1: return 1;
    case 2: return static_cast<uint32_t>(rng() % 3) | (static_cast<uint32_t>(rng()) & 0xffffff00u);
    default: return static_cast<uint32_t>(rng());
    }
}

} // namespace

// ---- Visitors ------------------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_VisitIds, 0xce3280)
{
    std::mt19937 rng(0xce3280);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0xce3280);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLists(sc, rng);
        const uint32_t a = static_cast<uint32_t>(rng()), b = static_cast<uint32_t>(rng());
        Compare(ctx, "VisitIds", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), a, b);
            else w.S()->VisitIds(a, b);
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_VisitIdsF7, 0xcf76a0)
{
    std::mt19937 rng(0xcf76a0);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0xcf76a0);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect fake(ctx, 0xcf7700, reinterpret_cast<const void *>(&F_cf7700));
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLists(sc, rng);
        uint32_t p[6];
        for (auto &x : p) x = static_cast<uint32_t>(rng());
        Compare(ctx, "VisitIdsF7", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), p[0], p[1], p[2], p[3], p[4], p[5]);
            else w.S()->VisitIdsF7(p[0], p[1], p[2], p[3], p[4], p[5]);
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_VisitIdsEA, 0xcece60)
{
    std::mt19937 rng(0xcece60);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0xcece60);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect fake(ctx, 0xcecea0, reinterpret_cast<const void *>(&F_cecea0));
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLists(sc, rng);
        uint32_t p[6];
        for (auto &x : p) x = static_cast<uint32_t>(rng());
        Compare(ctx, "VisitIdsEA", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), p[0], p[1], p[2], p[3], p[4], p[5]);
            else w.S()->VisitIdsEA(p[0], p[1], p[2], p[3], p[4], p[5]);
            return 0;
        });
    }
}

// Slot 38: the text is parsed by the original 0x4f8eb0 into a GUID; strings of hex digits, dashes and other characters.
DIFF_TEST(cTSCASBodyPartSystem_ApplyGuidText, 0xcf5ca0)
{
    std::mt19937 rng(0xcf5ca0);
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *, uint32_t, uint32_t)>(0xcf5ca0);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    static const char kAlphabet[] = "0123456789abcdefABCDEF-----xyz{} ";
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        char text[64];
        const int len = rng() % 4 == 0 ? static_cast<int>(rng() % 10) : 30 + static_cast<int>(rng() % 8);
        for (int k = 0; k < len; ++k) text[k] = kAlphabet[rng() % (sizeof(kAlphabet) - 1 - (rng() % 4 ? 3 : 0))];
        text[len] = 0;
        const uint32_t a = static_cast<uint32_t>(rng()), b = static_cast<uint32_t>(rng());
        Compare(ctx, "ApplyGuidText", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), text, a, b);
            else w.S()->ApplyGuidText(text, a, b);
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_StoreKey, 0xcf8140)
{
    std::mt19937 rng(0xcf8140);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, const void *)>(0xcf8140);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect fake1(ctx, 0xcf69a0, reinterpret_cast<const void *>(&F_cf69a0));
    Redirect fake2(ctx, 0xcf8670, reinterpret_cast<const void *>(&F_cf8670));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        const uint32_t index = rng() % 5 == 0 ? static_cast<uint32_t>(rng()) : rng() % 3;
        uint8_t key[16];
        for (auto &k : key) k = static_cast<uint8_t>(rng());
        if (rng() % 3 == 0 && index <= 1) std::memcpy(key, sc.bytes + kIface + 0xe8 + 16 * index, 16); // unchanged key
        else if (rng() % 4 == 0 && index <= 1) { // differs in one byte only
            std::memcpy(key, sc.bytes + kIface + 0xe8 + 16 * index, 16);
            key[rng() % 16] ^= 0x40;
        }
        Compare(ctx, "StoreKey", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), index, key);
            else w.S()->StoreKey(index, key);
            return 0;
        });
    }
}

// ---- Dispatchers ---------------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_ApplyAll, 0xcfac20)
{
    std::mt19937 rng(0xcfac20);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0xcfac20);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect f1(ctx, 0xcf8960, reinterpret_cast<const void *>(&F_cf8960));
    Redirect f2(ctx, 0xcfa2d0, reinterpret_cast<const void *>(&F_cfa2d0));
    Redirect f3(ctx, 0xcfa6e0, reinterpret_cast<const void *>(&F_cfa6e0));
    for (int i = 0; i < 4000; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLists(sc, rng);
        SetAge(sc, rng);
        SetFlag12A(sc, rng);
        uint32_t p[6];
        for (auto &x : p) x = RandomArg(rng);
        Compare(ctx, "ApplyAll", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), p[0], p[1], p[2], p[3], p[4], p[5]);
            else w.S()->ApplyAll(p[0], p[1], p[2], p[3], p[4], p[5]);
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_ApplyAllPasses, 0xcfade0)
{
    std::mt19937 rng(0xcfade0);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0xcfade0);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect f2(ctx, 0xcfa2d0, reinterpret_cast<const void *>(&F_cfa2d0));
    Redirect f3(ctx, 0xcfa6e0, reinterpret_cast<const void *>(&F_cfa6e0));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        SetLists(sc, rng, false, false, true);
        SetFlag12A(sc, rng);
        uint32_t p[4];
        for (auto &x : p) x = RandomArg(rng);
        Compare(ctx, "ApplyAllPasses", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), p[0], p[1], p[2], p[3]);
            else w.S()->ApplyAllPasses(p[0], p[1], p[2], p[3]);
            return 0;
        });
    }
}

// ---- Undo and redo -------------------------------------------------------------------------------------------------

DIFF_TEST(cTSCASBodyPartSystem_PopEntry, 0xcfab70)
{
    std::mt19937 rng(0xcfab70);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xcfab70);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect f1(ctx, 0xce9530, reinterpret_cast<const void *>(&F_ce9530));
    Redirect f2(ctx, 0xcebb70, reinterpret_cast<const void *>(&F_cebb70));
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        const uint32_t flag = RandomArg(rng);
        Compare(ctx, "PopEntry", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S(), flag);
            else w.S()->PopEntry(flag);
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_RedoEntry, 0xcf6820)
{
    std::mt19937 rng(0xcf6820);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcf6820);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect f1(ctx, 0xcf5930, reinterpret_cast<const void *>(&F_cf5930));
    for (int i = 0; i < 4000; ++i) {
        Scenario sc = RandomScenario(rng);
        if (!sc.redo.empty() && rng() % 3) sc.redo.back().kind = static_cast<int32_t>(rng() % 10); // the newest record decides
        Compare(ctx, "RedoEntry", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S());
            else w.S()->RedoEntry();
            return 0;
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_ClearStacks, 0xcf6e80)
{
    std::mt19937 rng(0xcf6e80);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcf6e80);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect f1(ctx, 0xce9530, reinterpret_cast<const void *>(&F_ce9530));
    Redirect f2(ctx, 0xcebb70, reinterpret_cast<const void *>(&F_cebb70));
    for (int i = 0; i < 2000; ++i) {
        Scenario sc = RandomScenario(rng);
        Compare(ctx, "ClearStacks", sc, vt, [&](World &w, bool original) -> uint64_t {
            if (original) orig(w.S());
            else w.S()->ClearStacks();
            return 0;
        });
    }
}

// ---- Tree resets (slots 10 and 9) ------------------------------------------------------------------------------------

namespace {

using nTSCASCore::TreeHeader;

// The heap map headers of the side being run, for naming them in recorded calls.
const void *gTreePtrs[6];

uint32_t NormPtr(const void *p)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p), lo = reinterpret_cast<uintptr_t>(gBase);
    if (v >= lo && v < lo + kObject) return static_cast<uint32_t>(v - lo);
    for (int i = 0; i < 6; ++i)
        if (p == gTreePtrs[i]) return 0xA0000000u + static_cast<uint32_t>(i);
    return 0xDEADBEEFu;
}

void __fastcall F_erase_cdb400(void *self, void *, void *a, void *root)
{
    gEvents.push_back({0xcdb400, NormPtr(self), NormPtr(a), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))});
}
void __fastcall F_erase_cdb5e0(void *self, void *, void *a, void *root)
{
    gEvents.push_back({0xcdb5e0, NormPtr(self), NormPtr(a), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))});
}
void __fastcall F_erase_cdc280(void *self, void *, void *a, void *root)
{
    gEvents.push_back({0xcdc280, NormPtr(self), NormPtr(a), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))});
}
void __fastcall F_erase_cdc300(void *self, void *, void *a, void *root)
{
    gEvents.push_back({0xcdc300, NormPtr(self), NormPtr(a), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(root))});
}
void __fastcall F_d0a9e0(void *self, void *) { gEvents.push_back({0xd0a9e0, Rel(self)}); }
void __fastcall F_cfebc0(void *self, void *) { gEvents.push_back({0xcfebc0, Rel(self)}); }
void __fastcall F_ce9690(void *self, void *) { gEvents.push_back({0xce9690, Rel(self)}); }
void __fastcall Slot16(void *self, void *) { gEvents.push_back({16, Rel(self)}); }

// A map header on the heap whose head node's root link is a token (the fakes never walk the tree).
TreeHeader *MakeHeapTree(uint32_t token, uint32_t size)
{
    auto *head = new TreeHeader::Node{reinterpret_cast<TreeHeader::Node *>(0x11110000u + token),
                                      reinterpret_cast<TreeHeader::Node *>(0x22220000u + token),
                                      reinterpret_cast<TreeHeader::Node *>(0x33330000u + token)};
    return new TreeHeader{head, size};
}

// 1 when the tree is empty again (head links to itself, size 0), else a hash of what it holds.
uint64_t TreeState(const TreeHeader *t)
{
    const bool reset = t->mHead->mLeft == t->mHead && t->mHead->mParent == t->mHead && t->mHead->mRight == t->mHead && t->mSize == 0;
    return reset ? 1u : (reinterpret_cast<uintptr_t>(t->mHead->mParent) ^ t->mSize) | 2u;
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_ClearTreeAt14, 0xcf71f0)
{
    std::mt19937 rng(0xcf71f0);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcf71f0);
    VTable vt = FakeVTable(ctx);
    Env env(ctx);
    Redirect fake(ctx, 0xcdb400, reinterpret_cast<const void *>(&F_erase_cdb400));
    for (int i = 0; i < 500; ++i) {
        Scenario sc = RandomScenario(rng);
        const uint32_t token = static_cast<uint32_t>(rng() % 1000), size = static_cast<uint32_t>(rng() % 50);
        Compare(ctx, "ClearTreeAt14", sc, vt, [&](World &w, bool original) -> uint64_t {
            TreeHeader *t = MakeHeapTree(token, size);
            gTreePtrs[1] = t;
            w.S()->mTree14 = t;
            if (original) orig(w.S());
            else w.S()->ClearTreeAt14();
            const uint64_t state = TreeState(t);
            w.S()->mTree14 = nullptr;
            return state;
        });
    }
}

// Slot 9: the headers at +0xc (heap), +(age+7)*8 and +0x58 (inline) are cleared; the age is kept to 0..3 so that the
// inline header lies inside the object at a place this test has prepared.
DIFF_TEST(cTSCASBodyPartSystem_ResetState, 0xcf7110)
{
    std::mt19937 rng(0xcf7110);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xcf7110);
    VTable vt = FakeVTable(ctx);
    vt.slots[16] = reinterpret_cast<const void *>(&Slot16);
    Env env(ctx);
    Redirect r1(ctx, 0xcdb5e0, reinterpret_cast<const void *>(&F_erase_cdb5e0));
    Redirect r2(ctx, 0xcdc280, reinterpret_cast<const void *>(&F_erase_cdc280));
    Redirect r3(ctx, 0xcdc300, reinterpret_cast<const void *>(&F_erase_cdc300));
    Redirect r4(ctx, 0xd0a9e0, reinterpret_cast<const void *>(&F_d0a9e0));
    Redirect r5(ctx, 0xcfebc0, reinterpret_cast<const void *>(&F_cfebc0));
    Redirect r6(ctx, 0xce9530, reinterpret_cast<const void *>(&F_ce9530));
    // 0xce9690 is ours now (cTSCASBodyPartSystem::ResetKeys): the same stand-in over the image's and ours.
    shape::StandIn r7(0xce9690, &nTSCASCore::cTSCASBodyPartSystem::ResetKeys, &F_ce9690);
    for (int i = 0; i < 1500; ++i) {
        Scenario sc = RandomScenario(rng);
        const uint32_t age = rng() % 4; // age 4 would alias the +0x58 header (the second erase then sees a head pointer as its root)
        PutAt<uint32_t>(sc, 0x118, age);
        uint32_t tokens[6], sizes[6];
        for (int k = 0; k < 6; ++k) { tokens[k] = static_cast<uint32_t>(rng() % 1000); sizes[k] = static_cast<uint32_t>(rng() % 50); }
        Compare(ctx, "ResetState", sc, vt, [&](World &w, bool original) -> uint64_t {
            // heap header at +0xc; inline headers at +0x40, +0x48, +0x50, +0x58 (ages 1..4 and the fixed one)
            TreeHeader *heap = MakeHeapTree(tokens[0], sizes[0]);
            gTreePtrs[0] = heap;
            w.S()->mTreeC = heap;
            TreeHeader *inl[4];
            for (int k = 0; k < 4; ++k) {
                inl[k] = reinterpret_cast<TreeHeader *>(reinterpret_cast<uint8_t *>(w.S()) + 0x40 + 8 * k);
                TreeHeader *t = MakeHeapTree(tokens[k + 1], sizes[k + 1]);
                *inl[k] = *t;
            }
            if (original) orig(w.S());
            else w.S()->ResetState();
            uint64_t h = TreeState(heap);
            for (int k = 0; k < 4; ++k) h = h * 31 + TreeState(inl[k]);
            // the heap pointers differ between sides: clear the fields that hold them before the object bytes are compared
            w.S()->mTreeC = nullptr;
            std::memset(reinterpret_cast<uint8_t *>(w.S()) + 0x40, 0, 0x20);
            return h;
        });
    }
}

// ---- Newest-record lookups (0xcec4a0, 0xcec4d0, slots 34 and 36) ----------------------------------------------------------

namespace {

// The index of a record of the world's stack, or -1 for null.
int64_t IndexOf(World &w, const BodyPartEntry *e)
{
    return e ? static_cast<int64_t>(e - w.stack->mFirst) : -1;
}

void BiasKinds(Scenario &sc, std::mt19937 &rng, int32_t kind)
{
    for (auto &s : sc.stack)
        if (rng() % 3 == 0) s.kind = kind;
    if (!sc.stack.empty() && rng() % 2) {
        sc.stack.back().kind = kind;
        sc.stack.back().items = static_cast<int>(rng() % 4);
    }
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_FindNewestKind5, 0xcec4a0)
{
    std::mt19937 rng(0xcec4a0);
    auto orig = ctx.Original<BodyPartEntry *(__thiscall *)(void *)>(0xcec4a0);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack = RandomSpecs(rng, 8);
        Compare(ctx, "FindNewestKind5", sc, vt, [&](World &w, bool original) -> uint64_t {
            return static_cast<uint64_t>(IndexOf(w, original ? orig(w.Base()) : reinterpret_cast<nTSCASCore::cTSCASBodyPartSystem *>(w.Base())->FindNewestKind5()));
        });
    }
}

DIFF_TEST(cTSCASBodyPartSystem_FindNewestKind6, 0xcec4d0)
{
    std::mt19937 rng(0xcec4d0);
    auto orig = ctx.Original<BodyPartEntry *(__thiscall *)(void *)>(0xcec4d0);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 3000; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack = RandomSpecs(rng, 8);
        Compare(ctx, "FindNewestKind6", sc, vt, [&](World &w, bool original) -> uint64_t {
            return static_cast<uint64_t>(IndexOf(w, original ? orig(w.Base()) : reinterpret_cast<nTSCASCore::cTSCASBodyPartSystem *>(w.Base())->FindNewestKind6()));
        });
    }
}

// Slot 34: the vector to compare is built from the newest kind-5 record's own items, then perturbed in one place half of the time.
DIFF_TEST(cTSCASBodyPartSystem_ItemsMatchNewestKind5, 0xcf3360)
{
    std::mt19937 rng(0xcf3360);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const nTSCASCore::BodyPartItemVector *)>(0xcf3360);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 5000; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack = RandomSpecs(rng, 6);
        BiasKinds(sc, rng, 5);
        const int perturb = static_cast<int>(rng() % 6);   // 0 none, 1 first string, 2 second string, 3 value, 4 extra item, 5 one item fewer
        const uint32_t pick = static_cast<uint32_t>(rng());
        Compare(ctx, "ItemsMatchNewestKind5", sc, vt, [&](World &w, bool original) -> uint64_t {
            const BodyPartEntry *rec = nullptr;
            for (const BodyPartEntry *e = w.stack->mLast; e != w.stack->mFirst;) {
                --e;
                if (e->mKind == 5) { rec = e; break; }
            }
            std::vector<nTSCASCore::BodyPartItem> items;
            if (rec) {
                const auto *src = reinterpret_cast<const nTSCASCore::BodyPartItem *>(rec->mItemsFirst);
                const size_t n = (rec->mItemsLast - rec->mItemsFirst) / 0x44;
                items.resize(n);
                for (size_t k = 0; k < n; ++k) {
                    items[k].mFirst.Construct(src[k].mFirst.mString.c_str());
                    items[k].mSecond.Construct(src[k].mSecond.mString.c_str());
                    items[k].mValue = src[k].mValue;
                }
                if (!items.empty()) {
                    auto &it = items[pick % items.size()];
                    if (perturb == 1) it.mFirst.mString += "!";
                    if (perturb == 2) it.mSecond.mString += "!";
                    if (perturb == 3) it.mValue ^= 1;
                    if (perturb == 5) items.pop_back();
                }
                if (perturb == 4) { items.emplace_back(); items.back().mFirst.Construct(""); items.back().mSecond.Construct(""); items.back().mValue = 0; }
            }
            nTSCASCore::BodyPartItemVector v{items.data(), items.data() + items.size(), items.data() + items.size()};
            const uint32_t r = original ? orig(w.S(), &v) : static_cast<uint32_t>(w.S()->ItemsMatchNewestKind5(&v));
            return r & 0xff;
        });
    }
}

// Slot 36: the text is read through the named weight's vtable slot 5; the weight is compared as a float.
DIFF_TEST(cTSCASBodyPartSystem_MatchesNewestKind6, 0xcf3410)
{
    std::mt19937 rng(0xcf3410);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const nTSCASCore::NamedWeight *)>(0xcf3410);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 5000; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack = RandomSpecs(rng, 6);
        BiasKinds(sc, rng, 6);
        const int nameMode = static_cast<int>(rng() % 3), weightMode = static_cast<int>(rng() % 3);
        const char *other = RandomName(rng);
        Compare(ctx, "MatchesNewestKind6", sc, vt, [&](World &w, bool original) -> uint64_t {
            const BodyPartEntry *rec = nullptr;
            for (const BodyPartEntry *e = w.stack->mLast; e != w.stack->mFirst;) {
                --e;
                if (e->mKind == 6) { rec = e; break; }
            }
            nTSCASCore::NamedWeight named;
            named.mName.Construct(rec && nameMode != 0 ? rec->Name() : other);
            named.mWeight = rec && weightMode != 0 ? rec->mWeight : 0.5f;
            const uint32_t r = original ? orig(w.S(), &named) : static_cast<uint32_t>(w.S()->MatchesNewestKind6(&named));
            named.mName.Destruct();
            return r & 0xff;
        });
    }
}

// ---- Name collectors (slots 44 and 46) -------------------------------------------------------------------------------

namespace {

using StrVec = nTSCASCore::StringVector;

// Builds a vector<cRZString> with the game's own push_back, so its storage is the game's.
StrVec *BuildStrVec(difftest::Context &ctx, const std::vector<std::string> &names)
{
    auto push = ctx.Original<void(__thiscall *)(StrVec *, const cRZString *)>(0x3a05e0);
    auto *v = new StrVec{nullptr, nullptr, nullptr};
    for (const auto &n : names) {
        cRZString s;
        s.Construct(n.c_str());
        push(v, &s);
        s.Destruct();
    }
    return v;
}

uint64_t HashStrVec(const StrVec *v)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    mix(static_cast<uint64_t>(v->mLast - v->mFirst));
    mix(static_cast<uint64_t>(v->mEnd - v->mFirst));
    for (const cRZString *s = v->mFirst; s != v->mLast; ++s) {
        mix(s->mString.size());
        for (char c : s->mString) mix(static_cast<uint8_t>(c));
        mix(s->mRefCount);
    }
    return h;
}

template <typename Ours>
void CheckCollect(difftest::Context &ctx, const char *name, uint32_t rva, Ours ours, int32_t wantedValue)
{
    std::mt19937 rng(rva);
    auto orig = ctx.Original<void(__thiscall *)(void *, StrVec *)>(rva);
    const VTable vt = FakeVTable(ctx);
    Env env(ctx);
    for (int i = 0; i < 2500; ++i) {
        Scenario sc = RandomScenario(rng);
        sc.stack = RandomSpecs(rng, 7);
        for (auto &s : sc.stack) {
            if (rng() % 2) {
                s.kind = 3;
                s.value = rng() % 3 == 0 ? wantedValue : static_cast<int32_t>(rng() % 12);
            }
        }
        SetAge(sc, rng);
        std::vector<std::string> before;
        for (unsigned k = rng() % 4; k > 0; --k) before.push_back(RandomName(rng));
        Compare(ctx, name, sc, vt, [&](World &w, bool original) -> uint64_t {
            StrVec *out = BuildStrVec(ctx, before);
            if (original) orig(w.S(), out);
            else (w.S()->*ours)(out);
            return HashStrVec(out);
        });
    }
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_CollectNamesValue0B, 0xcf1f70)
{
    CheckCollect(ctx, "CollectNamesValue0B", 0xcf1f70, &Iface::CollectNamesValue0B, 0xb);
}

DIFF_TEST(cTSCASBodyPartSystem_CollectNamesValue5, 0xcf1d90)
{
    CheckCollect(ctx, "CollectNamesValue5", 0xcf1d90, &Iface::CollectNamesValue5, 5);
}
