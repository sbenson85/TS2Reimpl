// nTSSG::cDiffusionManager's portal lights (lane 261110l, scenegraph LD1b; src/sg/cDiffusionManager.h): the helpers
// 0x3c5a60 and 0x3c5c30 and slots 8-11 (0x3c4f20, 0x3c5050, 0x3c48a0, 0x3c4b30; lane 261110m slot 11). The light
// itself is src/sg/cLightingManagerPortalLight.cpp.
#include "sg/cDiffusionManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cTSObject.h"
#include "misc/SmallVirtuals.h"
#include "sg/SGLightingReceivers.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cLightRefNode.h"
#include "world/WorldHeights.h"
#include "world/WorldTileMath.h"
#include "world/cWorldDB.h"

#include <cmath>
#include <cstring>

namespace nTSSG {

namespace {

constexpr uint32_t kPortalIid = 0xfa1c39ed; // the portal face of a node's object
constexpr uint32_t kPresetColor = 0x10da3d0; // .data, zero at load (slot 11's only): the colour a light starts with
constexpr uint32_t kYAxis = 0x100e7cc;       // .data (0, 1, 0)
constexpr uint32_t kZAxis = 0x100e7d8;       // .data (0, 0, 1)
constexpr double kTwoPi = 6.2831854820251465; // 0xdbed40: the float 2 pi widened

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;

uint32_t WordOf(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

void *SceneGraphSystem() { return OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); }

cITSSGSystemManagers *SystemManagers() { return static_cast<cITSSGSystemManagers *>(SceneGraphSystem()); }

// The scene the scene manager (the system's slot 33) holds (its slot 17), not counted.
nGZSceneGraph::cICompositionTreeNode *TheScene()
{
    return SceneOf(SystemManagers()->GetSceneManager());
}

// The portal node's owner (slot 120), the portal kind object: an nTSSG::cTSObject.
__forceinline void *OwnerOf(nGZSceneGraph::cICompositionTreeNode *node) { return static_cast<cIPortalNode *>(node)->GetOwner(); }

// The partition manager the placement reference holds (a counted cIGZUnknown, nTSWorld::cPartitionManager).
cIPartitionManager *Partitions(const DiffusionRef &ref) { return reinterpret_cast<cIPartitionManager *>(ref.mObject); }

} // namespace

void *__cdecl cDiffusionManager::PortalOf(nGZSceneGraph::cICompositionTreeNode *start)
{
    void *held = nullptr;
    void *found = nullptr;
    for (nGZSceneGraph::cICompositionTreeNode *node = start; node;) {
        DropHeld(held); // a failed QueryInterface that wrote a reference leaves it for the next ask to release
        if (node->QueryInterface(kPortalIid, &held)) {
            found = held;
            break;
        }
        node = node->GetParent();
    }
    if (held) ReleaseThroughBase(held);
    return found;
}

bool cDiffusionManager::LotValueBelow100(void *object)
{
    // Slot 42 gives a position by value: a 12-byte vector through the hidden pointer (its one argument, ret 4), the
    // pointer back (the object's class is not identified: the portal's kind object). The original pushes the next
    // call's last two arguments (1, 0) before it.
    float position[3];
    void *const where = As<ifaces::cTSObject>(object)->GetDefinitionKey(position); // slot 42 (0x546a80): the position
    const uint32_t level = static_cast<uint16_t>(Partitions(mPartitions)->GetLevelAt(where, 0u, 1u));
    const cLightingManager *const manager = SystemManagers()->GetLightingManager();
    return static_cast<int16_t>(Call<36>(manager, level)) < 100; // receiver: nTSSG::cLightingManager (slot 36: LotValue)
}

// Slots 8 and 9: the portal lights of every node of the scene that has a portal kind object (slot 9: only the lots whose
// value is 100 or more, and a kind object that answers no to its slot 99), by slot 10, into `out` (emptied first).
void cDiffusionManager::PortalLightsOfScene(std::vector<cPortalLight> *out)
{
    out->resize(0);
    // The scene (counted while it is used)
    nGZSceneGraph::cICompositionTreeNode *const scene = TheScene();
    if (scene) scene->AddRef();
    nRZSceneGraph::cBranchNodesIIDFilter filter{GameAddress<const void>(nRZSceneGraph::cBranchNodesIIDFilter::kVTable), kPortalIid};
    {
        NodeRange range;
        scene->MakeIterator44(&range, 0, WordOf(&filter)); // receiver: the scene (a cICompositionTreeNode face)
        while (!range.AtEnd()) {
            auto *const node = reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(range.mCursor->Get()); // receiver: cISceneNodeIterator
            if (node && OwnerOf(node)) PortalLightsOfNode(node, out); // receiver: the node (slot 120: the portal kind object)
            range.mCursor->Next(); // receiver: cISceneNodeIterator
        }
    }
    filter.Destruct();
    if (scene) scene->Release();
}

void cDiffusionManager::PortalLightsOfScenePublicLots(std::vector<cPortalLight> *out)
{
    out->resize(0);
    // The scene (counted while it is used)
    nGZSceneGraph::cICompositionTreeNode *const scene = TheScene();
    if (scene) scene->AddRef();
    nRZSceneGraph::cBranchNodesIIDFilter filter{GameAddress<const void>(nRZSceneGraph::cBranchNodesIIDFilter::kVTable), kPortalIid};
    {
        NodeRange range;
        scene->MakeIterator44(&range, 0, WordOf(&filter)); // receiver: the scene (a cICompositionTreeNode face)
        while (!range.AtEnd()) {
            auto *const node = reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(range.mCursor->Get()); // receiver: cISceneNodeIterator
            // The kind object is asked for three times (as the original does): non-null, its slot 99, the lot test.
            if (node && OwnerOf(node)) { // receiver: the node (slot 120: the portal kind object)
                void *const second = OwnerOf(node); // receiver: the node
                if (!RawSlot<99, bool>(second)) { // receiver: the portal kind object (class not identified)
                    if (!LotValueBelow100(OwnerOf(node))) PortalLightsOfNode(node, out); // receiver: the node
                }
            }
            range.mCursor->Next(); // receiver: cISceneNodeIterator
        }
    }
    filter.Destruct();
    if (scene) scene->Release();
}

// Slot 10: the lights of one portal node's own children (those whose portal face is this node) into `out`, as many as
// slot 11 accepts; the count added comes back. Nothing is added when the node's object stands outside the world.
uint32_t cDiffusionManager::PortalLightsOfNode(nGZSceneGraph::cICompositionTreeNode *node, std::vector<cPortalLight> *out)
{
    // The node's object (slot 120, a cTSObject): slot 42 its position (ret 4, a vector through its one argument; the third
    // element is the level as integer bits), slot 101 its tile as three signed bytes (ret 4, through its one argument).
    void *const object = OwnerOf(node); // receiver: the node (slot 120: its owner)
    float position[3];
    const float *const at = static_cast<const float *>(As<ifaces::cTSObject>(object)->GetDefinitionKey(position)); // slot 42: position
    nTSWorld::WorldPoint point;
    std::memcpy(&point.level, at + 2, 4);
    int8_t tile[3];
    float ground[3];
    worldmath::TileBytesToPosition(ground, reinterpret_cast<const int8_t *>(As<ifaces::cTSObject>(object)->GetTileBytes(reinterpret_cast<uint8_t *>(tile)))); // slot 101: the tile
    point.x = ground[0];
    point.y = ground[1];
    // receiver: nTWorld::cPartitionManager (slot 80 0x818640, ret 0xc). The key it answers is not used (the original
    // passes it through 0x3a97d0, SkySampleKey, and drops the result).
    Partitions(mPartitions)->GetLevelAt(&point, 0u, 0u);
    if (!static_cast<nTSWorld::cIWorldDB *>(mWorld.mObject)->IsInWorldPoint(&point)) return 0;

    uint32_t count = 0;
    nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter filter{
        GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable), 0xf, nGZSceneGraph::cILightRefNode::kIid};
    {
        NodeRange range;
        node->MakeIterator44(&range, 0, WordOf(&filter)); // receiver: the node (slot 44)
        while (!range.AtEnd()) {
            // receiver: cISceneNodeIterator
            auto *child = reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(range.mCursor->Get());
            if (PortalOf(child) == static_cast<void *>(node)) {
                // 0x1d4ab0: the child as interface nGZSceneGraph::cILightRefNode::kIid, into an empty reference (the child asked a second time)
                child = reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(range.mCursor->Get());
                void *ref = nullptr;
                child->QueryInterface(nGZSceneGraph::cILightRefNode::kIid, &ref);
                cPortalLight light;
                light.Construct();
                if (MakePortalLight(node, static_cast<nGZSceneGraph::cILightRefNode *>(ref), &light)) {
                    out->push_back(light);
                    ++count;
                }
                if (ref) RawSlot<2, void>(ref); // receiver: the queried object (Release)
            }
            range.mCursor->Next(); // receiver: cISceneNodeIterator
        }
    }
    filter.Destruct();
    return count;
}

// Slot 11: fills `light` (constructed by the caller) for the light reference node `ref` under the portal node `node`.
// False when the extension leaves the colour within 32 ulps of the preset on every axis, or the light's cell is outside
// the world; else the light is placed, faced and its box turned, and true.
bool cDiffusionManager::MakePortalLight(nGZSceneGraph::cICompositionTreeNode *node, nGZSceneGraph::cILightRefNode *ref,
                               cPortalLight *light)
{
    OwnerOf(node); // receiver: the node's portal face (slot 120: its owner; not used)
    const float *const preset = GameAddress<const float>(kPresetColor);
    std::memcpy(light->mColor, preset, 12);
    light->FillFromExtension(ref);
    if (nGZCompGeom::NearlyEqual(light->mColor[0], preset[0]) && nGZCompGeom::NearlyEqual(light->mColor[1], preset[1]) &&
        nGZCompGeom::NearlyEqual(light->mColor[2], preset[2]))
        return false;

    // receiver: nGZSceneGraph::cILightRefNode (slot 54 shared_vf_a4bca0: its world transform, 0x1c bytes)
    const nGZSceneGraph::cRigidTransform placed = ref->GetWorldTransform();
    float at[3];
    std::memcpy(at, placed.Translation(), 12);
    int32_t ground[3];
    const int32_t level = worldheights::WithLevelOfGround(ground, at)[2];
    light->mPosition[0] = at[0];
    light->mPosition[1] = at[1];
    light->mCell[0] = static_cast<int32_t>(at[0]);
    light->mCell[1] = static_cast<int32_t>(at[1]);
    light->mLevel = static_cast<uint32_t>(level);
    light->mLevelAgain = static_cast<uint32_t>(level);
    // receiver: nTSWorld::cIWorldDB (slot 71: IsInside); the light's cell, level and the extension's flag as a tile
    if (!static_cast<nTSWorld::cIWorldDB *>(mWorld.mObject)->IsInside(reinterpret_cast<const nTSWorld::WorldTile *>(light->mCell)))
        return false;

    // The facing: the game's down vector turned by the light's rotation (negated by sign flips, no quieting); when it
    // stands within 0.99 of vertical (in double; a NaN does not), the game's y axis turned by the node's rotation.
    const float *const down = GameAddress<const float>(kZAxis);
    float facing[3] = {-down[0], -down[1], -down[2]};
    reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(&placed)->RotateVector(facing);
    const double z = static_cast<double>(facing[2]);
    if (z < -0.99 || z > 0.99) {
        // receiver: the node's portal face (slot 54 0x25cce5 -> 0x25cd00: its world transform, 0x1c bytes)
        const nGZSceneGraph::cRigidTransform own = node->GetWorldTransform();
        float axis[3];
        std::memcpy(axis, GameAddress<const float>(kYAxis), 12);
        reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(&own)->RotateVector(axis);
        std::memcpy(facing, axis, 12);
    }
    light->mDirection[0] = facing[0];
    light->mDirection[1] = facing[1];
    // x87 (the CRT's atan2 and fmod, 0x831270 and 0x82fd60): the angle rounded to float, then taken modulo 2 pi.
    const float angle = static_cast<float>(std::atan2(static_cast<double>(facing[1]), static_cast<double>(facing[0])));
    light->mAngle = static_cast<float>(std::fmod(static_cast<double>(angle), kTwoPi));

    // The point the light's box reaches along the facing (min y of the box, maxss with 0, over the facing's squared
    // length), its level asked of the ground and the partitions.
    const float x = facing[0];
    const float y = facing[1];
    const float reach = light->mSourceMin[1] > 0.0f ? light->mSourceMin[1] : 0.0f; // maxss (minY, 0): NaN and -0 give 0
    const float s = DivS(reach, AddS(MulS(y, y), MulS(x, x)));
    const float point[3] = {AddS(MulS(x, s), at[0]), AddS(MulS(y, s), at[1]), AddS(MulS(s, 0.0f), at[2])};
    int32_t reached[3];
    worldheights::WithLevelOfGround(reached, point);
    // receiver: nTSWorld::cPartitionManager (slot 80 0x818640, ret 0xc: the level at a position)
    light->mReachedLevel = static_cast<int16_t>(Partitions(mPartitions)->GetLevelAt(reached, 0u, 0u));
    light->PlaceCorners();
    return true;
}

} // namespace nTSSG
