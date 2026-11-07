#include "rz/cRZThreadRelease.h"

#include "game/game.h"
#include "game/vtable.h"

#define NOMINMAX
#include <windows.h>

#include <new>

namespace appdebugger {

void __fastcall DestroyThreadState(cRZThreadState *self, void *)
{
    if (self->mField4) CloseHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(self->mField4)));
    self->mLock.Destruct();
    self->mName.Destruct();
}

uint32_t __fastcall ReleaseThreadState(cRZThreadState *self, void *)
{
    using LockFn = int32_t(__thiscall *)(void *);
    void *lock = &self->mLock;
    VirtualCall<LockFn, 1>(lock);
    uint32_t count = self->mRefCount;
    if (count > 1) {
        --count;
        self->mRefCount = count;
        VirtualCall<LockFn, 2>(lock);
        return count;
    }
    // The last reference (or none left to count): the lock is released before it is destroyed with the block.
    VirtualCall<LockFn, 2>(lock);
    DestroyThreadState(self, nullptr);
    ::operator delete(self, sizeof(cRZThreadState));
    return 0;
}

uint32_t __fastcall DestroyThread(cRZThread *self, void *)
{
    self->mVTable = GameAddress(cRZThread::kVTable);
    return ReleaseThreadState(self->mState, nullptr);
}

} // namespace appdebugger
