#pragma once
// The effects parser's camera and shake blocks (`camera`, `shake`) and their commands, and the group commands that name
// them inside an effect (`cameraEffect`, `shakeEffect`). Their classes are in an anonymous namespace of the game's
// camera-effect translation unit (`.?AUcCameraEffectCommand@?A0x4da3b4cd@@` and so on); here they are in the global
// namespace. RVAs are 1.18.0.187.
//
// Hook shape, as the particle and decal blocks (gz/ParticleCommands.h): the effects parser (not ours) creates the blocks
// through the registration function 0xbf6300 (ours here), with the game's vtables, so every slot is an RVA hook; the
// blocks' slot 3 builds their children as the original does (operator new, the base constructor, the child's game
// vtable, its name). Both blocks fill the same description, cTSCameraDescription, and share their slots 9 and 12.
//
// Also the water and screen blocks (`water`, `screen`; registrations 0xbf6ff0 and 0xbf83e0) and their group commands
// (`waterEffect`, `screenEffect`), the same shape.
#include "gz/ParticleCommands.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nGZCommandParser {
class cIArguments;
}

// A curve (0x10 bytes): cTSEffectCurve (src/ts/cTSEffectCurve.h), as bytes here; parsed by 0xbd1a50 (not ours).
struct EffectCurve {
    uint8_t mBytes[0x10];
};

// cTSCameraDescription (0xc8 bytes): what a `camera` or a `shake` block fills. Its constructor (0xbf4610), destructor
// (0xbf4720), move assignment (0xbf4a50) and copy (0xbf4d20) are ours: src/ts/cTSCompDescription.h cTSCameraDescription,
// which this view mirrors with the parser's names.
struct CameraDescription {
    static constexpr uint32_t kTypeId = 0xcbb908f4; // the word the constructor writes at +0
    static constexpr uint32_t kComponentKind = 8;   // the description manager's kind for these

    // The flag bits (mFlags) the commands set.
    enum : uint32_t {
        kMoveCamera = 0x2,              // control -moveCamera (and -slave); `camera` sets it with kOrientCamera
        kOrientCamera = 0x4,            // control -orientCamera (and -slave)
        kOrientCameraZPole = 0x8,       // control -orientCameraZPole
        kRotation = 0x10,               // heading, pitch, roll
        kLens = 0x20,                   // fov, nearClip, farClip
        kOrbit = 0x40,                  // orbit
        kSuspendController = 0x80,      // control -suspendController
        kSelect = 0x100,                // control -select
        kZoom = 0x200,                  // control -zoom
        kRotate = 0x400,                // control -rotate
        kMoveController = 0x800,        // control -moveController
        kOrientController = 0x1000,     // control -orientController
        kNoRestoreController = 0x2000,  // control -noRestoreController
        kNoStop = 0x8000,               // control -noStop; an anonymous cameraEffect's description
        kSnapshot = 0x10000,            // control -snapshot
        kPostSnapshot = 0x20000,        // control -postSnapshot
        kSlave = kMoveCamera | kOrientCamera, // control -slave
    };

    uint32_t mTypeId;        // +0 kTypeId
    uint32_t mFlags;         // +4
    float mLife;             // +8 camera life, shake length
    float mFade;             // +0xc shake length -fade (at most the length; 1 at construction)
    EffectCurve mAmplitude;  // +0x10 shake amplitude
    EffectCurve mFrequency;  // +0x20 shake frequency
    float mShakeAspect;      // +0x30 shake shakeAspect (1 at construction)
    uint32_t mTable;         // +0x34 shake table (the parser's table 0x10fd268)
    EffectCurve mHeading;    // +0x38 heading
    EffectCurve mPitch;      // +0x48 pitch
    EffectCurve mRoll;       // +0x58 roll
    EffectCurve mOrbit;      // +0x68 orbit
    EffectCurve mFov;        // +0x78 fov
    EffectCurve mNearClip;   // +0x88 nearClip
    EffectCurve mFarClip;    // +0x98 farClip
    uint8_t mZoom;           // +0xa8 control -zoom (the face's EvalInt, low byte)
    uint8_t mRotate;         // +0xa9 control -rotate
    uint8_t mAA[2];
    std::string mSelect;     // +0xac control -select
    float mAttachRadius;     // +0xc4 control -attachRadius
};
static_assert(offsetof(CameraDescription, mFade) == 0xc && offsetof(CameraDescription, mShakeAspect) == 0x30 &&
                  offsetof(CameraDescription, mHeading) == 0x38 && offsetof(CameraDescription, mFarClip) == 0x98 &&
                  offsetof(CameraDescription, mZoom) == 0xa8 && offsetof(CameraDescription, mSelect) == 0xac &&
                  offsetof(CameraDescription, mAttachRadius) == 0xc4 && sizeof(CameraDescription) == 0xc8,
              "CameraDescription layout");

// control's switches (the function 0xbf5f20 both `control` and an anonymous `cameraEffect` run): flags and values of the
// description from the switches, the values through the parser face.
void ParseCameraControl(nGZCommandParser::cIArguments *args, void *face, CameraDescription *description);
constexpr uint32_t kParseCameraControl = 0xbf5f20;

// ---- camera (vtable 0xe908b4) and shake (vtable 0xe90620) --------------------------------------------------------------
// `camera name [: base]` / `shake name [: base]`: a block that fills a description (the children's state is the
// description itself) and registers a copy under the name at its end. cTSEffectsBlockCommandT (0x128 bytes). Slot 5
// (shared_vf_1674d0) answers mCommandName; slots 9 and 12 are one body for both blocks (shared_vf_bf5720,
// shared_vf_bf4f20).
class cCameraEffectBlock : public nGZCommandParser::cBlockCommandT {
public:
    cTSEffectsParserFields *Parser() const { return static_cast<cTSEffectsParserFields *>(mState); }

    // Slot 4's common part: the name, the defaults, the base named after the colon. `camera` sets kSlave after the
    // defaults, `shake` does not.
    void Begin(nGZCommandParser::cIArguments *args, uint32_t flags);
    void EndBlock();                                       // slot 9, 0xbf5720
    cCameraEffectBlock *DeletingDestructor(uint32_t flags); // slot 12, 0xbf4f20

    const char *mCommandName;       // +0x44 "camera" / "shake"
    CameraDescription mDescription; // +0x48 the children's state
    std::string mName;              // +0x110 the block's name
};
static_assert(offsetof(cCameraEffectBlock, mCommandName) == 0x44 && offsetof(cCameraEffectBlock, mDescription) == 0x48 &&
                  offsetof(cCameraEffectBlock, mName) == 0x110 && sizeof(cCameraEffectBlock) == 0x128,
              "cCameraEffectBlock layout");

class cCameraEffectCommand : public cCameraEffectBlock {
public:
    static constexpr uint32_t kVTable = 0xe908b4;
    void SetContext(void *parser, void *stateInterface); // slot 3, 0xbf57e0: 9 child commands
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xbf5020
};

class cShakeEffectCommand : public cCameraEffectBlock {
public:
    static constexpr uint32_t kVTable = 0xe90620;
    void SetContext(void *parser, void *stateInterface); // slot 3, 0xbf5c70: 5 child commands
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xbf54e0
};

// The effects parser's registration of the two blocks (0xbf6300, cdecl): each built (the block base, its vtable, a
// description, an empty name, its command name) and handed to the parser's slot 4.
void RegisterCameraEffectBlocks(void *parser);
constexpr uint32_t kRegisterCameraEffectBlocks = 0xbf6300;

// ---- the blocks' commands --------------------------------------------------------------------------------------------
// cTSEffectsCommandT<cTSCameraDescription>: each sets values of the block's description. Slots other than 4 are shared.
class cCameraCommand : public cEffectsCommand {
public:
    CameraDescription &Description() const { return *static_cast<CameraDescription *>(mState); }
};

#define CAMERA_COMMAND(cls, vtable, execute)                                                                           \
    class cls : public cCameraCommand {                                                                                \
    public:                                                                                                            \
        static constexpr uint32_t kVTable = vtable;                                                                    \
        static constexpr uint32_t kExecute = execute; /* the game's slot 4 */                                          \
        void Execute(nGZCommandParser::cIArguments *args);                                                             \
    }

CAMERA_COMMAND(cCameraControlCommand, 0xe90728, 0xbf4fe0);  // control
CAMERA_COMMAND(cCameraLifeCommand, 0xe90754, 0xbf5230);     // life
CAMERA_COMMAND(cCameraHeadingCommand, 0xe90780, 0xbf5200);  // heading
CAMERA_COMMAND(cCameraPitchCommand, 0xe907ac, 0xbf52c0);    // pitch
CAMERA_COMMAND(cCameraRollCommand, 0xe907d8, 0xbf52f0);     // roll
CAMERA_COMMAND(cCameraFOVCommand, 0xe90804, 0xbf51a0);      // fov
CAMERA_COMMAND(cCameraNearClipCommand, 0xe90830, 0xbf5260); // nearClip
CAMERA_COMMAND(cCameraFarClipCommand, 0xe9085c, 0xbf51d0);  // farClip
CAMERA_COMMAND(cCameraOrbitCommand, 0xe90888, 0xbf5290);    // orbit
CAMERA_COMMAND(cShakeLengthCommand, 0xe9053c, 0xbf5680);    // length
CAMERA_COMMAND(cShakeAmplitudeCommand, 0xe905c8, 0xbf5460); // amplitude
CAMERA_COMMAND(cShakeFrequencyCommand, 0xe905f4, 0xbf5660); // frequency
CAMERA_COMMAND(cShakeAspectCommand, 0xe90570, 0xbf5480);    // shakeAspect
CAMERA_COMMAND(cShakeBaseTableCommand, 0xe9059c, 0xbf54b0); // table

#undef CAMERA_COMMAND

// ---- the group commands (`shakeEffect name`, `cameraEffect [name] -control's switches`) --------------------------------
// cTSEffectsCommandT whose state is the effects parser; registered by 0xbf6420 (ours here) into a group block.
class cGroupShakeCommand : public cEffectsCommand {
public:
    static constexpr uint32_t kVTable = 0xe90658;
    static constexpr uint32_t kExecute = 0xbf5430;
    void Execute(nGZCommandParser::cIArguments *args); // slot 4: the parser's group handling, kind 8
};

class cGroupCameraCommand : public cEffectsCommand {
public:
    static constexpr uint32_t kVTable = 0xe908ec;
    static constexpr uint32_t kExecute = 0xbf5320;
    // Slot 4: with a name, the parser's group handling (kind 8); without, an anonymous description made from control's
    // switches (with kNoStop) and handed to the parser's 0xbcd810.
    void Execute(nGZCommandParser::cIArguments *args);
};

// The group block's registration of the two (0xbf6420, cdecl): each made and handed to the block's slot 10.
void RegisterCameraGroupCommands(nGZCommandParser::cBlockCommandBase *block);
constexpr uint32_t kRegisterCameraGroupCommands = 0xbf6420;

// ==== water (`.?AUcWaterEffectCommand@?A0xb7fcad62@@`) =====================================================================

// cTSWaterDescription (0x28 bytes, plain values): what a `water` block fills. The block writes its defaults itself; the
// component that keeps a registered copy (cTSCompDescriptionBaseT<cTSWaterDescription>, 0x34 bytes) is built by the
// game's constructor 0xbf66f0 (not ours).
struct WaterDescription {
    static constexpr uint32_t kTypeId = 0x0c92aee0;
    static constexpr uint32_t kComponentKind = 0xc;
    enum : uint32_t { kSurface = 0x1 }; // surface given

    uint32_t mTypeId; // +0
    uint32_t mFlags;  // +4
    union {
        float mDropSize; // +8 drop -size (1 by default)
        struct {
            uint8_t mValues[2]; // +8 surface's two values (the face's EvalUint, low byte)
            uint16_t mStyle;    // +0xa surface -style (the parser's table 0x10fd284; 2 without it)
        } mSurface;
    };
    float mDropDepth;    // +0xc drop -depth (1)
    uint32_t m10;        // +0x10 (0)
    float mSpeed;        // +0x14 surface -speed (30)
    float mDamp;         // +0x18 surface -damp (0.01)
    float mLargeWave;    // +0x1c surface -largeWave (100)
    float mStopResidual; // +0x20 surface -stopResidual (0.01)
    float mDropScale;    // +0x24 surface -dropScale (0.25)

    // The values a block starts from (as the block and its registration write them).
    void SetDefaults()
    {
        mTypeId = kTypeId;
        mFlags = 0;
        mDropSize = 1.0f;
        mDropDepth = 1.0f;
        m10 = 0;
        mSpeed = 30.0f;
        mDamp = 0.01f;
        mLargeWave = 100.0f;
        mStopResidual = 0.01f;
        mDropScale = 0.25f;
    }
};
static_assert(offsetof(WaterDescription, mDropDepth) == 0xc && offsetof(WaterDescription, mSpeed) == 0x14 &&
                  offsetof(WaterDescription, mDropScale) == 0x24 && sizeof(WaterDescription) == 0x28,
              "WaterDescription layout");

// ---- water (vtable 0xe90a18) ------------------------------------------------------------------------------------------------
// `water name [: base]`: cTSEffectsBlockCommandT (0x88 bytes). Slot 5 (shared_vf_1674d0) answers mCommandName.
class cWaterEffectCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xe90a18;
    cTSEffectsParserFields *Parser() const { return static_cast<cTSEffectsParserFields *>(mState); }

    void SetContext(void *parser, void *stateInterface);  // slot 3, 0xbf6cf0: 3 child commands
    void Execute(nGZCommandParser::cIArguments *args);    // slot 4, 0xbf6900
    void EndBlock();                                      // slot 9, 0xbf6c50
    cWaterEffectCommand *DeletingDestructor(uint32_t flags); // slot 12, 0xbf67a0

    const char *mCommandName;      // +0x44 "water"
    WaterDescription mDescription; // +0x48 the children's state
    std::string mName;             // +0x70 the block's name
};
static_assert(offsetof(cWaterEffectCommand, mDescription) == 0x48 && offsetof(cWaterEffectCommand, mName) == 0x70 &&
                  sizeof(cWaterEffectCommand) == 0x88,
              "cWaterEffectCommand layout");

// The effects parser's registration of the block (0xbf6ff0, cdecl), as RegisterCameraEffectBlocks.
void RegisterWaterEffectBlock(void *parser);
constexpr uint32_t kRegisterWaterEffectBlock = 0xbf6ff0;

class cWaterCommand : public cEffectsCommand {
public:
    WaterDescription &Description() const { return *static_cast<WaterDescription *>(mState); }
};

// drop (also named `control`): -size and -depth (both expected: see the .cpp).
class cWaterDropCommand : public cWaterCommand {
public:
    static constexpr uint32_t kVTable = 0xe90990;
    static constexpr uint32_t kExecute = 0xbf6890;
    void Execute(nGZCommandParser::cIArguments *args);
};

// surface a b -style -speed -damp -largeWave -stopResidual -dropScale
class cWaterSurfaceCommand : public cWaterCommand {
public:
    static constexpr uint32_t kVTable = 0xe909c4;
    static constexpr uint32_t kExecute = 0xbf6ab0;
    void Execute(nGZCommandParser::cIArguments *args);
};

// `waterEffect name` in a group (kind 0xc); registered by 0xbf70d0.
class cGroupWaterCommand : public cEffectsCommand {
public:
    static constexpr uint32_t kVTable = 0xe90a50;
    static constexpr uint32_t kExecute = 0xbf6860;
    void Execute(nGZCommandParser::cIArguments *args);
};
void RegisterWaterGroupCommand(nGZCommandParser::cBlockCommandBase *block);
constexpr uint32_t kRegisterWaterGroupCommand = 0xbf70d0;

// ==== screen (`.?AUcScreenEffectCommand@?A0x95c6415c@@`) ===================================================================

// The game's curve with its values (0x10 bytes): a word, then the values. Its constructor (0xbdc880, from one value) and
// its parse (0xbd1a50) are the game's.
struct EffectCurveValues {
    uint32_t mHead;
    std::vector<float> mValues;
};
static_assert(sizeof(EffectCurveValues) == 0x10, "EffectCurveValues layout");

// cTSScreenDescription (0x48 bytes): what a `screen` block fills. Its constructor (0xbf7280), destructor (0xbf7320) and copy
// assignment (0xbf73c0) are ours: src/ts/cTSCompDescription.h cTSScreenDescription.
struct ScreenDescription {
    static constexpr uint32_t kTypeId = 0xcbce1184;
    static constexpr uint32_t kComponentKind = 0xb;
    enum : uint32_t {
        kLoop = 0x1,    // length -loop
        kSustain = 0x2, // length -sustain
    };

    uint32_t mTypeId;             // +0
    uint32_t mMode;               // +4 mode (the parser's table 0x10fd2a0)
    uint32_t mFlags;              // +8
    std::vector<Vec3> mColors;    // +0xc color (a block without colours ends with white)
    EffectCurveValues mStrength;  // +0x18 strength (1 at construction)
    float mLength;                // +0x28 length (2)
    float mDelay;                 // +0x2c length -delay
    std::string mTexture;         // +0x30 texture
};
static_assert(offsetof(ScreenDescription, mColors) == 0xc && offsetof(ScreenDescription, mStrength) == 0x18 &&
                  offsetof(ScreenDescription, mLength) == 0x28 && offsetof(ScreenDescription, mTexture) == 0x30 &&
                  sizeof(ScreenDescription) == 0x48,
              "ScreenDescription layout");

// ---- screen (vtable 0xe90c30) -------------------------------------------------------------------------------------------
// `screen name [: base]`: cTSEffectsBlockCommandT (0xa8 bytes). Slot 5 (shared_vf_1674d0) answers mCommandName.
class cScreenEffectCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xe90c30;
    cTSEffectsParserFields *Parser() const { return static_cast<cTSEffectsParserFields *>(mState); }

    void SetContext(void *parser, void *stateInterface);   // slot 3, 0xbf7d40: 8 child commands
    void Execute(nGZCommandParser::cIArguments *args);     // slot 4, 0xbf7830
    void EndBlock();                                       // slot 9, 0xbf7c10
    cScreenEffectCommand *DeletingDestructor(uint32_t flags); // slot 12, 0xbf7490

    const char *mCommandName;       // +0x44 "screen"
    ScreenDescription mDescription; // +0x48 the children's state
    std::string mName;              // +0x90 the block's name
};
static_assert(offsetof(cScreenEffectCommand, mDescription) == 0x48 && offsetof(cScreenEffectCommand, mName) == 0x90 &&
                  sizeof(cScreenEffectCommand) == 0xa8,
              "cScreenEffectCommand layout");

// The effects parser's registration of the block (0xbf83e0, cdecl).
void RegisterScreenEffectBlock(void *parser);
constexpr uint32_t kRegisterScreenEffectBlock = 0xbf83e0;

class cScreenCommand : public cEffectsCommand {
public:
    ScreenDescription &Description() const { return *static_cast<ScreenDescription *>(mState); }
};

#define SCREEN_COMMAND(cls, vtable, execute)                                                                           \
    class cls : public cScreenCommand {                                                                                \
    public:                                                                                                            \
        static constexpr uint32_t kVTable = vtable;                                                                    \
        static constexpr uint32_t kExecute = execute; /* the game's slot 4 */                                          \
        void Execute(nGZCommandParser::cIArguments *args);                                                             \
    }

SCREEN_COMMAND(cScreenModeCommand, 0xe90b54, 0xbf7b60);     // mode
SCREEN_COMMAND(cScreenStrengthCommand, 0xe90bac, 0xbf7b90); // strength
SCREEN_COMMAND(cScreenLengthCommand, 0xe90b80, 0xbf7ac0);   // length
SCREEN_COMMAND(cScreenColorCommand, 0xe90bd8, 0xbf7580);    // color, colour, color255, colour255
SCREEN_COMMAND(cScreenTextureCommand, 0xe90c04, 0xbf7bb0);  // texture

#undef SCREEN_COMMAND

// `screenEffect name` in a group (kind 0xb); registered by 0xbf8480.
class cGroupScreenCommand : public cEffectsCommand {
public:
    static constexpr uint32_t kVTable = 0xe90c68;
    static constexpr uint32_t kExecute = 0xbf7550;
    void Execute(nGZCommandParser::cIArguments *args);
};
void RegisterScreenGroupCommand(nGZCommandParser::cBlockCommandBase *block);
constexpr uint32_t kRegisterScreenGroupCommand = 0xbf8480;
