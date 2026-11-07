#pragma once
// cTSParticlesEffect (lane WR, rendering row E4): the effect a particles description makes (RTTI name, 0x210 bytes, vtables
// 0xe8bdbc with 14 slots and 0xe8bdf8 at +4). A list of pooled particles, an emitter that adds them, the integrators that move
// them, the renderable node that draws them (cTSParticlesRenderable, which also reads this class's fields). RVAs 1.18.0.187;
// all /O2.
//
// Standalone shape (docs/standalone-shape.md): only Create 0xbc0c50 builds one (cTSParticlesDescription::CreateEffect, ours),
// so every effect carries our vtables and the slots need no adapters. Slot 5, the step, is ours too (src/ts/cTSParticlesStep.cpp, rows E5, E6: 0xbb6830 is
// in effect through its callers, the vtable). The
// helpers are hooked at their RVAs; nothing here may move in the layout.
#include "gz/cIGZUnknown.h"
#include "rz/RZRefCountBase.h"
#include "rz/cRZString.h"
#include "ts/CountedRef.h"
#include "ts/EffectsTransform.h"
#include "ts/cTSEffectBase.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

class cTSParticlesDescription;
struct cTSParticleAttractor;
struct cTSParticleCollider;

namespace nGZSceneGraph {
class cICompositionTreeNode;
class cTSParticlesRenderable;
} // namespace nGZSceneGraph

// One particle (0x7c bytes), from the fixed pool: its first word links the free list while it is free.
struct cTSParticle {
    uint8_t mAlive;           // +0x00 an integrator clears it when the life runs out
    uint8_t mFrameOffset;     // +0x01 added to the frame counter (a random start in the texture's strip)
    uint8_t mPad02[2];
    float mAge;               // +0x04
    float mLife;              // +0x08
    fx::Vector3 mPosition;    // +0x0c
    fx::Vector3 mVelocity;    // +0x18
    fx::Vector3 mAxis;        // +0x24
    fx::Vector3 mPathPosition; // +0x30 the description's path, its first point at the start
    fx::Vector3 mPathVelocity; // +0x3c
    float mSize;              // +0x48
    float mAspect;            // +0x4c
    float mRotation;          // +0x50
    float mRotationSpeed;     // +0x54 (the second of InitParticle's two rotations)
    float mPivot[2];          // +0x58
    float mExtra;             // +0x60
    float mAlpha;             // +0x64
    fx::Vector3 mColor;       // +0x68
    float mScrewRadius;       // +0x74
    float mScrewAngle;        // +0x78
};
static_assert(sizeof(cTSParticle) == 0x7c, "cTSParticle size");
static_assert(offsetof(cTSParticle, mSize) == 0x48 && offsetof(cTSParticle, mAlpha) == 0x64 &&
                  offsetof(cTSParticle, mScrewAngle) == 0x78,
              "cTSParticle layout");

// What the owner's slot 21 hands out (the effects world's settings, not counted): the bytes before these two are not read here.
struct cTSParticlesSettings {
    float mRateFactor;        // +0x00 the step: the effect's +0x17c is this times its scale
    float mSizeFactor;        // +0x04 the step: +0x180 likewise
    uint8_t mWord8[4];
    float mDetail;            // +0x0c the step's mix between the two ends of its three ranges (+0x148..)
    uint8_t mBefore[0xe];
    uint8_t mSetsBlend;       // +0x1e the geometry's blend word is written
    uint8_t mPad1f;
    int32_t mRenderLayer;     // +0x20 a particle's child node's second flag, when positive
};

// An attractor's running state (0x14 bytes); the effect's vector has one per attractor of the description, and so has the
// meta-particles effect's. The value-initialised element is all zero (0xbddce0).
struct cTSParticleAttractorState {
    fx::Vector3 mCentre;                        // +0x00
    float mStrength;                            // +0x0c
    const cTSParticleAttractor *mDescription;   // +0x10

    // 0xbde5a0 (ret 8): the description taken; one with a centre starts there (through `transform`, if any), at strength 1.
    void Set(const cTSParticleAttractor *description, const fx::EffectsTransform *transform);
    // 0xbde5e0 (ret 4).
    void SetCentre(const fx::Vector3 *centre);
    // 0xbde650 (ret 4): a centre the description fixes (exactly byte 1) goes along with `transform`.
    void Follow(const fx::EffectsTransform *transform);
    // 0xbb9840 (ret 8): the force on a point, by the description's byte: 2 and 3 scale the offsets by its three factors (+0x10..),
    // 4 pulls to its radius (a random unit vector for a point at the centre), anything else is centre minus point.
    fx::Vector3 *Force(fx::Vector3 *out, const fx::Vector3 *position) const;
    // 0xbb9b90 (ret 0xc, `this` the vector): the sum of the forces, each rescaled to its strength (through the description's curve
    // at distance over range). `stop`, when given, is set once a force falls inside the description's minimum distance (the rest
    // of the vector is then skipped).
    static fx::Vector3 *__fastcall Sum(std::vector<cTSParticleAttractorState> *states, void *, fx::Vector3 *out,
                                       const fx::Vector3 *position, char *stop);
    // 0xbddf10 (ret 4, `this` the vector): one state per attractor, each Set with no transform.
    static void __fastcall Assign(std::vector<cTSParticleAttractorState> *states, void *,
                                  const std::vector<cTSParticleAttractor> *attractors);
};
static_assert(sizeof(cTSParticleAttractorState) == 0x14, "cTSParticleAttractorState size");

// A collider the owner found for one of the description's (0xc bytes). The object is not counted.
struct cTSParticleColliderState {
    cIGZUnknown *mObject;
    const cTSParticleCollider *mDescription; // the description's entry
    uint32_t mStateOffset;      // into the effect's scratch: the collider's slot 3 says how much state it wants
};
static_assert(sizeof(cTSParticleColliderState) == 0xc, "cTSParticleColliderState size");

namespace fx {
// 0xbe01c0 (cdecl): the owner makes the effect called `name` (its slot 14), placed at `*position` (and under `parent`, if any),
// handed `argument`, started; it goes on `keep` (counted), when there is one. Not counted otherwise: released at the end.
void __cdecl SpawnEffect(cIGZUnknown *owner, const char *name, const Vector3 *position, uint32_t argument,
                         std::vector<CountedRef<cIGZUnknown>> *keep, EffectsTransform *parent);
// 0xbdf6a0 (cdecl): a particle moving by `*vector` (to `*position`, at `*velocity`) against each collider in turn: one that is
// hit may kill the particle (returns false; its text spawns an effect), spawns its hit effect, and bounces the velocity (damped by
// its drag). Nothing at all while the owner's slot 8 (2) says so. True: the particle lives.
bool __cdecl SweepColliders(cIGZUnknown *owner, std::vector<cTSParticleColliderState> *colliders, std::vector<char> *scratch,
                            float distance, const Vector3 *vector, Vector3 *position, Vector3 *velocity, uint32_t argument,
                            std::vector<CountedRef<cIGZUnknown>> *keep, EffectsTransform *parent);
// 0xbb9ea0 (cdecl): where the particle is drawn at time t: its position, turned about its velocity by the description's screw.
void __cdecl ParticlePosition(Vector3 *out, const cTSParticle *particle, const cTSParticlesDescription *description, float t);
} // namespace fx

// The effect's box: the two corners. 0xbb9350 and 0xbb5bd0.
struct cTSParticleBox {
    fx::Vector3 mMin, mMax;
    fx::Vector3 Center() const;            // 0xbb9350 (ret 4, a hidden result)
    void Extend(const fx::Vector3 *point); // 0xbb5bd0 (ret 4): grown to hold the point; an empty one (max.x < min.x) becomes it
    // 0xbc0cb0 (ret 4): the box into the transform's space: scaled and moved; with its rotation (flag 2) as the box of the rotated box
    // (centre through the rotation, the half size through the matrix of absolute values).
    void Transform(const fx::EffectsTransform *transform);
};
static_assert(sizeof(cTSParticleBox) == 0x18, "cTSParticleBox size");

class cTSParticlesEffect : public cITSComponentEffect, public rz::cRZRefCount {
public:
    static constexpr size_t kSize = 0x210;
    static constexpr uint32_t kPoolFreeCell = 0x107cfd4; // the particle pool's free list (the emitters carve it, still game code)

    explicit cTSParticlesEffect(cTSParticlesDescription *description); // 0xbb4a80
    ~cTSParticlesEffect() override;                                    // 0xbb5290; deleting 0xbb5860, +4 thunk 0xbb5761
    static cITSComponentEffect *__cdecl Create(cTSParticlesDescription *description); // 0xbc0c50: null on failure

    void Attach(cIGZUnknown *owner) override;   // 0xbba860: once; the owner's objects, the renderable
    void Detach() override;                     // 0xbc07e0
    void Start(uint32_t a) override;            // 0xbc0a10
    void Stop(uint32_t a) override;             // 0xbc0b90: `a` nonzero stops at once and clears the particles
    bool IsRunning() override { return mRunning; } // shared 0x1b79e0
    // 5: 0xbb6830 (src/ts/cTSParticlesStep.cpp, row E6): `step` is a float's bits, `stats` the world's counters.
    void AddToScene(void *step, void *stats) override;
    void Update(const fx::EffectsTransform *local, const fx::EffectsTransform *parent) override; // 0xbbd8f0
    bool HandleEvent(uint32_t kind, void *a, void *b) override; // 0xbbd0e0: `a` the values
    bool Query(void *a, void *b) override;      // 0xbbdc90: `a` 0 and `b` a string: the texture override
    void SetFlagged(bool value) override;       // 0xbbd380: shown or not
    void Slot10(uint32_t follower) override;    // 0xbbd0a0: the effect dead particles go on to
    void Slot11(uint32_t description) override; // 0xbc0c20: a new description, from an interface's slot 3
    uint32_t AddRef() override { return rz::cRZRefCount::AddRef(); }   // shared 0x1dab20
    uint32_t Release() override { return rz::cRZRefCount::Release(); } // shared 0x5b7980

    // ---- helpers the step calls, hooked at their RVAs ----
    void Prepare();                         // 0xbbab40: what comes from the description (and the owner's lookups by name)
    void Aim(const fx::Vector3 *target);    // 0xbbd410: event 5, the rotation and speed that reach `target`
    void ClearParticles();                  // 0xbbce70
    void RetireParticle(cTSParticle *particle); // 0xbb94d0: gone to the pool (with a death effect), or on to the follower
    void HandParticleOn(cTSParticle *particle); // 0xbba740: to the follower: `this` is the one the particle came from
    void InitParticle(cTSParticle *particle);   // 0xbbb390 (ret 4): a new particle, every field, the game's randoms in the original's order
    void AddParticle(cTSParticle *particle);    // 0xbb5b30
    void ResetLife(cTSParticle *particle);      // 0xbc1120 eax-ignored: the one direct call site, in 0xbc1160, doesn't read it. A new life from the start
    void ResetLives();                          // 0xbc1160
    void StepWind(float step);                  // 0xbc1b10: the wind's next gust once the clock runs out
    // 0xbb97a0 (ret 4): one step's new particles: nothing once stopping; the delay first (a negative rest carries over), then the
    // description's preroll caps the step at preroll - 0.01, and flag 2 picks the once-per-step emitter, flag 1 the bursts, else
    // the continuous one.
    void Emit(float step);
    void EmitContinuous(float step); // 0xbb5c70 (ret 4): a rate (the curve over the clock) in pieces of at most 1/15 s
    void EmitBursts(float step);     // 0xbbbfa0 (ret 4): a count at each tick of the clock
    void EmitOnce(float step);       // 0xbbc560 (ret 4): fills up to the curve's count when the clock ticks
    void MarkFollowerSet() { mFollowerSet = true; } // 0xbc0c10
    // 0xbb6730 (ret 0x10): the sweep over the colliders for one particle's step, the move `vector` (by value) over its size and
    // alpha curves, its variance and the effect's scale; false when a collider killed the particle.
    bool CollideColliders(cTSParticle *particle, fx::Vector3 vector);
    // 0xbb82f0 (ret 0xc): one step's forces on a particle: the attractors' sum, the drag, the colliders, then the surface; `vector`
    // is the move over the step. false: the particle is dead (its first byte cleared).
    bool ApplyForces(cTSParticle *particle, const fx::Vector3 *vector, float step);
    // 0xbb85b0 (ret 0xc): the surface under a particle (`where`, in the surface's space): pushed off it (terrain repel), or
    // reflected from it. false: the particle is dead.
    bool CollideSurface(cTSParticle *particle, const fx::Vector3 *where, float step);
    // The integrators (ret 8, thiscall; `step` the time to move on). Each ages the particle and returns false when it is dead.
    bool IntegrateParticle(cTSParticle *particle, float step);        // 0xbc1190: picks one by the description's flags
    bool IntegrateSimple(cTSParticle *particle, float step);          // 0xbc12a0: one trapezoid step with drag
    bool IntegrateSubsteps(cTSParticle *particle, float step);        // 0xbc13c0: pieces of at most 1/15 s, forces after each
    bool IntegrateSteeredSubsteps(cTSParticle *particle, float step); // 0xbc19f0: as that, steered along the path in each piece
    bool IntegrateSteered(cTSParticle *particle, float step);         // 0xbc1530: pieces, the path's offset and velocity added
    bool IntegrateOnPath(cTSParticle *particle, float step);          // 0xbbcb70: position follows the path's moves
    bool IntegrateOnPathPlaced(cTSParticle *particle, float step);    // 0xbc08b0: position is the path's point
    void SteerAlongPath(cTSParticle *particle, float step);           // 0xbb8c00 (ret 8): a cubic towards the next path point

    // A text with the game's layout, embedded (+0x1c4; 0x20 bytes).
    struct Text {
        union {
            cRZString mString;
        };
        Text() { mString.Construct(); }
        ~Text() { mString.Destruct(); }
        Text(const Text &) = delete;
        Text &operator=(const Text &) = delete;
    };

    const std::string &TextureOverride() const { return mTextureOverride.mString.mString; }

    bool mAttached;                      // +0x0c
    bool mRunning;                       // +0x0d
    bool mStopping;                      // +0x0e no new particles, the live ones run out
    bool mVisible;                       // +0x0f the renderable is under its parent; 1 at first
    uint32_t mStopMode;                  // +0x10 the last Stop's argument; nonzero: particles leave no death effect
    std::list<cTSParticle *> mParticles; // +0x14
    int32_t mCount;                      // +0x1c the live ones
    cTSParticlesDescription *mDescription; // +0x20 not counted
    fx::CountedRef<cIGZUnknown> mOwner;  // +0x24
    fx::CountedRef<nGZSceneGraph::cICompositionTreeNode> mParent; // +0x28 owner's slot 42, once the step asks
    fx::CountedRef<cIGZUnknown> mScene;  // +0x2c owner's slot 43, likewise; InitParticle's flag 0x20 asks it
    const cTSParticlesSettings *mSettings; // +0x30 owner's slot 21, not counted
    uint32_t mWord34;                    // +0x34 never set
    double mTime;                        // +0x38
    bool mPrerollPending;                // +0x40 the next step also runs the description's preroll
    double mEmitClock;                   // +0x48
    int32_t mEmitted;                    // +0x50
    double mEmitPeriod;                  // +0x58 1 / rate
    float mEmitRemainder;                // +0x60
    fx::Vector3 mGravity;                // +0x64 the description's force
    float mWindClock;                    // +0x70 seconds to the next gust
    float mWindAngle[2];                 // +0x74 turns
    fx::Vector3 mWind;                   // +0x7c
    std::vector<cTSParticleAttractorState> mAttractors; // +0x88
    float mDelay;                        // +0x94 before the first emission
    float mBound[6];                     // +0x98 the box of the particles, min then max (cTSParticleBox)
    float mMaxSize;                      // +0xb0 the size curve's largest, with its variance
    float mMaxAlpha;                     // +0xb4 likewise the +0x88 curve
    fx::Vector3 mEmitterPosition;        // +0xb8 in the world, from Update
    fx::EffectsTransform mLocal;         // +0xc4
    fx::EffectsTransform mWorld;         // +0xfc
    fx::Vector3 mPreviousPosition;       // +0x134 the last step's mLocal translation
    uint16_t mFramesDrawn;               // +0x140
    uint16_t mWord142;                   // +0x142
    float mSpeedScale;                   // +0x144 event 0
    float mRange[6];                     // +0x148 event 0x101: min/max of three scales
    float mRateScale;                    // +0x160 event 2
    float mLifeScale;                    // +0x164 event 1
    float mEvent3;                       // +0x168 event 3 (0 from the constructor, 1 from Attach)
    fx::Vector3 mEvent4;                 // +0x16c event 4
    uint32_t mFirstFrame;                // +0x178 the description's byte; event 0xd
    float mScale17c;                     // +0x17c
    float mSpawnSize;                    // +0x180 a new particle's size scale
    float mSizeScale;                    // +0x184
    float mAlphaScale;                   // +0x188
    fx::Vector3 mColorScale;             // +0x18c
    fx::CountedRef<cIGZUnknown> mSurface; // +0x198 owner's slot 24, then its slot 7
    float mSurfaceRange[2];              // +0x19c the surface's slot 3, its first two numbers taken off
    float mWord1a4;                      // +0x1a4
    cTSParticleBox mSource;              // +0x1a8 the description's source box
    fx::CountedRef<nGZSceneGraph::cTSParticlesRenderable> mRenderable; // +0x1c0
    Text mTextureOverride;               // +0x1c4
    std::vector<cTSParticleColliderState> mColliders; // +0x1e4
    bool mCollidersPlaced;               // +0x1f0 set by the step; Update and Prepare clear it
    std::vector<char> mColliderState;    // +0x1f4 the colliders' scratch
    fx::CountedRef<cIGZUnknown> mConstrain; // +0x200 owner's slot 30, by the description's constraint name
    fx::CountedRef<cITSComponentEffect> mFollower; // +0x204 a cTSParticlesEffect
    bool mFollowerSet;                   // +0x208 Slot10 was called; a follower's Update keeps the owner's placement

private:
    cTSParticleBox &Box() { return *reinterpret_cast<cTSParticleBox *>(mBound); }
    void FromTheParent();   // the renderable out of its parent, if it has one
    // The emitters differ in the order of a few operands (NaN payloads only): which one is in the shared tail below.
    enum class EmitStyle : uint8_t { kContinuous, kBursts, kOnce };
    // A new particle that is alive and has been integrated: counted, and its drawn position (stretched along its velocity when
    // the description asks) grown into the box. A dead one goes back to the pool.
    void LaunchParticle(cTSParticle *particle, float step, EmitStyle style);
};
static_assert(offsetof(cTSParticlesEffect, mAttached) == 0xc && offsetof(cTSParticlesEffect, mStopMode) == 0x10 &&
                  offsetof(cTSParticlesEffect, mParticles) == 0x14 && offsetof(cTSParticlesEffect, mCount) == 0x1c &&
                  offsetof(cTSParticlesEffect, mDescription) == 0x20 && offsetof(cTSParticlesEffect, mSettings) == 0x30 &&
                  offsetof(cTSParticlesEffect, mTime) == 0x38 && offsetof(cTSParticlesEffect, mEmitPeriod) == 0x58 &&
                  offsetof(cTSParticlesEffect, mAttractors) == 0x88 && offsetof(cTSParticlesEffect, mBound) == 0x98 &&
                  offsetof(cTSParticlesEffect, mEmitterPosition) == 0xb8 && offsetof(cTSParticlesEffect, mLocal) == 0xc4 &&
                  offsetof(cTSParticlesEffect, mWorld) == 0xfc && offsetof(cTSParticlesEffect, mFramesDrawn) == 0x140 &&
                  offsetof(cTSParticlesEffect, mRange) == 0x148 && offsetof(cTSParticlesEffect, mFirstFrame) == 0x178 &&
                  offsetof(cTSParticlesEffect, mSizeScale) == 0x184 && offsetof(cTSParticlesEffect, mColorScale) == 0x18c &&
                  offsetof(cTSParticlesEffect, mSurface) == 0x198 && offsetof(cTSParticlesEffect, mSource) == 0x1a8 &&
                  offsetof(cTSParticlesEffect, mRenderable) == 0x1c0 && offsetof(cTSParticlesEffect, mTextureOverride) == 0x1c4 &&
                  offsetof(cTSParticlesEffect, mColliders) == 0x1e4 && offsetof(cTSParticlesEffect, mColliderState) == 0x1f4 &&
                  offsetof(cTSParticlesEffect, mConstrain) == 0x200 && offsetof(cTSParticlesEffect, mFollowerSet) == 0x208 &&
                  sizeof(cTSParticlesEffect) == cTSParticlesEffect::kSize,
              "cTSParticlesEffect layout");
