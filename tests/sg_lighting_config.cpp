// Differential tests for nTSSG::cLightingManager's config light creators (lane 261109m, scenegraph LG4c;
// src/sg/cLightingManagerConfigLights.cpp): the dispatcher 0x2ccec0, the ambient creator 0x2ccf10, the bounce creator
// 0x2ce130 and the min-side creator 0x2ceee0. Hook shape: the original runs on the same manager, sample and config as
// ours.
//
// The world: the manager is 0x548 bytes of junk with the lightmaps (+0x24), the scene manager (+0x28, slot 7 logs the node
// it is given) and the fixed-bounce byte (+0x46, 0 or any other value) set. The sample is 0x8c bytes: six vectors drawn
// from a small pool (ties, +0 / -0, opposite signs) or wide floats with NaN payloads, its light vector (+0x6c) over a heap
// buffer with a drawn size and spare capacity (0 now and then, so a push reallocates and copies the nodes already there),
// and its lerp set (+0x78) a fake or null. The config is twelve drawn words (NaNs, zeros, negatives; the type 0..10 or
// wild). The game's globals the creators read (the zero vector, the up normal, the two bounce axes, the direction table,
// the five fixed colours, the min-side 1) are drawn too (NaN payloads among them) and restored after the test; the two
// adjustment tables are the image's. The refresh switch 0xffde64 is drawn.
//
// Fakes on both sides: the framework getter 0x1ae18d (slot 3 hands out pool nodes and lights by class id), the node
// (AddRef / Release, layer slots 78 / 80 / 81, light 93), the standard light (AddRef / Release, 41-43), the name and
// direction setters 0xae78f0 / 0xaeee30, the lightmap ambient constructor 0x3c7d30 (frees the block and returns a pool
// object whose +0x2c is a fake light), the lerp set's AddLight 0x3c10a0 and AddDirectional 0x3c1600 (ours stood in too),
// each logging its arguments (references as pool ids, names hashed). The dispatcher's test stands in the three creators.
//
// Compared: the log, the pools' reference counts and colours, the sample's bytes (its light vector as size, capacity and
// pool ids), the manager and config bytes, the globals, and the heap blocks each side made or freed; at the x87's 53-bit
// and 24-bit precision (the creators are SSE; the precision must not matter).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "sg_lighting_common.h"

#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightingManager.h"
#include "sg/nTSSGLights.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using nTSSG::cLerpLightSet;
using nTSSG::cLightConfig;
using nTSSG::cLightingManager;
using nTSSG::cSkySample;

// ---- fakes (the log, bit casts, Hash, NaN, Wide, PutWord and the runner are tests/sg_lighting_common.h's) ----

constexpr int kNodes = 24; // 0-7 the sample's lights at the start, 8-23 handed out
constexpr int kLights = 16;
constexpr int kMade = 8;
constexpr int kLerps = 3;

struct FakeRef {
    void **vt;
    uint32_t id, refs;
    float rgb[3];
};
struct Made {
    uint8_t head[0x2c];
    FakeRef light;
};
static_assert(offsetof(Made, light) == 0x2c, "Made");
struct Plain {
    void **vt;
};

FakeRef gNodes[kNodes], gLights[kLights];
Made gMade[kMade];
uint8_t gLerps[kLerps][0x20];
uint8_t gLightMaps[0x10];
Plain gCom, gSceneManager;
void *gNodeVT[100], *gLightVT[50], *gComVT[8], *gSceneManagerVT[12];
uint32_t gNextNode, gNextLight, gNextMade;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kNodes; ++i)
        if (p == &gNodes[i]) return 0x100 + i;
    for (int i = 0; i < kLights; ++i)
        if (p == &gLights[i]) return 0x200 + i;
    for (int i = 0; i < kMade; ++i) {
        if (p == &gMade[i].light) return 0x400 + i;
        if (p == &gMade[i]) return 0x480 + i;
    }
    for (int i = 0; i < kLerps; ++i)
        if (p == gLerps[i]) return 0x600 + i;
    if (p == gLightMaps) return 0x700;
    return 0xbad00000u | (P(p) & 0xfffff);
}

uint32_t __fastcall AddRef(FakeRef *self, void *) { return L(1), L(Id(self)), ++self->refs; }
uint32_t __fastcall Release(FakeRef *self, void *) { return L(2), L(Id(self)), --self->refs; }
void __fastcall NodeAddLayerBits(FakeRef *n, void *, uint32_t v) { L(78), L(Id(n)), L(v); }
void __fastcall NodeRemoveLayerBits(FakeRef *n, void *, uint32_t v) { L(80), L(Id(n)), L(v); }
void __fastcall NodeSetLayerTag(FakeRef *n, void *, uint32_t v) { L(81), L(Id(n)), L(v); }
void __fastcall NodeSetLight(FakeRef *n, void *, const void *light) { L(93), L(Id(n)), L(Id(light)); }
void __fastcall LightSetColor(FakeRef *l, void *, const float *rgb)
{
    L(41), L(Id(l)), LF(rgb, 3);
    std::memcpy(l->rgb, rgb, 12);
}
void __fastcall LightSetIntensity(FakeRef *l, void *, float v) { L(42), L(Id(l)), L(U(v)); }
void __fastcall LightSetFactor(FakeRef *l, void *, float v) { L(43), L(Id(l)), L(U(v)); }

uint32_t __fastcall ComGetClassObject(Plain *, void *, uint32_t cls, uint32_t iid, void **out)
{
    L(0xc0), L(cls), L(iid), L(Id(*out));
    FakeRef *made = nullptr;
    if (cls == 0x253d2018) made = &gNodes[8 + gNextNode++ % (kNodes - 8)];
    else if (cls == 0xc9c81b9b || cls == 0xc9c81ba3) made = &gLights[gNextLight++ % kLights];
    if (made) ++made->refs;
    *out = made;
    return made ? 1 : 0;
}
void *__cdecl GetFramework() { return L(0xf0), &gCom; }
void __fastcall SceneAdd(Plain *, void *, const void *node) { L(0x307), L(Id(node)); }

void __cdecl SetName(const void *node, const char *name) { L(0xae7), L(Id(node)), L(Hash(name)); }
void __cdecl SetDirection(const void *node, const float *d) { L(0xaee), L(Id(node)), LF(d, 3); }

uint8_t *__fastcall MakeLightMapAmbient(void *memory, void *, const void *src, float a, float b, uint32_t one)
{
    L(0xc1), L(Id(src)), L(U(a)), L(U(b)), L(one);
    ::operator delete(memory);
    return reinterpret_cast<uint8_t *>(&gMade[gNextMade++ % kMade]);
}

void __fastcall LerpAddLight(cLerpLightSet *self, void *, uint32_t group, const char *name, uint32_t kind,
                             const float *color, float scale, float a6, float a7)
{
    L(0x3c1), L(Id(self)), L(group), L(Hash(name)), L(kind), LF(color, 3), L(U(scale)), L(U(a6)), L(U(a7));
}
void __fastcall LerpAddDirectional(cLerpLightSet *self, void *, uint32_t group, const char *name, uint32_t kind,
                                   const float *color, float scale, const float *direction, float a7, float a8)
{
    L(0x3c2), L(Id(self)), L(group), L(Hash(name)), L(kind), LF(color, 3), L(U(scale)), LF(direction, 3), L(U(a7)),
        L(U(a8));
}

// The creators, stood in for the dispatcher's test.
void LogCall(uint32_t what, const void *self, const void *sample, const void *config, uint32_t tag, uint32_t bits,
             const void *prefix)
{
    L(what), L(P(self)), L(P(sample)), L(P(config)), L(tag), L(bits), L(P(prefix));
}
void __fastcall StandBounce(void *self, void *, void *sample, const void *config, uint32_t tag, uint32_t bits,
                            const void *prefix)
{
    LogCall(0xb0, self, sample, config, tag, bits, prefix);
}
void __fastcall StandMinSide(void *self, void *, void *sample, const void *config, uint32_t tag, uint32_t bits,
                             const void *prefix)
{
    LogCall(0xb1, self, sample, config, tag, bits, prefix);
}
void __fastcall StandAmbient(void *self, void *, void *sample, const void *config, uint32_t tag, uint32_t bits,
                             const void *prefix, int32_t deltas)
{
    LogCall(0xb2, self, sample, config, tag, bits, prefix), L(uint32_t(deltas));
}

void FillTables()
{
    gNodeVT[1] = reinterpret_cast<void *>(&AddRef);
    gNodeVT[2] = reinterpret_cast<void *>(&Release);
    gNodeVT[78] = reinterpret_cast<void *>(&NodeAddLayerBits);
    gNodeVT[80] = reinterpret_cast<void *>(&NodeRemoveLayerBits);
    gNodeVT[81] = reinterpret_cast<void *>(&NodeSetLayerTag);
    gNodeVT[93] = reinterpret_cast<void *>(&NodeSetLight);
    gLightVT[1] = reinterpret_cast<void *>(&AddRef);
    gLightVT[2] = reinterpret_cast<void *>(&Release);
    gLightVT[41] = reinterpret_cast<void *>(&LightSetColor);
    gLightVT[42] = reinterpret_cast<void *>(&LightSetIntensity);
    gLightVT[43] = reinterpret_cast<void *>(&LightSetFactor);
    gComVT[3] = reinterpret_cast<void *>(&ComGetClassObject);
    gSceneManagerVT[7] = reinterpret_cast<void *>(&SceneAdd);
    gCom.vt = gComVT, gSceneManager.vt = gSceneManagerVT;
}

// The game's globals the creators read (12-byte vectors unless noted), drawn per case and restored after the test.
constexpr uint32_t kRefreshSwitch = 0xffde64;
const uint32_t kVectorGlobals[] = {0x10d9424, 0xffd918, 0xffd900, 0xffd90c,
                                   0xffd9a0,  0xffd9ac, 0xffd9b8, 0xffd9dc, 0xffd9d0};
constexpr uint32_t kDirections = 0x10d93a8; // six vectors
constexpr uint32_t kMinSideOne = 0xd9c698;  // one float

struct Globals : SavedGlobals {
    Globals()
    {
        Save(kRefreshSwitch, 1);
        for (uint32_t rva : kVectorGlobals) Save(rva, 12);
        Save(kDirections, 72);
        Save(kMinSideOne, 4);
    }
};

// ---- the drawn world ----

// A vector component: from a small pool half the time (ties and sizes about equal), else wide.
float Component(std::mt19937 &gen, unsigned mode)
{
    static const float kPool[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.5f, 3.0f, 1.005f, 0.995f};
    if (mode == 0 || (mode == 1 && gen() % 2)) return kPool[gen() % std::size(kPool)];
    return Wide(gen, 4.0f);
}

alignas(16) uint8_t gManager[0x548];
alignas(16) uint8_t gSample[0x8c];
alignas(16) uint8_t gConfig[0x30];
cRZString gPrefix;
const char *const kPrefixes[] = {"", "LM_", "LM_Outdoor_Light_With_A_Long_Name_", "Room7_"};

struct World {
    uint32_t seed;
    uint32_t tag, bits;
    int32_t deltas;
};

// Draws the case into the globals and the game's data (both sides draw the same).
World Draw(uint32_t seed)
{
    std::mt19937 gen(seed * 2654435761u + 0x2ccf10u);
    World w{seed, 0, 0, 0};
    for (auto &b : gManager) b = uint8_t(gen());
    PutWord(gManager, 0x24, P(gLightMaps));
    PutWord(gManager, 0x28, P(&gSceneManager));
    gManager[0x46] = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    // the sample: vectors, then junk, its light vector and lerp set
    for (auto &b : gSample) b = uint8_t(gen());
    const unsigned mode = gen() % 3;
    for (int i = 0; i < 18; ++i) {
        const float f = Component(gen, mode);
        std::memcpy(gSample + 4 * i, &f, 4);
    }
    const uint32_t capacity = gen() % 4 == 0 ? 0 : 1 + gen() % 8;
    const uint32_t size = capacity ? (gen() % 3 == 0 ? capacity : gen() % (capacity + 1)) : 0;
    uint32_t *buffer = capacity ? static_cast<uint32_t *>(::operator new(capacity * 4)) : nullptr;
    for (uint32_t i = 0; i < size; ++i) {
        buffer[i] = gen() % 6 ? P(&gNodes[gen() % 8]) : 0;
        if (buffer[i]) ++reinterpret_cast<FakeRef *>(uintptr_t(buffer[i]))->refs;
    }
    PutWord(gSample, 0x6c, P(buffer)), PutWord(gSample, 0x70, P(buffer) + size * 4);
    PutWord(gSample, 0x74, P(buffer) + capacity * 4);
    PutWord(gSample, 0x78, gen() % 4 == 0 ? 0 : P(gLerps[gen() % kLerps]));
    // the config
    for (int i = 0; i < 12; ++i) {
        const float f = gen() % 3 ? Component(gen, 1) : Wide(gen, 2.0f);
        std::memcpy(gConfig + 4 * i, &f, 4);
    }
    static const uint32_t kTypes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0xfffffffeu};
    PutWord(gConfig, 0, kTypes[gen() % std::size(kTypes)]);
    if (gen() % 3 == 0) PutWord(gConfig, 0x20, 0);           // the lightmap weight 0 (the plain ambient light)
    if (gen() % 3 == 0) PutWord(gConfig, 0x20, 0x80000000u); // -0 too
    // the globals
    *GameAddress<uint8_t>(kRefreshSwitch) = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    for (uint32_t rva : kVectorGlobals) {
        float *v = GameAddress<float>(rva);
        const bool nan = gen() % 12 == 0;
        for (int k = 0; k < 3; ++k) v[k] = gen() % 3 ? float(int(gen() % 5) - 2) * 0.5f : Wide(gen, 1.0f);
        if (nan) v[gen() % 3] = NaN(gen);
        if (rva == 0x10d9424 && gen() % 2) v[0] = v[1] = v[2] = 0.0f;
    }
    float *dirs = GameAddress<float>(kDirections);
    const bool nanDirections = gen() % 6 == 0; // NaNs in several directions at once (the min-side light's operand order)
    for (int i = 0; i < 18; ++i) {
        dirs[i] = gen() % 8 ? float(int(gen() % 5) - 2) * 0.5f : Wide(gen, 1.0f);
        if (nanDirections && gen() % 3 == 0) dirs[i] = NaN(gen);
    }
    *GameAddress<float>(kMinSideOne) = gen() % 4 ? 1.0f : Wide(gen, 2.0f);
    // the call
    static const uint32_t kMasks[] = {0, 1, 0xffffffffu, 0x80000000u, 0x55};
    w.tag = gen() % 3 ? gen() % 4 : gen();
    w.bits = gen() % 2 ? kMasks[gen() % std::size(kMasks)] : gen();
    static const int32_t kDeltas[] = {0, 1, 2, 3, 4, 5, 6, -1, 7, 9, INT32_MIN};
    w.deltas = gen() % 6 ? int32_t(gen() % 7) : kDeltas[gen() % std::size(kDeltas)];
    gPrefix.mString = kPrefixes[gen() % std::size(kPrefixes)];
    // the pools
    for (int i = 0; i < kNodes; ++i) gNodes[i] = {gNodeVT, 0x100u + i, gen() % 4, {0, 0, 0}};
    for (int i = 0; i < kLights; ++i) gLights[i] = {gLightVT, 0x200u + i, gen() % 4, {0, 0, 0}};
    for (int i = 0; i < kMade; ++i) {
        std::memset(gMade[i].head, 0xa5, sizeof gMade[i].head);
        gMade[i].light = {gLightVT, 0x400u + i, gen() % 4, {0, 0, 0}};
    }
    gNextNode = gNextLight = gNextMade = 0;
    return w;
}

Blob Snapshot(const Globals &globals, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    uint32_t words[3];
    std::memcpy(words, gSample + 0x6c, 12);
    const uint32_t size = (words[1] - words[0]) / 4, capacity = (words[2] - words[0]) / 4;
    o.push_back(size), o.push_back(capacity);
    for (uint32_t i = 0; i < size; ++i) o.push_back(Id(reinterpret_cast<const void *const *>(uintptr_t(words[0]))[i]));
    for (uint32_t i = 0; i < 0x8c; i += 4) {
        if (i >= 0x6c && i < 0x78) continue;
        uint32_t w;
        std::memcpy(&w, gSample + i, 4);
        o.push_back(w);
    }
    for (uint32_t i = 0; i < sizeof gManager; i += 4) {
        uint32_t w;
        std::memcpy(&w, gManager + i, 4);
        o.push_back(w);
    }
    for (uint32_t i = 0; i < sizeof gConfig; i += 4) {
        uint32_t w;
        std::memcpy(&w, gConfig + i, 4);
        o.push_back(w);
    }
    for (const FakeRef &r : gNodes) o.push_back(r.refs);
    for (const FakeRef &r : gLights) o.push_back(r.refs), o.push_back(U(r.rgb[0])), o.push_back(U(r.rgb[2]));
    for (const Made &m : gMade) o.push_back(m.light.refs), o.push_back(U(m.light.rgb[1]));
    o.push_back(gNextNode), o.push_back(gNextLight), o.push_back(gNextMade);
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    o.push_back(gPrefix.mString.size()), o.push_back(Hash(gPrefix.mString.c_str()));
    return o;
}

void FreeSampleLights()
{
    uint32_t begin;
    std::memcpy(&begin, gSample + 0x6c, 4);
    ::operator delete(reinterpret_cast<void *>(uintptr_t(begin)));
}

// The stand-ins every test needs.
struct Stands {
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &GetFramework};
    shape::StandIn name{0xae78f0, &nGZSceneGraph::SetNodeTag, &SetName};
    shape::StandIn direction{0xaeee30, &nGZSceneGraph::AimNode, &SetDirection};
    shape::StandIn lightMap{0x3c7d30, &cAmbientFromLightMapLight_Constructor, &MakeLightMapAmbient};
    shape::StandIn addLight{0x3c10a0, &cLerpLightSet::AddLight, &LerpAddLight};
    shape::StandIn addDirectional{0x3c1600, &cLerpLightSet::AddDirectional, &LerpAddDirectional};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    Globals globals;
    Stands stands;
    Scope()
    {
        FillTables();
        gLog.reserve(1 << 14);
        gPrefix.Construct("");
    }
    ~Scope() { gPrefix.Destruct(); }
};

// Runs `call(side, world)` on both sides of each case at both precisions and compares.
template <class Call> void Run(difftest::Context &ctx, const Scope &scope, const char *what, uint32_t cases, Call call)
{
    SidesAtBothPrecisions(ctx, what, cases, false, [&](uint32_t draw, int side) {
        const World w = Draw(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        call(side, w);
        heap.Stop();
        Blob outcome = Snapshot(scope.globals, heap.Blocks(), heap.Bytes());
        FreeSampleLights();
        return outcome;
    });
}

cLightingManager *Manager() { return reinterpret_cast<cLightingManager *>(gManager); }
cSkySample *Sample() { return reinterpret_cast<cSkySample *>(gSample); }
const cLightConfig *Config() { return reinterpret_cast<const cLightConfig *>(gConfig); }

using Creator = void(__thiscall *)(void *, void *, const void *, uint32_t, uint32_t, const void *);

} // namespace

// 0x2ccec0: the three creators stood in on both sides; the types 0..10 and wild ones.
DIFF_TEST(sg_lighting_config_dispatch, 0x2ccec0)
{
    Scope scope;
    shape::StandIn bounce{0x2ce130, &cLightingManager::CreateBounceLight, &StandBounce};
    shape::StandIn minSide{0x2ceee0, &cLightingManager::CreateMinSideLight, &StandMinSide};
    shape::StandIn ambient{0x2ccf10, &cLightingManager::CreateAmbientLight, &StandAmbient};
    auto original = ctx.Original<Creator>(0x2ccec0);
    Run(ctx, scope, "dispatch", 400, [&](int side, const World &w) {
        if (side == 0) original(gManager, gSample, gConfig, w.tag, w.bits, &gPrefix);
        else Manager()->CreateConfigLight(Sample(), Config(), w.tag, w.bits, &gPrefix);
    });
}

// 0x2ccf10: the ambient light (plain or from the lightmap, or to the lerp set with the switch on) and the delta lights.
DIFF_TEST(sg_lighting_config_ambient, 0x2ccf10)
{
    Scope scope;
    auto original = ctx.Original<void(__thiscall *)(void *, void *, const void *, uint32_t, uint32_t, const void *,
                                                    int32_t)>(0x2ccf10);
    Run(ctx, scope, "ambient", 6000, [&](int side, const World &w) {
        if (side == 0) original(gManager, gSample, gConfig, w.tag, w.bits, &gPrefix, w.deltas);
        else Manager()->CreateAmbientLight(Sample(), Config(), w.tag, w.bits, &gPrefix, w.deltas);
    });
}

// 0x2ce130: the five bounce lights, coloured from the sample or fixed.
DIFF_TEST(sg_lighting_config_bounce, 0x2ce130)
{
    Scope scope;
    auto original = ctx.Original<Creator>(0x2ce130);
    Run(ctx, scope, "bounce", 3000, [&](int side, const World &w) {
        if (side == 0) original(gManager, gSample, gConfig, w.tag, w.bits, &gPrefix);
        else Manager()->CreateBounceLight(Sample(), Config(), w.tag, w.bits, &gPrefix);
    });
}

// 0x2ceee0: the bounce light, the two largest sides and the light between the two smallest.
DIFF_TEST(sg_lighting_config_min_side, 0x2ceee0)
{
    Scope scope;
    auto original = ctx.Original<Creator>(0x2ceee0);
    Run(ctx, scope, "min side", 6000, [&](int side, const World &w) {
        if (side == 0) original(gManager, gSample, gConfig, w.tag, w.bits, &gPrefix);
        else Manager()->CreateMinSideLight(Sample(), Config(), w.tag, w.bits, &gPrefix);
    });
}
