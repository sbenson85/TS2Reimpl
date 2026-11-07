// Differential tests for the service-heavy cTSNeighborhood members (src/ts/cTSNeighborhood.*): Shutdown (0x47a3d0), Save (0x479560),
// FormatNumber / FormatNumberOf (0x473f50 / 0x473fa0), NotifyMembers (0x4737e0), GetStringTable (0x475bc0), AdjustCounter (0x4754d0),
// LookupEntryText (0x475a70), SaveLot (0x479610), SaveLots (0x4796e0), ResetLots (0x47a570) and RefreshLotFlags (0x47aef0).
// Fixture: tests/ts_services_fixture.h.
//
// The neighbourhood is a real object built by Construct() whose vtable is a table of recording shims (slots 16, 22, 69, 73 are the
// ones the members call through `this`) and whose persist face answers the group; everything else the members touch is a
// recording fake (persist system, app singleton, cached slot-23 object, resource manager, framework, message service, lots,
// records, ...). Both sides see the same seeded world; the event log, the fakes' reference counts and the neighbourhood's own
// state are compared.
#include "ts_services_fixture.h"

#include "rz/RZMemory.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSNeighborhood.h"
#include "ts_neighborhood_shape.h"

#include <random>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "d"}, {1, "dddd"}, {2, "d---dd"}, {3, "ddd"}, {4, "ddd"}, {5, "ddd"}, {6, "ddd"}, {7, "d"}, {23, "d"}};
using namespace svcfx;
using Nb = cTSNeighborhood;

constexpr unsigned kNbFake = 23; // the fake that stands for the neighbourhood's own vtable (slots called through `this`)

void *gNbVT[80];
void *gFaceVT[12];
uint32_t gGroup = 0;
bool gVTablesReady = false;

uint32_t Shim(unsigned slot, uint32_t *args, unsigned count) { return detail::Dispatch(&F(kNbFake), slot, args, count); }
uint32_t __fastcall NbSlot16(void *, void *)
{
    uint32_t a[1] = {};
    return Shim(16, a, 0);
}
uint32_t __fastcall NbSlot22(void *, void *, uint32_t x)
{
    uint32_t a[1] = {x};
    return Shim(22, a, 1);
}
uint32_t __fastcall NbSlot69(void *, void *, uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t a[3] = {x, y, z};
    return Shim(69, a, 3);
}
uint32_t __fastcall NbSlot73(void *, void *)
{
    uint32_t a[1] = {};
    return Shim(73, a, 0);
}
uint32_t __fastcall FaceGroup(void *, void *)
{
    Ev(evNote);
    Ev(6);
    return gGroup;
}

void InstallVTables()
{
    if (gVTablesReady) return;
    gNbVT[16] = reinterpret_cast<void *>(&NbSlot16);
    gNbVT[22] = reinterpret_cast<void *>(&NbSlot22);
    gNbVT[69] = reinterpret_cast<void *>(&NbSlot69);
    gNbVT[73] = reinterpret_cast<void *>(&NbSlot73);
    gFaceVT[6] = reinterpret_cast<void *>(&FaceGroup);
    gVTablesReady = true;
}

struct NbMem {
    alignas(8) uint8_t bytes[sizeof(Nb)];
    Nb *Get() { return reinterpret_cast<Nb *>(bytes); }
};

void BuildNb(difftest::Context &ctx, bool original, NbMem &mem)
{
    std::memset(mem.bytes, 0, sizeof(mem.bytes));
    Nb *nb = nbshape::Build(ctx, original, mem.bytes);
    nbshape::CopyOwn(gNbVT, 0); // the fakes are copies of the side's own vtables, patched
    nbshape::CopyOwn(gFaceVT, 1);
    gVTablesReady = false;
    InstallVTables();
    nbshape::VTableOf(nb, 0) = gNbVT;
    nbshape::VTableOf(nb, 1) = gFaceVT;
    AddRange(nb, sizeof(Nb), 0xe0000000);
}

void CaptureNb(Blob &b, Nb *nb)
{
    b.push_back(nb->mInitialized);
    b.push_back(nb->mField6C);
    b.push_back(nb->mByteC8);
    b.push_back(Norm(P(nb->mStateHelper)));
    b.push_back(Norm(P(nb->mDelegate)));
    b.push_back(Norm(P(nb->mSecondOwner)));
    b.push_back(Norm(P(nb->mOwner)));
    b.push_back(Norm(P(nb->mField_A4)));
    b.push_back(uint32_t(nb->mLots.size()));
    for (auto &entry : nb->mLots) {
        b.push_back(uint32_t(entry.first));
        b.push_back(Norm(P(entry.second.Get())));
    }
    b.push_back(uint32_t(nb->mTexts.size()));
    for (uint32_t id : nb->mServiceIds) b.push_back(id);
}

template <class Fn> bool Pair(difftest::Context &ctx, const char *what, uint32_t seed, Fn world)
{
    bool same = true;
    for (int route = 0; route < 2; ++route) {
        nbshape::gRoute = nbshape::Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            std::mt19937 rng(seed * 7919u + 17);
            Globals g;
            NbMem mem;
            BuildNb(ctx, side == 0, mem);
            gGroup = 0x1000 + (rng() & 0xfff);
            Blob &b = blobs[side];
            const uint32_t r = world(g, mem.Get(), rng, side == 0);
            b.push_back(r);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureNb(b, mem.Get());
            CaptureFakes(b, kValueKinds);
            CaptureAllocated(b);
            nbshape::Destroy(ctx, side == 0, mem.Get());
        }
        same = Same(ctx, blobs[0], blobs[1], what, seed | (route ? 0x80000000u : 0)) && same;
    }
    nbshape::gRoute = nbshape::kVTableRoute;
    return same;
}

uint32_t RandomWord(Obj *o, uint32_t *) { return o->value[0] = o->value[0] * 1664525u + 1013904223u; }

void LogKey(const void *key)
{
    const auto *w = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) Ev(w[i]);
}
// A handler that logs the key its first argument points at.
uint32_t KeyArg0(Obj *, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return 0;
}

// Fills the lot map with up to `count` entries: fakes first .. first + count - 1 (some null).
void FillLots(Nb *nb, std::mt19937 &rng, unsigned first, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        NeighborDataRef &slot = nb->mLots[int32_t(i * 3 + rng() % 3)];
        if (rng() % 6 != 0) slot.Assign(&F(first + i));
    }
}

// ================= Shutdown (0x47a3d0) =================
enum Sd { sdSvc, sdMsg, sdHelper, sdDelegate, sdOwner, sdSecond, sdLot0 };
uint32_t SvcQuery(Obj *, uint32_t *a)
{
    *reinterpret_cast<void **>(a[2]) = &F(sdMsg);
    ++F(sdMsg).refs;
    return 1;
}
} // namespace

DIFF_TEST(NeighborhoodServices_Shutdown, 0x47a3d0)
{
    for (uint32_t seed = 0; seed < 400; ++seed)
        Pair(ctx, "Shutdown", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            nb->mInitialized = rng() % 2;
            nb->mField6C = rng();
            nb->mStateHelper = rng() % 4 ? &F(sdHelper) : nullptr;
            nb->mDelegate = rng() % 4 ? &F(sdDelegate) : nullptr;
            nb->mSecondOwner = rng() % 4 ? &F(sdSecond) : nullptr;
            nb->mOwner = rng() % 4 ? &F(sdOwner) : nullptr;
            FillLots(nb, rng, sdLot0, rng() % 5);
            nb->mServiceIds.clear();
            for (unsigned n = rng() % 5; n; --n) nb->mServiceIds.insert(rng() % 300);
            g.Set(0x10c9378, rng() % 5 ? &F(sdSvc) : nullptr);
            F(sdSvc).Set(5, "pdp", 1u, &SvcQuery);
            F(sdMsg).Set(6, "pd");
            using Fn = uint8_t(__thiscall *)(void *);
            return nbshape::Call<Fn>(ctx, orig, nb, 0, 0x47a3d0, NB_ADAPTER(Shutdown));
        });
}

namespace {
// ================= Save (0x479560) =================
enum Sv { svPersist, svRes };
uint32_t SaveResource(Obj *o, uint32_t *)
{
    return RandomWord(o, nullptr);
}
} // namespace

DIFF_TEST(NeighborhoodServices_Save, 0x479560)
{
    for (uint32_t seed = 0; seed < 100; ++seed)
        Pair(ctx, "Save", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            F(svPersist).value[0] = rng();
            F(svPersist).Set(16, "p-", 0u, &SaveResource);
            F(kNbFake).Set(16, 0, &F(svRes));
            g.Set(kPersistCell, &F(svPersist));
            using Fn = uint8_t(__thiscall *)(void *);
            return (nbshape::Call<Fn>(ctx, orig, nb, 0, 0x479560, NB_ADAPTER(Save))) & 0xff;
        });
}

namespace {
// ================= FormatNumber / FormatNumberOf =================
struct PrintfObj {
    void **vt;
};
PrintfObj gOut;
void *gOutVT[34];
const char *const kTexts[] = {"alpha", "", "Neighborhood 12", "x"};
void *__cdecl FakeSprintf(PrintfObj *self, const char *format, const char *text, uint32_t value)
{
    Ev(evNote);
    Ev(31);
    Ev(P(format));
    for (const char *c = text; *c; ++c) Ev(uint8_t(*c));
    Ev(value);
    return self;
}
enum Fn_ { fnS23, fnSvc, fnNumbers, fnArg, fnArgNumbers, fnObject };
uint32_t PickText(Obj *o, uint32_t *) { return P(kTexts[(o->value[0]++) % 4]); }
} // namespace

DIFF_TEST(NeighborhoodServices_FormatNumber, 0x473f50)
{
    gOutVT[31] = reinterpret_cast<void *>(&FakeSprintf);
    gOut.vt = gOutVT;
    for (uint32_t seed = 0; seed < 300; ++seed)
        Pair(ctx, "FormatNumber", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            F(fnS23).Set(6, 0, &F(fnSvc));
            F(fnSvc).Set(8, 0, &F(fnNumbers));
            F(fnArg).Set(8, 0, &F(fnArgNumbers));
            F(fnNumbers).value[0] = rng();
            F(fnArgNumbers).value[0] = rng();
            F(fnNumbers).Set(5, 0, 0u, &PickText);
            F(fnArgNumbers).Set(5, 0, 0u, &PickText);
            g.Set(kSlot23Cache, &F(fnS23));
            const uint32_t pick = rng() % 4;
            const uint32_t value = pick == 0 ? 0 : pick == 1 ? 1 : rng();
            void *service = rng() % 2 ? &F(fnArg) : nullptr;
            using Fn = uint8_t(__thiscall *)(void *, void *, uint32_t, void *);
            return (nbshape::Call<Fn>(ctx, orig, nb, 0, 0x473f50, NB_ADAPTER(FormatNumber), &gOut, value, service)) & 0xff;
        });
}

DIFF_TEST(NeighborhoodServices_FormatNumberOf, 0x473fa0)
{
    for (uint32_t seed = 0; seed < 100; ++seed)
        Pair(ctx, "FormatNumberOf", seed, [&](Globals &, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            F(fnObject).value[0] = rng();
            F(fnObject).Set(18, 0, 0u, &RandomWord);
            F(kNbFake).value[0] = rng();
            F(kNbFake).Set(69, "pdp", 0u, &RandomWord);
            void *service = rng() % 2 ? &F(fnArg) : nullptr;
            using Fn = uint8_t(__thiscall *)(void *, void *, void *, void *);
            return (nbshape::Call<Fn>(ctx, orig, nb, 0, 0x473fa0, NB_ADAPTER(FormatNumberOf), &gOut, &F(fnObject), service)) & 0xff;
        });
}

namespace {
// ================= NotifyMembers (0x4737e0) =================
enum Nm { nmApp, nmA1, nmDelegate, nmHelper, nmMember0 };
uint32_t PickNext(Obj *o, uint32_t *)
{
    // slot 17 of the app (no arguments): the object the notification goes to (or null)
    return o->value[0] ? P(&F(nmA1)) : 0u;
}
} // namespace

DIFF_TEST(NeighborhoodServices_NotifyMembers, 0x4737e0)
{
    for (uint32_t seed = 0; seed < 400; ++seed)
        Pair(ctx, "NotifyMembers", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            nb->mDelegate = rng() % 2 ? &F(nmDelegate) : nullptr;
            nb->mStateHelper = rng() % 2 ? &F(nmHelper) : nullptr;
            F(nmDelegate).Set(43, "d");
            F(nmHelper).Set(17, "d");
            F(nmApp).value[0] = rng() % 4 != 0;
            F(nmApp).Set(17, 0, 0u, &PickNext);
            F(nmA1).Set(17, "d");
            unsigned fake = nmMember0;
            for (auto *map : {&nb->mMembers1, &nb->mMembers2, &nb->mMembers3})
                for (unsigned n = rng() % 3; n; --n) {
                    NeighborDataRef &slot = (*map)[int16_t(rng() % 50)];
                    if (rng() % 5 != 0 && fake < kNbFake) {
                        F(fake).Set(17, "d");
                        slot.Assign(&F(fake++));
                    }
                }
            g.Set(kAppCell, &F(nmApp));
            const uint32_t value = rng();
            using Fn = void(__thiscall *)(void *, uint32_t);
            nbshape::Call<Fn>(ctx, orig, nb, 0, 0x4737e0, NB_ADAPTER(NotifyMembers), value);
            return 0;
        });
}

namespace {
// ================= GetStringTable (0x475bc0) =================
enum Gt { gtDelegate, gtFramework, gtTable, gtPre };
uint32_t TableCreate(Obj *, uint32_t *a)
{
    Ev(a[0]);
    Ev(a[1]);
    *reinterpret_cast<void **>(a[2]) = &F(gtTable);
    ++F(gtTable).refs;
    return 1;
}
} // namespace

DIFF_TEST(NeighborhoodServices_GetStringTable, 0x475bc0)
{
    for (uint32_t seed = 0; seed < 200; ++seed)
        Pair(ctx, "GetStringTable", seed, [&](Globals &, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            nb->mDelegate = rng() % 4 == 0 ? &F(gtDelegate) : nullptr;
            nb->mField_A4 = rng() % 3 == 0 ? &F(gtPre) : nullptr;
            F(gtDelegate).Set(73, 0, 0x7700 + (rng() & 0xff));
            // The delegate's table is a token 0x7700 + n returned as is: registered so the result logs as a pointer.
            svcfx::AddRange(reinterpret_cast<const void *>(uintptr_t(0x7700)), 0x100, 0x77000000);
            F(gtFramework).Set(3, "ddp", 1u, &TableCreate);
            F(gtTable).Set(17, "p", 0u, &KeyArg0);
            F(gtTable).Set(19, 0);
            gFrameworkIndex = gtFramework;
            using Fn = void *(__thiscall *)(void *);
            void *table = nbshape::Call<Fn>(ctx, orig, nb, 0, 0x475bc0, NB_ADAPTER(GetStringTable));
            return Norm(P(table));
        });
}

namespace {
// ================= AdjustCounter (0x4754d0) =================
enum Ac { acPersist, acSeg, acRec, acReader, acWriter, acDelegate };
// persist slot 34 (key, iid, out): finds the segment
uint32_t AcFind(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    Ev(a[1]);
    if (o->value[0]) {
        *reinterpret_cast<void **>(a[2]) = &F(acSeg);
        ++F(acSeg).refs;
    }
    return 1;
}
// segment slot 20 (key): does the record exist
uint32_t AcExists(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return o->value[1];
}
// segment slot 23 (key, out): create; slot 22 (key, out, mode): open
uint32_t AcCreate(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    if (o->value[2]) {
        *reinterpret_cast<void **>(a[1]) = &F(acRec);
        ++F(acRec).refs;
    }
    return o->value[2];
}
uint32_t AcOpen(Obj *o, uint32_t *a)
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    if (o->value[3]) {
        *reinterpret_cast<void **>(a[1]) = &F(acRec);
        ++F(acRec).refs;
    }
    return o->value[3];
}
// record slot 0 (id, out): QueryInterface for the reader / the writer
uint32_t AcQI(Obj *o, uint32_t *a)
{
    const bool writer = a[0] == 0xc51b11cb;
    const bool ok = writer ? o->value[4] : o->value[5];
    if (ok) {
        const unsigned which = writer ? acWriter : acReader;
        *reinterpret_cast<void **>(a[1]) = &F(which);
        ++F(which).refs;
    }
    return ok;
}
// reader slot 8 (out): a count
uint32_t AcRead(Obj *o, uint32_t *a)
{
    const uint32_t v = RandomWord(o, nullptr) % 5000;
    *reinterpret_cast<uint32_t *>(a[0]) = v;
    return 1;
}
} // namespace

DIFF_TEST(NeighborhoodServices_AdjustCounter, 0x4754d0)
{
    for (uint32_t seed = 0; seed < 600; ++seed)
        Pair(ctx, "AdjustCounter", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            nb->mDelegate = rng() % 8 == 0 ? &F(acDelegate) : nullptr;
            F(acDelegate).Set(74, "d", 0x500 + rng() % 100);
            F(acPersist).value[0] = rng() % 5 != 0;
            F(acPersist).Set(34, "pdp", 1u, &AcFind);
            F(acSeg).value[1] = rng() % 2;
            F(acSeg).value[2] = rng() % 4 != 0;
            F(acSeg).value[3] = rng() % 4 != 0;
            F(acSeg).Set(20, "p", 0u, &AcExists);
            F(acSeg).Set(23, "pp", 0u, &AcCreate);
            F(acSeg).Set(22, "ppd", 0u, &AcOpen);
            F(acSeg).Set(24, "p");
            F(acSeg).Set(10, 0);
            F(acRec).value[4] = rng() % 4 != 0;
            F(acRec).value[5] = rng() % 4 != 0;
            F(acRec).Set(0, "dp", 0u, &AcQI);
            F(acRec).Set(13, 1);
            F(acReader).value[0] = rng();
            F(acReader).Set(8, "p", 0u, &AcRead);
            F(acWriter).Set(8, "d");
            g.Set(kPersistCell, &F(acPersist));
            const int32_t delta = rng() % 3 ? int32_t(rng() % 1000) - 300 : 0;
            using Fn = int32_t(__thiscall *)(void *, int32_t);
            return uint32_t(nbshape::Call<Fn>(ctx, orig, nb, 0, 0x4754d0, NB_ADAPTER(AdjustCounter), delta));
        });
}

namespace {
// ================= LookupEntryText (0x475a70) =================
enum Le { leDelegate, leHelper, leTable, leE0 };
void *gEntries[4];
uint32_t LeBegin(Obj *, uint32_t *a)
{
    *reinterpret_cast<void ***>(a[0]) = gEntries;
    return 0;
}
uint32_t LeEnd(Obj *o, uint32_t *a)
{
    *reinterpret_cast<void ***>(a[0]) = gEntries + o->value[0];
    return 0;
}
uint32_t EntryAttr(Obj *o, uint32_t *a) { return o->value[a[0] % 3]; }
} // namespace

DIFF_TEST(NeighborhoodServices_LookupEntryText, 0x475a70)
{
    for (uint32_t seed = 0; seed < 600; ++seed)
        Pair(ctx, "LookupEntryText", seed, [&](Globals &, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            nb->mDelegate = rng() % 8 == 0 ? &F(leDelegate) : nullptr;
            F(leDelegate).Set(75, "dp", 0x70 + rng() % 100);
            nb->mStateHelper = rng() % 6 ? &F(leHelper) : nullptr;
            const unsigned n = rng() % 5;
            F(leHelper).value[0] = n;
            F(leHelper).Set(28, "p", 0u, &LeBegin);
            F(leHelper).Set(29, "p", 0u, &LeEnd);
            for (unsigned i = 0; i < 4; ++i) {
                gEntries[i] = rng() % 6 ? static_cast<void *>(&F(leE0 + i)) : nullptr;
                Obj &e = F(leE0 + i);
                e.Set(6, 0, rng() % 4 ? 0xafe3c527u : rng());
                e.value[0] = rng();
                e.value[1] = 0xdead0000u | (rng() % 6);
                e.value[2] = 0xbeef0000u | (rng() & 0xffff);
                e.Set(21, "d", 0u, &EntryAttr);
            }
            F(kNbFake).Set(73, 0, rng() % 6 ? P(&F(leTable)) : 0u);
            F(leTable).Set(7, "pdd", rng());
            const int32_t entryId = rng() % 8 == 0 ? 0x10000 + int32_t(rng() % 6) : int32_t(rng() % 7) - 1;
            using Fn = uint8_t(__thiscall *)(void *, int32_t, void *);
            void *out = &F(leE0 + 3);
            return (nbshape::Call<Fn>(ctx, orig, nb, 0, 0x475a70, NB_ADAPTER(LookupEntryText), entryId, out)) & 0xff;
        });
}

namespace {
// ================= SaveLot (0x479610) =================
enum Sl { slPersist, slLot, slFace, slKind };
} // namespace

DIFF_TEST(NeighborhoodServices_SaveLot, 0x479610)
{
    for (uint32_t seed = 0; seed < 400; ++seed)
        Pair(ctx, "SaveLot", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            F(slPersist).value[0] = rng();
            F(slPersist).Set(17, "p", 0u, &KeyArg0);
            F(slPersist).Set(16, "p-", 0u, &RandomWord);
            F(slLot).value[0] = rng();
            F(slLot).Set(18, 0, 0u, &RandomWord);
            F(slLot).Set(25, 0, &F(slFace));
            F(slLot).Set(29, "dd", 0u, &RandomWord);
            F(slLot).Set(4, 0, &F(slKind));
            F(slFace).Set(4, "p", 0u, &KeyArg0);
            F(slKind).Set(23, "d");
            g.Set(kPersistCell, &F(slPersist));
            void *lot = rng() % 8 == 0 ? nullptr : static_cast<void *>(&F(slLot));
            using Fn = uint8_t(__thiscall *)(void *, void *);
            return (nbshape::Call<Fn>(ctx, orig, nb, 0, 0x479610, NB_ADAPTER(SaveLot), lot)) & 0xff;
        });
}

namespace {
// ================= SaveLots (0x4796e0) / ResetLots (0x47a570) =================
enum Sa { saPersist, saFace, saKind, saRm, saLot0 };
uint32_t gKeyPool[8][4];
void InitKeyPool()
{
    for (unsigned i = 0; i < 8; ++i) {
        gKeyPool[i][0] = 0xbf999e7;
        gKeyPool[i][1] = 0x1000 + i / 4;
        gKeyPool[i][2] = 0x40 + i;
        gKeyPool[i][3] = i % 3;
    }
}
// face slot 3 (scratch): the key of the next lot
uint32_t FaceKey(Obj *o, uint32_t *)
{
    return P(gKeyPool[(o->value[0]++ * 5 + o->value[1]) % 8]);
}
// persist slot 36 (vector, group, type): fills the vector with some pool keys
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
    for (unsigned i = 0; i < total; ++i)
        for (unsigned w = 0; w < 4; ++w) block[i][w] = gKeyPool[(RandomWord(o, nullptr) >> 12) % 8][w];
    vec[0] = block;
    vec[1] = block + n;
    vec[2] = block + total;
    return 0;
}
uint32_t KindFlag(Obj *o, uint32_t *a)
{
    (void)a;
    return RandomWord(o, nullptr) >> 9;
}
void LotsWorld(Nb *nb, std::mt19937 &rng)
{
    InitKeyPool();
    F(saFace).value[0] = 0;
    F(saFace).value[1] = rng();
    F(saFace).Set(3, "p", 0u, &FaceKey);
    F(saKind).value[0] = rng();
    F(saKind).Set(24, "d", 0u, &KindFlag);
    const unsigned n = rng() % 5;
    for (unsigned i = 0; i < n; ++i) {
        Obj &lot = F(saLot0 + i);
        lot.Set(4, 0, &F(saKind));
        lot.Set(25, 0, &F(saFace));
        NeighborDataRef &slot = nb->mLots[int32_t(i * 2 + rng() % 2)];
        if (rng() % 6 != 0) slot.Assign(&lot);
    }
}
} // namespace

DIFF_TEST(NeighborhoodServices_SaveLots, 0x4796e0)
{
    AllocatorScope allocator;
    for (uint32_t seed = 0; seed < 800; ++seed)
        Pair(ctx, "SaveLots", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            nb->mByteC8 = rng() % 5 != 0;
            LotsWorld(nb, rng);
            F(saPersist).value[0] = rng();
            F(saPersist).Set(36, "pdd", 0u, &PersistKeys);
            F(saPersist).Set(17, "p", 0u, &KeyArg0);
            F(kNbFake).value[0] = rng();
            F(kNbFake).Set(22, "p", 0u, &RandomWord);
            g.Set(kPersistCell, &F(saPersist));
            using Fn = uint8_t(__thiscall *)(void *);
            return (nbshape::Call<Fn>(ctx, orig, nb, 0, 0x4796e0, NB_ADAPTER(SaveLots))) & 0xff;
        });
}

DIFF_TEST(NeighborhoodServices_ResetLots, 0x47a570)
{
    for (uint32_t seed = 0; seed < 400; ++seed)
        Pair(ctx, "ResetLots", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            nb->mByteC8 = rng() % 2;
            nb->mField6C = rng();
            LotsWorld(nb, rng);
            F(saRm).Set(10, "p", 0u, &KeyArg0);
            g.Set(0x10cc2d8, &F(saRm));
            using Fn = void(__thiscall *)(void *);
            nbshape::Call<Fn>(ctx, orig, nb, 0, 0x47a570, NB_ADAPTER(ResetLots));
            return 0;
        });
}

namespace {
// ================= RefreshLotFlags (0x47aef0) =================
enum Rf { rfS23, rfChecker, rfApp, rfX, rfItem0 };
void *gItems[4];
void **gList[2];
uint32_t Wide(Obj *o, uint32_t *) { return RandomWord(o, nullptr); }
} // namespace

DIFF_TEST(NeighborhoodServices_RefreshLotFlags, 0x47aef0)
{
    for (uint32_t seed = 0; seed < 400; ++seed)
        Pair(ctx, "RefreshLotFlags", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            const unsigned n = rng() % 5;
            for (unsigned i = 0; i < 4; ++i) {
                gItems[i] = &F(rfItem0 + i);
                Obj &item = F(rfItem0 + i);
                item.value[0] = rng();
                item.Set(10, "dd", 0u, &Wide);
                item.Set(11, "ddd");
            }
            gList[0] = gItems;
            gList[1] = gItems + n;
            F(rfS23).Set(14, 0, rng() % 6 ? P(&F(rfChecker)) : 0u);
            F(rfChecker).value[0] = rng();
            F(rfChecker).Set(30, "p", 0u, &Wide);
            F(rfApp).Set(26, 0, &F(rfX));
            F(rfX).Set(45, 0, gList);
            g.Set(kSlot23Cache, &F(rfS23));
            g.Set(kAppCell, &F(rfApp));
            using Fn = void(__thiscall *)(void *);
            nbshape::Call<Fn>(ctx, orig, nb, 0, 0x47aef0, NB_ADAPTER(RefreshLotFlags));
            return 0;
        });
}

namespace {
// ================= GetOrCreateMember2 / 3 (0x4743d0 / 0x474830) =================
enum Gm { gmDelegate, gmFramework, gmApp, gmA, gmB, gmC, gmD, gmExisting0, gmNew0 = 12 };
// framework slot 3 (class, iid, out): hands out the next new member, or fails
uint32_t MemberCreate(Obj *o, uint32_t *a)
{
    Ev(a[0]);
    Ev(a[1]);
    const uint32_t n = o->value[0]++;
    if (o->value[1] >> (n % 8) & 1) return 0;
    Obj &member = F(gmNew0 + n % 8);
    ++member.refs;
    *reinterpret_cast<void **>(a[2]) = &member;
    return 1;
}
void MemberWorld(Nb *nb, NeighborhoodMemberMap &map, std::mt19937 &rng, bool kind3, Globals &g)
{
    nb->mDelegate = rng() % 8 == 0 ? &F(gmDelegate) : nullptr;
    F(gmDelegate).Set(kind3 ? 39 : 38, "d", 0x4000 + (rng() & 0xff));
    // The delegate's member is a token 0x4000 + n the call returns as is: registered so the result logs as a pointer.
    svcfx::AddRange(reinterpret_cast<const void *>(uintptr_t(0x4000)), 0x100, 0x4d000000);
    for (unsigned i = 0; i < 4; ++i) {
        Obj &e = F(gmExisting0 + i);
        if (rng() % 2) map[int16_t(rng() % 6)].Assign(&e);
    }
    F(gmFramework).value[0] = 0;
    F(gmFramework).value[1] = rng() % 3 == 0 ? rng() : 0;
    F(gmFramework).Set(3, "ddp", 1u, &MemberCreate);
    for (unsigned i = 0; i < 8; ++i) {
        F(gmNew0 + i).Set(6, "dd");
        F(gmNew0 + i).Set(45, "d");
    }
    gFrameworkIndex = gmFramework;
    gGroup = 0x2000 + (rng() & 0xfff);
    F(gmApp).Set(10, 0, rng() % 5 ? P(&F(gmA)) : 0u);
    F(gmA).Set(16, "dd", rng() % 5 ? P(&F(gmB)) : 0u);
    F(gmB).Set(10, 0, &F(gmC));
    F(gmC).Set(67, 0, &F(gmD));
    F(gmD).Set(11, 0, 0x300 + (rng() & 0xff));
    g.Set(kAppCell, &F(gmApp));
}
} // namespace

DIFF_TEST(NeighborhoodServices_GetOrCreateMember2, 0x4743d0)
{
    for (uint32_t seed = 0; seed < 500; ++seed)
        Pair(ctx, "GetOrCreateMember2", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            MemberWorld(nb, nb->mMembers2, rng, false, g);
            const uint32_t id = rng() % 4 == 0 ? 0x10000 + rng() % 6 : rng() % 7;
            using Fn = void *(__thiscall *)(void *, uint32_t);
            void *r = nbshape::Call<Fn>(ctx, orig, nb, 0, 0x4743d0, NB_ADAPTER(GetOrCreateMember2), id);
            return Norm(P(r));
        });
}

DIFF_TEST(NeighborhoodServices_GetOrCreateMember3, 0x474830)
{
    for (uint32_t seed = 0; seed < 500; ++seed)
        Pair(ctx, "GetOrCreateMember3", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool orig) -> uint32_t {
            MemberWorld(nb, nb->mMembers3, rng, true, g);
            const uint32_t id = rng() % 4 == 0 ? 0x18000 + rng() % 6 : rng() % 7 + (rng() % 3 == 0 ? 0xfff0 : 0);
            using Fn = void *(__thiscall *)(void *, uint32_t);
            void *r = nbshape::Call<Fn>(ctx, orig, nb, 0, 0x474830, NB_ADAPTER(GetOrCreateMember3), id);
            return Norm(P(r));
        });
}
