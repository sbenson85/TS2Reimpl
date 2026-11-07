#include "simantics/SimGeometry.h"

#include "game/vtable.h"
#include "misc/X87.h"

#include <cmath>

namespace simgeom {

namespace {

constexpr float kCellScale = 1.0f / 65536.0f; // game constant 0xdc87b8
constexpr float kTileCentre = 0.5f;           // 0xd9c694
constexpr float kQuarterPi = 0.7853981852531433f; // 0xdb1dcc (pi / 4 as a float)
constexpr float kTwoPiFloat = 6.2831854820251465f; // 0xda20f0
constexpr double kTwoPiDouble = 6.2831854820251465; // 0xdbed40 (the float's value as a double)

using ObjectFloatGetter = float(__thiscall *)(void *);

} // namespace

void __cdecl RotateByQuaternion(float *v, const float *q)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float v0 = v[0], v1 = v[1], v2 = v[2];

    // Every product is doubled by adding it to itself.
    const float xx = x * x, wy = w * y, xy = y * x, xz = z * x, wx = w * x, yy = y * y, wz = w * z, zz = z * z, yz = z * y;
    const float xx2 = xx + xx, wy2 = wy + wy, xy2 = xy + xy, xz2 = xz + xz, wx2 = wx + wx, yy2 = yy + yy, wz2 = wz + wz,
                zz2 = zz + zz, yz2 = yz + yz;

    const float m00 = (1.0f - yy2) - zz2;
    const float m01 = xy2 - wz2;
    const float m02 = wy2 + xz2;
    v[0] = (m00 * v0 + m01 * v1) + m02 * v2;

    const float m10 = wz2 + xy2;
    const float m11 = (1.0f - xx2) - zz2;
    const float m12 = yz2 - wx2;
    v[1] = (m11 * v1 + m10 * v0) + m12 * v2;

    const float m20 = xz2 - wy2;
    const float m21 = yz2 + wx2;
    const float m22 = (1.0f - xx2) - yy2;
    v[2] = (m20 * v0 + m21 * v1) + m22 * v2;
}

float *__cdecl ReadObjectVector(float *out, void *object)
{
    if (!object) return out;
    out[0] = VirtualCall<ObjectFloatGetter, 4>(object);
    const float y = VirtualCall<ObjectFloatGetter, 5>(object);
    out[1] = -y;
    out[2] = VirtualCall<ObjectFloatGetter, 6>(object);
    return out;
}

float __cdecl SineOfFloat(float angle)
{
    return static_cast<float>(std::sin(static_cast<double>(angle)));
}

float __cdecl CosineOfFloat(float angle)
{
    return static_cast<float>(std::cos(static_cast<double>(angle)));
}

int32_t *__cdecl TileFromBytes(int32_t *out, const uint8_t *bytes)
{
    const int32_t third = static_cast<int8_t>(bytes[2]);
    const int32_t second = static_cast<int8_t>(bytes[1]);
    const int32_t first = static_cast<int8_t>(bytes[0]);
    out[0] = second;
    out[1] = first;
    out[2] = third - 1;
    return out;
}

float *__cdecl PositionToFloats(float *out, const int32_t *in, int32_t level)
{
    out[0] = static_cast<float>(in[0]) * kCellScale;
    reinterpret_cast<int32_t *>(out)[2] = level - 1;
    out[1] = static_cast<float>(in[1]) * kCellScale;
    return out;
}

float *__cdecl TileCentre(float *out, const int32_t *in)
{
    const float x = static_cast<float>(in[0]) + kTileCentre;
    const float y = static_cast<float>(in[1]) + kTileCentre;
    reinterpret_cast<int32_t *>(out)[2] = in[2];
    out[1] = y;
    out[0] = x;
    return out;
}

int32_t __cdecl FacingToNotchOffset(int32_t facing)
{
    return (10 - static_cast<int16_t>(facing)) % 8;
}

float __cdecl NotchAngle(int32_t notch)
{
    return static_cast<float>(static_cast<int16_t>(2 - notch)) * kQuarterPi;
}

float *__cdecl NotchQuaternion(float *out, int32_t notch)
{
    // The axis (0, 0, 1) is normalised first: sqrt(1) in float, then 1 / that, times 0 for the x and y components.
    const float inverseLength = 1.0f / static_cast<float>(std::sqrt(1.0));
    const float axisXY = inverseLength * 0.0f;
    const float axisZ = inverseLength;

    // The sum is an x87 fadd on NotchAngle's returned value, so it rounds at the thread's precision (exact at 53-bit,
    // to 24 bits under D3D9's 24-bit; src/misc/X87.h). The fmod after it is fprem, exact at any precision.
    const double angle = x87::Rounded(static_cast<double>(NotchAngle(notch)) + static_cast<double>(kTwoPiFloat));
    const double reduced = std::fmod(angle, kTwoPiDouble);
    const double half = static_cast<double>(static_cast<float>(reduced) * 0.5f);
    const float s = static_cast<float>(std::sin(half));
    out[2] = axisZ * s;
    out[0] = s * axisXY;
    out[1] = s * axisXY;
    out[3] = static_cast<float>(std::cos(half));
    return out;
}

} // namespace simgeom
