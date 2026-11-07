// Differential tests for cTSFamily::CountMembers (0x49d950) and CollectFilteredObjectsOfGroup1 (0x49e5c0).
#include "ts_family_services.h"
#include "rz/RZMemory.h"
#include "simantics/ServiceRefVector.h"

#include <iterator>

using namespace tsfam;
using namespace tsfam::svc;

namespace {
constexpr unsigned kSeeds = 300;
uint32_t P(const void *p) { return uint32_t(uintptr_t(p)); }
} // namespace

DIFF_TEST(Family_CountMembers, 0x49d950)
{
    // CountMembers calls 0x54ab60 / 0x54aa00 (not replaced) on both sides. Over these fakes 0x54ab60 asks the neighbor
    // manager's neighbor (slot 17) its slot 16 with the kind; 0x54aa00 stops at the missing lot manager (singleton slot 6)
    // and adds 0, so the flag-4/8 additions are exercised only with 0.
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 values(seed + 13);
        const uint32_t flags = values() % 4 ? values() % 16 : values();
        Pair2(ctx, "CountMembers", seed, 0xf,
            [](std::mt19937 &rng) {
                Reset(&S(kLot), &S(kHousehold));
                S(kApp).Set(26, 0, &S(kHelperB));
                S(kHelperB).Set(43, 1, 0u);
                S(kApp).Set(10, 0, &S(kNeighbors));
                S(kApp).Set(6, 0, 0u);
                S(kNeighbors).Set(17, 1, 0, [](SObj *, uint32_t *args) -> uint32_t {
                    return P(args[0] % 3 == 0 ? nullptr : args[0] % 3 == 1 ? &S(kNeighborA) : &S(kNeighborB));
                });
                S(kNeighborA).Set(16, 1, rng() % 2);
                S(kNeighborB).Set(16, 1, rng() % 2 ? 1u : rng());
            },
            [&](bool original) -> uint32_t {
                return Call<uint32_t(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49d950, FAM_ADAPTER(CountMembers), flags);
            });
    }
}

namespace {
// The filter: member objects answer 0x0c1fe23e by their `out[0]` low bit (with themselves, counted).
uint8_t __fastcall FilterQI(Obj *self, void *, uint32_t id, void **out)
{
    Ev({evQI, self->id, id});
    if (!(self->out[0] & 1)) return 0;
    *out = self;
    ++self->refs;
    return 1;
}
} // namespace

DIFF_TEST(Family_CollectFilteredObjectsOfGroup1, 0x49e5c0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair2(ctx, "CollectFilteredObjectsOfGroup1", seed, 0xf,
            [](std::mt19937 &rng) {
                Reset(&S(kLot), &S(kHousehold));
                // A null entry would crash the original: none in the first map.
                Family *f = F();
                for (auto it = f->mGroup1First.begin(); it != f->mGroup1First.end();)
                    it = it->second.Get() ? std::next(it) : f->mGroup1First.erase(it);
                for (auto &o : W().objs) o.refs = 1000, o.vt[0] = reinterpret_cast<void *>(&FilterQI);
                (void)rng;
            },
            [&](bool original) -> uint32_t {
                ServiceRefVector vector;
                vector.mFirst = vector.mLast = vector.mEnd = nullptr;
                if (seed % 3 == 0) {
                    vector.mFirst = static_cast<void **>(RZMemory::Malloc(16));
                    vector.mLast = vector.mFirst;
                    vector.mEnd = vector.mFirst + 4;
                }
                Call<void(__thiscall *)(Family *, ServiceRefVector *)>(ctx, original, 0x49e5c0, FAM_ADAPTER(CollectFilteredObjectsOfGroup1), &vector);
                uint32_t digest = vector.Size() * 1000 + uint32_t(vector.mEnd - vector.mFirst);
                for (void **it = vector.mFirst; it != vector.mLast; ++it) digest = digest * 31 + Id(*it);
                vector.ReleaseAndFree();
                return digest;
            });
    }
}
