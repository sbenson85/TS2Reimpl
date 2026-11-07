// Differential test for the shape-reference behaviour's event registry 0xa585a0 (lane TF, scenegraph H2f;
// nGZSceneGraph::cShapeRefNodeBehaviorTraits::RegistryFor in src/sg/cShapeRefNode.cpp): the cAbstractBehaviorTypeT<
// cIShapeRefNode> registry built on first use, with `attribute` stored under `id` when id is not 0. Folded into it: the
// holder 0xa53800 (run first on every call, its once byte 0x10f62e0 skipping it after the first).
//
// The original's first-use build cannot run in the harness (the holder and the map's function-local static call the CRT's
// thread-safe init and atexit, traps here), so the original side starts from the state that build leaves, made by the test
// from what the build is: the holder's transform step 0x257110 run for real (0x257030's node events, then the four bone
// events through the transform registry), the map 0x10f62d0 a copy of the transform map, then the holder's ten records stored
// through the real 0xa585a0 (guards and once byte marked done), read from the image's data: 0x106b76c, 0x106b79c, 0x106b7cc
// and 0x106b7e4 with 4 in the last word, then 0x106b7fc, 0x106b814, 0x106b82c, 0x106b844, 0x106b85c and 0x106b874 whole.
// Ours starts from nothing (ResetRegistryForTest) and builds its own on the first call, from its owned records. Both sides
// see the same planted base, object-graph, node and transform registries (0x1485f0, 0x264010, 0x263f30 and 0x2640f0 stood
// in over planted maps). Compared after every call: the returned map's identity (stable across calls) and every record of
// the five registries: key, id, name (by content), the three words and the last.
//
// Verified here: 0xa585a0 and 0xa53800 (holder).
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "sgfx.h"

#include "gz/cGZEventReflector.h"
#include "sg/cCompositionTreeNode.h"
#include "sg/cObjectGraphNodeImpl.h"
#include "sg/cShapeRefNode.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using namespace nGZEvent;
using nGZSceneGraph::cShapeRefNodeBehaviorTraits;

namespace {

constexpr uint32_t kBaseMap = 0x10c94f4, kBaseMapGuard = 0x10c94fc, kBaseOnce = 0x10c9510, kBaseHolderGuard = 0x10c9518;
constexpr sgfx::RegistryStatics kObjectGraph{0x10cc9f4, 0x10cc9fc, 0x10cca1c, 0x10cca24};
constexpr uint32_t kMap = 0x10f62d0, kMapGuard = 0x10f62d8, kOnce = 0x10f62e0, kHolder = 0x10f62e4, kHolderGuard = 0x10f62e8;

struct HolderRecord {
    uint32_t id, rva;
    bool lastWord4;
};
constexpr HolderRecord kRecords[] = {
    {0xdd041f30, 0x106b76c, true},  {0x2a7e3a48, 0x106b79c, true},  {0x3d44b140, 0x106b7cc, true},
    {0xedf79966, 0x106b7e4, true},  {0x69c7eb40, 0x106b7fc, false}, {0x09f5fdb0, 0x106b814, false},
    {0x09f5fdb6, 0x106b82c, false}, {0x09f5fdbc, 0x106b844, false}, {0x09f5fdc0, 0x106b85c, false},
    {0x8c798726, 0x106b874, false},
};

using Fn = cEventAttributeMap *(__cdecl *)(uint32_t, const cEventAttribute *);
using Thiscall = void(__thiscall *)(void *);

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
        *GameAddress<uint8_t>(kOnce) = 1;
        *GameAddress<int32_t>(kHolderGuard) = INT_MIN;
        ctx.Original<Thiscall>(0x257110)(nullptr);
        mine_ = ::new (GameAddress<void>(kMap)) cEventAttributeMap(*GameAddress<cEventAttributeMap>(sgfx::kTransformRegistry.map));
        *GameAddress<int32_t>(kMapGuard) = INT_MIN;
        for (const HolderRecord &r : kRecords) {
            cEventAttribute a = *GameAddress<const cEventAttribute>(r.rva);
            if (r.lastWord4) a.mWord14 = 4;
            ctx.Original<Fn>(0xa585a0)(r.id, &a);
        }
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
    return Snapshot(map) + " | transform " + Snapshot(GameAddress<cEventAttributeMap>(sgfx::kTransformRegistry.map)) +
           " | node " + Snapshot(GameAddress<cEventAttributeMap>(sgfx::kNodeRegistry.map)) + " | object graph " +
           Snapshot(GameAddress<cEventAttributeMap>(kObjectGraph.map)) + " | base " +
           Snapshot(GameAddress<cEventAttributeMap>(kBaseMap));
}

// Runs one sequence on a side; returns the snapshots after every call and whether the returned map stayed the same.
std::vector<std::string> Run(difftest::Context &ctx, uint32_t seed, bool ours, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    Scope scope;
    sgfx::RegistryScope objectGraph{kObjectGraph};
    sgfx::RegistryScope node{sgfx::kNodeRegistry};           // stands 0x263f30 over its map
    sgfx::RegistryScope transform{sgfx::kTransformRegistry}; // stands 0x2640f0 over its map
    shape::StandIn baseStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<kBaseMap>};
    shape::StandIn objectGraphStand{0x264010, &nGZSceneGraph::cObjectGraphBehaviorTraits::RegistryFor,
                                    &pdfx::StandRegistry<kObjectGraph.map>};
    if (ours)
        cShapeRefNodeBehaviorTraits::ResetRegistryForTest();
    else
        scope.PlantOriginalRegistry(ctx);
    const uint32_t ids[] = {0,          0,          0x10,       0x7b54c99c, 0xc9de8b96, 0xdd041f30, 0x2a7e3a48,
                            0x3d44b140, 0xedf79966, 0x69c7eb40, 0x09f5fdb0, 0x09f5fdb6, 0x09f5fdbc, 0x09f5fdc0,
                            0x8c798726, 0x1234,     0x77,       0xfffffff0u, rng(),     rng()};
    for (unsigned i = 0; i < 8; ++i) std::snprintf(gNames[i], sizeof gNames[i], "n%u_%u", seed % 100, i);
    std::vector<std::string> out;
    cEventAttributeMap *first = nullptr;
    stable = true;
    const unsigned calls = 1 + rng() % 6;
    for (unsigned c = 0; c < calls; ++c) {
        const uint32_t id = ids[rng() % (sizeof ids / sizeof ids[0])];
        cEventAttribute attribute{rng() % 5 == 0 ? id : rng(), gNames[rng() % 8], rng() % 4, rng(), rng() % 3, rng() % 16};
        cEventAttributeMap *map = ours ? cShapeRefNodeBehaviorTraits::RegistryFor(id, &attribute)
                                       : ctx.Original<Fn>(0xa585a0)(id, &attribute);
        if (!first) first = map;
        stable = stable && map == first;
        out.push_back(All(map));
    }
    // The plain registry the slots use (id 0).
    if (ours) {
        out.push_back(All(cShapeRefNodeBehaviorTraits::Registry()));
    } else {
        cEventAttribute defaults{0, "", 0, 0, 0, 0};
        out.push_back(All(ctx.Original<Fn>(0xa585a0)(0, &defaults)));
    }
    if (ours) cShapeRefNodeBehaviorTraits::ResetRegistryForTest(); // (its copy holds names from this run)
    return out;
}

} // namespace

DIFF_TEST(SG_ShapeRefNode_Registry, 0xa585a0)
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
