// Differential tests for cBaseParser's run path (lane IH; src/gz/cParserRun.cpp): stripping a line's comments
// (0xb02450), running a line (cIBaseParser slot 15, 0xb03fa0) and ThrowUnless's pointer twin (0xafc340).
//
// The parsers are built by the original constructor and Reset (tests/gz_base_parser_slots.h); the original runs on
// parser A, ours on parser B, and the results, the calls on the fakes and the parsers' state (the Form, plus the two
// line lists below) are compared. The game's operator new / delete and the RZ allocator are ours on both sides (the
// list nodes and the cArguments vectors one side allocates are freed by the parser's original destructor).
#define NOMINMAX
#include "code_patch.h"
#include "gz_base_parser_slots.h"

#include "gz/CommandParser.h"
#include "gz/cArguments.h"
#include "rz/RZMemory.h"

#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace hu;
namespace cp = nGZCommandParser;

namespace {

// ---- allocators ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
int gLiveRz = 0;
void *__cdecl TestRzMalloc(uint32_t bytes)
{
    ++gLiveRz;
    return std::malloc(bytes ? bytes : 1);
}
void __cdecl TestRzFree(void *p)
{
    if (p) --gLiveRz;
    std::free(p);
}

struct Allocators {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::StandIn rzMalloc{0x1be3b0, &RZMemory::Malloc, &TestRzMalloc};
    shape::StandIn rzFree{0x1be3a1, &RZMemory::Free, &TestRzFree};
};

// ---- reading what the run left ----

std::string StringAt(const uint8_t *s)
{
    const uint32_t size = W(s, 0x10), cap = W(s, 0x14);
    if (size > 0x10000) return "BAD";
    const char *text = cap > 15 ? reinterpret_cast<const char *>(Ptr(W(s, 0))) : reinterpret_cast<const char *>(s);
    return std::string(text, size) + "|" + std::to_string(cap);
}

uint32_t gOurArgumentsVTable = 0;

// A cArguments (either side's): the vtable as a code, the buffer, the words as offsets into it, the counts, the
// switches. The case flag (+0x15) only once Parse has run (the argument vector is not empty): a list node's cArguments
// is a copy (0xb0bdd0, i.e. operator= 0xb0c000, which does not write +0x15) in a block from operator new (0xb04056 in
// 0xb03fa0 zeroes only +0xc..+0x24 of the node), so before a Parse it holds what the heap block held. +0x16 / +0x17
// are padding nobody writes.
std::string Arguments(const uint8_t *a)
{
    std::string s;
    char b[64];
    const uint32_t vt = W(a, 0);
    s += vt == gOurArgumentsVTable || vt == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(cp::cArguments::kVTable)))
             ? "args{"
             : "args?{";
    const uint32_t buf = W(a, 4), bufEnd = W(a, 8), bufCap = W(a, 0xc);
    std::snprintf(b, sizeof(b), "buf n%x c%x '", bufEnd - buf, bufCap - buf);
    s += b;
    for (uint32_t p = buf; p < bufEnd && bufEnd - buf < 0x10000; ++p) s += *Ptr(p) ? static_cast<char>(*Ptr(p)) : '0';
    const uint32_t first = W(a, 0x18), last = W(a, 0x1c), cap = W(a, 0x20);
    std::snprintf(b, sizeof(b), "' pos %u skip %u ", W(a, 0x10), a[0x14]);
    s += b;
    if (first != last) {
        std::snprintf(b, sizeof(b), "case %u ", a[0x15]);
        s += b;
    }
    std::snprintf(b, sizeof(b), "words n%x c%x [", last - first, cap - first);
    s += b;
    for (uint32_t p = first; p < last && last - first < 0x10000; p += 4) s += std::to_string(W(Ptr(p), 0) - buf) + ",";
    const uint32_t sw = W(a, 0x24), swEnd = W(a, 0x28), swCap = W(a, 0x2c);
    std::snprintf(b, sizeof(b), "] switches n%x c%x [", swEnd - sw, swCap - sw);
    s += b;
    for (uint32_t p = sw; p < swEnd && swEnd - sw < 0x10000; p += 16) {
        std::snprintf(b, sizeof(b), "%u,%u,%u,%u;", W(Ptr(p), 0) - buf, W(Ptr(p), 4), W(Ptr(p), 8), Ptr(p)[12]);
        s += b;
    }
    return s + "]}";
}

// The line lists (+0x19c in use, +0x1a4 free): each item's text (vtable, string, count) and arguments.
std::string Lists(const uint8_t *p)
{
    std::string s;
    for (uint32_t off : {0x19cu, 0x1a4u}) {
        const uint32_t head = W(p, off);
        s += "list" + std::to_string(off) + " n" + std::to_string(W(p, off + 4)) + ":";
        int guard = 0;
        for (uint32_t n = W(Ptr(head), 0); n != head && guard < 64; n = W(Ptr(n), 0), ++guard) {
            const uint8_t *item = Ptr(n) + 8;
            s += " [" + std::string(W(item, 0) == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(cRZString::kVTable))) ? "rz" : "rz?") +
                 " '" + StringAt(item + 4) + "' rc" + std::to_string(W(item, 0x1c)) + " " + Arguments(item + 0x20) + "]";
        }
        s += "\n";
    }
    return s;
}

// ---- fakes ----

// What a command's Execute does (by the command's fake id): log its arguments, take switch "v", throw a cError, throw
// another cIGZExceptionBase, or run another line on the same parser (through the side's own slot 15: the in-use list
// then holds two lines, and the free list grows).
enum Mode { kLog, kTakeSwitch, kThrowError, kThrowOther, kNested };
Mode gModes[8];
const void *gRunLine = nullptr; // the side's slot 15 (the original's RVA or our adapter)
void *gFace = nullptr;
int gNesting = 0;
const char *const kNestedLines[] = {"run nested", "echo -v x", "nope", "Set \"open", ""};
unsigned gNestedLine = 0;

// An error that is a cIGZExceptionBase but not a cError (the "unknown exception type" handler).
const char *__fastcall OtherErrorText(const void *, void *) { return "other failure"; }
void *gOtherErrorVTable[1] = {reinterpret_cast<void *>(&OtherErrorText)};
struct OtherError : cIGZExceptionBase {
    uint32_t extra = 7;
};

void LogArguments(int id, void *args)
{
    const int32_t n = VirtualCall<int32_t(__thiscall *)(void *), 7>(args);
    std::string s = "n" + std::to_string(n) + " pos" + std::to_string(VirtualCall<uint32_t(__thiscall *)(void *), 8>(args)) +
                    " sw" + std::to_string(VirtualCall<int32_t(__thiscall *)(void *), 9>(args)) + ":";
    for (int32_t i = 0; i < n && i < 40; ++i) s += std::string(" ") + VirtualCall<const char *(__thiscall *)(void *, uint32_t), 4>(args, static_cast<uint32_t>(i));
    gCalls.push_back({id, 100, s});
}

void CommandHook(int id, int slot, const uint32_t *args)
{
    if (slot != 4) return;
    void *arguments = reinterpret_cast<void *>(static_cast<uintptr_t>(args[0]));
    LogArguments(id, arguments);
    switch (gModes[id]) {
    case kLog: break;
    case kTakeSwitch: {
        const bool taken = VirtualCall<bool(__thiscall *)(void *, const char *), 1>(arguments, "v");
        gCalls.push_back({id, 101, taken ? "taken" : "not taken"});
        break;
    }
    case kThrowError: cp::ThrowFormatted("command %d failed", id);
    case kThrowOther: {
        OtherError error;
        error.mVTable = gOtherErrorVTable;
        throw error;
    }
    case kNested:
        if (gNesting < 2) {
            ++gNesting;
            struct Leave {
                ~Leave() { --gNesting; }
            } leave;
            CallFace<void>(gRunLine, gFace, kNestedLines[gNestedLine]);
        }
        break;
    }
}

void MetaHook(int id, int slot, const uint32_t *args)
{
    if (slot != 10) return;
    const uint8_t *text = Ptr(args[0]);
    gCalls.push_back({id, 110, StringAt(text + 4)});
}

// ---- texts ----

const char *const kNames[] = {"echo", "ECHO", "Set", "run", "nope", "echo2"};
// '%' only as "%%": a failing line's text goes through the error's format string again (the ORIGINAL-BUG in RunLine), so
// a lone conversion would read whatever words the caller's frame holds, which differ between the two sides.
const char *const kWords[] = {"a", "bc", "-v", "-V", "-x", "\"q r\"", "(p q)", "$x", "${y}", "50%%", "\"open", "close)", "--", "-"};

std::string MakeComments(std::mt19937 &g)
{
    static const char *const kPieces[] = {"a", "bc", " ", "\t", "\r", "\n", "#", "#<", "#>", "##", "x#y", "#<#<", "#>#>",
                                          "  word  ", "<", ">", "# <", "#\t>", "-v", "\xe9"};
    std::string t;
    for (unsigned i = 0, n = g() % 9; i < n; ++i) t += kPieces[g() % std::size(kPieces)];
    return t;
}

std::string MakeLine(std::mt19937 &g)
{
    std::string t;
    for (unsigned i = 0, n = g() % 3; i < n; ++i) t += g() % 2 ? ' ' : '\t';
    unsigned kind = g() % 10;
    if (kind == 0) return t + MakeComments(g);
    if (kind == 1) return t;
    t += kNames[g() % std::size(kNames)];
    for (unsigned i = 0, n = g() % 5; i < n; ++i) {
        t += ' ';
        t += kWords[g() % std::size(kWords)];
    }
    if (g() % 5 == 0) t += g() % 2 ? " # comment" : " #< block";
    if (g() % 8 == 0) t += " #>";
    for (unsigned i = 0, n = g() % 3; i < n; ++i) t += g() % 2 ? ' ' : '\n';
    return t;
}

std::string Describe(const cptest::Outcome &o)
{
    char b[48];
    std::snprintf(b, sizeof(b), "ret %08x threw %d ", o.ret, o.threw);
    return b + o.detail;
}

} // namespace

// 0xb02450 on the parser (__thiscall, ret 8): texts with blanks, plain '#', "#<" and "#>" (nested, unbalanced), from
// block-comment depths 0..2, appended to empty, short and long strings.
DIFF_TEST(cBaseParser_AppendUncommented, 0xb02450)
{
    Allocators allocators;
    std::mt19937 g(0xb02450);
    for (int i = 0; i < 2000 && ctx.failures.size() < 6; ++i) {
        const std::string text = MakeComments(g);
        static const uint32_t kDepths[] = {0, 0, 0, 1, 2};
        const uint32_t depth = kDepths[g() % 5];
        static const char *const kStarts[] = {"", "", "pre", "0123456789abcdefghij"};
        const char *start = kStarts[g() % 4];
        char what[160];
        std::snprintf(what, sizeof(what), "AppendUncommented '%s' depth %u start '%s'", text.c_str(), depth, start);
        auto setup = [&](Side &s) { s.parser->P()->mFieldF4 = depth; };
        auto run = [&](Side &s, bool orig) {
            std::string out(start);
            cptest::Outcome o = hu::RunGuarded([&] {
                cp::cBaseParser *p = s.parser->P();
                return orig ? ctx.Original<bool(__thiscall *)(void *, const char *, std::string *)>(0xb02450)(p, text.c_str(), &out)
                            : cp::cBaseParserAdapters::AppendUncommented(p, nullptr, text.c_str(), &out);
            });
            o.ret &= 0xff; // al
            s.extra = "out '" + out + "' n" + std::to_string(out.size()) + " c" + std::to_string(out.capacity()) + " " + Describe(o) + "\n";
            return 0u;
        };
        Compare(ctx, what, setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
}

// Slot 15 (0xb03fa0, ecx = the face): a few lines per case on a parser with fake commands (each logging its arguments,
// taking a switch, or throwing a cError or another error), meta blocks that accept or decline the text, condition
// stacks, a pending call open or not, block-comment depth 0 or 1, both case flags, variables x and y defined or not
// (so expansion shows). After each line: what it threw;
// after the lines, the Form and the line lists.
DIFF_TEST(cBaseParser_RunLine, 0xb03fa0)
{
    Allocators allocators;
    {
        cp::cArguments probe;
        std::memcpy(&gOurArgumentsVTable, static_cast<const void *>(&probe), 4);
    }
    std::mt19937 g(0xb03fa0);
    for (int round = 0; round < 700 && ctx.failures.size() < 6; ++round) {
        Fakes fakes;
        std::vector<FakeObject *> commands;
        for (int i = 0; i < 5; ++i) {
            FakeObject *c = fakes.Add(i, kNames[i]);
            Fake &vt = fakes.VTableOf(c);
            vt.SetArity<4>(1);
            vt.stackArgs |= 1ull << 4;
            vt.ret[7] = g() % 3 == 0 ? 0x100u * (g() % 2) : 1u;   // al: 0x100 is false
            vt.ret[8] = g() % 4;
            const int id = i;
            vt.hook = [id](void *, int slot, const uint32_t *args) { CommandHook(id, slot, args); };
            gModes[i] = static_cast<Mode>(g() % 8 < 5 ? g() % 2 : 2 + g() % 3);
            commands.push_back(c);
        }
        commands.erase(commands.begin() + 4); // "nope" stays unregistered
        FakeObject *meta = fakes.Add(6, "meta");
        Fake &metaVt = fakes.VTableOf(meta);
        metaVt.SetArity<10>(2);
        metaVt.stackArgs |= 1ull << 10;
        metaVt.ret[10] = g() % 3 == 0 ? 0u : (g() % 4 == 0 ? 0x100u : 1u);
        metaVt.hook = [](void *, int slot, const uint32_t *args) { MetaHook(6, slot, args); };
        const bool withMeta = g() % 4 == 0;
        const bool pending = g() % 6 == 0;
        const bool withVariables = g() % 2 == 0;
        const uint32_t depth = g() % 6 == 0 ? 1 : 0;
        const uint8_t caseFlag = static_cast<uint8_t>(g() % 2);
        std::vector<cp::cBaseParser::Condition> conditions;
        for (unsigned i = 0, n = 1 + g() % 3; i < n; ++i)
            conditions.push_back({static_cast<int32_t>(g() % 3), static_cast<uint8_t>(g() % 2), 0, {0, 0}});
        const unsigned nestedLine = g() % static_cast<unsigned>(std::size(kNestedLines));
        std::vector<std::string> lines;
        for (unsigned i = 0, n = 1 + g() % 4; i < n; ++i) lines.push_back(MakeLine(g));
        std::string what = "RunLine round " + std::to_string(round) + ":";
        for (const std::string &l : lines) what += " '" + l + "'";

        auto setup = [&](Side &s) {
            cp::cBaseParser *p = s.parser->P();
            p->mCaseSensitive = caseFlag;
            for (FakeObject *c : commands) CallFace<void>(ctx.image.At(0xb059e0), s.parser->Face(), c);
            if (withMeta) p->mMetaBlocks.push_back(meta);
            p->mConditions.assign(conditions.begin(), conditions.end());
            p->mFieldF4 = depth;
            if (withVariables) {
                // "$x" and "${y}" in the lines then expand (only when slot 29 is asked to): through the original
                // SetVariable (primary slot 1, ecx = the whole parser), its trace output (+0xea) off.
                using SetVariableFn = void(__thiscall *)(void *, const char *, const char *);
                const uint8_t trace = s.parser->bytes[0xea];
                s.parser->bytes[0xea] = 0;
                ctx.Original<SetVariableFn>(0xb06450)(s.parser->bytes, "x", "v1");
                ctx.Original<SetVariableFn>(0xb06450)(s.parser->bytes, "y", "a b");
                s.parser->bytes[0xea] = trace;
            }
            if (pending) {
                auto *calls = static_cast<cp::PendingCall *>(::operator new(sizeof(cp::PendingCall)));
                std::memset(static_cast<void *>(calls), 0, sizeof(cp::PendingCall));
                p->mPendingCalls = {calls, calls + 1, calls + 1};
            }
        };
        auto run = [&](Side &s, bool orig) {
            gRunLine = orig ? ctx.image.At(0xb03fa0) : reinterpret_cast<const void *>(&cp::cBaseParserAdapters::RunLine);
            gFace = s.parser->Face();
            gNesting = 0;
            gNestedLine = nestedLine;
            for (const std::string &line : lines) {
                cptest::Outcome o = hu::RunGuarded([&] {
                    if (orig) CallFace<void>(ctx.image.At(0xb03fa0), s.parser->Face(), line.c_str());
                    else CallFace<void>(reinterpret_cast<const void *>(&cp::cBaseParserAdapters::RunLine), s.parser->Face(), line.c_str());
                    return 0u;
                });
                s.extra += Describe(o) + "\n";
            }
            s.extra += Lists(s.parser->bytes);
            return 0u;
        };
        Compare(ctx, what.c_str(), setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
}

// 0xafc340 (__cdecl, varargs): no throw for any non-null word (0x100 too: the whole word is tested); for null, the
// message formatted into 0x10fa248 and thrown as a cError, the message read again as its format.
DIFF_TEST(CommandParser_ThrowIfNull, 0xafc340)
{
    using Fn = void(__cdecl *)(uint32_t, const char *, ...);
    auto orig = ctx.Original<Fn>(0xafc340);
    const uint32_t words[] = {0, 1, 0x100, 0xff000000, 0x12345600};
    struct Case {
        const char *fmt;
        uint32_t a, b;
    };
    static const char kLong[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    const Case cases[] = {
        {"plain", 0, 0}, {"", 0, 0}, {"%s", reinterpret_cast<uint32_t>("hello"), 0}, {"%s %d", reinterpret_cast<uint32_t>("n"), 42},
        {"%s", reinterpret_cast<uint32_t>("100%%"), 0}, {"%x:%u", 0xabc, 7}, {"%s%s", reinterpret_cast<uint32_t>(kLong), reinterpret_cast<uint32_t>(kLong)},
        // The half-built error object, read by the second formatting: zero-width conversions skip to it.
        {"%s", reinterpret_cast<uint32_t>("v%.0s%.0s%.0s%.0s%x"), 0},
    };
    uint8_t *buffer = ctx.image.At<uint8_t>(0x10fa248);
    for (uint32_t word : words)
        for (const Case &c : cases) {
            for (int b = 0; b < 0x800; ++b) buffer[b] = static_cast<uint8_t>(0x41 + b % 26);
            char what[96];
            std::snprintf(what, sizeof(what), "ThrowIfNull %x '%s'", word, c.fmt);
            cptest::Differential(
                ctx, what, {{buffer, 0x800}}, [&] { return cptest::RunGuarded([&] { orig(word, c.fmt, c.a, c.b); }); },
                [&] { return cptest::RunGuarded([&] { reinterpret_cast<Fn>(&cp::ThrowIfNull)(word, c.fmt, c.a, c.b); }); });
        }
}
