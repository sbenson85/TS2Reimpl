// Differential tests for nTSSG::cLightingManager's Shutdown and Init (lane 261110u, scenegraph LG3g;
// src/sg/cLightingManagerInit.cpp): Shutdown 0x2e2b60 (slot 4), Init 0x2d98c0 (slot 3). Hook shape: the original runs on
// the same bytes as ours.
//
// The world is the construct test's (tests/sg_lighting_construct.cpp): the game's operator new / sized delete patched to
// ours, the allocator service (the settings keys' strings allocate through it) under a heap watch that numbers the blocks
// and records each free, the fakes of tests/ts_services_fixture.h. Every fake that the code calls logs the manager's words
// (every pointer a fake or a block by identity, data as it is, the strings by size, capacity and text) at the call, so a
// store moved across a call fails.
//
// The eleven settings rows' name cells (0xffde38 + 12 * i, .data pointers the game's static initialisation fills) are set to
// the test's own C strings on both sides: the keys' text, length and the registration order are compared.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/LightingCommands.h"
#include "gz/cFileParser.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZString.h"
#include "sg/cDiffusionManager.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightingManager.h"
#include "sg/cLightingParserState.h"
#include "sg/cRenderGroupNameIterator.h"
#include "sg/cTextureManager.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace lg3g {
namespace {

using namespace svcfx;
using nTSSG::cBuiltRef;
using nTSSG::cLerpLightSet;
using nTSSG::cLightingManager;
using nTSSG::cPortalRecord;
using nTSSG::cSkySample;

constexpr uint32_t kObjectSize = 0x548;
constexpr uint32_t kWords = kObjectSize / 4;
constexpr uint32_t kBlockTag = 0xb0000000; // | allocation number << 16 | offset
constexpr uint32_t kHolderTag = 0xc0000000;
constexpr uint32_t kNameCells = 0xffde38, kNameStride = 12;
constexpr unsigned kNames = 11;
enum : uint32_t {
    evRelease = 0x70,
    evTexture = 0x71,
    evSample = 0x72,
    evBuilt = 0x73,
    evWalk = 0x74,
    evSlot4 = 0x75,
    evRegistry = 0x76,
    evFramework = 0x77,
};

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

// The +0x2c member: an object whose virtual base (a fake) the code reaches through its vbptr at +8.
struct Holder {
    uint32_t words[2];
    const int32_t *vbptr;
    uint32_t tail;
};
Holder gHolder;
int32_t gVbtable[3]; // [1] its cIGZUnknown, [2] its cIBaseParser face (Init's SetParser argument)

uint32_t Ident(uint32_t v)
{
    if (!v) return 0;
    for (unsigned i = 0; i < kFakes; ++i) {
        const uint32_t at = P(&F(i));
        if (v == at) return 200 + i;
        if (v > at && v < at + sizeof(Obj)) return normwords::kFakeInterior | i << 12 | (v - at);
    }
    for (unsigned k = gBlockCount; k-- > 0;)
        if (!gBlocks[k].reused && v >= gBlocks[k].lo && v < gBlocks[k].hi)
            return kBlockTag | k << 16 | uint32_t(v - gBlocks[k].lo);
    for (unsigned k = gBlockCount; k-- > 0;)
        if (!gBlocks[k].reused && v == gBlocks[k].hi) return kBlockTag | k << 16 | uint32_t(v - gBlocks[k].lo);
    if (v >= P(&gHolder) && v < P(&gHolder) + sizeof(gHolder)) return kHolderTag | (v - P(&gHolder));
    // The parser states' own tables (lane 261110y) as the game's that they stand for.
    using Base = cTSLightingParserState;
    using Derived = nTSSG::cLightingParserState;
    const struct {
        const void *ours;
        uint32_t game;
    } kTables[] = {
        {Base::kOwnVTable, Base::kVTable},           {Base::kOwnUnknownVTable, Base::kUnknownVTable},
        {Base::kOwnParserVTable, Base::kParserVTable}, {Base::kOwnVbTable, Base::kVbTable},
        {Base::kOwnParserFaceVbTable, Base::kParserFaceVbTable},
        {Derived::kOwnVTable, Derived::kVTable},     {Derived::kOwnUnknownVTable, Derived::kUnknownVTable},
        {Derived::kOwnParserVTable, Derived::kParserVTable}, {Derived::kOwnVbTable, Derived::kVbTable},
        {Derived::kOwnParserFaceVbTable, Derived::kParserFaceVbTable},
    };
    for (const auto &table : kTables)
        if (v == P(table.ours)) return P(GameAddress<void>(table.game));
    return v;
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

void StartSide()
{
    ResetAll();
    gEv.reserve(1u << 18);
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
    auto str = [&](uint32_t off) {
        p(off);
        for (uint32_t i = 1; i <= 6; ++i) t.k[off / 4 + i] = 'x';
    };
    p(0x000, 2);
    p(0x00c);
    p(0x018, 7);
    p(0x038, 2);
    for (uint32_t v : {0x08cu, 0x098u, 0x0a4u, 0x1f4u, 0x21cu, 0x2b0u}) p(v, 3);
    str(0x0b0);
    p(0x0d4);
    for (uint32_t v : {0x10cu, 0x15cu, 0x1acu}) str(v);
    p(0x214);
    p(0x228, 3);
    p(0x238, 2);
    for (uint32_t v : {0x248u, 0x254u, 0x260u, 0x2a0u}) p(v, 3);
    str(0x2d0);
    str(0x524);
    return t;
}
const KindTable kKinds = MakeKinds();
constexpr uint32_t kStrings[] = {0x0b0, 0x10c, 0x15c, 0x1ac, 0x2d0, 0x524};

template <class Out> void LogString(Out &&out, const uint8_t *at, bool full)
{
    uint32_t w[6];
    std::memcpy(w, at, sizeof(w));
    out(w[4]), out(w[5]);
    const bool large = w[5] > 15;
    const uint8_t *text = large ? reinterpret_cast<const uint8_t *>(uintptr_t(w[0])) : at;
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

// ---- the fakes' log ----
constexpr unsigned kLogWords = 1u << 19;
uint32_t gLog[kLogWords];
unsigned gLogCount = 0;
void Log(uint32_t v)
{
    if (gLogCount < kLogWords) gLog[gLogCount++] = v;
    else gOverflow = true;
}
// The manager's words (not the strings' unused bytes) at a call.
void Snapshot() { LogWords(&Log, gObject, false); }

struct Plan {
    uint32_t action[3];       // what the second parser / lit-object slot 4 / RemoveBuiltLights do to their member: 0 nothing, 1 null, 2 replace
    uint32_t replacement;     // the fake that replaces
    bool giveRegistry;        // the framework's lookup writes the registry
    uint32_t frameworkResult; // and returns this
    uint32_t registryResult;  // the registry's slot 5 returns this
    bool overwriteNext;       // each removal replaces the next name cell (an early read of the cells shows)
};
Plan gPlan;
unsigned gSlot4Calls = 0, gRegistryCalls = 0;
std::string gNames[2 * kNames];

uint32_t NameCell(unsigned i) { return kNameCells + kNameStride * i; }

void Act(uint32_t offset, uint32_t action)
{
    uint32_t value = 0;
    if (action == 0) return;
    if (action == 2) value = P(&F(gPlan.replacement));
    std::memcpy(gObject + offset, &value, 4);
}

uint32_t FakeRelease(Obj *self, uint32_t *)
{
    Log(evRelease), Log(self->id);
    Snapshot();
    return uint32_t(--self->refs);
}

// The parser's slot 4 (Shutdown), then the lit object's: the second call.
uint32_t Slot4(Obj *self, uint32_t *)
{
    const unsigned call = gSlot4Calls++;
    Log(evSlot4), Log(self->id), Log(call);
    Snapshot();
    if (call < 2) Act(call == 0 ? 0x30 : 0x18, gPlan.action[call]);
    return 0;
}

uint32_t FrameworkGet(Obj *, uint32_t *args)
{
    Log(evFramework);
    Snapshot();
    if (gPlan.giveRegistry) *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = P(&F(1));
    return gPlan.frameworkResult;
}

// The registry's slot 5 (RemoveNotification(target, id, key)): the target's offset in the manager, the id, the key's text.
uint32_t RegistryRemove(Obj *, uint32_t *args)
{
    Log(evRegistry), Log(args[0] - P(gObject)), Log(args[1]);
    LogString(&Log, reinterpret_cast<const uint8_t *>(uintptr_t(args[2])), false);
    Snapshot();
    const unsigned call = gRegistryCalls++;
    if (gPlan.overwriteNext && call + 1 < kNames) *GameAddress<uint32_t>(NameCell(call + 1)) = P(gNames[kNames + call + 1].c_str());
    return gPlan.registryResult;
}

void __fastcall StandTexture(uint32_t *handle, void *)
{
    Log(evTexture), Log(Ident(P(handle))), Log(*handle);
    Snapshot();
}
void __fastcall StandSample(void *self, void *)
{
    Log(evSample), Log(P(self) - P(gObject));
    Snapshot();
}
void __fastcall StandBuilt(void *self, void *)
{
    Log(evBuilt), Log(P(self) - P(gObject));
    Snapshot();
    Act(0x28, gPlan.action[2]);
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Log(evWalk), Log(P(self) - P(gObject)), Log(arg - P(gObject)), Log(P(obj));
    Snapshot();
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Log(id);
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

const uint32_t kLengths[] = {0, 1, 4, 14, 15, 16, 31, 40};
std::string ShortText(std::mt19937 &gen)
{
    std::string s(kLengths[gen() % 8], 'a');
    for (char &c : s) c = char('a' + gen() % 26);
    return s;
}
std::string LongText(std::mt19937 &gen)
{
    static const uint32_t kBig[] = {0x1000, 0x1100};
    std::string s(gen() % 8 ? kLengths[gen() % 8] : kBig[gen() % 2], 'a');
    for (char &c : s) c = char('a' + gen() % 26);
    return s;
}

// The two name sets (the cells' own text, then the text a cell is replaced with after a removal): drawn before the side's
// heap watch starts, so their blocks are not the code's.
void DrawNames(std::mt19937 &gen)
{
    for (std::string &name : gNames) {
        name.assign(kLengths[gen() % 8] + (gen() % 3 == 0 ? 20 : 0), 'a');
        for (char &c : name) c = char('a' + gen() % 26);
    }
}

} // namespace

// 0x2e2b60 Shutdown over a manager our constructor built and the test filled: every counted pointer a fake or null (+0x2c
// through its virtual base; the parser always a fake: the original does not test it), the sky samples, the path string of
// every length, the guard byte clear or any other value (every other byte drawn), the framework present or not and its
// lookup giving the registry or not (and a drawn result), the registry's answers, the cells replaced after each removal,
// and the parser's, lit object's and scene manager's members changed by the calls made on them (the code reads them again).
// The texture handle's release 0xae8710, RemoveSampleLights 0x2dffd0, RemoveBuiltLights 0x2dff00 and the key-map walk
// 0x1c081e are stood in on both sides. Compared: the result, each call made (the fake, its arguments, the manager's words at
// that moment), the keys' text, the frees in order (by block), the free recorder's sizes, the heap counts, the words after,
// every fake's count.
DIFF_TEST(sg_lighting_init_shutdown, 0x2e2b60)
{
    Fixture fixture;
    Globals globals;
    shape::StandIn texture{0xae8710, &nGZSceneGraph::cTextureHandle::Reset, &StandTexture};
    shape::StandIn textureDestroy{0xae82b0, &nGZSceneGraph::cTextureHandle::Destroy, &StandTexture}; // the cleanup's Destruct
    shape::StandIn sample{0x2dffd0, &cLightingManager::RemoveSampleLights, &StandSample};
    shape::StandIn built{0x2dff00, &cLightingManager::RemoveBuiltLights, &StandBuilt};
    shape::StandIn walk6{0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6};
    auto original = ctx.Original<bool(__thiscall *)(void *)>(0x2e2b60);
    constexpr uint32_t kMembers[] = {0x18, 0x1c, 0x20, 0x24, 0x28, 0x38, 0x3c, 0x228, 0x22c, 0x230, 0x238, 0x23c};
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 plan(seed * 7919u + 0x2e2b60u);
        uint8_t fill[kObjectSize];
        Fill(plan, fill, sizeof(fill));
        const uint32_t planSeed = plan();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(planSeed);
            DrawNames(gen);
            // The plan and the globals before the heap watch starts (the globals' own vector grows).
            gPlan = Plan{};
            for (uint32_t &a : gPlan.action) a = gen() % 3;
            gPlan.replacement = 2 + gen() % (kFakes - 2);
            gPlan.giveRegistry = gen() % 4 != 0;
            gPlan.frameworkResult = gen() % 2;
            gPlan.registryResult = gen() % 2;
            gPlan.overwriteNext = gen() % 2;
            gSlot4Calls = 0, gRegistryCalls = 0;
            const bool framework = gen() % 5 != 0;
            globals.Set(0x10c9378, framework ? &F(0) : nullptr);
            for (unsigned i = 0; i < kNames; ++i) globals.SetWord(NameCell(i), P(gNames[i].c_str()));
            StartSide();
            for (unsigned i = 0; i < kFakes; ++i) {
                F(i).Set(1, 0u, 0u, &FakeRelease);
                F(i).Set(2, 0u, 0u, &FakeRelease);
                F(i).Set(4, 0u, 0u, &Slot4);
            }
            F(0).Set(5, "ddp", 0u, &FrameworkGet);
            F(1).Set(5, "xdx", 0u, &RegistryRemove);

            std::memcpy(gObject, fill, sizeof(fill));
            cLightingManager *m = Ours()->Construct();
            auto fake = [&]() -> void * { return gen() % 4 ? &F(2 + gen() % (kFakes - 2)) : nullptr; };
            for (uint32_t off : kMembers) {
                void *f = fake();
                std::memcpy(gObject + off, &f, 4);
            }
            m->mFileParser = static_cast<nGZCommandParser::cIFileParser *>(static_cast<void *>(&F(2 + gen() % (kFakes - 2))));
            if (gen() % 3) {
                gVbtable[0] = int32_t(gen());
                gVbtable[1] = int32_t(P(&F(2 + gen() % (kFakes - 2))) - P(&gHolder.vbptr));
                gHolder.vbptr = gVbtable;
                m->mRef2C = reinterpret_cast<uint8_t *>(&gHolder);
            } else {
                m->mRef2C = nullptr;
            }
            m->mShadeTexture = gen() % 3 ? gen() : 0xffffffff;
            for (uint32_t n = gen() % 4; n; --n) m->mMessageTypes.insert(gen() % 64);
            m->mName = ShortText(gen);
            m->mObjectSettings.mText.mString = ShortText(gen);
            m->mSimSettings.mText.mString = ShortText(gen);
            m->mWallSettings.mText.mString = ShortText(gen);
            reinterpret_cast<LightingState *>(&m->mDefaultSet)->mName.mString = ShortText(gen);
            m->mLightingPath = LongText(gen);
            const uint32_t samples = gen() % 4;
            m->mSkySamples.reserve(samples + gen() % 3);
            for (uint32_t s = 0; s < samples; ++s) {
                cSkySample &one = m->mSkySamples.emplace_back();
                one.mLights.resize(gen() % 3);
                for (cBuiltRef &ref : one.mLights) ref.mObject = fake();
                one.mLerp78.mSet = static_cast<cLerpLightSet *>(fake());
                one.mPortals.resize(gen() % 3);
                for (cPortalRecord &record : one.mPortals) record.mObject.mObject = fake(), record.mOther.mObject = fake();
                one.mLerp88.mSet = static_cast<cLerpLightSet *>(fake());
            }
            // The guard byte: clear or any other value; the bytes after it, the flag, and the other bytes drawn.
            gObject[0x14] = gen() % 3 ? uint8_t(1 + gen() % 255) : 0;
            gObject[0x15] = uint8_t(gen()), gObject[0x16] = uint8_t(gen()), gObject[0x17] = uint8_t(gen());
            gObject[0x69] = uint8_t(gen());
            gEv.clear();
            gLogCount = 0;
            Blob &o = outcome[side];
            bool result;
            {
                difftest::AllocationCounter count;
                difftest::FreeRecorder frees;
                ctx.Mark(side ? "ours" : "original");
                result = side == 0 ? original(gObject) : Ours()->Shutdown();
                count.Stop();
                difftest::WatchHeap(nullptr);
                frees.Stop();
                const unsigned freesBefore = gFreeCount;
                o.push_back(result);
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
            DumpManager(o, true);
            CaptureFakes(o);
            Ours()->Destruct();
        }
        Same(ctx, outcome[0], outcome[1], "shutdown", seed);
    }
}

// ---- Init (0x2d98c0) ----
namespace {

enum : uint32_t {
    evWalk5 = 0x80,
    evMaker = 0x81,
    evService = 0x82,
    evNotify = 0x83,
    evBit = 0x84,
    evAdd = 0x85,
    evHandle = 0x86,
    evDiffusion = 0x87,
    evLerp = 0x88,
    evKeys = 0x89,
    evPool = 0x8a,
    evRef = 0x8b,
    evGetter = 0x8c,
};
// The fakes' parts: F(0) the service framework (0x10c9378), F(1) the director's framework (its slot 3 the COM maker),
// F(2) the settings service, F(3) the render-group registry, F(4) the texture manager; F(5) on the objects made and held.
constexpr unsigned kDirector = 1, kSettings = 2, kRegistry = 3, kTextureManager = 4, kPool = 5;
constexpr uint32_t kWorldCell = 0x10cc298; // 0x20e0b2's cache
constexpr uint32_t kFadeByte = 0xffde34;
constexpr uint32_t kSettingsService = 0x0ac1887e, kWorldService = 0x4a037ee3;
constexpr uint32_t kStateWords = nTSSG::cLightingParserState::kSize / 4;

// A word written into the manager by whichever fake or stand-in is called `at`-th (1 the first): members the code reads
// again after a call are seen to be read again.
struct Perturb {
    uint32_t at, offset, value;
};
struct InitPlan {
    Perturb perturb[16];
    bool giveSettings; // the framework's lookup writes the settings service
    bool giveWorld;    // and the world database's (0x20e0b2's, through 0x20dd6f)
    uint32_t worldFake;
};
InitPlan gInit;
std::mt19937 gAnswers; // the fakes' answers, drawn in call order from the side's own seed
unsigned gCalls = 0, gLerpCalls = 0;
bool gActive = false;   // inside the call (the clean-up's calls perturb nothing)
void *gLeaked[8];       // the blocks the stood-in constructors were handed (freed after the side)
unsigned gLeakCount = 0;
uint8_t *gStateBlock = nullptr; // the parser state's block (0x43c bytes)
const void *gTemp = nullptr;    // the temporary texture handle (InitFromId's)

void Tick()
{
    if (!gActive) return;
    ++gCalls;
    for (const Perturb &p : gInit.perturb)
        if (p.at == gCalls) std::memcpy(gObject + p.offset, &p.value, 4);
}

uint32_t PoolFake() { return P(&F(kPool + gAnswers() % (kFakes - kPool))); }
uint32_t MemberWord(uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, gObject + offset, 4);
    return v;
}

// A location: inside the manager (its offset), a fake or a block (Ident), else the code's stack.
uint32_t Where(const void *p)
{
    const uint32_t v = P(p);
    if (v >= P(gObject) && v < P(gObject) + kObjectSize) return 0x0b000000 | (v - P(gObject));
    const uint32_t id = Ident(v);
    return id != v ? id : 0x5ac00000;
}

void LogText(const char *text)
{
    const uint32_t n = uint32_t(std::strlen(text));
    Log(n);
    for (uint32_t i = 0; i <= n; i += 4) {
        uint32_t word = 0;
        std::memcpy(&word, text + i, n + 1 - i < 4 ? n + 1 - i : 4);
        Log(word);
    }
}

void Leak(void *block)
{
    if (gLeakCount < 8) gLeaked[gLeakCount++] = block;
    else gOverflow = true;
}

void Allocated43c(void *p, size_t bytes)
{
    Allocated(p, bytes);
    if (bytes == nTSSG::cLightingParserState::kSize) gStateBlock = static_cast<uint8_t *>(p);
}
const difftest::HeapWatcher gInitWatch{&Allocated43c, &Resized, &Freed};

uint32_t RefUp(Obj *self, uint32_t *)
{
    Log(evRef), Log(self->id), Log(1);
    Snapshot();
    Tick();
    return uint32_t(++self->refs);
}
uint32_t RefDown(Obj *self, uint32_t *)
{
    Log(evRef), Log(self->id), Log(2);
    Snapshot();
    Tick();
    return uint32_t(--self->refs);
}
// The director framework asked for (coordinator mutants LV: the getter comes before the old member's release).
void DirectorAsked()
{
    if (!gActive) return;
    Log(evRef), Log(0xd12ec7), Log(3);
    Snapshot();
}
// The made objects' slots: the cDiffusionManager's and the file parser's Init (3), SetParser (5), SetLight (93).
uint32_t PoolCall(Obj *self, uint32_t *)
{
    Log(evPool), Log(self->id);
    Snapshot();
    Tick();
    return gAnswers() % 2;
}

// The director's slot 3 (GetClassObject(class, iid, out)): out's location and value before, then a fake written or not
// (always for +0x30 and +0x238: the original calls them unchecked), and a drawn answer (not read).
uint32_t Maker(Obj *, uint32_t *args)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(args[2]));
    Log(evMaker), Log(Where(out)), Log(Ident(*out));
    Snapshot();
    const bool must = out == reinterpret_cast<uint32_t *>(gObject + 0x30) || out == reinterpret_cast<uint32_t *>(gObject + 0x238);
    if (must || gAnswers() % 3) *out = PoolFake();
    Tick();
    return gAnswers() % 2;
}

// The service framework's slot 5 (GetSystemService(id, iid, out)).
uint32_t Service(Obj *, uint32_t *args)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(args[2]));
    Log(evService), Log(Where(out)), Log(Ident(*out));
    Snapshot();
    if (args[0] == kSettingsService && gInit.giveSettings) *out = P(&F(kSettings));
    if (args[0] == kWorldService && gInit.giveWorld) *out = gInit.worldFake;
    Tick();
    return gAnswers() % 2;
}

// The settings service's slot 4 (AddNotification(target, id, key, default)): the target's offset in the manager, the
// key's text and the default's RVA.
uint32_t Notify(Obj *, uint32_t *args)
{
    Log(evNotify), Log(args[0] - P(gObject));
    LogString(&Log, reinterpret_cast<const uint8_t *>(uintptr_t(args[2])), false);
    Log(args[3] - P(GameAddress<uint8_t>(0)));
    Snapshot();
    Tick();
    return gAnswers() % 2;
}

// The registry's slot 3 (BitOfName(name)): the name's text; a bit from 0 to 63 (the code keeps its low five bits).
uint32_t Bit(Obj *, uint32_t *args)
{
    Log(evBit);
    LogText(reinterpret_cast<const char *>(uintptr_t(args[0])));
    Snapshot();
    Tick();
    return gAnswers() % 64;
}

// The texture manager's slot 20 (Add(image, name, flags)): the name's text; a drawn id.
uint32_t Add(Obj *, uint32_t *args)
{
    Log(evAdd);
    LogText(reinterpret_cast<const char *>(uintptr_t(args[1])));
    Snapshot();
    Tick();
    return gAnswers();
}

void __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Log(evWalk5), Log(P(self) - P(gObject)), Log(arg - P(gObject)), Log(P(obj));
    Snapshot();
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Log(id);
    Tick();
}
// The cDiffusionManager's constructor 0x3c4140: its block (kept, freed after the side); the object it answers, a fake
// (or +0x18's own when set: the assignment's equal case).
void *__fastcall StandDiffusion(void *memory, void *)
{
    Log(evDiffusion), Log(Ident(P(memory)));
    Snapshot();
    Leak(memory);
    Tick();
    const uint32_t old = MemberWord(0x18);
    return reinterpret_cast<void *>(uintptr_t(old && gAnswers() % 4 == 0 ? old : PoolFake()));
}
// cLerpLightSet's constructor 0x3c0ec0 (first for +0x22c, then +0x230): likewise.
void *__fastcall StandLerp(void *self, void *)
{
    Log(evLerp), Log(Ident(P(self)));
    Snapshot();
    Leak(self);
    Tick();
    const uint32_t old = MemberWord(gLerpCalls++ == 0 ? 0x22c : 0x230);
    return reinterpret_cast<void *>(uintptr_t(old && gAnswers() % 4 == 0 ? old : PoolFake()));
}
void __fastcall StandKeys(void *self, void *, uint32_t tag, uint32_t flag)
{
    Log(evKeys), Log(Ident(P(self))), Log(tag), Log(flag);
    Snapshot();
    Tick();
}
void *__cdecl StandTextureManager()
{
    Log(evGetter), Log(0);
    Snapshot();
    Tick();
    return &F(kTextureManager);
}
void *__cdecl StandRegistry()
{
    Log(evGetter), Log(1);
    Snapshot();
    Tick();
    return &F(kRegistry);
}
// The texture handle's InitFromId / Assign / Destroy: where each runs (the temporary by identity), the ids.
uint32_t *__fastcall StandInitFromId(uint32_t *handle, void *, uint32_t id)
{
    Log(evHandle), Log(0), Log(Where(handle)), Log(id);
    Snapshot();
    gTemp = handle;
    *handle = id ^ 0x5a5a5a5a;
    Tick();
    return handle;
}
uint32_t *__fastcall StandAssign(uint32_t *handle, void *, const uint32_t *other)
{
    Log(evHandle), Log(1), Log(Where(handle)), Log(other == gTemp), Log(*handle), Log(*other);
    Snapshot();
    *handle = *other;
    Tick();
    return handle;
}
void __fastcall StandDestroy(uint32_t *handle, void *)
{
    Log(evHandle), Log(2), Log(handle == gTemp), Log(Where(handle)), Log(*handle);
    Snapshot();
    Tick();
}

void DestroyState(void *object)
{
    // The primary vtable's slot 0, the deleting destructor (1: delete): the game's on both sides.
    (*reinterpret_cast<void *(__thiscall *const *const *)(void *, uint32_t)>(object))[0](object, 1);
}

} // namespace

// 0x2d98c0 Init over a manager our constructor built and the test filled: the guard byte clear or any other value (every
// other byte drawn), the byte 0xffde34 clear or set, the framework present or not, its lookups writing the settings
// service and the world database or not (the world's cache 0x10cc298 set or not), the director's makes writing a fake or
// not (+0x30's and +0x238's always: the original calls them unchecked), every old member a fake or null (+0x2c through
// its virtual base), the stood-in constructors answering a fresh fake or the member's own, the registry's bits, and up to
// sixteen words of the manager written by the n-th call (half within the first 16 calls) (members read again after a call). Stood in on both sides: the
// key-map walk 0x1c0458, the cDiffusionManager's constructor 0x3c4140, cLerpLightSet's constructor 0x3c0ec0 and
// SetKeys 0x3c2e30, the texture manager 0xae8750 and the registry 0xaeb370 getters, the texture handle's InitFromId
// 0xae8260, Assign 0xae82e0 and Destroy 0xae82b0. The parser state's construction runs (the original's 0xb3ce70 against
// ours). Compared: the result, each call (its arguments, the manager's words at it), the heap (blocks, frees, counts),
// the words after, the parser state's every word (blocks by identity), every fake's count.
DIFF_TEST(sg_lighting_init_init, 0x2d98c0)
{
    Fixture fixture;
    Globals globals;
    shape::StandIn walk5{0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5};
    shape::StandIn diffusion{0x3c4140, &nTSSG::cDiffusionManager::Construct, &StandDiffusion};
    shape::StandIn lerp{0x3c0ec0, &cLerpLightSet::Construct, &StandLerp};
    shape::StandIn keys{0x3c2e30, &cLerpLightSet::SetKeys, &StandKeys};
    shape::StandIn manager{0xae8750, &nGZSceneGraph::TextureManager, &StandTextureManager};
    shape::StandIn registry{0xaeb370, &nGZSceneGraph::RenderGroupRegistry, &StandRegistry};
    shape::StandIn initFromId{0xae8260, &nGZSceneGraph::cTextureHandle::InitFromId, &StandInitFromId};
    shape::StandIn assign{0xae82e0, &nGZSceneGraph::cTextureHandle::Assign, &StandAssign};
    shape::StandIn destroy{0xae82b0, &nGZSceneGraph::cTextureHandle::Destroy, &StandDestroy};
    auto original = ctx.Original<bool(__thiscall *)(void *)>(0x2d98c0);
    constexpr uint32_t kMembers[] = {0x18, 0x1c, 0x22c, 0x230, 0x238, 0x23c};
    constexpr uint32_t kPerturbed[] = {0x18, 0x1c, 0x2c, 0x30, 0x238, 0x23c, 0x22c, 0x230,
                                       0x50c, 0x510, 0x514, 0x518, 0x51c, 0x520};
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 plan(seed * 7919u + 0x2d98c0u);
        uint8_t fill[kObjectSize];
        Fill(plan, fill, sizeof(fill));
        const uint32_t planSeed = plan();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(planSeed);
            DrawNames(gen);
            auto pool = [&]() { return P(&F(kPool + gen() % (kFakes - kPool))); };
            gInit = InitPlan{};
            for (Perturb &p : gInit.perturb) {
                p.at = 1 + gen() % (gen() % 2 ? 16 : 64); // (half among the first calls: the member re-reads)
                p.offset = kPerturbed[gen() % (sizeof(kPerturbed) / 4)];
                const bool nullable = p.offset == 0x1c || p.offset == 0x23c || p.offset == 0x22c || p.offset == 0x230;
                if (p.offset >= 0x50c) p.value = gen();
                else if (p.offset == 0x2c) p.value = gen() % 2 ? P(&gHolder) : 0;
                else p.value = nullable && gen() % 3 == 0 ? 0 : pool();
            }
            const bool framework = gen() % 4 != 0;
            gInit.giveSettings = gen() % 4 != 0;
            gInit.giveWorld = gen() % 2;
            gInit.worldFake = pool();
            const bool worldCached = !framework || gen() % 2; // (0x20dd6f does not test the framework)
            uint32_t fade = gen();
            if (gen() % 3 == 0) fade &= ~0xffu;
            else if (!(fade & 0xff)) fade |= 1 + gen() % 255;
            // The globals before the heap watch starts (the globals' own vector grows).
            globals.Set(0x10c9378, framework ? &F(0) : nullptr);
            globals.SetWord(kWorldCell, worldCached ? pool() : 0);
            globals.SetWord(kFadeByte, fade);
            for (unsigned i = 0; i < kNames; ++i) globals.SetWord(NameCell(i), P(gNames[i].c_str()));
            gFrameworkIndex = kDirector;
            svcfx::gOnDirectorFramework = &DirectorAsked;
            gAnswers.seed(gen());
            gCalls = 0, gLerpCalls = 0, gLeakCount = 0, gStateBlock = nullptr, gTemp = nullptr;
            StartSide();
            difftest::WatchHeap(&gInitWatch);
            for (unsigned i = 0; i < kFakes; ++i) {
                F(i).Set(1, 0u, 0u, &RefUp);
                F(i).Set(2, 0u, 0u, &RefDown);
            }
            for (unsigned i = kPool; i < kFakes; ++i) {
                F(i).Set(3, 0u, 0u, &PoolCall);
                F(i).Set(5, "pd", 0u, &PoolCall);
                F(i).Set(93, "p", 0u, &PoolCall);
            }
            F(0).Set(5, "ddx", 0u, &Service);                 // out logged by the custom
            F(kDirector).Set(3, "ddx", 0u, &Maker);           // likewise
            F(kSettings).Set(4, "xdxx", 0u, &Notify);         // target, key and default logged by the custom
            F(kRegistry).Set(3, "x", 0u, &Bit);               // the name's text, by the custom
            F(kTextureManager).Set(20, "pxd", 0u, &Add);      // likewise

            std::memcpy(gObject, fill, sizeof(fill));
            cLightingManager *m = Ours()->Construct();
            auto fake = [&]() -> uint32_t { return gen() % 4 ? pool() : 0; };
            for (uint32_t off : kMembers) {
                const uint32_t f = fake();
                std::memcpy(gObject + off, &f, 4);
            }
            const uint32_t parser = fake();
            std::memcpy(gObject + 0x30, &parser, 4);
            gVbtable[0] = int32_t(gen());
            gVbtable[1] = int32_t(pool() - P(&gHolder.vbptr));
            gVbtable[2] = int32_t(pool() - P(&gHolder.vbptr));
            gHolder.vbptr = gVbtable;
            m->mRef2C = gen() % 3 ? reinterpret_cast<uint8_t *>(&gHolder) : nullptr;
            m->mShadeTexture = gen();
            for (uint32_t n = gen() % 4; n; --n) m->mMessageTypes.insert(gen() % 64);
            gObject[0x14] = gen() % 4 ? 0 : uint8_t(1 + gen() % 255);
            gObject[0x15] = uint8_t(gen()), gObject[0x16] = uint8_t(gen()), gObject[0x17] = uint8_t(gen());
            gEv.clear();
            gLogCount = 0;
            Blob &o = outcome[side];
            {
                difftest::AllocationCounter count;
                difftest::FreeRecorder frees;
                ctx.Mark(side ? "ours" : "original");
                gActive = true;
                const bool result = side == 0 ? original(gObject) : Ours()->Init();
                gActive = false;
                svcfx::gOnDirectorFramework = nullptr;
                count.Stop();
                difftest::WatchHeap(nullptr);
                frees.Stop();
                o.push_back(result);
                DumpHeap(o, count);
                o.push_back(uint32_t(frees.Count()));
                o.push_back(frees.Overflowed());
                for (size_t i = 0; i < frees.Count(); ++i) o.push_back(uint32_t(frees.Size(i)));
            }
            o.push_back(gCalls);
            o.push_back(gLogCount);
            o.insert(o.end(), gLog, gLog + gLogCount);
            o.insert(o.end(), gEv.begin(), gEv.end());
            DumpManager(o, true);
            // The parser state: every word (vtables as the image's addresses, blocks by identity).
            o.push_back(gStateBlock != nullptr);
            if (gStateBlock)
                for (uint32_t i = 0; i < kStateWords; ++i) o.push_back(Ident(reinterpret_cast<const uint32_t *>(gStateBlock)[i]));
            CaptureFakes(o);
            // The clean-up, outside the counts and the same on both sides.
            if (gStateBlock) {
                if (m->mRef2C == gStateBlock) m->mRef2C = nullptr;
                DestroyState(gStateBlock);
            }
            for (unsigned i = 0; i < gLeakCount; ++i) ::operator delete(gLeaked[i]);
            Ours()->Destruct();
        }
        Same(ctx, outcome[0], outcome[1], "init", seed);
    }
}

// 0xb3ce70, cTSLightingParserState's constructor, directly: the whole object (any flag but 0; 0x418 bytes) and the base
// part of a nTSSG::cLightingParserState (flag 0; 0x43c bytes, the vbptrs and the virtual bases' tables stored first as
// Init stores them), over drawn bytes. Compared: the result, the heap, every word of the block after (vtables as the
// image's addresses, blocks by identity). Destroyed through the primary vtable's deleting destructor on both sides.
DIFF_TEST(sg_lighting_init_parser_state, 0xb3ce70)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xb3ce70);
    for (uint32_t seed = 0; seed < 64; ++seed) {
        std::mt19937 plan(seed * 7919u + 0xb3ce70u);
        const bool whole = seed % 2 == 0;
        uint32_t flag = 0;
        if (whole)
            while (!flag) flag = seed % 4 == 0 ? 1 : uint32_t(plan());
        const uint32_t size = whole ? 0x418 : nTSSG::cLightingParserState::kSize;
        uint8_t fill[nTSSG::cLightingParserState::kSize];
        Fill(plan, fill, size);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            StartSide();
            gEv.clear();
            uint8_t *const block = static_cast<uint8_t *>(::operator new(size));
            std::memcpy(block, fill, size);
            auto store = [&](uint32_t offset, uint32_t value) { std::memcpy(block + offset, &value, 4); };
            if (!whole) {
                const auto *parserVbtable = GameAddress<const int32_t>(0xdb6064);
                store(8, P(GameAddress<void>(0xdb6058)));
                store(0x438, P(parserVbtable));
                store(0x42c, P(GameAddress<void>(0xd9b428)));
                store(0x434, P(GameAddress<void>(0xdb574c)));
                store(0x438 + parserVbtable[1], P(GameAddress<void>(0xdb581c)));
            }
            Blob &o = outcome[side];
            {
                difftest::AllocationCounter count;
                difftest::FreeRecorder frees;
                ctx.Mark(side ? "ours" : "original");
                void *const result = side == 0 ? original(block, flag)
                                               : reinterpret_cast<cTSLightingParserState *>(block)->Construct(flag);
                count.Stop();
                difftest::WatchHeap(nullptr);
                frees.Stop();
                o.push_back(Ident(P(result)));
                DumpHeap(o, count);
                o.push_back(uint32_t(frees.Count()));
            }
            for (uint32_t i = 0; i < size / 4; ++i) o.push_back(Ident(reinterpret_cast<const uint32_t *>(block)[i]));
            o.insert(o.end(), gEv.begin(), gEv.end());
            DestroyState(block);
        }
        Same(ctx, outcome[0], outcome[1], "parser state", seed);
    }
}

} // namespace lg3g
