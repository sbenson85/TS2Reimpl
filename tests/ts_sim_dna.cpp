// nTSGenetics::cTSSimDNA (12 bytes, one vtable of 18 slots): differential tests.
//
// Standalone shape (docs/standalone-shape.md): the original side's object carries the game's vtable and its code is called at
// its RVA; ours is built by our constructor (Create, or placement new on the test buffer) and every call goes through slot `n`
// of our own vtable (CallOwn), as the game's callers reach it. Fakes that patch the object's vtable (selfVT, dnaCopyVT) patch a copy
// of the side's own vtable, so the slots they leave alone are still the side's own, and the slot under test is called through
// the side's pristine vtable. The vtable word is the only byte range that differs by design: Code() maps it to "the side's own"
// (1) or "the patched copy" (2, 3), everything else is compared raw. SimDNA_Slots covers the slots no other test calls (AddRef,
// SetPropertyStore, GetPropertyStore); SimDNA_Adapters the RVA hooks' entry points on an object carrying the game's vtable with
// the forwarded slot trapped.
#include "difftest.h"
#include "free_recorder.h"
#include "ts/cTSSimDNA.h"
#include "misc/HexParse.h"
#include "game/game.h"
#include "code_patch.h"
#include "shape.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <random>
#include <vector>
#include <cstdarg>
#include <intrin.h>
#include <new>

extern "C" __declspec(thread) int _Init_thread_epoch;

namespace {
using nTSGenetics::cTSSimDNA;
using nTSGenetics::DNAStringPair;
struct Guarded { uint8_t before[16]; alignas(4) uint8_t bytes[12]; uint8_t after[16]; } work;
cTSSimDNA *Object() { return reinterpret_cast<cTSSimDNA *>(work.bytes); }
void *selfVT[18]{}, *sourceVT[18]{}, *stringVT[8]{}, *storeVT[3]{};

// ---- the sides ----
int gSide; // 0: the original's object and code, 1: ours
void *const *OurVT()
{
    static void *const *ours;
    if (!ours) {
        alignas(4) uint8_t block[sizeof(cTSSimDNA)];
        ::new (block) cTSSimDNA;
        std::memcpy(&ours, block, sizeof(ours));
    }
    return ours;
}
void *const *GameVT() { return GameAddress<void *const>(cTSSimDNA::kOriginalVTable); }
void *const *OwnVT() { return gSide ? OurVT() : GameVT(); }
// Slot `slot` of the side's own (unpatched) vtable, called with `this` = object.
template <class R, class... A> R CallOwn(const void *object, unsigned slot, A... args)
{
    return reinterpret_cast<R(__thiscall *)(const void *, A...)>(OwnVT()[slot])(object, args...);
}
void *dnaCopyVT[18]{}; // DNAExtended's patched copy (declared here for Code)
// A vtable word: 1 the side's own (and ours is not the game's), 2 / 3 the patched copies, otherwise the raw word.
uint32_t Code(const void *word)
{
    if (OurVT() != GameVT() && word == OwnVT()) return 1;
    if (word == selfVT) return 2;
    if (word == dnaCopyVT) return 3;
    return uint32_t(reinterpret_cast<uintptr_t>(word));
}
void NormalizeWord(void *object)
{
    const uint32_t code = Code(*static_cast<void **>(object));
    std::memcpy(object, &code, 4);
}
// selfVT: a copy of the side's own vtable with the slots the simple fakes replace.
void SetupSelf();
void Side(int side) { gSide = side; SetupSelf(); }
struct Fake { void **vt; uint32_t id; uint32_t calls; } source{sourceVT, 20, 0}, store{storeVT, 21, 0};
struct Text { void **vt; uint32_t id; uint32_t calls; char data[32]; } strings[8];
std::vector<uint32_t> trace;
uint32_t presentMask, pairMask, salt;
int throwAt = -1, ticks;
bool modifyPair, watchStore;
cTSSimDNA *watchedObject;
std::array<uint8_t, 12> releaseSnapshot;
uint64_t hasFalse, hasTrue, nullStrings, emptyStrings, parsedStrings, modifiedPairs, injected;
uint64_t families[3]{}, excludedChecks;

void Tick() { if (ticks++ == throwAt) throw 73; }
uint32_t TextId(const void *p) {
    if (!p) return 0;
    for (auto &text : strings) if (p == &text) return text.id;
    return 0xffffffff;
}
uint32_t __fastcall Add(cTSSimDNA *self, void *) {
    Tick(); trace.insert(trace.end(), {1, self->mRefCount});
    return ++self->mRefCount;
}
void *__fastcall Delete(cTSSimDNA *self, void *, uint8_t flags) {
    Tick(); trace.insert(trace.end(), {17, flags, self->mRefCount});
    self->mRefCount ^= 0x12345678;
    return self;
}
uint32_t __fastcall StoreRelease(Fake *self, void *) {
    trace.insert(trace.end(), {2, self->id}); ++self->calls;
    if (watchStore) { std::memcpy(releaseSnapshot.data(), watchedObject, 12); NormalizeWord(releaseSnapshot.data()); }
    return 0xfedcba98;
}
uint32_t __fastcall TextRelease(Text *self, void *) {
    trace.insert(trace.end(), {2, self->id}); ++self->calls;
    return 0x12345678;
}
uint32_t __fastcall Length(Text *self, void *) {
    Tick(); trace.insert(trace.end(), {7, self->id}); ++self->calls;
    const uint32_t length = uint32_t(std::strlen(self->data));
    if (!length) ++emptyStrings;
    return length;
}
const char *__fastcall Data(Text *self, void *) {
    Tick(); trace.insert(trace.end(), {5, self->id}); ++self->calls; ++parsedStrings;
    return self->data;
}
uint8_t __fastcall Has(Fake *self, void *, uint32_t gene) {
    Tick(); ++self->calls;
    if (gene == 4) ++excludedChecks;
    const bool present = gene < 16 ? (presentMask & (1u << gene)) != 0 : ((gene ^ salt) % 3) != 0;
    trace.insert(trace.end(), {5, self->id, gene, uint32_t(present)});
    if (present) ++hasTrue; else ++hasFalse;
    // Noncanonical true bytes catch accidental comparisons with exactly 1.
    return present ? uint8_t(0x80) : 0;
}
void *__fastcall Get(Fake *self, void *, DNAStringPair *out, uint32_t gene) {
    Tick(); ++self->calls;
    if (gene == 10) ++families[0];
    if (gene == 11) ++families[1];
    if (gene == 15) ++families[2];
    uint32_t first = gene == 10 ? 0 : gene == 11 ? 2 : gene == 15 ? 4 : 6;
    out->first = pairMask & 1 ? reinterpret_cast<cIGZString *>(&strings[first]) : nullptr;
    out->second = pairMask & 2 ? reinterpret_cast<cIGZString *>(&strings[first + 1]) : nullptr;
    if (!out->first) ++nullStrings;
    if (!out->second) ++nullStrings;
    trace.insert(trace.end(), {6, self->id, gene, TextId(out->first), TextId(out->second)});
    return out;
}
uint8_t __fastcall Set(cTSSimDNA *self, void *, uint32_t gene, DNAStringPair *pair) {
    Tick(); trace.insert(trace.end(), {4, gene, TextId(pair->first), TextId(pair->second)});
    self->mRefCount = self->mRefCount * 33 + gene;
    if (modifyPair) {
        pair->first = reinterpret_cast<cIGZString *>(&strings[7]);
        pair->second = nullptr;
        ++modifiedPairs;
    }
    return 0;
}
void SetupSelf() {
    std::memcpy(selfVT, OwnVT(), sizeof(selfVT));
    selfVT[1] = reinterpret_cast<void *>(&Add);
    selfVT[4] = reinterpret_cast<void *>(&Set);
    selfVT[17] = reinterpret_cast<void *>(&Delete);
}
void Setup() {
    Side(0);
    sourceVT[5] = reinterpret_cast<void *>(&Has);
    sourceVT[6] = reinterpret_cast<void *>(&Get);
    stringVT[2] = reinterpret_cast<void *>(&TextRelease);
    stringVT[5] = reinterpret_cast<void *>(&Data);
    stringVT[7] = reinterpret_cast<void *>(&Length);
    storeVT[2] = reinterpret_cast<void *>(&StoreRelease);
    for (uint32_t i = 0; i < 8; ++i) { strings[i].vt = stringVT; strings[i].id = 100 + i; }
    trace.reserve(12000);
}
void Reset() {
    trace.clear(); ticks = 0; source.calls = store.calls = 0;
    for (auto &text : strings) text.calls = 0;
}
struct Outcome {
    Guarded bytes;
    std::vector<uint32_t> calls;
    std::array<Text, 8> texts;
    Fake sourceState, storeState;
    int result;
};
Outcome Capture(int result) {
    Outcome value{work, trace, {}, source, store, result};
    NormalizeWord(value.bytes.bytes);
    std::memcpy(value.texts.data(), strings, sizeof(strings));
    return value;
}
// The side's object in work: ours placement-built by our constructor, then (both sides) the case's field values and the patched
// copy of the side's own vtable.
void Place(const Guarded &initial) {
    work = initial;
    if (gSide) {
        ::new (Object()) cTSSimDNA;
        std::memcpy(Object(), initial.bytes, sizeof(initial.bytes));
    }
    SetupSelf();
}
void Compare(difftest::Context &ctx, const Outcome &a, const Outcome &b, unsigned seed) {
    ++ctx.cases;
    if (a.result != b.result || std::memcmp(&a.bytes, &b.bytes, sizeof(work)) || a.calls != b.calls ||
        std::memcmp(a.texts.data(), b.texts.data(), sizeof(strings)) ||
        std::memcmp(&a.sourceState, &b.sourceState, sizeof(Fake)) ||
        std::memcmp(&a.storeState, &b.storeState, sizeof(Fake)))
        ctx.Fail("SimDNA seed %u: return/object/canaries/fakes/call arguments differ", seed);
}
Guarded Initial(std::mt19937 &rng) {
    Guarded value;
    for (auto &byte : reinterpret_cast<uint8_t (&)[sizeof(Guarded)]>(value)) byte = uint8_t(rng());
    auto *object = reinterpret_cast<cTSSimDNA *>(value.bytes);
    *reinterpret_cast<void **>(object) = selfVT;
    object->mPropertyStore = &store;
    return value;
}

// Temporarily redirect only allocator entries, to observe the factory's requested
// size and seed every allocation byte identically, including its null return path.
// The code stays writable after the restore: putting back the old protection VirtualProtect reported (the first page's)
// narrows both pages when the five bytes straddle a page boundary (lane MW, tests/app_fixture.h).
struct Redirect {
    uint8_t *site; uint8_t saved[5];
    Redirect(void *entry, void *target) : site(static_cast<uint8_t *>(entry)) {
        std::memcpy(saved, site, 5);
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t relative = int32_t(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xe9; std::memcpy(site + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~Redirect() {
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
};
bool failAllocation;
size_t allocationSize;
unsigned allocations;
unsigned frees;
size_t freeSize;
std::array<uint8_t, 12> freeSnapshot;
void __cdecl Free(void *object, size_t size) {
    ++frees; freeSize = size;
    std::memcpy(freeSnapshot.data(), object, 12); NormalizeWord(freeSnapshot.data());
    ::operator delete(object);
}
void *__cdecl Allocate(size_t size) {
    allocationSize = size; ++allocations;
    return failAllocation ? nullptr : Object();
}
}

DIFF_TEST(SimDNA_Create, 0x4f9020) {
    Setup(); std::mt19937 rng(0xd0a0); // deterministic bytes before allocation
    auto original = ctx.Original<cTSSimDNA *(__cdecl *)()>(0x4f9020);
    auto newFunction = static_cast<void *(__cdecl *)(size_t)>(&::operator new);
    unsigned nonnull = 0, null = 0;
    for (unsigned i = 0; i < 200; ++i) {
        Guarded initial = Initial(rng), a, b;
        cTSSimDNA *ra, *rb; size_t sizeA, sizeB; unsigned callsA, callsB;
        failAllocation = i % 2 != 0;
        {
            Redirect gameNew(ctx.image.At(0x824632), reinterpret_cast<void *>(&Allocate));
            Redirect ourNew(reinterpret_cast<void *>(newFunction), reinterpret_cast<void *>(&Allocate));
            work = initial; allocations = 0;
            ra = original(); a = work; gSide = 0; NormalizeWord(a.bytes); sizeA = allocationSize; callsA = allocations;
            work = initial; allocations = 0;
            rb = cTSSimDNA::Create(); b = work; gSide = 1; NormalizeWord(b.bytes); sizeB = allocationSize; callsB = allocations;
        }
        ++ctx.cases;
        if (ra != rb || std::memcmp(&a, &b, sizeof(a)) || sizeA != sizeB || callsA != callsB || sizeB != 12)
            ctx.Fail("SimDNA factory allocation %u differs", i);
        failAllocation ? ++null : ++nonnull;
    }
    // Also exercise the actual CRT allocation, checking the complete twelve-byte
    // result; the deterministic allocator cases above check guards and failures.
    for (unsigned i = 0; i < 200; ++i) {
        auto *a = original(); auto *b = cTSSimDNA::Create(); ++ctx.cases;
        gSide = 0; NormalizeWord(a); gSide = 1; NormalizeWord(b);
        if (!a || !b || std::memcmp(a, b, 12)) ctx.Fail("SimDNA real factory %u differs", i);
        ::operator delete(a, 12); ::operator delete(b, 12);
    }
    std::printf("  factory: %u seeded allocations, %u null failures, 200 real allocations\n", nonnull, null);
}

DIFF_TEST(SimDNA_QueryInterface, 0x4fb6a0) {
    Setup(); std::mt19937 rng(0xd0a);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x4fb6a0);
    uint64_t accepted = 0, rejected = 0, nullOut = 0, exceptions = 0;
    for (unsigned i = 0; i < 2000; ++i) {
        auto initial = Initial(rng);
        uint32_t id = i % 4 == 0 ? 1 : i % 4 == 1 ? 0x0c7aad39 : i % 4 == 2 ? 0 : rng();
        bool noOut = i % 5 == 0;
        throwAt = i % 7 == 0 ? 0 : -1;
        void *sentinel = &source, *outA = sentinel, *outB = sentinel;
        Side(0); Place(initial); Reset(); int ra;
        try { ra = original(Object(), id, noOut ? nullptr : &outA); } catch (int e) { ra = -e; ++exceptions; }
        auto a = Capture(ra);
        Side(1); Place(initial); Reset(); int rb;
        try { rb = CallOwn<uint8_t>(Object(), 0, id, noOut ? nullptr : &outB); } catch (int e) { rb = -e; }
        auto b = Capture(rb); Compare(ctx, a, b, i);
        if (outA != outB) ctx.Fail("SimDNA query output differs %u", i);
        if (noOut) ++nullOut; else if (id == 1 || id == 0x0c7aad39) ++accepted; else ++rejected;
    }
    throwAt = -1;
    std::printf("  query: accepted %llu, rejected %llu, null output %llu, injected throws %llu\n", accepted, rejected, nullOut, exceptions);
}

DIFF_TEST(SimDNA_Release, 0x4fb6f0) {
    Setup(); std::mt19937 rng(0xd0b);
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0x4fb6f0);
    const uint32_t edges[] = {0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
    uint64_t deleted = 0, decremented = 0, exceptions = 0;
    for (unsigned i = 0; i < 2000; ++i) {
        auto initial = Initial(rng);
        auto *object = reinterpret_cast<cTSSimDNA *>(initial.bytes);
        object->mRefCount = i < 1000 ? edges[i % 8] : rng();
        throwAt = i % 7 == 0 ? 0 : -1;
        Side(0); Place(initial); Reset(); int ra;
        try { ra = int(original(Object())); } catch (int e) { ra = -e; ++exceptions; }
        auto a = Capture(ra);
        Side(1); Place(initial); Reset(); int rb;
        try { rb = int(CallOwn<uint32_t>(Object(), 2)); } catch (int e) { rb = -e; }
        Compare(ctx, a, Capture(rb), i);
        object->mRefCount < 2 ? ++deleted : ++decremented;
    }
    throwAt = -1;
    std::printf("  release: deletion %llu, decrement %llu, injected throws %llu\n", deleted, decremented, exceptions);
}

DIFF_TEST(SimDNA_DeletingDestructor, 0x4f5e40) {
    Setup(); std::mt19937 rng(0xd0c); throwAt = -1;
    auto original = ctx.Original<cTSSimDNA *(__thiscall *)(void *, uint8_t)>(0x4f5e40);
    uint64_t retained = 0, freed = 0, stores = 0, noStore = 0;
    for (unsigned i = 0; i < 512; ++i) {
        uint8_t flags = uint8_t(i);
        auto initial = Initial(rng);
        auto *initialObject = reinterpret_cast<cTSSimDNA *>(initial.bytes);
        if (i & 256) { initialObject->mPropertyStore = nullptr; ++noStore; } else ++stores;
        if (!(flags & 1)) {
            watchStore = true;
            watchedObject = Object();
            Side(0); Place(initial); Reset(); auto *ra = original(Object(), flags); auto a = Capture(ra == Object());
            auto snapshotA = releaseSnapshot;
            Side(1); Place(initial); Reset(); auto *rb = CallOwn<cTSSimDNA *>(Object(), 17, uint32_t(flags)); auto b = Capture(rb == Object());
            Compare(ctx, a, b, i);
            if (snapshotA != releaseSnapshot) ctx.Fail("SimDNA destructor store observation differs %u", i);
            ++retained;
        } else {
            // Frees are recorded across each side's call (FreeRecorder): exact, whatever reuses the block.
            watchStore = true;
            auto sizedDelete = static_cast<void (__cdecl *)(void *, size_t)>(&::operator delete);
            Redirect gameDelete(ctx.image.At(0x824624), reinterpret_cast<void *>(&Free));
            Redirect ourDelete(reinterpret_cast<void *>(sizedDelete), reinterpret_cast<void *>(&Free));
            auto *a = static_cast<cTSSimDNA *>(::operator new(12)); std::memcpy(a, initial.bytes, 12);
            watchedObject = a; frees = 0;
            Side(0); Reset();
            difftest::FreeRecorder recA;
            auto *ra = original(a, flags);
            recA.Stop();
            bool liveA = !recA.Freed(a);
            auto snapshotA = releaseSnapshot, freeA = freeSnapshot;
            unsigned freesA = frees; size_t sizeA = freeSize;
            auto callsA = trace; auto stateA = store;
            auto *b = cTSSimDNA::Create(); std::memcpy(b, initial.bytes, 12);
            watchedObject = b; frees = 0;
            Side(1); Reset();
            difftest::FreeRecorder recB;
            auto *rb = CallOwn<cTSSimDNA *>(b, 17, uint32_t(flags));
            recB.Stop();
            bool liveB = !recB.Freed(b);
            ++ctx.cases;
            if (ra != a || rb != b || liveA || liveB || callsA != trace || std::memcmp(&stateA, &store, sizeof(Fake)) ||
                snapshotA != releaseSnapshot || freeA != freeSnapshot || freesA != frees || frees != 1 || sizeA != freeSize || freeSize != 12)
                ctx.Fail("SimDNA deleting destructor %u return/free/store differ", i);
            if (liveA) ::operator delete(a, 12);
            if (liveB) ::operator delete(b, 12);
            ++freed;
        }
    }
    watchStore = false;
    std::printf("  destructor: retained %llu, freed %llu, store %llu, null store %llu\n", retained, freed, stores, noStore);
}

DIFF_TEST(SimDNA_CopyFrom, 0x4f6aa0) {
    Setup(); std::mt19937 rng(0xd0d);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x4f6aa0);
    const char *counts[] = {"", "0", "1", "2", "3", "  +4tail", "nonnumeric", "0002"};
    uint64_t nullSource = 0, normal = 0;
    for (unsigned i = 0; i < 3000; ++i) {
        auto initial = Initial(rng);
        presentMask = i < 400 ? 0xffff : rng() & 0xffff;
        pairMask = i % 4; salt = rng(); modifyPair = i % 3 == 0;
        for (unsigned j = 0; j < 8; ++j) std::strcpy(strings[j].data, counts[(i + j * 3) % 8]);
        void *argument = i % 19 == 0 ? nullptr : &source;
        throwAt = i % 3 == 1 ? int(rng() % 150) : -1;
        Side(0); Place(initial); Reset(); int ra;
        try { ra = original(Object(), argument); } catch (int e) { ra = -e; ++injected; }
        auto a = Capture(ra);
        Side(1); Place(initial); Reset(); int rb;
        try { rb = CallOwn<uint8_t>(Object(), 3, argument); } catch (int e) { rb = -e; }
        Compare(ctx, a, Capture(rb), i);
        argument ? ++normal : ++nullSource;
    }
    // Negative text is a reachable bug: abort with a fake before the enormous
    // unsigned loop completes. This distinguishes unsigned max from signed max.
    for (unsigned family = 0; family < 3; ++family) {
        auto initial = Initial(rng);
        const unsigned gene = family == 0 ? 10 : family == 1 ? 11 : 15;
        presentMask = 1u << gene; pairMask = 3; modifyPair = false; salt = 0;
        for (auto &text : strings) std::strcpy(text.data, "0");
        std::strcpy(strings[family * 2].data, "-1");
        throwAt = 35;
        Side(0); Place(initial); Reset(); int ra;
        try { ra = original(Object(), &source); } catch (int e) { ra = -e; ++injected; }
        auto a = Capture(ra);
        Side(1); Place(initial); Reset(); int rb;
        try { rb = CallOwn<uint8_t>(Object(), 3, static_cast<void *>(&source)); } catch (int e) { rb = -e; }
        Compare(ctx, a, Capture(rb), 3000 + family);
    }
    throwAt = -1;
    if (excludedChecks || !hasFalse || !hasTrue || !nullStrings || !emptyStrings || !parsedStrings || !modifiedPairs || !injected)
        ctx.Fail("SimDNA missing copy branch coverage or queried excluded gene 4");
    for (auto family : families) if (!family) ctx.Fail("SimDNA missing indexed family coverage");
    std::printf("  copy: null source %llu, nonnull %llu, has false/true %llu/%llu, null/empty/parsed strings %llu/%llu/%llu, modified pairs %llu, injected throws %llu, family gets %llu/%llu/%llu\n",
        nullSource, normal, hasFalse, hasTrue, nullStrings, emptyStrings, parsedStrings, modifiedPairs, injected, families[0], families[1], families[2]);
}

namespace DNAExtended {
using namespace nTSGenetics;
struct FakeText { void **vt; uint32_t id, calls; char data[96]; };
struct FakeObject { void **vt; uint32_t id, calls; };
struct World {
    Guarded dna;
    FakeObject appearance, singleton, policy, allocator;
    FakeObject property, alternateProperty;
    FakeText texts[40];
    DNAResourceKey keys[6];
    DNAStringFloat value;
    DNAWeightedGene weighted[3];
    cRZString stringValues[3], additionalValues[3];
    DNAWeightedVector weightedView;
    DNAStringVector stringsView, additionalView;
};
World *world;
void *dnaPatch[18]{}, *appearanceVT[66]{}, *textVT[34]{}, *singletonVT[28]{}, *policyVT[10]{}, *allocatorVT[5]{};
std::vector<uint32_t> events;
int failure = -1, calls;
uint32_t seed, typeQueries;
bool applying;
bool writingGenes, memorySpy, forceShort;
enum class Profile { Capture, Apply, Properties, GetVectors, SetVectors, Insert };
Profile profile;
unsigned vectorMethod;
bool selectedSecond;
cIGZString *pendingWeight;
bool mutateProperty;
void *alternatePolicyVT[10]{};
int nullNewAt = -1;
unsigned objectNews;
uint64_t exceptions, captureSpecial, captureNormal, captureBits[9]{}, applyNull, applySpecial, applyNormal;
uint64_t applyPresent[8]{}, applyMissing[8]{}, policyYes, policyNo, vectorYes[3]{}, vectorNo[3]{};
uint64_t weights[4]{};
struct Block { void *pointer; uint32_t id, size; bool stringObject, freed; unsigned elementSize; } blocks[128];
unsigned blockCount;
void Tick() { if (calls++ == failure) throw 83; }
void Emit(std::initializer_list<uint32_t> values) { events.insert(events.end(), values); }
void Bytes(const void *data, size_t size) {
    auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) events.push_back(bytes[i]);
}
uint32_t Id(const void *pointer) {
    if (!pointer) return 0;
    for (unsigned i = blockCount; i; --i) if (blocks[i - 1].pointer == pointer) return blocks[i - 1].id;
    uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    for (unsigned i = blockCount; i; --i) {
        auto &block = blocks[i - 1]; uintptr_t start = reinterpret_cast<uintptr_t>(block.pointer);
        if (!block.freed && address > start && address < start + block.size) return block.id + uint32_t(address - start);
    }
    uintptr_t value = reinterpret_cast<uintptr_t>(pointer), base = reinterpret_cast<uintptr_t>(world);
    if (value >= base && value < base + sizeof(World)) return 0x100000 + uint32_t(value - base);
    return 0xffffffff;
}
Block &Track(void *pointer, uint32_t size, bool stringObject = false) {
    if (memorySpy)
        for (unsigned i = blockCount; i; --i) if (blocks[i - 1].pointer == pointer && !blocks[i - 1].freed) {
            blocks[i - 1].stringObject |= stringObject; return blocks[i - 1];
        }
    if (blockCount >= 128) std::abort();
    auto &block = blocks[blockCount];
    block = {pointer, 0x200000 + blockCount * 0x10000, size, stringObject, false, 0};
    ++blockCount;
    return block;
}
void StringBytes(const cRZString &text) {
    Emit({uint32_t(reinterpret_cast<uintptr_t>(text.mVTable)), text.mRefCount, uint32_t(text.mString.size()), uint32_t(text.mString.capacity())});
    if (text.mString.capacity() <= 15) Bytes(text.mString.data(), 16);
    else Bytes(text.mString.data(), text.mString.size() + 1);
}
void TextValue(const cIGZString *text) {
    if (!text) { Emit({0}); return; }
    Emit({Id(text)});
    for (auto &fake : world->texts) if (text == reinterpret_cast<const cIGZString *>(&fake)) {
        Bytes(fake.data, sizeof(fake.data)); return;
    }
    StringBytes(*reinterpret_cast<const cRZString *>(text));
}
cTSSimDNA *DNA() { return reinterpret_cast<cTSSimDNA *>(world->dna.bytes); }
uint32_t __fastcall FreshAdd(cRZString *text, void *) {
    Track(text, 32, true);
    Emit({101, Id(text)}); StringBytes(*text); Tick();
    return text->AddRef();
}
uint32_t __fastcall FreshRelease(cRZString *text, void *) {
    Emit({102, Id(text)}); StringBytes(*text);
    if (text->mRefCount <= 1)
        for (unsigned i = blockCount; i; --i) if (blocks[i - 1].pointer == text && !blocks[i - 1].freed) { blocks[i - 1].freed = true; break; }
    return text->Release();
}
uint32_t __fastcall TextRelease(FakeText *text, void *) { Emit({2, text->id}); ++text->calls; return 0; }
const char *__fastcall TextData(FakeText *text, void *) {
    Emit({5, text->id}); Tick(); ++text->calls;
    if (mutateProperty) DNA()->mPropertyStore = &world->alternateProperty;
    if (reinterpret_cast<cIGZString *>(text) == pendingWeight) {
        const int32_t value = std::atoi(text->data);
        ++weights[value < 0 ? 1 : value > 255 ? 3 : 2]; pendingWeight = nullptr;
    }
    return text->data;
}
const char *__fastcall TextBuffer(FakeText *text, void *) { Emit({6, text->id}); Tick(); ++text->calls; return text->data; }
uint32_t __fastcall TextLength(FakeText *text, void *) { Emit({7, text->id}); Tick(); ++text->calls; return uint32_t(std::strlen(text->data)); }
void *__fastcall GetGene(cTSSimDNA *, void *, DNAStringPair *out, uint32_t gene) {
    Emit({6, gene});
    if (profile == Profile::Capture && typeQueries == 2) {
        int bit = gene == 3 ? 0 : gene == 14 ? 1 : gene == 7 ? 2 : gene == 8 ? 3 : gene == 12 ? 4 : gene == 9 ? 5 : -1;
        if (bit >= 0) ++captureBits[bit];
    }
    Tick();
    const bool required = gene == 1 || gene == 3 || gene == 6;
    unsigned text = gene < 16 ? gene * 2 : 4 + (gene % 12) * 2;
    out->first = required || writingGenes || (seed & (1u << (gene % 16))) ? reinterpret_cast<cIGZString *>(&world->texts[text]) : nullptr;
    out->second = writingGenes || (seed & 0x800) ? reinterpret_cast<cIGZString *>(&world->texts[text + 1]) : nullptr;
    if (!applying) {
        // Capture must release old pointers, including both members of each pair.
        out->first = reinterpret_cast<cIGZString *>(&world->texts[text]);
        out->second = writingGenes || (seed & 1) ? reinterpret_cast<cIGZString *>(&world->texts[text + 1]) : nullptr;
    }
    Emit({Id(out->first), Id(out->second)});
    if (profile == Profile::GetVectors && vectorMethod == 0 && gene >= 0xc00 && gene < 0x1000) {
        pendingWeight = selectedSecond ? out->second : out->first;
        if (!pendingWeight) ++weights[0];
    }
    if (profile == Profile::Apply) {
        int index = gene == 14 ? 0 : gene == 7 ? 1 : gene == 8 ? 2 : gene == 12 ? 3 : gene == 13 ? 4 : gene == 9 ? 5 : -1;
        if (index >= 0) out->first ? ++applyPresent[index] : ++applyMissing[index];
    }
    return out;
}
uint8_t __fastcall SetGene(cTSSimDNA *dna, void *, uint32_t gene, DNAStringPair *pair) {
    Emit({4, gene}); TextValue(pair->first); TextValue(pair->second); Tick();
    dna->mRefCount = dna->mRefCount * 31 + gene;
    return 0;
}
int32_t __fastcall Type(FakeObject *self, void *) {
    ++self->calls;
    uint32_t type = applying ? seed % 4 == 0 ? 1 : 2 : (seed >> (typeQueries++ ? 10 : 9)) & 1 ? 1 : 2;
    Emit({24, self->id, type}); Tick();
    if (profile == Profile::Capture) {
        if (typeQueries == 1 && type == 1) ++captureSpecial;
        if (typeQueries == 2 && type != 1) ++captureNormal;
    }
    return int32_t(type);
}
template<unsigned Slot, unsigned KeyIndex> void *__fastcall Key(FakeObject *self, void *) {
    Emit({Slot, self->id}); Tick(); ++self->calls; return &world->keys[KeyIndex];
}
void *__fastcall PairedKey(FakeObject *self, void *, uint32_t which) {
    Emit({32, self->id, which}); Tick(); ++self->calls; return &world->keys[which ? 4 : 5];
}
uint32_t __fastcall Selection(FakeObject *self, void *) {
    Emit({23, self->id}); Tick(); ++self->calls; return seed ^ 0xabcdef01;
}
int32_t __fastcall SelectKey(FakeObject *self, void *, void *dna, cIGZString **out) {
    Emit({23, self->id, Id(dna)}); Tick(); ++self->calls;
    *out = seed & 2 ? reinterpret_cast<cIGZString *>(&world->texts[38]) : nullptr;
    if (seed & 16) world->policy.vt = alternatePolicyVT;
    Emit({Id(*out)}); return seed & 2 ? 2 : 1;
}
void *__fastcall Policy(FakeObject *self, void *) { Emit({27, self->id}); Tick(); ++self->calls; return &world->policy; }
uint8_t __fastcall Override(FakeObject *self, void *, uint32_t matched) {
    Emit({9, self->id, matched}); Tick(); ++self->calls;
    bool override = matched && (seed & 4);
    override ? ++policyYes : ++policyNo;
    return override ? 0x80 : 0;
}
uint8_t __fastcall UnexpectedOverride(FakeObject *self, void *, uint32_t matched) {
    Emit({999, self->id, matched}); Tick(); ++self->calls; return 0;
}
void *__fastcall FloatValue(FakeObject *self, void *) { Emit({38, self->id}); Tick(); ++self->calls; return &world->value; }
uint32_t __fastcall Numeric(FakeObject *self, void *) { Emit({33, self->id}); Tick(); ++self->calls; return seed ^ 0x98765432; }
template<unsigned Slot, unsigned Which> void *__fastcall VectorValue(FakeObject *self, void *) {
    Emit({Slot, self->id}); Tick(); ++self->calls;
    return Which == 0 ? static_cast<void *>(&world->weightedView) : Which == 1 ? static_cast<void *>(&world->stringsView) : &world->additionalView;
}
template<unsigned Slot> void __fastcall PutKey(FakeObject *self, void *, const DNAResourceKey *key) {
    Emit({Slot, self->id}); Bytes(key, 16); Tick(); ++self->calls;
}
void __fastcall PutPaired(FakeObject *self, void *, uint32_t which, const DNAResourceKey *key) {
    Emit({59, self->id, which}); Bytes(key, 16); Tick(); ++self->calls;
}
void __fastcall PutNumber(FakeObject *self, void *, uint32_t value) { Emit({60, self->id, value}); Tick(); ++self->calls; }
void __fastcall PutFloat(FakeObject *self, void *, DNAStringFloat *value) {
    Emit({65, self->id}); StringBytes(value->text); Bytes(&value->value, 4); Tick(); ++self->calls;
}
template<class T> void VectorBytes(const DNAVector<T> &values) {
    Emit({values.Size(), values.begin ? uint32_t(values.capacity - values.begin) : 0});
    for (auto *value = values.begin; value != values.end; ++value) {
        if constexpr (sizeof(T) == 68) { StringBytes(value->name); StringBytes(value->value); Bytes(&value->weight, 4); }
        else StringBytes(*value);
    }
}
template<unsigned Slot, class T> uint8_t __fastcall SetVector(cTSSimDNA *dna, void *, DNAVector<T> *values, uint8_t both) {
    if (profile == Profile::Capture) ++captureBits[Slot == 10 ? 6 : Slot == 12 ? 7 : 8];
    Emit({Slot, both}); VectorBytes(*values); Tick(); dna->mRefCount ^= Slot;
    return 0x80;
}
template<unsigned Slot, class T> void __fastcall PutVector(FakeObject *self, void *, DNAVector<T> *values) {
    Emit({Slot, self->id}); VectorBytes(*values); Tick(); ++self->calls;
}
void __fastcall ServiceFree(FakeObject *self, void *, void *pointer) {
    Emit({404, self->id, Id(pointer)}); ++self->calls;
    for (unsigned i = blockCount; i; --i) if (blocks[i - 1].pointer == pointer && !blocks[i - 1].freed) { blocks[i - 1].freed = true; break; }
    ::operator delete(pointer);
}
void *__fastcall ServiceAllocate(FakeObject *self, void *, uint32_t size) {
    Emit({403, self->id, size}); Tick(); ++self->calls;
    void *pointer = ::operator new(size); std::memset(pointer, 0xa5, size);
    auto &block = Track(pointer, size); block.elementSize = size % 68 == 0 ? 68 : 32;
    return pointer;
}
template<unsigned Which, class T> uint8_t FillVector(DNAVector<T> *out) {
    Emit({209 + Which}); Tick();
    unsigned count = (seed >> (Which * 2)) % 4;
    unsigned capacity = count + ((seed >> (Which + 6)) & 1);
    if (capacity) {
        out->begin = static_cast<T *>(ServiceAllocate(&world->allocator, nullptr, uint32_t(capacity * sizeof(T))));
        out->end = out->begin;
        out->capacity = out->begin + capacity;
        for (unsigned i = 0; i < count; ++i) {
            auto *value = out->end;
            if constexpr (sizeof(T) == 68) {
                std::memset(&value->name.mString, 0, 24); value->name.Construct(i & 1 ? "weighted-name-longer-than-sso" : "name");
                std::memset(&value->value.mString, 0, 24); value->value.Construct("value"); value->weight = int32_t(seed + i);
            } else { std::memset(&value->mString, 0, 24); value->Construct(i & 1 ? "string-value-longer-than-sso" : "value"); }
            ++out->end;
        }
    }
    bool result = seed & (1u << (Which + 4));
    result ? ++vectorYes[Which] : ++vectorNo[Which];
    return result ? 0x80 : 0;
}
uint8_t __fastcall GetWeighted(cTSSimDNA *, void *, DNAWeightedVector *out, uint8_t second) { Emit({9, second}); return FillVector<0>(out); }
uint8_t __fastcall GetStrings(cTSSimDNA *, void *, DNAStringVector *out) { Emit({11}); return FillVector<1>(out); }
uint8_t __fastcall GetAdditional(cTSSimDNA *, void *, DNAStringVector *out) { Emit({13}); return FillVector<2>(out); }
void __fastcall Format(const DNAResourceKey *key, void *, std::string *out) {
    Emit({0x4fdd30}); Bytes(key, 16);
    Emit({uint32_t(out->size()), uint32_t(out->capacity())});
    if (out->capacity() <= 15) Bytes(out->data(), 16);
    Tick();
    const unsigned lengths[] = {0, 7, 15, 16, 40, 123};
    unsigned length = forceShort ? 7 : lengths[key->words[0] % 6];
    void *old = out->capacity() > 15 ? out->data() : nullptr;
    out->assign(length, char('A' + (key->words[1] % 26)));
    if (old && old != out->data())
        for (unsigned i = blockCount; i; --i) if (blocks[i - 1].pointer == old && !blocks[i - 1].freed) { blocks[i - 1].freed = true; break; }
    bool tracked = false;
    for (unsigned i = 0; i < blockCount; ++i) if (blocks[i].pointer == out->data() && !blocks[i].freed) tracked = true;
    if (out->capacity() > 15 && !tracked) Track(out->data(), uint32_t(out->capacity() + 1));
}
DNAResourceKey *__cdecl Resolve(DNAResourceKey *out, uint32_t category, uint32_t selection, const DNAResourceKey *fallback) {
    Emit({0xb5e590, category, selection}); Bytes(fallback, 16); Tick();
    *out = *fallback; out->words[0] ^= selection; return out;
}
DNAResourceKey *__cdecl Parse(DNAResourceKey *out, const char *data) {
    Emit({0x4f8eb0}); Bytes(data, std::strlen(data) + 1); Tick();
    for (unsigned i = 0; i < 4; ++i) out->words[i] = uint32_t(std::strlen(data)) * (17 + i) + uint8_t(data[0]) + i;
    return out;
}
double __cdecl Number(const char *data) { Emit({0x1b0ff4}); Bytes(data, std::strlen(data) + 1); Tick(); return std::atof(data); }
void __fastcall FormatFloat(DNAResourceKey *key, void *, cRZString *text) {
    Emit({0x4fdcf0}); Bytes(key, 16); StringBytes(*text); Tick();
    text->mString.assign(seed & 1 ? "composite-value-longer-than-sso" : "value");
}
void __fastcall DestroyFloat(DNAStringFloat *value, void *) {
    Emit({0x1d99b0}); StringBytes(value->text); Bytes(&value->value, 4);
    value->text.Destruct();
}
void __fastcall DestroyWeighted(DNAWeightedVector *values, void *) {
    Emit({0x4f5cc0}); VectorBytes(*values);
    if (!values->begin) return;
    for (auto *value = values->begin; value != values->end; ++value) { value->name.Destruct(); value->value.Destruct(); }
    ServiceFree(&world->allocator, nullptr, values->begin);
    *values = {};
}
struct Slots {
    void **table; unsigned first, count; void *saved[3]; DWORD protection;
    Slots(void **table, unsigned first, std::initializer_list<void *> replacements) : table(table), first(first), count(unsigned(replacements.size())) {
        VirtualProtect(table + first, count * 4, PAGE_READWRITE, &protection);
        std::memcpy(saved, table + first, count * 4);
        unsigned i = 0; for (auto *replacement : replacements) table[first + i++] = replacement;
    }
    ~Slots() { std::memcpy(table + first, saved, count * 4); DWORD ignored; VirtualProtect(table + first, count * 4, protection, &ignored); }
};
struct Globals {
    void **cache, **allocator; int32_t *guard; void *savedCache, *savedAllocator; int32_t savedGuard;
    explicit Globals(difftest::Context &ctx) {
        cache = ctx.image.At<void *>(0x10cc288); allocator = ctx.image.At<void *>(0x10c95d4); guard = ctx.image.At<int32_t>(0x10c95d8);
        savedCache = *cache; savedAllocator = *allocator; savedGuard = *guard;
        *cache = &world->singleton; *allocator = &world->allocator; *guard = -2;
    }
    ~Globals() { *cache = savedCache; *allocator = savedAllocator; *guard = savedGuard; }
};
void Initialize(difftest::Context &ctx, World &w, uint32_t value, bool apply) {
    world = &w; seed = value; applying = apply;
    events.reserve(50000);
    std::mt19937 rng(value);
    for (auto &byte : reinterpret_cast<uint8_t (&)[sizeof(Guarded)]>(w.dna)) byte = uint8_t(rng());
    *reinterpret_cast<void **>(DNA()) = dnaCopyVT; DNA()->mPropertyStore = &w.allocator;
    dnaPatch[4] = reinterpret_cast<void *>(&SetGene); dnaPatch[6] = reinterpret_cast<void *>(&GetGene);
    dnaPatch[9] = reinterpret_cast<void *>(&GetWeighted); dnaPatch[11] = reinterpret_cast<void *>(&GetStrings); dnaPatch[13] = reinterpret_cast<void *>(&GetAdditional);
    dnaPatch[10] = reinterpret_cast<void *>(&SetVector<10, DNAWeightedGene>); dnaPatch[12] = reinterpret_cast<void *>(&SetVector<12, cRZString>); dnaPatch[14] = reinterpret_cast<void *>(&SetVector<14, cRZString>);
    textVT[2] = reinterpret_cast<void *>(&TextRelease); textVT[5] = reinterpret_cast<void *>(&TextData);
    textVT[6] = reinterpret_cast<void *>(&TextBuffer); textVT[7] = reinterpret_cast<void *>(&TextLength);
    w.appearance = {appearanceVT, 1, 0}; w.singleton = {singletonVT, 2, 0}; w.policy = {policyVT, 3, 0}; w.allocator = {allocatorVT, 4, 0};
    appearanceVT[23] = apply ? reinterpret_cast<void *>(&SelectKey) : reinterpret_cast<void *>(&Selection);
    appearanceVT[24] = reinterpret_cast<void *>(&Type);
    appearanceVT[28] = reinterpret_cast<void *>(&Key<28, 0>); appearanceVT[29] = reinterpret_cast<void *>(&Key<29, 1>);
    appearanceVT[30] = reinterpret_cast<void *>(&Key<30, 2>); appearanceVT[31] = reinterpret_cast<void *>(&Key<31, 3>); appearanceVT[32] = reinterpret_cast<void *>(&PairedKey);
    appearanceVT[33] = reinterpret_cast<void *>(&Numeric); appearanceVT[38] = reinterpret_cast<void *>(&FloatValue);
    appearanceVT[34] = reinterpret_cast<void *>(&VectorValue<34, 0>); appearanceVT[35] = reinterpret_cast<void *>(&VectorValue<35, 1>); appearanceVT[36] = reinterpret_cast<void *>(&VectorValue<36, 2>);
    appearanceVT[55] = reinterpret_cast<void *>(&PutKey<55>); appearanceVT[56] = reinterpret_cast<void *>(&PutKey<56>); appearanceVT[57] = reinterpret_cast<void *>(&PutKey<57>); appearanceVT[58] = reinterpret_cast<void *>(&PutKey<58>);
    appearanceVT[59] = reinterpret_cast<void *>(&PutPaired); appearanceVT[60] = reinterpret_cast<void *>(&PutNumber); appearanceVT[65] = reinterpret_cast<void *>(&PutFloat);
    appearanceVT[61] = reinterpret_cast<void *>(&PutVector<61, DNAWeightedGene>); appearanceVT[62] = reinterpret_cast<void *>(&PutVector<62, cRZString>); appearanceVT[63] = reinterpret_cast<void *>(&PutVector<63, cRZString>);
    singletonVT[27] = reinterpret_cast<void *>(&Policy); policyVT[9] = reinterpret_cast<void *>(&Override);
    alternatePolicyVT[9] = reinterpret_cast<void *>(&UnexpectedOverride);
    allocatorVT[3] = reinterpret_cast<void *>(&ServiceAllocate); allocatorVT[4] = reinterpret_cast<void *>(&ServiceFree);
    for (unsigned i = 0; i < 40; ++i) {
        w.texts[i] = {textVT, 100 + i, 0, {}};
        std::snprintf(w.texts[i].data, 96, "%u.%u", value % 99, i);
    }
    // Gene 9's text (index 18) is parsed as a number: give it full-range signed values on half the seeds.
    if (value & 1) std::snprintf(w.texts[18].data, 96, "%d", static_cast<int32_t>(rng()));
    for (auto &key : w.keys) for (auto &word : key.words) word = rng();
    auto initString = [&](cRZString &text, unsigned i) {
        text.mString.clear(); text.mString.shrink_to_fit();
        std::memset(text.mString.data(), 0, 16);
        text.mString.assign(value & 1 ? 80 : i % 15, char('a' + i));
        text.mVTable = ctx.image.At(cRZString::kVTable); text.mRefCount = 0;
    };
    initString(w.value.text, 4); w.value.value = float(int32_t(value)) / 11;
    for (unsigned i = 0; i < 3; ++i) {
        initString(w.weighted[i].name, i); initString(w.weighted[i].value, i + 3); w.weighted[i].weight = int32_t(value + i);
        initString(w.stringValues[i], i + 6); initString(w.additionalValues[i], i + 9);
    }
    unsigned count = value % 4;
    w.weightedView = {w.weighted, w.weighted + count, w.weighted + 3};
    w.stringsView = {w.stringValues, w.stringValues + count, w.stringValues + 3};
    w.additionalView = {w.additionalValues, w.additionalValues + count, w.additionalValues + 3};
}
void ResetWorld(const Guarded &initial, int side) {
    gSide = side;
    std::memcpy(dnaCopyVT, OwnVT(), sizeof(dnaCopyVT));
    for (unsigned i = 0; i < 18; ++i) if (dnaPatch[i]) dnaCopyVT[i] = dnaPatch[i];
    if (side) ::new (world->dna.bytes) cTSSimDNA;
    world->dna = initial; events.clear(); calls = 0; typeQueries = 0; blockCount = 0;
    world->appearance.calls = world->singleton.calls = world->policy.calls = world->allocator.calls = 0;
    world->property.calls = world->alternateProperty.calls = 0;
    for (auto &text : world->texts) text.calls = 0;
    world->policy.vt = policyVT; pendingWeight = nullptr;
}
struct Result {
    Guarded dna; FakeObject objects[6]; FakeText texts[40];
    std::vector<uint32_t> events;
    std::vector<uint32_t> heap;
    int thrown;
};
Result Finish(int thrown) {
    // Snapshot live allocations before allocating result containers, so freed
    // pointers cannot be reused by the snapshot itself.
    uint32_t heap[12000]; unsigned used = 0;
    for (unsigned i = 0; i < blockCount; ++i) {
        auto &block = blocks[i];
        heap[used++] = block.id; heap[used++] = block.freed;
        if (!block.freed) {
            if (!HeapValidate(GetProcessHeap(), 0, block.pointer)) std::abort();
            heap[used++] = block.size;
            if (block.stringObject) {
                const auto *text = static_cast<cRZString *>(block.pointer);
                heap[used++] = text->mRefCount; heap[used++] = uint32_t(text->mString.size()); heap[used++] = uint32_t(text->mString.capacity());
                for (char c : text->mString) heap[used++] = uint8_t(c);
            } else {
                // Caller-owned vector allocations must all have been freed. A
                // helper-owned replaced string buffer may be reclaimed by its fake.
                heap[used++] = 0xabcdef;
            }
        }
    }
    Result result{world->dna, {world->appearance, world->singleton, world->policy, world->allocator, world->property, world->alternateProperty}, {}, events, {}, thrown};
    NormalizeWord(result.dna.bytes);
    std::memcpy(result.texts, world->texts, sizeof(result.texts)); result.heap.assign(heap, heap + used);
    for (unsigned i = 0; i < blockCount; ++i) if (!blocks[i].freed) {
        if (blocks[i].stringObject) {
            auto *text = static_cast<cRZString *>(blocks[i].pointer); text->Destruct(); ::operator delete(text, 32);
        } else {
            // Formatting fake can replace its own previous heap buffer. Such
            // buffers are delegated helper effects, not caller leaks.
        }
    }
    return result;
}
void Compare(difftest::Context &ctx, const Result &a, const Result &b, unsigned value) {
    ++ctx.cases;
    if (a.thrown != b.thrown || std::memcmp(&a.dna, &b.dna, sizeof(Guarded)) || std::memcmp(a.objects, b.objects, sizeof(a.objects)) ||
        std::memcmp(a.texts, b.texts, sizeof(a.texts)) || a.events != b.events || a.heap != b.heap) {
        size_t i = 0; while (i < a.events.size() && i < b.events.size() && a.events[i] == b.events[i]) ++i;
        ctx.Fail("SimDNA appearance seed %u: thrown %d/%d, trace mismatch %zu (%u/%u), lengths %zu/%zu, heap %zu/%zu", value,
            a.thrown, b.thrown, i, i < a.events.size() ? a.events[i] : 0, i < b.events.size() ? b.events[i] : 0,
            a.events.size(), b.events.size(), a.heap.size(), b.heap.size());
    }
}
}

DIFF_TEST(SimDNA_CaptureAppearance, 0x4fb740) {
    using namespace DNAExtended;
    World w{};
    profile = Profile::Capture;
    auto original = ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0x4fb740);
    Redirect format(ctx.image.At(0x4fdd30), reinterpret_cast<void *>(&Format));
    Redirect resolve(ctx.image.At(0xb5e590), reinterpret_cast<void *>(&Resolve));
    Redirect destroyFloat(ctx.image.At(0x1d99b0), reinterpret_cast<void *>(&DestroyFloat));
    Slots stringSlots(ctx.image.At<void *>(cRZString::kVTable), 1, {reinterpret_cast<void *>(&FreshAdd), reinterpret_cast<void *>(&FreshRelease)});
    for (uint32_t i = 0; i < 1600; ++i) {
        uint32_t value = i * 7919 + 13;
        Initialize(ctx, w, value, false); Globals globals(ctx); auto initial = w.dna;
        uint32_t mask = i < 512 ? i : value;
        failure = i % 3 == 0 ? int(value % 55) : -1;
        int ra = 0; ResetWorld(initial, 0);
        try { original(DNA(), &w.appearance, mask); } catch (int e) { ra = e; ++exceptions; }
        auto a = Finish(ra);
        int rb = 0; ResetWorld(initial, 1);
        try { CallOwn<void>(DNA(), 15, static_cast<void *>(&w.appearance), mask); } catch (int e) { rb = e; }
        auto b = Finish(rb); Compare(ctx, a, b, i);
    }
    std::printf("  capture: special %llu, normal %llu, injected throws %llu; mask bits", captureSpecial, captureNormal, exceptions);
    for (auto count : captureBits) { std::printf(" %llu", count); if (!count) ctx.Fail("capture mask branch missing"); }
    std::printf("\n"); failure = -1;
}

DIFF_TEST(SimDNA_ApplyAppearance, 0x4fc8a0) {
    using namespace DNAExtended;
    World w{};
    profile = Profile::Apply;
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x4fc8a0);
    // 0x4f8eb0 is ours now (hexparse::ParseGuidText): the stand-in covers the image's and ours.
    shape::StandIn parse(0x4f8eb0, &hexparse::ParseGuidText, &Parse);
    Redirect number(ctx.image.At(0x1b0ff4), reinterpret_cast<void *>(&Number));
    Redirect formatFloat(ctx.image.At(0x4fdcf0), reinterpret_cast<void *>(&FormatFloat));
    Redirect destroyFloat(ctx.image.At(0x1d99b0), reinterpret_cast<void *>(&DestroyFloat));
    Redirect destroyWeighted(ctx.image.At(0x4f5cc0), reinterpret_cast<void *>(&DestroyWeighted));
    for (uint32_t i = 0; i < 1600; ++i) {
        uint32_t value = i * 7919 + 17;
        Initialize(ctx, w, value, true); Globals globals(ctx); auto initial = w.dna;
        failure = i % 3 == 0 ? int(value % 55) : -1;
        void *appearance = i % 23 == 0 ? nullptr : &w.appearance;
        int ra = 0; ResetWorld(initial, 0);
        try { original(DNA(), appearance); } catch (int e) { ra = e; ++exceptions; }
        auto a = Finish(ra);
        int rb = 0; ResetWorld(initial, 1);
        try { CallOwn<void>(DNA(), 16, appearance); } catch (int e) { rb = e; }
        auto b = Finish(rb); Compare(ctx, a, b, i);
        if (!appearance) ++applyNull; else if (value % 4 == 0) ++applySpecial; else ++applyNormal;
    }
    std::printf("  apply: null %llu, special %llu, normal %llu, policy yes/no %llu/%llu; vector success/failure", applyNull, applySpecial, applyNormal, policyYes, policyNo);
    for (unsigned i = 0; i < 3; ++i) { std::printf(" %llu/%llu", vectorYes[i], vectorNo[i]); if (!vectorYes[i] || !vectorNo[i]) ctx.Fail("apply vector branch missing"); }
    std::printf("\n"); failure = -1;
    std::printf("  apply optional gene present/missing:");
    for (unsigned i = 0; i < 6; ++i) { std::printf(" %llu/%llu", applyPresent[i], applyMissing[i]); if (!applyPresent[i] || !applyMissing[i]) ctx.Fail("apply optional gene branch missing"); }
    std::printf("\n");
}

namespace DNAExtended {
void *propertyVT[11]{};
unsigned propertyReads, propertyTests;
bool mutateStore;
uint64_t propertyNull, propertyReadSuccess, propertyReadFailure, propertyHasFirst, propertyHasSecond, propertyHasNeither;
uint64_t geneMissing, genePresent, getterThrows, writerThrows, reallocations, spareCapacity;
struct AnyValue { const void *vt; cIGZString *value; };
void KeyBytes(const std::string &key) { Emit({uint32_t(key.size()), uint32_t(key.capacity())}); Bytes(key.data(), 16); }
uint8_t __fastcall PropertyRead(FakeObject *self, void *, const std::string *key, AnyValue *out) {
    Emit({309, self->id}); KeyBytes(*key); Emit({uint32_t(reinterpret_cast<uintptr_t>(out->vt)), Id(out->value)}); Tick(); ++self->calls;
    bool found = seed & (1u << propertyReads);
    unsigned index = propertyReads++;
    if (found || (seed & 8)) {
        if (out->value) TextRelease(reinterpret_cast<FakeText *>(out->value), nullptr);
        out->value = reinterpret_cast<cIGZString *>(&world->texts[4 + index]);
    }
    found ? ++propertyReadSuccess : ++propertyReadFailure;
    return found ? 0x80 : 0;
}
uint8_t __fastcall PropertyTest(FakeObject *self, void *, const std::string *key) {
    Emit({310, self->id}); KeyBytes(*key); Tick(); ++self->calls;
    bool found = seed & (1u << propertyTests);
    ++propertyTests;
    if (found && propertyTests == 1) ++propertyHasFirst;
    else if (found) ++propertyHasSecond;
    else if (propertyTests == 2) ++propertyHasNeither;
    return found ? 0xff : 0;
}
void __fastcall PropertyWrite(FakeObject *self, void *, const std::string *key, AnyValue *value) {
    Emit({303, self->id}); KeyBytes(*key); Emit({uint32_t(reinterpret_cast<uintptr_t>(value->vt))}); TextValue(value->value); Tick(); ++self->calls;
}
void __fastcall TextCopy(FakeText *self, void *, const cIGZString *source) {
    Emit({17, self->id}); TextValue(source); Tick(); ++self->calls;
    const auto *text = reinterpret_cast<const cRZString *>(source);
    std::memset(self->data, 0, sizeof(self->data));
    std::memcpy(self->data, text->mString.data(), (std::min)(sizeof(self->data) - 1, text->mString.size()));
}
void __cdecl TextPrintf(FakeText *self, const char *format, ...) {
    va_list arguments; va_start(arguments, format); int32_t value = va_arg(arguments, int32_t); va_end(arguments);
    Emit({31, self->id, uint32_t(value)}); Bytes(format, std::strlen(format) + 1); Tick(); ++self->calls;
    std::snprintf(self->data, sizeof(self->data), format, value);
}
uint8_t __fastcall HasGene(cTSSimDNA *, void *, uint32_t gene) {
    Emit({5, gene}); Tick(); bool present = seed & 1;
    present ? ++genePresent : ++geneMissing; return present ? 0x80 : 0;
}
void *__cdecl New(size_t size) {
    Emit({500, uint32_t(size)});
    if (size == 32 && int(objectNews++) == nullNewAt) { Emit({0}); return nullptr; }
    void *pointer = std::malloc(size); if (!pointer) throw std::bad_alloc();
    std::memset(pointer, 0xa5, size);
    auto &block = blocks[blockCount]; block = {pointer, 0x200000 + blockCount * 0x10000, uint32_t(size), false, false, 0};
    if (++blockCount >= 128) std::abort();
    Emit({block.id}); return pointer;
}
void DeleteMemory(void *pointer, size_t size) {
    Emit({501, Id(pointer), uint32_t(size)});
    for (unsigned i = blockCount; i; --i) if (blocks[i - 1].pointer == pointer && !blocks[i - 1].freed) { blocks[i - 1].freed = true; break; }
    std::free(pointer);
}
void __cdecl SizedDelete(void *pointer, size_t size) { DeleteMemory(pointer, size); }
void __cdecl UnsizedDelete(void *pointer) { DeleteMemory(pointer, 0); }
struct MemorySpy {
    Redirect gameNew, ourNew, gameSized, ourSized, gameUnsized, ourUnsized;
    explicit MemorySpy(difftest::Context &ctx) :
        gameNew(ctx.image.At(0x824632), reinterpret_cast<void *>(&New)),
        ourNew(reinterpret_cast<void *>(static_cast<void *(__cdecl *)(size_t)>(&::operator new)), reinterpret_cast<void *>(&New)),
        gameSized(ctx.image.At(0x824624), reinterpret_cast<void *>(&SizedDelete)),
        ourSized(reinterpret_cast<void *>(static_cast<void (__cdecl *)(void *, size_t)>(&::operator delete)), reinterpret_cast<void *>(&SizedDelete)),
        gameUnsized(ctx.image.At(0x82540f), reinterpret_cast<void *>(&UnsizedDelete)),
        ourUnsized(reinterpret_cast<void *>(static_cast<void (__cdecl *)(void *)>(&::operator delete)), reinterpret_cast<void *>(&UnsizedDelete)) { memorySpy = true; }
    ~MemorySpy() { memorySpy = false; }
};
void CanonicalBytes(const void *pointer, unsigned size, unsigned elementSize) {
    uint8_t bytes[68];
    for (unsigned offset = 0; offset < size; offset += elementSize) {
        std::memcpy(bytes, static_cast<const uint8_t *>(pointer) + offset, elementSize);
        for (unsigned stringOffset = 0; stringOffset < elementSize; stringOffset += 32) {
            if (stringOffset + 32 > elementSize) break;
            auto *text = reinterpret_cast<const cRZString *>(static_cast<const uint8_t *>(pointer) + offset + stringOffset);
            if (text->mVTable == GameAddress(cRZString::kVTable) && text->mString.capacity() > 15) {
                uint32_t id = Id(text->mString.data()); std::memcpy(bytes + stringOffset + 4, &id, 4);
            } else if (text->mVTable == GameAddress(cRZString::kVTable) && text->mString.empty()) {
                // Destruction resets the first SSO byte but leaves three bytes of
                // the freed heap pointer behind. Normalize those address bytes;
                // compare all other inactive-slot bytes, including the SSO tail.
                uint32_t word; std::memcpy(&word, bytes + stringOffset + 4, 4);
                for (unsigned i = 0; i < blockCount; ++i) if (blocks[i].freed &&
                    word == (uint32_t(reinterpret_cast<uintptr_t>(blocks[i].pointer)) & 0xffffff00u)) {
                    word = 0xfedcba00; std::memcpy(bytes + stringOffset + 4, &word, 4); break;
                }
            }
        }
        Bytes(bytes, elementSize);
    }
}
void HeapBytes() {
    for (unsigned i = 0; i < blockCount; ++i) {
        const auto &block = blocks[i]; Emit({block.id, block.size, block.freed});
        if (block.freed) continue;
        if (!HeapValidate(GetProcessHeap(), 0, block.pointer)) std::abort();
        if (block.stringObject) CanonicalBytes(block.pointer, 32, 32);
        else if (block.elementSize) CanonicalBytes(block.pointer, block.size, block.elementSize);
        else Bytes(block.pointer, block.size);
    }
}
void InputBytes() {
    Emit({0x700001, Id(world->weightedView.begin), world->weightedView.Size(), uint32_t(world->weightedView.capacity - world->weightedView.begin)});
    CanonicalBytes(world->weighted, sizeof(world->weighted), 68);
    for (auto &value : world->weighted) { StringBytes(value.name); StringBytes(value.value); }
    Emit({0x700002, Id(world->stringsView.begin), world->stringsView.Size(), uint32_t(world->stringsView.capacity - world->stringsView.begin)});
    CanonicalBytes(world->stringValues, sizeof(world->stringValues), 32);
    for (auto &value : world->stringValues) StringBytes(value);
    Emit({0x700003, Id(world->additionalView.begin), world->additionalView.Size(), uint32_t(world->additionalView.capacity - world->additionalView.begin)});
    CanonicalBytes(world->additionalValues, sizeof(world->additionalValues), 32);
    for (auto &value : world->additionalValues) StringBytes(value);
}
DNAWeightedGene *__fastcall CopyRecord(DNAWeightedGene *out, void *, const DNAWeightedGene *value) {
    Emit({0x4f5a10, Id(out), Id(value)}); StringBytes(value->name); StringBytes(value->value); Bytes(&value->weight, 4); Tick();
    std::memset(&out->name.mString, 0, 24); out->name.Construct(value->name, 0, UINT32_MAX);
    std::memset(&out->value.mString, 0, 24); out->value.Construct(value->value, 0, UINT32_MAX);
    out->weight = value->weight; return out;
}
void __fastcall DestroyRecord(DNAWeightedGene *value, void *) {
    Emit({0x257860, Id(value)}); StringBytes(value->name); StringBytes(value->value); Bytes(&value->weight, 4);
    value->value.Destruct(); value->name.Destruct();
}
void PrepareGenes(difftest::Context &ctx, World &w, uint32_t value, bool writing) {
    Initialize(ctx, w, value, true); writingGenes = writing;
    dnaPatch[5] = reinterpret_cast<void *>(&HasGene);
    textVT[17] = reinterpret_cast<void *>(&TextCopy); textVT[31] = reinterpret_cast<void *>(&TextPrintf);
    propertyVT[3] = reinterpret_cast<void *>(&PropertyWrite); propertyVT[9] = reinterpret_cast<void *>(&PropertyRead); propertyVT[10] = reinterpret_cast<void *>(&PropertyTest);
    w.property = {propertyVT, 5, 0}; w.alternateProperty = {propertyVT, 6, 0};
    DNA()->mPropertyStore = &w.property;
    const char *counts[] = {"", "0", "1", "2", "3", "4", "-1", "bad"};
    for (unsigned gene : {10u, 11u, 15u}) {
        std::snprintf(w.texts[gene * 2].data, 96, "%s", counts[(value >> 2) % 8]);
        std::snprintf(w.texts[gene * 2 + 1].data, 96, "%s", counts[(value >> 5) % 8]);
    }
    const char *weight[] = {"", "-20", "0", "254", "255", "256", "2000000000", "bad"};
    // Indexed ids share the fixture's strings modulo twelve. Weight ids land at
    // text 4/5, 6/7, etc.; name/value copies still use distinct ids and order.
    for (unsigned i = 4; i < 20; ++i) {
        if (value & 0x100) std::memset(w.texts[i].data, char('a' + i), 40);
        else std::snprintf(w.texts[i].data, 96, "%s", weight[(value + i) % 8]);
    }
}
void ResetGenes(const Guarded &initial, int side) { ResetWorld(initial, side); propertyReads = propertyTests = objectNews = 0; }
struct VectorOutput { DNAWeightedVector weighted{}; DNAStringVector strings{}; };
void InitializeOutput(VectorOutput &out, bool weighted, unsigned count, unsigned capacity) {
    if (!capacity) return;
    if (weighted) {
        out.weighted.begin = static_cast<DNAWeightedGene *>(ServiceAllocate(&world->allocator, nullptr, capacity * 68));
        out.weighted.end = out.weighted.begin; out.weighted.capacity = out.weighted.begin + capacity;
        for (unsigned i = 0; i < count; ++i) { CopyRecord(out.weighted.end, nullptr, &world->weighted[i % 3]); ++out.weighted.end; }
    } else {
        out.strings.begin = static_cast<cRZString *>(ServiceAllocate(&world->allocator, nullptr, capacity * 32));
        out.strings.end = out.strings.begin; out.strings.capacity = out.strings.begin + capacity;
        for (unsigned i = 0; i < count; ++i) {
            std::memset(&out.strings.end->mString, 0, 24); out.strings.end->Construct(world->stringValues[i % 3], 0, UINT32_MAX); ++out.strings.end;
        }
    }
}
void RecordOutput(const VectorOutput &out, bool weighted) {
    if (weighted) {
        Emit({Id(out.weighted.begin), out.weighted.Size(), out.weighted.begin ? uint32_t(out.weighted.capacity - out.weighted.begin) : 0});
        if (out.weighted.begin) CanonicalBytes(out.weighted.begin, uint32_t((out.weighted.capacity - out.weighted.begin) * 68), 68);
    } else {
        Emit({Id(out.strings.begin), out.strings.Size(), out.strings.begin ? uint32_t(out.strings.capacity - out.strings.begin) : 0});
        if (out.strings.begin) CanonicalBytes(out.strings.begin, uint32_t((out.strings.capacity - out.strings.begin) * 32), 32);
    }
}
void FreeOutput(VectorOutput &out, bool weighted) {
    if (weighted) {
        for (auto *value = out.weighted.begin; value != out.weighted.end; ++value) { value->value.Destruct(); value->name.Destruct(); }
        if (out.weighted.begin) ServiceFree(&world->allocator, nullptr, out.weighted.begin);
    } else {
        for (auto *value = out.strings.begin; value != out.strings.end; ++value) value->Destruct();
        if (out.strings.begin) ServiceFree(&world->allocator, nullptr, out.strings.begin);
    }
}
}

DIFF_TEST(SimDNA_PropertyAccessors, 0x4f9280) {
    using namespace DNAExtended;
    World w{};
    profile = Profile::Properties;
    auto get = ctx.Original<DNAStringPair *(__thiscall *)(void *, DNAStringPair *, uint32_t)>(0x4f9280);
    auto set = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, const DNAStringPair *)>(0x4fc690);
    auto has = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(0x4fa590);
    Slots stringSlots(ctx.image.At<void *>(cRZString::kVTable), 1, {reinterpret_cast<void *>(&FreshAdd), reinterpret_cast<void *>(&FreshRelease)});
    for (unsigned method = 0; method < 3; ++method) for (uint32_t i = 0; i < 700; ++i) {
        uint32_t value = i * 7919 + 13;
        PrepareGenes(ctx, w, value, false); Globals globals(ctx);
        if (i % 5 == 0) { DNA()->mPropertyStore = nullptr; ++propertyNull; }
        auto initial = w.dna;
        uint32_t gene = i < 32 ? (1u << i) : value;
        DNAStringPair input{(value & 1) ? reinterpret_cast<cIGZString *>(&w.texts[4]) : nullptr, (value & 2) ? reinterpret_cast<cIGZString *>(&w.texts[5]) : nullptr};
        mutateProperty = method == 1 && (value & 16);
        failure = i % 3 == 0 ? int(value % 12) : -1;
        nullNewAt = i % 7 == 0 ? int(i % 2) : -1;
        Result results[2];
        for (unsigned side = 0; side < 2; ++side) {
            ResetGenes(initial, int(side));
            struct PairBuffer { uint32_t before[4]; DNAStringPair pair; uint32_t after[4]; } output;
            std::memset(&output, 0xa5, sizeof(output)); int returned = 0;
            {
                MemorySpy memory(ctx);
                try {
                    if (method == 0) {
                        auto *result = side == 0 ? get(DNA(), &output.pair, gene) : CallOwn<DNAStringPair *>(DNA(), 6, &output.pair, gene);
                        returned = result == &output.pair;
                    } else if (method == 1) returned = side == 0 ? set(DNA(), gene, &input) : CallOwn<uint8_t>(DNA(), 4, gene, const_cast<const DNAStringPair *>(&input));
                    else returned = side == 0 ? has(DNA(), gene) : CallOwn<uint8_t>(DNA(), 5, gene);
                } catch (int e) { returned = -e; if (!side) ++getterThrows; }
                if (method == 0) { Bytes(output.before, 16); Emit({Id(output.pair.first), Id(output.pair.second)}); Bytes(output.after, 16); }
                HeapBytes(); InputBytes();
            }
            results[side] = Finish(returned);
        }
        Compare(ctx, results[0], results[1], method * 1000 + i);
    }
    nullNewAt = -1; failure = -1; mutateProperty = false;
    std::printf("  properties: null store %llu, reads success/failure %llu/%llu, has first/second/neither %llu/%llu/%llu, injected throws %llu\n",
        propertyNull, propertyReadSuccess, propertyReadFailure, propertyHasFirst, propertyHasSecond, propertyHasNeither, getterThrows);
}

DIFF_TEST(SimDNA_VectorGetters, 0x4f9830) {
    using namespace DNAExtended;
    World w{};
    profile = Profile::GetVectors;
    auto weighted = ctx.Original<uint8_t(__thiscall *)(void *, DNAWeightedVector *, uint8_t)>(0x4f9830);
    auto strings = ctx.Original<uint8_t(__thiscall *)(void *, DNAStringVector *)>(0x4f9d30);
    auto additional = ctx.Original<uint8_t(__thiscall *)(void *, DNAStringVector *)>(0x4f9600);
    Redirect copy(ctx.image.At(0x4f5a10), reinterpret_cast<void *>(&CopyRecord));
    Redirect destroy(ctx.image.At(0x257860), reinterpret_cast<void *>(&DestroyRecord));
    for (unsigned method = 0; method < 3; ++method) for (uint32_t i = 0; i < 700; ++i) {
        uint32_t value = i * 7919 + 17;
        PrepareGenes(ctx, w, value, false); Globals globals(ctx); auto initial = w.dna;
        unsigned capacity = i % 3 == 0 ? 0 : i % 3 == 1 ? 2 : 7;
        unsigned count = capacity ? i % 3 : 0;
        uint8_t second = uint8_t(i % 3);
        vectorMethod = method; selectedSecond = second != 0;
        const int injected = i % 3 == 0 ? int(value % 40) : -1;
        Result results[2];
        for (unsigned side = 0; side < 2; ++side) {
            ResetGenes(initial, int(side)); failure = -1; int returned = 0;
            VectorOutput output;
            {
                MemorySpy memory(ctx); InitializeOutput(output, method == 0, count, capacity);
                events.clear(); calls = 0; world->allocator.calls = 0; failure = injected;
                try {
                    if (method == 0) returned = side == 0 ? weighted(DNA(), &output.weighted, second) : CallOwn<uint8_t>(DNA(), 9, &output.weighted, second);
                    else if (method == 1) returned = side == 0 ? strings(DNA(), &output.strings) : CallOwn<uint8_t>(DNA(), 11, &output.strings);
                    else returned = side == 0 ? additional(DNA(), &output.strings) : CallOwn<uint8_t>(DNA(), 13, &output.strings);
                } catch (int e) { returned = -e; if (!side) ++getterThrows; }
                RecordOutput(output, method == 0); HeapBytes(); InputBytes(); FreeOutput(output, method == 0);
            }
            results[side] = Finish(returned);
        }
        Compare(ctx, results[0], results[1], method * 1000 + i);
    }
    failure = -1;
    std::printf("  vector getters: gene present/missing %llu/%llu, injected throws (properties+vectors) %llu\n", genePresent, geneMissing, getterThrows);
    std::printf("  weight branches default/lower/middle/upper:");
    for (auto count : weights) { std::printf(" %llu", count); if (!count) ctx.Fail("weighted getter clamp/default branch missing"); }
    std::printf("\n");
}

DIFF_TEST(SimDNA_VectorSetters, 0x4fd090) {
    using namespace DNAExtended;
    World w{};
    profile = Profile::SetVectors;
    auto weighted = ctx.Original<uint8_t(__thiscall *)(void *, const DNAWeightedVector *, uint8_t)>(0x4fd090);
    auto strings = ctx.Original<uint8_t(__thiscall *)(void *, const DNAStringVector *, uint8_t)>(0x4fd390);
    auto additional = ctx.Original<uint8_t(__thiscall *)(void *, const DNAStringVector *, uint8_t)>(0x4fcea0);
    for (unsigned method = 0; method < 3; ++method) for (uint32_t i = 0; i < 700; ++i) {
        uint32_t value = i * 7919 + 19;
        PrepareGenes(ctx, w, value, true); Globals globals(ctx);
        if (i % 5 == 0) DNA()->mPropertyStore = nullptr;
        auto initial = w.dna;
        uint8_t both = uint8_t(i % 3);
        failure = i % 3 == 0 ? int(value % 40) : -1;
        Result results[2];
        for (unsigned side = 0; side < 2; ++side) {
            ResetGenes(initial, int(side)); int returned = 0;
            {
                MemorySpy memory(ctx);
                try {
                    if (method == 0) returned = side == 0 ? weighted(DNA(), &w.weightedView, both) : CallOwn<uint8_t>(DNA(), 10, &w.weightedView, both);
                    else if (method == 1) returned = side == 0 ? strings(DNA(), &w.stringsView, both) : CallOwn<uint8_t>(DNA(), 12, &w.stringsView, both);
                    else returned = side == 0 ? additional(DNA(), &w.additionalView, both) : CallOwn<uint8_t>(DNA(), 14, &w.additionalView, both);
                } catch (int e) { returned = -e; if (!side) ++writerThrows; }
                HeapBytes(); InputBytes();
            }
            results[side] = Finish(returned);
            // The recording Copy/Sprintf fakes mutate gene strings. Recreate
            // them from the same seed before invoking the other implementation.
            if (side == 0) { PrepareGenes(ctx, w, value, true); DNA()->mPropertyStore = reinterpret_cast<cTSSimDNA *>(initial.bytes)->mPropertyStore; }
        }
        Compare(ctx, results[0], results[1], method * 1000 + i);
    }
    writingGenes = false; failure = -1;
    std::printf("  vector setters: 2100 cases, both flags 0/1/2, null stores, injected throws %llu\n", writerThrows);
}

namespace DNAExtended {
uint8_t __fastcall FrameworkQuery(FakeObject *self, void *, uint32_t cls, uint32_t iface, void **out) {
    Emit({405, self->id, cls, iface}); Tick(); ++self->calls; *out = &world->allocator; return 0x80;
}
uint32_t __fastcall AllocatorRelease(FakeObject *self, void *) { Emit({402, self->id}); ++self->calls; return 0; }
}
DIFF_TEST(SimDNA_WeightedInsertReallocate, 0x4f50e0) {
    using namespace DNAExtended;
    World w{};
    profile = Profile::Insert;
    auto original = ctx.Original<DNAWeightedGene *(__thiscall *)(DNAWeightedVector *, DNAWeightedGene *, const DNAWeightedGene *)>(0x4f50e0);
    Redirect copy(ctx.image.At(0x4f5a10), reinterpret_cast<void *>(&CopyRecord));
    Redirect destroy(ctx.image.At(0x257860), reinterpret_cast<void *>(&DestroyRecord));
    unsigned appends = 0, interiors = 0, aliases = 0, firstUse = 0, failures = 0;
    auto **framework = ctx.image.At<void *>(0x10c9378); void *savedFramework = *framework;
    for (uint32_t i = 0; i < 1000; ++i) {
        uint32_t value = i * 7919 + 23;
        PrepareGenes(ctx, w, value, false); Globals globals(ctx); auto initial = w.dna;
        singletonVT[5] = reinterpret_cast<void *>(&FrameworkQuery); allocatorVT[2] = reinterpret_cast<void *>(&AllocatorRelease);
        *framework = &w.singleton;
        unsigned count = i % 6, capacity = count + (i % 3);
        unsigned position = i % (count + 1);
        bool alias = count && i % 4 == 0;
        bool initialize = i % 5 == 0;
        const int injectedCall = i % 3 == 0 ? int(i % 8) : -1;
        position == count ? ++appends : ++interiors; if (alias) ++aliases; if (initialize) ++firstUse;
        Result results[2];
        for (unsigned side = 0; side < 2; ++side) {
            ResetGenes(initial, int(side)); failure = -1; nullNewAt = -1; int returned = 0;
            VectorOutput output;
            {
                MemorySpy memory(ctx); InitializeOutput(output, true, count, capacity);
                events.clear(); calls = 0; world->allocator.calls = world->singleton.calls = 0;
                *globals.allocator = initialize ? nullptr : &w.allocator; *globals.guard = initialize ? 0 : -2;
                const auto *input = alias ? output.weighted.begin + (i % count) : &w.weighted[1];
                failure = injectedCall;
                try {
                    auto *inserted = side == 0 ? original(&output.weighted, output.weighted.begin ? output.weighted.begin + position : nullptr, input) :
                        output.weighted.InsertReallocate(output.weighted.begin ? output.weighted.begin + position : nullptr, input);
                    returned = int(inserted - output.weighted.begin);
                } catch (int e) { returned = -e; if (!side) ++failures; }
                // Thread-safe static epochs belong to separate CRT instances.
                // Compare pending/uninitialized/initialized state, not epoch ids.
                Emit({Id(*globals.allocator), uint32_t(*globals.guard == 0 ? 0 : *globals.guard == -1 ? 1 : 2)});
                RecordOutput(output, true); HeapBytes(); FreeOutput(output, true);
            }
            results[side] = Finish(returned);
        }
        Compare(ctx, results[0], results[1], i);
    }
    *framework = savedFramework; failure = -1;
    std::printf("  weighted insert: append %u, interior %u, aliased inputs %u, first allocator use %u, injected failures %u\n", appends, interiors, aliases, firstUse, failures);
}

DIFF_TEST(SimDNA_CaptureAllocationFailures, 0x4fb740) {
    using namespace DNAExtended;
    World w{}; profile = Profile::Capture; forceShort = true;
    auto original = ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0x4fb740);
    Redirect format(ctx.image.At(0x4fdd30), reinterpret_cast<void *>(&Format));
    Redirect resolve(ctx.image.At(0xb5e590), reinterpret_cast<void *>(&Resolve));
    Redirect destroyFloat(ctx.image.At(0x1d99b0), reinterpret_cast<void *>(&DestroyFloat));
    Slots stringSlots(ctx.image.At<void *>(cRZString::kVTable), 1, {reinterpret_cast<void *>(&FreshAdd), reinterpret_cast<void *>(&FreshRelease)});
    for (uint32_t i = 0; i < 100; ++i) {
        uint32_t value = (i * 7919 + 13) & ~0x600u;
        if (i & 1) value |= 0x600;
        Initialize(ctx, w, value, false); Globals globals(ctx); auto initial = w.dna;
        writingGenes = false; failure = -1; nullNewAt = int(i % 6);
        Result results[2];
        for (unsigned side = 0; side < 2; ++side) {
            ResetGenes(initial, int(side));
            {
                MemorySpy memory(ctx);
                if (side == 0) original(DNA(), &w.appearance, 0x3f);
                else CallOwn<void>(DNA(), 15, static_cast<void *>(&w.appearance), 0x3f);
                HeapBytes(); InputBytes();
            }
            results[side] = Finish(0);
        }
        Compare(ctx, results[0], results[1], i);
    }
    nullNewAt = -1; forceShort = false;
    std::printf("  capture allocation failure: 100 cases, special/normal, six fresh-string allocation positions\n");
}

// ---- the slots no other test calls: AddRef (folded 0x148fb0), SetPropertyStore (folded 0x4043a0), GetPropertyStore (folded
// 0x46390). Each side calls slot n of its own vtable on its own object (the original side: the game's folded bodies).
namespace {
struct CountedStore { void **vt; uint32_t id; uint32_t count; };
void *countedVT[3]{};
CountedStore counted[3];
uint32_t StoreId(const void *pointer)
{
    if (!pointer) return 0;
    for (auto &value : counted) if (pointer == &value) return value.id;
    if (pointer == &store) return store.id;
    return 0xffffffff;
}
uint32_t __fastcall CountedAdd(CountedStore *self, void *)
{
    Tick(); trace.insert(trace.end(), {91, self->id, self->count, StoreId(Object()->mPropertyStore)});
    return ++self->count;
}
uint32_t __fastcall CountedRelease(CountedStore *self, void *)
{
    Tick(); trace.insert(trace.end(), {92, self->id, self->count, StoreId(Object()->mPropertyStore)});
    return --self->count;
}
}

DIFF_TEST(SimDNA_Slots, 0x4043a0) {
    Setup(); std::mt19937 rng(0xd0e);
    countedVT[1] = reinterpret_cast<void *>(&CountedAdd);
    countedVT[2] = reinterpret_cast<void *>(&CountedRelease);
    const uint32_t edges[] = {0, 1, 0x7fffffff, 0xffffffff};
    uint64_t perSlot[3]{}, same = 0, exceptions = 0;
    for (unsigned i = 0; i < 1800; ++i) {
        auto initial = Initial(rng);
        auto *object = reinterpret_cast<cTSSimDNA *>(initial.bytes);
        object->mRefCount = i % 2 ? edges[(i / 2) % 4] : rng();
        void *const choices[] = {nullptr, &counted[0], &counted[1], &counted[2]};
        object->mPropertyStore = choices[rng() % 4];
        void *argument = i % 5 == 0 ? object->mPropertyStore : choices[rng() % 4];
        const unsigned which = i % 3, slot = which == 0 ? 1 : which == 1 ? 7 : 8;
        throwAt = i % 11 == 0 ? int(rng() % 2) : -1;
        Outcome results[2];
        for (int side = 0; side < 2; ++side) {
            for (unsigned c = 0; c < 3; ++c) counted[c] = {countedVT, 300 + c, 5 + c};
            Side(side); Place(initial); Reset(); int r = 0;
            try {
                if (slot == 1) r = int(CallOwn<uint32_t>(Object(), 1));
                else if (slot == 7) CallOwn<void>(Object(), 7, argument);
                else r = int(StoreId(CallOwn<void *>(Object(), 8)));
            } catch (int e) { r = -e; if (!side) ++exceptions; }
            for (auto &value : counted) trace.insert(trace.end(), {value.id, value.count});
            results[side] = Capture(r);
        }
        Compare(ctx, results[0], results[1], i);
        ++perSlot[which];
        if (slot == 7 && argument == object->mPropertyStore) ++same;
    }
    throwAt = -1;
    std::printf("  slots: AddRef %llu, SetPropertyStore %llu (same store %llu), GetPropertyStore %llu, injected throws %llu\n",
        perSlot[0], perSlot[1], same, perSlot[2], exceptions);
}

// ---- the RVA hooks' entry points (cTSSimDNAAdapters) on an object carrying a copy of the game's vtable whose forwarded slot is a
// trap (with the slot's stack arity): an adapter that dispatched virtually would reach the trap (in game: its own hook). Each case
// runs the original at its RVA and the adapter on the same object state; the slots an adapter's body legitimately calls on this
// (QueryInterface: 1, Release: 17) are the game's, Release's slot 17 the recording fake. The gene slots' adapters were dropped
// (lane GJ, in effect through callers); their behaviour is tested through our vtable above.
namespace {
using A = nTSGenetics::cTSSimDNAAdapters;
void *adapterVT[18]{};
unsigned trapped;
uint32_t TrapHit(void *, unsigned) { ++trapped; return 0; }
struct AdapterCase { uint32_t rva; void *adapter; unsigned slot; unsigned arity; };
const AdapterCase kAdapterCases[] = {
    {0x4fb6a0, reinterpret_cast<void *>(&A::QueryInterface), 0, 2},
    {0x4fb6f0, reinterpret_cast<void *>(&A::Release), 2, 0},
    // The other slots' adapters were dropped (lane GJ): only the game's vtable reached them, and no object carries it
    // while the creator hook is in; their originals are trapped. QueryInterface and Release keep theirs (byte matches
    // tie through the adapter).
};
void SetTrap(const AdapterCase &c)
{
    std::memcpy(adapterVT, GameVT(), sizeof(adapterVT));
    if (c.slot == 2) adapterVT[17] = reinterpret_cast<void *>(&Delete);
    adapterVT[c.slot] = shape::TrapFor<TrapHit>(c.arity);
}
}

DIFF_TEST(SimDNA_Adapters, 0x4fb6a0) {
    Setup(); std::mt19937 rng(0xd0f); throwAt = -1;
    const uint32_t ids[] = {1, cTSSimDNA::kInterfaceId, 0, 0x0c7aad38};
    unsigned cases = 0;
    for (const AdapterCase &c : kAdapterCases)
        for (unsigned seed = 0; seed < 40; ++seed) {
            auto initial = Initial(rng);
            auto *object = reinterpret_cast<cTSSimDNA *>(initial.bytes);
            *reinterpret_cast<void **>(object) = adapterVT;
            const bool counted = c.slot == 0 || c.slot == 2 || c.slot == 17;
            if (!counted) object->mPropertyStore = nullptr;
            if (seed % 4 == 0) object->mPropertyStore = nullptr;
            const uint32_t a1 = rng(), a2 = rng();
            if (c.slot == 2) object->mRefCount = seed % 3 ? seed % 4 : a1;
            presentMask = a1 & 0xffff; pairMask = seed % 4; salt = a2; modifyPair = false;
            for (unsigned j = 0; j < 8; ++j) std::strcpy(strings[j].data, j % 3 ? "1" : "");
            Outcome results[2];
            unsigned traps[2];
            for (int side = 0; side < 2; ++side) {
                void *fn = side == 0 ? ctx.image.At(c.rva) : c.adapter;
                gSide = side; Reset(); work = initial; SetTrap(c); trapped = 0;
                int r = 0;
                switch (c.slot) {
                case 0: {
                    void *out = &source;
                    r = uint8_t(reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, void **)>(fn)(Object(), ids[seed % 4], seed % 5 ? &out : nullptr));
                    trace.push_back(out == Object() ? 1 : out == &source ? 2 : 3);
                    break;
                }
                case 2: r = int(reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(Object())); break;
                }
                results[side] = Capture(r);
                traps[side] = trapped;
            }
            Compare(ctx, results[0], results[1], c.rva << 8 | seed);
            if (traps[0] || traps[1]) ctx.Fail("SimDNA adapter 0x%x reached its trapped slot %u (original %u, adapter %u)", c.rva, c.slot, traps[0], traps[1]);
            ++cases;
        }
    std::printf("  adapters: %zu entry points, %u cases\n", std::size(kAdapterCases), cases);
}
