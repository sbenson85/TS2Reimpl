#include "ts/cTSPersistDBSegmentInfo.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZFrameworkHelpers.h"

#include <new>

namespace {

// Destroys a cRZString local when the scope ends, also when a callee throws (the original's unwind does).
struct StringGuard {
    cRZString *s;
    ~StringGuard() { s->Destruct(); }
};

using StringFromCharFn = uint32_t(__thiscall *)(void *, const char *);
using StringAssignFn = uint32_t(__thiscall *)(void *, const void *);
using ToCharFn = const char *(__thiscall *)(void *);
using SetStringsFn = uint32_t(__thiscall *)(void *, uint32_t, void *, void *, uint32_t, uint32_t);
using SetPathFn = uint32_t(__thiscall *)(void *, const char *, uint32_t);

} // namespace

cTSPersistDBSegmentInfo *cTSPersistDBSegmentInfo::Create()
{
    auto *self = static_cast<cTSPersistDBSegmentInfo *>(::operator new(sizeof(cTSPersistDBSegmentInfo)));
    if (!self) return nullptr;
    // The original first stores a temporary vtable (0xda5c64) at +0 and runs the cRZUnknown constructor on +4;
    // both are overwritten below, so only the reference count it zeroes remains.
    reinterpret_cast<RZUnknownLayout *>(&self->mUnknown)->Construct();
    self->mVTable = GameAddress(kVTable);
    self->mUnknown.mVTable = GameAddress(kUnknownVTable);
    self->mField0C = 0;
    self->mString1.Construct();
    self->mString2.Construct();
    self->mField50 = 0;
    self->mField54 = 0xffff;
    self->mFlags = 1;
    return self;
}

cTSPersistDBSegmentInfo *cTSPersistDBSegmentInfo::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    mUnknown.mVTable = GameAddress(kUnknownVTable);
    mString2.Destruct();
    mString1.Destruct();
    mUnknown.Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool cTSPersistDBSegmentInfo::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (!ppvObj) return false;
    if (riid == kIdSegmentInfo) {
        *ppvObj = this;
        ++mUnknown.mRefCount; // the original calls 0x148fb0 directly, not through the vtable
        return true;
    }
    return reinterpret_cast<RZUnknownQueryInterface *>(&mUnknown)->QueryInterfaceUnknown(riid, ppvObj);
}

uint32_t cTSPersistDBSegmentInfo::AddRef()
{
    return ++mUnknown.mRefCount;
}

uint32_t cTSPersistDBSegmentInfo::Release()
{
    return mUnknown.Release();
}

cRZString *cTSPersistDBSegmentInfo::GetString1()
{
    return &mString1;
}

cRZString *cTSPersistDBSegmentInfo::GetString2()
{
    return &mString2;
}

uint32_t cTSPersistDBSegmentInfo::GetField50()
{
    return mField50;
}

uint16_t cTSPersistDBSegmentInfo::GetField54()
{
    return mField54;
}

uint32_t cTSPersistDBSegmentInfo::GetFlags()
{
    return mFlags;
}

bool cTSPersistDBSegmentInfo::TestFlags(uint32_t mask)
{
    return (mFlags & mask) == mask;
}

void cTSPersistDBSegmentInfo::SetFromChars(uint32_t arg1, const char *text1, const char *text2, uint32_t arg4, uint32_t arg5)
{
    cRZString first;
    first.Construct();
    StringGuard guardFirst{&first};
    first.FromChar(text1);
    cRZString second;
    second.Construct();
    StringGuard guardSecond{&second};
    second.FromChar(text2);
    VirtualCall<SetStringsFn, kSetStringsSlot>(this, arg1, &first, &second, arg4, arg5);
}

uint32_t cTSPersistDBSegmentInfo::SetString2FromChar(const char *text)
{
    return VirtualCall<StringFromCharFn, 4>(&mString2, text);
}

uint32_t cTSPersistDBSegmentInfo::AssignString2(const void *other)
{
    return VirtualCall<StringAssignFn, 16>(&mString2, other);
}

void cTSPersistDBSegmentInfo::SetField50(uint32_t value)
{
    mField50 = value;
}

void cTSPersistDBSegmentInfo::SetField54(uint16_t value)
{
    mField54 = value;
}

void cTSPersistDBSegmentInfo::ChangeFlags(uint32_t mask, bool set)
{
    if (set)
        mFlags |= mask;
    else
        mFlags &= ~mask;
}

uint32_t cTSPersistDBSegmentInfo::SetPathFromString(void *string, uint32_t arg2)
{
    // The primary vtable is read before the string's slot 5 runs, as the original does.
    auto setPath = VirtualSlot<SetPathFn>(this, kSetPathSlot);
    const char *text = VirtualCall<ToCharFn, 5>(string);
    return setPath(this, text, arg2);
}

bool cTSPersistDBSegmentInfoThunks::QueryInterface4(uint32_t riid, void **ppvObj)
{
    return reinterpret_cast<cTSPersistDBSegmentInfo *>(reinterpret_cast<char *>(this) - 4)->QueryInterface(riid, ppvObj);
}

uint32_t cTSPersistDBSegmentInfoThunks::AddRef4()
{
    return ++reinterpret_cast<cRZUnknown *>(this)->mRefCount;
}

uint32_t cTSPersistDBSegmentInfoThunks::Release4()
{
    return reinterpret_cast<cRZUnknown *>(this)->Release();
}

void *cTSPersistDBSegmentInfoThunks::DeletingDestructor4(uint32_t flags)
{
    return reinterpret_cast<cTSPersistDBSegmentInfo *>(reinterpret_cast<char *>(this) - 4)->DeletingDestructor(flags);
}
