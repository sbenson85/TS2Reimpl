// Differential tests for nTSSG::cLightingManager's light-set selectors (lane 261110q, scenegraph LG3c;
// src/sg/cLightingManagerSelect.cpp): SelectTimeOfDayLightSet 0x2e4480, SelectHoodLightSet 0x2e10a0, SetSkyMode
// 0x2e29f0 and SetSkyboxCubeTexture 0x2dfe10. Hook shape: the original runs on the same manager image as ours.
//
// The world (one draw for both sides): the manager is 0x548 random bytes with mInitialised (+0x14), mUseDefaultName
// (+0x4c) and mMorningEvening (+0x6c) drawn zero or not, and mName (+0xb0, a cRZString over the game's vtable) a real
// std::string (empty, a set's name, in place or on the heap). The game cells are fakes: the clock (0x10cc2ac; its slot
// 15 answers a word whose low half is the time: 0..3, past 3, negative, junk upper half), the singleton (0x10cc288;
// slot 6 the neighbourhood, whose slot 15 is its record: slots 18 the kind and 31 the vacation test, drawn words; slot
// 33 the weather, null or a fake: slots 35 and 17 drawn words), the effects manager (0x10faf84; slot 31 logs), the
// scene-graph system (0x10cc458; null now and then, when the framework 0x10c9378's slot 5 gives it, then released) and
// the material manager (0x10f8fcc; slots 19, 20, 21, 23, 31, 33, its definition's slot 7 object with slots 1, 2, 13).
// The byte 0xffde40 is drawn. Our callees are stand-ins on both sides: FindLightSet 0x2d6b80 (scripted answers, -1
// among them; it logs the string it is given and the manager's mCurrentSet and mName at the call), ApplyLightSet
// 0x2d0730, HasOptionBits 0xb39490 (scripted answers that differ between its two calls, junk above al), and, where the
// function under test calls them, SelectHoodLightSet (it changes mName: kept, the same text, other text, on the heap),
// SetSkyMode and SetSkyboxCubeTexture. The image's operator new / delete (0x824632 / 0x824624) are ours on both sides.
//
// Compared: the result's low byte where there is one, the fakes' log, every word of the manager (mName by content:
// size, capacity, text, the in-place buffer), the cells and the byte, the counts of the counted fakes, and the heap
// blocks and bytes each side allocated.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_common.h"

#include "app/AppInit.h"
#include "rz/cRZString.h"
#include "sg/cLightingManager.h"
#include "sg/cMaterialManager.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <new>
#include <random>
#include <string>

namespace {
namespace selfx {

using nTSSG::cLightingManager;

constexpr uint32_t kManagerSize = 0x548;
constexpr uint32_t kPortalSwitch = 0xffde40;
constexpr uint32_t kClockCell = 0x10cc2ac, kSingletonCell = 0x10cc288, kEffectsCell = 0x10faf84;
constexpr uint32_t kSystemCell = 0x10cc458, kFrameworkCell = 0x10c9378, kMaterialsCell = 0x10f8fcc;

alignas(16) uint8_t gManager[kManagerSize];
cLightingManager *Manager() { return reinterpret_cast<cLightingManager *>(gManager); }
std::string &Name() { return *reinterpret_cast<std::string *>(gManager + 0xb4); }

void Set(uint32_t cell, const void *value) { *GameAddress<const void *>(cell) = value; }

// --- fakes ---------------------------------------------------------------------------------------------------------

struct Fake {
    void **vt;
    uint32_t id;
    int32_t refs;
};

void *gClockVT[16], *gGlobalsVT[34], *gHoodVT[16], *gInfoVT[32], *gWeatherVT[36], *gEffectsVT[32], *gSystemVT[32],
    *gFrameworkVT[6], *gMaterialsVT[34], *gDefinitionVT[8], *gTextureVT[14];
Fake gClock{gClockVT, 0xc10c}, gGlobals{gGlobalsVT, 0x6106}, gHood{gHoodVT, 0x400d}, gInfo{gInfoVT, 0x1f0},
    gWeather{gWeatherVT, 0x3ea7}, gEffects{gEffectsVT, 0xeffe}, gSystem{gSystemVT, 0x5959}, gFramework{gFrameworkVT, 0xf3a},
    gMaterials{gMaterialsVT, 0x3a7}, gDefinition{gDefinitionVT, 0xdef}, gTexture{gTextureVT, 0x7e8};

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (const Fake *f : {&gClock, &gGlobals, &gHood, &gInfo, &gWeather, &gEffects, &gSystem, &gFramework, &gMaterials,
                          &gDefinition, &gTexture})
        if (p == f) return f->id;
    if (p == gManager) return 0x5000;
    return 0xbad0;
}

// The manager's members a fake logs at its call: mSkyMode, the flags +0x80 and +0x83, mName (size and text) and
// mCurrentSet.
void Held()
{
    L(Word<uint32_t>(gManager, 0x48)), L(gManager[0x80]), L(gManager[0x83]);
    L(uint32_t(Name().size())), L(Hash(Name().c_str())), L(Word<uint32_t>(gManager, 0xd0));
}

// The drawn answers.
uint32_t gTime, gKind, gVacation, gOvercast, gSeason, gIndex, gLoaded, gAdded, gHoodAnswer;
bool gWeatherSet;
uint32_t gOptions[2], gOptionCalls;
int32_t gFinds[3];
uint32_t gFindCalls;
uint32_t gHoodChange;

uint32_t __fastcall ClockTime(Fake *self, void *, uint32_t kind)
{
    L(0xc15), L(Id(self)), L(kind);
    return gTime;
}
void *__fastcall GlobalsHood(Fake *self, void *) { return L(0x6106), L(Id(self)), &gHood; }
void *__fastcall GlobalsWeather(Fake *self, void *) { return L(0x6133), L(Id(self)), gWeatherSet ? &gWeather : nullptr; }
void *__fastcall HoodInfo(Fake *self, void *) { return L(0x400f), L(Id(self)), &gInfo; }
uint32_t __fastcall InfoKind(Fake *self, void *) { return L(0x1f18), L(Id(self)), gKind; }
uint32_t __fastcall InfoVacation(Fake *self, void *) { return L(0x1f31), L(Id(self)), gVacation; }
uint32_t __fastcall WeatherOvercast(Fake *self, void *) { return L(0x3e35), L(Id(self)), gOvercast; }
uint32_t __fastcall WeatherSeason(Fake *self, void *) { return L(0x3e17), L(Id(self)), gSeason; }

void __fastcall EffectsSet(Fake *self, void *, uint32_t index, uint32_t value)
{
    L(0xeff31), L(Id(self)), L(index), L(value), L(Word<uint32_t>(gManager, 0x48));
}

void __fastcall SystemRefresh(Fake *self, void *) { L(0x5931), L(Id(self)); }
uint32_t __fastcall SystemRelease(Fake *self, void *) { return L(0x5902), L(Id(self)), uint32_t(--self->refs); }
uint32_t __fastcall FrameworkQuery(Fake *self, void *, uint32_t first, uint32_t second, void **out)
{
    L(0xf305), L(Id(self)), L(first), L(second);
    ++gSystem.refs;
    *out = &gSystem;
    return 1;
}

uint32_t __fastcall MaterialsFind(Fake *self, void *, const char *name) { return L(0x3a19), L(Id(self)), L(Hash(name)), gIndex; }
void __fastcall MaterialsAddRef(Fake *self, void *, int32_t index) { L(0x3a20), L(Id(self)), L(uint32_t(index)); }
void __fastcall MaterialsRelease(Fake *self, void *, int32_t index) { L(0x3a21), L(Id(self)), L(uint32_t(index)); }
uint32_t __fastcall MaterialsAdd(Fake *self, void *, void *object, uint32_t flags)
{
    L(0x3a23), L(Id(self)), L(Id(object)), L(flags), L(uint32_t(gTexture.refs));
    return gAdded;
}
uint32_t __fastcall MaterialsLoaded(Fake *self, void *, const char *name) { return L(0x3a31), L(Id(self)), L(Hash(name)), gLoaded; }
nGZSceneGraph::MaterialInfo *__fastcall MaterialsInfo(Fake *self, void *, nGZSceneGraph::MaterialInfo *out, int32_t index)
{
    L(0x3a33), L(Id(self)), L(uint32_t(index));
    out->nameHash = Mix(uint32_t(index), 0x3a33);
    out->material = &gDefinition;
    return out;
}
void *__fastcall DefinitionObject(Fake *self, void *) { return L(0xde7), L(Id(self)), L(uint32_t(gTexture.refs)), &gTexture; }
uint32_t __fastcall TextureAddRef(Fake *self, void *) { return L(0x7e1), L(Id(self)), uint32_t(++self->refs); }
uint32_t __fastcall TextureRelease(Fake *self, void *) { return L(0x7e2), L(Id(self)), uint32_t(--self->refs); }
void __fastcall TextureSet(Fake *self, void *, const char *name, const char *texture)
{
    L(0x7e13), L(Id(self)), L(Hash(name)), L(Hash(texture)), L(uint32_t(self->refs));
}

void FillVTables()
{
    gClockVT[15] = reinterpret_cast<void *>(&ClockTime);
    gGlobalsVT[6] = reinterpret_cast<void *>(&GlobalsHood);
    gGlobalsVT[33] = reinterpret_cast<void *>(&GlobalsWeather);
    gHoodVT[15] = reinterpret_cast<void *>(&HoodInfo);
    gInfoVT[18] = reinterpret_cast<void *>(&InfoKind);
    gInfoVT[31] = reinterpret_cast<void *>(&InfoVacation);
    gWeatherVT[35] = reinterpret_cast<void *>(&WeatherOvercast);
    gWeatherVT[17] = reinterpret_cast<void *>(&WeatherSeason);
    gEffectsVT[31] = reinterpret_cast<void *>(&EffectsSet);
    gSystemVT[2] = reinterpret_cast<void *>(&SystemRelease);
    gSystemVT[31] = reinterpret_cast<void *>(&SystemRefresh);
    gFrameworkVT[5] = reinterpret_cast<void *>(&FrameworkQuery);
    gMaterialsVT[19] = reinterpret_cast<void *>(&MaterialsFind);
    gMaterialsVT[20] = reinterpret_cast<void *>(&MaterialsAddRef);
    gMaterialsVT[21] = reinterpret_cast<void *>(&MaterialsRelease);
    gMaterialsVT[23] = reinterpret_cast<void *>(&MaterialsAdd);
    gMaterialsVT[31] = reinterpret_cast<void *>(&MaterialsLoaded);
    gMaterialsVT[33] = reinterpret_cast<void *>(&MaterialsInfo);
    gDefinitionVT[7] = reinterpret_cast<void *>(&DefinitionObject);
    gTextureVT[1] = reinterpret_cast<void *>(&TextureAddRef);
    gTextureVT[2] = reinterpret_cast<void *>(&TextureRelease);
    gTextureVT[13] = reinterpret_cast<void *>(&TextureSet);
}

// Our callees' stand-ins (the same on both sides).
int32_t __fastcall FindSet(cLightingManager *self, void *, const cRZString *name)
{
    L(0x2d6b80), L(Id(self)), L(P(name->mVTable)), L(name->mRefCount), L(uint32_t(name->mString.size()));
    L(uint32_t(name->mString.capacity())), L(Hash(name->mString.c_str()));
    Held();
    return gFinds[gFindCalls++ % std::size(gFinds)];
}
void __fastcall Apply(cLightingManager *self, void *) { L(0x2d0730), L(Id(self)), Held(); }
uint32_t __cdecl Option(uint32_t bits)
{
    L(0xb39490), L(bits);
    return gOptions[gOptionCalls++ % 2];
}
const char *const kHeapName = "a light set name long enough for the heap";
uint32_t __fastcall Hood(cLightingManager *self, void *, const char *name)
{
    L(0x2e10a0), L(Id(self)), L(Hash(name)), L(uint32_t(std::strlen(name)));
    Held();
    switch (gHoodChange) {
    case 1: Name().assign(std::string(Name())); break; // the same text
    case 2: Name().assign("night"); break;
    case 3: Name().assign(name); break;
    case 4: Name().assign(kHeapName); break;
    case 5: Name().clear(); break;
    default: break;
    }
    return gHoodAnswer;
}
void __fastcall SkyMode(cLightingManager *self, void *, uint32_t kind) { L(0x2e29f0), L(Id(self)), L(kind), Held(); }
void __cdecl Skybox(const char *material, const char *texture) { L(0x2dfe10), L(Hash(material)), L(Hash(texture)), Held(); }

void *__cdecl GameNew(size_t n) { return ::operator new(n ? n : 1); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// --- the world -----------------------------------------------------------------------------------------------------

// Names: every one the selectors know, near misses (case, a letter more or less, the other spelling), empty, long.
const char *const kNames[] = {"day", "neighbourhoodImposter", "neighbourhood", "overcast", "spring", "autumn", "winter",
                              "night", "demoNight", "neighborhoodnight", "vacationNight", "morning", "evening", "Day",
                              "days", "da", "", "neighborhood", "neighbourhoodnight", "nightNoPortals", "Night",
                              "eveningx", "mornin", "winterNoPortals", "a light set name long enough for the heap"};

uint32_t Pick(std::mt19937 &gen, std::initializer_list<uint32_t> values)
{
    return values.begin()[gen() % values.size()];
}
uint8_t Flag(std::mt19937 &gen, uint32_t clearOdds) { return gen() % clearOdds == 0 ? 0 : uint8_t(1 + gen() % 255); }

void Draw(uint32_t draw, uint32_t salt)
{
    std::mt19937 gen(draw * 2654435761u + salt);
    for (auto &b : gManager) b = uint8_t(gen());
    gManager[0x14] = Flag(gen, 5);
    gManager[0x4c] = Flag(gen, 5);
    gManager[0x6c] = Flag(gen, 2);
    PutWord(gManager, 0xb0, P(cRZString::VTable()));
    new (gManager + 0xb4) std::string(gen() % 6 == 0 ? kHeapName : kNames[gen() % std::size(kNames)]);
    // the clock's answer: the time in the low half (0..3, past 3, negative), junk or zero above it
    const uint32_t low = Pick(gen, {0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0x7fff, 0x8000, 0xffff, 0xfffd, 0x103});
    gTime = low | (gen() % 2 ? gen() << 16 : 0u);
    gKind = Pick(gen, {5, 6, 5, 6, 0, 4, 7, 0x105, 0x106, 0xffffffff, gen()});
    gVacation = Pick(gen, {0, 1, 0x100, 0xff00ff01, 0x80, 0xffffff00});
    gWeatherSet = gen() % 4 != 0;
    gOvercast = Pick(gen, {1, 1, 0, 2, 0x101, 0xffffffff, 0x80000001});
    gSeason = Pick(gen, {0, 1, 2, 3, 0, 2, 3, 4, 0x100, 0x102, 0x103, 0xffffffff});
    for (auto &o : gOptions) o = Pick(gen, {0, 1, 0x80, 0x100, 0xffffff00, 0x12345601, 0, 1});
    gOptionCalls = 0;
    for (auto &f : gFinds) f = int32_t(Pick(gen, {0xffffffff, 0xffffffff, 0, 1, 2, 0x7fffffff, 0xfffffffe, 0x80000000}));
    gFindCalls = 0;
    gHoodChange = gen() % 6;
    gHoodAnswer = gen();
    gIndex = Pick(gen, {0, 3, 0xffffffff, 0x7fffffff, gen()});
    gLoaded = Pick(gen, {0, 1, 0x100, 0xffffff01, 1, 1});
    gAdded = gen();
    gSystem.refs = int32_t(gen() % 3);
    gTexture.refs = int32_t(gen() % 3);
    *GameAddress<uint8_t>(kPortalSwitch) = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    Set(kClockCell, &gClock), Set(kSingletonCell, &gGlobals), Set(kEffectsCell, &gEffects);
    Set(kFrameworkCell, &gFramework), Set(kMaterialsCell, &gMaterials);
    Set(kSystemCell, gen() % 4 ? &gSystem : nullptr);
}

Blob Snapshot(uint32_t result, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee), o.push_back(result);
    for (uint32_t i = 0; i < kManagerSize; i += 4)
        if (i < 0xb4 || i >= 0xcc) o.push_back(Word<uint32_t>(gManager, i));
    const std::string &name = Name();
    o.push_back(uint32_t(name.size())), o.push_back(uint32_t(name.capacity())), o.push_back(Hash(name.c_str()));
    if (name.capacity() < 16) // in place: the whole buffer (the bytes past the text too)
        for (uint32_t k = 0; k < 16; k += 4) o.push_back(Word<uint32_t>(gManager, 0xb4 + k));
    for (uint32_t cell : {kClockCell, kSingletonCell, kEffectsCell, kSystemCell, kFrameworkCell, kMaterialsCell})
        o.push_back(Id(*GameAddress<const void *>(cell)));
    o.push_back(*GameAddress<uint8_t>(kPortalSwitch));
    o.push_back(uint32_t(gSystem.refs)), o.push_back(uint32_t(gTexture.refs));
    o.push_back(gOptionCalls), o.push_back(gFindCalls);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

struct Scope {
    SavedGlobals globals;
    shape::StandIn find{0x2d6b80u, &cLightingManager::FindLightSet, &FindSet};
    shape::StandIn apply{0x2d0730u, &cLightingManager::ApplyLightSet, &Apply};
    shape::StandIn option{0xb39490u, &appinit::HasOptionBits, &Option};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    Scope()
    {
        globals.Save(kPortalSwitch, 1);
        for (uint32_t cell : {kClockCell, kSingletonCell, kEffectsCell, kSystemCell, kFrameworkCell, kMaterialsCell})
            globals.Save(cell, 4);
        FillVTables();
        gLog.reserve(1 << 14);
    }
};

// `call(side)` on the world drawn for `salt`; its result compared with the rest.
template <class Call> void Run(difftest::Context &ctx, const char *what, uint32_t cases, uint32_t salt, Call call)
{
    Scope scope;
    SidesAtBothPrecisions(ctx, what, cases, true, [&](uint32_t draw, int side) {
        Draw(draw, salt);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        const uint32_t result = call(side, draw);
        heap.Stop();
        Blob outcome = Snapshot(result, heap.Blocks(), heap.Bytes());
        Name().~basic_string();
        return outcome;
    });
}

} // namespace selfx
} // namespace

// 0x2e4480: the two flags, every time kind (with and without mMorningEvening, junk above the short), the vacation
// test, the option's two answers, the weather null or set (overcast, every season), FindLightSet's answer for the
// weather name, a vacation world's kinds; SelectHoodLightSet keeping mName, giving it the same text or another.
DIFF_TEST(sg_lighting_select_time, 0x2e4480)
{
    using namespace selfx;
    shape::StandIn hood{0x2e10a0u, &cLightingManager::SelectHoodLightSet, &Hood};
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2e4480);
    Run(ctx, "time of day", 4000, 0x2e4480u, [&](int side, uint32_t) {
        if (side == 0) original(gManager);
        else Manager()->SelectTimeOfDayLightSet();
        return 0u;
    });
}

// 0x2e10a0: every name the selectors know and near misses, the portal switch set and clear, the scene-graph system's
// cell set and null, FindLightSet's answers for the "NoPortals" name and the plain one (-1 among them), the name
// mName's own text. The result's low byte counts.
DIFF_TEST(sg_lighting_select_hood, 0x2e10a0)
{
    using namespace selfx;
    shape::StandIn mode{0x2e29f0u, &cLightingManager::SetSkyMode, &SkyMode};
    shape::StandIn skybox{0x2dfe10u, &cLightingManager::SetSkyboxCubeTexture, &Skybox};
    auto original = ctx.Original<bool(__thiscall *)(void *, const char *)>(0x2e10a0);
    Run(ctx, "hood", 4000, 0x2e10a0u, [&](int side, uint32_t draw) {
        std::mt19937 gen(draw * 40503u + 0x2e10a0u);
        char own[64]; // mName's text, copied before the call (the original assigns mName from `name`)
        std::snprintf(own, sizeof own, "%s", Name().c_str());
        const char *const name = gen() % 8 == 0 ? own : kNames[gen() % std::size(kNames)];
        const bool picked = side == 0 ? original(gManager, name) : Manager()->SelectHoodLightSet(name);
        return uint32_t(picked);
    });
}

// 0x2e29f0: the kinds 0..3 and others (past 3, negative, high words); mSkyMode stored before the effects are set.
DIFF_TEST(sg_lighting_select_sky_mode, 0x2e29f0)
{
    using namespace selfx;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2e29f0);
    Run(ctx, "sky mode", 1000, 0x2e29f0u, [&](int side, uint32_t draw) {
        std::mt19937 gen(draw * 40503u + 0x2e29f0u);
        const uint32_t kind = Pick(gen, {0, 1, 2, 3, 4, 5, 0xffffffff, 0x80000000, 0x100, 0x101, 0x10003, gen()});
        if (side == 0) original(gManager, kind);
        else Manager()->SetSkyMode(kind);
        return 0u;
    });
}

// 0x2dfe10: the material loaded or not (junk above al), the index found (-1 among them), the object counted around the
// texture call and AddMaterial, the handle released.
DIFF_TEST(sg_lighting_select_skybox, 0x2dfe10)
{
    using namespace selfx;
    auto original = ctx.Original<void(__cdecl *)(const char *, const char *)>(0x2dfe10);
    Run(ctx, "skybox", 1000, 0x2dfe10u, [&](int side, uint32_t draw) {
        std::mt19937 gen(draw * 40503u + 0x2dfe10u);
        static const char *const kMaterials[] = {"lotSkirtReflectionSkybox", "", "other"};
        static const char *const kTextures[] = {"skymap-generic-day-highres-envcube", "skymap-generic-night-envcube", ""};
        const char *const material = kMaterials[gen() % std::size(kMaterials)];
        const char *const texture = kTextures[gen() % std::size(kTextures)];
        if (side == 0) original(material, texture);
        else cLightingManager::SetSkyboxCubeTexture(material, texture);
        return 0u;
    });
}
