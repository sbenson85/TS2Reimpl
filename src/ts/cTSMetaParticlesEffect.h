#pragma once
// The meta-particles effect (rendering rows E7..E9): particles that are themselves effects, or plain moving points, each with
// its own random walk. What cTSMetaParticlesDescription's slot 1 makes. The game's RTTI: over cTSEffectBase, but the object
// repeats the base's words in its own places (the description at +0x1c), so the class derives from the two faces directly, as
// cTSParticlesEffect does. Vtables 0xe91284 (14 slots) and 0xe912c0 (the +4 face, 5). Region 0xbfd4a0..0xc05090, RVAs 1.18.0.187.
//
// All ours (rows E7..E9): the object, every slot, what the slots reach, the walks, the pool, the particle's first values (E8), the
// step (slot 5, 0xbfe210), the emitters and the draw tick (E9). Files: cTSMetaParticlesEffect.cpp (object, slots), ...Init,
// ...Motion, ...Surface (E8), ...Place (the bases, the particle's place, the draw tick), ...Emit (the step, the emitters).
#include "gz/cIGZUnknown.h"
#include "rz/RZRefCountBase.h"
#include "rz/cRZString.h"
#include "ts/CountedRef.h"
#include "ts/EffectsTransform.h"
#include "ts/cTSEffectBase.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>


// A random walk's running state (0x14 bytes): one each in the effect (the two the description has) and in a particle. Walks
// pick a new heading and pitch and two strengths each time their clock runs out.
struct cTSMetaWalk {
    float mClock;      // +0x00 seconds to the next change
    float mVertical;   // +0x04 the push along the pitch
    float mHorizontal; // +0x08 the push in the plane
    float mHeading;    // +0x0c radians
    float mPitch;      // +0x10 radians

    // 0xbfdb00 (ret 4): the walk's push added to `force`.
    void Apply(fx::Vector3 *force) const;
    // 0xbfffb0 (ret 0xc): heading and pitch from `direction`; `randomStart`: a random clock and strengths, otherwise the first Next.
    void Init(const cTSParticleWalk *walk, const fx::Vector3 *direction, bool randomStart);
    // 0xc03cb0 (ret 4): the clock moved on; new strengths; the angles turned by the description's two amounts.
    void Next(const cTSParticleWalk *walk);
};
static_assert(sizeof(cTSMetaWalk) == 0x14, "cTSMetaWalk size");

namespace fx {
// 0xc016f0 (cdecl): the heading and pitch of a direction. Straight up or down (x and y under 1e-6 by the squares): heading 0.
void __cdecl MetaDirectionAngles(const Vector3 *direction, float *heading, float *pitch);
// 0xbff400 (`this` an EffectsTransform, ret 4): a direction turned by the rotation transposed (the columns), in place, when the flag says so.
void __fastcall MetaRotateBack(const EffectsTransform *transform, void *, Vector3 *vector);
// 0xbdce90, 0xbdcfd0, 0xbdd110 (cdecl): a rotation (nine floats, row by row) with the direction as its column 0, 1 or 2.
void __cdecl MetaBasisA(const float *in, float *out);
void __cdecl MetaBasisB(const float *in, float *out);
void __cdecl MetaBasisC(const float *in, float *out);
// 0xc017e0 (`this` an EffectsTransform, ret 4): the rotation turned about z by `angle`; the flags and version updated.
void __fastcall MetaRotateZ(EffectsTransform *transform, void *, float angle);
// 0xbdd5e0 (cdecl): a random point of the box's ellipsoid by rejection from three randoms in the box's axes (not RandomInEllipsoid's
// unit ball scaled); a NaN box loops on. Returns `out`.
Vector3 *__cdecl MetaRandomInEllipsoid(Vector3 *out, const float *box);
} // namespace fx

// A meta-particle (0x74 bytes, from its own pool). The emitters fill it (0xc006f0).
struct cTSMetaParticle {
    uint8_t mAlive;        // +0x00
    uint8_t mPad1[3];
    float mAge;            // +0x04
    float mLife;           // +0x08
    fx::Vector3 mPosition; // +0x0c
    fx::Vector3 mVelocity; // +0x18
    float mSizeVary;       // +0x24
    float mWord28;         // +0x28
    float mSpin;           // +0x2c
    float mAlpha;          // +0x30
    fx::Vector3 mColor;    // +0x34
    float mBank;           // +0x40
    float mHeading;        // +0x44 -999 until the step sets it
    cTSMetaWalk mWalks[2]; // +0x48
    fx::CountedRef<cIGZUnknown> mEffect; // +0x70 the particle's effect, by the visual-effect face's slots

    cTSMetaParticle();     // 0xbfd4a0: all zero

    static void *__cdecl Allocate(size_t size); // 0xbfda30 cdecl, the size unused: from the free list, a new block when it is empty
    static void __cdecl Free(void *particle);   // 0xbfdab0 cdecl: back on the free list; null is ignored
};
static_assert(sizeof(cTSMetaParticle) == 0x74, "cTSMetaParticle size");
static_assert(offsetof(cTSMetaParticle, mWalks) == 0x48 && offsetof(cTSMetaParticle, mEffect) == 0x70, "cTSMetaParticle layout");

class cTSMetaParticlesEffect : public cITSComponentEffect, public rz::cRZRefCount {
public:
    static constexpr size_t kSize = 0x1f0;

    explicit cTSMetaParticlesEffect(cTSMetaParticlesDescription *description); // 0xbfd4f0
    ~cTSMetaParticlesEffect() override;                                        // 0xbfd840; deleting 0xbfdad0, +4 thunk 0xbfdac6
    static cITSComponentEffect *__cdecl Create(cTSMetaParticlesDescription *description); // 0xc03910: null on failure

    void Attach(cIGZUnknown *owner) override;   // 0xbffe20: once; the owner's objects
    void Detach() override;                     // 0xc02ea0
    void Start(uint32_t a) override;            // 0xc035f0
    void Stop(uint32_t a) override;             // 0xc03770: `a` nonzero stops at once
    bool IsRunning() override { return mRunning; } // shared 0x1b79e0
    void AddToScene(void *step, void *stats) override; // 0xbfe210 (ret 8): the step is a float in the first word, `stats` the world's counters
    void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) override; // 0xc02bb0
    bool HandleEvent(uint32_t kind, void *a, void *b) override; // 0xc020c0: `a` the values
    bool Query(void *a, void *b) override;      // 0xc02dd0: `a` 0: `b` a string, the name override; `a` 1: `b` kept for what's spawned
    void SetFlagged(bool value) override;       // 0xc02600: shown or not; particle effects and spawned ones hear the opposite
    void Slot10(uint32_t follower) override;    // 0xc02080: the effect dead particles go on to
    void Slot11(uint32_t description) override; // 0xc038f0: a new description, from an interface's slot 3
    uint32_t AddRef() override { return rz::cRZRefCount::AddRef(); }   // shared 0x1dab20
    uint32_t Release() override { return rz::cRZRefCount::Release(); } // shared 0x5b7980

    // ---- helpers the game's step still calls, hooked at their RVAs ----
    void Prepare();                                       // 0xc00160: what comes from the description and the owner's lookups
    void ChooseSourceQuadrant(uint32_t quadrant);         // 0xc02a80: a quarter of the surface as the source box
    void Aim(const fx::Vector3 *target);                  // 0xc026a0: event 5, the rotation and speed that reach `target`
    void SetEvent101(float a, float b);                   // 0xc02680
    void PruneSpawned();                                  // 0xbff510: the spawned effects still running kept, in order
    void RetireParticle(cTSMetaParticle *particle);       // 0xbff610: to the pool, with a death effect, or on to the follower
    void HandParticleOn(cTSMetaParticle *particle);       // 0xbffbd0: to the follower: `this` is the effect it came from
    cTSMetaParticle *NewParticle();                       // 0xbff4a0: from the pool, initialised
    void AddParticle(cTSMetaParticle *particle);          // 0xbfdc30
    void Emit(float step);                                // 0xbffb30: the delay, then the emitter the flags pick
    void EmitOnce(float step);                            // 0xc01590 (ret 4): up to the rate's count, once
    void EmitBurst(float step);                           // 0xc01310 (ret 4): a burst of the rate's count each period
    void EmitContinuous(float step);                      // 0xbfdcb0 (ret 4): at the rate, in pieces of at most 1/15 s
    // 0xc03ea0: every live particle's effect given its colour, alpha and place (and the bloom fade by the world's slot 143).
    void TickParticles();
    // 0xc018f0 (ret 0x14): the particle's place `out`: the alignment (heading, or the velocity's basis with the damped heading and
    // bank), the spin, the warps moving `*pos`, the scale; `t` is the particle's age over its life.
    void BuildParticlePlace(cTSMetaParticle *particle, float t, float scale, fx::Vector3 *pos, fx::EffectsTransform *out);
    void TickWalks(float step);                           // 0xc047e0: the effect's two walks
    void MarkFollowerSet() { mFollowerSet = true; }       // 0xc038e0
    // 0xbfe190 (ret 8): the sweep over the colliders for one particle's move `vector`, over its size curve and the scale; false: it dies.
    bool CollideColliders(cTSMetaParticle *particle, const fx::Vector3 *vector);
    // 0xbfed50 (ret 8): one steering step along the path (the effect's points, or the description's when it has none): the particle's position
    // and velocity bent by a cubic towards the segment's end over `step`; past the last point it flies straight on.
    void Steer(cTSMetaParticle *particle, float step);
    // 0xc02f30 (ret 8, the float unused): the particle's position and velocity at its age on the path; false once past its end.
    bool FollowPath(cTSMetaParticle *particle, float unused);
    // 0xc04840 (ret 8): one step of the particle's life: false once it is past its life. The path, the sub-stepped integrator (0xc04a00)
    // or the plain Euler step by the description's flags and drag.
    bool Integrate(cTSMetaParticle *particle, float step);
    // 0xc04a00 (ret 8): the same step in pieces of at most 1/15 s with the walks, attractors, colliders and the surface applied in each.
    bool IntegrateSubsteps(cTSMetaParticle *particle, float step);
    // 0xc03970 (ret 8): the effect named by the description's plus `suffix`, made by the owner and started at `place` (in the world) when shown.
    void SpawnSurfaceEffect(const char *suffix, const fx::Vector3 *place);
    // 0xbfe480 (ret 0xc): the surface under a particle (`where`, in the surface's space); the ceiling when the high flag says so, then terrain
    // repel (flag 0x100), or the bounce, the sticky or the plain landing; false: it dies.
    bool CollideSurface(cTSMetaParticle *particle, const fx::Vector3 *where, float step);
    // 0xbff8d0 (ret 4): a new particle put on the surface (its height from the description's flags and a random); false (and not alive): out of range.
    bool SnapToSurface(cTSMetaParticle *particle);
    // 0xc006f0 (ret 4): a new particle, every field, the game's randoms in the original's order: its life, the start (the path's first point, or a
    // random one in the source and a random velocity), the surface, the randoms of its looks, its own effect started, its walks.
    void InitParticle(cTSMetaParticle *particle);

    bool mAttached;                      // +0x0c
    bool mRunning;                       // +0x0d
    bool mStopping;                      // +0x0e no new particles, the live ones run out
    bool mVisible;                       // +0x0f 1 at first
    std::list<cTSMetaParticle *> mParticles; // +0x10
    int32_t mCount;                      // +0x18 the live ones
    const cTSMetaParticlesDescription *mDescription; // +0x1c not counted
    fx::CountedRef<cIGZUnknown> mOwner;  // +0x20
    const cTSParticlesSettings *mSettings; // +0x24 owner's slot 21, not counted
    fx::EffectsTransform mLocal;         // +0x28
    fx::EffectsTransform mWorld;         // +0x60
    double mTime;                        // +0x98
    bool mPrerollPending;                // +0xa0 the next step also runs the description's preroll
    double mEmitClock;                   // +0xa8
    int32_t mEmitted;                    // +0xb0
    double mEmitPeriod;                  // +0xb8 1 / rate
    float mEmitRemainder;                // +0xc0
    fx::Vector3 mForce;                  // +0xc4 the description's
    cTSMetaWalk mWalks[2];               // +0xd0
    bool mHasPreferDir;                  // +0xf8
    float mPreferHeading;                // +0xfc
    float mPreferPitch;                  // +0x100
    std::vector<cTSParticleAttractorState> mAttractors; // +0x104
    uint32_t mStartArgument;             // +0x110 the last Start's
    uint32_t mStopMode;                  // +0x114 the last Stop's argument (1 at first); nonzero: particles leave no death effect
    float mDelay;                        // +0x118 before the first emission
    fx::Vector3 mEmitterPosition;        // +0x11c in the world, from Update
    float mEvent101[2];                  // +0x128
    float mSpeedScale;                   // +0x130 event 0, Aim
    float mEvent3;                       // +0x134
    fx::Vector3 mEvent4;                 // +0x138
    float mRateScale;                    // +0x144 event 2
    float mLifeScale;                    // +0x148 event 1
    fx::CountedRef<cIGZUnknown> mSurface; // +0x14c owner's slot 24, then its slot 7
    float mSurfaceRange[2];              // +0x150 the surface's slot 3, its first two numbers taken off
    float mSurfaceBase;                  // +0x158 the surface's slot 7
    cTSParticleBox mSource;              // +0x15c the description's source box
    bool mFollowerSet;                   // +0x174 Slot10 was called on the effect handing on to this one
    bool mHasEffect;                     // +0x175 the owner knows the description's name: Start needs it
    bool mHasDeathEffect;                // +0x176
    bool mHasSurfaceEffect[2];           // +0x177 flag 0x40: the owner knows the two surface-hit names
    std::vector<cTSParticleColliderState> mColliders; // +0x17c
    bool mCollidersPlaced;               // +0x188 set by the step; Update and Prepare clear it
    std::vector<char> mColliderState;    // +0x18c the colliders' scratch
    bool mHasSpawnArgument;              // +0x198 Query 1 was called
    fx::CountedRef<cIGZUnknown> mSpawnArgument; // +0x19c what a spawned effect's slot 14 is handed
    float mFirstFrame;                   // +0x1a0 event 0xd
    cTSParticlesEffect::Text mNameOverride; // +0x1a4 Query 0
    float mWord1c4;                      // +0x1c4 1 from the constructor
    fx::CountedRef<cITSComponentEffect> mFollower; // +0x1c8 the effect dead particles go on to
    std::vector<cTSParticlePathPoint> mPath; // +0x1cc event 0xe: the points given, with velocities and times
    uint32_t mWord1d8;                   // +0x1d8
    std::vector<fx::CountedRef<cIGZUnknown>> mSpawned; // +0x1dc effects started and left to run out
    fx::CountedRef<cIGZUnknown> mNode;   // +0x1e8 the step makes it
    uint32_t mWord1ec;                   // +0x1ec never set
};
static_assert(offsetof(cTSMetaParticlesEffect, mAttached) == 0xc && offsetof(cTSMetaParticlesEffect, mParticles) == 0x10 &&
                  offsetof(cTSMetaParticlesEffect, mCount) == 0x18 && offsetof(cTSMetaParticlesEffect, mDescription) == 0x1c &&
                  offsetof(cTSMetaParticlesEffect, mOwner) == 0x20 && offsetof(cTSMetaParticlesEffect, mLocal) == 0x28 &&
                  offsetof(cTSMetaParticlesEffect, mWorld) == 0x60 && offsetof(cTSMetaParticlesEffect, mTime) == 0x98 &&
                  offsetof(cTSMetaParticlesEffect, mEmitClock) == 0xa8 && offsetof(cTSMetaParticlesEffect, mEmitPeriod) == 0xb8 &&
                  offsetof(cTSMetaParticlesEffect, mForce) == 0xc4 && offsetof(cTSMetaParticlesEffect, mWalks) == 0xd0 &&
                  offsetof(cTSMetaParticlesEffect, mHasPreferDir) == 0xf8 && offsetof(cTSMetaParticlesEffect, mAttractors) == 0x104 &&
                  offsetof(cTSMetaParticlesEffect, mStartArgument) == 0x110 && offsetof(cTSMetaParticlesEffect, mEmitterPosition) == 0x11c &&
                  offsetof(cTSMetaParticlesEffect, mEvent101) == 0x128 && offsetof(cTSMetaParticlesEffect, mRateScale) == 0x144 &&
                  offsetof(cTSMetaParticlesEffect, mSurface) == 0x14c && offsetof(cTSMetaParticlesEffect, mSource) == 0x15c &&
                  offsetof(cTSMetaParticlesEffect, mFollowerSet) == 0x174 && offsetof(cTSMetaParticlesEffect, mColliders) == 0x17c &&
                  offsetof(cTSMetaParticlesEffect, mColliderState) == 0x18c && offsetof(cTSMetaParticlesEffect, mSpawnArgument) == 0x19c &&
                  offsetof(cTSMetaParticlesEffect, mNameOverride) == 0x1a4 && offsetof(cTSMetaParticlesEffect, mFollower) == 0x1c8 &&
                  offsetof(cTSMetaParticlesEffect, mPath) == 0x1cc && offsetof(cTSMetaParticlesEffect, mSpawned) == 0x1dc &&
                  offsetof(cTSMetaParticlesEffect, mNode) == 0x1e8 && sizeof(cTSMetaParticlesEffect) == cTSMetaParticlesEffect::kSize,
              "cTSMetaParticlesEffect layout");
