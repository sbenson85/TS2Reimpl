// nTSSG::cShadowManager (lane IR). See src/sg/cShadowManager.h.
#include "sg/cShadowManager.h"

#include "game/original.h"
#include "game/vtable.h"

#include <cstring>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

constexpr uint32_t kIidShadowManager = 0xcbf9c006;
constexpr uint32_t kIidMessageTarget2 = 0x452294aa;
constexpr uint32_t kOptionCount = 10;

// The cached texture service (0x20dfb5, cell 0x10cc2d0), not replaced yet.
void *TextureService() { return OriginalFunction<void *(__cdecl *)(), 0x20dfb5>()(); }

} // namespace

namespace nTSSG {

bool cShadowManager::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == 1 || iid == kIidShadowManager) {
        *out = this;
    } else if (iid == kIidMessageTarget2) {
        *out = &mVTable4;
    } else {
        return false;
    }
    Call<1>(this);
    return true;
}

uint32_t cShadowManager::Release()
{
    if (--mRefCount) return mRefCount;
    Call<17>(this, 1u);
    return 0;
}

void cShadowManager::SetOption(uint32_t index, uint8_t on)
{
    if (index >= kOptionCount) return;
    uint32_t &word = (&mOptionBits)[index >> 5];
    const uint32_t bit = 1u << (index & 31);
    if (uint8_t((word & bit) != 0) == on) return;
    if (on) word |= bit;
    else word &= ~bit;
    ++mChangeCount;
}

void cShadowManager::GetOptionBits(uint32_t *out) { *out = mOptionBits; }

cShadowEntry *cShadowManager::Entry(int32_t index)
{
    if (index < 0 || uint32_t(index) >= uint32_t(mShadowsLast - mShadows)) return nullptr;
    cShadowEntry *entry = mShadows + index;
    return entry->mUsers > 0 ? entry : nullptr;
}

uint32_t cShadowManager::ShadowTexture(int32_t index)
{
    cShadowEntry *entry = Entry(index);
    if (!entry) return 0;
    uint32_t result[2];
    return *Call<24, uint32_t *>(TextureService(), result, entry->mTextureHandle);
}

bool cShadowManager::GetShadowMatrix(int32_t index, float *out, uint32_t *stamp)
{
    const cShadowEntry *entry = Entry(index);
    if (!entry) return false;
    if (stamp) {
        if (*stamp == entry->mMatrixStamp) return false;
        *stamp = entry->mMatrixStamp;
    }
    std::memcpy(out, entry->mMatrix, sizeof(entry->mMatrix)); // bit copies (random bits may be NaNs)
    return true;
}

void cShadowManager::GetShadowScale(int32_t index, float *offset, float *scale)
{
    if (const cShadowEntry *entry = Entry(index)) {
        Call<8>(mRenderer, entry->mScaleKeys[0], entry->mScaleKeys[1], offset, scale);
        return;
    }
    offset[0] = 0.0f, offset[1] = 0.0f;
    scale[0] = 1.0f, scale[1] = 1.0f;
}

cShadowBox cShadowManager::GetShadowBox(int32_t index)
{
    if (const cShadowEntry *entry = Entry(index)) {
        cShadowBox box;
        std::memcpy(box.mValues, entry->mBox, sizeof(box.mValues));
        return box;
    }
    return cShadowBox{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
}

cShadowManager *cShadowManager::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(cShadowManager *), 0x2e6c70>()(this);
    if (flags & 1) ::operator delete(this, sizeof(cShadowManager));
    return this;
}

} // namespace nTSSG
