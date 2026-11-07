#include "gz/cGZDBReadRecord.h"

#include "game/vtable.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZRefCount.h"

#include <cstring>

namespace {

using U32Fn = uint32_t(__thiscall *)(void *);
using BoolPtrU32Fn = bool(__thiscall *)(void *, void *, uint32_t);
using BoolPtrU32U32Fn = bool(__thiscall *)(void *, void *, uint32_t, uint32_t);
using BoolU32PtrFn = bool(__thiscall *)(void *, uint32_t, void **);
using VoidPtrFn = void(__thiscall *)(void *, void *);
using VoidU32Fn = void(__thiscall *)(void *, uint32_t);

// The +4 face (cGZPersistDBRecord) of a record, the `this` of its slots.
void *Face(cGZDBRecordPackedFile *record) { return reinterpret_cast<uint8_t *>(record) + 4; }

// GetAccessFlags (+4 slot 5): nonzero while the record is open.
uint32_t Access(cGZDBRecordPackedFile *record)
{
    return VirtualCall<U32Fn, cGZDBRecordPackedFile::kAccessFlagsSlot>(Face(record));
}

// SetError (primary slot 63) with 1: how a failed read marks the record.
void RaiseError(cGZDBRecordPackedFile *record) { VirtualCall<VoidU32Fn, cGZDBRecordPackedFile::kSetErrorSlot>(record, 1); }

// Drops one reference of a counted block; at zero it goes back to the global allocator (0x1c6c5a, called directly by the
// original, with the allocator at 0x10cb098).
void ReleaseBlock(uint32_t *block)
{
    if (RZAtomicAdd(reinterpret_cast<int32_t *>(block), -1) == 0) RZMemory::GlobalAllocator()->Free(block);
}

// Shared by both constructors: the part up to the variant's own stores.
void ConstructBase(cGZDBRecordPackedFile *record, const cGZPersistResourceKey *key, void *file, uint32_t flag)
{
    void *child = file != nullptr ? static_cast<uint8_t *>(file) + 4 : nullptr;
    record->Construct(key, child, flag);
}

// Shared by both destructors, after the variant's own part: the base tables and member destructors, then the free.
cGZDBRecordPackedFile *Finish(cGZDBRecordPackedFile *record, uint32_t flags)
{
    record->DestructMembers();
    // flags & 1: free the object, unless bit 2 says it was not allocated by the global allocator (then the original calls
    // a sized operator delete that does nothing).
    if ((flags & 1) != 0 && (flags & 4) == 0) RZMemory::Free(record);
    return record;
}

} // namespace

bool cGZDBRecordPackedFile::Seek(uint32_t position)
{
    if (Access(this) == 0 || position >= mSize) return false;
    mPosition = position;
    return true;
}

// ---- File-backed record -----------------------------------------------------------------------------------------------

cGZDBReadRecordPackedFile *cGZDBReadRecordPackedFile::Construct(uint32_t data, uint32_t size, const cGZPersistResourceKey *key, void *file,
                                                                uint32_t flag)
{
    ConstructBase(this, key, file, flag);
    mData = data;
    mSize = size;
    mKind = kKindFile;
    mError = 0;
    mVTable = VTable();
    mRecordVTable = RecordVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x20) = OStreamVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x30) = IStreamVTable();
    mPosition = 0;
    mCacheStart = 0xffffffffu;
    mCacheLength = 0;
    *reinterpret_cast<uint8_t *>(&mRecordFlags) = 1;
    return this;
}

cGZDBReadRecordPackedFile *cGZDBReadRecordPackedFile::DeletingDestructor(uint32_t flags)
{
    return static_cast<cGZDBReadRecordPackedFile *>(Finish(this, flags));
}

bool cGZDBReadRecordPackedFile::GetFieldVoid(void *dst, uint32_t n)
{
    if (Access(this) == 0 || mPosition + n > mSize) {
        RaiseError(this);
        return false;
    }
    // What is cached is served by the buffered read; the rest comes straight from the source.
    uint32_t cached = VirtualCall<U32Fn, kCachedRemainingSlot>(this);
    if (n <= cached + kCacheBytes) return VirtualCall<BoolPtrU32Fn, kReadBufferedSlot>(this, dst, n);
    if (!VirtualCall<BoolPtrU32Fn, kReadBufferedSlot>(this, dst, cached)) return false;
    if (!VirtualCall<BoolPtrU32U32Fn, kReadSourceSlot>(this, static_cast<uint8_t *>(dst) + cached, mData + mPosition, n - cached)) return false;
    mPosition += n - cached;
    return true;
}

bool cGZDBReadRecordPackedFile::SetFieldVoid(const void *, uint32_t)
{
    // Read-only: the access flags are asked (twice, when the record is open) and the answer is no.
    if (Access(this) != 0) Access(this);
    return false;
}

bool cGZDBReadRecordPackedFile::SeekRelative(int32_t offset)
{
    if (Access(this) == 0) return false;
    uint32_t target = mPosition + static_cast<uint32_t>(offset);
    if (target >= mSize) return false;
    mPosition = target;
    return true;
}

void cGZDBReadRecordPackedFile::Close()
{
    VirtualCall<VoidU32Fn, kSetAccessFlagsSlot>(Face(this), 0);
    VirtualCall<VoidPtrFn, kSetChildSlot>(Face(this), nullptr);
    mPosition = 0;
}

bool cGZDBReadRecordPackedFile::ReadSource(void *dst, uint32_t offset, uint32_t n)
{
    void *segment = nullptr; // the +4 face of the segment (the record's child)
    bool ok = false;
    if (VirtualCall<BoolU32PtrFn, kQueryChildSlot>(Face(this), kIdSegment, &segment)) {
        // The segment's primary is 4 below its +4 face; its slot 56 reads `n` bytes at file offset `offset`.
        void *file = segment != nullptr ? static_cast<uint8_t *>(segment) - 4 : nullptr;
        ok = VirtualCall<BoolPtrU32U32Fn, kReadAtSlot>(file, dst, offset, n);
    }
    if (!ok) RaiseError(this);
    if (segment != nullptr) VirtualCall<U32Fn, 2>(segment); // Release
    return ok;
}

bool cGZDBReadRecordPackedFile::ReadBuffered(void *dst, uint32_t n)
{
    auto *out = static_cast<uint8_t *>(dst);
    while (n != 0) {
        uint32_t position = mPosition;
        uint32_t start = mCacheStart;
        if (position < start || position >= start + kCacheBytes) {
            // The position is outside the cache: refill it with the next (up to) 0x1000 bytes of the record.
            uint32_t chunk = mSize - position;
            if (chunk > kCacheBytes) chunk = kCacheBytes;
            if (!VirtualCall<BoolPtrU32U32Fn, kReadSourceSlot>(this, mCache, mData + position, chunk)) {
                mCacheStart = 0xffffffffu;
                RaiseError(this);
                return false;
            }
            mCacheLength = chunk;
            position = mPosition; // reloaded: the source read is a call
            mCacheStart = position;
            start = position;
        }
        uint32_t offset = position - start;
        uint32_t copy = mCacheLength - offset;
        if (n < copy) copy = n;
        std::memcpy(out, mCache + offset, copy);
        mPosition += copy;
        out += copy;
        n -= copy;
    }
    return true;
}

uint32_t cGZDBReadRecordPackedFile::CachedRemaining()
{
    uint32_t length = mCacheLength;
    if (length == 0) return 0;
    uint32_t position = mPosition;
    uint32_t start = mCacheStart;
    if (position < start || position >= start + length) return 0;
    return start - position + length;
}

// ---- Memory-backed record ---------------------------------------------------------------------------------------------

cGZDBReadRecordRAM *cGZDBReadRecordRAM::Construct(void *block, uint32_t size, const cGZPersistResourceKey *key, void *file, uint32_t flag)
{
    ConstructBase(this, key, file, flag);
    mKind = kKindRAM;
    mError = 0;
    mVTable = VTable();
    mRecordVTable = RecordVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x20) = OStreamVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x30) = IStreamVTable();
    mData = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
    mPosition = 0;
    mSize = size;
    RZAtomicAdd(static_cast<int32_t *>(block), 1);
    *reinterpret_cast<uint8_t *>(&mRecordFlags) = 1;
    return this;
}

cGZDBReadRecordRAM *cGZDBReadRecordRAM::DeletingDestructor(uint32_t flags)
{
    auto *block = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(mData));
    if (block != nullptr) ReleaseBlock(block);
    return static_cast<cGZDBReadRecordRAM *>(Finish(this, flags));
}

bool cGZDBReadRecordRAM::GetFieldVoid(void *dst, uint32_t n)
{
    if (Access(this) == 0) return false;
    int32_t end = static_cast<int32_t>(mPosition + n);
    if (end > static_cast<int32_t>(mSize)) return false;
    std::memcpy(dst, reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(mData)) + mPosition + 4, n);
    mPosition = static_cast<uint32_t>(end);
    return true;
}

bool cGZDBReadRecordRAM::SetFieldVoid(const void *src, uint32_t n)
{
    if (Access(this) == 0) return false;
    int32_t end = static_cast<int32_t>(mPosition + n);
    if (end >= static_cast<int32_t>(mSize)) return false;
    std::memcpy(reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(mData)) + mPosition + 4, src, n);
    mPosition = static_cast<uint32_t>(end);
    return true;
}

bool cGZDBReadRecordRAM::SeekRelative(int32_t offset)
{
    if (Access(this) == 0) return false;
    int32_t target = static_cast<int32_t>(mPosition + static_cast<uint32_t>(offset));
    if (target < 0 || target >= static_cast<int32_t>(mSize)) return false;
    mPosition = static_cast<uint32_t>(target);
    return true;
}

void cGZDBReadRecordRAM::Close()
{
    VirtualCall<VoidU32Fn, kSetAccessFlagsSlot>(Face(this), 0);
    VirtualCall<VoidPtrFn, kSetChildSlot>(Face(this), nullptr);
    auto *block = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(mData));
    if (block != nullptr) {
        ReleaseBlock(block);
        mData = 0;
    }
    mPosition = 0;
}
