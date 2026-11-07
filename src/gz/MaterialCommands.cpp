#include "sg/cTextureManager.h"
#include "gz/MaterialCommands.h"

#include "gz/GraphicServices.h"
#include "gz/cArguments.h"
#include "gz/cIGZString.h"
#include "misc/UtilitiesE.h"   // GetCachedQueriedObject
#include "rz/StaticGetters.h"  // GetDirectorFrameworkObject
#include "rz/StaticGetters2.h" // Dispatch8
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <new>

namespace nGZSceneGraph {

namespace cp = nGZCommandParser;

namespace {

// The framework object's slot 3: GetClassObject(class id, interface id, out).
void GetClassObject(uint32_t classId, uint32_t iid, UnknownRef &out)
{
    void *framework = GetDirectorFrameworkObject();
    void **slot = out.AsPPVoid();
    VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, classId, iid, slot);
}

const char *Text(uint32_t rva) { return GameAddress<char>(rva); }

// 0x18ae40 (__cdecl, no arguments; gz/GraphicServices.h): the render device word the shader code is checked against.
uint32_t CurrentDevice() { return reinterpret_cast<uint32_t>(nGZGraphic4::GetCurrentDeviceCached()); }
// 0x18ddf0 / 0x18dcf0 (__cdecl, no arguments; not ours): the shader compiler service and the vertex format builder,
// each asked for once and cached in a global.
void *ShaderCompiler() { return nGZGraphic4::GetShaderProgramsManagerCached(); }
void *VertexFormatBuilder() { return nGZGraphic4::GetVertexFormatsManagerCached(); }

constexpr uint32_t kWrongNumberOfArguments = 0xdb59cc;
constexpr uint32_t kTopLevelOnly = 0xe7a174; // "Must appear as a top level command"

// The shader interfaces (class 0x7c17ff25, interface 0x5c17ff18) a shader and a default state block build.
constexpr uint32_t kShaderClass = 0x7c17ff25;
constexpr uint32_t kShaderInterface = 0x5c17ff18;

} // namespace

// ---- the blocks' slot 3 ----------------------------------------------------------------------------------------------

void SetContextWithChildren(cp::cBlockCommandT *block, void *parser, void *stateInterface, const BlockChild *children,
                            size_t count)
{
    block->SetContextAdjusted(parser, stateInterface);
    for (size_t i = 0; i < count; ++i) {
        void *child = nullptr;
        switch (children[i].base) {
        case ChildBase::Command:
            if (void *memory = ::operator new(sizeof(cp::cCommandT))) {
                cp::cCommand *command = static_cast<cp::cCommand *>(memory)->Construct();
                command->mVTable = GameAddress(children[i].vtable);
                child = command;
            }
            break;
        case ChildBase::MetaCommand:
            if (void *memory = ::operator new(sizeof(cp::cMetaCommandT))) {
                cp::cMetaCommand *command = static_cast<cp::cMetaCommand *>(memory)->Construct();
                command->mVTable = GameAddress(children[i].vtable);
                child = command;
            }
            break;
        case ChildBase::Block:
            if (void *memory = ::operator new(sizeof(cp::cBlockCommandT))) {
                cp::cBlockCommandBase *command = static_cast<cp::cBlockCommandBase *>(memory)->Construct();
                command->mVTable = GameAddress(children[i].vtable);
                child = command;
            }
            break;
        }
        VirtualCall<void(__thiscall *)(void *, void *), 10>(block, child); // AddCommand
    }
}

// ---- materialDefinition ----------------------------------------------------------------------------------------------

void cMaterialDefinitionCommand::SetContext(void *parser, void *stateInterface)
{
    static constexpr Child kChildren[] = {
        {0xe7a29c, ChildBase::Command}, // setDefinition
        {0xe7a2f8, ChildBase::Command}, // addParam
    };
    SetContextWithChildren(parser, stateInterface, kChildren, std::size(kChildren));
}

void cMaterialDefinitionCommand::Execute(cp::cIArguments *args)
{
    cp::ThrowUnless(State()->mBlock == 0, Text(kTopLevelOnly));
    args->CheckSwitchesUsed();
    const char *const *name = args->PositionalExactly(1);
    ParserFace(State()).PushOpenBlock(this);
    ParserFace(State()).PushNamespace(name[0]);
    // A new material definition (class 0x49596978, interface 0x49596972), named after the block.
    GetClassObject(0x49596978, 0x49596972, State()->mMaterialDefinition);
    VirtualCall<void(__thiscall *)(void *, const char *), 10>(State()->mMaterialDefinition.Get(), name[0]);
}

const char *cMaterialDefinitionCommand::GetName() const { return Text(0xe7a364); }

void cMaterialDefinitionCommand::EndBlock()
{
    ParserFace(State()).PopNamespace();
    if (State()->mMaterialDefinition) {
        // The material manager's slot 23 registers the definition (the second argument: replace an existing one).
        void *manager = GetCachedQueriedObject();
        VirtualCall<void(__thiscall *)(void *, cIGZUnknown *, uint32_t), 23>(manager, State()->mMaterialDefinition.Get(), 1);
    }
    State()->mMaterialDefinition.Assign(nullptr);
}

// ---- sampler ---------------------------------------------------------------------------------------------------------

void cSamplerCommand::SetContext(void *parser, void *stateInterface)
{
    static constexpr Child kChildren[] = {
        {0xe7a55c, ChildBase::Command}, // texture
        {0xe7b0b0, ChildBase::Command}, // textureAddressing
        {0xe7b134, ChildBase::Command}, // textureFilterHint
        {0xe7b1a4, ChildBase::Command}, // textureFilter
        {0xe7b1e0, ChildBase::Command}, // textureMipFilterHint
        {0xe7b224, ChildBase::Command}, // textureMipFilter
    };
    SetContextWithChildren(parser, stateInterface, kChildren, std::size(kChildren));
}

void cSamplerCommand::Execute(cp::cIArguments *args)
{
    ParserFace(State()).PushOpenBlock(this);
    if (State()->mBlock != 4) return;
    args->CheckSwitchesUsed();
    cp::ThrowUnless(State()->mInSampler == 0, Text(0xe7bbf8));              // "Nested sampler block"
    cp::ThrowUnless(State()->mHasPixelProgram != 0, Text(0xe7bc10));        // "No pixel program defined"
    cp::ThrowUnless(State()->mTextureStageCascade == -1, Text(0xe7bc30));   // "Cannot mix sampler definitions with ..."
    const char *const *name = args->PositionalExactly(1);
    State()->mInSampler = 1;
    // The pixel program's constant table names the samplers; a name it does not know is a sampler number.
    uint32_t found = 0, count = 0;
    const uint32_t *indices = nullptr;
    if (cIGZUnknown *program = State()->mPixelProgram.Get()) {
        SamplerList list;
        const SamplerList *result =
            VirtualCall<const SamplerList *(__thiscall *)(void *, SamplerList *, const char *), 7>(program, &list, name[0]);
        found = result->found;
        count = result->count;
        indices = result->indices;
    }
    cMaterialParserFields *state = State();
    if (found) {
        state->mSamplerCount = count;
        std::memmove(State()->mSamplers, indices, count * sizeof(uint32_t));
    } else {
        state->mSamplerCount = 1;
        uint32_t sampler = ParserFace(State()).EvalUint(name[0]);
        State()->mSamplers[0] = sampler;
    }
    for (uint32_t i = 0; i < State()->mSamplerCount; ++i)
        cp::ThrowUnless(State()->mSamplers[i] < 16, Text(0xe7bc70)); // "Bad sampler #"
}

const char *cSamplerCommand::GetName() const { return Text(0xe7bbf0); }

void cSamplerCommand::EndBlock()
{
    State()->mInSampler = 0;
    State()->mSamplerCount = 0;
}

// ---- shaderSource ----------------------------------------------------------------------------------------------------

namespace {
// The original's vector<char> insert at the end (0x1be8b6, an STL instantiation: our STL's here).
void AppendText(std::vector<char> &source, const char *text, size_t length) { source.insert(source.end(), text, text + length); }
} // namespace

void cShaderSourceCommand::Execute(cp::cIArguments *args)
{
    // One argument: a line of source. None: the lines up to endShaderSource are the source (the block opens).
    uint32_t count;
    const char *const *line = args->Positional(&count, 0, 1);
    if (static_cast<int32_t>(count) <= 0) {
        ParserFace(State()).PushMetaBlock(this);
        args->CheckSwitchesUsed();
        return;
    }
    const char *text = line[0];
    AppendText(State()->mSource, text, std::strlen(text));
    State()->mSource.push_back('\n');
    args->CheckSwitchesUsed();
}

const char *cShaderSourceCommand::GetName() const { return Text(0xe7b320); }

void cShaderSourceCommand::Open(const char *rest)
{
    uint32_t length;
    if (cp::NextToken(&length, rest) == nullptr) ParserFace(State()).PushMetaBlock(this);
}

bool cShaderSourceCommand::HandleLine(cIGZString *line, uint8_t active)
{
    // The lengths of "endShaderSource" and "__parseCommand", kept by the game in two globals.
    const uint32_t endLength = *GameAddress<uint32_t>(0x1070050);
    uint32_t length;
    const char *word = cp::NextToken(&length, line->ToChar());
    if (word && length == endLength && std::strncmp(word, Text(0xe795b8), endLength) == 0) {
        ParserFace(State()).PopMetaBlock();
        return false;
    }
    if (!active) return false;
    const uint32_t commandLength = *GameAddress<uint32_t>(0x1070054);
    if (word && length == commandLength && std::strncmp(word, Text(0xe795c8), commandLength) == 0) {
        // The rest of the line runs as a command line.
        line->FromChar(word + commandLength);
        return true;
    }
    ParserFace(State()).ExpandVariables(line, false);
    const char *data = line->Data();
    uint32_t size = line->Strlen();
    AppendText(State()->mSource, data, size);
    State()->mSource.push_back('\n');
    return false;
}

// ---- shader ----------------------------------------------------------------------------------------------------------

void cShaderCommand::SetContext(void *parser, void *stateInterface)
{
    static constexpr Child kChildren[] = {
        {0xe7bc84, ChildBase::Block},   // pass
        {0xe7b7e8, ChildBase::Command}, // validateRenderShaderContext
        {0xe7b914, ChildBase::Command}, // vertexFormatPred
        {0xe7b98c, ChildBase::Command}, // numLightsPred
        {0xe7b954, ChildBase::Command}, // flagPred
        {0xe7b9c8, ChildBase::Command}, // numPoseTransformsPred
        {0xe7ba18, ChildBase::Command}, // numMorphTargetsPred
        {0xe7ba58, ChildBase::Command}, // viewerRenderTypePred
        {0xe7ba9c, ChildBase::Command}, // supportedRenderShaderContexts
        {0xe7a4e0, ChildBase::Command}, // sendNotifyString
    };
    SetContextWithChildren(parser, stateInterface, kChildren, std::size(kChildren));
}

namespace {
// What a shader block starts and ends with: no pass, no sampler, no texture stage cascade.
void ResetShaderState(const cShaderCommand *command, bool inShader)
{
    command->State()->mInShader = inShader;
    command->State()->mInPass = 0;
    command->State()->m26b = 0;
    command->State()->mInSampler = 0;
    command->State()->mPassIndex = -1;
    command->State()->mTextureStageCascade = -1;
    command->State()->mSamplerCount = 0;
}
} // namespace

void cShaderCommand::Execute(cp::cIArguments *args)
{
    ParserFace(State()).PushOpenBlock(this);
    int32_t block = State()->mBlock;
    if (block != 2 && block != 6) return;
    cp::ThrowUnless(args->PositionalCount() == 0, Text(kWrongNumberOfArguments));
    cp::ThrowUnless(State()->mInShader == 0, Text(0xe7bf48)); // "Nested shader block"
    int32_t layer = 0;
    State()->mBlock = 4;
    ResetShaderState(this, true);
    if (const char *const *value = args->TakeSwitchArgumentsExactly(Text(0xdba3c8), 1)) // "layer"
        layer = ParserFace(State()).EvalInt(value[0]);
    bool ignoreDefault = args->TakeSwitch(Text(0xe7bf5c)); // "ignoreDefaultState"
    cMaterialParserFields *state = State();
    uint32_t renderType = (state->mFlags >> 11) & 0xf;
    cIGZUnknown *defaultState;
    if (!ignoreDefault && renderType < state->mDefaultStates.size() &&
        (defaultState = state->mDefaultStates[renderType].Get()) != nullptr) {
        // The default state of this viewer render type gives a copy of its shader (its slot 3).
        void **out = state->mShader.AsPPVoid();
        VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 3>(defaultState, kShaderInterface, out);
    } else {
        GetClassObject(kShaderClass, kShaderInterface, State()->mShader);
    }
    VirtualCall<void(__thiscall *)(void *, int32_t), 4>(State()->mShader.Get(), layer); // SetLayer
    args->CheckSwitchesUsed();
}

const char *cShaderCommand::GetName() const { return Text(0xe7bf40); }

void cShaderCommand::EndBlock()
{
    switch (State()->mBlock) {
    case 4: {
        // A shader (class 0xebfb3834, interface 0xfc17e651: an nGZSceneGraph::cShader, vtable 0xe758dc) is built from
        // the block's shader description and checked against the render shader context: valid shaders are kept (block
        // 5), others are not (block 6). Its slot 8 (0xaa5030, ret 0x14) takes five arguments: the device (0x18ae40, no
        // arguments), the description's slot 21 (cShaderInit 0x142840: its +8 data, no arguments, ret 0), the context,
        // the context data's address and the +0x268 byte. The original reads the three fields before the two calls.
        UnknownRef validator; // receiver: nGZSceneGraph::cShader
        GetClassObject(0xebfb3834, 0xfc17e651, validator);
        cMaterialParserFields *state = State();
        const uint32_t flag = state->m268; // pushed zero-extended (movzx), as the original does
        void *const contextData = state->mRenderShaderContextData;
        const uint32_t context = state->mRenderShaderContext;
        void *const shaderData = VirtualCall<void *(__thiscall *)(void *), 21>(state->mShader.Get());
        const uint32_t device = CurrentDevice();
        using ValidateFn = bool(__thiscall *)(void *, uint32_t, void *, uint32_t, void *, uint32_t);
        bool valid = VirtualCall<ValidateFn, 8>(validator.Get(), device, shaderData, context, contextData, flag);
        if (valid) {
            State()->mValidShader = validator;
            State()->mBlock = 5;
        } else {
            State()->mBlock = 6;
        }
    }
        [[fallthrough]];
    case 1:
    case 5:
    case 6:
        State()->mShader.Assign(nullptr);
        ResetShaderState(this, false);
        break;
    default:
        break;
    }
}

// ---- defaultState ----------------------------------------------------------------------------------------------------

void cDefaultStateCommand::SetContext(void *parser, void *stateInterface)
{
    static constexpr Child kChildren[] = {
        {0xe7a37c, ChildBase::Command}, // renderTarget
        {0xe7a93c, ChildBase::Command}, // ffPerVertexColor
        {0xe7aba8, ChildBase::Command}, // cullMode
        {0xe7abe0, ChildBase::Command}, // fillMode
        {0xe7ac18, ChildBase::Command}, // shading
        {0xe7ac4c, ChildBase::Command}, // colorWriteEnable
        {0xe7aca0, ChildBase::Command}, // alphaBlend
        {0xe7ace8, ChildBase::Command}, // alphaTest
        {0xe7ad20, ChildBase::Command}, // alphaTestFunction
        {0xe7ad60, ChildBase::Command}, // depthTest
        {0xe7adac, ChildBase::Command}, // depthTestFunction
        {0xe7aee0, ChildBase::Command}, // stencil
        {0xe7af18, ChildBase::Command}, // stencilFunction
        {0xe7af68, ChildBase::Command}, // stencilOperation
        {0xe7afc8, ChildBase::Command}, // fog
        {0xe7b078, ChildBase::Command}, // addSpecular
        {0xe7b0b0, ChildBase::Command}, // textureAddressing
        {0xe7b0f0, ChildBase::Command}, // texCoordInterpolation
        {0xe7b7e8, ChildBase::Command}, // validateRenderShaderContext
    };
    SetContextWithChildren(parser, stateInterface, kChildren, std::size(kChildren));
}

void cDefaultStateCommand::Execute(cp::cIArguments *args)
{
    cp::ThrowUnless(State()->mBlock == 0, Text(kTopLevelOnly));
    args->CheckSwitchesUsed();
    const char *const *a = args->PositionalExactly(1);
    uint32_t renderType = ParserFace(State()).EvalUint(a[0]);
    cp::ThrowUnless(renderType < 16, Text(0xe7b8f0)); // "Viewer render type out of range"
    ParserFace(State()).PushOpenBlock(this);
    State()->mBlock = 3;
    GetClassObject(kShaderClass, kShaderInterface, State()->mShader);
    // The default states grow to hold this render type (never shrink); the block's shader is stored there.
    std::vector<UnknownRef> &defaults = State()->mDefaultStates;
    if (defaults.size() < renderType + 1) defaults.resize(renderType + 1);
    State()->mDefaultStates[renderType] = State()->mShader;
    // Every sampler, in order.
    State()->mSamplerCount = 16;
    for (uint32_t i = 0; i < 16; ++i) State()->mSamplers[i] = i;
}

const char *cDefaultStateCommand::GetName() const { return Text(0xe7bfe0); }

void cDefaultStateCommand::EndBlock()
{
    if (State()->mBlock == 6) {
        // The shader did not validate: its fallback state (interface 0x2d2f66a8: the shader's own, slot 24, or a new
        // one, class 0x2d2f66ad, given to the shader's slot 10) gets slot 19 with 0x10.
        UnknownRef fallback;
        bool found = VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 24>(State()->mShader.Get(), 0x2d2f66a8,
                                                                                    fallback.AsPPVoid());
        if (!found) {
            GetClassObject(0x2d2f66ad, 0x2d2f66a8, fallback);
            VirtualCall<void(__thiscall *)(void *, cIGZUnknown *), 10>(State()->mShader.Get(), fallback.Get());
        }
        VirtualCall<void(__thiscall *)(void *, uint32_t), 19>(fallback.Get(), 0x10);
    }
    State()->mBlock = 0;
    State()->mShader.Assign(nullptr);
    State()->mSamplerCount = 0;
}

// ---- pass ------------------------------------------------------------------------------------------------------------

void cPassCommand::SetContext(void *parser, void *stateInterface)
{
    static constexpr Child kChildren[] = {
        {0xe7b2b4, ChildBase::Command}, // outputVertexFormat
        {0xe7bb68, ChildBase::Block},   // stage
        {0xe7bbbc, ChildBase::Block},   // sampler
        {0xe7aba8, ChildBase::Command}, // cullMode
        {0xe7abe0, ChildBase::Command}, // fillMode
        {0xe7ac18, ChildBase::Command}, // shading
        {0xe7ac4c, ChildBase::Command}, // colorWriteEnable
        {0xe7a828, ChildBase::Command}, // ffIdentityModelTransform
        {0xe7a898, ChildBase::Command}, // ffScaleModelTransform
        {0xe7a8dc, ChildBase::Command}, // ffLighting
        {0xe7a650, ChildBase::Command}, // nonStandardLight
        {0xe7a37c, ChildBase::Command}, // renderTarget
        {0xe7a93c, ChildBase::Command}, // ffPerVertexColor
        {0xe7aca0, ChildBase::Command}, // alphaBlend
        {0xe7ace8, ChildBase::Command}, // alphaTest
        {0xe7ad20, ChildBase::Command}, // alphaTestFunction
        {0xe7ad60, ChildBase::Command}, // depthTest
        {0xe7adac, ChildBase::Command}, // depthTestFunction
        {0xe7adec, ChildBase::Command}, // forceDepthTestFailCondition
        {0xe7ae88, ChildBase::Command}, // applyStencilStateForOverlappingReflections
        {0xe7aee0, ChildBase::Command}, // stencil
        {0xe7af18, ChildBase::Command}, // stencilFunction
        {0xe7af68, ChildBase::Command}, // stencilOperation
        {0xe7afc8, ChildBase::Command}, // fog
        {0xe7aff8, ChildBase::Command}, // colorScalar
        {0xe7b078, ChildBase::Command}, // addSpecular
        {0xe7b0f0, ChildBase::Command}, // texCoordInterpolation
        {0xe7a9dc, ChildBase::Command}, // ffMaterialCoef
        {0xe7a4a0, ChildBase::Command}, // renderClipSpaceRect
        {0xe7a7bc, ChildBase::Command}, // textureMatrixAnimation
        {0xe7a690, ChildBase::Command}, // textureTileAnimation
        {0xe7a520, ChildBase::Command}, // ffDepthOffset
    };
    SetContextWithChildren(parser, stateInterface, kChildren, std::size(kChildren));
}

namespace {
// What a pass starts and ends with: no programs, no sampler, no texture stage cascade.
void ResetPassState(cMaterialParserFields *state)
{
    state->m26b = 0;
    state->mInSampler = 0;
    state->mFixedFunctionPass = 0;
    state->mHasPreprocessProgram = 0;
    state->mHasVertexProgram = 0;
    state->mHasPixelProgram = 0;
    state->mTextureStageCascade = -1;
    state->mSamplerCount = 0;
}

using ShaderPassFlagFn = void(__thiscall *)(void *, int32_t pass, uint32_t value);
template <int Slot> void ShaderPassCall(cMaterialParserFields *state, uint32_t value)
{
    VirtualCall<ShaderPassFlagFn, Slot>(state->mShader.Get(), state->mPassIndex, value);
}
} // namespace

void cPassCommand::Execute(cp::cIArguments *args)
{
    ParserFace(State()).PushOpenBlock(this);
    if (State()->mBlock != 4) return;
    cp::ThrowUnless(args->PositionalCount() == 0, Text(kWrongNumberOfArguments));
    cp::ThrowUnless(State()->mInPass == 0, Text(0xe7bcc0)); // "Nested pass block"
    State()->mPixelProgram.Assign(nullptr);
    State()->mInPass = 1;
    ResetPassState(State());
    State()->mOutputVertexFormat.clear();
    ++State()->mPassIndex;
    if (args->TakeSwitch(Text(0xe79080))) { // "fixedFunction"
        State()->mFixedFunctionPass = 1;
        // The pass's fixed-function state (class 0xca0ca1d9, interface 0xca0ca1d4) goes to the shader's slot 9; slot 14
        // (the vertex program) is cleared.
        UnknownRef fixedFunction;
        GetClassObject(0xca0ca1d9, 0xca0ca1d4, fixedFunction);
        VirtualCall<void(__thiscall *)(void *, int32_t, cIGZUnknown *), 9>(State()->mShader.Get(), State()->mPassIndex,
                                                                           fixedFunction.Get());
        VirtualCall<void(__thiscall *)(void *, int32_t, cIGZUnknown *), 14>(State()->mShader.Get(), State()->mPassIndex,
                                                                            nullptr);
    }
    if (args->TakeSwitch(Text(0xe7bcd4))) ShaderPassCall<16>(State(), 1); // "useGeomVertexDataState"
    if (args->TakeSwitch(Text(0xe7bcec))) ShaderPassCall<17>(State(), 1); // "modifiedEachFrameHint"
    if (args->TakeSwitch(Text(0xe7bd04))) ShaderPassCall<18>(State(), 1); // "renderEachFrame"
    if (args->TakeSwitch(Text(0xe7bd14))) ShaderPassCall<18>(State(), 1); // "renderAlways": the same slot
    if (args->TakeSwitch(Text(0xe7bd24))) ShaderPassCall<19>(State(), 1); // "clipAlways"
    args->CheckSwitchesUsed();
}

const char *cPassCommand::GetName() const { return Text(0xe7bcb8); }

void cPassCommand::EndBlock()
{
    if (State()->mBlock == 4) {
        cMaterialParserFields *state = State();
        cp::ThrowUnless(state->mFixedFunctionPass != 0 || state->mHasVertexProgram != 0, Text(0xe7bd30)); // "Missing vertex program"
        state = State();
        cp::ThrowUnless(state->mHasPreprocessProgram != 0 || state->mOutputVertexFormat.empty(), Text(0xe7bd48)); // "Missing preprocess ..."
        state = State();
        cp::ThrowUnless(state->mHasPreprocessProgram == 0 || !state->mOutputVertexFormat.empty(), Text(0xe7bd6c)); // "Missing output ..."
        state = State();
        cp::ThrowUnless(state->mHasPixelProgram == 0 || state->mTextureStageCascade == -1, Text(0xe7b5e8));
        if (!State()->mOutputVertexFormat.empty()) {
            // The output vertex format: started (slot 3), each element added (slot 4), then built (slot 5) and given to
            // the shader's slot 7 for this pass. The builder is asked for again for every call.
            UnknownRef format;
            VirtualCall<void(__thiscall *)(void *), 3>(VertexFormatBuilder());
            for (const OutputVertexElement *e = State()->mOutputVertexFormat.data();
                 e != State()->mOutputVertexFormat.data() + State()->mOutputVertexFormat.size(); ++e) {
                void *builder = VertexFormatBuilder();
                VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 4>(builder, 0, e->a, e->b, e->c);
            }
            void *builder = VertexFormatBuilder();
            VirtualCall<void(__thiscall *)(void *, void **), 5>(builder, format.AsPPVoid());
            VirtualCall<void(__thiscall *)(void *, int32_t, cIGZUnknown *), 7>(State()->mShader.Get(), State()->mPassIndex,
                                                                               format.Get());
        }
    }
    State()->mPixelProgram.Assign(nullptr);
    State()->mInPass = 0;
    ResetPassState(State());
}

// ---- shaderProgram ---------------------------------------------------------------------------------------------------

void cShaderProgramCommand::SetContext(void *parser, void *stateInterface)
{
    static constexpr Child kChildren[] = {
        {0xe7b2f0, ChildBase::MetaCommand}, // shaderSource
        {0xe7b334, ChildBase::Command},     // shaderFragment
        {0xe7b384, ChildBase::Command},     // bindShaderProgramConstants
    };
    SetContextWithChildren(parser, stateInterface, kChildren, std::size(kChildren));
    // Room for 4 KB of source.
    State()->mSource.reserve(0x1000);
}

void cShaderProgramCommand::ResetSource()
{
    std::vector<char> &source = State()->mSource;
    source.clear();
    if (source.capacity() > 0x1000) {
        // A buffer grown past 4 KB is given back (an empty vector moved in) and a 4 KB one taken.
        source = std::vector<char>();
        source.reserve(0x1000);
    }
}

namespace {
// The parser's name tables shaderProgram looks its options up in: the targets (0x10f6b34) and the compile methods
// (0x10f6b40).
int32_t LookUp(uint32_t table, const char *name) { return GameAddress<const cp::EnumTable>(table)->Lookup(name); }

// path + the resource's name + suffix (the original builds it through std::operator+(string&&, const char *), 0x37ad40).
std::string ResourcePath(const char *path, cMaterialParserFields *state, const char *suffix, size_t suffixLength)
{
    std::string text(path);
    text += reinterpret_cast<const char *>(Dispatch8(&state->mResourceName));
    text.append(suffix, suffixLength);
    return text;
}
} // namespace

void cShaderProgramCommand::Execute(cp::cIArguments *args)
{
    ParserFace(State()).PushOpenBlock(this);
    cp::ThrowUnless(args->PositionalCount() == 0, Text(kWrongNumberOfArguments));
    State()->mProgramTarget = 1;
    if (const char *const *target = args->TakeSwitchArgumentsExactly(Text(0xe7b524), 1)) // "target"
        State()->mProgramTarget = LookUp(0x10f6b34, target[0]);
    if (args->TakeSwitch(Text(0xe7b52c))) State()->mProgramTarget = 0; // "preprocess"
    int32_t block = State()->mBlock;
    if (block == 0)
        cp::ThrowUnless(State()->mProgramTarget == 3, Text(0xe7b538)); // "Expecting fragments target"
    else if (block != 4)
        return;
    cMaterialParserFields *state = State();
    switch (state->mProgramTarget) {
    case 0:
        cp::ThrowUnless(state->mInPass != 0, Text(0xe7b554));                   // "Outside a pass block"
        cp::ThrowUnless(State()->mHasPreprocessProgram == 0, Text(0xe7b56c));   // "Multiple preprocess vertex programs"
        State()->mCompileMethod = 1;
        break;
    case 1:
        cp::ThrowUnless(state->mInPass != 0, Text(0xe7b554));
        cp::ThrowUnless(State()->mHasVertexProgram == 0, Text(0xe7b590));       // "Multiple vertex programs"
        cp::ThrowUnless(State()->mFixedFunctionPass == 0, Text(0xe7b5ac));      // "In a fixed function pass section"
        State()->mCompileMethod = 1;
        break;
    case 2:
        cp::ThrowUnless(state->mInPass != 0, Text(0xe7b554));
        cp::ThrowUnless(State()->mHasPixelProgram == 0, Text(0xe7b5d0));        // "Multiple pixel programs"
        cp::ThrowUnless(State()->mFixedFunctionPass == 0, Text(0xe7b5ac));
        cp::ThrowUnless(State()->mTextureStageCascade == -1, Text(0xe7b5e8));
        State()->mPixelProgram.Assign(nullptr);
        State()->mCompileMethod = 1;
        break;
    case 3:
        state->mCompileMethod = 0;
        break;
    default:
        break;
    }
    if (const char *const *method = args->TakeSwitchArgumentsExactly(Text(0xe7b624), 1)) // "method"
        State()->mCompileMethod = LookUp(0x10f6b40, method[0]);
    state = State();
    cp::ThrowUnless(!(state->mProgramTarget == 3 && state->mCompileMethod != 0), Text(0xe7b62c));
    State()->mCompileVersion.clear();
    if (State()->mCompileMethod == 2) {
        // A compile from source needs the profile version: "vs_" / "ps_" and the version given.
        const char *const *version = args->TakeSwitchArgumentsExactly(Text(0xdbc1a4), 1); // "version"
        ThrowIfNullMaterial(version, Text(0xe7b658));                                  // "Missing version for compile"
        State()->mCompileVersion.assign(State()->mProgramTarget == 2 ? Text(0xe7b674) : Text(0xe7b678));
        const char *text = version[0];
        State()->mCompileVersion.append(text, std::strlen(text));
    }
    State()->mOptimize = !args->TakeSwitch(Text(0xe7b67c)); // "noOpt"
    State()->mDumpSourcePath.clear();
    if (const char *const *path = args->TakeSwitchArgumentsExactly(Text(0xe7b684), 1)) // "dumpSource"
        State()->mDumpSourcePath = ResourcePath(path[0], State(), Text(0xe7b690), 7); // ".source"
    State()->mDisassemblyPath.clear();
    if (const char *const *path = args->TakeSwitchArgumentsExactly(Text(0xe7b698), 1)) // "disassemble"
        State()->mDisassemblyPath = ResourcePath(path[0], State(), Text(0xe7b6a4), 10); // ".shaderAsm"
    args->CheckSwitchesUsed();
    if (State()->mCompileMethod == 3) VirtualCall<void(__thiscall *)(void *), 7>(ShaderCompiler());
    ResetSource();
}

const char *cShaderProgramCommand::GetName() const { return Text(0xe7b514); }

namespace {
// A cRZString on the stack (the compiler's error text, the disassembly), built and destroyed as the original's.
class LocalString {
public:
    LocalString() { String()->Construct(); }
    ~LocalString() { String()->Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    cIGZString *Interface() { return String()->AsInterface(); }
    cRZString *String() { return reinterpret_cast<cRZString *>(mRaw); }
    const char *Chars() { return String()->ToChar(); }

private:
    alignas(4) uint8_t mRaw[sizeof(cRZString)];
};

// The game's output file stream (cRZOFStream, 0x18 bytes, rz/RZFileStreams.h): opened on a path by its constructor
// (0x1b4e80, the four other arguments 0) and closed by its destructor (0x1b52d8).
class OutputFile {
public:
    explicit OutputFile(const char *path) { Stream()->Construct(path, 0, 0, 0, 0); }
    ~OutputFile() { Stream()->Destruct(); }
    OutputFile(const OutputFile &) = delete;
    OutputFile &operator=(const OutputFile &) = delete;
    // Slot 14: writes a C string.
    bool Write(const char *text) { return VirtualCall<bool(__thiscall *)(void *, const char *), 14>(mRaw, text); }
    // 0x1b6109 (cRZOFStream::SetGZStr): writes a cIGZString; the result is tested as a bool (al).
    bool Write(cIGZString *text) { return static_cast<uint8_t>(Stream()->SetGZStr(text)) != 0; }

private:
    cRZOFStream *Stream() { return reinterpret_cast<cRZOFStream *>(mRaw); }
    alignas(4) uint8_t mRaw[0x18];
};

void DumpSource(const char *path, const char *text)
{
    OutputFile file(path);
    cp::ThrowUnless(file.Write(text), Text(0xe7b710)); // "Failed to dump shader program source"
}
} // namespace

void cShaderProgramCommand::EndBlock()
{
    int32_t block = State()->mBlock;
    if (block != 0 && block != 4) return;
    cMaterialParserFields *state = State();
    // The compile flags: 4 for a preprocess program, 2 from +0x272, 1 to optimize.
    const uint32_t flags = (state->mProgramTarget == 0 ? 4u : 0u) | (state->m272 != 0 ? 2u : 0u) | (state->mOptimize != 0 ? 1u : 0u);
    int32_t result = 1; // 0: compiled; 1: errors; 2: failed otherwise
    LocalString errors;
    UnknownRef program, constants;
    switch (state->mCompileMethod) {
    case 0: {
        // Preprocess only (slot 6: text, size, errors, flags).
        cp::ThrowUnless(!state->mSource.empty(), Text(0xe7b6b0)); // "No program text"
        void *compiler = ShaderCompiler();
        std::vector<char> &source = State()->mSource;
        result = VirtualCall<int32_t(__thiscall *)(void *, const char *, uint32_t, cIGZString *, uint32_t), 6>(
            compiler, source.data(), static_cast<uint32_t>(source.size()), errors.Interface(), flags);
        break;
    }
    case 1: {
        // Assemble (slot 3: device, text, size, program, errors, flags).
        cp::ThrowUnless(!state->mSource.empty(), Text(0xe7b6b0));
        void *compiler = ShaderCompiler();
        const char *text = State()->mSource.data();
        void **programOut = program.AsPPVoid();
        uint32_t size = static_cast<uint32_t>(State()->mSource.size());
        uint32_t device = CurrentDevice();
        result = VirtualCall<int32_t(__thiscall *)(void *, uint32_t, const char *, uint32_t, void **, cIGZString *, uint32_t), 3>(
            compiler, device, text, size, programOut, errors.Interface(), flags);
        break;
    }
    case 2: {
        // Compile (slot 4: device, entry point, profile, text, size, program, constants, errors, flags).
        cp::ThrowUnless(!state->mSource.empty(), Text(0xe7b6b0));
        const char *entry = State()->mProgramTarget == 2 ? Text(0xe7b6c0) : Text(0xe7b6cc); // "PixelMain" / "VertexMain"
        void *compiler = ShaderCompiler();
        cMaterialParserFields *now = State();
        const char *text = now->mSource.data();
        uint32_t size = static_cast<uint32_t>(now->mSource.size());
        const char *profile = now->mCompileVersion.c_str();
        void **constantsOut = constants.AsPPVoid();
        void **programOut = program.AsPPVoid();
        uint32_t device = CurrentDevice();
        result = VirtualCall<int32_t(__thiscall *)(void *, uint32_t, const char *, const char *, const char *, uint32_t, void **,
                                                   void **, cIGZString *, uint32_t),
                             4>(compiler, device, entry, profile, text, size, programOut, constantsOut, errors.Interface(), flags);
        break;
    }
    case 3: {
        // Link the fragments (slot 9: device, program, constants, errors, flags): no source allowed.
        cp::ThrowUnless(state->mSource.empty(), Text(0xe7b6d8)); // "Cannot mix fragment linking with shader source"
        void *compiler = ShaderCompiler();
        void **constantsOut = constants.AsPPVoid();
        void **programOut = program.AsPPVoid();
        uint32_t device = CurrentDevice();
        result = VirtualCall<int32_t(__thiscall *)(void *, uint32_t, void **, void **, cIGZString *, uint32_t), 9>(
            compiler, device, programOut, constantsOut, errors.Interface(), flags);
        break;
    }
    default:
        break;
    }
    // The source is terminated, then dumped when asked for (to the .error file when there were errors), and a compiled
    // program disassembled when asked for.
    State()->mSource.push_back('\0');
    state = State();
    const char *text = state->mSource.data();
    if (!state->mDumpSourcePath.empty() && !state->mSource.empty()) {
        if (result == 1) {
            std::string path(state->mDumpSourcePath);
            path.append(Text(0xe7b708), 6); // ".error"
            DumpSource(path.c_str(), text);
        } else {
            DumpSource(state->mDumpSourcePath.c_str(), text);
        }
    }
    if (result == 0 && !State()->mDisassemblyPath.empty()) {
        LocalString listing;
        void *compiler = ShaderCompiler();
        bool ok = VirtualCall<bool(__thiscall *)(void *, cIGZString *, cIGZUnknown *), 11>(compiler, listing.Interface(),
                                                                                         program.Get());
        cp::ThrowUnless(ok, Text(0xe7b738)); // "Failed to disassemble shader program source"
        OutputFile file(State()->mDisassemblyPath.c_str());
        cp::ThrowUnless(file.Write(listing.Interface()), Text(0xe7b764)); // "Failed to write out ..."
    }
    State()->mCompileVersion.clear();
    State()->mDumpSourcePath.clear();
    State()->mDisassemblyPath.clear();
    if (result == 2 || result == 1) {
        ResetSource();
        State()->mBlock = 6;
        return;
    }
    ResetSource();
    state = State();
    // The program goes to the shader's slot for its target, for this pass.
    using ShaderProgramFn = void(__thiscall *)(void *, int32_t, cIGZUnknown *);
    switch (state->mProgramTarget) {
    case 0:
        VirtualCall<ShaderProgramFn, 6>(state->mShader.Get(), state->mPassIndex, program.Get());
        State()->mHasPreprocessProgram = 1;
        break;
    case 1:
        VirtualCall<ShaderProgramFn, 14>(state->mShader.Get(), state->mPassIndex, program.Get());
        State()->mHasVertexProgram = 1;
        break;
    case 2:
        VirtualCall<ShaderProgramFn, 15>(state->mShader.Get(), state->mPassIndex, program.Get());
        State()->mPixelProgram = constants;
        State()->mHasPixelProgram = 1;
        break;
    default:
        break;
    }
    if (!constants) return;
    // The program's constants that the engine knows by name (the table at 0xe78f18, matched by name and shape) are bound
    // to its values through the pass's constant binder; an "export_" constant the table does not know is an error.
    UnknownRef binder;
    const uint32_t count = VirtualCall<uint32_t(__thiscall *)(void *), 3>(constants.Get());
    const AutoBoundConstant *table = GameAddress<const AutoBoundConstant>(0xe78f18);
    for (uint32_t i = 0; i < count; ++i) {
        ProgramConstantDesc desc;
        VirtualCall<void(__thiscall *)(void *, ProgramConstantDesc *, uint32_t), 5>(constants.Get(), &desc, i);
        uint32_t index = 0;
        for (; index < 9; ++index) {
            const AutoBoundConstant &entry = table[index];
            if (std::strcmp(desc.name, entry.name) == 0 && desc.registerSet == entry.registerSet &&
                desc.parameterClass == entry.parameterClass && desc.type == entry.type && desc.rows == entry.rows &&
                desc.columns == entry.columns && desc.elements == entry.elements && desc.bytes == entry.bytes)
                break;
        }
        if (index < 9) {
            if (!binder) {
                // The pass's binder: the shader's own (slot 22 for a preprocess program, 23 otherwise), or a new one
                // (class 0xaa60fff1) given to the shader (slot 8 / 9).
                state = State();
                using FindBinderFn = bool(__thiscall *)(void *, int32_t, uint32_t, void **);
                using SetBinderFn = void(__thiscall *)(void *, int32_t, cIGZUnknown *);
                if (state->mProgramTarget == 0) {
                    if (!VirtualCall<FindBinderFn, 22>(state->mShader.Get(), state->mPassIndex, 0x8a60ffeb, binder.AsPPVoid())) {
                        GetClassObject(0xaa60fff1, 0x8a60ffeb, binder);
                        VirtualCall<SetBinderFn, 8>(State()->mShader.Get(), State()->mPassIndex, binder.Get());
                    }
                } else {
                    if (!VirtualCall<FindBinderFn, 23>(state->mShader.Get(), state->mPassIndex, 0x8a60ffeb, binder.AsPPVoid())) {
                        GetClassObject(0xaa60fff1, 0x8a60ffeb, binder);
                        VirtualCall<SetBinderFn, 9>(State()->mShader.Get(), State()->mPassIndex, binder.Get());
                    }
                }
            }
            ConstantBinding binding{};
            binding.source = static_cast<uint8_t>(table[index].source);
            binding.stage = State()->mProgramTarget == 2 ? 3 : 0;
            binding.kind = 4;
            binding.registerIndex = desc.registerIndex;
            binding.registerCount = desc.registerCount;
            binding.minusOne = -1;
            VirtualCall<void(__thiscall *)(void *, ConstantBinding *), 14>(binder.Get(), &binding);
        }
        if (std::strncmp(desc.name, Text(0xe7b794), 7) == 0) // "export_"
            cp::ThrowUnless(index != 9, Text(0xe7b7a0), desc.name);
    }
}

// ---- the throw helper ------------------------------------------------------------------------------------------------

void __cdecl ThrowIfNullMaterial(const void *p, const char *fmt, ...)
{
    if (p) return;
    constexpr uint32_t kThrowBuffer = 0x10f73c8; // its own 2 KB buffer
    constexpr int kBufferSize = 0x800;
    char *message = GameAddress<char>(kThrowBuffer);
    va_list ap;
    va_start(ap, fmt);
    // ORIGINAL-BUG: as nGZCommandParser::ThrowUnless (an unterminated message at 2048 characters, and the message used
    // as the error's format string, its arguments the words the original's frame holds: the same frame as ThrowUnless's).
    _vsnprintf(message, kBufferSize, fmt, ap);
    va_end(ap);
    cp::ThrowFormatted(message, reinterpret_cast<uint32_t>(message), static_cast<uint32_t>(kBufferSize),
                       reinterpret_cast<uint32_t>(fmt), reinterpret_cast<uint32_t>(ap),
                       reinterpret_cast<uint32_t>(GameAddress(cp::cError::kVTable)), 0u, 0u, 0u, 0u, 0u, 0xfu, 0u);
}

// ---- texture ---------------------------------------------------------------------------------------------------------

namespace {
struct Vec2 {
    float x, y;
};
struct Vec3 {
    float x, y, z;
};

// The texture manager (0xae8750, sg/cTextureManager.h): asked for once and cached in a global.
void *Manager() { return nGZSceneGraph::TextureManager(); }

// A counted texture id of the texture manager (4 bytes; nGZSceneGraph::cTextureHandle): made from an id (0xae8260) or a name (0xae8280),
// released by 0xae82b0; Get (0xae8730) gives the texture (null for the invalid id).
class TextureHandle {
public:
    struct FromId {};
    TextureHandle(FromId, uint32_t id) { Handle()->InitFromId(id); }
    explicit TextureHandle(const char *name) { Handle()->InitFromName(name); }
    ~TextureHandle() { Handle()->Destroy(); }
    TextureHandle(const TextureHandle &) = delete;
    TextureHandle &operator=(const TextureHandle &) = delete;
    bool IsInvalid() const { return mId == 0xffffffffu; } // cInvalidDwordTag::IsInvalid (0x18c080)
    void *Get() { return Handle()->Get(); }

private:
    nGZSceneGraph::cTextureHandle *Handle() { return nGZSceneGraph::cTextureHandle::At(&mId); }
    uint32_t mId;
};

// The texture filter states a texture line gives each of its samplers (cTextureFilterState / cTextureMipFilterState,
// 0x14 bytes, built on the stack and handed to the manager's slot 27 and the shader's slot 11). The original sets only
// the named fields; the pad bytes are whatever its stack held.
struct FilterState {
    uint32_t vtable;
    uint8_t kind; // 0x1e
    uint8_t pad1;
    uint16_t size; // 0x14
    uint32_t sampler;
    uint16_t value16; // +0xc
    uint16_t pad2;
    uint32_t value; // +0x10
};
struct MipFilterState {
    uint32_t vtable;
    uint8_t kind; // 0x1f
    uint8_t pad1;
    uint16_t size; // 0x14
    uint32_t sampler;
    uint8_t value8; // +0xc
    uint8_t pad2[3];
    uint32_t value; // +0x10
};
static_assert(sizeof(FilterState) == 0x14 && sizeof(MipFilterState) == 0x14, "filter state layout");

// The generators' sizes and counts: the float truncated to 64 bits (the original's _ftol3), the low word kept (1e10
// gives 0x540be400, not the 32-bit conversion's 0x80000000). The falloff's power is the 32-bit conversion.
int32_t Truncate(float f) { return static_cast<int32_t>(static_cast<int64_t>(f)); }
} // namespace

void cTextureCommand::Execute(cp::cIArguments *args)
{
    if (State()->mBlock != 4) return;
    const char *name = args->PositionalExactly(1)[0];
    // A texture of a material in a resource group (other than 0x1c0532fa) is named within the group (0xae7340, not
    // ours: the resolved name written to the string).
    LocalString resolved;
    uint32_t group = State()->m1d4;
    if (group != 0 && group != 0x1c0532fa) {
        nGZSceneGraph::ResolveResourceName(resolved.Interface(), name, group, nullptr);
        name = resolved.Chars();
    }
    const char *const *noise = args->TakeSwitchArgumentsExactly(Text(0xe7a584), 3);       // "noise"
    const char *const *rampH = args->TakeSwitchArgumentsExactly(Text(0xe7a58c), 3);       // "rampH"
    const char *const *rampV = args->TakeSwitchArgumentsExactly(Text(0xe7a594), 3);       // "rampV"
    const char *const *mipTest = args->TakeSwitchArgumentsExactly(Text(0xe7a59c), 1);     // "mipTest"
    const char *const *normalCube = args->TakeSwitchArgumentsExactly(Text(0xe7a5a4), 1);  // "normalizationCubeMap"
    const char *const *falloffCube = args->TakeSwitchArgumentsExactly(Text(0xe7a5bc), 4); // "alphaFalloffCubeMap"
    const char *const *colorBuffer = args->TakeSwitchArgumentsExactly(Text(0xe7a5d0), 1); // "colorBufferIndex"
    int32_t colorBufferIndex = 0;
    if (colorBuffer) colorBufferIndex = ParserFace(State()).EvalInt(colorBuffer[0]);
    args->CheckSwitchesUsed();
    cp::ThrowUnless(args->SwitchCount() <= 1, Text(0xe7a5e4));   // "Conflicting switches"
    cp::ThrowUnless(State()->mSamplerCount != 0, Text(0xe7a5fc)); // "Bad sampler reg"
    // With every viewer render type bit set (0x7800), a texture whose first level is a render target is named after
    // its size ("texDensity_%d_%d").
    LocalString sized;
    int32_t width = 0, height = 0;
    if ((State()->mFlags & 0x7800) == 0x7800) {
        uint32_t id = VirtualCall<uint32_t(__thiscall *)(void *, const char *), 17>(Manager(), name);
        TextureHandle handle(TextureHandle::FromId{}, id);
        void *texture, *level;
        if (!handle.IsInvalid() && (texture = handle.Get()) != nullptr &&
            VirtualCall<int32_t(__thiscall *)(void *), 5>(texture) == 4 &&
            (level = VirtualCall<void *(__thiscall *)(void *, uint32_t), 8>(texture, 0)) != nullptr &&
            VirtualCall<bool(__thiscall *)(void *), 6>(level)) {
            width = VirtualCall<int32_t(__thiscall *)(void *, uint32_t), 17>(level, 0);
            height = VirtualCall<int32_t(__thiscall *)(void *, uint32_t), 18>(level, 0);
            sized.String()->Sprintf(Text(0xe7a60c), width, height);
            name = sized.Chars();
        }
    }
    // A texture the manager does not have yet is made when the line asks for a generated one (the generators are
    // __cdecl and not ours; each writes the image to the counted pointer, which lets go of the one before), and added.
    if (!VirtualCall<bool(__thiscall *)(void *, const char *), 25>(Manager(), name)) {
        UnknownRef image;
        if (width > 0 && height > 0)
            OriginalFunction<void(__cdecl *)(int32_t, int32_t, float, float, void **), 0xaec1a0>()(width, height, 240.0f, 60.0f,
                                                                                                image.AsPPVoid());
        ParserFace face(State());
        if (noise) {
            Vec2 cells, scale;
            face.EvalVector2(&cells.x, noise[0], 2);
            float octaves = face.EvalFloat(noise[1]);
            face.EvalVector2(&scale.x, noise[2], 2);
            void **out = image.AsPPVoid();
            OriginalFunction<void(__cdecl *)(int32_t, int32_t, int32_t, float, float, void **), 0xaec6b0>()(
                Truncate(cells.x), Truncate(cells.y), Truncate(octaves), scale.x, scale.y, out);
        }
        if (rampH) {
            Vec2 size;
            Vec3 from, to;
            face.EvalVector2(&size.x, rampH[0], 2);
            face.EvalVector3(&from.x, rampH[1], 3);
            face.EvalVector3(&to.x, rampH[2], 3);
            void **out = image.AsPPVoid();
            OriginalFunction<void(__cdecl *)(int32_t, int32_t, const Vec3 *, const Vec3 *, void **), 0xaec8e0>()(
                Truncate(size.x), Truncate(size.y), &from, &to, out);
        }
        if (rampV) {
            Vec2 size;
            Vec3 from, to;
            face.EvalVector2(&size.x, rampV[0], 2);
            face.EvalVector3(&from.x, rampV[1], 3);
            face.EvalVector3(&to.x, rampV[2], 3);
            void **out = image.AsPPVoid();
            OriginalFunction<void(__cdecl *)(int32_t, int32_t, const Vec3 *, const Vec3 *, void **), 0xaecba0>()(
                Truncate(size.x), Truncate(size.y), &from, &to, out);
        }
        if (mipTest) {
            Vec2 size;
            face.EvalVector2(&size.x, mipTest[0], 2);
            void **out = image.AsPPVoid();
            OriginalFunction<void(__cdecl *)(int32_t, int32_t, void **), 0xaec310>()(Truncate(size.x), Truncate(size.y), out);
        }
        if (normalCube) {
            uint32_t size = face.EvalUint(normalCube[0]);
            void **out = image.AsPPVoid();
            OriginalFunction<void(__cdecl *)(uint32_t, void **), 0xaec4a0>()(size, out);
        }
        if (falloffCube) {
            uint32_t size = face.EvalUint(falloffCube[0]);
            Vec3 color;
            Vec2 range;
            face.EvalVector3(&color.x, falloffCube[1], 3);
            face.EvalVector2(&range.x, falloffCube[2], 2);
            float power = face.EvalFloat(falloffCube[3]);
            void **out = image.AsPPVoid();
            OriginalFunction<void(__cdecl *)(uint32_t, const Vec2 *, const Vec3 *, int32_t, void **), 0xaebd70>()(
                size, &range, &color, static_cast<int32_t>(power), out);
        }
        if (image)
            VirtualCall<void(__thiscall *)(void *, cIGZUnknown *, const char *, uint32_t), 20>(Manager(), image.Get(), name, 1);
    }
    // The texture with its default filter and mip filter states, registered with the manager (slot 27); each sampler
    // of the block gets the states and the texture (the shader's slots 11 and 13), once (slot 25: already given one?).
    TextureHandle bound(name);
    FilterState filter{};
    filter.vtable = reinterpret_cast<uint32_t>(GameAddress(0xd9c63c)); // cTextureFilterState
    filter.kind = 0x1e;
    filter.size = 0x14;
    filter.sampler = 0;
    filter.value16 = 0;
    filter.value = 1;
    MipFilterState mip{};
    mip.vtable = reinterpret_cast<uint32_t>(GameAddress(0xd9c648)); // cTextureMipFilterState
    mip.kind = 0x1f;
    mip.size = 0x14;
    mip.sampler = 0;
    mip.value8 = 0;
    mip.value = 0;
    void *manager = Manager();
    VirtualCall<void(__thiscall *)(void *, FilterState *, MipFilterState *, void *), 27>(manager, &filter, &mip, bound.Get());
    for (uint32_t i = 0; i < State()->mSamplerCount; ++i) {
        cMaterialParserFields *state = State();
        uint32_t sampler = state->mSamplers[i];
        int32_t taken = VirtualCall<int32_t(__thiscall *)(void *, int32_t, uint32_t), 25>(state->mShader.Get(), state->mPassIndex, sampler);
        cp::ThrowUnless(taken == 0, Text(0xe7a620)); // "A texture was already set for this sampler"
        filter.sampler = sampler;
        VirtualCall<void(__thiscall *)(void *, int32_t, void *), 11>(State()->mShader.Get(), State()->mPassIndex, &filter);
        mip.sampler = sampler;
        VirtualCall<void(__thiscall *)(void *, int32_t, void *), 11>(State()->mShader.Get(), State()->mPassIndex, &mip);
        VirtualCall<void(__thiscall *)(void *, int32_t, uint32_t, const char *, int32_t), 13>(
            State()->mShader.Get(), State()->mPassIndex, sampler, name, colorBufferIndex);
    }
}

const char *cTextureCommand::GetName() const { return Text(0xdb2da0); }

} // namespace nGZSceneGraph
