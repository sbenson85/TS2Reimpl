// cRZCallbackThread's destructor (lane HT; src/rz/cRZThread.h). Size profile, as the original's.
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZThread.h"
#include "rz/cRZThreadRelease.h"

#include <memory>
#include <new>
#include <vector>

namespace {

// A queued callback (28 bytes): never read here, only freed with the queue's buffer.
struct QueuedCallback {
    uint32_t words[7];
};
using CallbackQueue = std::vector<QueuedCallback>;
static_assert(sizeof(CallbackQueue) == 12);

// The slots used, of the lock (cIGZCriticalSection) and the thread (cIGZThread).
constexpr int kLockSlotLock = 1, kLockSlotUnlock = 2;
constexpr int kThreadSlotResume = 5, kThreadSlotWake = 7, kThreadSlotWait = 8, kThreadSlotIsRunning = 11;

} // namespace

void cRZCallbackThread::Destruct()
{
    mVTable = GameAddress(kVTable);
    mStartableVTable = GameAddress(kStartableVTable);
    VirtualCall<int32_t(__thiscall *)(void *), kLockSlotLock>(&mLock);
    mRunning = 0;
    auto *queue = reinterpret_cast<CallbackQueue *>(&mField10);
    queue->clear();
    if (VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotIsRunning>(&mThread)) {
        VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotResume>(&mThread);
        VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotWake>(&mThread);
        VirtualCall<uint32_t(__thiscall *)(void *, int32_t), kThreadSlotWait>(&mThread, -1);
    }
    VirtualCall<int32_t(__thiscall *)(void *), kLockSlotUnlock>(&mLock);
    mLock.Destruct();
    appdebugger::DestroyThread(&mThread, nullptr);
    // The stopwatch's destructor does nothing (0x140fe0).
    std::destroy_at(queue);
}

cRZCallbackThread *cRZCallbackThread::DeletingDestruct(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}
