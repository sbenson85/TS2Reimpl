// Differential tests for the cTSFamily group inserts (0x49bfd0 / 0x49c900 by object, 0x49c6b0 by key) and the lot import
// (0x49d150). Fakes: ts_family_services_world.cpp.
#include "ts_family_services.h"

using namespace tsfam;
using namespace tsfam::svc;

namespace {
constexpr unsigned kSeeds = 300;
uint32_t P(const void *p) { return uint32_t(uintptr_t(p)); }

// A new member object: the framework hands out the helper fake, whose slot 20 stores the key and slot 21 returns it.
uint32_t Create(SObj *self, uint32_t *args)
{
    if (self->value[1]) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(kHelper);
        ++S(kHelper).refs;
    }
    return self->value[0];
}
uint32_t SetKey(SObj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) self->value[i] = key[i] + self->value[4]; // value[4] lets a seed change the stored key
    Ev({key[0], key[1], key[2], key[3]});
    return 0;
}
uint32_t GetKey(SObj *self, uint32_t *) { return P(&self->value[0]); }

void __fastcall FamInsert2(Family *, void *, void *object) { Ev({evFamSlot, 18, Norm(P(object))}); }

// The lot objects' key list: entries of the world's keys, by mask bit (value[0] selects the masks that list anything).
FamilyKey gKeys[5];
FamilyKey *gList[2];
uint32_t KeyList(SObj *self, uint32_t *args)
{
    const uint32_t mask = args[1];
    if (!(self->value[0] & mask)) {
        if (!(self->value[1] & mask)) return 0;
        gList[0] = gList[1] = &gKeys[0]; // an empty list
        return P(gList);
    }
    const unsigned first = mask % 5, count = 1 + (mask >> 2) % 3;
    gList[0] = &gKeys[first < 5 - count ? first : 0];
    gList[1] = gList[0] + count;
    return P(gList);
}

void SetupMembers(std::mt19937 &rng)
{
    Reset(&S(kLot), &S(kHousehold));
    S(kFramework).Set(3, 3, 0, &Create);
    S(kFramework).value[0] = rng() % 5 ? 1 : 0;
    S(kFramework).value[1] = rng() % 6 != 0;
    if (S(kFramework).value[0]) S(kFramework).value[1] = 1; // answering without an object crashes the original
    S(kHelper).Set(20, 1, 0, &SetKey);
    S(kHelper).Set(21, 0, 0, &GetKey);
    S(kHelper).value[4] = rng() % 4 == 0;
    SetFamilySlot(18, reinterpret_cast<void *>(&FamInsert2));
    InstallFakes(F());
    Family *f = F();
    if (rng() % 3) f->mFamilyId = 1 + rng() % 31999;
    for (unsigned i = 0; i < 5; ++i) gKeys[i] = W().objs[(rng() + i) % kObjects].key;
}
void SetupLotObjects(std::mt19937 &rng, Fake which)
{
    S(which).Set(24, 0, rng() % 4 ? 1u : rng() % 3);
    S(which).Set(42, 2, 0, &KeyList);
    S(which).value[0] = rng() % 0x2000;
    S(which).value[1] = rng();
}
} // namespace

DIFF_TEST(Family_InsertObjectIntoGroup, 0x49bfd0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 values(seed + 77);
        const unsigned pick = values() % 14;
        for (int group = 0; group < 2; ++group) {
            Pair2(ctx, group ? "InsertObjectIntoGroup2" : "InsertObjectIntoGroup1", seed, 0xf,
                [&](std::mt19937 &rng) {
                    SetupMembers(rng);
                    // Often the object's key sits in the second map (and maybe the first), with the dirty flags clear.
                    Family *f = F();
                    if (rng() % 2) f->mGroup1Dirty = f->mGroup2Dirty = 0;
                    if (pick < kObjects && rng() % 2) {
                        const FamilyKey key = W().objs[pick].key;
                        f->mGroup1Second[key].Assign(&W().objs[(pick + 1) % kObjects]);
                        f->mGroup2Second[key].Assign(&W().objs[(pick + 2) % kObjects]);
                        if (rng() % 2) f->mGroup1First[key].Assign(nullptr), f->mGroup2First[key].Assign(nullptr);
                        for (auto &o : W().objs) o.refs = 1000;
                    }
                },
                [&](bool original) -> uint32_t {
                    void *object = pick < kObjects ? static_cast<void *>(&W().objs[pick]) : nullptr;
                    if (group) {
                        Call<void(__thiscall *)(Family *, void *)>(ctx, original, 0x49c900, FAM_ADAPTER(InsertObjectIntoGroup2), object);
                    } else {
                        Call<void(__thiscall *)(Family *, void *)>(ctx, original, 0x49bfd0, FAM_ADAPTER(InsertObjectIntoGroup1), object);
                    }
                    return 0;
                });
        }
    }
}

namespace {
// A lot-objects object handed to the inserts: it answers only 0xac598ea8 (with itself).
uint32_t LotQuery(SObj *self, uint32_t *args)
{
    if (args[0] != 0xac598ea8 || !self->value[2]) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = self;
    ++self->refs;
    return 1;
}
} // namespace

DIFF_TEST(Family_InsertLotObjectsObject, 0x49c900)
{
    // The lot-objects path hands the object to 0x49cad0 (not replaced): it runs on both sides.
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair2(ctx, "InsertObjectIntoGroup2 (lot objects)", seed, 0xf,
            [](std::mt19937 &rng) {
                SetupMembers(rng);
                SetupLotObjects(rng, kResource);
                S(kResource).Set(0, 2, 0, &LotQuery);
                S(kResource).value[2] = rng() % 4 != 0;
            },
            [&](bool original) -> uint32_t {
                Call<void(__thiscall *)(Family *, void *)>(ctx, original, 0x49c900, FAM_ADAPTER(InsertObjectIntoGroup2), &S(kResource));
                return 0;
            });
        // The group-1 twin hands such an object to 0x49c1a0, which reaches two more services and the allocator: only a
        // lot-objects object that does not answer is tested there (below).
        Pair2(ctx, "InsertObjectIntoGroup1 (no answer)", seed, 0xf,
            [](std::mt19937 &rng) {
                SetupMembers(rng);
                S(kResource).Set(0, 2, 0, &LotQuery);
            },
            [&](bool original) -> uint32_t {
                Call<void(__thiscall *)(Family *, void *)>(ctx, original, 0x49bfd0, FAM_ADAPTER(InsertObjectIntoGroup1), &S(kResource));
                return 0;
            });
    }
}

DIFF_TEST(Family_InsertKeyIntoGroup2, 0x49c6b0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 values(seed + 31);
        const unsigned kind = values() % 4;
        Pair2(ctx, "InsertKeyIntoGroup2", seed, 0xf,
            [&](std::mt19937 &rng) {
                SetupMembers(rng);
                if (rng() % 2) F()->mGroup2Dirty = 0;
                if (kind && rng() % 2) {
                    FamilyKey key = W().objs[(kind * 5 + 1) % kObjects].key;
                    if (kind == 3) key.mInstance += 9;
                    F()->mGroup2Second[key].Assign(&W().objs[0]);
                    if (rng() % 2) F()->mGroup2First[key].Assign(nullptr);
                    for (auto &o : W().objs) o.refs = 1000;
                }
                SetupLotObjects(rng, kResource);
                S(kPersist).Set(5, 5, 0, [](SObj *self, uint32_t *args) -> uint32_t {
                    if (self->value[1]) {
                        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(kResource);
                        ++S(kResource).refs;
                    }
                    return self->value[0];
                });
                S(kPersist).value[0] = rng() % 2;
                S(kPersist).value[1] = rng() % 4 != 0;
            },
            [&](bool original) -> uint32_t {
                FamilyKey key = kind == 0 ? FamilyKey{0xac598eac, 7, 1, 0} : W().objs[(kind * 5 + 1) % kObjects].key;
                if (kind == 3) key.mInstance += 9;
                Call<void(__thiscall *)(Family *, const FamilyKey *)>(ctx, original, 0x49c6b0, FAM_ADAPTER(InsertKeyIntoGroup2), &key);
                return 0;
            });
    }
}

DIFF_TEST(Family_ImportLotObjects, 0x49d150)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 values(seed + 41);
        const uint32_t lot = values() % 9;
        Pair2(ctx, "ImportLotObjects", seed, 0xf,
            [](std::mt19937 &rng) {
                SetupMembers(rng);
                SetupLotObjects(rng, kResource);
                if (rng() % 8 == 0) *Cache(0x10cc288) = 0;
                S(kApp).Set(24, 0, rng() % 6 ? &S(kRegistry) : nullptr);
                S(kRegistry).Set(14, 1, rng() % 5 ? &S(kLotObject) : nullptr);
                S(kLotObject).Set(38, 0, rng() % 4 ? 1u : 0u);
                S(kLotObject).Set(67, 0, &S(kCache));
                S(kCache).Set(11, 0, rng());
                S(kPersist).Set(5, 5, 0, [](SObj *self, uint32_t *args) -> uint32_t {
                    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
                    Ev({key[0], key[1], key[2], key[3]});
                    if (self->value[1]) {
                        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(kResource);
                        ++S(kResource).refs;
                    }
                    return self->value[0];
                });
                S(kPersist).value[0] = rng() % 3 != 0;
                S(kPersist).value[1] = rng() % 4 != 0;
            },
            [&](bool original) -> uint32_t {
                Call<void(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49d150, FAM_ADAPTER(ImportLotObjects), lot);
                *reinterpret_cast<void **>(Cache(0x10cc288)) = &S(kApp);
                return 0;
            });
    }
}
