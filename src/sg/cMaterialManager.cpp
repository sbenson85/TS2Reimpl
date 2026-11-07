// nGZSceneGraph::cMaterialManager (lane RD); see the header.
#include "sg/cMaterialManager.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h" // GetCurrentDeviceCached
#include "gz/cBehaviorRegistry.h"
#include "gz/cDeviceDX.h"
#include "gz/cIGZCOM.h"
#include "gz/cVertexArrayDX.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFixedAllocator.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZStopwatch.h"
#include "sg/cMaterialIterators.h"
#include "sg/cStringPool.h"
#include "sg/cTextureManager.h"

#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"
#include "misc/UtilitiesE.h"

#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace nGZSceneGraph {

using nGZGraphic4::cIStaticVertexArray;

// ---- the variant keys' hash ---------------------------------------------------------------------------------------

uint32_t __cdecl MixHash3(uint32_t a, uint32_t b, uint32_t c)
{
    a -= b; a -= c; a ^= c >> 13;
    b -= c; b -= a; b ^= a << 8;
    c -= a; c -= b; c ^= b >> 13;
    a -= b; a -= c; a ^= c >> 12;
    b -= c; b -= a; b ^= a << 16;
    c -= a; c -= b; c ^= b >> 5;
    a -= b; a -= c; a ^= c >> 3;
    b -= c; b -= a; b ^= a << 10;
    c -= a; c -= b; c ^= b >> 15;
    return c;
}

// ---- the behaviour registry ---------------------------------------------------------------------------------------

namespace {

std::optional<nGZEvent::cEventAttributeMap> gRegistry;
// The class's one event. Its record ends in 4, where the graphics classes' (ClassRegistryFor) end in 8.
const nGZEvent::cEventAttribute kDeviceEventRecord{0xca7ccbbc, "CurrentDeviceEvent", 1, 0x3b6c3e0a, 2, 4};

constexpr uint32_t kVertexArrayClass = 0x3bffec52; // cStaticVertexComponentArrayDX
constexpr uint32_t kIndexArrayClass = 0x9bfff0ea;  // cStaticVertexIndexArrayDX
constexpr uint32_t kVertexBufferBytes = 0x4000;
constexpr uint32_t kIndexBufferBytes = 6;          // three 16-bit indices
constexpr uint32_t kDeviceBack = 1;                // the device event's code when the device is ready again
constexpr uint32_t kNoName = 0xffffffffu;

cIStringPool *Pool() { return static_cast<cIStringPool *>(GetCachedObjectB()); }

ifaces::cIGZFrameWork *Framework() { return static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()); }

// The graphics system the manager listens to for the device event (held while the caller uses it).
void GraphicSystem(RZAutoRef &graphics)
{
    if (ifaces::cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(cMaterialManager::kGraphicSystemId, cMaterialManager::kGraphicSystemIid,
                                    &graphics.mObject);
}

// A new static array of class `classId` (asked for interface `iid`) in `ref`, the old one released first.
void MakeArray(cMaterialManager::VertexRef &ref, uint32_t classId, uint32_t iid)
{
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    ref.Reset();
    com->GetClassObject(classId, iid, reinterpret_cast<void **>(&ref.p));
}

// A vertex array cleared to zero.
void ZeroArray(cIStaticVertexArray *array)
{
    void *const bytes = array->Lock(1, 0, 0);
    std::memset(bytes, 0, array->GetSize());
    array->Unlock();
}

} // namespace

nGZEvent::cEventAttributeMap *cMaterialManagerBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

// 0xacc550.
nGZEvent::cEventAttributeMap *__cdecl cMaterialManagerBehaviorTraits::RegistryFor(uint32_t id,
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

void cMaterialManagerBehaviorTraits::ResetRegistryForTest() { gRegistry.reset(); }

// ---- cells, keys and sets -----------------------------------------------------------------------------------------

// 0xaeb6f0.
cMaterialManager::NameCell::NameCell(const NameCell &other) : ServiceCell{other.mValue}
{
    Pool()->AddRefHandle(mValue);
}

cMaterialManager::NameCell::NameCell() : ServiceCell{kNoName} {}

cMaterialManager::NameCell::NameCell(const ServiceCell &other) : ServiceCell{other.mValue}
{
    Pool()->AddRefHandle(mValue);
}

// 0xae87d0.
cMaterialManager::NamedCell::NamedCell(const char *name) : ServiceCell{kNoName}
{
    if (name) Open(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)), 0);
}

size_t cMaterialManager::ListenerHash::operator()(cIGZUnknown *object) const { return RawSlot<6>(object); }

bool cMaterialManager::ListenerEqual::operator()(cIGZUnknown *a, cIGZUnknown *b) const
{
    return RawSlot<7, bool>(a, b);
}

// ---- construction -------------------------------------------------------------------------------------------------

// 0xacc040.
cMaterialManager *cMaterialManager::Get()
{
    static cMaterialManager manager;
    return &manager;
}

cMaterialManager::cMaterialManager() : mBuffersReady(false), mStarted(false)
{
    AddRef();
    mByIndex.reserve(100);
    mFreeIndices.reserve(50);

    RZAutoRef graphics{nullptr};
    GraphicSystem(graphics);
    static_cast<nGZEvent::cIAbstractBehavior *>(graphics.mObject)->Connect(kDeviceEvent, this, kDeviceEvent);
    if (nGZGraphic4::GetCurrentDeviceCached()) {
        for (uint32_t i = 0; i < kBuffers; ++i) {
            MakeArray(mVerticesA[i], kVertexArrayClass, nGZGraphic4::kIidStaticVertexComponentArray);
            MakeArray(mVerticesB[i], kVertexArrayClass, nGZGraphic4::kIidStaticVertexComponentArray);
        }
        MakeArray(mIndicesA, kIndexArrayClass, nGZGraphic4::kIidStaticVertexIndexArray);
        MakeArray(mIndicesB, kIndexArrayClass, nGZGraphic4::kIidStaticVertexIndexArray);
    }
    CreateDeviceObjects();
    graphics.ReleaseObject();
}

// The members go in reverse order (the arrays, the jobs, the sets, the entries).
cMaterialManager::~cMaterialManager() = default;

bool cMaterialManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    switch (riid) {
    case cIMaterialManager::kIid:
    case 1:
    case kIidBehavior2:
        *out = static_cast<cIMaterialManager *>(this);
        AddRef();
        return true;
    case kIidBehaviorType:
        *out = static_cast<cIMaterialManager *>(this);
        return true;
    }
    return false;
}

// ---- the device ---------------------------------------------------------------------------------------------------

void cMaterialManager::OnEvent(uint32_t type, uint32_t a, void *data, uint32_t b)
{
    Behavior::OnEvent(type, a, data, b);
    if (type == kDeviceEvent) OnDeviceEvent(data);
}

void cMaterialManager::OnDeviceEvent(const void *data)
{
    if (static_cast<const uint32_t *>(data)[1] != kDeviceBack) return;
    CreateDeviceObjects();
    ReloadAll(nullptr, 1); // slot 24 (the reload of every material, its listeners told)
}

void cMaterialManager::CreateDeviceObjects()
{
    mBuffersReady = false;
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device || !device->IsDeviceReady()) return;
    for (uint32_t i = 0; i < kBuffers; ++i) {
        mVerticesA[i].p->ReleaseResource();
        mVerticesB[i].p->ReleaseResource();
    }
    mIndicesA.p->ReleaseResource();
    mIndicesB.p->ReleaseResource();
    for (uint32_t i = 0; i < kBuffers; ++i) {
        if (!mVerticesA[i].p->Create(device, kVertexBufferBytes, 1)) return;
        if (!mVerticesB[i].p->Create(device, kVertexBufferBytes, 2)) return;
    }
    if (!mIndicesA.p->Create(device, kIndexBufferBytes, 1) || !mIndicesB.p->Create(device, kIndexBufferBytes, 2)) return;
    mBuffersReady = true;
    for (uint32_t i = 0; i < kBuffers; ++i) {
        ZeroArray(mVerticesA[i].p);
        ZeroArray(mVerticesB[i].p);
    }
    // The triangle 0, 1, 2 in each index array.
    uint16_t *indices = static_cast<uint16_t *>(mIndicesA.p->Lock(1, 0, 0));
    indices[0] = 0, indices[1] = 1, indices[2] = 2;
    mIndicesA.p->Unlock();
    indices = static_cast<uint16_t *>(mIndicesB.p->Lock(1, 0, 0));
    indices[2] = 2, indices[0] = 0, indices[1] = 1;
    mIndicesB.p->Unlock();
}

// ---- starting and stopping ----------------------------------------------------------------------------------------

void cMaterialManager::Start() { mStarted = true; }

void cMaterialManager::Shutdown()
{
    Reset(nullptr, 1);
    mEntries.clear();
    mByIndex.clear();
    mFreeIndices.clear();
    mPending.clear();
    for (VertexRef &array : mVerticesA) array.Assign(nullptr);
    for (VertexRef &array : mVerticesB) array.Assign(nullptr);
    mIndicesA.Reset();
    mIndicesB.Reset();

    RZAutoRef graphics{nullptr};
    GraphicSystem(graphics);
    static_cast<nGZEvent::cIAbstractBehavior *>(graphics.mObject)->Disconnect(kDeviceEvent, this, kDeviceEvent);
    mStarted = false;
    graphics.ReleaseObject();
}

// ---- the tick -----------------------------------------------------------------------------------------------------

void cMaterialManager::OnTick()
{
    if (!mJobs.empty()) {
        cRZStopwatch watch;
        watch.Construct(4);
        watch.Start();
        while (watch.Elapsed() < kTickBudgetMs) {
            Job &job = mJobs.front();
            // ORIGINAL-BUG: a job's name always has an entry (ResetEntry drops an entry's jobs before it goes); were it
            // missing, the original would take the map's head node for the entry. Ours does the same.
            Entry &entry = mEntries.find(job.name)->second;
            if (entry.variants.find(job.key) == entry.variants.end())
                BuildVariant(&entry, &mJobs.front().key, false);
            mJobs.pop_front();
            if (mJobs.empty()) break;
        }
    }
    for (std::set<Entry *>::iterator it = mPending.begin(); it != mPending.end();) {
        Entry *const entry = *it;
        ++it;
        if (--entry->countdown == 0) DestroyEntry(entry);
    }
}

void cMaterialManager::FlushPending()
{
    while (!mPending.empty()) DestroyEntry(*mPending.begin());
}

// ---- entry references ---------------------------------------------------------------------------------------------

void cMaterialManager::AddRefIndex(int32_t index)
{
    if (!mStarted || index == -1) return;
    Entry *const entry = mByIndex[index];
    if (entry->references == 0) {
        const std::set<Entry *>::iterator waiting = mPending.find(entry);
        if (waiting != mPending.end()) {
            mPending.erase(waiting);
            entry->countdown = 0;
        }
    }
    ++entry->references;
}

void cMaterialManager::ReleaseIndex(int32_t index)
{
    if (!mStarted || index == -1) return;
    Entry *const entry = mByIndex[index];
    if (--entry->references == 0 && (entry->flags & 2)) {
        entry->countdown = kDeleteDelay;
        mPending.insert(entry);
    }
}

void cMaterialManager::DestroyEntry(Entry *entry)
{
    ResetEntry(entry);
    mPending.erase(entry);
    if (entry->flags & 2) {
        const uint32_t index = entry->index;
        NameCell name(entry->name);
        mEntries.erase(name);
        mByIndex[index] = nullptr;
        mFreeIndices.push_back(index);
    }
}

void cMaterialManager::ResetEntry(Entry *entry)
{
    if (entry->variants.size()) {
        entry->variants.clear();
        entry->keys.clear();
    }
    for (size_t i = 0; i < mJobs.size();) {
        if (mJobs[i].name.mValue == entry->name.mValue)
            mJobs.erase(mJobs.begin() + i);
        else
            ++i;
    }
}

// ---- resets and queries -------------------------------------------------------------------------------------------

void cMaterialManager::Reset(const char *name, uint32_t flags)
{
    if (name) {
        NamedCell cell(name);
        const EntryMap::iterator found = mEntries.find(cell);
        if (found != mEntries.end()) ResetEntry(&found->second);
        return;
    }
    if (flags & 1) {
        for (cIGZUnknown *listener : mListenersB) {
            RawSlot<3>(listener);
            RawSlot<4>(listener, 0u);
        }
        for (cIGZUnknown *listener : mListenersA) RawSlot<4>(listener, 0u);
        TextureManager()->ReloadStale();
    }
    mListenersA.clear();
    mListenersB.clear();
    mJobs.clear();
    for (EntryMap::value_type &node : mEntries) {
        Entry &entry = node.second;
        if (entry.variants.size()) entry.variants.clear();
        entry.keys.clear();
    }
}

void cMaterialManager::MarkAllStale()
{
    for (EntryMap::value_type &node : mEntries)
        if (node.second.flags & 1) node.second.flags |= 4;
    Reset(nullptr, 1);
}

bool cMaterialManager::IsLoaded(const char *name)
{
    NamedCell cell(name);
    const EntryMap::iterator found = mEntries.find(cell);
    return found != mEntries.end() && !(found->second.flags & 4);
}

MaterialInfo cMaterialManager::GetInfo(int32_t index)
{
    MaterialInfo info{};
    if (mStarted && index != -1) {
        Entry *const entry = mByIndex[index];
        info.nameHash = RawSlot<19>(entry->material.p);
        info.material = entry->material.p;
        info.parsed = entry->parsed.p;
        info.references = entry->references;
        info.variants = static_cast<uint32_t>(entry->variants.size());
    }
    return info;
}

// ---- part 2: variants, loading and the listeners ------------------------------------------------------------------

namespace {

constexpr uint32_t kResetQuery = 0xcd2e1781;        // the interface a listener answers to be kept in set B
constexpr uint32_t kVariantQuery = 0xfc17e651;      // what a made variant is asked for
constexpr uint32_t kMaterialClass = 0x49596978;     // the director's material class (and the "_txmt" type)
constexpr uint32_t kMaterialIid = 0x49596972;
constexpr uint32_t kResourceService = 0x056b906e;   // the resource service (the framework's)
constexpr uint32_t kResourceIid = 0x656b8efc;
constexpr uint32_t kTextureService = 0x8d127822;
constexpr uint32_t kTextureIid = 0xad127830;
constexpr uint32_t kEngineGroup = 0x1c0532fa;       // the group a material of the game's own is in

// A cRZString on the stack as the original builds it inline (game vtable, the text, refcount 0), destroyed with
// cRZString's destructor.
struct LocalRZString {
    union {
        cRZString mText;
    };
    explicit LocalRZString(const char *text = "")
    {
        ::new (&mText.mString) std::string(text);
        mText.mVTable = cRZString::VTable(); // the owned word (the game's table hooked, rzface's image-free)
        mText.mRefCount = 0;
    }
    LocalRZString(const LocalRZString &) = delete;
    ~LocalRZString() { mText.Destruct(); }
};

// The material parser (0xae8980, a cached service, ours).
void *MaterialParser() { return GetMaterialParserCached(); }

// The resource service (counted; null when there is no framework).
void *ResourceService()
{
    void *service = nullptr;
    if (ifaces::cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(kResourceService, kResourceIid, &service);
    return service;
}

// The key of a "_txmt" resource named `name`.
void TxmtKey(StringKeyParser::Result &result, const char *name)
{
    std::string text(name);
    text += "_txmt";
    result = {};
    StringKeyParser::ParseKey(&result, text.c_str(), kMaterialClass, nullptr);
}

// What the parser reads of a material (0x18 bytes): its parsed object, the words, the 52-byte blobs and a flag byte.
struct ParsedMaterial {
    cIGZUnknown *parsed;
    uint32_t wordCount;
    const uint32_t *words;
    uint32_t blobCount;
    const cMaterialManager::Blob52 *blobs;
    uint32_t flag;
};

} // namespace

// 0xacadd0.
void cMaterialManager::BuildVariant(Entry *entry, const VariantKey *key, bool refine)
{
    cIGZUnknown *const format =
        RawSlot<8, cIGZUnknown *>(nGZGraphic4::GetVertexFormatsManagerCached(), (key->w[2] >> 4) & 0x7f);
    format->AddRef();
    bool held = false;
    for (const Held &h : entry->keys)
        if (h.p == format) held = true;
    if (!held) entry->keys.emplace(format);
    format->Release();

    void *buffers[34] = {};
    if (mBuffersReady) {
        for (uint32_t i = 0; i < kBuffers; ++i) {
            buffers[i] = mVerticesA[i].p;
            buffers[kBuffers + i] = mVerticesB[i].p;
        }
        buffers[32] = mIndicesA.p;
        buffers[33] = mIndicesB.p;
        nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
        for (auto &node : entry->variants) {
            VariantKey nodeKey = node.first;
            if (RawSlot<10, bool>(node.second.p, &nodeKey, key, buffers, device)) {
                const std::pair<const VariantKey, Ref<cIGZUnknown>> shared(*key, node.second);
                entry->variants.insert(shared);
                return;
            }
        }
    }

    cIGZUnknown *made = nullptr;
    void *parser = MaterialParser();
    const uint32_t more = RawSlot<6>(parser, &made, entry->material.p, buffers, key);
    if (more && refine) {
        parser = MaterialParser();
        if (made) {
            cIGZUnknown *const old = made;
            made = nullptr;
            old->Release();
        }
        RawSlot<7>(parser, &made, entry->material.p, buffers, key, more);
    }
    if (made) {
        cIGZUnknown *queried = nullptr;
        Register(made, kVariantQuery, reinterpret_cast<void **>(&queried));
        {
            cIGZUnknown *const old = made;
            made = nullptr;
            old->Release();
        }
        {
            const std::pair<const VariantKey, Ref<cIGZUnknown>> variant(*key, Ref<cIGZUnknown>());
            const_cast<Ref<cIGZUnknown> &>(variant.second).Assign(queried);
            entry->variants.insert(variant);
        }
        if (queried) queried->Release();
    }
}

// 0xacce00.
void cMaterialManager::ReloadEntry(Entry *entry)
{
    std::vector<std::pair<VariantKey, Ref<cIGZUnknown>>> saved;
    saved.reserve(entry->variants.size());
    for (auto &node : entry->variants) saved.emplace_back(node.first, node.second);
    if (entry->variants.size()) entry->variants.clear();
    for (auto &variant : saved) BuildVariant(entry, &variant.first, true);
}

// 0xaccfb0.
void cMaterialManager::ReloadAll(const char *name, uint32_t flags)
{
    if (name) {
        NamedCell cell(name);
        const EntryMap::iterator found = mEntries.find(cell);
        if (found != mEntries.end()) ReloadEntry(&found->second);
        return;
    }
    std::vector<Ref<cIGZUnknown>> told;
    if (flags & 1) {
        if (mListenersB.size()) told.reserve(mListenersB.size());
        for (cIGZUnknown *listener : mListenersB) {
            // Each is held (a counted copy in the vector), then told.
            {
                listener->AddRef();
                Ref<cIGZUnknown> copy;
                copy.p = listener;
                told.push_back(copy);
            }
            RawSlot<3>(listener);
            RawSlot<4>(listener, 0u);
        }
        for (cIGZUnknown *listener : mListenersA) RawSlot<4>(listener, 0u);
        mListenersB.clear();
        mListenersA.clear();
        TextureManager()->ReloadStale();
    }
    for (EntryMap::value_type &node : mEntries) ReloadEntry(&node.second);
}

// 0xacc840.
uint32_t cMaterialManager::Register(cIGZUnknown *object, uint32_t iid, void **out)
{
    void *queried = nullptr;
    const bool wanted = RawSlot<0, bool>(object, kResetQuery, &queried);
    // The set is searched first; a new one is inserted (its hash taken again) and told it was registered.
    ListenerSet &set = wanted ? mListenersB : mListenersA;
    cIGZUnknown *const key = wanted ? static_cast<cIGZUnknown *>(queried) : object;
    cIGZUnknown *element;
    const ListenerSet::iterator present = set.find(key);
    if (present != set.end()) {
        element = *present;
    } else {
        const std::pair<ListenerSet::iterator, bool> put = set.insert(key);
        element = *put.first;
        RawSlot<4>(element, 1u);
    }
    return RawSlot<0>(element, iid, out);
}

// 0xacdbc0. ORIGINAL-BUG: both sets are searched for `object` itself, but slot 26 keeps in set B what the object's
// query answered (not always the object), so an object it answered another pointer for is never found there.
void cMaterialManager::Unregister(cIGZUnknown *object)
{
    void *queried = nullptr;
    const bool wanted = RawSlot<0, bool>(object, kResetQuery, &queried);
    ListenerSet &set = wanted ? mListenersB : mListenersA;
    const ListenerSet::iterator found = set.find(object);
    if (found != set.end() && *found == object) set.erase(found);
}

// 0xacc320.
MaterialInfo cMaterialManager::GetInfoByName(const char *name)
{
    if (name) {
        for (EntryMap::value_type &node : mEntries) {
            Entry &entry = node.second;
            if (!entry.material.p) continue;
            const char *const text = RawSlot<20, const char *>(entry.material.p);
            if (!text || _stricmp(text, name) != 0) continue;
            if (entry.name.mValue == kNoName || entry.references == 0) continue;
            return GetInfo(static_cast<int32_t>(entry.index));
        }
    }
    return GetInfo(-1);
}

// 0xaccce0.
uint32_t cMaterialManager::Slot30(uint32_t nameWord)
{
    const char *const name = reinterpret_cast<const char *>(nameWord);
    if (!name) return nameWord;
    std::vector<NameCell> found;
    for (EntryMap::value_type &node : mEntries) {
        Entry &entry = node.second;
        if (!entry.material.p) continue;
        const char *const text = RawSlot<20, const char *>(entry.material.p);
        if (!text || _stricmp(text, name) != 0) continue;
        if (entry.name.mValue == kNoName) continue;
        found.push_back(entry.name);
    }
    for (const NameCell &cell : found) {
        const NameCell copy(cell);
        const uint32_t text = Dispatch8(const_cast<NameCell *>(&copy));
        if (text) ReloadAll(reinterpret_cast<const char *>(text), 0);
    }
    return nameWord;
}

// 0xacc0d0.
bool cMaterialManager::IsAvailable(const char *name)
{
    NamedCell cell(name); // held to the end
    const EntryMap::iterator found = mEntries.find(cell);
    if (found != mEntries.end() && !(found->second.flags & 4)) return true;
    StringKeyParser::Result key;
    TxmtKey(key, name);
    void *const service = ResourceService();
    const bool there = RawSlot<15, bool>(service, &key);
    RZAutoRef{service}.ReleaseObject();
    return there;
}

// 0xacc640.
bool cMaterialManager::LoadMaterial(const char *name)
{
    if (!name || !nGZGraphic4::GetCurrentDeviceCached()) return false;
    StringKeyParser::Result key;
    TxmtKey(key, name);
    void *const service = ResourceService();
    cIGZUnknown *material = nullptr;
    bool loaded = false;
    if (RawSlot<3, bool>(service, &key, kMaterialIid, &material, 0u, 0u)) {
        if (key.id != kEngineGroup) RawSlot<10>(material, name);
        BuildEntry(material, 3);
        loaded = true;
    }
    if (material) material->Release();
    RZAutoRef{service}.ReleaseObject();
    return loaded;
}

// 0xacbaf0 (slot 23's body: 0xacc630 jumps to it).
uint32_t cMaterialManager::BuildEntry(cIGZUnknown *material, uint32_t flags)
{
    const char *const name = RawSlot<19, const char *>(material);
    NamedCell cell(name);
    EntryMap::iterator at = mEntries.lower_bound(cell);
    if (at == mEntries.end() || NameLess()(cell, at->first))
        at = mEntries.emplace_hint(at, std::piecewise_construct, std::forward_as_tuple(static_cast<const ServiceCell &>(cell)),
                                   std::forward_as_tuple());
    Entry *const entry = &at->second;
    if (entry->index == 0xffffffffu) {
        if (mFreeIndices.empty()) {
            entry->index = static_cast<uint32_t>(mByIndex.size());
            mByIndex.push_back(nullptr);
        } else {
            entry->index = mFreeIndices.back();
            mFreeIndices.pop_back();
        }
        mByIndex[entry->index] = entry;
        void *service = nullptr;
        if (ifaces::cIGZFrameWork *const framework = Framework())
            framework->GetSystemService(kTextureService, kTextureIid, &service);
        if (service) {
            {
                LocalRZString text(name);
                if (flags & 4)
                    RawSlot<10>(service, &text.mText);
                else
                    RawSlot<11>(service, &text.mText);
            }
            RZAutoRef{service}.ReleaseObject();
        }
    } else {
        if (!RawSlot<18, bool>(material, entry->material.p)) ResetEntry(entry);
        const std::set<Entry *>::iterator waiting = mPending.find(entry);
        if (waiting != mPending.end()) mPending.erase(waiting);
    }
    entry->name.Assign(&cell.mValue);
    entry->material.Assign(material);
    entry->flags = flags;
    entry->countdown = 0;

    ParsedMaterial parsed{};
    RawSlot<4>(MaterialParser(), &parsed, entry->material.p);
    entry->parsed.Assign(parsed.parsed);
    if (parsed.wordCount) {
        entry->words.resize(parsed.wordCount);
        std::memcpy(entry->words.data(), parsed.words, parsed.wordCount * sizeof(uint32_t));
    }
    if (parsed.blobCount) {
        entry->blobs.resize(parsed.blobCount);
        for (uint32_t i = 0; i < parsed.blobCount; ++i) entry->blobs[i] = parsed.blobs[i];
    }
    entry->byte5c = static_cast<uint8_t>(parsed.flag);
    RawSlot<5>(MaterialParser());
    return entry->index;
}

// Slot 23 (0xacc630 is a jump to BuildEntry).
uint32_t cMaterialManager::AddMaterial(cIGZUnknown *material, uint32_t flags)
{
    return BuildEntry(material, flags);
}

// 0xacb7b0.
uint32_t cMaterialManager::FindOrLoad(const char *name)
{
    NamedCell cell(name);
    EntryMap::iterator found = mEntries.find(cell);
    if (found != mEntries.end()) return found->second.index;
    LoadMaterial(name);
    found = mEntries.find(cell);
    if (name[0] == '#') {
        if (found != mEntries.end()) return found->second.index;
        LocalRZString stripped;
        ResolveResourceName(stripped.mText.AsInterface(), name, 0, nullptr);
        if (stripped.mText.CompareTo(name, 0xffffffffu, true) != 0) return FindOrLoad(stripped.mText.ToChar());
    } else if (found != mEntries.end()) {
        return found->second.index;
    }
    cIGZUnknown *material = nullptr;
    RawSlot<3>(GetDirectorFrameworkObject(), kMaterialClass, kMaterialIid, &material);
    RawSlot<10>(material, name);
    const uint32_t index = BuildEntry(material, 7);
    material->Release();
    return index;
}

// 0xacb470.
void cMaterialManager::LoadStale()
{
    GetCachedObjectB();
    cIGZUnknown *material = nullptr;
    for (EntryMap::value_type &node : mEntries) {
        Entry &entry = node.second;
        if (!(entry.flags & 4)) continue;
        const char *const name = reinterpret_cast<const char *>(Dispatch8(const_cast<NameCell *>(&node.first)));
        if (LoadMaterial(name)) continue;
        if (material) {
            cIGZUnknown *const old = material;
            material = nullptr;
            old->Release();
        }
        RawSlot<3>(GetDirectorFrameworkObject(), kMaterialClass, kMaterialIid, &material);
        RawSlot<10>(material, RawSlot<19>(entry.material.p));
        BuildEntry(material, 7);
        cIGZUnknown *const old = material;
        material = nullptr;
        old->Release();
    }
}

// ---- part 2 leftovers: the image's bodies on our object -----------------------------------------------------------

// ---- slots 34-36: the iterator ranges -------------------------------------------------------------------------------

namespace {

// {a clone of begin, begin, end}, each counted (the clone first, as the image does).
template <class Iterator> MaterialRange MakeRange(Iterator *begin, Iterator *end)
{
    MaterialRange range;
    range.cursor = begin->Clone();
    if (range.cursor) range.cursor->AddRef();
    range.begin = begin;
    begin->AddRef();
    range.end = end;
    end->AddRef();
    return range;
}

// The list head of a hash container (its sentinel node) and the node after it (a node's first word is its next).
const MaterialListNode *ListHead(const void *container)
{
    return reinterpret_cast<const MaterialListNode *const *>(container)[1];
}
const MaterialListNode *FirstNode(const MaterialListNode *head) { return head->next; }

} // namespace

// 0xaca850 (thiscall, ret 4).
MaterialRange cMaterialManager::EntryRange()
{
    const auto *const entries = reinterpret_cast<const std::vector<void *> *>(&mByIndex);
    auto *const end = new cMaterialHandleIterator(entries, static_cast<uint32_t>(mByIndex.size()));
    auto *const begin = new cMaterialHandleIterator(entries, 0);
    return MakeRange(begin, end);
}

// 0xaccc00 (thiscall, ret 8).
MaterialRange cMaterialManager::VariantRange(uint32_t index)
{
    const MaterialListNode *const head = ListHead(&mByIndex[index]->variants);
    auto *const end = new cRenderShaderContextIterator(head);
    auto *const begin = new cRenderShaderContextIterator(FirstNode(head));
    return MakeRange(begin, end);
}

// 0xacd7e0 (thiscall, ret 4).
MaterialRange cMaterialManager::ListenerRange()
{
    const MaterialListNode *const head = ListHead(&mListenersB);
    auto *const end = new cShaderIterator(head);
    auto *const begin = new cShaderIterator(FirstNode(head));
    return MakeRange(begin, end);
}

// ---- slot 22: the variant for a key, and the jobs for the keys it implies -------------------------------------------

namespace {

using Key = cMaterialManager::VariantKey;

// Two keys' order in the set of keys to queue: their twelve bytes compared as memory (0xacd3f3's loops).
struct KeyBytesLess {
    bool operator()(const Key &a, const Key &b) const { return std::memcmp(&a, &b, sizeof(Key)) < 0; }
};

// 0xaca6f0: for each of the entry's blobs, a key made from the blob's words: the running sums of words 1-12 packed as
// nibbles from position 1 (each sum or-ed in whole, so a sum over 15 spills into the nibbles above), the key's third
// word kept.
void AddBlobKeys(std::vector<Key> &keys, const cMaterialManager::Entry &entry, const Key &base)
{
    for (const cMaterialManager::Blob52 &blob : entry.blobs) {
        uint32_t packed[2] = {0, 0};
        uint32_t sum = 0;
        for (uint32_t position = 1; position <= 12; ++position) {
            sum += blob.w[position];
            packed[position >> 3] |= sum << ((position & 7) * 4);
        }
        keys.push_back(Key{{packed[0], packed[1], base.w[2]}});
    }
}

// 0xaca5a0: with the entry's byte5c set, the key with its low flag bits cleared (mask 0xf0007ff9), then with the
// related format `a` (bit 1), `b` (bit 2) and both (bit 1 and 2) set into its fields.
void AddFlagKeys(std::vector<Key> &keys, const cMaterialManager::Entry &entry, const Key &base, uint32_t a, uint32_t b)
{
    if (entry.byte5c == 0) return;
    const uint32_t cleared = base.w[2] & 0xf0007ff9;
    keys.push_back(Key{{base.w[0], base.w[1], cleared}});
    if (a != 0) keys.push_back(Key{{base.w[0], base.w[1], ((a & 0x7f) << 15) | cleared | 2}});
    if (b != 0) keys.push_back(Key{{base.w[0], base.w[1], ((b & 0x3f) << 22) | cleared | 4}});
    if (a != 0 && b != 0)
        keys.push_back(Key{{base.w[0], base.w[1], ((((b & 0x3f) << 7) | (a & 0x7f)) << 15) | cleared | 6}});
}

// 0xaca4d0: the keys implied by `key` with its field at bits 11-14 set to `bits`: the blob keys, the flag keys, then the
// blob keys of the flag keys.
void AddRelatedKeys(std::vector<Key> &keys, const cMaterialManager::Entry &entry, const Key &key, uint32_t bits,
                    uint32_t a, uint32_t b)
{
    Key changed = key;
    changed.w[2] = ((bits & 0xf) << 11) | (key.w[2] & 0xffff87ff);
    AddBlobKeys(keys, entry, changed);
    const size_t first = keys.size();
    AddFlagKeys(keys, entry, changed, a, b);
    const size_t last = keys.size();
    for (size_t i = first; i < last; ++i) {
        const Key flagged = keys[i];
        AddBlobKeys(keys, entry, flagged);
    }
}

} // namespace

// 0xacd2d0 (thiscall, ret 0x14).
cIGZUnknown *cMaterialManager::GetVariant(uint32_t index, const uint32_t *keyWords, uint32_t queue, uint32_t refA,
                                          uint32_t refB)
{
    Entry *const entry = mByIndex[index];
    const VariantKey &key = *reinterpret_cast<const VariantKey *>(keyWords);
    if (entry->variants.find(key) == entry->variants.end()) {
        BuildVariant(entry, &key, true);
        if (static_cast<uint8_t>(queue) != 0) {
            std::vector<Key> related;
            AddRelatedKeys(related, *entry, key, key.w[2] >> 11 & 0xf, refA, refB);
            for (uint32_t bits : entry->words) AddRelatedKeys(related, *entry, key, bits, refA, refB);
            std::set<Key, KeyBytesLess> wanted;
            for (const Key &candidate : related)
                if (entry->variants.find(candidate) == entry->variants.end()) wanted.insert(candidate);
            if (!wanted.empty()) {
                NameCell name; // the cell of no name, then the entry's name put in (the old value, none, released)
                name.Assign(&entry->name.mValue);
                Ref<cIGZUnknown> format;
                // receiver: nGZGraphic4::cVertexFormatsManager (slot 8 is 0x16db10: thiscall, one stack argument, ret 4, returns the element)
                format.Assign(RawSlot<8, cIGZUnknown *>(nGZGraphic4::GetVertexFormatsManagerCached(), key.w[2] >> 4 & 0x7f));
                for (const Key &wantedKey : wanted) {
                    mJobs.emplace_back(name, wantedKey);
                    mJobs.back().format = format;
                }
            }
        }
    }
    const auto found = entry->variants.find(key);
    // ORIGINAL-BUG: a key BuildVariant could not make a variant for (the parser failed) reads the variant table's head
    // node, whose value word the original never sets: it returns whatever that memory held. Ours returns null.
    return found == entry->variants.end() ? nullptr : found->second.p;
}

} // namespace nGZSceneGraph
