// nGZEvent::cGZEventReflector and its behaviour base (lane LT): src/gz/cGZEventReflector.h.
#include "gz/cGZEventReflector.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cAttributeIDIterator.h"
#include "gz/cEventSystemService.h"
#include "gz/cEventFaces.h" // the null event's vtable
#include "gz/cIGZMessage2Standard.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZFrameworkHelpers.h" // RZUnknownSlot
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h" // the empty text

#include <algorithm>
#include <optional>
#include <new>

namespace nGZEvent {

namespace {

constexpr uint32_t kEventManagerCell = 0x10cadc8; // GetEventManager's cache
constexpr uint32_t kMissingAttribute = 0xff2374;  // the record slot 14 hands out for an unknown event

// The id list GetEventIds hands out (cRZRefCountedT<std::vector<unsigned long>>, game vtable 0xd9bcfc): counted at
// +0x10, deleted at the last Release. The iterators (the original's cAttributeIDIterator, constructor 0x1a9942) hold a
// reference each.
class cAttributeIDVector : public cIGZUnknown {
public:
    std::vector<uint32_t> mIds; // +4
    uint32_t mRefCount = 0;     // +0x10
    bool QueryInterface(uint32_t riid, void **out) override
    {
        if (riid != 1) return false;
        *out = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mRefCount; }
    uint32_t Release() override
    {
        if (mRefCount > 1) return --mRefCount;
        delete this;
        return 0;
    }
    virtual ~cAttributeIDVector() = default; // slot 3
};
static_assert(sizeof(cAttributeIDVector) == 0x14, "cAttributeIDVector size");

// The listener range of (behaviour, event) from the connection manager (slot 10), released at scope end as 0x146670
// does: the end, the begin, then the cursor, each when there is one.
struct ListenerRange {
    cConnectionRange r;
    ListenerRange(cIConnectionManager *manager, cIGZUnknown *source, uint32_t event)
        : r(manager->ConnectionsFrom(source, event))
    {
    }
    ListenerRange(const ListenerRange &) = delete;
    ListenerRange &operator=(const ListenerRange &) = delete;
    ~ListenerRange() { ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&r)); }
    bool AtEnd() const { return r.mCurrent->IsEqual(r.mEnd); }
    // The listener and the event it listens with (the original reads the cursor twice: once for each).
    cIAbstractBehavior *Listener() const { return static_cast<cIAbstractBehavior *>(r.mCurrent->Get().mObject); }
    uint32_t ListenerEvent() const { return r.mCurrent->Get().mEvent; }
    void Next() const { r.mCurrent->Next(); }
};

// The range of a counted id list: two iterators over it (ours of 0x1a9942; end first), handed to the range constructor.
cAttributeIDRange MakeIdRange(cAttributeIDVector *ids)
{
    cIAttributeIDIterator *last = nullptr;
    if (void *const memory = ::operator new(0x10)) {
        const uint32_t *const cursor = ids->mIds.data() + ids->mIds.size();
        last = reinterpret_cast<cIAttributeIDIterator *>(static_cast<cAttributeIDIterator *>(memory)->Construct(ids, &cursor));
    }
    cIAttributeIDIterator *first = nullptr;
    if (void *const memory = ::operator new(0x10)) {
        const uint32_t *const cursor = ids->mIds.data();
        first = reinterpret_cast<cIAttributeIDIterator *>(static_cast<cAttributeIDIterator *>(memory)->Construct(ids, &cursor));
    }
    return MakeIteratorRange(first, last);
}

cAttributeIDVector *NewIdVector()
{
    cAttributeIDVector *const ids = new cAttributeIDVector;
    if (ids) ids->AddRef();
    return ids;
}

} // namespace

// ---- free helpers ----

// 0x1aa148.
void __cdecl SyncActiveBit(uint32_t *count, bool active)
{
    if (static_cast<uint8_t>(*count >> 31) != static_cast<uint8_t>(active)) *count -= cAbstractBehaviorType::kActiveBit;
}

// 0x1a98e0: the cursor is a counted clone of begin (begin is assumed), then begin and end counted.
cAttributeIDRange MakeIteratorRange(cIAttributeIDIterator *begin, cIAttributeIDIterator *end)
{
    cAttributeIDRange range;
    range.mCurrent = begin->Clone();
    if (range.mCurrent) range.mCurrent->AddRef();
    range.mBegin = begin;
    begin->AddRef();
    range.mEnd = end;
    if (end) end->AddRef();
    return range;
}

// 0x146670.
void ReleaseIteratorRange(cIGZUnknown *const range[3]) noexcept
{
    if (range[2]) range[2]->Release();
    if (range[1]) range[1]->Release();
    if (range[0]) range[0]->Release();
}

// 0x1a991d.
cEventServiceRef *cEventServiceRef::Acquire()
{
    mRef.mObject = nullptr;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(cEventSystemService::kServiceId,
                                                               cIEventSystemService::kIid, &mRef.mObject);
    return this;
}

cIConnectionManager *cEventServiceRef::GetConnectionManager() const
{
    return static_cast<cIEventSystemService *>(mRef.mObject)->GetConnectionManager();
}

// ---- the behaviour base ----

namespace {

// The registry: the original keeps it as a function-local static map (0x10c94f4, built under the thread-safe-static
// guards on first use, with a small leaked holder object whose constructor stores the two base events); ours is an
// optional of the same map type. Nothing else in the image touches that static.
std::optional<cEventAttributeMap> gBaseRegistry;

constexpr cEventAttribute kConnectionChanged{0x5b51e806, "ConnectionChanged", 1, 0xdb27d6d4, 2, 4};
constexpr cEventAttribute kAttributeInfosUpdated{0xe9dd42cf, "AttributeInfosUpdated", 1, 0x7b256782, 2, 0xc};

} // namespace

// 0x1480d0: the registry (id 0: nothing stored; the default record).
cEventAttributeMap *cAbstractBehaviorType::Registry()
{
    const cEventAttribute defaults{0, ts2_rva_d9b9d0, 0, 0, 0, 0};
    return RegistryFor(0, &defaults);
}

// 0x1485f0.
cEventAttributeMap *__cdecl cAbstractBehaviorType::RegistryFor(uint32_t id, const cEventAttribute *attribute)
{
    if (!gBaseRegistry) {
        gBaseRegistry.emplace();
        (*gBaseRegistry)[kConnectionChanged.mId] = kConnectionChanged;
        (*gBaseRegistry)[kAttributeInfosUpdated.mId] = kAttributeInfosUpdated;
    }
    if (id != 0) (*gBaseRegistry)[id] = *attribute;
    return &*gBaseRegistry;
}

// 0x1464f0 (it calls 0x146580 / 0x1465d0's halves in turn: one event each).
void cAbstractBehaviorType::RegisterBaseEvents()
{
    RegistryFor(kConnectionChanged.mId, &kConnectionChanged);
    RegistryFor(kAttributeInfosUpdated.mId, &kAttributeInfosUpdated);
}

void *__fastcall cAbstractBehaviorType::RegisterBaseEventsAdapter(void *self, void *)
{
    RegisterBaseEvents();
    return self;
}

// 0x1465d0.
void *__fastcall cAbstractBehaviorType::RegisterConnectionChangedAdapter(void *self, void *)
{
    RegistryFor(kConnectionChanged.mId, &kConnectionChanged);
    return self;
}

// 0x146580.
void *__fastcall cAbstractBehaviorType::RegisterAttributeInfosUpdatedAdapter(void *self, void *)
{
    RegistryFor(kAttributeInfosUpdated.mId, &kAttributeInfosUpdated);
    return self;
}

void cAbstractBehaviorType::ResetRegistryForTest() { gBaseRegistry.reset(); }

// 0x1a9d01.
cIConnectionManager *cAbstractBehaviorType::GetEventManager()
{
    cIConnectionManager *&cell = *GameAddress<cIConnectionManager *>(kEventManagerCell);
    if (!cell) {
        cEventServiceRef service;
        if (service.mRef.mObject) cell = service.GetConnectionManager();
    }
    return cell;
}

// shared_vf_1aa007: the behaviour itself for 1 and kBehaviorIid2 (counted) and for the behaviour face (not counted).
bool cAbstractBehaviorType::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == 1 || riid == kBehaviorIid2) {
        *out = static_cast<cIAbstractBehavior *>(this);
        AddRef();
        return true;
    }
    if (riid == cIAbstractBehaviorType::kIid) {
        *out = static_cast<cIAbstractBehavior *>(this);
        return true;
    }
    return false;
}

// 0x1a9a25.
uint32_t cAbstractBehaviorType::AddRef() { return ++mRefCount & kCountMask; }

// 0x1aa071: the last reference disconnects the behaviour from everything (when there is a manager), then deletes it.
uint32_t cAbstractBehaviorType::Release()
{
    if ((mRefCount & kCountMask) == 1) {
        if (GetEventManager()) GetEventManager()->DisconnectAll(static_cast<cIAbstractBehavior *>(this));
        delete this;
        return 0;
    }
    --mRefCount;
    return mRefCount & kCountMask;
}

// 0x1a9e32: a connection change makes the active bit say whether anything listens to this behaviour.
void cAbstractBehaviorType::OnEvent(uint32_t type, uint32_t, void *, uint32_t)
{
    if (type != cConnectionChangedEvent::kType) return;
    SyncActiveBit(&mRefCount, GetEventManager()->HasConnectionsFrom(static_cast<cIAbstractBehavior *>(this)));
}

// shared_vf_1a9f00.
bool cAbstractBehaviorType::OnQuery(uint32_t type, uint32_t arg1, void *data) { return AskListeners(type, arg1, data); }

// 0x1aa0c4: every listener of `type` gets (its event, arg1, data, this) through its slot 3, while active.
void cAbstractBehaviorType::NotifyListeners(uint32_t type, uint32_t arg1, void *data)
{
    if (static_cast<int32_t>(mRefCount) >= 0) return;
    ListenerRange range(GetEventManager(), static_cast<cIAbstractBehavior *>(this), type);
    while (!range.AtEnd()) {
        cIAbstractBehavior *const listener = range.Listener();
        listener->OnEvent(range.ListenerEvent(), arg1, data, reinterpret_cast<uintptr_t>(static_cast<cIAbstractBehavior *>(this)));
        range.Next();
    }
}

// 0x1a9d56: true when there is a listener of `type` and every one answered its slot 4 with true (all are asked).
bool cAbstractBehaviorType::AskListeners(uint32_t type, uint32_t arg1, void *data)
{
    if (static_cast<int32_t>(mRefCount) >= 0) return false;
    bool any = false;
    bool every = true;
    {
        ListenerRange range(GetEventManager(), static_cast<cIAbstractBehavior *>(this), type);
        while (!range.AtEnd()) {
            cIAbstractBehavior *const listener = range.Listener();
            const bool yes = listener->OnQuery(range.ListenerEvent(), arg1, data);
            every = yes && every;
            any = true;
            range.Next();
        }
    }
    return every && any;
}

// 0x1a9ce1.
void cAbstractBehaviorType::Connect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    GetEventManager()->Connect(static_cast<cIAbstractBehavior *>(this), event, target, targetEvent);
}

// 0x1a9d36.
void cAbstractBehaviorType::Disconnect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    GetEventManager()->Disconnect(static_cast<cIAbstractBehavior *>(this), event, target, targetEvent);
}

// shared 0x147cf0.
uint32_t cAbstractBehaviorType::IsActive() { return mRefCount >> 31; }

// shared_vf_1a9ff1.
uint32_t cAbstractBehaviorType::GetEventCount() { return static_cast<uint32_t>(Registry()->size()); }

// 0x1a9a41: every registered id, in order.
cAttributeIDRange cAbstractBehaviorType::GetEventIds()
{
    cAttributeIDVector *const ids = NewIdVector();
    for (auto it = Registry()->begin(); it != Registry()->end(); ++it) ids->mIds.push_back(it->first);
    const cAttributeIDRange range = MakeIdRange(ids);
    if (ids) ids->Release();
    return range;
}

// 0x1a9bd9.
cEventAttribute cAbstractBehaviorType::GetEventAttribute(uint32_t event)
{
    const auto found = Registry()->find(event);
    return found != Registry()->end() ? found->second : *GameAddress<cEventAttribute>(kMissingAttribute);
}

// ---- the reflector ----

// 0x1a99c8: the records freed, the target released (members in reverse order), then the base.
cGZEventReflector::~cGZEventReflector()
{
    // the members do the work
}

// 0x1a9e0b.
cIGZEventReflector *__cdecl cGZEventReflector::Create()
{
    return new cGZEventReflector; // the face of a null result is null
}

cGZEventReflector *__fastcall cGZEventReflectorAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZEventReflector;
}

cEventServiceRef *__fastcall cGZEventReflectorAdapters::AcquireEventService(cEventServiceRef *self, void *)
{
    return self->Acquire();
}

cAttributeIDRange *__fastcall cGZEventReflectorAdapters::MakeIteratorRange(cAttributeIDRange *out, void *,
                                                                          cIAttributeIDIterator *begin,
                                                                          cIAttributeIDIterator *end)
{
    *out = nGZEvent::MakeIteratorRange(begin, end);
    return out;
}

void __fastcall cGZEventReflectorAdapters::ReleaseIteratorRange(cIGZUnknown *const *range, void *)
{
    nGZEvent::ReleaseIteratorRange(range);
}

// 0x1aa03e: the reflector face (counted), else the base's answers.
bool cGZEventReflector::QueryInterface(uint32_t riid, void **out)
{
    if (riid != cIGZEventReflector::kIid) return cAbstractBehaviorType::QueryInterface(riid, out);
    *out = static_cast<cIGZEventReflector *>(this);
    AddRef();
    return true;
}

uint32_t cGZEventReflector::Release() { return cAbstractBehaviorType::Release(); }

// 0x1a98af.
const cEventAttribute *cGZEventReflector::FindRecord(const cEventAttribute *first, const cEventAttribute *last,
                                                     uint32_t event)
{
    return std::find_if(first, last, [event](const cEventAttribute &record) { return record.mId == event; });
}

const cEventAttribute *cGZEventReflector::FindReflected(uint32_t event) const
{
    return FindRecord(mReflected.data(), mReflected.data() + mReflected.size(), event);
}

// A new cRZMessage2Standard (RZMemory) of kMessageType carrying (event, arg1, data), counted.
cIGZUnknown *cGZEventReflector::NewMessage(uint32_t event, uint32_t arg1, void *data)
{
    cIGZMessage2Standard *message = nullptr;
    if (void *const memory = RZMemory::Malloc(cRZMessage2Standard::kSize))
        message = reinterpret_cast<cIGZMessage2Standard *>(static_cast<cRZMessage2Standard *>(memory)->Construct());
    if (message) message->AddRef();
    // ORIGINAL-BUG: a failed allocation is not checked: the null message is called.
    message->SetType(kMessageType);
    message->SetData1(event);
    message->SetData2(arg1);
    message->SetVoid3(data);
    return message;
}

// 0x1a9e5f: a reflected event goes to the target as a message (the target's answer ignored), then to the base.
void cGZEventReflector::OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3)
{
    if (FindReflected(type) != mReflected.data() + mReflected.size()) {
        cIGZUnknown *const message = NewMessage(type, arg1, data);
        mTarget.mObject->DoMessage(message); // the target is assumed
        if (message) message->Release();
    }
    cAbstractBehaviorType::OnEvent(type, arg1, data, arg3);
}

// 0x1a9f05: a reflected query the target handles is answered true; anything else is asked of the listeners.
bool cGZEventReflector::OnQuery(uint32_t type, uint32_t arg1, void *data)
{
    if (FindReflected(type) != mReflected.data() + mReflected.size()) {
        cIGZUnknown *const message = NewMessage(type, arg1, data);
        const bool handled = mTarget.mObject->DoMessage(message);
        if (message) message->Release();
        if (handled) return true;
    }
    return cAbstractBehaviorType::OnQuery(type, arg1, data);
}

// 0x1a9ffa.
uint32_t cGZEventReflector::GetEventCount() { return static_cast<uint32_t>(mReflected.size()); }

// 0x1a9b15: the reflected events' ids, in order.
cAttributeIDRange cGZEventReflector::GetEventIds()
{
    cAttributeIDVector *const ids = NewIdVector();
    for (const cEventAttribute &record : mReflected) ids->mIds.push_back(record.mId);
    const cAttributeIDRange range = MakeIdRange(ids);
    if (ids) ids->Release();
    return range;
}

// 0x1a9c11.
cEventAttribute cGZEventReflector::GetEventAttribute(uint32_t event)
{
    const cEventAttribute *const found = FindReflected(event);
    return found != mReflected.data() + mReflected.size() ? *found : *GameAddress<cEventAttribute>(kMissingAttribute);
}

// 0x1a9fb2.
void cGZEventReflector::ReflectRegistered()
{
    for (auto it = Registry()->begin(); it != Registry()->end(); ++it) mReflected.push_back(it->second);
}

// 0x1aa166: through a fresh service reference (not the cached manager).
void cGZEventReflector::Reset()
{
    cEventServiceRef service;
    if (service.mRef.mObject) service.GetConnectionManager()->DisconnectAll(static_cast<cIAbstractBehavior *>(this));
    mReflected.clear();
    cGZEventReflector::SetTarget(nullptr);
}

// shared_vf_1aa15e.
void *cGZEventReflector::SetTarget(cIGZMessageTarget2 *target)
{
    return reinterpret_cast<RZUnknownSlot *>(&mTarget.mObject)->Assign(target);
}

// 0x1a9c4a: the source's record of `event` (none when its id is 0); a first one is kept (word +0x14 4) and announced to
// this reflector's listeners (kReflectedEvent with a cNullEvent); then the source is connected to this reflector.
void cGZEventReflector::Reflect(cIAbstractBehavior *source, uint32_t event)
{
    const cEventAttribute record = source->GetEventAttribute(event);
    if (record.mId == 0) return;
    if (FindReflected(event) == mReflected.data() + mReflected.size()) {
        cEventAttribute added = record;
        added.mWord14 = kAddedWord14;
        mReflected.push_back(added);
        const void *nullEvent = nGZEvent::NullEventVTable();
        NotifyListeners(kReflectedEvent, 0, &nullEvent); // virtual, as the original
    }
    source->Connect(event, static_cast<cIAbstractBehavior *>(this), event);
}

} // namespace nGZEvent
