#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZDBSegmentPackedFileInternal.h"
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

using namespace gz_dbsegment_detail;

// ---- Record sizes, range statistics and the header check ------------------------------------------------

void __cdecl RangeTree::AddRange(RangeTree *tree, uint32_t offset, uint32_t length)
{
    RangeMap &map = Ranges(tree);
    uint32_t start = offset;
    uint32_t total = length;
    auto next = map.lower_bound(offset);
    // A range that starts exactly where the new one ends is absorbed.
    if (next != map.end() && offset + length == next->first) {
        total += next->second;
        auto after = std::next(next);
        map.erase(next);
        next = after;
    }
    // So is one that ends exactly where the new one starts.
    if (next != map.begin()) {
        auto before = std::prev(next);
        if (before->first + before->second == offset) {
            start = before->first;
            total += before->second;
            map.erase(before);
        }
    }
    map.try_emplace(next, start, total);
}

bool cGZDBSegmentPackedFile::AddFreeRangeTo(uint32_t offset, uint32_t length, uint8_t which)
{
    if (offset == 0 || length == 0) return true;
    uint32_t selector = ((mOptions & 0x10) != 0) ? which : 1u;
    // ORIGINAL-BUG: `selector` is not range checked; 0 selects +0x18c, 2 and 3 select +0x194 (1 selects +0x184),
    // and any larger byte indexes past the object. Callers pass 0 or 1.
    RangeTree *tree = &mFreeRanges + (selector ^ 1u);
    RangeTree::AddRange(tree, offset, length);
    mFreeRangesDirty = 1;
    return true;
}

bool cGZDBSegmentPackedFile::AddFreeRange(uint32_t offset, uint32_t length)
{
    if (offset != 0 && length != 0) {
        RangeTree::AddRange(&mFreeRanges, offset, length);
        mFreeRangesDirty = 1;
    }
    return true;
}

namespace {

// EditCompressedTypes and EditCompressedGroups differ only in the set: the pointer lives at +0x1ac or +0x1b0.
void EditSet(cRZCriticalSection *lock, IdSet *&set, uint32_t value, bool add)
{
    As<ifaces::cIGZCriticalSection>(lock)->Lock();
    if (add) {
        if (set == nullptr) set = new IdSet();
        set->insert(value);
    } else {
        // ORIGINAL-BUG: no set (null pointer) is dereferenced here.
        set->erase(value);
        if (set->empty()) {
            delete set;
            set = nullptr;
        }
    }
    As<ifaces::cIGZCriticalSection>(lock)->Unlock();
}

} // namespace

bool cGZDBSegmentPackedFile::EditCompressedTypes(uint32_t value, bool add)
{
    EditSet(&mSegment.mLock, mCompressedTypes, value, add);
    return false;
}

bool cGZDBSegmentPackedFile::EditCompressedGroups(uint32_t value, bool add)
{
    EditSet(&mSegment.mLock, mCompressedGroups, value, add);
    return false;
}

bool cGZDBSegmentPackedFile::PassesFilter(const uint32_t *id, uint32_t size)
{
    if (size - 0x32u > 0xf423ceu) return false;
    if (id[0] == 0xe86b1eefu) return false;
    if (mCompressAll != 0) return true;
    if (mCompressedTypes != nullptr) {
        if (mCompressedTypes->find(id[0]) != mCompressedTypes->end()) return true;
    }
    if (mCompressedGroups != nullptr) return mCompressedGroups->find(id[1]) != mCompressedGroups->end();
    return false;
}

// ---- The reference-count maps ----------------------------------------------------------------------------------

namespace {

void *NodeOf(RefMap::iterator it)
{
    static_assert(sizeof(it) == sizeof(void *), "iterators are node pointers");
    void *node;
    std::memcpy(&node, &it, sizeof(node));
    return node;
}

// One map's share of AdjustRefs / RebuildRefs: count a key up (inserting it with 1 when absent) ...
void CountUp(RefMap &map, uint32_t key)
{
    auto it = map.find(key);
    if (it != map.end()) {
        ++it->second;
    } else {
        uint32_t pair[2] = {key, 1};
        HashMap32::EmplaceResult result;
        reinterpret_cast<HashMap32 *>(&map)->Emplace(&result, pair);
    }
}

// ... or down: above 1 (signed) it is decremented, otherwise the entry is erased.
void CountDown(RefMap &map, uint32_t key)
{
    auto it = map.find(key);
    if (it == map.end()) return;
    int32_t count = static_cast<int32_t>(it->second);
    if (count > 1) {
        it->second = static_cast<uint32_t>(count - 1);
    } else {
        map.erase(it);
    }
}

} // namespace

HashMap32::EmplaceResult *HashMap32::Emplace(EmplaceResult *out, const uint32_t *pair)
{
    // The original looks the key up first and allocates its 0x10-byte node only when it is absent.
    auto result = reinterpret_cast<RefMap *>(this)->try_emplace(pair[0], pair[1]);
    out->node = NodeOf(result.first);
    out->inserted = result.second ? 1 : 0;
    return out;
}

void cGZDBSegmentPackedFile::AdjustRefs(uint32_t key1, uint32_t key2, bool increment)
{
    if (mTrackRefs == 0) return;
    if (increment) CountUp(*mTypeRefs, key1); else CountDown(*mTypeRefs, key1);
    if (mTrackRefs == 0) return;
    if (increment) CountUp(*mGroupRefs, key2); else CountDown(*mGroupRefs, key2);
}

void cGZDBSegmentPackedFile::RebuildRefs()
{
    if (mTrackRefs == 0) return;
    // A vector of 16-byte entries (begin, end, capacity) filled with the keys of the index.
    uint32_t *keys[3] = {nullptr, nullptr, nullptr};
    mIndex->GetResourceKeyList(keys, nullptr);
    for (uint32_t *entry = keys[0]; entry != keys[1]; entry += 4) {
        CountUp(*mTypeRefs, entry[0]);
        CountUp(*mGroupRefs, entry[1]);
    }
    if (keys[0] != nullptr) {
        uint32_t count = static_cast<uint32_t>(reinterpret_cast<uint8_t *>(keys[2]) - reinterpret_cast<uint8_t *>(keys[0])) >> 4;
        FreeThroughAllocatorService(keys[0], count);
    }
}

uint32_t cGZDBSegmentPackedFile::SetRefTracking(uint8_t enable)
{
    auto *lock = As<ifaces::cIGZCriticalSection>(&mSegment.mLock);
    lock->Lock();
    if (mTrackRefs != enable) {
        mTrackRefs = enable;
        delete mTypeRefs;
        mTypeRefs = nullptr;
        delete mGroupRefs;
        mGroupRefs = nullptr;
        if (enable != 0) {
            mTypeRefs = new RefMap();
            mGroupRefs = new RefMap();
            RebuildRefs();
        }
    }
    // The original ends with this call and leaves its result in eax.
    return static_cast<uint32_t>(lock->Unlock());
}

// ---- The pending-record tree at +0x110 --------------------------------------------------------------------

bool cGZDBSegmentPackedFile::FlushOpenRecords()
{
    if ((mOpenMode & 3) == 0) return false;
    if (mOpenRecords.size == 0) return true;
    // A list that holds one reference to every record: each is AddRef'd to make a temporary, AddRef'd again for the
    // list, and the temporary is released. (The original's list is a circular std::list with 0xc-byte nodes.)
    std::vector<ifaces::cIGZPersistDBRecord *> held;
    for (const auto &entry : *reinterpret_cast<const PendingMap *>(&mOpenRecords)) {
        ifaces::cIGZPersistDBRecord *handle = SubOf(entry.second);
        if (handle != nullptr) handle->AddRef();
        if (handle != nullptr) handle->AddRef();
        held.push_back(handle);
        if (handle != nullptr) handle->Release();
    }
    auto *segment = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    for (ifaces::cIGZPersistDBRecord *handle : held) segment->CloseRecord(handle);
    for (ifaces::cIGZPersistDBRecord *handle : held) {
        if (handle != nullptr) handle->Release();
    }
    return true;
}

bool SegmentView::DoAbortRecord(void *handle)
{
    cGZDBSegmentPackedFile *primary = Primary(this);
    if ((primary->mOpenMode & 3) == 0) return false;
    // The record's key: the 16 bytes the handle's GetKey points at, copied before the lookup.
    uint32_t key[4];
    std::memcpy(key, As<ifaces::cIGZPersistDBRecord>(handle)->GetKey(), sizeof(key));
    KeyTree *tree = &primary->mOpenRecords;
    KeyTreeNode *found;
    tree->Find(&found, key);
    PendingMap &map = *reinterpret_cast<PendingMap *>(tree);
    // From the node the lookup found, while the key stays the same: the record whose sub-object is the handle.
    for (auto it = MapIterator<PendingMap>(found); it != map.end(); ++it) {
        if (std::memcmp(&it->first, key, sizeof(key)) != 0) return false;
        void *record = it->second;
        if (SubOf(record) != handle) continue;
        As<ifaces::cGZDBRecordPackedFile>(record)->Invalidate();
        As<ifaces::cIGZPersistDBSerialRecord>(record)->Release();
        map.erase(it);
        return true;
    }
    return false;
}

namespace {

// CountTypes and CountGroups: the number of different values of one key word (type or group). The reference-count
// map answers when there is one; otherwise the index and the open records are merged.
template <bool Groups> uint32_t CountDistinct(cGZDBSegmentPackedFile *file, const RefMap *refs)
{
    As<ifaces::cIGZCriticalSection>(&file->mSegment.mLock)->Lock();
    uint32_t count;
    if (refs != nullptr) {
        count = static_cast<uint32_t>(refs->size());
    } else {
        std::set<uint32_t> values;
        if constexpr (Groups) file->mIndex->CollectGroups(&values); else file->mIndex->CollectTypes(&values);
        for (const auto &entry : *reinterpret_cast<const PendingMap *>(&file->mOpenRecords))
            values.insert(entry.first.w[Groups ? 1 : 0]);
        count = static_cast<uint32_t>(values.size());
    }
    As<ifaces::cIGZCriticalSection>(&file->mSegment.mLock)->Unlock();
    return count;
}

} // namespace

uint32_t cGZDBSegmentPackedFile::CountTypes()
{
    return CountDistinct<false>(this, mTypeRefs);
}

uint32_t cGZDBSegmentPackedFile::CountGroups()
{
    return CountDistinct<true>(this, mGroupRefs);
}

uint32_t SegmentView::DoGetRecordSize(const uint32_t *record)
{
    cGZDBSegmentPackedFile *primary = Primary(this);
    primary->EnsureLoaded();
    uint32_t pending = 0xffffffffu;
    uint8_t state = primary->mOpenMode;
    if ((state & 3) == 0) return pending;
    if ((state & 2) != 0) {
        KeyTree *tree = &primary->mOpenRecords;
        KeyTreeNode *found;
        tree->Find(&found, record);
        if (found != tree->head) {
            auto *held = As<ifaces::cIGZPersistDBSerialRecord>(reinterpret_cast<void *>(static_cast<uintptr_t>(found->value)));
            pending = held->GetSize();
            if (pending != 0xffffffffu) return pending;
        }
    }
    if (As<ifaces::cIGZDBSegmentPackedFile>(primary)->IsRecordCompressed(AsKey(record))) return primary->LookupRecordEnd(record);
    KeyCopy keyCopy(record);
    const uint32_t *hit = primary->mIndex->FindRecord(keyCopy.Get(), primary->mMatchFlagA, primary->mMatchFlagB);
    return hit != nullptr ? hit[1] : pending;
}

// ---- File space, the commit slot, the record walk and the scratch copy ---------------------------------------

void **RangeTree::Erase(void **out, void *node)
{
    // The node after the erased one (computed first, as the original does), then the erase.
    auto next = std::next(MapIterator<RangeMap>(node));
    Ranges(this).erase(MapIterator<RangeMap>(node));
    *out = NodeOf(next);
    return out;
}

bool cGZDBSegmentPackedFile::AllocateSpaceUnlocked(uint32_t *outOffset, uint32_t length)
{
    if ((mOpenMode & 3) == 0 || (mOpenMode & 2) == 0) return false;
    auto *file = As<ifaces::cIGZFile>(&mFile);
    uint32_t fileLength = file->Length();
    RangeTree *tree = &mFreeRanges;
    RangeMap &free = Ranges(tree);
    uint32_t offset = fileLength; // where a plain growth of the file starts
    uint32_t growth = length;     // and by how much
    if ((mOptions & 2) != 0) {
        auto best = free.end();
        uint32_t bestLength = 0xffffffffu;
        for (auto it = free.begin(); it != free.end();) {
            uint32_t start = it->first, size = it->second;
            if (start < fileLength && start + size <= fileLength) {
                if (size >= length && size < bestLength) {
                    best = it;
                    bestLength = size;
                }
                ++it;
            } else {
                // A range outside the file is dropped.
                mFreeRangesDirty = 1;
                it = free.erase(it);
            }
        }
        if (best != free.end()) {
            uint32_t start = best->first, size = best->second;
            *outOffset = start;
            mFreeRangesDirty = 1;
            auto after = std::next(best);
            void *scratch;
            tree->Erase(&scratch, NodeOf(best));
            if (size - length != 0) {
                // The rest of the range goes back next to its old neighbour (map::try_emplace with a hint; 0x9b2e20 is
                // that library function).
                free.try_emplace(after, start + length, size - length);
            }
            return true;
        }
        if (mFreeRanges.size != 0) {
            auto last = std::prev(free.end());
            if (last->second + last->first == fileLength) {
                *outOffset = last->first;
                growth = length - last->second;
                if (file->SetLength(fileLength + growth)) {
                    void *scratch;
                    tree->Erase(&scratch, NodeOf(last));
                    return true;
                }
            }
        }
    }
    // Grow the file at its end. After a failed growth for the last range, this tries the same size once more and
    // then reports the old length.
    if (file->SetLength(offset + growth)) {
        *outOffset = offset;
        return true;
    }
    return false;
}

bool cGZDBSegmentPackedFile::AllocateSpace(uint32_t *outOffset, uint32_t length)
{
    if ((mOptions & 0xa) == 0xa) {
        if (mHeaderDirty == 0) {
            As<ifaces::cIGZDBSegmentPackedFile>(this)->WriteAt(const_cast<uint32_t *>(&ts2_rva_e5f440), 0, 4);
            mHeaderDirty = 1;
        }
        if (mHeader.holeSize != 0) {
            AddFreeRangeTo(mHeader.holeOffset, mHeader.holeSize, 1);
            mHeader.holeCount = 0;
            mHeader.holeSize = 0;
            mHeader.holeOffset = 0;
            mHeaderDirty = 1;
            mFreeRangesDirty = 1;
        }
        if (mHeader.indexSize != 0) {
            AddFreeRangeTo(mHeader.indexOffset, mHeader.indexSize, 1);
            mHeader.indexCount = 0;
            mHeader.indexSize = 0;
            mHeader.indexOffset = 0;
            mHeaderDirty = 1;
            mIndexDirty = 1;
        }
    }
    return AllocateSpaceUnlocked(outOffset, length);
}

bool cGZDBSegmentPackedFile::VisitAllRecords()
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    bool result = (mOpenMode & 3) != 0;
    uint32_t *keys[3] = {nullptr, nullptr, nullptr};
    mIndex->GetResourceKeyList(keys, nullptr);
    auto *segment = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    for (uint32_t *entry = keys[0]; entry != keys[1]; entry += 4) {
        // ORIGINAL-BUG: a store entry without a record (a null lookup) is dereferenced.
        KeyCopy keyCopy(entry);
        const uint32_t *hit = mIndex->FindRecord(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
        if ((hit[2] & 0x7fffffffu) <= hit[1]) continue;
        void *object = nullptr;
        if (!segment->DoOpenRecord(AsKey(entry), &object, 1)) {
            if (object != nullptr) As<ifaces::cIGZPersistDBRecord>(object)->Release();
            result = false;
            break;
        }
        // ORIGINAL-BUG: the object is not checked for null before its Close is called.
        As<ifaces::cIGZPersistDBRecord>(object)->Close();
        if (object != nullptr) As<ifaces::cIGZPersistDBRecord>(object)->Release();
    }
    if (keys[0] != nullptr) {
        uint32_t count = static_cast<uint32_t>(reinterpret_cast<uint8_t *>(keys[2]) - reinterpret_cast<uint8_t *>(keys[0])) >> 4;
        FreeThroughAllocatorService(keys[0], count);
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

uint32_t cGZDBSegmentPackedFile::CopyToFile(void *path, uint32_t a2, uint32_t a3, uint32_t a4)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    uint32_t result = 0xffffffffu;
    auto *scratch = static_cast<cGZDBSegmentPackedFile *>(::operator new(sizeof(cGZDBSegmentPackedFile)));
    scratch->Construct();
    auto *file = As<ifaces::cIGZDBSegmentPackedFile>(scratch);
    file->AddRef();
    if (file->Init()) {
        if (file->SetPath(path)) {
            // Open (1, flag): flag is 0 only when both a3 and a4 have a nonzero low byte.
            uint8_t flag = ((a3 & 0xff) == 0 || (a4 & 0xff) == 0) ? 1 : 0;
            auto *segment = As<ifaces::cIGZPersistDBSegment>(&scratch->mSegment);
            if (segment->Open(1, flag)) {
                result = As<ifaces::cIGZDBSegmentPackedFile>(this)->CopyRecordsTo(
                    segment, reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(static_cast<uintptr_t>(a2)), a3, a4);
                segment->Close();
            }
        }
        file->Shutdown();
    }
    file->Release();
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

bool cGZDBSegmentPackedFile::WriteRecordData(const uint32_t *key, const void *data, uint32_t size)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    bool result = false;
    if ((mOpenMode & 3) != 0 && (mOpenMode & 2) != 0) {
        KeyTree *tree = &mOpenRecords;
        KeyTreeNode *found;
        tree->Find(&found, key);
        if (found == tree->head) {
            uint32_t keyCopy[4];
            std::memcpy(keyCopy, key, sizeof(keyCopy));
            EnsureStore();
            // The store removes a record that has this key and says where its space was: {offset, length, flags}.
            uint32_t removed[3];
            if (mIndex->RemoveRecord(AsKey(keyCopy), removed, mMatchFlagA, mMatchFlagB)) {
                AdjustRefs(key[0], key[1], false);
                AddFreeRangeTo(removed[0], removed[1], (removed[2] >> 31) ^ 1u);
                mIndexDirty = 1;
            }
            uint32_t offset = 0;
            auto *self = As<ifaces::cIGZDBSegmentPackedFile>(this);
            if (self->AllocateSpace(&offset, size)) {
                uint32_t record[3] = {offset, size, size & 0x7fffffffu};
                if (self->WriteAt(const_cast<void *>(data), offset, size) != 0) {
                    mIndex->AddRecord(AsKey(keyCopy), record);
                    AdjustRefs(key[0], key[1], true);
                    mIndexDirty = 1;
                    result = true;
                } else if (offset != 0 && size != 0) {
                    RangeTree::AddRange(&mFreeRanges, offset, size);
                    mFreeRangesDirty = 1;
                }
            }
        }
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

void *GZServiceAllocate(size_t bytes)
{
    return as2::AllocateViaAllocatorService(static_cast<uint32_t>(bytes), 0);
}

void GZServiceFree(void *memory)
{
    as2::FreeViaAllocatorService(memory, 0);
}

void cGZDBSegmentPackedFile::GetInfo(PackedFileInfo *out)
{
    auto *sub = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    sub->Lock();
    const uint32_t dirKey[4] = {0xe86b1eefu, 0xe86b1eefu, 0x286b1f03u, 0};
    out->zero0C = 0;
    out->headerBytes = 0x60;
    out->extentA = mHeader.indexOffset;
    out->extentALength = mHeader.indexSize;
    out->extentB = mHeader.holeOffset;
    out->extentBLength = mHeader.holeSize;
    uint32_t keyCopy[4];
    std::memcpy(keyCopy, dirKey, sizeof(keyCopy));
    uint32_t indexOffset;
    uint32_t indexSize;
    if (sub->DoTestForRecord(AsKey(keyCopy))) {
        indexOffset = As<ifaces::cIGZDBSegmentPackedFile>(this)->GetRecordOffset(AsKey(keyCopy));
        indexSize = sub->DoGetRecordSize(AsKey(keyCopy));
    } else {
        indexOffset = 0;
        indexSize = 0;
    }
    out->indexOffset = indexOffset;
    out->indexSize = indexSize;
    out->totalA = 0;
    for (const auto &range : *reinterpret_cast<const RangeMap *>(&mFreeRanges.head)) {
        out->freeA.emplace_back(range.first, range.second);
        out->totalA += range.second;
    }
    out->totalB = 0;
    for (const auto &range : *reinterpret_cast<const RangeMap *>(&mPendingFreeRanges.head)) {
        out->freeB.emplace_back(range.first, range.second);
        out->totalB += range.second;
    }
    out->fileLength = As<ifaces::cIGZFile>(&mFile)->Length();
    out->storeEnd = 0;
    out->storeBegin = 0;
    if (ifaces::cIGZDBPFIndex *store = mIndex) {
        out->storeBegin = store->GetTotalSize() - out->indexSize;
        out->storeEnd = store->GetTotalMemSize() - out->indexSize;
    }
    sub->Unlock();
}

bool SegmentView::DoDeleteRecord(const uint32_t *key)
{
    cGZDBSegmentPackedFile *primary = Primary(this);
    primary->EnsureLoaded();
    uint8_t state = primary->mOpenMode;
    if ((state & 3) == 0 || (state & 2) == 0) return false;
    KeyTree *tree = &primary->mOpenRecords;
    KeyTreeNode *found;
    tree->Find(&found, key);
    if (found != tree->head) return false;
    uint32_t keyCopy[4];
    std::memcpy(keyCopy, key, sizeof(keyCopy));
    primary->EnsureStore();
    uint32_t removed[3];
    if (!primary->mIndex->RemoveRecord(AsKey(keyCopy), removed, primary->mMatchFlagA, primary->mMatchFlagB)) return false;
    primary->AdjustRefs(key[0], key[1], false);
    primary->AddFreeRangeTo(removed[0], removed[1], (removed[2] >> 31) ^ 1u);
    primary->mIndexDirty = 1;
    return true;
}

void cGZDBSegmentPackedFile::EnsureStore()
{
    if (mIndex != nullptr && mIndex->IsModifiable()) return;
    auto *fresh = NewModifiableIndex();
    if (ifaces::cIGZDBPFIndex *old = mIndex) {
        struct Entry28 {
            uint32_t w[7];
        };
        // The old store says how many entries it has, fills a zeroed vector of them, and the new store takes it.
        uint32_t count = old->GetCount();
        std::vector<Entry28> entries(count);
        if (!entries.empty()) {
            old->CopyEntries(entries.data(), mSegment.mNamespace);
            uint32_t filled = static_cast<uint32_t>(entries.size());
            fresh->SetEntries(entries.data(), filled, mSegment.mNamespace);
        }
        mIndex->DeletingDestructor(1);
    }
    mIndex = fresh;
}

uint32_t cGZDBSegmentPackedFile::ReadRecordData(const uint32_t *key, void *destination, uint32_t *size)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    uint32_t result = 0xffffffffu;
    uint32_t bytesRead = 0;
    EnsureLoaded();
    uint8_t state = mOpenMode;
    if ((state & 3) != 0 && (state & 1) != 0) {
        bool pending = false;
        if ((state & 2) != 0) {
            KeyTree *tree = &mOpenRecords;
            KeyTreeNode *found;
            tree->Find(&found, key);
            if (found != tree->head) {
                // The record being written answers with flags through the slot 5 of its secondary vtable.
                ifaces::cIGZPersistDBRecord *sub = SubOf(reinterpret_cast<void *>(static_cast<uintptr_t>(found->value)));
                pending = (sub->GetAccessFlags() & 2) != 0;
            }
        }
        if (pending) {
            *size = 0;
        } else {
            KeyCopy keyCopy(key);
            const uint32_t *record = mIndex->FindRecord(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
            if (record != nullptr) {
                if (destination == nullptr) {
                    *size = 0;
                    result = record[2] & 0x7fffffffu;
                } else {
                    uint32_t wanted = *size;
                    if (record[1] < wanted) wanted = record[1];
                    auto *file = As<ifaces::cIGZFile>(&mFile);
                    uint32_t landed = file->SeekToPosition(record[0] + mBaseOffset);
                    if (landed == record[0] + mBaseOffset) {
                        if (file->Read(destination, wanted) != 0) bytesRead = wanted;
                    }
                    *size = bytesRead;
                    result = bytesRead;
                }
            }
        }
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

uint32_t cGZDBSegmentPackedFile::LookupRecordEnd(const uint32_t *record)
{
    uint32_t copy[4];
#pragma loop(no_vector)
    for (int i = 0; i < 4; ++i) copy[i] = record[i];
    const uint32_t *hit = mIndex->FindRecord(AsKey(copy), mMatchFlagA, mMatchFlagB);
    if (hit == nullptr) return 0xffffffffu;
    uint32_t end = hit[2] & 0x7fffffffu;
    return end > hit[1] ? end : 0xffffffffu;
}

bool cGZDBSegmentPackedFile::GetFreeRangeTotals(uint32_t *count, uint32_t *total)
{
    *count = 0;
    *total = 0;
    if ((mOpenMode & 3) == 0) return false;
    // The tree at +0x184 first, then the one at +0x18c, each in key order.
    for (void **head : {&mFreeRanges.head, &mPendingFreeRanges.head}) {
        const RangeMap &map = *reinterpret_cast<const RangeMap *>(head);
        for (const auto &entry : map) {
            ++*count;
            *total += entry.second;
        }
    }
    return true;
}

bool cGZDBSegmentPackedFile::CheckExtents()
{
    auto *file = As<ifaces::cIGZFile>(&mFile);
    auto length = [file]() { return file->Length(); };
    if (mHeader.magic != ts2_rva_10f5030 || mHeader.majorVersion > 1 || mHeader.indexCount >= 0x7ffffffu) return false;
    if (!(mHeader.indexOffset < length())) return false;
    if (mHeader.indexSize + mHeader.indexOffset > length()) return false;
    if (mHeader.holeCount >= 0x7ffffffu) return false;
    if (!(mHeader.holeOffset < length())) return false;
    if (mHeader.holeSize + mHeader.holeOffset > length()) return false;
    return mHeader.indexMinorVersion != 0;
}
