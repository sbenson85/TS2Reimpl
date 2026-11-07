// Differential test for nGZCommandParser::cSetECommand::Execute (0xb01b30; src/gz/CommandParser.cpp), on the
// command fixture of tests/command_parser_env.h: the same fakes, recorded calls and compared bytes as the other
// Execute tests, and thrown cErrors compared by RunGuarded.
//
// The command builds a local cArguments with the game's constructor, parses the choices (argument 2) into it, asks
// for them (at least one: an empty list throws from the game's 0xb0c650) and throws through ThrowUnless when the value
// (argument 1) is none of them, so both throw paths unwind through the replacement's frame and must destroy the local
// (the game's destructor frees its three vectors). The vectors live on the RZ allocator (RZMalloc 0x1be3b0, RZFree
// 0x1be3a1): for the test both are patched to the CRT's malloc and free, on both sides alike (each side calls the
// image's cArguments members). Blocks still allocated after a run would be a missed destruction: what the run left
// allocated minus freed is compared too, in blocks and bytes (difftest::AllocationCounter: this thread's heap, every
// route; the fakes' log is not counted). Lane MH: before, a count of the RZ allocator's blocks alone.
#define NOMINMAX // code_patch.h includes windows.h; command_parser.h uses std::max
#include "code_patch.h"
#include "command_parser_env.h"
#include "rz/RZMemory.h"

#include <cstdlib>
#include <optional>

namespace {

std::optional<difftest::AllocationCounter> gHeap; // over one run, from `before` to `after`

void *__cdecl TestRzMalloc(uint32_t bytes)
{
    return std::malloc(bytes ? bytes : 1);
}

void __cdecl TestRzFree(void *p)
{
    std::free(p);
}

// Choice lists: plain words, quoted words, parentheses, options ("-x"), the empty list (throws), an unclosed quote or
// parenthesis (the parser throws), and lists that hold the kStrings values the value argument is drawn from.
const char *const kChoices[] = {
    "alpha x",          "x",           "\"set up\" alpha", "(x y) alpha",   "",           "  ",
    "-x alpha",         "100% a\\b",   "\"unclosed",       "(open",         "close)",     "ThE end",
    "\"ThE end\" x",    "alpha alpha", "beta gamma",       "\"\" alpha",    "- x",        "a b c d e f g h i j",
};

} // namespace

DIFF_TEST(cSetECommand_Execute, 0xb01b30)
{
    // Ours too: cArguments (ours, standalone shape) allocates through RZMemory::Malloc / Free.
    shape::StandIn rzMalloc(0x1be3b0, &RZMemory::Malloc, &TestRzMalloc);
    shape::StandIn rzFree(0x1be3a1, &RZMemory::Free, &TestRzFree);
    ExecuteTest<cp::cSetECommand>(
        ctx, 0xb01b30, "cSetECommand::Execute", &cp::cSetECommand::Execute,
        [](Env &e) {
            e.parserVt.Set<43>(0);
            e.parserVt.ret[43] = e.RandomResult();
            e.stateVt.Set<1>(2);
            e.argv[2] = kChoices[e.gen() % (sizeof(kChoices) / sizeof(kChoices[0]))];
            Hooks h;
            h.before = [] {
                gHeap.reset();
                gHeap.emplace();
            };
            h.after = [] {
                gHeap->Stop();
                const int32_t left[2] = {static_cast<int32_t>(gHeap->Blocks()), static_cast<int32_t>(gHeap->Bytes())};
                gExtra.insert(gExtra.end(), reinterpret_cast<const uint8_t *>(left), reinterpret_cast<const uint8_t *>(left + 2));
            };
            return h;
        },
        600);
}
