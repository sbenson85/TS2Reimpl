#pragma once
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZString.h"

#include <new>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

struct ServiceRefVector; // simantics/ServiceRefVector.h

// nTSCASCore::cTSCASBodyPartSystem: Create-a-Sim's body-part system (RTTI name; service id 0xeb7d6513). The object
// derives from cRZSystemService (vftable 0xeba13c at +0, vftable 0xeba17c at +4) and from the interface
// nTSCASCore::cITSCASBodyPartSystem, whose subobject sits at +0x1c with vftable 0xeba198 (89 slots, slot N at byte
// offset 4N). Nearly every method lives on that subobject (the game's names vf28_N are the slot numbers there), and the
// game calls them with `this` pointing AT the subobject, so they are members of BodyPartSystemIface below, whose offsets
// are relative to the subobject (system offset = iface offset + 0x1c).
//
// What the iface fields are, as far as these functions show:
//   +0x18  pointer to the "redo" stack, +0x1c pointer to the "modifier stack": each a heap {first,last,end} triple of
//          0x40-byte BodyPartEntry records (the stack grows at the back; the newest record is last).
//   +0x78/+0x7c  begin/end of a vector of dwords (ids visited by the per-id loops)
//   +0x9c  a std::string (the character name or similar), c_str() is slot 11
//   +0xb8, +0xc8, +0xd8  16-byte blocks stored by slots 54, 74, 56
//   +0xe8, +0xf8  two 16-byte keys (slot 59 returns the address of one, slot 58 sets one)
//   +0x108 dword (slot 60), +0x10c byte (slot 62), +0x118 dword (an age/species selector; slot 72 maps it with
//   0x267350), +0x120 byte (read by slot 70), +0x129 byte (slot 67), +0x12a byte (set when slot 35 pushes a record)
namespace nTSCASCore {

// One record of the modifier stack. 0x40 bytes: the game's own copy constructor (0xcddbc0) and destructor (0xcdf820)
// are used on these, the layout is what the stack code reads.
struct BodyPartEntry {
    int32_t mKind;        // +0x00  1 = morph by name, 2/7 = named weights, 3 = part by id, 5/6/8 = other (see slot 15)
    int16_t mShortA;      // +0x04  (kind 3: the part category)
    int16_t mShortB;      // +0x06
    int32_t mValue;       // +0x08
    float mWeight;        // +0x0c
    uint8_t *mItemsFirst; // +0x10  vector of 0x44-byte items
    uint8_t *mItemsLast;  // +0x14
    uint8_t *mItemsEnd;   // +0x18
    float mExtra;         // +0x1c
    uint32_t mNameVTable; // +0x20  an embedded cRZString (vtable 0xd9b470, std::string, refcount)
    char mNameBuf[16];    // +0x24  std::string small buffer / heap pointer
    uint32_t mNameSize;   // +0x34
    uint32_t mNameCap;    // +0x38
    uint32_t mNameRef;    // +0x3c

    // The name's text: the heap pointer when the capacity is above 15, else the inline buffer.
    const char *Name() const;
};
static_assert(sizeof(BodyPartEntry) == 0x40, "BodyPartEntry layout");
static_assert(offsetof(BodyPartEntry, mWeight) == 0x0c, "BodyPartEntry layout");
static_assert(offsetof(BodyPartEntry, mNameBuf) == 0x24, "BodyPartEntry layout");
static_assert(offsetof(BodyPartEntry, mNameCap) == 0x38, "BodyPartEntry layout");

// A name and a weight: a cRZString followed by a float (slot 35 reads the string's text and the float at +0x20).
struct NamedWeight {
    cRZString mName;
    float mWeight;
};
static_assert(offsetof(NamedWeight, mWeight) == 0x20, "NamedWeight layout");

// One 0x44-byte item of a record's item vector: two cRZStrings and a dword.
struct BodyPartItem {
    cRZString mFirst;  // +0x00
    cRZString mSecond; // +0x20
    uint32_t mValue;   // +0x40
};
static_assert(sizeof(BodyPartItem) == 0x44, "BodyPartItem layout");

struct BodyPartItemVector {
    BodyPartItem *mFirst;
    BodyPartItem *mLast;
    BodyPartItem *mEnd;
};

// An MSVC std::vector<cRZString> header (elements are 0x20 bytes, with the game's vtable).
struct StringVector {
    cRZString *mFirst;
    cRZString *mLast;
    cRZString *mEnd;
};
static_assert(sizeof(StringVector) == 0xc, "StringVector layout");

// Containers whose memory comes from the allocator service (the original's node and buffer allocators call its slot 3,
// the frees slot 4).
template <typename T> using CasAlloc = GZServiceAllocator<T>;
using IdVector = std::vector<uint32_t, CasAlloc<uint32_t>>;
using ServiceString = std::basic_string<char, std::char_traits<char>, CasAlloc<char>>; // GZServiceString, 0x18 bytes
using ServiceStringVector = std::vector<ServiceString, CasAlloc<ServiceString>>;

// A 16-byte id (the GUIDs of parts and keys).
struct CasGuid {
    uint32_t mWords[4];
};
using GuidVector = std::vector<CasGuid, CasAlloc<CasGuid>>;

// The {first, last, end} triple the stacks keep on the heap (an MSVC std::vector<BodyPartEntry>).
struct BodyPartEntryVector {
    BodyPartEntry *mFirst;
    BodyPartEntry *mLast;
    BodyPartEntry *mEnd;
};
static_assert(sizeof(BodyPartEntryVector) == 0xc, "BodyPartEntryVector layout");

// An MSVC std::map header as the system embeds it: the head node and the size. The head node's links are
// {left, parent (the root), right}.
struct TreeHeader {
    struct Node {
        Node *mLeft;
        Node *mParent;
        Node *mRight;
    } *mHead;
    uint32_t mSize;
};
static_assert(sizeof(TreeHeader) == 8, "TreeHeader layout");
// An empty map: a fresh head node of `nodeBytes` from the allocator service (linked to itself, black, nil) and size 0, as
// the image's head allocators (0x3cff20, 0xcfca50, ...) build it; NewHeapTree does it on a header from operator new.
void InitTree(TreeHeader *tree, size_t nodeBytes);
TreeHeader *NewHeapTree(size_t nodeBytes);

// What slot 12 saves under an id and slot 13 restores (the value of a 0x78-byte node of the map at iface +0x28): the age
// mask, the three blocks (+0xb8/+0xc8/+0xd8), a copy of the modifier stack, the two keys and the dword at +0x108.
struct BodyPartSnapshot {
    uint32_t mAgeMask;            // +0x00  0x267350 of the age
    uint8_t mBlocks[3][16];       // +0x04
    BodyPartEntryVector mStack;   // +0x34  records copied with the game's vector assign 0xcd9ab0
    uint8_t mKeys[2][16];         // +0x40
    uint32_t mDword108;           // +0x60
    BodyPartSnapshot() { std::memset(this, 0, sizeof(*this)); } // the node constructor zero-fills the whole value
};
static_assert(sizeof(BodyPartSnapshot) == 0x64, "BodyPartSnapshot layout");
struct SnapshotMap : std::map<int32_t, BodyPartSnapshot, std::less<int32_t>, CasAlloc<std::pair<const int32_t, BodyPartSnapshot>>> {};
static_assert(sizeof(SnapshotMap) == 8, "SnapshotMap layout");

// The value of an AgeNameMap node: a map {head, size} whose own nodes (key a dword, value another map, erase 0xcdb830)
// are still the image's code. Destroyed as the pair destructor 0xcdf040 does: the image's erase, then the head freed.
struct AgeNameTable {
    TreeHeader mTree;
    AgeNameTable(const AgeNameTable &) = delete;
    ~AgeNameTable();
};

struct SurfaceRecord;
struct PartMaterialTable;

// The cITSCASBodyPartSystem subobject. `this` is the subobject; the vtable at +0 is the game's vftable 0xeba198.
struct BodyPartSystemIface {
    static constexpr uint32_t kVTable = 0xeba198; // 89 slots

    const void *mVTable;               // +0x00
    void *mOffsetLimits;               // +0x04  (system +0x20) heap map: region -> region -> key -> {low, high} (slot 63)
    std::vector<SurfaceRecord, CasAlloc<SurfaceRecord>> *mRecords; // +0x08  (system +0x24)
    struct TreeHeader *mTreeC;         // +0x0c  pointer to a heap map header
    struct TreeHeader *mTree10;        // +0x10  (system +0x2c) heap map, 0x1c-byte nodes (erase 0xcdb4a0)
    struct TreeHeader *mTree14;        // +0x14  pointer to a heap map header
    BodyPartEntryVector *mRedoStack;   // +0x18
    BodyPartEntryVector *mStack;       // +0x1c
    struct TreeHeader *mTree20;        // +0x20  (system +0x3c) heap map, 0x1c-byte nodes (erase 0xcdb970)
    void *mFloatNameMap;               // +0x24  (system +0x40)
    struct SnapshotMap *mSnapshots;    // +0x28  (system +0x44) heap std::map<int32_t, BodyPartSnapshot>, 0x78-byte nodes
    PartMaterialTable *mMaterials;     // +0x2c  (system +0x48) heap map: part key -> the part's materials (slot 25)
    TreeHeader mResources;             // +0x30  (system +0x4c) std::map<uint32_t, counted resource> (0x18-byte nodes,
                                       //         erase 0xcdbc10): the face-0 slot 14 cache
    TreeHeader mByAge[4];              // +0x38  (system +0x54) per-age AgeNameMap, indexed by +0x118 (unchecked)
    TreeHeader mTree58;                // +0x58  (system +0x74)
    uint32_t *mListAFirst;             // +0x60  vector of dwords (ids), begin
    uint32_t *mListALast;              // +0x64
    uint8_t mPad68[0x6c - 0x68];       // +0x68
    uint32_t *mListBFirst;             // +0x6c  vector of dwords (ids), begin
    uint32_t *mListBLast;              // +0x70
    uint8_t mPad74[0x78 - 0x74];       // +0x74
    uint32_t *mIdsFirst;               // +0x78
    uint32_t *mIdsLast;                // +0x7c
    uint8_t mPad80[0x9c - 0x80];       // +0x80
    char mText[16];                    // +0x9c  std::string small buffer / heap pointer
    uint32_t mTextSize;                // +0xac
    uint32_t mTextCap;                 // +0xb0
    uint8_t mPadB4[0xb8 - 0xb4];       // +0xb4
    uint8_t mBlockB8[16];              // +0xb8
    uint8_t mBlockC8[16];              // +0xc8
    uint8_t mBlockD8[16];              // +0xd8
    uint8_t mKeys[2][16];              // +0xe8 / +0xf8
    uint32_t mDword108;                // +0x108
    uint8_t mByte10C;                  // +0x10c
    uint8_t mPad10D[0x110 - 0x10d];    // +0x10d
    uint32_t mLastKind;                // +0x110  the part kind slot 24 last applied (7 counted as 3)
    uint32_t mLastKey;                 // +0x114  its key (0 for a non-zero id)
    uint32_t mAge;                     // +0x118
    uint32_t mCategoryMask;            // +0x11c  (system +0x138, 0x3f after construction) slot 88 ANDs it with an item's
                                       //         category mask and the id's (0xb5fd30)
    uint8_t mByte120;                  // +0x120
    uint8_t mPad121[3];                // +0x121
    uint32_t mLastPartId;              // +0x124  the part id slot 24 last applied
    uint8_t mPad128;                   // +0x128
    uint8_t mByte129;                  // +0x129
    uint8_t mByte12A;                  // +0x12a
    uint8_t mPad12B;                   // +0x12b
    uint32_t mMaterialSerial;          // +0x12c  counts the material names slots 26 / 27 generate
    uint8_t mPad130[0x140 - 0x130];    // +0x130
    void *mSkinBaseFilter;             // +0x140  the system's nTSCASCore::cTSSkinBaseFilter (system +0x15c)
    void *mSkinFilter;                 // +0x144  (system +0x160) nTSCASCore::cTSSkinFilter
    void *mOverlayFilter;              // +0x148  (system +0x164) nTSCASCore::cTSOverlayFilter, the filter slot 40 queries with

    // ---- trivial stores and getters ----
    void StoreBlockB8(const void *src);        // slot 54, 0xb81a30: ret 4, 16 bytes to +0xb8
    void StoreBlockC8(const void *src);        // slot 74, 0xcf7f00: ret 4, 16 bytes to +0xc8
    void StoreBlockD8(const void *src);        // slot 56, 0xcf80c0: ret 4, 16 bytes to +0xd8
    void StoreDword108(uint32_t value);        // slot 60, 0xcf81c0: ret 4
    void StoreByte10C(uint8_t value);          // slot 62, 0xcf7f20: ret 4
    void StoreByte129(uint8_t value);          // slot 67, 0xcf7ef0: ret 4
    uint32_t ByteGate120(int32_t mode);        // slot 70, 0xcf3720: ret 4; mode 0 true, positive -> [+0x120] != 0, negative false (eax returned whole)
    const char *GetText();                     // slot 11, 0xceaae0: ret 0, c_str() of the string at +0x9c
    int32_t RedoStackCount();                  // slot 19, 0xcf6810: ret 0, entries in the redo stack
    int32_t StackCount();                      // slot 18, 0xcfab60: ret 0, entries in the modifier stack
    const void *GetKey(uint32_t index);        // slot 59, 0xcf2160: ret 4, +0xe8 + 16*index for index 0/1, else the zero GUID
    uint32_t AgeMask();                        // slot 72, 0xcf3210: ret 0, 0x267350 applied to the +0x118 selector

    // ---- queries on the modifier stack ----
    bool HasActiveMorph(const char *name);     // slot 8, 0xcf3220: ret 4, a kind-1 record with a non-zero weight and this name
    float TopMorphWeight(const char *name);    // slot 51, 0xce1690: ret 4, weight of the newest record if it is kind 1 with this name, else 0
    float SumNamedWeights(const char *name, uint32_t unused); // slot 50, 0xcf4310: ret 8, kind 2/7 weights by name, newest first, up to a kind-1 record
    bool ClearPartWeight(int32_t value, int32_t category);    // slot 17, 0xcf7370: ret 8, zeroes the weight of kind-3 records with this value (+8) and category (+4; -1 = any)
    void ClearGuidPartWeights(const uint32_t *guid);          // slot 39, 0xcf7220: ret 4, zeroes the weight of kind-3 records named by this GUID

    // ---- pushes onto the modifier stack (each builds a record, copies it onto the end, destroys the temporary) ----
    void PushMarker();                         // slot 32, 0xcf59e0: ret 0, a kind-8 record with nothing in it
    void PushKind5(const void *items);         // slot 33, 0xcf5ce0: ret 4, kind 5 weight 1.0 carrying a copy of the item vector (only when +0x118 != 0)
    void PushKind6(const NamedWeight *source); // slot 35, 0xcf5da0: ret 4, kind 6 named from the string at source+4, weight from source+0x20; sets +0x12a
    void PushKind0(const char *name);          // slot 47, 0xcf5e50: ret 4, kind 0 with weight 1.0

    // ---- dispatchers over the id lists (they call other slots of this object, and original helpers on the system) ----
    void VisitIds(uint32_t a, uint32_t b);     // slot 21, 0xce3280: ret 8, slot 20(a, b, id) for each id at +0x78
    // Slots 64 and 65, ret 0x18: for each id at +0x78, the original helper 0xcf7700 / 0xcecea0 on the system (7 dwords:
    // the arguments with the id third). Arguments 4 and 5 are floats in the original (copied as bits).
    void VisitIdsF7(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6);  // slot 64, 0xcf76a0
    void VisitIdsEA(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6);  // slot 65, 0xcece60
    void ApplyGuidText(const char *text, uint32_t a, uint32_t b);  // slot 38, 0xcf5ca0: ret 0xc, parses the text into a GUID (0x4f8eb0) and calls slot 37
    void StoreKey(uint32_t index, const void *key);               // slot 58, 0xcf8140: ret 8, sets key 0/1 when it changes, then refreshes
    void ApplyAll(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6);  // slot 22, 0xcfac20: ret 0x18
    void ApplyAllPasses(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4);                       // slot 23, 0xcfade0: ret 0x10

    // Slots 44 and 46, ret 4: only when +0x118 != 0. Empties the vector of cRZString passed in (each element destroyed
    // through its own vtable slot 33 with flag 0), then appends the name of every kind-3 record with a non-zero weight
    // whose value (+8) is 0 or 0xb (slot 44) / 5 (slot 46) with the game's push_back 0x3a05e0.
    void CollectNamesValue0B(StringVector *out);   // slot 44, 0xcf1f70
    void CollectNamesValue5(StringVector *out);    // slot 46, 0xcf1d90

    // Slots 34 and 36: whether the newest kind-5 / kind-6 record equals the given contents (al only; the upper bits of eax
    // are leftovers in the original).
    bool ItemsMatchNewestKind5(const BodyPartItemVector *items);   // slot 34, 0xcf3360: ret 4, same item count and every item's two strings and dword equal
    bool MatchesNewestKind6(const NamedWeight *named);             // slot 36, 0xcf3410: ret 4, same name (text through slot 5) and weight

    // ---- resets ----
    void ClearTreeAt14();                      // slot 10, 0xcf71f0: ret 0: erases every node of the tree held by [+0x14] (0xcdb400), leaving it empty
    void LoadFaceData();                       // slot 4, 0xcf3750: records and name map emptied, then the FACE resource of
                                               // the first package (of the directories 0x8ded6280) that has one is read
    void ResetState();                         // slot 9, 0xcf7110: ret 0: zero GUIDs into +0xb8/+0xc8/+0xd8, +0x108 = 1, clears the trees at [+0xc], +(age+7)*8 and +0x58, calls slot 16 and several originals

    // ---- undo and redo ----
    void PopEntry(uint32_t moveToRedo);        // slot 14, 0xcfab70: ret 4 (byte flag): removes the newest record, optionally onto the redo stack
    void RedoEntry();                          // slot 15, 0xcf6820: ret 0: takes the newest redo record and replays it through the push slots
    void ClearStacks();                        // slot 16, 0xcf6e80: ret 0: destroys every record of both stacks

    // ---- parts (lane JA). A part is looked up by (a, b, id): the two outer keys and an id of the list at +0x78 ----
    // Slots 43 and 45, ret 4: only when +0x118 != 0. Zero the kind-3 weights for value 0 and 0xb (slot 43) / 5 (slot 45)
    // through slot 17, then apply each name of the vector through slot 38 (text, 1, 0): the counterparts of slots 44 / 46.
    void ApplyNamesValue0B(const StringVector *names); // slot 43, 0xcf80e0
    void ApplyNamesValue5(const StringVector *names);  // slot 45, 0xcf8070
    void *GetPart(uint32_t a, uint32_t b, uint32_t id); // slot 52, 0xcf43d0: ret 0xc, the system's lookup 0xcf1670, its +0xc
    // Slot 30, ret 4: the target's slot 18, then for every (A, B, C) id triple of the three lists the system's 0xce5830
    // (target, a, b, c, flags); for a non-zero +0x118 two passes per (a, b) around 0xcfa2d0(a, b, 0, 0). The byte results
    // ANDed together (1 for no calls; 0 for a null target).
    uint8_t ApplyToAllParts(void *target);
    // Slot 81, ret 0xc: for each id, each child of the part (slot 21, then slot 20 of each child) that answers interface
    // 0x3ca0e8ab with its virtual base's slot 18 non-zero gets slot 82(a, b, id, system as 0x4512dd13); then slot 82(a, b,
    // id, target) for each id. The byte results ANDed (0 for a null target).
    uint8_t ApplyToChildren(uint32_t a, uint32_t b, void *target);
    // Slot 83, ret 0x14: the node `name` of the system's slot-14 object for (a, b): its transform (slot 53) copied out,
    // the 12-byte position and the 16 bytes from +0xc. False when there is no object or node.
    bool GetNodeTransform(uint32_t a, uint32_t b, const char *name, void *position, void *rotation);
    // Slot 20, ret 0xc: refreshes the part (a, b, id) and the parts linked to it (slot 53): for each id the object
    // lists (its slot 56), the entry of that id (slot 48) gets slot 34, ids other than 0 only while slot 69 is set.
    void RefreshPart(uint32_t a, uint32_t b, uint32_t id);
    // Slot 53, ret 0x18: the parts linked to part (a, b, id) through the system's entry 0xcf04b0 for the part's first id:
    // each enabled link (+8) with an object (+0xc) adds that object (counted) to `objects`, its value (0xcfeba0) to
    // `values` and its GUID (0xcff0c0 when its kind 0xd025f0 is 8, else the zero GUID) to `guids` (both optional).
    void CollectLinkedParts(uint32_t a, uint32_t b, uint32_t id, ::ServiceRefVector *objects, IdVector *values,
                            GuidVector *guids);

    // ---- copying in (lane JA) ----
    // Slot 29, ret 0x1c: copies state from `source` (another object's interface: slot 24 its kind, 30/31 two GUIDs, 32 a
    // key by index, 33 a word, 34-38 records) through this face's own slots; each flag byte selects one group. Nothing
    // when the source's kind is 1.
    void CopyFrom(void *source, uint8_t resetBlocks, uint8_t copyWord, uint8_t copyKeys, uint8_t copyNamed, uint8_t copyItems,
                  uint8_t copyNames5);
    // Slot 37, ret 0xc: the skin base filter narrowed to this age and GUID; for the skin catalog's first match whose
    // category, value and layer 0xcf1c20 gives, a kind-3 record named by the GUID text (weight 1 when `on`) replaces the
    // first matching kind-3 record (same category, a layer not above it, same value unless the age is 0; never when
    // `asNew`) or is appended.
    void ApplyPartGuid(const uint32_t *guid, uint8_t on, uint8_t asNew);
    // Slot 24, ret 0x28: for part (kind, key, id) and each of its ids (all of them through slot 56, else slot 57's)
    // that has an entry (slot 48), each (kind, key) pair (all of lists A / B, or the part's own) gets the system's entry
    // 0xcf04b0 rebuilt (0xcfbbb0, 0xce7430; +0x50 = -1 first when `resetIndex`), a first pass 0xcfb180(1, 0), and a
    // refresh (slot 31, then 0xcfb180(0, 1)), each when its flag is set; +0x110/+0x114/+0x12c remember the last one.
    void ApplyPart(uint32_t kind, uint32_t key, uint32_t id, uint8_t allKinds, uint8_t allKeys, uint8_t allIds, uint8_t rebuild,
                   uint8_t refresh, uint8_t firstPass, uint8_t resetIndex);
    // Slot 80, ret 0x14: the mean of (position - base position) over the vertices of entry (kind, key, id, 0) whose
    // weight for `morph` (0xcf29b0) is exactly 1; false (out untouched) for morph 0, no weights or no such vertex.
    bool AverageWeightedOffset(uint32_t kind, uint32_t key, uint32_t id, uint32_t morph, float *out);

    // ---- lane JY ----
    // Slots 6 and 7, ret 4: the entry `name` of the current age's AgeNameMap (+0x38 + 8 * age) erased / looked up.
    void EraseAgeName(const char *name);       // slot 6, 0xcf6ee0
    bool HasAgeName(const char *name);         // slot 7, 0xcf3280
    // Slot 12, ret 4: the snapshot under `id` (inserted zero-filled when missing) set from the current state.
    void SaveSnapshot(int32_t id);             // 0xcf74c0
    // Slot 13, ret 4: false when there is no snapshot under `id`; else the system's 0xce9530 and 0xcebb70, the age (slot
    // 71 with the saved mask), the blocks (slots 54, 74, 56), the stack, the keys and +0x108 restored, then 0xcf8670.
    bool RestoreSnapshot(int32_t id);          // 0xcf73e0
    // Slot 71, ret 4: the age 0xb5b700 maps `code` to; when it changes, everything that depends on it is reset and
    // rebuilt (+0xd8 zeroed, keys, 0xcf8670, slots 10 and 9, +0x10c, the resource cache, the category lists and names).
    void SetAge(uint32_t code);                // 0xcf85e0
    // Slot 48, ret 0xc: with `merge` set and the newest record a kind-1 record of this name (any case), its weight
    // grows by `weight` (clamped to 0..1) and the mesh entries 0xcf06e0 collects that belong to the newest record (+0x50
    // the record count, enabled at +0x54) are rebuilt from their base positions and reweighted (0xce18c0) when they are of
    // the last part (+0x110/+0x114), else marked stale (+0x50 = -1); otherwise a kind-1 record is pushed.
    void AddMorph(const char *name, float weight, uint8_t merge);   // 0xcf54e0
    // Slot 49, ret 8: the same for kind-7 records (no clamp, no enabled flag): 0xce1550 moves the entries from the old
    // total (weight + extra) to the new one; otherwise a kind-7 record is pushed with slot 50's sum as its extra.
    void AddNamedWeight(const char *name, float weight);            // 0xcf5770
    // Slot 5, ret 8: for each id `source` lists (its slot 14), the points of that id (slot 13; its slot 16 gives the kind,
    // key and two more values) are copied into this system's part entry (0xcec500) for `name`.
    void ImportPartData(void *source, const char *name);            // 0xce09a0
    // Slots 85 and 86: a text line per id of +0x78 about the vertex ids of the id's part entry for `name` (0xcec500, or
    // 0xcf07a0 with a group index), handed to `out` (a cIGZString: emptied with slot 26, set with slot 16).
    void ReportVertexIds(const char *name, void *out);                       // slot 85, 0xcee3f0, ret 8
    void ReportGroupVertexIds(const char *name, uint32_t group, void *out);  // slot 86, 0xcee680, ret 0xc
    // Slot 87, ret 8 (lane JZ): for each region of the delimited list and each id of +0x78 whose part has weights for
    // it (0xcf29b0), a line when the region mask is missing (a region the part id must cover, +0x42 clear), the vertex
    // ids are missing (+0x30 clear) or fewer were matched (+0x3c < +0x34: the counts); handed to `out` like slot 85.
    void ReportRegionMasks(const char *regions, void *out);                  // 0xcee930
    // Slot 66, ret 4: `out` emptied, then one "a|b|c|d" text (the image's list delimiter) per record of +0x08.
    void CollectRecordTexts(std::vector<std::basic_string<char, std::char_traits<char>, CasAlloc<char>>,
                                        CasAlloc<std::basic_string<char, std::char_traits<char>, CasAlloc<char>>>> *out); // 0xced0e0
    // Slot 28, ret 8: (`reset`: slot 16 first) the property set the queried object (GetCachedQueriedObject, its slots
    // 19 and 33) gives for `id`: the hair and skin tone GUIDs to slots 74 and 54, then for each of the overlay count's
    // overlay properties the text before its first delimiter to slot 38 (text, 1, 0).
    void ApplyOutfitProperties(uint32_t id, uint8_t reset); // 0xcf9f30

    // ---- lane JZ: the point exports. `source` lists ids (its slot 14), each id's 12-byte points (slot 13) and the
    // part entry key (slot 16: kind, key, part id, id); `target` is emptied first (its slot 18) and gets one point
    // vector per id (slot 12). They run at age 1: the state is saved under 0x91e37c4f (slot 12), the age set (slot
    // 71) and restored after (slot 13), each only when slot 72 says the age mask is not 1 already. ----
    // Slot 76, ret 8: per vertex of the id's part entry a (point - vertex origin) delta and the vertex's uv, reading
    // the points two at a time (an ORIGINAL-BUG stride: see the source).
    void ExportPointDeltas(void *source, void *target);                // 0xce9840
    // Slot 77, ret 8: per vertex the origin plus the point of the same index, then a zero point.
    void ExportPointPositions(void *source, void *target);             // 0xce9b30
    // Slot 78, ret 0xc: per vertex its origin and uv; the origin moved by the first (delta, uv) pair of the points whose
    // uv matches the vertex's (same signs, within 1e-4), except for the kinds `mode` 3 / 4 leaves alone.
    void ExportPositionsByUv(void *source, void *target, uint32_t mode); // 0xce9e20
    // Slot 41, ret 4: the morph records removed (face-0 slot 13) and an empty kind-0 group pushed (slot 47), then each
    // text parsed (0xb5ebe0: name, kind, weight): kind 1 a morph (slot 48, weight, no merge; before it, with no group
    // yet and a weight of at least 1, the morphs removed and the empty group pushed again), kinds 4 / 2 a named weight
    // (0xcf5930 with merge 0 / 1), kind 3 a group pushed (slot 47) when it differs from the last (the image's
    // "every region" list counting as the empty group).
    void ApplyModifierTexts(const StringVector *texts); // 0xcf81d0
    // Slot 42, ret 4, the inverse of slot 41: `out` emptied, then the stack's records (those of weight 0 only when kind
    // 0) as (name, code, weight): kind 1 code 1 (at weight >= 1 with no group seen yet, the list so far dropped), 7 code
    // 4, 2 code 2, 0 code 3 when its name differs from the last group's; each formatted by 0xb5f0b0 ("%s:%d:%f").
    void ExportModifierTexts(StringVector *out); // 0xcf2180
    // Slot 31, ret 0x10: part (kind, key, id) (slot 52) and its entry 0xcf04b0(kind, key, id, partId) written back into
    // the part's geometry (its slot 48(partId)): the geometry's mesh (slot 39) must have one group per entry group; per
    // group with a vertex block, the position and normal elements (0xaeda40) are read out (index and vertex iterators),
    // each entry vertex's position (+0) / normal (+0x30) stored at the slots the group's index maps give, and both
    // uploaded (mesh slot 12); the mesh is handed back (geometry slot 38).
    void RefreshGeometry(uint32_t kind, uint32_t key, uint32_t id, uint32_t partId); // 0xce7ee0
    // Slot 40, ret 0xc, 0 / 1 in eax: the overlay filter (+0x148) set up (a and b mapped by 0xb5fc80 / 0xb5fd20, the age
    // mask, the +0xb8 / +0xc8 blocks, the GUID of `guidText`) and the resource catalog queried (retried with the skin
    // base's GUID 0xceefb0 in place of +0xc8 when nothing matched); the first match's value (its slot 7 / 6 for types
    // 4 / 5); then whether an enabled kind-3 record (weight above 0.5, the last of case-insensitively equal names
    // deciding) has a first match with the same GUID and that value.
    uint32_t IsGuidApplied(uint32_t a, uint32_t b, const char *guidText); // 0xcf45e0
    // Slot 82, ret 0x10, always true: for each bone adjust of the part entry (kind, key, id) (0xcf04b0 with part 0,
    // its 0x3c-byte records at +0x18), when the target has that bone (0xae7490), the target's "bone_adjust" extension
    // tagged with the bone (0xaeb5b0, slot 18) or a new one (the framework's slot 3, added with the target's slot 22)
    // gets the translation and rotation (its slots 5 and 7), and so does the bone node (its slots 64 and 65).
    bool ApplyBoneAdjusts(uint32_t kind, uint32_t key, uint32_t id, void *target); // 0xce0e60
    // Slots 26 and 27: a material from the property set the queried object (GetCachedQueriedObject, its slots 19 and
    // 33) gives for `id`: its material (slot 7, held) named `name` or a generated unique name (+0x12c counted, the
    // queried object's slot 31 says whether a name is taken), parameters set (its slot 13), handed to the queried
    // object (slot 23 with 2), then its "_txmt" resource key (0xae7340 with `keyArg`) and `resourceArg` set on the
    // material's slot-9 object, and the persist service told; the material's slot 19 returned (0 without properties).
    // Slot 26, ret 0x14: the age and gender come from the properties (both required; numbered with the name), plus
    // the eye gizmo flag and the overlay count and overlay texts copied.
    uint32_t CreateSkinMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint8_t eyeGizmo); // 0xcea630
    // Slot 27, ret 0x20: `a`, `b` number a generated name; the species is set, and with `hair` the hair flag and
    // alpha blending.
    uint32_t CreateHairMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint32_t a, uint32_t b,
                                uint32_t species, uint8_t hair); // 0xcea2f0

    // ---- lane KA ----
    // Slot 25, ret 0x10: the part entries of (kind, key) made for each id of +0x78 (0xcf04b0 with part 0), then each
    // material of the part (mMaterials[PartTableKey(kind, key)]) that is rebuildable and used by some subset is rebuilt
    // (0xcfb1d0) and renamed after the new material; with `applyToParts`, each subset user's part (slot 52) gets the
    // (material, subset) names through its slot 34: directly when the user's GUID is the zero key, else on each linked
    // object of each of the part's ids (slot 56) whose link GUID (0xcff0c0) is the user's. True when any was renamed.
    bool RebuildPartMaterials(uint32_t kind, uint32_t key, uint32_t materialArg, uint8_t applyToParts); // 0xcfb830
    // Slot 3, ret 4: empties the containers `flags` selects, each heap map with its own erase: 1 the four per-age name
    // maps, 2 the map at +0x58; 8 the heap maps at +0x10, +0xc and the materials (+0x2c) replaced by new empty ones
    // (with 0xd0a9e0 and 0xcfebc0 on system +0x158 / +0x14c between); 4 the heap maps at +0x14 and +0x20 replaced; 0x10
    // slot 16, both stacks' records destroyed, the snapshots emptied, then the two stacks and the snapshot map replaced.
    void ClearData(uint32_t flags); // 0xcf4f50
    // The key of a part's per-part tables (the face-0 slot 14 cache and mMaterials): kind (7 as 3) in bits 1-7, the low
    // bit of `key` in bit 0 at age 0.
    int32_t PartTableKey(uint32_t kind, uint32_t key) const
    {
        const uint32_t k = kind == 7 ? 3 : kind;
        return static_cast<int32_t>((k & 0x7f) * 2 | (mAge == 0 ? key & 1 : 0));
    }

    TreeHeader *AgeMap() { return &mByAge[mAge]; } // unchecked, as the original indexes it

    // ---- lane KB ----
    // Slot 88, ret 4: `out` (a cIGZString) emptied (slot 26), then for a non-zero age a bone report appended line by line
    // (slot 19). The skin base filter (+0x140) is reset (its slot 7) and set to the age mask, 8 and 2; the catalog's
    // modifiers (slot 34) and archetypes (slot 32) for it are grouped by GUID (their slots 10 / 6), and per GUID and id
    // of +0x78 each bone of the GUID's part entry (0xcf07a0 per group, "MOD+" / "MOD-" when the entry is signed; 0xcec500
    // for archetypes) whose name the id's base part entry (0xcf04b0) also has is printed with the delta and both
    // positions, under the name of the first item of the GUID in the id's categories (+0x11c).
    void ReportBones(void *out); // 0xced540
    // Slot 63, ret 0x10: `out` emptied, the service 0xb390c0 fetched, and `flags` ("region,flag" texts; flag 1 picks
    // outfit A for the region, else B; A without a text) read. For each kind of +0x60, key of +0x6c and id of +0x78 whose
    // part (slot 52) exists, each part id of it (the part's slot 56): the entry 0xcf04b0 refreshed (0xce4590) and its
    // +0x50 zeroed; with groups, and both outfits having the part (their slot 15 then 13), each record of +0x08 whose two
    // regions have weights (0xcf29b0): each region fitted to its outfit once per part (0xce3010), the offset of their
    // centre heights (0xce9710 at 0.9) checked against the limits at +0x04 (only with +0x129; else +-9999): below moves
    // region 3, above region 2 (0xce1600 by the excess), and a "kind,key,a,b,verdict,offset" line is added (verdict 0
    // below, 1 above, 2 within); after the records 0xce7430 and slot 31 (kind, key, id, part id).
    void CheckRegionPairs(void *outfitA, void *outfitB, const ServiceStringVector *flags, ServiceStringVector *out); // 0xce1ff0
    // Slot 84, ret 0xc: writes the mesh of entry 0xcf04b0(kind, key, age ? 2 : 0, 0) (rebuilt with 0xcfbbb0 + 0xce7430)
    // as a Wavefront file `path` + ".obj" (false when it does not open): positions and normals keyed by the vertices'
    // indices (+0x4a / +0x4e; missing indices up to the largest filled in), texture coordinates per position index
    // (a corner whose coordinate differs by more than 1e-6 gets its own), then the faces. Then, when the part (slot 52)
    // has a "face" whose properties name a "compositeBaseTextureName" texture with a level-0 image, that image is saved
    // by the image service as `path` + ".bmp". True once the file was written.
    bool ExportFaceObj(const char *path, uint32_t kind, uint32_t key); // 0xceac20
};

// ---- Strings and containers the system keeps (element types as the original's container code builds them) ----------

// A cRZString held by value in the system's surface containers. The node code builds it inline: a copy of the text, the
// game's cRZString vtable, refcount 0 (not the source's). kVirtualDestroy: how the container's erase destroys it. The
// surface map's erase (0xcdc380, element destructor 0x257860) frees the text inline; the name sets' erase (0xcdc500)
// calls the element's own vtable slot 33 (the deleting destructor) with flags 0.
template <bool kVirtualDestroy> struct BasicCasName {
    union {
        cRZString mText;
    };
    BasicCasName();                                  // empty text
    BasicCasName(const char *text, size_t length);   // the text's first `length` bytes
    BasicCasName(const BasicCasName &other);         // a copy of the text (std::string's _Construct 0x14135c), refcount 0
    ~BasicCasName();
    BasicCasName &operator=(const BasicCasName &) = delete;
};
template <bool V> BasicCasName<V>::BasicCasName()
{
    ::new (&mText.mString) std::string();
    mText.mVTable = GameAddress<void>(cRZString::kVTable);
    mText.mRefCount = 0;
}
template <bool V> BasicCasName<V>::BasicCasName(const char *text, size_t length)
{
    ::new (&mText.mString) std::string(text, length);
    mText.mVTable = GameAddress<void>(cRZString::kVTable);
    mText.mRefCount = 0;
}
template <bool V> BasicCasName<V>::BasicCasName(const BasicCasName &other)
{
    ::new (&mText.mString) std::string(other.mText.mString);
    mText.mVTable = GameAddress<void>(cRZString::kVTable);
    mText.mRefCount = 0;
}
template <bool V> BasicCasName<V>::~BasicCasName()
{
    if constexpr (V) {
        VirtualCall<void *(__thiscall *)(void *, uint32_t), 33>(&mText, 0); // the element's own deleting destructor, flags 0
    } else {
        mText.mString.~basic_string();
    }
}
using SurfaceName = BasicCasName<false>;
using SurfaceSetName = BasicCasName<true>;
static_assert(sizeof(SurfaceName) == 0x20, "SurfaceName: a cRZString");

// std::less over the texts (std::operator< on std::string, 0x144a19).
struct CasNameLess {
    template <bool A, bool B> bool operator()(const BasicCasName<A> &a, const BasicCasName<B> &b) const
    {
        return a.mText.mString < b.mText.mString;
    }
};

using SurfaceMap = std::map<SurfaceName, SurfaceName, CasNameLess, CasAlloc<std::pair<const SurfaceName, SurfaceName>>>; // 0x50-byte nodes (0xcfcf50)
using SurfaceSet = std::set<SurfaceSetName, CasNameLess, CasAlloc<SurfaceSetName>>; // 0x30-byte nodes (0xcfce10)
using SurfaceSetVector = std::vector<SurfaceSet, CasAlloc<SurfaceSet>>;
// The per-age maps at iface +0x38: a name (0x38-byte nodes, find 0x37eeb0) to a table of the image's.
using AgeNameMap = std::map<SurfaceName, AgeNameTable, CasNameLess, CasAlloc<std::pair<const SurfaceName, AgeNameTable>>>;
static_assert(sizeof(AgeNameMap) == 8, "AgeNameMap layout");
static_assert(sizeof(IdVector) == 0xc && sizeof(SurfaceMap) == 8 && sizeof(SurfaceSet) == 8, "container sizes");

// One 0x60-byte record of the vector at +0x24 (four std::strings; its element destructor is 0xcdf3d0).
struct SurfaceRecord {
    std::string mText[4];
};
static_assert(sizeof(SurfaceRecord) == 0x60, "SurfaceRecord layout");

// nTSCASCore::cTSCASLockedObjectManager as the system embeds it at +0x14c (constructor 0xcfdf50, destructor 0xcfe450, both
// the image's, called by the system's constructor and destructor): its vtable and a std::map (std::allocator: a 0x58-byte
// head node from operator new) of locked objects.
struct cTSCASLockedObjectManager {
    const void *mVTable; // +0 the class's game vtable
    void *mHead;         // +4 the map's head node
    uint32_t mSize;      // +8
    void RegisterNoFactory(); // 0xd005c0: the persist resource manager's slot 20 with no factory for the locked-object type
};
static_assert(sizeof(cTSCASLockedObjectManager) == 0xc, "cTSCASLockedObjectManager layout");

// The maps the system embeds whose nodes the image's code builds and erases (their node types only partly known here): a
// header with a fresh head node from the allocator service when built; destroyed as the image's map destructors do (0xcdea90
// for the per-age maps): the image's erase from the root, then the head freed (no reset of the header).
struct ResourceTree : TreeHeader { // +0x4c: 0x18-byte nodes (ResourceMap), erase 0xcdbc10 (releases the values)
    ResourceTree();
    ~ResourceTree();
    ResourceTree(const ResourceTree &) = delete;
};
struct AgeNameTree : TreeHeader { // +0x54 (four): 0x38-byte nodes (AgeNameMap), erase 0xcdc280
    AgeNameTree();
    ~AgeNameTree();
    AgeNameTree(const AgeNameTree &) = delete;
};
struct Tree74 : TreeHeader { // +0x74: 0x38-byte nodes, erase 0xcdc300
    Tree74();
    ~Tree74();
    Tree74(const Tree74 &) = delete;
};
// The system's name (+0xb4): a cRZString built as the constructor builds it inline (the game's vtable, the std::string's 16
// bytes zeroed first, refcount 0) and destroyed inline (the vtable stored again, the text freed).
struct SystemName {
    union {
        cRZString mText;
    };
    SystemName();
    ~SystemName();
    SystemName(const SystemName &) = delete;
};
static_assert(sizeof(SystemName) == 0x20, "SystemName: a cRZString");

class cTSCASBodyPartSystem;

// nTSCASCore::cITSCASBodyPartSystem (iid 0xcb7d64f8): the 89-slot face at +0x1c (game vftable 0xeba198). Slots 3-88 in slot
// order with the bodies' signatures (BodyPartSystemIface above: `ret N` / 4 stack words each). The folded getters and
// setters (55, 57, 61, 68, 69, 73, 75) and the empty slot 79 are the class's own inline overrides.
class cITSCASBodyPartSystem : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xcb7d64f8;

    virtual void ClearData(uint32_t flags) = 0;                                                    // 3
    virtual void LoadFaceData() = 0;                                                               // 4
    virtual void ImportPartData(void *source, const char *name) = 0;                               // 5
    virtual void EraseAgeName(const char *name) = 0;                                               // 6
    virtual bool HasAgeName(const char *name) = 0;                                                 // 7
    virtual bool HasActiveMorph(const char *name) = 0;                                             // 8
    virtual void ResetState() = 0;                                                                 // 9
    virtual void ClearTreeAt14() = 0;                                                              // 10
    virtual const char *GetText() = 0;                                                             // 11
    virtual void SaveSnapshot(int32_t id) = 0;                                                     // 12
    virtual bool RestoreSnapshot(int32_t id) = 0;                                                  // 13
    virtual void PopEntry(uint32_t moveToRedo) = 0;                                                // 14
    virtual void RedoEntry() = 0;                                                                  // 15
    virtual void ClearStacks() = 0;                                                                // 16
    virtual bool ClearPartWeight(int32_t value, int32_t category) = 0;                             // 17
    virtual int32_t StackCount() = 0;                                                              // 18
    virtual int32_t RedoStackCount() = 0;                                                          // 19
    virtual void RefreshPart(uint32_t a, uint32_t b, uint32_t id) = 0;                             // 20
    virtual void VisitIds(uint32_t a, uint32_t b) = 0;                                             // 21
    virtual void ApplyAll(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) = 0; // 22
    virtual void ApplyAllPasses(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4) = 0;           // 23
    virtual void ApplyPart(uint32_t kind, uint32_t key, uint32_t id, uint8_t allKinds, uint8_t allKeys, uint8_t allIds,
                           uint8_t rebuild, uint8_t refresh, uint8_t firstPass, uint8_t resetIndex) = 0; // 24
    virtual bool RebuildPartMaterials(uint32_t kind, uint32_t key, uint32_t materialArg, uint8_t applyToParts) = 0; // 25
    virtual uint32_t CreateSkinMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg,
                                        uint8_t eyeGizmo) = 0;                                     // 26
    virtual uint32_t CreateHairMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint32_t a,
                                        uint32_t b, uint32_t species, uint8_t hair) = 0;           // 27
    virtual void ApplyOutfitProperties(uint32_t id, uint8_t reset) = 0;                            // 28
    virtual void CopyFrom(void *source, uint8_t resetBlocks, uint8_t copyWord, uint8_t copyKeys, uint8_t copyNamed,
                          uint8_t copyItems, uint8_t copyNames5) = 0;                              // 29
    virtual uint8_t ApplyToAllParts(void *target) = 0;                                             // 30
    virtual void RefreshGeometry(uint32_t kind, uint32_t key, uint32_t id, uint32_t partId) = 0;   // 31
    virtual void PushMarker() = 0;                                                                 // 32
    virtual void PushKind5(const void *items) = 0;                                                 // 33
    virtual bool ItemsMatchNewestKind5(const BodyPartItemVector *items) = 0;                       // 34
    virtual void PushKind6(const NamedWeight *source) = 0;                                         // 35
    virtual bool MatchesNewestKind6(const NamedWeight *named) = 0;                                 // 36
    virtual void ApplyPartGuid(const uint32_t *guid, uint8_t on, uint8_t asNew) = 0;               // 37
    virtual void ApplyGuidText(const char *text, uint32_t a, uint32_t b) = 0;                      // 38
    virtual void ClearGuidPartWeights(const uint32_t *guid) = 0;                                   // 39
    virtual uint32_t IsGuidApplied(uint32_t a, uint32_t b, const char *guidText) = 0;              // 40
    virtual void ApplyModifierTexts(const StringVector *texts) = 0;                                // 41
    virtual void ExportModifierTexts(StringVector *out) = 0;                                       // 42
    virtual void ApplyNamesValue0B(const StringVector *names) = 0;                                 // 43
    virtual void CollectNamesValue0B(StringVector *out) = 0;                                       // 44
    virtual void ApplyNamesValue5(const StringVector *names) = 0;                                  // 45
    virtual void CollectNamesValue5(StringVector *out) = 0;                                        // 46
    virtual void PushKind0(const char *name) = 0;                                                  // 47
    virtual void AddMorph(const char *name, float weight, uint8_t merge) = 0;                      // 48
    virtual void AddNamedWeight(const char *name, float weight) = 0;                               // 49
    virtual float SumNamedWeights(const char *name, uint32_t unused) = 0;                          // 50
    virtual float TopMorphWeight(const char *name) = 0;                                            // 51
    virtual void *GetPart(uint32_t a, uint32_t b, uint32_t id) = 0;                                // 52
    virtual void CollectLinkedParts(uint32_t a, uint32_t b, uint32_t id, ::ServiceRefVector *objects, IdVector *values,
                                    GuidVector *guids) = 0;                                        // 53
    virtual void StoreBlockB8(const void *src) = 0;                                                // 54
    virtual const void *GetBlockB8() = 0;                                                          // 55 (folded 0x418bd0)
    virtual void StoreBlockD8(const void *src) = 0;                                                // 56
    virtual const void *GetBlockD8() = 0;                                                          // 57 (folded 0x60d700)
    virtual void StoreKey(uint32_t index, const void *key) = 0;                                    // 58
    virtual const void *GetKey(uint32_t index) = 0;                                                // 59
    virtual void StoreDword108(uint32_t value) = 0;                                                // 60
    virtual uint32_t GetDword108() = 0;                                                            // 61 (folded 0x418b60)
    virtual void StoreByte10C(uint8_t value) = 0;                                                  // 62
    virtual void CheckRegionPairs(void *outfitA, void *outfitB, const ServiceStringVector *flags,
                                  ServiceStringVector *out) = 0;                                   // 63
    virtual void VisitIdsF7(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) = 0; // 64
    virtual void VisitIdsEA(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) = 0; // 65
    virtual void CollectRecordTexts(ServiceStringVector *out) = 0;                                 // 66
    virtual void StoreByte129(uint8_t value) = 0;                                                  // 67
    virtual void SetByte120(uint8_t value) = 0;                                                    // 68 (folded 0x4e7f40)
    virtual uint8_t GetByte120() = 0;                                                              // 69 (folded 0x1cc650, the raw byte)
    virtual uint32_t ByteGate120(int32_t mode) = 0;                                                // 70
    virtual void SetAge(uint32_t code) = 0;                                                        // 71
    virtual uint32_t AgeMask() = 0;                                                                // 72
    virtual IdVector *GetIds() = 0;                                                                // 73 (folded 0x4a6cf0)
    virtual void StoreBlockC8(const void *src) = 0;                                                // 74
    virtual const void *GetBlockC8() = 0;                                                          // 75 (folded 0x931190)
    virtual void ExportPointDeltas(void *source, void *target) = 0;                                // 76
    virtual void ExportPointPositions(void *source, void *target) = 0;                             // 77
    virtual void ExportPositionsByUv(void *source, void *target, uint32_t mode) = 0;               // 78
    virtual void Unused79(uint32_t a, uint32_t b, uint32_t c, uint32_t d) = 0;                     // 79 (folded 0x16a930, ret 0x10)
    virtual bool AverageWeightedOffset(uint32_t kind, uint32_t key, uint32_t id, uint32_t morph, float *out) = 0; // 80
    virtual uint8_t ApplyToChildren(uint32_t a, uint32_t b, void *target) = 0;                     // 81
    virtual bool ApplyBoneAdjusts(uint32_t kind, uint32_t key, uint32_t id, void *target) = 0;     // 82
    virtual bool GetNodeTransform(uint32_t a, uint32_t b, const char *name, void *position, void *rotation) = 0; // 83
    virtual bool ExportFaceObj(const char *path, uint32_t kind, uint32_t key) = 0;                 // 84
    virtual void ReportVertexIds(const char *name, void *out) = 0;                                 // 85
    virtual void ReportGroupVertexIds(const char *name, uint32_t group, void *out) = 0;            // 86
    virtual void ReportRegionMasks(const char *regions, void *out) = 0;                            // 87
    virtual void ReportBones(void *out) = 0;                                                       // 88
};

// The whole system object (0x168 bytes), in the standalone shape (docs/standalone-shape.md, stage A): a real class on
// rz::cRZSystemService (+0 cIGZSystemService, vftable 0xeba13c, 15 slots with the class's 13 and 14; +4 cRZUnknownMT,
// 0xeba17c) and cITSCASBodyPartSystem (+0x1c, 0xeba198), with the compiler's vtables and thunks. The interface's slots
// forward to the hook-shaped bodies of BodyPartSystemIface (`this` = the +0x1c face, as the original's); the +0 face's own
// slots are the class's members. The creator 0xcebc20 (registered by address by cTSCASCOMDirector) is the only caller of the
// constructor 0xcddf40; the vtables are referenced only by the constructor and the destructor 0xcdfb50. With the two hooks on,
// every system carries our vtables.
class cTSCASBodyPartSystem : public rz::cRZSystemService, public cITSCASBodyPartSystem {
public:
    static constexpr uint32_t kVTable = 0xeba13c, kVTable4 = 0xeba17c, kIfaceVTable = 0xeba198;
    static constexpr uint32_t kServiceId = 0xeb7d6513;
    static constexpr int32_t kPriority = -3000000; // 0xffd23940

    // 0xcddf40: cRZSystemService(0xeb7d6513, -3000000); the embedded maps (+0x4c, the four per-age maps, +0x74, +0xa0) and
    // vectors empty, the name empty, the blocks zeroed and the fields set (+0x12c 6, +0x130 2, +0x138 0x3f, +0x13c 1, +0x145
    // 1; +0x124 left alone); the locked-object manager (0xcfdf50) built and +0x158 null; the heap maps (+0x20, +0x28, +0x2c,
    // +0x30, +0x3c, +0x44, +0x48), the record vector and the two stacks (+0x24, +0x34, +0x38) and the float name map (+0x40)
    // new; the three filters built (+0x15c, +0x160, +0x164, each AddRef'd); then InitCategoryLists, InitTargetSurfaceNames,
    // the face's slots 10 and 9 (direct calls) and ResetKeys.
    cTSCASBodyPartSystem();
    // 0xcdfb50 (deleting 0xce0970, +4 thunk 0xce08c2): the face's slots 10 and 9 (direct), the heap containers deleted (+0x20,
    // +0x24, +0x28, the float name map's deleting destructor, +0x2c, +0x30, +0x34, +0x38, +0x3c, +0x44, +0x48), the filters
    // released (+0x164, +0x160, +0x15c), +0x158 deleted, the locked-object manager's destructor (0xcfe450), then the members
    // from the name down to +0x4c and the service.
    ~cTSCASBodyPartSystem() override;
    cTSCASBodyPartSystem(const cTSCASBodyPartSystem &) = delete;
    cTSCASBodyPartSystem &operator=(const cTSCASBodyPartSystem &) = delete;
    static cTSCASBodyPartSystem *__cdecl Create(); // 0xcebc20: a new system (null when new failed)

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xcf5f00, ret 8: cITSCASBodyPartSystem -> the +0x1c face; thunks 0xcf5f31 (+4), 0xcf5f39 (+0x1c)
    uint32_t AddRef() override;                              // folded 0x1cc320 (the service's); ITS thunk 0x1cc32d
    uint32_t Release() override;                             // folded 0x1cd7a0 (the service's); ITS thunk 0x1cd7ad

    // ---- cIGZSystemService ----
    bool Init() override;     // 0xcf3580: factory, cheats, persist registrations; slot 4 of the face
    bool Shutdown() override; // 0xcfa200: slots 10 and 9 of the face, records and maps emptied

    // ---- the +0 face's own slots (lane JY) ----
    // Slot 13, 0xcf6ff0, ret 0: erases every record of kind 0, 1, 2 or 7 from the modifier stack (the vector's erase:
    // later records assigned down with the record assignment 0xce01a0, the last destroyed), then 0xcebb70.
    virtual void RemoveMorphRecords();
    // Slot 14, 0xcf43f0, ret 0xc: the part resource for (kind, key, flag) from the cache at +0x4c (key: flag byte, kind (7
    // as 3) and the low bit of the key when the flag is 0), loaded on a miss: 0xcf18b0 names it, "_cres" is appended, the
    // key parsed (type 0xe519c933) and the persist service's slot 5 asked for interface 0x4512dd13.
    virtual void *GetPartResource(uint32_t kind, uint32_t key, uint32_t flag);

    // ---- cITSCASBodyPartSystem (+0x1c): each forwards to the body of the same name on Iface() ----
    void ClearData(uint32_t flags) override;
    void LoadFaceData() override;
    void ImportPartData(void *source, const char *name) override;
    void EraseAgeName(const char *name) override;
    bool HasAgeName(const char *name) override;
    bool HasActiveMorph(const char *name) override;
    void ResetState() override;
    void ClearTreeAt14() override;
    const char *GetText() override;
    void SaveSnapshot(int32_t id) override;
    bool RestoreSnapshot(int32_t id) override;
    void PopEntry(uint32_t moveToRedo) override;
    void RedoEntry() override;
    void ClearStacks() override;
    bool ClearPartWeight(int32_t value, int32_t category) override;
    int32_t StackCount() override;
    int32_t RedoStackCount() override;
    void RefreshPart(uint32_t a, uint32_t b, uint32_t id) override;
    void VisitIds(uint32_t a, uint32_t b) override;
    void ApplyAll(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) override;
    void ApplyAllPasses(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4) override;
    void ApplyPart(uint32_t kind, uint32_t key, uint32_t id, uint8_t allKinds, uint8_t allKeys, uint8_t allIds, uint8_t rebuild,
                   uint8_t refresh, uint8_t firstPass, uint8_t resetIndex) override;
    bool RebuildPartMaterials(uint32_t kind, uint32_t key, uint32_t materialArg, uint8_t applyToParts) override;
    uint32_t CreateSkinMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint8_t eyeGizmo) override;
    uint32_t CreateHairMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint32_t a, uint32_t b,
                                uint32_t species, uint8_t hair) override;
    void ApplyOutfitProperties(uint32_t id, uint8_t reset) override;
    void CopyFrom(void *source, uint8_t resetBlocks, uint8_t copyWord, uint8_t copyKeys, uint8_t copyNamed, uint8_t copyItems,
                  uint8_t copyNames5) override;
    uint8_t ApplyToAllParts(void *target) override;
    void RefreshGeometry(uint32_t kind, uint32_t key, uint32_t id, uint32_t partId) override;
    void PushMarker() override;
    void PushKind5(const void *items) override;
    bool ItemsMatchNewestKind5(const BodyPartItemVector *items) override;
    void PushKind6(const NamedWeight *source) override;
    bool MatchesNewestKind6(const NamedWeight *named) override;
    void ApplyPartGuid(const uint32_t *guid, uint8_t on, uint8_t asNew) override;
    void ApplyGuidText(const char *text, uint32_t a, uint32_t b) override;
    void ClearGuidPartWeights(const uint32_t *guid) override;
    uint32_t IsGuidApplied(uint32_t a, uint32_t b, const char *guidText) override;
    void ApplyModifierTexts(const StringVector *texts) override;
    void ExportModifierTexts(StringVector *out) override;
    void ApplyNamesValue0B(const StringVector *names) override;
    void CollectNamesValue0B(StringVector *out) override;
    void ApplyNamesValue5(const StringVector *names) override;
    void CollectNamesValue5(StringVector *out) override;
    void PushKind0(const char *name) override;
    void AddMorph(const char *name, float weight, uint8_t merge) override;
    void AddNamedWeight(const char *name, float weight) override;
    float SumNamedWeights(const char *name, uint32_t unused) override;
    float TopMorphWeight(const char *name) override;
    void *GetPart(uint32_t a, uint32_t b, uint32_t id) override;
    void CollectLinkedParts(uint32_t a, uint32_t b, uint32_t id, ::ServiceRefVector *objects, IdVector *values,
                            GuidVector *guids) override;
    void StoreBlockB8(const void *src) override;
    const void *GetBlockB8() override { return mGuids[0]; } // folded 0x418bd0: lea eax, [ecx + 0xb8]
    void StoreBlockD8(const void *src) override;
    const void *GetBlockD8() override { return mGuids[2]; } // folded 0x60d700: lea eax, [ecx + 0xd8]
    void StoreKey(uint32_t index, const void *key) override;
    const void *GetKey(uint32_t index) override;
    void StoreDword108(uint32_t value) override;
    uint32_t GetDword108() override { return mDword124; } // folded 0x418b60: mov eax, [ecx + 0x108]
    void StoreByte10C(uint8_t value) override;
    void CheckRegionPairs(void *outfitA, void *outfitB, const ServiceStringVector *flags, ServiceStringVector *out) override;
    void VisitIdsF7(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) override;
    void VisitIdsEA(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6) override;
    void CollectRecordTexts(ServiceStringVector *out) override;
    void StoreByte129(uint8_t value) override;
    void SetByte120(uint8_t value) override { mByte13C = value; } // folded 0x4e7f40: the byte at [ecx + 0x120]
    uint8_t GetByte120() override { return mByte13C; }            // folded 0x1cc650: mov al, [ecx + 0x120]
    uint32_t ByteGate120(int32_t mode) override;
    void SetAge(uint32_t code) override;
    uint32_t AgeMask() override;
    IdVector *GetIds() override { return &mListC; } // folded 0x4a6cf0: lea eax, [ecx + 0x78]
    void StoreBlockC8(const void *src) override;
    const void *GetBlockC8() override { return mGuids[1]; } // folded 0x931190: lea eax, [ecx + 0xc8]
    void ExportPointDeltas(void *source, void *target) override;
    void ExportPointPositions(void *source, void *target) override;
    void ExportPositionsByUv(void *source, void *target, uint32_t mode) override;
    void Unused79(uint32_t, uint32_t, uint32_t, uint32_t) override {} // folded 0x16a930: ret 0x10
    bool AverageWeightedOffset(uint32_t kind, uint32_t key, uint32_t id, uint32_t morph, float *out) override;
    uint8_t ApplyToChildren(uint32_t a, uint32_t b, void *target) override;
    bool ApplyBoneAdjusts(uint32_t kind, uint32_t key, uint32_t id, void *target) override;
    bool GetNodeTransform(uint32_t a, uint32_t b, const char *name, void *position, void *rotation) override;
    bool ExportFaceObj(const char *path, uint32_t kind, uint32_t key) override;
    void ReportVertexIds(const char *name, void *out) override;
    void ReportGroupVertexIds(const char *name, uint32_t group, void *out) override;
    void ReportRegionMasks(const char *regions, void *out) override;
    void ReportBones(void *out) override;

    // The +0x1c subobject as the hook-shaped bodies take it.
    BodyPartSystemIface *Iface() { return reinterpret_cast<BodyPartSystemIface *>(reinterpret_cast<uint8_t *>(this) + 0x1c); }

    // +0x00 .. +0x1b: rz::cRZSystemService; +0x1c: the cITSCASBodyPartSystem vtable
    struct TreeHeader *mTree20;        // +0x20  heap map, 0x30-byte nodes: std::string key + an inner map (erase 0xcdbf10)
    std::vector<SurfaceRecord, CasAlloc<SurfaceRecord>> *mRecords; // +0x24  heap vector of 0x60-byte records
    struct TreeHeader *mTree28;        // +0x28  heap map, 0x24-byte nodes (erase 0xcdb5e0)
    struct TreeHeader *mTree2C;        // +0x2c  heap map, 0x1c-byte nodes (erase 0xcdb4a0)
    struct TreeHeader *mTree30;        // +0x30  heap map, 0x1c-byte nodes (erase 0xcdb400)
    BodyPartEntryVector *mRedoStack;   // +0x34
    BodyPartEntryVector *mStack;       // +0x38
    struct TreeHeader *mTree3C;        // +0x3c  heap map, 0x1c-byte nodes (erase 0xcdb970)
    void *mFloatNameMap;               // +0x40  nTSCASCore::cFloatNameMap (polymorphic; slot 0 the deleting destructor)
    struct TreeHeader *mTree44;        // +0x44  heap map, 0x78-byte nodes (erase 0xcdb710)
    struct TreeHeader *mTree48;        // +0x48  heap map, 0x1c-byte nodes (erase 0xcdb540)
    ResourceTree mResources;           // +0x4c  map<uint32_t, counted object> (ResourceMap): the face-0 slot 14 cache
    AgeNameTree mByAge[4];             // +0x54  four AgeNameMaps (the image's constructor 0xcdda80, destructor 0xcdea90)
    Tree74 mTree74;                    // +0x74  map, 0x38-byte nodes (erase 0xcdc300)
    IdVector mListA;                   // +0x7c  category lists filled by InitCategoryLists
    IdVector mListB;                   // +0x88
    IdVector mListC;                   // +0x94  (the ids slots 21/64/65 visit)
    SurfaceMap mSurfaceMap;            // +0xa0  surface name -> region name
    SurfaceSetVector mSurfaceSets;     // +0xa8  five name sets
    SystemName mName;                  // +0xb4
    uint8_t mGuids[5][16];            // +0xd4  blocks of slots 54/74/56 and the two keys (iface +0xb8..+0xf8)
    uint32_t mDword124;                // +0x124
    uint8_t mByte128;                  // +0x128
    uint8_t mPad129[3];
    uint32_t mDword12C;                // +0x12c  6 after construction
    uint32_t mDword130;                // +0x130  2
    uint32_t mAge;                     // +0x134  the age / species selector
    uint32_t mDword138;                // +0x138  0x3f
    uint8_t mByte13C;                  // +0x13c  1
    uint8_t mPad13D[3];
    uint32_t mDword140;                // +0x140
    uint8_t mByte144;                  // +0x144
    uint8_t mByte145;                  // +0x145  1
    uint8_t mByte146;                  // +0x146
    uint8_t mPad147;
    uint32_t mDirectorCookie;          // +0x148  handed by address to the COM director's slot 7 in Init
    cTSCASLockedObjectManager mLockedObjects; // +0x14c
    TreeHeader *mTree158;              // +0x158  heap map, null until used (0xd0a970); deleted with erase 0xd0a8e0 (0xd0a980)
    void *mSkinBaseFilter;             // +0x15c  counted: nTSCASCore::cTSSkinBaseFilter
    void *mSkinFilter;                 // +0x160  nTSCASCore::cTSSkinFilter
    void *mOverlayFilter;              // +0x164  nTSCASCore::cTSOverlayFilter

    BodyPartEntry *FindNewestKind5(); // 0xcec4a0: ret 0, the newest record of kind 5 in the modifier stack, or null
    BodyPartEntry *FindNewestKind6(); // 0xcec4d0: ret 0, the same for kind 6

    // ---- start-up helpers (called by the constructor) ----
    void InitCategoryLists();       // 0xcf8fd0: the three id lists for the current age
    void InitTargetSurfaceNames();  // 0xcf9340: five empty name sets; for a non-zero age, sets 1-3 and the surface map filled
    __declspec(noinline) void ResetKeys(); // 0xce9690 (not inlined into ResetState: tests stand in for it there): slot 58 with keys 0 and 1 set to the static default GUID
};
static_assert(sizeof(cTSCASBodyPartSystem) == 0x168, "cTSCASBodyPartSystem size");
static_assert(offsetof(cTSCASBodyPartSystem, mTree20) == 0x20, "cTSCASBodyPartSystem layout: the members after the +0x1c face");
static_assert(offsetof(cTSCASBodyPartSystem, mResources) == 0x4c && offsetof(cTSCASBodyPartSystem, mTree74) == 0x74,
              "cTSCASBodyPartSystem layout");
static_assert(offsetof(cTSCASBodyPartSystem, mTree158) == 0x158, "cTSCASBodyPartSystem layout");
static_assert(offsetof(cTSCASBodyPartSystem, mListA) == 0x7c, "cTSCASBodyPartSystem layout");
static_assert(offsetof(cTSCASBodyPartSystem, mSurfaceMap) == 0xa0, "cTSCASBodyPartSystem layout");
static_assert(offsetof(cTSCASBodyPartSystem, mName) == 0xb4, "cTSCASBodyPartSystem layout");
static_assert(offsetof(cTSCASBodyPartSystem, mAge) == 0x134, "cTSCASBodyPartSystem layout");
static_assert(offsetof(cTSCASBodyPartSystem, mDirectorCookie) == 0x148, "cTSCASBodyPartSystem layout");
static_assert(offsetof(cTSCASBodyPartSystem, mOverlayFilter) == 0x164, "cTSCASBodyPartSystem layout");
static_assert(offsetof(BodyPartSystemIface, mSnapshots) == 0x28 && offsetof(BodyPartSystemIface, mResources) == 0x30 &&
                  offsetof(BodyPartSystemIface, mByAge) == 0x38 && offsetof(BodyPartSystemIface, mListAFirst) == 0x60,
              "BodyPartSystemIface layout");

// The RVA hooks' entry points for the class's members that are virtual (docs/standalone-shape.md, step 6): `this` in ecx (the
// +0 face), the dummy edx, the stack arguments; qualified calls (no dispatch). The constructor's places our class.
struct cTSCASBodyPartSystemAdapters {
    static cTSCASBodyPartSystem *__fastcall Construct(void *memory, void *edx);                                // 0xcddf40
    static bool __fastcall QueryInterface(cTSCASBodyPartSystem *self, void *edx, uint32_t riid, void **out);  // 0xcf5f00
    static bool __fastcall Init(cTSCASBodyPartSystem *self, void *edx);                                       // 0xcf3580
    static bool __fastcall Shutdown(cTSCASBodyPartSystem *self, void *edx);                                   // 0xcfa200
    static void __fastcall RemoveMorphRecords(cTSCASBodyPartSystem *self, void *edx);                         // 0xcf6ff0
    static void *__fastcall GetPartResource(cTSCASBodyPartSystem *self, void *edx, uint32_t kind, uint32_t key,
                                            uint32_t flag);                                                   // 0xcf43f0
};

// ---- Lane JY: the views the morph and import slots use ---------------------------------------------------------------


// One 0x68-byte vertex of a part's mesh: the position slot 48 resets from the base position at +0x20.
struct PartMeshVertex {
    float mPosition[3]; // +0x00
    uint8_t mPad0C[0x10 - 0xc];
    float mOrigin[3];   // +0x10  what the point deltas of slots 76-78 (and 80) are measured from
    uint8_t mPad1C[0x20 - 0x1c];
    float mBase[3];     // +0x20
    uint8_t mPad2C[0x30 - 0x2c];
    float mNormal[3];   // +0x30  written back with the position by slot 31 (tentatively the normal)
    uint8_t mPad3C[0x40 - 0x3c];
    float mUv[2];       // +0x40  exported beside each delta (slot 76) and matched by slot 78 (tentatively texture coordinates)
    uint8_t mPad48[0x4a - 0x48];
    uint16_t mPositionIndex; // +0x4a  the position's number in slot 84's export
    uint8_t mPad4C[0x4e - 0x4c];
    uint16_t mNormalIndex;   // +0x4e  the normal's number in slot 84's export
    uint8_t mPad50[0x68 - 0x50];
};
static_assert(sizeof(PartMeshVertex) == 0x68, "PartMeshVertex layout");
static_assert(offsetof(PartMeshVertex, mOrigin) == 0x10 && offsetof(PartMeshVertex, mUv) == 0x40, "PartMeshVertex layout");
struct PartMeshGroup {
    PartMeshVertex *mFirst, *mLast, *mEnd;
};
// A 0x20-byte triangle of a part's mesh: its corners, indices into the vertex group of the same number.
struct PartMeshFace {
    uint8_t mPad00[0x14];
    uint16_t mCorners[3]; // +0x14
    uint8_t mPad1A[0x20 - 0x1a];
};
static_assert(sizeof(PartMeshFace) == 0x20 && offsetof(PartMeshFace, mCorners) == 0x14, "PartMeshFace layout");
struct PartMeshFaceGroup {
    PartMeshFace *mFirst, *mLast, *mEnd;
};
// A part's mesh entry (what 0xcf04b0 and 0xcf06e0 hand out), as far as the morph slots read it.
struct PartMeshEntry {
    PartMeshGroup *mGroupsFirst; // +0x00  vertex groups
    PartMeshGroup *mGroupsLast;  // +0x04
    uint8_t mPad08[0xc - 0x8];
    PartMeshFaceGroup *mFaceGroupsFirst; // +0x0c  one face group per vertex group
    PartMeshFaceGroup *mFaceGroupsLast;  // +0x10
    uint8_t mPad14[0x50 - 0x14];
    int32_t mStackIndex;         // +0x50  the record count it was built for (-1 stale)
    uint8_t mEnabled;            // +0x54
    uint8_t mPad55[3];
    uint32_t mKind;              // +0x58  compared with +0x110
    uint32_t mKey;               // +0x5c  compared with +0x114
};
static_assert(offsetof(PartMeshEntry, mStackIndex) == 0x50 && offsetof(PartMeshEntry, mKey) == 0x5c, "PartMeshEntry layout");

// 12-byte points (slot 5 copies them between a source and a part entry's vector at +0x40, resized with 0xcfd180).
struct PartPoint {
    float mXyz[3];
};
struct PartPoints {
    PartPoint *mFirst, *mLast, *mEnd;
};

// A counted reference held by the face-0 slot 14 cache (released through slot 2 when the cache's erase 0xcdbc10 frees
// the node).
struct ResourceRef {
    void *mObject = nullptr;
};
using ResourceMap = std::map<uint32_t, ResourceRef, std::less<uint32_t>, CasAlloc<std::pair<const uint32_t, ResourceRef>>>; // 0x18-byte nodes
static_assert(sizeof(ResourceMap) == 8, "ResourceMap layout");

// A cRZString on the stack as the original builds it inline (game vtable, empty text, refcount 0), destroyed with
// cRZString's destructor (0x141193).
struct LocalRZString {
    union {
        cRZString mText;
    };
    LocalRZString()
    {
        ::new (&mText.mString) std::string();
        mText.mVTable = GameAddress<void>(cRZString::kVTable);
        mText.mRefCount = 0;
    }
    explicit LocalRZString(const char *text)
    {
        ::new (&mText.mString) std::string(text);
        mText.mVTable = GameAddress<void>(cRZString::kVTable);
        mText.mRefCount = 0;
    }
    LocalRZString(const LocalRZString &) = delete;
    ~LocalRZString() { mText.Destruct(); }
};

// ---- Lane KA: the part materials (iface +0x2c) ----------------------------------------------------------------------

// A node of a game-built MSVC tree (left, parent, right, colour, isnil, the value at +0x10) whose key and order are not
// known here: only walked in order.
template <typename T> struct GameTreeNode {
    GameTreeNode *mLeft;   // +0x00
    GameTreeNode *mParent; // +0x04
    GameTreeNode *mRight;  // +0x08
    uint8_t mColor;        // +0x0c
    uint8_t mIsNil;        // +0x0d
    uint8_t mPad0E[2];
    T mValue;              // +0x10
    // The in-order successor (the tree iterator's ++, 0x15dc50).
    GameTreeNode *Next()
    {
        GameTreeNode *node = this;
        if (!node->mRight->mIsNil) {
            node = node->mRight;
            while (!node->mLeft->mIsNil) node = node->mLeft;
            return node;
        }
        GameTreeNode *parent = node->mParent;
        while (!parent->mIsNil && node == parent->mRight) {
            node = parent;
            parent = parent->mParent;
        }
        return parent;
    }
};
template <typename T> struct GameTree {
    GameTreeNode<T> *mHead;
    uint32_t mSize;
    GameTreeNode<T> *First() const { return mHead->mLeft; }
    GameTreeNode<T> *End() const { return mHead; }
};

// What the material manager's slot 33 (0xacc410, the queried object of GetCachedQueriedObject) fills in for a key: all
// 0x14 bytes are written (zeroed first), so the out must be this whole size. The property set is the second word.
struct MaterialEntryRef {
    void *mFirst;      // +0x00 the entry's slot 19
    void *mProperties; // +0x04
    uint32_t mRest[3]; // +0x08
};
static_assert(sizeof(MaterialEntryRef) == 0x14, "MaterialEntryRef");

// One subset that uses a part material (a value of PartMaterial::mUsers): the part (kind, key, id) whose subset it is,
// the subset's name, and the GUID of the linked object it belongs to (the zero key for the part itself).
struct PartMaterialUser {
    uint32_t mKind;     // +0x00
    uint32_t mKey;      // +0x04
    uint32_t mId;       // +0x08
    cRZString mSubset;  // +0x0c
    CasGuid mGuid;      // +0x2c
};
static_assert(sizeof(PartMaterialUser) == 0x3c, "PartMaterialUser layout");

// One material of a part (0x54 bytes; destructor 0xcdf550): its current name, whether it is rebuilt (slot 25), the
// subsets that use it, and further state 0xcfb1d0 reads (+0x24 a level that must be above 3 to use the text at +0x34).
struct PartMaterial {
    cRZString mName;                     // +0x00  renamed after each rebuild
    uint32_t mRebuildable;               // +0x20  slot 25 rebuilds only these
    int32_t mLevel;                      // +0x24
    uint32_t mField28;                   // +0x28
    GameTree<PartMaterialUser> mUsers;   // +0x2c  (erase 0xcdc480)
    cRZString mText34;                   // +0x34
    PartMaterial(const PartMaterial &) = delete;
    ~PartMaterial(); // as 0xcdf550: +0x34's text, the users (the image's erase, then the head), the name
};
static_assert(sizeof(PartMaterial) == 0x54 && offsetof(PartMaterial, mUsers) == 0x2c, "PartMaterial layout");

// A part's materials (0x68-byte nodes, head from 0xcfcff0; their key's meaning and order are not known here, nothing of
// ours inserts) and the table of them by part key (PartTableKey; 0x1c-byte nodes,
// operator[] 0xce0330, erase 0xcdb540).
using PartMaterialMap = std::multimap<uint32_t, PartMaterial, std::less<uint32_t>, CasAlloc<std::pair<const uint32_t, PartMaterial>>>;
struct PartMaterialTable : std::map<int32_t, PartMaterialMap, std::less<int32_t>, CasAlloc<std::pair<const int32_t, PartMaterialMap>>> {};
static_assert(sizeof(PartMaterialMap) == 8 && sizeof(PartMaterialTable) == 8, "part material containers");

} // namespace nTSCASCore
