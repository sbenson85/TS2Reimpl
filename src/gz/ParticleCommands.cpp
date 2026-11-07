#include "gz/ParticleCommands.h"

#include "gz/MaterialCommands.h" // ParserFace
#include "gz/cArguments.h"
#include "misc/UtilitiesC.h"
#include "rz/TopCallersA.h" // cBlockCommand::StoreAndQuery
#include "ts/cTSCompDescription.h"
#include "ts/cTSEffectCurve.h"
#include "ts/cTSParticlesDescription.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <new>
#include <string.h> // _stricmp

namespace cp = nGZCommandParser;
using nGZSceneGraph::ParserFace;

namespace {

const char *Text(uint32_t rva) { return GameAddress<char>(rva); }
// A name the game keeps in a global pointer (its value is read when the command is built).
const char *NameAt(uint32_t rva) { return *GameAddress<const char *const>(rva); }

ParserFace FaceOf(const cp::cCommand *command) { return ParserFace(ParserFace::Direct{}, command->mParser); }

// ---- the description (cTSParticlesDescription, src/ts/cTSParticlesDescription.h: lane IX's entries) --------------------

using Parts = cTSParticlePartsAdapters;

void ConstructDescription(void *at) { Parts::ConstructParticles(at, nullptr); }
void DestroyDescription(void *at) { Parts::DestroyParticles(static_cast<cTSParticlesDescription *>(at), nullptr); }
// The move assignment (0xbe2320).
void AssignDescription(void *to, const void *from)
{
    Parts::MoveParticles(static_cast<cTSParticlesDescription *>(to), nullptr,
                         static_cast<cTSParticlesDescription *>(const_cast<void *>(from)));
}
// Takes another description's values (the base a block names, or the block's own when it is registered).
void CopyDescription(void *to, const void *from)
{
    Parts::CopyParticles(static_cast<cTSParticlesDescription *>(to), nullptr, static_cast<const cTSParticlesDescription *>(from));
}
void FinishDescription(void *at) { OriginalFunction<void(__thiscall *)(void *), 0xbe58d0>()(at); }

// A description on the stack: the game's constructor and destructor.
struct LocalDescription {
    alignas(8) uint8_t mBytes[sizeof(ParticlesDescription)];
    LocalDescription() { ConstructDescription(mBytes); }
    ~LocalDescription() { DestroyDescription(mBytes); }
    LocalDescription(const LocalDescription &) = delete;
    LocalDescription &operator=(const LocalDescription &) = delete;
};

// cTSCompDescriptionBaseT<cTSParticlesDescription, 1> (0x298 bytes): what the description manager keeps. Its
// cITSComponentDescription face at +0 (slot 7 AddRef, slot 8 Release), cRZRefCount at +4, the description at +0x10.
struct ParticlesComponent {
    static constexpr uint32_t kInterfaceVTable = 0xe8ecb8; // cITSComponentDescription's (set first)
    static constexpr uint32_t kVTable = 0xe8ece0;
    static constexpr uint32_t kRefCountVTable = 0xe8ed08;

    const void *mVTable;               // +0
    UtilObjectA mRefCount;             // +4 cRZRefCount (vtable, count)
    uint32_t mC;                       // +0xc (not set)
    ParticlesDescription mDescription; // +0x10
};
static_assert(offsetof(ParticlesComponent, mDescription) == 0x10 && sizeof(ParticlesComponent) == 0x298,
              "ParticlesComponent layout");

void Release(void *component) { VirtualCall<uint32_t(__thiscall *)(void *), 8>(component); }

// A found description, released when it goes (as the original's unwinding does when the lookup throws).
template <class Component> struct FoundComponent {
    Component *mPointer = nullptr;
    ~FoundComponent()
    {
        if (mPointer) Release(mPointer);
    }
};

// ---- the children ------------------------------------------------------------------------------------------------------

// A cTSEffectsCommandT child: the command base, an empty name, the game's vtable, then the name.
cEffectsCommand *NewEffectsCommand(uint32_t vtable, uint32_t nameRva)
{
    cEffectsCommand *command = nullptr;
    if (void *memory = ::operator new(sizeof(cEffectsCommand))) {
        auto *base = static_cast<cp::cCommand *>(memory)->Construct();
        command = static_cast<cEffectsCommand *>(base);
        new (&command->mName) std::string();
        command->mVTable = GameAddress(vtable);
    }
    const char *name = NameAt(nameRva);
    command->mName.assign(name, std::strlen(name));
    return command;
}

struct NamedChild {
    uint32_t vtable;
    uint32_t name; // the global holding the name
};

void AddToBlock(cp::cBlockCommandBase *block, void *command)
{
    VirtualCall<void(__thiscall *)(void *, void *), 10>(block, command);
}

void AddChildren(cp::cBlockCommandBase *block, const NamedChild *children, size_t count)
{
    for (size_t i = 0; i < count; ++i) AddToBlock(block, NewEffectsCommand(children[i].vtable, children[i].name));
}

} // namespace

// ---- particles -------------------------------------------------------------------------------------------------------

void cParticleEffectCommand::SetContext(void *parser, void *stateInterface)
{
    // cBlockCommandT's slot 3 (the state with 4 subtracted), then the children's state: the block's own.
    SetContextAdjusted(parser, stateInterface);
    mValue = reinterpret_cast<uint32_t>(&mParticles);

    static constexpr NamedChild kFirst[] = {
        {cParticleColorCommand::kVTable, 0x107dda4},          // color
        {cParticleColorCommand::kVTable, 0x107ddcc},          // color255
        {cParticleColorCommand::kVTable, 0x107ddc8},          // colour
        {cParticleColorCommand::kVTable, 0x107ddd0},          // colour255
        {cParticleAlphaCommand::kVTable, 0x107ddd4},          // alpha
        {cParticleAlphaCommand::kVTable, 0x107ddd8},          // alpha255
        {cParticleSizeCommand::kVTable, 0x107dddc},           // size
        {cParticleAspectCommand::kVTable, 0x107dde0},         // aspect
        {cParticleRotateCommand::kVTable, 0x107dde4},         // rotate
        {cParticleSourceCommand::kVTable, 0x107dde8},         // source
        {cParticleEmitCommand::kVTable, 0x107ddec},           // emit
        {cParticleForceCommand::kVTable, 0x107ddf0},          // force
        {cParticleAttractorCommand::kVTable, 0x107ddf4},      // attractor
        {cParticleWarpCommand::kVTable, 0x107ddf8},           // warp
        {cParticleRandomWalkCommand::kVTable, 0x107ddfc},     // randomWalk
        {cParticleStretchCommand::kVTable, 0x107de00},        // stretch
        {cParticleLifeCommand::kVTable, 0x107de04},           // life
        {cParticleRateCommand::kVTable, 0x107de08},           // rate
        {cParticleInjectCommand::kVTable, 0x107de0c},         // inject
        {cParticleMaintainCommand::kVTable, 0x107de10},       // maintain
        {cParticleMaterialCommand::kVTable, 0x107de14},       // material
        {cParticleTextureCommand::kVTable, 0x107de1c},        // texture
        {cParticleShapeCommand::kVTable, 0x107de18},          // shape
        {cParticleFramesCommand::kVTable, 0x107de20},         // frames
        {cParticleAlignmentCommand::kVTable, 0x107de24},      // align
        {cParticleCollideCommand::kVTable, 0x107de28},        // collide
        {cParticleTerrainCollideCommand::kVTable, 0x107de2c}, // terrainCollide
        {cParticleTerrainRepelCommand::kVTable, 0x107de30},   // terrainRepel
        {cParticleDeathEffectCommand::kVTable, 0x107de3c},    // deathEffect
    };
    AddChildren(this, kFirst, std::size(kFirst));

    // The path block: the block base, its vtable, its kind word and its path (the game's constructor), its name.
    cParticlePathCommand *path = nullptr;
    if (void *memory = ::operator new(sizeof(cParticlePathCommand))) {
        path = static_cast<cParticlePathCommand *>(static_cast<cp::cBlockCommandBase *>(memory)->Construct());
        path->mVTable = GameAddress(cParticlePathCommand::kVTable);
        path->mKind = cParticlePathCommand::kPathKind;
        OriginalFunction<void *(__thiscall *)(void *), 0xbfa4a0>()(path->mPath);
    }
    path->mCommandName = NameAt(0x107de40); // "path"
    AddToBlock(this, path);

    static constexpr NamedChild kLast[] = {
        {cParticlePlaneCollisionCommand::kVTable, 0x107de38}, // collider-plane
        {cParticleMaterialCommand::kVTable, 0x107de34},       // billboardMaterial
    };
    AddChildren(this, kLast, std::size(kLast));
}

void cParticleEffectCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 3);
    mParticles.mName.assign(argv[0], std::strlen(argv[0]));
    {
        LocalDescription defaults;
        AssignDescription(&mParticles.mDescription, defaults.mBytes);
    }
    if (static_cast<int32_t>(count) > 1) {
        if (_stricmp(argv[1], Text(0xda9e7c)) != 0) cp::ThrowFormatted(Text(0xe8f6f4), Text(0xda9e7c)); // "Expecting '%s'", ":"
        if (static_cast<int32_t>(count) < 3) cp::ThrowFormatted(Text(0xe8f704)); // "Expecting description name"
        if (void *manager = Parser()->mDescriptions) {
            FoundComponent<ParticlesComponent> found;
            const bool ok = VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t, ParticlesComponent **), 8>(
                manager, argv[2], 1, &found.mPointer);
            if (!ok) cp::ThrowFormatted(Text(0xe8f720), argv[2]); // "Unknown description: %s"
            CopyDescription(&mParticles.mDescription, &found.mPointer->mDescription);
        }
    }
    ParserFace(mState).PushOpenBlock(this);
}

void cParticleEffectCommand::EndBlock()
{
    if (void *manager = Parser()->mDescriptions) {
        ParticlesComponent *component = nullptr;
        if (void *memory = ::operator new(sizeof(ParticlesComponent))) {
            component = static_cast<ParticlesComponent *>(memory);
            component->mVTable = GameAddress(ParticlesComponent::kInterfaceVTable);
            component->mRefCount.Construct();
            component->mVTable = GameAddress(ParticlesComponent::kVTable);
            component->mRefCount.mVTable = GameAddress(ParticlesComponent::kRefCountVTable);
            ConstructDescription(&component->mDescription);
        }
        CopyDescription(&component->mDescription, &mParticles.mDescription);
        FinishDescription(&component->mDescription);
        VirtualCall<void(__thiscall *)(void *, const char *, ParticlesComponent *), 7>(manager, mParticles.mName.c_str(),
                                                                                         component);
    }
    mParticles.mName.assign(Text(0xd9b9d0), 0);
}

cParticleEffectCommand *cParticleEffectCommand::DeletingDestructor(uint32_t flags)
{
    // The particles state (its name, then the description), then the block.
    mParticles.mName.~basic_string();
    DestroyDescription(&mParticles.mDescription);
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cParticleEffectCommand));
    return this;
}

// ---- path ------------------------------------------------------------------------------------------------------------

void cParticlePathCommand::SetContext(void *parser, void *state)
{
    // cBlockCommand's slot 3, then the state as given; the children's state is this block's +0x48.
    reinterpret_cast<cp::cBlockCommand *>(this)->StoreAndQuery(parser, reinterpret_cast<uint32_t>(state));
    mParticles = static_cast<ParticlesState *>(state);
    mValue = reinterpret_cast<uint32_t>(&mKind);

    static constexpr NamedChild kChildren[] = {
        {cPathPointCommand::kVTable, 0x107de44},      // p
        {cPathDeltaPointCommand::kVTable, 0x107de4c}, // dp
        {cPathVelocityCommand::kVTable, 0x107de54},   // v
        {cPathSpeedCommand::kVTable, 0x107de5c},      // s
        {cPathTimeCommand::kVTable, 0x107de64},       // t
        {cPathDeltaTimeCommand::kVTable, 0x107de6c},  // dt
        {cPathPointCommand::kVTable, 0x107de48},      // position
        {cPathDeltaPointCommand::kVTable, 0x107de50}, // deltaPosition
        {cPathVelocityCommand::kVTable, 0x107de58},   // velocity
        {cPathSpeedCommand::kVTable, 0x107de60},      // speed
        {cPathTimeCommand::kVTable, 0x107de68},       // time
        {cPathDeltaTimeCommand::kVTable, 0x107de70},  // deltaTime
    };
    AddChildren(this, kChildren, std::size(kChildren));
}

void cParticlePathCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    uint64_t &flags = mParticles->mDescription.mFlags;
    flags |= args->TakeSwitch(Text(0xe8f694)) ? ParticlesDescription::kTractorPath : ParticlesDescription::kFreePath;
    if (args->TakeSwitch(Text(0xe8f69c))) // "noSnap"
        flags &= ~ParticlesDescription::kSnapPath;
    else
        flags |= ParticlesDescription::kSnapPath;
    mPreExtend = args->TakeSwitch(Text(0xe8f6a4));  // "preExtend"
    mPostExtend = args->TakeSwitch(Text(0xe8f6b0)); // "postExtend"
    OriginalFunction<void(__thiscall *)(void *), 0xbfaaa0>()(mPath); // the path restarts
    FaceOf(reinterpret_cast<const cp::cCommand *>(this)).PushOpenBlock(this);
}

void cParticlePathCommand::EndBlock()
{
    ParticlesDescription &d = mParticles->mDescription;
    OriginalFunction<void(__thiscall *)(void *), 0xbfac30>()(mPath); // the path finishes
    // The description's path: emptied (the points, trivially destroyed, and the flags word; the game's body is the
    // library's std::vector<bool>::clear, so it is done here), then the path's points stored in it.
    d.mPathPoints.clear();
    d.mPathFlags = 0;
    OriginalFunction<void(__thiscall *)(void *, void *), 0xbfb1f0>()(mPath, &d.mPathPoints);
    d.mPathFlags = mPreExtend ? d.mPathFlags | 1 : d.mPathFlags & ~1u;
    d.mPathFlags = mPostExtend ? d.mPathFlags | 2 : d.mPathFlags & ~2u;
}

cParticlePathCommand *cParticlePathCommand::DeletingDestructor(uint32_t flags)
{
    OriginalFunction<void(__thiscall *)(void *), 0xbe19f0>()(mPath); // the path's destructor
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cParticlePathCommand));
    return this;
}

namespace {
// A path command's argument: a vector (face slot 35, at least one value), handed on as the pointer the face returns.
using PathVectorFn = void(__thiscall *)(void *, const float *);
void PathVector(const cPathCommand *command, cp::cIArguments *args, PathVectorFn add)
{
    const char *const *argv = args->PositionalExactly(1);
    float vector[3];
    const float *v = FaceOf(command).EvalVector3(vector, argv[0], 1);
    add(command->Path(), v);
}
// Or a float (face slot 31).
using PathFloatFn = void(__thiscall *)(void *, float);
void PathFloat(const cPathCommand *command, cp::cIArguments *args, PathFloatFn set)
{
    const char *const *argv = args->PositionalExactly(1);
    const float value = FaceOf(command).EvalFloat(argv[0]);
    set(command->Path(), value);
}
} // namespace

void cPathPointCommand::Execute(cp::cIArguments *args) { PathVector(this, args, OriginalFunction<PathVectorFn, 0xbfa890>()); }
void cPathDeltaPointCommand::Execute(cp::cIArguments *args) { PathVector(this, args, OriginalFunction<PathVectorFn, 0xbfa6b0>()); }
void cPathVelocityCommand::Execute(cp::cIArguments *args) { PathVector(this, args, OriginalFunction<PathVectorFn, 0xbfaa10>()); }
void cPathSpeedCommand::Execute(cp::cIArguments *args) { PathFloat(this, args, OriginalFunction<PathFloatFn, 0xbfa8f0>()); }
void cPathTimeCommand::Execute(cp::cIArguments *args) { PathFloat(this, args, OriginalFunction<PathFloatFn, 0xbfa980>()); }
void cPathDeltaTimeCommand::Execute(cp::cIArguments *args) { PathFloat(this, args, OriginalFunction<PathFloatFn, 0xbfa790>()); }

// ---- the particles block's commands ----------------------------------------------------------------------------------

namespace {
// The description's own parsing of a command's arguments (cTSBaseParticlesDescription's, not ours).
using ParseFn = void(__thiscall *)(void *, void *, cp::cIArguments *);
void DescriptionParses(const cParticleCommand *command, cp::cIArguments *args, ParseFn parse)
{
    parse(&command->Description(), command->mParser, args);
}

// The emission rate commands: the rate (the game's parse of it, with 30 as its default), then the switches every rate
// command takes (the game's 0xbe7020).
using RateFn = void(__cdecl *)(void *, cp::cIArguments *, void *, float);
void RateCommand(const cParticleCommand *command, cp::cIArguments *args, RateFn parse)
{
    parse(command->Description().mRate, args, command->mParser, 30.0f);
}
void RateSwitches(const cParticleCommand *command, cp::cIArguments *args)
{
    OriginalFunction<void(__cdecl *)(cp::cIArguments *, void *, void *), 0xbe7020>()(args, command->mParser, command->State());
}
} // namespace

void cParticleColorCommand::Execute(cp::cIArguments *args) { DescriptionParses(this, args, OriginalFunction<ParseFn, 0xbfbc50>()); }
void cParticleAlphaCommand::Execute(cp::cIArguments *args) { DescriptionParses(this, args, OriginalFunction<ParseFn, 0xbfbb60>()); }
void cParticleSizeCommand::Execute(cp::cIArguments *args) { DescriptionParses(this, args, OriginalFunction<ParseFn, 0xbfc150>()); }
void cParticleLifeCommand::Execute(cp::cIArguments *args) { DescriptionParses(this, args, OriginalFunction<ParseFn, 0xbfc0a0>()); }

void cParticleAspectCommand::Execute(cp::cIArguments *args)
{
    DescriptionParses(this, args, OriginalFunction<ParseFn, 0xbfbbf0>());
    Description().mFlags |= ParticlesDescription::kAspect;
}

void cParticleTerrainRepelCommand::Execute(cp::cIArguments *args)
{
    Description().mFlags |= ParticlesDescription::kTerrainRepel;
    DescriptionParses(this, args, OriginalFunction<ParseFn, 0xbfc1b0>());
}

void cParticleEmitCommand::Execute(cp::cIArguments *args)
{
    DescriptionParses(this, args, OriginalFunction<ParseFn, 0xbfbdd0>());
    ParticlesDescription &d = Description();
    if (args->TakeSwitch(Text(0xe8efdc))) d.mFlags |= ParticlesDescription::kBaseEmit;           // "base"
    if (args->TakeSwitch(Text(0xe8efe4))) d.mFlags |= ParticlesDescription::kRadialEmit;         // "radial"
    if (args->TakeSwitch(Text(0xe8efec))) d.mFlags |= ParticlesDescription::kScaleExisting;      // "scaleExisting"
    // Both emit volume values within 1e-6 of zero (compared as doubles).
    if (std::fabs(static_cast<double>(d.mEmitVolume[0])) < 1e-6 && std::fabs(static_cast<double>(d.mEmitVolume[1])) < 1e-6)
        d.mFlags |= ParticlesDescription::kNoEmitVolume;
}

void cParticleRateCommand::Execute(cp::cIArguments *args)
{
    RateCommand(this, args, OriginalFunction<RateFn, 0xbd1b70>());
    RateSwitches(this, args);
}

void cParticleInjectCommand::Execute(cp::cIArguments *args)
{
    RateCommand(this, args, OriginalFunction<RateFn, 0xbd1ce0>());
    Description().mFlags |= ParticlesDescription::kInject;
    RateSwitches(this, args);
}

void cParticleMaintainCommand::Execute(cp::cIArguments *args)
{
    RateCommand(this, args, OriginalFunction<RateFn, 0xbd1ce0>());
    Description().mFlags |= ParticlesDescription::kMaintain;
    RateSwitches(this, args);
}

void cParticleStretchCommand::Execute(cp::cIArguments *args)
{
    Description().mStretch = FaceOf(this).EvalFloat(args->PositionalExactly(1)[0]);
}

void cParticleDeathEffectCommand::Execute(cp::cIArguments *args)
{
    const char *name = args->PositionalExactly(1)[0];
    Description().mDeathEffect.assign(name, std::strlen(name));
}

void cParticleAlignmentCommand::Execute(cp::cIArguments *args)
{
    const char *name = args->PositionalExactly(1)[0];
    Description().mAlignment = static_cast<uint8_t>(GameAddress<const cp::EnumTable>(0x10fd18c)->Lookup(name));
}

void cParticleFramesCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    ParticlesDescription &d = Description();
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe49160), 1)) // "speed"
        d.mFrameSpeed = FaceOf(this).EvalFloat(a[0]);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f3f4), 1)) // "count"
        d.mFrameCount = static_cast<uint8_t>(FaceOf(this).EvalUint(a[0]));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f3fc), 1)) // "start"
        d.mFrameStart = static_cast<uint8_t>(FaceOf(this).EvalUint(a[0]));
}

// material / billboardMaterial and shape: a custom material and a shape exclude each other.
void cParticleMaterialCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    ParticlesDescription &d = Description();
    if (d.mFlags & ParticlesDescription::kShape)
        cp::ThrowFormatted(Text(0xe8f2a0), VirtualCall<const char *(__thiscall *)(const void *), 5>(this)); // "Cannot specify %s with a shape"
    d.mMaterial.assign(argv[0], std::strlen(argv[0]));
    d.mFlags |= ParticlesDescription::kMaterial;
    if (args->TakeSwitch(Text(0xdb6c88))) d.mFlags |= ParticlesDescription::kLit; // "light"
}

void cParticleShapeCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    ParticlesDescription &d = Description();
    if (d.mFlags & ParticlesDescription::kMaterial) cp::ThrowFormatted(Text(0xe8f39c)); // "Cannot specify shape with a custom material"
    d.mMaterial.assign(argv[0], std::strlen(argv[0]));
    d.mFlags |= ParticlesDescription::kShape;
    if (args->TakeSwitch(Text(0xdb6c88))) d.mFlags |= ParticlesDescription::kLit; // "light"
}

void cParticleRandomWalkCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    ParticlesDescription &d = Description();
    d.mFlags |= ParticlesDescription::kRandomWalk;
    uint32_t count;
    // -delay and -strength: a centre and an optional range (0 when not given), stored as (centre - range, range + centre).
    auto range = [&](uint32_t name, float *out) {
        if (const char *const *a = args->TakeSwitchArguments(Text(name), &count, 1, 0x7fffffff)) {
            const float centre = FaceOf(this).EvalFloat(a[0]);
            const float spread = static_cast<int32_t>(count) > 1 ? FaceOf(this).EvalFloat(a[1]) : 0.0f;
            out[0] = centre - spread;
            out[1] = spread + centre;
        }
    };
    range(0xe8ed58, d.mWalkDelay);    // "delay"
    range(0xe848b0, d.mWalkStrength); // "strength"
    // -turn: one or two values, each in [0, 1] (the parser's 0xaf6f80 checks the range); the second 0 when not given.
    if (const char *const *a = args->TakeSwitchArguments(Text(0xdb1d74), &count, 1, 0x7fffffff)) { // "turn"
        using RangeFn = float(__cdecl *)(void *, const char *, float, float);
        d.mWalkTurn[0] = OriginalFunction<RangeFn, 0xaf6f80>()(mParser, a[0], 0.0f, 1.0f);
        d.mWalkTurn[1] =
            static_cast<int32_t>(count) > 1 ? OriginalFunction<RangeFn, 0xaf6f80>()(mParser, a[1], 0.0f, 1.0f) : 0.0f;
    }
}

namespace {
// An attractor (0x34 bytes, cTSParticleAttractor): its constructor and copy are lane IX's entries; its parse and the
// vector's growth are the game's. Its destructor is inline in the original: it frees the vector at +0x20.
struct Attractor {
    uint8_t m0[0x20];
    std::vector<uint32_t> mValues; // +0x20
    uint8_t m2C[8];
};
static_assert(offsetof(Attractor, mValues) == 0x20 && sizeof(Attractor) == 0x34, "Attractor layout");

// A collider (0x84 bytes, cTSParticleCollider): constructor, destructor and copy are lane IX's entries; its parse and the
// vector's growth are the game's.
struct Collider {
    uint32_t m0;
    std::string mName;  // +4
    uint8_t m1C[0x30];  // +0x1c the plane's three vectors (+0x1c, +0x28, +0x34)
    float mBounce;      // +0x4c (1 for a collider-plane)
    uint8_t m50[0x34];
};
static_assert(offsetof(Collider, mName) == 4 && offsetof(Collider, mBounce) == 0x4c && sizeof(Collider) == 0x84,
              "Collider layout");

struct LocalCollider {
    alignas(8) uint8_t mBytes[sizeof(Collider)];
    LocalCollider() { Parts::ConstructCollider(mBytes, nullptr); }
    ~LocalCollider() { Parts::DestroyCollider(reinterpret_cast<cTSParticleCollider *>(mBytes), nullptr); }
    Collider &Get() { return *reinterpret_cast<Collider *>(mBytes); }
    LocalCollider(const LocalCollider &) = delete;
    LocalCollider &operator=(const LocalCollider &) = delete;
};

// push_back of an element the game copies: in place when there is room (its copy constructor), else the vector's
// reallocating insert at the end, which is library code (0xbe09e0, 0xbe0b90): the toolchain's vector of the real element,
// on operator new as the original's.
using CopyFn = void *(*)(void *, const void *);
template <typename T> void PushBack(RawVector &v, const T &element, CopyFn copy)
{
    if (v.mEnd != v.mCapacity) {
        copy(v.mEnd, &element);
        v.mEnd += sizeof(T);
    } else {
        reinterpret_cast<std::vector<T> *>(&v)->push_back(element);
    }
}
void *CopyCollider(void *to, const void *from)
{
    return Parts::CopyCollider(to, nullptr, static_cast<const cTSParticleCollider *>(from));
}
void *CopyAttractor(void *to, const void *from)
{
    return Parts::CopyAttractor(to, nullptr, static_cast<const cTSParticleAttractor *>(from));
}
void PushCollider(BaseParticlesDescription &d, const LocalCollider &c)
{
    PushBack(d.mColliders, *reinterpret_cast<const cTSParticleCollider *>(c.mBytes), &CopyCollider);
}
void ClearColliders(BaseParticlesDescription &d)
{
    for (uint8_t *p = d.mColliders.mBegin; p != d.mColliders.mEnd; p += sizeof(Collider))
        Parts::DestroyCollider(reinterpret_cast<cTSParticleCollider *>(p), nullptr);
    d.mColliders.mEnd = d.mColliders.mBegin;
}
void ParseCollider(void *face, cp::cIArguments *args, LocalCollider &c)
{
    OriginalFunction<void(__cdecl *)(void *, cp::cIArguments *, void *), 0xbdf9c0>()(face, args, c.mBytes);
}
} // namespace

void cParticleAttractorCommand::Execute(cp::cIArguments *args)
{
    alignas(8) uint8_t bytes[sizeof(Attractor)];
    auto *attractor = reinterpret_cast<Attractor *>(Parts::ConstructAttractor(bytes, nullptr));
    struct Destroy {
        Attractor *a;
        ~Destroy() { a->mValues.~vector(); }
    } destroy{attractor};
    OriginalFunction<void(__thiscall *)(void *, void *, cp::cIArguments *, uint32_t), 0xbde020>()(attractor, mParser, args, 0);
    ParticlesDescription &d = Description();
    PushBack(d.mAttractors, *reinterpret_cast<const cTSParticleAttractor *>(attractor), &CopyAttractor);
    d.mFlags |= ParticlesDescription::kAttractor;
}

void cParticleCollideCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    LocalCollider collider;
    collider.Get().mName.assign(argv[0], std::strlen(argv[0]));
    ParseCollider(mParser, args, collider);
    ParticlesDescription &d = Description();
    if (args->TakeSwitch(Text(0xdbbfc8))) ClearColliders(d); // "reset": the colliders so far go
    PushCollider(d, collider);
    d.mFlags |= ParticlesDescription::kCollide;
}

void cParticlePlaneCollisionCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(3);
    LocalCollider collider;
    collider.Get().mName.assign(Text(0xe8ce88), 5); // "plane"
    collider.Get().mBounce = 1.0f;
    ParseCollider(mParser, args, collider);
    // The plane: three vectors (face slot 35, at least one value each), copied from the pointers the face returns.
    for (int i = 0; i < 3; ++i) {
        float vector[3];
        const float *v = FaceOf(this).EvalVector3(vector, argv[i], 1);
        std::memcpy(collider.Get().m1C + 0xc * i, v, 0xc);
    }
    PushCollider(Description(), collider);
}

// ---- terrainCollide, texture, rotate, source, force, warp ----------------------------------------------------------

namespace {
// The parser's float in a range (0xaf6f80, cdecl: face, text, least, most; throws outside it).
float Ranged(const cp::cCommand *command, const char *text)
{
    return OriginalFunction<float(__cdecl *)(void *, const char *, float, float), 0xaf6f80>()(command->mParser, text, 0.0f, 1.0f);
}
} // namespace

void cParticleTerrainCollideCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    ParticlesDescription &d = Description();
    d.mFlags |= ParticlesDescription::kTerrainCollide;
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e9b0), 1)) d.mBounce = FaceOf(this).EvalFloat(a[0]); // "bounce"
    if (args->TakeSwitch(Text(0xe8f488))) d.mFlags |= ParticlesDescription::kPinToTerrain;       // "pinToTerrain"
    if (args->TakeSwitch(Text(0xe8f498))) d.mFlags |= ParticlesDescription::kSticky;             // "sticky"
    if (args->TakeSwitch(Text(0xe8f4a0))) d.mFlags |= ParticlesDescription::kKillOutsideTerrain; // "killOutsideTerrain"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e994), 1)) d.mDeath = Ranged(this, a[0]); // "death"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f4b4), 1)) d.mDeathByWater = Ranged(this, a[0]); // "deathByWater"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f4c4), 1)) { // "limitHeight": the flag first
        d.mFlags |= ParticlesDescription::kLimitHeight;
        d.mLimitHeight = FaceOf(this).EvalFloat(a[0]);
    }
}

void cParticleTextureCommand::Execute(cp::cIArguments *args)
{
    // The texture name may be left out once a name is set.
    uint32_t count = 1;
    const char *const *argv = Description().mMaterial.size() == 0 ? args->PositionalExactly(1) : args->Positional(&count, 0, 1);
    ParticlesDescription &d = Description();
    if (d.mFlags & ParticlesDescription::kShape)
        cp::ThrowFormatted(Text(0xe8f2a0), VirtualCall<const char *(__thiscall *)(const void *), 5>(this)); // "Cannot specify %s with a shape"
    if (d.mFlags & ParticlesDescription::kMaterial)
        cp::ThrowFormatted(Text(0xe8f2ec), VirtualCall<const char *(__thiscall *)(const void *), 5>(this)); // "... with a custom material"
    if (static_cast<int32_t>(count) > 0) d.mMaterial.assign(argv[0], std::strlen(argv[0]));
    d.mFlags &= ~(ParticlesDescription::kAcceptOverride | ParticlesDescription::kAcceptComposite);
    if (const char *const *a = args->TakeSwitchArguments(Text(0xdf75c4), &count, 1, 2)) { // "tile": one or two counts
        d.mTile[0] = static_cast<uint8_t>(FaceOf(this).EvalUint(a[0]));
        d.mTile[1] = static_cast<int32_t>(count) > 1 ? static_cast<uint8_t>(FaceOf(this).EvalUint(a[1])) : 1;
        if ((d.mTile[0] & (d.mTile[0] - 1)) != 0 || (d.mTile[1] & (d.mTile[1] - 1)) != 0)
            cp::ThrowFormatted(Text(0xe8f318)); // "Tile counts must be factors of two"
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f33c), 1)) // "draw"
        d.mDraw = static_cast<uint8_t>(GameAddress<const cp::EnumTable>(0x10fd180)->Lookup(a[0]));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xdba3c8), 1)) // "layer"
        d.mLayer = static_cast<float>(FaceOf(this).EvalInt(a[0]));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f344), 1)) { // "directLayer"
        d.mLayer = static_cast<float>(FaceOf(this).EvalInt(a[0]));
        d.mFlags |= ParticlesDescription::kDirectLayer;
    }
    if (args->TakeSwitch(Text(0xdb6c88))) d.mFlags |= ParticlesDescription::kLit;             // "light"
    if (args->TakeSwitch(Text(0xe8f350))) d.mFlags |= ParticlesDescription::kAcceptOverride;  // "acceptOverride"
    if (args->TakeSwitch(Text(0xe8f360))) d.mFlags |= ParticlesDescription::kAcceptComposite; // "acceptComposite"
}

void cParticleRotateCommand::Execute(cp::cIArguments *args)
{
    ParticlesDescription &d = Description();
    // The rotation curve of this rotate (the first or the second): the game's curve parse (cdecl: curve, args, face, least).
    OriginalFunction<void(__cdecl *)(void *, cp::cIArguments *, void *, uint32_t), 0xbd1a50>()(d.mRotateCurve[d.mRotateCount], args,
                                                                                             mParser, 0);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ee50), 1)) { // "vary": a range around 1, or none
        const float vary = Ranged(this, a[0]);
        float *range = d.mRotateVary[d.mRotateCount];
        if (vary > 0.0f) {
            range[0] = -vary;
            range[1] = vary;
        } else {
            range[0] = 1.0f;
            range[1] = 1.0f;
        }
    }
    uint32_t count;
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8ee58), &count, 2, 0x7fffffff)) { // "multVaryRange"
        float pair[2];
        const char *const *argv = a;
        const float *p = OriginalFunction<float *(__cdecl *)(float *, void *, const char *const *const *, uint32_t), 0xaf6f20>()(
            pair, mParser, &argv, count);
        d.mRotateVary[d.mRotateCount][0] = p[0];
        d.mRotateVary[d.mRotateCount][1] = p[1];
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e244), 1)) // "offset"
        d.mRotateOffset[d.mRotateCount] = FaceOf(this).EvalFloat(a[0]);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ee68), 1)) { // "axis": shapes only, at most two
        if (!(d.mFlags & ParticlesDescription::kShape)) cp::ThrowFormatted(Text(0xe8ee70));
        if (d.mRotateCount > 1) cp::ThrowFormatted(Text(0xe8eea0)); // "Too many rotate commands - limit of 2"
        float vector[3];
        const float *v = FaceOf(this).EvalVector3(vector, a[0], 1);
        std::memcpy(d.mRotateAxis[d.mRotateCount], v, sizeof(float) * 3);
        ++d.mRotateCount;
    }
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8eac4), &count, 1, 0x7fffffff)) { // "pivot": the list anew
        d.mPivots.clear();
        for (uint32_t i = 0; i < count; ++i) {
            float vector[3];
            const float *v = FaceOf(this).EvalVector3(vector, a[i], 1);
            d.mPivots.push_back(Vec3{v[0], v[1], v[2]});
        }
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8eec8), 1)) { // "pivotVary"
        float vector[3];
        const float *v = FaceOf(this).EvalVector3(vector, a[0], 1);
        std::memcpy(d.mPivotVary, v, sizeof(float) * 3);
    }
}

namespace {
// The source's box: grown by a half-size on each axis (z by zero for the flat shapes).
struct SourceBox {
    float lo[3], hi[3];
    void Grow(float x, float y, float z)
    {
        lo[0] = lo[0] - x;
        lo[1] = lo[1] - y;
        lo[2] = lo[2] - z;
        hi[0] = hi[0] + x;
        hi[1] = hi[1] + y;
        hi[2] = hi[2] + z;
    }
};
} // namespace

void cParticleSourceCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    ParticlesDescription &d = Description();
    const float *origin = GameAddress<const float>(0x10fd1a4); // the game's zero vector
    SourceBox box{{origin[0], origin[1], origin[2]}, {origin[0], origin[1], origin[2]}};
    d.mFlags &= ~ParticlesDescription::kRoundSource;
    ParserFace face = FaceOf(this);
    float vector[3];
    // One shape (the first given): a point; a square, quad, cube or box; a circle, sphere, ellipse or ellipsoid (round);
    // a ring or torus (with a width in [0, 1]).
    if (!args->TakeSwitch(Text(0xe79c04))) {                                  // "point"
        if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef00), 1)) { // "square"
            const float r = face.EvalFloat(a[0]);
            box.Grow(r, r, 0.0f);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef08), 1)) { // "quad"
            face.EvalVector2(vector, a[0], 1);
            box.Grow(vector[0], vector[1], 0.0f);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef10), 1)) { // "cube"
            const float r = face.EvalFloat(a[0]);
            box.Grow(r, r, r);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef18), 1)) { // "box"
            face.EvalVector3(vector, a[0], 1);
            box.Grow(vector[0], vector[1], vector[2]);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef1c), 1)) { // "circle"
            d.mFlags |= ParticlesDescription::kRoundSource;
            const float r = face.EvalFloat(a[0]);
            box.Grow(r, r, 0.0f);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e98c), 1)) { // "sphere"
            d.mFlags |= ParticlesDescription::kRoundSource;
            const float r = face.EvalFloat(a[0]);
            box.Grow(r, r, r);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef24), 1)) { // "ellipse"
            d.mFlags |= ParticlesDescription::kRoundSource;
            face.EvalVector3(vector, a[0], 1);
            box.Grow(vector[0], vector[1], vector[2]);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef2c), 1)) { // "ellipsoid"
            d.mFlags |= ParticlesDescription::kRoundSource;
            face.EvalVector3(vector, a[0], 1);
            box.Grow(vector[0], vector[1], vector[2]);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe52420), 2)) { // "ring"
            const float r = face.EvalFloat(a[0]);
            d.mSourceRing = Ranged(this, a[1]);
            box.Grow(r, r, 0.0f);
        } else if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef38), 2)) { // "torus"
            face.EvalVector3(vector, a[0], 1);
            d.mSourceRing = Ranged(this, a[1]);
            box.Grow(vector[0], vector[1], vector[2]);
        } else {
            cp::ThrowFormatted(Text(0xe8ef40)); // "no source shape specified"
        }
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e244), 1)) { // "offset": the box moves
        const float *v = face.EvalVector3(vector, a[0], 1);
        for (int k = 0; k < 3; ++k) {
            box.lo[k] = v[k] + box.lo[k];
            box.hi[k] = v[k] + box.hi[k];
        }
    }
    std::memcpy(d.mSourceMax, box.hi, sizeof(box.hi));
    std::memcpy(d.mSourceMin, box.lo, sizeof(box.lo));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe856fc), 1)) d.mConstrain.assign(a[0], std::strlen(a[0])); // "constrain"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef5c), 1)) { // "constrainOffset"
        const float *v = face.EvalVector3(vector, a[0], 1);
        std::memcpy(d.mConstrainOffset, v, sizeof(float) * 3);
    }
    if (args->TakeSwitch(Text(0xe8ef6c))) d.mFlags |= ParticlesDescription::kScaleParticles; // "scaleParticles"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef7c), 1)) {        // "belowHeight"
        d.mFlags |= ParticlesDescription::kHeightRange;
        d.mHeightRange[1] = face.EvalFloat(a[0]);
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef88), 1)) { // "aboveHeight"
        d.mFlags |= ParticlesDescription::kHeightRange;
        d.mHeightRange[0] = face.EvalFloat(a[0]);
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef94), 2)) { // "heightRange"
        d.mFlags |= ParticlesDescription::kHeightRange;
        d.mHeightRange[0] = face.EvalFloat(a[0]);
        d.mHeightRange[1] = face.EvalFloat(a[1]);
    }
    if (args->TakeSwitch(Text(0xe8efa0))) d.mFlags |= ParticlesDescription::kResetIncoming; // "resetIncoming"
}

namespace {
// The screw switches force and warp share: its speed (in 1/50ths), radius and angle curves (the game's curve parse, cdecl:
// curve, values, count, face, default), their speed tables (the parser's table 0x10fd198), variations and the radius cap.
void ScrewSwitches(const cParticleCommand *command, cp::cIArguments *args)
{
    using CurveFn = void(__cdecl *)(void *, const char *const *, uint32_t, void *, float);
    ParticlesDescription &d = command->Description();
    const cp::EnumTable *speeds = GameAddress<const cp::EnumTable>(0x10fd198);
    uint32_t count;
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f044), 1)) // "screw"
        d.mScrew = FaceOf(command).EvalFloat(a[0]) * 0.02f;
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8f04c), &count, 1, 0x7fffffff)) // "screwRadius"
        OriginalFunction<CurveFn, 0xbd1be0>()(d.mScrewRadius, a, count, command->mParser, 0.0f);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f058), 1)) // "screwRadiusToSpeed"
        d.mScrewRadiusToSpeed = static_cast<uint8_t>(speeds->Lookup(a[0]));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f06c), 1)) d.mScrewRadiusVary = Ranged(command, a[0]); // "screwRadiusVary"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f07c), 1)) { // "screwRadiusMax": a negative one is 100
        float most = FaceOf(command).EvalFloat(a[0]);
        if (0.0f > most) most = 100.0f;
        d.mScrewRadiusMax = most;
    }
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8f08c), &count, 1, 0x7fffffff)) // "screwAngle"
        OriginalFunction<CurveFn, 0xbd1be0>()(d.mScrewAngle, a, count, command->mParser, 0.0f);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f098), 1)) // "screwAngleToSpeed"
        d.mScrewAngleToSpeed = static_cast<uint8_t>(speeds->Lookup(a[0]));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f0ac), 1)) d.mScrewAngleVary = Ranged(command, a[0]); // "screwAngleVary"
}

// A float in [0, 1] (about) to a byte: rounded to 1/256ths through the float's bits (adding 49152 leaves the value in
// the low bits), then the 256 of a full 1 taken back to 255.
uint8_t ByteOf(float scaled)
{
    uint32_t bits;
    std::memcpy(&bits, &scaled, 4);
    bits += 0xb8c00000u;
    return static_cast<uint8_t>(static_cast<uint8_t>(bits) - static_cast<uint8_t>(bits >> 8));
}
// The bloom values: the first plus 49152; the second scaled by 1/16 first (in double, as the original's x87 does).
uint8_t BloomFirst(float v) { return ByteOf(static_cast<float>(static_cast<double>(v) + 49152.0)); }
uint8_t BloomSecond(float v) { return ByteOf(static_cast<float>(static_cast<double>(v) * 0.0625 + 49152.0)); }

// warp -wiggle and -wiggleDir (both blocks' warp): a warp force each.
void WiggleSwitches(const cEffectsCommand *command, BaseParticlesDescription &d, cp::cIArguments *args)
{
    ParserFace face = FaceOf(command);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f134), 4)) { // "wiggle": period, ..., angle
        WarpForce wiggle{};
        wiggle.v[3] = face.EvalFloat(a[0]);
        wiggle.v[0] = face.EvalFloat(a[1]);
        const float scale = face.EvalFloat(a[2]);
        const double angle = static_cast<double>(Ranged(command, a[3])) * static_cast<double>(6.2831855f);
        wiggle.v[4] = static_cast<float>(std::cos(angle)) * scale;
        wiggle.v[5] = static_cast<float>(std::sin(angle)) * -scale;
        d.mWarps.push_back(wiggle);
    }
    uint32_t count;
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8f13c), &count, 2, 3)) { // "wiggleDir"
        WarpForce wiggle;
        wiggle.v[0] = face.EvalFloat(a[0]);
        float vector[3];
        const float *v = face.EvalVector3(vector, a[1], 1);
        std::memcpy(&wiggle.v[4], v, sizeof(float) * 3);
        if (static_cast<int32_t>(count) > 2) {
            float second[3];
            const float *w = face.EvalVector3(second, a[2], 1);
            std::memcpy(&wiggle.v[1], w, sizeof(float) * 3);
        } else {
            wiggle.v[1] = wiggle.v[2] = wiggle.v[3] = 0.0f;
        }
        d.mWarps.push_back(wiggle);
    }
}
} // namespace

void cParticleWarpCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    ScrewSwitches(this, args);
    ParticlesDescription &d = Description();
    ParserFace face = FaceOf(this);
    WiggleSwitches(this, d, args);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f148), 2)) { // "screenBloom": both pairs
        d.mBloom[1] = BloomFirst(face.EvalFloat(a[0]));
        d.mBloom[0] = BloomSecond(face.EvalFloat(a[1]));
        d.mBloom[3] = BloomFirst(face.EvalFloat(a[0]));
        d.mBloom[2] = BloomSecond(face.EvalFloat(a[1]));
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f154), 2)) { // "bloomAlpha"
        d.mBloom[1] = BloomFirst(face.EvalFloat(a[0]));
        d.mBloom[0] = BloomSecond(face.EvalFloat(a[1]));
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f160), 2)) { // "bloomSize"
        d.mBloom[3] = BloomFirst(face.EvalFloat(a[0]));
        d.mBloom[2] = BloomSecond(face.EvalFloat(a[1]));
    }
}

namespace {
// An attractor on the stack: its constructor (0xbddd10's entry); its inline destructor frees the values.
struct LocalAttractor {
    alignas(8) uint8_t mBytes[sizeof(Attractor)];
    LocalAttractor() { Parts::ConstructAttractor(mBytes, nullptr); }
    ~LocalAttractor() { Get().mValues.~vector(); }
    Attractor &Get() { return *reinterpret_cast<Attractor *>(mBytes); }
    LocalAttractor(const LocalAttractor &) = delete;
    LocalAttractor &operator=(const LocalAttractor &) = delete;
};
// An attractor's preset curve: its constructor 0xbdc8f0 (through the hook's adapter, so a test can stand in for it); the
// values at +4 freed inline.
struct LocalCurve {
    alignas(8) uint8_t mBytes[0x20];
    LocalCurve() { cTSEffectCurveAdapters::Construct(mBytes, nullptr); }
    ~LocalCurve() { reinterpret_cast<std::vector<float> *>(mBytes + 4)->~vector(); }
    cTSEffectCurve &Get() { return *reinterpret_cast<cTSEffectCurve *>(mBytes); }
    LocalCurve(const LocalCurve &) = delete;
    LocalCurve &operator=(const LocalCurve &) = delete;
};
// The attractor added to the description's (the flag is the caller's).
void AddAttractor(BaseParticlesDescription &d, LocalAttractor &a)
{
    PushBack(d.mAttractors, *reinterpret_cast<const cTSParticleAttractor *>(a.mBytes), &CopyAttractor);
}
void PushAttractor(ParticlesDescription &d, LocalAttractor &a)
{
    AddAttractor(d, a);
    d.mFlags |= ParticlesDescription::kAttractor;
}
} // namespace

void cParticleForceCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    ParticlesDescription &d = Description();
    ParserFace face = FaceOf(this);
    if (args->TakeSwitch(Text(0xdbbfc8))) d.mForce[0] = d.mForce[1] = d.mForce[2] = 0.0f; // "reset"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f028), 1)) {     // "gravity": down
        const float g = face.EvalFloat(a[0]);
        d.mForce[0] = d.mForce[0] + 0.0f;
        d.mForce[1] = d.mForce[1] + 0.0f;
        d.mForce[2] = d.mForce[2] - g;
    }
    uint32_t count;
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8f030), &count, 1, 0x7fffffff)) { // "wind": a vector, a strength
        float wind[3];
        face.EvalVector3(wind, a[0], 1);
        if (static_cast<int32_t>(count) > 1) {
            const float strength = face.EvalFloat(a[1]);
            for (float &f : wind) f = f * strength;
        }
        for (int k = 0; k < 3; ++k) d.mForce[k] = wind[k] + d.mForce[k];
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f038), 1)) d.mGlobalWind = face.EvalFloat(a[0]); // "globalWind"
    ScrewSwitches(this, args);
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8f0bc), &count, 1, 0x7fffffff)) { // "bomb": a strength, a centre
        d.mBomb = face.EvalFloat(a[0]);
        if (static_cast<int32_t>(count) >= 2) {
            float vector[3];
            const float *v = face.EvalVector3(vector, a[1], 1);
            std::memcpy(d.mBombCentre, v, sizeof(float) * 3);
        }
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f0c4), 1)) d.mDrag = face.EvalFloat(a[0]); // "drag"

    LocalAttractor attractor;
    // -attractor ...: the attractor's own switches (the game's parse answers whether there was one).
    if (OriginalFunction<bool(__thiscall *)(void *, void *, cp::cIArguments *, const char *), 0xbde020>()(attractor.mBytes, mParser, args,
                                                                                                      Text(0xe8eadc))) // "attractor"
        PushAttractor(d, attractor);
    OriginalFunction<void(__thiscall *)(void *), 0xbdded0>()(attractor.mBytes); // reset
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8f0cc), &count, 2, 0x7fffffff)) { // "presetAttractor": strength, curve
        OriginalFunction<void(__thiscall *)(void *, float), 0x4b21f0>()(attractor.mBytes, face.EvalFloat(a[0]));
        std::vector<float> values;
        for (int32_t i = 1; i < static_cast<int32_t>(count); ++i) values.push_back(face.EvalFloat(a[i]));
        LocalCurve curve;
        curve.Get().SetValues(values);
        Parts::SetCurve(reinterpret_cast<cTSParticleAttractor *>(attractor.mBytes), nullptr, &curve.Get());
        PushAttractor(d, attractor);
    }
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8eb40), &count, 2, 5)) { // "terrainRepel": two to five values
        d.mTerrainRepel[0] = face.EvalFloat(a[0]);
        d.mTerrainRepel[1] = face.EvalFloat(a[1]);
        d.mFlags |= ParticlesDescription::kTerrainRepel;
        for (int32_t i = 2; i < 5 && static_cast<int32_t>(count) > i; ++i) d.mTerrainRepel[i] = face.EvalFloat(a[i]);
    }
}

// ==== the meta-particle block ============================================================================================

namespace {

// ---- the meta description (cTSMetaParticlesDescription, src/ts/cTSParticlesDescription.h: lane IX's entries) ----------

void ConstructMetaDescription(void *at) { Parts::ConstructMeta(at, nullptr); }
void DestroyMetaDescription(void *at) { Parts::DestroyMeta(static_cast<cTSMetaParticlesDescription *>(at), nullptr); }
// Takes another description's values (the base a block names, or the block's own when it is registered).
void CopyMetaDescription(void *to, const void *from)
{
    Parts::CopyMeta(static_cast<cTSMetaParticlesDescription *>(to), nullptr, static_cast<const cTSMetaParticlesDescription *>(from));
}

struct LocalMetaDescription {
    alignas(8) uint8_t mBytes[sizeof(MetaParticlesDescription)];
    LocalMetaDescription() { ConstructMetaDescription(mBytes); }
    ~LocalMetaDescription() { DestroyMetaDescription(mBytes); }
    MetaParticlesDescription &Get() { return *reinterpret_cast<MetaParticlesDescription *>(mBytes); }
    LocalMetaDescription(const LocalMetaDescription &) = delete;
    LocalMetaDescription &operator=(const LocalMetaDescription &) = delete;
};

// The description's move assignment (inline in the original): the base's assignment (0xbe1df0's entry), then the
// meta part: the rotate values and the effect name moved (the source's left empty), the rest copied.
void MoveMetaDescription(MetaParticlesDescription &to, MetaParticlesDescription &from)
{
    Parts::AssignBase(reinterpret_cast<cTSBaseParticlesDescription *>(&to), nullptr,
                      reinterpret_cast<const cTSBaseParticlesDescription *>(&from));
    to.mFlags = from.mFlags;
    to.mRotateCurve = from.mRotateCurve;
    to.mRotateValues = std::move(from.mRotateValues);
    to.mRotateVary = from.mRotateVary;
    to.mRotateOffset = from.mRotateOffset;
    to.mEffect = std::move(from.mEffect);
    static_assert(offsetof(MetaParticlesDescription, m1E4) - offsetof(MetaParticlesDescription, mWalks) == 0x54, "copied part");
    std::memcpy(to.mWalks, from.mWalks, offsetof(MetaParticlesDescription, m1E4) - offsetof(MetaParticlesDescription, mWalks));
}

// cTSCompDescriptionBaseT<cTSMetaParticlesDescription, 2> (0x1f8 bytes): what the description manager keeps.
struct MetaParticlesComponent {
    static constexpr uint32_t kInterfaceVTable = 0xe8ecb8; // cITSComponentDescription's (set first)
    static constexpr uint32_t kVTable = 0xe8fb30;
    static constexpr uint32_t kRefCountVTable = 0xe8fb58;

    const void *mVTable;                   // +0
    UtilObjectA mRefCount;                 // +4 cRZRefCount (vtable, count)
    uint32_t mC;                           // +0xc (not set)
    MetaParticlesDescription mDescription; // +0x10
};
static_assert(offsetof(MetaParticlesComponent, mDescription) == 0x10 && sizeof(MetaParticlesComponent) == 0x1f8,
              "MetaParticlesComponent layout");

} // namespace

// ---- metaParticles ---------------------------------------------------------------------------------------------------

void cMetaParticleEffectCommand::SetContext(void *parser, void *stateInterface)
{
    // As the particles block's: cBlockCommandT's slot 3, the children's state the block's own.
    SetContextAdjusted(parser, stateInterface);
    mValue = reinterpret_cast<uint32_t>(&mMeta);

    static constexpr NamedChild kFirst[] = {
        {kMetaParticleColorVTable, 0x107e2ac},             // color
        {kMetaParticleColorVTable, 0x107e2b4},             // color255
        {kMetaParticleColorVTable, 0x107e2b0},             // colour
        {kMetaParticleColorVTable, 0x107e2b8},             // colour255
        {kMetaParticleAlphaVTable, 0x107e2bc},             // alpha
        {kMetaParticleAlphaVTable, 0x107e2c0},             // alpha255
        {kMetaParticleSizeVTable, 0x107e2c4},              // size
        {cMetaParticleAspectCommand::kVTable, 0x107e2c8},  // aspect
        {cMetaParticleRotateCommand::kVTable, 0x107e2cc},  // rotate
        {cMetaParticleSourceCommand::kVTable, 0x107e2d0},  // source
        {cMetaParticleEmitCommand::kVTable, 0x107e2d4},    // emit
        {cMetaParticleForceCommand::kVTable, 0x107e2d8},   // force
        {cMetaParticleAttractorCommand::kVTable, 0x107e2dc}, // attractor
    };
    AddChildren(this, kFirst, std::size(kFirst));

    // The path block: the block base, its vtable, its kind word and its path (the game's constructor), its name.
    cMetaParticlePathCommand *path = nullptr;
    if (void *memory = ::operator new(sizeof(cMetaParticlePathCommand))) {
        path = static_cast<cMetaParticlePathCommand *>(static_cast<cp::cBlockCommandBase *>(memory)->Construct());
        path->mVTable = GameAddress(cMetaParticlePathCommand::kVTable);
        path->mKind = cMetaParticlePathCommand::kPathKind;
        OriginalFunction<void *(__thiscall *)(void *), 0xbfa4a0>()(path->mPath);
    }
    path->mCommandName = NameAt(0x107e2e0); // "path"
    AddToBlock(this, path);

    static constexpr NamedChild kLast[] = {
        {cMetaParticleWarpCommand::kVTable, 0x107e314},           // warp
        {cMetaParticleRandomWalkCommand::kVTable, 0x107e318},     // randomWalk
        {kMetaParticleLifeVTable, 0x107e31c},                     // life
        {cMetaParticleRateCommand::kVTable, 0x107e320},           // rate
        {cMetaParticleInjectCommand::kVTable, 0x107e324},         // inject
        {cMetaParticleMaintainCommand::kVTable, 0x107e328},       // maintain
        {cMetaParticleAlignmentCommand::kVTable, 0x107e32c},      // align
        {cMetaParticleCollisionCommand::kVTable, 0x107e330},      // collide
        {cMetaParticleTerrainCollideCommand::kVTable, 0x107e334}, // terrainCollide
        {cMetaParticleTerrainRepelCommand::kVTable, 0x107e338},   // terrainRepel
        {cMetaParticleEffectBaseCommand::kVTable, 0x107e33c},     // effect
    };
    AddChildren(this, kLast, std::size(kLast));
}

void cMetaParticleEffectCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 3);
    mMeta.mName.assign(argv[0], std::strlen(argv[0]));
    MetaParticlesDescription &d = mMeta.mDescription;
    {
        LocalMetaDescription defaults;
        MoveMetaDescription(d, defaults.Get());
    }
    if (static_cast<int32_t>(count) > 1) {
        if (_stricmp(argv[1], Text(0xda9e7c)) != 0) cp::ThrowFormatted(Text(0xe8f6f4), Text(0xda9e7c)); // "Expecting '%s'", ":"
        if (static_cast<int32_t>(count) < 3) cp::ThrowFormatted(Text(0xe8f704)); // "Expecting description name"
        if (void *manager = Parser()->mDescriptions) {
            FoundComponent<MetaParticlesComponent> found;
            const bool ok = VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t, MetaParticlesComponent **), 8>(
                manager, argv[2], 2, &found.mPointer);
            if (!ok) cp::ThrowFormatted(Text(0xe8f720), argv[2]); // "Unknown description: %s"
            CopyMetaDescription(&d, &found.mPointer->mDescription);
        }
    }
    // The tractor path goes on from the description's last point's time; the time scale and the random walks start anew.
    mMeta.mTime = d.mPathPoints.empty() ? 0.0f : d.mPathPoints.back().mTime;
    mMeta.mTimeScale = 1.0f;
    mMeta.mWalkIndex = 0;
    FaceOf(reinterpret_cast<const cp::cCommand *>(this)).PushOpenBlock(this);
}

void cMetaParticleEffectCommand::EndBlock()
{
    if (void *manager = Parser()->mDescriptions) {
        MetaParticlesComponent *component = nullptr;
        if (void *memory = ::operator new(sizeof(MetaParticlesComponent))) {
            component = static_cast<MetaParticlesComponent *>(memory);
            component->mVTable = GameAddress(MetaParticlesComponent::kInterfaceVTable);
            component->mRefCount.Construct();
            component->mVTable = GameAddress(MetaParticlesComponent::kVTable);
            component->mRefCount.mVTable = GameAddress(MetaParticlesComponent::kRefCountVTable);
            ConstructMetaDescription(&component->mDescription);
        }
        CopyMetaDescription(&component->mDescription, &mMeta.mDescription);
        OriginalFunction<void(__thiscall *)(void *), 0xbefb90>()(&component->mDescription); // the finishing pass
        VirtualCall<void(__thiscall *)(void *, const char *, MetaParticlesComponent *), 7>(Parser()->mDescriptions,
                                                                                             mMeta.mName.c_str(), component);
    }
    mMeta.mName.assign(Text(0xd9b9d0), 0);
}

cMetaParticleEffectCommand *cMetaParticleEffectCommand::DeletingDestructor(uint32_t flags)
{
    mMeta.mName.~basic_string();
    DestroyMetaDescription(&mMeta.mDescription);
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cMetaParticleEffectCommand));
    return this;
}

// ---- the meta block's path ---------------------------------------------------------------------------------------------

void cMetaParticlePathCommand::SetContext(void *parser, void *state)
{
    reinterpret_cast<cp::cBlockCommand *>(this)->StoreAndQuery(parser, reinterpret_cast<uint32_t>(state));
    mMeta = static_cast<MetaParticlesState *>(state);
    mValue = reinterpret_cast<uint32_t>(&mKind);

    static constexpr NamedChild kChildren[] = {
        {kMetaPathVTables[0], 0x107e2e4}, // p
        {kMetaPathVTables[1], 0x107e2ec}, // dp
        {kMetaPathVTables[2], 0x107e2f4}, // v
        {kMetaPathVTables[3], 0x107e2fc}, // s
        {kMetaPathVTables[4], 0x107e304}, // t
        {kMetaPathVTables[5], 0x107e30c}, // dt
        {kMetaPathVTables[0], 0x107e2e8}, // position
        {kMetaPathVTables[1], 0x107e2f0}, // deltaPosition
        {kMetaPathVTables[2], 0x107e2f8}, // velocity
        {kMetaPathVTables[3], 0x107e300}, // speed
        {kMetaPathVTables[4], 0x107e308}, // time
        {kMetaPathVTables[5], 0x107e310}, // deltaTime
    };
    AddChildren(this, kChildren, std::size(kChildren));
}

void cMetaParticlePathCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    mTractor = args->TakeSwitch(Text(0xe8f694)); // "tractor"
    OriginalFunction<void(__thiscall *)(void *), 0xbfaaa0>()(mPath); // the path restarts
    FaceOf(reinterpret_cast<const cp::cCommand *>(this)).PushOpenBlock(this);
}

void cMetaParticlePathCommand::EndBlock()
{
    MetaParticlesDescription &d = mMeta->mDescription;
    OriginalFunction<void(__thiscall *)(void *), 0xbfac30>()(mPath); // the path finishes
    d.mPathPoints.clear(); // as the particles path's end (the game's body is the library's)
    d.mPathFlags = 0;
    OriginalFunction<void(__thiscall *)(void *, void *), 0xbfb1f0>()(mPath, &d.mPathPoints);
    d.mFlags |= MetaParticlesDescription::kPath;
    if (!mTractor) d.mFlags |= MetaParticlesDescription::kFreePath;
}

// ---- the meta block's commands -------------------------------------------------------------------------------------------

namespace {
using MetaParseFn = void(__thiscall *)(void *, void *, cp::cIArguments *);
void MetaDescriptionParses(const cMetaParticleCommand *command, cp::cIArguments *args, MetaParseFn parse)
{
    parse(&command->Description(), command->mParser, args);
}
// The rate's own switches (the game's 0xbf0ef0: args, description, face).
void MetaRateSwitches(const cMetaParticleCommand *command, cp::cIArguments *args)
{
    OriginalFunction<void(__cdecl *)(cp::cIArguments *, void *, void *), 0xbf0ef0>()(args, &command->Description(), command->mParser);
}
// inject / maintain: a rate (the rate curve's constant, 0xbdcd10) from the one argument.
void MetaRate(const cMetaParticleCommand *command, cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    const float rate = FaceOf(command).EvalFloat(argv[0]);
    reinterpret_cast<cTSEffectCurve *>(command->Description().mRate)->SetConstant(rate);
}
} // namespace

void cMetaParticleAspectCommand::Execute(cp::cIArguments *args) { MetaDescriptionParses(this, args, OriginalFunction<MetaParseFn, 0xbfbbf0>()); }

void cMetaParticleTerrainRepelCommand::Execute(cp::cIArguments *args)
{
    Description().mFlags |= MetaParticlesDescription::kTerrainRepel;
    MetaDescriptionParses(this, args, OriginalFunction<MetaParseFn, 0xbfc1b0>());
}

void cMetaParticleEmitCommand::Execute(cp::cIArguments *args)
{
    MetaDescriptionParses(this, args, OriginalFunction<MetaParseFn, 0xbfbdd0>());
    if (args->TakeSwitch(Text(0xe8efdc))) Description().mFlags |= MetaParticlesDescription::kBaseEmit; // "base"
}

void cMetaParticleEffectBaseCommand::Execute(cp::cIArguments *args)
{
    const char *name = args->PositionalExactly(1)[0];
    Description().mEffect.assign(name, std::strlen(name));
}

void cMetaParticleRateCommand::Execute(cp::cIArguments *args)
{
    using RateFn = void(__cdecl *)(void *, cp::cIArguments *, void *, float);
    OriginalFunction<RateFn, 0xbd1b70>()(Description().mRate, args, mParser, 30.0f);
    MetaRateSwitches(this, args);
}

void cMetaParticleInjectCommand::Execute(cp::cIArguments *args)
{
    MetaRate(this, args);
    Description().mFlags |= MetaParticlesDescription::kInject;
    MetaRateSwitches(this, args);
}

void cMetaParticleMaintainCommand::Execute(cp::cIArguments *args)
{
    MetaRate(this, args);
    MetaParticlesDescription &d = Description();
    d.mFlags |= MetaParticlesDescription::kMaintain;
    uint32_t count;
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8ed58), &count, 1, 2)) { // "delay": the parser's pair
        float pair[2];
        const char *const *values = a;
        const float *p = OriginalFunction<float *(__cdecl *)(float *, void *, const char *const *const *, uint32_t), 0xaf6f20>()(
            pair, mParser, &values, count);
        std::memcpy(d.mRate + 0x18, p, sizeof(float) * 2);
    }
}

void cMetaParticleRotateCommand::Execute(cp::cIArguments *args)
{
    MetaParticlesDescription &d = Description();
    OriginalFunction<void(__cdecl *)(void *, cp::cIArguments *, void *, uint32_t), 0xbd1a50>()(&d.mRotateCurve, args, mParser, 0);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ee50), 1)) d.mRotateVary = Ranged(this, a[0]); // "vary"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e244), 1)) // "offset"
        d.mRotateOffset = FaceOf(this).EvalFloat(a[0]);
}

void cMetaParticleAlignmentCommand::Execute(cp::cIArguments *args)
{
    const char *name = args->PositionalExactly(1)[0];
    MetaParticlesDescription &d = Description();
    d.mAlignment = static_cast<uint8_t>(GameAddress<const cp::EnumTable>(0x10fd204)->Lookup(name));
    ParserFace face = FaceOf(this);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8fff8), 1)) d.mDamp = face.EvalFloat(a[0]); // "damp"
    const char *const *bank = args->TakeSwitchArgumentsExactly(Text(0xe90000), 2); // "bank", or "windBank" (flagged)
    if (!bank) {
        bank = args->TakeSwitchArgumentsExactly(Text(0xe90008), 2);
        if (!bank) return;
        d.mFlags |= MetaParticlesDescription::kWindBank;
    }
    d.mBank[0] = face.EvalFloat(bank[0]);
    d.mBank[1] = face.EvalFloat(bank[1]);
}

void cMetaParticleAttractorCommand::Execute(cp::cIArguments *args)
{
    LocalAttractor attractor;
    // The parse's answer is not looked at: the attractor is added either way.
    OriginalFunction<bool(__thiscall *)(void *, void *, cp::cIArguments *, const char *), 0xbde020>()(attractor.mBytes, mParser, args,
                                                                                                   nullptr);
    MetaParticlesDescription &d = Description();
    AddAttractor(d, attractor);
    d.mFlags |= MetaParticlesDescription::kAttractor;
}

namespace {
// The terrain switches of collide (without a name) and terrainCollide.
void MetaTerrainSwitches(const cMetaParticleCommand *command, cp::cIArguments *args)
{
    MetaParticlesDescription &d = command->Description();
    ParserFace face = FaceOf(command);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e9b0), 1)) d.mBounce = face.EvalFloat(a[0]); // "bounce"
    if (args->TakeSwitch(Text(0xe8f488))) d.mFlags |= MetaParticlesDescription::kPinToTerrain;       // "pinToTerrain"
    if (args->TakeSwitch(Text(0xe8f498))) d.mFlags |= MetaParticlesDescription::kSticky;             // "sticky"
    if (args->TakeSwitch(Text(0xe8f4a0))) d.mFlags |= MetaParticlesDescription::kKillOutsideTerrain; // "killOutsideTerrain"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e994), 1)) d.mDeath = Ranged(command, a[0]); // "death"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f4b4), 1)) d.mDeathByWater = Ranged(command, a[0]); // "deathByWater"
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f4c4), 1)) { // "limitHeight": the flag first
        d.mFlags |= MetaParticlesDescription::kLimitHeight;
        d.mLimitHeight = face.EvalFloat(a[0]);
    }
}
} // namespace

void cMetaParticleCollisionCommand::Execute(cp::cIArguments *args)
{
    // `collide name ... -reset` adds a collider; `collide` alone is terrainCollide.
    uint32_t count;
    const char *const *argv = args->Positional(&count, 0, 1);
    MetaParticlesDescription &d = Description();
    if (static_cast<int32_t>(count) > 0) {
        d.mFlags |= MetaParticlesDescription::kCollide;
        LocalCollider collider;
        collider.Get().mName.assign(argv[0], std::strlen(argv[0]));
        ParseCollider(mParser, args, collider);
        if (args->TakeSwitch(Text(0xdbbfc8))) ClearColliders(d); // "reset"
        PushCollider(d, collider);
    } else {
        d.mFlags |= MetaParticlesDescription::kTerrainCollide;
        MetaTerrainSwitches(this, args);
    }
}

void cMetaParticleTerrainCollideCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    Description().mFlags |= MetaParticlesDescription::kTerrainCollide;
    MetaTerrainSwitches(this, args);
}

void cMetaParticleRandomWalkCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    MetaParticlesDescription &d = Description();
    d.mFlags |= args->TakeSwitch(Text(0xe8fefc)) ? MetaParticlesDescription::kSyncWalk : MetaParticlesDescription::kRandomWalk; // "sync"
    // This command's walk: one per randomWalk in the block.
    // ORIGINAL-BUG: the index is not bounded; a third randomWalk in a block writes past the two walks (over preferDir,
    // align's values and, from the fourth, the state after the description).
    MetaWalk *walk = reinterpret_cast<MetaWalk *>(reinterpret_cast<uint8_t *>(d.mWalks) + State()->mWalkIndex * sizeof(MetaWalk));
    ParserFace face = FaceOf(this);
    uint32_t count;
    // -delay and -strength: a centre and an optional range (0 when not given), stored as (centre - range, range + centre).
    auto range = [&](uint32_t name, float *out) {
        if (const char *const *a = args->TakeSwitchArguments(Text(name), &count, 1, 2)) {
            const float centre = face.EvalFloat(a[0]);
            const float spread = static_cast<int32_t>(count) > 1 ? face.EvalFloat(a[1]) : 0.0f;
            out[0] = centre - spread;
            out[1] = spread + centre;
        }
    };
    range(0xe8ed58, walk->mDelay);    // "delay"
    range(0xe848b0, walk->mStrength); // "strength"
    if (const char *const *a = args->TakeSwitchArguments(Text(0xdb1d74), &count, 1, 2)) { // "turn": one or two in [0, 1]
        walk->mTurn[0] = Ranged(this, a[0]);
        walk->mTurn[1] = static_cast<int32_t>(count) > 1 ? Ranged(this, a[1]) : 0.0f;
    }
    const char *const *mix = args->TakeSwitchArgumentsExactly(Text(0xe8ff04), 1); // "mix"
    if (mix) walk->mMix = Ranged(this, mix[0]);
    if (args->TakeSwitch(Text(0xe8ff08))) d.mFlags |= MetaParticlesDescription::kWalkWait; // "wait"
    if (args->TakeSwitch(Text(0xe8ff10))) { // "preferDir"
        // ORIGINAL-BUG: -preferDir takes no value; the vector is read from -mix's argument (none given: a null read).
        const char *text = *static_cast<const char *const volatile *>(mix);
        float vector[3];
        const float *v = face.EvalVector3(vector, text, 1);
        std::memcpy(d.mPreferDir, v, sizeof(d.mPreferDir));
    }
    ++State()->mWalkIndex;
}

void cMetaParticleWarpCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    MetaParticlesDescription &d = Description();
    ParserFace face = FaceOf(this);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f044), 1)) d.mScrew = face.EvalFloat(a[0]); // "screw"
    WiggleSwitches(this, d, args);
    // The bloom values: each plus 49152 (no 1/16 here).
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f154), 2)) { // "bloomAlpha"
        d.mBloom[1] = BloomFirst(face.EvalFloat(a[0]));
        d.mBloom[0] = BloomFirst(face.EvalFloat(a[1]));
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f160), 2)) { // "bloomSize"
        d.mBloom[3] = BloomFirst(face.EvalFloat(a[0]));
        d.mBloom[2] = BloomFirst(face.EvalFloat(a[1]));
    }
}

void cMetaParticleSourceCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    MetaParticlesDescription &d = Description();
    const float *origin = GameAddress<const float>(0x10fd210); // the game's zero vector (another than the particles')
    SourceBox box{{origin[0], origin[1], origin[2]}, {origin[0], origin[1], origin[2]}};
    d.mFlags &= ~MetaParticlesDescription::kRoundSource;
    ParserFace face = FaceOf(this);
    float vector[3];
    uint32_t count;
    // One shape (the first given), as the particles source's, or terrain: -terrain / -terrainWindySide with an optional
    // height range (the parser's pair) for the box's z.
    if (!args->TakeSwitch(Text(0xe79c04))) { // "point"
        const char *const *a;
        if ((a = args->TakeSwitchArgumentsExactly(Text(0xe8ef00), 1)) != nullptr) { // "square"
            const float r = face.EvalFloat(a[0]);
            box.Grow(r, r, 0.0f);
        } else if ((a = args->TakeSwitchArgumentsExactly(Text(0xe8ef08), 1)) != nullptr) { // "quad"
            face.EvalVector2(vector, a[0], 1);
            box.Grow(vector[0], vector[1], 0.0f);
        } else if ((a = args->TakeSwitchArgumentsExactly(Text(0xe8ef10), 1)) != nullptr) { // "cube"
            const float r = face.EvalFloat(a[0]);
            box.Grow(r, r, r);
        } else if ((a = args->TakeSwitchArgumentsExactly(Text(0xe8ef18), 1)) != nullptr) { // "box"
            face.EvalVector3(vector, a[0], 1);
            box.Grow(vector[0], vector[1], vector[2]);
        } else if ((a = args->TakeSwitchArgumentsExactly(Text(0xe8ef1c), 1)) != nullptr) { // "circle"
            d.mFlags |= MetaParticlesDescription::kRoundSource;
            const float r = face.EvalFloat(a[0]);
            box.Grow(r, r, 0.0f);
        } else if ((a = args->TakeSwitchArgumentsExactly(Text(0xe8e98c), 1)) != nullptr) { // "sphere"
            d.mFlags |= MetaParticlesDescription::kRoundSource;
            const float r = face.EvalFloat(a[0]);
            box.Grow(r, r, r);
        } else if ((a = args->TakeSwitchArgumentsExactly(Text(0xe8ef24), 1)) != nullptr ||  // "ellipse"
                   (a = args->TakeSwitchArgumentsExactly(Text(0xe8ef2c), 1)) != nullptr) { // "ellipsoid"
            d.mFlags |= MetaParticlesDescription::kRoundSource;
            face.EvalVector3(vector, a[0], 1);
            box.Grow(vector[0], vector[1], vector[2]);
        } else if ((a = args->TakeSwitchArgumentsExactly(Text(0xe52420), 2)) != nullptr) { // "ring"
            const float r = face.EvalFloat(a[0]);
            d.mSourceRing = Ranged(this, a[1]);
            box.Grow(r, r, 0.0f);
        } else if ((a = args->TakeSwitchArgumentsExactly(Text(0xe8ef38), 2)) != nullptr) { // "torus"
            face.EvalVector3(vector, a[0], 1);
            d.mSourceRing = Ranged(this, a[1]);
            box.Grow(vector[0], vector[1], vector[2]);
        } else {
            if ((a = args->TakeSwitchArguments(Text(0xdb30f4), &count, 0, 2)) != nullptr) { // "terrain"
                d.mFlags |= MetaParticlesDescription::kTerrainSource;
            } else if ((a = args->TakeSwitchArguments(Text(0xe8fc74), &count, 0, 2)) != nullptr) { // "terrainWindySide"
                d.mFlags |= MetaParticlesDescription::kTerrainSource | MetaParticlesDescription::kWindySide;
            } else {
                cp::ThrowFormatted(Text(0xe8ef40)); // "no source shape specified"
            }
            if (static_cast<int32_t>(count) > 0) {
                float pair[2];
                const char *const *values = a;
                OriginalFunction<float *(__cdecl *)(float *, void *, const char *const *const *, uint32_t), 0xaf6f20>()(
                    pair, mParser, &values, count);
                box.lo[2] = pair[0]; // read from the pair given, not the pointer answered
                box.hi[2] = pair[1];
            }
        }
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e244), 1)) { // "offset": the box moves
        const float *v = face.EvalVector3(vector, a[0], 1);
        for (int k = 0; k < 3; ++k) {
            box.lo[k] = v[k] + box.lo[k];
            box.hi[k] = v[k] + box.hi[k];
        }
    }
    std::memcpy(d.mSourceMax, box.hi, sizeof(box.hi));
    std::memcpy(d.mSourceMin, box.lo, sizeof(box.lo));
    if (args->TakeSwitch(Text(0xe8ef6c))) d.mFlags |= MetaParticlesDescription::kScaleParticles;     // "scaleParticles"
    if (args->TakeSwitch(Text(0xe8f488))) d.mFlags |= MetaParticlesDescription::kSourcePinToTerrain; // "pinToTerrain"
    if (args->TakeSwitch(Text(0xe8fc88))) d.mFlags |= MetaParticlesDescription::kPinToWater;         // "pinToWater"
    if (args->TakeSwitch(Text(0xe8fc94))) { // "terrainOnly"
        d.mFlags |= MetaParticlesDescription::kHeightRange;
        d.mHeightRange[0] = 0.0f;
    }
    if (args->TakeSwitch(Text(0xe8fca0))) { // "waterOnly"
        d.mFlags |= MetaParticlesDescription::kHeightRange;
        d.mHeightRange[1] = 0.0f;
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef7c), 1)) { // "belowHeight"
        d.mFlags |= MetaParticlesDescription::kHeightRange;
        d.mHeightRange[1] = face.EvalFloat(a[0]);
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef88), 1)) { // "aboveHeight"
        d.mFlags |= MetaParticlesDescription::kHeightRange;
        d.mHeightRange[0] = face.EvalFloat(a[0]);
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ef94), 2)) { // "heightRange"
        d.mFlags |= MetaParticlesDescription::kHeightRange;
        d.mHeightRange[0] = face.EvalFloat(a[0]);
        d.mHeightRange[1] = face.EvalFloat(a[1]);
    }
    if (args->TakeSwitch(Text(0xe8f4a0))) d.mFlags |= MetaParticlesDescription::kKillOutsideTerrain; // "killOutsideTerrain"
    if (args->TakeSwitch(Text(0xe8efa0))) d.mFlags |= MetaParticlesDescription::kResetIncoming;      // "resetIncoming"
}

void cMetaParticleForceCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    MetaParticlesState &s = *State();
    MetaParticlesDescription &d = s.mDescription;
    ParserFace face = FaceOf(this);
    if (args->TakeSwitch(Text(0xdbbfc8))) { // "reset": the force and the path
        d.mForce[0] = d.mForce[1] = d.mForce[2] = 0.0f;
        d.mPathPoints.clear(); // (the game's body is the library's)
        d.mPathFlags = 0;
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f028), 1)) { // "gravity": down
        const float g = face.EvalFloat(a[0]);
        d.mForce[0] = d.mForce[0] + 0.0f;
        d.mForce[1] = d.mForce[1] + 0.0f;
        d.mForce[2] = d.mForce[2] - g;
    }
    uint32_t count;
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8f030), &count, 1, 2)) { // "wind": a direction, a strength
        float vector[3];
        const float *v = face.EvalVector3(vector, a[0], 1);
        float wind[3] = {v[0], v[1], v[2]};
        const float lengthSquared = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (static_cast<double>(lengthSquared) > 1e-6) { // the direction made a unit vector
            const float length = static_cast<float>(std::sqrt(static_cast<double>(lengthSquared)));
            for (float &f : wind) f = f / length;
        }
        if (static_cast<int32_t>(count) > 1) {
            const float strength = face.EvalFloat(a[1]);
            for (float &f : wind) f = f * strength;
        }
        for (int k = 0; k < 3; ++k) d.mForce[k] = wind[k] + d.mForce[k];
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f038), 1)) d.mGlobalWind = face.EvalFloat(a[0]); // "globalWind"
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8f0bc), &count, 1, 2)) { // "bomb": a strength, a centre
        d.mBomb = face.EvalFloat(a[0]);
        if (static_cast<int32_t>(count) > 1) {
            float vector[3];
            const float *v = face.EvalVector3(vector, a[1], 1);
            std::memcpy(d.mBombCentre, v, sizeof(float) * 3);
        }
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f0c4), 1)) d.mDrag = face.EvalFloat(a[0]); // "drag"

    LocalAttractor attractor;
    // -attractor ...: the attractor's own switches (the game's parse answers whether there was one).
    if (OriginalFunction<bool(__thiscall *)(void *, void *, cp::cIArguments *, const char *), 0xbde020>()(attractor.mBytes, mParser, args,
                                                                                                      Text(0xe8eadc))) { // "attractor"
        AddAttractor(d, attractor);
        d.mFlags |= MetaParticlesDescription::kAttractor;
    }

    // -tractor / -tractorRel position, speed-or-velocity [, time]: a point added to the path. A one-value second argument
    // is a speed towards the position (from the last point, or from the source's centre); a vector is the velocity.
    // -tractorRel's position is relative to the last point. Speeds and times are in the block's time scale.
    bool relative = true;
    const char *const *t = args->TakeSwitchArguments(Text(0xe8fd04), &count, 2, 3); // "tractorRel"
    if (!t) {
        relative = false;
        t = args->TakeSwitchArguments(Text(0xe8f694), &count, 2, 3); // "tractor"
    }
    if (t) {
        const float timeScale = s.mTimeScale;
        PathPoint point;
        point.mTime = 0.0f;
        float vector[3];
        const float *p = face.EvalVector3(vector, t[0], 1);
        float position[3] = {p[0], p[1], p[2]};
        float velocity[3];
        if (OriginalFunction<int32_t(__cdecl *)(const char *), 0xaf6a80>()(t[1]) == 1) { // how many values the text has
            const float speed = face.EvalFloat(t[1]) / timeScale;
            float delta[3];
            if (!d.mPathPoints.empty()) {
                const PathPoint &last = d.mPathPoints.back();
                for (int k = 0; k < 3; ++k) delta[k] = position[k] - last.mPosition[k];
            } else {
                for (int k = 0; k < 3; ++k) delta[k] = position[k] - (d.mSourceMin[k] + d.mSourceMax[k]) * 0.5f;
            }
            std::memcpy(velocity, delta, sizeof(delta));
            const float lengthSquared = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
            if (lengthSquared > 0.0f) {
                const float length = static_cast<float>(std::sqrt(static_cast<double>(lengthSquared)));
                const float k = speed / length;
                for (int i = 0; i < 3; ++i) velocity[i] = k * delta[i];
            }
        } else {
            float second[3];
            const float *v = face.EvalVector3(second, t[1], 1);
            for (int k = 0; k < 3; ++k) velocity[k] = v[k] / timeScale;
        }
        if (relative && !d.mPathPoints.empty()) {
            const PathPoint &last = d.mPathPoints.back();
            for (int k = 0; k < 3; ++k) position[k] = position[k] + last.mPosition[k];
        }
        if (static_cast<int32_t>(count) > 2) { // a time step: the path's time goes on
            const float step = face.EvalFloat(t[2]) * timeScale;
            s.mTime = step + s.mTime;
            point.mTime = s.mTime;
        }
        std::memcpy(point.mPosition, position, sizeof(position));
        std::memcpy(point.mVelocity, velocity, sizeof(velocity));
        d.mPathPoints.push_back(point);
        d.mFlags |= MetaParticlesDescription::kPath;
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8fd10), 1)) // "tractorResetSpeed"
        d.mTractorResetSpeed = face.EvalFloat(a[0]);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8fd24), 1)) { // "tractorTimeScale": times so far rescaled
        const float scale = face.EvalFloat(a[0]);
        s.mTimeScale = scale;
        s.mTime = scale * s.mTime;
        d.mLife[0] = scale * d.mLife[0];
        d.mLife[1] = scale * d.mLife[1];
        for (size_t i = 0; i < d.mPathPoints.size(); ++i) {
            PathPoint &point = d.mPathPoints[i];
            for (float &f : point.mVelocity) f = f / scale;
            point.mTime = scale * point.mTime;
        }
    }
    if (args->TakeSwitch(Text(0xe5b998))) d.mFlags |= MetaParticlesDescription::kFreePath; // "path"
    if (const char *const *a = args->TakeSwitchArguments(Text(0xe8eb40), &count, 2, 5)) { // "terrainRepel": two to five values
        d.mTerrainRepel[0] = face.EvalFloat(a[0]);
        d.mTerrainRepel[1] = face.EvalFloat(a[1]);
        d.mFlags |= MetaParticlesDescription::kTerrainRepel;
        for (int32_t i = 2; i < 5 && static_cast<int32_t>(count) > i; ++i) d.mTerrainRepel[i] = face.EvalFloat(a[i]);
    }
}

void cGroupMetaParticlesCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    // The parser's group handling (thiscall on the parser state: args, name, kind 2 = meta particles, 0).
    OriginalFunction<void(__thiscall *)(void *, cp::cIArguments *, const char *, uint32_t, uint32_t), 0xbcd940>()(mState, args, argv[0], 2,
                                                                                                                 0);
}

// ---- the decal description (cTSDecalDescription, src/ts/cTSCompDescription.h) ---------------------------------------------

namespace {
using DecalAdapters = cTSCompDescriptionAdapters<cTSDecalComponentDescription>;
void ConstructDecalDescription(void *at) { DecalAdapters::ConstructDescription(at, nullptr); }
void DestroyDecalDescription(void *at) { DecalAdapters::DestroyDescription(static_cast<cTSDecalDescription *>(at), nullptr); }
void CopyDecalDescription(void *to, const void *from)
{
    DecalAdapters::CopyDescription(static_cast<cTSDecalDescription *>(to), nullptr, static_cast<const cTSDecalDescription *>(from));
}
void MoveDecalDescription(void *to, void *from)
{
    DecalAdapters::MoveDescription(static_cast<cTSDecalDescription *>(to), nullptr, static_cast<cTSDecalDescription *>(from));
}

struct LocalDecalDescription {
    alignas(8) uint8_t mBytes[sizeof(DecalDescription)];
    LocalDecalDescription() { ConstructDecalDescription(mBytes); }
    ~LocalDecalDescription() { DestroyDecalDescription(mBytes); }
    LocalDecalDescription(const LocalDecalDescription &) = delete;
    LocalDecalDescription &operator=(const LocalDecalDescription &) = delete;
};

// cTSCompDescriptionBaseT<cTSDecalDescription> (0x98 bytes): the description at +0xc.
struct DecalComponent {
    static constexpr uint32_t kInterfaceVTable = 0xe8ecb8; // cITSComponentDescription's (set first)
    static constexpr uint32_t kVTable = 0xe8f8f4;
    static constexpr uint32_t kRefCountVTable = 0xe8f91c;

    const void *mVTable;           // +0
    UtilObjectA mRefCount;         // +4
    DecalDescription mDescription; // +0xc
};
static_assert(offsetof(DecalComponent, mDescription) == 0xc && sizeof(DecalComponent) == 0x98, "DecalComponent layout");
} // namespace

// ---- decal ----------------------------------------------------------------------------------------------------------------

void cDecalEffectCommand::SetContext(void *parser, void *stateInterface)
{
    SetContextAdjusted(parser, stateInterface);
    mValue = reinterpret_cast<uint32_t>(&mDecal);
    static constexpr NamedChild kChildren[] = {
        {cDecalColorCommand::kVTable, 0x107e1b8},   // color
        {cDecalColorCommand::kVTable, 0x107e1c0},   // color255
        {cDecalColorCommand::kVTable, 0x107e1bc},   // colour
        {cDecalColorCommand::kVTable, 0x107e1c4},   // colour255
        {cDecalAlphaCommand::kVTable, 0x107e1c8},   // alpha
        {cDecalAlphaCommand::kVTable, 0x107e1cc},   // alpha255
        {cDecalSizeCommand::kVTable, 0x107e1d0},    // size
        {cDecalAspectCommand::kVTable, 0x107e1d8},  // aspect
        {cDecalRotateCommand::kVTable, 0x107e1d4},  // rotate
        {cDecalLifeCommand::kVTable, 0x107e1dc},    // life
        {cDecalTextureCommand::kVTable, 0x107e1e0}, // texture
    };
    AddChildren(this, kChildren, std::size(kChildren));
}

void cDecalEffectCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 3);
    mName.assign(argv[0], std::strlen(argv[0]));
    {
        // The defaults: a description built and moved in (the description's move assignment, 0xbeb5a0).
        LocalDecalDescription defaults;
        MoveDecalDescription(&mDecal, defaults.mBytes);
    }
    if (static_cast<int32_t>(count) > 1) {
        if (_stricmp(argv[1], Text(0xda9e7c)) != 0) cp::ThrowFormatted(Text(0xe8f6f4), Text(0xda9e7c)); // "Expecting '%s'", ":"
        if (static_cast<int32_t>(count) < 3) cp::ThrowFormatted(Text(0xe8f704)); // "Expecting description name"
        if (void *manager = Parser()->mDescriptions) {
            FoundComponent<DecalComponent> found;
            const bool ok = VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t, DecalComponent **), 8>(
                manager, argv[2], 3, &found.mPointer);
            if (!ok) cp::ThrowFormatted(Text(0xe8f720), argv[2]); // "Unknown description: %s"
            CopyDecalDescription(&mDecal, &found.mPointer->mDescription);
        }
    }
    FaceOf(reinterpret_cast<const cp::cCommand *>(this)).PushOpenBlock(this);
}

void cDecalEffectCommand::EndBlock()
{
    void *manager = Parser()->mDescriptions;
    if (!manager) return;
    DecalComponent *component = nullptr;
    if (void *memory = ::operator new(sizeof(DecalComponent))) {
        component = static_cast<DecalComponent *>(memory);
        component->mVTable = GameAddress(DecalComponent::kInterfaceVTable);
        component->mRefCount.Construct();
        component->mVTable = GameAddress(DecalComponent::kVTable);
        component->mRefCount.mVTable = GameAddress(DecalComponent::kRefCountVTable);
        ConstructDecalDescription(&component->mDescription);
    }
    CopyDecalDescription(&component->mDescription, &mDecal);
    VirtualCall<void(__thiscall *)(void *, const char *, DecalComponent *), 7>(Parser()->mDescriptions, mName.c_str(), component);
}

cDecalEffectCommand *cDecalEffectCommand::DeletingDestructor(uint32_t flags)
{
    mName.~basic_string();
    DestroyDecalDescription(&mDecal);
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cDecalEffectCommand));
    return this;
}

// ---- the decal block's commands ------------------------------------------------------------------------------------------

namespace {
// color255 / alpha255: the command's name (its first word) ends in "255". As the original, a name shorter than three
// characters throws out_of_range (never one the block makes).
bool NamedFor255(cp::cIArguments *args)
{
    const std::string name(args->Argument(0));
    return name.substr(name.size() - 3) == "255";
}
using CurveParseFn = void(__cdecl *)(void *, cp::cIArguments *, void *, uint32_t);
void ParseCurve(const cDecalCommand *command, void *curve, cp::cIArguments *args)
{
    OriginalFunction<CurveParseFn, 0xbd1a50>()(curve, args, command->mParser, 0);
}
// -vary: a ranged float.
void Vary(const cDecalCommand *command, cp::cIArguments *args, float &vary)
{
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ee50), 1)) vary = Ranged(command, a[0]); // "vary"
}
} // namespace

void cDecalColorCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 0x7fffffff);
    std::vector<Vec3> &colors = Description().mColors;
    colors.clear();
    ParserFace face = FaceOf(this);
    for (uint32_t i = 0; i < count; ++i) {
        Vec3 color;
        const float *c = face.EvalColorRGB(&color.x, argv[i]);
        colors.push_back({c[0], c[1], c[2]});
    }
    if (NamedFor255(args)) {
        constexpr float scale = 1.0f / 255.0f;
        for (Vec3 &c : colors) {
            c.x *= scale;
            c.y *= scale;
            c.z *= scale;
        }
    }
}

void cDecalAlphaCommand::Execute(cp::cIArguments *args)
{
    DecalDescription &d = Description();
    ParseCurve(this, d.mAlpha, args);
    Vary(this, args, d.mAlphaVary);
    if (NamedFor255(args)) reinterpret_cast<cTSEffectCurve *>(d.mAlpha)->Divide(255.0f); // the curve scaled (0xbdc9f0)
}

void cDecalSizeCommand::Execute(cp::cIArguments *args)
{
    DecalDescription &d = Description();
    ParseCurve(this, d.mSize, args);
    Vary(this, args, d.mSizeVary);
    if (args->TakeSwitch(Text(0xe8f9b8))) d.mFlags |= DecalDescription::kTerrainScale; // "terrainScale"
}

void cDecalAspectCommand::Execute(cp::cIArguments *args) { ParseCurve(this, Description().mAspect, args); }

void cDecalRotateCommand::Execute(cp::cIArguments *args)
{
    DecalDescription &d = Description();
    ParseCurve(this, d.mRotate, args);
    Vary(this, args, d.mRotateVary);
}

void cDecalLifeCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 0, 0x7fffffff);
    DecalDescription &d = Description();
    if (static_cast<int32_t>(count) > 0) d.mLife = FaceOf(this).EvalFloat(argv[0]);
    if (args->TakeSwitch(Text(0xe8fa4c))) { // "static"
        d.mFlags |= DecalDescription::kStatic;
        d.mLifeMode = 0;
    }
    if (args->TakeSwitch(Text(0xe8ed1c))) d.mLifeMode = 0; // "loop"
    if (args->TakeSwitch(Text(0xe8ed24))) d.mLifeMode = 1; // "single"
    if (args->TakeSwitch(Text(0xe8ed2c))) d.mLifeMode = 2; // "sustain"
}

void cDecalTextureCommand::Execute(cp::cIArguments *args)
{
    // The texture's name only when the description has none yet (a base's is kept: then no name may be given).
    DecalDescription &d = Description();
    const bool unnamed = d.mTexture.empty();
    const char *const *argv = args->PositionalExactly(unnamed ? 1 : 0);
    if (unnamed) d.mTexture.assign(argv[0], std::strlen(argv[0]));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8f33c), 1)) // "draw"
        d.mDraw = static_cast<uint8_t>(GameAddress<const cp::EnumTable>(0x10fd1e8)->Lookup(a[0]));
    if (args->TakeSwitch(Text(0xdb6c88))) d.mFlags |= DecalDescription::kLight; // "light"
    if (args->TakeSwitch(Text(0xe8e530))) d.mFlags |= DecalDescription::kWater; // "water"
    if (args->TakeSwitch(Text(0xe52420))) d.mFlags |= DecalDescription::kRing;  // "ring"
    ParserFace face = FaceOf(this);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8fa80), 1)) { // "repeat"
        d.mFlags |= DecalDescription::kRepeat;
        d.mRepeat = face.EvalFloat(a[0]);
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8e244), 1)) { // "offset"
        float offset[2];
        const float *p = face.EvalVector2(offset, a[0], 1);
        d.mOffset[0] = p[0];
        d.mOffset[1] = p[1];
    }
}
