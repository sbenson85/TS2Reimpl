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

// ---- Close, reopen, record counts and the copy slots (the large helpers stay original code) ----------------------

// ---- The index from the segment cache (0x9ae5c0) ---------------------------------------------------------------------

namespace {

// A free range as the cache stores it. The vector the original reads them into is not zeroed (0x9a6b40 only allocates),
// so the element's constructor leaves it alone.
struct CachedRange {
    uint32_t start;
    uint32_t length;
    CachedRange() {}
};
static_assert(sizeof(CachedRange) == 8, "cached free range");

template <typename Index> ifaces::cIGZDBPFIndex *AsIndex(Index *index)
{
    return reinterpret_cast<ifaces::cIGZDBPFIndex *>(static_cast<cIGZDBPFIndex *>(index));
}

} // namespace

uint8_t cGZDBSegmentPackedFile::LoadCachedIndex()
{
    using ReadFn = uint8_t(__thiscall *)(void *, void *, uint32_t);
    using RecordFn = uint8_t(__thiscall *)(void *);
    cGZPersistResourceKey key{0x86d4a89u, 0, StringKeyParser::Crc32(mPath.mString.c_str(), 1), 0};
    void *record = nullptr;
    uint8_t result = 0;
    void *cache = mSegment.mSegmentCache;
    if (cache != nullptr && As<ifaces::cIGZPersistDBSegment>(cache)->OpenRecord(&key, &record, 1)) {
        auto read = [record](void *to, uint32_t size) { return VirtualCall<ReadFn, 8>(record, to, size) != 0; };
        uint32_t stamp = 0;
        bool loaded = [&]() {
            uint32_t word = 0;
            if (!read(&word, 4) || word != 3) return false;
            stamp = As<ifaces::cIGZPersistDBSegment>(&mSegment)->GetSegmentChangeID();
            if (!read(&word, 4) || word != stamp) return false;
            mBaseOffset = 0;
            if (!read(&mHeader, sizeof(mHeader)) || !CheckExtents()) return false;
            mHeaderDirty = 0;
            if (mIndex != nullptr) mIndex->DeletingDestructor(1);
            mIndex = nullptr;
            {
                std::vector<cGZDBPFIndexEntry> entries(mHeader.indexCount);
                const uint32_t count = static_cast<uint32_t>(entries.size());
                if (!read(entries.data(), count * static_cast<uint32_t>(sizeof(cGZDBPFIndexEntry)))) return false;
                bool taken = false;
                if (mHeader.indexCount >= 0x40) {
                    mIndex = AsIndex(new cGZDBPFIndexCompressed);
                    // Slot 18 answers whether it took them (the generated view declares it void).
                    using SetEntriesFn = uint8_t(__thiscall *)(void *, const void *, uint32_t, void *);
                    taken = VirtualCall<SetEntriesFn, 18>(mIndex, entries.data(), count, mSegment.mNamespace) != 0;
                }
                if (!taken) {
                    if (mIndex != nullptr) mIndex->DeletingDestructor(1);
                    mIndex = AsIndex(new cGZDBPFIndexStatic);
                    mIndex->SetEntries(entries.data(), count, mSegment.mNamespace);
                }
                RebuildRefs();
                mIndexDirty = 0;
            }
            mIndex->AllEntriesWithin(0, As<ifaces::cIGZFile>(&mFile)->Length());
            ClearRanges(&mFreeRanges);
            ClearRanges(&mPendingFreeRanges);
            uint32_t ranges = 0;
            if (!read(&ranges, 4)) return false;
            {
                std::vector<CachedRange> cached(ranges);
                if (!read(cached.data(), static_cast<uint32_t>(cached.size() * sizeof(CachedRange)))) return false;
                // ORIGINAL-BUG: the header's count decides how many are taken, not the count the cache gave.
                const CachedRange *range = cached.data();
                for (uint32_t i = 0; i < mHeader.holeCount; ++i) Ranges(&mFreeRanges).try_emplace(range[i].start, range[i].length);
                mFreeRangesDirty = 0;
            }
            if (mHeader.holeOffset != 0 && (mHeader.holeOffset < 0x60 || mHeader.holeOffset >= As<ifaces::cIGZFile>(&mFile)->Length()))
                return false;
            return mHeader.holeCount == mHeader.holeSize >> 3;
        }();
        if (loaded) {
            mFileStamp = stamp;
            result = 1;
        }
        VirtualCall<RecordFn, 6>(record);
    }
    if (record != nullptr) VirtualCall<RecordFn, 2>(record);
    return result;
}

void cGZDBSegmentPackedFile::SaveCachedIndex()
{
    using WriteFn = uint8_t(__thiscall *)(void *, const void *, uint32_t);
    using SetSizeFn = uint32_t(__thiscall *)(void *, uint32_t);
    using RecordFn = uint8_t(__thiscall *)(void *);
    static const uint32_t kCacheVersion = 3;
    cGZPersistResourceKey key{0x86d4a89u, 0, StringKeyParser::Crc32(mPath.mString.c_str(), 1), 0};
    void *cache = mSegment.mSegmentCache;
    if (cache == nullptr) return;
    void *record = nullptr;
    if (As<ifaces::cIGZPersistDBSegment>(cache)->OpenRecord(&key, &record, 2)) {
        VirtualCall<SetSizeFn, 11>(record, 0);
        auto write = [record](const void *from, uint32_t size) { return VirtualCall<WriteFn, 9>(record, from, size) != 0; };
        bool written = [&]() {
            if (!write(&kCacheVersion, 4)) return false;
            const uint32_t stamp = As<ifaces::cIGZPersistDBSegment>(&mSegment)->GetSegmentChangeID();
            if (!write(&stamp, 4)) return false;
            if (!write(&mHeader, sizeof(mHeader))) return false;
            if (mHeader.indexCount != 0) {
                std::vector<cGZDBPFIndexEntry> entries(mHeader.indexCount);
                mIndex->CopyEntries(entries.data(), mSegment.mNamespace);
                const uint32_t count = static_cast<uint32_t>(entries.size());
                if (!write(entries.data(), count * static_cast<uint32_t>(sizeof(cGZDBPFIndexEntry)))) return false;
            }
            std::vector<CachedRange> ranges(mFreeRanges.size);
            CachedRange *out = ranges.data();
            for (const auto &range : Ranges(&mFreeRanges)) {
                out->start = range.first;
                out->length = range.second;
                ++out;
            }
            const uint32_t count = static_cast<uint32_t>(ranges.size());
            return write(&count, 4) && write(ranges.data(), count * static_cast<uint32_t>(sizeof(CachedRange)));
        }();
        if (written)
            VirtualCall<RecordFn, 6>(record);
        else
            VirtualCall<RecordFn, 7>(record);
    }
    if (record != nullptr) VirtualCall<RecordFn, 2>(record);
}

// ---- Flush's commit steps: the index and the free ranges into the file (0x9b1d50, 0x9b1a70) -------------------------

uint8_t cGZDBSegmentPackedFile::WriteIndex()
{
    auto *self = As<ifaces::cIGZDBSegmentPackedFile>(this);
    const uint32_t version = self->GetIndexMinorVersion();
    const uint32_t words = version == 1 ? 5 : version == 2 ? 6 : 0;
    if ((mOpenMode & 3) == 0 || (mOpenMode & 2) == 0 || mIndexDirty == 0) return 1;
    const uint32_t count = mIndex->GetCount();
    const uint32_t oldSize = mHeader.indexSize;
    mHeaderDirty = 1;
    const uint32_t size = count * words * 4;
    if (size == 0) {
        const bool added = AddFreeRangeTo(mHeader.indexOffset, oldSize, 0);
        mHeader.indexSize = 0;
        mHeader.indexCount = 0;
        mHeader.indexOffset = 0;
        return added ? 1 : 0;
    }
    if (size != oldSize || (mOptions & 0x10) != 0) {
        if (mHeader.indexOffset != 0 && oldSize != 0) {
            RangeTree::AddRange((mOptions & 0x10) != 0 ? &mPendingFreeRanges : &mFreeRanges, mHeader.indexOffset, oldSize);
            mFreeRangesDirty = 1;
        }
        mHeader.indexCount = 0;
        mHeader.indexOffset = 0;
        mHeader.indexSize = 0;
        if (!AllocateSpaceUnlocked(&mHeader.indexOffset, size)) {
            mHeader.indexSize = 0;
            mHeader.indexOffset = 0;
            return 0;
        }
    }
    mHeader.indexCount = count;
    mHeader.indexSize = size;
    uint8_t written;
    {
        std::vector<uint32_t> entries;
        mIndex->WriteIndexEntries(&entries, mSegment.mNamespace, version);
        written = self->WriteAt(entries.data(), mHeader.indexOffset, mHeader.indexSize);
    }
    if (written != 0 || mHeader.indexOffset == 0) return written;
    if (mHeader.indexSize != 0) {
        RangeTree::AddRange(&mFreeRanges, mHeader.indexOffset, mHeader.indexSize);
        mFreeRangesDirty = 1;
    }
    mHeader.indexSize = 0;
    mHeader.indexCount = 0;
    mHeader.indexOffset = 0;
    return written;
}

uint8_t cGZDBSegmentPackedFile::WriteHoles()
{
    uint8_t result = 1;
    mSwapRangesOnFlush = 0;
    if ((mOpenMode & 3) == 0 || (mOpenMode & 2) == 0 || mFreeRangesDirty == 0) return 1;
    mHeaderDirty = 1;
    auto *file = As<ifaces::cIGZFile>(&mFile);
    const uint32_t length = file->Length();
    auto &ranges = Ranges(&mFreeRanges);
    if (mFreeRanges.size != 0) {
        const auto last = std::prev(ranges.end());
        if (last->second + last->first >= length) {
            file->SetLength(last->first);
            ranges.erase(last);
        }
    }
    if ((mFreeRanges.size + mPendingFreeRanges.size) * 8 != mHeader.holeSize || (mOptions & 0x10) != 0) {
        bool allocate = true;
        if (mHeader.holeSize != 0) {
            result = AddFreeRangeTo(mHeader.holeOffset, mHeader.holeSize, 0) ? 1 : 0;
            if (result != 0) {
                mHeader.holeOffset = 0;
                mHeader.holeSize = 0;
                mHeader.holeCount = 0;
            } else {
                allocate = false;
            }
        }
        if (allocate && (mFreeRanges.size != 0 || mPendingFreeRanges.size != 0)) {
            mHeader.holeSize = (mPendingFreeRanges.size + mFreeRanges.size) * 8;
            result = AllocateSpaceUnlocked(&mHeader.holeOffset, mHeader.holeSize) ? 1 : 0;
        }
    }
    mSwapRangesOnFlush = 1;
    Ranges(&mSpareFreeRanges) = Ranges(&mFreeRanges);
    for (const auto &range : Ranges(&mPendingFreeRanges)) RangeTree::AddRange(&mSpareFreeRanges, range.first, range.second);
    if (result != 0 && mSpareFreeRanges.size != 0) {
        const uint32_t count = mSpareFreeRanges.size;
        mHeader.holeCount = count;
        mHeader.holeSize = count * 8;
        std::vector<CachedRange> holes(count);
        CachedRange *out = holes.data();
        for (const auto &range : Ranges(&mSpareFreeRanges)) {
            out->start = range.first;
            out->length = range.second;
            ++out;
        }
        return As<ifaces::cIGZDBSegmentPackedFile>(this)->WriteAt(holes.data(), mHeader.holeOffset, mHeader.holeSize);
    }
    mHeader.holeCount = 0;
    mHeader.holeOffset = 0;
    mHeader.holeSize = 0;
    return result;
}

// ---- Committing a written record into the file (0x9b1fe0) --------------------------------------------------------------

uint8_t cGZDBSegmentPackedFile::StoreWrittenRecord(void *record, const uint32_t *key)
{
    using CopyToFileFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    if ((mOpenMode & 3) == 0 || (mOpenMode & 2) == 0) return 0;
    cGZPersistResourceKey keyCopy;
    std::memcpy(&keyCopy, key, sizeof(keyCopy));
    EnsureStore();
    cGZDBPFIndexRecord removed;
    if (mIndex->RemoveRecord(&keyCopy, &removed.offset, mMatchFlagA, mMatchFlagB) != 0) {
        AdjustRefs(key[0], key[1], false);
        AddFreeRangeTo(removed.offset, removed.size, static_cast<uint8_t>((removed.memSize >> 31) ^ 1));
        mIndexDirty = 1;
    }
    auto *serial = As<ifaces::cIGZPersistDBSerialRecord>(record);
    const uint32_t size = serial->GetSize();
    if (size == 0) {
        cGZDBPFIndexRecord empty{0, 0, 0};
        mIndex->AddRecord(&keyCopy, &empty.offset);
        AdjustRefs(key[0], key[1], true);
        mIndexDirty = 1;
        return 1;
    }
    auto *self = As<ifaces::cIGZDBSegmentPackedFile>(this);
    if (PassesFilter(key, size)) {
        auto *allocator = &ts2_rva_10cb098;
        auto *data = static_cast<uint8_t *>(allocator->Malloc(size + 2, 0));
        data[size] = 0;
        data[size + 1] = 0;
        void *compressed = nullptr;
        uint32_t compressedSize = 0;
        bool stored = false;
        if (serial->SeekAbsolute(0) && serial->GetFieldVoid(data, size) &&
            PackedFileCompression::TryCompress(data, size, &compressed, &compressedSize)) {
            uint32_t offset = 0;
            if (self->AllocateSpace(&offset, compressedSize) != 0) {
                if (self->WriteAt(compressed, offset, compressedSize) != 0) {
                    cGZDBPFIndexRecord entry{offset, compressedSize, size & 0x7fffffffu};
                    AdjustRefs(key[0], key[1], true);
                    mIndex->AddRecord(&keyCopy, &entry.offset);
                    mIndexDirty = 1;
                    stored = true;
                } else if (offset != 0 && compressedSize != 0) {
                    RangeTree::AddRange(&mFreeRanges, offset, compressedSize);
                    mFreeRangesDirty = 1;
                }
            }
            if (compressed != nullptr) allocator->Free(compressed);
        }
        if (data != nullptr) allocator->Free(data);
        if (stored) return 1;
    }
    uint32_t offset = 0;
    if (self->AllocateSpace(&offset, size) == 0) return 0;
    uint32_t kind;
    std::memcpy(&kind, static_cast<uint8_t *>(record) + 0x44, sizeof(kind));
    const uint8_t copied = kind == 3 ? VirtualCall<CopyToFileFn, 64>(record, offset, size) : 0;
    if (copied != 0) {
        cGZDBPFIndexRecord entry{offset, size, size & 0x7fffffffu};
        mIndex->AddRecord(&keyCopy, &entry.offset);
        // ORIGINAL-BUG: a record stored uncompressed counts its references down; the other two paths count them up.
        AdjustRefs(key[0], key[1], false);
        mIndexDirty = 1;
        return copied;
    }
    if (offset != 0) {
        RangeTree::AddRange(&mFreeRanges, offset, size);
        mFreeRangesDirty = 1;
    }
    return 0;
}

// ---- The readers of the file's own header, index, free ranges and compression directory (Open's fallback) ------------

uint32_t cGZDBSegmentPackedFile::FindHeader()
{
    // The marker is kept with its first byte inverted (as the original keeps it, so it is not found in the program
    // itself), and put right before the search.
    static const char kMarkerStored[16] = {'\x7f', '\x9d', '\x88', '\xec', '\x8f', '\x24', '\x03', '\x6c',
                                           '\xc9', '\xa6', '\x31', '\x56', '\x5b', '\xcf', '\x77', '\x22'};
    uint32_t found = 0xffffffffu;
    auto *file = As<ifaces::cIGZFile>(&mFile);
    const bool wasOpen = file->IsOpen() != 0;
    if (!wasOpen) {
        file->SetPath(mPath.AsInterface());
        file->Open(1, 2, 1);
    }
    if (file->IsOpen() == 0) return found;
    if (file->Length() > 0x10) {
        std::string chunk(0x200, '\0');
        std::string marker(kMarkerStored, sizeof(kMarkerStored));
        marker[0] = static_cast<char>(~marker[0]);
        uint32_t got = 0x200;
        file->SeekToBegin();
        while (got == 0x200) {
            const int32_t chunkAt = file->Position();
            got = 0x200;
            if (file->ReadWithCount(chunk.data(), &got) == 0) break;
            chunk.resize(got);
            const size_t index = chunk.find(marker);
            if (index != std::string::npos) {
                found = static_cast<uint32_t>(chunkAt) + 0x10 + static_cast<uint32_t>(index);
                const int32_t back = file->Position();
                file->SeekToPosition(static_cast<int32_t>(found));
                char magic[4];
                if (file->Read(magic, 4) != 0 && std::memcmp(magic, "DBPF", 4) == 0) break;
                file->SeekToPosition(back);
            }
            file->SeekToRelativePosition(-0x10);
        }
    }
    if (!wasOpen) file->Close();
    return found;
}

uint8_t cGZDBSegmentPackedFile::ReadHeader()
{
    static bool sSearched = false; // the original's byte at 0x10f5034
    auto *file = As<ifaces::cIGZFile>(&mFile);
    if (static_cast<uint32_t>(file->SeekToPosition(static_cast<int32_t>(mBaseOffset))) != mBaseOffset) return 0;
    if (file->Read(&mHeader, sizeof(mHeader)) == 0) return 0;
    const uint32_t major = mHeader.majorVersion, minor = mHeader.minorVersion;
    if (major == 0 || (major == 1 && minor == 0)) mHeader.indexMinorVersion = 1;
    if (major == 0 || (major == 1 && minor < 2)) mHeader.groupInfo = 0;
    if (CheckExtents()) return 1;
    mBaseOffset = FindHeader();
    if (mBaseOffset == 0xffffffffu) {
        mBaseOffset = 0;
        return 0;
    }
    if (sSearched) return 0;
    sSearched = true;
    const uint8_t result = ReadHeader();
    sSearched = false;
    return result;
}

uint8_t cGZDBSegmentPackedFile::ReadIndex()
{
    const uint32_t version = As<ifaces::cIGZDBSegmentPackedFile>(this)->GetIndexMinorVersion();
    const uint32_t words = version == 1 ? 5 : version == 2 ? 6 : 0;
    const uint32_t count = mHeader.indexCount;
    if (count == 0) {
        if (mIndex == nullptr) mIndex = AsIndex(new cGZDBPFIndexModifiable);
        return 1;
    }
    auto *file = As<ifaces::cIGZFile>(&mFile);
    const uint32_t indexAt = mBaseOffset + mHeader.indexOffset;
    if (static_cast<uint32_t>(file->SeekToPosition(static_cast<int32_t>(indexAt))) != mBaseOffset + mHeader.indexOffset) return 0;
    std::vector<uint32_t> buffer(count * words);
    if (file->Read(buffer.data(), mHeader.indexCount * words * 4) == 0) return 0;
    ifaces::cIGZDBPFIndex *old = mIndex;
    if (old != nullptr) old->DeletingDestructor(1);
    bool taken = false;
    if (mHeader.indexCount >= 0x40) {
        mIndex = AsIndex(new cGZDBPFIndexCompressed);
        taken = mIndex->SetEntriesFromIndex(buffer.data(), mHeader.indexCount, mSegment.mNamespace, version) != 0;
    }
    if (!taken) {
        // ORIGINAL-BUG: below 0x40 entries the old index, deleted above but still in +0x30, is deleted a second time.
        if (mIndex != nullptr) mIndex->DeletingDestructor(1);
        mIndex = AsIndex(new cGZDBPFIndexStatic);
        mIndex->SetEntriesFromIndex(buffer.data(), mHeader.indexCount, mSegment.mNamespace, version);
    }
    RebuildRefs();
    mIndex->AllEntriesWithin(0, file->Length());
    mIndexDirty = 0;
    return 1;
}

uint8_t cGZDBSegmentPackedFile::ReadHoles()
{
    if (mHeader.holeCount == 0) return 1;
    auto *file = As<ifaces::cIGZFile>(&mFile);
    const uint32_t holesAt = mBaseOffset + mHeader.holeOffset;
    if (static_cast<uint32_t>(file->SeekToPosition(static_cast<int32_t>(holesAt))) != mHeader.holeOffset + mBaseOffset) return 0;
    std::vector<CachedRange> holes(mHeader.holeCount);
    if (file->Read(holes.data(), mHeader.holeCount << 3) == 0) return 0;
    ClearRanges(&mFreeRanges);
    ClearRanges(&mPendingFreeRanges);
    for (uint32_t i = 0; i < mHeader.holeCount; ++i) Ranges(&mFreeRanges).try_emplace(holes[i].start, holes[i].length);
    bool fine = mHeader.holeOffset == 0 || (mHeader.holeOffset >= 0x60 && mHeader.holeOffset < file->Length());
    fine = fine && mHeader.holeCount == mHeader.holeSize >> 3;
    if (!fine && mFlag2A == 0) return 0;
    mFreeRangesDirty = 0;
    return 1;
}

uint8_t cGZDBSegmentPackedFile::ReadCompressionDirectory()
{
    using KeyMapFn = void(__thiscall *)(void *, cGZPersistResourceKey *, cGZPersistResourceKey *);
    const uint32_t version = mHeader.indexMinorVersion;
    cGZPersistResourceKey key{0xe86b1eefu, 0xe86b1eefu, 0x286b1f03u, 0};
    auto *segment = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    uint32_t size = 0;
    size = segment->DoReadRecord(&key, nullptr, &size);
    if (size == 0xffffffffu) return 1;
    auto *allocator = &ts2_rva_10cb098;
    auto *block = static_cast<uint32_t *>(allocator->Malloc(size + 4, 0));
    uint8_t result = 0;
    if (segment->DoReadRecord(&key, block, &size) != 0) {
        const uint32_t stride = version == 2 ? 20 : 16;
        void *keyMap = mSegment.mNamespace;
        const uint32_t *entry = block;
        for (uint32_t left = size / stride; left != 0; --left) {
            const uint32_t resourceWords = mHeader.indexMinorVersion == 2 ? 1 : 0;
            cGZPersistResourceKey record{entry[0], entry[1], entry[2], resourceWords != 0 ? entry[3] : 0};
            const uint32_t memSize = entry[3 + resourceWords];
            entry += 4 + resourceWords;
            if (keyMap != nullptr) VirtualCall<KeyMapFn, 4>(keyMap, &record, &record);
            mIndex->SetRecordMemSize(&record, memSize);
        }
        result = 1;
    }
    if (block != nullptr) allocator->Free(block);
    return result;
}

uint8_t cGZDBSegmentPackedFile::Close(uint32_t flush)
{
    uint8_t result;
    uint8_t needCommit = 0;
    uint8_t state = mOpenMode;
    if ((state & 3) != 0) {
        if ((state & 2) != 0) {
            result = FlushOpenRecords() ? 1 : 0;
            if ((flush & 0xff) != 0 && result != 0) {
                needCommit = (mIndexDirty != 0 || mFreeRangesDirty != 0 || mHeaderDirty != 0) ? 1 : 0;
                result = As<ifaces::cIGZPersistDBSegment>(&mSegment)->Flush();
            }
            As<ifaces::cIGZFile>(&mFile)->Close();
            std::memset(&mOpenMode, 0, 4);
            // 0x9b0670: the index and the free ranges into the segment cache.
            if (needCommit != 0) SaveCachedIndex();
        } else {
            result = 1;
            As<ifaces::cIGZFile>(&mFile)->Close();
            std::memset(&mOpenMode, 0, 4);
        }
    } else {
        result = 1;
    }
    if (mIndex != nullptr) mIndex->DeletingDestructor(1);
    mIndex = nullptr;
    if (auto *map = static_cast<RefMap *>(mTypeRefs)) {
        delete map;
        mTypeRefs = nullptr;
    }
    if (auto *map = static_cast<RefMap *>(mGroupRefs)) {
        delete map;
        mGroupRefs = nullptr;
    }
    ClearRanges(&mFreeRanges.head);
    ClearRanges(&mPendingFreeRanges.head);
    return result;
}

uint8_t SegmentView::Close()
{
    return Primary(this)->Close(1);
}

uint8_t cGZDBSegmentPackedFile::ReopenIfDirty()
{
    if (As<ifaces::cIGZDBSegmentPackedFile>(this)->IsDirty() == 0) return 1;
    if ((mOptions & 0x10) == 0) return 0;
    uint8_t state = mOpenMode;
    if (Close(0) == 0) return 0;
    return As<ifaces::cIGZPersistDBSegment>(&mSegment)->Open(state & 1u, (state >> 1) & 1u);
}

uint8_t cGZDBSegmentPackedFile::Compact()
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    EnsureLoaded();
    uint8_t result = 0;
    if (mCompacting == 0 && (mOpenMode & 3) != 0) {
        mCompacting = 1;
        result = CompactBody();
        mCompacting = 0;
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}

uint8_t cGZDBSegmentPackedFile::CompactBody()
{
    // Pending ranges join the free ranges.
    for (const auto &range : Ranges(&mPendingFreeRanges)) RangeTree::AddRange(&mFreeRanges, range.first, range.second);
    ClearRanges(&mPendingFreeRanges);

    if (mFreeRanges.size == 0) {
        if (mHeader.holeCount == 0) return 1;
        if (mHeader.holeOffset != 0 && mHeader.holeSize != 0) {
            // +0x184, or +0x18c when bit 4 of the options is set.
            RangeTree *tree = &mFreeRanges + ((mOptions >> 4) & 1u);
            RangeTree::AddRange(tree, mHeader.holeOffset, mHeader.holeSize);
            mFreeRangesDirty = 1;
        }
        mHeader.holeCount = 0;
        mHeader.holeOffset = 0;
        mHeader.holeSize = 0;
        mHeaderDirty = 1;
        if (mFreeRanges.size == 0 && mPendingFreeRanges.size == 0) return 1;
    }

    if ((mOpenMode & 2) == 0 || mOpenRecords.size != 0) return 0;

    auto *sub = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    if (mIndex->IsEmpty()) {
        ClearRanges(&mFreeRanges);
        ClearRanges(&mPendingFreeRanges);
        mHeader.holeCount = 0;
        mHeader.indexCount = 0;
        mHeader.indexOffset = 0;
        mHeader.indexSize = 0;
        mHeaderDirty = 1;
        mHeader.holeOffset = 0;
        mHeader.holeSize = 0;
        sub->Flush();
        As<ifaces::cIGZFile>(&mFile)->SetLength(sizeof(DBPFHeader));
        return 1;
    }

    // The rewritten file goes next to this one, under names made from the prefix.
    uint8_t result = 0;
    cRZString pathB;
    pathB.Construct("TempDBSegmentPackedFile_");
    cRZString pathA;
    pathA.Construct("TempDBSegmentPackedFile_");
    cRZString directory;
    directory.Construct();
    appbase::GetModuleDirectory(mPath.AsInterface(), directory.AsInterface());
    if (MakeTempFileIn(directory.AsInterface(), pathB.AsInterface(), nullptr)) {
        if (MakeTempFileIn(directory.AsInterface(), pathA.AsInterface(), nullptr)) {
            if (cRZFile::PathExists(*pathA.AsInterface())) PathDelete(pathA.AsInterface());
            auto *temp = static_cast<cGZDBSegmentPackedFile *>(::operator new(sizeof(cGZDBSegmentPackedFile)));
            temp->Construct();
            auto *tempFile = As<ifaces::cIGZDBSegmentPackedFile>(temp);
            tempFile->AddRef();
            if (tempFile->Init()) {
                auto *tempSub = As<ifaces::cIGZPersistDBSegment>(&temp->mSegment);
                auto *thisFile = As<ifaces::cIGZFile>(&mFile);
                tempFile->SetPath(pathB.AsInterface());
                tempFile->SetBuffering(0x1000, 0x10000);
                tempSub->SetNamespace(mSegment.mNamespace);
                tempFile->SetCompressAll(0);
                if (tempSub->Open(1, 1)) {
                    thisFile->SetBuffering(0x1000, 0x10000);
                    uint32_t size = mIndex->GetTotalSize();
                    uint32_t where;
                    if (tempFile->AllocateSpace(&where, size)) temp->AddFreeRangeTo(where, size, 1);
                    uint32_t copied = As<ifaces::cIGZDBSegmentPackedFile>(this)->CopyRecordsTo(tempSub, nullptr, 0, 1);
                    uint32_t count = mIndex->GetResourceKeyList(nullptr, nullptr);
                    if (copied == count) {
                        tempFile->SetOptions(2);
                        if (tempSub->Close()) {
                            tempFile->Shutdown();
                            uint32_t saved = mOptions;
                            mOptions = saved & ~1u;
                            if (sub->Close()) {
                                // The segment cache forgets the entry it keeps for this path.
                                cGZPersistResourceKey key{0x86d4a89u, 0, StringKeyParser::Crc32(mPath.mString.c_str(), 1), 0};
                                if (mSegment.mSegmentCache != nullptr) {
                                    As<ifaces::cIGZPersistDBSegment>(mSegment.mSegmentCache)->DeleteRecord(&key);
                                }
                                if (cRZFile::MovePath(*mPath.AsInterface(), *pathA.AsInterface())) {
                                    if (cRZFile::MovePath(*pathB.AsInterface(), *mPath.AsInterface())) {
                                        result = sub->Open(1, 1);
                                    } else {
                                        cRZFile::MovePath(*pathA.AsInterface(), *mPath.AsInterface());
                                    }
                                }
                            }
                            mOptions = saved;
                        }
                    }
                    if (tempSub->IsOpen()) tempSub->Close();
                    thisFile->SetBuffering(static_cast<int32_t>(mReadBufferSize), static_cast<int32_t>(mWriteBufferSize));
                    tempFile->Shutdown();
                } else {
                    tempFile->Shutdown();
                }
            }
            tempFile->Release();
            if (cRZFile::PathExists(*pathA.AsInterface())) PathDelete(pathA.AsInterface());
        }
        if (cRZFile::PathExists(*pathB.AsInterface())) PathDelete(pathB.AsInterface());
    }
    directory.Destruct();
    pathA.Destruct();
    pathB.Destruct();
    return result;
}

// The key-list filter, which the callers of these slots pass as a plain dword.
static ifaces::cIGZPersistResourceKeyFilter *AsFilter(uint32_t filter)
{
    return reinterpret_cast<ifaces::cIGZPersistResourceKeyFilter *>(static_cast<uintptr_t>(filter));
}

uint32_t SegmentView::DoGetRecordCount(uint32_t filter)
{
    if (filter != 0) return As<ifaces::cIGZPersistDBSegment>(this)->DoGetResourceKeyList(nullptr, AsFilter(filter));
    cGZDBSegmentPackedFile *file = Primary(this);
    LoadStoreIfAbsent(file);
    ifaces::cIGZDBPFIndex *store = file->mIndex;
    if (store == nullptr) return 0;
    return store->GetCount();
}

uint32_t SegmentView::DoGetResourceKeyList(uint32_t a, uint32_t b)
{
    cGZDBSegmentPackedFile *file = Primary(this);
    LoadStoreIfAbsent(file);
    ifaces::cIGZDBPFIndex *store = file->mIndex;
    if (store == nullptr) return 0;
    return store->GetResourceKeyList(reinterpret_cast<void *>(static_cast<uintptr_t>(a)), AsFilter(b));
}

bool SegmentView::DoTestForRecord(const uint32_t *key)
{
    cGZDBSegmentPackedFile *file = Primary(this);
    LoadStoreIfAbsent(file);
    if (file->mIndex == nullptr) return false;
    KeyCopy keyCopy(key);
    if (file->mIndex->FindRecord(keyCopy.Get(), file->mMatchFlagA, file->mMatchFlagB) != nullptr) return true;
    KeyTree *tree = &file->mOpenRecords;
    KeyTreeNode *found;
    tree->Find(&found, key);
    return found != tree->head;
}

uint32_t cGZDBSegmentPackedFile::CopyRecordsTo(void *other, uint32_t filter, uint32_t byteA, uint32_t byteB)
{
    auto *sub = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    auto *otherSegment = As<ifaces::cIGZPersistDBSegment>(other);
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    sub->Lock();
    uint32_t copied = 0;
    otherSegment->Lock();
    uint8_t *list[3] = {nullptr, nullptr, nullptr}; // begin, end, capacity of a vector of 16-byte entries
    if ((byteA & 0xff) != 0) {
        otherSegment->GetResourceKeyList(list, AsFilter(filter));
    } else {
        sub->DoGetResourceKeyList(list, AsFilter(filter));
    }
    auto count = [&]() { return static_cast<uint32_t>((list[1] - list[0]) >> 4); };
    for (uint32_t i = 0; i < count(); ++i) {
        uint8_t ok = CopyOneRecord(other, reinterpret_cast<const uint32_t *>(list[0] + i * 16), byteA, byteB);
        if (ok != 0) ++copied;
    }
    otherSegment->Unlock();
    sub->Unlock();
    if (list[0] != nullptr) {
        uint32_t blockCount = static_cast<uint32_t>((list[2] - list[0]) >> 4);
        FreeThroughAllocatorService(list[0], blockCount);
        list[0] = list[1] = list[2] = nullptr;
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return copied;
}

uint8_t cGZDBSegmentPackedFile::CopyOneRecord(void *other, const uint32_t *key, uint32_t intoThisWord, uint32_t keepWord)
{
    const uint8_t intoThis = static_cast<uint8_t>(intoThisWord);
    const uint8_t keep = static_cast<uint8_t>(keepWord);
    // The packed-file interface's GetRecordStoredSize, ReadRecordData and WriteRecordData answers are tested as
    // whole words (cmp eax, -1 / test eax, eax), so they are called with word returns here.
    using StoredSizeFn = uint32_t(__thiscall *)(void *, const uint32_t *);
    using ReadDataFn = uint32_t(__thiscall *)(void *, const uint32_t *, void *, uint32_t *);
    using WriteDataFn = uint32_t(__thiscall *)(void *, const uint32_t *, void *, uint32_t);
    auto *lock = As<ifaces::cIGZCriticalSection>(&mSegment.mLock);
    lock->Lock();
    auto *self = As<ifaces::cIGZDBSegmentPackedFile>(this);
    auto *mine = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    auto *theirs = As<ifaces::cIGZPersistDBSegment>(other);
    auto *rkey = reinterpret_cast<const cGZPersistResourceKey *>(key);
    auto *allocator = &ts2_rva_10cb098;
    ifaces::cIGZPersistDBSegment *source = intoThis != 0 ? theirs : mine;
    uint8_t result = 0;
    if (source->TestForRecord(rkey) != 0) {
        bool plain = true;
        void *packed = nullptr;
        if (theirs->QueryInterface(0x286d4a6a, &packed) != 0) {
            // The packed source and destination: the other file's packed interface and this file, either way round.
            void *from = intoThis != 0 ? packed : static_cast<void *>(self);
            void *to = intoThis != 0 ? static_cast<void *>(self) : packed;
            if (VirtualCall<uint8_t(__thiscall *)(void *, const uint32_t *), 41>(from, key) != 0) {
                uint32_t stored = VirtualCall<StoredSizeFn, 27>(from, key);
                const uint32_t full = intoThis != 0 ? theirs->GetRecordSize(rkey) : LookupRecordEnd(key);
                if (stored != 0xffffffffu) {
                    void *data = allocator->Malloc(stored, 0);
                    if (VirtualCall<ReadDataFn, 28>(from, key, data, &stored) != 0xffffffffu &&
                        VirtualCall<WriteDataFn, 29>(to, key, data, stored) != 0) {
                        VirtualCall<void(__thiscall *)(void *, const uint32_t *, uint8_t, uint32_t), 42>(to, key, 1, full);
                        if (keep == 0) source->DeleteRecord(rkey);
                        result = 1;
                        plain = false;
                    }
                    if (data != nullptr) allocator->Free(data);
                }
            }
            VirtualCall<uint32_t(__thiscall *)(void *), 2>(packed);
        }
        if (plain) {
            uint32_t size = source->GetRecordSize(rkey);
            if (size != 0xffffffffu) {
                void *data = allocator->Malloc(size, 0);
                ifaces::cIGZPersistDBSegment *destination = intoThis != 0 ? mine : theirs;
                if (source->ReadRecord(rkey, data, &size) != 0 && destination->WriteRecord(rkey, data, size) != 0) {
                    if (keep == 0) source->DeleteRecord(rkey);
                    result = 1;
                }
                if (data != nullptr) allocator->Free(data);
            }
        }
    }
    lock->Unlock();
    return result;
}
