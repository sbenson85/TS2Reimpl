#pragma once
#include "gz/cGZPersistDBSegment.h"
#include "std/StdHash.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

class cRZString;

// cGZPersistDBSegmentMultiPackedFiles: a database segment made of several DBPF .package files. Primary vtable
// 0xe60504 (50 slots: cGZPersistDBSegment and cIGZPersistDBSegment, cIGZUnknown at +0); the
// cIGZDBSegmentMultiPackedFiles subobject at +0x18 has vtable 0xe605d0 (11 slots). RVAs are 1.18.0.187.
//
// Object: 0x94 bytes (the deleting destructor frees that size). Constructor 0x9b8df0.
//
// Field map (primary offsets), from the code of the slots below:
//   +0x00  cGZPersistDBSegment base (0x18 bytes, see cGZPersistDBSegment.h): vptr, reference count, lock,
//          segment cache and namespace
//   +0x18  subobject vptr (vtable 0xe605d0); the subobject's own slots read this object through `this + 0x18`
//   +0x1c  byte, cleared by Shutdown
//   +0x20  dword, the segment ID (SetSegmentID stores it)
//   +0x24  cRZString (0x20 bytes: vptr, std::string, reference count): the directory path. GetPath passes its
//          address to the output object's slot 16; SetPath calls its slot 16 with the path
//   +0x44  cRZString: the file name pattern, "*.dat" after construction. The subobject's slot 3 forwards to its
//          slot 16
//   +0x64  byte "open" flag: Shutdown closes the segment (slot 9) when it is nonzero; SetPath refuses then
//   +0x68  vector of item pointers: begin, +0x6c end, +0x70 end of storage. The items are the
//          cIGZPersistDBSegment interfaces of cGZDBSegmentPackedFile objects (the packed file's +4); the
//          vector holds one reference to each. The subobject sees begin and end at +0x50 and +0x54.
//   +0x74  hash table (StdHashTable layout, 0x1c bytes) from resource key to item: the load factor (1.0f) at
//          +0x74, list head +0x78, element count +0x7c, buckets +0x80 .. +0x88, mask +0x8c; node = 0x1c bytes:
//          next, prev, the 16-byte key at +8, the item pointer at +0x18. The hash of a key is its third
//          dword (key + 8).
//   +0x90  dword (8, the initial bucket count)
// The item slots the record forwarders use are those of cIGZPersistDBSegment (slot 20 TestForRecord, 21
// GetRecordSize, 22 OpenRecord, 24 CloseRecord, 25 AbortRecord, 27 ReadRecord, 19 GetResourceKeyList).
//
// Not reimplemented here, and why: Open (0x9b9670), the subobject's slot 4 (0x9b9230, creates a packed file
// with the constructor 0x9a6ed0 of another class) and slot 9 (0x9b9020, inserts into the hash table and
// rehashes through 0x9a2d40). See db/status/cGZPersistDBSegmentMultiPackedFiles.jsonl.
class MultiPackedFilesSegmentView;

// The hash table at +0x74 as the instantiation for (resource key -> item pointer) uses it. `this` is the table.
struct MultiPackedFilesTable : StdHashTable {
    static constexpr uint32_t kNodeSize = 0x1c;

    // 0x1fc2f0: when the element count is nonzero, frees every node, makes the list empty (head linked to
    // itself), zeroes the count and sets every bucket entry to the head.
    void Clear();
    // 0x1fb410 (the destructor) is library code: DeletingDestructor destroys the table as MultiPackedFilesMap.
};
static_assert(sizeof(MultiPackedFilesTable) == sizeof(StdHashTable), "table layout");

// The table and the dword after it (+0x74 .. +0x94) as the toolchain's container, resource key (16 bytes) to item
// pointer: node {next, prev, pair<key, item>} of 0x1c bytes. Used where the table is destroyed.
struct ResourceKey16 {
    uint32_t words[4];
};
struct ResourceKey16Hash {
    size_t operator()(const ResourceKey16 &) const;
};
struct ResourceKey16Equal {
    bool operator()(const ResourceKey16 &, const ResourceKey16 &) const;
};
using MultiPackedFilesMap = std::unordered_map<ResourceKey16, void *, ResourceKey16Hash, ResourceKey16Equal>;
static_assert(sizeof(MultiPackedFilesMap) == sizeof(StdHashTable) + 4, "the table and the bucket count are a std::unordered_map");

class cGZPersistDBSegmentMultiPackedFiles {
public:
    static constexpr uint32_t kVTable = 0xe60504;
    static constexpr uint32_t kSubVTable = 0xe605d0;
    static constexpr uint32_t kIdSelf = 0xe90f85f3; // cIGZDBSegmentMultiPackedFiles: +0x18 (the subobject)
    static constexpr int kCloseSlot = 9;            // the primary's slot 0x24: Close
    static constexpr uint32_t kSize = 0x94;

    // Constructor, 0x9b8df0 (returns this; the base's constructor 0x99b790 runs first).
    cGZPersistDBSegmentMultiPackedFiles *Construct();
    // Slot 33, the deleting destructor 0x9b8ef0: writes the vtables, destroys the table, frees the item
    // vector's storage, destroys the two strings, runs the base destructor (0x99b8e0: vtable, releases +0x14 and
    // +0x10, destroys the lock) and frees kSize bytes when bit 0 of `flags` is set. Returns this.
    cGZPersistDBSegmentMultiPackedFiles *DeletingDestructor(uint32_t flags);

    // Slot 0, 0x9b9950. Id 0xe90f85f3 stores this + 0x18 in *ppvObj and calls AddRef through this's vtable (slot
    // 1). Any other id goes to the base's QueryInterface (cGZPersistDBSegment, 0x99cf70) with the same arguments.
    bool QueryInterface(uint32_t riid, void **ppvObj);
    // Slot 4, 0x9b9a60: if the open flag is set, calls Close (slot 9) through this's vtable; clears +0x1c; then the
    // base's Shutdown (0x99de70) and returns its answer.
    bool Shutdown();
    // Slot 9, 0x9b91b0: under the lock, closes every item (its slot 9, then slot 4, then slot 2), empties the item
    // vector without freeing it, clears the table, clears the open flag and unlocks. Returns true.
    bool Close();
    // Slot 11, 0x9b9630: calls slot 16 of `out` with the address of the embedded object at +0x24 (void).
    void GetPath(void *out);
    // Slot 12, 0x9b9a20: refuses (false) while the open flag is set; otherwise calls slot 16 of the embedded
    // object at +0x24 with `path` and returns true.
    bool SetPath(void *path);
    // Slot 16, 0x9b9a50: +0x20 = *id. Returns true.
    bool SetSegmentID(const uint32_t *id);
    // Slots 35, 36, 37, 39, 40 and 42: the record operations of the segment. While the open flag is set they
    // look the key up in the table and forward to the matching item's slot with the same arguments. When the
    // flag is clear or the key is not in the table, they return false (0 for the sizes and reads).
    // Slot 35, 0x9b9570: item slot 20 (TestForRecord)(key).
    bool TestForRecordImpl(const uint32_t *key);
    // Slot 36, 0x9b9410: item slot 21 (GetRecordSize)(key).
    uint32_t GetRecordSizeImpl(const uint32_t *key);
    // Slot 37, 0x9b94b0: item slot 22 (OpenRecord)(key, a, b).
    bool OpenRecordImpl(const uint32_t *key, uint32_t a, uint32_t b);
    // Slot 39, 0x9b93a0: `record`'s slot 3 gives the key; item slot 24 (CloseRecord)(record).
    bool CloseRecordImpl(void *record);
    // Slot 40, 0x9b9340: `record`'s slot 3 gives the key; item slot 25 (AbortRecord)(record).
    bool AbortRecordImpl(void *record);
    // Slot 42, 0x9b9510: item slot 27 (ReadRecord)(key, a, b).
    uint32_t ReadRecordImpl(const uint32_t *key, uint32_t a, uint32_t b);
    // Slot 44, 0x9b9400 (one argument, ignored): returns the dword at +0x7c, the table's element count.
    uint32_t Field7C(uint32_t unused);
    // Slot 45, 0x9b9460 (two arguments): the sum over the items of item->slot 19(a, b), where the count is the
    // subobject's slot 7 (ItemCount) and each item is read from +0x68 again for every call.
    uint32_t SumItems(uint32_t a, uint32_t b);

    // The subobject at +0x18 (this is the subobject for its methods).
    MultiPackedFilesSegmentView *View();

    // The item for a key, or null: the table lookup shared by the record operations. The open flag is not
    // looked at here.
    void *FindItem(const uint32_t *key);

    cGZPersistDBSegment mBase;   // +0x00, 0x18 bytes
    void *mSubVTable;            // +0x18
    uint8_t mState1C;            // +0x1c
    uint32_t mSegmentID;         // +0x20
    void *mPathVTable;           // +0x24 (the path string's vptr; its address is this + 0x24)
    uint8_t mPathBody[0x44 - 0x28];
    void *mPatternVTable;        // +0x44 (the pattern string's vptr)
    uint8_t mPatternBody[0x64 - 0x48];
    uint8_t mOpen64;             // +0x64
    uint8_t mUnmapped65[0x68 - 0x65];
    void **mItemsBegin;          // +0x68
    void **mItemsEnd;            // +0x6c
    void **mItemsCapEnd;         // +0x70
    MultiPackedFilesTable mTable; // +0x74 .. +0x90
    uint32_t mBucketCount;       // +0x90

    cRZString *PathString() { return reinterpret_cast<cRZString *>(&mPathVTable); }
    cRZString *PatternString() { return reinterpret_cast<cRZString *>(&mPatternVTable); }
};

static_assert(sizeof(cGZPersistDBSegmentMultiPackedFiles) == 0x94, "multi packed files: 0x94 bytes");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mBase) == 0, "multi packed files: base at +0");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mSubVTable) == 0x18, "multi packed files: subobject at +0x18");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mState1C) == 0x1c, "multi packed files: state at +0x1c");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mSegmentID) == 0x20, "multi packed files: ID at +0x20");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mPathVTable) == 0x24, "multi packed files: embedded at +0x24");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mPatternVTable) == 0x44, "multi packed files: embedded at +0x44");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mOpen64) == 0x64, "multi packed files: flag at +0x64");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mItemsBegin) == 0x68, "multi packed files: items at +0x68");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mItemsEnd) == 0x6c, "multi packed files: items end at +0x6c");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mItemsCapEnd) == 0x70, "multi packed files: items cap at +0x70");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mTable) == 0x74, "multi packed files: table at +0x74");
static_assert(offsetof(cGZPersistDBSegmentMultiPackedFiles, mBucketCount) == 0x90, "multi packed files: bucket count at +0x90");

// The cIGZDBSegmentMultiPackedFiles subobject, at +0x18 of cGZPersistDBSegmentMultiPackedFiles. `this` here is the
// subobject; its fields are reached through the pointer at the offsets of the primary minus 0x18.
class MultiPackedFilesSegmentView {
public:
    static constexpr int kCountSlot = 7;
    static constexpr int kItemSlot = 8;

    // Slot 0, 0x9b997f: the primary's QueryInterface with this - 0x18.
    bool QueryInterface(uint32_t riid, void **ppvObj);
    // Slot 1, 0x9b9012: adds one reference (RZAtomicAdd on the primary's count at +4, through this - 0x18).
    uint32_t AddRef();
    // Slot 2, 0x9b9995: the base's Release with this - 0x18.
    uint32_t Release();
    // Slot 3, 0x9b9a40: slot 16 of the pattern string (cRZString::operator=) with the same argument; its answer
    // is returned.
    void *SetPattern(void *pattern);
    // Slot 5, 0x9b92d0 (two arguments): the sum over the items (count from slot 7, item from slot 8) of slot 35 of
    // the item's packed file (QueryInterface with 0x286d4a6a) called with (a, b, 0, 1).
    uint32_t SumPackedFiles(uint32_t a, uint32_t b);
    // Slot 6, 0x9b95c0 (key, out): under the lock, when the segment is open and the key is in the table, stores
    // the item in *out and AddRefs it; returns whether it did.
    bool GetItem(const uint32_t *key, void **out);
    // Slot 7, 0x9b9660: (end - begin) / 4 as a signed shift (a negative span gives a negative count).
    uint32_t ItemCount() const;
    // Slot 8, 0x9b9650: the item at `index` of the array at +0x50 of the subobject (primary +0x68). No bounds check.
    void *ItemAt(uint32_t index) const;
    // Slot 10, 0x9b99a0 (key, one more argument that is not read): removes the key's node from the table and
    // frees it. The item is not released. Nothing when the key is not in the table.
    void RemoveItem(const uint32_t *key, uint32_t unused);
};
