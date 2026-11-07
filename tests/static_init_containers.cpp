// Differential tests for lane HF's owned XC entries (src/app/StaticInitContainers.cpp; docs/subsystems/startup.md
// "Done in lane HF"): empty containers, two arrays, atexit-only vectors, a name table, the computed floats, the CPU
// feature flags and the cRZTimer section.
//
// StaticInitHF_Entries: each entry against the image's in its slot, on the object and 8 bytes on each side filled with
// two junk patterns: the window after the entry (heap pointers compared by what they point at: the block's size, a
// head node's links and flags, a bucket array's entries, a vector's spans and contents), the registrations (count and
// that ours is not the image's thunk); then the registered thunk, on the built object and on live ones (elements
// inserted, long strings assigned, vectors filled, identically on both sides): the blocks freed (count, sizes, and for
// the arrays the order by element), the window after (dangling words masked).
//
// StaticInitHF_Empty: slots 106 and 107 are `push ebp; mov ebp, esp; pop ebp; ret`, ours does nothing.
// StaticInitHF_CpuFeatures: 0xee0d1 against CpuFeatureFlags with the image's GetVersionExA import bound to fakes (the
// platform and minor version varied) and to the real one; and the entries.
// StaticInitHF_TimerSection: slot 104 with the image's kernel32 / user32 imports bound to the real functions: a fresh
// mapping (named cRZTimer<pid>, 8 bytes, its view zero), an existing one (junk in the view kept), a name taken by
// another kind of object (no mapping: both words 0, no view); the thunk unmaps and closes.
// StaticInitHF_OnImageTable: the runner over a copy of the XC table with only these slots: counts, registrations, and
// after each side's exit list run LIFO the same frees and the same values.
#include "difftest.h"
#include "free_recorder.h"
#include "app/StaticInit.h"
#include "app/StaticInitContainers.h"
#include "app/WinMainStart.h"
#include "rz/cRZString.h"
#include "std/StdInstances.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using staticinit::HFKind;
using staticinit::HFStatic;

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kAtExit = 0x824cbe;
constexpr uint32_t kCpuFeatures = 0xee0d1;
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

// One of the image's IAT entries bound to a function of ours for a test (the harness traps every import).
struct BindImport {
    void **entry;
    void *saved;
    BindImport(const difftest::Context &ctx, uint32_t rva, const void *target) : entry(ctx.image.At<void *>(rva)), saved(*entry)
    {
        DWORD old;
        VirtualProtect(entry, 4, PAGE_READWRITE, &old);
        *entry = const_cast<void *>(target);
    }
    ~BindImport() { *entry = saved; }
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

const HFStatic *Entries(size_t *n) { return staticinit::HFStatics(n); }

size_t WindowSize(const HFStatic &e) { return staticinit::HFObjectSize(e) + 2 * kMargin; }

std::vector<uint8_t> Window(const difftest::Context &ctx, const HFStatic &e)
{
    const uint8_t *p = ctx.image.At<uint8_t>(e.storage - kMargin);
    return std::vector<uint8_t>(p, p + WindowSize(e));
}

void Put(const difftest::Context &ctx, const HFStatic &e, const std::vector<uint8_t> &w)
{
    std::memcpy(ctx.image.At<uint8_t>(e.storage - kMargin), w.data(), w.size());
}

uint32_t Word(const std::vector<uint8_t> &w, size_t off)
{
    uint32_t v;
    std::memcpy(&v, w.data() + off, 4);
    return v;
}

size_t BlockSize(uint32_t p) { return HeapSize(GetProcessHeap(), 0, reinterpret_cast<void *>(static_cast<uintptr_t>(p))); }

template <typename T> T *At(const difftest::Context &ctx, uint32_t rva) { return ctx.image.At<T>(rva); }

// ---- The window after an entry, heap pointers replaced by what they point at ----

struct Shot {
    std::vector<uint8_t> bytes;    // the window, pointer words zeroed
    std::vector<uint32_t> heap;    // per pointer: block size, links, contents
};

bool IsTree(HFKind k)
{
    return k == HFKind::IntSet || k == HFKind::IntIntMap || k == HFKind::FloatMap12 || k == HFKind::FloatMap4 ||
           k == HFKind::NamedSet44 || k == HFKind::KeyedNameSet36;
}
bool IsHash(HFKind k) { return k == HFKind::NamedWordHash36 || k == HFKind::NamedRefHash28; }

void MaskWord(Shot &s, size_t objectOffset) { std::memset(s.bytes.data() + kMargin + objectOffset, 0, 4); }

// A tree's head node: its block size, left / parent / right pointing at itself, color and isnil.
void DescribeHead(Shot &s, uint32_t head)
{
    const uint32_t *n = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(head));
    s.heap.push_back(static_cast<uint32_t>(BlockSize(head)));
    for (int i = 0; i < 3; ++i) s.heap.push_back(n[i] == head);
    s.heap.push_back(reinterpret_cast<const uint16_t *>(n)[6]);
}

// A vector's three pointers: the spans and the block (elements compared by their bytes, `elementBytes` each).
void DescribeVector(Shot &s, const std::vector<uint8_t> &w, size_t off, size_t elementBytes)
{
    const uint32_t first = Word(w, kMargin + off), last = Word(w, kMargin + off + 4), end = Word(w, kMargin + off + 8);
    for (size_t i = 0; i < 3; ++i) MaskWord(s, off + 4 * i);
    s.heap.push_back(first != 0);
    s.heap.push_back(last - first);
    s.heap.push_back(end - first);
    if (!first) return;
    s.heap.push_back(static_cast<uint32_t>(BlockSize(first)));
    const uint32_t *p = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(first));
    for (size_t i = 0; i < (last - first) / 4; ++i) s.heap.push_back(p[i]);
    (void)elementBytes;
}

Shot Take(const difftest::Context &ctx, const HFStatic &e)
{
    Shot s;
    const std::vector<uint8_t> w = Window(ctx, e);
    s.bytes = w;
    if (IsTree(e.kind)) {
        MaskWord(s, 0);
        DescribeHead(s, Word(w, kMargin));
    } else if (IsHash(e.kind)) {
        // {max load, list {head, count}, buckets {first, last, end}, mask, max index}
        const uint32_t head = Word(w, kMargin + 4);
        MaskWord(s, 4);
        const uint32_t *n = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(head));
        s.heap.push_back(static_cast<uint32_t>(BlockSize(head)));
        s.heap.push_back(n[0] == head);
        s.heap.push_back(n[1] == head);
        const uint32_t first = Word(w, kMargin + 0xc), last = Word(w, kMargin + 0x10), end = Word(w, kMargin + 0x14);
        for (size_t off = 0xc; off <= 0x14; off += 4) MaskWord(s, off);
        s.heap.push_back(last - first);
        s.heap.push_back(end - first);
        s.heap.push_back(static_cast<uint32_t>(BlockSize(first)));
        for (uint32_t p = first; p != last; p += 4) s.heap.push_back(*reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(p)) == head);
    } else if (e.kind == HFKind::NameTable) {
        DescribeVector(s, w, 0, 0x1c);
    }
    return s;
}

bool operator==(const Shot &a, const Shot &b) { return a.bytes == b.bytes && a.heap == b.heap; }

// ---- Live objects: the same elements on both sides, through our STL on the built object ----

// Fake referenced objects for slot 729's elements: a call through any of their first three slots is logged.
struct Fake {
    const void *vtable;
};
Fake gFakes[64];
std::vector<int> gCalls; // slot * 100 + the fake's index
uint32_t __fastcall FakeSlot0(Fake *self, void *, uint32_t) { gCalls.push_back(static_cast<int>(self - gFakes)); return 0; }
uint32_t __fastcall FakeSlot1(Fake *self, void *) { gCalls.push_back(100 + static_cast<int>(self - gFakes)); return 2; }
uint32_t __fastcall FakeSlot2(Fake *self, void *) { gCalls.push_back(200 + static_cast<int>(self - gFakes)); return 1; }
const void *const kFakeVTable[3] = {reinterpret_cast<const void *>(&FakeSlot0), reinterpret_cast<const void *>(&FakeSlot1),
                                    reinterpret_cast<const void *>(&FakeSlot2)};
struct FakeInit {
    FakeInit()
    {
        for (Fake &f : gFakes) f.vtable = kFakeVTable;
        gCalls.reserve(256); // no reallocation (a free) while a thunk runs
    }
} gFakeInit;

void Populate(const difftest::Context &ctx, const HFStatic &e, int round, std::vector<void *> &extra)
{
    const int n = round == 0 ? 0 : round == 1 ? 1 : round == 2 ? 6 : 40;
    using namespace staticinit;
    auto blob = [](int i, auto tag) {
        decltype(tag) b{};
        for (auto &x : b.words) x = static_cast<uint32_t>(i * 7919 + 1);
        return b;
    };
    switch (e.kind) {
    case HFKind::IntSet: for (int i = 0; i < n; ++i) At<IntSet>(ctx, e.storage)->insert(i * 37 % 101); break;
    case HFKind::IntIntMap: for (int i = 0; i < n; ++i) (*At<IntIntMap>(ctx, e.storage))[i * 37 % 101] = i; break;
    case HFKind::FloatMap12: for (int i = 0; i < n; ++i) (*At<FloatMap12>(ctx, e.storage))[i * 0.5f] = blob(i, Blob<12>{}); break;
    case HFKind::FloatMap4: for (int i = 0; i < n; ++i) (*At<FloatMap4>(ctx, e.storage))[-i * 0.25f] = blob(i, Blob<4>{}); break;
    // Long texts on some elements: the teardown frees their buffers.
    case HFKind::NamedSet44:
        for (int i = 0; i < n; ++i) At<NamedSet44>(ctx, e.storage)->emplace(std::string(i % 2 ? 5 : 20 + i, 's').c_str(), i * 37 % 101);
        break;
    case HFKind::KeyedNameSet36:
        for (int i = 0; i < n; ++i) At<KeyedNameSet36>(ctx, e.storage)->emplace(std::string(i % 3 ? 7 : 30 + i, 'k').c_str(), i * 37 % 101);
        break;
    case HFKind::NamedWordHash36:
        for (int i = 0; i < n; ++i) At<NamedWordHash36>(ctx, e.storage)->emplace(std::string(i % 2 ? 5 : 20 + i, 'n').c_str(), i * 13 + 1);
        break;
    case HFKind::NamedRefHash28:
        // Distinct names (long from the 16th); a fake object on every other one, whose Release is logged.
        for (int i = 0; i < n; ++i) {
            std::string name = std::to_string(i);
            if (i >= 16) name.append(20, 'q');
            At<NamedRefHash28>(ctx, e.storage)->emplace(name.c_str(), i % 2 ? &gFakes[i] : nullptr);
        }
        break;
    case HFKind::Vector16:
        if (n) ::new (At<void>(ctx, e.storage)) std::vector<Blob<16>>(n * 300); // above 4096 bytes from n = 1
        break;
    case HFKind::Vectors84And564:
        if (n) {
            ::new (At<void>(ctx, e.storage)) std::vector<Blob<84>>(n);
            ::new (At<void>(ctx, e.storage + 0xc)) std::vector<Blob<564>>(n + 1);
        }
        break;
    case HFKind::NameTable:
        if (n) {
            // A long name: the destructor frees it with the buffer.
            At<NameValueTable>(ctx, e.storage)->mEntries[0].first.assign(40 + n, 'x');
        }
        break;
    case HFKind::StringPairs: {
        auto *p = At<std::pair<const std::string, std::string>>(ctx, e.storage);
        for (uint32_t i = 0; i < e.count; ++i) {
            if (n >= 1 && i % 2 == 0) const_cast<std::string &>(p[i].first).assign(16 + i + n, 'a');
            if (n >= 6) p[i].second.assign(20 + 3 * i + n, 'b');
        }
        break;
    }
    case HFKind::RZStrings: {
        auto *s = At<cRZString>(ctx, e.storage);
        for (uint32_t i = 0; i < e.count; ++i) {
            if (n >= 1 && (i % 3 == 0 || n >= 6)) reinterpret_cast<std::string *>(reinterpret_cast<uint8_t *>(&s[i]) + 4)->assign(16 + i + n, 'r');
        }
        break;
    }
    default: break;
    }
}

// The order blocks were freed in, as indices into the elements' blocks taken before (-1: another block).
std::vector<int> FreeOrder(const difftest::FreeRecorder &rec, const std::vector<const void *> &blocks)
{
    std::vector<int> order;
    for (size_t i = 0; i < rec.Count(); ++i) {
        auto it = std::find(blocks.begin(), blocks.end(), rec.Pointer(i));
        order.push_back(it == blocks.end() ? -1 : static_cast<int>(it - blocks.begin()));
    }
    return order;
}

// The heap blocks of an array's elements, in element order (first, second / the one string), null when inline.
std::vector<const void *> ElementBlocks(const difftest::Context &ctx, const HFStatic &e)
{
    std::vector<const void *> out;
    auto block = [](const std::string &s) -> const void * { return s.capacity() > 15 ? s.data() : nullptr; };
    if (e.kind == HFKind::StringPairs) {
        auto *p = At<std::pair<const std::string, std::string>>(ctx, e.storage);
        for (uint32_t i = 0; i < e.count; ++i) {
            out.push_back(block(p[i].first));
            out.push_back(block(p[i].second));
        }
    } else if (e.kind == HFKind::RZStrings) {
        for (uint32_t i = 0; i < e.count; ++i)
            out.push_back(block(*reinterpret_cast<const std::string *>(At<uint8_t>(ctx, e.storage + 0x20 * i + 4))));
    }
    return out;
}

struct Run {
    Shot built;
    size_t registrations = 0;
    bool imageThunk = false;
    std::vector<size_t> freed;   // the blocks' sizes, in the order freed (both sides hold the same elements in the same structure)
    std::vector<int> order;      // arrays: the element blocks in the order freed
    std::vector<uint8_t> destroyed;
    std::vector<int> calls;     // into the fake objects, in order
};

bool IsComputed(HFKind k) { return k == HFKind::Tangent || k == HFKind::ReciprocalLog || k == HFKind::CpuFeatures || k == HFKind::GuidText; }

} // namespace

DIFF_TEST(StaticInitHF_Entries, 0x13e293)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const HFStatic *entries = Entries(&n);
    if (n != 36) ctx.Fail("%u entries, expected 36", static_cast<unsigned>(n));
    std::mt19937 rng(0x4846);
    for (size_t i = 0; i < n; ++i) {
        const HFStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        if (e.kind == HFKind::Empty || e.kind == HFKind::TimerSection || e.kind == HFKind::CpuFeatures) continue; // own tests
        ctx.Mark("HF entry");
        if (e.kind == HFKind::GuidText && std::strcmp(e.text, ctx.image.At<const char>(e.textRva)) != 0)
            ctx.Fail("slot %u: our text is not the image's at %08x", e.slot, e.textRva);
        const std::vector<uint8_t> saved = Window(ctx, e);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        const uint32_t imageThunk = e.thunk ? static_cast<uint32_t>(ctx.image.Base()) + e.thunk : 0;
        // Rounds 0-1: junk everywhere (the build compared); 2-5: the file's object (zero) in junk margins, then live.
        for (int round = 0; round < 6; ++round) {
            std::vector<uint8_t> start(WindowSize(e));
            for (auto &b : start) b = static_cast<uint8_t>(rng());
            const bool live = round >= 2;
            if (live) std::memset(start.data() + kMargin, 0, staticinit::HFObjectSize(e));
            std::vector<void *> extraA, extraB;
            auto run = [&](auto init, std::vector<void *> &extra) {
                Run r;
                Put(ctx, e, start);
                gRegistered.clear();
                init();
                r.built = Take(ctx, e);
                r.registrations = gRegistered.size();
                if (r.registrations != 1) return r;
                r.imageThunk = reinterpret_cast<uint32_t>(gRegistered[0]) == imageThunk;
                // The atexit-only vectors: their entry writes nothing, so on junk there is no object to destroy.
                if (!live && (e.kind == HFKind::Vector16 || e.kind == HFKind::Vectors84And564)) return r;
                if (live) Populate(ctx, e, round - 2, extra);
                const std::vector<const void *> blocks = ElementBlocks(ctx, e);
                {
                    difftest::FreeRecorder rec;
                    gCalls.clear();
                    gRegistered[0]();
                    r.calls = gCalls;
                    rec.Stop();
                    for (size_t k = 0; k < rec.Count(); ++k) r.freed.push_back(rec.Size(k));
                    r.order = FreeOrder(rec, blocks);
                }
                r.destroyed = Window(ctx, e);
                // Dangling words (a freed head / buffer the destructor leaves in place) differ between the sides.
                if (IsTree(e.kind)) std::memset(r.destroyed.data() + kMargin, 0, 4);
                if (IsHash(e.kind)) std::memset(r.destroyed.data() + kMargin + 4, 0, 4);
                // A freed string's union keeps bytes 1-3 of its old pointer (the destructor zeroes only the first
                // character: _Tidy_deallocate), and the pointers differ between the sides.
                if (e.kind == HFKind::StringPairs) {
                    for (uint32_t k = 0; k < 2 * e.count; ++k) std::memset(r.destroyed.data() + kMargin + 0x18 * k + 1, 0, 3);
                }
                if (e.kind == HFKind::RZStrings) {
                    for (uint32_t k = 0; k < e.count; ++k) std::memset(r.destroyed.data() + kMargin + 0x20 * k + 5, 0, 3);
                }
                return r;
            };
            Run a = run([&] { image(); }, extraA);
            Run b = run([&] { staticinit::InitHF(&e); }, extraB);
            ++ctx.cases;
            const size_t expected = e.thunk ? 1 : 0;
            if (a.registrations != expected || b.registrations != expected) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours (%u)", e.slot,
                         static_cast<unsigned>(a.registrations), static_cast<unsigned>(b.registrations), static_cast<unsigned>(expected));
            } else if (expected && (!a.imageThunk || b.imageThunk)) {
                ctx.Fail("slot %u: the image's entry registered %s, ours %s", e.slot, a.imageThunk ? "its thunk" : "another",
                         b.imageThunk ? "the image's thunk" : "its own");
            }
            if (!(a.built == b.built)) ctx.Fail("slot %u round %d: the built object differs", e.slot, round);
            if (a.freed != b.freed) ctx.Fail("slot %u round %d: the destructors free %u / %u blocks", e.slot, round,
                                             static_cast<unsigned>(a.freed.size()), static_cast<unsigned>(b.freed.size()));
            if (a.order != b.order) ctx.Fail("slot %u round %d: the elements are freed in another order", e.slot, round);
            if (a.calls != b.calls) ctx.Fail("slot %u round %d: %u / %u calls into the referenced objects", e.slot, round, static_cast<unsigned>(a.calls.size()), static_cast<unsigned>(b.calls.size()));
            if (e.kind == HFKind::NamedRefHash28 && round == 5 && a.calls.size() != 20) ctx.Fail("slot %u: %u releases (20)", e.slot, static_cast<unsigned>(a.calls.size()));
            if (a.destroyed != b.destroyed) ctx.Fail("slot %u round %d: the destroyed object differs", e.slot, round);
            if (expected && live && round >= 3 && a.freed.empty())
                ctx.Fail("slot %u round %d: a live object freed %u blocks", e.slot, round, static_cast<unsigned>(a.freed.size()));
            if (e.kind == HFKind::NameTable && live && round >= 3 && a.freed.size() != 2)
                ctx.Fail("slot %u round %d: %u blocks freed (the buffer and the long name)", e.slot, round, static_cast<unsigned>(a.freed.size()));
            if (IsComputed(e.kind) && round == 1) break; // no destructor: two junk rounds are all there is
        }
        Put(ctx, e, saved);
    }
}

DIFF_TEST(StaticInitHF_Empty, 0x138440)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const HFStatic *entries = Entries(&n);
    const uint8_t body[] = {0x55, 0x8b, 0xec, 0x5d, 0xc3};
    int empties = 0;
    for (size_t i = 0; i < n; ++i) {
        const HFStatic &e = entries[i];
        if (e.kind != HFKind::Empty) continue;
        ++empties;
        ++ctx.cases;
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        if (std::memcmp(ctx.image.At<uint8_t>(e.imageRva), body, sizeof(body)) != 0) ctx.Fail("slot %u: not an empty function", e.slot);
        gRegistered.clear();
        ctx.Original<void(__cdecl *)()>(e.imageRva)();
        staticinit::InitHF(&e);
        if (!gRegistered.empty()) ctx.Fail("slot %u: %u registrations", e.slot, static_cast<unsigned>(gRegistered.size()));
    }
    if (empties != 2) ctx.Fail("%d empty entries (2)", empties);
}

// ---- The CPU feature flags (slot 765) ----

namespace {
DWORD gFakePlatform, gFakeMinor;
BOOL WINAPI FakeGetVersionExA(OSVERSIONINFOA *info)
{
    std::memset(reinterpret_cast<uint8_t *>(info) + 4, 0x5a, sizeof(*info) - 4);
    info->dwMajorVersion = 4;
    info->dwMinorVersion = gFakeMinor;
    info->dwBuildNumber = 950;
    info->dwPlatformId = gFakePlatform;
    return TRUE;
}
#pragma warning(suppress : 4996)
BOOL WINAPI RealGetVersionExA(OSVERSIONINFOA *info) { return GetVersionExA(info); }
} // namespace

DIFF_TEST(StaticInitHF_CpuFeatures, 0xee0d1)
{
    constexpr uint32_t kGetVersionExA = 0xd9a138;
    auto image = ctx.Original<uint32_t(__cdecl *)()>(kCpuFeatures);
    {
        BindImport bind(ctx, kGetVersionExA, reinterpret_cast<const void *>(&FakeGetVersionExA));
        for (DWORD platform : {0u, 1u, 2u, 3u}) {
            for (DWORD minor : {0u, 1u, 10u, 90u}) {
                gFakePlatform = platform;
                gFakeMinor = minor;
                const uint32_t a = image(), b = staticinit::CpuFeatureFlags(platform, minor);
                const uint32_t c = staticinit::DetectCpuFeatures(reinterpret_cast<staticinit::GetVersionExFn>(&FakeGetVersionExA));
                ++ctx.cases;
                if (a != b || a != c) ctx.Fail("platform %u minor %u: the image's flags %u, ours %u / %u", platform, minor, a, b, c);
            }
        }
    }
    // The entries on the real system, from junk.
    BindImport bind(ctx, kGetVersionExA, reinterpret_cast<const void *>(&RealGetVersionExA));
    {
        const uint32_t a = image(), b = staticinit::DetectCpuFeatures();
        ++ctx.cases;
        if (a != b) ctx.Fail("this system: the image's flags %u, ours %u", a, b);
    }
    AtExitCapture capture(ctx);
    size_t n = 0;
    const HFStatic *entries = Entries(&n);
    for (size_t i = 0; i < n; ++i) {
        const HFStatic &e = entries[i];
        if (e.kind != HFKind::CpuFeatures || !SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e);
        std::vector<uint8_t> junk(saved.size(), 0xa5), a, b;
        Put(ctx, e, junk);
        ctx.Original<void(__cdecl *)()>(e.imageRva)();
        a = Window(ctx, e);
        Put(ctx, e, junk);
        staticinit::InitHF(&e);
        b = Window(ctx, e);
        ++ctx.cases;
        if (a != b || Word(a, kMargin) == 0xa5a5a5a5) ctx.Fail("slot %u: flags %08x by the image, %08x by ours", e.slot, Word(a, kMargin), Word(b, kMargin));
        if (!gRegistered.empty()) ctx.Fail("slot %u: a registration", e.slot);
        Put(ctx, e, saved);
    }
}

DIFF_TEST(StaticInitHF_CpuidAvailable, 0xee07f)
{
    auto image = ctx.Original<uint32_t(__cdecl *)()>(0xee07f);
    for (int i = 0; i < 200; ++i) {
        const uintptr_t before = __readeflags();
        const uint32_t a = image();
        const uintptr_t between = __readeflags();
        const uint32_t b = staticinit::CpuidAvailable();
        const uintptr_t after = __readeflags();
        ++ctx.cases;
        // EFLAGS is left as it was (its ID bit restored) by both.
        if (a != b || a != 0x200000u || (before ^ between) & 0x200000u || (between ^ after) & 0x200000u)
            ctx.Fail("CPUID check: the image's %08x, ours %08x (ID flag %x %x %x)", a, b, static_cast<unsigned>(before >> 21 & 1),
                static_cast<unsigned>(between >> 21 & 1), static_cast<unsigned>(after >> 21 & 1));
    }
}

DIFF_TEST(StaticInitHF_CpuidQuery, 0xee0a9)
{
    auto image = ctx.Original<void(__cdecl *)(uint32_t *, uint32_t)>(0xee0a9);
    // One processor: leaf 1's EBX carries the APIC id of the processor the thread is on.
    const DWORD_PTR oldMask = SetThreadAffinityMask(GetCurrentThread(), 1);
    // Leaves without sub-leaves only (the image's version leaves ECX as its caller had it; see CpuidQuery).
    const uint32_t leaves[] = {0, 1, 0x80000000u, 0x80000001u, 0x80000002u, 0x80000003u, 0x80000004u};
    for (int round = 0; round < 20; ++round) {
        for (uint32_t leaf : leaves) {
            uint32_t a[6], b[6];
            for (int i = 0; i < 6; ++i) a[i] = b[i] = 0xa5a5a5a5u + i;
            image(a + 1, leaf);
            staticinit::CpuidQuery(b + 1, leaf);
            ++ctx.cases;
            // The four registers in order, nothing written around them.
            if (std::memcmp(a, b, sizeof(a)) != 0 || a[0] != 0xa5a5a5a5u || a[5] != 0xa5a5a5aau)
                ctx.Fail("leaf %08x: the image's %08x %08x %08x %08x, ours %08x %08x %08x %08x", leaf, a[1], a[2], a[3], a[4], b[1], b[2], b[3], b[4]);
        }
    }
    if (oldMask) SetThreadAffinityMask(GetCurrentThread(), oldMask);
}

// ---- The cRZTimer section (slot 104) ----

namespace {
struct TimerImports {
    BindImport pid, print, create, error, map, unmap, close;
    explicit TimerImports(const difftest::Context &ctx)
        : pid(ctx, 0xd9a1bc, reinterpret_cast<const void *>(&GetCurrentProcessId)),
          print(ctx, 0xd9a5bc, reinterpret_cast<const void *>(&wsprintfA)),
          create(ctx, 0xd9a264, reinterpret_cast<const void *>(&CreateFileMappingA)),
          error(ctx, 0xd9a118, reinterpret_cast<const void *>(&GetLastError)),
          map(ctx, 0xd9a25c, reinterpret_cast<const void *>(&MapViewOfFile)),
          unmap(ctx, 0xd9a260, reinterpret_cast<const void *>(&UnmapViewOfFile)),
          close(ctx, 0xd9a17c, reinterpret_cast<const void *>(&CloseHandle))
    {
    }
};

std::string TimerName()
{
    char name[32];
    wsprintfA(name, "cRZTimer%08x", GetCurrentProcessId());
    return name;
}

// What a run leaves: {mapping != 0, view != 0, the view's two words, the name opens the same memory}, then after
// the thunk {the handle still valid, the view still mapped}.
struct TimerRun {
    uint32_t mapping = 0, view = 0;
    std::vector<uint32_t> facts;
    std::vector<uint8_t> margins;
    size_t registrations = 0;
};
} // namespace

DIFF_TEST(StaticInitHF_TimerSection, 0x1383b4)
{
    TimerImports imports(ctx);
    AtExitCapture capture(ctx);
    size_t n = 0;
    const HFStatic *entries = Entries(&n);
    const HFStatic *timer = nullptr;
    for (size_t i = 0; i < n; ++i) {
        if (entries[i].kind == HFKind::TimerSection) timer = &entries[i];
    }
    if (!timer || !SlotHolds(ctx, timer->slot, timer->imageRva)) {
        ctx.Fail("no timer entry");
        return;
    }
    const HFStatic &e = *timer;
    const std::vector<uint8_t> saved = Window(ctx, e);
    const std::string name = TimerName();
    std::mt19937 rng(104);
    // 0: fresh; 1: the mapping exists (junk in it is kept); 2: the name belongs to a mutex (no mapping).
    for (int round = 0; round < 3; ++round) {
        std::vector<uint8_t> junk(saved.size());
        for (auto &b : junk) b = static_cast<uint8_t>(rng());
        auto run = [&](auto init) {
            TimerRun r;
            HANDLE pre = nullptr;
            uint32_t *preView = nullptr;
            if (round == 1) {
                pre = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 8, name.c_str());
                preView = static_cast<uint32_t *>(MapViewOfFile(pre, FILE_MAP_ALL_ACCESS, 0, 0, 0));
                preView[0] = 0x11223344;
                preView[1] = 0x55667788;
            } else if (round == 2) {
                pre = CreateMutexA(nullptr, FALSE, name.c_str());
            }
            Put(ctx, e, junk);
            gRegistered.clear();
            SetLastError(0);
            init();
            const std::vector<uint8_t> w = Window(ctx, e);
            r.mapping = Word(w, kMargin);
            r.view = Word(w, kMargin + 4);
            r.margins.assign(w.begin(), w.begin() + kMargin);
            r.margins.insert(r.margins.end(), w.end() - kMargin, w.end());
            r.registrations = gRegistered.size();
            r.facts.push_back(r.mapping != 0);
            r.facts.push_back(r.view != 0);
            if (r.view) {
                const uint32_t *v = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(r.view));
                r.facts.push_back(v[0]);
                r.facts.push_back(v[1]);
                HANDLE other = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
                uint32_t *ov = other ? static_cast<uint32_t *>(MapViewOfFile(other, FILE_MAP_ALL_ACCESS, 0, 0, 0)) : nullptr;
                if (ov) {
                    ov[1] = 0xabcdef01;
                    r.facts.push_back(v[1] == 0xabcdef01);
                    MEMORY_BASIC_INFORMATION mi{};
                    VirtualQuery(ov, &mi, sizeof(mi));
                    r.facts.push_back(static_cast<uint32_t>(mi.RegionSize));
                    UnmapViewOfFile(ov);
                }
                if (other) CloseHandle(other);
            }
            if (r.registrations == 1) {
                gRegistered[0]();
                DWORD flags = 0;
                r.facts.push_back(r.mapping && GetHandleInformation(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(r.mapping)), &flags));
                MEMORY_BASIC_INFORMATION mi{};
                r.facts.push_back(r.view && VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(r.view)), &mi, sizeof(mi)) &&
                                  mi.State == MEM_COMMIT);
                const std::vector<uint8_t> after = Window(ctx, e);
                r.facts.push_back(Word(after, kMargin) == r.mapping && Word(after, kMargin + 4) == r.view);
            }
            if (preView) UnmapViewOfFile(preView);
            if (pre) CloseHandle(pre);
            return r;
        };
        TimerRun a = run([&] { ctx.Original<void(__cdecl *)()>(e.imageRva)(); });
        TimerRun b = run([&] { staticinit::InitHF(&e); });
        ++ctx.cases;
        if (a.registrations != 1 || b.registrations != 1)
            ctx.Fail("round %d: %u / %u registrations", round, static_cast<unsigned>(a.registrations), static_cast<unsigned>(b.registrations));
        if (a.facts != b.facts || a.margins != b.margins) ctx.Fail("round %d: the section differs (%u facts / %u)", round,
                                                                   static_cast<unsigned>(a.facts.size()), static_cast<unsigned>(b.facts.size()));
        if (round == 0 && (a.facts.size() < 6 || a.facts[2] != 0 || a.facts[3] != 0)) ctx.Fail("round 0: no fresh zeroed view");
        if (round == 1 && (a.facts.size() < 6 || a.facts[2] != 0x11223344)) ctx.Fail("round 1: the existing view was not kept");
        if (round == 2 && (a.facts[0] || a.facts[1])) ctx.Fail("round 2: a mapping over a mutex's name");
    }
    Put(ctx, e, saved);
}

// ---- The runner over the image's table with only these slots ----

DIFF_TEST(StaticInitHF_OnImageTable, 0x13e09d)
{
    TimerImports imports(ctx);
    BindImport version(ctx, 0xd9a138, reinterpret_cast<const void *>(&RealGetVersionExA));
    AtExitCapture capture(ctx);
    size_t ownedCount = 0, n = 0;
    const winmainstart::OwnedInitialiser *owned = staticinit::OwnedHF(&ownedCount);
    const HFStatic *entries = Entries(&n);
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    std::vector<winmainstart::Initialiser> table(slots, nullptr);
    for (size_t i = 0; i < ownedCount; ++i) {
        if (owned[i].slot < slots) table[owned[i].slot] = ctx.image.At<winmainstart::Initialiser>(kXcFirst)[owned[i].slot];
    }
    std::vector<std::vector<uint8_t>> saved;
    for (size_t i = 0; i < n; ++i) saved.push_back(entries[i].kind == HFKind::Empty ? std::vector<uint8_t>() : Window(ctx, entries[i]));
    auto restore = [&] {
        for (size_t i = 0; i < n; ++i) {
            if (entries[i].kind != HFKind::Empty) Put(ctx, entries[i], saved[i]);
        }
    };
    auto values = [&] {
        std::vector<uint8_t> all;
        for (size_t i = 0; i < n; ++i) {
            if (!IsComputed(entries[i].kind)) continue;
            std::vector<uint8_t> w = Window(ctx, entries[i]);
            all.insert(all.end(), w.begin(), w.end());
        }
        return all;
    };
    auto lifo = [&](std::vector<size_t> &freed) {
        difftest::FreeRecorder rec;
        for (auto it = gRegistered.rbegin(); it != gRegistered.rend(); ++it) (*it)();
        rec.Stop();
        for (size_t k = 0; k < rec.Count(); ++k) freed.push_back(rec.Size(k));
    };

    restore();
    gRegistered.clear();
    for (uint32_t s = 0; s < slots; ++s) {
        if (table[s] != nullptr) table[s]();
    }
    const size_t imageRegistrations = gRegistered.size();
    const std::vector<uint8_t> valuesA = values();
    std::vector<size_t> freedA;
    lifo(freedA);

    restore();
    gRegistered.clear();
    winmainstart::XcRunCounts c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots, ctx.image.Base(), owned, ownedCount);
    const size_t ourRegistrations = gRegistered.size();
    const std::vector<uint8_t> valuesB = values();
    std::vector<size_t> freedB;
    lifo(freedB);
    ++ctx.cases;
    if (c.owned != static_cast<int>(ownedCount) || ownedCount != 36 || c.image || c.mismatched || c.unused)
        ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused (owned %u)", c.owned, c.image, c.mismatched, c.unused,
                 static_cast<unsigned>(ownedCount));
    // 17 trees, 2 hashes, 2 vector entries, the name table, the two arrays, the timer section.
    if (imageRegistrations != 25 || ourRegistrations != imageRegistrations)
        ctx.Fail("registrations: %u by the image's entries (25), %u by ours", static_cast<unsigned>(imageRegistrations),
                 static_cast<unsigned>(ourRegistrations));
    if (valuesA != valuesB) ctx.Fail("computed values differ after the run");
    // The frees in exit order: the same blocks (sizes) in the same order; at least the 19 head nodes, 2 bucket arrays
    // and the name table's buffer.
    if (freedA != freedB || freedA.size() < 22)
        ctx.Fail("exit list: %u blocks freed after the image's entries, %u after ours", static_cast<unsigned>(freedA.size()),
                 static_cast<unsigned>(freedB.size()));
    restore();
}
