// nTSSG::cNHoodOccupantManager (lane GO): occupants coming and going, node flags and colours, the fade and the per-frame update.
// Split from one file along these concerns; the shared helpers are in nTSSGNHoodOccupantManagerInternal.h.
#include "sg/DetailObject.h"
#include "std/StdInstances.h"
#include "ts/nTSSGNHoodOccupantManager.h"
#include "ts/nTSSGNHoodOccupantManagerInternal.h"
#include "world/WorldTileMath.h"

namespace nTSSG {

using namespace occupant_detail;

namespace {

constexpr unsigned kSceneViewSlot = 18;     // scene: the object slot 11 works on (counted by the caller)

// Scene nodes (the framework's group / shape nodes).
constexpr unsigned kNodeEventSlot = 7;        // (event id, arg, event)
constexpr unsigned kNextSiblingSlot = 38;     // on a child: the next one (the range's end after the last)

constexpr unsigned kNodeHasFeatureSlot = 48;  // (4) -> bool

constexpr unsigned kSetFlagsSlot = 78;        // (flags)
constexpr unsigned kClearFlagsSlot = 80;      // (flags)
constexpr uint32_t kNodeFeature = 4;
constexpr uint32_t kColorEventId = 0x8c798726;
constexpr uint32_t kColorEventVTable = 0xdb07d4; // nGZEvent::cVectorEvent<cVector<4, float>, ...>
constexpr uint32_t kEventHeaderVTable = 0xd9ca58; // nGZEvent::cIEventHeader (what the event's destructor 0x154ba0 leaves)

// The occupant's placement (occupant slot 3): slot 6 its position (three floats), slot 8 its footprint (x0, y0, x1, y1).
constexpr unsigned kOccupantPlacementSlot = 3;

constexpr unsigned kFootprintRectSlot = 8;
constexpr int32_t kLayerCellSize = 640;

// The colour event on the stack: the game's event vtable and the four floats. The destructor only puts the base's vtable
// back (0x154ba0), which the original does on unwinding alone.
struct ColorEvent {
    const void *mVTable;
    float mValue[4];
    explicit ColorEvent(const float *rgba) : mVTable(GameAddress(kColorEventVTable))
    {
        for (int i = 0; i < 4; ++i) mValue[i] = rgba[i];
    }
    ColorEvent(const ColorEvent &) = delete;
    ColorEvent &operator=(const ColorEvent &) = delete;
    ~ColorEvent() { mVTable = GameAddress(kEventHeaderVTable); }
};

// An occupant's faces (QI), by the node builder that takes each.
constexpr uint32_t kFloraFaceIid = 0x4bd0e45b;     // AddFloraOccupant 0x2a5ef0; its position gives the layer
constexpr uint32_t kOccupantFaceBIid = 0xabd4c3f4; // 0x2a69b0
constexpr uint32_t kOccupantFaceCIid = 0x6bd4d6ce; // 0x2a7130
constexpr uint32_t kOtherFaceIid = 0x2c0c10c8;     // AddOtherOccupant 0x2a63a0
constexpr unsigned kOccupantSetNodeSlot = 10;      // (node); RemoveOccupant clears it
constexpr unsigned kNodeObjectSlot = 24;           // node: (id, out, 0), the out counted through its virtual base
constexpr uint32_t kNodeObjectId = 0x0bb263d5;
constexpr unsigned kNodeObjectDetachSlot = 8;      // that object: (0)

constexpr unsigned kMergeListSlot = 6;             // a layer's merge node: (occupant, list) -> the occupant's props entries

// The original's round-down of a float (x - 0.5 + 1.5 * 2^23, the mantissa read back; one up where that lands one short).
int32_t RoundDown(float value)
{
    const float biased = (value - 0.5f) + 12582912.0f;
    const int32_t result = std::bit_cast<int32_t>(biased) - 0x4b400000;
    return static_cast<float>(result + 1) == value ? result + 1 : result;
}

// The camera service, cached in game global 0x10cc45c.
constexpr uint32_t kCameraCache = 0x10cc45c;
constexpr uint32_t kCameraIdA = 0x4a037ee3;
constexpr uint32_t kCameraIdB = 0x49ff7d6f;
constexpr unsigned kCameraContainsSlot = 69; // (point from 0x7ddd30) -> inside the bounds
constexpr unsigned kCameraMinXSlot = 75;
constexpr unsigned kCameraMinYSlot = 76;
constexpr unsigned kCameraMaxXSlot = 77;
constexpr unsigned kCameraMaxYSlot = 78;
constexpr unsigned kViewPositionSlot = 140;  // (out vec3, 0)
constexpr unsigned kShapeBoxSlot = 72;       // (out box: min xyz, max xyz)
constexpr unsigned kDetailLevelSlot = 89;
constexpr unsigned kDetailLimitSlot = 43;    // 0xaead80's object
constexpr uint32_t kDetailIid = 0x1c585e30;

struct Vec3 {
    float x, y, z;
};
struct Box {
    Vec3 min, max;
};
// 0x7ddd30's result: the position's x and y with the terrain height there (which the caller then drops).
struct GroundPoint {
    float x, y, z;
};

// The camera's bounds slots return through x87; the original stores each as a float.
float CameraBound(void *camera, unsigned slot)
{
    switch (slot) {
    case kCameraMinXSlot: return VirtualCall<float(__thiscall *)(void *), kCameraMinXSlot>(camera);
    case kCameraMinYSlot: return VirtualCall<float(__thiscall *)(void *), kCameraMinYSlot>(camera);
    case kCameraMaxXSlot: return VirtualCall<float(__thiscall *)(void *), kCameraMaxXSlot>(camera);
    default: return VirtualCall<float(__thiscall *)(void *), kCameraMaxYSlot>(camera);
    }
}

// The inlined float compare of the original: `value` within 32 units in the last place of 1.0 (the bits mapped to a
// monotonic integer, the difference's high bits all equal).
bool NearlyOne(float value)
{
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = bits >> 31;
    const int32_t ordered = int32_t((((0u - sign) >> 1) ^ bits) + 0xc0800000u + sign);
    return ((ordered >> 26 ^ ordered) & 0xffffffe0u) == 0;
}

} // namespace

// 0x2a5560 (cdecl, recursive): on the node, where its slot 48 answers for 4, the flags `clear` cleared (slot 80) and `set`
// set (slot 78); then the same on each child (slot 42 gives the range, a child's slot 38 the next).
void __cdecl cNHoodOccupantManager::ChangeNodeFlags(void *node, uint32_t set, uint32_t clear)
{
    if (VirtualCall<bool(__thiscall *)(void *, uint32_t), kNodeHasFeatureSlot>(node, kNodeFeature)) {
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kClearFlagsSlot>(node, clear);
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kSetFlagsSlot>(node, set);
    }
    NodeChildren children;
    VirtualCall<NodeChildren *(__thiscall *)(void *, NodeChildren *), kChildrenSlot>(node, &children);
    for (void *child = children.mFirst; child != children.mEnd;
         child = VirtualCall<void *(__thiscall *)(void *), kNextSiblingSlot>(child))
        ChangeNodeFlags(child, set, clear);
}

// 0x2a55e0 (cdecl, recursive, EH): on the node, where its slot 48 answers for 4, the colour event (a
// nGZEvent::cVectorEvent<cVector<4, float>> on the stack, vtable 0xdb07d4) sent through slot 7 with id 0x8c798726 and `arg`;
// then the same on each child.
void __cdecl cNHoodOccupantManager::SetNodeColor(void *node, uint32_t arg, const float *rgba)
{
    if (VirtualCall<bool(__thiscall *)(void *, uint32_t), kNodeHasFeatureSlot>(node, kNodeFeature)) {
        ColorEvent event(rgba);
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, ColorEvent *), kNodeEventSlot>(node, kColorEventId, arg,
            &event);
    }
    NodeChildren children;
    VirtualCall<NodeChildren *(__thiscall *)(void *, NodeChildren *), kChildrenSlot>(node, &children);
    for (void *child = children.mFirst; child != children.mEnd;
         child = VirtualCall<void *(__thiscall *)(void *), kNextSiblingSlot>(child))
        SetNodeColor(child, arg, rgba);
}

// 0x2a7f20 (cdecl): -1 for no occupant; else its position (slot 3's object, slot 6) rounded down per axis (RoundDown), each
// divided by 640 (truncating), qx + 2 qy capped at 3.
int32_t __cdecl cNHoodOccupantManager::LayerOf(void *occupant)
{
    if (!occupant) return -1;
    void *placement = reinterpret_cast<void *>(uintptr_t(VirtualCall<Get0, kOccupantPlacementSlot>(occupant)));
    float position[3]; // the original's 12-byte frame (sub esp, 0xc)
    VirtualCall<uint32_t(__thiscall *)(void *, float *), kPlacementPositionSlot>(placement, position);
    const int32_t qx = RoundDown(position[0]) / kLayerCellSize;
    const int32_t qy = RoundDown(position[1]) / kLayerCellSize;
    const int32_t layer = qx + qy * 2;
    return layer > 3 ? 3 : layer;
}

// 0x2a8570 (cdecl): the footprint (slot 8 fills x0, y0, x1, y1) and the rectangle overlap and, with `whole`, the footprint
// lies wholly inside (its intersection with the rectangle is the footprint). The result is al only.
bool __cdecl cNHoodOccupantManager::FootprintIn(void *footprint, const int32_t *rect, uint8_t whole)
{
    int32_t box[4];
    VirtualCall<uint32_t(__thiscall *)(void *, int32_t *), kFootprintRectSlot>(footprint, box);
    if (box[2] >= rect[0] && box[0] <= rect[2] && box[3] >= rect[1] && box[1] <= rect[3]) {
        const int32_t x0 = box[0] > rect[0] ? box[0] : rect[0];
        const int32_t y0 = box[1] > rect[1] ? box[1] : rect[1];
        const int32_t x1 = box[2] < rect[2] ? box[2] : rect[2];
        const int32_t y1 = box[3] < rect[3] ? box[3] : rect[3];
        if (x0 == box[0] && y0 == box[1] && x1 == box[2] && y1 == box[3]) return true;
    }
    if (whole) return false;
    return box[2] >= rect[0] && box[0] <= rect[2] && box[3] >= rect[1] && box[1] <= rect[3];
}

// 0x2a7500 (ret 4, EH): an occupant without a node built by its kind: the flora face (QI 0x4bd0e45b, AddFloraOccupant
// 0x2a5ef0), else face 0xabd4c3f4 (0x2a69b0), else face 0x6bd4d6ce (0x2a7130), else face 0x2c0c10c8 (AddOtherOccupant
// 0x2a63a0). The four faces are released in the original's order (others, flora, b, c).
void cNHoodOccupantManager::AddOccupant(cIGZUnknown *occupant)
{
    if (OccupantNode(occupant)) return;
    SGRef faceC, faceB, flora, other; // destroyed (released) as other, flora, faceB, faceC
    if (VirtualCall<Query, 0>(occupant, kFloraFaceIid, &flora.mObject) & 0xff) {
        AddFloraOccupant(flora.mObject);
        return;
    }
    faceB.Reset();
    if (VirtualCall<Query, 0>(occupant, kOccupantFaceBIid, &faceB.mObject) & 0xff) {
        AddRoadOccupant(faceB.mObject);
        return;
    }
    faceC.Reset();
    if (VirtualCall<Query, 0>(occupant, kOccupantFaceCIid, &faceC.mObject) & 0xff) {
        AddFaceCOccupant(faceC.mObject);
        return;
    }
    other.Reset();
    if (VirtualCall<Query, 0>(occupant, kOtherFaceIid, &other.mObject) & 0xff) AddOtherOccupant(other.mObject);
}

// 0x2a8e70 (ret 4, EH): an occupant's node taken down: the node's object 0xbb263d5 (slot 24; a class with a virtual base,
// counted through it) gets slot 8(0); the node leaves the layer node of the occupant's type (slot 4); the occupant's node is
// cleared (slot 10(0)); with layer 0 built, the layer of the occupant's flora face is marked dirty.
void cNHoodOccupantManager::RemoveOccupant(cIGZUnknown *occupant)
{
    void *node = OccupantNode(occupant);
    if (!node) return;
    void *object;
    {
        VirtualBaseRef out; // the out reference, dropped before the held one counts (the original's order)
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, void **, uint32_t), kNodeObjectSlot>(node, kNodeObjectId,
            &out.mObject, 0);
        object = out.mObject;
    }
    VirtualBaseRef held(object);
    if (object) VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kNodeObjectDetachSlot>(object, 0);
    const uint32_t type = VirtualCall<Get0, kOccupantTypeSlot>(occupant);
    VirtualCall<uint32_t(__thiscall *)(void *, void *), kRemoveChildSlot>(mLayerNodes[type].mObject, node);
    VirtualCall<uint32_t(__thiscall *)(void *, void *), kOccupantSetNodeSlot>(occupant, nullptr);
    if (mLayerNodes[0].mObject) {
        SGRef flora;
        // ORIGINAL-BUG: a face answered as null gives layer -1, which marks the byte before the flags (+0xb7).
        if (VirtualCall<Query, 0>(occupant, kFloraFaceIid, &flora.mObject) & 0xff) mLayerDirty[LayerOf(flora.mObject)] = 1;
    }
}

// 0x2a9100 (ret 4, EH; reached through the thunk 0x2a8560): a queued occupant's props handed to its layer's target in the
// merged flora node (slot 93's table): the layer's merge node (slot 6) lists the occupant's entries, each a pair of names
// (slot 34 (second, first)) and the first name under "Props" and "Shade" (slot 36; game text at 0xdb076c / 0xdb34ec).
void cNHoodOccupantManager::HandOnPending(void *occupant)
{
    void *flora = mFloraNode.mObject;
    const int32_t layer = LayerOf(occupant);
    if (layer == -1) return;
    uint32_t scratch[3]; // the original's 12-byte frame slot the call gets
    auto *targets = VirtualCall<void ***(__thiscall *)(void *, uint32_t *), kFloraTargetsSlot>(flora, scratch);
    CountedLocal target(static_cast<cIGZUnknown *>((*targets)[layer]));
    PropsList list;
    if (VirtualCall<bool(__thiscall *)(void *, void *, PropsList *), kMergeListSlot>(mMergeNodes[layer].mObject, occupant,
            &list)) {
        for (uint32_t i = 0; i < uint32_t(list.mLast - list.mFirst); ++i) {
            PropsEntry &entry = list.mFirst[i];
            const char *first = entry.mFirst.mCapacity > 15 ? entry.mFirst.mPtr : entry.mFirst.mBuf;
            const char *second = entry.mSecond.mCapacity > 15 ? entry.mSecond.mPtr : entry.mSecond.mBuf;
            using Names = uint32_t(__thiscall *)(void *, const char *, const char *);
            VirtualCall<Names, kTargetPairSlot>(target.mObject, second, first);
            VirtualCall<Names, kTargetGroupSlot>(target.mObject, GameAddress<const char>(kPropsText), first);
            VirtualCall<Names, kTargetGroupSlot>(target.mObject, GameAddress<const char>(kShadeText), first);
        }
    }
}

// 0x2a86a0. The two merge helpers take the object (this - 8) and are still game code.
void cNHoodOccupantManager::Update()
{
    if (mBuildAll && mFloraPending) {
        ClearLayers1And2();
        RebuildMergedLayers();
        mFloraPending = 0;
    }
    void *system = CachedServiceGetter<kSgSystemCache, kSgSystemIdA, kSgSystemIdB>();
    void *scene = VirtualCall<void *(__thiscall *)(void *), kSceneOfSystemSlot>(system);
    auto *view = VirtualCall<cIGZUnknown *(__thiscall *)(void *), kSceneViewSlot>(scene);
    if (view) view->AddRef();
    UpdateFade(view);
    if (view) view->Release();
}

// 0x2a9610.
void cNHoodOccupantManager::UpdateFade(cIGZUnknown *view)
{
    if (mBuildAll || !mFadeDistanceSq || mNodes.empty() || !view) return;
    void *camera = CachedServiceGetter<kCameraCache, kCameraIdA, kCameraIdB>();
    Vec3 position;
    {
        Vec3 out;
        VirtualCall<uint32_t(__thiscall *)(void *, Vec3 *, uint32_t), kViewPositionSlot>(view, &out, 0);
        position = out;
    }
    GroundPoint ground;
    worldmath::WithTileValue(reinterpret_cast<int32_t *>(&ground), reinterpret_cast<const int32_t *>(&position)); // 0x7ddd30
    ground.z = 0.0f; // the camera asks about the ground plane
    const bool inside = VirtualCall<uint32_t(__thiscall *)(void *, GroundPoint *), kCameraContainsSlot>(camera, &ground) & 0xff;
    // the view's distance (squared) to the bounds; 0 inside
    float boundsDistance = 0.0f;
    if (!inside) {
        const float maxX = CameraBound(camera, kCameraMaxXSlot);
        const float halfWidth = float((double(maxX) - double(CameraBound(camera, kCameraMinXSlot))) * 0.5);
        const float maxY = CameraBound(camera, kCameraMaxYSlot);
        const float halfHeight = float((double(maxY) - double(CameraBound(camera, kCameraMinYSlot))) * 0.5);
        float x = halfWidth;
        if (position.x > CameraBound(camera, kCameraMinXSlot) && CameraBound(camera, kCameraMaxXSlot) > position.x)
            x = position.x;
        float y = halfHeight;
        if (position.y > CameraBound(camera, kCameraMinYSlot) && CameraBound(camera, kCameraMaxYSlot) > position.y)
            y = position.y;
        boundsDistance = (x - position.x) * (x - position.x) + (y - position.y) * (y - position.y) + 0.0f;
    }
    const uint32_t mask = ShapeColorMask();
    void *detail = nGZSceneGraph::GetDetailObject();
    const uint32_t detailLimit = VirtualCall<uint32_t(__thiscall *)(void *), kDetailLimitSlot>(detail) - 1;
    for (SGNodeEntry *entry = mNodes.data(), *end = entry + mNodes.size(); entry != end; ++entry) {
        cIGZUnknown *node = entry->mNode.mObject;
        const uint8_t faded = entry->mFaded;
        if (node) node->AddRef();
        {
            SGRef shape;
            if (!(VirtualCall<Query, 0>(node, kShapeNodeIid, &shape.mObject) & 0xff)) goto next;
            SGRef level;
            const bool hasLevel = VirtualCall<Query, 0>(shape.mObject, kDetailIid, &level.mObject) & 0xff;
            if (!hasLevel || detailLimit <= VirtualCall<Get0, kDetailLevelSlot>(level.mObject)) {
                bool fade = false;
                float alpha = 1.0f;
                Box box;
                if (!inside) {
                    VirtualCall<uint32_t(__thiscall *)(void *, Box *), kShapeBoxSlot>(shape.mObject, &box);
                    const float top = box.max.z;
                    if (0.0f > top) goto next; // below the ground: left as it is
                    float nodeDistance;
                    const Vec3 at = position;
                    if (OriginalFunction<uint32_t(__thiscall *)(const Box *, const Vec3 *), 0x19d340>()(&box, &at) & 0xff) {
                        nodeDistance = 0.0f;
                    } else {
                        float cy = (box.max.y - box.min.y) * 0.5f + box.min.y;
                        float cx = (box.max.x - box.min.x) * 0.5f + box.min.x;
                        if (position.x > box.min.x && box.max.x > position.x) cx = position.x;
                        if (position.y > box.min.y && box.max.y > position.y) cy = position.y;
                        nodeDistance = (cy - position.y) * (cy - position.y) + (cx - position.x) * (cx - position.x) + 0.0f;
                        if (20.0f > top) {
                            const float rise = top - position.z;
                            nodeDistance = nodeDistance - std::fabs(rise) * rise;
                        }
                    }
                    if (boundsDistance > nodeDistance) {
                        const float fadeDistance = float(double(mFadeDistanceSq));
                        if (fadeDistance > nodeDistance) alpha = float(std::pow(double(nodeDistance / fadeDistance), 4.0));
                        fade = !NearlyOne(alpha);
                    }
                }
                if (fade) {
                    const float color[4] = {alpha, 1.0f, 1.0f, 1.0f};
                    SetNodeColor(shape.mObject, 0, color);
                    if (!faded) {
                        ChangeNodeFlags(shape.mObject, mask, 0);
                        entry->mFaded = 1;
                    }
                } else if (faded) {
                    const float opaque[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    SetNodeColor(shape.mObject, 0, opaque);
                    ChangeNodeFlags(shape.mObject, 0, mask);
                    entry->mFaded = 0;
                }
            }
        }
    next:
        if (node) node->Release();
    }
}

// ---- adapters ----

using Adapters = cNHoodOccupantManagerAdapters;
using Self = cNHoodOccupantManager;

void __fastcall Adapters::Update(cINHoodOccupantManager *face, void *)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::Update();
}

void __fastcall Adapters::UpdateFade(cINHoodOccupantManager *face, void *, cIGZUnknown *view)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::UpdateFade(view);
}

} // namespace nTSSG
