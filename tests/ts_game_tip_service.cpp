// Differential tests for the game tip service and the classes only it reaches (lane KX): cTSGameTipService
// (src/ts/cTSGameTipService.h), cTSGameTip (src/ts/cTSGameTip.h) and cTSGameTipResourceFactory
// (src/ts/cTSGameTipResourceFactory.h), all in the standalone shape.
//
// GameTipService_Run: a service built by the original factory (0xcb5ce0) or constructor (0xcb59c0, on 0xc5 memory)
// against one built by ours (Create, or placement new), each driven through its own vtables by the same random sequence
// of operations, then destroyed by the last Release (the deleting destructor 0xcb5b00 behind the +4 thunk 0xcb5ad0; the
// destructor 0xcb5a60). Operations: Init (0xcb6170), Shutdown (0xcb6de0), QueryInterface (0xcb6320; thunks 0xcb6351 (+4),
// 0xcb6359 (+0x1c)) / AddRef / Release on every face, the base service's accessors, GetTextCount (0xcb5e70), GetTip
// (0xcb5dd0), GetTips (0xcb5e90, with the vector growth 0xcb5680), GetText (0xcb5e40), CreateTip (0xcb5b30), DeleteTip
// (0xcb5c70, through HasTip 0xcb5d50), SetTipTexts (0xcb6490), SaveTip (0xcb63e0) and SetTipGroup (0xcb6dc0); through
// them Load (0xcb66a0), LoadStrings (0xcb6250) and CreateDefaultTips (0xcb67e0). The tips the service makes (CreateTip,
// and the tip resource factory Init registers) are driven too: every cTSGameTip operation of GameTip_Run, and the
// factory's CreateResource (0xcb5bb0), ReadRecord (0xcb6c20) and WriteRecord (0xcb6cf0) on its tips or on fakes.
//
// GameTip_Run: a tip built by the original constructors (0xcb5940 default, 0xcb58a0 with an instance and a kind, on 0xc5
// memory) against ours, driven through its own vtables: QueryInterface (0xcb62d0; thunks 0xcb630d (+0x1c), 0xcb6315
// (+0x20)), AddRef / Release (thunks 0xcb5b20, 0xcb63d4), Write (0xcb6e20), Read (0xcb6370), GetGZCLSID (0xcb5dc0), the
// cITSGameTip slots (GetId 0xcb6080, GetStringId0 0xcb60d0, GetStringId2 0xcb6120, GetString0 / 1 / 2 0xcb6090 / 0xcb6130
// / 0xcb60e0, and the folded getters / setters), the persist key slots, and the service getter 0xb39020 (called directly,
// and by the GetString slots); destroyed by the last Release (the deleting destructor 0xcb5ae0, the destructor 0xcb5a40).
//
// Every object the code reaches is a recording fake (tests/ts_services_fixture.h): the service object (GetServiceObject's
// cell 0x10c9378: the persist system and the tip service by their ids), the persist system (GetResourceManager,
// GetResource, SaveResource, DeleteResource), its resource manager (GetPrivateResource, GetResourceKeyListOfType with key
// counts 0, 1, 2, 3, 5 and instances at the unsigned boundaries, RegisterFactory), the string table (GetStringCount,
// GetString, GetStringInto, SetString, Commit), tips the persist system hands out, the filter, records and their streams.
// Compared after every operation: the result (the low byte where the original returns a bool in al), the outputs, the
// event log (strings by content, every tip the persist system is asked to save by content), every field, the current
// tip's fields, every fake's count, and the stack balance of every slot call. Saves fail at a chosen save (0, 1, 6, 7, 8,
// 16, 17, 25, 26, 27, 41, 42 or none) to end CreateDefaultTips' loops at their boundaries.
//
// Not driven: out-of-memory in CreateTip / CreateDefaultTips / CreateResource (the original calls on the null tip:
// ORIGINAL-BUG), a null string from the table in CreateDefaultTips (used unchecked), the factory's base slots 3-6 and the
// shared slot 8 (0x1f62f0, folded: cGZPersistResourceFactory's).
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h"    // GZServiceAllocator
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "rz/cRZString.h"
#include "ts/cTSCustomContentSupport.h" // ContentString
#include "ts/cTSGameTip.h"
#include "ts/cTSGameTipResourceFactory.h"
#include "ts/cTSGameTipService.h"

#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace gts_test {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Sys = cTSGameTipService;
using Tip = cTSGameTip;

constexpr uint32_t kTag = 0x7e000000;     // the service under test
constexpr uint32_t kTipTag = 0x7e100000;  // the current tip (+ 0x1000 per tip)
constexpr uint32_t kFacTag = 0x7e200000;  // the registered factories (+ 0x1000 each)
constexpr uint32_t kTextTag = 0x7e300000; // the test's strings
constexpr uint32_t kPersistId = 0x056b906e, kPersistIid = 0x9af5e924;
constexpr uint32_t kTipCell = 0x10faf8c;

enum : unsigned {
    kSvcObj = 0,
    kPersist,
    kManager,
    kStrings,
    kFilter,
    kRecord,
    kIStream,
    kOStream,
    kResource,
    kSerial,
    kTipSvc,
    kTip0,
    kTipCount = 4,
};

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }
uint32_t Bool() { return 0x100u | Draw(2); } // a bool in al with a busy upper byte
bool gOurs;
difftest::Context *gCtx;

uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
bool IsFake(uint32_t p)
{
    const uint32_t n = svcfx::Norm(p);
    return n >= 200 && n < 200 + svcfx::kFakes;
}
// A pointer argument, or an out pointer left at the 0x5e fill.
uint32_t NormPtr(uint32_t p) { return svcfx::NormOut(p); }
void LogText(const std::string &text)
{
    Ev(uint32_t(text.size()));
    for (char c : text) Ev(uint8_t(c));
}
cRZString *Str(uint32_t at) { return reinterpret_cast<cRZString *>(uintptr_t(at)); }
void LogString(uint32_t at) { LogText(Str(at)->mString); }
void Call(Obj *self, unsigned slot)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(slot);
}
void LogKey(uint32_t at)
{
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(at));
    for (int i = 0; i < 4; ++i) Ev(words[i]);
}
uint32_t Word(const void *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(m) + off); }
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

// A real tip's state: whether each face carries its side's vtable, the count, the key and the fields.
void DumpTip(Blob &b, const void *tip)
{
    static const uint32_t kFaces[3][2] = {{0, 0xeb7588}, {0x1c, 0xeb75c0}, {0x20, 0xeb75dc}};
    for (const auto &f : kFaces) {
        const bool game = Word(tip, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(gOurs ? !game : game);
    }
    for (uint32_t off = 4; off < 0x1c; off += 4) b.push_back(Word(tip, off));
    for (uint32_t off = 0x24; off < 0x30; off += 4) b.push_back(Word(tip, off));
}
void LogResource(uint32_t p)
{
    if (!p || IsFake(p)) {
        Ev(svcfx::Norm(p));
        return;
    }
    Ev(0x71b);
    Blob b;
    DumpTip(b, reinterpret_cast<const void *>(uintptr_t(p)));
    for (uint32_t w : b) Ev(w);
}

// ---- the world for a seed ----

const char *const kNumbers[] = {"1a", "0x1A", "0X1f", "", "0x", "ffffffff", "123456789", "zz", "10", "0xFFFFFFFFF", "7", "0X0x5", "0x0X7"};
constexpr uint32_t kNumberCount = sizeof(kNumbers) / sizeof(kNumbers[0]);
const char *const kTexts[] = {"", "Tip", "tip", "Hello world", "A tip text longer than fifteen characters"};
constexpr uint32_t kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);
const uint32_t kCounts[] = {0, 1, 2, 3, 5};
const uint32_t kInstances[] = {0, 1, 2, 0x10, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
const uint32_t kGroups[] = {0, 1, 0x7f12a3b4, 0xffffffff};
const uint32_t kFailAt[] = {0, 1, 6, 7, 8, 16, 17, 25, 26, 27, 41, 42, 1000000};
const uint32_t kStringIds[] = {0, 1, 0x5a, 0x7fff, 0xffff8000, 0xffffffff, 0x12d};
const uint32_t kTypes[] = {Tip::kType, 0x53545223, 0, 0xb1827a46};
const uint32_t kIids[] = {1, Sys::kIid, 0x287fb697, 0x0dc7057a, 0x12345678};
const uint32_t kTipIids[] = {1, 0x456b8f1d, Tip::kSerializableIid, Tip::kIid, 0x12345678};

struct World {
    bool persist;      // the service object answers the persist system
    bool manager;      // the persist system hands out its resource manager
    bool strings;      // the manager hands out the string table
    bool resources;    // the persist system hands out tips
    bool keepFactory;  // the manager keeps the factory it is given
    bool tipService;   // the tip service is found (the service under test, or the fake in GameTip_Run)
    bool streams;      // records hand out their streams
    uint32_t group;    // the first listed key's group
    uint32_t failAt;   // the save that fails
    uint32_t shift;    // where the string table's rows start in kNumbers / kTexts
    uint32_t resType;  // the fake resource's type
};
World gWorld;
uint32_t gSaves;

std::unique_ptr<ContentString[]> gNumberStrings, gTextStrings;
ContentString *gOut;

// ---- fake handlers ----

uint32_t ServiceQuery(Obj *, uint32_t *a)
{
    if (a[0] == kPersistId && a[1] == kPersistIid && gWorld.persist) return HandOut(a[2], F(kPersist));
    if (a[0] == Sys::kServiceId && a[1] == Sys::kIid && gWorld.tipService) return HandOut(a[2], F(kTipSvc));
    return 0;
}
uint32_t BoolResult(Obj *, uint32_t *) { return Bool(); }
uint32_t ManagerOf(Obj *, uint32_t *) { return gWorld.manager ? P(&F(kManager)) : 0; }

// The persist system: 5 GetResource, 16 SaveResource, 17 DeleteResource.
uint32_t __fastcall PersistGetResource(Obj *self, void *, uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5)
{
    Call(self, 5);
    LogKey(key);
    Ev(iid);
    Ev(NormPtr(out));
    Ev(a4);
    Ev(a5);
    if (!gWorld.resources || Draw(4) == 0) return 0x100;
    const uint32_t instance = reinterpret_cast<const uint32_t *>(uintptr_t(key))[2];
    HandOut(out, F(kTip0 + instance % kTipCount));
    return 0x101;
}
uint32_t __fastcall PersistSave(Obj *self, void *, uint32_t resource, uint32_t segment)
{
    Call(self, 16);
    LogResource(resource);
    Ev(NormPtr(segment));
    const bool ok = gSaves++ != gWorld.failAt && Draw(16) != 0;
    return (gRng() & 0xff00u) | uint32_t(ok);
}
uint32_t __fastcall PersistDelete(Obj *self, void *, uint32_t key)
{
    Call(self, 17);
    LogKey(key);
    return Bool();
}

// The resource manager: 5 GetPrivateResource, 19 GetResourceKeyListOfType, 20 RegisterFactory.
std::vector<void *> gFactories;
uint32_t __fastcall ManagerPrivate(Obj *self, void *, uint32_t key, uint32_t iid, uint32_t out, uint32_t a4, uint32_t a5)
{
    Call(self, 5);
    LogKey(key);
    Ev(iid);
    Ev(NormPtr(out));
    Ev(a4);
    Ev(a5);
    if (!gWorld.strings || Draw(5) == 0) return 0;
    HandOut(out, F(kStrings));
    return 1;
}
using KeyList = std::vector<cGZPersistResourceKey, GZServiceAllocator<cGZPersistResourceKey>>;
uint32_t __fastcall ManagerKeys(Obj *self, void *, uint32_t list, uint32_t type)
{
    Call(self, 19);
    Ev(type);
    auto *const keys = reinterpret_cast<KeyList *>(uintptr_t(list));
    Ev(uint32_t(keys->size()));
    const uint32_t n = kCounts[Draw(5)];
    const uint32_t extra = Draw(4) == 0 ? 1 : 0; // listed beyond the count returned
    for (uint32_t i = 0; i < n + extra; ++i) {
        const uint32_t group = i == 0 ? gWorld.group : kGroups[Draw(4)];
        keys->push_back({Tip::kType, group, i == n ? 0xffffffffu : kInstances[Draw(8)], Draw(2)});
    }
    return n;
}
uint32_t __fastcall ManagerRegister(Obj *self, void *, uint32_t id, uint32_t type, uint32_t factory);

// The string table: 6 GetString, 7 GetStringInto, 10 SetString.
uint32_t __fastcall StringsGet(Obj *self, void *, uint32_t index, uint32_t language)
{
    Call(self, 6);
    Ev(index);
    Ev(language);
    return P(&gNumberStrings[(index * 7 + gWorld.shift) % kNumberCount].s);
}
uint32_t __fastcall StringsInto(Obj *self, void *, uint32_t out, uint32_t index, uint32_t language)
{
    Call(self, 7);
    Ev(NormPtr(out));
    Ev(index);
    Ev(language);
    if (Draw(4) == 0) return 0x100;
    Str(out)->mString.assign(kTexts[(index + gWorld.shift + Draw(2)) % kTextCount]);
    LogString(out);
    return 0x101;
}
uint32_t __fastcall StringsSet(Obj *self, void *, uint32_t text, uint32_t index, uint32_t language)
{
    Call(self, 10);
    LogString(text);
    Ev(index);
    Ev(language);
    return gRng();
}

// The tip service fake (GameTip_Run): 6 GetText.
uint32_t __fastcall TipSvcText(Obj *self, void *, uint32_t id, uint32_t out)
{
    Call(self, 6);
    Ev(id);
    Ev(NormPtr(out));
    if (Draw(4) == 0) return 0x100;
    Str(out)->mString.assign(kTexts[(id + gWorld.shift) % kTextCount]);
    return 0x101;
}

// The fake tips: 0 QueryInterface (cIGZPersistResource: itself).
uint32_t __fastcall FakeTipQuery(Obj *self, void *, uint32_t riid, uint32_t out)
{
    Call(self, 0);
    Ev(riid);
    Ev(NormPtr(out));
    if (riid != 0x456b8f1d || Draw(5) == 0) return 0x100;
    return HandOut(out, *self) | 0x100;
}

// The record: 0 QueryInterface (its streams); the fake resource: 0 QueryInterface (cIGZSerializable: the serial fake).
uint32_t __fastcall RecordQuery(Obj *self, void *, uint32_t riid, uint32_t out)
{
    Call(self, 0);
    Ev(riid);
    Ev(NormPtr(out));
    if (!gWorld.streams || Draw(5) == 0) return 0x100;
    if (riid == cTSGameTipResourceFactory::kIStreamId) return HandOut(out, F(kIStream)) | 0x100;
    if (riid == cTSGameTipResourceFactory::kOStreamId) return HandOut(out, F(kOStream)) | 0x100;
    return 0x100;
}
uint32_t __fastcall ResourceQuery(Obj *self, void *, uint32_t riid, uint32_t out)
{
    Call(self, 0);
    Ev(riid);
    Ev(NormPtr(out));
    if (riid != Tip::kSerializableIid || Draw(5) == 0) return 0x100;
    return HandOut(out, F(kSerial)) | 0x100;
}

// The streams: 6 Sint16, 7 Uint16, 9 Uint32 (Get into the pointer / Set the value).
template <unsigned Slot> uint32_t __fastcall StreamGet(Obj *self, void *, uint32_t out)
{
    Call(self, Slot);
    Ev(NormPtr(out));
    if (Draw(6) == 0) return 0x100; // nothing written
    static const uint32_t kVersions[] = {0, 1, 2, 3, 0xffff, 0x8000};
    const uint32_t value = Slot == 7 && Draw(2) ? kVersions[Draw(6)] : gRng();
    Ev(value);
    if (Slot == 9) *reinterpret_cast<uint32_t *>(uintptr_t(out)) = value;
    else *reinterpret_cast<uint16_t *>(uintptr_t(out)) = uint16_t(value);
    return Bool();
}
template <unsigned Slot> uint32_t __fastcall StreamSet(Obj *self, void *, uint32_t value)
{
    Call(self, Slot);
    Ev(Slot == 9 ? value : value & 0xffff);
    return Bool();
}

template <class Fn> void Shim(Obj &obj, unsigned slot, unsigned args, Fn fn)
{
    obj.Set(slot, args);
    obj.table[slot] = reinterpret_cast<void *>(fn);
}

void BuildFakes(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 7);
    svcfx::ResetAll();
    gFactories.clear();
    gSaves = 0;
    gWorld.persist = rng() % 7 != 0;
    gWorld.manager = rng() % 6 != 0;
    gWorld.strings = rng() % 5 != 0;
    gWorld.resources = rng() % 4 != 0;
    gWorld.keepFactory = rng() % 4 != 0;
    gWorld.tipService = rng() % 3 != 0;
    gWorld.streams = rng() % 5 != 0;
    gWorld.group = kGroups[rng() % 4];
    gWorld.failAt = kFailAt[rng() % 13];
    gWorld.shift = rng() % 64;
    gWorld.resType = kTypes[rng() % 4];

    svcfx::gFrameworkIndex = kSvcObj;
    F(kSvcObj).Set(5, "ddp", 0, &ServiceQuery);
    Obj &persist = F(kPersist);
    persist.Set(3, 0, 0, &ManagerOf);
    Shim(persist, 5, 5, &PersistGetResource);
    Shim(persist, 16, 2, &PersistSave);
    Shim(persist, 17, 1, &PersistDelete);
    Obj &manager = F(kManager);
    Shim(manager, 5, 5, &ManagerPrivate);
    Shim(manager, 19, 2, &ManagerKeys);
    Shim(manager, 20, 3, &ManagerRegister);
    Obj &strings = F(kStrings);
    strings.Set(5, 1, rng() % 3 == 0 ? 0 : rng() % 400);
    Shim(strings, 6, 2, &StringsGet);
    Shim(strings, 7, 3, &StringsInto);
    Shim(strings, 10, 3, &StringsSet);
    strings.Set(21, 0, 0, &BoolResult);
    F(kFilter).Set(0, "p", 0, &BoolResult);
    Shim(F(kRecord), 0, 2, &RecordQuery);
    Shim(F(kIStream), 6, 1, &StreamGet<6>);
    Shim(F(kIStream), 7, 1, &StreamGet<7>);
    Shim(F(kIStream), 9, 1, &StreamGet<9>);
    Shim(F(kOStream), 6, 1, &StreamSet<6>);
    Shim(F(kOStream), 7, 1, &StreamSet<7>);
    Shim(F(kOStream), 9, 1, &StreamSet<9>);
    Shim(F(kResource), 0, 2, &ResourceQuery);
    F(kResource).Set(5, 0, gWorld.resType);
    F(kSerial).Set(3, "p", 0, &BoolResult);
    F(kSerial).Set(4, "p", 0, &BoolResult);
    Shim(F(kTipSvc), 6, 2, &TipSvcText);
    for (unsigned i = 0; i < kTipCount; ++i) {
        Obj &tip = F(kTip0 + i);
        Shim(tip, 0, 2, &FakeTipQuery);
        for (unsigned s = 5; s <= 7; ++s) tip.Set(s, 0, kStringIds[rng() % 7]);
    }

    *GameAddress<void *>(0x10c9378) = rng() % 9 ? &F(kSvcObj) : nullptr;
    *GameAddress<uint32_t>(svcfx::kPersistCell) = 0;
    *GameAddress<uint32_t>(kTipCell) = 0;
}

// ---- driving ----

// Every slot is called with its arguments pushed by hand and the stack pointer read back: a body that pops a different
// number of bytes than the caller pushed (a wrong declaration) sets gStackSkew, which each step logs.
int32_t gStackSkew;
uint32_t CallChecked(void *fn, void *self, const uint32_t *args, uint32_t count)
{
    uint32_t result, before, after;
    __asm {
        mov before, esp
        mov ecx, count
        mov edx, args
    push_next:
        test ecx, ecx
        jz pushed
        dec ecx
        push dword ptr [edx + ecx * 4]
        jmp push_next
    pushed:
        mov ecx, self
        call fn
        mov result, eax
        mov after, esp
        mov esp, before
    }
    if (after != before) gStackSkew = int32_t(after - before);
    return result;
}
template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    static_assert(((sizeof(A) == 4) && ...), "dword arguments only");
    static_assert(sizeof(R) == 4, "dword result");
    const uint32_t args[sizeof...(A) + 1] = {(uint32_t)(uintptr_t)a..., 0};
    return R(CallChecked((*static_cast<void ***>(face))[slot], face, args, uint32_t(sizeof...(A))));
}

uint32_t __fastcall ManagerRegister(Obj *self, void *, uint32_t id, uint32_t type, uint32_t factory)
{
    Call(self, 20);
    Ev(id);
    Ev(type);
    Ev(NormPtr(factory));
    void *const object = reinterpret_cast<void *>(uintptr_t(factory));
    if (object) {
        Ev(Word(object, 4)); // its count
        const bool game = Word(object, 0) == P(GameAddress<void>(cTSGameTipResourceFactory::kOriginalVTable));
        Ev(gOurs ? !game : game);
        if (gWorld.keepFactory && gFactories.size() < 4) {
            Slot<uint32_t>(object, 1);
            svcfx::AddRange(object, 0x10, kFacTag + uint32_t(gFactories.size()) * 0x1000);
            gFactories.push_back(object);
        }
    }
    return Bool();
}

// The current tip (its +0 face), held by the test with one reference.
void *gTip;
uint32_t gTipNo;
void HoldTip(void *primary)
{
    gTip = primary;
    svcfx::AddRange(primary, Tip::kSize, kTipTag + (gTipNo++ % 0x100) * 0x1000);
}
void DropTip(Blob &b)
{
    if (!gTip) return;
    for (size_t i = 0; i < svcfx::gRanges.size(); ++i) {
        if (svcfx::gRanges[i].lo == reinterpret_cast<uintptr_t>(gTip)) {
            svcfx::gRanges.erase(svcfx::gRanges.begin() + ptrdiff_t(i));
            break;
        }
    }
    b.push_back(Slot<uint32_t>(gTip, 2));
    gTip = nullptr;
}

const uint32_t kValues[] = {0, 1, 0x11, 0x1000, 0xffff, 0x8000, 0x12345678, 0xffffffff};
uint32_t Busy(uint32_t value, uint32_t a) { return (a & 3) == 0 ? value : (value & 0xffff) | (a & 0xffff0000u); }

uint32_t CallGetter()
{
    if (gOurs) return P(GetGameTipService());
    return P(gCtx->Original<void *(__cdecl *)()>(0xb39020)());
}

// One cTSGameTip operation on the tip whose +0 face is `tip`.
void TipOp(Blob &b, void *tip, uint32_t a, uint32_t c)
{
    void *const ser = Face(tip, 0x1c);
    void *const face = Face(tip, 0x20);
    switch (a % 13) {
    case 0: // the getters
        for (unsigned s = 3; s <= 8; ++s) b.push_back(Slot<uint32_t>(face, s));
        break;
    case 1: // GetString0 / 1 / 2 (through the service getter 0xb39020)
        gOut->s.mString.assign("old");
        b.push_back(Slot<uint32_t>(face, 9 + (a >> 4) % 3, gOut->Interface()) & 0xff);
        LogText(gOut->s.mString);
        b.push_back(svcfx::Norm(*GameAddress<uint32_t>(kTipCell)));
        break;
    case 2: Slot<uint32_t>(face, 12, Busy(kValues[c % 8], a)); break; // SetCategory
    case 3: Slot<uint32_t>(face, 13 + (a >> 4) % 3, Busy(kStringIds[c % 7], a)); break; // SetStringId0 / 1 / 2
    case 4: Slot<uint32_t>(face, 16, kValues[c % 8] ^ (a & 0xff00)); break; // SetValue
    case 5: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {tip, ser, face};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 3], 0, kTipIids[(a >> 4) % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 10) % 3], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 6: b.push_back(Slot<uint32_t>(ser, 3, static_cast<void *>(&F(kOStream))) & 0xff); break; // Write
    case 7: b.push_back(Slot<uint32_t>(ser, 4, static_cast<void *>(&F(kIStream))) & 0xff); break; // Read
    case 8: // GetGZCLSID, the key's parts and the count
        b.push_back(Slot<uint32_t>(ser, 5));
        for (unsigned s : {5u, 6u, 7u, 11u}) b.push_back(Slot<uint32_t>(tip, s));
        break;
    case 9: b.push_back(Slot<uint32_t>(tip, 8 + (a >> 4) % 3, kInstances[c % 8]) & 0xff); break; // SetType / Group / Instance
    case 10: b.push_back(svcfx::Norm(CallGetter())); break; // the service getter 0xb39020 directly
    case 11: // Write then Read again (the version Read sees is the stream's)
        b.push_back(Slot<uint32_t>(ser, 3, static_cast<void *>(&F(kOStream))) & 0xff);
        b.push_back(Slot<uint32_t>(ser, 4, static_cast<void *>(&F(kIStream))) & 0xff);
        break;
    default: b.push_back(Slot<uint32_t>(face, 3)); break; // GetId
    }
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

// ---- GameTipService_Run ----

bool gFactory;
void *BuildService(difftest::Context &ctx, Blob &b, bool ours)
{
    if (gFactory) {
        void *object = ours ? static_cast<void *>(Sys::Create()) : ctx.Original<void *(__cdecl *)()>(0xcb5ce0)();
        b.push_back(object ? 1 : 0);
        return object;
    }
    void *memory = ::operator new(Sys::kSize);
    std::memset(memory, 0xc5, Sys::kSize);
    void *r = ours ? static_cast<void *>(::new (memory) Sys) : ctx.Original<void *(__thiscall *)(void *)>(0xcb59c0)(memory);
    b.push_back(r == memory);
    return memory;
}

void DumpService(Blob &b, void *object)
{
    const auto *m = static_cast<const uint8_t *>(object);
    static const uint32_t kFaces[3][2] = {{0, Sys::kVTable}, {4, Sys::kVTable4}, {0x1c, Sys::kTipServiceVTable}};
    for (const auto &f : kFaces) {
        const bool game = Word(m, f[0]) == P(GameAddress<void>(f[1]));
        b.push_back(gOurs ? !game : game);
    }
    // +0x19..+0x1b (after the running flag) are never written: the 0xc5 fill, or heap leftovers from the factory.
    for (uint32_t off = 8; off < 0x1c; off += 4) b.push_back(off == 0x18 && gFactory ? m[0x18] : Word(m, off));
    b.push_back(svcfx::Norm(Word(m, 0x20)));
    b.push_back(svcfx::Norm(Word(m, 0x24)));
    b.push_back(Word(m, 0x28));
    b.push_back(Word(m, 0x2c));
    if (gTip) DumpTip(b, gTip);
}

std::vector<Op> MakeServiceOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 3);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 24;
    if (rng() % 5) ops.push_back({0, uint32_t(rng()), uint32_t(rng())}); // usually Init first
    for (unsigned i = 0; i < n; ++i) ops.push_back({unsigned(rng() % 19), uint32_t(rng()), uint32_t(rng())});
    return ops;
}

void *TextArg(uint32_t a) { return &gTextStrings[a % kTextCount].s; }

void ServiceOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const face = Face(object, 0x1c);
    const uint32_t a = op.arg, c = op.arg2;
    switch (op.kind) {
    case 0: b.push_back(Slot<uint32_t>(primary, 8) & 0xff); break; // Init
    case 1: b.push_back(Slot<uint32_t>(primary, 9) & 0xff); break; // Shutdown
    case 2: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, Face(object, 4), face};
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(faces[a % 3], 0, kIids[(a >> 4) % 5], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 3], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 10) % 3], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 3: // the base service's accessors, GetTextCount
        b.push_back(Slot<uint32_t>(primary, 3));
        b.push_back(Slot<uint32_t>(primary, 5));
        b.push_back(Slot<uint32_t>(primary, 6) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 12));
        Slot<uint32_t>(primary, 7, a % 2);
        b.push_back(Slot<uint32_t>(primary, 10, c) & 0xff);
        b.push_back(Slot<uint32_t>(primary, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(Face(object, 4), 5));
        b.push_back(Slot<uint32_t>(face, 3));
        break;
    case 4: { // GetTip
        void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        b.push_back(Slot<uint32_t>(face, 4, kInstances[a % 8], &out) & 0xff);
        b.push_back(svcfx::Norm(P(out)));
        if (out) Slot<uint32_t>(out, 2);
        break;
    }
    case 5: { // GetTips into a list holding 0-3 tips
        GameTipList list;
        const uint32_t n = a % 4;
        if ((a >> 2) % 2) list.reserve(n + (a >> 3) % 3);
        for (uint32_t i = 0; i < n; ++i) list.push_back(GameTipRef(reinterpret_cast<cITSGameTip *>(&F(kTip0 + (c >> i) % kTipCount))));
        void *filter = (a >> 5) % 3 == 0 ? nullptr : static_cast<void *>(&F(kFilter));
        b.push_back(Slot<uint32_t>(face, 5, &list, filter, 0x100u | (a >> 7) % 2));
        b.push_back(uint32_t(list.size()));
        b.push_back(uint32_t(list.capacity()));
        for (const GameTipRef &tip : list) b.push_back(svcfx::Norm(P(tip.Get())));
        break;
    }
    case 6: // GetText
        gOut->s.mString.assign("old");
        b.push_back(Slot<uint32_t>(face, 6, kStringIds[a % 7], gOut->Interface()) & 0xff);
        LogText(gOut->s.mString);
        break;
    case 7: { // CreateTip: the new tip becomes the current one
        DropTip(b);
        void *tip = Slot<void *>(face, 7);
        b.push_back(tip != nullptr);
        if (tip) {
            Slot<uint32_t>(tip, 1);
            HoldTip(Face(tip, uint32_t(-0x20)));
        }
        break;
    }
    case 8: b.push_back(Slot<uint32_t>(face, 8, kInstances[a % 8]) & 0xff); break; // DeleteTip
    case 9: { // SetTipTexts on the current tip, a fake or null
        void *tip = a % 4 == 0 ? nullptr : a % 4 == 1 && gTip ? Face(gTip, 0x20) : static_cast<void *>(&F(kTip0 + c % kTipCount));
        b.push_back(Slot<uint32_t>(face, 9, tip, TextArg(c >> 4), TextArg(c >> 8), TextArg(c >> 12)) & 0xff);
        break;
    }
    case 10: { // SaveTip
        void *tip = a % 4 == 0 ? nullptr : a % 4 == 1 && gTip ? Face(gTip, 0x20) : static_cast<void *>(&F(kTip0 + c % kTipCount));
        b.push_back(Slot<uint32_t>(face, 10, tip) & 0xff);
        break;
    }
    case 11: Slot<uint32_t>(face, 11, kGroups[a % 4]); break; // SetTipGroup
    case 12: case 13: // an operation on the current tip
        if (gTip) TipOp(b, gTip, a, c);
        break;
    case 14: { // the factory's CreateResource: a new tip becomes the current one
        if (gFactories.empty()) break;
        void *factory = gFactories[a % gFactories.size()];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t riid = kTipIids[(a >> 4) % 5];
        const uint32_t type = a % 3 == 0 ? kTypes[c % 4] : Tip::kType;
        const uint32_t made = Slot<uint32_t>(factory, 9, type, riid, &out) & 0xff;
        b.push_back(made);
        b.push_back(NormPtr(P(out)));
        if (made && out) {
            DropTip(b);
            const uint32_t offset = riid == Tip::kIid ? 0x20 : riid == Tip::kSerializableIid ? 0x1c : 0;
            HoldTip(Face(out, 0u - offset));
        }
        break;
    }
    case 15: case 16: { // the factory's ReadRecord / WriteRecord on the current tip or the fake resource
        if (gFactories.empty()) break;
        void *factory = gFactories[a % gFactories.size()];
        void *resource = (a >> 4) % 3 && gTip ? gTip : static_cast<void *>(&F(kResource));
        b.push_back(Slot<uint32_t>(factory, op.kind == 15 ? 10 : 11, resource, static_cast<void *>(&F(kRecord))) & 0xff);
        break;
    }
    case 17: { // the factory's QueryInterface / AddRef / Release
        if (gFactories.empty()) break;
        void *factory = gFactories[a % gFactories.size()];
        void *q = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
        const uint32_t ok = Slot<uint32_t>(factory, 0, (a >> 4) % 2 ? 1u : 0xc4e5b2a4u, &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        b.push_back(Slot<uint32_t>(factory, 1));
        b.push_back(Slot<uint32_t>(factory, 2));
        break;
    }
    default: // SetTipGroup then GetTextCount (Load with the strings at a group)
        Slot<uint32_t>(face, 11, kGroups[1 + a % 3]);
        b.push_back(Slot<uint32_t>(face, 3));
        break;
    }
}

// ---- GameTip_Run ----

uint32_t gTipKind; // 0: the default constructor, else the constructor with an instance and this kind (low bits)
void *BuildTip(difftest::Context &ctx, Blob &b, bool ours, uint32_t instance, uint32_t kind)
{
    void *memory = ::operator new(Tip::kSize);
    std::memset(memory, 0xc5, Tip::kSize);
    void *r;
    if (gTipKind == 0) {
        r = ours ? static_cast<void *>(::new (memory) Tip) : ctx.Original<void *(__thiscall *)(void *)>(0xcb5940)(memory);
    } else {
        r = ours ? static_cast<void *>(::new (memory) Tip(instance, kind))
                 : ctx.Original<void *(__thiscall *)(void *, uint32_t, uint32_t)>(0xcb58a0)(memory, instance, kind);
    }
    b.push_back(r == memory);
    return memory;
}

// ---- stand-ins: the game's new / delete over ours, so both sides' blocks meet ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void Fill(std::unique_ptr<ContentString[]> &strings, const char *const *texts, uint32_t count)
{
    strings = std::make_unique<ContentString[]>(count);
    for (uint32_t i = 0; i < count; ++i) strings[i].s.mString.assign(texts[i]);
}

template <class Run> void Compare(difftest::Context &ctx, const char *name, uint32_t seed, std::vector<Blob> (&results)[2],
                                  const std::vector<Op> &ops, uint32_t &stop)
{
    for (size_t i = 0; i < results[0].size() || i < results[1].size(); ++i) {
        const Blob empty;
        const Blob &x = i < results[0].size() ? results[0][i] : empty;
        const Blob &y = i < results[1].size() ? results[1][i] : empty;
        if (!svcfx::Same(ctx, x, y, name, seed)) {
            if (svcfx::gTrace)
                for (size_t w = 0; w < x.size() || w < y.size(); ++w)
                    fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < x.size() ? x[w] : 0, w < y.size() ? y[w] : 0);
            const size_t op = i ? i - 1 : 0;
            ctx.Fail("  at step %u of %u (kind %u)", unsigned(i), unsigned(ops.size() + 2), i && op < ops.size() ? ops[op].kind : 99u);
            stop = 1;
            return;
        }
    }
}

void Finish(Blob &b)
{
    b.push_back(uint32_t(gStackSkew)); // bytes popped beyond (or short of) what was pushed: 0
    gStackSkew = 0;
    b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
    svcfx::gEv.clear();
    svcfx::CaptureFakes(b);
}

struct Scope {
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624, shape::CodeOf(&GameDelete)};
    Scope()
    {
        char traceFlag[4];
        svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
        globals.SetWord(0x10c9378, 0);
        globals.SetWord(svcfx::kPersistCell, 0);
        globals.SetWord(kTipCell, 0);
        Fill(gNumberStrings, kNumbers, kNumberCount);
        Fill(gTextStrings, kTexts, kTextCount);
        gOut = new ContentString;
    }
    ~Scope()
    {
        delete gOut;
        gNumberStrings.reset();
        gTextStrings.reset();
        svcfx::gFrameworkIndex = 0;
        svcfx::gRanges.clear();
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;
};

void AddTextRanges()
{
    svcfx::AddRange(gOut, sizeof(ContentString), kTextTag);
    svcfx::AddRange(gTextStrings.get(), sizeof(ContentString) * kTextCount, kTextTag + 0x1000);
    svcfx::AddRange(gNumberStrings.get(), sizeof(ContentString) * kNumberCount, kTextTag + 0x2000);
}

} // namespace gts_test

using namespace gts_test;

DIFF_TEST(GameTipService_Run, 0xcb6170)
{
    // Also reached (see the file comment): 0xcb5ce0 (factory), 0xcb59c0 (constructor), 0xcb5a60 (destructor), 0xcb5b00
    // (deleting destructor), 0xcb5ad0 (its +4 thunk), 0xcb6320 (QueryInterface), 0xcb6351 / 0xcb6359 (its thunks), 0xcb6de0,
    // 0xcb5e70, 0xcb5dd0, 0xcb5e90, 0xcb5e40, 0xcb5b30, 0xcb5c70, 0xcb5d50, 0xcb6490, 0xcb63e0, 0xcb6dc0, 0xcb66a0, 0xcb6250,
    // 0xcb67e0; the tips' 0xcb58a0, 0xcb5940, 0xcb5a40, 0xcb5ae0, 0xcb62d0, 0xcb630d, 0xcb6315, 0xcb5b20, 0xcb63d4, 0xcb6e20,
    // 0xcb6370, 0xcb5dc0, 0xcb6080, 0xcb60d0, 0xcb6120, 0xcb6090, 0xcb6130, 0xcb60e0; the factory's 0xcb5bb0, 0xcb6c20,
    // 0xcb6cf0; the service getter 0xb39020. The list's growth 0xcb5680 is the original's on its side, compared through
    // the list (size, capacity, elements) and the tips' counts.
    Scope scope;
    gCtx = &ctx;
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        const std::vector<Op> ops = MakeServiceOps(seed);
        gFactory = seed % 4 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes(seed);
            AddTextRanges();
            gTip = nullptr;
            gTipNo = 0;
            Blob first;
            void *object = BuildService(ctx, first, gOurs);
            svcfx::AddRange(object, Sys::kSize, kTag);
            if (gWorld.tipService) {
                *GameAddress<void *>(kTipCell) = Face(object, 0x1c); // the tips' texts come from the service under test
                gWorld.tipService = false;
            }
            DumpService(first, object);
            Finish(first);
            results[side].push_back(std::move(first));
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    ServiceOp(b, object, ops[i]);
                    DumpService(b, object);
                } else {
                    DropTip(b);
                    for (void *factory : gFactories) b.push_back(Slot<uint32_t>(factory, 2)); // the last release deletes it
                    gFactories.clear();
                    b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys the service
                }
                Finish(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
            *GameAddress<uint32_t>(kTipCell) = 0;
        }
        uint32_t stop = 0;
        Compare<int>(ctx, "GameTipService_Run", seed, results, ops, stop);
        if (stop) break;
    }
}

DIFF_TEST(GameTip_Run, 0xcb6e20)
{
    // Also reached (see the file comment): 0xcb5940, 0xcb58a0 (constructors), 0xcb5a40 (destructor), 0xcb5ae0 (deleting
    // destructor), 0xcb62d0 (QueryInterface), 0xcb630d / 0xcb6315 (its thunks), 0xcb5b20 / 0xcb63d4 (AddRef / Release
    // thunks), 0xcb6370 (Read), 0xcb5dc0 (GetGZCLSID), 0xcb6080 (GetId), 0xcb60d0 / 0xcb6120 (GetStringId0 / 2), 0xcb6090
    // / 0xcb6130 / 0xcb60e0 (GetString0 / 1 / 2), 0xb39020 (the service getter).
    Scope scope;
    gCtx = &ctx;
    static const uint32_t kKinds[] = {0, Tip::kUserTipKind, 3, 0x1001, 0x11000, 0xffffffff};
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        std::mt19937 rng(seed * 97 + 1);
        gTipKind = rng() % 4 == 0 ? 0 : 1;
        const uint32_t instance = kInstances[rng() % 8];
        const uint32_t kind = kKinds[rng() % 6];
        std::vector<Op> ops;
        const unsigned n = 1 + rng() % 16;
        for (unsigned i = 0; i < n; ++i) ops.push_back({0, uint32_t(rng()), uint32_t(rng())});
        const bool presetCache = rng() % 3 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            gOurs = side == 1;
            BuildFakes(seed);
            AddTextRanges();
            if (presetCache) *GameAddress<void *>(kTipCell) = &F(kTipSvc);
            Blob first;
            void *tip = BuildTip(ctx, first, gOurs, instance, kind);
            svcfx::AddRange(tip, Tip::kSize, kTipTag);
            DumpTip(first, tip);
            Finish(first);
            results[side].push_back(std::move(first));
            Slot<uint32_t>(tip, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    TipOp(b, tip, ops[i].arg, ops[i].arg2);
                    DumpTip(b, tip);
                } else {
                    b.push_back(Slot<uint32_t>(tip, 2)); // the last release destroys it
                }
                Finish(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
            *GameAddress<uint32_t>(kTipCell) = 0;
        }
        uint32_t stop = 0;
        Compare<int>(ctx, "GameTip_Run", seed, results, ops, stop);
        if (stop) break;
    }
}
