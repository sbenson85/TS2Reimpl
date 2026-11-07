// Differential tests for the cTSPersistCacheStrategyGenerational functions reimplemented so far (field
// map in src/ts/cTSPersistCacheStrategyGenerational.h). Each run works on its own copy of a fake object
// with the same bytes. Both sides use the original primary vtable, copied, with slots 18, 22 and 23
// replaced by recording fakes: the object's own slot calls are then observed (and not run through
// unreconstructed code). Logged calls carry offsets from the object. Compared: the return value (only al
// for the byte results, as callers read it), the logged calls and arguments, and the object's bytes.
#include "difftest.h"
#include "ts/cTSPersistCacheStrategyGenerational.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Ret32 = uint32_t(__thiscall *)(void *);
using Ret8 = uint8_t(__thiscall *)(void *);
using Ret8Arg = uint8_t(__thiscall *)(void *, uint32_t);
using Void = void(__thiscall *)(void *);
using Ret32Arg3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

using cTS = cTSPersistCacheStrategyGenerational;

constexpr size_t kObjBytes = 0x100;
constexpr size_t kSlots = 24;

void *gPrimaryVt[kSlots];
void *gSubVt[3];

// The fake object at +0x4c (its slot 3 records its argument and returns gSlot3Ret).
uint8_t gPObj[8];
void *gPVt[4];

// Logged calls, and the offsets of their receivers from the object under test.
uint8_t *gBase = nullptr;
std::vector<uint32_t> gLog;
std::vector<uint32_t> gScript; // results of slot 23 calls, in order (0 once exhausted)
size_t gScriptNext = 0;
uint32_t gSlot18Ret = 0;
uint32_t gSlot3Ret = 0;
int gDeletes = 0;
uint32_t gDeleteFlags = 0;

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

uint32_t OffsetOf(const void *self)
{
    return Addr(self) - Addr(gBase);
}

uint32_t NextScript()
{
    uint32_t v = gScriptNext < gScript.size() ? gScript[gScriptNext] : 0;
    ++gScriptNext;
    return v;
}

// Fakes. __fastcall with the stack arguments of each slot, so each pops them as the game's callees do.
uint32_t __fastcall Slot3(void *self, void *, uint32_t a)
{
    gLog.push_back(3);
    gLog.push_back(self == gPObj ? 0u : 1u);
    gLog.push_back(a);
    return gSlot3Ret;
}

uint32_t __fastcall Slot18(void *self, void *)
{
    gLog.push_back(18);
    gLog.push_back(OffsetOf(self));
    return gSlot18Ret;
}

void __fastcall Slot22(void *self, void *, uint32_t a)
{
    gLog.push_back(22);
    gLog.push_back(OffsetOf(self));
    gLog.push_back(a);
}

uint32_t __fastcall Slot23(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    gLog.push_back(23);
    gLog.push_back(OffsetOf(self));
    gLog.push_back(a);
    gLog.push_back(b);
    gLog.push_back(c);
    return NextScript();
}

// Deleting destructor slot of the subobject: records the flags (and does not free; the test does).
void __fastcall RecordDelete(void * /*self*/, void *, uint32_t flags)
{
    ++gDeletes;
    gDeleteFlags = flags;
}

void SetupVTables(const difftest::Context &ctx)
{
    std::memcpy(gPrimaryVt, ctx.image.At(cTS::kVTable), sizeof(gPrimaryVt));
    gPrimaryVt[18] = reinterpret_cast<void *>(&Slot18);
    gPrimaryVt[22] = reinterpret_cast<void *>(&Slot22);
    gPrimaryVt[23] = reinterpret_cast<void *>(&Slot23);
    std::memcpy(gSubVt, ctx.image.At(cTS::kSubVTable), sizeof(gSubVt));
    gSubVt[0] = reinterpret_cast<void *>(&RecordDelete);
    for (auto &s : gPVt) s = nullptr;
    gPVt[3] = reinterpret_cast<void *>(&Slot3);
    // The fake object at +0x4c: its first dword is its vtable pointer.
    *reinterpret_cast<void **>(gPObj) = gPVt;
}

template <typename T> void Put(uint8_t *base, uintptr_t offset, T value)
{
    std::memcpy(base + offset, &value, sizeof(value));
}

void FillRandom(uint8_t *p, size_t n, std::mt19937 &rng)
{
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(rng());
}

// A 32-bit value that is zero in the low byte a third of the time (so al is zero though eax is not).
uint32_t ByteOrWord(std::mt19937 &rng)
{
    uint32_t v = rng();
    return rng() % 3 == 0 ? (v & ~0xffu) : v;
}

struct Result {
    uint32_t ret = 0;
    std::vector<uint32_t> log;
    std::vector<uint8_t> bytes;
};

template <typename Call> Result Run(uint8_t *obj, Call call)
{
    gBase = obj;
    gLog.clear();
    gScriptNext = 0;
    Result r;
    r.ret = call(obj);
    r.log = gLog;
    r.bytes.assign(obj, obj + kObjBytes);
    return r;
}

// Fills one object (`fill` also sets the script and fake results for this case), runs both sides on
// identical copies and compares them.
template <typename Fill, typename OrigCall, typename OursCall>
void Check(difftest::Context &ctx, const char *name, uint32_t seed, Fill fill, OrigCall origCall, OursCall oursCall)
{
    auto *a = new uint8_t[kObjBytes];
    auto *b = new uint8_t[kObjBytes];
    fill(a);
    std::memcpy(b, a, kObjBytes);
    Result ra = Run(a, origCall);
    Result rb = Run(b, oursCall);
    ++ctx.cases;
    if (ra.ret != rb.ret) ctx.Fail("%s seed %u: original returned %u, ours %u", name, seed, ra.ret, rb.ret);
    if (ra.log != rb.log) ctx.Fail("%s seed %u: logged calls differ (original %zu words, ours %zu)", name, seed, ra.log.size(), rb.log.size());
    for (size_t i = 0; i < kObjBytes; ++i) {
        if (ra.bytes[i] != rb.bytes[i]) {
            ctx.Fail("%s seed %u: object byte +0x%zx differs (original %u, ours %u)", name, seed, i, ra.bytes[i], rb.bytes[i]);
            break;
        }
    }
    delete[] a;
    delete[] b;
}

// Optional random table used by vf22: +4 and four limits at +8..+0x14.
uint32_t gTable[6];

} // namespace

DIFF_TEST(TSPersistCacheStrategyGenerational_Release, 0x1fc090)
{
    SetupVTables(ctx);
    auto orig = ctx.Original<Ret32>(0x1fc090);
    std::mt19937 rng(2);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        // Object block: primary vptr at 0, subobject (cRZRefCountMT) at +4 with its vptr and count at +8.
        // The subobject's deleting-destructor slot records the call; the test frees the blocks.
        uint32_t refs = rng() % 6;
        auto *a = new uint8_t[16];
        auto *b = new uint8_t[16];
        for (uint8_t *p : {a, b}) {
            std::memset(p, 0, 16);
            Put(p, 0, Addr(gPrimaryVt));
            Put(p, 4, Addr(gSubVt));
            Put(p, 8, refs);
        }
        gDeletes = 0;
        gDeleteFlags = 0;
        uint32_t ra = orig(a);
        int deletedA = gDeletes;
        uint32_t flagsA = gDeleteFlags;
        gDeletes = 0;
        gDeleteFlags = 0;
        uint32_t rb = reinterpret_cast<cTS *>(b)->Release();
        int deletedB = gDeletes;
        uint32_t flagsB = gDeleteFlags;
        ++ctx.cases;
        if (ra != rb || deletedA != deletedB || flagsA != flagsB) {
            ctx.Fail("Release refs %u: original %u (deleted %d flags %u), ours %u (deleted %d flags %u)", refs, ra, deletedA, flagsA, rb,
                     deletedB, flagsB);
        }
        if (!deletedA && !deletedB && std::memcmp(a + 8, b + 8, 4) != 0) ctx.Fail("Release refs %u: counts differ", refs);
        delete[] a;
        delete[] b;
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_vf4, 0x1fc140)
{
    SetupVTables(ctx);
    auto orig = ctx.Original<Ret32>(0x1fc140);
    std::mt19937 rng(4);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        gSlot18Ret = ByteOrWord(rng);
        gScript = {ByteOrWord(rng)};
        Check(
            ctx, "vf4", seed,
            [&](uint8_t *o) {
                FillRandom(o, kObjBytes, rng);
                Put(o, 0, Addr(gPrimaryVt));
                Put(o, 4, Addr(gSubVt));
            },
            [&](uint8_t *o) { return orig(o); }, [&](uint8_t *o) { return reinterpret_cast<cTS *>(o)->vf4(); });
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_vf11, 0x1fb790)
{
    SetupVTables(ctx);
    auto orig = ctx.Original<Ret8Arg>(0x1fb790);
    std::mt19937 rng(11);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        uint32_t a = rng();
        bool nullObj = rng() % 4 == 0;
        gSlot3Ret = ByteOrWord(rng);
        Check(
            ctx, "vf11", seed,
            [&](uint8_t *o) {
                FillRandom(o, kObjBytes, rng);
                Put(o, 0, Addr(gPrimaryVt));
                Put(o, 4, Addr(gSubVt));
                Put(o, 0x4c, nullObj ? 0u : Addr(gPObj));
            },
            [&](uint8_t *o) { return static_cast<uint32_t>(orig(o, a)) & 0xffu; },
            [&](uint8_t *o) { return static_cast<uint32_t>(reinterpret_cast<cTS *>(o)->vf11(a)); });
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_vf14, 0x1fc040)
{
    SetupVTables(ctx);
    auto orig = ctx.Original<Ret8Arg>(0x1fc040);
    std::mt19937 rng(14);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        uint32_t a = rng();
        // The flag at +0x70 is zero about half the time.
        uint8_t flag = rng() % 2 ? static_cast<uint8_t>(rng() | 1) : 0;
        Check(
            ctx, "vf14", seed,
            [&](uint8_t *o) {
                FillRandom(o, kObjBytes, rng);
                Put(o, 0, Addr(gPrimaryVt));
                Put(o, 4, Addr(gSubVt));
                Put(o, 0x70, flag);
            },
            [&](uint8_t *o) { return static_cast<uint32_t>(orig(o, a)) & 0xffu; },
            [&](uint8_t *o) { return static_cast<uint32_t>(reinterpret_cast<cTS *>(o)->vf14(a)); });
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_vf17, 0x1fb830)
{
    SetupVTables(ctx);
    auto orig = ctx.Original<Ret8>(0x1fb830);
    std::mt19937 rng(17);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        // A script of up to ten slot 23 results, zero about a quarter of the time. More than eight
        // results means the loop must stop at eight calls.
        gScript.clear();
        size_t n = rng() % 11;
        for (size_t i = 0; i < n; ++i) gScript.push_back(rng() % 4 == 0 ? 0u : ByteOrWord(rng));
        Check(
            ctx, "vf17", seed,
            [&](uint8_t *o) {
                FillRandom(o, kObjBytes, rng);
                Put(o, 0, Addr(gPrimaryVt));
                Put(o, 4, Addr(gSubVt));
            },
            [&](uint8_t *o) { return static_cast<uint32_t>(orig(o)) & 0xffu; },
            [&](uint8_t *o) { return static_cast<uint32_t>(reinterpret_cast<cTS *>(o)->vf17()); });
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_vf21, 0x1fc0a0)
{
    SetupVTables(ctx);
    auto origVf21 = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x1fc0a0);
    std::mt19937 rng(21);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        // 0, 1 and 2 select the three static tables; other values change nothing.
        uint32_t which = rng() % 4 == 3 ? rng() : rng() % 3;
        Check(
            ctx, "vf21", seed,
            [&](uint8_t *o) {
                FillRandom(o, kObjBytes, rng);
                Put(o, 0, Addr(gPrimaryVt));
                Put(o, 4, Addr(gSubVt));
            },
            [&](uint8_t *o) {
                origVf21(o, which);
                return 0u;
            },
            [&](uint8_t *o) {
                reinterpret_cast<cTS *>(o)->vf21(which);
                return 0u;
            });
    }
}

DIFF_TEST(TSPersistCacheStrategyGenerational_vf22, 0x1fbd90)
{
    SetupVTables(ctx);
    auto origVf22 = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1fbd90);
    std::mt19937 rng(22);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        uint32_t force = ByteOrWord(rng);
        gScript = {rng()};
        // Table: +4 is the third argument of slot 23; limits (+8..+0x14) are mostly 0..3, sometimes huge.
        gTable[1] = rng();
        for (size_t i = 2; i < 6; ++i) gTable[i] = rng() % 8 == 0 ? 0xffffffffu : rng() % 4;
        Check(
            ctx, "vf22", seed,
            [&](uint8_t *o) {
                FillRandom(o, kObjBytes, rng);
                Put(o, 0, Addr(gPrimaryVt));
                Put(o, 4, Addr(gSubVt));
                Put(o, 0x5c, Addr(gTable));
                for (uintptr_t i = 0; i < 4; ++i) Put(o, 0x60 + 4 * i, rng() % 4 == 0 ? rng() : rng() % 5);
            },
            [&](uint8_t *o) { return origVf22(o, force); },
            [&](uint8_t *o) { return reinterpret_cast<cTS *>(o)->vf22(force); });
    }
}
