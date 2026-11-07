// The point and spot lights' shared parts (lane OT). See the header.
#include "sg/SGLightShapes.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "rz/Singleton_20ddfb.h"

#include <cmath>

namespace nGZSceneGraph {

namespace {
constexpr uint32_t kSettingsClass = 0x0ac1887e;
constexpr uint32_t kSettingsIid = 0x4bb0d269;
constexpr uint32_t kCutoffName = 0xdb6090; // "lightRadianceRangeCutoff"
constexpr int kFrameworkGetService = 5;
constexpr int kSettingsGet = 9;
constexpr float kDefaultCutoff = 0.5f;
constexpr float kUnboundedRange = 10000.0f; // 0xdba8d4
} // namespace

EmbeddedSphereShape::EmbeddedSphereShape() : mVTable(GameAddress<const void>(kVTable)), mData{}, mRefCount(0) {}

EmbeddedConeShape::EmbeddedConeShape() : mVTable(GameAddress<const void>(kVTable)), mData{}, mRefCount(0) {}

void EmbeddedConeShape::Fit(float range, float falloffAngle, float coneAngle)
{
    mData[1] = range * 0.5f;
    mData[0] = static_cast<float>(std::tan(static_cast<double>((falloffAngle + coneAngle) * 0.5f))) * range;
    mData[2] = 0.0f;
    mData[3] = 0.0f;
    mData[4] = -range * 0.5f;
}

RadianceCutoff::RadianceCutoff()
{
    ScopedRef settings{nullptr};
    AnyFloat32 value{GameAddress<const void>(gzany::kFloat32VTable), kDefaultCutoff};
    if (void *framework = GetServiceObject())
        VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **), kFrameworkGetService>(
            framework, kSettingsClass, kSettingsIid, &settings.mObject);
    mSettings = settings.mObject;
    {
        GZServiceString key(GameAddress<const char>(kCutoffName));
        // ORIGINAL-BUG: a missing settings object is called through (the original does the same).
        VirtualCall<uint32_t(__thiscall *)(void *, GZServiceString *, AnyFloat32 *), kSettingsGet>(mSettings, &key, &value);
    }
    mValue = value.mValue;
}

RadianceCutoff::~RadianceCutoff()
{
    ScopedRef settings{mSettings};
    settings.ReleaseObject();
}

float LightColorLength(const float *c)
{
    const float sum = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    return static_cast<float>(std::sqrt(static_cast<double>(sum)));
}

float LightRange(const float *colour, float intensity, float falloff, float cutoff)
{
    const float a = colour[0] * intensity;
    const float b = colour[1] * intensity;
    const float c = colour[2] * intensity;
    const float sum = (a * a + b * b) + c * c;
    const float r = static_cast<float>(std::sqrt(static_cast<double>(sum))) / cutoff;
    if (1.0f >= r) return 0.0f;
    if (0.0f >= falloff) return kUnboundedRange;
    const float x = (r - 1.0f) / falloff;
    if (!(x > 0.0f)) return 0.0f;
    return static_cast<float>(std::sqrt(static_cast<double>(x)));
}

} // namespace nGZSceneGraph
