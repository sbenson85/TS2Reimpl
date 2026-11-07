// How a meta-particle is drawn (lane XE, rendering row E9): the three orthonormal bases (0xbdce90, 0xbdcfd0, 0xbdd110), the
// rotation about z (0xc017e0), the particle's place (0xc018f0) and the draw tick that puts every live particle's effect there
// (0xc03ea0). Declared in src/ts/cTSMetaParticlesEffect.h. Scalar SSE single with the first-operand NaN rule of fx::Add /
// fx::Mul; operand orders and comparisons follow the assembler.
#include "ts/cTSMetaParticlesEffect.h"

#include "compgeom/ConvexMath.h"
#include "compgeom/Rigid.h"
#include "game/game.h"
#include "game/vtable.h"
#include "ts/ParticleCurve.h"
#include "ts/ParticleMath.h"

#include <bit>
#include <cmath>
#include <cstdint>

using fx::Add;
using fx::CosF;
using fx::Div;
using fx::EffectsTransform;
using fx::Mul;
using fx::RowFromY;
using fx::SinF;
using fx::SqrtF;
using fx::Sub;
using fx::Vector3;

namespace {

constexpr float kTwoPi = 6.2831855f;

float Length2(float a, float b) { return SqrtF(Add(Add(Mul(a, a), Mul(b, b)), 0.0f)); }

float Abs(float v) { return std::bit_cast<float>(std::bit_cast<uint32_t>(v) & 0x7fffffffu); }

// 0xc017e0's arithmetic, on the matrix of `t`.
void TurnAboutZ(EffectsTransform *t, float angle)
{
    const float s = SinF(angle);
    const float c = CosF(angle);
    const float ns = -s;
    float(&m)[3][3] = t->m;
    const float m00 = m[0][0], m01 = m[0][1], m10 = m[1][0], m11 = m[1][1], m20 = m[2][0], m21 = m[2][1];
    m[0][0] = Add(Mul(m01, s), Mul(m00, c));
    m[1][0] = Add(Mul(m11, s), Mul(m10, c));
    m[2][0] = Add(Mul(m21, s), Mul(m20, c));
    m[0][1] = Add(Mul(m01, c), Mul(m00, ns));
    m[1][1] = Add(Mul(m11, c), Mul(m10, ns));
    m[2][1] = Add(Mul(m21, c), Mul(m20, ns));
    t->flags |= EffectsTransform::kRotated;
    ++t->Version();
}

float Min1(float v) { return v < 1.0f ? v : 1.0f; }

bool EffectRunning(void *effect) { return VirtualCall<bool(__thiscall *)(void *), 5>(effect); }

} // namespace

namespace fx {

// 0xbdce90.
void __cdecl MetaBasisA(const float *in, float *out)
{
    const float x = in[0], y = in[1], z = in[2];
    out[0] = x;
    out[3] = y;
    out[6] = z;
    const float a = -y;
    const float len = Length2(a, x);
    if (len > 0.0f) {
        out[4] = Div(x, len);
        out[1] = Div(a, len);
        out[7] = Div(-0.0f, len);
        const float c1x = out[1], c1y = out[4], c1z = out[7];
        out[8] = Sub(Mul(x, c1y), Mul(y, c1x));
        out[2] = Sub(Mul(y, c1z), Mul(z, c1y));
        out[5] = Sub(Mul(z, c1x), Mul(x, c1z));
    } else {
        out[1] = 1.0f;
        out[4] = 0.0f;
        out[7] = 0.0f;
        out[2] = 0.0f;
        out[5] = 1.0f;
        out[8] = 0.0f;
    }
}

// 0xbdcfd0.
void __cdecl MetaBasisB(const float *in, float *out)
{
    const float x = in[0], y = in[1], z = in[2];
    out[1] = x;
    out[4] = y;
    out[7] = z;
    const float a = -x;
    const float len = Length2(a, y);
    if (len > 0.0f) {
        out[0] = Div(y, len);
        out[3] = Div(a, len);
        out[6] = Div(0.0f, len);
        const float c0x = out[0], c0y = out[3], c0z = out[6];
        out[8] = Sub(Mul(y, c0x), Mul(x, c0y));
        out[2] = Sub(Mul(z, c0y), Mul(y, c0z));
        out[5] = Sub(Mul(x, c0z), Mul(z, c0x));
    } else {
        out[0] = 0.0f;
        out[3] = 1.0f;
        out[6] = 0.0f;
        out[2] = 1.0f;
        out[5] = 0.0f;
        out[8] = 0.0f;
    }
}

// 0xbdd110: no test for a zero length (the divides stay).
void __cdecl MetaBasisC(const float *in, float *out)
{
    const float x = in[0], y = in[1], z = in[2];
    out[2] = x;
    out[5] = y;
    out[8] = z;
    const float base = Add(Mul(z, z), 0.0f);
    if (Abs(x) > Abs(y)) {
        const float a = -x;
        const float len = SqrtF(Add(base, Mul(a, a)));
        const float L = Div(a, len), N = Div(-0.0f, len), P = Div(z, len);
        out[1] = Sub(Mul(L, y), Mul(z, N));
        out[4] = Sub(Mul(z, P), Mul(L, x));
        out[7] = Sub(Mul(N, x), Mul(P, y));
        out[0] = P;
        out[3] = N;
        out[6] = L;
    } else {
        const float a = -y;
        const float len = SqrtF(Add(base, Mul(a, a)));
        const float L = Div(a, len), P = Div(z, len), N = Div(0.0f, len);
        out[0] = Sub(Mul(z, P), Mul(L, y));
        out[3] = Sub(Mul(L, x), Mul(z, N));
        out[6] = Sub(Mul(N, y), Mul(P, x));
        out[1] = N;
        out[4] = P;
        out[7] = L;
    }
}

// 0xc017e0.
void __fastcall MetaRotateZ(EffectsTransform *transform, void *, float angle) { TurnAboutZ(transform, angle); }

} // namespace fx

// 0xc018f0.
void cTSMetaParticlesEffect::BuildParticlePlace(cTSMetaParticle *p, float t, float scale, Vector3 *pos, EffectsTransform *out)
{
    const cTSMetaParticlesDescription *const d = mDescription;
    EffectsTransform T;
    T.pad[0] = 0; // ORIGINAL-BUG: the original leaves this byte as it was on the stack
    T.flags = 0;
    T.pad[1] = 0;
    T.pad[2] = 0;
    T.m[0][0] = 1.0f; T.m[0][1] = 0.0f; T.m[0][2] = 0.0f;
    T.m[1][0] = 0.0f; T.m[1][1] = 1.0f; T.m[1][2] = 0.0f;
    T.m[2][0] = 0.0f; T.m[2][1] = 0.0f; T.m[2][2] = 1.0f;
    T.Version() = 1;
    T.scale = scale;
    T.translation = {0.0f, 0.0f, 0.0f};

    const uint32_t mode = d->mAlignment;
    if (mode - 2u <= 2u) {
        Vector3 v = p->mVelocity;
        const float len = SqrtF(Add(Add(Mul(v.x, v.x), Mul(v.y, v.y)), Mul(v.z, v.z)));
        if (len > 0.0f) {
            v.x = Div(v.x, len);
            v.y = Div(v.y, len);
            v.z = Div(v.z, len);
        }
        float B[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        const float in[3] = {v.x, v.y, v.z};
        if (mode == 2)
            fx::MetaBasisA(in, B);
        else if (mode == 3)
            fx::MetaBasisB(in, B);
        else
            fx::MetaBasisC(in, B);
        if (d->mDamp > 0.0f) {
            float h = Mul(static_cast<float>(std::atan2(static_cast<double>(B[3]), static_cast<double>(B[0]))), 0.15915494f);
            const float H = p->mHeading;
            if (!(H == -999.0f)) {
                float H2 = H;
                if (Sub(H, 0.5f) > h)
                    H2 = Sub(H, 1.0f);
                else if (h > Add(H, 0.5f))
                    H2 = Add(H, 1.0f);
                h = Add(Mul(Sub(h, H2), Div(0.06666667f, d->mDamp)), H2);
                if (-0.5f > h)
                    h = Add(h, 1.0f);
                else if (h > 0.5f)
                    h = Sub(h, 1.0f);
            }
            TurnAboutZ(&T, Mul(h, kTwoPi));
            p->mHeading = h;
        } else {
            for (int i = 0; i < 9; ++i) T.m[i / 3][i % 3] = B[i];
            T.flags |= EffectsTransform::kRotated;
            ++T.Version();
        }
        if (!(d->mBank.x == 0.0f)) TurnAboutZ(&T, p->mBank);
    } else {
        const float angle = Mul(p->mHeading, kTwoPi);
        const float s = SinF(angle);
        const float c = CosF(angle);
        T.flags = EffectsTransform::kRotated;
        T.Version() = 2;
        T.m[0][0] = Add(Mul(s, 0.0f), c);
        T.m[2][0] = Add(Mul(s, 0.0f), Mul(c, 0.0f));
        T.m[1][0] = Add(s, Mul(c, 0.0f));
        T.m[0][1] = Add(-s, Mul(c, 0.0f));
        T.m[1][1] = Add(Mul(-s, 0.0f), c);
        T.m[2][1] = Add(Mul(-s, 0.0f), Mul(c, 0.0f));
    }

    {
        const float r = fx::EvaluateCurve(d->mRotateCurve, t);
        const float angle = Mul(Mul(r, p->mSpin), kTwoPi);
        const float s = SinF(angle);
        const float c = CosF(angle);
        const float ns = -s;
        float(&m)[3][3] = T.m;
        const float m00 = m[0][0], m01 = m[0][1], m10 = m[1][0], m11 = m[1][1], m20 = m[2][0], m21 = m[2][1];
        m[0][0] = Add(Mul(m01, s), Mul(m00, c));
        m[1][0] = Add(Mul(m11, s), Mul(m10, c));
        m[2][0] = Add(Mul(m21, s), Mul(m20, c));
        m[0][1] = Add(Mul(m01, c), Mul(m00, ns));
        m[1][1] = Add(Mul(m11, c), Mul(m10, ns));
        m[2][1] = Add(Mul(m20, ns), Mul(m21, c));
        T.flags |= EffectsTransform::kRotated;
        ++T.Version();
    }

    if (!d->mWarps.empty()) {
        const Vector3 from = {Sub(pos->x, mLocal.translation.x), Sub(pos->y, mLocal.translation.y), Sub(pos->z, mLocal.translation.z)};
        const float phaseBase = static_cast<float>(static_cast<double>(d->mRateVary) * mTime);
        for (const cTSParticleWarp &warp : d->mWarps) {
            const double dot = nGZCompGeom::Dot87(&from.x, &warp.mFirst.x);
            const float phase = Mul(phaseBase, warp.mValue);
            const float s = static_cast<float>(std::sin(nGZCompGeom::convex::Add87(dot, static_cast<double>(phase))));
            pos->x = Add(Mul(warp.mSecond.x, s), pos->x);
            pos->y = Add(Mul(warp.mSecond.y, s), pos->y);
            pos->z = Add(Mul(warp.mSecond.z, s), pos->z);
        }
    }

    T.translation = *pos;
    T.flags |= EffectsTransform::kTranslated;
    ++T.Version();
    *out = T;
}

// 0xc03ea0.
void cTSMetaParticlesEffect::TickParticles()
{
    const cTSMetaParticlesDescription *const d = mDescription;
    constexpr float k1 = 0.003921569f, k3 = 16.0f;
    // ORIGINAL-BUG: the original leaves these unset when neither bloom byte is used (and never reads them then)
    float A = 0.0f, B = 0.0f, C = 0.0f, D = 0.0f;
    if (d->mBloom[0] | d->mBloom[2]) {
        A = Mul(static_cast<float>(d->mBloom[1]), k1);
        B = Mul(Mul(static_cast<float>(d->mBloom[0]), k1), k3);
        C = Mul(static_cast<float>(d->mBloom[3]), k1);
        D = Mul(Mul(static_cast<float>(d->mBloom[2]), k1), k3);
    }
    // ORIGINAL-BUG: with no colours the original looks up the key of -t and reads far outside the vector; we draw nothing
    if (d->mColors.empty()) return;
    const int32_t count1 = static_cast<int32_t>(d->mColors.size() - 1);

    for (cTSMetaParticle *const p : mParticles) {
        Vector3 P = p->mPosition;
        if (!(d->mScrew == 0.0f)) {
            const Vector3 rel = {Sub(P.x, mLocal.translation.x), Sub(P.y, mLocal.translation.y), Sub(P.z, mLocal.translation.z)};
            const float turn = Mul(rel.z, d->mScrew);
            const float s = SinF(turn);
            const float c = CosF(turn);
            // ORIGINAL-BUG: the turned offset is added to the particle's old position, not to the centre
            P.x = Add(Sub(Mul(rel.x, c), Mul(rel.y, s)), P.x);
            P.y = Add(Add(Mul(rel.y, c), Mul(rel.x, s)), P.y);
        }
        const float t = Div(p->mAge, p->mLife);
        const float sizeScale = Mul(fx::EvaluateCurve(d->mSize, t), p->mSizeVary);
        EffectsTransform X;
        BuildParticlePlace(p, t, sizeScale, &P, &X);
        if (!p->mEffect.Get() || !EffectRunning(p->mEffect.Get())) continue;

        Vector3 c;
        if (count1 == 0) {
            c = {d->mColors[0].r, d->mColors[0].g, d->mColors[0].b};
        } else {
            const fx::KeyPosition key = fx::FindKey(Mul(fx::FromUnsigned(static_cast<uint32_t>(count1)), t));
            const fxdesc::Color *const colors = d->mColors.data(); // ORIGINAL-BUG: no range check on the key
            if (key.mFraction > 0.0f) {
                const fxdesc::Color &cur = colors[key.mIndex], &next = colors[key.mIndex + 1];
                c.x = Add(Mul(Sub(next.r, cur.r), key.mFraction), cur.r);
                c.y = Add(Mul(Sub(next.g, cur.g), key.mFraction), cur.g);
                c.z = Add(Mul(Sub(next.b, cur.b), key.mFraction), cur.b);
            } else {
                c = {colors[key.mIndex].r, colors[key.mIndex].g, colors[key.mIndex].b};
            }
        }
        Vector3 color = {Mul(p->mColor.x, c.x), Mul(c.y, p->mColor.y), Mul(c.z, p->mColor.z)};
        float alpha = Mul(Mul(fx::EvaluateCurve(d->mAspect, t), p->mAlpha), mEvent3);

        if (d->mBloom[2] | d->mBloom[0]) {
            Vector3 P2 = X.translation;
            if (mWorld.flags & EffectsTransform::kRotated) {
                const float(&w)[3][3] = mWorld.m;
                const Vector3 q = P2;
                P2 = {RowFromY(q.x, q.y, q.z, w[0][0], w[0][1], w[0][2]), RowFromY(q.x, q.y, q.z, w[1][0], w[1][1], w[1][2]),
                      RowFromY(q.x, q.y, q.z, w[2][0], w[2][1], w[2][2])};
            }
            P2 = {Add(Mul(P2.x, mWorld.scale), mWorld.translation.x), Add(Mul(P2.y, mWorld.scale), mWorld.translation.y),
                  Add(Mul(P2.z, mWorld.scale), mWorld.translation.z)};
            void *const world = VirtualCall<void *(__thiscall *)(void *), 43>(mOwner.Get());
            float O[3] = {0.0f, 0.0f, 0.0f};
            VirtualCall<void(__thiscall *)(void *, float *, const Vector3 *, uint32_t), 143>(world, O, &P2, 0);
            X.flags |= 1;
            ++X.Version();
            const float ax = Abs(O[0]);
            const float ay = Abs(O[1]);
            const float m = ay > ax ? ay : ax;
            const float f = Sub(1.0f, m);
            if (f > 0.0f) {
                X.scale = Mul(Min1(Add(Mul(f, D), C)), X.scale);
                alpha = Mul(Min1(Add(Mul(f, B), A)), alpha);
            } else {
                alpha = Mul(alpha, A);
                X.scale = Mul(X.scale, C);
            }
        }

        void *const effect = p->mEffect.Get();
        VirtualCall<void(__thiscall *)(void *, uint32_t, void *, uint32_t), 13>(effect, 3, &alpha, 1);
        VirtualCall<void(__thiscall *)(void *, uint32_t, void *, uint32_t), 12>(effect, 4, &color, 1);
        VirtualCall<void(__thiscall *)(void *, const EffectsTransform *), 9>(effect, &mWorld);
        VirtualCall<void(__thiscall *)(void *, const EffectsTransform *), 10>(effect, &X);
    }
}
