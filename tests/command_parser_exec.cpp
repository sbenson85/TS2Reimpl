// Differential tests for the Execute slots of the nGZCommandParser commands (src/gz/CommandParser.h).
//
// Each command is run on an Env: a command object of random bytes (parser at +4, state at +0xc), a
// fake argument list, a fake parser, a parser state made of random bytes with a fake vtable at +0
// and a fake output object at state+0x14. Every call a command makes on them is recorded
// (tests/command_parser.h); the original and the replacement run from the same snapshot and must
// make the same calls with the same arguments and leave the same bytes behind.
#include "command_parser_env.h"


DIFF_TEST(cEvalCommand_Execute, 0xb01670)
{
    ExecuteTest<cp::cEvalCommand>(ctx, 0xb01670, "cEvalCommand::Execute", &cp::cEvalCommand::Execute,
                                  [](Env &e) { e.parserVt.Set<15>(1); });
}

DIFF_TEST(cIgnoreErrorsCommand_Execute, 0xb017d0)
{
    ExecuteTest<cp::cIgnoreErrorsCommand>(ctx, 0xb017d0, "cIgnoreErrorsCommand::Execute", &cp::cIgnoreErrorsCommand::Execute,
                                          [](Env &e) {
                                              e.parserVt.Set<30>(1);
                                              e.parserVt.ret[30] = e.RandomResult();
                                          });
}

DIFF_TEST(cIncludeCommand_Execute, 0xb01810)
{
    ExecuteTest<cp::cIncludeCommand>(ctx, 0xb01810, "cIncludeCommand::Execute", &cp::cIncludeCommand::Execute,
                                     [](Env &e) { e.parserVt.Set<12>(2); });
}

DIFF_TEST(cSincludeCommand_Execute, 0xb01ff0)
{
    ExecuteTest<cp::cSincludeCommand>(ctx, 0xb01ff0, "cSincludeCommand::Execute", &cp::cSincludeCommand::Execute,
                                      [](Env &e) { e.parserVt.Set<12>(2); });
}

DIFF_TEST(cTraceCommand_Execute, 0xb02020)
{
    ExecuteTest<cp::cTraceCommand>(ctx, 0xb02020, "cTraceCommand::Execute", &cp::cTraceCommand::Execute,
                                   [](Env &e) { e.outVt.Set<14>(1); });
}

DIFF_TEST(cSetCommand_Execute, 0xb01b00)
{
    ExecuteTest<cp::cSetCommand>(ctx, 0xb01b00, "cSetCommand::Execute", &cp::cSetCommand::Execute,
                                 [](Env &e) { e.stateVt.Set<1>(2); });
}

DIFF_TEST(cSetBCommand_Execute, 0xb019e0)
{
    ExecuteTest<cp::cSetBCommand>(ctx, 0xb019e0, "cSetBCommand::Execute", &cp::cSetBCommand::Execute, [](Env &e) {
        e.parserVt.Set<30>(1);
        e.parserVt.ret[30] = e.RandomResult();
        e.stateVt.Set<1>(2);
    });
}

namespace {

// Slot 3 of the argument list looks an option up by name: the answer is configured per name.
std::map<uint32_t, uint32_t> gKeyed;
uint32_t __fastcall KeyedSlot3(void *self, void *, uint32_t key, uint32_t n)
{
    Record(self, 3, key, n);
    auto it = gKeyed.find(key);
    return it == gKeyed.end() ? 0 : it->second;
}

} // namespace

DIFF_TEST(cDebugParserCommand_Execute, 0xb00480)
{
    // The six option names (RVAs of their strings) and the cell each one finds.
    const uint32_t keys[] = {0xe7eb68, 0xe7eb78, 0xe7eb14, 0xe7eb84, 0xe7eb08, 0xe7eb94};
    ExecuteTest<cp::cDebugParserCommand>(
        ctx, 0xb00480, "cDebugParserCommand::Execute", &cp::cDebugParserCommand::Execute,
        [&](Env &e) {
            e.argsVt.slots[3] = reinterpret_cast<void *>(&KeyedSlot3);
            e.parserVt.Set<30>(1);
            e.parserVt.ret[30] = e.RandomResult();
            gKeyed.clear();
            for (size_t k = 0; k < 6; ++k)
                if (e.gen() % 3 != 0) gKeyed[reinterpret_cast<uint32_t>(ctx.image.At(keys[k]))] = reinterpret_cast<uint32_t>(&e.argv[k]);
        },
        120);
}

// ---- commands that check their argument count and use the state ----------------------------------

namespace {

// A count for the argument list's slot 7: usually the right one for the command, sometimes not.
uint32_t CountNear(Env &e, uint32_t right)
{
    switch (e.gen() % 6) {
    case 0: return right + 1;
    case 1: return right - 1;
    case 2: return 0;
    case 3: return e.gen();
    default: return right;
    }
}

uint32_t gTextSeed = 0;
// Slot 4 of the argument list: the text of argument `index`.
uint32_t __fastcall ArgsText(void *self, void *, uint32_t index)
{
    Record(self, 4, index);
    return reinterpret_cast<uint32_t>(kStrings[(index + gTextSeed) % (sizeof(kStrings) / sizeof(kStrings[0]))]);
}

} // namespace

DIFF_TEST(cDebuggerBreakCommand_Execute, 0xb005b0)
{
    ExecuteTest<cp::cDebuggerBreakCommand>(ctx, 0xb005b0, "cDebuggerBreakCommand::Execute", &cp::cDebuggerBreakCommand::Execute,
                                           [](Env &e) {
                                               e.argsVt.Set<7>(0);
                                               e.argsVt.ret[7] = CountNear(e, 1);
                                           });
}

DIFF_TEST(cEndCommand_Execute, 0xb01260)
{
    ExecuteTest<cp::cEndCommand>(ctx, 0xb01260, "cEndCommand::Execute", &cp::cEndCommand::Execute, [](Env &e) {
        e.argsVt.Set<7>(0);
        e.argsVt.ret[7] = CountNear(e, 1);
        // The open blocks: itemList[0..k) with begin at +0x104 and end at +0x108.
        uint32_t k = e.gen() % 5;
        if (k > 4) k = 4;
        e.itemVt.Set<9>(0);
        *reinterpret_cast<uint32_t *>(e.state + 0x104) = reinterpret_cast<uint32_t>(e.itemList);
        *reinterpret_cast<uint32_t *>(e.state + 0x108) = reinterpret_cast<uint32_t>(e.itemList + k);
    });
}

DIFF_TEST(cListCommandsCommand_Execute, 0xb01840)
{
    ExecuteTest<cp::cListCommandsCommand>(ctx, 0xb01840, "cListCommandsCommand::Execute", &cp::cListCommandsCommand::Execute, [](Env &e) {
        e.argsVt.Set<7>(0);
        e.argsVt.ret[7] = CountNear(e, 1);
        e.itemVt.Set<5>(0);
        e.itemVt.Set<6>(1);
        // A circular list with n nodes after the sentinel (node: next, prev, ..., command at +0x10).
        static uint32_t nodes[5][8];
        uint32_t n = e.gen() % 4;
        for (uint32_t i = 0; i <= n; ++i) {
            nodes[i][0] = reinterpret_cast<uint32_t>(nodes[(i + 1) % (n + 1)]);
            nodes[i][1] = reinterpret_cast<uint32_t>(nodes[(i + n) % (n + 1)]);
            nodes[i][4] = i ? reinterpret_cast<uint32_t>(e.items[i - 1]) : 0;
        }
        *reinterpret_cast<uint32_t *>(e.state + 0x34) = reinterpret_cast<uint32_t>(nodes[0]);
        // The vector of commands at +0x16c (begin) and +0x170 (end).
        uint32_t m = e.gen() % 4;
        *reinterpret_cast<uint32_t *>(e.state + 0x16c) = reinterpret_cast<uint32_t>(e.itemList);
        *reinterpret_cast<uint32_t *>(e.state + 0x170) = reinterpret_cast<uint32_t>(e.itemList + m);
    });
}

DIFF_TEST(cPurgeCommand_Execute, 0xb01960)
{
    ExecuteTest<cp::cPurgeCommand>(ctx, 0xb01960, "cPurgeCommand::Execute", &cp::cPurgeCommand::Execute, [](Env &e) {
        static const char *const kScopes[] = {"scope:", ":", "", "x", "ab:", "a:b", "tail::", "q:"};
        for (auto &s : e.argv) s = kScopes[e.gen() % 8];
        e.SetUpVirtualBase();
        e.scopeVt.Set<22>(1);
        e.argsVt.Set<7>(0);
    });
}

DIFF_TEST(cNamespaceCommand_Execute, 0xb01910)
{
    ExecuteTest<cp::cNamespaceCommand>(ctx, 0xb01910, "cNamespaceCommand::Execute", &cp::cNamespaceCommand::Execute, [](Env &e) {
        e.SetUpVirtualBase();
        *reinterpret_cast<void **>(e.cmd + 0x40) = e.state;
        e.scopeVt.Set<16>(1);
        e.scopeVt.Set<20>(1);
    });
}

DIFF_TEST(cVersionCommand_Execute, 0xb021c0)
{
    ExecuteTest<cp::cVersionCommand>(
        ctx, 0xb021c0, "cVersionCommand::Execute", &cp::cVersionCommand::Execute,
        [](Env &e) {
            e.parserVt.Set<32>(1);
            int32_t lowest = static_cast<int32_t>(e.gen() % 20) - 10, highest = lowest + static_cast<int32_t>(e.gen() % 10);
            *reinterpret_cast<int32_t *>(e.state + 0x60) = lowest;
            *reinterpret_cast<int32_t *>(e.state + 0x64) = highest;
            e.state[0x5c] = e.gen() % 4 ? static_cast<uint8_t>(1 + e.gen() % 255) : 0;
            // Versions around the limits.
            int32_t pick[] = {lowest - 1, lowest, lowest + 1, highest - 1, highest, highest + 1, static_cast<int32_t>(e.gen())};
            e.parserVt.ret[32] = static_cast<uint32_t>(pick[e.gen() % 7]);
        },
        200);
}

DIFF_TEST(cShowArgumentsCommand_Execute, 0xb01f80)
{
    ExecuteTest<cp::cShowArgumentsCommand>(ctx, 0xb01f80, "cShowArgumentsCommand::Execute", &cp::cShowArgumentsCommand::Execute,
                                           [](Env &e) {
                                               e.argsVt.Set<7>(0);
                                               e.argsVt.slots[4] = reinterpret_cast<void *>(&ArgsText);
                                               gTextSeed = e.gen() % 7;
                                               uint32_t n = e.gen() % 5;
                                               e.argsVt.ret[7] = e.gen() % 12 == 0 ? static_cast<uint32_t>(-1 - e.gen() % 3) : n;
                                               e.outVt.Set<9>(1);
                                               e.outVt.Set<14>(1);
                                           });
}

DIFF_TEST(cAssertCommand_Execute, 0xaffda0)
{
    ExecuteTest<cp::cAssertCommand>(ctx, 0xaffda0, "cAssertCommand::Execute", &cp::cAssertCommand::Execute, [](Env &e) {
        e.parserVt.Set<30>(1);
        e.parserVt.ret[30] = e.RandomResult();
    });
}

namespace {

// The state's slot 1 takes a name and the text of a number: the 22 bytes at the text are taken.
uint32_t __fastcall StateSetCapture(void *self, void *, uint32_t name, uint32_t value)
{
    Record(self, 1, name, 0); // the buffer is on the stack: its address differs, its bytes are compared
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(value);
    gExtra.insert(gExtra.end(), bytes, bytes + 22);
    return 0;
}

} // namespace

DIFF_TEST(cSetICommand_Execute, 0xb01cc0)
{
    ExecuteTest<cp::cSetICommand>(
        ctx, 0xb01cc0, "cSetICommand::Execute", &cp::cSetICommand::Execute,
        [](Env &e) {
            e.parserVt.Set<32>(1);
            const int32_t values[] = {0, 1, -1, 9, 10, 99, 100, 12345, -12345, 2147483647, static_cast<int32_t>(0x80000000), 65536};
            e.parserVt.ret[32] = static_cast<uint32_t>(e.gen() % 4 == 0 ? static_cast<int32_t>(e.gen()) : values[e.gen() % 12]);
            e.stateVt.slots[1] = reinterpret_cast<void *>(&StateSetCapture);
        },
        120);
}

// ---- the conditional commands --------------------------------------------------------------------

DIFF_TEST(cElseCommand_Execute, 0xb00f20)
{
    ExecuteTest<cp::cElseCommand>(
        ctx, 0xb00f20, "cElseCommand::Execute", &cp::cElseCommand::Execute,
        [](Env &e) {
            e.argsVt.Set<7>(0);
            e.argsVt.ret[7] = CountNear(e, 1);
            e.outVt.Set<14>(1);
            return MakeCondHooks(e, e.gen, 1, 5, false);
        },
        300);
}

DIFF_TEST(cElseIfCommand_Execute, 0xb01050)
{
    ExecuteTest<cp::cElseIfCommand>(
        ctx, 0xb01050, "cElseIfCommand::Execute", &cp::cElseIfCommand::Execute,
        [](Env &e) {
            e.parserVt.Set<30>(1);
            e.parserVt.ret[30] = e.RandomResult();
            e.outVt.Set<14>(1);
            return MakeCondHooks(e, e.gen, 1, 5, false);
        },
        300);
}

DIFF_TEST(cIfCommand_Execute, 0xb016a0)
{
    ExecuteTest<cp::cIfCommand>(
        ctx, 0xb016a0, "cIfCommand::Execute", &cp::cIfCommand::Execute,
        [](Env &e) {
            *reinterpret_cast<void **>(e.cmd + 8) = e.state; // a meta command keeps its state at +8
            e.SetUpVirtualBase();
            e.scopeVt.Set<17>(1);
            e.scopeVt.Set<30>(1);
            e.scopeVt.ret[30] = e.RandomResult();
            e.outVt.Set<14>(1);
            return MakeCondHooks(e, e.gen, 1, 5, false);
        },
        300);
}

DIFF_TEST(cEndIfCommand_Execute, 0xb01590)
{
    ExecuteTest<cp::cEndIfCommand>(
        ctx, 0xb01590, "cEndIfCommand::Execute", &cp::cEndIfCommand::Execute,
        [](Env &e) {
            e.argsVt.Set<7>(0);
            e.argsVt.ret[7] = CountNear(e, 1);
            e.SetUpVirtualBase();
            e.scopeVt.Set<18>(0);
            e.outVt.Set<14>(1);
            return MakeCondHooks(e, e.gen, 1, 5, true);
        },
        300);
}

// ---- small slots that reach into the state ---------------------------------------------------------

DIFF_TEST(cEndIfCommand_NegatedBlockWord, 0xb02860)
{
    auto orig = ctx.Original<int32_t(__thiscall *)(void *)>(0xb02860);
    std::mt19937 gen(0xb02860);
    for (int i = 0; i < 60; ++i) {
        Env e(gen);
        // The word before the stack's end minus 8: a few values, including the extremes.
        static uint32_t words[8];
        for (auto &w : words) w = gen() % 3 == 0 ? gen() : static_cast<uint32_t>(static_cast<int32_t>(gen() % 7) - 3);
        words[0] = i == 0 ? 0x80000000u : words[0];
        *reinterpret_cast<uint32_t *>(e.state + 0x114) = reinterpret_cast<uint32_t>(words + 4);
        Differential(
            ctx, "cEndIfCommand::NegatedBlockWord", e.Regions(),
            [&] {
                Outcome o;
                o.ret = static_cast<uint32_t>(orig(e.cmd));
                return o;
            },
            [&] {
                Outcome o;
                o.ret = static_cast<uint32_t>(reinterpret_cast<cp::cEndIfCommand *>(e.cmd)->NegatedBlockWord());
                return o;
            });
    }
}

DIFF_TEST(cElseCommand_ReturnsMinusOne, 0x1b86e0)
{
    auto orig = ctx.Original<int32_t(__thiscall *)(void *)>(0x1b86e0);
    std::mt19937 gen(0x1b86e0);
    Env e(gen);
    ++ctx.cases;
    if (orig(e.cmd) != reinterpret_cast<cp::cElseCommand *>(e.cmd)->ReturnsMinusOne()) ctx.Fail("ReturnsMinusOne differs");
}

DIFF_TEST(cBlockCommand_EndBlock, 0x140fe0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x140fe0);
    std::mt19937 gen(0x140fe0);
    for (int i = 0; i < 20; ++i) {
        Env e(gen);
        Differential(
            ctx, "cBlockCommand::EndBlock", e.Regions(), [&] { return RunGuarded([&] { orig(e.cmd); }); },
            [&] { return RunGuarded([&] { reinterpret_cast<cp::cBlockCommandBase *>(e.cmd)->EndBlock(); }); });
    }
}

DIFF_TEST(cNamespaceCommand_EndBlock, 0xb03e30)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xb03e30);
    std::mt19937 gen(0xb03e30);
    for (int i = 0; i < 20; ++i) {
        Env e(gen);
        e.SetUpVirtualBase();
        *reinterpret_cast<void **>(e.cmd + 0x40) = e.state;
        e.scopeVt.Set<21>(0);
        e.scopeVt.ret[21] = gen();
        Differential(
            ctx, "cNamespaceCommand::EndBlock", e.Regions(),
            [&] {
                Outcome o;
                o.ret = orig(e.cmd);
                return o;
            },
            [&] {
                Outcome o;
                o.ret = reinterpret_cast<cp::cNamespaceCommand *>(e.cmd)->EndBlock();
                return o;
            });
    }
}

DIFF_TEST(cMetaCommandT_RegisterInScope, 0xb02350)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0xb02350);
    std::mt19937 gen(0xb02350);
    for (int i = 0; i < 20; ++i) {
        Env e(gen);
        e.SetUpVirtualBase();
        *reinterpret_cast<void **>(e.cmd + 8) = e.state;
        e.scopeVt.Set<17>(1);
        e.scopeVt.ret[17] = gen();
        void *unused = reinterpret_cast<void *>(gen());
        Differential(
            ctx, "cMetaCommandT::RegisterInScope", e.Regions(),
            [&] {
                Outcome o;
                o.ret = orig(e.cmd, unused);
                return o;
            },
            [&] {
                Outcome o;
                o.ret = reinterpret_cast<cp::cMetaCommandT *>(e.cmd)->RegisterInScope(unused);
                return o;
            });
    }
}
