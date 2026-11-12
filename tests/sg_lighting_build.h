// The light builders' test world (lane 261109j, scenegraph LG4), shared by the LG4 rows' tests (tests/sg_lighting_build.cpp
// and the rows after it). It sits on tests/sg_lighting_world.h (the 0x548-byte object image gObject, its light sets, the
// four sky samples gSky, the fake terrain and the log gLog) and adds, for one case drawn from a seed (BuildLighting):
//
// - fake COM objects (FakeObject, a pool of 24): slots 1 / 2 (AddRef / Release), 3 (one argument), 24 (an interface
//   query: writes its drawn answer object and returns a drawn bool, only al counting), 25 (a flag query, drawn bool),
//   34 (gives the drawn owner) and 46 (one argument) log; each has a virtual base (vbptr at +4, the vbtable's second
//   entry the base's offset, the first a wrong one) whose slot 2 logs as a base release;
// - the scene manager (+0x28): slot 9 (remove a node) logs, slot 17 gives the fake scene, whose slot 44 fills an
//   iterator (three words: the fake iterator and two FakeObjects or nulls) and logs the filter's vtable; the iterator's
//   slots 4 (at the end?), 5 (the current object), 6 (next) and 2 log;
// - the object's own vtable (slot 13 logs) and the lightmaps (+0x24, slot 11 logs its float);
// - the built lights (+0x21c), each sky sample's lights (+0x6c) and portal records (+0x7c) as vectors over pools, the
//   lerp sets (+0x228, +0x22c, +0x230, each sample's +0x78 and +0x88) from a pool of fake sets (null now and then where
//   the code checks), the portal scale (+0x1d0) and the blend scale (+0x240);
// - the switch bytes 0xffde64 and 0xffde34 drawn (BuildFixture restores them).
//
// BuildFixture stands in for the callees both sides call: the lerp sets' ClearActive / FadeOutUnused / Clear / Evaluate
// (ours and the image's), the sun height map's BuildGround / AddWalls / AddRoofs / AddAwnings / BuildShadow (likewise),
// the stopwatch's Restart (the image's, which both call) and Stop (both), each logging its arguments. EndBuild gives
// the log, the object, the light sets, the sky samples and the pools for the comparison.
#pragma once

#include "code_patch.h"
#include "sg_lighting_world.h"

#include "rz/StopwatchMore.h"
#include "sg/cLerpLightSet.h"
#include "sg/cSunHeightMap.h"

namespace {

using nTSSG::cLerpLightSet;
using nTSSG::cPortalRecord;
using nTSSG::cSkySample;

constexpr uint32_t kRefreshSwitchByte = 0xffde64;
constexpr uint32_t kFadeSwitchByte = 0xffde34;
constexpr uint32_t kObjectsFilterVt = 0xdb608c;

// --- the fake objects ----------------------------------------------------------------------------------------------

struct FakeObject {
    void **vt;            // +0
    const int32_t *vbptr; // +4
    uint32_t id;          // +8
    void **baseVt;        // +0xc the virtual base (vbptr + 8)
    FakeObject *owner;    // slot 34's answer
    FakeObject *found;    // slot 24's answer object
    uint32_t answers;     // bit 0: slot 24's answer, bit 1: slot 25's; the rest junk for the upper bytes
};
constexpr int kObjects = 24;
FakeObject gObjects[kObjects];
const int32_t kVbTable[2] = {-4, 8};

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (const FakeObject &o : gObjects)
        if (p == &o) return o.id;
    return 0xbad00000u | (P(p) & 0xfff);
}

void __fastcall ObjAddRef(FakeObject *self, void *) { gLog.push_back(0x101), gLog.push_back(self->id); }
void __fastcall ObjRelease(FakeObject *self, void *) { gLog.push_back(0x102), gLog.push_back(self->id); }
void __fastcall ObjSlot3(FakeObject *self, void *, uint32_t a) { gLog.push_back(0x103), gLog.push_back(self->id), gLog.push_back(a); }
uint32_t __fastcall ObjQuery(FakeObject *self, void *, uint32_t iid, void **out, uint32_t a)
{
    gLog.push_back(0x124), gLog.push_back(self->id), gLog.push_back(iid), gLog.push_back(Id(*out)), gLog.push_back(a);
    if ((self->answers & 1) || (self->answers & 4)) *out = self->found;
    return (self->answers & ~0xffu) | (self->answers & 1);
}
uint32_t __fastcall ObjFlag(FakeObject *self, void *, uint32_t flag)
{
    gLog.push_back(0x125), gLog.push_back(self->id), gLog.push_back(flag);
    return (self->answers & ~0xffu) | (self->answers >> 1 & 1);
}
FakeObject *__fastcall ObjOwner(FakeObject *self, void *)
{
    gLog.push_back(0x134), gLog.push_back(self->id);
    return self->owner;
}
void __fastcall ObjSlot46(FakeObject *self, void *, void *a) { gLog.push_back(0x146), gLog.push_back(self->id), gLog.push_back(Id(a)); }
void __fastcall BaseRelease(uint8_t *base, void *)
{
    gLog.push_back(0x1b2), gLog.push_back(reinterpret_cast<FakeObject *>(base - 0xc)->id);
}
void *gObjectVt[64];
void *gBaseVt[4];

// --- the scene and its iterator --------------------------------------------------------------------------------------

struct FakeIterator {
    void **vt;
    FakeObject *items[6];
    uint32_t count, pos;
};
FakeIterator gIterator;
FakeObject *gIterField4, *gIterField8;
uint32_t gAtEndJunk;
uint32_t __fastcall IterAtEnd(FakeIterator *self, void *, void *other)
{
    gLog.push_back(0x204), gLog.push_back(self->pos), gLog.push_back(Id(other));
    return (gAtEndJunk & ~0xffu) | (self->pos >= self->count);
}
FakeObject *__fastcall IterCurrent(FakeIterator *self, void *)
{
    gLog.push_back(0x205), gLog.push_back(self->pos);
    return self->items[self->pos];
}
void __fastcall IterNext(FakeIterator *self, void *) { gLog.push_back(0x206), gLog.push_back(self->pos++); }
void __fastcall IterRelease(FakeIterator *self, void *) { gLog.push_back(0x202), gLog.push_back(self->pos); }
void *gIteratorVt[8];

struct FakeScene {
    void **vt;
};
FakeScene gScene;
void __fastcall SceneObjects(FakeScene *, void *, void **it, uint32_t a, const void *const *filter)
{
    gLog.push_back(0x244), gLog.push_back(a), gLog.push_back(*filter == GameAddress<const void>(kObjectsFilterVt));
    gIterator.pos = 0;
    it[0] = &gIterator, it[1] = gIterField4, it[2] = gIterField8;
}
void *gSceneVt[48];

struct FakeSceneManager {
    void **vt;
};
FakeSceneManager gSceneManager;
void __fastcall ManagerRemove(FakeSceneManager *, void *, void *node) { gLog.push_back(0x309), gLog.push_back(Id(node)); }
FakeScene *__fastcall ManagerScene(FakeSceneManager *, void *)
{
    gLog.push_back(0x317);
    return &gScene;
}
void *gSceneManagerVt[20];

// The lighting manager's own slot 13 and the lightmaps' slot 11.
void __fastcall LightObject(void *self, void *, void *object, uint32_t a)
{
    gLog.push_back(0x413), gLog.push_back(self == gObject), gLog.push_back(Id(object)), gLog.push_back(a);
}
void *gManagerVt[20];
struct FakeLightMaps {
    void **vt;
};
FakeLightMaps gLightMaps;
void __fastcall LightMapsEvaluate(FakeLightMaps *, void *, float t) { gLog.push_back(0x511), gLog.push_back(U(t)); }
void *gLightMapsVt[12];

// --- the lerp sets ---------------------------------------------------------------------------------------------------

constexpr int kLerps = 12;
alignas(8) uint8_t gLerps[kLerps][0x20];
uint32_t LerpId(const void *p) { return (P(p) - P(gLerps)) / 0x20 + 1; }
void __fastcall LerpClearActive(cLerpLightSet *self, void *) { gLog.push_back(0x601), gLog.push_back(LerpId(self)); }
void __fastcall LerpFadeOut(cLerpLightSet *self, void *) { gLog.push_back(0x602), gLog.push_back(LerpId(self)); }
void __fastcall LerpClear(cLerpLightSet *self, void *) { gLog.push_back(0x603), gLog.push_back(LerpId(self)); }
void __fastcall LerpEvaluate(cLerpLightSet *self, void *, float t)
{
    gLog.push_back(0x604), gLog.push_back(LerpId(self)), gLog.push_back(U(t));
}

// --- the sun height map and the stopwatch ----------------------------------------------------------------------------

void LogMap(uint32_t tag, const void *map)
{
    gLog.push_back(tag), gLog.push_back(map == gObject + kSunMap);
    gLog.push_back(*reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(map) + 0x4c)); // mBlendScale
}
void LogDir(const float *dir) { gLog.push_back(U(dir[0])), gLog.push_back(U(dir[1])), gLog.push_back(U(dir[2])); }
void __cdecl SunGround(cSunHeightMap *map, void *db, int32_t n) { LogMap(0x701, map), gLog.push_back(db == &gTerrain), gLog.push_back(n); }
void __cdecl SunWalls(cSunHeightMap *map, void *db, int32_t n, const float *dir)
{
    LogMap(0x702, map), gLog.push_back(db == &gTerrain), gLog.push_back(n), LogDir(dir);
}
void __cdecl SunRoofs(cSunHeightMap *map, void *db, int32_t n) { LogMap(0x703, map), gLog.push_back(db == &gTerrain), gLog.push_back(n); }
void __cdecl SunAwnings(cSunHeightMap *map, void *db, int32_t n, const float *dir)
{
    LogMap(0x704, map), gLog.push_back(db == &gTerrain), gLog.push_back(n), LogDir(dir);
}
void __fastcall SunShadow(cSunHeightMap *map, void *, const float *dir) { LogMap(0x705, map), LogDir(dir); }

uint32_t gWatchTag;
void __fastcall WatchRestart(cRZStopwatch *watch, void *)
{
    uint32_t *const w = reinterpret_cast<uint32_t *>(watch);
    if (!w[2]) w[2] = ++gWatchTag;
    gLog.push_back(0x801), gLog.push_back(w[2]), gLog.push_back(w[0]);
}
void __fastcall WatchStop(cRZStopwatch *watch, void *)
{
    const uint32_t *const w = reinterpret_cast<const uint32_t *>(watch);
    gLog.push_back(0x802), gLog.push_back(w[2]), gLog.push_back(w[0]);
}

void FillBuildFakes()
{
    FillFakes();
    gObjectVt[1] = reinterpret_cast<void *>(&ObjAddRef);
    gObjectVt[2] = reinterpret_cast<void *>(&ObjRelease);
    gObjectVt[3] = reinterpret_cast<void *>(&ObjSlot3);
    gObjectVt[24] = reinterpret_cast<void *>(&ObjQuery);
    gObjectVt[25] = reinterpret_cast<void *>(&ObjFlag);
    gObjectVt[34] = reinterpret_cast<void *>(&ObjOwner);
    gObjectVt[46] = reinterpret_cast<void *>(&ObjSlot46);
    gBaseVt[2] = reinterpret_cast<void *>(&BaseRelease);
    gIteratorVt[2] = reinterpret_cast<void *>(&IterRelease);
    gIteratorVt[4] = reinterpret_cast<void *>(&IterAtEnd);
    gIteratorVt[5] = reinterpret_cast<void *>(&IterCurrent);
    gIteratorVt[6] = reinterpret_cast<void *>(&IterNext);
    gSceneVt[44] = reinterpret_cast<void *>(&SceneObjects);
    gSceneManagerVt[9] = reinterpret_cast<void *>(&ManagerRemove);
    gSceneManagerVt[17] = reinterpret_cast<void *>(&ManagerScene);
    gManagerVt[13] = reinterpret_cast<void *>(&LightObject);
    gLightMapsVt[11] = reinterpret_cast<void *>(&LightMapsEvaluate);
    gIterator.vt = gIteratorVt;
    gScene.vt = gSceneVt;
    gSceneManager.vt = gSceneManagerVt;
    gLightMaps.vt = gLightMapsVt;
}

// The stand-ins and the switch bytes for one test.
struct BuildFixture {
    shape::StandIn clearActive{0x3c2570, &cLerpLightSet::ClearActive, &LerpClearActive};
    shape::StandIn fadeOut{0x3c25d0, &cLerpLightSet::FadeOutUnused, &LerpFadeOut};
    shape::StandIn clear{0x3c2cc0, &cLerpLightSet::Clear, &LerpClear};
    shape::StandIn evaluate{0x3c3450, &cLerpLightSet::Evaluate, &LerpEvaluate};
    shape::StandIn ground{0x3bea70, &cSunHeightMap::BuildGround, &SunGround};
    shape::StandIn walls{0x3bef50, &cSunHeightMap::AddWalls, &SunWalls};
    shape::StandIn roofs{0x3be5c0, &cSunHeightMap::AddRoofs, &SunRoofs};
    shape::StandIn awnings{0x3be1c0, &cSunHeightMap::AddAwnings, &SunAwnings};
    shape::StandIn shadow{0x3bfde0, &cSunHeightMap::BuildShadow, &SunShadow};
    shape::CodePatch restart{0x1c95ecu, shape::CodeOf(&WatchRestart)};
    shape::StandIn stop{0x1c9842u, &stopwatch_more::Stop, &WatchStop};
    const uint8_t savedRefresh = *GameAddress<uint8_t>(kRefreshSwitchByte);
    const uint8_t savedFade = *GameAddress<uint8_t>(kFadeSwitchByte);
    BuildFixture() { FillBuildFakes(); }
    ~BuildFixture()
    {
        *GameAddress<uint8_t>(kRefreshSwitchByte) = savedRefresh;
        *GameAddress<uint8_t>(kFadeSwitchByte) = savedFade;
    }
};

// --- the case --------------------------------------------------------------------------------------------------------

constexpr int kBuilt = 8, kSampleLights = 6, kRecords = 5;
void *gBuilt[kBuilt];
void *gSampleLights[4][kSampleLights];
alignas(8) uint8_t gRecords[4][kRecords][sizeof(cPortalRecord)];

void PutVector(uint8_t *at, const void *begin, uint32_t count, uint32_t elementSize, uint32_t capacity)
{
    const uint32_t b = P(begin);
    const uint32_t words[3] = {b, b + count * elementSize, b + capacity * elementSize};
    std::memcpy(at, words, 12);
}

FakeObject *AnyObject(std::mt19937 &gen, unsigned nullOneIn)
{
    if (nullOneIn && gen() % nullOneIn == 0) return nullptr;
    return &gObjects[gen() % kObjects];
}

void *AnyLerp(std::mt19937 &gen, bool mayBeNull) { return mayBeNull && gen() % 4 == 0 ? nullptr : gLerps[gen() % kLerps]; }

float AnyFloat(std::mt19937 &gen, float scale)
{
    const unsigned roll = gen() % 16;
    if (roll == 0) return NaN(gen);
    if (roll == 1) return 0.0f;
    if (roll == 2) return -0.0f;
    return Nice(gen, scale);
}

// Everything the builders read, drawn after Build(seed) (both sides start from the same draw).
void BuildLighting(std::mt19937 &gen)
{
    for (int i = 0; i < kObjects; ++i) {
        FakeObject &o = gObjects[i];
        o.vt = gObjectVt, o.vbptr = kVbTable, o.id = 0x10 + i, o.baseVt = gBaseVt;
        o.owner = AnyObject(gen, 0);
        o.found = AnyObject(gen, 0);
        o.answers = gen();
    }
    Put(0, P(gManagerVt));
    Put(0x24, P(&gLightMaps));
    Put(0x28, P(&gSceneManager));
    // the built lights and the manager's lerp sets
    const uint32_t built = gen() % (kBuilt + 1);
    for (auto &b : gBuilt) b = AnyObject(gen, 6);
    PutVector(gObject + 0x21c, gBuilt, built, 4, kBuilt);
    Put(0x228, P(AnyLerp(gen, false))), Put(0x22c, P(AnyLerp(gen, false))), Put(0x230, P(AnyLerp(gen, false)));
    PutF(0x1d0, AnyFloat(gen, 2.0f));
    PutF(0x240, AnyFloat(gen, 2.0f));
    // the flags RebuildSunHeights reads (0x5c, 0x5d, 0x68, 0x6a, 0x82): zero half the time
    for (uint32_t at : {0x5cu, 0x5du, 0x68u, 0x6au, 0x82u}) gObject[at] = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    // the sky samples (Build drew 0..4 of them and their floats)
    static const int16_t kIndexes[] = {-1, 0, 1, 2, 3, 4, 5, -2, 0x7fff, int16_t(0x8000)};
    for (int s = 0; s < 4; ++s) {
        uint8_t *const sample = reinterpret_cast<uint8_t *>(gSky[s]);
        for (auto &l : gSampleLights[s]) l = AnyObject(gen, 6);
        PutVector(sample + 0x6c, gSampleLights[s], gen() % (kSampleLights + 1), 4, kSampleLights);
        const uint32_t lerp78 = P(AnyLerp(gen, true)), lerp88 = P(AnyLerp(gen, true));
        std::memcpy(sample + 0x78, &lerp78, 4), std::memcpy(sample + 0x88, &lerp88, 4);
        for (auto &record : gRecords[s]) {
            for (uint32_t i = 0; i < sizeof(cPortalRecord); i += 4) {
                const float f = AnyFloat(gen, 4.0f);
                std::memcpy(record + i, &f, 4);
            }
            const uint32_t object = P(AnyObject(gen, 4)), other = P(AnyObject(gen, 3));
            std::memcpy(record, &object, 4), std::memcpy(record + 4, &other, 4);
            const int16_t index = gen() % 4 ? int16_t(gen() % 4) : kIndexes[gen() % std::size(kIndexes)];
            std::memcpy(record + 8, &index, 2);
        }
        PutVector(sample + 0x7c, gRecords[s], gen() % (kRecords + 1), sizeof(cPortalRecord), kRecords);
    }
    // the scene's objects
    gIterator.count = gen() % 7;
    for (auto &item : gIterator.items) item = AnyObject(gen, 0);
    gIterField4 = AnyObject(gen, 2), gIterField8 = AnyObject(gen, 2);
    gAtEndJunk = gen();
    gWatchTag = 0;
    // the switches
    *GameAddress<uint8_t>(kRefreshSwitchByte) = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    *GameAddress<uint8_t>(kFadeSwitchByte) = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
}

Blob EndBuild()
{
    Blob o = End(nullptr, 0);
    o.push_back(0xeeef);
    for (auto &e : gSky) for (float f : e) o.push_back(U(f));
    for (void *b : gBuilt) o.push_back(Id(b));
    for (auto &s : gSampleLights) for (void *l : s) o.push_back(Id(l));
    const auto *r = reinterpret_cast<const uint32_t *>(gRecords);
    for (size_t i = 0; i < sizeof(gRecords) / 4; ++i) o.push_back(r[i]);
    o.push_back(*GameAddress<uint8_t>(kRefreshSwitchByte)), o.push_back(*GameAddress<uint8_t>(kFadeSwitchByte));
    return o;
}

} // namespace
