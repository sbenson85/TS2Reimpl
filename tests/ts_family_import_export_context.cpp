// Differential tests for cTSFamilyDataImportExportContext (src/ts/cTSFamilyDataImportExportContext.*).
//
// A world is a context built by the original factory (0x470bf0) and then filled through our std containers (same layout):
// the id table, the object list, the two held objects, the reference count and the key. The counted objects are recording
// fakes (slot 0 query, slot 1 AddRef, slot 2 Release; every call appended to the event log, pointers normalised to ids), the
// stream is a scripted fake (slots 8 / 9 read the next scripted word or log the written one, slot 18 answers the error state),
// and the context's own vtable is a copy with slots 1 (AddRef) and 28 (deleting destructor) replaced by recorders for the QI
// and Release tests. Both sides start from worlds built from one seed; compared are the result, the event log, the whole
// state afterwards (table in list order with its bucket count, list, held objects, key, count) and the fakes' reference
// counts. 8-bit answers carry junk in the rest of eax (compared by the low byte).
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts/cTSFamilyDataImportExportContext.h"
#include "ts_services_fixture.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

using Ctx = cTSFamilyDataImportExportContext;
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
void *gOwnVT[29];
void UseOwnVTable(Ctx *c)
{
    std::memcpy(gOwnVT, GameAddress<void>(Ctx::kVTable), sizeof(gOwnVT));
    gOwnVT[1] = reinterpret_cast<void *>(&OwnAddRef);
    gOwnVT[28] = reinterpret_cast<void *>(&OwnDelete);
    c->mVTable = gOwnVT;
}

Ctx *BuildCtx(difftest::Context &ctx, std::mt19937 &rng)
{
    Ctx *c = ctx.Original<Ctx *(__cdecl *)()>(0x470bf0)();
    gCtx = c;
    const unsigned entries = rng() % 9;
    for (unsigned i = 0; i < entries; ++i) {
        const uint32_t id = rng() % 3 == 0 ? uint32_t(rng()) : rng() % 12;
        c->mIdMap[id] = rng() % 4 == 0 ? Ctx::kNoMapping : uint32_t(rng() % 30);
    }
    const unsigned objects = rng() % 5;
    for (unsigned i = 0; i < objects; ++i) c->mObjects.push_back(NeighborDataRef(&gFakes[rng() % 8]));
    if (rng() % 3) c->mSource.Assign(&gFakes[8 + rng() % 4]);
    if (rng() % 3) c->mDestination.Assign(&gFakes[8 + rng() % 4]);
    c->mRefCount = rng() % 4;
    for (auto &w : c->mKey) w = uint32_t(rng());
    return c;
}

void Capture(Blob &b, Ctx *c)
{
    b.insert(b.end(), {0xeeee0000u, uint32_t(reinterpret_cast<uintptr_t>(c->mVTable)), uint32_t(reinterpret_cast<uintptr_t>(c->mSerialVTable)),
                       c->mRefCount, c->mField4C, c->mKey[0], c->mKey[1], c->mKey[2], c->mKey[3],
                       uint32_t(c->mIdMap.size()), uint32_t(c->mIdMap.bucket_count()), uint32_t(c->mObjects.size()),
                       Id(c->mSource.Get()), Id(c->mDestination.Get())});
    for (const auto &e : c->mIdMap) b.insert(b.end(), {e.first, e.second});
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

} // namespace

DIFF_TEST(FamilyContext_Create, 0x470bf0)
{
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetFakes();
            Ctx *c = side == 0 ? ctx.Original<Ctx *(__cdecl *)()>(0x470bf0)() : Ctx::Create();
            gCtx = c;
            Capture(blobs[side], c);
            blobs[side].push_back(uint32_t(c->mIdMap.max_load_factor() * 100));
            // Destroying it frees every block the factory made: the same sizes show the same allocations.
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

DIFF_TEST(FamilyContext_DeletingDestructor, 0x470a60)
{
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
            difftest::FreeRecorder rec;
            Ctx *r = side == 0 ? ctx.Original<Ctx *(__thiscall *)(void *, uint32_t)>(0x470a60)(c, flags) : c->DeletingDestructor(flags);
            rec.Stop();
            blobs[side] = gEv;
            blobs[side].push_back(r == c);
            blobs[side].push_back(rec.Freed(c));
            if (!rec.Freed(c)) { // a freed block's words are the heap's now
                blobs[side].push_back(c->mVTable == GameAddress<void>(Ctx::kVTable));
                blobs[side].push_back(c->mSerialVTable == GameAddress<void>(Ctx::kSerialVTable));
            }
            blobs[side].push_back(uint32_t(rec.Count()));
            size_t sizes[32];
            const unsigned n = unsigned(rec.SortedSizes(sizes, 32));
            for (unsigned i = 0; i < n; ++i) blobs[side].push_back(uint32_t(sizes[i]));
            for (const auto &f : gFakes) blobs[side].push_back(uint32_t(f.refs));
            if (!rec.Freed(c)) ::operator delete(c);
        }
        svcfx::Same(ctx, blobs[0], blobs[1], "DeletingDestructor", seed);
    }
}

DIFF_TEST(FamilyContext_QueryInterface, 0x470f40)
{
    const uint32_t ids[] = {0, 1, 2, 0x6db5bf2e, 0x6db5bf2d, 0x6db5bf2f, 0x456b8f1d, 0x456b8f1c, 0x6db6f3ff, 0x6db6f400, 0xe4fda3d4,
                            0xe4fda3d3, 0xdb5c0c2, 0xffffffff};
    uint32_t seed = 0;
    for (uint32_t id : ids) {
        for (int nullOut = 0; nullOut < 2; ++nullOut, ++seed) {
            Case(ctx, "QueryInterface", seed, [](std::mt19937 &, Ctx *c) { UseOwnVTable(c); },
                 [&](int side, std::mt19937 &, Ctx *c) {
                     void *out = reinterpret_cast<void *>(0x1234);
                     void **target = nullOut ? nullptr : &out;
                     const uint32_t r = Low8(side == 0 ? ctx.Original<Query>(0x470f40)(c, id, target) : c->QueryInterface(id, target));
                     Ev({Id(out)}); // 0x2000 + the offset into the context, or the untouched marker
                     return r;
                 });
        }
    }
}

DIFF_TEST(FamilyContext_QueryInterfaceThunk, 0x470f9d)
{
    const uint32_t ids[] = {1, 0x456b8f1d, 0xe4fda3d4, 0x6db6f3ff, 7};
    uint32_t seed = 0;
    for (uint32_t id : ids) {
        Case(ctx, "QueryInterfaceThunk", seed++, [](std::mt19937 &, Ctx *c) { UseOwnVTable(c); },
             [&](int side, std::mt19937 &, Ctx *c) {
                 void *out = nullptr;
                 auto *face = c->SerialFace();
                 const uint32_t r = Low8(side == 0 ? ctx.Original<Query>(0x470f9d)(face, id, &out) : face->QueryInterface(id, &out));
                 Ev({Id(out)});
                 return r;
             });
    }
}

DIFF_TEST(FamilyContext_Release, 0x471080)
{
    for (uint32_t seed = 0; seed < 24; ++seed) {
        const uint32_t refs = seed % 6 == 5 ? 0xffffffffu : seed % 4;
        Case(ctx, "Release", seed, [&](std::mt19937 &, Ctx *c) { UseOwnVTable(c); c->mRefCount = refs; },
             [&](int side, std::mt19937 &, Ctx *c) {
                 return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x471080)(c) : c->Release();
             });
    }
}

DIFF_TEST(FamilyContext_ReleaseThunk, 0x471097)
{
    for (uint32_t seed = 0; seed < 8; ++seed) {
        const uint32_t refs = seed % 4;
        Case(ctx, "ReleaseThunk", seed, [&](std::mt19937 &, Ctx *c) { UseOwnVTable(c); c->mRefCount = refs; },
             [&](int side, std::mt19937 &, Ctx *c) {
                 auto *face = c->SerialFace();
                 return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x471097)(face) : face->Release();
             });
    }
}

DIFF_TEST(FamilyContext_GetClassId, 0x470d30)
{
    Case(ctx, "GetClassId", 0, NoSetup, [&](int side, std::mt19937 &, Ctx *c) {
        return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x470d30)(c) : c->GetClassId();
    });
}

DIFF_TEST(FamilyContext_GetGZCLSID, 0x470ddd)
{
    Case(ctx, "GetGZCLSID", 0, NoSetup, [&](int side, std::mt19937 &, Ctx *c) {
        auto *face = c->SerialFace();
        return side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x470ddd)(face) : face->GetGZCLSID();
    });
}

// Slots 12 and 27 forward a query to a held object.
static void QueryForwardTest(difftest::Context &ctx, uint32_t rva, bool source)
{
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Case(ctx, source ? "QuerySource" : "QueryDestination", seed,
             [&](std::mt19937 &rng, Ctx *c) {
                 if (seed % 5 == 0) (source ? c->mSource : c->mDestination).Assign(nullptr);
                 for (auto &f : gFakes) {
                     f.queryAnswer = rng() % 2;
                     f.queryOut = rng() % 2 ? &gFakes[12 + rng() % 4] : nullptr;
                 }
             },
             [&](int side, std::mt19937 &rng, Ctx *c) {
                 void *out = reinterpret_cast<void *>(0x77);
                 const uint32_t id = rng();
                 uint32_t r;
                 if (side == 0) r = ctx.Original<Query>(rva)(c, id, &out);
                 else r = source ? c->QuerySource(id, &out) : c->QueryDestination(id, &out);
                 Ev({Id(out)});
                 // The answer is the low byte; with nothing held the original leaves eax as it was, so skip it then.
                 const bool held = (source ? c->mSource : c->mDestination).Get() != nullptr;
                 return held ? Low8(r) : 0u;
             });
    }
}
DIFF_TEST(FamilyContext_QuerySource, 0x470d80) { QueryForwardTest(ctx, 0x470d80, true); }
DIFF_TEST(FamilyContext_QueryDestination, 0x470e00) { QueryForwardTest(ctx, 0x470e00, false); }

DIFF_TEST(FamilyContext_SetSource, 0x4711c0)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "SetSource", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            void *object = seed % 6 == 0 ? static_cast<void *>(c->mSource.Get()) : AnyFake(rng);
            if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(0x4711c0)(c, object);
            else c->SetSource(object);
            return 0u;
        });
    }
}

DIFF_TEST(FamilyContext_SetDestinationFrom, 0x471220)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "SetDestinationFrom", seed,
             [&](std::mt19937 &rng, Ctx *) {
                 for (auto &f : gFakes) {
                     f.queryAnswer = rng() % 2;
                     f.queryOut = rng() % 3 ? &gFakes[12 + rng() % 4] : nullptr;
                 }
             },
             [&](int side, std::mt19937 &rng, Ctx *c) {
                 void *object = AnyFake(rng);
                 if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(0x471220)(c, object);
                 else c->SetDestinationFrom(object);
                 return 0u;
             });
    }
}

DIFF_TEST(FamilyContext_AddObject, 0x470b00)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "AddObject", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            void *object = AnyFake(rng);
            if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(0x470b00)(c, object);
            else c->AddObject(object);
            return 0u;
        });
    }
}

DIFF_TEST(FamilyContext_RemoveObject, 0x471130)
{
    for (uint32_t seed = 0; seed < 80; ++seed) {
        Case(ctx, "RemoveObject", seed,
             [&](std::mt19937 &, Ctx *c) {
                 if (seed % 4 == 0) c->mObjects.clear();
                 c->mObjects.push_back(NeighborDataRef(&gFakes[seed % 8])); // duplicates of one object
                 c->mObjects.push_back(NeighborDataRef(&gFakes[seed % 8]));
             },
             [&](int side, std::mt19937 &rng, Ctx *c) {
                 void *object = seed % 7 == 0 ? static_cast<void *>(&gFakes[15]) : static_cast<void *>(&gFakes[rng() % 8]);
                 if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(0x471130)(c, object);
                 else c->RemoveObject(object);
                 return 0u;
             });
    }
}

DIFF_TEST(FamilyContext_ClearObjects, 0x470bc0)
{
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Case(ctx, "ClearObjects", seed, NoSetup, [&](int side, std::mt19937 &, Ctx *c) {
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(0x470bc0)(c);
            else c->ClearObjects();
            // The emptied list must be a valid empty list again.
            Ev({uint32_t(c->mObjects.size()), uint32_t(c->mObjects.begin() == c->mObjects.end())});
            return 0u;
        });
    }
}

// Slots 19 and 23 append to a caller's vector in the allocator service's memory.
static void CopyTest(difftest::Context &ctx, uint32_t rva, bool objects)
{
    svcfx::AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 80; ++seed) {
        Case(ctx, objects ? "CopyObjects" : "CopyIds", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            auto *out = new ServiceWordVector();
            const unsigned preset = rng() % 7;
            if (seed % 3 == 0) out->reserve(preset + rng() % 6);
            for (unsigned i = 0; i < preset; ++i) out->push_back(0x1000 + i);
            if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(rva)(c, out);
            else if (objects) c->CopyObjects(out);
            else c->CopyIds(out);
            Ev({uint32_t(out->size()), uint32_t(out->capacity())});
            for (uint32_t v : *out) Ev({objects ? Id(reinterpret_cast<void *>(uintptr_t(v))) : v});
            delete out;
            return 0u;
        });
    }
}
DIFF_TEST(FamilyContext_CopyObjects, 0x470d40) { CopyTest(ctx, 0x470d40, true); }
DIFF_TEST(FamilyContext_CopyIds, 0x470da0) { CopyTest(ctx, 0x470da0, false); }

DIFF_TEST(FamilyContext_AddId, 0x470eb0)
{
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Case(ctx, "AddId", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            // Half of the ids are ones the table holds (the first entry), the rest new.
            const uint32_t id = (seed % 2 && !c->mIdMap.empty()) ? c->mIdMap.begin()->first : uint32_t(rng() % 3 ? rng() % 40 : rng());
            if (side == 0) ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x470eb0)(c, id);
            else c->AddId(id);
            return 0u;
        });
    }
}

DIFF_TEST(FamilyContext_SetMappedId, 0x4710a0)
{
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Case(ctx, "SetMappedId", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t id = (seed % 2 && !c->mIdMap.empty()) ? c->mIdMap.begin()->first : uint32_t(rng() % 20);
            const uint32_t mapped = rng() % 5 == 0 ? Ctx::kNoMapping : uint32_t(rng());
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
            return Low8(side == 0 ? ctx.Original<Fn>(0x4710a0)(c, id, mapped) : c->SetMappedId(id, mapped));
        });
    }
}

DIFF_TEST(FamilyContext_GetMappedId, 0x470e20)
{
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Case(ctx, "GetMappedId", seed, NoSetup, [&](int side, std::mt19937 &rng, Ctx *c) {
            const uint32_t id = (seed % 2 && !c->mIdMap.empty()) ? c->mIdMap.begin()->first : uint32_t(rng() % 20);
            uint32_t out = 0x5a5a5a5a;
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t *);
            const uint32_t r = Low8(side == 0 ? ctx.Original<Fn>(0x470e20)(c, id, &out) : c->GetMappedId(id, &out));
            Ev({out});
            return r;
        });
    }
}

DIFF_TEST(FamilyContext_Write, 0x471260)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Case(ctx, "Write", seed, [&](std::mt19937 &, Ctx *) { SetupStream(false, {}, seed % 4 == 0); },
             [&](int side, std::mt19937 &, Ctx *c) {
                 auto *face = c->SerialFace();
                 return Low8(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x471260)(face, &gStream) : face->Write(&gStream));
             });
    }
}

DIFF_TEST(FamilyContext_Read, 0x470fb0)
{
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Case(ctx, "Read", seed,
             [&](std::mt19937 &rng, Ctx *) {
                 // Versions 0 .. 3; counts small; ids from a small range (duplicates, collisions with the table's) or wide.
                 Blob script;
                 const uint32_t version = seed % 4;
                 script.push_back(version);
                 if (version >= 2) {
                     const uint32_t count = seed % 9 == 0 ? 0 : rng() % 6;
                     script.push_back(count);
                     for (uint32_t i = 0; i < count; ++i) {
                         script.push_back(rng() % 3 == 0 ? uint32_t(rng()) : rng() % 12);
                         script.push_back(rng() % 4 == 0 ? Ctx::kNoMapping : uint32_t(rng() % 30));
                     }
                 } else if (version == 1) {
                     script.push_back(rng() % 12);
                 }
                 if (seed % 11 == 0) script.resize(script.size() / 2 + 1); // short scripts: later reads see zeros
                 SetupStream(true, script, seed % 5 == 0);
             },
             [&](int side, std::mt19937 &, Ctx *c) {
                 auto *face = c->SerialFace();
                 difftest::FreeRecorder rec; // the table's nodes and buckets are allocated and freed the same way
                 const uint32_t r = Low8(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x470fb0)(face, &gStream) : face->Read(&gStream));
                 rec.Stop();
                 Ev({uint32_t(rec.Count())});
                 return r;
             });
    }
}
