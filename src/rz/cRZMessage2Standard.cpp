#include "rz/cRZMessage2Standard.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZMessage2Standard.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZMemory.h"

namespace {

using Thiscall0 = uint32_t(__thiscall *)(void *);
using GetIdFn = uint32_t(__thiscall *)(void *);
using DeletingDtor = void *(__thiscall *)(void *, uint32_t);

cIGZMessage2Standard *Face(void *message) { return static_cast<cIGZMessage2Standard *>(message); }

void ReleaseHeld(void *object)
{
    if (object) static_cast<cIGZUnknown *>(object)->Release();
}

// The pointer setters' flag: set for a non-null object, cleared for null.
void SetFlag(uint8_t &flags, uint8_t bit, const void *object)
{
    if (object)
        flags |= bit;
    else
        flags &= static_cast<uint8_t>(~bit);
}

} // namespace

// The bool results are returned in al only, as the originals do (they write nothing to the rest of
// eax); callers read al.

bool cRZMessage2Standard::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != 0x65297976 && riid != 0xa52294b4) return false;
    *out = this;
    VirtualCall<Thiscall0, 1>(this); // AddRef through the vtable
    return true;
}

uint32_t cRZMessage2Standard::Release()
{
    if (mRefCount > 1) return --mRefCount;
    VirtualCall<DeletingDtor, 37>(this, 1);
    return 0;
}

void cRZMessage2Standard::SetId(uint32_t id)
{
    mId = id;
    if (id)
        mFlags |= kFlagId;
    else
        mFlags &= static_cast<uint8_t>(~kFlagId);
}

bool cRZMessage2Standard::IdEquals(const void *other)
{
    return mId == VirtualSlot<GetIdFn>(other, 4)(const_cast<void *>(other)); // receiver: cRZMessage2Standard
}

bool cRZMessage2Standard::IdLess(const void *other)
{
    return mId < VirtualSlot<GetIdFn>(other, 4)(const_cast<void *>(other)); // receiver: cRZMessage2Standard
}

bool cRZMessage2Standard::HasValue0() const { return (mFlags & kFlagValue0) != 0; }
bool cRZMessage2Standard::HasValue1() const { return (mFlags & kFlagValue1) != 0; }
bool cRZMessage2Standard::HasValue2() const { return (mFlags & kFlagValue2) != 0; }
bool cRZMessage2Standard::HasValue3() const { return (mFlags & kFlagValue3) != 0; }
bool cRZMessage2Standard::HasPtr20() const { return (mFlags & kFlagPtr20) != 0; }
bool cRZMessage2Standard::HasPtr24() const { return (mFlags & kFlagPtr24) != 0; }

void cRZMessage2Standard::SetValue0(uint32_t value)
{
    mFlags |= kFlagValue0;
    mValue[0] = value;
}

void cRZMessage2Standard::SetValue1(uint32_t value)
{
    mFlags |= kFlagValue1;
    mValue[1] = value;
}

void cRZMessage2Standard::SetValue2(uint32_t value)
{
    mFlags |= kFlagValue2;
    mValue[2] = value;
}

void cRZMessage2Standard::SetValue3(uint32_t value)
{
    mFlags |= kFlagValue3;
    mValue[3] = value;
}

cRZMessage2Standard *cRZMessage2Standard::Construct()
{
    mVTable = &ts2_rva_da4064;
    mRefCount = 0;
    mFlags = 0;
    mId = 0;
    for (uint32_t &value : mValue) value = 0;
    mPtr20 = nullptr;
    mPtr24 = nullptr;
    mPtr28 = nullptr;
    return this;
}

void cRZMessage2Standard::Destruct()
{
    mVTable = &ts2_rva_da4064;
    ReleaseHeld(mPtr28);
    ReleaseHeld(mPtr24);
    ReleaseHeld(mPtr20);
}

bool cRZMessage2Standard::Create(uint32_t iid, void **out)
{
    void *memory = RZMemory::Malloc(kSize);
    cRZMessage2Standard *message = memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr;
    if (!Face(message)->QueryInterface(iid, out)) {
        VirtualCall<DeletingDtor, 37>(message, 1);
        return false;
    }
    Face(message)->SetType(Face(this)->GetType());
    return true;
}

bool cRZMessage2Standard::GetString(uint32_t iid, void **out)
{
    if (!Face(this)->HasString()) return false;
    return static_cast<cIGZUnknown *>(mPtr20)->QueryInterface(iid, out);
}

void *cRZMessage2Standard::SetString(void *string)
{
    SetFlag(mFlags, kFlagPtr20, string);
    return reinterpret_cast<RZUnknownSlot *>(&mPtr20)->Assign(string);
}

void *cRZMessage2Standard::SetObject(void *object)
{
    SetFlag(mFlags, kFlagPtr24, object);
    return reinterpret_cast<RZUnknownSlot *>(&mPtr24)->Assign(object);
}

bool cRZMessage2Standard::HasExtra() const { return (mFlags & kFlagPtr28) != 0; }

bool cRZMessage2Standard::GetExtra(uint32_t iid, void **out)
{
    if (!Face(this)->HasExtra()) return false;
    return static_cast<cIGZUnknown *>(mPtr28)->QueryInterface(iid, out);
}

void *cRZMessage2Standard::SetExtra(void *object)
{
    SetFlag(mFlags, kFlagPtr28, object);
    return reinterpret_cast<RZUnknownSlot *>(&mPtr28)->Assign(object);
}

cRZMessage2Standard *cRZMessage2Standard::DeletingDestructor(uint32_t flags)
{
    Destruct();
    // With flag 4 the original hands the block and its size to an empty function: nothing is freed.
    if ((flags & 1) && !(flags & 4)) RZMemory::Free(this);
    return this;
}
