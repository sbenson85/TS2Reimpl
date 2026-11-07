// Differential test for cDraw2D's event registry 0x1516f0 (lane OZ, cDraw2DBehaviorTraits::RegistryFor in
// src/gz/cDraw2D.cpp): the template's registry built on first use, with `attribute` stored under `id` when id is not 0.
//
// The original's first-use build cannot run in the harness (its function-local statics call the CRT's thread-safe init and
// atexit, which are traps here), so the original side starts from the state that build leaves, built by the test from what the
// build is: the base registry (planted over the game's static 0x10c94f4 with its guards marked done, as
// tests/gz_event_reflector.cpp does) holding the two base events the original 0x1464f0 registers (run for real), copied to the
// registry 0x10c95e0, and the device event 0xca7ccbbc whose record the test reads from the image's data (0xfeb6d8: id, name,
// three words) with 4 in the last word. The original's guards and flag are marked done. Ours starts from nothing (the reset
// hook) and builds its own on the first call, so the first-use build is checked against that expectation, the rest (the stores,
// the returned map staying the same) differentially. Compared after every call: the returned map's identity (stable across
// calls) and every record: key, id, name (by content), the three words and the last. Sequences of calls with ids that are 0,
// base events, the device event, planted records, new ids, with and without a first call that stores.
#include "difftest.h"
#include "gz_registry_fx.h"

#include "gz/cDraw2D.h"
#include "gz/cGZEventReflector.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace nGZEvent;
using nGZGraphic4::cDraw2DBehaviorTraits;

namespace {

constexpr uint32_t kBaseMap = 0x10c94f4, kBaseMapGuard = 0x10c94fc, kBaseOnce = 0x10c9510, kBaseHolderGuard = 0x10c9518;
constexpr uint32_t kMap = 0x10c95e0, kFlag = 0x10c95f0, kHolderGuard = 0x10c95f8, kMapGuard = 0x10c95e8, kHolder = 0x10c95f4;
constexpr uint32_t kDeviceEvent = 0xca7ccbbc;
constexpr uint32_t kBaseEventA = 0x5b51e806, kBaseEventB = 0xe9dd42cf;
static_assert(sizeof(cEventAttributeMap) == 8, "the registry map is two words, as the original's");

char gNames[8][12];

// A copy of the game's bytes, restored at the end.
struct Saved {
    uint32_t rva;
    unsigned size;
    uint8_t bytes[8];
};

class Scope {
public:
    Scope()
    {
        save(kBaseMap, 8);
        save(kBaseMapGuard, 4);
        save(kBaseOnce, 1);
        save(kBaseHolderGuard, 4);
        save(kMap, 8);
        save(kFlag, 1);
        save(kHolderGuard, 4);
        save(kMapGuard, 4);
        save(kHolder, 4);
        base_ = ::new (GameAddress<void>(kBaseMap)) cEventAttributeMap;
        *GameAddress<int32_t>(kBaseMapGuard) = INT_MIN;
        *GameAddress<uint8_t>(kBaseOnce) = 1;
        *GameAddress<int32_t>(kBaseHolderGuard) = INT_MIN;
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof gNames[i], "base%u", i);
            (*base_)[0x10 + i] = {0x10 + i, gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
    }
    // The original side: the state the first-use build leaves (see the file comment).
    void PlantOriginalRegistry(difftest::Context &ctx)
    {
        ctx.Original<void(__thiscall *)(void *)>(0x1464f0)(nullptr); // the base events into the base registry
        mine_ = ::new (GameAddress<void>(kMap)) cEventAttributeMap(*base_);
        const auto *record = GameAddress<uint32_t>(0xfeb6d8);
        (*mine_)[kDeviceEvent] = {record[0], reinterpret_cast<const char *>(uintptr_t(record[1])), record[2], record[3],
                                  record[4], 4};
        *GameAddress<uint8_t>(kFlag) = 1;
        *GameAddress<int32_t>(kHolderGuard) = INT_MIN;
        *GameAddress<int32_t>(kMapGuard) = INT_MIN;
    }
    ~Scope()
    {
        if (mine_) mine_->~cEventAttributeMap();
        base_->~cEventAttributeMap();
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) std::memcpy(GameAddress<void>(it->rva), it->bytes, it->size);
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    void save(uint32_t rva, unsigned size)
    {
        Saved s{rva, size, {}};
        std::memcpy(s.bytes, GameAddress<void>(rva), size);
        saved_.push_back(s);
    }
    cEventAttributeMap *base_ = nullptr, *mine_ = nullptr;
    std::vector<Saved> saved_;
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

// Runs one sequence on a side; returns the snapshot after every call and whether the returned map stayed the same.
std::vector<std::string> Run(difftest::Context &ctx, uint32_t seed, bool ours, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    Scope scope;
    shape::StandIn baseStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<kBaseMap>}; // both sides see the planted base
    if (ours)
        cDraw2DBehaviorTraits::ResetRegistryForTest();
    else
        scope.PlantOriginalRegistry(ctx);
    const uint32_t ids[] = {0,      0,      0x10,   0x11,   0x12,       kBaseEventA, kBaseEventB,
                            kDeviceEvent, 0x77, 0x1000, 0xfffffff0u, rng(), rng()};
    for (unsigned i = 0; i < 8; ++i) std::snprintf(gNames[i], sizeof gNames[i], "n%u_%u", seed % 100, i);
    std::vector<std::string> out;
    cEventAttributeMap *first = nullptr;
    stable = true;
    const unsigned calls = 1 + rng() % 6;
    for (unsigned c = 0; c < calls; ++c) {
        const uint32_t id = ids[rng() % (sizeof ids / sizeof ids[0])];
        cEventAttribute attribute{rng() % 5 == 0 ? id : rng(), gNames[rng() % 8], rng() % 4, rng(), rng() % 3, rng() % 16};
        cEventAttributeMap *map = ours ? cDraw2DBehaviorTraits::RegistryFor(id, &attribute)
                                       : ctx.Original<Fn>(0x1516f0)(id, &attribute);
        if (!first) first = map;
        stable = stable && map == first;
        out.push_back(Snapshot(map));
    }
    // The plain registry the template's slots use (id 0).
    if (ours) {
        out.push_back(Snapshot(cDraw2DBehaviorTraits::Registry()));
    } else {
        cEventAttribute defaults{0, "", 0, 0, 0, 0};
        out.push_back(Snapshot(ctx.Original<Fn>(0x1516f0)(0, &defaults)));
    }
    return out;
}

} // namespace

DIFF_TEST(cDraw2D_Registry, 0x1516f0)
{
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        bool stableOriginal = false, stableOurs = false;
        const std::vector<std::string> original = Run(ctx, seed, false, stableOriginal);
        const std::vector<std::string> ours = Run(ctx, seed, true, stableOurs);
        ++ctx.cases;
        if (!stableOriginal || !stableOurs || original.size() != ours.size()) {
            ctx.Fail("seed %u: the returned map moved (original %d, ours %d) or the call counts differ (%zu / %zu)", seed,
                     stableOriginal, stableOurs, original.size(), ours.size());
            return;
        }
        for (size_t i = 0; i < original.size(); ++i)
            if (original[i] != ours[i]) {
                ctx.Fail("seed %u call %zu:\n  original %s\n  ours     %s", seed, i, original[i].c_str(), ours[i].c_str());
                return;
            }
    }
}
