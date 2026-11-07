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

// ---- Open, Flush and the header writers ---------------------------------------------------------------------

uint8_t cGZDBSegmentPackedFile::WriteHeader()
{
    if ((mOpenMode & 2) == 0) return 0;
    auto *file = As<ifaces::cIGZFile>(&mFile);
    if (file->IsOpen() == 0) return 0;
    if (mHeaderDirty == 0) return 1;
    if (static_cast<uint32_t>(file->SeekToPosition(mBaseOffset)) != mBaseOffset) return 0;
    // The header: the in-memory copy with a few words replaced.
    uint32_t header[0x18];
    static_assert(sizeof(header) == sizeof(DBPFHeader), "the header is written as 0x18 dwords");
    std::memcpy(header, &mHeader, sizeof(header));
    header[0] = ts2_rva_10f5030;
    header[1] = 1;
    header[2] = 2;
    header[4] = mUserVersionMinor;
    // The original calls _time64(0) here and drops the result; the two words after it are zeroed.
    header[6] = 0;
    header[7] = 0;
    uint8_t written = file->Write(header, 0x60);
    return written != 0 ? 1 : 0;
}

uint8_t cGZDBSegmentPackedFile::InitHeader()
{
    std::memset(mHeader.reserved, 0, sizeof(mHeader.reserved));
    mHeader.magic = ts2_rva_10f5030;
    mHeader.userVersionMajor = mUserVersionMajor;
    mHeader.majorVersion = 1;
    mHeader.minorVersion = 2;
    mHeader.userVersionMinor = mUserVersionMinor;
    mHeader.flags = 0;
    mHeader.created = 0;
    mHeader.modified = 0;
    mHeader.indexMajorVersion = 7;
    mHeader.indexCount = 0;
    mHeader.indexOffset = 0;
    mHeader.indexSize = 0;
    mHeader.holeCount = 0;
    mHeader.holeOffset = 0;
    mHeader.holeSize = 0;
    mHeader.indexMinorVersion = 1;
    mHeader.groupInfo = 0;
    mHeaderDirty = 1;
    if (WriteHeader() == 0) return 0;
    return As<ifaces::cIGZFile>(&mFile)->SetLength(0x60);
}

namespace {

// The readers Open falls back on (this = the primary, no arguments, a byte answer).
uint8_t OlderReader(void *primary, int which)
{
    auto *file = static_cast<cGZDBSegmentPackedFile *>(primary);
    switch (which) {
    case 0: return file->ReadHeader();
    case 1: return file->ReadIndex();
    case 2: return file->ReadHoles();
    default: return file->ReadCompressionDirectory();
    }
}

void SetStateDword(cGZDBSegmentPackedFile *file, uint32_t value)
{
    std::memcpy(&file->mOpenMode, &value, sizeof(value));
}

} // namespace

uint8_t SegmentView::Open(uint32_t read, uint32_t write)
{
    cGZDBSegmentPackedFile *file = Primary(this);
    if ((file->mOpenMode & 3) != 0 || file->mPath.mString.size() == 0 || file->mInitialized == 0) return 0;
    uint8_t wantRead = static_cast<uint8_t>(read), wantWrite = static_cast<uint8_t>(write);
    if (wantRead == 0 && wantWrite == 0) return 0;
    uint32_t state = 0;
    uint32_t mode = 2;
    if (wantRead != 0) state = 1;
    if (wantWrite != 0) {
        state = 3;
        mode = 3;
    }
    SetStateDword(file, state);
    auto *fileObject = As<ifaces::cIGZFile>(&file->mFile);
    fileObject->SetPath(file->mPath.AsInterface());
    if (fileObject->Open(state, mode, 1) == 0) {
        SetStateDword(file, 0);
        return 0;
    }
    uint8_t result = 0; // bl
    uint8_t loaded = 0; // bh: the index was read
    bool indexRead = false, failed = false;
    uint32_t length = fileObject->Length();
    bool tryIndex = true;
    if (length == 0) {
        // An empty file: a writable one gets a header first and then goes on to the index load; a read-only one skips it.
        tryIndex = false;
        if ((file->mOpenMode & 2) != 0 && file->InitHeader() != 0) tryIndex = true;
    }
    if (tryIndex) {
        loaded = LoadIndex(file);
        result = loaded;
        indexRead = loaded != 0;
    }
    if (!indexRead) {
        // The older formats, then (for a writable file that may start empty) a fresh file.
        if (OlderReader(file, 0) != 0 && OlderReader(file, 1) != 0 && OlderReader(file, 2) != 0 && OlderReader(file, 3) != 0) {
            file->mIndexDirty = 0;
            result = 1;
        } else {
            result = 0;
            if (file->mCreateIfMissing != 0 && (file->mOpenMode & 2) != 0) {
                result = file->InitHeader();
                if (result != 0) {
                    ClearRanges(&file->mFreeRanges.head);
                    ClearRanges(&file->mPendingFreeRanges.head);
                    file->mFreeRangesDirty = 1;
                    if (file->mIndex != nullptr) file->mIndex->DeletingDestructor(1);
                    file->mIndex = NewModifiableIndex();
                    file->mIndexDirty = 1;
                }
            }
            failed = result == 0;
        }
    }
    if (failed) {
        fileObject->Close();
        SetStateDword(file, 0);
        return result;
    }
    // Success. A writable file is extended to cover everything the index and the header describe.
    bool extended = false;
    if ((file->mOpenMode & 2) != 0) {
        // ORIGINAL-BUG: the store is not checked for null here (it exists whenever the loaders above succeed).
        uint32_t needed = file->mIndex->GetDataEnd();
        if (needed < 0x60) needed = 0x60;
        uint32_t extentA = file->mHeader.indexOffset + file->mHeader.indexSize;
        if (needed < extentA) needed = extentA;
        uint32_t extentB = file->mHeader.holeOffset + file->mHeader.holeSize;
        if (needed < extentB) needed = extentB;
        if (file->mFreeRanges.size != 0) {
            // The last free range (the library iterator's decrement from end(), 0x9a7660 in the game).
            auto last = std::prev(Ranges(&file->mFreeRanges).end());
            uint32_t end = last->first + last->second;
            if (needed < end) needed = end;
        }
        uint32_t current = fileObject->Length();
        if (current > needed) {
            fileObject->SetLength(needed);
            extended = true;
        }
    }
    if (extended || loaded == 0) file->SaveCachedIndex();
    return result;
}

uint8_t SegmentView::Flush()
{
    cGZDBSegmentPackedFile *file = Primary(this);
    uint8_t result = 1;
    As<ifaces::cIGZPersistDBSegment>(this)->Lock();
    uint8_t state = file->mOpenMode;
    if ((state & 3) != 0 && (state & 2) != 0 && (file->mIndexDirty != 0 || file->mFreeRangesDirty != 0 || file->mHeaderDirty != 0)) {
        file->mHeader.indexMinorVersion = 1;
        // The store's entries (16 bytes each, a block from the allocator service); one with a nonzero fourth word makes it 2.
        uint8_t *list[3] = {nullptr, nullptr, nullptr};
        file->mIndex->GetResourceKeyList(list, nullptr);
        for (uint8_t *entry = list[0]; entry != list[1]; entry += 16) {
            uint32_t word;
            std::memcpy(&word, entry + 12, sizeof(word));
            if (word != 0) {
                file->mHeader.indexMinorVersion = 2;
                break;
            }
        }
        if (list[0] != nullptr) FreeThroughAllocatorService(list[0], static_cast<uint32_t>((list[2] - list[0]) >> 4));
        state = file->mOpenMode;
        if ((state & 3) != 0 && (state & 2) != 0 && file->mIndexDirty != 0) {
            // The directory record is rewritten: the old one removed, the store's list of directory entries written back.
            uint32_t version = As<ifaces::cIGZDBSegmentPackedFile>(file)->GetIndexMinorVersion();
            const uint32_t dirKey[4] = {0xe86b1eefu, 0xe86b1eefu, 0x286b1f03u, 0};
            As<ifaces::cIGZPersistDBSegment>(this)->DoDeleteRecord(AsKey(dirKey)); // the answer is ignored
            std::vector<uint32_t> entries;
            file->mIndex->WriteIndex(&entries, file->mSegment.mNamespace, version);
            if (!entries.empty()) {
                uint32_t bytes = static_cast<uint32_t>(entries.size() * sizeof(uint32_t)) & 0xfffffffcu;
                uint8_t written = As<ifaces::cIGZPersistDBSegment>(this)->DoWriteRecord(AsKey(dirKey), entries.data(), bytes);
                result = written != 0 ? 1 : 0;
            }
        }
        if (result != 0) {
            // The free-space ratio: above the threshold (+0x180) the file is compacted first.
            if ((file->mOptions & 1) != 0 && file->mOpenRecords.size == 0) {
                file->EnsureLoaded();
                if ((file->mOpenMode & 3) != 0) {
                    uint32_t length = As<ifaces::cIGZFile>(&file->mFile)->Length();
                    uint32_t count = 0, total = 0;
                    As<ifaces::cIGZDBSegmentPackedFile>(file)->GetFreeRangeTotals(&count, &total);
                    if (total < length) {
                        float ratio = static_cast<float>(static_cast<double>(total)) / static_cast<float>(static_cast<double>(length - total));
                        if (ratio > file->mCompactRatio) As<ifaces::cIGZDBSegmentPackedFile>(file)->Compact();
                    }
                }
            }
            // The commit steps; the first one that answers 0 ends the sequence with that answer.
            result = file->WriteIndex();
            if (result != 0) result = file->WriteHoles();
            if (result != 0) result = file->WriteHeader();
            if (result != 0) {
                auto *fileObject = As<ifaces::cIGZFile>(&file->mFile);
                result = fileObject->Flush();
                if (result != 0) result = fileObject->SysFlush();
            }
            if (result != 0) {
                if (file->mSwapRangesOnFlush != 0) {
                    RangeTree *current = &file->mFreeRanges;
                    RangeTree *spare = &file->mSpareFreeRanges;
                    std::swap(*current, *spare);
                    ClearRanges(&file->mPendingFreeRanges.head);
                }
                file->mHeaderDirty = 0;
                file->mFreeRangesDirty = 0;
                file->mIndexDirty = 0;
                if (file->mIndex != nullptr) file->mIndex->SetFlagOnAll();
            }
        }
    }
    file->mFileStamp = 0;
    As<ifaces::cIGZPersistDBSegment>(this)->Unlock();
    return result;
}

// ---- Closing a pending record, and the file stamp ---------------------------------------------------------

bool SegmentView::DoCloseRecord(void *handle)
{
    cGZDBSegmentPackedFile *primary = Primary(this);
    if ((primary->mOpenMode & 3) == 0) return false;
    uint32_t key[4];
    std::memcpy(key, As<ifaces::cIGZPersistDBRecord>(handle)->GetKey(), sizeof(key));
    KeyTree *tree = &primary->mOpenRecords;
    KeyTreeNode *found;
    tree->Find(&found, key);
    PendingMap &map = *reinterpret_cast<PendingMap *>(tree);
    for (auto it = MapIterator<PendingMap>(found); it != map.end(); ++it) {
        if (std::memcmp(&it->first, key, sizeof(key)) != 0) return false;
        void *record = it->second;
        if (SubOf(record) != handle) continue;
        // A record still being written (bit 1 of the handle's slot 5) is first committed into the file (its answer unused).
        uint32_t flags = As<ifaces::cIGZPersistDBRecord>(handle)->GetAccessFlags();
        if ((flags & 2) != 0) primary->StoreWrittenRecord(record, key);
        As<ifaces::cGZDBRecordPackedFile>(record)->Invalidate();
        As<ifaces::cIGZPersistDBSerialRecord>(record)->Release();
        map.erase(it);
        return true;
    }
    return false;
}

uint32_t SegmentView::DoGetSegmentChangeID()
{
    cGZDBSegmentPackedFile *file = Primary(this);
    if (file->mIndexDirty != 0 || file->mFreeRangesDirty != 0 || file->mHeaderDirty != 0) return 0;
    if (file->mFileStamp != 0) return file->mFileStamp;
    auto *fileObject = As<ifaces::cIGZFile>(&file->mFile);
    cIGZString *path = file->mPath.AsInterface();
    uint32_t length;
    if (fileObject->IsOpen() != 0) {
        length = fileObject->Length();
    } else {
        length = cRZFile::LengthOfPath(*path);
    }
    // The modification time of the path (PathFileTime, 0x1c2068).
    uint32_t stamp = PathFileTime(path, 2);
    file->mFileStamp = (length << 16) + stamp;
    return file->mFileStamp;
}

// ---- Record compression helpers -------------------------------------------------------------------------------

namespace {

// 0x9cc0c2 builds the compressor on the stack: the game's vtable, count 0, flag clear.
cRZFastCompression3 MakeCompressor()
{
    cRZFastCompression3 compressor;
    compressor.mVTable = cRZFastCompression3::VTable();
    compressor.mRefCount = 0;
    compressor.mFlag = false;
    return compressor;
}

} // namespace

bool __stdcall PackedFileCompression::Compress(const void *data, uint32_t size, void **outBuffer, uint32_t *outSize)
{
    void *supplied = *outBuffer;
    if (size >= 40000000u) {
        // Too big to compress: stored as it is.
        if (supplied == nullptr) {
            *outSize = size;
            try {
                *outBuffer = RZMemory::Malloc(size);
                std::memcpy(*outBuffer, data, size);
                return true;
            } catch (...) {
                *outSize = 0;
                *outBuffer = nullptr;
                return false;
            }
        }
        if (*outSize < size) {
            *outSize = 0;
            return false;
        }
        *outSize = size;
        std::memcpy(*outBuffer, data, size);
        return true;
    }
    cRZFastCompression3 compressor = MakeCompressor();
    bool ok = false;
    if (supplied == nullptr) {
        try {
            uint32_t most = compressor.GetMaxLengthRequiredForCompressedData(size);
            *outSize = most;
            *outBuffer = RZMemory::Malloc(most);
        } catch (...) {
            *outBuffer = nullptr;
        }
    }
    if (*outBuffer != nullptr) {
        if (size > 250000u) compressor.mFlag = true; // the quick mode
        if (compressor.CompressData(data, size, *outBuffer, outSize)) {
            ok = true;
        } else {
            *outSize = 0;
            if (supplied == nullptr && *outBuffer != nullptr) {
                RZMemory::Free(*outBuffer);
                *outBuffer = nullptr;
            }
        }
    }
    compressor.Destruct();
    return ok;
}

bool __stdcall PackedFileCompression::TryCompress(const void *data, uint32_t size, void **outBuffer, uint32_t *outSize)
{
    *outBuffer = nullptr;
    *outSize = 0;
    if (size - 0x32u > 0xf423ceu) return false;
    if (!Compress(data, size, outBuffer, outSize)) return false;
    float ratio = static_cast<float>(static_cast<double>(*outSize)) / static_cast<float>(static_cast<double>(size));
    if (ts2_rva_db13e8 > ratio) return true;
    if (*outBuffer != nullptr) RZMemory::Free(*outBuffer);
    *outBuffer = nullptr;
    *outSize = 0;
    return false;
}

bool __stdcall PackedFileCompression::Decompress(const void *source, uint32_t size, uint32_t expected, void **buffer, uint32_t *length)
{
    cRZFastCompression3 compressor = MakeCompressor();
    bool ok = false;
    if (!compressor.HasValidHeader(source, size)) {
        compressor.Destruct();
        return false;
    }
    void *supplied = *buffer;
    uint32_t decompressed = compressor.GetLengthOfDecompressedData(source);
    if (*buffer != nullptr && decompressed != expected) {
        compressor.Destruct();
        return false;
    }
    if (supplied == nullptr) {
        try {
            *length = decompressed;
            *buffer = RZMemory::Malloc(decompressed);
            expected = decompressed;
        } catch (...) {
            *buffer = nullptr;
        }
    }
    if (*buffer != nullptr) {
        uint32_t capacity = *length;
        if (capacity >= decompressed) {
            if (size == 0 || capacity == 0) {
                ok = true;
            } else if (compressor.DecompressData(source, size, *buffer, expected, length)) {
                ok = true;
            } else {
                *length = 0;
                if (supplied == nullptr && *buffer != nullptr) {
                    RZMemory::Free(*buffer);
                    *buffer = nullptr;
                }
            }
        }
    }
    compressor.Destruct();
    return ok;
}

bool cGZDBSegmentPackedFile::ReadAndDecompress(uint32_t offset, uint32_t storedSize, uint32_t expected, void **buffer, uint32_t *length)
{
    if (offset == 0) return false;
    alignas(8) uint8_t stackBuffer[0xc00];
    void *block = nullptr;
    uint8_t *destination;
    if (storedSize <= 0xc00) {
        destination = stackBuffer + ((0xc00u - storedSize) & 0xfffffff8u);
    } else {
        block = RZMemory::Malloc(storedSize);
        destination = static_cast<uint8_t *>(block);
    }
    bool ok = false;
    // Slot 56 (ReadAt): destination, offset, length.
    if (As<ifaces::cIGZDBSegmentPackedFile>(this)->ReadAt(destination, offset, storedSize)) {
        ok = PackedFileCompression::Decompress(destination, storedSize, expected, buffer, length);
    }
    if (block != nullptr) RZMemory::Free(block);
    return ok;
}
