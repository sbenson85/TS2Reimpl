// Differential tests for nTSSG::cLightingManager's construction and destruction (lane 261110t, scenegraph LG3f;
// src/sg/cLightingManagerCreate.cpp): the constructor 0x2ca6f0, Create 0x2d83a0, the destructor 0x2cb800 and the light
// set copy 0x2cb200 (the push_back's copy, our LightingState copy constructor). Hook shape: the original runs on the
// same bytes as ours.
//
// The world: the game's operator new 0x824632 and sized delete 0x824624 are patched to ours, so both sides' blocks come
// from one heap; the allocator service (0x10c95d4, svcfx::AllocatorScope) holds the +0x214 tree's blocks. A heap watcher
// (chained to the normalised words' registry) fills every new block with 0xcd, so whatever a side leaves unwritten reads
// the same on both, numbers the blocks in allocation order and records each free. Nothing in the code under test
// reallocates (the fill would overwrite a moved block's contents).
//
// A word of the manager is data or a pointer by its offset (Kinds below). Pointers compare by identity: null, a fake (its
// id), a heap block (its allocation number and offset, freed blocks included unless a later block reused their memory, newest first), the +0x2c holder, or as
// they are (the image's addresses: the vtables). The strings compare by size, capacity and text (the in-place buffer's
// unused bytes too where a side wrote them); the trees node by node in pre-order.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/LightingCommands.h"
#include "rz/cRZString.h"
#include "sg/cLightingManager.h"
#include "sg/cTextureManager.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace lg3f {
namespace {

using namespace svcfx;
using nTSSG::cBuiltRef;
using nTSSG::cLerpLightSet;
using nTSSG::cLightingManager;
using nTSSG::cPortalLight;
using nTSSG::cPortalRecord;
using nTSSG::cSkySample;

constexpr uint32_t kObjectSize = 0x548;
constexpr uint32_t kWords = kObjectSize / 4;
constexpr uint32_t kBlockTag = 0xb0000000; // | allocation number << 16 | offset
constexpr uint32_t kHolderTag = 0xc0000000;
enum : uint32_t { evRelease = 0x70, evTexture = 0x71 };

alignas(16) uint8_t gObject[kObjectSize];
cLightingManager *Ours() { return reinterpret_cast<cLightingManager *>(gObject); }

// ---- the heap watch ----
struct Block {
    uintptr_t lo, hi;
    bool reused; // a later block overlaps it (it was freed and its memory handed out again)
};
constexpr unsigned kMaxBlocks = 8192;
Block gBlocks[kMaxBlocks];
unsigned gBlockCount = 0;
uint32_t gFreed[kMaxBlocks];
unsigned gFreeCount = 0;
bool gOverflow = false;

// The +0x2c member: an object whose virtual base (a fake) the destructor reaches through its vbptr at +8.
struct Holder {
    uint32_t words[2];
    const int32_t *vbptr;
    uint32_t tail;
};
Holder gHolder;
int32_t gVbtable[2];

uint32_t Ident(uint32_t v)
{
    if (!v) return 0;
    for (unsigned i = 0; i < kFakes; ++i) {
        const uint32_t at = P(&F(i));
        if (v == at) return 200 + i;
        if (v > at && v < at + sizeof(Obj)) return normwords::kFakeInterior | i << 12 | (v - at);
    }
    // Blocks whose memory a later block reuses are skipped (which old block a reused address lands in depends on the
    // heap's layout, which differs between the sides), so the blocks searched never overlap.
    for (unsigned k = gBlockCount; k-- > 0;) // inside a block first (the next block may start where one ends)
        if (!gBlocks[k].reused && v >= gBlocks[k].lo && v < gBlocks[k].hi)
            return kBlockTag | k << 16 | uint32_t(v - gBlocks[k].lo);
    for (unsigned k = gBlockCount; k-- > 0;) // then an end pointer
        if (!gBlocks[k].reused && v == gBlocks[k].hi) return kBlockTag | k << 16 | uint32_t(v - gBlocks[k].lo);
    if (v >= P(&gHolder) && v < P(&gHolder) + sizeof(gHolder)) return kHolderTag | (v - P(&gHolder));
    return v; // the image's addresses (the same on both sides)
}

void Allocated(void *p, size_t bytes)
{
    normwords::HeapAllocated(p, bytes);
    std::memset(p, 0xcd, bytes);
    if (gBlockCount == kMaxBlocks) return void(gOverflow = true);
    const uintptr_t lo = uintptr_t(p), hi = lo + bytes;
    for (unsigned k = 0; k < gBlockCount; ++k)
        if (gBlocks[k].lo < hi && lo < gBlocks[k].hi) gBlocks[k].reused = true;
    gBlocks[gBlockCount++] = {lo, hi, false};
}
void Resized(void *p, size_t bytes)
{
    normwords::HeapResized(p, bytes);
    for (unsigned k = gBlockCount; k-- > 0;)
        if (gBlocks[k].lo == uintptr_t(p)) return void(gBlocks[k].hi = gBlocks[k].lo + bytes);
}
void Freed(void *p)
{
    normwords::HeapFreed(p);
    if (gFreeCount == kMaxBlocks) return void(gOverflow = true);
    gFreed[gFreeCount++] = Ident(P(p));
}
const difftest::HeapWatcher gWatch{&Allocated, &Resized, &Freed};

// A side starts: the fakes, the log and the registry reset, the blocks numbered from 0.
void StartSide()
{
    ResetAll();
    gEv.reserve(1u << 18); // the log never grows inside a call (its old buffer's free would be seen)
    difftest::WatchHeap(&gWatch);
    gBlockCount = 0, gFreeCount = 0, gOverflow = false;
}

// ---- the manager's words ----
struct KindTable {
    char k[kWords];
};
KindTable MakeKinds()
{
    KindTable t;
    std::memset(t.k, 'd', sizeof(t.k));
    auto p = [&](uint32_t off, uint32_t n = 1) {
        for (uint32_t i = 0; i < n; ++i) t.k[off / 4 + i] = 'p';
    };
    auto str = [&](uint32_t off) { // a cRZString: vtable, the std::string (logged by LogString), reference count
        p(off);
        for (uint32_t i = 1; i <= 6; ++i) t.k[off / 4 + i] = 'x';
    };
    p(0x000, 2);                                                          // the two vtables
    p(0x00c);                                                             // mMessageTypes' head
    p(0x018, 7);                                                          // mLitObject .. mFileParser
    p(0x038, 2);                                                          // mShadeImage, mAmbientImage
    for (uint32_t v : {0x08cu, 0x098u, 0x0a4u, 0x1f4u, 0x21cu, 0x2b0u}) p(v, 3); // the vectors
    str(0x0b0);
    p(0x0d4);                                                             // mSetD4's head
    for (uint32_t v : {0x10cu, 0x15cu, 0x1acu}) str(v);                   // the settings blocks' texts
    p(0x214);                                                             // mSet214's head
    p(0x228, 3);                                                          // the lerp sets
    p(0x238, 2);
    for (uint32_t v : {0x248u, 0x254u, 0x260u, 0x2a0u}) p(v, 3);          // the sun height map's vectors
    str(0x2d0);                                                           // mDefaultSet's name
    str(0x524);                                                           // mLightingPath
    return t;
}
const KindTable kKinds = MakeKinds();
constexpr uint32_t kStrings[] = {0x0b0, 0x10c, 0x15c, 0x1ac, 0x2d0, 0x524};

// A std::string at `at`: size, capacity, the heap buffer's identity and its text (size + 1 characters); `full` adds the
// in-place buffer's other bytes (the constructors' and copies' untouched bytes).
template <class Out> void LogString(Out &&out, const uint8_t *at, bool full)
{
    uint32_t w[6];
    std::memcpy(w, at, sizeof(w));
    out(w[4]), out(w[5]);
    const bool large = w[5] > 15;
    const uint8_t *text = large ? reinterpret_cast<const uint8_t *>(uintptr_t(w[0])) : at;
    // A buffer of 0x1000 bytes and more is 32-byte aligned inside a larger block whose start is stored before it: where
    // in the block it lands follows the heap's address, so the block, the alignment and that stored start are logged.
    if (large && w[5] + 1 >= 0x1000)
        out(Ident(w[0]) & 0xffff0000), out(w[0] & 0x1f), out(Ident(reinterpret_cast<const uint32_t *>(uintptr_t(w[0]))[-1]));
    else if (large) out(Ident(w[0]));
    const uint32_t n = (w[4] < 0x2000 ? w[4] : 0x2000) + 1;
    for (uint32_t i = 0; i < n; i += 4) {
        uint32_t word = 0;
        std::memcpy(&word, text + i, n - i < 4 ? n - i : 4);
        out(word);
    }
    if (full)
        for (int i = large ? 1 : 0; i < 4; ++i) out(w[i]);
}

template <class Out> void LogWords(Out &&out, const uint8_t *object, bool full)
{
    const auto *w = reinterpret_cast<const uint32_t *>(object);
    for (uint32_t i = 0; i < kWords; ++i) {
        if (kKinds.k[i] == 'p') out(Ident(w[i]));
        else if (kKinds.k[i] == 'd') out(w[i]);
    }
    for (uint32_t s : kStrings) LogString(out, object + s + 4, full);
}

void DumpManager(Blob &b, bool full)
{
    LogWords([&](uint32_t v) { b.push_back(v); }, gObject, full);
}

// A tree's nodes (std::_Tree: left, parent, right, colour / isnil bytes and the value) in pre-order from the root, and
// the head; `words` per node.
void DumpTree(Blob &b, uint32_t headWord, uint32_t words)
{
    const auto *head = reinterpret_cast<const uint32_t *>(uintptr_t(headWord));
    auto node = [&](const uint32_t *n) {
        for (uint32_t i = 0; i < words; ++i) b.push_back(i < 3 ? Ident(n[i]) : n[i]);
    };
    node(head);
    const uint32_t *stack[128];
    unsigned depth = 0;
    if (head[1] != headWord) stack[depth++] = reinterpret_cast<const uint32_t *>(uintptr_t(head[1]));
    unsigned visited = 0;
    while (depth && visited++ < 4096) {
        const uint32_t *n = stack[--depth];
        node(n);
        if (n[2] != headWord && depth < 128) stack[depth++] = reinterpret_cast<const uint32_t *>(uintptr_t(n[2]));
        if (n[0] != headWord && depth < 128) stack[depth++] = reinterpret_cast<const uint32_t *>(uintptr_t(n[0]));
    }
}

// A light set (a LightingState, 0x214 bytes): the name's vtable, its string, reference count, then the values' words.
void DumpLightSet(Blob &b, const uint8_t *set, bool full)
{
    const auto *w = reinterpret_cast<const uint32_t *>(set);
    b.push_back(Ident(w[0]));
    LogString([&](uint32_t v) { b.push_back(v); }, set + 4, full);
    for (uint32_t i = 7; i < 0x214 / 4; ++i) b.push_back(w[i]);
}

void DumpOwned(Blob &b, bool full)
{
    const auto *w = reinterpret_cast<const uint32_t *>(gObject);
    DumpTree(b, w[0x00c / 4], 5);
    DumpTree(b, w[0x0d4 / 4], 5);
    DumpTree(b, w[0x214 / 4], 4);
    for (uint32_t at = w[0x098 / 4]; at && at < w[0x09c / 4]; at += 0x214)
        DumpLightSet(b, reinterpret_cast<const uint8_t *>(uintptr_t(at)), full);
}

void DumpHeap(Blob &b, const difftest::AllocationCounter &count)
{
    b.push_back(gOverflow);
    b.push_back(gBlockCount);
    for (unsigned k = 0; k < gBlockCount; ++k) b.push_back(uint32_t(gBlocks[k].hi - gBlocks[k].lo));
    b.push_back(gFreeCount);
    for (unsigned k = 0; k < gFreeCount; ++k) b.push_back(gFreed[k]);
    b.push_back(uint32_t(count.Allocations())), b.push_back(uint32_t(count.Frees()));
    b.push_back(uint32_t(count.Blocks())), b.push_back(uint32_t(count.Bytes()));
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- the destructor's world: what the fakes see ----
constexpr unsigned kLogWords = 1u << 19;
uint32_t gLog[kLogWords];
unsigned gLogCount = 0;
void Log(uint32_t v)
{
    if (gLogCount < kLogWords) gLog[gLogCount++] = v;
    else gOverflow = true;
}
// The manager's words (not the strings' unused bytes) when a fake is released or the texture handle let go.
void Snapshot() { LogWords(&Log, gObject, false); }

uint32_t FakeRelease(Obj *self, uint32_t *)
{
    Log(evRelease), Log(self->id), Log(gFreeCount); // the frees so far: a release and a free in the other order differ
    Snapshot();
    return uint32_t(--self->refs);
}

void __fastcall StandTexture(uint32_t *handle, void *)
{
    Log(evTexture), Log(Ident(P(handle))), Log(*handle), Log(gFreeCount);
    Snapshot();
}

struct Fixture {
    AllocatorScope allocator;
    shape::CodePatch gameNew{GameAddress<void>(0x824632), shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{GameAddress<void>(0x824624), shape::CodeOf(&GameDelete)};
    ~Fixture() { normwords::StopHeap(); }
};

void Fill(std::mt19937 &gen, uint8_t *bytes, size_t n)
{
    for (size_t i = 0; i < n; ++i) bytes[i] = uint8_t(gen());
}

const uint32_t kLengths[] = {0, 1, 4, 14, 15, 16, 31, 40, 0x1000, 0x1100};
std::string Text(std::mt19937 &gen)
{
    const uint32_t n = gen() % 8 ? kLengths[gen() % 8] : kLengths[8 + gen() % 2];
    std::string s(n, 'a');
    for (char &c : s) c = char('a' + gen() % 26);
    return s;
}

} // namespace

// 0x2ca6f0 the constructor over drawn bytes (and through it 0xb3d240 the settings blocks' defaults, 0xb3cfb0 the
// lighting state's with 0xb3cdb0 / 0xb3cdf0 / 0xb3ce50 its lights, diffusion and modifiers, and 0x2cb200 the push_back's
// copy). Compared: the result, every word, the trees, the light set's block, the blocks allocated (sizes in order) and
// freed (by number), the heap counts.
DIFF_TEST(sg_lighting_construct_ctor, 0x2ca6f0)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0x2ca6f0);
    for (uint32_t seed = 0; seed < 64; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2ca6f0u);
        uint8_t fill[kObjectSize];
        Fill(gen, fill, sizeof(fill));
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            StartSide();
            std::memcpy(gObject, fill, sizeof(fill));
            Blob &o = outcome[side];
            void *result;
            {
                difftest::AllocationCounter count;
                ctx.Mark(side ? "ours" : "original");
                result = side == 0 ? original(gObject) : Ours()->Construct();
                count.Stop();
                difftest::WatchHeap(nullptr); // the test's own blocks from here on are not the code's
                o.push_back(Ident(P(result)) == P(gObject));
                DumpHeap(o, count);
            }
            DumpManager(o, true);
            DumpOwned(o, true);
            Ours()->Destruct();
        }
        Same(ctx, outcome[0], outcome[1], "ctor", seed);
    }
}

// 0x2d83a0 Create: the block (0x548 bytes from operator new, then the constructor). Compared: the result's block, its
// words, the trees, the light set, the heap.
DIFF_TEST(sg_lighting_construct_create, 0x2d83a0)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__cdecl *)()>(0x2d83a0);
    for (uint32_t seed = 0; seed < 8; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            StartSide();
            Blob &o = outcome[side];
            void *result;
            {
                difftest::AllocationCounter count;
                ctx.Mark(side ? "ours" : "original");
                result = side == 0 ? original() : cLightingManager::Create();
                count.Stop();
                difftest::WatchHeap(nullptr); // the test's own blocks from here on are not the code's
                o.push_back(Ident(P(result)));
                DumpHeap(o, count);
            }
            std::memcpy(gObject, result, kObjectSize); // the words are read from the copy (its pointers are the block's)
            DumpManager(o, true);
            DumpOwned(o, true);
            static_cast<cLightingManager *>(result)->Destruct();
            ::operator delete(result);
        }
        Same(ctx, outcome[0], outcome[1], "create", seed);
    }
}

// 0x2cb800 the destructor (and through it 0x2cbbb0 the settings blocks' and 0x2cb6c0 the sun height map's) over a
// manager our constructor built and the test filled: extra tree values, portal lights, light sets with short and long
// names, ambient maps (fakes or null), every string short or long, sky samples whose lights, lerp sets and portal records
// hold fakes, built lights, the sun height map's grids, samples, and every counted pointer a fake or null (+0x2c through
// its virtual base). The texture handle's release 0xae82b0 is stood in on both sides. Compared: each release (the fake,
// with the manager's words at that moment), the texture release, the frees in order (by block), the free recorder's
// sizes, the heap counts, the words after, every fake's count.
DIFF_TEST(sg_lighting_construct_dtor, 0x2cb800)
{
    Fixture fixture;
    shape::StandIn texture{0xae82b0, &nGZSceneGraph::cTextureHandle::Destroy, &StandTexture};
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2cb800);
    constexpr uint32_t kMembers[] = {0x18, 0x1c, 0x20, 0x24, 0x28, 0x30, 0x38, 0x3c, 0x228, 0x22c, 0x230, 0x238, 0x23c};
    for (uint32_t seed = 0; seed < 96; ++seed) {
        std::mt19937 plan(seed * 7919u + 0x2cb800u);
        uint8_t fill[kObjectSize];
        Fill(plan, fill, sizeof(fill));
        const uint32_t planSeed = plan();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(planSeed);
            StartSide();
            for (unsigned i = 0; i < kFakes; ++i) {
                F(i).Set(1, 0u, 0u, &FakeRelease);
                F(i).Set(2, 0u, 0u, &FakeRelease);
            }
            std::memcpy(gObject, fill, sizeof(fill));
            cLightingManager *m = Ours()->Construct();
            auto fake = [&]() -> void * { return gen() % 4 ? &F(gen() % kFakes) : nullptr; };
            for (uint32_t off : kMembers) {
                void *f = fake();
                std::memcpy(gObject + off, &f, 4);
            }
            if (gen() % 3) {
                gVbtable[0] = int32_t(gen());
                gVbtable[1] = int32_t(P(&F(gen() % kFakes)) - P(&gHolder.vbptr));
                gHolder.vbptr = gVbtable;
                m->mRef2C = reinterpret_cast<uint8_t *>(&gHolder);
            } else {
                m->mRef2C = nullptr;
            }
            m->mShadeTexture = gen() % 3 ? gen() : 0xffffffff;
            for (uint32_t n = gen() % 4; n; --n) m->mMessageTypes.insert(gen() % 64);
            for (uint32_t n = gen() % 5; n; --n) m->mSetD4.insert(gen());
            for (uint32_t n = gen() % 5; n; --n) m->mSet214.insert(int16_t(gen()));
            m->mPortalLights.resize(gen() % 3);
            for (uint32_t n = gen() % 3; n; --n) {
                alignas(4) uint8_t raw[sizeof(LightingState)];
                LightingState *state = reinterpret_cast<LightingState *>(raw)->Construct();
                state->mName.mString = Text(gen);
                reinterpret_cast<std::vector<LightingState> &>(m->mLightSets).push_back(*state);
                state->~LightingState();
            }
            for (uint32_t n = gen() % 4; n; --n) m->mAmbientMaps.push_back(fake());
            m->mName = Text(gen);
            m->mObjectSettings.mText.mString = Text(gen);
            m->mSimSettings.mText.mString = Text(gen);
            m->mWallSettings.mText.mString = Text(gen);
            reinterpret_cast<LightingState *>(&m->mDefaultSet)->mName.mString = Text(gen);
            m->mLightingPath = Text(gen);
            const uint32_t samples = gen() % 4;
            m->mSkySamples.reserve(samples);
            for (uint32_t s = 0; s < samples; ++s) {
                cSkySample &sample = m->mSkySamples.emplace_back();
                sample.mLights.resize(gen() % 3);
                for (cBuiltRef &ref : sample.mLights) ref.mObject = fake();
                sample.mLerp78.mSet = static_cast<cLerpLightSet *>(fake());
                sample.mPortals.resize(gen() % 3);
                for (cPortalRecord &record : sample.mPortals) record.mObject.mObject = fake(), record.mOther.mObject = fake();
                sample.mLerp88.mSet = static_cast<cLerpLightSet *>(fake());
            }
            m->mBuiltLights.resize(gen() % 4);
            for (cBuiltRef &ref : m->mBuiltLights) ref.mObject = fake();
            m->mSunHeights.mValues.resize(gen() % 5);
            m->mSunHeights.mGround.resize(gen() % 5);
            m->mSunHeights.mHeights.resize(gen() % 5);
            m->mSunHeights.mRun.resize(gen() % 5);
            m->mSamples.resize(gen() % 3);
            // A derived class's vtables (the destructor stores the manager's own back first).
            m->mVTable = reinterpret_cast<const void *>(uintptr_t(0x1100 + 8 * (gen() % 16)));
            m->mVTable4 = reinterpret_cast<const void *>(uintptr_t(0x2100 + 8 * (gen() % 16)));
            gEv.clear();
            gLogCount = 0;
            Blob &o = outcome[side];
            {
                difftest::AllocationCounter count;
                difftest::FreeRecorder frees;
                ctx.Mark(side ? "ours" : "original");
                if (side == 0) original(gObject);
                else Ours()->Destruct();
                count.Stop();
                difftest::WatchHeap(nullptr); // the test's own blocks from here on are not the code's
                frees.Stop();
                const unsigned freesBefore = gFreeCount;
                DumpHeap(o, count);
                o.push_back(uint32_t(frees.Count()));
                o.push_back(frees.Overflowed());
                for (size_t i = 0; i < frees.Count(); ++i) o.push_back(uint32_t(frees.Size(i)));
                size_t sizes[512];
                const size_t n = frees.SortedSizes(sizes, 512);
                for (size_t i = 0; i < n; ++i) o.push_back(uint32_t(sizes[i]));
                o.push_back(freesBefore);
            }
            o.push_back(gLogCount);
            o.insert(o.end(), gLog, gLog + gLogCount);
            o.insert(o.end(), gEv.begin(), gEv.end());
            DumpManager(o, false);
            CaptureFakes(o);
        }
        Same(ctx, outcome[0], outcome[1], "dtor", seed);
    }
}

// 0x2cb200 the light set copy (the push_back's: our LightingState copy constructor) from a set with drawn values and a
// name of drawn length (and spare capacity) into drawn bytes. Compared: the result, the copy's words (the name's buffer
// whole), the heap.
DIFF_TEST(sg_lighting_construct_copy_set, 0x2cb200)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x2cb200);
    alignas(16) static uint8_t source[sizeof(LightingState)];
    alignas(16) static uint8_t target[sizeof(LightingState)];
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 plan(seed * 7919u + 0x2cb200u);
        uint8_t fill[sizeof(target)], values[sizeof(LightingValues)];
        Fill(plan, fill, sizeof(fill));
        Fill(plan, values, sizeof(values));
        const uint32_t planSeed = plan();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(planSeed);
            StartSide();
            std::memcpy(source, fill, sizeof(source));
            LightingState *from = reinterpret_cast<LightingState *>(source)->Construct();
            from->mName.mString = Text(gen);
            if (gen() % 3 == 0) from->mName.mString.reserve(from->mName.mString.size() + gen() % 40);
            std::memcpy(&from->mValues, values, sizeof(values));
            from->mName.mRefCount = gen() % 4; // a named set others hold (the copy starts its own count)
            std::memcpy(target, fill, sizeof(target));
            Blob &o = outcome[side];
            void *result;
            {
                difftest::AllocationCounter count;
                ctx.Mark(side ? "ours" : "original");
                result = side == 0 ? original(target, from) : new (target) LightingState(*from);
                count.Stop();
                difftest::WatchHeap(nullptr); // the test's own blocks from here on are not the code's
                o.push_back(P(result) == P(target));
                DumpHeap(o, count);
            }
            DumpLightSet(o, target, true);
            reinterpret_cast<LightingState *>(target)->~LightingState();
            from->~LightingState();
        }
        Same(ctx, outcome[0], outcome[1], "copy_set", seed);
    }
}

} // namespace lg3f
