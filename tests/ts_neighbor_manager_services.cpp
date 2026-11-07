// Differential tests for the service-heavy cTSNeighborManager members (src/ts/cTSNeighborManager.*): UpdateKeyMap (0x4bf4f0),
// RemoveNeighborEffects (0x4be3f0), LoadNeighbors (0x4bdf00), Reload (slot 8, 0x4bd290), RemoveNeighbor (slot 21, 0x4bc0f0),
// RetireNeighbor (slot 22, 0x4be980), DeleteNeighborResources (slot 23, 0x4bed00) and CreateNeighbor (slot 29, 0x4bbd80).
// Fixture: tests/ts_services_fixture.h (svcfx recording fakes).
//
// A world is one manager (a real object built by Construct(), whose vtable is a table of shims: slot 23 and the persist face's slot 6
// are the ones the members call through `this`) populated with neighbour fakes, plus the fakes the members reach: the app singleton
// (cache 0x10cc288) and what it hands out, the persist system (0x10faf6c), the message server (cache 0x10f519c; messages are real
// heap objects, logged by content), the framework (director slot 12). Both sides see the same seeded world; the event log, the
// fakes' reference counts, the allocator-service blocks the fakes handed out (freed or not) and the manager's own state are compared.
//
// Standalone shape (tests/ts_neighbor_manager_shape.h): every world runs three routes (the original, our vtable, the adapter with
// a trap); the shims are patched over copies of the route's own vtables.
#include "ts_services_fixture.h"

#include "rz/RZMemory.h"
#include "simantics/ServiceRefVector.h"
#include "ts_neighbor_manager_shape.h"

#include <random>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "d"}, {1, "d"}, {2, "d"}, {3, "d"}, {4, "dd"}, {9, "d"}, {11, "d"}, {12, "d"}, {14, "d"}, {15, "d"}, {16, "d"}, {17, "d"}, {19, "dd"}, {20, "d"}, {22, "d"}};
using namespace svcfx;
using namespace nbrmgrshape;

enum Fk : unsigned {
    fNb0, fNb1, fNb2, fNb3, fNb4, fNb5,   // neighbours; fNb4 is the one the member works on, fNb5 is what the framework creates
    fTable, fOwnTable,                    // relationship tables (the others', the subject's)
    fApp,                                 // the app singleton
    fX, fY,                               // app slot 29 / 27 objects (fX also answers app slot 24)
    fPersist, fRecord,                    // the persist system, a neighbour's record
    fServer, fFramework,                  // message server, framework
    fPerson, fKind,                       // a neighbour's person data object and its kind object
    fEntries, fEntry,                     // app slot 26 and what it finds
    fFamilies, fFamily,                   // app slot 9 and a family
    fHeld, fMgr                           // person slot 67 object; the manager's own slots (shims)
};
constexpr unsigned fRecords = fX;
constexpr unsigned fCreated = fNb5;

void *gMgrVT[80];
void *gFaceVT[12];
uint32_t gGroup = 0;
bool gVTablesReady = false;

uint32_t RandomWord(Obj *o, uint32_t *) { return o->value[0] = o->value[0] * 1664525u + 1013904223u; }
// Registers a heap block as a range, dropping the ranges of earlier (freed) blocks that its address reuses.
void AddFreshRange(const void *block, size_t size, uint32_t tag)
{
    const uintptr_t lo = reinterpret_cast<uintptr_t>(block), hi = lo + size;
    for (size_t i = gRanges.size(); i-- > 0;)
        if (gRanges[i].lo < hi && lo < gRanges[i].hi) gRanges.erase(gRanges.begin() + i);
    AddRange(block, size, tag);
}
void LogKey(const void *key)
{
    const auto *w = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) Ev(w[i]);
}
uint32_t KeyArg0(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return 0;
}
// A record's slot 3 (GetKey): writes a key from the fake's generator into the buffer and returns the buffer.
uint32_t RecordGetKey(Obj *o, uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(a[0]);
    for (int i = 0; i < 4; ++i) out[i] = RandomWord(o, nullptr);
    return a[0];
}

uint32_t __fastcall MgrSlot23(void *, void *, uint32_t vec, uint32_t unused)
{
    uint32_t a[2] = {vec, unused};
    return detail::Dispatch(&F(fMgr), 23, a, 2);
}
uint32_t __fastcall FaceGroup(void *, void *)
{
    Ev(evNote);
    Ev(6);
    return gGroup;
}
// The manager's slot 23 gets the vector of resource pointers: log its contents.
uint32_t LogResourceVector(Obj *o, uint32_t *a)
{
    const auto *vec = reinterpret_cast<const NeighborIdVector *>(a[0]);
    Ev(uint32_t(vec->size()));
    for (uint32_t element : *vec) Ev(Norm(element));
    return o->value[0];
}

void InstallVTables()
{
    if (gVTablesReady) return;
    gMgrVT[23] = reinterpret_cast<void *>(&MgrSlot23);
    gFaceVT[6] = reinterpret_cast<void *>(&FaceGroup);
    gVTablesReady = true;
}

struct MgrMem {
    alignas(8) uint8_t bytes[sizeof(Mgr)];
    Mgr *Get() { return reinterpret_cast<Mgr *>(bytes); }
};

// The route's object (tests/ts_neighbor_manager_shape.h) with the shims patched over copies of its own vtables.
void BuildMgr(MgrMem &mem)
{
    InstallVTables();
    std::memset(mem.bytes, 0, sizeof(mem.bytes));
    Mgr *m = Construct(mem.bytes);
    ClearPatches();
    gPatch[0][23] = gMgrVT[23];
    gPatch[1][6] = gFaceVT[6];
    Install(mem.bytes);
    gTrapHits = 0;
    AddRange(m, sizeof(Mgr), 0xe0000000);
}

void CaptureMgr(Blob &b, Mgr *m)
{
    for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtCode(f, shape::Word(m, kFaceOffset[f])));
    b.push_back(gTrapHits);
    b.push_back(m->mRefCount);
    b.push_back(m->mUserFileCounter);
    b.push_back(m->mNextNeighborId);
    b.push_back(m->mDirty);
    b.push_back(uint32_t(m->mNeighbors.size()));
    for (auto &entry : m->mNeighbors) {
        b.push_back(uint32_t(entry.first));
        b.push_back(Norm(P(entry.second.Get())));
    }
    b.push_back(uint32_t(m->mByPersonDataId.size()));
    for (auto &entry : m->mByPersonDataId) {
        b.push_back(entry.first);
        b.push_back(Norm(P(entry.second.Get())));
    }
    for (uint32_t w : {m->mResourceKey.type, m->mResourceKey.group, m->mResourceKey.instance, m->mResourceKey.resource})
        b.push_back(w);
}

// Every world on three routes; route 0 compared with routes 1 and 2.
template <class Fn> bool Pair(difftest::Context &ctx, const char *what, uint32_t seed, Fn world)
{
    Blob blobs[kRoutes];
    for (int side = 0; side < kRoutes; ++side) {
        gRoute = side;
        ResetAll();
        std::mt19937 rng(seed * 7919u + 29);
        Globals g;
        MgrMem mem;
        BuildMgr(mem);
        gGroup = 0x1000 + (rng() & 0xfff);
        gFrameworkIndex = fFramework;
        Blob &b = blobs[side];
        const uint32_t r = world(g, mem.Get(), rng, side == 0);
        b.push_back(r);
        b.insert(b.end(), gEv.begin(), gEv.end());
        CaptureMgr(b, mem.Get());
        CaptureFakes(b, kValueKinds);
        CaptureAllocated(b);
        gEv.clear();
        Destroy(mem.bytes);
    }
    gRoute = 0;
    bool same = true;
    for (int route = 1; route < kRoutes; ++route) same &= Same(ctx, blobs[0], blobs[route], what, seed);
    return same;
}
#define ROUTE(Fn, rva, face, name, ...) Call<Fn>(ctx, m, rva, face, NBRMGR_ADAPTER(name), __VA_ARGS__)
#define ROUTE_NA(Fn, rva, face, name, ...) Call<Fn>(ctx, m, rva, face, NBRMGR_NO_ADAPTER, __VA_ARGS__)

// ---- shared pieces of the worlds ----

// A 16-bit word with junk in the upper half, sometimes negative as a 16-bit value.
uint32_t WordValue(std::mt19937 &rng, uint32_t id)
{
    const uint32_t low = rng() % 6 == 0 ? (0xfff0u + id % 16) : (id % 40 + 1);
    return low | (rng() % 3 ? 0u : (rng() & 0xffff0000u));
}
// An 8-bit answer (AL) with junk above.
uint32_t ByteValue(std::mt19937 &rng, bool yes) { return (yes ? 1 + rng() % 0xff : 0) | (rng() & 0xffffff00u); }

uint32_t KindValue(Obj *o, uint32_t *) { return o->value[0]; }

// The neighbour objects: the slots the removal code and the person-data tests reach.
void SetupNeighborObj(Obj &o, std::mt19937 &rng, uint32_t id)
{
    o.Set(7, 0);
    o.Set(8, 0, WordValue(rng, id));
    o.Set(10, 0, rng() % 5 ? &F(fPerson) : nullptr);
    o.Set(12, 0, &F(fTable));
    o.Set(15, 0, rng() % 6);
    o.Set(39, 1);
    o.Set(48, 0, &F(fRecord));
}
void SetupRemovalFakes(Globals &g, std::mt19937 &rng)
{
    F(fTable).Set(7, "d");
    F(fTable).Set(8, "d");
    F(fOwnTable).Set(8, "d");
    F(fRecord).value[0] = rng();
    F(fRecord).Set(3, "p", 0u, &RecordGetKey);
    F(fRecord).Set(4, 1, 0u, &KeyArg0);
    F(fPersist).Set(17, "p", 0u, &KeyArg0);
    F(fApp).Set(29, 0, rng() % 3 ? &F(fX) : nullptr);
    F(fApp).Set(27, 0, rng() % 3 ? &F(fY) : nullptr);
    F(fX).Set(4, "d");
    F(fY).Set(5, "d");
    UseAsMessageServer(F(fServer));
    F(fPerson).value[0] = rng();
    F(fPerson).Set(4, 0, rng() % 4 ? &F(fKind) : nullptr);
    F(fKind).value[0] = rng() % 3 == 0 ? 2 : rng() % 3 == 0 ? 0x11 : rng() % 20;
    F(fKind).Set(3, "d", 0u, &KindValue);
    g.Set(kAppCell, &F(fApp));
    g.Set(kPersistCell, &F(fPersist));
    g.Set(kMessageServerCell, &F(fServer));
}
// Enters the neighbour fakes first .. first + count - 1 into the map under ids (the first key may be the subject's word).
void FillMap(Mgr *m, std::mt19937 &rng, unsigned first, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        Obj &o = F(first + i);
        NeighborDataRef &slot = m->mNeighbors[int32_t(i * 3 + 1 + rng() % 3)];
        if (rng() % 8 != 0) slot.Assign(&o);
    }
}
void FillKeyMap(Mgr *m, std::mt19937 &rng)
{
    for (unsigned n = rng() % 4; n; --n) m->mByPersonDataId[rng() % 6].Assign(&F(fNb0 + rng() % 5));
}

// Persist slot 36 (vector, group, type): fills the vector with some keys; the return value is their count.
uint32_t PersistKeys(Obj *o, uint32_t *a)
{
    Ev(a[1]);
    Ev(a[2]);
    auto *vec = reinterpret_cast<void **>(a[0]);
    const unsigned n = RandomWord(o, nullptr) % 5;
    if (!n) return 0;
    const unsigned total = n + (RandomWord(o, nullptr) >> 8) % 3;
    auto *block = static_cast<uint32_t(*)[4]>(RZMemory::Malloc(total * 16));
    gAllocated.push_back(block);
    AddFreshRange(block, total * 16, 0xd0000000);
    for (unsigned i = 0; i < total; ++i) {
        block[i][0] = 0xaace2efb;
        block[i][1] = a[1];
        block[i][2] = RandomWord(o, nullptr) % 9;
        block[i][3] = 0;
    }
    vec[0] = block;
    vec[1] = block + n;
    vec[2] = block + total;
    return n;
}
// Persist slot 5 (key, interface, out, 0, 0): "creates" one of the first three neighbours.
uint32_t PersistCreate(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    const uint32_t r = RandomWord(o, nullptr) >> 8;
    Obj &made = F(fNb0 + r % 3);
    if (r % 5 != 0) {
        ++made.refs;
        *reinterpret_cast<void **>(a[2]) = &made;
        return (r & 0xffffff00u) | 1;
    }
    if (r % 7 == 0) {
        ++made.refs;
        *reinterpret_cast<void **>(a[2]) = &made;
    }
    return r & 0xffffff00u;
}
// Gives a neighbour fake the slots the loaded neighbours are asked for.
void SetupLoadedNeighbors(std::mt19937 &rng)
{
    for (unsigned i = 0; i < 3; ++i) SetupNeighborObj(F(fNb0 + i), rng, i * 5);
    F(fPersist).value[0] = rng();
    F(fPersist).Set(36, "pdd", 0u, &PersistKeys);
    F(fPersist).Set(5, "pdp--", 0u, &PersistCreate);
}
} // namespace

DIFF_TEST(NeighborManagerServices_UpdateKeyMap, 0x4bf4f0)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 500; ++seed)
        Pair(ctx, "UpdateKeyMap", seed, [&](Globals &, Mgr *m, std::mt19937 &rng, bool orig) -> uint32_t {
            const unsigned n = rng() % 5;
            for (unsigned i = 0; i < 5; ++i) F(fNb0 + i).Set(15, 0, rng() % 6);
            FillMap(m, rng, fNb0, n);
            FillKeyMap(m, rng);
            void *subject = nullptr;
            switch (rng() % 4) {
            case 1:
            case 2: subject = &F(fNb4); break;
            case 3: subject = &F(fNb0 + rng() % 5); break;
            }
            const uint32_t remove = rng() % 2 ? 1 + (rng() & 0xffffff00u) : (rng() & 0xffffff00u);
            using Fn = void(__thiscall *)(Mgr *, void *, uint32_t);
            ROUTE(Fn, 0x4bf4f0, kNoFace, UpdateKeyMap, subject, remove);
            return 0;
        });
}

DIFF_TEST(NeighborManagerServices_RemoveNeighborEffects, 0x4be3f0)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 500; ++seed)
        Pair(ctx, "RemoveNeighborEffects", seed, [&](Globals &g, Mgr *m, std::mt19937 &rng, bool orig) -> uint32_t {
            for (unsigned i = 0; i < 5; ++i) SetupNeighborObj(F(fNb0 + i), rng, i * 7);
            F(fNb4).Set(12, 0, rng() % 4 ? &F(fOwnTable) : nullptr);
            SetupRemovalFakes(g, rng);
            FillMap(m, rng, fNb0, rng() % 5);
            // the subject is usually in the map under its own word
            if (rng() % 5) m->mNeighbors[int32_t(int16_t(F(fNb4).ret[8]))].Assign(&F(fNb4));
            FillKeyMap(m, rng);
            m->mByPersonDataId[F(fNb4).ret[15]].Assign(&F(fNb4));
            using Fn = void(__thiscall *)(Mgr *, void *);
            ROUTE(Fn, 0x4be3f0, kNoFace, RemoveNeighborEffects, static_cast<void *>(&F(fNb4)));
            return 0;
        });
}

DIFF_TEST(NeighborManagerServices_LoadNeighbors, 0x4bdf00)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 500; ++seed)
        Pair(ctx, "LoadNeighbors", seed, [&](Globals &g, Mgr *m, std::mt19937 &rng, bool orig) -> uint32_t {
            SetupLoadedNeighbors(rng);
            g.Set(kPersistCell, &F(fPersist));
            FillMap(m, rng, fNb0, rng() % 4);
            FillKeyMap(m, rng);
            const uint32_t group = rng();
            using Fn = uint8_t(__thiscall *)(Mgr *, uint32_t);
            return ROUTE(Fn, 0x4bdf00, kNoFace, LoadNeighbors, group) & 0xff;
        });
}

DIFF_TEST(NeighborManagerServices_Reload, 0x4bd290)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 600; ++seed)
        Pair(ctx, "Reload", seed, [&](Globals &g, Mgr *m, std::mt19937 &rng, bool orig) -> uint32_t {
            SetupLoadedNeighbors(rng);
            for (unsigned i = 3; i < 5; ++i) SetupNeighborObj(F(fNb0 + i), rng, i * 7);
            SetupRemovalFakes(g, rng);
            m->mUserFileCounter = rng();
            FillMap(m, rng, fNb3, 2);
            FillKeyMap(m, rng);
            const uint32_t group = rng();
            const uint32_t flag = rng() % 3 ? 1 + (rng() & 0xffffff00u) : (rng() & 0xffffff00u);
            using Fn = uint8_t(__thiscall *)(Mgr *, uint32_t, uint32_t);
            return ROUTE_NA(Fn, 0x4bd290, 0, Reload, group, flag) & 0xff;
        });
}

namespace {
// App slot 26's slot 42 (key): finds the entry for the first `value[0]` calls, then nothing.
uint32_t EntriesFind(Obj *o, uint32_t *)
{
    if (!o->value[0]) return 0;
    --o->value[0];
    return P(&F(fEntry));
}
// App slot 9's slot 19 (vector of 16-bit ids): fills it with some family ids from the allocator service.
uint32_t FamilyIds(Obj *o, uint32_t *a)
{
    auto *vec = reinterpret_cast<void **>(a[0]);
    const unsigned n = RandomWord(o, nullptr) % 4;
    if (!n) return 0;
    const unsigned total = n + (RandomWord(o, nullptr) >> 8) % 3;
    auto *block = static_cast<int16_t *>(RZMemory::Malloc(total * 2));
    gAllocated.push_back(block);
    for (unsigned i = 0; i < total; ++i) block[i] = int16_t(RandomWord(o, nullptr) >> 10);
    vec[0] = block;
    vec[1] = block + n;
    vec[2] = block + total;
    return 0;
}
uint32_t FamilyFind(Obj *o, uint32_t *a) { return (a[0] & 1) == o->value[1] ? P(&F(fFamily)) : 0; }
// Persist slot 24 / records slot 27 / family slot 29 answers: a byte with junk above.
uint32_t FlagAnswer(Obj *o, uint32_t *) { return o->value[0]; }

void SetupSubject(std::mt19937 &rng)
{
    Obj &s = F(fNb4);
    SetupNeighborObj(s, rng, 11);
    s.Set(12, 0, rng() % 4 ? &F(fOwnTable) : nullptr);
    s.Set(10, 0, &F(fPerson));
    s.Set(35, 0, ByteValue(rng, rng() % 6 != 0));
    for (unsigned i = 0; i < 4; ++i) SetupNeighborObj(F(fNb0 + i), rng, i * 5);
}
void SetupListAndFamilies(std::mt19937 &rng)
{
    F(fApp).Set(26, 0, &F(fEntries));
    F(fEntries).value[0] = rng() % 4;
    F(fEntries).Set(42, "d", 0u, &EntriesFind);
    F(fEntries).Set(17, "d");
    F(fEntry).Set(47, 0, rng());
    F(fPerson).Set(28, 0, rng());
    F(fPerson).Set(67, 0, rng() % 6 ? &F(fHeld) : nullptr);
    F(fHeld).Set(11, 0, rng());
    F(fApp).Set(9, 0, &F(fFamilies));
    F(fFamilies).value[0] = rng();
    F(fFamilies).Set(19, "p", 0u, &FamilyIds);
    F(fFamilies).Set(11, "d", 0u, &FamilyFind);
    F(fFamily).value[0] = ByteValue(rng, rng() % 4 == 0);
    F(fFamily).Set(29, "d", 0u, &FlagAnswer);
}
} // namespace

DIFF_TEST(NeighborManagerServices_RemoveNeighbor, 0x4bc0f0)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 800; ++seed)
        Pair(ctx, "RemoveNeighbor", seed, [&](Globals &g, Mgr *m, std::mt19937 &rng, bool orig) -> uint32_t {
            SetupSubject(rng);
            SetupRemovalFakes(g, rng);
            SetupListAndFamilies(rng);
            F(fFamilies).value[1] = rng() % 2;
            F(fX).value[0] = ByteValue(rng, rng() % 4 != 0);
            // the original uses the held object unchecked once the records object says yes
            if (!F(fPerson).ret[67] && (F(fX).value[0] & 0xff)) F(fPerson).Set(67, 0, &F(fHeld));
            F(fApp).Set(24, 0, &F(fRecords));
            F(fRecords).Set(27, "p", 0u, &FlagAnswer);
            F(fPersist).value[0] = ByteValue(rng, rng() % 3 != 0);
            F(fPersist).Set(24, "dd", 0u, &FlagAnswer);
            FillMap(m, rng, fNb0, rng() % 4);
            if (rng() % 5) m->mNeighbors[int32_t(int16_t(F(fNb4).ret[8]))].Assign(&F(fNb4));
            FillKeyMap(m, rng);
            m->mByPersonDataId[F(fNb4).ret[15]].Assign(&F(fNb4));
            using Fn = int32_t(__thiscall *)(Mgr *, void *);
            return uint32_t(ROUTE_NA(Fn, 0x4bc0f0, 0, RemoveNeighbor, static_cast<void *>(&F(fNb4))));
        });
}

namespace {
// The neighbour's data value query (slot 16, one argument): 0xa5 answers from the fake's word, the low half only is tested.
uint32_t DataValue(Obj *o, uint32_t *) { return o->value[1]; }
// Persist slot 48 (group, list): fills a ServiceRefVector (storage from the allocator service) with some counted objects.
uint32_t PersistList(Obj *o, uint32_t *a)
{
    Ev(a[0]);
    auto *list = reinterpret_cast<ServiceRefVector *>(a[1]);
    const unsigned n = RandomWord(o, nullptr) % 4;
    if (!n) return 0;
    const unsigned total = n + (RandomWord(o, nullptr) >> 8) % 3;
    auto **block = static_cast<void **>(RZMemory::Malloc(total * 4));
    gAllocated.push_back(block);
    for (unsigned i = 0; i < n; ++i) block[i] = &F(fNb0 + (RandomWord(o, nullptr) >> 6) % 4);
    list->mFirst = block;
    list->mLast = block + n;
    list->mEnd = block + total;
    return 0;
}
// The framework's slot 3 (class, interface, out): hands out the created object sometimes.
uint32_t FrameworkCreateHelper(Obj *o, uint32_t *a)
{
    const uint32_t r = RandomWord(o, nullptr) >> 8;
    if (r % 4 != 0) {
        ++F(fCreated).refs;
        *reinterpret_cast<void **>(a[2]) = &F(fCreated);
    }
    return r;
}
} // namespace

DIFF_TEST(NeighborManagerServices_RetireNeighbor, 0x4be980)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 800; ++seed)
        Pair(ctx, "RetireNeighbor", seed, [&](Globals &g, Mgr *m, std::mt19937 &rng, bool orig) -> uint32_t {
            SetupSubject(rng);
            SetupRemovalFakes(g, rng);
            SetupListAndFamilies(rng);
            F(fPerson).Set(67, 0, &F(fHeld)); // used unchecked
            Obj &s = F(fNb4);
            s.value[1] = rng() % 5 == 0 ? (rng() & 0xffff) | (rng() & 0xffff0000u) : (rng() & 0xffff0000u);
            s.Set(16, "d", 0u, &DataValue);
            s.Set(17, "dd");
            F(fApp).Set(24, 0, &F(fRecords));
            F(fRecords).Set(14, "d", &F(fPerson));
            F(fPerson).Set(3, 0);
            F(fKind).Set(6, "dd");
            F(fPersist).value[0] = rng();
            F(fPersist).Set(48, "dp", 0u, &PersistList);
            F(fMgr).value[0] = ByteValue(rng, rng() % 4 != 0);
            F(fMgr).Set(23, "pd", 0u, &LogResourceVector);
            F(fFramework).value[0] = rng();
            F(fFramework).Set(3, "ddp", 0u, &FrameworkCreateHelper);
            F(fCreated).Set(3, 0);
            F(fCreated).Set(10, "dd");
            // (slot 23 is the MgrSlot23 shim, patched by BuildMgr)
            FillMap(m, rng, fNb0, rng() % 4);
            using Fn = int32_t(__thiscall *)(Mgr *, void *, uint32_t);
            const uint32_t unused = rng();
            return uint32_t(ROUTE_NA(Fn, 0x4be980, 0, RetireNeighbor, static_cast<void *>(&F(fNb4)), unused));
        });
}

namespace {
// ================= DeleteNeighborResources =================
enum Dr { drE0, drE1, drE2, drE3, drHolder, drPersist };
constexpr uint32_t kTypePool[] = {0x43545353, 0x856ddbac, 0x4f424a44, 0x5b12bf90, 0x4f424a66, 0xaace2efb, 0x3053cf74, 0x0, 0x43545352};
// Element slot 0 (QueryInterface: interface, out): hands out the holder, or not.
uint32_t ElementQuery(Obj *o, uint32_t *a)
{
    if (o->value[0] & 1) {
        ++F(drHolder).refs;
        *reinterpret_cast<void **>(a[1]) = &F(drHolder);
    }
    return o->value[0] >> 8;
}
// Holder slot 19 (vector, 0): fills a key vector from the allocator service.
uint32_t HolderKeys(Obj *o, uint32_t *a)
{
    auto *vec = reinterpret_cast<void **>(a[0]);
    const unsigned n = RandomWord(o, nullptr) % 6;
    if (!n) return 0;
    const unsigned total = n + (RandomWord(o, nullptr) >> 8) % 3;
    auto *block = static_cast<NeighborResourceKey *>(RZMemory::Malloc(total * 16));
    gAllocated.push_back(block);
    AddFreshRange(block, total * 16, 0xd0000000);
    for (unsigned i = 0; i < total; ++i) {
        block[i].type = kTypePool[(RandomWord(o, nullptr) >> 11) % 9];
        block[i].group = RandomWord(o, nullptr) % 5;
        block[i].instance = RandomWord(o, nullptr) % 5;
        block[i].high = 0;
    }
    vec[0] = block;
    vec[1] = block + n;
    vec[2] = block + total;
    return 0;
}
} // namespace

DIFF_TEST(NeighborManagerServices_DeleteNeighborResources, 0x4bed00)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 600; ++seed)
        Pair(ctx, "DeleteNeighborResources", seed, [&](Globals &g, Mgr *m, std::mt19937 &rng, bool orig) -> uint32_t {
            for (unsigned i = 0; i < 4; ++i) {
                F(drE0 + i).value[0] = rng();
                F(drE0 + i).Set(0, "dp", 0u, &ElementQuery);
            }
            F(drHolder).value[0] = rng();
            F(drHolder).Set(19, "p-", 0u, &HolderKeys);
            F(drHolder).Set(26, "p", 0u, &KeyArg0);
            F(drHolder).Set(10, 0);
            g.Set(kPersistCell, &F(drPersist));
            NeighborIdVector list;
            for (unsigned n = rng() % 6; n; --n) list.push_back(P(&F(drE0 + rng() % 4)));
            const uint32_t unused = rng();
            using Fn = uint8_t(__thiscall *)(Mgr *, const NeighborIdVector *, uint32_t);
            return ROUTE_NA(Fn, 0x4bed00, 0, DeleteNeighborResources, &list, unused) & 0xff;
        });
}

namespace {
// The framework's slot 3 (class, interface, out) for CreateNeighbor: the created neighbour or nothing.
uint32_t FrameworkCreateNeighbor(Obj *o, uint32_t *a)
{
    const uint32_t r = RandomWord(o, nullptr) >> 8;
    if (r % 4 == 0) return 0;
    ++F(fCreated).refs;
    *reinterpret_cast<void **>(a[2]) = &F(fCreated);
    return r | 1;
}
} // namespace

DIFF_TEST(NeighborManagerServices_CreateNeighbor, 0x4bbd80)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 800; ++seed)
        Pair(ctx, "CreateNeighbor", seed, [&](Globals &g, Mgr *m, std::mt19937 &rng, bool orig) -> uint32_t {
            SetupNeighborObj(F(fNb0), rng, 3);
            for (unsigned i = 1; i < 5; ++i) SetupNeighborObj(F(fNb0 + i), rng, i * 5);
            Obj &created = F(fCreated);
            SetupNeighborObj(created, rng, 9);
            created.Set(6, "dp");
            F(fPerson).Set(4, 0, rng() % 3 ? &F(fKind) : nullptr);
            F(fKind).Set(3, "d");
            F(fRecord).Set(4, "p", 0u, &KeyArg0);
            F(fFramework).value[0] = rng();
            F(fFramework).Set(3, "ddp", 0u, &FrameworkCreateNeighbor);
            g.Set(kAppCell, &F(fApp));
            // the counter: small values often (the map fills up to and past it), sometimes large or negative
            const unsigned kind = rng() % 6;
            m->mNextNeighborId = kind == 0 ? uint16_t(rng()) : kind == 1 ? uint16_t(0xfff0 + rng() % 16) : uint16_t(rng() % 9);
            const int32_t base = int16_t(m->mNextNeighborId);
            for (unsigned n = rng() % 7; n; --n) {
                NeighborDataRef &slot = m->mNeighbors[(rng() % 3 ? base : 0) + int32_t(rng() % 5) - (rng() % 4 ? 0 : 1)];
                if (rng() % 8) slot.Assign(&F(fNb0 + rng() % 4));
            }
            FillKeyMap(m, rng);
            void *data = rng() % 8 == 0 ? nullptr : &F(fPerson);
            using Fn = void *(__thiscall *)(Mgr *, void *);
            void *result = ROUTE_NA(Fn, 0x4bbd80, 0, CreateNeighbor, data);
            return Norm(P(result));
        });
}


