// Differential test for nGZCommandParser::cUndefineCommand::Execute (0xb02060; src/gz/CommandParser.cpp), on the
// command fixture of tests/command_parser_env.h (recorded calls, compared bytes, thrown cErrors compared by
// RunGuarded).
//
// The parser state's definitions are a vector of pointers at state+0x6c / +0x70, sorted by the name (a std::string at
// +8 of each definition) in the name-table order: length first, then the bytes from the last one backwards. Each run
// starts from a fresh copy of the same sorted array (the command erases in place), and afterwards the array's
// contents (as indexes into the pool, 0xff for anything else) and the end pointer (in the state bytes) are compared.
// The definitions carry the fixture's item vtable: the element copy and the erase call their slots 1 and 2, which are
// recorded.
#include "command_parser_env.h"

#include <string>

namespace {

struct Definition {
    void *vtable;     // +0
    uint32_t word4;   // +4
    std::string name; // +8 (the game's std::string layout: 0x18 bytes)
};
static_assert(sizeof(std::string) == 0x18, "the game's std::string layout");

// In the name-table order: "x" (1), "ba" before "ab" (the last bytes compare first), "set", "alpha", "longer1".
const char *const kPool[] = {"x", "ba", "ab", "set", "alpha", "longer1"};
constexpr int kPoolSize = 6;

Definition gDefs[kPoolSize];
void *gInitial[kPoolSize];
int gInitialCount = 0;
void *gArray[kPoolSize + 1];

// Names looked up: every pool name, near misses, the empty name.
const char *const kNames[] = {"x", "ba", "ab", "set", "alpha", "longer1", "y", "aa", "sex", "", "alphA", "longer2"};

const char *const kCommandName = "undefine";

} // namespace

DIFF_TEST(cUndefineCommand_Execute, 0xb02060)
{
    ExecuteTest<cp::cUndefineCommand>(
        ctx, 0xb02060, "cUndefineCommand::Execute", &cp::cUndefineCommand::Execute,
        [](Env &e) {
            for (int i = 0; i < kPoolSize; ++i) {
                gDefs[i].vtable = e.itemVt.slots;
                gDefs[i].word4 = e.gen();
                gDefs[i].name = kPool[i];
            }
            e.itemVt.Set<1>(0);
            e.itemVt.Set<2>(0);
            // A sorted subset of the pool.
            gInitialCount = 0;
            for (int i = 0; i < kPoolSize; ++i)
                if (e.gen() % 3 != 0) gInitial[gInitialCount++] = &gDefs[i];
            e.argv[0] = kNames[e.gen() % (sizeof(kNames) / sizeof(kNames[0]))];
            // The option (-quiet) through slot 1, the command's name through slot 4.
            e.argsVt.Set<1>(1);
            e.argsVt.ret[1] = e.RandomResult();
            e.argsVt.Set<4>(1);
            e.argsVt.ret[4] = reinterpret_cast<uint32_t>(kCommandName);
            e.outVt.Set<14>(1);
            // Mostly no definition being recorded (begin == end at +0x11c / +0x120); echo flag at +0xed.
            if (e.gen() % 5 != 0) *reinterpret_cast<uint32_t *>(e.state + 0x120) = *reinterpret_cast<uint32_t *>(e.state + 0x11c);
            e.state[0xed] = e.gen() % 2 ? 0 : static_cast<uint8_t>(1 + e.gen() % 255);
            uint8_t *state = e.state;
            Hooks h;
            h.before = [state] {
                for (int i = 0; i < gInitialCount; ++i) gArray[i] = gInitial[i];
                gArray[gInitialCount] = nullptr;
                *reinterpret_cast<void ***>(state + 0x6c) = gArray;
                *reinterpret_cast<void ***>(state + 0x70) = gArray + gInitialCount;
            };
            h.after = [] {
                for (int i = 0; i < gInitialCount; ++i) {
                    uint8_t index = 0xff;
                    for (int k = 0; k < kPoolSize; ++k)
                        if (gArray[i] == &gDefs[k]) index = static_cast<uint8_t>(k);
                    gExtra.push_back(index);
                }
            };
            return h;
        },
        800);
}
