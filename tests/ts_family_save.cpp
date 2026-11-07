// Differential tests for the cTSFamily saves: the member group saves (0x4a0610 / 0x4a0930), the resource delete (0x4a0020)
// and the text save (0x4a03f0). Service fakes: ts_family_services_world.cpp; the group members are this file's own recording
// fakes (SObj), so every slot they are called through is logged.
#include "ts_family_services.h"

using namespace tsfam;
using namespace tsfam::svc;

namespace {
constexpr unsigned kSeeds = 400;
constexpr unsigned kMembers = 6;
uint32_t P(const void *p) { return uint32_t(uintptr_t(p)); }

SObj gMembers[kMembers];

void EvKey(const uint32_t *key) { Ev({key[0], key[1], key[2], key[3]}); }
// Slot 3 (GetKey(out)): writes the fake's key words (value[0..3]) and returns out.
uint32_t ResourceKey(SObj *self, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) out[i] = self->value[i] ^ self->id;
    return args[0];
}
// Key setters (slots 4, 18, 20): log the key words.
uint32_t TakeKey(SObj *, uint32_t *args)
{
    EvKey(reinterpret_cast<const uint32_t *>(uintptr_t(args[0])));
    return 1;
}
// Slot 21 (the member's own key): the address of its words.
uint32_t OwnKey(SObj *self, uint32_t *) { return P(&self->value[4]); }

// Writes `which` (counted) into the out pointer args[outArg] when value[6] says so; answers value[5].
template <unsigned OutArg, Fake Which> uint32_t HandOut(SObj *self, uint32_t *args)
{
    if (self->value[6]) {
        *reinterpret_cast<void **>(uintptr_t(args[OutArg])) = &S(Which);
        ++S(Which).refs;
    }
    return self->value[5];
}
// Framework CreateInstance: hands out a fake by class (container class -> kHelperB, member class -> kHelper, string table
// -> kResource), answering value[0] (bit per call) and writing when it answers or value[1] is set.
uint32_t Create(SObj *self, uint32_t *args)
{
    const unsigned call = self->value[7]++;
    const bool answer = (self->value[0] >> (call % 32)) & 1;
    if (answer || (self->value[1] >> (call % 32)) & 1) {
        Fake which = args[0] == 0x6c4f359d ? kHelperB : args[0] == 0x0c560f39 ? kHelper : kResource;
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(which);
        ++S(which).refs;
    }
    return answer;
}
// The persist system's GetResource (the member container, or the string table): as HandOut, the object from value[4].
uint32_t GetResource(SObj *self, uint32_t *args)
{
    EvKey(reinterpret_cast<const uint32_t *>(uintptr_t(args[0])));
    if (self->value[6]) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(Fake(self->value[4]));
        ++S(Fake(self->value[4])).refs;
    }
    return self->value[5];
}
// The store's SaveResource: answers bit n of value[0] for its n-th call.
uint32_t SaveCopy(SObj *self, uint32_t *) { return (self->value[0] >> (self->value[7]++ % 32)) & 1; }
// The container's QueryInterface: hands out itself (counted) when value[5] is set.
uint32_t SelfQuery(SObj *self, uint32_t *args)
{
    if (!self->value[5]) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = self;
    ++self->refs;
    return 1;
}

void SetupResource(SObj &o, std::mt19937 &rng)
{
    o.Set(3, 1, 0, &ResourceKey);
    o.Set(4, 1, 0, &TakeKey);
    o.Set(9, 1, 1);
    o.Set(10, 1, 1);
    for (int i = 0; i < 4; ++i) o.value[i] = rng();
}

void SetupMembers(std::mt19937 &rng)
{
    Family *f = F();
    for (unsigned i = 0; i < kMembers; ++i) {
        SObj &m = gMembers[i];
        m.Reset(300 + i);
        SetupResource(m, rng);
        for (int k = 4; k < 8; ++k) m.value[k] = rng() % 4;
        m.Set(6, 0, rng() % 3 == 0 ? rng() % 3 : f->mRecordKey[1]);
        m.Set(7, 0, rng() % 2 ? 0 : rng());
        m.Set(18, 1, 0, &TakeKey);
        m.Set(20, 1, 0, &TakeKey);
        m.Set(21, 0, 0, &OwnKey);
    }
    FamilyObjectMap *maps[4] = {&f->mGroup1First, &f->mGroup1Second, &f->mGroup2First, &f->mGroup2Second};
    for (auto *map : maps) {
        map->clear();
        const unsigned n = rng() % 5;
        for (unsigned i = 0; i < n; ++i) {
            const FamilyKey key{rng() % 3, rng() % 3, rng() % 4, rng() % 2};
            (*map)[key].Assign(&gMembers[rng() % kMembers]);
        }
    }
    for (auto &m : gMembers) m.refs = 1000;
}

// Persist, store, framework and container fakes for the group saves and the delete.
void SetupServices(std::mt19937 &rng)
{
    Reset(&S(kLot), &S(kHousehold));
    Family *f = F();
    if (rng() % 3) f->mFamilyId = 1 + rng() % 31999;
    if (rng() % 4 == 0) f->mRecordKey[1] = rng() % 3;
    // The member container the persist system finds (kResource), its version and answer.
    S(kPersist).Set(5, 5, 0, &GetResource);
    S(kPersist).value[4] = kResource;
    S(kPersist).value[5] = rng() % 2;
    S(kPersist).value[6] = rng() % 3 != 0;
    S(kResource).Set(0, 2, 0, &SelfQuery);
    S(kResource).value[5] = rng() % 4 != 0;
    S(kResource).Set(12, 0, rng() % 2 ? 0x80u : rng() % 2 ? 0x10u : rng() % 0x100);
    S(kResource).Set(26, 1);
    SetupResource(S(kResource), rng);
    // The record's segment (kResourceB) through FindSegment, and the cache's FindSegmentOf.
    S(kPersist).Set(34, 3, 0, &HandOut<2, kResourceB>);
    S(kPersist).Set(3, 0, &S(kCache));
    S(kCache).Set(34, 2, 0, &HandOut<1, kResourceB>);
    S(kCache).value[5] = rng() % 3 != 0;
    S(kCache).value[6] = rng() % 4 != 0;
    if (S(kCache).value[5]) S(kCache).value[6] = 1; // a found segment that is not handed out would crash the original
    S(kResourceB).Set(26, 1, 0, &TakeKey);
    // A created container (kHelperB) and member copies (kHelper).
    S(kFramework).Set(3, 3, 0, &Create);
    S(kFramework).value[0] = rng() % 4 ? rng() : rng() % 2 ? 0xffffffffu : 0;
    S(kFramework).value[1] = rng();
    for (Fake which : {kHelper, kHelperB}) {
        SetupResource(S(which), rng);
        S(which).Set(13, 1);
        S(which).Set(26, 1);
        S(which).Set(18, 1, 0, &TakeKey);
        S(which).Set(20, 1, 0, &TakeKey);
    }
    // The store.
    S(kStore).Set(8, 2);
    S(kStore).Set(11, 1);
    S(kStore).Set(20, 3, 0, &SaveCopy);
    S(kStore).value[0] = rng() % 3 ? rng() : 0xffffffffu;
    S(kStore).Set(26, 1);
    InstallFakes(f);
    f->mGroup1Loaded = uint8_t(rng() % 2);
    f->mGroup2Loaded = uint8_t(rng() % 2);
    f->mGroup1Dirty = uint8_t(rng() % 2);
    f->mGroup2Dirty = uint8_t(rng() % 2);
    SetupMembers(rng);
}

// Runs setup and run on two identical worlds (original first) and compares the result, state, fakes and members.
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
            std::mt19937 rng(seed * 7919u + 11);
            setup(rng);
            blobs[side].push_back(uint32_t(run(side == 0)));
            blobs[side].push_back(uint32_t(gTrapHits));
            Capture(blobs[side]);
            CaptureFakes(blobs[side]);
            for (const SObj &m : gMembers) blobs[side].push_back(uint32_t(m.refs));
        }
        Same(ctx, blobs[0], blobs[1], what, RouteSeed(seed));
    }
}
} // namespace

DIFF_TEST(Family_SaveGroups, 0x4a0610)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (int group = 0; group < 2; ++group) {
            const uint8_t force = uint8_t(seed % 3 == 0 ? 1 : seed % 7 == 1 ? 0x80 : 0);
            Compare(ctx, group ? "SaveSecondGroup" : "SaveFirstGroup", seed, &SetupServices, [&](bool original) -> uint32_t {
                using Fn = uint8_t(__thiscall *)(Family *, uint8_t);
                if (group) return Call<Fn>(ctx, original, 0x4a0930, FAM_ADAPTER(SaveSecondGroup), force);
                return Call<Fn>(ctx, original, 0x4a0610, FAM_ADAPTER(SaveFirstGroup), force);
            });
        }
    }
}

DIFF_TEST(Family_DeleteResources, 0x4a0020)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "DeleteResources", seed, &SetupServices, [&](bool original) -> uint32_t {
            return Call<uint8_t(__thiscall *)(Family *)>(ctx, original, 0x4a0020, FAM_ADAPTER(DeleteResources));
        });
    }
}

namespace {
void *__fastcall FamPersistFace(Family *f, void *)
{
    Ev({evFamSlot, 31});
    return f ? static_cast<cIGZPersistResource *>(f) : nullptr;
}

void SetupTexts(std::mt19937 &rng)
{
    Reset(&S(kLot), &S(kHousehold));
    Family *f = F();
    if (rng() % 5 == 0) f->mFamilyId = 0xffffffff;
    f->mModified = uint8_t(rng() % 2);
    // Persist: SaveResource (value per call), GetResource of the table (kResource) or a new one from the framework.
    S(kPersist).Set(16, 2, 0, &SaveCopy);
    S(kPersist).value[0] = rng() % 3 ? 0xffffffffu : rng();
    S(kPersist).Set(5, 5, 0, &GetResource);
    S(kPersist).value[4] = kResource;
    S(kPersist).value[5] = rng() % 2;
    S(kPersist).value[6] = 1; // the original uses the table without checking it
    S(kFramework).Set(3, 3, 0, &Create);
    S(kFramework).value[0] = rng();
    S(kFramework).value[1] = 0xffffffff;
    S(kResource).Set(3, 0, &S(kResourceB));
    S(kResource).Set(10, 3);
    S(kResource).Set(21, 0, rng() % 2 ? 0 : rng() % 3);
    S(kResourceB).Set(4, 1, 0, &TakeKey);
    // The language service (cache 0x10cc2dc).
    S(kHelperB).Set(7, 0, rng() % 2 ? 1 : rng() % 3);
    *Cache(0x10cc2dc) = P(&S(kHelperB));
    // The record: a fake whose slot 9 gives its persist resource (kLotObject).
    f->mRecord = rng() % 3 ? &S(kHelper) : nullptr;
    S(kHelper).Set(9, 0, &S(kLotObject));
    S(kLotObject).Set(4, 1, 0, &TakeKey);
    SetFamilySlot(31, reinterpret_cast<void *>(&FamPersistFace));
    InstallFakes(f);
}
} // namespace

DIFF_TEST(Family_SaveTexts, 0x4a03f0)
{
    Globals globals(ctx);
    const uint32_t savedLanguage = *Cache(0x10cc2dc);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "SaveTexts", seed, &SetupTexts, [&](bool original) -> uint32_t {
            const uint8_t r = Call<uint8_t(__thiscall *)(Family *, uint32_t)>(ctx, original, 0x4a03f0, FAM_ADAPTER(SaveTexts), 0x1234);
            F()->mRecord = nullptr; // the fake record is not the world's
            return r;
        });
    }
    *Cache(0x10cc2dc) = savedLanguage;
}
