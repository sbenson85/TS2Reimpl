#include "gz/cGZPersistDBRecord.h"

#include "game/game.h"
#include "game/vtable.h"

#include <cstring>
#include <new>

namespace {

using RefFn = uint32_t(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using ChildCallFn = bool(__thiscall *)(void *, void *);
using DeleteFn = void *(__thiscall *)(void *, uint32_t);

} // namespace

bool cGZPersistDBRecord::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kIdRecord && riid != kIdUnknown) return false;
    *ppvObj = this;
    VirtualCall<RefFn, 1>(this); // AddRef through this object's own vtable
    return true;
}

// Slot 1 (0x469a20, folded with cTSBehaviorNode's slot 1): a 16-bit increment that wraps at 0xffff;
// the result is the zero-extended word.
uint32_t cGZPersistDBRecord::AddRef()
{
    ++mRefCount;
    return mRefCount;
}

uint32_t cGZPersistDBRecord::Release()
{
    if (mRefCount > 1) {
        --mRefCount;
        return mRefCount;
    }
    // Last reference (the count is 0 or 1 and is left as it is): the deleting destructor, flags 1.
    VirtualCall<DeleteFn, kDeleteSlot>(this, 1);
    return 0;
}

void cGZPersistDBRecord::SetKey(const void *key)
{
    std::memcpy(mKey, key, sizeof(mKey));
}

uint32_t cGZPersistDBRecord::GetAccessFlags()
{
    return static_cast<uint8_t>(mFlags);
}

// Calls the child's slot with this object as its one argument, then releases the child whatever the
// result. Returns the child's result, or false without a child.
static bool CallChildThenRelease(cGZPersistDBRecord *self, void *&child, int slot)
{
    if (!child) return false;
    bool result = VirtualSlot<ChildCallFn>(child, slot)(child, self);
    // The original reloads the child pointer here, so a child replaced during the call is the one released.
    void *current = child;
    if (current) VirtualCall<RefFn, 2>(current);
    child = nullptr;
    return result;
}

bool cGZPersistDBRecord::Close()
{
    return CallChildThenRelease(this, mChild, kCloseChildSlot);
}

bool cGZPersistDBRecord::Abort()
{
    return CallChildThenRelease(this, mChild, kAbortChildSlot);
}

bool cGZPersistDBRecord::QueryChild(uint32_t riid, void **ppvObj)
{
    if (!mChild) return false;
    return VirtualCall<QueryFn, 0>(mChild, riid, ppvObj);
}

void cGZPersistDBRecord::SetChild(void *child)
{
    if (child) VirtualCall<RefFn, 1>(child); // AddRef the new child first
    void *old = mChild;
    if (old) VirtualCall<RefFn, 2>(old);
    mChild = child;
}

bool cGZPersistDBRecord::TestHighFlags(uint8_t mask)
{
    uint8_t high = static_cast<uint8_t>(mFlags >> 8);
    return (high & mask) == mask;
}

// Set: the value's bits are ORed into the high byte. Clear: the high byte keeps only the bits that are
// not in the value, and the low byte (the access flags) becomes 0. The original does this too (the mask
// it builds for the clear case has no low byte bits), so the access flags are lost on a clear; this is
// reproduced as it is.
uint32_t cGZPersistDBRecord::SetHighFlags(uint8_t value, uint8_t set)
{
    uint32_t word;
    if (set) {
        word = (static_cast<uint32_t>(value) << 8) | mFlags;
    } else {
        uint32_t high = (mFlags >> 8) & static_cast<uint8_t>(~value);
        word = high << 8;
    }
    mFlags = static_cast<uint16_t>(word);
    return mFlags;
}

uint32_t cGZPersistDBRecord::SetAccessFlags(uint8_t value)
{
    mFlags = static_cast<uint16_t>((mFlags & 0xff00) | value);
    return mFlags;
}

void cGZPersistDBRecord::Destruct()
{
    mVTable = VTable();
    if (mChild) VirtualCall<RefFn, 2>(mChild);
}

void *cGZPersistDBRecord::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}
