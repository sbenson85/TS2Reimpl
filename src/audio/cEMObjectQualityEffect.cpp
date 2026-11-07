#include "audio/cEMObjectQualityEffect.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"

namespace {

struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A curve's text: 0 maps to `start`, 1 to `end`.
void LoadCurve(void *curve, double start, double end)
{
    LocalString text;
    text.s.Sprintf(GameAddress<const char>(cEMObjectQualityEffect::kCurveText), start, end);
    OriginalFunction<bool(__thiscall *)(void *, void *), 0xc0a86a>()(curve, &text.s);
}

// The value a curve gives, through a float and a 64-bit truncation.
uint32_t CurveValue(void *curve, float x)
{
    const float value = static_cast<float>(OriginalFunction<double(__thiscall *)(void *, float), 0xc0a6a1>()(curve, x));
    return static_cast<uint32_t>(static_cast<int64_t>(value));
}

} // namespace

bool cEMObjectQualityEffect::Init()
{
    if (!OriginalFunction<bool(__thiscall *)(void *), 0x240684>()(this)) return false;
    LoadCurve(mLowCurve, kLowCurveStart, kCurveEnd);
    LoadCurve(mHighCurve, kHighCurveStart, 0.0);
    return true;
}

bool cEMObjectQualityEffect::Apply(void *event, void *)
{
    const uint32_t key = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 11>(event, kQualityFilterId);
    void *registry = VirtualCall<void *(__thiscall *)(void *), 26>(GetCachedSingleton());
    void *found = VirtualCall<void *(__thiscall *)(void *, uint32_t), 29>(registry, key);
    if (!found) return false;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(found);
    uint32_t &toggle = *GameAddress<uint32_t>(kQualityToggle);
    toggle = toggle == 0;
    const float quality = static_cast<float>(toggle);
    const uint32_t low = CurveValue(mLowCurve, quality);
    const uint32_t high = CurveValue(mHighCurve, quality);
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 12>(event, kLowValueId, low);
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 12>(event, kHighValueId, high);
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(found);
    return true;
}
