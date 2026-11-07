// Differential tests for the material script's defaultState, pass and shaderProgram commands
// (src/gz/MaterialCommands.cpp). The world: tests/gz_material_commands.h.
#include "gz_material_commands.h"

#include <algorithm>

using namespace mattest;

namespace {

using SetContextFn = void(__thiscall *)(void *, void *, void *);
using ExecuteFn = void(__thiscall *)(void *, void *);
using VoidFn = void(__thiscall *)(void *);

template <class C> void SetContextTest(difftest::Context &ctx, uint32_t rva, const char *name, bool withSource)
{
    auto orig = ctx.Original<SetContextFn>(rva);
    Stands stands;
    std::mt19937 gen(rva);
    for (int i = 0; i < 30; ++i) {
        World w(gen);
        w.AsBlock();
        if (withSource) {
            // shaderProgram's slot 3 makes room for 4 KB of source: capacities on both sides of that.
            w.SaneFields();
            w.withSource = true;
            for (size_t k = 0, n = gen() % 30; k < n; ++k) w.source += static_cast<char>('a' + gen() % 26);
            const size_t capacities[] = {0, w.source.size(), 0xfff, 0x1000, 0x1001, 0x1800};
            w.sourceCapacity = std::max(w.source.size(), capacities[gen() % std::size(capacities)]);
        }
        // A null state (the interface null) only for the blocks that do not touch the state after the base call.
        void *iface = i % 5 == 4 && !withSource ? nullptr : w.state + 4;
        char what[96];
        std::snprintf(what, sizeof(what), "%s case %d", name, i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.parser, iface); },
            [&] { reinterpret_cast<C *>(w.cmd)->SetContext(w.parser, iface); });
    }
}

template <class C> void NameTest(difftest::Context &ctx, uint32_t rva, const char *expected)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(rva);
    uint8_t object[0x50] = {};
    const char *a = orig(object);
    const char *b = reinterpret_cast<C *>(object)->GetName();
    ++ctx.cases;
    if (a != b || std::strcmp(b, expected) != 0) ctx.Fail("GetName %08x: %p '%s' vs %p '%s'", rva, a, a, b, b);
}

// The argument list with switches answered by name.
void NamedSwitches(World &w)
{
    w.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsTakeSwitch);
    w.argsVt.slots[3] = reinterpret_cast<void *>(&ArgsTakeSwitchArgs);
    gSwitches.clear();
    gSwitchArgs.clear();
}

// A TakeSwitch answer: mostly 0 or 1, sometimes a word whose low byte (al, what the callers test) is 0.
uint32_t SwitchAnswer(std::mt19937 &gen) { return gen() % 2 ? 0 : (gen() % 4 == 0 ? 0x100 : 1); }

} // namespace

DIFF_TEST(MaterialCommands_SetContext2, 0xadab40)
{
    SetContextTest<sg::cDefaultStateCommand>(ctx, 0xadab40, "defaultState", false);
    SetContextTest<sg::cPassCommand>(ctx, 0xadb220, "pass", false);
    SetContextTest<sg::cShaderProgramCommand>(ctx, 0xadbdf0, "shaderProgram", true);
}

DIFF_TEST(MaterialCommands_Names2, 0xad95e0)
{
    NameTest<sg::cDefaultStateCommand>(ctx, 0xad95e0, "defaultState");
    NameTest<sg::cPassCommand>(ctx, 0xad9780, "pass");
    NameTest<sg::cShaderProgramCommand>(ctx, 0xad98f0, "shaderProgram");
}

// ---- defaultState ------------------------------------------------------------------------------------------------------

DIFF_TEST(DefaultStateCommand_Execute, 0xad27f0)
{
    auto orig = ctx.Original<ExecuteFn>(0xad27f0);
    Stands stands;
    std::mt19937 gen(0xad27f0);
    for (int i = 0; i < 160; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        w.Put(0x1d0, gen() % 6 == 0 ? 1 + gen() % 6 : 0);
        // The render type: inside the sixteen, at the edge, past it.
        const uint32_t types[] = {0, 1, 5, 14, 15, 16, 17, 0x80000000u, gen() % 16, gen() % 16};
        w.faceVt.ret[33] = types[gen() % std::size(types)];
        w.withDefaultStates = true;
        size_t n = gen() % 3 == 0 ? 0 : gen() % 18;
        for (size_t k = 0; k < n; ++k) w.defaultStates.push_back(w.Object(gen() % 2 ? kShader : kDefaultState));
        w.defaultCapacity = n + (gen() % 3 == 0 ? 0 : gen() % 6);
        gClassObject = w.Object(kShader);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<sg::cDefaultStateCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

DIFF_TEST(DefaultStateCommand_EndBlock, 0xad9a60)
{
    auto orig = ctx.Original<VoidFn>(0xad9a60);
    Stands stands;
    std::mt19937 gen(0xad9a60);
    for (int i = 0; i < 80; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        w.Put(0x1d0, gen() % 3 == 0 ? gen() % 8 : 6);
        if (w.State()->mBlock == 6) w.PutObject(0x1fc, kShader, false);
        w.kindVt[kShader].ret[24] = SwitchAnswer(gen); // the shader has its fallback state?
        gFoundOut = w.Object(kFallback, false);
        gClassObject = w.Object(kFallback, false);
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Compare(ctx, w, what, [&] { orig(w.cmd); }, [&] { reinterpret_cast<sg::cDefaultStateCommand *>(w.cmd)->EndBlock(); });
    }
}

// ---- pass --------------------------------------------------------------------------------------------------------------

namespace {
void RandomFormat(World &w)
{
    w.withFormat = true;
    size_t n = w.gen() % 3 == 0 ? 0 : w.gen() % 6;
    for (size_t k = 0; k < n; ++k) {
        sg::OutputVertexElement e{};
        e.a = w.gen();
        e.b = w.gen() % 16;
        e.c = static_cast<uint8_t>(w.gen());
        w.format.push_back(e);
    }
    w.formatCapacity = n + w.gen() % 3;
}
} // namespace

DIFF_TEST(PassCommand_Execute, 0xad4bc0)
{
    auto orig = ctx.Original<ExecuteFn>(0xad4bc0);
    Stands stands;
    const char *const switches[] = {"fixedFunction", "useGeomVertexDataState", "modifiedEachFrameHint", "renderEachFrame",
                                    "renderAlways", "clipAlways"};
    std::mt19937 gen(0xad4bc0);
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        NamedSwitches(w);
        w.Put(0x1d0, gen() % 5 == 0 ? gen() % 8 : 4);
        w.state[0x26a] = gen() % 6 == 0; // in a pass already
        w.PutObject(0x1fc, kShader, false);
        w.argsVt.ret[8] = gen() % 8 == 0 ? 1 + gen() % 2 : 0;
        w.Put(0x27c, gen() % 4 == 0 ? 0xffffffffu : gen() % 5);
        for (const char *s : switches)
            if (gen() % 3 == 0) gSwitches.push_back({s, SwitchAnswer(gen)});
        RandomFormat(w);
        gClassObject = w.Object(kOther);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<sg::cPassCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

DIFF_TEST(PassCommand_EndBlock, 0xad9bc0)
{
    auto orig = ctx.Original<VoidFn>(0xad9bc0);
    Stands stands;
    ProgramStands programStands;
    std::mt19937 gen(0xad9bc0);
    for (int i = 0; i < 240; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        w.Put(0x1d0, gen() % 5 == 0 ? gen() % 8 : 4);
        if (w.State()->mBlock == 4) w.PutObject(0x1fc, kShader, false);
        for (size_t off : {0x26d, 0x26e, 0x26f, 0x270}) w.state[off] = gen() % 2 ? 0 : (gen() % 3 == 0 ? 0x80 : 1);
        w.Put(0x280, gen() % 3 == 0 ? gen() % 8 : 0xffffffffu);
        w.Put(0x27c, gen() % 5);
        RandomFormat(w);
        gFormatOut = w.Object(kFormat);
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Compare(ctx, w, what, [&] { orig(w.cmd); }, [&] { reinterpret_cast<sg::cPassCommand *>(w.cmd)->EndBlock(); });
    }
}

// ---- shaderProgram -----------------------------------------------------------------------------------------------------

namespace {
void RandomProgramSource(World &w, bool big)
{
    w.withSource = true;
    w.source.clear();
    size_t n = w.gen() % 3 == 0 ? 0 : w.gen() % 40;
    for (size_t k = 0; k < n; ++k) w.source += static_cast<char>('a' + w.gen() % 26);
    const size_t capacities[] = {0, 0, 7, 70, 0xfff, 0x1000, 0x1001, 0x2400};
    size_t extra = big ? capacities[w.gen() % std::size(capacities)] : (w.gen() % 3 == 0 ? 0 : w.gen() % 70);
    w.sourceCapacity = std::max(w.source.size(), w.source.size() + extra);
    if (big && w.gen() % 2) w.sourceCapacity = std::max(w.source.size(), extra);
}
} // namespace

DIFF_TEST(ShaderProgramCommand_ResetSource, 0xad0310)
{
    auto orig = ctx.Original<VoidFn>(0xad0310);
    std::mt19937 gen(0xad0310);
    for (int i = 0; i < 80; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        RandomProgramSource(w, true);
        char what[64];
        std::snprintf(what, sizeof(what), "ResetSource case %d", i);
        Compare(ctx, w, what, [&] { orig(w.cmd); }, [&] { reinterpret_cast<sg::cShaderProgramCommand *>(w.cmd)->ResetSource(); });
    }
}

DIFF_TEST(ShaderProgramCommand_Execute, 0xad5c80)
{
    auto orig = ctx.Original<ExecuteFn>(0xad5c80);
    Stands stands;
    ProgramStands programStands;
    EnumTables tables(ctx);
    // Names the tables know (and one they do not: Lookup throws), as switch arguments.
    static const char *const targets[][1] = {{"v"}, {"px"}, {"pre"}, {"frag"}, {"weird"}, {"nope"}};
    static const char *const methods[][1] = {{"a"}, {"bb"}, {"ccc"}, {"dddd"}, {"eeeee"}, {"zz"}};
    static const char *const versions[][1] = {{"2_0"}, {""}, {"3_0_and_a_long_version_name"}};
    static const char *const paths[][1] = {{"dump/"}, {""}, {"C:/a/longer/path/name/"}};
    std::mt19937 gen(0xad5c80);
    for (int i = 0; i < 400; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        NamedSwitches(w);
        const int32_t blocks[] = {0, 4, 4, 4, 0, 2, 5};
        w.Put(0x1d0, blocks[gen() % std::size(blocks)]);
        w.argsVt.ret[8] = gen() % 10 == 0 ? 1 : 0;
        for (size_t off : {0x26a, 0x26d, 0x26e, 0x26f, 0x270}) w.state[off] = gen() % 3 == 0 ? (gen() % 2 ? 1 : 0x80) : 0;
        w.state[0x26a] = gen() % 5 == 0 ? 0 : 1; // in a pass, mostly
        w.Put(0x280, gen() % 5 == 0 ? gen() % 8 : 0xffffffffu);
        w.Put(0x278, gen() % 5);
        if (gen() % 3) gSwitchArgs.push_back({"target", targets[gen() % 5 + (gen() % 12 == 0)]});
        if (gen() % 4 == 0) gSwitches.push_back({"preprocess", SwitchAnswer(gen)});
        if (gen() % 2) gSwitchArgs.push_back({"method", methods[gen() % 5 + (gen() % 12 == 0)]});
        if (gen() % 4) gSwitchArgs.push_back({"version", versions[gen() % std::size(versions)]});
        if (gen() % 3 == 0) gSwitches.push_back({"noOpt", SwitchAnswer(gen)});
        if (gen() % 3 == 0) gSwitchArgs.push_back({"dumpSource", paths[gen() % std::size(paths)]});
        if (gen() % 3 == 0) gSwitchArgs.push_back({"disassemble", paths[gen() % std::size(paths)]});
        const char *const startTexts[] = {"", "vs_9", "x"};
        for (size_t off : World::kStrings) w.PutString(off, startTexts[gen() % std::size(startTexts)]);
        RandomProgramSource(w, true);
        w.after = [&] { w.CaptureStrings(); };
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<sg::cShaderProgramCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

namespace {
// Constants for the binding loop: the table's own (bound), the same names with one field off (not bound), an export_
// the table does not know (throws), and others.
void RandomDescs(difftest::Context &ctx, World &w)
{
    gDescs.clear();
    const sg::AutoBoundConstant *table = ctx.image.At<sg::AutoBoundConstant>(0xe78f18);
    static const char *const others[] = {"gColor", "export_unknownThing", "export_", "expor", "", "exportX"};
    size_t n = w.gen() % 6;
    for (size_t k = 0; k < n; ++k) {
        const sg::AutoBoundConstant &e = table[w.gen() % 9];
        sg::ProgramConstantDesc d{};
        d.name = e.name;
        d.registerSet = e.registerSet;
        d.registerIndex = w.gen() % 64;
        d.registerCount = 1 + w.gen() % 4;
        d.parameterClass = e.parameterClass;
        d.type = e.type;
        d.rows = e.rows;
        d.columns = e.columns;
        d.elements = e.elements;
        d.structMembers = w.gen() % 2;
        d.bytes = e.bytes;
        d.defaultValue = nullptr;
        switch (w.gen() % 12) {
        case 0: d.registerSet ^= 1; break;
        case 1: d.parameterClass ^= 1; break;
        case 2: d.type ^= 1; break;
        case 3: d.rows ^= 1; break;
        case 4: d.columns ^= 1; break;
        case 5: d.elements ^= 1; break;
        case 6: d.bytes ^= 1; break;
        case 7: d.name = others[w.gen() % std::size(others)]; break;
        default: break;
        }
        gDescs.push_back(d);
    }
}
} // namespace

DIFF_TEST(ShaderProgramCommand_EndBlock, 0xad9fb0)
{
    auto orig = ctx.Original<VoidFn>(0xad9fb0);
    Stands stands;
    ProgramStands programStands;
    std::mt19937 gen(0xad9fb0);
    for (int i = 0; i < 800; ++i) {
        World w(gen);
        w.AsBlock();
        w.SaneFields();
        const int32_t blocks[] = {0, 4, 4, 4, 0, 2, 6};
        w.Put(0x1d0, blocks[gen() % std::size(blocks)]);
        w.PutObject(0x1fc, kShader, false);
        const int32_t targetsList[] = {0, 1, 2, 3, 5};
        const int32_t methodsList[] = {0, 1, 2, 2, 3, 3, 6};
        w.Put(0x274, targetsList[gen() % std::size(targetsList)]); // the target
        w.Put(0x278, methodsList[gen() % std::size(methodsList)]); // the compile method
        w.state[0x271] = gen() % 2 ? 0 : (gen() % 2 ? 1 : 0x40); // optimize
        w.state[0x272] = gen() % 2 ? 0 : (gen() % 2 ? 1 : 0x40);
        w.Put(0x27c, gen() % 4);
        const char *const versions[] = {"", "vs_2_0", "ps_3_0"};
        const char *const dumps[] = {"", "", "d.txt", "dump/p"};
        const char *const listings[] = {"", "", "a.asm"};
        w.PutString(0x208, versions[gen() % std::size(versions)]);
        w.PutString(0x22c, dumps[gen() % std::size(dumps)]);
        w.PutString(0x244, listings[gen() % std::size(listings)]);
        RandomProgramSource(w, gen() % 3 == 0);
        // Fragment linking (method 3) wants no source, the others some: mostly what they want.
        if (gen() % 4) {
            if (w.State()->mCompileMethod == 3)
                w.source.clear();
            else if (w.source.empty())
                w.source = "src";
            w.sourceCapacity = std::max(w.sourceCapacity, w.source.size());
        }
        w.after = [&] { w.CaptureStrings(); };
        // The compiler's answers: 0 compiled, 1 errors, 2 failed, others.
        const uint32_t results[] = {0, 0, 0, 1, 2, 3, 0x100};
        for (int s : {3, 4, 6, 9}) w.compilerVt.ret[s] = results[gen() % std::size(results)];
        w.compilerVt.ret[11] = SwitchAnswer(gen); // disassembled?
        gWriteResult = gen() % 6 == 0 ? (gen() % 2 ? 0 : 0x100) : 1;
        gServiceQueryResult = gen();
        gProgramOut = w.Object(kProgram);
        gConstantsOut = w.Object(kConstants);
        RandomDescs(ctx, w);
        w.kindVt[kConstants].ret[3] = static_cast<uint32_t>(gDescs.size());
        w.kindVt[kShader].ret[22] = SwitchAnswer(gen);
        w.kindVt[kShader].ret[23] = SwitchAnswer(gen);
        gFoundOut = w.Object(kBinder, false);
        gClassObject = w.Object(kBinder, false);
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Compare(ctx, w, what, [&] { orig(w.cmd); }, [&] { reinterpret_cast<sg::cShaderProgramCommand *>(w.cmd)->EndBlock(); });
    }
}

// 0xace700 (__cdecl, varargs): ThrowIfNull's twin with the buffer 0x10f73c8.
DIFF_TEST(MaterialCommands_ThrowIfNull, 0xace700)
{
    using Fn = void(__cdecl *)(uint32_t, const char *, ...);
    auto orig = ctx.Original<Fn>(0xace700);
    const uint32_t words[] = {0, 1, 0x100, 0xff000000, 0x12345600};
    struct Case {
        const char *fmt;
        uint32_t a, b;
    };
    static const char kLong[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const Case cases[] = {
        {"plain", 0, 0}, {"", 0, 0}, {"%s", reinterpret_cast<uint32_t>("hello"), 0}, {"%s %d", reinterpret_cast<uint32_t>("n"), 42},
        {"%s", reinterpret_cast<uint32_t>("100%%"), 0}, {"%x:%u", 0xabc, 7}, {"%s%s", reinterpret_cast<uint32_t>(kLong), reinterpret_cast<uint32_t>(kLong)},
        {"%s", reinterpret_cast<uint32_t>("v%.0s%.0s%.0s%.0s%x"), 0},
    };
    uint8_t *buffer = ctx.image.At<uint8_t>(0x10f73c8);
    for (uint32_t word : words)
        for (const Case &c : cases) {
            for (int b = 0; b < 0x800; ++b) buffer[b] = static_cast<uint8_t>(0x41 + b % 26);
            char what[96];
            std::snprintf(what, sizeof(what), "ThrowIfNull %x '%s'", word, c.fmt);
            cptest::Differential(
                ctx, what, {{buffer, 0x800}}, [&] { return cptest::RunGuarded([&] { orig(word, c.fmt, c.a, c.b); }); },
                [&] { return cptest::RunGuarded([&] { reinterpret_cast<Fn>(&sg::ThrowIfNullMaterial)(word, c.fmt, c.a, c.b); }); });
        }
}
