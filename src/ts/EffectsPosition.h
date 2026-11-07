#pragma once
// The place an effect puts itself at (lane WP, rendering row E2: the sound's slot 6 0xc052d0 and the water's 0xc09080, the
// same arithmetic): `local`'s translation scaled by `parent`'s scale and turned by `parent`'s rotation, plus `parent`'s
// translation. The original's first operands are kept, which shows when two NaNs meet (EffectsTransform.h, FirstNaN):
// row x: x * m00, m01 * y, z * m02; row y: x * m10, m11 * y, m12 * z; row z: m20 * x, m21 * y, m22 * z.
#include "ts/EffectsTransform.h"

namespace fx {

inline void EffectPosition(const EffectsTransform *local, const EffectsTransform *parent, float out[3])
{
    const float scale = parent->scale;
    const float x = Mul(scale, local->translation.x);
    const float y = Mul(scale, local->translation.y);
    const float z = Mul(scale, local->translation.z);
    const auto &m = parent->m;
    out[0] = Add(Add(Add(Mul(x, m[0][0]), Mul(m[0][1], y)), Mul(z, m[0][2])), parent->translation.x);
    out[1] = Add(Add(Add(Mul(x, m[1][0]), Mul(m[1][1], y)), Mul(m[1][2], z)), parent->translation.y);
    out[2] = Add(Add(Add(Mul(m[2][0], x), Mul(m[2][1], y)), Mul(m[2][2], z)), parent->translation.z);
}

} // namespace fx
