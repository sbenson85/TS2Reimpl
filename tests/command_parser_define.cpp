// Differential tests for nGZCommandParser::cDefineCommand (src/gz/MacroDefinition.cpp, lane NV): Execute (slot 4,
// 0xb005e0) and HandleLine (slot 10, 0xb038f0), on the command fixture of tests/command_parser_env.h (recorded calls,
// compared bytes, thrown cErrors compared by RunGuarded). The command is a meta command:
// its state is at +8 (the fixture's +0xc is not read).
//
// Execute: checks the switches (args slot 15), that no definition call is running (state +0x11c == +0x120), takes
// exactly two positional arguments (slot 11), looks the name up in the sorted definitions (+0x6c, lower_bound: the
// image's 0xafd790 on the original's side, ours on ours) and throws "Redefinition" for a name defined already, pushes
// itself as the meta block (slot 17 of the state's virtual base), echoes when +0xed is set, then makes the definition
// being recorded (+0x15c) a new one (the old one released) with the name, the parameters parsed, the file (+0xc8), the
// line (+0xe0) and the level (+0x158). The definitions in the vector and the old recording are fakes (item vtable:
// slot 1 release, slot 2 reference, recorded). After each run the new definition is read into the data the fakes saw
// (its vtable word, count, name, parameters through its own vtable's Count and Argument, body and line sizes, level,
// file, first line), destroyed, and +0x15c set back, so the state bytes compare without heap addresses; the state's
// file name (a std::string at +0xc8) is rebuilt before each run and read and cleared after it likewise.
//
// HandleLine: the line is a fake cIGZString (slot 5 the text, slot 7 its length, recorded). "enddef" as the first word
// returns `running` when running, else pops the meta block (slot 18) and returns false; another line with a word, when
// running, is appended to the recording's body (+0x50, with its zero) and line table (+0x5c: the line number, -1000),
// and echoed when +0xed is set. The recording's two vectors are rebuilt on fresh blocks before each run and read after
// it (sizes, capacities, elements). The word "enddef"'s length is a dword the game's static initialisers set
// (0x10749a0, which the harness leaves 0): the test sets it to 6 for its duration.
//
// Allocation: the image's operator new / delete are patched to the CRT's and the RZ allocator (cArguments' vectors) is
// stood in on both sides, so either side's blocks can be freed by the other's code (as in command_parser_create.cpp).
#define NOMINMAX
#include "code_patch.h"
#include "command_parser_env.h"
#include "gz/MacroDefinition.h"
#include "rz/RZMemory.h"

#include <cstdlib>
#include <string>

namespace {

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void *__cdecl TestRzMalloc(uint32_t bytes) { return std::malloc(bytes ? bytes : 1); }
void __cdecl TestRzFree(void *p) { std::free(p); }

struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::StandIn rzMalloc{0x1be3b0, &RZMemory::Malloc, &TestRzMalloc};
    shape::StandIn rzFree{0x1be3a1, &RZMemory::Free, &TestRzFree};
};

void PushWord(uint32_t w)
{
    difftest::NotCounted bookkeeping;
    gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&w), reinterpret_cast<uint8_t *>(&w) + 4);
}

void PushText(const char *s, size_t n)
{
    difftest::NotCounted bookkeeping;
    PushWord(static_cast<uint32_t>(n));
    gExtra.insert(gExtra.end(), s, s + n);
}

struct FakeDefinition {
    void *vtable;     // +0
    uint32_t count;   // +4
    std::string name; // +8
};

// In the name-table order.
const char *const kPool[] = {"x", "ba", "ab", "set", "alpha", "longer1", "averyveryverylongname"};
constexpr int kPoolSize = 7;
const char *const kNames[] = {"x", "ba", "ab", "set", "alpha", "longer1", "averyveryverylongname", "y", "aa", "sex", "", "alphA",
                              "bb", "\xe9", "a definition name past sixteen"};
const char *const kParameters[] = {"", "a", "a b", "first second third", "(x y) z", "a \"quoted words\" b", "-s x"};
const char *const kFiles[] = {"", "f.txt", "a file name longer than fifteen.txt"};

FakeDefinition gDefs[kPoolSize + 1]; // the last one: an old recording
void *gSorted[kPoolSize];
const char gCommandName[] = "define";

} // namespace

DIFF_TEST(cDefineCommand_Execute, 0xb005e0)
{
    Allocators allocators;
    static std::string fileName;
    static int sortedCount;
    ExecuteTest<cp::cDefineCommand>(
        ctx, 0xb005e0, "cDefineCommand::Execute", &cp::cDefineCommand::Execute,
        [](Env &e) {
            for (int i = 0; i <= kPoolSize; ++i) {
                gDefs[i].vtable = e.itemVt.slots;
                gDefs[i].count = e.gen();
                gDefs[i].name = i < kPoolSize ? kPool[i] : "old";
            }
            e.itemVt.Set<1>(0);
            e.itemVt.Set<2>(0);
            sortedCount = 0;
            for (int i = 0; i < kPoolSize; ++i)
                if (e.gen() % 3 != 0) gSorted[sortedCount++] = &gDefs[i];
            uint8_t *state = e.state;
            *reinterpret_cast<void **>(e.cmd + 8) = state; // a meta command: the state at +8
            *reinterpret_cast<void **>(state + 0x6c) = gSorted;
            *reinterpret_cast<void **>(state + 0x70) = gSorted + sortedCount;
            *reinterpret_cast<void **>(state + 0x74) = gSorted + kPoolSize;
            // No definition call running, mostly.
            if (e.gen() % 6 != 0) *reinterpret_cast<uint32_t *>(state + 0x120) = *reinterpret_cast<uint32_t *>(state + 0x11c);
            e.argv[0] = kNames[e.gen() % (sizeof(kNames) / sizeof(kNames[0]))];
            e.argv[1] = kParameters[e.gen() % (sizeof(kParameters) / sizeof(kParameters[0]))];
            e.argsVt.Set<4>(1);
            e.argsVt.ret[4] = reinterpret_cast<uint32_t>(gCommandName);
            e.outVt.Set<14>(1);
            e.SetUpVirtualBase();
            e.scopeVt.Set<17>(1);
            e.state[0xed] = e.gen() % 2 ? 0 : static_cast<uint8_t>(1 + e.gen() % 255);
            *reinterpret_cast<void **>(state + 0x15c) = e.gen() % 3 == 0 ? &gDefs[kPoolSize] : nullptr;
            fileName = kFiles[e.gen() % 3];
            Hooks h;
            h.before = [state] { new (state + 0xc8) std::string(fileName); };
            h.after = [state] {
                void *&recorded = *reinterpret_cast<void **>(state + 0x15c);
                if (recorded && recorded != &gDefs[kPoolSize]) {
                    auto *d = static_cast<cp::cMacroDefinition *>(recorded);
                    PushWord(reinterpret_cast<uint32_t *>(d)[0]);
                    PushWord(static_cast<uint32_t>(d->mRefCount));
                    PushText(d->mName.data(), d->mName.size());
                    auto *parameters = reinterpret_cast<cp::cIArguments *>(reinterpret_cast<char *>(d) + 0x20); // through its own vtable
                    const int32_t count = parameters->Count();
                    PushWord(static_cast<uint32_t>(count));
                    for (int32_t k = 0; k < count; ++k) {
                        const char *a = parameters->Argument(k);
                        PushText(a, std::strlen(a));
                    }
                    // The parse flags the parameter list keeps (cArguments +0x14 skipFirst, +0x15 caseSensitive).
                    PushWord(reinterpret_cast<uint8_t *>(d)[0x34] | reinterpret_cast<uint8_t *>(d)[0x35] << 8);
                    PushWord(static_cast<uint32_t>(d->mBody.size()));
                    PushWord(static_cast<uint32_t>(d->mLines.size()));
                    PushWord(static_cast<uint32_t>(d->mLevel));
                    PushText(d->mFile.data(), d->mFile.size());
                    PushWord(static_cast<uint32_t>(d->mFirstLine));
                    d->DeletingDestructor(1);
                    recorded = reinterpret_cast<void *>(0x4ec0de);
                }
                auto *file = reinterpret_cast<std::string *>(state + 0xc8);
                PushText(file->data(), file->size());
                file->~basic_string();
                std::memset(state + 0xc8, 0, sizeof(std::string));
            };
            return h;
        },
        1500);
}

namespace {

constexpr int kLineId = 40;
FakeVTable gLineVt{kLineId};
const char *const kLines[] = {"enddef", "  enddef", "\tenddef x", "enddefx", "endde", "ENDDEF", "", "   ", "set x 1", "\tfoo bar",
                              "end def", "enddef\r", "a line of the body longer than sixteen", "x", "  # comment", "enddef;"};

struct Recording {
    alignas(8) uint8_t block[cp::cMacroDefinition::kSize] = {};
    std::vector<char> bodyInitial;
    std::vector<cp::MacroLine> linesInitial;
    size_t bodyCapacity = 0, linesCapacity = 0;

    template <class T> T **Words(size_t offset) { return reinterpret_cast<T **>(block + offset); }

    void Before()
    {
        char *body = static_cast<char *>(::operator new(bodyCapacity + 4));
        std::copy(bodyInitial.begin(), bodyInitial.end(), body);
        Words<char>(0x50)[0] = body;
        Words<char>(0x50)[1] = body + bodyInitial.size();
        Words<char>(0x50)[2] = body + bodyCapacity;
        auto *lines = static_cast<cp::MacroLine *>(::operator new(linesCapacity * sizeof(cp::MacroLine) + 4));
        std::copy(linesInitial.begin(), linesInitial.end(), lines);
        Words<cp::MacroLine>(0x5c)[0] = lines;
        Words<cp::MacroLine>(0x5c)[1] = lines + linesInitial.size();
        Words<cp::MacroLine>(0x5c)[2] = lines + linesCapacity;
    }
    void After()
    {
        char **body = Words<char>(0x50);
        PushWord(static_cast<uint32_t>(body[1] - body[0]));
        PushWord(static_cast<uint32_t>(body[2] - body[0]));
        PushText(body[0], body[1] - body[0]);
        ::operator delete(body[0]);
        body[0] = body[1] = body[2] = nullptr;
        cp::MacroLine **lines = Words<cp::MacroLine>(0x5c);
        PushWord(static_cast<uint32_t>(lines[1] - lines[0]));
        PushWord(static_cast<uint32_t>(lines[2] - lines[0]));
        for (cp::MacroLine *p = lines[0]; p != lines[1]; ++p) {
            PushWord(static_cast<uint32_t>(p->number));
            PushWord(static_cast<uint32_t>(p->depth));
        }
        ::operator delete(lines[0]);
        lines[0] = lines[1] = lines[2] = nullptr;
    }
};

} // namespace

DIFF_TEST(cDefineCommand_HandleLine, 0xb038f0)
{
    Allocators allocators;
    uint32_t *wordLength = ctx.image.At<uint32_t>(0x10749a0);
    const uint32_t savedLength = *wordLength;
    *wordLength = 6;
    auto orig = ctx.Original<bool(__thiscall *)(void *, void *, uint8_t)>(0xb038f0);
    gLineVt.Set<5>(0);
    gLineVt.Set<7>(0);
    alignas(8) static uint8_t line[8];
    *reinterpret_cast<void **>(line) = gLineVt.slots;
    static Recording rec;
    std::mt19937 gen(0xb038f0);
    for (int i = 0; i < 2000; ++i) {
        Env e(gen);
        const char *text = kLines[gen() % (sizeof(kLines) / sizeof(kLines[0]))];
        gLineVt.ret[5] = reinterpret_cast<uint32_t>(text);
        gLineVt.ret[7] = static_cast<uint32_t>(std::strlen(text));
        const uint8_t running = gen() % 3 == 0 ? 0 : static_cast<uint8_t>(gen() % 4 == 0 ? 1 + gen() % 255 : 1);
        e.outVt.Set<14>(1);
        e.SetUpVirtualBase();
        e.scopeVt.Set<18>(0);
        *reinterpret_cast<void **>(e.cmd + 8) = e.state; // a meta command: the state at +8
        e.state[0xed] = gen() % 2 ? 0 : static_cast<uint8_t>(1 + gen() % 255);
        for (auto &b : rec.block) b = static_cast<uint8_t>(gen());
        *reinterpret_cast<void **>(e.state + 0x15c) = rec.block;
        rec.bodyInitial.clear();
        rec.linesInitial.clear();
        const size_t lines = gen() % 3 == 0 ? 0 : gen() % 6;
        for (size_t k = 0; k < lines; ++k) {
            rec.linesInitial.push_back({static_cast<int32_t>(gen() % 100), -1000});
            for (const char *c = "body\0"; ; ++c) {
                rec.bodyInitial.push_back(*c);
                if (!*c) break;
            }
        }
        rec.bodyCapacity = rec.bodyInitial.size() + (gen() % 2 ? 0 : gen() % 50);
        rec.linesCapacity = lines + (gen() % 2 ? 0 : gen() % 3);
        char what[160];
        std::snprintf(what, sizeof(what), "HandleLine case %d '%s' running %u", i, Printable(text).c_str(), running);
        Differential(
            ctx, what, {e.Regions()[0], e.Regions()[1], e.Regions()[2], {rec.block, sizeof(rec.block)}},
            [&] { return RunGuarded([&] { return orig(e.cmd, line, running); }); },
            [&] { return RunGuarded([&] { return reinterpret_cast<cp::cDefineCommand *>(e.cmd)->HandleLine(line, running); }); },
            [] { rec.After(); }, [] { rec.Before(); });
    }
    *wordLength = savedLength;
}
