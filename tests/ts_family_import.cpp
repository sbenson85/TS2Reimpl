// Differential tests for the cTSFamily group 1 lot imports: the lot-objects bodies 0x49c1a0 (group 1) and 0x49cad0
// (group 2), the key insert 0x49bab0 (slot 10) and the lot import 0x49cc70 (slot 9). Service fakes:
// ts_family_services_world.cpp; the resource service (cache 0x10faf74, a 9-argument slot) and the found objects are this
// file's own fakes.
#include "ts_family_services.h"

#include "rz/RZMemory.h"
#include "simantics/ServiceRefVector.h"

using namespace tsfam;
using namespace tsfam::svc;

namespace {
constexpr unsigned kSeeds = 400;
uint32_t P(const void *p) { return uint32_t(uintptr_t(p)); }

// ---- the lot objects (kResource) ----

FamilyKey gKeys[6];
FamilyKey *gList[2];
// GetKeyList(kind, mask): some of the world's keys for the masks value[0] selects (an empty list for value[1]'s); kind
// 0x20 lists get keys whose first word picks the lot part (odd: one).
uint32_t KeyList(SObj *self, uint32_t *args)
{
    const uint32_t kind = args[0], mask = args[1];
    if (!(self->value[0] & mask) || (kind == 0x20 && !(self->value[2] & mask))) {
        if (!(self->value[1] & mask)) return 0;
        gList[0] = gList[1] = &gKeys[0];
        return P(gList);
    }
    const unsigned first = (mask + kind) % 4, count = 1 + (mask >> 1) % 3;
    gList[0] = &gKeys[first];
    gList[1] = gList[0] + count;
    return P(gList);
}

// The GUID names: real cRZStrings (their slot 5 is the game's).
constexpr unsigned kNames = 4;
alignas(8) uint8_t gNameBytes[kNames][sizeof(cRZString)];
bool gNamesBuilt = false;
cRZString &Name(unsigned i) { return *reinterpret_cast<cRZString *>(gNameBytes[i]); }
cRZString *gNameRange[2];
uint32_t GuidNames(SObj *, uint32_t *) { return P(gNameRange); }

std::string RandomGuid(std::mt19937 &rng)
{
    static const char hex[] = "0123456789abcdefABCDEF";
    std::string s;
    const unsigned shape = rng() % 6;
    if (shape == 0) return s;
    const unsigned digits = shape == 1 ? rng() % 40 : shape == 2 ? 33 + rng() % 30 : 32;
    if (rng() % 2) s.push_back('{');
    for (unsigned i = 0; i < digits; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) s.push_back('-');
        s.push_back(rng() % 20 == 0 ? char('g' + rng() % 10) : hex[rng() % 22]);
    }
    if (rng() % 2) s.push_back('}');
    return s;
}

void SetupLotObjects(std::mt19937 &rng)
{
    S(kResource).Set(24, 0, rng() % 3 ? 1u : rng() % 2 ? 0u : rng());
    S(kResource).Set(42, 2, 0, &KeyList);
    S(kResource).value[0] = rng() % 0x2000;
    S(kResource).value[1] = rng();
    S(kResource).value[2] = rng();
    S(kResource).Set(22, 0, rng() % 0x100);
    S(kResource).Set(23, 0, rng() % 0x100);
    S(kResource).Set(37, 0, 0, &GuidNames);
    if (!gNamesBuilt) {
        for (unsigned i = 0; i < kNames; ++i) Name(i).Construct();
        gNamesBuilt = true;
    }
    for (unsigned i = 0; i < kNames; ++i) Name(i).mString = RandomGuid(rng);
    gNameRange[0] = &Name(0);
    gNameRange[1] = &Name(rng() % (kNames + 1));
    for (unsigned i = 0; i < 6; ++i) {
        gKeys[i] = W().objs[(rng() + i) % kObjects].key;
        gKeys[i].mType = rng() % 3 ? rng() % 4 : rng();
    }
}

// ---- the resource service and what it finds ----

constexpr unsigned kFound = 3;
SObj gFound[kFound], gFoundResource[kFound];
struct ResourceServiceFake {
    void **vt;
    uint32_t pick;
} gResources;
void *gResourcesVT[32];

void __fastcall FindResources(ResourceServiceFake *self, void *, ServiceBlockVector *out, uint32_t a, uint32_t b, uint32_t c,
    uint32_t d, uint32_t e, const uint32_t *key, uint32_t f, uint32_t g)
{
    Ev({0x80, a, b, c, d, e, f, g, key[0], key[1], key[2], key[3], Norm(P(out->mFirst)), Norm(P(out->mLast))});
    const unsigned n = (self->pick++ * 7 + key[0]) % 4;
    if (!n) return;
    auto **block = static_cast<void **>(RZMemory::Malloc(n * 4));
    for (unsigned i = 0; i < n; ++i) block[i] = (key[1] + i) % 4 == 3 ? nullptr : &gFound[(key[2] + i) % kFound];
    out->mFirst = block;
    out->mLast = out->mEnd = block + n;
}
uint32_t FoundResource(SObj *self, uint32_t *) { return P(&gFoundResource[self->id - 300]); }
uint32_t ResourceKey(SObj *self, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) out[i] = self->value[i];
    return args[0];
}

// ---- the persist system: lot objects (kResource) by 0xac598ea8, lot parts (kResourceB) by 0x0c1fe23e ----

uint32_t GetResource(SObj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    Ev({key[0], key[1], key[2], key[3], args[1]});
    void *answer = nullptr;
    if (args[1] == 0xac598ea8 && self->value[1]) answer = &S(kResource);
    if (args[1] == 0x0c1fe23e && (key[0] & 1)) answer = &S(kResourceB);
    if (answer) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = answer;
        ++static_cast<SObj *>(answer)->refs;
    }
    return self->value[0];
}

// New member objects: the framework hands out kHelper (slot 20 logs the key, 21 returns it).
uint32_t Create(SObj *self, uint32_t *args)
{
    const bool answer = (self->value[0] >> (self->value[7]++ % 32)) & 1;
    if (answer || self->value[1]) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(kHelper);
        ++S(kHelper).refs;
    }
    return answer;
}
uint32_t SetKey(SObj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) self->value[i] = key[i];
    Ev({key[0], key[1], key[2], key[3]});
    return 0;
}
uint32_t GetKey(SObj *self, uint32_t *) { return P(&self->value[0]); }

// The family's slots 10 and 11 (the recursion and the object insert): logged only.
void __fastcall FamInsertKey1(Family *, void *, const uint32_t *key) { Ev({evFamSlot, 10, key[0], key[1], key[2], key[3]}); }
void __fastcall FamInsertObject1(Family *, void *, void *object) { Ev({evFamSlot, 11, Norm(P(object))}); }
void __fastcall FamInsertObject2(Family *, void *, void *object) { Ev({evFamSlot, 18, Norm(P(object))}); }

void SetupImport(std::mt19937 &rng)
{
    Reset(&S(kLot), &S(kHousehold));
    SetupLotObjects(rng);
    S(kPersist).Set(5, 5, 0, &GetResource);
    S(kPersist).value[0] = rng() % 3 != 0;
    S(kPersist).value[1] = rng() % 4 != 0;
    S(kResourceB).Set(3, 0, &S(kHelperB));
    S(kHelperB).Set(17, 0, P(&S(kHelperB).value[0]));
    for (int i = 0; i < 4; ++i) S(kHelperB).value[i] = rng() % 8;
    S(kFramework).Set(3, 3, 0, &Create);
    S(kFramework).value[0] = rng() % 4 ? rng() : 0;
    S(kFramework).value[1] = rng() % 2;
    S(kHelper).Set(20, 1, 0, &SetKey);
    S(kHelper).Set(21, 0, 0, &GetKey);
    gResourcesVT[22] = reinterpret_cast<void *>(&FindResources);
    gResources.vt = gResourcesVT;
    gResources.pick = rng() % 4;
    for (unsigned i = 0; i < kFound; ++i) {
        gFound[i].Reset(300 + i);
        gFound[i].Set(3, 0, 0, &FoundResource);
        gFoundResource[i].Reset(310 + i);
        gFoundResource[i].Set(3, 1, 0, &ResourceKey);
        for (int k = 0; k < 4; ++k) gFoundResource[i].value[k] = rng();
    }
    SetFamilySlot(10, reinterpret_cast<void *>(&FamInsertKey1));
    SetFamilySlot(11, reinterpret_cast<void *>(&FamInsertObject1));
    SetFamilySlot(18, reinterpret_cast<void *>(&FamInsertObject2));
    InstallFakes(F());
    Family *f = F();
    if (rng() % 4) f->mFamilyId = 1 + rng() % 31999;
    else f->mFamilyId = rng() % 2 ? 0 : 31999 + rng() % 3;
}

template <class Setup, class Run>
void Compare(difftest::Context &ctx, const char *what, uint32_t seed, Setup setup, Run run)
{
    for (int route = 0; route < 2; ++route) {
        gRoute = Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gOurSide = side != 0;
            Build(W(), seed, 0xf);
            gTrapHits = 0;
            std::mt19937 rng(seed * 7919u + 17);
            setup(rng);
            blobs[side].push_back(uint32_t(run(side == 0)));
            blobs[side].push_back(uint32_t(gTrapHits));
            Capture(blobs[side]);
            CaptureFakes(blobs[side]);
            for (const SObj &o : gFound) blobs[side].push_back(uint32_t(o.refs));
        }
        Same(ctx, blobs[0], blobs[1], what, RouteSeed(seed));
    }
}

// Points the resource service cache (0x10faf74) at the fake for the scope.
struct ResourceServiceScope {
    void *&cell;
    void *saved;
    explicit ResourceServiceScope(difftest::Context &ctx) : cell(*ctx.image.At<void *>(0x10faf74)), saved(cell)
    {
        cell = &gResources;
    }
    ~ResourceServiceScope() { cell = saved; }
};
} // namespace

DIFF_TEST(Family_ImportLotObjectsIntoGroup, 0x49c1a0)
{
    Globals globals(ctx);
    ResourceServiceScope resources(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const bool none = seed % 17 == 3;
        for (int group = 0; group < 2; ++group) {
            Compare(ctx, group ? "ImportLotObjectsIntoGroup2" : "ImportLotObjectsIntoGroup1", seed, &SetupImport,
                [&](bool original) -> uint32_t {
                    void *objects = none ? nullptr : &S(kResource);
                    using Fn = uint8_t(__thiscall *)(Family *, void *);
                    if (group) return Call<Fn>(ctx, original, 0x49cad0, FAM_ADAPTER(ImportLotObjectsIntoGroup2), objects);
                    return Call<Fn>(ctx, original, 0x49c1a0, FAM_ADAPTER(ImportLotObjectsIntoGroup1), objects);
                });
        }
    }
}

DIFF_TEST(Family_InsertKeyIntoGroup1, 0x49bab0)
{
    Globals globals(ctx);
    ResourceServiceScope resources(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 values(seed + 61);
        const unsigned kind = values() % 4;
        Compare(ctx, "InsertKeyIntoGroup1", seed, [&](std::mt19937 &rng) {
            SetupImport(rng);
            if (rng() % 2) F()->mGroup1Dirty = 0;
            if (kind && rng() % 2) {
                FamilyKey key = W().objs[(kind * 5 + 1) % kObjects].key;
                if (kind == 3) key.mInstance += 9;
                F()->mGroup1Second[key].Assign(&W().objs[0]);
                if (rng() % 2) F()->mGroup1First[key].Assign(nullptr);
                for (auto &o : W().objs) o.refs = 1000;
            }
        }, [&](bool original) -> uint32_t {
            FamilyKey key = kind == 0 ? FamilyKey{0xac598eac, 7, 1, 0} : W().objs[(kind * 5 + 1) % kObjects].key;
            if (kind == 3) key.mInstance += 9;
            Call<void(__thiscall *)(Family *, const FamilyKey *)>(ctx, original, 0x49bab0, FAM_ADAPTER(InsertKeyIntoGroup1), &key);
            return 0;
        });
    }
}

DIFF_TEST(Family_ImportLotIntoGroup1, 0x49cc70)
{
    Globals globals(ctx);
    ResourceServiceScope resources(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 values(seed + 71);
        const uint32_t lot = values() % 9;
        Compare(ctx, "ImportLotIntoGroup1", seed, [](std::mt19937 &rng) {
            SetupImport(rng);
            if (rng() % 8 == 0) *Cache(0x10cc288) = 0;
            S(kApp).Set(24, 0, rng() % 6 ? &S(kRegistry) : nullptr);
            S(kRegistry).Set(14, 1, rng() % 5 ? &S(kLotObject) : nullptr);
            S(kLotObject).Set(38, 0, rng() % 4 ? 1u : 0u);
            S(kLotObject).Set(67, 0, &S(kCache));
            S(kCache).Set(11, 0, rng());
        }, [&](bool original) -> uint32_t {
            Call<void(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x49cc70, FAM_ADAPTER(ImportLotIntoGroup1), lot);
            *reinterpret_cast<void **>(Cache(0x10cc288)) = &S(kApp);
            return 0;
        });
    }
}
