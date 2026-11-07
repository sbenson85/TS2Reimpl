#include "rz/PathFileOps.h"
#include "ts/cTSPersistSystem.h"
#include "app/SpecialFolders.h"
#include "game/vtable.h"
#include "gz/cGZPersistResourceManager.h"
#include "gz/cGZPersistCacheStrategyBasic.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZLock.h"
#include "rz/CountedRefVector.h" // AppendCounted
#include "rz/GameAllocator.h" // GZServiceAllocator
#include "std/StdHash.h"
#include "ts/cTSPersistDBRecord.h"
#include "ts/cRAMShadowRecord.h"
#include "ts/ChangeRecordSet.h"
#include "ts/DirectoryListing.h"
#include "game/game.h"
#include "game/original.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/PersistSystemListener.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZDBSegmentPackedFile.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZPersistCacheStrategy.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistDBSegmentDirectoryFiles.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cIGZPersistResourceFactory.h"
#include "ifaces/cIGZPersistResourceKeyFilter.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cIGZSerializable.h"
#include "ifaces/cIGZThread.h"
#include "ifaces/cITSObjectFileRef.h"
#include "ifaces/cITSPersistDBRecord.h"
#include "ifaces/cITSPersistDBSegment.h"
#include "ifaces/cITSPersistDBSegmentInfo.h"
#include "ifaces/cITSPersistDBSegmentWithRollback.h"
#include "ifaces/cITSPersistSpeculativeLoader.h"
#include "ifaces/cITSPersistSystem.h"
#include "ifaces/cTSAnimTable.h"
#include "ifaces/cTSPersistSystem.h"
#include "misc/UtilitiesC.h"
#include "rz/StaticGetters.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StringKeyParser.h"
#include "misc/HexParse.h"
#include "rz/cRZFileName.h"
#include "rz/cRZStopwatch.h"
#include "rz/PathAttributes.h"
#include "rz/cRZString.h"
#include "rz/cRZThread.h"
#include "rz/cRZVariant.h"
#include "simantics/ServiceRefVector.h"
#include "simantics/cTSBehaviorTree.h"

#include "app/AppCtor.h"
#include "rz/StopwatchMore.h"

#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "rz/cGZAllocatorService.h"
#include "rz/nGZLog.h"
#include "rz/cRZFile.h"
#include "app/AppServices2.h"
#include "simantics/PrimAACommon.h"
#include "app/ReportBlockers.h"

#include <cctype>
#include <cstring>

#include <string>

#include <algorithm>
#include <new>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <unordered_map>

#include <cstdint>

namespace {

using Segment = ifaces::cIGZPersistDBSegment;

// Interface ids this class asks segments and records for.
constexpr uint32_t kIidPackedFile = 0x286d4a6a;      // cIGZDBSegmentPackedFile
constexpr uint32_t kIidDirectoryFiles = 0x856c8cf7;  // cIGZPersistDBSegmentDirectoryFiles
constexpr uint32_t kIidKeyStatus = 0x3b2271bb;       // cITSPersistDBSegment
constexpr uint32_t kIidRollback = 0x8ceec300;        // cITSPersistDBSegmentWithRollback
constexpr uint32_t kIidRecordCopy = 0x456b8f1d;      // the id Primary::vf53 asks the factory's resource for
constexpr uint32_t kKeyTypeUseA3 = 0x1c0532fa;       // a key whose [1] is this takes its group from [0]

// A pointer the original passes as a 32-bit word (the public signatures keep the word).
uint32_t Word(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

template <typename T> T *Ptr(uint32_t word)
{
    return reinterpret_cast<T *>(static_cast<uintptr_t>(word));
}

const ::cGZPersistResourceKey *KeyOf(const uint32_t *key)
{
    return reinterpret_cast<const ::cGZPersistResourceKey *>(key);
}

// A byte returned as the interface's bool without normalising it: the original passes a callee's al through as it is
// (0x8c stays 0x8c; callers test al). The shared interfaces (cIGZSystemService) declare bool.
bool RawBool(uint8_t value)
{
    bool b;
    std::memcpy(&b, &value, 1);
    return b;
}

// The game's hash tables in the object are cGZPersistHashMap; the lookups use the StdHashTable view of them.
StdHashTable &HashTable(cGZPersistHashMap &map)
{
    return reinterpret_cast<StdHashTable &>(map);
}


// FNV-1a over the four bytes of the key, least significant first (the hash of the id tables).
uint32_t Fnv1a32(uint32_t key)
{
    uint32_t hash = 0x811c9dc5u;
    for (int i = 0; i < 4; ++i) {
        hash ^= (key >> (8 * i)) & 0xffu;
        hash *= 0x01000193u;
    }
    return hash;
}

// AddRef and Release through cIGZUnknown for objects of an unnamed interface (the hand-written global one).
void ReleaseObject(void *p)
{
    static_cast<cIGZUnknown *>(p)->Release();
}

void AddRefIf(void *p)
{
    if (p) static_cast<cIGZUnknown *>(p)->AddRef();
}

void ReleaseIf(void *p)
{
    if (p) static_cast<cIGZUnknown *>(p)->Release();
}

// Releases the object when the scope ends, if there is one.
template <typename T> class ReleaseScope {
public:
    explicit ReleaseScope(T *&p) : mP(p) {}
    ~ReleaseScope() { ReleaseIf(mP); }
    ReleaseScope(const ReleaseScope &) = delete;
    ReleaseScope &operator=(const ReleaseScope &) = delete;

private:
    T *&mP;
};

// QueryInterface through cIGZUnknown, slot 0 (the result is al).
template <typename T> uint8_t Query(const void *object, uint32_t iid, T **out)
{
    return const_cast<cIGZUnknown *>(static_cast<const cIGZUnknown *>(object))->QueryInterface(iid, reinterpret_cast<void **>(out));
}

// Enters a critical section and leaves it when the scope ends, as the original's unwind does.
class LockScope {
public:
    explicit LockScope(ifaces::cIGZCriticalSection *lock) : mLock(lock) { lock->Lock(); }
    ~LockScope() { mLock->Unlock(); }
    LockScope(const LockScope &) = delete;
    LockScope &operator=(const LockScope &) = delete;

private:
    ifaces::cIGZCriticalSection *mLock;
};

// The 16-byte block of zeros that events 9 and 10 carry (a fresh local each time in the original).
struct EventBlock {
    uint32_t words[4] = {0, 0, 0, 0};
    uint32_t Address() const { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(words)); }
};

// A std::vector's three pointers where the element type does not matter (the caller's output vectors).
struct RawVector {
    void **first;
    void **last;
    void **end;
};

// A std::vector<cIGZPersistDBSegment *> (first, last, end): the segments of one group.
struct SegmentVector {
    Segment **first;
    Segment **last;
    Segment **end;
};

// A node of mSegmentsByGroup (0x18 bytes): link, group at +8, the segments of that group at +0xc.
struct SegmentGroupNode : StdHashNode {
    uint32_t group;
    SegmentVector segments;
};
static_assert(sizeof(SegmentGroupNode) == 0x18, "segments-by-group node");

// A range of dwords (first, last): the value of the id table mSegmentIdRanges.
struct DwordRange {
    uint32_t *first;
    uint32_t *last;
};

// A node of mSegmentIdRanges (0x10 bytes): link, segment id at +8, the id range at +0xc.
struct SegmentIdNode : StdHashNode {
    uint32_t segmentId;
    DwordRange *range;
};
static_assert(sizeof(SegmentIdNode) == 0x10, "segment id node");

// A node of mKeyedObjects (0x10 bytes): link, key at +8, the counted object at +0xc.
struct KeyedObjectNode : StdHashNode {
    uint32_t key;
    ::cIGZUnknown *object;
};
static_assert(sizeof(KeyedObjectNode) == 0x10, "keyed object node");

// A node of mChangeRecords (0x30 bytes): link, group at +8, the change records (built by 0x1f6de0) at +0xc.
struct ChangeRecordNode : StdHashNode {
    uint32_t group;
    uint8_t records[0x24];
};
static_assert(sizeof(ChangeRecordNode) == 0x30, "change record node");

// The table 0x1d7970, 0x1d75b0 and similar emplace functions return their {node, inserted} pair through a hidden pointer.
template <typename Node> struct EmplaceResult {
    Node *node;
    bool inserted;
};

// Finds `group` (identity hash) in the table; the node's segment list, or null when there is no node.
SegmentVector *FindSegments(StdHashTable &table, uint32_t group)
{
    StdHashFindResult found{};
    table.Find32(&found, &group, group);
    StdHashNode *node = found.node ? found.node : table.mHead;
    return node == table.mHead ? nullptr : &static_cast<SegmentGroupNode *>(node)->segments;
}

// cTSPersistSystem::FillInSegments (0x1e95c0): adds the node for `group` (gathers its segments through the primary's
// slot 37 and stable-sorts them) and returns its list.
SegmentVector *FillInSegments(cTSPersistSystem *system, uint32_t group)
{
    return static_cast<SegmentVector *>(system->FillInSegments(group));
}

SegmentVector *LookupSegments(cTSPersistSystem *system, uint32_t group)
{
    SegmentVector *list = FindSegments(HashTable(system->mSegmentsByGroup), group);
    return list ? list : FillInSegments(system, group);
}

// The count of 16-byte elements between a vector's first and last (an arithmetic shift, as the original).
uint32_t ElementCount16(const RawVector &vector)
{
    return static_cast<uint32_t>(static_cast<int32_t>(Word(vector.last) - Word(vector.first)) >> 4);
}

// The primary's dirty-record flush (cTSPersistSystem::SaveChangeRecords, 0x1dedf0); 0x1c95ec (the stopwatch restart) is
// not replaced yet.
void FlushDirtyRecords(cTSPersistSystem *system)
{
    system->SaveChangeRecords();
}

void RestartStopwatch(cRZStopwatch *stopwatch)
{
    OriginalFunction<void(__thiscall *)(void *), 0x1c95ec>()(stopwatch);
}

// 0x1f24d0: slot 5 (no arguments) of the object whose pointer is the global at 0x10cc140. That object's interface is
// not identified, so the raw call.
void FlushGlobal()
{
    void *obj = *GameAddress<void *>(0x10cc140);
    VirtualCall<void(__thiscall *)(void *), 5>(obj);
}

// The duplicate-ignoring key filter vf19 creates (cTSPersistResourceKeyFilterIgnoreDuplicates, 0x1c bytes; its
// methods are the game's, through the vtable at 0xda62d4).
struct KeyFilterIgnoreDuplicates {
    static constexpr uint32_t kVTable = 0xda62d4;
    const void *mVTable;  // +0x00
    uint32_t mRefCount;   // +0x04
    void *mSetHead;       // +0x08 std::set head node (0x20 bytes)
    uint32_t mSetSize;    // +0x0c
    uint32_t mValue;      // +0x10
    uint32_t mType;       // +0x14 (not written by the constructor; the segment listings store the type)
    uint16_t mFlag;       // +0x18 (word 0, then the byte set to 1)
    uint16_t mPad1a;
};
static_assert(sizeof(KeyFilterIgnoreDuplicates) == 0x1c, "key filter layout");

// An empty MSVC std::set head node: left, parent and right point at itself; colour black and isnil set.
struct SetHeadNode {
    void *left;
    void *parent;
    void *right;
    uint8_t color;
    uint8_t isnil;
    uint8_t pad[0x20 - 0xe];
};
static_assert(sizeof(SetHeadNode) == 0x20, "set head node size");

// A new key filter as the constructor 0x1d8b10 leaves it (vf19 has the same code inline): reference count 0,
// the game's vtable, an empty set with a fresh head node, the flag word 0; +0x10 and +0x14 are not written.
// Null when the allocation fails.
KeyFilterIgnoreDuplicates *NewKeyFilter()
{
    auto *filter = static_cast<KeyFilterIgnoreDuplicates *>(::operator new(sizeof(KeyFilterIgnoreDuplicates)));
    if (filter) {
        filter->mRefCount = 0;
        filter->mVTable = GameAddress<void>(KeyFilterIgnoreDuplicates::kVTable);
        filter->mSetHead = nullptr;
        filter->mSetSize = 0;
        auto *head = static_cast<SetHeadNode *>(::operator new(sizeof(SetHeadNode)));
        head->left = head->parent = head->right = head;
        head->color = 1;
        head->isnil = 1;
        filter->mSetHead = head;
        filter->mFlag = 0;
    }
    return filter;
}

} // namespace

// The class's members. The compiler passes each slot's `this` as the face that declares it (the cITSPersistSystem slots
// get primary + 0xa8, as in the original). Calls the original makes through the object's vtable go through it here too
// (virtual calls on a face, or VirtualCall where the original passes a callee's eax through), so the tests' fake vtables
// see them.

// Slot 7, 0x1e04e0: if object B is non-null, returns B's slot 9 called with (a1, a2, a3, 0, 0, a4)
// (eax as returned). Otherwise stores 0 in the byte at a4 (if a4 is non-null), then returns the
// primary's slot 5 called with (a1, a2, a3, 0, 0). Four stack arguments (ret 0x10).
uint32_t cTSPersistSystem::GetResourceSpeculative(uint32_t key, uint32_t iid, uint32_t out, void *fromPool)
{
    cTSPersistSystem *system = this;
    ifaces::cITSPersistSpeculativeLoader *loader = system->mSpeculativeLoader;
    if (loader) {
        // The loader's GetResource (slot 9, bool): the original returns the callee's eax as it is, so the raw call.
        using LoaderGetResource = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        return VirtualCall<LoaderGetResource, 9>(loader, key, iid, out, 0, 0, reinterpret_cast<uintptr_t>(fromPool));
    }
    // The byte is written before the primary's GetPrivateResource is called; fromPool is used as a pointer only here.
    if (fromPool) *static_cast<uint8_t *>(fromPool) = 0;
    return As<ifaces::cIGZPersistResourceManager>(system)->GetPrivateResource(Ptr<const ::cGZPersistResourceKey>(key), iid, Ptr<void *>(out), 0, 0);
}

// Slot 11, 0x1dff90: calls a's slot 5 (no arguments), then this's slot 10 with its result, and
// returns eax from that call. One stack argument (ret 4).
uint32_t cTSPersistSystem::GetGroupIdForPath(void *path)
{
    // The original reads this's vtable before the calls; the virtual calls read it at each call.
    const char *chars = static_cast<cIGZString *>(path)->ToChar();
    return As<ifaces::cITSPersistSystem>(ITSFace())->GetGroupIdForPathChars(chars);
}

// Slot 17, 0x1e2d60: calls the primary's slot 34 with (a1, &local). If that returns nonzero, calls
// local's slot 26 with a1. Then calls the primary's slot 10 with a1, releases local if it is
// non-null (slot 2), and returns 1 (al). One stack argument (ret 4).
uint8_t cTSPersistSystem::DeleteResource(uint32_t key)
{
    cTSPersistSystem *system = this;
    auto *resources = As<ifaces::cIGZPersistResourceManager>(system);
    const auto *resourceKey = Ptr<const ::cGZPersistResourceKey>(key);
    Segment *segment = nullptr;
    if (resources->FindSegmentWithKey(resourceKey, reinterpret_cast<void **>(&segment))) {
        segment->DeleteRecord(resourceKey);
    }
    resources->ForgetResource(resourceKey);
    ReleaseIf(segment);
    return 1;
}

// Slot 22, 0x1dda20: returns this's slot 21 called with (a1, a2, a3, a4->slot5(), a5->slot5(), a6,
// a7). The original calls a5's slot 5 first, then a4's. Seven stack arguments (ret 0x1c).
uint32_t cTSPersistSystem::OpenSegment(uint32_t iid, uint32_t out, uint32_t a3, void *description, void *path, uint32_t a6, uint32_t a7)
{
    // description and path are strings (ToChar, slot 5). path's is read before description's; the results are the
    // fourth and fifth arguments.
    const char *pathChars = static_cast<cIGZString *>(path)->ToChar();
    const char *descriptionChars = static_cast<cIGZString *>(description)->ToChar();
    // OpenSegmentFromValues (slot 21, u8) is called raw: the original returns its eax as it is.
    using OpenFromValues = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VirtualCall<OpenFromValues, 21>(ITSFace(), iid, out, a3, Word(descriptionChars), Word(pathChars), a6, a7);
}

// Slot 23, 0x1e5dc0: local = null; if this's slot 20 (0x656b8f0c, &local, a1) returns nonzero, al is
// local's slot 10. Releases local (slot 2) if non-null. Returns al. One stack argument (ret 4).
uint8_t cTSPersistSystem::FlushSegmentByInfo(uint32_t info)
{
    Segment *segment = nullptr;
    uint8_t flushed = 0;
    if (As<ifaces::cITSPersistSystem>(ITSFace())->OpenSegmentByInfo(0x656b8f0c, reinterpret_cast<void **>(&segment), Ptr<ifaces::cITSPersistDBSegmentInfo>(info))) {
        flushed = segment->Flush();
    }
    ReleaseIf(segment);
    return flushed;
}

// Slot 31, 0x1dfd40: tries a1's slot 0 with IID 0x286d4a6a (out1); on success calls out1's slot 8 with
// a2 and returns 1. Otherwise tries IID 0x856c8cf7 (out2) the same way. Releases out2, then out1, if
// non-null. Two stack arguments (ret 8). ecx is not used.
uint8_t cTSPersistSystem::GetSegmentPath(void *segmentObject, uint32_t path)
{
    ifaces::cIGZDBSegmentPackedFile *packed = nullptr;
    ifaces::cIGZPersistDBSegmentDirectoryFiles *directory = nullptr;
    uint8_t answered = 0;
    if (Query(segmentObject, kIidPackedFile, &packed)) {
        packed->GetPath(Ptr<void>(path));
        answered = 1;
    } else if (Query(segmentObject, kIidDirectoryFiles, &directory)) {
        directory->GetPath(Ptr<void>(path));
        answered = 1;
    }
    ReleaseIf(directory);
    ReleaseIf(packed);
    return answered;
}

// Slot 57, 0x1e2700: if a3's low byte is nonzero and this's slot 30 (a1, a2->[4]) returns a value whose
// low byte equals it, returns that eax. Otherwise returns this's slot 45 called with a1. Three stack
// arguments (ret 0xc). The return value is eax as left by the last call.
uint32_t cTSPersistSystem::RegisterSegmentForKey(uint32_t segment, void *key, uint32_t expected)
{
    // SegmentHasGroup (slot 30) and RegisterSegment (slot 45) are u8, but the result is the callee's eax as it is
    // (the low byte is compared with `expected` and the whole eax returned), so the raw calls.
    uint8_t want = static_cast<uint8_t>(expected);
    if (want) {
        uint32_t r = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 30>(ITSFace(), segment, static_cast<const uint32_t *>(key)[1]);
        if (static_cast<uint8_t>(r) == want) return r;
    }
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 45>(ITSFace(), segment);
}

// Slot 4, 0x1e0800: r = a2's slot 11 (no arguments). A four-dword local {a1, r, (uint16)a3, 0} is
// passed, with a4, a5, 0, 0, to the primary's cGZPersistResourceManager::vf3 (a direct call). If
// that returns a nonzero al, the result is 0. Otherwise al is the primary's slot 15 (&local); the
// result is 0xffffffff when it is nonzero, else 0xffffff9e. Five stack arguments (ret 0x14).
uint32_t cTSPersistSystem::GetResourceFromFileRef(uint32_t type, void *fileRef, uint32_t instance, uint32_t iid, uint32_t out)
{
    cTSPersistSystem *system = this;
    // The key the original passes (by address) to the primary's GetResource and then to TestForKey: type, group (the
    // file ref's), instance, resource.
    cGZPersistResourceKey key;
    key.type = type;
    key.group = As<ifaces::cITSObjectFileRef>(fileRef)->GetGroup();
    key.instance = static_cast<uint16_t>(instance); // the original zero-extends the low word (movzx)
    key.resource = 0;
    uint32_t block = Word(&key);
    uint8_t found = static_cast<uint8_t>(system->Base()->vf3(block, iid, out, 0, 0));
    if (found) return 0;
    uint8_t exists = As<ifaces::cIGZPersistResourceManager>(system)->TestForKey(&key);
    return exists ? 0xffffffffu : 0xffffff9eu;
}

// Slot 64, 0x1e0410: FNV-1a hash of the four bytes of the argument (its address is the key), looked
// up with StdHashTable::Find32 in the table at +0x1a0. Returns the node's value (+0xc), or 0 when
// the key is absent. One stack argument (ret 4).
uint32_t cTSPersistSystem::GetKeyedObject(uint32_t key)
{
    StdHashTable &table = HashTable(this->mKeyedObjects);
    StdHashFindResult found{};
    table.Find32(&found, &key, Fnv1a32(key));
    StdHashNode *node = found.node ? found.node : table.mHead;
    if (node == table.mHead) return 0;
    return Word(static_cast<KeyedObjectNode *>(node)->object);
}

// Slot 0 (QueryInterface), 0x1e2e50: ids 0x9af5e924, 0x03fa40bf and 0x2efe1873 store the subobject at
// +0xa8, +0xac or +0xb0 in *ppv and AddRef this through slot 1, returning 1. Any other id is the base
// cGZPersistResourceManager::QueryInterface (a tail call in the original). ppv is not checked.
bool cTSPersistSystem::QueryInterface(uint32_t riid, void **ppv)
{
    uint32_t offset;
    if (riid == 0x9af5e924) {
        offset = 0xa8;
    } else if (riid == 0x03fa40bf) {
        offset = 0xac;
    } else if (riid == 0x2efe1873) {
        offset = 0xb0;
    } else {
        // Other ids: the base QueryInterface, which is a tail call in the original.
        return this->Base()->QueryInterface(riid, ppv);
    }
    *ppv = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(this) + offset);
    As<ifaces::cIGZPersistResourceManager>(this)->AddRef(); // the result is not used
    return 1;
}

// Slot 56, 0x1e2d50: calls slot 21 (no arguments) of the object GetServiceObject() returns; returns
// 1 in al.
uint8_t cTSPersistSystem::vf56()
{
    void *service = GetServiceObject();
    // Slot 21 of the service object: its interface is not identified, so the raw call.
    VirtualCall<void(__thiscall *)(void *), 21>(service);
    return 1;
}

// Slot 55, 0x1e0780: local = null; if the primary's slot 34 (a1, &local) returns nonzero, al is
// local's slot 30 (a1, a2). Releases local (slot 2) if non-null. Two stack arguments (ret 8).
uint8_t cTSPersistSystem::GetRecordChangeID(uint32_t key, uint32_t changeId)
{
    const auto *resourceKey = Ptr<const ::cGZPersistResourceKey>(key);
    Segment *segment = nullptr;
    uint8_t found = 0;
    if (As<ifaces::cIGZPersistResourceManager>(this)->FindSegmentWithKey(resourceKey, reinterpret_cast<void **>(&segment))) {
        found = segment->GetRecordChangeID(resourceKey, Ptr<uint32_t>(changeId));
    }
    ReleaseIf(segment);
    return found;
}

namespace {

// The hash maps the destructor tears down are std::unordered_map of the game's STL: their destructors (0x1d9320,
// 0x1d93e0, 0x1d9380, 0x1d92c0) are the library's _Hash destructor (db/library-matches.jsonl), each ending in its node
// type's list destructor, which runs the value's destructor on every node. Ours are the same template instantiated over
// layout-identical key and value types whose destructors have the original's effects. Hashing and comparing are never
// instantiated (the maps are only destroyed here), so the hasher and comparer are empty placeholders.
template <typename Key> struct UnusedHash {
    size_t operator()(const Key &) const { return 0; }
};
template <typename Key> struct UnusedEqual {
    bool operator()(const Key &, const Key &) const { return false; }
};
template <typename Key, typename Value>
using PersistMap = std::unordered_map<Key, Value, UnusedHash<Key>, UnusedEqual<Key>>;

// A cRZString held by value (0x20 bytes): destroyed by its own destructor (0x141193, inlined in the originals:
// the base vtable stored back, the text freed).
struct StringKey {
    alignas(cRZString) unsigned char mStorage[sizeof(cRZString)];
    ~StringKey() { reinterpret_cast<cRZString *>(mStorage)->Destruct(); }
};

// A counted pointer: released (slot 2) if set.
struct CountedRef {
    void *mObject;
    ~CountedRef() { ReleaseIf(mObject); }
};

// mChangeRecords (node 0x30): group -> a ChangeRecordSet (0x24, ts/ChangeRecordSet.h). The set's destructor is 0x1f6080 (a
// jump to its map's library destructor 0x1f5f90); ours is the same template's.
using ChangeRecordMap = PersistMap<uint32_t, ::ChangeRecordSet>;
static_assert(sizeof(std::pair<const uint32_t, ::ChangeRecordSet>) + 8 == 0x30, "change-record node 0x30");

// mMap178 / mMap198 (node 0x2c): cRZString -> counted segment (at pair+0x20). Value destructor 0x1d9ab0: the segment
// released, then the string destroyed.
using StringSegmentMap = PersistMap<StringKey, CountedRef>;
static_assert(sizeof(std::pair<const StringKey, CountedRef>) + 8 == 0x2c, "string-segment node 0x2c");

// mMap154 (node 0x3c): cRZString -> two words and a vector of words (at pair+0x28). Value destructor 0x1d9a10: the
// vector freed, then the string destroyed.
struct PathEntry {
    uint32_t mWords[2];            // pair+0x20
    std::vector<uint32_t> mValues; // pair+0x28
};
using PathEntryMap = PersistMap<StringKey, PathEntry>;
static_assert(offsetof(PathEntry, mValues) == 8, "vector at pair+0x28");
static_assert(sizeof(std::pair<const StringKey, PathEntry>) + 8 == 0x3c, "path-entry node 0x3c");

// mSegmentsByGroup (node 0x18): group -> a vector of counted segments in the allocator service's memory (0xc),
// destroyed by 0x1e6a40 (game code: every segment released, the block freed through the allocator service).
struct GroupSegments {
    void *mFirst;
    void *mLast;
    void *mEnd;
    ~GroupSegments() { OriginalFunction<void(__thiscall *)(void *), 0x1e6a40>()(this); }
};
using GroupSegmentMap = PersistMap<uint32_t, GroupSegments>;
static_assert(sizeof(std::pair<const uint32_t, GroupSegments>) + 8 == 0x18, "group node 0x18");

static_assert(sizeof(ChangeRecordMap) == sizeof(cGZPersistHashMap), "unordered_map layout");
static_assert(sizeof(StringSegmentMap) == sizeof(cGZPersistHashMap), "unordered_map layout");
static_assert(sizeof(PathEntryMap) == sizeof(cGZPersistHashMap), "unordered_map layout");
static_assert(sizeof(GroupSegmentMap) == sizeof(cGZPersistHashMap), "unordered_map layout");

template <typename Map> void DestroyAs(cGZPersistHashMap &map)
{
    reinterpret_cast<Map *>(&map)->~Map();
}

} // namespace

// 0x1d9cb0 (457 B): the keyed objects (0x1d9250), the category tree (0x1d5aa0 and its head), the listeners (released), the
// change records (library map destructor 0x1d9320), the path groups (0x1d91e0), the stopwatch (nothing), the loader
// released, the resource-name tree (0x1d5930 and its head), the two segment tables (0x1d93e0), +0x154 (0x1d9380), the id
// ranges (0x15d6a0), the groups (0x1d92c0), the named map (0x1d91e0), the four caches released (+0xec first), the segment
// vector (0x1e6a40), the two type handlers released (+0xd0 first), then the base. The library map destructors are ours
// (the helpers above); the others stay originals: the keyed-object and path-group maps' (thiscall on the map) and the
// trees' erase (thiscall on the tree, the tree and its root as arguments). Defined here rather than beside the constructor
// (lane HZ): it stores our vtables, so this unit emits them, and the ITS slots hooked from here are tied to them
// (matchcheck reads our vtables from the replacement's unit).
cTSPersistSystem::~cTSPersistSystem()
{
    using MapDtor = void(__thiscall *)(void *);
    using TreeErase = void(__thiscall *)(void *, void *, void *);
    OriginalFunction<MapDtor, 0x1d9250>()(&mKeyedObjects);
    OriginalFunction<TreeErase, 0x1d5aa0>()(&mSet23CHead, &mSet23CHead, static_cast<void **>(mSet23CHead)[1]);
    ::operator delete(mSet23CHead, 0x1c);
    reinterpret_cast<RefPtrVector *>(&mListenersBegin)->Destroy();
    DestroyAs<ChangeRecordMap>(mChangeRecords);
    OriginalFunction<MapDtor, 0x1d91e0>()(&mPathGroups);
    ReleaseIf(mSpeculativeLoader);
    OriginalFunction<TreeErase, 0x1d5930>()(&mTreeHead, &mTreeHead, static_cast<void **>(mTreeHead)[1]);
    ::operator delete(mTreeHead, 0x1c);
    DestroyAs<StringSegmentMap>(mMap198);
    DestroyAs<StringSegmentMap>(mMap178);
    DestroyAs<PathEntryMap>(mMap154);
    gz::DestroyPersistU32Map(mSegmentIdRanges);
    DestroyAs<GroupSegmentMap>(mSegmentsByGroup);
    OriginalFunction<MapDtor, 0x1d91e0>()(&mNamedMap);
    ReleaseIf(mSimCache);
    ReleaseIf(mGeneralCache);
    ReleaseIf(mLongTermCache);
    ReleaseIf(mSmallObjectCache);
    OriginalFunction<MapDtor, 0x1e6a40>()(&mSegmentsBegin);
    ReleaseIf(mTypeHandler4d45f8e6);
    ReleaseIf(mTypeHandler2026960b);
}

void __fastcall cTSPersistSystemAdapters::Destruct(cTSPersistSystem *self, void *)
{
    self->cTSPersistSystem::~cTSPersistSystem();
}

// Slot 5 of cIGZSystemService, 0x1e0930: returns the constant 0x001ab3f1 (not relocated).
int32_t cTSPersistSystem::GetServicePriority()
{
    return 0x001ab3f1;
}

// Slot 3, 0x1dae50: returns the primary object (this - 0xa8).
void *cTSPersistSystem::GetResourceManager()
{
    return this;
}

// Slot 6, 0x1e3ae0: if object B is null returns 1 (al). Otherwise calls B's slot 5 with six
// arguments (a1, a4, a5, a2, a3, a3), in that order, and returns its result in al. The callee
// pops the five arguments (ret 0x14).
uint8_t cTSPersistSystem::QueueResourceLoad(uint32_t key, uint32_t priority, uint32_t lifetimeMs, uint32_t poolOnly, uint32_t followReferences)
{
    ifaces::cITSPersistSpeculativeLoader *loader = this->mSpeculativeLoader;
    if (!loader) return 1;
    // Preload (slot 5) takes the two flags as bool, but these arguments go through as the full words they arrive as
    // (the lifetime is passed twice: as lifetime and as retain), so the raw call.
    using LoaderPreload = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VirtualCall<LoaderPreload, 5>(loader, key, poolOnly, followReferences, priority, lifetimeMs, lifetimeMs);
}

// Slot 8, 0x2170: if object B is null returns 0. Otherwise tail-calls B's slot 7 with the one
// argument; returns its result in al.
uint8_t cTSPersistSystem::IsResourceAvailable(uint32_t key)
{
    ifaces::cITSPersistSpeculativeLoader *loader = this->mSpeculativeLoader;
    if (!loader) return 0;
    return loader->IsAvailable(Ptr<void>(key));
}

// Slot 9, 0x2190: as IsResourceAvailable, with B's slot 8.
uint8_t cTSPersistSystem::IsResourcePending(uint32_t key)
{
    ifaces::cITSPersistSpeculativeLoader *loader = this->mSpeculativeLoader;
    if (!loader) return 0;
    return loader->IsPending(Ptr<void>(key));
}

// Slot 41, 0x1dea20: clears the flag byte at sub+0x185.
void cTSPersistSystem::ClearDeferWrites()
{
    this->mDeferWrites = 0;
}

// The segment lock mLockB is embedded in the primary object at +0x68 (its vtable pointer is the first dword
// there), so `this - 0x40` is the lock itself, not a pointer to it.
// Slot 52, 0x1dae80: al = (object A's slot 1 returned nonzero).
bool cTSPersistSystem::LockSystem()
{
    return As<ifaces::cIGZCriticalSection>(&this->mLockB)->Lock() != 0;
}

// Slot 53, 0x1dea10: al = (object A's slot 2 returned zero).
bool cTSPersistSystem::UnlockSystem()
{
    return As<ifaces::cIGZCriticalSection>(&this->mLockB)->Unlock() == 0;
}

// Slot 68, 0x1e5e40: if object C is non-null and its slot 11 returns nonzero, calls C's slot 8
// with -1 (the field is read again for this call).
void cTSPersistSystem::StopBackgroundOpen()
{
    cTSPersistSystem *system = this;
    if (!system->mBackgroundThread) return;
    if (system->mBackgroundThread->IsRunning()) {
        // The original reads the field again for this call.
        system->mBackgroundThread->Join(0xffffffffu);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Listener notification and the slots that wrap base calls in it.

// 0x1de870 (non-virtual, ret 8): under lock D (the base's lock object at +0x98: slot 1 enter, slot 2
// leave), calls slot 3 (event, arg) of every listener in the pointer vector [+0x230, +0x234). The end is
// read once, before the loop.
void cTSPersistSystem::NotifyListeners(uint32_t event, uint32_t arg)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockD));
    ifaces::PersistSystemListener **it = system->mListenersBegin;
    ifaces::PersistSystemListener **end = system->mListenersEnd; // read once, before the loop
    for (; it != end; ++it) {
        (*it)->OnPersistEvent(event, arg);
    }
}

// Keys of these types are served by the type handlers (mTypeHandler2026960b, mTypeHandler4d45f8e6) instead of the
// base class. The handlers' interface is not identified, so their slots 7 and 8 are called raw.
namespace {
constexpr uint32_t kKeyType2026960b = 0x2026960b;
constexpr uint32_t kKeyType4d45f8e6 = 0x4d45f8e6;
} // namespace

// Slots 3, 4 and 5 (0x1e0890, 0x1e08e0, 0x1e04a0): resource keys whose type (*key) is 0x2026960b go to
// the object at +0xcc, type 0x4d45f8e6 to the object at +0xd0, and other types to the base
// cGZPersistResourceManager slot. vf3 and vf4 call the object's slot 7 and vf5 its slot 8, each with
// (key, a2, a3, a4, a5); vf4 drops its sixth argument on that path. No null checks.
uint32_t cTSPersistSystem::vf3(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    cTSPersistSystem *system = this;
    ::cIGZUnknown *handler;
    if (*Ptr<const uint32_t>(key) == kKeyType2026960b) {
        handler = system->mTypeHandler2026960b;
    } else if (*Ptr<const uint32_t>(key) == kKeyType4d45f8e6) {
        handler = system->mTypeHandler4d45f8e6;
    } else {
        return system->Base()->vf3(key, a2, a3, a4, a5);
    }
    using HandlerSlot7 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VirtualCall<HandlerSlot7, 7>(handler, key, a2, a3, a4, a5);
}

uint8_t cTSPersistSystem::vf4(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    cTSPersistSystem *system = this;
    ::cIGZUnknown *handler;
    if (*Ptr<const uint32_t>(key) == kKeyType2026960b) {
        handler = system->mTypeHandler2026960b;
    } else if (*Ptr<const uint32_t>(key) == kKeyType4d45f8e6) {
        handler = system->mTypeHandler4d45f8e6;
    } else {
        return system->Base()->vf4(key, a2, a3, a4, a5, a6);
    }
    using HandlerSlot7 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VirtualCall<HandlerSlot7, 7>(handler, key, a2, a3, a4, a5);
}

uint8_t cTSPersistSystem::vf5(uint32_t key, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    cTSPersistSystem *system = this;
    ::cIGZUnknown *handler;
    if (*Ptr<const uint32_t>(key) == kKeyType2026960b) {
        handler = system->mTypeHandler2026960b;
    } else if (*Ptr<const uint32_t>(key) == kKeyType4d45f8e6) {
        handler = system->mTypeHandler4d45f8e6;
    } else {
        return system->Base()->vf5(key, a2, a3, a4, a5);
    }
    using HandlerSlot8 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    return VirtualCall<HandlerSlot8, 8>(handler, key, a2, a3, a4, a5);
}

// Slot 49, 0x1dfd00 (ret 0xc): NotifyListeners(6, a1), the base vf49(a1, a2, a3), then
// NotifyListeners(7 when it returned nonzero, else 8, a1). Returns the base's al.
uint8_t cTSPersistSystem::vf49(uint32_t a1, uint32_t a2, uint32_t a3)
{
    NotifyListeners(6, a1);
    uint8_t r = this->Base()->vf49(a1, a2, a3);
    NotifyListeners((r ^ 1u) + 7, a1);
    return r;
}

// Slot 52, 0x1dd9d0 (ret 0x18): NotifyListeners(2, a1), the base vf52(a1..a6), NotifyListeners(3, a1).
uint8_t cTSPersistSystem::vf52(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    NotifyListeners(2, a1);
    uint8_t r = this->Base()->vf52(a1, a2, a3, a4, a5, a6);
    NotifyListeners(3, a1);
    return r;
}

// Slot 54, 0x1defd0: tail-calls slot 11 (no arguments) of object B (+0x1c0) when it is non-null.
void cTSPersistSystem::ClearSpeculativeLoader()
{
    ifaces::cITSPersistSpeculativeLoader *loader = this->mSpeculativeLoader;
    if (loader) loader->Clear();
}

// Slot 11, 0x1e2740 (ret 4): NotifyListeners(9, &zero block), the base OnIdle(a1) (this = +4),
// NotifyListeners(10, &zero block). Returns the base's al.
bool cTSPersistSystem::OnIdle(uint32_t a1)
{
    cTSPersistSystem *primary = this;
    EventBlock before;
    primary->NotifyListeners(9, before.Address());
    // The base OnIdle takes the service subobject (+4) as `this`, as here.
    uint8_t r = reinterpret_cast<::cGZPersistResourceManager *>(ServiceFace())->OnIdle(a1);
    EventBlock after;
    primary->NotifyListeners(10, after.Address());
    return RawBool(r); // the base's byte as it is
}

// Slot 5, 0x1e08cf: the thunk to the primary's vf3 (this - 0xa8). Five stack arguments.
uint32_t cTSPersistSystem::GetResource(const uint32_t *key, uint32_t iid, uint32_t out, uint32_t loadFlags, uint32_t reserved)
{
    return cTSPersistSystem::vf3(Word(key), iid, out, loadFlags, reserved);
}

// Slot 39, 0x1def30: NotifyListeners(9, &zero block); then slot 17 (no arguments) of the objects at
// sub+0x3c, sub+0x38 and sub+0x40 (each when non-null), in that order; then FlushGlobal 0x1f24d0
// (slot 5 of the object at global 0x10cc140); then NotifyListeners(10, &zero block). Each block is a
// fresh 16-byte local of zeros.
void cTSPersistSystem::AgeCaches()
{
    cTSPersistSystem *system = this;
    cTSPersistSystem *primary = system;
    EventBlock before;
    primary->NotifyListeners(9, before.Address());
    static ifaces::cIGZPersistCacheStrategy *cTSPersistSystem::*const kOrder[] = {&cTSPersistSystem::mLongTermCache, &cTSPersistSystem::mSmallObjectCache,
                                                                                  &cTSPersistSystem::mGeneralCache};
    for (auto cacheField : kOrder) {
        ifaces::cIGZPersistCacheStrategy *cache = system->*cacheField;
        if (cache) cache->AdvanceGeneration();
    }
    FlushGlobal();
    EventBlock after;
    primary->NotifyListeners(10, after.Address());
}

// Slot 40, 0x1df160: as AgeCaches without the global, calling slot 23 with (1, 0, -1) on the objects at
// sub+0x38, sub+0x3c and sub+0x40 (each when non-null), in that order.
void cTSPersistSystem::PurgeCaches()
{
    cTSPersistSystem *system = this;
    cTSPersistSystem *primary = system;
    EventBlock before;
    primary->NotifyListeners(9, before.Address());
    static ifaces::cIGZPersistCacheStrategy *cTSPersistSystem::*const kOrder[] = {&cTSPersistSystem::mSmallObjectCache, &cTSPersistSystem::mLongTermCache,
                                                                                  &cTSPersistSystem::mGeneralCache};
    for (auto cacheField : kOrder) {
        ifaces::cIGZPersistCacheStrategy *cache = system->*cacheField;
        if (cache) cache->Purge(1, 0, 0xffffffffu);
    }
    EventBlock after;
    primary->NotifyListeners(10, after.Address());
}

// Slot 69, 0x1e0680 (ret 0x18; `this` unused): if obj and a5 are non-null, out = null and
// obj->slot19(a2, a3, &out); on success al = out->slot0(a4, a5) and *status (if non-null) is 0 (al 1) or
// 0xffffffa4 (al 0); on failure *status = 0xffffff9e. out is released when non-null. With a null obj or
// a5, *status = 0xffffff9f and the result is 0.
uint8_t cTSPersistSystem::OpenRecordFromFileRef(void *fileRef, uint32_t type, uint32_t instance, uint32_t iid, uint32_t out, uint32_t *status)
{
    if (!fileRef || !out) {
        if (status) *status = 0xffffff9f;
        return 0;
    }
    ifaces::cIGZPersistDBRecord *record = nullptr;
    ReleaseScope release(record);
    if (!As<ifaces::cITSObjectFileRef>(fileRef)->OpenRecord(type, instance, reinterpret_cast<void **>(&record))) {
        if (status) *status = 0xffffff9e;
        return 0;
    }
    uint8_t ok = record->QueryInterface(iid, Ptr<void>(out));
    if (status) *status = ok ? 0 : 0xffffffa4;
    return ok ? 1 : 0;
}

// Slot 70, 0x1de1e0 (ret 8): creates a cTSPersistDBRecord (operator new 0x64, Construct(1)), AddRefs it
// through slot 1 (when the allocation worked), returns its slot 0 (QueryInterface) called with (riid, ppv),
// and releases it through slot 2. A failed allocation would call slot 0 on null, as in the original.
uint8_t cTSPersistSystem::CreateRecord(uint32_t iid, void **out)
{
    void *mem = ::operator new(0x64);
    auto *record = mem ? static_cast<cTSPersistDBRecordObject *>(mem)->Construct(1) : nullptr;
    cIGZUnknown *unknown = reinterpret_cast<cIGZUnknown *>(record);
    if (record) unknown->AddRef();
    ReleaseScope release(unknown);
    return unknown->QueryInterface(iid, out);
}

// ---------------------------------------------------------------------------------------------------------
// Segment info, segment scoring, listeners and the record readers and writers.

// Slot 21, 0x1dda60 (ret 0x1c): local = null; GetDirectorFrameworkObject()->slot3(0x51509ded, 0x51509de7,
// &local) (result ignored); local->slot11(a3, a4, a5, a6, a7) (unchecked); returns this's slot 20 (a1, a2,
// local); releases local.
uint8_t cTSPersistSystem::OpenSegmentFromValues(uint32_t iid, uint32_t out, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    ifaces::cITSPersistDBSegmentInfo *info = nullptr;
    ReleaseScope release(info);
    // The new segment info object (cTSPersistDBSegmentInfo); the result of GetClassObject is ignored.
    As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())->GetClassObject(0x51509ded, 0x51509de7, &info);
    info->SetFromValues(a3, a4, a5, a6, a7); // unchecked, as in the original
    return As<ifaces::cITSPersistSystem>(ITSFace())->OpenSegmentByInfo(iid, Ptr<void *>(out), info);
}

// Slot 32, 0x1dfe10 (ret 8): a status for (obj, info), info = three dwords. Zero when this's slot 30 (obj,
// info[1]) fails. Else local = obj->slot0(0x3b2271bb) query; when it works, r = local->slot13(info[0]), and
// when that is -1, local->slot13(-1); r == 2 becomes 3 for info[2] in {0x93, 0x12d, 0x12e} and 0 otherwise.
// When the query fails or both reads are -1: 4 if obj->slot20(info) succeeds, else 1.
uint32_t cTSPersistSystem::ScoreSegment(void *segmentObject, const uint32_t *info)
{
    auto *segment = static_cast<Segment *>(segmentObject);
    if (!As<ifaces::cITSPersistSystem>(ITSFace())->SegmentHasGroup(segment, info[1])) return 0;
    ifaces::cITSPersistDBSegment *keyStatus = nullptr;
    ReleaseScope release(keyStatus);
    uint32_t score = 0;
    bool fallback = true;
    if (Query(segmentObject, kIidKeyStatus, &keyStatus)) {
        score = keyStatus->GetKeyStatus(info[0]);
        if (score == 0xffffffffu) {
            score = keyStatus->GetKeyStatus(0xffffffffu);
            fallback = score == 0xffffffffu;
        } else {
            fallback = false;
            if (score == 2) score = info[2] == 0x93 || info[2] == 0x12d || info[2] == 0x12e ? 3 : 0;
        }
    }
    if (fallback) score = segment->TestForRecord(KeyOf(info)) ? 4 : 1;
    return score;
}

// Slot 51, 0x1de7b0 (ret 4): the primary's slot 54, then over the pointer vector `items` (begin, end; the
// end is read once) while the result is 1: local = item->slot0(0x8ceec300) query; when it works and
// local->slot5() is nonzero, the result is local->slot8(0) != 0. Releases local each time.
uint8_t cTSPersistSystem::CommitSegments(void *const *items)
{
    cTSPersistSystem *system = this;
    As<ifaces::cTSPersistSystem>(system)->ClearSpeculativeLoader();
    uint8_t ok = 1;
    void *const *end = reinterpret_cast<void *const *>(items[1]);
    for (void *const *it = reinterpret_cast<void *const *>(items[0]); it != end && ok; ++it) {
        ifaces::cITSPersistDBSegmentWithRollback *rollback = nullptr;
        ReleaseScope release(rollback);
        if (Query(*it, kIidRollback, &rollback) && rollback->IsRollbackEnabled()) {
            ok = rollback->Commit(0) != 0;
        }
    }
    return ok;
}

// 0x1c1a65 (__cdecl, original): the free bytes on the drive of a path (GetDiskFreeSpaceEx through the game's wide
// function table, else the ANSI forms).
static uint64_t FreeDiskSpace(cRZString *path)
{
    return OriginalFunction<uint64_t(__cdecl *)(cRZString *), 0x1c1a65>()(path);
}

// Slot 50, 0x1dcef0 (1300 B, ret 8; lane FD): ClearSpeculativeLoader, then EndRollback (rollback face slot 7) of every
// segment with rollback on, while it works. With `guardLowDiskSpace` and under 100 MB free on the first segment's drive
// (0x1c1a65), each changed packed file is first read into memory (cRZFile Open(1, 2, 3)); a failed open stops the run.
// On failure: slot 51 (CommitSegments) over the same vector, then every backup is written back (segment Close, cRZFile
// Open(2, 4, 1) + Write, segment Open(1, 1)). The backups are freed; returns whether every segment worked.
uint8_t cTSPersistSystem::CommitSegmentsWithBackup(void *const *segments, uint32_t guardLowDiskSpace)
{
    cTSPersistSystem *system = this;
    As<ifaces::cTSPersistSystem>(system)->ClearSpeculativeLoader();
    // A packed file's bytes as they were before the commit (the original's std::list node: segment, length, bytes).
    struct Backup {
        Segment *segment;
        uint32_t length;
        char *bytes;
    };
    std::list<Backup> backups;
    Segment *const *first = static_cast<Segment *const *>(segments[0]);
    Segment *const *end = static_cast<Segment *const *>(segments[1]);
    // Packed files are copied to memory first only when asked to and the first segment's drive has less than 100 MB free.
    bool backUp = false;
    if (first != end) {
        cRZString path;
        path.Construct();
        first[0]->GetPath(&path);
        if (static_cast<uint8_t>(guardLowDiskSpace) != 0) backUp = FreeDiskSpace(&path) < 0x6400000u;
        path.Destruct();
    }
    bool ok = true;
    for (Segment *const *it = first; it != end && ok; ++it) {
        ifaces::cITSPersistDBSegmentWithRollback *rollback = nullptr;
        ReleaseScope releaseRollback(rollback);
        if (!Query(*it, kIidRollback, &rollback) || !rollback->IsRollbackEnabled()) continue;
        ifaces::cIGZDBSegmentPackedFile *packed = nullptr;
        ReleaseScope releasePacked(packed);
        if (backUp && !rollback->IsUnchanged() && Query(rollback, kIidPackedFile, &packed)) {
            cRZString path;
            path.Construct();
            rollback->GetSegment()->GetPath(&path);
            cRZFile file;
            file.Construct(*path.AsInterface());
            if (file.Open(1, 2, 3)) {
                const uint32_t length = file.Length();
                // operator new[] (0x8252b1); the original's null test after it never fires (the allocation throws).
                char *bytes = new char[length];
                file.Read(bytes, length);
                backups.push_back(Backup{*it, length, bytes});
                file.Close();
            } else {
                ok = false;
            }
            file.Destruct();
            path.Destruct();
            if (!ok) continue;
        }
        ok = rollback->EndRollback() != 0;
    }
    if (!ok) {
        // Undo: the system's slot 51 over the same segments, then each backed-up file is written back while its
        // segment is closed, and the segment reopened.
        As<ifaces::cITSPersistSystem>(ITSFace())->CommitSegments(segments);
        for (const Backup &backup : backups) {
            cRZString path;
            path.Construct();
            backup.segment->GetPath(&path);
            backup.segment->Close();
            cRZFile file;
            file.Construct(*path.AsInterface());
            if (file.Open(2, 4, 1)) {
                file.Write(backup.bytes, backup.length);
                file.Close();
            }
            backup.segment->Open(1, 1);
            file.Destruct();
            path.Destruct();
        }
    }
    for (const Backup &backup : backups) delete[] backup.bytes;
    return ok;
}

// Slot 61, 0x1daa20 (ret 4): AddListener. Under lock D, appends `listener` (AddRef) to the listener vector
// (sub+0x188, reallocating through RefPtrVector::EmplaceReallocate when full), then calls the listener's
// slot 3 (0, &zero block). A held reference (AddRef/Release) brackets the append.
void cTSPersistSystem::AddListener(void *listenerObject)
{
    cTSPersistSystem *system = this;
    auto *listener = static_cast<ifaces::PersistSystemListener *>(listenerObject);
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockD));
    {
        void *held = listenerObject;
        AddRefIf(held);
        ReleaseScope release(held);
        auto &listeners = reinterpret_cast<RefPtrVector &>(system->mListenersBegin);
        void **last = listeners.mLast;
        if (last != listeners.mEnd) {
            *last = listenerObject;
            AddRefIf(listenerObject);
            listeners.mLast = last + 1;
        } else {
            listeners.EmplaceReallocate(last, &held);
        }
    }
    EventBlock block;
    listener->OnPersistEvent(0, block.Address());
}

// Slot 62, 0x1e39c0 (ret 4): RemoveListener. Under lock D, erases the first matching entry from the
// listener vector (counted-pointer moves down, the last entry released), then calls the listener's slot 3
// (1, &zero block) whether or not it was found.
void cTSPersistSystem::RemoveListener(void *listenerObject)
{
    cTSPersistSystem *system = this;
    auto *listener = static_cast<ifaces::PersistSystemListener *>(listenerObject);
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockD));
    ifaces::PersistSystemListener **begin = system->mListenersBegin;
    ifaces::PersistSystemListener **&end = system->mListenersEnd;
    ifaces::PersistSystemListener **found = begin;
    while (found != end && *found != listener) ++found;
    if (found != end) {
        // std::vector<counted pointer>::erase: each later entry moves down one place (AddRef the moved pointer,
        // Release the overwritten one, skipped when they are equal), then the last entry is released.
        ifaces::PersistSystemListener **last = end;
        for (ifaces::PersistSystemListener **dst = found; dst + 1 != last; ++dst) {
            ifaces::PersistSystemListener *moved = dst[1];
            ifaces::PersistSystemListener *old = *dst;
            if (moved == old) continue;
            AddRefIf(moved);
            *dst = moved;
            ReleaseIf(old);
        }
        ReleaseIf(end[-1]);
        end = end - 1;
    }
    EventBlock block;
    listener->OnPersistEvent(1, block.Address());
}

// Slot 71, 0x1dd410 (ret 0x14; `this` unused): with obj and a2 non-null, a cRZString local (assigned from
// `name` when given) is passed to obj->slot27(a2, a3, a4, &local, 1); if that returns 0 and obj->slot28(a2)
// returns 0, the result is 1; if slot 28 returned nonzero, obj->slot29(a2) is called. Otherwise 0.
uint8_t cTSPersistSystem::CommitRecordToFileRef(void *fileRefObject, uint32_t record, uint32_t type, uint32_t instance, void *name)
{
    if (!fileRefObject || !record) return 0;
    cRZString local;
    local.Construct();
    if (name) static_cast<cIGZString *>(static_cast<void *>(&local))->Copy(*static_cast<cIGZString *>(name)); // cRZString::operator= (0x1ac9d0) is slot 17
    uint8_t committed = 0;
    auto *fileRef = As<ifaces::cITSObjectFileRef>(fileRefObject);
    auto *dbRecord = Ptr<ifaces::cITSPersistDBRecord>(record);
    if (!fileRef->AddRecord(dbRecord, type, instance, &local, 1)) {
        if (!fileRef->CommitRecord(dbRecord)) {
            committed = 1;
        } else {
            fileRef->AbortRecord(dbRecord);
        }
    }
    local.Destruct();
    return committed;
}

// Slot 72, 0x1e5e70 (ret 0xc): creates a record through this's slot 70 (0x3b2271c0, &local), fills a key
// block {a1->slot5(), a2->slot11(), (uint16)a3, 0} into it (local->slot25), events 0xe and then 0xf or
// 0x10 around a1->slot3(local->slot3()); on success this's slot 71 (a2, local, a1->slot5(), a3, 0) decides
// the result 0, else -1. Releases local.
uint32_t cTSPersistSystem::WriteResourceToFileRef(void *resourceObject, void *fileRefObject, uint32_t instance)
{
    cTSPersistSystem *system = this;
    ifaces::cITSPersistDBRecord *record = nullptr;
    ReleaseScope release(record);
    auto *resource = static_cast<ifaces::cIGZSerializable *>(resourceObject);
    auto *fileRef = static_cast<ifaces::cITSObjectFileRef *>(fileRefObject);
    auto *its = As<ifaces::cITSPersistSystem>(ITSFace());
    its->CreateRecord(0x3b2271c0, reinterpret_cast<void **>(&record));
    cGZPersistResourceKey key;
    uint32_t group = fileRef->GetGroup();
    key.type = resource->GetGZCLSID();
    key.group = group;
    key.instance = static_cast<uint16_t>(instance);
    key.resource = 0;
    record->SetKey(&key);
    cTSPersistSystem *primary = system;
    primary->NotifyListeners(0xe, Word(&key));
    uint8_t ok = resource->Write(record->GetOStream());
    primary->NotifyListeners(ok ? 0xf : 0x10, Word(&key));
    uint32_t result = 0xffffffffu;
    if (ok) {
        if (its->CommitRecordToFileRef(fileRef, record, resource->GetGZCLSID(), instance, nullptr)) result = 0;
    }
    return result;
}

// Slot 73, 0x1e3030 (ret 0x10): this's slot 69 (a2, a1->slot5(), a3, 0x3b2271c0, &local, &status). Without
// a record the result is the status. With one: when a4 is non-null a UtilObjectB(a4) (operator new 0x14) is
// passed to local->slot4()->slot19; then events 2 and 3 with the block {a1->slot5(), a2->slot11(),
// (int16)a3, 0} around a1->slot4(local->slot4()), whose al gives 0 (else 0xffffffa5). Releases local.
uint32_t cTSPersistSystem::ReadResourceFromSegment(void *resourceObject, void *fileRefObject, uint32_t instance, uint32_t metadata)
{
    cTSPersistSystem *system = this;
    ifaces::cITSPersistDBRecord *record = nullptr;
    uint32_t status = 0;
    ReleaseScope release(record);
    auto *resource = static_cast<ifaces::cIGZSerializable *>(resourceObject);
    auto *fileRef = static_cast<ifaces::cITSObjectFileRef *>(fileRefObject);
    uint32_t type = resource->GetGZCLSID();
    As<ifaces::cITSPersistSystem>(ITSFace())->OpenRecordFromFileRef(fileRef, type, instance, 0x3b2271c0, reinterpret_cast<void **>(&record), &status);
    if (!record) return status;
    if (metadata) {
        void *mem = ::operator new(0x14);
        UtilObjectB *extra = mem ? static_cast<UtilObjectB *>(mem)->Construct(metadata) : nullptr;
        record->GetIStream()->SetMetadata(extra);
    }
    cGZPersistResourceKey key;
    key.group = fileRef->GetGroup();
    key.type = type;
    key.instance = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(instance)));
    key.resource = 0;
    cTSPersistSystem *primary = system;
    primary->NotifyListeners(2, Word(&key));
    uint8_t ok = resource->Read(record->GetIStream());
    primary->NotifyListeners(3, Word(&key));
    return ok ? 0 : 0xffffffa5u;
}

// Slot 74, 0x1df640 (ret 0xc): when the primary's slot 11 (key) succeeds, the base vf3 (key, a2, a3, 0, 0).
// Otherwise GetDirectorFrameworkObject()->slot3(0x5b561442, 0x3b56143a, &local); if local->slot4(key[1],
// (uint16)key[2]) is 0, the primary's slot 8 gets local->slot14() and the result is local->slot0(a2, a3);
// else 0. Releases local.
uint8_t cTSPersistSystem::GetAnimTable(const uint32_t *key, uint32_t iid, uint32_t out)
{
    cTSPersistSystem *system = this;
    if (As<ifaces::cIGZPersistResourceManager>(system)->IsResourceCached(KeyOf(key))) {
        return static_cast<uint8_t>(system->Base()->vf3(Word(key), iid, out, 0, 0));
    }
    ifaces::cTSAnimTable *animTable = nullptr;
    ReleaseScope release(animTable);
    As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())->GetClassObject(0x5b561442, 0x3b56143a, &animTable);
    if (animTable->Load(key[1], static_cast<uint16_t>(key[2]))) return 0;
    As<ifaces::cIGZPersistResourceManager>(system)->CacheResource(animTable->GetPersistResource());
    return animTable->QueryInterface(iid, Ptr<void *>(out));
}

// 0x1e5580 (non-virtual): for each of the objects at +0xe0, +0xe4, +0xe8 and +0xec that is non-null: this's
// slot 42 (object), the object's slot 4, then the field is cleared and the object released (slot 2). The
// field is read again for each step.
void cTSPersistSystem::ReleaseSegments()
{
    cTSPersistSystem *system = this;
    // The caches in the order they are released: small-object, long-term, general, sim.
    static ifaces::cIGZPersistCacheStrategy *cTSPersistSystem::*const kCaches[] = {&cTSPersistSystem::mSmallObjectCache, &cTSPersistSystem::mLongTermCache,
                                                                                   &cTSPersistSystem::mGeneralCache, &cTSPersistSystem::mSimCache};
    for (auto cacheField : kCaches) {
        if (!(system->*cacheField)) continue;
        As<ifaces::cIGZPersistResourceManager>(system)->RemoveCacheStrategy(system->*cacheField);
        ifaces::cIGZPersistCacheStrategy *cache = system->*cacheField; // each field is read again for every step
        cache->Shutdown();
        cache = system->*cacheField;
        if (cache) {
            system->*cacheField = nullptr;
            ReleaseObject(cache);
        }
    }
}

// Slot 6, 0x1e2ce0: cITSPersistSystem slot 68 (this - 4), the primary's slot 32, object B (+0x1c0): slot 4,
// then cleared and released; the primary's slot 45, ReleaseSegments. Returns 1.
bool cTSPersistSystem::PreAppShutdown()
{
    cTSPersistSystem *system = this;
    As<ifaces::cITSPersistSystem>(system->ITSFace())->StopBackgroundOpen();
    auto *resources = As<ifaces::cIGZPersistResourceManager>(system);
    resources->FlushSegments();
    ifaces::cITSPersistSpeculativeLoader *loader = system->mSpeculativeLoader;
    if (loader) {
        loader->Shutdown();
        loader = system->mSpeculativeLoader;
        if (loader) {
            system->mSpeculativeLoader = nullptr;
            loader->Release();
        }
    }
    resources->AdvanceCacheGenerations();
    system->ReleaseSegments();
    return 1;
}

// Slot 10, 0x1e27c0 (ret 4): object B (+0x1c0) slot 10 when non-null; unless the byte at +0x22d is set, r =
// the primary's slot 47 and, when non-null, SaveChangeRecords (0x1dedf0, the dirty-record flush) then r->slot10();
// when the dword at +0x1e0 is nonzero and the stopwatch at +0x1c8 has run longer, the primary's slot 32 and
// a restart of the stopwatch (0x1c95ec, original). Returns 1.
bool cTSPersistSystem::OnTick(uint32_t)
{
    cTSPersistSystem *system = this;
    ifaces::cITSPersistSpeculativeLoader *loader = system->mSpeculativeLoader;
    if (loader) loader->Update();
    if (!system->mDeferWrites) {
        Segment *defaultSegment = As<ifaces::cIGZPersistResourceManager>(system)->GetDefaultSegment();
        if (defaultSegment) {
            FlushDirtyRecords(system);
            defaultSegment->Flush();
        }
    }
    if (system->mFlushIntervalMs) {
        if (system->mFlushTimer.Elapsed() > system->mFlushIntervalMs) {
            As<ifaces::cIGZPersistResourceManager>(system)->FlushSegments();
            RestartStopwatch(&system->mFlushTimer);
        }
    }
    return 1; // the original ends with a call to ReturnTrueIgnoringArgument (0x148260)
}

// ---------------------------------------------------------------------------------------------------------
// Segment vector, key filter, resource registration.

namespace {

uint8_t BaseVf14(cTSPersistSystem *system, void *resource, const void *key, void *segment)
{
    return system->Base()->vf14(resource, key, segment);
}

} // namespace

// Slot 19, 0x1df9b0 (ret 8): creates a duplicate-ignoring resource key filter (0x1c bytes, vtable 0xda62d4,
// an empty std::set with a fresh head node, a2 at +0x10, flag byte +0x18 set), AddRefs it, returns the
// primary's (from cITSPersistSystem slot 3) slot 18 (a1, filter), then releases it.
uint32_t cTSPersistSystem::vf19(uint32_t list, uint32_t value)
{
    cTSPersistSystem *system = this;
    auto *filter = NewKeyFilter();
    AddRefIf(filter);
    filter->mValue = value;
    *reinterpret_cast<uint8_t *>(&filter->mFlag) = 1;
    auto *keyFilter = reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(filter);
    void *manager = As<ifaces::cITSPersistSystem>(system->ITSFace())->GetResourceManager();
    uint32_t count = As<ifaces::cIGZPersistResourceManager>(manager)->GetResourceKeyList(Ptr<void>(list), keyFilter);
    keyFilter->Release();
    return count;
}

// Slot 16, 0x1e3b20 (ret 8): a1->slot3(&key) fills a 16-byte key; the primary's slot 10 (&key); local = a2
// (AddRef) or, when null, this's slot 34 (&key, 0x656b8f0c, &local). Without one the result is 0. If this's
// slot 28 (local) accepts it: events 0xb and 0xc/0xd around the base vf14 (a1, &key, local) (0x9b5610,
// original), then this's slot 57 (local, &key, 1); result 1. Otherwise local->slot11(&string) into a
// discarded cRZString; result 0. Releases local.
uint8_t cTSPersistSystem::SaveResource(void *resourceObject, void *segmentObject)
{
    cTSPersistSystem *system = this;
    auto *its = As<ifaces::cITSPersistSystem>(ITSFace());
    cGZPersistResourceKey key;
    auto *resource = static_cast<ifaces::cIGZPersistResource *>(resourceObject);
    resource->GetKey(&key);
    As<ifaces::cIGZPersistResourceManager>(system)->ForgetResource(&key);
    auto *segment = static_cast<Segment *>(segmentObject);
    AddRefIf(segment);
    ReleaseScope release(segment);
    if (!segment) {
        its->FindSegment(&key, 0x656b8f0c, reinterpret_cast<void **>(&segment));
        if (!segment) return 0;
    }
    if (its->IsSegmentWritable(segment)) {
        cTSPersistSystem *primary = system;
        primary->NotifyListeners(0xb, Word(&key));
        uint8_t ok = BaseVf14(system, resourceObject, &key, segment);
        primary->NotifyListeners((ok ^ 1u) + 0xc, Word(&key));
        its->RegisterSegmentForKey(segment, &key, 1);
        return 1;
    }
    cRZString name;
    name.Construct();
    segment->GetPath(&name);
    name.Destruct();
    return 0;
}

// Slot 26, 0x1e2430 (ret 4): unless obj answers query 0x856c8cf7 or this's slot 28 (obj) accepts it:
// obj->slot10(), obj->slot9(), obj->slot11(&path) into a cRZString, 0x1c3505(path, 2, 1) (a file
// attribute call, original), obj->slot6(1, 1). Releases the query result.
void cTSPersistSystem::ReopenSegment(void *segmentObject)
{
    auto *segment = static_cast<Segment *>(segmentObject);
    ifaces::cIGZPersistDBSegmentDirectoryFiles *directory = nullptr;
    ReleaseScope release(directory);
    if (Query(segmentObject, kIidDirectoryFiles, &directory)) return;
    if (As<ifaces::cITSPersistSystem>(ITSFace())->IsSegmentWritable(segment)) return;
    segment->Flush();
    segment->Close();
    cRZString path;
    path.Construct();
    segment->GetPath(&path);
    OriginalFunction<uint32_t(__cdecl *)(cRZString *, uint32_t, uint32_t), 0x1c3505>()(&path, 2, 1);
    segment->AutoOpen(1, 1);
    path.Destruct();
}

// Slot 29, 0x1e0980 (ret 4): under lock B (+0x68), appends (counted) every segment of the vector at
// sub+0x2c (primary +0xd4) that this's slot 28 accepts to `out` (ServiceRefVector::InsertGrown, ours,
// when full). The end is read once.
void cTSPersistSystem::GetWritableSegments(void *out)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    auto *its = As<ifaces::cITSPersistSystem>(ITSFace());
    Segment **it = system->mSegmentsBegin;
    Segment **end = system->mSegmentsEnd; // read once
    auto &target = *static_cast<ServiceRefVector *>(out);
    for (; it != end; ++it) {
        Segment *segment = *it;
        if (!its->IsSegmentWritable(segment)) continue;
        Segment *held = segment;
        AddRefIf(held);
        ReleaseScope release(held);
        void **last = target.mLast;
        if (last != target.mEnd) {
            *last = segment;
            AddRefIf(segment);
            target.mLast = last + 1;
        } else {
            target.InsertGrown(last, reinterpret_cast<void **>(&held));
        }
    }
}

// Slot 45, 0x1e3630 (ret 4): under lock B, finds the first segment whose slot 15 equals seg->slot15(); if
// none, returns the primary's slot 27 (seg); else 0x1e9b50 (primary, found) (original) and 1.
uint8_t cTSPersistSystem::RegisterSegment(void *segmentObject)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    auto *segment = static_cast<Segment *>(segmentObject);
    uint32_t id = segment->GetSegmentID();
    Segment **it = system->mSegmentsBegin;
    Segment **end = system->mSegmentsEnd;
    while (it != end) {
        if ((*it)->GetSegmentID() == id) break;
        ++it;
    }
    // The end is read again for this test.
    if (it == system->mSegmentsEnd) {
        return As<ifaces::cIGZPersistResourceManager>(system)->AddSegment(segment);
    }
    system->IndexSegmentGroups(*it); // 0x1e9b50, ours since lane IP
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// The segments of a group (mSegmentsByGroup) and the functions that walk them.

// Slot 34, 0x1dea30 (ret 8): under lock B (+0x68), looks key[1] (or key[0] when key[1] is 0x1c0532fa) up in the
// factory table (+0x114, see GetSegmentsForGroup), filling it in when absent; the first factory
// whose slot 20 (key) answers nonzero is stored in *out and AddRef'd (slot 1); returns that, else 0.
uint8_t cTSPersistSystem::vf34(uint32_t keyWord, void **out)
{
    const uint32_t *key = Ptr<const uint32_t>(keyWord);
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    uint32_t group = key[1];
    if (group == kKeyTypeUseA3) group = key[0];
    StdHashTable &table = HashTable(system->mSegmentsByGroup);
    SegmentVector *list = FindSegments(table, group);
    if (!list) {
        system->FillInSegments(group);
        list = FindSegments(table, group);
    }
    if (!list) return 0;
    Segment **it = list->first;
    Segment **end = list->last;
    for (; it != end; ++it) {
        if ((*it)->TestForRecord(KeyOf(key))) {
            Segment *found = *it;
            *out = found;
            found->AddRef();
            return 1;
        }
    }
    return 0;
}

// Slot 15, 0x1e5670 (ret 4): as vf34 without the out object: true when a factory accepts the key.
uint8_t cTSPersistSystem::vf15(uint32_t keyWord)
{
    const uint32_t *key = Ptr<const uint32_t>(keyWord);
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    uint32_t group = key[1];
    if (group == kKeyTypeUseA3) group = key[0];
    StdHashTable &table = HashTable(system->mSegmentsByGroup);
    SegmentVector *list = FindSegments(table, group);
    if (!list) {
        system->FillInSegments(group);
        list = FindSegments(table, group);
    }
    if (!list) return 0;
    Segment **it = list->first;
    Segment **end = list->last;
    for (; it != end; ++it) {
        if ((*it)->TestForRecord(KeyOf(key))) return 1;
    }
    return 0;
}

// The factory table (sub+0x6c, primary +0x114): an identity-hashed StdHashTable keyed by a type id, each node
// (0x18 bytes: link, key at +8) holding a std::vector of factory pointers at +0xc. A key that is absent is
// filled in by FillInSegments (0x1e95c0, ours): it adds the node (it gathers the factories
// through the primary's slot 37 and stable-sorts them) and returns the node's vector.
// Slot 48, 0x1e0140 (ret 8): under lock B (sub-0x40), appends the factories of `key` to the pointer vector
// `out` with the vector's range insert (0x1d6520 in the image, library code: where = out's end, first, count; the
// factories are counted by it).
void cTSPersistSystem::GetSegmentsForGroup(uint32_t group, void *out)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    SegmentVector *list = LookupSegments(system, group);
    uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(Word(list->last) - Word(list->first)) >> 2);
    Segment **first = list->first;
    AppendCounted(out, reinterpret_cast<void *const *>(first), count);
}

// Slot 30, 0x1de910 (ret 8): under lock B, id = obj->slot15(); looks the id up (FNV-1a of its four bytes) in the
// table at sub+0x8c, filling it in with 0x1e97b0 (original, thiscall on the primary, ret 4) when absent; the
// value is a pointer to a dword range (first, last). True when `value` is in the range (std::find over dwords).
uint8_t cTSPersistSystem::SegmentHasGroup(void *segmentObject, uint32_t group)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    uint32_t id = static_cast<Segment *>(segmentObject)->GetSegmentID();
    StdHashTable &table = HashTable(system->mSegmentIdRanges);
    StdHashFindResult found{};
    table.Find32(&found, &id, Fnv1a32(id));
    StdHashNode *node = found.node ? found.node : table.mHead;
    DwordRange *range;
    if (node != table.mHead) {
        range = static_cast<SegmentIdNode *>(node)->range;
    } else {
        range = static_cast<DwordRange *>(system->SegmentGroups(segmentObject)); // 0x1e97b0, ours since lane IP
    }
    if (!range) return 0;
    return std::find(range->first, range->last, group) != range->last;
}

// Slot 34, 0x1dec80 (ret 0xc): under lock B, the factories for key[1] (filled in when absent): picks the one
// with the highest score (this's slot 32 (factory, key)), the first one with a working query for 0x286d4a6a
// winning ties against a failed one; then best->slot0(iid, out) (al). 0 without factories.
uint8_t cTSPersistSystem::FindSegment(const uint32_t *key, uint32_t iid, void **out)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    SegmentVector *segments = LookupSegments(system, key[1]);
    auto *its = As<ifaces::cITSPersistSystem>(ITSFace());
    ifaces::cIGZDBSegmentPackedFile *packed = nullptr;
    Segment *best = nullptr;
    uint32_t bestScore = 0;
    uint8_t bestIsPacked = 0;
    // The end is read again every round.
    for (Segment **it = segments->first; it != segments->last; ++it) {
        Segment *segment = *it;
        uint32_t score = its->ScoreSegment(segment, KeyOf(key));
        if (packed) {
            ifaces::cIGZDBSegmentPackedFile *old = packed;
            packed = nullptr;
            ReleaseObject(old);
        }
        uint8_t isPacked = Query(segment, kIidPackedFile, &packed);
        if (score > bestScore || (bestIsPacked == 0 && score == bestScore && isPacked != 0)) {
            best = segment;
            bestScore = score;
            bestIsPacked = isPacked;
        }
    }
    uint8_t result = 0;
    if (best) result = best->QueryInterface(iid, out);
    ReleaseIf(packed);
    return result;
}

// Slot 35, 0x1df880 (ret 8): under lock B, builds a filter (0x1c bytes, vtable 0xda62d4; +0x14 = type, flag
// byte +0x19 = 1) and calls slot 19 (out, filter) of every factory for `type` (filled in when absent), then
// returns the number of 16-byte elements of `out` (first, last). 0 (and no filter) when there are no factories.
uint32_t cTSPersistSystem::ListKeysOfType(void *out, uint32_t type)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    SegmentVector *segments = LookupSegments(system, type);
    if (!segments || segments->first == segments->last) return 0;
    KeyFilterIgnoreDuplicates *filter = NewKeyFilter();
    AddRefIf(filter);
    filter->mType = type;
    filter->mFlag = 0x100; // the byte at +0x19 is set; the constructor left the word 0
    auto *keyFilter = reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(filter);
    Segment **end = segments->last;
    for (Segment **it = segments->first; it != end; ++it) {
        (*it)->GetResourceKeyList(out, keyFilter);
    }
    uint32_t count = ElementCount16(*static_cast<const RawVector *>(out));
    keyFilter->Release();
    return count;
}

// Slot 36, 0x1df740 (ret 0xc): as slot 35 for the key (type, or a3 when type is 0x1c0532fa); the filter also
// has +0x10 = a3 and +0x18 = 0x101.
uint32_t cTSPersistSystem::ListKeys(void *out, uint32_t type, uint32_t value)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    uint32_t group = type == kKeyTypeUseA3 ? value : type;
    SegmentVector *segments = LookupSegments(system, group);
    if (!segments || segments->first == segments->last) return 0;
    KeyFilterIgnoreDuplicates *filter = NewKeyFilter();
    AddRefIf(filter);
    filter->mValue = value;
    filter->mFlag = 0x101;
    filter->mType = type;
    auto *keyFilter = reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(filter);
    Segment **end = segments->last;
    for (Segment **it = segments->first; it != end; ++it) {
        (*it)->GetResourceKeyList(out, keyFilter);
    }
    uint32_t count = ElementCount16(*static_cast<const RawVector *>(out));
    keyFilter->Release();
    return count;
}

// Slot 14, 0x1e0550 (ret 4; no lock): over the factories for `key` (not filled in; none gives 0xffff), each non-null
// factory's query for 0x286d4a6a, released at once; the (already released) object's slot 25 gives a value; when
// bit 16 is set its low word is the candidate. 0xffff while there is none; a second different candidate
// (0xffff ignored) makes the result 0xffff. The result is ax (the original leaves garbage in the upper half).
uint32_t cTSPersistSystem::GetPackedFileGroup(uint32_t group)
{
    SegmentVector *segments = FindSegments(HashTable(this->mSegmentsByGroup), group);
    uint32_t result = 0xffff;
    if (!segments) return result;
    for (Segment **it = segments->first; it != segments->last; ++it) {
        Segment *segment = *it;
        if (!segment) continue;
        ifaces::cIGZDBSegmentPackedFile *packed = nullptr;
        Query(segment, kIidPackedFile, &packed);
        ReleaseIf(packed);
        if (!packed) continue;
        // ORIGINAL-BUG: the object is used after the release above; safe only while something else keeps it alive.
        uint32_t info = packed->GetGroupInfo();
        if (!(info & 0x10000)) continue;
        uint32_t candidate = info & 0xffff;
        if (result == 0xffff) {
            result = candidate;
        } else if (candidate != 0xffff && candidate != result) {
            return 0xffff;
        }
    }
    return result;
}

// Slot 27, 0x1e2510 (ret 4): under lock B, the file name stem of obj (strings from its slots 7 and 6, split with
// cRZFileName::SplitInto), id = this's slot 11 (&stem), and this's slot 26 (factory) for every factory of id.
void cTSPersistSystem::ReopenSegmentsForPath(void *path)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    auto *its = As<ifaces::cITSPersistSystem>(ITSFace());
    cRZString stem;
    stem.Construct();
    {
        // path is a string: Strlen (slot 7) before Data (slot 6).
        auto *fileName = static_cast<cIGZString *>(path);
        uint32_t length = fileName->Strlen();
        const char *chars = fileName->Data();
        cRZString full;
        full.Construct();
        full.FromChar(chars, length);
        cRZFileName::SplitInto(&full, nullptr, nullptr, &stem, nullptr);
        full.Destruct();
    }
    uint32_t group = its->GetGroupIdForPath(&stem);
    SegmentVector *segments = LookupSegments(system, group);
    if (segments) {
        Segment **end = segments->last;
        for (Segment **it = segments->first; it != end; ++it) {
            its->ReopenSegment(*it);
        }
    }
    stem.Destruct();
}

// Slot 18, 0x1df300 (ret 0xc): under lock B, the factories of key (a3 == 3: 0x1c0532fa, else this's slot 11
// (a2)); for a3 0 no query, a3 1 the query 0x286d4a6a, a3 2 and 3 the query 0x856c8cf7 must work, other a3
// skips every factory. A working factory with an empty `name` ends with 1. Otherwise its slot 31 fills a path (a
// trailing separator removed: the two one-character strings 0xda52b4 and 0xda31d4), which is compared
// (0x1c8151, original) with the cRZFileName of `name`; equal ends with 1. 0 otherwise.
uint8_t cTSPersistSystem::HasSegmentForFile(void *name, void *path, uint32_t mode)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    auto *its = As<ifaces::cITSPersistSystem>(ITSFace());
    uint32_t group = mode == 3 ? kKeyTypeUseA3 : its->GetGroupIdForPath(path);
    SegmentVector *segments = LookupSegments(system, group);
    if (!segments || segments->first == segments->last) return 0;
    auto *nameString = static_cast<cIGZString *>(name); // the file name wanted (Strlen slot 7, Data slot 6)
    Segment **end = segments->last;
    for (Segment **it = segments->first; it != end; ++it) {
        Segment *segment = *it;
        if (mode > 3) continue;
        if (mode != 0) {
            // mode 1: the packed-file face must answer; modes 2 and 3: the directory face.
            cIGZUnknown *face = nullptr;
            uint8_t works = Query(segment, mode == 1 ? kIidPackedFile : kIidDirectoryFiles, &face);
            ReleaseIf(face);
            if (!works) continue;
        }
        if (nameString->Strlen() == 0) return 1;
        cRZString storedPath;
        storedPath.Construct();
        its->GetSegmentPath(segment, &storedPath);
        auto *helpers = reinterpret_cast<cRZStringHelpers *>(&storedPath);
        if (helpers->EndsWith(GameAddress<const char>(0xda52b4), 1, 1) || helpers->EndsWith(GameAddress<const char>(0xda31d4), 1, 1)) {
            storedPath.Erase(storedPath.Strlen() - 1, 0xffffffffu);
        }
        cRZFileName stored;
        stored.Construct(&storedPath);
        uint32_t length = nameString->Strlen();
        const char *chars = nameString->Data();
        cRZString text;
        text.Construct();
        text.FromChar(chars, length);
        cRZFileName wanted;
        wanted.Construct(&text);
        text.Destruct();
        uint32_t different = OriginalFunction<uint32_t(__thiscall *)(void *, void *, uint32_t), 0x1c8151>()(&stored, &wanted, 1);
        wanted.Destruct();
        stored.Destruct();
        storedPath.Destruct();
        if (different == 0) return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------
// Slots 53 and 57.

// Slot 53, 0x1e2f00 (ret 0x1c): unless the byte at +0x22d and the dword at +0x84 are both nonzero, the base
// vf53 (all seven arguments). Otherwise local = null; this's slot 39 (a1, &local, 0); on success
// NotifyListeners(2, a1), then a2's slot 3 (local, 0x456b8f1d, &local2, a6, a7) -> r and NotifyListeners(3, a1);
// r nonzero: local2's slot 4 (a1), r = local2's slot 0 (a4, a5), local's slot 6 (), local2 released; r zero:
// local's slot 7 (). Local is released when non-null. Returns r (0 when the slot 39 call failed).
uint8_t cTSPersistSystem::vf53(uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    cTSPersistSystem *system = this;
    if (system->mDeferWrites == 0 || system->mField84 == 0) {
        return system->Base()->vf53(a1, a2, a3, a4, a5, a6, a7);
    }
    const auto *key = Ptr<const ::cGZPersistResourceKey>(a1);
    ifaces::cIGZPersistDBRecord *record = nullptr; // the original reuses the stack slot of a3
    uint8_t result = 0;
    if (As<ifaces::cIGZPersistResourceManager>(system)->OpenRecord(key, reinterpret_cast<void **>(&record), 0)) {
        ifaces::cIGZPersistResource *resource = nullptr; // and the slot of a1
        NotifyListeners(2, a1);
        result = As<ifaces::cIGZPersistResourceFactory>(a2)->CreateInstanceFromRecord(record, kIidRecordCopy, reinterpret_cast<void **>(&resource), a6, a7);
        NotifyListeners(3, a1);
        if (result) {
            resource->SetKey(Ptr<void>(a1));
            result = resource->QueryInterface(a4, Ptr<void>(a5));
            record->Close();
            ReleaseObject(resource);
        } else {
            record->Abort();
        }
    }
    ReleaseIf(record);
    return result;
}

// Slot 57, 0x1dcd80 (ret 4): under lock B (+0x68), mSegmentsByGroup cleared (0x1e8ce0, library),
// then over a copy (0x1d89b0 / 0x1e6a40, original) of the segment vector at +0xd4: each segment's slot 11 gives
// a name, which is lowercased; when it starts with the text of `prefix` (its slot 7 length, then slot 5 chars;
// case-sensitive) this's slot 30 (segment).
void cTSPersistSystem::RemoveSegmentsWithPrefix(void *prefixObject)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    // 0x1e8ce0 is the toolchain's unordered_map::clear (each value destroyed by 0x1e6a40, its node freed).
    reinterpret_cast<GroupSegmentMap &>(system->mSegmentsByGroup).clear();
    ServiceRefVector copy;
    OriginalFunction<void(__thiscall *)(void *, const void *), 0x1d89b0>()(&copy, &system->mSegmentsBegin);
    auto *prefix = static_cast<cIGZString *>(prefixObject);
    void **end = copy.mLast;
    for (void **it = copy.mFirst; it != end; ++it) {
        auto *segment = static_cast<Segment *>(*it);
        cRZString name;
        name.Construct();
        segment->GetPath(&name);
        reinterpret_cast<cRZStringHelpers *>(&name)->LowerCase();
        uint32_t length = prefix->Strlen();
        const char *chars = prefix->ToChar();
        if (name.StartsWith(chars, length, true)) As<ifaces::cIGZPersistResourceManager>(system)->RemoveSegment(segment);
        name.Destruct();
    }
    OriginalFunction<void(__thiscall *)(void *), 0x1e6a40>()(&copy);
}

// Slot 63, 0x1e5200: the table at +0x1a0's entry for `key` (made by the game's try_emplace 0x1d75b0)
// takes `object` as a counted reference: AddRef on the new one, Release on the old one, nothing when
// they are the same. Two stack arguments (ret 8).
void cTSPersistSystem::SetKeyedObject(uint32_t key, void *object)
{
    // try_emplace(key): {node, inserted} through a hidden pointer, which it returns.
    using TryEmplaceFn = EmplaceResult<KeyedObjectNode> *(__thiscall *)(StdHashTable *, EmplaceResult<KeyedObjectNode> *, const uint32_t *);
    EmplaceResult<KeyedObjectNode> result;
    KeyedObjectNode *node = OriginalFunction<TryEmplaceFn, 0x1d75b0>()(&HashTable(this->mKeyedObjects), &result, &key)->node;
    ::cIGZUnknown *old = node->object;
    auto *next = static_cast<::cIGZUnknown *>(object);
    if (next != old) {
        if (next) next->AddRef();
        node->object = next;
        if (old) old->Release();
    }
}

// Slot 56, 0x1e3b10: the thunk to the primary's 0x1ea4f0 (SaveResourceNames: one argument, ret 4, returns al).
uint8_t cTSPersistSystem::vf168_56(void *record)
{
    return SaveResourceNames(record);
}

// Slot 59, 0x1e4b40: under lock D (a cRZLock on the stack), when the base's object at primary +0x84 is
// set and the primary's slot 55 (a2, &value) succeeds with a non-zero value: the entry for `group` in
// the map at +0x164 (the game's 0x1d7970) records (a2, value, a3) through 0x1f6de0, and the flag byte
// at +0x184 is set. Three stack arguments (ret 0xc).
void cTSPersistSystem::NoteResourceChange(uint32_t group, uint32_t key, uint32_t a3)
{
    cTSPersistSystem *system = this;
    cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockD));
    if (system->mField84) { // the base's object (set by vf46)
        uint32_t changeId;
        if (As<ifaces::cTSPersistSystem>(system)->GetRecordChangeID(Ptr<const ::cGZPersistResourceKey>(key), &changeId) && changeId) {
            // The entry for `group` in mChangeRecords, made by the game's 0x1d7970; its records (node +0xc) take
            // (key, changeId, a3) (ChangeRecordSet::Put, 0x1f6de0).
            using EntryFn = EmplaceResult<ChangeRecordNode> *(__thiscall *)(void *, EmplaceResult<ChangeRecordNode> *, const uint32_t *);
            EmplaceResult<ChangeRecordNode> where;
            ChangeRecordNode *node = OriginalFunction<EntryFn, 0x1d7970>()(&system->mChangeRecords, &where, &group)->node;
            reinterpret_cast<ChangeRecordSet *>(node->records)
                ->Put(Ptr<const ChangeRecordKey>(key), changeId, reinterpret_cast<const void *>(static_cast<uintptr_t>(a3)));
            system->mHasChangeRecords = 1;
        }
    }
}

// Lane FD: ITS slot 58. The change records of a group (node +0xc, built by 0x1f5850): a hash table of 0x20-byte
// entries {link, key (4 dwords) at +8, the counted object at +0x18, its change id at +0x1c}, with the list head at +4,
// the size at +8, the bucket vector at +0xc/+0x10, and a "changed" flag at +0x20.
namespace {
struct CachedRecord : StdHashNode {
    uint32_t key[4];
    ::cIGZUnknown *object;
    uint32_t changeId;
};
static_assert(sizeof(CachedRecord) == 0x20, "cached record");
struct ChangeRecords {
    uint32_t loadFactor;
    CachedRecord *head;
    uint32_t size;
    CachedRecord **bucketsBegin, **bucketsEnd, **bucketsCapacity;
    uint32_t mask, bucketCount;
    uint8_t changed;
};
static_assert(sizeof(ChangeRecords) == 0x24 && offsetof(ChangeRecords, changed) == 0x20, "change records");

// Empties the records (the original does it inline): the objects are released, the nodes freed and every bucket
// pointed back at the head.
void ClearChangeRecords(ChangeRecords &records)
{
    if (records.size == 0) return;
    CachedRecord *head = records.head;
    head->prev->next = nullptr;
    for (StdHashNode *node = head->next; node;) {
        StdHashNode *next = node->next;
        auto *record = static_cast<CachedRecord *>(node);
        if (record->object) record->object->Release();
        ::operator delete(record, sizeof(CachedRecord));
        node = next;
    }
    head->next = head;
    head->prev = head;
    records.size = 0;
    for (CachedRecord **bucket = records.bucketsBegin; bucket < records.bucketsEnd; ++bucket) *bucket = head;
}
} // namespace

// Slot 58, 0x1dfaa0 (596 B, ret 0xc): GetCachedResource. Under lock D, with the store at +0x84: the group's
// change records in mChangeRecords, loaded on first use from the store's record (0xad1a6720, 0, group, 0)
// through 0x1f6bd0 (a failed read empties them and sets their flag and +0x22c). Then the key's change id
// (primary slot 55): non-zero gives 0x1f6780 (the cached object into *out when the id matches; a miss ORs the
// records' flag into +0x22c), else 0x1f6410 drops the key's entry and 0.
uint8_t cTSPersistSystem::GetCachedResource(uint32_t group, const uint32_t *key, void **out)
{
    cTSPersistSystem *system = this;
    cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockD));
    void *store = reinterpret_cast<void *>(system->mField84);
    if (!store) return 0;

    StdHashTable &table = HashTable(system->mChangeRecords);
    StdHashFindResult found{};
    table.Find32(&found, &group, Fnv1a32(group));
    StdHashNode *node = found.node ? found.node : table.mHead;
    ChangeRecords *records = nullptr;
    if (node != table.mHead) {
        records = reinterpret_cast<ChangeRecords *>(static_cast<ChangeRecordNode *>(node)->records);
    } else {
        // Not loaded yet: the group's cache record (type 0xad1a6720, instance = group) from the store, read into
        // a new entry through 0x1f6bd0; a record that does not read empties the entry and marks it changed.
        const uint32_t recordKey[4] = {0xad1a6720u, 0, group, 0};
        ::cIGZUnknown *record = nullptr;
        if (VirtualCall<uint8_t(__thiscall *)(void *, const uint32_t *, ::cIGZUnknown **, uint32_t), 22>(store, recordKey, &record, 1)) {
            void *stream = nullptr;
            record->QueryInterface(0xc56b8f08, &stream);
            using EntryFn = EmplaceResult<ChangeRecordNode> *(__thiscall *)(void *, EmplaceResult<ChangeRecordNode> *, const uint32_t *);
            EmplaceResult<ChangeRecordNode> where;
            ChangeRecordNode *entry = OriginalFunction<EntryFn, 0x1d7970>()(&system->mChangeRecords, &where, &group)->node;
            records = reinterpret_cast<ChangeRecords *>(entry->records);
            if (!reinterpret_cast<ChangeRecordSet *>(records)->Read(stream)) {
                ClearChangeRecords(*records);
                records->changed = 1;
                system->mHasChangeRecords = 1;
            }
            VirtualCall<uint32_t(__thiscall *)(void *), 6>(record);
            if (stream) static_cast<::cIGZUnknown *>(stream)->Release();
        }
        if (record) record->Release();
    }
    if (!records) return 0;

    uint32_t changeId;
    if (As<ifaces::cTSPersistSystem>(system)->GetRecordChangeID(reinterpret_cast<const ::cGZPersistResourceKey *>(key), &changeId) && changeId) {
        // ChangeRecordSet::Take (0x1f6780): the cached object when its change id matches (AddRef'd into *out), else the
        // stale entry erased.
        const uint8_t hit = reinterpret_cast<ChangeRecordSet *>(records)->Take(reinterpret_cast<const ChangeRecordKey *>(key), changeId, out);
        if (!hit) system->mHasChangeRecords |= records->changed;
        return hit;
    }
    reinterpret_cast<ChangeRecordSet *>(records)->Erase(reinterpret_cast<const ChangeRecordKey *>(key)); // erases the entry
    return 0;
}

// ---------------------------------------------------------------------------------------------------------
// Lane CT: the functions Lane CN left unfinished, leaves first.

namespace {

// A node of mPathGroups (0x2c bytes): link, the path key (a cRZString) at +8, the group id at +0x28.
struct PathGroupNode : StdHashNode {
    cRZString path;
    uint32_t group;
};
static_assert(sizeof(PathGroupNode) == 0x2c && offsetof(PathGroupNode, group) == 0x28, "path group node");

// The mapped value of mKeyedObjects: a counted object released when its node dies (0x1d5dd0 frees the 0x10-byte nodes).
struct CountedObject {
    ::cIGZUnknown *object = nullptr;
    CountedObject() = default;
    CountedObject(const CountedObject &) = delete;
    CountedObject &operator=(const CountedObject &) = delete;
    ~CountedObject()
    {
        if (object) object->Release();
    }
};
static_assert(sizeof(std::pair<const uint32_t, CountedObject>) == 8, "keyed object element");

struct PathGroupInsertResult {
    PathGroupNode *node;
    uint8_t inserted;
};

} // namespace

// Slot 67, 0x1e2c40: makes the background-open thread when there is none (a new cRZThread, named, priority 2), and
// starts it on 0x1defe0 (ours, BackgroundOpenThread) with the whole object, unless it is already running.
// 67 StartBackgroundOpen, 0x1e2c40, ret 0 (lane CT)
void cTSPersistSystem::StartBackgroundOpen()
{
    cTSPersistSystem *system = this;
    if (!system->mBackgroundThread) {
        auto *created = static_cast<cRZThread *>(::operator new(sizeof(cRZThread)));
        if (created) created = created->Construct();
        system->mBackgroundThread = reinterpret_cast<ifaces::cIGZThread *>(created);
        // The name goes to the new object (the local), the priority to the field read again.
        reinterpret_cast<ifaces::cIGZThread *>(created)->SetName(GameAddress<const char>(0xda6688));
        system->mBackgroundThread->SetPriority(2);
    } else if (system->mBackgroundThread->IsRunning()) {
        return;
    }
    system->mBackgroundThread->Start(Word(reinterpret_cast<void *>(&cTSPersistSystem::BackgroundOpenThread)), Word(system));
}

// Slot 25, 0x1de2d0 (ret 8): under the segment lock, ReleaseSegmentsForGroup for the group of the file name's stem.
// 25 ReleaseSegmentsForFile (CJ), 0x1de2d0, ret 8
uint8_t cTSPersistSystem::ReleaseSegmentsForFile(void *path, uint32_t flag)
{
    cTSPersistSystem *system = this;
    cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    auto *its = As<ifaces::cITSPersistSystem>(ITSFace());
    cRZString stem;
    stem.Construct();
    {
        // path is a string: Strlen (slot 7) before Data (slot 6).
        auto *fileName = static_cast<cIGZString *>(path);
        uint32_t length = fileName->Strlen();
        const char *chars = fileName->Data();
        cRZString full;
        full.Construct();
        full.FromChar(chars, length);
        cRZFileName::SplitInto(&full, nullptr, nullptr, &stem, nullptr);
        full.Destruct();
    }
    uint32_t group = its->GetGroupIdForPath(&stem);
    uint32_t released = its->ReleaseSegmentsForGroup(group, flag);
    stem.Destruct();
    return released & 0xff;
}

// Slot 12, 0x1e02f0: the stem of the name (cut at the first occurrence of the text at 0xd9f880) as a group id: the
// 24-bit CRC of its lowercase form, tagged 0x7f000000.
// 12 GetGroupIdForNameChars, 0x1e02f0, ret 4: Crc24(lowercase stem) | 0x7f000000 (lane CT)
uint32_t cTSPersistSystem::GetGroupIdForNameChars(const char *name)
{
    cRZString text;
    text.Construct(name);
    int32_t at = text.Find(GameAddress<const char>(0xd9f880), 0, true);
    if (at != -1) text.Erase(static_cast<uint32_t>(at), 0xffffffffu);
    uint32_t crc = StringKeyParser::Crc24(text.ToChar(), 1);
    text.Destruct();
    return crc | 0x7f000000;
}

// Slot 13, 0x1e0200: the group id of a segment's file: its path, split into the file name, through slot 12. A null
// segment gives 0.
// 13 GetGroupIdForFileName, 0x1e0200, ret 4: the segment's GetPath (slot 11), split into the file name (lane CT)
uint32_t cTSPersistSystem::GetGroupIdForFileName(void *segmentObject)
{
    if (!segmentObject) return 0;
    cRZString path;
    path.Construct();
    static_cast<Segment *>(segmentObject)->GetPath(&path);
    cRZString name;
    name.Construct();
    cRZFileName::SplitInto(&path, nullptr, nullptr, &name, nullptr);
    uint32_t group = As<ifaces::cITSPersistSystem>(ITSFace())->GetGroupIdForNameChars(name.ToChar());
    name.Destruct();
    path.Destruct();
    return group;
}

// Slot 10, 0x1dffb0: the group id of a path: the entry of mPathGroups (case-insensitive, cut at the first '.'), else
// the next number from mNextPathGroup tagged 0x6f000000, recorded under the path.
// Not reimplemented yet (lane CN handoff, notes/feedback/CN.md section (c); declared only, no bodies).
// Slot, RVA, ret N; names are the cITSPersistSystem facts where one exists.
// 10 GetGroupIdForPathChars, 0x1dffb0, ret 4: mPathGroups lookup, else mNextPathGroup++ | 0x6f000000 (lane CT)
uint32_t cTSPersistSystem::GetGroupIdForPathChars(const char *path)
{
    cTSPersistSystem *system = this;
    StdHashTable &table = HashTable(system->mPathGroups);
    StdHashNode *found;
    {
        cRZString key;
        key.Construct(path);
        // 0x1d4a70 (stdcall): the key's hash (first seven characters, lowercased); 0x1d5b50: the lookup.
        uint32_t hash = OriginalFunction<uint32_t(__stdcall *)(const void *), 0x1d4a70>()(&key);
        StdHashFindResult result{};
        OriginalFunction<void(__thiscall *)(void *, StdHashFindResult *, const void *, uint32_t), 0x1d5b50>()(&table, &result, &key, hash);
        found = result.node ? result.node : table.mHead;
        key.Destruct();
    }
    if (found != table.mHead) return static_cast<PathGroupNode *>(found)->group;
    uint32_t group = system->mNextPathGroup | 0x6f000000;
    system->mNextPathGroup = system->mNextPathGroup + 1;
    cRZString key;
    key.Construct(path);
    PathGroupInsertResult inserted;
    // 0x1d7390: the table's try_emplace for a path key.
    OriginalFunction<void(__thiscall *)(void *, PathGroupInsertResult *, const void *), 0x1d7390>()(&table, &inserted, &key);
    inserted.node->group = group;
    key.Destruct();
    return group;
}

// Slot 49, 0x1e4c00 (ret 4): over the segments in the vector `segments` (begin, end; the end is read once) while every
// one works: a segment with the rollback face and rollback switched off is flushed (its result decides); one with
// rollback on only has its path read (the string is dropped). Returns the flag (al).
// 49, 0x1e4c00, ret 4 (lane CT)
uint8_t cTSPersistSystem::FlushRollbackSegments(void *const *segments)
{
    Segment *const *it = static_cast<Segment *const *>(segments[0]);
    Segment *const *end = static_cast<Segment *const *>(segments[1]);
    bool ok = true;
    for (; it != end && ok; ++it) {
        ifaces::cITSPersistDBSegmentWithRollback *rollback = nullptr;
        if (Query(*it, kIidRollback, &rollback)) {
            if (!rollback->IsRollbackEnabled()) {
                ok = rollback->FlushChanges() != 0;
            } else {
                cRZString path;
                path.Construct();
                rollback->GetSegment()->GetPath(&path);
                path.Destruct();
            }
        }
        ReleaseIf(rollback);
    }
    return ok;
}

// Slot 33, 0x1deb30 (ret 0xc): FindSegment's choice, made over every segment of the base list (count and element by
// index) instead of a group's list.
// 33, 0x1deb30, ret 0xc (lane CT)
uint8_t cTSPersistSystem::FindBestSegment(const uint32_t *key, uint32_t iid, void **out)
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    auto *resources = As<ifaces::cIGZPersistResourceManager>(system);
    auto *its = As<ifaces::cITSPersistSystem>(ITSFace());
    uint32_t count = resources->GetSegmentCount();
    ifaces::cIGZDBSegmentPackedFile *packed = nullptr;
    Segment *best = nullptr;
    uint32_t bestScore = 0;
    uint8_t bestIsPacked = 0;
    for (uint32_t i = 0; i < count; ++i) {
        Segment *segment = resources->GetSegmentAt(i);
        uint32_t score = its->ScoreSegment(segment, KeyOf(key));
        if (packed) {
            ifaces::cIGZDBSegmentPackedFile *old = packed;
            packed = nullptr;
            ReleaseObject(old);
        }
        uint8_t isPacked = Query(segment, kIidPackedFile, &packed);
        if (score > bestScore || (bestIsPacked == 0 && score == bestScore && isPacked != 0)) {
            best = segment;
            bestScore = score;
            bestIsPacked = isPacked;
        }
    }
    uint8_t result = 0;
    if (best) result = best->QueryInterface(iid, out);
    ReleaseIf(packed);
    return result;
}

namespace {

// mNamedMap emptied when it is not (inlined in 0x1dcc60 and 0x1ea760): its nodes freed by 0x1d5e50 (ours:
// appctor::DestroyListNodes), the head linked to itself, the size 0, every bucket iterator set to the head (0x15b5e0 is
// the toolchain's std::fill).
void EmptyNamedMap(cGZPersistHashMap &named)
{
    if (named.size == 0) return;
    appctor::DestroyListNodes(&named.head, reinterpret_cast<appctor::ListNode *>(named.head));
    named.head->next = named.head;
    named.head->prev = named.head;
    named.size = 0;
    cGZPersistListNode *head = named.head;
    std::fill(reinterpret_cast<cGZPersistListNode **>(named.buckets), reinterpret_cast<cGZPersistListNode **>(named.bucketsEnd), head);
}

} // namespace

// Slot 42, 0x1dcc60: under the segment lock: 0x1e8ce0 and 0x1e8c00 (library clears) on the two segment tables, every segment
// of the vector at +0xd4 is held, removed (slot 30) and shut down (the walk starts again from the vector's first
// element each time, up to the end read at the start), the vector is cleared (0x176f00, original), and a non-empty
// mNamedMap is emptied (its nodes freed by 0x1d5e50, the buckets refilled with the head by 0x15b5e0, both original).
// 42, 0x1dcc60, ret 0 (lane CT)
void cTSPersistSystem::CloseAllSegments()
{
    cTSPersistSystem *system = this;
    LockScope lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    // 0x1e8ce0 is the toolchain's unordered_map::clear (each value destroyed by 0x1e6a40, its node freed).
    reinterpret_cast<GroupSegmentMap &>(system->mSegmentsByGroup).clear();
    // 0x1e8c00 is the toolchain's unordered_map<id, pointer>::clear (the ranges are not freed here).
    reinterpret_cast<std::unordered_map<uint32_t, DwordRange *> *>(&system->mSegmentIdRanges)->clear();
    auto *resources = As<ifaces::cIGZPersistResourceManager>(system);
    Segment **end = system->mSegmentsEnd;
    Segment **it = system->mSegmentsBegin;
    if (it != end) {
        do {
            Segment *segment = *it;
            if (segment) segment->AddRef();
            resources->RemoveSegment(segment);
            segment->Shutdown();
            it = system->mSegmentsBegin;
            if (segment) segment->Release();
        } while (it != end);
    }
    reinterpret_cast<RefPtrVector *>(&system->mSegmentsBegin)->Clear();
    EmptyNamedMap(system->mNamedMap);
}

// cIGZSystemService slot 9, 0x1e53c0 (this = primary + 4): saves the group map (0x1ea190), stops the flush timer
// (0x1c9842), tells the listeners event 1, releases them all, then clears the keyed objects (0x1e8c80), shuts down and
// drops the speculative loader, 0x1f42b0, ReleaseSegments, 0x1dedf0, the two type handlers, the set at +0x23c, the
// background thread (exited when running, deleted), unregisters the hooks face from the service object (slot 8), and
// ends with the base class's Shutdown (0x9b57e0).
// cIGZSystemService Shutdown, 0x1e53c0 (443 B) (lane CT)
bool cTSPersistSystem::Shutdown()
{
    cTSPersistSystem *system = this;
    system->SaveGroupMap(); // 0x1ea190, ours since lane IP
    stopwatch_more::Stop(&system->mFlushTimer, nullptr);
    EventBlock block;
    system->NotifyListeners(1, block.Address());
    ifaces::PersistSystemListener **listeners = system->mListenersBegin;
    ifaces::PersistSystemListener **listenersEnd = system->mListenersEnd; // read once
    if (listeners != listenersEnd) {
        for (; listeners != listenersEnd; ++listeners) {
            if (*listeners) ReleaseObject(*listeners);
        }
        system->mListenersEnd = system->mListenersBegin;
    }
    // 0x1e8c80 is the toolchain's unordered_map<key, counted object>::clear (the nodes' objects are released).
    reinterpret_cast<std::unordered_map<uint32_t, CountedObject> *>(&system->mKeyedObjects)->clear();
    ifaces::cITSPersistSpeculativeLoader *loader = system->mSpeculativeLoader;
    if (loader) {
        loader->Shutdown();
        loader = system->mSpeculativeLoader;
        if (loader) {
            system->mSpeculativeLoader = nullptr;
            loader->Release();
        }
    }
    OriginalFunction<void(__cdecl *)(), 0x1f42b0>()();
    system->ReleaseSegments();
    FlushDirtyRecords(system);
    ::cIGZUnknown *handler = system->mTypeHandler2026960b;
    if (handler) {
        system->mTypeHandler2026960b = nullptr;
        handler->Release();
    }
    handler = system->mTypeHandler4d45f8e6;
    if (handler) {
        system->mTypeHandler4d45f8e6 = nullptr;
        handler->Release();
    }
    auto *head = static_cast<SetHeadNode *>(system->mSet23CHead);
    OriginalFunction<void(__thiscall *)(void *, void *, void *), 0x1d5aa0>()(&system->mSet23CHead, &system->mSet23CHead, head->parent);
    head->parent = head;
    head->left = head;
    head->right = head;
    system->mSet23CSize = 0;
    if (system->mBackgroundThread) {
        if (system->mBackgroundThread->IsRunning()) system->mBackgroundThread->Exit(0);
        ::operator delete(system->mBackgroundThread, 4);
        system->mBackgroundThread = nullptr;
    }
    // Slot 8 of the service object gets the hooks face (null for a null object); its interface is not identified.
    // (the object is cached in a global the first time, as the original does)
    void *&cachedService = *GameAddress<void *>(0x10f5194);
    if (!cachedService) cachedService = GetServiceObject();
    void *service = cachedService;
    void *hooks = system ? system->HooksFace() : nullptr;
    VirtualCall<void(__thiscall *)(void *, void *), 8>(service, hooks);
    return RawBool(reinterpret_cast<::cGZPersistResourceManager *>(ServiceFace())->Shutdown()); // the base's (0x9b57e0, ours)
}

// ---------------------------------------------------------------------------------------------------------
// cITSPersistSystem slot 43, OpenContentPaths (lane ES).

namespace {

// A std::list<std::string> node (0x20 bytes) with the game's allocator-service allocator (0x1e7190 / 0x197100).
struct PathNode {
    PathNode *next;
    PathNode *prev;
    std::string text; // +0x08
};
static_assert(sizeof(PathNode) == 0x20, "path list node");

// The list's header (the sentinel node and the size), as cTSAppCommonImpl slot 5 fills it.
struct PathList {
    PathNode *head;
    uint32_t size;
};

// A node of the category map at +0x23c (std::map<int, ...>): links, colour, nil flag, key at +0x10, value at +0x14.
struct CategoryNode {
    CategoryNode *left;
    CategoryNode *parent;
    CategoryNode *right;
    uint8_t color;
    uint8_t isNil;
    uint8_t pad[2];
    int32_t mask;
    uint32_t value;
};
static_assert(offsetof(CategoryNode, mask) == 0x10 && offsetof(CategoryNode, value) == 0x14, "category node");

// map::find(mask): the address of the mapped value, or null when the mask is absent.
void *FindCategory(cTSPersistSystem *system, int32_t mask)
{
    auto *head = static_cast<CategoryNode *>(system->mSet23CHead);
    CategoryNode *bound = head;
    for (CategoryNode *node = head->parent; !node->isNil;) {
        if (node->mask < mask) {
            node = node->right;
        } else {
            bound = node;
            node = node->left;
        }
    }
    if (bound == head || mask < bound->mask) return nullptr;
    return &bound->value;
}

// The content path ids of cTSAppCommonImpl (slots 4, 5 and 6 take them).
constexpr uint32_t kNeighborhoodTemplatePath = 0x4c4324c5;
constexpr uint32_t kNeighborhoodPath = 0x2bcfce83;
constexpr uint32_t kLastCategoryPath = 0x4e36c5a3;
constexpr uint32_t kAllPaths = 0xffffffffu;
constexpr uint32_t kPackFlag = 0x20000; // 0xb39360 returns it; the lists are asked for without it and then with it

// The application's directory list for `pathId`: slot 5 (pathId, list, 1, which, mask) adds its entries.
void ReadPaths(PathList &list, uint32_t pathId, uint32_t which, uint32_t mask)
{
    appinit::CommonImplSlot5(pathId, Word(&list), 1, which, mask);
}

// Slot 6 of the application object (0x1d0cc0, not replaced): whether it has a list for `pathId`.
bool HasPaths(uint32_t pathId)
{
    return OriginalFunction<uint8_t(__cdecl *)(uint32_t), 0x1d0cc0>()(pathId) != 0;
}

// std::list::clear: the nodes are destroyed (0x1d5ea0) and the sentinel points at itself again.
void ClearPaths(PathList &list)
{
    appdebugger::DestroyStringListNodes(reinterpret_cast<appdebugger::ListHeader *>(&list), list.head);
    list.head->next = list.head;
    list.head->prev = list.head;
    list.size = 0;
}

// 0x1e3c70 (ours since lane FQ, OpenContentFolder): opens the segments of one content directory.
uint8_t AddContentPath(cTSPersistSystem *system, uint32_t mode, cRZString *path, uint32_t flag, void *category)
{
    return system->OpenContentFolder(mode, path, flag, category, nullptr, nullptr);
}

// Offers every entry of the list, last first, to AddContentPath; the result is the AND of the answers. With `shared`
// the text is assigned to that string each time, else a fresh string is made per entry (as the original's two forms).
uint8_t OfferPaths(cTSPersistSystem *system, PathList &list, cRZString *shared, uint32_t mode, uint32_t flag, void *category)
{
    uint8_t result = 1;
    if (list.head->next == list.head) return result;
    PathNode *current = list.head;
    do {
        PathNode *node = current->prev;
        if (shared) {
            shared->mString.assign(node->text.c_str());
            result &= AddContentPath(system, mode, shared, flag, category);
        } else {
            cRZString path;
            path.Construct(node->text.c_str());
            result &= AddContentPath(system, mode, &path, flag, category);
            path.Destruct();
        }
        current = node;
    } while (current != list.head->next);
    return result;
}

} // namespace

// Slot 43, 0x1dae90 (7597 B, ret 4; lane ES): OpenContentPaths. Under lock B: +0x174 = 1; AgeCaches (slot 39) when
// called on the owner thread; 0x1ea760 (original); +0xb8..+0xc4 zeroed. Then for each nibble of `categoryMask` that is
// set (0xf, 0xf0, ... 0xf00000) a fixed sequence of directory lists from cTSAppCommonImpl (slot 5: key, list, 1,
// which, mask; slot 6: has the key; slot 4: one path) whose entries, last first, go to 0x1e3c70 (original, thiscall
// on the primary: mode, path, flag, category value (the map at +0x23c), 0, 0). The 0xf00 nibble asks the command line
// for -templatedev / -nhdev and then adds the neighbourhood paths (and calls slot 44 (0xf00, "Neighborhoods")). Then
// 0x1e9eb0 (original), word +0x174 = 0x100. Returns the byte AND of every 0x1e3c70 result (starting at 1).
uint8_t cTSPersistSystem::OpenContentPaths(uint32_t categoryMask)
{
    cTSPersistSystem *system = this;
    cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    system->mOpeningContent = 1;
    uint32_t threadId;
    nGZLog::StoreCurrentThreadId(&threadId);
    if (threadId == system->mOwnerThreadId) VirtualCall<void(__thiscall *)(void *), 39>(ITSFace()); // AgeCaches
    system->ParkOpenSegments(); // 0x1ea760, ours since lane IP
    for (uint32_t &field : system->mFieldB8) field = 0;

    PathList list{nullptr, 0};
    list.head = static_cast<PathNode *>(appdebugger::AllocateListNodes(1, 0));
    list.head->next = list.head;
    list.head->prev = list.head;

    uint8_t result = 1;
    cRZString shared; // the original's reused string locals
    auto offer = [&](cRZString *text, uint32_t mode, uint32_t flag, void *category) {
        result &= OfferPaths(system, list, text, mode, flag, category);
    };
    auto read = [&](uint32_t pathId, uint32_t which, uint32_t mask) { ReadPaths(list, pathId, which, mask); };
    auto clear = [&]() { ClearPaths(list); };
    // The common pass: the user's entries (which = 1) when the id has a list, then the game's (which = 0).
    auto pass = [&](uint32_t pathId, bool clearFirst, uint32_t flag, void *category, uint32_t mask) {
        if (HasPaths(pathId)) {
            if (clearFirst) clear();
            read(pathId, 1, mask);
            offer(&shared, 2, flag, category);
        }
        clear();
        read(pathId, 0, mask);
        offer(&shared, 2, flag, category);
    };

    const uint32_t low = categoryMask & 0xf;
    if (low) {
        void *category = FindCategory(system, 0xf);
        clear();
        read(0x313fa8b2, 0, kAllPaths);
        offer(nullptr, 2, 1, category);
        shared.Construct();
        pass(0x2bd0234d, false, 1, category, kAllPaths); // the original does not empty the list before the user entries
        shared.Destruct();
        clear();
        read(0xd431eedd, 0, kAllPaths);
        offer(nullptr, 2, 1, category);
        shared.Construct();
        pass(0x2f46701b, false, 1, category, kAllPaths);
        shared.Destruct();
        shared.Construct();
        pass(0x2bd051f9, true, 1, category, kAllPaths);
        clear();
        read(0xf431eeea, 0, kAllPaths);
        offer(nullptr, 2, 1, category);
        clear();
        read(0x3505f6b5, 0, kAllPaths);
        offer(nullptr, 2, 1, category);
        shared.Destruct();
        shared.Construct();
        pass(0xac4f3639, true, 1, category, kAllPaths);
        shared.Destruct();
        shared.Construct();
        pass(0x2c891701, true, 1, category, kAllPaths);
        shared.Destruct();
        clear();
        read(0x313fa8b2, 0, kAllPaths);
        offer(nullptr, 2, 1, category);
    }

    if (categoryMask & 0xf0) {
        void *category = FindCategory(system, 0xf0);
        if (low != 0xf) {
            shared.Construct();
            pass(0x8c4f2dba, true, 1, category, kAllPaths);
            shared.Destruct();
            shared.Construct();
            pass(0xcbe2f142, true, 1, category, kAllPaths);
            clear();
            read(0xf431eeea, 0, kAllPaths);
            offer(nullptr, 2, 1, category);
            shared.Destruct();
        }
        shared.Construct();
        pass(0xcdd17ed5, true, 0, category, kAllPaths);
        shared.Destruct();
        for (uint32_t pathId : {0xee5c0210u, 0xec9f78aau, 0x2e5c023eu}) {
            clear();
            read(pathId, 0, kAllPaths);
            offer(nullptr, 2, 0, category);
        }
        shared.Construct();
        if (HasPaths(0xcc4368f3)) { // both reads without emptying the list in between
            clear();
            read(0xcc4368f3, 1, ~kPackFlag);
            offer(&shared, 2, 0, category);
            read(0xcc4368f3, 1, kPackFlag);
            offer(&shared, 2, 0, category);
        }
        clear();
        read(0xcc4368f3, 0, ~kPackFlag);
        offer(&shared, 2, 0, category);
        read(0xcc4368f3, 0, kPackFlag);
        offer(&shared, 2, 0, category);
        shared.Destruct();
    }

    if (categoryMask & 0xf00) {
        void *category = FindCategory(system, 0xf00);
        void *commandLine = VirtualCall<void *(__thiscall *)(void *), 21>(GetServiceObject());
        using SwitchFn = uint8_t(__thiscall *)(void *, cIGZString *);
        cRZString name;
        name.Construct("templatedev");
        const uint8_t templateDev = VirtualCall<SwitchFn, 9>(commandLine, name.AsInterface());
        name.Destruct();
        name.Construct("nhdev");
        const uint8_t neighborhoodDev = VirtualCall<SwitchFn, 9>(commandLine, name.AsInterface());
        name.Destruct();
        if (templateDev || neighborhoodDev) {
            cRZString path;
            path.Construct();
            appinit::CommonImplSlot4(kNeighborhoodTemplatePath, Word(&path), 1, 0, kAllPaths);
            result &= AddContentPath(system, 1, &path, 1, category);
            VirtualCall<void(__thiscall *)(void *, int32_t, const char *), 44>(ITSFace(), 0xf00, "Neighborhoods");
            path.Destruct();
        }
        cRZString path;
        path.Construct();
        appinit::CommonImplSlot4(kNeighborhoodPath, Word(&path), 1, 0, kAllPaths);
        result &= AddContentPath(system, 1, &path, 1, category);
        path.Destruct();
    }

    if (categoryMask & 0xf000) {
        void *category = FindCategory(system, 0xf000);
        shared.Construct();
        pass(0xcc9f6e91, true, 1, category, kAllPaths);
        shared.Destruct();
        shared.Construct();
        if (HasPaths(0x6cbe1ca3)) {
            clear();
            read(0x6cbe1ca3, 1, ~kPackFlag);
            offer(&shared, 2, 1, category);
            clear();
            read(0x6cbe1ca3, 1, kPackFlag);
            offer(&shared, 2, 1, category);
        }
        clear();
        read(0x6cbe1ca3, 0, ~kPackFlag);
        offer(&shared, 2, 1, category);
        clear();
        read(0x6cbe1ca3, 0, kPackFlag);
        offer(&shared, 2, 1, category);
        shared.Destruct();
    }

    if (categoryMask & 0xf0000) {
        void *category = FindCategory(system, 0xf0000);
        shared.Construct();
        pass(0xccbe1c05, true, 1, category, kAllPaths);
        shared.Destruct();
    }

    if (categoryMask & 0xf00000) {
        void *category = FindCategory(system, 0xf00000);
        cRZString path;
        path.Construct();
        appinit::CommonImplSlot4(kLastCategoryPath, Word(&path), 1, 0, kAllPaths);
        result &= AddContentPath(system, 2, &path, 1, category);
        path.Destruct();
    }

    system->RelistParkedSegments(); // 0x1e9eb0, ours since lane IP
    system->mOpeningContent = 0;
    system->mContentOpened = 1;
    appdebugger::DestroyStringListNodes(reinterpret_cast<appdebugger::ListHeader *>(&list), list.head);
    FreeThroughAllocatorService(list.head, 1);
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// cIGZSystemService slot 8, Init (lane ES).

namespace {

// The key filters given to the caches: 8-byte objects (the game's vtable, a zero word).
constexpr uint32_t kSmallObjectKeyFilterVTable = 0xda5d20;
constexpr uint32_t kLongTermObjectKeyFilterVTable = 0xda5d38;
constexpr uint32_t kSimKeyFilterVTable = 0xda5d50;

struct KeyFilterObject {
    const void *vtable;
    uint32_t word;
};

void *NewKeyFilterObject(uint32_t vtable)
{
    auto *filter = static_cast<KeyFilterObject *>(::operator new(sizeof(KeyFilterObject)));
    if (!filter) return nullptr;
    filter->word = 0;
    filter->vtable = GameAddress(vtable);
    return filter;
}

// A generational cache strategy (0x78 bytes, constructor 0x1fb300, original) or the basic one (0x34; ours,
// gz/cGZPersistCacheStrategyBasic.h, since lane QQ).
::cIGZUnknown *NewCacheStrategy(bool generational)
{
    void *memory = ::operator new(generational ? 0x78 : cGZPersistCacheStrategyBasic::kObjectSize);
    if (!memory) return nullptr;
    using Ctor = ::cIGZUnknown *(__thiscall *)(void *);
    return generational ? OriginalFunction<Ctor, 0x1fb300>()(memory) : cGZPersistCacheStrategyBasic::Construct(memory);
}

// Stores a counted pointer in a field: AddRef the new one, then Release the old one (nothing when they are equal).
template <typename T> void AssignCounted(T *&field, ::cIGZUnknown *object)
{
    ::cIGZUnknown *old = reinterpret_cast<::cIGZUnknown *>(field);
    if (object == old) return;
    if (object) object->AddRef();
    field = reinterpret_cast<T *>(object);
    if (old) old->Release();
}

// Cache strategy slots used here: 5 SetKeyFilter, 20 a size (100 for the small-object cache), 21 a kind (1, 2).
void CacheSlot(::cIGZUnknown *cache, int slot, uint32_t value)
{
    if (slot == 5) VirtualCall<void(__thiscall *)(void *, uint32_t), 5>(cache, value);
    else if (slot == 20) VirtualCall<void(__thiscall *)(void *, uint32_t), 20>(cache, value);
    else VirtualCall<void(__thiscall *)(void *, uint32_t), 21>(cache, value);
}

// The primary's slot 41: adds a cache strategy.
void AddCacheStrategy(cTSPersistSystem *system, ::cIGZUnknown *cache)
{
    VirtualCall<void(__thiscall *)(void *, void *), 41>(system, cache);
}

// The primary's slot 20: RegisterFactory(factory class, resource type, handler).
void RegisterFactory(cTSPersistSystem *system, uint32_t factoryClass, uint32_t type, void *handler)
{
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void *), 20>(system, factoryClass, type, handler);
}

struct FactoryRegistration {
    uint32_t factoryClass;
    uint32_t type;
};

// The resource factories Init registers, in order (class 0xaba91d51 is the generic serializable factory).
constexpr FactoryRegistration kFactories[] = {
    {0xaba91de9, 0x54544142}, {0xcc3113e4, 0x2c310f46}, {0xac870753, 0x8c870733}, {0xac870757, 0x8c870743},
    {0xaba91d51, 0x4f424a44}, {0xaba91d51, 0x42434f4e}, {0xaba91d51, 0x43415252}, {0xaba91d51, 0x42484156},
    {0xaba91d51, 0x54524545}, {0xaba91d51, 0x4f424a66}, {0xaba91d51, 0x0c06b721}, {0xaba91d51, 0xabd0dc63},
    {0xaba91d51, 0xabcb5da4}, {0xaba91d51, 0x4e474248}, {0xaba91d51, 0x0bf999e7}, {0xaba91d51, 0x6c589723},
    {0xaba91d51, 0xcc2a6a34}, {0xaba91d51, 0xaace2efb}, {0xaba91d51, 0x46414d49}, {0xaba91d51, 0x46414d68},
    {0xaba91d51, 0xcc8a6a69}, {0xaba91d51, 0xac8a7a2e}, {0xaba91d51, 0x54415454}, {0xaba91d51, 0xac506764},
    {0xaba91d51, 0x53494d49}, {0xdb4cb1e8, 0x53545223}, {0xdb4cb1e8, 0x43545353}, {0xdb4cb1e8, 0x54544173},
    {0xaba91d51, 0xcd8b6498}, {0xaba91d51, 0x2db5c0f4}, {0xaba91d51, 0x6db6f410}, {0xaba91d51, 0x6e9c59cf},
    {0xaba91d51, 0x104f6a6e}, {0xaba91d51, 0x3053cf74}, {0xaba91d51, 0xb21be28b}, {0xdb4cb1e8, 0x46414d73},
};

// Creates `field` through the framework (the old object is cleared and released first).
template <typename T> void CreateThroughFramework(T *&field, uint32_t classId, uint32_t iid)
{
    void *framework = GetDirectorFrameworkObject();
    if (field) {
        auto *old = reinterpret_cast<::cIGZUnknown *>(field);
        field = nullptr;
        old->Release();
    }
    As<ifaces::cIGZCOM>(framework)->GetClassObject(classId, iid, reinterpret_cast<void **>(&field));
}

struct PathGroupEntry {
    const char *name;
    uint32_t group;
};

// The fixed group ids of the game's own package folders (Crc24 of the name | 0x7f000000), in Init's order.
constexpr PathGroupEntry kPathGroups[] = {
    {"uitext", 0x7fa7dc7c},   {"build", 0x7f606c7a},      {"live", 0x7f96c284},        {"cas", 0x7f549c7f},
    {"ecas", 0x7f8760bb},     {"credits", 0x7f52f7d1},    {"neighborhood", 0x7f3a262a}, {"options", 0x7f232750},
    {"Tutorial", 0x7fc66556}, {"cas_shared", 0x7f2128d7}, {"musictitles", 0x7ff20eac}, {"behavior", 0x7fe59fd0},
    {"global", 0x7fd46cd0},   {"eptext", 0x7fe10cb9},
};

} // namespace

// cIGZSystemService Init, 0x1e0d60 (4105 B; lane ES): caches and key filters, Version.sig, 36 factories, the type
// handlers and loader, the service hooks, the flush stopwatch, 14 fixed path groups. Returns 1.
bool cTSPersistSystem::Init()
{
    cTSPersistSystem *system = this;
    // (the base cIGZSystemService Init, 0x144910, only returns true)
    VirtualCall<void *(__thiscall *)(void *), 21>(GetServiceObject());
    OriginalFunction<void(__cdecl *)(), 0x1f3b70>()();

    ::cIGZUnknown *cache = NewCacheStrategy(true);
    AssignCounted(system->mGeneralCache, cache);
    AddCacheStrategy(system, cache);

    cache = NewCacheStrategy(true);
    AssignCounted(system->mLongTermCache, cache);
    CacheSlot(cache, 21, 2);
    CacheSlot(cache, 5, Word(NewKeyFilterObject(kLongTermObjectKeyFilterVTable)));
    AddCacheStrategy(system, cache);

    cache = NewCacheStrategy(true);
    AssignCounted(system->mSmallObjectCache, cache);
    CacheSlot(cache, 5, Word(NewKeyFilterObject(kSmallObjectKeyFilterVTable)));
    CacheSlot(cache, 20, 100);
    CacheSlot(cache, 21, 1);
    AddCacheStrategy(system, cache);

    ::cIGZUnknown *simCache = NewCacheStrategy(false); // held by a local reference until the end
    if (simCache) simCache->AddRef();
    CacheSlot(simCache, 5, Word(NewKeyFilterObject(kSimKeyFilterVTable)));
    AssignCounted(system->mSimCache, simCache);
    AddCacheStrategy(system, reinterpret_cast<::cIGZUnknown *>(system->mSimCache));

    // Version.sig in any of the application's data folders (path id 0xec43275c) switches ITS slot 19 on.
    PathList list{nullptr, 0};
    list.head = static_cast<PathNode *>(appdebugger::AllocateListNodes(1, 0));
    list.head->next = list.head;
    list.head->prev = list.head;
    ReadPaths(list, 0xec43275c, 0, kAllPaths);
    for (PathNode *node = list.head->next; node != list.head; node = node->next) {
        cRZString path;
        path.Construct(node->text.c_str());
        path.mString.append("Version.sig", 0xb);
        if (cRZFile::PathExists(*path.AsInterface())) {
            VirtualCall<void(__thiscall *)(void *, uint32_t), 19>(system->ITSFace(), 1);
            system->mVersionSigFound = 1;
            path.Destruct();
            break;
        }
        path.Destruct();
    }

    for (const FactoryRegistration &f : kFactories) RegisterFactory(system, f.factoryClass, f.type, nullptr);
    CreateThroughFramework(system->mTypeHandler2026960b, 0xdb5cb1ab, 0xa56b8e23);
    RegisterFactory(system, 0xdb5cb1ab, 0x2026960b, system->mTypeHandler2026960b);
    CreateThroughFramework(system->mTypeHandler4d45f8e6, 0xed45f777, 0xa56b8e23);
    RegisterFactory(system, 0xdb5cb1ab, 0x4d45f8e6, system->mTypeHandler4d45f8e6);
    CreateThroughFramework(system->mSpeculativeLoader, 0x8ccf489d, 0x6ccf488d);
    VirtualCall<void(__thiscall *)(void *), 3>(system->mSpeculativeLoader);

    // The cached service object (the one Shutdown also uses) gets this service (slot 9) and the hooks face (slot 7).
    void *&cachedService = *GameAddress<void *>(0x10f5194);
    if (!cachedService) cachedService = GetServiceObject();
    void *service = cachedService;
    VirtualCall<void(__thiscall *)(void *, void *), 9>(service, system ? static_cast<void *>(ServiceFace()) : nullptr);
    VirtualCall<void(__thiscall *)(void *, void *), 7>(service, system ? system->HooksFace() : nullptr);

    system->mFlushIntervalMs = 0;
    system->mFlushTimer.mMode = 4; // 0x1c981d: the mode, then the counts cleared (0x1c95dd)
    system->mFlushTimer.mStart = 0;
    system->mFlushTimer.mAccumulated = 0;
    system->mFlushTimer.Start();

    StdHashTable &table = HashTable(system->mPathGroups);
    for (const PathGroupEntry &entry : kPathGroups) {
        cRZString key;
        key.Construct(entry.name);
        PathGroupInsertResult inserted;
        OriginalFunction<void(__thiscall *)(void *, PathGroupInsertResult *, const void *), 0x1d7390>()(&table, &inserted, &key);
        inserted.node->group = entry.group;
        key.Destruct();
    }

    appdebugger::DestroyStringListNodes(reinterpret_cast<appdebugger::ListHeader *>(&list), list.head);
    FreeThroughAllocatorService(list.head, 1);
    if (simCache) simCache->Release();
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// cITSPersistSystem slot 44, AddCategoryPath (lane ES).

namespace {

// An element of a category list: a cRZString by value (the game's vtable, the text, reference count 0).
struct CategoryString {
    explicit CategoryString(const char *name) : vtable(GameAddress<const void>(cRZString::kVTable)), text(name) {}
    const void *vtable;
    std::string text;
    uint32_t refCount = 0;
};
static_assert(sizeof(CategoryString) == sizeof(cRZString), "category string is a cRZString");

// The map at +0x23c: category mask -> the names added for it (std::map / std::list, MSVC 14.40 like the game's).
using CategoryMap = std::map<int32_t, std::list<CategoryString>>;
static_assert(sizeof(CategoryMap) == 8, "category map: head, size");

} // namespace

// Slot 44, 0x1da360 (669 B, ret 8; lane ES): AddCategoryPath. A null `name` does nothing. Otherwise the category map
// at +0x23c gets an (empty) entry for `categoryMask` if it has none, and a lowercased cRZString copy of `name` is
// appended to that entry's list. No lock is taken.
void cTSPersistSystem::AddCategoryPath(int32_t categoryMask, const char *name)
{
    if (!name) return;
    auto &categories = *reinterpret_cast<CategoryMap *>(&this->mSet23CHead);
    std::list<CategoryString> &names = categories[categoryMask];
    CategoryString &added = names.emplace_back(name);
    reinterpret_cast<cRZStringHelpers *>(&added)->LowerCase(); // 0x1ad167, ours
}

// ---------------------------------------------------------------------------------------------------------
// cITSPersistSystem slot 47, CollectPathGroups (lane ES).

namespace {

using GroupSet = std::set<uint32_t, std::less<uint32_t>, GZServiceAllocator<uint32_t>>; // the allocator service's nodes
static_assert(sizeof(GroupSet) == 8, "group set: head, size");

// A resource key as the segments list them: type, group, instance, resource.
struct ListedKey {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
    uint32_t resource;
};

} // namespace

// Slot 47, 0x1da600 (1050 B, ret 8; lane ES): CollectPathGroups. 0 when `path` is empty or DirectoryExists says no.
// Else under lock B (AgeCaches, slot 39, on the owner thread) 0x1e3c70 (original; mode 2, the path, flag 1, no
// category) opens the directory's segments into a vector; the lock is left. When that worked, each segment's
// resource key list (cIGZPersistDBSegment slot 19, GetResourceKeyList(list, null)) adds every key's group to
// `groups` (a std::set<uint32_t> whose nodes come from the allocator service). Every segment is released (slot 2).
// Returns 0x1e3c70's byte.
uint8_t cTSPersistSystem::CollectPathGroups(cIGZString *path, void *groups)
{
    cTSPersistSystem *system = this;
    if (path->Strlen() == 0) return 0;
    {
        const uint32_t length = path->Strlen();
        cRZString text;
        text.Construct();
        text.mString.assign(path->Data(), length);
        const bool exists = as2::DirectoryExists(&text);
        text.Destruct();
        if (!exists) return 0;
    }
    std::vector<void *> segments;
    uint8_t result;
    {
        cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
        uint32_t threadId;
        nGZLog::StoreCurrentThreadId(&threadId);
        if (threadId == system->mOwnerThreadId) VirtualCall<void(__thiscall *)(void *), 39>(ITSFace()); // AgeCaches
        const uint32_t length = path->Strlen();
        cRZString text;
        text.Construct();
        text.mString.assign(path->Data(), length);
        // Ours since lane FQ (0x1e3c70).
        result = system->OpenContentFolder(2, &text, 1, nullptr, &segments, nullptr);
        text.Destruct();
    }
    if (result) {
        auto &set = *static_cast<GroupSet *>(groups);
        for (void *segment : segments) {
            ServiceBlockVector keys{};
            VirtualCall<void(__thiscall *)(void *, ServiceBlockVector *, void *), 19>(segment, &keys, nullptr); // GetResourceKeyList
            for (auto *key = static_cast<ListedKey *>(keys.mFirst); key != keys.mLast; ++key) set.insert(key->group);
            // 0x195720's work, through the service's slot 4 as the original does (ServiceBlockVector::FreeBlock goes to
            // RZMemory::Free directly, the same thing in game but not through the service object).
            if (keys.mFirst) FreeThroughAllocatorService(keys.mFirst, 0);
        }
    }
    for (void *segment : segments)
        if (segment) VirtualCall<uint32_t(__thiscall *)(void *), 2>(segment); // Release
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Lane EW: OpenSegmentByInfo (ITS slot 20) and its helper 0x1e5260.

namespace {

// A node of mNamedMap (0x2c bytes): link, the lowercased path (a cRZString) at +8, the segment opened for it at +0x28.
struct NamedSegmentNode : StdHashNode {
    cRZString path;
    void *segment;
};
static_assert(sizeof(NamedSegmentNode) == 0x2c, "named segment node");

constexpr uint32_t kIidSegmentInfo = 0x656b8f0c;      // cIGZPersistDBSegment, as slot 23 asks for it
constexpr uint32_t kClassPackedFile = 0x9af5e892;    // the packed-file segment (a .package)
constexpr uint32_t kClassDirectoryFiles = 0x9af5e881; // the directory segment (a folder of files)
constexpr uint32_t kOptionContentRoot = 0x4c4324c5;  // cTSAppCommonImpl option: the folder below which files are packages

// The segment-info object (cTSPersistDBSegmentInfo; its interface is not named in the method tables yet).
struct SegmentInfoCalls {
    static uint32_t Slot3(void *info) { return VirtualCall<uint32_t(__thiscall *)(void *), 3>(info); }
    static cIGZString *Directory(void *info) { return VirtualCall<cIGZString *(__thiscall *)(void *), 4>(info); }
    static cIGZString *Name(void *info) { return VirtualCall<cIGZString *(__thiscall *)(void *), 5>(info); }
    static uint32_t Type(void *info) { return VirtualCall<uint32_t(__thiscall *)(void *), 6>(info); }
    static uint32_t Group(void *info) { return VirtualCall<uint32_t(__thiscall *)(void *), 7>(info); }
    static uint8_t HasFlags(void *info, uint32_t mask) { return VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), 9>(info, mask); }
};

// Packed-file slot 26 (0x68): sets the group the file's resources belong to (the value carries bit 16).
void SetPackedGroup(void *packed, uint32_t value)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t), 26>(packed, value);
}

} // namespace

// 0x1e5260 (__cdecl): when the segment answers the packed-file query, its group becomes `group` | 0x10000. True when
// it answered; a null segment gives false.
uint8_t __cdecl PersistSetPackedFileGroup(void *segment, uint16_t group)
{
    if (!segment) return 0;
    cIGZUnknown *packed = nullptr;
    Query(segment, kIidPackedFile, &packed);
    uint8_t answered;
    if (!packed) {
        answered = 0;
    } else {
        SetPackedGroup(packed, static_cast<uint32_t>(group) | 0x10000);
        answered = 1;
    }
    ReleaseIf(packed);
    return answered;
}

// Slot 20, 0x1ddb10 (1740 B, ret 0xc; lane EW): OpenSegmentByInfo. Null info: 0. Under lock B: the type (info slot
// 6; 0 asks 0x1c2269 whether the folder is below the content-root option: 1, else +0xf0; slot 6 returning 3 with
// +0xf0 == 1 makes it 1), the path (folder slot 4 + name slot 5, ".package"/".files" when info flag 1, a trailing
// separator for type 3), looked up lowercased in mNamedMap. Found: the segment is reopened (slot 6 (1, writable by
// 0x1c2182)), its packed group set (0x1e5260) when flag 2 and writable, and queried for iid. Else the file or folder
// is made (types 1 / 2 / 3), the segment class 0x9af5e892 / 0x9af5e881 created through the framework COM, set up
// (0x1e7230, 0x1dab60 for packages), added to the primary (slot 27) and opened when open, and queried for iid.
uint8_t cTSPersistSystem::OpenSegmentByInfo(uint32_t iid, void **out, void *info)
{
    if (!info) return 0;
    cTSPersistSystem *system = this;
    cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    uint32_t type = SegmentInfoCalls::Type(info);
    if (type == 0) {
        // No type: a package when the folder is below the content root option, else the system's default type.
        cRZString root;
        root.Construct();
        appinit::CommonImplSlot4(kOptionContentRoot, Word(&root), 1, 0, 0xffffffffu);
        cIGZString *directory = SegmentInfoCalls::Directory(info);
        const uint32_t length = directory->Strlen();
        const char *chars = directory->Data();
        cRZString folder;
        folder.Construct();
        folder.mString.assign(chars, length);
        cRZFileName::NormaliseSeparators(&folder, '/');
        const char *folderChars = folder.ToChar();
        // 0x1c2269 (__cdecl, original): whether the first path is a leading path of the second.
        const bool below = OriginalFunction<bool(__cdecl *)(const char *, const char *), 0x1c2269>()(root.ToChar(), folderChars);
        type = below ? 1 : system->mFieldF0;
        folder.Destruct();
        root.Destruct();
    }
    if (SegmentInfoCalls::Type(info) == 3 && system->mFieldF0 == 1) type = 1;

    // The file's path: the folder, the name, the extension of the type when flag 1 is set; a folder gets a separator.
    cRZString path;
    {
        cIGZString *directory = SegmentInfoCalls::Directory(info);
        const uint32_t length = directory->Strlen();
        const char *chars = directory->Data();
        path.Construct();
        path.mString.assign(chars, length);
    }
    {
        const char *name = SegmentInfoCalls::Name(info)->ToChar();
        path.mString.append(name, std::strlen(name));
    }
    if (SegmentInfoCalls::HasFlags(info, 1)) {
        // The extensions are the constant strings at 0xda62b0 (".package") and 0xda62bc (".files").
        std::string extension = type == 1 ? ".package" : type == 2 ? ".files" : "";
        path.mString.append(extension.c_str(), extension.size());
    }
    if (type == 3) {
        auto *helpers = reinterpret_cast<cRZStringHelpers *>(&path);
        if (!helpers->EndsWith(GameAddress<const char>(0xda52b4), 1, 1) && !helpers->EndsWith(GameAddress<const char>(0xda31d4), 1, 1))
            path.mString.append(GameAddress<const char>(0xda52b4), 1);
    }

    // The table of open files by lowercased path (backslashes).
    cRZString key;
    key.Construct();
    key.mString.assign(path.mString.c_str(), path.mString.size());
    cRZFileName::NormaliseSeparators(&key, '\\');
    reinterpret_cast<cRZStringHelpers *>(&key)->LowerCase();
    StdHashTable &table = HashTable(system->mNamedMap);
    StdHashBucket &bucket = table.mBuckets[nGZLog::HashChars(key.ToChar()) & table.mMask];
    StdHashNode *head = table.mHead;
    StdHashNode *found = head;
    StdHashNode *node = bucket.first;
    if (node != head) {
        for (;;) {
            const std::string &text = static_cast<NamedSegmentNode *>(node)->path.mString;
            if (text.size() == key.mString.size() && std::memcmp(text.data(), key.mString.data(), text.size()) == 0) {
                found = node;
                break;
            }
            if (node == bucket.last) break;
            node = node->next;
        }
    }

    uint8_t result;
    if (found != table.mHead) {
        // Already open: reopen it for reading, writing when the file can be written.
        void *segment = static_cast<NamedSegmentNode *>(found)->segment;
        // 0x1c2182 (__cdecl, original): whether the file can be opened for writing (made when missing, then removed).
        const uint8_t writable = OriginalFunction<uint8_t(__cdecl *)(cRZString *), 0x1c2182>()(&path);
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 6>(segment, 1, writable); // Open(read, write)
        if (SegmentInfoCalls::HasFlags(info, 2) && writable) PersistSetPackedFileGroup(segment, static_cast<uint16_t>(SegmentInfoCalls::Group(info)));
        result = Query(segment, iid, out);
    } else {
        uint32_t classId = 0;
        uint8_t setGroup = 0;
        if (type == 1) {
            setGroup = SegmentInfoCalls::HasFlags(info, 2) || (system->mFieldB5 && !cRZFile::PathExists(*path.AsInterface()));
            // 0x1c116e (__cdecl, original): opens the file, creating it when missing (no truncation), and closes it.
            OriginalFunction<uint8_t(__cdecl *)(cRZString *, uint8_t), 0x1c116e>()(&path, 0);
            classId = kClassPackedFile;
        } else if (type == 2) {
            appbase::CreateDirectories(&path);
            classId = kClassDirectoryFiles;
        } else if (type == 3) {
            // 0x1c5031 (ours, lane LY): DirectoryExists, else CreateDirectories.
            folders::EnsureDirectory(&path);
            classId = kClassDirectoryFiles;
        }
        cIGZUnknown *created = nullptr;
        if (classId == 0) {
            result = 0;
        } else {
            void *com = GetDirectorFrameworkObject();
            // The original tests al (the interface table types the result as a dword).
            if (!static_cast<uint8_t>(As<ifaces::cIGZCOM>(com)->GetClassObject(classId, kIidKeyStatus, &created))) {
                result = 0;
            } else {
                auto segmentOf = [&created]() { return VirtualCall<void *(__thiscall *)(void *), 3>(created); };
                void *segment = segmentOf();
                void *primary = system;
                VirtualCall<void(__thiscall *)(void *, uint32_t), 17>(segment, VirtualCall<uint32_t(__thiscall *)(void *), 47>(primary));
                cRZString name;
                name.Construct(*SegmentInfoCalls::Name(info));
                void *opened = segmentOf();
                const uint32_t slot3 = SegmentInfoCalls::Slot3(info);
                // Ours (lane FL): sets the segment up for the path (0x1e7230).
                system->SetUpSegment(&path, &name, slot3, type, opened);
                name.Destruct();
                // 0x1dab60 (__cdecl, original): the packed file's three standard key filters.
                if (type == 1) OriginalFunction<void(__cdecl *)(void *), 0x1dab60>()(created);
                if (VirtualCall<uint8_t(__thiscall *)(void *), 3>(segmentOf())) { // IsOpen
                    VirtualCall<uint32_t(__thiscall *)(void *, void *), 27>(primary, segmentOf()); // the primary's AddSegment
                    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 6>(segmentOf(), 1, 1);
                    if (setGroup) {
                        const uint16_t group = static_cast<uint16_t>(SegmentInfoCalls::Group(info));
                        cIGZUnknown *asSegment = nullptr;
                        Query(created, kIidSegmentInfo, &asSegment);
                        if (asSegment) {
                            cIGZUnknown *packed = nullptr;
                            Query(asSegment, kIidPackedFile, &packed);
                            if (packed) {
                                SetPackedGroup(packed, static_cast<uint32_t>(group) | 0x10000);
                                ReleaseIf(packed);
                            }
                        }
                        ReleaseIf(asSegment);
                    }
                }
                result = Query(created, iid, out);
            }
        }
        ReleaseIf(created);
    }
    key.Destruct();
    path.Destruct();
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Lane FD: AddPackageFile (ITS slot 46).

// Slot 46, 0x1e3180 (1186 B, ret 4; lane FD): AddPackageFile. Under a cRZLock on lock B: the cRZFileName of `path` is
// compared (0x1c8365) with each listed segment's; the walk stops at the first one that DIFFERS (ORIGINAL-BUG), which
// gets 0x1e9b50 and the result 1. When every segment matched (or there is none): an existing file with the extension
// ".package" becomes a packed-file segment (framework COM 0x9af5e892), set up like OpenSegmentByInfo's (slot 17, 0x1e7230
// with the lowercased name, 0x1dab60) and, when its slot 3 says so, added (primary slot 27) and opened (1, writable by
// 0x1c2182): 1. Otherwise 0.
uint8_t cTSPersistSystem::AddPackageFile(cIGZString *path)
{
    cTSPersistSystem *system = this;
    cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    cRZFileName wanted;
    {
        cRZString text;
        text.Construct(*path);
        wanted.Construct(&text);
        text.Destruct();
    }
    // 0x1c8365 (thiscall on a cRZFileName, ret 8, original): compares two file names (other, 0); 0 when they are the same.
    using CompareFileNamesFn = int32_t(__thiscall *)(cRZFileName *, cRZFileName *, uint32_t);
    Segment **it = system->mSegmentsBegin;
    Segment **const end = system->mSegmentsEnd;
    for (; it != end; ++it) {
        cRZString text;
        text.Construct();
        (*it)->GetPath(&text);
        cRZFileName listed;
        listed.Construct(&text);
        const int32_t differs = OriginalFunction<CompareFileNamesFn, 0x1c8365>()(&wanted, &listed, 0);
        listed.Destruct();
        text.Destruct();
        // ORIGINAL-BUG: the walk stops at the first segment whose file name differs from `path` (a test for "the same"
        // would need differs == 0), so with two or more segments listed one of them is taken as the file's.
        if (differs != 0) break;
    }
    uint8_t result = 0;
    if (it != system->mSegmentsEnd) {
        // 0x1e9b50 (thiscall on the primary, ret 4, original): the segment's groups into mSegmentsByGroup.
        system->IndexSegmentGroups(*it);
        result = 1;
    } else if (cRZFile::PathExists(*path)) {
        cRZString *extension = wanted.GetExtension();
        cRZString packageExtension;
        packageExtension.Construct(".package");
        const int32_t order = extension->CompareTo(*packageExtension.AsInterface(), false);
        packageExtension.Destruct();
        if (order == 0) {
            cIGZUnknown *created = nullptr;
            void *com = GetDirectorFrameworkObject();
            // The original tests al (the interface table types the result as a dword).
            if (static_cast<uint8_t>(As<ifaces::cIGZCOM>(com)->GetClassObject(kClassPackedFile, kIidKeyStatus, &created))) {
                // 0x1c2182 (__cdecl, original): whether the file can be opened for writing.
                const uint8_t writable = OriginalFunction<uint8_t(__cdecl *)(cIGZString *), 0x1c2182>()(path);
                cRZString name;
                name.Construct(*wanted.GetName(), 0, 0xffffffffu);
                reinterpret_cast<cRZStringHelpers *>(&name)->LowerCase(); // 0x1ad167, ours
                auto segmentOf = [&created]() { return VirtualCall<void *(__thiscall *)(void *), 3>(created); };
                void *segment = segmentOf();
                VirtualCall<void(__thiscall *)(void *, uint32_t), 17>(segment, VirtualCall<uint32_t(__thiscall *)(void *), 47>(system));
                cRZString full;
                full.Construct(*path);
                void *opened = segmentOf();
                // Ours (lane FL): sets the segment up for the path (0x1e7230).
                system->SetUpSegment(&full, &name, 0, 1, opened);
                full.Destruct();
                // 0x1dab60 (__cdecl, original): the packed file's three standard key filters.
                OriginalFunction<void(__cdecl *)(void *), 0x1dab60>()(created);
                if (VirtualCall<uint8_t(__thiscall *)(void *), 3>(segmentOf())) {
                    VirtualCall<uint32_t(__thiscall *)(void *, void *), 27>(system, segmentOf()); // the primary's AddSegment
                    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 6>(segmentOf(), 1, writable); // Open
                    result = 1;
                }
                name.Destruct();
            }
            ReleaseIf(created);
        }
    }
    wanted.Destruct();
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Lane FD: the resource names at primary +0x1b8 (ITS slots 54 and 55).

namespace {

// The names the cGroupNameTranslationProvider face hands out: segment id -> resource type -> (group, instance) -> a
// counted cRZString. Three MSVC std::maps (every node 0x1c bytes: key at +0x10, value at +0x14, or +0x18 under the pair
// key), filled per segment by 0x1e8fd0 from the segment's name-map resources.
using ResourceNamesByKey = std::map<std::pair<uint32_t, uint32_t>, cRZString *>;
using ResourceNamesByType = std::map<uint32_t, ResourceNamesByKey>;
using ResourceNames = std::map<uint32_t, ResourceNamesByType>;
static_assert(sizeof(ResourceNames) == 8, "std::map is its head and size");

ResourceNames &NamesOf(cTSPersistSystem *system)
{
    return *reinterpret_cast<ResourceNames *>(&system->mTreeHead);
}

// The names of a segment, read in (LoadResourceNames, 0x1e8fd0) when the segment has none yet; null when it still has
// none.
ResourceNamesByType *SegmentNames(cTSPersistSystem *system, Segment *segment)
{
    ResourceNames &names = NamesOf(system);
    const uint32_t id = segment->GetSegmentID();
    auto it = names.find(id);
    if (it == names.end()) {
        system->LoadResourceNames(segment);
        it = names.find(id);
        if (it == names.end()) return nullptr;
    }
    return &it->second;
}

void AddRefString(cRZString *s)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(s);
}

} // namespace

// The resource names at primary +0x1b8 (segment id -> type -> (group, instance) -> counted cRZString; three std::maps,
// a segment's filled in by 0x1e8fd0, original, when it has none). Lane FD.
// Slot 54, 0x1e4de0 (319 B, ret 0xc): SetResourceName. The segment's names (0 when there are none even after
// 0x1e8fd0); the entry for key[0] then (key[1], key[2]) (made when absent) takes a new cRZString copy of `name`
// (AddRef; the old one released). Returns 1.
uint8_t cTSPersistSystem::SetResourceName(void *segment, const uint32_t *key, cIGZString *name)
{
    ResourceNamesByType *types = SegmentNames(this, static_cast<Segment *>(segment));
    if (!types) return 0;
    ResourceNamesByKey &byKey = (*types)[key[0]];
    // operator new (0x20) and cRZString(const cIGZString &); the original's null test after it never fires.
    cRZString *text = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    text->Construct(*name);
    cRZString *&slot = byKey[std::make_pair(key[1], key[2])];
    cRZString *const old = slot;
    if (text != old) {
        AddRefString(text);
        slot = text;
        if (old) VirtualCall<uint32_t(__thiscall *)(void *), 2>(old);
    }
    return 1;
}

// Slot 55, 0x1e21b0 (636 B, ret 0xc): GetResourceName. A segment with the directory-files face (0x856c8cf7): its
// slot 17 (key, &path) gives a file path whose name part (cRZFileName) becomes a new cRZString in *out (AddRef), 1.
// Any other segment: its names (filled in by 0x1e8fd0 when absent) for key[0] and (key[1], key[2]) into *out
// (AddRef), 1. Else 0.
uint8_t cTSPersistSystem::GetResourceName(void *segment, const uint32_t *key, cIGZString **out)
{
    cRZString path;
    path.Construct();
    cIGZUnknown *directory = nullptr;
    uint8_t result = 0;
    if (Query(segment, kIidDirectoryFiles, &directory)) {
        // Slot 17 of the directory-files face: the path of the file that holds the key's record.
        if (VirtualCall<uint8_t(__thiscall *)(void *, const uint32_t *, cRZString *), 17>(directory, key, &path)) {
            cRZFileName file;
            file.Construct(&path);
            cRZString *name = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
            name->Construct(*file.GetName(), 0, 0xffffffffu);
            *out = name->AsInterface();
            AddRefString(name);
            file.Destruct();
            result = 1;
        }
    } else if (ResourceNamesByType *types = SegmentNames(this, static_cast<Segment *>(segment))) {
        auto byType = types->find(key[0]);
        if (byType != types->end()) {
            auto entry = byType->second.find(std::make_pair(key[1], key[2]));
            if (entry != byType->second.end()) {
                *out = entry->second->AsInterface();
                AddRefString(entry->second);
                result = 1;
            }
        }
    }
    path.Destruct();
    ReleaseIf(directory);
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Lane EW: CopyResource (ITS slot 37), CopySegmentResources (slot 38), IsSegmentWritable (slot 28).

namespace {

// The scratch buffer of 0x1dd4f0 (size, heap block, 0x800 bytes in place): the first request below 0x800 bytes uses the
// bytes in place, a larger first one a recorded heap block, any later one an unrecorded heap block. Release is 0x1de280.
struct ScratchBuffer {
    uint32_t size = 0xffffffffu;
    uint8_t *heap = nullptr;
    uint8_t local[0x800];

    uint8_t *Get(uint32_t n)
    {
        if (size == 0xffffffffu) {
            size = n;
            if (n < sizeof(local)) return local;
            heap = static_cast<uint8_t *>(::operator new[](n));
            return heap;
        }
        return static_cast<uint8_t *>(::operator new[](n));
    }
    void Release(uint8_t *p)
    {
        if (p == local) {
            size = 0xffffffffu;
        } else if (p == heap) {
            ::operator delete[](heap);
            heap = nullptr;
            size = 0xffffffffu;
        } else {
            ::operator delete[](p);
        }
    }
    ~ScratchBuffer()
    {
        if (size != 0xffffffffu && heap) ::operator delete(heap);
    }
};

// 0x1c2182 (__cdecl, original): whether the file can be opened for writing (made when missing, then removed).
uint8_t CanWriteFile(cRZString *path)
{
    return OriginalFunction<uint8_t(__cdecl *)(cRZString *), 0x1c2182>()(path);
}

} // namespace

// Slot 37, 0x1dd4f0 (681 B, ret 0x10; lane EW; `this` unused): copies the resource `key` of `source` to `newKey` of
// `target`. Both packed files: the raw bytes (source slots 41 compressed, 27 size, 28 read; target 29 write, 42 the
// flags), else through records (source slot 22 open, target 23 create, record 10/8 and 9, 6 close, target 24 commit or
// 25 abandon). A 0x800-byte stack buffer serves small sizes. Returns 1 when written.
uint8_t cTSPersistSystem::CopyResource(const void *key, void *source, const void *newKey, void *target)
{
    ScratchBuffer scratch;
    cIGZUnknown *targetPacked = nullptr;
    cIGZUnknown *sourcePacked = nullptr;
    uint8_t result = 0;
    if (Query(target, kIidPackedFile, &targetPacked) && Query(source, kIidPackedFile, &sourcePacked)) {
        // Packed file to packed file: the stored bytes as they are.
        const uint8_t compressed = VirtualCall<uint8_t(__thiscall *)(void *, const void *), 41>(sourcePacked, key);
        uint32_t size = VirtualCall<uint32_t(__thiscall *)(void *, const void *), 27>(sourcePacked, key);
        void *database = VirtualCall<void *(__thiscall *)(void *), 3>(sourcePacked);
        const uint32_t extra = VirtualCall<uint32_t(__thiscall *)(void *, const void *), 21>(database, key);
        if (size != 0xffffffffu) {
            uint8_t *buffer = scratch.Get(size);
            if (VirtualCall<uint32_t(__thiscall *)(void *, const void *, void *, uint32_t *), 28>(sourcePacked, key, buffer, &size) != 0 &&
                VirtualCall<uint32_t(__thiscall *)(void *, const void *, void *, uint32_t), 29>(targetPacked, newKey, buffer, size) != 0) {
                VirtualCall<uint32_t(__thiscall *)(void *, const void *, uint32_t, uint32_t), 42>(targetPacked, newKey, compressed, extra);
                result = 1;
            }
            scratch.Release(buffer);
        }
    } else {
        // Through records: read the source's, write a new one in the target.
        cIGZUnknown *record = nullptr;
        if (VirtualCall<uint8_t(__thiscall *)(void *, const void *, void *, uint32_t), 22>(source, key, &record, 1)) {
            cIGZUnknown *output = nullptr;
            if (VirtualCall<uint8_t(__thiscall *)(void *, const void *, void *), 23>(target, newKey, &output)) {
                const uint32_t n = VirtualCall<uint32_t(__thiscall *)(void *), 10>(record);
                uint8_t *buffer = scratch.Get(n);
                VirtualCall<uint32_t(__thiscall *)(void *, void *, uint32_t), 8>(record, buffer, n);
                VirtualCall<uint32_t(__thiscall *)(void *, void *, uint32_t), 9>(output, buffer, n);
                scratch.Release(buffer);
                VirtualCall<uint32_t(__thiscall *)(void *), 6>(record);
                if (VirtualCall<uint8_t(__thiscall *)(void *, void *), 24>(target, output)) {
                    result = 1;
                } else {
                    VirtualCall<uint32_t(__thiscall *)(void *, void *), 25>(target, output);
                }
            }
            ReleaseIf(output);
        }
        ReleaseIf(record);
    }
    ReleaseIf(sourcePacked);
    ReleaseIf(targetPacked);
    return result;
}

// Slot 38, 0x1dd7a0 (552 B, ret 0x10; lane EW): every key of `source` (slot 19 with `filter`) copied to `target`
// through slot 37, the key mapped by `mapper`'s slot 3 when given; target slot 10 and 1 when all worked.
uint8_t cTSPersistSystem::CopySegmentResources(void *target, void *source, void *mapper, void *filter)
{
    ServiceBlockVector keys{};
    uint8_t result = 0;
    if (VirtualCall<uint32_t(__thiscall *)(void *, ServiceBlockVector *, void *), 19>(source, &keys, filter) != 0) {
        const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(Word(keys.mLast) - Word(keys.mFirst)) >> 4);
        using CopyFn = uint8_t(__thiscall *)(void *, const void *, void *, const void *, void *);
        bool copied = true;
        if (mapper) {
            for (uint32_t i = 0; i < count; ++i) {
                const ListedKey *key = static_cast<const ListedKey *>(keys.mFirst) + i;
                ListedKey mapped{};
                VirtualCall<void(__thiscall *)(void *, const void *, void *), 3>(mapper, key, &mapped);
                if (!VirtualCall<CopyFn, 37>(ITSFace(), key, source, &mapped, target)) {
                    copied = false;
                    break;
                }
            }
        } else {
            cIGZUnknown *targetSegment = nullptr;
            Query(target, kIidKeyStatus, &targetSegment);
            const uint32_t targetGroup = targetSegment ? VirtualCall<uint32_t(__thiscall *)(void *), 4>(targetSegment) : 0;
            cIGZUnknown *sourceSegment = nullptr;
            Query(source, kIidKeyStatus, &sourceSegment);
            const uint32_t sourceGroup = sourceSegment ? VirtualCall<uint32_t(__thiscall *)(void *), 4>(sourceSegment) : 0;
            const uint32_t tagged = sourceGroup & targetGroup & 0x6f000000;
            for (uint32_t i = 0; i < count; ++i) {
                const ListedKey *key = static_cast<const ListedKey *>(keys.mFirst) + i;
                ListedKey mapped = *key;
                // ORIGINAL-BUG: a key of the source's path group (both groups path-tagged) gets the source's group again,
                // not the target's: the copy keeps its group.
                if (tagged == 0x6f000000 && key->group == sourceGroup) mapped.group = sourceGroup;
                if (!VirtualCall<CopyFn, 37>(ITSFace(), key, source, &mapped, target)) {
                    copied = false;
                    break;
                }
            }
            ReleaseIf(sourceSegment);
            ReleaseIf(targetSegment);
        }
        if (copied) {
            VirtualCall<uint32_t(__thiscall *)(void *), 10>(target);
            result = 1;
        }
    }
    // 0x195720's work, through the allocator service's slot 4 as the original does.
    if (keys.mFirst) FreeThroughAllocatorService(keys.mFirst, 0);
    return result;
}

// Slot 28, 0x1e1d70 (585 B, ret 4; lane EW): a packed file is writable when its database's slot 8 says so or 0x1c2182
// can write its path; a directory segment when its listing (0x1f58f0..0x1f6090) is empty or a file in it (kind & 0xf
// != 4) can be written, or no file was checked. Neither face: 0.
uint8_t cTSPersistSystem::IsSegmentWritable(void *segment)
{
    cIGZUnknown *packed = nullptr;
    cIGZUnknown *directory = nullptr;
    uint8_t result;
    if (Query(segment, kIidPackedFile, &packed)) {
        cRZString path;
        path.Construct();
        VirtualCall<void(__thiscall *)(void *, cRZString *), 8>(packed, &path); // GetPath
        void *database = VirtualCall<void *(__thiscall *)(void *), 3>(packed);
        result = VirtualCall<uint8_t(__thiscall *)(void *), 8>(database) || CanWriteFile(&path);
        path.Destruct();
    } else if (!Query(segment, kIidDirectoryFiles, &directory)) {
        result = 0;
    } else {
        cRZString path;
        path.Construct();
        VirtualCall<void(__thiscall *)(void *, cRZString *), 8>(directory, &path); // GetPath
        {
            ScopedDirectoryListing listing(&path, this);
            if (!listing->First()) {
                result = 1;
            } else {
                uint32_t unwritable = 0;
                bool writable = false;
                do {
                    alignas(cRZString) char storage[sizeof(cRZString)];
                    auto *name = reinterpret_cast<cRZString *>(storage);
                    listing->EntryPath(name);
                    if ((listing->EntryKind() & 0xf) != 4) {
                        if (CanWriteFile(name)) {
                            name->Destruct();
                            writable = true;
                            break;
                        }
                        ++unwritable;
                    }
                    name->Destruct();
                } while (listing->Next());
                result = writable || unwritable == 0;
            }
        }
        path.Destruct();
    }
    ReleaseIf(directory);
    ReleaseIf(packed);
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Lane EZ: the primary's AddSegment override (slot 28).

namespace {

// The value mNamedMap's insert (0x1d84b0) copies into a new node: the key and the segment (the node's +8..+0x2c).
struct NamedSegmentEntry {
    cRZString path;
    void *segment;
};
static_assert(sizeof(NamedSegmentEntry) == 0x24, "named segment entry");

// 0x1c1931 (rz/PathAttributes.h, ours since lane MA): tests the file attributes of a path against a mode mask (mode 8: a folder, or no
// attributes at all).
uint8_t PathHasAttributes(cRZString *path, uint32_t mode)
{
    return PathAttributesMatch(path->AsInterface(), mode);
}

} // namespace

// Not reimplemented yet (lane CN handoff, declared only): cTSPersistSystem overrides of base slots.
// 28 (cIGZPersistResourceManager AddSegment family), 0x1e36f0 (607 B), ret 4
uint8_t cTSPersistSystem::vf28(void *segmentObject)
{
    cTSPersistSystem *system = this;
    auto *segment = static_cast<Segment *>(segmentObject);
    const uint8_t result = system->Base()->vf28(segment);
    cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    cRZString path;
    path.Construct();
    segment->GetPath(&path);
    const uint8_t writable = PathHasAttributes(&path, 8) || segment->IsWritable() || CanWriteFile(&path);
    segment->AutoOpen(1, writable);

    Segment **it = system->mSegmentsBegin;
    Segment **end = system->mSegmentsEnd;
    while (it != end && *it != segment) ++it;
    if (it == end) {
        // A new segment: the next id, a counted entry in the segment vector, and its lowercased path in mNamedMap.
        uint32_t id = system->mFieldC8++;
        segment->SetSegmentID(&id);
        Segment *held = segment;
        held->AddRef();
        Segment **last = system->mSegmentsEnd; // read again
        if (last != system->mSegmentsCap) {
            *last = segment;
            segment->AddRef();
            ++system->mSegmentsEnd;
        } else {
            reinterpret_cast<ServiceRefVector *>(&system->mSegmentsBegin)->InsertGrown(reinterpret_cast<void **>(last), reinterpret_cast<void **>(&held));
        }
        segment->Release();

        segment->GetPath(&path);
        cRZFileName::NormaliseSeparators(&path, '\\');
        reinterpret_cast<cRZStringHelpers *>(&path)->LowerCase();
        NamedSegmentEntry entry;
        entry.path.Construct();
        entry.path.mString.assign(path.mString.data(), path.mString.size());
        entry.segment = segment;
        void *inserted = nullptr;
        // 0x1d84b0 (thiscall on the map, ret 8, original): the multimap's emplace (it never replaces an entry).
        OriginalFunction<void *(__thiscall *)(void *, void **, NamedSegmentEntry *), 0x1d84b0>()(&system->mNamedMap, &inserted, &entry);
        entry.path.Destruct();
    }
    // 0x1e9b50 and 0x1e97b0 (thiscall on the primary, ret 4, original): the segment's groups into mSegmentsByGroup, and its
    // group list into mSegmentIdRanges.
    system->IndexSegmentGroups(segment);
    system->SegmentGroups(segment);
    path.Destruct();
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Lane EZ: the primary's RemoveSegment override (slot 30).

namespace {

// 0x1d6df0's work (__cdecl in the original: first, last, dest): moves the counted pointers [first, last) down to `dest`,
// as assignments of counted pointers (the incoming one AddRef'd, the one overwritten released, nothing when they are the
// same object or the same place). Returns the end of the moved range.
Segment **MoveCounted(Segment **first, Segment **last, Segment **dest)
{
    for (; first != last; ++first, ++dest) {
        if (first == dest) continue;
        Segment *incoming = *first;
        Segment *old = *dest;
        if (incoming == old) continue;
        if (incoming) incoming->AddRef();
        *dest = incoming;
        if (old) old->Release();
    }
    return dest;
}

// A vector of counted pointers' erase of one element: the rest moved down, the (re-read) last element released, the end
// moved back.
void EraseCounted(Segment **where, Segment **&last)
{
    MoveCounted(where + 1, last, where);
    Segment *tail = last[-1];
    if (tail) tail->Release();
    --last;
}

// Unlinks a node from one of the game's hash tables: its bucket (hash & mask) is fixed up, the size (+8) decremented and
// the node taken out of the list. The node is not freed.
void UnlinkHashNode(StdHashTable &table, StdHashNode *node, uint32_t hash)
{
    StdHashBucket &bucket = table.mBuckets[hash & table.mMask];
    if (bucket.last == node) {
        if (bucket.first == node) {
            bucket.first = table.mHead;
            bucket.last = table.mHead;
        } else {
            bucket.last = node->prev;
        }
    } else if (bucket.first == node) {
        bucket.first = node->next;
    }
    --table.mUnknown08;
    StdHashNode *next = node->next;
    node->prev->next = next;
    next->prev = node->prev;
}

// Takes `segment` out of the group lists of mSegmentsByGroup (a list left empty loses its node) for every group its id
// lists in mSegmentIdRanges, then the id's node itself (0x1d86f0's erase; the range it points at is not freed), and closes
// the segment when it is open.
void ForgetSegmentGroups(cTSPersistSystem *system, Segment *segment)
{
    uint32_t id = segment->GetSegmentID();
    StdHashTable &ids = HashTable(system->mSegmentIdRanges);
    StdHashFindResult found{};
    ids.Find32(&found, &id, Fnv1a32(id));
    StdHashNode *idNode = found.node ? found.node : ids.mHead;
    if (idNode == ids.mHead) return;
    DwordRange *range = static_cast<SegmentIdNode *>(idNode)->range;
    uint32_t *group = range->first;
    uint32_t *groupsEnd = range->last;
    StdHashTable &groups = HashTable(system->mSegmentsByGroup);
    for (; group != groupsEnd; ++group) {
        StdHashFindResult inGroup{};
        groups.Find32(&inGroup, group, *group);
        StdHashNode *node = inGroup.node ? inGroup.node : groups.mHead;
        if (node == groups.mHead) continue;
        auto *groupNode = static_cast<SegmentGroupNode *>(node);
        SegmentVector &list = groupNode->segments;
        for (Segment **it = list.first; it != list.last; ++it) {
            if (*it == segment) {
                EraseCounted(it, list.last);
                break;
            }
        }
        if (list.first == list.last) {
            UnlinkHashNode(groups, groupNode, groupNode->group);
            // 0x1e6a40: the (empty) vector's destructor; its block goes back through the allocator service.
            if (list.first) {
                for (Segment **it = list.first; it != list.last; ++it) ReleaseIf(*it);
                FreeThroughAllocatorService(list.first, static_cast<uint32_t>(list.end - list.first));
                list.first = list.last = list.end = nullptr;
            }
            ::operator delete(groupNode, sizeof(SegmentGroupNode));
        }
    }
    UnlinkHashNode(ids, idNode, Fnv1a32(static_cast<SegmentIdNode *>(idNode)->segmentId));
    ::operator delete(idNode, sizeof(SegmentIdNode));
    if (segment->IsOpen()) segment->Close();
}

// Erases mNamedMap's first entry for `key` (lowercased, backslashes) whose segment is `segment`, among the entries with
// that key (the bucket's equal range).
void ForgetNamedSegment(cTSPersistSystem *system, const cRZString &key, Segment *segment)
{
    StdHashTable &named = HashTable(system->mNamedMap);
    const std::string &text = key.mString;
    StdHashBucket &bucket = named.mBuckets[nGZLog::HashChars(key.ToChar()) & named.mMask];
    auto matches = [&text](StdHashNode *node) { return static_cast<NamedSegmentNode *>(node)->path.mString == text; };
    StdHashNode *head = named.mHead;
    StdHashNode *lo = head;
    StdHashNode *hi = head;
    for (StdHashNode *node = bucket.first; node != head;) {
        if (matches(node)) {
            lo = node;
            hi = node->next;
            for (StdHashNode *at = node; at != bucket.last;) {
                StdHashNode *next = hi;
                if (!matches(next)) break;
                hi = next->next;
                at = next;
            }
            break;
        }
        if (node == bucket.last) break;
        node = node->next;
    }
    if (lo == named.mHead || lo == hi) return;
    for (StdHashNode *node = lo; node != hi; node = node->next) {
        auto *entry = static_cast<NamedSegmentNode *>(node);
        if (entry->segment != segment) continue;
        UnlinkHashNode(named, entry, nGZLog::HashChars(entry->path.ToChar()));
        appctor::DestroyElement(&named.mHead, &entry->path);
        ::operator delete(entry, sizeof(NamedSegmentNode));
        return;
    }
}

} // namespace

// 30 RemoveSegment override, 0x1e57a0 (1563 B), ret 4
uint8_t cTSPersistSystem::vf30(void *segmentObject)
{
    cTSPersistSystem *system = this;
    auto *segment = static_cast<Segment *>(segmentObject);
    auto *lockB = As<ifaces::cIGZCriticalSection>(&system->mLockB);
    lockB->Lock();
    Segment **end = system->mSegmentsEnd;
    Segment **it = system->mSegmentsBegin;
    while (it != end && *it != segment) ++it;
    lockB->Unlock();
    if (it == end) return 0;

    VirtualCall<void(__thiscall *)(void *), 54>(system); // ClearSpeculativeLoader
    // The segment's resources leave the caches (the base slot 10 for every key it lists, when it is open).
    ServiceBlockVector keys{nullptr, nullptr, nullptr};
    if (segment->IsOpen() && segment->GetResourceKeyList(&keys, nullptr) != 0) {
        for (uint32_t key = Word(keys.mFirst); key != Word(keys.mLast); key += 16) system->Base()->vf10(key);
    }
    cRZString path;
    path.Construct();
    segment->GetPath(&path);
    cRZString name;
    name.Construct();
    cRZFileName::SplitInto(&path, nullptr, nullptr, &name, nullptr);
    cRZFileName::NormaliseSeparators(&path, '\\');
    reinterpret_cast<cRZStringHelpers *>(&path)->LowerCase();
    // Every keyed object hears of the file's group (slot 9 with its address).
    uint32_t group = As<ifaces::cITSPersistSystem>(system->ITSFace())->GetGroupIdForPath(&name);
    StdHashNode *keyedHead = HashTable(system->mKeyedObjects).mHead;
    for (StdHashNode *node = keyedHead->next; node != keyedHead; node = node->next)
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t *), 9>(static_cast<KeyedObjectNode *>(node)->object, &group);

    uint8_t result;
    {
        cRZLock lock(lockB);
        Segment **last = system->mSegmentsEnd;
        Segment **found = system->mSegmentsBegin;
        while (found != last && *found != segment) ++found;
        if (found != last) {
            ForgetSegmentGroups(system, segment);
            EraseCounted(found, system->mSegmentsEnd);
            ForgetNamedSegment(system, path, segment);
        }
        result = system->Base()->vf30(segment);
    }
    name.Destruct();
    path.Destruct();
    // 0x195720's work, through the service's slot 4 as the original does.
    if (keys.mFirst) FreeThroughAllocatorService(keys.mFirst, 0);
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Lane EZ: DeleteGroupFiles (ITS slot 24).

namespace {

// A node of mMap154 (0x3c bytes): link, a cRZString key at +8 (the key 0x1e6e80 makes from a file's path), a vector of
// dwords at +0x30 (0x1d9a10 frees it at key+0x28); +0x28 and +0x2c are not read here.
struct PathKeyNode : StdHashNode {
    cRZString key;
    uint32_t rest[2];
    uint32_t *first;
    uint32_t *last;
    uint32_t *end;
};
static_assert(sizeof(PathKeyNode) == 0x3c, "mMap154 node");

// Erases mMap154's node for `key` (its value destroyed as 0x1d9a10 does: the vector's block, then the key).
void ForgetPathKey(cTSPersistSystem *system, cRZString &key)
{
    StdHashTable &table = HashTable(system->mMap154);
    StdHashFindResult found{};
    table.FindStringAt12(&found, &key, nGZLog::HashChars(key.ToChar()));
    StdHashNode *node = found.node ? found.node : table.mHead;
    if (node == table.mHead) return;
    auto *entry = static_cast<PathKeyNode *>(node);
    UnlinkHashNode(table, entry, nGZLog::HashChars(entry->key.ToChar()));
    if (entry->first) {
        std::allocator<uint32_t>().deallocate(entry->first, static_cast<size_t>(entry->end - entry->first));
        entry->first = entry->last = entry->end = nullptr;
    }
    entry->key.Destruct();
    ::operator delete(entry, sizeof(PathKeyNode));
}

} // namespace

// Slot 24, 0x1de470 (816 B, ret 8; lane EZ): DeleteGroupFiles. Under lock B, the segments of `group` (filled in when
// absent); none: 0. Over a counted copy of them: unless `onlyUnused`'s low byte is set and the segment's key-status face
// (0x3b2271bb) answers slot 6 with something other than 1, the primary's slot 30 (RemoveSegment), mMap154's entry for
// the path's key (0x1e6e80) erased, and the file (0x1c2e25) or folder (0x1c5fa0, recursive) deleted. Returns 1.
uint8_t cTSPersistSystem::DeleteGroupFiles(uint32_t group, uint32_t onlyUnused)
{
    cTSPersistSystem *system = this;
    cRZLock lock(As<ifaces::cIGZCriticalSection>(&system->mLockB));
    SegmentVector *list = LookupSegments(system, group);
    if (!list || list->first == list->last) return 0;
    // A counted copy of the list (0x1d89b0, original), as removing a segment changes the list.
    ServiceRefVector copy;
    OriginalFunction<void(__thiscall *)(void *, const void *), 0x1d89b0>()(&copy, list);
    void **end = copy.mLast;
    for (void **it = copy.mFirst; it != end; ++it) {
        auto *segment = static_cast<Segment *>(*it);
        cRZString path;
        path.Construct();
        segment->GetPath(&path);
        uint8_t remove = 1;
        if (static_cast<uint8_t>(onlyUnused)) {
            cIGZUnknown *status = nullptr;
            if (Query(segment, kIidKeyStatus, &status)) remove = VirtualCall<uint32_t(__thiscall *)(void *), 6>(status) == 1;
            ReleaseIf(status);
        }
        if (remove) {
            As<ifaces::cIGZPersistResourceManager>(system)->RemoveSegment(segment);
            cRZString key;
            key.Construct();
            system->MakePathKey(&path, &key);
            ForgetPathKey(system, key);
            // 0x1c5fa0 (folder, recursive) and 0x1c2e25 (file) (__cdecl, original): the deletes.
            if (PathHasAttributes(&path, 8)) {
                OriginalFunction<uint8_t(__cdecl *)(cRZString *, uint32_t), 0x1c5fa0>()(&path, 1);
            } else {
                PathDelete(path.AsInterface());
            }
            key.Destruct();
        }
        path.Destruct();
    }
    // 0x1e6a40: the copy's destructor (its elements released, the block back through the allocator service).
    if (copy.mFirst) {
        for (void **it = copy.mFirst; it != copy.mLast; ++it) ReleaseIf(*it);
        FreeThroughAllocatorService(copy.mFirst, static_cast<uint32_t>(copy.mEnd - copy.mFirst));
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Lane FK: primary slot 39 (OpenRecord) and its record-compare helper.

namespace {
constexpr uint32_t kShadowGroup = 0x6d1b65ee;     // the change-record group of RAM-shadowed records (ITS 58 / 59)
constexpr uint32_t kIidSegment = 0x656b8f0c;      // what the record's child (its segment) is asked for
constexpr uint32_t kShadowMaxSize = 0x4000;       // larger records are never copied into the store

using RecordSizeFn = uint32_t(__thiscall *)(void *);
using RecordReadFn = uint8_t(__thiscall *)(void *, void *, uint32_t);
constexpr int kRecordReadSlot = 8;   // cIGZPersistDBRecord: GetFieldVoid (raw read)
constexpr int kRecordSizeSlot = 10;  // GetSize
} // namespace

// 0x1dacb0 (__cdecl): true when both records have the same size and both raw reads of that many bytes succeed with the
// same bytes. One zeroed std::vector<uint8_t> of twice the size holds a's bytes, then b's.
uint8_t __cdecl CompareRecordContents(void *a, void *b)
{
    const uint32_t size = VirtualCall<RecordSizeFn, kRecordSizeSlot>(a);
    if (size != VirtualCall<RecordSizeFn, kRecordSizeSlot>(b)) return 0;
    std::vector<uint8_t> bytes(size + size);
    uint8_t *first = bytes.data();
    uint8_t same = 0;
    if (VirtualCall<RecordReadFn, kRecordReadSlot>(a, first, size) && VirtualCall<RecordReadFn, kRecordReadSlot>(b, first + size, size))
        same = std::memcmp(first, first + size, size) == 0;
    return same;
}

// Slot 39 (OpenRecord), 0x1e2850. With deferred writes on, a store (+0x84) and a read (mode's low byte 0), a record whose
// cached copy is current is read from the store into a cRAMShadowRecord, which is handed out instead; otherwise the
// record is opened as usual and, when at most 0x4000 bytes, copied into the store.
// Slot 39 OpenRecord override, 0x1e2850 (994 B, ret 0xc; lane FK). NotifyListeners(4, key) first and (5, key) last.
// Unless +0x22d and +0x84 are set and mode's low byte is 0: the base vf39 (key, record, mode). Otherwise ITS 58
// GetCachedResource(0x6d1b65ee, key, &obj) (obj released). Hit: the store's (+0x84) slot 22 OpenRecord(key, &rec, 1);
// on success primary slot 34 (key, &segment), a new cRAMShadowRecord (AddRef) sized to rec's GetSize, its +4 face's
// SetChild(segment), rec's raw read (slot 8) into it, rec Close, rec = the shadow's +4 face (counted move); with
// +0x22e set, a debug check: base vf39(key, &check, 0), CompareRecordContents(rec, check) (ignored), rec
// SeekAbsolute(0), check Close (no null check), check released; then the shadow and segment released. Miss, or
// OpenRecord failed: base vf39(key, &rec, 0); when it succeeds and rec's size is at most 0x4000: store slot 26
// DeleteRecord(key), store QueryInterface(0x286d4a6a) (unused), rec QueryChild(0x656b8f0c, &source) and then ITS 37
// (key, source, key, store); when that succeeds ITS 59 NoteResourceChange(0x6d1b65ee, key, &an empty cRZVariant).
// Returns 1 with *record = rec when rec is non-null (whatever the base returned), else 0.
uint8_t cTSPersistSystem::vf39(uint32_t keyWord, uint32_t recordWord, uint32_t mode)
{
    cTSPersistSystem *system = this;
    const void *key = Ptr<const void>(keyWord);
    void **record = Ptr<void *>(recordWord);
    const auto *resourceKey = static_cast<const ::cGZPersistResourceKey *>(key);
    auto *base = system->Base();
    uint8_t result = 0;
    NotifyListeners(4, keyWord);
    if (system->mDeferWrites == 0 || system->mField84 == 0 || static_cast<uint8_t>(mode) != 0) {
        result = base->vf39(keyWord, Word(record), mode);
        NotifyListeners(5, keyWord);
        return result;
    }

    ifaces::cIGZPersistDBRecord *opened = nullptr;
    void *cachedObject = nullptr;
    uint8_t cached = As<ifaces::cITSPersistSystem>(system->ITSFace())->GetCachedResource(kShadowGroup, const_cast<void *>(key), &cachedObject);
    ReleaseIf(cachedObject);
    if (cached) {
        // The cached copy is current: the store's record, copied into RAM.
        void *store = Ptr<void>(system->mField84);
        if (As<ifaces::cIGZPersistDBSegment>(store)->OpenRecord(resourceKey, &opened, 1)) {
            void *segment = nullptr;
            As<ifaces::cIGZPersistResourceManager>(system)->FindSegmentWithKey(resourceKey, &segment);
            cRAMShadowRecord *shadow = new cRAMShadowRecord(resourceKey);
            VirtualCall<uint32_t(__thiscall *)(void *), 1>(shadow); // AddRef
            const uint32_t size = opened->GetSize();
            shadow->mData.resize(size);
            auto *shadowRecord = reinterpret_cast<ifaces::cIGZPersistDBRecord *>(reinterpret_cast<uint8_t *>(shadow) + 4);
            VirtualCall<void(__thiscall *)(void *, void *), 16>(shadowRecord, segment); // SetChild
            VirtualCall<RecordReadFn, kRecordReadSlot>(opened, shadow->mData.data(), size);
            opened->Close();
            ifaces::cIGZPersistDBRecord *old = opened;
            if (shadowRecord != old) {
                shadowRecord->AddRef();
                opened = shadowRecord;
                if (old) old->Release();
            }
            if (system->mField22E) {
                // Debug check: the record as the segments have it, compared with the copy (the answer is unused).
                ifaces::cIGZPersistDBRecord *check = nullptr;
                base->vf39(keyWord, Word(&check), 0);
                CompareRecordContents(opened, check);
                opened->SeekAbsolute(0);
                check->Close(); // not null-checked
                if (check) check->Release();
            }
            VirtualCall<uint32_t(__thiscall *)(void *), 2>(shadow); // Release
            ReleaseIf(segment);
            cached = 2; // handed out (below)
        } else {
            cached = 0;
        }
    }
    if (cached != 2) {
        // Not cached (cached is always 0 here): the record as the segments have it, copied into the store when small.
        if (opened) {
            ifaces::cIGZPersistDBRecord *old = opened;
            opened = nullptr;
            old->Release();
        }
        if (base->vf39(keyWord, Word(&opened), 0) && opened->GetSize() <= kShadowMaxSize) {
            auto *store = As<ifaces::cIGZPersistDBSegment>(Ptr<void>(system->mField84));
            if (!cached) store->DeleteRecord(resourceKey);
            void *packed = nullptr;
            store->QueryInterface(kIidPackedFile, &packed);
            void *source = nullptr;
            if (VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, void **), 15>(opened, kIidSegment, &source)) { // QueryChild
                using CopyFn = uint8_t(__thiscall *)(void *, const void *, void *, const void *, void *);
                if (VirtualCall<CopyFn, 37>(system->ITSFace(), key, source, key, Ptr<void>(system->mField84))) {
                    cRZVariant empty;
                    As<ifaces::cITSPersistSystem>(system->ITSFace())
                        ->NoteResourceChange(kShadowGroup, resourceKey, Word(static_cast<cIGZVariant *>(&empty)));
                }
            }
            ReleaseIf(source);
            ReleaseIf(packed);
        }
    }
    if (opened) {
        result = 1;
        *record = opened;
    }
    NotifyListeners(5, keyWord);
    return result;
}

// ---------------------------------------------------------------------------------------------------------
// Lane FL: the segment set-up 0x1e7230 (SetUpSegment).

namespace {

constexpr uint32_t kIidSegmentGroup = 0xad095a01; // a segment's group face: slot 6 sets the group, slot 5 reads it back
constexpr uint32_t kNoGroup = 0xffffffffu;        // "group unknown": the segment's own records name its groups

// cTSAppCommonImpl path ids: folders below a content-root override take group 0; objects and SCObjects folders.
constexpr uint32_t kContentRootOverridePath = 0xcc4368f3;
constexpr uint32_t kObjectsPath = 0x2bd0234d;
constexpr uint32_t kSCObjectsPath = 0xd431eedd;

constexpr uint32_t kTypePathGroupNames = 0x4e524546; // 'NREF' records: a path group's name (the key's group is its id)
constexpr uint32_t kTypeGlobals = 0x474c4f42;        // 'GLOB' records: a group name at +0x40

// The key statuses a string-table package or folder gets (key-status face slot 12: type, status; the -1 type last).
constexpr uint32_t kTypeStringSet = 0x53545223;     // 'STR#'
constexpr uint32_t kTypeCatalogStrings = 0x43545353; // 'CTSS'
constexpr uint32_t kTypeTTAs = 0x54544173;           // 'TTAs'

// Game texts, read from the image: ".package" (8 characters), the folder names compared with path groups, the group
// names "Global" and "PersonGlobals", the extensions whose lengths are game globals, and "." (an extension's dot).
constexpr uint32_t kTextPackageExtension = 0xda62b0;
constexpr uint32_t kTextSCObjects = 0xda63f4, kTextObjects = 0xda6400;
constexpr uint32_t kTextGlobal = 0xda6408, kTextPersonGlobals = 0xda6410;
constexpr uint32_t kTextBundlePackage = 0xda5afc, kLengthBundlePackage = 0xff25f0;
constexpr uint32_t kTextStringPackage = 0xda5aec, kLengthStringPackage = 0xff257c;
constexpr uint32_t kTextStringFiles = 0xda5b0c, kLengthStringFiles = 0xff25ec;
constexpr uint32_t kTextDot = 0xd9f880;

// A type-3 folder's fixed extensions: 19 three-character texts, four bytes apart from 0xda6420, with these types.
constexpr uint32_t kTextTemplateExtensions = 0xda6420;
constexpr uint32_t kTemplateExtensionTypes[] = {0x25232b11, 0xe519c933, 0xfc6eb1f7, 0xc9c81b9b, 0xc9c81ba3, 0xc9c81ba9, 0xc9c81bad,
                                                0x6a97042f, 0xbca76ddc, 0x7ba3838c, 0xac4f8687, 0x49596978, 0x1c4a276c, 0xed534136,
                                                0xfb00791d, 0xfb00791e, 0x499743e5, 0x4d51f042, 0x6aede879};

// cGZPersistResourceKeyFilterByType (12 bytes, game vtable 0xda5cf0): accepts the keys of one type. Made as the game
// makes it, with reference count 0; the user AddRefs it.
struct TypeKeyFilter {
    static constexpr uint32_t kVTable = 0xda5cf0;
    const void *mVTable;
    uint32_t mRefCount;
    uint32_t mType;
};
static_assert(sizeof(TypeKeyFilter) == 0xc, "type key filter layout");

TypeKeyFilter *NewTypeKeyFilter(uint32_t type)
{
    auto *filter = static_cast<TypeKeyFilter *>(::operator new(sizeof(TypeKeyFilter)));
    filter->mRefCount = 0;
    filter->mVTable = GameAddress(TypeKeyFilter::kVTable);
    filter->mType = type;
    return filter;
}

// Room for a cRZString that a callee constructs (an out parameter in raw memory).
struct StringStorage {
    alignas(cRZString) char bytes[sizeof(cRZString)];
    cRZString *S() { return reinterpret_cast<cRZString *>(bytes); }
};

// An empty option path list (the sentinel from the allocator service), and its destruction (nodes, then sentinel).
PathList NewPathList()
{
    PathList list{nullptr, 0};
    list.head = static_cast<PathNode *>(appdebugger::AllocateListNodes(1, 0));
    list.head->next = list.head;
    list.head->prev = list.head;
    return list;
}

void FreePathList(PathList &list)
{
    appdebugger::DestroyStringListNodes(reinterpret_cast<appdebugger::ListHeader *>(&list), list.head);
    FreeThroughAllocatorService(list.head, 1);
}

// Whether an entry of the list is a leading path of `path` (0x1c2269, original), first entry first.
bool AnyLeadingPath(const PathList &list, const cRZString *path)
{
    for (const PathNode *node = list.head->next; node != list.head; node = node->next)
        if (OriginalFunction<bool(__cdecl *)(const char *, const char *), 0x1c2269>()(node->text.c_str(), path->ToChar())) return true;
    return false;
}

bool PathEndsWith(cRZString *path, uint32_t textRva, uint32_t lengthRva)
{
    return reinterpret_cast<cRZStringHelpers *>(path)->EndsWith(GameAddress<const char>(textRva), *GameAddress<uint32_t>(lengthRva), 1);
}

// mPathGroups lookup of a text (hash 0x1d4a70, find 0x1d5b50; original): whether it has an entry.
bool HasPathGroup(StdHashTable &table, const char *text)
{
    StdHashNode *const head = table.mHead;
    cRZString key;
    key.Construct(text);
    const uint32_t hash = OriginalFunction<uint32_t(__stdcall *)(const void *), 0x1d4a70>()(&key);
    StdHashFindResult result{};
    OriginalFunction<void(__thiscall *)(void *, StdHashFindResult *, const void *, uint32_t), 0x1d5b50>()(&table, &result, &key, hash);
    StdHashNode *const found = result.node ? result.node : table.mHead;
    key.Destruct();
    return found != head;
}

// mPathGroups' entry for a text, made when absent (the table's try_emplace 0x1d7390, original).
PathGroupNode *PathGroupEntry(StdHashTable &table, const char *text)
{
    cRZString key;
    key.Construct(text);
    PathGroupInsertResult inserted;
    OriginalFunction<void(__thiscall *)(void *, PathGroupInsertResult *, const void *), 0x1d7390>()(&table, &inserted, &key);
    key.Destruct();
    return inserted.node;
}

// The group of a segment that no content-root override covers: kNoGroup for a folder below an SCObjects path (only when
// "<name>.package" beside `path` has a nonzero length) or an objects path whose path group is `group`, and for a
// ".bundle.package" file; else `group`.
uint32_t FolderGroup(ifaces::cITSPersistSystem *its, cRZString *path, cRZString *name, uint32_t group)
{
    PathList objectsOverrides = NewPathList();
    PathList objects = NewPathList();
    PathList scObjectsOverrides = NewPathList();
    PathList scObjects = NewPathList();
    if (HasPaths(kObjectsPath)) {
        ReadPaths(objectsOverrides, kObjectsPath, 1, kAllPaths);
        ReadPaths(scObjectsOverrides, kSCObjectsPath, 1, kAllPaths);
    }
    ReadPaths(objects, kObjectsPath, 0, kAllPaths);
    ReadPaths(scObjects, kSCObjectsPath, 0, kAllPaths);

    cRZFile file;
    file.Construct();
    cRZFileName package;
    package.Construct(path);
    // 0x1c8b12 / 0x1c8abb (thiscall on a cRZFileName, ret 8, original): replace the name / the extension (flag 0: as is).
    OriginalFunction<uint8_t(__thiscall *)(cRZFileName *, cRZString *, uint32_t), 0x1c8b12>()(&package, name, 0);
    {
        cRZString extension;
        extension.Construct(GameAddress<const char>(kTextPackageExtension));
        OriginalFunction<uint8_t(__thiscall *)(cRZFileName *, cRZString *, uint32_t), 0x1c8abb>()(&package, &extension, 0);
        extension.Destruct();
    }
    {
        StringStorage full;
        package.GetFullPath(full.S());
        file.SetPath(*full.S()->AsInterface());
        full.S()->Destruct();
    }
    uint32_t length;
    {
        StringStorage full;
        package.GetFullPath(full.S());
        length = cRZFile::LengthOfPath(*full.S()->AsInterface());
        full.S()->Destruct();
    }
    bool inObjects = AnyLeadingPath(objects, path);
    bool inSCObjects = length != 0 && AnyLeadingPath(scObjects, path);
    if (!inObjects) inObjects = AnyLeadingPath(objectsOverrides, path);
    if (length != 0 && AnyLeadingPath(scObjectsOverrides, path)) inSCObjects = true;
    package.Destruct();
    file.Destruct();
    FreePathList(scObjects);
    FreePathList(scObjectsOverrides);
    FreePathList(objects);
    FreePathList(objectsOverrides);

    if (inSCObjects && its->GetGroupIdForPathChars(GameAddress<const char>(kTextSCObjects)) == group) return kNoGroup;
    if (inObjects && its->GetGroupIdForPathChars(GameAddress<const char>(kTextObjects)) == group) return kNoGroup;
    if (PathEndsWith(path, kTextBundlePackage, kLengthBundlePackage)) return kNoGroup;
    return group;
}

// A record's size (slot 10), read (slot 8: buffer, size; al), close (slot 6).
uint32_t RecordSize(void *record)
{
    return VirtualCall<uint32_t(__thiscall *)(void *), 10>(record);
}
uint8_t ReadRecord(void *record, void *buffer, uint32_t size)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, void *, uint32_t), 8>(record, buffer, size);
}
void CloseRecord(void *record)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 6>(record);
}

// The scan of a segment whose group is not known: each 'NREF' record's text becomes a path group with the key's group
// as its id (an existing entry takes the new id); each 'GLOB' record over 0x40 bytes names a group (from +0x40, or +0x41
// when its last byte is not 0) that is added with ITS slot 12's id when absent, and when there were any, "Global" and
// "PersonGlobals" the same way. A closed segment is initialised and opened for reading first, and closed afterwards.
void ReadPathGroupRecords(cTSPersistSystem *system, ifaces::cITSPersistSystem *its, Segment *segment)
{
    const uint8_t wasOpen = segment->IsOpen();
    if (!wasOpen) {
        segment->Init();
        segment->Open(1, 0);
    }
    StdHashTable &table = HashTable(system->mPathGroups);
    ServiceBlockVector keys{};
    TypeKeyFilter *filter = NewTypeKeyFilter(kTypePathGroupNames);
    AddRefIf(filter);
    if (segment->GetResourceKeyList(&keys, reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(filter)) != 0) {
        const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(Word(keys.mLast) - Word(keys.mFirst)) >> 4);
        for (uint32_t i = 0; i < count; ++i) {
            const ListedKey *key = static_cast<const ListedKey *>(keys.mFirst) + i;
            cIGZUnknown *record = nullptr;
            if (segment->OpenRecord(KeyOf(&key->type), &record, 1)) {
                const uint32_t size = RecordSize(record);
                char *text = static_cast<char *>(::operator new[](size + 1));
                text[size] = 0;
                if (ReadRecord(record, text, size)) PathGroupEntry(table, text)->group = key->group;
                ::operator delete[](text);
                CloseRecord(record);
            }
            ReleaseIf(record);
        }
    }
    if (keys.mFirst != keys.mLast) keys.mLast = keys.mFirst;

    TypeKeyFilter *globals = NewTypeKeyFilter(kTypeGlobals);
    AddRefIf(globals);
    ReleaseIf(filter);
    filter = globals;
    if (segment->GetResourceKeyList(&keys, reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(filter)) != 0) {
        const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(Word(keys.mLast) - Word(keys.mFirst)) >> 4);
        if (count != 0) {
            for (uint32_t i = 0; i < count; ++i) {
                const ListedKey *key = static_cast<const ListedKey *>(keys.mFirst) + i;
                cIGZUnknown *record = nullptr;
                if (segment->OpenRecord(KeyOf(&key->type), &record, 1)) {
                    const uint32_t size = RecordSize(record);
                    if (size > 0x40) {
                        char *data = static_cast<char *>(::operator new[](size + 1));
                        data[size] = 0;
                        if (ReadRecord(record, data, size)) {
                            const char *groupName = data + 0x40 + (data[size - 1] != 0 ? 1 : 0);
                            if (!HasPathGroup(table, groupName)) {
                                PathGroupNode *entry = PathGroupEntry(table, groupName);
                                entry->group = its->GetGroupIdForNameChars(groupName);
                            }
                        }
                        ::operator delete[](data);
                    }
                    CloseRecord(record);
                }
                ReleaseIf(record);
            }
            for (uint32_t textRva : {kTextGlobal, kTextPersonGlobals}) {
                const char *groupName = GameAddress<const char>(textRva);
                if (!HasPathGroup(table, groupName)) {
                    PathGroupNode *entry = PathGroupEntry(table, groupName);
                    entry->group = its->GetGroupIdForNameChars(groupName);
                }
            }
        }
    }
    if (!wasOpen) segment->Close();
    ReleaseIf(filter);
    // 0x195720's work, through the allocator service's slot 4 as the original does.
    if (keys.mFirst) FreeThroughAllocatorService(keys.mFirst, 0);
}

// The type of a listed file's extension (without its dot): up to four characters packed big-endian ('_' as '#'),
// longer ones as a number (hexparse::ParseHex32, 0x1b0e2c).
uint32_t ExtensionType(const char *text)
{
    if (std::strlen(text) > 4) return hexparse::ParseHex32(text);
    uint32_t type = 0;
    for (uint32_t i = 0; i < 4 && text[i] != 0; ++i) type = type << 8 | (text[i] == '_' ? uint32_t('#') : static_cast<uint8_t>(text[i]));
    return type;
}

// The directory-files face's slots used here: 10 the group, 14 an extension (type, string), 23 the file-name converter,
// 28 and 29 two switches.
void DirectorySetGroup(void *directory, uint32_t group)
{
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 10>(directory, group);
}
void DirectoryAddExtension(void *directory, uint32_t type, cRZString *extension)
{
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, cRZString *), 14>(directory, type, extension);
}

} // namespace

// Lane FL. 0x1e7230 (6463 B, non-virtual, ret 0x14): sets a new segment up for its file or folder `path`, whose file
// name is `name` (OpenSegmentByInfo, AddPackageFile and 0x1e3c70 call it before adding the segment). The group id is
// the ITS face's slot 11 (name); the segment gets SetPath (slot 12). Through its group face (0xad095a01): group 0 when
// the path is below a content-root override path (option 0xcc4368f3), else -1 for "SCObjects"/"objects" folders
// (options 0xd431eedd / 0x2bd0234d, the former only when "<name>.package" beside the path has a nonzero length) whose
// path group id is the segment's, and for ".bundle.package" files, else the group id; slot 6 sets it. When slot 5
// then answers -1 the segment (opened for reading if it was closed, and closed again) is scanned: its 'NREF' records
// name path groups (mPathGroups, the key's group) and its 'GLOB' records (the name at +0x40) add the groups of their
// names, then of "Global" and "PersonGlobals", through ITS slot 12 when absent. Then the key-status face (0x3b2271bb)
// gets `info` (slot 7) and the group id (slot 5), with four string-type statuses (slot 12) for ".string.package" and
// ".string.files"; the packed-file face (0x286d4a6a) slots 49 and 50 (1); the directory-files face (0x856c8cf7) slots
// 28 and 29 (1), then for type 3 the group 0x1c0532fa, 19 fixed extensions (slot 14) and a cTSSceneGraphFileNameTo-
// ResKeyConverter (slot 23), else the group id and one extension per file in the folder's listing (slot 14).
void cTSPersistSystem::SetUpSegment(cRZString *path, cRZString *name, uint32_t info, uint32_t type, void *segmentObject)
{
    cTSPersistSystem *system = this;
    auto *its = As<ifaces::cITSPersistSystem>(system->ITSFace());
    auto *segment = static_cast<Segment *>(segmentObject);
    const uint32_t group = its->GetGroupIdForPath(name);
    segment->SetPath(path);

    cIGZUnknown *groupFace = nullptr;
    if (Query(segment, kIidSegmentGroup, &groupFace)) {
        PathList overrides = NewPathList();
        PathList roots = NewPathList();
        bool belowRoot = false;
        if (HasPaths(kContentRootOverridePath)) {
            ReadPaths(overrides, kContentRootOverridePath, 1, kAllPaths);
            belowRoot = AnyLeadingPath(overrides, path);
        }
        if (!belowRoot) {
            ReadPaths(roots, kContentRootOverridePath, 0, kAllPaths);
            belowRoot = AnyLeadingPath(roots, path);
        }
        const uint32_t assigned = belowRoot ? 0 : FolderGroup(its, path, name, group);
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 6>(groupFace, assigned);
        if (VirtualCall<uint32_t(__thiscall *)(void *), 5>(groupFace) == kNoGroup) ReadPathGroupRecords(system, its, segment);
        FreePathList(roots);
        FreePathList(overrides);
    }

    cIGZUnknown *keyStatus = nullptr;
    if (Query(segment, kIidKeyStatus, &keyStatus)) {
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 7>(keyStatus, info);
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 5>(keyStatus, group);
        if (PathEndsWith(path, kTextStringPackage, kLengthStringPackage) || PathEndsWith(path, kTextStringFiles, kLengthStringFiles)) {
            using StatusFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
            VirtualCall<StatusFn, 12>(keyStatus, kTypeStringSet, 2);
            VirtualCall<StatusFn, 12>(keyStatus, kTypeCatalogStrings, 3);
            VirtualCall<StatusFn, 12>(keyStatus, kTypeTTAs, 3);
            VirtualCall<StatusFn, 12>(keyStatus, 0xffffffffu, 0);
        }
    }

    cIGZUnknown *packed = nullptr;
    if (Query(segment, kIidPackedFile, &packed)) {
        As<ifaces::cIGZDBSegmentPackedFile>(packed)->SetMatchFlagA(1);
        As<ifaces::cIGZDBSegmentPackedFile>(packed)->SetMatchFlagB(1);
    }

    cIGZUnknown *directory = nullptr;
    if (Query(segment, kIidDirectoryFiles, &directory)) {
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 28>(directory, 1);
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 29>(directory, 1);
        if (type == 3) {
            DirectorySetGroup(directory, kKeyTypeUseA3);
            if (keyStatus) VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 5>(keyStatus, kKeyTypeUseA3);
            for (uint32_t i = 0; i < sizeof(kTemplateExtensionTypes) / sizeof(kTemplateExtensionTypes[0]); ++i) {
                cRZString extension;
                extension.Construct(GameAddress<const char>(kTextTemplateExtensions + 4 * i));
                DirectoryAddExtension(directory, kTemplateExtensionTypes[i], &extension);
                extension.Destruct();
            }
            // 0x1f5f80 (thiscall, original): cTSSceneGraphFileNameToResKeyConverter's constructor (8 bytes, count 0).
            void *converter = OriginalFunction<void *(__thiscall *)(void *), 0x1f5f80>()(::operator new(8));
            VirtualCall<uint32_t(__thiscall *)(void *, void *), 23>(directory, converter);
        } else {
            DirectorySetGroup(directory, group);
            ScopedDirectoryListing listing(path, system);
            if (listing->First()) {
                do {
                    StringStorage entry;
                    listing->EntryPath(entry.S());
                    cRZString extension;
                    extension.Construct();
                    cRZFileName::SplitInto(entry.S(), nullptr, nullptr, nullptr, &extension);
                    if (extension.StartsWith(GameAddress<const char>(kTextDot), 1, true)) extension.Erase(0, 1);
                    DirectoryAddExtension(directory, ExtensionType(extension.ToChar()), &extension);
                    extension.Destruct();
                    entry.S()->Destruct();
                } while (listing->Next());
            }
        }
    }
    ReleaseIf(directory);
    ReleaseIf(packed);
    ReleaseIf(keyStatus);
    ReleaseIf(groupFace);
}

// ---------------------------------------------------------------------------------------------------------
// Lane FQ: the content-folder opener 0x1e3c70.

namespace {

// A std::list<cRZString> as the folder opener reads one (the category's folder names, the file-name filter): the
// sentinel node and the size; each node holds a cRZString by value at +8.
struct StringListNode {
    StringListNode *next;
    StringListNode *prev;
    cRZString text; // +0x08
};
struct StringList {
    StringListNode *head;
    uint32_t size;
};

// A node of the cRZString-keyed segment tables mMap178 and mMap198 (0x2c bytes, built by 0x1d8a50): link, the path key at
// +8, a counted segment at +0x28.
struct PathSegmentNode : StdHashNode {
    cRZString key;
    Segment *segment;
};
static_assert(sizeof(PathSegmentNode) == 0x2c, "path segment node");

// The listing's kind byte (0x1f65c0): FindFirst's directory flag gives 4, anything else 8.
constexpr uint8_t kListedFolder = 4;
constexpr uint8_t kListedFile = 8;

// Content folders whose segments are template directories (type 3) when found at the top level without an extension
// (the names at 0xda63b8 ... 0xda63dc), and the one that is a template directory anywhere (0xda63e8).
const char *const kTemplateFolderNames[] = {"CASFace", "LightRigs", "Objects", "Sims", "Textures"};
const char *const kThumbnailsFolderName = "Thumbnails";


// The key of a path (cTSPersistSystem::MakePathKey, 0x1e6e80).
void MakePathKey(cTSPersistSystem *system, cRZString *path, cRZString *key)
{
    system->MakePathKey(path, key);
}

// The node of `key` in mMap178 or mMap198, or null.
PathSegmentNode *FindPathSegment(cGZPersistHashMap &map, cRZString &key)
{
    StdHashTable &table = HashTable(map);
    StdHashFindResult found{};
    table.FindStringAt12(&found, &key, nGZLog::HashChars(key.ToChar()));
    StdHashNode *node = found.node ? found.node : table.mHead;
    return node == table.mHead ? nullptr : static_cast<PathSegmentNode *>(node);
}

// cRZString::CompareTo against a literal (a temporary cRZString, as the original builds one).
int32_t CompareToText(const cRZString &text, const char *literal)
{
    cRZString other;
    other.Construct(literal);
    const int32_t order = text.CompareTo(*other.AsInterface(), false);
    other.Destruct();
    return order;
}

// Whether `name` (already lowercased) is in the category's list; the list holds lowercased names and is compared exactly.
bool IsExcludedFolder(const StringList *category, const cRZString &name)
{
    cRZString lowered;
    lowered.Construct();
    lowered.mString.assign(name.mString.data(), name.mString.size());
    reinterpret_cast<cRZStringHelpers *>(&lowered)->LowerCase(); // 0x1ad167, ours
    StringListNode *node = category->head->next;
    for (; node != category->head; node = node->next)
        if (node->text.mString == lowered.mString) break;
    lowered.Destruct();
    return node != category->head;
}

// Whether the file-name filter names "name + extension" (_stricmp against each entry's ToChar, slot 5).
bool IsWantedFile(const StringList *onlyFiles, const cRZString &name, const cRZString &extension)
{
    for (StringListNode *node = onlyFiles->head->next; node != onlyFiles->head; node = node->next) {
        const std::string joined = name.mString + extension.mString;
        cRZString candidate;
        candidate.Construct();
        candidate.mString.assign(joined.data(), joined.size());
        const char *listed = VirtualCall<const char *(__thiscall *)(void *), 5>(&node->text);
        const int differs = _stricmp(listed, candidate.ToChar());
        candidate.Destruct();
        if (differs == 0) return true;
    }
    return false;
}

// A segment kept in mMap178 for the entry: listed again in the segment vector and mNamedMap, reopened, and its groups
// and id ranges filled in (as the primary's AddSegment does for a new one).
void RelistSegment(cTSPersistSystem *system, Segment *segment, uint8_t writable)
{
    Segment **last = system->mSegmentsEnd;
    if (last != system->mSegmentsCap) {
        *last = segment;
        AddRefIf(segment);
        ++system->mSegmentsEnd;
    } else {
        Segment *held = segment;
        reinterpret_cast<ServiceRefVector *>(&system->mSegmentsBegin)->InsertGrown(reinterpret_cast<void **>(last), reinterpret_cast<void **>(&held));
    }
    cRZString path;
    path.Construct();
    segment->GetPath(&path);
    cRZFileName::NormaliseSeparators(&path, '\\');
    reinterpret_cast<cRZStringHelpers *>(&path)->LowerCase();
    NamedSegmentEntry entry;
    entry.path.Construct();
    entry.path.mString.assign(path.mString.data(), path.mString.size());
    entry.segment = segment;
    void *inserted = nullptr;
    // 0x1d84b0 (thiscall on the map, ret 8, original): mNamedMap's emplace.
    OriginalFunction<void *(__thiscall *)(void *, void **, NamedSegmentEntry *), 0x1d84b0>()(&system->mNamedMap, &inserted, &entry);
    entry.path.Destruct();
    segment->AutoOpen(1, writable);
    // 0x1e9b50 and 0x1e97b0 (thiscall on the primary, ret 4, original): the segment's groups into mSegmentsByGroup, and its
    // group list into mSegmentIdRanges.
    system->IndexSegmentGroups(segment);
    system->SegmentGroups(segment);
    path.Destruct();
}

// A new segment for the entry, made through the framework COM: set up, added and kept in mMap198 when it initialises,
// and appended to `out` (when given) either way.
void CreateContentSegment(cTSPersistSystem *system, cRZString *path, cRZString *name, uint32_t mode, uint32_t type, uint32_t classId,
                          uint8_t writable, RefPtrVector *out)
{
    cIGZUnknown *created = nullptr;
    void *com = GetDirectorFrameworkObject();
    // The original tests al (the interface table types the result as a dword).
    if (static_cast<uint8_t>(As<ifaces::cIGZCOM>(com)->GetClassObject(classId, kIidKeyStatus, &created))) {
        auto segmentOf = [&created]() { return static_cast<Segment *>(VirtualCall<void *(__thiscall *)(void *), 3>(created)); };
        Segment *segment = segmentOf();
        const uint32_t cache = VirtualCall<uint32_t(__thiscall *)(void *), 47>(system);
        VirtualCall<void(__thiscall *)(void *, uint32_t), 17>(segment, cache); // SetSegmentCache
        // Ours (lane FL): sets the segment up for the path (0x1e7230).
        system->SetUpSegment(path, name, mode, type, segment);
        // 0x1dab60 (__cdecl, original): the packed file's three standard key filters.
        if (type == 1) OriginalFunction<void(__cdecl *)(void *), 0x1dab60>()(created);
        if (VirtualCall<uint8_t(__thiscall *)(void *), 3>(segmentOf())) { // Init
            VirtualCall<uint32_t(__thiscall *)(void *, void *), 27>(system, segment); // the primary's AddSegment
            segment->AutoOpen(1, writable);
            cRZString key;
            key.Construct();
            MakePathKey(system, path, &key);
            EmplaceResult<PathSegmentNode> kept{};
            // 0x1d7df0 (thiscall on the table, ret 8, original): mMap198's try_emplace (a null segment when new).
            OriginalFunction<void *(__thiscall *)(void *, EmplaceResult<PathSegmentNode> *, cRZString *), 0x1d7df0>()(&system->mMap198, &kept, &key);
            Segment *&slot = kept.node->segment;
            Segment *previous = slot;
            if (segment != previous) {
                segment->AddRef();
                slot = segment;
                if (previous) previous->Release();
            }
            key.Destruct();
        }
        if (out) {
            Segment *held = segment;
            segment->AddRef();
            void **last = out->mLast;
            if (last != out->mEnd) {
                *last = segment;
                segment->AddRef();
                out->mLast = last + 1;
            } else {
                out->EmplaceReallocate(last, reinterpret_cast<void *const *>(&held));
            }
            segment->Release();
        }
    }
    ReleaseIf(created);
}

// One listed entry of the folder: its segment found, skipped, made or the folder opened recursively.
void OpenContentEntry(cTSPersistSystem *primary, DirectoryListing *listing, uint32_t mode, uint8_t recursive, StringList *category,
                      RefPtrVector *out, StringList *onlyFiles)
{
    cTSPersistSystem *system = primary;
    StringStorage entryStorage;
    cRZString &entry = *entryStorage.S();
    listing->EntryPath(&entry);
    for (char &c : entry.mString)
        if (c == '\\') c = '/';
    cRZString name, extension;
    name.Construct();
    extension.Construct();
    cRZFileName::SplitInto(&entry, nullptr, nullptr, &name, &extension);
    auto lowerName = [&name]() { reinterpret_cast<cRZStringHelpers *>(&name)->LowerCase(); }; // 0x1ad167, ours

    uint32_t type = 0;
    uint32_t classId = 0;
    uint8_t writable = 0;
    const uint8_t kind = static_cast<uint8_t>(listing->EntryKind());
    if (kind & kListedFolder) {
        lowerName();
        if (CompareToText(extension, ".files") == 0) {
            type = 2;
        } else {
            cRZString sceneGraph;
            sceneGraph.Construct(".sgfiles");
            // cRZString slot 12, IsEqual(const cIGZString &, bool), through the extension's vtable as the original calls it.
            bool isTemplate = VirtualCall<uint8_t(__thiscall *)(void *, const cIGZString *, uint32_t), 12>(&extension, sceneGraph.AsInterface(), 0) != 0;
            sceneGraph.Destruct();
            if (!isTemplate && !recursive && extension.mString.empty()) {
                for (const char *folder : kTemplateFolderNames) {
                    if (name.IsEqual(folder, 0xffffffffu, false)) {
                        isTemplate = true;
                        break;
                    }
                }
            }
            if (!isTemplate) isTemplate = name.IsEqual(kThumbnailsFolderName, 0xffffffffu, false);
            if (isTemplate) {
                type = 3;
            } else if (recursive && !(category && IsExcludedFolder(category, name))) {
                primary->OpenContentFolder(mode, &entry, 1, category, out, nullptr);
            }
        }
        if (type) {
            entry.mString.append("/", 1);
            classId = kClassDirectoryFiles;
            writable = 1;
        }
    } else if ((kind & kListedFile) && CompareToText(extension, ".package") == 0) {
        lowerName();
        type = 1;
        classId = kClassPackedFile;
        cRZFile file;
        file.Construct(*entry.AsInterface());
        writable = file.Open(2, 2, 1);
        file.Destruct();
    }

    if (type) {
        ++system->mFieldB8[type];
        Segment *kept = nullptr;
        {
            cRZString key;
            key.Construct();
            MakePathKey(system, &entry, &key);
            if (PathSegmentNode *node = FindPathSegment(system->mMap178, key)) {
                kept = node->segment;
                kept->AddRef();
            }
            key.Destruct();
        }
        if (kept) {
            RelistSegment(system, kept, writable);
        } else {
            cRZString key;
            key.Construct();
            MakePathKey(system, &entry, &key);
            const bool opened = FindPathSegment(system->mMap198, key) != nullptr;
            key.Destruct();
            if (!opened && (!onlyFiles || IsWantedFile(onlyFiles, name, extension)))
                CreateContentSegment(system, &entry, &name, mode, type, classId, writable, out);
        }
        ReleaseIf(kept);
    }
    extension.Destruct();
    name.Destruct();
    entry.Destruct();
}

} // namespace

// Lane FQ. 0x1e3c70 (3601 B, non-virtual, ret 0x18): opens the segments of one content folder `path` (OpenContentPaths
// and CollectPathGroups call it). Over the folder listing (0x1f58f0 ... 0x1f6090), each entry's path ('\' made '/')
// is split into name and extension; the listing's kind byte is 4 for a folder, 8 for a file. A ".package" file is a
// packed-file segment (type 1, class 0x9af5e892, writable when cRZFile Open(2, 2, 1) works); a ".files" folder a
// directory segment (type 2); a ".sgfiles" folder, a "Thumbnails" folder, or at the top level (`recursive` 0) a folder
// without extension named CASFace / LightRigs / Objects / Sims / Textures a template directory (type 3); folders of
// types 2 and 3 get a trailing '/' and are writable. Any other folder is opened recursively when `recursive` is set,
// unless its lowercased name is in the `category` list (std::list<cRZString>*). For each segment entry: mFieldB8[type]
// counts it; a segment kept in mMap178 under the path's key (0x1e6e80) is listed again (the segment vector, mNamedMap
// by its lowercased path, AutoOpen(1, writable), 0x1e9b50, 0x1e97b0); a key already in mMap198 is skipped; else, when
// `onlyFiles` (std::list<cRZString>*) is null or names "name + extension" (_stricmp), a new segment is created through
// the framework COM (QI 0x3b2271bb), given the primary's slot 47 (slot 17), set up (SetUpSegment, 0x1dab60 for
// packages) and, when it initialises (slot 3), added (primary slot 27), opened (AutoOpen(1, writable)) and kept in
// mMap198; it is appended to `out` (std::vector of counted segments) when given. Returns 1.
uint8_t cTSPersistSystem::OpenContentFolder(uint32_t mode, cRZString *path, uint32_t recursive, void *category, void *out, void *onlyFiles)
{
    ScopedDirectoryListing listing(path, this);
    if (listing->First()) {
        do {
            OpenContentEntry(this, listing.Get(), mode, static_cast<uint8_t>(recursive), static_cast<StringList *>(category), static_cast<RefPtrVector *>(out),
                             static_cast<StringList *>(onlyFiles));
        } while (listing->Next());
    }
    return 1;
}

// ---------------------------------------------------------------------------------------------------------
// Adapters: the RVA hooks' entry points (qualified calls, inlined: no virtual dispatch).

int32_t __fastcall cTSPersistSystemAdapters::GetServicePriority(cIGZSystemService *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::GetServicePriority();
}

void * __fastcall cTSPersistSystemAdapters::GetResourceManager(cITSPersistSystem *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::GetResourceManager();
}

void __fastcall cTSPersistSystemAdapters::ClearDeferWrites(cITSPersistSystem *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::ClearDeferWrites();
}

bool __fastcall cTSPersistSystemAdapters::LockSystem(cITSPersistSystem *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::LockSystem();
}

bool __fastcall cTSPersistSystemAdapters::UnlockSystem(cITSPersistSystem *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::UnlockSystem();
}

void __fastcall cTSPersistSystemAdapters::StopBackgroundOpen(cITSPersistSystem *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::StopBackgroundOpen();
}

uint8_t __fastcall cTSPersistSystemAdapters::vf56(cTSPersistSystem *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::vf56();
}

uint32_t __fastcall cTSPersistSystemAdapters::vf3(cIGZPersistResourceManager *face, void *, uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::vf3(key, iid, out, a4, a5);
}

uint8_t __fastcall cTSPersistSystemAdapters::vf49(cIGZPersistResourceManager *face, void *, uint32_t a1, uint32_t a2, uint32_t a3)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::vf49(a1, a2, a3);
}

uint8_t __fastcall cTSPersistSystemAdapters::vf52(cIGZPersistResourceManager *face, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::vf52(a1, a2, a3, a4, a5, a6);
}

void __fastcall cTSPersistSystemAdapters::ClearSpeculativeLoader(cTSPersistSystem *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::ClearSpeculativeLoader();
}

void __fastcall cTSPersistSystemAdapters::SetKeyedObject(cITSPersistSystem *face, void *, uint32_t key, void *object)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::SetKeyedObject(key, object);
}

uint8_t __fastcall cTSPersistSystemAdapters::vf168_56(cITSPersistSystem *face, void *, void *record)
{
    [[msvc::forceinline_calls]] return static_cast<cTSPersistSystem *>(face)->cTSPersistSystem::vf168_56(record);
}


// 0x1e8b80 (123 B; thiscall, ret 4, `this` unused): a path in key form, in place: every character through the CRT's
// tolower (signed char, as the original passes it), a backslash then made a slash; then one trailing slash dropped. The
// loop runs over the size read before it.
void cTSPersistSystem::NormalisePath(cRZString *path)
{
    std::string &text = path->mString;
    const size_t size = text.size();
    for (size_t i = 0; i < size; ++i) {
        char &c = text[i];
        c = static_cast<char>(std::tolower(static_cast<signed char>(c)));
        if (c == '\\') c = '/';
    }
    if (!text.empty() && text.back() == '/') text.pop_back();
}

// 0x1e6e80 (773 B; thiscall on the primary, ret 8): the key of a path, the path in key form (NormalisePath) with its
// data-folder prefix named. key = path (not copied onto itself). The user data folder (cTSAppCommonImpl slot 4 with
// 0x2bcfce83, flag 1, 0, -1; its answer not checked), in key form: when the key starts with it (std::string compare of
// the key's first size() characters with its text), that prefix becomes "%UserDataDir%" and nothing else is asked
// (an empty folder therefore puts "%UserDataDir%" in front of every key). Otherwise for each option bit i from 0 to
// HighestOptionBit() (inclusive; one game folder string reused across the loop) the folder of option 1 << i (slot 4,
// 0x4bcfce5e): when it answers (al), in key form, and the key starts with it, that prefix becomes "%GameDataDir%" (bit 0)
// or "%GameDataDir<i>%" (cRZString::Sprintf, the format at 0xda6380), and the loop ends.
void cTSPersistSystem::MakePathKey(const cRZString *path, cRZString *key)
{
    if (&key->mString != &path->mString) key->mString.assign(path->mString.data(), path->mString.size());
    NormalisePath(key);

    cRZString userDir;
    userDir.Construct();
    appinit::CommonImplSlot4(0x2bcfce83, reinterpret_cast<uint32_t>(&userDir), 1, 0, 0xffffffffu);
    NormalisePath(&userDir);
    if (key->mString.compare(0, userDir.mString.size(), userDir.mString.c_str()) == 0) {
        key->mString.replace(0, userDir.mString.size(), "%UserDataDir%", 13);
    } else {
        const uint32_t highest = appinit::HighestOptionBit();
        cRZString gameDir;
        gameDir.Construct();
        for (uint32_t bit = 0; bit <= highest; ++bit) {
            const uint32_t answered =
                appinit::CommonImplSlot4(appinit::kOptionQueryId, reinterpret_cast<uint32_t>(&gameDir), 1, 0, 1u << bit);
            if (static_cast<uint8_t>(answered) == 0) continue;
            NormalisePath(&gameDir);
            const std::string &game = gameDir.mString;
            const std::string &text = key->mString;
            if (text.size() < game.size() || std::memcmp(text.data(), game.data(), game.size()) != 0) continue;
            cRZString name;
            name.Construct("%GameDataDir%");
            if (bit != 0) name.Sprintf("%%GameDataDir%d%%", bit);
            key->mString.replace(0, game.size(), name.mString.c_str(), std::strlen(name.mString.c_str()));
            name.Destruct();
            break;
        }
        gameDir.Destruct();
    }
    userDir.Destruct();
}

namespace {

// mSegmentsByGroup as the game's std::unordered_map instantiation sees it: group -> the group's counted segments (a
// std::vector in the allocator service's memory), identity hash. Only try_emplace is used here; the value starts empty
// and is never destroyed by this code (the system's destructor owns that), so it needs no destructor of its own.
struct IdentityHash {
    size_t operator()(uint32_t key) const { return key; }
};
struct GroupSegmentList {
    Segment **first = nullptr;
    Segment **last = nullptr;
    Segment **end = nullptr;
};
using GroupSegmentLookup = std::unordered_map<uint32_t, GroupSegmentList, IdentityHash>;
static_assert(sizeof(GroupSegmentLookup) == sizeof(cGZPersistHashMap), "unordered_map layout");
static_assert(sizeof(std::pair<const uint32_t, GroupSegmentList>) + 8 == 0x18, "group node 0x18");

// An element of the group's vector while it is sorted: a counted pointer copied with AddRef (slot 1) and Release (slot
// 2), as the original's element type (no move: the sort copies).
struct CountedSegment {
    Segment *segment;
    CountedSegment(const CountedSegment &other) : segment(other.segment)
    {
        if (segment) segment->AddRef();
    }
    CountedSegment &operator=(const CountedSegment &other)
    {
        Segment *old = segment;
        if (other.segment != old) {
            if (other.segment) other.segment->AddRef();
            segment = other.segment;
            if (old) old->Release();
        }
        return *this;
    }
    ~CountedSegment()
    {
        if (segment) segment->Release();
    }
};
static_assert(sizeof(CountedSegment) == 4, "counted segment");

// What the primary's slot 37 hands the callback: the system, the group, the group's list.
struct GatherContext {
    cTSPersistSystem *system;
    uint32_t group;
    GroupSegmentList *list;
};

// 0x1e9710 (154 B, __cdecl; the callback FillInSegments gives slot 37): when the ITS face's slot 30 (0x78) answers for
// (segment, group), the segment is appended to the list (counted; ServiceRefVector::InsertGrown when full) under a held
// reference. Always 1, so the walk goes on.
uint8_t __cdecl GatherGroupSegment(Segment *segment, GatherContext *context)
{
    void *its = reinterpret_cast<uint8_t *>(context->system) + 0xa8;
    if (!VirtualCall<uint8_t(__thiscall *)(void *, Segment *, uint32_t), 30>(its, segment, context->group)) return 1;
    Segment *held = segment;
    if (held) held->AddRef();
    GroupSegmentList *list = context->list;
    Segment **last = list->last;
    if (last == list->end) {
        reinterpret_cast<ServiceRefVector *>(list)->InsertGrown(reinterpret_cast<void **>(last), reinterpret_cast<void **>(&held));
    } else {
        *last = segment;
        if (segment) segment->AddRef();
        ++list->last;
    }
    if (segment) segment->Release();
    return 1;
}

} // namespace

// 0x1e95c0 (323 B; thiscall on the primary, ret 4): the group's node in mSegmentsByGroup (std::unordered_map
// try_emplace, identity hash; an existing list is kept and added to), filled through the primary's slot 37 (the base
// segment deque walked with GatherGroupSegment), then std::stable_sort'ed with segments that are not packed files ahead
// of those that are (each comparison asks the left segment, then the right, releasing the right's answer first).
// Returns the list.
void *cTSPersistSystem::FillInSegments(uint32_t group)
{
    GroupSegmentList &list = reinterpret_cast<GroupSegmentLookup &>(mSegmentsByGroup).try_emplace(group).first->second;
    GatherContext context{this, group, &list};
    VirtualCall<uint8_t(__thiscall *)(void *, uint8_t(__cdecl *)(Segment *, GatherContext *), GatherContext *), 37>(this,
        &GatherGroupSegment, &context);
    auto *first = reinterpret_cast<CountedSegment *>(list.first);
    auto *last = reinterpret_cast<CountedSegment *>(list.last);
    std::stable_sort(first, last, [](const CountedSegment &left, const CountedSegment &right) {
        void *leftFace = nullptr;
        const uint8_t leftPacked = left.segment->QueryInterface(kIidPackedFile, &leftFace);
        void *rightFace = nullptr;
        const uint8_t rightPacked = right.segment->QueryInterface(kIidPackedFile, &rightFace);
        ReleaseIf(rightFace);
        ReleaseIf(leftFace);
        return (leftPacked != 0) < (rightPacked != 0);
    });
    return &list;
}

// 0x1dedf0 (310 B; thiscall on the primary): when mHasChangeRecords is set, every group's change records that changed
// are written to the store (+0x84) as the record {0xad1a6720, 0, group, 0}: OpenRecord (slot 22, mode 2), the record's
// output stream (QueryInterface 0xc56b8f08) given to ChangeRecordSet::Write; on success the record is cut at its
// position (slot 12, then slot 11 with it) and closed (slot 6), else aborted (slot 7). The stream and the record are
// released; the set is marked unchanged whether or not it was written. Groups in table order; then the flag is cleared.
void cTSPersistSystem::SaveChangeRecords()
{
    if (!mHasChangeRecords) return;
    for (auto &entry : reinterpret_cast<ChangeRecordMap &>(mChangeRecords)) {
        ::ChangeRecordSet &records = entry.second;
        if (!records.mChanged) continue;
        const uint32_t key[4] = {0xad1a6720u, 0, entry.first, 0};
        ::cIGZUnknown *record = nullptr;
        void *store = reinterpret_cast<void *>(mField84);
        if (VirtualCall<uint8_t(__thiscall *)(void *, const uint32_t *, ::cIGZUnknown **, uint32_t), 22>(store, key, &record, 2)) {
            void *stream = nullptr;
            record->QueryInterface(0xc56b8f08, &stream);
            if (records.Write(stream)) {
                const uint32_t position = VirtualCall<uint32_t(__thiscall *)(void *), 12>(record);
                VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), 11>(record, position);
                VirtualCall<uint8_t(__thiscall *)(void *), 6>(record);
            } else {
                VirtualCall<uint8_t(__thiscall *)(void *), 7>(record);
            }
            if (stream) static_cast<::cIGZUnknown *>(stream)->Release();
        }
        records.mChanged = 0;
        if (record) record->Release();
    }
    mHasChangeRecords = 0;
}

namespace {

// mMap154 as the game's std::unordered_map instantiation sees it (try_emplace 0x1d7b60): the key a cRZString copied as
// that function copies it (the text, the cRZString vtable, count 0), hashed with nGZLog::HashChars over its text and
// compared as std::string; the value PathEntry (the two words at pair+0x20, the vector at pair+0x28).
struct PathKey {
    cRZString text;
    PathKey() { text.Construct(); }
    PathKey(const PathKey &other)
    {
        text.Construct();
        text.mString.assign(other.text.mString.data(), other.text.mString.size());
    }
    PathKey &operator=(const PathKey &) = delete;
    ~PathKey() { text.Destruct(); }
};
static_assert(sizeof(PathKey) == 0x20, "path key");
struct PathKeyHash {
    size_t operator()(const PathKey &key) const { return nGZLog::HashChars(key.text.ToChar()); }
};
struct PathKeyEqual {
    bool operator()(const PathKey &left, const PathKey &right) const { return left.text.mString == right.text.mString; }
};
using PathEntryLookup = std::unordered_map<PathKey, PathEntry, PathKeyHash, PathKeyEqual>;
static_assert(sizeof(PathEntryLookup) == sizeof(cGZPersistHashMap), "unordered_map layout");
static_assert(sizeof(std::pair<const PathKey, PathEntry>) + 8 == 0x3c, "path-entry node 0x3c");

// mSegmentIdRanges as the game's instantiation sees it (lower_bound 0x1d8790, operator[] 0x1d8050, erase 0x1d86f0):
// segment id -> the segment's group vector (an mMap154 entry's), std::hash.
using SegmentGroupsById = std::unordered_map<uint32_t, std::vector<uint32_t> *>;
static_assert(sizeof(SegmentGroupsById) == sizeof(cGZPersistHashMap), "unordered_map layout");

// mSegmentsByGroup for erasing a group's node (inlined in 0x1e9b50: _Find_last 0x15ac20, the unlink, the value's
// destructor 0x1e6a40, the node freed): the value destroyed as the original does (GroupSegments).
using GroupSegmentErase = std::unordered_map<uint32_t, GroupSegments, IdentityHash>;
static_assert(sizeof(GroupSegmentErase) == sizeof(cGZPersistHashMap), "unordered_map layout");

// mMap178 / mMap198 as the game's instantiation sees them (operator[] 0x1d7df0, clear 0x1e8d50): the key as mMap154's,
// the value a counted segment (CountedRef: released before the key is destroyed, as 0x1d9ab0 does).
using KeptSegmentLookup = std::unordered_map<PathKey, CountedRef, PathKeyHash, PathKeyEqual>;
static_assert(sizeof(KeptSegmentLookup) == sizeof(cGZPersistHashMap), "unordered_map layout");
static_assert(sizeof(std::pair<const PathKey, CountedRef>) + 8 == 0x2c, "kept-segment node 0x2c");

// The group of every listed resource key (16-byte keys: type, group, ...) appended in order, with the key's type after
// it when the group is 0x1c0532fa (the inlined loop of 0x1e9b50 and 0x1e97b0; push_back's growth is 0x1461c0).
void AppendListedGroups(std::vector<uint32_t> &groups, const ServiceBlockVector &keys)
{
    const auto *first = static_cast<const ListedKey *>(keys.mFirst);
    const size_t count = static_cast<size_t>(static_cast<const ListedKey *>(keys.mLast) - first);
    for (size_t i = 0; i < count; ++i) {
        groups.push_back(first[i].group);
        if (first[i].group == 0x1c0532fa) groups.push_back(first[i].type);
    }
}

// `value` appended when it is not 0, then the list sorted and its repeats dropped (std::sort and std::unique: the
// image's 0x1d70f0 and 0x1d8900 are library code).
void FinishGroups(std::vector<uint32_t> &groups, uint32_t value)
{
    if (value) groups.push_back(value);
    std::sort(groups.begin(), groups.end());
    groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
}

} // namespace

// 0x1e8ef0 (223 B; thiscall on the primary, ret 4): the time stamp of a path: a copy of its text in key form
// (NormalisePath), its modification time (0x1c2068 with 2, original), 1 when that is 0.
uint32_t cTSPersistSystem::PathStamp(const cRZString *path)
{
    cRZString copy;
    copy.Construct();
    copy.mString.assign(path->mString.data(), path->mString.size());
    NormalisePath(&copy);
    const uint32_t stamp = PathFileTime(copy.AsInterface(), 2);
    copy.Destruct();
    return stamp ? stamp : 1;
}

// 0x1e94e0 (223 B; thiscall on the primary, ret 8): mMap154's entry for the path's key (MakePathKey; made when there is
// none) gets the path's stamp (PathStamp) and `value` in its two words, and its vector emptied (the block kept).
// Returns the vector.
void *cTSPersistSystem::ResetPathEntry(const cRZString *path, uint32_t value)
{
    PathKey key;
    MakePathKey(path, &key.text);
    PathEntry &entry = reinterpret_cast<PathEntryLookup &>(mMap154).try_emplace(key).first->second;
    entry.mWords[0] = PathStamp(path);
    entry.mWords[1] = value;
    entry.mValues.clear();
    return &entry.mValues;
}

// 0x1e9b50 (853 B; thiscall on the primary, ret 4): nothing while ITS 43 opens content (+0x174). Else the segment's
// key-status face (QueryInterface 0x3b2271bb) gives a value (its slot 4) when it answers. The segment's id (slot 15) is
// looked up in mSegmentIdRanges; when it is not there, the segment's path (slot 11) gives a reset mMap154 entry
// (ResetPathEntry with the value) whose vector the id is then mapped to; else the id's vector is used as it is. The
// group of every resource key the segment lists (slot 19, no filter) is appended to it, with the key's type after it
// when the group is 0x1c0532fa, then the value when it is not 0; the vector is sorted and its repeats dropped. Every
// group in it loses its node in mSegmentsByGroup (the cached segment list, rebuilt by FillInSegments), the id's node in
// mSegmentIdRanges is erased again, the key list's block freed (ServiceBlockVector::FreeBlock) and the face released.
void cTSPersistSystem::IndexSegmentGroups(void *segment)
{
    if (mOpeningContent) return;
    uint32_t value = 0;
    void *face = nullptr;
    if (VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, void **), 0>(segment, 0x3b2271bb, &face))
        value = VirtualCall<uint32_t(__thiscall *)(void *), 4>(face);

    auto &byId = reinterpret_cast<SegmentGroupsById &>(mSegmentIdRanges);
    std::vector<uint32_t> *groups;
    if (byId.find(VirtualCall<uint32_t(__thiscall *)(void *), 15>(segment)) == byId.end()) {
        cRZString path;
        path.Construct();
        VirtualCall<uint8_t(__thiscall *)(void *, cRZString *), 11>(segment, &path);
        groups = static_cast<std::vector<uint32_t> *>(ResetPathEntry(&path, value));
        byId[VirtualCall<uint32_t(__thiscall *)(void *), 15>(segment)] = groups;
        path.Destruct();
    } else {
        groups = byId[VirtualCall<uint32_t(__thiscall *)(void *), 15>(segment)];
    }
    const auto found = byId.find(VirtualCall<uint32_t(__thiscall *)(void *), 15>(segment));

    ServiceBlockVector keys{};
    if (VirtualCall<uint32_t(__thiscall *)(void *, ServiceBlockVector *, void *), 19>(segment, &keys, nullptr) != 0)
        AppendListedGroups(*groups, keys);
    FinishGroups(*groups, value);

    auto &byGroup = reinterpret_cast<GroupSegmentErase &>(mSegmentsByGroup);
    for (const uint32_t group : *groups) byGroup.erase(group);
    byId.erase(found);
    keys.FreeBlock();
    ReleaseIf(face);
}

// 0x1e97b0 (919 B; thiscall on the primary, ret 4): the segment's group list. Its path (slot 11), then the key-status
// face (QueryInterface 0x3b2271bb) gives a value (slot 4) when it answers. The path's mMap154 entry (MakePathKey; looked
// up, not made) is current when the path's stamp (PathStamp) is the entry's first word (a 0 word takes the stamp) and
// the value its second: the id (slot 15) is then mapped to its vector in mSegmentIdRanges as it is. Otherwise, when the
// segment lists resource keys (slot 19, no filter), or lists none and slot 7 says so (al), the entry is reset
// (ResetPathEntry with the value) and mapped to the id, and gets the keys' groups (AppendListedGroups) and the value
// (FinishGroups); else there is no list (0). The key list's block is freed (FreeBlock) unless the entry was current, the
// face released. Returns the list (std::vector<uint32_t>*) or 0.
void *cTSPersistSystem::SegmentGroups(void *segment)
{
    cRZString path;
    path.Construct();
    VirtualCall<uint8_t(__thiscall *)(void *, cRZString *), 11>(segment, &path);
    uint32_t value = 0;
    void *face = nullptr;
    if (VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, void **), 0>(segment, 0x3b2271bb, &face))
        value = VirtualCall<uint32_t(__thiscall *)(void *), 4>(face);

    auto &byId = reinterpret_cast<SegmentGroupsById &>(mSegmentIdRanges);
    std::vector<uint32_t> *groups = nullptr;
    {
        PathKey key;
        MakePathKey(&path, &key.text);
        auto &paths = reinterpret_cast<PathEntryLookup &>(mMap154);
        const auto found = paths.find(key);
        if (found != paths.end()) {
            PathEntry &entry = found->second;
            const uint32_t stamp = PathStamp(&path);
            if (entry.mWords[0] != stamp && entry.mWords[0] == 0) entry.mWords[0] = stamp;
            if (entry.mWords[0] == stamp && entry.mWords[1] == value) groups = &entry.mValues;
        }
    }
    if (groups) {
        byId[VirtualCall<uint32_t(__thiscall *)(void *), 15>(segment)] = groups;
    } else {
        ServiceBlockVector keys{};
        const bool listed = VirtualCall<uint32_t(__thiscall *)(void *, ServiceBlockVector *, void *), 19>(segment, &keys, nullptr) != 0;
        if (listed || VirtualCall<uint8_t(__thiscall *)(void *), 7>(segment)) {
            groups = static_cast<std::vector<uint32_t> *>(ResetPathEntry(&path, value));
            byId[VirtualCall<uint32_t(__thiscall *)(void *), 15>(segment)] = groups;
            if (listed) AppendListedGroups(*groups, keys);
            FinishGroups(*groups, value);
        }
        keys.FreeBlock();
    }
    ReleaseIf(face);
    path.Destruct();
    return groups;
}

// 0x1ea760 (534 B; thiscall on the primary): before the content is opened again (OpenContentPaths), every segment of the
// vector (+0xd4; the end read once) is kept in mMap178 under its path's key (slot 11, MakePathKey; the entry made when
// absent, its counted segment replaced: AddRef the new, Release the old, nothing when the same). Then the vector is
// cleared (RefPtrVector::Clear), a non-empty mNamedMap emptied as CloseAllSegments does, and mSegmentsByGroup (0x1e8ce0)
// and mSegmentIdRanges (0x1e8c00) cleared (std::unordered_map::clear, library code in the image).
void cTSPersistSystem::ParkOpenSegments()
{
    auto &kept = reinterpret_cast<KeptSegmentLookup &>(mMap178);
    for (Segment **it = mSegmentsBegin, **end = mSegmentsEnd; it != end; ++it) {
        cRZString path;
        path.Construct();
        VirtualCall<uint8_t(__thiscall *)(void *, cRZString *), 11>(*it, &path);
        Segment *segment = *it;
        {
            PathKey key;
            MakePathKey(&path, &key.text);
            CountedRef &entry = kept[key];
            Segment *old = static_cast<Segment *>(entry.mObject);
            if (segment != old) {
                if (segment) segment->AddRef();
                entry.mObject = segment;
                if (old) old->Release();
            }
        }
        path.Destruct();
    }
    reinterpret_cast<RefPtrVector *>(&mSegmentsBegin)->Clear();
    EmptyNamedMap(mNamedMap);
    reinterpret_cast<GroupSegmentErase &>(mSegmentsByGroup).clear();
    reinterpret_cast<SegmentGroupsById &>(mSegmentIdRanges).clear();
}

// 0x1e9eb0 (730 B; thiscall on the primary): after the content is opened (the end of OpenContentPaths), every segment
// kept in mMap178 (in table order) that the vector (+0xd4) does not hold again is listed again: the ITS face's slot 31
// (GetSegmentPath) with a scratch string, a counted push_back onto the vector (a held reference around it;
// ServiceRefVector::InsertGrown when full), mNamedMap's emplace (0x1d84b0, original) of its path (slot 11) with
// backslashes and lowercased, then IndexSegmentGroups and SegmentGroups. Then mMap178 and mMap198 are cleared
// (0x1e8d50: std::unordered_map::clear, library code; each segment released, then its key destroyed).
void cTSPersistSystem::RelistParkedSegments()
{
    auto &kept = reinterpret_cast<KeptSegmentLookup &>(mMap178);
    for (auto &entry : kept) {
        auto *segment = static_cast<Segment *>(entry.second.mObject);
        if (std::find(mSegmentsBegin, mSegmentsEnd, segment) != mSegmentsEnd) continue;
        cRZString scratch;
        scratch.Construct();
        VirtualCall<uint8_t(__thiscall *)(void *, Segment *, cRZString *), 31>(ITSFace(), segment, &scratch);
        Segment *held = segment;
        if (held) held->AddRef();
        Segment **last = mSegmentsEnd;
        if (last != mSegmentsCap) {
            *last = segment;
            if (segment) segment->AddRef();
            ++mSegmentsEnd;
        } else {
            reinterpret_cast<ServiceRefVector *>(&mSegmentsBegin)->InsertGrown(reinterpret_cast<void **>(last), reinterpret_cast<void **>(&held));
        }
        if (segment) segment->Release();

        cRZString path;
        path.Construct();
        VirtualCall<uint8_t(__thiscall *)(void *, cRZString *), 11>(segment, &path);
        cRZFileName::NormaliseSeparators(&path, '\\');
        reinterpret_cast<cRZStringHelpers *>(&path)->LowerCase();
        NamedSegmentEntry named;
        named.path.Construct();
        named.path.mString.assign(path.mString.data(), path.mString.size());
        named.segment = segment;
        void *inserted = nullptr;
        OriginalFunction<void *(__thiscall *)(void *, void **, NamedSegmentEntry *), 0x1d84b0>()(&mNamedMap, &inserted, &named);
        named.path.Destruct();
        IndexSegmentGroups(segment);
        SegmentGroups(segment);
        path.Destruct();
        scratch.Destruct();
    }
    kept.clear();
    reinterpret_cast<KeptSegmentLookup &>(mMap198).clear();
}

namespace {

// The resource names (+0x1b8) with the innermost value counted, as the image's tree code treats it (0x1d5a10, the
// innermost map's subtree erase, releases each string): layout-identical to ResourceNames above.
struct CountedName {
    cRZString *text = nullptr;
    ~CountedName()
    {
        if (text) VirtualCall<uint32_t(__thiscall *)(void *), 2>(text);
    }
};
using OwnedNamesByKey = std::map<std::pair<uint32_t, uint32_t>, CountedName>;
using OwnedNamesByType = std::map<uint32_t, OwnedNamesByKey>;
using OwnedNames = std::map<uint32_t, OwnedNamesByType>;
static_assert(sizeof(OwnedNames) == sizeof(ResourceNames) && sizeof(CountedName) == sizeof(cRZString *), "names layout");

// The resource key that holds a segment's names of one type: {0x4e6d6150, 0x52737256, type, 0}.
constexpr uint32_t kNameMapType = 0x4e6d6150, kNameMapGroup = 0x52737256;

// cGZPersistResourceKeyFilterByTypeAndGroup (16 bytes, game vtable 0xda5d08): accepts the keys of one type and group.
// Made as 0x1e8fd0 makes it inline (count 0), then AddRef'd through its vtable.
struct TypeGroupKeyFilter {
    static constexpr uint32_t kVTable = 0xda5d08;
    const void *mVTable;
    uint32_t mRefCount;
    uint32_t mType;
    uint32_t mGroup;
};
static_assert(sizeof(TypeGroupKeyFilter) == 0x10, "type-and-group key filter layout");

} // namespace

// 0x1e8fd0 (1282 B; thiscall on the primary, ret 4): reads a segment's resource names. The segment's entry (its id,
// slot 15) in the names is made when absent and emptied (every string released). The keys of type 0x4e6d6150 and group
// 0x52737256 the segment lists (slot 19 with a type-and-group key filter, made here and released at the end): none
// gives 0. For each key in order: the record opened for reading (slot 22, mode 1) and its stream (QueryInterface
// 0xc56b8f08), either failing gives 0 (both released); the entry for the key's instance (a resource type; made when
// absent); a count (GetUint32, slot 13), then per name a new cRZString (AddRef), its group and instance (slot 13 twice)
// and its text (slot 24), stored under (group, instance) (counted: the old one released) and its own reference
// released. Then the record is closed (slot 6) and both released. 1 when every key was read. The key list's block is
// freed (FreeBlock).
uint8_t cTSPersistSystem::LoadResourceNames(void *segment)
{
    auto &names = reinterpret_cast<OwnedNames &>(mTreeHead);
    OwnedNamesByType &types = names[VirtualCall<uint32_t(__thiscall *)(void *), 15>(segment)];
    types.clear();
    ServiceBlockVector keys{};
    auto *filter = static_cast<TypeGroupKeyFilter *>(::operator new(sizeof(TypeGroupKeyFilter)));
    filter->mRefCount = 0;
    filter->mVTable = GameAddress(TypeGroupKeyFilter::kVTable);
    filter->mType = kNameMapType;
    filter->mGroup = kNameMapGroup;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(filter);
    uint8_t result = 0;
    if (VirtualCall<uint32_t(__thiscall *)(void *, ServiceBlockVector *, void *), 19>(segment, &keys, filter) != 0) {
        result = 1;
        const auto *first = static_cast<const ListedKey *>(keys.mFirst);
        const size_t count = static_cast<size_t>(static_cast<const ListedKey *>(keys.mLast) - first);
        for (size_t i = 0; i < count; ++i) {
            const ListedKey &key = first[i];
            ::cIGZUnknown *record = nullptr;
            void *stream = nullptr;
            if (!VirtualCall<uint8_t(__thiscall *)(void *, const ListedKey *, ::cIGZUnknown **, uint32_t), 22>(segment, &key, &record, 1) ||
                !record->QueryInterface(0xc56b8f08, &stream)) {
                ReleaseIf(stream);
                ReleaseIf(record);
                result = 0;
                break;
            }
            OwnedNamesByKey &byKey = types[key.instance];
            uint32_t listed = 0;
            VirtualCall<uint8_t(__thiscall *)(void *, uint32_t *), 13>(stream, &listed);
            for (uint32_t n = 0; n < listed; ++n) {
                auto *text = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
                text->Construct();
                VirtualCall<uint32_t(__thiscall *)(void *), 1>(text);
                uint32_t group = 0, instance = 0;
                VirtualCall<uint8_t(__thiscall *)(void *, uint32_t *), 13>(stream, &group);
                VirtualCall<uint8_t(__thiscall *)(void *, uint32_t *), 13>(stream, &instance);
                VirtualCall<uint8_t(__thiscall *)(void *, cRZString *), 24>(stream, text);
                CountedName &slot = byKey[std::make_pair(group, instance)];
                cRZString *const old = slot.text;
                if (text != old) {
                    VirtualCall<uint32_t(__thiscall *)(void *), 1>(text);
                    slot.text = text;
                    if (old) VirtualCall<uint32_t(__thiscall *)(void *), 2>(old);
                }
                VirtualCall<uint32_t(__thiscall *)(void *), 2>(text);
            }
            VirtualCall<uint8_t(__thiscall *)(void *), 6>(record);
            ReleaseIf(stream);
            ReleaseIf(record);
        }
    }
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(filter);
    keys.FreeBlock();
    return result;
}

// 0x1ea4f0 (619 B; thiscall on the primary, ret 4; ITS slot 56 forwards to it): writes a segment's resource names. No
// entry for the segment's id (slot 15): 1. Else per type in order: the record {0x4e6d6150, 0x52737256, type, 0} opened
// for writing (slot 23) and its stream (QueryInterface 0xc56b8f08), either failing gives 0 (both released); the count
// (SetUint32, slot 37), then per name in order its group, its instance (slot 37) and its text (SetGZStr, slot 48); the
// record closed (slot 6), both released. Then the ITS face's slot 45 (RegisterSegment) with the segment, and 1.
uint8_t cTSPersistSystem::SaveResourceNames(void *segment)
{
    auto &names = reinterpret_cast<OwnedNames &>(mTreeHead);
    const auto found = names.find(VirtualCall<uint32_t(__thiscall *)(void *), 15>(segment));
    if (found == names.end()) return 1;
    for (auto &type : found->second) {
        const uint32_t key[4] = {kNameMapType, kNameMapGroup, type.first, 0};
        ::cIGZUnknown *record = nullptr;
        void *stream = nullptr;
        if (!VirtualCall<uint8_t(__thiscall *)(void *, const uint32_t *, ::cIGZUnknown **), 23>(segment, key, &record) ||
            !record->QueryInterface(0xc56b8f08, &stream)) {
            ReleaseIf(stream);
            ReleaseIf(record);
            return 0;
        }
        VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), 37>(stream, static_cast<uint32_t>(type.second.size()));
        for (auto &name : type.second) {
            VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), 37>(stream, name.first.first);
            VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), 37>(stream, name.first.second);
            VirtualCall<uint8_t(__thiscall *)(void *, cRZString *), 48>(stream, name.second.text);
        }
        VirtualCall<uint8_t(__thiscall *)(void *), 6>(record);
        ReleaseIf(stream);
        ReleaseIf(record);
    }
    VirtualCall<uint8_t(__thiscall *)(void *, void *), 45>(ITSFace(), segment);
    return 1;
}

// 0x1ea190 (849 B; thiscall on the primary): writes mMap154 to "Groups.cache" in the user folder. The folder (the
// cTSAppCommonImpl slot 4 forwarder: 0x2bcfce83, all options) and the name go into a cRZFileName (0x1c8a65 and 0x1c8b5f,
// original: its folder and name setters). The framework's slot 21 (CommandLine) is called and its answer dropped. The COM
// director's framework object makes a packed file (slot 3: class 0x86d4a89, interface 0x656b8f0c); when it does, the
// file gets the full path (slot 12) and slot 3, and when it opens (slot 5 with 1, 1) its record {0x54535053, 1, 1, 0}
// is opened for writing (slot 22, mode 3). An open record's stream (QueryInterface 0xc56b8f08, not tested: the game
// would fault without one) gets the version 5, the entry count, then per entry in table order its key (SetGZStr, slot
// 48), its two words, its value count and the values (SetUint32, slot 37); the record is closed (slot 6) and the stream
// released. The file is closed (slot 9) whether or not the record opened; the record and the file are released.
void cTSPersistSystem::SaveGroupMap()
{
    cRZString folder;
    folder.Construct();
    appinit::CommonImplSlot4(0x2bcfce83, Word(&folder), 1, 0, 0xffffffffu);
    cRZFileName file;
    file.ConstructEmpty();
    OriginalFunction<void *(__thiscall *)(cRZFileName *, const cRZString *, uint32_t), 0x1c8a65>()(&file, &folder, 0);
    cRZString name;
    name.Construct("Groups.cache");
    OriginalFunction<void *(__thiscall *)(cRZFileName *, const cRZString *, uint32_t), 0x1c8b5f>()(&file, &name, 0);
    name.Destruct();
    const uint32_t key[4] = {0x54535053, 1, 1, 0};
    VirtualCall<void *(__thiscall *)(void *), 21>(reinterpret_cast<void *>(RZGetGlobal10C9378())); // CommandLine, unused
    ::cIGZUnknown *packed = nullptr;
    void *director = GetDirectorFrameworkObject();
    if (VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, ::cIGZUnknown **), 3>(director, 0x86d4a89, 0x656b8f0c, &packed)) {
        StringStorage full; // GetFullPath constructs it
        VirtualCall<uint8_t(__thiscall *)(void *, cRZString *), 12>(packed, file.GetFullPath(full.S()));
        full.S()->Destruct();
        VirtualCall<uint8_t(__thiscall *)(void *), 3>(packed);
        if (VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t), 5>(packed, 1, 1)) {
            ::cIGZUnknown *record = nullptr;
            if (VirtualCall<uint8_t(__thiscall *)(void *, const uint32_t *, ::cIGZUnknown **, uint32_t), 22>(packed, key, &record, 3)) {
                void *stream = nullptr;
                record->QueryInterface(0xc56b8f08, &stream);
                using SetU32 = uint8_t(__thiscall *)(void *, uint32_t);
                VirtualCall<SetU32, 37>(stream, 5);
                auto &paths = reinterpret_cast<PathEntryLookup &>(mMap154);
                VirtualCall<SetU32, 37>(stream, static_cast<uint32_t>(paths.size()));
                for (auto &entry : paths) {
                    VirtualCall<uint8_t(__thiscall *)(void *, const cRZString *), 48>(stream, &entry.first.text);
                    VirtualCall<SetU32, 37>(stream, entry.second.mWords[0]);
                    VirtualCall<SetU32, 37>(stream, entry.second.mWords[1]);
                    VirtualCall<SetU32, 37>(stream, static_cast<uint32_t>(entry.second.mValues.size()));
                    for (const uint32_t value : entry.second.mValues) VirtualCall<SetU32, 37>(stream, value);
                }
                VirtualCall<uint8_t(__thiscall *)(void *), 6>(record);
                ReleaseIf(stream);
            }
            VirtualCall<uint8_t(__thiscall *)(void *), 9>(packed);
            ReleaseIf(record);
        }
    }
    ReleaseIf(packed);
    file.Destruct();
    folder.Destruct();
}

namespace {

// A counted segment held in BackgroundOpenThread's copy of the segment list (std::list of counted pointers: AddRef on
// copy, Release on destruction).
struct HeldSegment {
    Segment *segment;
    explicit HeldSegment(Segment *held) : segment(held)
    {
        if (segment) segment->AddRef();
    }
    HeldSegment(const HeldSegment &) = delete;
    HeldSegment &operator=(const HeldSegment &) = delete;
    ~HeldSegment()
    {
        if (segment) segment->Release();
    }
};

} // namespace

// 0x1defe0 (383 B; __stdcall thread procedure, ret 4; StartBackgroundOpen starts the background thread on it with the
// system): the ITS face's slot 44 with 0xf00 and the three texts at 0xda66b4, 0xda66c0 and 0xda66cc, then its slot 43
// with 0xffff00ff (OpenContentPaths). Then, under lock B (Lock and Unlock, slots 1 and 2), a counted copy of the base's
// segment deque (+0x70) as a std::list (0x1d4bb0 is the library's range insert); after the lock each copied segment's
// slot 18 with 0, then the copy released in order. Returns 0.
uint32_t __stdcall cTSPersistSystem::BackgroundOpenThread(void *argument)
{
    auto *system = static_cast<cTSPersistSystem *>(argument);
    void *its = system->ITSFace();
    using AddPathsFn = uint8_t(__thiscall *)(void *, uint32_t, const char *);
    VirtualCall<AddPathsFn, 44>(its, 0xf00, GameAddress<const char>(0xda66b4));
    VirtualCall<AddPathsFn, 44>(its, 0xf00, GameAddress<const char>(0xda66c0));
    VirtualCall<AddPathsFn, 44>(its, 0xf00, GameAddress<const char>(0xda66cc));
    VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), 43>(its, 0xffff00ffu);
    std::list<HeldSegment> copy;
    {
        auto *lock = As<ifaces::cIGZCriticalSection>(&system->mLockB);
        lock->Lock();
        for (void *segment : reinterpret_cast<std::deque<void *> &>(system->mDeque)) copy.emplace_back(static_cast<Segment *>(segment));
        lock->Unlock();
    }
    for (HeldSegment &held : copy) VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), 18>(held.segment, 0);
    return 0;
}
