#include "rz/cRZMutex.h"

#include "game/game.h"
#include "game/vtable.h"

#include <windows.h>

#include <new>

// cRZMutexData ----------------------------------------------------------------------------------

cRZMutexData *cRZMutexData::DeletingDestructor(uint32_t flags)
{
    mName.Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZMutexData));
    return this;
}

// cRZMutex: construction ------------------------------------------------------------------------

cRZMutex *cRZMutex::Construct(const cIGZString &name)
{
    mVTable = GameAddress(kVTable);
    mData = nullptr;
    // The original tests the result of operator new for null (the allocator throws instead).
    auto *data = static_cast<cRZMutexData *>(::operator new(sizeof(cRZMutexData)));
    data->mValid = 0;
    data->mName.Construct();
    data->mHandle = nullptr;
    mData = data;
    // The copy of the name goes through the string's own vtable (slot 16, operator=), the characters
    // for CreateMutexA come from the argument's (slot 5, ToChar).
    VirtualSlot<void *(__thiscall *)(void *, const cIGZString *)>(&data->mName, kNameSlotAssign)(&data->mName, &name);
    const char *text = name.ToChar();
    HANDLE handle = CreateMutexA(nullptr, FALSE, text);
    data->mHandle = handle;
    data->mValid = handle != nullptr ? 1 : 0;
    return this;
}

void cRZMutex::Destruct()
{
    mVTable = GameAddress(kVTable);
    cRZMutexData *data = mData;
    CloseHandle(data->mHandle);
    data->DeletingDestructor(1);
}

cRZMutex *cRZMutex::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// cRZMutex: slots -------------------------------------------------------------------------------

uint32_t cRZMutex::Delete()
{
    // The original tests `this` for null (a static helper taking the object in ecx); callers do
    // reach it with a null pointer, so the member has to cope.
    if (reinterpret_cast<uintptr_t>(this) != 0) {
        VirtualCall<void *(__thiscall *)(void *, uint32_t), kSlotDeletingDestructor>(this, 1);
    }
    return 0;
}

uint32_t cRZMutex::Lock()
{
    return WaitForSingleObject(mData->mHandle, INFINITE) != WAIT_FAILED ? 1 : 0;
}

uint32_t cRZMutex::Unlock()
{
    return ReleaseMutex(mData->mHandle) != 0 ? 1 : 0;
}

uint32_t cRZMutex::TryLock()
{
    DWORD result = WaitForSingleObject(mData->mHandle, 0);
    return (result != WAIT_FAILED && result != WAIT_TIMEOUT) ? 1 : 0;
}

uint32_t cRZMutex::IsValid()
{
    return mData->mValid;
}

uint32_t cRZMutex::GetHandle(void **out)
{
    *out = mData->mHandle;
    return 1;
}
