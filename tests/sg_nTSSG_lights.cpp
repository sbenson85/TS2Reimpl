// Differential tests of the nTSSG lights (lane 261108n, scenegraph LG7a; src/sg/nTSSGLights.h): the game's six
// constructors and their classes' vtables against ours. The original side builds each light with the game's constructor
// in a block from the game's operator new 0x824632 (its vtables, its bodies); our side builds it as cLerpLightSet does,
// with our constructor at its C name (the hook target) in a block from our operator new (our vtables, our bodies). Both
// run the same operations through every face (the part +0, the node +0x2c, the persist face +0x34, the virtual base),
// then release the lights through a random face (or delete them through the part's slot 12 or the node face's deleting
// slot), and compare the calls on the fakes, the notes, every word of each light but its vtable words, vbptr, referrer
// vector (by content instead) and the padding at +0x2a, and the blocks freed.
//
//   SG_nTSSGLights_Slots     random lights of the six classes and random operations (the standard lights' slots on the
//                            four standard-based ones; the light's own slots on the shade lights), reads, clones (the
//                            shade lights' leaked clones caught at operator new: CloneCatch).
//   SG_nTSSGLights_Params    one fed light of each standard-based class: GetParams on random shader items and the
//                            strengths on random boxes and points, the fakes answering near the thresholds.
//   SG_nTSSGLights_TileUnder TileUnder 0x3c9bd0 on random points against the world fakes.
//
// The fakes: the light map (slot 16, the colour at a point), the lighting manager (slot 32, the sun's visibility at a
// point), the draw record's object (slot 54, its placement; slot 48), the item's bone source (slot 5, a bone set whose box
// is at +0x1c), the world DBs (the placement service's, cell 0x10cc298 read by 0x20e0b2, and the cached one, cell
// 0x10cc45c), and the render-group registry 0xaeb370 (sg_renderable_fx.h's stand-in: "NoHMShadow" and "Portals" take the
// first free bits of a random set).
//
// Covered here (db/status/nTSSGLights.jsonl): the constructors 0x3c7d30, 0x3c7fd0, 0x3c7e80, 0x3c7de0, 0x3c80b0 and
// 0x3c8120 (hooked); in effect through those callers (the game's vtables are stored by nothing that runs; the original
// side reaches each through them): the deleting destructors 0x3c8260, 0x3c82c0, 0x3c82f0, 0x3c8360, 0x3c83d0, 0x3c83f0
// and the level's 0x3c8240 with its destructor 0x3c81c0, their node-face thunks 0x3c820a, 0x3c8212, 0x3c821a, 0x3c8222,
// 0x3c822a, 0x3c8232, 0x3c8202; Clone 0x3c84e0 / 0x3c8560; GetParams 0x3c8a00 / 0x3c8cc0; the strengths 0x3c9060,
// 0x3c9070, 0x3c9360, 0x3c93b0, 0x3c9570, 0x3c9630, 0x3c96b0; the shade lights' type info 0x3c9b20 and Write 0x3c9da0;
// TileUnder 0x3c9bd0; and the virtual base's class-specific vtordisp thunks (slots 0-24, through GetResource()):
// 0x3c899b 0x3c89a6 0x3c89b1 0x3c89bc 0x3c89c7 0x3c89d5 0x3c8459 0x3c8464 0x3c846f 0x3c847a 0x3c8488 0x3c8fdc 0x3c8fe7
// 0x3c8ff2 0x3c8ffd 0x3c9008 0x3c9016 0x3c8f09 0x3c8f14 0x3c8f1f 0x3c8f2a 0x3c8f35 0x3c8f40 0x3c9af6 0x3c9b01 0x3c9b0c
// 0x3c9b9c 0x3c9ba7 0x3c9bb2 0x3c9bbd 0x3c9d73 0x3c9d7e 0x3c9d89 0x3c9e17 0x3c9e22 0x3c9e2d 0x3c9e38 0x3c977a 0x3c9785
// 0x3c9790 0x3c979b 0x3c97a6 0x3c84a1 0x3c84ac 0x3c84c2 0x3c84cd 0x3c9738 0x3c9743 0x3c9759 0x3c9764 0x3c996a 0x3c9975
// 0x3c998b 0x3c9996 0x3c8872 0x3c887d 0x3c8893 0x3c889e 0x3c9a72 0x3c9a7d 0x3c9a93 0x3c9a9e 0x3c99ee 0x3c99f9 0x3c9a0f
// 0x3c9a1a 0x3c9a30 0x3c9a3b 0x3c9a51 0x3c9a5c 0x3c9ab4 0x3c9abf 0x3c9ad5 0x3c9ae0 0x3c99ac 0x3c99b7 0x3c99cd 0x3c99d8
// 0x3c88f6 0x3c8901 0x3c8917 0x3c8922 0x3c8964 0x3c896f 0x3c897a 0x3c8985 0x3c8990 0x3c892d 0x3c8938 0x3c8943 0x3c894e
// 0x3c97fe 0x3c981f 0x3c982a 0x3c8830 0x3c883b 0x3c8851 0x3c885c 0x3c97bc 0x3c97c7 0x3c97dd 0x3c97e8 0x3c88b4 0x3c88bf
// 0x3c88d5 0x3c88e0 0x3c8f56 0x3c8f61 0x3c8f77 0x3c8f82. The shade lights' abstract base cLightT<cILight, 4000>'s own
// bodies (0x3c8240, 0x3c8202, 0x3c9af6, 0x3c9d73), which only its own vtables hold (live only while its constructor or
// destructor runs), are called directly on game-built shade lights on the original side, against our light's slots.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "game/vtable.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "misc/UtilitiesC.h"   // AffineTransform3
#include "sg_renderable_fx.h"  // the render-group registry stand-in
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "sg/nTSSGLights.h"
#include "world/cWorldDB.h"

#include <array>
#include <bit>
#include <cstring>
#include <initializer_list>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kNode = 0x2c;  // the node face
constexpr uint32_t kVbptr = 0x1c; // the cSGResource part's vbptr
constexpr uint32_t kLightTag = 0x7d000000;
constexpr unsigned kMax = 10;
constexpr unsigned kSeeds = 64;
constexpr uint32_t kGameNew = 0x824632;
constexpr uint32_t kWorldCell = 0x10cc298;  // 0x20e0b2's cached world DB (TileUnder's)
constexpr uint32_t kCachedCell = 0x10cc45c; // worldsvc::CachedWorldDB's (the light-map ambient's clamp)
// The shade lights' abstract base cLightT<cILight, 4000> (its vtables 0xdbf530 / 0xdbf568 / 0xdbf610 / 0xdbf630 live
// only while its constructor or destructor runs): its own bodies the derived vtables don't hold, called directly on the
// original side against our light's slot (the same work for the object they could ever see).
constexpr uint32_t kLevelDeleting = 0x3c8240, kLevelDeletingThunk = 0x3c8202; // part 12, node face 40
constexpr uint32_t kLevelTypeInfoThunk = 0x3c9af6, kLevelWriteThunk = 0x3c9d73; // virtual base 4, 5
constexpr uint32_t kLevelVtables[] = {0xdbf530, 0xdbf568, 0xdbf610, 0xdbf630};  // part, node, persist, virtual base

// The standard lights' event registry (0xa49210): its static map, guards and holder.
constexpr sgfx::RegistryStatics kLightRegistry{0x10f6138, 0x10f6140, 0x10f6150, 0x10f6158};

// What a held light is: the six classes, or a standard light a Clone made (the standard-based lights' Clones are their
// bases': an ambient or directional light, or the point light for the half-space light).
enum Kind : uint8_t { kPlain, kPoint, kAmbientMap, kHeightMap, kBinary, kHalfspace, kShade, kTerrain };
constexpr uint32_t kKindSize[] = {0x70, 0x90, 0x7c, 0x80, 0xb0, 0xa0, 0x64, 0x58};
bool Standard(Kind k) { return k < kShade; }
// Left out of the word dump: the vtables and the vbptr (words 0, 7, 11, 13 and the last, the virtual base's), the
// referrer vector (16-18, by content instead) and word 10 (+0x28; its upper half is padding: its low half is noted).
constexpr uint64_t kSkipCommon = (1u << 0) | (1u << 7) | (1u << 10) | (1u << 11) | (1u << 13) | (1u << 16) | (1u << 17) |
                                 (1u << 18);

// ---- the fakes' tables ----
struct SlotAt {
    unsigned index;
    Slot slot;
};
template <size_t N> constexpr std::array<Slot, N> Slots(std::initializer_list<SlotAt> set)
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const SlotAt &s : set) t[s.index] = s.slot;
    return t;
}
// nTSSG::cLightMapManager: 16 (out, {x, y, level}) the colour there.
constexpr auto kLightMapSlots = Slots<17>({{16, {2, Ret::Int, true, "ColourAt", "aW3"}}});
constexpr drawfx::Iface kLightMap{"LightMap", 17, kLightMapSlots.data(), false, drawfx::kTestTable};
// nTSSG::cLightingManager: 32 (point) the sun's visibility there.
constexpr auto kSunSlots = Slots<33>({{32, {1, Ret::Float, true, "SunVisibility", "F3"}}});
constexpr drawfx::Iface kSun{"LightingManager", 33, kSunSlots.data(), false, drawfx::kTestTable};
// The draw record's object: 48 (0x17) a bool, 54 (scratch) its placement.
constexpr auto kObjectSlots = Slots<55>({{48, {1, Ret::Int, true, "Slot48", "u"}}, {54, {1, Ret::Int, true, "Placement", "a"}}});
constexpr drawfx::Iface kObject{"RecordObject", 55, kObjectSlots.data(), false, drawfx::kTestTable};
// The item's bone source: 5 (index) a bone set.
constexpr auto kBonesSlots = Slots<6>({{5, {1, Ret::Int, true, "BoneSet", "u"}}});
constexpr drawfx::Iface kBones{"BoneSource", 6, kBonesSlots.data(), false, drawfx::kTestTable};
// nTSWorld::cIWorldDB: the six slots the lights ask.
constexpr auto kWorldSlots = Slots<90>({{50, {3, Ret::Int, true, "HasFloor", "abb"}},
                                        {62, {1, Ret::Float, true, "GetElevation", "a"}},
                                        {72, {2, Ret::Int, true, "IsInWorld", "ff"}},
                                        {77, {0, Ret::Float, true, "GetMaxXF", ""}},
                                        {78, {0, Ret::Float, true, "GetMaxYF", ""}},
                                        {89, {0, Ret::Int, true, "GetMaxLevel", ""}}});
constexpr drawfx::Iface kWorld{"WorldDB", 90, kWorldSlots.data(), false, drawfx::kTestTable};

// The configuration service's settings object the point light reads its cutoff from (as sg_lights_standard.cpp's).
constexpr drawfx::Slot kSettingsSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"}, {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},          {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {2, drawfx::Ret::Int, true, "GetProperty", "ii"},
};
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots};

struct World {
    cILight *lights[kMax];
    Kind kind[kMax];
    bool fed[kMax]; // built with its light map / manager (not null)
    unsigned count = 0;
    Fake ext[3];      // ids 200-202
    Fake referrer[3]; // ids 600-602
    Fake context;
    sgfx::Manager manager;
};
World w;
Fake settings, lightMap, sun, object, bones, world, cached;

// The shader item GetParams is given (+0xc the bone source, +0x14 the bone set's index, +0x2c the draw record), the
// record (+0 the object, +4 its render groups, +0x44 its box), the bone set (+0x1c its box) and the placement
// (translation, rotation) the object answers.
alignas(16) uint8_t gItem[0x30];
alignas(16) uint8_t gRecord[0x60];
alignas(16) uint8_t gBoneSet[0x40];
alignas(16) float gPlacement[8];
alignas(16) float gLit[4]; // what the light map's slot 16 returns (its out-parameter gets other values)

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }
float Float(uint32_t u) { return std::bit_cast<float>(u); }

uint32_t SettingsGet(Fake &self, const uint32_t *a)
{
    if (!self.value[1]) return 0;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1]) + 4) = self.value[0];
    return 1;
}
uint32_t FrameworkService(Fake &self, const uint32_t *a)
{
    if (a[0] == 0x0ac1887e && a[1] == 0x4bb0d269) {
        ++settings.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &settings;
        return 1;
    }
    return sgfx::detail::GetSystemService(self, a);
}

// The light map: the out-parameter filled with one colour, another returned (each light reads what it reads).
uint32_t LightMapColour(Fake &self, const uint32_t *a)
{
    float *out = reinterpret_cast<float *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 3; ++i) out[i] = Float(self.value[i]);
    return P(gLit);
}
// The sun's visibility: the next of a sequence about the thresholds.
uint32_t SunVisibility(Fake &self, const uint32_t *)
{
    static const float kValues[] = {1.0f, 0.0f, 0.5f, 0.25f, 0.75f, 0.3f, 2.0f, 0.2500001f, -1.0f};
    return Bits(kValues[(self.value[0]++ + self.value[1]) % 9]);
}
uint32_t ObjectSlot48(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t ObjectPlacement(Fake &, const uint32_t *) { return P(gPlacement); }
uint32_t BoneSet(Fake &, const uint32_t *) { return P(gBoneSet); }

// The world DB: floors by level (value[0]'s bits), elevations by level (value[2] + 3 * level), the top level value[1],
// IsInWorld value[3], the extents value[4] / value[5]. The tiles asked about are noted (no segment word for GetElevation:
// TileUnder leaves its copy's unset).
uint32_t WorldHasFloor(Fake &self, const uint32_t *a)
{
    const auto *t = reinterpret_cast<const nTSWorld::WorldTile *>(uintptr_t(a[0]));
    drawfx::Note("floor tile", {self.id, uint32_t(t->x), uint32_t(t->y), uint32_t(t->level), uint32_t(uint8_t(t->segments))});
    return (self.value[0] >> (uint32_t(t->level) & 31)) & 1;
}
uint32_t WorldElevation(Fake &self, const uint32_t *a)
{
    const auto *t = reinterpret_cast<const nTSWorld::WorldTile *>(uintptr_t(a[0]));
    drawfx::Note("elevation tile", {self.id, uint32_t(t->x), uint32_t(t->y), uint32_t(t->level)});
    return Bits(Float(self.value[2]) + 3.0f * float(t->level));
}
uint32_t WorldMaxLevel(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t WorldInWorld(Fake &self, const uint32_t *) { return self.value[3]; }
uint32_t WorldMaxX(Fake &self, const uint32_t *) { return self.value[4]; }
uint32_t WorldMaxY(Fake &self, const uint32_t *) { return self.value[5]; }

void MakeWorld(Fake &f, uint32_t id, std::mt19937 &rng)
{
    f.Reset<kWorld>(id);
    f.On(50, &WorldHasFloor);
    f.On(62, &WorldElevation);
    f.On(72, &WorldInWorld);
    f.On(77, &WorldMaxX);
    f.On(78, &WorldMaxY);
    f.On(89, &WorldMaxLevel);
    f.value[0] = uint32_t(rng());
    const int32_t tops[] = {0, 1, 2, 3, 5, -1};
    f.value[1] = uint32_t(tops[rng() % 6]);
    const float offsets[] = {0.0f, -0.5f, 2.99f, 0.01f};
    f.value[2] = Bits(offsets[rng() % 4]);
    f.value[3] = rng() % 3 ? 1u : rng() % 2 ? 0u : 0x100u;
    const float extents[] = {64.0f, 10.5f, 128.0f, 0.0f};
    f.value[4] = Bits(extents[rng() % 4]);
    f.value[5] = Bits(extents[rng() % 4]);
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gPool.Handle("alpha");
    sgfx::gPool.Handle("sun_lght");
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::gWriter.fake.Returns(23, 0x2300u + seed);
    sgfx::gResources.Reset(cells);
    settings.Reset<kSettings>(463);
    settings.On(9, &SettingsGet);
    const float cutoffs[] = {0.25f, 0.5f, 2.0f, 0.0f};
    settings.value[0] = Bits(cutoffs[seed % 4]);
    settings.value[1] = seed % 3 != 0;
    sgfx::gResources.framework.On(5, &FrameworkService);
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
    for (unsigned i = 0; i < 3; ++i) {
        w.referrer[i].Reset<sgfx::kBehavior>(600 + i);
        w.referrer[i].Returns(4, uint32_t(rng() % 2));
    }
    w.count = 0;
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cStandardLightBehaviorTraits::kEventManagerCell);
    sgrx::Setup(rng); // the render-group registry: a random set of named bits

    lightMap.Reset<kLightMap>(700);
    lightMap.On(16, &LightMapColour);
    const float colours[] = {0.0f, 0.5f, 1.0f, 0.125f, 2.0f, -0.25f};
    for (unsigned i = 0; i < 3; ++i) lightMap.value[i] = Bits(colours[rng() % 6]);
    for (unsigned i = 0; i < 3; ++i) gLit[i] = colours[rng() % 6];
    sun.Reset<kSun>(701);
    sun.On(32, &SunVisibility);
    sun.value[1] = uint32_t(rng() % 9);
    object.Reset<kObject>(702);
    object.On(48, &ObjectSlot48);
    object.On(54, &ObjectPlacement);
    bones.Reset<kBones>(703);
    bones.On(5, &BoneSet);
    MakeWorld(world, 704, rng);
    MakeWorld(cached, 705, rng);
    cells.Set(kWorldCell, &world);
    cells.Set(kCachedCell, rng() % 4 ? static_cast<void *>(&cached) : static_cast<void *>(&world));

    drawfx::AddRange(gItem, sizeof gItem, 0x7b010000);
    drawfx::AddRange(gRecord, sizeof gRecord, 0x7b020000);
    drawfx::AddRange(gBoneSet, sizeof gBoneSet, 0x7b030000);
    drawfx::AddRange(gPlacement, sizeof gPlacement, 0x7b040000);
    drawfx::AddRange(gLit, sizeof gLit, 0x7b050000);
}

// ---- the lights ----
uint8_t *Object(cILight *n) { return reinterpret_cast<uint8_t *>(n) - kNode; }
cIGZPersistResource *PartFace(cILight *n) { return reinterpret_cast<cIGZPersistResource *>(Object(n)); }
cIGZPersistSerializableReferent *PersistFace(cILight *n) { return n->GetReferent(); }
uint32_t CountOf(cILight *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }
cIGZUnknown *Unknown(Fake &f) { return reinterpret_cast<cIGZUnknown *>(&f); }

// A held light (counted by the test); false (the light released) when the test holds kMax already.
bool Hold(cILight *n, Kind kind, bool fed, bool counted)
{
    if (w.count >= kMax) {
        if (counted) n->Release();
        return false;
    }
    drawfx::AddRange(Object(n), kKindSize[kind], kLightTag | (w.count << 8));
    if (!counted) n->AddRef();
    w.kind[w.count] = kind;
    w.fed[w.count] = fed;
    w.lights[w.count++] = n;
    return true;
}

Kind KindOf(cILight *n)
{
    for (unsigned i = 0; i < w.count; ++i)
        if (w.lights[i] == n) return w.kind[i];
    return kPlain;
}

using GameNew = void *(__cdecl *)(size_t);
// The constructors: thiscall (`this` in ecx, edx unused), the most-derived flag last, the object returned.
using AmbientMapCtor = void *(__fastcall *)(void *, void *, cIGZUnknown *map, float weight, float scale, int flag);
using HeightMapCtor = void *(__fastcall *)(void *, void *, cIGZUnknown *manager, float value, int flag);
using BinaryCtor = void *(__fastcall *)(void *, void *, cIGZUnknown *manager, const float *direction, float margin,
                                        float threshold, int flag);
using HalfspaceCtor = void *(__fastcall *)(void *, void *, const float *normal, const float *point, int flag);
using ShadeCtor = void *(__fastcall *)(void *, void *, const float *values, int flag);
using TerrainCtor = void *(__fastcall *)(void *, void *, cIGZUnknown *manager, int flag);

// The game's constructor (original) or ours at its C name, the hook's target.
template <class Fn> Fn Ctor(difftest::Context &ctx, bool original, uint32_t rva, const uint8_t &ours)
{
    return original ? ctx.Original<Fn>(rva) : reinterpret_cast<Fn>(const_cast<uint8_t *>(&ours));
}

Kind RandomKind(std::mt19937 &rng) { return Kind(kAmbientMap + rng() % 6); }

// The last half-space light's construction point: a point exactly on its plane (StrengthAt's boundary).
float gHalfspacePoint[3];

// A new light of `kind` from random arguments, held (null when the test holds kMax already).
cILight *Make(difftest::Context &ctx, bool original, Kind kind, std::mt19937 &rng, bool fed = true)
{
    if (w.count >= kMax) return nullptr;
    const float values[] = {0.0f, 0.5f, 1.0f, -2.0f, 3.25f, 0.25f};
    const float a = values[rng() % 6], b = values[rng() % 6];
    float direction[3] = {values[rng() % 6], -0.75f, values[rng() % 6]};
    if (rng() % 4 == 0) { // straight down or straight up: the rotation's two edge cases
        direction[0] = direction[1] = 0.0f;
        direction[2] = rng() % 2 ? 1.0f : -1.0f;
    }
    const float point[3] = {values[rng() % 6], values[rng() % 6], 1.5f};
    const float shade[4] = {1.0f, values[rng() % 6], values[rng() % 6], values[rng() % 6]};
    cIGZUnknown *const map = fed ? Unknown(lightMap) : nullptr;
    cIGZUnknown *const manager = fed ? Unknown(sun) : nullptr;
    void *const memory = original ? ctx.Original<GameNew>(kGameNew)(kKindSize[kind]) : ::operator new(kKindSize[kind]);
    void *made = nullptr;
    switch (kind) {
    case kAmbientMap:
        made = Ctor<AmbientMapCtor>(ctx, original, 0x3c7d30, cAmbientFromLightMapLight_Constructor)(memory, nullptr, map,
                                                                                                    a, b, 1);
        break;
    case kHeightMap:
        made = Ctor<HeightMapCtor>(ctx, original, 0x3c7fd0, cHeightMapShadowLight_Constructor)(memory, nullptr, manager, a,
                                                                                               1);
        break;
    case kBinary:
        made = Ctor<BinaryCtor>(ctx, original, 0x3c7e80, cHeightMapBinaryShadowLight_Constructor)(memory, nullptr, manager,
                                                                                                  direction, a, b, 1);
        break;
    case kHalfspace:
        std::memcpy(gHalfspacePoint, point, sizeof gHalfspacePoint);
        made = Ctor<HalfspaceCtor>(ctx, original, 0x3c7de0, cHalfspacePointLight_Constructor)(memory, nullptr, direction,
                                                                                              point, 1);
        break;
    case kShade:
        made = Ctor<ShadeCtor>(ctx, original, 0x3c80b0, cShadeLight_Constructor)(memory, nullptr, shade, 1);
        break;
    default:
        made = Ctor<TerrainCtor>(ctx, original, 0x3c8120, cTerrainShadeLight_Constructor)(memory, nullptr, manager, 1);
        break;
    }
    cILight *const n = reinterpret_cast<cILight *>(static_cast<uint8_t *>(made) + kNode);
    drawfx::Note("made", {uint32_t(kind), uint8_t(made == memory), CountOf(n), uint32_t(lightMap.refs), uint32_t(sun.refs)});
    // The constructors leave the count at 0 (the caller's AddRef): held uncounted, then released once.
    Hold(n, kind, fed, false);
    return n;
}

// A light's words (the skipped ones left out), the part's flags, its vbtable's entries, its referrers.
void NoteObject(const char *label, unsigned index, const uint8_t *object, Kind kind)
{
    const uint32_t *words = reinterpret_cast<const uint32_t *>(object);
    drawfx::Call c;
    c.note = label;
    c.words.push_back(index);
    const unsigned count = kKindSize[kind] / 4;
    const uint64_t skip = kSkipCommon | (1ull << (count - 1));
    for (unsigned k = 0; k < count; ++k)
        if (!(skip >> k & 1)) c.words.push_back(drawfx::Norm(words[k]));
    c.words.push_back(words[10] & 0xffff);
    const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(object + kVbptr);
    c.words.push_back(uint32_t(vbtable[0]));
    c.words.push_back(uint32_t(vbtable[1]));
    const auto *first = reinterpret_cast<Fake *const *>(uintptr_t(words[16]));
    const auto *last = reinterpret_cast<Fake *const *>(uintptr_t(words[17]));
    const auto *end = reinterpret_cast<Fake *const *>(uintptr_t(words[18]));
    c.words.push_back(first != nullptr);
    c.words.push_back(uint32_t(last - first));
    c.words.push_back(uint32_t(end - first));
    for (auto *p = first; p && p != last; ++p) c.words.push_back((*p)->id);
    drawfx::gLog.push_back(std::move(c));
}

void NoteLights(const char *label)
{
    for (unsigned i = 0; i < w.count; ++i) NoteObject(label, i, Object(w.lights[i]), w.kind[i]);
}

// The shade lights' Clones hand nothing out (the clone leaks): to compare what they built, operator new (the image's
// 0x824632 and ours) is stood in for during the call, and the first block of the light's size it hands out is the clone.
// The blocks come from malloc and are never freed (the clone leaks in the game too).
struct CloneCatch {
    static inline size_t size = 0;
    static inline void *block = nullptr;
    static void *__cdecl New(size_t n)
    {
        void *const p = std::malloc(n ? n : 1);
        if (!p) std::abort();
        if (!block && n == size) block = p;
        return p;
    }
    shape::CodePatch image, ours;
    explicit CloneCatch(size_t watch)
        : image(kGameNew, reinterpret_cast<const void *>(&New)),
          ours(reinterpret_cast<void *>(static_cast<void *(*)(size_t)>(&::operator new)), reinterpret_cast<const void *>(&New))
    {
        size = watch;
        block = nullptr;
    }
};

void NoteFakes()
{
    for (const Fake &e : w.ext) drawfx::Note("extension", {e.id, uint32_t(e.refs), drawfx::Norm(e.value[0]), drawfx::Norm(e.value[1])});
    for (const Fake &r : w.referrer) drawfx::Note("referrer", {r.id, uint32_t(r.refs)});
    drawfx::Note("fakes", {uint32_t(lightMap.refs), uint32_t(sun.refs), uint32_t(settings.refs)});
    sgfx::gPool.NoteCounts();
    sgfx::gResources.NoteRefs();
}

// Lets go of the test's references, each through a random face, or (the last reference) deletes the light through the
// part's deleting destructor (slot 12) or the node face's (58 on the standard lights, 40 on the shade lights); notes the
// sizes of the blocks freed and what the lights held.
void ReleaseAll(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    unsigned via[kMax];
    bool last[kMax];
    for (unsigned i = 0; i < w.count; ++i) {
        via[i] = rng() % 9;
        last[i] = PartFace(w.lights[i])->GetRefCount() == 1;
        drawfx::Note("release", {i, via[i], uint8_t(last[i])});
    }
    difftest::FreeRecorder freed;
    for (unsigned i = 0; i < w.count; ++i) {
        cILight *n = w.lights[i];
        using Deleting = void *(__thiscall *)(void *, unsigned);
        if (last[i] && via[i] >= 7 && w.kind[i] == kShade && original) // the abstract level's own deleting bodies
            ctx.Original<Deleting>(via[i] == 7 ? kLevelDeleting : kLevelDeletingThunk)(via[i] == 7 ? Object(n) : static_cast<void *>(n), 1u);
        else if (last[i] && via[i] == 7 && w.kind[i] == kShade)
            RawSlot<12, void *>(Object(n), 1u);
        else if (last[i] && via[i] == 8 && w.kind[i] == kShade)
            RawSlot<40, void *>(n, 1u);
        else if (last[i] && via[i] == 5)
            RawSlot<12, void *>(Object(n), 1u);
        else if (last[i] && via[i] == 6 && Standard(w.kind[i]))
            RawSlot<58, void *>(n, 1u);
        else if (last[i] && via[i] == 6)
            RawSlot<40, void *>(n, 1u);
        else if (via[i] % 4 == 1)
            PersistFace(n)->Release();
        else if (via[i] % 4 == 2)
            PartFace(n)->Release();
        else if (via[i] % 4 == 3)
            n->GetResource()->Release();
        else
            n->Release();
    }
    w.count = 0;
    freed.Stop();
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    NoteFakes();
}

void NoteFloats(const char *label, const float *f, unsigned n)
{
    drawfx::Call c;
    c.note = label;
    for (unsigned i = 0; i < n; ++i) c.words.push_back(Bits(f[i]));
    drawfx::gLog.push_back(std::move(c));
}
void NoteFloats(const char *label, std::initializer_list<float> f) { NoteFloats(label, f.begin(), unsigned(f.size())); }

// A real event body (a cNullEvent) for the notifications.
void *NullEventBody()
{
    static const void *body;
    body = GameAddress<const void>(nGZEvent::cNullEvent::kVTable);
    return &body;
}

// The bit of a render-group name in the registry (0 when it has none yet).
uint32_t GroupBit(const char *name)
{
    const sgrx::Registry &r = sgrx::Registry::Get();
    for (unsigned i = 0; i < 32; ++i)
        if (r.names[i] && std::strcmp(r.names[i], name) == 0) return 1u << i;
    return 0;
}

void RandomBox(float *box, std::mt19937 &rng)
{
    static const float kBoxes[][6] = {{-0.5f, 0.25f, -1.0f, 0.5f, 1.0f, 0.75f},
                                      {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 2.0f},
                                      {2.0f, -3.0f, 0.5f, 2.5f, 4.0f, 0.5f},
                                      {-10.0f, -10.0f, -1.0f, 10.0f, 10.0f, 30.0f},
                                      {1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f}};
    std::memcpy(box, kBoxes[rng() % 5], sizeof(float) * 6);
}

// A shader item: random groups (the two named ones among them), boxes, placement and slot-48 answer.
uint32_t FillItem(std::mt19937 &rng)
{
    std::memset(gItem, 0xab, sizeof gItem);
    std::memset(gRecord, 0xab, sizeof gRecord);
    std::memset(gBoneSet, 0xab, sizeof gBoneSet);
    *reinterpret_cast<void **>(gItem + 0xc) = &bones;
    *reinterpret_cast<uint16_t *>(gItem + 0x14) = uint16_t(rng() % 3 ? rng() % 8 : 0xffff);
    *reinterpret_cast<void **>(gItem + 0x2c) = gRecord;
    *reinterpret_cast<void **>(gRecord) = &object;
    const uint32_t portals = GroupBit("Portals"), noShadow = GroupBit("NoHMShadow");
    const uint32_t groups[] = {0, portals, noShadow, portals | noShadow, uint32_t(rng()), ~0u, 1u << (rng() % 32)};
    *reinterpret_cast<uint32_t *>(gRecord + 4) = groups[rng() % 7];
    RandomBox(reinterpret_cast<float *>(gRecord + 0x44), rng);
    RandomBox(reinterpret_cast<float *>(gBoneSet + 0x1c), rng);
    static const float kTranslations[][3] = {{1.5f, 2.25f, 0.5f}, {-3.0f, 70.0f, 12.0f}, {10.75f, 10.25f, 3.0f},
                                             {0.0f, 0.0f, 0.0f},  {200.0f, -5.0f, 1.0f}, {4.0f, 4.5f, 6.0f},
                                             {7.25f, 7.75f, -0.5f}};
    static const float kRotations[][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.6f, 0.8f}, {0.6f, 0.0f, 0.0f, 0.8f},
                                          {0.0f, 0.28f, 0.0f, 0.96f}};
    std::memcpy(gPlacement, kTranslations[rng() % 7], sizeof(float) * 3);
    std::memcpy(gPlacement + 3, kRotations[rng() % 4], sizeof(float) * 4);
    gPlacement[7] = -9.0f;
    const uint32_t answers[] = {0, 1, 0x100, 0xffffff01};
    object.value[0] = answers[rng() % 4];
    return P(gItem);
}

// GetParams on a random item (the light-map ambient and the height-map shadow read it; the others ignore it).
void NoteParams(cIStandardLight *s, Kind kind, bool fed, std::mt19937 &rng)
{
    float params[9];
    for (float &p : params) p = -7.0f;
    const uint32_t item = FillItem(rng);
    if ((kind == kAmbientMap || kind == kHeightMap) && !fed) return drawfx::Note("params skipped", 1u);
    s->GetParams(params, item);
    NoteFloats("params", params, 9);
}

// The strengths over a random box (in a random frame) and at a random point.
void NoteStrengths(cILight *n, Kind kind, bool fed, std::mt19937 &rng)
{
    if (kind == kBinary && !fed) return drawfx::Note("strengths skipped", 1u);
    float box[6];
    RandomBox(box, rng);
    static const AffineTransform3 kFrames[] = {
        {0, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 1.0f, -0.25f}},
        {0, {0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
        {1, {2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 2.0f}, {-4.0f, 3.0f, 10.0f}},
    };
    AffineTransform3 toLight = kFrames[rng() % 3];
    static const float kPoints[][3] = {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {-2.0f, -0.75f, 0.5f},
                                       {0.0f, 0.0f, 0.0f}, {3.25f, 1.5f, -1.0f}, {0.0f, 0.0f, -1.5f}};
    float point[3];
    std::memcpy(point, kPoints[rng() % 6], sizeof point);
    if (kind == kHalfspace && rng() % 2) std::memcpy(point, gHalfspacePoint, sizeof point); // on the plane
    NoteFloats("strength", {n->StrengthOverBox(box, &toLight), n->StrengthAt(point)});
}

// A clone of `n` (mode, iid; a context the light does not ask), held when answered. The shade lights' Clones answer
// nothing (ORIGINAL-BUG in nTSSGLights.cpp: the clone leaks); the others' make their bases' lights.
void CloneOf(cILight *n, std::mt19937 &rng)
{
    const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, sgfx::kIidGraph, cIReferentNode::kIid, 0x12345678};
    const uint32_t mode = rng() % 3, iid = iids[rng() % 5];
    sgfx::MakeCloneContext(w.context, nullptr);
    const Kind from = KindOf(n);
    const Kind made = from == kPoint || from == kHalfspace ? kPoint : kPlain;
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    cINodeCloneContext *const context = rng() % 2 ? reinterpret_cast<cINodeCloneContext *>(&w.context) : nullptr;
    if (from == kShade || from == kTerrain) {
        void *leaked = nullptr;
        {
            CloneCatch caught(kKindSize[from]);
            n->Clone(mode, iid, &out, context);
            leaked = CloneCatch::block;
        }
        drawfx::Note("leaked clone", uint8_t(leaked != nullptr));
        if (leaked) NoteObject("leaked clone", 0, static_cast<const uint8_t *>(leaked), from);
    } else {
        n->Clone(mode, iid, &out, context);
    }
    const bool answered = out != reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    drawfx::Note("clone", {mode, iid, uint8_t(answered), answered && out ? CountOf(static_cast<cILight *>(out)) : 0});
    if (answered && out) {
        cILight *clone = static_cast<cILight *>(out);
        if (Hold(clone, made, true, true)) drawfx::Note("clone held", w.count - 1);
    }
}

// A standard light's record for the reader (as sg_lights_standard.cpp's): the leaf's version, cStandardLightBase's, the
// resource's, the level's, the name and the five floats.
void FeedLightRecord(std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    r.versionCount = r.versionAt = r.wordCount = r.wordAt = r.refCount = r.refAt = r.textCount = r.textAt = 0;
    r.Version(1);
    r.Version(rng() % 2 ? 0xb : rng() % 16);
    r.Version(rng() % 3);
    r.Text(rng() % 2 ? "sun_lght" : "");
    r.Version(1);
    r.Version(rng() % 3);
    r.Text(rng() % 2 ? "sun_lght" : "beta");
    r.Version(1);
    r.Version(4);
    const uint32_t extensions = rng() % 2;
    r.Word(extensions);
    if (extensions) r.Ref(&w.ext[1]);
    r.Text(rng() % 2 ? "alpha" : "");
    r.Text(rng() % 2 ? "sun_lght" : "beta");
    const float values[] = {1.5f, 0.75f, 0.2f, 0.4f, 0.6f, 0.0f, -2.0f};
    for (unsigned i = 0; i < 5; ++i) r.Float(values[rng() % 7]);
}
// A shade light's: the level's record on.
void FeedShadeRecord(std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    r.versionCount = r.versionAt = r.wordCount = r.wordAt = r.refCount = r.refAt = r.textCount = r.textAt = 0;
    r.Version(rng() % 4 ? 1 : 2);
    r.Version(rng() % 3);
    r.Text(rng() % 2 ? "sun_lght" : "beta");
    r.Version(1);
    r.Version(4);
    const uint32_t extensions = rng() % 2;
    r.Word(extensions);
    if (extensions) r.Ref(&w.ext[1]);
    r.Text(rng() % 2 ? "alpha" : "");
}

// Runs `call` (on the original side) with the game-built shade light `n` as it is while its level's constructor or
// destructor runs: the level's four vtables in place, and the vtordisp word before the virtual base holding the base's
// displacement from where the level puts it (+0x50), which the level's thunks subtract. Restored afterwards.
template <class Call> void AsLevel(cILight *n, Call &&call)
{
    uint8_t *const object = Object(n);
    uint8_t *const base = reinterpret_cast<uint8_t *>(n->GetResource());
    uint32_t *const words[] = {reinterpret_cast<uint32_t *>(object), reinterpret_cast<uint32_t *>(object + 0x2c),
                               reinterpret_cast<uint32_t *>(object + 0x34), reinterpret_cast<uint32_t *>(base),
                               reinterpret_cast<uint32_t *>(base - 4)};
    const uint32_t level[] = {P(GameAddress<const void>(kLevelVtables[0])), P(GameAddress<const void>(kLevelVtables[1])),
                              P(GameAddress<const void>(kLevelVtables[2])), P(GameAddress<const void>(kLevelVtables[3])),
                              uint32_t(int32_t(base - object) - 0x50)};
    uint32_t saved[5];
    for (unsigned i = 0; i < 5; ++i) {
        saved[i] = *words[i];
        *words[i] = level[i];
    }
    call();
    for (unsigned i = 0; i < 5; ++i) *words[i] = saved[i];
}

cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}

// One random operation on a random light through one of its faces, its results noted.
void Operate(difftest::Context &ctx, bool original, int k, std::mt19937 &rng)
{
    const unsigned index = rng() % w.count;
    cILight *n = w.lights[index];
    const Kind kind = w.kind[index];
    const bool fed = w.fed[index];
    cIStandardLight *s = Standard(kind) ? static_cast<cIStandardLight *>(n) : nullptr;
    cISGResource *face = n->GetResource();
    const unsigned op = rng() % 22;
    drawfx::Note("op", {op, index});
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, cIReferentNode::kIid, sgfx::kIidGraph, 0x5b0aaa74,
                                 0xbb11a53e, 1, 0x3b253dc7, 0xfb859324, cISGResource::kIid, 0x9b11bf70,
                                 cGZPersistResource::kIid, 0x12345678, sgfx::kIidTree};
        const uint32_t iid = iids[rng() % 14];
        const unsigned via = rng() % 4;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(n)
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                                     : static_cast<cIGZUnknown *>(face);
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = rng() % 8 ? on->QueryInterface(iid, &out) : on->QueryInterface(iid, nullptr);
        drawfx::Note("query", {via, iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (out && CountOf(n) != before) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release through any face, the count through the part's
        const unsigned via = rng() % 4;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(n)
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                                     : static_cast<cIGZUnknown *>(face);
        const uint32_t a = on->AddRef();
        const uint32_t c = PartFace(n)->GetRefCount();
        const uint32_t r = on->Release();
        drawfx::Note("counts", {via, a, c, r, face->GetPersistResource()->GetRefCount()});
        break;
    }
    case 2: // the colour, then the strengths
        if (s) {
            float now[3];
            drawfx::Note("colour returns", {s->GetColor(now) == now ? 1u : 0u});
            const float values[] = {0.0f, -0.0f, 1.0f, 0.25f, 0.8f, -0.5f, 3.0f};
            const float rgb[3] = {values[rng() % 7], values[rng() % 7], values[rng() % 7]};
            s->SetColor(rgb);
            s->GetColor(now);
            NoteFloats("colour", now, 3);
        }
        NoteStrengths(n, kind, fed, rng);
        break;
    case 3: // intensity and factor, then the strengths
        if (s) {
            const float values[] = {0.0f, -0.0f, 0.5f, 1.0f, 2.5f, -1.0f};
            if (rng() % 2)
                s->SetIntensity(values[rng() % 6]);
            else
                s->SetFactor(values[rng() % 6]);
            NoteFloats("scalars", {s->GetIntensity(), s->GetFactor()});
        }
        NoteStrengths(n, kind, fed, rng);
        break;
    case 4: // the name (a standard light's), the light's kind
        if (s) {
            const char *names[] = {"alpha", "sun_lght", "", "beta"};
            s->SetLightName(names[rng() % 4]);
            sgfx::NoteText("name", s->GetLightName());
        }
        drawfx::Note("light", {n->GetLightType(), drawfx::Norm(P(n->GetBoundShape())), drawfx::Norm(P(n->GetStandardLight())),
                               drawfx::Norm(P(n->GetStandardLightConst()))});
        break;
    case 5:
    case 21: // the parameters and the constant slots
        if (s) {
            NoteParams(s, kind, fed, rng);
            NoteFloats("constants", {s->GetFalloff(), s->GetRange(), s->GetConeAngle(), s->GetConeFalloffAngle()});
        } else {
            NoteStrengths(n, kind, fed, rng);
        }
        break;
    case 6: // the no-op setters (a standard light's)
        if (s) {
            s->SetFalloff(2.0f);
            s->SetRange(5.0f);
            s->Update();
            s->SetConeAngle(0.5f);
            s->SetConeFalloffAngle(0.75f);
            NoteFloats("after no-ops", {s->GetFalloff(), s->GetRange(), s->GetConeAngle(), s->GetConeFalloffAngle()});
        }
        drawfx::Note("type", n->GetLightType());
        break;
    case 7: { // referrers: added, removed (only one in the list), counted
        Fake &r = w.referrer[rng() % 3];
        auto *b = reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&r);
        bool listed = false;
        const uint32_t *words = reinterpret_cast<const uint32_t *>(Object(n));
        for (auto *p = reinterpret_cast<Fake *const *>(uintptr_t(words[16]));
             p && p != reinterpret_cast<Fake *const *>(uintptr_t(words[17])); ++p)
            listed |= *p == &r;
        if (listed && rng() % 2)
            n->RemoveReferrer(b);
        else
            n->AddReferrer(b);
        drawfx::Note("referrers", n->GetReferrerCount());
        break;
    }
    case 8: // notifications and queries (a standard light's: the shade level's behaviour is not this test's)
        if (s) {
            const uint32_t events[] = {cIStandardLight::kChangedEvent, 0x1234};
            n->NotifyListeners(events[rng() % 2], rng() % 3, rng() % 2 ? NullEventBody() : nullptr);
            drawfx::Note("asked", uint8_t(n->AskListeners(events[rng() % 2], rng() % 3, nullptr)));
            drawfx::Note("queried", uint8_t(n->OnQuery(0x1234, 0, nullptr)));
        }
        break;
    case 9: { // extensions and the tag
        Fake &e = w.ext[rng() % 3];
        auto *ext = reinterpret_cast<cIExtension *>(&e);
        if (n->HasExtension(ext))
            n->RemoveExtension(ext);
        else if (!e.value[sgfx::kExtOwnerWord])
            n->AddExtension(ext);
        const char *texts[] = {"alpha", "sun_lght", "", nullptr};
        n->SetTag(texts[rng() % 4]);
        sgfx::NoteText("tag", n->GetTag());
        drawfx::Note("extensions", {drawfx::Norm(P(n->GetFirstExtension())), uint8_t(n->HasExtension(ext)), n->GetTagHandle()});
        break;
    }
    case 10: { // kinds
        const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, cIReferentNode::kIid, sgfx::kIidGraph,
                                 cISGResource::kIid, 0x3b253dc7, 0x5b0aaa74, 1, 0, sgfx::kIidTree, 0xc9c81b9b};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 11: { // the virtual base: segment, flag, the three assignments, name, key
        switch (rng() % 5) {
        case 0: drawfx::Note("set segment", uint8_t(face->SetSegment(Segment(rng)))); break;
        case 1: face->SetFlag(uint8_t(rng())); break;
        case 2: drawfx::Note("assign", face->AssignAndSubmit(rng() % 2 ? "sun_lght" : "beta", Segment(rng))); break;
        case 3: drawfx::Note("open assign", face->OpenAssignAndSubmit("sun_lght", rng(), Segment(rng))); break;
        default: drawfx::Note("reset", face->ResetAndSubmit(rng(), rng())); break;
        }
        sgfx::NoteText("resource name", face->GetName());
        cGZPersistResourceKey key{};
        face->CopyKeyTo(&key);
        drawfx::Note("resource", {face->GetFlag(), face->HasKey(), drawfx::Norm(P(face->GetSegment())), face->ForwardKey(),
                                  key.type, key.group, key.instance, key.resource});
        break;
    }
    case 12: { // the virtual base's other slots, the resource getters
        sgfx::NoteText("record 18", static_cast<const char *>(face->GetRecord18()));
        sgfx::NoteText("record 19", static_cast<const char *>(face->GetRecord19()));
        cISGResource *other = w.lights[rng() % w.count]->GetResource();
        drawfx::Note("misc", {drawfx::Norm(P(face->GetSubResource(rng() % 2))), drawfx::Norm(P(face->GetPersistResourceConst())),
                              uint32_t(face->CompareIdentity(other)), face->SubmitKeyToService(),
                              drawfx::Norm(P(n->GetResourceConst())), drawfx::Norm(P(n->GetReferentConst()))});
        break;
    }
    case 13: { // the part's key slots
        cIGZPersistResource *part = PartFace(n);
        part->SetGroup(rng());
        part->SetInstance(rng() % 2 ? rng() : 0);
        const cGZPersistResourceKey key = part->GetKey();
        drawfx::Note("key", {key.type, key.group, key.instance, key.resource});
        break;
    }
    case 14: // the owner, through both persist faces
        drawfx::Note("owner info", {drawfx::Norm(PersistFace(n)->GetOwnerInfo()), drawfx::Norm(face->GetOwnerInfo())});
        break;
    case 15: { // a type-info record, through either persist face (on a shade light, the original side may take the
               // abstract level's own virtual-base thunk 0x3c9af6, which no live object's vtable holds; ours its slot)
        const unsigned via = rng() % 3;
        cPersistTypeInfo info{};
        if (via == 2 && !s && original)
            AsLevel(n, [&] { ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(kLevelTypeInfoThunk)(face, &info); });
        else
            info = via == 0 ? PersistFace(n)->GetTypeInfo() : face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 16: { // written, through either persist face (the level's thunk 0x3c9d73 as in 15)
        auto *writer = reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake);
        const unsigned via = rng() % 3;
        if (via == 2 && !s && original)
            AsLevel(n, [&] { ctx.Original<void(__thiscall *)(void *, ifaces::cIGZPersistWriter *)>(kLevelWriteThunk)(face, writer); });
        else if (via == 0)
            PersistFace(n)->Write(writer);
        else
            face->Write(writer);
        break;
    }
    case 17: CloneOf(n, rng); break;
    case 18: Make(ctx, original, RandomKind(rng), rng, rng() % 5 != 0); break;
    case 19: // an event to the light itself (a standard light's)
        if (s) {
            n->OnEvent(rng() % 2 ? 0x5b51e806u : 0x777u, 0, nullptr, 0);
            drawfx::Note("active", n->IsActive());
        }
        break;
    default: { // read into a new light (as the game reads: into a fresh object), through either face
        const Kind made = RandomKind(rng);
        cILight *fresh = Make(ctx, original, made, rng);
        if (!fresh) break;
        if (Standard(made))
            FeedLightRecord(rng);
        else
            FeedShadeRecord(rng);
        sgfx::Reader &r = sgfx::gReader;
        if (rng() % 2)
            PersistFace(fresh)->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        else
            fresh->GetResource()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
        if (Standard(made)) sgfx::NoteText("name", static_cast<cIStandardLight *>(fresh)->GetLightName());
        break;
    }
    }
}

using TileFn = nTSWorld::WorldTile *(__cdecl *)(nTSWorld::WorldTile *, const float *);

} // namespace

DIFF_TEST(SG_nTSSGLights_Slots, 0x3c7d30)
{
    sgfx::RegistryScope registry(kLightRegistry);
    svcfx::AllocatorScope allocator; // the point light's settings key is a service string
    sgrx::Services services;
    sgfx::CheckSides(ctx, "nTSSG lights", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const unsigned count = 1 + rng() % 5;
        for (unsigned i = 0; i < count; ++i) Make(ctx, original, RandomKind(rng), rng, rng() % 5 != 0);
        NoteLights("built");
        for (int k = 0; k < 24; ++k) Operate(ctx, original, k, rng);
        NoteLights("after");
        sgfx::gReader.NoteNames();
        ReleaseAll(ctx, original, rng);
    });
    w.manager.Drop();
}

DIFF_TEST(SG_nTSSGLights_Params, 0x3c8cc0)
{
    sgfx::RegistryScope registry(kLightRegistry);
    svcfx::AllocatorScope allocator;
    sgrx::Services services;
    sgfx::CheckSides(ctx, "nTSSG params", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        for (Kind kind : {kAmbientMap, kHeightMap, kBinary, kHalfspace}) Make(ctx, original, kind, rng);
        NoteLights("built");
        for (int k = 0; k < 32; ++k) {
            const unsigned index = rng() % w.count;
            auto *s = static_cast<cIStandardLight *>(w.lights[index]);
            switch (rng() % 4) {
            case 0: {
                const float rgb[3] = {float(rng() % 5) * 0.25f, 0.5f, float(rng() % 3)};
                s->SetColor(rgb);
                s->SetIntensity(float(rng() % 4) * 0.75f);
                s->SetFactor(float(rng() % 3) - 0.5f);
                break;
            }
            case 1: NoteStrengths(s, w.kind[index], true, rng); break;
            default: NoteParams(s, w.kind[index], true, rng); break;
            }
        }
        NoteLights("after");
        ReleaseAll(ctx, original, rng);
    });
    w.manager.Drop();
}

DIFF_TEST(SG_nTSSGLights_TileUnder, 0x3c9bd0)
{
    sgfx::CheckSides(ctx, "TileUnder", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const TileFn tile = original ? ctx.Original<TileFn>(0x3c9bd0) : &nTSSG::TileUnder;
        static const float kCoords[] = {0.0f, 0.5f, 0.25f, 0.75f, 3.0f, 3.999f, -0.5f, -1.0f, 12.3f, 7.5f, -0.0f,
                                        63.75f, 1e-8f, 2.5f, 8388607.5f};
        static const float kHeights[] = {0.0f, 2.99f, 3.0f, 5.98f, 6.0f, 100.0f, -1.0f, 8.99f, 0.5f};
        alignas(16) static nTSWorld::WorldTile out[2];
        drawfx::AddRange(out, sizeof out, 0x7b060000);
        for (int k = 0; k < 24; ++k) {
            float pos[3] = {kCoords[rng() % 15], kCoords[rng() % 15], kHeights[rng() % 9]};
            if (rng() % 4 == 0) pos[1] = pos[0]; // on the diagonal
            if (rng() % 4 == 0) pos[1] = std::floor(pos[1]) + (1.0f - (pos[0] - std::floor(pos[0]))); // the other one
            std::memset(out, 0xcd, sizeof out);
            nTSWorld::WorldTile *const r = tile(&out[0], pos);
            const uint32_t *words = reinterpret_cast<const uint32_t *>(out);
            drawfx::Note("tile", {drawfx::Norm(P(r)), words[0], words[1], words[2], words[3], words[4]});
        }
    });
}
