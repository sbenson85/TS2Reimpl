// nTSSG::cLightingManager, the rooms' portals (lane 261109r, scenegraph LG4g): the walls between two rooms with the
// room value 100 searched for portal objects, and each light-reference node of a portal turned into a portal record of
// the sky sample of the room it faces (BuildRoomPortals, AddRoomPortal), plus the records' and their lights'
// constructors. See sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "misc/SmallVirtuals.h"
#include "misc/X87.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGLightInterfaces.h"
#include "sg/cIDiffusionLightExtension.h"
#include "sg/cLightRefNode.h"
#include "sg/cSceneGraphSystem.h"
#include "sg/nTSSGLights.h"
#include "world/WorldHeights.h"
#include "world/WorldTileMath.h"
#include "world/WorldTileRects.h"
#include "world/cWorldDB.h"

#include <cstring>
#include <vector>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::BitsOf;
using nGZCompGeom::convex::FloatOf;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;
using nGZCompGeom::convex::X87Pick;
using nGZSceneGraph::cILightRefNode;
using nGZSceneGraph::cIStandardLight;

constexpr uint32_t kPortalLightColor = 0x100e4fc; // a cPortalLight's colour at construction

constexpr uint32_t kRoomValue = 100;           // the room value (slot 36) both sides of a portal wall need
constexpr uint32_t kPortalIid = 0xfa1c39ed;    // the wall-side object's slot 9 asks for the portal by these two
constexpr uint32_t kPortalKind = 0xfa1c39f7;
constexpr uint32_t kDroppedTag = 0x3ffffffe;   // a node that makes no record gets this layer tag

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// 0x19d250 (still the game's): the 2D dot product b.y * a.y + b.x * a.x on the x87, at its precision.
double Dot2(const float *a, const float *b)
{
    return OriginalFunction<double(__cdecl *)(const float *, const float *), 0x19d250>()(a, b);
}

// 0x7e37b0 (still the game's): the object of a wall side (kind 8, else 9), not counted.
void *WallSideObject(const float *side)
{
    return OriginalFunction<void *(__cdecl *)(const float *), 0x7e37b0>()(side);
}

// 0x7f2340 (still the game's): the room on a wall side.
uint16_t SideRoom(const float *side)
{
    return OriginalFunction<uint16_t(__cdecl *)(const float *, int32_t), 0x7f2340>()(side, 0);
}

// A node into a record's reference: counted (the original does not test it for null), the old one released.
void HoldNode(cBuiltRef &ref, cILightRefNode *node)
{
    void *const old = ref.mObject;
    if (node == old) return;
    node->AddRef(); // receiver: nGZSceneGraph::cILightRefNode
    ref.mObject = node;
    if (old) RawSlot<2, void>(old); // receiver: the record's old node (cIGZUnknown Release)
}

// A light node into a record's reference, counted when there is one.
void HoldLight(cBuiltRef &ref, cILightRefNode *node)
{
    void *const old = ref.mObject;
    if (node == old) return;
    if (node) node->AddRef(); // receiver: nGZSceneGraph::cILightRefNode
    ref.mObject = node;
    if (old) RawSlot<2, void>(old); // receiver: the record's old light node (cIGZUnknown Release)
}

nGZCompGeom::RigidTransform3 *Rigid(nGZSceneGraph::cRigidTransform *transform)
{
    return reinterpret_cast<nGZCompGeom::RigidTransform3 *>(transform);
}

} // namespace

cPortalLight *cPortalLight::Construct()
{
    Copy3(mColor, GameAddress<const float>(kPortalLightColor));
    mReachedLevel = 0;
    mPosition[0] = 0.0f;
    mPosition[1] = 0.0f;
    mLevel = 0;
    mCell[0] = 0;
    mCell[1] = 0;
    mLevelAgain = 0;
    mDirectional = 0;
    mDirection[0] = 0.0f;
    mDirection[1] = 0.0f;
    mAngle = 0.0f;
    mDirectionalFalloff = 0.0f;
    mFalloff = 1.0f;
    mWallIntensityScale = 1.0f;
    mFloorIntensityScale = 1.0f;
    mInterFloorFalloff = 0.5f;
    mSourceMin[0] = -0.5f;
    mSourceMin[1] = -0.5f;
    mSourceMax[0] = 0.5f;
    mSourceMax[1] = 0.5f;
    mCorners[0] = -0.5f;
    mCorners[1] = -0.5f;
    mCorners[2] = 0.5f;
    mCorners[3] = 0.5f;
    return this;
}

cPortalRecord *cPortalRecord::Construct()
{
    mObject.mObject = nullptr;
    mOther.mObject = nullptr;
    mShade = 1.0f;
    mSample = 0;
    mSegment[0] = -1.0f;
    mSegment[1] = -1.0f;
    std::memset(&mSegment[2], 0, sizeof(float));
    mSegment[3] = -1.0f;
    mSegment[4] = -1.0f;
    std::memset(&mSegment[5], 0, sizeof(float));
    Copy3(mColor, GameAddress<const float>(kZeroVector));
    mScaleDirect = 0;
    mStrength = 0.0f;
    Copy3(mTint, GameAddress<const float>(kGrey));
    mValue = 1.0f;
    mPosition[0] = 0.0f;
    mPosition[1] = 0.0f;
    mPosition[2] = 0.0f;
    mLight.Construct();
    return this;
}

void cLightingManager::BuildRoomPortals()
{
    if (!mWorld) return;
    void *object = nullptr; // the wall side's object, counted
    void *portal = nullptr; // its portal face, counted through its virtual base
    for (uint32_t i = 0; i < Call<55>(mWorld); ++i) { // receiver: mWorld (class not identified): the walls
        cWallSide wall;
        Call<56>(mWorld, &wall, i); // receiver: mWorld: wall i
        const cWallSide reversed = {{wall.v[3], wall.v[4], wall.v[5], wall.v[0], wall.v[1], wall.v[2]}};
        worldmath::TileQuadrant front{};
        worldmath::TileQuadrant back{};
        worldrect::SegmentToQuadrants(&front, wall.v);
        worldrect::SegmentToQuadrants(&back, reversed.v);
        const uint16_t frontRoom = static_cast<uint16_t>(Call<79>(mWorld, &front, 0u, 1u)); // receiver: mWorld
        const uint16_t backRoom = static_cast<uint16_t>(Call<79>(mWorld, &back, 0u, 1u));   // receiver: mWorld
        // receiver: nTSSG::cLightingManager (vf36 LotValue 0x2d9480)
        if (static_cast<uint16_t>(RawSlot<36, uint32_t>(this, static_cast<uint32_t>(frontRoom))) != kRoomValue) continue;
        // receiver: nTSSG::cLightingManager (vf36 LotValue 0x2d9480)
        if (static_cast<uint16_t>(RawSlot<36, uint32_t>(this, static_cast<uint32_t>(backRoom))) != kRoomValue) continue;

        // The wall's own object first, then the reversed side's; the portal comes from whichever answers.
        const cWallSide *const sides[2] = {&wall, &reversed};
        for (int s = 0; s < 2; ++s) {
            void *const found = WallSideObject(sides[s]->v);
            if (found != object) {
                void *const old = object;
                if (found) RawSlot<1, void>(found); // receiver: the wall-side object (cIGZUnknown AddRef)
                object = found;
                if (old) RawSlot<2, void>(old); // receiver: the wall-side object (cIGZUnknown Release)
            }
            if (!object) continue;
            DropHeld(portal);
            // receiver: the wall-side object (class not identified): the portal face by kind and interface
            RawSlot<9, void>(object, kPortalKind, kPortalIid, &portal);
            if (!portal) continue;
            AddRoomPortal(portal, *sides[s], *sides[1 - s]);
            break;
        }
    }
    if (portal) ReleaseThroughBase(portal);
    if (object) RawSlot<2, void>(object); // receiver: the wall-side object (cIGZUnknown Release)
}

void cLightingManager::AddRoomPortal(void *portal, cWallSide side, cWallSide other)
{
    if (!mWorld) return;
    const float *const a = side.v;
    float normal[2];
    float otherNormal[2];
    EdgeNormal(normal, side.v);
    EdgeNormal(otherNormal, other.v);
    // The side's middle: moved with the side when a node's projection falls outside it, and kept for the next node.
    float middleX = MulS(AddS(a[3], a[0]), 0.5f);
    float middleY = MulS(AddS(a[4], a[1]), 0.5f);
    const int32_t lowest = static_cast<int32_t>(Call<88>(mTerrain));  // receiver: mTerrain (class not identified)
    const int32_t highest = static_cast<int32_t>(Call<89>(mTerrain)); // receiver: mTerrain

    nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter filter{
        GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable), 0xf, cILightRefNode::kIid};
    {
        NodeRange range;
        // receiver: the portal (a cICompositionTreeNode face; class not identified)
        reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(portal)->MakeIterator44(&range, 0, Word(&filter));
        while (!range.AtEnd()) {
            // receiver: cISceneNodeIterator
            auto *const node = reinterpret_cast<cILightRefNode *>(range.mCursor->Get());
            bool made = false;
            [&] {
                nGZSceneGraph::cRigidTransform placed = node->GetWorldTransform(); // receiver: cILightRefNode
                const float position[3] = {placed.mValues[0], placed.mValues[1], placed.mValues[2]};
                const float *const up = GameAddress<const float>(kUpNormal);
                float down[3] = {Negated(up[0]), Negated(up[1]), Negated(up[2])};
                placed = node->GetWorldTransform(); // receiver: cILightRefNode
                Rigid(&placed)->RotateVector(down);
                int32_t ground[3];
                worldheights::WithLevelOfGround(ground, position);
                // receiver: mTerrain (class not identified): the point is on the terrain
                if (!Call<72, bool>(mTerrain, position[0], position[1])) return;
                const nTSWorld::WorldPoint low{position[0], position[1], lowest};
                if (static_cast<float>(worldheights::GroundHeightAt(&low)) > position[2]) return;
                const nTSWorld::WorldPoint high{position[0], position[1], highest};
                worldheights::GroundHeightAt(&high);

                const float along[2] = {SubS(a[3], a[0]), SubS(a[4], a[1])};
                const float offset[2] = {SubS(position[0], a[0]), SubS(position[1], a[1])};
                nTSWorld::WorldPoint start{a[0], a[1], 0};
                std::memcpy(&start.level, &a[2], sizeof(int32_t));
                const float length2 = static_cast<float>(Dot2(along, along));
                if (1e-6 > static_cast<double>(length2)) return;
                const double dot = Dot2(along, offset);
                const double divisor = static_cast<double>(length2);
                const float t = static_cast<float>(
                    x87::Rounded((dot != dot && divisor != divisor) ? X87Pick(dot, divisor) : dot / divisor));
                if (0.0f > t || t >= 1.0f) {
                    // Whole lengths to move the side by: t rounded to the nearest integer, halves down unless t is
                    // that integer.
                    const float biased = AddS(SubS(t, 0.5f), 12582912.0f);
                    uint32_t steps = BitsOf(biased) + 0xb4c00000u;
                    if (static_cast<float>(static_cast<int32_t>(steps + 1)) == t) steps += 1;
                    const float r = static_cast<float>(static_cast<int32_t>(steps));
                    middleX = AddS(MulS(r, along[0]), middleX);
                    middleY = AddS(MulS(r, along[1]), middleY);
                    start.y = AddS(MulS(r, along[1]), a[1]);
                    start.x = AddS(MulS(r, along[0]), a[0]);
                }

                // The first level up whose ground is not below the node, and the first down whose ground is.
                const int32_t level = start.level;
                start.level = level + 1;
                float above = static_cast<float>(worldheights::GroundHeightAt(&start));
                if (0.0f > position[2]) return;
                while (position[2] > above && above > 0.0f) {
                    ++start.level;
                    above = static_cast<float>(worldheights::GroundHeightAt(&start));
                }
                if (0.0f >= above) return;
                --start.level;
                float below = static_cast<float>(worldheights::GroundHeightAt(&start));
                while (below > position[2]) {
                    --start.level;
                    below = static_cast<float>(worldheights::GroundHeightAt(&start));
                }

                const float endX = AddS(along[0], start.x);
                const float endY = AddS(along[1], start.y);
                float forward[6] = {start.x, start.y, 0.0f, endX, endY, 0.0f};
                float backward[6] = {endX, endY, 0.0f, start.x, start.y, 0.0f};
                std::memcpy(&forward[2], &start.level, 4);
                std::memcpy(&forward[5], &start.level, 4);
                std::memcpy(&backward[2], &start.level, 4);
                std::memcpy(&backward[5], &start.level, 4);
                if (!Call<44, bool>(mWorld, forward)) return; // receiver: mWorld (class not identified)
                const uint16_t forwardRoom = SideRoom(forward);
                const uint16_t backwardRoom = SideRoom(backward);

                cPortalRecord record;
                record.Construct();
                const float facing = AddS(MulS(normal[1], down[1]), MulS(normal[0], down[0]));
                uint16_t sample;
                uint16_t tag;
                const float *chosen;
                if (facing > 0.0f) {
                    std::memcpy(record.mSegment, backward, sizeof(backward));
                    sample = backwardRoom;
                    tag = forwardRoom;
                    chosen = normal;
                } else if (0.0f > facing) {
                    std::memcpy(record.mSegment, forward, sizeof(forward));
                    sample = forwardRoom;
                    tag = backwardRoom;
                    chosen = otherNormal;
                } else {
                    return;
                }
                const float nx = chosen[0];
                const float ny = chosen[1];
                HoldNode(record.mObject, node);

                const bool lerp = Switch(kRefreshSwitch);
                if (!lerp) {
                    node->SetLayerTag(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(tag))));
                    node->SetLightFlag(0);
                } else if (node->GetLight()) {
                    ScalarEvent one{GameAddress<const void>(kScalarEventVTable), 0.0f};
                    VectorEvent color{GameAddress<const void>(kVectorEventVTable), {0.0f, 0.0f, 0.0f}};
                    ScalarEvent value{GameAddress<const void>(kScalarEventVTable), 0.0f};
                    GetAttribute(node, kAttrOne, &one);
                    GetAttribute(node, kAttrColor, &color);
                    GetAttribute(node, kAttrValue, &value);
                    node->DropLightCopy();
                    ScalarEvent oneCopy{GameAddress<const void>(kScalarEventVTable), 0.0f};
                    VectorEvent colorCopy{GameAddress<const void>(kVectorEventVTable), {0.0f, 0.0f, 0.0f}};
                    ScalarEvent valueCopy{GameAddress<const void>(kScalarEventVTable), 0.0f};
                    GetAttribute(node, kAttrOne, &oneCopy);
                    GetAttribute(node, kAttrColor, &colorCopy);
                    GetAttribute(node, kAttrValue, &valueCopy);
                    record.mStrength = oneCopy.mValue;
                    Copy3(record.mTint, colorCopy.mValue);
                    record.mValue = valueCopy.mValue;
                    // The light's own values back on the node, now that it has its copy dropped.
                    SetAttribute(node, kAttrOne, &one);
                    SetAttribute(node, kAttrColor, &color);
                    SetAttribute(node, kAttrValue, &value);
                }

                // The light: mPortalLightOffset off the middle along the normal, halfway between the two grounds.
                const float scale = mPortalLightOffset;
                float direction[3] = {nx, ny, 0.0f};
                float place[3];
                place[0] = AddS(MulS(scale, nx), middleX);
                place[1] = AddS(MulS(scale, ny), middleY);
                place[2] = AddS(static_cast<float>(static_cast<double>(AddS(below, above)) * 0.5),
                                MulS(scale, 0.0f));
                if (lerp) {
                    void *extension = nullptr;
                    // receiver: cILightRefNode
                    if (node->FindExtension(cIDiffusionLightExtension::kIid, &extension, nullptr))
                        static_cast<cIDiffusionLightExtension *>(extension)->ResetFlags();
                    // receiver: mLitObject (class not identified): the record's cPortalLight for the portal and node
                    RawSlot<11, void>(mLitObject, portal, node, &record.mLight);
                    Copy3(record.mPosition, place);
                    if (extension) ReleaseThroughBase(extension);
                } else {
                    cILightRefNode *spill = nullptr;
                    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
                    com->GetClassObject(kLightRefNodeClass, cILightRefNode::kIid, reinterpret_cast<void **>(&spill));
                    nGZSceneGraph::cRigidTransform inverse = node->GetWorldTransform(); // receiver: cILightRefNode
                    Rigid(&inverse)->Invert();
                    Rigid(&inverse)->TransformPoint(reinterpret_cast<nGZCompGeom::Vec3 *>(place));
                    Rigid(&inverse)->RotateVector(direction);
                    void *const memory = ::operator new(0xa0);
                    uint8_t *light = nullptr;
                    if (memory) {
                        using Constructor = uint8_t *(__thiscall *)(void *, const float *, const float *, uint32_t);
                        light = reinterpret_cast<Constructor>(const_cast<uint8_t *>(&cHalfspacePointLight_Constructor))(
                            memory, direction, place, 1u);
                    }
                    auto *const face = reinterpret_cast<cIStandardLight *>(light + 0x2c);
                    {
                        cRZString name;
                        name.Construct();
                        cRZString *const text = name.Sprintf("LM_PortalSpill");
                        // ORIGINAL-BUG: 0x2d5e9f / 0x2d5ea2 name the light without testing it: with no memory for it
                        // the name goes to address 0x2c.
                        face->SetLightName(text->ToChar());
                        name.Destruct();
                    }
                    spill->SetLight(light ? face : nullptr);
                    spill->SetLayerTag(SampleKey(static_cast<int32_t>(tag)));
                    spill->SetLightFlag(0);
                    void *extension = nullptr;
                    // receiver: cILightRefNode
                    if (node->FindExtension(cIDiffusionLightExtension::kIid, &extension, nullptr)) {
                        auto *const base = static_cast<nGZSceneGraph::cIExtension *>(
                            static_cast<cIDiffusionLightExtension *>(extension));
                        spill->AddExtension(base->Clone()); // receiver: cIExtension, then cILightRefNode
                    } else {
                        RawSlot<124, void>(portal); // receiver: the portal (class not identified)
                    }
                    HoldLight(record.mOther, spill);
                    node->AddChild(spill); // receiver: cILightRefNode
                    if (extension) ReleaseThroughBase(extension);
                    if (spill) spill->Release(); // receiver: cILightRefNode
                }

                record.mSample = static_cast<int16_t>(tag);
                record.mNormal[0] = nx;
                record.mNormal[1] = ny;
                // receiver: the portal (class not identified), then what its slot 120 gives
                record.mScaleDirect = RawSlot<98, uint32_t>(RawSlot<120, void *>(portal)) == 8;
                const int32_t index = static_cast<int16_t>(sample);
                // ORIGINAL-BUG: 0x2d6064 / 0x2d6072 / 0x2d6076 take a room of 0x8000 and up as a negative index: the
                // samples are resized to index + 1 (none for -1) and the record goes before the first.
                if (!(SkySampleCount() > static_cast<uint32_t>(index)))
                    mSkySamples.resize(static_cast<uint32_t>(index) + 1);
                mSkySamples.data()[index].mPortals.push_back(record);
                made = true;
            }();
            if (!made) {
                node->SetLayerTag(kDroppedTag);
                ScalarEvent off{GameAddress<const void>(kScalarEventVTable), 0.0f};
                SetAttribute(node, kAttrOne, &off);
            }
            range.mCursor->Next(); // receiver: cISceneNodeIterator
        }
    }
    filter.Destruct();
}

} // namespace nTSSG
