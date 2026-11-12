// Differential tests for the light set's light nodes (lane 261111n, scenegraph LG4d leftovers;
// src/sg/cLightingManagerSetNodes.cpp): BuildLightSetNodes 0xb3f770 (the old references released, the vector emptied,
// then the worker) and AppendLightSetNodes 0xb3f7f0 (a node per enabled part of the set). Free cdecl functions on a
// light set and two vectors.
//
// The world: a light set of 0x214 drawn bytes (floats from the Wide families: NaN, +-0, denormal, infinite, wide
// exponents) with a drawn enabled word (zero, all of bits 0..10, one bit of 0..13, random bits, junk above bit 10), the
// game's white 0x10fb040 drawn (restored after the test), the lights vector (raw begin / end / capacity) of 0..5 pool
// nodes or nulls with 0..3 spare places (none: all three words null), the kinds vector of 0..4 drawn words with 0..2
// spare, or no kinds vector; for 0xb3f770 now and then no lights vector.
//
// Fakes on both sides: the framework getter 0x1ae18d (slot 3 hands out nodes, standard, fog and texture lights from
// pools by class id, logging the class, the iid and the cell's value at the call), the node (AddRef / Release, layer
// slots 77 by name and 80, SetLight 93), the standard light (colour 41, intensity 42), the fog light (near / far 40,
// colour 41), the texture light (colour 41 with its index), the tag setter 0xae78f0 (the node and the tag hashed), the
// aimer 0xaeee30 (the node and its direction's bits), cRZString's Sprintf 0x1ad6f7 (format and text hashed). Every AddRef
// and Release also notes both vectors' sizes and capacities at that moment (the push's order against its count, the
// growth's copies and releases). The vector growth (the image's library 0x2c9800 / 0x18b860; std::vector's in ours)
// runs as it is.
//
// Compared: the log, both vectors (size, capacity, the live elements as ids or words; the words past the end are not
// compared: the original leaves the released pointers there, ours nulls them before emptying, and nothing reads past
// the end), each pool object's reference count and the colour and intensity it was given, the white, and the heap
// blocks and bytes each side made; at the x87's 53-bit and 24-bit precision.
#include "sg_lighting_fakes.h"

#include "sg/SGUtilities.h"

namespace {

using namespace lightfx;

constexpr int kNodes = 32;   // 0-7 the vector's at the start, 8-31 handed out
constexpr int kLights = 16;  // ambient and directional lights
constexpr int kFogs = 4;
constexpr int kTextures = 6;

FakeRef gNodes[kNodes], gLights[kLights], gFogs[kFogs], gTextures[kTextures];
uint32_t gNextNode, gNextLight, gNextFog, gNextTexture;
void *gFogVT[50];

alignas(16) uint8_t gSet[0x214];
uint32_t gLightsVector[3], gKindsVector[3];
bool gHaveLights, gHaveKinds;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kNodes; ++i)
        if (p == &gNodes[i]) return 0x100 + i;
    for (int i = 0; i < kLights; ++i)
        if (p == &gLights[i]) return 0x200 + i;
    for (int i = 0; i < kFogs; ++i)
        if (p == &gFogs[i]) return 0x280 + i;
    for (int i = 0; i < kTextures; ++i)
        if (p == &gTextures[i]) return 0x300 + i;
    if (p == gSet) return 0x700;
    return 0xbad00000u | (P(p) & 0xfffff);
}

void *Make(uint32_t cls)
{
    FakeRef *made = nullptr;
    if (cls == 0x253d2018) made = &gNodes[8 + gNextNode++ % 24];
    else if (cls == 0xc9c81b9b || cls == 0xc9c81ba3) made = &gLights[gNextLight++ % kLights];
    else if (cls == 0xac06a66f) made = &gFogs[gNextFog++ % kFogs];
    else if (cls == 0x6a97042f) made = &gTextures[gNextTexture++ % kTextures];
    if (made) ++made->refs;
    return made;
}

void __fastcall NodeAddLayer(FakeRef *n, void *, const char *name) { L(77), L(Id(n)), L(Hash(name)); }
void __fastcall FogNearFar(FakeRef *l, void *, float nearDistance, float farDistance)
{
    L(0x2840), L(Id(l)), L(U(nearDistance)), L(U(farDistance));
    l->intensity = nearDistance, l->other[0] = farDistance;
}

// A vector's state, relative to its own buffer: size and capacity in elements (and whether there is a buffer).
void NoteVector(Blob &o, const uint32_t *v)
{
    o.push_back(v[0] ? 1 : 0), o.push_back((v[1] - v[0]) / 4), o.push_back((v[2] - v[0]) / 4);
}
void NoteVectors()
{
    L(0x5e5e);
    if (gHaveLights) {
        NoteVector(gLog, gLightsVector);
        // The cells too: a node is let go before its cell is emptied.
        for (uint32_t p = gLightsVector[0]; p != gLightsVector[1]; p += 4)
            gLog.push_back(Id(*reinterpret_cast<const void *const *>(uintptr_t(p))));
    }
    if (gHaveKinds) NoteVector(gLog, gKindsVector);
}

void FillTables()
{
    FillShared();
    gIdOf = &Id;
    gMake = &Make;
    gOnRef = &NoteVectors;
    gNodeVT[77] = reinterpret_cast<void *>(&NodeAddLayer);
    gFogVT[1] = reinterpret_cast<void *>(&AddRef);
    gFogVT[2] = reinterpret_cast<void *>(&Release);
    gFogVT[40] = reinterpret_cast<void *>(&FogNearFar);
    gFogVT[41] = reinterpret_cast<void *>(&LightSetColor);
    gCom.vt = gComVT;
}

uint32_t *NewWords(uint32_t count) { return count ? static_cast<uint32_t *>(::operator new(count * 4)) : nullptr; }

constexpr uint32_t kWhite = 0x10fb040;

struct World {
    uint32_t enabled;
};

World Draw(uint32_t seed, bool mayDropLights)
{
    std::mt19937 gen(seed * 0x9e3779b9u + 0x5e7);
    World w{};
    for (FakeRef &r : gNodes) r = FakeRef{gNodeVT, 0, 1 + gen() % 3};
    for (FakeRef &r : gLights) r = FakeRef{gLightVT, 0, 1};
    for (FakeRef &r : gFogs) r = FakeRef{gFogVT, 0, 1};
    for (FakeRef &r : gTextures) r = FakeRef{gTextureVT, 0, 1};
    gNextNode = gNextLight = gNextFog = gNextTexture = 0;

    for (uint32_t i = 0; i < sizeof gSet; i += 4) PutFloat(gSet, i, Wide(gen, 4.0f));
    switch (gen() % 6) {
    case 0: w.enabled = 0; break;
    case 1: w.enabled = 0x7ff | (gen() % 2 ? gen() & ~0x7ffu : 0); break;
    case 2: w.enabled = 1u << (gen() % 14); break;
    case 3: w.enabled = gen() & 0x7ff; break;
    default: w.enabled = gen(); break;
    }
    PutWord(gSet, 0x20, w.enabled);
    for (int k = 0; k < 3; ++k) GameAddress<float>(kWhite)[k] = gen() % 3 ? 1.0f : Wide(gen, 2.0f);

    gHaveLights = !(mayDropLights && gen() % 8 == 0);
    {
        const uint32_t count = gen() % 6, spare = gen() % 4;
        uint32_t *const words = NewWords(count + spare);
        for (uint32_t i = 0; i < count; ++i) words[i] = gen() % 6 == 0 ? 0 : P(&gNodes[gen() % 8]);
        gLightsVector[0] = P(words), gLightsVector[1] = P(words + count), gLightsVector[2] = P(words + count + spare);
    }
    gHaveKinds = gen() % 5 != 0;
    {
        const uint32_t count = gen() % 5, spare = gen() % 3;
        uint32_t *const words = NewWords(count + spare);
        for (uint32_t i = 0; i < count; ++i) words[i] = gen() % 2 ? gen() % 12 : gen();
        gKindsVector[0] = P(words), gKindsVector[1] = P(words + count), gKindsVector[2] = P(words + count + spare);
    }
    return w;
}

Blob Snapshot(const SavedGlobals &globals, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    o.push_back(gHaveLights), o.push_back(gHaveKinds);
    NoteVector(o, gLightsVector);
    for (uint32_t p = gLightsVector[0]; p != gLightsVector[1]; p += 4)
        o.push_back(Id(*reinterpret_cast<const void *const *>(uintptr_t(p))));
    NoteVector(o, gKindsVector);
    for (uint32_t p = gKindsVector[0]; p != gKindsVector[1]; p += 4) o.push_back(*reinterpret_cast<const uint32_t *>(uintptr_t(p)));
    for (const FakeRef &r : gNodes) o.push_back(r.refs);
    for (const FakeRef &r : gLights)
        o.push_back(r.refs), o.push_back(U(r.rgb[0])), o.push_back(U(r.rgb[1])), o.push_back(U(r.rgb[2])),
            o.push_back(U(r.intensity));
    for (const FakeRef &r : gFogs)
        o.push_back(r.refs), o.push_back(U(r.rgb[0])), o.push_back(U(r.rgb[2])), o.push_back(U(r.intensity)),
            o.push_back(U(r.other[0]));
    for (const FakeRef &r : gTextures) o.push_back(r.refs), o.push_back(U(r.rgb[0])), o.push_back(U(r.rgb[2]));
    o.push_back(gNextNode), o.push_back(gNextLight), o.push_back(gNextFog), o.push_back(gNextTexture);
    for (uint32_t i = 0; i < sizeof gSet; i += 4) o.push_back(Word<uint32_t>(gSet, i));
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

void FreeAll()
{
    ::operator delete(reinterpret_cast<void *>(uintptr_t(gLightsVector[0])));
    ::operator delete(reinterpret_cast<void *>(uintptr_t(gKindsVector[0])));
    gLightsVector[0] = gKindsVector[0] = 0;
}

struct Stands {
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &GetFramework};
    shape::StandIn tag{0xae78f0, &nGZSceneGraph::SetNodeTag, &SetName};
    shape::StandIn aim{0xaeee30, &nGZSceneGraph::AimNode, &SetDirection};
    shape::StandIn sprintf{0x1ad6f7, &cRZString::Sprintf, &StandSprintf};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    SavedGlobals globals;
    Stands stands;
    Scope()
    {
        globals.Save(kWhite, 12);
        FillTables();
        gLog.reserve(1 << 14);
    }
    ~Scope() { gOnRef = nullptr; }
};

using Fn = void(__cdecl *)(const void *, void *, void *);

template <class Call> void Run(difftest::Context &ctx, const Scope &scope, const char *what, uint32_t cases, bool mayDropLights,
                               Call call)
{
    SidesAtBothPrecisions(ctx, what, cases, false, [&](uint32_t draw, int side) {
        Draw(draw, mayDropLights);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        void *const lights = gHaveLights ? gLightsVector : nullptr;
        void *const kinds = gHaveKinds ? gKindsVector : nullptr;
        difftest::AllocationCounter heap;
        call(side, lights, kinds);
        heap.Stop();
        Blob outcome = Snapshot(scope.globals, heap.Blocks(), heap.Bytes());
        FreeAll();
        return outcome;
    });
}

using nTSSG::cBuiltRef;
using nTSSG::cLightSet;
using LightsVector = std::vector<cBuiltRef>;
using KindsVector = std::vector<int32_t>;

} // namespace

// 0xb3f770: nothing without a lights vector; else its references released in order and the vector emptied, then the
// set's nodes appended.
DIFF_TEST(sg_lighting_set_nodes_build, 0xb3f770)
{
    Scope scope;
    const Fn original = ctx.Original<Fn>(0xb3f770);
    Run(ctx, scope, "build", 3000, true, [&](int side, void *lights, void *kinds) {
        if (side == 0) original(gSet, lights, kinds);
        else
            nTSSG::BuildLightSetNodes(reinterpret_cast<const cLightSet *>(gSet), static_cast<LightsVector *>(lights),
                                      static_cast<KindsVector *>(kinds));
    });
}

// 0xb3f7f0: the set's nodes appended to the vectors as they are.
DIFF_TEST(sg_lighting_set_nodes_append, 0xb3f7f0)
{
    Scope scope;
    const Fn original = ctx.Original<Fn>(0xb3f7f0);
    Run(ctx, scope, "append", 3000, false, [&](int side, void *lights, void *kinds) {
        if (side == 0) original(gSet, lights, kinds);
        else
            nTSSG::AppendLightSetNodes(reinterpret_cast<const cLightSet *>(gSet), static_cast<LightsVector *>(lights),
                                       static_cast<KindsVector *>(kinds));
    });
}
