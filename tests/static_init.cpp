// Differential tests for the XC entries our build owns (src/app/StaticInit.cpp, docs/subsystems/startup.md
// "Stage 3: the static initialisers") and the host's XC runner that skips them (winmainstart::RunInitialisersOwned).
//
// Key entries: each owned entry against the image's initialiser in its slot, on the same start state: the
// entry's storage and the bytes around it filled with junk, and the CRC tables of StringKeyParser (two 256-dword
// tables and their "built" byte, 0x10f51d0..0x10f59d0) in one of three states: not built (junk, flag 0: the first
// Crc24 builds them), built, or junk with the flag set (both sides then hash with the junk tables). Compared: the
// storage window and the whole table block. Nothing else is written: the image's initialiser is
// `global = Crc24(name, 1) | 0xff000000` (or the pair form, two stores), and Crc24 / Crc32 write only the table
// block (tests/string_key_parser.cpp compares their state). Also checks that each entry's slot in the image's XC
// table holds the initialiser our table names, and that the runner, given a copy of the XC table with only the
// owned slots kept, runs ours in exactly those slots.
//
// Runner: against the image's _initterm 0x843514 (library code, the loop the image's CRT runs the XC table with)
// over tables of recorders with null holes, with no owned entries; then with owned entries (matching, mismatched,
// out of range, listed twice), against the expected sequence.
#include "difftest.h"
#include "app/StaticInit.h"
#include "app/WinMainStart.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kCrcBlock = 0x10f51d0, kCrcBlockSize = 0x801; // two tables, then the "built" byte
constexpr uint32_t kCrcReadyOffset = 0x800;
constexpr uint32_t kInitterm = 0x843514;
constexpr int kWindowBefore = 8; // junk-filled bytes on each side of an entry's storage

struct CrcBlock {
    std::vector<uint8_t> bytes;
};

CrcBlock CaptureCrc(const difftest::Context &ctx)
{
    const uint8_t *p = ctx.image.At<uint8_t>(kCrcBlock);
    return {std::vector<uint8_t>(p, p + kCrcBlockSize)};
}

void RestoreCrc(const difftest::Context &ctx, const CrcBlock &b)
{
    std::memcpy(ctx.image.At<uint8_t>(kCrcBlock), b.bytes.data(), b.bytes.size());
}

// The three table states (see the top of the file).
CrcBlock CrcState(const difftest::Context &ctx, std::mt19937 &rng, int which, const CrcBlock &built)
{
    if (which == 1) return built;
    CrcBlock b{std::vector<uint8_t>(kCrcBlockSize)};
    for (auto &x : b.bytes) x = static_cast<uint8_t>(rng());
    b.bytes[kCrcReadyOffset] = which == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 255);
    (void)ctx;
    return b;
}

std::vector<uint8_t> Window(const difftest::Context &ctx, uint32_t storage, uint32_t size)
{
    const uint8_t *p = ctx.image.At<uint8_t>(storage - kWindowBefore);
    return std::vector<uint8_t>(p, p + size + 2 * kWindowBefore);
}

void FillWindow(const difftest::Context &ctx, uint32_t storage, uint32_t size, std::mt19937 &rng,
                std::vector<uint8_t> &out)
{
    out.resize(size + 2 * kWindowBefore);
    for (auto &x : out) x = static_cast<uint8_t>(rng());
    std::memcpy(ctx.image.At<uint8_t>(storage - kWindowBefore), out.data(), out.size());
}

void RestoreWindow(const difftest::Context &ctx, uint32_t storage, const std::vector<uint8_t> &w)
{
    std::memcpy(ctx.image.At<uint8_t>(storage - kWindowBefore), w.data(), w.size());
}

// A CRC block with the tables built, by the image's own CrcTables::Initialize 0x9cbb60 on a cleared flag.
CrcBlock BuiltCrc(const difftest::Context &ctx)
{
    *ctx.image.At<uint8_t>(kCrcBlock + kCrcReadyOffset) = 0;
    ctx.Original<void *(__thiscall *)(void *)>(0x9cbb60)(nullptr);
    return CaptureCrc(ctx);
}

uint32_t SlotValue(const difftest::Context &ctx, uint32_t slot)
{
    return *ctx.image.At<uint32_t>(kXcFirst + 4 * slot);
}

// One owned entry against the image's: three table states x a few junk fills.
template <typename Entry>
void CompareEntry(difftest::Context &ctx, const Entry &e, void(__cdecl *ours)(const void *), uint32_t size,
                  const CrcBlock &built, std::mt19937 &rng)
{
    if (SlotValue(ctx, e.slot) - static_cast<uint32_t>(ctx.image.Base()) != e.imageRva) {
        ctx.Fail("slot %u holds %08x, our table says %08x", e.slot,
                 SlotValue(ctx, e.slot) - static_cast<uint32_t>(ctx.image.Base()), e.imageRva);
        return;
    }
    auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
    const std::vector<uint8_t> saved = Window(ctx, e.storage, size);
    for (int state = 0; state < 3; ++state) {
        for (int fill = 0; fill < 3; ++fill) {
            CrcBlock start = CrcState(ctx, rng, state, built);
            std::vector<uint8_t> junk;
            FillWindow(ctx, e.storage, size, rng, junk);
            RestoreCrc(ctx, start);
            image();
            std::vector<uint8_t> wa = Window(ctx, e.storage, size);
            CrcBlock ca = CaptureCrc(ctx);

            RestoreWindow(ctx, e.storage, junk);
            RestoreCrc(ctx, start);
            ours(&e);
            std::vector<uint8_t> wb = Window(ctx, e.storage, size);
            CrcBlock cb = CaptureCrc(ctx);
            ++ctx.cases;
            if (wa != wb) {
                uint32_t a0, b0;
                std::memcpy(&a0, wa.data() + kWindowBefore, 4);
                std::memcpy(&b0, wb.data() + kWindowBefore, 4);
                ctx.Fail("slot %u '%s' (tables %d): storage differs, first word image %08x ours %08x", e.slot, e.name,
                         state, a0, b0);
            }
            if (ca.bytes != cb.bytes) ctx.Fail("slot %u '%s' (tables %d): CRC tables differ", e.slot, e.name, state);
        }
    }
    RestoreWindow(ctx, e.storage, saved);
}

} // namespace

DIFF_TEST(StaticInit_NameKeys, 0x13d9ab)
{
    CrcBlock before = CaptureCrc(ctx);
    CrcBlock built = BuiltCrc(ctx);
    std::mt19937 rng(384);
    size_t n = 0;
    const staticinit::NameKey *keys = staticinit::NameKeys(&n);
    if (n != 81) ctx.Fail("%u name keys, expected 81 (slots 384-464)", static_cast<unsigned>(n));
    for (size_t i = 0; i < n; ++i) CompareEntry(ctx, keys[i], &staticinit::InitNameKey, 4, built, rng);
    RestoreCrc(ctx, before);
}

DIFF_TEST(StaticInit_NameKeyPairs, 0x13852c)
{
    CrcBlock before = CaptureCrc(ctx);
    CrcBlock built = BuiltCrc(ctx);
    std::mt19937 rng(115);
    size_t n = 0;
    const staticinit::NameKeyPair *keys = staticinit::NameKeyPairs(&n);
    if (n != 30) ctx.Fail("%u name key pairs, expected 30", static_cast<unsigned>(n));
    for (size_t i = 0; i < n; ++i) CompareEntry(ctx, keys[i], &staticinit::InitNameKeyPair, 8, built, rng);
    RestoreCrc(ctx, before);
}

// The skip list on the image's real XC table: a copy with only the owned slots kept runs ours in each of them,
// and leaves the same globals as the image's initialisers run in slot order.
DIFF_TEST(StaticInit_SkipListOnImageTable, 0x13d9ab)
{
    CrcBlock before = CaptureCrc(ctx);
    size_t ownedCount = 0;
    const winmainstart::OwnedInitialiser *owned = staticinit::OwnedNameKeys(&ownedCount); // the objects: tests/static_init_objects.cpp
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    std::vector<winmainstart::Initialiser> table(slots, nullptr);
    for (size_t i = 0; i < ownedCount; ++i) {
        if (owned[i].slot < slots) table[owned[i].slot] = ctx.image.At<winmainstart::Initialiser>(kXcFirst)[owned[i].slot];
    }
    // Every owned entry's storage: a window from the lowest to the highest key global, per region.
    struct Region { uint32_t from, to; };
    const Region regions[] = {{0x10cc420, 0x10cc6f8}, {0x10dc6e8, 0x10dc7c0}, {0x10de030, 0x10de060},
                              {0x10de518, 0x10de668}, {0x10dea98, 0x10deac8}};
    std::vector<std::vector<uint8_t>> saved;
    for (const Region &r : regions) {
        const uint8_t *p = ctx.image.At<uint8_t>(r.from);
        saved.emplace_back(p, p + (r.to - r.from));
    }
    std::mt19937 rng(7);
    for (int round = 0; round < 4; ++round) {
        std::vector<std::vector<uint8_t>> junk;
        for (const Region &r : regions) {
            std::vector<uint8_t> j(r.to - r.from);
            for (auto &x : j) x = static_cast<uint8_t>(rng());
            junk.push_back(j);
        }
        CrcBlock start = round == 0 ? BuiltCrc(ctx) : CrcState(ctx, rng, round % 2 == 0 ? 2 : 0, CrcBlock{});
        auto load = [&] {
            for (size_t i = 0; i < std::size(regions); ++i) std::memcpy(ctx.image.At<uint8_t>(regions[i].from), junk[i].data(), junk[i].size());
            RestoreCrc(ctx, start);
        };
        auto capture = [&] {
            std::vector<uint8_t> all;
            for (const Region &r : regions) {
                const uint8_t *p = ctx.image.At<uint8_t>(r.from);
                all.insert(all.end(), p, p + (r.to - r.from));
            }
            CrcBlock c = CaptureCrc(ctx);
            all.insert(all.end(), c.bytes.begin(), c.bytes.end());
            return all;
        };
        load();
        for (uint32_t s = 0; s < slots; ++s) {
            if (table[s] != nullptr) table[s]();
        }
        std::vector<uint8_t> a = capture();
        load();
        winmainstart::XcRunCounts c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots,
                                                                          ctx.image.Base(), owned, ownedCount);
        std::vector<uint8_t> b = capture();
        ++ctx.cases;
        if (c.owned != static_cast<int>(ownedCount) || c.image != 0 || c.mismatched != 0 || c.unused != 0)
            ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused (owned %u)", c.owned, c.image, c.mismatched,
                     c.unused, static_cast<unsigned>(ownedCount));
        if (a != b) ctx.Fail("round %d: globals or CRC tables differ after the owned slots", round);
    }
    if (ownedCount != 111) ctx.Fail("%u owned entries, expected 111 (the key-hash kind)", static_cast<unsigned>(ownedCount));
    for (size_t i = 0; i < std::size(regions); ++i) std::memcpy(ctx.image.At<uint8_t>(regions[i].from), saved[i].data(), saved[i].size());
    RestoreCrc(ctx, before);
}

// ---- the runner against the image's _initterm ----

namespace {

std::vector<int> gCalls;
template <int N> void __cdecl Rec() { gCalls.push_back(N); }
template <int N> void __cdecl OwnedRec(const void *arg) { gCalls.push_back(1000 + N + static_cast<int>(reinterpret_cast<uintptr_t>(arg))); }

using Init = winmainstart::Initialiser;
constexpr Init kRecs[] = {&Rec<0>, &Rec<1>, &Rec<2>, &Rec<3>, &Rec<4>, &Rec<5>, &Rec<6>, &Rec<7>,
                          &Rec<8>, &Rec<9>, &Rec<10>, &Rec<11>, &Rec<12>, &Rec<13>, &Rec<14>, &Rec<15>};

} // namespace

DIFF_TEST(StaticInit_Runner, 0x843514)
{
    auto initterm = ctx.Original<void(__cdecl *)(Init *, Init *)>(kInitterm);
    std::mt19937 rng(800);
    for (int round = 0; round < 400; ++round) {
        size_t n = rng() % 17;
        std::vector<Init> table(n);
        for (auto &t : table) t = rng() % 4 == 0 ? nullptr : kRecs[rng() % 16];

        // No owned entries: the same calls as the image's _initterm.
        gCalls.clear();
        initterm(table.data(), table.data() + n);
        std::vector<int> a = gCalls;
        gCalls.clear();
        winmainstart::XcRunCounts c = winmainstart::RunInitialisersOwned(table.data(), table.data() + n, 0, nullptr, 0);
        std::vector<int> b = gCalls;
        ++ctx.cases;
        if (a != b) ctx.Fail("round %d: %u calls by _initterm, %u by ours", round, static_cast<unsigned>(a.size()),
                             static_cast<unsigned>(b.size()));
        int nonNull = 0;
        for (Init t : table) nonNull += t != nullptr;
        if (c.image != nonNull || c.owned || c.mismatched || c.unused) ctx.Fail("round %d: counts without owned entries", round);

        // Owned entries: some slots matching ("base" 0x1000: the slot holds 0x1000 + imageRva), some holding another
        // initialiser, one out of range, one listed twice.
        const uintptr_t base = 0x1000;
        std::vector<winmainstart::OwnedInitialiser> owned;
        std::vector<int> expect;
        int wantOwned = 0, wantImage = 0, wantMismatch = 0, wantUnused = 0;
        std::vector<int> ownedAt(n, -1);
        for (size_t s = 0; s < n; ++s) {
            if (rng() % 3 != 0) continue;
            bool match = rng() % 4 != 0;
            uint32_t rva = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table[s]) - base);
            if (!match || table[s] == nullptr) rva ^= 0x10;
            ownedAt[s] = static_cast<int>(owned.size());
            owned.push_back({static_cast<uint32_t>(s), rva, &OwnedRec<0>, reinterpret_cast<const void *>(s)});
        }
        if (!owned.empty() && rng() % 2) {
            owned.push_back(owned[rng() % owned.size()]);
            owned.back().run = &OwnedRec<500>;
            ++wantUnused;
        }
        if (rng() % 2) {
            owned.push_back({static_cast<uint32_t>(n + rng() % 3), 0, &OwnedRec<700>, nullptr});
            ++wantUnused;
        }
        std::shuffle(owned.begin(), owned.end(), rng);
        for (size_t s = 0; s < n; ++s) {
            if (table[s] == nullptr) continue;
            const winmainstart::OwnedInitialiser *o = nullptr;
            for (const auto &x : owned) {
                if (x.slot == s && o == nullptr) o = &x;
            }
            if (o && reinterpret_cast<uintptr_t>(table[s]) == base + o->imageRva) {
                expect.push_back(1000 + (o->run == &OwnedRec<500> ? 500 : 0) + static_cast<int>(s));
                ++wantOwned;
            } else {
                if (o) ++wantMismatch;
                for (int k = 0; k < 16; ++k) {
                    if (kRecs[k] == table[s]) expect.push_back(k);
                }
                ++wantImage;
            }
        }
        gCalls.clear();
        c = winmainstart::RunInitialisersOwned(table.data(), table.data() + n, base, owned.data(), owned.size());
        ++ctx.cases;
        if (gCalls != expect) ctx.Fail("round %d: owned run made %u calls, expected %u", round,
                                      static_cast<unsigned>(gCalls.size()), static_cast<unsigned>(expect.size()));
        if (c.owned != wantOwned || c.image != wantImage || c.mismatched != wantMismatch || c.unused != wantUnused)
            ctx.Fail("round %d: counts %d/%d/%d/%d, expected %d/%d/%d/%d", round, c.owned, c.image, c.mismatched,
                     c.unused, wantOwned, wantImage, wantMismatch, wantUnused);
    }
}
