#pragma once
// The particle effect script's commands: the `particles` block of the effects parser (cTSEffectsParser) and the commands
// inside it. Their classes are in an anonymous namespace of the game's particle translation unit
// (`.?AUcParticleEffectCommand@?A0x7c74d268@@` and so on); here they are in the global namespace. RVAs are 1.18.0.187.
//
// Hook shape, as the material and lighting commands: the effects parser (not ours) creates the `particles` command with
// the game's vtable, so every slot is an RVA hook; the block's slot 3 builds its children as the original does
// (operator new, the base constructor, the child's game vtable, its name).
#include "gz/CommandParser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nGZCommandParser {
class cIArguments;
}

// A vector of a type that is not ours (its elements are built, copied and destroyed by the game's functions).
struct RawVector {
    uint8_t *mBegin, *mEnd, *mCapacity;
};

struct Vec3 {
    float x, y, z;
};
// A warp force (0x1c bytes): -wiggle's {second value, 0, 0, first value, cos * third, sin * -third, 0}, -wiggleDir's
// {first value, the optional second vector, the first vector}.
struct WarpForce {
    float v[7];
};

// A point of a particle path (0x1c bytes): where, the velocity there, when.
struct PathPoint {
    float mPosition[3];
    float mVelocity[3];
    float mTime;
};
static_assert(sizeof(PathPoint) == 0x1c, "PathPoint layout");

// cTSBaseParticlesDescription (0x158 bytes): the part of a description both blocks fill (`particles`, `metaParticles`).
// Its parsing helpers some commands hand their arguments to (0xbfbb60 and the others) are the game's.
struct BaseParticlesDescription {
    const void *mVTable;          // +0
    float mLife[2];               // +4 life (the game's 0xbfc0a0); the meta force's -tractorTimeScale scales them
    float mPreroll;               // +0xc life -preroll
    uint8_t mRate[0x40];          // +0x10 the emission rate (inject, maintain, rate; parsed by the game's 0xbd1ce0 / 0xbd1b70)
    float mEmitVolume[2];         // +0x50 set by emit's description call; both near zero sets kNoEmitVolume
    float mSourceMin[3];          // +0x58 source: the box's low corner (the shape's half-size off the game's zero vector)
    float mSourceMax[3];          // +0x64 source: the high corner
    float mSourceRing;            // +0x70 source -ring / -torus: the width, in [0, 1]
    uint8_t m74[0x54];
    float mForce[3];              // +0xc8 force -gravity (down), -wind (added), -reset (cleared)
    float mGlobalWind;            // +0xd4 force -globalWind
    float mBomb;                  // +0xd8 force -bomb
    float mBombCentre[3];         // +0xdc force -bomb's second value
    float mDrag;                  // +0xe8 force -drag
    uint8_t mAlignment;           // +0xec align (the parser's table 0x10fd18c; the meta block's 0x10fd204)
    uint8_t mED[3];
    float mScrew;                 // +0xf0 force / warp -screw (in 1/50ths; the meta warp's as given)
    std::vector<WarpForce> mWarps; // +0xf4 warp -wiggle, -wiggleDir
    uint8_t mBloom[4];            // +0x100 warp -screenBloom, -bloomAlpha (0, 1), -bloomSize (2, 3)
    float mBounce;                // +0x104 terrainCollide -bounce
    float mTerrainRepel[5];       // +0x108 force -terrainRepel
    float mDeath;                 // +0x11c terrainCollide -death
    float mDeathByWater;          // +0x120 terrainCollide -deathByWater
    float mHeightRange[2];        // +0x124 source -aboveHeight, -belowHeight
    float mLimitHeight;           // +0x12c terrainCollide -limitHeight
    RawVector mColliders;         // +0x130 collide, collider-plane (elements of 0x84 bytes)
    RawVector mAttractors;        // +0x13c attractor (elements of 0x34 bytes)
    std::vector<PathPoint> mPathPoints; // +0x148 the path (written by the game's 0xbfb1f0; the meta force's -tractor adds)
    uint32_t mPathFlags;          // +0x154 bit 0 preExtend, bit 1 postExtend (cleared with the points)
};
static_assert(offsetof(BaseParticlesDescription, mRate) == 0x10 && offsetof(BaseParticlesDescription, mEmitVolume) == 0x50 &&
                  offsetof(BaseParticlesDescription, mAlignment) == 0xec && offsetof(BaseParticlesDescription, mColliders) == 0x130 &&
                  offsetof(BaseParticlesDescription, mPathFlags) == 0x154 && sizeof(BaseParticlesDescription) == 0x158 &&
                  offsetof(BaseParticlesDescription, mSourceRing) == 0x70 && offsetof(BaseParticlesDescription, mForce) == 0xc8 &&
                  offsetof(BaseParticlesDescription, mWarps) == 0xf4 && offsetof(BaseParticlesDescription, mBloom) == 0x100 &&
                  offsetof(BaseParticlesDescription, mLimitHeight) == 0x12c && offsetof(BaseParticlesDescription, mPathPoints) == 0x148,
              "BaseParticlesDescription layout");

// The description a `particles` block fills: cTSParticlesDescription (0x288 bytes, vtable 0xe8ecb0). Not ours: its
// constructor (0xbe15d0), destructor (0xbe1be0), assignment (0xbe2320), copy of another's values (0xbe2700), finishing
// pass (0xbe58d0). Named here: the members the commands write themselves.
struct ParticlesDescription : BaseParticlesDescription {
    static constexpr uint32_t kVTable = 0xe8ecb0;

    // The flag bits (mFlags) the commands set.
    enum : uint64_t {
        kInject = 1ull << 0,             // inject
        kMaintain = 1ull << 1,           // maintain
        kCollide = 1ull << 3,            // collide
        kBaseEmit = 1ull << 5,           // emit -base
        kRadialEmit = 1ull << 6,         // emit -radial
        kRoundSource = 1ull << 8,        // source -circle, -sphere, -ellipse, -ellipsoid
        kHeightRange = 1ull << 10,       // source -belowHeight, -aboveHeight, -heightRange
        kScaleParticles = 1ull << 14,    // source -scaleParticles
        kTerrainCollide = 1ull << 15,    // terrainCollide
        kSticky = 1ull << 16,            // terrainCollide -sticky
        kTerrainRepel = 1ull << 17,      // terrainRepel, force -terrainRepel
        kKillOutsideTerrain = 1ull << 18, // terrainCollide -killOutsideTerrain
        kPinToTerrain = 1ull << 19,      // terrainCollide -pinToTerrain
        kRandomWalk = 1ull << 20,        // randomWalk
        kMaterial = 1ull << 21,          // material
        kShape = 1ull << 22,             // shape
        kLit = 1ull << 23,               // material / shape / texture -light
        kAcceptOverride = 1ull << 24,    // texture -acceptOverride
        kAcceptComposite = 1ull << 25,   // texture -acceptComposite
        kAttractor = 1ull << 26,         // attractor, force -attractor / -presetAttractor
        kScaleExisting = 1ull << 28,     // emit -scaleExisting
        kResetIncoming = 1ull << 29,     // source -resetIncoming
        kDirectLayer = 1ull << 31,       // texture -directLayer
        kAspect = 1ull << 32,            // aspect
        kTractorPath = 1ull << 33,       // path -tractor
        kFreePath = 1ull << 34,          // path without -tractor
        kSnapPath = 1ull << 35,          // path without -noSnap
        kLimitHeight = 1ull << 36,       // terrainCollide -limitHeight
        kNoEmitVolume = 1ull << 37,      // emit with both mEmitVolume values near zero
    };

    uint64_t mFlags;              // +0x158
    uint8_t m160[4];
    float mRotateAxis[2][3];      // +0x164 rotate -axis (one per rotate, at most two)
    uint8_t mRotateCount;         // +0x17c how many rotate -axis have been given (which rotate the next one sets)
    uint8_t m17D[3];
    float mRotateVary[2][2];      // +0x180 rotate -vary, -multVaryRange
    float mRotateOffset[2];       // +0x190 rotate -offset
    uint8_t mRotateCurve[2][0x10]; // +0x198 rotate's values (parsed by the game's 0xbd1a50)
    std::vector<Vec3> mPivots;    // +0x1b8 rotate -pivot
    float mPivotVary[3];          // +0x1c4 rotate -pivotVary
    std::string mMaterial;        // +0x1d0 material / shape / texture
    uint8_t mTile[2];             // +0x1e8 texture -tile (powers of two)
    uint8_t mDraw;                // +0x1ea texture -draw (the parser's table 0x10fd180)
    uint8_t m1EB;
    float mLayer;                 // +0x1ec texture -layer, -directLayer
    float mFrameSpeed;            // +0x1f0 frames -speed
    uint8_t mFrameStart;          // +0x1f4 frames -start
    uint8_t mFrameCount;          // +0x1f5 frames -count
    uint8_t m1F6[2];
    float mStretch;               // +0x1f8 stretch
    uint8_t mScrewRadius[0x10];   // +0x1fc force / warp -screwRadius (parsed by the game's 0xbd1be0)
    uint8_t mScrewRadiusToSpeed;  // +0x20c -screwRadiusToSpeed (the parser's table 0x10fd198)
    uint8_t m20D[3];
    float mScrewRadiusVary;       // +0x210 -screwRadiusVary
    float mScrewRadiusMax;        // +0x214 -screwRadiusMax (a negative one is 100)
    uint8_t mScrewAngle[0x10];    // +0x218 -screwAngle
    uint8_t mScrewAngleToSpeed;   // +0x228 -screwAngleToSpeed
    uint8_t m229[3];
    float mScrewAngleVary;        // +0x22c -screwAngleVary
    std::string mConstrain;       // +0x230 source -constrain
    float mConstrainOffset[3];    // +0x248 source -constrainOffset
    float mWalkDelay[2];         // +0x254 randomWalk -delay (centre - range, centre + range)
    float mWalkStrength[2];       // +0x25c randomWalk -strength
    float mWalkTurn[2];           // +0x264 randomWalk -turn (each in [0, 1])
    std::string mDeathEffect;     // +0x26c deathEffect
    uint8_t m284[4];
};
static_assert(offsetof(ParticlesDescription, mFlags) == 0x158 &&
                  offsetof(ParticlesDescription, mMaterial) == 0x1d0 && offsetof(ParticlesDescription, mFrameSpeed) == 0x1f0 &&
                  offsetof(ParticlesDescription, mStretch) == 0x1f8 && offsetof(ParticlesDescription, mWalkDelay) == 0x254 &&
                  offsetof(ParticlesDescription, mDeathEffect) == 0x26c && sizeof(ParticlesDescription) == 0x288 &&
                  offsetof(ParticlesDescription, mRotateAxis) == 0x164 &&
                  offsetof(ParticlesDescription, mRotateCount) == 0x17c && offsetof(ParticlesDescription, mRotateVary) == 0x180 &&
                  offsetof(ParticlesDescription, mRotateCurve) == 0x198 && offsetof(ParticlesDescription, mPivots) == 0x1b8 &&
                  offsetof(ParticlesDescription, mPivotVary) == 0x1c4 && offsetof(ParticlesDescription, mLayer) == 0x1ec &&
                  offsetof(ParticlesDescription, mScrewRadius) == 0x1fc && offsetof(ParticlesDescription, mScrewAngleVary) == 0x22c &&
                  offsetof(ParticlesDescription, mConstrain) == 0x230 && offsetof(ParticlesDescription, mConstrainOffset) == 0x248,
              "ParticlesDescription layout");

// The state the `particles` block's commands share (0x2a8 bytes, at +0x48 of the block): cParticlesState.
struct ParticlesState {
    uint32_t m0;                       // +0
    uint32_t m4;                       // +4
    ParticlesDescription mDescription; // +8
    std::string mName;                 // +0x290 the block's name (the description is registered under it)
};
static_assert(offsetof(ParticlesState, mDescription) == 8 && offsetof(ParticlesState, mName) == 0x290 &&
                  sizeof(ParticlesState) == 0x2a8,
              "ParticlesState layout");

// The effects parser's fields the block uses: its cBaseParser part (the vbptr at +8 leads to the parser face) and the
// component description manager (+0x1b0; slot 8 finds a description by name, slot 7 adds one).
struct cTSEffectsParserFields {
    uint8_t mBaseParser[0x1b0];
    void *mDescriptions; // +0x1b0 (may be null: then nothing is looked up or registered)
};
static_assert(offsetof(cTSEffectsParserFields, mDescriptions) == 0x1b0, "cTSEffectsParserFields layout");

// cTSEffectsCommandT<State> (0x28 bytes): a cCommandT whose name is a std::string (its slot 5, shared_vf_b03b80,
// answers the string's text; its slot 9, shared_vf_bcd710, destroys it). The `particles` block's children are these,
// their state the block's ParticlesState; the path block's, theirs the path block's +0x48.
class cEffectsCommand : public nGZCommandParser::cCommandT {
public:
    std::string mName; // +0x10
};
static_assert(sizeof(cEffectsCommand) == 0x28, "cEffectsCommand layout");

// ---- particles (vtable 0xe8f6c0) -------------------------------------------------------------------------------------
// `particles name [: base]`: opens a block that fills a description, starting from the defaults or from the description
// named after the colon; the block's end registers a copy under the name. cTSEffectsBlockCommandT<cParticlesState>
// (0x2f0 bytes). Slots 0..2, 6..8, 10, 11 are cBlockCommand's (or shared); slot 5 (shared_vf_1674d0, 4 bytes) answers
// mCommandName and is not hooked.
class cParticleEffectCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xe8f6c0;
    cTSEffectsParserFields *Parser() const { return static_cast<cTSEffectsParserFields *>(mState); }

    void SetContext(void *parser, void *stateInterface); // slot 3, 0xbe5b00: 32 child commands
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xbe2f30
    void EndBlock();                                     // slot 9, 0xbe59b0
    cParticleEffectCommand *DeletingDestructor(uint32_t flags); // slot 12, 0xbe2b50

    const char *mCommandName;  // +0x44 the block's name (set by whoever creates it)
    ParticlesState mParticles; // +0x48 the children's state
};
static_assert(offsetof(cParticleEffectCommand, mCommandName) == 0x44 && offsetof(cParticleEffectCommand, mParticles) == 0x48 &&
                  sizeof(cParticleEffectCommand) == 0x2f0,
              "cParticleEffectCommand layout");

// ---- path (vtable 0xe8f660) ------------------------------------------------------------------------------------------
// `path -tractor -noSnap -preExtend -postExtend` inside `particles`: opens a block of path points (its children add to
// the path at +0x4c); the block's end stores the path in the description. cTSEffectsBlockCommandT<cPathState> (0x84
// bytes); its state is the particles state, as given.
class cParticlePathCommand : public nGZCommandParser::cBlockCommandBase {
public:
    static constexpr uint32_t kVTable = 0xe8f660;
    static constexpr uint32_t kPathKind = 0xd01ae32a; // the word at +0x48 the `particles` block's slot 3 writes

    void SetContext(void *parser, void *state);        // slot 3, 0xbe6a30: 12 child commands
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0xbe3be0
    void EndBlock();                                   // slot 9, 0xbe5a90
    cParticlePathCommand *DeletingDestructor(uint32_t flags); // slot 12, 0xbe2b80

    ParticlesState *mParticles; // +0x40 (the state as given)
    const char *mCommandName;   // +0x44 "path"
    uint32_t mKind;             // +0x48 kPathKind (the children's state starts here)
    uint8_t mPath[0x34];        // +0x4c the path being built (its constructor 0xbfa4a0 and the rest are not ours)
    uint8_t mPreExtend;         // +0x80
    uint8_t mPostExtend;        // +0x81
    uint8_t m82[2];
};
static_assert(offsetof(cParticlePathCommand, mParticles) == 0x40 && offsetof(cParticlePathCommand, mKind) == 0x48 &&
                  offsetof(cParticlePathCommand, mPreExtend) == 0x80 && sizeof(cParticlePathCommand) == 0x84,
              "cParticlePathCommand layout");

// ---- the path block's commands (p / position, dp / deltaPosition, v / velocity, s / speed, t / time, dt / deltaTime)
// Each hands its one argument (a vector or a float) to the path's own function (not ours). Their state is the path
// block's +0x48 (the path at +4).
class cPathCommand : public cEffectsCommand {
public:
    void *Path() const { return static_cast<uint8_t *>(mState) + 4; }
};

#define PARTICLE_PATH_COMMAND(cls, vtable, execute)                                                                    \
    class cls : public cPathCommand {                                                                                  \
    public:                                                                                                            \
        static constexpr uint32_t kVTable = vtable;                                                                    \
        static constexpr uint32_t kExecute = execute; /* the game's slot 4 */                                          \
        void Execute(nGZCommandParser::cIArguments *args);                                                             \
    }

PARTICLE_PATH_COMMAND(cPathPointCommand, 0xe8f558, 0xbe57b0);      // p / position
PARTICLE_PATH_COMMAND(cPathDeltaPointCommand, 0xe8f584, 0xbe5730); // dp / deltaPosition
PARTICLE_PATH_COMMAND(cPathVelocityCommand, 0xe8f5b0, 0xbe5870);   // v / velocity
PARTICLE_PATH_COMMAND(cPathSpeedCommand, 0xe8f5dc, 0xbe57f0);      // s / speed
PARTICLE_PATH_COMMAND(cPathTimeCommand, 0xe8f608, 0xbe5830);       // t / time
PARTICLE_PATH_COMMAND(cPathDeltaTimeCommand, 0xe8f634, 0xbe5770);  // dt / deltaTime

#undef PARTICLE_PATH_COMMAND

// ---- the particles block's commands ----------------------------------------------------------------------------------
// cTSEffectsCommandT<cParticlesState>: each sets values of the block's description. Slots other than 4 are shared
// (GetName answers mName).
class cParticleCommand : public cEffectsCommand {
public:
    ParticlesState *State() const { return static_cast<ParticlesState *>(mState); }
    ParticlesDescription &Description() const { return State()->mDescription; }
};

#define PARTICLE_COMMAND(cls, vtable, execute)                                                                         \
    class cls : public cParticleCommand {                                                                              \
    public:                                                                                                            \
        static constexpr uint32_t kVTable = vtable;                                                                    \
        static constexpr uint32_t kExecute = execute; /* the game's slot 4 */                                          \
        void Execute(nGZCommandParser::cIArguments *args);                                                             \
    }

PARTICLE_COMMAND(cParticleColorCommand, 0xe8ed78, 0xbe2ed0);          // color, colour, color255, colour255
PARTICLE_COMMAND(cParticleAlphaCommand, 0xe8eda4, 0xbe2c70);          // alpha, alpha255
PARTICLE_COMMAND(cParticleSizeCommand, 0xe8edd0, 0xbe43c0);           // size
PARTICLE_COMMAND(cParticleAspectCommand, 0xe8edfc, 0xbe2c90);         // aspect
PARTICLE_COMMAND(cParticleEmitCommand, 0xe8efb4, 0xbe30c0);           // emit
PARTICLE_COMMAND(cParticleAttractorCommand, 0xe8f0e0, 0xbe2cc0);      // attractor
PARTICLE_COMMAND(cParticleRandomWalkCommand, 0xe8f170, 0xbe3dd0);     // randomWalk
PARTICLE_COMMAND(cParticleStretchCommand, 0xe8f19c, 0xbe4b40);        // stretch
PARTICLE_COMMAND(cParticleLifeCommand, 0xe8f1c8, 0xbe3ab0);           // life
PARTICLE_COMMAND(cParticleRateCommand, 0xe8f1f4, 0xbe3fc0);           // rate
PARTICLE_COMMAND(cParticleInjectCommand, 0xe8f220, 0xbe3a60);         // inject
PARTICLE_COMMAND(cParticleMaintainCommand, 0xe8f24c, 0xbe3ad0);       // maintain
PARTICLE_COMMAND(cParticleMaterialCommand, 0xe8f278, 0xbe3b20);       // material, billboardMaterial
PARTICLE_COMMAND(cParticleShapeCommand, 0xe8f374, 0xbe4300);          // shape
PARTICLE_COMMAND(cParticleFramesCommand, 0xe8f3cc, 0xbe39c0);         // frames
PARTICLE_COMMAND(cParticleAlignmentCommand, 0xe8f408, 0xbe2c40);      // align
PARTICLE_COMMAND(cParticleCollideCommand, 0xe8f434, 0xbe2da0);        // collide
PARTICLE_COMMAND(cParticleTerrainRepelCommand, 0xe8f4d4, 0xbe4d30);   // terrainRepel
PARTICLE_COMMAND(cParticlePlaneCollisionCommand, 0xe8f500, 0xbe3ca0); // collider-plane
PARTICLE_COMMAND(cParticleDeathEffectCommand, 0xe8f52c, 0xbe2ef0);    // deathEffect
PARTICLE_COMMAND(cParticleRotateCommand, 0xe8ee28, 0xbe4000);         // rotate
PARTICLE_COMMAND(cParticleSourceCommand, 0xe8eed8, 0xbe43e0);         // source
PARTICLE_COMMAND(cParticleForceCommand, 0xe8f000, 0xbe31a0);          // force
PARTICLE_COMMAND(cParticleWarpCommand, 0xe8f10c, 0xbe5050);           // warp
PARTICLE_COMMAND(cParticleTextureCommand, 0xe8f2c4, 0xbe4d70);        // texture
PARTICLE_COMMAND(cParticleTerrainCollideCommand, 0xe8f460, 0xbe4b80); // terrainCollide

#undef PARTICLE_COMMAND

// ==== the meta-particle block ===========================================================================================
// `metaParticles name [: base]`: a block of the effects parser that fills a cTSMetaParticlesDescription (particles of
// particle effects). Its classes are in another anonymous namespace (`.?AUcMetaParticleEffectCommand@?A0x2a4d774e@@` and
// so on). The color, alpha, size and life commands and the path block's point commands run the particles block's
// bodies (the same functions: cParticleColorCommand::Execute and the others above).

// A random walk's values (0x1c bytes): randomWalk -delay, -strength (centre - range, centre + range), -turn, -mix.
struct MetaWalk {
    float mDelay[2];
    float mStrength[2];
    float mTurn[2];
    float mMix;
};
static_assert(sizeof(MetaWalk) == 0x1c, "MetaWalk layout");

// cTSMetaParticlesDescription (0x1e8 bytes, vtable 0xe8fb28). Not ours: its constructor (0xbecf80), destructor
// (0xbed140), its base's assignment (0xbe1df0), copy of another's values (0xbed200) and finishing pass (0xbefb90); the
// rate's functions (0xbdcd10, 0xbf0ef0).
struct MetaParticlesDescription : BaseParticlesDescription {
    static constexpr uint32_t kVTable = 0xe8fb28;

    enum : uint64_t {
        kInject = 1ull << 0,              // inject
        kMaintain = 1ull << 1,            // maintain
        kBaseEmit = 1ull << 4,            // emit -base
        kCollide = 1ull << 5,             // collide name
        kTerrainCollide = 1ull << 6,      // terrainCollide, collide without a name
        kSticky = 1ull << 7,              // -sticky
        kTerrainRepel = 1ull << 8,        // terrainRepel, force -terrainRepel
        kKillOutsideTerrain = 1ull << 9,  // -killOutsideTerrain (terrainCollide, source)
        kPinToTerrain = 1ull << 10,       // terrainCollide -pinToTerrain
        kRoundSource = 1ull << 11,        // source -circle, -sphere, -ellipse, -ellipsoid
        kTerrainSource = 1ull << 12,      // source -terrain, -terrainWindySide
        kWindySide = 1ull << 13,          // source -terrainWindySide
        kSourcePinToTerrain = 1ull << 14, // source -pinToTerrain
        kPinToWater = 1ull << 15,         // source -pinToWater
        kHeightRange = 1ull << 16,        // source -terrainOnly, -waterOnly, -belowHeight, -aboveHeight, -heightRange
        kScaleParticles = 1ull << 22,     // source -scaleParticles
        kSyncWalk = 1ull << 23,           // randomWalk -sync
        kRandomWalk = 1ull << 24,         // randomWalk without -sync
        kWalkWait = 1ull << 25,           // randomWalk -wait
        kAttractor = 1ull << 27,          // attractor, force -attractor
        kPath = 1ull << 29,               // path, force -tractor / -tractorRel
        kFreePath = 1ull << 30,           // path without -tractor, force -path
        kWindBank = 1ull << 31,           // align -windBank
        kResetIncoming = 1ull << 32,      // source -resetIncoming
        kLimitHeight = 1ull << 33,        // -limitHeight
    };

    uint64_t mFlags;                  // +0x158
    uint32_t mRotateCurve;            // +0x160 rotate's values with the next (parsed by the game's 0xbd1a50)
    std::vector<float> mRotateValues; // +0x164
    float mRotateVary;                // +0x170 rotate -vary
    float mRotateOffset;              // +0x174 rotate -offset
    std::string mEffect;              // +0x178 effect
    MetaWalk mWalks[2];               // +0x190 randomWalk, one per command (the state's mWalkIndex)
    float mPreferDir[3];              // +0x1c8 randomWalk -preferDir
    float mDamp;                      // +0x1d4 align -damp
    float mBank[2];                   // +0x1d8 align -bank, -windBank
    float mTractorResetSpeed;         // +0x1e0 force -tractorResetSpeed
    uint32_t m1E4;
};
static_assert(offsetof(MetaParticlesDescription, mFlags) == 0x158 && offsetof(MetaParticlesDescription, mRotateValues) == 0x164 &&
                  offsetof(MetaParticlesDescription, mEffect) == 0x178 && offsetof(MetaParticlesDescription, mWalks) == 0x190 &&
                  offsetof(MetaParticlesDescription, mPreferDir) == 0x1c8 && offsetof(MetaParticlesDescription, mDamp) == 0x1d4 &&
                  offsetof(MetaParticlesDescription, mTractorResetSpeed) == 0x1e0 && sizeof(MetaParticlesDescription) == 0x1e8,
              "MetaParticlesDescription layout");

// The state the meta block's commands share (0x218 bytes, at +0x48 of the block).
struct MetaParticlesState {
    uint32_t m0;                           // +0
    uint32_t m4;                           // +4
    MetaParticlesDescription mDescription; // +8
    float mTime;                           // +0x1f0 the tractor path's time so far (its last point's)
    float mTimeScale;                      // +0x1f4 force -tractorTimeScale (1 at the block's start)
    uint32_t mWalkIndex;                   // +0x1f8 how many randomWalk commands so far (0 at the block's start)
    uint32_t m1FC;
    std::string mName;                     // +0x200 the block's name (the description is registered under it)
};
static_assert(offsetof(MetaParticlesState, mDescription) == 8 && offsetof(MetaParticlesState, mTime) == 0x1f0 &&
                  offsetof(MetaParticlesState, mWalkIndex) == 0x1f8 && offsetof(MetaParticlesState, mName) == 0x200 &&
                  sizeof(MetaParticlesState) == 0x218,
              "MetaParticlesState layout");

// ---- metaParticles (vtable 0xe900c8) ---------------------------------------------------------------------------------
// cTSEffectsBlockCommandT<cMetaParticlesState> (0x260 bytes). Slot 5 (shared_vf_1674d0) answers mCommandName.
class cMetaParticleEffectCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xe900c8;
    cTSEffectsParserFields *Parser() const { return static_cast<cTSEffectsParserFields *>(mState); }

    void SetContext(void *parser, void *stateInterface); // slot 3, 0xbefd10: 27 child commands and the path block
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xbed9e0
    void EndBlock();                                     // slot 9, 0xbefbc0
    cMetaParticleEffectCommand *DeletingDestructor(uint32_t flags); // slot 12, 0xbed360

    const char *mCommandName; // +0x44
    MetaParticlesState mMeta; // +0x48 the children's state
};
static_assert(offsetof(cMetaParticleEffectCommand, mMeta) == 0x48 && sizeof(cMetaParticleEffectCommand) == 0x260,
              "cMetaParticleEffectCommand layout");

// ---- the meta block's path (vtable 0xe8fe70) -------------------------------------------------------------------------
// `path -tractor`: as the particles block's path (the same children's bodies; the same destructor, 0xbe2b80), its end
// marks the description's path.
class cMetaParticlePathCommand : public nGZCommandParser::cBlockCommandBase {
public:
    static constexpr uint32_t kVTable = 0xe8fe70;
    static constexpr uint32_t kPathKind = 0xad5c3612; // the word at +0x48 the meta block's slot 3 writes

    void SetContext(void *parser, void *state);        // slot 3, 0xbf0900: 12 child commands
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0xbee8a0
    void EndBlock();                                   // slot 9, 0xbefca0

    MetaParticlesState *mMeta; // +0x40 (the state as given)
    const char *mCommandName;  // +0x44 "path"
    uint32_t mKind;            // +0x48 kPathKind (the children's state starts here)
    uint8_t mPath[0x34];       // +0x4c the path being built (the game's)
    uint8_t mTractor;          // +0x80 path -tractor
    uint8_t m81[3];
};
static_assert(offsetof(cMetaParticlePathCommand, mMeta) == 0x40 && offsetof(cMetaParticlePathCommand, mTractor) == 0x80 &&
                  sizeof(cMetaParticlePathCommand) == 0x84,
              "cMetaParticlePathCommand layout");

// ---- the meta block's commands ---------------------------------------------------------------------------------------
class cMetaParticleCommand : public cEffectsCommand {
public:
    MetaParticlesState *State() const { return static_cast<MetaParticlesState *>(mState); }
    MetaParticlesDescription &Description() const { return State()->mDescription; }
};

#define META_PARTICLE_COMMAND(cls, vtable, execute)                                                                    \
    class cls : public cMetaParticleCommand {                                                                          \
    public:                                                                                                            \
        static constexpr uint32_t kVTable = vtable;                                                                    \
        static constexpr uint32_t kExecute = execute; /* the game's slot 4 */                                          \
        void Execute(nGZCommandParser::cIArguments *args);                                                             \
    }

META_PARTICLE_COMMAND(cMetaParticleAspectCommand, 0xe8fbf4, 0xbed5c0);         // aspect
META_PARTICLE_COMMAND(cMetaParticleRotateCommand, 0xe8fc20, 0xbeec20);         // rotate
META_PARTICLE_COMMAND(cMetaParticleSourceCommand, 0xe8fc4c, 0xbeecc0);         // source
META_PARTICLE_COMMAND(cMetaParticleEmitCommand, 0xe8fcb0, 0xbedd30);           // emit
META_PARTICLE_COMMAND(cMetaParticleForceCommand, 0xe8fcdc, 0xbedd80);          // force
META_PARTICLE_COMMAND(cMetaParticleAttractorCommand, 0xe8fd3c, 0xbed5e0);      // attractor
META_PARTICLE_COMMAND(cMetaParticleWarpCommand, 0xe8fea8, 0xbef7d0);           // warp
META_PARTICLE_COMMAND(cMetaParticleRandomWalkCommand, 0xe8fed4, 0xbee8e0);     // randomWalk
META_PARTICLE_COMMAND(cMetaParticleRateCommand, 0xe8ff4c, 0xbeebe0);           // rate
META_PARTICLE_COMMAND(cMetaParticleInjectCommand, 0xe8ff78, 0xbee7a0);         // inject
META_PARTICLE_COMMAND(cMetaParticleMaintainCommand, 0xe8ffa4, 0xbee800);       // maintain
META_PARTICLE_COMMAND(cMetaParticleAlignmentCommand, 0xe8ffd0, 0xbed480);      // align
META_PARTICLE_COMMAND(cMetaParticleCollisionCommand, 0xe90018, 0xbed6c0);      // collide
META_PARTICLE_COMMAND(cMetaParticleTerrainCollideCommand, 0xe90044, 0xbef5f0); // terrainCollide
META_PARTICLE_COMMAND(cMetaParticleTerrainRepelCommand, 0xe90070, 0xbef7a0);   // terrainRepel
META_PARTICLE_COMMAND(cMetaParticleEffectBaseCommand, 0xe9009c, 0xbed9a0);     // effect

#undef META_PARTICLE_COMMAND

// The meta block's commands that run the particles block's bodies: their own vtables.
constexpr uint32_t kMetaParticleColorVTable = 0xe8fb70;  // color, colour, color255, colour255 (0xbe2ed0)
constexpr uint32_t kMetaParticleAlphaVTable = 0xe8fb9c;  // alpha, alpha255 (0xbe2c70)
constexpr uint32_t kMetaParticleSizeVTable = 0xe8fbc8;   // size (0xbe43c0)
constexpr uint32_t kMetaParticleLifeVTable = 0xe8ff20;   // life (0xbe3ab0)
// The meta path block's point commands (p / position ... dt / deltaTime; the particles path's bodies).
constexpr uint32_t kMetaPathVTables[6] = {0xe8fd68, 0xe8fd94, 0xe8fdc0, 0xe8fdec, 0xe8fe18, 0xe8fe44};

// `group name` for meta-particle groups (vtable 0xe90100): a cTSEffectsCommandT whose state is the effects parser; it
// hands the name to the parser's group handling (0xbcd940, not ours) with the meta-particle kind (2).
class cGroupMetaParticlesCommand : public cEffectsCommand {
public:
    static constexpr uint32_t kVTable = 0xe90100;
    static constexpr uint32_t kExecute = 0xbed450;
    void Execute(nGZCommandParser::cIArguments *args); // slot 4
};

// ---- decals (the effects parser's `decal` block, its own translation unit: `.?AUcDecalEffectCommand@?A0xcd4ad098@@`) ---

// cTSDecalDescription (0x8c bytes; constructor 0xbeb2c0, destructor 0xbeb410, move assignment 0xbeb5a0, copy 0xbeb760:
// ours, src/ts/cTSCompDescription.h, which this view mirrors with the parser's names). The curves (0x10 bytes each) are
// cTSEffectCurve (src/ts/cTSEffectCurve.h), parsed by 0xbd1a50.
struct DecalDescription {
    enum : uint32_t {
        kLight = 0x2,        // texture -light
        kWater = 0x4,        // texture -water
        kRepeat = 0x8,       // texture -repeat
        kTerrainScale = 0x10, // size -terrainScale
        kRing = 0x20,        // texture -ring
        kStatic = 0x40,      // life -static
    };
    uint32_t mTypeId;          // +0 0x2c0fcace
    uint32_t mFlags;           // +4
    std::string mTexture;      // +8
    uint8_t mDraw;             // +0x20 texture -draw (the parser's table 0x10fd1e8)
    uint8_t mLifeMode;         // +0x21 0 loop (and static), 1 single, 2 sustain
    uint8_t m22[2];
    float mLife;               // +0x24
    uint8_t mRotate[0x10];     // +0x28 curve
    uint8_t mSize[0x10];       // +0x38 curve
    uint8_t mAlpha[0x10];      // +0x48 curve
    std::vector<Vec3> mColors; // +0x58
    uint8_t mAspect[0x10];     // +0x64 curve
    float mAlphaVary;          // +0x74
    float mSizeVary;           // +0x78
    float mRotateVary;         // +0x7c
    float mRepeat;             // +0x80
    float mOffset[2];          // +0x84
};
static_assert(offsetof(DecalDescription, mTexture) == 8 && offsetof(DecalDescription, mLife) == 0x24 &&
                  offsetof(DecalDescription, mColors) == 0x58 && offsetof(DecalDescription, mAspect) == 0x64 &&
                  offsetof(DecalDescription, mOffset) == 0x84 && sizeof(DecalDescription) == 0x8c,
              "DecalDescription layout");

// ---- decal (vtable 0xe8fa8c) -------------------------------------------------------------------------------------------
// `decal name [: base]`: as `particles`, a block that fills a description (the children's state is the description
// itself) and registers a copy under the name at its end. cTSEffectsBlockCommandT (0xec bytes). Slot 5 (shared_vf_1674d0)
// answers mCommandName.
class cDecalEffectCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xe8fa8c;
    cTSEffectsParserFields *Parser() const { return static_cast<cTSEffectsParserFields *>(mState); }

    void SetContext(void *parser, void *stateInterface); // slot 3, 0xbec320: 11 child commands
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xbebdb0
    void EndBlock();                                     // slot 9, 0xbec260
    cDecalEffectCommand *DeletingDestructor(uint32_t flags); // slot 12, 0xbeb8d0

    const char *mCommandName; // +0x44
    DecalDescription mDecal;  // +0x48 the children's state
    std::string mName;        // +0xd4 the block's name
};
static_assert(offsetof(cDecalEffectCommand, mDecal) == 0x48 && offsetof(cDecalEffectCommand, mName) == 0xd4 &&
                  sizeof(cDecalEffectCommand) == 0xec,
              "cDecalEffectCommand layout");

// ---- the decal block's commands ------------------------------------------------------------------------------------------
class cDecalCommand : public cEffectsCommand {
public:
    DecalDescription &Description() const { return *static_cast<DecalDescription *>(mState); }
};

#define DECAL_COMMAND(cls, vtable, execute)                                                                            \
    class cls : public cDecalCommand {                                                                                 \
    public:                                                                                                            \
        static constexpr uint32_t kVTable = vtable;                                                                    \
        static constexpr uint32_t kExecute = execute; /* the game's slot 4 */                                          \
        void Execute(nGZCommandParser::cIArguments *args);                                                             \
    }

DECAL_COMMAND(cDecalColorCommand, 0xe8f934, 0xbebb60);   // color, colour, color255, colour255
DECAL_COMMAND(cDecalAlphaCommand, 0xe8f964, 0xbeb990);   // alpha, alpha255
DECAL_COMMAND(cDecalSizeCommand, 0xe8f990, 0xbec050);    // size
DECAL_COMMAND(cDecalAspectCommand, 0xe8f9cc, 0xbebb40);  // aspect
DECAL_COMMAND(cDecalRotateCommand, 0xe8f9f8, 0xbebff0);  // rotate
DECAL_COMMAND(cDecalLifeCommand, 0xe8fa24, 0xbebf30);    // life
DECAL_COMMAND(cDecalTextureCommand, 0xe8fa58, 0xbec0d0); // texture

#undef DECAL_COMMAND
