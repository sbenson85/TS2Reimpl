// nGZSceneGraph::cGeometryNode as a whole class (lane QU). Layout, faces and the switch: src/sg/cGeometryNode.h.
// Not named cGeometryNode.cpp: one source name per target (the hook table is src/hook/tables/cGeometryNode.cpp).
#include "sg/SGEventRecords.h"
#include "sg/cGeometryNode.h"
#include "sg/cObjectGraphNodeImpl.h" // cObjectGraphBehaviorTraits

#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h" // GetCurrentDeviceCached
#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPersistResourceManager.h"
#include "gz/cGZPropertySet.h"  // gzany:: the value vtables
#include "gz/cIGZPropertySet.h" // GZServiceString, cIGZPropertySetString
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/SmallVirtuals.h" // cNullEvent
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject (the framework), ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StringKeyParser.h"
#include "sg/SGShapeGeometry.h"  // the slot bodies hooked before this class (lane history: SGShapeGeometry)
#include "sg/cGeometryBuilder.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cGeometryNodeIterators.h" // the subset iterators (slots 42 / 44)
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <algorithm>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>

// 0xa3dd80's hook target: our constructor's code (QU_cGeometryNode_Constructor, declared in cGeometryNode.h), as
// cSGResource's: a C name the linker resolves to the constructor's decorated one.
#pragma comment(linker, "/alternatename:_QU_cGeometryNode_Constructor=??0cGeometryNode@nGZSceneGraph@@QAE@XZ")

namespace nGZSceneGraph {

static_assert(sizeof(cGeometryNode) == cGeometryNode::kSize, "cGeometryNode size");
static_assert(offsetof(cGeometryNode, mSubsets) == 0x44, "cGeometryNode subsets");
static_assert(offsetof(cGeometryNode, mRenderInfo) == 0x4c, "cGeometryNode render info");
static_assert(offsetof(cGeometryNode, mElements) == 0x78, "cGeometryNode elements");
static_assert(offsetof(cGeometryNode, mRecords) == 0x84, "cGeometryNode records");
static_assert(offsetof(cGeometryNode, mBoundsInfo) == 0x90, "cGeometryNode bounds info");
static_assert(offsetof(cGeometryNode, mBoundsCount) == 0x98, "cGeometryNode bounds count");
static_assert(offsetof(cGeometryNode, mBoundsData) == 0x9c, "cGeometryNode bounds data");
static_assert(offsetof(cGeometryNode, mRecords24) == 0xa0, "cGeometryNode records 24");
static_assert(offsetof(cGeometryNode, mBounds) == 0xac, "cGeometryNode bounds");
static_assert(offsetof(cGeometryNode, mRefB8) == 0xb8, "cGeometryNode +0xb8");
static_assert(offsetof(cGeometryNode, mRefC4) == 0xc4, "cGeometryNode +0xc4");
static_assert(offsetof(cGeometryNode, mListeners) == 0xc8, "cGeometryNode listeners");
static_assert(offsetof(cGeometryNode, mParts) == 0xd4, "cGeometryNode parts");
static_assert(offsetof(cGeometryNode, mContainer) == 0xe0, "cGeometryNode container");
static_assert(offsetof(cGeometryNode, mKey) == 0xe4, "cGeometryNode key");
static_assert(offsetof(cGeometryNode, mRecordsF4) == 0xf4, "cGeometryNode +0xf4");
static_assert(offsetof(cGeometryNode, mRecords100) == 0x100, "cGeometryNode +0x100");
static_assert(offsetof(cGeometryNode, mNamesA) == 0x10c, "cGeometryNode names A");
static_assert(offsetof(cGeometryNode, mNamesB) == 0x118, "cGeometryNode names B");
static_assert(offsetof(cGeometryNode, mFlags) == 0x124, "cGeometryNode flags");
static_assert(offsetof(cGeometryNode, mDeviceReady) == 0x128, "cGeometryNode +0x128");
static_assert(offsetof(cGeometryNode, mResourcesReady) == 0x129, "cGeometryNode +0x129");
static_assert(offsetof(cGeometryNodeElement, mName) == 0x38, "element name");
static_assert(offsetof(cGeometryNodeElement, mIndices) == 0x40, "element indices");
static_assert(offsetof(cGeometryNodeElement, mRefs) == 0x54, "element references");

namespace {

constexpr uint32_t kGraphicSystemId = 0x85522803;  // the framework's graphic system service
constexpr uint32_t kGraphicSystemIid = 0xa55227bb;
constexpr uint32_t kResourceManagerService = 0x056b906e; // cIGZPersistResourceManager
constexpr uint32_t kIidResourceManager = 0x656b8efc;
constexpr uint32_t kSettingsService = 0x0ac1887e;  // the settings property set (string keys)
constexpr uint32_t kIidSettings = 0x4bb0d269;
constexpr uint32_t kContainerClass = 0xac4f8687;   // cGeometryDataContainer's class id
constexpr uint32_t kIidContainer = 0x5bafb147;     // cIGeometryDataContainer
constexpr uint32_t kIidBounds = 0x5ce57549;        // what SetBounds asks each object for (slot 3)
constexpr uint32_t kIidPart = 0xbbb79c90;          // the parts an old record lists (reader slot 24)
constexpr uint32_t kOldPartClass = 0x5ce7e026;     // the part a version-3 record is given when it has none
constexpr uint32_t kIidOldPart = 0x7ce7e022;

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

cIGZFrameWork *Framework() { return static_cast<cIGZFrameWork *>(GetServiceObject()); }

// The settings service's boolean value object (built inline by the original): the destructor stores the base's vtable.
struct AnyBooleanValue {
    const void *mVTable;
    bool mValue;
    AnyBooleanValue() : mVTable(gzany::BooleanVTable()), mValue(false) {}
    AnyBooleanValue(const AnyBooleanValue &) = delete;
    AnyBooleanValue &operator=(const AnyBooleanValue &) = delete;
    ~AnyBooleanValue() { *static_cast<const void *volatile *>(&mVTable) = gzany::AnyVTable(); }
};

// The resource manager (the framework's 0x056b906e), counted; released by the caller.
cIGZPersistResourceManager *ResourceManager(RZAutoRef &holder)
{
    holder.mObject = nullptr;
    if (cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(kResourceManagerService, kIidResourceManager, &holder.mObject);
    return static_cast<cIGZPersistResourceManager *>(holder.mObject);
}

// A range from its begin and end iterators: the cursor the begin's Clone (slot 3), then the begin and the end, counted.
cGeometrySubsetRange *FillRange(cGeometrySubsetRange *out, cIGeometrySubsetIterator *begin, cIGeometrySubsetIterator *end)
{
    // ORIGINAL-BUG: the begin iterator is used without a null check (its pool allocation failing).
    cIGZUnknown *const cursor = begin->Clone();
    out->mCursor = cursor;
    if (cursor) cursor->AddRef();
    out->mBegin = begin;
    begin->AddRef();
    out->mEnd = end;
    if (end) end->AddRef();
    return out;
}

// A subset name's handle: the string pool's cell for the text, empty (0xffffffff) for a null or empty text (0xaed300).
void OpenNameCell(ServiceCell &cell, const char *name)
{
    if (name && *name)
        cell.Open(Word(name), 0);
    else
        cell.mValue = 0xffffffff;
}

} // namespace

// ---- the behaviour template's registry (0xa42f30) ---------------------------------------------------------------------

namespace {

std::optional<nGZEvent::cEventAttributeMap> gGeometryNodeRegistry;

// 0x264010: the object-graph nodes' registry, a non-zero `id` storing *attribute in it first.
nGZEvent::cEventAttributeMap *ObjectGraphRegistry(uint32_t id, const nGZEvent::cEventAttribute *attribute)
{
    return cObjectGraphBehaviorTraits::RegistryFor(id, attribute);
}

// An event's record as the game's data holds it (owned in sg/SGEventRecords.h).
nGZEvent::cEventAttribute GameAttribute(const nGZEvent::cEventAttribute &record) { return record; }

// The 20-byte records: the last word is 4.
nGZEvent::cEventAttribute GameAttribute20(const nGZEvent::cEventAttribute &record)
{
    nGZEvent::cEventAttribute attribute = GameAttribute(record);
    attribute.mWord14 = 4;
    return attribute;
}

} // namespace

nGZEvent::cEventAttributeMap *cGeometryNodeBehaviorTraits::Registry()
{
    nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

// Built on first use. The original's leaked holder (0xa3cf50) runs first: the base events, then the object-graph node's two
// events stored in its registry (0x2570a0), then this map made a copy of that registry, then this class's four events.
nGZEvent::cEventAttributeMap *__cdecl cGeometryNodeBehaviorTraits::RegistryFor(uint32_t id,
                                                                             const nGZEvent::cEventAttribute *attribute)
{
    if (!gGeometryNodeRegistry) {
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        ObjectGraphRegistry(0x7b54c99c, &sgevents::kGraphConnChanged);
        ObjectGraphRegistry(0x1d05597b, &sgevents::kSubordinateListChanged);
        const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        gGeometryNodeRegistry.emplace(*ObjectGraphRegistry(0, &defaults));
        nGZEvent::cEventAttributeMap &map = *gGeometryNodeRegistry;
        map[0x9bb24282] = GameAttribute(sgevents::kGeometryNodeUpdated);
        map[0x2a7e3a48] = GameAttribute(sgevents::kHandleGeometryNodeUpdated);
        map[0xbbb112f6] = GameAttribute20(sgevents::kBuildGeometry);
        map[0xca7ccbbc] = GameAttribute20(sgevents::kCurrentDeviceEvent);
    }
    if (id != 0) (*gGeometryNodeRegistry)[id] = *attribute;
    return &*gGeometryNodeRegistry;
}

void cGeometryNodeBehaviorTraits::ResetRegistryForTest() { gGeometryNodeRegistry.reset(); }

// ---- construction -----------------------------------------------------------------------------------------------

// 0xa3dd80: the node (0xa3dd10) and the cSGResource part, every member empty (the map's head node allocated), then
// Reset and ReleaseElementResources, then connected to the graphic system's device event (OnEvent: OnDeviceEvent).
cGeometryNode::cGeometryNode()
{
    cGeometryNode::Reset();
    cGeometryNode::ReleaseElementResources();
    RZAutoRef graphics{nullptr};
    if (cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(kGraphicSystemId, kGraphicSystemIid, &graphics.mObject);
    // ORIGINAL-BUG: the graphic system is used without a null check.
    static_cast<nGZEvent::cIAbstractBehavior *>(graphics.mObject)
        ->Connect(kDeviceEvent, static_cast<cIGeometryNode *>(this), kDeviceEvent);
    graphics.ReleaseObject();
}

// 0xa3e320: the parts told they are no longer listened to and dropped; then the members (in reverse), the cSGResource part
// and the node (0xa3e120: the extensions removed).
cGeometryNode::~cGeometryNode() { cGeometryNode::ClearParts(); }

// (The original's operator new throws on failure, so its null branch is dead.)
cGeometryNode *__cdecl cGeometryNode::Create() { return new cGeometryNode; }

// ---- the built state ---------------------------------------------------------------------------------------------

void cGeometryNode::Reset()
{
    mSubsets.clear();
    std::fill(std::begin(mRenderInfo), std::end(mRenderInfo), 0u);
    mElements.clear();
    mRecords.clear();
    mRecordsF4.clear();
    mRecords100.clear();
    mNamesA.clear();
    mNamesB.clear();
    mBoundsInfo = 0;
    mBoundsCount = 0;
    mWord94 = 0;
    mBoundsData = 0;
    mRecords24.clear();
    mBounds.clear();
    mRefB8.Reset();
    mRefBC.Reset();
    mRefC0.Reset();
}

void cGeometryNode::ReleaseElementResources()
{
    mRefC4.Reset();
    mResourcesReady = 0;
    for (auto it = mElements.begin(); it != mElements.end(); ++it) {
        it->mPrepared = 0;
        it->mRefs[2].Reset();
        it->mRefs[3].Reset();
        it->mRefs[4].Reset();
        it->mRefs[5].Reset();
    }
}

// 0xa42650: `data` is the graphic system's device record (+4 the state: 1 when the device is back). With a device, the
// device objects are dropped (another order than ReleaseElementResources'), part 6's device state reset (0xa419e0) and
// the geometry rebuilt (slot 34).
void cGeometryNode::OnDeviceEvent(void *data)
{
    if (static_cast<const uint32_t *>(data)[1] != 1 || !nGZGraphic4::GetCurrentDeviceCached()) return;
    mDeviceReady = 0;
    mRefBC.Reset();
    mRefC4.Reset();
    for (auto it = mElements.begin(); it != mElements.end(); ++it) {
        it->mRefs[2].Reset();
        it->mRefs[4].Reset();
        it->mRefs[3].Reset();
        it->mRefs[5].Reset();
    }
    ResetDeviceState(false, false);
    Rebuild();
}

// ---- cIGZUnknown --------------------------------------------------------------------------------------------------

// 0xa42b20: a null out answers false. cIGeometryNode gives the node, the render id the render face (both counted), a
// third id the node uncounted; the rest is the node template's (0x3a5ae0), then the cSGResource part's.
bool cGeometryNode::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIGeometryNode::kIid) {
        *out = static_cast<cIGeometryNode *>(this);
        AddRef();
        return true;
    }
    if (iid == kIidRender) {
        *out = static_cast<cIGeometryRender *>(this);
        AddRef();
        return true;
    }
    if (iid == kIidUncounted) {
        *out = static_cast<cIGeometryNode *>(this);
        return true;
    }
    if (Node::QueryInterface(iid, out)) return true;
    return cSGResource::QueryInterface(iid, out);
}

uint32_t cGeometryNode::AddRef() { return Node::AddRef(); }
uint32_t cGeometryNode::Release() { return Node::Release(); }

// ---- the behaviour --------------------------------------------------------------------------------------------------

// 0xa42760: the template's (0xa42720), then the rebuild event rebuilds (slot 34) and the device event goes to
// OnDeviceEvent.
void cGeometryNode::OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3)
{
    Behavior::OnEvent(type, arg1, data, arg3);
    if (type == kRebuildEvent)
        Rebuild();
    else if (type == kDeviceEvent)
        cGeometryNode::OnDeviceEvent(data);
}

// 0xa43410: each listener's OnEvent (this node as the last argument; the end re-read after each call), then the node
// template's notify (0xa433e0: a no-op, then the behaviour's 0xa432f0).
void cGeometryNode::NotifyListeners(uint32_t type, uint32_t arg1, void *data)
{
    for (auto it = mListeners.begin(); it != mListeners.end(); ++it)
        (*it)->OnEvent(type, arg1, data, Word(static_cast<cIGeometryNode *>(this)));
    // ORIGINAL-BUG: the template's notify is passed 0, not arg1.
    Behavior::NotifyListeners(type, 0, data);
}

// 0xa42510: every listener is asked (no short cut); true when the behaviour's own answer (0xa423f0) is true and every
// listener said yes, and there was at least one listener.
bool cGeometryNode::AskListeners(uint32_t type, uint32_t arg1, void *data)
{
    bool any = false;
    bool all = true;
    for (auto it = mListeners.begin(); it != mListeners.end(); ++it) {
        const bool answer = (*it)->OnQuery(type, arg1, data);
        all = answer && all;
        any = true;
    }
    return Behavior::AskListeners(type, arg1, data) && all && any;
}

// ---- cIObjectGraphNode ------------------------------------------------------------------------------------------------

void cGeometryNode::Clone(uint32_t, uint32_t, void **, cINodeCloneContext *) {}

bool cGeometryNode::IsKindOf(uint32_t iid) { return iid == cIGeometryNode::kIid || mImpl.IsKindOf(iid); }

// ---- cIGeometryNode -------------------------------------------------------------------------------------------------

cIGeometryRender *cGeometryNode::GetRenderConst() { return this; }
cIGeometryRender *cGeometryNode::GetRender() { return this; }

void cGeometryNode::SetBuildFlags(uint32_t flags) { reinterpret_cast<cGeometryNodeObject *>(this)->SetField124(flags); }

// 0xa3e790: kept (counted), then told this node listens to it.
void cGeometryNode::AddPart(cIGeometryBuilder *part)
{
    {
        // The original's counted copy, dropped before the call below; the list copies on growth (cGeometryNodeCopiedRef).
        const cGeometryNodeCopiedRef<cIGeometryBuilder> held(part);
        mParts.push_back(held);
    }
    // ORIGINAL-BUG: the part is used without a null check.
    part->SetListener(static_cast<cIGeometryNode *>(this));
}

// 0xa430c0: each part told nobody listens (the end re-read after each call), then all dropped.
void cGeometryNode::ClearParts()
{
    for (auto it = mParts.begin(); it != mParts.end(); ++it)
        if (it->mPtr) it->mPtr->SetListener(nullptr);
    mParts.clear();
}

// 0xa3ebe0: with a container (loaded or built), the build (0xa3f180, part 5) over it, the container dropped again when it
// can be made again; then the listeners told (kBuiltEvent) and asked (kBuiltQuery) with an empty event.
void cGeometryNode::Rebuild()
{
    cGeometryNode::EnsureContainer();
    if (!mContainer.mPtr) return;
    cGeometryNode::BuildGeometry(mContainer.mPtr);
    cGeometryNode::ReleaseContainerIfKeyed();
    nGZEvent::cNullEvent event{nGZEvent::NullEventVTable()};
    NotifyListeners(kBuiltEvent, 0, &event);
    *static_cast<const void *volatile *>(&event.mVTable) = nGZEvent::NullEventVTable();
    AskListeners(kBuiltQuery, 0, &event);
}

uint8_t cGeometryNode::FindPart(uint32_t iid, void **out)
{
    return reinterpret_cast<cGeometryNodeObject *>(this)->AnyElementAccepts(iid, Word(out));
}

void cGeometryNode::AddListener(nGZEvent::cIAbstractBehavior *listener) { mListeners.push_back(listener); }

// 0xa43160: the first match removed (the rest moved down).
void cGeometryNode::RemoveListener(nGZEvent::cIAbstractBehavior *listener)
{
    const auto it = std::find(mListeners.begin(), mListeners.end(), listener);
    // ORIGINAL-BUG: the original removes "the element at the end" for a listener it does not hold: a move of -1 elements
    // (memmove with a huge length) and the end stepped back. Not reproduced: an absent listener changes nothing.
    if (it == mListeners.end()) return;
    mListeners.erase(it);
}

void cGeometryNode::SetContainer(cIGeometryDataContainer *container)
{
    reinterpret_cast<cGeometryNodeObject *>(this)->ReplaceRefE0(container);
}

// 0xa42600: a null out answers false; otherwise the container (loaded or built), counted, then dropped from the node if it
// can be made again.
bool cGeometryNode::GetContainer(cIGeometryDataContainer **out)
{
    if (!out) return false;
    cGeometryNode::EnsureContainer();
    cIGeometryDataContainer *const container = mContainer.mPtr;
    *out = container;
    // ORIGINAL-BUG: the container is used without a null check (no key and no parts).
    container->AddRef();
    cGeometryNode::ReleaseContainerIfKeyed();
    return true;
}

// 0xa43470: nothing for no objects; else the list sized to `count` (released or null-filled), each non-null object asked
// for kIidBounds into its slot through its slot 3 (not QueryInterface), and the count and array kept in +0x98 / +0x9c.
void cGeometryNode::SetBounds(uint32_t count, cIGZUnknown *const *objects)
{
    if (!count || !objects) return;
    mBounds.resize(count);
    mBoundsCount = static_cast<uint32_t>(mBounds.size());
    for (uint32_t i = 0; i < count; ++i) {
        if (!objects[i]) continue;
        mBounds[i].Reset();
        // Slot 3 of what the caller hands (cIGeometryBoundsSource), not QueryInterface.
        static_cast<cIGeometryBoundsSource *>(objects[i])->Find(kIidBounds, reinterpret_cast<void **>(&mBounds[i].mPtr));
    }
    mBoundsData = Word(mBounds.data());
}

uint32_t cGeometryNode::GetBuildFlags() { return mFlags; }

// 0xa436a0: an iterator over the subset names (the map's end, then its first node).
cGeometrySubsetRange *cGeometryNode::GetSubsetNames(cGeometrySubsetRange *out)
{
    cIGeometrySubsetIterator *const end = new cGeometrySubsetNameIterator(&mSubsets, mSubsets.end());
    cIGeometrySubsetIterator *const begin = new cGeometrySubsetNameIterator(&mSubsets, mSubsets.begin());
    return FillRange(out, begin, end);
}

// 0xa428d0: how many parts the subset named `name` has.
uint32_t cGeometryNode::CountSubset(const char *name)
{
    ServiceCell cell;
    OpenNameCell(cell, name);
    const auto range = mSubsets.equal_range(cell.mValue);
    const uint32_t count = static_cast<uint32_t>(std::distance(range.first, range.second));
    cell.Close();
    return count;
}

// 0xa43770: an iterator over the parts of the subset named `name`.
cGeometrySubsetRange *cGeometryNode::GetSubsetParts(cGeometrySubsetRange *out, const char *name)
{
    ServiceCell cell;
    OpenNameCell(cell, name);
    const auto range = mSubsets.equal_range(cell.mValue);
    cIGeometrySubsetIterator *const end = new cSubsetGeomPartIndexIterator(range.second);
    cIGeometrySubsetIterator *const begin = new cSubsetGeomPartIndexIterator(range.first);
    FillRange(out, begin, end);
    cell.Close();
    return out;
}

uint32_t cGeometryNode::GetElementName(uint32_t index)
{
    return reinterpret_cast<cGeometryNodeObject *>(this)->Dispatch8OfElement(index);
}
uint32_t cGeometryNode::GetElementNameHandle(uint32_t index)
{
    return reinterpret_cast<cGeometryNodeObject *>(this)->ElementWordAt38(index);
}
uint32_t cGeometryNode::GetNameA(uint32_t index)
{
    return reinterpret_cast<cGeometryNodeObject *>(this)->Dispatch8OfWordAt10c(index);
}
uint32_t cGeometryNode::GetNameB(uint32_t index)
{
    return reinterpret_cast<cGeometryNodeObject *>(this)->Dispatch8OfWordAt118(index);
}
void *cGeometryNode::GetBoundsInfo() { return &mBoundsInfo; }

// ---- the container ------------------------------------------------------------------------------------------------

// 0xa3ec70: nothing when there is one. A key loads it through the resource manager (slot 3); otherwise, with parts, a new
// container is made (the director's class object) and the parts build into it.
void cGeometryNode::EnsureContainer()
{
    if (mContainer.mPtr) return;
    if (mKey.type != 0) {
        RZAutoRef holder;
        cIGZPersistResourceManager *const manager = ResourceManager(holder);
        mContainer.Reset();
        // ORIGINAL-BUG: the resource manager is used without a null check.
        manager->vf3(Word(&mKey), kIidContainer, Word(&mContainer.mPtr), 0, 0);
        holder.ReleaseObject();
    } else if (!mParts.empty()) {
        cIGZCOM *const director = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
        mContainer.Reset();
        director->GetClassObject(kContainerClass, kIidContainer, reinterpret_cast<void **>(&mContainer.mPtr));
        cGeometryNode::BuildParts(mContainer.mPtr);
    }
}

// 0xa42360: a container that a key or the parts can make again is dropped (cleared, then released).
void cGeometryNode::ReleaseContainerIfKeyed()
{
    if ((mKey.type != 0 || !mParts.empty()) && mContainer.mPtr) mContainer.Reset();
}

// The "geomCheckGeomDataIntegrity" setting (false without the service's answer).
bool cGeometryNode::CheckIntegritySetting() { return ReadBooleanSetting("geomCheckGeomDataIntegrity"); }

// A boolean setting by name (false without the service's answer): the service, a boolean value, the key, the read.
bool cGeometryNode::ReadBooleanSetting(const char *name)
{
    ScopedRef settings{nullptr};
    if (cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(kSettingsService, kIidSettings, &settings.mObject);
    AnyBooleanValue value;
    {
        GZServiceString key(name);
        // ORIGINAL-BUG: the settings service is used without a null check.
        static_cast<cIGZPropertySetString *>(settings.mObject)->GetPropertyValue(&key, &value);
    }
    const bool check = value.mValue;
    settings.ReleaseObject();
    return check;
}

// 0xa431e0: every part builds into the container (the end re-read after each), the container checked after each when
// the setting asks for it (its slot 55).
void cGeometryNode::BuildParts(cIGeometryDataContainer *container)
{
    const bool check = CheckIntegritySetting();
    for (auto it = mParts.begin(); it != mParts.end(); ++it) {
        // ORIGINAL-BUG: a null part is used without a check.
        it->mPtr->Build(container);
        if (check) container->Slot55();
    }
}

// ---- cIGZPersistSerializableReferent --------------------------------------------------------------------------------

// 0xa42bb0 (versions 1-12). The node, then the resource. From 7 a byte for the integrity flag; 11 skips 2 bytes, 6 three
// unused bools. Below 9 the parts are listed (each added, slot 32); below 4 a node without a kIidOldPart part gets a new
// one. From 5 (8 always, 5-7 when the resource has a name and a segment) the container follows (counted reference, key
// kept, checked when the setting says so), the node rebuilds and drops its parts; otherwise it just rebuilds.
void cGeometryNode::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    const uint32_t version = info.mVersion;
    Node::Read(reader);
    cSGResource::Read(reader);
    if (version > 6) mFlags |= reader->GetBool() ? kFlagCheckIntegrity : 0;
    if (version == 11) RawSlot<23>(reader, 2u);
    if (version == 6) {
        reader->GetBool();
        reader->GetBool();
        reader->GetBool();
    }
    if (version < 9) {
        for (uint32_t n = reader->GetUint32(); n != 0; --n) {
            cIGeometryBuilder *part = nullptr;
            RawSlot<24>(reader, kIidPart, &part);
            AddPart(part);
            if (part) part->Release();
        }
    }
    if (version < 4) {
        cGeometryBuilderV *part = nullptr;
        if (!FindPart(kIidOldPart, reinterpret_cast<void **>(&part))) {
            cIGZCOM *const director = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
            if (cGeometryBuilderV *const old = part) {
                part = nullptr;
                static_cast<cIGeometryBuilder *>(old)->Release();
            }
            director->GetClassObject(kOldPartClass, kIidOldPart, reinterpret_cast<void **>(&part));
            AddPart(part ? static_cast<cIGeometryBuilder *>(part) : nullptr);
        }
        if (part) static_cast<cIGeometryBuilder *>(part)->Release();
    }
    bool readContainer = false;
    if (version > 7) {
        readContainer = true;
    } else if (version > 4) {
        if (GetName() && *GetName() && GetSegment()) readContainer = true;
    }
    if (readContainer) {
        if (version < 10) RawSlot<23>(reader, 0xcu);
        mContainer.Reset();
        reader->GetObjectRef(kIidContainer, &mContainer.mPtr);
        cGZPersistResourceKey key;
        // ORIGINAL-BUG: the container is used without a null check.
        mKey = *mContainer.mPtr->GetResource()->CopyKeyTo(&key);
        if (CheckIntegritySetting()) mContainer.mPtr->Slot55();
        Rebuild();
        ClearParts();
    } else {
        Rebuild();
    }
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cGeometryNode::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cGeometryNode");
    info.mWord4 = kTypeId;
    info.mVersion = kVersion;
    return info;
}

// 0xa43950: the type info, the node, the resource, the integrity flag byte, then the container as a flagged object. A
// node without a key builds its container if it has none, names its resource after this one (the name's own record suffix
// replaced by the container's: "x_gmnd" -> "x_gmdc"), drops a cached resource of that key from the resource manager and
// submits the container under the name and this node's segment. A keyed node without a container loads it. The key is
// then the container's, and the container's resource flagged.
void cGeometryNode::Write(ifaces::cIGZPersistWriter *writer)
{
    {
        cPersistTypeInfo info = cGeometryNode::GetTypeInfo();
        writer->WriteTypeInfo(&info);
        if (info.mName) info.mName->Release();
    }
    Node::Write(writer);
    cSGResource::WriteResource(writer);
    writer->SetUint8((mFlags >> 1) & 1);

    if (mKey.type == 0) {
        if (!mContainer.mPtr) {
            cIGZCOM *const director = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
            mContainer.Reset();
            director->GetClassObject(kContainerClass, kIidContainer, reinterpret_cast<void **>(&mContainer.mPtr));
            cGeometryNode::BuildParts(mContainer.mPtr);
        }
        std::string name(static_cast<const char *>(GetName()));
        const size_t suffix = std::strlen(static_cast<const char *>(GetRecord18()));
        if (suffix < name.size())
            name = name.substr(0, name.size() - std::strlen(static_cast<const char *>(GetRecord18())));
        // ORIGINAL-BUG: the container is used without a null check (no parts to build it from).
        cISGResource *const resource = mContainer.mPtr->GetResource();
        name += static_cast<const char *>(resource->GetRecord18());

        RZAutoRef holder;
        cIGZPersistResourceManager *const manager = ResourceManager(holder);
        StringKeyParser::Result key{};
        {
            cPersistTypeInfo type = mContainer.mPtr->GetResource()->GetTypeInfo();
            StringKeyParser::ParseKey(&key, name.c_str(), type.mWord4, nullptr);
            if (type.mName) type.mName->Release();
        }
        // ORIGINAL-BUG: the resource manager is used without a null check.
        if (manager->vf11(Word(&key))) manager->vf10(Word(&key));
        cISGResource *const target = mContainer.mPtr->GetResource();
        cIGZUnknown *const segment = GetSegment();
        target->AssignAndSubmit(name.c_str(), segment);
        holder.ReleaseObject();
    } else if (!mContainer.mPtr) {
        RZAutoRef holder;
        cIGZPersistResourceManager *const manager = ResourceManager(holder);
        mContainer.Reset();
        // ORIGINAL-BUG: the resource manager is used without a null check.
        manager->vf3(Word(&mKey), kIidContainer, Word(&mContainer.mPtr), 0, 0);
        holder.ReleaseObject();
    }

    cGZPersistResourceKey key;
    mKey = *mContainer.mPtr->GetResource()->CopyKeyTo(&key);
    mContainer.mPtr->GetResource()->SetFlag(1);
    writer->WriteFlaggedObject(mContainer.mPtr);
}

uint32_t cGeometryNode::GetOwnerInfo() { return cSGResource::GetOwnerInfo(); }

uint32_t cGeometryNode::GetRefCount() { return this->Node::mRefCount & nGZEvent::behavior_mt::kCountMask; }

const void *cGeometryNode::GetRecord18() { return "_gmnd"; }
const void *cGeometryNode::GetRecord19() { return "5gn"; }

// ---- cIGeometryRender -----------------------------------------------------------------------------------------------

namespace {
void *RenderFace(cGeometryNode *self) { return static_cast<cIGeometryRender *>(self); }
} // namespace

// IsSupported, Prepare and Render: cGeometryNodeRender.cpp.

uint32_t *cGeometryNode::GetFlags() { return &mRenderInfo[0]; }

uint32_t *cGeometryNode::GetDrawInfo(uint32_t index)
{
    return static_cast<uint32_t *>(reinterpret_cast<cGeometryNodeRenderSubobject *>(RenderFace(this))->ElementPointer(index));
}

void cGeometryNode::Begin(uint32_t index, cGeometryRenderContext *context, uint32_t first)
{
    reinterpret_cast<cGeometryNodeRenderSubobject *>(RenderFace(this))->ForwardElement(index, context, first);
}

// ---- the adapters -------------------------------------------------------------------------------------------------

void __fastcall cGeometryNodeAdapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->cGeometryNode::~cGeometryNode();
}
void __fastcall cGeometryNodeAdapters::Reset(Self *self, void *) { [[msvc::forceinline_calls]] self->cGeometryNode::Reset(); }
void __fastcall cGeometryNodeAdapters::ReleaseElementResources(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->cGeometryNode::ReleaseElementResources();
}
bool __fastcall cGeometryNodeAdapters::QueryInterface(Self *self, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::QueryInterface(iid, out);
}
uint32_t __fastcall cGeometryNodeAdapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::Release();
}
void __fastcall cGeometryNodeAdapters::OnEvent(Self *self, void *, uint32_t type, uint32_t arg1, void *data, uint32_t arg3)
{
    [[msvc::forceinline_calls]] self->cGeometryNode::OnEvent(type, arg1, data, arg3);
}
void __fastcall cGeometryNodeAdapters::NotifyListeners(Self *self, void *, uint32_t type, uint32_t arg1, void *data)
{
    [[msvc::forceinline_calls]] self->cGeometryNode::NotifyListeners(type, arg1, data);
}
bool __fastcall cGeometryNodeAdapters::AskListeners(Self *self, void *, uint32_t type, uint32_t arg1, void *data)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::AskListeners(type, arg1, data);
}
void __fastcall cGeometryNodeAdapters::Connect(Self *self, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    [[msvc::forceinline_calls]] self->Self::Behavior::Connect(event, target, targetEvent);
}
void __fastcall cGeometryNodeAdapters::Disconnect(Self *self, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    [[msvc::forceinline_calls]] self->Self::Behavior::Disconnect(event, target, targetEvent);
}
uint32_t __fastcall cGeometryNodeAdapters::GetEventCount(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Behavior::GetEventCount();
}
nGZEvent::cAttributeIDRange *__fastcall cGeometryNodeAdapters::GetEventIds(Self *self, void *, nGZEvent::cAttributeIDRange *out)
{
    [[msvc::forceinline_calls]] *out = self->Self::Behavior::GetEventIds();
    return out;
}
nGZEvent::cEventAttribute *__fastcall cGeometryNodeAdapters::GetEventAttribute(Self *self, void *, nGZEvent::cEventAttribute *out,
                                                                              uint32_t event)
{
    [[msvc::forceinline_calls]] *out = self->Self::Behavior::GetEventAttribute(event);
    return out;
}
bool __fastcall cGeometryNodeAdapters::IsKindOf(Self *self, void *, uint32_t iid)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::IsKindOf(iid);
}
cIGeometryRender *__fastcall cGeometryNodeAdapters::GetRender(Self *self, void *)
{
    // The original's null-safe conversion (a null node gives a null face).
    return self ? static_cast<cIGeometryRender *>(self) : nullptr;
}
void __fastcall cGeometryNodeAdapters::AddPart(Self *self, void *, cIGeometryBuilder *part)
{
    [[msvc::forceinline_calls]] self->cGeometryNode::AddPart(part);
}
void __fastcall cGeometryNodeAdapters::ClearParts(Self *self, void *) { [[msvc::forceinline_calls]] self->cGeometryNode::ClearParts(); }
void __fastcall cGeometryNodeAdapters::Rebuild(Self *self, void *) { [[msvc::forceinline_calls]] self->cGeometryNode::Rebuild(); }
void __fastcall cGeometryNodeAdapters::AddListener(Self *self, void *, nGZEvent::cIAbstractBehavior *listener)
{
    [[msvc::forceinline_calls]] self->cGeometryNode::AddListener(listener);
}
void __fastcall cGeometryNodeAdapters::RemoveListener(Self *self, void *, nGZEvent::cIAbstractBehavior *listener)
{
    [[msvc::forceinline_calls]] self->cGeometryNode::RemoveListener(listener);
}
bool __fastcall cGeometryNodeAdapters::GetContainer(Self *self, void *, cIGeometryDataContainer **out)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::GetContainer(out);
}
void __fastcall cGeometryNodeAdapters::SetBounds(Self *self, void *, uint32_t count, cIGZUnknown *const *objects)
{
    [[msvc::forceinline_calls]] self->cGeometryNode::SetBounds(count, objects);
}
cGeometrySubsetRange *__fastcall cGeometryNodeAdapters::GetSubsetNames(Self *self, void *, cGeometrySubsetRange *out)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::GetSubsetNames(out);
}
uint32_t __fastcall cGeometryNodeAdapters::CountSubset(Self *self, void *, const char *name)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::CountSubset(name);
}
cGeometrySubsetRange *__fastcall cGeometryNodeAdapters::GetSubsetParts(Self *self, void *, cGeometrySubsetRange *out,
                                                                       const char *name)
{
    [[msvc::forceinline_calls]] return self->cGeometryNode::GetSubsetParts(out, name);
}
void __fastcall cGeometryNodeAdapters::Read(void *face, void *, ifaces::cIGZPersistReader *reader)
{
    [[msvc::forceinline_calls]] FromFace(face, kPersistOffset)->cGeometryNode::Read(reader);
}
cPersistTypeInfo *__fastcall cGeometryNodeAdapters::GetTypeInfo(void *face, void *, cPersistTypeInfo *out)
{
    [[msvc::forceinline_calls]] *out = FromFace(face, kPersistOffset)->cGeometryNode::GetTypeInfo();
    return out;
}
void __fastcall cGeometryNodeAdapters::Write(void *face, void *, ifaces::cIGZPersistWriter *writer)
{
    [[msvc::forceinline_calls]] FromFace(face, kPersistOffset)->cGeometryNode::Write(writer);
}
uint32_t __fastcall cGeometryNodeAdapters::GetRefCount(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromFace(face, kResourceOffset)->cGeometryNode::GetRefCount();
}

} // namespace nGZSceneGraph
