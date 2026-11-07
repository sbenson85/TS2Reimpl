// Differential tests for the two event registries every scene-graph behaviour hangs off (lane SL, scenegraph H1b part 8):
//   SG_ObjectGraph_Registry  0x264010 (nGZSceneGraph::cObjectGraphBehaviorTraits::RegistryFor in src/sg/cObjectGraphNodeImpl.cpp),
//                            the object-graph nodes' registry, with its holder 0x255910 folded in, and its id-0 form 0x25f030;
//   SG_SimpleLight_Registry  0x3a5d00 (nGZSceneGraph::cSimpleLightBehaviorTraits::RegistryFor in src/sg/cLinearFogLight.cpp),
//                            the shared map of the simple lights, the standard lights' and the shape's parent behaviour, with
//                            its holders 0x3a3e60 and 0x3a4090 folded in, and its id-0 form 0xa480c0.
// Each is a function-local static map built on first use and returned from every call, `attribute` stored under `id` first
// when id is not 0. The first-use build of the object-graph map: the base events (0x1464f0) into the base registry, the map a
// copy of the base registry (0x1480d0), then the connection and subordinates events (records 0xff832c, 0xff8344) stored. The
// shared map's: the object-graph registry's build, the map a copy of that registry (0x25f030 is 0x264010 with id 0), then the
// event 0xdd041f30 stored (record 0x100b6e8).
// Lane SQ: the records the registries store are owned now (src/sg/SGEventRecords.h), not read from the image. Verified in the
// unpacked image (id, name text, words 08..14): 0xff832c GraphConnChanged, 0xff8344 SubordinateListChanged, 0xff835c
// PartitioningUpdateReqStateChanged, 0xff8374 IncludeInPartitionStateChanged, 0xff838c BoneID, 0xff83a4 BoneTranslation, 0xff83bc
// BoneOrientation, 0xff83d4 BoneTransform, 0x100b6e8 / 0x106abb0 / 0x106ba08 BoundsUpdated, 0x106acf8 LightDependentAttributesChanged,
// 0x106ab4c / 0x106a558 BuildGeometry, 0x106a528 / 0x106ba50 GeometryNodeUpdated, 0x106a540 / 0x106ba68 HandleGeometryNodeUpdated,
// 0x106ba20 MaterialBindingsUpdated, 0x106ba38 PartCountUpdated, 0xff7890 / 0x106a5a0 CurrentDeviceEvent, 0xff78a8
// CurrentDeviceRenderSceneEvent. The tests compare the maps' name TEXT (Snapshot prints %s), so the name pointer is not compared;
// the registries are read for id, text and words (no reader of a name pointer found in our ports; the pointer identity is not kept). Mutants (a word, a name)
// are killed by the Registry tests.
//
// The originals' first-use builds cannot run in the harness (the holders and the maps' function-local statics call the CRT's
// thread-safe init and atexit, traps here), so the original side starts from the state a build leaves, made by the test from
// what the build is: the base events run for real (0x1464f0), the map a copy of the base registry placed at the static with its
// guards marked done, then the holder's own work run for real over it (0x2570a0 stores the two object-graph events through the
// real 0x264010; 0x3a4090 runs 0x2570a0 and stores 0xdd041f30 through the real 0x3a5d00). Ours starts from nothing
// (ResetRegistryForTest) and builds on the first call. Both sides see the same base registry (0x1485f0 stood in over the planted
// map at 0x10c94f4). Compared after every call: the returned map's identity (stable across calls) and every record of the
// registries involved: key, id, name (by content), the three words and the last.
//
// Verified here: 0x264010 and its holder 0x255910, 0x25f030 (not verified: the node registry 0x263f30 also calls it; the original wrapper is only checked to return the same map as id 0), 0x3a5d00
// and its holders 0x3a3e60 / 0x3a4090, and 0xa480c0 (called on the original side, likewise).
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "sgfx.h"

#include "gz/cGZEventReflector.h"
#include "sg/SGBehaviorCopies.h" // DisplayListRegistryFor
#include "sg/cCompositionTreeNode.h"
#include "sg/cGeometryBuilder.h"
#include "sg/cLinearFogLight.h"
#include "sg/cObjectGraphNodeImpl.h"
#include "sg/cStandardLight.h"
#include "sg/cTSSGSystem.h"
#include "sg/cTransformNode.h"

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
using nGZSceneGraph::cObjectGraphBehaviorTraits;
using nGZSceneGraph::cSimpleLightBehaviorTraits;

namespace {

constexpr uint32_t kBaseMap = 0x10c94f4;
constexpr sgfx::RegistryStatics kBase{0x10c94f4, 0x10c94fc, 0x10c9510, 0x10c9518};
constexpr sgfx::RegistryStatics kObjectGraph{0x10cc9f4, 0x10cc9fc, 0x10cca1c, 0x10cca24};
constexpr sgfx::RegistryStatics kSimple{0x10da02c, 0x10da034, 0x10da03c, 0x10da044};
constexpr uint32_t kConnection = 0x7b54c99c, kSubordinates = 0x1d05597b, kParentEvent = 0xdd041f30;

char gNames[8][12];

// The four statics of a registry, saved and restored; the guards and the once byte marked done on request.
class Statics {
public:
    explicit Statics(const sgfx::RegistryStatics &at) : at_(at)
    {
        std::memcpy(map_, GameAddress<void>(at.map), sizeof map_);
        mapGuard_ = *GameAddress<int32_t>(at.mapGuard);
        once_ = *GameAddress<uint8_t>(at.once);
        holderGuard_ = *GameAddress<int32_t>(at.holderGuard);
    }
    // A new map at the static, copying `from` when given; the guards and once byte done.
    cEventAttributeMap *Place(const cEventAttributeMap *from)
    {
        placed_ = from ? ::new (GameAddress<void>(at_.map)) cEventAttributeMap(*from)
                       : ::new (GameAddress<void>(at_.map)) cEventAttributeMap;
        *GameAddress<int32_t>(at_.mapGuard) = INT_MIN;
        *GameAddress<uint8_t>(at_.once) = 1;
        *GameAddress<int32_t>(at_.holderGuard) = INT_MIN;
        return placed_;
    }
    ~Statics()
    {
        if (placed_) placed_->~cEventAttributeMap();
        std::memcpy(GameAddress<void>(at_.map), map_, sizeof map_);
        *GameAddress<int32_t>(at_.mapGuard) = mapGuard_;
        *GameAddress<uint8_t>(at_.once) = once_;
        *GameAddress<int32_t>(at_.holderGuard) = holderGuard_;
    }
    Statics(const Statics &) = delete;
    Statics &operator=(const Statics &) = delete;

private:
    sgfx::RegistryStatics at_;
    uint8_t map_[sizeof(cEventAttributeMap)];
    int32_t mapGuard_, holderGuard_;
    uint8_t once_;
    cEventAttributeMap *placed_ = nullptr;
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
using Fn0 = cEventAttributeMap *(__cdecl *)();
using Thiscall = void *(__thiscall *)(void *);

// What the test plants on either side. `chain` 1: the object-graph registry; 2: the shared map too.
struct Side {
    Statics base{kBase}, objectGraph{kObjectGraph}, simple{kSimple};
    cEventAttributeMap *baseMap = nullptr, *objectGraphMap = nullptr, *simpleMap = nullptr;
    shape::StandIn baseStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<kBaseMap>};

    Side()
    {
        baseMap = base.Place(nullptr);
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof gNames[i], "base%u", i);
            (*baseMap)[0x10 + i] = {0x10 + i, gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
    }
    // The state the original's first-use build leaves (see the file comment).
    void PlantOriginal(difftest::Context &ctx, int chain)
    {
        ctx.Original<Thiscall>(0x1464f0)(nullptr); // the base events
        objectGraphMap = objectGraph.Place(baseMap);
        ctx.Original<Thiscall>(0x2570a0)(nullptr); // the holder's work: the two events through the real 0x264010
        if (chain < 2) return;
        simpleMap = simple.Place(objectGraphMap);
        ctx.Original<Thiscall>(0x3a4090)(nullptr); // 0x2570a0 again, then 0xdd041f30 through the real 0x3a5d00
    }
};

std::string All(const cEventAttributeMap *map, const cEventAttributeMap *other, const cEventAttributeMap *base)
{
    return Snapshot(map) + " | " + Snapshot(other) + " | base " + Snapshot(base);
}

constexpr uint32_t kIds[] = {0,          0,          0x10,       0x11,       kConnection, kSubordinates, kParentEvent,
                             0x9bb24282, 0x1234,     0x77,       0xfffffff0u};

struct Call {
    uint32_t id;
    cEventAttribute attribute;
};

std::vector<Call> MakeCalls(std::mt19937 &rng, uint32_t seed)
{
    for (unsigned i = 0; i < 8; ++i) std::snprintf(gNames[i], sizeof gNames[i], "n%u_%u", seed % 100, i);
    std::vector<Call> calls;
    const unsigned count = 1 + rng() % 6;
    for (unsigned c = 0; c < count; ++c) {
        const uint32_t pick = rng() % (sizeof kIds / sizeof kIds[0] + 2);
        const uint32_t id = pick < sizeof kIds / sizeof kIds[0] ? kIds[pick] : rng();
        calls.push_back({id, {rng() % 5 == 0 ? id : rng(), gNames[rng() % 8], rng() % 4, rng(), rng() % 3, rng() % 16}});
    }
    return calls;
}

// One sequence on one side: the snapshots after every call and whether the returned map stayed the same. `simpleTest`
// selects the registry under test.
std::vector<std::string> Run(difftest::Context &ctx, uint32_t seed, bool ours, bool simpleTest, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    Side side;
    if (ours) {
        cObjectGraphBehaviorTraits::ResetRegistryForTest();
        cSimpleLightBehaviorTraits::ResetRegistryForTest();
    } else {
        side.PlantOriginal(ctx, simpleTest ? 2 : 1);
    }
    const uint32_t rva = simpleTest ? 0x3a5d00 : 0x264010;
    const auto call = [&](uint32_t id, const cEventAttribute *attribute) {
        if (!ours) return ctx.Original<Fn>(rva)(id, attribute);
        return simpleTest ? cSimpleLightBehaviorTraits::RegistryFor(id, attribute)
                          : cObjectGraphBehaviorTraits::RegistryFor(id, attribute);
    };
    // The object-graph map as each side holds it (ours is built by the call, so asked for through the same function).
    const auto objectGraphMap = [&]() -> const cEventAttributeMap * {
        const cEventAttribute defaults{0, "", 0, 0, 0, 0};
        return ours ? cObjectGraphBehaviorTraits::RegistryFor(0, &defaults) : side.objectGraphMap;
    };
    std::vector<std::string> out;
    cEventAttributeMap *first = nullptr;
    stable = true;
    for (const Call &c : MakeCalls(rng, seed)) {
        cEventAttributeMap *const map = call(c.id, &c.attribute);
        if (!first) first = map;
        stable = stable && map == first;
        const cEventAttributeMap *const base = ours ? cAbstractBehaviorType::Registry() : side.baseMap;
        out.push_back(All(map, simpleTest ? objectGraphMap() : map, base));
    }
    // The plain forms: id 0 through the registry, and the original's own id-0 wrappers (0x25f030 / 0xa480c0), which must
    // return the same map.
    const cEventAttribute defaults{0, "", 0, 0, 0, 0};
    cEventAttributeMap *const plain = call(0, &defaults);
    stable = stable && plain == first;
    if (!ours) {
        cEventAttributeMap *const wrapper = simpleTest ? ctx.Original<Fn0>(0xa480c0)() : ctx.Original<Fn0>(0x25f030)();
        stable = stable && wrapper == first;
    }
    out.push_back(All(plain, simpleTest ? objectGraphMap() : plain, ours ? cAbstractBehaviorType::Registry() : side.baseMap));
    if (ours) {
        cObjectGraphBehaviorTraits::ResetRegistryForTest(); // (the copies hold names from this run)
        cSimpleLightBehaviorTraits::ResetRegistryForTest();
    }
    return out;
}

void Compare(difftest::Context &ctx, bool simpleTest)
{
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        bool stableOriginal = false, stableOurs = false;
        const std::vector<std::string> original = Run(ctx, seed, false, simpleTest, stableOriginal);
        const std::vector<std::string> ours = Run(ctx, seed, true, simpleTest, stableOurs);
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

} // namespace

DIFF_TEST(SG_ObjectGraph_Registry, 0x264010) { Compare(ctx, false); }

DIFF_TEST(SG_SimpleLight_Registry, 0x3a5d00) { Compare(ctx, true); }

// ---- lane SM (scenegraph H1b part 9): the five registries below the object-graph and simple-light ones ----
//   SG_CompositionTree_Registry   0x263f30 (cCompositionTreeNodeBehaviorTraits::RegistryFor), holder 0x255860 folded in
//   SG_TransformNode_Registry     0x2640f0 (cTransformNodeBehaviorTraits::RegistryFor), holder 0x2559c0 folded in
//   SG_StandardLight_Registry     0xa49210 (cStandardLightBehaviorTraits::RegistryFor), holder 0xa457f0 folded in
//   SG_GeometryBuilder_Registry   0xa453a0 (cGeometryBuilderBehaviorTraits::RegistryFor), holder 0xa44a20 folded in
//   SG_TSSGSystem_Registry        0x253de0 (nTSSG::cTSSGSystemBehaviorTraits::RegistryFor), holder 0x2455e0 folded in
//   SG_DisplayList_Registry       0xaa2210 (behavior_copies::DisplayListRegistryFor), holder 0xa9a020 folded in
// Each is the same function-local static: built on first use as a copy of its parent's map after the holder's registrations,
// returned from every call, `attribute` stored under `id` first when id is not 0. Parents: the node registry's is the
// object-graph map (0x25f030 is its id-0 form), the transform registry's the node map (0x25efe0), the standard light's the
// shared simple-light map (0xa480c0, with 0xdd041f30 given the standard light's own record 0x106abb0 first), the builder's
// the TSSG system's and the display list's the base registry (0x1485f0). The events the holders add (record addresses from the holders; the
// ones through 0x2570a0 / 0x257030 / 0x257110 are those functions run for real on the original side):
//   node       0x0a884a15 (0xff835c), 0xcc6c9992 (0xff8374), after the object-graph events (0x257030)
//   transform  0xc9de8b96 (0xff838c), 0x9b59fb00 (0xff83d4), 0xfb5b3383 (0xff83a4), 0x9b5b3506 (0xff83bc), after 0x257030's
//   standard   0x69f75eba (0x106acf8), after the object-graph events and 0xdd041f30 (0x106abb0)
//   builder    0xbbb112f6 (0x106ab4c), after the base events
//   TSSG       0xca7ccbbc (0xff7890), 0x2b5745f8 (0xff78a8), each with its last word 4, after the base events
//   display    0xca7ccbbc (0x106df00), its last word 4, after the base events
// The original side is planted as above (the maps placed at the statics, the guards and once bytes done, the holder's own
// registrations then run through the real registry); ours starts from nothing (ResetRegistryForTest). 0x257030 / 0x257110 /
// 0x2570a0 are not replaced (other holders call them): the node and transform tests cover our copies of their work.
namespace sm {

constexpr sgfx::RegistryStatics kStandard{0x10f6138, 0x10f6140, 0x10f6150, 0x10f6158};
constexpr sgfx::RegistryStatics kBuilder{0x10f60e8, 0x10f60f0, 0x10f60f8, 0x10f6100};
constexpr sgfx::RegistryStatics kTssg{0x10cc89c, 0x10cc8a4, 0x10cc8ac, 0x10cc8b4};
constexpr sgfx::RegistryStatics kDisplayList{0x10f661c, 0x10f6624, 0x10f662c, 0x10f6634};

enum class Reg { Node, Transform, Standard, Builder, Tssg, DisplayList };

// The shape's holder (and SL's own build) store a different record under 0xdd041f30 in the shared simple-light map before the
// standard light's first use, and the standard light's first use then overwrites it with 0x106abb0's. (The two image records
// are equal in content, so only a differing earlier record makes the overwrite observable.) Odd seeds plant one on both sides.
const cEventAttribute kEarlier{0xdd041f30, "EarlierBounds", 9, 0x55u, 7, 6};
bool gEarlier = false;

struct Maps {
    const cEventAttributeMap *m[3] = {};
    unsigned count = 0;
    void Add(const cEventAttributeMap *map) { m[count++] = map; }
};

// Everything one side holds: the planted statics (restored at the end) and, for the original, the maps placed.
struct Side2 {
    Statics base{kBase}, objectGraph{kObjectGraph}, node{sgfx::kNodeRegistry}, transform{sgfx::kTransformRegistry},
        simple{kSimple}, standard{kStandard}, builder{kBuilder}, tssg{kTssg}, displayList{kDisplayList};
    cEventAttributeMap *baseMap = nullptr, *ogMap = nullptr, *nodeMap = nullptr, *transformMap = nullptr, *simpleMap = nullptr,
                       *standardMap = nullptr, *builderMap = nullptr, *tssgMap = nullptr, *displayListMap = nullptr;
    shape::StandIn baseStand{0x1485f0, &cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<kBaseMap>};

    Side2()
    {
        baseMap = base.Place(nullptr);
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof gNames[i], "base%u", i);
            (*baseMap)[0x10 + i] = {0x10 + i, gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
    }

    void PlantObjectGraph(difftest::Context &ctx)
    {
        ctx.Original<Thiscall>(0x1464f0)(nullptr); // the base events
        ogMap = objectGraph.Place(baseMap);
        ctx.Original<Thiscall>(0x2570a0)(nullptr); // the two object-graph events through the real 0x264010
    }
    void PlantNode(difftest::Context &ctx)
    {
        PlantObjectGraph(ctx);
        nodeMap = node.Place(ogMap);
        ctx.Original<Thiscall>(0x257030)(nullptr); // 0x2570a0, then the two node events through the real 0x263f30
    }
    void PlantOriginal(difftest::Context &ctx, Reg which)
    {
        switch (which) {
        case Reg::Node:
            PlantNode(ctx);
            break;
        case Reg::Transform:
            PlantNode(ctx);
            transformMap = transform.Place(nodeMap);
            ctx.Original<Thiscall>(0x257110)(nullptr); // 0x257030, then the four transform events through 0x2640f0
            break;
        case Reg::Standard: {
            PlantObjectGraph(ctx);
            simpleMap = simple.Place(ogMap);
            ctx.Original<Thiscall>(0x3a4090)(nullptr); // the shared map's own build: 0xdd041f30 from 0x100b6e8
            if (gEarlier) ctx.Original<Fn>(0x3a5d00)(0xdd041f30, &kEarlier);
            // The standard light's holder: 0xdd041f30 given its own record, the map made as a copy, then 0x69f75eba.
            ctx.Original<Fn>(0x3a5d00)(0xdd041f30, GameAddress<const cEventAttribute>(0x106abb0));
            standardMap = standard.Place(simpleMap);
            ctx.Original<Fn>(0xa49210)(0x69f75eba, GameAddress<const cEventAttribute>(0x106acf8));
            break;
        }
        case Reg::Builder:
            ctx.Original<Thiscall>(0x1464f0)(nullptr);
            builderMap = builder.Place(baseMap);
            ctx.Original<Fn>(0xa453a0)(0xbbb112f6, GameAddress<const cEventAttribute>(0x106ab4c));
            break;
        case Reg::Tssg: {
            ctx.Original<Thiscall>(0x1464f0)(nullptr);
            tssgMap = tssg.Place(baseMap);
            cEventAttribute record = *GameAddress<const cEventAttribute>(0xff7890);
            record.mWord14 = 4;
            ctx.Original<Fn>(0x253de0)(0xca7ccbbc, &record);
            record = *GameAddress<const cEventAttribute>(0xff78a8);
            record.mWord14 = 4;
            ctx.Original<Fn>(0x253de0)(0x2b5745f8, &record);
            break;
        }
        case Reg::DisplayList: { // the holder 0xa9a020: the base events, then 0xca7ccbbc (record 0x106df00) with its last word 4
            ctx.Original<Thiscall>(0x1464f0)(nullptr);
            displayListMap = displayList.Place(baseMap);
            cEventAttribute record = *GameAddress<const cEventAttribute>(0x106df00);
            record.mWord14 = 4;
            ctx.Original<Fn>(0xaa2210)(0xca7ccbbc, &record);
            break;
        }
        }
    }
    Maps OriginalMaps(Reg which) const
    {
        Maps maps;
        switch (which) {
        case Reg::Node: maps.Add(ogMap); maps.Add(nodeMap); break;
        case Reg::Transform: maps.Add(nodeMap); maps.Add(transformMap); break;
        case Reg::Standard: maps.Add(simpleMap); maps.Add(standardMap); break;
        case Reg::Builder: maps.Add(builderMap); break;
        case Reg::Tssg: maps.Add(tssgMap); break;
        case Reg::DisplayList: maps.Add(displayListMap); break;
        }
        return maps;
    }
};

void ResetAll()
{
    cObjectGraphBehaviorTraits::ResetRegistryForTest();
    cSimpleLightBehaviorTraits::ResetRegistryForTest();
    nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::ResetRegistryForTest();
    nGZSceneGraph::cTransformNodeBehaviorTraits::ResetRegistryForTest();
    nGZSceneGraph::cStandardLightBehaviorTraits::ResetRegistryForTest();
    nGZSceneGraph::cGeometryBuilderBehaviorTraits::ResetRegistryForTest();
    nTSSG::cTSSGSystemBehaviorTraits::ResetRegistryForTest();
    nGZSceneGraph::behavior_copies::ResetDisplayListRegistryForTest();
}

uint32_t RvaOf(Reg which)
{
    switch (which) {
    case Reg::Node: return 0x263f30;
    case Reg::Transform: return 0x2640f0;
    case Reg::Standard: return 0xa49210;
    case Reg::Builder: return 0xa453a0;
    case Reg::Tssg: return 0x253de0;
    case Reg::DisplayList: return 0xaa2210;
    }
    return 0;
}

cEventAttributeMap *OursCall(Reg which, uint32_t id, const cEventAttribute *attribute)
{
    switch (which) {
    case Reg::Node: return nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::RegistryFor(id, attribute);
    case Reg::Transform: return nGZSceneGraph::cTransformNodeBehaviorTraits::RegistryFor(id, attribute);
    case Reg::Standard: return nGZSceneGraph::cStandardLightBehaviorTraits::RegistryFor(id, attribute);
    case Reg::Builder: return nGZSceneGraph::cGeometryBuilderBehaviorTraits::RegistryFor(id, attribute);
    case Reg::Tssg: return nTSSG::cTSSGSystemBehaviorTraits::RegistryFor(id, attribute);
    case Reg::DisplayList: return nGZSceneGraph::behavior_copies::DisplayListRegistryFor(id, attribute);
    }
    return nullptr;
}

Maps OursMaps(Reg which)
{
    const cEventAttribute defaults{0, "", 0, 0, 0, 0};
    Maps maps;
    switch (which) {
    case Reg::Node:
        maps.Add(cObjectGraphBehaviorTraits::RegistryFor(0, &defaults));
        maps.Add(OursCall(which, 0, &defaults));
        break;
    case Reg::Transform:
        maps.Add(nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::RegistryFor(0, &defaults));
        maps.Add(OursCall(which, 0, &defaults));
        break;
    case Reg::Standard:
        maps.Add(cSimpleLightBehaviorTraits::RegistryFor(0, &defaults));
        maps.Add(OursCall(which, 0, &defaults));
        break;
    default: maps.Add(OursCall(which, 0, &defaults)); break;
    }
    return maps;
}

std::string AllMaps(const Maps &maps, const cEventAttributeMap *base)
{
    std::string out;
    for (unsigned i = 0; i < maps.count; ++i) out += Snapshot(maps.m[i]) + " | ";
    return out + "base " + Snapshot(base);
}

std::vector<std::string> RunSm(difftest::Context &ctx, uint32_t seed, bool ours, Reg which, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    Side2 side;
    gEarlier = which == Reg::Standard && (seed & 1) != 0;
    if (ours) {
        ResetAll();
        if (gEarlier) cSimpleLightBehaviorTraits::RegistryFor(0xdd041f30, &kEarlier);
    } else
        side.PlantOriginal(ctx, which);
    const uint32_t rva = RvaOf(which);
    const auto call = [&](uint32_t id, const cEventAttribute *attribute) {
        return ours ? OursCall(which, id, attribute) : ctx.Original<Fn>(rva)(id, attribute);
    };
    const auto base = [&]() { return ours ? cAbstractBehaviorType::Registry() : side.baseMap; };
    std::vector<std::string> out;
    cEventAttributeMap *first = nullptr;
    stable = true;
    for (const Call &c : MakeCalls(rng, seed)) {
        cEventAttributeMap *const map = call(c.id, &c.attribute);
        if (!first) first = map;
        stable = stable && map == first;
        out.push_back(AllMaps(ours ? OursMaps(which) : side.OriginalMaps(which), base()));
    }
    const cEventAttribute defaults{0, "", 0, 0, 0, 0};
    stable = stable && call(0, &defaults) == first;
    out.push_back(AllMaps(ours ? OursMaps(which) : side.OriginalMaps(which), base()));
    if (ours) ResetAll(); // (the copies hold names from this run)
    return out;
}

void CompareSm(difftest::Context &ctx, Reg which)
{
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        bool stableOriginal = false, stableOurs = false;
        const std::vector<std::string> original = RunSm(ctx, seed, false, which, stableOriginal);
        const std::vector<std::string> ours = RunSm(ctx, seed, true, which, stableOurs);
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

} // namespace sm

DIFF_TEST(SG_CompositionTree_Registry, 0x263f30) { sm::CompareSm(ctx, sm::Reg::Node); }

DIFF_TEST(SG_TransformNode_Registry, 0x2640f0) { sm::CompareSm(ctx, sm::Reg::Transform); }

DIFF_TEST(SG_StandardLight_Registry, 0xa49210) { sm::CompareSm(ctx, sm::Reg::Standard); }

DIFF_TEST(SG_GeometryBuilder_Registry, 0xa453a0) { sm::CompareSm(ctx, sm::Reg::Builder); }

DIFF_TEST(SG_TSSGSystem_Registry, 0x253de0) { sm::CompareSm(ctx, sm::Reg::Tssg); }

DIFF_TEST(SG_DisplayList_Registry, 0xaa2210) { sm::CompareSm(ctx, sm::Reg::DisplayList); }

// ---- the event-registering bodies the holders run: 0x2570a0, 0x257030 and 0x257110 ----
// Each is a thiscall whose `this` is unused and returned: it runs the one below it (0x1464f0 / 0x2570a0 / 0x257030) and then
// stores its own events through its registry's RegistryFor (0x264010 / 0x263f30 / 0x2640f0). The callees are stood in on both
// sides with loggers that note the call and every record stored (id, name text, words); the original's run and ours (through
// the adapter that is hooked) are compared on the log and the value returned, for three `this` values.
namespace body {

std::vector<uint32_t> gLog;

uint32_t TextHash(const char *text)
{
    uint32_t h = 2166136261u;
    for (; text && *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h;
}

template <uint32_t Tag>
__declspec(noinline) void *__fastcall StandBody(void *self, void *)
{
    gLog.push_back(Tag);
    return self;
}

template <uint32_t Tag>
__declspec(noinline) cEventAttributeMap *__cdecl StandRegistry(uint32_t id, const cEventAttribute *attribute)
{
    gLog.push_back(Tag);
    gLog.push_back(id);
    gLog.push_back(attribute->mId);
    gLog.push_back(TextHash(attribute->mName));
    gLog.push_back(attribute->mWord08);
    gLog.push_back(attribute->mWord0C);
    gLog.push_back(attribute->mWord10);
    gLog.push_back(attribute->mWord14);
    return reinterpret_cast<cEventAttributeMap *>(uintptr_t(0x1000 + Tag));
}

// Runs the body at `rva` and `adapter` for three `this` values and compares what each logged and returned.
void Compare(difftest::Context &ctx, uint32_t rva, void *(__fastcall *adapter)(void *, void *))
{
    const auto original = ctx.Original<void *(__thiscall *)(void *)>(rva);
    for (uintptr_t self : {uintptr_t(0x1234), uintptr_t(0), uintptr_t(0xdeadbeef)}) {
        gLog.clear();
        void *const returnedOriginal = original(reinterpret_cast<void *>(self));
        const std::vector<uint32_t> logOriginal = gLog;
        gLog.clear();
        void *const returnedOurs = adapter(reinterpret_cast<void *>(self), nullptr);
        if (returnedOriginal != reinterpret_cast<void *>(self) || returnedOurs != reinterpret_cast<void *>(self))
            ctx.Fail("this %zx: returned %p (original), %p (ours)", self, returnedOriginal, returnedOurs);
        if (logOriginal != gLog || gLog.empty())
            ctx.Fail("this %zx: the calls differ (%zu words original, %zu ours)", self, logOriginal.size(), gLog.size());
        ++ctx.cases;
    }
}

} // namespace body

// Verifies: 0x2570a0 (the object-graph events).
DIFF_TEST(SG_ObjectGraph_RegisterEvents, 0x2570a0)
{
    shape::StandIn base{0x1464f0, &cAbstractBehaviorType::RegisterBaseEvents, &body::StandBody<1>};
    shape::StandIn registry{0x264010, &cObjectGraphBehaviorTraits::RegistryFor, &body::StandRegistry<2>};
    body::Compare(ctx, 0x2570a0, &cObjectGraphBehaviorTraits::RegisterEventsAdapter);
}

// Verifies: 0x257030 (the node events).
DIFF_TEST(SG_CompositionTree_RegisterEvents, 0x257030)
{
    shape::StandIn below{0x2570a0, &cObjectGraphBehaviorTraits::RegisterEvents, &body::StandBody<1>};
    shape::StandIn registry{0x263f30, &nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::RegistryFor, &body::StandRegistry<2>};
    body::Compare(ctx, 0x257030, &nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::RegisterEventsAdapter);
}

// Verifies: 0x257110 (the transform events).
DIFF_TEST(SG_TransformNode_RegisterEvents, 0x257110)
{
    shape::StandIn below{0x257030, &nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::RegisterEvents,
                         &body::StandBody<1>};
    shape::StandIn registry{0x2640f0, &nGZSceneGraph::cTransformNodeBehaviorTraits::RegistryFor, &body::StandRegistry<2>};
    body::Compare(ctx, 0x257110, &nGZSceneGraph::cTransformNodeBehaviorTraits::RegisterEventsAdapter);
}
