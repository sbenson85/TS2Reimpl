// Differential tests for cTSNeighborDataImportExportContext (src/ts/cTSNeighborDataImportExportContext.*).
//
// A world is a context built by the original factory (0x46fb80) and then filled through our std containers (same layout):
// the two id sets, the three tables, the vector of ids (in the allocator service's memory, so every test runs inside one
// svcfx::AllocatorScope), the object list, the held object, the count. The counted objects are recording fakes (slot 0 query,
// 1 AddRef, 2 Release; every call is appended to the event log with pointers normalised to ids), the stream a scripted fake
// (slots 8 / 9 read the next scripted word or log the written one, slot 18 answers the error state), and the context's own
// vtable a copy with slots 1 (AddRef) and 37 (deleting destructor) replaced by recorders. Both sides start from worlds built
// from one seed; compared are the result, the event log, the whole state afterwards (sets in order, tables in list order with
// their bucket counts, the vector's size and capacity, the list, the held object, the count) and the fakes' reference counts.
// 8-bit answers carry junk in the rest of eax (compared by the low byte).
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts/cTSNeighborDataImportExportContext.h"
#include "ts_services_fixture.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Ctx = cTSNeighborDataImportExportContext;
using Blob = std::vector<uint32_t>;

enum : uint32_t { evQuery = 1, evAddRef, evRelease, evRead, evWrite, evError, evOwnAddRef, evOwnDelete };

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
uint32_t gJunk = 0;
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
    return (self->queryAnswer & 0xff) | (gJunk << 8);
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
uint32_t __fastcall StreamError(Stream *s, void *)
{
    Ev({evError});
    return (s->error & 0xff) | (gJunk << 8);
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
void *__fastcall OwnDelete(Ctx *, void *, uint32_t flags)
{
    Ev({evOwnDelete, flags});
    return nullptr;
}
void *gOwnVT[38];
void UseOwnVTable(Ctx *c)
{
    std::memcpy(gOwnVT, GameAddress<void>(Ctx::kVTable), sizeof(gOwnVT));
    gOwnVT[1] = reinterpret_cast<void *>(&OwnAddRef);
    gOwnVT[37] = reinterpret_cast<void *>(&OwnDelete);
    c->mVTable = gOwnVT;
}

int32_t SmallId(std::mt19937 &rng) { return rng() % 5 == 0 ? int32_t(rng()) : int32_t(rng() % 14) - 3; }

Ctx *BuildCtx(difftest::Context &ctx, std::mt19937 &rng)
{
    Ctx *c = ctx.Original<Ctx *(__cdecl *)()>(0x46fb80)();
    gCtx = c;
    for (unsigned i = 0, n = rng() % 7; i < n; ++i) c->mResolved.insert(SmallId(rng));
    for (unsigned i = 0, n = rng() % 7; i < n; ++i) c->mPending.insert(SmallId(rng));
    NeighborIdMap *maps[] = {&c->mMapA, &c->mMapB, &c->mMapC};
    for (auto *map : maps)
        for (unsigned i = 0, n = rng() % 8; i < n; ++i) (*map)[uint32_t(SmallId(rng))] = rng() % 4 == 0 ? Ctx::kNoValue : uint32_t(rng() % 20);
    if (rng() % 2) c->mListedIds.reserve(rng() % 9);
    for (unsigned i = 0, n = rng() % 6; i < n; ++i) c->mListedIds.push_back(uint32_t(SmallId(rng)));
    for (unsigned i = 0, n = rng() % 5; i < n; ++i) c->mObjects.push_back(NeighborDataRef(&gFakes[rng() % 8]));
    if (rng() % 3) c->mHeld.Assign(&gFakes[8 + rng() % 4]);
    c->mRefCount = rng() % 4;
    return c;
}

void Capture(Blob &b, Ctx *c)
{
    b.insert(b.end(), {0xeeee0000u, uint32_t(reinterpret_cast<uintptr_t>(c->mVTable)), uint32_t(reinterpret_cast<uintptr_t>(c->mSerialVTable)),
                       c->mRefCount, c->mField1, c->mField18[0], c->mField18[1], c->mField18[2], c->mField18[3],
                       uint32_t(c->mResolved.size()), uint32_t(c->mPending.size()), uint32_t(c->mObjects.size()), Id(c->mHeld.Get()),
                       uint32_t(c->mListedIds.size()), uint32_t(c->mListedIds.capacity())});
    for (int32_t id : c->mResolved) b.push_back(uint32_t(id));
    for (int32_t id : c->mPending) b.push_back(uint32_t(id));
    for (const NeighborIdMap *map : {&c->mMapA, &c->mMapB, &c->mMapC}) {
        b.insert(b.end(), {0xeeee0001u, uint32_t(map->size()), uint32_t(map->bucket_count())});
        for (const auto &e : *map) b.insert(b.end(), {e.first, e.second});
    }
    for (const auto &e : c->mObjects) b.push_back(Id(e.Get()));
    for (uint32_t v : c->mListedIds) b.push_back(v);
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
        gJunk = seed * 0x9e3779b1u;
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

uint32_t Low8(uint32_t v) { return v & 0xff; }

using Query = uint8_t(__thiscall *)(void *, uint32_t, void **);

// An id the sets hold (half of the time), else a small one.
int32_t PickId(uint32_t seed, std::mt19937 &rng, Ctx *c)
{
    if (seed % 3 == 0 && !c->mResolved.empty()) return *c->mResolved.begin();
    if (seed % 3 == 1 && !c->mPending.empty()) return *c->mPending.rbegin();
    return SmallId(rng);
}
// A key a table holds (half of the time), else a small one.
uint32_t PickKey(uint32_t seed, std::mt19937 &rng, const NeighborIdMap &map)
{
    if (seed % 2 && !map.empty()) return map.begin()->first;
    return uint32_t(SmallId(rng));
}

} // namespace

DIFF_TEST(NeighborContext_Create, 0x46fb80)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            Ctx *c = side == 0 ? ctx.Original<Ctx *(__cdecl *)()>(0x46fb80)() : Ctx::Create();
            gCtx = c;
            Capture(blobs[side], c);
            blobs[side].push_back(uint32_t(c->mMapA.max_load_factor() * 100));
            // Destroying it frees every block the factory made: the same sizes show the same allocations.
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

DIFF_TEST(NeighborContext_DeletingDestructor, 0x46f950)
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
            c->mSerialVTable = nullptr;
            gEv.clear();
            gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
            difftest::FreeRecorder rec;
            Ctx *r = side == 0 ? ctx.Original<Ctx *(__thiscall *)(void *, uint32_t)>(0x46f950)(c, flags) : c->DeletingDestructor(flags);
            rec.Stop();
            blobs[side] = gEv;
            blobs[side].push_back(r == c);
            blobs[side].push_back(rec.Freed(c));
            if (!rec.Freed(c)) { // a freed block's words are the heap's now
                blobs[side].push_back(c->mVTable == GameAddress<void>(Ctx::kVTable));
                blobs[side].push_back(c->mSerialVTable == GameAddress<void>(Ctx::kSerialVTable));
            }
            blobs[side].push_back(uint32_t(rec.Count()));
            size_t sizes[48];
            const unsigned n = unsigned(rec.SortedSizes(sizes, 48));
            for (unsigned i = 0; i < n; ++i) blobs[side].push_back(uint32_t(sizes[i]));
            // The vector's pointers are zeroed (the vector went back to the allocator service).
            if (!rec.Freed(c)) {
                blobs[side].insert(blobs[side].end(), {uint32_t(c->mListedIds.capacity()), uint32_t(c->mListedIds.size())});
                ::operator delete(c);
            }
            for (const auto &f : gFakes) blobs[side].push_back(uint32_t(f.refs));
        }
        svcfx::Same(ctx, blobs[0], blobs[1], "DeletingDestructor", seed);
    }
}

DIFF_TEST(NeighborContext_QueryInterface, 0x470150)
{
    svcfx::AllocatorScope allocator;
    const uint32_t ids[] = {0, 1, 2, 0x6db5bf2e, 0x6db5bf2d, 0x456b8f1d, 0x456b8f1c, 0xdb5c0c2, 0xdb5c0c1, 0xdb5c0c3, 0x6db6f3ff,
                            0xe4fda3d4, 0xe4fda3d3, 0xffffffff};
    uint32_t seed = 0;
    for (uint32_t id : ids) {
        for (int nullOut = 0; nullOut < 2; ++nullOut, ++seed) {
            Case(ctx, "QueryInterface", seed, [](std::mt19937 &, Ctx *c) { UseOwnVTable(c); },
                 [&](int side, std::mt19937 &, Ctx *c) {
                     void *out = reinterpret_cast<void *>(0x1234);
                     void **target = nullOut ? nullptr : &out;
                     const uint32_t r = Low8(side == 0 ? ctx.Original<Query>(0x470150)(c, id, target) : c->QueryInterface(id, target));
                     Ev({Id(out)}); // 0x2000 + the offset into the context, or the untouched marker
                     return r;
                 });
        }
    }
}

DIFF_TEST(NeighborContext_QueryInterfaceThunk, 0x4701ad)
{
    svcfx::AllocatorScope allocator;
    const uint32_t ids[] = {1, 0x456b8f1d, 0xe4fda3d4, 0xdb5c0c2, 7};
    uint32_t seed = 0;
    for (uint32_t id : ids) {
        Case(ctx, "QueryInterfaceThunk", seed++, [](std::mt19937 &, Ctx *c) { UseOwnVTable(c); },
             [&](int side, std::mt19937 &, Ctx *c) {
                 void *out = nullptr;
                 auto *face = c->SerialFace();
                 const uint32_t r = Low8(side == 0 ? ctx.Original<Query>(0x4701ad)(face, id, &out) : face->QueryInterface(id, &out));
                 Ev({Id(out)});
                 return r;
             });
    }
}

DIFF_TEST(NeighborContext_Release, 0x470500)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 24; ++seed) {
        const uint32_t refs = seed % 6 == 5 ? 0xffffffffu : seed % 4;
        Case(ctx, "Release", seed, [&](std::mt19937 &, Ctx *c) { UseOwnVTable(c); c->mRefCount = refs; },
             [&](int side, std::mt19937 &, Ctx *c) {
                 return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x470500)(c) : c->Release();
             });
    }
}

DIFF_TEST(NeighborContext_ReleaseThunk, 0x47051a)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 8; ++seed) {
        const uint32_t refs = seed % 4;
        Case(ctx, "ReleaseThunk", seed, [&](std::mt19937 &, Ctx *c) { UseOwnVTable(c); c->mRefCount = refs; },
             [&](int side, std::mt19937 &, Ctx *c) {
                 auto *face = c->SerialFace();
                 return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x47051a)(face) : face->Release();
             });
    }
}

DIFF_TEST(NeighborContext_GetClassId, 0x46fcf0)
{
    svcfx::AllocatorScope allocator;
    Case(ctx, "GetClassId", 0, NoSetup, [&](int side, std::mt19937 &, Ctx *c) {
        return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x46fcf0)(c) : c->GetClassId();
    });
}

DIFF_TEST(NeighborContext_GetGZCLSID, 0x46fd70)
{
    svcfx::AllocatorScope allocator;
    Case(ctx, "GetGZCLSID", 0, NoSetup, [&](int side, std::mt19937 &, Ctx *c) {
        auto *face = c->SerialFace();
        return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x46fd70)(face) : face->GetGZCLSID();
    });
}

DIFF_TEST(NeighborContext_QueryHeld, 0x46fd40)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Case(ctx, "QueryHeld", seed,
             [&](std::mt19937 &rng, Ctx *c) {
                 if (seed % 5 == 0) c->mHeld.Assign(nullptr);
                 for (auto &f : gFakes) {
                     f.queryAnswer = rng() % 2;
                     f.queryOut = rng() % 2 ? &gFakes[12 + rng() % 4] : nullptr;
                 }
             },
             [&](int side, std::mt19937 &rng, Ctx *c) {
                 void *out = reinterpret_cast<void *>(0x77);
                 const uint32_t id = rng();
                 const uint32_t r = side == 0 ? ctx.Original<Query>(0x46fd40)(c, id, &out) : c->QueryHeld(id, &out);
                 Ev({Id(out)});
                 // With nothing held the original leaves eax as it was, so the answer is skipped then.
                 return c->mHeld.Get() ? Low8(r) : 0u;
             });
    }
}

DIFF_TEST(NeighborContext_SetHeld, 0x4706d0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "SetHeld", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            void *object = seed % 6 == 0 ? static_cast<void *>(c->mHeld.Get()) : AnyFake(rng);
            if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(0x4706d0)(c, object);
            else c->SetHeld(object);
            return 0u;
        });
    }
}

DIFF_TEST(NeighborContext_AddObject, 0x46fa40)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "AddObject", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            void *object = AnyFake(rng);
            if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(0x46fa40)(c, object);
            else c->AddObject(object);
            return 0u;
        });
    }
}

DIFF_TEST(NeighborContext_RemoveObject, 0x470610)
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
                 if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(0x470610)(c, object);
                 else c->RemoveObject(object);
                 return 0u;
             });
    }
}

DIFF_TEST(NeighborContext_ClearObjects, 0x46fb50)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Case(ctx, "ClearObjects", seed, NoSetup, [&](int side, std::mt19937 &, Ctx *c) {
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x46fb50)(c);
            else c->ClearObjects();
            // The emptied list must be a valid empty list again.
            Ev({uint32_t(c->mObjects.size()), uint32_t(c->mObjects.begin() == c->mObjects.end())});
            return 0u;
        });
    }
}

// Slots 19, 23, 29 and 30 append to (23: replace) a caller's vector in the allocator service's memory.
enum class Copy { Objects, Listed, Resolved, All };
static void CopyTest(difftest::Context &ctx, uint32_t rva, Copy what)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        // CopyAll's slot 29 call reaches the game's own vf29 through the context's vtable (the original, on both sides).
        Case(ctx, "Copy", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            auto *out = new ServiceWordVector();
            const unsigned preset = rng() % 9;
            if (seed % 3 == 0) out->reserve(preset + rng() % 12);
            for (unsigned i = 0; i < preset; ++i) out->push_back(0x1000 + i);
            if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(rva)(c, out);
            else if (what == Copy::Objects) c->CopyObjects(out);
            else if (what == Copy::Listed) c->CopyListedIds(out);
            else if (what == Copy::Resolved) c->CopyResolved(out);
            else c->CopyAll(out);
            Ev({uint32_t(out->size()), uint32_t(out->capacity())});
            for (uint32_t v : *out) Ev({what == Copy::Objects ? Id(reinterpret_cast<void *>(uintptr_t(v))) : v});
            delete out;
            return 0u;
        });
    }
}
DIFF_TEST(NeighborContext_CopyObjects, 0x46fd00) { CopyTest(ctx, 0x46fd00, Copy::Objects); }
DIFF_TEST(NeighborContext_CopyListedIds, 0x46fd60) { CopyTest(ctx, 0x46fd60, Copy::Listed); }
DIFF_TEST(NeighborContext_CopyResolved, 0x46fde0) { CopyTest(ctx, 0x46fde0, Copy::Resolved); }
DIFF_TEST(NeighborContext_CopyAll, 0x46fd80) { CopyTest(ctx, 0x46fd80, Copy::All); }

DIFF_TEST(NeighborContext_AddListedId, 0x46fb00)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Case(ctx, "AddListedId", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t id = (seed % 2 && !c->mListedIds.empty()) ? c->mListedIds[rng() % c->mListedIds.size()] : uint32_t(SmallId(rng));
            if (side == 0) ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x46fb00)(c, id);
            else c->AddListedId(id);
            return 0u;
        });
    }
}

// The set members: argument is an id; results compared with the sets' sizes and the freed blocks.
template <class Orig, class Ours>
static void IdTest(difftest::Context &ctx, const char *name, Orig orig, Ours ours, bool hasResult)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Case(ctx, name, seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const int32_t id = PickId(seed, rng, c);
            gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
            difftest::FreeRecorder rec;
            const uint32_t r = side == 0 ? orig(c, id) : ours(c, id);
            rec.Stop();
            Ev({uint32_t(rec.Count())});
            return hasResult ? Low8(r) : 0u;
        });
    }
}
DIFF_TEST(NeighborContext_AddResolved, 0x470030)
{
    IdTest(ctx, "AddResolved", [&](Ctx *c, int32_t id) { ctx.Original<void(__thiscall *)(void *, int32_t)>(0x470030)(c, id); return 0u; },
           [](Ctx *c, int32_t id) { c->AddResolved(id); return 0u; }, false);
}
DIFF_TEST(NeighborContext_AddPending, 0x46ffe0)
{
    IdTest(ctx, "AddPending", [&](Ctx *c, int32_t id) { ctx.Original<void(__thiscall *)(void *, int32_t)>(0x46ffe0)(c, id); return 0u; },
           [](Ctx *c, int32_t id) { c->AddPending(id); return 0u; }, false);
}
DIFF_TEST(NeighborContext_RemoveId, 0x4706a0)
{
    IdTest(ctx, "RemoveId", [&](Ctx *c, int32_t id) { ctx.Original<void(__thiscall *)(void *, int32_t)>(0x4706a0)(c, id); return 0u; },
           [](Ctx *c, int32_t id) { c->RemoveId(id); return 0u; }, false);
}
DIFF_TEST(NeighborContext_IsResolved, 0x470110)
{
    IdTest(ctx, "IsResolved", [&](Ctx *c, int32_t id) { return uint32_t(ctx.Original<uint8_t(__thiscall *)(void *, int32_t)>(0x470110)(c, id)); },
           [](Ctx *c, int32_t id) { return uint32_t(c->IsResolved(id)); }, true);
}
DIFF_TEST(NeighborContext_IsKnown, 0x470090)
{
    IdTest(ctx, "IsKnown", [&](Ctx *c, int32_t id) { return uint32_t(ctx.Original<uint8_t(__thiscall *)(void *, int32_t)>(0x470090)(c, id)); },
           [](Ctx *c, int32_t id) { return uint32_t(c->IsKnown(id)); }, true);
}

DIFF_TEST(NeighborContext_SetMapA, 0x470580)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Case(ctx, "SetMapA", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const int32_t id = PickId(seed, rng, c);
            const uint32_t value = rng();
            using Fn = uint8_t(__thiscall *)(void *, int32_t, uint32_t);
            return Low8(side == 0 ? ctx.Original<Fn>(0x470580)(c, id, value) : c->SetMapA(id, value));
        });
    }
}

DIFF_TEST(NeighborContext_AddMapC, 0x470720)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Case(ctx, "AddMapC", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            // Known ids that the table already has, known ones it lacks, and unknown ones.
            int32_t id = PickId(seed, rng, c);
            if (seed % 5 == 4 && !c->mMapC.empty()) id = int32_t(c->mMapC.begin()->first);
            const uint32_t value = rng();
            using Fn = uint8_t(__thiscall *)(void *, int32_t, uint32_t);
            return Low8(side == 0 ? ctx.Original<Fn>(0x470720)(c, id, value) : c->AddMapC(id, value));
        });
    }
}

DIFF_TEST(NeighborContext_SetMapBForValue, 0x470530)
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
            const uint32_t b = rng();
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
            return Low8(side == 0 ? ctx.Original<Fn>(0x470530)(c, a, b) : c->SetMapBForValue(a, b));
        });
    }
}

DIFF_TEST(NeighborContext_GetMapC, 0x46fe30)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Case(ctx, "GetMapC", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t key = PickKey(seed, rng, c->mMapC);
            return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x46fe30)(c, key) : c->GetMapC(key);
        });
    }
}

// Slots 32 and 36: a table's value into an out word.
static void GetMapTest(difftest::Context &ctx, uint32_t rva, bool first)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Case(ctx, first ? "GetMapA" : "GetMapB", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t key = PickKey(seed, rng, first ? c->mMapA : c->mMapB);
            uint32_t out = 0x5a5a5a5a;
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t *);
            const uint32_t r = Low8(side == 0 ? ctx.Original<Fn>(rva)(c, key, &out) : first ? c->GetMapA(key, &out) : c->GetMapB(key, &out));
            Ev({out});
            return r;
        });
    }
}
DIFF_TEST(NeighborContext_GetMapA, 0x46ff50) { GetMapTest(ctx, 0x46ff50, true); }
DIFF_TEST(NeighborContext_GetMapB, 0x46fec0) { GetMapTest(ctx, 0x46fec0, false); }

DIFF_TEST(NeighborContext_Write, 0x470820)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "Write", seed, [&](std::mt19937 &, Ctx *) { SetupStream(false, {}, seed % 4 == 0); },
             [&](int side, std::mt19937 &, Ctx *c) {
                 auto *face = c->SerialFace();
                 return Low8(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x470820)(face, &gStream) : face->Write(&gStream));
             });
    }
}

DIFF_TEST(NeighborContext_Read, 0x4701c0)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 300; ++seed) {
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
                 auto pairs = [&] {
                     const uint32_t count = rng() % 6;
                     script.push_back(count);
                     for (uint32_t i = 0; i < count; ++i) {
                         script.push_back(uint32_t(SmallId(rng)));
                         script.push_back(rng() % 4 == 0 ? Ctx::kNoValue : uint32_t(rng() % 30));
                     }
                 };
                 if (version >= 1) {
                     ids();
                     ids();
                     pairs();
                     if (version >= 2) {
                         pairs();
                         pairs();
                     }
                 }
                 if (seed % 11 == 0) script.resize(script.size() / 2 + 1); // short scripts: later reads see zeros
                 SetupStream(true, script, seed % 5 == 0);
             },
             [&](int side, std::mt19937 &, Ctx *c) {
                 auto *face = c->SerialFace();
                 gEv.reserve(gEv.size() + 4096); // the fakes log during the call: the log's growth must not be recorded as a free
                 difftest::FreeRecorder rec; // clearing frees the old nodes; inserting an id twice must not allocate and free
                 const uint32_t r = Low8(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x4701c0)(face, &gStream) : face->Read(&gStream));
                 rec.Stop();
                 Ev({uint32_t(rec.Count())});
                 return r;
             });
    }
}
