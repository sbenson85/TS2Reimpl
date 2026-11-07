// Differential test for the shape behaviour's event registry 0xa5f010 (lane SK, scenegraph H1b part 7;
// nGZSceneGraph::cShapeBehaviorTraits::RegistryFor in src/sg/cShape.cpp): the cAbstractBehaviorTypeT<cIShape> registry built
// on first use, with `attribute` stored under `id` when id is not 0. Folded into it: the holder 0xa59480 (run first on every
// call, its once byte 0x10f6328 skipping it after the first) and the parent's map getter 0xa480c0 (0x3a5d00 with id 0).
//
// The original's first-use build cannot run in the harness (the holder and the map's function-local static call the CRT's
// thread-safe init and atexit, traps here), so the original side starts from the state that build leaves, made by the test
// from what the build is: the holder's base step 0x2570a0 (run for real: the base events 0x1464f0 into the base registry, the
// connection and subordinate events into the object-graph registry 0x264010), the parent's map given 0xdd041f30 (0x3a5d00,
// real, record 0x106ba08), the map 0x10f6318 a copy of the parent's map (the shared map 0x10da02c), then the four records the
// holder stores, read from the image's data (0x106ba50 with 4 in the last word, 0x106ba68 with 0xc, 0x106ba20, 0x106ba38).
// The guards and the once byte are marked done. Ours starts from nothing (ResetRegistryForTest) and builds its own on the first
// call. Both sides see the same planted base registry (0x1485f0 stood in), object-graph registry and shared map (statics
// planted done, sgfx::RegistryScope). Compared after every call: the returned map's identity (stable across calls) and every
// record of the four registries: key, id, name (by content), the three words and the last.
//
// Verified here: 0xa5f010 and 0xa59480 (holder). 0xa480c0 stays the game's: another registry (0xa49210) calls it too; ours
// calls 0x3a5d00 with id 0, which is what it does.
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "sgfx.h"

#include "gz/cGZEventReflector.h"
#include "sg/cLinearFogLight.h"
#include "sg/cObjectGraphNodeImpl.h"
#include "sg/cShape.h"

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
using nGZSceneGraph::cShapeBehaviorTraits;

namespace {

constexpr uint32_t kBaseMap = 0x10c94f4, kBaseMapGuard = 0x10c94fc, kBaseOnce = 0x10c9510, kBaseHolderGuard = 0x10c9518;
constexpr sgfx::RegistryStatics kObjectGraph{0x10cc9f4, 0x10cc9fc, 0x10cca1c, 0x10cca24};
constexpr sgfx::RegistryStatics kSimple{0x10da02c, 0x10da034, 0x10da03c, 0x10da044}; // the parent's map (0x3a5d00)
constexpr uint32_t kMap = 0x10f6318, kMapGuard = 0x10f6320, kOnce = 0x10f6328, kHolder = 0x10f632c, kHolderGuard = 0x10f6330;
constexpr uint32_t kParentEvent = 0xdd041f30, kConnection = 0x7b54c99c, kSubordinates = 0x1d05597b;
constexpr uint32_t kShapeEvents[] = {0x9bb24282, 0x2a7e3a48, 0x3d44b140, 0xedf79966};

char gNames[8][12];

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
        save(kMapGuard, 4);
        save(kOnce, 1);
        save(kHolder, 4);
        save(kHolderGuard, 4);
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
        ctx.Original<void(__thiscall *)(void *)>(0x2570a0)(nullptr);
        cEventAttribute parentRecord = *GameAddress<const cEventAttribute>(0x106ba08);
        ctx.Original<cEventAttributeMap *(__cdecl *)(uint32_t, cEventAttribute *)>(0x3a5d00)(kParentEvent, &parentRecord);
        mine_ = ::new (GameAddress<void>(kMap)) cEventAttributeMap(*GameAddress<cEventAttributeMap>(kSimple.map));
        const auto record = [](uint32_t rva, uint32_t last, bool replace) {
            cEventAttribute a = *GameAddress<const cEventAttribute>(rva);
            if (replace) a.mWord14 = last;
            return a;
        };
        (*mine_)[0x9bb24282] = record(0x106ba50, 4, true);
        (*mine_)[0x2a7e3a48] = record(0x106ba68, 0xc, true);
        (*mine_)[0x3d44b140] = record(0x106ba20, 0, false);
        (*mine_)[0xedf79966] = record(0x106ba38, 0, false);
        *GameAddress<int32_t>(kMapGuard) = INT_MIN;
        *GameAddress<uint8_t>(kOnce) = 1;
        *GameAddress<int32_t>(kHolderGuard) = INT_MIN;
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

std::string All(const cEventAttributeMap *map)
{
    return Snapshot(map) + " | parent " + Snapshot(GameAddress<cEventAttributeMap>(kSimple.map)) + " | object graph " +
           Snapshot(GameAddress<cEventAttributeMap>(kObjectGraph.map)) + " | base " +
           Snapshot(GameAddress<cEventAttributeMap>(kBaseMap));
}

using Fn = cEventAttributeMap *(__cdecl *)(uint32_t, const cEventAttribute *);

// Runs one sequence on a side; returns the snapshots after every call and whether the returned map stayed the same.
std::vector<std::string> Run(difftest::Context &ctx, uint32_t seed, bool ours, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    Scope scope;
    sgfx::RegistryScope objectGraph{kObjectGraph};
    sgfx::RegistryScope parent{kSimple};
    shape::StandIn baseStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<kBaseMap>};
    // 0x264010 and 0x3a5d00 are ours now: both sides stand over the planted maps, as the original runs over them.
    shape::StandIn objectGraphStand{0x264010, &nGZSceneGraph::cObjectGraphBehaviorTraits::RegistryFor,
                                    &pdfx::StandRegistry<kObjectGraph.map>};
    shape::StandIn simpleStand{0x3a5d00, &nGZSceneGraph::cSimpleLightBehaviorTraits::RegistryFor,
                               &pdfx::StandRegistry<kSimple.map>};
    if (ours)
        cShapeBehaviorTraits::ResetRegistryForTest();
    else
        scope.PlantOriginalRegistry(ctx);
    const uint32_t ids[] = {0,          0,          0x10,       0x11,        kConnection, kSubordinates, kParentEvent,
                            0x9bb24282, 0x2a7e3a48, 0x3d44b140, 0xedf79966, 0x1234,      0x77,          0xfffffff0u,
                            rng(),      rng()};
    for (unsigned i = 0; i < 8; ++i) std::snprintf(gNames[i], sizeof gNames[i], "n%u_%u", seed % 100, i);
    std::vector<std::string> out;
    cEventAttributeMap *first = nullptr;
    stable = true;
    const unsigned calls = 1 + rng() % 6;
    for (unsigned c = 0; c < calls; ++c) {
        const uint32_t id = ids[rng() % (sizeof ids / sizeof ids[0])];
        cEventAttribute attribute{rng() % 5 == 0 ? id : rng(), gNames[rng() % 8], rng() % 4, rng(), rng() % 3, rng() % 16};
        cEventAttributeMap *map = ours ? cShapeBehaviorTraits::RegistryFor(id, &attribute)
                                       : ctx.Original<Fn>(0xa5f010)(id, &attribute);
        if (!first) first = map;
        stable = stable && map == first;
        out.push_back(All(map));
    }
    // The plain registry the slots use (id 0).
    if (ours) {
        out.push_back(All(cShapeBehaviorTraits::Registry()));
    } else {
        cEventAttribute defaults{0, "", 0, 0, 0, 0};
        out.push_back(All(ctx.Original<Fn>(0xa5f010)(0, &defaults)));
    }
    if (ours) cShapeBehaviorTraits::ResetRegistryForTest(); // (its copy holds names from this run)
    return out;
}

} // namespace

DIFF_TEST(SG_Shape_Registry, 0xa5f010)
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
