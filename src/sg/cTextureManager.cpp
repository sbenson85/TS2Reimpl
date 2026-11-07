// nGZSceneGraph::cTextureManager (lanes SA and SE, M4a; see sg/cTextureManager.h).
#include "sg/cTextureManager.h"

#include "game/game.h"
#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h" // GetCurrentDeviceCached
#include "gz/TextureSamplerCapsDX.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFixedAllocator.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/StringKeyParser.h"
#include "sg/cMaterialAttributes.h" // OpenNameCell (0xae87d0)
#include "sg/cStringPool.h"
#include "sg/cTextureUpdateStrategy.h"

#include <new>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f8bd4); // cTextureHandleIterator's pool (0xadd550)

namespace nGZSceneGraph {

namespace {

using TextureCall = void(__thiscall *)(void *);

std::optional<nGZEvent::cEventAttributeMap> gRegistry;
// The class's one event. Its record ends in 4 (the code stores 4 over the image record's 8).
const nGZEvent::cEventAttribute kDeviceEventRecord{0xca7ccbbc, "CurrentDeviceEvent", 1, 0x3b6c3e0a, 2, 4};

constexpr uint32_t kNoName = 0xffffffffu;
constexpr uint32_t kDeviceBack = 1;                 // the device event's code when the device is ready again
constexpr uint32_t kResourceService = 0x056b906e;   // the resource service (the framework's)
constexpr uint32_t kResourceIid = 0x656b8efc;
constexpr uint32_t kTextureType = 0x1c4a276c;       // the "_txtr" resources' type
constexpr uint32_t kTextureResourceIid = 0xfc49f6ee; // what the resource service is asked for
constexpr uint32_t kEngineGroup = 0x1c0532fa;       // the group the game's own resources are in
constexpr uint32_t kBlankTextureClass = 0x6b81dfe9; // the director's texture classes
constexpr uint32_t kBlankTextureIid = 0x4b81dfe2;
constexpr uint32_t kImageTextureClass = 0x2a956dad;
constexpr uint32_t kCubeTextureClass = 0x4a956dd2;  // for a resource of type 6
constexpr uint32_t kImageTextureIid = 0x2a956da8;
constexpr uint32_t kIteratorVTable = 0xe7c7f4;      // cTextureHandleIterator

cIStringPool *Pool() { return static_cast<cIStringPool *>(GetCachedObjectB()); }

ifaces::cIGZFrameWork *Framework() { return static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()); }

// A director class object (slot 3 of the COM object: class, interface, out).
void CreateObject(uint32_t classId, uint32_t iid, cIGZUnknown **out)
{
    RawSlot<3>(GetDirectorFrameworkObject(), classId, iid, out);
}

// The texture update strategies (ours, src/sg/cTextureUpdateStrategy.h; count 0): the plain one (0xaebd30 in the
// original) and the missing-texture one of a name (0xadd030).
cIGZUnknown *NewUpdateStrategy() { return new nRZSceneGraph::cTextureUpdateStrategy; }

cIGZUnknown *NewMissingStrategy(const char *name) { return new cMissingTextureUpdateStrategy(name); }

void ReleaseIf(cIGZUnknown *object)
{
    if (object) object->Release();
}

} // namespace

// ---- the behaviour registry ----------------------------------------------------------------------------------------

nGZEvent::cEventAttributeMap *cTextureManagerBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

// 0xade400 (and its first-use build 0xadce80).
nGZEvent::cEventAttributeMap *__cdecl cTextureManagerBehaviorTraits::RegistryFor(uint32_t id,
                                                                                 const nGZEvent::cEventAttribute *attribute)
{
    // Built on first use: the base events stored, a copy of the base registry, then the device event.
    if (!gRegistry) {
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        gRegistry.emplace(*nGZEvent::cAbstractBehaviorType::Registry());
        (*gRegistry)[kDeviceEventRecord.mId] = kDeviceEventRecord;
    }
    if (id != 0) (*gRegistry)[id] = *attribute;
    return &*gRegistry;
}

void cTextureManagerBehaviorTraits::ResetRegistryForTest() { gRegistry.reset(); }

// ---- cells and records ---------------------------------------------------------------------------------------------

// 0xaeb6f0.
cTextureManager::Cell::Cell(const Cell &other)
{
    mValue = other.mValue;
    Pool()->AddRefHandle(mValue);
}

cTextureManager::Record::Record(const Record &other)
    : name(other.name), texture(other.texture), references(other.references), id(other.id), flags(other.flags)
{
    if (texture) texture->AddRef();
}

// ---- construction --------------------------------------------------------------------------------------------------

// 0xade2e0.
cTextureManager *cTextureManager::Get()
{
    static cTextureManager manager;
    return &manager;
}

cTextureManager::cTextureManager() : mStarted(false)
{
    AddRef();
    mById.reserve(100);
    mFreeIds.reserve(50);
    mFilter.a = 3;
    mFilter.mode = 3;
    mFilter.bias = -0.2f;

    // The default white texture: 1x1, its own update strategy, added under its name with flags 2.
    if (nGZGraphic4::GetCurrentDeviceCached()) {
        cIGZUnknown *texture = nullptr;
        CreateObject(kBlankTextureClass, kBlankTextureIid, &texture);
        cIGZUnknown *const strategy = NewUpdateStrategy();
        strategy->AddRef();
        RawSlot<10>(texture, strategy, 1u, 1u, 1u, 0x10u, 4u, 1.0f); // receiver: nGZSceneGraph::cITexture (Create)
        cTextureManager::Add(texture, "DefaultWhiteA1Texture", 2);  // a direct call in the original
        strategy->Release();
        ReleaseIf(texture);
    }

    RZAutoRef graphics{nullptr};
    if (ifaces::cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(kGraphicSystemId, kGraphicSystemIid, &graphics.mObject);
    static_cast<nGZEvent::cIAbstractBehavior *>(graphics.mObject)->Connect(kDeviceEvent, this, kDeviceEvent);
    graphics.ReleaseObject();
}

// The members go in reverse order (the free ids, the ids, the records).
cTextureManager::~cTextureManager() = default;

bool cTextureManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    switch (riid) {
    case cITextureManager::kIid:
    case 1:
    case kIidBehavior2:
        *out = static_cast<cITextureManager *>(this);
        AddRef();
        return true;
    case kIidBehaviorType:
        *out = static_cast<cITextureManager *>(this);
        return true;
    }
    return false;
}

// ---- the device ----------------------------------------------------------------------------------------------------

void cTextureManager::OnEvent(uint32_t type, uint32_t a, void *data, uint32_t b)
{
    Behavior::OnEvent(type, a, data, b);
    if (type == kDeviceEvent) OnDeviceEvent(data);
}

void cTextureManager::OnDeviceEvent(const void *data)
{
    if (static_cast<const uint32_t *>(data)[1] == kDeviceBack) ReloadStale(); // slot 21
}

// ---- slots 15-26 ---------------------------------------------------------------------------------------------------

void cTextureManager::Start()
{
    mStarted = true;
}

void cTextureManager::Shutdown()
{
    mStarted = false;
    GetCachedObjectB();
    mRecords.clear();
    mById.clear();
    mFreeIds.clear();
}

void cTextureManager::AddRefId(uint32_t id)
{
    if (!mStarted || id == cTextureHandle::kNone) return;
    ++mById.data()[id]->references;
}

void cTextureManager::ReleaseId(uint32_t id)
{
    if (!mStarted || id == cTextureHandle::kNone) return;
    Record *const record = mById.data()[id];
    if (--record->references != 0) return;
    if (record->flags & 1) {
        const auto range = mRecords.equal_range(static_cast<const ServiceCell &>(record->name));
        mRecords.erase(range.first, range.second);
        mById.data()[id] = nullptr;
        mFreeIds.push_back(id);
        return;
    }
    VirtualCall<TextureCall, 4>(record->texture); // receiver: nGZSceneGraph::cITexture (0xab5e00 / 0xab5dc0 / 0xab5dd0, ret 0)
}

void cTextureManager::ReloadStale()
{
    for (auto &node : mRecords) {
        cIGZUnknown *const texture = node.second.texture;
        if (texture && (node.second.flags & 4)) VirtualCall<TextureCall, 3>(texture); // receiver: nGZSceneGraph::cITexture (0xab5610 / 0xab5090 / 0xab5240, ret 0)
    }
}

void cTextureManager::SetFilterParams(uint32_t a, uint32_t mode, float bias)
{
    if (a != mFilter.a || mode != mFilter.mode || bias != mFilter.bias) {
        mFilter.a = a;
        mFilter.mode = mode;
        mFilter.bias = bias;
    }
}

cIGZUnknown *cTextureManager::GetTexture(uint32_t id)
{
    return mById.data()[id]->texture;
}

TextureInfo cTextureManager::GetInfo(uint32_t id)
{
    TextureInfo info{0, 0};
    if (mStarted && id != cTextureHandle::kNone) {
        Record *const record = mById.data()[id];
        info.name = Dispatch8(record);
        info.references = record->references;
    }
    return info;
}

bool cTextureManager::Has(const char *name)
{
    Cell cell(0);
    OpenNameCell(&cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)));
    return mRecords.find(static_cast<const ServiceCell &>(cell)) != mRecords.end();
}

TextureFilterParams cTextureManager::GetFilterParams()
{
    return mFilter;
}

// ---- slot 20: a texture under a name -------------------------------------------------------------------------------

uint32_t cTextureManager::Add(cIGZUnknown *texture, const char *name, uint32_t flags)
{
    Cell cell(0);
    OpenNameCell(&cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)));
    RecordMap::iterator at = mRecords.lower_bound(static_cast<const ServiceCell &>(cell));
    if (at == mRecords.end() || CellLess()(cell, at->first)) {
        // A new record: the node is copied from a pair made of the name and a blank record {no name, no texture, 0,
        // -1, flag 1} (the pool sees each copy open its handle and the pair's close), then named and given an id.
        {
            const Record blank(kNoName, nullptr, 0, 0xffffffffu, 1);
            const std::pair<const Cell, Record> value(cell, blank);
            at = mRecords.emplace_hint(at, value);
        }
        Record &record = at->second;
        record.name.Assign(&cell.mValue);
        if (mFreeIds.empty()) {
            record.id = static_cast<uint32_t>(mById.size());
            mById.push_back(&record);
        } else {
            record.id = mFreeIds.back();
            mFreeIds.pop_back();
            mById[record.id] = &record;
        }
    }
    Record &record = at->second;
    cIGZUnknown *const old = record.texture;
    if (texture != old) {
        if (texture) texture->AddRef();
        record.texture = texture;
        ReleaseIf(old);
    }
    record.flags = flags;
    return record.id;
}

// ---- slot 17: find or load by name ---------------------------------------------------------------------------------

uint32_t cTextureManager::FindOrLoad(const char *name)
{
    Cell cell(0);
    OpenNameCell(&cell, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)));
    const RecordMap::iterator found = mRecords.find(static_cast<const ServiceCell &>(cell));
    if (found != mRecords.end()) return found->second.id;

    // The "_txtr" resource of the name, in the name's group and then (when that is not it already) the game's.
    std::string text(name);
    text += "_txtr";
    StringKeyParser::Result key{};
    StringKeyParser::ParseKey(&key, text.c_str(), kTextureType, nullptr);
    cIGZUnknown *resource = nullptr;
    const auto load = [&]() {
        void *service = nullptr;
        if (ifaces::cIGZFrameWork *const framework = Framework())
            framework->GetSystemService(kResourceService, kResourceIid, &service);
        if (cIGZUnknown *const old = resource) {
            resource = nullptr;
            old->Release();
        }
        const bool loaded = RawSlot<3, bool>(service, &key, kTextureResourceIid, &resource, 0u, 0u);
        RZAutoRef{service}.ReleaseObject();
        return loaded;
    };
    bool loaded = load();
    if (!loaded && key.id != kEngineGroup) {
        key.id = kEngineGroup;
        loaded = load();
    }

    uint32_t id;
    cIGZUnknown *texture = nullptr;
    if (loaded) {
        // The texture class for the resource's kind (slot 25: 6 a cube map), loaded from it (slot 9).
        const uint32_t kind = RawSlot<25>(resource);
        CreateObject(kind == 6 ? kCubeTextureClass : kImageTextureClass, kImageTextureIid, &texture);
        RawSlot<9>(texture, resource, 0u); // receiver: nGZSceneGraph::cITexture
        id = Add(texture, name, 1);
        ReleaseIf(texture);
    } else {
        // Not there: a 128x128 texture drawn by the missing-texture strategy.
        cIGZUnknown *const strategy = NewMissingStrategy(name);
        strategy->AddRef();
        CreateObject(kBlankTextureClass, kBlankTextureIid, &texture);
        RawSlot<10>(texture, strategy, 0x80u, 0x80u, 1u, 0x10u, 0u, 1.0f); // receiver: nGZSceneGraph::cITexture (Create)
        id = Add(texture, name, 1);
        ReleaseIf(texture);
        strategy->Release();
    }
    ReleaseIf(resource);
    return id;
}

// ---- slot 27: a texture's sampler states ---------------------------------------------------------------------------

void cTextureManager::SamplerStates(void *stateA, void *stateB, cIGZUnknown *texture)
{
    uint8_t *const a = static_cast<uint8_t *>(stateA);
    uint8_t *const b = static_cast<uint8_t *>(stateB);
    const auto word = [](uint8_t *p, uint32_t offset, uint16_t value) { *reinterpret_cast<uint16_t *>(p + offset) = value; };
    const auto dword = [](uint8_t *p, uint32_t offset, uint32_t value) { *reinterpret_cast<uint32_t *>(p + offset) = value; };

    RawSlot<3>(texture); // receiver: nGZSceneGraph::cITexture (its result unused)
    if (!RawSlot<6, bool>(texture)) {
        a[4] = 0x1e;
        word(a, 6, 0x14);
        dword(a, 8, 0);
        word(a, 0xc, 0);
        dword(a, 0x10, 1);
        b[4] = 0x1f;
        word(b, 6, 0x14);
        dword(b, 8, 0);
        b[0xc] = 0;
        dword(b, 0x10, 0);
        return;
    }
    switch (mFilter.mode) {
    case 0:
        word(a, 0xc, 0);
        dword(a, 0x10, 1);
        b[0xc] = 1;
        break;
    case 1:
        word(a, 0xc, 0x101);
        dword(a, 0x10, 1);
        b[0xc] = 1;
        break;
    case 2:
        word(a, 0xc, 0x101);
        dword(a, 0x10, 1);
        b[0xc] = 2;
        break;
    case 3:
        word(a, 0xc, 0x202);
        dword(a, 0x10, 999);
        b[0xc] = 2;
        break;
    default:
        break;
    }
    *reinterpret_cast<float *>(b + 0x10) = mFilter.bias;
    using nGZGraphic4::cTextureFilterState;
    using nGZGraphic4::cTextureMipFilterState;
    auto *const filter = reinterpret_cast<cTextureFilterState *>(a);
    auto *const mip = reinterpret_cast<cTextureMipFilterState *>(b);
    switch (RawSlot<5>(texture)) {
    case 4:
    case 7:
        nGZGraphic4::FitTextureSamplerStates(filter, mip, RawSlot<8, nGZGraphic4::cITextureBase *>(texture, 0u));
        break;
    case 6:
        nGZGraphic4::FitCubeTextureSamplerStates(filter, mip, RawSlot<8, nGZGraphic4::cITextureBase *>(texture, 0u));
        break;
    default:
        break;
    }
}

// ---- slot 28: the iterator range over the ids ----------------------------------------------------------------------

namespace {

// The game's cTextureHandleIterator (0x10 bytes, pool-allocated; its Clone, AddRef, Release and the rest stay the
// image's, reached through its vtable): +0 vtable, +4 count, +8 the index, +0xc the vector of records.
struct IteratorObject {
    const void *vtable;
    uint32_t count;
    uint32_t index;
    const void *records;
};

class cITextureIterator : public cIGZUnknown {
public:
    virtual cIGZUnknown *Clone() = 0; // 3
};

// 0xadd130: an iterator over the ids, moved on to the first index that holds a record (0xacc480).
IteratorObject *NewHandleIterator(const std::vector<cTextureManager::Record *> *records, uint32_t index)
{
    auto *const object = static_cast<IteratorObject *>(reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f8bd4)->Alloc());
    object->vtable = GameAddress<void>(kIteratorVTable);
    object->count = 0;
    object->index = index;
    object->records = records;
    const size_t size = records->size();
    if (index < size && (*records)[index] == nullptr) {
        do ++object->index;
        while (object->index < size && (*records)[object->index] == nullptr);
    }
    return object;
}

} // namespace

TextureRange cTextureManager::Range()
{
    IteratorObject *const end = NewHandleIterator(&mById, static_cast<uint32_t>(mById.size()));
    IteratorObject *const begin = NewHandleIterator(&mById, 0);
    auto *const first = reinterpret_cast<cITextureIterator *>(begin);
    TextureRange range;
    range.cursor = first->Clone();
    if (range.cursor) range.cursor->AddRef();
    range.begin = first;
    first->AddRef();
    range.end = reinterpret_cast<cIGZUnknown *>(end);
    range.end->AddRef();
    return range;
}

} // namespace nGZSceneGraph
