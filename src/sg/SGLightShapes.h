#pragma once
// The bound shapes the point and spot lights embed, and what their OnLightChanged bodies share (lane OT, scenegraph N2).
//
// A point light's bound is a game cTranslatedSphereShape embedded at +0x70 (0x18 bytes: its vtable 0xda1970, the sphere's
// radius and centre xyz, a reference count) and a spot light's a game cone shape at +0x80 (0x1c bytes: vtable 0xda1b00, five
// floats, the count). Slot 37 of the light hands the shape out (GetBoundShape) and the game's shape code (collision, the
// lighting manager) runs on it, so the shapes keep the game's vtables and are plain data here: slot 44 of both shapes
// (0x18ea60 / its cone twin) is `lea eax,[ecx+4]`, so the lights' writes to the floats go straight to the fields.
#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// The embedded sphere (point light, +0x70): data[0] the radius, data[1..3] the centre.
struct EmbeddedSphereShape {
    static constexpr uint32_t kVTable = 0xda1970;
    const void *mVTable;
    float mData[4];
    uint32_t mRefCount;

    EmbeddedSphereShape();
};

// The embedded cone (spot light, +0x80): data[0] the base radius, data[1] the height, data[2..4] the position (the apex
// sits at -range / 2 along z, the base half a range in front).
struct EmbeddedConeShape {
    static constexpr uint32_t kVTable = 0xda1b00;
    const void *mVTable;
    float mData[5];
    uint32_t mRefCount;

    EmbeddedConeShape();

    // The cone for a range and the two angles (0xa48540's and 0xa4a370's tail): height range / 2, radius
    // tan((falloffAngle + coneAngle) / 2) * range, the position (0, 0, -range / 2).
    void Fit(float range, float falloffAngle, float coneAngle);
};

static_assert(sizeof(EmbeddedSphereShape) == 0x18, "the game's sphere shape is 0x18 bytes");
static_assert(sizeof(EmbeddedConeShape) == 0x1c, "the game's cone shape is 0x1c bytes");

// The setting "lightRadianceRangeCutoff" (string at 0xdb6090) of the configuration service (framework slot 5 with service
// 0xac1887e / iid 0x4bb0d269, the settings object's slot 9), 0.5 when it has none. The point and spot lights read it on
// every OnLightChanged. With no settings object the original calls through null (ORIGINAL-BUG style: so does this).
class RadianceCutoff {
public:
    RadianceCutoff();
    ~RadianceCutoff();
    RadianceCutoff(const RadianceCutoff &) = delete;
    RadianceCutoff &operator=(const RadianceCutoff &) = delete;
    float Value() const { return mValue; }

private:
    void *mSettings = nullptr; // held until the end of OnLightChanged (ScopedRef::ReleaseObject, as the original)
    float mValue = 0.5f;
};

// The reach of a light (the same in the point and spot lights): r = |colour * intensity| / cutoff; 0 when r <= 1, 10000
// for a falloff <= 0, else sqrt((r - 1) / falloff) (0 when that is not positive). NaN follows the compare instructions
// the original uses (comiss: unordered goes to the computing branch).
float LightRange(const float *colour, float intensity, float falloff, float cutoff);

// |colour|: the squares summed in float, the root taken in double and rounded back.
float LightColorLength(const float *colour);

} // namespace nGZSceneGraph
