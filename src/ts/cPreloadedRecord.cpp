#include "ts/cPreloadedRecord.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistResourceManager.h"
#include "rz/RZMemory.h"

#include <cstring>
#include <new>

// The speculative loader's record (see the header). The original's translation unit has C++ EH; nothing here can throw.

// Not a function in the original (inlined in Process and Update).
void cPreloadedRecord::Construct(const cGZPersistResourceKey &key)
{
    cGZPersistDBSerialRecord::Construct(&key, nullptr, 1);
    mVTable = GameAddress(kVTable);
    mRecordVTable = GameAddress(kRecordVTable);
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x20) = GameAddress(kOStreamVTable);
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x30) = GameAddress(kIStreamVTable);
    mPosition = 0;
    mData = nullptr;
    mSize = 0;
    mError = 0;
    mCompressed = 0;
}

// Slot 0, 0x1f82b0.
bool cPreloadedRecord::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kInterfaceId) {
        VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef first
        *out = this;
        return true;
    }
    return cGZPersistDBSerialRecord::QueryInterface(riid, out);
}

// Slot 27, 0x1f7d50: the raw read every typed getter goes through.
bool cPreloadedRecord::GetFieldVoid(void *dst, uint32_t n)
{
    if (mSize - mPosition < n) {
        mError = 1;
        return false;
    }
    std::memcpy(dst, mData + mPosition, n);
    mPosition += n;
    return true;
}

// Slot 55, 0x1f8c00.
bool cPreloadedRecord::SeekAbsolute(uint32_t pos)
{
    mPosition = pos;
    if (pos > mSize) mPosition = mSize;
    return true;
}

// Slot 56, 0x1f8c20.
bool cPreloadedRecord::SeekRelative(int32_t delta)
{
    const uint32_t magnitude = 0u - static_cast<uint32_t>(delta);
    if (delta < 0 && mPosition < magnitude) {
        mPosition = 0;
    } else if (mSize - mPosition < static_cast<uint32_t>(delta)) {
        // ORIGINAL-BUG: a negative delta that stays in range compares as a huge unsigned value and lands here, so a
        // backward seek can only reach 0 or the end (mSize).
        mPosition = mSize;
    } else {
        mPosition += delta;
    }
    return true;
}

// 0x1f74b0 (+4 slot 19 reaches it through a thunk).
// The destructor body; the base class's destructor (cGZPersistDBSerialRecord::DestructMembers) runs after it, and as the
// unwind action (the original's one-state EH frame) if the body throws.
cPreloadedRecord::~cPreloadedRecord()
{
    uint8_t *self = reinterpret_cast<uint8_t *>(this);
    mVTable = GameAddress(kVTable);
    mRecordVTable = GameAddress(kRecordVTable);
    *reinterpret_cast<const void **>(self + 0x20) = GameAddress(kOStreamVTable);
    *reinterpret_cast<const void **>(self + 0x30) = GameAddress(kIStreamVTable);
    if (mData) RZMemory::Free(mData);
}

cPreloadedRecord *cPreloadedRecord::DeletingDestructor(uint32_t flags)
{
    this->~cPreloadedRecord();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}
