// nTSSG::cNHoodOccupantManager (lane GO): the node builders: the build-all preparation and the flora, other, face-C and road occupants.
// Split from one file along these concerns; the shared helpers are in nTSSGNHoodOccupantManagerInternal.h.
#include "sg/DetailObject.h"
#include "std/StdInstances.h"
#include "ts/nTSSGNHoodOccupantManager.h"
#include "ts/nTSSGNHoodOccupantManagerInternal.h"

namespace nTSSG {

using namespace occupant_detail;

namespace {

void *AsPointer(uint32_t word) { return reinterpret_cast<void *>(uintptr_t(word)); }

// A virtual base of an object (its vbtable at +4; entry `index` is the base's offset from +4).
void *VirtualBaseAt(void *object, unsigned index)
{
    auto *bytes = static_cast<uint8_t *>(object);
    const int32_t *vbtable = *reinterpret_cast<int32_t *const *>(bytes + 4);
    return bytes + 4 + vbtable[index];
}

// MergeLayerChildren 0x2a7be0.
constexpr uint32_t kCombinerClass = 0xafd79c10;   // the shape combiner (framework slot 3)
constexpr uint32_t kCombinerIid = 0x8fd79c0b;
constexpr unsigned kCombinerAddSlot = 3;          // (shape)
constexpr unsigned kCombinerFinishSlot = 4;
constexpr unsigned kCombinerPropsSlot = 5;        // (props list out)
constexpr uint32_t kTypeFilterVTable = 0xe7cf54;  // a filter by interface id (built inline: 0xaea7a0 / 0xaea7e0)
constexpr unsigned kChildIteratorSlot = 44;       // a node: (out iterator, 0, filter)
constexpr unsigned kIteratorAtEndSlot = 4;        // the iterator: (end) -> bool
constexpr unsigned kIteratorGetSlot = 5;          // -> the current child
constexpr unsigned kIteratorNextSlot = 6;
constexpr unsigned kShapeCountSlot = 98;          // a shape: > 0 when it has something to merge
constexpr unsigned kPartBuilderSlot = 41;         // a flora part: (builder, 0)
constexpr unsigned kBuilderBuildSlot = 34;

// The filter object (vtable 0xe7cf54 and an interface id) the child iterator takes; its destructor writes the vtable again.
struct TypeFilter {
    const void *mVTable;
    uint32_t mIid;
    explicit TypeFilter(uint32_t iid) : mVTable(GameAddress(kTypeFilterVTable)), mIid(iid) {}
    TypeFilter(const TypeFilter &) = delete;
    TypeFilter &operator=(const TypeFilter &) = delete;
    ~TypeFilter() { mVTable = GameAddress(kTypeFilterVTable); }
};
// The child iterator a node's slot 44 fills: three counted words (the iterator, one more, the end), released end first.
struct ChildIterator {
    SGRef mIterator;
    SGRef mMiddle;
    SGRef mEnd;
};

// AddFaceCOccupant 0x2a7130.
constexpr unsigned kOccupantRecordSlot = 3;       // an occupant face: its record; the record's slot 3: its placement
constexpr unsigned kOccupantModelNameSlot = 6;    // (cRZString out)
constexpr unsigned kOccupantOffsetSlot = 5;       // (float[3] out)
constexpr unsigned kOccupantRotationSlot = 8;     // (float[4] in / out, identity first)
constexpr unsigned kPlacementResourceSlot = 12;   // -> an object with virtual bases (counted through the first)
constexpr unsigned kResourceGroupSlot = 34;       // on its second virtual base: (out group) -> found
constexpr unsigned kSgFindSlot = 23;              // scene-graph system: (name, group or null, 0, type, out, 0)
constexpr uint32_t kModelType = 0xe519c92c;
constexpr unsigned kModelValidSlot = 52;          // -> bool
constexpr unsigned kSetShapeSlot = 46;            // (shape)
constexpr uint32_t kFaceCNodeClass = 0x65246462;
constexpr uint32_t kFaceCNodeIid = 0xe51c10f5;
constexpr unsigned kPlacementRefreshSlot = 10;    // (0)

// PrepareBuildAll 0x2a81f0.
constexpr uint32_t kFxCubeNameCell = 0xffac30;    // a pointer in the image's data to "neighborhood_fxCube"
constexpr unsigned kShapePartsSlot = 93;          // a shape: (scratch) -> its parts' table (the first word: the first part)
constexpr unsigned kRef78EntriesSlot = 54;        // +0x78: (out iterator, 0)
constexpr unsigned kRegistryTextSlot = 33;        // GetCachedQueriedObject's object: (scratch, key) -> a C string's cell

// AddFloraOccupant 0x2a5ef0.
constexpr unsigned kFloraModelNameSlot = 5;      // the flora face: (cRZString out) -> has a model
constexpr unsigned kFloraAngleSlot = 7;          // the flora face: its turn in degrees (float, on x87)
constexpr unsigned kStateLoggerSlot = 66;        // 0x211da3's object: the model logger
constexpr unsigned kModelLogSlot = 7;            // the logger: (copy of the name, the name, 0, 1)
constexpr unsigned kShapeOwnerSlot = 34;         // a shape: the object whose slot 46 takes it
constexpr unsigned kShapeNextSlot = 40;          // a shape without parts: the object to ask for the next shape

// AddOtherOccupant 0x2a63a0.
constexpr unsigned kSgLoggerSlot = 66;           // the scene-graph system: the model logger
constexpr unsigned kExtraOwnerSlot = 18;         // the build-all extra's first virtual base: its owner
constexpr unsigned kOwnerAttachSlot = 23;        // that owner: (the base)
constexpr unsigned kShapeAttachExtraSlot = 22;   // the shape: (the extra's first virtual base)
constexpr unsigned kExtraDoneSlot = 15;          // the extra itself
constexpr unsigned kSingletonModeObjectSlot = 33; // GetCachedSingleton's object: the mode object (counted here)
constexpr unsigned kModeKindSlot31 = 31;         // == 1 ...
constexpr unsigned kModeFlagSlot29 = 29;         // ... and non-zero: "WeatherLight" (unless the logger answered)
constexpr unsigned kShapeBoundsSlot = 72;        // (float[6] out: min xyz, max xyz)
constexpr float kTallShape = 15.0f;              // image 0xdb35a8: a top above this fades without the view test
constexpr uint32_t kWaterReflectionText = 0xdb34f4; // "VisibleInWaterReflection"
constexpr uint32_t kWeatherLightText = 0xdae45c;    // "WeatherLight"

// AddRoadOccupant 0x2a69b0: a road occupant (face 0xabd4c3f4) as meshes.
constexpr uint32_t kRoadBuilderClass = 0x7ba3838c; // with kLayerBuilderIid
constexpr uint32_t kRoadMeshClass = 0x9bffc10d;
constexpr uint32_t kRoadMeshIid = 0x9bffc101;      // counted through its first virtual base
constexpr unsigned kBuilderMeshSlot = 32;          // builder: (the mesh's first virtual base)
constexpr unsigned kMeshBeginSlot = 0;             // mesh: (0)
constexpr unsigned kMeshEndSlot = 1;               // mesh: ()
constexpr unsigned kMeshVertexSlot = 9;            // (three floats)
constexpr unsigned kMeshNormalSlot = 10;           // (three floats)
constexpr unsigned kMeshTexCoordSlot = 11;         // (two floats, 0)
constexpr unsigned kMeshSubsetSlot = 27;           // (name)
constexpr unsigned kMeshFaceBeginSlot = 28;        // ()
constexpr unsigned kMeshFaceEndSlot = 29;          // ()
constexpr unsigned kMeshIndexSlot = 35;            // (vertex)
constexpr unsigned kMeshFaceIndexSlot = 36;        // (face)
constexpr unsigned kMeshCornerSlot = 37;           // (vertex, 0)
constexpr unsigned kRoadQuadSlot = 5;              // occupant: (four RoadVertex)
constexpr unsigned kRoadIdSlot = 7;                // occupant: () -> the id in the road's name
constexpr unsigned kRoadPieceCountSlot = 15;       // occupant: ()
constexpr unsigned kRoadPieceSlot = 16;            // occupant: (index, RoadPiece *) -> bool (al)
constexpr unsigned kGroupAddPartSlot = 94;         // group: (part)
constexpr uint32_t kSubsetText = 0xdb30fc;         // game text "subset0"
constexpr uint32_t kRoadNameFormat = 0xdb355c;     // "neighborhood-roads-%s-%08x" (the hood's name, the id)
constexpr uint32_t kRoadPieceFormat = 0xdb3578;    // "neighborhood-roads-%08x" (the piece's id)
constexpr uint32_t kQuadTriangles = 0xdb3544;      // two triangles of the quad: six vertex indices
// The quad's vertex (20 bytes) and a piece (a triangle; 64 bytes), as the occupant fills them.
struct RoadVertex {
    float mPosition[3];
    float mTexCoord[2];
};
struct RoadPiece {
    uint32_t mId;
    float mPosition[3][3];
    float mTexCoord[3][2];
};
static_assert(sizeof(RoadVertex) == 20 && sizeof(RoadPiece) == 64);

using RoadCreate = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *);
using MeshArg = uint32_t(__thiscall *)(void *, uint32_t);
using MeshArg2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using MeshVector = uint32_t(__thiscall *)(void *, const float *);
using MeshVector0 = uint32_t(__thiscall *)(void *, const float *, uint32_t);
using Normal = void(__cdecl *)(float *, const float *, const float *, const float *);
using NameBuilder = void(__cdecl *)(const char *, void *, void *);

// A builder made (framework slot 3); null when refused.
void MakeCounted(SGRef &ref, uint32_t classId, uint32_t iid)
{
    void *framework = GetDirectorFrameworkObject();
    ref.Reset();
    VirtualCall<RoadCreate, kCreateSlot>(framework, classId, iid, &ref.mObject);
}

} // namespace

// 0x2a81f0 (EH): nothing with +0x78 set. The model "neighborhood_fxCube" (scene-graph system slot 23); a valid one (slot 52)
// gives its shape (0xaeba70): with none or no parts (slot 98 below 1) nothing more happens, else its first part (slot 93's
// table) becomes +0x78. Then the text pairs are destroyed (0x257860's work: second, then first) and, with +0x78, refilled
// from its entries (slot 54's iterator: a name and a key the registry's slot 33 turns into the second text; pushed through
// the vector's own copy (0x2a4cb0) or reallocating insert (0x2a4410)), and +0x78's setting applied (0x2a8020).
void cNHoodOccupantManager::PrepareBuildAll()
{
    if (mRef78.mObject) return;
    SGRef found;
    void *sg = CachedServiceGetter<kSgSystemCache, kSgSystemIdA, kSgSystemIdB>();
    found.Reset();
    using Find = uint32_t(__thiscall *)(void *, const char *, uint32_t *, uint32_t, uint32_t, cIGZUnknown **, uint32_t);
    VirtualCall<Find, kSgFindSlot>(sg, *GameAddress<const char *>(kFxCubeNameCell), nullptr, 0, kModelType, &found.mObject, 0);
    if (found.mObject && VirtualCall<bool(__thiscall *)(void *), kModelValidSlot>(found.mObject)) {
        auto *cube = static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(void *), 0xaeba70>()(found.mObject));
        CountedLocal heldCube(cube);
        if (!cube || int32_t(VirtualCall<Get0, kShapeCountSlot>(cube)) < 1) return;
        uint32_t scratch[3];
        auto **parts = reinterpret_cast<cIGZUnknown ***>(
            VirtualCall<void *(__thiscall *)(void *, uint32_t *), kShapePartsSlot>(cube, scratch));
        AssignCounted(mRef78, **parts);
    }
    mTexts.clear(); // each pair's destructor: second, then first (0x257860's order)
    if (!mRef78.mObject) return;
    void *registry = GetCachedQueriedObject();
    ChildIterator entries;
    VirtualCall<uint32_t(__thiscall *)(void *, ChildIterator *, uint32_t), kRef78EntriesSlot>(mRef78.mObject, &entries, 0);
    using AtEnd = bool(__thiscall *)(void *, cIGZUnknown *);
    while (!VirtualCall<AtEnd, kIteratorAtEndSlot>(entries.mIterator.mObject, entries.mEnd.mObject)) {
        // 16 bytes: a name, the registry's key and two words the game fills too (the original's [ebp-0x38] up to the
        // iterator at [ebp-0x28]; an 8-byte buffer here let them overwrite the registry pointer in game)
        const char *entry[4];
        VirtualCall<uint32_t(__thiscall *)(void *, const char **), kIteratorGetSlot>(entries.mIterator.mObject, entry);
        SGTextPair pair;
        pair.mFirst.mString.mString.assign(entry[0]);
        uint32_t scratch[5];
        const char *const *second = VirtualCall<const char *const *(__thiscall *)(void *, uint32_t *, const char *),
            kRegistryTextSlot>(registry, scratch, entry[1]);
        pair.mSecond.mString.mString.assign(*second);
        SGTextPair **words = reinterpret_cast<SGTextPair **>(&mTexts); // first, last, end
        if (words[1] == words[2]) {
            OriginalFunction<void(__thiscall *)(void *, SGTextPair *, SGTextPair *), 0x2a4410>()(&mTexts, words[1], &pair);
        } else {
            OriginalFunction<void(__thiscall *)(SGTextPair *, SGTextPair *), 0x2a4cb0>()(words[1], &pair);
            ++words[1];
        }
        VirtualCall<Get0, kIteratorNextSlot>(entries.mIterator.mObject);
    }
    ApplyRef78Setting();
}

// 0x2a5ef0 (ret 4, EH): a flora occupant's node built. Its model name (slot 5; nothing without one), logged (with option bit
// 7) through 0x211da3's logger; looked up through +0x30's slot 23 (with the placement's resource group, as 0x2a7130); a
// found model's shape (0xaeba70) handed to its owner (slot 34's slot 46), then followed while valid (slot 52) and without
// parts (slot 98): slot 40's QI 0xa516cd10 replaces it. The shape is grouped Props and Shade, placed at the placement's position
// (moved by 0x2a8640 for one lot), turned by (lot byte * 90 + the occupant's angle) degrees about z (0x1a72c0), attached
// (0x2a53f0), 0x2a9000 told; for one lot 0xaeac50(0x15, 1) and the shape queued on the fade list (0x2a9c60). The occupant's
// layer is marked dirty whenever the name was there.
void cNHoodOccupantManager::AddFloraOccupant(cIGZUnknown *occupant)
{
    using Vector = uint32_t(__thiscall *)(void *, float *);
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    using Valid = bool(__thiscall *)(void *);
    using Group = uint32_t(__thiscall *)(void *, const char *);
    SGString name;
    if (!VirtualCall<bool(__thiscall *)(void *, cRZString *), kFloraModelNameSlot>(occupant, &name.mString)) return;
    if (mOptionBit7) {
        void *state = OriginalFunction<void *(__cdecl *)(), 0x211da3>()();
        void *logger = AsPointer(VirtualCall<Get0, kStateLoggerSlot>(state));
        SGString copy;
        copy.mString.mString.assign(name.mString.mString.c_str(), name.mString.mString.size());
        VirtualCall<uint32_t(__thiscall *)(void *, cRZString *, cRZString *, uint32_t, uint32_t), kModelLogSlot>(logger,
            &copy.mString, &name.mString, 0, 1);
    }
    uint32_t *groupOrNull = nullptr;
    void *placement = AsPointer(VirtualCall<Get0, kOccupantRecordSlot>(occupant));
    void *resource = AsPointer(VirtualCall<Get0, kPlacementResourceSlot>(placement));
    VirtualBaseRef heldResource(resource);
    uint32_t group;
    if (resource &&
        VirtualCall<bool(__thiscall *)(void *, uint32_t *), kResourceGroupSlot>(VirtualBaseAt(resource, 2), &group))
        groupOrNull = &group;
    SGRef found;
    using Find = uint32_t(__thiscall *)(void *, const char *, uint32_t *, uint32_t, uint32_t, cIGZUnknown **, uint32_t);
    VirtualCall<Find, kSgFindSlot>(mSgSystem.mObject, name.mString.ToChar(), groupOrNull, 0, kModelType, &found.mObject, 0);
    if (found.mObject) {
        SGRef shape;
        shape.mObject = static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(void *), 0xaeba70>()(found.mObject));
        if (shape.mObject) shape.mObject->AddRef();
        VirtualCall<Child, kSetShapeSlot>(AsPointer(VirtualCall<Get0, kShapeOwnerSlot>(shape.mObject)), shape.mObject);
        if (VirtualCall<Valid, kModelValidSlot>(shape.mObject)) {
            while (!VirtualCall<Get0, kShapeCountSlot>(shape.mObject)) {
                {
                    SGRef next;
                    void *from = AsPointer(VirtualCall<Get0, kShapeNextSlot>(shape.mObject));
                    next.Reset();
                    if (VirtualCall<bool(__thiscall *)(void *, uint32_t, cIGZUnknown **), 0>(from, kShapeNodeIid, &next.mObject))
                        AssignCounted(shape, next.mObject);
                }
                if (!VirtualCall<Valid, kModelValidSlot>(shape.mObject)) break;
            }
        }
        VirtualCall<Group, kNodeGroupSlot>(shape.mObject, GameAddress<const char>(kPropsText));
        VirtualCall<Group, kNodeGroupSlot>(shape.mObject, GameAddress<const char>(kShadeText));
        OriginalFunction<void *(__cdecl *)(), 0x211ce1>()(); // the scene (result unused)
        void *at = AsPointer(VirtualCall<Get0, kOccupantRecordSlot>(occupant));
        float position[3];
        VirtualCall<Vector, kPlacementPositionSlot>(at, position);
        if (!mBuildAll) OriginalFunction<void(__cdecl *)(float *), 0x2a8640>()(position);
        VirtualCall<Vector, kNodePlaceSlot>(shape.mObject, position);
        const float turn = VirtualCall<float(__thiscall *)(void *), kFloraAngleSlot>(occupant);
        float angles[3] = {0.0f, 0.0f, (float(int32_t(mLotByte)) * kQuarterTurnDegrees + turn) * kDegreesToRadians};
        float rotation[4];
        OriginalFunction<void(__cdecl *)(float *, const float *), 0x1a72c0>()(rotation, angles);
        VirtualCall<Vector, kNodeRotateSlot>(shape.mObject, rotation);
        OriginalFunction<void(__thiscall *)(cNHoodOccupantManager *, void *, cIGZUnknown *), 0x2a53f0>()(this, at,
            shape.mObject);
        OriginalFunction<void(__thiscall *)(cNHoodOccupantManager *, cIGZUnknown *), 0x2a9000>()(this, occupant);
        if (!mBuildAll) {
            OriginalFunction<void(__cdecl *)(cIGZUnknown *, uint32_t, uint32_t), 0xaeac50>()(shape.mObject, kFloraSettingB, 1);
            SGNodeEntry entry;
            entry.mNode.mObject = shape.mObject;
            if (shape.mObject) shape.mObject->AddRef();
            entry.mFaded = 0;
            OriginalFunction<void(__thiscall *)(void *, SGNodeEntry *), 0x2a9c60>()(&mNodes, &entry);
        }
    }
    mLayerDirty[LayerOf(occupant)] = 1;
}

// 0x2a63a0 (ret 4, EH): one of app slot 12's other occupants built. Its record (slot 3) and model name (slot 5; false without
// one), logged (option bit 7) through the cached scene-graph system's slot 66 logger (whose answer is kept); the model found
// as in 0x2a7130 (cached system's slot 23; true when none or invalid); 0xaeb9a0's model (false without); for build-all the
// found object goes to 0xb3c070 and 0x267560 gives an extra (counted through its first virtual base); the shape (the model's
// QI 0xa516cd10, set on the found object; else 0xaeb8a0's, set on the model when there is one; false without); with the extra:
// +0x78 added to the shape (slot 94) when set, the extra's first base attached to its owner (slot 18's slot 23) and to the
// shape (slot 22), the extra's slot 15; groups Props and VisibleInWaterReflection, and WeatherLight when the singleton's mode
// object says so; placed, turned and attached as the flora; for one lot, when its top (slot 72) is above 15 or its footprint
// touches the view (FootprintIn), 0xaeac50(0x15, 1) and queued on the fade list (0x2a9c60). True.
bool cNHoodOccupantManager::AddOtherOccupant(cIGZUnknown *occupant)
{
    using Vector = uint32_t(__thiscall *)(void *, float *);
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    using Group = uint32_t(__thiscall *)(void *, const char *);
    void *record = AsPointer(VirtualCall<Get0, kOccupantRecordSlot>(occupant));
    SGString name;
    bool logged = false;
    if (!VirtualCall<bool(__thiscall *)(void *, cRZString *), kFloraModelNameSlot>(occupant, &name.mString)) return false;
    void *sg = CachedServiceGetter<kSgSystemCache, kSgSystemIdA, kSgSystemIdB>();
    if (mOptionBit7) {
        void *logger = AsPointer(VirtualCall<Get0, kSgLoggerSlot>(sg));
        SGString copy;
        copy.mString.mString.assign(name.mString.mString.c_str(), name.mString.mString.size());
        logged = VirtualCall<bool(__thiscall *)(void *, cRZString *, cRZString *, uint32_t, uint32_t), kModelLogSlot>(logger,
            &copy.mString, &name.mString, 0, 1);
    }
    void *resource = AsPointer(VirtualCall<Get0, kPlacementResourceSlot>(record));
    VirtualBaseRef heldResource(resource);
    uint32_t group;
    uint32_t *groupOrNull = nullptr;
    if (resource &&
        VirtualCall<bool(__thiscall *)(void *, uint32_t *), kResourceGroupSlot>(VirtualBaseAt(resource, 2), &group))
        groupOrNull = &group;
    SGRef found;
    using Find = uint32_t(__thiscall *)(void *, const char *, uint32_t *, uint32_t, uint32_t, cIGZUnknown **, uint32_t);
    VirtualCall<Find, kSgFindSlot>(sg, name.mString.ToChar(), groupOrNull, 0, kModelType, &found.mObject, 0);
    if (!found.mObject || !VirtualCall<bool(__thiscall *)(void *), kModelValidSlot>(found.mObject)) return true;
    auto *model = static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(void *), 0xaeb9a0>()(found.mObject));
    CountedLocal heldModel(model);
    if (!model) return false;
    VirtualBaseRef extra;
    if (mBuildAll) {
        OriginalFunction<void(__cdecl *)(void *), 0xb3c070>()(found.mObject);
        void *object = OriginalFunction<void *(__cdecl *)(void *), 0x267560>()(found.mObject);
        if (object) VirtualBase(object)->AddRef();
        extra.mObject = object;
    }
    SGRef shape;
    if (VirtualCall<bool(__thiscall *)(void *, uint32_t, cIGZUnknown **), 0>(model, kShapeNodeIid, &shape.mObject)) {
        VirtualCall<Child, kSetShapeSlot>(found.mObject, shape.mObject);
    } else {
        AssignCounted(shape, static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(void *), 0xaeb8a0>()(model)));
        if (shape.mObject) VirtualCall<Child, kSetShapeSlot>(model, shape.mObject);
    }
    if (!shape.mObject) return false;
    if (extra.mObject) {
        if (mRef78.mObject) VirtualCall<Child, kFloraAddPartSlot>(shape.mObject, mRef78.mObject);
        void *base = VirtualBaseAt(extra.mObject, 1);
        void *owner = AsPointer(VirtualCall<Get0, kExtraOwnerSlot>(base));
        VirtualCall<uint32_t(__thiscall *)(void *, void *), kOwnerAttachSlot>(owner, base);
        VirtualCall<uint32_t(__thiscall *)(void *, void *), kShapeAttachExtraSlot>(shape.mObject, VirtualBaseAt(extra.mObject, 1));
        VirtualCall<Get0, kExtraDoneSlot>(extra.mObject);
    }
    VirtualCall<Group, kNodeGroupSlot>(shape.mObject, GameAddress<const char>(kPropsText));
    VirtualCall<Group, kNodeGroupSlot>(shape.mObject, GameAddress<const char>(kWaterReflectionText));
    auto *mode = static_cast<cIGZUnknown *>(AsPointer(VirtualCall<Get0, kSingletonModeObjectSlot>(GetCachedSingleton())));
    CountedLocal heldMode(mode);
    if (mode && VirtualCall<Get0, kModeKindSlot31>(mode) == 1 && VirtualCall<Get0, kModeFlagSlot29>(mode) && !logged)
        VirtualCall<Group, kNodeGroupSlot>(shape.mObject, GameAddress<const char>(kWeatherLightText));
    float position[3];
    VirtualCall<Vector, kPlacementPositionSlot>(record, position);
    if (!mBuildAll) OriginalFunction<void(__cdecl *)(float *), 0x2a8640>()(position);
    VirtualCall<Vector, kNodePlaceSlot>(shape.mObject, position);
    const float turn = VirtualCall<float(__thiscall *)(void *), kFloraAngleSlot>(occupant);
    float angles[3] = {0.0f, 0.0f, (float(int32_t(mLotByte)) * kQuarterTurnDegrees + turn) * kDegreesToRadians};
    float rotation[4];
    OriginalFunction<void(__cdecl *)(float *, const float *), 0x1a72c0>()(rotation, angles);
    VirtualCall<Vector, kNodeRotateSlot>(shape.mObject, rotation);
    OriginalFunction<void(__thiscall *)(cNHoodOccupantManager *, void *, cIGZUnknown *), 0x2a53f0>()(this, record,
        shape.mObject);
    if (!mBuildAll) {
        float bounds[6];
        VirtualCall<Vector, kShapeBoundsSlot>(shape.mObject, bounds);
        if (bounds[5] > kTallShape ||
            FootprintIn(AsPointer(VirtualCall<Get0, kOccupantRecordSlot>(occupant)), mViewRect, 0)) {
            OriginalFunction<void(__cdecl *)(cIGZUnknown *, uint32_t, uint32_t), 0xaeac50>()(shape.mObject, kFloraSettingB, 1);
            SGNodeEntry entry; // 0x2a4350's copy of {shape, 0}
            entry.mNode.mObject = shape.mObject;
            shape.mObject->AddRef();
            entry.mFaded = 0;
            OriginalFunction<void(__thiscall *)(void *, SGNodeEntry *), 0x2a9c60>()(&mNodes, &entry);
        }
    }
    return true;
}

// 0x2a7be0 (ret 8, EH; this unused): a layer's shape children merged: a combiner (framework slot 3) gets each child
// shape (the layer's slot 44 iterator over QI 0xa516cd10, each child's shape asked again, added when its slot 98 is
// positive); a group node with one flora part (its builder the combiner's face 0x5ba38381, built through slot 34), the
// combiner's props entries named on the part (slots 34, 36 with "Props" and "Shade"), the combiner finished (slot 4); the
// group's shape face into `out` (the result).
bool cNHoodOccupantManager::MergeLayerChildren(cIGZUnknown *layer, cIGZUnknown **out)
{
    using Create = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *);
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    SGRef combiner;
    void *framework = GetDirectorFrameworkObject();
    combiner.Reset();
    VirtualCall<Create, kCreateSlot>(framework, kCombinerClass, kCombinerIid, &combiner.mObject);
    TypeFilter filter(kShapeNodeIid);
    ChildIterator children;
    VirtualCall<uint32_t(__thiscall *)(void *, ChildIterator *, uint32_t, TypeFilter *), kChildIteratorSlot>(layer, &children, 0,
        &filter);
    using AtEnd = bool(__thiscall *)(void *, cIGZUnknown *);
    while (!VirtualCall<AtEnd, kIteratorAtEndSlot>(children.mIterator.mObject, children.mEnd.mObject)) {
        {
            SGRef shape;
            void *child = AsPointer(VirtualCall<Get0, kIteratorGetSlot>(children.mIterator.mObject));
            shape.Reset();
            VirtualCall<Query, 0>(child, kShapeNodeIid, &shape.mObject);
            if (int32_t(VirtualCall<Get0, kShapeCountSlot>(shape.mObject)) > 0)
                VirtualCall<Child, kCombinerAddSlot>(combiner.mObject, shape.mObject);
        }
        VirtualCall<Get0, kIteratorNextSlot>(children.mIterator.mObject);
    }
    SGRef builder;
    VirtualCall<Query, 0>(combiner.mObject, kLayerBuilderIid, &builder.mObject);
    SGRef group;
    framework = GetDirectorFrameworkObject();
    group.Reset();
    VirtualCall<Create, kCreateSlot>(framework, kGroupNodeClass, kShapeNodeIid, &group.mObject);
    SGRef part;
    framework = GetDirectorFrameworkObject();
    part.Reset();
    VirtualCall<Create, kCreateSlot>(framework, kFloraChildClass, kFloraChildIid, &part.mObject);
    VirtualCall<Child, kFloraAddPartSlot>(group.mObject, part.mObject);
    VirtualCall<uint32_t(__thiscall *)(void *, cIGZUnknown *, uint32_t), kPartBuilderSlot>(part.mObject, builder.mObject, 0);
    VirtualCall<Get0, kBuilderBuildSlot>(builder.mObject);
    PropsList props;
    VirtualCall<uint32_t(__thiscall *)(void *, PropsList *), kCombinerPropsSlot>(combiner.mObject, &props);
    for (uint32_t i = 0; i < uint32_t(props.mLast - props.mFirst); ++i) {
        PropsEntry &entry = props.mFirst[i];
        const char *first = entry.mFirst.mCapacity > 15 ? entry.mFirst.mPtr : entry.mFirst.mBuf;
        const char *second = entry.mSecond.mCapacity > 15 ? entry.mSecond.mPtr : entry.mSecond.mBuf;
        using Names = uint32_t(__thiscall *)(void *, const char *, const char *);
        VirtualCall<Names, kTargetPairSlot>(part.mObject, second, first);
        VirtualCall<Names, kTargetGroupSlot>(part.mObject, GameAddress<const char>(kPropsText), first);
        VirtualCall<Names, kTargetGroupSlot>(part.mObject, GameAddress<const char>(kShadeText), first);
    }
    VirtualCall<Get0, kCombinerFinishSlot>(combiner.mObject);
    return VirtualCall<bool(__thiscall *)(void *, uint32_t, cIGZUnknown **), 0>(group.mObject, kShapeNodeIid, out);
    // leaving: the props list, part, group, builder, the iterator (end, middle, iterator), the filter, the combiner
}

// 0x2a7130 (ret 4, EH): an occupant of face 0x6bd4d6ce built: its model name (slot 6) looked up in the scene-graph system
// (slot 23, with the placement's resource group where its second virtual base answers one); a valid model (slot 52) gives
// its shape (0xaeb9a0, then QI 0xa516cd10 or 0xaeb8a0) set on the found object or the model (slot 46), grouped "Props"; a
// node of class 0x65246462 put under the scene (0x211ce1, slot 11) with the shape, placed at the placement's position plus the
// occupant's offset, turned by the occupant's rotation, attached to the placement (0x2a53f0), the placement refreshed (slot
// 10). Always true.
bool cNHoodOccupantManager::AddFaceCOccupant(cIGZUnknown *occupant)
{
    using Vector = uint32_t(__thiscall *)(void *, float *);
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    void *record = AsPointer(VirtualCall<Get0, kOccupantRecordSlot>(occupant));
    SGString name;
    VirtualCall<uint32_t(__thiscall *)(void *, cRZString *), kOccupantModelNameSlot>(occupant, &name.mString);
    uint32_t *groupOrNull = nullptr;
    void *placement = AsPointer(VirtualCall<Get0, kOccupantRecordSlot>(record));
    void *resource = AsPointer(VirtualCall<Get0, kPlacementResourceSlot>(placement));
    VirtualBaseRef heldResource(resource);
    uint32_t group;
    if (resource &&
        VirtualCall<bool(__thiscall *)(void *, uint32_t *), kResourceGroupSlot>(VirtualBaseAt(resource, 2), &group))
        groupOrNull = &group;
    SGRef found;
    void *sg = CachedServiceGetter<kSgSystemCache, kSgSystemIdA, kSgSystemIdB>();
    found.Reset();
    using Find = uint32_t(__thiscall *)(void *, const char *, uint32_t *, uint32_t, uint32_t, cIGZUnknown **, uint32_t);
    VirtualCall<Find, kSgFindSlot>(sg, name.mString.ToChar(), groupOrNull, 0, kModelType, &found.mObject, 0);
    if (found.mObject && VirtualCall<bool(__thiscall *)(void *), kModelValidSlot>(found.mObject)) {
        auto *model = static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(void *), 0xaeb9a0>()(found.mObject));
        CountedLocal heldModel(model);
        SGRef shape;
        if (VirtualCall<bool(__thiscall *)(void *, uint32_t, cIGZUnknown **), 0>(model, kShapeNodeIid, &shape.mObject)) {
            VirtualCall<Child, kSetShapeSlot>(found.mObject, shape.mObject);
        } else {
            AssignCounted(shape, static_cast<cIGZUnknown *>(OriginalFunction<void *(__cdecl *)(void *), 0xaeb8a0>()(model)));
            VirtualCall<Child, kSetShapeSlot>(model, shape.mObject);
        }
        VirtualCall<uint32_t(__thiscall *)(void *, const char *), kNodeGroupSlot>(shape.mObject,
            GameAddress<const char>(kPropsText));
        void *scene = OriginalFunction<void *(__cdecl *)(), 0x211ce1>()();
        SGRef node;
        void *framework = GetDirectorFrameworkObject();
        node.Reset();
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *), kCreateSlot>(framework, kFaceCNodeClass,
            kFaceCNodeIid, &node.mObject);
        VirtualCall<uint32_t(__thiscall *)(void *, cIGZUnknown *, cIGZUnknown *), kSceneAddUnderSlot>(scene, node.mObject,
            shape.mObject);
        void *at = AsPointer(VirtualCall<Get0, kOccupantRecordSlot>(record));
        float base[3];
        VirtualCall<Vector, kPlacementPositionSlot>(at, base);
        float offset[3];
        VirtualCall<Vector, kOccupantOffsetSlot>(occupant, offset);
        float position[3] = {base[0] + offset[0], base[1] + offset[1], base[2] + offset[2]};
        VirtualCall<Vector, kNodePlaceSlot>(node.mObject, position);
        float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // image 0xda8660
        VirtualCall<Vector, kOccupantRotationSlot>(occupant, rotation);
        VirtualCall<Vector, kNodeRotateSlot>(node.mObject, rotation);
        OriginalFunction<void(__thiscall *)(cNHoodOccupantManager *, void *, cIGZUnknown *), 0x2a53f0>()(this, at,
            node.mObject);
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kPlacementRefreshSlot>(at, 0);
    }
    return true;
}

// 0x2a69b0 (ret 4, EH): a road occupant (face 0xabd4c3f4). A builder (class 0x7ba3838c) and a mesh (0x9bffc10d, counted
// through its virtual base) make the road's quad: subset "subset0", the occupant's four vertices (slot 5: position, texture
// coordinate) and the two triangles of the image's index table, each with its normal (0x2a5900) and corner indices; a part
// (0xfc6eb1f7) takes the builder, named "neighborhood-roads-<hood>-<id>" (0xaed340). A group node gets the part, then one
// part per piece the occupant hands out (slots 15, 16: a triangle with its own builder, mesh and part, named
// "neighborhood-roads-<piece id>"). The group gets priority 999 (0xaeb070) and "Props", and is attached to the occupant's
// placement (0x2a53f0, slot 10(0)). False when a builder, mesh or part is refused (a piece's too: the rest is skipped).
bool cNHoodOccupantManager::AddRoadOccupant(cIGZUnknown *occupant)
{
    SGRef builder;
    MakeCounted(builder, kRoadBuilderClass, kLayerBuilderIid);
    if (!builder.mObject) return false;
    VirtualBaseRef mesh;
    {
        void *framework = GetDirectorFrameworkObject();
        VirtualCall<RoadCreate, kCreateSlot>(framework, kRoadMeshClass, kRoadMeshIid, &mesh.mObject);
    }
    if (!mesh.mObject) return false;
    void *quadMesh = mesh.mObject;
    VirtualCall<uint32_t(__thiscall *)(void *, void *), kBuilderMeshSlot>(builder.mObject, VirtualBaseAt(quadMesh, 1));
    VirtualCall<MeshArg, kMeshBeginSlot>(quadMesh, 0);
    VirtualCall<uint32_t(__thiscall *)(void *, const char *), kMeshSubsetSlot>(quadMesh, GameAddress<const char>(kSubsetText));
    RoadVertex quad[4];
    VirtualCall<uint32_t(__thiscall *)(void *, RoadVertex *), kRoadQuadSlot>(occupant, quad);
    for (RoadVertex &v : quad) {
        VirtualCall<MeshVector, kMeshVertexSlot>(quadMesh, v.mPosition);
        VirtualCall<MeshVector0, kMeshTexCoordSlot>(quadMesh, v.mTexCoord, 0);
    }
    const uint32_t *triangles = GameAddress<const uint32_t>(kQuadTriangles);
    for (uint32_t face = 0; face < 2; ++face) {
        const uint32_t *corner = triangles + face * 3;
        float normal[3];
        OriginalFunction<Normal, 0x2a5900>()(normal, quad[corner[0]].mPosition, quad[corner[1]].mPosition,
            quad[corner[2]].mPosition);
        VirtualCall<MeshVector, kMeshNormalSlot>(quadMesh, normal);
        VirtualCall<Get0, kMeshFaceBeginSlot>(quadMesh);
        for (int i = 0; i < 3; ++i) VirtualCall<MeshArg, kMeshIndexSlot>(quadMesh, corner[i]);
        for (int i = 0; i < 3; ++i) VirtualCall<MeshArg, kMeshFaceIndexSlot>(quadMesh, face);
        for (int i = 0; i < 3; ++i) VirtualCall<MeshArg2, kMeshCornerSlot>(quadMesh, corner[i], 0);
        VirtualCall<Get0, kMeshFaceEndSlot>(quadMesh);
    }
    VirtualCall<Get0, kMeshEndSlot>(quadMesh);
    SGRef part;
    MakeCounted(part, kFloraChildClass, kFloraChildIid);
    if (!part.mObject) return false;
    SGString name;
    const uint32_t id = VirtualCall<Get0, kRoadIdSlot>(occupant);
    name.mString.Sprintf(GameAddress<const char>(kRoadNameFormat), mNeighborhoodName.mString.ToChar(), id);
    using AttachBuilder = uint32_t(__thiscall *)(void *, cIGZUnknown *, uint32_t);
    VirtualCall<AttachBuilder, kPartBuilderSlot>(part.mObject, builder.mObject, 0);
    OriginalFunction<NameBuilder, 0xaed340>()(name.mString.ToChar(), builder.mObject, part.mObject);
    SGRef group;
    MakeCounted(group, kGroupNodeClass, kShapeNodeIid);
    using AddPart = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    VirtualCall<AddPart, kGroupAddPartSlot>(group.mObject, part.mObject);
    const uint32_t pieces = VirtualCall<Get0, kRoadPieceCountSlot>(occupant);
    for (uint32_t index = 0; index < pieces; ++index) {
        RoadPiece piece;
        if (!VirtualCall<bool(__thiscall *)(void *, uint32_t, RoadPiece *), kRoadPieceSlot>(occupant, index, &piece)) continue;
        SGRef pieceBuilder;
        MakeCounted(pieceBuilder, kRoadBuilderClass, kLayerBuilderIid);
        if (!pieceBuilder.mObject) return false;
        VirtualBaseRef pieceMesh;
        {
            void *framework = GetDirectorFrameworkObject();
            VirtualCall<RoadCreate, kCreateSlot>(framework, kRoadMeshClass, kRoadMeshIid, &pieceMesh.mObject);
        }
        if (!pieceMesh.mObject) return false;
        SGRef piecePart;
        MakeCounted(piecePart, kFloraChildClass, kFloraChildIid);
        if (!piecePart.mObject) return false;
        void *triangle = pieceMesh.mObject;
        VirtualCall<uint32_t(__thiscall *)(void *, void *), kBuilderMeshSlot>(pieceBuilder.mObject, VirtualBaseAt(triangle, 1));
        VirtualCall<MeshArg, kMeshBeginSlot>(triangle, 0);
        VirtualCall<uint32_t(__thiscall *)(void *, const char *), kMeshSubsetSlot>(triangle, GameAddress<const char>(kSubsetText));
        float normal[3];
        OriginalFunction<Normal, 0x2a5900>()(normal, piece.mPosition[0], piece.mPosition[1], piece.mPosition[2]);
        VirtualCall<MeshVector, kMeshNormalSlot>(triangle, normal);
        for (int i = 0; i < 3; ++i) {
            VirtualCall<MeshVector, kMeshVertexSlot>(triangle, piece.mPosition[i]);
            VirtualCall<MeshVector0, kMeshTexCoordSlot>(triangle, piece.mTexCoord[i], 0);
        }
        VirtualCall<Get0, kMeshFaceBeginSlot>(triangle);
        for (uint32_t i = 0; i < 3; ++i) {
            VirtualCall<MeshArg, kMeshIndexSlot>(triangle, i);
            VirtualCall<MeshArg, kMeshFaceIndexSlot>(triangle, 0);
            VirtualCall<MeshArg2, kMeshCornerSlot>(triangle, i, 0);
        }
        VirtualCall<Get0, kMeshFaceEndSlot>(triangle);
        VirtualCall<Get0, kMeshEndSlot>(triangle);
        SGString pieceName;
        pieceName.mString.Sprintf(GameAddress<const char>(kRoadPieceFormat), piece.mId);
        VirtualCall<AttachBuilder, kPartBuilderSlot>(piecePart.mObject, pieceBuilder.mObject, 0);
        OriginalFunction<NameBuilder, 0xaed340>()(pieceName.mString.ToChar(), pieceBuilder.mObject, piecePart.mObject);
        VirtualCall<AddPart, kGroupAddPartSlot>(group.mObject, piecePart.mObject);
        // leaving: the name, the part, the mesh, the builder
    }
    OriginalFunction<void(__cdecl *)(cIGZUnknown *, uint32_t), 0xaeb070>()(group.mObject, kMergedLayerPriority);
    VirtualCall<uint32_t(__thiscall *)(void *, const char *), kNodeGroupSlot>(group.mObject, GameAddress<const char>(kPropsText));
    void *record = AsPointer(VirtualCall<Get0, kOccupantRecordSlot>(occupant));
    OriginalFunction<void(__thiscall *)(cNHoodOccupantManager *, void *, cIGZUnknown *), 0x2a53f0>()(this, record, group.mObject);
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kPlacementRefreshSlot>(record, 0);
    return true;
    // leaving: the group, the name, the part, the mesh, the builder
}

} // namespace nTSSG
