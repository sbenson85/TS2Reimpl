#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "game/owned.h"
#include "gz/cGZPersistDBSegment.h"
#include "ifaces/cIGZDBPFIndex.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "rz/GameAllocator.h" // GZServiceAllocator

class cIGZString;

// The reference-count maps (+0x1a0, +0x1a4: type or group -> number of records) and the sets of ids that are stored
// compressed (+0x1ac, +0x1b0).
using RefMap = std::unordered_map<uint32_t, uint32_t>;
using IdSet = std::unordered_set<uint32_t>;

// The allocator service's allocate and free (slot 3 / slot 4 of the cached service: AllocateViaAllocatorService /
// FreeViaAllocatorService, src/app/AppServices2.h) for code that holds raw blocks. Containers use GZServiceAllocator
// (src/rz/GameAllocator.h).
void *GZServiceAllocate(size_t bytes);
void GZServiceFree(void *memory);

using GZRangeVector = std::vector<std::pair<uint32_t, uint32_t>, GZServiceAllocator<std::pair<uint32_t, uint32_t>>>;

// What cGZDBSegmentPackedFile::GetInfo (slot 51) fills in. The two vectors take (start, length) pairs of the free
// ranges and are appended to, not cleared.
struct PackedFileInfo {
    uint32_t fileLength;                                  // +0x00: the file object's slot 7
    uint32_t storeBegin;                                  // +0x04: store slot 4 minus indexSize (0 without a store)
    uint32_t storeEnd;                                    // +0x08: store slot 5 minus indexSize
    uint32_t zero0C;                                      // +0x0c: 0
    uint32_t headerBytes;                                 // +0x10: 0x60
    uint32_t extentA, extentALength;                      // +0x14, +0x18: the object's +0x140, +0x144
    uint32_t extentB, extentBLength;                      // +0x1c, +0x20: the object's +0x14c, +0x150
    uint32_t indexOffset;                                 // +0x24: the DIR record's offset, 0 when there is none
    uint32_t indexSize;                                   // +0x28: its size
    GZRangeVector freeA;                                  // +0x2c: ranges of the tree at +0x184
    GZRangeVector freeB;                                  // +0x38: ranges of the tree at +0x18c
    uint32_t totalA;                                      // +0x44: their summed lengths
    uint32_t totalB;                                      // +0x48
};
static_assert(sizeof(PackedFileInfo) == 0x4c, "PackedFileInfo layout");

// The 0x60-byte DBPF package header as cGZDBSegmentPackedFile keeps it at +0x118 (offsets are into the header).
struct DBPFHeader {
    uint32_t magic;             // +0x00 'DBPF'
    uint32_t majorVersion;      // +0x04
    uint32_t minorVersion;      // +0x08
    uint32_t userVersionMajor;  // +0x0c
    uint32_t userVersionMinor;  // +0x10
    uint32_t flags;             // +0x14
    uint32_t created;           // +0x18 (slot 13 GetDates)
    uint32_t modified;          // +0x1c
    uint32_t indexMajorVersion; // +0x20 (7)
    uint32_t indexCount;        // +0x24
    uint32_t indexOffset;       // +0x28
    uint32_t indexSize;         // +0x2c
    uint32_t holeCount;         // +0x30
    uint32_t holeOffset;        // +0x34
    uint32_t holeSize;          // +0x38
    uint32_t indexMinorVersion; // +0x3c (Flush writes 1 or 2; slot 48 returns it)
    uint32_t groupInfo;         // +0x40 (slots 25/26; lane CN's tentative name)
    uint8_t reserved[0x1c];     // +0x44 (zeroed before a header write)
};
static_assert(sizeof(DBPFHeader) == 0x60, "DBPFHeader layout");

// The free-range trees at +0x184, +0x18c and +0x194 of cGZDBSegmentPackedFile: a std::map<unsigned, unsigned>
// (range start -> length) whose 0x18-byte nodes come from RZMalloc and RZFree; head pointer and size, 8 bytes.
struct RangeTree {
    void *head;
    uint32_t size;
    // (0x9b2e20, map::try_emplace with a hint, is a library match: no entry.)
    // 0x9ad570 (cdecl): adds the free range [offset, offset + length), merging it with a range that starts
    // where it ends and with one that ends where it starts (erased nodes go back through RZFree).
    static void __cdecl AddRange(RangeTree *tree, uint32_t offset, uint32_t length);
    // 0x9b2bb0 (this = the tree; two stack arguments, popped): erases the node (extracted and freed through RZFree)
    // and stores the node after it in *out. Returns out.
    void **Erase(void **out, void *node);
};
static_assert(sizeof(RangeTree) == 8, "RangeTree layout");

// A tree of 16-byte keys (std::map node layout, MSVC: left, parent, right, colour byte, nil byte), as at
// +0x110 of cGZDBSegmentPackedFile. `this` is the address of the head pointer (object + 0x110).
struct KeyTreeNode {
    KeyTreeNode *left;   // +0x00
    KeyTreeNode *parent; // +0x04
    KeyTreeNode *right;  // +0x08
    uint8_t color;       // +0x0c
    uint8_t isNil;       // +0x0d
    uint8_t pad[2];      // +0x0e
    uint32_t key[4];     // +0x10: ordered as (key[0], key[1], key[3], key[2])
    uint32_t value;      // +0x20
};
static_assert(sizeof(KeyTreeNode) == 0x24, "KeyTreeNode layout");

struct KeyTree {
    KeyTreeNode *head;
    uint32_t size;
    // 0x9b2bf0 (two stack arguments): lower bound of the key; *out = that node when its key is not above
    // the search key, else the head node. Returns out.
    KeyTreeNode **Find(KeyTreeNode **out, const uint32_t *key);
    // 0x9a3900 (two stack arguments: the allocator, which is the tree itself, and the subtree root): frees
    // the nodes of the subtree (right subtree first, then the node, then down the left) through RZFree.
    void EraseSubtree(const void *allocator, KeyTreeNode *root);
    // 0x9a6980 (this = the pending-record tree at +0x110 of the file; two stack arguments, popped): inserts the pair at `pair`
    // (16-byte key, then the record pointer) after any equal keys, without looking for them (allocating a 0x24-byte node
    // through RZMalloc). *out = the new node. Returns out.
    void **Insert(void **out, const uint8_t *pair);
};

// cGZDBSegmentPackedFile: a DBPF .package file segment. Primary vtable 0xe5fc58 (59 slots:
// cIGZDBSegmentPackedFile and cIGZUnknown at +0); the cGZPersistDBSegment subobject at +4 has vtable
// 0xe5fd48 (50 slots). RVAs are 1.18.0.187.
//
// Field map, from the code of the slots below and of the ones not reimplemented yet:
//   +0x00  primary vtable (59 slots)
//   +0x04  cGZPersistDBSegment subobject vtable (50 slots); the subobject's slots are in SegmentView
//   +0x08  reference count (int32), shared by both interfaces: AddRef and Release update it with
//          RZAtomicAdd. The subobject's own AddRef (slot 1 of +0x04) and Release (thunked to the
//          primary's slot 2) act on the same counter.
//   +0x0c  lock object, embedded here (its vtable pointer is this word). Slot 1 = Lock, slot 2 = Unlock,
//          called with the lock's address (this + 0x0c) as `this`. Used by vf10..vf12, vf14, vf15,
//          vf26, vf38, vf41..vf43, vf52 and SetPath.
//   +0x20  dword, the segment ID (SegmentView::SetSegmentID stores it; sub-relative +0x1c)
//   +0x24  state byte: bits 0..1 nonzero means open (vf7 refuses to set +0xf4 then). Bit 1 is the
//          writable bit read by SegmentView::IsWritable (through the dword at +0x20 from the subobject,
//          i.e. the dword at +0x24 here). IsOpen and IsWritable read it through the subobject.
//   +0x28  byte, set by SegmentView::AutoOpen (sub-relative +0x24)
//   +0x29  byte, set by SegmentView::AutoOpen (sub-relative +0x25)
//   +0x2a  byte (vf17)
//   +0x30  pointer to a record store object (slots 11, 12); used by IsRecordCompressed, GetCompressedStoredSize,
//          SetRecordFullSize and GetRecordFlag
//   +0x34  byte (vf24); SetRecordFullSize sets it to 1
//   +0x38  object embedded here (its vtable pointer is this word). SetPath calls its slot 16 with `this`
//          = +0x38.
//   +0x3c  path: std::string (MSVC layout: buffer at +0x3c, size at +0x4c, capacity at +0x50). vf8
//          (GetPath) passes it to the output string's FromChar(data, size).
//   +0x58  object embedded here (vtable pointer is this word). SetBuffering calls its slot 32 with `this` = +0x58.
//   +0xf4  dword (vf7)
//   +0xf8  dword (SetBuffering stores the first argument), +0xfc dword (SetBuffering stores the second)
//   +0x104 dword (vf11 reads, vf12 writes), +0x108 dword (same)
//   +0x124 dword (vf10 reads), +0x128 dword (same)
//   +0x158 dword (vf26), +0x178 byte set to 1 by vf26 when +0x158 changes; read by vf24
//   +0x17c dword (vf15)
//   +0x180 float (vf16)
//   +0x19d byte (vf24)
//   +0x1a8 byte (vf49), +0x1a9 byte (vf50), +0x1aa byte (vf38)
// Further fields, from the later members: +0x110 is the tree of pending records (16-byte key, record pointer; nodes
// of 0x24 bytes), +0x184 / +0x18c / +0x194 are the free-range trees (std::map<unsigned, unsigned>, 0x18-byte nodes,
// all RZMalloc: RangeTree), +0x19d is a "free ranges changed" flag, +0x19f the reference-counting switch for the
// two unordered_map<unsigned, unsigned> at +0x1a0 / +0x1a4, +0x1ac / +0x1b0 the unordered_set<unsigned> of allowed
// types / groups, +0x1aa "accept everything", +0x30 the record store (a cIGZDBPFIndex), +0x118..+0x154 the header
// copy (CheckExtents), +0x178 / +0x34 dirty flags. vf20 allocates a cGZDBSerialRecordAsIGZFile (primary vtable
// 0xe5fbb0, secondary 0xe5fbd0, from db/classes.jsonl).
//
// Calls through other objects' vtables (this's slot 1 for AddRef, the subobject's slot 34 for the
// deleting destructor, the lock's slots 1 and 2, the output string's slot 3, the objects at +0x30,
// +0x38 and +0x58) are made as the original makes them: through the vtable, with the same arguments
// in the same order.
class cGZDBSegmentPackedFile {
public:
    static constexpr uint32_t kVTable = 0xe5fc58;    // the game's tables; the VTable() functions are their owned symbols
    static constexpr uint32_t kSubVTable = 0xe5fd48;
    static const void *VTable();                     // the word our Construct and DeletingDestructor store at +0
    static const void *SubVTable();                  // and at +4 (the cGZPersistDBSegment subobject)
    static const void *InterfaceVTable();            // 0xe5f45c, cIGZDBSegmentPackedFile: stored first by Construct
    static const void *OwnVTable();                  // rzface::cGZDBSegmentPackedFile's: the image-free VTable()
    static const void *OwnSubVTable();               // rzface::cGZDBSegmentPackedFileSub's
    static const void *OwnInterfaceVTable();         // rzface::cIGZDBSegmentPackedFile's
    static constexpr uint32_t kIdSelf = 0x286d4a6a;  // cIGZDBSegmentPackedFile: +0
    static constexpr uint32_t kIdSub = 0x656b8f0c;   // cIGZPersistDBSegment: +4 (also accepted: 1)
    static constexpr uint32_t kIdSubUnknown = 1;     // cIGZUnknown: +4
    static constexpr int kDeleteSlot = 34;           // subobject's slot 0x88: deleting destructor
    static constexpr uint32_t kRecordFileVTable = 0xe5fbb0;    // object made by CreateRecordFile: primary
    static constexpr uint32_t kRecordFileSubVTable = 0xe5fbd0; // and its +4 base

    // Slot 0, 0x9adaa0: the primary's own id returns this; the subobject's ids (0x656b8f0c and 1) return
    // this + 4. Each accepted id calls AddRef on the returned object through its vtable.
    bool QueryInterface(uint32_t riid, void **ppvObj);
    // Slot 2, 0x9af980 (+0x04 thunk: this + 4 is passed to the base's release, 0x99d070): decrements
    // the count with RZAtomicAdd; at zero, sets it to 1 and calls the subobject's slot 34 with no
    // arguments. Returns 0 after a delete, otherwise the new count.
    uint32_t Release();
    // Slot 7, 0x9b0160: refuses (false) while the state byte has bits 0..1 set; else stores the dword.
    bool SetBaseOffset(uint32_t value);
    // Slot 8, 0x9ac850: out->FromChar(path data, path size), out's slot 3 (cIGZString, this = out).
    void GetPath(cIGZString *out);
    // Slot 9, 0x9b0230 (one stack argument): under the lock, if the state byte has bits 0..1 clear,
    // calls slot 16 of the object embedded at +0x38 with `path` and returns true; otherwise false.
    bool SetPath(void *path);
    // Slot 10, 0x9ab940: under the lock, *a = +0x124, *b = +0x128.
    void GetFileUserVersion(uint32_t *a, uint32_t *b);
    // Slot 11, 0x9ac7c0: under the lock, *a = +0x104, *b = +0x108.
    void GetUserVersion(uint32_t *a, uint32_t *b);
    // Slot 12, 0x9b0200: under the lock, +0x104 = a, +0x108 = b.
    void SetUserVersion(uint32_t a, uint32_t b);
    // Slot 14, 0x9b0100 (two stack arguments): under the lock, asks slot 32 of the object embedded at
    // +0x58 whether to take (a, b). If it says yes, +0xf8 = a and +0xfc = b. Returns the answer.
    bool SetBuffering(uint32_t a, uint32_t b);
    // Slot 15, 0x9b01b0: under the lock, +0x17c = value. Returns true.
    bool SetOptions(uint32_t value);
    // Slot 16, 0x9b0190: +0x180 = value.
    void SetCompactRatio(float value);
    // Slot 17, 0x9b0180: +0x2a = low byte of value.
    void SetFlag2A(uint8_t value);
    // Slot 24, 0x1d3dd0: true when +0x34, +0x178 or +0x19d is nonzero.
    bool IsDirty();
    // Slot 26, 0x9b0280: under the lock, when value differs from +0x158, stores it and sets +0x178.
    void SetGroupInfo(uint32_t value);
    // Slot 38, 0x9aacc0: under the lock, +0x1aa = low byte of value. Returns true.
    bool SetCompressAll(uint8_t value);
    // Slot 41, 0x9ac9b0 (one stack argument, a pointer to four dwords): under the lock, copies the record
    // and calls slot 11 of the object at +0x30 with (copy, byte +0x1a8, byte +0x1a9). If that returns a
    // record: true when (record[2] & 0x7fffffff) > record[1] (unsigned). Otherwise false.
    bool IsRecordCompressed(const uint32_t *record);
    // Slot 42, 0x9b02c0 (three stack arguments): under the lock, the same lookup through slot 12 of the
    // object at +0x30. If it returns a record, record[2] keeps its top bit and takes the low 31 bits of
    // (useArg == 0 ? record[1] : value), and +0x34 = 1.
    void SetRecordFullSize(const uint32_t *record, uint8_t useArg, uint32_t value);
    // Slot 43, 0x9ab8c0: under the lock, the lookup through slot 11. Returns record[1] when the record
    // exists and (record[2] & 0x7fffffff) > record[1] (unsigned); otherwise 0xffffffff.
    uint32_t GetCompressedStoredSize(const uint32_t *record);
    // Slot 52, 0x9ad660: under the lock, the lookup through slot 11. Returns the top bit of record[2]
    // when a record exists, otherwise false.
    bool GetRecordFlag(const uint32_t *record);
    // 0x9abb50 (one stack argument; no lock): the lookup through slot 11 of the store at +0x30 with the record
    // copy and the bytes +0x1a8, +0x1a9. Returns 0xffffffff when there is no record, else the record's
    // (hit[2] & 0x7fffffff) when that is above hit[1], else 0xffffffff.
    uint32_t LookupRecordEnd(const uint32_t *record);
    // Slot 47, 0x9abde0 (two stack arguments): *count = 0 and *total = 0; false when the file is not open
    // (state bits 0..1). Otherwise walks the trees at +0x184 and +0x18c in key order, adding one to *count and
    // the node's value to *total for each node, and returns true.
    bool GetFreeRangeTotals(uint32_t *count, uint32_t *total);
    // 0x9b0dc0: the header sanity check. +0x118 must equal the game's global at 0x10f5030, +0x11c be at most 1,
    // +0x13c, +0x148 below 0x7ffffff, the two extents (+0x140, +0x144) and (+0x14c, +0x150) lie inside the length
    // the file object at +0x58 reports (its slot 7, asked again for every test); then returns whether +0x154 is
    // nonzero.
    bool CheckExtents();
    // 0x9ab860 (three stack arguments): adds the range (offset, length) to one of the three free-range trees,
    // unless the offset or the length is zero (then it does nothing). The tree is chosen from the byte
    // `which`: when bit 4 of +0x17c is clear it is the tree at +0x184; otherwise tree (which ^ 1) of the three
    // at +0x184, +0x18c, +0x194 (the byte is not range checked in the original, so only 0..3 are valid; a
    // larger value indexes past the object). Sets +0x19d to 1 when it added. Always returns true.
    bool AddFreeRangeTo(uint32_t offset, uint32_t length, uint8_t which);
    // Slot 45, 0x9ab820 (two stack arguments): the same into the tree at +0x184 only.
    bool AddFreeRange(uint32_t offset, uint32_t length);
    // Slot 39, 0x9aac00 (two stack arguments: value, add): under the lock, edits the unordered_set<unsigned>
    // at +0x1ac (the set of allowed resource types). add != 0: create the set when absent (operator new(0x20),
    // default constructed) and insert the value. Otherwise erase the value from the set and, when that leaves it
    // empty, destroy and delete it and null the pointer. Returns false in both cases.
    // ORIGINAL-BUG: removing while no set exists dereferences a null pointer (the erase runs before any check).
    bool EditCompressedTypes(uint32_t value, bool add);
    // Slot 40, 0x9aab40: the same for the set at +0x1b0 (the allowed groups).
    bool EditCompressedGroups(uint32_t value, bool add);
    // 0x9b03a0 (two stack arguments: id pointer, size): false when size - 50 is above 15999950 (so size outside
    // 50..16000000) or when id[0] is 0xe86b1eef (the directory resource type). True when +0x1aa is nonzero.
    // Otherwise true when id[0] is in the set at +0x1ac, or else when id[1] is in the set at +0x1b0.
    bool PassesFilter(const uint32_t *id, uint32_t size);
    // 0x9ad0f0 (three stack arguments: key1, key2, increment): when +0x19f is nonzero, adjusts the reference
    // counts in the two unordered_map<unsigned, unsigned> at +0x1a0 (by key1) and +0x1a4 (by key2). increment:
    // a present key's count goes up by one and an absent key is inserted with 1. Otherwise a present key's count
    // goes down by one when it is above 1 (signed) and the entry is erased when it is not; an absent key is left
    // alone. Nothing happens when +0x19f is zero. No return value.
    void AdjustRefs(uint32_t key1, uint32_t key2, bool increment);
    // 0x9ad3b0 (no arguments): when +0x19f is nonzero, asks the record store at +0x30 for its entries (slot 10,
    // with a vector of 16-byte entries and 0) and counts them into the two maps: +1 for each entry's first dword
    // in the map at +0x1a0 and for its second dword in the map at +0x1a4 (absent keys are inserted with 1). The
    // vector's block goes back through FreeThroughAllocatorService (begin, count of entries).
    void RebuildRefs();
    // Slot 33, 0x9aacf0 (one stack argument, a byte): under the lock, when it differs from +0x19f: stores it,
    // deletes both maps (destroyed and freed with operator delete(p, 0x20), pointers nulled) and, when it is
    // nonzero, creates two empty maps and rebuilds the counts (RebuildRefs). Returns what the lock's Unlock
    // returned in eax (the original ends with that call and leaves its result in place).
    uint32_t SetRefTracking(uint8_t enable);
    // 0x9a8220 (no arguments): false when the file is not open (state bits 0..1). True when the pending-record
    // tree at +0x110 is empty by its size field (+0x114). Otherwise takes one reference on the sub-object (record
    // + 4, or null) of every pending record in key order (AddRef for a temporary, AddRef for the list, Release of
    // the temporary), calls the subobject's slot 24 (CloseRecord) with each of them in the same order, releases
    // them in the same order, and returns true.
    bool FlushOpenRecords();
    // Slot 31, 0x9acfd0: under the lock, how many different type ids (key[0]) there are: the size of the
    // reference-count map at +0x1a0 when it exists, otherwise the size of the set made of what slot 8 of the
    // record store puts into an (initially empty) std::set<unsigned> and the first key words of the tree at +0x110.
    uint32_t CountTypes();
    // Slot 32, 0x9aceb0: the same for the groups: the map at +0x1a4, store slot 9, and key[1].
    uint32_t CountGroups();
    // 0x9a7e00 (two stack arguments: out offset, length): finds room for `length` bytes in the file. False (without
    // touching anything) unless the file is writable (state bit 1). Reads the file's length (the file at +0x58,
    // slot 7). When bit 1 of +0x17c is set the free-range tree at +0x184 is used: ranges that are not inside the
    // file (start at or past the length, or end past it) are erased on the way (setting +0x19d), and the smallest
    // range that is long enough is taken: *out = its start, the range is erased and the remainder (when any) is
    // inserted again at start + length. Otherwise, or when no range is long enough, if the last range ends exactly
    // at the end of the file the file is grown through its slot 8 (SetLength) by the missing bytes, *out is that
    // range's start and the range is erased; when that growth fails (or the last range does not reach the end),
    // the file is grown by the whole length (slot 8, tried again with the same size after a failed first try) and
    // *out is the old length. False when the growth fails; *out then still holds the last range's start if that
    // was stored.
    bool AllocateSpaceUnlocked(uint32_t *outOffset, uint32_t length);
    // Slot 44, 0x9a7d20 (two stack arguments, passed on to AllocateSpaceUnlocked): when +0x17c has bits 1 and 3 set
    // (& 0xa == 0xa): writes the 4 bytes at the game address 0xe5f440 to offset 0 through the primary's slot 58
    // (WriteAt) when +0x178 is clear (and sets it); then hands the pending extents (+0x14c/+0x150 and +0x140/+0x144)
    // to AddFreeRangeTo (tree selector 1), zeroing them and setting +0x178, +0x19d (first) / +0x34 (second).
    bool AllocateSpace(uint32_t *outOffset, uint32_t length);
    // Slot 37, 0x9b0c70 (no stack arguments): under the lock, EnsureLoaded; the result starts as "the file is open"
    // (state bits 0..1). Asks the record store (+0x30, slot 10) for its entries (a vector of 16-byte keys, filled
    // with the vector and 0); for each entry whose lookup (slot 11, ORIGINAL-BUG: not checked for null) has data
    // ((record[2] & 0x7fffffff) > record[1]) calls the subobject's slot 37 (entry, &object, 1); when that fails the
    // object (if any) is released, the result becomes false and the walk stops; otherwise the object's slot 6 is
    // called (ORIGINAL-BUG: the object is not checked for null) and it is released. The entries' block is freed
    // through FreeThroughAllocatorService.
    bool VisitAllRecords();
    // Slot 34, 0x9a8f00 (four stack arguments: path, a2, a3, a4; the last two are tested as bytes): under the lock,
    // makes a scratch packed file (operator new(0x1b8), Construct), AddRefs it, and if its Init (slot 4) and
    // SetPath (slot 9) accept `path` and its Open (subobject slot 5) accepts (1, !(a3 && a4)), calls this object's
    // slot 35 with (the scratch file's subobject, a2, a3, a4) and then the scratch file's Close (subobject slot 9);
    // the result is that call's return (0xffffffff when any step before it failed). When Init succeeded its
    // Shutdown (slot 5) is called whatever happened after; the scratch file is Released last.
    uint32_t CopyToFile(void *path, uint32_t a2, uint32_t a3, uint32_t a4);
    // Slot 28, 0x9af7d0 (three stack arguments: key pointer, destination, size pointer): reads a record's bytes.
    // Under the lock, after EnsureLoaded: 0xffffffff when the file is not readable (state bit 0 clear). When writable
    // (state bit 1) and the key is in the pending tree at +0x110, the pending record's subobject slot 5 gives flags
    // and bit 1 of them means "still being written": *size = 0 and 0xffffffff. Then the store lookup (slot 11, copy of
    // the key, +0x1a8, +0x1a9); none: 0xffffffff, *size untouched. A null destination: *size = 0 and the result is
    // record[2] & 0x7fffffff. Otherwise n = min(record[1], *size); seeks the file (slot 12) to record[0] + the
    // current +0xf4 and, when that is where it landed (+0xf4 read again), reads n bytes (slot 14, destination, n);
    // when the read answered nonzero the result and *size are n, else 0.
    uint32_t ReadRecordData(const uint32_t *key, void *destination, uint32_t *size);
    // 0x9ad750 (no stack arguments): makes sure the record store at +0x30 is the current kind. Nothing happens when a
    // store exists and its slot 1 answers nonzero. Otherwise a new cGZDBPFIndexModifiable is made (operator new(0x24) and the
    // verified constructor); if there was an old store, it is asked for its entry count (slot 3, no arguments), a
    // zeroed vector of that many 28-byte entries is made, and (when it is not empty) the old store fills it (slot
    // 19: entries, +0x18) and the new store takes it (slot 18: entries, count, +0x18); the old store is then deleted
    // (slot 0 with 1) and +0x30 points at the new store.
    void EnsureStore();
    // 0x9ae5c0 (no arguments): the record index and the free ranges from the segment cache (+0x14) instead of the file.
    // 0 without a cache or when its OpenRecord (slot 22: key {0x86d4a89, 0, Crc32(path, lower), 0}, mode 1) fails.
    // Otherwise the record's slot 8 (Read: buffer, size) gives in turn: the version (must be 3); the stamp (must be the
    // subobject's GetSegmentChangeID, asked before it is read); +0xf4 = 0, then the 0x60-byte header into +0x118, which
    // CheckExtents must accept; +0x178 = 0, the old index deleted (slot 0 with 1) and +0x30 nulled; header.indexCount
    // zeroed 0x1c-byte entries (one read of them all). 0x40 entries or more: a cGZDBPFIndexCompressed takes them
    // (slot 18: entries, count, +0x18); when that refuses, and below 0x40, the index (if any) is deleted and a
    // cGZDBPFIndexStatic takes them (its answer ignored). Then RebuildRefs, +0x34 = 0, the entries freed, the index's
    // slot 16 (0, the file's Length), the trees at +0x184 and +0x18c emptied; the count of free ranges, then that many
    // (start, length) pairs (one read); the first header.holeCount of them (ORIGINAL-BUG: not limited to the count read)
    // go into the tree at +0x184 (an existing start keeps its length), +0x19d = 0. 1 when header.holeOffset is 0 or in
    // [0x60, Length()) and header.holeCount == header.holeSize >> 3: then +0x100 = the stamp. Any failed step stops with 0.
    // The record is closed (slot 6) once it was opened, before the result is decided, and released (slot 2) at the end.
    __declspec(noinline) uint8_t LoadCachedIndex();
    // 0x9b0670 (no arguments, no result): the reverse of LoadCachedIndex, into the same cache record. Nothing without a
    // cache (+0x14). Its OpenRecord (slot 22, the same key, mode 2) failing: only the record (when it was set) released.
    // Otherwise the record's slot 11 (SetSize 0), then its slot 9 (Write: data, size) with the version (3), the
    // subobject's GetSegmentChangeID, the header (+0x118, 0x60 bytes), when header.indexCount is nonzero that many zeroed
    // 0x1c-byte entries filled by the index's CopyEntries (slot 19: entries, +0x18), then the count of the free-range tree
    // at +0x184 (its size field) and its (start, length) pairs in key order (one write each). All written: Close (slot 6);
    // a failed write stops the rest and Aborts (slot 7). The record is released (slot 2) at the end.
    __declspec(noinline) void SaveCachedIndex();
    // The readers Open uses when the cache has nothing (this = the primary, no arguments, a byte answer).
    // 0x9af2a0: the header from the file. The file object (+0x58) seeks to mBaseOffset (slot 12; another position
    // answers 0) and reads 0x60 bytes into +0x118 (slot 14; failing answers 0). Files of version 0.x and 1.0 get
    // indexMinorVersion 1, versions 0.x, 1.0 and 1.1 groupInfo 0. CheckExtents passing answers 1; otherwise the header
    // is searched for (FindHeader): none (-1) sets mBaseOffset 0 and answers 0, one found becomes
    // mBaseOffset and the header is read once more from there (a recursion guard; the nested answer is the answer).
    __declspec(noinline) uint8_t ReadHeader();
    // 0x9aadf0: where a header embedded in a larger file starts (-1: none found). Opens the file (path +0x38; Open 1,
    // 2, 1) when it is not open, and closes it again afterwards (not when the open failed). Files of more than 16
    // bytes are read from the start in 0x200-byte chunks (ReadWithCount), each searched for the 16-byte marker that
    // precedes a header; a marker at chunk index i makes (chunk position + 16 + i) the answer, and when the 4 bytes
    // there read "DBPF" the search ends. Otherwise the file goes back where it was and the search goes on (the answer
    // stays the last marker's); every chunk after the first starts 16 bytes before the previous one ended, and a
    // short chunk or a failed read ends the search.
    __declspec(noinline) uint32_t FindHeader();
    // 0x9af590: the index from the file. Words per entry from GetIndexMinorVersion (own slot 48: 1 -> 5, 2 -> 6,
    // else 0). No entries: a cGZDBPFIndexModifiable when there is no index yet, answer 1. Otherwise a seek to
    // mBaseOffset + indexOffset and a read of count * words words into a zeroed buffer (either failing answers 0, the
    // index untouched); the old index deleted; >= 0x40 entries a cGZDBPFIndexCompressed filled by slot 20
    // (SetEntriesFromIndex: words, count, +0x18, the minor version), else, or when it refuses, a cGZDBPFIndexStatic the
    // same way. RebuildRefs, AllEntriesWithin(0, file length), +0x34 = 0, answer 1.
    __declspec(noinline) uint8_t ReadIndex();
    // 0x9af390: the free ranges from the file. None: answer 1. A seek to mBaseOffset + holeOffset and a read of
    // holeCount (start, length) pairs (either failing answers 0); both range trees (+0x184, +0x18c) emptied, the
    // pairs inserted into +0x184 (a repeated start keeps its first length). Then as LoadCachedIndex's final check
    // (holeOffset 0 or in [0x60, length), holeCount == holeSize / 8), or +0x2a set: +0x19d = 0, answer 1; else 0.
    __declspec(noinline) uint8_t ReadHoles();
    // 0x9aea00: the directory of compressed records (key e86b1eef e86b1eef 286b1f03 0) through the subobject's
    // DoReadRecord (slot 42): asked for the size first (no record, -1: answer 1), then read into a GeneralAllocator
    // (0x10cb098) block of size + 4 (failing answers 0). Each entry (type, group, instance, resource when
    // indexMinorVersion is 2, then the full size) maps its key through the namespace (+0x18, slot 4) when there is one
    // and sets the index's record size (slot 15). The block freed; answer 1.
    __declspec(noinline) uint8_t ReadCompressionDirectory();
    // The first two of Flush's commit steps (this = the primary, no arguments, a byte answer).
    // 0x9b1d50: the index into the file. Nothing to do (answer 1) unless the file is writable and the index changed
    // (+0x34). Sets +0x178. The new size is the index's count (slot 3) times the words per entry (5 or 6 by
    // GetIndexMinorVersion, else 0) times 4. Size 0: the old extent (indexOffset, indexSize) goes to AddFreeRangeTo
    // (tree selector 0), the three index fields are zeroed and its answer is the answer. A different size (or +0x17c
    // bit 4) gives the old extent back to the free ranges (+0x18c with bit 4, else +0x184; +0x19d set) when it has an
    // offset and a size, zeroes the fields and finds new room (AllocateSpaceUnlocked into indexOffset; failing zeroes
    // indexOffset and indexSize and answers 0). Then the count and size are stored, the index writes its entries
    // (slot 21: a vector, +0x18, the minor version) and they go to the file (own slot 58, WriteAt). A failed write
    // gives a stored extent back to +0x184 (+0x19d set) and zeroes the three fields; the write's answer is the answer.
    __declspec(noinline) uint8_t WriteIndex();
    // 0x9b1a70: the free ranges into the file. +0x19c = 0; nothing to do (answer 1) unless the file is writable and the
    // free ranges changed (+0x19d). Sets +0x178. A last free range reaching the end of the file is cut off (the file's
    // SetLength to its start) and erased. When the count of ranges in +0x184 and +0x18c times 8 differs from holeSize
    // (or +0x17c bit 4): the old extent goes to AddFreeRangeTo (selector 0; its answer is kept, and when it is 0 nothing
    // more is allocated) and the hole fields are zeroed, then, when there are ranges, holeSize = their count * 8 and
    // AllocateSpaceUnlocked finds room (its answer kept). Then +0x19c = 1, +0x194 becomes a copy of +0x184 with the
    // ranges of +0x18c added (AddRange). With a nonzero answer and ranges in +0x194, holeCount/holeSize are set and the
    // (start, length) pairs written at holeOffset (own slot 58); that write's answer is the answer. Otherwise the three
    // hole fields are zeroed and the kept answer is the answer.
    __declspec(noinline) uint8_t WriteHoles();
    // 0x9b1fe0 (two stack arguments: the record, the key; ret 8): commits a record written through DoCreateNewRecord /
    // OpenRecordForWrite into the file. 0 unless the file is writable. EnsureStore; the key's old entry is removed
    // (index slot 14 with a copy of the key and +0x1a8, +0x1a9): its references dropped (AdjustRefs, decrement), its
    // extent given back (AddFreeRangeTo, selector 1 when the stored size had no top bit), +0x34 set. The record's size
    // (serial record slot 52) 0: an empty entry {0, 0, 0} is added (slot 13), the references counted up, +0x34, 1.
    // Otherwise, when the data passes the filter, it is read whole (slot 55 SeekAbsolute 0, slot 27) into a
    // GeneralAllocator block of size + 2 (two zero bytes after it) and TryCompress'ed; the compressed bytes get room (own
    // slot 44) and are written (own slot 58) and entered as {offset, compressed size, size & 0x7fffffff} (references
    // counted up, +0x34): answer 1. A failed write gives its room back to +0x184 (+0x19d). Otherwise (or without
    // compression) the plain size gets room (own slot 44; failing answers 0); a memory record (+0x44 == 3) copies itself
    // there (record slot 64: offset, size), and when that works the entry {offset, size, size & 0x7fffffff} is added, the
    // references counted *down* (as the original does) and +0x34 set, its answer the answer; else the room goes back to
    // +0x184 (+0x19d) and the answer is 0.
    __declspec(noinline) uint8_t StoreWrittenRecord(void *record, const uint32_t *key);
    // Slot 29, 0x9b23b0 (three stack arguments: key pointer, data, size): stores a record's bytes in the file. Under
    // the lock, after EnsureLoaded; false unless the file is writable (state bits 0..1 and bit 1). False as well when
    // the key is in the pending tree (+0x110). Otherwise EnsureStore, then the store's slot 14 removes an existing
    // record for the key (called with a copy of the key, a 3-word out record and +0x1a8, +0x1a9): when that answers
    // true the counts are lowered (AdjustRefs, key[0], key[1], false), the removed space is given back
    // (AddFreeRangeTo(out[0], out[1], !(out[2] >> 31))) and +0x34 is set. Then this object's slot 44
    // (AllocateSpace, &offset starting at 0, size) finds room; if it fails, false. The record {offset, size,
    // size & 0x7fffffff} is written with slot 58 (WriteAt: data, offset, size); on success the store's slot 13 takes
    // (key copy, record), the counts are raised (AdjustRefs true), +0x34 is set and the result is true. When the
    // write fails and offset and size are both nonzero the space goes back into the tree at +0x184 (AddRange) with
    // +0x19d set; false.
    bool WriteRecordData(const uint32_t *key, const void *data, uint32_t size);
    // Slot 51, 0x9ab980 (one stack argument): fills `out`. Takes the subobject's lock (sub slot 13) and releases it
    // (sub slot 14). Writes +0x0c = 0, +0x10 = 0x60 and copies +0x140, +0x144, +0x14c, +0x150 into +0x14..+0x20.
    // The DIR record's key {0xe86b1eef, 0xe86b1eef, 0x286b1f03, 0}: when the subobject's slot 35 says it exists,
    // +0x24 = the primary's slot 30 (GetRecordOffset) with that key and +0x28 = the subobject's slot 36 (DoGetRecordSize);
    // otherwise both are 0. +0x44 = 0, then each range of the tree at +0x184 is appended to the vector at +0x2c and
    // its length added to +0x44; the same for +0x18c, +0x38 and +0x48. +0x00 = the file's slot 7 (length); +0x04 and
    // +0x08 = 0, or when a store exists its slot 4 and slot 5 minus +0x28.
    void GetInfo(PackedFileInfo *out);
    // Slot 49, 0x1d3ad0: +0x1a8 = low byte of value.
    void SetMatchFlagA(uint8_t value);
    // Slot 50, 0x1d3ab0: +0x1a9 = low byte of value.
    void SetMatchFlagB(uint8_t value);
    // Slot 20, 0x9ac940 (two stack arguments: owner, out). Under the lock, allocates a 16-byte object with
    // the game's cGZDBSerialRecordAsIGZFile vtables: +0 primary, +4 secondary, +8 zero, +0xc = owner - 4
    // (or 0 when owner is 0 or 4). AddRefs the owner - 4 when it is nonzero, sets *out = object + 4,
    // AddRefs *out through its vtable slot 1, unlocks, returns true. operator new throws on failure, so
    // the original's null branch (which would crash) is not reproduced.
    bool CreateRecordFile(void *owner, void **out);

    // 0x9a80c0 (Close; one stack argument, `flush`; sub slot 9 at 0x9a8080 passes 1). The result is the raw byte the original
    // leaves in al. Nothing happens to the file when its state bits 0..1 are clear (the result is 1). Otherwise, when
    // writable (bit 1): FlushOpenRecords; with `flush` set and that true, "needs commit" becomes (+0x34, +0x19d or +0x178 set) and
    // the result is sub slot 10 (Flush); then the file object's slot 5 closes the file and the state is zeroed, and when
    // "needs commit" the 0x9b0670 helper runs (original code). Read-only: the file is closed and the result is 1. Then, always: the
    // record store (+0x30) is deleted through its slot 0 (1) and nulled, the two reference maps (+0x1a0, +0x1a4) are deleted and
    // nulled, and the free-range trees at +0x184 and +0x18c are emptied (head node kept).
    uint8_t Close(uint32_t flush);
    // Slot 23, 0x9b0620 (reopen when anything is pending): true (1) when slot 24 (IsDirty) says no. Otherwise, when
    // bit 4 of +0x17c is set, Close(0) and, if that answered nonzero, the subobject's Open (slot 5) with the old state
    // bits 0 and 1 as the two byte arguments (its result); otherwise 0.
    uint8_t ReopenIfDirty();
    // Slot 36, 0x9a8370: under the lock, EnsureLoaded; when +0x19e is clear and the file is open, +0x19e = 1 around the
    // CompactBody (0x9a83d0, below; its result is returned), else 0. The lock is released before returning.
    uint8_t Compact();
    // 0x9a83d0: the work of Compact. The pending free ranges (+0x18c) are merged into the free ranges (+0x184) and emptied.
    // With no free range left, a header hole (holeOffset, holeSize) goes into the tree the +0x17c bit 4 chooses (+0x184, or
    // +0x18c with it), the hole fields are zeroed and the header is marked; 1 when no free range is left either way.
    // Otherwise 0 unless the file is writable with no open records. An empty index just empties both trees, zeroes the
    // index and hole fields, marks the header, flushes and cuts the file to the header (0x60 bytes): 1. A non-empty index
    // is rewritten into a temporary segment next to the file (names from 0x1c23bf with the prefix "TempDBSegmentPackedFile_",
    // original code): all records are copied, the temporary file is closed, and when every step answers, the file is moved
    // aside, the temporary file put in its place and this segment reopened (the segment cache is told to forget the path's
    // entry; a failed step puts things back). The result is the reopen's answer; 0 on any other path. Both temporary
    // files are deleted before returning.
    uint8_t CompactBody();
    // Slot 35, 0x9a8fe0 (four stack arguments: other file, a filter, two bytes; returns how many records were copied): under both
    // locks, asks slot 19 of `other` (when the first byte is set) or the subobject's slot 45 (otherwise) for the entry list, then
    // copies each entry with the 0x9a8be0 helper (original code) and counts the successes. The list block goes back through
    // FreeThroughAllocatorService.
    uint32_t CopyRecordsTo(void *other, uint32_t filter, uint32_t byteA, uint32_t byteB);
    // 0x9a8be0 (four stack arguments: the other segment, the key, a direction byte, a keep byte; ret 0x10): copies one
    // record between this file and `other` under the lock (+0x0c). The direction byte set: from `other` into this file;
    // clear: from this file into `other`. Nothing unless the source has the record (slot 20). When `other` answers
    // QueryInterface for the packed-file interface (0x286d4a6a) and the source holds the record compressed (slot 41),
    // the stored bytes move as they are: the stored size (slot 27; -1 skips this), the full size (the source's
    // GetRecordSize, or LookupRecordEnd when the source is this file), ReadRecordData (slot 28, -1 failing) into a
    // GeneralAllocator block, WriteRecordData (slot 29) into the destination, then its SetRecordFullSize(key, 1, full)
    // (slot 42); the packed pointer is released either way. Otherwise (or when that did not finish) the plain bytes:
    // GetRecordSize (segment slot 21; -1 gives up), ReadRecord (27) and WriteRecord (28). A copy that worked deletes the
    // source's record (segment slot 26) unless the keep byte is set, and answers 1.
    // The two bytes arrive as stack words (CopyRecordsTo passes its own arguments on); only their low bytes count.
    __declspec(noinline) uint8_t CopyOneRecord(void *other, const uint32_t *key, uint32_t intoThis, uint32_t keep);

    // 0x9a9260 (five stack arguments: file offset, stored size, expected size, buffer pointer, length pointer): false for offset 0.
    // Reads `stored size` bytes at the offset through slot 56 (ReadAt; a stack buffer up to 0xc00 bytes, otherwise a block
    // from the global allocator) and, when that worked, hands them to PackedFileCompression::Decompress with the remaining
    // arguments. The block is freed again.
    bool ReadAndDecompress(uint32_t offset, uint32_t storedSize, uint32_t expected, void **buffer, uint32_t *length);
    // Slot 53, 0x9a9d80 (two stack arguments: key, out): opens a record for writing. EnsureLoaded; false unless writable and
    // the key is not pending. With no stored record: the subobject's slot 38 (DoCreateNewRecord). A compressed record is decompressed
    // (ReadAndDecompress) and copied into a new 0x64-byte memory-backed record object built here after the base constructor 0x99b7c0
    // (cGZPersistDBSerialRecord::Construct): kind 3, the game's cGZDBWriteRecordPackedFile vtables, a std::vector<uint8_t> at +0x4c holding the bytes; a plain
    // record gets the object of constructor 0x9a70f0 (offset, stored size, key, file, 1; original code). The object is AddRef'd, *out =
    // object + 4, AddRef'd again and entered in the pending tree. True.
    bool OpenRecordForWrite(const uint32_t *key, void **out);
    // Slot 54, 0x9aa2f0 (two stack arguments: key, path): writes a record's bytes to a file. False (nothing touched) when
    // the record has no size (sub slot 36 answers 0xffffffff). Otherwise the target's directory is made when missing, the file is
    // opened (cRZFile::Open 2, 3, 1), the record opened (sub slot 22, mode 1; its handle is asked for interface 0xc56b8f08), the file
    // truncated and filled 0x400 bytes at a time from the stream's slot 27; then everything is closed and released (sub slot 24
    // for the record). When anything failed the file is deleted (0x1c2e25, original code). The result is the last step's byte.
    uint8_t ExportRecord(const uint32_t *key, cIGZString *path);
    // Slot 21, 0x9aa580 (two stack arguments: key, path): stores a file as a record. Under the lock, EnsureLoaded; nothing unless
    // the file is writable and the key not pending. An old record with the key is removed (like WriteRecordData). The file is opened
    // for reading; with a nonzero length that passes the filter it is read whole and TryCompress'ed, and when that works the
    // space (slot 44) and the compressed bytes (slot 58) are entered as {offset, compressed size, length}. Otherwise the file is
    // stored as it is in chunks of 0x4000 bytes (slot 58 at consecutive offsets), a failed copy giving the space back.
    uint8_t ImportFile(const uint32_t *key, cIGZString *path);
    // 0x9b1950 (WriteHeader; no arguments): false unless the file is writable and its file object's slot 4 says yes. True
    // (without writing) while +0x178 is clear. Otherwise seeks (slot 12) to +0xf4 and, when that lands there, writes the
    // 0x60-byte header (slot 16 of the file object: buffer, 0x60): a copy of +0x118..+0x177 with word 0 = the game's
    // global at 0x10f5030, word 1 = 1, word 2 = 2, word 4 = +0x108, and words 6 and 7 (+0x130, +0x134) zeroed; the result is
    // whether the write answered nonzero. The original also calls _time64(0) in the middle and drops the result; that call
    // has no effect and is not made here.
    uint8_t WriteHeader();
    // 0x9b17e0 (InitHeader; no arguments): resets the header fields (+0x118..+0x158 to the values of an empty file,
    // +0x15c..+0x174 zeroed, +0x178 = 1, +0x118 = the global, +0x124 = +0x104, +0x128 = +0x108), then WriteHeader; when that
    // answered nonzero the file object's slot 8 is called with 0x60 (SetLength). The result is what that call left in al,
    // or 0 when the header was not written.
    uint8_t InitHeader();

    // Constructor, 0x9a6ed0 (returns this): intermediate vtable 0xe5f45c, the base constructor on this + 4,
    // the two game vtables, fields zeroed or set as in the field map (+0x2b = 1, +0x10c = 0x200, +0x17c =
    // 0x13, +0x180 = 0.15f, +0x1aa = 1), the cRZString at +0x38 empty (capacity 15), the cRZFile at +0x58
    // constructed, and four tree head nodes (RZMalloc: 0x24 bytes at +0x110, 0x18 bytes at +0x184, +0x18c
    // and +0x194; left = parent = right = itself, colour/nil word 0x101).
    cGZDBSegmentPackedFile *Construct();
    // 0x9b05c0 (called by most slots): 1 when the file is open (state bits 0..1) or neither auto-open byte is
    // set; otherwise tries the subobject's slot 5 (Open) with (+0x28, +0x29): 0 when that succeeds, else, if
    // both bytes (re-read) are nonzero, the result of Open(+0x28, 0). The result is the raw byte the Open
    // call leaves in al, so it is not normalised.
    uint8_t EnsureLoaded();
    // Slot 5, 0x9b0530 (sub slot 4 thunks here): if +0x1c is zero, true. Otherwise clears it, calls sub slot 9
    // (Close) when the open bits are set, destroys and deletes the two unordered_set<unsigned> objects at +0x1ac
    // and +0x1b0 (zeroing the pointers), and returns the base class's Shutdown (0x99de70) of the subobject.
    bool Shutdown();
    // Destructor with delete, 0x9a7a00 (sub slot 33 thunks here; one stack argument, the flags): writes the
    // two game vtables, Shutdown, erases the nodes of the trees at +0x194, +0x18c, +0x184 and +0x110 (in that
    // order) and frees their head nodes, destroys the cRZFile at +0x58 and the cRZString at +0x38, runs the
    // base destructor body on this + 4, and when flags bit 0 is set frees 0x1b8 bytes. Returns this.
    cGZDBSegmentPackedFile *DeletingDestructor(uint32_t flags);
    // Slot 1, 0x9a7d10 (shared with one other class): adds one to the count at +8 (the base class's AddRef on this + 4).
    uint32_t AddRef();
    // Slot 3, 0x1c04e0 (shared): this + 4 (the subobject). A null `this` returning 0 is not reproduced.
    void *AsPersistDBSegment();
    // Slot 4, 0x9ad210 (shared): sets +0x1c to 1 if it was 0; returns true.
    bool Init();
    // Slot 6, 0x254bc0 (shared): +0xf4.
    uint32_t GetBaseOffset();
    // Slot 13, 0x9ac770: under the lock, EnsureLoaded; when open, *a = +0x130, *b = +0x134 and true.
    bool GetDates(uint32_t *a, uint32_t *b);
    // Slot 18, 0x9afcc0 (shared): +0x2b = low byte of value.
    void SetCreateIfMissing(uint8_t value);
    // Slot 19, 0x9ad6f0: under the lock, whether the key (four dwords) is in the tree at +0x110.
    bool IsRecordOpen(const uint32_t *key);
    // Slot 22, 0x238ec0 (shared): the address of the file object at +0x58.
    void *GetFile();
    // Slot 25, 0x5b1920 (shared): +0x158.
    uint32_t GetGroupInfo();
    // Slot 27, 0x9acac0: like GetCompressedStoredSize's lookup (store slot 11) but returns record[1] unconditionally;
    // 0xffffffff when closed or not found.
    uint32_t GetRecordStoredSize(const uint32_t *record);
    // Slot 30, 0x9aca40: the same lookup, returning record[0].
    uint32_t GetRecordOffset(const uint32_t *record);
    // Slot 46, 0x1474a0 (shared; three stack arguments): returns false.
    bool Reserved46(uint32_t a, uint32_t b, uint32_t c);
    // Slots 55..58 act on the cRZFile at +0x58 under the lock after EnsureLoaded, when open (state bits).
    // Slot 55, 0x9afa50: file slot 12 (seek) to +0xf4 + offset; true when it lands there.
    bool SeekTo(uint32_t offset);
    // Slot 56, 0x9af1a0 (three stack arguments): seek to +0xf4 + offset, and if that landed, file slot 14
    // (read) with (dst, length); true when the read answered nonzero.
    bool ReadAt(void *dst, uint32_t offset, uint32_t length);
    // Slot 57, 0x9af220: the same with file slot 15 (read with count).
    bool ReadCountedAt(void *dst, uint32_t offset, uint32_t *length);
    // Slot 58, 0x9b18d0: the same with file slot 16 (write) and only when state bit 1 (writable) is set.
    bool WriteAt(const void *src, uint32_t offset, uint32_t length);

    // Layout (lane CP structure pass). Embedded objects are typed; fields keep the original's widths.
    const void *mVTable;               // +0x00: kVTable 0xe5fc58 (cIGZDBSegmentPackedFile, 59 slots)
    cGZPersistDBSegment mSegment;      // +0x04: base subobject; its vtable is kSubVTable 0xe5fd48 (cIGZPersistDBSegment,
                                       //        50 slots), refcount +0x08, lock +0x0c (cRZCriticalSection), segment cache
                                       //        +0x14, namespace +0x18
    uint8_t mInitialized;              // +0x1c: set by Init (slot 4), cleared by Shutdown
    uint8_t mPad1D[3];                 // +0x1d
    uint32_t mSegmentID;               // +0x20
    uint8_t mOpenMode;                 // +0x24: bit 0 open for reading, bit 1 open for writing
    uint8_t mOpenModePad[3];           // +0x25: zeroed with the mode by the constructor
    uint8_t mAutoOpenRead;             // +0x28: stored by SegmentView::AutoOpen
    uint8_t mAutoOpenWrite;            // +0x29
    uint8_t mFlag2A;                   // +0x2a: slot 17 stores it; meaning not determined
    uint8_t mCreateIfMissing;          // +0x2b: 1 after construction; slot 18 stores it; Open creates the file when set
    uint32_t mField2C;                 // +0x2c: zeroed by the constructor; no reader found
    ifaces::cIGZDBPFIndex *mIndex;     // +0x30: the record index (cGZDBPFIndexModifiable 0xe5ff40, Static 0xe5fedc or
                                       //        Compressed 0xe5fe78; built by EnsureStore / LoadIndex)
    uint8_t mIndexDirty;               // +0x34: the index changed since the last flush
    uint8_t mPad35[3];                 // +0x35
    cRZString mPath;                   // +0x38: the file path (cIGZString face; vtable 0xd9b470)
    cRZFile mFile;                     // +0x58: the file (cIGZFile face; vtable 0xda42a0)
    uint32_t mBaseOffset;              // +0xf4: offset of the package inside the file (slots 6/7)
    uint32_t mReadBufferSize;          // +0xf8: slot 14 (SetBuffering) stores it and passes it to cIGZFile slot 32
    uint32_t mWriteBufferSize;         // +0xfc
    uint32_t mFileStamp;               // +0x100
    uint32_t mUserVersionMajor;        // +0x104: slots 11/12; written into the header by Flush
    uint32_t mUserVersionMinor;        // +0x108
    uint32_t mSmallRecordLimit;        // +0x10c: 0x200 after construction
    KeyTree mOpenRecords;              // +0x110: open cGZDBRecordPackedFile records by key (0x24-byte nodes, 16-byte
                                       //         key at +0x10, record at +0x20); head +0x110, size +0x114
    DBPFHeader mHeader;                // +0x118: copy of the 0x60-byte DBPF header
    uint8_t mHeaderDirty;              // +0x178
    uint8_t mPad179[3];                // +0x179
    uint32_t mOptions;                 // +0x17c: 0x13 after construction (slot 15); bit 0 = flush keeps open records
    float mCompactRatio;               // +0x180: slot 16
    RangeTree mFreeRanges;             // +0x184: free ranges of the file (start -> length)
    RangeTree mPendingFreeRanges;      // +0x18c: ranges freed by committed records, merged on flush
    RangeTree mSpareFreeRanges;        // +0x194: swapped with mFreeRanges on flush when mSwapRangesOnFlush
    uint8_t mSwapRangesOnFlush;        // +0x19c: WriteHoles (0x9b1a70) clears it, then sets it once +0x194 is filled
    uint8_t mFreeRangesDirty;          // +0x19d
    uint8_t mCompacting;               // +0x19e
    uint8_t mTrackRefs;                // +0x19f: slot 33 (SetRefTracking)
    RefMap *mTypeRefs;                 // +0x1a0: type -> record count (null unless mTrackRefs)
    RefMap *mGroupRefs;                // +0x1a4: group -> record count
    uint8_t mMatchFlagA;               // +0x1a8: first flag passed to cIGZDBPFIndex::FindRecord (slot 49)
    uint8_t mMatchFlagB;               // +0x1a9: second flag (slot 50)
    uint8_t mCompressAll;              // +0x1aa: 1 after construction (slot 38)
    uint8_t mPad1AB;                   // +0x1ab
    IdSet *mCompressedTypes;           // +0x1ac: types stored compressed (slot 39); null when none
    IdSet *mCompressedGroups;          // +0x1b0: groups stored compressed (slot 40)
    uint8_t mPad1B4[4];                // +0x1b4
};

static_assert(sizeof(cGZDBSegmentPackedFile) == 0x1b8, "cGZDBSegmentPackedFile: 0x1b8 bytes");
static_assert(offsetof(cGZDBSegmentPackedFile, mVTable) == 0, "cGZDBSegmentPackedFile: vtable at +0");
static_assert(offsetof(cGZDBSegmentPackedFile, mSegment) == 4, "cGZDBSegmentPackedFile: subobject at +4");
static_assert(offsetof(cGZDBSegmentPackedFile, mSegment.mRefCount) == 8, "cGZDBSegmentPackedFile: refcount at +8");
static_assert(offsetof(cGZDBSegmentPackedFile, mSegment.mLock) == 0x0c, "cGZDBSegmentPackedFile: lock at +0x0c");
static_assert(offsetof(cGZDBSegmentPackedFile, mSegment.mNamespace) == 0x18, "cGZDBSegmentPackedFile: +0x18");
static_assert(offsetof(cGZDBSegmentPackedFile, mInitialized) == 0x1c, "cGZDBSegmentPackedFile: +0x1c");
static_assert(offsetof(cGZDBSegmentPackedFile, mSegmentID) == 0x20, "cGZDBSegmentPackedFile: segment ID at +0x20");
static_assert(offsetof(cGZDBSegmentPackedFile, mOpenMode) == 0x24, "cGZDBSegmentPackedFile: open mode at +0x24");
static_assert(offsetof(cGZDBSegmentPackedFile, mAutoOpenRead) == 0x28, "cGZDBSegmentPackedFile: +0x28");
static_assert(offsetof(cGZDBSegmentPackedFile, mFlag2A) == 0x2a, "cGZDBSegmentPackedFile: +0x2a");
static_assert(offsetof(cGZDBSegmentPackedFile, mCreateIfMissing) == 0x2b, "cGZDBSegmentPackedFile: +0x2b");
static_assert(offsetof(cGZDBSegmentPackedFile, mField2C) == 0x2c, "cGZDBSegmentPackedFile: +0x2c");
static_assert(offsetof(cGZDBSegmentPackedFile, mIndex) == 0x30, "cGZDBSegmentPackedFile: index at +0x30");
static_assert(offsetof(cGZDBSegmentPackedFile, mIndexDirty) == 0x34, "cGZDBSegmentPackedFile: +0x34");
static_assert(offsetof(cGZDBSegmentPackedFile, mPath) == 0x38, "cGZDBSegmentPackedFile: path at +0x38");
static_assert(offsetof(cGZDBSegmentPackedFile, mPath.mString) == 0x3c, "cGZDBSegmentPackedFile: path string at +0x3c");
static_assert(offsetof(cGZDBSegmentPackedFile, mFile) == 0x58, "cGZDBSegmentPackedFile: file at +0x58");
static_assert(offsetof(cGZDBSegmentPackedFile, mBaseOffset) == 0xf4, "cGZDBSegmentPackedFile: +0xf4");
static_assert(offsetof(cGZDBSegmentPackedFile, mFileStamp) == 0x100, "cGZDBSegmentPackedFile: +0x100");
static_assert(offsetof(cGZDBSegmentPackedFile, mUserVersionMajor) == 0x104, "cGZDBSegmentPackedFile: +0x104");
static_assert(offsetof(cGZDBSegmentPackedFile, mSmallRecordLimit) == 0x10c, "cGZDBSegmentPackedFile: +0x10c");
static_assert(offsetof(cGZDBSegmentPackedFile, mOpenRecords) == 0x110, "cGZDBSegmentPackedFile: +0x110");
static_assert(offsetof(cGZDBSegmentPackedFile, mOpenRecords.size) == 0x114, "cGZDBSegmentPackedFile: +0x114");
static_assert(offsetof(cGZDBSegmentPackedFile, mHeader) == 0x118, "cGZDBSegmentPackedFile: header at +0x118");
static_assert(offsetof(cGZDBSegmentPackedFile, mHeader.indexMinorVersion) == 0x154, "cGZDBSegmentPackedFile: +0x154");
static_assert(offsetof(cGZDBSegmentPackedFile, mHeader.groupInfo) == 0x158, "cGZDBSegmentPackedFile: +0x158");
static_assert(offsetof(cGZDBSegmentPackedFile, mHeaderDirty) == 0x178, "cGZDBSegmentPackedFile: +0x178");
static_assert(offsetof(cGZDBSegmentPackedFile, mOptions) == 0x17c, "cGZDBSegmentPackedFile: +0x17c");
static_assert(offsetof(cGZDBSegmentPackedFile, mCompactRatio) == 0x180, "cGZDBSegmentPackedFile: +0x180");
static_assert(offsetof(cGZDBSegmentPackedFile, mFreeRanges) == 0x184, "cGZDBSegmentPackedFile: +0x184");
static_assert(offsetof(cGZDBSegmentPackedFile, mPendingFreeRanges) == 0x18c, "cGZDBSegmentPackedFile: +0x18c");
static_assert(offsetof(cGZDBSegmentPackedFile, mSpareFreeRanges) == 0x194, "cGZDBSegmentPackedFile: +0x194");
static_assert(offsetof(cGZDBSegmentPackedFile, mSwapRangesOnFlush) == 0x19c, "cGZDBSegmentPackedFile: +0x19c");
static_assert(offsetof(cGZDBSegmentPackedFile, mTrackRefs) == 0x19f, "cGZDBSegmentPackedFile: +0x19f");
static_assert(offsetof(cGZDBSegmentPackedFile, mTypeRefs) == 0x1a0, "cGZDBSegmentPackedFile: +0x1a0");
static_assert(offsetof(cGZDBSegmentPackedFile, mMatchFlagA) == 0x1a8, "cGZDBSegmentPackedFile: +0x1a8");
static_assert(offsetof(cGZDBSegmentPackedFile, mCompressAll) == 0x1aa, "cGZDBSegmentPackedFile: +0x1aa");
static_assert(offsetof(cGZDBSegmentPackedFile, mCompressedTypes) == 0x1ac, "cGZDBSegmentPackedFile: +0x1ac");
static_assert(offsetof(cGZDBSegmentPackedFile, mCompressedGroups) == 0x1b0, "cGZDBSegmentPackedFile: +0x1b0");

// The cGZPersistDBSegment subobject at +4 of cGZDBSegmentPackedFile. The original enters its slots with
// ecx = object + 4, so `this` here is the subobject. Offsets in the comments are relative to the
// subobject (primary offset = subobject offset + 4). The class has no data members: fields are reached
// through the pointer, as the original does. Use it as reinterpret_cast<SegmentView *>(object + 4).
class SegmentView {
public:
    // Sub slot 7, 0x9ad650: true when the state byte (sub +0x20) has bits 0..1 set.
    bool IsOpen() const;
    // Sub slot 8, 0x1d3e10: bit 1 of the dword at sub +0x20.
    bool IsWritable() const;
    // Sub slot 6, 0x9a8030 (two stack arguments, bytes used): sub +0x24 = state, sub +0x25 = flag. Returns true.
    bool AutoOpen(uint8_t state, uint8_t flag);
    // Sub slot 16, 0x9b0350: sub +0x1c = *id (primary +0x20, the segment ID). Returns true.
    bool SetSegmentID(const uint32_t *id);
    // Sub slot 47, 0x9a9850 (one stack argument and an out pointer): if sub slot 35 (this, arg) returns
    // true, *out = sub slot 46 (this) and returns true. Otherwise returns false and leaves *out alone.
    bool DoGetRecordChangeID(void *arg, uint32_t *out);
    // Sub slot 36, 0x9a98d0 (one stack argument, a record key): EnsureLoaded; 0xffffffff when the file is not open.
    // When writable, a pending record with that key (tree at primary +0x110) answers with its slot 52; any answer
    // other than 0xffffffff is the result. Otherwise, when the primary's slot 41 (IsRecordCompressed) says yes, the
    // result is LookupRecordEnd; if not, record[1] of the store's lookup (slot 11), or 0xffffffff when there is none.
    uint32_t DoGetRecordSize(const uint32_t *record);
    // Sub slot 41, 0x9a9760 (one stack argument, a record key; no lock): the removal half of the primary's slot 29.
    // EnsureLoaded; false unless the file is writable (state bit 1) and the key is not in the pending tree; then
    // EnsureStore and the store's slot 14 (key copy, 3-word out record, +0x1a8, +0x1a9); when that answers true the
    // counts are lowered (AdjustRefs false), the space is given back (AddFreeRangeTo(out[0], out[1], !(out[2] >> 31)))
    // and +0x34 is set; true. False when the store had no such record.
    bool DoDeleteRecord(const uint32_t *key);
    // Sub slot 40, 0x9a9420 (one stack argument, a record's sub-object pointer): false when the file is not open.
    // Looks up the key slot 3 of the handle gives in the pending-record tree and walks the nodes with that key to the
    // one whose record + 4 is the handle: calls the record's slot 62, releases it (slot 2), erases the node
    // (RZFree) and returns true. False when there is none.
    bool DoAbortRecord(void *handle);
    // Sub slot 5, 0x9ad830 (Open; two stack arguments, bytes: read, write): returns 0 (and does nothing) when the file is already
    // open, the path string (+0x3c) is empty, Init (+0x1c) has not run, or both arguments are zero. Otherwise the state becomes
    // 3 with write, else 1; the file object is given the path (slot 29) and opened (slot 3: 1 or 3, mode 2 or 3, 1); when that
    // fails the state is 0 and the result 0. An empty file that is writable gets InitHeader first. An index is read by
    // 0x9ae5c0; when there is none (or the file is read-only and empty) the four older readers (0x9af2a0, 0x9af590, 0x9af390,
    // 0x9aea00) are tried in turn (all must answer nonzero; +0x34 = 0), and for a writable file with +0x2b set a fresh file is
    // started instead (InitHeader, free-range trees at +0x184/+0x18c emptied, +0x19d = 1, new record store, +0x34 = 1). When
    // nothing worked the file object is closed and the state is 0. On success, if writable, the file is extended to cover
    // the store's length, the header extents and the last free range (file slot 8), and 0x9b0670 runs when the index was not
    // read or the file was extended. The result is the raw byte the original leaves in al.
    uint8_t Open(uint32_t read, uint32_t write);
    // Sub slot 10, 0x9ab4f0 (Flush; no arguments): the commit. Under the lock (sub slots 13 and 14): when the file is writable
    // and something is dirty (+0x34, +0x19d or +0x178), writes the directory record and runs the original helpers 0x9b1d50 and
    // 0x9b1a70, WriteHeader, the file's slots 18 and 19, swaps the free-range trees and clears the dirty flags. The result is
    // 1, or the byte of the first step that failed.
    uint8_t Flush();
    // Sub slot 39, 0x9a9520 (one stack argument, a record's sub-object pointer): DoAbortRecord, except that a record whose slot 5
    // (flags) has bit 1 set is first committed by the original helper 0x9b1fe0 (this = the primary; the record and the
    // key copy). False when the file is not open or no pending record has that handle.
    bool DoCloseRecord(void *handle);
    // Sub slot 46, 0x9a99f0 (no arguments): 0 while anything is dirty (+0x34, +0x19d or +0x178). Otherwise the cached +0x100 when
    // nonzero, else (length << 16) + the path's modification time (0x1c2068, original code), where the length comes from
    // the file object (slot 7) when it is open (slot 4) and from cRZFile::LengthOfPath on the path string (+0x38) otherwise.
    // The result is cached in +0x100.
    uint32_t DoGetSegmentChangeID();
    // Sub slot 42, 0x9aa0f0 (three stack arguments: key, destination, size pointer): reads a record's bytes. EnsureLoaded; 0xffffffff
    // (and *size = 0) unless the file is readable (state bit 0), when a pending record with the key is still being written (its
    // sub slot 5, bit 1) and when the store (slot 11, key copy, +0x1a8, +0x1a9) has no such record. A null destination: *size = 0 and the
    // result is the record's full size (word 2 & 0x7fffffff). A record stored compressed (full size above the stored size,
    // word 1) is looked up again and decompressed into the destination by ReadAndDecompress (capacity *size; the result
    // and *size are the decompressed length; 0 and *size = 0 on failure). Otherwise min(*size, stored size) bytes are read at
    // +0xf4 + offset (seek slot 12, which must land there; read slot 14): the result and *size are that count, or 0.
    uint32_t DoReadRecord(const uint32_t *key, void *destination, uint32_t *size);
    // Sub slot 43, 0x9aa940 (three stack arguments: key, data, size): stores a record. EnsureLoaded; false unless writable
    // and the key is not pending. Like WriteRecordData (slot 29, which locks): the store's slot 14 removes an old record (counts
    // lowered, space given back, +0x34), but the data is first passed through PassesFilter and TryCompress; the space is
    // allocated (slot 44) for the stored size, written (slot 58) and entered as {offset, stored size, full size & 0x7fffffff}
    // (store slot 13; counts raised; +0x34). A failed write gives the space back (tree +0x184, +0x19d). The compressed block is freed.
    bool DoWriteRecord(const uint32_t *key, const void *data, uint32_t size);
    // Sub slot 38, 0x9a9640 (two stack arguments: key, out): a new record for writing. EnsureLoaded; false unless the file is
    // writable, the store has no such record and no pending record has the key. Otherwise a 0x64-byte record object is made
    // (RZMalloc; its constructor 0x9a70f0 is original code, called with 0, 0, the key, the file and 1), AddRef'd for the caller
    // (*out = object + 4), AddRef'd again and entered in the pending tree. True.
    bool DoCreateNewRecord(const uint32_t *key, void **out);
    // Sub slot 37, 0x9a9a60 (three stack arguments: key, out, mode): opens a record. EnsureLoaded; mode bit 1 hands the work to the
    // primary's slot 53 (OpenRecordForWrite). Otherwise (mode bit 0): a pending record with the key that is not being written
    // gets a second object of its own kind (kind 2: 0x58 bytes through 0x9a6e60, kind 1: 0x1060 bytes through 0x9a6de0,
    // sharing its data and size; other kinds none); a stored record is read: compressed ones are decompressed into a counted
    // block (the count in its first dword), plain ones up to +0x10c bytes are read into one (slot 56), both given to a 0x58-byte
    // object; bigger plain records get a 0x1060-byte object that reads the file on demand. The object is AddRef'd (sub
    // slot 1), then again and entered in the pending tree. *out = the object's sub-object (or null); the result is whether there
    // is one. The read-record constructors 0x9a6e60 and 0x9a6de0 are ours (cGZDBReadRecord.cpp); the write record's 0x9a70f0 is original code.
    bool DoOpenRecord(const uint32_t *key, void **out, uint32_t mode);
    // Sub slot 9, 0x9a8080: Close(1) on the primary.
    uint8_t Close();
    // Sub slot 44, 0x9a9880 (one stack argument, a filter): with a filter, sub slot 45 (0, filter) and its result. Without
    // one: the store is loaded when absent (0x9ae5c0, original code; EnsureLoaded when that answers 0) and the result is the
    // store's slot 3, or 0 when there is no store.
    uint32_t DoGetRecordCount(uint32_t filter);
    // Sub slot 45, 0x9a99b0 (two stack arguments): the same loading, then the store's slot 10 gets the arguments as they came
    // (its result is returned); 0 without a store.
    uint32_t DoGetResourceKeyList(uint32_t a, uint32_t b);
    // Sub slot 35, 0x9aa4d0 (one stack argument, a record key): the same loading, then false without a store; true when the store's slot
    // 11 (copy of the key, +0x1a8, +0x1a9) finds the record, else whether the key is in the pending tree (+0x110).
    bool DoTestForRecord(const uint32_t *key);
    // The thunks of the subobject's vtable: each moves `this` back by 4 and runs the primary's function.
    bool QueryInterface(uint32_t riid, void **ppvObj);  // sub slot 0, 0x9adac7
    uint32_t AddRef();                                  // sub slot 1, 0x9a7d18 (shared; then 0x9a7d10 and 0x99ba50)
    uint32_t Release();                                 // sub slot 2, 0x9af988
    bool Init();                                        // sub slot 3, 0x1d3dbc (into 0x9ad210)
    void GetPath(cIGZString *out);                      // sub slot 11, 0x1d3b5e
    bool SetPath(void *path);                           // sub slot 12, 0x1d450d
    // Sub slot 15, 0x147d00 (shared): the dword at sub +0x1c (the segment ID, primary +0x20).
    uint32_t GetSegmentID() const;
    // Sub slot 34, 0x9aadc0: if the open bits (sub +0x20, bits 0..1) are set, calls sub slot 9 (Close); if the
    // byte at sub +0x18 (the primary's +0x1c, set by Init) is set, calls the primary's slot 5 (Shutdown); then
    // runs cGZPersistDBSegment::vf34 on the subobject (the deleting destructor through slot 33).
    void Delete();
    bool Shutdown();                                    // sub slot 4, 0x9b05a9 (thunk into 0x9b0530)
    cGZDBSegmentPackedFile *DeletingDestructor(uint32_t flags); // sub slot 33, 0x9a76f6 (thunk into 0x9a7a00)
};

// An unordered_map<unsigned, unsigned> (MSVC layout, 0x20 bytes) as embedded in the packed file's objects.
struct HashMap32 {
    uint8_t bytes[0x20];
    // 0x9a6ad0: the default constructor (empty, 8 buckets, load factor 1.0). Returns this.
    HashMap32 *Construct();
    // The result of Emplace: the node holding the key and whether this call inserted it. Only the first
    // byte of the flag is written; the other three bytes keep what was there.
    struct EmplaceResult {
        void *node;
        uint8_t inserted;
        uint8_t pad[3];
    };
    // 0x9a65b0 (this = the map; two stack arguments, popped): inserts the pair (key, value) unless the key is
    // present, as try_emplace: an existing key keeps its value. Returns out.
    EmplaceResult *Emplace(EmplaceResult *out, const uint32_t *pair);
};
static_assert(sizeof(HashMap32) == 0x20, "HashMap32 layout");

// The record compression helpers of the packed file (plain functions in the game). Memory comes from the global
// GeneralAllocator (RVA 0x10cb098) directly, as in the original; buffers handed out are freed with RZMemory::Free.
struct PackedFileCompression {
    // 0x9a8a00 (stdcall, ret 0x10): compresses `size` bytes at `data` into *outBuffer, sets *outSize to the compressed length.
    // *outBuffer null: a buffer of the compressor's maximum length is allocated (and freed again on failure); non-null: the
    // caller's, whose capacity is *outSize. Sizes of 40,000,000 and more are not compressed: the bytes are copied (a new
    // buffer of `size`, or the caller's when its capacity is at least `size`; *outSize = 0 and false when it is smaller).
    // Above 250,000 bytes the compressor runs in its quick mode. An exception from the allocation leaves *outBuffer null
    // and the answer false. Returns whether it worked.
    static bool __stdcall Compress(const void *data, uint32_t size, void **outBuffer, uint32_t *outSize);
    // 0x9b0470 (stdcall, ret 0x10): Compress for record data of 50 to 16,000,000 bytes, keeping the result only when
    // outSize / size is below the game's float at 0xdb13e8. *outBuffer and *outSize are zeroed first; false (and nothing
    // allocated) otherwise.
    static bool __stdcall TryCompress(const void *data, uint32_t size, void **outBuffer, uint32_t *outSize);
    // 0x9a9100 (stdcall, ret 0x14): decompresses `size` bytes at `source` (checked by the compressor's header test 0x9cc320);
    // *buffer non-null: the caller's buffer, which must be for exactly `expected` bytes; null: one is allocated for
    // the decompressed length. *length holds the buffer's capacity and receives the decompressed length. Returns whether it worked;
    // on failure *length is 0 and a buffer allocated here is freed again.
    static bool __stdcall Decompress(const void *source, uint32_t size, uint32_t expected, void **buffer, uint32_t *length);
};

// The packed-file statics (owned.h): the image's objects in the hooked game, standalone/owned_statics.cpp's otherwise.
TS2_OWNED_STATIC(uint32_t, 10f5030);    // the "FPBD" magic of the file header
TS2_OWNED_STATIC(const float, db13e8);  // the ratio below which compressed record data is kept
TS2_OWNED_STATIC(const uint32_t, e5f440); // the four bytes "BUSY" (0x59535542) the header gets at offset 0 while the file is being changed

// The vtables (owned.h, OwnedVTable): the image's in the hooked game, our faces' (rzface, cGZDBSegmentPackedFileFaces.cpp)
// in an image-free program.
TS2_OWNED_STATIC(const OwnedVTable, e5fc58);
TS2_OWNED_STATIC(const OwnedVTable, e5fd48);
TS2_OWNED_STATIC(const OwnedVTable, e5f45c);
