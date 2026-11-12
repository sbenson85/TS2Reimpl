// The design-mode module (lane 261111o). Declarations and behaviour: sg/DesignMode.h.
#include "sg/DesignMode.h"

#include "app/AppDebugger.h"  // appdebugger::FreeStringBuffer
#include "app/AppServices2.h" // as2::RawString, as2::FreeViaAllocatorService
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZIniFile.h" // cRZIniStringHelpers::SplitOff
#include "rz/StringKeyParser.h" // cRZStringHelpers::EndsWith
#include "rz/cRZString.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cDataListExtension.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"

#include <string.h>

namespace DesignMode {

namespace {

using nGZSceneGraph::cChildRange;
using nGZSceneGraph::cIDataListExtension;
using nGZSceneGraph::cIShapeRefNode;
using nGZSceneGraph::cShapeRange;

constexpr uint32_t kPortalIid = 0xfa1c39ed, kPortalHolderIid = 0xfa1c39f7, kShapeRefIid = 0xa516cd10;
constexpr int kGeometrySlot = 50;                                           // a shape's geometry (FirstPartObject2)
constexpr int kNamesSlot = 42, kIsEqualSlot = 4, kGetSlot = 5, kNextSlot = 6; // the geometry's subset-name range
constexpr int kStrlenSlot = 7, kToCharSlot = 5, kIsEqualRefSlot = 11, kAppendSlot = 19; // cIGZString
constexpr int kComponentSlot = 9;                                          // 0x3aaec0: the object's face lookup
constexpr int kListCountSlot = 15, kListItemSlot = 5;                      // 0x3ab570: the shape container
constexpr int kTableSlot = 26, kObjectSlot = 29;                           // 0x3abe70
const char kEnabledList[] = "tsDesignModeEnabled";
const char kMeshNameList[] = "tsMaterialsMeshName";
const char kShadow[] = "shadow";
const char kCres[] = "_cres";

struct cNameRange {
    cIGZUnknown *mCurrent, *mMiddle, *mEnd;
};

// The face's virtual base (vbptr at +4, the base's offset in its second entry): where the portal face and the data
// lists are AddRef'd and Released.
cIGZUnknown *VirtualBaseOf(void *face)
{
    char *p = static_cast<char *>(face) + 4;
    return reinterpret_cast<cIGZUnknown *>(p + (*reinterpret_cast<int32_t **>(p))[1]);
}

void *GeometryOf(cIShape *shape) { return RawSlot<kGeometrySlot, void *>(shape); }
uint32_t Strlen(const void *string) { return RawSlot<kStrlenSlot, uint32_t>(string); }
const char *TextOf(const void *string) { return RawSlot<kToCharSlot, const char *>(string); }

// The geometry's subset names in order, each copied into a local string for `visit`; true (and the walk ended) when
// `visit` says so. The range's iterators are Released end, middle, current.
template <class Visit> bool AnySubsetName(void *geometry, Visit visit)
{
    cNameRange range;
    RawSlot<kNamesSlot, cNameRange *>(geometry, &range);
    bool found = false;
    while (!RawSlot<kIsEqualSlot, bool>(range.mCurrent, range.mEnd)) {
        cRZString name;
        name.Construct(RawSlot<kGetSlot, const char *>(range.mCurrent));
        found = visit(name);
        if (!found) RawSlot<kNextSlot, uint32_t>(range.mCurrent);
        name.Destruct();
        if (found) break;
    }
    if (range.mEnd) range.mEnd->Release();
    if (range.mMiddle) range.mMiddle->Release();
    if (range.mCurrent) range.mCurrent->Release();
    return found;
}

// A subset name equal to `subset` (case-blind; the subset's text asked first, each time).
bool HasSubsetNamed(void *geometry, cIGZString *subset)
{
    return AnySubsetName(geometry, [&](cRZString &name) {
        const char *wanted = TextOf(subset);
        return _stricmp(name.ToChar(), wanted) == 0;
    });
}

// One of the data lists on the geometry, AddRef'd (null when there is none).
cIDataListExtension *DataList(void *geometry, const char *name)
{
    cIDataListExtension *list =
        nGZSceneGraph::FindDataList(static_cast<cICompositionTreeNode *>(geometry), name, nullptr);
    if (list) VirtualBaseOf(list)->AddRef();
    return list;
}

void Empty(cRZString *string) { string->mString.clear(); }

} // namespace

// 0x3abe70
void *ObjectById(int16_t id)
{
    void *const table = RawSlot<kTableSlot, void *>(GetCachedSingleton());
    return RawSlot<kObjectSlot, void *>(table, static_cast<int32_t>(id));
}

// 0x3aa260
void CollectChildShapes(cICompositionTreeNode *node, ShapeList *shapes)
{
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        void *portal = nullptr;
        if (!child->QueryInterface(kPortalIid, &portal)) {
            cIGZUnknown *shapeRef = nullptr;
            if (child->QueryInterface(kShapeRefIid, reinterpret_cast<void **>(&shapeRef))) {
                const cShapeRange range = static_cast<cIShapeRefNode *>(shapeRef)->GetShapes();
                for (cIShape **shape = range.mFirst; shape != range.mEnd; ++shape) {
                    const fx::CountedRef<cIShape> held(*shape);
                    shapes->push_back(held);
                }
            }
            CollectChildShapes(child, shapes);
            if (shapeRef) shapeRef->Release();
        }
        if (portal) VirtualBaseOf(portal)->Release();
    }
}

// 0x3abe90
bool IsDesignModeSubset(cIShape *shape, cIGZString *subset)
{
    void *const geometry = GeometryOf(shape);
    if (!geometry) return false;
    cIDataListExtension *const list = DataList(geometry, kEnabledList);
    if (!list) return false;
    bool found = false;
    const int32_t count = static_cast<int32_t>(list->GetCount());
    for (int32_t i = 0; i < count; ++i) {
        const char *item = list->GetItemName(i);
        if (_stricmp(TextOf(subset), item) == 0) {
            found = true;
            break;
        }
    }
    VirtualBaseOf(list)->Release();
    return found;
}

// 0x3ab850
bool MaterialsMeshName(cIShape *shape, cIGZString *subset, cRZString *out)
{
    void *const geometry = GeometryOf(shape);
    if (!geometry) return false;
    cIDataListExtension *const list = DataList(geometry, kMeshNameList);
    if (!list) return false;
    const bool found = list->GetString(TextOf(subset), out->AsInterface());
    if (found && !reinterpret_cast<cRZStringHelpers *>(out)->EndsWith(kCres, 5, 1)) {
        cRZString suffix;
        suffix.Construct(kCres);
        RawSlot<kAppendSlot, uint32_t>(out, &suffix);
        suffix.Destruct();
    }
    VirtualBaseOf(list)->Release();
    return found;
}

// 0x3aab80
void GetObjectDesignModeNames(cICompositionTreeNode *node, cRZString *primary, cRZString *primaryMesh,
                              cRZString *secondary, cRZString *secondaryMesh)
{
    Empty(primary);
    Empty(primaryMesh);
    Empty(secondary);
    Empty(secondaryMesh);
    if (!node) return;
    ShapeList shapes;
    CollectChildShapes(node, &shapes);
    for (const fx::CountedRef<cIShape> &entry : shapes) {
        cIShape *const shape = entry.Get();
        void *const geometry = GeometryOf(shape);
        if (!geometry) continue;
        AnySubsetName(geometry, [&](cRZString &name) {
            if (!IsDesignModeSubset(shape, name.AsInterface()) || name.Find(kShadow, 0, true) != -1) return false;
            if (Strlen(primary) == 0) {
                primary->mString = name.mString;
                MaterialsMeshName(shape, name.AsInterface(), primaryMesh);
                return false;
            }
            if (Strlen(secondary) != 0 || RawSlot<kIsEqualRefSlot, bool>(primary, name.AsInterface(), true))
                return false;
            secondary->mString = name.mString;
            MaterialsMeshName(shape, name.AsInterface(), secondaryMesh);
            return true;
        });
    }
}

// 0x3aaec0
void GetPortalDesignModeNames(cIGZUnknown *object, cRZString *primary, cRZString *primaryMesh, cRZString *secondary,
                              cRZString *secondaryMesh)
{
    void *portal = nullptr;
    if (object && RawSlot<kComponentSlot, bool>(object, kPortalHolderIid, kPortalIid, &portal)) {
        GetObjectDesignModeNames(static_cast<cICompositionTreeNode *>(portal), primary, primaryMesh, secondary,
                                 secondaryMesh);
    } else {
        Empty(primary);
        Empty(primaryMesh);
        Empty(secondary);
        Empty(secondaryMesh);
    }
    if (portal) VirtualBaseOf(portal)->Release();
}

// 0x3ab990
cIShape *FindObjectDesignMode(cICompositionTreeNode *node, cIGZString *subset)
{
    if (!node) return nullptr;
    cIGZUnknown *shapeRef = nullptr;
    if (node->QueryInterface(kShapeRefIid, reinterpret_cast<void **>(&shapeRef)) && Strlen(subset) != 0) {
        const cShapeRange range = static_cast<cIShapeRefNode *>(shapeRef)->GetShapes();
        for (cIShape **at = range.mFirst; at != range.mEnd; ++at) {
            cIShape *const shape = *at;
            if (!shape) continue;
            if (HasSubsetNamed(GeometryOf(shape), subset)) {
                if (shapeRef) shapeRef->Release();
                return shape;
            }
        }
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        cIShape *const found = FindObjectDesignMode(child, subset);
        if (found) {
            { const fx::CountedRef<cIShape> held(found); }
            if (shapeRef) shapeRef->Release();
            return found;
        }
    }
    if (shapeRef) shapeRef->Release();
    return nullptr;
}

// 0x3ab2b0
cICompositionTreeNode *FindGeometryWithSubset(cICompositionTreeNode *node, cIGZString *subset)
{
    if (!node || Strlen(subset) == 0) return nullptr;
    cIGZUnknown *shapeRef = nullptr;
    if (node->QueryInterface(kShapeRefIid, reinterpret_cast<void **>(&shapeRef))) {
        const cShapeRange range = static_cast<cIShapeRefNode *>(shapeRef)->GetShapes();
        for (cIShape **at = range.mFirst; at != range.mEnd; ++at) {
            void *const geometry = GeometryOf(*at);
            if (HasSubsetNamed(geometry, subset)) {
                if (shapeRef) shapeRef->Release();
                return static_cast<cICompositionTreeNode *>(geometry);
            }
        }
    }
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling()) {
        cICompositionTreeNode *const found = FindGeometryWithSubset(child, subset);
        if (found) {
            if (shapeRef) shapeRef->Release();
            return found;
        }
    }
    if (shapeRef) shapeRef->Release();
    return nullptr;
}

namespace {

// 0x3ab570 / 0x3abc40: the first listed shape whose geometry has the subset; its geometry through `geometryOut`.
cIShape *FirstListedWithSubset(cIGZUnknown *shapes, cIGZString *subset, void **geometryOut)
{
    if (!shapes || Strlen(subset) == 0) return nullptr;
    const int32_t count = RawSlot<kListCountSlot, int32_t>(shapes);
    for (int32_t i = 0; i < count; ++i) {
        cIShape *const shape = RawSlot<kListItemSlot, cIShape *>(shapes, i);
        if (!shape) continue;
        void *const geometry = GeometryOf(shape);
        if (HasSubsetNamed(geometry, subset)) {
            *geometryOut = geometry;
            return shape;
        }
    }
    return nullptr;
}

} // namespace

// 0x3ab570
cICompositionTreeNode *FindListedGeometryWithSubset(cIGZUnknown *shapes, cIGZString *subset)
{
    void *geometry = nullptr;
    return FirstListedWithSubset(shapes, subset, &geometry) ? static_cast<cICompositionTreeNode *>(geometry) : nullptr;
}

// 0x3abc40
cIShape *FindListedShapeWithSubset(cIGZUnknown *shapes, cIGZString *subset)
{
    void *geometry = nullptr;
    return FirstListedWithSubset(shapes, subset, &geometry);
}

// ---- lane 261111p ----

namespace {

const char kSlaveList[] = "tsDesignModeSlaveSubsets";
const char kListSeparator[] = ",";
const char kLodFormat[] = "%sLOD%d";
constexpr int kCompareToSlot = 15;                                    // cIGZString CompareTo(other, caseSensitive)
constexpr uint32_t kOverrideClsid = 0xac6965bc, kOverrideIid = 0x6c69658c; // 0x3aa720's framework service
constexpr int kFrameworkServiceSlot = 5;                              // (clsid, iid, out)
constexpr int kByMeshSlot = 8, kByKeySlot = 9;                        // the service's candidate queries
constexpr int kCandidateNameSlot = 6, kCandidateUsableSlot = 7;       // a candidate's name (hidden return), its flag
constexpr int kLinkSlot = 61, kKeyHolderSlot = 27, kKeySlot = 28;     // 0x3abf70: object -> a -> b -> key
constexpr int kApplyMaterialSlot = 290, kHasMaterialSlot = 292;       // the object's
constexpr int kContainerNameSlot = 13;
constexpr int kBaseNameSlot = 124;                                    // 0x3ab7a0's object

// The service's candidate vector (begin, end, capacity), block from the allocator service.
struct CandidateVector {
    cIGZUnknown **mBegin = nullptr, **mEnd = nullptr, **mCapacity = nullptr;
};

// Releases every non-null candidate (begin and end read now) and frees the block through the allocator service;
// nothing for a null block.
void ReleaseCandidates(const CandidateVector &candidates)
{
    cIGZUnknown **const begin = candidates.mBegin;
    if (!begin) return;
    cIGZUnknown **const end = candidates.mEnd;
    for (cIGZUnknown **at = begin; at != end; ++at)
        if (*at) (*at)->Release();
    as2::FreeViaAllocatorService(begin, 0);
}

// 0x3abf70's step for one subset and its mesh name.
void ApplySubsetMaterial(cIGZUnknown *container, cIGZUnknown *object, void *keyHolder, cRZString *subset,
                         cRZString *mesh)
{
    if (subset->Strlen() == 0 || RawSlot<kHasMaterialSlot, uint32_t>(object, subset) != 0) return;
    cRZString copy;
    copy.Construct(*subset, 0, UINT32_MAX);
    cIShape *const shape = FindListedShapeWithSubset(container, copy.AsInterface());
    copy.Destruct();
    if (!shape) return;
    cRZString name;
    name.Construct(RawSlot<kContainerNameSlot, const char *>(container));
    cIGZUnknown *const material =
        FindMaterialOverride(RawSlot<kKeySlot, uint32_t>(keyHolder), mesh->AsInterface(), &name, subset);
    if (material) material->AddRef();
    name.Destruct();
    if (!material) return;
    RawSlot<kApplyMaterialSlot, uint32_t>(object, material);
    material->Release();
}

} // namespace

bool SubsetNameLess::operator()(const cRZString &left, const cRZString &right) const
{
    return RawSlot<kCompareToSlot, int32_t>(&left, right.AsInterface(), false) < 0;
}

// 0x3aafa0
void SlaveSubsets(void *geometry, const char *subset, SubsetList *out)
{
    if (!geometry || !subset || !*subset) return;
    cIDataListExtension *const list = DataList(geometry, kSlaveList);
    if (!list) return;
    cRZString text;
    text.Construct();
    if (list->GetString(subset, text.AsInterface())) {
        while (!text.mString.empty()) {
            cRZString piece;
            reinterpret_cast<cRZIniStringHelpers *>(&text)->SplitOff(&piece, kListSeparator);
            if (piece.mString.empty()) {
                // ORIGINAL-BUG: an empty piece (the text starts with ',', also after a doubled ',') takes the whole
                // remaining text, commas and all, as one name, instead of skipping the empty one.
                piece.mString.assign(text.mString.data(), text.mString.size());
                text.mString.clear();
            }
            out->push_back(piece);
            piece.Destruct();
        }
    }
    text.Destruct();
    VirtualBaseOf(list)->Release();
}

// 0x3ab1d0
void ListedSlaveSubsets(cIGZUnknown *shapes, const char *subset, SubsetList *out)
{
    if (!shapes || !subset) return;
    cRZString name;
    name.Construct(subset);
    cIGZUnknown *const geometry =
        reinterpret_cast<cIGZUnknown *>(FindListedGeometryWithSubset(shapes, name.AsInterface()));
    if (geometry) geometry->AddRef();
    name.Destruct();
    if (!geometry) return;
    SlaveSubsets(geometry, subset, out);
    geometry->Release();
}

// 0x3aa720
cIGZUnknown *FindMaterialOverride(uint32_t key, cIGZString *mesh, cRZString *containerName, cRZString *subset)
{
    RZAutoRef service{nullptr};
    if (void *const framework = reinterpret_cast<void *>(RZGetGlobal10C9378()))
        RawSlot<kFrameworkServiceSlot, bool>(framework, kOverrideClsid, kOverrideIid, &service.mObject);
    if (!service.mObject) {
        service.ReleaseObject();
        return nullptr;
    }
    CandidateVector candidates;
    if (Strlen(mesh) != 0) {
        const std::string subsetText(TextOf(subset));
        const std::string meshText(TextOf(mesh));
        RawSlot<kByMeshSlot, uint32_t>(service.mObject, &candidates, &meshText, &subsetText, 0);
    } else {
        const std::string container(containerName->mString);
        const std::string subsetText(subset->mString);
        const bool hasContainer = Strlen(containerName) != 0;
        const bool hasSubset = Strlen(subset) != 0;
        RawSlot<kByKeySlot, uint32_t>(service.mObject, &candidates, key, hasContainer ? &container : nullptr,
                                      hasSubset ? &subsetText : nullptr, 0);
    }
    cIGZUnknown **const end = candidates.mEnd;
    for (cIGZUnknown **at = candidates.mBegin; at != end; ++at) {
        cIGZUnknown *const candidate = *at;
        if (candidate) candidate->AddRef();
        as2::RawString name;
        const as2::RawString *const got = RawSlot<kCandidateNameSlot, const as2::RawString *>(candidate, &name);
        const char *const text = got->mCapacity > 15 ? got->mPtr : got->mBuf;
        const bool match =
            _stricmp(text, TextOf(subset)) == 0 && RawSlot<kCandidateUsableSlot, uint32_t>(candidate) != 0;
        if (name.mCapacity > 15) appdebugger::FreeStringBuffer(&name, name.mPtr, name.mCapacity);
        if (candidate) candidate->Release();
        if (match) {
            ReleaseCandidates(candidates);
            service.ReleaseObject();
            return candidate;
        }
    }
    ReleaseCandidates(candidates);
    service.ReleaseObject();
    return nullptr;
}

// 0x3abf70
void ApplyDesignModeMaterials(cIGZUnknown *container, cIGZUnknown *object)
{
    if (!container || !object) return;
    cRZString primary, secondary, primaryMesh, secondaryMesh;
    primary.Construct();
    secondary.Construct();
    primaryMesh.Construct();
    secondaryMesh.Construct();
    GetPortalDesignModeNames(object, &primary, &primaryMesh, &secondary, &secondaryMesh);
    if (void *const link = RawSlot<kLinkSlot, void *>(object)) {
        if (void *const keyHolder = RawSlot<kKeyHolderSlot, void *>(link)) {
            ApplySubsetMaterial(container, object, keyHolder, &primary, &primaryMesh);
            ApplySubsetMaterial(container, object, keyHolder, &secondary, &secondaryMesh);
        }
    }
    secondaryMesh.Destruct();
    primaryMesh.Destruct();
    secondary.Destruct();
    primary.Destruct();
}

// 0x3ab7a0
cRZString *FormatMaterialName(cRZString *out, cIGZUnknown *object, int32_t lod, bool cres)
{
    out->Construct();
    if (object) {
        out->Sprintf(kLodFormat, RawSlot<kBaseNameSlot, const char *>(object), lod);
        if (cres) out->mString.append(kCres, 5);
    }
    return out;
}

// 0x3aa420
void CollectSubsetNames(cICompositionTreeNode *node, SubsetNameSet *names)
{
    ShapeList shapes;
    CollectChildShapes(node, &shapes);
    for (const fx::CountedRef<cIShape> &entry : shapes) {
        cIGZUnknown *const geometry = static_cast<cIGZUnknown *>(GeometryOf(entry.Get()));
        if (!geometry) continue;
        geometry->AddRef();
        cNameRange range;
        RawSlot<kNamesSlot, cNameRange *>(geometry, &range);
        while (!RawSlot<kIsEqualSlot, bool>(range.mCurrent, range.mEnd)) {
            cRZString name;
            name.Construct(RawSlot<kGetSlot, const char *>(range.mCurrent));
            names->insert(name);
            name.Destruct();
            RawSlot<kNextSlot, uint32_t>(range.mCurrent);
        }
        if (range.mEnd) range.mEnd->Release();
        if (range.mMiddle) range.mMiddle->Release();
        if (range.mCurrent) range.mCurrent->Release();
        geometry->Release();
    }
}

} // namespace DesignMode
