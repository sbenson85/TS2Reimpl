// Differential tests for cTSFamilyRelationship (src/ts/cTSFamilyRelationship.*): one sim's family ties.
//
// A world is a 0x28-byte object built by the original constructor (so both sides start the same), its tie map filled from a
// seed. Fakes: the object's own vtables where the code under test calls through them (AddRef, the serializable face), a
// recording stream (slots 7 / 9 / 19), a stream source for ReadFrom / WriteTo, and the allocator service for the id vectors.
// A run compares the result, the object's words and tie list, the output vector, and the event log.
//
// Functions that match the original's bytes (GetGZCLSID, GetTie, GetTieType, RemoveSim, RemoveTie, Release) are
// verified by tools/matchcheck.py; the first four are run here as well as a behavioural cross-check.
#include "game_allocator.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "ts/cTSFamilyRelationship.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;
using Obj = cTSFamilyRelationship;

enum : uint32_t {
    evAddRef = 1, evQuery, evSerRead, evSerWrite, evStreamRelease, evRead16, evRead32, evWrite16, evWrite32, evSetMeta, evAlloc, evFree,
};

Blob gEv;
void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }
std::mt19937 gRng;

// ---- the object under test ----
struct World {
    alignas(8) uint8_t raw[sizeof(Obj)];
    Obj *Get() { return reinterpret_cast<Obj *>(raw); }
    void Build(difftest::Context &ctx, uint32_t seed, unsigned maxTies, int maxType, unsigned maxSim)
    {
        std::memset(raw, 0xcd, sizeof(raw));
        ctx.Original<void *(__thiscall *)(void *)>(0x4f1fd0)(raw);
        std::mt19937 r(seed * 7919u + 13);
        const unsigned n = maxTies ? r() % (maxTies + 1) : 0;
        for (unsigned i = 0; i < n; ++i) {
            const int32_t type = int32_t(r() % uint32_t(maxType + 1)) - (r() % 11 == 0 ? 1 : 0);
            Get()->mTies.emplace(type, uint16_t(r() % maxSim));
        }
        Get()->mRefCount = r() % 4;
    }
    void Destroy() { Get()->mTies.~FamilyTieMap(); }
};

void Capture(Blob &b, Obj *o)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(o);
    for (int i = 0; i < 8; ++i) b.push_back(w[i]); // vtables, refcount, key
    b.push_back(uint32_t(o->mTies.size()));
    for (const auto &e : o->mTies) b.insert(b.end(), {uint32_t(e.first), e.second});
}

void Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differ at word %u (sizes %u/%u): %08x vs %08x", what, seed, unsigned(i), unsigned(a.size()), unsigned(b.size()),
             i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
}

// ---- fakes ----
void *gObjVT[16];  // the primary vtable: slot 1 AddRef
void *gSerVT[8];   // the serializable face: slots 3 (Write) and 4 (Read)
uint32_t __fastcall FakeAddRef(Obj *self, void *)
{
    Ev({evAddRef});
    return ++self->mRefCount;
}
uint8_t __fastcall FakeSerWrite(void *, void *, void *stream)
{
    const uint8_t r = gRng() % 3 != 0;
    Ev({evSerWrite, stream ? 1u : 0u, r});
    return r;
}
uint8_t __fastcall FakeSerRead(void *, void *, void *stream)
{
    const uint8_t r = gRng() % 3 != 0;
    Ev({evSerRead, stream ? 1u : 0u, r});
    return r;
}
void SetFakeVTables(Obj *o)
{
    for (auto &s : gObjVT) s = nullptr;
    gObjVT[1] = reinterpret_cast<void *>(&FakeAddRef);
    for (auto &s : gSerVT) s = nullptr;
    gSerVT[3] = reinterpret_cast<void *>(&FakeSerWrite);
    gSerVT[4] = reinterpret_cast<void *>(&FakeSerRead);
    o->mVTable = gObjVT;
    o->mSerial.mVTable = gSerVT;
}

// The stream: GetUint16 / GetUint32 deliver words from a seeded generator (and can fail without writing); SetUint16 / SetUint32 /
// SetMetadata record.
struct Stream {
    void **vt;
    uint32_t reads32 = 0;
    uint32_t maxCount = 5;
    uint32_t failAfter = 0; // reads fail from this ordinal on (0: never)
    uint32_t ordinal = 0;
} gStream;
void *gStreamVT[24];
bool Fails(Stream *s)
{
    const uint32_t n = s->ordinal++;
    return s->failAfter && n >= s->failAfter && gRng() % 3 == 0;
}
uint8_t __fastcall Read16(Stream *s, void *, uint16_t *out)
{
    if (Fails(s)) {
        Ev({evRead16, 0xffffffffu});
        return 0;
    }
    const uint16_t v = uint16_t(gRng() % 3 == 0 ? gRng() : gRng() % 6);
    Ev({evRead16, v});
    *out = v;
    return 1;
}
uint8_t __fastcall Read32(Stream *s, void *, uint32_t *out)
{
    if (Fails(s)) {
        Ev({evRead32, 0xffffffffu});
        return 0;
    }
    const uint32_t n = s->reads32++;
    const uint32_t v = n == 0 ? gRng() : n == 1 ? gRng() % (s->maxCount + 1) : (gRng() % 7 == 0 ? gRng() : gRng() % 6);
    Ev({evRead32, n, v});
    *out = v;
    return 1;
}
uint8_t __fastcall Write16(Stream *, void *, uint32_t v) // the argument slot is a dword; only the low word is the value
{
    Ev({evWrite16, v & 0xffff});
    return 1;
}
uint8_t __fastcall Write32(Stream *, void *, uint32_t v)
{
    Ev({evWrite32, v});
    return 1;
}
void __fastcall SetMeta(Stream *, void *, void *metadata)
{
    uint32_t words[5];
    std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, words[0] ? 1u : 0u, words[1], words[2], words[3], words[4]});
    ::operator delete(metadata, 0x14);
}
void ResetStream(bool reading, uint32_t maxCount, uint32_t failAfter)
{
    for (auto &s : gStreamVT) s = nullptr;
    gStreamVT[7] = reading ? reinterpret_cast<void *>(&Read16) : reinterpret_cast<void *>(&Write16);
    gStreamVT[9] = reading ? reinterpret_cast<void *>(&Read32) : reinterpret_cast<void *>(&Write32);
    gStreamVT[19] = reinterpret_cast<void *>(&SetMeta);
    gStream.vt = gStreamVT;
    gStream.reads32 = 0;
    gStream.maxCount = maxCount;
    gStream.failAfter = failAfter;
    gStream.ordinal = 0;
}

// A ReadFrom / WriteTo source: its slot 0 answers for the stream ids and hands out `stream`; the stream's slot 2 is a release.
struct Source {
    void **vt;
    uint8_t answer;
    void *stream;
    uint32_t askedId;
} gSource;
void *gSourceVT[4];
void *gStreamObjVT[4];
struct StreamObj {
    void **vt;
} gStreamObj;
uint8_t __fastcall SourceQuery(Source *s, void *, uint32_t riid, void **out)
{
    Ev({evQuery, riid});
    if (s->answer) *out = s->stream;
    return s->answer;
}
uint32_t __fastcall StreamObjRelease(StreamObj *, void *)
{
    Ev({evStreamRelease});
    return 0;
}
void ResetSource(uint8_t answer, bool withStream)
{
    gSourceVT[0] = reinterpret_cast<void *>(&SourceQuery);
    gSource.vt = gSourceVT;
    gSource.answer = answer;
    gStreamObjVT[2] = reinterpret_cast<void *>(&StreamObjRelease);
    gStreamObj.vt = gStreamObjVT;
    gSource.stream = withStream ? static_cast<void *>(&gStreamObj) : nullptr;
}

// The allocator service the id vectors use (cache 0x10c95d4, guard 0x10c95d8) and the general allocator behind it.
struct AllocService {
    void **vt;
} gAlloc;
void *gAllocVT[8];
void *__fastcall AllocAlloc(AllocService *, void *, uint32_t bytes)
{
    Ev({evAlloc, bytes});
    return RZMemory::Malloc(bytes);
}
void __fastcall AllocFree(AllocService *, void *, void *block)
{
    Ev({evFree});
    if (block) RZMemory::Free(block);
}
struct Globals {
    difftest::Context &ctx;
    uint32_t savedService, savedGuard;
    bool builtAllocator = false;
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        savedService = *ctx.image.At<uint32_t>(0x10c95d4);
        savedGuard = *ctx.image.At<uint32_t>(0x10c95d8);
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr), builtAllocator = true;
        gAllocVT[3] = reinterpret_cast<void *>(&AllocAlloc);
        gAllocVT[4] = reinterpret_cast<void *>(&AllocFree);
        gAlloc.vt = gAllocVT;
        *ctx.image.At<void *>(0x10c95d4) = &gAlloc;
        *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
    }
    ~Globals()
    {
        *ctx.image.At<uint32_t>(0x10c95d4) = savedService;
        *ctx.image.At<uint32_t>(0x10c95d8) = savedGuard;
        if (builtAllocator) {
            auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
            difftest_alloc::ShutdownAndRelease(ga);
        }
    }
};

using SimIdVector = std::vector<uint16_t, GZServiceAllocator<uint16_t>>;

constexpr uint32_t kSeeds = 200;

} // namespace

DIFF_TEST(FamilyTies_Construct, 0x4f1fd0)
{
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[sizeof(Obj)];
            std::memset(raw, int(0x11 * (seed + 1)), sizeof(raw));
            auto *o = reinterpret_cast<Obj *>(raw);
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0x4f1fd0)(raw) : o->Construct();
            blobs[side].push_back(r == raw ? 1u : 0u);
            uint32_t words[10];
            std::memcpy(words, raw, sizeof(words));
            for (int i = 0; i < 8; ++i) blobs[side].push_back(words[i]); // the head pointer (+0x20) is a heap address
            const auto *head = reinterpret_cast<const uint8_t *>(words[8]);
            const auto *links = reinterpret_cast<const uint32_t *>(head);
            blobs[side].insert(blobs[side].end(), {links[0] == words[8], links[1] == words[8], links[2] == words[8], head[0xc], head[0xd], words[9]});
            o->mTies.~FamilyTieMap();
        }
        Same(ctx, blobs[0], blobs[1], "Construct", seed);
    }
}

DIFF_TEST(FamilyTies_Create, 0x4f2680)
{
    Blob blobs[2];
    for (int side = 0; side < 2; ++side) {
        Obj *o = side == 0 ? ctx.Original<Obj *(__cdecl *)()>(0x4f2680)() : Obj::Create();
        blobs[side].push_back(o ? 1u : 0u);
        if (!o) continue;
        blobs[side].push_back(uint32_t(HeapSize(GetProcessHeap(), 0, o) >= sizeof(Obj)));
        const uint32_t *w = reinterpret_cast<const uint32_t *>(o);
        for (int i = 0; i < 8; ++i) blobs[side].push_back(w[i]);
        blobs[side].push_back(uint32_t(o->mTies.size()));
        blobs[side].push_back(uint32_t(o->mTies.begin() == o->mTies.end()));
        o->mTies.~FamilyTieMap();
        ::operator delete(o, sizeof(Obj));
    }
    Same(ctx, blobs[0], blobs[1], "Create", 0);
}

DIFF_TEST(FamilyTies_QueryInterface, 0x4f2e90)
{
    const uint32_t ids[] = {0x6c870723, 0x6c870722, 0x6c870724, 1, 0, 2, 0x456b8f1d, 0x456b8f1c, 0x456b8f1e, 0xe4fda3d4, 0xe4fda3d3,
                            0xe4fda3d5, 0xffffffff, 0x80000000, 0x051b11bd, 0x8c870733};
    for (uint32_t seed = 0; seed < 4; ++seed) {
        for (uint32_t id : ids) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, seed, 3, 4, 8);
                SetFakeVTables(w.Get());
                gEv.clear();
                void *out = reinterpret_cast<void *>(0xdeadbeef);
                uint8_t r;
                if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x4f2e90)(w.raw, id, &out);
                else r = w.Get()->QueryInterface(id, &out);
                blobs[side].push_back(r);
                blobs[side].push_back(out == reinterpret_cast<void *>(0xdeadbeef) ? 0xdeadbeefu : uint32_t(reinterpret_cast<uint8_t *>(out) - w.raw));
                blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
                Capture(blobs[side], w.Get());
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "QueryInterface", id);
        }
    }
}

// The tie queries and removals (matched against the original's bytes; run here as a behavioural cross-check too).
DIFF_TEST(FamilyTies_GetTie, 0x4f2d50)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (int32_t type = -1; type <= 5; ++type) {
            for (int withOut = 0; withOut < 2; ++withOut) {
                Blob blobs[2];
                for (int side = 0; side < 2; ++side) {
                    World w;
                    w.Build(ctx, seed, 8, 4, 6);
                    uint32_t slot = 0xabcd1234; // the high word must stay
                    auto *out = withOut ? reinterpret_cast<uint16_t *>(&slot) : nullptr;
                    uint8_t r;
                    if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, int32_t, uint16_t *)>(0x4f2d50)(w.raw, type, out);
                    else r = w.Get()->GetTie(type, out);
                    blobs[side].push_back(r);
                    blobs[side].push_back(slot);
                    Capture(blobs[side], w.Get());
                    w.Destroy();
                }
                Same(ctx, blobs[0], blobs[1], "GetTie", seed * 100 + uint32_t(type + 1) * 2 + withOut);
            }
        }
    }
}

DIFF_TEST(FamilyTies_GetTieType, 0x4f2d00)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (uint16_t sim = 0; sim < 8; ++sim) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, seed, 8, 4, 6);
                int32_t type = 0x5a5a5a5a;
                uint8_t r;
                if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, int32_t *)>(0x4f2d00)(w.raw, sim, &type);
                else r = w.Get()->GetTieType(sim, &type);
                blobs[side].push_back(r);
                blobs[side].push_back(uint32_t(type));
                Capture(blobs[side], w.Get());
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "GetTieType", seed * 100 + sim);
        }
    }
}

DIFF_TEST(FamilyTies_RemoveSim, 0x4f37e0)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (uint16_t sim = 0; sim < 8; ++sim) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, seed, 8, 4, 6);
                uint8_t r;
                if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x4f37e0)(w.raw, sim);
                else r = w.Get()->RemoveSim(sim);
                blobs[side].push_back(r);
                Capture(blobs[side], w.Get());
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "RemoveSim", seed * 100 + sim);
        }
    }
}

DIFF_TEST(FamilyTies_RemoveTie, 0x4f3840)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (int32_t type = -1; type <= 5; ++type) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, seed, 8, 4, 6);
                uint8_t r;
                if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, int32_t)>(0x4f3840)(w.raw, type);
                else r = w.Get()->RemoveTie(type);
                blobs[side].push_back(r);
                Capture(blobs[side], w.Get());
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], "RemoveTie", seed * 100 + uint32_t(type + 1));
        }
    }
}

DIFF_TEST(FamilyTies_SetTie, 0x4f3cd0)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (int32_t type = -1; type <= 5; ++type) {
            for (uint16_t sim = 0; sim < 7; sim += 2) {
                Blob blobs[2];
                for (int side = 0; side < 2; ++side) {
                    World w;
                    w.Build(ctx, seed, 8, 4, 6);
                    uint8_t r;
                    if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, int32_t, uint32_t)>(0x4f3cd0)(w.raw, type, sim);
                    else r = w.Get()->SetTie(type, sim);
                    blobs[side].push_back(r);
                    Capture(blobs[side], w.Get());
                    w.Destroy();
                }
                Same(ctx, blobs[0], blobs[1], "SetTie", seed * 1000 + uint32_t(type + 1) * 10 + sim);
            }
        }
    }
}

namespace {
void RunLists(difftest::Context &ctx, uint32_t rva, bool children)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (int nullOut = 0; nullOut < 2; ++nullOut) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                World w;
                w.Build(ctx, seed, 10, 6, 6); // types beyond 4 occur too
                SimIdVector ids;
                for (unsigned i = 0; i < seed % 5; ++i) ids.push_back(uint16_t(0x900 + i));
                if (seed % 7 == 3) ids.reserve(64); // spare capacity: no reallocation
                gEv.clear();
                SimIdVector *out = nullOut ? nullptr : &ids;
                uint8_t r;
                if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, SimIdVector *)>(rva)(w.raw, out);
                else r = children ? w.Get()->GetChildren(out) : w.Get()->GetSiblings(out);
                blobs[side].push_back(r);
                blobs[side].push_back(uint32_t(ids.capacity()));
                for (auto id : ids) blobs[side].push_back(id);
                blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
                Capture(blobs[side], w.Get());
                w.Destroy();
            }
            Same(ctx, blobs[0], blobs[1], children ? "GetChildren" : "GetSiblings", seed * 2 + nullOut);
        }
    }
}
} // namespace

DIFF_TEST(FamilyTies_GetChildren, 0x4f2c90) { RunLists(ctx, 0x4f2c90, true); }
DIFF_TEST(FamilyTies_GetSiblings, 0x4f2e10) { RunLists(ctx, 0x4f2e10, false); }

DIFF_TEST(FamilyTies_Write, 0x4f3dc0)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            World w;
            w.Build(ctx, seed, 8, 4, 6);
            ResetStream(false, 0, 0);
            gEv.clear();
            uint8_t r;
            if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x4f3dc0)(w.raw + 8, &gStream);
            else r = w.Get()->mSerial.Write(reinterpret_cast<ifaces::cIGZOStream *>(&gStream));
            blobs[side].push_back(r);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            Capture(blobs[side], w.Get());
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Write", seed);
    }
}

DIFF_TEST(FamilyTies_Read, 0x4f2f80)
{
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            World w;
            w.Build(ctx, seed, seed % 3 ? 6 : 0, 4, 6); // a non-empty map: ties are added, duplicates kept
            gRng.seed(seed);
            // From the fifth read on a read can fail and leave its value alone. The version, the count and the first tie's
            // reads fill otherwise uninitialised stack words in the original (ours start at 0), so they never fail.
            ResetStream(true, 6, seed % 2 ? 0 : 4);
            gEv.clear();
            uint8_t r;
            if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x4f2f80)(w.raw + 8, &gStream);
            else r = w.Get()->mSerial.Read(reinterpret_cast<ifaces::cIGZIStream *>(&gStream));
            blobs[side].push_back(r);
            blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
            Capture(blobs[side], w.Get());
            w.Destroy();
        }
        Same(ctx, blobs[0], blobs[1], "Read", seed);
    }
}

namespace {
void RunStreamSource(difftest::Context &ctx, uint32_t rva, bool reading)
{
    for (uint32_t seed = 0; seed < 60; ++seed) {
        for (int answer = 0; answer < 2; ++answer) {
            for (int withStream = 0; withStream < 2; ++withStream) {
                Blob blobs[2];
                for (int side = 0; side < 2; ++side) {
                    World w;
                    w.Build(ctx, seed, 3, 4, 6);
                    SetFakeVTables(w.Get());
                    ResetSource(uint8_t(answer), withStream);
                    gRng.seed(seed);
                    gEv.clear();
                    uint8_t r;
                    if (side == 0) r = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(rva)(w.raw, &gSource);
                    else r = reading ? w.Get()->ReadFrom(&gSource) : w.Get()->WriteTo(&gSource);
                    blobs[side].push_back(r);
                    blobs[side].insert(blobs[side].end(), gEv.begin(), gEv.end());
                    Capture(blobs[side], w.Get());
                    w.Destroy();
                }
                Same(ctx, blobs[0], blobs[1], reading ? "ReadFrom" : "WriteTo", seed * 4 + answer * 2 + withStream);
            }
        }
    }
}
} // namespace

DIFF_TEST(FamilyTies_ReadFrom, 0x4f3a90) { RunStreamSource(ctx, 0x4f3a90, true); }
DIFF_TEST(FamilyTies_WriteTo, 0x4f3b90) { RunStreamSource(ctx, 0x4f3b90, false); }
