// Differential tests for nTSCASCore::cTSSimOutfitData (primary vtable 0xe8b25c, 116 slots).
//
// Each trial builds one random object three times with the same bytes and the same vtable pointer: the original runs
// on the first side, ours on the second. The vtable is a copy of the original's with two recording fakes: slot 16
// (called by LowWordIsZero, which returns a random dword) and slot 53 (called by SetDword50Notify). Results, recorded
// calls, the object bytes and canaries around the object are compared. Pointer results are compared as offsets from
// their side's object.
#include "difftest.h"
#include "cas/cTSSimOutfitData.h"
#include "game/vtable.h"

#include <array>
#include <cstring>
#include <random>
#include <vector>

namespace {

using Obj = nTSCASCore::cTSSimOutfitData;

constexpr int kSlots = 116;
constexpr int kObjectSize = 0x200; // covers the fields up to +0x188

// ---- Recording fakes -------------------------------------------------------------------------------------------

using Event = std::array<uint32_t, 4>; // kind, object offset, and up to two values
std::vector<Event> gEvents;
enum : uint32_t { kEvSlot16 = 1, kEvSlot53 };

const uint8_t *gCur = nullptr; // the side whose offsets the fakes record

uint32_t Rel(const void *p, const void *base)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(base));
}

uint32_t gRet16 = 0;

uint32_t __fastcall FakeSlot16(void *self, void *)
{
    gEvents.push_back({kEvSlot16, Rel(self, gCur), 0, 0});
    return gRet16;
}

void __fastcall FakeSlot53(void *self, void *)
{
    gEvents.push_back({kEvSlot53, Rel(self, gCur), 0, 0});
}

struct VTable {
    const void *slots[kSlots];
};

VTable MakeVTable(const difftest::Context &ctx)
{
    VTable v;
    std::memcpy(v.slots, ctx.image.At(Obj::kVTable), sizeof(v.slots));
    v.slots[16] = reinterpret_cast<const void *>(&FakeSlot16);
    v.slots[53] = reinterpret_cast<const void *>(&FakeSlot53);
    return v;
}

// ---- Sides -------------------------------------------------------------------------------------------------------

struct Init {
    uint8_t body[kObjectSize];
};

struct Side {
    uint8_t front[32];
    uint8_t body[kObjectSize];
    uint8_t back[32];
};

Init RandomInit(std::mt19937 &rng, const void *vt)
{
    Init init;
    for (auto &b : init.body) b = static_cast<uint8_t>(rng());
    const uintptr_t v = reinterpret_cast<uintptr_t>(vt);
    std::memcpy(init.body, &v, sizeof(v));
    return init;
}

template <typename T> void Put(Init &init, intptr_t offset, T value)
{
    std::memcpy(init.body + offset, &value, sizeof(value));
}

void Fill(Side &s, const Init &init)
{
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.body, init.body, sizeof(s.body));
}

bool CanariesIntact(const Side &s)
{
    for (uint8_t b : s.front)
        if (b != 0xA5) return false;
    for (uint8_t b : s.back)
        if (b != 0x5A) return false;
    return true;
}

// Runs the operation on both sides, then compares the result, the recorded calls, the object and the canaries.
// run(side, true) runs the original, run(side, false) ours.
template <typename Run> void Compare(difftest::Context &ctx, const char *name, const Init &init, Run run)
{
    static Side a, b;
    Fill(a, init);
    Fill(b, init);

    gCur = a.body;
    gEvents.clear();
    const uint64_t ra = run(a, true);
    const std::vector<Event> ea = gEvents;

    gCur = b.body;
    gEvents.clear();
    const uint64_t rb = run(b, false);
    const std::vector<Event> eb = gEvents;

    ++ctx.cases;
    if (ra != rb) {
        ctx.Fail("%s: result original %llu, ours %llu", name, static_cast<unsigned long long>(ra),
                 static_cast<unsigned long long>(rb));
    }
    if (ea != eb) ctx.Fail("%s: calls made differ (original %zu, ours %zu)", name, ea.size(), eb.size());
    if (std::memcmp(a.body, b.body, sizeof(a.body)) != 0) ctx.Fail("%s: object state differs", name);
    if (!CanariesIntact(a) || !CanariesIntact(b)) ctx.Fail("%s: canaries overwritten", name);
}

// ---- Block copies (slots 56, 57, 58) -----------------------------------------------------------------------------

// Source is either a random buffer outside the object, or 16 bytes of the object starting k bytes from the
// destination (overlapping it, which the original's load-then-store copes with).
template <typename Ours>
void CheckBlock(difftest::Context &ctx, const char *name, uint32_t rva, intptr_t offset, Ours ours,
                std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(rva);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        uint8_t external[16];
        for (auto &b : external) b = static_cast<uint8_t>(rng());
        const bool overlap = rng() % 2 == 0;
        const int k = static_cast<int>(rng() % 31) - 15;
        Compare(ctx, name, init, [&](Side &s, bool original) -> uint64_t {
            const uint8_t *src = overlap ? s.body + offset + k : external;
            if (original) {
                orig(s.body, src);
            } else {
                (reinterpret_cast<Obj *>(s.body)->*ours)(src);
            }
            return 0;
        });
    }
}

// ---- Single-value stores (slots 15, 109, 54) ---------------------------------------------------------------------

void CheckByte14C(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xba6f60);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        const uint8_t value = static_cast<uint8_t>(rng());
        Compare(ctx, "StoreByte14C", init, [&](Side &s, bool original) -> uint64_t {
            if (original) {
                orig(s.body, value);
            } else {
                reinterpret_cast<Obj *>(s.body)->StoreByte14C(value);
            }
            return 0;
        });
    }
}

void CheckDword134(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xba6f00);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        const uint32_t value = static_cast<uint32_t>(rng());
        Compare(ctx, "StoreDword134", init, [&](Side &s, bool original) -> uint64_t {
            if (original) {
                orig(s.body, value);
            } else {
                reinterpret_cast<Obj *>(s.body)->StoreDword134(value);
            }
            return 0;
        });
    }
}

// The argument is passed as its bits (see StoreFloatBits58): random bit patterns, NaNs and infinities included.
void CheckFloatBits58(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xba6f30);
    static const uint32_t kSpecial[] = {0x00000000u, 0x80000000u, 0x3f800000u, 0x7f800000u, 0xff800000u,
                                        0x7fc00000u, 0x7f800001u, 0xffc12345u};
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        const uint32_t value = rng() % 3 == 0 ? kSpecial[rng() % 8] : static_cast<uint32_t>(rng());
        Compare(ctx, "StoreFloatBits58", init, [&](Side &s, bool original) -> uint64_t {
            if (original) {
                orig(s.body, value);
            } else {
                reinterpret_cast<Obj *>(s.body)->StoreFloatBits58(value);
            }
            return 0;
        });
    }
}

// ---- Address getters (slots 17, 37) ------------------------------------------------------------------------------

void CheckAddress(difftest::Context &ctx, const char *name, uint32_t rva, void *(Obj::*ours)(), std::mt19937 &rng,
                  int trials, const void *vt)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(rva);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        Compare(ctx, name, init, [&](Side &s, bool original) -> uint64_t {
            void *p = original ? orig(s.body) : (reinterpret_cast<Obj *>(s.body)->*ours)();
            return Rel(p, s.body);
        });
    }
}

// ---- Dword at +0x50 (slot 24) ------------------------------------------------------------------------------------

void CheckDword50OrOne(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xba9f80);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        Put<uint32_t>(init, 0x50, rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng()));
        Compare(ctx, "Dword50OrOne", init, [&](Side &s, bool original) -> uint64_t {
            if (original) return orig(s.body);
            return reinterpret_cast<Obj *>(s.body)->Dword50OrOne();
        });
    }
}

// ---- Lowest-bit table lookup (slot 46) ---------------------------------------------------------------------------

void CheckSlotForLowestBit(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xb99410);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        uint32_t mask;
        switch (rng() % 4) {
        case 0: mask = 0; break;
        case 1: mask = 1u << (rng() % 32); break;
        case 2: mask = static_cast<uint32_t>(rng()) & static_cast<uint32_t>(rng()); break;
        default: mask = static_cast<uint32_t>(rng()); break;
        }
        Compare(ctx, "SlotForLowestBit", init, [&](Side &s, bool original) -> uint64_t {
            if (original) return orig(s.body, mask);
            return reinterpret_cast<Obj *>(s.body)->SlotForLowestBit(mask);
        });
    }
}

// ---- Flag test (slot 47) -----------------------------------------------------------------------------------------

void CheckFlagsContain(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xbaa250);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        Put<uint32_t>(init, 0x150, static_cast<uint32_t>(rng()) & static_cast<uint32_t>(rng()));
        uint32_t mask;
        switch (rng() % 3) {
        case 0: mask = 0; break;
        case 1: mask = static_cast<uint32_t>(rng()) & 0x3f; break;
        default: mask = static_cast<uint32_t>(rng()); break;
        }
        Compare(ctx, "FlagsContain", init, [&](Side &s, bool original) -> uint64_t {
            // Only al is the result (sete al): compared as a byte.
            if (original) return static_cast<uint8_t>(orig(s.body, mask));
            return static_cast<uint8_t>(reinterpret_cast<Obj *>(s.body)->FlagsContain(mask));
        });
    }
}

// ---- Bit set and clear (slot 111) --------------------------------------------------------------------------------

void CheckSetMaskBits150(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0xba6a20);
    static const uint32_t kSets[] = {0, 1, 0x100, 0xff, 0x80, 0x10000, 0x7f, 0xffffff00u};
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        const uint32_t bits = rng() % 4 == 0 ? 0 : static_cast<uint32_t>(rng());
        const uint32_t set = kSets[rng() % 8];
        Compare(ctx, "SetMaskBits150", init, [&](Side &s, bool original) -> uint64_t {
            if (original) {
                orig(s.body, bits, set);
            } else {
                reinterpret_cast<Obj *>(s.body)->SetMaskBits150(bits, set);
            }
            return 0;
        });
    }
}

// ---- Table fill (slot 110) ---------------------------------------------------------------------------------------

void CheckFillSlots154(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0xba6b50);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        uint32_t mask;
        switch (rng() % 4) {
        case 0: mask = 0; break;
        case 1: mask = 0x3fff; break;
        case 2: mask = static_cast<uint32_t>(rng()) & 0x3fff; break;
        default: mask = static_cast<uint32_t>(rng()); break;
        }
        uint32_t value;
        switch (rng() % 4) {
        case 0: value = 0xffffffffu; break;
        case 1: value = 0; break;
        default: value = static_cast<uint32_t>(rng()); break;
        }
        Compare(ctx, "FillSlots154", init, [&](Side &s, bool original) -> uint64_t {
            if (original) {
                orig(s.body, mask, value);
            } else {
                reinterpret_cast<Obj *>(s.body)->FillSlots154(mask, value);
            }
            return 0;
        });
    }
}

// ---- Notifying store (slot 52) -----------------------------------------------------------------------------------

void CheckSetDword50Notify(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xba6f10);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        const uint32_t current = rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng() % 4);
        Put<uint32_t>(init, 0x50, current);
        // Often the same value (no call), otherwise a small value or a random one.
        uint32_t value;
        switch (rng() % 3) {
        case 0: value = current; break;
        case 1: value = static_cast<uint32_t>(rng() % 4); break;
        default: value = static_cast<uint32_t>(rng()); break;
        }
        Compare(ctx, "SetDword50Notify", init, [&](Side &s, bool original) -> uint64_t {
            if (original) {
                orig(s.body, value);
            } else {
                reinterpret_cast<Obj *>(s.body)->SetDword50Notify(value);
            }
            return 0;
        });
    }
}

// ---- Low word of slot 16 (slot 18) -------------------------------------------------------------------------------

void CheckLowWordIsZero(difftest::Context &ctx, std::mt19937 &rng, int trials, const void *vt)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(0xb9aa80);
    static const uint32_t kResults[] = {0, 1, 0x10000, 0x20000, 0xffff0000u, 0x0000ffffu, 0x00010001u, 0x8000u};
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng, vt);
        gRet16 = rng() % 2 == 0 ? kResults[rng() % 8] : static_cast<uint32_t>(rng());
        Compare(ctx, "LowWordIsZero", init, [&](Side &s, bool original) -> uint64_t {
            // Only al is the result (sete al): compared as a byte.
            if (original) return static_cast<uint8_t>(orig(s.body));
            return static_cast<uint8_t>(reinterpret_cast<Obj *>(s.body)->LowWordIsZero());
        });
    }
}

} // namespace

// The three block copies. Offsets are the fields they write (+0x6c, +0x7c, +0x8c).
DIFF_TEST(cTSSimOutfitData_StoreBlock6C, 0xba6be0)
{
    std::mt19937 rng(0x3a01);
    VTable vt = MakeVTable(ctx);
    CheckBlock(ctx, "StoreBlock6C", 0xba6be0, 0x6c, &Obj::StoreBlock6C, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_StoreBlock7C, 0xb81c80)
{
    std::mt19937 rng(0x3a57);
    VTable vt = MakeVTable(ctx);
    CheckBlock(ctx, "StoreBlock7C", 0xb81c80, 0x7c, &Obj::StoreBlock7C, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_StoreBlock8C, 0xb81970)
{
    std::mt19937 rng(0x3a58);
    VTable vt = MakeVTable(ctx);
    CheckBlock(ctx, "StoreBlock8C", 0xb81970, 0x8c, &Obj::StoreBlock8C, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_StoreByte14C, 0xba6f60)
{
    std::mt19937 rng(0x3a15);
    VTable vt = MakeVTable(ctx);
    CheckByte14C(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_StoreDword134, 0xba6f00)
{
    std::mt19937 rng(0x3a6d);
    VTable vt = MakeVTable(ctx);
    CheckDword134(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_StoreFloatBits58, 0xba6f30)
{
    std::mt19937 rng(0x3a54);
    VTable vt = MakeVTable(ctx);
    CheckFloatBits58(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_Address138, 0xb94ce0)
{
    std::mt19937 rng(0x3a17);
    VTable vt = MakeVTable(ctx);
    CheckAddress(ctx, "Address138", 0xb94ce0, &Obj::Address138, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_Address0E4, 0xb9be90)
{
    std::mt19937 rng(0x3a37);
    VTable vt = MakeVTable(ctx);
    CheckAddress(ctx, "Address0E4", 0xb9be90, &Obj::Address0E4, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_Dword50OrOne, 0xba9f80)
{
    std::mt19937 rng(0x3a24);
    VTable vt = MakeVTable(ctx);
    CheckDword50OrOne(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_SlotForLowestBit, 0xb99410)
{
    std::mt19937 rng(0x3a46);
    VTable vt = MakeVTable(ctx);
    CheckSlotForLowestBit(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_FlagsContain, 0xbaa250)
{
    std::mt19937 rng(0x3a47);
    VTable vt = MakeVTable(ctx);
    CheckFlagsContain(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_SetMaskBits150, 0xba6a20)
{
    std::mt19937 rng(0x3a6f);
    VTable vt = MakeVTable(ctx);
    CheckSetMaskBits150(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_FillSlots154, 0xba6b50)
{
    std::mt19937 rng(0x3a6e);
    VTable vt = MakeVTable(ctx);
    CheckFillSlots154(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_SetDword50Notify, 0xba6f10)
{
    std::mt19937 rng(0x3a34);
    VTable vt = MakeVTable(ctx);
    CheckSetDword50Notify(ctx, rng, 4000, vt.slots);
}

DIFF_TEST(cTSSimOutfitData_LowWordIsZero, 0xb9aa80)
{
    std::mt19937 rng(0x3a12);
    VTable vt = MakeVTable(ctx);
    CheckLowWordIsZero(ctx, rng, 4000, vt.slots);
}
