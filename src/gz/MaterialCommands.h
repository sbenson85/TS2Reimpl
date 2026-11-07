#pragma once
// The material script's commands: the commands nGZSceneGraph::cMaterialParser registers (its slot 0 at 0xad8ba0 builds
// the top-level ones) and the ones those blocks add when they are given their context (each block's slot 3). They read
// and write the parser's material state (the "state" a cCommandT keeps is the parser itself) and build material
// definitions, shaders and shader programs through objects the framework hands out. RVAs are 1.18.0.187.
//
// Hook shape, as the command framework they derive from (gz/CommandParser.h): the game's code creates the top-level
// commands (0xad8ba0, not ours) with the game's vtables, so every slot is an RVA hook. The blocks' slot 3 functions are
// ours and build their children as the original does (operator new, the base constructor, the child's game vtable).
// The standalone shape needs the bases (cCommand, cMetaCommand, cBlockCommand) in that shape first.
#include "gz/CommandParser.h"
#include "gz/cIGZUnknown.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include "game/vtable.h" // VirtualCall (the parser face)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class cIGZString;

namespace nGZCommandParser {
class cIArguments;
}

namespace nGZSceneGraph {

// A counted interface pointer as the material state keeps them (the game's cRZAutoRefCount<cIGZUnknown>).
using UnknownRef = cRZAutoRefCount<cIGZUnknown>;

// 0xace700 (__cdecl varargs): nGZCommandParser::ThrowIfNull's twin in the material commands' code, with its own 2 KB
// message buffer (0x10f73c8): throws the formatted cError when `p` is null.
void __cdecl ThrowIfNullMaterial(const void *p, const char *fmt, ...);

// One element of a pass's output vertex format (12 bytes): the pass's end hands each to the vertex format builder.
struct OutputVertexElement {
    uint32_t a;
    uint32_t b;
    uint8_t c;
};
static_assert(sizeof(OutputVertexElement) == 12, "OutputVertexElement layout");

// What the commands use of the material parser (nGZSceneGraph::cMaterialParser, the object a command's mState points
// at). cMaterialParser derives from cBaseParser (0x1ac bytes; its vbptr at +8 leads to the cIBaseParser face the
// commands call); the fields after it are the material state. Only the fields the commands use are named. Values of
// mBlock: 0 top level, 3 a default state block, 4 a shader block, 5 a shader that validated, 6 one that did not.
struct cMaterialParserFields {
    uint8_t mBaseParser[0x1ac];                 // +0 the cBaseParser part
    std::vector<UnknownRef> mDefaultStates;     // +0x1ac shaders by viewer render type (bits 11..14 of mFlags)
    uint32_t m1b8;                              // +0x1b8
    uint32_t mResourceName;                     // +0x1bc a name cell: Dispatch8 0xaeb800 gives its text
    uint32_t mRenderShaderContext;              // +0x1c0 given to the built shader's slot 8 (cShaderCommand::EndBlock)
    uint8_t mRenderShaderContextData[8];        // +0x1c4 its address is given to the built shader's slot 8
    uint32_t mFlags;                            // +0x1cc bits 11..14: the viewer render type
    int32_t mBlock;                             // +0x1d0 which block is open (see above)
    uint32_t m1d4;                              // +0x1d4
    UnknownRef mMaterialDefinition;             // +0x1d8 the definition a materialDefinition block builds
    uint8_t m1dc[0x20];                         // +0x1dc
    UnknownRef mShader;                         // +0x1fc the shader a shader or defaultState block builds
    UnknownRef mValidShader;                    // +0x200 the last shader that validated
    UnknownRef mPixelProgram;                   // +0x204 the compiled pixel program (its samplers)
    std::string mCompileVersion;                // +0x208 the compiler profile ("vs_" / "ps_" and the version)
    std::vector<char> mSource;                  // +0x220 the shader program's source text
    std::string mDumpSourcePath;                // +0x22c
    std::string mDisassemblyPath;               // +0x244
    std::vector<OutputVertexElement> mOutputVertexFormat; // +0x25c the pass's outputVertexFormat lines
    uint8_t m268;                               // +0x268 given to the built shader's slot 8 (its last argument)
    uint8_t mInShader;                          // +0x269
    uint8_t mInPass;                            // +0x26a
    uint8_t m26b;                               // +0x26b
    uint8_t mInSampler;                         // +0x26c
    uint8_t mFixedFunctionPass;                 // +0x26d
    uint8_t mHasPreprocessProgram;              // +0x26e
    uint8_t mHasVertexProgram;                  // +0x26f
    uint8_t mHasPixelProgram;                   // +0x270
    uint8_t mOptimize;                          // +0x271
    uint8_t m272;                               // +0x272
    uint8_t m273;                               // +0x273
    int32_t mProgramTarget;                     // +0x274 0 preprocess, 1 vertex, 2 pixel, 3 fragments
    int32_t mCompileMethod;                     // +0x278
    int32_t mPassIndex;                         // +0x27c
    int32_t mTextureStageCascade;               // +0x280 -1: none
    uint32_t mSamplerCount;                     // +0x284
    uint32_t mSamplers[16];                     // +0x288
};
static_assert(offsetof(cMaterialParserFields, mDefaultStates) == 0x1ac && offsetof(cMaterialParserFields, mFlags) == 0x1cc &&
                  offsetof(cMaterialParserFields, mMaterialDefinition) == 0x1d8 && offsetof(cMaterialParserFields, mShader) == 0x1fc &&
                  offsetof(cMaterialParserFields, mCompileVersion) == 0x208 && offsetof(cMaterialParserFields, mSource) == 0x220 &&
                  offsetof(cMaterialParserFields, mDumpSourcePath) == 0x22c && offsetof(cMaterialParserFields, mDisassemblyPath) == 0x244 &&
                  offsetof(cMaterialParserFields, m268) == 0x268 && offsetof(cMaterialParserFields, mProgramTarget) == 0x274 &&
                  offsetof(cMaterialParserFields, mSamplerCount) == 0x284 && sizeof(cMaterialParserFields) == 0x2c8,
              "cMaterialParserFields layout");

// ---- the parser's cIBaseParser face, as the commands reach it --------------------------------------------------------
//
// The face is the parser's virtual base: the vbtable at state+8 gives its offset (third word). The original finds it
// again for every call. Slots are those of cBaseParser's face (gz/cBaseParser.h). The material and the lighting
// parsers (both cBaseParser at +0) are reached the same way.
class ParserFace {
public:
    explicit ParserFace(const void *state)
    {
        const char *vbptr = static_cast<const char *>(state) + 8;
        mFace = const_cast<char *>(vbptr) + (*reinterpret_cast<const int32_t *const *>(vbptr))[2];
    }
    // The face itself (as a command's SetContext stored it in mParser).
    struct Direct {};
    ParserFace(Direct, void *face) : mFace(face) {}
    void PushOpenBlock(void *block) { VirtualCall<void(__thiscall *)(void *, void *), 16>(mFace, block); }
    void PushMetaBlock(void *block) { VirtualCall<void(__thiscall *)(void *, void *), 17>(mFace, block); }
    void PopMetaBlock() { VirtualCall<void(__thiscall *)(void *), 18>(mFace); }
    void PushNamespace(const char *name) { VirtualCall<void(__thiscall *)(void *, const char *), 20>(mFace, name); }
    void PopNamespace() { VirtualCall<void(__thiscall *)(void *), 21>(mFace); }
    void ExpandVariables(cIGZString *text, bool parenthesize)
    {
        VirtualCall<void(__thiscall *)(void *, cIGZString *, bool), 29>(mFace, text, parenthesize);
    }
    // Slots 32 and 33 are the same function (EvalInt); the commands call one or the other.
    int32_t EvalInt(const char *text) { return VirtualCall<int32_t(__thiscall *)(void *, const char *), 32>(mFace, text); }
    uint32_t EvalUint(const char *text) { return VirtualCall<uint32_t(__thiscall *)(void *, const char *), 33>(mFace, text); }
    bool EvalBool(const char *text) { return VirtualCall<bool(__thiscall *)(void *, const char *), 30>(mFace, text); }
    // Slot 31 answers in st0; slots 34 / 35 write two / three floats (the last argument: how many the text must give),
    // slot 39 an RGB colour; each returns `out`.
    float EvalFloat(const char *text) { return VirtualCall<float(__thiscall *)(void *, const char *), 31>(mFace, text); }
    float *EvalVector2(float *out, const char *text, uint32_t least)
    {
        return VirtualCall<float *(__thiscall *)(void *, float *, const char *, uint32_t), 34>(mFace, out, text, least);
    }
    float *EvalVector3(float *out, const char *text, uint32_t least)
    {
        return VirtualCall<float *(__thiscall *)(void *, float *, const char *, uint32_t), 35>(mFace, out, text, least);
    }
    float *EvalColorRGB(float *out, const char *text)
    {
        return VirtualCall<float *(__thiscall *)(void *, float *, const char *), 39>(mFace, out, text);
    }

private:
    void *mFace;
};

// What a block's slot 3 builds: a command of one of the three bases with the given game vtable.
enum class ChildBase : uint8_t { Command, MetaCommand, Block };
struct BlockChild {
    uint32_t vtable;
    ChildBase base;
};
// The start of a script block's slot 3: the base's slot 3 (the parser, the case flag), the state (the interface less
// 4); then each child made (operator new, the base's constructor, the child's game vtable) and handed to the block's
// slot 10 (AddCommand), in order.
void SetContextWithChildren(nGZCommandParser::cBlockCommandT *block, void *parser, void *stateInterface,
                            const BlockChild *children, size_t count);

// The block commands of the material script: cBlockCommandT<cMaterialParser> (0x44 bytes, the state at +0x40).
class cMaterialBlockCommand : public nGZCommandParser::cBlockCommandT {
public:
    cMaterialParserFields *State() const { return static_cast<cMaterialParserFields *>(mState); }

    using ChildBase = nGZSceneGraph::ChildBase;
    using Child = BlockChild;
    void SetContextWithChildren(void *parser, void *stateInterface, const Child *children, size_t count)
    {
        nGZSceneGraph::SetContextWithChildren(this, parser, stateInterface, children, count);
    }
};

// The meta commands of the material script: cMetaCommandT<cMaterialParser> (12 bytes, the state at +8).
class cMaterialMetaCommand : public nGZCommandParser::cMetaCommandT {
public:
    cMaterialParserFields *State() const { return static_cast<cMaterialParserFields *>(mState); }
};

// The plain commands of the material script: cCommandT<cMaterialParser> (16 bytes, the state at +0xc).
class cMaterialCommand : public nGZCommandParser::cCommandT {
public:
    cMaterialParserFields *State() const { return static_cast<cMaterialParserFields *>(mState); }
};

// ---- texture (vtable 0xe7a55c) ---------------------------------------------------------------------------------------
// A line of a sampler block naming the texture its samplers use: the name resolved against the material's resource
// group, a generated texture made and added to the texture manager when the line asks for one (noise, ramps, mip test,
// cube maps; a render-target density texture when every viewer render type bit is set), and the texture with default
// filter states given to each sampler of the block. Its other slots are cCommandT's.
class cTextureCommand : public cMaterialCommand {
public:
    static constexpr uint32_t kVTable = 0xe7a55c;
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0xad6f30
    const char *GetName() const;                       // slot 5, 0xad99a0: "texture"
};

// ---- materialDefinition (vtable 0xe7a330) --------------------------------------------------------------------------
// A top-level block naming a material definition: gets one from the framework (class 0x49596978) and fills it with the
// block's setDefinition / addParam lines; its end registers it with the material manager.
class cMaterialDefinitionCommand : public cMaterialBlockCommand {
public:
    static constexpr uint32_t kVTable = 0xe7a330;
    void SetContext(void *parser, void *stateInterface); // slot 3, 0xadb160 (ret 8)
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xad45d0 (ret 4)
    const char *GetName() const;                         // slot 5, 0xad9720: "materialDefinition"
    void EndBlock();                                     // slot 9, 0xad9b60
};

// ---- sampler (vtable 0xe7bbbc) -------------------------------------------------------------------------------------
// Inside a shader's pass with a pixel program: the samplers a texture block applies to (by name, through the pixel
// program's constant table, or a number).
class cSamplerCommand : public cMaterialBlockCommand {
public:
    static constexpr uint32_t kVTable = 0xe7bbbc;
    void SetContext(void *parser, void *stateInterface); // slot 3, 0xadb9b0
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xad57c0
    const char *GetName() const;                         // slot 5, 0xad98a0: "sampler"
    void EndBlock();                                     // slot 9, 0xad9de0
};

// The 12 bytes slot 7 of the pixel program gives for a sampler name (a hidden return pointer).
struct SamplerList {
    uint32_t found;           // 0: no such name
    uint32_t count;
    const uint32_t *indices;
};

// ---- shaderSource (vtable 0xe7b2f0) --------------------------------------------------------------------------------
// The text between shaderSource and endShaderSource is collected, line by line, into the state's source.
class cShaderSourceCommand : public cMaterialMetaCommand {
public:
    static constexpr uint32_t kVTable = 0xe7b2f0;
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0xad6320 (ret 4)
    const char *GetName() const;                       // slot 5, 0xad9900: "shaderSource"
    // Slot 9, 0xad8b30 (ret 4): called with the rest of the line when the block is opened by another command's line;
    // with nothing on it, the block is pushed as the open meta block.
    void Open(const char *rest);
    // Slot 10, 0xad9400 (ret 8): each line while the block is open (`line` a line object: slot 5 its text, slot 4 sets
    // it, slots 6 / 7 its expanded text and length). endShaderSource closes the block (false); when the line runs
    // (`active`), "__parseCommand" lines are run as commands (true), others expanded and appended to the source.
    bool HandleLine(cIGZString *line, uint8_t active);
};

// ---- shader (vtable 0xe7bf0c) --------------------------------------------------------------------------------------
// A shader inside a material (or default state) definition: gets the shader from the default state of the viewer
// render type, or a new one from the framework; its end validates it against the render shader context.
class cShaderCommand : public cMaterialBlockCommand {
public:
    static constexpr uint32_t kVTable = 0xe7bf0c;
    void SetContext(void *parser, void *stateInterface); // slot 3, 0xadbb60
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xad5a60
    const char *GetName() const;                         // slot 5, 0xad98d0: "shader"
    void EndBlock();                                     // slot 9, 0xad9e00
};

// ---- defaultState (vtable 0xe7bfac) --------------------------------------------------------------------------------
// A top-level block giving the shader a viewer render type starts from: the shader built in the block is stored at
// that render type in the state's default states.
class cDefaultStateCommand : public cMaterialBlockCommand {
public:
    static constexpr uint32_t kVTable = 0xe7bfac;
    void SetContext(void *parser, void *stateInterface); // slot 3, 0xadab40
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xad27f0
    const char *GetName() const;                         // slot 5, 0xad95e0: "defaultState"
    void EndBlock();                                     // slot 9, 0xad9a60
};

// ---- pass (vtable 0xe7bc84) ----------------------------------------------------------------------------------------
// A pass of a shader: the pass index counts up; fixed-function passes get their state object here; the end checks the
// programs and builds the output vertex format.
class cPassCommand : public cMaterialBlockCommand {
public:
    static constexpr uint32_t kVTable = 0xe7bc84;
    void SetContext(void *parser, void *stateInterface); // slot 3, 0xadb220
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xad4bc0
    const char *GetName() const;                         // slot 5, 0xad9780: "pass"
    void EndBlock();                                     // slot 9, 0xad9bc0
};

// ---- shaderProgram (vtable 0xe7b4e0) -------------------------------------------------------------------------------
// A vertex, pixel or preprocess program of a pass (or a fragment link at the top level): the source is collected by
// the block's lines, compiled at its end by the shader compiler service, and the program (and its constants) given to
// the shader.
class cShaderProgramCommand : public cMaterialBlockCommand {
public:
    static constexpr uint32_t kVTable = 0xe7b4e0;
    void SetContext(void *parser, void *stateInterface); // slot 3, 0xadbdf0
    void Execute(nGZCommandParser::cIArguments *args);   // slot 4, 0xad5c80
    const char *GetName() const;                         // slot 5, 0xad98f0: "shaderProgram"
    void EndBlock();                                     // slot 9, 0xad9fb0
    // 0xad0310 (__thiscall): empties the source; a buffer grown past 4 KB is given back for a fresh 4 KB one.
    void ResetSource();
};

// A constant of a compiled program as its constant table describes it (slot 5 of the table; D3DXCONSTANT_DESC's
// layout), and the table of constants shaderProgram binds by name (0xe78f18, 9 entries of 0x28 bytes).
struct ProgramConstantDesc {
    const char *name;     // +0
    uint32_t registerSet; // +4
    uint32_t registerIndex;
    uint32_t registerCount;
    uint32_t parameterClass; // +0x10
    uint32_t type;
    uint32_t rows;
    uint32_t columns;
    uint32_t elements; // +0x20
    uint32_t structMembers;
    uint32_t bytes; // +0x28
    const void *defaultValue;
};
static_assert(sizeof(ProgramConstantDesc) == 0x30, "ProgramConstantDesc layout");

struct AutoBoundConstant {
    const char *name;     // +0
    uint32_t registerSet; // +4
    uint32_t parameterClass;
    uint32_t type;
    uint32_t rows; // +0x10
    uint32_t columns;
    uint32_t elements;
    uint32_t unused1c;
    uint32_t bytes; // +0x20
    uint32_t source; // +0x24 its low byte goes to the binding
};
static_assert(sizeof(AutoBoundConstant) == 0x28, "AutoBoundConstant layout");

// What shaderProgram hands the constant binder (slot 14) for an auto-bound constant (0x20 bytes).
struct ConstantBinding {
    uint8_t source;
    uint8_t stage; // 3 for a pixel program, 0 otherwise
    uint16_t kind; // 4
    uint32_t registerIndex;
    uint32_t registerCount;
    int32_t minusOne;
    uint32_t zero[4];
};
static_assert(sizeof(ConstantBinding) == 0x20, "ConstantBinding layout");

static_assert(sizeof(cMaterialBlockCommand) == 0x44 && sizeof(cMaterialMetaCommand) == 12, "material command layout");

} // namespace nGZSceneGraph
