// Differential tests for the owned "atexit-only" XC entries (src/app/StaticInitAtExit.cpp, lane HC;
// docs/subsystems/startup.md "Done in lane HC"): 109 objects the image constant-initialises in .data whose entry only
// registers the destructor thunk.
//
// StaticInitAtExit_Form: what each image entry does, on the object and 8 bytes on each side filled with two junk
// patterns: nothing written, exactly one atexit registration, of the thunk our table names. And the object as the
// image's file holds it: an empty object of its kind (a std::string of size 0 and capacity 15, everything else zero),
// so the entry has nothing to construct.
//
// StaticInitAtExit_Thunks: each entry against the image's in its slot: the entry run (one registration each), then the
// registered thunk on start states of its kind (empty, and live objects: heap strings and vectors of both block
// schemes, chunk lists, references to fake objects), with 8 junk bytes on each side. Compared: the window after the
// thunk, byte for byte (a freed heap pointer's word compared relative to that side's block, since each side frees its
// own), the blocks freed (each side exactly its own, by the recorder), the calls into the fake objects (slot, `this`,
// argument). The pools: the image's RZFree redirected to a logging HeapFree (its chunks from the process heap); ours
// frees into a real allocator (its chunks from it), and the chunks it hands out next are the ones freed.
//
// StaticInitAtExit_OnImageTable: the runner over a copy of the XC table with only these slots, from the file's state:
// 109 ours, 109 registrations each, and after each side's exit list run LIFO the same objects.
#include "difftest.h"
#include "free_recorder.h"
#include "game_allocator.h"
#include "app/StaticInit.h"
#include "app/StaticInitAtExit.h"
#include "app/WinMainStart.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"

#include <windows.h>
#include <oleauto.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using staticinit::AtExitKind;
using staticinit::AtExitStatic;

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kAtExit = 0x824cbe, kRZFree = 0x1be3a1;
constexpr uint32_t kVariantClearImport = 0xd9a474; // the image's IAT entry the Variant thunk calls through
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

// The image's VariantClear import bound to the real one (the harness traps every import) for a test.
struct BindVariantClear {
    void **entry;
    void *saved;
    explicit BindVariantClear(const difftest::Context &ctx) : entry(ctx.image.At<void *>(kVariantClearImport)), saved(*entry)
    {
        DWORD old;
        VirtualProtect(entry, 4, PAGE_READWRITE, &old);
        *entry = reinterpret_cast<void *>(&VariantClear);
    }
    ~BindVariantClear() { *entry = saved; }
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

size_t WindowSize(const AtExitStatic &e) { return staticinit::AtExitObjectSize(e.kind) + 2 * kMargin; }

std::vector<uint8_t> Window(const difftest::Context &ctx, const AtExitStatic &e)
{
    const uint8_t *p = ctx.image.At<uint8_t>(e.storage - kMargin);
    return std::vector<uint8_t>(p, p + WindowSize(e));
}

void Put(const difftest::Context &ctx, const AtExitStatic &e, const std::vector<uint8_t> &w)
{
    std::memcpy(ctx.image.At<uint8_t>(e.storage - kMargin), w.data(), w.size());
}

uint32_t WordAt(const std::vector<uint8_t> &w, size_t off)
{
    uint32_t v;
    std::memcpy(&v, w.data() + off, 4);
    return v;
}

void SetWord(std::vector<uint8_t> &w, size_t off, uint32_t v) { std::memcpy(w.data() + off, &v, 4); }

uint32_t Addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ---- Fake objects for the reference kinds: every call through their vtable is logged ----

struct Call {
    int slot;
    uint32_t self, arg;
    bool operator==(const Call &) const = default;
};
std::vector<Call> gCalls;

uint32_t __fastcall LogSlot0(void *self, void *, uint32_t flags) // thiscall, one argument (a deleting destructor)
{
    gCalls.push_back({0, Addr(self), flags});
    return Addr(self);
}
uint32_t __fastcall LogSlot1(void *self, void *)
{
    gCalls.push_back({1, Addr(self), 0});
    return 7;
}
uint32_t __fastcall LogSlot2(void *self, void *)
{
    gCalls.push_back({2, Addr(self), 0});
    return 3;
}
void *const kLogVTable[3] = {reinterpret_cast<void *>(&LogSlot0), reinterpret_cast<void *>(&LogSlot1),
                             reinterpret_cast<void *>(&LogSlot2)};
// COM's IUnknown (what VariantClear calls): stdcall, `this` on the stack.
HRESULT __stdcall ComSlot0(void *self, const IID &, void **)
{
    gCalls.push_back({0, Addr(self), 0});
    return E_NOINTERFACE;
}
ULONG __stdcall ComSlot1(void *self)
{
    gCalls.push_back({1, Addr(self), 0});
    return 7;
}
ULONG __stdcall ComSlot2(void *self)
{
    gCalls.push_back({2, Addr(self), 0});
    return 3;
}
void *const kComVTable[3] = {reinterpret_cast<void *>(&ComSlot0), reinterpret_cast<void *>(&ComSlot1),
                             reinterpret_cast<void *>(&ComSlot2)};

// 64 bytes a fake object lives in; `offset` is where the vtable pointer goes (the cIGZUnknown subobject). For the
// virtual-base form, the first word is the vbtable pointer and the vbtable's second word that offset (its first junk).
struct FakeObject {
    alignas(16) uint8_t bytes[64];
    uint32_t vbtable[2];
    uint32_t offset = 0;
    void Make(std::mt19937 &rng, bool virtualBase, bool com = false)
    {
        for (auto &b : bytes) b = static_cast<uint8_t>(rng());
        offset = virtualBase ? 4 * (1 + rng() % 8) : 0;
        if (virtualBase) {
            vbtable[0] = rng();
            vbtable[1] = offset;
            void *vb = vbtable;
            std::memcpy(bytes, &vb, 4);
        }
        const void *vt = com ? kComVTable : kLogVTable;
        std::memcpy(bytes + offset, &vt, 4);
    }
};

// ---- A start state per kind ----

struct Start {
    std::vector<uint8_t> window;
    const void *block = nullptr; // a heap block the object owns (string / vector)
    const void *freed = nullptr; // what freeing it hands to HeapFree (the block, or the real allocation of a big one)
    size_t pointerWord = SIZE_MAX; // the window offset of a word compared relative to `block`
    std::vector<void *> chunks;  // a pool's chunk list, in order
    uint32_t chunkBytes = 0;
    FakeObject *fake = nullptr;
    size_t fakeWord = kMargin;   // where the window holds the pointer to it
};

// The allocation a std::allocator block of `bytes` came from (big blocks are 32-aligned with the real pointer before).
const void *RealBlock(const void *block, size_t bytes)
{
    return bytes >= 4096 ? reinterpret_cast<void *const *>(block)[-1] : block;
}

// round 0 is always the empty object of the kind (as the file holds it, junk margins).
Start MakeStart(const AtExitStatic &e, int round, std::mt19937 &rng, bool imageSide, FakeObject &fake)
{
    Start s;
    s.window.resize(WindowSize(e));
    for (auto &b : s.window) b = static_cast<uint8_t>(rng());
    uint8_t *object = s.window.data() + kMargin;
    const size_t size = staticinit::AtExitObjectSize(e.kind);
    if (e.kind != AtExitKind::String) std::memset(object, 0, size);
    switch (e.kind) {
    case AtExitKind::String: {
        uint32_t capacity = 15, length = 0;
        if (round == 1) length = rng() % 16;
        if (round >= 2) {
            capacity = round % 2 ? 16 + rng() % 4000 : 4095 + rng() % 3000;
            length = rng() % (capacity + 1);
            auto *block = std::allocator<char>().allocate(capacity + 1);
            std::memset(block, 'q', capacity + 1);
            s.block = block;
            s.freed = RealBlock(block, capacity + 1);
            s.pointerWord = kMargin;
            SetWord(s.window, kMargin, Addr(block));
        }
        if (round == 0) object[0] = 0;
        SetWord(s.window, kMargin + 0x10, length);
        SetWord(s.window, kMargin + 0x14, capacity);
        break;
    }
    case AtExitKind::Vector4:
    case AtExitKind::Vector8: {
        const uint32_t element = e.kind == AtExitKind::Vector4 ? 4 : 8;
        if (round == 0) break;
        if (round == 1) { // first null: the other two words are left alone
            SetWord(s.window, kMargin + 4, rng());
            SetWord(s.window, kMargin + 8, rng());
            break;
        }
        const uint32_t capacity = round % 2 ? 1 + rng() % 500 : 4096 / element + rng() % 600;
        const uint32_t used = rng() % (capacity + 1);
        uint8_t *block = e.kind == AtExitKind::Vector4 ? reinterpret_cast<uint8_t *>(std::allocator<uint32_t>().allocate(capacity))
                                                       : reinterpret_cast<uint8_t *>(std::allocator<uint64_t>().allocate(capacity));
        s.block = block;
        s.freed = RealBlock(block, capacity * element);
        SetWord(s.window, kMargin, Addr(block));
        SetWord(s.window, kMargin + 4, Addr(block + used * element));
        SetWord(s.window, kMargin + 8, Addr(block + capacity * element));
        break;
    }
    case AtExitKind::PoolChunks: {
        const uint32_t n = round == 0 ? 0 : 1 + rng() % 6;
        const uint32_t bytes = 8 + rng() % 600;
        s.chunkBytes = bytes;
        for (uint32_t i = 0; i < n; ++i) {
            void *chunk = imageSide ? HeapAlloc(GetProcessHeap(), 0, bytes) : RZMemory::Malloc(bytes);
            std::memset(chunk, 0x5c, bytes);
            if (i) std::memcpy(s.chunks.back(), &chunk, 4);
            s.chunks.push_back(chunk);
        }
        if (n) {
            std::memset(s.chunks.back(), 0, 4);
            SetWord(s.window, kMargin, Addr(s.chunks[0]));
        }
        break;
    }
    case AtExitKind::Release:
    case AtExitKind::ReleaseVirtualBase:
    case AtExitKind::Delete:
        if (round == 0) break;
        fake.Make(rng, e.kind == AtExitKind::ReleaseVirtualBase);
        s.fake = &fake;
        SetWord(s.window, kMargin, Addr(fake.bytes));
        break;
    case AtExitKind::Variant: {
        VARIANT v;
        VariantInit(&v);
        if (round == 4) { // the file's state: vtMissing
            v.vt = VT_ERROR;
            v.scode = DISP_E_PARAMNOTFOUND;
        } else if (round % 4 == 1) {
            v.vt = VT_I4;
            v.lVal = static_cast<LONG>(rng());
        } else if (round % 4 == 2) {
            v.vt = VT_R8;
            v.dblVal = static_cast<double>(rng()) / 7;
        } else if (round % 4 == 3) { // an interface: VariantClear releases it
            fake.Make(rng, false, true);
            s.fake = &fake;
            s.fakeWord = kMargin + 8;
            v.vt = static_cast<VARTYPE>(round == 3 ? VT_UNKNOWN : VT_DISPATCH);
            v.punkVal = reinterpret_cast<IUnknown *>(fake.bytes);
        }
        std::memcpy(object, &v, sizeof(v));
        break;
    }
    }
    return s;
}

int RoundsOf(AtExitKind kind)
{
    switch (kind) {
    case AtExitKind::String:
    case AtExitKind::Vector4:
    case AtExitKind::Vector8: return 8;
    case AtExitKind::Variant: return 8;
    default: return 5;
    }
}

struct ThunkRun {
    std::vector<uint8_t> after;
    size_t registrations = 0;
    std::vector<const void *> frees;
    std::vector<Call> calls;
};

std::vector<void *> gLoggedFrees;
void __cdecl LoggingFree(void *p)
{
    gLoggedFrees.push_back(p);
    HeapFree(GetProcessHeap(), 0, p);
}

template <typename Init> ThunkRun RunThunk(const difftest::Context &ctx, const AtExitStatic &e, const Start &s, Init init)
{
    ThunkRun r;
    Put(ctx, e, s.window);
    gRegistered.clear();
    init();
    r.registrations = gRegistered.size();
    if (r.registrations != 1) return r;
    gCalls.clear();
    {
        difftest::FreeRecorder rec;
        gRegistered[0]();
        rec.Stop();
        for (size_t i = 0; i < rec.Count(); ++i) r.frees.push_back(rec.Pointer(i));
    }
    r.calls = gCalls;
    r.after = Window(ctx, e);
    if (s.pointerWord != SIZE_MAX) {
        // Relative to this side's block; a string's first character overlays the pointer's low byte (the destructor
        // zeroes it), so that byte is compared as it is.
        const uint32_t word = WordAt(r.after, s.pointerWord);
        const uint32_t relative = word ^ Addr(s.block);
        SetWord(r.after, s.pointerWord, e.kind == AtExitKind::String ? (relative & ~0xffu) | (word & 0xff) : relative);
    }
    if (s.fake) {
        for (auto &c : r.calls) c.self -= Addr(s.fake->bytes);
        SetWord(r.after, s.fakeWord, WordAt(r.after, s.fakeWord) ^ Addr(s.fake->bytes));
    }
    return r;
}

} // namespace

// The 109 image initialisers, in slot order (db/initializers.jsonl: atexit-only, the owned ones), checked against our
// table independently of it.
constexpr uint32_t kImageEntries[109] = {
    0x136e60, 0x136e80, 0x136e70, 0x138840, 0x138870, 0x1388e0, 0x138c30, 0x139270, 0x139280, 0x139290, 0x1392d0, 0x1394a0,
    0x1394b0, 0x139570, 0x1395a0, 0x139780, 0x1397c0, 0x13a780, 0x13a790, 0x13a910, 0x13a920, 0x13a950, 0x13a960, 0x13ab10,
    0x13ab50, 0x13b5b0, 0x13c4e0, 0x13c810, 0x13d0b4, 0x13d0c0, 0x13d2fb, 0x13dff0, 0x13e4c0, 0x13e4d0, 0x13e4e0, 0x13e4f0,
    0x13e500, 0x13e510, 0x13e520, 0x13e530, 0x13e540, 0x13e550, 0x13e560, 0x13e570, 0x13e580, 0x13e590, 0x13e7a0, 0x13e7b0,
    0x13e7c0, 0x13e7d0, 0x13e7e0, 0x13e8c0, 0x13e9f0, 0x13ef40, 0x13f500, 0x13f510, 0x13f520, 0x13f560, 0x13f550, 0x13f570,
    0x13f5a0, 0x13f5ce, 0x13f5c2, 0x13f720, 0x13f730, 0x13f740, 0x13f750, 0x13f7a0, 0x13ff70, 0x13ffa0, 0x140010, 0x140050,
    0x140060, 0x140070, 0x140130, 0x140180, 0x1401a0, 0x140190, 0x1401c0, 0x140230, 0x140260, 0x140290, 0x1402c0, 0x1402d0,
    0x140300, 0x140330, 0x140360, 0x140390, 0x1403a0, 0x1403b0, 0x1403c0, 0x1403d0, 0x1403e0, 0x1404f0, 0x140500, 0x140887,
    0x1408c0, 0x1409e0, 0x140a10, 0x140a20, 0x140a30, 0x140a80, 0x140a90, 0x140ac0, 0x140af0, 0x140b00, 0x140e40, 0x140e50,
    0x140e60,
};

DIFF_TEST(StaticInitAtExit_Form, 0x13e046)
{
    AtExitCapture capture(ctx);
    size_t n = 0;
    const AtExitStatic *entries = staticinit::AtExitStatics(&n);
    if (n != 109) ctx.Fail("%u atexit-only entries, expected 109", static_cast<unsigned>(n));
    int whitespace = 0, slashes = 0, empties = 0;
    for (size_t i = 0; i < n; ++i) {
        const AtExitStatic &e = entries[i];
        if (i < 109 && e.imageRva != kImageEntries[i]) ctx.Fail("row %u: entry %06x, expected %06x", static_cast<unsigned>(i), e.imageRva, kImageEntries[i]);
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e);
        // The file's object: a short string in its inline buffer (capacity 15, the terminator after the characters:
        // 70 of the 72 hold " \t\n\r", 2 "\/"), comutil's vtMissing, everything else zero.
        std::vector<uint8_t> file(saved.begin() + kMargin, saved.end() - kMargin);
        ++ctx.cases;
        if (e.kind == AtExitKind::String) {
            const uint32_t size = WordAt(file, 0x10), capacity = WordAt(file, 0x14);
            if (capacity != 15 || size > 15 || file[size] != 0)
                ctx.Fail("slot %u: the file's string at %06x: size %u, capacity %u", e.slot, e.storage, size, capacity);
            else if (size == 4 && std::memcmp(file.data(), " \t\n\r", 5) == 0) ++whitespace;
            else if (size == 2 && std::memcmp(file.data(), "\\/", 3) == 0) ++slashes;
            else if (size == 0) ++empties;
            else ctx.Fail("slot %u: the file's string at %06x holds %u characters", e.slot, e.storage, size);
        } else if (e.kind == AtExitKind::Variant) {
            // comutil's vtMissing: VT_ERROR, DISP_E_PARAMNOTFOUND.
            if (WordAt(file, 0) != VT_ERROR || WordAt(file, 4) != 0 || WordAt(file, 8) != 0x80020004u || WordAt(file, 12) != 0)
                ctx.Fail("slot %u: the file's VARIANT at %06x: %08x %08x %08x %08x", e.slot, e.storage, WordAt(file, 0),
                         WordAt(file, 4), WordAt(file, 8), WordAt(file, 12));
        } else if (std::count(file.begin(), file.end(), uint8_t(0)) != static_cast<ptrdiff_t>(file.size())) {
            ctx.Fail("slot %u: the file's object at %06x is not zero (kind %d)", e.slot, e.storage, static_cast<int>(e.kind));
        }
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (uint8_t pattern : {uint8_t(0x5a), uint8_t(0xc3)}) {
            std::vector<uint8_t> junk(saved.size());
            for (size_t k = 0; k < junk.size(); ++k) junk[k] = static_cast<uint8_t>(pattern + k * 7);
            Put(ctx, e, junk);
            gRegistered.clear();
            image();
            ++ctx.cases;
            if (Window(ctx, e) != junk) ctx.Fail("slot %u: the image's entry wrote the object", e.slot);
            if (gRegistered.size() != 1 || reinterpret_cast<void *>(gRegistered[0]) != ctx.image.At<void>(e.thunk))
                ctx.Fail("slot %u: the image's entry registered %u thunks, not %06x", e.slot,
                         static_cast<unsigned>(gRegistered.size()), e.thunk);
        }
        Put(ctx, e, saved);
    }
    if (whitespace != 70 || slashes != 2 || empties != 0)
        ctx.Fail("the file's strings: %d \" \\t\\n\\r\" (70), %d \"\\\\/\" (2), %d empty (0)", whitespace, slashes, empties);
}

DIFF_TEST(StaticInitAtExit_Thunks, 0xd90d40)
{
    AtExitCapture capture(ctx);
    Redirect rzFree(ctx, kRZFree, reinterpret_cast<const void *>(&LoggingFree));
    BindVariantClear variantClear(ctx);
    GeneralAllocator *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;

    size_t n = 0;
    const AtExitStatic *entries = staticinit::AtExitStatics(&n);
    std::mt19937 rng(0x4c);
    for (size_t i = 0; i < n; ++i) {
        const AtExitStatic &e = entries[i];
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        const std::vector<uint8_t> saved = Window(ctx, e);
        auto image = ctx.Original<void(__cdecl *)()>(e.imageRva);
        for (int round = 0; round < RoundsOf(e.kind); ++round) {
            const uint32_t seed = rng();
            FakeObject fakeA, fakeB;
            std::mt19937 ra(seed), rb(seed);
            Start sa = MakeStart(e, round, ra, true, fakeA);
            Start sb = MakeStart(e, round, rb, false, fakeB);
            gLoggedFrees.clear();
            ThunkRun a = RunThunk(ctx, e, sa, [&] { image(); });
            const std::vector<void *> loggedA = gLoggedFrees;
            gLoggedFrees.clear();
            ThunkRun b = RunThunk(ctx, e, sb, [&] { staticinit::InitAtExit(&e); });
            ++ctx.cases;
            if (a.registrations != 1 || b.registrations != 1) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours", e.slot,
                         static_cast<unsigned>(a.registrations), static_cast<unsigned>(b.registrations));
                continue;
            }
            if (a.after != b.after) ctx.Fail("slot %u round %d: the objects differ after the thunks", e.slot, round);
            if (a.calls != b.calls)
                ctx.Fail("slot %u round %d: %u calls into the object by the image's thunk, %u by ours%s", e.slot, round,
                         static_cast<unsigned>(a.calls.size()), static_cast<unsigned>(b.calls.size()),
                         a.calls.size() == 1 && b.calls.size() == 1 ? " (slot, this or argument differ)" : "");
            if (sa.fake && a.calls.size() != 1) ctx.Fail("slot %u round %d: the image's thunk made %u calls", e.slot, round, static_cast<unsigned>(a.calls.size()));
            if (e.kind == AtExitKind::PoolChunks) {
                // The image's frees: the chunks in list order. Ours: the allocator hands back exactly the chunks.
                if (loggedA != std::vector<void *>(sa.chunks.begin(), sa.chunks.end()))
                    ctx.Fail("slot %u round %d: the image's thunk freed %u blocks, not the %u chunks in order", e.slot, round,
                             static_cast<unsigned>(loggedA.size()), static_cast<unsigned>(sa.chunks.size()));
                if (!gLoggedFrees.empty()) ctx.Fail("slot %u: our thunk called the image's RZFree", e.slot);
                std::vector<void *> again, made = sb.chunks;
                for (size_t k = 0; k < made.size(); ++k) again.push_back(RZMemory::Malloc(sb.chunkBytes));
                std::sort(again.begin(), again.end());
                std::sort(made.begin(), made.end());
                if (again != made) ctx.Fail("slot %u round %d: our thunk did not free exactly the %u chunks", e.slot, round, static_cast<unsigned>(made.size()));
                for (void *p : again) RZMemory::Free(p);
                if (WordAt(b.after, kMargin) != 0) ctx.Fail("slot %u round %d: our pool's chunk list not emptied", e.slot, round);
            } else {
                const std::vector<const void *> wantA = sa.freed ? std::vector<const void *>{sa.freed} : std::vector<const void *>{};
                const std::vector<const void *> wantB = sb.freed ? std::vector<const void *>{sb.freed} : std::vector<const void *>{};
                if (a.frees != wantA || b.frees != wantB)
                    ctx.Fail("slot %u round %d: %u / %u frees (%u / %u expected, each side its own block)", e.slot, round,
                             static_cast<unsigned>(a.frees.size()), static_cast<unsigned>(b.frees.size()),
                             static_cast<unsigned>(wantA.size()), static_cast<unsigned>(wantB.size()));
            }
        }
        Put(ctx, e, saved);
    }
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
}

DIFF_TEST(StaticInitAtExit_OnImageTable, 0x138840)
{
    AtExitCapture capture(ctx);
    BindVariantClear variantClear(ctx);
    size_t ownedCount = 0, n = 0;
    const winmainstart::OwnedInitialiser *owned = staticinit::OwnedAtExits(&ownedCount);
    const AtExitStatic *entries = staticinit::AtExitStatics(&n);
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
    // Every object's bytes (the margins left out: neighbouring objects sit in them; the Thunks test covers them).
    auto objects = [&] {
        std::vector<uint8_t> all;
        for (size_t i = 0; i < n; ++i) {
            std::vector<uint8_t> w = Window(ctx, entries[i]);
            all.insert(all.end(), w.begin() + kMargin, w.end() - kMargin);
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
    std::vector<uint8_t> builtA = objects();
    lifo();
    std::vector<uint8_t> destroyedA = objects();

    restore();
    gRegistered.clear();
    winmainstart::XcRunCounts c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots, ctx.image.Base(),
                                                                      owned, ownedCount);
    const size_t ourRegistrations = gRegistered.size();
    std::vector<uint8_t> builtB = objects();
    lifo();
    std::vector<uint8_t> destroyedB = objects();
    restore();

    ++ctx.cases;
    if (c.owned != 109 || c.image || c.mismatched || c.unused)
        ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused", c.owned, c.image, c.mismatched, c.unused);
    if (imageRegistrations != 109 || ourRegistrations != 109)
        ctx.Fail("registrations: %u by the image's entries, %u by ours (109)", static_cast<unsigned>(imageRegistrations),
                 static_cast<unsigned>(ourRegistrations));
    if (builtA != builtB) ctx.Fail("the objects differ after the run");
    if (destroyedA != destroyedB) ctx.Fail("the objects differ after the exit list");
}
