// Differential tests for nTSSG::cLerpLightSet (lane 261108l, scenegraph LG5b; src/sg/cLerpLightSet.cpp): the constructor,
// destructors and entry life (0x3c0ec0, 0x3c1030, 0x3c1080, 0x3c0e30, 0x3c0d60, 0x3c0fc0), the queries and clears
// (0x3c2570, 0x3c25d0, 0x3c2660, 0x3c26c0, 0x3c2cc0, 0x3c2e30), the attribute events (0x3c2d80, 0x3c2b50), the entry's
// step and the set's (0x3c2e60, 0x3c3450), the creators (0x3c10a0, 0x3c1600, 0x3c1a80, 0x3c1d60) and the render-group
// builder 0x3c2120.
//
// The world: the set is 0x1c bytes with its entries in a buffer from operator new (spare capacity drawn), each entry drawn
// (kinds 0-9 and beyond, groups from a few masks, nodes / lights / texture lights from fake pools or null, the flag
// bytes 0, 1 or 0xff, colours near black and NaN payloads). The fakes log every slot they are called on (with pool ids, not
// pointers) and keep what is set on them, so later reads see it: the node (AddRef / Release, attributes 7 / 10, parent 34,
// local transform 53, transform 63, the layer slots 78 / 80 / 81, flag 91, light 93), the standard light (41-44, 51, 54)
// and the texture light (41, 42). Stand-ins on both sides: the framework getter 0x1ae18d (its slot 3 hands out pool objects
// by class id, failing on drawn calls), the render-group registry 0xaeb370 (slot 3 answers drawn bits, 32 and above
// included), the service 0x20defb (slots 48 / 49), the node registry 0x20df51 (slots 7 / 9), the name and direction setters
// 0xae78f0 / 0xaeee30, and the six light constructors (each returns a pool object whose +0x2c is a fake light).
//
// Compared: the log, the set's header and every entry (references as pool ids; a new entry's value only where the creator
// sets it: the original leaves it as the stack held it), the pools' counts and stored values, the heap blocks each side
// made or freed (difftest::AllocationCounter) and, for the destructors, the delete each makes. All the code is SSE (the
// light's falloff comes back on the x87 stack and is stored as it is), so the cases run at the harness's precision.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "rz/RZRefCountBase.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightingManager.h"
#include "sg/cRenderGroupNameIterator.h"
#include "sg/nTSSGLights.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using nTSSG::cLerpLightSet;
using nTSSG::cLightSet;
using Entry = cLerpLightSet::Entry;

constexpr uint32_t kSetVTable = 0xdbea94;
constexpr uint32_t kRefCountVTable = 0xda2fa8;

std::vector<uint32_t> gLog;
void L(uint32_t w) { gLog.push_back(w); }

uint32_t U(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float Fl(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
void LF(const float *p, int n)
{
    for (int i = 0; i < n; ++i) L(U(p[i]));
}
uint32_t Hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

// ---- fakes ----

constexpr int kNodes = 24;      // 0-7 the drawn entries', 8-19 handed out by the framework, 20-23 the caller's own
constexpr int kMadeNodes = 8;   // first handed-out node
constexpr int kOwnNodes = 20;   // first caller's node
constexpr int kLights = 24;     // 0-7 the drawn entries', 8-23 handed out
constexpr int kTextures = 8;    // 0-2 the drawn entries', 3-7 handed out
constexpr int kMade = 16;       // the light constructors' objects

struct FakeNode {
    void **vt;
    uint32_t id, refs, parent;
    float xf[7];
    float one, rgb[3], value; // the attributes e9d24d51, 29de3049, 49e37821
    uint32_t answer;          // GetAttribute fills the event
};
struct FakeLight {
    void **vt;
    uint32_t id, refs;
    float rgb[3], falloff;
};
struct FakeTexture {
    void **vt;
    uint32_t id, refs;
    float rgba[4];
};
struct Made {
    uint8_t head[0x2c];
    FakeLight light;
};
static_assert(offsetof(Made, light) == 0x2c, "Made");
struct Plain {
    void **vt;
    uint32_t id;
};

FakeNode gNodes[kNodes];
FakeLight gLights[kLights];
FakeTexture gTextures[kTextures];
Made gMade[kMade];
Plain gCom, gGroups, gService, gRegistry, gSourceA, gSourceB, gParent;
void *gNodeVT[100], *gLightVT[60], *gTextureVT[50], *gComVT[8], *gGroupsVT[8], *gServiceVT[52], *gRegistryVT[12];
uint32_t gNextNode, gNextLight, gNextTexture, gNextMade, gComCalls, gComFail;
uint32_t gBits[8];
const void *gScalarVT, *gVectorVT;

const char *const kGroupNames[8] = {"Sims", "Props", "Portals", "Roof", "LotSkirt", "Walls", "Floors", "Ground"};

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
    if (p == &gSourceA) return 0x501;
    if (p == &gSourceB) return 0x502;
    if (p == &gParent) return 0x503;
    return 0xbad;
}
uint32_t EventKind(const void *event)
{
    const void *vt = *static_cast<const void *const *>(event);
    return vt == gScalarVT ? 1 : vt == gVectorVT ? 2 : 0xbad;
}

uint32_t __fastcall AddRef(Plain *self, void *) { return L(1), L(Id(self)), ++reinterpret_cast<FakeLight *>(self)->refs; }
uint32_t __fastcall Release(Plain *self, void *) { return L(2), L(Id(self)), --reinterpret_cast<FakeLight *>(self)->refs; }

// The node.
float *AttributeOf(FakeNode *n, uint32_t id, int *count)
{
    *count = id == 0x29de3049 ? 3 : 1;
    return id == 0xe9d24d51 ? &n->one : id == 0x29de3049 ? n->rgb : id == 0x49e37821 ? &n->value : nullptr;
}
void __fastcall NodeSetAttribute(FakeNode *n, void *, uint32_t id, uint32_t zero, const uint8_t *event)
{
    L(7), L(n->id), L(id), L(zero), L(EventKind(event));
    int count;
    float *stored = AttributeOf(n, id, &count);
    const float *v = reinterpret_cast<const float *>(event + 4);
    LF(v, EventKind(event) == 2 ? 3 : 1);
    if (stored) std::memcpy(stored, v, count * 4);
}
void __fastcall NodeGetAttribute(FakeNode *n, void *, uint8_t *event, uint32_t id, uint32_t zero)
{
    L(10), L(n->id), L(id), L(zero), L(EventKind(event));
    int count;
    const float *stored = AttributeOf(n, id, &count);
    // A node without the value attribute would leave the original stepping from stack garbage (ORIGINAL-BUG in
    // LerpLightEvents), so the value is always answered.
    if ((n->answer || id == 0x49e37821) && stored) std::memcpy(event + 4, stored, count * 4);
}
void *__fastcall NodeGetParent(FakeNode *n, void *) { return L(34), L(n->id), n->parent ? &gParent : nullptr; }
float *__fastcall NodeGetLocalTransform(FakeNode *n, void *, float *out)
{
    L(53), L(n->id);
    std::memcpy(out, n->xf, sizeof n->xf);
    return out;
}
void __fastcall NodeSetTransform(FakeNode *n, void *, const float *xf) { L(63), L(n->id), LF(xf, 7); }
void __fastcall NodeAddLayerBits(FakeNode *n, void *, uint32_t v) { L(78), L(n->id), L(v); }
void __fastcall NodeRemoveLayerBits(FakeNode *n, void *, uint32_t v) { L(80), L(n->id), L(v); }
void __fastcall NodeSetLayerTag(FakeNode *n, void *, uint32_t v) { L(81), L(n->id), L(v); }
void __fastcall NodeSetLightFlag(FakeNode *n, void *, uint32_t v) { L(91), L(n->id), L(v & 0xff); }
void __fastcall NodeSetLight(FakeNode *n, void *, const void *light) { L(93), L(n->id), L(Id(light)); }

// The standard light.
void __fastcall LightSetColor(FakeLight *l, void *, const float *rgb)
{
    L(41), L(l->id), LF(rgb, 3);
    std::memcpy(l->rgb, rgb, 12);
}
void __fastcall LightSetIntensity(FakeLight *l, void *, float v) { L(42), L(l->id), L(U(v)); }
void __fastcall LightSetFactor(FakeLight *l, void *, float v) { L(43), L(l->id), L(U(v)); }
void __fastcall LightSetFalloff(FakeLight *l, void *, float v)
{
    L(44), L(l->id), L(U(v));
    l->falloff = v;
}
float *__fastcall LightGetColor(FakeLight *l, void *, float *out)
{
    L(51), L(l->id);
    std::memcpy(out, l->rgb, 12);
    return out;
}
float __fastcall LightGetFalloff(FakeLight *l, void *) { return L(54), L(l->id), l->falloff; }

// The texture light.
void __fastcall TextureSetColor(FakeTexture *t, void *, uint32_t index, const float *rgba)
{
    L(141), L(t->id), L(index), LF(rgba, 4);
    if (index == 0) std::memcpy(t->rgba, rgba, 16);
}
float *__fastcall TextureGetColor(FakeTexture *t, void *, float *out, uint32_t index)
{
    L(142), L(t->id), L(index);
    std::memcpy(out, t->rgba, 16);
    return out;
}

// The framework's class objects.
uint32_t __fastcall ComGetClassObject(Plain *, void *, uint32_t cls, uint32_t iid, void **out)
{
    L(0xc0), L(cls), L(iid), L(Id(*out));
    // Only the standard lights may fail: the original uses a node or texture light it asked for without a test.
    const bool light = cls == 0xc9c81b9b || cls == 0xc9c81ba3;
    if (((gComFail >> (gComCalls++ & 31)) & 1) && light) return 0;
    void *made = nullptr;
    if (cls == 0x253d2018) {
        made = &gNodes[kMadeNodes + gNextNode++ % (kOwnNodes - kMadeNodes)];
    } else if (cls == 0x6a97042f) {
        made = &gTextures[3 + gNextTexture++ % (kTextures - 3)];
    } else if (cls == 0xc9c81b9b || cls == 0xc9c81ba3) {
        made = &gLights[8 + gNextLight++ % (kLights - 8)];
    }
    if (made) ++static_cast<FakeLight *>(made)->refs;
    *out = made;
    return made ? 1 : 0;
}
void *__cdecl GetFramework() { return L(0xf0), &gCom; }

uint32_t __fastcall GroupsBitOfName(Plain *, void *, const char *name)
{
    L(0xf2), L(Hash(name));
    for (int i = 0; i < 8; ++i)
        if (std::strcmp(name, kGroupNames[i]) == 0) return gBits[i];
    return 0xbad;
}
nGZSceneGraph::cIRenderGroupRegistry *__cdecl GetGroups()
{
    L(0xf1);
    return reinterpret_cast<nGZSceneGraph::cIRenderGroupRegistry *>(&gGroups);
}

void *__fastcall ServiceSlot48(Plain *, void *) { return L(0x148), &gSourceA; }
void *__fastcall ServiceSlot49(Plain *, void *) { return L(0x149), &gSourceB; }
void *__cdecl GetService() { return L(0xf3), &gService; }

void __fastcall RegistryAdd(Plain *, void *, const void *node) { L(0x107), L(Id(node)); }
void __fastcall RegistryRemove(Plain *, void *, const void *node) { L(0x109), L(Id(node)); }
void *__cdecl GetRegistry() { return L(0xf4), &gRegistry; }

void __cdecl SetName(const void *node, const char *name) { L(0xae7), L(Id(node)), L(Hash(name)); }
void __cdecl SetDirection(const void *node, const float *d) { L(0xaee), L(Id(node)), LF(d, 3); }

// The light constructors: each logs, frees the block (the test's pool object stands for it) and returns a pool object.
uint8_t *MadeLight(void *memory)
{
    ::operator delete(memory);
    return reinterpret_cast<uint8_t *>(&gMade[gNextMade++ % kMade]);
}
uint8_t *__fastcall MakeLightMapAmbient(void *memory, void *, const void *src, float a, float b, uint32_t one)
{
    L(0xc1), L(Id(src)), L(U(a)), L(U(b)), L(one);
    return MadeLight(memory);
}
uint8_t *__fastcall MakeShade(void *memory, void *, const float *rgba, uint32_t one)
{
    L(0xc6), LF(rgba, 4), L(one);
    return MadeLight(memory);
}
uint8_t *__fastcall MakeTerrainShade(void *memory, void *, const void *src, uint32_t one)
{
    L(0xc7), L(Id(src)), L(one);
    return MadeLight(memory);
}
uint8_t *__fastcall MakeShadowA(void *memory, void *, const void *src, float a, uint32_t one)
{
    L(0xc3), L(Id(src)), L(U(a)), L(one);
    return MadeLight(memory);
}
uint8_t *__fastcall MakeShadowB(void *memory, void *, const void *src, const float *d, float a, float b, uint32_t one)
{
    L(0xc4), L(Id(src)), LF(d, 3), L(U(a)), L(U(b)), L(one);
    return MadeLight(memory);
}
uint8_t *__fastcall MakeHalfspace(void *memory, void *, const float *d, const float *p, uint32_t one)
{
    L(0xc5), LF(d, 3), LF(p, 3), L(one);
    return MadeLight(memory);
}

void FillTables()
{
    gNodeVT[1] = reinterpret_cast<void *>(&AddRef);
    gNodeVT[2] = reinterpret_cast<void *>(&Release);
    gNodeVT[7] = reinterpret_cast<void *>(&NodeSetAttribute);
    gNodeVT[10] = reinterpret_cast<void *>(&NodeGetAttribute);
    gNodeVT[34] = reinterpret_cast<void *>(&NodeGetParent);
    gNodeVT[53] = reinterpret_cast<void *>(&NodeGetLocalTransform);
    gNodeVT[63] = reinterpret_cast<void *>(&NodeSetTransform);
    gNodeVT[78] = reinterpret_cast<void *>(&NodeAddLayerBits);
    gNodeVT[80] = reinterpret_cast<void *>(&NodeRemoveLayerBits);
    gNodeVT[81] = reinterpret_cast<void *>(&NodeSetLayerTag);
    gNodeVT[91] = reinterpret_cast<void *>(&NodeSetLightFlag);
    gNodeVT[93] = reinterpret_cast<void *>(&NodeSetLight);
    gLightVT[1] = reinterpret_cast<void *>(&AddRef);
    gLightVT[2] = reinterpret_cast<void *>(&Release);
    gLightVT[41] = reinterpret_cast<void *>(&LightSetColor);
    gLightVT[42] = reinterpret_cast<void *>(&LightSetIntensity);
    gLightVT[43] = reinterpret_cast<void *>(&LightSetFactor);
    gLightVT[44] = reinterpret_cast<void *>(&LightSetFalloff);
    gLightVT[51] = reinterpret_cast<void *>(&LightGetColor);
    gLightVT[54] = reinterpret_cast<void *>(&LightGetFalloff);
    gTextureVT[1] = reinterpret_cast<void *>(&AddRef);
    gTextureVT[2] = reinterpret_cast<void *>(&Release);
    gTextureVT[41] = reinterpret_cast<void *>(&TextureSetColor);
    gTextureVT[42] = reinterpret_cast<void *>(&TextureGetColor);
    gComVT[3] = reinterpret_cast<void *>(&ComGetClassObject);
    gGroupsVT[3] = reinterpret_cast<void *>(&GroupsBitOfName);
    gServiceVT[48] = reinterpret_cast<void *>(&ServiceSlot48);
    gServiceVT[49] = reinterpret_cast<void *>(&ServiceSlot49);
    gRegistryVT[7] = reinterpret_cast<void *>(&RegistryAdd);
    gRegistryVT[9] = reinterpret_cast<void *>(&RegistryRemove);
    gCom.vt = gComVT, gGroups.vt = gGroupsVT, gService.vt = gServiceVT, gRegistry.vt = gRegistryVT;
}

// Every stand-in, on both sides.
struct Stands {
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &GetFramework};
    shape::StandIn groups{0xaeb370, &nGZSceneGraph::RenderGroupRegistry, &GetGroups};
    shape::CodePatch service{0x20defb, reinterpret_cast<const void *>(&GetService)};
    shape::CodePatch registry{0x20df51, reinterpret_cast<const void *>(&GetRegistry)};
    shape::StandIn name{0xae78f0, &nGZSceneGraph::SetNodeTag, &SetName};
    shape::StandIn direction{0xaeee30, &nGZSceneGraph::AimNode, &SetDirection};
    shape::StandIn lightMap{0x3c7d30, &cAmbientFromLightMapLight_Constructor, &MakeLightMapAmbient};
    shape::StandIn shade{0x3c80b0, &cShadeLight_Constructor, &MakeShade};
    shape::StandIn terrainShade{0x3c8120, &cTerrainShadeLight_Constructor, &MakeTerrainShade};
    shape::StandIn shadowA{0x3c7fd0, &cHeightMapShadowLight_Constructor, &MakeShadowA};
    shape::StandIn shadowB{0x3c7e80, &cHeightMapBinaryShadowLight_Constructor, &MakeShadowB};
    shape::StandIn halfspace{0x3c7de0, &cHalfspacePointLight_Constructor, &MakeHalfspace};
};

// ---- the drawn world ----

float Nice(std::mt19937 &gen)
{
    switch (gen() % 16) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return float(int(gen() % 9) - 4);
    case 3: return Fl((gen() & 0x80000000u) | (1 + gen() % 40)); // near black: denormals within NearlyEqual's reach or not
    case 4: return Fl((gen() & 0x807fffffu) | (uint32_t(127 - 30 + gen() % 60) << 23));
    case 5: return Fl(0x7f800000u | (gen() & 0x80000000u) | (gen() % 2 ? 0x400000u : 0u) | (gen() % 0x3fffffu));
    default: return (float(int32_t(gen() & 0xffffff) - 0x800000) / float(0x800000)) * 2.0f;
    }
}
float NearOne(std::mt19937 &gen) // t: around 0.95 (0.95f itself is below the double 0.95), 0, 1, beyond, NaN
{
    static const uint32_t kT[] = {0x3f733333, 0x3f733334, 0x3f733332, 0x3f800000, 0x00000000, 0x80000000,
                                  0x3f000000, 0xbf000000, 0x40000000, 0x7fc00000, 0xffc00001, 0x3e800000};
    return gen() % 3 ? Fl(kT[gen() % std::size(kT)]) : Nice(gen);
}
float Ulps(float x, std::mt19937 &gen)
{
    static const int32_t kSteps[] = {0, 0, 0, 1, -1, 31, -31, 32, -32, 33, -33, 0x100000};
    return Fl(U(x) + uint32_t(kSteps[gen() % std::size(kSteps)]));
}
uint8_t Flag(std::mt19937 &gen) { return gen() % 8 == 0 ? 0xff : uint8_t(gen() % 2); }

struct EntrySpec {
    uint32_t kind, group;
    int node, light, texture; // pool indexes, -1 for none
    uint8_t lerp[3];
    float color[3], direction[3], value;
    uint8_t active, reusable;
};

struct World {
    struct NodeData {
        uint32_t refs, parent, answer;
        float xf[7], attr[5];
    } nodes[kNodes];
    struct LightData {
        uint32_t refs;
        float rgb[3], falloff;
    } lights[kLights];
    struct TextureData {
        uint32_t refs;
        float rgba[4];
    } textures[kTextures];
    uint32_t comFail, bits[8];
    uint32_t refCount, layerTag, lightFlag;
    std::vector<EntrySpec> entries;
    uint32_t spare;
};

const uint32_t kGroups[] = {1, 2, 4, 0x18, 0x80000001, 0xffffffff};

void DrawPools(World &w, std::mt19937 &gen)
{
    for (auto &n : w.nodes) {
        n.refs = 1 + gen() % 4, n.parent = gen() % 3 != 0, n.answer = gen() % 4 != 0;
        for (float &f : n.xf) f = Nice(gen);
        for (float &f : n.attr) f = Nice(gen);
    }
    for (auto &l : w.lights) {
        l.refs = 1 + gen() % 4;
        for (float &f : l.rgb) f = Nice(gen);
        l.falloff = Nice(gen);
    }
    for (auto &t : w.textures) {
        t.refs = 1 + gen() % 4;
        for (float &f : t.rgba) f = Nice(gen);
    }
    w.comFail = gen() % 4 == 0 ? (1u << (gen() % 6)) : 0;
    for (uint32_t &b : w.bits) b = gen() % 6 == 0 ? 32 + gen() % 40 : gen() % 32;
    w.refCount = gen() % 4, w.layerTag = gen() % 3 ? gen() % 4 : gen(), w.lightFlag = gen() % 3 ? 2 : gen();
}

EntrySpec DrawEntry(std::mt19937 &gen, int index)
{
    EntrySpec e{};
    e.kind = gen() % 12;
    if (e.kind > 9) e.kind = gen() % 2 ? 9 : 5;
    e.group = kGroups[gen() % std::size(kGroups)];
    e.node = index; // every entry has its node (the original's FindPoint and kExisting steps use it untested)
    e.light = gen() % 3 == 0 ? -1 : index;
    e.texture = gen() % 4 == 0 ? index % 3 : -1;
    for (uint8_t &b : e.lerp) b = Flag(gen);
    for (float &f : e.color) f = gen() % 3 ? Fl((gen() & 0x80000000u) | (gen() % 48)) : Nice(gen);
    for (float &f : e.direction) f = Nice(gen);
    e.value = Nice(gen);
    e.active = Flag(gen), e.reusable = gen() % 5 ? 1 : Flag(gen);
    return e;
}

void DrawEntries(World &w, std::mt19937 &gen, uint32_t most = 7)
{
    const uint32_t count = gen() % (most + 1);
    for (uint32_t i = 0; i < count; ++i) w.entries.push_back(DrawEntry(gen, int(i)));
    w.spare = gen() % 3 ? gen() % 3 : 0;
}

// ---- one side ----

alignas(8) uint8_t gObject[0x1c];
uint32_t gInitialCount;

const void *OurVTable()
{
    static const void *vt = [] {
        alignas(8) uint8_t scratch[sizeof(cLerpLightSet)];
        auto *s = ::new (scratch) cLerpLightSet;
        const void *v = *reinterpret_cast<const void **>(scratch);
        s->~cLerpLightSet();
        return v;
    }();
    return vt;
}

Entry *&First(uint8_t *object) { return *reinterpret_cast<Entry **>(object + 0x10); }
Entry *&Last(uint8_t *object) { return *reinterpret_cast<Entry **>(object + 0x14); }
Entry *&End(uint8_t *object) { return *reinterpret_cast<Entry **>(object + 0x18); }

void ResetPools(const World &w)
{
    for (int i = 0; i < kNodes; ++i) {
        FakeNode &n = gNodes[i];
        n.vt = gNodeVT, n.id = 0x100 + i, n.refs = w.nodes[i].refs, n.parent = w.nodes[i].parent;
        n.answer = w.nodes[i].answer;
        std::memcpy(n.xf, w.nodes[i].xf, sizeof n.xf);
        n.one = w.nodes[i].attr[0], n.value = w.nodes[i].attr[4];
        std::memcpy(n.rgb, w.nodes[i].attr + 1, 12);
    }
    for (int i = 0; i < kLights; ++i) {
        FakeLight &l = gLights[i];
        l.vt = gLightVT, l.id = 0x200 + i, l.refs = w.lights[i].refs, l.falloff = w.lights[i].falloff;
        std::memcpy(l.rgb, w.lights[i].rgb, 12);
    }
    for (int i = 0; i < kTextures; ++i) {
        FakeTexture &t = gTextures[i];
        t.vt = gTextureVT, t.id = 0x300 + i, t.refs = w.textures[i].refs;
        std::memcpy(t.rgba, w.textures[i].rgba, 16);
    }
    for (int i = 0; i < kMade; ++i) {
        FakeLight &l = gMade[i].light;
        l.vt = gLightVT, l.id = 0x400 + i, l.refs = 1, l.falloff = w.lights[i].falloff;
        std::memcpy(l.rgb, w.lights[i].rgb, 12);
    }
    gNextNode = gNextLight = gNextTexture = gNextMade = gComCalls = 0;
    gComFail = w.comFail;
    std::memcpy(gBits, w.bits, sizeof gBits);
}

// The set as the world draws it, on `side`'s object (0 the original: the game's vtable; 1 ours).
uint8_t *Build(const World &w, int side)
{
    ResetPools(w);
    uint8_t *const o = gObject;
    std::memset(o, 0, sizeof gObject);
    *reinterpret_cast<const void **>(o) = side == 0 ? static_cast<const void *>(GameAddress<void>(kSetVTable)) : OurVTable();
    std::memcpy(o + 4, &w.refCount, 4);
    std::memcpy(o + 8, &w.layerTag, 4);
    std::memcpy(o + 0xc, &w.lightFlag, 4);
    const size_t capacity = w.entries.size() + w.spare;
    Entry *first = capacity ? static_cast<Entry *>(::operator new(capacity * sizeof(Entry))) : nullptr;
    for (size_t i = 0; i < w.entries.size(); ++i) {
        const EntrySpec &s = w.entries[i];
        uint8_t *raw = reinterpret_cast<uint8_t *>(first + i);
        std::memset(raw, 0, sizeof(Entry));
        Entry &e = *reinterpret_cast<Entry *>(raw);
        e.mKind = s.kind, e.mGroup = s.group;
        e.mNode = s.node < 0 ? nullptr : reinterpret_cast<nGZSceneGraph::cILightRefNode *>(&gNodes[s.node]);
        e.mLight = s.light < 0 ? nullptr : reinterpret_cast<nGZSceneGraph::cIStandardLight *>(&gLights[s.light]);
        e.mTexture = s.texture < 0 ? nullptr : reinterpret_cast<nGZSceneGraph::cITextureLight *>(&gTextures[s.texture]);
        e.mLerpColor = s.lerp[0], e.mLerpDirection = s.lerp[1], e.mLerpValue = s.lerp[2];
        std::memcpy(e.mColor, s.color, 12);
        std::memcpy(e.mDirection, s.direction, 12);
        e.mValue = s.value, e.mActive = s.active, e.mReusable = s.reusable;
    }
    First(o) = first, Last(o) = first + w.entries.size(), End(o) = first + capacity;
    gInitialCount = uint32_t(w.entries.size());
    gLog.clear();
    return o;
}

// The entries let go of without logging, and the buffer freed.
void Teardown(uint8_t *o)
{
    for (Entry *e = First(o); e && e != Last(o); ++e) e->~Entry();
    ::operator delete(First(o));
    First(o) = Last(o) = End(o) = nullptr;
}

bool MadeNode(const void *node)
{
    for (int i = kMadeNodes; i < kOwnNodes; ++i)
        if (node == &gNodes[i]) return true;
    return false;
}

std::vector<uint32_t> Snapshot(uint8_t *o, long long blocks, long long bytes)
{
    std::vector<uint32_t> s;
    s.push_back(uint32_t(gLog.size()));
    s.insert(s.end(), gLog.begin(), gLog.end());
    s.push_back(uint32_t(blocks)), s.push_back(uint32_t(bytes));
    for (uint32_t off = 4; off < 0x10; off += 4) s.push_back(shape::Word(o, off));
    const Entry *first = First(o);
    const uint32_t count = first ? uint32_t(Last(o) - first) : 0;
    s.push_back(count), s.push_back(first ? uint32_t(End(o) - first) : 0);
    for (uint32_t i = 0; i < count; ++i) {
        const Entry &e = first[i];
        s.push_back(e.mKind), s.push_back(e.mGroup), s.push_back(Id(e.mNode)), s.push_back(Id(e.mLight));
        s.push_back(Id(e.mTexture)), s.push_back(e.mLerpColor), s.push_back(e.mLerpDirection), s.push_back(e.mLerpValue);
        for (float f : e.mColor) s.push_back(U(f));
        for (float f : e.mDirection) s.push_back(U(f));
        // A new entry of AddLight / AddDirectional keeps the value the original's stack held.
        const bool defined = !MadeNode(e.mNode) || e.mKind == cLerpLightSet::kHalfspacePoint;
        s.push_back(defined ? U(e.mValue) : 0), s.push_back(e.mActive), s.push_back(e.mReusable);
    }
    for (const FakeNode &n : gNodes) {
        s.push_back(n.refs), s.push_back(U(n.one)), s.push_back(U(n.rgb[0])), s.push_back(U(n.rgb[1]));
        s.push_back(U(n.rgb[2])), s.push_back(U(n.value));
    }
    for (const FakeLight &l : gLights) s.push_back(l.refs), s.push_back(U(l.rgb[0])), s.push_back(U(l.falloff));
    for (const Made &m : gMade) s.push_back(m.light.refs), s.push_back(U(m.light.rgb[1])), s.push_back(U(m.light.falloff));
    for (const FakeTexture &t : gTextures) {
        s.push_back(t.refs);
        for (float f : t.rgba) s.push_back(U(f));
    }
    s.push_back(gNextNode), s.push_back(gNextLight), s.push_back(gNextTexture), s.push_back(gNextMade);
    s.push_back(gComCalls);
    return s;
}

// Runs `call(side, object)` on both sides of the world and compares.
template <class Call>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, const World &w, Call call)
{
    std::vector<uint32_t> snap[2];
    for (int side = 0; side < 2; ++side) {
        uint8_t *o = Build(w, side);
        difftest::AllocationCounter heap;
        call(side, o);
        heap.Stop();
        snap[side] = Snapshot(o, heap.Blocks(), heap.Bytes());
        Teardown(o);
    }
    ++ctx.cases;
    if (snap[0] == snap[1]) return;
    size_t i = 0;
    while (i < snap[0].size() && i < snap[1].size() && snap[0][i] == snap[1][i]) ++i;
    ctx.Fail("%s seed %u: word %u of %u/%u: %08x vs %08x (log %u words)", what, seed, unsigned(i),
             unsigned(snap[0].size()), unsigned(snap[1].size()), i < snap[0].size() ? snap[0][i] : 0,
             i < snap[1].size() ? snap[1][i] : 0, snap[0][0]);
}

cLerpLightSet *Ours(uint8_t *o) { return reinterpret_cast<cLerpLightSet *>(o); }

// The test-wide setup.
struct Scope {
    svcfx::AllocatorScope allocator;
    Stands stands;
    Scope(difftest::Context &ctx)
    {
        FillTables();
        gScalarVT = ctx.image.At(0xdb07f4);
        gVectorVT = ctx.image.At(0xdb5e70);
        gLog.reserve(1 << 16);
    }
};

const char *const kNames[] = {"LM_Outdoor_Ambient", "L", "", "LM_Outdoor_Dir_3 and a name past the short buffer"};

} // namespace

// 0x3c0ec0 the constructor, 0x3c1030 the destructor (a set with entries: the registry's nodes removed, the references let
// go of, the buffer freed), 0x3c1080 the deleting destructor (bits 0 and 1 of the flags; through our vtable's slot 0, where
// the game's has it) and the vtable's other slots (the count's own on both).
DIFF_TEST(sg_lerp_light_set_life, 0x3c0ec0)
{
    Scope scope(ctx);
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x3c0ec0);
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(0x3c1030);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x3c1080);

    // The vtables: the game's slots 1-4 are cRZRefCount's, as ours are rz::cRZRefCount's; slot 0 the deleting destructor.
    {
        void *const *game = static_cast<void *const *>(ctx.image.At(kSetVTable));
        void *const *count = static_cast<void *const *>(ctx.image.At(kRefCountVTable));
        rz::cRZRefCount base;
        void *const *ourBase = *reinterpret_cast<void *const *const *>(&base);
        void *const *ours = static_cast<void *const *>(OurVTable());
        for (int s = 1; s < 5; ++s) {
            if (game[s] != count[s]) ctx.Fail("game slot %d is not cRZRefCount's", s);
            if (ours[s] != ourBase[s]) ctx.Fail("our slot %d is not rz::cRZRefCount's", s);
        }
        if (shape::SlotOf(kSetVTable, 5, 0x3c1080) != 0) ctx.Fail("0x3c1080 is not slot 0");
        ++ctx.cases;
    }

    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3c0ec0u);
        uint8_t raw[2][0x20];
        const uint8_t fill = uint8_t(gen());
        std::memset(raw, fill, sizeof raw);
        void *r0 = construct(raw[0]);
        cLerpLightSet *r1 = Ours(raw[1])->Construct();
        ++ctx.cases;
        if (r0 != raw[0] || r1 != reinterpret_cast<void *>(raw[1])) ctx.Fail("Construct seed %u: result", seed);
        if (std::memcmp(raw[0] + 4, raw[1] + 4, 0x1c) != 0) ctx.Fail("Construct seed %u: bytes", seed);
        if (shape::Word(raw[0], 0) != shape::P(ctx.image.At(kSetVTable)) || shape::Word(raw[1], 0) != shape::P(OurVTable()))
            ctx.Fail("Construct seed %u: vtable", seed);
        Ours(raw[1])->Destruct();
        destruct(raw[0]);
    }

    for (uint32_t seed = 0; seed < 2000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3c1030u);
        World w;
        DrawPools(w, gen);
        DrawEntries(w, gen);
        const uint32_t flags = gen() % 4;
        const int route = gen() % 3; // the destructor, the deleting destructor, or ours through its vtable
        std::vector<uint32_t> snap[2];
        uint32_t deleted[2][3] = {};
        for (int side = 0; side < 2; ++side) {
            uint8_t *built = Build(w, side);
            uint8_t *o = static_cast<uint8_t *>(::operator new(0x1c));
            std::memcpy(o, built, 0x1c);
            shape::DeleteRecorder deletes;
            difftest::AllocationCounter heap;
            if (route == 0) {
                side == 0 ? destruct(o) : Ours(o)->Destruct();
            } else {
                deletes.Begin();
                void *r;
                if (side == 0)
                    r = deleting(o, flags);
                else if (route == 1)
                    r = Ours(o)->DeletingDestructor(flags);
                else
                    r = (*reinterpret_cast<void *(__thiscall *const *const *)(void *, uint32_t)>(o))[0](o, flags);
                deletes.End();
                deleted[side][0] = r == o;
            }
            heap.Stop();
            deleted[side][1] = uint32_t(deletes.Count());
            deleted[side][2] = deletes.Deleted(o);
            snap[side] = Snapshot(o, heap.Blocks(), heap.Bytes());
            if (route == 0) {
                // The vtable left behind: the count's own (the game's, or ours).
                const void *vt = *reinterpret_cast<const void **>(o);
                rz::cRZRefCount base;
                const void *ours = *reinterpret_cast<const void *const *>(&base);
                snap[side].push_back(vt == (side == 0 ? ctx.image.At(kRefCountVTable) : ours));
                for (uint32_t off = 0x10; off < 0x1c; off += 4) snap[side].push_back(shape::Word(o, off));
            }
            deletes.Release();
            if (!(route != 0 && (flags & 1))) ::operator delete(o);
            std::memset(built + 0x10, 0, 12);
        }
        ++ctx.cases;
        if (snap[0] != snap[1] || std::memcmp(deleted[0], deleted[1], sizeof deleted[0]) != 0)
            ctx.Fail("destructors seed %u route %d flags %u: differ (%u/%u words, deleted %u/%u)", seed, route, flags,
                     unsigned(snap[0].size()), unsigned(snap[1].size()), deleted[0][1], deleted[1][1]);
    }
}

// 0x3c0e30, 0x3c0d60, 0x3c0fc0: the entry's constructors and destructor on a buffer of one drawn byte (what the default
// constructor leaves is compared as it is), copies of drawn entries (each reference counted in field order).
DIFF_TEST(sg_lerp_light_set_entry_life, 0x3c0d60)
{
    Scope scope(ctx);
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x3c0e30);
    auto copy = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x3c0d60);
    auto destroy = ctx.Original<void(__thiscall *)(void *)>(0x3c0fc0);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3c0d60u);
        World w;
        DrawPools(w, gen);
        w.entries.push_back(DrawEntry(gen, int(gen() % 8)));
        w.spare = 0;
        const uint8_t fill = uint8_t(gen());
        std::vector<uint32_t> snap[2];
        uint8_t bytes[2][3][0x38];
        for (int side = 0; side < 2; ++side) {
            uint8_t *o = Build(w, side);
            std::memset(bytes[side], fill, sizeof bytes[side]);
            void *r0 = side == 0 ? construct(bytes[side][0]) : ::new (bytes[side][0]) Entry;
            void *r1 = side == 0 ? copy(bytes[side][1], First(o)) : ::new (bytes[side][1]) Entry(*First(o));
            std::memcpy(bytes[side][2], bytes[side][1], 0x38);
            side == 0 ? destroy(bytes[side][1]) : reinterpret_cast<Entry *>(bytes[side][1])->~Entry();
            L(r0 == bytes[side][0]), L(r1 == bytes[side][1]);
            snap[side] = Snapshot(o, 0, 0);
            Teardown(o);
        }
        // The copy's references are normalised to pool ids (the rest of it compared as bytes).
        for (int side = 0; side < 2; ++side)
            for (uint32_t off = 8; off < 0x14; off += 4) {
                uint32_t id = Id(reinterpret_cast<const void *>(uintptr_t(shape::Word(bytes[side][2], off))));
                std::memcpy(bytes[side][2] + off, &id, 4);
            }
        ++ctx.cases;
        if (snap[0] != snap[1]) ctx.Fail("entry life seed %u: log or pools differ", seed);
        if (std::memcmp(bytes[0][0], bytes[1][0], 0x38) != 0) ctx.Fail("entry life seed %u: default bytes", seed);
        if (std::memcmp(bytes[0][2], bytes[1][2], 0x38) != 0) ctx.Fail("entry life seed %u: copy bytes", seed);
    }
}

// 0x3c2570 ClearActive, 0x3c25d0 FadeOutUnused, 0x3c2660 Find, 0x3c26c0 FindPoint (the probe a drawn entry's node's
// transform moved by 0, 1, 31, 32, 33 ulps or far, its rotation negated whole or in part), 0x3c2cc0 Clear, 0x3c2e30
// SetKeys (the keys kept or not).
DIFF_TEST(sg_lerp_light_set_queries, 0x3c26c0)
{
    Scope scope(ctx);
    auto clearActive = ctx.Original<void(__thiscall *)(void *)>(0x3c2570);
    auto fadeOut = ctx.Original<void(__thiscall *)(void *)>(0x3c25d0);
    auto find = ctx.Original<int32_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x3c2660);
    auto findPoint = ctx.Original<int32_t(__thiscall *)(void *, uint32_t, uint32_t, const float *)>(0x3c26c0);
    auto clear = ctx.Original<void(__thiscall *)(void *)>(0x3c2cc0);
    auto setKeys = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0x3c2e30);
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3c26c0u);
        World w;
        DrawPools(w, gen);
        DrawEntries(w, gen);
        const uint32_t op = seed % 6;
        uint32_t group = kGroups[gen() % std::size(kGroups)], kind = gen() % 10;
        if (!w.entries.empty() && gen() % 3) {
            const EntrySpec &e = w.entries[gen() % w.entries.size()];
            group = e.group, kind = e.kind;
        }
        float probe[7];
        for (float &f : probe) f = Nice(gen);
        if (!w.entries.empty() && gen() % 4) {
            const EntrySpec &e = w.entries[gen() % w.entries.size()];
            if (e.node >= 0) {
                const World::NodeData &n = w.nodes[e.node];
                const int negate = gen() % 3; // none, the whole rotation, one component
                const int which = 3 + int(gen() % 4);
                for (int k = 0; k < 7; ++k) {
                    probe[k] = gen() % 3 ? n.xf[k] : Ulps(n.xf[k], gen);
                    if (k >= 3 && (negate == 1 || (negate == 2 && k == which))) probe[k] = Fl(U(probe[k]) ^ 0x80000000u);
                }
            }
        }
        const uint32_t tag = gen() % 2 ? w.layerTag : gen() % 4, flag = gen() % 2 ? w.lightFlag : gen() % 3;
        auto call = [&](int side, uint8_t *o) {
            int32_t r = 0;
            switch (op) {
            case 0: side == 0 ? clearActive(o) : Ours(o)->ClearActive(); break;
            case 1: side == 0 ? fadeOut(o) : Ours(o)->FadeOutUnused(); break;
            case 2: r = side == 0 ? find(o, group, kind) : Ours(o)->Find(group, kind); break;
            case 3:
                r = side == 0 ? findPoint(o, group, kind, probe)
                              : Ours(o)->FindPoint(group, kind, reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(probe));
                break;
            case 4: side == 0 ? clear(o) : Ours(o)->Clear(); break;
            default: side == 0 ? setKeys(o, tag, flag) : Ours(o)->SetKeys(tag, flag); break;
            }
            L(0x7e5), L(uint32_t(r));
        };
        static const char *const kOps[] = {"ClearActive", "FadeOutUnused", "Find", "FindPoint", "Clear", "SetKeys"};
        Pair(ctx, kOps[op], seed, w, call);
    }
}

// 0x3c2d80 SetLightEvents, 0x3c2b50 LerpLightEvents: a node answering its attributes or not (the scalar's value then left
// as the stack held it: only the second and third are read back).
DIFF_TEST(sg_lerp_light_set_events, 0x3c2b50)
{
    Scope scope(ctx);
    auto set = ctx.Original<void(__cdecl *)(void *, const float *, float)>(0x3c2d80);
    auto lerp = ctx.Original<void(__cdecl *)(void *, float, const float *, float)>(0x3c2b50);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3c2b50u);
        World w;
        DrawPools(w, gen);
        w.spare = 0;
        const int node = int(gen() % kNodes);
        float color[3];
        for (float &f : color) f = Nice(gen);
        const float value = Nice(gen), t = NearOne(gen);
        const bool lerping = seed % 3 != 0;
        auto call = [&](int side, uint8_t *) {
            auto *n = reinterpret_cast<nGZSceneGraph::cILightRefNode *>(&gNodes[node]);
            if (!lerping)
                side == 0 ? set(n, color, value) : cLerpLightSet::SetLightEvents(n, color, value);
            else
                side == 0 ? lerp(n, t, color, value) : cLerpLightSet::LerpLightEvents(n, t, color, value);
        };
        Pair(ctx, lerping ? "LerpLightEvents" : "SetLightEvents", seed, w, call);
    }
}

// 0x3c2e60 the entry's step (one drawn entry of the set: each kind, with or without its light, texture light and node,
// each flag byte drawn, t around 0.95 and NaN, colours at and near black, directions lerped to nearly nothing), 0x3c3450
// the set's (inactive entries removed from the registry and dropped, the rest moved down: the assignment's counting
// order).
DIFF_TEST(sg_lerp_light_set_evaluate, 0x3c3450)
{
    Scope scope(ctx);
    auto step = ctx.Original<void(__thiscall *)(void *, float)>(0x3c2e60);
    auto evaluate = ctx.Original<void(__thiscall *)(void *, float)>(0x3c3450);
    for (uint32_t seed = 0; seed < 8000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3c3450u);
        World w;
        DrawPools(w, gen);
        DrawEntries(w, gen);
        const bool one = seed % 2 == 0;
        if (one && w.entries.empty()) w.entries.push_back(DrawEntry(gen, 0));
        const uint32_t which = w.entries.empty() ? 0 : gen() % w.entries.size();
        float t = NearOne(gen);
        if (one && seed % 3 == 0) {
            // A direction the step takes to nearly nothing (length 1e-7 to 3e-4, either side of the 1e-6 cut): the node
            // unturned, so its direction is (0, 0, -1), and the entry's direction where the lerp lands next to the origin.
            EntrySpec &e = w.entries[which];
            float *q = w.nodes[e.node].xf + 3;
            q[0] = q[1] = q[2] = 0.0f, q[3] = 1.0f;
            e.lerp[1] = 1;
            t = 0.25f + float(gen() % 64) / 100.0f;
            const float off = (gen() % 2 ? 1.0f : -1.0f) * float(gen() % 3000 + 1) * 1e-7f;
            e.direction[0] = off, e.direction[1] = gen() % 2 ? 0.0f : off;
            e.direction[2] = 1.0f / t - 1.0f;
        }
        auto call = [&](int side, uint8_t *o) {
            if (one)
                side == 0 ? step(First(o) + which, t) : First(o)[which].Evaluate(t);
            else
                side == 0 ? evaluate(o, t) : Ours(o)->Evaluate(t);
        };
        Pair(ctx, one ? "Entry::Evaluate" : "Evaluate", seed, w, call);
    }
}

// 0x3c10a0 AddLight (kinds 0-10: an inactive reusable entry of the group and kind reused, else a new node and light; the
// framework failing on drawn calls), 0x3c1600 AddDirectional (kinds 1-5), 0x3c1a80 AddExisting (an entry's node or the
// caller's own), 0x3c1d60 AddPoint (scales at and below 0 and NaN; an entry's transform within NearlyEqual or not).
DIFF_TEST(sg_lerp_light_set_creators, 0x3c10a0)
{
    Scope scope(ctx);
    auto addLight =
        ctx.Original<void(__thiscall *)(void *, uint32_t, const char *, uint32_t, const float *, float, float, float)>(0x3c10a0);
    auto addDirectional = ctx.Original<void(__thiscall *)(void *, uint32_t, const char *, uint32_t, const float *, float,
                                                         const float *, float, float)>(0x3c1600);
    auto addExisting = ctx.Original<void(__thiscall *)(void *, uint32_t, const char *, void *, uint32_t, const float *, float,
                                                      float)>(0x3c1a80);
    auto addPoint = ctx.Original<void(__thiscall *)(void *, uint32_t, const char *, const float *, const float *,
                                                   const float *, const float *, float, float)>(0x3c1d60);
    for (uint32_t seed = 0; seed < 8000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3c10a0u);
        World w;
        DrawPools(w, gen);
        DrawEntries(w, gen, 5);
        for (EntrySpec &e : w.entries)
            if (gen() % 2) e.active = 0, e.reusable = 1;
        const uint32_t op = seed % 4;
        uint32_t group = kGroups[gen() % std::size(kGroups)];
        static const uint32_t kLightKinds[] = {0, 1, 6, 7, 8, 2, 5, 9, 10};
        static const uint32_t kDirectionalKinds[] = {2, 3, 4, 1, 5};
        uint32_t kind = op == 0 ? kLightKinds[gen() % std::size(kLightKinds)] : kDirectionalKinds[gen() % 5];
        if (!w.entries.empty() && gen() % 3 == 0) {
            const EntrySpec &e = w.entries[gen() % w.entries.size()];
            group = e.group;
            if (op < 2) kind = e.kind;
        }
        const char *name = kNames[gen() % std::size(kNames)];
        float color[3], direction[3], position[3], xf[7];
        for (float &f : color) f = Nice(gen);
        for (float &f : direction) f = Nice(gen);
        for (float &f : position) f = Nice(gen);
        for (float &f : xf) f = Nice(gen);
        if (op == 3 && !w.entries.empty() && gen() % 2) {
            const EntrySpec &e = w.entries[gen() % w.entries.size()];
            if (e.node >= 0) {
                for (int k = 0; k < 7; ++k) xf[k] = gen() % 4 ? w.nodes[e.node].xf[k] : Ulps(w.nodes[e.node].xf[k], gen);
                if (gen() % 3 == 0)
                    for (int k = 3; k < 7; ++k) xf[k] = Fl(U(xf[k]) ^ 0x80000000u);
            }
        }
        static const uint32_t kScales[] = {0x3f800000, 0x00000000, 0x80000000, 0xbf800000, 0x7fc00000, 0x00000001};
        const float scale = gen() % 2 ? Nice(gen) : Fl(kScales[gen() % std::size(kScales)]);
        const float a = Nice(gen), b = Nice(gen), value = Nice(gen);
        int node = kOwnNodes + int(gen() % (kNodes - kOwnNodes));
        if (op == 2 && !w.entries.empty() && gen() % 2 && w.entries[gen() % w.entries.size()].node >= 0)
            node = w.entries[gen() % w.entries.size()].node;
        const uint32_t unused = gen();
        auto call = [&](int side, uint8_t *o) {
            void *n = node >= 0 ? &gNodes[node] : nullptr;
            const auto *t = reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(xf);
            switch (op) {
            case 0:
                side == 0 ? addLight(o, group, name, kind, color, scale, a, b)
                          : Ours(o)->AddLight(group, name, kind, color, scale, a, b);
                break;
            case 1:
                side == 0 ? addDirectional(o, group, name, kind, color, scale, direction, a, b)
                          : Ours(o)->AddDirectional(group, name, kind, color, scale, direction, a, b);
                break;
            case 2:
                side == 0 ? addExisting(o, group, name, n, unused, color, scale, value)
                          : Ours(o)->AddExisting(group, name, static_cast<nGZSceneGraph::cILightRefNode *>(n), unused,
                                                 color, scale, value);
                break;
            default:
                side == 0 ? addPoint(o, group, name, xf, direction, position, color, scale, value)
                          : Ours(o)->AddPoint(group, name, t, direction, position, color, scale, value);
                break;
            }
        };
        static const char *const kOps[] = {"AddLight", "AddDirectional", "AddExisting", "AddPoint"};
        Pair(ctx, kOps[op], seed, w, call);
    }
}

// 0x3c2120 BuildRenderGroupLights: the light set's enabled bits drawn (indoor sets, the ambient, the environment), the
// render groups' bits 32 and above included, the set's entries drawn with the groups the builder asks for (so some are
// reused), light 0's kind drawn.
DIFF_TEST(sg_lerp_light_set_build, 0x3c2120)
{
    Scope scope(ctx);
    auto build = ctx.Original<void(__cdecl *)(const void *, void *, uint32_t, float)>(0x3c2120);
    alignas(8) static uint8_t lightSet[sizeof(cLightSet)];
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x3c2120u);
        World w;
        DrawPools(w, gen);
        DrawEntries(w, gen, 6);
        float words[sizeof(cLightSet) / 4];
        for (float &f : words) f = Nice(gen);
        uint32_t enabled = gen() & 0x3ff;
        if (gen() % 3 == 0) enabled &= ~0x70u;
        // Groups the builder asks for (a mask is any OR of the drawn bits), so that some entries are reused.
        auto bit = [&](int i) { return 1u << (w.bits[i] & 31); };
        const uint32_t objects = 1u | bit(1) | bit(2) | bit(3) | bit(4);
        const uint32_t masks[] = {bit(0), objects, objects | bit(0), objects | bit(0) | bit(5) | bit(6) | bit(7)};
        static const uint32_t kKinds[] = {0, 2, 3, 4, 8};
        for (EntrySpec &e : w.entries) {
            if (gen() % 2) e.group = masks[gen() % 4], e.kind = kKinds[gen() % 5];
            if (gen() % 2) e.active = 0, e.reusable = 1;
        }
        static const uint32_t kKind0[] = {2, 3, 4, 2, 0, 5};
        const uint32_t kind0 = kKind0[gen() % std::size(kKind0)];
        const float a7 = Nice(gen);
        auto call = [&](int side, uint8_t *o) {
            std::memcpy(lightSet, words, sizeof lightSet);
            std::memcpy(lightSet + 0x20, &enabled, 4);
            side == 0 ? build(lightSet, o, kind0, a7)
                      : cLerpLightSet::BuildRenderGroupLights(reinterpret_cast<const cLightSet *>(lightSet), Ours(o), kind0, a7);
        };
        Pair(ctx, "BuildRenderGroupLights", seed, w, call);
    }
}
