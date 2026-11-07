#include "ts/cRAMShadowRecord.h"

#include "game/game.h"

#include <cstring>
#include <new>

// cTSPersistSystem's RAM copy of a record (see the header).

cRAMShadowRecord::cRAMShadowRecord(const cGZPersistResourceKey *key)
{
    cGZPersistDBSerialRecord::Construct(key, nullptr, 1);
    uint8_t *self = reinterpret_cast<uint8_t *>(this);
    mVTable = GameAddress(kVTable);
    mRecordVTable = GameAddress(kRecordVTable);
    *reinterpret_cast<const void **>(self + 0x20) = GameAddress(kOStreamVTable);
    *reinterpret_cast<const void **>(self + 0x30) = GameAddress(kIStreamVTable);
    mPosition = 0;
    mError = 0;
}

// Slot 27, 0x1dff20: the raw read every typed getter goes through.
bool cRAMShadowRecord::GetFieldVoid(void *dst, uint32_t n)
{
    if (mData.size() - mPosition < n) {
        mError = 1;
        return false;
    }
    std::memcpy(dst, mData.data() + mPosition, n);
    mPosition += n;
    return true;
}

// Slot 52, 0x1e0940.
uint32_t cRAMShadowRecord::GetSize()
{
    return static_cast<uint32_t>(mData.size());
}

// Slot 55, 0x1e4a90.
bool cRAMShadowRecord::SeekAbsolute(uint32_t pos)
{
    mPosition = pos;
    const uint32_t size = static_cast<uint32_t>(mData.size());
    if (pos > size) mPosition = size;
    return true;
}

// Slot 56, 0x1e4ac0.
bool cRAMShadowRecord::SeekRelative(int32_t delta)
{
    if (delta < 0 && mPosition < 0u - static_cast<uint32_t>(delta)) {
        mPosition = 0;
    } else if (mData.size() - mPosition < static_cast<uint32_t>(delta)) {
        // ORIGINAL-BUG: a negative delta that stays in range compares as a huge unsigned value and lands here, so a
        // backward seek can only reach 0 or the end.
        mPosition = static_cast<uint32_t>(mData.size());
    } else {
        mPosition += delta;
    }
    return true;
}

// 0x1da120 (+4 slot 19 and the stream faces' slot 21 reach it through thunks). The vector member is freed first, then
// the base destructor (cGZPersistDBSerialRecord::DestructMembers) runs.
cRAMShadowRecord *cRAMShadowRecord::DeletingDestructor(uint32_t flags)
{
    this->~cRAMShadowRecord();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}
