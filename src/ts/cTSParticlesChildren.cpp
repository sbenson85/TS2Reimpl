// cTSParticlesRenderable::UpdateParticleChildren 0xbc1d10 (lane XB, rendering row E6): with a shape, each particle has a
// child node (AddParticleChild), and each frame the particles' places, colours and sizes go to those children: a transform
// by slot 63, then up to seven shader parameters as events. Walks the children and the particles together.
#include "ts/cTSParticlesRenderable.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "misc/Quaternion.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cIndexedMeshBuilder.h"
#include "simantics/SimGeometry.h"
#include "ts/ParticleCurve.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace nGZSceneGraph {

namespace {

using fx::Add;
using fx::Div;
using fx::Mul;
using fx::Sub;
using math::Quaternion;
using nGZCompGeom::convex::Add87;
using nGZCompGeom::convex::Mul87;
using nGZCompGeom::convex::Stored;

constexpr float kAxisX[3] = {1.0f, 0.0f, 0.0f};
constexpr float kAxisY[3] = {0.0f, 1.0f, 0.0f};
constexpr float kAxisZ[3] = {0.0f, 0.0f, 1.0f};
// Below this a square length (or, for the stopped-particle test, a component) counts as zero.
constexpr double kNearZero = 1e-6;

// The shader parameters' events.
constexpr uint32_t kEventScalar = 0x09f5fdb0;
constexpr uint32_t kEventVector3 = 0x09f5fdbc;
constexpr uint32_t kEventVector4 = 0x09f5fdc0;
// The vtables of the event objects a child's SetAttribute is handed; it reads the floats after the first word.
constexpr uint32_t kScalarEventVTable = 0xdb07f4;  // cScalarEvent<float>
constexpr uint32_t kVector3EventVTable = 0xdb5e70; // cVectorEvent<cVector<3>>
constexpr uint32_t kVector4EventVTable = 0xdb07d4; // cVector4fEvent

struct cParticleEvent {
    const void *mVTable;
    float mValues[4];
};

// The description's mAlignment (+0xec): how a particle's orientation follows its motion. Each of the others turns a model
// axis on to the direction flattened into the z = 0 plane; the first three then turn on to the whole direction.
enum : uint8_t {
    kAlignFree = 0, // the description's own rotations
    kAlignTiltX = 2,
    kAlignTiltY = 3,
    kAlignTiltYBack = 4,
    kAlignTurnX = 7,
    kAlignTurnY = 8,
    kAlignTurnYBack = 9,
};

void Normalised(float *out, const float *v)
{
    const float lengthSquared = Add(Add(Mul(v[0], v[0]), Mul(v[1], v[1])), Mul(v[2], v[2]));
    if (static_cast<double>(lengthSquared) > kNearZero) {
        const float length = fx::SqrtF(lengthSquared);
        out[0] = Div(v[0], length);
        out[1] = Div(v[1], length);
        out[2] = Div(v[2], length);
    } else {
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
    }
}

// `start` on to `heading`, then what that made of `next` on to the whole direction.
void TurnThenTilt(Quaternion *q, const float *start, const float *heading, const float *next, const float *full)
{
    q->SetRotationBetween(start, heading);
    float turned[3] = {next[0], next[1], next[2]};
    simgeom::RotateByQuaternion(turned, &q->x);
    float direction[3];
    Normalised(direction, full);
    Quaternion tilt;
    tilt.SetRotationBetween(turned, direction);
    q->PreMultiplyNormalised(&tilt);
}

// The description's rotation `which` of a particle: the curve times the particle's `amount`, plus the offset.
float RotationAngle(const cTSParticlesDescription *d, int which, float t, float amount)
{
    return Stored(Add87(Mul87(fx::EvaluateCurve(d->mRotateCurve[which], t), amount), static_cast<double>(d->mRotateOffset[which])));
}

float Between(float current, float next, float fraction)
{
    return Add(Mul(Sub(next, current), fraction), current);
}

} // namespace

void cTSParticlesRenderable::UpdateParticleChildren(cTSParticlesDescription *d)
{
    using nGZCompGeom::RigidTransform3;

    const cRigidTransform worldTransform = GetWorldTransform();
    RigidTransform3 inverse;
    std::memcpy(&inverse, &worldTransform, sizeof(inverse));
    inverse.Invert();

    const cChildRange range = GetChildren();
    cICompositionTreeNode *child = range.mCurrent;
    cTSParticlesEffect *const effect = mEffect;
    auto particle = effect->mParticles.begin();

    for (; child != range.mEnd; child = child->GetNextSibling(), ++particle) {
        if (particle == effect->mParticles.end()) break;
        const cTSParticle *const p = *particle;
        const float t = Div(p->mAge, p->mLife);
        const uint8_t mode = d->mAlignment;
        Quaternion q{0.0f, 0.0f, 0.0f, 1.0f};

        if (mode == kAlignFree) {
            const uint8_t count = d->mRotateCount;
            if (count != 0) {
                const float angle = RotationAngle(d, 0, t, p->mRotation);
                const fxdesc::Vector3 &a = d->mRotateAxis[0];
                const float inverseLength = Div(1.0f, fx::SqrtF(Add(Add(Mul(a.y, a.y), Mul(a.x, a.x)), Mul(a.z, a.z))));
                const float half = Mul(angle, 0.5f);
                const float s = fx::SinF(half);
                q.x = Mul(Mul(inverseLength, a.x), s);
                q.y = Mul(Mul(inverseLength, a.y), s);
                q.z = Mul(Mul(inverseLength, a.z), s);
                q.w = fx::CosF(half);
                if (count == 2) {
                    const float secondAngle = RotationAngle(d, 1, t, p->mRotationSpeed);
                    const fxdesc::Vector3 &b = d->mRotateAxis[1];
                    const float secondInverse = Div(1.0f, fx::SqrtF(Add(Add(Mul(b.x, b.x), Mul(b.y, b.y)), Mul(b.z, b.z))));
                    const float secondHalf = Mul(secondAngle, 0.5f);
                    const float secondSin = fx::SinF(secondHalf);
                    Quaternion second;
                    second.x = Mul(Mul(b.x, secondInverse), secondSin);
                    second.y = Mul(Mul(b.y, secondInverse), secondSin);
                    second.z = Mul(Mul(b.z, secondInverse), secondSin);
                    second.w = fx::CosF(secondHalf);
                    q.PreMultiplyNormalised(&second);
                }
            }
        } else {
            // With the flag, a particle that has stopped follows its axis instead of its velocity.
            const fx::Vector3 *direction = &p->mVelocity;
            if ((d->mFlags & (uint64_t(0x20) << 32)) != 0 && kNearZero > static_cast<double>(std::fabs(direction->x)) &&
                kNearZero > static_cast<double>(std::fabs(direction->y)) &&
                kNearZero > static_cast<double>(std::fabs(direction->z))) {
                direction = &p->mAxis;
            }
            const float flatSquared = Add(Add(Mul(direction->x, direction->x), Mul(direction->y, direction->y)), 0.0f);
            float flat[3] = {direction->x, direction->y, 0.0f};
            if (static_cast<double>(flatSquared) > kNearZero) {
                const float length = fx::SqrtF(flatSquared);
                flat[0] = Div(direction->x, length);
                flat[1] = Div(direction->y, length);
                flat[2] = Div(0.0f, length);
            }
            const float full[3] = {direction->x, direction->y, direction->z};
            const float flatBack[3] = {-flat[0], -flat[1], -flat[2]};

            switch (mode) {
            case kAlignTiltX: TurnThenTilt(&q, kAxisX, flat, kAxisX, full); break;
            // The original tests a byte here (0x107d178): it is 1 in the image and nothing writes it, so always the tilt.
            case kAlignTiltY: TurnThenTilt(&q, kAxisY, flat, kAxisY, full); break;
            case kAlignTiltYBack: TurnThenTilt(&q, kAxisY, flatBack, kAxisZ, full); break;
            case kAlignTurnX: q.SetRotationBetween(kAxisX, flat); break;
            case kAlignTurnY: q.SetRotationBetween(kAxisY, flat); break;
            case kAlignTurnYBack: q.SetRotationBetween(kAxisY, flatBack); break;
            // ORIGINAL-BUG: the kinds without a case (1, 5, 6, above 9) leave at the first particle, the drawn count unbumped.
            default: return;
            }
        }

        // The particle in the effect's world, then in this node's space.
        RigidTransform3 placed;
        std::memcpy(&placed.translation, &p->mPosition, sizeof(placed.translation));
        std::memcpy(placed.rotation, &q, sizeof(placed.rotation));
        RigidTransform3 world;
        std::memcpy(&world.translation, &effect->mWorld.translation, sizeof(world.translation));
        indexedmesh::QuaternionFromMatrix(world.rotation, &effect->mWorld.m[0][0]);
        placed.Compose(&world);
        placed.Compose(&inverse);
        static_cast<cITransformNode *>(child)->SetTransform(reinterpret_cast<const cRigidTransform *>(&placed));

        // The colour along the description's key list. The list always has a key; an age past the life reads past it.
        const fxdesc::Color *const colors = d->mColors.data();
        const int32_t last = static_cast<int32_t>(d->mColors.size()) - 1;
        fxdesc::Color c = colors[0];
        if (last != 0) {
            const fx::KeyPosition key = fx::FindKey(Mul(fx::FromUnsigned(static_cast<uint32_t>(last)), t));
            const fxdesc::Color &from = colors[key.mIndex];
            if (key.mFraction > 0.0f) {
                const fxdesc::Color &to = colors[key.mIndex + 1];
                c = {Between(from.r, to.r, key.mFraction), Between(from.g, to.g, key.mFraction), Between(from.b, to.b, key.mFraction)};
            } else {
                c = from;
            }
        }
        const float tint[3] = {Mul(p->mColor.x, c.r), Mul(c.g, p->mColor.y), Mul(c.b, p->mColor.z)};
        const float colour[3] = {Mul(tint[0], effect->mColorScale.x), Mul(tint[1], effect->mColorScale.y),
                                 Mul(tint[2], effect->mColorScale.z)};
        const float alpha = Mul(Mul(fx::EvaluateCurve(d->mAspect, t), p->mAlpha), effect->mAlphaScale);
        const float size = Mul(Mul(Mul(fx::EvaluateCurve(d->mSize, t), p->mSize), effect->mSizeScale), effect->mWorld.scale);
        // A NaN size passes both tests as it is.
        const float atLeast = -1.0f > size ? -1.0f : size;
        const float clamped = 1.0f < atLeast ? 1.0f : atLeast;

        // A parameter goes only where the description names one and the child's shader has that many.
        const auto wanted = [&](int parameter, uint32_t id) {
            if (static_cast<int32_t>(mParams[parameter]) < 0) return false;
            const nGZEvent::cEventAttribute info = child->GetEventAttribute(id);
            return mParams[parameter] < info.mWord08;
        };
        const auto send = [&](int parameter, uint32_t id, uint32_t vtable, float v0, float v1, float v2, float v3) {
            cParticleEvent event{GameAddress<const void>(vtable), {v0, v1, v2, v3}};
            child->SetAttribute(id, mParams[parameter], static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&event)));
        };
        if (wanted(0, kEventVector3)) send(0, kEventVector3, kVector3EventVTable, colour[0], colour[1], colour[2], 0.0f);
        if (wanted(1, kEventScalar)) send(1, kEventScalar, kScalarEventVTable, alpha, 0.0f, 0.0f, 0.0f);
        if (wanted(2, kEventScalar)) send(2, kEventScalar, kScalarEventVTable, clamped, 0.0f, 0.0f, 0.0f);
        if (wanted(3, kEventVector4)) send(3, kEventVector4, kVector4EventVTable, colour[0], colour[1], colour[2], alpha);
        if (wanted(4, kEventVector4)) send(4, kEventVector4, kVector4EventVTable, clamped, clamped, clamped, 1.0f);
        if (wanted(5, kEventVector4)) {
            const fx::Vector3 &v = p->mVelocity;
            send(5, kEventVector4, kVector4EventVTable, v.x, v.y, v.z, fx::SqrtF(Add(Add(Mul(v.y, v.y), Mul(v.x, v.x)), Mul(v.z, v.z))));
        }
        if (wanted(6, kEventVector4)) {
            send(6, kEventVector4, kVector4EventVTable, t, static_cast<float>(effect->mEmitClock),
                 Stored(Mul87(fx::EvaluateCurve(d->mAlpha, t), p->mAspect)), d->mFrameSpeed);
        }
    }
    ++effect->mFramesDrawn;
}

} // namespace nGZSceneGraph
