#pragma once
// The world of the material command tests (tests/gz_material_commands*.cpp): a command object, the material parser's
// state (a block of random bytes with real containers and fake objects in the fields the commands use), the parser's
// cIBaseParser face, the argument list, and the objects the framework hands out. Everything the commands call records
// into cptest::gLog (tests/command_parser.h); the original and ours run from the same memory (cptest::Differential).
#include "command_parser.h" // first: it defines NOMINMAX before windows.h
#include "code_patch.h"
#include "stack_out.h"

#include "gz/GraphicServices.h"
#include "gz/MaterialCommands.h"
#include "misc/UtilitiesE.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/RZFileStreams.h"

#include <cstring>
#include <new>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace mattest {

namespace sg = nGZSceneGraph;
namespace cp = nGZCommandParser;
using namespace cptest;

// Recording fakes with four to six stack arguments: the first four go to the event, the others to gExtra.
template <int S> uint32_t __fastcall Fn4(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return Record(self, S, a, b, c, d); }
template <int S> uint32_t __fastcall Fn5(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&e), reinterpret_cast<uint8_t *>(&e) + 4);
    return Record(self, S, a, b, c, d);
}
template <int S> uint32_t __fastcall Fn6(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&e), reinterpret_cast<uint8_t *>(&e) + 4);
    gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&f), reinterpret_cast<uint8_t *>(&f) + 4);
    return Record(self, S, a, b, c, d);
}

template <int S> void SetArity(FakeVTable &vt, int arity)
{
    switch (arity) {
    case 4: vt.slots[S] = reinterpret_cast<void *>(&Fn4<S>); break;
    case 5: vt.slots[S] = reinterpret_cast<void *>(&Fn5<S>); break;
    case 6: vt.slots[S] = reinterpret_cast<void *>(&Fn6<S>); break;
    default: vt.Set<S>(arity); break;
    }
}

// A fake object: the first word is a FakeVTable's slots (its id is what the logs show).
struct FakeObject {
    void *vtable;
    uint32_t words[3];
};

// The kinds of object the framework and the state hold, one fake vtable each.
enum Kind {
    kDefinition,
    kShader,
    kValidator,
    kDefaultState,
    kPixelProgram,
    kOther,
    kProgram,   // a compiled program
    kConstants, // a compiled program's constant table
    kBinder,    // a pass's constant binder
    kFallback,  // a default state's fallback state
    kFormat,    // a built output vertex format
    kKinds
};
constexpr int kPerKind = 3;

// ---- what the fakes answer (set per case, the same on both sides) ----------------------------------------------------

inline void *gClassObject = nullptr; // what the framework's GetClassObject writes to its out-parameter
inline uint32_t gClassObjectResult = 1;
inline uint32_t gServiceQueryResult = 0; // 0x18ae40's answer
inline int32_t gPositionalCount = 0;     // what the argument list's slot 10 writes to its count
inline sg::SamplerList gSamplerList{};   // what the pixel program's slot 7 answers
inline uint32_t gFramework[4], gManager[4];

// A stack address (a local of the caller: the two sides' frames differ) is logged as a marker.
inline bool OnStack(const void *p)
{
    ULONG_PTR low, high;
    GetCurrentThreadStackLimits(&low, &high);
    return reinterpret_cast<ULONG_PTR>(p) >= low && reinterpret_cast<ULONG_PTR>(p) < high;
}
inline uint32_t Arg(const void *p) { return OnStack(p) ? 0x57AC0000u : reinterpret_cast<uint32_t>(p); }

inline void ExtraBytes(const void *p, size_t n)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    gExtra.insert(gExtra.end(), b, b + n);
}
inline void ExtraWord(uint32_t v) { ExtraBytes(&v, 4); }
inline void ExtraText(const char *s)
{
    ExtraWord(s ? static_cast<uint32_t>(std::strlen(s)) : 0xffffffffu);
    if (s) ExtraBytes(s, std::strlen(s));
}

// The framework object (GetDirectorFrameworkObject's): slot 3 records and writes gClassObject to the out-parameter.
inline uint32_t __fastcall FrameworkGetClassObject(void *self, void *, uint32_t cls, uint32_t iid, void **out)
{
    Record(self, 3, cls, iid, Arg(out));
    *out = gClassObject;
    return gClassObjectResult;
}

// A default state's slot 3: a copy of its shader (interface, out): records and writes gClassObject.
inline uint32_t __fastcall DefaultStateCopy(void *self, void *, uint32_t iid, void **out)
{
    Record(self, 3, iid, Arg(out));
    *out = gClassObject;
    return gClassObjectResult;
}

// The argument list's switches by name: slot 1 (TakeSwitch) answers gSwitches[name] (0 when absent), slot 3
// (TakeSwitchArgumentsExactly) gSwitchArgs[name] (null when absent).
inline std::vector<std::pair<std::string, uint32_t>> gSwitches;
inline std::vector<std::pair<std::string, const char *const *>> gSwitchArgs;
inline uint32_t __fastcall ArgsTakeSwitch(void *self, void *, const char *name)
{
    Record(self, 1, reinterpret_cast<uint32_t>(name));
    for (const auto &s : gSwitches)
        if (s.first == name) return s.second;
    return 0;
}
inline const char *const *__fastcall ArgsTakeSwitchArgs(void *self, void *, const char *name, uint32_t count)
{
    Record(self, 3, reinterpret_cast<uint32_t>(name), count);
    for (const auto &s : gSwitchArgs)
        if (s.first == name) return s.second;
    return nullptr;
}

// ---- the shader compiler (0x18ddf0's), the vertex format builder (0x18dcf0's), output files -------------------------

inline void *gProgramOut = nullptr;   // what the compiler writes to its program out-parameter
inline void *gConstantsOut = nullptr; // ... and to its constants out-parameter
inline void *gFoundOut = nullptr;     // what a shader's find slots (22, 23, 24) write when they answer true
inline void *gFormatOut = nullptr;    // what the builder's slot 5 writes
inline uint32_t gWriteResult = 1;     // what the output file's writes answer
inline void *gStreamSlots = nullptr;  // the output file's fake vtable
inline uint32_t gCompiler[4], gBuilder[4];
inline const char *gResourceName = "Res"; // Dispatch8's answer (the resource's name)
inline std::vector<sg::ProgramConstantDesc> gDescs; // the constants slot 5 describes

// Slot 6, preprocess (text, size, errors, flags).
inline uint32_t __fastcall CompilerPreprocess(void *self, void *, const char *text, uint32_t size, uint32_t *errors, uint32_t flags)
{
    uint32_t r = Record(self, 6, size, *errors, flags, Arg(errors));
    ExtraBytes(text, size);
    return r;
}
// Slot 3, assemble (device, text, size, program, errors, flags).
inline uint32_t __fastcall CompilerAssemble(void *self, void *, uint32_t device, const char *text, uint32_t size, void **program,
                                            uint32_t *errors, uint32_t flags)
{
    uint32_t r = Record(self, 3, device, size, flags, *errors);
    ExtraWord(Arg(program));
    ExtraWord(Arg(errors));
    ExtraBytes(text, size);
    *program = gProgramOut;
    return r;
}
// Slot 4, compile (device, entry point, profile, text, size, program, constants, errors, flags).
inline uint32_t __fastcall CompilerCompile(void *self, void *, uint32_t device, const char *entry, const char *profile, const char *text,
                                           uint32_t size, void **program, void **constants, uint32_t *errors, uint32_t flags)
{
    uint32_t r = Record(self, 4, device, size, flags, *errors);
    ExtraText(entry);
    ExtraText(profile);
    ExtraWord(Arg(program));
    ExtraWord(Arg(constants));
    ExtraWord(Arg(errors));
    ExtraBytes(text, size);
    *program = gProgramOut;
    *constants = gConstantsOut;
    return r;
}
// Slot 9, link (device, program, constants, errors, flags).
inline uint32_t __fastcall CompilerLink(void *self, void *, uint32_t device, void **program, void **constants, uint32_t *errors,
                                        uint32_t flags)
{
    uint32_t r = Record(self, 9, device, flags, *errors, Arg(program));
    ExtraWord(Arg(constants));
    ExtraWord(Arg(errors));
    *program = gProgramOut;
    *constants = gConstantsOut;
    return r;
}
// Slot 11, disassemble (listing, program).
inline uint32_t __fastcall CompilerDisassemble(void *self, void *, uint32_t *listing, uint32_t program)
{
    return Record(self, 11, *listing, program, Arg(listing));
}

// The constant table: slot 5 describes constant i (desc, i) from gDescs.
inline uint32_t __fastcall ConstantsDescribe(void *self, void *, sg::ProgramConstantDesc *desc, uint32_t i)
{
    uint32_t r = Record(self, 5, Arg(desc), i);
    difftest::FillOut(desc, sizeof(*desc));
    if (i < gDescs.size()) *desc = gDescs[i];
    return r;
}
// The binder's slot 14: the binding's bytes.
inline uint32_t __fastcall BinderBind(void *self, void *, const sg::ConstantBinding *binding)
{
    uint32_t r = Record(self, 14, Arg(binding));
    ExtraBytes(binding, sizeof(*binding));
    return r;
}
// The shader's find slots: 22 / 23 (pass, interface, out), 24 (interface, out): write gFoundOut when they answer true.
template <int S> uint32_t __fastcall ShaderFind3(void *self, void *, uint32_t pass, uint32_t iid, void **out)
{
    uint32_t r = Record(self, S, pass, iid, Arg(out));
    if (r & 0xff) *out = gFoundOut;
    return r;
}
inline uint32_t __fastcall ShaderFind2(void *self, void *, uint32_t iid, void **out)
{
    uint32_t r = Record(self, 24, iid, Arg(out));
    if (r & 0xff) *out = gFoundOut;
    return r;
}
// The builder's slot 5 (out).
inline uint32_t __fastcall BuilderBuild(void *self, void *, void **out)
{
    uint32_t r = Record(self, 5, Arg(out));
    *out = gFormatOut;
    return r;
}

// The output file: the constructor stand-in (0x1b4e80, this + 5 words) gives the object the fake vtable and logs the
// path; slot 14 writes a C string, 0x1b6109 a cIGZString; the destructor (0x1b52d8) logs.
inline void *__fastcall StandFileOpen(void *self, void *, const char *path, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    gLog.push_back({23, 0, Arg(self), {a, b, c, d}});
    ExtraText(path);
    *static_cast<void **>(self) = gStreamSlots;
    return self;
}
inline void __fastcall StandFileClose(void *self, void *)
{
    gLog.push_back({24, 0, Arg(self), {*static_cast<void **>(self) == gStreamSlots, 0, 0, 0}});
}
inline uint32_t __fastcall StandFileWriteString(void *self, void *, uint32_t *text)
{
    gLog.push_back({25, 0, Arg(self), {*text, Arg(text), 0, 0}});
    return gWriteResult;
}
inline uint32_t __fastcall FileWrite(void *self, void *, const char *text)
{
    gLog.push_back({26, 14, Arg(self), {0, 0, 0, 0}});
    ExtraText(text);
    return gWriteResult;
}

inline void *StandCompiler()
{
    gLog.push_back({27, 0, 0, {0, 0, 0, 0}});
    return gCompiler;
}
inline void *StandBuilder()
{
    gLog.push_back({28, 0, 0, {0, 0, 0, 0}});
    return gBuilder;
}
inline uint32_t __fastcall StandDispatch8(void *self)
{
    gLog.push_back({29, 0, reinterpret_cast<uint32_t>(self), {0, 0, 0, 0}});
    return reinterpret_cast<uint32_t>(gResourceName);
}

// The stand-ins the shader program and pass tests need besides Stands.
struct ProgramStands {
    shape::StandIn compiler{0x18ddf0u, &nGZGraphic4::GetShaderProgramsManagerCached, &StandCompiler};
    shape::StandIn builder{0x18dcf0u, &nGZGraphic4::GetVertexFormatsManagerCached, &StandBuilder};
    shape::StandIn open{0x1b4e80u,
                        static_cast<cRZOFStreamFromChars>(&cRZOFStream::Construct),
                        &StandFileOpen};
    shape::StandIn close{0x1b52d8u, &cRZOFStream::Destruct, &StandFileClose};
    shape::StandIn writeString{0x1b6109u, &cRZOFStream::SetGZStr, &StandFileWriteString};
    shape::StandIn resourceName{0xaeb800, &Dispatch8, &StandDispatch8};
};

// The parser's name tables shaderProgram reads (unset in the harness's image): the targets (0x10f6b34) and the compile
// methods (0x10f6b40), each a sorted vector of (name, value) entries. Put back after.
struct EnumTables {
    difftest::Context &ctx;
    uint8_t saved[2][sizeof(std::vector<cp::EnumEntry>)];
    static constexpr uint32_t kTables[2] = {0x10f6b34, 0x10f6b40};
    explicit EnumTables(difftest::Context &c) : ctx(c)
    {
        // Names of increasing length: the table order (length first).
        const std::vector<cp::EnumEntry> entries[2] = {
            {{"v", 1}, {"px", 2}, {"pre", 0}, {"frag", 3}, {"weird", 7}},
            {{"a", 0}, {"bb", 1}, {"ccc", 2}, {"dddd", 3}, {"eeeee", 9}},
        };
        for (int i = 0; i < 2; ++i) {
            std::memcpy(saved[i], ctx.image.At(kTables[i]), sizeof(saved[i]));
            new (ctx.image.At(kTables[i])) std::vector<cp::EnumEntry>(entries[i]);
        }
    }
    ~EnumTables()
    {
        for (int i = 0; i < 2; ++i) {
            using V = std::vector<cp::EnumEntry>;
            ctx.image.At<V>(kTables[i])->~V();
            std::memcpy(ctx.image.At(kTables[i]), saved[i], sizeof(saved[i]));
        }
    }
};

// The argument list's slot 10 (Positional): records, writes the count, answers ret[10].
inline uint32_t __fastcall ArgsPositional(void *self, void *, uint32_t *count, uint32_t least, uint32_t most)
{
    uint32_t r = Record(self, 10, reinterpret_cast<uint32_t>(count) != 0, least, most);
    if (count) *count = static_cast<uint32_t>(gPositionalCount);
    return r;
}

// The pixel program's slot 7: the samplers of a name, by a hidden return pointer (a stack buffer of the caller's: its
// address is not logged).
inline sg::SamplerList *__fastcall PixelProgramSamplers(void *self, void *, sg::SamplerList *out, const char *name)
{
    Record(self, 7, 0, reinterpret_cast<uint32_t>(name));
    *out = gSamplerList;
    difftest::FillOut(out, sizeof(*out));
    return out;
}

// 0x18ae40 takes no arguments (the word a caller leaves on the stack is the next call's argument).
inline uint32_t __cdecl StandServiceQuery()
{
    gLog.push_back({20, 0, 0, {0, 0, 0, 0}});
    return gServiceQueryResult;
}
inline void *StandFramework()
{
    gLog.push_back({21, 0, 0, {0, 0, 0, 0}});
    return gFramework;
}
inline void *StandManager()
{
    gLog.push_back({22, 0, 0, {0, 0, 0, 0}});
    return gManager;
}

// The stand-ins every material command test needs, for its life.
struct Stands {
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn manager{0xae8900, &GetCachedQueriedObject, &StandManager};
    shape::StandIn serviceQuery{0x18ae40u, &nGZGraphic4::GetCurrentDeviceCached, &StandServiceQuery};
};

// The globals the line handling reads (unset in the harness's image): the parser's separator string (0x107479c, a
// std::string) and the lengths of "endShaderSource" (0x1070050) and "__parseCommand" (0x1070054). Put back after.
struct LineGlobals {
    difftest::Context &ctx;
    uint8_t savedString[sizeof(std::string)];
    uint32_t savedLengths[2];
    static constexpr uint32_t kLengths[2] = {0x1070050, 0x1070054};
    explicit LineGlobals(difftest::Context &c) : ctx(c)
    {
        std::memcpy(savedString, ctx.image.At(0x107479c), sizeof(savedString));
        new (ctx.image.At(0x107479c)) std::string(" \t");
        const uint32_t lengths[2] = {15, 14};
        for (int i = 0; i < 2; ++i) {
            savedLengths[i] = *ctx.image.At<uint32_t>(kLengths[i]);
            *ctx.image.At<uint32_t>(kLengths[i]) = lengths[i];
        }
    }
    ~LineGlobals()
    {
        ctx.image.At<std::string>(0x107479c)->~basic_string();
        std::memcpy(ctx.image.At(0x107479c), savedString, sizeof(savedString));
        for (int i = 0; i < 2; ++i) *ctx.image.At<uint32_t>(kLengths[i]) = savedLengths[i];
    }
};

// ---- the command's own vtable ----------------------------------------------------------------------------------------
//
// Slot 10 (AddCommand) records the child a block's slot 3 made: its words that are not heap pointers (the vtable, the
// parser, the count; a block's value, table size and bucket count) go to gExtra, and the child is destroyed. The other
// slots record.
inline uint32_t __fastcall CmdAddCommand(void *self, void *, void *child)
{
    gLog.push_back({-1, 10, reinterpret_cast<uint32_t>(self), {child != nullptr, 0, 0, 0}});
    if (!child) return 0;
    const uint32_t *w = static_cast<const uint32_t *>(child);
    // Which base built it: the size of the block decides what is compared; a block command's heap block is 0x44.
    uint32_t kind = static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, child));
    gExtra.insert(gExtra.end(), reinterpret_cast<const uint8_t *>(&kind), reinterpret_cast<const uint8_t *>(&kind) + 4);
    size_t words = kind == 0xc ? 2 : kind == 0x10 ? 3 : 4; // a command's state word (+0xc) is not written
    gExtra.insert(gExtra.end(), reinterpret_cast<const uint8_t *>(w), reinterpret_cast<const uint8_t *>(w + words));
    if (kind == 0x44) {
        auto *block = static_cast<cp::cBlockCommandBase *>(child);
        // Not the case flag (+0x3c): cBlockCommand's constructor 0xaf7750 leaves it as the heap had it (slot 3 sets it).
        gExtra.push_back(static_cast<uint8_t>(block->mCommands.size));
        gExtra.push_back(static_cast<uint8_t>(block->mCommands.bucketCount));
        block->Destruct();
    }
    ::operator delete(child);
    return 0;
}

struct CmdVTable {
    FakeVTable vt{-2};
    CmdVTable()
    {
        vt.Set<9>(0);
        vt.Set<11>(2);
        vt.Set<12>(1);
        vt.slots[10] = reinterpret_cast<void *>(&CmdAddCommand);
    }
};

// ---- the world ---------------------------------------------------------------------------------------------------------

constexpr size_t kStateSize = 0x400;
constexpr size_t kCmdSize = 0x50;

inline const char *const kWords[] = {"alpha", "", "x", "set up", "100%", "a\\b", "ThE end", "gSampler", "3", "17"};

struct World {
    FakeVTable argsVt{1}, parserVt{2}, faceVt{6}, frameworkVt{7}, managerVt{8}, lineVt{9};
    FakeVTable kindVt[kKinds] = {FakeVTable{10}, FakeVTable{11}, FakeVTable{12}, FakeVTable{13}, FakeVTable{14}, FakeVTable{15},
                                 FakeVTable{16}, FakeVTable{17}, FakeVTable{18}, FakeVTable{19}, FakeVTable{20}};
    FakeVTable compilerVt{30}, builderVt{31}, streamVt{32};
    CmdVTable cmdVt;
    FakeObject objects[kKinds][kPerKind];
    alignas(8) uint8_t args[16] = {};
    alignas(8) uint8_t parser[16] = {};
    alignas(8) uint8_t face[16] = {};
    alignas(8) uint8_t line[16] = {};
    alignas(16) uint8_t state[kStateSize] = {};
    alignas(8) uint8_t cmd[kCmdSize] = {};
    int32_t vbtable[4] = {};
    const char *argv[8] = {};
    const char *switchArgs[4] = {};
    std::mt19937 &gen;
    std::function<void()> before, after; // rebuild / capture the containers around each side's run

    explicit World(std::mt19937 &g) : gen(g)
    {
        *reinterpret_cast<void **>(args) = argsVt.slots;
        *reinterpret_cast<void **>(parser) = parserVt.slots;
        *reinterpret_cast<void **>(face) = faceVt.slots;
        *reinterpret_cast<void **>(line) = lineVt.slots;
        *reinterpret_cast<void **>(gFramework) = frameworkVt.slots;
        *reinterpret_cast<void **>(gManager) = managerVt.slots;
        for (int k = 0; k < kKinds; ++k)
            for (int i = 0; i < kPerKind; ++i) objects[k][i] = {kindVt[k].slots, {0, 0, 0}};
        for (auto &w : reinterpret_cast<uint32_t(&)[kStateSize / 4]>(state)) w = gen();
        for (auto &w : reinterpret_cast<uint32_t(&)[kCmdSize / 4]>(cmd)) w = gen();
        for (auto &s : argv) s = kWords[gen() % std::size(kWords)];
        for (auto &s : switchArgs) s = kWords[gen() % std::size(kWords)];
        // The state's cIBaseParser face: the vbtable at state+8 gives its offset (outside the state block).
        vbtable[2] = static_cast<int32_t>(face - (state + 8));
        *reinterpret_cast<void **>(state + 8) = vbtable;
        // The argument list.
        argsVt.Set<1>(1);
        argsVt.Set<2>(4);
        argsVt.Set<3>(2);
        argsVt.Set<4>(1);
        argsVt.Set<6>(1);
        argsVt.slots[10] = reinterpret_cast<void *>(&ArgsPositional);
        argsVt.Set<11>(1);
        argsVt.Set<12>(1);
        argsVt.ret[11] = reinterpret_cast<uint32_t>(argv);
        argsVt.ret[10] = reinterpret_cast<uint32_t>(argv);
        // The parser face's slots the commands call.
        faceVt.Set<16>(1);
        faceVt.Set<17>(1);
        faceVt.Set<20>(1);
        faceVt.Set<22>(1);
        faceVt.Set<29>(2);
        faceVt.Set<30>(1);
        faceVt.Set<31>(1);
        faceVt.Set<32>(1);
        faceVt.Set<33>(1);
        faceVt.Set<34>(3);
        faceVt.Set<35>(3);
        faceVt.Set<36>(3);
        faceVt.Set<37>(2);
        faceVt.Set<38>(2);
        faceVt.Set<39>(2);
        frameworkVt.slots[3] = reinterpret_cast<void *>(&FrameworkGetClassObject);
        managerVt.Set<23>(2);
        lineVt.Set<4>(1);
        kindVt[kPixelProgram].slots[7] = reinterpret_cast<void *>(&PixelProgramSamplers);
        kindVt[kDefinition].Set<10>(1);
        kindVt[kShader].Set<4>(1);
        // cShaderCommand::EndBlock: the description's slot 21 takes nothing (cShaderInit 0x142840, ret 0), the built
        // shader's slot 8 takes five arguments (cShader 0xaa5030, ret 0x14).
        kindVt[kShader].Set<21>(0);
        SetArity<8>(kindVt[kValidator], 5);
        // The shader's pass slots (pass, value): programs 6 / 14 / 15, binders 8 / 9, the format 7, the fixed-function
        // state 9, the pass flags 16..19; its finds 22 / 23 / 24; its fallback slot 10.
        FakeVTable &shader = kindVt[kShader];
        shader.Set<6>(2);
        shader.Set<7>(2);
        shader.Set<8>(2);
        shader.Set<9>(2);
        shader.Set<14>(2);
        shader.Set<15>(2);
        shader.Set<16>(2);
        shader.Set<17>(2);
        shader.Set<18>(2);
        shader.Set<19>(2);
        kindVt[kShader].slots[22] = reinterpret_cast<void *>(&ShaderFind3<22>);
        kindVt[kShader].slots[23] = reinterpret_cast<void *>(&ShaderFind3<23>);
        kindVt[kShader].slots[24] = reinterpret_cast<void *>(&ShaderFind2);
        kindVt[kShader].Set<10>(1);
        kindVt[kFallback].Set<19>(1);
        kindVt[kConstants].slots[5] = reinterpret_cast<void *>(&ConstantsDescribe);
        kindVt[kBinder].slots[14] = reinterpret_cast<void *>(&BinderBind);
        *reinterpret_cast<void **>(gCompiler) = compilerVt.slots;
        compilerVt.slots[3] = reinterpret_cast<void *>(&CompilerAssemble);
        compilerVt.slots[4] = reinterpret_cast<void *>(&CompilerCompile);
        compilerVt.slots[6] = reinterpret_cast<void *>(&CompilerPreprocess);
        compilerVt.slots[9] = reinterpret_cast<void *>(&CompilerLink);
        compilerVt.slots[11] = reinterpret_cast<void *>(&CompilerDisassemble);
        *reinterpret_cast<void **>(gBuilder) = builderVt.slots;
        builderVt.slots[4] = reinterpret_cast<void *>(&Fn4<4>);
        builderVt.slots[5] = reinterpret_cast<void *>(&BuilderBuild);
        streamVt.slots[14] = reinterpret_cast<void *>(&FileWrite);
        gStreamSlots = streamVt.slots;
        kindVt[kDefaultState].slots[3] = reinterpret_cast<void *>(&DefaultStateCopy);
        // The parser the block's slot 3 is given: its slot 43 (case sensitive?) answers.
        parserVt.ret[43] = RandomResult();
        // The command: its own vtable, the parser at +4.
        *reinterpret_cast<void **>(cmd) = cmdVt.vt.slots;
    }

    sg::cMaterialParserFields *State() { return reinterpret_cast<sg::cMaterialParserFields *>(state); }
    uint32_t RandomResult() { return gen() % 3 == 0 ? (gen() & 0xffffff00u) : gen(); }
    // A fake object of a kind, or (sometimes, when allowed) null.
    void *Object(Kind k, bool maybeNull = true) { return maybeNull && gen() % 4 == 0 ? nullptr : &objects[k][gen() % kPerKind]; }
    void Put(size_t offset, uint32_t value) { *reinterpret_cast<uint32_t *>(state + offset) = value; }
    void PutObject(size_t offset, Kind k, bool maybeNull = true) { Put(offset, reinterpret_cast<uint32_t>(Object(k, maybeNull))); }

    // A block command (state at +0x40) or a meta command (state at +8).
    void AsBlock() { *reinterpret_cast<void **>(cmd + 0x40) = state; *reinterpret_cast<void **>(cmd + 4) = parser; }
    void AsMeta() { *reinterpret_cast<void **>(cmd + 8) = state; }

    // The counted fields: fake objects or null, never random words. The strings: empty.
    void SaneFields()
    {
        PutObject(0x1d8, kDefinition);
        PutObject(0x1fc, kShader);
        PutObject(0x200, kShader);
        PutObject(0x204, kPixelProgram);
        for (size_t off : {0x208u, 0x22cu, 0x244u}) {
            std::memset(state + off, 0, 0x18);
            Put(off + 0x14, 15);
        }
        std::memset(state + 0x1ac, 0, 12); // mDefaultStates
        std::memset(state + 0x220, 0, 12); // mSource
        std::memset(state + 0x25c, 0, 12); // mOutputVertexFormat
    }

    // mDefaultStates: rebuilt before each side's run with `entries` (fake objects or null), read into gExtra after (as
    // object codes) and freed.
    std::vector<void *> defaultStates;
    size_t defaultCapacity = 0;
    // mSource: rebuilt from `source` with `sourceCapacity`, read into gExtra after (size, capacity, bytes) and freed.
    std::string source;
    size_t sourceCapacity = 0;
    bool withDefaultStates = false, withSource = false;

    uint32_t Code(const void *p)
    {
        if (!p) return 0;
        for (int k = 0; k < kKinds; ++k)
            for (int i = 0; i < kPerKind; ++i)
                if (p == &objects[k][i]) return 0x0b000000u | (k << 8) | i;
        return 0xdeadu;
    }
    // mOutputVertexFormat (+0x25c): rebuilt from `format` with `formatCapacity`, read into gExtra after and freed.
    std::vector<sg::OutputVertexElement> format;
    size_t formatCapacity = 0;
    bool withFormat = false;
    void *defaultBlock = nullptr, *sourceBlock = nullptr, *formatBlock = nullptr; // the blocks Before() made

    // The blocks are the STL allocator's (a block of 4 KB or more is the aligned kind, as the game's STL makes).
    void Before()
    {
        if (withDefaultStates) {
            void **block = defaultCapacity ? std::allocator<void *>().allocate(defaultCapacity) : nullptr;
            for (size_t i = 0; i < defaultStates.size(); ++i) block[i] = defaultStates[i];
            defaultBlock = block;
            Put(0x1ac, reinterpret_cast<uint32_t>(block));
            Put(0x1b0, reinterpret_cast<uint32_t>(block + defaultStates.size()));
            Put(0x1b4, reinterpret_cast<uint32_t>(block + defaultCapacity));
        }
        if (withSource) {
            char *block = sourceCapacity ? std::allocator<char>().allocate(sourceCapacity) : nullptr;
            if (!source.empty()) std::memcpy(block, source.data(), source.size());
            sourceBlock = block;
            Put(0x220, reinterpret_cast<uint32_t>(block));
            Put(0x224, reinterpret_cast<uint32_t>(block + source.size()));
            Put(0x228, reinterpret_cast<uint32_t>(block + sourceCapacity));
        }
        if (withFormat) {
            sg::OutputVertexElement *block = formatCapacity ? std::allocator<sg::OutputVertexElement>().allocate(formatCapacity) : nullptr;
            for (size_t i = 0; i < format.size(); ++i) block[i] = format[i];
            formatBlock = block;
            Put(0x25c, reinterpret_cast<uint32_t>(block));
            Put(0x260, reinterpret_cast<uint32_t>(block + format.size()));
            Put(0x264, reinterpret_cast<uint32_t>(block + formatCapacity));
        }
        if (before) before();
    }
    void After()
    {
        if (after) after();
        auto add = [](uint32_t v) { gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&v), reinterpret_cast<uint8_t *>(&v) + 4); };
        if (withDefaultStates) {
            void **b = *reinterpret_cast<void ***>(state + 0x1ac), **e = *reinterpret_cast<void ***>(state + 0x1b0),
                 **c = *reinterpret_cast<void ***>(state + 0x1b4);
            add(static_cast<uint32_t>(e - b));
            add(static_cast<uint32_t>(c - b));
            // The block kept, or a new one: told only when the capacity is unchanged (a new block of another capacity may
            // come back at the freed block's address, so its address says nothing).
            add(static_cast<size_t>(c - b) == defaultCapacity ? b == defaultBlock : 2);
            for (void **p = b; p != e; ++p) add(Code(*p));
            if (b) std::allocator<void *>().deallocate(b, static_cast<size_t>(c - b));
            std::memset(state + 0x1ac, 0, 12);
        }
        if (withSource) {
            char *b = *reinterpret_cast<char **>(state + 0x220), *e = *reinterpret_cast<char **>(state + 0x224),
                 *c = *reinterpret_cast<char **>(state + 0x228);
            add(static_cast<uint32_t>(e - b));
            add(static_cast<uint32_t>(c - b));
            add(static_cast<size_t>(c - b) == sourceCapacity ? b == sourceBlock : 2); // as above
            gExtra.insert(gExtra.end(), b, e);
            if (b) std::allocator<char>().deallocate(b, static_cast<size_t>(c - b));
            std::memset(state + 0x220, 0, 12);
        }
        if (withFormat) {
            auto *b = *reinterpret_cast<sg::OutputVertexElement **>(state + 0x25c), *e = *reinterpret_cast<sg::OutputVertexElement **>(state + 0x260),
                 *c = *reinterpret_cast<sg::OutputVertexElement **>(state + 0x264);
            add(static_cast<uint32_t>(e - b));
            add(static_cast<uint32_t>(c - b));
            add(static_cast<size_t>(c - b) == formatCapacity ? b == formatBlock : 2); // as above
            gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(b), reinterpret_cast<uint8_t *>(e));
            if (b) std::allocator<sg::OutputVertexElement>().deallocate(b, static_cast<size_t>(c - b));
            std::memset(state + 0x25c, 0, 12);
        }
    }

    // The state's strings (mCompileVersion +0x208, mDumpSourcePath +0x22c, mDisassemblyPath +0x244): set to short texts
    // (kept in the string itself, no heap block), and read into gExtra after a run (then emptied: a heap block's
    // address differs between the sides).
    static constexpr size_t kStrings[3] = {0x208, 0x22c, 0x244};
    void PutString(size_t offset, const char *text)
    {
        std::memset(state + offset, 0, 0x18);
        new (state + offset) std::string(text);
        if (std::strlen(text) > 15) std::abort(); // a heap block would be shared by the two runs
    }
    void CaptureStrings()
    {
        for (size_t offset : kStrings) {
            auto *s = reinterpret_cast<std::string *>(state + offset);
            ExtraText(s->c_str());
            s->~basic_string();
            std::memset(state + offset, 0, 0x18);
            Put(offset + 0x14, 15);
        }
    }

    std::vector<Region> Regions() { return {{cmd, kCmdSize}, {state, kStateSize}, {objects, sizeof(objects)}}; }
};

// Runs the original slot (`rva`, __thiscall with the arguments given) and ours from the same world, compares.
template <class Orig, class Ours> bool Compare(difftest::Context &ctx, World &w, const char *what, Orig orig, Ours ours)
{
    return Differential(
        ctx, what, w.Regions(), [&] { return RunGuarded(orig); }, [&] { return RunGuarded(ours); }, [&] { w.After(); },
        [&] { w.Before(); });
}

} // namespace mattest
