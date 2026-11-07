#include "rz/cRZThread.h"

#include "game/game.h"
#include "game/vtable.h"

#include <windows.h>

#include <new>
#include <string>

cRZThreadState *cRZThreadState::Construct()
{
    mField0 = 0;
    mField4 = 0;
    mThreadId = 0;
    mMagic = 0x12345678;
    mName.Construct();
    mRefCount = 0;
    mLock.Construct();
    return this;
}

uint32_t cRZThreadState::AddRef()
{
    VirtualCall<int32_t(__thiscall *)(void *), 1>(&mLock); // lock
    uint32_t count = mRefCount + 1;
    mRefCount = count;
    VirtualCall<int32_t(__thiscall *)(void *), 2>(&mLock); // unlock
    return count;
}

cRZThread *cRZThread::Construct()
{
    mVTable = GameAddress(kVTable);
    auto *state = static_cast<cRZThreadState *>(::operator new(sizeof(cRZThreadState)));
    state->Construct();
    mState = state;
    state->AddRef();
    return this;
}

void __cdecl cRZThread::AnnounceThreadName(const char *name, uint32_t threadId)
{
    // THREADNAME_INFO: type 0x1000, the name, the thread id, flags 0.
    ULONG_PTR info[4] = {0x1000, reinterpret_cast<ULONG_PTR>(name), threadId, 0};
    __try {
        RaiseException(kThreadNameException, 0, 4, info);
    } __except (EXCEPTION_CONTINUE_EXECUTION) {
    }
}

bool cRZThread::SetName(const char *name)
{
    cRZThreadState *state = mState;
    std::string &text = state->mName.mString;
    if (name) {
        text.assign(name);
    } else {
        text.erase(static_cast<size_t>(0));
    }
    if (state->mThreadId == 0) return false;
    AnnounceThreadName(text.c_str(), state->mThreadId);
    return true;
}

cRZCallbackThread *cRZCallbackThread::Construct()
{
    mStartableVTable = GameAddress(kStartableVTable);
    mRunning = 0;
    mOwnedByCaller = 0;
    mVTable = GameAddress(kVTable);
    mField0c = 0;
    mField10 = 0;
    mField14 = 0;
    mField18 = 0;
    mStopwatch.Construct(4);
    mThread.Construct();
    mField40 = 1;
    mLock.Construct();
    mStopwatch.Start();
    mRunning = 1;
    VirtualCall<bool(__thiscall *)(void *, const char *), cRZThread::kSlotSetName>(&mThread, GameAddress<const char>(kNameRva));
    return this;
}
