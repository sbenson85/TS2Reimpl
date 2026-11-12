// Differential test for nTSSG::cLightingManager slot 10, the per-frame update 0x2df430 (lane 261110s, scenegraph LG3e;
// src/sg/cLightingManagerUpdate.cpp). Hook shape: the original runs on the same object image as ours.
//
// The world (one draw for both sides): the manager is 0x548 random bytes with its own vtable a fake (slot 20 Refresh,
// slot 25 an object: one of two targets whose slot 9 takes the scene), the lit object (+0x18, one of two: slots 20, 21),
// the scene manager (+0x28: one of two or null; slot 17 the scene), the file parser (+0x30: one of two or null; slot 10
// IsDirty drawn with junk above the low byte, 7 RunFiles, 9 GetParser per call: one of two base parsers, the second call
// null on some draws) and the lighting path (+0x524: a fake cIGZString, slot 7 Strlen drawn 0 / 1 / 0x100 / 0x80000000,
// slot 5 the text). The base parsers have a virtual base like the game's (primary slot 12 runs the file; the cIGZUnknown
// face through the vbptr counts). The state bytes +0x80 / +0x81 / +0x84 / +0x85 / +0x87 are each 0, 1 or another
// nonzero byte, +0x88 0, 1 or a wide word; the refresh watch's start (+0x4f0, 64 bits: what 0x1c95cf, run on both sides,
// tests) zero or nonzero in either half; mField500 / mField504 and the elapsed time (ElapsedAsFloat 0x1c9368, stood in)
// drawn so the elapsed is below, equal to and past mField500, NaN, infinite, with mField504 equal to mField500 (a 0/0 or
// x/0 fade), above it (reversed bounds) and NaN. Stood in on both sides: Stop 0x1c9842, RebuildAll 0x2d4010, RebuildStep
// 0x2d3a50, EvaluateLerpLights 0x2dfc80, ReadLightingResources 0x2d0ae0 (logs the key vector), cRZFile::PathExists
// 0x1c1242, GetCachedMessageServer 0x9c5a89 (null on some draws; the server's slot 3 logs the message's words),
// RZMemory::Malloc / Free (the message's block, numbered), the scene flag setter 0xaeeea0 (ours: RebuildLightBounds). Cells:
// the resource service 0x10faf6c (slot 3 the manager, whose slot 19 fills the key vector from the allocator service or
// leaves it), the singleton 0x10cc288 (slot 22's slot 20's slot 6), the allocator service 0x10c95d4 (a wrapper logging
// and forwarding), the switches 0xffde88 / 0xffde34 (each 0, 1 or another byte). Every fake logs the state words (+0x80,
// +0x84, +0x88, +0x500, +0x504) and the watch's start and then may change one thing the code reads again (a state byte or
// word, a float, a switch, which file parser / lit object / scene manager is held, the watch's start). On some draws a
// fake throws (an int): the base parser's slot 12, GetParser's first call, the text, the post, the resource listing or
// ReadLightingResources; the update's own catch covers the first three.
//
// Compared: the fakes' log (the pointers by identity), whether the call threw, every word of the manager, the cells and
// switches, the message blocks and the heap counts; at the x87's 53-bit and 24-bit precision.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_common.h"

#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZFile.h"
#include "rz/cRZStopwatch.h"
#include "sg/SGUtilities.h"
#include "sg/cLightingManager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <random>

namespace {

using nTSSG::cLightingKey;
using nTSSG::cLightingKeyList;
using nTSSG::cLightingManager;

constexpr uint32_t kManagerSize = 0x548;
constexpr uint32_t kServiceCache = 0x10faf6c, kSingletonCell = 0x10cc288, kAllocCell = 0x10c95d4;
constexpr uint32_t kSwitch88 = 0xffde88, kFadeSwitch = 0xffde34;

alignas(16) uint8_t gManager[kManagerSize];

namespace updfx {

struct Obj {
    void **vt;
    uint32_t tag;
};
struct BaseParser {
    void **vt;     // +0 the primary face
    int32_t *vb;   // +4 the vbptr
    void **faceVt; // +8 the cIGZUnknown face
};

Obj gFileParser[2], gTarget[2], gLit[2], gSceneMgr[2], gService, gResMgr, gServer, gSingle[3], gPath;
BaseParser gBase[2];
int32_t gVb[2] = {0, 4};
uint32_t gScene; // the scene's address only
void *gOwnVT[40], *gFileParserVT[16], *gTargetVT[16], *gLitVT[24], *gSceneMgrVT[20], *gServiceVT[8], *gResMgrVT[24],
    *gServerVT[8], *gSingleVT[3][24], *gPathVT[10], *gBaseVT[16], *gFaceVT[4];
const char kPathText[] = "C:\\lighting\\lights.txt";

// ---- the draw's script ----
enum Throw { kNoThrow, kThrowRunFile, kThrowGetParser, kThrowText, kThrowPost, kThrowList, kThrowRead, kThrowKinds };
struct Script {
    uint32_t dirty, strlen, exists, keyCount, keyJunk;
    bool keysFail, hasServer, secondParserNull;
    uint32_t parserIx[2], targetIx;
    float elapsed;
    uint32_t disturbMask, disturbSeed;
    float disturbFloat[2];
    int throwAt;
};
Script gS;
uint32_t gCalls, gGetParserCalls;
cLightingKey gKeys[6];

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < 2; ++i) {
        if (p == &gFileParser[i]) return 0x9a00 + i;
        if (p == &gTarget[i]) return 0x7a00 + i;
        if (p == &gLit[i]) return 0x1100 + i;
        if (p == &gSceneMgr[i]) return 0x3100 + i;
        if (p == &gBase[i]) return 0x9b00 + i;
        if (p == &gBase[i].faceVt) return 0x9b08 + i;
    }
    for (int i = 0; i < 3; ++i)
        if (p == &gSingle[i]) return 0x2000 + i;
    if (p == &gScene) return 0x5ce0;
    if (p == &gServer) return 0x5e00;
    if (p == &gResMgr) return 0xb000;
    if (p == &gService) return 0xb100;
    const auto *const at = static_cast<const uint8_t *>(p);
    if (at >= gManager && at < gManager + kManagerSize) return 0x50000 + uint32_t(at - gManager);
    return 0xdead;
}

// The state the code reads around its calls.
void Held()
{
    L(Word<uint32_t>(gManager, 0x80)), L(Word<uint32_t>(gManager, 0x84)), L(Word<uint32_t>(gManager, 0x88));
    L(Word<uint32_t>(gManager, 0x500)), L(Word<uint32_t>(gManager, 0x504));
    L(Word<uint32_t>(gManager, 0x4f0)), L(Word<uint32_t>(gManager, 0x4f4));
    L(Id(Word<void *>(gManager, 0x18))), L(Id(Word<void *>(gManager, 0x28))), L(Id(Word<void *>(gManager, 0x30)));
}

void Swap(uint32_t offset, const Obj *pair)
{
    void *const now = Word<void *>(gManager, offset);
    if (now == &pair[0]) PutWord(gManager, offset, P(&pair[1]));
    else if (now == &pair[1]) PutWord(gManager, offset, P(&pair[0]));
}

// After every fake: when the draw says so, one thing the code reads again changes.
void Disturb()
{
    const uint32_t n = gCalls++;
    if (!(gS.disturbMask >> (n & 31) & 1)) return;
    const uint32_t what = Mix2(gS.disturbSeed, n) % 15;
    L(0xd157), L(what);
    switch (what) {
    case 0: gManager[0x80] ^= 1; break;
    case 1: gManager[0x81] ^= 1; break;
    case 2: gManager[0x84] ^= 1; break;
    case 3: if (Word<uint32_t>(gManager, 0x28)) gManager[0x85] ^= 1; break; // the fade calls the scene manager
    case 4: gManager[0x87] ^= 1; break;
    case 5: PutWord(gManager, 0x88, Word<uint32_t>(gManager, 0x88) ^ 1); break;
    case 6: PutFloat(gManager, 0x500, gS.disturbFloat[0]); break;
    case 7: PutFloat(gManager, 0x504, gS.disturbFloat[1]); break;
    case 8: *GameAddress<uint8_t>(kFadeSwitch) ^= 1; break;
    case 9: *GameAddress<uint8_t>(kSwitch88) ^= 1; break;
    case 10: Swap(0x30, gFileParser); break;
    case 11: Swap(0x18, gLit); break;
    case 12: Swap(0x28, gSceneMgr); break;
    case 13: gManager[0x4f0] ^= 1; break;
    default: gManager[0x4f7] ^= 0x40; break;
    }
}

void Fake(uint32_t tag)
{
    L(tag);
    Held();
    Disturb();
}

// ---- the manager's own slots and the objects it holds ----
void __fastcall OwnRefresh(void *self, void *) { L(Id(self)), Fake(0x2014); }
uint32_t __fastcall OwnValue404(void *self, void *) { return L(Id(self)), Fake(0x2519), P(&gTarget[gS.targetIx]); }
uint32_t __fastcall TargetScene(Obj *self, void *, const void *scene) { return L(Id(self)), L(Id(scene)), Fake(0x7a09), 0x99; }
void __fastcall Lit20(Obj *self, void *) { L(Id(self)), Fake(0x1120); }
void __fastcall Lit21(Obj *self, void *) { L(Id(self)), Fake(0x1121); }
void *__fastcall SceneOf(Obj *self, void *) { return L(Id(self)), Fake(0x3117), &gScene; }
void *__fastcall Single22(Obj *self, void *) { return L(Id(self)), Fake(0x2022), &gSingle[1]; }
void *__fastcall Single20(Obj *self, void *) { return L(Id(self)), Fake(0x2020), &gSingle[2]; }
uint32_t __fastcall Single6(Obj *self, void *) { return L(Id(self)), Fake(0x2006), 0x66; }
void __cdecl StandSceneFlags(const void *scene, uint32_t flags) { L(Id(scene)), L(flags), Fake(0xaeee); }

// ---- the file parser, the base parsers and the path ----
uint32_t __fastcall ParserIsDirty(Obj *self, void *) { return L(Id(self)), Fake(0x9a0a), gS.dirty; }
uint32_t __fastcall ParserRunFiles(Obj *self, void *) { return L(Id(self)), Fake(0x9a07), 0x7700; }
void *__fastcall ParserGetParser(Obj *self, void *)
{
    const uint32_t n = gGetParserCalls++;
    L(Id(self)), L(n), Fake(0x9a09);
    if (n == 0 && gS.throwAt == kThrowGetParser) throw 0x9a09;
    if (n > 0 && gS.secondParserNull) return nullptr;
    return &gBase[gS.parserIx[n & 1]];
}
uint32_t __fastcall BaseRunFile(BaseParser *self, void *, const char *path, uint32_t flags)
{
    L(Id(self)), L(Hash(path)), L(flags), Fake(0x9b0c);
    if (gS.throwAt == kThrowRunFile) throw 0x9b0c;
    return 0x1234;
}
uint32_t __fastcall FaceAddRef(void *self, void *) { return L(Id(self)), Fake(0x9b01), 1; }
uint32_t __fastcall FaceRelease(void *self, void *) { return L(Id(self)), Fake(0x9b02), 1; }
uint32_t __fastcall PathStrlen(void *self, void *) { return L(Id(self)), Fake(0x5207), gS.strlen; }
const char *__fastcall PathText(void *self, void *)
{
    L(Id(self)), Fake(0x5205);
    if (gS.throwAt == kThrowText) throw 0x5205;
    return kPathText;
}
bool __cdecl StandExists(const cIGZString &path) { return L(Id(&path)), Fake(0x1242), (gS.exists & 1) != 0; }

// ---- RZMemory's blocks (the message's, and the allocator service's under the key vector), numbered; the newest block
// at an address names it (a freed block's address comes back on one side and not the other) ----
void *gRzBlocks[8];
uint32_t gRzCount;
uint32_t RzId(const void *p)
{
    for (uint32_t i = gRzCount < std::size(gRzBlocks) ? gRzCount : uint32_t(std::size(gRzBlocks)); i-- > 0;)
        if (gRzBlocks[i] == p) return 0x4200 + i;
    return Id(p);
}
void *__cdecl RzMalloc(uint32_t bytes)
{
    void *const p = ::operator new(bytes ? bytes : 1);
    std::memset(p, 0xa5, bytes);
    if (gRzCount < std::size(gRzBlocks)) gRzBlocks[gRzCount] = p;
    ++gRzCount;
    L(bytes), Fake(0x3b0);
    return p;
}
void __cdecl RzFree(void *p)
{
    L(RzId(p)), Fake(0x3a1);
    ::operator delete(p);
}
void *__cdecl StandServer() { return Fake(0x9c5a), gS.hasServer ? &gServer : nullptr; }
uint32_t __fastcall ServerPost(Obj *self, void *, const uint32_t *message)
{
    L(Id(self)), L(RzId(message));
    if (message) {
        for (int i = 1; i < 9; ++i) L(message[i]);
        L(Id(reinterpret_cast<const void *>(uintptr_t(message[9])))); // mPtr24
        L(message[10]);
    }
    Fake(0x5e03);
    if (gS.throwAt == kThrowPost) throw 0x5e03;
    return 1;
}

// ---- the resource manager, the allocator service and the stood-in members ----
struct Svc {
    void **vt;
};
Svc gAllocObj;
void *gAllocVT[8];
void *gRealSvc, **gRealVT;
void *gBlocks[16];
uint32_t gBlockCount;
uint32_t BlockId(const void *p)
{
    for (uint32_t i = gBlockCount < std::size(gBlocks) ? gBlockCount : uint32_t(std::size(gBlocks)); i-- > 0;)
        if (gBlocks[i] == p) return 0x4300 + i;
    return Id(p);
}
void *__fastcall SvcAllocate(Svc *, void *, uint32_t bytes)
{
    void *const p = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(gRealVT[3])(gRealSvc, bytes);
    if (gBlockCount < std::size(gBlocks)) gBlocks[gBlockCount] = p;
    L(bytes), L(0x4300 + gBlockCount++), Fake(0x4303);
    return p;
}
void __fastcall SvcFree(Svc *, void *, void *p)
{
    L(BlockId(p)), Fake(0x4304);
    reinterpret_cast<void(__thiscall *)(void *, void *)>(gRealVT[4])(gRealSvc, p);
}
void *__fastcall ServiceSlot3(Obj *self, void *) { return L(Id(self)), Fake(0xb103), &gResMgr; }
uint32_t __fastcall MgrKeys(Obj *self, void *, cLightingKeyList *keys, uint32_t type)
{
    L(Id(self)), L(P(keys->mBegin)), L(P(keys->mEnd)), L(P(keys->mCapacity)), L(type), Fake(0xb013);
    if (!gS.keysFail) {
        auto *const mem = static_cast<cLightingKey *>(reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(gAllocVT[3])(&gAllocObj, gS.keyCount * 16 + 16));
        for (uint32_t i = 0; i < gS.keyCount; ++i) mem[i] = gKeys[i];
        keys->mBegin = mem, keys->mEnd = mem + gS.keyCount, keys->mCapacity = mem + gS.keyCount + 1;
    }
    if (gS.throwAt == kThrowList) throw 0xb013;
    return 1 | (gS.keyJunk << 8);
}
void __fastcall StandRead(cLightingManager *self, void *, cLightingKeyList *keys)
{
    L(Id(self)), L(BlockId(keys->mBegin)), L(uint32_t(keys->mEnd - keys->mBegin)), L(uint32_t(keys->mCapacity - keys->mBegin));
    for (cLightingKey *k = keys->mBegin; k && k != keys->mEnd; ++k) L(k->mType), L(k->mWord4), L(k->mWord8), L(k->mWordC);
    Fake(0x2d0a);
    if (gS.throwAt == kThrowRead) throw 0x2d0a;
}
void __fastcall StandRebuildAll(cLightingManager *self, void *, uint32_t lerp) { L(Id(self)), L(lerp), Fake(0x4010); }
void __fastcall StandRebuildStep(cLightingManager *self, void *) { L(Id(self)), Fake(0x3a50); }
void __fastcall StandEvaluate(cLightingManager *self, void *, float t) { L(Id(self)), L(U(t)), Fake(0xfc80); }
void __fastcall StandStop(cRZStopwatch *watch, void *) { L(Id(watch)), Fake(0x9842); }
void __fastcall StandElapsed(const cRZStopwatch *watch, void *, float *out)
{
    L(Id(watch)), L(Id(out) == 0xdead), Fake(0x9368);
    *out = gS.elapsed;
}

void FillTables()
{
    gOwnVT[20] = reinterpret_cast<void *>(&OwnRefresh);
    gOwnVT[25] = reinterpret_cast<void *>(&OwnValue404);
    gTargetVT[9] = reinterpret_cast<void *>(&TargetScene);
    gLitVT[20] = reinterpret_cast<void *>(&Lit20);
    gLitVT[21] = reinterpret_cast<void *>(&Lit21);
    gSceneMgrVT[17] = reinterpret_cast<void *>(&SceneOf);
    gSingleVT[0][22] = reinterpret_cast<void *>(&Single22);
    gSingleVT[1][20] = reinterpret_cast<void *>(&Single20);
    gSingleVT[2][6] = reinterpret_cast<void *>(&Single6);
    gFileParserVT[7] = reinterpret_cast<void *>(&ParserRunFiles);
    gFileParserVT[9] = reinterpret_cast<void *>(&ParserGetParser);
    gFileParserVT[10] = reinterpret_cast<void *>(&ParserIsDirty);
    gBaseVT[12] = reinterpret_cast<void *>(&BaseRunFile);
    gFaceVT[1] = reinterpret_cast<void *>(&FaceAddRef);
    gFaceVT[2] = reinterpret_cast<void *>(&FaceRelease);
    gPathVT[5] = reinterpret_cast<void *>(&PathText);
    gPathVT[7] = reinterpret_cast<void *>(&PathStrlen);
    gServerVT[3] = reinterpret_cast<void *>(&ServerPost);
    gServiceVT[3] = reinterpret_cast<void *>(&ServiceSlot3);
    gResMgrVT[19] = reinterpret_cast<void *>(&MgrKeys);
    for (int i = 0; i < 2; ++i) {
        gFileParser[i].vt = gFileParserVT, gTarget[i].vt = gTargetVT, gLit[i].vt = gLitVT, gSceneMgr[i].vt = gSceneMgrVT;
        gBase[i].vt = gBaseVT, gBase[i].vb = gVb, gBase[i].faceVt = gFaceVT;
    }
    for (int i = 0; i < 3; ++i) gSingle[i].vt = gSingleVT[i];
    gService.vt = gServiceVT, gResMgr.vt = gResMgrVT, gServer.vt = gServerVT;
}

// ---- the draw ----
uint8_t Flag(std::mt19937 &gen)
{
    switch (gen() % 4) {
    case 0:
    case 1: return 0;
    case 2: return 1;
    default: return uint8_t(1 + gen() % 255);
    }
}

float Bound(std::mt19937 &gen)
{
    switch (gen() % 8) {
    case 0: return 0.0f;
    case 1: return float(gen() % 4000);
    case 2: return NaN(gen);
    default: return Wide(gen, 2000.0f);
    }
}

void Draw(uint32_t draw)
{
    std::mt19937 gen(draw * 0x9e3779b9u + 0x2df430u);
    for (uint32_t i = 0; i < kManagerSize; i += 4) PutWord(gManager, i, gen());
    PutWord(gManager, 0x00, P(gOwnVT));
    PutWord(gManager, 0x18, P(&gLit[gen() & 1]));
    PutWord(gManager, 0x28, gen() % 5 == 0 ? 0u : P(&gSceneMgr[gen() & 1]));
    PutWord(gManager, 0x30, gen() % 5 == 0 ? 0u : P(&gFileParser[gen() & 1]));
    PutWord(gManager, 0x524, P(gPathVT));
    for (uint32_t o : {0x80u, 0x81u, 0x82u, 0x83u, 0x84u, 0x85u, 0x86u, 0x87u}) gManager[o] = Flag(gen);
    if (!Word<uint32_t>(gManager, 0x28)) gManager[0x85] = 0; // the fade's end calls the scene manager unchecked
    switch (gen() % 4) {
    case 0:
    case 1: PutWord(gManager, 0x88, 0); break;
    case 2: PutWord(gManager, 0x88, 1); break;
    default: PutWord(gManager, 0x88, gen() % 2 ? 0x100u << (gen() % 24) : uint32_t(gen())); break;
    }
    switch (gen() % 4) { // the refresh watch's start (64 bits)
    case 0: PutWord(gManager, 0x4f0, 0), PutWord(gManager, 0x4f4, 0); break;
    case 1: PutWord(gManager, 0x4f0, 1u << (gen() % 32)), PutWord(gManager, 0x4f4, 0); break;
    case 2: PutWord(gManager, 0x4f0, 0), PutWord(gManager, 0x4f4, 1u << (gen() % 32)); break;
    default: break; // the random words
    }
    const float duration = Bound(gen);
    float last;
    switch (gen() % 6) {
    case 0: last = duration; break;
    case 1: last = 0.0f; break;
    case 2: last = duration + 1.0f + float(gen() % 100); break; // reversed bounds
    case 3: last = NaN(gen); break;
    default: last = Bound(gen); break;
    }
    PutFloat(gManager, 0x500, duration), PutFloat(gManager, 0x504, last);
    switch (gen() % 9) {
    case 0: gS.elapsed = duration; break;
    case 1: gS.elapsed = std::nextafter(duration, -1e30f); break;
    case 2: gS.elapsed = std::nextafter(duration, 1e30f); break;
    case 3: gS.elapsed = NaN(gen); break;
    case 4: gS.elapsed = gen() % 2 ? Fl(0x7f800000u) : Fl(0xff800000u); break;
    case 5: gS.elapsed = last; break;
    case 6: gS.elapsed = duration - float(gen() % 500); break;
    default: gS.elapsed = Wide(gen, 3000.0f); break;
    }
    *GameAddress<uint8_t>(kSwitch88) = Flag(gen);
    *GameAddress<uint8_t>(kFadeSwitch) = Flag(gen);

    gS.dirty = (gen() % 2 ? 1u : 0u) | (gen() % 2 ? uint32_t(gen()) & 0xffffff00u : 0u);
    static const uint32_t kLengths[] = {0, 1, 0x100, 0x80000000u, 22};
    gS.strlen = kLengths[gen() % std::size(kLengths)];
    gS.exists = gen() % 3 != 0;
    gS.keyCount = gen() % 3 == 0 ? 0 : 1 + gen() % 5;
    gS.keyJunk = gen() & 0xffffff;
    gS.keysFail = gen() % 5 == 0;
    gS.hasServer = gen() % 5 != 0;
    gS.secondParserNull = gen() % 4 == 0;
    gS.parserIx[0] = gen() & 1, gS.parserIx[1] = gen() & 1, gS.targetIx = gen() & 1;
    gS.disturbMask = gen() % 3 == 0 ? 0 : uint32_t(gen()) & uint32_t(gen());
    gS.disturbSeed = gen();
    gS.disturbFloat[0] = Bound(gen), gS.disturbFloat[1] = Bound(gen);
    gS.throwAt = gen() % 6 == 0 ? int(1 + gen() % (kThrowKinds - 1)) : kNoThrow;
    for (cLightingKey &k : gKeys) k = {uint32_t(gen()), uint32_t(gen()), uint32_t(gen()), uint32_t(gen())};
    gCalls = gGetParserCalls = gRzCount = gBlockCount = 0;
    std::memset(gBlocks, 0, sizeof(gBlocks));
}

} // namespace updfx

struct Scope {
    svcfx::AllocatorScope allocator;
    SavedGlobals globals;
    shape::StandIn stop{0x1c9842u, &stopwatch_more::Stop, &updfx::StandStop};
    shape::StandIn elapsed{0x1c9368u, &stopwatch_more::ElapsedAsFloat, &updfx::StandElapsed};
    shape::StandIn rebuildAll{0x2d4010u, &cLightingManager::RebuildAll, &updfx::StandRebuildAll};
    shape::StandIn rebuildStep{0x2d3a50u, &cLightingManager::RebuildStep, &updfx::StandRebuildStep};
    shape::StandIn evaluate{0x2dfc80u, &cLightingManager::EvaluateLerpLights, &updfx::StandEvaluate};
    shape::StandIn read{0x2d0ae0u, &cLightingManager::ReadLightingResources, &updfx::StandRead};
    shape::StandIn exists{0x1c1242u, &cRZFile::PathExists, &updfx::StandExists};
    shape::StandIn server{0x9c5a89u, &GetCachedMessageServer, &updfx::StandServer};
    shape::StandIn malloc{0x1be3b0u, &RZMemory::Malloc, &updfx::RzMalloc};
    shape::StandIn free{0x1be3a1u, &RZMemory::Free, &updfx::RzFree};
    shape::StandIn sceneFlags{0xaeeea0u, &nGZSceneGraph::RebuildLightBounds, &updfx::StandSceneFlags};
    Scope()
    {
        using namespace updfx;
        FillTables();
        globals.Save(kServiceCache, 4);
        globals.Save(kSingletonCell, 4);
        globals.Save(kSwitch88, 1);
        globals.Save(kFadeSwitch, 1);
        *GameAddress<void *>(kServiceCache) = &gService;
        *GameAddress<void *>(kSingletonCell) = &gSingle[0];
        gRealSvc = *GameAddress<void *>(kAllocCell);
        gRealVT = *reinterpret_cast<void ***>(gRealSvc);
        std::memcpy(gAllocVT, gRealVT, 6 * sizeof(void *));
        gAllocVT[3] = reinterpret_cast<void *>(&SvcAllocate);
        gAllocVT[4] = reinterpret_cast<void *>(&SvcFree);
        gAllocObj.vt = gAllocVT;
        *GameAddress<void *>(kAllocCell) = &gAllocObj; // AllocatorScope puts the real one back
        gLog.reserve(1 << 14);
    }
};

Blob Snapshot(const SavedGlobals &globals, uint32_t threw, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee), o.push_back(threw);
    for (uint32_t i = 0; i < kManagerSize; i += 4) o.push_back(Word<uint32_t>(gManager, i));
    globals.Append(o);
    o.push_back(updfx::gRzCount), o.push_back(updfx::gBlockCount), o.push_back(updfx::gCalls);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

} // namespace

// 0x2df430: the reload when the file parser is dirty (the path's file run under a catch, the message posted, the scripts in
// the resources read, the scene passed on), the rebuild choice from mDirty / mFlag81 / mField87, the incremental step, and
// the fade while the refresh watch runs.
DIFF_TEST(sg_lighting_update, 0x2df430)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2df430);
    Scope scope;
    SidesAtBothPrecisions(ctx, "update", 6000, true, [&](uint32_t draw, int side) {
        updfx::Draw(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        uint32_t threw = 0;
        difftest::AllocationCounter heap;
        try {
            if (side == 0) original(gManager);
            else reinterpret_cast<cLightingManager *>(gManager)->Update();
        } catch (int what) {
            threw = uint32_t(what);
        }
        heap.Stop();
        return Snapshot(scope.globals, threw, heap.Blocks(), heap.Bytes());
    });
}
