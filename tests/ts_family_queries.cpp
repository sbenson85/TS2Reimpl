// Differential tests for the cTSFamily queries: the move-in limits (0x49d5d0), the id set copy (0x49e3b0) and the lot
// price modifiers (0x49e9f0). Service fakes: ts_family_services_world.cpp; the lot and its entries are this file's own fakes.
#include "ts_family_services.h"

#include "app/AppServices2.h"
#include "rz/RZMemory.h"
#include "simantics/ServiceRefVector.h"

using namespace tsfam;
using namespace tsfam::svc;

namespace {
constexpr unsigned kSeeds = 400;
uint32_t P(const void *p) { return uint32_t(uintptr_t(p)); }

// ---- CheckMoveIn ----

// The service object's slot 5: hands out the property service (kResource), counted.
uint32_t QueryService(SObj *, uint32_t *args)
{
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(kResource);
    ++S(kResource).refs;
    return 1;
}
// The property service's GetValue: logs the key text and the value object's vtable, writes value[n] for the n-th key.
uint32_t GetValue(SObj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const as2::RawString *>(uintptr_t(args[0]));
    const char *text = key->mCapacity > 15 ? key->mPtr : key->mBuf;
    Ev({key->mSize, key->mCapacity});
    for (uint32_t i = 0; i < key->mSize; ++i) Ev({uint32_t(uint8_t(text[i]))});
    auto *value = reinterpret_cast<uint32_t *>(uintptr_t(args[1]));
    Ev({value[0], value[1]});
    const uint32_t n = key->mSize == 12 && text[8] == 'S' ? 0 : key->mSize == 14 ? 1 : 2;
    value[1] = self->value[n];
    return self->value[3];
}

uint32_t gCounts[2][16]; // [this, other][flags]
uint32_t __fastcall FamCount(Family *, void *, uint32_t flags)
{
    Ev({evFamSlot, 24, flags});
    return gCounts[0][flags & 15];
}
uint32_t OtherCount(SObj *, uint32_t *args) { return gCounts[1][args[0] & 15]; }
uint32_t __fastcall FamSlot44(Family *f, void *)
{
    Ev({evFamSlot, 44});
    return f->mBusinessLot;
}

void SetupMoveIn(std::mt19937 &rng)
{
    Reset(&S(kLot), &S(kHousehold));
    S(kHelperB).Set(5, 3, 0, &QueryService);
    S(kResource).Set(9, 2, 0, &GetValue);
    for (int i = 0; i < 3; ++i) S(kResource).value[i] = rng() % 4 ? rng() % 12 : rng();
    S(kResource).value[3] = rng() % 2;
    for (auto &side : gCounts)
        for (auto &c : side) c = rng() % 5 ? rng() % 4 : rng() % 3 ? 0 : rng();
    S(kHelper).Set(24, 1, 0, &OtherCount);
    SetFamilySlot(24, reinterpret_cast<void *>(&FamCount));
    SetFamilySlot(44, reinterpret_cast<void *>(&FamSlot44));
    InstallFakes(F());
    if (rng() % 4) F()->mBusinessLot = F()->mHomeLot;
}

// Runs setup and run on two identical worlds (original first) and compares the result, state and fakes; `extra` adds more.
template <class Setup, class Run, class Extra>
void Compare(difftest::Context &ctx, const char *what, uint32_t seed, Setup setup, Run run, Extra extra)
{
    for (int route = 0; route < 2; ++route) {
        gRoute = Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gOurSide = side != 0;
            Build(W(), seed, 0xf);
            gTrapHits = 0;
            std::mt19937 rng(seed * 7919u + 13);
            setup(rng);
            blobs[side].push_back(uint32_t(run(side == 0)));
            blobs[side].push_back(uint32_t(gTrapHits));
            Capture(blobs[side]);
            CaptureFakes(blobs[side]);
            extra(blobs[side]);
        }
        Same(ctx, blobs[0], blobs[1], what, RouteSeed(seed));
    }
}
void NoExtra(Blob &) {}
} // namespace

DIFF_TEST(Family_CheckMoveIn, 0x49d5d0)
{
    Globals globals(ctx);
    void *&services = *ctx.image.At<void *>(0x10c9378);
    void *const savedServices = services;
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        const bool noOther = seed % 23 == 5;
        Compare(ctx, "CheckMoveIn", seed, [&](std::mt19937 &rng) {
            SetupMoveIn(rng);
            services = &S(kHelperB);
        }, [&](bool original) -> uint32_t {
            auto *other = noOther ? nullptr : reinterpret_cast<Family *>(&S(kHelper));
            return Call<uint32_t(__thiscall *)(Family *, Family *)>(ctx, original, 0x49d5d0, FAM_ADAPTER(CheckMoveIn), other);
        }, &NoExtra);
    }
    services = savedServices;
}

// ---- CopyIdSet ----

namespace {
uint32_t GetNeighbors(SObj *self, uint32_t *) { return self->value[0] ? P(&S(kNeighbors)) : 0; }
FamilyServiceIdSet *gOut;
void SetupCopy(std::mt19937 &rng)
{
    Reset(&S(kLot), &S(kHousehold));
    S(kApp).Set(10, 0, 0, &GetNeighbors);
    S(kApp).value[0] = rng() % 4 != 0;
    S(kNeighbors).Set(26, 1);
    F()->mFlag79 = uint8_t(rng() % 2 ? 0 : rng());
    gOut->clear();
    const unsigned n = rng() % 6;
    for (unsigned i = 0; i < n; ++i) gOut->insert(rng() % 10);
    F()->mIdSet.clear();
    const unsigned m = rng() % 8;
    for (unsigned i = 0; i < m; ++i) F()->mIdSet.insert(rng() % 3 ? rng() % 10 : rng());
}
} // namespace

DIFF_TEST(Family_CopyIdSet, 0x49e3b0)
{
    Globals globals(ctx);
    FamilyServiceIdSet out;
    gOut = &out;
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "CopyIdSet", seed, &SetupCopy, [&](bool original) -> uint32_t {
            Call<void(__thiscall *)(Family *, FamilyServiceIdSet *)>(ctx, original, 0x49e3b0, FAM_ADAPTER(CopyIdSet), &out);
            return 0;
        }, [&](Blob &b) {
            b.push_back(uint32_t(out.size()));
            for (uint32_t id : out) b.push_back(id);
        });
    }
    out.clear();
    gOut = nullptr;
}

// ---- CountOutfitCombinations ----

namespace {
constexpr unsigned kObjs = 6, kParts = 4;
SObj gObjs[kObjs], gParts[kParts], gProps[kParts];

uint32_t QueryServiceMaybe(SObj *self, uint32_t *args)
{
    if (!self->value[0]) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(kResource);
    ++S(kResource).refs;
    return 1;
}
// GetValue for the boolean: logs the key, writes value[0]'s low byte into the value object, answers value[1].
uint32_t GetBoolean(SObj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const as2::RawString *>(uintptr_t(args[0]));
    const char *text = key->mCapacity > 15 ? key->mPtr : key->mBuf;
    Ev({key->mSize});
    for (uint32_t i = 0; i < key->mSize; ++i) Ev({uint32_t(uint8_t(text[i]))});
    auto *value = reinterpret_cast<uint8_t *>(uintptr_t(args[1]));
    Ev({*reinterpret_cast<uint32_t *>(value), value[4]});
    value[4] = uint8_t(self->value[0]);
    return self->value[1];
}
// An object's QueryInterface: 0x0c560f33 answers with itself when value[0], 0xebcf3e20 with part value[1] - 1 when set.
uint32_t ObjQuery(SObj *self, uint32_t *args)
{
    void *answer = nullptr;
    if (args[0] == 0x0c560f33 && self->value[0]) answer = self;
    if (args[0] == 0xebcf3e20 && self->value[1]) answer = &gParts[self->value[1] - 1];
    if (!answer) return 0;
    *reinterpret_cast<void **>(uintptr_t(args[1])) = answer;
    ++static_cast<SObj *>(answer)->refs;
    return 1;
}
uint32_t ObjKey(SObj *self, uint32_t *) { return P(&self->value[4]); }
// The persist system's GetResource: the part named by the key's first word (0: none), counted; answers value[5].
uint32_t GetPart(SObj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    Ev({key[0], key[1], key[2], key[3]});
    if (key[0] % (kParts + 1)) {
        SObj &p = gParts[key[0] % (kParts + 1) - 1];
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &p;
        ++p.refs;
    }
    return self->value[5];
}
uint32_t PartProps(SObj *self, uint32_t *) { return P(&gProps[self->id - 400]); }

// The family's slot 14 (CollectObjectsOfGroup1): a vector of some objects (counted), as the original's fills it.
void __fastcall FamCollect(Family *, void *, ServiceRefVector *out)
{
    Ev({evFamSlot, 14});
    const unsigned n = W().config[2] % 5;
    if (!n) return;
    auto **block = static_cast<void **>(RZMemory::Malloc(n * 4));
    for (unsigned i = 0; i < n; ++i) {
        SObj &o = gObjs[(W().config[3] + i * 5) % kObjs];
        block[i] = &o;
        ++o.refs;
    }
    out->mFirst = block;
    out->mLast = out->mEnd = block + n;
}

ServiceRefVector gGiven;
bool gGiveNull;
void SetupOutfits(std::mt19937 &rng)
{
    Reset(&S(kLot), &S(kHousehold));
    S(kHelperB).Set(5, 3, 0, &QueryServiceMaybe);
    S(kHelperB).value[0] = rng() % 5 != 0;
    S(kResource).Set(9, 2, 0, &GetBoolean);
    S(kResource).value[0] = rng() % 2;
    S(kResource).value[1] = rng() % 3 != 0;
    S(kPersist).Set(5, 5, 0, &GetPart);
    S(kPersist).value[5] = rng() % 2;
    for (unsigned i = 0; i < kObjs; ++i) {
        SObj &o = gObjs[i];
        o.Reset(300 + i);
        o.Set(0, 2, 0, &ObjQuery);
        o.Set(21, 0, 0, &ObjKey);
        o.value[0] = rng() % 2;
        o.value[1] = rng() % (kParts + 1);
        o.value[4] = rng() % (kParts + 2);
        for (int k = 5; k < 8; ++k) o.value[k] = rng() % 3;
    }
    for (unsigned i = 0; i < kParts; ++i) {
        gParts[i].Reset(400 + i);
        gParts[i].Set(3, 0, 0, &PartProps);
        gParts[i].Set(7, 0, rng() % 3 == 0 ? 0 : rng() % 0x10000);
        gProps[i].Reset(500 + i);
        gProps[i].Set(18, 0, rng() % 3 == 0);
        gProps[i].Set(19, 0, rng() % 4 ? rng() % 8 : rng());
        gProps[i].Set(20, 0, rng() % 4 ? rng() % 8 : rng());
        gProps[i].Set(21, 0, rng() % 4 ? 1 + 2 * (rng() % 4) : rng());
        gProps[i].Set(22, 0, rng() % 0x20 | (rng() % 4 == 0 ? rng() & 0xffffff00 : 0));
    }
    W().config[2] = rng();
    W().config[3] = rng();
    SetFamilySlot(14, reinterpret_cast<void *>(&FamCollect));
    InstallFakes(F());
    // The given vector (when not null): some objects, nulls among them; built afresh per side.
    gGiveNull = rng() % 3 == 0;
    gGiven.mFirst = gGiven.mLast = gGiven.mEnd = nullptr;
    if (!gGiveNull) {
        const unsigned n = rng() % 7;
        if (n) {
            auto **block = static_cast<void **>(RZMemory::Malloc(n * 4));
            for (unsigned i = 0; i < n; ++i) block[i] = rng() % 6 ? &gObjs[rng() % kObjs] : nullptr;
            gGiven.mFirst = block;
            gGiven.mLast = gGiven.mEnd = block + n;
        }
    }
    for (auto *set : {gObjs, gParts, gProps})
        for (unsigned i = 0; i < (set == gObjs ? kObjs : kParts); ++i) set[i].refs = 1000;
}
void CaptureOutfits(Blob &b)
{
    for (const SObj &o : gObjs) b.push_back(uint32_t(o.refs));
    for (const SObj &o : gParts) b.push_back(uint32_t(o.refs));
    if (gGiven.mFirst) RZMemory::Free(gGiven.mFirst); // the given vector's block is the caller's (no references held)
    gGiven.mFirst = gGiven.mLast = gGiven.mEnd = nullptr;
}
} // namespace

DIFF_TEST(Family_CountOutfitCombinations, 0x49e6f0)
{
    Globals globals(ctx);
    void *&services = *ctx.image.At<void *>(0x10c9378);
    void *const savedServices = services;
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        std::mt19937 values(seed + 57);
        const uint32_t categoryMask = values() % 4 ? values() % 8 : values();
        const uint32_t secondMask = values() % 4 ? values() % 8 : values();
        const uint32_t ageMask = values() % 3 ? values() % 0x10000 : values();
        Compare(ctx, "CountOutfitCombinations", seed, [&](std::mt19937 &rng) {
            SetupOutfits(rng);
            services = &S(kHelperB);
        }, [&](bool original) -> uint32_t {
            ServiceRefVector *given = gGiveNull ? nullptr : &gGiven;
            using Fn = uint32_t(__thiscall *)(Family *, uint32_t, uint32_t, uint32_t, ServiceRefVector *);
            return Call<Fn>(ctx, original, 0x49e6f0, FAM_ADAPTER(CountOutfitCombinations), categoryMask, secondMask, ageMask, given);
        }, &CaptureOutfits);
    }
    services = savedServices;
}

// ---- ScaleByLotModifiers ----

namespace {
constexpr unsigned kEntries = 7;
struct Entry {
    void **vt;
    uint32_t type;
    int16_t values[5];
    uint32_t count;
};
struct Lot {
    void **vt;
    Entry *items[kEntries];
    uint32_t count;
};
Entry gEntries[kEntries];
Lot gLot;
void *gLotVT[32], *gEntryVT[32];

void **__fastcall LotFind(Lot *lot, void *, void ***out, uint32_t type, uint32_t from, uint32_t a, int32_t b, uint32_t c,
    uint32_t d)
{
    Ev({0x70, type, from, a, uint32_t(b), c, d});
    uint32_t i = from;
    while (i < lot->count && lot->items[i]->type != type) ++i;
    if (i > lot->count) i = lot->count;
    *out = reinterpret_cast<void **>(&lot->items[i]);
    return reinterpret_cast<void **>(out);
}
void **__fastcall LotBegin(Lot *lot, void *, void ***out)
{
    Ev({0x71});
    *out = reinterpret_cast<void **>(&lot->items[0]);
    return reinterpret_cast<void **>(out);
}
void **__fastcall LotEnd(Lot *lot, void *, void ***out)
{
    Ev({0x72});
    *out = reinterpret_cast<void **>(&lot->items[lot->count]);
    return reinterpret_cast<void **>(out);
}
void __fastcall EntryBegin(Entry *e, void *, int16_t **out)
{
    Ev({0x73, e->type});
    *out = e->values;
}
void __fastcall EntryEnd(Entry *e, void *, int16_t **out)
{
    Ev({0x74, e->type});
    *out = e->values + e->count;
}
uint32_t GetLot(SObj *self, uint32_t *) { return self->value[0] ? P(&gLot) : 0; }
uint32_t GetLotManager(SObj *self, uint32_t *) { return self->value[1] ? P(&S(kLotManager)) : 0; }

void SetupLot(std::mt19937 &rng)
{
    Reset(&S(kLot), &S(kHousehold));
    gLotVT[25] = reinterpret_cast<void *>(&LotFind);
    gLotVT[28] = reinterpret_cast<void *>(&LotBegin);
    gLotVT[29] = reinterpret_cast<void *>(&LotEnd);
    gEntryVT[25] = reinterpret_cast<void *>(&EntryBegin);
    gEntryVT[26] = reinterpret_cast<void *>(&EntryEnd);
    gLot.vt = gLotVT;
    gLot.count = rng() % (kEntries + 1);
    if (gLot.count == kEntries) gLot.count = kEntries - 1; // keep the end slot inside the array
    for (unsigned i = 0; i < kEntries; ++i) {
        Entry &e = gEntries[i];
        e.vt = gEntryVT;
        e.type = rng() % 3 == 0 ? 0x74d328a8 : rng() % 2 ? 0xf4d3289f : rng() % 2 ? 0 : rng();
        e.count = rng() % 6;
        for (auto &v : e.values) v = int16_t(rng() % 3 == 0 ? 0 : rng() % 4 ? int(rng() % 200) - 50 : int(rng() % 65536) - 32768);
        gLot.items[i] = &e;
    }
    S(kApp).Set(6, 0, 0, &GetLotManager);
    S(kApp).value[1] = rng() % 6 != 0;
    S(kLotManager).Set(38, 1, 0, &GetLot);
    S(kLotManager).value[0] = rng() % 6 != 0;
}
} // namespace

DIFF_TEST(Family_ScaleByLotModifiers, 0x49e9f0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        std::mt19937 values(seed + 31);
        const int32_t amount = values() % 4 ? int32_t(values() % 5000) : values() % 2 ? -int32_t(values() % 100) : int32_t(values());
        Compare(ctx, "ScaleByLotModifiers", seed, &SetupLot, [&](bool original) -> uint32_t {
            return uint32_t(Call<int32_t(__thiscall *)(Family *, int32_t)>(ctx, original, 0x49e9f0, FAM_ADAPTER(ScaleByLotModifiers), amount));
        }, &NoExtra);
    }
}
