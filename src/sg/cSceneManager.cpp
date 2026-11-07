// nTSSG::cSceneManager (lane IR). See src/sg/cSceneManager.h.
#include "sg/cSceneManager.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

constexpr uint32_t kIidSceneManager = 0x69512c64;
constexpr uint32_t kIidSceneManager2 = 0x69512c6d;
// The global call counter of slot 23 and its period.
constexpr uint32_t kTickCounter = 0x10d92dc;
constexpr uint32_t kTickPeriod = 30;
constexpr uint32_t kObjectSize = 0x528;

} // namespace

namespace nTSSG {

bool cSceneManager::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != 1 && iid != kIidSceneManager && iid != kIidSceneManager2) return false;
    *out = this;
    Call<1>(this);
    return true;
}

uint32_t cSceneManager::Release()
{
    if (--mRefCount) return mRefCount;
    Call<28>(this, 1u);
    return 0;
}

void cSceneManager::SetRef520(void *object)
{
    void *old = mRef520;
    if (object == old) return;
    if (object) Call<1>(object);
    mRef520 = object;
    if (old) Call<2>(old);
}

uint32_t cSceneManager::LastFrame() { return mFramesCount - 1 + mFirstFrame; }

uint32_t cSceneManager::FrameValue(uint32_t frame)
{
    const uint32_t position = mFramesOffset - mFirstFrame + frame;
    return mFramesMap[(position >> 1) & (mFramesMapSize - 1)][position & 1].mValue;
}

uint8_t cSceneManager::FrameFlag(uint32_t frame)
{
    const uint32_t position = mFramesOffset - mFirstFrame + frame;
    return mFramesMap[(position >> 1) & (mFramesMapSize - 1)][position & 1].mFlag;
}

void *cSceneManager::Tick()
{
    uint32_t &counter = *GameAddress<uint32_t>(kTickCounter);
    if (!counter) Call<18>(mRef51C, mRef8);
    counter = (counter + 1) % kTickPeriod;
    return mRef51C;
}

const char *cSceneManager::Name1() { return Call<5, const char *>(&mCounters[1]); }

const char *cSceneManager::Name10() { return Call<5, const char *>(&mCounters[10]); }

void cSceneManager::ResetSamples()
{
    mSamplesReset = 1;
    mSamples.resize(0);
    mSampleSums[0] = 0;
    mSampleSums[1] = 0;
}

cSceneManager *cSceneManager::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cSceneManager *), 0x2bd900>()(this);
    if (flags & 1) ::operator delete(this, kObjectSize);
    return this;
}

} // namespace nTSSG
