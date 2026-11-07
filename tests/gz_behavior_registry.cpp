// Differential tests for the behaviour event registries (lane PD): the base registry 0x1485f0 (cAbstractBehaviorType::RegistryFor,
// src/gz/cGZEventReflector.size.cpp), the registration of the two base events 0x1464f0, and the four graphics classes'
// registries 0x156a70 (cCanvasW32), 0x149ef0 (cGZCanvas), 0x148440 (cGZGraphicSystem), 0x15a250 (cGraphicSystemDX), whose build is
// src/gz/cBehaviorRegistry.h. The pattern is tests/gz_draw2d_registry.cpp's (lane OZ).
//
// The originals' first-use build cannot run in the harness (the function-local statics call the CRT's thread-safe init and atexit,
// which are traps here), so the original side starts from the state that build leaves, built by the test from what the build is:
// the base registry planted over the game's static with its guards marked done, holding the two base events (the image's 0x1464f0
// runs for real), and a class registry planted over its static: a copy of that base registry plus the class's own events, whose
// records the test reads from the image's data (the tables the original copies them from: id, name, three words, and the last
// word 8), the guards and flag marked done. Ours starts from nothing (the reset hooks) and builds its own on the first call, so the
// first-use build is checked against that expectation, the rest (the stores, the returned map staying the same) differentially.
// Compared after every call: the returned map's identity (stable across calls) and every record: key, id, name (by content), the
// three words and the last. Sequences of calls with ids that are 0, base events, the class's events, planted records, new ids.
#include "difftest.h"

#include "gz/cCanvasW32.h"
#include "gz/cGZCanvas.h"
#include "gz/cGZEventReflector.h"
#include "gz/cGZGraphicSystem.h"
#include "gz/cGraphicSystemDX.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace nGZEvent;
using namespace nGZGraphic4;

namespace {

struct Statics {
    uint32_t map, mapGuard, once, holderGuard;
};
constexpr Statics kBase{0x10c94f4, 0x10c94fc, 0x10c9510, 0x10c9518};
constexpr uint32_t kBaseEventA = 0x5b51e806, kBaseEventB = 0xe9dd42cf;
static_assert(sizeof(cEventAttributeMap) == 8, "the registry map is two words, as the original's");

char gNames[8][12];

// The game's bytes of one registry's statics, restored at the end, and the map placed over them.
class Planted {
public:
    explicit Planted(const Statics &at) : at_(at)
    {
        std::memcpy(saved_, GameAddress<void>(at.map), sizeof saved_);
        mapGuard_ = *GameAddress<int32_t>(at.mapGuard);
        once_ = *GameAddress<uint8_t>(at.once);
        holderGuard_ = *GameAddress<int32_t>(at.holderGuard);
        map_ = ::new (GameAddress<void>(at.map)) cEventAttributeMap;
        *GameAddress<int32_t>(at.mapGuard) = INT_MIN;
        *GameAddress<uint8_t>(at.once) = 1;
        *GameAddress<int32_t>(at.holderGuard) = INT_MIN;
    }
    ~Planted()
    {
        std::destroy_at(map_);
        std::memcpy(GameAddress<void>(at_.map), saved_, sizeof saved_);
        *GameAddress<int32_t>(at_.mapGuard) = mapGuard_;
        *GameAddress<uint8_t>(at_.once) = once_;
        *GameAddress<int32_t>(at_.holderGuard) = holderGuard_;
    }
    Planted(const Planted &) = delete;
    Planted &operator=(const Planted &) = delete;
    cEventAttributeMap *Map() const { return map_; }

private:
    Statics at_;
    uint8_t saved_[sizeof(cEventAttributeMap)];
    int32_t mapGuard_;
    uint8_t once_;
    int32_t holderGuard_;
    cEventAttributeMap *map_;
};

std::string Snapshot(const cEventAttributeMap *map)
{
    std::string out;
    char line[160];
    for (const auto &[key, a] : *map) {
        std::snprintf(line, sizeof line, "%08x:%08x:%s:%u:%u:%u:%u;", key, a.mId, a.mName ? a.mName : "<null>", a.mWord08,
                      a.mWord0C, a.mWord10, a.mWord14);
        out += line;
    }
    return out;
}

using Fn = cEventAttributeMap *(__cdecl *)(uint32_t, const cEventAttribute *);

// One registry under test: the original's RVA and statics, ours, the reset hook, and the tables of the class's own events in the
// image (none for the base).
struct Spec {
    const char *name;
    uint32_t rva;
    Statics statics;
    Fn ours;
    void (*reset)();
    std::vector<uint32_t> tables;
};

void ResetBase() { cAbstractBehaviorType::ResetRegistryForTest(); }

// Runs one sequence on a side; returns the snapshot after every call and whether the returned map stayed the same.
std::vector<std::string> Run(difftest::Context &ctx, const Spec &spec, uint32_t seed, bool ours, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    std::vector<uint32_t> ids = {0, 0, 1, kBaseEventA, kBaseEventB, 0x10, 0x77, 0x1000, 0xfffffff0u, 0xffffffffu, uint32_t(rng()), uint32_t(rng())};
    for (const uint32_t table : spec.tables) ids.push_back(GameAddress<uint32_t>(table)[0]);
    for (unsigned i = 0; i < 8; ++i) std::snprintf(gNames[i], sizeof gNames[i], "n%u_%u", seed % 100, i);

    // For a class registry, a record stored under a base event id before the class registry's first use: the class build
    // registers the base events again first (the holder's 0x1464f0), so its copy of the base holds the real record.
    const bool disturb = spec.reset && seed % 3 == 0;
    const cEventAttribute weird{kBaseEventA, "weird", 7, 8, 9, 10};
    std::unique_ptr<Planted> base, mine;
    if (ours) {
        ResetBase();
        if (spec.reset) spec.reset();
        if (disturb) cAbstractBehaviorType::RegistryFor(kBaseEventA, &weird);
    } else {
        base = std::make_unique<Planted>(kBase);
        if (disturb) (*base->Map())[kBaseEventA] = weird;
        ctx.Original<void(__thiscall *)(void *)>(0x1464f0)(nullptr); // the base events into the base registry
        if (spec.reset) { // a class registry: its static holds a copy of the base plus its own events
            mine = std::make_unique<Planted>(spec.statics);
            *mine->Map() = *base->Map();
            for (const uint32_t table : spec.tables) {
                const auto *record = GameAddress<uint32_t>(table);
                (*mine->Map())[record[0]] = {record[0], reinterpret_cast<const char *>(uintptr_t(record[1])), record[2],
                                             record[3], record[4], record[5]};
            }
        }
    }
    std::vector<std::string> out;
    cEventAttributeMap *first = nullptr;
    stable = true;
    const unsigned calls = 1 + rng() % 6;
    for (unsigned c = 0; c < calls; ++c) {
        const uint32_t id = ids[rng() % ids.size()];
        const cEventAttribute attribute{rng() % 5 == 0 ? id : uint32_t(rng()), gNames[rng() % 8], rng() % 4, uint32_t(rng()),
                                        rng() % 3, rng() % 16};
        cEventAttributeMap *map = ours ? spec.ours(id, &attribute) : ctx.Original<Fn>(spec.rva)(id, &attribute);
        if (!first) first = map;
        stable = stable && map == first;
        out.push_back(Snapshot(map));
    }
    // The plain registry the template's slots use (id 0, the default record).
    const cEventAttribute defaults{0, "", 0, 0, 0, 0};
    out.push_back(Snapshot(ours ? spec.ours(0, &defaults) : ctx.Original<Fn>(spec.rva)(0, &defaults)));
    if (ours) {
        ResetBase();
        if (spec.reset) spec.reset();
    }
    return out;
}

void Compare(difftest::Context &ctx, const Spec &spec, uint32_t seeds)
{
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        bool stableOriginal = false, stableOurs = false;
        const std::vector<std::string> original = Run(ctx, spec, seed, false, stableOriginal);
        const std::vector<std::string> ours = Run(ctx, spec, seed, true, stableOurs);
        ++ctx.cases;
        if (!stableOriginal || !stableOurs || original.size() != ours.size()) {
            ctx.Fail("%s seed %u: the returned map moved (original %d, ours %d) or the call counts differ (%zu / %zu)", spec.name,
                     seed, stableOriginal, stableOurs, original.size(), ours.size());
            return;
        }
        for (size_t i = 0; i < original.size(); ++i)
            if (original[i] != ours[i]) {
                ctx.Fail("%s seed %u call %zu:\n  original %s\n  ours     %s", spec.name, seed, i, original[i].c_str(),
                         ours[i].c_str());
                return;
            }
    }
}

} // namespace

DIFF_TEST(GZBehaviorRegistry_Base, 0x1485f0)
{
    Compare(ctx, {"base", 0x1485f0, kBase, &cAbstractBehaviorType::RegistryFor, nullptr, {}}, 600);
}

DIFF_TEST(GZBehaviorRegistry_CanvasW32, 0x156a70)
{
    Compare(ctx,
            {"cCanvasW32", 0x156a70, {0x10c96fc, 0x10c9704, 0x10c970c, 0x10c9714}, &cCanvasW32Traits::RegistryFor,
             &cCanvasW32Traits::ResetRegistryForTest, {0xfebc78, 0xfebc90, 0xfebca8, 0xfebcc0, 0xfebcd8, 0xfebcf0}},
            600);
}

DIFF_TEST(GZBehaviorRegistry_Canvas, 0x149ef0)
{
    Compare(ctx,
            {"cGZCanvas", 0x149ef0, {0x10c9574, 0x10c957c, 0x10c9580, 0x10c9588}, &cGZCanvasBehaviorTraits::RegistryFor,
             &cGZCanvasBehaviorTraits::ResetRegistryForTest, {0xfeb4e8, 0xfeb500, 0xfeb428, 0xfeb470, 0xfeb488}},
            600);
}

DIFF_TEST(GZBehaviorRegistry_GraphicSystem, 0x148440)
{
    Compare(ctx,
            {"cGZGraphicSystem", 0x148440, {0x10c9504, 0x10c950c, 0x10c951c, 0x10c9524},
             &cGZGraphicSystemBehaviorTraits::RegistryFor, &cGZGraphicSystemBehaviorTraits::ResetRegistryForTest, {0xfeb208}},
            600);
}

DIFF_TEST(GZBehaviorRegistry_GraphicSystemDX, 0x15a250)
{
    Compare(ctx,
            {"cGraphicSystemDX", 0x15a250, {0x10c9768, 0x10c9770, 0x10c9778, 0x10c9780},
             &cGraphicSystemDXBehaviorTraits::RegistryFor, &cGraphicSystemDXBehaviorTraits::ResetRegistryForTest,
             {0xfebde0, 0xfebdf8, 0xfebe10, 0xfebd20, 0xfebd38, 0xfebdb0, 0xfebdc8}},
            600);
}

// 0x1464f0 / 0x1465d0 / 0x146580: the base events stored in the registry (both, or one each; the argument, `this`, is unused and
// comes back in eax), over a registry that may hold records under the base ids (which they overwrite).
namespace {
void CheckRegistration(difftest::Context &ctx, uint32_t rva, void *(__fastcall *ours)(void *, void *))
{
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        std::string snapshots[2];
        void *returned[2] = {};
        void *const self = reinterpret_cast<void *>(uintptr_t(0x1000 + seed * 16));
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            std::unique_ptr<Planted> base;
            if (side == 1) ResetBase();
            else base = std::make_unique<Planted>(kBase);
            const cEventAttribute none{};
            cEventAttributeMap *const map = side == 1 ? cAbstractBehaviorType::RegistryFor(0, &none) : base->Map();
            if (side == 1) map->clear(); // ours builds with the two base events; the planted original starts empty
            // Records of the test's own, some under the base ids, before the registration.
            for (unsigned i = 0; i < seed % 4; ++i) {
                const uint32_t id = i == 0 ? kBaseEventA : i == 1 ? kBaseEventB : uint32_t(rng());
                (*map)[id] = {id, "planted", i, uint32_t(rng()), i, 7};
            }
            returned[side] = side == 1 ? ours(self, nullptr) : ctx.Original<void *(__thiscall *)(void *)>(rva)(self);
            snapshots[side] = Snapshot(map);
            if (side == 1) ResetBase();
        }
        ++ctx.cases;
        if (returned[0] != returned[1] || snapshots[0] != snapshots[1]) {
            ctx.Fail("0x%x seed %u: returned %p / %p\n  original %s\n  ours     %s", rva, seed, returned[0], returned[1],
                     snapshots[0].c_str(), snapshots[1].c_str());
            return;
        }
    }
}
} // namespace

DIFF_TEST(GZBehaviorRegistry_RegisterBaseEvents, 0x1464f0)
{
    CheckRegistration(ctx, 0x1464f0, &cAbstractBehaviorType::RegisterBaseEventsAdapter);
}

DIFF_TEST(GZBehaviorRegistry_RegisterConnectionChanged, 0x1465d0)
{
    CheckRegistration(ctx, 0x1465d0, &cAbstractBehaviorType::RegisterConnectionChangedAdapter);
}

DIFF_TEST(GZBehaviorRegistry_RegisterAttributeInfosUpdated, 0x146580)
{
    CheckRegistration(ctx, 0x146580, &cAbstractBehaviorType::RegisterAttributeInfosUpdatedAdapter);
}
