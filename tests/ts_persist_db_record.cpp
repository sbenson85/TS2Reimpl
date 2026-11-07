// Differential tests for the cTSPersistDBRecord functions reimplemented so far (field map in
// src/ts/cTSPersistDBRecord.h). Each run gets its own copy of a fake record: the primary bytes are
// random, the cITSPersistDBRecord subobject (+0x38) and cIGZPersistDBRecord subobject (+0x3c) carry
// fake vtables whose slots record their calls, and the secondary object of vf60_15 (+0x5c) is a fake
// with a recording slot 0. Return values (full eax, or al where the original sets only al), the
// recorded calls with their arguments (pointers as offsets from the primary), and the primary bytes
// that changed must all match.
#include "difftest.h"
#include "game/game.h"
#include "heap_poison.h"
#include "ts/cTSPersistDBRecord.h"

#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using NoArgPtr = void *(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using TwoArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using OneArgBool = uint8_t(__thiscall *)(void *, uint32_t);
using TwoArgVoid = void(__thiscall *)(void *, uint32_t, uint32_t);

// Recorded calls: a kind, the object's id, then the arguments.
std::vector<uint32_t> gTrace;
std::vector<std::pair<const void *, uint32_t>> gIds;
uintptr_t gBase = 0;
uint32_t gRetS12 = 0, gRetS16 = 0, gRetG12 = 0, gRetG13 = 0, gRetO0 = 0;

uint32_t IdOf(const void *p)
{
    for (const auto &e : gIds)
        if (e.first == p) return e.second;
    return 0xffff;
}

// Pointer arguments are recorded relative to the primary object, which differs between the worlds.
uint32_t Rel(uint32_t p)
{
    return (p >= gBase && p < gBase + 0x100) ? p - static_cast<uint32_t>(gBase) : p;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(IdOf(self));
}

// The fake slots are __fastcall so they pop their stack arguments like the game's callees do.
uint32_t __fastcall SlotS12(void *self, void *)
{
    Note(12, self);
    return gRetS12;
}

uint32_t __fastcall SlotS16(void *self, void *, uint32_t a)
{
    Note(16, self);
    gTrace.push_back(a);
    return gRetS16;
}

uint32_t __fastcall SlotG12(void *self, void *)
{
    Note(112, self);
    return gRetG12;
}

uint32_t __fastcall SlotG13(void *self, void *, uint32_t a)
{
    Note(113, self);
    gTrace.push_back(a);
    return gRetG13;
}

uint32_t __fastcall SlotO0(void *self, void *, uint32_t a1, uint32_t a2)
{
    Note(200, self);
    gTrace.push_back(a1);
    gTrace.push_back(Rel(a2));
    return gRetO0;
}

constexpr size_t kPrimaryBytes = 0x100;
constexpr size_t kObjBytes = 0x20;

// Second batch fakes. Pointers the fakes hand back are world-specific, so Run() points these at the
// world being run.
uint32_t gTextPtr = 0;    // returned by the fake ITS slot 11 (text of the record)
uint32_t gTextOff = 0;    // offset added to it
uint32_t gSrcData = 0;    // returned by the fake string's slot 6
uint32_t gSrcLen = 0;     // returned by the fake string's slot 7
uint32_t gRetS11 = 0, gRetS17 = 0, gRetP23 = 0, gRetSS11 = 0;
const void *gGameStringVT = nullptr;

// What a cRZString-like object looks like to the recorders: a known fake by id, else its string content
// (size and bytes), reference count and whether it carries the game's cRZString vtable.
void Digest(const void *p)
{
    if (!p) {
        gTrace.push_back(0xd0);
        return;
    }
    uint32_t id = IdOf(p);
    if (id != 0xffff) {
        gTrace.push_back(0xd1);
        gTrace.push_back(id);
        return;
    }
    const auto *b = static_cast<const uint8_t *>(p);
    const auto &s = *reinterpret_cast<const std::string *>(b + 4);
    gTrace.push_back(0xd2);
    gTrace.push_back(*reinterpret_cast<void *const *>(b) == gGameStringVT);
    gTrace.push_back(static_cast<uint32_t>(s.size()));
    for (char c : s) gTrace.push_back(static_cast<uint8_t>(c));
    gTrace.push_back(*reinterpret_cast<const uint32_t *>(b + 0x1c));
}

uint32_t __fastcall SlotS11(void *self, void *)
{
    Note(11, self);
    return gTextPtr + gTextOff;
}

uint32_t __fastcall SlotS17(void *self, void *)
{
    Note(17, self);
    return gRetS17;
}

uint32_t __fastcall SlotP23(void *self, void *)
{
    Note(23, self);
    return gRetP23;
}

uint32_t __fastcall SlotP24(void *self, void *, uint32_t a)
{
    Note(24, self);
    gTrace.push_back(a);
    return 0;
}

uint32_t __fastcall SlotSS11(void *self, void *)
{
    Note(511, self);
    return gRetSS11;
}

uint32_t __fastcall SlotF1(void *self, void *)
{
    Note(301, self);
    return 1;
}

uint32_t __fastcall SlotF2(void *self, void *)
{
    Note(302, self);
    return 1;
}

uint32_t __fastcall SlotStr6(void *self, void *)
{
    Note(406, self);
    return gSrcData;
}

uint32_t __fastcall SlotStr7(void *self, void *)
{
    Note(407, self);
    return gSrcLen;
}

uint32_t __fastcall SlotStr17(void *self, void *, const void *what)
{
    Note(417, self);
    Digest(what);
    return 0;
}

struct World {
    alignas(8) uint8_t primary[kPrimaryBytes];
    alignas(8) uint8_t objO[kObjBytes];
    alignas(8) uint8_t fa[kObjBytes]; // reference-counted fakes (ids 6 and 8)
    alignas(8) uint8_t fb[kObjBytes];
    alignas(8) uint8_t arg[kObjBytes]; // the fake string argument (id 7)
    char text[100];                    // the record's data and the source text
    void *vtS[20]; // cITSPersistDBRecord subobject (+0x38), slots 11, 12, 16 and 17 used
    void *vtG[20]; // cIGZPersistDBRecord subobject (+0x3c), slots 12 and 13 used
    void *vtO[4];  // the secondary object of vf60_15, slot 0 used
    void *vtP[32]; // primary vtable (+0), slots 23 and 24
    void *vtSS[16]; // cIGZSerializableStream subobject (+0x20), slot 11
    void *vtF[4];  // fakes fa/fb: slots 1 and 2
    void *vtStr[20]; // fake string: slots 6, 7 and 17
};

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t Addr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

// Builds a world from a seed. The subobject pointer at +0x5c (the secondary object of vf60_15) is
// null in a quarter of the cases.
void Build(World &w, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.primary, sizeof(w.primary));
    fill(w.objO, sizeof(w.objO));

    gRetS12 = rng();
    gRetS16 = rng();
    gRetG12 = rng();
    gRetG13 = rng();
    gRetO0 = rng();

    for (auto &s : w.vtS) s = nullptr;
    w.vtS[12] = reinterpret_cast<void *>(&SlotS12);
    w.vtS[16] = reinterpret_cast<void *>(&SlotS16);
    for (auto &s : w.vtG) s = nullptr;
    w.vtG[12] = reinterpret_cast<void *>(&SlotG12);
    w.vtG[13] = reinterpret_cast<void *>(&SlotG13);
    for (auto &s : w.vtO) s = nullptr;
    w.vtO[0] = reinterpret_cast<void *>(&SlotO0);

    Put(w.primary, 0x38, Addr(w.vtS));
    Put(w.primary, 0x3c, Addr(w.vtG));
    Put(w.primary, 0x5c, rng() % 4 ? Addr(w.objO) : uintptr_t(0));
    Put(w.objO, 0x00, Addr(w.vtO));

    // Second batch: further fake vtables and objects. Every value comes from the seed, after the
    // draws above, so the first batch's worlds are unchanged.
    fill(w.fa, sizeof(w.fa));
    fill(w.fb, sizeof(w.fb));
    fill(w.arg, sizeof(w.arg));
    for (auto &c : w.text) c = static_cast<char>('a' + rng() % 5);
    w.text[sizeof(w.text) - 1] = 0;
    gRetS11 = rng();
    gRetS17 = rng();
    gRetP23 = rng();
    gRetSS11 = rng();
    gTextOff = 0;
    gSrcLen = 0;
    w.vtS[11] = reinterpret_cast<void *>(&SlotS11);
    w.vtS[17] = reinterpret_cast<void *>(&SlotS17);
    for (auto &s : w.vtP) s = nullptr;
    w.vtP[23] = reinterpret_cast<void *>(&SlotP23);
    w.vtP[24] = reinterpret_cast<void *>(&SlotP24);
    for (auto &s : w.vtSS) s = nullptr;
    w.vtSS[11] = reinterpret_cast<void *>(&SlotSS11);
    for (auto &s : w.vtF) s = nullptr;
    w.vtF[1] = reinterpret_cast<void *>(&SlotF1);
    w.vtF[2] = reinterpret_cast<void *>(&SlotF2);
    for (auto &s : w.vtStr) s = nullptr;
    w.vtStr[6] = reinterpret_cast<void *>(&SlotStr6);
    w.vtStr[7] = reinterpret_cast<void *>(&SlotStr7);
    w.vtStr[17] = reinterpret_cast<void *>(&SlotStr17);
    Put(w.primary, 0x00, Addr(w.vtP));
    Put(w.primary, 0x20, Addr(w.vtSS));
    Put(w.primary, 0x34, Addr(w.text));
    Put(w.fa, 0x00, Addr(w.vtF));
    Put(w.fb, 0x00, Addr(w.vtF));
    Put(w.arg, 0x00, Addr(w.vtStr));
}

struct Outcome {
    uint64_t ret = 0;
    std::vector<uint32_t> trace;
    // Primary-object bytes that changed, with their new values.
    std::vector<std::pair<size_t, uint8_t>> changes;
};

// Names the objects whose pointers appear in the trace.
void SetIds(const World &w)
{
    gIds = {{w.primary + 0x38, 1}, {w.primary + 0x3c, 2}, {w.objO, 3}, {w.primary, 4}, {w.primary + 0x20, 5},
            {w.fa, 6},             {w.arg, 7},             {w.fb, 8}};
}

// Where the world lives. Original and ours run on this one world, rebuilt from the seed before each, so they see it
// at the same address and the id map (and the in-the-primary-object test) is the same function on both sides:
// equal words stay equal and different words stay different, whatever the call wrote. Two worlds at two addresses
// made the map depend on the heap layout (a random word equal to one world's address was renamed in that world
// only; lane NB, tests/gz_persist_db_segment_multi_packed_files.cpp).
World &SharedWorld()
{
    static World *const w = new World;
    return *w;
}

// Runs one side.
template <typename Call> Outcome Run(World &w, Call call)
{
    SetIds(w);
    gBase = Addr(w.primary);
    gTextPtr = static_cast<uint32_t>(Addr(w.text));
    gSrcData = static_cast<uint32_t>(Addr(w.text) + 50); // the source text: a second half of the buffer
    gGameStringVT = GameAddress(0xd9b470);
    uint8_t before[kPrimaryBytes];
    std::memcpy(before, w.primary, kPrimaryBytes);

    gTrace.clear();
    Outcome out;
    out.ret = call(w);
    out.trace = gTrace;
    for (size_t i = 0; i < kPrimaryBytes; ++i)
        if (before[i] != w.primary[i]) out.changes.push_back({i, w.primary[i]});
    return out;
}

// Runs both sides on identical worlds and compares them.
struct NoPrep {
    void operator()(World &, uint32_t) const {}
};

// `prep(world, seed)` adjusts a freshly built world (it must be deterministic in the seed: it runs on
// both worlds, and the globals it sets are shared).
template <typename OrigCall, typename OursCall, typename Prep = NoPrep>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, OrigCall origCall, OursCall oursCall, Prep prep = Prep())
{
    World &sw = SharedWorld();
    Build(sw, seed);
    prep(sw, seed);
    Outcome oa = Run(sw, [&](World &w) { return origCall(w); });
    Build(sw, seed);
    prep(sw, seed);
    Outcome ob = Run(sw, [&](World &w) { return oursCall(w); });
    ++ctx.cases;
    if (oa.ret != ob.ret || oa.trace != ob.trace) {
        ctx.Fail("%s seed %u: original returned 0x%llx (%zu trace words), ours 0x%llx (%zu)", name, seed,
                 static_cast<unsigned long long>(oa.ret), oa.trace.size(), static_cast<unsigned long long>(ob.ret),
                 ob.trace.size());
    }
    if (oa.changes.size() != ob.changes.size()) {
        ctx.Fail("%s seed %u: original changed %zu primary bytes, ours %zu", name, seed, oa.changes.size(), ob.changes.size());
    } else {
        for (size_t i = 0; i < oa.changes.size(); ++i) {
            if (oa.changes[i] != ob.changes[i]) {
                ctx.Fail("%s seed %u: primary byte +0x%zx differs (original %u, ours %u)", name, seed, oa.changes[i].first,
                         oa.changes[i].second, ob.changes[i].second);
                break;
            }
        }
    }
}

// A pointer result as an offset from the primary object, or all ones for null.
uint64_t RelPtr(const void *p)
{
    return p ? static_cast<uint64_t>(Addr(p) - gBase) : 0xffffffffffffffffull;
}

// A dword argument, sometimes with a zero low byte or a zero high byte, as the byte readers see it.
uint32_t PickWord(std::mt19937 &r)
{
    uint32_t v = r();
    switch (r() % 6) {
    case 0: return v & 0xffffff00u;
    case 1: return v & 0xffu;
    default: return v;
    }
}

} // namespace

DIFF_TEST(TSPersistDBRecord_vf30, 0x1f0e50)
{
    auto orig = ctx.Original<OneArgRet32>(0x1f0e50);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf30", seed,
                [&](World &w) { return orig(w.primary, a); },
                [&](World &w) { return reinterpret_cast<cTSPersistDBRecord *>(w.primary)->vf30(a); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_5, 0x1f08a0)
{
    auto orig = ctx.Original<NoArgPtr>(0x1f08a0);
    // The null primary (this = 0x38) is never dereferenced by either side.
    uint64_t nullOrig = RelPtr(orig(reinterpret_cast<void *>(0x38)));
    uint64_t nullOurs = RelPtr(reinterpret_cast<cTSPersistDBRecordITS *>(0x38)->vf56_5());
    ++ctx.cases;
    if (nullOrig != nullOurs) ctx.Fail("vf56_5 with this = 0x38: original %llx, ours %llx", static_cast<unsigned long long>(nullOrig), static_cast<unsigned long long>(nullOurs));
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf56_5", seed,
                [&](World &w) { return RelPtr(orig(w.primary + 0x38)); },
                [&](World &w) { return RelPtr(reinterpret_cast<cTSPersistDBRecordITS *>(w.primary + 0x38)->vf56_5()); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_6, 0x1f0890)
{
    auto orig = ctx.Original<NoArgPtr>(0x1f0890);
    uint64_t nullOrig = RelPtr(orig(reinterpret_cast<void *>(0x38)));
    uint64_t nullOurs = RelPtr(reinterpret_cast<cTSPersistDBRecordITS *>(0x38)->vf56_6());
    ++ctx.cases;
    if (nullOrig != nullOurs) ctx.Fail("vf56_6 with this = 0x38: original %llx, ours %llx", static_cast<unsigned long long>(nullOrig), static_cast<unsigned long long>(nullOurs));
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf56_6", seed,
                [&](World &w) { return RelPtr(orig(w.primary + 0x38)); },
                [&](World &w) { return RelPtr(reinterpret_cast<cTSPersistDBRecordITS *>(w.primary + 0x38)->vf56_6()); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_15, 0x1f0c20)
{
    auto orig = ctx.Original<NoArgRet32>(0x1f0c20);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf56_15", seed,
                [&](World &w) { return orig(w.primary + 0x38); },
                [&](World &w) { return reinterpret_cast<cTSPersistDBRecordITS *>(w.primary + 0x38)->vf56_15(); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_16, 0x1f0ed0)
{
    auto orig = ctx.Original<OneArgRet32>(0x1f0ed0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf56_16", seed,
                [&](World &w) { return orig(w.primary + 0x38, a); },
                [&](World &w) { return reinterpret_cast<cTSPersistDBRecordITS *>(w.primary + 0x38)->vf56_16(a); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_17, 0x1f0ab0)
{
    auto orig = ctx.Original<NoArgRet32>(0x1f0ab0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf56_17", seed,
                [&](World &w) { return orig(w.primary + 0x38); },
                [&](World &w) { return reinterpret_cast<cTSPersistDBRecordITS *>(w.primary + 0x38)->vf56_17(); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf60_13, 0x1f0df0)
{
    auto orig = ctx.Original<OneArgRet32>(0x1f0df0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf60_13", seed,
                [&](World &w) { return orig(w.primary + 0x3c, a); },
                [&](World &w) { return reinterpret_cast<cTSPersistDBRecordIGZ *>(w.primary + 0x3c)->vf60_13(a); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf60_14, 0x1f0e10)
{
    auto orig = ctx.Original<OneArgRet32>(0x1f0e10);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf60_14", seed,
                [&](World &w) { return orig(w.primary + 0x3c, a); },
                [&](World &w) { return reinterpret_cast<cTSPersistDBRecordIGZ *>(w.primary + 0x3c)->vf60_14(a); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf60_15, 0x1f0a70)
{
    auto orig = ctx.Original<TwoArgRet32>(0x1f0a70);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        uint32_t a1 = PickWord(r);
        // The out-pointer is null in a quarter of the cases, else a dword inside the primary object
        // that the fake secondary object's slot 0 does not touch (the object pointer is at +0x5c).
        bool nullOut = r() % 4 == 0;
        uint32_t outOff = 0x64 + (r() % 0x8c & ~3u);
        // Both sides are compared on al: the original's null-path returns set only al.
        Compare(ctx, "vf60_15", seed,
                [&](World &w) {
                    uint32_t a2 = nullOut ? 0 : static_cast<uint32_t>(Addr(w.primary + outOff));
                    return static_cast<uint64_t>(static_cast<uint8_t>(orig(w.primary + 0x3c, a1, a2)));
                },
                [&](World &w) {
                    uint32_t a2 = nullOut ? 0 : static_cast<uint32_t>(Addr(w.primary + outOff));
                    return static_cast<uint64_t>(static_cast<uint8_t>(reinterpret_cast<cTSPersistDBRecordIGZ *>(w.primary + 0x3c)->vf60_15(a1, a2)));
                });
    }
}

DIFF_TEST(TSPersistDBRecord_vf60_17, 0x1f0bf0)
{
    auto orig = ctx.Original<OneArgBool>(0x1f0bf0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        // Random masks (zero, single bits and multiple bits), sometimes with garbage above the low byte.
        uint32_t mask = PickWord(r);
        Compare(ctx, "vf60_17", seed,
                [&](World &w) {
                    // The flag byte is random; the original sets only al.
                    return static_cast<uint64_t>(static_cast<uint8_t>(orig(w.primary + 0x3c, mask)));
                },
                [&](World &w) {
                    return static_cast<uint64_t>(reinterpret_cast<cTSPersistDBRecordIGZ *>(w.primary + 0x3c)->vf60_17(static_cast<uint8_t>(mask)));
                });
    }
}

DIFF_TEST(TSPersistDBRecord_vf60_18, 0x1f0fa0)
{
    auto orig = ctx.Original<TwoArgVoid>(0x1f0fa0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        uint32_t bits = PickWord(r);
        uint32_t set = PickWord(r);
        Compare(ctx, "vf60_18", seed,
                [&](World &w) {
                    orig(w.primary + 0x3c, bits, set);
                    return uint64_t(0);
                },
                [&](World &w) {
                    reinterpret_cast<cTSPersistDBRecordIGZ *>(w.primary + 0x3c)->vf60_18(static_cast<uint8_t>(bits), static_cast<uint8_t>(set));
                    return uint64_t(0);
                });
    }
}

// ---- second batch -------------------------------------------------------------------------------

namespace {

using PtrArgRet32 = uint32_t(__thiscall *)(void *, void *);
using NoArgRet8 = uint8_t(__thiscall *)(void *);

using IS = cTSPersistDBRecordIS;
using SS = cTSPersistDBRecordSS;
using Slots = cTSPersistDBRecordSlots;
using ITS2 = cTSPersistDBRecordITS2;
using IGZ2 = cTSPersistDBRecordIGZ2;

template <typename T> T *As(uint8_t *p)
{
    return reinterpret_cast<T *>(p);
}

// A pointer-sized value in the primary object.
uintptr_t GetPtr(const World &w, size_t off)
{
    uintptr_t v;
    std::memcpy(&v, w.primary + off, sizeof(v));
    return v;
}

// Frees a cRZString the code under test created (not one of the world's fakes).
void Reclaim(void *p)
{
    if (!p || IdOf(p) != 0xffff) return;
    auto *b = static_cast<uint8_t *>(p);
    reinterpret_cast<std::string *>(b + 4)->~basic_string();
    ::operator delete(p);
}

// Records the object stored at primary + off, then puts back the pointer the world started with (the
// object addresses differ between the two worlds, so the stored pointer itself is not compared).
void DigestAndRestore(World &w, size_t off, uintptr_t before)
{
    void *now = reinterpret_cast<void *>(GetPtr(w, off));
    Digest(now);
    Reclaim(now);
    Put(w.primary, off, before);
}

// Picks null, fa or fb.
void *Pick(World &w, uint32_t i)
{
    return i == 0 ? nullptr : (i == 1 ? static_cast<void *>(w.fa) : static_cast<void *>(w.fb));
}

} // namespace

DIFF_TEST(TSPersistDBRecord_vf16_18, 0x1f0a33)
{
    auto orig = ctx.Original<NoArgRet8>(0x1f0a33);
    for (uint32_t seed = 0; seed < 300; ++seed)
        Compare(ctx, "vf16_18", seed, [&](World &w) { return uint64_t(orig(w.primary + 0x10)); },
                [&](World &w) { return uint64_t(As<IS>(w.primary + 0x10)->vf16_18()); });
}

DIFF_TEST(TSPersistDBRecord_vf32_13, 0x1f0a3b)
{
    auto orig = ctx.Original<NoArgRet8>(0x1f0a3b);
    for (uint32_t seed = 0; seed < 300; ++seed)
        Compare(ctx, "vf32_13", seed, [&](World &w) { return uint64_t(orig(w.primary + 0x20)); },
                [&](World &w) { return uint64_t(As<SS>(w.primary + 0x20)->vf32_13()); });
}

DIFF_TEST(TSPersistDBRecord_vf32_7, 0x1f0860)
{
    auto orig = ctx.Original<NoArgPtr>(0x1f0860);
    uint64_t o = RelPtr(orig(reinterpret_cast<void *>(0x20)));
    uint64_t u = RelPtr(reinterpret_cast<SS *>(0x20)->vf32_7());
    ++ctx.cases;
    if (o != u) ctx.Fail("vf32_7 with this = 0x20: original %llx, ours %llx", static_cast<unsigned long long>(o), static_cast<unsigned long long>(u));
    for (uint32_t seed = 0; seed < 100; ++seed)
        Compare(ctx, "vf32_7", seed, [&](World &w) { return RelPtr(orig(w.primary + 0x20)); },
                [&](World &w) { return RelPtr(As<SS>(w.primary + 0x20)->vf32_7()); });
}

DIFF_TEST(TSPersistDBRecord_vf32_10, 0x1f0a10)
{
    auto orig = ctx.Original<NoArgRet32>(0x1f0a10);
    for (uint32_t seed = 0; seed < 300; ++seed)
        Compare(ctx, "vf32_10", seed,
                [&](World &w) { return uint64_t(orig(w.primary + 0x20) - gTextPtr); },
                [&](World &w) { return uint64_t(As<SS>(w.primary + 0x20)->vf32_10() - gTextPtr); });
}

DIFF_TEST(TSPersistDBRecord_vf32_11, 0x1f09e0)
{
    auto orig = ctx.Original<NoArgRet32>(0x1f09e0);
    for (uint32_t seed = 0; seed < 300; ++seed)
        Compare(ctx, "vf32_11", seed, [&](World &w) { return uint64_t(orig(w.primary + 0x20)); },
                [&](World &w) { return uint64_t(As<SS>(w.primary + 0x20)->vf32_11()); });
}

DIFF_TEST(TSPersistDBRecord_vf29_vf31, 0x1f0f90)
{
    auto orig29 = ctx.Original<OneArgRet32>(0x1f0f90);
    auto orig31 = ctx.Original<OneArgRet32>(0x1f0e60);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        uint32_t a = PickWord(r);
        Compare(ctx, "vf29", seed, [&](World &w) { return uint64_t(orig29(w.primary, a)); },
                [&](World &w) { return uint64_t(As<Slots>(w.primary)->vf29(a)); });
        Compare(ctx, "vf31", seed, [&](World &w) { return uint64_t(orig31(w.primary, a)); },
                [&](World &w) { return uint64_t(As<Slots>(w.primary)->vf31(a)); });
    }
}

DIFF_TEST(TSPersistDBRecord_AddRef, 0x1f0805)
{
    auto origIts = ctx.Original<NoArgRet32>(0x1f0805);
    auto origIgz = ctx.Original<NoArgRet32>(0x1f080d);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "ITS AddRef", seed, [&](World &w) { return uint64_t(origIts(w.primary + 0x38)); },
                [&](World &w) { return uint64_t(As<ITS2>(w.primary + 0x38)->AddRef()); });
        Compare(ctx, "IGZ AddRef", seed, [&](World &w) { return uint64_t(origIgz(w.primary + 0x3c)); },
                [&](World &w) { return uint64_t(As<IGZ2>(w.primary + 0x3c)->AddRef()); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_3_4, 0x1f0884)
{
    auto orig3 = ctx.Original<NoArgPtr>(0x1f0884);
    auto orig4 = ctx.Original<NoArgPtr>(0x1f086c);
    uint64_t o = RelPtr(orig4(reinterpret_cast<void *>(0x38)));
    uint64_t u = RelPtr(reinterpret_cast<ITS2 *>(0x38)->vf56_4());
    ++ctx.cases;
    if (o != u) ctx.Fail("vf56_4 with this = 0x38: original %llx, ours %llx", static_cast<unsigned long long>(o), static_cast<unsigned long long>(u));
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Compare(ctx, "vf56_3", seed, [&](World &w) { return RelPtr(orig3(w.primary + 0x38)); },
                [&](World &w) { return RelPtr(As<ITS2>(w.primary + 0x38)->vf56_3()); });
        Compare(ctx, "vf56_4", seed, [&](World &w) { return RelPtr(orig4(w.primary + 0x38)); },
                [&](World &w) { return RelPtr(As<ITS2>(w.primary + 0x38)->vf56_4()); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_9_10_11, 0x1f0a2b)
{
    auto orig9 = ctx.Original<NoArgRet32>(0x1f0a2b);
    auto orig10 = ctx.Original<NoArgRet32>(0x1f09fb);
    auto orig11 = ctx.Original<NoArgRet32>(0x1f0a60);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "vf56_9", seed, [&](World &w) { return uint64_t(orig9(w.primary + 0x38) - gTextPtr); },
                [&](World &w) { return uint64_t(As<ITS2>(w.primary + 0x38)->vf56_9() - gTextPtr); });
        Compare(ctx, "vf56_10", seed, [&](World &w) { return uint64_t(orig10(w.primary + 0x38)); },
                [&](World &w) { return uint64_t(As<ITS2>(w.primary + 0x38)->vf56_10()); });
        Compare(ctx, "vf56_11", seed, [&](World &w) { return uint64_t(orig11(w.primary + 0x38) - gTextPtr); },
                [&](World &w) { return uint64_t(As<ITS2>(w.primary + 0x38)->vf56_11() - gTextPtr); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_20, 0x1f0cd0)
{
    auto orig = ctx.Original<PtrArgRet32>(0x1f0cd0);
    // The record holds a NUL-terminated string at data + position, with a size that is sometimes too
    // small (the error path), and positions of both parities (the alignment step).
    auto prep = [](World &w, uint32_t seed) {
        std::mt19937 r(seed * 40503u + 11);
        uint32_t pos = r() % 30;
        uint32_t len = r() % 40;
        if (r() % 8 == 0) len = 14 + r() % 3; // around the 15-character inline buffer
        w.text[pos + len] = 0;
        int delta = static_cast<int>(r() % 7) - 3;
        Put(w.primary, 0x2c, pos);
        Put(w.primary, 0x24, static_cast<uint32_t>(pos + len + 1 + delta));
        if (r() % 20 == 0) Put(w.primary, 0x24, uint32_t(0));
    };
    for (uint32_t seed = 0; seed < 600; ++seed)
        Compare(ctx, "vf56_20", seed,
                [&](World &w) { return uint64_t(uint8_t(orig(w.primary + 0x38, w.arg))); },
                [&](World &w) { return uint64_t(As<ITS2>(w.primary + 0x38)->vf56_20(w.arg)); }, prep);
}

DIFF_TEST(TSPersistDBRecord_vf56_22, 0x1f0ef0)
{
    auto orig = ctx.Original<PtrArgRet32>(0x1f0ef0);
    auto prep = [](World &w, uint32_t seed) {
        std::mt19937 r(seed * 40503u + 13);
        gSrcLen = r() % 45;
        if (r() % 8 == 0) gSrcLen = 14 + r() % 3;
        Put(w.primary, 0x58, uintptr_t(Pick(w, r() % 3)));
    };
    for (uint32_t seed = 0; seed < 600; ++seed) {
        uintptr_t before = 0;
        auto after = [&](World &w) { DigestAndRestore(w, 0x58, before); };
        Compare(ctx, "vf56_22", seed,
                [&](World &w) {
                    before = GetPtr(w, 0x58);
                    uint64_t r = uint8_t(orig(w.primary + 0x38, w.arg));
                    after(w);
                    return r;
                },
                [&](World &w) {
                    before = GetPtr(w, 0x58);
                    uint64_t r = As<ITS2>(w.primary + 0x38)->vf56_22(w.arg);
                    after(w);
                    return r;
                },
                prep);
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_23, 0x1f0ad0)
{
    auto orig = ctx.Original<PtrArgRet32>(0x1f0ad0);
    auto prep = [](World &w, uint32_t seed) {
        std::mt19937 r(seed * 40503u + 17);
        uint32_t off = r() % 8;
        uint32_t len = r() % 40;
        if (r() % 8 == 0) len = 14 + r() % 3;
        w.text[off + len] = 0;
        if (r() % 6 == 0) w.text[off] = 0; // an empty string: nothing is cached
        gTextOff = off;
        switch (r() % 4) {
        case 0: gRetS12 = 0; break;
        case 1: gRetS12 = r() % 10; break;
        case 2: gRetS12 = 10 + r() % 40; break;
        default: gRetS12 = 0xffffffffu; break;
        }
        Put(w.primary, 0x58, uintptr_t(r() % 4 == 0 ? Pick(w, 1 + r() % 2) : nullptr));
    };
    for (uint32_t seed = 0; seed < 800; ++seed) {
        uintptr_t before = 0;
        Compare(ctx, "vf56_23", seed,
                [&](World &w) {
                    before = GetPtr(w, 0x58);
                    uint64_t r = uint8_t(orig(w.primary + 0x38, w.arg));
                    DigestAndRestore(w, 0x58, before);
                    return r;
                },
                [&](World &w) {
                    before = GetPtr(w, 0x58);
                    uint64_t r = As<ITS2>(w.primary + 0x38)->vf56_23(w.arg);
                    DigestAndRestore(w, 0x58, before);
                    return r;
                },
                prep);
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_25_26, 0x1f0e70)
{
    auto orig25 = ctx.Original<PtrArgRet32>(0x1f0e70);
    auto orig26 = ctx.Original<OneArgRet32>(0x1f0e90);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 2654435761u + 1);
        uint32_t a = PickWord(r);
        // The key comes from the second half of the world's text buffer (random bytes).
        Compare(ctx, "vf56_25", seed,
                [&](World &w) { orig25(w.primary + 0x38, w.text + 50); return uint64_t(0); },
                [&](World &w) { As<ITS2>(w.primary + 0x38)->vf56_25(w.text + 50); return uint64_t(0); });
        // Only the stored byte is compared: the original leaves the argument byte in al, nothing else defined.
        Compare(ctx, "vf56_26", seed,
                [&](World &w) { orig26(w.primary + 0x38, a); return uint64_t(0); },
                [&](World &w) { As<ITS2>(w.primary + 0x38)->vf56_26(a); return uint64_t(0); });
    }
}

DIFF_TEST(TSPersistDBRecord_GetKey_SetKey, 0x1f0a68)
{
    auto origGet = ctx.Original<NoArgPtr>(0x1f0a68);
    auto origSet = ctx.Original<PtrArgRet32>(0x1f0e7e);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "GetKey", seed, [&](World &w) { return RelPtr(origGet(w.primary + 0x3c)); },
                [&](World &w) { return RelPtr(As<IGZ2>(w.primary + 0x3c)->GetKey()); });
        Compare(ctx, "SetKey", seed,
                [&](World &w) { origSet(w.primary + 0x3c, w.text + 50); return uint64_t(0); },
                [&](World &w) { As<IGZ2>(w.primary + 0x3c)->SetKey(w.text + 50); return uint64_t(0); });
    }
}

DIFF_TEST(TSPersistDBRecord_Close_vf60_10_12, 0x1f07e0)
{
    auto origClose = ctx.Original<NoArgRet32>(0x1f07e0);
    auto orig10 = ctx.Original<NoArgRet32>(0x1f0be0);
    auto orig12 = ctx.Original<NoArgRet32>(0x1f0aa0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Compare(ctx, "Close", seed, [&](World &w) { return uint64_t(origClose(w.primary + 0x3c)); },
                [&](World &w) { return uint64_t(As<IGZ2>(w.primary + 0x3c)->Close()); });
        Compare(ctx, "vf60_10", seed, [&](World &w) { return uint64_t(orig10(w.primary + 0x3c)); },
                [&](World &w) { return uint64_t(As<IGZ2>(w.primary + 0x3c)->vf60_10()); });
        Compare(ctx, "vf60_12", seed, [&](World &w) { return uint64_t(orig12(w.primary + 0x3c)); },
                [&](World &w) { return uint64_t(As<IGZ2>(w.primary + 0x3c)->vf60_12()); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf60_16, 0x1f0ea0)
{
    auto orig = ctx.Original<PtrArgRet32>(0x1f0ea0);
    auto prep = [](World &w, uint32_t seed) {
        std::mt19937 r(seed * 40503u + 19);
        Put(w.primary, 0x5c, uintptr_t(Pick(w, r() % 3)));
    };
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 r(seed * 40503u + 23);
        uint32_t pick = r() % 3;
        uintptr_t before = 0;
        Compare(ctx, "vf60_16", seed,
                [&](World &w) {
                    before = GetPtr(w, 0x5c);
                    orig(w.primary + 0x3c, Pick(w, pick));
                    DigestAndRestore(w, 0x5c, before);
                    return uint64_t(0);
                },
                [&](World &w) {
                    before = GetPtr(w, 0x5c);
                    As<IGZ2>(w.primary + 0x3c)->vf60_16(Pick(w, pick));
                    DigestAndRestore(w, 0x5c, before);
                    return uint64_t(0);
                },
                prep);
    }
}

// ==== third batch: constructor, factory, destructor and the thunks into cRZSerializableStream ==========
// These run on a real stream: a heap record (0x64 bytes) with random bytes, a valid buffer (size <=
// capacity, data present when the capacity is not 0), copies of the game's five vtables whose called slots
// are fakes that record their calls (AddRef, GetError, SetError, the deleting destructor, slot 12 of the
// cITSPersistDBRecord vtable, Release of the owners and of the cache and secondary objects, and the output
// and input stream slots Write and Read use). Return values (al only where the original sets al), the
// recorded calls (pointers as offsets or ids), the whole object and the buffer contents must match.
// The ids this harness maps are only ever applied to the fields it sets itself (the owner, cache and
// secondary pointers, and the vtable words), never to the random words, so the worlds always start equal
// (the same-start requirement is met by construction).
#include "ts/cTSPersistDBRecord.h"

namespace {
namespace rec {

using Th0 = uint32_t(__thiscall *)(void *);
using Th1 = uint32_t(__thiscall *)(void *, uint32_t);
using Th1P = uint32_t(__thiscall *)(void *, void *);
using Th2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using Th2P = uint32_t(__thiscall *)(void *, const void *, uint32_t);
using ThQI = uint32_t(__thiscall *)(void *, uint32_t, void **);
using Obj = cTSPersistDBRecordObject;

constexpr size_t kBytes = Obj::kSize;

std::vector<uint32_t> gTrace;
uintptr_t gBase = 0;
uint8_t *gData = nullptr;
uint32_t gNoise = 0;
uint32_t gReadSize = 0;
bool gReadFail = false;
uint32_t gS12Base = 0, gS12Calls = 0;
const void *gFakeVT[5] = {};

struct IdEntry {
    const void *p;
    uint32_t id;
};
std::vector<IdEntry> gIds;

uint32_t IdOf(const void *p)
{
    for (const auto &e : gIds)
        if (e.p == p) return e.id;
    return 0xffff;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(IdOf(self));
}

constexpr size_t kVtOffsets[5] = {0x00, 0x10, 0x20, 0x38, 0x3c};

// A vtable word as the trace sees it: the world's own fake vtable becomes a marker, anything else (the
// game's vtables, which every world shares) is kept.
uint32_t NormVT(int k, uint32_t v)
{
    return v == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gFakeVT[k])) ? 0xfa4e0000u + k : v;
}

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gTrace.push_back(1);
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) - gBase));
    return ++*reinterpret_cast<uint32_t *>(gBase + 4);
}

uint32_t __fastcall FakeGetError(void *self, void *)
{
    Note(18, self);
    return (gNoise & 0xffffff00u) | *reinterpret_cast<uint8_t *>(gBase + 0x30);
}

uint32_t __fastcall FakeDelete(void *self, void *, uint32_t flags)
{
    Note(21, self);
    gTrace.push_back(flags);
    return gNoise;
}

uint32_t __fastcall FakeSetError(void *self, void *, uint32_t flag)
{
    Note(24, self);
    gTrace.push_back(flag & 0xff);
    *reinterpret_cast<uint8_t *>(gBase + 0x30) = static_cast<uint8_t>(flag);
    return gNoise;
}

// cITSPersistDBRecord slot 12: a different small value on every call.
uint32_t __fastcall FakeS12(void *self, void *)
{
    Note(112, self);
    return gS12Base + 3 * gS12Calls++;
}

// Release of the owners, the cache and the secondary object: records what the object looks like now.
uint32_t __fastcall FakeRelease(void *self, void *)
{
    Note(2, self);
    for (int k = 0; k < 5; ++k) gTrace.push_back(NormVT(k, *reinterpret_cast<uint32_t *>(gBase + kVtOffsets[k])));
    return gNoise;
}

uint32_t __fastcall FakeOutSint32(void *self, void *, uint32_t v)
{
    Note(0x109, self);
    gTrace.push_back(v);
    return gNoise;
}

uint32_t __fastcall FakeOutVoid(void *self, void *, const uint8_t *p, uint32_t n)
{
    Note(0x117, self);
    gTrace.push_back(p == gData);
    gTrace.push_back(n);
    for (uint32_t i = 0; i < n; ++i) gTrace.push_back(p[i]);
    return gNoise;
}

uint32_t __fastcall FakeInSint32(void *self, void *, uint32_t *out)
{
    Note(0x209, self);
    gTrace.push_back(out != nullptr);
    if (!gReadFail) *out = gReadSize;
    return gNoise;
}

uint32_t __fastcall FakeInVoid(void *self, void *, uint8_t *dst, uint32_t n)
{
    Note(0x217, self);
    gTrace.push_back(dst == gData);
    gTrace.push_back(n);
    for (uint32_t i = 0; i < n; ++i) dst[i] = static_cast<uint8_t>(i * 7 + 3);
    return gNoise;
}

struct RWorld {
    uint8_t *obj = nullptr; // 0x64 bytes from operator new (the deleting destructor frees it)
    bool freed = false;
    alignas(8) uint8_t ownerIn[16];
    alignas(8) uint8_t ownerOut[16];
    alignas(8) uint8_t cache[16];
    alignas(8) uint8_t second[16];
    alignas(8) uint8_t outS[16];
    alignas(8) uint8_t inS[16];
    void *vtP[32];
    void *vtIS[24];
    void *vtSS[14];
    void *vtITS[28];
    void *vtIGZ[19];
    void *vtOwner[4];
    void *vtOut[20];
    void *vtIn[20];
    uint32_t noise = 0;

    ~RWorld()
    {
        if (!freed && obj) {
            auto *data = *reinterpret_cast<uint8_t **>(obj + 0x34);
            delete[] data;
            ::operator delete(obj);
        }
    }
};

template <typename T> void RPut(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t RAddr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

struct Shape {
    uint32_t capMax = 70;
    bool withOwners = true;
    bool dataNullWithSize = false;
};

void RBuild(RWorld &w, const difftest::Context &ctx, uint32_t seed, const Shape &shape)
{
    std::mt19937 rng(seed);
    w.obj = static_cast<uint8_t *>(::operator new(kBytes));
    for (size_t i = 0; i < kBytes; ++i) w.obj[i] = static_cast<uint8_t>(rng());
    std::memset(w.ownerIn, 0, sizeof(w.ownerIn));
    std::memset(w.ownerOut, 0, sizeof(w.ownerOut));
    std::memset(w.cache, 0, sizeof(w.cache));
    std::memset(w.second, 0, sizeof(w.second));

    std::memcpy(w.vtP, ctx.image.At(Obj::kVTableRecord), sizeof(w.vtP));
    std::memcpy(w.vtIS, ctx.image.At(Obj::kVTableIS), sizeof(w.vtIS));
    std::memcpy(w.vtSS, ctx.image.At(Obj::kVTableSS), sizeof(w.vtSS));
    std::memcpy(w.vtITS, ctx.image.At(Obj::kVTableITS), sizeof(w.vtITS));
    std::memcpy(w.vtIGZ, ctx.image.At(Obj::kVTableIGZ), sizeof(w.vtIGZ));
    w.vtP[1] = w.vtIS[1] = w.vtSS[1] = w.vtITS[1] = w.vtIGZ[1] = reinterpret_cast<void *>(&FakeAddRef);
    w.vtP[18] = reinterpret_cast<void *>(&FakeGetError);
    w.vtP[21] = reinterpret_cast<void *>(&FakeDelete);
    w.vtP[24] = reinterpret_cast<void *>(&FakeSetError);
    w.vtITS[12] = reinterpret_cast<void *>(&FakeS12);
    std::memset(w.vtOwner, 0, sizeof(w.vtOwner));
    w.vtOwner[2] = reinterpret_cast<void *>(&FakeRelease);
    std::memset(w.vtOut, 0, sizeof(w.vtOut));
    w.vtOut[9] = reinterpret_cast<void *>(&FakeOutSint32);
    w.vtOut[17] = reinterpret_cast<void *>(&FakeOutVoid);
    std::memset(w.vtIn, 0, sizeof(w.vtIn));
    w.vtIn[9] = reinterpret_cast<void *>(&FakeInSint32);
    w.vtIn[17] = reinterpret_cast<void *>(&FakeInVoid);
    for (uint8_t *o : {w.ownerIn, w.ownerOut, w.cache, w.second}) RPut(o, 0, RAddr(w.vtOwner));
    RPut(w.outS, 0, RAddr(w.vtOut));
    RPut(w.inS, 0, RAddr(w.vtIn));

    RPut(w.obj, 0x00, RAddr(w.vtP));
    RPut(w.obj, 0x10, RAddr(w.vtIS));
    RPut(w.obj, 0x20, RAddr(w.vtSS));
    RPut(w.obj, 0x38, RAddr(w.vtITS));
    RPut(w.obj, 0x3c, RAddr(w.vtIGZ));
    RPut(w.obj, 0x04, uint32_t(rng() % 5));
    RPut(w.obj, 0x0c, shape.withOwners && rng() % 3 ? RAddr(w.ownerOut) : uintptr_t(0));
    RPut(w.obj, 0x1c, shape.withOwners && rng() % 3 ? RAddr(w.ownerIn) : uintptr_t(0));
    RPut(w.obj, 0x58, rng() % 3 ? RAddr(w.cache) : uintptr_t(0));
    RPut(w.obj, 0x5c, rng() % 3 ? RAddr(w.second) : uintptr_t(0));
    static const uint8_t errs[] = {0, 0, 1, 2, 0x80, 0xff};
    w.obj[0x30] = errs[rng() % 6];

    uint32_t cap = rng() % 4 == 0 ? 0 : rng() % (shape.capMax + 1);
    uint32_t size = rng() % (cap + 1);
    if (rng() % 8 == 0) size = cap;
    uint32_t pos = rng() % (size + 1);
    RPut(w.obj, 0x24, size);
    RPut(w.obj, 0x28, cap);
    RPut(w.obj, 0x2c, pos);
    uint8_t *data = nullptr;
    if (cap) {
        data = new uint8_t[cap];
        for (uint32_t i = 0; i < cap; ++i) data[i] = static_cast<uint8_t>(rng());
    }
    RPut(w.obj, 0x34, RAddr(data));
    if (shape.dataNullWithSize && rng() % 2) {
        delete[] data;
        RPut(w.obj, 0x34, uintptr_t(0));
        RPut(w.obj, 0x24, uint32_t(rng() % 5));
        RPut(w.obj, 0x28, uint32_t(0));
        RPut(w.obj, 0x2c, uint32_t(0));
    }
    gNoise = w.noise = static_cast<uint32_t>(rng());
}

struct Outcome {
    uint64_t ret = 0;
    std::vector<uint32_t> trace;
    std::vector<uint8_t> object;
    std::vector<uint8_t> data;
    bool hadData = false;
    bool deleted = false;
};

template <typename Call> Outcome Run(RWorld &w, Call call)
{
    gBase = RAddr(w.obj);
    gData = *reinterpret_cast<uint8_t **>(w.obj + 0x34);
    gNoise = w.noise;
    gS12Base = w.noise % 61;
    gS12Calls = 0;
    gFakeVT[0] = w.vtP;
    gFakeVT[1] = w.vtIS;
    gFakeVT[2] = w.vtSS;
    gFakeVT[3] = w.vtITS;
    gFakeVT[4] = w.vtIGZ;
    gIds = {{w.obj, 4},          {w.obj + 0x10, 5}, {w.obj + 0x20, 6}, {w.obj + 0x38, 13}, {w.obj + 0x3c, 14},
            {w.ownerIn, 7},      {w.ownerOut, 8},   {w.outS, 9},       {w.inS, 10},        {w.cache, 11},
            {w.second, 12}};
    gTrace.clear();
    Outcome out;
    out.ret = call(w);
    out.trace = gTrace;
    if (!w.freed) {
        out.object.assign(w.obj, w.obj + kBytes);
        uint8_t *data = *reinterpret_cast<uint8_t **>(w.obj + 0x34);
        out.hadData = data != nullptr;
        std::memset(out.object.data() + 0x34, 0, 4); // the data pointer differs between worlds
        for (int k = 0; k < 5; ++k) {
            uint32_t v;
            std::memcpy(&v, out.object.data() + kVtOffsets[k], 4);
            v = NormVT(k, v);
            std::memcpy(out.object.data() + kVtOffsets[k], &v, 4);
        }
        for (size_t off : {size_t(0x0c), size_t(0x1c), size_t(0x58), size_t(0x5c)}) {
            uintptr_t p;
            std::memcpy(&p, out.object.data() + off, sizeof(p));
            uint32_t id = IdOf(reinterpret_cast<void *>(p));
            std::memcpy(out.object.data() + off, &id, 4);
        }
        if (data) out.data.assign(data, data + *reinterpret_cast<uint32_t *>(w.obj + 0x28));
    } else {
        out.deleted = true;
    }
    return out;
}

template <typename OrigCall, typename OursCall>
void RCompare(difftest::Context &ctx, const char *name, uint32_t seed, const Shape &shape, OrigCall origCall, OursCall oursCall)
{
    RWorld a, b;
    RBuild(a, ctx, seed, shape);
    RBuild(b, ctx, seed, shape);
    Outcome oa = Run(a, origCall);
    Outcome ob = Run(b, oursCall);
    ++ctx.cases;
    if (oa.ret != ob.ret) ctx.Fail("%s seed %u: original returned 0x%llx, ours 0x%llx", name, seed, (unsigned long long)oa.ret, (unsigned long long)ob.ret);
    else if (oa.trace != ob.trace) ctx.Fail("%s seed %u: recorded calls differ (%zu vs %zu words)", name, seed, oa.trace.size(), ob.trace.size());
    else if (oa.deleted != ob.deleted) ctx.Fail("%s seed %u: deletion differs", name, seed);
    else if (oa.object != ob.object) {
        size_t i = 0;
        while (i < oa.object.size() && oa.object[i] == ob.object[i]) ++i;
        ctx.Fail("%s seed %u: object bytes differ first at +0x%zx (original 0x%02x, ours 0x%02x)", name, seed, i, oa.object[i], ob.object[i]);
    } else if (oa.hadData != ob.hadData || oa.data != ob.data) ctx.Fail("%s seed %u: buffer contents differ", name, seed);
    if (oa.deleted) a.freed = true;
    if (ob.deleted) b.freed = true;
}

template <typename T> T *As(RWorld &w, size_t off)
{
    return reinterpret_cast<T *>(w.obj + off);
}

constexpr int kCases = 700;
constexpr size_t kSubOffsets[5] = {0x00, 0x10, 0x20, 0x38, 0x3c};

uint32_t PickCount(std::mt19937 &rng, const RWorld &w)
{
    uint32_t cap = *reinterpret_cast<const uint32_t *>(w.obj + 0x28);
    uint32_t size = *reinterpret_cast<const uint32_t *>(w.obj + 0x24);
    uint32_t n;
    switch (rng() % 8) {
    case 0: n = 0; break;
    case 1: n = cap - size; break;
    case 2: n = cap - size + 1; break;
    case 3: n = cap - size - 1; break;
    case 4: n = cap; break;
    case 5: n = cap + 1; break;
    default: n = rng() % 100; break;
    }
    return n > 1000 ? rng() % 50 : n;
}

} // namespace rec
} // namespace

DIFF_TEST(TSPersistDBRecord_Construct, 0x1f0690)
{
    using namespace rec;
    auto orig = ctx.Original<Th1>(0x1f0690);
    std::mt19937 rng(21);
    for (int i = 0; i < kCases; ++i) {
        uint32_t order = i < 4 ? uint32_t(i) * 0x1000000u + (i & 1) : uint32_t(rng());
        Shape shape;
        shape.capMax = 0;
        // The original's initial bytes are garbage: the constructor must overwrite every field it owns
        // (the padding bytes are left as they were, which the whole-object comparison checks).
        RCompare(ctx, "Construct", i, shape,
                [&](RWorld &w) { return uint64_t(orig(w.obj, order) - RAddr(w.obj)); },
                [&](RWorld &w) { return uint64_t(RAddr(As<Obj>(w, 0)->Construct(order)) - RAddr(w.obj)); });
    }
}

DIFF_TEST(TSPersistDBRecord_Create, 0x1f0900)
{
    using namespace rec;
    auto orig = ctx.Original<void *(__cdecl *)()>(0x1f0900);
    // Every heap block is poisoned on both sides (heap_poison.h, DT): the padding after the byte fields (0x9-0xb,
    // 0x19-0x1b, 0x31-0x33, 0x45-0x47, 0x61-0x63, never stored) holds the pattern on both sides, so every byte is compared.
    auto defined = [](size_t) { return true; };
    for (int i = 0; i < 50; ++i) {
        heappoison::Scope poison(ctx, i % 2 ? 0x5a : 0xcd);
        void *a = orig();
        void *b = Obj::Create();
        ++ctx.cases;
        if (!a || !b) {
            ctx.Fail("Create run %d: original %p, ours %p", i, a, b);
            continue;
        }
        const auto *pa = static_cast<const uint8_t *>(a) - 0x38;
        const auto *pb = static_cast<const uint8_t *>(b) - 0x38;
        for (size_t k = 0; k < kBytes; ++k) {
            if (defined(k) && pa[k] != pb[k]) {
                ctx.Fail("Create run %d: byte +0x%zx differs (original 0x%02x, ours 0x%02x)", i, k, pa[k], pb[k]);
                break;
            }
        }
        ::operator delete(const_cast<uint8_t *>(pa), kBytes);
        ::operator delete(const_cast<uint8_t *>(pb), kBytes);
    }
}

DIFF_TEST(TSPersistDBRecord_DeletingDestructor, 0x1f0740)
{
    using namespace rec;
    auto orig = ctx.Original<Th1>(0x1f0740);
    auto origIS = ctx.Original<Th1>(0x1f0733);
    static const uint32_t flagSet[] = {0, 1, 2, 3, 0x100, 0xfffffffeu, 0xffffffffu};
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        uint32_t flags = flagSet[i % 7];
        RCompare(ctx, "DeletingDestructor", i, shape,
                [&](RWorld &w) {
                    uint32_t r = orig(w.obj, flags);
                    if (flags & 1) w.freed = true;
                    return uint64_t(r == RAddr(w.obj));
                },
                [&](RWorld &w) {
                    Obj *r = As<Obj>(w, 0)->DeletingDestructor(flags);
                    if (flags & 1) w.freed = true;
                    return uint64_t(RAddr(r) == RAddr(w.obj));
                });
        RCompare(ctx, "DeletingDestructorIS", i, shape,
                [&](RWorld &w) {
                    uint32_t r = origIS(w.obj + 0x10, flags);
                    if (flags & 1) w.freed = true;
                    return uint64_t(r == RAddr(w.obj));
                },
                [&](RWorld &w) {
                    Obj *r = As<cTSPersistDBRecordIS2>(w, 0x10)->vf16_21(flags);
                    if (flags & 1) w.freed = true;
                    return uint64_t(RAddr(r) == RAddr(w.obj));
                });
    }
}

DIFF_TEST(TSPersistDBRecord_QueryInterface, 0x1f0c60)
{
    using namespace rec;
    const uint32_t rvas[5] = {0x1f0c60, 0x1f0c9d, 0x1f0ca5, 0x1f0cad, 0x1f0cb5};
    ThQI origs[5];
    for (int k = 0; k < 5; ++k) origs[k] = ctx.Original<ThQI>(rvas[k]);
    // Subobject offsets of the thunks: the primary, then +0x10, +0x20, +0x38, +0x3c.
    const uint32_t riids[] = {Obj::kIdITS,  Obj::kIdIGZ,  Obj::kIdITS - 1,  Obj::kIdIGZ + 1, 0x853d26deu, 0xe4fda3d4u,
                              1u,           0xc51b11cbu,  0x051b11bdu,      0u};
    std::mt19937 rng(22);
    for (int i = 0; i < kCases; ++i) {
        uint32_t riid = i < 10 ? riids[i] : i % 3 == 0 ? riids[rng() % 10] : uint32_t(rng());
        Shape shape;
        for (int k = 0; k < 5; ++k) {
            uint32_t outA[2] = {0xdeadbeefu, 0x5a5a5a5au}, outB[2] = {0xdeadbeefu, 0x5a5a5a5au};
            uint32_t markA = 0, markB = 0;
            auto mark = [](const uint32_t *o, const RWorld &w) {
                return o[0] == 0xdeadbeefu ? 1u : o[0] == 0 ? 2u : static_cast<uint32_t>(o[0] - RAddr(w.obj));
            };
            RCompare(ctx, "QueryInterface", i, shape,
                    [&](RWorld &w) {
                        uint32_t r = origs[k](w.obj + kSubOffsets[k], riid, reinterpret_cast<void **>(outA));
                        markA = mark(outA, w);
                        return uint64_t(r & 0xff);
                    },
                    [&](RWorld &w) {
                        void **out = reinterpret_cast<void **>(outB);
                        bool r = false;
                        switch (k) {
                        case 0: r = As<Obj>(w, 0)->QueryInterface(riid, out); break;
                        case 1: r = As<cTSPersistDBRecordIS2>(w, 0x10)->QueryInterface(riid, out); break;
                        case 2: r = As<cTSPersistDBRecordSS2>(w, 0x20)->QueryInterface(riid, out); break;
                        case 3: r = As<cTSPersistDBRecordITS3>(w, 0x38)->QueryInterface(riid, out); break;
                        default: r = As<cTSPersistDBRecordIGZ3>(w, 0x3c)->QueryInterface(riid, out); break;
                        }
                        markB = mark(outB, w);
                        return uint64_t(r);
                    });
            ++ctx.cases;
            if (markA != markB || outA[1] != outB[1]) ctx.Fail("QueryInterface thunk %d riid 0x%08x: out original 0x%x, ours 0x%x", k, riid, markA, markB);
        }
    }
}

DIFF_TEST(TSPersistDBRecord_Release, 0x1f0dc0)
{
    using namespace rec;
    const uint32_t rvas[5] = {0x1f0dc0, 0x1f0dc5, 0x1f0dcd, 0x1f0dd5, 0x1f0ddd};
    Th0 origs[5];
    for (int k = 0; k < 5; ++k) origs[k] = ctx.Original<Th0>(rvas[k]);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        auto prep = [i](RWorld &w) {
            static const uint32_t counts[] = {0, 1, 2, 3, 0xffffffffu, 0x80000000u};
            if (i % 2 == 0) RPut(w.obj, 4, counts[(i / 2) % 6]);
        };
        for (int k = 0; k < 5; ++k) {
            RCompare(ctx, "Release", i, shape,
                    [&](RWorld &w) {
                        prep(w);
                        return uint64_t(origs[k](w.obj + kSubOffsets[k]));
                    },
                    [&](RWorld &w) {
                        prep(w);
                        switch (k) {
                        case 0: return uint64_t(As<Obj>(w, 0)->Release());
                        case 1: return uint64_t(As<cTSPersistDBRecordIS2>(w, 0x10)->Release());
                        case 2: return uint64_t(As<cTSPersistDBRecordSS2>(w, 0x20)->Release());
                        case 3: return uint64_t(As<cTSPersistDBRecordITS3>(w, 0x38)->Release());
                        default: return uint64_t(As<cTSPersistDBRecordIGZ3>(w, 0x3c)->Release());
                        }
                    });
        }
    }
}

DIFF_TEST(TSPersistDBRecord_AddRefShared, 0x1f07f0)
{
    using namespace rec;
    auto orig = ctx.Original<Th0>(0x1f07f0);
    auto origIS = ctx.Original<Th0>(0x1f07f5);
    auto origSS = ctx.Original<Th0>(0x1f07fd);
    auto origSS6 = ctx.Original<Th0>(0x1f0880);
    for (int i = 0; i < 300; ++i) {
        Shape shape;
        RCompare(ctx, "AddRef", i, shape, [&](RWorld &w) { return uint64_t(orig(w.obj)); },
                [&](RWorld &w) { return uint64_t(As<Obj>(w, 0)->AddRef()); });
        RCompare(ctx, "AddRefIS", i, shape, [&](RWorld &w) { return uint64_t(origIS(w.obj + 0x10)); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordIS2>(w, 0x10)->AddRef()); });
        RCompare(ctx, "AddRefSS", i, shape, [&](RWorld &w) { return uint64_t(origSS(w.obj + 0x20)); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordSS2>(w, 0x20)->AddRef()); });
        RCompare(ctx, "vf32_6", i, shape, [&](RWorld &w) { return uint64_t(origSS6(w.obj + 0x20) - RAddr(w.obj)); },
                [&](RWorld &w) { return uint64_t(RAddr(As<cTSPersistDBRecordSS2>(w, 0x20)->vf32_6()) - RAddr(w.obj)); });
    }
}

DIFF_TEST(TSPersistDBRecord_SetVoid, 0x1f0fd0)
{
    using namespace rec;
    auto orig = ctx.Original<Th2P>(0x1f0fd0);
    auto origITS = ctx.Original<Th2P>(0x1f0fd5);
    auto origIGZ = ctx.Original<Th2P>(0x1f0e40);
    std::mt19937 rng(23);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 300;
        std::vector<uint8_t> src(1001);
        for (auto &b : src) b = static_cast<uint8_t>(rng());
        RWorld probe;
        RBuild(probe, ctx, i, shape);
        uint32_t n = PickCount(rng, probe);
        const void *s = i % 11 == 0 ? nullptr : src.data();
        RCompare(ctx, "SetVoid", i, shape, [&](RWorld &w) { return uint64_t(orig(w.obj, s, n) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<Obj>(w, 0)->SetVoid(s, n)); });
        RCompare(ctx, "SetVoidITS", i, shape, [&](RWorld &w) { return uint64_t(origITS(w.obj + 0x38, s, n) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordITS3>(w, 0x38)->vf56_18(s, n)); });
        RCompare(ctx, "SetVoidIGZ", i, shape, [&](RWorld &w) { return uint64_t(origIGZ(w.obj + 0x3c, s, n) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordIGZ3>(w, 0x3c)->vf60_9(s, n)); });
    }
}

DIFF_TEST(TSPersistDBRecord_SetByteOrder, 0x1f0c40)
{
    using namespace rec;
    auto orig = ctx.Original<Th1>(0x1f0c40);
    auto origIS = ctx.Original<Th1>(0x1f0c45);
    auto origSS = ctx.Original<Th1>(0x1f0c4d);
    auto origITS = ctx.Original<Th1>(0x1f0c55);
    std::mt19937 rng(24);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        uint32_t order = i % 5 == 0 ? 0 : i % 5 == 1 ? 1 : uint32_t(rng());
        RCompare(ctx, "SetByteOrder", i, shape, [&](RWorld &w) { return uint64_t(orig(w.obj, order) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<Obj>(w, 0)->SetByteOrder(order)); });
        RCompare(ctx, "SetByteOrderIS", i, shape, [&](RWorld &w) { return uint64_t(origIS(w.obj + 0x10, order) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordIS2>(w, 0x10)->vf16_22(order)); });
        RCompare(ctx, "SetByteOrderSS", i, shape, [&](RWorld &w) { return uint64_t(origSS(w.obj + 0x20, order) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordSS2>(w, 0x20)->vf32_8(order)); });
        RCompare(ctx, "SetByteOrderITS", i, shape, [&](RWorld &w) { return uint64_t(origITS(w.obj + 0x38, order) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordITS3>(w, 0x38)->vf56_7(order)); });
    }
}

DIFF_TEST(TSPersistDBRecord_Reset, 0x1f1010)
{
    using namespace rec;
    auto orig = ctx.Original<Th0>(0x1f1010);
    auto origIS = ctx.Original<Th0>(0x1f1015);
    auto origSS = ctx.Original<Th0>(0x1f101d);
    auto origITS = ctx.Original<Th0>(0x1f1025);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        RCompare(ctx, "Reset", i, shape, [&](RWorld &w) { return uint64_t(orig(w.obj) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<Obj>(w, 0)->Reset()); });
        RCompare(ctx, "ResetIS", i, shape, [&](RWorld &w) { return uint64_t(origIS(w.obj + 0x10) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordIS2>(w, 0x10)->vf16_23()); });
        RCompare(ctx, "ResetSS", i, shape, [&](RWorld &w) { return uint64_t(origSS(w.obj + 0x20) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordSS2>(w, 0x20)->vf32_9()); });
        RCompare(ctx, "ResetITS", i, shape, [&](RWorld &w) { return uint64_t(origITS(w.obj + 0x38) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordITS3>(w, 0x38)->vf56_8()); });
    }
}

DIFF_TEST(TSPersistDBRecord_GetVoid, 0x1f0c10)
{
    using namespace rec;
    auto orig = ctx.Original<Th2P>(0x1f0c10);
    auto origIGZ = ctx.Original<Th2P>(0x1f0a50);
    std::mt19937 rng(25);
    for (int i = 0; i < kCases; ++i) {
        Shape shape; // position never passes the size here: the copy would read outside the buffer
        RWorld probe;
        RBuild(probe, ctx, i, shape);
        uint32_t size = *reinterpret_cast<uint32_t *>(probe.obj + 0x24);
        uint32_t pos = *reinterpret_cast<uint32_t *>(probe.obj + 0x2c);
        uint32_t left = size - pos;
        uint32_t n = i % 5 == 0 ? left : i % 5 == 1 ? left + 1 : i % 5 == 2 ? 0 : i % 5 == 3 ? uint32_t(rng() % 120) : left - 1;
        if (n > 128) n = 128;
        for (int which = 0; which < 2; ++which) {
            uint8_t bufA[130], bufB[130];
            std::memset(bufA, 0xA5, sizeof(bufA));
            std::memset(bufB, 0xA5, sizeof(bufB));
            std::vector<uint8_t> gotA, gotB;
            RCompare(ctx, which ? "GetVoidIGZ" : "GetVoid", i, shape,
                    [&](RWorld &w) {
                        uint64_t r = (which ? origIGZ(w.obj + 0x3c, bufA, n) : orig(w.obj + 0x10, bufA, n)) & 0xff;
                        gotA.assign(bufA, bufA + sizeof(bufA));
                        return r;
                    },
                    [&](RWorld &w) {
                        uint64_t r = which ? As<cTSPersistDBRecordIGZ3>(w, 0x3c)->vf60_8(bufB, n)
                                           : As<cTSPersistDBRecordIS2>(w, 0x10)->GetVoid(bufB, n);
                        gotB.assign(bufB, bufB + sizeof(bufB));
                        return r;
                    });
            ++ctx.cases;
            if (gotA != gotB) ctx.Fail("GetVoid thunk %d seed %d: destination bytes differ", which, i);
        }
    }
}

DIFF_TEST(TSPersistDBRecord_Write, 0x1f1030)
{
    using namespace rec;
    auto orig = ctx.Original<Th1P>(0x1f1030);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.dataNullWithSize = true;
        RCompare(ctx, "Write", i, shape, [&](RWorld &w) { return uint64_t(orig(w.obj + 0x20, w.outS) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordSS2>(w, 0x20)->Write(w.outS)); });
    }
}

DIFF_TEST(TSPersistDBRecord_Read, 0x1f0cc0)
{
    using namespace rec;
    auto orig = ctx.Original<Th1P>(0x1f0cc0);
    std::mt19937 rng(26);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 100;
        RWorld probe;
        RBuild(probe, ctx, i, shape);
        uint32_t cap = *reinterpret_cast<uint32_t *>(probe.obj + 0x28);
        uint32_t size = i % 5 == 0 ? cap : i % 5 == 1 ? cap + 1 : i % 5 == 2 ? 0 : i % 5 == 3 ? cap - 1 : rng() % 200;
        if (cap == 0 && i % 5 == 3) size = 0;
        gReadFail = i % 7 == 0;
        uint32_t scripted = size;
        RCompare(ctx, "Read", i, shape,
                [&](RWorld &w) {
                    gReadSize = scripted;
                    return uint64_t(orig(w.obj + 0x20, w.inS) & 0xff);
                },
                [&](RWorld &w) {
                    gReadSize = scripted;
                    return uint64_t(As<cTSPersistDBRecordSS2>(w, 0x20)->Read(w.inS));
                });
    }
    gReadFail = false;
}

DIFF_TEST(TSPersistDBRecord_vf56_14, 0x1f0fe0)
{
    using namespace rec;
    auto orig = ctx.Original<Th1>(0x1f0fe0);
    std::mt19937 rng(27);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 100;
        RWorld probe;
        RBuild(probe, ctx, i, shape);
        uint32_t cap = *reinterpret_cast<uint32_t *>(probe.obj + 0x28);
        uint32_t base = probe.noise % 61; // the fake slot 12's first result
        static const int delta[] = {-1, 0, 1};
        uint32_t a = i % 3 == 0 ? cap - base + uint32_t(delta[(i / 3) % 3]) : uint32_t(rng() % 200);
        if (a + base > 400) a = 5; // cap 0 with delta -1 would ask for 4 GB
        RCompare(ctx, "vf56_14", i, shape,
                [&](RWorld &w) {
                    orig(w.obj + 0x38, a);
                    return uint64_t(0);
                },
                [&](RWorld &w) {
                    As<cTSPersistDBRecordITS3>(w, 0x38)->vf56_14(a);
                    return uint64_t(0);
                });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_21, 0x1f0820)
{
    using namespace rec;
    auto orig = ctx.Original<Th1>(0x1f0820);
    std::mt19937 rng(28);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 100;
        RWorld probe;
        RBuild(probe, ctx, i, shape);
        uint32_t cap = *reinterpret_cast<uint32_t *>(probe.obj + 0x28);
        uint32_t base = probe.noise % 61;
        static const int delta[] = {-1, 0, 1};
        uint32_t a = i % 3 == 0 ? cap - base + uint32_t(delta[(i / 3) % 3]) : uint32_t(rng() % 200);
        if (a + base > 400) a = 5; // cap 0 with delta -1 would ask for 4 GB
        RCompare(ctx, "vf56_21", i, shape,
                [&](RWorld &w) { return uint64_t(orig(w.obj + 0x38, a) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordITS3>(w, 0x38)->vf56_21(a)); });
    }
}

DIFF_TEST(TSPersistDBRecord_vf56_19, 0x1f08b0)
{
    using namespace rec;
    auto orig = ctx.Original<Th2>(0x1f08b0);
    std::mt19937 rng(29);
    for (int i = 0; i < kCases; ++i) {
        Shape shape;
        shape.capMax = 100;
        RWorld probe;
        RBuild(probe, ctx, i, shape);
        uint32_t cap = *reinterpret_cast<uint32_t *>(probe.obj + 0x28);
        uint32_t size = *reinterpret_cast<uint32_t *>(probe.obj + 0x24);
        uint32_t count = i % 6 == 0 ? 0 : i % 6 == 1 ? cap - size : i % 6 == 2 ? cap - size + 1 : i % 6 == 3 ? cap - size - 1 : uint32_t(rng() % 150);
        if (count > 300) count = 0; // cap == size makes the "one less" case wrap
        uint32_t value = (i % 4 == 0 ? 0u : uint32_t(rng())) | (i % 5 == 0 ? 0xabcdef00u : 0u);
        RCompare(ctx, "vf56_19", i, shape,
                [&](RWorld &w) { return uint64_t(orig(w.obj + 0x38, value, count) & 0xff); },
                [&](RWorld &w) { return uint64_t(As<cTSPersistDBRecordITS3>(w, 0x38)->vf56_19(static_cast<uint8_t>(value), count)); });
    }
}
