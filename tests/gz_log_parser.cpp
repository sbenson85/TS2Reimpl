// Differential tests for cGZLogParser (gz/cGZLogParser.h, lane HP): the constructor 0xcac2b0, the destructor 0xcac4c0
// and RegisterCommands 0xcaf620. The parser's virtual calls go to recorders (tests/gz_log_parser_fakes.h): the game's
// cBaseParser face vtables patched for the constructor (both sides' parsers, the original's built by the original
// constructor chain and ours by ours, call them), a fake parser for the other two.
#include "difftest.h"
#include "free_recorder.h"
#include "gz_base_parser_form.h"
#include "gz_log_parser_fakes.h"
#include "gz/cGZLogParser.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

using nGZCommandParser::cBaseParser;
using nGZCommandParser::cGZLogParser;
using nGZCommandParser::cGZLogParserAdapters;
using nGZCommandParser::cGZLogParserState;
using namespace logfakes;

// From junk: the state (id, empty name), the parser (ParserForm), and the calls: AddRef of the new parser, slot 0
// with 0, slot 3 with the state, then RegisterCommands' 13 (slot 3, 11 commands, slot 3 with null).
DIFF_TEST(cGZLogParser_Construct, 0xcac2b0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xcac2b0);
    PatchedParserVTables patched(ctx);
    std::mt19937 gen(0xcac2b0);
    for (int round = 0; round < 10; ++round) {
        alignas(8) uint8_t a[sizeof(cGZLogParser)], b[sizeof(cGZLogParser)];
        for (auto &c : a) c = static_cast<uint8_t>(gen());
        std::memcpy(b, a, sizeof(a));
        gCalls.clear();
        void *ra = orig(a);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        void *rb = cGZLogParserAdapters::Construct(b, nullptr);
        std::vector<Call> callsB = gCalls;
        auto *la = reinterpret_cast<cGZLogParser *>(a);
        auto *lb = reinterpret_cast<cGZLogParser *>(b);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("returned %p / %p, expected this", ra, rb);
        if (!la->mParser || !lb->mParser || !la->mState || !lb->mState) {
            ctx.Fail("round %d: parser %p / %p, state %p / %p", round, la->mParser, lb->mParser, la->mState, lb->mState);
            continue;
        }
        if (callsA.size() != 16) ctx.Fail("round %d: the original made %zu calls (16)", round, callsA.size());
        if (Normal(callsA, la->mParser, la->mState) != Normal(callsB, lb->mParser, lb->mState))
            ctx.Fail("round %d: the parser calls differ (%zu / %zu)", round, callsA.size(), callsB.size());
        if (StateForm(la->mState) != StateForm(lb->mState)) ctx.Fail("round %d: the states differ", round);
        // Two heap blocks: the bytes the constructor leaves alone differ, so they are left out.
        const auto pa = parserform::ParserForm(reinterpret_cast<const uint8_t *>(la->mParser), sizeof(cBaseParser), true);
        const auto pb = parserform::ParserForm(reinterpret_cast<const uint8_t *>(lb->mParser), sizeof(cBaseParser), true);
        if (pa != pb) ctx.Fail("round %d: the parsers differ (from byte %ld of the form)", round, parserform::FirstDiff(pa, pb));
        for (cGZLogParser *l : {la, lb}) {
            parserform::TearDownParser(*l->mParser);
            ::operator delete(l->mParser);
            delete l->mState;
        }
    }
}

// On a fake parser face: the state handed over, the 11 commands (vtable, zeroed parser, count and state), the state
// taken back. The state word is random or null (only passed on).
DIFF_TEST(cGZLogParser_RegisterCommands, 0xcaf620)
{
    auto orig = ctx.Original<void(__thiscall *)(cGZLogParser *, void *)>(0xcaf620);
    gImageBase = static_cast<uint32_t>(ctx.image.Base());
    std::mt19937 gen(0xcaf620);
    for (int round = 0; round < 20; ++round) {
        FakeParser fake;
        BuildFake(fake, gen);
        void *face = fake.bytes + 0x20;
        alignas(8) uint8_t raw[sizeof(cGZLogParser)];
        for (auto &c : raw) c = static_cast<uint8_t>(gen());
        if (round % 5 == 0) std::memset(raw + 4, 0, 4);
        auto *log = reinterpret_cast<cGZLogParser *>(raw);
        gCalls.clear();
        orig(log, face);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        log->RegisterCommands(face);
        std::vector<Call> callsB = gCalls;
        ++ctx.cases;
        if (callsA.size() != 13) ctx.Fail("round %d: the original made %zu calls (13)", round, callsA.size());
        if (Normal(callsA, fake.bytes, log->mState) != Normal(callsB, fake.bytes, log->mState))
            ctx.Fail("round %d: the calls differ (%zu / %zu)", round, callsA.size(), callsB.size());
    }
}

// The destructor on a fake parser and a state with a short name, a long name (a heap block), or no state: the calls
// (slot 1 on the parser face, then one Release), the reference cleared, and the blocks freed. (A null parser crashes
// the original: slot 1 is called without a check; the object always has one.)
DIFF_TEST(cGZLogParser_Destruct, 0xcac4c0)
{
    auto orig = ctx.Original<void(__thiscall *)(cGZLogParser *)>(0xcac4c0);
    std::mt19937 gen(0xcac4c0);
    for (int round = 0; round < 30; ++round) {
        FakeParser fake;
        BuildFake(fake, gen);
        const int kind = round % 3;
        const std::string name = kind == 1 ? std::string(1 + gen() % 15, 'x') : std::string(16 + gen() % 300, 'y');
        // Raw storage: the objects are only ever built and destroyed by the code under test.
        alignas(4) uint8_t raw[2][sizeof(cGZLogParser)];
        cGZLogParser *sides[2] = {reinterpret_cast<cGZLogParser *>(raw[0]), reinterpret_cast<cGZLogParser *>(raw[1])};
        size_t sizes[2][4] = {};
        size_t counts[2] = {};
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            cGZLogParser &l = *sides[side];
            l.mParser = reinterpret_cast<cBaseParser *>(fake.bytes);
            l.mState = nullptr;
            if (kind != 0) {
                l.mState = new cGZLogParserState;
                l.mState->mName = name;
            }
            const void *state = l.mState;
            gCalls.clear();
            gCalls.reserve(16); // the fakes log into it during the call: its growth must not count as a free
            difftest::FreeRecorder rec;
            if (side == 0) orig(&l);
            else cGZLogParserAdapters::Destruct(&l, nullptr);
            rec.Stop();
            counts[side] = rec.SortedSizes(sizes[side], 4);
            logs[side] = Normal(gCalls, fake.bytes, state);
            if (state && !rec.Freed(state)) ctx.Fail("round %d side %d: the state was not freed", round, side);
        }
        ++ctx.cases;
        if (logs[0].size() != 2 * kWordsPerCall) ctx.Fail("round %d: the original made %zu calls (2)", round, logs[0].size() / kWordsPerCall);
        if (logs[0] != logs[1]) ctx.Fail("round %d: the calls differ", round);
        if (sides[0]->mParser != nullptr || sides[1]->mParser != nullptr) ctx.Fail("round %d: the parser reference is kept", round);
        if (counts[0] != counts[1] || std::memcmp(sizes[0], sizes[1], sizeof(sizes[0])) != 0)
            ctx.Fail("round %d: %zu / %zu blocks freed, or other sizes", round, counts[0], counts[1]);
    }
}
