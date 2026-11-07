#include "gz/cGZDBSerialRecordAsIGZFile.h"
#include "game/vtable.h"
#include "rz/cRZString.h"

#include <cstdlib>
#include <new>

extern "C" __declspec(thread) int _Init_thread_epoch;

namespace {
constexpr int kPersistAccessFlags = 5; // cIGZPersistDBRecord::GetAccessFlags
}

namespace {

using RefFn = uint32_t(__thiscall *)(void *);          // AddRef, Release
using VoidFn = void(__thiscall *)(void *);             // persist Close (no arguments)
using VoidDwordFn = void(__thiscall *)(void *, uint32_t); // deleting destructor
using PersistFn = void *(__thiscall *)(void *);        // AsIGZPersistDBRecord
using GetIntFn = int32_t(__thiscall *)(void *);        // GetSize, GetPosition
using SetSizeFn = bool(__thiscall *)(void *, uint32_t);
using SeekFn = bool(__thiscall *)(void *, int32_t);     // SeekAbsolute, SeekRelative
using GetVoidFn = bool(__thiscall *)(void *, void *, uint32_t);
using SetVoidFn = bool(__thiscall *)(void *, const void *, uint32_t);
using SetRecordFn = bool(__thiscall *)(void *, void *); // primary slot 3
using FileSeekFn = int32_t(__thiscall *)(void *, int32_t);
using FileSeekOriginFn = int32_t(__thiscall *)(void *, int32_t, uint32_t);

} // namespace

cGZDBSerialRecordAsIGZFile &cGZDBSerialRecordAsIGZFile::File::Owner()
{
    return *reinterpret_cast<cGZDBSerialRecordAsIGZFile *>(reinterpret_cast<uint8_t *>(this) - kFileOffset);
}

bool cGZDBSerialRecordAsIGZFile::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid == kGZIID_cIGZFile) {
        *ppvObj = reinterpret_cast<uint8_t *>(this) + kFileOffset;
    } else if (riid == kGZIID_Self) {
        *ppvObj = this;
    } else if (riid == kGZIID_cIGZUnknown) {
        *ppvObj = this;
    } else {
        return false;
    }
    VirtualCall<RefFn, 1>(this); // AddRef through this object's vtable
    return true;
}

uint32_t cGZDBSerialRecordAsIGZFile::Release()
{
    if (mRefCount > 1) return --mRefCount;
    // Last reference: drop the record through this object's vtable, then delete through it.
    VirtualCall<SetRecordFn, kSetRecordSlot>(this, nullptr);
    VirtualCall<VoidDwordFn, kDeleteSlot>(this, 1);
    return 0;
}

bool cGZDBSerialRecordAsIGZFile::SetRecord(void *record)
{
    if (record) VirtualCall<RefFn, kRecAddRef>(record);
    void *old = mRecord;
    if (old) VirtualCall<RefFn, kRecRelease>(old);
    mRecord = record;
    return true;
}

bool cGZDBSerialRecordAsIGZFile::File::Close()
{
    cGZDBSerialRecordAsIGZFile &o = Owner();
    if (!o.mRecord) return false;
    void *persist = VirtualCall<PersistFn, kRecAsPersistRecord>(o.mRecord);
    VirtualCall<VoidFn, kPersistClose>(persist);
    void *record = o.mRecord; // reloaded: the persist Close may have run arbitrary code
    VirtualCall<RefFn, kRecRelease>(record);
    o.mRecord = nullptr;
    return true;
}

int32_t cGZDBSerialRecordAsIGZFile::File::Position()
{
    void *record = Owner().mRecord;
    if (!record) return -1;
    return VirtualCall<GetIntFn, kRecGetPosition>(record);
}

uint32_t cGZDBSerialRecordAsIGZFile::File::Length()
{
    void *record = Owner().mRecord;
    if (!record) return 0;
    return static_cast<uint32_t>(VirtualCall<GetIntFn, kRecGetSize>(record));
}

bool cGZDBSerialRecordAsIGZFile::File::SetLength(uint32_t length)
{
    void *record = Owner().mRecord;
    if (!record) return false;
    return VirtualCall<SetSizeFn, kRecSetSize>(record, length);
}

int32_t cGZDBSerialRecordAsIGZFile::File::SeekToBegin()
{
    void *record = Owner().mRecord;
    if (!record) return -1;
    return VirtualCall<SeekFn, kRecSeekAbsolute>(record, 0) ? 0 : -1;
}

int32_t cGZDBSerialRecordAsIGZFile::File::SeekToEnd()
{
    void *record = Owner().mRecord;
    if (!record) return -1;
    int32_t size = VirtualCall<GetIntFn, kRecGetSize>(record);
    if (size == 0) return -1; // an empty record is not sought at all
    record = Owner().mRecord;
    return VirtualCall<SeekFn, kRecSeekAbsolute>(record, size) ? size : -1;
}

int32_t cGZDBSerialRecordAsIGZFile::File::SeekToRelativePosition(int32_t offset)
{
    void *record = Owner().mRecord;
    if (!record) return -1;
    if (!VirtualCall<SeekFn, kRecSeekRelative>(record, offset)) return -1;
    record = Owner().mRecord;
    return VirtualCall<GetIntFn, kRecGetPosition>(record);
}

int32_t cGZDBSerialRecordAsIGZFile::File::SeekToPosition(int32_t position)
{
    void *record = Owner().mRecord;
    if (!record) return -1;
    return VirtualCall<SeekFn, kRecSeekAbsolute>(record, position) ? position : -1;
}

int32_t cGZDBSerialRecordAsIGZFile::File::Seek(int32_t offset, uint32_t origin)
{
    void *record = Owner().mRecord;
    if (!record) return -1;
    switch (origin) {
    case 0:
        return VirtualCall<FileSeekFn, 12>(this, offset); // SeekToPosition through this vtable
    case 1:
        return VirtualCall<FileSeekFn, 11>(this, offset); // SeekToRelativePosition
    case 2: {
        int32_t size = VirtualCall<GetIntFn, kRecGetSize>(record);
        if (size == 0) return -1;
        uint32_t target = static_cast<uint32_t>(size) + static_cast<uint32_t>(offset);
        return VirtualCall<FileSeekFn, 12>(this, static_cast<int32_t>(target));
    }
    default:
        return -1;
    }
}

bool cGZDBSerialRecordAsIGZFile::File::Read(void *dst, uint32_t length)
{
    void *record = Owner().mRecord;
    if (!record) return false;
    return VirtualCall<GetVoidFn, kRecGetFieldVoid>(record, dst, length);
}

bool cGZDBSerialRecordAsIGZFile::File::ReadWithCount(void *dst, uint32_t &length)
{
    void *record = Owner().mRecord;
    if (!record) return false;
    uint32_t position = static_cast<uint32_t>(VirtualCall<GetIntFn, kRecGetPosition>(record));
    record = Owner().mRecord;
    uint32_t size = static_cast<uint32_t>(VirtualCall<GetIntFn, kRecGetSize>(record));
    if (length + position > size) length = size - position; // clamps even when the read later fails
    record = Owner().mRecord;
    return VirtualCall<GetVoidFn, kRecGetFieldVoid>(record, dst, length);
}

bool cGZDBSerialRecordAsIGZFile::File::Write(const void *src, uint32_t length)
{
    void *record = Owner().mRecord;
    if (!record) return false;
    return VirtualCall<SetVoidFn, kRecSetFieldVoid>(record, src, length);
}

cGZDBSerialRecordAsIGZFile *cGZDBSerialRecordAsIGZFile::DeletingDestructor(uint32_t flags)
{
    mVTable = const_cast<void *>(VTable());
    mFileVTable = const_cast<void *>(FileVTable());
    if (mRecord) {
        void *persist = VirtualCall<PersistFn, kRecAsPersistRecord>(mRecord);
        VirtualCall<VoidFn, kPersistClose>(persist);
        VirtualCall<RefFn, kRecRelease>(mRecord);
        mRecord = nullptr;
    }
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool cGZDBSerialRecordAsIGZFile::File::WriteWithCount(const void *src, uint32_t &length)
{
    void *record = Owner().mRecord;
    if (!record) return false;
    if (!VirtualCall<SetVoidFn, kRecSetFieldVoid>(record, src, length)) length = 0;
    return true;
}

bool cGZDBSerialRecordAsIGZFile::File::GetOpenModes(uint32_t *access, uint32_t *creation, uint32_t *sharing)
{
    *access = 0;
    *creation = 3;
    *sharing = 1;
    void *record = Owner().mRecord;
    if (!record) return false;
    void *persist = VirtualCall<PersistFn, kRecAsPersistRecord>(record);
    uint32_t flags = VirtualCall<RefFn, kPersistAccessFlags>(persist);
    if (flags & 1) *access |= 1;
    if (flags & 2) *access |= 2;
    return true;
}

namespace {

// The exit handler the original registers (0xd95310): the path string's destructor.
void __cdecl DestroyPath()
{
    reinterpret_cast<cRZString *>(&ts2_rva_10f500c)->Destruct();
}

} // namespace

void *cGZDBSerialRecordAsIGZFile::File::GetPath()
{
    if (ts2_rva_10f502c > _Init_thread_epoch) {
        _Init_thread_header(&ts2_rva_10f502c);
        if (ts2_rva_10f502c == -1) {
            reinterpret_cast<cRZString *>(&ts2_rva_10f500c)->Construct();
            atexit(&DestroyPath);
            _Init_thread_footer(&ts2_rva_10f502c);
        }
    }
    return &ts2_rva_10f500c;
}
