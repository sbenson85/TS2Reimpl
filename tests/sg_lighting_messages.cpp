// nTSSG::cLightingManager's DoMessage 0x2d44b0 (the +4 message-target face; scenegraph LG3e, lane 261110s) against the
// original. The manager is a raw block both sides run on, entered at +4. Its callees are stood in on both sides
// (SelectTimeOfDayLightSet, SelectHoodLightSet, ReadRoomSettings, ApplyLightSet, ReadLightingResources, CurrentSet, the
// cached getters, the ambient map loader 0xb409f0 since lane 261111n); the state service 0x211da3, still the game's, is
// patched in the image, which both sides call. The key list's block goes through the allocator service (ServiceBlockVector's
// FreeBlock, ours on our side, the image's on the original's) and the type filter through the game's own vtable.
//
// Each draw picks a message id: every id the code handles, the ids one off each, the gap between the fade option and
// the first option row, the row past the last, 0, all ones and random words. Its data (data word 1, HasData2's answer
// with junk above al, GetVoid2's block or null, the message's object or null) is drawn wide; every fake logs the
// members the code reads around its calls and, by the draw's mask, changes one of them (or an option byte) after it.
// The ambient maps vector is a real heap block of drawn size and capacity (the growth past it, the shrink, the growth
// within it); the light sets swapped in are static. Throws from the stood-in callees check the unwinding.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_common.h"

#include "misc/UtilitiesE.h"
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/Singleton_20ddfb.h"
#include "sg/cLightingManager.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <random>

namespace {

using nTSSG::cLightingKey;
using nTSSG::cLightingKeyList;
using nTSSG::cLightingManager;
using nTSSG::cLightSet;

constexpr uint32_t kManagerSize = 0x548;
constexpr uint32_t kAllocCell = 0x10c95d4;
constexpr uint32_t kOptionRows = 0xffde30, kOptionBytes = 0x84; // the fade row and the ten option rows
constexpr uint32_t kSetSize = 0x214;

alignas(16) uint8_t gManager[kManagerSize];

namespace msgfx {

struct Obj {
    void **vt;
    uint32_t tag;
    uint32_t refs;
};

// kRoomSettings' interface: +8 its vbptr; +0x1ac the light sets (a vector's three words); the virtual cIGZUnknown base
// at +0x1c0 (vbtable entry 1) and the base parser face at +0x1d0 (entry 2).
struct Settings {
    void **vt;
    uint32_t pad4;
    const int32_t *vbptr;
    uint8_t fill[0x1ac - 0xc];
    uint32_t sets[3];
    uint8_t fill2[0x1c0 - 0x1b8];
    void **unknownVt;
    uint8_t fill3[0x1d0 - 0x1c4];
    void **parserVt;
    uint8_t fill4[0x200 - 0x1d4];
};
static_assert(offsetof(Settings, sets) == 0x1ac && offsetof(Settings, unknownVt) == 0x1c0 && offsetof(Settings, parserVt) == 0x1d0);
const int32_t kSettingsVb[3] = {-8, 0x1c0 - 8, 0x1d0 - 8};

// The message (cIGZMessage2Standard's slots 4, 5, 9, 13, 15, 32).
struct Message {
    void **vt;
    uint32_t type, data1, has2;
    void *void2;
    void *unknown;
};

Message gMsg;
Obj gLit[2], gSceneMgr[2], gScene, gIters[3], gItems[6], gParser, gMaterialMgr, gServer, gApp, gView, gCamera, gApp8,
    gState, gStateObj, gUnknown, gResources, gMaps[8], gKeys[4];
Settings gSettings;
void *gOwnVT[24], *gMsgVT[40], *gLitVT[8], *gSceneMgrVT[20], *gSceneVT[48], *gIterVT[8], *gItemVT[28], *gParserVT[4],
    *gMaterialMgrVT[28], *gServerVT[8], *gAppVT[24], *gViewVT[8], *gCameraVT[24], *gApp8VT[8], *gStateVT[40],
    *gStateObjVT[12], *gUnknownVT[4], *gResourcesVT[24], *gMapVT[4], *gSettingsVT[4], *gSettingsUnkVT[4];
alignas(8) uint8_t gOwnSets[9 * kSetSize], gSrcSets[9 * kSetSize], gCurrent[kSetSize];
uint8_t gValue[8];
char gLongName[32];

// ---- the draw's script ----
enum Throw { kNoThrow, kThrowRead, kThrowList, kThrowRoom, kThrowHood, kThrowLoad, kThrowStrength, kThrowApply, kThrowKinds };
struct Script {
    uint32_t qiOk, qiWritesOnFail, viewKind, camera20, itemCount, itemFlags, iterNulls, stateNull, parserNull;
    uint32_t keyCount, keysFail, junk, loadNull;
    uint32_t disturbMask, disturbSeed;
    float disturbFloat[2];
    int throwAt;
};
Script gS;
uint32_t gCalls, gIterPos, gLoads;
cLightingKey gKeyRows[6];

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < 2; ++i) {
        if (p == &gLit[i]) return 0x1100 + i;
        if (p == &gSceneMgr[i]) return 0x3100 + i;
    }
    for (int i = 0; i < 3; ++i)
        if (p == &gIters[i]) return 0x7000 + i;
    for (int i = 0; i < 6; ++i)
        if (p == &gItems[i]) return 0x7100 + i;
    for (int i = 0; i < 8; ++i)
        if (p == &gMaps[i]) return 0x8000 + i;
    for (int i = 0; i < 4; ++i)
        if (p == &gKeys[i]) return 0x8100 + i;
    if (p == &gScene) return 0x5ce0;
    if (p == &gMsg) return 0x3500;
    if (p == &gParser) return 0xae89;
    if (p == &gMaterialMgr) return 0xae90;
    if (p == &gServer) return 0x5e00;
    if (p == &gApp) return 0x20dd;
    if (p == &gView) return 0x2100;
    if (p == &gCamera) return 0x2200;
    if (p == &gApp8) return 0x2300;
    if (p == &gState) return 0x211d;
    if (p == &gStateObj) return 0x2120;
    if (p == &gUnknown) return 0x4400;
    if (p == &gResources) return 0x4500;
    if (p == &gSettings) return 0x4600;
    if (p == &gSettings.unknownVt) return 0x46c0;
    if (p == &gSettings.parserVt) return 0x46d0;
    if (p == gCurrent) return 0x5000;
    const auto *const at = static_cast<const uint8_t *>(p);
    if (at >= gManager && at < gManager + kManagerSize) return 0x50000 + uint32_t(at - gManager);
    if (at >= gOwnSets && at < gOwnSets + sizeof gOwnSets) return 0x60000 + uint32_t(at - gOwnSets);
    if (at >= gSrcSets && at < gSrcSets + sizeof gSrcSets) return 0x70000 + uint32_t(at - gSrcSets);
    if (p == GameAddress<void>(0xdae884)) return 0xa884;
    if (p == GameAddress<void>(0xdae894)) return 0xa894;
    return 0xdead;
}

const uint8_t &Opt(uint32_t rva) { return *GameAddress<uint8_t>(rva); }

// The state the code reads around its calls.
void Held()
{
    L(gManager[0x14]), L(Word<uint32_t>(gManager, 0x40)), L(gManager[0x4d]), L(gManager[0x5c]), L(gManager[0x69]);
    L(gManager[0x6a]), L(Word<uint32_t>(gManager, 0x80)), L(gManager[0x87]), L(Word<uint32_t>(gManager, 0xd0));
    L(Word<uint32_t>(gManager, 0x500)), L(Word<uint32_t>(gManager, 0x508));
    L(Id(Word<void *>(gManager, 0x18))), L(Id(Word<void *>(gManager, 0x28)));
    L(Opt(0xffde34)), L(Opt(0xffde4c)), L(Opt(0xffde58)), L(Opt(0xffde7c)), L(Opt(0xffdea0)), L(Opt(0xffdeac));
}

void Swap(uint32_t offset, const Obj *pair)
{
    void *const now = Word<void *>(gManager, offset);
    if (now == &pair[0]) PutWord(gManager, offset, P(&pair[1]));
    else if (now == &pair[1]) PutWord(gManager, offset, P(&pair[0]));
}

char *NameText()
{
    return Word<uint32_t>(gManager, 0xb4 + 0x14) >= 16 ? Word<char *>(gManager, 0xb4) : reinterpret_cast<char *>(gManager + 0xb4);
}

// After every fake: when the draw says so, one thing the code reads again changes.
void Disturb()
{
    const uint32_t n = gCalls++;
    if (!(gS.disturbMask >> (n & 31) & 1)) return;
    const uint32_t what = Mix2(gS.disturbSeed, n) % 22;
    L(0xd157), L(what);
    static const uint32_t kBytes[] = {0x14, 0x4d, 0x5c, 0x80, 0x81, 0x83, 0x87, 0x69, 0x6a};
    static const uint32_t kOptions[] = {0xffde34, 0xffde58, 0xffdea0, 0xffdeac, 0xffde4c, 0xffde7c};
    if (what < 9) gManager[kBytes[what]] ^= 1;
    else if (what < 15) *GameAddress<uint8_t>(kOptions[what - 9]) ^= 1;
    else switch (what) {
        case 15: PutWord(gManager, 0x40, Word<uint32_t>(gManager, 0x40) ^ 1); break;
        case 16: PutFloat(gManager, 0x500, gS.disturbFloat[0]); break;
        case 17: PutFloat(gManager, 0x508, gS.disturbFloat[1]); break;
        case 18: PutWord(gManager, 0xd0, Word<uint32_t>(gManager, 0xd0) ^ 1); break;
        case 19: Swap(0x28, gSceneMgr); break;
        case 20: Swap(0x18, gLit); break;
        default:
            if (Word<uint32_t>(gManager, 0xb4 + 0x10)) NameText()[0] ^= 0x20;
            break;
        }
}

void Fake(uint32_t tag)
{
    L(tag);
    Held();
    Disturb();
}

uint32_t Text(const char *s) { return Id(s) == 0xdead ? Hash(s) : Id(s); }

// ---- the message ----
uint32_t __fastcall MsgType(Message *self, void *) { return L(Id(self)), Fake(0x3504), self->type; }
uint32_t __fastcall MsgSetType(Message *self, void *, uint32_t type)
{
    L(Id(self)), L(type), Fake(0x3505);
    return self->type = type;
}
uint32_t __fastcall MsgData1(Message *self, void *) { return Fake(0x3509), self->data1; }
uint32_t __fastcall MsgHas2(Message *self, void *) { return Fake(0x350d), self->has2; }
void *__fastcall MsgVoid2(Message *self, void *) { return Fake(0x350f), self->void2; }
void *__fastcall MsgUnknown(Message *self, void *) { return L(Id(self->unknown)), Fake(0x3520), self->unknown; }

// ---- the manager's own slots and the objects it holds ----
void __fastcall Own14(cLightingManager *self, void *, void *object, uint32_t a, float strength, uint32_t b)
{
    L(Id(self)), L(Id(object)), L(a), L(U(strength)), L(b), Fake(0x14);
    if (gS.throwAt == kThrowStrength && gIterPos >= 1) throw 0x14;
}
void __fastcall Own19(cLightingManager *self, void *, const char *name) { L(Id(self)), L(Text(name)), Fake(0x19); }
void __fastcall Lit5(Obj *self, void *) { L(Id(self)), Fake(0x1105); }
void __fastcall Lit6(Obj *self, void *) { L(Id(self)), Fake(0x1106); }
void *__fastcall SceneOf(Obj *self, void *) { return L(Id(self)), Fake(0x3117), &gScene; }

void *__fastcall SceneRange(Obj *self, void *, void **out, uint32_t a, const void *const *filter)
{
    L(Id(self)), L(Id(out)), L(a), L(*filter == GameAddress<void>(0xdb608c)), Fake(0x5c44);
    gIterPos = 0;
    out[0] = &gIters[0];
    out[1] = gS.iterNulls & 1 ? nullptr : &gIters[1];
    out[2] = gS.iterNulls & 2 ? nullptr : &gIters[2];
    return out;
}
uint32_t __fastcall IterEqual(Obj *self, void *, const Obj *other)
{
    L(Id(self)), L(Id(other)), L(gIterPos), Fake(0x7004);
    return (gIterPos >= gS.itemCount ? 1u : 0u) | (gS.junk & 0xffffff00u);
}
void *__fastcall IterGet(Obj *self, void *) { return L(Id(self)), L(gIterPos), Fake(0x7005), &gItems[gIterPos % 6]; }
void __fastcall IterNext(Obj *self, void *) { L(Id(self)), Fake(0x7006), ++gIterPos; }
uint32_t __fastcall IterRelease(Obj *self, void *) { return L(Id(self)), Fake(0x7002), 0; }
uint32_t __fastcall Item25(Obj *self, void *, uint32_t flag)
{
    L(Id(self)), L(flag), Fake(0x7125);
    return (gS.itemFlags >> (self - gItems) & 1) | (gS.junk & 0xffffff00u);
}

// ---- the material parser and manager ----
void __fastcall ParserSet(Obj *self, void *, uint32_t a, const char *name, const char *value)
{
    L(Id(self)), L(a), L(Text(name)), L(Text(value)), Fake(0xae80);
}
void __fastcall ParserClear(Obj *self, void *, uint32_t a, const char *name) { L(Id(self)), L(a), L(Text(name)), Fake(0xae81); }
void __fastcall Material24(Obj *self, void *, uint32_t a, uint32_t b) { L(Id(self)), L(a), L(b), Fake(0xae24); }

// ---- the camera toggle ----
void *__fastcall App23(Obj *self, void *) { return L(Id(self)), Fake(0x2023), &gView; }
void *__fastcall App8(Obj *self, void *) { return L(Id(self)), Fake(0x2008), &gApp8; }
void __fastcall App8Slot5(Obj *self, void *) { L(Id(self)), Fake(0x2305); }
uint32_t __fastcall View5(Obj *self, void *) { return L(Id(self)), Fake(0x2105), gS.viewKind; }
void *__fastcall View6(Obj *self, void *) { return L(Id(self)), Fake(0x2106), &gCamera; }
uint32_t __fastcall Camera20(Obj *self, void *) { return L(Id(self)), Fake(0x2220), gS.camera20; }
void __fastcall Camera19(Obj *self, void *, uint32_t on) { L(Id(self)), L(on), Fake(0x2219); }
uint32_t __fastcall ServerSend(Obj *self, void *, Message *message, uint32_t b)
{
    L(Id(self)), L(Id(message)), L(message->type), L(b), Fake(0x5e04);
    return gS.junk;
}

// ---- the state service ----
void *__fastcall State36(Obj *self, void *) { return L(Id(self)), Fake(0x2136), gS.stateNull ? nullptr : &gStateObj; }
void __fastcall StateObj8(Obj *self, void *) { L(Id(self)), Fake(0x2128); }

// ---- the message's object, the resources and the room settings ----
uint32_t __fastcall UnknownQI(Obj *self, void *, uint32_t iid, void **out)
{
    L(Id(self)), L(iid), Fake(0x4400);
    void *const answer = iid == 0x656b8f0c ? static_cast<void *>(&gResources) : iid == 0xab7cc174 ? static_cast<void *>(&gSettings) : nullptr;
    if (gS.qiOk || gS.qiWritesOnFail) *out = answer;
    return (gS.qiOk ? 1u : 0u) | (gS.junk & 0xffffff00u);
}
uint32_t __fastcall ResRelease(Obj *self, void *) { return L(Id(self)), Fake(0x4502), --self->refs; }
uint32_t __fastcall SettingsRelease(void *self, void *) { return L(Id(self)), Fake(0x46c2), --gSettings.pad4; }

// The key list's block: made by the resources' slot 19 fake, freed through RZMemory::Free (0x1be3a1, stood in on both
// sides: the image's FreeBlock 0x195720 and ours reach it).
void *gBlocks[8];
uint32_t gBlockCount;
uint32_t BlockId(const void *p)
{
    for (uint32_t i = gBlockCount < std::size(gBlocks) ? gBlockCount : uint32_t(std::size(gBlocks)); i-- > 0;)
        if (gBlocks[i] == p) return 0x4300 + i;
    return Id(p);
}
void *KeyBlock(uint32_t bytes)
{
    void *const p = ::operator new(bytes);
    std::memset(p, 0xa5, bytes);
    if (gBlockCount < std::size(gBlocks)) gBlocks[gBlockCount] = p;
    L(bytes), L(0x4300 + gBlockCount++), Fake(0x4303);
    return p;
}
// RZMemory's pair (stood in on both sides) and the allocator service's (the cell 0x10c95d4 holds a fake): plain blocks
// of the process heap. The image's FreeBlock 0x195720 frees through the service's slot 4, ours through RZMemory::Free
// (the service's Free is RZMemory::Free); both log the same, and only for the key blocks (the type filter's and the
// maps' blocks go through the game's delete on the original's side and our CRT's on ours; the heap counter sees those).
void *__cdecl RzMalloc(uint32_t bytes) { return ::operator new(bytes ? bytes : 1); }
void __cdecl RzFree(void *p)
{
    if (BlockId(p) >> 8 == 0x43) L(BlockId(p)), Fake(0x3a1);
    ::operator delete(p);
}
struct Svc {
    void **vt;
};
Svc gAllocObj;
void *gAllocVT[8];
void *__fastcall SvcAllocate(Svc *, void *, uint32_t bytes) { return RzMalloc(bytes); }
void __fastcall SvcFree(Svc *, void *, void *p) { RzFree(p); }

uint32_t __fastcall ResKeys(Obj *self, void *, cLightingKeyList *keys, const uint32_t *filter)
{
    L(Id(self)), L(P(keys->mBegin)), L(P(keys->mEnd)), L(P(keys->mCapacity));
    L(filter[0] == P(GameAddress<void>(0xda5cf0))), L(filter[1]), L(filter[2]), Fake(0x4519);
    if (!gS.keysFail) {
        auto *const mem = static_cast<cLightingKey *>(KeyBlock(gS.keyCount * 16 + 16));
        for (uint32_t i = 0; i < gS.keyCount; ++i) mem[i] = gKeyRows[i];
        keys->mBegin = mem, keys->mEnd = mem + gS.keyCount, keys->mCapacity = mem + gS.keyCount + 1;
    }
    if (gS.throwAt == kThrowList) throw 0x4519;
    return gS.junk;
}

// ---- the stood-in members and the image's callees ----
void __fastcall StandRead(cLightingManager *self, void *, cLightingKeyList *keys)
{
    L(Id(self)), L(BlockId(keys->mBegin)), L(uint32_t(keys->mEnd - keys->mBegin)), L(uint32_t(keys->mCapacity - keys->mBegin));
    for (cLightingKey *k = keys->mBegin; k && k != keys->mEnd; ++k) L(k->mType), L(k->mWord4), L(k->mWord8), L(k->mWordC);
    Fake(0x2d0a);
    if (gS.throwAt == kThrowRead) throw 0x2d0a;
}
void __fastcall StandTimeOfDay(cLightingManager *self, void *) { L(Id(self)), Fake(0x2e44); }
uint32_t __fastcall StandHood(cLightingManager *self, void *, const char *name)
{
    L(Id(self)), L(Text(name)), L(Id(name)), Fake(0x2e10);
    if (gS.throwAt == kThrowHood) throw 0x2e10;
    return gS.junk;
}
void __fastcall StandRoom(cLightingManager *self, void *, void *parser)
{
    L(Id(self)), L(Id(parser)), Fake(0x2e37);
    if (gS.throwAt == kThrowRoom) throw 0x2e37;
}
void __fastcall StandApply(cLightingManager *self, void *)
{
    L(Id(self)), Fake(0x2d07);
    if (gS.throwAt == kThrowApply) throw 0x2d07;
}
const void *__fastcall StandCurrent(const cLightingManager *self, void *) { return L(Id(self)), Fake(0x2d37), gCurrent; }
void *__cdecl StandParser() { return Fake(0xae98), gS.parserNull ? nullptr : &gParser; }
void *__cdecl StandMaterialMgr() { return Fake(0xae99), &gMaterialMgr; }
void *__cdecl StandServer() { return Fake(0x9c5a), &gServer; }
void *__cdecl StandSingleton() { return Fake(0x20dd), &gApp; }
void *__cdecl StandState() { return Fake(0x211d), &gState; }

// The maps' slot index (from the manager's current block) for the logs.
uint32_t MapSlot(const void *slot)
{
    const uint8_t *const begin = Word<uint8_t *>(gManager, 0xa4);
    return uint32_t(static_cast<const uint8_t *>(slot) - begin) / 4;
}
uint8_t __cdecl StandLoad(uint32_t type, const void *key, const void **slot)
{
    L(type), L(Id(key)), L(MapSlot(slot)), L(Id(*slot)), Fake(0xb409);
    if (gS.throwAt == kThrowLoad && gLoads == 1) throw 0xb409;
    *slot = gS.loadNull >> (gLoads & 31) & 1 ? nullptr : &gMaps[(gLoads + 5) % 8];
    ++gLoads;
    return *slot ? 1 : 0;
}
// How many of the manager's map slots (+0xa4 begin, +0xa8 end) hold `self` at this moment: a slot is nulled before its
// map is released (the room settings' loop), and a count or release during a resize sees the vector as the original leaves it.
uint32_t SlotsHolding(const Obj *self)
{
    const void *const *const begin = Word<const void *const *>(gManager, 0xa4);
    const void *const *const end = Word<const void *const *>(gManager, 0xa8);
    uint32_t n = 0;
    for (const void *const *at = begin; at < end; ++at) n += *at == self;
    return n;
}
uint32_t __fastcall MapAddRef(Obj *self, void *) { return L(Id(self)), L(SlotsHolding(self)), Fake(0x8001), ++self->refs; }
uint32_t __fastcall MapRelease(Obj *self, void *) { return L(Id(self)), L(SlotsHolding(self)), Fake(0x8002), --self->refs; }

template <class F> void Put(void **vt, int slot, F f) { vt[slot] = reinterpret_cast<void *>(f); }

void FillTables()
{
    Put(gOwnVT, 14, &Own14), Put(gOwnVT, 19, &Own19);
    Put(gMsgVT, 4, &MsgType), Put(gMsgVT, 5, &MsgSetType), Put(gMsgVT, 9, &MsgData1), Put(gMsgVT, 13, &MsgHas2);
    Put(gMsgVT, 15, &MsgVoid2), Put(gMsgVT, 32, &MsgUnknown);
    Put(gLitVT, 5, &Lit5), Put(gLitVT, 6, &Lit6);
    Put(gSceneMgrVT, 17, &SceneOf);
    Put(gSceneVT, 44, &SceneRange);
    Put(gIterVT, 2, &IterRelease), Put(gIterVT, 4, &IterEqual), Put(gIterVT, 5, &IterGet), Put(gIterVT, 6, &IterNext);
    Put(gItemVT, 25, &Item25);
    Put(gParserVT, 0, &ParserSet), Put(gParserVT, 1, &ParserClear);
    Put(gMaterialMgrVT, 24, &Material24);
    Put(gServerVT, 4, &ServerSend);
    Put(gAppVT, 23, &App23), Put(gAppVT, 8, &App8), Put(gApp8VT, 5, &App8Slot5);
    Put(gViewVT, 5, &View5), Put(gViewVT, 6, &View6);
    Put(gCameraVT, 20, &Camera20), Put(gCameraVT, 19, &Camera19);
    Put(gStateVT, 36, &State36), Put(gStateObjVT, 8, &StateObj8);
    Put(gUnknownVT, 0, &UnknownQI);
    Put(gResourcesVT, 2, &ResRelease), Put(gResourcesVT, 19, &ResKeys);
    Put(gMapVT, 1, &MapAddRef), Put(gMapVT, 2, &MapRelease);
    Put(gSettingsUnkVT, 2, &SettingsRelease);
    gMsg.vt = gMsgVT;
    for (int i = 0; i < 2; ++i) gLit[i].vt = gLitVT, gSceneMgr[i].vt = gSceneMgrVT;
    gScene.vt = gSceneVT;
    for (Obj &o : gIters) o.vt = gIterVT;
    for (Obj &o : gItems) o.vt = gItemVT;
    for (Obj &o : gMaps) o.vt = gMapVT;
    gParser.vt = gParserVT, gMaterialMgr.vt = gMaterialMgrVT, gServer.vt = gServerVT, gApp.vt = gAppVT;
    gView.vt = gViewVT, gCamera.vt = gCameraVT, gApp8.vt = gApp8VT, gState.vt = gStateVT, gStateObj.vt = gStateObjVT;
    gUnknown.vt = gUnknownVT, gResources.vt = gResourcesVT;
    gSettings.vt = gSettingsVT, gSettings.vbptr = kSettingsVb, gSettings.unknownVt = gSettingsUnkVT;
    gSettings.parserVt = gSettingsVT;
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

constexpr uint32_t kIds[] = {
    0x8d, 0x8c, 0x8e, 0xb23b7d66, 0xb23b7d65, 0xb23b7d67, 0xd23b7d7c, 0xd23b7d7b, 0xd23b7d7d, 0xfb96149, 0xfb96148,
    0xfb9614a, 0xfb96150, 0xfb9614f, 0xfb96151, 0xfe3cf23, 0xfe3cf22, 0xfe3cf24, 0x2bf18d56, 0x2bf18d55, 0x2bf18d57,
    0x2bf18d58, 0x2bf18d59, 0x2bf18d5a, 0x2bf18d5b, 0x2bf18d5c, 0x2bf18d5d, 0x2bf18d5e, 0x2bf18d5f, 0x2bf18d60,
    0x2bf18d61, 0x2bf18d62, 0x2df90c05, 0x2df90c04, 0x2df90c06, 0x4b3bc675, 0x4b3bc674, 0x4b3bc676, 0xeb425393,
    0xeb425392, 0xeb425394, 0, 0xffffffff, 0x2bf18d58 + 0x80000000u};
constexpr uint32_t kStrengthId = 0x2bf18d60, kRoomId = 0x4b3bc675;

// A vector's three words over `base`'s sets: n of them, cap - n spare.
void PutVector(uint8_t *at, uint8_t *base, uint32_t n, uint32_t spare)
{
    std::memcpy(at, &base, 4);
    const uint32_t end = P(base) + n * kSetSize, cap = end + spare * kSetSize;
    std::memcpy(at + 4, &end, 4), std::memcpy(at + 8, &cap, 4);
}

void Draw(uint32_t draw)
{
    std::mt19937 gen(draw * 0x9e3779b9u + 0x2d44b0u);
    for (uint32_t i = 0; i < kManagerSize; i += 4) PutWord(gManager, i, gen());
    PutWord(gManager, 0x00, P(gOwnVT));
    PutWord(gManager, 0x18, P(&gLit[gen() & 1]));

    // The message.
    const uint32_t pick = gen() % 8;
    gMsg.type = pick == 0 ? uint32_t(gen()) : pick == 1 ? 0x2bf18d58u + gen() % 10 : kIds[gen() % std::size(kIds)];
    switch (gen() % 6) {
    case 0: gMsg.data1 = uint32_t(gen()); break;
    case 1: gMsg.data1 = gen() % 2 ? 0x100u + gen() % 9 : 0xffffff00u | (gen() % 9); break;
    default: gMsg.data1 = gen() % 10; break;
    }
    gMsg.has2 = (gen() % 4 ? 1u : 0u) | (gen() % 3 == 0 ? uint32_t(gen()) & 0xffffff00u : 0u);
    for (uint8_t &b : gValue) b = uint8_t(gen());
    gValue[4] = Flag(gen);
    gMsg.void2 = gen() % 6 == 0 ? nullptr : gValue;
    gMsg.unknown = gen() % 5 == 0 && gMsg.type != kRoomId ? nullptr : &gUnknown; // the room settings: not null-checked

    // The scene manager: SetSceneStrengths does not check it.
    const bool strength = gMsg.type == kStrengthId;
    PutWord(gManager, 0x28, gen() % 5 == 0 && !strength ? 0u : P(&gSceneMgr[gen() & 1]));
    for (uint32_t o : {0x14u, 0x4du, 0x5cu, 0x69u, 0x6au, 0x80u, 0x81u, 0x82u, 0x83u, 0x87u}) gManager[o] = Flag(gen);
    switch (gen() % 4) {
    case 0: PutWord(gManager, 0x40, 1); break;
    case 1: PutWord(gManager, 0x40, 0); break;
    case 2: PutWord(gManager, 0x40, 2); break;
    default: PutWord(gManager, 0x40, gen() % 2 ? 0x101u : uint32_t(gen())); break;
    }
    PutFloat(gManager, 0x500, Wide(gen, 3000.0f)), PutFloat(gManager, 0x508, Wide(gen, 4.0f));
    PutFloat(gCurrent, 0x200, Wide(gen, 4.0f));
    // The world-change delay is a maxss against +0x500 (0x2d4dc0): equal operands keep +0x500's, so zeros of both signs.
    if (gMsg.type == 0xeb425393u && draw % 3 == 0) {
        PutFloat(gManager, 0x500, draw & 4 ? -0.0f : 0.0f);
        PutFloat(gManager, 0x508, draw & 8 ? -0.0f : 0.0f), PutFloat(gCurrent, 0x200, draw & 8 ? -0.0f : 0.0f);
    }
    PutWord(gManager, 0xd0, gen() % 3 ? gen() % 6 : uint32_t(gen()));

    // The name (short in the buffer or long through the pointer).
    static const char *const kNames[] = {"neighbourhood", "neighborhoodnight", "Neighbourhood", "", "neighbourhood2", "neighbour", "neighbourhoodnight"};
    const char *const name = kNames[gen() % std::size(kNames)];
    const uint32_t length = uint32_t(std::strlen(name));
    if (length < 16) {
        std::memset(gManager + 0xb4, 0, 16);
        std::memcpy(gManager + 0xb4, name, length);
        PutWord(gManager, 0xb4 + 0x14, 15);
    } else {
        std::memset(gLongName, 0, sizeof gLongName);
        std::memcpy(gLongName, name, length);
        PutWord(gManager, 0xb4, P(gLongName));
        PutWord(gManager, 0xb4 + 0x14, 31);
    }
    PutWord(gManager, 0xb4 + 0x10, length);

    // The light sets: ours and the source's.
    for (uint32_t i = 0; i < 9; ++i) {
        PutWord(gOwnSets, i * kSetSize + 0xe8, gen() % 3 ? P(&gKeys[gen() % 4]) : 0u);
        PutWord(gSrcSets, i * kSetSize + 0xe8, gen() % 3 ? P(&gKeys[gen() % 4]) : 0u);
    }
    const uint32_t own = gen() % 6, src = gen() % 4 == 0 ? 0 : 1 + gen() % 8;
    PutVector(gManager + 0x98, gOwnSets, own, gen() % 2);
    if (src == 0 && gen() % 2) std::memset(gSettings.sets, 0, 12);
    else PutVector(reinterpret_cast<uint8_t *>(gSettings.sets), gSrcSets, src, gen() % 2);
    gSettings.pad4 = 0x40;

    // The ambient maps: a heap block of drawn size and capacity, or none. A third of the time the capacity is one or
    // two short of the source's set count, so the growth's one and a half times the capacity sometimes wins.
    uint32_t size = gen() % 7, capacity = size + (gen() % 3 == 0 ? 0 : gen() % 4);
    if (src > 2 && gen() % 3 == 0) capacity = src - 1 - gen() % 2, size = gen() % (capacity + 1);
    if (capacity == 0 && gen() % 2) {
        std::memset(gManager + 0xa4, 0, 12);
    } else {
        auto *const block = static_cast<uint32_t *>(::operator new(capacity ? capacity * 4 : 4));
        for (uint32_t i = 0; i < capacity; ++i) block[i] = i < size && gen() % 4 ? P(&gMaps[gen() % 8]) : (i < size ? 0u : 0xcdcdcdcdu);
        PutWord(gManager, 0xa4, P(block)), PutWord(gManager, 0xa8, P(block + size)), PutWord(gManager, 0xac, P(block + capacity));
    }
    for (Obj &m : gMaps) m.refs = 0x10;
    gResources.refs = 0x20;

    gS.qiOk = gen() % 4 != 0;
    gS.qiWritesOnFail = gen() % 3 == 0;
    gS.viewKind = gen() % 3 ? 0xabea28c7u : gen() % 2 ? 0xabea28c6u : uint32_t(gen());
    gS.camera20 = gen() % 3 == 0 ? 0u : gen() % 2 ? 1u : 0x100u << (gen() % 24);
    gS.itemCount = gen() % 6;
    gS.itemFlags = gen();
    gS.iterNulls = gen() % 4;
    gS.stateNull = gen() % 3 == 0;
    gS.parserNull = gen() % 5 == 0;
    gS.keyCount = gen() % 3 == 0 ? 0 : 1 + gen() % 5;
    gS.keysFail = gen() % 5 == 0;
    gS.junk = gen() % 2 ? uint32_t(gen()) & 0xffffff00u : 0u;
    gS.loadNull = gen() % 3 == 0 ? uint32_t(gen()) : 0u;
    gS.disturbMask = gen() % 3 == 0 ? 0 : uint32_t(gen()) & uint32_t(gen());
    gS.disturbSeed = gen();
    gS.disturbFloat[0] = Wide(gen, 3000.0f), gS.disturbFloat[1] = Wide(gen, 3000.0f);
    gS.throwAt = gen() % 6 == 0 ? int(1 + gen() % (kThrowKinds - 1)) : kNoThrow;
    for (cLightingKey &k : gKeyRows) k = {uint32_t(gen()), uint32_t(gen()), uint32_t(gen()), uint32_t(gen())};

    // The option bytes (the fade row and the ten rows).
    *GameAddress<uint8_t>(0xffde34) = Flag(gen);
    for (uint32_t i = 0; i < 10; ++i) *GameAddress<uint8_t>(0xffde40 + 0xc * i) = Flag(gen);
    if (gen() % 4 == 0) gValue[4] = *GameAddress<uint8_t>(gMsg.type == 0x2bf18d56 ? 0xffde34 : 0xffde40 + 0xc * ((gMsg.type - 0x2bf18d58) % 10));
    gCalls = gIterPos = gLoads = gBlockCount = 0;
    std::memset(gBlocks, 0, sizeof(gBlocks));
}

} // namespace msgfx

struct Scope {
    svcfx::AllocatorScope allocator;
    SavedGlobals globals;
    shape::StandIn malloc{0x1be3b0u, &RZMemory::Malloc, &msgfx::RzMalloc};
    shape::StandIn free{0x1be3a1u, &RZMemory::Free, &msgfx::RzFree};
    shape::StandIn timeOfDay{0x2e4480u, &cLightingManager::SelectTimeOfDayLightSet, &msgfx::StandTimeOfDay};
    shape::StandIn hood{0x2e10a0u, &cLightingManager::SelectHoodLightSet, &msgfx::StandHood};
    shape::StandIn room{0x2e3710u, &cLightingManager::ReadRoomSettings, &msgfx::StandRoom};
    shape::StandIn apply{0x2d0730u, &cLightingManager::ApplyLightSet, &msgfx::StandApply};
    shape::StandIn read{0x2d0ae0u, &cLightingManager::ReadLightingResources, &msgfx::StandRead};
    shape::StandIn current{0x2d37c0u, &cLightingManager::CurrentSet, &msgfx::StandCurrent};
    shape::StandIn parser{0xae8980u, &GetMaterialParserCached, &msgfx::StandParser};
    shape::StandIn material{0xae8900u, &GetCachedQueriedObject, &msgfx::StandMaterialMgr};
    shape::StandIn server{0x9c5a89u, &GetCachedMessageServer, &msgfx::StandServer};
    shape::StandIn singleton{0x20ddfbu, &GetCachedSingleton, &msgfx::StandSingleton};
    shape::CodePatch state{0x211da3u, shape::CodeOf(&msgfx::StandState)};
    shape::StandIn load{0xb409f0u, &nTSSG::LoadAmbientMapImage, &msgfx::StandLoad};
    Scope()
    {
        using namespace msgfx;
        FillTables();
        globals.Save(kOptionRows, kOptionBytes);
        void **const realVT = *reinterpret_cast<void ***>(*GameAddress<void *>(kAllocCell));
        std::memcpy(gAllocVT, realVT, 6 * sizeof(void *));
        gAllocVT[3] = reinterpret_cast<void *>(&SvcAllocate);
        gAllocVT[4] = reinterpret_cast<void *>(&SvcFree);
        gAllocObj.vt = gAllocVT;
        *GameAddress<void *>(kAllocCell) = &gAllocObj; // AllocatorScope puts the real one back
        gLog.reserve(1 << 14);
    }
};

// The outcome: the log, the manager (the maps vector as its size, capacity and entries), the light set words, the
// counts, the option rows.
Blob Snapshot(const SavedGlobals &globals, uint32_t result, uint32_t threw, long long blocks, long long bytes)
{
    using namespace msgfx;
    Blob o = gLog;
    o.push_back(0xeeee), o.push_back(result), o.push_back(threw);
    for (uint32_t i = 0; i < kManagerSize; i += 4)
        if (i < 0xa4 || i >= 0xb0) o.push_back(Word<uint32_t>(gManager, i));
    const auto *const begin = Word<const uint32_t *>(gManager, 0xa4);
    const auto *const end = Word<const uint32_t *>(gManager, 0xa8);
    const auto *const cap = Word<const uint32_t *>(gManager, 0xac);
    o.push_back(begin ? 1u : 0u), o.push_back(uint32_t(end - begin)), o.push_back(uint32_t(cap - begin));
    for (const uint32_t *p = begin; p && p != end; ++p) o.push_back(Id(reinterpret_cast<const void *>(*p)));
    for (const Obj &m : gMaps) o.push_back(m.refs);
    o.push_back(gResources.refs), o.push_back(gSettings.pad4), o.push_back(gMsg.type);
    for (uint32_t w : gSettings.sets) o.push_back(Id(reinterpret_cast<const void *>(w)));
    globals.Append(o);
    o.push_back(gBlockCount), o.push_back(gCalls), o.push_back(gLoads);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

} // namespace

// 0x2d44b0: the option messages (the fade byte and the ten rows written from data 2's block; the material switches,
// the scene strengths, the options' common end), the neighbourhood set swap, the sun shadows off, the camera toggle,
// the time of day (with and without the state service's object), the lighting resources, the room settings (the light
// sets swapped in, the ambient maps resized and loaded) and the world change; any other id answers false.
DIFF_TEST(sg_lighting_messages, 0x2d44b0)
{
    auto original = ctx.Original<bool(__thiscall *)(void *, void *)>(0x2d44b0);
    Scope scope;
    SidesAtBothPrecisions(ctx, "messages", 6000, true, [&](uint32_t draw, int side) {
        msgfx::Draw(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        uint32_t threw = 0, result = 0xbad;
        difftest::AllocationCounter heap;
        try {
            if (side == 0) result = original(gManager + 4, &msgfx::gMsg);
            else result = reinterpret_cast<cLightingManager::MessageFace *>(gManager + 4)->DoMessage(&msgfx::gMsg);
        } catch (int what) {
            threw = uint32_t(what);
        }
        heap.Stop();
        Blob outcome = Snapshot(scope.globals, result, threw, heap.Blocks(), heap.Bytes());
        if (void *const block = Word<void *>(gManager, 0xa4)) ::operator delete(block);
        return outcome;
    });
}
