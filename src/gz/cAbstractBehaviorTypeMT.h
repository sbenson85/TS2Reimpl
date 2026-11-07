#pragma once
// nGZEvent::cAbstractBehaviorTypeT<I, nGZEvent::cRefMultiThreaded> (lane NQ): the event behaviour template the graphics
// classes derive from, as one C++ template. Every instantiation in the game is its own copy of the same bodies; what
// differs between them is the connection-manager cache cell, the event attribute registry (a function-local static map
// per instantiation) and the record a missing event reads as. `Traits` supplies those:
//
//   struct Traits {
//       static constexpr uint32_t kEventManagerCell = 0x...; // GetEventManager's cache
//       static constexpr uint32_t kMissingAttribute = 0x...; // optional (else behavior_mt::gMissingRecord, owned)
//       static nGZEvent::cEventAttributeMap *Registry();     // the instantiation's registry (game code, by literal RVA)
//   };
//
// A Traits whose missing record is an owned static (game/owned.h) names it instead of giving kMissingAttribute:
//   static const nGZEvent::cEventAttribute &MissingAttributeRecord() { return ts2_rva_<hex>; }
//
// The behaviour: a count at +4 whose low 31 bits count references (atomically, RZAtomicAdd) and whose top bit says
// "someone listens to me" (kept in step by OnEvent when the connection manager reports a change: SyncActive); Notify /
// Ask walk this object's listeners through the connection manager; Connect / Disconnect forward to it. Slots 7 and 10
// do nothing (shared 0x377e0), slot 4 asks the listeners (shared 0x147ce0), slot 11 is the top bit (shared 0x147cf0).
// cDeviceDX (lane NC) holds the same bodies written out; this header is the shared form (cGraphicSystemDX,
// cGZGraphicSystem, cGZCanvas).
//
// `Iface` is the class's primary interface (nGZEvent::cIAbstractBehavior or one derived from it) and must declare the
// virtual destructor at the slot the class's deleting destructor has: the last Release deletes through it.
#include "game/game.h"
#include "game/original.h"
#include "gz/cAttributeIDIterator.h"
#include "gz/cConnectionManager.h"
#include "gz/cGZEventReflector.h" // cIAbstractBehavior, cEventAttribute, cAttributeIDRange, cEventServiceRef
#include "gz/cIGZUnknown.h"
#include "rz/cRZRefCount.h"       // RZAtomicAdd
#include "rz/cRZString.h"         // the empty text (ts2_rva_d9b9d0)

#include <cstdint>
#include <vector>

namespace nGZEvent {

namespace behavior_mt {

constexpr uint32_t kCountMask = 0x7fffffff;
constexpr uint32_t kActiveBit = 0x80000000;
constexpr uint32_t kEmptyText = 0xd9b9d0; // the registry default record's name

// The id list GetEventIds hands out (cRZRefCountedT<std::vector<unsigned long>>, game vtable 0xd9bcfc): counted at
// +0x10, deleted at the last Release. The iterators (nGZEvent::cAttributeIDIterator, constructor 0x1a9942) hold a
// reference each.
class EventIdList : public cIGZUnknown {
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
    virtual ~EventIdList() = default; // slot 3
};
static_assert(sizeof(EventIdList) == 0x14, "EventIdList size");

// One iterator over the list at `cursor` (ours of 0x1a9942, gz/cAttributeIDIterator.h).
inline cIAttributeIDIterator *NewIdIterator(EventIdList *ids, const uint32_t *cursor)
{
    return NewAttributeIDIterator(ids, cursor);
}

// The record slot 14 answers for an unknown event when the Traits name none: all the image's per-instantiation records
// are {0, the empty text, 0, 0, 0, 0}, only ever copied out by the GetEventAttribute bodies (a by-value return, so the
// record's address is never compared). A variable (not a constant) only so a test can plant a pattern in it and see
// which record is returned; nothing else writes it.
inline cEventAttribute gMissingRecord = {0, ts2_rva_d9b9d0, 0, 0, 0, 0};

// The registry's default record, as every instantiation's callers build it: {0, the empty text, 0, 0, 0, 0}.
inline cEventAttribute DefaultAttribute() { return {0, ts2_rva_d9b9d0, 0, 0, 0, 0}; }

// The listener range of (behaviour, event) from the connection manager, released at scope end as 0x146670 does.
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

} // namespace behavior_mt

template <class Iface, class Traits> class cAbstractBehaviorTypeMT : public Iface {
public:
    cAbstractBehaviorTypeMT() = default;
    cAbstractBehaviorTypeMT(const cAbstractBehaviorTypeMT &) = delete;
    cAbstractBehaviorTypeMT &operator=(const cAbstractBehaviorTypeMT &) = delete;

    // shared 0x146a20 (cRefMultiThreaded): the low 31 bits count, atomically.
    uint32_t AddRef() override
    {
        RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), 1);
        return mRefCount & behavior_mt::kCountMask;
    }

    // The last reference disconnects the object from everything (when there is a manager), then deletes it through
    // its virtual (deleting) destructor; otherwise the count goes down atomically.
    uint32_t Release() override
    {
        if ((mRefCount & behavior_mt::kCountMask) == 1) {
            if (GetEventManager()) GetEventManager()->DisconnectAll(this);
            delete this;
            return 0;
        }
        RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), -1);
        return mRefCount & behavior_mt::kCountMask;
    }

    // shared 0x147ce0: the query goes to the listeners (slot 6).
    bool OnQuery(uint32_t type, uint32_t arg1, void *data) override { return this->AskListeners(type, arg1, data); }

    // Every listener of `type` gets (its event, arg1, data, this) through its slot 3, while active.
    void NotifyListeners(uint32_t type, uint32_t arg1, void *data) override
    {
        if (static_cast<int32_t>(mRefCount) >= 0) return;
        behavior_mt::ListenerRange range(GetEventManager(), this, type);
        while (!range.AtEnd()) {
            cIAbstractBehavior *const listener = range.Listener();
            listener->OnEvent(range.ListenerEvent(), arg1, data, reinterpret_cast<uintptr_t>(static_cast<cIGZUnknown *>(this)));
            range.Next();
        }
    }

    // True when there is a listener of `type` and every one answered its slot 4 with true (all are asked).
    bool AskListeners(uint32_t type, uint32_t arg1, void *data) override
    {
        if (static_cast<int32_t>(mRefCount) >= 0) return false;
        bool any = false;
        bool every = true;
        {
            behavior_mt::ListenerRange range(GetEventManager(), this, type);
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

    void SetAttribute(uint32_t, uint32_t, uint32_t) override {} // shared 0x377e0

    void Connect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent) override
    {
        GetEventManager()->Connect(this, event, target, targetEvent);
    }

    void Disconnect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent) override
    {
        GetEventManager()->Disconnect(this, event, target, targetEvent);
    }

    void GetAttribute(uint32_t, uint32_t, uint32_t) override {} // shared 0x377e0

    uint32_t IsActive() override { return mRefCount >> 31; } // shared 0x147cf0

    uint32_t GetEventCount() override { return static_cast<uint32_t>(Traits::Registry()->size()); }

    // Every registered id, in order (the end iterator is made first).
    cAttributeIDRange GetEventIds() override
    {
        behavior_mt::EventIdList *const ids = new behavior_mt::EventIdList;
        ids->AddRef();
        for (auto it = Traits::Registry()->begin(); it != Traits::Registry()->end(); ++it) ids->mIds.push_back(it->first);
        cIAttributeIDIterator *const last = behavior_mt::NewIdIterator(ids, ids->mIds.data() + ids->mIds.size());
        cIAttributeIDIterator *const first = behavior_mt::NewIdIterator(ids, ids->mIds.data());
        const cAttributeIDRange range = MakeIteratorRange(first, last);
        ids->Release();
        return range;
    }

    // The event's record, or the instantiation's static "missing" record.
    cEventAttribute GetEventAttribute(uint32_t event) override
    {
        const auto found = Traits::Registry()->find(event);
        if (found != Traits::Registry()->end()) return found->second;
        if constexpr (requires { Traits::MissingAttributeRecord(); })
            return Traits::MissingAttributeRecord();
        else if constexpr (requires { Traits::kMissingAttribute; })
            return *GameAddress<cEventAttribute>(Traits::kMissingAttribute);
        else
            return behavior_mt::gMissingRecord;
    }

    // The instantiation's connection manager cell: the traits' owned static where they name one (EventManagerCell),
    // else the game's cell at kEventManagerCell.
    static cIConnectionManager *&CellOf()
    {
        if constexpr (requires { Traits::EventManagerCell(); })
            return Traits::EventManagerCell();
        else
            return *GameAddress<cIConnectionManager *>(Traits::kEventManagerCell);
    }

    // The connection manager, cached in the instantiation's cell once the event system service gave one.
    static cIConnectionManager *GetEventManager()
    {
        cIConnectionManager *&cell = CellOf();
        if (!cell) {
            cEventServiceRef service;
            if (service.mRef.mObject) cell = service.GetConnectionManager();
        }
        return cell;
    }

protected:
    // OnEvent's cConnectionChangedEvent: the active bit follows whether anyone listens to this object (flipped
    // atomically when it differs).
    void SyncActive()
    {
        const bool listened = GetEventManager()->HasConnectionsFrom(this);
        if (static_cast<uint8_t>(mRefCount >> 31) != static_cast<uint8_t>(listened))
            RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), static_cast<int32_t>(behavior_mt::kActiveBit));
    }

public:
    uint32_t mRefCount = 0; // +4: the count (low 31 bits) and the active bit
};

} // namespace nGZEvent
