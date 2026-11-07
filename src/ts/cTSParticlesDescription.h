#pragma once
// The particle effect descriptions (lane IX): cTSParticlesDescription (component type 1, 0x288 bytes) and
// cTSMetaParticlesDescription (type 2, 0x1e8 bytes, particles of particle effects), both on cTSBaseParticlesDescription
// (0x158 bytes, RTTI with one virtual slot: the deleting destructor). The effects parser's `particles` and `metaParticles`
// blocks fill them (src/gz/ParticleCommands.*, whose BaseParticlesDescription / ParticlesDescription /
// MetaParticlesDescription views name the members the commands set); the effects resource reads and writes them through
// the component template cTSCompDescriptionBaseT<D, N> (src/ts/cTSCompDescription.h, the description at +0x10).
//
// The parts they hold: colliders (0x84), attractors (0x34), warp forces (0x1c), a path (its points, 0x1c each) and the
// meta's random walks (0x1c); each with its own serializers. Their vectors' growth, copies and moves are the STL's.
//
// Standalone shape: real classes; the compiler emits the vtables. Hooks: the constructors, destructors, assignments and
// serializers at their RVAs (adapters: this in ecx), the component factories and slots (the template's adapters).
// RVAs 1.18.0.187, all /O2.
#include "ts/cTSCompDescription.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ---- the parts ----

// A collider (0x84 bytes): `collide name` or `collider-plane` in the parser. Constructor 0xbe1530 (the plane's three vectors
// left unset), destructor 0xbe1aa0, copy constructor 0xbe1490; serializers 0xbde880 / 0xbde940 (cdecl: stream, collider).
struct cTSParticleCollider {
    uint32_t mFlags = 2;             // +0x00 bit 1 set by the constructor; the writer always sets it
    std::string mName;               // +0x04
    fxdesc::Vector3 mPlane[3];       // +0x1c (unset by the constructor)
    float m40 = 1.0f;                // +0x40
    float m44 = 1.0f;                // +0x44
    float m48 = 0.0f;                // +0x48 serialized when the byte the flags' bit 1 announces is set (the writer: always)
    float mBounce = 0.0f;            // +0x4c (1 for a collider-plane)
    float m50 = 0.0f;                // +0x50
    std::string m54;                 // +0x54
    std::string m6C;                 // +0x6c

    cTSParticleCollider() {}
    static void __cdecl Read(void *stream, cTSParticleCollider *c);        // 0xbde880
    static void __cdecl Write(void *stream, const cTSParticleCollider *c); // 0xbde940
};
static_assert(offsetof(cTSParticleCollider, mPlane) == 0x1c && offsetof(cTSParticleCollider, mBounce) == 0x4c &&
              offsetof(cTSParticleCollider, m6C) == 0x6c && sizeof(cTSParticleCollider) == 0x84);

// An attractor (0x34 bytes): `attractor` (and force -attractor / -presetAttractor). Constructor 0xbddd10, destructor
// 0xbe1920, copy constructor 0xbe13d0; the parser's setters 0xbde010 (no centre), 0xbde600 (the centre), 0xbde620 (the
// curve), 0x4b21f0 (the strength, a body folded with other classes'); serializers 0xbde550 / 0xbde750 (this in ecx), reached
// through 0xbddd80 / 0xbdde60 (cdecl: stream, attractor).
struct cTSParticleAttractor {
    uint8_t mHasCentre = 0;                        // +0x00
    fxdesc::Vector3 mCentre{0.0f, 0.0f, 0.0f};     // +0x04
    float m10 = 1.0f;                              // +0x10 (not serialized)
    float m14 = 0.0f;                              // +0x14 (not serialized)
    float m18 = 0.0f;                              // +0x18 (not serialized)
    cTSEffectCurve mCurve;                         // +0x1c
    float mStrength = 1.0f;                        // +0x2c
    float m30 = -1.0f;                             // +0x30

    cTSParticleAttractor() {} // user-provided: a vector's new elements keep their padding (+0x01..0x03) as the original's
    void ClearCentre() { mHasCentre = 0; }         // 0xbde010
    void SetCentre(const fxdesc::Vector3 &centre); // 0xbde600
    void SetCurve(const cTSEffectCurve &curve);    // 0xbde620
    void SetStrength(float strength) { mStrength = strength; } // 0x4b21f0 (folded)
    void Read(void *stream);                       // 0xbde550
    void Write(void *stream) const;                // 0xbde750
};
static_assert(offsetof(cTSParticleAttractor, mCurve) == 0x1c && offsetof(cTSParticleAttractor, mStrength) == 0x2c &&
              sizeof(cTSParticleAttractor) == 0x34);

// A warp force (0x1c bytes): warp -wiggle / -wiggleDir. A vector's new elements get only the first word cleared (0xbe13b0).
// Serializers 0xbfa580 / 0xbfa660 (cdecl: stream, warp).
struct cTSParticleWarp {
    float mValue = 0.0f;        // +0x00
    fxdesc::Vector3 mFirst;     // +0x04 (unset by the constructor)
    fxdesc::Vector3 mSecond;    // +0x10 (unset by the constructor)

    cTSParticleWarp() {}
    static void __cdecl Read(void *stream, cTSParticleWarp *w);        // 0xbfa580
    static void __cdecl Write(void *stream, const cTSParticleWarp *w); // 0xbfa660
};
static_assert(sizeof(cTSParticleWarp) == 0x1c);

// A point of a path (0x1c bytes): where, the velocity there, when. New elements are cleared (0xbecf30). Serializers 0xbfa550 /
// 0xbfa620 (cdecl: stream, point).
struct cTSParticlePathPoint {
    fxdesc::Vector3 mPosition;  // +0x00
    fxdesc::Vector3 mVelocity;  // +0x0c
    float mTime;                // +0x18

    static void __cdecl Read(void *stream, cTSParticlePathPoint *p);        // 0xbfa550
    static void __cdecl Write(void *stream, const cTSParticlePathPoint *p); // 0xbfa620
};
static_assert(sizeof(cTSParticlePathPoint) == 0x1c);

// A path (0x10 bytes): the points and flags (bit 0 preExtend, bit 1 postExtend). Serializers 0xbfb430 / 0xbfb510 (this in
// ecx), reached through 0xbfa5b0 / 0xbfa6a0 (cdecl: stream, path).
struct cTSParticlePath {
    std::vector<cTSParticlePathPoint> mPoints; // +0x00
    uint32_t mFlags = 0;                       // +0x0c

    void Read(void *stream);        // 0xbfb430
    void Write(void *stream) const; // 0xbfb510
    // 0xbba1b0 (ret 8): the point at time `t`: carried on at the end velocities outside the points (if the flags say so), a cubic
    // between the two around it, the point itself at the start. false: outside and not extended. Needs at least one point.
    bool Evaluate(float t, cTSParticlePathPoint *out) const;
};
static_assert(sizeof(cTSParticlePath) == 0x10);

// The meta description's random walk (0x1c bytes): -delay, -strength (centre - range, centre + range), -turn, -mix.
// Serializers 0xbfa500 / 0xbfa5c0 (cdecl: stream, walk).
struct cTSParticleWalk {
    fxdesc::Vector2 mDelay;     // +0x00
    fxdesc::Vector2 mStrength;  // +0x08
    float mTurn[2];             // +0x10
    float mMix;                 // +0x18

    static void __cdecl Read(void *stream, cTSParticleWalk *w);        // 0xbfa500
    static void __cdecl Write(void *stream, const cTSParticleWalk *w); // 0xbfa5c0
};
static_assert(sizeof(cTSParticleWalk) == 0x1c);

// The colour (and pivot) lists' reader 0xbe02f0 (cdecl: stream, vector; returns the stream): a count, the vector resized
// (new elements unset), then three floats each.
void *__cdecl ReadParticleTriples(void *stream, std::vector<fxdesc::Color> *v);
// The particle and collider readers' string reader 0xbde7b0 (cdecl: stream, string; returns the stream): fxdesc::GetText.
void *__cdecl ReadParticleText(void *stream, std::string *text);

// ---- the descriptions ----

// cTSBaseParticlesDescription: constructor 0xbfb5a0, destructor 0xbfb990 (deleting 0xbfbb30), copy assignment 0xbe1df0 (the
// compiler's, member by member: also the derived classes' move takes the base by copy). What its members are, from the
// parser's commands: src/gz/ParticleCommands.h BaseParticlesDescription.
class cTSBaseParticlesDescription {
public:
    cTSBaseParticlesDescription() {}
    virtual ~cTSBaseParticlesDescription();
    cTSBaseParticlesDescription(const cTSBaseParticlesDescription &) = default;
    cTSBaseParticlesDescription &operator=(const cTSBaseParticlesDescription &) = default;

    fxdesc::Vector2 mLife{-1.0f, -1.0f};               // +0x04
    float mPreroll = 0.0f;                             // +0x0c
    cTSEffectCurve mRate{30.0f};                       // +0x10
    float mRateVary = 1.0f;                            // +0x20
    uint32_t mRateMode = 0;                            // +0x24 (the particles' serializers: its low 16 bits)
    fxdesc::Vector2 m28{-1.0f, -1.0f};                 // +0x28
    fxdesc::Vector2 m30{-1.0f, -1.0f};                 // +0x30
    fxdesc::Vector3 m38{0.0f, 0.0f, 1.0f};             // +0x38
    fxdesc::Vector3 m44{0.0f, 0.0f, 1.0f};             // +0x44
    fxdesc::Vector2 mEmitVolume{0.0f, 0.0f};           // +0x50
    fxdesc::Vector3 mSourceMin{-1.0f, -1.0f, -1.0f};   // +0x58
    fxdesc::Vector3 mSourceMax{1.0f, 1.0f, 1.0f};      // +0x64
    float mSourceRing = -1.0f;                         // +0x70
    cTSEffectCurve mSize{1.0f};                        // +0x74
    float mSizeVary = 0.0f;                            // +0x84
    cTSEffectCurve mAlpha{1.0f};                       // +0x88
    float mAlphaVary = 0.0f;                           // +0x98
    std::vector<fxdesc::Color> mColors;                // +0x9c (the derived constructors add one white)
    fxdesc::Vector3 mColorVary{0.0f, 0.0f, 0.0f};      // +0xa8
    cTSEffectCurve mAspect{1.0f};                      // +0xb4
    float mAspectVary = 0.0f;                          // +0xc4
    fxdesc::Vector3 mForce{0.0f, 0.0f, 0.0f};          // +0xc8
    float mGlobalWind = 0.0f;                          // +0xd4
    float mBomb = 0.0f;                                // +0xd8
    fxdesc::Vector3 mBombCentre{0.0f, 0.0f, 0.0f};     // +0xdc
    float mDrag = 0.0f;                                // +0xe8
    uint8_t mAlignment = 0;                            // +0xec (+0xed..0xef padding)
    float mScrew = 0.0f;                               // +0xf0
    std::vector<cTSParticleWarp> mWarps;               // +0xf4
    uint8_t mBloom[4] = {0x00, 0xff, 0x00, 0xff};      // +0x100
    float mBounce = 1.0f;                              // +0x104
    float mTerrainRepel[5] = {0.0f, 0.0f, 0.0f, 0.0f, -1.0e9f}; // +0x108
    float mDeath = 0.0f;                               // +0x11c
    float mDeathByWater = 1.0f;                        // +0x120
    fxdesc::Vector2 mHeightRange{-10000.0f, 10000.0f}; // +0x124
    float mLimitHeight = 0.0f;                         // +0x12c
    std::vector<cTSParticleCollider> mColliders;       // +0x130
    std::vector<cTSParticleAttractor> mAttractors;     // +0x13c
    cTSParticlePath mPath;                             // +0x148
};
static_assert(offsetof(cTSBaseParticlesDescription, mRate) == 0x10 && offsetof(cTSBaseParticlesDescription, mRateMode) == 0x24 &&
              offsetof(cTSBaseParticlesDescription, mSourceRing) == 0x70 && offsetof(cTSBaseParticlesDescription, mColors) == 0x9c &&
              offsetof(cTSBaseParticlesDescription, mAspect) == 0xb4 && offsetof(cTSBaseParticlesDescription, mAlignment) == 0xec &&
              offsetof(cTSBaseParticlesDescription, mWarps) == 0xf4 && offsetof(cTSBaseParticlesDescription, mTerrainRepel) == 0x108 &&
              offsetof(cTSBaseParticlesDescription, mColliders) == 0x130 && offsetof(cTSBaseParticlesDescription, mPath) == 0x148 &&
              sizeof(cTSBaseParticlesDescription) == 0x158);

// type 1. Constructor 0xbe15d0, destructor 0xbe1be0 (deleting 0xbe2bb0), move assignment 0xbe2320, copy assignment 0xbe2700;
// serializers 0xbe72e0 / 0xbe7db0 (versions 1..11); CreateEffect makes a cTSParticlesEffect.
class cTSParticlesDescription : public cTSBaseParticlesDescription {
public:
    cTSParticlesDescription();
    ~cTSParticlesDescription() override;
    cTSParticlesDescription(const cTSParticlesDescription &) = delete;
    cTSParticlesDescription &operator=(const cTSParticlesDescription &) = default;
    cTSParticlesDescription &operator=(cTSParticlesDescription &&) = default;

    static constexpr uint16_t kVersion = 11;
    static bool Accepts(uint16_t version) { return uint16_t(version - 1) <= 10; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSParticlesDescription *d); // 0xbe72e0
    static void __cdecl Write(void *stream, const cTSParticlesDescription *d);          // 0xbe7db0

    uint64_t mFlags = 0;                               // +0x158 (ParticleCommands.h: the bits)
    float m160 = 0.0f;                                 // +0x160
    fxdesc::Vector3 mRotateAxis[2]{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}; // +0x164
    uint8_t mRotateCount = 0;                          // +0x17c (+0x17d..0x17f padding)
    fxdesc::Vector2 mRotateVary[2]{{1.0f, 1.0f}, {1.0f, 1.0f}}; // +0x180
    float mRotateOffset[2] = {0.0f, 0.0f};             // +0x190
    cTSEffectCurve mRotateCurve[2];                    // +0x198
    std::vector<fxdesc::Color> mPivots;                // +0x1b8 (three floats each; the constructor adds one zero)
    fxdesc::Vector3 mPivotVary{0.0f, 0.0f, 0.0f};      // +0x1c4
    std::string mMaterial;                             // +0x1d0
    uint8_t mTile[2] = {1, 1};                         // +0x1e8
    uint8_t mDraw = 0;                                 // +0x1ea (+0x1eb padding)
    float mLayer = 0.0f;                               // +0x1ec
    float mFrameSpeed = 1.0f;                          // +0x1f0
    uint8_t mFrameStart = 0;                           // +0x1f4
    uint8_t mFrameCount = 0;                           // +0x1f5 (+0x1f6..0x1f7 padding)
    float mStretch = 0.0f;                             // +0x1f8
    cTSEffectCurve mScrewRadius{0.0f};                 // +0x1fc
    uint8_t mScrewRadiusToSpeed = 0;                   // +0x20c (+0x20d..0x20f padding)
    float mScrewRadiusVary = 0.0f;                     // +0x210
    float mScrewRadiusMax = 100.0f;                    // +0x214
    cTSEffectCurve mScrewAngle{0.0f};                  // +0x218
    uint8_t mScrewAngleToSpeed = 0;                    // +0x228 (+0x229..0x22b padding)
    float mScrewAngleVary = 0.0f;                      // +0x22c
    std::string mConstrain;                            // +0x230
    fxdesc::Vector3 mConstrainOffset{0.0f, 0.0f, 0.0f}; // +0x248
    fxdesc::Vector2 mWalkDelay{5.0f, 5.0f};            // +0x254
    fxdesc::Vector2 mWalkStrength{50.0f, 50.0f};       // +0x25c
    fxdesc::Vector2 mWalkTurn{0.1f, 0.2f};             // +0x264
    std::string mDeathEffect;                          // +0x26c (+0x284..0x287 padding)
};
static_assert(offsetof(cTSParticlesDescription, mFlags) == 0x158 && offsetof(cTSParticlesDescription, mRotateCount) == 0x17c &&
              offsetof(cTSParticlesDescription, mRotateCurve) == 0x198 && offsetof(cTSParticlesDescription, mPivots) == 0x1b8 &&
              offsetof(cTSParticlesDescription, mMaterial) == 0x1d0 && offsetof(cTSParticlesDescription, mFrameStart) == 0x1f4 &&
              offsetof(cTSParticlesDescription, mScrewRadius) == 0x1fc && offsetof(cTSParticlesDescription, mScrewAngle) == 0x218 &&
              offsetof(cTSParticlesDescription, mConstrain) == 0x230 && offsetof(cTSParticlesDescription, mWalkDelay) == 0x254 &&
              offsetof(cTSParticlesDescription, mDeathEffect) == 0x26c && sizeof(cTSParticlesDescription) == 0x288);

// type 2. Constructor 0xbecf80, destructor 0xbed140 (deleting 0xbed3f0), copy assignment 0xbed200 (its move is inline in the
// parser's block); serializers 0xbf1100 / 0xbf1c80 (versions 1..4); the effect maker is 0xc03910 (cTSMetaParticlesEffect::Create).
class cTSMetaParticlesDescription : public cTSBaseParticlesDescription {
public:
    cTSMetaParticlesDescription();
    ~cTSMetaParticlesDescription() override;
    cTSMetaParticlesDescription(const cTSMetaParticlesDescription &) = delete;
    cTSMetaParticlesDescription &operator=(const cTSMetaParticlesDescription &) = default;
    cTSMetaParticlesDescription &operator=(cTSMetaParticlesDescription &&) = default;

    static constexpr uint16_t kVersion = 4;
    static bool Accepts(uint16_t version) { return uint16_t(version - 1) <= 3; }
    cITSComponentEffect *CreateEffect();
    static void __cdecl Read(void *stream, uint32_t version, cTSMetaParticlesDescription *d); // 0xbf1100
    static void __cdecl Write(void *stream, const cTSMetaParticlesDescription *d);          // 0xbf1c80

    uint64_t mFlags = 0;                               // +0x158 (ParticleCommands.h: the bits)
    cTSEffectCurve mRotateCurve;                       // +0x160
    float mRotateVary = 0.0f;                          // +0x170
    float mRotateOffset = 0.0f;                        // +0x174
    std::string mEffect;                               // +0x178
    cTSParticleWalk mWalks[2]{{{5.0f, 5.0f}, {0.0f, 0.0f}, {0.25f, 0.0f}, 0.0f},
                              {{5.0f, 5.0f}, {0.0f, 0.0f}, {0.25f, 0.0f}, 0.0f}}; // +0x190
    fxdesc::Vector3 mPreferDir{0.0f, 0.0f, 0.0f};      // +0x1c8
    float mDamp = 0.0f;                                // +0x1d4
    fxdesc::Vector2 mBank{0.0f, 0.0f};                 // +0x1d8
    float mTractorResetSpeed = 0.0f;                   // +0x1e0 (+0x1e4..0x1e7 padding)
};
static_assert(offsetof(cTSMetaParticlesDescription, mRotateCurve) == 0x160 && offsetof(cTSMetaParticlesDescription, mEffect) == 0x178 &&
              offsetof(cTSMetaParticlesDescription, mWalks) == 0x190 && offsetof(cTSMetaParticlesDescription, mPreferDir) == 0x1c8 &&
              offsetof(cTSMetaParticlesDescription, mTractorResetSpeed) == 0x1e0 && sizeof(cTSMetaParticlesDescription) == 0x1e8);

using cTSParticlesComponentDescription = cTSCompDescriptionBaseT<cTSParticlesDescription, 1>;
using cTSMetaParticlesComponentDescription = cTSCompDescriptionBaseT<cTSMetaParticlesDescription, 2>;
static_assert(offsetof(cTSParticlesComponentDescription, mDescription) == 0x10 && sizeof(cTSParticlesComponentDescription) == 0x298);
static_assert(offsetof(cTSMetaParticlesComponentDescription, mDescription) == 0x10 &&
              sizeof(cTSMetaParticlesComponentDescription) == 0x1f8);

// The RVA hooks' entry points for the parts and the descriptions (this in ecx). Not the template's description adapters: the
// descriptions' destructors are virtual, and the hooked complete-object destructors must not dispatch (a game vtable's
// deleting destructor reaches them). Out of line where the parser's commands call them (their tests stand in for them).
struct cTSParticlePartsAdapters {
    // the base
    __declspec(noinline) static cTSBaseParticlesDescription *__fastcall ConstructBase(void *memory, void *);
    __declspec(noinline) static void __fastcall DestroyBase(cTSBaseParticlesDescription *self, void *);
    __declspec(noinline) static cTSBaseParticlesDescription *__fastcall AssignBase(cTSBaseParticlesDescription *self, void *,
                                                                                  const cTSBaseParticlesDescription *from);
    // the descriptions' deleting destructors (slot 0 of the game's vtables: 0xbfbb30, 0xbe2bb0, 0xbed3f0): the destructor
    // (not dispatched), then the class's size freed on flags bit 0
    static cTSBaseParticlesDescription *__fastcall DeleteBase(cTSBaseParticlesDescription *self, void *, uint32_t flags);
    static cTSParticlesDescription *__fastcall DeleteParticles(cTSParticlesDescription *self, void *, uint32_t flags);
    static cTSMetaParticlesDescription *__fastcall DeleteMeta(cTSMetaParticlesDescription *self, void *, uint32_t flags);
    // the particle description
    __declspec(noinline) static cTSParticlesDescription *__fastcall ConstructParticles(void *memory, void *);
    __declspec(noinline) static void __fastcall DestroyParticles(cTSParticlesDescription *self, void *);
    __declspec(noinline) static cTSParticlesDescription *__fastcall MoveParticles(cTSParticlesDescription *self, void *,
                                                                                 cTSParticlesDescription *from);
    __declspec(noinline) static cTSParticlesDescription *__fastcall CopyParticles(cTSParticlesDescription *self, void *,
                                                                                 const cTSParticlesDescription *from);
    // the meta-particle description
    __declspec(noinline) static cTSMetaParticlesDescription *__fastcall ConstructMeta(void *memory, void *);
    __declspec(noinline) static void __fastcall DestroyMeta(cTSMetaParticlesDescription *self, void *);
    __declspec(noinline) static cTSMetaParticlesDescription *__fastcall CopyMeta(cTSMetaParticlesDescription *self, void *,
                                                                               const cTSMetaParticlesDescription *from);
    // colliders
    __declspec(noinline) static cTSParticleCollider *__fastcall ConstructCollider(void *memory, void *);
    __declspec(noinline) static void __fastcall DestroyCollider(cTSParticleCollider *self, void *);
    __declspec(noinline) static cTSParticleCollider *__fastcall CopyCollider(void *memory, void *, const cTSParticleCollider *from);
    // attractors
    __declspec(noinline) static cTSParticleAttractor *__fastcall ConstructAttractor(void *memory, void *);
    __declspec(noinline) static void __fastcall DestroyAttractor(cTSParticleAttractor *self, void *);
    __declspec(noinline) static cTSParticleAttractor *__fastcall CopyAttractor(void *memory, void *, const cTSParticleAttractor *from);
    static void __fastcall SetCentre(cTSParticleAttractor *self, void *, const fxdesc::Vector3 *centre);
    __declspec(noinline) static void __fastcall SetCurve(cTSParticleAttractor *self, void *, const cTSEffectCurve *curve);
    static void __fastcall ReadAttractor(cTSParticleAttractor *self, void *, void *stream);
    static void __fastcall WriteAttractor(const cTSParticleAttractor *self, void *, void *stream);
    static void __cdecl ReadAttractorOf(void *stream, cTSParticleAttractor *a);        // 0xbddd80
    static void __cdecl WriteAttractorOf(void *stream, const cTSParticleAttractor *a); // 0xbdde60
    static void __cdecl ReadAttractors(void *stream, std::vector<cTSParticleAttractor> *v);        // 0xbddd90
    static void __cdecl WriteAttractors(void *stream, const std::vector<cTSParticleAttractor> *v); // 0xbdde70
    // the path
    static void __fastcall ReadPath(cTSParticlePath *self, void *, void *stream);
    static void __fastcall WritePath(const cTSParticlePath *self, void *, void *stream);
    static void __cdecl ReadPathOf(void *stream, cTSParticlePath *p);        // 0xbfa5b0
    static void __cdecl WritePathOf(void *stream, const cTSParticlePath *p); // 0xbfa6a0
};
