#include "ts/cTSPersistSpeculativeLoader.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "rz/RZAutoRef.h"
#include "rz/RZMemory.h"
#include "rz/TrivialAccessors.h"
#include "rz/cRZLock.h"
#include "rz/cRZFastCompression3.h"
#include "simantics/ServiceRefVector.h"
#include "rz/Singleton_20ddfb.h"

#include <algorithm>
#include <ctime>

#include <new>

namespace {

using Key = cGZPersistResourceKey;
using Request = SpecLoaderRequest;

using AddRefFn = uint32_t(__thiscall *)(void *);
using QueryInterfaceFn = bool(__thiscall *)(void *, uint32_t, void **);
using CallFn = void(__thiscall *)(void *);
using LockFn = int32_t(__thiscall *)(void *);

// A virtual call with a real narrow (bool) result: MSVC then loads the slot into a register before the call, as the
// original does.
template <int N, class R, class... A> struct NarrowSlot : vtable_detail::Pad<N> {
    virtual R __thiscall call(A...) = 0;
};

template <int N, class... A> inline bool NarrowCall(void *object, A... args)
{
    return static_cast<NarrowSlot<N, bool, A...> *>(object)->call(args...);
}

// A key compare is always field by field: type, group, instance, resource.
inline bool KeyEquals(const Key &a, const Key &b)
{
    return a.type == b.type && a.group == b.group && a.instance == b.instance && a.resource == b.resource;
}

// A counted reference held in a local (the "Abandon" holder): released through slot 2 when the scope ends.
struct CountedRef {
    void *object = nullptr;
    ~CountedRef()
    {
        if (object) VirtualCall<AddRefFn, 2>(object);
    }
};

// Drops a request's object: if it answers as a DB record, that record is told (slot 6, Close).
void Abandon(const SpecLoaderObjectRef &ref)
{
    CountedRef holder;
    if (ref.mObject && NarrowCall<0>(ref.mObject, SpecLoader::kDBRecordIid, &holder.object))
        VirtualCall<CallFn, 6>(holder.object);
}

// The resource manager, held as the original's RZAutoRef local: released with RZAutoRef::ReleaseObject (0x1466e0).
// Never null-checked after the lookup, as in the original.
struct ResourceManagerHolder {
    RZAutoRef ref{nullptr};

    ResourceManagerHolder()
    {
        if (void *service = GetServiceObject())
            VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(
                service, SpecLoader::kServiceId, SpecLoader::kResourceManagerIid, &ref.mObject);
    }
    ~ResourceManagerHolder() { ref.ReleaseObject(); }
    void *Get() const { return ref.mObject; }
    ResourceManagerHolder(const ResourceManagerHolder &) = delete;
    ResourceManagerHolder &operator=(const ResourceManagerHolder &) = delete;
};

using HasKeyFn = bool(__thiscall *)(void *, const Key *);
using GetUint32Fn = bool(__thiscall *)(void *, uint32_t *);
using GetPositionFn = uint32_t(__thiscall *)(void *);
using SetPositionFn = void(__thiscall *)(void *, uint32_t);

// Releases the object a holder has and clears the holder first: `if (p) { field = 0; p->Release(); }`.
inline void ResetHolder(void *&field)
{
    if (void *old = field) {
        field = nullptr;
        VirtualCall<AddRefFn, 2>(old);
    }
}

// `request.object = p` without the self check: AddRef the new object, store it, release the old one.
inline void AssignObject(SpecLoaderObjectRef &target, void *p)
{
    if (p != target.mObject) {
        if (p) VirtualCall<AddRefFn, 1>(p);
        void *old = target.mObject;
        target.mObject = p;
        if (old) VirtualCall<AddRefFn, 2>(old);
    }
}

// cPreloadedRecord (another class, anonymous namespace in the game, 0x58 bytes): a DB serial record whose bytes were read
// raw (possibly compressed) by the worker thread. Only what the loader touches; its base is constructed by the original
// our cGZPersistDBSerialRecord::Construct (0x99b7c0). Its face at +4 is what a request's object holds.
class PreloadedRecord {
public:
    explicit PreloadedRecord(const Key &key)
    {
        reinterpret_cast<cGZPersistDBSerialRecord *>(this)->Construct(&key, nullptr, 1);
        mVTable = GameAddress(0xda6f2c);
        mVTable4 = GameAddress(0xda7028);
        mVTable20 = GameAddress(0xda7080);
        mVTable30 = GameAddress(0xda70e4);
        mField44 = 0;
        mData = nullptr;
        mSize = 0;
        mFlags54 = 0;
    }

    const void *mVTable;     // +0x00
    const void *mVTable4;    // +0x04
    uint8_t mBase8[0x18];    // +0x08 (the base class's)
    const void *mVTable20;   // +0x20
    uint8_t mBase24[0xc];    // +0x24
    const void *mVTable30;   // +0x30
    uint8_t mBase34[0x10];   // +0x34
    uint32_t mField44;       // +0x44
    void *mData;             // +0x48 the raw bytes (RZMemory::Malloc)
    uint32_t mSize;          // +0x4c
    uint32_t mField50;       // +0x50 the segment's slot 21 answer for the key (the uncompressed size, per Update's use)
    union {
        uint16_t mFlags54;       // +0x54 cleared as one word
        struct {
            uint8_t mByte54;     // +0x54 unused
            uint8_t mCompressed; // +0x55 1 when the bytes are compressed (the packed segment's slot 41)
        };
    };
    uint8_t mPad56[2];
};
static_assert(sizeof(PreloadedRecord) == SpecLoader::kPreloadedRecordSize, "cPreloadedRecord size");

} // namespace

// ---- Queue ----

// 0x1f79f0
SpecLoaderRequest *cTSPersistSpeculativeLoaderQueue::Find(const cGZPersistResourceKey &key)
{
    for (SpecLoaderRequest &r : mList) {
        if (KeyEquals(r.key, key)) return &r;
    }
    return nullptr;
}

// 0x1f8260
bool cTSPersistSpeculativeLoaderQueue::Contains(const cGZPersistResourceKey &key)
{
    for (SpecLoaderRequest &r : mList) {
        if (KeyEquals(r.key, key)) return true;
    }
    return false;
}

// 0x1f7de0
bool cTSPersistSpeculativeLoaderQueue::PopBack(SpecLoaderRequest &out)
{
    if (mList.empty()) return false;
    out = mList.back();
    mList.pop_back();
    return true;
}

// 0x1f7900
bool cTSPersistSpeculativeLoaderQueue::Remove(const cGZPersistResourceKey &key)
{
    for (auto it = mList.begin(); it != mList.end(); ++it) {
        if (KeyEquals(it->key, key)) {
            Abandon(it->object);
            mList.erase(it);
            return true;
        }
    }
    return false;
}

// 0x1f7ae0 (a real call from the destructors, so not inlined)
__declspec(noinline) void cTSPersistSpeculativeLoaderQueue::Clear()
{
    while (!mList.empty()) {
        Abandon(mList.front().object);
        mList.pop_front();
    }
}

// 0x1f8800
bool cTSPersistSpeculativeLoaderQueue::Insert(const SpecLoaderRequest &request)
{
    bool inserted = false;
    if (mList.empty()) {
        mList.insert(mList.end(), request);
        return true;
    }
    for (auto it = mList.begin(); it != mList.end();) {
        if (!inserted && it->priority >= request.priority) {
            it = mList.insert(it, request);
            ++it;
            inserted = true;
        }
        if (KeyEquals(it->key, request.key)) {
            if (inserted) {
                Abandon(it->object);
                it = mList.erase(it);
            } else {
                // Merge into the queued request.
                it->time = request.time;
                if (request.lifetime == 0 || (it->lifetime != 0 && it->lifetime < request.lifetime))
                    it->lifetime = request.lifetime;
                if (request.retain == 0 || (it->retain != 0 && it->retain < request.retain)) it->retain = request.retain;
                it->poolOnly = it->poolOnly && request.poolOnly;
                it->followReferences = it->followReferences || request.followReferences;
                if (it->followReferences && request.parentKey.group != 0) it->parentKey = request.parentKey;
                // ORIGINAL-BUG: `inserted` is set without advancing, so the next pass sees the node just merged into
                // with `inserted` true and erases it: a duplicate key that was not inserted before it ends with
                // neither request queued, and Insert still returns true.
                inserted = true;
            }
        } else {
            ++it;
        }
    }
    return inserted;
}

// 0x1f7710 (slot 0)
cTSPersistSpeculativeLoaderQueue *cTSPersistSpeculativeLoaderQueue::DeletingDestructor(uint32_t flags) noexcept
{
    mVTable = GameAddress(kVTable);
    Clear();
    mList.~SpecLoaderList();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

// 0x1f76b0 (slot 0)
cTSPersistSpeculativeLoaderPool *cTSPersistSpeculativeLoaderPool::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    mList.clear();
    mList.~SpecLoaderList();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

// ---- Layers ----

// 0x1f7600 (slot 0)
cTSPersistSpeculativeLoaderLayer *cTSPersistSpeculativeLoaderLayer::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

// 0x1f7690 (slot 0; lane IG): the physical layer's destructor (0x1f7370, still the original's: three queues, the lock
// and the signal, whose destructor closes the event through an import), then the free.
cTSPersistSpeculativeLoaderPhysicalLayer *cTSPersistSpeculativeLoaderPhysicalLayer::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cTSPersistSpeculativeLoaderPhysicalLayer *), 0x1f7370>()(this);
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

// 0x1f7630 (slot 0; lane IG): the pool's destructor inlined (as 0x1f76b0 without the free), then the base's vtable.
cTSPersistSpeculativeLoaderLogicalLayer *cTSPersistSpeculativeLoaderLogicalLayer::DeletingDestructor(uint32_t flags)
{
    mPool.mVTable = GameAddress(cTSPersistSpeculativeLoaderPool::kVTable);
    mPool.mList.clear();
    mPool.mList.~SpecLoaderList();
    mVTable = GameAddress(cTSPersistSpeculativeLoaderLayer::kVTable);
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

// ---- The loader ----

// 0x1f7d40
uint32_t __cdecl cTSPersistSpeculativeLoader::GetClassId()
{
    return SpecLoader::kClassId;
}

// 0x1f7da0 (slot 12)
bool cTSPersistSpeculativeLoader::GetStatistics(uint32_t *hits, uint32_t *expired, uint32_t *cancelled, uint32_t *misses)
{
    *hits = mHits;
    *expired = mExpired;
    *cancelled = mCancelled;
    *misses = mMisses;
    return true;
}

// 0x1f8300 (slot 0)
bool cTSPersistSpeculativeLoader::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case SpecLoader::kInterfaceId:
        *out = this;
        VirtualCall<AddRefFn, 1>(this);
        return true;
    }
    return false;
}

// ---- Physical layer ----

// 0x1f81b0
bool cTSPersistSpeculativeLoaderPhysicalLayer::IsPending(const cGZPersistResourceKey &key)
{
    cRZLock lock(&mLock);
    return (mCurrent && KeyEquals(mCurrent->key, key)) || mPending.Contains(key) || mWaiting.Contains(key) ||
           mDone.Contains(key);
}

// 0x1f8650: merges a request for a key that is being loaded or is done, else queues it and wakes the worker.
bool cTSPersistSpeculativeLoaderPhysicalLayer::Submit(const SpecLoaderRequest &request)
{
    cRZLock lock(&mLock);
    if (mCurrent && KeyEquals(mCurrent->key, request.key)) {
        SpecLoaderRequest &current = *mCurrent;
        current.time = request.time;
        if (request.lifetime == 0 || (current.lifetime != 0 && current.lifetime < request.lifetime))
            current.lifetime = request.lifetime;
        if (request.retain == 0 || (current.retain != 0 && current.retain < request.retain))
            current.retain = request.retain;
        current.poolOnly = current.poolOnly && request.poolOnly;
        if (!current.followReferences && !request.followReferences) {
            current.followReferences = false;
        } else {
            current.followReferences = true;
            if (request.parentKey.group != 0) current.parentKey = request.parentKey;
        }
        return true; // the priority is not merged
    }
    if (SpecLoaderRequest *done = mDone.Find(request.key)) {
        done->retain = request.retain ? std::max(done->retain, request.retain) : 0;
        done->priority = std::min(done->priority, request.priority);
        done->poolOnly = done->poolOnly && request.poolOnly;
        done->lifetime = request.lifetime ? std::max(done->lifetime, request.lifetime) : 0;
        done->time = request.time;
        return true;
    }
    bool queued = mPending.Insert(request);
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(&mSignal); // wake the worker, whatever Insert said
    return queued;
}

// 0x1f8330: a finished child completes its parents: each waiting parent in the chain moves to the done queue.
void cTSPersistSpeculativeLoaderPhysicalLayer::CompleteParents(const SpecLoaderRequest &request)
{
    cGZPersistResourceKey parent = request.parentKey;
    while (parent.group != 0) {
        auto it = mWaiting.mList.begin();
        for (; it != mWaiting.mList.end(); ++it) {
            if (KeyEquals(it->key, parent)) break;
        }
        if (it == mWaiting.mList.end()) break;
        mDone.Insert(*it);
        parent = it->parentKey;
        mWaiting.mList.erase(it);
    }
}

#pragma warning(push)
#pragma warning(disable : 4701) // instanceHigh (see below)
// 0x1f89d0: reads the key list a record holds and queues each key as a child request. The caller holds the lock.
bool cTSPersistSpeculativeLoaderPhysicalLayer::QueueReferences(SpecLoaderRequest &request, void *record)
{
    CountedRef stream; // the record's stream face, released through slot 2
    request.followReferences = false;
    if (record && NarrowCall<0>(record, SpecLoader::kRecordStreamIid, &stream.object)) {
        uint32_t savedPosition = VirtualCall<GetPositionFn, 54>(stream.object);
        uint32_t version = 0;
        uint32_t count;
        if (!NarrowCall<13>(stream.object, &count)) return false;
        if ((count & 0xffff0000) == 0xffff0000) {
            version = count & 0xffff;
            if (!NarrowCall<13>(stream.object, &count)) return false;
        }
        SpecLoaderRequest child;
        child.key = request.key;
        if (count != 0) {
            uint32_t index = 0;
            do {
                uint32_t skipped, instance, type;
                // ORIGINAL-BUG: instanceHigh is only read when version >= 1; for version 0 the children's fourth key word is
                // whatever the stack held.
#pragma warning(suppress : 4701)
                uint32_t instanceHigh;
                if (!NarrowCall<13>(stream.object, &skipped) ||
                    !NarrowCall<13>(stream.object, &instance) ||
                    (version != 0 && !NarrowCall<13>(stream.object, &instanceHigh)) ||
                    !NarrowCall<13>(stream.object, &type))
                    return false;
                child.parentKey = cGZPersistResourceKey{};
                if (index == count - 1) child.parentKey = request.key;
                child.key.group = request.key.group;
                child.key.type = type;
                child.key.instance = instance;
                child.key.resource = instanceHigh;
                child.retain = request.retain;
                child.lifetime = request.lifetime;
                child.time = request.time;
                child.priority = request.priority;
                child.poolOnly = false;
                child.followReferences = true;
                ResetHolder(child.object.mObject);
                mPending.Insert(child);
                ++index;
                request.followReferences = true;
            } while (index < count);
        }
        VirtualCall<SetPositionFn, 55>(stream.object, savedPosition);
    }
    return true;
}
#pragma warning(pop)

// 0x1f9360: the worker's step. Takes the pending requests (largest priority first) until one loads, drops expired waiting
// requests, and tells the thread to sleep when idle. Returns false once Shutdown asked to quit.
bool cTSPersistSpeculativeLoaderPhysicalLayer::Process(uint32_t now)
{
    cRZLock lock(&mLock);
    ResourceManagerHolder rm;
    SpecLoaderRequest request;
    while (mPending.PopBack(request)) {
        if (request.lifetime != 0 && now >= request.time + request.lifetime) continue; // expired, not abandoned
        mCurrent = &request;
        VirtualCall<LockFn, 2>(&mLock);
        CountedRef segment;
        bool loaded = NarrowCall<34>(rm.Get(), &request.key, &segment.object);
        if (!loaded) {
            request.key.group = SpecLoader::kLocalGroup;
            ResetHolder(segment.object);
            loaded = NarrowCall<34>(rm.Get(), &request.key, &segment.object);
        }
        bool opened = false;
        CountedRef record;
        if (loaded) {
            CountedRef packed;
            if ((!request.followReferences || request.key.type == SpecLoader::kRawTypeA ||
                 request.key.type == SpecLoader::kRawTypeB || request.key.type == SpecLoader::kRawTypeC) &&
                NarrowCall<0>(segment.object, SpecLoader::kPackedSegmentIid, &packed.object)) {
                PreloadedRecord *raw = new PreloadedRecord(request.key);
                if (raw) VirtualCall<AddRefFn, 1>(raw);
                uint32_t size = VirtualCall<uint32_t(__thiscall *)(void *, const Key *), 27>(packed.object, &request.key);
                uint32_t field50 = VirtualCall<uint32_t(__thiscall *)(void *, const Key *), 21>(segment.object, &request.key);
                raw->mCompressed = VirtualCall<uint8_t(__thiscall *)(void *, const Key *), 41>(packed.object, &request.key);
                raw->mData = RZMemory::Malloc(size);
                raw->mSize = size;
                raw->mField50 = field50;
                uint32_t readSize = size;
                VirtualCall<uint32_t(__thiscall *)(void *, const Key *, void *, uint32_t *), 28>(packed.object, &request.key,
                                                                                               raw->mData, &readSize);
                if (readSize == size)
                    AssignObject(request.object, &raw->mVTable4);
                else
                    loaded = false;
                VirtualCall<AddRefFn, 2>(raw);
            } else {
                ResetHolder(record.object);
                loaded = NarrowCall<22>(segment.object, &request.key,
                                                                                                   &record.object, 1);
                if (loaded) {
                    AssignObject(request.object, record.object);
                    opened = true;
                }
            }
        }
        VirtualCall<LockFn, 1>(&mLock);
        SpecLoaderRequest *current = mCurrent;
        mCurrent = nullptr;
        bool ok = current ? loaded : false; // Shutdown or Cancel may have cleared mCurrent while unlocked
        if (!ok) {
            CompleteParents(request);
            continue;
        }
        if (!request.followReferences) {
            mDone.Insert(request);
        } else if (!opened) {
            request.followReferences = false;
            mDone.Insert(request);
            CompleteParents(request);
        } else {
            QueueReferences(request, record.object);
            if (request.followReferences) {
                mWaiting.Insert(request);
            } else {
                mDone.Insert(request);
                CompleteParents(request);
            }
        }
        break; // one load per call
    }
    for (auto it = mWaiting.mList.begin(); it != mWaiting.mList.end();) {
        if (it->lifetime == 0 || it->time + it->lifetime >= now) {
            ++it;
        } else {
            Abandon(it->object);
            it = mWaiting.mList.erase(it);
        }
    }
    if (mPending.mList.empty() && mWaiting.mList.empty()) VirtualCall<uint32_t(__thiscall *)(void *), 6>(&mSignal);
    return !mQuit;
}

// 0x1f9860
int __cdecl cTSPersistSpeculativeLoaderPhysicalLayer::ThreadMain(cTSPersistSpeculativeLoaderPhysicalLayer *layer)
{
    if (!layer) return -1;
    do {
        VirtualCall<uint32_t(__thiscall *)(void *, int), 3>(&layer->mSignal, -1); // wait
        if (layer->mQuit) break;
    } while (layer->Process(static_cast<uint32_t>(clock())));
    return 0;
}

// ---- Logical layer ----

// 0x1f7790
bool cTSPersistSpeculativeLoaderLogicalLayer::Cancel(const cGZPersistResourceKey &key)
{
    if (mPhysical) {
        cRZLock lock(&mPhysical->mLock);
        if (mPhysical->mCurrent && KeyEquals(mPhysical->mCurrent->key, key)) {
            mPhysical->mCurrent = nullptr;
            return true;
        }
        if (mPhysical->mPending.Remove(key) || mPhysical->mWaiting.Remove(key) || mPhysical->mDone.Remove(key))
            return true;
    }
    for (auto it = mPool.mList.begin(); it != mPool.mList.end(); ++it) {
        if (KeyEquals(it->key, key)) {
            mPool.mList.erase(it);
            return true;
        }
    }
    return false;
}

// 0x1f8d90: the main thread's step. Turns finished loads from the done queue into resources (decompressing raw records
// first), pools and caches them, queues their references, until the time budget (ms) or the item limit is reached; then
// drops expired pool entries.
bool cTSPersistSpeculativeLoaderLogicalLayer::Update(uint32_t now, uint32_t budget)
{
    if (!mPhysical) return false;
    cRZStopwatch timer;
    timer.Construct(4);
    timer.Start();
    int32_t count = 0;
    int32_t limit = cTSPersistSpeculativeLoader::kUpdateItemLimit;
    for (;;) {
        SpecLoaderRequest request;
        cTSPersistSpeculativeLoaderPhysicalLayer *physical = mPhysical;
        bool finished;
        {
            cRZLock lock(&physical->mLock);
            bool popped = physical->mDone.PopBack(request);
            while (popped && !((request.retain != 0 || now < request.time) && request.object.mObject)) {
                // ORIGINAL-BUG: the physical layer's owner is never set, so skipped requests are never counted as expired.
                if (physical->mOwner) ++physical->mOwner->mExpired;
                popped = physical->mDone.PopBack(request);
            }
            finished = !popped;
        }
        if (finished) break;
        if (request.object.mObject) {
            CountedRef rawRecord;
            bool handled = false;
            if (NarrowCall<0>(request.object.mObject, SpecLoader::kPreloadedRecordIid, &rawRecord.object) &&
                static_cast<PreloadedRecord *>(rawRecord.object)->mCompressed != 0) {
                PreloadedRecord *raw = static_cast<PreloadedRecord *>(rawRecord.object);
                PreloadedRecord *expanded = new PreloadedRecord(request.key);
                if (expanded) VirtualCall<AddRefFn, 1>(expanded);
                uint32_t sourceLength = VirtualCall<uint32_t(__thiscall *)(void *), 52>(raw);
                uint32_t size = raw->mField50;
                void *buffer = RZMemory::Malloc(size);
                expanded->mData = buffer;
                expanded->mSize = size;
                expanded->mField50 = size;
                cRZFastCompression3 decompressor;
                OriginalFunction<void(__thiscall *)(void *), 0x9cc0c2>()(&decompressor);
                uint32_t decompressedSize;
                if (decompressor.DecompressData(raw->mData, sourceLength, expanded->mData, size, &decompressedSize) &&
                    decompressedSize == size) {
                    AssignObject(request.object, &expanded->mVTable4);
                    cTSPersistSpeculativeLoaderPhysicalLayer *doneOwner = mPhysical;
                    {
                        cRZLock lock(&doneOwner->mLock);
                        doneOwner->mDone.mList.push_back(request);
                    }
                    ++limit;
                    handled = true;
                }
                decompressor.Destruct();
                VirtualCall<AddRefFn, 2>(expanded);
            }
            CountedRef dbRecord;
            if (!handled && NarrowCall<0>(request.object.mObject, SpecLoader::kDBRecordIid, &dbRecord.object)) {
                ResourceManagerHolder rm;
                CountedRef factory;
                if (NarrowCall<23>(rm.Get(), &request.key, &factory.object)) {
                    CountedRef resource;
                    if (NarrowCall<3>(
                            factory.object, dbRecord.object, SpecLoader::kPersistResourceIid, &resource.object, 0, 0)) {
                        VirtualCall<void(__thiscall *)(void *, const Key *), 4>(resource.object, &request.key);
                        AssignObject(request.object, resource.object);
                        if (request.poolOnly || request.retain) mPool.mList.push_back(request);
                        if (!request.poolOnly) VirtualCall<void(__thiscall *)(void *, void *), 8>(rm.Get(), resource.object);
                        if (request.followReferences) {
                            ServiceBlockVector references = {nullptr, nullptr, nullptr};
                            ScopedRef referenceService;
                            referenceService.mObject = nullptr;
                            if (void *service = GetServiceObject())
                                VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(
                                    service, SpecLoader::kReferenceServiceId, SpecLoader::kReferenceServiceIid,
                                    &referenceService.mObject);
                            if (referenceService.mObject &&
                                NarrowCall<6>(
                                    referenceService.mObject, resource.object, &references)) {
                                Key *last = static_cast<Key *>(references.mLast);
                                for (Key *key = static_cast<Key *>(references.mFirst); key != last; ++key) {
                                    RZAutoRef persistSystem{nullptr};
                                    if (void *service = GetServiceObject())
                                        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(
                                            service, SpecLoader::kServiceId, SpecLoader::kPersistSystemIid,
                                            &persistSystem.mObject);
                                    VirtualCall<uint32_t(__thiscall *)(void *, const Key *, uint32_t, uint32_t, uint32_t, uint32_t),
                                                6>(persistSystem.mObject, key, 0, request.lifetime, 0, 0);
                                    persistSystem.ReleaseObject();
                                }
                            }
                            referenceService.ReleaseObject();
                            references.FreeBlock();
                        }
                    }
                }
                VirtualCall<CallFn, 6>(dbRecord.object); // Close
            }
        }
        if (timer.Elapsed() > budget) break;
        if (++count >= limit) break;
    }
    for (auto it = mPool.mList.begin(); it != mPool.mList.end();) {
        if (it->lifetime == 0 || it->time + it->lifetime >= now)
            ++it;
        else
            it = mPool.mList.erase(it);
    }
    acc::DoNothing(&timer, nullptr);
    return true;
}

// ---- The loader ----

// 0x1f8050 (slot 3)
bool cTSPersistSpeculativeLoader::Init()
{
    mMisses = 0;
    mCancelled = 0;
    mExpired = 0;
    mHits = 0;
    mLogical.mPhysical = &mPhysical;
    NarrowCall<12>(&mThread, GameAddress<const char>(SpecLoader::kThreadNameRva));
    VirtualCall<uint32_t(__thiscall *)(void *, int(__cdecl *)(cTSPersistSpeculativeLoaderPhysicalLayer *), void *), 2>(
        &mThread, &cTSPersistSpeculativeLoaderPhysicalLayer::ThreadMain, &mPhysical);
    mLogical.mOwner = this;
    return true;
}

// 0x1f8c70 (slot 4)
bool cTSPersistSpeculativeLoader::Shutdown()
{
    {
        cRZLock lock(&mPhysical.mLock);
        mPhysical.mQuit = true;
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(&mPhysical.mSignal);
    }
    return VirtualCall<uint32_t(__thiscall *)(void *, int), 8>(&mThread, -1) != 0;
}

// 0x1f8450 (slot 5)
bool cTSPersistSpeculativeLoader::Preload(const cGZPersistResourceKey &key, bool poolOnly, bool followReferences,
                                          uint32_t priority, uint32_t lifetime, uint32_t retain)
{
    {
        ResourceManagerHolder rm;
        if (NarrowCall<11>(rm.Get(), &key)) return true;
    }
    SpecLoaderRequest request;
    request.key = key;
    request.priority = priority;
    request.lifetime = lifetime;
    request.retain = retain;
    request.poolOnly = poolOnly;
    request.followReferences = followReferences;
    request.time = static_cast<uint32_t>(clock());
    request.parentKey = cGZPersistResourceKey{};
    for (SpecLoaderRequest &pooled : mLogical.mPool.mList) {
        if (!KeyEquals(pooled.key, request.key)) continue;
        pooled.retain = request.retain ? std::max(pooled.retain, request.retain) : 0;
        pooled.priority = std::min(pooled.priority, request.priority);
        pooled.poolOnly = pooled.poolOnly && request.poolOnly;
        pooled.lifetime = request.lifetime ? std::max(pooled.lifetime, request.lifetime) : 0;
        pooled.time = request.time;
        return true;
    }
    return mLogical.mPhysical ? mLogical.mPhysical->Submit(request) : false;
}

// 0x1f7780 (slot 6)
bool cTSPersistSpeculativeLoader::Cancel(const cGZPersistResourceKey &key)
{
    return mLogical.Cancel(key);
}

// 0x1f80b0 (slot 7)
bool cTSPersistSpeculativeLoader::IsAvailable(const cGZPersistResourceKey &key)
{
    {
        ResourceManagerHolder rm;
        if (NarrowCall<11>(rm.Get(), &key)) return true;
    }
    for (SpecLoaderRequest &pooled : mLogical.mPool.mList) {
        if (KeyEquals(pooled.key, key)) return true;
    }
    return false;
}

// 0x1f8170 (slot 8)
bool cTSPersistSpeculativeLoader::IsPending(const cGZPersistResourceKey &key)
{
    return mPhysical.IsPending(key) || (mLogical.mPhysical && mLogical.mPhysical->IsPending(key));
}

// 0x1f7ee0 (slot 9): hands out (once) a pooled resource, else asks the resource manager.
bool cTSPersistSpeculativeLoader::GetResource(const cGZPersistResourceKey &key, uint32_t riid, void **out, uint32_t a4,
                                              uint32_t a5, bool *fromPool)
{
    CountedRef pooled; // the pooled resource, released through slot 2 when the call ends
    bool hit = false;
    for (auto it = mLogical.mPool.mList.begin(); it != mLogical.mPool.mList.end(); ++it) {
        if (KeyEquals(it->key, key)) {
            pooled.object = it->object.mObject;
            VirtualCall<AddRefFn, 1>(pooled.object); // no null check
            mLogical.mPool.mList.erase(it);
            hit = true;
            break;
        }
    }
    if (fromPool) *fromPool = hit;
    bool result;
    if (hit) {
        ++mHits;
        result = NarrowCall<0>(pooled.object, riid, out); // no null check
    } else {
        if (NarrowCall<6>(this, &key))
            ++mCancelled;
        else
            ++mMisses;
        ResourceManagerHolder rm;
        result = NarrowCall<5>(
            rm.Get(), &key, riid, out, a4, a5);
    }
    return result;
}

// 0x1f8cf0 (slot 10): the per-tick call. The budget (ms) for the logical layer's Update follows the average frame time.
bool cTSPersistSpeculativeLoader::Update()
{
    uint32_t budget = kDefaultBudget;
    if (OriginalFunction<bool(__thiscall *)(void *), 0x1c95cf>()(&mFrameTimer)) {
        mAverageFrameTime += (static_cast<float>(mFrameTimer.Elapsed()) - mAverageFrameTime) * 0.001f;
        budget = static_cast<uint32_t>(mAverageFrameTime * 0.2f);
        if (budget < kMinBudget)
            budget = kMinBudget;
        else if (budget > kMaxBudget)
            budget = kMaxBudget;
    }
    bool result = mLogical.Update(static_cast<uint32_t>(clock()), budget);
    OriginalFunction<void(__thiscall *)(void *), 0x1c95ec>()(&mFrameTimer);
    return result;
}

// 0x1f7a40 (slot 11)
void cTSPersistSpeculativeLoader::Clear()
{
    {
        cRZLock lock(&mPhysical.mLock);
        mPhysical.mPending.Clear();
        mPhysical.mWaiting.Clear();
        mPhysical.mDone.Clear();
        mPhysical.mCurrent = nullptr;
    }
    mLogical.mPool.mList.clear();
}
