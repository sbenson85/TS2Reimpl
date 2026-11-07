// Differential tests for cTSNeighborGeneticDataTable (the map members, QueryInterface/Release, construction and the
// resource manager members). The fake world is in ts_neighbor_genetic_data_table_world.cpp; the stream, persist-system and
// container members are tested in ts_neighbor_genetic_data_table_io.cpp.
#include "ts_neighbor_genetic_data_table.h"
#include <cstdio>

using namespace ngdt;

namespace {
struct RawNode {
    RawNode *left, *parent, *right;
    uint8_t color, isNil;
};

// Runs `run(original, blob)` on two identically seeded worlds and compares everything observable.
Seeded Plain(unsigned seed, unsigned entries) { return {seed, entries, (seed & 1) != 0, seed % 5 + 1}; }

std::vector<uint32_t> Ids()
{
    std::vector<uint32_t> ids;
    for (const auto &entry : T()->mData) ids.push_back(entry.first);
    return ids;
}

// Used by tests that need to name an id before the world exists: the ids a seed generates.
std::vector<uint32_t> IdsOf(const Seeded &s)
{
    Reset(s, Config{});
    auto ids = Ids();
    Cleanup();
    return ids;
}
} // namespace

DIFF_TEST(NeighborGeneticDataTable_Construct, 0x4f0ac0)
{
    unsigned heads = 0;
    for (unsigned seed = 0; seed < 64; ++seed) {
        Guarded a, b;
        std::mt19937 rng(seed);
        for (auto &byte : reinterpret_cast<std::array<uint8_t, sizeof(a)> &>(a)) byte = uint8_t(rng());
        b = a;
        auto *left = reinterpret_cast<Table *>(a.bytes);
        auto *right = reinterpret_cast<Table *>(b.bytes);
        using Fn = Table *(__thiscall *)(Table *);
        auto *ra = ctx.Original<Fn>(0x4f0ac0)(left);
        auto *rb = right->Construct();
        ++ctx.cases;
        if (ra != left || rb != right) ctx.Fail("construct seed %u return", seed);
        // The head nodes are separate heap blocks: compare their links (all self-referencing) and flags, not their payload.
        auto check = [&](Guarded &g, Table *t) {
            auto *head = *reinterpret_cast<RawNode **>(&t->mData);
            const bool ok = head && head->left == head && head->parent == head && head->right == head && head->color == 1 && head->isNil == 1;
            *reinterpret_cast<RawNode **>(&t->mData) = nullptr;
            return ok;
        };
        const uint32_t sizeA = reinterpret_cast<uint32_t *>(a.bytes)[0x28 / 4], sizeB = reinterpret_cast<uint32_t *>(b.bytes)[0x28 / 4];
        auto *headA = *reinterpret_cast<RawNode **>(&left->mData);
        auto *headB = *reinterpret_cast<RawNode **>(&right->mData);
        const bool okA = check(a, left), okB = check(b, right);
        if (!okA || !okB) ctx.Fail("construct seed %u head node", seed); else ++heads;
        if (sizeA != 0 || sizeB != 0) ctx.Fail("construct seed %u size", seed);
        if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("construct seed %u bytes differ", seed);
        // Vtables are the game's, installed in all three places.
        if (left->mVTable != ctx.image.At(0xdcdf6c) || left->mContainer.mVTable != ctx.image.At(0xdcdfc8) ||
            left->mPersistVTable != ctx.image.At(0xdcdfe4))
            ctx.Fail("construct seed %u vtables", seed);
        ::operator delete(headA, 0x18);
        ::operator delete(headB, 0x18);
    }
    std::printf("  Construct: 64 randomized allocations, %u head nodes self-linked\n", heads);
}

DIFF_TEST(NeighborGeneticDataTable_Create, 0x4f0f30)
{
    unsigned made = 0;
    for (unsigned i = 0; i < 32; ++i) {
        using Fn = Table *(__cdecl *)();
        Table *a = ctx.Original<Fn>(0x4f0f30)();
        Table *b = Table::Create();
        ++ctx.cases;
        if (!a || !b) { ctx.Fail("create returned null"); continue; }
        ++made;
        if (!HeapValidate(GetProcessHeap(), 0, a) || !HeapValidate(GetProcessHeap(), 0, b)) ctx.Fail("create blocks not live");
        if (HeapSize(GetProcessHeap(), 0, a) < 0x2c || HeapSize(GetProcessHeap(), 0, b) < 0x2c) ctx.Fail("create block too small");
        for (Table *t : {a, b}) {
            auto *head = *reinterpret_cast<RawNode **>(&t->mData);
            if (!head || head->left != head || head->parent != head || head->right != head || head->color != 1 || head->isNil != 1)
                ctx.Fail("create head");
            if (t->mRefCount != 0 || t->mModified != 0 || t->mKey[0] | t->mKey[1] | t->mKey[2] | t->mKey[3]) ctx.Fail("create fields");
            if (t->mVTable != ctx.image.At(0xdcdf6c) || t->mContainer.mVTable != ctx.image.At(0xdcdfc8) ||
                t->mPersistVTable != ctx.image.At(0xdcdfe4))
                ctx.Fail("create vtables");
            if (reinterpret_cast<uint32_t *>(t)[0x28 / 4] != 0) ctx.Fail("create size");
        }
        // Destroyed through Release with the game's own code on both sides: one replacement, one original.
        a->Release();
        b->Release();
    }
    std::printf("  Create: %u tables, fields, vtables, empty map\n", made);
}

DIFF_TEST(NeighborGeneticDataTable_QueryInterface, 0x4f10c0)
{
    Globals globals(ctx);
    SweepReport report("QueryInterface");
    constexpr uint32_t ids[] = {1, 0xe4fda3d4, 0xec2391bf, 0x456b8f1d, 0x8db5bf48, 0, 2, 0x456b8f1c, 0x456b8f1e, 0xffffffff, 0x8db5bf47, 0xec2391c0, 0xe4fda3d3};
    unsigned perId[13]{};
    for (unsigned i = 0; i < 195; ++i) {
        const uint32_t id = ids[i % 13];
        ++perId[i % 13];
        Pair(ctx, "QueryInterface", i, Plain(i, i % 4), Config{}, [&](bool original, Blob &b) {
            struct Output { uint32_t before; void *value; uint32_t after; } out{0x98ab76cd, reinterpret_cast<void *>(0x1234abcd), 0xcdef4321};
            // The original writes AL only; the upper bits of EAX are the id arithmetic or the AddRef result.
            using Fn = uint8_t(__thiscall *)(Table *, uint32_t, void **);
            const uint8_t r = original ? ctx.Original<Fn>(0x4f10c0)(T(), id, &out.value) : T()->QueryInterface(id, &out.value);
            b.Mark("result");
            b.Add(r); b.Add(out.before); b.Add(out.after);
            const auto offset = static_cast<uint8_t *>(out.value) - W().work.bytes;
            b.Add(out.value == reinterpret_cast<void *>(0x1234abcd) ? 0xffffffff : uint32_t(offset));
        });
    }
    std::printf("  QueryInterface: 15 cases for each of 13 ids (primary x3, persist, container, 8 unsupported), out guards, AddRef\n");
}

DIFF_TEST(NeighborGeneticDataTable_Release, 0x4f1660)
{
    Globals globals(ctx);
    SweepReport report("Release");
    constexpr uint32_t counts[] = {2, 3, 0x80000000, 0xffffffff, 7, 0x10000};
    for (unsigned i = 0; i < 48; ++i) {
        Seeded s = Plain(i, i % 6);
        s.refCount = counts[i % 6];
        Pair(ctx, "Release", i, s, Config{}, [&](bool original, Blob &b) {
            using Fn = uint32_t(__thiscall *)(Table *);
            b.Mark("result");
            b.Add(original ? ctx.Original<Fn>(0x4f1660)(T()) : T()->Release());
        });
    }
    // The last reference: the table frees itself and releases each data object, highest id first.
    unsigned freed = 0;
    for (unsigned i = 0; i < 80; ++i) {
        Seeded s = Plain(i, i % 11);
        s.refCount = i % 2;
        Blob left, right;
        for (int side = 0; side < 2; ++side) {
            Reset(s, Config{});
            Blob &b = side == 0 ? left : right;
            auto *heap = static_cast<Table *>(::operator new(sizeof(Table)));
            std::memcpy(heap, T(), sizeof(Table));
            MarkTableFreed(); // the map now belongs to the heap copy
            void *blocks[26];
            unsigned count = 0;
            blocks[count++] = heap;
            auto *head = *reinterpret_cast<RawNode **>(&heap->mData);
            blocks[count++] = head;
            struct Walk { static void Run(RawNode *n, void **blocks, unsigned &count) {
                if (n->isNil) return;
                Run(n->left, blocks, count); blocks[count++] = n; Run(n->right, blocks, count); } };
            Walk::Run(head->parent, blocks, count);
            using Fn = uint32_t(__thiscall *)(Table *);
            difftest::FreeRecorder rec; // exact: records the frees, whatever the heap does with the blocks afterwards
            const uint32_t r = side == 0 ? ctx.Original<Fn>(0x4f1660)(heap) : heap->Release();
            rec.Stop();
            bool live[26];
            for (unsigned k = 0; k < count; ++k) live[k] = !rec.Freed(blocks[k]);
            // Every listed block is freed. The replacement frees exactly those; the original also allocates and
            // frees one temporary block (an internal id list), not observable otherwise.
            if (rec.Overflowed() || (side == 1 ? rec.Count() != count : rec.Count() < count))
                ctx.Fail("Release seed %u side %d freed %zu blocks, %u listed", i, side, rec.Count(), count);
            b.Mark("result");
            b.Add(r);
            b.Mark("freed");
            for (unsigned k = 0; k < count; ++k) { b.Add(live[k]); if (live[k]) ctx.Fail("Release seed %u block %u not freed", i, k); }
            Capture(b);
            Cleanup();
        }
        if (Same(ctx, left, right, "Release(last)", i)) ++freed;
    }
    std::printf("  Release: 48 decrement cases (counts 2..0xffffffff), %u self-destruction cases (counts 0/1, 0..10 entries): all blocks freed, release order\n", freed);
}

DIFF_TEST(NeighborGeneticDataTable_GetGZCLSID, 0x4f0fc0)
{
    Globals globals(ctx);
    SweepReport report("GetGZCLSID");
    for (unsigned i = 0; i < 16; ++i)
        Pair(ctx, "GetGZCLSID", i, Plain(i, i % 4), Config{}, [&](bool original, Blob &b) {
            using Fn = uint32_t(__thiscall *)(Table *);
            b.Mark("result");
            b.Add(original ? ctx.Original<Fn>(0x4f0fc0)(T()) : T()->GetGZCLSID());
        });
}

DIFF_TEST(NeighborGeneticDataTable_Clear, 0x4f1920)
{
    Globals globals(ctx);
    SweepReport report("Clear");
    for (unsigned i = 0; i < 84; ++i) {
        Seeded s = Plain(i, i % 21);
        Pair(ctx, "Clear", i, s, Config{}, [&](bool original, Blob &b) {
            using Fn = uint8_t(__thiscall *)(Table *);
            b.Mark("result");
            b.Add(original ? ctx.Original<Fn>(0x4f1920)(T()) : T()->Clear());
        });
    }
    std::printf("  Clear: 84 cases (0..20 entries, modified flag both ways), release order, empty map links\n");
}

DIFF_TEST(NeighborGeneticDataTable_Set, 0x4f0b80)
{
    Globals globals(ctx);
    SweepReport report("Set");
    unsigned replaced = 0, same = 0, added = 0, nulls = 0;
    for (unsigned i = 0; i < 384; ++i) {
        const Seeded s = Plain(i, i % 12);
        const auto ids = IdsOf(s);
        const unsigned mode = i % 6;
        // modes 0-2 replace an existing id, 3-5 add a new one; data: another fake, the same fake, null
        uint32_t id = 40 + i % 7;
        if (mode < 3 && !ids.empty()) id = ids[i / 6 % ids.size()];
        else if (ids.empty() || mode >= 3) id = (i & 8) ? 0xfffffffd - i % 3 : 100 + i % 50;
        Pair(ctx, "Set", i, s, Config{}, [&](bool original, Blob &b) {
            void *data;
            if (mode % 3 == 2) data = nullptr;
            else if (mode % 3 == 1) { auto *existing = T()->Get(id); data = existing ? existing : &W().data[i % 24]; }
            else data = &W().data[(i / 3) % 24];
            if (original && W().cfg.throwAt < 0) { if (mode >= 3) ++added; else if (mode % 3 == 1) ++same; else ++replaced; if (!data) ++nulls; }
            using Fn = uint8_t(__thiscall *)(Table *, uint32_t, void *);
            b.Mark("result");
            b.Add(original ? ctx.Original<Fn>(0x4f0b80)(T(), id, data) : T()->Set(id, data));
        });
    }
    std::printf("  Set: 384 cases: %u replace, %u same pointer, %u add, %u null data; counts and order of AddRef/Release\n", replaced, same, added, nulls);
}

DIFF_TEST(NeighborGeneticDataTable_Remove, 0x4f16c0)
{
    Globals globals(ctx);
    SweepReport report("Remove");
    unsigned removed = 0, missing = 0;
    for (unsigned i = 0; i < 400; ++i) {
        Seeded s = Plain(i, i % 20 + (i % 20 == 0 ? 1 : 0));
        s.modified = (i / 4) % 2 == 1; // independent of the outcome, so a missing id is seen with both flag values
        const auto ids = IdsOf(s);
        const bool present = i % 4 != 3 && !ids.empty();
        const uint32_t id = present ? ids[i / 4 % ids.size()] : 500 + i;
        Blob left, right;
        for (int side = 0; side < 2; ++side) {
            Reset(s, Config{});
            Blob &b = side == 0 ? left : right;
            // The removed node, to see it freed.
            RawNode *node = nullptr;
            if (present) for (auto it = T()->mData.begin(); it != T()->mData.end(); ++it) if (it->first == id)
                node = reinterpret_cast<RawNode *>(reinterpret_cast<char *>(&*it) - 0x10);
            using Fn = uint8_t(__thiscall *)(Table *, uint32_t);
            difftest::FreeRecorder rec;
            const uint8_t r = side == 0 ? ctx.Original<Fn>(0x4f16c0)(T(), id) : T()->Remove(id);
            rec.Stop();
            const bool live = node && !rec.Freed(node);
            b.Mark("result");
            b.Add(r); b.Add(live);
            if (present && live) ctx.Fail("Remove seed %u node not freed", i);
            Capture(b);
            Cleanup();
        }
        Same(ctx, left, right, "Remove", i);
        present ? ++removed : ++missing;
    }
    std::printf("  Remove: 400 cases: %u removed (every tree position in 1..20 entries), %u missing; node freed, release traced\n", removed, missing);
}

DIFF_TEST(NeighborGeneticDataTable_SetModified, 0x4f10b0)
{
    Globals globals(ctx);
    SweepReport report("SetModified");
    for (unsigned i = 0; i < 16; ++i)
        Pair(ctx, "SetModified", i, Plain(i, i % 3), Config{}, [&](bool original, Blob &b) {
            using Fn = void(__thiscall *)(Table *);
            if (original) ctx.Original<Fn>(0x4f10b0)(T()); else T()->SetModified();
            b.Mark("result");
        });
}

DIFF_TEST(NeighborGeneticDataTable_Get, 0x4f1000)
{
    Globals globals(ctx);
    SweepReport report("Get");
    unsigned found = 0, absent = 0;
    for (unsigned i = 0; i < 300; ++i) {
        const Seeded s = Plain(i, i % 20);
        const auto ids = IdsOf(s);
        const bool present = i % 3 != 2 && !ids.empty();
        // Absent ids lie below, between and above the present ones (and at both unsigned extremes).
        const uint32_t probes[] = {0, 5, 16, 0x7ffffffe, 0x80000001, 0xfffffffd, 0xffffffff, 17 + i};
        const uint32_t id = present ? ids[i / 3 % ids.size()] : probes[i % 8];
        Pair(ctx, "Get", i, s, Config{}, [&](bool original, Blob &b) {
            using Fn = void *(__thiscall *)(Table *, uint32_t);
            void *r = original ? ctx.Original<Fn>(0x4f1000)(T(), id) : T()->Get(id);
            b.Mark("result");
            b.Add(IdOf(r));
            if (original && W().cfg.throwAt < 0) (r ? ++found : ++absent);
        });
    }
    std::printf("  Get: 300 cases, %u hits, %u misses (the miss probes may also hit)\n", found, absent);
}

DIFF_TEST(NeighborGeneticDataTable_FirstId, 0x4f0fe0)
{
    Globals globals(ctx);
    SweepReport report("FirstId");
    unsigned empty = 0;
    for (unsigned i = 0; i < 100; ++i)
        Pair(ctx, "FirstId", i, Plain(i, i % 10), Config{}, [&](bool original, Blob &b) {
            using Fn = uint32_t(__thiscall *)(Table *);
            const uint32_t r = original ? ctx.Original<Fn>(0x4f0fe0)(T()) : T()->FirstId();
            b.Mark("result");
            b.Add(r);
            if (original && W().cfg.throwAt < 0 && r == 0xffffffff) ++empty;
        });
    std::printf("  FirstId: 100 cases, %u empty tables\n", empty);
}

DIFF_TEST(NeighborGeneticDataTable_NextId, 0x4f1050)
{
    Globals globals(ctx);
    SweepReport report("NextId");
    unsigned last = 0, missing = 0, middle = 0;
    for (unsigned i = 0; i < 400; ++i) {
        const Seeded s = Plain(i, i % 21);
        const auto ids = IdsOf(s);
        // Walk positions: every present id (so first, middle and last), plus ids that are not in the table.
        const bool present = i % 4 != 3 && !ids.empty();
        const uint32_t id = present ? ids[i / 4 % ids.size()] : (i % 8 == 3 ? 0xffffffffu : 700 + i);
        Pair(ctx, "NextId", i, s, Config{}, [&](bool original, Blob &b) {
            using Fn = uint32_t(__thiscall *)(Table *, uint32_t);
            const uint32_t r = original ? ctx.Original<Fn>(0x4f1050)(T(), id) : T()->NextId(id);
            b.Mark("result");
            b.Add(r);
            if (original && W().cfg.throwAt < 0) { if (!present) ++missing; else if (r == 0xffffffff) ++last; else ++middle; }
        });
    }
    std::printf("  NextId: 400 cases, %u with a successor, %u last entries, %u absent ids\n", middle, last, missing);
}

DIFF_TEST(NeighborGeneticDataTable_CreateData, 0x4f0ea0)
{
    Globals globals(ctx);
    SweepReport report("CreateData");
    unsigned nulls = 0;
    for (unsigned i = 0; i < 48; ++i) {
        Config cfg;
        cfg.managerCreateNull = i % 4 == 1;
        const bool noManager = i % 6 == 5;
        globals.SetManager(noManager);
        Pair(ctx, "CreateData", i, Plain(i, i % 3), cfg, [&](bool original, Blob &b) {
            using Fn = void *(__thiscall *)(Table *);
            void *r = original ? ctx.Original<Fn>(0x4f0ea0)(T()) : T()->CreateData();
            b.Mark("result");
            b.Add(IdOf(r));
            if (original && W().cfg.throwAt < 0 && !r) ++nulls;
        });
    }
    globals.SetManager(false);
    std::printf("  CreateData: 48 cases (no manager, null creation, normal), %u null results\n", nulls);
}

DIFF_TEST(NeighborGeneticDataTable_LoadOrCreate, 0x4f0d00)
{
    Globals globals(ctx);
    SweepReport report("LoadOrCreate");
    unsigned replaced = 0, inserted = 0, nulls = 0, same = 0;
    for (unsigned i = 0; i < 384; ++i) {
        const Seeded s = Plain(i, i % 12);
        const auto ids = IdsOf(s);
        Config cfg;
        cfg.managerGetFail = (i % 3 == 1) ? 1 : 0;      // fall back to creating
        cfg.managerCreateNull = i % 7 == 6;
        cfg.failWritesOut = i % 2 == 0; // a failing lookup may leave a counted object behind
        const bool present = i % 2 && !ids.empty();
        const uint32_t id = present ? ids[i / 2 % ids.size()] : 30 + i % 9;
        globals.SetManager(i % 16 == 15);
        Pair(ctx, "LoadOrCreate", i, s, cfg, [&](bool original, Blob &b) {
            using Fn = void *(__thiscall *)(Table *, uint32_t);
            void *r = original ? ctx.Original<Fn>(0x4f0d00)(T(), id) : T()->LoadOrCreate(id);
            b.Mark("result");
            b.Add(IdOf(r));
            if (original && W().cfg.throwAt < 0) { if (!r) ++nulls; else if (present) { ++replaced; if (r == &W().data[id % 24]) ++same; } else ++inserted; }
        });
    }
    globals.SetManager(false);
    std::printf("  LoadOrCreate: 384 cases: %u overwrite an id (%u already held the object), %u inserted, %u null results\n", replaced, same, inserted, nulls);
}
