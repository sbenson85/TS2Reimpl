#include "gz/cGZDBWriteRecordPackedFile.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/GeneralAllocator.h"
#include "rz/PathFileOps.h"
#include "rz/RZMemory.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>

namespace {

using U32Fn = uint32_t(__thiscall *)(void *);
using U32U32Fn = uint32_t(__thiscall *)(void *, uint32_t);
using BoolFn = bool(__thiscall *)(void *);
using BoolU32Fn = bool(__thiscall *)(void *, uint32_t);
using BoolVoidU32Fn = bool(__thiscall *)(void *, const void *, uint32_t);
using BoolU32U32U32Fn = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using BoolU32PtrFn = bool(__thiscall *)(void *, uint32_t, void **);
using BoolPtrU32U32Fn = bool(__thiscall *)(void *, void *, uint32_t, uint32_t);
using BoolConstPtrU32U32Fn = bool(__thiscall *)(void *, const void *, uint32_t, uint32_t);
using VoidU32Fn = void(__thiscall *)(void *, uint32_t);
using VoidPtrFn = void(__thiscall *)(void *, void *);
using PtrFn = void *(__thiscall *)(void *);
using PathFn = const cIGZString *(__thiscall *)(void *);

// The +4 face of the record, the `this` of its slots.
void *FaceOf(cGZDBWriteRecordPackedFile *record) { return reinterpret_cast<uint8_t *>(record) + 4; }

// GetAccessFlags (+4 slot 5): nonzero while the record is open.
uint32_t Access(cGZDBWriteRecordPackedFile *record)
{
    return VirtualCall<U32Fn, cGZDBWriteRecordPackedFile::kAccessFlagsSlot>(FaceOf(record));
}

// The segment behind a +4 face (QueryChild or QueryInterface hand out the face; the segment's primary is 4 below it).
void *SegmentOf(void *face) { return face != nullptr ? static_cast<uint8_t *>(face) - 4 : nullptr; }

void ReleaseFace(void *face)
{
    if (face != nullptr) VirtualCall<U32Fn, 2>(face);
}

} // namespace

// ---- Construction and destruction -----------------------------------------------------------------------------------------

cGZDBWriteRecordPackedFile *cGZDBWriteRecordPackedFile::Construct(uint32_t offset, uint32_t size, const cGZPersistResourceKey *key,
                                                                  void *file, uint32_t flag)
{
    cGZPersistDBSerialRecord::Construct(key, file != nullptr ? static_cast<uint8_t *>(file) + 4 : nullptr, flag);
    mKind = kKind;
    new (&mBuffer) std::vector<char>();
    mPosition = 0;
    mSink = nullptr;
    mTriedFile = 0;
    mError = 0;
    mVTable = VTable();
    mRecordVTable = RecordVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x20) = OStreamVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x30) = IStreamVTable();
    *reinterpret_cast<uint8_t *>(&mRecordFlags) = 3;
    mBuffer.reserve(kInitialCapacity);
    if (size == 0) return this;

    // A big record goes to a temporary file, copied over in chunks.
    if (size > kMemoryLimit && mTriedFile == 0) {
        mTriedFile = 1;
        bool done = false;
        if (CreateSink(mKey[0]) && mChild != nullptr) {
            void *face = nullptr;
            if (VirtualCall<BoolU32PtrFn, 0>(mChild, kIdSegment, &face)) {
                void *segment = SegmentOf(face);
                void *buffer = RZMemory::GlobalAllocator()->Malloc(kCopyChunk, 0);
                VirtualCall<U32U32Fn, kSinkSeekSlot>(mSink, 0);
                bool ok = true;
                uint32_t cursor = offset; // the fall-back read below starts from the caller's offset again
                while (cursor < size && ok) {
                    uint32_t chunk = cursor + kCopyChunk <= size ? kCopyChunk : size - cursor;
                    ok = VirtualCall<BoolPtrU32U32Fn, kSegmentReadAtSlot>(segment, buffer, cursor, chunk);
                    if (ok) {
                        ok = VirtualCall<BoolVoidU32Fn, kSinkWriteSlot>(mSink, buffer, chunk);
                        cursor += chunk;
                    }
                }
                if (ok) VirtualCall<U32U32Fn, kSinkSeekSlot>(mSink, mPosition);
                if (buffer != nullptr) RZMemory::GlobalAllocator()->Free(buffer);
                ReleaseFace(face);
                done = ok;
            }
        }
        if (!done) DropSink();
    }

    if (mSink == nullptr && mChild != nullptr) {
        void *face = nullptr;
        if (VirtualCall<BoolU32PtrFn, 0>(mChild, kIdSegment, &face)) {
            void *segment = SegmentOf(face);
            mBuffer.resize(size);
            VirtualCall<BoolPtrU32U32Fn, kSegmentReadAtSlot>(segment, mBuffer.data(), offset, size);
            ReleaseFace(face);
        }
    }
    return this;
}

cGZDBWriteRecordPackedFile *cGZDBWriteRecordPackedFile::DeletingDestructor(uint32_t flags)
{
    mVTable = VTable();
    mRecordVTable = RecordVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x20) = OStreamVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x30) = IStreamVTable();
    DropSink();
    mBuffer.~vector();
    DestructMembers();
    if ((flags & 1) != 0 && (flags & 4) == 0) RZMemory::Free(this);
    return this;
}

// ---- The temporary file ---------------------------------------------------------------------------------------------------

bool cGZDBWriteRecordPackedFile::CreateSink(uint32_t keyType)
{
    cRZString name;
    name.Construct();
    name.Sprintf("Record_%08x_", keyType);
    bool opened = false;
    if (MakeTempFile(name.AsInterface(), nullptr)) {
        auto *file = static_cast<cRZFile *>(::operator new(sizeof(cRZFile)));
        file->Construct(*name.AsInterface());
        mSink = file;
        opened = VirtualCall<BoolU32U32U32Fn, kSinkOpenSlot>(mSink, 3, 3, 1);
    }
    name.Destruct();
    return opened;
}

void cGZDBWriteRecordPackedFile::DropSink()
{
    if (mSink == nullptr) return;
    if (VirtualCall<BoolFn, kSinkIsOpenSlot>(mSink)) {
        VirtualCall<BoolFn, kSinkCloseSlot>(mSink);
        PathDelete(VirtualCall<PathFn, kSinkGetPathSlot>(mSink));
    }
    VirtualCall<VoidU32Fn, kSinkDeleteSlot>(mSink, 1);
    mSink = nullptr;
}

// ---- Slots ----------------------------------------------------------------------------------------------------------------

bool cGZDBWriteRecordPackedFile::GetFieldVoid(void *out, uint32_t size)
{
    if (Access(this) == 0) return false;
    if (mSink != nullptr) return VirtualCall<BoolVoidU32Fn, kSinkReadSlot>(mSink, out, size);
    // The original computes position + size and end - start in 32 bits: the sum wraps, and the comparison is
    // unsigned (lea, cmp, ja).
    uint32_t available = static_cast<uint32_t>(mBuffer.size());
    if (mPosition + size > available) return false;
    std::memcpy(out, mBuffer.data() + mPosition, size);
    mPosition += size;
    return true;
}

bool cGZDBWriteRecordPackedFile::SetFieldVoid(const void *src, uint32_t n)
{
    if (Access(this) == 0) return false;
    if (mSink != nullptr) return VirtualCall<BoolVoidU32Fn, kSinkWriteSlot>(mSink, src, n);

    bool result = false;
    // A signed comparison here (the constructor's is unsigned).
    if (static_cast<int32_t>(mPosition + n) >= static_cast<int32_t>(kMemoryLimit) && mTriedFile == 0) {
        mTriedFile = 1;
        const uint32_t *key = static_cast<const uint32_t *>(VirtualCall<PtrFn, kGetKeySlot>(FaceOf(this)));
        if (CreateSink(key[0])) {
            VirtualCall<U32U32Fn, kSinkSeekSlot>(mSink, 0);
            VirtualCall<BoolU32Fn, kSinkSetLengthSlot>(mSink, 0);
            if (VirtualCall<BoolVoidU32Fn, kSinkWriteSlot>(mSink, mBuffer.data(), static_cast<uint32_t>(mBuffer.size()))) {
                VirtualCall<BoolVoidU32Fn, kSinkWriteSlot>(mSink, src, n);
                mBuffer.resize(0);
                mPosition = 0;
                result = true;
            }
        }
        if (!result && mSink != nullptr) {
            // Unlike the destructor's, this clean-up deletes the file whether or not the sink reports itself open.
            if (VirtualCall<BoolFn, kSinkIsOpenSlot>(mSink)) VirtualCall<BoolFn, kSinkCloseSlot>(mSink);
            PathDelete(VirtualCall<PathFn, kSinkGetPathSlot>(mSink));
            VirtualCall<VoidU32Fn, kSinkDeleteSlot>(mSink, 1);
            mSink = nullptr;
        }
    }
    if (mSink != nullptr) return result;

    uint32_t size = static_cast<uint32_t>(mBuffer.size());
    if (mPosition == size && n < kSmallWrite) {
        const char *bytes = static_cast<const char *>(src);
        for (uint32_t i = 0; i < n; ++i) mBuffer.push_back(bytes[i]);
        mPosition += n;
        return true;
    }
    if (mPosition + n > size) mBuffer.resize(mPosition + n);
    std::memcpy(mBuffer.data() + mPosition, src, n);
    mPosition += n;
    return true;
}

uint32_t cGZDBWriteRecordPackedFile::GetSize()
{
    if (Access(this) == 0) return 0;
    if (mSink != nullptr) return VirtualCall<U32Fn, kSinkSizeSlot>(mSink);
    return static_cast<uint32_t>(mBuffer.size());
}

bool cGZDBWriteRecordPackedFile::SetSize(uint32_t size)
{
    if (Access(this) == 0) return false;
    if (mSink != nullptr) return VirtualCall<BoolU32Fn, kSinkSetLengthSlot>(mSink, size);
    mBuffer.resize(size);
    return true;
}

uint32_t cGZDBWriteRecordPackedFile::GetPosition()
{
    if (Access(this) == 0) return 0;
    if (mSink != nullptr) return VirtualCall<U32Fn, kSinkPositionSlot>(mSink);
    return mPosition;
}

bool cGZDBWriteRecordPackedFile::SeekAbsolute(uint32_t position)
{
    if (Access(this) == 0) return false;
    if (mSink != nullptr) {
        // The sink answers with the position it now holds; the original compares the whole eax with the argument.
        return VirtualCall<U32U32Fn, kSinkSeekSlot>(mSink, position) == position;
    }
    mPosition = position;
    return true;
}

bool cGZDBWriteRecordPackedFile::SeekRelative(int32_t offset)
{
    // The original takes the position through this's vtable (slot 54) and sums in 64 bits: a negative sum fails;
    // otherwise the low 32 bits go to this's SeekAbsolute (slot 55) through the vtable.
    int64_t target = static_cast<int64_t>(VirtualCall<U32Fn, kGetPositionSlot>(this)) + offset;
    if (target < 0) return false;
    return VirtualCall<BoolU32Fn, kSeekAbsoluteSlot>(this, static_cast<uint32_t>(target));
}

void cGZDBWriteRecordPackedFile::Close()
{
    void *face = FaceOf(this);
    VirtualCall<U32U32Fn, kSetAccessFlagsSlot>(face, 0);
    VirtualCall<VoidPtrFn, kSetChildSlot>(face, nullptr);
    DropSink();
    mBuffer.clear();
    mTriedFile = 0;
    mPosition = 0;
}

bool cGZDBWriteRecordPackedFile::FlushToSegment(uint32_t offset, uint32_t size)
{
    if (Access(this) == 0) return false;
    void *face = nullptr;
    if (!VirtualCall<BoolU32PtrFn, kQueryChildSlot>(FaceOf(this), kIdSegment, &face)) return false;
    void *segment = SegmentOf(face);
    bool ok;
    if (mSink != nullptr) {
        uint32_t saved = VirtualCall<U32Fn, kSinkPositionSlot>(mSink);
        void *buffer = RZMemory::GlobalAllocator()->Malloc(kCopyChunk, 0);
        VirtualCall<U32U32Fn, kSinkSeekSlot>(mSink, 0);
        ok = true;
        uint32_t done = 0;
        while (done < size && ok) {
            uint32_t chunk = done + kCopyChunk <= size ? kCopyChunk : size - done;
            ok = VirtualCall<BoolVoidU32Fn, kSinkReadSlot>(mSink, buffer, chunk);
            if (ok) {
                ok = VirtualCall<BoolConstPtrU32U32Fn, kSegmentWriteAtSlot>(segment, buffer, offset, chunk);
                done += chunk;
                offset += chunk;
            }
        }
        if (buffer != nullptr) RZMemory::GlobalAllocator()->Free(buffer);
        VirtualCall<U32U32Fn, kSinkSeekSlot>(mSink, saved);
    } else {
        uint32_t count = static_cast<uint32_t>(mBuffer.size());
        if (size < count) count = size;
        ok = VirtualCall<BoolConstPtrU32U32Fn, kSegmentWriteAtSlot>(segment, mBuffer.data(), offset, count);
    }
    ReleaseFace(face);
    return ok;
}
