// Differential test for cTSNeighborhood::ValidateInventories (0x478ed0), lane CQ. Fixture: tests/ts_neighborhood_lots_d.h.
// The original callee 0x4eaa70 runs for real on both sides (the persist fake answers nothing, so it only logs the group key).
// Fakes: 0 app, 1 family manager, 2 neighbour manager, 3 global inventory, 4 persist resource / persist system, 5 a family,
// 6 and 7 lots, 8 and 9 neighbours, 10 person data, 11 kind info, 12 owner info, 13..18 inventories, 19 state helper,
// 20 framework, 22 delegate, 23 the neighbourhood's own recording slots.
#include "ts_neighborhood_lots_d.h"

#include "gz/cGZDBSegmentPackedFile.h"
#include "simantics/ServiceRefVector.h"

#include <set>

namespace {
using namespace svcfx;
using namespace lotsd;

using KnownIds = std::set<uint32_t, std::less<uint32_t>, GZServiceAllocator<uint32_t>>;

uint32_t FromValue0b(Obj *o, uint32_t *) { return o->value[0]; }
uint32_t Validate(Obj *self, uint32_t *a)   // ValidateTokens(set, a, b, c)
{
    if (self->value[0] & 1) reinterpret_cast<KnownIds *>(a[0])->insert(self->id);
    if (self->value[0] & 2) reinterpret_cast<KnownIds *>(a[0])->insert(self->id + 100);
    return 0;
}
uint32_t GetFamily(Obj *self, uint32_t *a) { return ((self->value[0] >> (a[0] & 7)) & 1) ? P(&F(5)) : 0; }
uint32_t GetFamilyIds(Obj *self, uint32_t *a)
{
    const uint32_t count = self->value[1];
    auto *vec = reinterpret_cast<ServiceBlockVector *>(a[0]);
    if (count) {
        auto *block = static_cast<int16_t *>(RZMemory::Malloc(count * 2));
        for (uint32_t i = 0; i < count; ++i) block[i] = int16_t(self->value[2] >> (i * 4)) & 7;
        vec->mFirst = block;
        vec->mLast = block + count;
        vec->mEnd = block + count;
    }
    return 0;
}
uint32_t GetNeighbor(Obj *self, uint32_t *a)   // (key, includeAll): asked twice in a row per key
{
    const uint32_t n = ++self->value[1];
    const uint32_t key = a[0] & 7;
    if ((n & 1) == 0 && ((self->value[2] >> key) & 1)) return 0;   // the second answer is null
    if ((self->value[3] >> key) & 1) return 0;                      // the first one too
    return P(&F(8 + (key & 1)));
}
uint32_t PersonData(Obj *self, uint32_t *)
{
    F(10).value[0] = self->id;
    return (self->value[0] & 1) ? 0 : P(&F(10));
}
uint32_t KindInfo(Obj *self, uint32_t *) { return (F(10).value[1] & 1) ? 0 : P(&F(11)); }
uint32_t Kind(Obj *self, uint32_t *a)
{
    Ev(a[0]);
    return ((self->value[0] >> (F(10).value[0] - 200)) & 1) ? 0x10 : 3;
}
uint32_t OwnerInfo(Obj *, uint32_t *) { return (F(10).value[1] & 2) ? 0 : P(&F(12)); }
uint32_t PersistKey(Obj *, uint32_t *a)   // the persist system's resource lookup: logs the key, finds nothing
{
    LogKey(reinterpret_cast<const void *>(a[0]));
    return 0;
}
uint32_t Create(Obj *, uint32_t *a)
{
    ++F(19).refs;
    *reinterpret_cast<void **>(a[2]) = &F(19);
    return 1;
}
} // namespace

DIFF_TEST(NeighborhoodLotsD_ValidateInventories, 0x478ed0)
{
    AllocatorScope allocator;
    using Fn = void(__thiscall *)(Nb *);
    unsigned ok = 0;
    for (uint32_t seed = 0; seed < 500; ++seed) {
        if (Pair(ctx, "ValidateInventories", seed, [&](Globals &g, Nb *nb, std::mt19937 &rng, bool original) {
                g.Set(kAppCell, &F(0));
                g.Set(kPersistCell, &F(4));
                gFrameworkIndex = 20;
                F(20).Set(3, "ddp", 0, Create);
                F(kNbFake).Set(38, "d"); // the neighbourhood's own slot 38 (NbSlot): its argument is data
                F(kNbFake).Set(37, "d"); // and slot 37: its argument is data too
                F(0).Set(9, 0, P(&F(1)));
                F(0).Set(10, 0, P(&F(2)));
                F(0).Set(17, 0, (seed % 4 == 0) ? 0 : P(&F(3)));
                F(1).value[0] = rng();            // which family ids exist
                F(1).value[1] = rng() % 5;        // how many ids GetFamilyIds hands out
                F(1).value[2] = rng();
                F(1).Set(11, "d", 0, GetFamily);
                F(1).Set(19, "p", 0, GetFamilyIds);
                F(2).value[2] = rng();            // second answers that are null
                F(2).value[3] = rng() & rng();    // first answers that are null
                F(2).Set(33, 0, P(&F(4)));
                F(2).Set(16, "dd", 0, GetNeighbor);
                F(2).Set(30, 0);
                F(4).value[0] = (seed % 3 == 0) ? gGroup : 0x2222;   // the group the neighbour manager / delegate answer
                F(4).Set(6, 0, 0, FromValue0b);
                F(4).Set(34, "pdp", 0, PersistKey);
                F(5).Set(41, 0, 0, FromValue0b);
                F(5).value[0] = 0x300 + (seed & 3);
                for (unsigned i = 0; i < 2; ++i) {
                    Obj &lot = F(6 + i);
                    lot.value[0] = (i == 0) ? 0 : 3 + (seed & 1);
                    lot.Set(18, 0, 0, FromValue0b);
                    nb->mLots[int32_t(1 + 2 * i)].Assign(&lot);
                }
                for (unsigned i = 0; i < 2; ++i) {
                    Obj &neighbor = F(8 + i);
                    neighbor.value[0] = rng() % 4 == 0;
                    neighbor.Set(10, 0, 0, PersonData);
                }
                F(10).value[1] = (seed % 5 == 0) ? 1 : (seed % 7 == 0) ? 2 : 0;
                F(10).Set(4, 0, 0, KindInfo);
                F(10).Set(67, 0, 0, OwnerInfo);
                F(11).value[0] = rng();
                F(11).Set(3, "d", 0, Kind);
                F(12).value[0] = 0x900 + (seed & 3);
                F(12).Set(11, 0, 0, FromValue0b);
                for (unsigned i = 0; i < 7; ++i) {
                    Obj &inv = F(i == 6 ? 19 : 13 + i);
                    inv.value[0] = rng() & 3;
                    inv.Set(45, "d");
                    inv.Set(47, "pppp", 0, Validate);
                    if (i == 6) inv.Set(6, 2);
                }
                F(3).Set(45, 1);
                F(3).Set(47, "pppp", 0, Validate);
                F(3).value[0] = rng() & 3;
                if (seed % 7 != 3) nb->mStateHelper = &F(19);   // else the framework makes it
                if (seed % 7 != 3) ++F(19).refs;
                nb->mByteC8 = (seed % 6) != 0;
                for (int16_t key : {int16_t(1), int16_t(2), int16_t(3), int16_t(5)})
                    if (rng() % 3) nb->mMembers1[key].Assign(&F(13 + rng() % 3));
                if (rng() % 3) nb->mMembers1[int16_t(1)];   // a null value
                for (int16_t key : {int16_t(0), int16_t(1), int16_t(2), int16_t(3)})
                    if (rng() % 3) nb->mMembers2[key].Assign(&F(15 + rng() % 3));
                for (int16_t key : {int16_t(1), int16_t(2), int16_t(3), int16_t(4)})
                    if (rng() % 3) nb->mMembers3[key].Assign(&F(16 + rng() % 3));
                if (seed % 5 == 4) {
                    nb->mDelegate = &F(22);
                    F(22).Set(35, 0);
                    F(22).Set(16, 0, P(&F(4)));
                }
                nbshape::Call<Fn>(ctx, original, nb, 0, 0x478ed0, NB_ADAPTER(ValidateInventories));
                return 0;
            }))
            ++ok;
    }
    std::printf("  ValidateInventories: %u worlds ok\n", ok);
}
