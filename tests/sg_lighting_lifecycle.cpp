// Differential tests for nTSSG::cLightingManager's lifecycle leaves (lane 261110o, scenegraph LG3b part 1;
// src/sg/cLightingManagerLife.cpp): slot 7 ReleaseWorld 0x2d3820, slot 9 ReleaseLot 0x2d3910, slot 8
// SetupNeighborhood 0x2d1300, slot 22 FindLightingFile 0x2d0db0 and ApplyLightSet 0x2d0730. Hook shape: the original
// runs on the same object image as ours.
//
// The world (one draw for both sides): the manager is 0x548 random bytes with its vtable a fake, the lit object (+0x18;
// null only where the original checks it), the terrain view (+0x1c: drawn floats, NaN, infinities, out of the int64
// range, and words), the world, the lightmaps and the scene manager (+0x20, +0x24, +0x28), the shade and ambient images
// (+0x38, +0x3c) counted fakes or null, the parser (+0x30; null where the original checks it), 0..3 light sets (+0x98,
// drawn bytes, mShade198 drawn) with the current index (+0xd0) in and out of range, real sky samples (+0x1f4: lights,
// two lerp references, portal records), the lerp set (+0x228) a fake or null, mSamples (+0x2b0) over a static array,
// mSunFalloff (+0x2ac) at, near and away from the epsilon 0xdaaa28, and mLightingPath (+0x528) a real std::string
// (empty, in place, on the heap). The byte 0xffde34 is drawn (saved and restored). The fakes are
// tests/sg_lighting_fakes.h's: the builders (stand-ins on both sides), the lerp set's Construct 0x3c0ec0 / SetKeys
// 0x3c2e30, the system getter 0x20defb (slot 48 the lightmaps: null, the current ones, others), and lightfx::life's
// terrain view, parser, lit object, message server, RZMemory heap, ambient evaluations (drawn colours: NaN, negative,
// around 0 and 1, tiny) and the option lists / PathExists (drawn paths with and without a separator, drawn answers).
// The image's operator new / delete (0x824632 / 0x824624) are ours on both sides, so the std::string and vector code
// the original runs from the image shares our heap.
//
// Compared: the result, the fakes' log (messages by their words, heap blocks by number), every word of the manager (the
// sky samples and the path by content: size, capacity, text, the in-place buffer), the counts of the fakes, the byte
// 0xffde34, and the heap blocks and bytes each side allocated; at the x87's 53-bit and 24-bit precision.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_fakes.h"

#include "rz/RZMessageServer.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightingManager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using nTSSG::cLerpLightSet;
using nTSSG::cLightingManager;
using nTSSG::cLightSample;
using nTSSG::cLightSet;
using nTSSG::cPortalRecord;
using nTSSG::cSkySample;

using lightfx::FakeRef;
using lightfx::gLerps;
using lightfx::gNewLerpCount;
using lightfx::gNewLerps;
using lightfx::kLerps;
namespace life = lightfx::life;

constexpr uint32_t kManagerSize = 0x548;
constexpr uint32_t kFadeSwitch = 0xffde34;
constexpr uint32_t kEpsilon = 0xdaaa28;
constexpr int kRefs = 8;
constexpr int kSampleSpace = 6;

enum Fn { kReleaseWorld, kReleaseLot, kSetup, kFind, kApply };

alignas(16) uint8_t gManager[kManagerSize];
alignas(16) cLightSet gSets[3];
cLightSample gSampleSpace[kSampleSpace];
FakeRef gRefs[kRefs];

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kRefs; ++i)
        if (p == &gRefs[i]) return 0x100 + i;
    for (int i = 0; i < kLerps; ++i)
        if (p == &gLerps[i]) return 0x1e0 + i;
    for (uint32_t i = 0; i < gNewLerpCount && i < std::size(gNewLerps); ++i)
        if (p == gNewLerps[i]) return 0x3c00 + i;
    if (p == &life::gParser) return 0x9a00;
    if (p == &life::gLit) return 0x1100;
    if (p == gManager) return 0x5000;
    return life::RzId(p);
}

void *__cdecl GameNew(size_t n) { return ::operator new(n ? n : 1); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

std::vector<cSkySample> &SkySamples() { return *reinterpret_cast<std::vector<cSkySample> *>(gManager + 0x1f4); }
std::string &Path() { return *reinterpret_cast<std::string *>(gManager + 0x528); }
cLightingManager *Manager() { return reinterpret_cast<cLightingManager *>(gManager); }

double Epsilon() { return *GameAddress<const double>(kEpsilon); }

// A float for the comparisons against the epsilon: 0, the epsilon and its neighbours, tiny, negative, NaN, or wide.
float NearEpsilon(std::mt19937 &gen)
{
    const float e = float(Epsilon());
    switch (gen() % 8) {
    case 0: return 0.0f;
    case 1: return e;
    case 2: return Fl(U(e) + 1);
    case 3: return Fl(U(e) - 1);
    case 4: return -e;
    case 5: return NaN(gen);
    default: return Wide(gen, 2.0f);
    }
}

// An ambient colour component: around Shape's corners (0 and 1), the epsilon, NaN, wide.
float Colour(std::mt19937 &gen)
{
    static const float kCorners[] = {0.0f, -0.0f, 1.0f, 0.5f, 2.0f, -1.0f, 0.999f, 1.001f};
    switch (gen() % 4) {
    case 0: return kCorners[gen() % std::size(kCorners)];
    case 1: return NearEpsilon(gen);
    default: return Wide(gen, 1.5f);
    }
}

// The terrain's x87 answers: plain counts, fractions, negatives, 2^31 and up, past the int64 range, NaN, infinities.
float ViewFloat(std::mt19937 &gen)
{
    // (between 2^32 and 2^63 the low word kept is not zero: 1.5 * 2^32, 2^32 + 1024, 2^40 + 2^17)
    static const float kEdges[] = {0.0f, -0.0f, 0.5f, -0.5f, 1.0f, 64.0f, 2147483648.0f, 4294967296.0f, 4294967295.0f,
                                   6442450944.0f, -6442450944.0f, 4294968320.0f, 1099511758848.0f, -3e18f,
                                   9223372036854775808.0f, -9223372036854775808.0f, 1e30f, -1e30f, 0.75f, -1.75f};
    switch (gen() % 6) {
    case 0: return kEdges[gen() % std::size(kEdges)];
    case 1: return Wide(gen, 1e6f);
    case 2: return float(gen() % 300) + float(gen() % 4) * 0.25f;
    default: return Wide(gen, 100.0f);
    }
}

const char *const kTexts[] = {"", "a", "lighting.txt", "fifteen chars..", "sixteen chars...",
                              "C:\\Program Files\\EA GAMES\\The Sims 2\\TSData\\Res\\Lighting\\"};
// The separator slot 22 cuts after is "/" (0xda52b4); a "\\" path is never cut.
const char *const kPaths[] = {"C:/Games/Lighting/", "D:/x/y/lights.txt", "noslash", "/", "", "a/b",
                              "C:/a much longer directory name/with more/levels/file.cfg", "x/", "C:\\Games\\Lighting\\",
                              "a\\b/c"};
const char *const kNames[] = {"Lighting.txt", "", "neighborhood\\day.txt", "a name long enough for the heap.txt"};

void *DrawRef(std::mt19937 &gen, uint32_t nullOdds) { return gen() % nullOdds ? &gRefs[gen() % kRefs] : nullptr; }

void FillSet(std::mt19937 &gen, cLightSet &set)
{
    auto *const bytes = reinterpret_cast<uint8_t *>(&set);
    for (uint32_t i = 0; i < sizeof(cLightSet); ++i) bytes[i] = uint8_t(gen());
    // (a NaN one time in six, so that both factors of the shade are NaNs now and then: the first one's payload wins)
    set.mShade198 = gen() % 6 == 0 ? NaN(gen) : gen() % 3 ? Nice(gen, 2.0f) : Wide(gen, 4.0f);
}

// The world for function `fn` from `draw` (the same on both sides); the name slot 22 looks for.
const char *Draw(uint32_t draw, Fn fn)
{
    std::mt19937 gen(draw * 2654435761u + 0x2d3820u + uint32_t(fn) * 0x9e3779b9u);
    for (auto &b : gManager) b = uint8_t(gen());
    PutWord(gManager, 0, P(lightfx::gManagerVT));
    for (int i = 0; i < kRefs; ++i) {
        gRefs[i] = FakeRef{};
        gRefs[i].vt = lightfx::gNodeVT, gRefs[i].id = 0x100u + i, gRefs[i].refs = gen() % 4;
    }
    *GameAddress<uint8_t>(kFadeSwitch) = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    // the lit object: slot 7 calls it unchecked when the switch is set; ApplyLightSet checks it
    const bool litNull = (fn == kApply || (fn == kReleaseWorld && !*GameAddress<uint8_t>(kFadeSwitch))) && gen() % 4 == 0;
    PutWord(gManager, 0x18, litNull ? 0 : P(&life::gLit));
    PutWord(gManager, 0x1c, P(&life::gView));
    for (auto &f : life::gView.floats) f = ViewFloat(gen);
    for (auto &w : life::gView.ints) w = gen() % 4 ? int32_t(gen() % 300) : int32_t(gen());
    PutWord(gManager, 0x20, P(DrawRef(gen, 4)));
    PutWord(gManager, 0x24, P(DrawRef(gen, 4)));
    PutWord(gManager, 0x28, P(DrawRef(gen, 4)));
    // the parser: slots 7 and 9 check it, slot 22 does not
    PutWord(gManager, 0x30, fn != kFind && gen() % 4 == 0 ? 0 : P(&life::gParser));
    PutWord(gManager, 0x38, P(DrawRef(gen, 3)));
    PutWord(gManager, 0x3c, P(DrawRef(gen, 3)));
    gManager[0x44] = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    PutFloat(gManager, 0x7c, gen() % 6 == 0 ? NaN(gen) : gen() % 3 ? Nice(gen, 2.0f) : Wide(gen, 4.0f));
    // the light sets and the current one
    for (auto &s : gSets) FillSet(gen, s);
    FillSet(gen, *reinterpret_cast<cLightSet *>(gManager + 0x2d0));
    const uint32_t sets = gen() % 4;
    PutWord(gManager, 0x98, P(gSets)), PutWord(gManager, 0x9c, P(gSets + sets)), PutWord(gManager, 0xa0, P(gSets + 3));
    static const uint32_t kIndexes[] = {0, 1, 2, 1, 3, 0xffffffffu, 0x80000000u};
    PutWord(gManager, 0xd0, kIndexes[gen() % std::size(kIndexes)]);
    // the sky samples (their lerp references, and +0x228, release through the pool's table)
    for (auto &lerp : gLerps) lerp.vt = lightfx::gLerpVT;
    auto *const samples = new (gManager + 0x1f4) std::vector<cSkySample>();
    const uint32_t count = gen() % 4;
    samples->reserve(count + gen() % 3);
    for (uint32_t i = 0; i < count; ++i) {
        cSkySample &s = samples->emplace_back();
        auto *const bytes = reinterpret_cast<uint8_t *>(&s);
        for (uint32_t k = 0; k < 0x6c; k += 4) PutWord(bytes, k, gen());
        s.mLights.resize(gen() % 3);
        for (auto &l : s.mLights) l.mObject = &gRefs[gen() % kRefs];
        s.mLerp78.mSet = gen() % 3 ? reinterpret_cast<cLerpLightSet *>(&gLerps[gen() % kLerps]) : nullptr;
        s.mLerp88.mSet = gen() % 3 ? reinterpret_cast<cLerpLightSet *>(&gLerps[gen() % kLerps]) : nullptr;
        s.mPortals.resize(gen() % 3);
        for (auto &r : s.mPortals) {
            r.mObject.mObject = &gRefs[gen() % kRefs];
            r.mOther.mObject = gen() % 2 ? &gRefs[gen() % kRefs] : nullptr;
        }
    }
    PutWord(gManager, 0x228, gen() % 4 ? P(&gLerps[gen() % kLerps]) : 0);
    PutFloat(gManager, 0x2ac, NearEpsilon(gen));
    const uint32_t used = gen() % (kSampleSpace + 1);
    PutWord(gManager, 0x2b0, P(gSampleSpace)), PutWord(gManager, 0x2b4, P(gSampleSpace + used));
    PutWord(gManager, 0x2b8, P(gSampleSpace + kSampleSpace));
    new (gManager + 0x528) std::string(kTexts[gen() % std::size(kTexts)]);
    // the callees' answers
    static void *const kNone = nullptr;
    const uint32_t pick = gen() % 3;
    life::gLightmaps = pick == 0 ? kNone : pick == 1 ? reinterpret_cast<void *>(uintptr_t(Word<uint32_t>(gManager, 0x24)))
                                                     : DrawRef(gen, 4);
    for (auto &row : life::gAmbients)
        for (auto &c : row) c = Colour(gen);
    life::gAmbientCalls = 0;
    life::gPathCount = gen() % 6;
    for (auto &p : life::gPaths) p = kPaths[gen() % std::size(kPaths)];
    life::gExists = gen() % 3 ? uint32_t(gen()) : 0;
    life::gExistsCalls = 0;
    life::ResetRz();
    gNewLerpCount = 0;
    return kNames[gen() % std::size(kNames)];
}

// The scene manager slot 8 is given: the current one, a drawn one or null (drawn after the world, from the draw).
void *SetupArgument(uint32_t draw)
{
    std::mt19937 gen(draw * 40503u + 0x2d1300u);
    const uint32_t pick = gen() % 3;
    if (pick == 0) return nullptr;
    if (pick == 1) return reinterpret_cast<void *>(uintptr_t(Word<uint32_t>(gManager, 0x28)));
    return &gRefs[gen() % kRefs];
}

void AppendSky(Blob &o)
{
    const std::vector<cSkySample> &samples = SkySamples();
    o.push_back(uint32_t(samples.size())), o.push_back(uint32_t(samples.capacity()));
    for (const cSkySample &s : samples) {
        const auto *const raw = reinterpret_cast<const uint8_t *>(&s);
        for (uint32_t k = 0; k < 0x6c; k += 4) o.push_back(Word<uint32_t>(raw, k));
        o.push_back(uint32_t(s.mLights.size()));
        for (const auto &l : s.mLights) o.push_back(Id(l.mObject));
        o.push_back(Id(s.mLerp78.mSet)), o.push_back(Id(s.mLerp88.mSet));
        o.push_back(uint32_t(s.mPortals.size()));
        for (const cPortalRecord &r : s.mPortals) o.push_back(Id(r.mObject.mObject)), o.push_back(Id(r.mOther.mObject));
    }
}

void AppendPath(Blob &o)
{
    const std::string &path = Path();
    o.push_back(uint32_t(path.size())), o.push_back(uint32_t(path.capacity())), o.push_back(Hash(path.c_str()));
    if (path.capacity() < 16) // in place: the whole buffer (the bytes past the text too)
        for (uint32_t k = 0; k < 16; k += 4) o.push_back(Word<uint32_t>(gManager, 0x528 + k));
}

Blob Snapshot(const SavedGlobals &globals, uint32_t result, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee), o.push_back(result);
    for (uint32_t i = 0; i < kManagerSize; i += 4) {
        if (i >= 0x1f4 && i < 0x200) continue; // the sky samples: by content below
        if (i >= 0x528 && i < 0x540) continue; // the path: by content below
        const uint32_t w = Word<uint32_t>(gManager, i);
        o.push_back(i == 0x228 ? Id(reinterpret_cast<const void *>(uintptr_t(w))) : w);
    }
    AppendSky(o);
    AppendPath(o);
    for (const FakeRef &r : gRefs) o.push_back(r.refs);
    o.push_back(gNewLerpCount), o.push_back(life::gRzCount), o.push_back(life::gAmbientCalls);
    o.push_back(life::gExistsCalls);
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

void FreeAll()
{
    SkySamples().~vector();
    Path().~basic_string();
    for (uint32_t i = 0; i < gNewLerpCount && i < std::size(gNewLerps); ++i) ::operator delete(gNewLerps[i]);
    gNewLerpCount = 0;
}

// The references and flags the manager holds at the moment a fake is called (a member is nulled before its release,
// mFlag81 cleared before the shadow texture is built): logged on every count, release and builder call.
void Held()
{
    for (uint32_t at : {0x20u, 0x24u, 0x28u, 0x38u, 0x3cu, 0x228u})
        gLog.push_back(Id(reinterpret_cast<const void *>(uintptr_t(Word<uint32_t>(gManager, at)))));
    gLog.push_back(Word<uint32_t>(gManager, 0x80));
}
void AfterBuilder(uint32_t) { Held(); }

struct Stands {
    lightfx::Builders builders;
    life::RzHeap rz;
    life::Ambients ambients;
    life::PathServices paths;
    shape::StandIn server{0x9c5a89u, &GetCachedMessageServer, &life::StandServer};
    shape::CodePatch system{0x20defbu, shape::CodeOf(&lightfx::StandSystem)};
    shape::StandIn construct{0x3c0ec0u, &cLerpLightSet::Construct, &lightfx::LerpConstruct};
    shape::StandIn setKeys{0x3c2e30u, &cLerpLightSet::SetKeys, &lightfx::LerpSetKeys};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    SavedGlobals globals;
    Stands stands;
    Scope()
    {
        globals.Save(kFadeSwitch, 1);
        lightfx::FillShared();
        lightfx::gIdOf = &Id;
        lightfx::gOnRef = &Held, lightfx::gAfterBuilder = &AfterBuilder;
        gLog.reserve(1 << 14);
    }
    ~Scope() { lightfx::gOnRef = nullptr, lightfx::gAfterBuilder = nullptr; }
};

// `call(side, name)` on the world drawn for `fn`; its result's low byte (when it has one) compared with the rest.
template <class Call> void Run(difftest::Context &ctx, const char *what, uint32_t cases, Fn fn, Call call)
{
    Scope scope;
    SidesAtBothPrecisions(ctx, what, cases, true, [&](uint32_t draw, int side) {
        const char *const name = Draw(draw, fn);
        void *const argument = SetupArgument(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        const uint32_t result = call(side, name, argument, draw);
        heap.Stop();
        Blob outcome = Snapshot(scope.globals, result, heap.Blocks(), heap.Bytes());
        FreeAll();
        return outcome;
    });
}

} // namespace

// 0x2d3820 slot 7: the switch byte set and clear (the lit object's slot 6), the references set and null, the parser
// set and null, with and without a scene manager (the builders' removals, the sky samples emptied), the lerp set set
// and null.
DIFF_TEST(sg_lighting_lifecycle_release_world, 0x2d3820)
{
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2d3820);
    Run(ctx, "release world", 3000, kReleaseWorld, [&](int side, const char *, void *, uint32_t draw) {
        const uint32_t unused = draw * 0x9e3779b9u;
        if (side == 0) original(gManager, unused);
        else Manager()->ReleaseWorld(unused);
        return 0u;
    });
}

// 0x2d3910 slot 9: as slot 7's cases, plus the two images set and null.
DIFF_TEST(sg_lighting_lifecycle_release_lot, 0x2d3910)
{
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2d3910);
    Run(ctx, "release lot", 3000, kReleaseLot, [&](int side, const char *, void *, uint32_t draw) {
        const uint32_t unused = draw * 0x85ebca6bu;
        if (side == 0) original(gManager, unused);
        else Manager()->ReleaseLot(unused);
        return 0u;
    });
}

// 0x2d1300 slot 8: the terrain view's answers (x87: the reciprocal and the truncations through int64 at both
// precisions), the lightmaps and the scene manager each null, the same as the current one or another, the old lerp set
// set or null; the new one from Construct, keyed by SetKeys, then the two builders.
DIFF_TEST(sg_lighting_lifecycle_setup, 0x2d1300)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x2d1300);
    Run(ctx, "setup", 3000, kSetup, [&](int side, const char *, void *sceneManager, uint32_t) {
        if (side == 0) original(gManager, sceneManager);
        else Manager()->SetupNeighborhood(sceneManager);
        return 0u;
    });
}

// 0x2d0db0 slot 22: 0..5 listed paths (with and without a separator, empty, a lone separator), drawn PathExists
// answers (none, one, several: the last found one wins), names empty to long, the old path empty, in place or on the
// heap. The result's low byte counts.
DIFF_TEST(sg_lighting_lifecycle_find, 0x2d0db0)
{
    auto original = ctx.Original<bool(__thiscall *)(void *, const char *)>(0x2d0db0);
    Run(ctx, "find", 3000, kFind, [&](int side, const char *name, void *, uint32_t) {
        const bool found = side == 0 ? original(gManager, name) : Manager()->FindLightingFile(name);        return uint32_t(found);
    });
}

// 0x2d0730: mFlag44 and the lit object (its slot 7 with the current set's parameters), the shade (the current set's
// mShade198 times mShadeFactor7C, NaNs among them), mSunFalloff at and around the epsilon, the ambient colours around
// Shape's corners and the epsilon, the current index 1 and others (the second message's value); the two messages
// allocated, posted and released.
DIFF_TEST(sg_lighting_lifecycle_apply, 0x2d0730)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d0730);
    Run(ctx, "apply", 3000, kApply, [&](int side, const char *, void *, uint32_t) {
        if (side == 0) original(gManager);
        else Manager()->ApplyLightSet();
        return 0u;
    });
}
