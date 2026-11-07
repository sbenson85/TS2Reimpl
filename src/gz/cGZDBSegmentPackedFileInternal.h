#pragma once
// Helpers shared by the cGZDBSegmentPackedFile*.cpp files (the class was one 3200-line file).
#include "gz/cGZDBSegmentPackedFile.h"
#include "app/AppServices2.h"
#include "app/ExceptionReporter.h"
#include "app/IniFile.h"
#include "app/ReportBlockers.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZDBPFIndexCompressed.h"
#include "gz/cGZDBReadRecord.h"
#include "gz/cGZDBSerialRecordAsIGZFile.h"
#include "gz/cGZDBPFIndexModifiable.h"
#include "gz/cGZDBPFIndexStatic.h"
#include "gz/cGZDBWriteRecordPackedFile.h"
#include "gz/cGZPersistDBSegment.h"
#include "gz/cGZPersistDBSerialRecord.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cGZDBRecordPackedFile.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZDBSegmentPackedFile.h"
#include "ifaces/cIGZFile.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistDBSerialRecord.h"
#include "rz/GeneralAllocator.h"
#include "rz/GameAllocator.h"
#include "rz/PathFileOps.h"
#include "rz/RZMemory.h"
#include "rz/StringKeyParser.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFastCompression3.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "rz/cRZRefCount.h"
#include <cstring>
#include <map>
#include <new>
#include <set>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace gz_dbsegment_detail {

// A resource key as the interfaces take it: the four dwords the file code passes around.
inline const cGZPersistResourceKey *AsKey(const uint32_t *key)
{
    return reinterpret_cast<const cGZPersistResourceKey *>(key);
}

// The original copies the caller's key onto its stack (after taking the lock) and hands the index that copy, not the
// caller's pointer. A struct of four scalars (not an array) is copied with dword moves and gets no /GS cookie.
struct KeyCopy {
    uint32_t a, b, c, d;
    explicit KeyCopy(const uint32_t *key) : a(key[0]), b(key[1]), c(key[2]), d(key[3]) {}
    const cGZPersistResourceKey *Get() const { return reinterpret_cast<const cGZPersistResourceKey *>(this); }
};

// The packed file a SegmentView (its +4 face) belongs to.
inline cGZDBSegmentPackedFile *Primary(SegmentView *view)
{
    return reinterpret_cast<cGZDBSegmentPackedFile *>(reinterpret_cast<uint8_t *>(view) - 4);
}

inline const cGZDBSegmentPackedFile *Primary(const SegmentView *view)
{
    return reinterpret_cast<const cGZDBSegmentPackedFile *>(reinterpret_cast<const uint8_t *>(view) - 4);
}

// The key order of the tree: (key[0], key[1], key[3], key[2]), each compared unsigned.
inline bool KeyLess(const uint32_t *a, const uint32_t *b)
{
    if (a[0] != b[0]) return a[0] < b[0];
    if (a[1] != b[1]) return a[1] < b[1];
    if (a[3] != b[3]) return a[3] < b[3];
    return a[2] < b[2];
}

// A std::map<unsigned, unsigned> whose nodes come from the game's allocator (GameAllocator: RZMalloc / RZFree): the type of the
// trees at +0x184, +0x18c and +0x194 (head pointer and size, 8 bytes; 0x18-byte nodes with the key at +0x10 and
// the value at +0x14).
using RangeMap = std::map<uint32_t, uint32_t, std::less<uint32_t>, GameAllocator<std::pair<const uint32_t, uint32_t>>>;
static_assert(sizeof(RangeMap) == 8, "RangeMap: head pointer and size");


// An iterator is its node pointer when iterator debugging is off (it is, in this build).
static_assert(sizeof(RangeMap::iterator) == sizeof(void *) && sizeof(RangeMap::const_iterator) == sizeof(void *),
              "map iterators are plain node pointers");

inline void *NodeOf(RangeMap::const_iterator it)
{
    void *node;
    std::memcpy(&node, &it, sizeof(node));
    return node;
}

inline RangeMap &Ranges(RangeTree *tree)
{
    return *reinterpret_cast<RangeMap *>(tree);
}

// A new, empty record store (the original: operator new(0x24) and the constructor 0x9a6cf0, whose hook builds ours too).
inline ifaces::cIGZDBPFIndex *NewModifiableIndex()
{
    return reinterpret_cast<ifaces::cIGZDBPFIndex *>(static_cast<cIGZDBPFIndex *>(new cGZDBPFIndexModifiable));
}

struct StoreKey {
    uint32_t w[4];
};

// The tree at +0x110: 16-byte keys ordered by KeyLess, one pointer each (to a pending record object), nodes of
// 0x24 bytes from RZMalloc.
struct PendingOrder {
    bool operator()(const StoreKey &a, const StoreKey &b) const { return KeyLess(a.w, b.w); }
};
using PendingMap = std::map<StoreKey, void *, PendingOrder, GameAllocator<std::pair<const StoreKey, void *>>>;
static_assert(sizeof(PendingMap) == 8, "PendingMap: head pointer and size");

template <typename Map> inline typename Map::iterator MapIterator(void *node)
{
    static_assert(sizeof(typename Map::iterator) == sizeof(void *), "map iterators are plain node pointers");
    typename Map::iterator it;
    std::memcpy(&it, &node, sizeof(node));
    return it;
}

// The handle of a pending record: its sub-object (the record's address plus 4), or null.
inline ifaces::cIGZPersistDBRecord *SubOf(void *record)
{
    return record != nullptr ? reinterpret_cast<ifaces::cIGZPersistDBRecord *>(static_cast<uint8_t *>(record) + 4) : nullptr;
}

// 0x9ae5c0 (ours: LoadCachedIndex): the index from the segment cache. Answers nonzero in al when it did the work.
inline uint8_t LoadIndex(void *primary)
{
    return static_cast<cGZDBSegmentPackedFile *>(primary)->LoadCachedIndex();
}

// What the slots that need the record store do first: when none exists, load the index, and when that answers 0, let
// EnsureLoaded try the auto-open.
inline void LoadStoreIfAbsent(cGZDBSegmentPackedFile *file)
{
    if (file->mIndex != nullptr) return;
    if (LoadIndex(file) == 0) file->EnsureLoaded();
}

// Empties one of the free-range trees (+0x184, +0x18c, +0x194): the library map's clear, which the game's code runs at
// 0x9b2a60 (library match) or inline.
inline void ClearRanges(void *tree)
{
    Ranges(static_cast<RangeTree *>(tree)).clear();
}

} // namespace gz_dbsegment_detail
