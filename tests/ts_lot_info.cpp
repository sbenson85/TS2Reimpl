// Differential tests for cTSLotInfo (src/ts/cTSLotInfo.*).
//
// The world: a lot info built by the original default constructor on each side, whose base (+0x80) is swapped for a recording
// fake for the slot tests (the real cTSBaseLotInfo stays for the constructor / destructor tests), the app singleton (0x10cc288)
// pointed at a fake whose slot 6 object answers slot 57 with a "deferred" lot info fake, and a recording stream. Every fake
// call is logged with its object id, slot and arguments (pointers to fakes become ids, pointers into the lot info offsets,
// other pointers a marker); a run compares the return value, the log and the lot info's state.
//
// Standalone shape (tests/ts_lot_info_shape.h): each test runs the vtable route (our side built by our constructor, called
// through its own vtable) and the adapter route (our side built by the original constructor, the RVA hook's adapter called with
// its slot trapped).
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "heap_poison.h"
#include "rz/RZMemory.h"
#include "ts/cTSLotInfo.h"
#include "ts_lot_info_shape.h"

#include <cstring>
#include <limits>
#include <random>
#include <utility>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;
constexpr unsigned kSlots = 64;
constexpr uint32_t kAppGlobal = 0x10cc288;
constexpr uint32_t kDefaultConstruct = 0x4c8a40;
constexpr uint32_t kCopyConstruct = 0x4c8800;
constexpr uint32_t kDestruct = 0x4c8c30;

Blob gEv;
cTSLotInfo *gLot = nullptr;
uintptr_t gStackLo = 0, gStackHi = 0;
uintptr_t gMessage = 0; // the last block TestMalloc handed out (SetMappedValue's message)

struct Fake;
using Custom = uint32_t (*)(Fake *self, uint32_t *args);
struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
    std::mt19937 rng;
    uint32_t mode;
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
    uint32_t Call(unsigned slot, unsigned n, uint32_t *args);
    void Reset(uint32_t newId, uint32_t seed);
    void Set(unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr);
};

enum FakeId { kBase, kRecord, kApp, kLots, kDeferred, kStream, kTarget, kPersist, kResource, kAlloc, kTerrain, kText, kFakeCount };
Fake gFakes[kFakeCount];

uint32_t Norm(uint32_t v)
{
    const uintptr_t p = v;
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (p == reinterpret_cast<uintptr_t>(&gFakes[i])) return 0xf0000000u | i;
    if (gLot && p >= reinterpret_cast<uintptr_t>(gLot) && p < reinterpret_cast<uintptr_t>(gLot) + sizeof(cTSLotInfo))
        return 0xe0000000u | uint32_t(p - reinterpret_cast<uintptr_t>(gLot));
    if (p >= gStackLo && p < gStackHi) return 0xcccc0000u;
    if (gMessage && p >= gMessage && p < gMessage + 0x2c) return 0xb0000000u | uint32_t(p - gMessage); // SetMappedValue's message
    return v;
}

uint32_t Fake::Call(unsigned slot, unsigned n, uint32_t *args)
{
    gEv.push_back(0x1000u + id * 0x100u + slot);
    gEv.push_back(n);
    for (unsigned i = 0; i < n; ++i) gEv.push_back(Norm(args[i]));
    if (custom[slot]) return custom[slot](this, args);
    return ret[slot];
}

template <unsigned S> uint32_t __fastcall T0(Fake *f, void *) { return f->Call(S, 0, nullptr); }
template <unsigned S> uint32_t __fastcall T1(Fake *f, void *, uint32_t a)
{
    uint32_t v[] = {a};
    return f->Call(S, 1, v);
}
template <unsigned S> uint32_t __fastcall T2(Fake *f, void *, uint32_t a, uint32_t b)
{
    uint32_t v[] = {a, b};
    return f->Call(S, 2, v);
}
template <unsigned S> uint32_t __fastcall T3(Fake *f, void *, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t v[] = {a, b, c};
    return f->Call(S, 3, v);
}
template <unsigned S> uint32_t __fastcall T4(Fake *f, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t v[] = {a, b, c, d};
    return f->Call(S, 4, v);
}
template <unsigned S> uint32_t __fastcall T5(Fake *f, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t v[] = {a, b, c, d, e};
    return f->Call(S, 5, v);
}
template <size_t... I> void FillThunks(void **t, std::index_sequence<I...>)
{
    void *a0[] = {reinterpret_cast<void *>(&T0<I>)...};
    void *a1[] = {reinterpret_cast<void *>(&T1<I>)...};
    void *a2[] = {reinterpret_cast<void *>(&T2<I>)...};
    void *a3[] = {reinterpret_cast<void *>(&T3<I>)...};
    void *a4[] = {reinterpret_cast<void *>(&T4<I>)...};
    void *a5[] = {reinterpret_cast<void *>(&T5<I>)...};
    for (size_t i = 0; i < sizeof...(I); ++i) {
        t[i] = a0[i];
        t[kSlots + i] = a1[i];
        t[2 * kSlots + i] = a2[i];
        t[3 * kSlots + i] = a3[i];
        t[4 * kSlots + i] = a4[i];
        t[5 * kSlots + i] = a5[i];
    }
}
void **Thunks()
{
    static void *t[6 * kSlots];
    static bool done = false;
    if (!done) {
        FillThunks(t, std::make_index_sequence<kSlots>());
        done = true;
    }
    return t;
}

uint32_t __fastcall AddRefFake(Fake *f, void *) { return uint32_t(++f->refs); }
uint32_t __fastcall ReleaseFake(Fake *f, void *)
{
    gEv.push_back(0x2000u + f->id);
    return uint32_t(--f->refs);
}

void Fake::Reset(uint32_t newId, uint32_t seed)
{
    vt = table;
    id = newId;
    refs = 1000;
    rng.seed(seed * 2654435761u + newId);
    mode = 0;
    for (unsigned i = 0; i < kSlots; ++i) {
        ret[i] = 0;
        custom[i] = nullptr;
        table[i] = nullptr;
    }
    table[1] = reinterpret_cast<void *>(&AddRefFake);
    table[2] = reinterpret_cast<void *>(&ReleaseFake);
}

void Fake::Set(unsigned slot, unsigned args, uint32_t result, Custom fn)
{
    table[slot] = Thunks()[args * kSlots + slot];
    ret[slot] = result;
    custom[slot] = fn;
}

uint32_t P(const void *p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }

// ---- the base fake ----
uint32_t BaseGetSize(Fake *f, uint32_t *a)
{
    *reinterpret_cast<int32_t *>(a[0]) = int32_t(f->rng() % 200) - 20;
    *reinterpret_cast<int32_t *>(a[1]) = int32_t(f->rng() % 200) - 20;
    return f->rng();
}
uint32_t BaseHasEdge(Fake *f, uint32_t *a) { return (f->mode >> (a[0] & 31)) & 1 ? (f->rng() | 1) : (f->rng() & ~0xffu); }
uint32_t RandomWord(Fake *f, uint32_t *) { return f->rng(); }

// ---- the stream fake: mode 0 reads (fills the out-pointer), mode 1 writes (records) ----
template <unsigned Bytes> uint32_t StreamGet(Fake *f, uint32_t *a)
{
    if (f->mode) return 1;
    if (Bytes == 2 && f->rng() % 4 == 0) return 0; // a failed 16-bit read leaves the word alone
    uint32_t v = f->rng();
    if (Bytes == 4) { const uint32_t k = f->rng() % 4; v = k < 2 ? v % 5 : k == 2 ? (v | 0x80000000u) : v % 300; } // counts stay small
    gEv.push_back(v);
    std::memcpy(reinterpret_cast<void *>(a[0]), &v, Bytes);
    return f->rng() | 1;
}
uint32_t gVersion;
uint32_t StreamGetVersion(Fake *f, uint32_t *a)
{
    if (f->mode) return 1;
    *reinterpret_cast<uint16_t *>(a[0]) = uint16_t(gVersion);
    return 1;
}
uint32_t StreamString(Fake *f, uint32_t *a)
{
    auto *text = reinterpret_cast<cRZString *>(a[0]);
    if (f->mode) {
        gEv.push_back(uint32_t(text->mString.size()));
        for (char c : text->mString) gEv.push_back(uint8_t(c));
        return 1;
    }
    const unsigned n = f->rng() % 3 == 0 ? 20 + f->rng() % 20 : f->rng() % 10;
    text->mString.assign(n, char('a' + f->rng() % 26));
    return 1;
}

void SetupStream(uint32_t version, bool writing)
{
    Fake &s = gFakes[kStream];
    gVersion = version;
    s.mode = writing;
    for (unsigned slot = 4; slot <= 9; ++slot) s.Set(slot, 1, 1);
    s.Set(4, 1, 1, &StreamGet<1>);
    s.Set(5, 1, 1, &StreamGet<1>);
    s.Set(6, 1, 1, &StreamGet<2>);
    s.Set(7, 1, 1, &StreamGetVersion);
    s.Set(8, 1, 1, &StreamGet<4>);
    s.Set(9, 1, 1, &StreamGet<4>);
    s.Set(12, 1, 1, &StreamGet<4>);
    s.Set(15, 1, 1, &StreamString);
}

// Resets every fake for a seed and points the app global at the app fake.
void ResetFakes(uint32_t seed, std::mt19937 &rng)
{
    for (unsigned i = 0; i < kFakeCount; ++i) gFakes[i].Reset(i, seed);
    Fake &b = gFakes[kBase];
    b.mode = rng();
    b.Set(5, 2, 0, &BaseGetSize);
    b.Set(8, 0, rng() % 3 == 0 ? 9 : rng() % 3 == 0 ? rng() % 12 : 8);
    b.Set(10, 0, 0, &RandomWord);
    b.Set(14, 1, 0, &BaseHasEdge);
    b.Set(17, 1, 0);
    b.Set(19, 0, rng());
    b.Set(22, 1, 0);
    b.Set(31, 0, 0, &RandomWord);
    b.Set(32, 1, 0);
    b.Set(33, 0, rng());
    b.Set(34, 1, 0);
    b.Set(35, 0, rng());
    b.Set(36, 1, 0);
    b.Set(37, 0, P(&gFakes[kRecord]));
    Fake &r = gFakes[kRecord];
    r.Set(3, 1, rng());
    r.Set(4, 1, rng() % 4 == 0 ? 0 : (rng() | 1) & 0xff);
    gFakes[kApp].Set(6, 0, rng() % 5 == 0 ? 0 : P(&gFakes[kLots]));
    gFakes[kLots].Set(57, 1, rng() % 5 == 0 ? 0 : P(&gFakes[kDeferred]));
    Fake &d = gFakes[kDeferred];
    d.Set(19, 0, rng());
    d.Set(38, 2, rng());
    d.Set(39, 1, rng());
    d.Set(40, 1, rng());
    d.Set(42, 0, rng());
    for (unsigned slot = 43; slot <= 46; ++slot) d.Set(slot, 1, rng());
    d.Set(53, 2, rng());
    d.Set(54, 2, rng());
    gFakes[kTarget].Set(16, 1, 0);
}

struct AppGlobal {
    explicit AppGlobal(void *value) : slot(GameAddress<void *>(kAppGlobal)), saved(*slot) { *slot = value; }
    ~AppGlobal() { *slot = saved; }
    void **slot;
    void *saved;
};

// The option mask cache (0x10faf94) that cTSBaseLotInfo's slot 8 reads (through HasOptionBits 0x400), which the base copy
// constructor calls: preset so the query never reaches the app.
struct OptionMask {
    explicit OptionMask(uint32_t value) : slot(GameAddress<uint32_t>(0x10faf94)), saved(*slot) { *slot = value | 0x1000; }
    ~OptionMask() { *slot = saved; }
    uint32_t *slot;
    uint32_t saved;
};

// A lot info built by the original default constructor (or ours: `ours`) with randomised members; the base is the fake unless
// `realBase`.
cTSLotInfo *BuildLot(difftest::Context &ctx, uint32_t seed, bool realBase, bool ours)
{
    std::mt19937 rng(seed * 7919u + 11);
    auto *mem = static_cast<uint8_t *>(::operator new(sizeof(cTSLotInfo)));
    for (size_t i = 0; i < sizeof(cTSLotInfo); ++i) mem[i] = uint8_t(rng());
    auto *lot = reinterpret_cast<cTSLotInfo *>(mem);
    if (ours) {
        cTSLotInfoAdapters::Construct(lot, nullptr);
        if (!lotshape::FacesWhereExpected(lot)) ctx.Fail("our faces are not at the original's offsets");
    } else
        ctx.Original<void *(__thiscall *)(void *)>(kDefaultConstruct)(lot);
    lot->mOriginX = int32_t(rng() % 300);
    lot->mOriginY = int32_t(rng() % 300);
    float f = float(int32_t(rng() % 2000) - 1000) / 8.0f;
    lot->mElevation = rng() % 3 == 0 ? -3.4028234663852886e38f : f;
    lot->mField44 = int32_t(rng() % 1000) - 500;
    lot->mRotation = uint8_t(rng() % 3 == 0 ? rng() : rng() % 4);
    lot->mField4C = int32_t(rng() % 1000);
    lot->mLotId = int32_t(rng() % 50);
    lot->mByteA0 = uint8_t(rng() % 2);
    lot->mFieldA4 = int32_t(rng() % 1000);
    lot->mFieldA8 = int32_t(rng() % 1000);
    lot->mName.mString.assign(rng() % 25, char('A' + rng() % 26));
    lot->mDescription.mString.assign(rng() % 25, char('a' + rng() % 26));
    for (auto *k = &lot->mKey.type; k <= &lot->mKey.resource; ++k) *k = rng();
    const unsigned connections = rng() % 6;
    for (unsigned i = 0; i < connections; ++i)
        lot->mConnections.push_back(
            {int32_t(rng() % 6), int32_t(rng() % 9) - 3, int32_t(rng() % 100), int32_t(rng() % 100)});
    if (rng() % 2) lot->mConnections.reserve(lot->mConnections.size() + rng() % 3);
    const unsigned values = rng() % 6;
    for (unsigned i = 0; i < values; ++i) lot->mValues[int16_t(rng() % 7 - 2)] = int16_t(int32_t(rng() % 600) - 300);
    if (!realBase) lot->mBase = &gFakes[kBase]; // the real base is left alive (not released) to keep its frees out of logs
    return lot;
}

void CaptureString(Blob &b, const cRZString &s)
{
    b.push_back(Norm(P(s.mVTable)));
    b.push_back(uint32_t(s.mString.size()));
    b.push_back(uint32_t(s.mString.capacity()));
    for (char c : s.mString) b.push_back(uint8_t(c));
    b.push_back(s.mRefCount);
}

void Capture(Blob &b, cTSLotInfo *lot)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(lot);
    for (unsigned off : {0x00u, 0x08u, 0x0cu, 0x10u, 0x14u}) b.push_back(lotshape::VtCode(w[off / 4]));
    // Data words as they are; only +0x84 (a counted pointer) through Norm. A data word through Norm (the key's full-range
    // randoms) that lay inside one side's lot was rewritten on that side only (seed 2147483891 word 27, orig 1316a9b7 ours
    // e0000017, 2026-11-08: docs/lane/test.md, "Normalised words").
    for (unsigned off : {0x04u, 0x18u, 0x1cu, 0x3cu, 0x40u, 0x44u, 0x48u, 0x4cu, 0x6cu, 0x70u, 0x74u, 0x78u, 0x7cu, 0x84u, 0x94u,
                         0xa0u, 0xa4u, 0xa8u})
        b.push_back(off == 0x84u ? Norm(w[off / 4]) : w[off / 4]);
    b.push_back(lot->mBase == &gFakes[kBase] ? 1u : lot->mBase ? 2u : 0u);
    CaptureString(b, lot->mName);
    CaptureString(b, lot->mDescription);
    b.push_back(uint32_t(lot->mConnections.size()));
    b.push_back(uint32_t(lot->mConnections.capacity()));
    for (const auto &c : lot->mConnections) b.insert(b.end(), {uint32_t(c.mField0), uint32_t(c.mField1), uint32_t(c.mField2), uint32_t(c.mField3)});
    b.push_back(uint32_t(lot->mValues.size()));
    for (const auto &e : lot->mValues) b.insert(b.end(), {uint32_t(e.first), uint32_t(uint16_t(e.second))});
    for (const auto &f : gFakes) b.push_back(uint32_t(f.refs));
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return true;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differs at word %u of %u/%u (orig %08x ours %08x)", what, seed, unsigned(i), unsigned(a.size()),
             unsigned(b.size()), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
    return false;
}

void MarkStack()
{
    uint8_t here;
    const uintptr_t p = reinterpret_cast<uintptr_t>(&here);
    gStackLo = p - 0x20000;
    gStackHi = p + 0x20000;
}

// Runs `run(lot, original)` on a fresh world per side and compares, once per route (tests/ts_lot_info_shape.h).
template <class Run>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, Run run, bool realBase = false, int routes = 2)
{
    lotshape::Ours();
    for (int route = 0; route < routes; ++route) {
        lotshape::gRoute = lotshape::Route(route);
        MarkStack();
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 31u + 7);
            ResetFakes(seed, rng);
            gLot = BuildLot(ctx, seed, realBase, side == 1 && route == lotshape::kVTableRoute);
            AppGlobal app(&gFakes[kApp]);
            gEv.clear();
            lotshape::gTrapHits = 0;
            const uint32_t r = run(gLot, side == 0, rng);
            Blob &b = blobs[side];
            b.push_back(Norm(r));
            b.insert(b.end(), gEv.begin(), gEv.end());
            Capture(b, gLot);
            b.push_back(uint32_t(lotshape::gTrapHits));
            gLot = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], what, seed + (route ? 0x80000000u : 0u)); // adapter-route seeds have the top bit
    }
}

// The function at `rva` on face `face` (0 = +0, 1 = +8, 2 = +0xc, 3 = +0x10, 4 = +0x14), called the side's and route's way.
#define LOT_CALL(Fn, face, rva, adapter, ...)                                                                                  \
    lotshape::Call<Fn>(ctx, orig, lot, face, rva, LOTINFO_ADAPTER_ADDRESS(adapter), ##__VA_ARGS__)

} // namespace

// ---- the cITSLotInfo face slots ----

#define FACE_TEST0(Name, Rva, Ret, Method)                                                                                     \
    DIFF_TEST(LotInfo_##Name, Rva)                                                                                           \
    {                                                                                                                         \
        for (uint32_t seed = 0; seed < 200; ++seed)                                                                           \
            Pair(ctx, #Name, seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &) -> uint32_t {                              \
                return uint32_t(LOT_CALL(Ret(__thiscall *)(void *), 1, Rva, Method));                                         \
            });                                                                                                               \
    }

#define FACE_TEST1(Name, Rva, Ret, Arg, Method, Gen)                                                                           \
    DIFF_TEST(LotInfo_##Name, Rva)                                                                                           \
    {                                                                                                                         \
        for (uint32_t seed = 0; seed < 300; ++seed)                                                                           \
            Pair(ctx, #Name, seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {                           \
                const Arg a = Arg(Gen);                                                                                       \
                return uint32_t(LOT_CALL(Ret(__thiscall *)(void *, Arg), 1, Rva, Method, a));                                 \
            });                                                                                                               \
    }

#define FACE_TEST1V(Name, Rva, Arg, Method, Gen)                                                                               \
    DIFF_TEST(LotInfo_##Name, Rva)                                                                                           \
    {                                                                                                                         \
        for (uint32_t seed = 0; seed < 200; ++seed)                                                                           \
            Pair(ctx, #Name, seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {                           \
                const Arg a = Arg(Gen);                                                                                       \
                LOT_CALL(void(__thiscall *)(void *, Arg), 1, Rva, Method, a);                                                 \
                return 0;                                                                                                     \
            });                                                                                                               \
    }

#define FACE_TEST2(Name, Rva, Arg, Method, Gen)                                                                                \
    DIFF_TEST(LotInfo_##Name, Rva)                                                                                           \
    {                                                                                                                         \
        for (uint32_t seed = 0; seed < 300; ++seed)                                                                           \
            Pair(ctx, #Name, seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {                           \
                const Arg a = Arg(Gen), b = Arg(Gen);                                                                         \
                return uint32_t(LOT_CALL(uint8_t(__thiscall *)(void *, Arg, Arg), 1, Rva, Method, a, b));                     \
            });                                                                                                               \
    }

FACE_TEST0(GetBaseName, 0x4c9f50, void *, GetBaseName)
FACE_TEST0(GetEffectiveBaseName, 0x4c9f60, void *, GetEffectiveBaseName)
FACE_TEST0(GetRotation, 0x4c9110, uint8_t, GetRotation)
FACE_TEST0(GetRelativeRotation, 0x4cafc0, uint8_t, GetRelativeRotation)
FACE_TEST0(GetLotType, 0x4c9c50, uint32_t, GetLotType)
FACE_TEST0(GetConnectionCount, 0x4c96b0, int32_t, GetConnectionCount)
FACE_TEST0(GetCappedBaseByte, 0x4c9610, uint8_t, GetCappedBaseByte)
FACE_TEST0(GetBaseByte88, 0x4c9650, uint8_t, GetBaseByte88)
FACE_TEST0(GetBaseWord90, 0x4c9660, uint32_t, GetBaseWord90)
FACE_TEST0(GetDeferredLotInfo, 0x4c9200, void *, GetDeferredLotInfo)

FACE_TEST1V(SetElevation, 0x4cb5b0, float, SetElevation, float(int32_t(rng() % 4000) - 2000) / 4.0f)
FACE_TEST1V(SetField44, 0x4cb480, int32_t, SetField44, rng() % 100)
FACE_TEST1V(SetRotation, 0x4cb230, uint8_t, SetRotation, rng())
FACE_TEST1V(SetField4C, 0x4cb5d0, int32_t, SetField4C, rng() % 2 ? lot->mField4C : int32_t(rng() % 1000))
FACE_TEST1V(SetLotId, 0x4cb1d0, int32_t, SetLotId, rng() % 2 ? lot->mLotId : int32_t(rng() % 50))
FACE_TEST1V(SetBaseByte88, 0x4cb540, uint8_t, SetBaseByte88, rng())
FACE_TEST1V(SetBaseWord90, 0x4cb570, uint32_t, SetBaseWord90, rng())
FACE_TEST1V(SetBaseWord8C, 0x4cb5a0, uint32_t, SetBaseWord8C, rng())
FACE_TEST1(RemoveConnectionBySecond, 0x4ca6b0, uint8_t, int32_t, RemoveConnectionBySecond, rng() % 7)
FACE_TEST1(RemoveConnectionByFirst, 0x4ca750, uint8_t, int32_t, RemoveConnectionByFirst, rng() % 7)
FACE_TEST1(GetConnectionField0, 0x4c9910, int32_t, int32_t, GetConnectionField0, ((lot->mConnections.empty() ? lot->mConnections.push_back({1, 2, 3, 4}) : void()), rng() % lot->mConnections.size()))
FACE_TEST1(GetConnectionField1, 0x4c98b0, int32_t, int32_t, GetConnectionField1, ((lot->mConnections.empty() ? lot->mConnections.push_back({1, 2, 3, 4}) : void()), rng() % lot->mConnections.size()))
FACE_TEST1(GetConnectionField2, 0x4c9850, int32_t, int32_t, GetConnectionField2, ((lot->mConnections.empty() ? lot->mConnections.push_back({1, 2, 3, 4}) : void()), rng() % lot->mConnections.size()))
FACE_TEST1(GetConnectionField3, 0x4c9780, int32_t, int32_t, GetConnectionField3, ((lot->mConnections.empty() ? lot->mConnections.push_back({1, 2, 3, 4}) : void()), rng() % lot->mConnections.size()))
FACE_TEST1(GetMappedValue, 0x4c9710, uint32_t, int16_t, GetMappedValue, rng() % 9 - 3)
FACE_TEST2(AddConnection, 0x4c8f10, int32_t, AddConnection, int32_t(rng() % 1000) - 500)
FACE_TEST2(SetConnectionField2, 0x4cb130, int16_t, SetConnectionField2, rng() % 3 ? int32_t(rng() % 10) - 4 : int32_t(rng()))
FACE_TEST2(SetConnectionField3, 0x4cb700, int16_t, SetConnectionField3, rng() % 3 ? int32_t(rng() % 10) - 4 : int32_t(rng()))

DIFF_TEST(LotInfo_SetOrigin, 0x4cb390)
{
    for (uint32_t seed = 0; seed < 100; ++seed)
        Pair(ctx, "SetOrigin", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {
            const int32_t x = int32_t(rng() % 500), y = int32_t(rng() % 500);
            LOT_CALL(void(__thiscall *)(void *, int32_t, int32_t), 1, 0x4cb390, SetOrigin, x, y);
            return 0;
        });
}

// Out-pointer slots: the rectangles / sizes are prefilled with the same random words on both sides.
DIFF_TEST(LotInfo_GetSize, 0x4c93b0)
{
    for (uint32_t seed = 0; seed < 300; ++seed)
        Pair(ctx, "GetSize", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {
            int32_t w = int32_t(rng()), d = int32_t(rng());
            LOT_CALL(void(__thiscall *)(void *, int32_t *, int32_t *), 1, 0x4c93b0, GetSize, &w, &d);
            gEv.push_back(uint32_t(w));
            gEv.push_back(uint32_t(d));
            return 0;
        });
}

#define RECT_TEST(Name, Rva, Method)                                                                                           \
    DIFF_TEST(LotInfo_##Name, Rva)                                                                                           \
    {                                                                                                                         \
        for (uint32_t seed = 0; seed < 300; ++seed)                                                                           \
            Pair(ctx, #Name, seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {                           \
                int32_t rect[4];                                                                                              \
                for (auto &v : rect) v = int32_t(rng());                                                                      \
                LOT_CALL(void(__thiscall *)(void *, int32_t *), 1, Rva, Method, rect);                                       \
                for (auto v : rect) gEv.push_back(uint32_t(v));                                                               \
                return 0;                                                                                                     \
            });                                                                                                               \
    }
RECT_TEST(GetBounds, 0x4c9230, GetBounds)
RECT_TEST(GetInnerBounds, 0x4c9280, GetInnerBounds)
RECT_TEST(GetEdgeStrip, 0x4c9300, GetEdgeStrip)

#define TEXT_TEST(Name, Rva, Method)                                                                                           \
    DIFF_TEST(LotInfo_##Name, Rva)                                                                                           \
    {                                                                                                                         \
        for (uint32_t seed = 0; seed < 60; ++seed)                                                                            \
            Pair(ctx, #Name, seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {                           \
                cRZString text;                                                                                               \
                text.Construct();                                                                                             \
                text.mString.assign(rng() % 30, char('a' + rng() % 26));                                                      \
                LOT_CALL(void(__thiscall *)(void *, void *), 1, Rva, Method, static_cast<void *>(&text));                    \
                return 0;                                                                                                     \
            });                                                                                                               \
    }
TEXT_TEST(SetName, 0x4cb410, SetName)
TEXT_TEST(SetDescription, 0x4cb4f0, SetDescription)

DIFF_TEST(LotInfo_SetNameText, 0x4cb440)
{
    for (uint32_t seed = 0; seed < 60; ++seed)
        Pair(ctx, "SetNameText", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {
            std::string text(rng() % 30, char('a' + rng() % 26));
            LOT_CALL(void(__thiscall *)(void *, const char *), 1, 0x4cb440, SetNameText, text.c_str());
            return 0;
        });
}

DIFF_TEST(LotInfo_Describe, 0x4c9690)
{
    for (uint32_t seed = 0; seed < 40; ++seed)
        Pair(ctx, "Describe", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &) -> uint32_t {
            return LOT_CALL(uint8_t(__thiscall *)(void *, void *), 4, 0x4c9690, Describe, static_cast<void *>(&gFakes[kTarget]));
        });
}

DIFF_TEST(LotInfo_GetGZCLSID, 0x4c91f0)
{
    for (uint32_t seed = 0; seed < 2; ++seed)
        Pair(ctx, "GetGZCLSID", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &) -> uint32_t {
            return LOT_CALL(uint32_t(__thiscall *)(void *), 2, 0x4c91f0, GetGZCLSID);
        });
}

DIFF_TEST(LotInfo_QueryInterface, 0x4ca090)
{
    const uint32_t ids[] = {0x6bf97e03, 0x456b8f1d, 0xe4fda3d4, 0x51f85085, 1, 0, 0x6bf97e04, 0x12345678, 0xffffffff};
    for (uint32_t seed = 0; seed < 9; ++seed)
        Pair(ctx, "QueryInterface", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &) -> uint32_t {
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            uint8_t r;
            r = LOT_CALL(uint8_t(__thiscall *)(void *, uint32_t, void **), 0, 0x4ca090, QueryInterface, ids[seed], &out);
            gEv.push_back(Norm(P(out)));
            return r;
        });
}

// ---- Read / Write ----

DIFF_TEST(LotInfo_Write, 0x4cb8f0)
{
    for (uint32_t seed = 0; seed < 200; ++seed)
        Pair(ctx, "Write", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &) -> uint32_t {
            SetupStream(0, true);
            return LOT_CALL(uint8_t(__thiscall *)(void *, void *), 2, 0x4cb8f0, Write, static_cast<void *>(&gFakes[kStream]));
        });
}

DIFF_TEST(LotInfo_Read, 0x4ca2a0)
{
    const uint32_t versions[] = {0, 6, 7, 8, 9, 10, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x100, 0xffff};
    for (uint32_t seed = 0; seed < 40; ++seed)
        for (uint32_t version : versions)
            Pair(ctx, "Read", seed * 1000 + version, [&](cTSLotInfo *lot, bool orig, std::mt19937 &) -> uint32_t {
                SetupStream(version, false);
                return LOT_CALL(uint8_t(__thiscall *)(void *, void *), 2, 0x4ca2a0, Read, static_cast<void *>(&gFakes[kStream]));
            });
}

// ---- constructors, destructor, clone ----

namespace {
// Captures a lot info with its real base: the base's vtable and count, and the base's +8 face relative to its block.
void CaptureReal(Blob &b, cTSLotInfo *lot)
{
    Capture(b, lot);
    auto *base = static_cast<uint8_t *>(lot->mBase) - 8;
    b.push_back(Norm(P(*reinterpret_cast<void **>(base))));
    b.push_back(*reinterpret_cast<uint32_t *>(base + 4));
}
} // namespace

DIFF_TEST(LotInfo_Construct, 0x4c8a40)
{
    MarkStack();
    lotshape::Ours();
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            auto *mem = static_cast<uint8_t *>(::operator new(sizeof(cTSLotInfo)));
            for (size_t i = 0; i < sizeof(cTSLotInfo); ++i) mem[i] = uint8_t(rng());
            auto *lot = reinterpret_cast<cTSLotInfo *>(mem);
            gLot = lot;
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(kDefaultConstruct)(lot)
                                : cTSLotInfoAdapters::Construct(lot, nullptr);
            blobs[side].push_back(Norm(P(r)));
            CaptureReal(blobs[side], lot);
            gLot = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Construct", seed);
    }
}

// 0x4c9180, the creator the framework knows by address: the +8 face of a new default lot info.
DIFF_TEST(LotInfo_Create, 0x4c9180)
{
    MarkStack();
    lotshape::Ours();
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Blob blobs[2];
        heappoison::Scope poison(ctx, seed % 2 ? 0x5a : 0xcd); // the unset rotation byte and padding are compared too
        for (int side = 0; side < 2; ++side) {
            void *r = side == 0 ? ctx.Original<void *(__cdecl *)()>(0x4c9180)() : static_cast<void *>(cTSLotInfo::Create());
            auto *lot = reinterpret_cast<cTSLotInfo *>(static_cast<uint8_t *>(r) - 8);
            gLot = lot;
            blobs[side].push_back(Norm(P(r)));
            CaptureReal(blobs[side], lot);
            gLot = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
    }
}

// Our side through the copy constructor's adapter, from a source built by our constructor (vtable route: the source's slot 4
// answers the base through our vtable) or by the original one (adapter route).
DIFF_TEST(LotInfo_CopyConstruct, 0x4c8800)
{
    lotshape::Ours();
    for (int route = 0; route < 2; ++route)
        for (uint32_t seed = 0; seed < 60; ++seed) {
            MarkStack();
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 rng(seed * 31u + 7);
                ResetFakes(seed, rng);
                OptionMask options(seed % 2 ? 0x400u : 0u);
                cTSLotInfo *source = BuildLot(ctx, seed, true, side == 1 && route == 0);
                auto *mem = static_cast<uint8_t *>(::operator new(sizeof(cTSLotInfo)));
                for (size_t i = 0; i < sizeof(cTSLotInfo); ++i) mem[i] = uint8_t(rng());
                auto *lot = reinterpret_cast<cTSLotInfo *>(mem);
                gLot = lot;
                void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *, void *)>(kCopyConstruct)(lot, source)
                                    : cTSLotInfoAdapters::CopyConstruct(lot, nullptr, source);
                blobs[side].push_back(Norm(P(r)));
                CaptureReal(blobs[side], lot);
                gLot = nullptr;
            }
            Same(ctx, blobs[0], blobs[1], "CopyConstruct", seed + (route ? 0x80000000u : 0u));
        }
}

DIFF_TEST(LotInfo_Clone, 0x4c90b0)
{
    lotshape::Ours();
    for (int route = 0; route < 2; ++route) {
        lotshape::gRoute = lotshape::Route(route);
        for (uint32_t seed = 0; seed < 40; ++seed) {
            MarkStack();
            Blob blobs[2];
            // The constructors leave +0x48 and the upper bytes of +0xa0 unset: every heap block is poisoned on both sides
            // (heap_poison.h, DT), so they are compared too (before, they were skipped as fresh heap memory).
            heappoison::Scope poison(ctx, seed % 2 ? 0x5a : 0xcd);
            for (int side = 0; side < 2; ++side) {
                std::mt19937 rng(seed * 31u + 7);
                ResetFakes(seed, rng);
                OptionMask options(seed % 2 ? 0x400u : 0u);
                cTSLotInfo *source = BuildLot(ctx, seed, true, side == 1 && route == 0);
                cTSLotInfo *lot = source;
                const bool orig = side == 0;
                lotshape::gTrapHits = 0;
                void *r = LOT_CALL(void *(__thiscall *)(void *), 1, 0x4c90b0, Clone);
                auto *copy = reinterpret_cast<cTSLotInfo *>(static_cast<uint8_t *>(r) - 8);
                gLot = copy;
                CaptureReal(blobs[side], copy);
                blobs[side].push_back(uint32_t(lotshape::gTrapHits));
                gLot = nullptr;
            }
            Same(ctx, blobs[0], blobs[1], "Clone", seed + (route ? 0x80000000u : 0u));
        }
    }
}

namespace {
// Vtable route: our lot info, destroyed through its slot 3 (the compiler's scalar deleting destructor; flags 0 for the plain
// destructor's test). Adapter route: a lot info built by the original constructor, destroyed by the adapter.
void DestroyCase(difftest::Context &ctx, uint32_t seed, bool deleting, int route)
{
    MarkStack();
    Blob blobs[2];
    for (int side = 0; side < 2; ++side) {
        std::mt19937 rng(seed * 31u + 7);
        ResetFakes(seed, rng);
        cTSLotInfo *lot = BuildLot(ctx, seed, seed % 2 == 0, side == 1 && route == 0);
        lot->mField84 = seed % 3 == 0 ? &gFakes[kTarget] : nullptr;
        const uint32_t flags = deleting ? (seed % 4 < 2 ? 1u : 0u) : 0u;
        gEv.clear();
        // The fakes' Release appends to gEv during the call: its buffer must not grow (and free the old one) while frees
        // are recorded, or the first case with two releases in a process counts the log's own 4-byte block.
        gEv.reserve(64);
        difftest::FreeRecorder rec;
        if (side == 0) {
            if (deleting) ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x4c8ee0)(lot, flags);
            else ctx.Original<void(__thiscall *)(void *)>(kDestruct)(lot);
        } else if (route == 0) {
            lotshape::CallOwn<void *(__thiscall *)(void *, uint32_t)>(lot, 0, 3, flags);
        } else {
            if (deleting) cTSLotInfoAdapters::DeletingDestructor(lot, nullptr, flags);
            else cTSLotInfoAdapters::Destruct(lot, nullptr);
        }
        rec.Stop();
        Blob &b = blobs[side];
        b.insert(b.end(), gEv.begin(), gEv.end());
        size_t sizes[64];
        const size_t n = rec.SortedSizes(sizes, 64);
        b.push_back(uint32_t(n));
        for (size_t i = 0; i < n && i < 64; ++i) b.push_back(uint32_t(sizes[i]));
        b.push_back(rec.Freed(lot));
        if (!rec.Freed(lot))
            for (unsigned f = 0; f < lotshape::kFaces; ++f) b.push_back(lotshape::VtCode(lotshape::Word(lot, lotshape::kFaceOffset[f])));
        for (const auto &f : gFakes) b.push_back(uint32_t(f.refs));
    }
    Same(ctx, blobs[0], blobs[1], deleting ? "DeletingDestructor" : "Destruct", seed + (route ? 0x80000000u : 0u));
}
} // namespace

DIFF_TEST(LotInfo_Destruct, 0x4c8c30)
{
    lotshape::Ours();
    for (int route = 0; route < 2; ++route)
        for (uint32_t seed = 0; seed < 60; ++seed) DestroyCase(ctx, seed, false, route);
}

DIFF_TEST(LotInfo_DeletingDestructor, 0x4c8ee0)
{
    lotshape::Ours();
    for (int route = 0; route < 2; ++route)
        for (uint32_t seed = 0; seed < 60; ++seed) DestroyCase(ctx, seed, true, route);
}

// ---- slots that reach the persist system (cache 0x10faf6c) and the allocator service static (0x10c95d4) ----

namespace {
uint32_t gBlock[4];
uint32_t PersistList(Fake *f, uint32_t *a)
{
    uint32_t *vec = reinterpret_cast<uint32_t *>(a[0]);
    gEv.insert(gEv.end(), {vec[0], vec[1], vec[2], a[1], a[2]});
    if (f->rng() % 2) {
        vec[0] = P(&gBlock[0]);
        vec[1] = vec[2] = P(&gBlock[2]);
    }
    return f->rng() % 3 ? f->rng() : 0;
}
uint32_t PersistFind(Fake *f, uint32_t *a)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(a[0]);
    gEv.insert(gEv.end(), {key[0], key[1], key[2], key[3]});
    void **out = reinterpret_cast<void **>(a[2]);
    gEv.push_back(Norm(P(*out)));
    // A nonzero answer always hands out the resource (the original calls through the pointer whenever the answer is nonzero).
    if (f->rng() % 4 == 0) return 0;
    *out = &gFakes[kResource];
    return f->rng() % 3 ? (f->rng() | 1) & 0xff : 0;
}
uint32_t ResourceQuery(Fake *f, uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(a[1]) = f->rng();
    return f->rng() & 0xff;
}

struct PersistWorld {
    explicit PersistWorld(std::mt19937 &rng)
        : persist(GameAddress<void *>(0x10faf6c)), alloc(GameAddress<void *>(0x10c95d4)), guard(GameAddress<int32_t>(0x10c95d8)),
          savedPersist(*persist), savedAlloc(*alloc), savedGuard(*guard)
    {
        *persist = &gFakes[kPersist];
        *alloc = &gFakes[kAlloc];
        *guard = INT32_MIN; // initialised: the cached service is used as is
        gFakes[kPersist].Set(5, 5, 0, &PersistFind);
        gFakes[kPersist].Set(11, 1, rng());
        gFakes[kPersist].Set(36, 3, 0, &PersistList);
        gFakes[kResource].Set(0, 2, 0, &ResourceQuery);
        gFakes[kAlloc].Set(4, 1, 0);
    }
    ~PersistWorld()
    {
        *persist = savedPersist;
        *alloc = savedAlloc;
        *guard = savedGuard;
    }
    void **persist, **alloc;
    int32_t *guard;
    void *savedPersist, *savedAlloc;
    int32_t savedGuard;
};
} // namespace

DIFF_TEST(LotInfo_HasLotResources, 0x4c9cc0)
{
    for (uint32_t seed = 0; seed < 100; ++seed)
        Pair(ctx, "HasLotResources", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {
            PersistWorld world(rng);
            return LOT_CALL(uint8_t(__thiscall *)(void *), 1, 0x4c9cc0, HasLotResources);
        });
}

DIFF_TEST(LotInfo_QueryLotResource, 0x4c9550)
{
    for (uint32_t seed = 0; seed < 100; ++seed)
        Pair(ctx, "QueryLotResource", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {
            PersistWorld world(rng);
            const uint32_t instance = rng(), group = rng(), iid = rng();
            uint32_t out = 0x5555;
            uint8_t r;
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void **);
            r = LOT_CALL(Fn, 1, 0x4c9550, QueryLotResource, instance, group, iid, reinterpret_cast<void **>(&out));
            gEv.push_back(out);
            return r;
        });
}

namespace {
char gText[32];
uint32_t TextLength(Fake *f, uint32_t *)
{
    const uint32_t n = f->rng() % 3 == 0 ? 0 : f->rng() % 30;
    for (uint32_t i = 0; i < n; ++i) gText[i] = char('a' + f->rng() % 26);
    gText[n] = 0;
    return n;
}
uint32_t TerrainHeight(Fake *f, uint32_t *a)
{
    const int32_t *rect = reinterpret_cast<const int32_t *>(a[0]);
    gEv.insert(gEv.end(), {uint32_t(rect[0]), uint32_t(rect[1]), uint32_t(rect[2]), uint32_t(rect[3])});
    *reinterpret_cast<float *>(a[1]) = float(int32_t(f->rng() % 1000)) / 4.0f;
    *reinterpret_cast<float *>(a[2]) = float(int32_t(f->rng() % 1000)) / 8.0f;
    return 0;
}
} // namespace

DIFF_TEST(LotInfo_LoadNamesFromResource, 0x4c9db0)
{
    for (uint32_t seed = 0; seed < 100; ++seed)
        Pair(ctx, "LoadNamesFromResource", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {
            PersistWorld world(rng);
            gFakes[kResource].Set(6, 2, P(&gFakes[kText]));
            gFakes[kText].Set(6, 0, P(gText));
            gFakes[kText].Set(7, 0, 0, &TextLength);
            gFakes[kBase].Set(20, 1, 0);
            const uint32_t instance = rng(), group = rng();
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
            return LOT_CALL(Fn, 1, 0x4c9db0, LoadNamesFromResource, instance, group);
        });
}

DIFF_TEST(LotInfo_GetElevation, 0x4c9f90)
{
    for (uint32_t seed = 0; seed < 200; ++seed)
        Pair(ctx, "GetElevation", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {
            gFakes[kApp].Set(11, 0, rng() % 4 == 0 ? 0 : P(&gFakes[kTerrain]));
            gFakes[kTerrain].Set(12, 3, 0, &TerrainHeight);
            if (rng() % 8 == 0) lot->mElevation = std::numeric_limits<float>::quiet_NaN();
            float r;
            r = LOT_CALL(float(__thiscall *)(void *), 1, 0x4c9f90, GetElevation);
            uint32_t bits;
            std::memcpy(&bits, &r, 4);
            return bits;
        });
}

// ---- 0x4cb630 (slot 56): the value map entry and the message ----

namespace {
// Both allocators (the image's RZMalloc 0x1be3b0 behind 0x1c03b7, our RZMemory::Malloc) hand out poisoned plain memory; Norm
// turns pointers into the block into offsets.
void *__cdecl TestMalloc(uint32_t bytes)
{
    void *p = std::malloc(bytes);
    if (p) std::memset(p, 0xcd, bytes);
    gMessage = reinterpret_cast<uintptr_t>(p);
    return p;
}

// The message server's slot 4 (message, flags): logs the message as it is posted (its vtable as an RVA, then every word: count,
// flag byte, id, values) and keeps it alive (AddRef), so its Release doesn't free it.
uint32_t ServerPost(Fake *, uint32_t *a)
{
    auto *message = reinterpret_cast<uint32_t *>(a[0]);
    gEv.push_back(message[0] - P(GameAddress<void>(0)));
    for (unsigned i = 1; i < 11; ++i) gEv.push_back(message[i]);
    void **vtable = *reinterpret_cast<void ***>(message);
    reinterpret_cast<uint32_t(__thiscall *)(void *)>(vtable[1])(message);
    return 0;
}
} // namespace

DIFF_TEST(LotInfo_SetMappedValue, 0x4cb630)
{
    lotshape::CodePatch theirs(ctx, 0x1be3b0, reinterpret_cast<void *>(&TestMalloc));
    lotshape::CodePatch ours(reinterpret_cast<void *>(&RZMemory::Malloc), reinterpret_cast<void *>(&TestMalloc));
    void **server = GameAddress<void *>(0x10f519c); // the message server's cache (0x9c5a89 answers it when set)
    void *const saved = *server;
    for (uint32_t seed = 0; seed < 200; ++seed)
        Pair(ctx, "SetMappedValue", seed, [&](cTSLotInfo *lot, bool orig, std::mt19937 &rng) -> uint32_t {
            *server = &gFakes[kAlloc];
            gFakes[kAlloc].Set(4, 2, 0, &ServerPost);
            const int16_t id = int16_t(rng() % 3 ? int32_t(rng() % 9) - 3 : int32_t(rng()));
            const int16_t value = int16_t(rng());
            LOT_CALL(void(__thiscall *)(void *, int16_t, int16_t), 1, 0x4cb630, SetMappedValue, id, value);
            gMessage = 0;
            return 0;
        });
    *server = saved;
}

// ---- every slot no other test calls, through each side's own vtable ----
//
// The adjustor thunks the compiler makes for us, as the original's: QueryInterface 0x4ca0fd (+8), 0x4ca115 (+0xc), 0x4ca105
// (+0x10), 0x4ca10d (+0x14); the AddRef / Release thunks are folded (shared_vf_*).
// The folded bodies (the base accessors, the origin, the texts, the words at +0x44 / +0x4c / +0x94, the face casts, the persist
// face's key, DescribeMore) and the secondary faces' QueryInterface / AddRef / Release thunks, plus cRZUnknown's RemoveRef and
// count. The original side calls the game's vtable slot (shared code), ours our vtable's.

namespace {
enum SlotKind { kNone, kVoidNone, kWord, kQuery, kSetBase, kOrigin, kKeyOut, kKeyIn };
struct SlotCase {
    unsigned face, slot;
    SlotKind kind;
};
constexpr SlotCase kSlotCases[] = {
    {0, 1, kNone},   {0, 2, kNone},     {0, 4, kNone},   {0, 5, kNone},   {1, 0, kQuery},  {1, 1, kNone},   {1, 2, kNone},
    {1, 3, kSetBase}, {1, 4, kNone},    {1, 8, kNone},   {1, 10, kOrigin}, {1, 18, kNone}, {1, 24, kNone},  {1, 25, kNone},
    {1, 28, kNone},  {1, 33, kNone},    {1, 36, kNone},  {2, 0, kQuery},  {2, 1, kNone},   {2, 2, kNone},   {3, 0, kQuery},
    {3, 1, kNone},   {3, 2, kNone},     {3, 3, kKeyOut}, {3, 4, kKeyIn},  {3, 5, kNone},   {3, 6, kNone},   {3, 7, kNone},
    {3, 8, kWord},   {3, 9, kWord},     {3, 10, kWord},  {3, 11, kNone},  {4, 0, kQuery},  {4, 1, kNone},   {4, 2, kNone},
    {4, 4, kWord},
};
} // namespace

DIFF_TEST(LotInfo_Slots, 0x4ca0fd)
{
    const uint32_t ids[] = {0x6bf97e03, 0x456b8f1d, 0xe4fda3d4, 0x51f85085, 1, 0, 0x12345678};
    for (const SlotCase &c : kSlotCases)
        for (uint32_t seed = 0; seed < 12; ++seed)
            Pair(
                ctx, "Slots", c.face * 0x10000u + c.slot * 0x100u + seed,
                [&](cTSLotInfo *lot, bool, std::mt19937 &rng) -> uint32_t {
                    lot->mRefCount = 3 + rng() % 4; // Release stays above the deleting count
                    using lotshape::CallOwn;
                    switch (c.kind) {
                    case kNone: return CallOwn<uint32_t(__thiscall *)(void *)>(lot, c.face, c.slot);
                    case kVoidNone: CallOwn<void(__thiscall *)(void *)>(lot, c.face, c.slot); return 0;
                    case kWord: return CallOwn<uint8_t(__thiscall *)(void *, uint32_t)>(lot, c.face, c.slot, uint32_t(rng()));
                    case kQuery: {
                        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
                        const uint32_t r = CallOwn<uint8_t(__thiscall *)(void *, uint32_t, void **)>(lot, c.face, c.slot,
                                                                                                    ids[rng() % 7], &out);
                        gEv.push_back(Norm(P(out)));
                        return r;
                    }
                    case kSetBase: {
                        void *base = rng() % 3 == 0 ? nullptr : rng() % 2 ? &gFakes[kTarget] : &gFakes[kBase];
                        CallOwn<void(__thiscall *)(void *, void *)>(lot, c.face, c.slot, base);
                        return 0;
                    }
                    case kOrigin: {
                        int32_t x = int32_t(rng()), y = int32_t(rng());
                        CallOwn<void(__thiscall *)(void *, int32_t *, int32_t *)>(lot, c.face, c.slot, &x, &y);
                        gEv.push_back(uint32_t(x));
                        gEv.push_back(uint32_t(y));
                        return 0;
                    }
                    case kKeyOut: {
                        uint32_t key[4] = {rng(), rng(), rng(), rng()};
                        const uint32_t r = CallOwn<uint32_t *(__thiscall *)(void *, uint32_t *)>(lot, c.face, c.slot, key) == key;
                        gEv.insert(gEv.end(), key, key + 4);
                        return r;
                    }
                    case kKeyIn: {
                        const uint32_t key[4] = {rng(), rng(), rng(), rng()};
                        return CallOwn<uint8_t(__thiscall *)(void *, const uint32_t *)>(lot, c.face, c.slot, key);
                    }
                    }
                    return 0;
                },
                false, 1);
}
