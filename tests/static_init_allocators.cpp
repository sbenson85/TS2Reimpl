// Differential tests for the owned XC entries of the fixed-size pool statics (src/app/StaticInitAllocators.cpp, lane
// HE; docs/subsystems/startup.md "Done in lane HE"): 50 cRZFastFixedAllocator2<size> / cRZFastAllocator<T> objects.
// Both sides on real block pools (tests/rz_block_pool_sides.h: the image's side on the original RZBlockPoolAlloc,
// ours on ours, each in its own arena, pages compared as arena offsets).
//
// StaticInitAllocators_Form: what each image entry does, on the object and 8 bytes on each side, the object's vtable
// word as the file holds it and the rest junk (two patterns): the margins untouched; the vtable word our table's class
// (the file's .data or the entry's own store); the current ring one page of 2^PageLog2(size) bytes, aligned, its free
// list running up from +16 in steps of the row's block size, count 0; no full ring; exactly one registration, of the
// row's thunk; the thunk stores the base class's game vtable, leaves the rest, and gives the page back (the next page
// the pool hands out is it).
//
// StaticInitAllocators_Entries: each entry against the image's in its slot from the same start (the file's vtable word,
// junk elsewhere, three rounds): the window after the entry and after its registered thunk, the vtable word as a code
// (the image's: the game's class vtable, then the base's; ours: our class's / our base's by RTTI name), the ring
// pointers as arena offsets, the page's bytes (pointers as offsets), one registration each, and the pool's next pages
// after the thunk.
//
// StaticInitAllocators_OnImageTable: the runner over a copy of the XC table with only these slots, the image's entries
// against ours: 50 ours, 50 registrations, the same objects (codes, offsets), the same pages handed out after each
// side's exit list run LIFO.
#include "difftest.h"
#include "rz_block_pool_sides.h"
#include "app/StaticInit.h"
#include "app/StaticInitAllocators.h"
#include "app/WinMainStart.h"
#include "rz/RZFastFixedAllocators.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using staticinit::AllocatorStatic;

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kAtExit = 0x824cbe;
constexpr int kMargin = 8, kWindow = 12 + 2 * kMargin;

using Thunk = void(__cdecl *)();

std::vector<Thunk> gRegistered;
int __cdecl RecordAtExit(Thunk f)
{
    gRegistered.push_back(f);
    return 0;
}

// A 5-byte jmp over an image function's entry for the duration of a test.
struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(const difftest::Context &ctx, uint32_t rva, const void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Redirect() { std::memcpy(site, saved, 5); }
};

// Both sides' atexit registrations go to gRegistered: the image's atexit redirected, ours bound.
struct AtExitCapture {
    Redirect image;
    staticinit::RegisterAtExit saved;
    explicit AtExitCapture(const difftest::Context &ctx)
        : image(ctx, kAtExit, reinterpret_cast<const void *>(&RecordAtExit)), saved(staticinit::BoundAtExit())
    {
        staticinit::BindAtExit(&RecordAtExit);
        gRegistered.clear();
    }
    ~AtExitCapture() { staticinit::BindAtExit(saved); }
};

bool SlotHolds(difftest::Context &ctx, uint32_t slot, uint32_t rva)
{
    uint32_t held = *ctx.image.At<uint32_t>(kXcFirst + 4 * slot) - static_cast<uint32_t>(ctx.image.Base());
    if (held == rva) return true;
    ctx.Fail("slot %u holds %08x, our table says %08x", slot, held, rva);
    return false;
}

std::vector<uint8_t> Window(const difftest::Context &ctx, uint32_t storage)
{
    const uint8_t *p = ctx.image.At<uint8_t>(storage - kMargin);
    return std::vector<uint8_t>(p, p + kWindow);
}

void Put(const difftest::Context &ctx, uint32_t storage, const std::vector<uint8_t> &w)
{
    std::memcpy(ctx.image.At<uint8_t>(storage - kMargin), w.data(), w.size());
}

uint32_t WordAt(const std::vector<uint8_t> &w, int at)
{
    uint32_t v;
    std::memcpy(&v, w.data() + at, 4);
    return v;
}

void SetWord(std::vector<uint8_t> &w, int at, uint32_t v) { std::memcpy(w.data() + at, &v, 4); }

uint32_t Addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

std::string RttiName(uint32_t vtable)
{
    auto *col = reinterpret_cast<const uint32_t *const *>(static_cast<uintptr_t>(vtable))[-1];
    return std::string(reinterpret_cast<const char *>(static_cast<uintptr_t>(col[3])) + 8);
}

// The game's vtable of cRZFastFixedAllocator2<size> (the base of every row's class).
uint32_t BaseVTable(uint32_t blockSize)
{
    switch (blockSize) {
    case 0x0c: return 0xd9f9bc;
    case 0x10: return 0xd9f520;
    case 0x14: return 0xd9d05c;
    case 0x18: return 0xd9d578;
    case 0x1c: return 0xd9d564;
    case 0x20: return 0xd9c2d4;
    case 0x28: return 0xd9ded4;
    case 0x34: return 0xda8148;
    case 0x38: return 0xe813c4;
    case 0x3c: return 0xdd3fec;
    case 0x54: return 0xe4f1fc;
    case 0x58: return 0xd9e99c;
    case 0x5c: return 0xd9db38;
    case 0x74: return 0xda75ec;
    case 0x98: return 0xe7588c;
    case 0x9c: return 0xe687ac;
    case 0x19c: return 0xd9dd48;
    case 0x1b0: return 0xdab48c;
    default: return 0;
    }
}

// The vtable word of a window as a code: 1 the game's class vtable, 2 ours (our class by RTTI name), 3 the game's
// base vtable, 4 our base's; 0 anything else (the word is then left for the comparison to show).
int CodeVTable(const difftest::Context &ctx, const AllocatorStatic &e, std::vector<uint8_t> &w)
{
    const uint32_t word = WordAt(w, kMargin);
    const uint32_t base = static_cast<uint32_t>(ctx.image.Base());
    const bool pooled = e.vtable != BaseVTable(e.blockSize);
    int code = 0;
    if (word == base + e.vtable) code = 1;
    else if (word == base + BaseVTable(e.blockSize)) code = 3;
    else if (word && (word < base || word >= base + ctx.image.Size())) {
        const std::string name = RttiName(word);
        const bool fixed = name.rfind(".?AV?$cRZFastFixedAllocator2@", 0) == 0;
        if (pooled && name.rfind(".?AV?$cRZFastAllocator@", 0) == 0) code = 2;
        else if (fixed) code = pooled ? 4 : 2;
    }
    if (code) SetWord(w, kMargin, 0);
    return code;
}

uint32_t PageBytes(const AllocatorStatic &e) { return 1u << rz::PageLog2(e.blockSize); }

// The window with the ring pointers as arena offsets, and the current page's bytes (pointers as offsets).
struct Built {
    std::vector<uint8_t> window;
    int code = -1;
    std::vector<uint32_t> page;
};

Built Capture(const difftest::Context &ctx, const AllocatorStatic &e, int side, bool withPage)
{
    Built b;
    b.window = Window(ctx, e.storage);
    b.code = CodeVTable(ctx, e, b.window);
    const uint32_t current = WordAt(b.window, kMargin + 4);
    if (withPage) {
        const auto *w = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(current));
        for (uint32_t i = 0; i < PageBytes(e) / 4; ++i) b.page.push_back(blockpools::Offset(side, w[i]));
    }
    SetWord(b.window, kMargin + 4, blockpools::Offset(side, current));
    SetWord(b.window, kMargin + 8, blockpools::Offset(side, WordAt(b.window, kMargin + 8)));
    return b;
}

// The 50 image initialisers in slot order, {slot, entry, object, block size} (the XC table and the entries' code: the
// class-inline ones store the size's vtable, the others call its constructor), checked against our table
// independently of it.
struct ImageEntry {
    uint32_t slot, entry, object, blockSize;
};
constexpr ImageEntry kImageEntries[50] = {
    {17, 0x136e90, 0xfeb8e0, 0x20},    {18, 0x136ef0, 0xfec088, 0x14},    {76, 0x137ee0, 0x10ca058, 0x1c},
    {77, 0x137ec0, 0x10ca064, 0x18},   {78, 0x137f20, 0x10ca070, 0x18},   {79, 0x137f00, 0x10ca07c, 0x1c},
    {80, 0x137fa0, 0xfec630, 0x5c},    {81, 0x137f40, 0xfec63c, 0x20},    {82, 0x138000, 0xfec898, 0x19c},
    {83, 0x1380a0, 0x10ca160, 0x1c},   {84, 0x138060, 0x10ca16c, 0x28},   {85, 0x1380c0, 0x10ca178, 0x1c},
    {86, 0x138080, 0x10ca184, 0x28},   {87, 0x1380e0, 0xfed220, 0x58},    {88, 0x138160, 0x10ca2fc, 0x28},
    {89, 0x138140, 0x10ca308, 0x28},   {90, 0x138180, 0xfed8f8, 0x10},    {91, 0x1381e0, 0xfedb58, 0x0c},
    {92, 0x138250, 0xfee35c, 0x18},    {112, 0x1384e0, 0x10cc1e8, 0x74},  {113, 0x13850c, 0x10cc3e8, 0x34},
    {125, 0x138734, 0x10cc5dc, 0x1b0}, {219, 0x13a9a0, 0x1012908, 0x10},  {285, 0x13c440, 0x1015d58, 0x3c},
    {288, 0x13c4f0, 0x1016994, 0x3c},  {504, 0x13e100, 0x105f820, 0x58},  {505, 0x13e160, 0x105f82c, 0x54},
    {560, 0x13e810, 0x10f5f54, 0x9c},  {561, 0x13e850, 0x10f5f60, 0x9c},  {562, 0x13e7f0, 0x10f5f6c, 0x9c},
    {563, 0x13e830, 0x10f5f78, 0x9c},  {565, 0x13e880, 0x10f6020, 0x10},  {566, 0x13e8a0, 0x10f602c, 0x0c},
    {568, 0x13e8d0, 0x10f617c, 0x0c},  {569, 0x13e8f0, 0x10f620c, 0x3c},  {570, 0x13e910, 0x10f6218, 0x10},
    {571, 0x13e930, 0x10f625c, 0x10},  {572, 0x13e950, 0x10f62f0, 0x0c},  {573, 0x13e970, 0x10f6340, 0x14},
    {574, 0x13e990, 0x10f639c, 0x14},  {575, 0x13e9b0, 0x10f6654, 0x98},  {576, 0x13e9d0, 0x10f6704, 0x58},
    {578, 0x13ea00, 0x10f6818, 0x10},  {579, 0x13ea20, 0x10f6824, 0x0c},  {580, 0x13ea40, 0x10f6830, 0x0c},
    {621, 0x13ef50, 0x10f8bd4, 0x10},  {622, 0x13ef70, 0x10f8c60, 0x10},  {641, 0x13f648, 0x10fad94, 0x38},
    {681, 0x140020, 0x10fc590, 0x20},  {732, 0x14050c, 0x10fd4c4, 0x18},
};

} // namespace

DIFF_TEST(StaticInitAllocators_Form, 0x136e90)
{
    AtExitCapture capture(ctx);
    blockpools::Sides pools(ctx);
    size_t n = 0;
    const AllocatorStatic *entries = staticinit::AllocatorStatics(&n);
    if (n != 50) ctx.Fail("%u allocator entries, expected 50", static_cast<unsigned>(n));
    for (size_t i = 0; i < n && i < 50; ++i) {
        const ImageEntry &x = kImageEntries[i];
        const AllocatorStatic &e = entries[i];
        if (e.slot != x.slot || e.imageRva != x.entry || e.storage != x.object || e.blockSize != x.blockSize)
            ctx.Fail("row %u: slot %u entry %x object %x size %x, expected %u %x %x %x", static_cast<unsigned>(i), e.slot,
                     e.imageRva, e.storage, e.blockSize, x.slot, x.entry, x.object, x.blockSize);
    }
    const uint32_t base = static_cast<uint32_t>(ctx.image.Base());
    for (size_t i = 0; i < n; ++i) {
        const AllocatorStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e.storage);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        const uint32_t log2 = rz::PageLog2(e.blockSize);
        for (uint8_t pattern : {uint8_t(0x5a), uint8_t(0xc3)}) {
            blockpools::ResetArenas();
            blockpools::OsRedirect redirect(ctx);
            pools.Begin(0);
            std::vector<uint8_t> junk(kWindow);
            for (int k = 0; k < kWindow; ++k) junk[k] = static_cast<uint8_t>(pattern + k * 7);
            SetWord(junk, kMargin, WordAt(saved, kMargin)); // the file's vtable word
            Put(ctx, e.storage, junk);
            gRegistered.clear();
            image();
            ++ctx.cases;
            std::vector<uint8_t> w = Window(ctx, e.storage);
            const uint32_t vt = WordAt(w, kMargin), current = WordAt(w, kMargin + 4), full = WordAt(w, kMargin + 8);
            if (!std::equal(w.begin(), w.begin() + kMargin, junk.begin()) || !std::equal(w.end() - kMargin, w.end(), junk.end() - kMargin))
                ctx.Fail("slot %u: the image's entry wrote outside the object", e.slot);
            if (vt != base + e.vtable) ctx.Fail("slot %u: vtable %08x, our table says %06x", e.slot, vt - base, e.vtable);
            const auto *page = reinterpret_cast<const RZFixedPage *>(static_cast<uintptr_t>(current));
            bool ok = full == 0 && current && (current & ((1u << log2) - 1)) == 0 && page->next == page && page->prev == page &&
                      page->used == 0 && Addr(page->freeList) == current + 16;
            for (uint32_t k = 0; ok && k < 3; ++k) {
                const uint32_t block = current + 16 + k * e.blockSize;
                ok = *reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(block)) == block + e.blockSize;
            }
            if (!ok) ctx.Fail("slot %u: not a pool of %x-byte blocks on one 2^%u page", e.slot, e.blockSize, log2);
            if (gRegistered.size() != 1 || reinterpret_cast<void *>(gRegistered[0]) != ctx.image.At<void>(e.thunk)) {
                ctx.Fail("slot %u: the image's entry registered %u thunks, not %06x", e.slot, static_cast<unsigned>(gRegistered.size()), e.thunk);
                pools.End();
                continue;
            }
            gRegistered[0]();
            ++ctx.cases;
            std::vector<uint8_t> want = w;
            SetWord(want, kMargin, base + BaseVTable(e.blockSize));
            if (Window(ctx, e.storage) != want) ctx.Fail("slot %u: the thunk wrote more than the base's vtable", e.slot);
            pools.End();
        }
        Put(ctx, e.storage, saved);
    }
}

DIFF_TEST(StaticInitAllocators_Entries, 0x13e810)
{
    AtExitCapture capture(ctx);
    blockpools::Sides pools(ctx);
    size_t n = 0;
    const AllocatorStatic *entries = staticinit::AllocatorStatics(&n);
    std::mt19937 rng(0xA110C);
    for (size_t i = 0; i < n; ++i) {
        const AllocatorStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e.storage);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        const uint32_t log2 = rz::PageLog2(e.blockSize);
        for (int round = 0; round < 3; ++round) {
            std::vector<uint8_t> start(kWindow);
            for (auto &x : start) x = static_cast<uint8_t>(rng());
            SetWord(start, kMargin, WordAt(saved, kMargin));
            Built built[2], destroyed[2];
            size_t registrations[2] = {};
            std::vector<uint32_t> next[2];
            blockpools::ResetArenas();
            blockpools::OsRedirect redirect(ctx);
            for (int side = 0; side < 2; ++side) {
                pools.Begin(side);
                Put(ctx, e.storage, start);
                gRegistered.clear();
                if (side) staticinit::InitAllocator(&e);
                else image();
                built[side] = Capture(ctx, e, side, true);
                registrations[side] = gRegistered.size();
                if (registrations[side] == 1) {
                    gRegistered[0]();
                    destroyed[side] = Capture(ctx, e, side, false);
                    next[side] = pools.Next(side, log2, 4);
                }
                pools.End();
            }
            ++ctx.cases;
            if (registrations[0] != 1 || registrations[1] != 1) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours", e.slot, static_cast<unsigned>(registrations[0]),
                         static_cast<unsigned>(registrations[1]));
                continue;
            }
            if (built[0].code != 1 || built[1].code != 2 || built[0].window != built[1].window || built[0].page != built[1].page)
                ctx.Fail("slot %u round %d: built object differs (vtable codes %d/%d)", e.slot, round, built[0].code, built[1].code);
            const int ourDestroyed = e.vtable != BaseVTable(e.blockSize) ? 4 : 2;
            const int imageDestroyed = e.vtable != BaseVTable(e.blockSize) ? 3 : 1;
            if (destroyed[0].code != imageDestroyed || destroyed[1].code != ourDestroyed || destroyed[0].window != destroyed[1].window)
                ctx.Fail("slot %u round %d: destroyed object differs (vtable codes %d/%d)", e.slot, round, destroyed[0].code,
                         destroyed[1].code);
            if (next[0] != next[1]) ctx.Fail("slot %u round %d: the pools differ after the thunk", e.slot, round);
        }
        Put(ctx, e.storage, saved);
    }
}

DIFF_TEST(StaticInitAllocators_OnImageTable, 0x13a9a0)
{
    AtExitCapture capture(ctx);
    blockpools::Sides pools(ctx);
    size_t ownedCount = 0, n = 0;
    const winmainstart::OwnedInitialiser *owned = staticinit::OwnedAllocators(&ownedCount);
    const AllocatorStatic *entries = staticinit::AllocatorStatics(&n);
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    std::vector<winmainstart::Initialiser> table(slots, nullptr);
    for (size_t i = 0; i < ownedCount; ++i) {
        if (owned[i].slot < slots) table[owned[i].slot] = ctx.image.At<winmainstart::Initialiser>(kXcFirst)[owned[i].slot];
    }
    std::vector<std::vector<uint8_t>> saved;
    for (size_t i = 0; i < n; ++i) saved.push_back(Window(ctx, entries[i].storage));
    auto restore = [&] {
        for (size_t i = 0; i < n; ++i) Put(ctx, entries[i].storage, saved[i]);
    };
    // Every object's 12 bytes (codes and offsets), its current page's bytes.
    auto objects = [&](int side, bool withPages) {
        std::vector<uint32_t> all;
        for (size_t i = 0; i < n; ++i) {
            Built b = Capture(ctx, entries[i], side, withPages);
            all.push_back(static_cast<uint32_t>(b.code));
            for (int k = 0; k < 3; ++k) all.push_back(WordAt(b.window, kMargin + 4 * k));
            all.insert(all.end(), b.page.begin(), b.page.end());
        }
        return all;
    };
    auto lifo = [&] {
        for (auto it = gRegistered.rbegin(); it != gRegistered.rend(); ++it) (*it)();
    };
    auto nextPages = [&](int side) {
        std::vector<uint32_t> all;
        for (uint32_t log2 = 11; log2 <= 15; ++log2) {
            std::vector<uint32_t> v = pools.Next(side, log2, 6);
            all.insert(all.end(), v.begin(), v.end());
        }
        return all;
    };

    blockpools::ResetArenas();
    blockpools::OsRedirect redirect(ctx);
    std::vector<uint32_t> built[2], destroyed[2], after[2];
    size_t registrations[2] = {};
    winmainstart::XcRunCounts c{};
    for (int side = 0; side < 2; ++side) {
        restore();
        pools.Begin(side);
        gRegistered.clear();
        if (side) {
            c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots, ctx.image.Base(), owned, ownedCount);
        } else {
            for (uint32_t s = 0; s < slots; ++s) {
                if (table[s] != nullptr) table[s]();
            }
        }
        registrations[side] = gRegistered.size();
        built[side] = objects(side, true);
        lifo();
        destroyed[side] = objects(side, false);
        after[side] = nextPages(side);
        pools.End();
    }
    restore();

    ++ctx.cases;
    if (c.owned != 50 || c.image || c.mismatched || c.unused)
        ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused", c.owned, c.image, c.mismatched, c.unused);
    if (registrations[0] != 50 || registrations[1] != 50)
        ctx.Fail("registrations: %u by the image's entries, %u by ours (50)", static_cast<unsigned>(registrations[0]),
                 static_cast<unsigned>(registrations[1]));
    // The codes differ by design (1 / 2 built; 1 / 2 or 3 / 4 destroyed): checked, then made equal.
    size_t pos = 0;
    for (size_t i = 0; i < n; ++i) {
        const AllocatorStatic &e = entries[i];
        const size_t words = 4 + PageBytes(e) / 4;
        if (pos < built[0].size() && pos < built[1].size()) {
            if (built[0][pos] != 1 || built[1][pos] != 2) ctx.Fail("slot %u: built vtable codes %u/%u", e.slot, built[0][pos], built[1][pos]);
            built[1][pos] = built[0][pos];
        }
        pos += words;
    }
    for (size_t i = 0; i < n; ++i) {
        const AllocatorStatic &e = entries[i];
        const bool pooled = e.vtable != BaseVTable(e.blockSize);
        uint32_t &a = destroyed[0][4 * i], &b = destroyed[1][4 * i];
        if (a != (pooled ? 3u : 1u) || b != (pooled ? 4u : 2u)) ctx.Fail("slot %u: destroyed vtable codes %u/%u", e.slot, a, b);
        b = a;
    }
    if (built[0] != built[1]) ctx.Fail("the objects differ after the run");
    if (destroyed[0] != destroyed[1]) ctx.Fail("the objects differ after the exit list");
    if (after[0] != after[1]) ctx.Fail("the pools differ after the exit list");
}
