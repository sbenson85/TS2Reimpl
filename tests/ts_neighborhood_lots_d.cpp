// Differential tests for cTSNeighborhood::AddLot (0x4730b0) and RemoveLot (0x478a70), lane CQ. Fixture: tests/ts_neighborhood_lots_d.h
// (a Construct()-built neighbourhood with recording slots, fakes for the lot, state controller, persist system, message server and
// the common implementation service; both sides see the same seeded world; return, event log, maps and fakes are compared).
#include "ts_neighborhood_lots_d.h"

namespace {
using namespace svcfx;
using namespace lotsd;

// 1 lot, 2 its base info, 3 persist system, 4 state controller, 5 neighbourhood info, 6 persist context, 7 common implementation
// service, 8 segment, 9 message server, 10 group data, 11 owner info, 12.. other lots
cRZString gLotName, gPrefix, gFileText, gNewFile;

uint32_t LotNumber(Obj *o, uint32_t *) { return o->value[0]; }
uint32_t SetLotNumber(Obj *o, uint32_t *a)
{
    o->value[0] = a[0];
    return 0;
}
uint32_t FormatFile(Obj *, uint32_t *a)   // FormatLotFileName(out, lot, info)
{
    reinterpret_cast<cRZString *>(a[0])->mString = "lot" + std::to_string(F(1).value[0]) + ".package";
    return 1;
}
uint32_t SetFileName(Obj *, uint32_t *a) { return TextArg0(nullptr, a); }
uint32_t CommonOption(Obj *, uint32_t *a)
{
    reinterpret_cast<cRZString *>(a[1])->mString = "Option";
    return 1;
}
uint32_t OpenSegment(Obj *self, uint32_t *a)   // (iid, out, 1, description, path, 1, 1)
{
    Ev(Hash(reinterpret_cast<const cRZString *>(a[3])->Data()));
    Ev(Hash(reinterpret_cast<const cRZString *>(a[4])->Data()));
    if (self->value[0]) {
        ++F(8).refs;
        *reinterpret_cast<void **>(a[1]) = &F(8);
    }
    return self->value[1];
}

void LotWorld(Globals &g, Nb *nb, std::mt19937 &rng)
{
    gLotName = MakeString("Maple Court");
    gPrefix = MakeString("N001");
    gFileText = MakeString("current.package");
    F(kNbFake).Set(68, 0, 40 + rng() % 5);
    F(kNbFake).Set(70, "ppp", 0, FormatFile);
    nb->mOwner = &F(11);
    F(1).Set(18, 0, 0, LotNumber);
    F(1).Set(17, "d", 0, SetLotNumber);
    F(1).Set(4, 0, P(&F(2)));
    F(1).Set(6, "p", 0, SetFileName);
    F(1).Set(8, 0, P(&gFileText));
    F(2).Set(19, 0, P(&gLotName));
    F(4).Set(6, 0, P(&F(5)));
    F(4).Set(34, "d", P(&F(6)));
    F(5).Set(8, 0, P(&gPrefix));
    F(6).Set(6, "p", 0, KeyArg0);
    F(7).Set(4, "dpd-d", 0, CommonOption);
    g.Set(kSlot23Cache, &F(4));
    g.Set(kPersistCell, &F(3));
    g.Set(0x10cc040, &F(7));
    g.Set(kMessageServerCell, &F(9));
    UseAsMessageServer2(F(9));
}
} // namespace

// ================= AddLot (0x4730b0) =================
DIFF_TEST(NeighborhoodLotsD_AddLot, 0x4730b0)
{
    AllocatorScope allocator;
    using Fn = uint8_t(__thiscall *)(Nb *, void *);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 320; ++seed) {
        if (Pair(ctx, "AddLot", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                LotWorld(g, nb, rng);
                const uint32_t numbers[] = {0, 0xffffffffu, 5, 7, 0, 41, 0xffffffffu, 12};
                F(1).value[0] = numbers[seed % 8];
                F(3).value[0] = (seed % 5) != 0;           // OpenSegment hands out a segment
                F(3).value[1] = rng();
                F(3).Set(22, "dpdppdd", 0, OpenSegment);
                F(10).Set(11, "d");
                nb->mSecondOwner = &F(10);
                // Lots and names that exist already (the inserts then fail).
                if (rng() % 3 == 0) nb->mLots[int32_t(numbers[seed % 8] ? numbers[seed % 8] : 40 + rng() % 5)].Assign(&F(12));
                if (rng() % 3 == 0) {
                    cRZString same = MakeString((rng() & 1) ? "MAPLE COURT" : "Elm");
                    nb->mTexts.insert(NeighborhoodTextEntry(*same.AsInterface(), &F(13)));
                }
                nb->mField6C = 1 + rng() % 3;
                return uint32_t(nbshape::Call<Fn>(ctx, original, nb, 0, 0x4730b0, NB_ADAPTER(AddLot), &F(1)));
            }))
            ++ok;
    }
    std::printf("  AddLot: %u worlds ok\n", ok);
}

// ================= RemoveLot (0x478a70) =================
DIFF_TEST(NeighborhoodLotsD_RemoveLot, 0x478a70)
{
    AllocatorScope allocator;
    using Fn = uint8_t(__thiscall *)(Nb *, int32_t);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 320; ++seed) {
        if (Pair(ctx, "RemoveLot", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                LotWorld(g, nb, rng);
                F(3).Set(25, "pd", 1, [](Obj *, uint32_t *a) -> uint32_t {
                    Ev(Hash(reinterpret_cast<const cRZString *>(a[0])->Data()));
                    Ev(a[1]);
                    return 1;
                });
                F(10).Set(10, "d");
                if (seed % 4) nb->mSecondOwner = &F(10);
                const int32_t number = (seed % 3 == 0) ? 0x10003 : int32_t(rng() % 6) - 1;
                F(1).value[0] = uint32_t(number);
                if (seed % 7 != 0) nb->mLots[number].Assign(&F(1));
                for (int i = 0; i < 3; ++i)
                    if (rng() % 2) nb->mLots[int32_t(10 + i)].Assign(&F(12 + i));
                const char *names[] = {"Maple Court", "maple court", "Elm", "Oak"};
                for (unsigned i = 0; i < 4; ++i) {
                    if (rng() % 3 == 0) continue;
                    cRZString text = MakeString(names[i]);
                    nb->mTexts.insert(NeighborhoodTextEntry(*text.AsInterface(), &F(15 + i)));
                }
                if (rng() % 2) nb->mMembers1[int16_t(number)].Assign(&F(20));
                if (rng() % 2) nb->mMembers1[int16_t(number + 1)].Assign(&F(21));
                const int32_t asked = (seed % 11 == 0) ? number + 100 : number;
                return uint32_t(nbshape::Call<Fn>(ctx, original, nb, 0, 0x478a70, NB_ADAPTER(RemoveLot), asked));
            }))
            ++ok;
    }
    std::printf("  RemoveLot: %u worlds ok\n", ok);
}
