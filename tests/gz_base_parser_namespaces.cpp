// Differential tests of cBaseParser's namespaces and its destructor (lane HU): the cIBaseParser face's slots 20 and 22
// with the namespace helpers they and Reset reach (0xb05d70 Clear, 0xafe800 the namespace destructor, 0xb036d0 the
// path lookup, 0xafd700 the child node erase, 0xafce70 / 0xb07790 the child table's insert and rehash, 0xafcb90 /
// 0xafdda0 the variable table, 0xaf6960 the pool reset), the pending calls (0xafc470, 0xafe000) and the destructor
// (0xafe1b0, 0xaff070). Fixture: tests/gz_base_parser_slots.h.
// Lane MH: the destructor tests read the heap from this thread's exact records (difftest::AllocationCounter and
// FreeRecorder) instead of three heap walks, and compare the call's allocated-minus-freed blocks and bytes as well.
#include "gz_base_parser_slots.h"

#include "game/vtable.h"
#include "hook/hooks.h"

#include <algorithm>
#include <memory>
#include <optional>

using namespace hu;
using namespace nGZCommandParser;

namespace {

// Through a volatile: the compiler must not see the function behind the cast.
template <class Fn> const void *Ours(Fn fn)
{
    const void *volatile p = reinterpret_cast<const void *>(fn);
    return p;
}

inline void Put(ParserBlock *p, size_t off, uint32_t v) { std::memcpy(p->bytes + off, &v, 4); }
uint32_t Addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A child node given a real (empty) namespace, made by the game's constructor, so that the parser's teardown finds
// one there.
void GiveNamespace(difftest::Context &ctx, CommandNode *node)
{
    void *block = ::operator new(sizeof(ParserNamespace));
    node->value = ctx.Original<void *(__thiscall *)(void *)>(0xafdc40)(block);
}

// A variable put into a namespace's table as the game's insert leaves one (after the last node of its bucket, or at
// the end of the list for an empty bucket). At most 8, so the table never needs to grow.
void AddVariable(ParserNamespace *ns, const char *name, const char *value)
{
    VariableTable &t = ns->mVariables;
    const uint32_t length = static_cast<uint32_t>(std::strlen(name));
    if (t.size >= 8) return;
    for (VariableNode *n = t.head->next; n != t.head; n = n->next)
        if (n->keyLength == length && std::memcmp(n->key, name, length) == 0) return;
    auto *node = static_cast<VariableNode *>(::operator new(sizeof(VariableNode)));
    node->key = name;
    node->keyLength = length;
    std::construct_at(&node->value, value);
    VariableNode **bucket = t.buckets.data() + 2 * (HashName(name, length) & t.mask);
    VariableNode *before = bucket[0] == t.head ? t.head : bucket[1]->next;
    node->next = before;
    node->prev = before->prev;
    before->prev->next = node;
    before->prev = node;
    if (bucket[0] == t.head) bucket[0] = node;
    bucket[1] = node;
    ++t.size;
}

// "i" and "q" share the bucket of "a" (mask 7), so removals meet buckets of several nodes.
const char *const kNames[] = {"a", "b", "c", "Alpha", "a_namespace_name_longer_than_fifteen", "i", "q"};
const char *const kPaths[] = {"a", "b", "a:b", "a:b:c", "a:", "a::b", "x", "", "a:b:", "Alpha:a", "b:a:c", "a:b:c:a", ":", "c:c", "i", "q", "a:i", "i:a", "q:", "a:q"};
const char *const kVariables[] = {"x", "y", "A", "b2", "zz", "count", "q", "a_long_variable_name"};
const char *const kValues[] = {"", "1", "two", "a value longer than fifteen characters"};

struct Op {
    enum Kind { Push, Pop, Purge, Variable, Reset, ResetBlocks } kind;
    int a, b;
};

// A random run of operations that never pops more than it pushed.
// A purge of a namespace that is open below the top of the stack is left out: Clear(false) keeps its open child but
// resets the pool that child's key is in (ORIGINAL-BUG, a use after free: what the freed key reads then depends on
// the heap's reuse, which differs between any two runs).
bool PurgesOpenParent(const std::vector<std::string> &stack, std::string path)
{
    if (path.size() > 1 && path.back() == ':') path.pop_back();
    std::string joined;
    for (size_t i = 0; i + 1 < stack.size(); ++i) {
        joined += (i ? ":" : "") + stack[i];
        if (joined == path) return true;
    }
    return false;
}

std::vector<Op> MakeOps(std::mt19937 &gen, bool stackOnlyGrows = false)
{
    std::vector<Op> ops;
    std::vector<std::string> stack;
    int depth = 0;
    for (int i = 0, n = 1 + static_cast<int>(gen() % 30); i < n; ++i) {
        const uint32_t r = gen() % 100;
        if (r < 35) {
            ops.push_back({Op::Push, static_cast<int>(gen() % std::size(kNames)), 0});
            stack.push_back(kNames[ops.back().a]);
            ++depth;
        } else if (r < 55) {
            if (stackOnlyGrows) continue;
            if (depth == 0) continue;
            ops.push_back({Op::Pop, 0, 0});
            stack.pop_back();
            --depth;
        } else if (r < 75) {
            const int path = static_cast<int>(gen() % std::size(kPaths));
            if (PurgesOpenParent(stack, kPaths[path])) continue;
            ops.push_back({Op::Purge, path, 0});
        } else if (r < 93) {
            ops.push_back({Op::Variable, static_cast<int>(gen() % std::size(kVariables)), static_cast<int>(gen() % std::size(kValues))});
        } else if (r < 96 && !stackOnlyGrows) {
            ops.push_back({Op::Reset, 0, 0});
            stack.clear();
            depth = 0;
        } else if (!stackOnlyGrows) {
            ops.push_back({Op::ResetBlocks, 0, 0});
            stack.clear();
            depth = 0;
        }
    }
    return ops;
}

// Runs the operations on a side: the face's slots from `fns` (the game's or ours), variables put in by the test.
struct Fns {
    const void *push, *pop, *purge, *reset, *resetBlocks;
};
void Run(Side &s, const std::vector<Op> &ops, const Fns &f)
{
    for (const Op &op : ops) {
        switch (op.kind) {
        case Op::Push: CallFace<void>(f.push, s.parser->Face(), kNames[op.a]); break;
        case Op::Pop: CallFace<void>(f.pop, s.parser->Face()); break;
        case Op::Purge: CallFace<void>(f.purge, s.parser->Face(), kPaths[op.a]); break;
        case Op::Variable: AddVariable(s.parser->P()->mNamespaces.back(), kVariables[op.a], kValues[op.b]); break;
        case Op::Reset: CallFace<void>(f.reset, s.parser->Face()); break;
        case Op::ResetBlocks: CallFace<void>(f.resetBlocks, s.parser->Face()); break;
        }
    }
}

} // namespace

// Slots 20 (open), 21 (close) and 22 (purge) and Reset / ResetBlocks over random runs, tracing on and off: the
// namespace tree (each namespace's variables, children, pool and use count), the stack, the path and the trace text.
DIFF_TEST(cBaseParser_Namespaces, 0xb05770)
{
    std::mt19937 gen(0xb05770);
    const Fns orig{ctx.image.At(0xb05770), ctx.image.At(0xb055b0), ctx.image.At(0xb05640), ctx.image.At(0xaff120), ctx.image.At(0xb03e50)};
    const Fns ours{Ours(&cBaseParserAdapters::PushNamespace), Ours(&cBaseParserAdapters::PopNamespace),
                   Ours(&cBaseParserAdapters::PurgeNamespace), Ours(&cBaseParserAdapters::Reset), Ours(&cBaseParserAdapters::ResetBlocks)};
    for (int round = 0; round < 400; ++round) {
        Fakes fakes;
        void *output = fakes.Add(30);
        const bool trace = round % 3 == 0;
        const std::vector<Op> ops = MakeOps(gen);
        auto setup = [&](Side &s) {
            if (!trace) return;
            s.parser->bytes[0xe9] = 1;
            Put(s.parser, 0x14, Addr(output));
        };
        auto done = [&](Side &s) {
            if (trace) Put(s.parser, 0x14, 0);
            return 0;
        };
        Compare(
            ctx, "namespaces", setup, [&](Side &s) { Run(s, ops, orig); return done(s); },
            [&](Side &s) { Run(s, ops, ours); return done(s); });
    }
}

// The child table's find-or-add (0xafce70) and its rehash (0xb07790) directly: up to 40 names into one namespace's
// child table, so it grows past 8 and 64 buckets; the table compared after each insert, and the returned node and flag.
DIFF_TEST(cBaseParser_InsertChild, 0xafce70)
{
    std::mt19937 gen(0xafce70);
    static std::vector<std::string> names;
    if (names.empty())
        for (int i = 0; i < 80; ++i) names.push_back(i % 7 == 0 ? std::string(1, static_cast<char>('A' + i % 26)) : "name" + std::to_string(i * 37 % 101));
    for (int round = 0; round < 40; ++round) {
        std::vector<int> picks;
        for (int i = 0, n = 1 + round; i < n; ++i) picks.push_back(static_cast<int>(gen() % names.size()));
        auto run = [&](Side &s, const void *insert) {
            CommandTable *table = &s.parser->P()->mGlobalNamespace.mCommands;
            std::string seen;
            for (int k : picks) {
                const KeyView key{names[k].c_str(), static_cast<uint32_t>(names[k].size())};
                InsertResult out{};
                reinterpret_cast<InsertResult *(__fastcall *)(CommandTable *, void *, InsertResult *, const KeyView *)>(const_cast<void *>(insert))(
                    table, nullptr, &out, &key);
                seen += std::string(out.node->key, out.node->keyLength) + (out.inserted ? "+" : "=") + ";";
                if (out.inserted) GiveNamespace(ctx, out.node);
            }
            s.extra = seen;
            return 0;
        };
        Compare(
            ctx, "insert child", [](Side &) {}, [&](Side &s) { return run(s, ctx.image.At(0xafce70)); },
            [&](Side &s) { return run(s, Ours(&cBaseParserAdapters::InsertChild)); });
    }
    // The rehash on its own: a table of 1..30 names rehashed to 8..1024 buckets.
    for (int round = 0; round < 60; ++round) {
        const int count = 1 + static_cast<int>(gen() % 30);
        const uint32_t to = 8u << (gen() % 8);
        auto run = [&](Side &s, const void *rehash) {
            CommandTable *table = &s.parser->P()->mGlobalNamespace.mCommands;
            for (int i = 0; i < count; ++i) {
                const KeyView key{names[(round + i * 3) % names.size()].c_str(), static_cast<uint32_t>(names[(round + i * 3) % names.size()].size())};
                InsertResult out{};
                table->Insert(&out, &key);
                if (out.inserted) GiveNamespace(ctx, out.node);
            }
            reinterpret_cast<void(__fastcall *)(CommandTable *, void *, uint32_t)>(const_cast<void *>(rehash))(table, nullptr, to);
            return 0;
        };
        Compare(
            ctx, "rehash children", [](Side &) {}, [&](Side &s) { return run(s, ctx.image.At(0xb07790)); },
            [&](Side &s) { return run(s, Ours(&cBaseParserAdapters::RehashChildren)); });
    }
}

namespace {

// The side's heap, from its start (before the parser is allocated) to the end of the destructor call, on this thread
// only (difftest::AllocationCounter and FreeRecorder: exact, and blind to other threads; lane MH replaced heap walks
// that a full-suite run's other threads could fool). Begin() starts it, Built() marks the call's start, Call() records
// the call's frees, Freed() ends it.
struct SideHeap {
    std::optional<difftest::AllocationCounter> counter;
    std::optional<difftest::FreeRecorder> frees;
    size_t built = 0;
    long long blocks = 0, bytes = 0;
    void Begin()
    {
        frees.reset();
        counter.reset();
        counter.emplace();
    }
    void Built()
    {
        built = counter->Allocations();
        blocks = counter->Blocks();
        bytes = counter->Bytes();
        frees.emplace();
    }
    // What the call freed of the blocks allocated since the side began (the parser and what it holds; a block freed
    // twice counts twice): their sizes, sorted; then what the call itself left allocated minus freed, in blocks and bytes.
    std::string Freed()
    {
        frees->Stop();
        counter->Stop();
        if (frees->Overflowed() || counter->Overflowed()) return "(heap record incomplete)";
        std::vector<size_t> sizes;
        for (size_t i = 0; i < frees->Count(); ++i)
            if (counter->Allocated(frees->Pointer(i), built)) sizes.push_back(frees->Size(i));
        std::sort(sizes.begin(), sizes.end());
        std::string s = std::to_string(sizes.size()) + " blocks:";
        for (size_t n : sizes) s += " " + std::to_string(n);
        s += "; call net " + std::to_string(counter->Blocks() - blocks) + " blocks " + std::to_string(counter->Bytes() - bytes) + " bytes";
        return s;
    }
};
SideHeap gHeap;

// A parser with something in every member the destructor tears down.
struct Contents {
    Fakes fakes;
    std::vector<void *> commands, metas;
    std::vector<uint32_t> definitions, pending;
    void *current = nullptr, *output = nullptr, *handler = nullptr, *source = nullptr;
    std::vector<Op> ops;
    bool initialised = false;
    int listItems = 0;
    const char *text = "", *file = "";
    Contents(std::mt19937 &gen, int round)
    {
        for (int i = 0, n = static_cast<int>(gen() % 5); i < n; ++i) commands.push_back(fakes.Add(i, kNames[gen() % std::size(kNames)]));
        for (int i = 0, n = static_cast<int>(gen() % 3); i < n; ++i) {
            metas.push_back(fakes.Add(10 + i, kVariables[gen() % std::size(kVariables)]));
            fakes.vtables.back()->ret[4] = 0x7000 + i;
        }
        for (int i = 0, n = static_cast<int>(gen() % 4); i < n; ++i) definitions.push_back(gen() % 5 ? Addr(fakes.Add(20 + i, "def")) : 0);
        for (int i = 0, n = static_cast<int>(gen() % 4); i < n; ++i) pending.push_back(gen() % 4 ? Addr(fakes.Add(30 + i, "pending")) : 0);
        if (gen() % 2) current = fakes.Add(40, "current");
        if (gen() % 3) output = fakes.Add(41);
        if (gen() % 2) handler = fakes.Add(42);
        if (gen() % 2) source = fakes.Add(43);
        ops = MakeOps(gen, true);
        initialised = round % 2 != 0;
        listItems = static_cast<int>(gen() % 3);
        text = kValues[gen() % std::size(kValues)];
        file = kValues[gen() % std::size(kValues)];
    }
    void Build(difftest::Context &ctx, Side &s) const
    {
        void *face = s.parser->Face();
        for (void *c : commands) CallFace<void>(ctx.image.At(0xb059e0), face, c);
        for (void *m : metas) CallFace<void>(ctx.image.At(0xb05b50), face, m);
        cBaseParser *p = s.parser->P();
        auto *defs = static_cast<void **>(::operator new((definitions.size() + 1) * 4));
        for (size_t i = 0; i < definitions.size(); ++i) defs[i] = reinterpret_cast<void *>(static_cast<uintptr_t>(definitions[i]));
        p->mDefinitions = {defs, defs + definitions.size(), defs + definitions.size() + 1};
        if (!pending.empty()) {
            auto *calls = static_cast<PendingCall *>(::operator new(pending.size() * sizeof(PendingCall)));
            std::memset(calls, 0, pending.size() * sizeof(PendingCall));
            for (size_t i = 0; i < pending.size(); ++i) calls[i].held.definition = reinterpret_cast<void *>(static_cast<uintptr_t>(pending[i]));
            p->mPendingCalls = {calls, calls + pending.size(), calls + pending.size()};
        }
        p->mCurrentDefinition = current;
        p->mOutput = output;
        p->mHandler = handler;
        if (source) {
            // The constructor's own source (a game object, whose release nobody logs) swapped for a fake, so the
            // order of the releases at the end shows.
            VirtualCall<uint32_t(__thiscall *)(void *), 2>(p->mSource);
            p->mSource = static_cast<cFileSource *>(source);
        }
        const Fns orig{ctx.image.At(0xb05770), ctx.image.At(0xb055b0), ctx.image.At(0xb05640), ctx.image.At(0xaff120), ctx.image.At(0xb03e50)};
        Run(s, ops, orig);
        for (int i = 0; i < listItems; ++i) {
            p->mList19C.push_back({});
            p->mList1A4.push_back({});
        }
        p->mText.mString.assign(text);
        p->mFileName.assign(file);
        p->mInitialised = initialised;
    }
};

// What the destructor leaves in the object, side A against side B: each byte equal after, or (where the sides held
// different heap addresses before) unchanged on both sides. Returns the first offset that is neither, or -1.
int AfterMismatch(const uint8_t *beforeA, const uint8_t *afterA, const uint8_t *beforeB, const uint8_t *afterB, size_t size = sizeof(cBaseParser))
{
    for (int i = 0; i < static_cast<int>(size); ++i)
        if (afterA[i] != afterB[i] && !(afterA[i] == beforeA[i] && afterB[i] == beforeB[i])) return i;
    return -1;
}

} // namespace

// The destructor (0xafe1b0) and the deleting destructor (0xaff070, flags 0 and 1) on parsers holding commands, meta
// commands, definitions, pending calls, a current definition, output, handler, namespaces with variables, list items,
// text and a file name, initialised or not (Shutdown first). Compared: the calls on the fakes (releases, in order), what
// is left in the object, the heap blocks freed (their sizes) and the call's allocated-minus-freed blocks and bytes
// (SideHeap; the net is new with lane MH), and the deleting form's return.
DIFF_TEST(cBaseParser_Destruct, 0xafe1b0)
{
    std::mt19937 gen(0xafe1b0);
    for (int round = 0; round < 90; ++round) {
        const Contents contents(gen, round);
        const int form = round % 3; // 0: the destructor, 1: deleting with 0, 2: deleting with 1
        struct Result {
            cptest::Outcome outcome;
            std::string calls;
            uint8_t before[sizeof(cBaseParser)], after[sizeof(cBaseParser)];
            std::string freed;
        } results[2];
        for (int side = 0; side < 2; ++side) {
            gHeap.Begin();
            // The deleting destructor frees with operator delete: the parser comes from operator new here.
            auto *block = static_cast<ParserBlock *>(::operator new(sizeof(ParserBlock)));
            std::memset(block->bytes, 0x5a, sizeof(block->bytes));
            ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kConstruct)(block->bytes, 1);
            block->bytes[0x2c] = 0;
            std::memset(block->bytes + 0xe4, 0, 4);
            block->bytes[0xef] = 0;
            ctx.Original<void(__thiscall *)(void *)>(kReset)(block->Face());
            Side s{block, {}};
            gBase = block->bytes;
            contents.Build(ctx, s);
            std::memcpy(results[side].before, block->bytes, sizeof(cBaseParser));
            gCalls.clear();
            gCalls.reserve(256);
            gHeap.Built();
            const void *fn = side == 0 ? (form == 0 ? ctx.image.At(0xafe1b0) : ctx.image.At(0xaff070))
                                       : (form == 0 ? MemberAddress(&cBaseParser::Destruct) : Ours(&cBaseParserAdapters::DeletingDestructor));
            cptest::Outcome o = RunGuarded([&]() -> uint32_t {
                if (form == 0) {
                    reinterpret_cast<void(__fastcall *)(void *, void *)>(const_cast<void *>(fn))(block->bytes, nullptr);
                    return 0;
                }
                void *r = reinterpret_cast<void *(__fastcall *)(void *, void *, uint32_t)>(const_cast<void *>(fn))(block->bytes, nullptr, form - 1);
                return r == block->bytes ? 1u : 2u;
            });
            results[side].freed = gHeap.Freed();
            results[side].outcome = o;
            results[side].calls = Describe(gCalls);
            if (form != 2) {
                std::memcpy(results[side].after, block->bytes, sizeof(cBaseParser));
                ::operator delete(block);
            }
            gCalls.clear();
            gBase = nullptr;
        }
        ++ctx.cases;
        char what[48];
        std::snprintf(what, sizeof(what), "destruct round %d form %d", round, form);
        if (!(results[0].outcome == results[1].outcome))
            ctx.Fail("%s: outcome %08x threw %d %s / %08x threw %d %s", what, results[0].outcome.ret, results[0].outcome.threw,
                     results[0].outcome.detail.c_str(), results[1].outcome.ret, results[1].outcome.threw, results[1].outcome.detail.c_str());
        if (results[0].calls != results[1].calls) ctx.Fail("%s: calls\n    A: %s\n    B: %s", what, results[0].calls.c_str(), results[1].calls.c_str());
        if (form != 2) {
            const int at = AfterMismatch(results[0].before, results[0].after, results[1].before, results[1].after);
            if (at >= 0)
                ctx.Fail("%s: object after differs at +%x: %02x (was %02x) / %02x (was %02x)", what, at, results[0].after[at], results[0].before[at],
                         results[1].after[at], results[1].before[at]);
        }
        if (results[0].freed != results[1].freed)
            ctx.Fail("%s: freed\n    A: %s\n    B: %s", what, results[0].freed.c_str(), results[1].freed.c_str());
    }
}

// The destructor on a parser constructed as the base of a derived class (mostDerived 0): the derived class's vbtable,
// stored at +8 before construction, puts the virtual bases `extra` bytes further out, so the vtordisps the destructor
// writes are `extra` (0 in a whole object) and the faces' vtables land past the cBaseParser part. The vtables and
// vtordisps start as a derived destructor leaves them (not the class's own), so each store shows. The whole block is
// compared afterwards, with the calls and the heap freed (SideHeap, as above).
DIFF_TEST(cBaseParser_DestructDerived, 0xafe1b0)
{
    constexpr size_t kSize = sizeof(cBaseParser) + 0x40;
    for (int round = 0; round < 16; ++round) {
        const int32_t extra = 4 * (round % 8) + (round >= 8 ? 0x20 : 0);
        alignas(8) static int32_t derivedVbTable[3];
        derivedVbTable[0] = -8;
        derivedVbTable[1] = cBaseParser::kUnknownOffset + extra;
        derivedVbTable[2] = cBaseParser::kParserOffset + extra;
        struct Result {
            uint8_t bytes[kSize]; // first: the heap block's 8-byte alignment
            uint8_t before[kSize];
            cptest::Outcome outcome;
            std::string calls;
            std::string freed;
        };
        auto results = std::make_unique<Result[]>(2);
        for (int side = 0; side < 2; ++side) {
            uint8_t *bytes = results[side].bytes;
            std::memset(bytes, 0x5a, kSize);
            gHeap.Begin();
            const uint32_t vb = Addr(derivedVbTable);
            std::memcpy(bytes + 8, &vb, 4);
            ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kConstruct)(bytes, 0);
            bytes[0x2c] = 0; // as NewParser: not written by the constructor
            std::memset(bytes + 0xe4, 0, 4);
            bytes[0xef] = 0;
            // As a derived class's destructor leaves the object before it calls this one: its own vtables in the three
            // slots and its own vtordisps (stand-in values); the base destructor puts the class's own back.
            const uint32_t marks[5] = {0x0badf00d, 0x0badf10d, 0x0badf20d, 0x3c3c3c3c, 0x4d4d4d4d};
            std::memcpy(bytes, &marks[0], 4);
            // The vbtable's offsets are from the vbptr at +8; each vtordisp is the word before its face.
            std::memcpy(bytes + 8 + cBaseParser::kUnknownOffset + extra, &marks[1], 4);
            std::memcpy(bytes + 8 + cBaseParser::kParserOffset + extra, &marks[2], 4);
            std::memcpy(bytes + 4 + cBaseParser::kUnknownOffset + extra, &marks[3], 4);
            std::memcpy(bytes + 4 + cBaseParser::kParserOffset + extra, &marks[4], 4);
            std::memcpy(results[side].before, bytes, kSize);
            gCalls.clear();
            gHeap.Built();
            const void *fn = side == 0 ? ctx.image.At(0xafe1b0) : MemberAddress(&cBaseParser::Destruct);
            results[side].outcome = RunGuarded([&]() -> uint32_t {
                reinterpret_cast<void(__fastcall *)(void *, void *)>(const_cast<void *>(fn))(bytes, nullptr);
                return 0;
            });
            results[side].freed = gHeap.Freed();
            results[side].calls = Describe(gCalls);
            gCalls.clear();
        }
        ++ctx.cases;
        const Result &a = results[0], &b = results[1];
        if (!(a.outcome == b.outcome))
            ctx.Fail("extra %d: outcome %08x threw %d %s / %08x threw %d %s", extra, a.outcome.ret, a.outcome.threw, a.outcome.detail.c_str(),
                     b.outcome.ret, b.outcome.threw, b.outcome.detail.c_str());
        if (a.calls != b.calls) ctx.Fail("extra %d: calls\n    A: %s\n    B: %s", extra, a.calls.c_str(), b.calls.c_str());
        const int at = AfterMismatch(a.before, a.bytes, b.before, b.bytes, kSize);
        if (at >= 0)
            ctx.Fail("extra %d: block after differs at +%x: %02x (was %02x) / %02x (was %02x)", extra, at, a.bytes[at], a.before[at], b.bytes[at], b.before[at]);
        if (a.freed != b.freed)
            ctx.Fail("extra %d: freed\n    A: %s\n    B: %s", extra, a.freed.c_str(), b.freed.c_str());
    }
}

// The child table's node erase (0xafd700) directly: 1..20 names inserted, then erased in a random order (each node's
// namespace destroyed first by the game's own destructor, on both sides). Compared after each erase: the returned
// pointer (the out argument), the node it holds (the next one in the list, or the head), and the table.
DIFF_TEST(cBaseParser_EraseChildNode, 0xafd700)
{
    std::mt19937 gen(0xafd700);
    static const char *const kKeys[] = {"a", "i", "q", "y", "b", "j", "Alpha", "beta", "gamma", "delta", "eps", "zeta", "eta",
                                        "theta", "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron", "pi"};
    for (int round = 0; round < 60; ++round) {
        const int count = 1 + static_cast<int>(gen() % 20);
        const int erases = 1 + static_cast<int>(gen() % count);
        const uint32_t seed = gen();
        auto run = [&](Side &s, const void *erase) {
            CommandTable *table = &s.parser->P()->mGlobalNamespace.mCommands;
            for (int i = 0; i < count; ++i) {
                const KeyView key{kKeys[i], static_cast<uint32_t>(std::strlen(kKeys[i]))};
                InsertResult out{};
                table->Insert(&out, &key);
                if (out.inserted) GiveNamespace(ctx, out.node);
            }
            std::mt19937 pick(seed);
            std::string seen;
            for (int k = 0; k < erases && table->size; ++k) {
                CommandNode *node = table->head->next;
                for (uint32_t steps = pick() % table->size; steps; --steps) node = node->next;
                void *ns = node->value;
                ctx.Original<void(__thiscall *)(void *)>(0xafe800)(ns);
                ::operator delete(ns);
                auto *next = reinterpret_cast<CommandNode *>(static_cast<uintptr_t>(0x1234));
                CommandNode **r = reinterpret_cast<CommandNode **(__fastcall *)(CommandTable *, void *, CommandNode **, CommandNode *)>(
                    const_cast<void *>(erase))(table, nullptr, &next, node);
                seen += r == &next ? "" : "(other pointer)";
                seen += next == table->head ? std::string("end") : std::string(next->key, next->keyLength);
                seen += ";";
            }
            s.extra = seen;
            return 0;
        };
        Compare(
            ctx, "erase child", [](Side &) {}, [&](Side &s) { return run(s, ctx.image.At(0xafd700)); },
            [&](Side &s) { return run(s, Ours(&cBaseParserAdapters::EraseChildNode)); });
    }
}

// Our cArguments' vtable (the compiler's), read from an object our constructor built.
static uint32_t OurArgumentsVTable()
{
    nGZCommandParser::cArguments probe;
    uint32_t word;
    std::memcpy(&word, static_cast<const void *>(&probe), 4);
    return word;
}

// The pending calls' helpers directly: 0xafc470 over a range (the arguments' destructor stores cArguments' vtable in
// each element; the definitions released in order) and 0xafe000 over the whole vector (then freed and nulled).
DIFF_TEST(cBaseParser_PendingCalls, 0xafc470)
{
    std::mt19937 gen(0xafc470);
    for (int round = 0; round < 80; ++round) {
        Fakes fakes;
        std::vector<uint32_t> definitions;
        for (int i = 0, n = static_cast<int>(gen() % 6); i < n; ++i) definitions.push_back(gen() % 4 ? Addr(fakes.Add(30 + i, "pending")) : 0);
        const size_t spare = gen() % 3;
        const size_t from = definitions.empty() ? 0 : gen() % definitions.size();
        const bool whole = round % 2 != 0;
        auto setup = [&](Side &s) {
            cBaseParser *p = s.parser->P();
            if (definitions.empty() && round % 4 == 0) return; // an empty vector (no storage)
            auto *calls = static_cast<PendingCall *>(::operator new((definitions.size() + spare + 1) * sizeof(PendingCall)));
            std::memset(calls, 0, (definitions.size() + spare + 1) * sizeof(PendingCall));
            for (size_t i = 0; i < definitions.size(); ++i) {
                calls[i].held.definition = reinterpret_cast<void *>(static_cast<uintptr_t>(definitions[i]));
                std::memcpy(static_cast<void *>(&calls[i].arguments), "\x11\x22\x33\x44", 4);
            }
            p->mPendingCalls = {calls, calls + definitions.size(), calls + definitions.size() + spare + 1};
        };
        auto run = [&](Side &s, bool orig) {
            cBaseParser *p = s.parser->P();
            auto *calls = static_cast<PendingCall *>(p->mPendingCalls.first);
            if (whole) {
                const void *fn = orig ? ctx.image.At(0xafe000) : Ours(&cBaseParserAdapters::DestroyPendingCallVector);
                reinterpret_cast<void(__fastcall *)(void *, void *)>(const_cast<void *>(fn))(&p->mPendingCalls, nullptr);
                return 0;
            }
            const void *fn = orig ? ctx.image.At(0xafc470) : Ours(&DestroyPendingCalls);
            reinterpret_cast<void(__cdecl *)(void *, void *, void *)>(const_cast<void *>(fn))(calls + from, static_cast<PendingCall *>(p->mPendingCalls.last),
                                                                                     &p->mPendingCalls);
            // What the arguments' destructor left: the vtable word of each destroyed element, as an image offset; ours
            // (cArguments is in the standalone shape: our destructor stores our class's vtable) as the game's.
            std::string &x = s.extra;
            const uint32_t ourVTable = OurArgumentsVTable();
            for (PendingCall *c = calls + from; c != p->mPendingCalls.last; ++c) {
                uint32_t vt;
                std::memcpy(&vt, static_cast<const void *>(&c->arguments), 4);
                if (vt == ourVTable) vt = static_cast<uint32_t>(ctx.image.Base()) + nGZCommandParser::cArguments::kVTable;
                char b[16];
                std::snprintf(b, sizeof(b), "%x,", vt - static_cast<uint32_t>(ctx.image.Base()));
                x += b;
            }
            p->mPendingCalls.last = calls + from; // the destroyed ones are gone (the parser's destructor must not see them)
            return 0;
        };
        Compare(
            ctx, "pending calls", setup, [&](Side &s) { return run(s, true); }, [&](Side &s) { return run(s, false); });
    }
}

namespace {

const char *const kLookups[] = {"x",   ":x",  "a:x",  ":a:x",  "a:b:x",   "b:x",   "Alpha:a:y", "nope",  "a:",   ":",  "",
                                "a::x", "count", "a:count", "c:c:q", "a_long_variable_name", "a:b:", "zz:", "i:x", "q:y", ":i:A", "a:i:zz"};

const char *const kTexts[] = {
    "no dollars", "$x", "${x}", "pre $x post", "$$x", "$$", "a$", "${x", "${}", "$1abc", "$ x", "$:x",
    "$a:x and ${a:b:x}", "${nope}", "$nope", "$x$y$zz", "${count}s", "$x.y", "x=$x;", "$_", "$A$$q", "${a:x}${:y}",
    "trailing $", "$y-$x", "{$x}", "$$$x", "${x}}", "$x:", "$:", "${:}",
    // The characters either side of the name classes' ranges, and bytes past 0x7f (lane RQ: the class test is ours).
    "$x@", "$@x", "$x[", "$[x", "$x`", "$`x", "$x{", "$x/", "$/x", "$x\xc0", "$\xe9x", "${x\xff}", "$Zx", "$z9_", "$9", "$0x",
};

// The tree the lookups run on: random opens, closes, purges and variables through the game's slots, with some
// namespaces left open.
void BuildTree(difftest::Context &ctx, Side &s, const std::vector<Op> &ops)
{
    const Fns orig{ctx.image.At(0xb05770), ctx.image.At(0xb055b0), ctx.image.At(0xb05640), ctx.image.At(0xaff120), ctx.image.At(0xb03e50)};
    Run(s, ops, orig);
}

} // namespace

// Slot 27: a variable through the open namespaces (innermost first; a leading ':' only the outermost; "a:b:x" through
// the child namespaces), tracing on and off. The value compared by its text (each side's string is its own).
DIFF_TEST(cBaseParser_FindVariable, 0xb07520)
{
    std::mt19937 gen(0xb07520);
    for (int round = 0; round < 120; ++round) {
        Fakes fakes;
        void *output = fakes.Add(30);
        const bool trace = round % 2 != 0;
        const std::vector<Op> ops = MakeOps(gen);
        for (const char *name : kLookups) {
            auto setup = [&](Side &s) {
                BuildTree(ctx, s, ops);
                if (!trace) return;
                s.parser->bytes[0xeb] = 1;
                Put(s.parser, 0x14, Addr(output));
            };
            auto run = [&](Side &s, const void *fn) {
                const char *value = CallFace<const char *>(fn, s.parser->Face(), name);
                s.extra = value ? std::string("'") + value + "'" : std::string("null");
                if (trace) Put(s.parser, 0x14, 0);
                return 0;
            };
            Compare(
                ctx, name, setup, [&](Side &s) { return run(s, ctx.image.At(0xb07520)); },
                [&](Side &s) { return run(s, Ours(&cBaseParserAdapters::FindVariable)); });
        }
    }
}

// Slot 29: the references in a cRZString replaced by the values slot 27 finds (the game's slot 27 through the face
// on both sides), with and without the parentheses around $name values; the errors (no name, a bad first character,
// no '}', an unknown variable, a text over 0x7ff bytes) compared as thrown cErrors.
DIFF_TEST(cBaseParser_ExpandVariables, 0xb06fb0)
{
    std::mt19937 gen(0xb06fb0);
    std::vector<std::string> texts(std::begin(kTexts), std::end(kTexts));
    texts.push_back(std::string(0x7ff, 'z') + "$");  // 0x800 bytes: too long
    texts.push_back(std::string(0x7fd, 'z') + "$x"); // 0x7ff bytes: the longest that fits
    texts.push_back(std::string(0x400, '$'));        // dollars only
    texts.push_back(std::string(0x900, 'z'));        // long, no '$': left as it is
    for (int round = 0; round < 40; ++round) {
        const std::vector<Op> ops = MakeOps(gen);
        for (const std::string &text : texts) {
            for (int parenthesize = 0; parenthesize < 2; ++parenthesize) {
                auto run = [&](Side &s, const void *fn) {
                    cRZString string;
                    string.Construct(text.c_str());
                    struct Keep {
                        Side &s;
                        cRZString &string;
                        ~Keep()
                        {
                            s.extra = "text '" + string.mString + "'";
                            string.Destruct();
                        }
                    } keep{s, string};
                    CallFace<void>(fn, s.parser->Face(), static_cast<void *>(&string), parenthesize != 0);
                    return 0;
                };
                char what[64];
                std::snprintf(what, sizeof(what), "expand '%.24s' (%zu) %d", text.c_str(), text.size(), parenthesize);
                Compare(
                    ctx, what, [&](Side &s) { BuildTree(ctx, s, ops); }, [&](Side &s) { return run(s, ctx.image.At(0xb06fb0)); },
                    [&](Side &s) { return run(s, Ours(&cBaseParserAdapters::ExpandVariables)); });
            }
        }
    }
}
