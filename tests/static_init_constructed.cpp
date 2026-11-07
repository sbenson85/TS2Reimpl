// Differential tests for the owned "object" XC entries of lane HD (src/app/StaticInitConstructed.cpp;
// docs/subsystems/startup.md "Done in lane HD"): statics the image builds by calling a constructor on the global.
//
// StaticInitConstructed_Entries: each entry against the image's in its slot, from junk start states (the object and
// 8 bytes on each side; the CRC tables' block for slot 533, with the built flag clear and set; a pool's chunk list of
// 0-6 chunks, since its constructor leaves the list alone): the registration (none, or exactly one, of the thunk our
// table names), the object as built, then the registered thunk run and the object as destroyed, and the blocks each
// side frees. Objects holding heap blocks are compared by content: a name table by its count, capacity and each
// element's text and value; the list-and-lock by its list (size, an empty ring on its head) and its lock (vtable, the
// block's CRITICAL_SECTION fields and count); a pointer to a block the object owned is compared as "that block".
// Slot 212's vector is also destroyed live (blocks of both allocation schemes). The pools: the image's RZFree
// redirected to a logging HeapFree; ours frees into a real allocator, which then hands back exactly the chunks.
//
// StaticInitConstructed_OnImageTable: the runner over a copy of the XC table with only these slots, from the file's
// state: 97 ours, the same registrations, the same objects after the run and after each side's exit list run LIFO.
#include "difftest.h"
#include "free_recorder.h"
#include "game_allocator.h"
#include "app/StaticInit.h"
#include "app/StaticInitConstructed.h"
#include "app/WinMainStart.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using staticinit::ConstructedKind;
using staticinit::ConstructedStatic;

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kAtExit = 0x824cbe, kRZFree = 0x1be3a1;
// StringKeyParser's CRC tables: CRC24 at 0x10f51d0, CRC32 at 0x10f55d0, the built flag 0x10f59d0, the entry's object
// (empty) 0x10f59d1.
constexpr uint32_t kCrcBlock = 0x10f51d0, kCrcFlag = 0x10f59d0, kCrcBlockEnd = 0x10f59d2;
constexpr int kMargin = 8;

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

std::vector<void *> gLoggedFrees;
void __cdecl LoggingFree(void *p)
{
    gLoggedFrees.push_back(p);
    HeapFree(GetProcessHeap(), 0, p);
}

bool SlotHolds(difftest::Context &ctx, uint32_t slot, uint32_t rva)
{
    uint32_t held = *ctx.image.At<uint32_t>(kXcFirst + 4 * slot) - static_cast<uint32_t>(ctx.image.Base());
    if (held == rva) return true;
    ctx.Fail("slot %u holds %08x, our table says %08x", slot, held, rva);
    return false;
}

uint32_t WindowStart(const ConstructedStatic &e) { return (e.kind == ConstructedKind::CrcTables ? kCrcBlock : e.storage) - kMargin; }
size_t ObjectOffset(const ConstructedStatic &e) { return e.storage - WindowStart(e); }
size_t WindowSize(const ConstructedStatic &e)
{
    if (e.kind == ConstructedKind::CrcTables) return kCrcBlockEnd - kCrcBlock + 2 * kMargin;
    return staticinit::ConstructedObjectSize(e) + 2 * kMargin;
}

std::vector<uint8_t> Window(const difftest::Context &ctx, const ConstructedStatic &e)
{
    const uint8_t *p = ctx.image.At<uint8_t>(WindowStart(e));
    return std::vector<uint8_t>(p, p + WindowSize(e));
}

void Put(const difftest::Context &ctx, const ConstructedStatic &e, const std::vector<uint8_t> &w)
{
    std::memcpy(ctx.image.At<uint8_t>(WindowStart(e)), w.data(), w.size());
}

uint32_t WordAt(const std::vector<uint8_t> &w, size_t off)
{
    uint32_t v;
    std::memcpy(&v, w.data() + off, 4);
    return v;
}
void SetWord(std::vector<uint8_t> &w, size_t off, uint32_t v) { std::memcpy(w.data() + off, &v, 4); }
template <typename T> void Append(std::vector<uint8_t> &out, const T &v)
{
    const auto *p = reinterpret_cast<const uint8_t *>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}
uint32_t Addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ---- The object as built and as destroyed, in a form both sides can be compared in ----

// The heap blocks a built object owns (by address), to recognise them after the destructor.
struct Owned {
    uint32_t head = 0, block = 0;
};

// A built name table: the margins, then count, capacity and each element's text (with its terminator), size,
// capacity and value. The element is a std::string (24 bytes) and a dword.
// Each form keeps the window's margins at both ends (the runner test strips them).
std::vector<uint8_t> WithMargins(const std::vector<uint8_t> &w, const std::vector<uint8_t> &middle)
{
    std::vector<uint8_t> out(w.begin(), w.begin() + kMargin);
    out.insert(out.end(), middle.begin(), middle.end());
    out.insert(out.end(), w.end() - kMargin, w.end());
    return out;
}

std::vector<uint8_t> NameTableForm(const std::vector<uint8_t> &w)
{
    std::vector<uint8_t> out;
    const uint32_t first = WordAt(w, kMargin), last = WordAt(w, kMargin + 4), end = WordAt(w, kMargin + 8);
    Append(out, last - first);
    Append(out, end - first);
    if (first == 0 || (last - first) % 28 != 0 || last < first || end < last) return WithMargins(w, out);
    for (uint32_t p = first; p != last; p += 28) {
        const auto *element = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(p));
        uint32_t size, capacity, value;
        std::memcpy(&size, element + 16, 4);
        std::memcpy(&capacity, element + 20, 4);
        std::memcpy(&value, element + 24, 4);
        Append(out, size);
        Append(out, capacity);
        Append(out, value);
        const char *text = capacity > 15 ? *reinterpret_cast<char *const *>(element) : reinterpret_cast<const char *>(element);
        if (size <= capacity && size < 4096) out.insert(out.end(), text, text + size + 1);
    }
    return WithMargins(w, out);
}

// The list-and-lock: the margins and the list's size as they are; the head word and the lock's block word as "the
// block the built object owned" (1) or as they are; while built, the head node's links (an empty ring) and the lock
// block's fields.
std::vector<uint8_t> ListLockForm(const std::vector<uint8_t> &w, const Owned &owned, bool built)
{
    std::vector<uint8_t> out(w.begin() + kMargin, w.end() - kMargin);
    const uint32_t head = WordAt(w, kMargin), block = WordAt(w, kMargin + 12);
    SetWord(out, 0, head == owned.head && head ? 1 : head);
    SetWord(out, 12, block == owned.block && block ? 1 : block);
    if (built && head && block) {
        const auto *node = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(head));
        out.push_back(node[0] == head);
        out.push_back(node[1] == head);
        const auto *cs = reinterpret_cast<const CRITICAL_SECTION *>(static_cast<uintptr_t>(block));
        Append(out, cs->LockCount);
        Append(out, cs->RecursionCount);
        Append(out, cs->OwningThread);
        Append(out, *reinterpret_cast<const int32_t *>(static_cast<uintptr_t>(block) + 0x18));
    }
    return WithMargins(w, out);
}

Owned OwnedBy(const ConstructedStatic &e, const std::vector<uint8_t> &built)
{
    Owned o;
    if (e.kind == ConstructedKind::ListLock) {
        o.head = WordAt(built, kMargin);
        o.block = WordAt(built, kMargin + 12);
    }
    return o;
}

std::vector<uint8_t> BuiltForm(const ConstructedStatic &e, const std::vector<uint8_t> &w)
{
    if (e.kind == ConstructedKind::NameValues) return NameTableForm(w);
    if (e.kind == ConstructedKind::ListLock) return ListLockForm(w, OwnedBy(e, w), true);
    return w;
}

std::vector<uint8_t> DestroyedForm(const ConstructedStatic &e, const std::vector<uint8_t> &w, const Owned &owned)
{
    if (e.kind == ConstructedKind::ListLock) return ListLockForm(w, owned, false);
    return w;
}

// ---- One side's run ----

struct Blob20 {
    uint32_t words[5];
};

struct Start {
    std::vector<uint8_t> window;
    std::vector<void *> chunks; // a pool's chunk list, in order
    uint32_t chunkBytes = 0;
    uint32_t liveElements = 0;  // slot 212: a live vector of this capacity put in before the thunk (0: none)
    uint32_t liveUsed = 0;
};

// round 0: the saved (file) state; then junk, kind by kind.
Start MakeStart(const ConstructedStatic &e, const std::vector<uint8_t> &saved, int round, std::mt19937 &rng, bool imageSide)
{
    Start s;
    s.window = saved;
    if (round > 0) {
        for (auto &b : s.window) b = static_cast<uint8_t>(rng());
    }
    switch (e.kind) {
    case ConstructedKind::CrcTables:
        if (round > 0) s.window[kCrcFlag - WindowStart(e)] = round % 2 ? 0 : static_cast<uint8_t>(1 + rng() % 255);
        break;
    case ConstructedKind::FixedPool: {
        // The constructor leaves the chunk list (+8) alone: a valid one, as the file's (empty) or as a used pool's.
        const uint32_t n = round == 0 ? 0 : rng() % 7;
        const uint32_t bytes = 8 + rng() % 600;
        s.chunkBytes = bytes;
        for (uint32_t i = 0; i < n; ++i) {
            void *chunk = imageSide ? HeapAlloc(GetProcessHeap(), 0, bytes) : RZMemory::Malloc(bytes);
            std::memset(chunk, 0x5c, bytes);
            if (i) std::memcpy(s.chunks.back(), &chunk, 4);
            s.chunks.push_back(chunk);
        }
        if (n) std::memset(s.chunks.back(), 0, 4);
        SetWord(s.window, kMargin + 8, n ? Addr(s.chunks[0]) : 0);
        break;
    }
    case ConstructedKind::Vector20:
        if (round >= 2) {
            s.liveElements = round % 2 ? 1 + rng() % 200 : 4096 / 20 + rng() % 400;
            s.liveUsed = rng() % (s.liveElements + 1);
        }
        break;
    default: break;
    }
    return s;
}

struct Run {
    std::vector<uint8_t> built, destroyed; // in their compared forms
    std::vector<Thunk> registered;
    std::vector<size_t> freedSizes;        // sorted
};

template <typename Init> Run RunSide(const difftest::Context &ctx, const ConstructedStatic &e, const Start &s, Init init)
{
    Run r;
    Put(ctx, e, s.window);
    gRegistered.clear();
    init();
    r.registered = gRegistered;
    const std::vector<uint8_t> built = Window(ctx, e);
    r.built = BuiltForm(e, built);
    if (e.kind == ConstructedKind::FixedPool) // the chunk list the start state put in, relative to this side's chunks
        SetWord(r.built, kMargin + 8, WordAt(r.built, kMargin + 8) ^ (s.chunks.empty() ? 0 : Addr(s.chunks[0])));
    if (r.registered.size() != 1) return r;
    const Owned owned = OwnedBy(e, built);
    const void *live = nullptr;
    if (s.liveElements) {
        auto *block = std::allocator<Blob20>().allocate(s.liveElements);
        std::memset(block, 0x3e, s.liveElements * sizeof(Blob20));
        live = block;
        uint32_t *object = ctx.image.At<uint32_t>(e.storage);
        object[0] = Addr(block);
        object[1] = Addr(block + s.liveUsed);
        object[2] = Addr(block + s.liveElements);
    }
    {
        difftest::FreeRecorder rec;
        r.registered[0]();
        rec.Stop();
        size_t sizes[64];
        const size_t n = rec.SortedSizes(sizes, 64);
        r.freedSizes.assign(sizes, sizes + std::min<size_t>(n, 64));
        if (live && !rec.Freed(s.liveElements * sizeof(Blob20) >= 4096 ? reinterpret_cast<void *const *>(live)[-1] : live))
            r.freedSizes.push_back(SIZE_MAX); // the live block not freed
    }
    r.destroyed = DestroyedForm(e, Window(ctx, e), owned);
    return r;
}

int RoundsOf(ConstructedKind kind)
{
    switch (kind) {
    case ConstructedKind::CrcTables: return 4;
    case ConstructedKind::FixedPool:
    case ConstructedKind::Vector20: return 7;
    default: return 3;
    }
}

} // namespace

// The 97 image initialisers, in slot order (db/initializers.jsonl: the object entries whose constructor is 0xaf6560,
// 0x57c3c0, 0x1b96af, 0x1c7f30, 0x90a915, 0x1c9030, 0x9cbb60, 0xc0a664, 0x945bd6 or 0x1ce3f3), checked against our
// table independently of it.
constexpr uint32_t kImageEntries[97] = {
    0x1382f7, 0x1382e5, 0x1384a5, 0x138850, 0x139790, 0x13a7a0, 0x13a7e0, 0x13c910, 0x13c820, 0x13c870, 0x13c8c0, 0x13c850,
    0x13c8a0, 0x13c8f0, 0x13c7b0, 0x13c560, 0x13c580, 0x13c5a0, 0x13c5c0, 0x13c5e0, 0x13c730, 0x13c700, 0x13c670, 0x13c610,
    0x13c640, 0x13c6d0, 0x13c760, 0x13c6a0, 0x13ca00, 0x13ca30, 0x13c9a0, 0x13c9d0, 0x13c940, 0x13c970, 0x13dfd0, 0x13e1d9,
    0x13e1ef, 0x13e21d, 0x13e235, 0x13e205, 0x13e46b, 0x13ed20, 0x13ed00, 0x13ed60, 0x13ec00, 0x13ec20, 0x13ed40, 0x13ea80,
    0x13ece0, 0x13eca0, 0x13ef00, 0x13ef20, 0x13eae0, 0x13eb80, 0x13ec40, 0x13eda0, 0x13ed80, 0x13edc0, 0x13ea60, 0x13eb20,
    0x13eb00, 0x13ec80, 0x13ec60, 0x13ebc0, 0x13ee20, 0x13eea0, 0x13eec0, 0x13eee0, 0x13ee40, 0x13ee60, 0x13ee80, 0x13eaa0,
    0x13eac0, 0x13ee00, 0x13ede0, 0x13ecc0, 0x13eba0, 0x13eb60, 0x13eb40, 0x13ebe0, 0x13f530, 0x13f60b, 0x13f5f0, 0x13f760,
    0x13f780, 0x13ffd0, 0x140140, 0x1401f0, 0x1401d0, 0x140210, 0x140240, 0x140270, 0x1402e0, 0x140310, 0x140340, 0x140a60,
    0x140a40,
};

DIFF_TEST(StaticInitConstructed_Entries, 0x13a7a0)
{
    AtExitCapture capture(ctx);
    Redirect rzFree(ctx, kRZFree, reinterpret_cast<const void *>(&LoggingFree));
    GeneralAllocator *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;

    size_t n = 0;
    const ConstructedStatic *entries = staticinit::ConstructedStatics(&n);
    if (n != 97) ctx.Fail("%u object entries, expected 97", static_cast<unsigned>(n));
    std::mt19937 rng(0x4d);
    for (size_t i = 0; i < n; ++i) {
        const ConstructedStatic &e = entries[i];
        if (i < 97 && e.imageRva != kImageEntries[i]) ctx.Fail("row %u: entry %06x, expected %06x", static_cast<unsigned>(i), e.imageRva, kImageEntries[i]);
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < RoundsOf(e.kind); ++round) {
            const uint32_t seed = rng();
            std::mt19937 ra(seed), rb(seed);
            Start sa = MakeStart(e, saved, round, ra, true);
            Start sb = MakeStart(e, saved, round, rb, false);
            gLoggedFrees.clear();
            Run a = RunSide(ctx, e, sa, [&] { image(); });
            const std::vector<void *> loggedA = gLoggedFrees;
            gLoggedFrees.clear();
            Run b = RunSide(ctx, e, sb, [&] { staticinit::InitConstructed(&e); });
            ++ctx.cases;
            const size_t want = e.thunk ? 1 : 0;
            if (a.registered.size() != want || (want && reinterpret_cast<void *>(a.registered[0]) != ctx.image.At<void>(e.thunk)))
                ctx.Fail("slot %u: the image's entry registered %u thunks, not %u (%06x)", e.slot,
                         static_cast<unsigned>(a.registered.size()), static_cast<unsigned>(want), e.thunk);
            if (b.registered.size() != want)
                ctx.Fail("slot %u: our entry registered %u thunks, not %u", e.slot, static_cast<unsigned>(b.registered.size()),
                         static_cast<unsigned>(want));
            if (a.built != b.built) {
                size_t k = 0;
                while (k < a.built.size() && k < b.built.size() && a.built[k] == b.built[k]) ++k;
                ctx.Fail("slot %u round %d: the objects differ as built (kind %d, first at byte %u)", e.slot, round,
                         static_cast<int>(e.kind), static_cast<unsigned>(k));
            }
            if (!want || a.registered.size() != 1 || b.registered.size() != 1) continue;
            if (a.destroyed != b.destroyed) ctx.Fail("slot %u round %d: the objects differ after the thunks", e.slot, round);
            if (e.kind == ConstructedKind::FixedPool) {
                // The image's frees: the chunks in list order. Ours: the allocator hands back exactly the chunks.
                if (loggedA != sa.chunks)
                    ctx.Fail("slot %u round %d: the image's thunk freed %u blocks, not the %u chunks in order", e.slot, round,
                             static_cast<unsigned>(loggedA.size()), static_cast<unsigned>(sa.chunks.size()));
                if (!gLoggedFrees.empty()) ctx.Fail("slot %u: our thunk called the image's RZFree", e.slot);
                std::vector<void *> again, made = sb.chunks;
                for (size_t k = 0; k < made.size(); ++k) again.push_back(RZMemory::Malloc(sb.chunkBytes));
                std::sort(again.begin(), again.end());
                std::sort(made.begin(), made.end());
                if (again != made) ctx.Fail("slot %u round %d: our thunk did not free exactly the %u chunks", e.slot, round, static_cast<unsigned>(made.size()));
                for (void *p : again) RZMemory::Free(p);
            } else if (a.freedSizes != b.freedSizes) {
                ctx.Fail("slot %u round %d: %u / %u blocks freed, or their sizes differ", e.slot, round,
                         static_cast<unsigned>(a.freedSizes.size()), static_cast<unsigned>(b.freedSizes.size()));
            }
        }
        Put(ctx, e, saved);
    }
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
}

DIFF_TEST(StaticInitConstructed_OnImageTable, 0x13ed20)
{
    AtExitCapture capture(ctx);
    size_t ownedCount = 0, n = 0;
    const winmainstart::OwnedInitialiser *owned = staticinit::OwnedConstructed(&ownedCount);
    const ConstructedStatic *entries = staticinit::ConstructedStatics(&n);
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    std::vector<winmainstart::Initialiser> table(slots, nullptr);
    for (size_t i = 0; i < ownedCount; ++i) {
        if (owned[i].slot < slots) table[owned[i].slot] = ctx.image.At<winmainstart::Initialiser>(kXcFirst)[owned[i].slot];
    }
    std::vector<std::vector<uint8_t>> saved;
    for (size_t i = 0; i < n; ++i) saved.push_back(Window(ctx, entries[i]));
    auto restore = [&] {
        for (size_t i = 0; i < n; ++i) Put(ctx, entries[i], saved[i]);
    };
    // Each object (the margins left out: neighbouring objects sit in them; the Entries test covers them).
    std::vector<Owned> owns(n);
    auto built = [&] {
        std::vector<std::vector<uint8_t>> all;
        for (size_t i = 0; i < n; ++i) {
            std::vector<uint8_t> w = Window(ctx, entries[i]);
            owns[i] = OwnedBy(entries[i], w);
            std::vector<uint8_t> f = BuiltForm(entries[i], w);
            all.emplace_back(f.begin() + kMargin, f.end() - kMargin);
        }
        return all;
    };
    auto destroyed = [&] {
        std::vector<std::vector<uint8_t>> all;
        for (size_t i = 0; i < n; ++i) {
            std::vector<uint8_t> f = DestroyedForm(entries[i], Window(ctx, entries[i]), owns[i]);
            all.emplace_back(f.begin() + kMargin, f.end() - kMargin);
        }
        return all;
    };
    auto lifo = [&] {
        for (auto it = gRegistered.rbegin(); it != gRegistered.rend(); ++it) (*it)();
    };

    restore();
    gRegistered.clear();
    for (uint32_t s = 0; s < slots; ++s) {
        if (table[s] != nullptr) table[s]();
    }
    const size_t imageRegistrations = gRegistered.size();
    auto builtA = built();
    lifo();
    auto destroyedA = destroyed();

    restore();
    gRegistered.clear();
    winmainstart::XcRunCounts c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots, ctx.image.Base(),
                                                                      owned, ownedCount);
    const size_t ourRegistrations = gRegistered.size();
    auto builtB = built();
    lifo();
    auto destroyedB = destroyed();
    restore();

    size_t withThunk = 0;
    for (size_t i = 0; i < n; ++i) withThunk += entries[i].thunk != 0;
    ++ctx.cases;
    if (c.owned != 97 || c.image || c.mismatched || c.unused)
        ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused", c.owned, c.image, c.mismatched, c.unused);
    if (withThunk != 67 || imageRegistrations != withThunk || ourRegistrations != withThunk)
        ctx.Fail("registrations: %u by the image's entries, %u by ours (%u rows with a thunk, 67)",
                 static_cast<unsigned>(imageRegistrations), static_cast<unsigned>(ourRegistrations), static_cast<unsigned>(withThunk));
    for (size_t i = 0; i < n; ++i) {
        if (builtA[i] != builtB[i]) ctx.Fail("slot %u: the objects differ after the run", entries[i].slot);
        if (destroyedA[i] != destroyedB[i]) ctx.Fail("slot %u: the objects differ after the exit list", entries[i].slot);
    }
}
