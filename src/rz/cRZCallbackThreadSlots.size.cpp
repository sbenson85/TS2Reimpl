// cRZCallbackThread's slots and its thread body (lane LO, foundations F1; src/rz/cRZThread.h). Size profile, as the
// original's. The lock and the thread are called through their vtables, as the original does (both are value members
// other code may also reach through the game's vtables); the queue is a std::vector of 28-byte entries (library code).
#include "rz/cRZThread.h"

#include "game/vtable.h"

#include <algorithm>
#include <vector>

namespace {

using Queue = std::vector<cRZCallbackThread::Entry>;
static_assert(sizeof(Queue) == 12, "the queue is a vector");

Queue &QueueOf(cRZCallbackThread *self) { return *reinterpret_cast<Queue *>(&self->mField10); }

constexpr int kLockSlotLock = 1, kLockSlotUnlock = 2;
constexpr int kThreadSlotStart = 1, kThreadSlotSuspend = 4, kThreadSlotResume = 5, kThreadSlotWake = 7,
              kThreadSlotGetPriority = 9, kThreadSlotSetPriority = 10, kThreadSlotIsRunning = 11,
              kThreadSlotGetThreadId = 15;
constexpr int kSlotDeletingDestructor = 8;

void Lock(cRZCallbackThread *self) { VirtualCall<int32_t(__thiscall *)(void *), kLockSlotLock>(&self->mLock); }
void Unlock(cRZCallbackThread *self) { VirtualCall<int32_t(__thiscall *)(void *), kLockSlotUnlock>(&self->mLock); }

} // namespace

bool cRZCallbackThread::QueryInterface(uint32_t iid, void **out)
{
    if (iid != 1 && iid != kIid) return false;
    *out = this;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this);
    return true;
}

uint32_t cRZCallbackThread::AddRef()
{
    if (!mOwnedByCaller) return 1;
    return ++mField0c;
}

uint32_t cRZCallbackThread::Release()
{
    if (!mOwnedByCaller) return 1;
    if (mField0c > 1) return --mField0c;
    VirtualCall<void *(__thiscall *)(void *, uint32_t), kSlotDeletingDestructor>(this, 1);
    return 0;
}

uint32_t cRZCallbackThread::AddCallback(Callback callback, void *data, uint32_t delay, bool once, bool fixedRate)
{
    if (!mRunning) return 0;
    Lock(this);
    Entry entry;
    entry.mId = mField40++;
    entry.mCallback = callback;
    entry.mData = data;
    entry.mOnce = once;
    entry.mInterval = delay;
    entry.mFixedRate = fixedRate;
    Queue &queue = QueueOf(this);
    if (queue.empty()) {
        // The first entry: its due time saturates, and the thread is started (or resumed).
        const uint32_t now = mStopwatch.Elapsed();
        const uint32_t due = now + delay;
        entry.mDue = due < now ? 0xffffffffu : due;
        queue.push_back(entry);
        if (!VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotIsRunning>(&mThread))
            VirtualCall<uint32_t(__thiscall *)(void *, void *), kThreadSlotStart>(&mThread, &mStartableVTable);
        VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotResume>(&mThread);
    } else {
        entry.mDue = mStopwatch.Elapsed() + delay; // no saturation here
        queue.push_back(entry);
    }
    VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotWake>(&mThread);
    Unlock(this);
    return entry.mId;
}

bool cRZCallbackThread::RemoveCallback(uint32_t id)
{
    if (!mRunning) return false;
    Lock(this);
    Queue &queue = QueueOf(this);
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (it->mId == id) {
            queue.erase(it);
            Unlock(this);
            return true;
        }
    }
    Unlock(this);
    return false;
}

bool cRZCallbackThread::SetPriority(int32_t level)
{
    return VirtualCall<bool(__thiscall *)(void *, int32_t), kThreadSlotSetPriority>(&mThread, level);
}

int32_t cRZCallbackThread::GetPriority() { return VirtualCall<int32_t(__thiscall *)(void *), kThreadSlotGetPriority>(&mThread); }

uint32_t cRZCallbackThread::GetThreadId()
{
    uint32_t id;
    VirtualCall<uint32_t(__thiscall *)(void *, uint32_t *), kThreadSlotGetThreadId>(&mThread, &id);
    return id;
}

uint32_t __fastcall cRZCallbackThread::Run(void *startable, void *, void *)
{
    cRZCallbackThread *self = reinterpret_cast<cRZCallbackThread *>(static_cast<char *>(startable) - 4);
    Queue &queue = QueueOf(self);
    Queue due;
    while (self->mRunning) {
        while (!queue.empty()) {
            if (!self->mRunning) return 0;
            Lock(self);
            const uint32_t now = self->mStopwatch.Elapsed();
            uint32_t wait = 0xffffffffu;
            due.clear();
            for (auto it = queue.begin(); it != queue.end();) {
                if (it->mDue > now) {
                    wait = std::min(wait, it->mDue - now);
                    ++it;
                    continue;
                }
                due.push_back(*it);
                if (it->mOnce) {
                    it = queue.erase(it);
                    continue;
                }
                // A repeating entry: the next due time from its own (fixed rate) or from now; one that wrapped
                // waits for ever.
                uint32_t next = (it->mFixedRate ? it->mDue : now) + it->mInterval;
                if (next < now) next = 0xffffffffu;
                it->mDue = next;
                wait = std::min(wait, next - now);
                ++it;
            }
            Unlock(self);
            for (const Entry &entry : due) entry.mCallback(entry.mData);
            if (queue.empty()) break;
            if (!self->mRunning) return 0;
            const uint32_t spent = self->mStopwatch.Elapsed() - now;
            cRZThread::SleepMicroseconds((spent < wait ? wait - spent : 10u) * 1000u);
        }
        if (!self->mRunning) break;
        VirtualCall<uint32_t(__thiscall *)(void *), kThreadSlotSuspend>(&self->mThread);
    }
    return 0;
}
