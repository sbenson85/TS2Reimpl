#include "gz/EffectCommands.h"

#include "gz/MaterialCommands.h" // ParserFace
#include "gz/cArguments.h"
#include "misc/UtilitiesC.h"
#include "ts/cTSCompDescription.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

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

ParserFace FaceOf(const void *face) { return ParserFace(ParserFace::Direct{}, const_cast<void *>(face)); }

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

void AddChildren(cp::cBlockCommandBase *block, const NamedChild *children, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        VirtualCall<void(__thiscall *)(void *, void *), 10>(block, NewEffectsCommand(children[i].vtable, children[i].name));
}

// ---- the camera description (cTSCameraDescription: ours through the hooks' adapters, lane IW) ------------------------------

using CameraAdapters = cTSCompDescriptionAdapters<cTSCameraComponentDescription>;
void ConstructDescription(void *at) { CameraAdapters::ConstructDescription(at, nullptr); }
void DestroyDescription(void *at) { CameraAdapters::DestroyDescription(static_cast<cTSCameraDescription *>(at), nullptr); }
void CopyDescription(void *to, const void *from)
{
    CameraAdapters::CopyDescription(static_cast<cTSCameraDescription *>(to), nullptr, static_cast<const cTSCameraDescription *>(from));
}
// The move assignment (0xbf4a50).
void MoveDescription(void *to, void *from)
{
    CameraAdapters::MoveDescription(static_cast<cTSCameraDescription *>(to), nullptr, static_cast<cTSCameraDescription *>(from));
}

struct LocalDescription {
    alignas(8) uint8_t mBytes[sizeof(CameraDescription)];
    LocalDescription() { ConstructDescription(mBytes); }
    ~LocalDescription() { DestroyDescription(mBytes); }
    LocalDescription(const LocalDescription &) = delete;
    LocalDescription &operator=(const LocalDescription &) = delete;
};

// cTSCompDescriptionBaseT<cTSCameraDescription, 8> (0xd4 bytes): its cITSComponentDescription face at +0 (slot 7 AddRef,
// slot 8 Release), cRZRefCount at +4, the description at +0xc.
struct CameraComponent {
    static constexpr uint32_t kInterfaceVTable = 0xe8ecb8; // cITSComponentDescription's (set first)
    static constexpr uint32_t kVTable = 0xe904fc;
    static constexpr uint32_t kRefCountVTable = 0xe90524;

    const void *mVTable;            // +0
    UtilObjectA mRefCount;          // +4
    CameraDescription mDescription; // +0xc
};
static_assert(offsetof(CameraComponent, mDescription) == 0xc && sizeof(CameraComponent) == 0xd4, "CameraComponent layout");

// A new component: the interface's vtable, the count, the component's vtables, a description of defaults.
CameraComponent *NewComponent()
{
    CameraComponent *component = nullptr;
    if (void *memory = ::operator new(sizeof(CameraComponent))) {
        component = static_cast<CameraComponent *>(memory);
        component->mVTable = GameAddress(CameraComponent::kInterfaceVTable);
        component->mRefCount.Construct();
        component->mVTable = GameAddress(CameraComponent::kVTable);
        component->mRefCount.mVTable = GameAddress(CameraComponent::kRefCountVTable);
        ConstructDescription(&component->mDescription);
    }
    return component;
}

void Release(void *component) { VirtualCall<uint32_t(__thiscall *)(void *), 8>(component); }

// A component held, released when it goes (as the original's unwinding does when what follows throws).
template <class Component = CameraComponent> struct HeldComponent {
    Component *mPointer = nullptr;
    ~HeldComponent()
    {
        if (mPointer) Release(mPointer);
    }
};

} // namespace

// ---- the blocks ----------------------------------------------------------------------------------------------------------

void cCameraEffectBlock::Begin(cp::cIArguments *args, uint32_t flags)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 3);
    mName.assign(argv[0], std::strlen(argv[0]));
    {
        // The defaults: a description built and moved in (the description's move assignment, 0xbf4a50).
        LocalDescription defaults;
        MoveDescription(&mDescription, defaults.mBytes);
    }
    mDescription.mFlags |= flags;
    if (static_cast<int32_t>(count) > 1) {
        if (_stricmp(argv[1], Text(0xda9e7c)) != 0) cp::ThrowFormatted(Text(0xe8f6f4), Text(0xda9e7c)); // "Expecting '%s'", ":"
        if (static_cast<int32_t>(count) < 3) cp::ThrowFormatted(Text(0xe8f704)); // "Expecting description name"
        if (void *manager = Parser()->mDescriptions) {
            HeldComponent<> found;
            const bool ok = VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t, CameraComponent **), 8>(
                manager, argv[2], CameraDescription::kComponentKind, &found.mPointer);
            if (!ok) cp::ThrowFormatted(Text(0xe8f720), argv[2]); // "Unknown description: %s"
            CopyDescription(&mDescription, &found.mPointer->mDescription);
        }
    }
    FaceOf(mParser).PushOpenBlock(this);
}

void cCameraEffectCommand::Execute(cp::cIArguments *args) { Begin(args, CameraDescription::kSlave); }

void cShakeEffectCommand::Execute(cp::cIArguments *args) { Begin(args, 0); }

void cCameraEffectBlock::EndBlock()
{
    if (!Parser()->mDescriptions) return;
    CameraComponent *component = NewComponent();
    CopyDescription(&component->mDescription, &mDescription);
    VirtualCall<void(__thiscall *)(void *, const char *, CameraComponent *), 7>(Parser()->mDescriptions, mName.c_str(), component);
}

cCameraEffectBlock *cCameraEffectBlock::DeletingDestructor(uint32_t flags)
{
    mName.~basic_string();
    DestroyDescription(&mDescription);
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cCameraEffectBlock));
    return this;
}

void cCameraEffectCommand::SetContext(void *parser, void *stateInterface)
{
    SetContextAdjusted(parser, stateInterface);
    mValue = reinterpret_cast<uint32_t>(&mDescription);
    static constexpr NamedChild kChildren[] = {
        {cCameraControlCommand::kVTable, 0x107e59c},  // control
        {cCameraLifeCommand::kVTable, 0x107e5a0},     // life
        {cCameraHeadingCommand::kVTable, 0x107e5a4},  // heading
        {cCameraPitchCommand::kVTable, 0x107e5a8},    // pitch
        {cCameraRollCommand::kVTable, 0x107e5ac},     // roll
        {cCameraFOVCommand::kVTable, 0x107e5b0},      // fov
        {cCameraNearClipCommand::kVTable, 0x107e5b4}, // nearClip
        {cCameraFarClipCommand::kVTable, 0x107e5b8},  // farClip
        {cCameraOrbitCommand::kVTable, 0x107e5bc},    // orbit
    };
    AddChildren(this, kChildren, std::size(kChildren));
}

void cShakeEffectCommand::SetContext(void *parser, void *stateInterface)
{
    SetContextAdjusted(parser, stateInterface);
    mValue = reinterpret_cast<uint32_t>(&mDescription);
    static constexpr NamedChild kChildren[] = {
        {cShakeLengthCommand::kVTable, 0x107e5c4},    // length
        {cShakeAmplitudeCommand::kVTable, 0x107e5c8}, // amplitude
        {cShakeFrequencyCommand::kVTable, 0x107e5cc}, // frequency
        {cShakeAspectCommand::kVTable, 0x107e5d0},    // shakeAspect
        {cShakeBaseTableCommand::kVTable, 0x107e5d4}, // table
    };
    AddChildren(this, kChildren, std::size(kChildren));
}

namespace {
// A block as the registration builds it: the block base, the block's vtable, a description of defaults, an empty name,
// then (outside the construction) its command name.
cCameraEffectBlock *NewBlock(uint32_t vtable, uint32_t nameRva)
{
    cCameraEffectBlock *block = nullptr;
    if (void *memory = ::operator new(sizeof(cCameraEffectBlock))) {
        block = static_cast<cCameraEffectBlock *>(static_cast<cp::cBlockCommandBase *>(memory)->Construct());
        block->mVTable = GameAddress(vtable);
        ConstructDescription(&block->mDescription);
        new (&block->mName) std::string();
    }
    block->mCommandName = NameAt(nameRva);
    return block;
}
} // namespace

void RegisterCameraEffectBlocks(void *parser)
{
    VirtualCall<void(__thiscall *)(void *, void *), 4>(parser, NewBlock(cShakeEffectCommand::kVTable, 0x107e5c0));  // "shake"
    VirtualCall<void(__thiscall *)(void *, void *), 4>(parser, NewBlock(cCameraEffectCommand::kVTable, 0x107e598)); // "camera"
}

// ---- the blocks' commands -------------------------------------------------------------------------------------------------

namespace {
using CurveParseFn = void(__cdecl *)(void *, cp::cIArguments *, void *, uint32_t);
// A curve from the command's arguments (the game's curve parse: curve, arguments, face, 0).
void ParseCurve(const cCameraCommand *command, EffectCurve &curve, cp::cIArguments *args)
{
    OriginalFunction<CurveParseFn, 0xbd1a50>()(&curve, args, command->mParser, 0);
}
// The one positional argument as a float (the face's slot 31).
float OneFloat(const cCameraCommand *command, cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    return FaceOf(command->mParser).EvalFloat(argv[0]);
}
} // namespace

void ParseCameraControl(cp::cIArguments *args, void *face, CameraDescription *d)
{
    using D = CameraDescription;
    if (args->TakeSwitch(Text(0xe90680))) d->mFlags |= D::kSlave;               // "slave"
    if (args->TakeSwitch(Text(0xe90688))) d->mFlags |= D::kSuspendController;  // "suspendController"
    if (args->TakeSwitch(Text(0xe9069c))) d->mFlags |= D::kNoRestoreController; // "noRestoreController"
    if (args->TakeSwitch(Text(0xe906b0))) d->mFlags |= D::kMoveCamera;         // "moveCamera"
    if (args->TakeSwitch(Text(0xe906bc))) d->mFlags |= D::kOrientCamera;       // "orientCamera"
    if (args->TakeSwitch(Text(0xe906cc))) d->mFlags |= D::kOrientCameraZPole;  // "orientCameraZPole"
    if (args->TakeSwitch(Text(0xe906e0))) d->mFlags |= D::kMoveController;     // "moveController"
    if (args->TakeSwitch(Text(0xe906f0))) d->mFlags |= D::kOrientController;   // "orientController"
    ParserFace f = FaceOf(face);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe90704), 1)) // "attachRadius"
        d->mAttachRadius = f.EvalFloat(a[0]);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe853e4), 1)) { // "zoom"
        d->mZoom = static_cast<uint8_t>(f.EvalInt(a[0]));
        d->mFlags |= D::kZoom;
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8eabc), 1)) { // "rotate"
        d->mRotate = static_cast<uint8_t>(f.EvalInt(a[0]));
        d->mFlags |= D::kRotate;
    }
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe79440), 1)) { // "select"
        d->mFlags |= D::kSelect;
        d->mSelect.assign(a[0], std::strlen(a[0]));
    }
    if (args->TakeSwitch(Text(0xde1064))) d->mFlags |= D::kSnapshot;     // "snapshot"
    if (args->TakeSwitch(Text(0xe90714))) d->mFlags |= D::kPostSnapshot; // "postSnapshot"
    if (args->TakeSwitch(Text(0xe90438))) d->mFlags |= D::kNoStop;       // "noStop"
}

void cCameraControlCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    // The slave bits cleared first (control's switches set them again).
    Description().mFlags &= ~CameraDescription::kMoveCamera;
    Description().mFlags &= ~CameraDescription::kOrientCamera;
    ParseCameraControl(args, mParser, &Description());
}

void cCameraLifeCommand::Execute(cp::cIArguments *args) { Description().mLife = OneFloat(this, args); }

void cCameraHeadingCommand::Execute(cp::cIArguments *args)
{
    ParseCurve(this, Description().mHeading, args);
    Description().mFlags |= CameraDescription::kRotation;
}

void cCameraPitchCommand::Execute(cp::cIArguments *args)
{
    ParseCurve(this, Description().mPitch, args);
    Description().mFlags |= CameraDescription::kRotation;
}

void cCameraRollCommand::Execute(cp::cIArguments *args)
{
    ParseCurve(this, Description().mRoll, args);
    Description().mFlags |= CameraDescription::kRotation;
}

void cCameraFOVCommand::Execute(cp::cIArguments *args)
{
    ParseCurve(this, Description().mFov, args);
    Description().mFlags |= CameraDescription::kLens;
}

void cCameraNearClipCommand::Execute(cp::cIArguments *args)
{
    ParseCurve(this, Description().mNearClip, args);
    Description().mFlags |= CameraDescription::kLens;
}

void cCameraFarClipCommand::Execute(cp::cIArguments *args)
{
    ParseCurve(this, Description().mFarClip, args);
    Description().mFlags |= CameraDescription::kLens;
}

void cCameraOrbitCommand::Execute(cp::cIArguments *args)
{
    ParseCurve(this, Description().mOrbit, args);
    Description().mFlags |= CameraDescription::kOrbit;
}

void cShakeLengthCommand::Execute(cp::cIArguments *args)
{
    CameraDescription &d = Description();
    d.mLife = OneFloat(this, args);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe90564), 1)) // "fade"
        d.mFade = FaceOf(mParser).EvalFloat(a[0]);
    if (d.mFade > d.mLife) d.mFade = d.mLife; // the fade is at most the length
}

void cShakeAmplitudeCommand::Execute(cp::cIArguments *args) { ParseCurve(this, Description().mAmplitude, args); }

void cShakeFrequencyCommand::Execute(cp::cIArguments *args) { ParseCurve(this, Description().mFrequency, args); }

void cShakeAspectCommand::Execute(cp::cIArguments *args) { Description().mShakeAspect = OneFloat(this, args); }

void cShakeBaseTableCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    Description().mTable = GameAddress<const cp::EnumTable>(0x10fd268)->Lookup(argv[0]);
}

// ---- the group commands ---------------------------------------------------------------------------------------------------

namespace {
// The parser's group handling (thiscall on the parser state: args, name, kind, 0).
void AddGroupEffect(void *parser, cp::cIArguments *args, const char *name, uint32_t kind)
{
    OriginalFunction<void(__thiscall *)(void *, cp::cIArguments *, const char *, uint32_t, uint32_t), 0xbcd940>()(
        parser, args, name, kind, 0);
}
} // namespace

void cGroupShakeCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    AddGroupEffect(mState, args, argv[0], CameraDescription::kComponentKind);
}

void cGroupCameraCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 0, 1);
    if (static_cast<int32_t>(count) > 0) {
        AddGroupEffect(mState, args, argv[0], CameraDescription::kComponentKind);
        return;
    }
    // No name: a description of its own (held while it is filled and handed on).
    HeldComponent<> held;
    held.mPointer = NewComponent();
    if (held.mPointer) VirtualCall<uint32_t(__thiscall *)(void *), 7>(held.mPointer); // AddRef
    ParseCameraControl(args, mParser, &held.mPointer->mDescription);
    held.mPointer->mDescription.mFlags |= CameraDescription::kNoStop;
    OriginalFunction<void(__thiscall *)(void *, cp::cIArguments *, void *, uint32_t), 0xbcd810>()(mState, args, held.mPointer, 1);
}

void RegisterCameraGroupCommands(cp::cBlockCommandBase *block)
{
    static constexpr NamedChild kChildren[] = {
        {cGroupShakeCommand::kVTable, 0x107e594},  // shakeEffect
        {cGroupCameraCommand::kVTable, 0x107e50c}, // cameraEffect
    };
    AddChildren(block, kChildren, std::size(kChildren));
}

// ==== water ===================================================================================================================

namespace {
// cTSCompDescriptionBaseT<cTSWaterDescription, 0xc> (0x34 bytes), built by the game's constructor 0xbf66f0 (it returns
// the object): the description at +0xc.
struct WaterComponent {
    const void *mVTable;           // +0
    UtilObjectA mRefCount;         // +4
    WaterDescription mDescription; // +0xc
};
static_assert(offsetof(WaterComponent, mDescription) == 0xc && sizeof(WaterComponent) == 0x34, "WaterComponent layout");
} // namespace

void cWaterEffectCommand::SetContext(void *parser, void *stateInterface)
{
    SetContextAdjusted(parser, stateInterface);
    mValue = reinterpret_cast<uint32_t>(&mDescription);
    static constexpr NamedChild kChildren[] = {
        {cWaterDropCommand::kVTable, 0x107e6a0},    // drop
        {cWaterDropCommand::kVTable, 0x107e69c},    // control
        {cWaterSurfaceCommand::kVTable, 0x107e6a4}, // surface
    };
    AddChildren(this, kChildren, std::size(kChildren));
}

void cWaterEffectCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 3);
    mName.assign(argv[0], std::strlen(argv[0]));
    mDescription.SetDefaults();
    if (static_cast<int32_t>(count) > 1) {
        if (_stricmp(argv[1], Text(0xda9e7c)) != 0) cp::ThrowFormatted(Text(0xe8f6f4), Text(0xda9e7c)); // "Expecting '%s'", ":"
        if (static_cast<int32_t>(count) < 3) cp::ThrowFormatted(Text(0xe8f704)); // "Expecting description name"
        if (void *manager = Parser()->mDescriptions) {
            HeldComponent<WaterComponent> found;
            const bool ok = VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t, WaterComponent **), 8>(
                manager, argv[2], WaterDescription::kComponentKind, &found.mPointer);
            if (!ok) cp::ThrowFormatted(Text(0xe8f720), argv[2]); // "Unknown description: %s"
            mDescription = found.mPointer->mDescription;
        }
    }
    FaceOf(mParser).PushOpenBlock(this);
}

void cWaterEffectCommand::EndBlock()
{
    if (!Parser()->mDescriptions) return;
    WaterComponent *component = nullptr;
    if (void *memory = ::operator new(sizeof(WaterComponent)))
        component = reinterpret_cast<WaterComponent *>(
            cTSCompDescriptionAdapters<cTSWaterComponentDescription>::Construct(memory, nullptr)); // 0xbf66f0, ours (lane IT)
    component->mDescription = mDescription;
    VirtualCall<void(__thiscall *)(void *, const char *, WaterComponent *), 7>(Parser()->mDescriptions, mName.c_str(), component);
}

cWaterEffectCommand *cWaterEffectCommand::DeletingDestructor(uint32_t flags)
{
    mName.~basic_string();
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cWaterEffectCommand));
    return this;
}

void RegisterWaterEffectBlock(void *parser)
{
    cWaterEffectCommand *block = nullptr;
    if (void *memory = ::operator new(sizeof(cWaterEffectCommand))) {
        block = static_cast<cWaterEffectCommand *>(static_cast<cp::cBlockCommandBase *>(memory)->Construct());
        block->mVTable = GameAddress(cWaterEffectCommand::kVTable);
        block->mDescription.SetDefaults();
        new (&block->mName) std::string();
    }
    block->mCommandName = NameAt(0x107e6a8); // "water"
    VirtualCall<void(__thiscall *)(void *, void *), 4>(parser, block);
}

void cWaterDropCommand::Execute(cp::cIArguments *args)
{
    args->PositionalExactly(0);
    WaterDescription &d = Description();
    ParserFace face = FaceOf(mParser);
    // ORIGINAL-BUG: both switches are read without testing that they were given; without one, the original reads
    // through null.
    d.mDropSize = face.EvalFloat(args->TakeSwitchArgumentsExactly(Text(0xdbbf88), 1)[0]);  // "size"
    d.mDropDepth = face.EvalFloat(args->TakeSwitchArgumentsExactly(Text(0xe909b8), 1)[0]); // "depth"
}

void cWaterSurfaceCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(2);
    WaterDescription &d = Description();
    d.mFlags |= WaterDescription::kSurface;
    ParserFace face = FaceOf(mParser);
    d.mSurface.mValues[0] = static_cast<uint8_t>(face.EvalUint(argv[0]));
    d.mSurface.mValues[1] = static_cast<uint8_t>(face.EvalUint(argv[1]));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe483b8), 1)) // "style"
        d.mSurface.mStyle = static_cast<uint16_t>(GameAddress<const cp::EnumTable>(0x10fd284)->Lookup(a[0]));
    else
        d.mSurface.mStyle = 2;
    struct Switch {
        uint32_t name;
        float WaterDescription::*field;
    };
    static constexpr Switch kFloats[] = {
        {0xe49160, &WaterDescription::mSpeed},        // "speed"
        {0xe8fff8, &WaterDescription::mDamp},         // "damp"
        {0xe909ec, &WaterDescription::mLargeWave},    // "largeWave"
        {0xe909f8, &WaterDescription::mStopResidual}, // "stopResidual"
        {0xe90a08, &WaterDescription::mDropScale},    // "dropScale"
    };
    for (const Switch &s : kFloats)
        if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(s.name), 1)) d.*s.field = face.EvalFloat(a[0]);
}

void cGroupWaterCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    AddGroupEffect(mState, args, argv[0], WaterDescription::kComponentKind);
}

void RegisterWaterGroupCommand(cp::cBlockCommandBase *block)
{
    static constexpr NamedChild kChildren[] = {{cGroupWaterCommand::kVTable, 0x107e614}}; // waterEffect
    AddChildren(block, kChildren, std::size(kChildren));
}

// ==== screen ==================================================================================================================

namespace {
// cTSScreenDescription's constructor, destructor and copy assignment (0xbf7280 / 0xbf7320 / 0xbf73c0): ours, through
// the hooks' adapters (lane IW).
using ScreenAdapters = cTSCompDescriptionAdapters<cTSScreenComponentDescription>;
void ConstructScreenDescription(void *at) { ScreenAdapters::ConstructDescription(at, nullptr); }
void DestroyScreenDescription(void *at) { ScreenAdapters::DestroyDescription(static_cast<cTSScreenDescription *>(at), nullptr); }
void CopyScreenDescription(void *to, const void *from)
{
    ScreenAdapters::CopyDescription(static_cast<cTSScreenDescription *>(to), nullptr, static_cast<const cTSScreenDescription *>(from));
}

struct LocalScreenDescription {
    alignas(8) uint8_t mBytes[sizeof(ScreenDescription)];
    LocalScreenDescription() { ConstructScreenDescription(mBytes); }
    ~LocalScreenDescription() { DestroyScreenDescription(mBytes); }
    ScreenDescription &Get() { return *reinterpret_cast<ScreenDescription *>(mBytes); }
    LocalScreenDescription(const LocalScreenDescription &) = delete;
    LocalScreenDescription &operator=(const LocalScreenDescription &) = delete;
};

// cTSCompDescriptionBaseT<cTSScreenDescription, 0xb> (0x54 bytes): the description at +0xc.
struct ScreenComponent {
    static constexpr uint32_t kInterfaceVTable = 0xe8ecb8;
    static constexpr uint32_t kVTable = 0xe90b14;
    static constexpr uint32_t kRefCountVTable = 0xe90b3c;

    const void *mVTable;            // +0
    UtilObjectA mRefCount;          // +4
    ScreenDescription mDescription; // +0xc
};
static_assert(offsetof(ScreenComponent, mDescription) == 0xc && sizeof(ScreenComponent) == 0x54, "ScreenComponent layout");
} // namespace

void cScreenEffectCommand::SetContext(void *parser, void *stateInterface)
{
    SetContextAdjusted(parser, stateInterface);
    mValue = reinterpret_cast<uint32_t>(&mDescription);
    static constexpr NamedChild kChildren[] = {
        {cScreenModeCommand::kVTable, 0x107e740},     // mode
        {cScreenStrengthCommand::kVTable, 0x107e748}, // strength
        {cScreenLengthCommand::kVTable, 0x107e744},   // length
        {cScreenColorCommand::kVTable, 0x107e74c},    // color
        {cScreenColorCommand::kVTable, 0x107e754},    // color255
        {cScreenColorCommand::kVTable, 0x107e750},    // colour
        {cScreenColorCommand::kVTable, 0x107e758},    // colour255
        {cScreenTextureCommand::kVTable, 0x107e75c},  // texture
    };
    AddChildren(this, kChildren, std::size(kChildren));
}

void cScreenEffectCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 3);
    mName.assign(argv[0], std::strlen(argv[0]));
    {
        // The defaults: a description built and moved in member by member (the containers taken), then destroyed.
        LocalScreenDescription defaults;
        ScreenDescription &from = defaults.Get();
        ScreenDescription &d = mDescription;
        d.mTypeId = from.mTypeId;
        d.mMode = from.mMode;
        d.mFlags = from.mFlags;
        d.mColors = std::move(from.mColors);
        d.mStrength.mHead = from.mStrength.mHead;
        d.mStrength.mValues = std::move(from.mStrength.mValues);
        d.mLength = from.mLength;
        d.mDelay = from.mDelay;
        d.mTexture = std::move(from.mTexture);
    }
    if (static_cast<int32_t>(count) > 1) {
        // Case-sensitive here (the other blocks compare without case).
        if (std::strcmp(argv[1], Text(0xda9e7c)) != 0) cp::ThrowFormatted(Text(0xe8f6f4), Text(0xda9e7c)); // "Expecting '%s'", ":"
        if (static_cast<int32_t>(count) < 3) cp::ThrowFormatted(Text(0xe8f704)); // "Expecting description name"
        if (void *manager = Parser()->mDescriptions) {
            HeldComponent<ScreenComponent> found;
            const bool ok = VirtualCall<bool(__thiscall *)(void *, const char *, uint32_t, ScreenComponent **), 8>(
                manager, argv[2], ScreenDescription::kComponentKind, &found.mPointer);
            if (!ok) cp::ThrowFormatted(Text(0xe8f720), argv[2]); // "Unknown description: %s"
            CopyScreenDescription(&mDescription, &found.mPointer->mDescription);
        }
    }
    ParserFace(mState).PushOpenBlock(this);
}

void cScreenEffectCommand::EndBlock()
{
    // A screen without colours is white.
    if (mDescription.mColors.empty()) mDescription.mColors.push_back({1.0f, 1.0f, 1.0f});
    if (Parser()->mDescriptions) {
        ScreenComponent *component = nullptr;
        if (void *memory = ::operator new(sizeof(ScreenComponent))) {
            component = static_cast<ScreenComponent *>(memory);
            component->mVTable = GameAddress(ScreenComponent::kInterfaceVTable);
            component->mRefCount.Construct();
            component->mVTable = GameAddress(ScreenComponent::kVTable);
            component->mRefCount.mVTable = GameAddress(ScreenComponent::kRefCountVTable);
            ConstructScreenDescription(&component->mDescription);
        }
        CopyScreenDescription(&component->mDescription, &mDescription);
        VirtualCall<void(__thiscall *)(void *, const char *, ScreenComponent *), 7>(Parser()->mDescriptions, mName.c_str(),
                                                                                      component);
    }
    mName.assign(Text(0xd9b9d0), 0);
}

cScreenEffectCommand *cScreenEffectCommand::DeletingDestructor(uint32_t flags)
{
    mName.~basic_string();
    DestroyScreenDescription(&mDescription);
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cScreenEffectCommand));
    return this;
}

void RegisterScreenEffectBlock(void *parser)
{
    cScreenEffectCommand *block = nullptr;
    if (void *memory = ::operator new(sizeof(cScreenEffectCommand))) {
        block = static_cast<cScreenEffectCommand *>(static_cast<cp::cBlockCommandBase *>(memory)->Construct());
        block->mVTable = GameAddress(cScreenEffectCommand::kVTable);
        ConstructScreenDescription(&block->mDescription);
        new (&block->mName) std::string();
    }
    block->mCommandName = NameAt(0x107e73c); // "screen"
    VirtualCall<void(__thiscall *)(void *, void *), 4>(parser, block);
}

void cScreenModeCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    Description().mMode = GameAddress<const cp::EnumTable>(0x10fd2a0)->Lookup(argv[0]);
}

void cScreenStrengthCommand::Execute(cp::cIArguments *args)
{
    OriginalFunction<CurveParseFn, 0xbd1a50>()(&Description().mStrength, args, mParser, 0);
}

void cScreenLengthCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    ParserFace face = FaceOf(mParser);
    ScreenDescription &d = Description();
    d.mLength = face.EvalFloat(argv[0]);
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe8ed58), 1)) d.mDelay = face.EvalFloat(a[0]); // "delay"
    if (args->TakeSwitch(Text(0xe8ed1c))) d.mFlags |= ScreenDescription::kLoop;    // "loop"
    if (args->TakeSwitch(Text(0xe8ed2c))) d.mFlags |= ScreenDescription::kSustain; // "sustain"
}

void cScreenColorCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 0x7fffffff);
    std::vector<Vec3> &colors = Description().mColors;
    colors.clear();
    ParserFace face = FaceOf(mParser);
    for (uint32_t i = 0; i < count; ++i) {
        Vec3 color;
        const float *c = face.EvalColorRGB(&color.x, argv[i]);
        colors.push_back({c[0], c[1], c[2]});
    }
    // color255 / colour255: the command's name (its first word) ends in "255". As the original, a name shorter than three
    // characters throws out_of_range (never one the block makes).
    const std::string name(args->Argument(0));
    if (name.substr(name.size() - 3) == "255") {
        constexpr float scale = 1.0f / 255.0f;
        for (Vec3 &c : colors) {
            c.x *= scale;
            c.y *= scale;
            c.z *= scale;
        }
    }
}

void cScreenTextureCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    Description().mTexture.assign(argv[0], std::strlen(argv[0]));
}

void cGroupScreenCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    AddGroupEffect(mState, args, argv[0], ScreenDescription::kComponentKind);
}

void RegisterScreenGroupCommand(cp::cBlockCommandBase *block)
{
    static constexpr NamedChild kChildren[] = {{cGroupScreenCommand::kVTable, 0x107e6b4}}; // screenEffect
    AddChildren(block, kChildren, std::size(kChildren));
}
