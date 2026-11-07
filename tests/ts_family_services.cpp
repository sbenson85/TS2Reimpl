// Differential tests for the cTSFamily members that use the game's services: funds, lot worth, the registry and neighbor
// manager hooks, resource forgetting and the record fetch. Fakes: ts_family_services_world.cpp.
#include "ts_family_services.h"

using namespace tsfam;
using namespace tsfam::svc;

namespace {
constexpr unsigned kSeeds = 300;
uint32_t P(const void *p) { return uint32_t(uintptr_t(p)); }

// The current lot / active family / live household chain the funds and worth follow. `strict` keeps the pointers the
// originals do not check non-null.
void SetupActiveFamily(std::mt19937 &rng, bool strict)
{
    const bool cachedLot = rng() % 4 != 0, cachedHousehold = rng() % 4 != 0;
    Reset(cachedLot ? &S(kLot) : nullptr, cachedHousehold ? &S(kHousehold) : nullptr);
    S(kApp).Set(3, 0, strict || rng() % 6 ? &S(kHousehold) : nullptr);
    S(kApp).Set(23, 0, strict || rng() % 5 ? &S(kLot) : nullptr);
    S(kLot).Set(14, 0, rng() % 5 ? &S(kActive) : nullptr);
    S(kLot).Set(5, 0, rng() % 4 ? 0x0bea28c2u : rng() % 2 ? 0x2c1805b0u : rng());
    S(kActive).Set(41, 0, rng() % 3 ? F()->mFamilyId : rng() % 2 ? F()->mFamilyId + 1 : rng());
    S(kHousehold).Set(29, 0, rng());
    S(kHousehold).Set(30, 1);
    S(kHousehold).Set(40, 1, rng());
}
} // namespace

DIFF_TEST(Family_Funds, 0x49e110)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair2(ctx, "GetFunds", seed, 0, [](std::mt19937 &rng) { SetupActiveFamily(rng, true); }, [&](bool original) -> uint32_t {
            return Call<int32_t(__thiscall *)(Family *)>(ctx, original, 0x49e110, FAM_ADAPTER(GetFunds));
        });
        std::mt19937 values(seed);
        const int32_t funds = values() % 3 ? int32_t(values() % 100000) : int32_t(values());
        Pair2(ctx, "SetFunds", seed, 0, [](std::mt19937 &rng) { SetupActiveFamily(rng, false); }, [&](bool original) -> uint32_t {
            Call<void(__thiscall *)(Family *, int32_t)>(ctx, original, 0x4a0c80, FAM_ADAPTER(SetFunds), funds);
            return 0;
        });
    }
}

namespace {
uint32_t LotInfo(SObj *self, uint32_t *args)
{
    // Writes the three outputs (a dword, two bytes) and answers what value[0] says; the outputs' layout is recorded.
    Ev({args[2] - args[1], args[3] - args[1]});
    *reinterpret_cast<uint32_t *>(uintptr_t(args[1])) = self->value[1];
    *reinterpret_cast<uint8_t *>(uintptr_t(args[2])) = uint8_t(self->value[2]);
    *reinterpret_cast<uint8_t *>(uintptr_t(args[3])) = uint8_t(self->value[3]);
    return self->value[0];
}
} // namespace

DIFF_TEST(Family_GetHomeLotWorth, 0x49f4c0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair2(ctx, "GetHomeLotWorth", seed, 0,
            [](std::mt19937 &rng) {
                SetupActiveFamily(rng, true);
                S(kApp).Set(6, 0, &S(kLotManager));
                S(kLotManager).Set(57, 1, rng() % 3 ? &S(kLotObject) : nullptr);
                S(kLotObject).Set(32, 0, rng() % 3 ? 9u : rng() % 12);
                const bool writes = rng() % 4 != 0;
                S(kLotManager).Set(23, 5, 0, writes ? &LotInfo : nullptr);
                if (!writes) S(kLotManager).ret[23] = rng() % 2;
                S(kLotManager).value[0] = rng() % 3 ? 1 : rng() % 2 ? 0 : 0x100;
                S(kLotManager).value[1] = rng();
                S(kLotManager).value[2] = rng();
                S(kLotManager).value[3] = rng();
            },
            [&](bool original) -> uint32_t {
                return Call<uint32_t(__thiscall *)(Family *)>(ctx, original, 0x49f4c0, FAM_ADAPTER(GetHomeLotWorth));
            });
    }
}

DIFF_TEST(Family_RegistryAndNeighbors, 0x49f480)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair2(ctx, "GetRegistryEntry", seed, 0,
            [](std::mt19937 &rng) {
                Reset(&S(kLot), &S(kHousehold));
                if (rng() % 6 == 0) *Cache(0x10cc288) = 0;
                S(kApp).Set(9, 0, rng() % 4 ? &S(kRegistry) : nullptr);
                S(kRegistry).Set(8, 1, rng());
            },
            [&](bool original) -> uint32_t {
                const uint32_t r = Call<uint32_t(__thiscall *)(Family *)>(ctx, original, 0x49f480, FAM_ADAPTER(GetRegistryEntry));
                *reinterpret_cast<void **>(Cache(0x10cc288)) = &S(kApp);
                return r;
            });
        Pair2(ctx, "FlushChanges", seed, 0xf,
            [](std::mt19937 &rng) {
                Reset(&S(kLot), &S(kHousehold));
                if (rng() % 6 == 0) *Cache(0x10cc288) = 0;
                F()->mFlag79 = uint8_t(rng() % 3 ? rng() % 2 : rng());
                S(kApp).Set(10, 0, rng() % 4 ? &S(kNeighbors) : nullptr);
                S(kNeighbors).Set(26, 1);
            },
            [&](bool original) -> uint32_t {
                Call<void(__thiscall *)(Family *)>(ctx, original, 0x49e0d0, FAM_ADAPTER(FlushChanges));
                *reinterpret_cast<void **>(Cache(0x10cc288)) = &S(kApp);
                return 0;
            });
        std::mt19937 values(seed + 5);
        const uint32_t shortId = values() % 3 ? (values() % 2 ? 0x77 : 0x30077) : values();
        Pair2(ctx, "FindMemberLot", seed, 0xf,
            [](std::mt19937 &rng) {
                Reset(&S(kLot), &S(kHousehold));
                S(kApp).Set(10, 0, &S(kNeighbors));
                S(kNeighbors).Set(17, 1, 0, [](SObj *, uint32_t *args) -> uint32_t {
                    return P(args[0] % 2 ? &S(kNeighborA) : &S(kNeighborB));
                });
                S(kNeighborA).Set(9, 0, rng() % 2 ? 0x77 : 0x10077);
                S(kNeighborB).Set(9, 0, rng() % 3 ? 0x78 : 0xffff0077);
            },
            [&](bool original) -> uint32_t {
                uint32_t out = 0xabababab;
                Call<void(__thiscall *)(Family *, uint32_t, uint32_t *)>(ctx, original, 0x49e330, FAM_ADAPTER(FindMemberLot), shortId, &out);
                return out;
            });
    }
}

namespace {
uint32_t ChainA(SObj *, uint32_t *) { return P(&S(kHelper)); }
uint32_t ChainB(SObj *, uint32_t *) { return P(&S(kHelperB)); }
} // namespace

DIFF_TEST(Family_HasMemberObject, 0x4a0e60)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair2(ctx, "HasMemberObject", seed, 0xf,
            [](std::mt19937 &rng) {
                Reset(&S(kLot), &S(kHousehold));
                S(kResource).Set(8, 0, 0, &ChainA);
                S(kHelper).Set(61, 0, 0, &ChainB);
                const uint32_t pick = F()->mMemberIds.empty() || rng() % 3 == 0 ? 0x100 + rng() % 6 : F()->mMemberIds[0];
                S(kHelperB).Set(28, 0, pick);
            },
            [&](bool original) -> uint32_t {
                return Call<uint8_t(__thiscall *)(Family *, void *)>(ctx, original, 0x4a0e60, FAM_ADAPTER(HasMemberObject), &S(kResource));
            });
    }
}

DIFF_TEST(Family_ForgetResources, 0x4a0e90)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair2(ctx, "ForgetResources", seed, 0xf,
            [](std::mt19937 &) {
                Reset(&S(kLot), &S(kHousehold));
                S(kPersist).Set(3, 0, &S(kCache));
                // The keys are on the stack: the handler records their words.
                S(kCache).Set(10, 1, 0, [](SObj *, uint32_t *args) -> uint32_t {
                    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
                    Ev({key[0], key[1], key[2], key[3]});
                    return 0;
                });
            },
            [&](bool original) -> uint32_t {
                Call<void(__thiscall *)(Family *)>(ctx, original, 0x4a0e90, FAM_ADAPTER(ForgetResources));
                return 0;
            });
    }
}

namespace {
uint32_t KeyAndAnswer(SObj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    Ev({key[0], key[1], key[2], key[3]});
    if (self->value[1]) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(Fake(self->value[1]));
        ++S(Fake(self->value[1])).refs;
    }
    return self->value[0];
}
uint32_t Create(SObj *self, uint32_t *args)
{
    if (self->value[1]) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(Fake(self->value[1]));
        ++S(Fake(self->value[1])).refs;
    }
    return self->value[0];
}
} // namespace

DIFF_TEST(Family_GetRecord, 0x49eed0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair2(ctx, "GetRecord", seed, 0xf,
            [](std::mt19937 &rng) {
                Reset(&S(kLot), &S(kHousehold));
                if (rng() % 4) F()->mRecord = nullptr;
                else F()->mRecord = &S(kResourceB), ++S(kResourceB).refs;
                S(kPersist).Set(5, 5, 0, &KeyAndAnswer);
                S(kPersist).value[0] = rng() % 2 ? rng() % 2 : 0x100;
                S(kPersist).value[1] = rng() % 3 ? kResource : 0;
                S(kFramework).Set(3, 3, 0, &Create);
                S(kFramework).value[0] = rng() % 2;
                S(kFramework).value[1] = rng() % 4 ? kHelper : 0;
            },
            [&](bool original) -> uint32_t {
                void *r = Call<void *(__thiscall *)(Family *)>(ctx, original, 0x49eed0, FAM_ADAPTER(GetRecord));
                return Norm(P(r));
            });
    }
}
