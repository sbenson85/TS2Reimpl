#include "ts/EffectsTransform.h"

namespace fx {

// 0x3a34b0.
void EffectsTransform::ToLocal(Vector3 *point) const
{
    Vector3 p = {Sub(point->x, translation.x), Sub(point->y, translation.y), Sub(point->z, translation.z)};
    if (scale != 1.0f) { // a NaN scale divides
        p.x = Div(p.x, scale);
        p.y = Div(p.y, scale);
        p.z = Div(p.z, scale);
    }
    if (flags & kRotated) {
        *point = {RowFromY(p.x, p.y, p.z, m[0][0], m[1][0], m[2][0]), RowFromY(p.x, p.y, p.z, m[0][1], m[1][1], m[2][1]),
                  RowFromY(p.x, p.y, p.z, m[0][2], m[1][2], m[2][2])};
        return;
    }
    *point = p;
}

// 0xbc0f80.
void EffectsTransform::Rotate(Vector3 *vector) const
{
    if (!(flags & kRotated)) return;
    const Vector3 in = *vector;
    *vector = {RowFromY(in.x, in.y, in.z, m[0][0], m[0][1], m[0][2]), RowFromY(in.x, in.y, in.z, m[1][0], m[1][1], m[1][2]),
               RowFromY(in.x, in.y, in.z, m[2][0], m[2][1], m[2][2])};
}

// 0xbbc9b0.
void EffectsTransform::Reset()
{
    m[0][0] = 1.0f;
    m[0][1] = m[0][2] = m[1][0] = 0.0f;
    m[1][1] = 1.0f;
    m[1][2] = m[2][0] = m[2][1] = 0.0f;
    m[2][2] = 1.0f;
    scale = 1.0f;
    translation = {0.0f, 0.0f, 0.0f};
    flags = 0;
    Version() = 0;
}

// 0xbc1010.
void EffectsTransform::Apply(Vector3 *point) const
{
    Rotate(point);
    const Vector3 scaled = {Mul(scale, point->x), Mul(scale, point->y), Mul(scale, point->z)};
    *point = {Add(scaled.x, translation.x), Add(scaled.y, translation.y), Add(scaled.z, translation.z)};
}

} // namespace fx
