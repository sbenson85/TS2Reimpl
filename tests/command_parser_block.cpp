// Differential tests for the block command's command table: the name pool (0xaf66b0, 0xaf69b0,
// 0xaf6770), the hash table (0xaf7420 find, 0xaf7560 insert, 0xaf7da0 rehash, 0xaf74e0, 0xaf7870),
// FoldName (0xaf6be0), the block's constructor (0xaf7750), its slots 10 and 11 (0xaf7cd0, 0xaf7a90)
// and its destructors (0xaf7970, 0xaf79c0, 0x2c2160).
//
// The original and the replacement work on two separate objects built the same way (by the
// original's constructor), driven with the same operations. After each operation the structures are
// dumped as data (nodes in list order with their keys, the bucket vector as node indexes, counts,
// capacities: no addresses), and the dumps, the results and the calls on the fake commands compared.
#include "command_parser_env.h"
#include "free_recorder.h"

#include <windows.h>

#include <memory>
#include <unordered_map>

namespace {

using cp::CommandNode;
using cp::CommandTable;
using cp::FindResult;
using cp::InsertResult;
using cp::KeyView;
using cp::NamePool;

// Runs `call` (guarded) under a FreeRecorder: *kept = how many of `blocks` it did not free; *objectFreed whether it
// freed `object`. Exact, unlike HeapValidate (a freed block may be reused; a large one looks freed either way).
template <class F>
Outcome RunKept(const std::vector<const void *> &blocks, size_t *kept, F &&call, const void *object = nullptr, bool *objectFreed = nullptr)
{
    difftest::FreeRecorder rec;
    Outcome o = RunGuarded(call);
    rec.Stop();
    *kept = 0;
    for (const void *p : blocks)
        if (!rec.Freed(p)) ++*kept;
    if (objectFreed) *objectFreed = rec.Freed(object);
    return o;
}

template <class T> void Put(std::vector<uint8_t> &out, const T &v)
{
    const auto *p = reinterpret_cast<const uint8_t *>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

// The table as data. `nodeIndex` maps nodes to their place in the list (the sentinel is -1).
std::vector<uint8_t> DumpTable(const CommandTable &t)
{
    std::vector<uint8_t> out;
    std::unordered_map<const CommandNode *, int32_t> index;
    index[t.head] = -1;
    int32_t n = 0;
    for (const CommandNode *node = t.head->next; node != t.head && n < 100000; node = node->next) index[node] = n++;
    Put(out, t.maxLoad);
    Put(out, t.size);
    Put(out, t.mask);
    Put(out, t.bucketCount);
    Put(out, static_cast<uint32_t>(t.buckets.size()));
    Put(out, static_cast<uint32_t>(t.buckets.capacity()));
    Put(out, static_cast<uint32_t>(n));
    for (const CommandNode *node = t.head->next; node != t.head && n-- > 0; node = node->next) {
        Put(out, node->keyLength);
        out.insert(out.end(), node->key, node->key + node->keyLength);
        Put(out, node->value);
        Put(out, index[node->prev]);
    }
    for (const CommandNode *b : t.buckets) {
        auto it = index.find(b);
        Put(out, it == index.end() ? int32_t(-2) : it->second);
    }
    return out;
}

std::vector<uint8_t> DumpPool(const NamePool &p)
{
    std::vector<uint8_t> out;
    Put(out, p.count);
    Put(out, p.used);
    uint32_t chunks = 0;
    for (const NamePool::Chunk *c = p.head->next; c != p.head && chunks < 100000; c = c->next) ++chunks;
    Put(out, chunks);
    return out;
}

// A fake command: slot 1 AddRef, 2 Release, 3 SetContext(parser, value), 5 GetName.
struct FakeCmd {
    void *vtable;
    const char *name;
};

uint32_t __fastcall CmdGetName(void *self, void *)
{
    Record(self, 5);
    return reinterpret_cast<uint32_t>(static_cast<FakeCmd *>(self)->name);
}

struct Fakes {
    FakeVTable vt{10};
    FakeCmd cmds[7];
    Fakes()
    {
        vt.Set<1>(0);
        vt.Set<2>(0);
        vt.Set<3>(2);
        vt.slots[5] = reinterpret_cast<void *>(&CmdGetName);
        // The last name is exactly 0x80 characters: a folding block cannot look it up.
        static const std::string longName(0x80, 'w');
        const char *const names[] = {"alpha", "beta", "Gamma", "delta-delta-delta-delta", "x", "ALPHA", longName.c_str()};
        for (int i = 0; i < 7; ++i) {
            cmds[i].vtable = vt.slots;
            cmds[i].name = names[i];
        }
    }
};

// Two blocks (original side and replacement side) built by the original constructor.
struct BlockPair {
    alignas(8) uint8_t raw[2][0x50];
    cp::cBlockCommandBase *Block(int i) { return reinterpret_cast<cp::cBlockCommandBase *>(raw[i]); }
    BlockPair(difftest::Context &ctx, std::mt19937 &gen, bool caseSensitive)
    {
        auto ctor = ctx.Original<void *(__thiscall *)(void *)>(0xaf7750);
        for (int i = 0; i < 2; ++i) {
            std::memset(raw[i], 0xCD, sizeof(raw[i]));
            ctor(raw[i]);
            Block(i)->mFlag = caseSensitive;
            Block(i)->mParser = reinterpret_cast<void *>(0x1000 + 7);
            Block(i)->mValue = 0x2000 + 9;
        }
        (void)gen;
    }
    // The command table and name pool of one side as data.
    std::vector<uint8_t> Dump(int i)
    {
        std::vector<uint8_t> d = DumpTable(Block(i)->mCommands);
        std::vector<uint8_t> p = DumpPool(Block(i)->mNames);
        d.insert(d.end(), p.begin(), p.end());
        return d;
    }
    void Destroy(difftest::Context &ctx)
    {
        auto dtor = ctx.Original<void(__thiscall *)(void *)>(0xaf7970);
        dtor(raw[0]);
        dtor(raw[1]);
    }
};

// A name for the tables: from a small alphabet so that keys collide, with high bytes.
std::string RandomKey(std::mt19937 &gen, size_t maxLength)
{
    static const char alphabet[] = {'a', 'b', 'c', 'A', 'Z', '_', '\xe9', '\x80', '0', '\0'};
    size_t length = gen() % (maxLength + 1);
    std::string s;
    for (size_t i = 0; i < length; ++i) s += alphabet[gen() % sizeof(alphabet)];
    return s;
}

// Runs one operation on both sides and compares what came back, the dumps and the calls.
template <class OpOrig, class OpOurs>
bool Step(difftest::Context &ctx, const char *what, BlockPair &pair, OpOrig opOrig, OpOurs opOurs)
{
    int side = 0;
    return Differential(
        ctx, what, {}, [&] {
            side = 0;
            return RunGuarded(opOrig);
        },
        [&] {
            side = 1;
            return RunGuarded(opOurs);
        },
        [&] {
            std::vector<uint8_t> d = pair.Dump(side);
            gExtra.insert(gExtra.end(), d.begin(), d.end());
        });
}

} // namespace

// ---- the constructor and the pool ------------------------------------------------------------------

DIFF_TEST(cBlockCommandBase_Construct, 0xaf7750)
{
    std::mt19937 gen(0xaf7750);
    for (int i = 0; i < 20; ++i) {
        // The same garbage in two objects: the original constructor runs on one, ours on the other.
        alignas(8) uint8_t theirs[0x50], ours[0x50];
        for (auto &byte : theirs) byte = static_cast<uint8_t>(gen());
        std::memcpy(ours, theirs, sizeof(ours));
        auto ctor = ctx.Original<void *(__thiscall *)(void *)>(0xaf7750);
        ctor(theirs);
        auto *a = reinterpret_cast<cp::cBlockCommandBase *>(theirs);
        auto *b = reinterpret_cast<cp::cBlockCommandBase *>(ours);
        b->Construct();
        ++ctx.cases;
        if (DumpTable(a->mCommands) != DumpTable(b->mCommands)) ctx.Fail("Construct: table differs");
        if (DumpPool(a->mNames) != DumpPool(b->mNames)) ctx.Fail("Construct: pool differs");
        // The words the constructor sets (the first 0x10 bytes, the table's and pool's own words are
        // compared as data above), and the ones it does not touch: the flag, the padding and the state.
        if (std::memcmp(theirs, ours, 0x10) != 0) ctx.Fail("Construct: the first 0x10 bytes differ");
        for (size_t k = 0x3c; k < 0x50; ++k)
            if (ours[k] != theirs[k]) ctx.Fail("Construct: byte +%zx differs", k);
        a->mNames.Destruct();
        a->mCommands.Destruct();
        b->mNames.Destruct();
        b->mCommands.Destruct();
    }
}

DIFF_TEST(cMetaCommand_Construct, 0xaf7860)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xaf7860);
    std::mt19937 gen(0xaf7860);
    for (int i = 0; i < 50; ++i) {
        alignas(8) uint32_t a[6], b[6];
        for (int k = 0; k < 6; ++k) a[k] = b[k] = gen();
        void *ra = orig(a), *rb = reinterpret_cast<cp::cMetaCommand *>(b)->Construct();
        ++ctx.cases;
        if (ra != a || rb != b || std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("cMetaCommand::Construct differs");
    }
}

DIFF_TEST(CommandNamePool_Construct, 0xaf66b0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xaf66b0);
    std::mt19937 gen(0xaf66b0);
    for (int i = 0; i < 20; ++i) {
        NamePool a, b;
        std::memset(&a, 0xCD, sizeof(a));
        std::memset(&b, 0xCD, sizeof(b));
        void *ra = orig(&a), *rb = b.Construct();
        ++ctx.cases;
        if (ra != &a || rb != &b) ctx.Fail("NamePool::Construct: return value");
        if (DumpPool(a) != DumpPool(b) || a.used != 0x1000 || a.count != 0) ctx.Fail("NamePool::Construct: state differs");
        a.Destruct();
        b.Destruct();
    }
}

DIFF_TEST(CommandNamePool_Store, 0xaf69b0)
{
    auto orig = ctx.Original<KeyView *(__thiscall *)(NamePool *, KeyView *, const KeyView *)>(0xaf69b0);
    std::mt19937 gen(0xaf69b0);
    for (int round = 0; round < 6; ++round) {
        NamePool a, b;
        a.Construct();
        b.Construct();
        for (int i = 0; i < 400; ++i) {
            // Lengths up to a few hundred, so that chunks fill and a new one is taken; never empty
            // (the first empty key would give a pointer made from an uninitialised word).
            size_t length = 1 + (gen() % 8 == 0 ? gen() % 700 : gen() % 20);
            std::string text(length, 'a');
            for (auto &c : text) c = static_cast<char>(gen());
            KeyView key{text.data(), static_cast<uint32_t>(text.size())};
            KeyView outA{}, outB{};
            uint32_t usedBefore = a.used, countBefore = a.count;
            KeyView *ra = orig(&a, &outA, &key);
            KeyView *rb = b.Store(&outB, &key);
            ++ctx.cases;
            if (ra != &outA || rb != &outB) ctx.Fail("Store: return value");
            if (outA.length != outB.length || std::memcmp(outA.ptr, text.data(), length) != 0 || std::memcmp(outB.ptr, text.data(), length) != 0)
                ctx.Fail("Store: copy differs");
            // The copy sits in the last block at the offset `used` was at (or 0 for a new chunk).
            uint32_t offsetA = static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(outA.ptr) - a.head->prev->block);
            uint32_t offsetB = static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(outB.ptr) - b.head->prev->block);
            if (offsetA != offsetB) ctx.Fail("Store: offset %u vs %u (used %u, count %u)", offsetA, offsetB, usedBefore, countBefore);
            if (DumpPool(a) != DumpPool(b)) ctx.Fail("Store: pool state differs");
        }
        a.Destruct();
        b.Destruct();
    }
    // Exact fits: a name that fills a block, and names that end exactly at 0x1000 or one byte over.
    static const std::vector<std::vector<size_t>> sequences = {
        {4096}, {4096, 1}, {100, 3996, 1}, {100, 3997, 1}, {1, 4095, 4095, 1}, {2000, 2096, 2096, 1, 1}, {4095, 1, 1, 4095}};
    for (const auto &lengths : sequences) {
        NamePool a, b;
        a.Construct();
        b.Construct();
        for (size_t length : lengths) {
            std::string text(length, 'k');
            for (auto &c : text) c = static_cast<char>(gen());
            KeyView key{text.data(), static_cast<uint32_t>(text.size())}, outA{}, outB{};
            orig(&a, &outA, &key);
            b.Store(&outB, &key);
            ++ctx.cases;
            uint32_t offsetA = static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(outA.ptr) - a.head->prev->block);
            uint32_t offsetB = static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(outB.ptr) - b.head->prev->block);
            if (offsetA != offsetB || DumpPool(a) != DumpPool(b) || std::memcmp(outB.ptr, text.data(), length) != 0)
                ctx.Fail("Store of %zu bytes (offset %u vs %u): differs", length, offsetA, offsetB);
        }
        a.Destruct();
        b.Destruct();
    }
}

DIFF_TEST(CommandNamePool_Destruct, 0xaf6770)
{
    auto orig = ctx.Original<void(__thiscall *)(NamePool *)>(0xaf6770);
    std::mt19937 gen(0xaf6770);
    for (int round = 0; round < 30; ++round) {
        NamePool pools[2];
        std::vector<const void *> blocks[2];
        for (auto &p : pools) p.Construct();
        size_t chunks = round % 5;
        for (int side = 0; side < 2; ++side) {
            for (size_t c = 0; c < chunks; ++c) {
                KeyView key{"some name", 9};
                KeyView out;
                pools[side].used = 0x1000; // forces a new chunk for each copy
                pools[side].Store(&out, &key);
            }
            for (auto *c = pools[side].head->next; c != pools[side].head; c = c->next) blocks[side].push_back(c->block);
            blocks[side].push_back(pools[side].head);
            for (auto *c = pools[side].head->next; c != pools[side].head; c = c->next) blocks[side].push_back(c);
        }
        bool freed[2][16] = {};
        for (int side = 0; side < 2; ++side) {
            difftest::FreeRecorder rec;
            if (side == 0) orig(&pools[0]);
            else pools[1].Destruct();
            rec.Stop();
            for (size_t k = 0; k < blocks[side].size(); ++k) freed[side][k] = rec.Freed(blocks[side][k]);
        }
        ++ctx.cases;
        if (pools[0].used != pools[1].used || pools[0].used != 0x1000) ctx.Fail("NamePool::Destruct: used %x vs %x", pools[0].used, pools[1].used);
        if (pools[0].count != pools[1].count) ctx.Fail("NamePool::Destruct: count");
        for (int side = 0; side < 2; ++side)
            for (size_t k = 0; k < blocks[side].size(); ++k)
                if (!freed[side][k]) ctx.Fail("NamePool::Destruct: side %d left block %zu allocated", side, k);
    }
}

DIFF_TEST(CommandParser_FoldName, 0xaf6be0)
{
    auto orig = ctx.Original<void(__cdecl *)(KeyView *)>(0xaf6be0);
    std::mt19937 gen(0xaf6be0);
    for (int i = 0; i < 300; ++i) {
        std::string a(gen() % 40, 'x');
        for (auto &c : a) c = static_cast<char>(gen());
        std::string b = a;
        KeyView ka{a.data(), static_cast<uint32_t>(a.size())}, kb{b.data(), static_cast<uint32_t>(b.size())};
        orig(&ka);
        cp::FoldName(&kb);
        ++ctx.cases;
        if (a != b || ka.length != kb.length) ctx.Fail("FoldName differs");
    }
}

// ---- the hash table ---------------------------------------------------------------------------------

DIFF_TEST(CommandTable_Insert, 0xaf7560)
{
    auto orig = ctx.Original<InsertResult *(__thiscall *)(CommandTable *, InsertResult *, const KeyView *)>(0xaf7560);
    std::mt19937 gen(0xaf7560);
    for (int round = 0; round < 12; ++round) {
        BlockPair pair(ctx, gen, false);
        // Keys live as long as the tables; a deque-like vector of owned strings with stable storage.
        std::vector<std::unique_ptr<std::string>> keys;
        size_t steps = round == 11 ? 1100 : 20 + round * 25; // the last round grows the table past 512 buckets
        for (size_t s = 0; s < steps; ++s) {
            keys.push_back(std::make_unique<std::string>(RandomKey(gen, round % 3 == 0 ? 3 : 8)));
            KeyView key{keys.back()->data(), static_cast<uint32_t>(keys.back()->size())};
            InsertResult ra{}, rb{};
            Step(
                ctx, "CommandTable::Insert", pair,
                [&] { orig(&pair.Block(0)->mCommands, &ra, &key); },
                [&] { pair.Block(1)->mCommands.Insert(&rb, &key); });
            // The node found or added: its place in the list (compared through the dumps) and the flag.
            if (ra.inserted != rb.inserted) ctx.Fail("Insert: inserted %d vs %d", ra.inserted, rb.inserted);
            int32_t ia = 0, ib = 0;
            for (CommandNode *n = pair.Block(0)->mCommands.head->next; n != ra.node; n = n->next) ++ia;
            for (CommandNode *n = pair.Block(1)->mCommands.head->next; n != rb.node; n = n->next) ++ib;
            if (ia != ib) ctx.Fail("Insert: node %d vs %d", ia, ib);
        }
        pair.Destroy(ctx);
    }
}

DIFF_TEST(CommandTable_Find, 0xaf7420)
{
    auto orig = ctx.Original<FindResult *(__thiscall *)(const CommandTable *, FindResult *, const KeyView *, uint32_t)>(0xaf7420);
    auto origInsert = ctx.Original<InsertResult *(__thiscall *)(CommandTable *, InsertResult *, const KeyView *)>(0xaf7560);
    std::mt19937 gen(0xaf7420);
    for (int round = 0; round < 8; ++round) {
        BlockPair pair(ctx, gen, false);
        std::vector<std::unique_ptr<std::string>> keys;
        for (size_t s = 0; s < 10 + round * 20; ++s) {
            keys.push_back(std::make_unique<std::string>(RandomKey(gen, 6)));
            KeyView key{keys.back()->data(), static_cast<uint32_t>(keys.back()->size())};
            InsertResult r;
            origInsert(&pair.Block(0)->mCommands, &r, &key); // both sides are built by the original
            origInsert(&pair.Block(1)->mCommands, &r, &key);
        }
        for (int q = 0; q < 200; ++q) {
            std::string probe = gen() % 2 && !keys.empty() ? *keys[gen() % keys.size()] : RandomKey(gen, 6);
            KeyView key{probe.data(), static_cast<uint32_t>(probe.size())};
            uint32_t hash = gen() % 5 == 0 ? static_cast<uint32_t>(gen()) : cp::HashName(probe.data(), key.length);
            FindResult ra{}, rb{};
            const bool origReturnsOut = orig(&pair.Block(0)->mCommands, &ra, &key, hash) == &ra; // game callers read eax
            const bool oursReturnsOut = pair.Block(1)->mCommands.Find(&rb, &key, hash) == &rb;
            if (origReturnsOut != oursReturnsOut) ctx.Fail("Find: returns out %d/%d", origReturnsOut, oursReturnsOut);
            ++ctx.cases;
            auto idx = [&](int side, CommandNode *n) -> int32_t {
                if (!n) return -2;
                int32_t i = 0;
                for (CommandNode *p = pair.Block(side)->mCommands.head->next; p != pair.Block(side)->mCommands.head; p = p->next, ++i)
                    if (p == n) return i;
                return -1;
            };
            if (idx(0, ra.position) != idx(1, rb.position) || idx(0, ra.found) != idx(1, rb.found))
                ctx.Fail("Find: position %d/%d found %d/%d", idx(0, ra.position), idx(1, rb.position), idx(0, ra.found), idx(1, rb.found));
        }
        pair.Destroy(ctx);
    }
}

// ---- tables built by hand (rehash with equal keys, and the destructor) ------------------------------

namespace {

// A table made directly, in a given list order, with the buckets not set up. Keys may repeat.
struct RawTable {
    alignas(8) uint8_t memory[sizeof(CommandTable)];
    CommandTable &T() { return *reinterpret_cast<CommandTable *>(memory); }
    std::vector<const void *> allocations; // every heap block the table owns

    void Build(const std::vector<std::string *> &keys, const std::vector<void *> &values, uint32_t initialBuckets)
    {
        std::memset(memory, 0xCD, sizeof(memory));
        CommandTable &t = T();
        t.maxLoad = 1.0f;
        t.head = static_cast<CommandNode *>(::operator new(sizeof(CommandNode)));
        t.head->next = t.head->prev = t.head;
        t.size = static_cast<uint32_t>(keys.size());
        new (&t.buckets) std::vector<CommandNode *>();
        t.buckets.assign(2 * static_cast<size_t>(initialBuckets), t.head);
        t.mask = initialBuckets - 1;
        t.bucketCount = initialBuckets;
        for (size_t i = 0; i < keys.size(); ++i) {
            auto *node = static_cast<CommandNode *>(::operator new(sizeof(CommandNode)));
            node->key = keys[i]->data();
            node->keyLength = static_cast<uint32_t>(keys[i]->size());
            node->value = values[i];
            node->prev = t.head->prev;
            node->next = t.head;
            t.head->prev->next = node;
            t.head->prev = node;
        }
    }
    void Allocations()
    {
        allocations.clear();
        allocations.push_back(T().head);
        for (CommandNode *n = T().head->next; n != T().head; n = n->next) allocations.push_back(n);
        if (T().buckets.data()) allocations.push_back(T().buckets.data());
    }
    // The table's own cleanup, written here (not the code under test).
    void Free()
    {
        CommandNode *n = T().head->next;
        while (n != T().head) {
            CommandNode *next = n->next;
            ::operator delete(n);
            n = next;
        }
        ::operator delete(T().head);
        T().buckets.~vector();
    }
};

} // namespace

// Inserts into tables made by hand: other starting bucket counts (down to 2, so the minimum of 8
// matters; 256 and 512, around the 0x200 limit of the 8x growth) and other load limits (fractions
// round the needed count up).
DIFF_TEST(CommandTable_InsertGrowth, 0xaf7560)
{
    auto orig = ctx.Original<InsertResult *(__thiscall *)(CommandTable *, InsertResult *, const KeyView *)>(0xaf7560);
    std::mt19937 gen(0xaf7561);
    struct Case {
        uint32_t buckets;
        float load;
        size_t inserts;
    };
    static const Case cases[] = {{2, 1.0f, 40},  {2, 0.5f, 40},  {4, 0.75f, 60},  {4, 2.0f, 80},   {8, 0.3f, 60},  {16, 1.0f, 100}, {16, 3.5f, 150},
                                 {64, 1.0f, 200}, {256, 1.0f, 400}, {256, 0.7f, 400}, {512, 1.0f, 900}, {512, 1.5f, 1000}, {128, 4.0f, 700}};
    for (const Case &c : cases) {
        RawTable a, b;
        a.Build({}, {}, c.buckets);
        b.Build({}, {}, c.buckets);
        a.T().maxLoad = b.T().maxLoad = c.load;
        std::vector<std::unique_ptr<std::string>> keys;
        for (size_t i = 0; i < c.inserts; ++i) {
            keys.push_back(std::make_unique<std::string>("k" + std::to_string(gen() % 100000) + RandomKey(gen, 2)));
            KeyView key{keys.back()->data(), static_cast<uint32_t>(keys.back()->size())};
            InsertResult ra{}, rb{};
            CommandTable *current = nullptr;
            Differential(
                ctx, "CommandTable::Insert (growth)", {},
                [&] {
                    current = &a.T();
                    return RunGuarded([&] { orig(&a.T(), &ra, &key); });
                },
                [&] {
                    current = &b.T();
                    return RunGuarded([&] { b.T().Insert(&rb, &key); });
                },
                [&] {
                    std::vector<uint8_t> d = DumpTable(*current);
                    gExtra.insert(gExtra.end(), d.begin(), d.end());
                });
            if (ra.inserted != rb.inserted) ctx.Fail("InsertGrowth: inserted flag differs");
        }
        a.Free();
        b.Free();
    }
}

DIFF_TEST(CommandTable_Rehash, 0xaf7da0)
{
    auto orig = ctx.Original<void(__thiscall *)(CommandTable *, uint32_t)>(0xaf7da0);
    std::mt19937 gen(0xaf7da0);
    for (int i = 0; i < 400; ++i) {
        // A few distinct keys, repeated, so that nodes with equal keys sit apart in the list.
        std::vector<std::unique_ptr<std::string>> pool;
        size_t distinct = 1 + gen() % 6;
        for (size_t k = 0; k < distinct; ++k) pool.push_back(std::make_unique<std::string>(RandomKey(gen, 3)));
        std::vector<std::string *> keys;
        std::vector<void *> values;
        size_t n = gen() % 40;
        for (size_t k = 0; k < n; ++k) {
            keys.push_back(pool[gen() % distinct].get());
            values.push_back(reinterpret_cast<void *>(0x100 + k));
        }
        static const uint32_t requests[] = {1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 100, 300, 5000};
        uint32_t request = requests[gen() % (sizeof(requests) / sizeof(requests[0]))];
        uint32_t initial = 1u << (1 + gen() % 4);
        RawTable a, b;
        a.Build(keys, values, initial);
        b.Build(keys, values, initial);
        orig(&a.T(), request);
        b.T().Rehash(request);
        ++ctx.cases;
        if (DumpTable(a.T()) != DumpTable(b.T())) ctx.Fail("Rehash(%u) of %zu nodes (%zu distinct keys, %u buckets): tables differ", request, n, distinct, initial);
        a.Free();
        b.Free();
    }
}

// The destructor: every command is released (slot 2), the nodes, the sentinel and the bucket
// storage are freed. ClearNodes (0xaf74e0) is the part that releases and frees the nodes.
DIFF_TEST(CommandTable_Destruct, 0xaf7870)
{
    auto orig = ctx.Original<void(__thiscall *)(CommandTable *)>(0xaf7870);
    std::mt19937 gen(0xaf7870);
    Fakes fakes;
    for (int i = 0; i < 100; ++i) {
        std::vector<std::unique_ptr<std::string>> pool;
        std::vector<std::string *> keys;
        std::vector<void *> values;
        size_t n = gen() % 12;
        for (size_t k = 0; k < n; ++k) {
            pool.push_back(std::make_unique<std::string>(RandomKey(gen, 4)));
            keys.push_back(pool.back().get());
            values.push_back(gen() % 4 == 0 ? nullptr : &fakes.cmds[gen() % 6]);
        }
        RawTable a, b;
        a.Build(keys, values, 8);
        b.Build(keys, values, 8);
        a.Allocations();
        b.Allocations();
        size_t keptA = 0, keptB = 0;
        Differential(
            ctx, "CommandTable::Destruct", {}, [&] { return RunKept(a.allocations, &keptA, [&] { orig(&a.T()); }); },
            [&] { return RunKept(b.allocations, &keptB, [&] { b.T().Destruct(); }); });
        if (keptA) ctx.Fail("Destruct: original left %zu blocks allocated", keptA);
        if (keptB) ctx.Fail("Destruct: replacement left %zu blocks allocated", keptB);
        if (a.T().buckets.data() != nullptr || b.T().buckets.data() != nullptr) ctx.Fail("Destruct: bucket pointers not cleared");
    }
}

DIFF_TEST(CommandTable_ClearNodes, 0xaf74e0)
{
    auto orig = ctx.Original<void(__cdecl *)(void *, CommandNode *)>(0xaf74e0);
    std::mt19937 gen(0xaf74e0);
    Fakes fakes;
    for (int i = 0; i < 100; ++i) {
        std::vector<std::unique_ptr<std::string>> pool;
        std::vector<std::string *> keys;
        std::vector<void *> values;
        size_t n = gen() % 12;
        for (size_t k = 0; k < n; ++k) {
            pool.push_back(std::make_unique<std::string>(RandomKey(gen, 4)));
            keys.push_back(pool.back().get());
            values.push_back(gen() % 4 == 0 ? nullptr : &fakes.cmds[gen() % 6]);
        }
        RawTable a, b;
        a.Build(keys, values, 8);
        b.Build(keys, values, 8);
        std::vector<const void *> nodesA, nodesB;
        for (CommandNode *p = a.T().head->next; p != a.T().head; p = p->next) nodesA.push_back(p);
        for (CommandNode *p = b.T().head->next; p != b.T().head; p = p->next) nodesB.push_back(p);
        size_t keptA = 0, keptB = 0;
        Differential(
            ctx, "CommandTable::ClearNodes", {}, [&] { return RunKept(nodesA, &keptA, [&] { orig(nullptr, a.T().head); }); },
            [&] { return RunKept(nodesB, &keptB, [&] { CommandTable::ClearNodes(nullptr, b.T().head); }); });
        if (keptA) ctx.Fail("ClearNodes: original left %zu nodes allocated", keptA);
        if (keptB) ctx.Fail("ClearNodes: replacement left %zu nodes allocated", keptB);
        // The sentinel stays (its next pointer is cleared); the buckets are the test's.
        ::operator delete(a.T().head);
        ::operator delete(b.T().head);
        a.T().buckets.~vector();
        b.T().buckets.~vector();
    }
}

// ---- the block's slots -------------------------------------------------------------------------------

DIFF_TEST(cBlockCommandBase_AddCommand, 0xaf7cd0)
{
    auto orig = ctx.Original<void(__thiscall *)(cp::cBlockCommandBase *, void *)>(0xaf7cd0);
    std::mt19937 gen(0xaf7cd0);
    Fakes fakes;
    for (int round = 0; round < 40; ++round) {
        BlockPair pair(ctx, gen, round % 3 == 0); // a third of the blocks are case sensitive
        for (int step = 0; step < 25; ++step) {
            void *command = &fakes.cmds[gen() % 7];
            Step(
                ctx, "AddCommand", pair, [&] { orig(pair.Block(0), command); }, [&] { pair.Block(1)->AddCommand(command); });
        }
        pair.Destroy(ctx);
    }
}

DIFF_TEST(cBlockCommandBase_FindCommand, 0xaf7a90)
{
    auto orig = ctx.Original<void *(__thiscall *)(cp::cBlockCommandBase *, const char *, uint32_t)>(0xaf7a90);
    auto add = ctx.Original<void(__thiscall *)(cp::cBlockCommandBase *, void *)>(0xaf7cd0);
    std::mt19937 gen(0xaf7a90);
    Fakes fakes;
    for (int round = 0; round < 30; ++round) {
        BlockPair pair(ctx, gen, round % 3 == 0);
        for (int k = 0; k < 4; ++k) {
            void *command = &fakes.cmds[gen() % 7];
            gLog.clear();
            add(pair.Block(0), command);
            add(pair.Block(1), command);
        }
        static const char *const probes[] = {"alpha", "ALPHA", "Alpha", "beta", "gamma", "Gamma", "GAMMA", "delta-delta-delta-delta", "x", "X", "nothing", "", "al"};
        for (int q = 0; q < 25; ++q) {
            static const size_t longLengths[] = {0x7e, 0x7f, 0x80, 0x81, 60, 150};
            std::string name = gen() % 5 == 0 ? std::string(longLengths[gen() % 6], 'q') : std::string(probes[gen() % 13]);
            if (gen() % 8 == 0) name = std::string(0x80, gen() % 2 ? 'w' : 'W');
            Step(
                ctx, "FindCommand", pair,
                [&]() -> uint32_t { return reinterpret_cast<uint32_t>(orig(pair.Block(0), name.c_str(), static_cast<uint32_t>(name.size()))); },
                [&]() -> uint32_t { return reinterpret_cast<uint32_t>(pair.Block(1)->FindCommand(name.c_str(), static_cast<uint32_t>(name.size()))); });
        }
        pair.Destroy(ctx);
    }
}

namespace {

// Every heap block a block command owns: the nodes, the sentinel, the bucket vector, the name pool.
std::vector<const void *> BlockAllocations(cp::cBlockCommandBase *b)
{
    std::vector<const void *> v;
    v.push_back(b->mCommands.head);
    for (CommandNode *n = b->mCommands.head->next; n != b->mCommands.head; n = n->next) v.push_back(n);
    if (b->mCommands.buckets.data()) v.push_back(b->mCommands.buckets.data());
    v.push_back(b->mNames.head);
    for (auto *c = b->mNames.head->next; c != b->mNames.head; c = c->next) {
        v.push_back(c);
        v.push_back(c->block);
    }
    return v;
}

// The deleting destructors on heap objects built by the original constructor with a few commands in
// them (the commands are the fakes above): the vtable word, the Release calls on the commands and
// every heap block freed (or kept, for the object itself when flags & 1 is clear).
template <class Obj> void DestructorTest(difftest::Context &ctx, const char *name, uint32_t rva, size_t size)
{
    std::mt19937 gen(rva);
    auto ctor = ctx.Original<void *(__thiscall *)(void *)>(0xaf7750);
    auto add = ctx.Original<void(__thiscall *)(cp::cBlockCommandBase *, void *)>(0xaf7cd0);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(rva);
    Fakes fakes;
    for (int round = 0; round < 40; ++round) {
        uint32_t flags = (round % 4) | (round % 7 == 0 ? 0xf0u : 0u);
        int commands = round % 6;
        void *objects[2];
        std::vector<const void *> blocks[2];
        for (int s = 0; s < 2; ++s) {
            objects[s] = ::operator new(size);
            std::memset(objects[s], 0xCD, size);
            ctor(objects[s]);
            auto *b = static_cast<cp::cBlockCommandBase *>(objects[s]);
            b->mFlag = 0;
            b->mParser = reinterpret_cast<void *>(0x1000);
            b->mValue = 0x2000;
            for (int k = 0; k < commands; ++k) add(b, &fakes.cmds[k % 6]);
            blocks[s] = BlockAllocations(b);
            b->mVTable = reinterpret_cast<const void *>(0xdead0000); // the destructor sets it again
        }
        gLog.clear();
        void *ra = nullptr, *rb = nullptr;
        size_t kept[2] = {};
        bool freedA = false, freedB = false;
        Outcome oa = RunKept(blocks[0], &kept[0], [&] { ra = orig(objects[0], flags); }, objects[0], &freedA);
        std::vector<Event> logA = gLog;
        gLog.clear();
        Outcome ob = RunKept(blocks[1], &kept[1], [&] { rb = reinterpret_cast<Obj *>(objects[1])->DeletingDestructor(flags); }, objects[1], &freedB);
        std::vector<Event> logB = gLog;
        ++ctx.cases;
        if (!(oa == ob)) ctx.Fail("%s: outcome differs", name);
        if (ra != objects[0] || rb != objects[1]) ctx.Fail("%s: wrong return value", name);
        if (logA.size() != logB.size()) ctx.Fail("%s: %zu calls vs %zu", name, logA.size(), logB.size());
        for (size_t k = 0; k < std::min(logA.size(), logB.size()); ++k)
            if (!(logA[k] == logB[k])) ctx.Fail("%s: call %zu differs", name, k);
        for (int s = 0; s < 2; ++s)
            if (kept[s]) ctx.Fail("%s: side %d left %zu blocks allocated", name, s, kept[s]);
        if (freedA != freedB || freedA != ((flags & 1) != 0)) ctx.Fail("%s: object freed %d vs %d (flags %x)", name, freedA, freedB, flags);
        if (!(flags & 1)) {
            if (std::memcmp(objects[0], objects[1], 4) != 0) ctx.Fail("%s: vtable word differs", name);
            ::operator delete(objects[0]);
            ::operator delete(objects[1]);
        }
    }
}

} // namespace

DIFF_TEST(cBlockCommandBase_Destruct, 0xaf7970)
{
    // The non-deleting body: vtable, pool, table (the object stays).
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xaf7970);
    std::mt19937 gen(0xaf7970);
    Fakes fakes;
    auto add = ctx.Original<void(__thiscall *)(cp::cBlockCommandBase *, void *)>(0xaf7cd0);
    for (int round = 0; round < 40; ++round) {
        BlockPair pair(ctx, gen, false);
        for (int k = 0, n = round % 7; k < n; ++k) {
            add(pair.Block(0), &fakes.cmds[k % 6]);
            add(pair.Block(1), &fakes.cmds[k % 6]);
        }
        std::vector<const void *> blocks[2] = {BlockAllocations(pair.Block(0)), BlockAllocations(pair.Block(1))};
        pair.Block(0)->mVTable = pair.Block(1)->mVTable = reinterpret_cast<const void *>(0xdead0000); // the body sets it again
        gLog.clear();
        size_t kept[2] = {};
        Outcome oa = RunKept(blocks[0], &kept[0], [&] { orig(pair.raw[0]); });
        std::vector<Event> logA = gLog;
        gLog.clear();
        Outcome ob = RunKept(blocks[1], &kept[1], [&] { pair.Block(1)->Destruct(); });
        ++ctx.cases;
        bool same = logA.size() == gLog.size();
        for (size_t k = 0; same && k < logA.size(); ++k) same = logA[k] == gLog[k];
        if (!(oa == ob) || !same) ctx.Fail("Block Destruct: outcome or calls differ (%zu vs %zu)", logA.size(), gLog.size());
        if (pair.Block(0)->mVTable != pair.Block(1)->mVTable) ctx.Fail("Block Destruct: vtable differs");
        for (int s = 0; s < 2; ++s)
            if (kept[s]) ctx.Fail("Block Destruct: side %d left %zu blocks allocated", s, kept[s]);
    }
}

DIFF_TEST(cBlockCommandBase_DeletingDestructor, 0xaf79c0)
{
    DestructorTest<cp::cBlockCommandBase>(ctx, "cBlockCommand deleting destructor", 0xaf79c0, 0x40);
}

DIFF_TEST(cBlockCommandT_DeletingDestructor, 0x2c2160)
{
    DestructorTest<cp::cBlockCommandT>(ctx, "cBlockCommandT deleting destructor", 0x2c2160, 0x44);
}
