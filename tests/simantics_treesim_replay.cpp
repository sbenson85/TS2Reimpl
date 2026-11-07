// Differential tests for the interpreter's record/replay table: cTSTreeSim::RecordResult (0x575ed0) and ReplayResult
// (0x576e70) on the fake world of simantics_treesim.h, the table's own functions (insert 0x575280, rehash 0x578680, clear
// 0x5787b0, destructor 0x5757a0, list destructor 0x575860) on stand-alone tables, and ClearReplayCache (slot 32).
//
// The world tests start from a table the ORIGINAL built (a script of Record calls on the original), so that our functions
// are also checked reading and extending a table made by the game's code; a third test runs whole scripts of records,
// replays and clears with each side's own functions. The captured state includes the table's list order, every key,
// row, argument and recorded word, and the buckets' shape.
#include "simantics_treesim_world.h"

#include <windows.h>

namespace {

using namespace tsw;

using RecordFn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void *);
using ReplayFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, const uint16_t *, uint32_t *, void *, void *);

// One call the setup recorded, so the checked call can ask for the same thing.
struct Call {
    uint32_t flags, cache, opcode, argCount;
    uint16_t args[8];
    int frame;
};
std::vector<Call> gCalls;

constexpr uint32_t kOpcodes[] = {0x100, 0x101, 0x102, 0x8123, 0xffff, 5, 0x7fff, 0x180};
constexpr uint32_t kFlagChoices[] = {0x08, 0x18, 0x10, 0x08, 0x00, 0x28};

// The number of arguments is the tree's too (a replayed call is compared with rows made by calls of the same tree).
uint32_t ArgCountFor(uint32_t opcode)
{
    static const uint32_t counts[] = {0, 1, 2, 3, 4, 5, 6, 8};
    for (size_t i = 0; i < 8; ++i)
        if (kOpcodes[i] == opcode) return counts[i];
    return 2;
}

// Which of the object's argument words a tree changes is a property of the tree, so the cache mask follows the opcode (a
// call replayed with words to restore that the recording never kept would read nothing: an original bug, not tested).
uint32_t CacheFor(uint32_t opcode)
{
    static const uint32_t masks[] = {0x00, 0x01, 0xff, 0x80, 0x00, 0x24, 0x10, 0x03};
    for (size_t i = 0; i < 8; ++i)
        if (kOpcodes[i] == opcode) return masks[i];
    return 0x41;
}

// The fakes the replay code calls need the world; the throw script of the world is off while a setup runs.
struct SetupScope {
    World &w;
    uint32_t savedThrow;
    explicit SetupScope(World &world) : w(world), savedThrow(gThrowAt)
    {
        gW = &w;
        gThrowAt = 0;
    }
    ~SetupScope()
    {
        gW = nullptr;
        gThrowAt = savedThrow;
        gTrace.clear();
        gHitCount = 0;
    }
};

// The record code dereferences the owner of the frame it is given (ORIGINAL-BUG), as the replay code does.
void GiveOwners(World &w)
{
    for (size_t i = 0; i < Frames(w).size(); ++i)
        if (!Fr(w, static_cast<int>(i))->mOwner) Fr(w, static_cast<int>(i))->mOwner = &w.owner[i % kOwners];
}

Call RandomCall(World &w, std::mt19937 &r, bool mostlyTop)
{
    const int depth = static_cast<int>(Frames(w).size());
    Call c{};
    c.frame = (mostlyTop && r() % 4 != 0) ? depth - 1 : static_cast<int>(r() % depth);
    c.opcode = kOpcodes[r() % 8];
    c.flags = kFlagChoices[r() % 6];
    c.cache = CacheFor(c.opcode);
    cTSTreeStackElem *frame = Fr(w, c.frame);
    for (int j = 0; j < 8; ++j) w.frame[c.frame].args[j] = static_cast<uint16_t>(r() % 3);
    frame->mArgCount = static_cast<uint8_t>(ArgCountFor(c.opcode));
    c.argCount = frame->mArgCount;
    for (int j = 0; j < 8; ++j) c.args[j] = w.frame[c.frame].args[j];
    return c;
}

// Records a script of calls into the world's table with `record`, and remembers them in gCalls.
void Populate(World &w, uint32_t seed, RecordFn record)
{
    SetupScope scope(w);
    std::mt19937 r(seed ^ 0x5eedc0deu);
    gCalls.clear();
    GiveOwners(w);
    const uint32_t n = r() % 4 == 0 ? 0 : 1 + r() % (r() % 3 == 0 ? 40 : 8);
    for (uint32_t k = 0; k < n; ++k) {
        Call c = RandomCall(w, r, true);
        Sim(w)->mLastBranch = static_cast<uint8_t>(r() % 2);
        for (uint16_t &t : w.temps.value) t = static_cast<uint16_t>(r());
        record(Sim(w), c.flags, c.cache, c.opcode, Fr(w, c.frame));
        gCalls.push_back(c);
    }
    // The checked call sees fresh randomness: argument words and temps change after the script.
    for (uint16_t &t : w.temps.value) t = static_cast<uint16_t>(r());
}

// The call to check: one of the recorded ones (sometimes changed), or a new one.
Call ChooseCall(World &w, uint32_t seed)
{
    std::mt19937 r(seed ^ 0xa11ca11au);
    Call c{};
    if (Frames(w).empty()) { // the calling frame is gone: any call finds nothing
        c.flags = 0x08;
        c.opcode = 0x100;
        return c;
    }
    if (!gCalls.empty() && r() % 4 != 0) {
        c = gCalls[r() % gCalls.size()];
        switch (r() % 6) {
        case 0: c.args[r() % 8] ^= 1; break;
        case 1:
            c.opcode = kOpcodes[r() % 8];
            c.cache = CacheFor(c.opcode);
            c.argCount = ArgCountFor(c.opcode);
            break;
        case 2: c.flags = kFlagChoices[r() % 6]; break;
        default: break;
        }
        // The frame the call runs on: the recording one for a record, the stack's top for a replay.
        c.frame = static_cast<int>(Frames(w).size()) - 1;
    } else {
        c = RandomCall(w, r, true);
    }
    return c;
}

uint32_t ReplayOutcome(uint32_t ret, uint32_t out)
{
    return (ret & 0xff) | (out == 0xdeadbeefu ? 0u : 0x100u) | (out & 0xffffu) << 12;
}

template <typename Replay> uint32_t DoReplay(World &w, const Call &c, uint32_t seed, Replay replay)
{
    uint32_t out = 0xdeadbeefu;
    uint32_t argCount = c.argCount;
    if (seed % 17 == 0) argCount |= 0xfff00000u;   // only the low 16 bits count
    if (seed % 19 == 0) argCount = 0xfffe;         // negative as a 16-bit value: every row matches
    const uint16_t *args = seed % 23 == 0 ? nullptr : c.args;
    const uint32_t ret = replay(Sim(w), c.flags, c.cache, c.opcode, argCount, args, &out, nullptr, nullptr);
    return ReplayOutcome(ret, out);
}

void NoSetup(World &, uint32_t) {}

} // namespace

DIFF_TEST(TreeSim_RecordResult, 0x575ed0)
{
    auto record = ctx.Original<RecordFn>(0x575ed0);
    auto setup = [&](World &w, uint32_t seed) { Populate(w, seed, record); };
    auto choose = [](World &w, Call &c, uint32_t seed) {
        c = ChooseCall(w, seed);
        if (seed % 3 == 0) c.frame = static_cast<int>(seed / 3 % Frames(w).size()); // any frame, not the top
        if (seed % 29 == 0) c.opcode = 0x12345678u;                                   // the half-word conversions
        GiveOwners(w);
        Fr(w, c.frame)->mStackObjectId = (seed % 7 == 0) ? static_cast<uint16_t>(0x8000 | seed) : Fr(w, c.frame)->mStackObjectId;
    };
    Stats st;
    Compare(ctx, "RecordResult", 60000, setup,
            [&](World &w) {
                Call c;
                choose(w, c, gSeed);
                record(Sim(w), c.flags, c.cache, c.opcode, Fr(w, c.frame));
                return 0u;
            },
            [&](World &w) {
                Call c;
                choose(w, c, gSeed);
                Sim(w)->RecordResult(c.flags, c.cache, c.opcode, Fr(w, c.frame));
                return 0u;
            },
            st);
    Report("RecordResult", st);
}

DIFF_TEST(TreeSim_ReplayResult, 0x576e70)
{
    auto record = ctx.Original<RecordFn>(0x575ed0);
    auto original = ctx.Original<ReplayFn>(0x576e70);
    auto setup = [&](World &w, uint32_t seed) {
        Populate(w, seed, record);
        if (seed % 41 == 0) {
            SetupScope scope(w);
            Frames(w).clear(); // no calling frame
        }
    };
    Stats st;
    Compare(ctx, "ReplayResult", 60000, setup,
            [&](World &w) {
                const Call c = ChooseCall(w, gSeed);
                return DoReplay(w, c, gSeed, [&](auto... a) { return original(a...); });
            },
            [&](World &w) {
                const Call c = ChooseCall(w, gSeed);
                return DoReplay(w, c, gSeed, [&](cTSTreeSim *sim, uint32_t f, uint32_t cache, uint32_t op, uint32_t n, const uint16_t *args,
                                                uint32_t *out, void *u1, void *u2) {
                    return static_cast<uint32_t>(sim->ReplayResult(f, cache, op, n, args, out, u1, u2));
                });
            },
            st);
    Report("ReplayResult", st);
}

// Whole scripts with each side's own functions: records and replays interleaved (the table grows through several
// rehashes), now and then emptied through the slot.
DIFF_TEST(TreeSim_ReplayScript, 0x575ed0)
{
    auto record = ctx.Original<RecordFn>(0x575ed0);
    auto replay = ctx.Original<ReplayFn>(0x576e70);
    auto clear = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x5761e0);
    auto script = [&](World &w, auto doRecord, auto doReplay, auto doClear) -> uint32_t {
        std::mt19937 r(gSeed ^ 0x5c71f7u);
        GiveOwners(w);
        std::vector<Call> made;
        uint32_t h = 1;
        const uint32_t ops = 10 + r() % 90;
        for (uint32_t k = 0; k < ops; ++k) {
            const uint32_t kind = r() % 10;
            if (kind < 5 || made.empty()) {
                Call c = RandomCall(w, r, true);
                Sim(w)->mLastBranch = static_cast<uint8_t>(r() % 2);
                for (uint16_t &t : w.temps.value) t = static_cast<uint16_t>(r());
                doRecord(w, c);
                made.push_back(c);
            } else if (kind < 9) {
                Call c = made[r() % made.size()];
                if (r() % 5 == 0) c.args[r() % 8] ^= 1;
                c.frame = static_cast<int>(Frames(w).size()) - 1;
                for (uint16_t &t : w.temps.value) t = static_cast<uint16_t>(r());
                h = h * 31 + DoReplay(w, c, 1, doReplay);
            } else {
                doClear(w);
                made.clear();
            }
        }
        return h;
    };
    Stats st;
    Compare(ctx, "ReplayScript", 3000, NoSetup,
            [&](World &w) {
                return script(
                    w, [&](World &x, const Call &c) { record(Sim(x), c.flags, c.cache, c.opcode, Fr(x, c.frame)); },
                    [&](auto... a) { return replay(a...); }, [&](World &x) { clear(Sim(x), 0); });
            },
            [&](World &w) {
                return script(
                    w, [&](World &x, const Call &c) { Sim(x)->RecordResult(c.flags, c.cache, c.opcode, Fr(x, c.frame)); },
                    [&](cTSTreeSim *sim, uint32_t f, uint32_t cache, uint32_t op, uint32_t n, const uint16_t *args, uint32_t *out, void *u1,
                        void *u2) { return static_cast<uint32_t>(sim->ReplayResult(f, cache, op, n, args, out, u1, u2)); },
                    [&](World &x) { Sim(x)->ClearReplayCache(0); });
            },
            st);
    Report("ReplayScript", st);
}

// ---- the table on its own ---------------------------------------------------------------------------------------

namespace {

using InsertFn = void *(__thiscall *)(void *, void *, const void *); // returns its result pointer (0x5760c8 reads eax)
using RehashFn = void(__thiscall *)(void *, uint32_t);
using VoidFn = void(__thiscall *)(void *);

// A table built by hand (the way the constructor builds it, so both sides start alike), with its allocations.
struct Table {
    TreeReplayMap map;
    Table()
    {
        std::memset(static_cast<void *>(&map), 0, sizeof(map));
        TreeReplayNode *head = static_cast<TreeReplayNode *>(::operator new(0x20));
        head->next = head;
        head->prev = head;
        map.mMaxLoadFactor = 1.0f;
        map.mList.mHead = head;
        new (&map.mBuckets) std::vector<TreeReplayNode *>(0x10, head);
        map.mMask = 7;
        map.mBucketCount = 8;
    }
    Table(const Table &) = delete;
    Table &operator=(const Table &) = delete;
};

// The table's shape and contents as numbers: size, buckets, then the nodes in list order with their keys and rows.
std::vector<uint32_t> Shape(const TreeReplayMap &m)
{
    std::vector<uint32_t> s;
    s.push_back(m.mList.mSize);
    s.push_back(*reinterpret_cast<const uint32_t *>(&m.mMaxLoadFactor));
    AppendReplayBuckets(m, s);
    for (const StdHashNode *n = m.mList.mHead->next; n != m.mList.mHead; n = n->next) {
        const TreeReplayNode *node = static_cast<const TreeReplayNode *>(n);
        s.push_back(node->mKey.mTreeIdentity);
        s.push_back(node->mKey.mCallSite);
        s.push_back(static_cast<uint32_t>(node->mRows.size()));
        for (const TreeReplayRow &row : node->mRows) {
            s.push_back(row.mResult);
            for (const std::vector<uint16_t> *v : {&row.mArgs, &row.mTemps}) {
                s.push_back(static_cast<uint32_t>(v->size()));
                for (uint16_t x : *v) s.push_back(x);
            }
        }
    }
    return s;
}

// Rows with arguments and temps of random lengths (now and then one long enough for the aligned big-block path).
void FillRows(TreeReplayNode *node, std::mt19937 &r)
{
    const uint32_t n = r() % 4;
    for (uint32_t i = 0; i < n; ++i) {
        TreeReplayRow row;
        row.mResult = r() % 2;
        row.mArgs.assign(r() % 9, static_cast<uint16_t>(r()));
        row.mTemps.assign(r() % 5 == 0 ? 8 : 0, static_cast<uint16_t>(r()));
        if (r() % 97 == 0) row.mArgs.assign(2500, static_cast<uint16_t>(r()));
        node->mRows.push_back(row);
    }
}

TreeReplayKey KeyFrom(std::mt19937 &r, uint32_t domain)
{
    // Few distinct words, so that keys collide in buckets and repeat; sums that collide too.
    return TreeReplayKey{(r() % domain) * 0x10001u, (r() % domain) << (r() % 3 == 0 ? 16 : 0)};
}

} // namespace

// One script of inserts, rehashes and clears applied to a table by each side; the tables must stay identical.
DIFF_TEST(TreeSim_ReplayMapInsert, 0x575280)
{
    auto insert = ctx.Original<InsertFn>(0x575280);
    auto rehash = ctx.Original<RehashFn>(0x578680);
    auto clear = ctx.Original<VoidFn>(0x5787b0);
    auto destroy = ctx.Original<VoidFn>(0x5757a0);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        Table a, b;
        std::mt19937 r(seed * 7919u + 1);
        const uint32_t domain = 2 + seed % 40;
        const uint32_t ops = 1 + seed % 150;
        bool ok = true;
        for (uint32_t k = 0; k < ops && ok; ++k) {
            const uint32_t kind = r() % 16;
            if (kind < 12) {
                const TreeReplayKey key = KeyFrom(r, domain);
                struct {
                    TreeReplayNode *node;
                    uint32_t flag;
                } fromOriginal{};
                const bool origReturnsOut = insert(&a.map, &fromOriginal, &key) == &fromOriginal;
                TreeReplayMap::InsertResult fromOurs;
                const bool oursReturnsOut = b.map.Insert(&fromOurs, &key) == &fromOurs;
                if (origReturnsOut != oursReturnsOut && ok) {
                    ok = false;
                    ctx.Fail("ReplayMap script seed %u op %u: returns its result pointer %d (original) / %d (ours)", seed, k, origReturnsOut, oursReturnsOut);
                }
                const bool fresh = (fromOriginal.flag & 0xff) != 0;
                if (fresh != (fromOurs.mInserted != 0) && ok) {
                    ok = false;
                    ctx.Fail("ReplayMap script seed %u op %u: insert reported new %d (original) / %d (ours)", seed, k, fresh, fromOurs.mInserted);
                }
                if (fresh) {
                    std::mt19937 rowsA(seed + k), rowsB(seed + k);
                    FillRows(fromOriginal.node, rowsA);
                    FillRows(fromOurs.mNode, rowsB);
                }
            } else if (kind < 15) {
                const uint32_t buckets = 1 + r() % (seed % 5 == 0 ? 5000 : 200);
                rehash(&a.map, buckets);
                b.map.Rehash(buckets);
            } else {
                clear(&a.map);
                b.map.Clear();
            }
            const std::vector<uint32_t> sa = Shape(a.map), sb = Shape(b.map);
            if (ok && sa != sb) {
                ok = false;
                size_t d = 0;
                while (d < sa.size() && d < sb.size() && sa[d] == sb[d]) ++d;
                ctx.Fail("ReplayMap script seed %u op %u (kind %u): tables differ, shape sizes %zu/%zu, first difference at %zu (0x%x/0x%x); size %u/%u mask 0x%x/0x%x count 0x%x/0x%x", seed, k,
                         kind, sa.size(), sb.size(), d, d < sa.size() ? sa[d] : 0xdeadu, d < sb.size() ? sb[d] : 0xdeadu, sa[0], sb[0], sa[2], sb[2], sa[3], sb[3]);
            }
        }
        ++ctx.cases;
        destroy(&a.map);
        destroy(&b.map);
    }
}

// Clear, the destructor and the list destructor free every block (nodes, rows, buckets, the head) like the original: the
// number of heap blocks that go away is the same, and the cleared table looks the same.
DIFF_TEST(TreeSim_ReplayMapFree, 0x5787b0)
{
    auto clear = ctx.Original<VoidFn>(0x5787b0);
    auto destroy = ctx.Original<VoidFn>(0x5757a0);
    auto freeNodes = ctx.Original<VoidFn>(0x575860);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        for (int what = 0; what < 3; ++what) { // 0 Clear, 1 destructor, 2 list destructor
            // Frees seen by difftest::FreeRecorder (exact, whatever the heap layout): how many blocks went away, and their sizes.
            constexpr size_t kMaxSizes = 4096;
            static size_t sizes[2][kMaxSizes];
            size_t freed[2] = {};
            std::vector<uint32_t> shape[2];
            for (int side = 0; side < 2; ++side) {
                Table t;
                std::mt19937 r(seed);
                const uint32_t n = seed % 60;
                for (uint32_t k = 0; k < n; ++k) {
                    const TreeReplayKey key = KeyFrom(r, 30);
                    TreeReplayMap::InsertResult res;
                    t.map.Insert(&res, &key);
                    if (res.mInserted) FillRows(res.mNode, r);
                }
                bool overflow = false;
                {
                    difftest::FreeRecorder rec; // records only this call: stopped before anything else allocates or frees
                    if (what == 0) {
                        if (side == 0) clear(&t.map);
                        else t.map.Clear();
                    } else if (what == 1) {
                        if (side == 0) destroy(&t.map);
                        else t.map.Destroy();
                    } else {
                        if (side == 0) freeNodes(&t.map.mList);
                        else t.map.mList.FreeNodes();
                    }
                    rec.Stop();
                    freed[side] = rec.Count();
                    overflow = rec.Overflowed();
                    rec.SortedSizes(sizes[side], kMaxSizes);
                }
                if (overflow || freed[side] > kMaxSizes) ctx.Fail("ReplayMap free %d seed %u: too many frees to record", what, seed);
                if (what == 0) {
                    shape[side] = Shape(t.map);
                    destroy(&t.map);
                } else if (what == 2) {
                    std::vector<TreeReplayNode *>().swap(t.map.mBuckets);
                }
            }
            ++ctx.cases;
            const bool sameSizes = freed[0] == freed[1] && freed[0] <= kMaxSizes &&
                                   std::equal(sizes[0], sizes[0] + freed[0], sizes[1]);
            if (!sameSizes || shape[0] != shape[1])
                ctx.Fail("ReplayMap free %d seed %u: freed %zu blocks (original) / %zu (ours), sizes %s, cleared shape %s", what, seed, freed[0], freed[1],
                         sameSizes ? "same" : "DIFFERENT", shape[0] == shape[1] ? "same" : "DIFFERENT");
        }
    }
}

// Rehash on lists that hold equal keys (a hash_map never inserts them, but the rehash code handles them: equal keys end up
// next to each other): nodes linked by hand, then re-bucketed by each side.
DIFF_TEST(TreeSim_ReplayMapRehashDuplicates, 0x578680)
{
    auto rehash = ctx.Original<RehashFn>(0x578680);
    auto destroy = ctx.Original<VoidFn>(0x5757a0);
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Table a, b;
        std::mt19937 r(seed + 5);
        const uint32_t domain = 1 + seed % 12;
        const uint32_t count = r() % 60;
        for (uint32_t k = 0; k < count; ++k) {
            const TreeReplayKey key = KeyFrom(r, domain);
            for (Table *t : {&a, &b}) {
                TreeReplayNode *node = static_cast<TreeReplayNode *>(::operator new(0x20));
                node->mKey = key;
                new (&node->mRows) std::vector<TreeReplayRow>();
                TreeReplayNode *head = t->map.mList.mHead;
                node->next = head;
                node->prev = head->prev;
                head->prev->next = node;
                head->prev = node;
                ++t->map.mList.mSize;
            }
        }
        const uint32_t buckets = 1 + r() % 100;
        rehash(&a.map, buckets);
        b.map.Rehash(buckets);
        ++ctx.cases;
        if (Shape(a.map) != Shape(b.map)) ctx.Fail("ReplayMap rehash of duplicate keys, seed %u: the tables differ", seed);
        destroy(&a.map);
        destroy(&b.map);
    }
}
