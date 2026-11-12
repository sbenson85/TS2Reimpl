#include "gz/LightingCommands.h"

#include "gz/cArguments.h"
#include "misc/HexParse.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

#include <cmath>
#include <cstring>
#include <string.h> // _stricmp
#include <iterator>
#include <new>

namespace cp = nGZCommandParser;
using nGZSceneGraph::BlockChild;
using nGZSceneGraph::ChildBase;
using nGZSceneGraph::ParserFace;

namespace {
const char *Text(uint32_t rva) { return GameAddress<char>(rva); }
} // namespace

// ---- the lighting state ----------------------------------------------------------------------------------------------

namespace {
constexpr uint32_t kOnes = 0x1079104;  // the game's (1, 1, 1)
constexpr uint32_t kWhite = 0x10fb040; // the lighting script's default light colour

void CopyVector(float *to, uint32_t rva)
{
    const float *const from = GameAddress<const float>(rva);
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
}
} // namespace

LightingValues::Light *LightingValues::Light::Construct()
{
    CopyVector(color, kWhite);
    intensity = 1.0f;
    direction[0] = 0.0f;
    direction[1] = 0.0f;
    direction[2] = 0.0f;
    return this;
}

LightingValues::Modifier *LightingValues::Modifier::Construct()
{
    a = 1.0f;
    b = 1.0f;
    c = 1.0f;
    d = 1.0f;
    return this;
}

LightingValues::Diffusion *LightingValues::Diffusion::Construct()
{
    constexpr uint32_t kThreshold = 0x3e0c49ba;
    insideFloorAmbient = 0.0f;
    insideWallAmbient = 0.0f;
    CopyVector(insideAmbientColor, kOnes);
    outsideFloorAmbient = 0.0f;
    outsideWallAmbient = 0.0f;
    CopyVector(outsideAmbientColor, kWhite);
    std::memcpy(&wallThreshold, &kThreshold, 4);
    std::memcpy(&floorThreshold, &kThreshold, 4);
    return this;
}

LightingState *LightingState::Construct()
{
    mName.Construct();
    LightingValues &v = mValues;
    new (&v.mEnabled) std::bitset<14>();
    for (LightingValues::Light &light : v.mLights) light.Construct();
    v.mLightMap = 0;
    CopyVector(v.mLightMapTint, kOnes);
    for (LightingValues::Modifier &modifier : v.mModifiers) modifier.Construct();
    CopyVector(v.mUnlitRoomColor, kWhite);
    v.mUnlitRoomThreshold = 0.0f;
    CopyVector(v.mSkylight, kWhite);
    CopyVector(v.mEnvironment, kOnes);
    CopyVector(v.mParticles, kOnes);
    CopyVector(v.mUIShade, kOnes);
    v.mProjectiveShadowStrength = 1.0f;
    CopyVector(v.mFlatShade, kOnes);
    v.mFlatShadeSims = 1.0f;
    v.mFlatShadeObjects = 1.0f;
    v.mFlatShadeIndoors = 1.0f;
    v.mPortalSaturation = 1.0f;
    v.mObjectLightThreshold = 1000.0f;
    CopyVector(v.mFogColor, kWhite);
    v.mFogA = 0.0f;
    v.mFogB = 0.0f;
    v.mDiffusion.Construct();
    v.mTransitionA = 2.0f;
    v.mTransitionB = 2.0f;
    v.m208[0] = 0;
    v.m208[1] = 0;
    v.m208[2] = 0;
    return this;
}

LightingState::LightingState(const LightingState &other)
{
    mName.mVTable = GameAddress(cRZString::kVTable);
    // From the characters and length (0x2cb200 copies size + 1 characters of a short name, not its whole buffer).
    new (&mName.mString) std::string(other.mName.mString.data(), other.mName.mString.size());
    mName.mRefCount = 0;
    mValues = other.mValues;
}

LightingState &LightingState::operator=(const LightingState &other)
{
    // 0xb3d510 (lane 261110y): the name unless it is the same string, then the values.
    if (&mName.mString != &other.mName.mString) mName.mString.assign(other.mName.mString.data(), other.mName.mString.size());
    mValues = other.mValues;
    return *this;
}

LightingState::~LightingState() { mName.Destruct(); }

// ---- lightingState ---------------------------------------------------------------------------------------------------

void cLightingStateCommand::SetContext(void *parser, void *stateInterface)
{
    static constexpr BlockChild kChildren[] = {
        {0xe84310, ChildBase::Command}, // diffParam
        {0xe843b4, ChildBase::Command}, // light
        {0xe843e0, ChildBase::Command}, // modifiers
        {0xe84418, ChildBase::Command}, // control
        {0xe84534, ChildBase::Command}, // unlitRoomColor
        {0xe8460c, ChildBase::Command}, // UIShade
        {0xe84640, ChildBase::Command}, // projectiveShadowStrength
        {0xe84688, ChildBase::Command}, // flatShade
        {0xe846c8, ChildBase::Command}, // portalSaturation
        {0xe84708, ChildBase::Command}, // objectLightThreshold
        {0xe8474c, ChildBase::Command}, // skylight
        {0xe84784, ChildBase::Command}, // environment
        {0xe847bc, ChildBase::Command}, // particles
        {0xe847f4, ChildBase::Command}, // fog
        {0xe84848, ChildBase::Command}, // lightMap
        {0xe848c8, ChildBase::Command}, // transitionTimes
    };
    nGZSceneGraph::SetContextWithChildren(this, parser, stateInterface, kChildren, std::size(kChildren));
}

void cLightingStateCommand::Execute(cp::cIArguments *args)
{
    mReplacing = 1;
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 3);
    ParserFace(State()).PushOpenBlock(this);
    ParserFace(State()).PushNamespace(argv[0]);
    if (!State()->FindState(argv[0], &State()->mCurrent, &mIndex)) {
        // A new state: the defaults (0xb3cfb0), and the block's end adds it.
        alignas(4) uint8_t raw[sizeof(LightingState)];
        LightingState *defaults = reinterpret_cast<LightingState *>(raw)->Construct();
        State()->mCurrent = *defaults;
        defaults->~LightingState();
        mReplacing = 0;
    }
    if (static_cast<int32_t>(count) >= 2) {
        // `name : base`: the state starts from the base. ORIGINAL-BUG: the base's index replaces the one found for
        // the name, so a block that names an existing state and a base overwrites the base at its end.
        if (std::strcmp(argv[1], Text(0xda9e7c)) != 0) cp::ThrowFormatted(Text(0xe84970)); // "expecting ':'"
        if (count != 3) cp::ThrowFormatted(Text(0xe84980));                             // "no base description name given"
        if (!State()->FindState(argv[2], &State()->mCurrent, &mIndex))
            cp::ThrowFormatted(Text(0xe849a0), argv[2]); // "base description '%s' doesn't exist"
    }
    std::string &name = State()->mCurrent.mName.mString;
    name.assign(argv[0], std::strlen(argv[0]));
}

const char *cLightingStateCommand::GetName() const { return Text(0xe84960); }

void cLightingStateCommand::EndBlock()
{
    ParserFace(State()).PopNamespace();
    cTSLightingParserFields *state = State();
    if (mReplacing)
        state->mStates[mIndex] = state->mCurrent;
    else
        state->mStates.push_back(state->mCurrent);
}

// ---- the lightingState block's commands ------------------------------------------------------------------------------

namespace {

int32_t LookUp(uint32_t table, const char *name) { return GameAddress<const cp::EnumTable>(table)->Lookup(name); }
constexpr uint32_t kLightTypes = 0x10fb00c;    // the parser's light type names
constexpr uint32_t kModifierTypes = 0x10fb018; // its modifier type names

void Copy3(float *to, const float *from)
{
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
}
void Scale3(float *v, float scale)
{
    v[0] = scale * v[0];
    v[1] = scale * v[1];
    v[2] = scale * v[2];
}

// `name color [scale]` (environment, particles, skylight, UIShade, unlitRoomColor): the colour (face slot 39, copied
// from the pointer it returns), scaled by the second word when there is one.
void ColorAndScale(const cLightingCommand *command, cp::cIArguments *args, float *color)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 1, 2);
    float rgb[3];
    Copy3(color, command->Face().EvalColorRGB(rgb, argv[0]));
    if (static_cast<int32_t>(count) > 1) Scale3(color, command->Face().EvalFloat(argv[1]));
}

// A switch with one word, evaluated as a float into `value` when it is given.
void FloatSwitch(const cLightingCommand *command, cp::cIArguments *args, uint32_t name, float &value)
{
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(name), 1)) value = command->Face().EvalFloat(a[0]);
}

} // namespace

void cControlCommand::Execute(cp::cIArguments *args)
{
    static constexpr struct {
        uint32_t name;
        size_t bit;
    } kSwitches[] = {{0xe8450c, 11}, {0xe8451c, 12}, {0xe84524, 13}}; // debugPortals, edith, optimize
    for (const auto &s : kSwitches)
        if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(s.name), 1)) {
            const bool on = Face().EvalBool(a[0]);
            Values().mEnabled.set(s.bit, on);
        }
}

void cDiffusionParamCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(2);
    LightingValues::Diffusion &d = Values().mDiffusion;
    float rgb[3];
    if (_stricmp(argv[0], Text(0xdbec48)) == 0) d.insideFloorAmbient = Face().EvalFloat(argv[1]);
    else if (_stricmp(argv[0], Text(0xdbec88)) == 0) d.insideWallAmbient = Face().EvalFloat(argv[1]);
    else if (_stricmp(argv[0], Text(0xe84344)) == 0) Copy3(d.insideAmbientColor, Face().EvalVector3(rgb, argv[1], 1));
    else if (_stricmp(argv[0], Text(0xdbec5c)) == 0) d.outsideFloorAmbient = Face().EvalFloat(argv[1]);
    else if (_stricmp(argv[0], Text(0xdbec9c)) == 0) d.outsideWallAmbient = Face().EvalFloat(argv[1]);
    else if (_stricmp(argv[0], Text(0xe84358)) == 0) Copy3(d.outsideAmbientColor, Face().EvalVector3(rgb, argv[1], 1));
    else if (_stricmp(argv[0], Text(0xe8436c)) == 0) d.floorThreshold = Face().EvalFloat(argv[1]);
    else if (_stricmp(argv[0], Text(0xe8437c)) == 0) d.wallThreshold = Face().EvalFloat(argv[1]);
    else cp::ThrowFormatted(Text(0xe8438c), argv[0]); // "unknown diffusion parameter: '%s'\n"
}

void cEnvironmentShadeCommand::Execute(cp::cIArguments *args)
{
    ColorAndScale(this, args, Values().mEnvironment);
    Values().mEnabled.set(9);
}

void cFlatShadeCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 0, 2);
    if (static_cast<int32_t>(count) > 0) {
        float rgb[3];
        Copy3(Values().mFlatShade, Face().EvalColorRGB(rgb, argv[0]));
        if (static_cast<int32_t>(count) > 1) Scale3(Values().mFlatShade, Face().EvalFloat(argv[1]));
    }
    FloatSwitch(this, args, 0xdd0604, Values().mFlatShadeSims);    // "sims"
    FloatSwitch(this, args, 0xda6400, Values().mFlatShadeObjects); // "objects"
    FloatSwitch(this, args, 0xe846bc, Values().mFlatShadeIndoors); // "indoors"
}

void cFogCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(3);
    float rgb[3];
    Copy3(Values().mFogColor, Face().EvalColorRGB(rgb, argv[0]));
    Values().mFogA = Face().EvalFloat(argv[1]);
    Values().mFogB = Face().EvalFloat(argv[2]);
    Values().mEnabled.set(8);
}

void cLightCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 3, 4);
    const uint32_t type = static_cast<uint32_t>(LookUp(kLightTypes, argv[0]));
    float vector[3], direction[3], color[3];
    Copy3(direction, Face().EvalVector3(vector, argv[1], 1));
    // Normalised: the squares summed in floats, the root taken in double.
    const float squares = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
    const float inverse = 1.0f / static_cast<float>(std::sqrt(static_cast<double>(squares)));
    direction[0] = direction[0] * inverse;
    direction[1] = direction[1] * inverse;
    direction[2] = direction[2] * inverse;
    Copy3(color, Face().EvalVector3(vector, argv[2], 1));
    const float intensity = count == 4 ? Face().EvalFloat(argv[3]) : 1.0f;
    // The type indexes the seven lights unchecked (a type past 6 writes over the values after them), then the
    // enabled bits, which throw out_of_range past 13.
    LightingValues::Light *lights = Values().mLights;
    Copy3(lights[type].color, color);
    lights[type].intensity = intensity;
    Copy3(lights[type].direction, direction);
    Values().mEnabled.set(type);
}

void cLightMapCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(1);
    Values().mLightMap = hexparse::ParseHex32(argv[0]);
    Copy3(Values().mLightMapTint, GameAddress<const float>(0x1079104)); // the game's (1, 1, 1)
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe848b0), 1)) // "strength"
        Scale3(Values().mLightMapTint, Face().EvalFloat(a[0]));
    if (const char *const *a = args->TakeSwitchArgumentsExactly(Text(0xe848bc), 1)) { // "tint"
        float rgb[3], tint[3];
        Copy3(tint, Face().EvalColorRGB(rgb, a[0]));
        float *t = Values().mLightMapTint;
        t[0] = tint[0] * t[0];
        t[1] = tint[1] * t[1];
        t[2] = tint[2] * t[2];
    }
}

void cLightingModifiersCommand::Execute(cp::cIArguments *args)
{
    uint32_t count;
    const char *const *argv = args->Positional(&count, 3, 5);
    const int32_t type = LookUp(kModifierTypes, argv[0]);
    LightingValues::Modifier *modifiers = Values().mModifiers; // indexed unchecked, as the original
    modifiers[type].a = Face().EvalFloat(argv[1]);
    modifiers[type].c = Face().EvalFloat(argv[2]);
    if (static_cast<int32_t>(count) > 3) modifiers[type].b = Face().EvalFloat(argv[3]);
    if (static_cast<int32_t>(count) > 4) modifiers[type].d = Face().EvalFloat(argv[4]);
    args->CheckSwitchesUsed();
}

void cObjectLightThresholdCommand::Execute(cp::cIArguments *args)
{
    Values().mObjectLightThreshold = Face().EvalFloat(args->PositionalExactly(1)[0]);
}

void cParticleShadeCommand::Execute(cp::cIArguments *args)
{
    ColorAndScale(this, args, Values().mParticles);
    Values().mEnabled.set(10);
}

void cPortalSaturationCommand::Execute(cp::cIArguments *args)
{
    Values().mPortalSaturation = Face().EvalFloat(args->PositionalExactly(1)[0]);
}

void cProjectiveShadowStrengthCommand::Execute(cp::cIArguments *args)
{
    Values().mProjectiveShadowStrength = Face().EvalFloat(args->PositionalExactly(1)[0]);
}

void cSkylightShadeCommand::Execute(cp::cIArguments *args)
{
    ColorAndScale(this, args, Values().mSkylight);
    Values().mEnabled.set(7);
}

void cTransitionTimesCommand::Execute(cp::cIArguments *args)
{
    const char *const *argv = args->PositionalExactly(2);
    // ORIGINAL-BUG: both times are the first word's; the second word is never read.
    Values().mTransitionA = Face().EvalFloat(argv[0]);
    Values().mTransitionB = Face().EvalFloat(argv[0]);
}

void cUIShadeCommand::Execute(cp::cIArguments *args) { ColorAndScale(this, args, Values().mUIShade); }

void cUnlitRoomColorCommand::Execute(cp::cIArguments *args)
{
    ColorAndScale(this, args, Values().mUnlitRoomColor);
    FloatSwitch(this, args, 0xdcaba8, Values().mUnlitRoomThreshold); // "threshold"
}

const char *cControlCommand::GetName() const { return Text(0xe60d1c); }
const char *cDiffusionParamCommand::GetName() const { return Text(0xe84338); }
const char *cEnvironmentShadeCommand::GetName() const { return Text(0xe847ac); }
const char *cFlatShadeCommand::GetName() const { return Text(0xe846b0); }
const char *cFogCommand::GetName() const { return Text(0xe7aff0); }
const char *cLightCommand::GetName() const { return Text(0xdb6c88); }
const char *cLightMapCommand::GetName() const { return Text(0xe84870); }
const char *cLightingModifiersCommand::GetName() const { return Text(0xe84408); }
const char *cObjectLightThresholdCommand::GetName() const { return Text(0xe84730); }
const char *cParticleShadeCommand::GetName() const { return Text(0xe847e4); }
const char *cPortalSaturationCommand::GetName() const { return Text(0xe846f0); }
const char *cProjectiveShadowStrengthCommand::GetName() const { return Text(0xe84668); }
const char *cSkylightShadeCommand::GetName() const { return Text(0xe84774); }
const char *cTransitionTimesCommand::GetName() const { return Text(0xe848f0); }
const char *cUIShadeCommand::GetName() const { return Text(0xe84634); }
const char *cUnlitRoomColorCommand::GetName() const { return Text(0xe8455c); }

// The help lines (slot 6) of the five commands that have them (lane 261110y); the others keep cCommand's.
const char *cControlCommand::GetHelp(uint32_t index) const
{
    if (index == 0) return Text(0xe84440);
    if (index == 1) return Text(0xe84460);
    return nullptr;
}
const char *cFogCommand::GetHelp(uint32_t) const { return Text(0xe8481c); }
const char *cLightMapCommand::GetHelp(uint32_t) const { return Text(0xe8487c); }
const char *cTransitionTimesCommand::GetHelp(uint32_t) const { return Text(0xe84900); }
const char *cUnlitRoomColorCommand::GetHelp(uint32_t index) const
{
    if (index == 0) return Text(0xe8456c);
    if (index == 1) return Text(0xe84598);
    return nullptr;
}
