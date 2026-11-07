// Differential tests for cAliasStubCommand (an alias: a command that runs another command line with
// its own arguments appended) and cAliasCommand's string getters. The stub holds four std::strings
// (0x70 bytes): they are built in place in the test, some long enough to live on the heap.
#include "command_parser_env.h"
#include "free_recorder.h"

#include <windows.h>

namespace {

constexpr size_t kStubSize = 0x70;
constexpr size_t kStringOffsets[4] = {0x10, 0x28, 0x40, 0x58};

// One text past 4 KB: its block is allocated aligned (the heap block starts before the text; HeapBlockOf).
const std::string kLongText = "set long " + std::string(4200, 'w');

const char *const kTexts[] = {"",
                              "x",
                              "set cmd",
                              "exactly15chars!",
                              "exactly16 chars!",
                              "a rather longer command line, well past the small string buffer of fifteen characters",
                              kLongText.c_str()};

// The heap block behind a std::string's buffer of `length` characters: the buffer itself, or for 4 KB and more
// (MSVC's _Allocate_manually_vector_aligned) the block whose address is stored just before it.
const void *HeapBlockOf(const void *text, size_t length)
{
    // A string built from `length` (>= 16) characters gets capacity length | 15 (at least 22), plus the terminator.
    if ((length | 15) + 1 < 0x1000) return text;
    return reinterpret_cast<void *const *>(text)[-1];
}

// An alias stub in raw memory with its four strings built in place.
struct Stub {
    alignas(8) uint8_t obj[kStubSize];
    std::string texts[4];  // what each string holds (a copy, for rebuilding)
    bool heap[4];

    std::string &At(int i) { return *reinterpret_cast<std::string *>(obj + kStringOffsets[i]); }

    void Build()
    {
        for (int i = 0; i < 4; ++i) {
            new (&At(i)) std::string(texts[i]);
            heap[i] = texts[i].size() > 15;
        }
    }
    void Randomize(std::mt19937 &gen)
    {
        for (auto &b : obj) b = static_cast<uint8_t>(gen());
        for (auto &t : texts) t = kTexts[gen() % (sizeof(kTexts) / sizeof(kTexts[0]))];
    }
    void Free()
    {
        for (int i = 0; i < 4; ++i) At(i).~basic_string();
    }
};

uint32_t gTextSeed = 0;
uint32_t __fastcall ArgsText(void *self, void *, uint32_t index)
{
    Record(self, 4, index);
    return reinterpret_cast<uint32_t>(kStrings[(index + gTextSeed) % (sizeof(kStrings) / sizeof(kStrings[0]))]);
}

// The parser's slot 15 takes the command line: the log gets no address, the text goes to gExtra.
uint32_t __fastcall ParserLine(void *self, void *, uint32_t text)
{
    Record(self, 15, 0);
    const char *s = reinterpret_cast<const char *>(text);
    gExtra.insert(gExtra.end(), s, s + std::strlen(s) + 1);
    return 0;
}

} // namespace

DIFF_TEST(cAliasStubCommand_GetName, 0xb03b80)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(0xb03b80);
    std::mt19937 gen(0xb03b80);
    for (int i = 0; i < 40; ++i) {
        Stub s;
        s.Randomize(gen);
        s.Build();
        const char *a = orig(s.obj), *b = reinterpret_cast<cp::cAliasStubCommand *>(s.obj)->GetName();
        ++ctx.cases;
        if (a != b || std::strcmp(a, s.texts[0].c_str()) != 0) ctx.Fail("GetName: %p vs %p", a, b);
        s.Free();
    }
}

DIFF_TEST(cAliasStubCommand_GetHelp, 0xaff5c0)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *, uint32_t)>(0xaff5c0);
    std::mt19937 gen(0xaff5c0);
    for (int i = 0; i < 60; ++i) {
        Stub s;
        s.Randomize(gen);
        s.Build();
        uint32_t index = i % 4 < 3 ? i % 4 : gen();
        const char *a = orig(s.obj, index), *b = reinterpret_cast<cp::cAliasStubCommand *>(s.obj)->GetHelp(index);
        ++ctx.cases;
        if (a != b) ctx.Fail("GetHelp(%u): %p vs %p", index, a, b);
        s.Free();
    }
}

DIFF_TEST(cAliasCommand_GetName, 0xb03b70)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(0xb03b70);
    alignas(8) uint8_t obj[0x10] = {};
    ++ctx.cases;
    const char *a = orig(obj), *b = reinterpret_cast<cp::cAliasCommand *>(obj)->GetName();
    if (a != b) ctx.Fail("GetName: %p vs %p", a, b);
}

DIFF_TEST(cAliasCommand_GetHelp, 0xaff590)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *, uint32_t)>(0xaff590);
    std::mt19937 gen(0xaff590);
    for (int i = 0; i < 60; ++i) {
        alignas(8) uint8_t obj[0x10] = {};
        uint32_t index = i < 4 ? i : (i % 2 ? gen() : gen() % 5);
        const char *a = orig(obj, index), *b = reinterpret_cast<cp::cAliasCommand *>(obj)->GetHelp(index);
        ++ctx.cases;
        if (a != b) ctx.Fail("GetHelp(%u): %p vs %p", index, a, b);
    }
}

DIFF_TEST(cAliasStubCommand_Execute, 0xaffb80)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xaffb80);
    std::mt19937 gen(0xaffb80);
    for (int i = 0; i < 120; ++i) {
        Env e(gen);
        Stub s;
        s.Randomize(gen);
        s.Build();
        *reinterpret_cast<void **>(s.obj + 4) = e.parser;
        e.argsVt.Set<7>(0);
        e.argsVt.slots[4] = reinterpret_cast<void *>(&ArgsText);
        e.argsVt.ret[7] = i % 9 == 0 ? static_cast<uint32_t>(-1 - gen() % 3) : gen() % 6;
        gTextSeed = gen() % 7;
        e.parserVt.slots[15] = reinterpret_cast<void *>(&ParserLine);
        Differential(
            ctx, "cAliasStubCommand::Execute", {{s.obj, kStubSize}},
            [&] { return RunGuarded([&] { orig(s.obj, e.args); }); },
            [&] { return RunGuarded([&] { reinterpret_cast<cp::cAliasStubCommand *>(s.obj)->Execute(reinterpret_cast<cp::cIArguments *>(e.args)); }); });
        s.Free();
    }
}

// The destructor body: the strings are released (their heap blocks freed) and reset to empty; the
// pointer left in the buffer of a string that was on the heap is stale and differs between sides.
DIFF_TEST(cAliasStubCommand_Destruct, 0xafe080)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xafe080);
    std::mt19937 gen(0xafe080);
    for (int i = 0; i < 80; ++i) {
        Stub s;
        s.Randomize(gen);
        // The object's own words (vtable and the base) are random.
        Differential(
            ctx, "cAliasStubCommand::Destruct", {{s.obj, kStubSize}},
            [&] {
                orig(s.obj);
                return Outcome{};
            },
            [&] {
                reinterpret_cast<cp::cAliasStubCommand *>(s.obj)->Destruct();
                return Outcome{};
            },
            [&] {
                for (int k = 0; k < 4; ++k)
                    if (s.heap[k]) *reinterpret_cast<uint32_t *>(s.obj + kStringOffsets[k]) = 0;
            },
            [&] { s.Build(); });
    }
}

DIFF_TEST(cAliasStubCommand_DeletingDestructor, 0xaff050)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xaff050);
    std::mt19937 gen(0xaff050);
    for (int i = 0; i < 40; ++i) {
        uint32_t flags = gen() % 4 | (i % 3 == 0 ? gen() & 0xfffffff0 : 0);
        Stub proto;
        proto.Randomize(gen);
        uint8_t *objs[2];
        const void *blocks[2][4] = {};
        for (int side = 0; side < 2; ++side) {
            objs[side] = static_cast<uint8_t *>(::operator new(kStubSize));
            std::memcpy(objs[side], proto.obj, kStubSize);
            for (int k = 0; k < 4; ++k) {
                new (reinterpret_cast<std::string *>(objs[side] + kStringOffsets[k])) std::string(proto.texts[k]);
                // The heap block behind a long string's text, read while the string is alive.
                if (proto.texts[k].size() > 15)
                    blocks[side][k] = HeapBlockOf(reinterpret_cast<std::string *>(objs[side] + kStringOffsets[k])->c_str(), proto.texts[k].size());
            }
        }
        // What each side freed, recorded across its call (exact: HeapValidate cannot tell a freed block from a reused
        // one, and a string of 4 KB or more is allocated aligned, its heap block starting before the text).
        void *ra = nullptr, *rb = nullptr;
        bool freedA = false, freedB = false, stringFreed[2][4] = {};
        for (int side = 0; side < 2; ++side) {
            difftest::FreeRecorder rec;
            void *r = side == 0 ? orig(objs[0], flags) : reinterpret_cast<cp::cAliasStubCommand *>(objs[1])->DeletingDestructor(flags);
            rec.Stop();
            (side == 0 ? ra : rb) = r;
            (side == 0 ? freedA : freedB) = rec.Freed(objs[side]);
            for (int k = 0; k < 4; ++k)
                if (blocks[side][k]) stringFreed[side][k] = rec.Freed(blocks[side][k]);
        }
        ++ctx.cases;
        if (ra != objs[0] || rb != objs[1]) ctx.Fail("deleting destructor flags %x: wrong return", flags);
        if (freedA != freedB || ((flags & 1) != 0) != freedA) ctx.Fail("deleting destructor flags %x: object freed %d / %d", flags, freedA, freedB);
        for (int k = 0; k < 4; ++k) {
            if (blocks[0][k] == nullptr) continue;
            // The string blocks are always released.
            bool a = stringFreed[0][k];
            bool b = stringFreed[1][k];
            if (!a || !b) ctx.Fail("deleting destructor flags %x: string %d (%zu characters) block released %d / %d", flags, k, proto.texts[k].size(), a, b);
        }
        if (!(flags & 1)) {
            // The strings are reset (the pointer left in a heap string's buffer differs).
            for (int k = 0; k < 4; ++k) {
                uint8_t *a = objs[0] + kStringOffsets[k], *b = objs[1] + kStringOffsets[k];
                size_t from = blocks[0][k] ? 4 : 0;
                if (std::memcmp(a + from, b + from, 0x18 - from) != 0) ctx.Fail("deleting destructor flags %x: string %d differs", flags, k);
            }
            if (std::memcmp(objs[0], objs[1], 0x10) != 0) ctx.Fail("deleting destructor flags %x: base differs", flags);
            ::operator delete(objs[0]);
            ::operator delete(objs[1]);
        }
    }
}

// ---- commands that run other commands --------------------------------------------------------------

namespace {

// Slot 10 of the argument list: (pointer to the count, minimum, maximum). The count it gives and the
// array of arguments are configured; the pointer (a stack address) is not logged.
int32_t gCount10 = 0;
const char *const *gArgv10 = nullptr;
uint32_t __fastcall ArgsSlot10(void *self, void *, int32_t *countOut, uint32_t lo, uint32_t hi)
{
    Record(self, 10, 0, lo, hi);
    *countOut = gCount10;
    return reinterpret_cast<uint32_t>(gArgv10);
}

// What the alias command registers: the stub's words and strings go to gExtra, and the stub is freed.
uint32_t __fastcall ParserRegister(void *self, void *, void *stub)
{
    Record(self, 4, 0);
    auto *bytes = static_cast<uint8_t *>(stub);
    // The state word at +0xc is not set by the command (uninitialised heap bytes): not compared.
    gExtra.insert(gExtra.end(), bytes, bytes + 0xc);
    for (size_t offset : kStringOffsets) {
        auto &s = *reinterpret_cast<std::string *>(bytes + offset);
        uint32_t size = static_cast<uint32_t>(s.size()), capacity = static_cast<uint32_t>(s.capacity());
        gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&size), reinterpret_cast<uint8_t *>(&size) + 4);
        gExtra.insert(gExtra.end(), reinterpret_cast<uint8_t *>(&capacity), reinterpret_cast<uint8_t *>(&capacity) + 4);
        gExtra.insert(gExtra.end(), s.begin(), s.end());
        s.~basic_string();
    }
    ::operator delete(stub);
    return 0;
}

const char *const kWords[] = {"alias1", "do_it", "a long enough alias name to leave the small buffer", "", "x y", "50%s"};

} // namespace

DIFF_TEST(cWildIncludeCommand_Execute, 0xb02260)
{
    ExecuteTest<cp::cWildIncludeCommand>(
        ctx, 0xb02260, "cWildIncludeCommand::Execute", &cp::cWildIncludeCommand::Execute,
        [](Env &e) {
            static const char *const kOptions[] = {"subfolders", "SUBFOLDERS", "SubFolders", "subfolder", "subfolderss", "", "x", "subfolders "};
            static const char *argv[2];
            argv[0] = kStrings[e.gen() % 7];
            argv[1] = kOptions[e.gen() % 8];
            e.argsVt.slots[10] = reinterpret_cast<void *>(&ArgsSlot10);
            gArgv10 = argv;
            gCount10 = e.gen() % 4 == 0 ? static_cast<int32_t>(e.gen() % 4) : 2;
            e.parserVt.Set<13>(2);
        },
        120);
}

DIFF_TEST(cArrayCreateCommand_Execute, 0xaffc70)
{
    ExecuteTest<cp::cArrayCreateCommand>(
        ctx, 0xaffc70, "cArrayCreateCommand::Execute", &cp::cArrayCreateCommand::Execute,
        [](Env &e) {
            e.parserVt.Set<32>(1);
            e.parserVt.ret[32] = e.gen() % 5;
            e.parserVt.slots[15] = reinterpret_cast<void *>(&ParserLine);
        },
        80);
}

DIFF_TEST(cAliasCommand_Execute, 0xaff8b0)
{
    ExecuteTest<cp::cAliasCommand>(
        ctx, 0xaff8b0, "cAliasCommand::Execute", &cp::cAliasCommand::Execute,
        [](Env &e) {
            static const char *argv[4];
            for (auto &w : argv) w = kWords[e.gen() % 6];
            e.argsVt.slots[10] = reinterpret_cast<void *>(&ArgsSlot10);
            gArgv10 = argv;
            gCount10 = 2 + e.gen() % 3;
            e.parserVt.slots[4] = reinterpret_cast<void *>(&ParserRegister);
        },
        120);
}
