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

// ---- Reading and writing a record's bytes (the compressing slots) ---------------------------------------

uint32_t SegmentView::DoReadRecord(const uint32_t *key, void *destination, uint32_t *size)
{
    cGZDBSegmentPackedFile *file = Primary(this);
    file->EnsureLoaded();
    uint8_t state = file->mOpenMode;
    if ((state & 3) == 0 || (state & 1) == 0) {
        *size = 0;
        return 0xffffffffu;
    }
    if ((state & 2) != 0) {
        KeyTree *tree = &file->mOpenRecords;
        KeyTreeNode *found;
        tree->Find(&found, key);
        if (found != tree->head) {
            // The pending record's own flags: bit 1 means it is still being written. (Not checked for a null record.)
            uint8_t *record = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(found->value));
            if ((As<ifaces::cIGZPersistDBRecord>(record + 4)->GetAccessFlags() & 2) != 0) {
                *size = 0;
                return 0xffffffffu;
            }
        }
    }
    KeyCopy keyCopy(key);
    const uint32_t *hit = file->mIndex->FindRecord(keyCopy.Get(), file->mMatchFlagA, file->mMatchFlagB);
    if (hit == nullptr) {
        *size = 0;
        return 0xffffffffu;
    }
    if (destination == nullptr) {
        *size = 0;
        return hit[2] & 0x7fffffffu;
    }
    uint32_t full = hit[2] & 0x7fffffffu;
    if (full > hit[1]) {
        // Stored compressed: the record is looked up once more and decompressed into the caller's buffer.
        KeyCopy secondCopy(key);
        const uint32_t *again = file->mIndex->FindRecord(secondCopy.Get(), file->mMatchFlagA, file->mMatchFlagB);
        if (again == nullptr || (again[2] & 0x7fffffffu) <= again[1]) {
            *size = 0;
            return 0;
        }
        void *buffer = destination;
        if (!file->ReadAndDecompress(again[0], again[1], again[2] & 0x7fffffffu, &buffer, size)) {
            *size = 0;
            return 0;
        }
        uint32_t length = *size;
        *size = length;
        return length;
    }
    uint32_t count = *size;
    if (hit[1] < count) count = hit[1];
    auto *fileObject = As<ifaces::cIGZFile>(&file->mFile);
    uint32_t landed = fileObject->SeekToPosition(file->mBaseOffset + hit[0]);
    if (landed != file->mBaseOffset + hit[0]) {
        *size = 0;
        return 0;
    }
    bool read = fileObject->Read(destination, count) != 0;
    uint32_t got = read ? count : 0;
    *size = got;
    return got;
}

bool SegmentView::DoWriteRecord(const uint32_t *key, const void *data, uint32_t size)
{
    cGZDBSegmentPackedFile *file = Primary(this);
    file->EnsureLoaded();
    uint8_t state = file->mOpenMode;
    if ((state & 3) == 0 || (state & 2) == 0) return false;
    KeyTree *tree = &file->mOpenRecords;
    KeyTreeNode *found;
    tree->Find(&found, key);
    if (found != tree->head) return false;
    uint32_t keyCopy[4];
    std::memcpy(keyCopy, key, sizeof(keyCopy));
    file->EnsureStore();
    uint32_t removed[3];
    if (file->mIndex->RemoveRecord(AsKey(keyCopy), removed, file->mMatchFlagA, file->mMatchFlagB)) {
        file->AdjustRefs(key[0], key[1], false);
        file->AddFreeRangeTo(removed[0], removed[1], (removed[2] >> 31) ^ 1u);
        file->mIndexDirty = 1;
    }
    uint32_t storedSize = size;
    const void *toWrite = data;
    void *compressed = nullptr;
    uint32_t compressedSize = 0;
    bool wasCompressed = false;
    if (file->PassesFilter(key, size) && PackedFileCompression::TryCompress(data, size, &compressed, &compressedSize)) {
        wasCompressed = true;
        toWrite = compressed;
        storedSize = compressedSize;
    }
    bool stored = false;
    uint32_t offset = 0;
    auto *packed = As<ifaces::cIGZDBSegmentPackedFile>(file);
    if (packed->AllocateSpace(&offset, storedSize)) {
        uint32_t record[3] = {offset, storedSize, size & 0x7fffffffu};
        if (packed->WriteAt(const_cast<void *>(toWrite), offset, storedSize) != 0) {
            file->mIndex->AddRecord(AsKey(keyCopy), record);
            file->AdjustRefs(key[0], key[1], true);
            file->mIndexDirty = 1;
            stored = true;
        } else if (offset != 0 && storedSize != 0) {
            RangeTree::AddRange(&file->mFreeRanges, offset, storedSize);
            file->mFreeRangesDirty = 1;
        }
    }
    if (wasCompressed && compressed != nullptr) RZMemory::Free(compressed);
    return stored;
}

// ---- Creating a record (sub slot 38) and the pending tree's insert ----------------------------------------

namespace {

// The pending-record tree as a multimap: 0x9a6980 inserts after equal keys without looking for them.
using PendingMulti = std::multimap<StoreKey, void *, PendingOrder, GameAllocator<std::pair<const StoreKey, void *>>>;
static_assert(sizeof(PendingMulti) == 8, "PendingMulti: head pointer and size");

} // namespace

void **KeyTree::Insert(void **out, const uint8_t *pair)
{
    StoreKey key;
    std::memcpy(&key, pair, sizeof(key));
    void *value;
    std::memcpy(&value, pair + 0x10, sizeof(value));
    auto it = reinterpret_cast<PendingMulti *>(this)->insert(std::pair<const StoreKey, void *>(key, value));
    void *node;
    std::memcpy(&node, &it, sizeof(node));
    *out = node;
    return out;
}

bool SegmentView::DoCreateNewRecord(const uint32_t *key, void **out)
{
    cGZDBSegmentPackedFile *file = Primary(this);
    file->EnsureLoaded();
    uint8_t state = file->mOpenMode;
    if ((state & 3) == 0 || (state & 2) == 0) return false;
    KeyCopy keyCopy(key);
    if (file->mIndex->FindRecord(keyCopy.Get(), file->mMatchFlagA, file->mMatchFlagB) != nullptr) return false;
    KeyTree *tree = &file->mOpenRecords;
    KeyTreeNode *found;
    tree->Find(&found, key);
    if (found != tree->head) return false;
    // The new record object (0x64 bytes): its constructor (cGZDBWriteRecordPackedFile::Construct, which can spill a big record
    // to a temporary file). Arguments: two zeros, the key, the file and 1.
    void *memory = RZMemory::Malloc(0x64);
    void *object = memory != nullptr
        ? static_cast<cGZDBWriteRecordPackedFile *>(memory)->Construct(0, 0, AsKey(key), file, 1) : nullptr;
    As<ifaces::cIGZPersistDBSerialRecord>(object)->AddRef();
    *out = object != nullptr ? static_cast<uint8_t *>(object) + 4 : nullptr;
    uint8_t pair[0x14];
    std::memcpy(pair, key, 16);
    std::memcpy(pair + 0x10, &object, sizeof(object));
    As<ifaces::cIGZPersistDBSerialRecord>(object)->AddRef();
    void *scratch;
    tree->Insert(&scratch, pair);
    return true;
}

// ---- Opening records (sub slot 37 and slot 53) ------------------------------------------------------------

namespace {

// The read-record objects are ours (cGZDBReadRecord.h); the write record is cGZDBWriteRecordPackedFile (0x64 bytes).
// The read records' arguments: the data (a counted block or a file offset), its size, the key, the file and 1.
// The two read-record objects (cGZDBReadRecord.h): 0x58 bytes backed by a counted block, 0x1060 bytes reading the file on demand.
void *NewMemoryRecord(void *block, uint32_t size, const uint32_t *key, void *file)
{
    void *memory = RZMemory::Malloc(0x58);
    if (memory == nullptr) return nullptr;
    return (new (memory) cGZDBReadRecordRAM)->Construct(block, size, reinterpret_cast<const cGZPersistResourceKey *>(key), file, 1);
}

void *NewFileRecord(uint32_t offset, uint32_t size, const uint32_t *key, void *file)
{
    void *memory = RZMemory::Malloc(0x1060);
    if (memory == nullptr) return nullptr;
    return (new (memory) cGZDBReadRecordPackedFile)->Construct(offset, size, reinterpret_cast<const cGZPersistResourceKey *>(key), file, 1);
}

// A counted block from the global allocator: the count in the first dword (starts at 1 here), the bytes after it.
struct CountedBlock {
    uint32_t *header = nullptr;
    uint8_t *Data() const { return reinterpret_cast<uint8_t *>(header) + 4; }
};

// 0x9a74d0: drops one reference of the counted block; at zero the block goes back to the global allocator.
void ReleaseCountedBlock(uint32_t *&header)
{
    if (header == nullptr) return;
    if (RZAtomicAdd(reinterpret_cast<int32_t *>(header), -1) == 0) RZMemory::Free(header);
}

// The sub-object pointer of a freshly made record object: the object plus 4, or null.
void *HandleOf(void *object)
{
    return object != nullptr ? static_cast<uint8_t *>(object) + 4 : nullptr;
}

// A record object that keeps its bytes in memory (cGZDBWriteRecordPackedFile, 0x64 bytes): the serial record's 0x44
// bytes and then the kind (3 = memory), a flag byte, the byte vector, a position and a sink (both zero here) and a byte.
// The object is made by Construct on raw memory and the rest is stored by OpenRecordForWrite, as the original's
// derived constructor does; no C++ constructor runs on it.
struct MemoryBackedRecord : cGZPersistDBSerialRecord {
    uint32_t kind;             // +0x44
    uint8_t byte48;            // +0x48
    std::vector<uint8_t> bytes; // +0x4c
    uint32_t position;         // +0x58
    void *sink;                // +0x5c
    uint8_t byte60;            // +0x60
};

static_assert(offsetof(MemoryBackedRecord, kind) == 0x44, "MemoryBackedRecord: kind at +0x44");
static_assert(offsetof(MemoryBackedRecord, byte48) == 0x48, "MemoryBackedRecord: byte at +0x48");
static_assert(offsetof(MemoryBackedRecord, bytes) == 0x4c, "MemoryBackedRecord: bytes at +0x4c");
static_assert(offsetof(MemoryBackedRecord, byte60) == 0x60, "MemoryBackedRecord: byte at +0x60");
static_assert(sizeof(MemoryBackedRecord) == 0x64, "MemoryBackedRecord: 0x64 bytes");

} // namespace

bool SegmentView::DoOpenRecord(const uint32_t *key, void **out, uint32_t mode)
{
    cGZDBSegmentPackedFile *file = Primary(this);
    void *handle = nullptr; // edi
    file->EnsureLoaded();
    if ((file->mOpenMode & 3) != 0) {
        if ((mode & 2) != 0) return As<ifaces::cIGZDBSegmentPackedFile>(file)->OpenRecordForWrite(AsKey(key), out);
        if ((mode & 1) != 0) {
            KeyTree *tree = &file->mOpenRecords;
            KeyTreeNode *found;
            tree->Find(&found, key);
            if (found != tree->head) {
                // Already open for writing or reading: a second object sharing the same data.
                uint8_t *record = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(found->value));
                if ((As<ifaces::cIGZPersistDBRecord>(record + 4)->GetAccessFlags() & 2) == 0) {
                    uint32_t kind;
                    std::memcpy(&kind, record + 0x44, sizeof(kind));
                    if (kind == 2 || kind == 1) {
                        uint32_t data, size;
                        std::memcpy(&data, record + 0x4c, sizeof(data));
                        std::memcpy(&size, record + 0x54, sizeof(size));
                        void *object = nullptr;
                        if (kind == 2) {
                            object = NewMemoryRecord(reinterpret_cast<void *>(static_cast<uintptr_t>(data)), size, key, file);
                        } else {
                            object = NewFileRecord(data, size, key, file);
                        }
                        handle = HandleOf(object);
                        As<ifaces::cIGZPersistDBRecord>(handle)->AddRef();
                    }
                }
            } else {
                KeyCopy keyCopy(key);
                const uint32_t *hit = file->mIndex->FindRecord(keyCopy.Get(), file->mMatchFlagA, file->mMatchFlagB);
                if (hit != nullptr) {
                    uint32_t full = hit[2] & 0x7fffffffu;
                    if (full > hit[1]) {
                        // Stored compressed: decompressed into a counted block that the new object then shares.
                        CountedBlock block;
                        block.header = static_cast<uint32_t *>(RZMemory::Malloc(full + 4));
                        if (block.header != nullptr) {
                            block.header[0] = 0;
                            RZAtomicAdd(reinterpret_cast<int32_t *>(block.header), 1);
                        }
                        void *dataPointer = block.Data();
                        uint32_t sizeValue = full;
                        if (file->ReadAndDecompress(hit[0], hit[1], full, &dataPointer, &sizeValue)) {
                            void *object = NewMemoryRecord(block.header, sizeValue, key, file);
                            handle = HandleOf(object);
                            As<ifaces::cIGZPersistDBRecord>(handle)->AddRef();
                        }
                        ReleaseCountedBlock(block.header);
                    } else if (hit[1] <= file->mSmallRecordLimit) {
                        // A small record: read into a counted block.
                        CountedBlock block;
                        block.header = static_cast<uint32_t *>(RZMemory::Malloc(full + 4));
                        if (block.header != nullptr) {
                            block.header[0] = 0;
                            RZAtomicAdd(reinterpret_cast<int32_t *>(block.header), 1);
                        }
                        if (As<ifaces::cIGZDBSegmentPackedFile>(file)->ReadAt(block.Data(), hit[0], hit[1])) {
                            void *object = NewMemoryRecord(block.header, hit[1], key, file);
                            handle = HandleOf(object);
                            As<ifaces::cIGZPersistDBRecord>(handle)->AddRef();
                        }
                        ReleaseCountedBlock(block.header);
                    } else {
                        // A big record stays in the file: an object that reads it on demand.
                        void *object = NewFileRecord(hit[0], hit[1], key, file);
                        handle = HandleOf(object);
                        As<ifaces::cIGZPersistDBRecord>(handle)->AddRef();
                    }
                }
            }
            if (handle != nullptr) {
                void *object = static_cast<uint8_t *>(handle) - 4;
                uint8_t pair[0x14];
                std::memcpy(pair, key, 16);
                As<ifaces::cIGZPersistDBSerialRecord>(object)->AddRef();
                std::memcpy(pair + 0x10, &object, sizeof(object));
                void *scratch;
                file->mOpenRecords.Insert(&scratch, pair);
            }
        }
    }
    *out = handle;
    return handle != nullptr;
}

bool cGZDBSegmentPackedFile::OpenRecordForWrite(const uint32_t *key, void **out)
{
    EnsureLoaded();
    if ((mOpenMode & 3) == 0 || (mOpenMode & 2) == 0) return false;
    KeyTree *tree = &mOpenRecords;
    KeyTreeNode *found;
    tree->Find(&found, key);
    if (found != tree->head) return false;
    KeyCopy keyCopy(key);
    const uint32_t *hit = mIndex->FindRecord(keyCopy.Get(), mMatchFlagA, mMatchFlagB);
    if (hit == nullptr) {
        // No such record yet: a new one.
        return As<ifaces::cIGZPersistDBSegment>(&mSegment)->DoCreateNewRecord(AsKey(key), out);
    }
    uint32_t full = hit[2] & 0x7fffffffu;
    uint8_t *object;
    if (full > hit[1]) {
        // Stored compressed: decompressed into a block and copied into a memory-backed record object.
        void *buffer = nullptr;
        uint32_t length = 0;
        if (!ReadAndDecompress(hit[0], hit[1], full, &buffer, &length)) return false;
        object = static_cast<uint8_t *>(RZMemory::Malloc(0x64));
        if (object != nullptr) {
            // The serial record's constructor (the key, the segment as the child, 1), then the memory-backed tail.
            auto *record = reinterpret_cast<MemoryBackedRecord *>(object);
            record->Construct(reinterpret_cast<const cGZPersistResourceKey *>(key), &mSegment, 1);
            record->kind = 3;
            record->byte48 = 0;
            record->mVTable = cGZDBWriteRecordPackedFile::VTable();
            record->mRecordVTable = cGZDBWriteRecordPackedFile::RecordVTable();
            record->mOStream.mVTable = cGZDBWriteRecordPackedFile::OStreamVTable();
            record->mIStream.mVTable = cGZDBWriteRecordPackedFile::IStreamVTable();
            std::memset(&record->bytes, 0, 0x14); // the vector, the position and the sink
            record->byte60 = 0;
            *reinterpret_cast<uint8_t *>(&record->mRecordFlags) = 3; // the low byte only
            auto *bytes = new (&record->bytes) std::vector<uint8_t>();
            if (length != 0) bytes->resize(length);
            std::memcpy(bytes->data(), buffer, length);
        }
        As<ifaces::cIGZPersistDBSerialRecord>(object)->AddRef();
        *out = object != nullptr ? object + 4 : nullptr;
        if (buffer != nullptr) RZMemory::Free(buffer);
    } else {
        void *memory = RZMemory::Malloc(0x64);
        object = static_cast<uint8_t *>(memory != nullptr
            ? static_cast<void *>(static_cast<cGZDBWriteRecordPackedFile *>(memory)->Construct(hit[0], hit[1], AsKey(key), this, 1))
            : nullptr);
        As<ifaces::cIGZPersistDBSerialRecord>(object)->AddRef();
        *out = object != nullptr ? object + 4 : nullptr;
    }
    uint8_t pair[0x14];
    std::memcpy(pair, key, 16);
    std::memcpy(pair + 0x10, &object, sizeof(object));
    As<ifaces::cIGZPersistDBSerialRecord>(object)->AddRef();
    void *scratch;
    tree->Insert(&scratch, pair);
    return true;
}

// ---- Exporting a record to a file (slot 54) and importing a file as a record (slot 21) -------------------------

namespace {

// Deletes the file at the path (PathDelete, 0x1c2e25).
void RemovePath(const cIGZString *path)
{
    PathDelete(path);
}

} // namespace

uint8_t cGZDBSegmentPackedFile::ExportRecord(const uint32_t *key, cIGZString *path)
{
    EnsureLoaded();
    auto *sub = As<ifaces::cIGZPersistDBSegment>(&mSegment);
    uint32_t remaining = sub->DoGetRecordSize(AsKey(key)); // the record's size
    uint8_t ok = 0;
    if (remaining == 0xffffffffu) return ok;
    // The directory of the target is made when it is missing.
    cRZString directory;
    directory.Construct();
    appbase::GetModuleDirectory(path, directory.AsInterface());
    if (!inifile::IsCanonicalAbsolutePathString(&directory)) inifile::AbsolutePathString(&directory, &directory);
    if (!as2::DirectoryExists(&directory)) appbase::CreateDirectories(&directory);
    cRZFile file;
    file.Construct(*path);
    void *handle = nullptr;
    bool opened = file.Open(2, 3, 1);
    if (opened) opened = sub->OpenRecord(AsKey(key), &handle, 1);
    if (opened) {
        void *stream = nullptr;
        if (As<ifaces::cIGZPersistDBRecord>(handle)->QueryInterface(0xc56b8f08u, &stream)) {
            ok = 1;
            file.SetLength(0);
            file.SeekToPosition(0);
            alignas(8) uint8_t buffer[0x400];
            while (remaining != 0) {
                uint32_t chunk = remaining < 0x400 ? remaining : 0x400;
                // The next `chunk` bytes of the record into the buffer.
                ok = As<ifaces::cIGZPersistDBSerialRecord>(stream)->GetFieldVoid(buffer, chunk);
                if (ok != 0) ok = file.Write(buffer, chunk) ? 1 : 0;
                remaining -= chunk;
                if (ok == 0) break;
            }
            file.Close();
            sub->CloseRecord(As<ifaces::cIGZPersistDBRecord>(handle));
            As<ifaces::cIGZPersistDBSerialRecord>(stream)->Release();
        }
        As<ifaces::cIGZPersistDBRecord>(handle)->Release();
    }
    if (ok == 0) RemovePath(path);
    file.Destruct();
    directory.Destruct();
    return ok;
}

uint8_t cGZDBSegmentPackedFile::ImportFile(const uint32_t *key, cIGZString *path)
{
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Lock();
    uint8_t result = 0;
    EnsureLoaded();
    if ((mOpenMode & 3) != 0 && (mOpenMode & 2) != 0) {
        KeyTree *tree = &mOpenRecords;
        KeyTreeNode *found;
        tree->Find(&found, key);
        if (found == tree->head) {
            uint32_t keyCopy[4];
            std::memcpy(keyCopy, key, sizeof(keyCopy));
            EnsureStore();
            uint32_t removed[3];
            if (mIndex->RemoveRecord(AsKey(keyCopy), removed, mMatchFlagA, mMatchFlagB)) {
                AdjustRefs(key[0], key[1], false);
                AddFreeRangeTo(removed[0], removed[1], (removed[2] >> 31) ^ 1u);
                mIndexDirty = 1;
            }
            auto *self = As<ifaces::cIGZDBSegmentPackedFile>(this);
            cRZFile file;
            file.Construct(*path);
            if (file.Open(1, 2, 1)) {
                uint32_t length = file.Length();
                if (length != 0) {
                    if (PassesFilter(key, length)) {
                        // The whole file is read and stored compressed when that pays off.
                        uint8_t *whole = static_cast<uint8_t *>(RZMemory::Malloc(length + 2));
                        void *compressed = nullptr;
                        uint32_t compressedSize = 0;
                        whole[length] = 0;
                        whole[length + 1] = 0;
                        if (file.Read(whole, length) && PackedFileCompression::TryCompress(whole, length, &compressed, &compressedSize)) {
                            uint32_t offset = 0;
                            if (self->AllocateSpace(&offset, compressedSize)) {
                                if (self->WriteAt(compressed, offset, compressedSize) != 0) {
                                    uint32_t record[3] = {offset, compressedSize, length & 0x7fffffffu};
                                    AdjustRefs(key[0], key[1], true);
                                    mIndex->AddRecord(AsKey(keyCopy), record);
                                    mIndexDirty = 1;
                                    result = 1;
                                } else if (offset != 0 && compressedSize != 0) {
                                    RangeTree::AddRange(&mFreeRanges, offset, compressedSize);
                                    mFreeRangesDirty = 1;
                                }
                            }
                            if (compressed != nullptr) RZMemory::Free(compressed);
                        }
                        RZMemory::Free(whole);
                    }
                    if (result == 0) {
                        // Stored as it is, in chunks of 0x4000 bytes.
                        uint32_t offset = 0;
                        if (self->AllocateSpace(&offset, length)) {
                            uint8_t *chunk = static_cast<uint8_t *>(RZMemory::Malloc(0x4000));
                            file.SeekToPosition(0);
                            uint8_t ok = 1;
                            uint32_t done = 0, at = offset;
                            while (ok != 0) {
                                uint32_t count = done + 0x4000 <= length ? 0x4000 : length - done;
                                ok = file.Read(chunk, count) ? 1 : 0;
                                if (ok != 0) {
                                    ok = self->WriteAt(chunk, at, count);
                                    done += count;
                                    at += count;
                                }
                                if (done >= length) break;
                            }
                            if (ok != 0) {
                                uint32_t record[3] = {offset, length, length & 0x7fffffffu};
                                mIndex->AddRecord(AsKey(keyCopy), record);
                                AdjustRefs(key[0], key[1], true);
                                mIndexDirty = 1;
                                result = ok;
                            } else if (offset != 0) {
                                RangeTree::AddRange(&mFreeRanges, offset, length);
                                mFreeRangesDirty = 1;
                            }
                            if (chunk != nullptr) RZMemory::Free(chunk);
                        }
                    }
                }
                file.Close();
            }
            file.Destruct();
        }
    }
    As<ifaces::cIGZCriticalSection>(&mSegment.mLock)->Unlock();
    return result;
}
