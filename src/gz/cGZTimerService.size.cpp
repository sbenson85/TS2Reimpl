#include "gz/cGZTimerService.h"

#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"

#include <new>

cGZTimerService::cGZTimerService()
    : rz::cRZSystemService(kServiceId, kPriority), mTime(0), mDelta(0), mRaw(0), mTicks(0), mOffset(0), mMaxDelta(kDefaultMaxDelta)
{
    mStopwatch.Construct(kStopwatchMode);
}

// The stopwatch's destructor (0x140fe0) does nothing; the service part's runs after this.
cGZTimerService::~cGZTimerService() {}

bool cGZTimerService::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != kTimerInterfaceId) return rz::cRZSystemService::QueryInterface(riid, ppv);
    *ppv = static_cast<cIGZTimerService *>(this);
    AddRef();
    return true;
}

uint32_t cGZTimerService::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZTimerService::Release() { return rz::cRZSystemService::Release(); }

bool cGZTimerService::Init()
{
    void *framework = GetServiceObject();
    VirtualCall<void(__thiscall *)(void *, void *), kFrameworkAddToTick>(framework, this);
    mStopwatch.Start();
    return true;
}

bool cGZTimerService::Shutdown()
{
    void *framework = GetServiceObject();
    VirtualCall<void(__thiscall *)(void *, void *), kFrameworkRemoveFromTick>(framework, this);
    return true;
}

bool cGZTimerService::OnTick(uint32_t)
{
    uint32_t elapsed = mStopwatch.Elapsed();
    uint32_t oldTime = mTime;
    uint32_t newTime = mOffset + elapsed;
    uint32_t delta = newTime - oldTime;
    mRaw = elapsed;
    mDelta = delta;
    if (delta > mMaxDelta) {
        // A step past the limit is cut to the limit, and the offset is moved so the clock stays there instead of catching
        // up later.
        newTime = oldTime + mMaxDelta;
        mDelta = mMaxDelta;
        mOffset = newTime - elapsed;
    }
    mTime = newTime;
    ++mTicks;
    return true;
}

void cGZTimerService::SetTimeOffset(uint32_t offset) { mOffset = offset; }

uint32_t cGZTimerService::GetTime() { return mTime; }
uint32_t cGZTimerService::GetDelta() { return mDelta; }
uint32_t cGZTimerService::GetTickCount() { return mTicks; }
void cGZTimerService::SetMaxDelta(uint32_t maxDelta) { mMaxDelta = maxDelta; }

// ---- adapters --------------------------------------------------------------------------------------------------------

cGZTimerService *__fastcall cGZTimerServiceAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZTimerService;
}
