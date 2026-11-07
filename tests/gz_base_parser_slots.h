#pragma once
// Fixtures for the differential tests of cBaseParser's virtual functions (lane HU; tests/gz_base_parser_*.cpp).
//
// Each case builds two parsers the same way with the original constructor (0xafd800, the whole object: the game's
// vtables) and the original Reset (0xaff120, which opens the global namespace), applies the same setup to both,
// then runs the original slot body on parser A (ecx = the slot's face, as the vtordisp thunk hands it over) and our
// adapter on parser B (the hook's entry, same registers). What happened is compared: the result (and any exception:
// tests/command_parser.h RunGuarded), the calls made on fake objects (in order, with arguments; addresses inside
// each side's parser as offsets, text arguments as their characters) and the parser's state afterwards (Form below:
// every field, vectors and strings by content, heap objects by what they hold).
#include "command_parser.h"
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cBaseParser.h"
#include "gz_file_source.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace hu {

using nGZCommandParser::cBaseParser;
using nGZCommandParser::cBaseParserAdapters;

constexpr uint32_t kConstruct = 0xafd800;
constexpr uint32_t kReset = 0xaff120;
constexpr uint32_t kDestruct = 0xafe1b0;

// ---- fake objects -------------------------------------------------------------------------------------------------
//
// An object whose vtable is a Fake's: every slot logs (object id, slot, arguments) and answers ret[slot]. Text
// arguments (argument i of slot s marked in textArgs) are logged by content.

struct Call {
    int object;
    int slot;
    std::string args;
};
inline std::vector<Call> gCalls;
inline const uint8_t *gBase = nullptr; // the parser of the side running: addresses inside it are logged as offsets
inline std::vector<std::pair<const void *, int>> gFakeObjects; // the test's objects (shared by both sides) and ids

inline int FakeId(uint32_t v)
{
    for (const auto &[p, id] : gFakeObjects)
        if (reinterpret_cast<uintptr_t>(p) == v) return id;
    return -1;
}

inline std::string Word(uint32_t v)
{
    char buf[32];
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gBase));
    if (gBase && v - base < 0x1c0) std::snprintf(buf, sizeof(buf), "P+%x", v - base);
    else if (FakeId(v) >= 0) std::snprintf(buf, sizeof(buf), "F%d", FakeId(v));
    else std::snprintf(buf, sizeof(buf), "%x", v);
    return buf;
}

// A text pointer as what it points to (for results that may be heap or parser addresses).
inline uint32_t TextResult(const char *text, const uint8_t *base)
{
    if (!text) return 0;
    const uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text));
    const uint32_t b = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base));
    uint32_t h = 2166136261u;
    for (const char *c = text; *c; ++c) h = (h ^ static_cast<uint8_t>(*c)) * 16777619u;
    return (v - b < 0x1c0 ? 0x80000000u | (v - b) << 20 : 0) ^ (h & 0x7fffffff);
}

struct Fake {
    static constexpr int kSlots = 64;
    void *slots[kSlots];       // the vtable: objects point here
    int id;
    uint32_t ret[kSlots] = {};
    uint64_t textArgs = 0;     // bit (slot): the slot's first argument is text
    uint64_t stackArgs = 0;    // bit (slot): the slot's first argument is a stack address (logged as "stack")
    std::function<void(void *self, int slot, const uint32_t *args)> hook; // optional side effect

    explicit Fake(int objectId);
    template <int S> void SetArity(int arity);
};

template <int S, int N> uint32_t __fastcall FakeSlot(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Fake *fake = reinterpret_cast<Fake *>(*static_cast<void ***>(self));
    const uint32_t args[3] = {a, b, c};
    {
        difftest::NotCounted bookkeeping; // the log is the test's (difftest::AllocationCounter)
        std::string text;
        for (int i = 0; i < N; ++i) {
            if (i == 0 && (fake->textArgs >> S & 1)) text += std::string("\"") + reinterpret_cast<const char *>(static_cast<uintptr_t>(a)) + "\" ";
            else if (i == 0 && (fake->stackArgs >> S & 1)) text += "stack ";
            else text += Word(args[i]) + " ";
        }
        gCalls.push_back({fake->id, S, text});
    }
    if (fake->hook) fake->hook(self, S, args);
    return fake->ret[S];
}
// The callee pops its N stack words: one function per (slot, arity) with the right `ret N`.
template <int S, int N> struct SlotFn;
template <int S> struct SlotFn<S, 0> {
    static uint32_t __fastcall F(void *self, void *e) { return FakeSlot<S, 0>(self, e, 0, 0, 0); }
};
template <int S> struct SlotFn<S, 1> {
    static uint32_t __fastcall F(void *self, void *e, uint32_t a) { return FakeSlot<S, 1>(self, e, a, 0, 0); }
};
template <int S> struct SlotFn<S, 2> {
    static uint32_t __fastcall F(void *self, void *e, uint32_t a, uint32_t b) { return FakeSlot<S, 2>(self, e, a, b, 0); }
};
template <int S> struct SlotFn<S, 3> {
    static uint32_t __fastcall F(void *self, void *e, uint32_t a, uint32_t b, uint32_t c) { return FakeSlot<S, 3>(self, e, a, b, c); }
};

template <int S> void Fake::SetArity(int arity)
{
    switch (arity) {
    case 0: slots[S] = reinterpret_cast<void *>(&SlotFn<S, 0>::F); break;
    case 1: slots[S] = reinterpret_cast<void *>(&SlotFn<S, 1>::F); break;
    case 2: slots[S] = reinterpret_cast<void *>(&SlotFn<S, 2>::F); break;
    default: slots[S] = reinterpret_cast<void *>(&SlotFn<S, 3>::F); break;
    }
}

template <int... S> void SetAllZero(Fake &f, std::integer_sequence<int, S...>) { (f.SetArity<S>(0), ...); }
inline Fake::Fake(int objectId) : id(objectId) { SetAllZero(*this, std::make_integer_sequence<int, kSlots>()); }

// A fake object: the vtable word, then room for fields (a definition's name at +8 and level at +0x68).
struct FakeObject {
    void *vtable;
    uint32_t pad;
    std::string name;      // +8
    uint8_t rest[0x48];    // +0x20
    int32_t level;         // +0x68
    uint32_t tail[4];
};
static_assert(offsetof(FakeObject, name) == 8 && offsetof(FakeObject, level) == 0x68, "FakeObject layout");

// A set of fake objects with their own vtables (one Fake each, so each answers its own name).
struct Fakes {
    std::vector<std::unique_ptr<Fake>> vtables;
    std::vector<std::unique_ptr<FakeObject>> objects;
    std::vector<std::unique_ptr<std::string>> names;

    Fakes() = default;
    Fakes(const Fakes &) = delete;
    ~Fakes()
    {
        for (const auto &o : objects)
            std::erase_if(gFakeObjects, [&](const auto &e) { return e.first == o.get(); });
    }

    FakeObject *Add(int id, const std::string &name = {}, int32_t level = 0)
    {
        vtables.push_back(std::make_unique<Fake>(id));
        objects.push_back(std::make_unique<FakeObject>());
        names.push_back(std::make_unique<std::string>(name));
        gFakeObjects.push_back({objects.back().get(), id});
        Fake &vt = *vtables.back();
        FakeObject &o = *objects.back();
        o.vtable = vt.slots;
        o.name = name;
        o.level = level;
        vt.ret[5] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(names.back()->c_str()));
        // Command slots: 3 SetContext (2), 6 GetHelp (1); output slot 14 Write (1, text).
        vt.SetArity<3>(2);
        vt.SetArity<6>(1);
        vt.SetArity<14>(1);
        vt.textArgs |= 1ull << 14;
        return &o;
    }
    Fake &VTableOf(const FakeObject *o) { return *reinterpret_cast<Fake *>(o->vtable); }
};

// ---- the two parsers -----------------------------------------------------------------------------------------------

struct alignas(16) ParserBlock {
    uint8_t bytes[sizeof(cBaseParser)];
    cBaseParser *P() { return reinterpret_cast<cBaseParser *>(bytes); }
    void *Face() { return bytes + 0x1b8; }
    void *UnknownFace() { return bytes + 0x1b0; }
};

inline ParserBlock *NewParser(difftest::Context &ctx, bool reset = true)
{
    auto *block = new ParserBlock;
    std::memset(block->bytes, 0x5a, sizeof(block->bytes));
    ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kConstruct)(block->bytes, 1);
    block->bytes[0x2c] = 0; // not written by the constructor: names folded
    std::memset(block->bytes + 0xe4, 0, 4);
    block->bytes[0xef] = 0;
    if (reset) ctx.Original<void(__thiscall *)(void *)>(kReset)(block->Face());
    return block;
}

// Frees the parser through the original destructor (which releases what it holds through their vtables: fakes log
// it, which nobody reads afterwards).
inline void FreeParser(difftest::Context &ctx, ParserBlock *block)
{
    ctx.Original<void(__thiscall *)(void *)>(kDestruct)(block->bytes);
    delete block;
    gCalls.clear();
}

// ---- the form: a parser's state as text ----------------------------------------------------------------------------

inline uint32_t W(const uint8_t *p, size_t off)
{
    uint32_t v;
    std::memcpy(&v, p + off, 4);
    return v;
}
inline const uint8_t *Ptr(uint32_t v) { return reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(v)); }

struct FormWriter {
    std::string out;
    const uint8_t *base;
    void Add(const char *label, const std::string &v) { out += label; out += '='; out += v; out += '\n'; }
    void Raw(const char *label, size_t from, size_t to)
    {
        std::string s;
        char b[4];
        for (size_t i = from; i < to; ++i) {
            std::snprintf(b, sizeof(b), "%02x", base[i]);
            s += b;
        }
        Add(label, s);
    }
    std::string Words(uint32_t first, uint32_t last, size_t elemWords = 1)
    {
        std::string s;
        if (last < first || last - first > 0x10000) return "BAD";
        for (uint32_t p = first; p < last; p += 4) s += Word(W(Ptr(p), 0)) + (((p - first) / 4 + 1) % elemWords == 0 ? ";" : ",");
        return s;
    }
    // A std::vector at `off`: size and capacity in bytes, and its words.
    void Vector(const char *label, size_t off, size_t elemWords = 1, bool contents = true)
    {
        const uint32_t first = W(base, off), last = W(base, off + 4), end = W(base, off + 8);
        char head[64];
        std::snprintf(head, sizeof(head), "n%x c%x ", last - first, end - first);
        Add(label, head + (contents ? Words(first, last, elemWords) : std::string()));
    }
    // A std::string at `off`: size, capacity, text.
    void String(const char *label, size_t off)
    {
        const uint32_t size = W(base, off + 0x10), cap = W(base, off + 0x14);
        const char *text = cap > 15 ? reinterpret_cast<const char *>(Ptr(W(base, off))) : reinterpret_cast<const char *>(base + off);
        char head[48];
        std::snprintf(head, sizeof(head), "n%x c%x ", size, cap);
        Add(label, head + std::string(text, size) + "|" + std::to_string(static_cast<unsigned char>(text[size])));
    }
    // A hash table (CommandTable / VariableTable layout) at `off`: sizes, the nodes in list order (key and value), and
    // each bucket as node indices. Variable values are std::strings (valueIsString).
    // A namespace's child table (valueIsString false, children true): each value is a namespace, dumped after the
    // table under its path (the heap addresses differ between the sides).
    void Table(const char *label, const uint8_t *table, bool valueIsString, bool children = false, int depth = 0)
    {
        std::vector<std::pair<std::string, uint32_t>> kids;
        const uint32_t head = W(table, 4);
        std::vector<uint32_t> nodes;
        std::string s;
        char b[64];
        std::snprintf(b, sizeof(b), "load %08x size %u mask %x buckets %u: ", W(table, 0), W(table, 8), W(table, 0x18), W(table, 0x1c));
        s += b;
        for (uint32_t n = W(Ptr(head), 0); n != head && nodes.size() < 5000; n = W(Ptr(n), 0)) {
            nodes.push_back(n);
            s += std::string(reinterpret_cast<const char *>(Ptr(W(Ptr(n), 8))), W(Ptr(n), 0xc)) + ":";
            if (valueIsString) {
                const uint8_t *v = Ptr(n) + 0x10;
                const uint32_t size = W(v, 0x10), cap = W(v, 0x14);
                s += std::string(cap > 15 ? reinterpret_cast<const char *>(Ptr(W(v, 0))) : reinterpret_cast<const char *>(v), size);
            } else if (children) {
                const uint32_t child = W(Ptr(n), 0x10);
                kids.push_back({std::string(reinterpret_cast<const char *>(Ptr(W(Ptr(n), 8))), W(Ptr(n), 0xc)), child});
                s += child ? "ns" : "null";
            } else {
                s += Word(W(Ptr(n), 0x10));
            }
            s += ";";
        }
        auto index = [&](uint32_t n) {
            if (n == head) return std::string("H");
            for (size_t i = 0; i < nodes.size(); ++i)
                if (nodes[i] == n) return std::to_string(i);
            return std::string("?");
        };
        s += " buckets:";
        const uint32_t bFirst = W(table, 0xc), bLast = W(table, 0x10);
        for (uint32_t p = bFirst; p < bLast && p - bFirst < 0x10000; p += 4) s += index(W(Ptr(p), 0)) + ",";
        Add(label, s);
        if (depth < 8)
            for (const auto &[name, child] : kids)
                if (child) Namespace(std::string(label) + "/" + name, Ptr(child), depth + 1);
    }
    void Namespace(const std::string &label, const uint8_t *ns, int depth = 0)
    {
        Table((label + ".vars").c_str(), ns, true);
        Table((label + ".children").c_str(), ns + 0x20, false, true, depth);
        char b[64];
        std::snprintf(b, sizeof(b), "chunks %u used %x uses %u", W(ns, 0x44), W(ns, 0x48), W(ns, 0x4c));
        Add((label + ".pool").c_str(), b);
    }
};

// The whole parser. `fakes` are the addresses the test handed in (shared by both sides: compared as they are).
inline std::string Form(const uint8_t *p)
{
    const uint8_t *saved = gBase;
    gBase = p;
    FormWriter f{{}, p};
    f.Raw("head", 0, 0x14);
    // The output: an object the test gave (same on both sides) or one the parser made (its fields: vtable, count, the
    // byte at +8 and +0xc; +9..+0xb are padding no constructor writes, see appdebugger::OStreamBase, so they hold
    // whatever the heap block held before).
    const uint32_t output = W(p, 0x14);
    f.Add("output", output == 0 || FakeId(output) >= 0
                        ? Word(output)
                        : "made " + f.Words(output, output + 8) + Word(*Ptr(output + 8)) + ";" + f.Words(output + 12, output + 16));
    f.Raw("handler", 0x18, 0x1c);
    const uint32_t source = W(p, 0x1c);
    // A made source's vtable words as the game's (ours is in the standalone shape: tests/gz_file_source.h).
    uint8_t sourceBytes[12] = {};
    if (source) filesource::SourceBytes(Ptr(source), sourceBytes);
    f.Add("source", source ? Word(W(sourceBytes, 0)) + "," + Word(W(sourceBytes, 4)) + "," + Word(W(sourceBytes, 8)) + "," : "0");
    f.Vector("functions", 0x20, 2);
    f.Raw("2c", 0x2c, 0x30);
    f.Table("commands", p + 0x30, false);
    char b[64];
    std::snprintf(b, sizeof(b), "chunks %u used %x", W(p, 0x54), W(p, 0x58));
    f.Add("pool", b);
    f.Raw("5c", 0x5c, 0x6c);
    f.Vector("definitions", 0x6c);
    f.Namespace("global", p + 0x78);
    f.String("file", 0xc8);
    f.Raw("e0", 0xe0, 0xf8);
    f.Vector("meta", 0xf8);
    f.Vector("open", 0x104);
    {
        const uint32_t first = W(p, 0x110), last = W(p, 0x114), end = W(p, 0x118);
        std::string s = "n" + std::to_string((last - first) / 8) + " c" + std::to_string((end - first) / 8) + " ";
        for (uint32_t q = first; q < last && last - first < 0x10000; q += 8) {
            std::snprintf(b, sizeof(b), "%d/%u/%u;", static_cast<int32_t>(W(Ptr(q), 0)), Ptr(q)[4], Ptr(q)[5]);
            s += b;
        }
        f.Add("conditions", s);
    }
    f.Vector("pending", 0x11c, 13, false);
    f.Vector("lengths", 0x128);
    f.String("path", 0x134);
    {
        const uint32_t first = W(p, 0x14c), last = W(p, 0x150), end = W(p, 0x154);
        std::snprintf(b, sizeof(b), "n%x c%x", last - first, end - first);
        f.Add("namespaces", b);
        int i = 0;
        for (uint32_t q = first; q < last && last - first < 0x1000; q += 4, ++i) {
            const uint32_t ns = W(Ptr(q), 0);
            if (Ptr(ns) == p + 0x78) f.Add(("ns" + std::to_string(i)).c_str(), "global");
            else f.Namespace("ns" + std::to_string(i), Ptr(ns));
        }
    }
    f.Raw("158", 0x158, 0x160);
    f.Vector("commandList", 0x160);
    f.Vector("metaCommands", 0x16c);
    f.Raw("text.vt", 0x178, 0x17c);
    f.String("text", 0x17c);
    f.Raw("text.rc", 0x194, 0x19c);
    f.Raw("lists", 0x1a0, 0x1a4);
    f.Raw("lists2", 0x1a8, 0x1c0);
    gBase = saved;
    return f.out;
}

// The first line where two forms differ (for the failure message).
inline std::string FirstDiff(const std::string &a, const std::string &b)
{
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    const size_t start = a.rfind('\n', i == 0 ? 0 : i - 1);
    const size_t s = start == std::string::npos ? 0 : start + 1;
    auto line = [&](const std::string &x) { return x.substr(s, x.find('\n', s) - s); };
    return "A: " + line(a) + "\n    B: " + line(b);
}

// cptest's capture reads a thrown cError (vtable, then the message as a std::string). The standard exceptions (the
// out_of_range the string erase in slot 21 throws) are {vtable, what, do_free} instead, and their vtables are each
// module's own (the game's STL and ours), so for them the description is what() and the catchable types.
struct StdThrown {
    cptest::Thrown thrown;
    bool isStd = false;
    std::string what;
};

inline int CaptureStd(EXCEPTION_POINTERS *p, StdThrown *t)
{
    const EXCEPTION_RECORD *r = p->ExceptionRecord;
    if (r->ExceptionCode != 0xE06D7363 || r->NumberParameters < 3) return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t *throwInfo = reinterpret_cast<const uint32_t *>(r->ExceptionInformation[2]);
    const uint32_t *types = reinterpret_cast<const uint32_t *>(throwInfo[3]);
    for (uint32_t i = 0; i < types[0]; ++i) {
        const uint32_t *ct = reinterpret_cast<const uint32_t *>(types[1 + i]);
        if (std::strcmp(reinterpret_cast<const char *>(ct[1]) + 8, ".?AVexception@std@@") == 0) t->isStd = true;
    }
    if (!t->isStd) return cptest::CaptureThrown(p, &t->thrown);
    const uint32_t *obj = reinterpret_cast<const uint32_t *>(r->ExceptionInformation[1]);
    t->what = obj[1] ? reinterpret_cast<const char *>(obj[1]) : "(null)";
    for (uint32_t i = 0; i < types[0]; ++i) {
        const uint32_t *ct = reinterpret_cast<const uint32_t *>(types[1 + i]);
        t->what += std::string(" ") + (reinterpret_cast<const char *>(ct[1]) + 8);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

inline bool CallGuardedStd(void (*fn)(void *), void *ctx, StdThrown *t)
{
    __try {
        fn(ctx);
        return false;
    } __except (CaptureStd(GetExceptionInformation(), t)) {
        return true;
    }
}

template <class F> cptest::Outcome RunGuarded(F f)
{
    struct Ctx {
        F *f;
        uint32_t ret;
    } c{&f, 0};
    StdThrown t;
    const bool threw = CallGuardedStd(
        [](void *p) {
            auto *cc = static_cast<Ctx *>(p);
            cc->ret = static_cast<uint32_t>((*cc->f)());
        },
        &c, &t);
    cptest::Outcome o;
    o.ret = c.ret;
    o.threw = threw;
    if (threw && t.isStd) o.detail = "std '" + t.what + "'";
    else if (threw) {
        char buf[2800];
        std::snprintf(buf, sizeof(buf), "vtable %08x size %u text '%s' types %s", t.thrown.vtable, t.thrown.size, t.thrown.text, t.thrown.types);
        o.detail = buf;
    }
    return o;
}

inline std::string Describe(const std::vector<Call> &calls)
{
    std::string s;
    for (const Call &c : calls) s += "o" + std::to_string(c.object) + ".s" + std::to_string(c.slot) + "(" + c.args + ") ";
    return s;
}

// One case: `setup` runs on each fresh parser (same on both sides), then `orig` on A and `ours` on B (each gets its
// parser; returns the value for eax). Compares outcome, calls and forms; `extra` may add side-specific checks to the
// form (an out buffer). Frees both parsers.
struct Side {
    ParserBlock *parser;
    std::string extra; // what the run wrote outside the parser, as text
};
template <class Setup, class Orig, class Ours>
bool Compare(difftest::Context &ctx, const char *what, Setup setup, Orig orig, Ours ours, bool reset = true)
{
    Side a{NewParser(ctx, reset), {}}, b{NewParser(ctx, reset), {}};
    gBase = a.parser->bytes;
    setup(a);
    gBase = b.parser->bytes;
    setup(b);
    gCalls.clear();
    gBase = a.parser->bytes;
    const cptest::Outcome oa = RunGuarded([&] { return orig(a); });
    const std::vector<Call> ca = gCalls;
    const std::string fa = Form(a.parser->bytes) + a.extra;
    gCalls.clear();
    gBase = b.parser->bytes;
    const cptest::Outcome ob = RunGuarded([&] { return ours(b); });
    const std::vector<Call> cb = gCalls;
    const std::string fb = Form(b.parser->bytes) + b.extra;
    gBase = nullptr;
    ++ctx.cases;
    bool ok = true;
    if (!(oa == ob)) {
        ctx.Fail("%s: outcome %08x threw %d %s / %08x threw %d %s", what, oa.ret, oa.threw, oa.detail.c_str(), ob.ret, ob.threw, ob.detail.c_str());
        ok = false;
    }
    if (Describe(ca) != Describe(cb)) {
        ctx.Fail("%s: calls\n    A: %s\n    B: %s", what, Describe(ca).c_str(), Describe(cb).c_str());
        ok = false;
    }
    if (fa != fb) {
        ctx.Fail("%s: state\n    %s", what, FirstDiff(fa, fb).c_str());
        ok = false;
    }
    FreeParser(ctx, a.parser);
    FreeParser(ctx, b.parser);
    return ok;
}

// Calls a face body: the original's RVA or our adapter, both with ecx = the face (the same register layout).
template <class R, class... Args> R CallFace(const void *fn, void *face, Args... args)
{
    return reinterpret_cast<R(__fastcall *)(void *, void *, Args...)>(const_cast<void *>(fn))(face, nullptr, args...);
}

} // namespace hu
