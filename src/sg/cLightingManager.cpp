// nTSSG::cLightingManager (lane IR). See src/sg/cLightingManager.h.
#include "sg/cLightingManager.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

constexpr uint32_t kIidLightingManager = 0x6b390bfe;
constexpr uint32_t kIidMessageTarget2 = 0x452294aa;
// Game data: the default name, slot 20's switch and slot 34's luminance factors.
constexpr uint32_t kDefaultName = 0xdaefa0;
constexpr uint32_t kRefreshSwitch = 0xffde64;
constexpr uint32_t kLuminanceX = 0xdadf5c;
constexpr uint32_t kLuminanceY = 0xdb687c;
constexpr uint32_t kLuminanceZ = 0xdb6874;

float Factor(uint32_t rva) { return *GameAddress<const float>(rva); }

} // namespace

namespace nTSSG {

bool cLightingManager::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == 1 || iid == kIidLightingManager) {
        *out = this;
    } else if (iid == kIidMessageTarget2) {
        *out = &mVTable4;
    } else {
        return false;
    }
    Call<1>(this);
    return true;
}

uint32_t cLightingManager::Release()
{
    if (--mRefCount) return mRefCount;
    Call<38>(this, 1u);
    return 0;
}

void cLightingManager::SetFlag44(uint8_t value)
{
    mFlag44 = value;
    mUseDefaultName = 1;
}

void cLightingManager::Refresh()
{
    OriginalFunction<void(__thiscall *)(cLightingManager *, uint32_t), 0x2d4010>()(this, 0);
    if (*GameAddress<uint8_t>(kRefreshSwitch))
        OriginalFunction<void(__thiscall *)(cLightingManager *, float), 0x2dfc80>()(this, 1.0f);
    mFlag81 = 1;
}

void cLightingManager::AddSample(const float *values, float weight)
{
    cLightSample sample;
    for (int i = 0; i < 4; ++i) reinterpret_cast<uint32_t *>(sample.mValues)[i] = reinterpret_cast<const uint32_t *>(values)[i];
    sample.mWeight = weight;
    mSamples.push_back(sample);
}

const char *cLightingManager::Name()
{
    if (mUseDefaultName) return GameAddress<const char>(kDefaultName);
    return mName.c_str();
}

uint32_t cLightingManager::Value404() { return *reinterpret_cast<const uint32_t *>(mRef2C + 0x404); }

const cLightSet &cLightingManager::CurrentSet() const
{
    return mCurrentSet < uint32_t(mLightSets.end() - mLightSets.begin()) ? mLightSets.begin()[mCurrentSet] : mDefaultSet;
}

cLightVector cLightingManager::Vector18C() { return CurrentSet().mVector18C; }

cLightVector cLightingManager::Vector19C() { return CurrentSet().mVector19C; }

cLightVector cLightingManager::Vector174() { return CurrentSet().mVector174; }

cLightVector cLightingManager::NegatedDirection()
{
    const cLightVector &d = CurrentSet().mDirection;
    return cLightVector{-d.x, -d.y, -d.z};
}

cLightVector cLightingManager::Vector2C4() { return mVector2C4; }

float cLightingManager::Luminance(uint32_t key)
{
    cLightVector colour;
    Call<16>(mRef24, &colour, key);
    return colour.x * Factor(kLuminanceX) + colour.y * Factor(kLuminanceY) + colour.z * Factor(kLuminanceZ);
}

cLightingManager *cLightingManager::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cLightingManager *), 0x2cb800>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cLightingManager));
    return this;
}

} // namespace nTSSG
