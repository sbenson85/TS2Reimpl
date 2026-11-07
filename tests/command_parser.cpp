// Differential tests for src/gz/CommandParser.h: the command classes of nGZCommandParser.
//
// The base-class slots (construction, destruction, QueryInterface, AddRef, Release, SetContext, the
// trivial defaults) run on objects with random contents and are compared on the whole object. Calls
// the originals make through the object's own vtable (AddRef from QueryInterface, the deleting
// destructor from Release) go to recording fakes (tests/command_parser.h), and so do the calls the
// Execute slots make on the argument list, the parser and the parser state: original and
// replacement get identical fake objects and the two call logs are compared.
#include "difftest.h"

#include "command_parser.h"
#include "gz/CommandParser.h"
#include "free_recorder.h"

#include <windows.h>

#include <cstring>
#include <random>

namespace cp = nGZCommandParser;
using namespace cptest;

namespace {

using Ret8Fn = void(__thiscall *)(void *, void *, void *);
using Ret4Fn = uint32_t(__thiscall *)(void *, uint32_t);
using Void0Fn = uint32_t(__thiscall *)(void *);
using QiFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
using DeletingFn = void *(__thiscall *)(void *, uint32_t);

// Random bytes with canary words after them; both sides start from the same bytes.
template <size_t N> struct Block {
    uint32_t words[N / 4 + 2];
    void Fill(std::mt19937 &gen)
    {
        for (auto &w : words) w = gen();
    }
};

uint32_t Pick(std::mt19937 &gen, std::initializer_list<uint32_t> special)
{
    uint32_t r = gen();
    if (r % 3 == 0) return *(special.begin() + (gen() % special.size()));
    return gen();
}

// ---- construction and destruction ----------------------------------------------------------------

DIFF_TEST(cCommand_Construct, 0xaf7820)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xaf7820);
    std::mt19937 gen(1101);
    for (int i = 0; i < 200; ++i) {
        Block<12> a, b;
        a.Fill(gen);
        b = a;
        void *retA = orig(a.words);
        void *retB = reinterpret_cast<cp::cCommand *>(b.words)->Construct();
        ++ctx.cases;
        if (retA != a.words || retB != b.words) ctx.Fail("cCommand ctor (case %d) returned %p / %p, expected this", i, retA, retB);
        if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("cCommand ctor (case %d): object or canary differs from original", i);
    }
}

template <class T> void DestructTest(difftest::Context &ctx, uint32_t rva, void (T::*ours)(), size_t size)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(rva);
    std::mt19937 gen(rva);
    for (int i = 0; i < 100; ++i) {
        Block<64> a, b;
        a.Fill(gen);
        b = a;
        orig(a.words);
        (reinterpret_cast<T *>(b.words)->*ours)();
        ++ctx.cases;
        if (std::memcmp(&a, &b, size + 8) != 0) ctx.Fail("destructor %x (case %d): object differs from original", rva, i);
    }
}

// The deleting destructor: flags with and without bit 0, on heap blocks of the object's size.
template <class T> void DeletingTest(difftest::Context &ctx, uint32_t rva, T *(T::*ours)(uint32_t), size_t size)
{
    auto orig = ctx.Original<DeletingFn>(rva);
    std::mt19937 gen(rva + 1);
    for (int i = 0; i < 40; ++i) {
        uint32_t flags = gen() % 4 | (i % 3 == 0 ? gen() & 0xfffffff0 : 0);
        auto *a = static_cast<uint32_t *>(::operator new(size));
        auto *b = static_cast<uint32_t *>(::operator new(size));
        for (size_t w = 0; w < size / 4; ++w) a[w] = b[w] = gen();
        std::vector<uint32_t> keep(a, a + size / 4);
        // Which block each side freed, recorded across the call (exact; with bit 0 clear nothing may be freed).
        void *retA;
        bool freedA, freedB;
        {
            difftest::FreeRecorder rec;
            retA = orig(a, flags);
            rec.Stop();
            freedA = rec.Freed(a);
        }
        T *retB;
        {
            difftest::FreeRecorder rec;
            retB = (reinterpret_cast<T *>(b)->*ours)(flags);
            rec.Stop();
            freedB = rec.Freed(b);
        }
        ++ctx.cases;
        if (retA != a || retB != reinterpret_cast<T *>(b)) ctx.Fail("deleting destructor %x flags %x: wrong return", rva, flags);
        if (freedA != freedB) ctx.Fail("deleting destructor %x flags %x: freed original %d, ours %d", rva, flags, freedA, freedB);
        if (flags & 1) {
            if (!freedA) ctx.Fail("deleting destructor %x flags %x: original did not free", rva, flags);
        } else {
            if (std::memcmp(a, b, size) != 0) ctx.Fail("deleting destructor %x flags %x: object differs", rva, flags);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

DIFF_TEST(cCommand_Destruct, 0xaf7990) { DestructTest<cp::cCommand>(ctx, 0xaf7990, &cp::cCommand::Destruct, 12); }
DIFF_TEST(cMetaCommand_Destruct, 0xaf79b0) { DestructTest<cp::cMetaCommand>(ctx, 0xaf79b0, &cp::cMetaCommand::Destruct, 8); }
DIFF_TEST(cCommand_DeletingDestructor, 0xaf7a00)
{
    DeletingTest<cp::cCommand>(ctx, 0xaf7a00, &cp::cCommand::DeletingDestructor, 12);
}
DIFF_TEST(cMetaCommand_DeletingDestructor, 0xaf7a60)
{
    DeletingTest<cp::cMetaCommand>(ctx, 0xaf7a60, &cp::cMetaCommand::DeletingDestructor, 8);
}
DIFF_TEST(cCommandT_DeletingDestructor, 0x247a60)
{
    DeletingTest<cp::cCommandT>(ctx, 0x247a60, &cp::cCommandT::DeletingDestructor, 16);
}
DIFF_TEST(cMetaCommandT_DeletingDestructor, 0xacfdc0)
{
    DeletingTest<cp::cMetaCommandT>(ctx, 0xacfdc0, &cp::cMetaCommandT::DeletingDestructor, 12);
}

// ---- QueryInterface -------------------------------------------------------------------------------

SelfVTable gSelfVTable;

// An object under test: its vtable is the recording one, the other words are random.
template <size_t N> struct Obj {
    uint32_t words[N / 4 + 2]; // the object, then canary words
    void Fill(std::mt19937 &gen)
    {
        for (auto &w : words) w = gen();
        words[0] = reinterpret_cast<uint32_t>(gSelfVTable.slots);
    }
    void *Ptr() { return words; }
};

template <class T>
void QueryInterfaceTest(difftest::Context &ctx, uint32_t rva, bool (T::*ours)(uint32_t, void **), const char *name,
                        std::initializer_list<uint32_t> ids)
{
    auto orig = ctx.Original<QiFn>(rva);
    std::mt19937 gen(rva + 2);
    for (int i = 0; i < 400; ++i) {
        Obj<16> a, b;
        a.Fill(gen);
        b = a;
        uint32_t iid = Pick(gen, ids);
        bool nullOut = i % 5 == 0;
        void *outA = reinterpret_cast<void *>(0x13579bdfu), *outB = outA;
        gLog.clear();
        uint8_t ra = orig(a.Ptr(), iid, nullOut ? nullptr : &outA);
        auto logA = gLog;
        gLog.clear();
        bool rb = (reinterpret_cast<T *>(b.Ptr())->*ours)(iid, nullOut ? nullptr : &outB);
        auto logB = gLog;
        ++ctx.cases;
        if (ra != static_cast<uint8_t>(rb)) ctx.Fail("%s id %08x null=%d: al %u vs %u", name, iid, nullOut, ra, rb);
        // The stored pointer is the object itself.
        bool storedA = outA == a.Ptr(), storedB = outB == b.Ptr();
        if (storedA != storedB || (!storedA && outA != outB)) ctx.Fail("%s id %08x: stored %p / %p", name, iid, outA, outB);
        if (!SameLogs(logA, a.Ptr(), logB, b.Ptr()))
            ctx.Fail("%s id %08x null=%d: calls differ (%zu vs %zu)", name, iid, nullOut, logA.size(), logB.size());
        if (std::memcmp(a.words + 1, b.words + 1, sizeof(a) - 4) != 0) ctx.Fail("%s id %08x: object changed differently", name, iid);
    }
}

DIFF_TEST(cCommand_QueryInterface, 0xaf7c30)
{
    QueryInterfaceTest<cp::cCommand>(ctx, 0xaf7c30, &cp::cCommand::QueryInterface, "cCommand::QueryInterface",
                                     {1, 0xaada39c7, 0xcada39cd, 0xcae872f1, 0, 2});
}
DIFF_TEST(cMetaCommand_QueryInterface, 0xaf7ca0)
{
    QueryInterfaceTest<cp::cMetaCommand>(ctx, 0xaf7ca0, &cp::cMetaCommand::QueryInterface, "cMetaCommand::QueryInterface",
                                         {1, 0xaada39c7, 0xcada39cd, 0xcae872f1, 0, 2});
}

// ---- AddRef and Release ---------------------------------------------------------------------------

template <class T>
void AddRefTest(difftest::Context &ctx, uint32_t rva, uint32_t (T::*ours)(), size_t countOffset)
{
    auto orig = ctx.Original<Void0Fn>(rva);
    std::mt19937 gen(rva + 3);
    for (int i = 0; i < 300; ++i) {
        Obj<16> a, b;
        a.Fill(gen);
        a.words[countOffset / 4] = Pick(gen, {0, 0xffffffffu, 0x7fffffffu, 0x80000000u, 1});
        b = a;
        uint32_t ra = orig(a.Ptr()), rb = (reinterpret_cast<T *>(b.Ptr())->*ours)();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("AddRef %x: returned %u vs %u", rva, ra, rb);
        if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("AddRef %x: object differs", rva);
    }
}

// Release: the object's deleting-destructor slot (9, 11 or 12) is a recording fake.
template <class T>
void ReleaseTest(difftest::Context &ctx, uint32_t rva, uint32_t (T::*ours)(), size_t countOffset)
{
    auto orig = ctx.Original<Void0Fn>(rva);
    std::mt19937 gen(rva + 4);
    for (int i = 0; i < 300; ++i) {
        Obj<16> a, b;
        a.Fill(gen);
        a.words[countOffset / 4] = Pick(gen, {0, 1, 2, 3, 0xffffffffu});
        b = a;
        gSelfRet = gen();
        gLog.clear();
        uint32_t ra = orig(a.Ptr());
        auto logA = gLog;
        gLog.clear();
        uint32_t rb = (reinterpret_cast<T *>(b.Ptr())->*ours)();
        auto logB = gLog;
        ++ctx.cases;
        if (ra != rb) ctx.Fail("Release %x count %u: returned %u vs %u", rva, a.words[countOffset / 4], ra, rb);
        if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("Release %x: object differs", rva);
        if (!SameLogs(logA, a.Ptr(), logB, b.Ptr())) ctx.Fail("Release %x: calls differ (%zu vs %zu)", rva, logA.size(), logB.size());
    }
}

DIFF_TEST(cCommand_AddRef, 0x145260) { AddRefTest<cp::cCommand>(ctx, 0x145260, &cp::cCommand::AddRef, 8); }
DIFF_TEST(cMetaCommand_AddRef, 0x148fb0) { AddRefTest<cp::cMetaCommand>(ctx, 0x148fb0, &cp::cMetaCommand::AddRef, 4); }
DIFF_TEST(cCommand_Release, 0x1d0be0) { ReleaseTest<cp::cCommand>(ctx, 0x1d0be0, &cp::cCommand::Release, 8); }
DIFF_TEST(cMetaCommand_Release, 0x153f70) { ReleaseTest<cp::cMetaCommand>(ctx, 0x153f70, &cp::cMetaCommand::Release, 4); }

// ---- SetContext and the trivial slots -------------------------------------------------------------

template <class T, class Fn> void SetContextTest(difftest::Context &ctx, uint32_t rva, Fn ours)
{
    auto orig = ctx.Original<Ret8Fn>(rva);
    std::mt19937 gen(rva + 5);
    for (int i = 0; i < 300; ++i) {
        Obj<16> a, b;
        a.Fill(gen);
        b = a;
        void *parser = reinterpret_cast<void *>(Pick(gen, {0, 1, 4, 0xffffffffu}));
        void *state = reinterpret_cast<void *>(Pick(gen, {0, 1, 3, 4, 5, 0xfffffffcu, 0xffffffffu}));
        orig(a.Ptr(), parser, state);
        (reinterpret_cast<T *>(b.Ptr())->*ours)(parser, state);
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("SetContext %x (parser %p, state %p): object differs: +4 %08x vs %08x, +8 %08x vs %08x, +c %08x vs %08x", rva,
                     parser, state, a.words[1], b.words[1], a.words[2], b.words[2], a.words[3], b.words[3]);
    }
}

DIFF_TEST(cCommand_SetContext, 0x1d0b40) { SetContextTest<cp::cCommand>(ctx, 0x1d0b40, &cp::cCommand::SetContext); }
DIFF_TEST(cMetaCommand_SetContext, 0x1b9a50) { SetContextTest<cp::cMetaCommand>(ctx, 0x1b9a50, &cp::cMetaCommand::SetContext); }
DIFF_TEST(cCommandT_SetContextAdjusted, 0x24c810)
{
    SetContextTest<cp::cCommandT>(ctx, 0x24c810, &cp::cCommandT::SetContextAdjusted);
}
DIFF_TEST(cCommandT_SetContextExact, 0x369510)
{
    SetContextTest<cp::cCommandT>(ctx, 0x369510, &cp::cCommandT::SetContextExact);
}
DIFF_TEST(cMetaCommandT_SetContextAdjusted, 0x24c860)
{
    SetContextTest<cp::cMetaCommandT>(ctx, 0x24c860, &cp::cMetaCommandT::SetContextAdjusted);
}

// The block command: QueryInterface, Release (slot 12 deletes), and slot 3 with the state.
DIFF_TEST(cBlockCommand_QueryInterface, 0xaf7c00)
{
    QueryInterfaceTest<cp::cBlockCommandBase>(ctx, 0xaf7c00, &cp::cBlockCommandBase::QueryInterface, "cBlockCommand::QueryInterface",
                                              {1, 0xaada39c7, 0xcada39cd, 0xcae872f1, 0, 2});
}
DIFF_TEST(cBlockCommand_Release, 0x313300) { ReleaseTest<cp::cBlockCommandBase>(ctx, 0x313300, &cp::cBlockCommandBase::Release, 8); }

DIFF_TEST(cBlockCommandT_SetContextAdjusted, 0x2c3c40)
{
    auto orig = ctx.Original<Ret8Fn>(0x2c3c40);
    std::mt19937 gen(0x2c3c40);
    FakeVTable parserVt(2);
    parserVt.Set<43>(0);
    alignas(8) uint8_t parser[16] = {};
    *reinterpret_cast<void **>(parser) = parserVt.slots;
    for (int i = 0; i < 200; ++i) {
        Obj<0x48> a, b;
        a.Fill(gen);
        b = a;
        parserVt.ret[43] = gen();
        void *state = reinterpret_cast<void *>(Pick(gen, {0, 1, 3, 4, 5, 0xfffffffcu, 0xffffffffu, 0x100000}));
        gLog.clear();
        orig(a.Ptr(), parser, state);
        auto logA = gLog;
        gLog.clear();
        reinterpret_cast<cp::cBlockCommandT *>(b.Ptr())->SetContextAdjusted(parser, state);
        auto logB = gLog;
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("cBlockCommandT::SetContextAdjusted (state %p): object differs", state);
        if (!SameLogs(logA, a.Ptr(), logB, b.Ptr())) ctx.Fail("cBlockCommandT::SetContextAdjusted: calls differ (%zu vs %zu)", logA.size(), logB.size());
        if (logA.size() != 1 || logA[0].slot != 43) ctx.Fail("cBlockCommandT::SetContextAdjusted: expected one slot-43 call");
    }
}

DIFF_TEST(cCommand_GetHelpDefault, 0x12e40)
{
    auto orig = ctx.Original<Ret4Fn>(0x12e40);
    std::mt19937 gen(6);
    for (int i = 0; i < 50; ++i) {
        Obj<16> a;
        a.Fill(gen);
        uint32_t index = gen();
        uint32_t ra = orig(a.Ptr(), index), rb = reinterpret_cast<cp::cCommand *>(a.Ptr())->GetHelpDefault(index);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("GetHelpDefault: %u vs %u", ra, rb);
    }
}

DIFF_TEST(cCommand_ReturnsFalse, 0x147480)
{
    auto orig = ctx.Original<Void0Fn>(0x147480);
    std::mt19937 gen(7);
    for (int i = 0; i < 50; ++i) {
        Obj<16> a;
        a.Fill(gen);
        uint32_t ra = orig(a.Ptr()), rb = reinterpret_cast<cp::cCommand *>(a.Ptr())->ReturnsFalse();
        ++ctx.cases;
        // The original sets only al; callers use al.
        if (static_cast<uint8_t>(ra) != static_cast<uint8_t>(rb)) ctx.Fail("ReturnsFalse: %u vs %u", ra & 0xff, rb & 0xff);
    }
}

DIFF_TEST(cCommand_ReturnsZero, 0x6c6b0)
{
    auto orig = ctx.Original<Void0Fn>(0x6c6b0);
    std::mt19937 gen(8);
    for (int i = 0; i < 50; ++i) {
        Obj<16> a;
        a.Fill(gen);
        uint32_t ra = orig(a.Ptr()), rb = reinterpret_cast<cp::cCommand *>(a.Ptr())->ReturnsZero();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("ReturnsZero: %u vs %u", ra, rb);
    }
}

// ---- name and help strings ------------------------------------------------------------------------

template <class T> void NameTest(difftest::Context &ctx, uint32_t rva, const char *(T::*ours)() const)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(rva);
    std::mt19937 gen(rva);
    for (int i = 0; i < 10; ++i) {
        Obj<64> a;
        a.Fill(gen);
        const char *ra = orig(a.Ptr()), *rb = (reinterpret_cast<T *>(a.Ptr())->*ours)();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("name %x: %p vs %p", rva, ra, rb);
    }
}

template <class T> void HelpTest(difftest::Context &ctx, uint32_t rva, const char *(T::*ours)(uint32_t) const)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *, uint32_t)>(rva);
    std::mt19937 gen(rva);
    for (int i = 0; i < 10; ++i) {
        Obj<64> a;
        a.Fill(gen);
        uint32_t index = i < 3 ? i : gen();
        const char *ra = orig(a.Ptr(), index), *rb = (reinterpret_cast<T *>(a.Ptr())->*ours)(index);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("help %x index %u: %p vs %p", rva, index, ra, rb);
    }
}

#define STRING_TESTS(cls, nameRva, helpRva)                                                             \
    DIFF_TEST(cls##_GetName, nameRva) { NameTest<cp::cls>(ctx, nameRva, &cp::cls::GetName); }          \
    DIFF_TEST(cls##_GetHelp, helpRva) { HelpTest<cp::cls>(ctx, helpRva, &cp::cls::GetHelp); }

STRING_TESTS(cArrayCreateCommand, 0xb03b90, 0xaff5f0);
STRING_TESTS(cAssertCommand, 0xb03ba0, 0xaff600);
STRING_TESTS(cCreateCommand, 0xb03bc0, 0xaff620);
STRING_TESTS(cDebugParserCommand, 0xb03bd0, 0xaff630);
STRING_TESTS(cDebuggerBreakCommand, 0xb03be0, 0xaff640);
STRING_TESTS(cDefineCommand, 0xb03bf0, 0xaff650);
STRING_TESTS(cDumpParserStateCommand, 0xb03c10, 0xaff670);
STRING_TESTS(cElseCommand, 0xb03c20, 0xaff680);
STRING_TESTS(cElseIfCommand, 0xb03c30, 0xaff690);
STRING_TESTS(cEndCommand, 0xb03c40, 0xaff6a0);
STRING_TESTS(cEndDefCommand, 0xb03c50, 0xaff6b0);
STRING_TESTS(cEndIfCommand, 0xb03c60, 0xaff6c0);
STRING_TESTS(cEvalCommand, 0xb03c70, 0xaff6d0);
STRING_TESTS(cIfCommand, 0xb03c80, 0xaff6e0);
STRING_TESTS(cIgnoreErrorsCommand, 0xb03c90, 0xaff6f0);
STRING_TESTS(cIncludeCommand, 0xb03ca0, 0xaff700);
STRING_TESTS(cListCommandsCommand, 0xb03cb0, 0xaff710);
STRING_TESTS(cLogCommand, 0xb03cc0, 0xaff720);
STRING_TESTS(cNamespaceCommand, 0xb03d00, 0xaff760);
STRING_TESTS(cPurgeCommand, 0xb03d10, 0xaff770);
STRING_TESTS(cSetBCommand, 0xb03d20, 0xaff780);
STRING_TESTS(cSetCCommand, 0xb03d30, 0xaff790);
STRING_TESTS(cSetCommand, 0xb03d40, 0xaff7a0);
STRING_TESTS(cSetECommand, 0xb03d50, 0xaff7b0);
STRING_TESTS(cSetFCommand, 0xb03d60, 0xaff7c0);
STRING_TESTS(cSetICommand, 0xb03d70, 0xaff7d0);
STRING_TESTS(cShowArgumentsCommand, 0xb03db0, 0xaff810);
STRING_TESTS(cSincludeCommand, 0xb03dc0, 0xaff820);
STRING_TESTS(cTraceCommand, 0xb03dd0, 0xaff830);
STRING_TESTS(cUndefineCommand, 0xb03de0, 0xaff840);
STRING_TESTS(cVersionCommand, 0xb03e00, 0xaff860);
STRING_TESTS(cWildIncludeCommand, 0xb03e10, 0xaff870);
STRING_TESTS(cSetV2Command, 0xb03d80, 0xaff7e0);
STRING_TESTS(cSetV3Command, 0xb03d90, 0xaff7f0);
STRING_TESTS(cSetV4Command, 0xb03da0, 0xaff800);

} // namespace
