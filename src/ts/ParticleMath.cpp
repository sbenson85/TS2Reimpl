// The particle code's free helpers (lane WV, rendering row E5): src/ts/ParticleMath.h.
#include "ts/ParticleMath.h"

#include <cmath>

namespace fx {

namespace {
// Twice a random less one, in double, then narrowed.
inline float RandomSigned()
{
    const double u = GameRandom().RandomDoubleUniform();
    return static_cast<float>(u + u - 1.0);
}
} // namespace

// 0xbbcd80.
Vector3 *__cdecl RandomInBox(Vector3 *out, const float *box)
{
    out->x = RandomIn(box[0], box[3]);
    out->y = RandomIn(box[1], box[4]);
    out->z = RandomIn(box[2], box[5]);
    return out;
}

// 0xbc2f80.
Vector3 *__cdecl Normalize(Vector3 *out, const Vector3 *in)
{
    const float lengthSquared = in->x * in->x + in->y * in->y + in->z * in->z;
    if (static_cast<double>(lengthSquared) > 1e-6) {
        const float length = static_cast<float>(std::sqrt(static_cast<double>(lengthSquared)));
        const Vector3 v = *in;
        *out = {v.x / length, v.y / length, v.z / length};
        return out;
    }
    *out = *in;
    return out;
}

// 0xbdd2f0.
Vector3 *__cdecl RandomInEllipsoid(Vector3 *out, const float *box)
{
    float x, y, z;
    do {
        x = RandomSigned();
        y = RandomSigned();
        z = RandomSigned();
    } while (!(1.0f >= y * y + x * x + z * z)); // a NaN loops on (jb)
    out->x = ((box[3] - box[0]) * x + (box[0] + box[3])) * 0.5f;
    out->y = ((box[4] - box[1]) * y + (box[1] + box[4])) * 0.5f;
    out->z = ((box[5] - box[2]) * z + (box[2] + box[5])) * 0.5f;
    return out;
}

// 0xbdd430.
Vector3 *__cdecl RandomOnRing(Vector3 *out, const float *box, float ratio)
{
    float x, y, z, radius;
    do {
        x = RandomSigned();
        y = RandomSigned();
        z = RandomSigned();
        radius = static_cast<float>(std::sqrt(static_cast<double>(y * y + x * x)));
        const float off = static_cast<float>(static_cast<double>(radius) - 0.5);
        const float room = static_cast<float>(0.25 - static_cast<double>(off * off));
        if (room >= z * z) break; // a NaN loops on (jb)
    } while (true);
    const float scale = (1.0f - ratio) / radius + ratio;
    out->x = Add(Mul(scale * x, box[3] - box[0]), box[0] + box[3]) * 0.5f;
    out->y = Add(Mul(scale * y, box[4] - box[1]), box[1] + box[4]) * 0.5f;
    out->z = Add(Mul(z * ratio, box[5] - box[2]), box[2] + box[5]) * 0.5f;
    return out;
}

} // namespace fx
