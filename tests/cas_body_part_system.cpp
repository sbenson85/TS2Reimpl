// Differential tests for nTSCASCore::cTSCASBodyPartSystem's interface subobject (cITSCASBodyPartSystem, vftable 0xeba198,
// 89 slots; `this` is the subobject at +0x1c of the system). The simple stores and getters are checked here; the modifier
// stack functions are in cas_body_part_system_stack.cpp.
//
// Each trial fills one object with random bytes, runs the original on one copy and ours on the other, then compares the
// result, the object bytes and canaries around it.
#include "difftest.h"
#include "cas/cTSCASBodyPartSystem.h"
#include "game/game.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Iface = nTSCASCore::BodyPartSystemIface;
using nTSCASCore::BodyPartEntry;
using nTSCASCore::BodyPartEntryVector;

constexpr int kBody = 0x180; // the iface fields reach +0x12a

struct Side {
    uint8_t front[32];
    uint8_t body[kBody];
    uint8_t back[32];
};

struct Init {
    uint8_t body[kBody];
};

Init RandomInit(std::mt19937 &rng)
{
    Init init;
    for (auto &b : init.body) b = static_cast<uint8_t>(rng());
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

// A pointer result is compared as an offset from its side's object when it points into it, else as itself.
uint32_t Rel(const void *p, const Side &s)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    const uintptr_t lo = reinterpret_cast<uintptr_t>(s.body);
    if (v >= lo && v < lo + kBody) return static_cast<uint32_t>(v - lo);
    return static_cast<uint32_t>(v);
}

template <typename Run> void Compare(difftest::Context &ctx, const char *name, const Init &init, Run run)
{
    static Side a, b;
    Fill(a, init);
    Fill(b, init);
    const uint64_t ra = run(a, true);
    const uint64_t rb = run(b, false);
    ++ctx.cases;
    if (ra != rb) {
        ctx.Fail("%s: result original %llx, ours %llx", name, static_cast<unsigned long long>(ra),
                 static_cast<unsigned long long>(rb));
    }
    if (std::memcmp(a.body, b.body, sizeof(a.body)) != 0) ctx.Fail("%s: object state differs", name);
    if (!CanariesIntact(a) || !CanariesIntact(b)) ctx.Fail("%s: canaries overwritten", name);
}

Iface *Obj(Side &s)
{
    return reinterpret_cast<Iface *>(s.body);
}

// ---- Block stores (slots 54, 74, 56) -------------------------------------------------------------------------------

// The source is either a random buffer outside the object or 16 bytes of the object starting k bytes from the
// destination (overlapping it, which the original's load-then-store copes with).
template <typename Ours>
void CheckBlock(difftest::Context &ctx, const char *name, uint32_t rva, intptr_t offset, Ours ours, int trials)
{
    std::mt19937 rng(rva);
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(rva);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng);
        uint8_t external[16];
        for (auto &b : external) b = static_cast<uint8_t>(rng());
        const bool overlap = rng() % 2 == 0;
        const int k = static_cast<int>(rng() % 31) - 15;
        Compare(ctx, name, init, [&](Side &s, bool original) -> uint64_t {
            const uint8_t *src = overlap ? s.body + offset + k : external;
            if (original) orig(s.body, src);
            else (Obj(s)->*ours)(src);
            return 0;
        });
    }
}

// ---- Single-value stores (slots 60, 62, 67) ------------------------------------------------------------------------

template <typename V, typename Ours>
void CheckStore(difftest::Context &ctx, const char *name, uint32_t rva, Ours ours, int trials)
{
    std::mt19937 rng(rva);
    auto orig = ctx.Original<void(__thiscall *)(void *, V)>(rva);
    for (int i = 0; i < trials; ++i) {
        Init init = RandomInit(rng);
        const V value = static_cast<V>(rng());
        Compare(ctx, name, init, [&](Side &s, bool original) -> uint64_t {
            if (original) orig(s.body, value);
            else (Obj(s)->*ours)(value);
            return 0;
        });
    }
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_StoreBlockB8, 0xb81a30)
{
    CheckBlock(ctx, "StoreBlockB8", 0xb81a30, 0xb8, &Iface::StoreBlockB8, 3000);
}

DIFF_TEST(cTSCASBodyPartSystem_StoreBlockC8, 0xcf7f00)
{
    CheckBlock(ctx, "StoreBlockC8", 0xcf7f00, 0xc8, &Iface::StoreBlockC8, 3000);
}

DIFF_TEST(cTSCASBodyPartSystem_StoreBlockD8, 0xcf80c0)
{
    CheckBlock(ctx, "StoreBlockD8", 0xcf80c0, 0xd8, &Iface::StoreBlockD8, 3000);
}

DIFF_TEST(cTSCASBodyPartSystem_StoreDword108, 0xcf81c0)
{
    CheckStore<uint32_t>(ctx, "StoreDword108", 0xcf81c0, &Iface::StoreDword108, 3000);
}

DIFF_TEST(cTSCASBodyPartSystem_StoreByte10C, 0xcf7f20)
{
    CheckStore<uint8_t>(ctx, "StoreByte10C", 0xcf7f20, &Iface::StoreByte10C, 3000);
}

DIFF_TEST(cTSCASBodyPartSystem_StoreByte129, 0xcf7ef0)
{
    CheckStore<uint8_t>(ctx, "StoreByte129", 0xcf7ef0, &Iface::StoreByte129, 3000);
}

// Slot 70: the whole of eax is compared (the upper bits come from the argument).
DIFF_TEST(cTSCASBodyPartSystem_ByteGate120, 0xcf3720)
{
    std::mt19937 rng(0xcf3720);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, int32_t)>(0xcf3720);
    for (int i = 0; i < 4000; ++i) {
        Init init = RandomInit(rng);
        Put<uint8_t>(init, 0x120, rng() % 3 == 0 ? 0 : static_cast<uint8_t>(rng()));
        int32_t mode;
        switch (rng() % 5) {
        case 0: mode = 0; break;
        case 1: mode = 1; break;
        case 2: mode = -1; break;
        case 3: mode = static_cast<int32_t>(rng() % 512) - 256; break;
        default: mode = static_cast<int32_t>(rng()); break;
        }
        Compare(ctx, "ByteGate120", init, [&](Side &s, bool original) -> uint64_t {
            return original ? orig(s.body, mode) : Obj(s)->ByteGate120(mode);
        });
    }
}

// Slot 11: the string at +0x9c is inline up to capacity 15, a heap pointer above it.
DIFF_TEST(cTSCASBodyPartSystem_GetText, 0xceaae0)
{
    std::mt19937 rng(0xceaae0);
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(0xceaae0);
    static char external[64];
    for (int i = 0; i < 2000; ++i) {
        Init init = RandomInit(rng);
        const uint32_t cap = rng() % 4 == 0 ? 0xf : (rng() % 2 ? static_cast<uint32_t>(rng() % 0x20) : 0xffffffffu);
        Put<uint32_t>(init, 0xb0, cap);
        Put<const char *>(init, 0x9c, external);
        Compare(ctx, "GetText", init, [&](Side &s, bool original) -> uint64_t {
            const char *p = original ? orig(s.body) : Obj(s)->GetText();
            return Rel(p, s);
        });
    }
}

namespace {

// Stack header triple with random pointer values (only subtracted by the counters).
template <typename Ours>
void CheckCount(difftest::Context &ctx, const char *name, uint32_t rva, intptr_t fieldOffset, Ours ours)
{
    std::mt19937 rng(rva);
    auto orig = ctx.Original<int32_t(__thiscall *)(void *)>(rva);
    static BodyPartEntryVector hdr;
    for (int i = 0; i < 3000; ++i) {
        Init init = RandomInit(rng);
        const uintptr_t first = (rng() & 0x7fffffc0u) | 0x10000u;
        int64_t n;
        switch (rng() % 4) {
        case 0: n = 0; break;
        case 1: n = rng() % 8; break;
        case 2: n = -static_cast<int64_t>(rng() % 8); break;
        default: n = static_cast<int64_t>(rng() % 0x40000) - 0x20000; break;
        }
        hdr.mFirst = reinterpret_cast<BodyPartEntry *>(first);
        hdr.mLast = reinterpret_cast<BodyPartEntry *>(first + static_cast<uintptr_t>(n * 0x40));
        hdr.mEnd = hdr.mLast;
        Put<BodyPartEntryVector *>(init, fieldOffset, &hdr);
        Compare(ctx, name, init, [&](Side &s, bool original) -> uint64_t {
            return static_cast<uint32_t>(original ? orig(s.body) : (Obj(s)->*ours)());
        });
    }
}

} // namespace

DIFF_TEST(cTSCASBodyPartSystem_StackCount, 0xcfab60)
{
    CheckCount(ctx, "StackCount", 0xcfab60, 0x1c, &Iface::StackCount);
}

DIFF_TEST(cTSCASBodyPartSystem_RedoStackCount, 0xcf6810)
{
    CheckCount(ctx, "RedoStackCount", 0xcf6810, 0x18, &Iface::RedoStackCount);
}

// Slot 59: indexes 0 and 1 give the address of a key in the object, anything else the shared zero GUID.
DIFF_TEST(cTSCASBodyPartSystem_GetKey, 0xcf2160)
{
    std::mt19937 rng(0xcf2160);
    auto orig = ctx.Original<const void *(__thiscall *)(void *, uint32_t)>(0xcf2160);
    for (int i = 0; i < 2000; ++i) {
        Init init = RandomInit(rng);
        const uint32_t index = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
        Compare(ctx, "GetKey", init, [&](Side &s, bool original) -> uint64_t {
            return Rel(original ? orig(s.body, index) : Obj(s)->GetKey(index), s);
        });
    }
}

// Slot 72: 0x267350 applied to the selector at +0x118.
DIFF_TEST(cTSCASBodyPartSystem_AgeMask, 0xcf3210)
{
    std::mt19937 rng(0xcf3210);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xcf3210);
    for (int i = 0; i < 1000; ++i) {
        Init init = RandomInit(rng);
        Put<uint32_t>(init, 0x118, rng() % 2 ? rng() % 6 : static_cast<uint32_t>(rng()));
        Compare(ctx, "AgeMask", init, [&](Side &s, bool original) -> uint64_t {
            return original ? orig(s.body) : Obj(s)->AgeMask();
        });
    }
}
