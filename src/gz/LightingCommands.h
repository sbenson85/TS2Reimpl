#pragma once
// The lighting script's commands: the commands cTSLightingParserState (the lighting parser, nTSSG::cLightingParserState's
// base) runs. Their classes are in an anonymous namespace of the game's lighting translation unit
// (`.?AUcLightingStateCommand@?A0x036c3978@@` and so on); here they are in the global namespace. RVAs are 1.18.0.187.
//
// Hook shape, as the material commands (gz/MaterialCommands.h): the lighting parser (not ours) creates the top-level
// commands with the game's vtables, so every slot is an RVA hook; a block's slot 3 builds its children as the original
// does (operator new, the base constructor, the child's game vtable).
#include "gz/MaterialCommands.h" // ParserFace, SetContextWithChildren
#include "rz/cRZString.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZCommandParser {
class cIArguments;
}

// The values of a lighting state (0x1f4 bytes, plain data), as the lightingState block's commands set them. Offsets are
// the state's (the values start at +0x20). Colours are RGB; a shade's optional second argument scales its colour.
struct LightingValues {
    // Which parts are given (+0x20): bits 0..6 the lights, 7 skylight, 8 fog, 9 environment, 10 particles, then the
    // control switches 11 debugPortals, 12 edith, 13 optimize. std::bitset: setting a position past 13 throws
    // out_of_range ("invalid bitset position"), as the original's.
    std::bitset<14> mEnabled;
    struct Light {           // `light type direction color [intensity]` (0x1c bytes)
        float color[3];      // +0
        float intensity;     // +0xc (1 when not given)
        float direction[3];  // +0x10 normalised
        // 0xb3cdb0 (returns this; lane 261110t): the colour the game's 0x10fb040, intensity 1, direction 0.
        Light *Construct();
    } mLights[7];            // +0x24, by the light type (the parser's table 0x10fb00c)
    uint32_t mLightMap;      // +0xe8 `lightMap hex`
    float mLightMapTint[3];  // +0xec reset to the game's constant (0x1079104), then scaled by -strength, -tint
    struct Modifier {        // `modifiers type a c [b [d]]` (0x10 bytes)
        float a, b, c, d;    // a, c always; b, d when given
        // 0xb3ce50 (returns this; lane 261110t): all four 1.
        Modifier *Construct();
    } mModifiers[6];        // +0xf8, by the modifier type (the parser's table 0x10fb018)
    float mUnlitRoomColor[3];        // +0x158
    float mUnlitRoomThreshold;       // +0x164 -threshold
    float mSkylight[3];              // +0x168
    float mEnvironment[3];           // +0x174
    float mParticles[3];             // +0x180
    float mUIShade[3];               // +0x18c
    float mProjectiveShadowStrength; // +0x198
    float mFlatShade[3];             // +0x19c
    float mFlatShadeSims;            // +0x1a8 -sims
    float mFlatShadeObjects;         // +0x1ac -objects
    float mFlatShadeIndoors;         // +0x1b0 -indoors
    float mPortalSaturation;         // +0x1b4
    float mObjectLightThreshold;     // +0x1b8
    float mFogColor[3];              // +0x1bc
    float mFogA;                     // +0x1c8 the fog's second argument
    float mFogB;                     // +0x1cc its third
    struct Diffusion {               // `diffParam name value` (0x30 bytes)
        float insideFloorAmbient;    // +0
        float insideWallAmbient;     // +4
        float insideAmbientColor[3]; // +8
        float outsideFloorAmbient;   // +0x14
        float outsideWallAmbient;    // +0x18
        float outsideAmbientColor[3]; // +0x1c
        float wallThreshold;         // +0x28
        float floorThreshold;        // +0x2c
        // 0xb3cdf0 (returns this; lane 261110t): the ambients 0, the inside colour the game's 0x1079104, the outside
        // one its 0x10fb040, both thresholds 0x3e0c49ba (about 0.137).
        Diffusion *Construct();
    } mDiffusion;                   // +0x1d0
    float mTransitionA;              // +0x200 `transitionTimes`
    float mTransitionB;              // +0x204
    uint32_t m208[3];                // +0x208
};
static_assert(sizeof(LightingValues) == 0x214 - 0x20 && offsetof(LightingValues, mLights) == 0x4 &&
                  offsetof(LightingValues, mLightMap) == 0xc8 && offsetof(LightingValues, mModifiers) == 0xd8 &&
                  offsetof(LightingValues, mUnlitRoomColor) == 0x138 && offsetof(LightingValues, mFogColor) == 0x19c &&
                  offsetof(LightingValues, mDiffusion) == 0x1b0 && offsetof(LightingValues, mTransitionA) == 0x1e0,
              "LightingValues layout");

// One lighting state of the lighting script (0x214 bytes): its name, then its values. Copying copies the name's text
// (not the name's vtable or count, as cRZString's copy) and the values; the game's out-of-line copy constructor
// (0x2cb200) and assignment (0xb3d510) do the same, and the lighting state command's slot 4 has the assignment inline.
struct LightingState {
    cRZString mName;        // +0
    LightingValues mValues; // +0x20

    // 0xb3cfb0 (returns this; lane 261110t), the defaults: the name empty, nothing enabled, the seven lights and six
    // modifiers their constructors', no light map, the tint, environment, particles, UI and flat shades the game's
    // 0x1079104, the unlit-room colour, skylight and fog its 0x10fb040, threshold 0, projective shadow strength and the
    // four flat-shade scales 1, object light threshold 1000, fog 0 0, the diffusion's constructor, transition times 2 2,
    // the last three words 0. Every word of the 0x214 bytes is stored.
    LightingState *Construct();
    LightingState(const LightingState &other);
    LightingState &operator=(const LightingState &other);
    ~LightingState(); // the name's destructor (the game's 0x1d99b0, a copy of cRZString's)
};
static_assert(sizeof(LightingState) == 0x214, "LightingState layout");

// What the commands use of the lighting parser (cTSLightingParserState, the object a command's mState points at): the
// cBaseParser part (its vbptr at +8 leads to the cIBaseParser face), the named states and the state the open block
// fills. Its own vtable's slot 5 (0xb3eda0, not ours) finds a named state: copies it to `out` and gives its index.
struct cTSLightingParserFields {
    uint8_t mBaseParser[0x1ac];        // +0
    std::vector<LightingState> mStates; // +0x1ac
    LightingState mCurrent;            // +0x1b8 the state the open lightingState block fills

    bool FindState(const char *name, LightingState *out, uint32_t *index)
    {
        return VirtualCall<bool(__thiscall *)(void *, const char *, LightingState *, uint32_t *), 5>(this, name, out, index);
    }
};
static_assert(offsetof(cTSLightingParserFields, mStates) == 0x1ac && offsetof(cTSLightingParserFields, mCurrent) == 0x1b8,
              "cTSLightingParserFields layout");

// ---- lightingState (vtable 0xe8492c) ---------------------------------------------------------------------------------
// `lightingState name [: base]`: opens a block that fills the parser's current state, starting from the named state
// when there is one (the block then replaces it), else from the base named after the colon, else from the defaults;
// the block's end stores the state (replacing the one found, or added). cBlockCommandT<cTSLightingParserState> with two
// fields of its own (0x4c bytes). Slots 0..2, 6..8, 10, 11 are cBlockCommand's; slot 12 (shared_vf_b3d840, the
// deleting destructor of 0x4c bytes) is shared with another class.
class cLightingStateCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xe8492c;
    cTSLightingParserFields *State() const { return static_cast<cTSLightingParserFields *>(mState); }

    void SetContext(void *parser, void *stateInterface); // slot 3, 0xb3f150: sixteen child commands
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xb3e3a0
    const char *GetName() const;                         // slot 5, 0xb3f020: "lightingState"
    void EndBlock();                                     // slot 9, 0xb3f0d0

    uint8_t mReplacing; // +0x44 the block replaces a state found by name (else its state is added)
    uint8_t m45[3];
    uint32_t mIndex;    // +0x48 that state's index (or the base's: see Execute)
};
static_assert(offsetof(cLightingStateCommand, mReplacing) == 0x44 && offsetof(cLightingStateCommand, mIndex) == 0x48 &&
                  sizeof(cLightingStateCommand) == 0x4c,
              "cLightingStateCommand layout");

// ---- the lightingState block's commands ------------------------------------------------------------------------------
// cCommandT<cTSLightingParserState> (16 bytes): the parser's face at +4 (as SetContext stored it: these commands call
// it directly), the parser state at +0xc. Each sets values of the parser's current state. Slots other than 4 and 5 are
// cCommandT's. The game's GetName bodies of light (0x2ea380, also reached through another class's thunk 0x2ea36e) and
// fog (0xad96e0, shared_vf) are folded bodies and are not hooked.
class cLightingCommand : public nGZCommandParser::cCommandT {
public:
    cTSLightingParserFields *State() const { return static_cast<cTSLightingParserFields *>(mState); }
    LightingValues &Values() const { return State()->mCurrent.mValues; }
    nGZSceneGraph::ParserFace Face() const { return nGZSceneGraph::ParserFace(nGZSceneGraph::ParserFace::Direct{}, mParser); }
};

#define LIGHTING_COMMAND(cls, vtable, execute, name)                                                                   \
    class cls : public cLightingCommand {                                                                              \
    public:                                                                                                            \
        static constexpr uint32_t kVTable = vtable;                                                                    \
        static constexpr uint32_t kExecute = execute; /* the game's slot 4 */                                          \
        static constexpr uint32_t kGetName = name;    /* the game's slot 5 */                                          \
        void Execute(nGZCommandParser::cIArguments *args);                                                             \
        const char *GetName() const;                                                                                   \
        /* slot 6; only control, fog, lightMap, transitionTimes and unlitRoomColor have their own (hooked) */          \
        const char *GetHelp(uint32_t index) const;                                                                     \
    }

LIGHTING_COMMAND(cControlCommand, 0xe84418, 0xb3dab0, 0xb3efc0);                  // control -debugPortals -edith -optimize
LIGHTING_COMMAND(cDiffusionParamCommand, 0xe84310, 0xb3db90, 0xb3efd0);           // diffParam name value
LIGHTING_COMMAND(cEnvironmentShadeCommand, 0xe84784, 0xb3dd60, 0xb3efe0);         // environment color [scale]
LIGHTING_COMMAND(cFlatShadeCommand, 0xe84688, 0xb3de10, 0xb3eff0);                // flatShade [color [scale]] -sims ...
LIGHTING_COMMAND(cFogCommand, 0xe847f4, 0xb3df50, 0xad96e0);                      // fog color a b
LIGHTING_COMMAND(cLightCommand, 0xe843b4, 0xb3dff0, 0x2ea380);                    // light type direction color [intensity]
LIGHTING_COMMAND(cLightMapCommand, 0xe84848, 0xb3e1b0, 0xb3f000);                 // lightMap hex -strength -tint
LIGHTING_COMMAND(cLightingModifiersCommand, 0xe843e0, 0xb3e2c0, 0xb3f010);        // modifiers type a c [b [d]]
LIGHTING_COMMAND(cObjectLightThresholdCommand, 0xe84708, 0xb3e890, 0xb3f030);     // objectLightThreshold value
LIGHTING_COMMAND(cParticleShadeCommand, 0xe847bc, 0xb3e8c0, 0xb3f040);            // particles color [scale]
LIGHTING_COMMAND(cPortalSaturationCommand, 0xe846c8, 0xb3e970, 0xb3f050);         // portalSaturation value
LIGHTING_COMMAND(cProjectiveShadowStrengthCommand, 0xe84640, 0xb3e9a0, 0xb3f060); // projectiveShadowStrength value
LIGHTING_COMMAND(cSkylightShadeCommand, 0xe8474c, 0xb3e9d0, 0xb3f070);            // skylight color [scale]
LIGHTING_COMMAND(cTransitionTimesCommand, 0xe848c8, 0xb3ebc0, 0xb3f090);          // transitionTimes a b
LIGHTING_COMMAND(cUIShadeCommand, 0xe8460c, 0xb3ec20, 0xb3f0a0);                  // UIShade color [scale]
LIGHTING_COMMAND(cUnlitRoomColorCommand, 0xe84534, 0xb3ecc0, 0xb3f0b0);           // unlitRoomColor color [scale] -threshold

#undef LIGHTING_COMMAND
