// Differential test for nGZCommandParser::cEndDefCommand::Execute (0xb012c0; src/gz/CommandParser.cpp), on the command
// fixture of tests/command_parser_env.h (recorded calls, compared bytes, thrown cErrors compared by RunGuarded).
//
// enddef checks its argument count (slot 7) and that a definition is being recorded (state+0x15c; 0xafc390 throws
// otherwise), calls slot 18 of the state's virtual base, gives back the recorded body's spare capacity (a std::vector of
// bytes at +0x50 of the definition), inserts the definition into the state's sorted definitions (a vector of held
// pointers at +0x6c..+0x74; the game's insert 0xafd3c0 runs on both sides) before any of the same name, drops the
// recording and echoes "enddef" when the flag at +0xed is set.
//
// The definitions carry the fixture's item vtable: slot 2 (reference) and slot 1 (release) are recorded. Each run starts
// from fresh heap blocks for the definitions vector and the body (both may be reallocated and freed by the command); after
// the run their contents are read into the data the fakes saw (the definitions as pool indexes, sizes and capacities, the
// body's bytes), the blocks are freed and the pointers in the state cleared, so the state bytes compare without heap
// addresses.
#include "command_parser_env.h"

#include <string>

namespace {

struct Definition {
    void *vtable;            // +0
    uint32_t word4;          // +4
    std::string name;        // +8 (0x18 bytes)
    uint8_t middle[0x30];    // +0x20 (the parameter list; not read by enddef)
    std::vector<char> body;  // +0x50
};
static_assert(offsetof(Definition, body) == 0x50, "definition layout");

// In the name-table order: "x", "ba" before "ab" (the last bytes compare first), "set", "alpha", "longer1".
const char *const kPool[] = {"x", "ba", "ab", "set", "alpha", "longer1"};
constexpr int kPoolSize = 6;
// Names of the recorded definition: pool names (it goes before the existing one), near misses, the empty name.
const char *const kNames[] = {"x", "ba", "ab", "set", "alpha", "longer1", "y", "aa", "sex", "", "alphA", "longer2", "bb",
                              "\xe9", "b\xe9"}; // bytes above 0x7f sort as negative chars

Definition gDefs[kPoolSize + 1]; // the last one is the recorded definition
int gInitial[kPoolSize];
int gInitialCount = 0;
size_t gCapacity = 0;
size_t gBodySize = 0, gBodyCapacity = 0;

uint8_t PoolIndex(void *p)
{
    for (int k = 0; k <= kPoolSize; ++k)
        if (p == &gDefs[k]) return static_cast<uint8_t>(k);
    return 0xff;
}

void PushWord(uint32_t w)
{
    gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&w), reinterpret_cast<uint8_t *>(&w) + 4);
}

} // namespace

DIFF_TEST(cEndDefCommand_Execute, 0xb012c0)
{
    ExecuteTest<cp::cEndDefCommand>(
        ctx, 0xb012c0, "cEndDefCommand::Execute", &cp::cEndDefCommand::Execute,
        [](Env &e) {
            for (int i = 0; i <= kPoolSize; ++i) {
                gDefs[i].vtable = e.itemVt.slots;
                gDefs[i].word4 = e.gen();
                gDefs[i].name = i < kPoolSize ? kPool[i] : kNames[e.gen() % (sizeof(kNames) / sizeof(kNames[0]))];
            }
            e.itemVt.Set<1>(0);
            e.itemVt.Set<2>(0);
            gInitialCount = 0;
            for (int i = 0; i < kPoolSize; ++i)
                if (e.gen() % 3 != 0) gInitial[gInitialCount++] = i;
            gCapacity = gInitialCount + (e.gen() % 2 ? 0 : 1 + e.gen() % 2);
            // Body sizes: empty, small, around the 4 KB aligned-allocation edge; capacity equal or larger (or zero).
            switch (e.gen() % 6) {
            case 0: gBodySize = 0; break;
            case 1: gBodySize = 0xfff + e.gen() % 3; break;
            case 2: gBodySize = 0x1000 + e.gen() % 0x100; break;
            default: gBodySize = e.gen() % 64; break;
            }
            gBodyCapacity = e.gen() % 3 == 0 ? gBodySize : gBodySize + 1 + e.gen() % 0x20;
            if (gBodyCapacity == 0 && e.gen() % 2) gBodyCapacity = 0;
            // The argument count (slot 7): mostly 1.
            e.argsVt.Set<7>(0);
            e.argsVt.ret[7] = e.gen() % 6 == 0 ? e.gen() % 3 * 2 : 1;
            e.outVt.Set<14>(1);
            e.SetUpVirtualBase();
            e.scopeVt.Set<18>(0);
            bool recording = e.gen() % 7 != 0;
            *reinterpret_cast<void **>(e.state + 0x15c) = recording ? &gDefs[kPoolSize] : nullptr;
            e.state[0xed] = e.gen() % 2 ? 0 : static_cast<uint8_t>(1 + e.gen() % 255);
            uint8_t *state = e.state;
            Hooks h;
            h.before = [state] {
                void **block = static_cast<void **>(::operator new(gCapacity * sizeof(void *) + 4));
                for (int i = 0; i < gInitialCount; ++i) block[i] = &gDefs[gInitial[i]];
                *reinterpret_cast<void ***>(state + 0x6c) = block;
                *reinterpret_cast<void ***>(state + 0x70) = block + gInitialCount;
                *reinterpret_cast<void ***>(state + 0x74) = block + gCapacity;
                std::vector<char> &body = gDefs[kPoolSize].body;
                std::vector<char>().swap(body);
                body.reserve(gBodyCapacity);
                for (size_t i = 0; i < gBodySize; ++i) body.push_back(static_cast<char>('a' + i % 23));
            };
            h.after = [state] {
                void **&begin = *reinterpret_cast<void ***>(state + 0x6c);
                void **&end = *reinterpret_cast<void ***>(state + 0x70);
                void **&cap = *reinterpret_cast<void ***>(state + 0x74);
                PushWord(static_cast<uint32_t>(end - begin));
                PushWord(static_cast<uint32_t>(cap - begin));
                for (void **p = begin; p != end; ++p) gExtra.push_back(PoolIndex(*p));
                ::operator delete(begin);
                begin = end = cap = nullptr;
                std::vector<char> &body = gDefs[kPoolSize].body;
                PushWord(static_cast<uint32_t>(body.size()));
                PushWord(static_cast<uint32_t>(body.capacity()));
                gExtra.insert(gExtra.end(), body.begin(), body.end());
                std::vector<char>().swap(body);
            };
            return h;
        },
        800);
}
