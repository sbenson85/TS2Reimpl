// Differential test for nGZCommandParser::cCreateCommand::Execute (0xaffe10; src/gz/CommandParser.cpp), on the command
// fixture of tests/command_parser_env.h (recorded calls, compared bytes, thrown cErrors compared by RunGuarded).
//
// create finds the definition by name (0xafd520, equal_range: the image's on the original's side, ours on ours, over the state's sorted definitions at +0x6c),
// pushes a frame on the state's definition stack (+0x11c: 0x34-byte frames of a held definition and a cArguments; the
// game's cArguments members and the frames' reallocating push 0xafc6c0 run on both sides), parses the arguments into
// the frame, checks their number against the definition's parameters, then runs the definition's lines (NUL-separated
// body at +0x50, a (number, depth) pair per line at +0x5c) through the parser's slot 15 after the state's variable
// expansion (0xb06920, stood in here on both sides: it logs the text and appends a marker), updating each line's depth,
// the state's file name (+0xc8) and line (+0xe0) on the way, and pops the frame. A cError from the lines (or the
// meta-command check at the end) is caught, the frame popped, and one error naming the definition thrown instead.
//
// Fakes: the parser's slot 15 logs the text (in the data the fakes saw, not as an address) and, from the case's script,
// sets the state's last line (+0xe4), moves the condition stack's end (+0x114) or the meta stack's end (+0xfc), or
// throws a cError. The output object's slot 14 logs its text the same way. The definitions carry the item vtable
// (slot 1 release, slot 2 reference, recorded) and their parameter lists a fake vtable of their own (slot 7: the count).
// Heap state the command may reallocate or free (the frames vector, the state's file name) is rebuilt before each run and
// read into the data the fakes saw after it (then freed and its words in the state cleared); the definitions' line
// tables are reset before each run and read after it. The cArguments vectors live on the RZ allocator, patched to the
// CRT's malloc and free for the test on both sides (as in command_parser_sete.cpp). What the run left allocated minus
// freed, from rebuilding the heap state to reading and freeing it, is compared in blocks and bytes
// (difftest::AllocationCounter: this thread's heap, every route; the fakes' log and gExtra are not counted). Lane MH:
// before, a count of the RZ allocator's blocks alone.
#define NOMINMAX
#include "code_patch.h"
#include "command_parser_env.h"
#include "gz/MacroDefinition.h"
#include "rz/RZMemory.h"

#include <cstdlib>
#include <optional>
#include <string>

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

void PushWord(uint32_t w)
{
    difftest::NotCounted bookkeeping;
    gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&w), reinterpret_cast<uint8_t *>(&w) + 4);
}
void PushText(const char *s)
{
    difftest::NotCounted bookkeeping;
    gExtra.insert(gExtra.end(), s, s + std::strlen(s) + 1);
}

struct Line {
    int32_t number;
    int32_t depth;
};

struct Definition {
    void *vtable;                 // +0 (item vtable)
    uint32_t word4;               // +4
    std::string name;             // +8
    void *paramsVtable;           // +0x20 the parameter list (its own fake vtable)
    uint8_t paramsRest[0x2c];     // +0x24
    std::vector<char> body;       // +0x50
    std::vector<Line> lines;      // +0x5c
    uint32_t word68;              // +0x68
    std::string file;             // +0x6c
    int32_t firstLine;            // +0x84
};
static_assert(offsetof(Definition, paramsVtable) == 0x20 && offsetof(Definition, body) == 0x50 &&
                  offsetof(Definition, lines) == 0x5c && offsetof(Definition, file) == 0x6c &&
                  offsetof(Definition, firstLine) == 0x84,
              "definition layout");

const char *const kPool[] = {"x", "ba", "ab", "longer1"}; // sorted in the name-table order
constexpr int kPoolSize = 4;
const char *const kNames[] = {"x", "ba", "ab", "longer1", "y", "aa", ""};
Definition gDefs[kPoolSize];
std::vector<Line> gInitialLines[kPoolSize];
void *gDefArray[kPoolSize];

const char *const kLineTexts[] = {"set a 1", "", "if $x", "a fairly long line that is heap allocated", "endif", "echo %s %d"};
const char *const kArgTexts[] = {"", "one", "one two", "one two three", "\"a b\" c", "(x y) z", "\"unclosed"};
const char *const kFiles[] = {"", "scripts/startup.txt", "a much longer file name that does not fit the small buffer.txt"};

CondEntry gCond[16]; // (CondEntry: tests/command_parser_env.h)

// The frames vector before each run: `gFrames` frames (a definition from the pool or none, empty arguments) in a block
// of `gFrameCapacity`. When the push reallocates, the old frames are copied (their definitions counted, slot 2) and
// destroyed (released, slot 1).
int gFrames = 0, gFrameCapacity = 0;
void *gFrameDefinitions[3];
std::string gInitialFile;

// The parser's script: one action per call to slot 15, from gScript (3 bits each).
uint8_t *gState = nullptr;
uint32_t gScript = 0;
int gCalls = 0;

uint32_t __fastcall OutText(void *self, void *, uint32_t text)
{
    Record(self, 14, 0);
    PushText(reinterpret_cast<const char *>(text));
    return 0;
}

uint32_t __fastcall ParserRun(void *self, void *, uint32_t text)
{
    Record(self, 15, 0);
    PushText(reinterpret_cast<const char *>(text));
    // The top frame's parsed arguments: the flag Parse stored (+0x15 of the cArguments at +4 of the frame).
    char *framesEnd = *reinterpret_cast<char **>(gState + 0x120);
    if (framesEnd != *reinterpret_cast<char **>(gState + 0x11c)) {
        difftest::NotCounted bookkeeping;
        gExtra.push_back(static_cast<uint8_t>(framesEnd[-0x34 + 4 + 0x15]));
    }
    uint32_t action = (gScript >> (3 * (gCalls % 10))) & 7;
    ++gCalls;
    switch (action) {
    case 1: *reinterpret_cast<int32_t *>(gState + 0xe4) = -1; break;
    case 2: *reinterpret_cast<int32_t *>(gState + 0xe4) = static_cast<int32_t>(gCalls % 4); break;
    case 3: {
        auto *&end = *reinterpret_cast<CondEntry **>(gState + 0x114);
        if (end < gCond + 15) ++end;
        break;
    }
    case 4: {
        auto *&end = *reinterpret_cast<CondEntry **>(gState + 0x114);
        if (end > gCond + 1) --end;
        break;
    }
    case 5: *reinterpret_cast<uint32_t *>(gState + 0xfc) += 4; break;
    case 6: cp::ThrowFormatted("line failed (%d)", gCalls); break;
    default: break;
    }
    return 0;
}

// 0xb06920 stood in: logs the text and the state, appends a marker.
void __fastcall StandExpand(void *state, void *, std::string *text)
{
    {
        difftest::NotCounted bookkeeping;
        gLog.push_back({99, 0, reinterpret_cast<uint32_t>(state), {0, 0, 0, 0}});
    }
    PushText(text->c_str());
    text->append("+e");
}

const char *const kCommandName = "create";

// The image's cArguments members (constructor, destructor, slot 7: the count), for the frames the fixture builds.
void ImageArgumentsConstruct(void *p) { reinterpret_cast<void(__thiscall *)(void *)>(GameAddress<void>(0xb0be60))(p); }
void ImageArgumentsDestroy(void *p) { reinterpret_cast<void(__thiscall *)(void *)>(GameAddress<void>(0xb0bf40))(p); }
uint32_t ImageArgumentsCount(void *p)
{
    return reinterpret_cast<uint32_t(__thiscall *)(void *)>((*static_cast<void ***>(p))[7])(p);
}

} // namespace

DIFF_TEST(cCreateCommand_Execute, 0xaffe10)
{
    // Ours too: cArguments (ours, standalone shape) allocates through RZMemory::Malloc / Free.
    shape::StandIn rzMalloc(0x1be3b0, &RZMemory::Malloc, &TestRzMalloc);
    shape::StandIn rzFree(0x1be3a1, &RZMemory::Free, &TestRzFree);
    shape::StandIn expand(0xb06920, &cp::cMacroParser::ExpandParameters, &StandExpand);
    static FakeVTable paramsVt{7};
    paramsVt.Set<7>(0);
    ExecuteTest<cp::cCreateCommand>(
        ctx, 0xaffe10, "cCreateCommand::Execute", &cp::cCreateCommand::Execute,
        [](Env &e) {
            uint32_t parameters = e.gen() % 4;
            paramsVt.ret[7] = parameters;
            for (int i = 0; i < kPoolSize; ++i) {
                Definition &d = gDefs[i];
                d.vtable = e.itemVt.slots;
                d.word4 = e.gen();
                d.name = kPool[i];
                d.paramsVtable = paramsVt.slots;
                int lines = static_cast<int>(e.gen() % 5);
                std::string body;
                gInitialLines[i].clear();
                int number = static_cast<int>(e.gen() % 50);
                for (int k = 0; k < lines; ++k) {
                    body += kLineTexts[e.gen() % (sizeof(kLineTexts) / sizeof(kLineTexts[0]))];
                    body += '\0';
                    static const int32_t kDepths[] = {-1000, -1, 0, 1, 2, 5};
                    gInitialLines[i].push_back({number, kDepths[e.gen() % 6]});
                    number += 1 + static_cast<int>(e.gen() % 3);
                }
                d.body.assign(body.begin(), body.end());
                d.file = kFiles[e.gen() % 3];
                d.firstLine = e.gen() % 3 == 0 ? 0 : static_cast<int32_t>(e.gen() % 20);
                gDefArray[i] = &d;
            }
            e.itemVt.Set<1>(0);
            e.itemVt.Set<2>(0);
            e.argv[0] = kNames[e.gen() % (sizeof(kNames) / sizeof(kNames[0]))];
            // Mostly as many arguments as parameters.
            uint32_t words = e.gen() % 4 == 0 ? e.gen() % 7 : parameters;
            static const char *const kByCount[] = {"", "one", "one two", "one two three"};
            e.argv[1] = words < 4 ? kByCount[words] : kArgTexts[words];
            e.argsVt.Set<4>(1);
            e.argsVt.ret[4] = reinterpret_cast<uint32_t>(kCommandName);
            e.outVt.slots[14] = reinterpret_cast<void *>(&OutText);
            e.outVt.Set<9>(1);
            e.parserVt.slots[15] = reinterpret_cast<void *>(&ParserRun);
            *reinterpret_cast<void **>(e.state + 0x6c) = gDefArray;
            *reinterpret_cast<void **>(e.state + 0x70) = gDefArray + kPoolSize;
            // The condition stack (1..4 entries; the parser may move its end), the meta stack's words (random: only
            // their difference is read), the last line, the trace flag.
            int conds = 1 + static_cast<int>(e.gen() % 4);
            for (auto &c : gCond) c = {static_cast<int32_t>(e.gen() % 3), static_cast<uint8_t>(e.gen() % 2), 0, 0, 0};
            *reinterpret_cast<CondEntry **>(e.state + 0x110) = gCond;
            *reinterpret_cast<CondEntry **>(e.state + 0x114) = gCond + conds;
            *reinterpret_cast<int32_t *>(e.state + 0xe4) = e.gen() % 2 ? -1 : static_cast<int32_t>(e.gen() % 6);
            e.state[0xee] = e.gen() % 2 ? 0 : static_cast<uint8_t>(1 + e.gen() % 255);
            gScript = e.gen() % 3 == 0 ? 0 : e.gen();
            gFrames = static_cast<int>(e.gen() % 3);
            gFrameCapacity = gFrames + (e.gen() % 2 ? 0 : 1);
            for (void *&d : gFrameDefinitions) d = e.gen() % 2 ? static_cast<void *>(&gDefs[e.gen() % kPoolSize]) : nullptr;
            gInitialFile = kFiles[e.gen() % 3];
            uint8_t *state = e.state;
            Hooks h;
            h.before = [state] {
                gState = state;
                gCalls = 0;
                for (int i = 0; i < kPoolSize; ++i) gDefs[i].lines = gInitialLines[i];
                gHeap.reset();
                gHeap.emplace(); // from here: the blocks this run builds are freed again in `after`
                char *block = static_cast<char *>(::operator new(gFrameCapacity * 0x34 + 4));
                for (int i = 0; i < gFrames; ++i) {
                    *reinterpret_cast<void **>(block + i * 0x34) = gFrameDefinitions[i];
                    ImageArgumentsConstruct(block + i * 0x34 + 4);
                }
                *reinterpret_cast<char **>(state + 0x11c) = block;
                *reinterpret_cast<char **>(state + 0x120) = block + gFrames * 0x34;
                *reinterpret_cast<char **>(state + 0x124) = block + gFrameCapacity * 0x34;
                ::new (state + 0xc8) std::string(gInitialFile);
            };
            h.after = [state] {
                char *&begin = *reinterpret_cast<char **>(state + 0x11c);
                char *&end = *reinterpret_cast<char **>(state + 0x120);
                char *&cap = *reinterpret_cast<char **>(state + 0x124);
                PushWord(static_cast<uint32_t>(end - begin));
                PushWord(static_cast<uint32_t>(cap - begin));
                for (char *f = begin; f != end; f += 0x34) {
                    void *def = *reinterpret_cast<void **>(f);
                    uint8_t index = 0xfe;
                    for (int k = 0; k < kPoolSize; ++k)
                        if (def == &gDefs[k]) index = static_cast<uint8_t>(k);
                    if (!def) index = 0xff;
                    {
                        difftest::NotCounted bookkeeping;
                        gExtra.push_back(index);
                    }
                    PushWord(ImageArgumentsCount(f + 4));
                    ImageArgumentsDestroy(f + 4);
                }
                ::operator delete(begin);
                begin = end = cap = nullptr;
                std::string &file = *reinterpret_cast<std::string *>(state + 0xc8);
                PushText(file.c_str());
                file.~basic_string();
                std::memset(state + 0xc8, 0, sizeof(std::string));
                for (int i = 0; i < kPoolSize; ++i)
                    for (const Line &l : gDefs[i].lines) {
                        PushWord(static_cast<uint32_t>(l.number));
                        PushWord(static_cast<uint32_t>(l.depth));
                    }
                gHeap->Stop();
                PushWord(static_cast<uint32_t>(gHeap->Blocks()));
                PushWord(static_cast<uint32_t>(gHeap->Bytes()));
            };
            return h;
        },
        1500);
}
