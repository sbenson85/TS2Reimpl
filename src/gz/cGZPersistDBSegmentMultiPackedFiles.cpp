#include "gz/cGZPersistDBSegmentMultiPackedFiles.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSegment.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZRefCount.h"
#include "rz/cRZString.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <memory>
#include <new>

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);
using VoidFn = void(__thiscall *)(void *);
using BoolFn = bool(__thiscall *)(void *);
using VoidPtrFn = void(__thiscall *)(void *, void *);
using U32Fn = uint32_t(__thiscall *)(void *);
using U32U32Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using U32PtrFn = uint32_t(__thiscall *)(void *, void *);
using PtrArgFn = void *(__thiscall *)(void *, uint32_t);
using PtrPtrFn = void *(__thiscall *)(void *, void *);
using QiFn = bool(__thiscall *)(void *, uint32_t, void **);
using U32x4Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
using BoolPtrFn = bool(__thiscall *)(void *, const void *);
using BoolPtr3Fn = bool(__thiscall *)(void *, const void *, uint32_t, uint32_t);
using U32Ptr3Fn = uint32_t(__thiscall *)(void *, const void *, uint32_t, uint32_t);
using PtrFn = const uint32_t *(__thiscall *)(void *);

constexpr uint32_t kIdPackedFile = 0x286d4a6a; // cIGZDBSegmentPackedFile

// The lock is the cRZCriticalSection at +8 of the primary: slot 1 Lock, slot 2 Unlock, with its own address.
void Enter(cGZPersistDBSegmentMultiPackedFiles *self)
{
    VirtualSlot<U32Fn>(&self->mBase.mLock, 1)(&self->mBase.mLock);
}

void Leave(cGZPersistDBSegmentMultiPackedFiles *self)
{
    VirtualSlot<U32Fn>(&self->mBase.mLock, 2)(&self->mBase.mLock);
}

// The primary object of a subobject pointer.
cGZPersistDBSegmentMultiPackedFiles *Primary(MultiPackedFilesSegmentView *view)
{
    return reinterpret_cast<cGZPersistDBSegmentMultiPackedFiles *>(reinterpret_cast<uint8_t *>(view) - 0x18);
}

void *NodeValue(const StdHashNode *node)
{
    return *reinterpret_cast<void *const *>(reinterpret_cast<const uint8_t *>(node) + 0x18);
}

// Frees the storage of a vector of 4-byte elements: the size in bytes is (end of storage - begin) rounded down
// to a multiple of 4.
void FreeStorage(void *begin, uint32_t capEnd)
{
    uint32_t bytes = (capEnd - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(begin))) & ~3u;
    std::allocator<void *>().deallocate(static_cast<void **>(begin), bytes / 4);
}

// The subobject at +0x18 and the array of items at +0x68 are read through the primary's fields.
void *const *ItemsBegin(const MultiPackedFilesSegmentView *view)
{
    return *reinterpret_cast<void *const *const *>(reinterpret_cast<const uint8_t *>(view) + 0x50);
}

} // namespace

MultiPackedFilesSegmentView *cGZPersistDBSegmentMultiPackedFiles::View()
{
    return reinterpret_cast<MultiPackedFilesSegmentView *>(reinterpret_cast<uint8_t *>(this) + 0x18);
}

bool cGZPersistDBSegmentMultiPackedFiles::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kIdSelf) {
        // The original tail-jumps to the base's QueryInterface with this and the same arguments.
        return mBase.QueryInterface(riid, ppvObj);
    }
    // The original computes this ? this + 0x18 : 0 without a branch and then calls AddRef on this, which is
    // never null (a null this faults in the AddRef load), so the null case is not reproduced.
    *ppvObj = View();
    VirtualCall<AddRefFn, 1>(this); // AddRef through the vtable of this
    return true;
}

bool cGZPersistDBSegmentMultiPackedFiles::Shutdown()
{
    if (mOpen64 != 0) VirtualCall<VoidFn, kCloseSlot>(this);
    mState1C = 0;
    return mBase.Shutdown();
}

void cGZPersistDBSegmentMultiPackedFiles::GetPath(void *out)
{
    // out->slot 16(address of the embedded object at +0x24)
    VirtualCall<VoidPtrFn, 16>(out, &mPathVTable);
}

bool cGZPersistDBSegmentMultiPackedFiles::SetPath(void *path)
{
    if (mOpen64 != 0) return false;
    // The embedded object's slot 16 with the path; the object is at this + 0x24.
    VirtualCall<VoidPtrFn, 16>(&mPathVTable, path);
    return true;
}

bool cGZPersistDBSegmentMultiPackedFiles::SetSegmentID(const uint32_t *id)
{
    mSegmentID = *id;
    return true;
}

uint32_t cGZPersistDBSegmentMultiPackedFiles::Field7C(uint32_t)
{
    return mTable.mUnknown08; // the element count of the table
}

uint32_t cGZPersistDBSegmentMultiPackedFiles::SumItems(uint32_t a, uint32_t b)
{
    // The count comes from the subobject's slot 7 through its vtable, and the items are read from +0x68 at
    // every step (the original does not keep the array pointer in a register across calls).
    MultiPackedFilesSegmentView *view = View();
    uint32_t count = VirtualCall<U32Fn, MultiPackedFilesSegmentView::kCountSlot>(view);
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        void *item = mItemsBegin[i];
        total += VirtualCall<U32U32Fn, 19>(item, a, b);
    }
    return total;
}

uint32_t MultiPackedFilesSegmentView::ItemCount() const
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(this);
    uint32_t begin = *reinterpret_cast<const uint32_t *>(bytes + 0x50);
    uint32_t end = *reinterpret_cast<const uint32_t *>(bytes + 0x54);
    // The original subtracts as 32-bit words and shifts arithmetically, so an end below begin gives a count
    // with the top bits set.
    return static_cast<uint32_t>(static_cast<int32_t>(end - begin) >> 2);
}

void *MultiPackedFilesSegmentView::ItemAt(uint32_t index) const
{
    return ItemsBegin(this)[index];
}

// --- table -------------------------------------------------------------------------------------------------

void MultiPackedFilesTable::Clear()
{
    if (mUnknown08 == 0) return;
    StdHashNode *head = mHead;
    head->prev->next = nullptr; // the list becomes null-terminated for the walk
    for (StdHashNode *node = head->next; node != nullptr;) {
        StdHashNode *next = node->next;
        ::operator delete(node, kNodeSize);
        node = next;
    }
    mHead->next = mHead;
    mHead->prev = mHead;
    mUnknown08 = 0;
    // Every bucket entry (two per bucket) points at the head again.
    StdHashNode *headValue = mHead;
    auto *first = reinterpret_cast<StdHashNode **>(mBuckets);
    auto *last = reinterpret_cast<StdHashNode **>(static_cast<uintptr_t>(mUnknown10[0]));
    std::fill(first, last, headValue);
}


// --- construction ------------------------------------------------------------------------------------------

cGZPersistDBSegmentMultiPackedFiles *cGZPersistDBSegmentMultiPackedFiles::Construct()
{
    // The base's constructor (0x99b790): vtable, count, the lock, the cache and the namespace.
    mBase.mVTable = GameAddress(cGZPersistDBSegment::kVTable);
    mBase.mRefCount = 0;
    mBase.mLock.Construct();
    mBase.mSegmentCache = nullptr;
    mBase.mNamespace = nullptr;

    mBase.mVTable = GameAddress(kVTable);
    mSubVTable = GameAddress(kSubVTable);
    mState1C = 0;
    mSegmentID = 0;
    // The two cRZString members, written as the original writes them (empty path; pattern "*.dat"): the
    // string body is zeroed, its size and capacity are set and the reference count is zero.
    std::memset(&mPathBody[0], 0, 0x10);
    *reinterpret_cast<uint32_t *>(&mPathBody[0x10]) = 0;   // size (+0x38)
    *reinterpret_cast<uint32_t *>(&mPathBody[0x14]) = 0xf; // capacity (+0x3c)
    mPathVTable = GameAddress(cRZString::kVTable);
    *reinterpret_cast<uint32_t *>(&mPathBody[0x18]) = 0; // reference count (+0x40)
    std::memset(&mPatternBody[0], 0, 0x10);
    *reinterpret_cast<uint32_t *>(&mPatternBody[0x10]) = 5;
    *reinterpret_cast<uint32_t *>(&mPatternBody[0x14]) = 0xf;
    std::memcpy(&mPatternBody[0], "*.dat", 6); // the five characters and the terminator
    mPatternVTable = GameAddress(cRZString::kVTable);
    *reinterpret_cast<uint32_t *>(&mPatternBody[0x18]) = 0; // reference count (+0x60)
    mOpen64 = 0;
    mItemsBegin = nullptr;
    mItemsEnd = nullptr;
    mItemsCapEnd = nullptr;
    mTable.mUnknown00 = 0;
    mTable.mHead = nullptr;
    mTable.mUnknown08 = 0;

    auto *head = static_cast<StdHashNode *>(::operator new(MultiPackedFilesTable::kNodeSize));
    head->next = head;
    head->prev = head;
    mTable.mHead = head;
    mTable.mBuckets = nullptr;
    mTable.mUnknown10[0] = 0;
    mTable.mUnknown10[1] = 0;
    mTable.mMask = 7;
    mBucketCount = 8;
    float loadFactor = 1.0f;
    std::memcpy(&mTable.mUnknown00, &loadFactor, sizeof(loadFactor));
    // The bucket vector grows from empty to 16 entries (two per bucket), all set to the head.
    constexpr uint32_t kBucketEntries = 16;
    auto *buckets = static_cast<StdHashNode **>(::operator new(kBucketEntries * sizeof(StdHashNode *)));
    mTable.mBuckets = reinterpret_cast<StdHashBucket *>(buckets);
    mTable.mUnknown10[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buckets + kBucketEntries));
    mTable.mUnknown10[1] = mTable.mUnknown10[0];
    std::fill(buckets, buckets + kBucketEntries, head);
    return this;
}

cGZPersistDBSegmentMultiPackedFiles *cGZPersistDBSegmentMultiPackedFiles::DeletingDestructor(uint32_t flags)
{
    mBase.mVTable = GameAddress(kVTable);
    mSubVTable = GameAddress(kSubVTable);
    std::destroy_at(reinterpret_cast<MultiPackedFilesMap *>(&mTable)); // 0x1fb410 is the container's destructor
    if (mItemsBegin != nullptr) {
        FreeStorage(mItemsBegin, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mItemsCapEnd)));
        mItemsBegin = nullptr;
        mItemsEnd = nullptr;
        mItemsCapEnd = nullptr;
    }
    PatternString()->Destruct();
    PathString()->Destruct();
    // The base's destructor body (0x99b8e0): the base vtable, the references to the namespace and the cache
    // (not cleared), the lock.
    mBase.mVTable = GameAddress(cGZPersistDBSegment::kVTable);
    if (void *ns = mBase.mNamespace) VirtualCall<U32Fn, 2>(ns);
    if (void *cache = mBase.mSegmentCache) VirtualCall<U32Fn, 2>(cache);
    mBase.mLock.Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// --- close -------------------------------------------------------------------------------------------------

bool cGZPersistDBSegmentMultiPackedFiles::Close()
{
    Enter(this);
    MultiPackedFilesSegmentView *view = View();
    uint32_t count = VirtualCall<U32Fn, MultiPackedFilesSegmentView::kCountSlot>(view);
    for (uint32_t i = 0; i < count; ++i) {
        void *item = mItemsBegin[i];
        VirtualCall<VoidFn, 9>(item); // Close
        VirtualCall<VoidFn, 4>(item); // Shutdown
        VirtualCall<U32Fn, 2>(item);  // Release
    }
    if (mItemsBegin != mItemsEnd) mItemsEnd = mItemsBegin;
    mTable.Clear();
    mOpen64 = 0;
    Leave(this);
    return true;
}

// --- record operations -------------------------------------------------------------------------------------

void *cGZPersistDBSegmentMultiPackedFiles::FindItem(const uint32_t *key)
{
    StdHashFindResult found;
    mTable.Find128(&found, key, key[2]);
    // A miss reads as the head node, which is never an element.
    StdHashNode *node = found.node != nullptr ? found.node : mTable.mHead;
    if (node == mTable.mHead) return nullptr;
    return NodeValue(node);
}

bool cGZPersistDBSegmentMultiPackedFiles::TestForRecordImpl(const uint32_t *key)
{
    if (mOpen64 == 0) return false;
    void *item = FindItem(key);
    if (item == nullptr) return false;
    return VirtualCall<BoolPtrFn, 20>(item, key);
}

uint32_t cGZPersistDBSegmentMultiPackedFiles::GetRecordSizeImpl(const uint32_t *key)
{
    if (mOpen64 == 0) return 0;
    void *item = FindItem(key);
    if (item == nullptr) return 0;
    return VirtualCall<U32PtrFn, 21>(item, const_cast<uint32_t *>(key));
}

bool cGZPersistDBSegmentMultiPackedFiles::OpenRecordImpl(const uint32_t *key, uint32_t a, uint32_t b)
{
    if (mOpen64 == 0) return false;
    void *item = FindItem(key);
    if (item == nullptr) return false;
    return VirtualCall<BoolPtr3Fn, 22>(item, key, a, b);
}

bool cGZPersistDBSegmentMultiPackedFiles::CloseRecordImpl(void *record)
{
    if (mOpen64 == 0) return false;
    const uint32_t *key = VirtualCall<PtrFn, 3>(record);
    void *item = FindItem(key);
    if (item == nullptr) return false;
    return VirtualCall<BoolPtrFn, 24>(item, record);
}

bool cGZPersistDBSegmentMultiPackedFiles::AbortRecordImpl(void *record)
{
    if (mOpen64 == 0) return false;
    const uint32_t *key = VirtualCall<PtrFn, 3>(record);
    void *item = FindItem(key);
    if (item == nullptr) return false;
    return VirtualCall<BoolPtrFn, 25>(item, record);
}

uint32_t cGZPersistDBSegmentMultiPackedFiles::ReadRecordImpl(const uint32_t *key, uint32_t a, uint32_t b)
{
    if (mOpen64 == 0) return 0;
    void *item = FindItem(key);
    if (item == nullptr) return 0;
    return VirtualCall<U32Ptr3Fn, 27>(item, key, a, b);
}

// --- the subobject -----------------------------------------------------------------------------------------

bool MultiPackedFilesSegmentView::QueryInterface(uint32_t riid, void **ppvObj)
{
    return Primary(this)->QueryInterface(riid, ppvObj);
}

uint32_t MultiPackedFilesSegmentView::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(&Primary(this)->mBase.mRefCount, 1));
}

uint32_t MultiPackedFilesSegmentView::Release()
{
    return Primary(this)->mBase.Release();
}

void *MultiPackedFilesSegmentView::SetPattern(void *pattern)
{
    void *str = Primary(this)->PatternString();
    return VirtualCall<PtrPtrFn, 16>(str, pattern);
}

uint32_t MultiPackedFilesSegmentView::SumPackedFiles(uint32_t a, uint32_t b)
{
    uint32_t total = 0;
    uint32_t count = VirtualCall<U32Fn, kCountSlot>(this);
    for (uint32_t i = 0; i < count; ++i) {
        void *packed = nullptr;
        void *item = VirtualCall<PtrArgFn, kItemSlot>(this, i);
        if (VirtualCall<QiFn, 0>(item, kIdPackedFile, &packed)) {
            total += VirtualCall<U32x4Fn, 35>(packed, a, b, 0, 1);
            VirtualCall<U32Fn, 2>(packed); // Release
        }
    }
    return total;
}

bool MultiPackedFilesSegmentView::GetItem(const uint32_t *key, void **out)
{
    cGZPersistDBSegmentMultiPackedFiles *self = Primary(this);
    Enter(self);
    bool found = false;
    if (self->mOpen64 != 0) {
        if (void *item = self->FindItem(key)) {
            *out = item;
            VirtualCall<U32Fn, 1>(item); // AddRef
            found = true;
        }
    }
    Leave(self);
    return found;
}

void MultiPackedFilesSegmentView::RemoveItem(const uint32_t *key, uint32_t)
{
    cGZPersistDBSegmentMultiPackedFiles *self = Primary(this);
    MultiPackedFilesTable &table = self->mTable;
    uint32_t hash = key[2];
    StdHashFindResult found;
    table.Find128(&found, key, hash);
    StdHashNode *node = found.node;
    if (node == nullptr) return;
    StdHashBucket *bucket = &table.mBuckets[hash & table.mMask];
    if (bucket->last == node) {
        if (bucket->first == node) {
            bucket->first = table.mHead; // the bucket becomes empty
            bucket->last = table.mHead;
        } else {
            bucket->last = node->prev;
        }
    } else if (bucket->first == node) {
        bucket->first = node->next;
    }
    StdHashNode *next = node->next;
    --table.mUnknown08;
    node->prev->next = next;
    next->prev = node->prev;
    ::operator delete(node, MultiPackedFilesTable::kNodeSize);
}
