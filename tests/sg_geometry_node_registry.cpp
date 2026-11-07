// Differential test for cGeometryNode's event registry 0xa42f30 (lane QZ, scenegraph D3 part 6;
// cGeometryNodeBehaviorTraits::RegistryFor in src/sg/cGeometryNodeClass.cpp): the class's registry built on first use, with
// `attribute` stored under `id` when id is not 0.
//
// The original's first-use build cannot run in the harness (its holder 0xa3cf50 and the map's function-local static call the
// CRT's thread-safe init and atexit, traps here), so the original side starts from the state that build leaves, made by the
// test from what the build is: the holder's base step 0xa3cf50 -> 0x2570a0 (run for real: the base events 0x1464f0 into the
// base registry, the connection and subordinate events into the object-graph registry 0x264010), the map 0x10f6074 a copy of
// the object-graph registry, then the four records the holder stores, read from the image's data (0x106a528, 0x106a540, and
// 0x106a558 / 0x106a5a0 with 4 in the last word). The guards and the holder's once byte are marked done. Ours starts from
// nothing (ResetRegistryForTest) and builds its own on the first call. Both sides see the same planted base registry (the
// base template 0x1485f0 stood in, as tests/gz_draw2d_registry.cpp does) and object-graph registry (its statics planted
// done, sgfx::RegistryScope). Compared after every call: the returned map's identity (stable across calls) and every record
// of the three registries: key, id, name (by content), the three words and the last.
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "sgfx.h"

#include "gz/cGZEventReflector.h"
#include "sg/cGeometryNode.h"
#include "sg/cObjectGraphNodeImpl.h"

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
using nGZSceneGraph::cGeometryNodeBehaviorTraits;

namespace {

constexpr uint32_t kBaseMap = 0x10c94f4, kBaseMapGuard = 0x10c94fc, kBaseOnce = 0x10c9510, kBaseHolderGuard = 0x10c9518;
constexpr sgfx::RegistryStatics kObjectGraph{0x10cc9f4, 0x10cc9fc, 0x10cca1c, 0x10cca24};
constexpr uint32_t kMap = 0x10f6074, kMapGuard = 0x10f607c, kOnce = 0x10f6084, kHolder = 0x10f6088, kHolderGuard = 0x10f608c;
constexpr uint32_t kBuildEvent = 0xbbb112f6, kDeviceEvent = 0xca7ccbbc, kConnection = 0x7b54c99c, kSubordinates = 0x1d05597b;

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
        mine_ = ::new (GameAddress<void>(kMap)) cEventAttributeMap(*GameAddress<cEventAttributeMap>(kObjectGraph.map));
        const auto record = [](uint32_t rva, bool four) {
            cEventAttribute a = *GameAddress<const cEventAttribute>(rva);
            if (four) a.mWord14 = 4;
            return a;
        };
        (*mine_)[0x9bb24282] = record(0x106a528, false);
        (*mine_)[0x2a7e3a48] = record(0x106a540, false);
        (*mine_)[kBuildEvent] = record(0x106a558, true);
        (*mine_)[kDeviceEvent] = record(0x106a5a0, true);
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
    return Snapshot(map) + " | object graph " + Snapshot(GameAddress<cEventAttributeMap>(kObjectGraph.map)) + " | base " +
           Snapshot(GameAddress<cEventAttributeMap>(kBaseMap));
}

using Fn = cEventAttributeMap *(__cdecl *)(uint32_t, const cEventAttribute *);

// Runs one sequence on a side; returns the snapshots after every call and whether the returned map stayed the same.
std::vector<std::string> Run(difftest::Context &ctx, uint32_t seed, bool ours, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    Scope scope;
    sgfx::RegistryScope objectGraph{kObjectGraph};
    shape::StandIn baseStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<kBaseMap>};
    shape::StandIn objectGraphStand{0x264010, &nGZSceneGraph::cObjectGraphBehaviorTraits::RegistryFor,
                                    &pdfx::StandRegistry<kObjectGraph.map>};
    if (ours)
        cGeometryNodeBehaviorTraits::ResetRegistryForTest();
    else
        scope.PlantOriginalRegistry(ctx);
    const uint32_t ids[] = {0,           0,          0x10,         0x11,       kConnection, kSubordinates, 0x9bb24282,
                            0x2a7e3a48,  kBuildEvent, kDeviceEvent, 0x1234,     0x77,        0xfffffff0u,   rng(), rng()};
    for (unsigned i = 0; i < 8; ++i) std::snprintf(gNames[i], sizeof gNames[i], "n%u_%u", seed % 100, i);
    std::vector<std::string> out;
    cEventAttributeMap *first = nullptr;
    stable = true;
    const unsigned calls = 1 + rng() % 6;
    for (unsigned c = 0; c < calls; ++c) {
        const uint32_t id = ids[rng() % (sizeof ids / sizeof ids[0])];
        cEventAttribute attribute{rng() % 5 == 0 ? id : rng(), gNames[rng() % 8], rng() % 4, rng(), rng() % 3, rng() % 16};
        cEventAttributeMap *map = ours ? cGeometryNodeBehaviorTraits::RegistryFor(id, &attribute)
                                       : ctx.Original<Fn>(0xa42f30)(id, &attribute);
        if (!first) first = map;
        stable = stable && map == first;
        out.push_back(All(map));
    }
    // The plain registry the slots use (id 0).
    if (ours) {
        out.push_back(All(cGeometryNodeBehaviorTraits::Registry()));
    } else {
        cEventAttribute defaults{0, "", 0, 0, 0, 0};
        out.push_back(All(ctx.Original<Fn>(0xa42f30)(0, &defaults)));
    }
    if (ours) cGeometryNodeBehaviorTraits::ResetRegistryForTest(); // (its copy holds names from this run)
    return out;
}

} // namespace

DIFF_TEST(SG_GeometryNode_Registry, 0xa42f30)
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
