// cTSPersistSystem in the standalone shape (lane FX): constructor, AddRef / Release, the folded slot 29 and the
// adapter of the constructor. The slot bodies and their adapters are in cTSPersistSystem.cpp (lane GD).
#include "ts/cTSPersistSystem.h"
#include "game/original.h"
#include "rz/nGZLog.h"
#include "simantics/cTSBehaviorTree.h"

#include <new>

namespace {

template <typename T> T *Ptr(uint32_t word)
{
    return reinterpret_cast<T *>(static_cast<uintptr_t>(word));
}

uint32_t Word(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// The head node of a std::map of the game's STL (0x1c bytes for every map here): left, parent and right point at itself,
// colour and is-nil bytes both 1 (the word 0x101 at +0xc). The rest of the node is left as allocated.
void *NewTreeHead()
{
    auto *head = static_cast<void **>(::operator new(0x1c));
    head[0] = head;
    head[1] = head;
    head[2] = head;
    *reinterpret_cast<uint16_t *>(reinterpret_cast<uint8_t *>(head) + 0xc) = 0x101;
    return head;
}

} // namespace

// 0x1d8b90 (1274 B): the resource manager base, then (after the vtables) the word 0x100 at +0xb4, 5000, the handler,
// segment and cache fields zero, +0xf0 = 1, the hash tables (node sizes 0x2c, 0x18, 0x10, 0x3c), the word 0 at +0x174, the
// two segment tables (0x1d8a50: node 0x2c), the resource-name tree, the loader 0, the flush stopwatch (mode 4), +0x1e0 and
// +0x1e4 zero, the path groups (node 0x2c) and the next group 1, the change records (node 0x30), the word 0 at +0x22d, the
// listener vector, the category tree, the keyed objects (node 0x10) and the owner thread id. +0xb6, +0xb8..+0xc7, +0x1c4,
// +0x22c and +0x22f are not written.
cTSPersistSystem::cTSPersistSystem()
{
    mVersionSigFound = 0;
    mFieldB5 = 1;
    mFieldC8 = 5000;
    mTypeHandler2026960b = nullptr;
    mTypeHandler4d45f8e6 = nullptr;
    mSegmentsBegin = nullptr;
    mSegmentsEnd = nullptr;
    mSegmentsCap = nullptr;
    mSmallObjectCache = nullptr;
    mLongTermCache = nullptr;
    mGeneralCache = nullptr;
    mSimCache = nullptr;
    mFieldF0 = 1;
    gz::ConstructPersistHashMap(mNamedMap, 0x2c);
    gz::ConstructPersistHashMap(mSegmentsByGroup, 0x18);
    gz::ConstructPersistHashMap(mSegmentIdRanges, 0x10);
    gz::ConstructPersistHashMap(mMap154, 0x3c);
    mOpeningContent = 0;
    mContentOpened = 0;
    gz::ConstructPersistHashMap(mMap178, 0x2c);
    gz::ConstructPersistHashMap(mMap198, 0x2c);
    mTreeHead = nullptr;
    mTreeSize = 0;
    mTreeHead = NewTreeHead();
    mSpeculativeLoader = nullptr;
    mFlushTimer.Construct(4);
    mFlushIntervalMs = 0;
    mBackgroundThread = nullptr;
    gz::ConstructPersistHashMap(mPathGroups, 0x2c);
    mNextPathGroup = 1;
    gz::ConstructPersistHashMap(mChangeRecords, 0x30);
    mDeferWrites = 0;
    mField22E = 0;
    mListenersBegin = nullptr;
    mListenersEnd = nullptr;
    mListenersCap = nullptr;
    mSet23CHead = nullptr;
    mSet23CSize = 0;
    mSet23CHead = NewTreeHead();
    gz::ConstructPersistHashMap(mKeyedObjects, 0x10);
    nGZLog::StoreCurrentThreadId(&mOwnerThreadId);
}

// The destructor 0x1d9cb0 and its adapter are in cTSPersistSystem.cpp (lane HZ): it stores our vtables, so that unit
// emits them too.

// cIGZUnknown: AddRef 0x1dab30 and Release 0x1e3980 jump to the system service's (the secondary faces' thunks adjust).
uint32_t cTSPersistSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSPersistSystem::Release() { return rz::cRZSystemService::Release(); }

// Primary slot 29, 0x1e36e0: the base's vf27 body (the same RVA as base slot 27).
uint8_t cTSPersistSystem::vf29(void *segment) { return static_cast<uint8_t>(Base()->vf27(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(segment)))); }

// The constructor's adapter.
cTSPersistSystem *__fastcall cTSPersistSystemAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSPersistSystem;
}
