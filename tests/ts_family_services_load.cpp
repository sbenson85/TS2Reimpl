// Differential tests for the cTSFamily loaders: the texts (0x49f020), the member container query (0x49dfd0) and the two group
// loaders (0x49f1f0 / 0x49f320). Fakes: ts_family_services_world.cpp.
#include "ts_family_services.h"
#include "rz/RZMemory.h"

using namespace tsfam;
using namespace tsfam::svc;

namespace {
constexpr unsigned kSeeds = 300;
uint32_t P(const void *p) { return uint32_t(uintptr_t(p)); }

// The two strings the fake string table hands out (real cRZStrings with the game's vtable).
alignas(8) uint8_t gTextBytes[2][sizeof(cRZString)];
bool gTextsBuilt = false;
cRZString &Text(unsigned i) { return *reinterpret_cast<cRZString *>(gTextBytes[i]); }

// Persist slot 5 (GetResource): records the key words; answers value[0] (first call) / value[2] (later calls) and, when
// value[1] / value[3] names a fake, writes it (counted) to the out pointer.
uint32_t GetResource(SObj *self, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    Ev({key[0], key[1], key[2], key[3]});
    const bool later = self->value[7]++ != 0;
    const uint32_t write = later ? self->value[3] : self->value[1];
    if (write) {
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &S(Fake(write));
        ++S(Fake(write)).refs;
    }
    return later ? self->value[2] : self->value[0];
}
uint32_t GetString(SObj *, uint32_t *args) { return P(&Text(args[0] == 1 ? 0 : 1)); }

std::string Letters(std::mt19937 &rng)
{
    std::string s;
    const unsigned n = rng() % 3 == 0 ? 0 : rng() % 30;
    for (unsigned i = 0; i < n; ++i) s.push_back(char('A' + rng() % 26));
    return s;
}
void SetupPersist(std::mt19937 &rng)
{
    S(kPersist).Set(5, 5, 0, &GetResource);
    S(kPersist).value[0] = rng() % 3 ? rng() % 2 : 0x100;
    S(kPersist).value[1] = rng() % 4 ? kResource : 0;
    S(kPersist).value[2] = rng() % 2;
    S(kPersist).value[3] = rng() % 4 ? kResourceB : 0;
    // A persist system that answers without handing out an object would crash the original (it uses the object).
    if (S(kPersist).value[0]) S(kPersist).value[1] = kResource;
    if (S(kPersist).value[2]) S(kPersist).value[3] = kResourceB;
}
} // namespace

DIFF_TEST(Family_LoadTexts, 0x49f020)
{
    Globals globals(ctx);
    if (!gTextsBuilt) Text(0).Construct(), Text(1).Construct(), gTextsBuilt = true;
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 values(seed + 3);
        const int32_t id = values() % 5 == 0 ? -int32_t(values() % 3 + 1) : values() % 4 ? int32_t(values() % 40000) : int32_t(values());
        Pair2(ctx, "LoadTexts", seed, 0xf,
            [](std::mt19937 &rng) {
                Reset(&S(kLot), &S(kHousehold));
                SetupPersist(rng);
                Text(0).mString = Letters(rng);
                Text(1).mString = Letters(rng);
                S(kResource).Set(6, 2, 0, &GetString);
                S(kResourceB).Set(6, 2, 0, &GetString);
            },
            [&](bool original) -> uint32_t {
                return Call<uint8_t(__thiscall *)(Family *, int32_t, uint32_t)>(ctx, original, 0x49f020, FAM_ADAPTER(LoadTexts), id, 0x5555);
            });
    }
}

namespace {
// The container's QueryInterface: records, answers value[0], writes the helper (counted) when value[1] is set.
uint32_t ContainerQuery(SObj *self, uint32_t *args)
{
    if (self->value[1]) {
        *reinterpret_cast<void **>(uintptr_t(args[1])) = &S(kHelper);
        ++S(kHelper).refs;
    }
    return self->value[0];
}
void SetupContainer(std::mt19937 &rng)
{
    SetupPersist(rng);
    for (Fake f : {kResource, kResourceB}) {
        S(f).Set(0, 2, 0, &ContainerQuery);
        S(f).value[0] = rng() % 4 ? 1 : rng() % 2 ? 0 : 0x200;
        S(f).value[1] = rng() % 5 != 0;
        S(f).Set(12, 0, rng() % 4 ? 0x80u : rng() % 2 ? 0x7fu : rng());
        S(f).Set(26, 1);
    }
}
} // namespace

DIFF_TEST(Family_QueryMemberContainer, 0x49dfd0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 values(seed + 9);
        const bool nullOut = values() % 6 == 0;
        const uint32_t iid = values() % 2 ? 0x6c4f3599 : values();
        const uint32_t version = values() % 4 ? 0x80 : values() % 0x100;
        Pair2(ctx, "QueryMemberContainer", seed, 0xf, [](std::mt19937 &rng) {
            Reset(&S(kLot), &S(kHousehold));
            SetupContainer(rng);
        }, [&](bool original) -> uint32_t {
            void *out = nullptr;
            void **where = nullOut ? nullptr : &out;
            const uint8_t r = Call<uint8_t(__thiscall *)(Family *, uint32_t, void **, uint32_t)>(ctx, original, 0x49dfd0, FAM_ADAPTER(QueryMemberContainer), iid, where, version);
            return r | Norm(P(out)) << 8;
        });
    }
}

namespace {
// The object store's CollectObjects: a counted vector (block from the game allocator) of some of the world's member objects.
uint32_t Collect(SObj *self, uint32_t *args)
{
    auto *vec = reinterpret_cast<void ***>(uintptr_t(args[0]));
    const unsigned n = self->value[0];
    if (!n) return 0;
    auto **block = static_cast<void **>(RZMemory::Malloc(n * 4));
    for (unsigned i = 0; i < n; ++i) {
        Obj &o = W().objs[(self->value[1] + i * 5) % kObjects];
        block[i] = &o;
        ++o.refs;
    }
    vec[0] = block;
    vec[1] = block + n;
    vec[2] = block + n;
    return 0;
}
} // namespace

DIFF_TEST(Family_LoadGroups, 0x49f1f0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (int group = 0; group < 2; ++group) {
            Pair2(ctx, group ? "LoadSecondGroup" : "LoadFirstGroup", seed, 0xf,
                [](std::mt19937 &rng) {
                    Reset(&S(kLot), &S(kHousehold));
                    SetupContainer(rng);
                    S(kStore).Set(29, 4, 0, &Collect);
                    S(kStore).value[0] = rng() % 7;
                    S(kStore).value[1] = rng();
                    Family *f = F();
                    if (rng() % 3) f->mGroup1Loaded = 0, f->mGroup2Loaded = 0;
                    if (rng() % 2) f->mFamilyId = rng() % 3 ? 1 + rng() % 31999 : rng() % 2 ? 31999 + rng() % 3 : 0;
                },
                [&](bool original) -> uint32_t {
                    if (group)
                        return Call<uint8_t(__thiscall *)(Family *)>(ctx, original, 0x49f320, FAM_ADAPTER(LoadSecondGroup));
                    return Call<uint8_t(__thiscall *)(Family *)>(ctx, original, 0x49f1f0, FAM_ADAPTER(LoadFirstGroup));
                });
        }
    }
}
