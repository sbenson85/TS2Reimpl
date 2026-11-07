// nGZEvent::cConnectionManager and its iterators (lane LR): see gz/cConnectionManager.h.
#include "gz/cConnectionManager.h"

#include "game/game.h"
#include "gz/cEventFaces.h" // the event's vtable

#include <algorithm>
#include <new>
#include <utility>

namespace nGZEvent {

namespace {

// The connection under `key` whose far end is `info`: the first match in the key's run, else the run's end (the
// original's find_if over equal_range returns that end, the first connection with a greater key or the map's end).
Connections::iterator FindConnection(Connections &connections, uint32_t key, cConnectionInfo info)
{
    const auto range = connections.equal_range(key);
    return std::find_if(range.first, range.second, [&](const Connections::value_type &entry) {
        return entry.second.mObject == info.mObject && entry.second.mEvent == info.mEvent;
    });
}

// Both ends told about the change, the source first, with the same event (a receiver that rewrites the event's ends
// does not change who is told next).
void Notify(cIAbstractBehaviorType *source, cIAbstractBehaviorType *target, uint32_t sourceEvent, uint32_t targetEvent,
            uint32_t disconnected)
{
    cConnectionChangedEvent event{ConnectionChangedEventVTable(),
                                  disconnected,
                                  source,
                                  target,
                                  sourceEvent,
                                  targetEvent};
    source->OnEvent(cConnectionChangedEvent::kType, 0, &event, 0);
    target->OnEvent(cConnectionChangedEvent::kType, 0, &event, 0);
}

// The range object of slots 10 / 11 (the template constructor 0x1a98e0, shared by eight instantiations): the cursor is a
// clone of begin (counted when there is one), then begin (counted, never null here) and end (counted when non-null).
cConnectionRange MakeRange(cIConnectionIterator *begin, cIConnectionIterator *end)
{
    cConnectionRange range;
    range.mCurrent = begin->Clone();
    if (range.mCurrent) range.mCurrent->AddRef();
    range.mBegin = begin;
    begin->AddRef();
    range.mEnd = end;
    if (end) end->AddRef();
    return range;
}

// Slots 10 / 11: the key's run under `object` in `table`; two null iterators when the object has no entry.
cConnectionRange RangeOf(ConnectionTable &table, cIAbstractBehaviorType *object, uint32_t key)
{
    const auto found = table.find(object);
    if (found == table.end()) {
        cIConnectionIterator *end = new cNullConnectionIterator;
        cIConnectionIterator *begin = new cNullConnectionIterator;
        return MakeRange(begin, end);
    }
    const auto run = found->second.equal_range(key);
    cIConnectionIterator *end = new cConnectionIterator(run.second);
    cIConnectionIterator *begin = new cConnectionIterator(run.first);
    return MakeRange(begin, end);
}

uint32_t CountOf(ConnectionTable &table, cIAbstractBehaviorType *object, uint32_t key)
{
    const auto found = table.find(object);
    return found == table.end() ? 0 : uint32_t(found->second.count(key));
}

} // namespace

// ---- cConnectionManager -----------------------------------------------------------------------------------------------

cConnectionManager::~cConnectionManager() = default;

cIAbstractBehaviorType *cConnectionManager::AsBehavior(cIGZUnknown *object)
{
    void *face = object;
    object->QueryInterface(cIAbstractBehaviorType::kIid, &face);
    return static_cast<cIAbstractBehaviorType *>(face);
}

bool cConnectionManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cConnectionManager::AddRef() { return 1; }
uint32_t cConnectionManager::Release() { return 1; }

void cConnectionManager::Connect(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target, uint32_t targetEvent)
{
    cIAbstractBehaviorType *const from = AsBehavior(source);
    cIAbstractBehaviorType *const to = AsBehavior(target);
    Connections &outgoing = mFrom[from];
    Connections &incoming = mTo[to];
    outgoing.emplace(sourceEvent, cConnectionInfo{to, targetEvent});
    incoming.emplace(targetEvent, cConnectionInfo{from, sourceEvent});
    Notify(from, to, sourceEvent, targetEvent, 0);
}

void cConnectionManager::Disconnect(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target, uint32_t targetEvent)
{
    cIAbstractBehaviorType *const from = AsBehavior(source);
    cIAbstractBehaviorType *const to = AsBehavior(target);
    Connections &outgoing = mFrom[from];
    const auto out = FindConnection(outgoing, sourceEvent, {to, targetEvent});
    Connections &incoming = mTo[to];
    const auto in = FindConnection(incoming, targetEvent, {from, sourceEvent});
    // ORIGINAL-BUG: the original erases what its search returned even when nothing matched: the next connection with a
    // greater key (a different connection, which is lost from that map only) or the map's end (its head node: the tree
    // is corrupted). The first is reproduced; the head is not erased.
    if (out != outgoing.end()) outgoing.erase(out);
    if (in != incoming.end()) incoming.erase(in);
    if (outgoing.empty()) mFrom.erase(from);
    if (incoming.empty()) mTo.erase(to);
    Notify(from, to, sourceEvent, targetEvent, 1);
}

void cConnectionManager::DisconnectAll(cIGZUnknown *object)
{
    cIAbstractBehaviorType *const self = AsBehavior(object);
    while (HasConnectionsFrom(self)) {
        const Connections::value_type first = *mFrom[self].begin();
        Disconnect(self, first.first, first.second.mObject, first.second.mEvent);
    }
    while (HasConnectionsTo(self)) {
        const Connections::value_type first = *mTo[self].begin();
        Disconnect(first.second.mObject, first.second.mEvent, self, first.first);
    }
}

bool cConnectionManager::HasConnectionsFrom(cIGZUnknown *source) { return mFrom.find(AsBehavior(source)) != mFrom.end(); }

bool cConnectionManager::HasConnectionsTo(cIGZUnknown *target) { return mTo.find(AsBehavior(target)) != mTo.end(); }

uint32_t cConnectionManager::CountFrom(cIGZUnknown *source, uint32_t sourceEvent)
{
    return CountOf(mFrom, AsBehavior(source), sourceEvent);
}

uint32_t cConnectionManager::CountTo(cIGZUnknown *target, uint32_t targetEvent)
{
    return CountOf(mTo, AsBehavior(target), targetEvent);
}

cConnectionRange cConnectionManager::ConnectionsFrom(cIGZUnknown *source, uint32_t sourceEvent)
{
    return RangeOf(mFrom, AsBehavior(source), sourceEvent);
}

cConnectionRange cConnectionManager::ConnectionsTo(cIGZUnknown *target, uint32_t targetEvent)
{
    return RangeOf(mTo, AsBehavior(target), targetEvent);
}

bool cConnectionManager::IsConnected(cIGZUnknown *source, uint32_t sourceEvent, cIGZUnknown *target, uint32_t targetEvent)
{
    cIAbstractBehaviorType *const from = AsBehavior(source);
    cIAbstractBehaviorType *const to = AsBehavior(target);
    const auto found = mFrom.find(from);
    if (found == mFrom.end()) return false;
    return FindConnection(found->second, sourceEvent, {to, targetEvent}) != found->second.upper_bound(sourceEvent);
}

// ---- cConnectionIterator ----------------------------------------------------------------------------------------------

bool cConnectionIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cConnectionIterator::AddRef() { return ++mRefCount; }

uint32_t cConnectionIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIConnectionIterator *cConnectionIterator::Clone() { return new cConnectionIterator(*this); }

bool cConnectionIterator::IsEqual(const cIConnectionIterator *other)
{
    return static_cast<const cConnectionIterator *>(other)->mNode == mNode;
}

cConnectionInfo cConnectionIterator::Get() { return mNode->second; }

void cConnectionIterator::Next() { ++mNode; }

// ---- cNullConnectionIterator ------------------------------------------------------------------------------------------

bool cNullConnectionIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cNullConnectionIterator::AddRef() { return ++mRefCount; }

uint32_t cNullConnectionIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cIConnectionIterator *cNullConnectionIterator::Clone() { return new cNullConnectionIterator; }

bool cNullConnectionIterator::IsEqual(const cIConnectionIterator *) { return true; }

cConnectionInfo cNullConnectionIterator::Get() { return {nullptr, 0}; }

void cNullConnectionIterator::Next() {}

void cNullConnectionIterator::Previous() {}

void cNullConnectionIterator::Advance(int32_t) {}

cConnectionInfo cNullConnectionIterator::At(int32_t) { return Get(); }

// ---- adapters ---------------------------------------------------------------------------------------------------------

cConnectionManager *__fastcall cConnectionManagerAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cConnectionManager;
}

void __fastcall cConnectionManagerAdapters::Destruct(cConnectionManager *self, void *)
{
    self->cConnectionManager::~cConnectionManager();
}

} // namespace nGZEvent
