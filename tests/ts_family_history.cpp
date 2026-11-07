// Differential tests for cTSFamilyHistory and its snapshot entries (src/ts/cTSFamilyHistory.*).
//
// The world: a history object carrying the game's vtables with a std::vector of random snapshots (random texts in the
// allocator-service vectors, real cRZString vtable so element destruction runs the game's cRZString destructor), the
// allocator service static (0x10c95d4, guard 0x10c95d8) pointed at a recording fake, and a recording stream (reads
// deliver seeded random values, writes are logged, slot 20's metadata answers slot 78 with the version). A run compares
// the return value, the event log (stream and allocator calls, in order) and the resulting entries (every field, text
// contents, element vtable/refcount, vector sizes and capacities).
//
// Standalone shape (lane FI): every class test runs three routes (Routes / Call below): the original, our vtable, the adapters.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts/cTSFamilyHistory.h"

#define NOMINMAX
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using Blob = std::vector<uint32_t>;
uint32_t P(const void *p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }

enum : uint32_t {
    evRead8 = 1, evRead16, evRead32s, evRead32u, evReadText, evError, evSetMeta, evGetMeta, evVersion,
    evWrite8, evWrite16, evWrite32s, evWrite32u, evWriteText, evAlloc, evFree, evAddRef, evDelete,
};

Blob gEv;
void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }

uint32_t TextCode(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ uint8_t(c)) * 16777619u;
    return h ^ uint32_t(s.size());
}

std::string RandomText(std::mt19937 &rng)
{
    const unsigned lengths[] = {0, 1, 7, 15, 16, 31, 40};
    std::string s(lengths[rng() % 7], 'x');
    for (char &c : s) c = char('a' + rng() % 26);
    return s;
}

// ---- allocator service fake ----
struct Alloc {
    void **vt;
} gAlloc;
void *gAllocVT[8];
void *__fastcall AllocAllocate(Alloc *, void *, uint32_t size)
{
    Ev({evAlloc, size});
    return ::operator new(size);
}
void __fastcall AllocFree(Alloc *, void *, void *memory)
{
    Ev({evFree, memory ? 1u : 0u});
    ::operator delete(memory);
}

struct AllocWorld {
    AllocWorld()
        : alloc(GameAddress<void *>(0x10c95d4)), guard(GameAddress<int32_t>(0x10c95d8)), savedAlloc(*alloc), savedGuard(*guard)
    {
        for (auto &slot : gAllocVT) slot = nullptr;
        gAllocVT[3] = reinterpret_cast<void *>(&AllocAllocate);
        gAllocVT[4] = reinterpret_cast<void *>(&AllocFree);
        gAlloc.vt = gAllocVT;
        *alloc = &gAlloc;
        *guard = INT32_MIN; // initialised: the cached service is used as is
    }
    ~AllocWorld()
    {
        *alloc = savedAlloc;
        *guard = savedGuard;
    }
    void **alloc;
    int32_t *guard;
    void *savedAlloc;
    int32_t savedGuard;
};

// ---- stream fake ----
struct Meta {
    void **vt;
    uint32_t version;
} gMeta;
void *gMetaVT[80];
const uint32_t *__fastcall MetaVersion(Meta *self, void *)
{
    Ev({evVersion});
    return &self->version;
}

struct Stream {
    void **vt;
    std::mt19937 rng;
    uint32_t headerReads = 0; // the first three 32-bit reads of a record header
    uint32_t version = 0;
    uint32_t errorBits = 0;
    uint32_t errorCalls = 0;
    uint32_t maxCount = 4;
} gStream;
void *gStreamVT[24];

uint32_t Pick(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return 0;
    case 1: return 0xffffffffu;
    case 2: return 0x8000u;
    case 3: return 0x7fffu;
    default: return rng();
    }
}
uint8_t __fastcall Read8(Stream *s, void *, uint8_t *out)
{
    const uint8_t v = s->rng() % 3 == 0 ? uint8_t(s->rng()) : uint8_t(s->rng() % 2);
    Ev({evRead8, v});
    *out = v;
    return 1;
}
uint8_t __fastcall Read16(Stream *s, void *, uint16_t *out)
{
    const uint16_t v = uint16_t(Pick(s->rng));
    Ev({evRead16, v});
    *out = v;
    return 1;
}
uint8_t __fastcall Read32s(Stream *s, void *, uint32_t *out)
{
    const uint32_t v = Pick(s->rng);
    Ev({evRead32s, v});
    *out = v;
    return 1;
}
uint8_t __fastcall Read32u(Stream *s, void *, uint32_t *out)
{
    uint32_t v;
    const uint32_t n = s->headerReads++;
    if (n == 1) v = s->version;
    else if (n < 3) v = s->rng();
    else v = s->rng() % (s->maxCount + 1); // counts
    Ev({evRead32u, n, v});
    *out = v;
    return 1;
}
uint8_t __fastcall ReadText(Stream *s, void *, cRZString *text)
{
    text->mString = RandomText(s->rng);
    Ev({evReadText, TextCode(text->mString)});
    return 1;
}
uint8_t __fastcall GetError(Stream *s, void *)
{
    const uint32_t n = s->errorCalls++;
    const uint8_t v = (s->errorBits >> (n % 32)) & 1 ? uint8_t(1 + n % 3) : 0;
    Ev({evError, n, v});
    return v;
}
void __fastcall SetMeta(Stream *, void *, void *metadata)
{
    uint32_t words[5];
    std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, words[0] ? 1u : 0u, words[1], words[2], words[3], words[4]});
    ::operator delete(metadata, 0x14);
}
Meta *__fastcall GetMeta(Stream *, void *)
{
    Ev({evGetMeta});
    return &gMeta;
}
uint8_t __fastcall Write8(Stream *, void *, uint8_t v)
{
    Ev({evWrite8, v});
    return 1;
}
uint8_t __fastcall Write16(Stream *, void *, uint16_t v)
{
    Ev({evWrite16, v});
    return 1;
}
uint8_t __fastcall Write32s(Stream *, void *, uint32_t v)
{
    Ev({evWrite32s, v});
    return 1;
}
uint8_t __fastcall Write32u(Stream *, void *, uint32_t v)
{
    Ev({evWrite32u, v});
    return 1;
}
uint8_t __fastcall WriteText(Stream *, void *, const cRZString *text)
{
    Ev({evWriteText, TextCode(text->mString), uint32_t(text->mRefCount)});
    return 1;
}

void ResetStream(uint32_t seed, uint32_t version, bool reading)
{
    for (auto &slot : gStreamVT) slot = nullptr;
    gStreamVT[5] = reading ? reinterpret_cast<void *>(&Read8) : reinterpret_cast<void *>(&Write8);
    gStreamVT[6] = reading ? reinterpret_cast<void *>(&Read16) : reinterpret_cast<void *>(&Write16);
    gStreamVT[8] = reading ? reinterpret_cast<void *>(&Read32s) : reinterpret_cast<void *>(&Write32s);
    gStreamVT[9] = reading ? reinterpret_cast<void *>(&Read32u) : reinterpret_cast<void *>(&Write32u);
    gStreamVT[15] = reading ? reinterpret_cast<void *>(&ReadText) : reinterpret_cast<void *>(&WriteText);
    gStreamVT[18] = reinterpret_cast<void *>(&GetError);
    gStreamVT[19] = reinterpret_cast<void *>(&SetMeta);
    gStreamVT[20] = reinterpret_cast<void *>(&GetMeta);
    gStream.vt = gStreamVT;
    gStream.rng.seed(seed * 7919 + 3);
    gStream.headerReads = 0;
    gStream.version = version;
    gStream.errorBits = seed % 3 == 0 ? seed * 2654435761u : 0;
    gStream.errorCalls = 0;
    gStream.maxCount = seed % 5 == 0 ? 12 : 4;
    for (auto &slot : gMetaVT) slot = nullptr;
    gMetaVT[78] = reinterpret_cast<void *>(&MetaVersion);
    gMeta.vt = gMetaVT;
    gMeta.version = version;
}

// ---- entries ----
cRZString MakeText(const std::string &s)
{
    return cRZString{GameAddress<void>(cRZString::kVTable), s, 0};
}

void RandomEntry(FamilyHistoryEntry &e, std::mt19937 &rng, unsigned maxTexts)
{
    for (auto &w : e.words) w = int16_t(Pick(rng));
    e.value10 = int32_t(Pick(rng));
    e.value14 = int32_t(Pick(rng));
    e.word18 = int16_t(Pick(rng));
    e.word1A = int16_t(Pick(rng));
    for (auto &v : e.values1C) v = int32_t(Pick(rng));
    e.value34 = int32_t(Pick(rng));
    e.value38 = int32_t(Pick(rng));
    e.word3C = int16_t(Pick(rng));
    e.word3E = int16_t(Pick(rng));
    e.flag40 = uint8_t(rng() & 1);
    e.flag41 = uint8_t(rng() & 1);
    e.flag42 = uint8_t(rng() & 1);
    e.word50 = int16_t(Pick(rng));
    const unsigned texts = maxTexts ? rng() % (maxTexts + 1) : 0;
    for (unsigned i = 0; i < texts; ++i) {
        e.texts.push_back(FamilyHistoryText(MakeText(RandomText(rng))));
        e.texts.back().value.mRefCount = rng() % 4; // copies reset it, assignment keeps it
    }
    if (rng() % 3 == 0) e.texts.reserve(e.texts.size() + rng() % 3);
}

void Capture(Blob &b, const FamilyHistoryEntry &e)
{
    for (auto w : e.words) b.push_back(uint16_t(w));
    b.insert(b.end(), {uint32_t(e.value10), uint32_t(e.value14), uint16_t(e.word18), uint16_t(e.word1A)});
    for (auto v : e.values1C) b.push_back(uint32_t(v));
    b.insert(b.end(), {uint32_t(e.value34), uint32_t(e.value38), uint16_t(e.word3C), uint16_t(e.word3E)});
    uint8_t flags[3];
    std::memcpy(flags, &e.flag40, 3); // raw bytes: the original stores 0/1
    b.insert(b.end(), {flags[0], flags[1], flags[2], uint16_t(e.word50)});
    b.insert(b.end(), {uint32_t(e.texts.size()), uint32_t(e.texts.capacity())});
    for (const auto &t : e.texts) {
        b.push_back(t.value.mVTable == GameAddress<void>(cRZString::kVTable) ? 1u : 0u);
        b.push_back(t.value.mRefCount);
        b.push_back(TextCode(t.value.mString));
    }
}

template <typename V> void Capture(Blob &b, const V &v)
{
    b.insert(b.end(), {0xeeee0000u, uint32_t(v.size()), uint32_t(v.capacity())});
    for (const auto &e : v) Capture(b, static_cast<const FamilyHistoryEntry &>(e));
}

// ---- routes (docs/standalone-shape.md) ----
// Route 0: the original side (an object in the game's shape with the game's vtables, the original RVA called). Route 1: our
// object from our constructor, the slot called through our pristine vtable. Route 2: an object in the game's shape (what the
// adapters serve when the Create hook is off or the game calls an old entry point), the adapter called with an arity-matched
// trap in the slot it forwards to, so an adapter that dispatched through the vtable is caught. Every object carries copies of
// its route's own vtables (fakes patched over the copies); only the vtable words are normalised (VtCode).
enum : uint32_t { evTrap = 0x7eaf };
int gRoute = 0;
unsigned gTrapHits = 0;
constexpr unsigned kFaceSlots[2] = {11, 12};
constexpr uint32_t kGameVt[2] = {cTSFamilyHistory::kOriginalVTable, cTSFamilyHistory::kOriginalPersistVTable};
// Stack arguments per slot (a trap must return with the slot's `ret N`).
constexpr uint8_t kArity[2][12] = {{2, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1}, {2, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0}};
void *const *gOurs[2];
void *gCopy[2][12];

void *const *GameVt(unsigned face) { return GameAddress<void *const>(kGameVt[face]); }

// Our two vtables, read from an object our constructor built (then destroyed).
void LearnOurs()
{
    if (gOurs[0]) return;
    alignas(8) static uint8_t scratch[sizeof(cTSFamilyHistory)];
    auto *h = ::new (scratch) cTSFamilyHistory;
    for (unsigned f = 0; f < 2; ++f) gOurs[f] = reinterpret_cast<void *const *const *>(scratch)[f];
    h->cTSFamilyHistory::~cTSFamilyHistory();
}

// A vtable word as a code: the installed copy of face f, the class vtable of face f that the route's code stores (the game's
// on the original side, ours on both other routes: the destructor's stores), or the raw value.
uint32_t VtCode(unsigned face, const void *word)
{
    if (word == gCopy[face]) return 0x7c00 + face;
    if (word == (gRoute == 0 ? GameVt(face) : gOurs[face])) return 0x7d00 + face;
    return uint32_t(reinterpret_cast<uintptr_t>(word));
}

template <size_t> using Word = uint32_t;
template <class Seq> struct TrapGen;
template <size_t... I> struct TrapGen<std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(void *, void *, Word<I>...)
    {
        ++gTrapHits;
        Ev({evTrap});
        return 0;
    }
};
void *TrapFor(unsigned arity)
{
    switch (arity) {
    case 0: return reinterpret_cast<void *>(&TrapGen<std::make_index_sequence<0>>::Fn);
    case 1: return reinterpret_cast<void *>(&TrapGen<std::make_index_sequence<1>>::Fn);
    default: return reinterpret_cast<void *>(&TrapGen<std::make_index_sequence<2>>::Fn);
    }
}

// The slot of face `face` that holds the function at `rva` in the game's vtable.
unsigned SlotOf(unsigned face, uint32_t rva)
{
    for (unsigned s = 0; s < kFaceSlots[face]; ++s)
        if (GameVt(face)[s] == GameAddress<void>(rva)) return s;
    return ~0u;
}

// A history object as the creator leaves it (built the route's way), with random entries.
struct History {
    alignas(8) uint8_t raw[sizeof(cTSFamilyHistory)];
    cTSFamilyHistory *Get() { return reinterpret_cast<cTSFamilyHistory *>(raw); }
    void *Face(unsigned face) { return raw + 4 * face; }
    void InstallVtables()
    {
        LearnOurs();
        auto **words = reinterpret_cast<void **>(raw);
        for (unsigned f = 0; f < 2; ++f) {
            void *const *own = gRoute == 1 ? gOurs[f] : GameVt(f);
            for (unsigned s = 0; s < kFaceSlots[f]; ++s) gCopy[f][s] = own[s];
            words[f] = gCopy[f];
        }
    }
    void Build(std::mt19937 &rng, unsigned count, unsigned maxTexts)
    {
        std::memset(raw, 0, sizeof(raw));
        if (gRoute == 1) ::new (raw) cTSFamilyHistory;
        InstallVtables();
        auto *h = Get();
        h->mEntries.reserve(count + rng() % 3);
        for (unsigned i = 0; i < count; ++i) {
            FamilyHistoryEntry e;
            RandomEntry(e, rng, maxTexts);
            h->mEntries.push_back(e);
            for (auto &t : h->mEntries.back().texts) t.value.mRefCount = rng() % 4; // the copy reset them
        }
        h->mRefCount = rng() % 4;
    }
    void Destroy() { Get()->mEntries.~FamilyHistoryEntryVector(); }
    void Words(Blob &b) { b.insert(b.end(), {VtCode(0, *reinterpret_cast<void **>(raw)), VtCode(1, *reinterpret_cast<void **>(raw + 4))}); }
};

// Runs the function at `rva` (a slot of face `face`) the route's way on `h`: the original RVA, our vtable's slot, or the
// adapter (a __fastcall (self, edx, args): the same ABI as the __thiscall slot) with a trap in the slot.
template <class Fn, class... A> auto Call(difftest::Context &ctx, History &h, uint32_t rva, unsigned face, void *adapter, A... args)
{
    void *self = h.Face(face);
    if (gRoute == 0) return ctx.Original<Fn>(rva)(self, args...);
    unsigned slot = SlotOf(face, rva);
    if (slot == ~0u) ctx.Fail("rva %x is not in face %u's vtable", rva, face), slot = 0;
    if (gRoute == 1) return reinterpret_cast<Fn>(gOurs[face][slot])(self, args...);
    gCopy[face][slot] = TrapFor(kArity[face][slot]);
    return reinterpret_cast<Fn>(adapter)(self, args...);
}
#define ADAPTER(name) reinterpret_cast<void *>(&cTSFamilyHistoryAdapters::name)

void Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differ at word %u (sizes %u/%u): %08x vs %08x", what, seed, unsigned(i), unsigned(a.size()),
             unsigned(b.size()), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
}

// Runs `run(blob)` once per route (0, 1, 2) and compares route 0 with each of the others; a trap hit fails.
template <class Run> void Routes(difftest::Context &ctx, const char *what, uint32_t seed, Run run)
{
    Blob blobs[3];
    for (int route = 0; route < 3; ++route) {
        gRoute = route;
        gTrapHits = 0;
        gEv.clear();
        run(blobs[route]);
        if (gTrapHits) ctx.Fail("%s seed %u route %d: %u trap hits", what, seed, route, gTrapHits);
    }
    gRoute = 0;
    char name[64];
    for (int route = 1; route < 3; ++route) {
        std::snprintf(name, sizeof(name), "%s (route %d)", what, route);
        Same(ctx, blobs[0], blobs[route], name, seed);
    }
}

using ReadFn = uint8_t(__thiscall *)(void *, void *);

} // namespace

DIFF_TEST(FamilyHistory_Read, 0x49fce0)
{
    AllocWorld world;
    const uint32_t versions[] = {0, 1, 0x28, 0x29, 0x4d, 0x4e, 0x55, 0x56, 0xffffffff};
    for (uint32_t seed = 0; seed < 60; ++seed) {
        for (uint32_t version : versions) {
            Routes(ctx, "Read", seed * 1000 + version, [&](Blob &b) {
                std::mt19937 rng(seed * 131 + version);
                History h;
                h.Build(rng, rng() % 5, 3);
                ResetStream(seed, version, true);
                gEv.clear();
                const uint8_t r = Call<ReadFn>(ctx, h, 0x49fce0, 0, ADAPTER(Read), &gStream);
                b = gEv;
                b.push_back(r);
                Capture(b, h.Get()->mEntries);
                h.Words(b);
                h.Destroy();
            });
        }
    }
}

DIFF_TEST(FamilyHistory_EntryRead, 0x49f650)
{
    AllocWorld world;
    const uint32_t versions[] = {0, 1, 0x28, 0x29, 0x4d, 0x4e, 0x55, 0xffffffff};
    for (uint32_t seed = 0; seed < 80; ++seed) {
        for (uint32_t version : versions) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 rng(seed * 17 + version);
                FamilyHistoryEntry e;
                RandomEntry(e, rng, 4);
                ResetStream(seed, version, true);
                gStream.headerReads = 3;
                gEv.clear();
                const uint8_t r = side == 0 ? ctx.Original<ReadFn>(0x49f650)(&e, &gStream) : e.Read(&gStream);
                blobs[side] = gEv;
                blobs[side].push_back(r);
                Capture(blobs[side], e);
            }
            Same(ctx, blobs[0], blobs[1], "EntryRead", seed * 1000 + version);
        }
    }
}

DIFF_TEST(FamilyHistory_Write, 0x4a1240)
{
    AllocWorld world;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Routes(ctx, "Write", seed, [&](Blob &b) {
            std::mt19937 rng(seed);
            History h;
            h.Build(rng, rng() % 6, 3);
            ResetStream(seed, 0, false);
            gEv.clear();
            const uint8_t r = Call<ReadFn>(ctx, h, 0x4a1240, 0, ADAPTER(Write), &gStream);
            b = gEv;
            b.push_back(r);
            Capture(b, h.Get()->mEntries);
            h.Words(b);
            h.Destroy();
        });
    }
}

DIFF_TEST(FamilyHistory_EntryWrite, 0x4a0f50)
{
    AllocWorld world;
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            FamilyHistoryEntry e;
            RandomEntry(e, rng, 4);
            // Flags with stored values other than 0/1 (written as != 0).
            if (seed % 4 == 0) std::memset(&e.flag40, int(seed % 251 + 2), 3);
            ResetStream(seed, 0, false);
            gEv.clear();
            const uint8_t r = side == 0 ? ctx.Original<ReadFn>(0x4a0f50)(&e, &gStream) : e.Write(&gStream);
            blobs[side] = gEv;
            blobs[side].push_back(r);
            if (seed % 4 == 0) std::memset(&e.flag40, 0, 3);
            Capture(blobs[side], e);
        }
        Same(ctx, blobs[0], blobs[1], "EntryWrite", seed);
    }
}

DIFF_TEST(FamilyHistory_AddEntry, 0x49d350)
{
    AllocWorld world;
    const unsigned counts[] = {0, 1, 3, 254, 255, 256, 257, 260};
    for (uint32_t seed = 0; seed < 48; ++seed) {
        const unsigned count = counts[seed % 8];
        Routes(ctx, "AddEntry", seed, [&](Blob &b) {
            std::mt19937 rng(seed);
            History h;
            h.Build(rng, count, count > 200 ? 2 : 3);
            if (seed % 3 == 0) h.Get()->mEntries.shrink_to_fit();
            FamilyHistoryEntry e;
            RandomEntry(e, rng, 3);
            gEv.clear();
            Call<void(__thiscall *)(void *, const FamilyHistoryEntry *)>(ctx, h, 0x49d350, 0, ADAPTER(AddEntry), &e);
            b = gEv;
            Capture(b, h.Get()->mEntries);
            Capture(b, e);
            h.Words(b);
            h.Destroy();
        });
    }
}

DIFF_TEST(FamilyHistory_GetEntries, 0x49eca0)
{
    AllocWorld world;
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Routes(ctx, "GetEntries", seed, [&](Blob &b) {
            std::mt19937 rng(seed);
            History h;
            h.Build(rng, rng() % 6, 3);
            FamilyHistoryEntryServiceVector out;
            const unsigned pre = rng() % 3;
            for (unsigned i = 0; i < pre; ++i) {
                FamilyHistoryEntry e;
                RandomEntry(e, rng, 2);
                out.push_back(e);
            }
            if (rng() % 2) out.reserve(out.size() + rng() % 8);
            gEv.clear();
            Call<void(__thiscall *)(void *, FamilyHistoryEntryServiceVector *)>(ctx, h, 0x49eca0, 0, ADAPTER(GetEntries), &out);
            b = gEv;
            Capture(b, out);
            Capture(b, h.Get()->mEntries);
            h.Words(b);
            h.Destroy();
        });
    }
}

// ---- reference counting, interfaces, destruction ----
namespace {
uint32_t __fastcall FakeAddRef(cTSFamilyHistory *self, void *)
{
    Ev({evAddRef});
    return ++self->mRefCount;
}
void *__fastcall FakeDelete(cTSFamilyHistory *, void *, uint32_t flags)
{
    Ev({evDelete, flags});
    return nullptr;
}
// Patches the recording AddRef and deleting destructor over the object's own primary vtable copy.
void UseFakes(bool addRef = true)
{
    if (addRef) gCopy[0][1] = reinterpret_cast<void *>(&FakeAddRef);
    gCopy[0][10] = reinterpret_cast<void *>(&FakeDelete);
}
} // namespace

DIFF_TEST(FamilyHistory_QueryInterface, 0x49f5f0)
{
    const uint32_t ids[] = {0, 1, 2, 0x7ab5bcbc, 0x7ab5bcbd, 0x7ab5bcbb, 0x456b8f1d, 0xe4fda3d4, 0xe4fda3d5, 0xffffffff};
    for (uint32_t id : ids) {
        Routes(ctx, "QueryInterface", id, [&](Blob &b) {
            std::mt19937 rng(id);
            History h;
            h.Build(rng, 0, 0);
            UseFakes();
            void *out = reinterpret_cast<void *>(0x1234);
            gEv.clear();
            const uint8_t r =
                Call<uint8_t(__thiscall *)(void *, uint32_t, void **)>(ctx, h, 0x49f5f0, 0, ADAPTER(QueryInterface), id, &out);
            b = gEv;
            b.push_back(r);
            b.push_back(out == reinterpret_cast<void *>(0x1234) ? 0xdead : uint32_t(reinterpret_cast<uint8_t *>(out) - h.raw));
            b.push_back(h.Get()->mRefCount);
            h.Words(b);
            h.Destroy();
        });
    }
}

DIFF_TEST(FamilyHistory_Release, 0x49feb0)
{
    for (uint32_t refs = 0; refs < 6; ++refs) {
        Routes(ctx, "Release", refs, [&](Blob &b) {
            std::mt19937 rng(refs);
            History h;
            h.Build(rng, 0, 0);
            UseFakes();
            h.Get()->mRefCount = refs == 5 ? 0xffffffffu : refs;
            gEv.clear();
            const uint32_t r = Call<uint32_t(__thiscall *)(void *)>(ctx, h, 0x49feb0, 0, ADAPTER(Release));
            b = gEv;
            b.push_back(r);
            b.push_back(h.Get()->mRefCount);
            h.Words(b);
            h.Destroy();
        });
    }
}

DIFF_TEST(FamilyHistory_GetGZCLSID, 0x49e260)
{
    Routes(ctx, "GetGZCLSID", 0, [&](Blob &b) {
        History h;
        std::mt19937 rng(1);
        h.Build(rng, 0, 0);
        b.push_back(Call<uint32_t(__thiscall *)(void *)>(ctx, h, 0x49e260, 0, ADAPTER(GetGZCLSID)));
        h.Words(b);
        h.Destroy();
    });
}

DIFF_TEST(FamilyHistory_DeletingDestructor, 0x49ba80)
{
    AllocWorld world;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        const uint32_t flags = seed % 4;
        Routes(ctx, "DeletingDestructor", seed, [&](Blob &b) {
            std::mt19937 rng(seed);
            auto *h = static_cast<History *>(::operator new(sizeof(History)));
            h->Build(rng, rng() % 5, 3);
            // The destructor stores both vtables: seed the words with junk (the function is called directly, not through
            // the object's vtable).
            reinterpret_cast<void **>(h->raw)[0] = nullptr;
            reinterpret_cast<void **>(h->raw)[1] = nullptr;
            const uint32_t fill = rng();
            for (auto &w : h->Get()->mRecordKey) w = fill;
            gEv.clear();
            difftest::FreeRecorder rec;
            void *r = Call<void *(__thiscall *)(void *, uint32_t)>(ctx, *h, 0x49ba80, 0, ADAPTER(DeletingDestructor), flags);
            rec.Stop();
            b = gEv;
            b.push_back(r == h->raw ? 1u : 0u);
            const bool freed = rec.Freed(h);
            b.push_back(freed ? 1u : 0u);
            if (!freed) {
                auto *o = h->Get();
                h->Words(b);
                uint32_t words[3];
                std::memcpy(words, &o->mEntries, sizeof(words));
                b.insert(b.end(), {words[0], words[1], words[2], o->mRefCount});
                for (auto w : o->mRecordKey) b.push_back(w);
                ::operator delete(h);
            }
        });
    }
}

// ---- the text dump (slot 8) ----
namespace {

// The CRT calls: the original's fopen 0x83aa0f (folded with _wfopen), fprintf 0x46f6a0 and fclose 0x838ff4 (folded with
// _close and others) are redirected here with a jump; ours go through cTSFamilyHistory::sOpen / sPrint / sClose. Nothing
// is written to disk: the fake fprintf formats into a per-route buffer.
enum : uint32_t { evOpen = 0x40, evPrint, evClose, evToChar };
std::string gOut;
uint32_t gFileToken;
bool gOpenFails = false;
FILE *const kFakeFile = reinterpret_cast<FILE *>(&gFileToken);

FILE *__cdecl FakeOpen(const char *path, const char *mode)
{
    Ev({evOpen, TextCode(path), TextCode(mode)});
    return gOpenFails ? nullptr : kFakeFile;
}
int __cdecl FakePrint(FILE *file, const char *format, ...)
{
    static char buffer[32768];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Ev({evPrint, file == kFakeFile ? 1u : P(file), TextCode(format), uint32_t(n)});
    if (n > 0) gOut.append(buffer, size_t(n) < sizeof(buffer) ? size_t(n) : sizeof(buffer) - 1);
    return n;
}
int __cdecl FakeClose(FILE *file)
{
    Ev({evClose, file == kFakeFile ? 1u : P(file)});
    return 0;
}

struct CodeRedirect {
    uint8_t *site;
    uint8_t saved[5];
    CodeRedirect(difftest::Context &ctx, uint32_t rva, void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xe9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~CodeRedirect() { std::memcpy(site, saved, 5); }
};

struct FileSeam {
    decltype(cTSFamilyHistory::sOpen) open = cTSFamilyHistory::sOpen;
    decltype(cTSFamilyHistory::sPrint) print = cTSFamilyHistory::sPrint;
    decltype(cTSFamilyHistory::sClose) close = cTSFamilyHistory::sClose;
    FileSeam()
    {
        cTSFamilyHistory::sOpen = &FakeOpen;
        cTSFamilyHistory::sPrint = &FakePrint;
        cTSFamilyHistory::sClose = &FakeClose;
    }
    ~FileSeam()
    {
        cTSFamilyHistory::sOpen = open;
        cTSFamilyHistory::sPrint = print;
        cTSFamilyHistory::sClose = close;
    }
};

// The path argument: a cIGZString whose slot 5 (ToChar) answers the text and logs the call.
struct PathString {
    void **vt;
    const char *text;
};
void *gPathVT[34];
const char *__fastcall PathToChar(PathString *self, void *)
{
    Ev({evToChar});
    return self->text;
}

// Texts for the dump: printable characters (including '%'), some long enough that a row's texts pass 0x1000 bytes.
std::string DumpText(std::mt19937 &rng, bool longTexts)
{
    const unsigned lengths[] = {0, 1, 13, 14, 15, 16, 30, 100};
    std::string s(longTexts && rng() % 2 ? 700 + rng() % 900 : lengths[rng() % 8], 'x');
    for (char &c : s) c = char(' ' + rng() % 95);
    return s;
}

} // namespace

DIFF_TEST(FamilyHistory_WriteText, 0x49d9c0)
{
    AllocWorld world;
    CodeRedirect open(ctx, 0x83aa0f, reinterpret_cast<void *>(&FakeOpen));
    CodeRedirect print(ctx, 0x46f6a0, reinterpret_cast<void *>(&FakePrint));
    CodeRedirect close(ctx, 0x838ff4, reinterpret_cast<void *>(&FakeClose));
    FileSeam seam;
    for (auto &slot : gPathVT) slot = nullptr;
    gPathVT[5] = reinterpret_cast<void *>(&PathToChar);
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Routes(ctx, "WriteText", seed, [&](Blob &b) {
            std::mt19937 rng(seed);
            History h;
            const unsigned count = seed % 10 == 9 ? 12 : rng() % 5;
            h.Build(rng, count, 0);
            const bool longTexts = seed % 8 == 3;
            for (auto &e : h.Get()->mEntries) {
                const unsigned texts = rng() % (longTexts ? 8 : 4);
                for (unsigned i = 0; i < texts; ++i) e.texts.push_back(FamilyHistoryText(MakeText(DumpText(rng, longTexts))));
                // Raw flag bytes (printed unsigned: the original zero-extends them).
                if (rng() % 3 == 0) std::memset(&e.flag40, int(rng() % 256), 1);
                if (rng() % 3 == 0) std::memset(&e.flag41, int(rng() % 256), 1);
                if (rng() % 3 == 0) std::memset(&e.flag42, int(rng() % 256), 1);
            }
            const std::string pathText = "C:\\history\\" + std::to_string(seed) + ".txt";
            PathString path{gPathVT, pathText.c_str()};
            gOpenFails = seed % 7 == 0;
            gOut.clear();
            gEv.clear();
            Call<void(__thiscall *)(void *, void *)>(ctx, h, 0x49d9c0, 0, ADAPTER(WriteText), &path);
            b = gEv;
            b.push_back(uint32_t(gOut.size()));
            b.push_back(TextCode(gOut));
            Capture(b, h.Get()->mEntries);
            h.Words(b);
            h.Destroy();
            if (seed == 1 && gRoute == 0 && gOut.find("Cheats Used\t\n") == std::string::npos)
                ctx.Fail("WriteText: header row not seen in the original's output");
        });
    }
    gOpenFails = false;
}

// ---- creation ----

// 0x49e1e0: operator new(0x28), the vtables, +8..+0x27 zeroed; null when the allocation fails (not reached here).
DIFF_TEST(FamilyHistory_Create, 0x49e1e0)
{
    LearnOurs();
    for (uint32_t seed = 0; seed < 4; ++seed) {
        Routes(ctx, "Create", seed, [&](Blob &b) {
            void *p = gRoute == 0 ? ctx.Original<void *(__cdecl *)()>(0x49e1e0)()
                                  : static_cast<void *>(static_cast<cTSFamilyHistory *>(cTSFamilyHistory::Create()));
            const auto *words = static_cast<const uint32_t *>(p);
            b.push_back(VtCode(0, reinterpret_cast<const void *>(words[0])));
            b.push_back(VtCode(1, reinterpret_cast<const void *>(words[1])));
            for (unsigned i = 2; i < 10; ++i) b.push_back(words[i]);
            ::operator delete(p, sizeof(cTSFamilyHistory)); // the entry vector is empty: nothing else to free
        });
    }
}

// ---- every slot through each side's own vtable ----

// The slots no other test calls: the folded AddRef and GetPersistFace, the persist face's thunks (QI 0x49f642, AddRef 0x4805dd
// folded, Release 0x49fec7) and its key accessors. Route 0 calls the game's vtable slot on the game-shaped object, routes 1 and
// 2 our vtable's slot on our object (route 2 repeats route 1: there are no adapters for these).
DIFF_TEST(FamilyHistory_Slots, 0x49f642)
{
    if (GameVt(1)[0] != GameAddress<void>(0x49f642) || GameVt(1)[2] != GameAddress<void>(0x49fec7))
        ctx.Fail("the persist face's QI / Release thunks are not 0x49f642 / 0x49fec7");
    enum Kind { kGet, kSet, kGetKey, kSetKey, kQI };
    struct Case {
        unsigned face, slot;
        Kind kind;
    };
    const Case cases[] = {
        {0, 1, kGet}, {0, 9, kGet}, {1, 0, kQI}, {1, 1, kGet}, {1, 2, kGet}, {1, 3, kGetKey}, {1, 4, kSetKey}, {1, 5, kGet},
        {1, 6, kGet}, {1, 7, kGet}, {1, 8, kSet}, {1, 9, kSet}, {1, 10, kSet}, {1, 11, kGet},
    };
    const uint32_t ids[] = {0x456b8f1d, 1, 0x7ab5bcbc, 0xe4fda3d4, 5};
    for (uint32_t seed = 0; seed < 40; ++seed) {
        for (const Case &c : cases) {
            Routes(ctx, "Slots", seed * 100 + c.face * 20 + c.slot, [&](Blob &b) {
                const int route = gRoute;
                gRoute = route == 2 ? 1 : route;
                std::mt19937 rng(seed);
                History h;
                h.Build(rng, 0, 0);
                UseFakes(false);
                h.Get()->mRefCount = seed % 4;
                for (auto &w : h.Get()->mRecordKey) w = rng();
                void *fn = (gRoute == 0 ? GameVt(c.face) : gOurs[c.face])[c.slot];
                void *self = h.Face(c.face);
                const uint32_t arg = rng();
                gEv.clear();
                uint32_t r = 0;
                switch (c.kind) {
                case kGet: {
                    void *v = reinterpret_cast<void *(__thiscall *)(void *)>(fn)(self);
                    r = v >= h.raw && v < h.raw + sizeof(h.raw) ? 0xf0000000u + uint32_t(static_cast<uint8_t *>(v) - h.raw) : P(v);
                    break;
                }
                case kSet: r = reinterpret_cast<uint8_t(__thiscall *)(void *, uint32_t)>(fn)(self, arg); break;
                case kGetKey: {
                    uint32_t key[4] = {1, 2, 3, 4};
                    void *ret = reinterpret_cast<void *(__thiscall *)(void *, uint32_t *)>(fn)(self, key);
                    r = ret == key ? 1u : 0u;
                    b.insert(b.end(), {key[0], key[1], key[2], key[3]});
                    break;
                }
                case kSetKey: {
                    const uint32_t key[4] = {arg, arg ^ 1, arg ^ 2, arg ^ 3};
                    r = reinterpret_cast<uint8_t(__thiscall *)(void *, const uint32_t *)>(fn)(self, key);
                    break;
                }
                case kQI: {
                    void *out = reinterpret_cast<void *>(0x1234);
                    r = reinterpret_cast<uint8_t(__thiscall *)(void *, uint32_t, void **)>(fn)(self, ids[seed % 5], &out);
                    b.push_back(out == reinterpret_cast<void *>(0x1234) ? 0xdead : uint32_t(static_cast<uint8_t *>(out) - h.raw));
                    break;
                }
                }
                b.insert(b.begin(), gEv.begin(), gEv.end());
                b.push_back(r);
                b.push_back(h.Get()->mRefCount);
                for (auto w : h.Get()->mRecordKey) b.push_back(w);
                h.Words(b);
                h.Destroy();
                gRoute = route;
            });
        }
    }
}
