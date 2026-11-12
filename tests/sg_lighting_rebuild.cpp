// Differential tests for nTSSG::cLightingManager's top light builders (lane 261109p, scenegraph LG4d;
// src/sg/cLightingManagerBuilders.cpp): the set's lights with the sun's height-map shadow 0x2cfd60, the rooms' lights
// 0x2d0250 and the flat outdoor / indoor lights 0x2e1560. Hook shape: the original runs on the same manager as ours.
//
// The world: the manager is 0x548 bytes of junk with a fake vtable (slot 36, the lot value, logs the room and answers a
// drawn word: 100, 100 with junk upper half, 99, -100, ...), the scene manager (+0x28, slot 7 logs the node), the sun
// mode (+0x40: 0..3 or junk upper bytes), the flat sun fraction (+0x58: NaN, +-0, negative, positive), the shade bytes
// (+0x5c, +0x5e), the sun value (+0x60), the light sets (+0x98 over zero to two static sets; the default set at +0x2d0),
// the current set (+0xd0 in or out of range), the sky samples (+0x1f4 over zero to seven static samples, each with six
// drawn vectors, a heap light vector of drawn size and spare capacity and a pool lerp set or null), the built lights
// (+0x21c, a heap vector of pool nodes), the three lerp sets (+0x228..+0x230) and the five group masks
// (+0x50c..+0x520). Each set's enabled bits (4..6 decide the indoor case), colour 0x19c, scales and the first two
// lights' directions are drawn. The refresh switch 0xffde64, the no-flat-lights byte 0xffde34, the game's 1.0f
// 0xd9c698 and the grey 0xffd994 are drawn and restored after the test.
//
// Fakes on both sides: the framework getter 0x1ae18d (slot 3 hands out nodes, standard and texture lights by class
// id), the node (AddRef / Release, layer slots 78 / 80 / 81, light flag 91 logged as a byte, SetLight 93, GetLight 95),
// the standard light (QueryInterface answering yes / no with junk upper bytes and writing its standard face or not,
// AddRef / Release, name 40, colour 41, intensity 42, GetColor 51 answering the buffer or another colour, GetIntensity
// 52), the texture light (AddRef / Release, SetColor 41), the name setter 0xae78f0, the set's light builder 0xb3f770
// (releases and refills the built lights with drawn pool nodes, the kinds 0..10 and wild ones), the two light
// constructors 0x3c7fd0 / 0x3c8120 (free the block, return a pool object whose +0x2c is a fake light), the lerp set's
// Construct, SetKeys, ClearActive, FadeOutUnused, AddLight, AddDirectional and BuildRenderGroupLights, cRZString's
// Sprintf, and the config light dispatcher 0x2ccec0 (ours stood in too), each logging its arguments (references as pool
// ids, names hashed). The sample key 0x3a97d0 and the six-vector average 0x2dae80 run as they are.
//
// Compared: the log, the pools' reference counts and colours, the manager's and samples' bytes (vectors as size,
// capacity and ids; lerp sets as ids), the made lerp sets, the globals, and the heap blocks each side made or freed; at
// the x87's 53-bit and 24-bit precision.
#include "sg_lighting_fakes.h"

#include "sg/SGUtilities.h"

namespace {

using namespace lightfx;
using nTSSG::cLightConfig;
using nTSSG::cSkySample;

// ---- fakes (the rest are tests/sg_lighting_fakes.h's) ----

constexpr int kNodes = 32;    // 0-7 the built lights at the start, 8-15 the samples' lights, 16-31 handed out
constexpr int kLights = 24;   // 0-7 the nodes' lights, 8-15 their standard faces, 16-23 handed out
constexpr int kTextures = 8;
constexpr int kSamples = 7;
constexpr int kSets = 2;

FakeRef gNodes[kNodes], gLights[kLights], gTextures[kTextures];
uint32_t gNextNode, gNextLight, gNextTexture;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kNodes; ++i)
        if (p == &gNodes[i]) return 0x100 + i;
    for (int i = 0; i < kLights; ++i)
        if (p == &gLights[i]) return 0x200 + i;
    for (int i = 0; i < kTextures; ++i)
        if (p == &gTextures[i]) return 0x300 + i;
    for (int i = 0; i < kMade; ++i) {
        if (p == &gMade[i].light) return 0x400 + i;
        if (p == &gMade[i]) return 0x480 + i;
    }
    for (int i = 0; i < kLerps; ++i)
        if (p == &gLerps[i]) return 0x600 + i;
    for (uint32_t i = 0; i < gNewLerpCount; ++i)
        if (p == gNewLerps[i]) return 0x680 + i;
    return 0xbad00000u | (P(p) & 0xfffff);
}

// The framework's objects by class id: nodes, standard lights and texture lights from the pools.
void *Make(uint32_t cls)
{
    FakeRef *made = nullptr;
    if (cls == 0x253d2018) made = &gNodes[16 + gNextNode++ % 16];
    else if (cls == 0xc9c81b9b) made = &gLights[16 + gNextLight++ % 8];
    else if (cls == 0x6a97042f) made = &gTextures[gNextTexture++ % kTextures];
    if (made) ++made->refs;
    return made;
}

const uint32_t kLotValues[] = {100, 100, 100, 100, 0x10064, 0xffff0064u, 99, 0xff9c, 0, 0x64640064u, 0x6400};

// The set's light builder 0xb3f770: the old nodes released, then drawn ones (one more reference each) and their kinds.
uint32_t gBuildSalt, gBuildCalls;
const uint8_t *gSetBase[kSets + 1];
uint32_t SetId(const void *set)
{
    for (int i = 0; i <= kSets; ++i)
        if (set == gSetBase[i]) return 0x700 + i;
    return 0xbad00000u | (P(set) & 0xfffff);
}
void __cdecl StandBuildSetLights(const void *set, uint32_t *lights, uint32_t *kinds)
{
    L(0xb3f7), L(SetId(set)), L(kinds[0]), L(kinds[1]), L(kinds[2]);
    for (uint32_t p = lights[0]; p != lights[1]; p += 4) {
        FakeRef *const node = *reinterpret_cast<FakeRef **>(uintptr_t(p));
        if (node) Release(node, nullptr);
    }
    lights[1] = lights[0];
    const uint32_t h = Mix2(gBuildSalt, gBuildCalls++);
    const uint32_t n = h % 8 == 0 ? 0 : 1 + (h >> 3) % 7;
    if ((lights[2] - lights[0]) / 4 < n) {
        ::operator delete(reinterpret_cast<void *>(uintptr_t(lights[0])));
        const uint32_t capacity = n + (h >> 6) % 3;
        const uint32_t buffer = P(::operator new(capacity * 4));
        lights[0] = lights[1] = buffer, lights[2] = buffer + capacity * 4;
    }
    if (!n) return;
    const uint32_t kindCapacity = n + (h >> 8) % 2;
    uint32_t *const kindWords = static_cast<uint32_t *>(::operator new(kindCapacity * 4));
    static const uint32_t kKinds[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0xffffffffu, 0x7fffffffu, 0x80000004u, 3, 6};
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t g = Mix2(h, i);
        FakeRef *const node = &gNodes[16 + gNextNode++ % 16];
        ++node->refs;
        reinterpret_cast<uint32_t *>(uintptr_t(lights[0]))[i] = P(node);
        kindWords[i] = g % 3 == 0 ? 0 : kKinds[(g >> 2) % std::size(kKinds)];
    }
    lights[1] = lights[0] + n * 4;
    kinds[0] = P(kindWords), kinds[1] = P(kindWords + n), kinds[2] = P(kindWords + kindCapacity);
}

void __cdecl StandRenderGroup(const void *set, cLerpLightSet *lerp, uint32_t kind, float value)
{
    L(0x3c21), L(SetId(set)), L(Id(lerp)), L(kind), L(U(value));
}

alignas(16) uint8_t gManager[0x548];
alignas(16) uint8_t gSets[kSets][0x214];
alignas(16) uint8_t gSamples[kSamples][0x8c];

uint32_t SampleIndex(const void *p)
{
    for (int i = 0; i < kSamples; ++i)
        if (p == gSamples[i]) return 0x800 + i;
    return 0xbad00000u | (P(p) & 0xfffff);
}
void __fastcall StandConfig(void *self, void *, void *sample, const void *config, uint32_t tag, uint32_t bits,
                            const cRZString *prefix)
{
    L(0xcce), L(P(self)), L(SampleIndex(sample)), L(P(config) - P(gManager)), L(tag), L(bits);
    L(Hash(prefix->mString.c_str()));
}

void FillTables()
{
    FillShared();
    gIdOf = &Id, gMake = &Make;
    LotValues(kLotValues);
}

// The game's globals the builders read, drawn per case and restored after the test.
constexpr uint32_t kRefreshSwitch = 0xffde64;
constexpr uint32_t kNoFlatLights = 0xffde34;
constexpr uint32_t kOne = 0xd9c698;
constexpr uint32_t kGrey = 0xffd994;

struct Globals : SavedGlobals {
    Globals()
    {
        Save(kRefreshSwitch, 1);
        Save(kNoFlatLights, 1);
        Save(kOne, 4);
        Save(kGrey, 12);
    }
};

// ---- the drawn world ----

float Component(std::mt19937 &gen)
{
    static const float kPool[] = {0.0f, -0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.5f, 3.0f, 1.005f, 0.995f};
    if (gen() % 2) return kPool[gen() % std::size(kPool)];
    return Wide(gen, 4.0f);
}

// A heap vector of `capacity` words, `size` of them pool nodes from [first, first + 8) (one more reference each).
void DrawNodeVector(std::mt19937 &gen, uint8_t *at, int first)
{
    const uint32_t capacity = gen() % 4 == 0 ? 0 : 1 + gen() % 6;
    const uint32_t size = capacity ? (gen() % 3 == 0 ? capacity : gen() % (capacity + 1)) : 0;
    uint32_t *buffer = capacity ? static_cast<uint32_t *>(::operator new(capacity * 4)) : nullptr;
    for (uint32_t i = 0; i < size; ++i) {
        FakeRef *const node = &gNodes[first + gen() % 8];
        ++node->refs;
        buffer[i] = P(node);
    }
    PutWord(at, 0, P(buffer)), PutWord(at, 4, P(buffer) + size * 4), PutWord(at, 8, P(buffer) + capacity * 4);
}

void DrawSet(std::mt19937 &gen, uint8_t *set)
{
    for (int i = 0; i < 0x214; ++i) set[i] = uint8_t(gen());
    static const uint32_t kEnabled[] = {0, 0x10, 0x20, 0x40, 0x70, 0xffffff8fu, 0xffffffffu, 0x8};
    PutWord(set, 0x20, gen() % 2 ? kEnabled[gen() % std::size(kEnabled)] : gen());
    for (uint32_t o : {0x34u, 0x38u, 0x3cu, 0x50u, 0x54u, 0x58u, 0x19cu, 0x1a0u, 0x1a4u}) PutFloat(set, o, Component(gen));
    for (uint32_t o : {0x1a8u, 0x1acu, 0x1b0u}) PutFloat(set, o, gen() % 3 ? Component(gen) : Wide(gen, 2.0f));
}

struct World {
    uint32_t seed;
    uint32_t lerpArg;
};

World Draw(uint32_t seed)
{
    std::mt19937 gen(seed * 2654435761u + 0x2cfd60u);
    World w{seed, 0};
    // the pools
    for (int i = 0; i < kNodes; ++i) {
        gNodes[i] = {gNodeVT, 0x100u + i, gen() % 4};
        gNodes[i].light = &gLights[gen() % 8];
    }
    for (int i = 0; i < kLights; ++i) {
        FakeRef &l = gLights[i];
        l = {gLightVT, 0x200u + i, gen() % 4};
        for (float &c : l.rgb) c = Component(gen);
        for (float &c : l.other) c = Component(gen);
        l.found = &gLights[8 + gen() % 8];
        l.answers = (gen() & ~0xffu) | (gen() % 4 ? (gen() % 3 ? 5 : 1) : (gen() % 2 ? 4 : 0));
        l.intensity = Component(gen);
        l.elsewhere = gen() % 4 == 0;
    }
    for (int i = 0; i < kTextures; ++i) gTextures[i] = {gTextureVT, 0x300u + i, gen() % 4};
    for (int i = 0; i < kMade; ++i) {
        std::memset(gMade[i].head, 0xa5, sizeof gMade[i].head);
        gMade[i].light = {gLightVT, 0x400u + i, gen() % 4};
    }
    for (int i = 0; i < kLerps; ++i) {
        gLerps[i].vt = gLerpVT;
        for (uint32_t &x : gLerps[i].words) x = gen();
    }
    gNextNode = gNextLight = gNextTexture = gNextMade = gNewLerpCount = 0;
    gLotSalt = gen(), gBuildSalt = gen(), gBuildCalls = 0;
    // the manager
    for (auto &b : gManager) b = uint8_t(gen());
    PutWord(gManager, 0, P(gManagerVT));
    PutWord(gManager, 0x28, P(&gSceneManager));
    static const uint32_t kModes[] = {0, 1, 2, 3, 1, 2, 0x101, 0x102, 0xffffffffu};
    PutWord(gManager, 0x40, kModes[gen() % std::size(kModes)]);
    static const float kFractions[] = {0.0f, -0.0f, 0.25f, 0.5f, 1.0f, -0.5f, 2.0f, 1e-30f};
    PutFloat(gManager, 0x58, gen() % 5 ? kFractions[gen() % std::size(kFractions)] : Wide(gen, 2.0f));
    gManager[0x5c] = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    gManager[0x5e] = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    PutFloat(gManager, 0x60, Component(gen));
    for (int i = 0; i < kSets; ++i) DrawSet(gen, gSets[i]);
    DrawSet(gen, gManager + 0x2d0);
    const uint32_t sets = gen() % (kSets + 1);
    PutWord(gManager, 0x98, P(gSets)), PutWord(gManager, 0x9c, P(gSets) + sets * 0x214);
    PutWord(gManager, 0xa0, P(gSets) + sets * 0x214);
    PutWord(gManager, 0xd0, gen() % 5 ? gen() % 3 : gen());
    // the sky samples
    const uint32_t samples = gen() % 6 == 0 ? gen() % 2 : gen() % (kSamples + 1);
    for (int i = 0; i < kSamples; ++i) {
        uint8_t *const s = gSamples[i];
        for (int k = 0; k < 0x8c; ++k) s[k] = uint8_t(gen());
        for (int k = 0; k < 18; ++k) PutFloat(s, 4 * k, Component(gen));
        DrawNodeVector(gen, s + 0x6c, 8);
        PutWord(s, 0x78, gen() % 3 == 0 ? 0 : P(&gLerps[gen() % kLerps]));
    }
    PutWord(gManager, 0x1f4, P(gSamples)), PutWord(gManager, 0x1f8, P(gSamples) + samples * 0x8c);
    PutWord(gManager, 0x1fc, P(gSamples) + kSamples * 0x8c);
    DrawNodeVector(gen, gManager + 0x21c, 0);
    for (int i = 0; i < 3; ++i) PutWord(gManager, 0x228 + 4 * i, P(&gLerps[gen() % kLerps]));
    static const uint32_t kMasks[] = {0, 1, 2, 4, 0x10, 0xffffffffu, 0x80000000u, 0x55};
    for (uint32_t o = 0x50c; o <= 0x520; o += 4) PutWord(gManager, o, gen() % 2 ? kMasks[gen() % std::size(kMasks)] : gen());
    // the globals
    *GameAddress<uint8_t>(kRefreshSwitch) = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    *GameAddress<uint8_t>(kNoFlatLights) = gen() % 3 ? 0 : uint8_t(1 + gen() % 255);
    *GameAddress<float>(kOne) = gen() % 4 ? 1.0f : Wide(gen, 2.0f);
    for (int k = 0; k < 3; ++k) GameAddress<float>(kGrey)[k] = Component(gen);
    // the flat builder's byte argument, junk above it
    w.lerpArg = (gen() & ~0xffu) | (gen() % 3 == 0 ? 0 : 1 + gen() % 255);
    return w;
}

void AppendNodeVector(Blob &o, const uint8_t *at)
{
    const uint32_t begin = Word<uint32_t>(at, 0), end = Word<uint32_t>(at, 4), cap = Word<uint32_t>(at, 8);
    o.push_back((end - begin) / 4), o.push_back((cap - begin) / 4);
    for (uint32_t p = begin; p != end; p += 4) o.push_back(Id(*reinterpret_cast<const void *const *>(uintptr_t(p))));
}

Blob Snapshot(const Globals &globals, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    for (uint32_t i = 0; i < sizeof gManager; i += 4) {
        if (i >= 0x21c && i < 0x228) continue;
        o.push_back(Word<uint32_t>(gManager, i));
    }
    AppendNodeVector(o, gManager + 0x21c);
    for (int s = 0; s < kSamples; ++s) {
        for (uint32_t i = 0; i < 0x8c; i += 4) {
            if (i >= 0x6c && i <= 0x78) continue;
            o.push_back(Word<uint32_t>(gSamples[s], i));
        }
        AppendNodeVector(o, gSamples[s] + 0x6c);
        o.push_back(Id(reinterpret_cast<const void *>(uintptr_t(Word<uint32_t>(gSamples[s], 0x78)))));
    }
    for (uint32_t i = 0; i < gNewLerpCount; ++i)
        for (int k = 0; k < 7; ++k) o.push_back(static_cast<const uint32_t *>(gNewLerps[i])[k]);
    for (int i = 0; i < kSets; ++i)
        for (uint32_t k = 0; k < 0x214; k += 4) o.push_back(Word<uint32_t>(gSets[i], k));
    for (const FakeRef &r : gNodes) o.push_back(r.refs);
    for (const FakeRef &r : gLights)
        o.push_back(r.refs), o.push_back(U(r.rgb[0])), o.push_back(U(r.rgb[2])), o.push_back(U(r.intensity));
    for (const FakeRef &r : gTextures) o.push_back(r.refs), o.push_back(U(r.rgb[1]));
    for (const Made &m : gMade)
        o.push_back(m.light.refs), o.push_back(U(m.light.rgb[1])), o.push_back(U(m.light.intensity));
    for (const FakeLerp &l : gLerps) o.push_back(P(l.vt));
    o.push_back(gNextNode), o.push_back(gNextLight), o.push_back(gNextTexture), o.push_back(gNextMade);
    o.push_back(gNewLerpCount), o.push_back(gBuildCalls);
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

void FreeAll()
{
    ::operator delete(reinterpret_cast<void *>(uintptr_t(Word<uint32_t>(gManager, 0x21c))));
    for (int s = 0; s < kSamples; ++s) ::operator delete(reinterpret_cast<void *>(uintptr_t(Word<uint32_t>(gSamples[s], 0x6c))));
    for (uint32_t i = 0; i < gNewLerpCount; ++i) ::operator delete(gNewLerps[i]);
    gNewLerpCount = 0;
}

struct Stands {
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &GetFramework};
    shape::StandIn name{0xae78f0, &nGZSceneGraph::SetNodeTag, &SetName};
    shape::StandIn build{0xb3f770, &nTSSG::BuildLightSetNodes, &StandBuildSetLights};
    shape::StandIn heightMap{0x3c7fd0, &cHeightMapShadowLight_Constructor, &MakeHeightMapLight};
    shape::StandIn terrainShade{0x3c8120, &cTerrainShadeLight_Constructor, &MakeTerrainShade};
    shape::StandIn construct{0x3c0ec0, &cLerpLightSet::Construct, &LerpConstruct};
    shape::StandIn setKeys{0x3c2e30, &cLerpLightSet::SetKeys, &LerpSetKeys};
    shape::StandIn clearActive{0x3c2570, &cLerpLightSet::ClearActive, &LerpClearActive};
    shape::StandIn fadeOut{0x3c25d0, &cLerpLightSet::FadeOutUnused, &LerpFadeOut};
    shape::StandIn addLight{0x3c10a0, &cLerpLightSet::AddLight, &LerpAddLight};
    shape::StandIn addDirectional{0x3c1600, &cLerpLightSet::AddDirectional, &LerpAddDirectional};
    shape::StandIn renderGroup{0x3c2120, &cLerpLightSet::BuildRenderGroupLights, &StandRenderGroup};
    shape::StandIn sprintf{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
    shape::StandIn config{0x2ccec0, &cLightingManager::CreateConfigLight, &StandConfig};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    Globals globals;
    Stands stands;
    Scope()
    {
        FillTables();
        gLog.reserve(1 << 14);
        gSetBase[0] = gSets[0], gSetBase[1] = gSets[1], gSetBase[2] = gManager + 0x2d0;
    }
};

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
        FreeAll();
        return outcome;
    });
}

cLightingManager *Manager() { return reinterpret_cast<cLightingManager *>(gManager); }

} // namespace

// 0x2cfd60: the set's lights to the lerp set (switch on) or as nodes with layer bits by kind, the sun as a height-map
// shadow light, and the terrain shade light.
DIFF_TEST(sg_lighting_rebuild_sun, 0x2cfd60)
{
    Scope scope;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2cfd60);
    Run(ctx, scope, "sun", 4000, [&](int side, const World &) {
        if (side == 0) original(gManager);
        else Manager()->BuildSunHeightMapLights();
    });
}

// 0x2d0250: each room sample at the lot value 100: its lerp set, the three config lights and the environment light.
DIFF_TEST(sg_lighting_rebuild_rooms, 0x2d0250)
{
    Scope scope;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d0250);
    Run(ctx, scope, "rooms", 3000, [&](int side, const World &) {
        if (side == 0) original(gManager);
        else Manager()->BuildRoomLights();
    });
}

// 0x2e1560: the flat ambient node, or the flat outdoor and indoor lerp lights; the byte argument with junk above it.
DIFF_TEST(sg_lighting_rebuild_flat, 0x2e1560)
{
    Scope scope;
    using Flat = void(__thiscall *)(void *, uint32_t);
    auto original = ctx.Original<Flat>(0x2e1560);
    auto ours = reinterpret_cast<Flat>(MemberAddress<&cLightingManager::BuildFlatOutdoorLights>());
    Run(ctx, scope, "flat", 4000, [&](int side, const World &w) {
        if (side == 0) original(gManager, w.lerpArg);
        else ours(gManager, w.lerpArg);
    });
}
