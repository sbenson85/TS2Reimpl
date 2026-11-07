// Differential tests for nGZGraphic4::cPixelFormatIterator (lane NK, rendering campaign R2; src/gz/cPixelFormatIterator.h).
//
// Both sides run on the same fixture: a fake fixed pool at 0xfec088 (three 0x800-byte pages, rebuilt per side, so the
// iterators of both sides sit at the same addresses), the game's operator new / delete (0x824632 / 0x824624) stood in by
// the heap for the original's set nodes, and fake pixel formats whose GetClassId (slot 5) and Less (slot 10) log every
// call (who, with whom, strict), so the sets' comparator order is compared call by call. The original side's objects
// come from the image's creators (0x15ea00, 0x15d040 on a pool block, 0x15e860 with its table 0xd9cf78 pointed at the
// fakes) and ours from ours (the store's table pointed at the same fakes); then a random script of slot calls runs on
// both through each object's own vtable. Compared: every result, the fakes' call log, the count and filter words of
// every live iterator (+4, +8: the same layout) and the pool's pages at the end. The set's node pointers (+0xc, +0x10
// and the nodes) are heap addresses and differ by side; their effect is compared through Get / IsEqual / walks.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/PixelFormatStore.h"
#include "gz/cPixelFormatIterator.h"
#include "rz/RZFixedAllocator.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

using namespace nGZGraphic4;

namespace {

constexpr uint32_t kPoolRva = 0xfec088;
constexpr uint32_t kTableRva = 0xd9cf78; // the manager's 56 static formats (CreateIterator's table)
constexpr uint32_t kOriginalEmpty = 0x15ea00;
constexpr uint32_t kOriginalConstruct = 0x15d040;
constexpr uint32_t kOriginalCreateRange = 0x15e860;
constexpr uint32_t kGameVTable = 0xd9d084;

// ---- the log ----

std::vector<uint32_t> gLog;
void Ev(uint32_t w) { gLog.push_back(w); }

// ---- fake pixel formats ----

constexpr unsigned kFormats = 56;
struct FakeFormat {
    void **vt;
    uint32_t classId;
    uint32_t key;   // the order Less answers by (duplicates: equal formats)
    uint32_t index; // its name in the log
};
FakeFormat gFormats[kFormats];
FakeFormat *gFormatPtrs[kFormats];

void __fastcall FormatUnexpected(FakeFormat *self, void *) { Ev(0xbad00000 | self->index); }
uint32_t __fastcall FormatClassId(FakeFormat *self, void *)
{
    Ev(0x51000000 | self->index);
    return self->classId;
}
bool __fastcall FormatLess(FakeFormat *self, void *, const FakeFormat *other, uint32_t strict)
{
    Ev(0x1e000000 | self->index << 8 | other->index);
    Ev(strict & 0xff);
    return self->key < other->key;
}
void *gFormatVT[11];

void BuildFormats(std::mt19937 &r, unsigned classes, unsigned keys)
{
    for (unsigned i = 0; i < 11; ++i) gFormatVT[i] = reinterpret_cast<void *>(&FormatUnexpected);
    gFormatVT[5] = reinterpret_cast<void *>(&FormatClassId);
    gFormatVT[10] = reinterpret_cast<void *>(&FormatLess);
    for (unsigned i = 0; i < kFormats; ++i) {
        gFormats[i] = {gFormatVT, 0x7c000000u + uint32_t(r() % classes), uint32_t(r() % keys), i};
        gFormatPtrs[i] = &gFormats[i];
    }
}

// ---- the game's heap ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- the pool ----

constexpr uint32_t kPage = 0x800;
constexpr uint32_t kBlock = 0x14;
constexpr uint32_t kBlocks = (kPage - 16) / kBlock;
uint8_t *gPages;
RZFixedPage *Page(int i) { return reinterpret_cast<RZFixedPage *>(gPages + kPage * uint32_t(i)); }
uint8_t *Block(int p, uint32_t k) { return gPages + kPage * uint32_t(p) + 16 + k * kBlock; }

void ChainPage(int p, uint32_t freeBlocks)
{
    uint8_t *first = nullptr;
    for (uint32_t k = freeBlocks; k-- > 0;) {
        *reinterpret_cast<uint8_t **>(Block(p, k)) = first;
        first = Block(p, k);
    }
    Page(p)->freeList = first;
    Page(p)->used = kBlocks - freeBlocks;
    Page(p)->next = Page(p)->prev = Page(p);
}
int gNextPage;
RZFixedPage *__fastcall PoolNewPage(void *, void *)
{
    Ev(0x77000002);
    const int p = gNextPage < 3 ? gNextPage++ : 2;
    ChainPage(p, kBlocks);
    Page(p)->used = 0;
    return Page(p);
}
void __fastcall PoolFreePage(void *, void *, RZFixedPage *page)
{
    Ev(0x77000003);
    Ev(uint32_t((reinterpret_cast<uint8_t *>(page) - gPages) / kPage));
}
void *gPoolVT[4] = {nullptr, nullptr, reinterpret_cast<void *>(&PoolNewPage), reinterpret_cast<void *>(&PoolFreePage)};

void BuildPool(uint32_t freeBlocks)
{
    if (!gPages) gPages = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::memset(gPages, 0xcd, kPage * 3);
    ChainPage(0, freeBlocks);
    gNextPage = 1;
    uint32_t *pool = GameAddress<uint32_t>(kPoolRva);
    pool[0] = uint32_t(uintptr_t(gPoolVT));
    pool[1] = uint32_t(uintptr_t(Page(0)));
    pool[2] = 0;
}

uint32_t Norm(uint32_t w)
{
    const uintptr_t base = uintptr_t(gPages);
    if (w >= base && w < base + kPage * 3) return 0x9a000000u | uint32_t(w - base);
    return w;
}

void DumpPool(std::vector<uint32_t> &out)
{
    const uint32_t *pool = GameAddress<uint32_t>(kPoolRva);
    out.push_back(Norm(pool[1]));
    out.push_back(Norm(pool[2]));
    for (int p = 0; p < 3; ++p) {
        out.push_back(Norm(uint32_t(uintptr_t(Page(p)->next))));
        out.push_back(Norm(uint32_t(uintptr_t(Page(p)->prev))));
        out.push_back(Norm(uint32_t(uintptr_t(Page(p)->freeList))));
        out.push_back(Page(p)->used);
    }
}

// ---- our store over the fakes ----

PixelFormatStore gStore;
const PixelFormatStore &FakeStore() { return gStore; }

struct Scope {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    uint32_t savedPool[3];
    uint32_t savedTable[kFormats];
    PixelFormatStoreProvider savedProvider;
    Scope()
    {
        std::memcpy(savedPool, GameAddress<uint32_t>(kPoolRva), sizeof(savedPool));
        uint32_t *table = GameAddress<uint32_t>(kTableRva);
        DWORD old;
        VirtualProtect(table, sizeof(savedTable), PAGE_READWRITE, &old);
        std::memcpy(savedTable, table, sizeof(savedTable));
        for (unsigned i = 0; i < kFormats; ++i) table[i] = uint32_t(uintptr_t(&gFormats[i]));
        gStore.table = reinterpret_cast<cPixelFormat *const *>(gFormatPtrs);
        gStore.unknown = nullptr;
        gStore.createIterator = nullptr;
        savedProvider = CurrentPixelFormatStoreProvider();
        SetPixelFormatStoreProvider(&FakeStore);
    }
    ~Scope()
    {
        std::memcpy(GameAddress<uint32_t>(kPoolRva), savedPool, sizeof(savedPool));
        std::memcpy(GameAddress<uint32_t>(kTableRva), savedTable, sizeof(savedTable));
        SetPixelFormatStoreProvider(savedProvider);
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

// ---- slot calls ----

template <class R, class... A> R Call(void *object, unsigned slot, A... a)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, a...);
}

uint32_t FormatName(const void *f)
{
    for (unsigned i = 0; i < kFormats; ++i)
        if (f == &gFormats[i]) return 0x46000000u | i;
    return 0xdead0000u;
}

// One side's run: the objects come from `make` (on the side's creators), then the script.
enum class Make { Empty, Construct, Range };

struct Run {
    std::vector<uint32_t> results;
    std::vector<uint32_t> log;
    std::vector<uint32_t> pool;
    bool vtableOk = true;
};

bool AtEnd(void *it)
{
    void *end = Call<void *>(it, 8);
    Call<uint32_t>(end, 1);
    Call<void>(end, 10);
    const bool atEnd = Call<bool>(it, 4, end);
    Call<uint32_t>(end, 2);
    return atEnd;
}

void Walk(void *it, std::vector<uint32_t> &out)
{
    void *w = Call<void *>(it, 8);
    Call<uint32_t>(w, 1);
    Call<void>(w, 9);
    unsigned guard = 0;
    while (!AtEnd(w) && guard++ < 200) {
        out.push_back(FormatName(Call<void *>(w, 5)));
        Call<void>(w, 6);
    }
    out.push_back(0xe0d00000u | guard);
    Call<uint32_t>(w, 2);
}

Run RunSide(bool original, difftest::Context &ctx, uint32_t seed, Make make)
{
    Run run;
    gLog.clear();
    std::mt19937 r(seed * 2654435761u + 11);
    BuildFormats(r, 1 + r() % 4, 1 + r() % 40);
    BuildPool(r() % 3 == 0 ? 0 : 1 + r() % kBlocks);
    const uint32_t filter = r() % 3 == 0 ? 0 : 0x7c000000u + r() % 5;
    const int32_t count = int32_t(r() % (kFormats + 1));

    std::vector<void *> live;
    // The sets and their nodes come from the heap on both sides (the original's through the game's operator new,
    // stood in above): count them, so a set never freed (a lost Release) shows. The test's own vectors are reserved
    // first so their growth is not counted.
    live.reserve(4096);
    run.results.reserve(1u << 16);
    gLog.reserve(1u << 18);
    difftest::AllocationCounter heap;
    switch (make) {
    case Make::Empty: {
        void *it = original ? ctx.Original<void *(__cdecl *)()>(kOriginalEmpty)() : cPixelFormatIterator::CreateEmpty();
        Call<uint32_t>(it, 1);
        live.push_back(it);
        break;
    }
    case Make::Construct: {
        void *it;
        if (original) {
            void *block = GameAddress<RZFixedAllocatorBase>(kPoolRva)->Alloc();
            it = ctx.Original<void *(__thiscall *)(void *, const void *, int32_t, uint32_t)>(kOriginalConstruct)(
                block, gFormatPtrs, count, filter);
        } else {
            it = new cPixelFormatIterator(reinterpret_cast<const cIPixelFormat *const *>(gFormatPtrs), count, filter);
        }
        Call<uint32_t>(it, 1);
        live.push_back(it);
        break;
    }
    case Make::Range: {
        void *range[3] = {};
        void *const back = original ? ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(kOriginalCreateRange)(
                                          nullptr, range, filter)
                                    : static_cast<void *>(cPixelFormatIterator::CreateRange(
                                          nullptr, reinterpret_cast<cPixelFormatRange *>(range), filter));
        run.results.push_back(back == range);
        for (void *it : range) {
            run.results.push_back(it ? 1 : 0);
            if (it) live.push_back(it);
        }
        if (range[0] && range[2]) run.results.push_back(Call<bool>(range[0], 4, range[2]));
        break;
    }
    }
    for (void *it : live) {
        if (*static_cast<void **>(it) == nullptr) run.vtableOk = false;
        if (original != (*static_cast<uint32_t *>(it) == uint32_t(uintptr_t(GameAddress<void>(kGameVTable)))))
            run.vtableOk = false;
    }

    const unsigned ops = 20 + r() % 60;
    for (unsigned op = 0; op < ops && !live.empty(); ++op) {
        void *const it = live[r() % live.size()];
        const unsigned kind = r() % 16;
        run.results.push_back(0x0b000000u | kind);
        switch (kind) {
        case 0: { // Clone (slot 3)
            void *c = Call<void *>(it, 3);
            run.results.push_back(Call<uint32_t>(c, 1));
            live.push_back(c);
            break;
        }
        case 1: { // IsEqual
            void *other = live[r() % live.size()];
            run.results.push_back(Call<bool>(it, 4, other));
            break;
        }
        case 2: // Get, where there is a format
            if (!AtEnd(it)) run.results.push_back(FormatName(Call<void *>(it, 5)));
            break;
        case 3: Call<void>(it, 6); break;
        case 4: Call<void>(it, 7); break;
        case 5: { // CloneFormatIterator (slot 8), count 0
            void *c = Call<void *>(it, 8);
            run.results.push_back(Call<uint32_t>(c, 1));
            live.push_back(c);
            break;
        }
        case 6: Call<void>(it, 9); break;
        case 7: Call<void>(it, 10); break;
        case 8: // SetFilter: it erases nodes from the set it shares with its clones (0x15fcc0 erases through the set's
                // own erase), so clones whose cursor sat on an erased node would hold a freed node; set the others
                // back (its own cursor is at the first format afterwards: compared here).
            Call<void>(it, 11, r() % 3 == 0 ? 0u : 0x7c000000u + r() % 5);
            run.results.push_back(AtEnd(it) ? 0xe0d0e0d0u : FormatName(Call<void *>(it, 5)));
            for (void *l : live)
                if (l != it) Call<void>(l, 9);
            break;
        case 9: Call<void>(it, 12, static_cast<void *>(&gFormats[r() % kFormats])); break;
        case 10: run.results.push_back(Call<uint32_t>(it, 13)); break;
        case 11: { // QueryInterface
            static const uint32_t kIids[] = {1, cPixelFormatIterator::kIid, 0x3b6739f4, 0xbb782354, 0};
            const uint32_t iid = kIids[r() % 5];
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const bool withOut = r() % 4 != 0;
            const bool ok = Call<bool>(it, 0, iid, withOut ? &out : static_cast<void **>(nullptr));
            run.results.push_back(ok);
            run.results.push_back(out == it ? 1 : out == reinterpret_cast<void *>(0x5a5a5a5a) ? 2 : 3);
            if (ok) run.results.push_back(Call<uint32_t>(it, 2));
            break;
        }
        case 12: { // AddRef, Release
            run.results.push_back(Call<uint32_t>(it, 1));
            run.results.push_back(Call<uint32_t>(it, 2));
            break;
        }
        case 13: { // Release one owned reference
            for (size_t i = 0; i < live.size(); ++i)
                if (live[i] == it) {
                    live.erase(live.begin() + ptrdiff_t(i));
                    break;
                }
            run.results.push_back(Call<uint32_t>(it, 2));
            break;
        }
        default: Walk(it, run.results); break;
        }
        // The count and filter words of every live iterator (the same layout on both sides).
        for (void *l : live) {
            run.results.push_back(static_cast<uint32_t *>(l)[1]);
            run.results.push_back(static_cast<uint32_t *>(l)[2]);
            run.results.push_back(Norm(uint32_t(uintptr_t(l))));
        }
    }
    for (void *l : live) Walk(l, run.results);
    while (!live.empty()) {
        run.results.push_back(Call<uint32_t>(live.back(), 2));
        live.pop_back();
    }
    heap.Stop();
    run.results.push_back(uint32_t(heap.Blocks()));
    run.results.push_back(uint32_t(heap.Bytes()));
    run.results.push_back(run.results.size() > (1u << 16) || gLog.size() > (1u << 18) || live.capacity() > 4096);
    run.log = gLog;
    DumpPool(run.pool);
    return run;
}

// The original side's script runs the image's bodies through the game's vtables; these are they (checked against the
// vtables before the run, so the comparison covers exactly these functions):
//   iterator 0xd9d084: 0 QueryInterface 0x15fc40, 5 Get 0x15e810, 6 Next 0x15f380, 7 Prev 0x15f910,
//   8 CloneFormatIterator 0x15e590, 9 SetToBegin 0x15e530, 10 SetToEnd 0x15e830, 11 SetFilter 0x15fcc0,
//   12 Insert 0x15e3f0, 14 deleting destructor 0x15e290 (released to zero by the script);
//   counted set 0xd9d070: 2 Release 0x15fca0, 3 deleting destructor 0x15e030 (each iterator's release of its set).
struct SlotRva {
    uint32_t vtable;
    unsigned slot;
    uint32_t rva;
};
constexpr SlotRva kSlotRvas[] = {
    {kGameVTable, 0, 0x15fc40},  {kGameVTable, 5, 0x15e810},  {kGameVTable, 6, 0x15f380},  {kGameVTable, 7, 0x15f910},
    {kGameVTable, 8, 0x15e590},  {kGameVTable, 9, 0x15e530},  {kGameVTable, 10, 0x15e830}, {kGameVTable, 11, 0x15fcc0},
    {kGameVTable, 12, 0x15e3f0}, {kGameVTable, 14, 0x15e290}, {0xd9d070, 2, 0x15fca0},      {0xd9d070, 3, 0x15e030},
};

void Compare(difftest::Context &ctx, Make make, unsigned seeds)
{
    for (const SlotRva &s : kSlotRvas)
        if (GameAddress<uint32_t>(s.vtable)[s.slot] != uint32_t(uintptr_t(GameAddress<void>(s.rva))))
            ctx.Fail("vtable %#x slot %u is not %#x", s.vtable, s.slot, s.rva);
    Scope scope;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        ++ctx.cases;
        const Run a = RunSide(true, ctx, seed, make);
        const Run b = RunSide(false, ctx, seed, make);
        if (!a.vtableOk || !b.vtableOk) ctx.Fail("seed %u: vtables (original %d, ours %d)", seed, a.vtableOk, b.vtableOk);
        if (a.results != b.results) {
            size_t i = 0;
            while (i < a.results.size() && i < b.results.size() && a.results[i] == b.results[i]) ++i;
            ctx.Fail("seed %u: results differ at %u of %u/%u (%#x vs %#x)", seed, unsigned(i), unsigned(a.results.size()),
                     unsigned(b.results.size()), i < a.results.size() ? a.results[i] : 0,
                     i < b.results.size() ? b.results[i] : 0);
        }
        if (a.log != b.log) {
            size_t i = 0;
            while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
            ctx.Fail("seed %u: fake calls differ at %u of %u/%u (%#x vs %#x)", seed, unsigned(i), unsigned(a.log.size()),
                     unsigned(b.log.size()), i < a.log.size() ? a.log[i] : 0, i < b.log.size() ? b.log[i] : 0);
        }
        if (a.pool != b.pool) ctx.Fail("seed %u: pool differs", seed);
    }
}

} // namespace

DIFF_TEST(cPixelFormatIterator_CreateEmpty, 0x15ea00) { Compare(ctx, Make::Empty, 300); }

DIFF_TEST(cPixelFormatIterator_Construct, 0x15d040) { Compare(ctx, Make::Construct, 300); }

DIFF_TEST(cPixelFormatsManagerDX_CreateIterator, 0x15e860) { Compare(ctx, Make::Range, 300); }
