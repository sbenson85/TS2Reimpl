// Differential tests for nGZSceneGraph::cStringPool, its handle iterator and the Jenkins hash (lane OX, src/sg/cStringPool.*).
//
// SG_StringPool_Hash: 0x98bff0 against JenkinsHash over random buffers (every tail length, seeds 0 and random).
// SG_StringPool_Slots: the game's pool built by its constructor 0xadf120 in a zeroed 0x70-byte block (the static it
// lives in is zero-initialised: the key's count word +0x40, which every new entry copies, is never written), ours by our
// constructor; the same seeded sequence of slot calls through each object's own vtable, every answer noted and compared:
// Init / Shutdown, Intern (case kept or not, null, empty, texts that collide once lowercased, texts past the 15-character
// inline buffer), AddRefHandle / ReleaseHandle (live, released, -1, out of range; counts going below zero), GetText (the
// text; for no entry, whether the answer is the image's empty string 0xd9b9d0), GetLength, GetHash, GetCrc, GetHandles
// (the cursor walked to the end, a clone, QueryInterface and IsEqual on the iterators, every iterator released), and
// QueryInterface on the pool. Enough texts are interned in long runs that the game's hash table rehashes (0xadfc00) many
// times. Each side is destroyed through its slot 13 at the end (0xadf600, the destructor 0xadf460).
//
// RVAs this file verifies: 0x98bff0 (JenkinsHash); the pool 0xadf8b0 (the getter: SG_StringPool_Instance), 0xadf120,
// 0xadf460, 0xadf600, 0xadf940, 0xadf8a0, 0xadfab0, 0xadf740, 0xadf710, 0xadf970, 0xadfae0, 0xadfbd0, 0xadfba0, 0xadfb20,
// 0xadf620; the game's hash-table code the pool alone calls (our std::unordered_map stands for it): 0xaded50 (find),
// 0xadede0 (emplace), 0xadfc00 (rehash); the handle iterator 0xadf0e0, 0xadf0b0, 0xadf6e0, 0xadf5c0, 0xadf450 and its
// pool's 0xadf510 / 0xadf540 (operator new / delete on 0x10f8c60).
//
// The iterators of both sides come from the game's fixed-size pool at 0x10f8c60 (ours through RZFastFixed0x10, the
// game's through 0xadf510): the test gives it pages of its own through a fake vtable (NewPage / FreePage) and restores its
// three words after.
#include "difftest.h"
#include "game/game.h"
#include "rz/RZFixedAllocator.h"
#include "rz/StringKeyParser.h"
#include "sg/cStringPool.h"

#include <windows.h>

#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

using nGZSceneGraph::cIStringPool;
using nGZSceneGraph::cStringPool;
using nGZEvent::cIAttributeIDIterator;

namespace {

constexpr uint32_t kEmptyText = 0xd9b9d0;
constexpr uint32_t kGameSize = 0x70;
constexpr uint32_t kNoHandle = 0xffffffffu;

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ---- the iterators' pool (0x10f8c60, a cRZFastFixedAllocator2<0x10>: pages of 0x800 bytes, 127 blocks each) ----
struct IteratorPool {
    static constexpr uint32_t kRva = nGZSceneGraph::cStringHandleIterator::kPool;
    static constexpr uint32_t kPage = 0x800;
    static constexpr uint32_t kBlock = 0x10;
    static constexpr uint32_t kBlocks = (kPage - 16) / kBlock;
    static constexpr int kPages = 8;
    static inline uint8_t *region = nullptr;
    static inline int next = 0;
    static inline int failures = 0;
    static inline void *vt[4] = {};
    uint32_t saved[3] = {};

    static uint32_t *Words() { return GameAddress<uint32_t>(kRva); }
    static RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(region + uint32_t(i) * kPage); }
    static void InitPage(RZFixedPage *p)
    {
        p->next = p;
        p->prev = p;
        p->used = 0;
        uint8_t *base = reinterpret_cast<uint8_t *>(p);
        for (uint32_t k = 0; k < kBlocks; ++k) {
            void **block = reinterpret_cast<void **>(base + 16 + k * kBlock);
            *block = k + 1 < kBlocks ? base + 16 + (k + 1) * kBlock : nullptr;
        }
        p->freeList = base + 16;
    }
    static RZFixedPage *__fastcall NewPage(void *, void *)
    {
        if (next >= kPages) {
            ++failures;
            return nullptr;
        }
        RZFixedPage *p = Page(next++);
        InitPage(p);
        return p;
    }
    static void __fastcall FreePage(void *, void *, RZFixedPage *) {}

    IteratorPool()
    {
        std::memcpy(saved, Words(), sizeof(saved));
        region = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPages * kPage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        vt[2] = reinterpret_cast<void *>(&NewPage);
        vt[3] = reinterpret_cast<void *>(&FreePage);
        Reset();
    }
    ~IteratorPool()
    {
        std::memcpy(Words(), saved, sizeof(saved));
        VirtualFree(region, 0, MEM_RELEASE);
        region = nullptr;
    }
    IteratorPool(const IteratorPool &) = delete;
    IteratorPool &operator=(const IteratorPool &) = delete;
    static void Reset()
    {
        next = 1;
        InitPage(Page(0));
        Words()[0] = P(vt);
        Words()[1] = P(Page(0));
        Words()[2] = 0;
    }
};

// One side's pool: the game's (its constructor on a zeroed block) or ours.
struct Side {
    bool game;
    alignas(8) uint8_t block[kGameSize + 16];
    cIStringPool *pool = nullptr;
    cStringPool *ours = nullptr;

    Side(difftest::Context &ctx, bool isGame) : game(isGame)
    {
        std::memset(block, 0, sizeof(block));
        if (game) {
            auto ctor = ctx.Original<void *(__thiscall *)(void *)>(0xadf120);
            void *self = ctor(block);
            if (self != block) ctx.Fail("game constructor returned %p, not its block %p", self, block);
            pool = reinterpret_cast<cIStringPool *>(block);
        } else {
            ours = new cStringPool();
            pool = ours;
        }
    }
    ~Side()
    {
        if (game) {
            // Slot 13 with flags 0: the destructor only (the block is the test's).
            using Fn = void *(__thiscall *)(void *, uint32_t);
            Fn dtor = reinterpret_cast<Fn>((*reinterpret_cast<void ***>(block))[13]);
            dtor(block, 0);
        } else {
            delete ours;
        }
    }
    Side(const Side &) = delete;
    Side &operator=(const Side &) = delete;
};

struct Gen {
    std::mt19937 rng;
    explicit Gen(uint32_t seed) : rng(seed) {}
    uint32_t Below(uint32_t n) { return n ? rng() % n : 0; }
    bool Coin() { return rng() % 2 != 0; }
};

// The texts: a fixed vocabulary (case variants that meet once lowercased, lengths around the inline buffer) plus
// generated ones, so long runs grow the table through several rehashes.
std::vector<std::string> Vocabulary(Gen &g)
{
    std::vector<std::string> v = {"a", "A", "Shape", "shape", "SHAPE", "cGeometryDataContainer", "cgeometrydatacontainer",
                                  "fifteen_chars__", "Fifteen_Chars__", "sixteen_chars___", "x", "tag_0", "TAG_0",
                                  "\xc3\x89t\xc3\xa9", "\xc3\xa9T\xc3\xa9", "@[`{", "body_skin", "Body_Skin_Long_Name_01"};
    for (int i = 0; i < 160; ++i) {
        static const char alphabet[] = "abcXYZ_09.-";
        std::string s;
        const uint32_t len = 1 + g.Below(24);
        for (uint32_t k = 0; k < len; ++k) s += alphabet[g.Below(sizeof(alphabet) - 1)];
        v.push_back(s);
    }
    return v;
}

// One answer: a label and words.
struct Note {
    std::string what;
    std::vector<uint32_t> words;
    std::string text;
    bool operator==(const Note &o) const { return what == o.what && words == o.words && text == o.text; }
};

struct Run {
    std::vector<Note> notes;
    void Add(std::string what, std::vector<uint32_t> words, std::string text = {})
    {
        notes.push_back({std::move(what), std::move(words), std::move(text)});
    }
};

uint32_t PickHandle(Gen &g, const std::vector<uint32_t> &seen)
{
    switch (g.Below(10)) {
    case 0: return kNoHandle;
    case 1: return 300 + g.Below(100);
    case 2: return g.Below(8);
    default: return seen.empty() ? g.Below(40) : seen[g.Below(static_cast<uint32_t>(seen.size()))];
    }
}

void WalkHandles(difftest::Context &ctx, cIStringPool *pool, Run &run, Gen &g)
{
    nGZEvent::cAttributeIDRange r = pool->GetHandles();
    std::vector<uint32_t> words{r.mCurrent != nullptr, r.mBegin != nullptr, r.mEnd != nullptr};
    if (!r.mCurrent || !r.mBegin || !r.mEnd) {
        run.Add("handles (null)", words);
        return;
    }
    words.push_back(r.mBegin->Get());
    words.push_back(r.mEnd->Get());
    words.push_back(r.mCurrent->IsEqual(r.mBegin));
    // QueryInterface on the cursor: cIGZUnknown only, the null-out case.
    void *out = nullptr;
    words.push_back(r.mCurrent->QueryInterface(1, &out));
    words.push_back(out == r.mCurrent);
    if (out) static_cast<cIAttributeIDIterator *>(out)->Release();
    words.push_back(r.mCurrent->QueryInterface(0x4caa9810, &out));
    words.push_back(r.mCurrent->QueryInterface(1, nullptr));
    cIAttributeIDIterator *mark = nullptr;
    const uint32_t cloneAt = g.Below(6);
    uint32_t steps = 0;
    while (!r.mCurrent->IsEqual(r.mEnd) && steps < 1000) {
        words.push_back(r.mCurrent->Get());
        if (steps == cloneAt) {
            mark = r.mCurrent->Clone();
            if (mark) mark->AddRef();
        }
        r.mCurrent->Next();
        ++steps;
    }
    words.push_back(steps);
    words.push_back(r.mCurrent->Get());
    if (mark) {
        words.push_back(mark->Get());
        words.push_back(mark->IsEqual(r.mBegin));
        words.push_back(mark->AddRef());
        words.push_back(mark->Release());
        words.push_back(mark->Release());
    }
    words.push_back(r.mEnd->Release());
    words.push_back(r.mBegin->Release());
    words.push_back(r.mCurrent->Release());
    (void)ctx;
    run.Add("handles", words);
}

Run RunSide(difftest::Context &ctx, bool game, uint32_t seed)
{
    Run run;
    Side side(ctx, game);
    cIStringPool *pool = side.pool;
    const char *empty = ctx.image.At<const char>(kEmptyText);
    Gen g(seed);
    std::vector<std::string> vocab = Vocabulary(g);
    std::vector<uint32_t> seen;
    if (g.Below(8) != 0) pool->Init();
    const int ops = 150 + static_cast<int>(g.Below(450));
    for (int i = 0; i < ops; ++i) {
        const uint32_t op = g.Below(100);
        if (op < 2) {
            pool->Init();
            run.Add("init", {});
        } else if (op < 4) {
            pool->Shutdown();
            run.Add("shutdown", {});
            if (g.Coin()) pool->Init();
        } else if (op < 40) {
            const char *text = nullptr;
            std::string holder;
            const uint32_t pick = g.Below(40);
            if (pick == 0) text = nullptr;
            else if (pick == 1) text = "";
            else {
                holder = vocab[g.Below(static_cast<uint32_t>(vocab.size()))];
                text = holder.c_str();
            }
            const bool keepCase = g.Coin();
            const uint32_t h = pool->Intern(text, keepCase);
            if (h != kNoHandle) seen.push_back(h);
            run.Add("intern", {h, keepCase}, text ? text : "(null)");
        } else if (op < 55) {
            const uint32_t h = PickHandle(g, seen);
            pool->AddRefHandle(h);
            run.Add("addref", {h});
        } else if (op < 72) {
            const uint32_t h = PickHandle(g, seen);
            pool->ReleaseHandle(h);
            run.Add("release", {h});
        } else if (op < 80) {
            const uint32_t h = PickHandle(g, seen);
            const char *text = pool->GetText(h);
            run.Add("text", {h, text == empty, text != nullptr}, text ? text : "(null)");
        } else if (op < 85) {
            const uint32_t h = PickHandle(g, seen);
            run.Add("length", {h, pool->GetLength(h)});
        } else if (op < 90) {
            const uint32_t h = PickHandle(g, seen);
            run.Add("hash", {h, pool->GetHash(h)});
        } else if (op < 95) {
            const uint32_t h = PickHandle(g, seen);
            run.Add("crc", {h, pool->GetCrc(h)});
        } else if (op < 98) {
            WalkHandles(ctx, pool, run, g);
        } else {
            void *out = nullptr;
            const uint32_t iid = g.Below(3) == 0 ? 1 : g.Coin() ? cIStringPool::kIid : 0x12345678;
            const bool nullOut = g.Below(5) == 0;
            const bool ok = pool->QueryInterface(iid, nullOut ? nullptr : &out);
            run.Add("qi", {iid, nullOut, ok, out == pool, pool->AddRef(), pool->Release()});
        }
    }
    // Every live handle's text, length, hash and crc at the end.
    for (uint32_t h = 0; h < 64; ++h) {
        const char *text = pool->GetText(h);
        run.Add("final", {h, text == empty, pool->GetLength(h), pool->GetHash(h), pool->GetCrc(h)}, text ? text : "");
    }
    return run;
}

std::string Show(const Note &n)
{
    std::string s = n.what;
    char buf[16];
    for (uint32_t w : n.words) {
        std::snprintf(buf, sizeof(buf), " %x", w);
        s += buf;
    }
    if (!n.text.empty()) s += " '" + n.text + "'";
    return s;
}

void BuildCrcTables()
{
    uint32_t dummy = 0;
    reinterpret_cast<StringKeyParser::CrcTables *>(&dummy)->Initialize();
}

} // namespace

DIFF_TEST(SG_StringPool_Hash, 0x98bff0)
{
    auto original = ctx.Original<uint32_t(__cdecl *)(const void *, uint32_t, uint32_t)>(0x98bff0);
    std::mt19937 rng(0x98bff0);
    std::vector<uint8_t> buffer(80);
    for (int i = 0; i < 20000; ++i) {
        const uint32_t length = i < 64 ? static_cast<uint32_t>(i) : rng() % 72;
        for (auto &b : buffer) b = static_cast<uint8_t>(rng());
        const uint32_t seed = i % 3 == 0 ? 0 : static_cast<uint32_t>(rng());
        const uint32_t a = original(buffer.data(), length, seed);
        const uint32_t b = nGZSceneGraph::JenkinsHash(buffer.data(), length, seed);
        ++ctx.cases;
        if (a != b) ctx.Fail("length %u seed %08x: original %08x, ours %08x", length, seed, a, b);
    }
}

DIFF_TEST(SG_StringPool_Slots, 0xadf740)
{
    BuildCrcTables();
    IteratorPool iterators;
    for (uint32_t seed = 1; seed <= 120; ++seed) {
        IteratorPool::Reset();
        ctx.Mark("game side");
        Run a = RunSide(ctx, true, seed);
        IteratorPool::Reset();
        ctx.Mark("our side");
        Run b = RunSide(ctx, false, seed);
        ++ctx.cases;
        if (a.notes.size() != b.notes.size()) {
            ctx.Fail("seed %u: %zu answers from the original, %zu from ours", seed, a.notes.size(), b.notes.size());
            continue;
        }
        for (size_t i = 0; i < a.notes.size(); ++i) {
            if (!(a.notes[i] == b.notes[i])) {
                ctx.Fail("seed %u step %zu: original %s, ours %s", seed, i, Show(a.notes[i]).c_str(), Show(b.notes[i]).c_str());
                break;
            }
        }
    }
    if (IteratorPool::failures) ctx.Fail("the iterator pool ran out of pages %d times", IteratorPool::failures);
}

// 0xadf8b0: the getter answers one object, the same every time, started by nobody (Init is the system's).
DIFF_TEST(SG_StringPool_Instance, 0xadf8b0)
{
    cStringPool *a = cStringPool::Instance();
    cStringPool *b = cStringPool::Instance();
    ++ctx.cases;
    if (a == nullptr || a != b) ctx.Fail("Instance answered %p then %p", a, b);
    if (a && a->Intern("x", true) != kNoHandle) ctx.Fail("a fresh singleton answered a handle before Init");
}
