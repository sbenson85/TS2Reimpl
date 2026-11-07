// Differential tests for cTSNeighborhoodDataImportExportContext (src/ts/cTSNeighborhoodDataImportExportContext.*).
//
// A world is a context built by the original factory (0x473be0, which answers the +8 face) and then filled through our std
// containers (same layout): the two id sets, the three neighbour tables, the three id tables (+0x18, +0x38, +0x60), the pair at
// +0x58, the key, the object list, the held object, the count. The out-vectors the Copy slots fill live in the allocator
// service's memory, so every test runs inside one svcfx::AllocatorScope. The counted objects are recording fakes (slot 0
// query, 1 AddRef, 2 Release; every call is appended to the event log with pointers normalised to ids), the stream a scripted
// fake (slots 8 / 9 read the next scripted word or log the written one, slot 18 answers the error state), and the context's
// own vtable a copy with slot 1 (AddRef) replaced by a recorder. Both sides start from worlds built from one seed; compared
// are the result, the event log, the whole state afterwards (sets in order, tables in list order with their bucket counts, the
// list, the held object, the key, the count) and the fakes' reference counts. 8-bit answers are read as 8-bit.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts/cTSNeighborhoodDataImportExportContext.h"
#include "ts_services_fixture.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Ctx = cTSNeighborhoodDataImportExportContext;
using Hood = NeighborhoodContextHoodFace;
using Fam = NeighborhoodContextFamilyFace;
using Nbr = NeighborhoodContextNeighborFace;
using Ser = NeighborhoodContextSerialFace;
using Blob = std::vector<uint32_t>;

enum : uint32_t { evQuery = 1, evAddRef, evRelease, evRead, evWrite, evError, evOwnAddRef };

Blob gEv;
void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }

// ---- counted-object fakes ----
struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t queryAnswer;
    Fake *queryOut;
};
constexpr unsigned kFakes = 16;
Fake gFakes[kFakes];
void *gFakeVT[4];
Ctx *gCtx = nullptr;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (const auto &f : gFakes)
        if (p == &f) return f.id;
    const auto base = reinterpret_cast<uintptr_t>(gCtx);
    const auto at = reinterpret_cast<uintptr_t>(p);
    if (gCtx && at >= base && at < base + sizeof(Ctx)) return 0x2000 + uint32_t(at - base);
    return 0xdead0000;
}
uint32_t __fastcall FAddRef(Fake *self, void *)
{
    Ev({evAddRef, self->id});
    return uint32_t(++self->refs);
}
uint32_t __fastcall FRelease(Fake *self, void *)
{
    Ev({evRelease, self->id});
    return uint32_t(--self->refs);
}
uint32_t __fastcall FQuery(Fake *self, void *, uint32_t id, void **out)
{
    Ev({evQuery, self->id, id, Id(out)});
    if (self->queryOut) {
        *out = self->queryOut;
        ++self->queryOut->refs;
    }
    return self->queryAnswer & 0xff;
}
void ResetFakes()
{
    gFakeVT[0] = reinterpret_cast<void *>(&FQuery);
    gFakeVT[1] = reinterpret_cast<void *>(&FAddRef);
    gFakeVT[2] = reinterpret_cast<void *>(&FRelease);
    for (unsigned i = 0; i < kFakes; ++i) gFakes[i] = Fake{gFakeVT, 100 + i, 1000, 0, nullptr};
}

// ---- the stream ----
struct Stream {
    void **vt;
    bool reading;
    Blob script;
    size_t pos;
    uint32_t error;
} gStream;
void *gStreamVT[24];
template <uint32_t Slot> uint8_t __fastcall StreamWord(Stream *s, void *, uint32_t arg)
{
    if (s->reading) {
        const uint32_t v = s->pos < s->script.size() ? s->script[s->pos] : 0;
        ++s->pos;
        Ev({evRead, Slot, v});
        *reinterpret_cast<uint32_t *>(arg) = v;
    } else {
        Ev({evWrite, Slot, arg});
    }
    return 1;
}
uint8_t __fastcall StreamError(Stream *s, void *)
{
    Ev({evError});
    return uint8_t(s->error);
}
void SetupStream(bool reading, Blob script, uint32_t error)
{
    for (auto &p : gStreamVT) p = nullptr;
    gStreamVT[8] = reinterpret_cast<void *>(&StreamWord<8>);
    gStreamVT[9] = reinterpret_cast<void *>(&StreamWord<9>);
    gStreamVT[18] = reinterpret_cast<void *>(&StreamError);
    gStream.vt = gStreamVT;
    gStream.reading = reading;
    gStream.script = std::move(script);
    gStream.pos = 0;
    gStream.error = error;
}

// ---- the context ----
uint32_t __fastcall OwnAddRef(Ctx *self, void *)
{
    Ev({evOwnAddRef});
    return ++self->mRefCount;
}
void *gOwnVT[6];
void UseOwnVTable(Ctx *c)
{
    std::memcpy(gOwnVT, GameAddress<void>(Ctx::kVTable), sizeof(gOwnVT));
    gOwnVT[1] = reinterpret_cast<void *>(&OwnAddRef);
    c->mVTable = gOwnVT;
}

int32_t SmallId(std::mt19937 &rng) { return rng() % 5 == 0 ? int32_t(rng()) : int32_t(rng() % 14) - 3; }
uint32_t SmallValue(std::mt19937 &rng) { return rng() % 4 == 0 ? Ctx::kNoValue : uint32_t(rng() % 20); }

void FillMap(NhIdMap &map, std::mt19937 &rng)
{
    for (unsigned i = 0, n = rng() % 8; i < n; ++i) map[uint32_t(SmallId(rng))] = SmallValue(rng);
}

Ctx *BuildCtx(difftest::Context &ctx, std::mt19937 &rng)
{
    void *face = ctx.Original<void *(__cdecl *)()>(0x473be0)();
    Ctx *c = reinterpret_cast<Ctx *>(static_cast<uint8_t *>(face) - 8);
    gCtx = c;
    for (unsigned i = 0, n = rng() % 7; i < n; ++i) c->mResolved.insert(SmallId(rng));
    for (unsigned i = 0, n = rng() % 7; i < n; ++i) c->mPending.insert(SmallId(rng));
    for (NhIdMap *map : {&c->mHoodIds, &c->mAux, &c->mFamilyIds, &c->mMapA, &c->mMapB, &c->mMapC}) FillMap(*map, rng);
    for (unsigned i = 0, n = rng() % 5; i < n; ++i) c->mObjects.push_back(NeighborDataRef(&gFakes[rng() % 8]));
    if (rng() % 3) {
        c->mHeld = &gFakes[8 + rng() % 4];
        ++static_cast<Fake *>(c->mHeld)->refs;
    }
    for (auto &w : c->mKey.words) w = rng();
    c->mPairKey = rng() % 3 == 0 ? Ctx::kNoValue : uint32_t(SmallId(rng));
    c->mPairValue = rng();
    c->mField10C = rng();
    c->mRefCount = rng() % 4;
    return c;
}

void Capture(Blob &b, Ctx *c)
{
    b.insert(b.end(), {0xeeee0000u, uint32_t(reinterpret_cast<uintptr_t>(c->mVTable)), c->mRefCount,
                       uint32_t(reinterpret_cast<uintptr_t>(c->mHoodVTable)), uint32_t(reinterpret_cast<uintptr_t>(c->mFamilyVTable)),
                       uint32_t(reinterpret_cast<uintptr_t>(c->mNeighborVTable)), uint32_t(reinterpret_cast<uintptr_t>(c->mSerialVTable)),
                       c->mPairKey, c->mPairValue, c->mField10C, c->mKey.words[0], c->mKey.words[1], c->mKey.words[2], c->mKey.words[3],
                       uint32_t(c->mResolved.size()), uint32_t(c->mPending.size()), uint32_t(c->mObjects.size()), Id(c->mHeld)});
    for (int32_t id : c->mResolved) b.push_back(uint32_t(id));
    for (int32_t id : c->mPending) b.push_back(uint32_t(id));
    for (const NhIdMap *map : {&c->mHoodIds, &c->mAux, &c->mFamilyIds, &c->mMapA, &c->mMapB, &c->mMapC}) {
        b.insert(b.end(), {0xeeee0001u, uint32_t(map->size()), uint32_t(map->bucket_count())});
        for (const auto &e : *map) b.insert(b.end(), {e.first, e.second});
    }
    for (const auto &e : c->mObjects) b.push_back(Id(e.Get()));
    for (const auto &f : gFakes) b.push_back(uint32_t(f.refs));
}

void Destroy(Ctx *c)
{
    c->mVTable = GameAddress<void>(Ctx::kVTable);
    c->DeletingDestructor(1);
}

// One case on both sides: build from the seed, `setup` adjusts the world, `call` runs the function (side 0 the original).
template <class Setup, class Call>
void Case(difftest::Context &ctx, const char *name, uint32_t seed, Setup setup, Call call)
{
    Blob blobs[2];
    for (int side = 0; side < 2; ++side) {
        ResetFakes();
        std::mt19937 rng(seed);
        Ctx *c = BuildCtx(ctx, rng);
        setup(rng, c);
        gEv.clear();
        const uint32_t r = call(side, rng, c);
        blobs[side] = gEv;
        blobs[side].push_back(r);
        Capture(blobs[side], c);
        gCtx = c;
        Destroy(c);
    }
    svcfx::Same(ctx, blobs[0], blobs[1], name, seed);
}

void NoSetup(std::mt19937 &, Ctx *) {}

void *AnyFake(std::mt19937 &rng) { return rng() % 5 == 0 ? nullptr : &gFakes[rng() % kFakes]; }

// Calls the original (side 0) or the replacement member on `face`.
template <class Face, class R, class... A>
R Run(difftest::Context &ctx, int side, uint32_t rva, Face *face, R (Face::*ours)(A...), A... args)
{
    if (side == 0) return ctx.Original<R(__thiscall *)(Face *, A...)>(rva)(face, args...);
    return (face->*ours)(args...);
}

// An id the sets hold (a third each), else a small one.
int32_t PickId(uint32_t seed, std::mt19937 &rng, Ctx *c)
{
    if (seed % 3 == 0 && !c->mResolved.empty()) return *c->mResolved.begin();
    if (seed % 3 == 1 && !c->mPending.empty()) return *c->mPending.rbegin();
    return SmallId(rng);
}
// A key a table holds (half of the time), else a small one.
uint32_t PickKey(uint32_t seed, std::mt19937 &rng, const NhIdMap &map)
{
    if (seed % 2 && !map.empty()) {
        auto it = map.begin();
        std::advance(it, rng() % map.size());
        return it->first;
    }
    return uint32_t(SmallId(rng));
}

} // namespace

// ---- construction and destruction ----

DIFF_TEST(NeighborhoodContext_Construct, 0x472040)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            auto *c = static_cast<Ctx *>(::operator new(Ctx::kSize));
            std::memset(c, 0xcd, Ctx::kSize);
            Ctx *r = side == 0 ? ctx.Original<Ctx *(__thiscall *)(void *)>(0x472040)(c) : c->Construct();
            gCtx = c;
            Capture(blobs[side], c);
            blobs[side].push_back(r == c);
            blobs[side].push_back(uint32_t(c->mHoodIds.max_load_factor() * 100));
            // Destroying it frees every block the constructor made: the same sizes show the same allocations.
            gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
            difftest::FreeRecorder rec;
            c->DeletingDestructor(1);
            rec.Stop();
            size_t sizes[16];
            const unsigned n = unsigned(rec.SortedSizes(sizes, 16));
            blobs[side].push_back(n);
            for (unsigned i = 0; i < n; ++i) blobs[side].push_back(uint32_t(sizes[i]));
        }
        svcfx::Same(ctx, blobs[0], blobs[1], "Construct", seed);
    }
}

DIFF_TEST(NeighborhoodContext_Create, 0x473be0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            void *face = side == 0 ? ctx.Original<void *(__cdecl *)()>(0x473be0)() : Ctx::Create();
            auto *c = reinterpret_cast<Ctx *>(static_cast<uint8_t *>(face) - 8);
            gCtx = c;
            Capture(blobs[side], c);
            blobs[side].push_back(uint32_t(c->mHoodIds.max_load_factor() * 100));
            gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
            difftest::FreeRecorder rec;
            c->DeletingDestructor(1);
            rec.Stop();
            size_t sizes[16];
            const unsigned n = unsigned(rec.SortedSizes(sizes, 16));
            blobs[side].push_back(n);
            for (unsigned i = 0; i < n; ++i) blobs[side].push_back(uint32_t(sizes[i]));
        }
        svcfx::Same(ctx, blobs[0], blobs[1], "Create", seed);
    }
}

DIFF_TEST(NeighborhoodContext_DeletingDestructor, 0x4728f0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 80; ++seed) {
        const uint32_t flags = seed % 3;
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            std::mt19937 rng(seed);
            Ctx *c = BuildCtx(ctx, rng);
            c->mVTable = nullptr;
            c->mHoodVTable = nullptr;
            c->mFamilyVTable = nullptr;
            c->mNeighborVTable = nullptr;
            c->mSerialVTable = nullptr;
            gEv.clear();
            gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
            difftest::FreeRecorder rec;
            Ctx *r = side == 0 ? ctx.Original<Ctx *(__thiscall *)(void *, uint32_t)>(0x4728f0)(c, flags) : c->DeletingDestructor(flags);
            rec.Stop();
            blobs[side] = gEv;
            blobs[side].push_back(r == c);
            blobs[side].push_back(rec.Freed(c));
            if (!rec.Freed(c)) { // a freed block's words are the heap's now
                blobs[side].push_back(c->mHoodVTable == GameAddress<void>(Ctx::kHoodVTable));
                blobs[side].push_back(c->mFamilyVTable == GameAddress<void>(Ctx::kFamilyVTable));
                blobs[side].push_back(c->mNeighborVTable == GameAddress<void>(Ctx::kNeighborVTable));
                blobs[side].push_back(c->mSerialVTable == GameAddress<void>(Ctx::kSerialVTable));
                blobs[side].push_back(uint32_t(reinterpret_cast<uintptr_t>(c->mVTable)));
                ::operator delete(c);
            }
            blobs[side].push_back(uint32_t(rec.Count()));
            size_t sizes[48];
            const unsigned n = unsigned(rec.SortedSizes(sizes, 48));
            for (unsigned i = 0; i < n; ++i) blobs[side].push_back(uint32_t(sizes[i]));
            for (const auto &f : gFakes) blobs[side].push_back(uint32_t(f.refs));
        }
        svcfx::Same(ctx, blobs[0], blobs[1], "DeletingDestructor", seed);
    }
}

DIFF_TEST(NeighborhoodContext_QueryInterface, 0x477240)
{
    svcfx::AllocatorScope allocator;
    const uint32_t ids[] = {0, 1, 2, 0x6db5bf2e, 0x6db5bf2d, 0x4e9c59c7, 0x4e9c59c8, 0x456b8f1d, 0x456b8f1c, 0xdb5c0c2, 0xdb5c0c1,
                            0xdb5c0c3, 0x6db6f3ff, 0x6db6f3fe, 0xe4fda3d4, 0xe4fda3d3, 0xffffffff};
    uint32_t seed = 0;
    for (uint32_t id : ids) {
        for (int nullOut = 0; nullOut < 2; ++nullOut, ++seed) {
            Case(ctx, "QueryInterface", seed, [](std::mt19937 &, Ctx *c) { UseOwnVTable(c); },
                 [&](int side, std::mt19937 &, Ctx *c) {
                     void *out = reinterpret_cast<void *>(0x1234);
                     void **target = nullOut ? nullptr : &out;
                     using Fn = uint8_t(__thiscall *)(void *, uint32_t, void **);
                     const uint32_t r = side == 0 ? ctx.Original<Fn>(0x477240)(c, id, target) : c->QueryInterface(id, target);
                     Ev({Id(out)}); // 0x2000 + the offset into the context, or the untouched marker
                     return r;
                 });
        }
    }
}

// ---- the +8 face ----

DIFF_TEST(NeighborhoodContext_GetKey, 0x4749f0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 6; ++seed) {
        Case(ctx, "GetKey", seed, NoSetup, [&](int side, std::mt19937 &, Ctx *c) {
            NeighborhoodResourceKey out = {{0xa1, 0xa2, 0xa3, 0xa4}};
            Run(ctx, side, 0x4749f0, c->HoodFace(), &Hood::GetKey, &out);
            Ev({out.words[0], out.words[1], out.words[2], out.words[3]});
            return 0u;
        });
    }
}

DIFF_TEST(NeighborhoodContext_SetKey, 0x479a40)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 6; ++seed) {
        Case(ctx, "SetKey", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            NeighborhoodResourceKey in = {{rng(), rng(), rng(), rng()}};
            return uint32_t(Run(ctx, side, 0x479a40, c->HoodFace(), &Hood::SetKey, static_cast<const NeighborhoodResourceKey *>(&in)));
        });
    }
}

DIFF_TEST(NeighborhoodContext_SetType, 0x479cb0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 6; ++seed) {
        Case(ctx, "SetType", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            return uint32_t(Run(ctx, side, 0x479cb0, c->HoodFace(), &Hood::SetType, uint32_t(rng())));
        });
    }
}

DIFF_TEST(NeighborhoodContext_SetInstance, 0x479a00)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 6; ++seed) {
        Case(ctx, "SetInstance", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            return uint32_t(Run(ctx, side, 0x479a00, c->HoodFace(), &Hood::SetInstance, uint32_t(rng())));
        });
    }
}

DIFF_TEST(NeighborhoodContext_QueryHeld, 0x4741c0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Case(ctx, "QueryHeld", seed,
             [&](std::mt19937 &rng, Ctx *c) {
                 if (seed % 5 == 0) c->mHeld = nullptr;
                 for (auto &f : gFakes) {
                     f.queryAnswer = rng() % 2;
                     f.queryOut = rng() % 2 ? &gFakes[12 + rng() % 4] : nullptr;
                 }
             },
             [&](int side, std::mt19937 &rng, Ctx *c) {
                 void *out = reinterpret_cast<void *>(0x77);
                 const uint32_t id = rng();
                 // With nothing held the answer is 0 (al); the fakes answer 0 or 1.
                 const uint32_t r = Run(ctx, side, 0x4741c0, c->HoodFace(), &Hood::QueryHeld, id, &out);
                 Ev({Id(out)});
                 return r;
             });
    }
}

DIFF_TEST(NeighborhoodContext_SetHeld, 0x479940)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "SetHeld", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            void *object = seed % 6 == 0 ? c->mHeld : AnyFake(rng);
            Run(ctx, side, 0x479940, c->HoodFace(), &Hood::SetHeld, object);
            return 0u;
        });
    }
}

DIFF_TEST(NeighborhoodContext_SetField10C, 0x479990)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 6; ++seed) {
        Case(ctx, "SetField10C", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            Run(ctx, side, 0x479990, c->HoodFace(), &Hood::SetField10C, uint32_t(rng()));
            return 0u;
        });
    }
}

DIFF_TEST(NeighborhoodContext_AddObject, 0x472d30)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "AddObject", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            Run(ctx, side, 0x472d30, c->HoodFace(), &Hood::AddObject, AnyFake(rng));
            return 0u;
        });
    }
}

DIFF_TEST(NeighborhoodContext_RemoveObject, 0x4789c0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 80; ++seed) {
        Case(ctx, "RemoveObject", seed,
             [&](std::mt19937 &, Ctx *c) {
                 if (seed % 4 == 0) c->mObjects.clear();
                 c->mObjects.push_back(NeighborDataRef(&gFakes[seed % 8])); // duplicates of one object
                 c->mObjects.push_back(NeighborDataRef(&gFakes[seed % 8]));
             },
             [&](int side, std::mt19937 &rng, Ctx *c) {
                 void *object = seed % 7 == 0 ? static_cast<void *>(&gFakes[15]) : static_cast<void *>(&gFakes[rng() % 8]);
                 Run(ctx, side, 0x4789c0, c->HoodFace(), &Hood::RemoveObject, object);
                 return 0u;
             });
    }
}

DIFF_TEST(NeighborhoodContext_ClearObjects, 0x4737a0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Case(ctx, "ClearObjects", seed, NoSetup, [&](int side, std::mt19937 &, Ctx *c) {
            Run(ctx, side, 0x4737a0, c->HoodFace(), &Hood::ClearObjects);
            // The emptied list must be a valid empty list again.
            Ev({uint32_t(c->mObjects.size()), uint32_t(c->mObjects.begin() == c->mObjects.end())});
            return 0u;
        });
    }
}

// The slots that append to a caller's vector in the allocator service's memory.
template <class Face, class Get>
static void CopyTest(difftest::Context &ctx, uint32_t rva, void (Face::*ours)(ServiceWordVector *), Get face, bool objects)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Case(ctx, "Copy", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            auto *out = new ServiceWordVector();
            const unsigned preset = rng() % 9;
            if (seed % 3 == 0) out->reserve(preset + rng() % 12);
            for (unsigned i = 0; i < preset; ++i) out->push_back(0x1000 + i);
            Run(ctx, side, rva, face(c), ours, out);
            Ev({uint32_t(out->size()), uint32_t(out->capacity())});
            for (uint32_t v : *out) Ev({objects ? Id(reinterpret_cast<void *>(uintptr_t(v))) : v});
            delete out;
            return 0u;
        });
    }
}
DIFF_TEST(NeighborhoodContext_CopyObjects, 0x474160)
{
    CopyTest(ctx, 0x474160, &Hood::CopyObjects, [](Ctx *c) { return c->HoodFace(); }, true);
}
DIFF_TEST(NeighborhoodContext_Hood_CopyIds, 0x474db0)
{
    CopyTest(ctx, 0x474db0, &Hood::CopyIds, [](Ctx *c) { return c->HoodFace(); }, false);
}
DIFF_TEST(NeighborhoodContext_Family_CopyIds, 0x474210)
{
    CopyTest(ctx, 0x474210, &Fam::CopyIds, [](Ctx *c) { return c->FamilyFace(); }, false);
}
DIFF_TEST(NeighborhoodContext_CopyResolved, 0x474370)
{
    CopyTest(ctx, 0x474370, &Nbr::CopyResolved, [](Ctx *c) { return c->NeighborFace(); }, false);
}
// CopyAll's slot 29 call reaches the game's own slot 29 through the context's vtable (the original, on both sides).
DIFF_TEST(NeighborhoodContext_CopyAll, 0x474310)
{
    CopyTest(ctx, 0x474310, &Nbr::CopyAll, [](Ctx *c) { return c->NeighborFace(); }, false);
}

DIFF_TEST(NeighborhoodContext_GetClassId, 0x473f20)
{
    svcfx::AllocatorScope allocator;
    Case(ctx, "GetClassId", 0, NoSetup,
         [&](int side, std::mt19937 &, Ctx *c) { return Run(ctx, side, 0x473f20, c->HoodFace(), &Hood::GetClassId); });
}

// The three id tables with a mapped id: AddId (adds with no mapping unless present), SetMappedId, GetMappedId.
template <class Face, class Get>
static void AddIdTest(difftest::Context &ctx, uint32_t rva, void (Face::*ours)(uint32_t), Get face, NhIdMap Ctx::*table)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Case(ctx, "AddId", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t id = PickKey(seed, rng, c->*table);
            gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
            difftest::FreeRecorder rec;
            Run(ctx, side, rva, face(c), ours, id);
            rec.Stop();
            Ev({uint32_t(rec.Count())});
            return 0u;
        });
    }
}
template <class Face, class Get>
static void SetMappedTest(difftest::Context &ctx, uint32_t rva, uint8_t (Face::*ours)(uint32_t, uint32_t), Get face, NhIdMap Ctx::*table)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Case(ctx, "SetMappedId", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t id = PickKey(seed, rng, c->*table);
            return uint32_t(Run(ctx, side, rva, face(c), ours, id, uint32_t(seed % 4 == 0 ? Ctx::kNoValue : rng())));
        });
    }
}
template <class Face, class Get>
static void GetMappedTest(difftest::Context &ctx, uint32_t rva, uint8_t (Face::*ours)(uint32_t, uint32_t *), Get face, NhIdMap Ctx::*table)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Case(ctx, "GetMappedId", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t id = PickKey(seed, rng, c->*table);
            uint32_t out = 0x5a5a5a5a;
            const uint32_t r = Run(ctx, side, rva, face(c), ours, id, &out);
            Ev({out});
            return r;
        });
    }
}
static Hood *HoodOf(Ctx *c) { return c->HoodFace(); }
static Fam *FamOf(Ctx *c) { return c->FamilyFace(); }
static Nbr *NbrOf(Ctx *c) { return c->NeighborFace(); }

DIFF_TEST(NeighborhoodContext_Hood_AddId, 0x475ed0) { AddIdTest(ctx, 0x475ed0, &Hood::AddId, HoodOf, &Ctx::mHoodIds); }
DIFF_TEST(NeighborhoodContext_Hood_SetMappedId, 0x478920) { SetMappedTest(ctx, 0x478920, &Hood::SetMappedId, HoodOf, &Ctx::mHoodIds); }
DIFF_TEST(NeighborhoodContext_Hood_GetMappedId, 0x4759c0) { GetMappedTest(ctx, 0x4759c0, &Hood::GetMappedId, HoodOf, &Ctx::mHoodIds); }
DIFF_TEST(NeighborhoodContext_Family_AddId, 0x475e40) { AddIdTest(ctx, 0x475e40, &Fam::AddId, FamOf, &Ctx::mFamilyIds); }
DIFF_TEST(NeighborhoodContext_Family_SetMappedId, 0x4787b0) { SetMappedTest(ctx, 0x4787b0, &Fam::SetMappedId, FamOf, &Ctx::mFamilyIds); }
DIFF_TEST(NeighborhoodContext_Family_GetMappedId, 0x475810) { GetMappedTest(ctx, 0x475810, &Fam::GetMappedId, FamOf, &Ctx::mFamilyIds); }

DIFF_TEST(NeighborhoodContext_SetPairKey, 0x4789b0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 8; ++seed) {
        Case(ctx, "SetPairKey", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            Run(ctx, side, 0x4789b0, c->HoodFace(), &Hood::SetPairKey, uint32_t(rng()), uint32_t(rng()));
            return 0u;
        });
    }
}

DIFF_TEST(NeighborhoodContext_GetPairValue, 0x475a50)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Case(ctx, "GetPairValue", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t key = seed % 2 ? c->mPairKey : uint32_t(SmallId(rng));
            uint32_t out = 0x5a5a5a5a;
            const uint32_t r = Run(ctx, side, 0x475a50, c->HoodFace(), &Hood::GetPairValue, key, &out);
            Ev({out});
            return r;
        });
    }
}

DIFF_TEST(NeighborhoodContext_AddAux, 0x478720)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Case(ctx, "AddAux", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t id = PickKey(seed, rng, c->mAux);
            gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
            difftest::FreeRecorder rec;
            Run(ctx, side, 0x478720, c->HoodFace(), &Hood::AddAux, id, uint32_t(rng()));
            rec.Stop();
            Ev({uint32_t(rec.Count())});
            return 0u;
        });
    }
}

DIFF_TEST(NeighborhoodContext_GetAux, 0x475780)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Case(ctx, "GetAux", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t id = PickKey(seed, rng, c->mAux);
            uint32_t out = 0x5a5a5a5a;
            const uint32_t r = Run(ctx, side, 0x475780, c->HoodFace(), &Hood::GetAux, id, &out);
            Ev({out});
            return r;
        });
    }
}

// ---- the +0xc face ----

DIFF_TEST(NeighborhoodContext_Family_QueryOwner, 0x475210)
{
    svcfx::AllocatorScope allocator;
    const uint32_t ids[] = {1, 0x6db6f3ff, 0xe4fda3d4, 0xdb5c0c2, 0x456b8f1d, 7};
    uint32_t seed = 0;
    for (uint32_t id : ids) {
        Case(ctx, "QueryOwner", seed++, [](std::mt19937 &, Ctx *c) { UseOwnVTable(c); }, [&](int side, std::mt19937 &, Ctx *c) {
            void *out = nullptr;
            const uint32_t r = Run(ctx, side, 0x475210, c->FamilyFace(), &Fam::QueryOwner, id, &out);
            Ev({Id(out)});
            return r;
        });
    }
}

// ---- the +0x10 face ----

DIFF_TEST(NeighborhoodContext_Neighbor_AddListedId, 0x472e00)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "AddListedId", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            // Reaches the game's own family slot 22 through the +0xc vtable (the original, on both sides).
            const uint32_t id = PickKey(seed, rng, c->mFamilyIds);
            Run(ctx, side, 0x472e00, c->NeighborFace(), &Nbr::AddListedId, id);
            return 0u;
        });
    }
}

// The set members: argument is an id; results compared with the sets' sizes and the freed blocks.
template <class Ret>
static void IdTest(difftest::Context &ctx, const char *name, uint32_t rva, Ret (Nbr::*ours)(int32_t))
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Case(ctx, name, seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const int32_t id = PickId(seed, rng, c);
            gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
            difftest::FreeRecorder rec;
            uint32_t r = 0;
            if constexpr (std::is_void_v<Ret>) Run(ctx, side, rva, c->NeighborFace(), ours, id);
            else r = uint32_t(Run(ctx, side, rva, c->NeighborFace(), ours, id));
            rec.Stop();
            Ev({uint32_t(rec.Count())});
            return r;
        });
    }
}
DIFF_TEST(NeighborhoodContext_AddResolved, 0x475fb0) { IdTest(ctx, "AddResolved", 0x475fb0, &Nbr::AddResolved); }
DIFF_TEST(NeighborhoodContext_AddPending, 0x475f60) { IdTest(ctx, "AddPending", 0x475f60, &Nbr::AddPending); }
DIFF_TEST(NeighborhoodContext_RemoveId, 0x478d50) { IdTest(ctx, "RemoveId", 0x478d50, &Nbr::RemoveId); }
DIFF_TEST(NeighborhoodContext_IsResolved, 0x4762f0) { IdTest(ctx, "IsResolved", 0x4762f0, &Nbr::IsResolved); }
DIFF_TEST(NeighborhoodContext_IsKnown, 0x476270) { IdTest(ctx, "IsKnown", 0x476270, &Nbr::IsKnown); }

DIFF_TEST(NeighborhoodContext_SetMapA, 0x478890)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Case(ctx, "SetMapA", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const int32_t id = PickId(seed, rng, c);
            return uint32_t(Run(ctx, side, 0x478890, c->NeighborFace(), &Nbr::SetMapA, id, uint32_t(rng())));
        });
    }
}

DIFF_TEST(NeighborhoodContext_AddMapC, 0x479a80)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Case(ctx, "AddMapC", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            // Known ids that the table already has, known ones it lacks, and unknown ones.
            int32_t id = PickId(seed, rng, c);
            if (seed % 5 == 4 && !c->mMapC.empty()) id = int32_t(c->mMapC.begin()->first);
            return uint32_t(Run(ctx, side, 0x479a80, c->NeighborFace(), &Nbr::AddMapC, id, uint32_t(rng())));
        });
    }
}

DIFF_TEST(NeighborhoodContext_SetMapBForValue, 0x478840)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Case(ctx, "SetMapBForValue", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            uint32_t a = uint32_t(rng() % 25);
            if (seed % 2 && !c->mMapC.empty()) {
                auto it = c->mMapC.begin();
                std::advance(it, rng() % c->mMapC.size());
                a = it->second;
            }
            return uint32_t(Run(ctx, side, 0x478840, c->NeighborFace(), &Nbr::SetMapBForValue, a, uint32_t(rng())));
        });
    }
}

DIFF_TEST(NeighborhoodContext_GetMapC, 0x475220)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Case(ctx, "GetMapC", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            return Run(ctx, side, 0x475220, c->NeighborFace(), &Nbr::GetMapC, PickKey(seed, rng, c->mMapC));
        });
    }
}

// Slots 32 and 36: a table's value into an out word.
static void GetMapTest(difftest::Context &ctx, uint32_t rva, uint8_t (Nbr::*ours)(uint32_t, uint32_t *), NhIdMap Ctx::*table)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Case(ctx, "GetMap", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            uint32_t out = 0x5a5a5a5a;
            const uint32_t r = Run(ctx, side, rva, c->NeighborFace(), ours, PickKey(seed, rng, c->*table), &out);
            Ev({out});
            return r;
        });
    }
}
DIFF_TEST(NeighborhoodContext_GetMapA, 0x475930) { GetMapTest(ctx, 0x475930, &Nbr::GetMapA, &Ctx::mMapA); }
DIFF_TEST(NeighborhoodContext_GetMapB, 0x4758a0) { GetMapTest(ctx, 0x4758a0, &Nbr::GetMapB, &Ctx::mMapB); }

// ---- the serialize face ----

DIFF_TEST(NeighborhoodContext_Write, 0x47b150)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "Write", seed, [&](std::mt19937 &, Ctx *) { SetupStream(false, {}, seed % 4 == 0); },
             [&](int side, std::mt19937 &, Ctx *c) {
                 return uint32_t(Run(ctx, side, 0x47b150, c->SerialFace(), &Ser::Write, static_cast<void *>(&gStream)));
             });
    }
}

DIFF_TEST(NeighborhoodContext_Read, 0x477980)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Case(ctx, "Read", seed,
             [&](std::mt19937 &rng, Ctx *) {
                 // Versions 0 .. 3; counts small; ids from a small range (duplicates) or wide.
                 Blob script;
                 const uint32_t version = seed % 4;
                 script.push_back(version);
                 auto ids = [&] {
                     const uint32_t count = rng() % 6;
                     script.push_back(count);
                     for (uint32_t i = 0; i < count; ++i) script.push_back(uint32_t(SmallId(rng)));
                 };
                 auto pairs = [&](bool signedCount) {
                     // A negative count of the first two tables reads nothing.
                     const uint32_t count = signedCount && rng() % 12 == 0 ? 0xfffffffeu : rng() % 6;
                     script.push_back(count);
                     for (int32_t i = 0; i < int32_t(count); ++i) {
                         script.push_back(uint32_t(SmallId(rng)));
                         script.push_back(SmallValue(rng));
                     }
                 };
                 if (version >= 1) pairs(true);
                 if (version >= 2) {
                     pairs(true);
                     ids();
                     ids();
                     pairs(false);
                     pairs(false);
                     pairs(false);
                 }
                 if (seed % 11 == 0) script.resize(script.size() / 2 + 1); // short scripts: later reads see zeros
                 SetupStream(true, script, seed % 5 == 0);
             },
             [&](int side, std::mt19937 &, Ctx *c) {
                 gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
                 difftest::FreeRecorder rec; // clearing frees the old nodes; inserting an id twice must not allocate and free
                 const uint32_t r = Run(ctx, side, 0x477980, c->SerialFace(), &Ser::Read, static_cast<void *>(&gStream));
                 rec.Stop();
                 Ev({uint32_t(rec.Count())});
                 return r;
             });
    }
}
