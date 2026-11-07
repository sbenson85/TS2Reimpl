// Differential tests for the material script's commands (src/gz/MaterialCommands.cpp): materialDefinition, sampler,
// shaderSource and shader. The world: tests/gz_material_commands.h.
#include "gz_material_commands.h"

#include "gz/cIGZString.h"

using namespace mattest;

namespace {

using SetContextFn = void(__thiscall *)(void *, void *, void *);
using ExecuteFn = void(__thiscall *)(void *, void *);
using VoidFn = void(__thiscall *)(void *);

// ---- the blocks' slot 3 (the children each block adds) -----------------------------------------------------------------

template <class C> void SetContextTest(difftest::Context &ctx, uint32_t rva, const char *name)
{
    auto orig = ctx.Original<SetContextFn>(rva);
    Stands stands;
    std::mt19937 gen(rva);
    for (int i = 0; i < 30; ++i) {
        World w(gen);
        w.AsBlock();
        // The state arrives as its interface, 4 bytes in (null stays null).
        void *iface = i % 5 == 4 ? nullptr : w.state + 4;
        char what[96];
        std::snprintf(what, sizeof(what), "%s case %d", name, i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.parser, iface); },
            [&] { reinterpret_cast<C *>(w.cmd)->SetContext(w.parser, iface); });
    }
}

// The GetName slots: the name in the game's data, whatever `this`.
template <class C> void NameTest(difftest::Context &ctx, uint32_t rva, const char *expected)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(rva);
    uint8_t object[0x50] = {};
    const char *a = orig(object);
    const char *b = reinterpret_cast<C *>(object)->GetName();
    ++ctx.cases;
    if (a != b || std::strcmp(b, expected) != 0) ctx.Fail("GetName %08x: %p '%s' vs %p '%s'", rva, a, a, b, b);
}

} // namespace

DIFF_TEST(MaterialCommands_SetContext, 0xadb160)
{
    SetContextTest<sg::cMaterialDefinitionCommand>(ctx, 0xadb160, "materialDefinition");
    SetContextTest<sg::cSamplerCommand>(ctx, 0xadb9b0, "sampler");
    SetContextTest<sg::cShaderCommand>(ctx, 0xadbb60, "shader");
}

DIFF_TEST(MaterialCommands_Names, 0xad9720)
{
    NameTest<sg::cMaterialDefinitionCommand>(ctx, 0xad9720, "materialDefinition");
    NameTest<sg::cSamplerCommand>(ctx, 0xad98a0, "sampler");
    NameTest<sg::cShaderSourceCommand>(ctx, 0xad9900, "shaderSource");
    NameTest<sg::cShaderCommand>(ctx, 0xad98d0, "shader");
}

// ---- materialDefinition ------------------------------------------------------------------------------------------------

DIFF_TEST(MaterialDefinitionCommand_Execute, 0xad45d0)
{
    auto orig = ctx.Original<ExecuteFn>(0xad45d0);
    Stands stands;
    std::mt19937 gen(0xad45d0);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        w.Put(0x1d0, i % 4 == 3 ? 1 + gen() % 6 : 0); // top level, or inside a block (throws)
        gClassObject = w.Object(kDefinition, false);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<sg::cMaterialDefinitionCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

DIFF_TEST(MaterialDefinitionCommand_EndBlock, 0xad9b60)
{
    auto orig = ctx.Original<VoidFn>(0xad9b60);
    Stands stands;
    std::mt19937 gen(0xad9b60);
    for (int i = 0; i < 40; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd); }, [&] { reinterpret_cast<sg::cMaterialDefinitionCommand *>(w.cmd)->EndBlock(); });
    }
}

// ---- sampler -----------------------------------------------------------------------------------------------------------

DIFF_TEST(SamplerCommand_Execute, 0xad57c0)
{
    auto orig = ctx.Original<ExecuteFn>(0xad57c0);
    Stands stands;
    std::mt19937 gen(0xad57c0);
    static uint32_t indices[20];
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        w.Put(0x1d0, gen() % 5 == 0 ? gen() % 7 : 4);
        w.state[0x26c] = gen() % 6 == 0;                                       // in a sampler already
        w.state[0x270] = gen() % 6 == 0 ? 0 : (gen() % 2 ? 1 : 0x80);           // a pixel program
        w.Put(0x280, gen() % 6 == 0 ? gen() % 8 : 0xffffffffu);                 // texture stage cascade
        gSamplerList.found = gen() % 3 == 0 ? 0 : (gen() % 2 ? 1 : gen());
        gSamplerList.count = gen() % 17;
        for (auto &x : indices) x = gen() % 10 == 0 ? 16 + gen() % 4 : gen() % 16;
        gSamplerList.indices = indices;
        w.faceVt.ret[33] = gen() % 8 == 0 ? 16 + gen() % 100 : gen() % 16;
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<sg::cSamplerCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

DIFF_TEST(SamplerCommand_EndBlock, 0xad9de0)
{
    auto orig = ctx.Original<VoidFn>(0xad9de0);
    std::mt19937 gen(0xad9de0);
    for (int i = 0; i < 20; ++i) {
        World w(gen);
        w.AsBlock();
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Compare(ctx, w, what, [&] { orig(w.cmd); }, [&] { reinterpret_cast<sg::cSamplerCommand *>(w.cmd)->EndBlock(); });
    }
}

// ---- shaderSource ------------------------------------------------------------------------------------------------------

namespace {
const char *const kSourceTexts[] = {"", "float4 main() : COLOR", "x", "a much longer line of shader source text, past sixteen", "\t"};

void RandomSource(World &w)
{
    w.withSource = true;
    w.source.clear();
    size_t n = w.gen() % 3 == 0 ? 0 : w.gen() % 40;
    for (size_t k = 0; k < n; ++k) w.source += static_cast<char>('a' + w.gen() % 26);
    w.sourceCapacity = w.source.size() + (w.gen() % 3 == 0 ? 0 : w.gen() % 70);
}
} // namespace

DIFF_TEST(ShaderSourceCommand_Execute, 0xad6320)
{
    auto orig = ctx.Original<ExecuteFn>(0xad6320);
    std::mt19937 gen(0xad6320);
    for (int i = 0; i < 80; ++i) {
        World w(gen);
        w.AsMeta();
        w.SaneFields();
        RandomSource(w);
        gPositionalCount = static_cast<int32_t>(gen() % 3) - 1 + (gen() % 9 == 0 ? 0x7fffffff : 0);
        w.argv[0] = kSourceTexts[gen() % std::size(kSourceTexts)];
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<sg::cShaderSourceCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

DIFF_TEST(ShaderSourceCommand_Open, 0xad8b30)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const char *)>(0xad8b30);
    LineGlobals globals(ctx);
    std::mt19937 gen(0xad8b30);
    const char *const rests[] = {"", "   ", "\t", "x", "  more words", "\t a"};
    for (int i = 0; i < 24; ++i) {
        World w(gen);
        w.AsMeta();
        const char *rest = rests[i % std::size(rests)];
        char what[64];
        std::snprintf(what, sizeof(what), "Open case %d", i);
        Compare(ctx, w, what, [&] { orig(w.cmd, rest); }, [&] { reinterpret_cast<sg::cShaderSourceCommand *>(w.cmd)->Open(rest); });
    }
}

DIFF_TEST(ShaderSourceCommand_HandleLine, 0xad9400)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t)>(0xad9400);
    LineGlobals globals(ctx);
    std::mt19937 gen(0xad9400);
    const char *const lines[] = {"endShaderSource",   "  endShaderSource  x", "endShaderSourceX", "endShader",
                                 "__parseCommand x",  "__parseCommand",       "__parseCommandX y", " \t__parseCommand",
                                 "float4 c = 1;",     "",                     "   ",                 "EndShaderSource"};
    const char *const expanded[] = {"", "x = $y;", "a longer expanded line, past the small buffer"};
    for (int i = 0; i < 160; ++i) {
        World w(gen);
        w.AsMeta();
        w.SaneFields();
        RandomSource(w);
        const char *text = lines[gen() % std::size(lines)];
        const char *data = expanded[gen() % std::size(expanded)];
        w.lineVt.ret[5] = reinterpret_cast<uint32_t>(text);
        w.lineVt.ret[6] = reinterpret_cast<uint32_t>(data);
        w.lineVt.ret[7] = static_cast<uint32_t>(std::strlen(data));
        // The flag is a byte (al): 0x80 runs the line, 0x100 does not.
        const uint32_t actives[] = {0, 1, 0x80, 0x100, 0xffffff01u};
        uint32_t active = actives[gen() % std::size(actives)];
        char what[64];
        std::snprintf(what, sizeof(what), "HandleLine case %d", i);
        Compare(
            ctx, w, what, [&] { return orig(w.cmd, w.line, active) & 0xff; },
            [&] {
                return static_cast<uint32_t>(
                    reinterpret_cast<sg::cShaderSourceCommand *>(w.cmd)->HandleLine(reinterpret_cast<cIGZString *>(w.line), static_cast<uint8_t>(active)));
            });
    }
}

// ---- shader ------------------------------------------------------------------------------------------------------------

DIFF_TEST(ShaderCommand_Execute, 0xad5a60)
{
    auto orig = ctx.Original<ExecuteFn>(0xad5a60);
    Stands stands;
    std::mt19937 gen(0xad5a60);
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        const int32_t blocks[] = {2, 6, 2, 6, 0, 1, 3, 4, 5};
        w.Put(0x1d0, blocks[gen() % std::size(blocks)]);
        w.state[0x269] = gen() % 6 == 0;
        w.argsVt.ret[8] = gen() % 6 == 0 ? 1 + gen() % 3 : 0;        // positional arguments (none allowed)
        w.argsVt.ret[3] = gen() % 2 ? reinterpret_cast<uint32_t>(w.switchArgs) : 0; // -layer
        w.argsVt.ret[1] = gen() % 3 == 0 ? (gen() % 2 ? 1 : 0x100) : 0;           // -ignoreDefaultState (al)
        w.faceVt.ret[32] = gen();
        w.Put(0x1cc, gen());
        w.withDefaultStates = true;
        w.defaultStates.clear();
        size_t n = gen() % 3 == 0 ? 0 : gen() % 18;
        for (size_t k = 0; k < n; ++k) w.defaultStates.push_back(w.Object(kDefaultState));
        w.defaultCapacity = n + gen() % 3;
        gClassObject = w.Object(kShader, false);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<sg::cShaderCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

DIFF_TEST(ShaderCommand_EndBlock, 0xad9e00)
{
    auto orig = ctx.Original<VoidFn>(0xad9e00);
    Stands stands;
    std::mt19937 gen(0xad9e00);
    for (int i = 0; i < 120; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        w.PutObject(0x1fc, kShader, false); // the shader the block built (slot 21 is called on it in block 4)
        if (gen() % 4 == 0) w.PutObject(0x1fc, kShader);
        w.Put(0x1d0, gen() % 3 == 0 ? gen() % 9 : 4);
        if (w.State()->mBlock == 4) w.PutObject(0x1fc, kShader, false);
        gClassObject = w.Object(kValidator, false);
        w.kindVt[kShader].ret[21] = gen();
        gServiceQueryResult = gen();
        w.kindVt[kValidator].ret[8] = gen() % 2 ? 0 : (gen() % 2 ? 1 : 0x100 | (gen() & 0xff));
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Compare(ctx, w, what, [&] { orig(w.cmd); }, [&] { reinterpret_cast<sg::cShaderCommand *>(w.cmd)->EndBlock(); });
    }
}
