// Differential tests for the cTSNeighborhood members written in lane CL: the person / neighbour sync table and its users
// (0x4b8ff0, 0x4b8fd0, 0x4b8f80, GetSyncTableEntry 0x475310, CopyNeighborToPerson 0x4768e0, CopyPersonToNeighbor 0x479850) and the
// lot members that follow. Fixture: tests/ts_services_fixture.h (recording fakes; both sides see the same seeded world).
#include "ts_services_fixture.h"

#include "rz/RZMemory.h"
#include "rz/cRZString.h"
#include "ts/cTSNeighborhood.h"
#include "ts_neighborhood_shape.h"

#include <random>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{1, "d"}, {3, "dddd---d"}, {4, "dddd---d"}, {5, "dddd---d"}, {7, "-d"}};
using namespace svcfx;
using Nb = cTSNeighborhood;

struct NbMem {
    alignas(8) uint8_t bytes[sizeof(Nb)];
    Nb *Get() { return reinterpret_cast<Nb *>(bytes); }
};

// A world: a constructed neighbourhood (its vtable is the game's), the fakes plugged into the app singleton cell, and a seeded rng.
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
            std::memset(mem.bytes, 0, sizeof(mem.bytes));
            nbshape::Build(ctx, side == 0, mem.bytes);
            AddRange(mem.Get(), sizeof(Nb), 0xe0000000);
            Blob &b = blobs[side];
            const uint32_t r = world(g, mem.Get(), rng, side == 0);
            b.push_back(r);
            b.insert(b.end(), gEv.begin(), gEv.end());
            CaptureFakes(b, kValueKinds);
            nbshape::Destroy(ctx, side == 0, mem.Get());
        }
        same = Same(ctx, blobs[0], blobs[1], what, seed | (route ? 0x80000000u : 0)) && same;
    }
    nbshape::gRoute = nbshape::kVTableRoute;
    return same;
}
} // namespace

// ================= the sync table (0x4b8ff0, 0x4b8fd0, 0x4b8f80, 0x475310) =================
DIFF_TEST(NeighborhoodLots_SyncTable, 0x4b8f80)
{
    const nbshape::SyncTableGlobal syncTable(ctx);
    using TableFn = const NeighborSyncTable *(__cdecl *)();
    using CountFn = uint32_t(__cdecl *)();
    using EntryFn = uint8_t(__cdecl *)(int32_t, uint16_t *, uint16_t *);
    using SlotFn = uint8_t(__thiscall *)(Nb *, int32_t, uint16_t *, uint16_t *);
    // The original builds its table in a global the first time it is asked for.
    const NeighborSyncTable *theirs = ctx.Original<TableFn>(0x4b8ff0)();
    const NeighborSyncTable *ours = GetNeighborSyncTable();
    ++ctx.cases;
    const size_t count = static_cast<size_t>(theirs->mLast - theirs->mFirst);
    if (count != static_cast<size_t>(ours->mLast - ours->mFirst) || count != 169 ||
        std::memcmp(theirs->mFirst, ours->mFirst, count * 4) != 0)
        ctx.Fail("table contents differ (original %u entries)", unsigned(count));
    ++ctx.cases;
    if (ctx.Original<CountFn>(0x4b8fd0)() != GetNeighborSyncCount() || GetNeighborSyncCount() != count) ctx.Fail("count");
    NbMem mem;
    std::memset(mem.bytes, 0, sizeof(mem.bytes));
    std::mt19937 rng(7);
    std::vector<int32_t> indices = {-1, 0, 1, 2, int32_t(count) - 1, int32_t(count), int32_t(count) + 1, int32_t(count) + 100, INT32_MIN,
                                    INT32_MAX, -100000, 0x10000, 0x40000000};
    for (int i = 0; i < 400; ++i) indices.push_back(int32_t(rng() % (count + 40)) - 5);
    for (int32_t index : indices) {
        uint16_t of[2] = {0x1111, 0x2222}, oe[2] = {0x3333, 0x4444}, uf[2] = {0x1111, 0x2222}, ue[2] = {0x3333, 0x4444}, sf[2] = {0x1111, 0x2222},
                 se[2] = {0x3333, 0x4444};
        const uint8_t a = ctx.Original<EntryFn>(0x4b8f80)(index, &of[0], &oe[0]);
        const uint8_t b = GetNeighborSyncEntry(index, &uf[0], &ue[0]);
        // Ours through our vtable's slot 30 and through the adapter (`this` unused).
        const uint8_t c = reinterpret_cast<SlotFn>(nbshape::Ours()[0][30])(mem.Get(), index, &sf[0], &se[0]);
        uint16_t af = 0x1111, ae = 0x3333;
        const uint8_t e = cTSNeighborhoodAdapters::GetSyncTableEntry(mem.Get(), nullptr, index, &af, &ae);
        if (e != c || af != sf[0] || ae != se[0]) ctx.Fail("entry %d: adapter differs", index);
        const uint8_t d = ctx.Original<SlotFn>(0x475310)(mem.Get(), index, &of[1], &oe[1]);
        ++ctx.cases;
        if (a != b || a != c || a != d || std::memcmp(of, uf, 2) != 0 || std::memcmp(oe, ue, 2) != 0 || sf[0] != uf[0] || se[0] != ue[0] ||
            of[1] != sf[0] || oe[1] != se[0])
            ctx.Fail("entry %d: return %u/%u/%u, field %04x/%04x, extra %04x/%04x", index, a, b, c, of[0], uf[0], oe[0], ue[0]);
    }
    std::printf("  SyncTable: %zu entries, %zu indices\n", count, indices.size());
}

// ================= CopyNeighborToPerson (0x4768e0) / CopyPersonToNeighbor (0x479850) =================
namespace {
// Fakes: 0 app, 1 neighbour manager, 2 want/fear service, 3 neighbour found, 4 neighbour created, 5 person, 6 its sim object, 7 its info.
constexpr unsigned kApp = 0, kManager = 1, kWantFear = 2, kFound = 3, kCreated = 4, kPerson = 5, kSimObject = 6, kInfo = 7;
void *gPersonVT[200];

uint32_t Garbage(Obj *o) { return o->value[7] = o->value[7] * 1664525u + 1013904223u; }
// Sixteen-bit results come back with garbage in the upper half of eax.
uint32_t Wide16(Obj *o, uint32_t low) { return (Garbage(o) & 0xffff0000u) | (low & 0xffff); }
uint32_t NeighborId(Obj *o, uint32_t *) { return Wide16(o, o->value[0]); }
uint32_t NeighborAttr(Obj *o, uint32_t *a) { return Wide16(o, a[0] * 31u + o->value[1]); }
uint32_t PersonData(Obj *o, uint32_t *a) { return Wide16(o, a[0] * 17u + o->value[1] + a[1]); }
uint32_t NeighborExtra(Obj *o, uint32_t *a) { return o->value[2 + (a[0] & 1)]; }
uint32_t ManagerFind(Obj *o, uint32_t *a) { return a[0] == o->value[0] ? P(&F(kFound)) : 0; }
uint32_t ManagerGet(Obj *o, uint32_t *a) { return int16_t(a[0]) == int16_t(o->value[0]) ? P(&F(kFound)) : 0; }
uint32_t PersonRow(Obj *o, uint32_t *) { return o->value[0]; }

uint32_t __fastcall PersonGetExtra(Obj *self, void *, uint32_t which)
{
    Ev(evCall); Ev(self->id); Ev(170); Ev(1); Ev(which);
    return self->value[2 + (which & 1)];
}
void __fastcall PersonSetExtra(Obj *self, void *, uint32_t which, uint32_t value)
{
    Ev(evCall); Ev(self->id); Ev(171); Ev(2); Ev(which); Ev(value); // the extra: data
    self->value[2 + (which & 1)] = value;
}

void BuildSimWorld(Globals &g, std::mt19937 &rng, bool found, bool created)
{
    const uint32_t key = rng();
    F(kApp).Set(10, 0, P(&F(kManager)));
    F(kApp).Set(4, 0, P(&F(kWantFear)));
    F(kWantFear).Set(6, "d");
    F(kWantFear).Set(7, "d");
    F(kManager).value[0] = (rng() & 1) ? key : (key ^ 0x10000);
    F(kManager).Set(17, "d", 0, found ? ManagerFind : nullptr);
    if (!found) F(kManager).value[0] = ~key;
    F(kManager).Set(29, "p", created ? P(&F(kCreated)) : 0);
    F(kManager).Set(16, "dd", 0, ManagerGet);
    for (unsigned n : {kFound, kCreated}) {
        F(n).value[0] = rng();
        F(n).value[1] = rng();
        F(n).value[2] = rng();
        F(n).value[3] = rng();
        F(n).Set(8, 0, 0, NeighborId);
        F(n).Set(16, "d", 0, NeighborAttr);
        F(n).Set(17, "dd");
        F(n).Set(51, "d", 0, NeighborExtra);
        F(n).Set(52, "dd");
    }
    F(kPerson).value[0] = rng();   // the sim row (garbage in the upper half)
    F(kPerson).value[1] = rng();
    F(kPerson).value[2] = rng();
    F(kPerson).value[3] = rng();
    F(kPerson).Set(8, 0, P(&F(kSimObject)));
    F(kPerson).Set(10, "dd", 0, PersonData);
    F(kPerson).Set(11, "ddd");
    F(kPerson).Set(57, 0, 0, PersonRow);
    F(kPerson).Set(58, "d");
    for (unsigned i = 0; i < kSlots; ++i) gPersonVT[i] = F(kPerson).table[i];
    gPersonVT[170] = reinterpret_cast<void *>(&PersonGetExtra);
    gPersonVT[171] = reinterpret_cast<void *>(&PersonSetExtra);
    F(kPerson).vt = gPersonVT;
    F(kSimObject).Set(61, 0, P(&F(kInfo)));
    F(kInfo).Set(28, 0, key);
    if (found) F(kManager).value[0] = key;
    if (!found) F(kManager).value[0] = key ^ 0x5a5a;
    F(kInfo).value[1] = key;
    g.Set(kAppCell, &F(kApp));
}
} // namespace

DIFF_TEST(NeighborhoodLots_CopyNeighborToPerson, 0x4768e0)
{
    const nbshape::SyncTableGlobal syncTable(ctx);
    using Fn = void(__thiscall *)(Nb *, void *);
    unsigned cases = 0;
    for (uint32_t seed = 0; seed < 120; ++seed) {
        const bool found = (seed % 3) != 0, created = (seed % 4) != 0;
        if (Pair(ctx, "CopyNeighborToPerson", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                BuildSimWorld(g, rng, found, created);
                nbshape::Call<Fn>(ctx, original, nb, 0, 0x4768e0, NB_ADAPTER(CopyNeighborToPerson), &F(kPerson));
                return 0;
            }))
            ++cases;
    }
    std::printf("  CopyNeighborToPerson: %u worlds ok\n", cases);
}

DIFF_TEST(NeighborhoodLots_CopyPersonToNeighbor, 0x479850)
{
    const nbshape::SyncTableGlobal syncTable(ctx);
    using Fn = void(__thiscall *)(Nb *, void *);
    unsigned cases = 0;
    for (uint32_t seed = 0; seed < 120; ++seed) {
        const bool found = (seed % 3) != 0;
        if (Pair(ctx, "CopyPersonToNeighbor", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                BuildSimWorld(g, rng, true, true);
                // The neighbour is found when the sim row (signed 16 bits) matches the manager's key.
                F(kManager).value[0] = found ? F(kPerson).value[0] ^ (uint32_t(rng()) & 0xffff0000u) : ~F(kPerson).value[0];
                nbshape::Call<Fn>(ctx, original, nb, 0, 0x479850, NB_ADAPTER(CopyPersonToNeighbor), &F(kPerson));
                return 0;
            }))
            ++cases;
    }
    std::printf("  CopyPersonToNeighbor: %u worlds ok\n", cases);
}
