// Differential tests for the data-container face of cTSNeighborManager (src/ts/cTSNeighborManager.*): Export (container
// face slot 4, 0x4bf710). Fixture: tests/ts_services_fixture.h (svcfx recording fakes).
//
// The manager is a zeroed block of the manager's size whose vtable answers slot 16 (GetNeighbor) from a fake and whose
// resource key is random; the container, its neighbour context and segment, the neighbours, their relationship tables and
// records, the app's lots, the persist system and its database are fakes. Compared: the result, the event log (keys logged
// by content), the fakes' reference counts and value words, and whether the blocks the fakes handed out were freed.
//
// Standalone shape (tests/ts_neighbor_manager_shape.h): every seed runs three routes (the original, our vtable, the adapter with a
// trap); the manager is the route's object built by our constructor in a zeroed block, its shims patched over copies of the
// route's own vtables. Route 0 is compared with routes 1 and 2.
#include "ts_services_fixture.h"

#include "rz/RZMemory.h"
#include "simantics/ServiceRefVector.h"
#include "ts_neighbor_manager_shape.h"

#include <random>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "d"}, {1, "d"}, {2, "d"}, {3, "d"}, {4, "d--d-dd"}, {5, "dd----d"}, {6, "d-pddd"}, {7, "dd---d"}, {8, "d--dd"}, {9, "dd"}, {11, "d"}, {12, "ddd"}, {13, "dd"}, {14, "dd"}, {15, "d-pdd"}, {16, "d"}, {17, "d"}, {18, "d"}, {19, "d"}, {20, "d"}, {23, "d-dddd"}};
using namespace svcfx;
using namespace nbrmgrshape;

// The route's manager in a zeroed block: our constructor, the route's vtables copied with the shims patched over them (primary
// face from `primary`, persist face from `persist`); destroyed at the end of the scope.
struct RouteMgr {
    alignas(8) uint32_t words[sizeof(Mgr) / 4] = {};
    explicit RouteMgr(void *const *primary, void *const *persist = nullptr)
    {
        Construct(words);
        ClearPatches();
        for (unsigned s = 0; s < kFaceSlots[0]; ++s) gPatch[0][s] = primary[s];
        if (persist)
            for (unsigned s = 0; s < kFaceSlots[1]; ++s) gPatch[1][s] = persist[s];
        Install(words);
        gTrapHits = 0;
    }
    ~RouteMgr() { Destroy(words); }
    RouteMgr(const RouteMgr &) = delete;
    RouteMgr &operator=(const RouteMgr &) = delete;
};
// The vtable words (coded) and the trap count, after the call.
void CaptureShape(Blob &b, const RouteMgr &mgr)
{
    for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtCode(f, shape::Word(mgr.words, kFaceOffset[f])));
    b.push_back(gTrapHits);
}
// Route 0 against routes 1 and 2.
void SameRoutes(difftest::Context &ctx, const Blob (&blobs)[kRoutes], const char *what, uint32_t seed)
{
    for (int route = 1; route < kRoutes; ++route) Same(ctx, blobs[0], blobs[route], what, seed);
}
#define ROUTE(Fn, rva, face, name, ...) Call<Fn>(ctx, mgr.words, rva, face, NBRMGR_ADAPTER(name), __VA_ARGS__)
#define ROUTE_NA(Fn, rva, face, name, ...) Call<Fn>(ctx, mgr.words, rva, face, NBRMGR_NO_ADAPTER, __VA_ARGS__)

enum Fk : unsigned {
    cNb0, cNb1, cNb2, cNb3,  // neighbours
    cTable, cRecord,         // a relationship table, a neighbour's record (persist face)
    cContainer, cContext, cSegment,
    cApp, cLots, cLotSide,
    cPersist, cDatabase, cSource,
    cFileRecord, cWriter,
    cMgr
};

void *gContainerMgrVT[40];

uint32_t __fastcall MgrGetNeighbor(void *, void *, uint32_t id, uint32_t all)
{
    uint32_t a[2] = {id, all};
    return detail::Dispatch(&F(cMgr), 16, a, 2);
}

void LogKey(const void *key)
{
    const auto *w = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) Ev(w[i]);
}
uint32_t KeyArg0(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return o->ret[0];
}
uint32_t Next(Obj *o) { return o->value[0] = o->value[0] * 1664525u + 1013904223u; }
// record slot 3: writes a key into the buffer
uint32_t RecordGetKey(Obj *o, uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(a[0]);
    for (int i = 0; i < 4; ++i) out[i] = Next(o) & (i == 1 ? 0xffff : 0xffffffffu);
    return a[0];
}
uint32_t RecordSetKey(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return 1;
}
// a yes/no byte with junk above, from the fake's generator
uint32_t RandomByte(Obj *o, uint32_t *)
{
    const uint32_t r = Next(o);
    return (r >> 8) % 3 ? (r & 0xffffff00u) | 1 : r & 0xffffff00u;
}
// context slot 30: fills the vector with ids from the allocator service
uint32_t ContextIds(Obj *o, uint32_t *a)
{
    auto *vec = reinterpret_cast<ServiceBlockVector *>(a[0]);
    const uint32_t n = o->value[1];
    if (!n) return 0;
    auto *block = static_cast<uint32_t *>(RZMemory::Malloc(n * 4));
    for (uint32_t i = 0; i < n; ++i) block[i] = Next(o) % 7;
    vec->mFirst = block;
    vec->mLast = vec->mEnd = block + n;
    gAllocated.push_back(block);
    return 0;
}
// container/record slot 0 (QueryInterface id, out): answers the fake in value[2] when value[3] says yes
uint32_t Query(Obj *o, uint32_t *a)
{
    if (!o->value[3]) return o->value[4] & 0xffffff00u;
    *reinterpret_cast<void **>(a[1]) = reinterpret_cast<void *>(o->value[2]);
    return 1 | (o->value[4] & 0xffffff00u);
}
uint32_t QueryAt1(Obj *o, uint32_t *a) { return Query(o, a); }
// container slot 12 (id, out): the segment
uint32_t GetSegment(Obj *o, uint32_t *a)
{
    if (!o->value[5]) return 0;
    *reinterpret_cast<void **>(a[1]) = &F(cSegment);
    return 1;
}
// segment slot 22 (key, out, mode)
uint32_t SegmentOpen(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    if (Next(o) % 4 == 0) return 0;
    *reinterpret_cast<void **>(a[1]) = &F(cFileRecord);
    return 1;
}
// database slot 34 (key, out)
uint32_t OpenSource(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    if (o->value[1]) *reinterpret_cast<void **>(a[1]) = &F(cSource);
    return 0;
}
uint32_t HasRecord(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return RandomByte(o, a);
}
uint32_t CopyRecord(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    LogKey(reinterpret_cast<const void *>(a[2]));
    return 0;
}
// writer slot 19 (metadata): logs the metadata's words
uint32_t SetMetadata(Obj *, uint32_t *a)
{
    const auto *w = reinterpret_cast<const uint32_t *>(a[0]);
    gEv.back() = w != nullptr; // a fresh heap block: log whether there is one, then its words
    if (!w) return 0;
    for (int i = 1; i < 5; ++i) Ev(w[i]);
    return 0;
}

// logs a bool argument by its low byte (the generic log has already logged the call)
uint32_t ByteArg(Obj *, uint32_t *a)
{
    gEv.back() &= 0xff;
    return a[0] & 0;
}

} // namespace

DIFF_TEST(NeighborManagerContainer_Export, 0x4bf710)
{
    AllocatorScope allocator;
    gContainerMgrVT[16] = reinterpret_cast<void *>(&MgrGetNeighbor);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Blob blobs[kRoutes];
        for (int side = 0; side < kRoutes; ++side) {
            gRoute = side;
            ResetAll();
            std::mt19937 rng(seed * 7919u + 3);
            Globals g;
            RouteMgr mgr(gContainerMgrVT);
            for (int i = 0; i < 4; ++i) mgr.words[0x44 / 4 + i] = rng();
            AddRange(mgr.words, sizeof(mgr.words), 0xe0000000);

            Obj &container = F(cContainer);
            container.value[2] = P(&F(cContext));
            container.value[3] = rng() % 8 != 0;
            container.value[4] = rng();
            container.value[5] = rng() % 8 != 0;
            container.Set(0, "dp", 0u, &Query);
            container.Set(12, "dp", 0u, &GetSegment);
            container.Set(14, 0, rng() & 0xffff);

            Obj &context = F(cContext);
            context.value[0] = rng();
            context.value[1] = rng() % 6;
            context.Set(30, "p", 0u, &ContextIds);
            context.Set(27, "d", 0u, &RandomByte);

            F(cMgr).value[0] = rng();
            F(cMgr).Set(16, "dd", 0u, [](Obj *o, uint32_t *a) -> uint32_t {
                const uint32_t r = Next(o);
                if (r % 5 == 0) return 0;
                return P(&F(cNb0 + a[0] % 4));
            });
            for (unsigned i = 0; i < 4; ++i) {
                Obj &n = F(cNb0 + i);
                n.value[0] = rng();
                n.Set(12, 0, rng() % 3 ? &F(cTable) : nullptr);
                n.Set(48, 0, &F(cRecord));
                n.Set(15, 0, rng());
                n.Set(38, 0, 0u, &RandomByte);
                n.Set(39, "d", 0u, &ByteArg); // a bool: the original pushes a dword with stale upper bytes
            }
            F(cTable).Set(11, 0, rng() % 4);
            F(cTable).Set(12, 0, rng());
            F(cTable).Set(5, "d");
            F(cTable).Set(13, "d", rng());
            F(cRecord).value[0] = rng();
            F(cRecord).Set(3, "p", 0u, &RecordGetKey);
            F(cRecord).Set(4, "p", 0u, &RecordSetKey);

            F(cApp).Set(6, 0, rng() % 4 ? &F(cLots) : nullptr);
            F(cLots).Set(39, "d", rng() % 3 ? &F(cLotSide) : nullptr);
            F(cLotSide).value[0] = rng();
            F(cLotSide).Set(3, "p", 0u, &RandomByte);
            F(cSegment).value[0] = rng();
            F(cSegment).Set(22, "ppd", 0u, &SegmentOpen);
            F(cSegment).Set(24, "p");
            F(cFileRecord).value[2] = P(&F(cWriter));
            F(cFileRecord).value[3] = rng() % 4 != 0;
            F(cFileRecord).value[4] = rng();
            F(cFileRecord).Set(0, "dp", 0u, &QueryAt1);
            F(cWriter).Set(9, "d");
            F(cWriter).Set(19, "p", 0u, &SetMetadata);

            F(cPersist).value[0] = rng();
            F(cPersist).Set(3, 0, &F(cDatabase));
            F(cPersist).Set(16, "pp", 0u, &RandomByte);
            F(cPersist).Set(37, "pppp", 0u, &CopyRecord);
            F(cDatabase).value[0] = rng();
            F(cDatabase).value[1] = rng() % 4 != 0;
            F(cDatabase).Set(34, "pp", 0u, &OpenSource);
            F(cDatabase).Set(15, "p", 0u, &HasRecord);

            g.Set(kAppCell, &F(cApp));
            g.Set(kPersistCell, &F(cPersist));

            void *arg = rng() % 10 ? &container : nullptr;
            Blob &b = blobs[side];
            using Fn = uint8_t(__thiscall *)(void *, void *);
            b.push_back(ROUTE_NA(Fn, 0x4bf710, 2, Export, arg) & 0xff);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureFakes(b, kValueKinds);
            CaptureAllocated(b);
            CaptureShape(b, mgr);
            gEv.clear();
        }
        gRoute = 0;
        SameRoutes(ctx, blobs, "Export", seed);
    }
}

// ---- CollectReferencedNeighbors (container face slot 3, 0x4bc700) ----

namespace {
enum : unsigned { cLot = cWriter, cEntry0 = cSource, cEntry1 = cFileRecord, cEntry2 = cDatabase, cRow0 = cRecord, cRow1 = cTable };
void *gEntries[4];
void *gRows[3];
unsigned gEntryCount = 0, gRowCount = 0;

uint32_t __fastcall MgrFindByKey(void *, void *, uint32_t key)
{
    uint32_t a[1] = {key};
    return detail::Dispatch(&F(cMgr), 17, a, 1);
}
uint32_t EntriesBegin(Obj *o, uint32_t *a)
{
    *reinterpret_cast<void ***>(a[0]) = o == &F(cLot) ? gEntries : gRows;
    return 0;
}
uint32_t EntriesEnd(Obj *o, uint32_t *a)
{
    *reinterpret_cast<void ***>(a[0]) = o == &F(cLot) ? gEntries + gEntryCount : gRows + gRowCount;
    return 0;
}
// a 16-bit value with junk above
uint32_t ShortValue(Obj *o, uint32_t *)
{
    const uint32_t r = Next(o);
    return ((r >> 4) % 9) | (r & 0xffff0000u);
}
// context slots 29 / 30: fill the vector (slot 30 overwrites the elements of a vector that already has a block)
uint32_t FillIds(Obj *o, uint32_t *a)
{
    auto *vec = reinterpret_cast<ServiceBlockVector *>(a[0]);
    if (vec->mFirst) {
        for (auto *p = static_cast<uint32_t *>(vec->mFirst); p != vec->mLast; ++p) *p = Next(o) % 7;
        return 0;
    }
    return ContextIds(o, a);
}
} // namespace

DIFF_TEST(NeighborManagerContainer_CollectReferenced, 0x4bc700)
{
    AllocatorScope allocator;
    gContainerMgrVT[16] = reinterpret_cast<void *>(&MgrGetNeighbor);
    gContainerMgrVT[17] = reinterpret_cast<void *>(&MgrFindByKey);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Blob blobs[kRoutes];
        for (int side = 0; side < kRoutes; ++side) {
            gRoute = side;
            ResetAll();
            std::mt19937 rng(seed * 104729u + 11);
            Globals g;
            RouteMgr mgr(gContainerMgrVT);
            AddRange(mgr.words, sizeof(mgr.words), 0xe0000000);

            Obj &container = F(cContainer);
            container.value[2] = P(&F(cContext));
            container.value[3] = rng() % 8 != 0;
            container.value[4] = rng();
            container.Set(0, "dp", 0u, &Query);

            Obj &context = F(cContext);
            context.value[0] = rng();
            context.value[1] = rng() % 5;
            // the lot list (the original uses the app's lots unchecked for every lot: no lots, no lot list)
            context.value[5] = rng() % 5;
            context.Set(23, "p", 0u, [](Obj *o, uint32_t *a) -> uint32_t {
                std::swap(o->value[1], o->value[5]);
                ContextIds(o, a);
                std::swap(o->value[1], o->value[5]);
                return 0;
            });
            context.Set(29, "p", 0u, &FillIds);
            context.Set(30, "p", 0u, &FillIds);
            context.Set(27, "d", 0u, &RandomByte);
            context.Set(28, "d", 0u, &RandomByte);
            context.Set(34, "dd", 0u, &RandomByte);
            context.Set(24, "d");
            context.Set(25, "d");

            F(cMgr).value[0] = rng();
            F(cMgr).Set(16, "dd", 0u, [](Obj *o, uint32_t *a) -> uint32_t {
                if (Next(o) % 5 == 0) return 0;
                return P(&F(cNb0 + a[0] % 4));
            });
            F(cMgr).Set(17, "d", 0u, [](Obj *o, uint32_t *a) -> uint32_t {
                if (Next(o) % 4 == 0) return 0;
                return P(&F(cNb0 + (a[0] ^ a[0] >> 16) % 4));
            });
            for (unsigned i = 0; i < 4; ++i) {
                Obj &n = F(cNb0 + i);
                n.Set(8, 0, (rng() % 9) | (rng() % 3 ? 0 : 0xfff0u) | (rng() & 0xffff0000u));
                n.Set(15, 0, rng());
            }

            F(cApp).Set(6, 0, rng() % 5 ? &F(cLots) : nullptr);
            if (!F(cApp).ret[6]) context.value[5] = 0;
            F(cLots).Set(38, "d", rng() % 4 ? &F(cLot) : nullptr);
            F(cLots).Set(39, "d", rng() % 4 ? &F(cLotSide) : nullptr);
            const uint32_t types[] = {0x4dd398a5, 0xedd39930, 0x4dd398a5, 0xedd39930, 0x1234};
            gEntryCount = rng() % 4;
            for (unsigned i = 0; i < 3; ++i) {
                Obj &e = F(i == 0 ? cEntry0 : i == 1 ? cEntry1 : cEntry2);
                e.value[0] = rng();
                e.Set(6, 0, types[rng() % 5]);
                e.Set(18, 0, (rng() % 5) | (rng() & 0xffff0000u));
                e.Set(21, "d", 0u, &ShortValue);
                e.Set(1, 0);
                e.Set(2, 0);
            }
            for (unsigned i = 0; i < gEntryCount; ++i) gEntries[i] = &F(i == 0 ? cEntry0 : i == 1 ? cEntry1 : cEntry2);
            gRowCount = rng() % 4;
            for (unsigned i = 0; i < 2; ++i) {
                Obj &r = F(i == 0 ? cRow0 : cRow1);
                r.value[0] = rng();
                r.Set(8, 0, rng() % 4);
                r.Set(18, 0, (10 + rng() % 5) | (rng() % 4 ? 0 : 0x8000u) | (rng() & 0xffff0000u));
                r.Set(21, "d", 0u, &ShortValue);
            }
            for (unsigned i = 0; i < gRowCount; ++i) gRows[i] = rng() % 5 ? &F(i % 2 ? cRow1 : cRow0) : nullptr;
            for (Obj *o : {&F(cLot), &F(cLotSide)}) {
                o->Set(28, "p", 0u, &EntriesBegin);
                o->Set(29, "p", 0u, &EntriesEnd);
            }
            g.Set(kAppCell, &F(cApp));

            void *arg = rng() % 10 ? &container : nullptr;
            using Fn = uint8_t(__thiscall *)(void *, void *);
            Blob &b = blobs[side];
            b.push_back(ROUTE_NA(Fn, 0x4bc700, 2, CollectReferenced, arg) & 0xff);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureFakes(b, kValueKinds);
            CaptureAllocated(b);
            CaptureShape(b, mgr);
            gEv.clear();
        }
        gRoute = 0;
        SameRoutes(ctx, blobs, "CollectReferenced", seed);
    }
}

// ---- CreateUserNeighbor (slot 20, 0x4bb840) ----

#include "rz/cRZString.h"
#include <set>

namespace {
enum : unsigned { uChars = cTable, uTemplate = cRecord, uUserFiles = cLots, uDirHolder = cLotSide, uDir = cSegment,
                  uCharacter = cContext, uData0 = cNb0, uData1 = cNb1, uNeighbor = cNb2, uPerson = cNb3, uCommon = cSource };
using DataSet = std::set<void *, std::less<void *>, GZServiceAllocator<void *>>;
constexpr uint32_t kCommonImplCell = 0x10cc040;
char gDirText[32];

void LogString(uint32_t pointer)
{
    const auto *s = reinterpret_cast<const cRZString *>(uintptr_t(pointer));
    Ev(P(s->mVTable) == P(GameAddress<void>(cRZString::kVTable)));
    Ev(uint32_t(s->mString.size()));
    uint32_t h = 2166136261u;
    for (char c : s->mString) h = (h ^ uint8_t(c)) * 16777619u;
    Ev(h);
    Ev(s->mRefCount);
}
// persist slot 18 (name, file, 0): the file exists for the first value[1] probes
uint32_t FileExists(Obj *o, uint32_t *a)
{
    LogString(a[0]);
    LogString(a[1]);
    if (o->value[1]) {
        --o->value[1];
        return 1 | (o->value[2] & 0xffffff00u);
    }
    return o->value[2] & 0xffffff00u;
}
// app interface slot 4 (id, string, 1, 0, -1): writes the default name
uint32_t DefaultName(Obj *o, uint32_t *a)
{
    reinterpret_cast<cRZString *>(uintptr_t(a[1]))->mString = o->value[1] ? "Default Sim" : "";
    return 1;
}
// characters slot 52 (10 arguments): the words around the two strings as logged (svcfx::DumpWords).
constexpr char kCreateHeadKinds[] = "dpdp";
constexpr char kCreateTailKinds[] = "-ddd";
uint32_t __fastcall CharactersCreate(Obj *self, void *, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                     uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9)
{
    Ev(evCall);
    Ev(self->id);
    Ev(52);
    const uint32_t head[] = {a0, a1, a2, a3}, tail[] = {a6, a7, a8, a9};
    svcfx::DumpWords(svcfx::gEv, head, sizeof(head), kCreateHeadKinds);
    LogString(a4);
    LogString(a5);
    svcfx::DumpWords(svcfx::gEv, tail, sizeof(tail), kCreateTailKinds);
    const uint32_t r = Next(self);
    // (the original uses the character unchecked once creation says yes)
    if (r % 5 == 0) *reinterpret_cast<void **>(a1) = &F(uCharacter);
    if (r % 3 == 0) return r & 0xffffff00u;
    *reinterpret_cast<void **>(a1) = &F(uCharacter);
    return (r & 0xffffff00u) | 1;
}
// the character's slot 4 (set, 1): enters some of the data fakes
uint32_t CollectData(Obj *o, uint32_t *a)
{
    auto *set = reinterpret_cast<DataSet *>(uintptr_t(a[0]));
    const uint32_t r = Next(o);
    if (r % 4 == 1) set->insert(&F(uData0));
    if (r % 4 == 2) set->insert(&F(uData1)), set->insert(&F(uData0));
    if (r % 4 == 3) set->insert(nullptr);
    return o->value[1];
}
uint32_t __fastcall MgrCreateNeighbor(void *, void *, uint32_t data)
{
    uint32_t a[1] = {data};
    return detail::Dispatch(&F(cMgr), 29, a, 1);
}
} // namespace

DIFF_TEST(NeighborManagerContainer_CreateUserNeighbor, 0x4bb840)
{
    AllocatorScope allocator;
    gContainerMgrVT[29] = reinterpret_cast<void *>(&MgrCreateNeighbor);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Blob blobs[kRoutes];
        for (int side = 0; side < kRoutes; ++side) {
            gRoute = side;
            ResetAll();
            std::mt19937 rng(seed * 15485863u + 5);
            Globals g;
            RouteMgr mgr(gContainerMgrVT);
            mgr.words[0x14 / 4] = rng() % 3 ? rng() % 50 : 31995 + rng() % 10;
            AddRange(mgr.words, sizeof(mgr.words), 0xe0000000);

            F(cPersist).value[1] = rng() % 3 ? rng() % 3 : rng() % 12;
            F(cPersist).value[2] = rng();
            F(cPersist).Set(18, "pp-", 0u, &FileExists);
            F(cApp).Set(24, 0, &F(uChars));
            F(cApp).Set(23, 0, &F(uUserFiles));
            F(uChars).value[0] = rng();
            F(uChars).Set(14, "d", rng() % 6 ? &F(uTemplate) : nullptr);
            F(uChars).Set(25, "d");
            F(uChars).table[52] = reinterpret_cast<void *>(&CharactersCreate);
            F(uUserFiles).Set(6, 0, &F(uDirHolder));
            F(uDirHolder).Set(8, 0, &F(uDir));
            snprintf(gDirText, sizeof(gDirText), "C:\\Sims\\Dir%u\\", unsigned(rng() % 100));
            F(uDir).Set(6, 0, gDirText);
            F(uDir).Set(7, 0, uint32_t(strlen(gDirText)));
            F(uCharacter).value[0] = rng();
            F(uCharacter).value[1] = rng();
            F(uCharacter).Set(4, "pd", 0u, &CollectData);
            F(cMgr).Set(29, "p", rng() % 4 ? &F(uNeighbor) : nullptr);
            const uint32_t personPick = rng() % 4;
            F(uNeighbor).Set(10, 0, personPick == 0 ? nullptr : personPick == 1 ? &F(uPerson) : &F(uData0));
            F(uCommon).value[1] = rng() % 2;
            F(uCommon).Set(4, "dpd-d", 0u, &DefaultName);

            g.Set(kAppCell, &F(cApp));
            g.Set(kPersistCell, &F(cPersist));
            g.Set(kCommonImplCell, &F(uCommon));

            const uint32_t pick = rng() % 4;
            const uint32_t templateId = pick == 0 ? 0 : pick == 1 ? 0xffffffffu : rng();
            const uint32_t a3 = rng(), a4 = rng(), a5 = rng();
            void *out = &F(cWriter);
            using Fn = int32_t(__thiscall *)(void *, void **, uint32_t, uint32_t, uint32_t, uint32_t);
            Blob &b = blobs[side];
            b.push_back(uint32_t(ROUTE_NA(Fn, 0x4bb840, 0, CreateUserNeighbor, &out, templateId, a3, a4, a5)));
            b.push_back(Norm(P(out)));
            b.push_back(mgr.words[0x14 / 4]);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureFakes(b, kValueKinds);
            CaptureShape(b, mgr);
            gEv.clear();
        }
        gRoute = 0;
        SameRoutes(ctx, blobs, "CreateUserNeighbor", seed);
    }
}

// ---- Import (container face slot 5, 0x4bd570) ----

namespace {
enum : unsigned {
    iDest, iFrom, iFromTable, iToTable, iContainer, iContext, iHoodContext, iSegment, iPersist, iDatabase, iSource,
    iCharSet, iHoodRecord, iApp, iLots, iLotSide, iHeld, iEntry0, iEntry1, iRecord, iReader, iCharacters, iFramework, iMgr
};
void *gImportMgrVT[40];
void *gImportFaceVT[12];
void *gHeldEntries[2];
unsigned gHeldCount = 0, gHeldCount2 = 0;
uint8_t gBlocks[2][0x1d0];

uint32_t __fastcall ImportMgrGetNeighbor(void *, void *, uint32_t id, uint32_t all)
{
    uint32_t a[2] = {id, all};
    return detail::Dispatch(&F(iMgr), 16, a, 2);
}
uint32_t __fastcall ImportFaceGetKey(void *, void *, uint32_t out)
{
    uint32_t a[1] = {out};
    return detail::Dispatch(&F(iMgr), 3, a, 1);
}
uint32_t __fastcall ImportFaceGroup(void *, void *) { return detail::Dispatch(&F(iMgr), 6, nullptr, 0); }
uint32_t FaceKey(Obj *o, uint32_t *a)
{
    auto *out = reinterpret_cast<uint32_t *>(a[0]);
    for (int i = 0; i < 4; ++i) out[i] = o->value[2 + i];
    return a[0];
}
// (id, *out): yes with an out value for most ids; small ids map to 0..2, large ones (table entries) to 0..5
uint32_t MapId(Obj *o, uint32_t *a)
{
    const uint32_t r = Next(o);
    if (r % 6 == 0) return r & 0xffffff00u;
    *reinterpret_cast<uint32_t *>(a[1]) = a[0] < 16 ? a[0] % 3 : (r >> 8) % 6;
    return (r & 0xffffff00u) | 1;
}
uint32_t ImportGetNeighbor(Obj *, uint32_t *a) { return a[0] == 5 ? 0 : P(&F(iDest)); }
// persist slot 5 (key, interface, out, 0, 0): logs the key; answers by the key's type
uint32_t PersistGet(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    const uint32_t type = *reinterpret_cast<const uint32_t *>(a[0]);
    void *answer = nullptr;
    if (type == 0xcc2a6a34) answer = o->value[3] ? &F(iCharSet) : nullptr;
    else if (type == 0x4e474248) answer = o->value[4] ? &F(iHoodRecord) : nullptr;
    else if (Next(o) % 4) answer = &F(iFrom);
    if (answer) *reinterpret_cast<void **>(a[2]) = answer, ++static_cast<Obj *>(answer)->refs;
    return answer != nullptr;
}
uint32_t AnswerFrom(Obj *o, uint32_t *) { return Next(o) % 4 ? P(&F(iFrom)) : 0; }
uint32_t AnswerHeld(Obj *o, uint32_t *) { return Next(o) % 3 ? 0 : P(&F(iHeld)); }
uint32_t BlockOf(Obj *o, uint32_t *a)
{
    if (Next(o) % 5 == 0) return 0;
    return P(gBlocks[a[0] ? 1 : 0]);
}
uint32_t HeldBegin(Obj *, uint32_t *a)
{
    *reinterpret_cast<void ***>(a[0]) = gHeldEntries;
    return 0;
}
uint32_t HeldEnd(Obj *, uint32_t *a)
{
    *reinterpret_cast<void ***>(a[0]) = gHeldEntries + gHeldCount;
    return 0;
}
uint32_t HeldEnd2(Obj *, uint32_t *a)
{
    *reinterpret_cast<void ***>(a[0]) = gHeldEntries + gHeldCount2;
    return 0;
}
// the record's QueryInterface for the reader
uint32_t RecordQuery(Obj *o, uint32_t *a)
{
    if (Next(o) % 4 == 0) return 0;
    *reinterpret_cast<void **>(a[1]) = &F(iReader);
    ++F(iReader).refs;
    return 1;
}
uint32_t ReaderVersion(Obj *o, uint32_t *a)
{
    if (Next(o) % 2) *reinterpret_cast<uint32_t *>(a[0]) = 0xc0 + Next(o) % 16;
    return 1;
}
uint32_t SegmentOpenForRead(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    if (Next(o) % 4 == 0) return 0;
    *reinterpret_cast<void **>(a[1]) = &F(iRecord);
    ++F(iRecord).refs;
    return 1;
}
// framework slot 3 (class, interface, out): the lot-side data object
uint32_t FrameworkCreate(Obj *, uint32_t *a)
{
    *reinterpret_cast<void **>(a[2]) = &F(iHeld);
    ++F(iHeld).refs;
    return 1;
}
uint32_t ContextQuery(Obj *o, uint32_t *a)
{
    if (!o->value[6]) return 0;
    *reinterpret_cast<void **>(a[1]) = &F(iHoodContext);
    ++F(iHoodContext).refs;
    return 1;
}
uint32_t ContainerQuery(Obj *o, uint32_t *a)
{
    void *answer = a[0] == 0xdb5c0c2 ? (o->value[3] ? &F(iContext) : nullptr) : (o->value[6] ? &F(iHoodContext) : nullptr);
    if (!answer) return 0;
    *reinterpret_cast<void **>(a[1]) = answer;
    ++static_cast<Obj *>(answer)->refs;
    return 1;
}
uint32_t LogBlock(const uint8_t *block)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(gBlocks[0]); ++i) h = (h ^ block[i]) * 16777619u;
    return h;
}
} // namespace

DIFF_TEST(NeighborManagerContainer_Import, 0x4bd570)
{
    AllocatorScope allocator;
    gImportMgrVT[16] = reinterpret_cast<void *>(&ImportMgrGetNeighbor);
    gImportFaceVT[3] = reinterpret_cast<void *>(&ImportFaceGetKey);
    gImportFaceVT[6] = reinterpret_cast<void *>(&ImportFaceGroup);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Blob blobs[kRoutes];
        for (int side = 0; side < kRoutes; ++side) {
            gRoute = side;
            ResetAll();
            std::mt19937 rng(seed * 2654435761u + 17);
            Globals g;
            RouteMgr mgr(gImportMgrVT, gImportFaceVT);
            AddRange(mgr.words, sizeof(mgr.words), 0xe0000000);
            for (auto &block : gBlocks)
                for (auto &byte : block) byte = uint8_t(rng());

            F(iMgr).value[0] = rng();
            for (int i = 0; i < 4; ++i) F(iMgr).value[2 + i] = rng();
            F(iMgr).Set(3, "p", 0u, &FaceKey);
            F(iMgr).Set(6, 0, rng() & 0xffff);
            F(iMgr).Set(16, "dd", 0u, &ImportGetNeighbor);

            Obj &container = F(iContainer);
            container.value[3] = rng() % 8 != 0;
            container.value[5] = rng() % 8 != 0;
            container.value[6] = rng() % 4 != 0;
            container.Set(0, "dp", 0u, &ContainerQuery);
            container.Set(12, "dp", 0u, [](Obj *o, uint32_t *a) -> uint32_t {
                if (!o->value[5]) return 0;
                *reinterpret_cast<void **>(a[1]) = &F(iSegment);
                ++F(iSegment).refs;
                return 1;
            });
            container.Set(14, 0, rng() & 0xffff);

            Obj &context = F(iContext);
            context.value[0] = rng();
            context.value[1] = rng() % 4;
            context.value[6] = 1;
            context.Set(0, 2, 0u, &ContextQuery);
            context.Set(30, "p", 0u, &ContextIds);
            context.Set(32, "dp", 0u, &MapId);
            context.Set(33, "d", rng());
            context.Set(36, "dp", 0u, &MapId);
            F(iHoodContext).value[0] = rng();
            F(iHoodContext).Set(25, "dp", 0u, &MapId);

            F(iSegment).value[0] = rng();
            F(iSegment).Set(22, "ppd", 0u, &SegmentOpenForRead);
            F(iSegment).Set(24, "p");
            F(iRecord).value[0] = rng();
            F(iRecord).Set(0, "pp", 0u, &RecordQuery);
            F(iReader).value[0] = rng();
            F(iReader).Set(9, "p", 0u, &ReaderVersion);
            F(iReader).Set(19, "p", 0u, &SetMetadata);
            gFrameworkIndex = iFramework;
            F(iFramework).Set(3, "ddp", 0u, &FrameworkCreate);

            F(iPersist).value[0] = rng();
            F(iPersist).value[3] = rng() % 2;
            F(iPersist).value[4] = rng() % 2;
            F(iPersist).Set(3, 0, &F(iDatabase));
            F(iPersist).Set(5, "pdp--", 0u, &PersistGet);
            F(iPersist).Set(37, "pppp", 0u, &CopyRecord);
            F(iDatabase).value[0] = rng();
            F(iDatabase).value[1] = rng() % 4 != 0;
            F(iDatabase).Set(34, "pp", 0u, &OpenSource);
            F(iDatabase).Set(15, "p", 0u, &HasRecord);
            F(iCharSet).value[0] = rng();
            F(iCharSet).Set(8, "d-");
            F(iCharSet).Set(17, "d", 0u, &AnswerFrom);
            F(iHoodRecord).value[0] = rng();
            F(iHoodRecord).Set(39, "d", 0u, &AnswerHeld);

            for (Obj *n : {&F(iDest), &F(iFrom)}) {
                n->value[0] = rng();
                n->Set(5, 0, rng());
                n->Set(8, 0, rng()); // GetId (ret 0)
                n->Set(16, "d", rng());
                n->Set(17, "dd");
                n->Set(18, "d", 0u, &BlockOf);
                n->Set(41, 0, rng());
                n->Set(42, "d");
                n->Set(51, "d", rng());
                n->Set(52, "dd");
            }
            F(iDest).Set(12, 0, &F(iToTable));
            F(iFrom).Set(12, 0, &F(iFromTable));
            for (Obj *t : {&F(iToTable), &F(iFromTable)}) {
                t->Set(11, 0, rng() % 4);
                t->Set(12, 0, 16 + rng() % 1000);
                t->Set(13, "d", 16 + rng() % 1000);
                t->Set(5, "d", rng() % 4);
                t->Set(6, "ddd"); // SetValueCount(owner, person, count) (ret 0xc)
                t->Set(7, "d");
                t->Set(9, "dd", rng());
                t->Set(10, "ddd");
            }

            F(iApp).Set(6, 0, rng() % 4 ? &F(iLots) : nullptr);
            F(iApp).Set(24, 0, &F(iCharacters));
            F(iCharacters).Set(14, "d", rng() % 2 ? &F(iHoodRecord) : nullptr);
            F(iLots).Set(39, "d", rng() % 4 ? &F(iLotSide) : nullptr);
            for (unsigned s : {9u, 15u, 32u}) F(iLotSide).Set(s, "p");
            F(iLotSide).Set(10, 0);
            F(iHeld).value[0] = rng();
            F(iHeld).Set(4, "p", 0u, &RandomByte);
            F(iHeld).Set(28, "p", 0u, &HeldBegin);
            F(iHeld).Set(29, "p", 0u, &HeldEnd);
            F(iHeld).Set(42, "p", 0u, &HeldBegin);
            F(iHeld).Set(43, "p", 0u, &HeldEnd2);
            gHeldCount = rng() % 3;
            gHeldCount2 = rng() % 3;
            for (unsigned i = 0; i < 2; ++i) {
                Obj &e = F(iEntry0 + i);
                gHeldEntries[i] = &e;
                e.value[0] = rng();
                e.Set(6, 0, rng() % 3 ? rng() % 3 : rng());
                e.Set(7, "d");
                e.Set(8, 0, rng() % 8);
                e.Set(9, 1);
                e.Set(18, 0, 10 + rng() % 5);
                e.Set(21, "d", 0u, &ShortValue);
                e.Set(22, "dd");
            }

            g.Set(kAppCell, &F(iApp));
            g.Set(kPersistCell, &F(iPersist));

            void *arg = rng() % 10 ? &container : nullptr;
            using Fn = uint8_t(__thiscall *)(void *, void *);
            Blob &b = blobs[side];
            b.push_back(ROUTE_NA(Fn, 0x4bd570, 2, Import, arg) & 0xff);
            b.push_back(LogBlock(gBlocks[0]));
            b.push_back(LogBlock(gBlocks[1]));
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureFakes(b, kValueKinds);
            CaptureAllocated(b);
            CaptureShape(b, mgr);
            gEv.clear();
        }
        gRoute = 0;
        SameRoutes(ctx, blobs, "Import", seed);
    }
}

// ---- RemapLotEntry (0x4be170, cdecl) ----

DIFF_TEST(NeighborManagerContainer_RemapLotEntry, 0x4be170)
{
    const uint32_t types[] = {0x5350ec38, 0x2eaa244c, 0x8e77e373, 0x0eaa2454, 0xed4cb1a4, 0x12345678, 0x60000000, 0};
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            std::mt19937 rng(seed * 40503u + 1);
            Obj &context = F(iContext);
            context.value[0] = rng();
            context.value[6] = rng() % 4 != 0;
            context.Set(0, "dp", 0u, &ContextQuery);
            context.Set(32, "dp", 0u, &MapId);
            context.Set(36, "dp", 0u, [](Obj *o, uint32_t *a) -> uint32_t {
                const uint32_t r = Next(o);
                if (r % 4 == 0) return r & 0xffffff00u;
                *reinterpret_cast<uint32_t *>(a[1]) = Next(o);
                return 1;
            });
            F(iHoodContext).value[0] = rng();
            F(iHoodContext).Set(25, "dp", 0u, &MapId);
            Obj &entry = F(iEntry0);
            entry.value[0] = rng();
            entry.Set(6, 0, types[rng() % 8]);
            entry.Set(8, 0, rng() % 8);
            entry.Set(9, "d");
            const uint32_t count = rng() % 3 ? 10 + rng() % 6 : rng() % 5;
            entry.Set(18, 0, count | (rng() % 5 ? 0 : 0x8000u) | (rng() & 0xffff0000u));
            entry.Set(21, "d", 0u, [](Obj *o, uint32_t *) -> uint32_t {
                const uint32_t r = Next(o);
                return (r >> 3) % 3 ? ((r >> 8) % 20) | (r & 0xffff0000u) : r;
            });
            entry.Set(22, "dd");
            const uint32_t pick = rng() % 10;
            void *ctxArg = pick == 0 ? nullptr : &context;
            void *entryArg = pick == 1 ? nullptr : &entry;
            const uint32_t id = rng();
            using Fn = void(__cdecl *)(void *, void *, uint32_t);
            if (side == 0) ctx.Original<Fn>(0x4be170)(ctxArg, entryArg, id);
            else Mgr::RemapLotEntry(ctxArg, entryArg, id);
            Blob &b = blobs[side];
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureFakes(b, kValueKinds);
            gEv.clear();
        }
        Same(ctx, blobs[0], blobs[1], "RemapLotEntry", seed);
    }
}
