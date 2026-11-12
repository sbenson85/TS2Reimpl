// Differential tests for cTSEffectsManager (src/ts/cTSEffectsManager.*), lane IT.
//
// Standalone shape. Three ways to run the same random operations, compared word for word:
//   mode 0: the original constructor 0xbc4870 builds the manager (game vtables); the original code is called at the RVAs;
//   mode 1: our constructor builds it; our members are called through our vtables (the non-virtual ones directly);
//   mode 2: the original constructor builds it; the adapters (the hook entries) are called.
// After each operation: the result, the out-parameters, the event log (every call into a fake or a stand-in, every AddRef
// and Release with the fake's id, pointers into the manager as offsets) and the whole manager (PushManager: the collection,
// every container with its buckets and capacities, every scalar word including the ones the constructor leaves, the
// random generator's state). At the end the destructor: its log and the frees by size.
//
// Code that is not ours yet is stood in for on both sides (CodePatch over the image's entry, which our OriginalFunction
// calls reach too): the callback timer's constructor / destructor 0x9ccf32 / 0x9ccfa9, the cheats 0xbc5aa0 / 0xbdb700
// (ours since lane YB, tests/ts_effect_cheats.cpp: StandIn over both sides; the stands also register what they make),
// the message server getter 0x9c5a89, ApplyDetail 0xbcb4c0, the effect re-attach 0xbead40, the name matcher 0x9ccea3,
// (in the older tests also the effect reload 0xbc9e20 and Update 0xbc6fe0, slot 19, called by UpdateMs; the lifecycle test
// runs both for real), the overlay manager constructor 0xbd9fd0, the effect description 0xbe9670 and the done check 0xbe8de0 (lane XG), and in
// the lifecycle test the image's operator new / delete (0x824632 / 0x824624, to our counted heap). The framework service
// (game global 0x10c9378) and the debug setting byte 0x107d57c are overridden for the run.
//
// Lane XG (EffectsManager_Lifecycle, 60 seeds x 3 modes of random operations; mode 0 original, 1 ours through the vtables,
// 2 the adapters): 0xbc8fa0 (Init), 0xbcaa00 (Shutdown), 0xbc7350 (OnTimer), 0xbca590 (SendText), 0xbc9840 (LoadEffects),
// 0xbc61e0 (CreateEffect), 0xbc6360 (CreateVisualEffect), 0xbc6fe0 (Update), 0xbc8760 (ListActive), 0xbc8900
// (ListEffects), 0xbc66d0 (DoMessage), 0xbc9ab0 (AcquireEffect), 0xbc8590 / 0xbc8620 (FindEffectIndex, FindAliasIndex),
// 0xbc9e20 (ApplyParserState), 0xbc6e10 (ApplySettings), 0xbcb3f0 (CounterBlend), 0xbc6560 (ActivityWalk). The message
// ids sent are compared (word 3); the other message words by presence only. Slot 1 (0x207d70) and the +8 AddRef
// 0xbc5d10 are the folded shared_vf_207d70: not hooked.
//
// Covered RVAs: 0xbc4870 (constructor), 0xbc5500 (destructor), 0xbc5970 (deleting destructor), 0xbc86f0 (Create),
// 0xbc9ea0 (QueryInterface), 0xbca210 (Release), 0xbca680 (SetPaths), 0xbc5c10 (AddPaths), 0xbca7c0 (SetFlag), 0xbc86c0
// (GetFlag), 0xbc5b20 (ParseResources), 0xbc8e20 (HasEffectVariant), 0xbc8f00 (HasEffect), 0xbca860 (SetState), 0xbca380
// (SetMaxDetail), 0xbca470 (SetDetail), 0xbca580 (SetTimeMode), 0xbc6f80 (UpdateMs), 0xbc8d80 (GetParams, slots 21/22),
// 0xbc7340 (GetBCC), 0xbca1d0 (SetSlotObject), 0xbc9e10 (GetSlotObject), 0xbc9f10 / 0xbc5df0 / 0xbca070 / 0xbc5f20 (named
// objects), 0xbca340 / 0xbc5d20 (debug bits), 0xbc9e80 (GetRendererBase), 0xbc8cb0 (PrintStats), 0xbc8c20
// (CopyEffectNames), 0xbca810 / 0xbca920 / 0xbca960 / 0xbca330 / 0xbcbde0 (scene and camera), 0xbca250 (GetDebugValue),
// 0xbc5d50 (Clear, collection face slot 3), 0xbc73d0 (Link, slot 4), 0xbc8d90 (HasEffectName), 0xbcb1a0
// (ReleaseAllEffects), 0xbcb210 (RefreshDetail), 0xbc59d0 (FlushPending), 0xbc6670 (Reload); the folded slots 1 (0x207d70),
// 23 (0x238f10) and 34 (0x148ba0) through our vtable.
#include "rz/RZMessageServer.h"
#include "ts_services_fixture.h"
#include "ts_effects_collection.h"

#include "code_patch.h"
#include "free_recorder.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZString.h"
#include "rz/RZWildcard.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cIGZPropertySet.h"
#include "rz/cRZFile.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSEffectCheats.h"
#include "ts/cTSEffectsManager.h"
#include "ts/cTSOverlayManager.h"
#include "ts/cTSVisualEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <limits>
#include <new>

namespace {

using namespace fxtest;
using Mgr = cTSEffectsManager;
using A = cTSEffectsManagerAdapters;

enum : uint32_t {
    evAddRef = 0xa1000000,
    evRelease = 0xa2000000,
    evQI = 0xa3000000,
    evParserChanged = 0xa4000000,
    evRendererNotify = 0xa5000000,
    evRegister = 0xa6000000,
    evUnregister = 0xa7000000,
    evService = 0xa8000000,
    evGetResource = 0xa9000000,
    evParse = 0xaa000000,
    evDebugObject = 0xab000000,
    evDebugPair = 0xac000000,
    evDebugValue = 0xad000000,
    evPrint = 0xae000000,
    evEffectSuspend = 0xaf000000,
    evEffectHide = 0xb0000000,
    evEffectFace = 0xb1000000,
    evTimerCtor = 0xb2000000,
    evTimerDtor = 0xb3000000,
    evCheats = 0xb4000000,
    evServerGet = 0xb5000000,
    evApply = 0xb6000000,
    evReattach = 0xb7000000,
    evMatch = 0xb8000000,
    evReloadEffects = 0xb9000000,
    evUpdate = 0xba000000,
    evTrap = 0xbf000000,
};

// ---- the fakes ----

// Every fake starts with a vtable pointer, an id and a reference count. Ids: unknowns 0x100+, effects 0x200+, the parser
// 0x300, the renderer's base 0x301, the server 0x302, the framework 0x303, the resource manager 0x304, resources 0x310+,
// the debug service 0x305 and object 0x306, the output 0x307.
struct Fake {
    void *const *vt;
    uint32_t id;
    uint32_t refs;
    uint32_t value[6]; // per-role answers
};

uint8_t *gBase = nullptr; // the manager under test (offsets of pointers into it are logged)
const uint8_t *gFakesBegin = nullptr, *gFakesEnd = nullptr; // the current World (its fakes: ids are read)
uint32_t Off(const void *p)
{
    const auto *q = static_cast<const uint8_t *>(p);
    if (gBase && q >= gBase && q < gBase + sizeof(Mgr)) return 0x0f000000u | uint32_t(q - gBase);
    if (p && q >= gFakesBegin && q < gFakesEnd) return 0x0e000000u | static_cast<const Fake *>(p)->id;
    return p ? 0x0c000000u : 0;
}

bool __fastcall FkQI(Fake *f, void *, uint32_t riid, void **)
{
    Ev({evQI | f->id, riid});
    return false;
}
uint32_t __fastcall FkAddRef(Fake *f, void *)
{
    Ev({evAddRef | f->id});
    return ++f->refs;
}
uint32_t __fastcall FkRelease(Fake *f, void *)
{
    Ev({evRelease | f->id});
    return --f->refs;
}
void __fastcall FkTrap(Fake *f, void *)
{
    Ev({evTrap | f->id});
}
// The parser's slot 10: whether the effects changed.
bool __fastcall FkParserChanged(Fake *f, void *)
{
    Ev({evParserChanged | f->id});
    return f->value[0] != 0;
}
// The message server's slots 5 / 6.
bool __fastcall FkRegister(Fake *f, void *, void *target, uint32_t message)
{
    Ev({evRegister | f->id, Off(target), message});
    return true;
}
bool __fastcall FkUnregister(Fake *f, void *, void *target, uint32_t message)
{
    Ev({evUnregister | f->id, Off(target), message});
    return true;
}
// The framework service's slot 5: the resource manager (0x56b906e) or the debug service (0x8af76dc8), counted.
Fake *gResourceManager = nullptr;
Fake *gDebugService = nullptr;
Fake *ServiceFor(uint32_t id);
bool __fastcall FkGetService(Fake *f, void *, uint32_t id, uint32_t iid, void **out)
{
    Ev({evService | f->id, id, iid});
    Fake *answer = ServiceFor(id);
    if (!answer) return false;
    *out = answer;
    FkAddRef(answer, nullptr);
    return true;
}
// The resource manager's slot 5: a resource for keys whose first word is odd (counted), else false.
Fake *gResources[8];
bool __fastcall FkGetResource(Fake *f, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    Ev({evGetResource | f->id, key[0], key[1], key[2], key[3], iid, a, b});
    if ((key[0] & 1) == 0) return false;
    Fake *resource = gResources[key[1] % 8];
    *out = resource;
    FkAddRef(resource, nullptr);
    return true;
}
// A resource's slot 5: it parses itself into the collection face it is given.
void __fastcall FkParse(Fake *f, void *, void *collection)
{
    Ev({evParse | f->id, Off(collection)});
}
// The debug service's slot 5: its object (value[0] says whether there is one).
Fake *gDebugObject = nullptr;
void *__fastcall FkDebugObject(Fake *f, void *)
{
    Ev({evDebugObject | f->id});
    return f->value[0] ? gDebugObject : nullptr;
}
// The debug object's slot 5 (writes a pair, returns it) and slot 7 (a value for the pair).
int32_t *__fastcall FkDebugPair(Fake *f, void *, int32_t *out, int32_t a, int32_t b, int32_t c)
{
    Ev({evDebugPair | f->id, uint32_t(a), uint32_t(b), uint32_t(c)});
    out[0] = int32_t(f->value[1]);
    out[1] = int32_t(f->value[2]);
    return out;
}
int32_t __fastcall FkDebugValue(Fake *f, void *, int32_t a, int32_t b)
{
    Ev({evDebugValue | f->id, uint32_t(a), uint32_t(b)});
    return int32_t(f->value[3] + a);
}
// The output's slot 31 (a printf, cdecl): the text is logged.
void __cdecl FkPrint(Fake *f, const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Ev({evPrint | f->id, uint32_t(n)});
    for (int i = 0; i < n && i < int(sizeof(text)); i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, text + i, (n - i) < 4 ? size_t(n - i) : 4);
        Ev({w});
    }
}

// ---- the fakes of the lifecycle members (Init, Shutdown, Update, DoMessage, the lists, creation, loading) ----
//
// Every slot logs its call (the object's id and slot, the arguments by value, pointers as offsets or ids) and answers from the
// fake's own words. Arities are the callers': a fastcall fake pops exactly what the caller pushed.

enum : uint32_t { evSlot = 0xc0000000, evHook = 0xd0000000 };

Fake *gMapsFake = nullptr; // what slot 7 of an unknown (the maps object's GetMap) hands out: the view object
Fake *gViewObject = nullptr;
Fake *gTextObject = nullptr;
Fake *gCreatorFake = nullptr;
Fake *gParserFake = nullptr;
Fake *gRendererBase = nullptr;
Fake *gEffectsService = nullptr, *gCheatsService = nullptr, *gSettingsService = nullptr, *gDebugSizeService = nullptr;
Fake *gDebugObjectFake = nullptr;

uint32_t TextWords(const char *text)
{
    if (!text) return 0xdead0000u;
    uint32_t h = 0x811c9dc5u;
    for (const char *p = text; *p; ++p) h = (h ^ uint8_t(*p)) * 0x01000193u;
    return h ^ uint32_t(std::strlen(text));
}
uint32_t RvaOf(const void *gameAddress)
{
    return uint32_t(static_cast<const uint8_t *>(gameAddress) - static_cast<const uint8_t *>(GameAddress<void>(0)));
}

template <uint32_t S> bool __fastcall FkLog0(Fake *f, void *)
{
    Ev({evSlot | (S << 16) | f->id});
    return true;
}
// An unknown's slot 7: the maps object's GetMap(index): the view object.
void *__fastcall FkGetMap(Fake *f, void *, int32_t index)
{
    Ev({evSlot | (7u << 16) | f->id, uint32_t(index)});
    return gViewObject;
}
// An unknown's slot 34: the size (a width and a height from its words).
void __fastcall FkSize(Fake *f, void *, int32_t *width, int32_t *height)
{
    Ev({evSlot | (34u << 16) | f->id});
    *width = int32_t(f->value[0]);
    *height = int32_t(f->value[1]);
}
// An unknown's slot 144: the projected point: three floats from its words.
const float *__fastcall FkProject(Fake *f, void *, float *out, const float *point, uint32_t flags)
{
    Ev({evSlot | (144u << 16) | f->id, Bits(point[0]), Bits(point[1]), Bits(point[2]), flags});
    for (int i = 0; i < 3; ++i) out[i] = std::bit_cast<float>(f->value[3 + i]);
    return out;
}
// The view object: slot 3 gives a rectangle (four floats), slot 5 the height at a point.
void __fastcall FkViewRect(Fake *f, void *, float *a, float *b, float *c, float *d)
{
    Ev({evSlot | (3u << 16) | f->id});
    *a = std::bit_cast<float>(f->value[0]);
    *b = std::bit_cast<float>(f->value[1]);
    *c = std::bit_cast<float>(f->value[2]);
    *d = std::bit_cast<float>(f->value[3]);
}
float __fastcall FkViewHeight(Fake *f, void *, float x, float y)
{
    Ev({evSlot | (5u << 16) | f->id, Bits(x), Bits(y)});
    return std::bit_cast<float>(f->value[4]);
}
const char *const kTexts[] = {"", "first text", "a much longer effects text, past sixteen", "x"};
const char *__fastcall FkText(Fake *f, void *)
{
    Ev({evSlot | (0x2cu << 16) | f->id});
    return f->value[0] == 3 ? nullptr : kTexts[f->value[0] % 4];
}
// The parser: 5 (the renderer's base, a number), 6 (a path, a kind), 7 (whether there are changes), 9 (the text object).
void __fastcall FkParserSet(Fake *f, void *, void *base, uint32_t number)
{
    Ev({evSlot | (5u << 16) | f->id, Off(base), number});
}
void __fastcall FkParserPath(Fake *f, void *, const char *path, uint32_t kind)
{
    Ev({evSlot | (6u << 16) | f->id, TextWords(path), kind});
}
bool __fastcall FkParserHasChanges(Fake *f, void *)
{
    Ev({evSlot | (7u << 16) | f->id});
    return f->value[1] != 0;
}
void *__fastcall FkParserText(Fake *f, void *)
{
    Ev({evSlot | (9u << 16) | f->id});
    return gTextObject;
}
// The resource manager: 19 (the keys of a type into a vector), 20 (a factory registered).
uint32_t gKeyCount = 0;
bool __fastcall FkListKeys(Fake *f, void *, ServiceBlockVector *keys, uint32_t type)
{
    Ev({evSlot | (19u << 16) | f->id, type, gKeyCount});
    if (gKeyCount == 0) return f->value[5] != 0;
    auto *block = static_cast<uint32_t *>(GZServiceAllocate(size_t(gKeyCount) * 16));
    for (uint32_t i = 0; i < gKeyCount; ++i) {
        block[i * 4 + 0] = (i * 5 + f->value[1]) | 1; // odd: FkGetResource finds a resource
        block[i * 4 + 1] = (i * 3 + f->value[2]) % 8;
        block[i * 4 + 2] = (f->value[3] + i * 7) % 5; // the instance: sorted by it, 1 sets the flag
        block[i * 4 + 3] = i;
    }
    keys->mFirst = block;
    keys->mLast = block + gKeyCount * 4;
    keys->mEnd = block + gKeyCount * 4;
    return true;
}
void __fastcall FkRegisterFactory(Fake *f, void *, uint32_t type, uint32_t iid, void *factory)
{
    Ev({evSlot | (20u << 16) | f->id, type, iid, Off(factory)});
}
// The settings service: 4 (notify of an option), 5 (withdraw).
void __fastcall FkSettingsAdd(Fake *f, void *, void *target, uint32_t message, const GZServiceString *name, void *cell)
{
    Ev({evSlot | (4u << 16) | f->id, Off(target), message, TextWords(name->c_str()), RvaOf(cell)});
}
void __fastcall FkSettingsRemove(Fake *f, void *, void *target, uint32_t message, const GZServiceString *name)
{
    Ev({evSlot | (5u << 16) | f->id, Off(target), message, TextWords(name->c_str())});
}
// The cheats service: 4 and 6.
void __fastcall FkCheatAdd(Fake *f, void *, void *cheat)
{
    Ev({evSlot | (4u << 16) | f->id, Off(cheat)});
}
void __fastcall FkCheatRemove(Fake *f, void *, void *cheat)
{
    Ev({evSlot | (6u << 16) | f->id, Off(cheat)});
}
// The effects service: the names (0x20, 0x21) and the indices (0x22, 0x23) of effects made or missed.
template <uint32_t S> void __fastcall FkEffectName(Fake *f, void *, const fx::PathString *name)
{
    Ev({evSlot | (S << 16) | f->id, TextWords(name->mText.c_str()), uint32_t(name->mText.size())});
}
template <uint32_t S> void __fastcall FkEffectIndex(Fake *f, void *, int32_t index)
{
    Ev({evSlot | (S << 16) | f->id, uint32_t(index)});
}
// The debug object's slot 6 (a value handed back).
void __fastcall FkDebugSet(Fake *f, void *, int32_t a, int32_t b)
{
    Ev({evSlot | (6u << 16) | f->id, uint32_t(a), uint32_t(b)});
}
// The message server's slot 4: a message posted (its words, with pointers by presence; the string's text).
void __fastcall FkPost(Fake *f, void *, const uint32_t *message, uint32_t flags)
{
    Ev({evSlot | (4u << 16) | f->id, flags});
    // Word 2 is the flag byte; the three bytes above it are left as the allocation had them.
    for (uint32_t i = 1; i < 11; ++i) {
        const uint32_t word = i == 2 ? message[i] & 0xffu : message[i];
        // Word 3 is the message id: compared as it is. The others (pointers, uninitialised value words) by presence.
        Ev({i == 3 ? word : word >= 0x10000u ? 0x0c000000u : word});
    }
    if (const auto *text = reinterpret_cast<const fx::PathString *>(message[9])) {
        Ev({TextWords(text->mText.c_str()), uint32_t(text->mText.size())});
    }
}
// The director's framework: CreateInstance (class, interface, out).
Fake *gRendererObject = nullptr;
bool __fastcall FkCreateInstance(Fake *f, void *, uint32_t classId, uint32_t iid, void **out)
{
    Ev({evSlot | (3u << 16) | f->id, classId, iid});
    if (classId == 0x5cf9459a) {
        *out = gParserFake;
        FkAddRef(gParserFake, nullptr);
        return true;
    }
    if (classId == 0xa956ad14) {
        *out = gRendererObject;
        FkAddRef(gRendererBase, nullptr);
        return true;
    }
    return false;
}
// The callback timer's slots: 5 (start: milliseconds and target), 7 (stop), 9 (set the callback).
void *gTimerCallbackImage = nullptr;
bool __fastcall TimerStart(void *self, void *, uint32_t ms, void *target)
{
    Ev({evSlot | (5u << 16) | 0xfff, Off(self), ms, Off(target)});
    return true;
}
bool __fastcall TimerStop(void *self, void *)
{
    Ev({evSlot | (7u << 16) | 0xfff, Off(self)});
    return true;
}
bool __fastcall TimerSetCallback(void *self, void *, void *callback, void *data)
{
    Ev({evSlot | (9u << 16) | 0xfff, Off(self), uint32_t(callback == gTimerCallbackImage || callback == reinterpret_cast<void *>(&Mgr::OnTimer)),
        Off(data)});
    return true;
}
struct TimerVT {
    void *slots[12];
    TimerVT()
    {
        for (void *&s : slots) s = reinterpret_cast<void *>(&FkTrap);
        slots[5] = reinterpret_cast<void *>(&TimerStart);
        slots[7] = reinterpret_cast<void *>(&TimerStop);
        slots[9] = reinterpret_cast<void *>(&TimerSetCallback);
    }
};
const TimerVT kTimerVT;
// The message object: 4 type, 9 data1, 13 has data 2, 15 the data 2 pointer.
uint32_t __fastcall FkMsgType(Fake *f, void *)
{
    Ev({evSlot | (4u << 16) | f->id});
    return f->value[0];
}
uint32_t __fastcall FkMsgData1(Fake *f, void *)
{
    Ev({evSlot | (9u << 16) | f->id});
    return f->value[1];
}
bool __fastcall FkMsgHas2(Fake *f, void *)
{
    Ev({evSlot | (13u << 16) | f->id});
    return f->value[2] != 0;
}
void *__fastcall FkMsgVoid2(Fake *f, void *)
{
    Ev({evSlot | (15u << 16) | f->id});
    return f->value[2] == 2 ? nullptr : &f->value[4];
}
bool gServicesOn = true; // false: the framework knows only the old two (a missing service)
Fake *ServiceFor(uint32_t id)
{
    switch (id) {
    case 0x56b906e: return gResourceManager;
    case 0x8af76dc8: return gDebugService;
    default: break;
    }
    if (!gServicesOn) return nullptr;
    switch (id) {
    case 0x8d127822: return gEffectsService;
    case 0x2a4915ea: return gCheatsService;
    case 0x0ac1887e: return gSettingsService;
    case 0xa417445e: return gDebugSizeService;
    default: return nullptr;
    }
}

template <size_t N> struct VTable {
    void *slots[N];
    VTable()
    {
        for (void *&s : slots) s = reinterpret_cast<void *>(&FkTrap);
        slots[0] = reinterpret_cast<void *>(&FkQI);
        slots[1] = reinterpret_cast<void *>(&FkAddRef);
        slots[2] = reinterpret_cast<void *>(&FkRelease);
    }
};
struct VTables {
    VTable<160> unknown, parser, server, framework, resourceManager, resource, debugService, debugObject, renderer;
    VTable<160> view, overlay, text, creator, settings, cheats, effectsService, message;
    VTable<40> output;
    VTables()
    {
        parser.slots[3] = reinterpret_cast<void *>(&FkLog0<3>);
        parser.slots[4] = reinterpret_cast<void *>(&FkLog0<4>);
        parser.slots[5] = reinterpret_cast<void *>(&FkParserSet);
        parser.slots[6] = reinterpret_cast<void *>(&FkParserPath);
        parser.slots[7] = reinterpret_cast<void *>(&FkParserHasChanges);
        parser.slots[9] = reinterpret_cast<void *>(&FkParserText);
        parser.slots[10] = reinterpret_cast<void *>(&FkParserChanged);
        server.slots[4] = reinterpret_cast<void *>(&FkPost);
        server.slots[5] = reinterpret_cast<void *>(&FkRegister);
        server.slots[6] = reinterpret_cast<void *>(&FkUnregister);
        framework.slots[5] = reinterpret_cast<void *>(&FkGetService);
        resourceManager.slots[5] = reinterpret_cast<void *>(&FkGetResource);
        resourceManager.slots[19] = reinterpret_cast<void *>(&FkListKeys);
        resourceManager.slots[20] = reinterpret_cast<void *>(&FkRegisterFactory);
        resource.slots[5] = reinterpret_cast<void *>(&FkParse);
        debugService.slots[5] = reinterpret_cast<void *>(&FkDebugObject);
        debugObject.slots[5] = reinterpret_cast<void *>(&FkDebugPair);
        debugObject.slots[6] = reinterpret_cast<void *>(&FkDebugSet);
        debugObject.slots[7] = reinterpret_cast<void *>(&FkDebugValue);
        output.slots[31] = reinterpret_cast<void *>(&FkPrint);
        // an unknown: Shutdown (4), the maps' GetMap (7), the debug service's size (34), a camera's projection (144)
        unknown.slots[4] = reinterpret_cast<void *>(&FkLog0<4>);
        unknown.slots[7] = reinterpret_cast<void *>(&FkGetMap);
        unknown.slots[34] = reinterpret_cast<void *>(&FkSize);
        unknown.slots[144] = reinterpret_cast<void *>(&FkProject);
        view.slots[3] = reinterpret_cast<void *>(&FkViewRect);
        view.slots[5] = reinterpret_cast<void *>(&FkViewHeight);
        overlay.slots[3] = reinterpret_cast<void *>(&FkLog0<3>);
        overlay.slots[4] = reinterpret_cast<void *>(&FkLog0<4>);
        text.slots[0x2c] = reinterpret_cast<void *>(&FkText);
        creator.slots[3] = reinterpret_cast<void *>(&FkCreateInstance);
        settings.slots[4] = reinterpret_cast<void *>(&FkSettingsAdd);
        settings.slots[5] = reinterpret_cast<void *>(&FkSettingsRemove);
        cheats.slots[4] = reinterpret_cast<void *>(&FkCheatAdd);
        cheats.slots[6] = reinterpret_cast<void *>(&FkCheatRemove);
        effectsService.slots[0x20] = reinterpret_cast<void *>(&FkEffectName<0x20>);
        effectsService.slots[0x21] = reinterpret_cast<void *>(&FkEffectName<0x21>);
        effectsService.slots[0x22] = reinterpret_cast<void *>(&FkEffectIndex<0x22>);
        effectsService.slots[0x23] = reinterpret_cast<void *>(&FkEffectIndex<0x23>);
        message.slots[4] = reinterpret_cast<void *>(&FkMsgType);
        message.slots[9] = reinterpret_cast<void *>(&FkMsgData1);
        message.slots[13] = reinterpret_cast<void *>(&FkMsgHas2);
        message.slots[15] = reinterpret_cast<void *>(&FkMsgVoid2);
    }
};
const VTables kVT;

// The renderer: an object with virtual bases (vbtable at +4: entries 1 and 2 at +4 + 8 and +4 + 0x10). Release goes to the
// first base (a Fake at +0xc); slot 6 of the object is a notification.
void __fastcall FkRendererNotify(void *self, void *)
{
    Ev({evRendererNotify, Off(self)});
}
void __fastcall FkRendererAttach0(void *self, void *, void *argument)
{
    Ev({evSlot | (0u << 16) | 0x301, Off(argument)});
}
void __fastcall FkRendererAttach1(void *self, void *, void *argument)
{
    Ev({evSlot | (1u << 16) | 0x301, Off(argument)});
}
void __fastcall FkRendererFlag(void *self, void *, uint32_t flag)
{
    Ev({evSlot | (4u << 16) | 0x301, flag});
}
const char *__fastcall FkRendererName(void *self, void *, const char *name, uint32_t variant)
{
    Ev({evSlot | (5u << 16) | 0x301, TextWords(name), variant});
    return variant % 3 == 0 ? nullptr : variant % 3 == 1 ? "renderer name" : "";
}
const int32_t kRendererVbtable[3] = {-4, 8, 0x10};
struct RendererVT {
    void *slots[8];
    RendererVT()
    {
        for (void *&s : slots) s = reinterpret_cast<void *>(&FkTrap);
        slots[0] = reinterpret_cast<void *>(&FkRendererAttach0);
        slots[1] = reinterpret_cast<void *>(&FkRendererAttach1);
        slots[4] = reinterpret_cast<void *>(&FkRendererFlag);
        slots[5] = reinterpret_cast<void *>(&FkRendererName);
        slots[6] = reinterpret_cast<void *>(&FkRendererNotify);
    }
};
const RendererVT kRendererVT;
struct Renderer {
    const RendererVT *vt = &kRendererVT;
    const int32_t *vbptr = kRendererVbtable;
    uint32_t pad = 0;
    Fake base{kVT.unknown.slots, 0x301, 0, {}};
    uint32_t tail[4] = {};
};
static_assert(offsetof(Renderer, base) == 0xc);

// The pool's effects (400 bytes): +0 vtable (1 / 2 counted, 7 SetSuspended, 8 SetHidden), +4 a second face (slot 1
// logged), random fields; the id at +0x100, the count at +0x104.
struct EffectFake {
    void *const *vt;
    void *const *vt4;
    uint8_t body[400 - 8];
    uint32_t &Id() { return *reinterpret_cast<uint32_t *>(body + 0x100 - 8); }
    uint32_t &Refs() { return *reinterpret_cast<uint32_t *>(body + 0x104 - 8); }
};
static_assert(sizeof(EffectFake) == 400);
EffectFake *EffectOfFace(void *face4) { return reinterpret_cast<EffectFake *>(static_cast<uint8_t *>(face4) - 4); }
uint32_t __fastcall EfAddRef(EffectFake *e, void *)
{
    Ev({evAddRef | e->Id()});
    return ++e->Refs();
}
uint32_t __fastcall EfRelease(EffectFake *e, void *)
{
    Ev({evRelease | e->Id()});
    return --e->Refs();
}
void __fastcall EfSuspend(EffectFake *e, void *, bool on)
{
    Ev({evEffectSuspend | e->Id(), uint32_t(on)});
}
void __fastcall EfHide(EffectFake *e, void *, bool on)
{
    Ev({evEffectHide | e->Id(), uint32_t(on)});
}
void __fastcall EfFace(void *face4, void *)
{
    Ev({evEffectFace | EffectOfFace(face4)->Id()});
}
void __fastcall EfTrap(EffectFake *e, void *)
{
    Ev({evTrap | e->Id()});
}
// The effect's slots the lifecycle members use: 0 of the +4 face (attach to the manager), 3 (activate), 10 (placement),
// 12 (the view vector), and 5 of the +4 face (a tick: the time and the counters).
void __fastcall EfAttach(void *face4, void *, void *owner)
{
    Ev({evSlot | (0u << 16) | 0xe00, EffectOfFace(face4)->Id(), Off(owner)});
}
void __fastcall EfActivate(EffectFake *e, void *, uint32_t flag)
{
    Ev({evSlot | (3u << 16) | 0xe00, e->Id(), flag});
}
void __fastcall EfPlace(EffectFake *e, void *, const fx::EffectsTransform *placement)
{
    Ev({evSlot | (10u << 16) | 0xe00, e->Id(), placement->flags, Bits(placement->translation.x), Bits(placement->translation.y),
        Bits(placement->translation.z)});
}
bool __fastcall EfViewVector(EffectFake *e, void *, uint32_t what, const float *view, uint32_t how)
{
    Ev({evSlot | (12u << 16) | 0xe00, e->Id(), what, Bits(view[0]), Bits(view[1]), Bits(view[2]), how});
    return true;
}
void __fastcall EfTick(void *face4, void *, float time, uint32_t *counters)
{
    EffectFake *e = EffectOfFace(face4);
    Ev({evSlot | (5u << 16) | 0xe00, e->Id(), Bits(time)});
    counters[e->Id() % 6] += 1 + e->Id() % 3;
    counters[(e->Id() + 2) % 6] += 2;
}
struct EffectVT {
    void *slots[16];
    void *face[8];
    EffectVT()
    {
        for (void *&s : slots) s = reinterpret_cast<void *>(&EfTrap);
        slots[1] = reinterpret_cast<void *>(&EfAddRef);
        slots[2] = reinterpret_cast<void *>(&EfRelease);
        slots[3] = reinterpret_cast<void *>(&EfActivate);
        slots[7] = reinterpret_cast<void *>(&EfSuspend);
        slots[8] = reinterpret_cast<void *>(&EfHide);
        slots[10] = reinterpret_cast<void *>(&EfPlace);
        slots[12] = reinterpret_cast<void *>(&EfViewVector);
        for (void *&s : face) s = reinterpret_cast<void *>(&EfTrap);
        face[0] = reinterpret_cast<void *>(&EfAttach);
        face[1] = reinterpret_cast<void *>(&EfFace);
        face[5] = reinterpret_cast<void *>(&EfTick);
    }
};
const EffectVT kEffectVT;

// ---- stand-ins over code not ours yet ----

void *__fastcall StandTimerCtor(uint8_t *self, void *)
{
    Ev({evTimerCtor, Off(self)});
    for (int i = 0; i < 0x28; ++i) self[i] = uint8_t(0x40 + i);
    const void *vtable = kTimerVT.slots; // Init starts and Shutdown stops the timer through these
    std::memcpy(self, &vtable, sizeof(vtable));
    return self;
}
void __fastcall StandTimerDtor(uint8_t *self, void *)
{
    Ev({evTimerDtor, Off(self)});
}
Fake *gCheats[3];
void __cdecl StandCheatsA(void *vector, void *manager)
{
    Ev({evCheats | 1, Off(vector), Off(manager)});
    auto *v = static_cast<std::vector<Mgr::UnknownRef> *>(vector);
    v->push_back(Mgr::UnknownRef(reinterpret_cast<cIGZUnknown *>(gCheats[0])));
    v->push_back(Mgr::UnknownRef(reinterpret_cast<cIGZUnknown *>(gCheats[1])));
}
void __cdecl StandCheatsB(void *vector, void *manager)
{
    Ev({evCheats | 2, Off(vector), Off(manager)});
    static_cast<std::vector<Mgr::UnknownRef> *>(vector)->push_back(Mgr::UnknownRef(reinterpret_cast<cIGZUnknown *>(gCheats[2])));
}
Fake *gServer = nullptr;
bool gServerOn = true;
void *__cdecl StandServer()
{
    Ev({evServerGet});
    return gServerOn ? gServer : nullptr;
}
uint32_t EffectId(const void *e)
{
    return e ? const_cast<EffectFake *>(static_cast<const EffectFake *>(e))->Id() : 0;
}
// EffectsManager_FlushRounds: while gRequeueLeft > 0, each activation (activate 1) queues another pool index on the
// manager's pending list, as an effect that starts others would, so FlushPending keeps going round.
Mgr *gRequeueMgr = nullptr;
uint32_t gRequeueLeft = 0;
uint32_t gRequeueNext = 0;
void __fastcall StandApply(void *manager, void *, void *effect, uint32_t activate, uint32_t bits)
{
    // The original reads the reloading flag (+0xa59) Reload sets around RefreshDetail.
    Ev({evApply, Off(manager), EffectId(effect), activate, bits, static_cast<const uint8_t *>(manager)[0xa59]});
    if (gRequeueMgr && activate == 1 && gRequeueLeft > 0) {
        --gRequeueLeft;
        gRequeueMgr->mPending.push_back(gRequeueNext++ % uint32_t(gRequeueMgr->mPool.size()));
    }
}
void __fastcall StandReattach(void *effect, void *)
{
    Ev({evReattach, EffectId(effect)});
}
bool __cdecl StandMatch(const char *name, const char *filter, int32_t flag)
{
    Ev({evMatch, uint32_t(std::strlen(name)), uint32_t(std::strlen(filter)), uint32_t(flag)});
    return std::strstr(name, filter) != nullptr;
}
void __fastcall StandReloadEffects(void *manager, void *)
{
    Ev({evReloadEffects, Off(manager)});
}
void __fastcall StandUpdate(void *manager, void *, float a, float b)
{
    uint32_t wa, wb;
    std::memcpy(&wa, &a, 4);
    std::memcpy(&wb, &b, 4);
    Ev({evUpdate, Off(manager), wa, wb});
}

// ---- stand-ins of the lifecycle members' callees (Init, Update, creation, listing, loading) ----

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

enum : uint32_t {
    evNewEffect = 0xc8000000,
    evDescribe = 0xc9000000,
    evDone = 0xca000000,
    evOverlay = 0xcb000000,
    evPath = 0xcc000000,
    evWalk = 0xcd000000,
    evFramework = 0xce000000,
};

uint32_t gDescWords[16];           // the description flag words the effects' +0x40 pointers lead to
std::vector<EffectFake *> gMade;   // the effects the pool made (400 bytes each, freed by the test)
uint32_t gMadeNext = 0x400;
std::vector<Fake *> gOverlays;     // the overlay objects Init made (0x78-byte blocks, freed by the test)
uint32_t gOverlayNext = 0x320;
Mgr *gMgr = nullptr;               // the manager under test (the describe stand-in reads its effect table)

// What every effect of the pool is: the fake's vtables, the fields the members read (the count at +0x10, a valid name string
// at +0x14, the index -1, the flag bytes, the description pointer, the flag bits) from a generator seeded by the id.
void InitPoolEffect(EffectFake *e)
{
    std::mt19937 r(gMadeNext * 2654435761u + 5);
    for (uint8_t &b : e->body) b = uint8_t(r());
    e->vt = kEffectVT.slots;
    e->vt4 = kEffectVT.face;
    auto at = [&](uint32_t offset) { return e->body + offset - 8; };
    *reinterpret_cast<uint32_t *>(at(0x10)) = 1 + r() % 2;
    ::new (at(0x14)) std::string();
    *reinterpret_cast<int32_t *>(at(0x34)) = -1;
    *at(0x38) = uint8_t(r() % 3 == 0);
    *at(0x3a) = uint8_t(r() % 2);
    *at(0x3b) = uint8_t(r() % 4 != 0);
    *reinterpret_cast<uint32_t **>(at(0x40)) = &gDescWords[r() % 16];
    *reinterpret_cast<uint32_t *>(at(0x44)) = r() % 4;
    e->Id() = gMadeNext++;
    e->Refs() = 0;
    gMade.push_back(e);
    Ev({evNewEffect, e->Id()});
}
// Ours: the pool's new effect (Mgr::NewVisualEffect, patched). The original: operator new(400) in its own code, then the
// constructor 0xbe86a0 (patched).
cITSVisualEffect *StandNewEffect()
{
    auto *e = static_cast<EffectFake *>(::operator new(sizeof(EffectFake)));
    InitPoolEffect(e);
    return reinterpret_cast<cITSVisualEffect *>(e);
}
void *__fastcall StandEffectCtor(EffectFake *e, void *)
{
    InitPoolEffect(e);
    return e;
}
// 0xbe9670 (thiscall, the effect and its description entry): the effect described from entry `index` of the table.
void __fastcall StandDescribe(EffectFake *e, void *, const EffectEntry *entry)
{
    const int32_t index = int32_t(entry - gMgr->mEffects.data());
    Ev({evDescribe, e->Id(), uint32_t(index), entry->mVariant});
    e->body[0x3b - 8] = 1;
    e->body[0x38 - 8] = uint8_t(index % 5 == 0);
    *reinterpret_cast<uint32_t **>(e->body + 0x40 - 8) = &gDescWords[uint32_t(index) % 16];
}
// 0xbe8de0 (thiscall): whether the effect is done: a bit of its body.
bool __fastcall StandDone(EffectFake *e, void *)
{
    Ev({evDone, e->Id()});
    return (e->body[0x48 - 8] & 1) != 0;
}
// 0xbd9fd0 (thiscall on a 0x78-byte block): an overlay manager: a fake in the block.
void *__fastcall StandOverlay(Fake *self, void *)
{
    *self = Fake{kVT.overlay.slots, gOverlayNext, 50, {}};
    Ev({evOverlay, gOverlayNext});
    ++gOverlayNext;
    gOverlays.push_back(self);
    return self;
}
bool __cdecl StandPathExists(const cIGZString &path)
{
    const auto &p = reinterpret_cast<const fx::PathString &>(path);
    const uint32_t h = TextWords(p.mText.c_str());
    Ev({evPath, h});
    return (h & 4) != 0;
}
uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *object)
{
    Ev({evWalk | 5, Off(self), arg - uint32_t(uintptr_t(gBase)), Off(object)});
    for (uint32_t id : *static_cast<std::set<uint32_t> *>(self)) Ev({id});
    return 0;
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *object)
{
    Ev({evWalk | 6, Off(self), arg - uint32_t(uintptr_t(gBase)), Off(object)});
    for (uint32_t id : *static_cast<std::set<uint32_t> *>(self)) Ev({id});
}
void *__cdecl StandFrameworkLife()
{
    Ev({evFramework});
    return gCreatorFake;
}

// The effects the test's World and the pool made, and the overlays: freed after the recorder stops.
void FreeMade()
{
    for (EffectFake *e : gMade) {
        reinterpret_cast<std::string *>(e->body + 0x14 - 8)->~basic_string();
        ::operator delete(e);
    }
    gMade.clear();
    for (Fake *o : gOverlays) ::operator delete(o);
    gOverlays.clear();
    gMadeNext = 0x400;
    gOverlayNext = 0x320;
}

// The option globals (0x107d500..0x107d588: a vtable word and a value for each option): saved and restored by the test; the
// values random (cells: floats 510 51c 534 564, words 528 540 54c, bytes 558 570 57c).
struct OptionCells {
    uint8_t saved[0x88];
    OptionCells() { std::memcpy(saved, GameAddress<void>(0x107d500), sizeof(saved)); }
    ~OptionCells() { std::memcpy(GameAddress<void>(0x107d500), saved, sizeof(saved)); }
    OptionCells(const OptionCells &) = delete;
    static void Randomise(std::mt19937 &r)
    {
        auto real = [&](float lo, float span) { return lo + span * float(r() % 1000) / 1000.0f; };
        *GameAddress<float>(0x107d510) = real(0.5f, 3.0f);
        *GameAddress<float>(0x107d51c) = r() % 5 ? real(0.0f, 2.0f) : 0.0f;
        *GameAddress<float>(0x107d534) = real(0.0f, 1.0f);
        *GameAddress<float>(0x107d564) = r() % 3 ? real(0.0f, 0.25f) : -real(0.0f, 0.25f);
        *GameAddress<uint32_t>(0x107d528) = r() % 8 ? r() % 2000 : 0x80000000u + r() % 50;
        *GameAddress<uint32_t>(0x107d540) = r() % 6 ? r() % 8 : 0x80000000u + r() % 50;
        *GameAddress<int32_t>(0x107d54c) = r() % 5 ? int32_t(r() % 6) : -int32_t(r() % 3);
        *GameAddress<uint8_t>(0x107d558) = uint8_t(r() % 2);
        *GameAddress<uint8_t>(0x107d570) = uint8_t(r() % 3 != 0);
        *GameAddress<uint8_t>(0x107d57c) = uint8_t(r() % 3 != 0);
    }
};
void PushOptions(Blob &b)
{
    for (uint32_t o = 0x107d500; o < 0x107d588; o += 4) b.push_back(*GameAddress<uint32_t>(o));
}

// The body our vtable holds at `slot` of the face at +0 (a virtual member's code, for a stand-in on our side).
void *OurSlot(uint32_t slot)
{
    alignas(8) static uint8_t block[sizeof(Mgr)];
    // The constructor registers the cheats through StandCheatsA, already patched in by then: gCheats would still point
    // into an earlier test's World (a dead stack frame), so it reads null here (no references taken).
    Fake *saved[3];
    std::memcpy(saved, gCheats, sizeof(saved));
    std::memset(gCheats, 0, sizeof(gCheats));
    Mgr *m = ::new (block) Mgr;
    void *code = (*reinterpret_cast<void *const *const *>(m))[slot];
    m->Mgr::~Mgr();
    std::memcpy(gCheats, saved, sizeof(saved));
    gEv.clear();
    return code;
}

struct StandIns {
    shape::StandIn timerCtor{0x9ccf32u, &cRZCallbackTimer::Construct, &StandTimerCtor};
    shape::StandIn timerDtor{0x9ccfa9u, &cRZCallbackTimer::Destruct, &StandTimerDtor};
    shape::StandIn cheatsA{0xbc5aa0u, &MakeEffectCheats, &StandCheatsA};
    shape::StandIn cheatsB{0xbdb700u, &MakeOverlayCheat, &StandCheatsB};
    shape::StandIn server{0x9c5a89u, &GetCachedMessageServer, &StandServer};
    shape::StandIn apply{0xbcb4c0u, &Mgr::ApplyDetail, &StandApply};
    shape::StandIn reattach{0xbead40u, &cTSVisualEffect::Follow, &StandReattach};
    shape::StandIn match{0x9ccea3u, &RZWildcard::MatchCase, &StandMatch};
    shape::StandIn reload{0xbc9e20u, &Mgr::ApplyParserState, &StandReloadEffects};
    shape::StandIn update{0xbc6fe0u, OurSlot(19), &StandUpdate}; // UpdateMs's callee (Update has its own test below)
};

template <typename T> struct Override {
    T *at;
    T saved;
    Override(uint32_t rva, T value) : at(GameAddress<T>(rva)), saved(*at) { *at = value; }
    ~Override() { *at = saved; }
    Override(const Override &) = delete;
};

// ---- a side: the manager and its fakes ----

struct World {
    alignas(8) uint8_t buffer[sizeof(Mgr)];
    Fake unknowns[24];
    EffectFake effects[24];
    uint32_t effectsUsed = 0;
    Fake parser, server, framework, resourceManager, resources[8], debugService, debugObject, output;
    Fake view, text, creator, settings, cheatsService, effectsService, debugSize, camera, message;
    Renderer renderer;
    Mgr *obj = nullptr;
    int mode = 0;

    void Build(difftest::Context &ctx, int m, uint32_t seed)
    {
        mode = m;
        std::mt19937 r(seed * 7u + 1);
        MakeFakes(r);
        for (uint8_t &b : buffer) b = uint8_t(r());
        gBase = buffer;
        if (mode == 1)
            obj = ::new (buffer) Mgr;
        else
            obj = ctx.Original<Mgr *(__thiscall *)(void *)>(0xbc4870)(buffer);
        // What Init (not ours yet) would set: the parser and the renderer, two counted references.
        obj->mParser.Assign(reinterpret_cast<cIGZUnknown *>(&parser));
        if (r() % 4 != 0) obj->mRenderer.mObject = &renderer;
        obj->mRefBCC.Assign(reinterpret_cast<cIGZUnknown *>(&unknowns[r() % 20]));
        obj->mRefC18.Assign(reinterpret_cast<cIGZUnknown *>(&unknowns[r() % 20]));
        obj->mRefCount = 10;
    }
    void MakeFakes(std::mt19937 &r)
    {
        gFakesBegin = reinterpret_cast<const uint8_t *>(this);
        gFakesEnd = gFakesBegin + sizeof(*this);
        for (uint32_t i = 0; i < 24; ++i) unknowns[i] = {kVT.unknown.slots, 0x100 + i, 50, {}};
        for (uint32_t i = 0; i < 24; ++i) {
            EffectFake &e = effects[i];
            for (uint8_t &b : e.body) b = uint8_t(r());
            e.vt = kEffectVT.slots;
            e.vt4 = kEffectVT.face;
            // +0x34 effect index, +0x38 / +0x3a / +0x3b flags bytes, +0x44 flag bits (biased so that each branch is taken)
            *reinterpret_cast<int32_t *>(e.body + 0x34 - 8) = r() % 4 ? int32_t(r() % 6) : -1;
            e.body[0x38 - 8] = uint8_t(r() % 3 == 0);
            e.body[0x3a - 8] = uint8_t(r() % 2);
            e.body[0x3b - 8] = uint8_t(r() % 4 != 0);
            *reinterpret_cast<uint32_t *>(e.body + 0x44 - 8) = r() % 4;
            e.Id() = 0x200 + i;
            e.Refs() = 50;
        }
        parser = {kVT.parser.slots, 0x300, 50, {r() % 3 != 0}};
        server = {kVT.server.slots, 0x302, 50, {}};
        framework = {kVT.framework.slots, 0x303, 50, {}};
        resourceManager = {kVT.resourceManager.slots, 0x304, 50, {}};
        for (uint32_t i = 0; i < 8; ++i) resources[i] = {kVT.resource.slots, 0x310 + i, 50, {}};
        debugService = {kVT.debugService.slots, 0x305, 50, {r() % 4 != 0}};
        debugObject = {kVT.debugObject.slots, 0x306, 50, {0, r() % 5, r(), r() % 100}};
        output = {kVT.output.slots, 0x307, 50, {}};
        auto word = [&](uint32_t k) { return std::bit_cast<uint32_t>(float(int32_t(r() % 2001) - 1000) / 8.0f + float(k)); };
        view = {kVT.view.slots, 0x330, 50, {word(0), word(1), word(2), word(3), word(4), 0}};
        text = {kVT.text.slots, 0x331, 50, {r() % 4, 0, 0, 0, 0, 0}};
        creator = {kVT.creator.slots, 0x332, 50, {}};
        settings = {kVT.settings.slots, 0x333, 50, {}};
        cheatsService = {kVT.cheats.slots, 0x334, 50, {}};
        effectsService = {kVT.effectsService.slots, 0x335, 50, {}};
        debugSize = {kVT.unknown.slots, 0x336, 50, {r() % 2000, r() % 1500, 0, 0, 0, 0}};
        camera = {kVT.unknown.slots, 0x337, 50, {0, 0, 0, word(5), word(6), word(7)}};
        message = {kVT.message.slots, 0x338, 50, {}};
        gViewObject = r() % 5 ? &view : nullptr;
        gTextObject = &text;
        gCreatorFake = &creator;
        gParserFake = &parser;
        gRendererObject = reinterpret_cast<Fake *>(&renderer);
        gRendererBase = &renderer.base;
        gEffectsService = &effectsService;
        gCheatsService = &cheatsService;
        gSettingsService = &settings;
        gDebugSizeService = &debugSize;
        gMapsFake = &view;
        renderer = Renderer{};
        gServer = &server;
        gServerOn = r() % 4 != 0;
        gResourceManager = &resourceManager;
        gDebugService = &debugService;
        gDebugObject = &debugObject;
        for (uint32_t i = 0; i < 8; ++i) gResources[i] = &resources[i];
        for (uint32_t i = 0; i < 3; ++i) gCheats[i] = &unknowns[21 + i];
    }
    void Destroy(difftest::Context &ctx)
    {
        if (mode == 0)
            ctx.Original<void(__thiscall *)(void *)>(0xbc5500)(obj);
        else if (mode == 1)
            obj->Mgr::~Mgr();
        else
            A::Destruct(obj, nullptr);
        obj = nullptr;
    }
    cIGZUnknown *Unknown(uint32_t i) { return reinterpret_cast<cIGZUnknown *>(&unknowns[i % 24]); }
};

uint32_t IdOf(const void *p)
{
    return Off(p);
}

// ---- the dump ----

// The bytes the constructor leaves as they were (padding and +0xae0..+0xaf7): compared where both sides start from the same
// block, masked where the block comes from operator new (Create).
bool gMaskUnset = false;
bool Unset(uint32_t o)
{
    return (o >= 0x7d && o < 0x80) || o == 0x8f || o == 0xa5a || o == 0xa5b || (o >= 0xae0 && o < 0xaf8) || o == 0xb53;
}
void PushRaw(Blob &b, const Mgr *m, uint32_t from, uint32_t to)
{
    for (uint32_t o = from; o < to; o += 4) {
        uint8_t bytes[4];
        std::memcpy(bytes, reinterpret_cast<const uint8_t *>(m) + o, 4);
        for (uint32_t k = 0; k < 4; ++k)
            if (gMaskUnset && Unset(o + k)) bytes[k] = 0;
        uint32_t w;
        std::memcpy(&w, bytes, 4);
        b.push_back(w);
    }
}
template <class V> void PushWords(Blob &b, const V &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
    for (const auto &x : v) b.push_back(uint32_t(x));
}
void PushPaths(Blob &b, const std::list<fx::PathString> &paths)
{
    b.push_back(uint32_t(paths.size()));
    for (const fx::PathString &p : paths) {
        b.push_back(uint32_t(p.mVTable == GameAddress<void>(cRZString::kVTable)));
        PushString(b, p.mText);
        b.push_back(p.mRefCount);
    }
}
void PushManager(Blob &b, const Mgr *m)
{
    PushCollection(b, static_cast<const cTSEffectsCollection *>(m));
    PushRaw(b, m, 0x7c, 0x90);
    for (uint32_t w : m->mRandom.mState) b.push_back(w);
    b.push_back(uint32_t(m->mRandom.mNext - m->mRandom.mState));
    b.push_back(uint32_t(m->mRandom.mLeft));
    PushRaw(b, m, 0xa58, 0xa6c);
    b.push_back(uint32_t(m->mMessageIds.size()));
    for (uint32_t id : m->mMessageIds) b.push_back(id);
    b.push_back(uint32_t(m->mPool.size()));
    b.push_back(uint32_t(m->mPool.capacity()));
    for (const auto &e : m->mPool) b.push_back(EffectId(e.Get()));
    PushWords(b, m->mPending);
    PushWords(b, m->mActive);
    PushWords(b, m->mFree);
    b.push_back(IdOf(m->mParser.Get()));
    b.push_back(m->mRenderer.mObject ? 0x52 : 0);
    PushPaths(b, m->mPathsA);
    PushPaths(b, m->mPathsB);
    PushRaw(b, m, 0xabc, 0xb58);
    PushRaw(b, m, 0xb58, 0xb80);
    PushHash(b, m->mAliases, [&](const auto &kv) { b.insert(b.end(), {kv.first.first, kv.first.second, kv.second}); });
    PushHash(b, m->mTriggers, [&](const auto &kv) { b.insert(b.end(), {kv.first, kv.second}); });
    b.push_back(uint32_t(m->mCheats.size()));
    b.push_back(uint32_t(m->mCheats.capacity()));
    for (const auto &c : m->mCheats) b.push_back(IdOf(c.Get()));
    b.push_back(IdOf(m->mRefBCC.Get()));
    b.push_back(IdOf(m->mSlotObjects[0].Get()));
    b.push_back(IdOf(m->mSlotObjects[1].Get()));
    PushHash(b, m->mNamedA, [&](const auto &kv) {
        PushString(b, kv.first);
        b.push_back(IdOf(kv.second.Get()));
    });
    PushHash(b, m->mNamedB, [&](const auto &kv) {
        PushString(b, kv.first);
        b.push_back(IdOf(kv.second.Get()));
    });
    b.push_back(IdOf(m->mRefC18.Get()));
    b.push_back(IdOf(m->mScene.Get()));
    b.push_back(IdOf(m->mCamera.Get()));
    PushRaw(b, m, 0xc24, 0xc2c);
}

// ---- calls ----

void *Face8(Mgr *m)
{
    return static_cast<cTSEffectsCollection *>(m);
}

// Slot `slot` of the face at +0: the original at `rva` (mode 0), our vtable (1), the adapter (2).
template <class R, class... Args>
R Invoke(difftest::Context &ctx, World &w, uint32_t rva, int slot, R(__fastcall *adapter)(Mgr *, void *, Args...), Args... args)
{
    if (w.mode == 0) return ctx.Original<R(__thiscall *)(void *, Args...)>(rva)(w.obj, args...);
    if (w.mode == 2) return adapter(w.obj, nullptr, args...);
    return CallSlot<R>(w.obj, slot, args...);
}
// A non-virtual member: the original at `rva`, our member, the adapter.
template <class R, class... Args, class Ours>
R InvokeMember(difftest::Context &ctx, World &w, uint32_t rva, Ours ours, R(__fastcall *adapter)(Mgr *, void *, Args...),
               Args... args)
{
    if (w.mode == 0) return ctx.Original<R(__thiscall *)(void *, Args...)>(rva)(w.obj, args...);
    if (w.mode == 2) return adapter(w.obj, nullptr, args...);
    return ours();
}

const char *MaybeName(std::mt19937 &r)
{
    const uint32_t k = r() % 8;
    return k == 0 ? nullptr : k == 1 ? "" : Name(r);
}

struct NameAndVariant {
    fx::PathString name;
    uint16_t variant;
};
static_assert(offsetof(NameAndVariant, variant) == 0x20);

constexpr uint32_t kOpCount = 35;

void Operate(difftest::Context &ctx, World &w, Blob &b, uint32_t op, std::mt19937 &r)
{
    Mgr *m = w.obj;
    // ReleaseAllEffects empties the pool but leaves the pending list (the original does too); an index past the pool would
    // make the next pass read freed memory on both sides, so the test drops such indices before each step (the same on
    // both sides, through the public containers).
    for (auto *list : {&m->mPending, &m->mActive, &m->mFree}) {
        const size_t pool = m->mPool.size();
        list->erase(std::remove_if(list->begin(), list->end(), [&](uint32_t i) { return i >= pool; }), list->end());
    }
    switch (op) {
    case 0: { // SetPaths
        const char *first = r() % 4 ? Name(r) : nullptr;
        const char *second = r() % 4 ? Name(r) : nullptr;
        Invoke<void, const char *, const char *>(ctx, w, 0xbca680, 5, &A::SetPaths, first, second);
        break;
    }
    case 1: { // AddPaths
        const char *first = r() % 4 ? Name(r) : nullptr;
        const char *second = r() % 4 ? Name(r) : nullptr;
        Invoke<void, const char *, const char *>(ctx, w, 0xbc5c10, 6, &A::AddPaths, first, second);
        break;
    }
    case 2: { // SetFlag / GetFlag
        const uint32_t bit = r() % 5 == 0 ? r() : r() % 4;
        const bool on = r() % 2;
        Invoke<void, uint32_t, bool>(ctx, w, 0xbca7c0, 7, &A::SetFlag, bit, on);
        const uint32_t query = r() % 5 == 0 ? r() : r() % 4;
        b.push_back(Invoke<bool, uint32_t>(ctx, w, 0xbc86c0, 8, &A::GetFlag, query));
        break;
    }
    case 3: { // effects into the collection (the collection's own AddEffect, verified; each side its own vtable)
        const char *name = Name(r);
        const EffectEntry e = RandomEffect(r);
        if (w.mode == 1)
            CallSlot<void>(Face8(m), 5, name, &e);
        else
            ctx.Original<void(__thiscall *)(void *, const char *, const EffectEntry *)>(0xbd7c00)(Face8(m), name, &e);
        break;
    }
    case 4: { // HasEffectVariant
        NameAndVariant key{fx::PathString(Name(r)), uint16_t(r() % 3 ? r() % 3 : r())};
        const void *arg = &key;
        if (r() % 8 == 0) arg = reinterpret_cast<uint8_t *>(m) + 0x64 - 4; // the scratch key itself (a cRZString view)
        b.push_back(Invoke<bool, const void *>(ctx, w, 0xbc8e20, 11, &A::HasEffectVariant, arg));
        break;
    }
    case 5: { // HasEffect
        const char *name = MaybeName(r);
        b.push_back(Invoke<bool, const char *>(ctx, w, 0xbc8f00, 12, &A::HasEffect, name));
        break;
    }
    case 6: { // SetState
        const int32_t state = int32_t(r() % 5);
        Invoke<void, int32_t>(ctx, w, 0xbca860, 15, &A::SetState, state);
        break;
    }
    case 7: { // SetMaxDetail
        const uint32_t k = r() % 6;
        const uint32_t detail = k < 3 ? r() % 8 : k == 3 ? r() : k == 4 ? 0x80000000u + r() % 16 : 5;
        Invoke<void, uint32_t>(ctx, w, 0xbca380, 16, &A::SetMaxDetail, detail);
        break;
    }
    case 8: { // SetDetail
        const uint32_t k = r() % 8;
        float detail = float(r() % 8000) / 1000.0f - 0.5f;
        if (k == 0) detail = 4.9999f;
        if (k == 1) detail = 5.0f;
        if (k == 2) detail = float(r() % 6);
        if (k == 3 && r() % 4 == 0) detail = std::numeric_limits<float>::quiet_NaN();
        Invoke<void, float>(ctx, w, 0xbca470, 17, &A::SetDetail, detail);
        break;
    }
    case 9: { // SetTimeMode, UpdateMs
        Invoke<void, int32_t>(ctx, w, 0xbca580, 18, &A::SetTimeMode, int32_t(r() % 3));
        const uint32_t a = r() % 3 ? r() % 100 : r(), c = r() % 3 ? r() % 100 : r();
        Invoke<void, uint32_t, uint32_t>(ctx, w, 0xbc6f80, 20, &A::UpdateMs, a, c);
        break;
    }
    case 10: { // the getters
        b.push_back(Off(Invoke<void *>(ctx, w, 0xbc8d80, 21, &A::GetParams)));
        if (w.mode != 2) b.push_back(Off(Invoke<void *>(ctx, w, 0xbc8d80, 22, &A::GetParams)));
        else b.push_back(Off(A::GetParams(m, nullptr)));
        b.push_back(IdOf(Invoke<void *>(ctx, w, 0xbc7340, 24, &A::GetBCC)));
        b.push_back(IdOf(Invoke<cIGZUnknown *>(ctx, w, 0xbca330, 42, &A::GetScene)));
        b.push_back(IdOf(Invoke<cIGZUnknown *>(ctx, w, 0xbcbde0, 43, &A::GetCamera)));
        const int32_t index = int32_t(r() % 2);
        b.push_back(IdOf(Invoke<cIGZUnknown *, int32_t>(ctx, w, 0xbc9e10, 26, &A::GetSlotObject, index)));
        void *base = Invoke<void *>(ctx, w, 0xbc9e80, 33, &A::GetRendererBase);
        b.push_back(base ? uint32_t(static_cast<uint8_t *>(base) - reinterpret_cast<uint8_t *>(&w.renderer)) : 0xffffffffu);
        // Slots 23 and 34 (folded bodies): through each side's own vtable.
        if (w.mode == 1) {
            b.push_back(uint32_t(CallSlot<int32_t>(m, 23)));
            b.push_back(CallSlot<uint32_t>(m, 34));
        } else {
            b.push_back(uint32_t(ctx.Original<int32_t(__thiscall *)(void *)>(0x238f10)(m)));
            b.push_back(ctx.Original<uint32_t(__thiscall *)(void *)>(0x148ba0)(m));
        }
        break;
    }
    case 11: { // SetSlotObject
        const int32_t index = int32_t(r() % 2);
        cIGZUnknown *object = r() % 4 ? w.Unknown(r() % 6) : nullptr;
        Invoke<void, int32_t, cIGZUnknown *>(ctx, w, 0xbca1d0, 25, &A::SetSlotObject, index, object);
        break;
    }
    case 12: { // SetNamedA / SetNamedB
        const char *name = Name(r);
        cIGZUnknown *object = r() % 5 ? w.Unknown(r() % 8) : nullptr;
        if (r() % 2)
            Invoke<void, const char *, cIGZUnknown *>(ctx, w, 0xbc9f10, 27, &A::SetNamedA, name, object);
        else
            Invoke<void, const char *, cIGZUnknown *>(ctx, w, 0xbca070, 29, &A::SetNamedB, name, object);
        break;
    }
    case 13: { // GetNamedA / GetNamedB
        const char *name = Name(r);
        b.push_back(IdOf(Invoke<cIGZUnknown *, const char *>(ctx, w, 0xbc5df0, 28, &A::GetNamedA, name)));
        b.push_back(IdOf(Invoke<cIGZUnknown *, const char *>(ctx, w, 0xbc5f20, 30, &A::GetNamedB, Name(r))));
        break;
    }
    case 14: { // debug bits (SetDebugBit writes the wanted bits; the applied ones are set here as Update would)
        const uint32_t bit = r() % 6 == 0 ? 32 + r() % 40 : r() % 32;
        const bool on = r() % 2;
        Invoke<void, uint32_t, bool>(ctx, w, 0xbca340, 31, &A::SetDebugBit, bit, on);
        if (r() % 4 == 0) m->mDebugBits = m->mWantedDebugBits ^ (r() % 4);
        const uint32_t query = r() % 6 == 0 ? 32 + r() % 40 : r() % 32;
        b.push_back(Invoke<bool, uint32_t>(ctx, w, 0xbc5d20, 32, &A::GetDebugBit, query));
        break;
    }
    case 15: { // PrintStats (counters set first, as Update would)
        for (uint32_t &c : m->mCounters) c = r() % 1000;
        m->mWordB00 = r() % 50;
        Invoke<void, void *>(ctx, w, 0xbc8cb0, 35, &A::PrintStats, &w.output);
        break;
    }
    case 16: { // CopyEffectNames
        char area[16 * 20];
        for (size_t i = 0; i < sizeof(area); ++i) area[i] = char(0x60 + i % 23);
        char *buffer = area;
        const int32_t count = int32_t(r() % 6);
        const int32_t size = int32_t(2 + r() % 18);
        const char *filter = r() % 3 == 0 ? (r() % 2 ? "e" : "a_") : nullptr;
        b.push_back(uint32_t(Invoke<int32_t, char **, int32_t, int32_t, const char *>(ctx, w, 0xbc8c20, 38, &A::CopyEffectNames,
                                                                                    &buffer, count, size, filter)));
        b.push_back(uint32_t(buffer - area));
        for (size_t i = 0; i < sizeof(area); i += 4) {
            uint32_t word;
            std::memcpy(&word, area + i, 4);
            b.push_back(word);
        }
        break;
    }
    case 17: { // SetScene
        const uint32_t k = r() % 4;
        cIGZUnknown *scene = k == 0 ? nullptr : k == 1 ? m->mScene.Get() : w.Unknown(10 + r() % 3);
        Invoke<void, cIGZUnknown *>(ctx, w, 0xbca810, 39, &A::SetScene, scene);
        break;
    }
    case 18: { // SetCamera / SetCameraAndRefresh
        const uint32_t k = r() % 4;
        cIGZUnknown *camera = k == 0 ? nullptr : k == 1 ? m->mCamera.Get() : w.Unknown(13 + r() % 3);
        if (r() % 2)
            Invoke<void, cIGZUnknown *>(ctx, w, 0xbca920, 40, &A::SetCamera, camera);
        else
            Invoke<void, cIGZUnknown *>(ctx, w, 0xbca960, 41, &A::SetCameraAndRefresh, camera);
        break;
    }
    case 19: { // GetDebugValue
        Override<uint8_t> setting(0x107d57c, uint8_t(r() % 3 ? 1 : 0));
        if (r() % 6 == 0) m->mDebugValue[0] = -1;
        b.push_back(uint32_t(Invoke<int32_t>(ctx, w, 0xbca250, 44, &A::GetDebugValue)));
        break;
    }
    case 20: // Clear (the collection face's slot 3)
        if (w.mode == 0)
            ctx.Original<void(__thiscall *)(void *)>(0xbc5d50)(Face8(m));
        else if (w.mode == 1)
            CallSlot<void>(Face8(m), 3);
        else
            A::Clear(static_cast<cTSEffectsCollection *>(m), nullptr);
        break;
    case 21: // Link (slot 4)
        if (w.mode == 0)
            ctx.Original<void(__thiscall *)(void *)>(0xbc73d0)(Face8(m));
        else if (w.mode == 1)
            CallSlot<void>(Face8(m), 4);
        else
            A::Link(static_cast<cTSEffectsCollection *>(m), nullptr);
        break;
    case 22: { // message aliases and triggers into the collection (its own verified slots 9 / 10)
        const char *name = Name(r);
        const uint32_t first = r() % 4, second = r() % 3;
        const EffectMessageTrigger trigger{r() % 6, Name(r)};
        if (w.mode == 1) {
            CallSlot<void>(Face8(m), 9, name, first, second);
            CallSlot<void>(Face8(m), 10, &trigger);
        } else {
            ctx.Original<void(__thiscall *)(void *, const char *, uint32_t, uint32_t)>(0xbd7b40)(Face8(m), name, first, second);
            ctx.Original<void(__thiscall *)(void *, const EffectMessageTrigger *)>(0xbd7ef0)(Face8(m), &trigger);
        }
        break;
    }
    case 23: // ReleaseAllEffects
        InvokeMember<void>(ctx, w, 0xbcb1a0, [&] { m->ReleaseAllEffects(); }, &A::ReleaseAllEffects);
        break;
    case 24: // RefreshDetail
        InvokeMember<void>(ctx, w, 0xbcb210, [&] { m->RefreshDetail(); }, &A::RefreshDetail);
        break;
    case 25: // FlushPending
        m->mNoFlush = r() % 5 == 0;
        InvokeMember<void>(ctx, w, 0xbc59d0, [&] { m->FlushPending(); }, &A::FlushPending);
        break;
    case 26: // Reload
        w.parser.value[0] = r() % 3 != 0;
        if (r() % 6 == 0) m->mParser.Assign(nullptr);
        // A successful reload reaches the renderer unchecked (the game always has one by then).
        if (!m->mRenderer.mObject && m->mParser.Get() && w.parser.value[0]) break;
        InvokeMember<void>(ctx, w, 0xbc6670, [&] { m->Reload(); }, &A::Reload);
        break;
    case 27: { // ParseResources
        uint32_t keys[6][4];
        for (auto &k : keys)
            for (uint32_t &x : k) x = r() % 8;
        struct {
            const void *first, *last, *end;
        } vector{keys, keys + r() % 7, keys + 6};
        Invoke<void, const void *>(ctx, w, 0xbc5b20, 10, &A::ParseResources, &vector);
        break;
    }
    case 28: { // pool setup (what CreateVisualEffect, not ours yet, would do): effects into the pool and the lists
        const uint32_t n = 1 + r() % 3;
        for (uint32_t i = 0; i < n && w.effectsUsed < 24; ++i) {
            EffectFake &e = w.effects[w.effectsUsed++];
            const uint32_t index = uint32_t(m->mPool.size());
            m->mPool.push_back(Mgr::EffectRef(reinterpret_cast<cITSVisualEffect *>(&e)));
            const uint32_t k = r() % 3;
            (k == 0 ? m->mActive : k == 1 ? m->mFree : m->mPending).push_back(index);
        }
        break;
    }
    case 29: { // QueryInterface
        const uint32_t ids[] = {1, 0xc97cf5cd, 0x452294aa, 0xc97cf5ce, 0x12345678, 0xc97cf5cf};
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint8_t res = Invoke<bool, uint32_t, void **>(ctx, w, 0xbc9ea0, 0, &A::QueryInterface, ids[r() % 6], &out);
        b.insert(b.end(), {res, out == reinterpret_cast<void *>(0x5a5a5a5a) ? 0x5a : Off(out)});
        break;
    }
    case 30: { // HasEffectName (non-virtual, this = the collection face)
        std::string key = Name(r);
        if (r() % 2)
            for (char &ch : key) ch = char(tolower(ch));
        uint8_t res;
        if (w.mode == 0)
            res = ctx.Original<bool(__thiscall *)(void *, const std::string *)>(0xbc8d90)(Face8(m), &key);
        else if (w.mode == 1)
            res = m->HasEffectName(key);
        else
            res = A::HasEffectName(static_cast<cTSEffectsCollection *>(m), nullptr, key);
        b.push_back(res);
        break;
    }
    case 31: { // AddRef / Release (the count stays above 1 here)
        if (w.mode == 1)
            b.push_back(CallSlot<uint32_t>(m, 1));
        else
            b.push_back(ctx.Original<uint32_t(__thiscall *)(void *)>(0x207d70)(m));
        b.push_back(Invoke<uint32_t>(ctx, w, 0xbca210, 2, &A::Release));
        break;
    }
    case 32: { // the detail bias and flags SetDetail / SetMaxDetail read (as Init would set them)
        m->mDetailBias = float(r() % 5) * 0.5f;
        m->mMaxDetail = r() % 2;
        break;
    }
    case 33: { // SetPaths with long paths (past 15 characters)
        Invoke<void, const char *, const char *>(ctx, w, 0xbca680, 5, &A::SetPaths, "a_long_effect_name_past_sixteen",
                                                 r() % 2 ? "Exactly16chars__" : nullptr);
        break;
    }
    case 34: { // QueryInterface / AddRef / Release on the +4 and +8 faces through each side's own vtable (the adjustor
               // thunks 0xbc9ef7 / 0xbc5d08 / 0xbca231 on +4 and 0xbc9eff / 0xbca239 on +8 in the game's; the compiler's in ours)
        void *face = r() % 2 ? static_cast<void *>(static_cast<cIGZMessageTarget2 *>(m)) : Face8(m);
        const uint32_t ids[] = {1u, Mgr::kInterfaceId, Mgr::kMessageTargetId, cTSEffectsCollection::kInterfaceId, 0x12345678u};
        void *out = reinterpret_cast<void *>(0x1234);
        const bool ok = CallSlot<bool>(face, 0, ids[r() % 5], &out);
        b.insert(b.end(), {uint32_t(ok), out == reinterpret_cast<void *>(0x1234) ? 0x1234u
                                                                                 : uint32_t(static_cast<uint8_t *>(out) - reinterpret_cast<uint8_t *>(m))});
        b.push_back(CallSlot<uint32_t>(face, 1));
        b.push_back(CallSlot<uint32_t>(face, 2));
        if (ok) b.push_back(CallSlot<uint32_t>(face, 2));
        break;
    }
    }
}

constexpr uint32_t kWeights[kOpCount] = {2, 2, 2, 6, 3, 3, 3, 3, 4, 2, 2, 2, 3, 3, 2, 1, 2, 2, 2, 2, 1, 2, 3, 1, 2, 2, 1, 2, 4, 2, 2, 1, 1, 1, 2};

uint32_t PickOp(std::mt19937 &r)
{
    uint32_t total = 0;
    for (uint32_t w : kWeights) total += w;
    uint32_t x = r() % total;
    for (uint32_t op = 0;; ++op) {
        if (x < kWeights[op]) return op;
        x -= kWeights[op];
    }
}

void PushFakes(Blob &b, World &w)
{
    for (const Fake &f : w.unknowns) b.push_back(f.refs);
    for (EffectFake &e : w.effects) b.push_back(e.Refs());
    for (const Fake *f : {&w.parser, &w.renderer.base, &w.server, &w.framework, &w.resourceManager, &w.debugService,
                          &w.debugObject, &w.output, &w.view, &w.text, &w.creator, &w.settings, &w.cheatsService,
                          &w.effectsService, &w.debugSize, &w.camera, &w.message})
        b.push_back(f->refs);
    for (const EffectFake *e : gMade) b.push_back(const_cast<EffectFake *>(e)->Refs());
    for (const Fake *o : gOverlays) b.push_back(o->refs);
    for (const Fake &f : w.resources) b.push_back(f.refs);
}

// ---- the lifecycle members: Init, Shutdown, creation, listing, loading, messages, Update ----

struct LifeStands {
    shape::StandIn timerCtor{0x9ccf32u, &cRZCallbackTimer::Construct, &StandTimerCtor};
    shape::StandIn timerDtor{0x9ccfa9u, &cRZCallbackTimer::Destruct, &StandTimerDtor};
    shape::StandIn cheatsA{0xbc5aa0u, &MakeEffectCheats, &StandCheatsA};
    shape::StandIn cheatsB{0xbdb700u, &MakeOverlayCheat, &StandCheatsB};
    shape::StandIn server{0x9c5a89u, &GetCachedMessageServer, &StandServer};
    shape::StandIn apply{0xbcb4c0u, &Mgr::ApplyDetail, &StandApply};
    shape::CodePatch reattach{0xbead40u, shape::CodeOf(&StandReattach)};
    shape::StandIn match{0x9ccea3u, &RZWildcard::MatchCase, &StandMatch};
    shape::StandIn walk5{0x1c0458u, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5};
    shape::StandIn walk6{0x1c081eu, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6};
    shape::StandIn framework{0x1ae18du, &GetDirectorFrameworkObject, &StandFrameworkLife};
    shape::StandIn pathExists{0x1c1242u, &cRZFile::PathExists, &StandPathExists};
    shape::StandIn overlay{0xbd9fd0u, &cTSOverlayManager::Construct, &StandOverlay};
    shape::StandIn describe{0xbe9670u, &cTSVisualEffect::Describe, &StandDescribe};
    shape::StandIn done{0xbe8de0u, &cTSVisualEffect::IsDone, &StandDone};
    shape::CodePatch effectCtor{0xbe86a0u, shape::CodeOf(&StandEffectCtor)};
    shape::CodePatch newEffect{shape::CodeOf(&Mgr::NewVisualEffect), shape::CodeOf(&StandNewEffect)};
    // The image's operator new / delete (its CRT is not initialised here) go to the test's heap, like ours.
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

const char *const kEffectNames[] = {"smoke", "fire_a", "spark", "a_long_effect_name_past_sixteen", "water.drop", "x"};

// What the effects need that the common World does not give: counts, valid name strings, description words, and the
// resource manager's key words.
void PrepareLife(World &w, std::mt19937 &r)
{
    gMgr = w.obj;
    for (uint32_t &d : gDescWords) d = r();
    for (EffectFake &e : w.effects) {
        auto at = [&](uint32_t offset) { return e.body + offset - 8; };
        *reinterpret_cast<uint32_t *>(at(0x10)) = 1 + r() % 2;
        ::new (at(0x14)) std::string(r() % 3 ? kEffectNames[r() % 6] : "");
        *reinterpret_cast<int32_t *>(at(0x34)) = -1;
        *reinterpret_cast<uint32_t **>(at(0x40)) = &gDescWords[r() % 16];
    }
    for (uint32_t k = 1; k < 4; ++k) w.resourceManager.value[k] = r();
    w.resourceManager.value[5] = r() % 2;
}
void FinishLife(World &w)
{
    for (EffectFake &e : w.effects) reinterpret_cast<std::string *>(e.body + 0x14 - 8)->~basic_string();
    FreeMade();
    gMgr = nullptr;
}
// An effect's description index must be -1 or in the table (both sides index with it).
void ClampIndices(Mgr *m)
{
    const int32_t n = int32_t(m->mEffects.size());
    for (auto &e : m->mPool) {
        if (!e.Get()) continue;
        auto *fields = reinterpret_cast<uint8_t *>(e.Get());
        int32_t &index = *reinterpret_cast<int32_t *>(fields + 0x34);
        if (index >= n || index < -1) index = -1;
    }
}

constexpr uint32_t kLifeOps = 24;

void OperateLife(difftest::Context &ctx, World &w, Blob &b, uint32_t op, std::mt19937 &r)
{
    Mgr *m = w.obj;
    gServicesOn = r() % 6 != 0;
    for (auto *list : {&m->mPending, &m->mActive, &m->mFree}) {
        const size_t pool = m->mPool.size();
        list->erase(std::remove_if(list->begin(), list->end(), [&](uint32_t i) { return i >= pool || !m->mPool[i].Get(); }),
                    list->end());
    }
    ClampIndices(m);
    // A reload or a list with the renderer's names reaches the renderer unchecked (the game always has one by then).
    if (r() % 3 == 0) m->mFlags = (m->mFlags & ~3u) | (r() & 3u); // bit 1 (renderer names) and bit 0 apart
    if (!m->mRenderer.mObject) m->mFlags &= ~2u;
    const float times[] = {0.0f, 0.016f, 0.5f, 3.0f, -1.0f, 100.0f};
    switch (op) {
    case 0: { // effects, then Link
        const uint32_t n = 1 + r() % 3;
        for (uint32_t i = 0; i < n; ++i) {
            EffectEntry e;
            e.mFlags = r();
            e.mVariant = uint16_t(r() % 4); // 0 is a description no lookup uses
            for (uint32_t &p : e.mParams) p = r();
            CallSlot<void>(Face8(m), 5, kEffectNames[r() % 6], &e);
        }
        CallSlot<void>(Face8(m), 4);
        break;
    }
    case 1: { // aliases and triggers
        const char *name = kEffectNames[r() % 6];
        const uint32_t first = r() % 4, second = r() % 3;
        const EffectMessageTrigger trigger{r() % 6, kEffectNames[r() % 6]};
        CallSlot<void>(Face8(m), 9, name, first, second);
        CallSlot<void>(Face8(m), 10, &trigger);
        CallSlot<void>(Face8(m), 4);
        break;
    }
    case 2: { // effects into the pool and the lists
        const uint32_t n = 1 + r() % 3;
        for (uint32_t i = 0; i < n && w.effectsUsed < 24; ++i) {
            EffectFake &e = w.effects[w.effectsUsed++];
            const uint32_t index = uint32_t(m->mPool.size());
            m->mPool.push_back(Mgr::EffectRef(reinterpret_cast<cITSVisualEffect *>(&e)));
            const uint32_t k = r() % 3;
            (k == 0 ? m->mActive : k == 1 ? m->mFree : m->mPending).push_back(index);
            if (!m->mEffects.empty() && r() % 2) *reinterpret_cast<int32_t *>(e.body + 0x34 - 8) = int32_t(r() % m->mEffects.size());
        }
        break;
    }
    case 3: { // AcquireEffect
        auto *e = InvokeMember<cITSVisualEffect *>(ctx, w, 0xbc9ab0, [&] { return m->AcquireEffect(); }, &A::AcquireEffect);
        b.push_back(EffectId(e));
        break;
    }
    case 4: { // FindEffectIndex / FindAliasIndex
        if (r() % 2) {
            std::string key = kEffectNames[r() % 6];
            if (r() % 5 == 0) key = "missing";
            b.push_back(uint32_t(InvokeMember<int32_t, const std::string &>(
                ctx, w, 0xbc8590, [&] { return m->FindEffectIndex(key); }, &A::FindEffectIndex, key)));
        } else {
            const uint32_t first = r() % 4, second = r() % 3;
            b.push_back(uint32_t(InvokeMember<int32_t, uint32_t, uint32_t>(
                ctx, w, 0xbc8620, [&] { return m->FindAliasIndex(first, second); }, &A::FindAliasIndex, first, second)));
        }
        break;
    }
    case 5: { // CreateEffect
        const uint32_t first = r() % 4, second = r() % 3;
        void *out = nullptr;
        const bool wantOut = r() % 8 != 0;
        m->mState = r() % 7 == 0 ? 1 : 0;
        // A bool: the original leaves the rest of EAX as the last call left it.
        b.push_back(Invoke<bool, uint32_t, uint32_t, void **>(ctx, w, 0xbc61e0, 13, &A::CreateEffect, first, second,
                                                                  wantOut ? &out : nullptr));
        b.push_back(EffectId(out));
        if (out) // the pair and the index the effect was given (+0x2c, +0x30, +0x34)
            for (uint32_t offset : {0x2cu, 0x30u, 0x34u}) b.push_back(*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(out) + offset));
        if (out) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 6: { // CreateVisualEffect
        const char *name = r() % 8 == 0 ? nullptr : r() % 8 == 0 ? "" : r() % 6 == 0 ? "Missing_One" : kEffectNames[r() % 6];
        void *out = nullptr;
        const bool wantOut = r() % 8 != 0;
        m->mState = r() % 7 == 0 ? 1 : 0;
        b.push_back(Invoke<bool, const char *, void **>(ctx, w, 0xbc6360, 14, &A::CreateVisualEffect, name,
                                                            wantOut ? &out : nullptr));
        b.push_back(EffectId(out));
        if (out) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 7:
    case 8: { // ListActive / ListEffects
        std::vector<fx::EffectListEntry> list;
        list.reserve(32);
        const uint32_t before = r() % 3;
        for (uint32_t i = 0; i < before; ++i) list.emplace_back();
        const char *filters[] = {nullptr, "e", "a_", "", "smoke", "x"};
        const char *filter = filters[r() % 6];
        if (op == 7)
            Invoke<void, void *, const char *>(ctx, w, 0xbc8760, 36, &A::ListActive, &list, filter);
        else
            Invoke<void, void *, const char *>(ctx, w, 0xbc8900, 37, &A::ListEffects, &list, filter);
        b.push_back(uint32_t(list.size()));
        b.push_back(uint32_t(list.capacity()));
        for (const fx::EffectListEntry &e : list) {
            PushString(b, e.mName.mText);
            b.push_back(uint32_t(e.mIndex));
            b.push_back(e.mVariant);
            PushString(b, e.mResolved.mText);
            b.push_back(EffectId(e.mObject));
        }
        break;
    }
    case 9:
    case 10:
    case 11: { // DoMessage
        const uint32_t ids[] = {0, 1, 2, 3, 4, 5, 0x697d01ed, 0x122ced69, 0x322ced70, 0x522ced95, 0xb23b7d6f, 0xd23b7d7c,
                                0xb23b7d66, 0x6bf17c99, 0x6bf17c9a, 0x6bf17c9b, 0x6bf17c9c, 0x6bf17c9d, 0x6bf17c9e,
                                0x6bf17c9f, 0x6bf17ca0, 0x6bf17ca1, 0x6bf17ca2, 0x6bf17ca3, 0x12345678};
        uint32_t id = ids[r() % (sizeof(ids) / sizeof(ids[0]))];
        w.parser.value[0] = r() % 3 != 0;
        if (id == 0x697d01ed && !m->mRenderer.mObject && m->mParser.Get() && w.parser.value[0]) id = 0x122ced69;
        OptionCells::Randomise(r);
        w.message.value[0] = id;
        w.message.value[1] = r() % 5;
        w.message.value[2] = r() % 3; // 0: no data, 1: data, 2: has data but a null pointer
        w.message.value[4] = r();
        w.message.value[5] = r() % 4 ? r() % 7 : std::bit_cast<uint32_t>(0.75f);
        auto *face = static_cast<cIGZMessageTarget2 *>(m);
        // A message with a trigger asks the maps object for the view (the game has one from Init on).
        if (!m->mRefBCC.Get()) m->mRefBCC.Assign(w.Unknown(r() % 20));
        bool handled;
        if (w.mode == 0)
            handled = ctx.Original<bool(__thiscall *)(void *, void *)>(0xbc66d0)(face, &w.message);
        else if (w.mode == 1)
            handled = CallSlot<bool>(face, 3, &w.message);
        else
            handled = A::DoMessage(face, nullptr, &w.message);
        b.push_back(handled);
        PushOptions(b);
        break;
    }
    case 12:
    case 13: { // Update
        OptionCells::Randomise(r);
        m->mByteA58 = r() % 3 == 0;
        m->mMaxElapsed = r() % 2 ? times[r() % 6] : -times[r() % 6];
        m->mWantedDebugBits = r() % 3 ? m->mDebugBits ^ (1u << (r() % 12)) : m->mDebugBits;
        m->mTimeMode = r() % 3;
        m->mCamera.Assign(r() % 4 ? reinterpret_cast<cIGZUnknown *>(&w.camera) : nullptr);
        m->mRefC18.Assign(r() % 5 ? reinterpret_cast<cIGZUnknown *>(&w.debugSize) : nullptr);
        const float elapsed = times[r() % 6], sim = times[r() % 6];
        // Settings that switch the state to 1 release every effect (the pool and the active list go) but leave the pending
        // list; the game flushes pending effects at once, so a pending index over an empty pool is not a state to test.
        if (m->mByteA58 && *GameAddress<uint8_t>(0x107d570) == 0 && m->mState != 1) m->mPending.clear();
        Invoke<void, float, float>(ctx, w, 0xbc6fe0, 19, &A::Update, elapsed, sim);
        break;
    }
    case 14: // ApplySettings
        OptionCells::Randomise(r);
        if (r() % 2) m->mDebugValue[0] = int32_t(r() % 5);
        // Any state (the original leaves 2 and 3 alone), a scene so the detail reaches the active effects, and the mode word
        // either unchanged by the settings or not.
        if (r() % 2) m->mState = r() % 4;
        if (r() % 3 != 0 && !m->mScene.Get()) m->mScene.Assign(w.Unknown(10 + r() % 3));
        if (r() % 3 == 0) m->mWordA5C = *GameAddress<int32_t>(0x107d54c);
        else if (r() % 2) m->mWordA5C = int32_t(r() % 6);
        InvokeMember<void>(ctx, w, 0xbc6e10, [&] { m->ApplySettings(); }, &A::ApplySettings);
        break;
    case 15: // CounterBlend
        OptionCells::Randomise(r);
        m->mWordAFC = r() % 4 ? r() % 3000 : 0x80000000u + r() % 9;
        if (r() % 4 == 0) m->mWordAFC = *GameAddress<uint32_t>(0x107d528); // the measured count at the target
        m->mParams[0] = std::bit_cast<float>(Bits(times[r() % 6]));
        InvokeMember<void>(ctx, w, 0xbcb3f0, [&] { m->CounterBlend(); }, &A::CounterBlend);
        b.push_back(Bits(m->mParams[0]));
        break;
    case 16: // ActivityWalk
        InvokeMember<void>(ctx, w, 0xbc6560, [&] { m->ActivityWalk(); }, &A::ActivityWalk);
        break;
    case 17: // OnTimer
        if (w.mode == 0)
            ctx.Original<void(__cdecl *)(void *)>(0xbc7350)(nullptr);
        else
            Mgr::OnTimer(nullptr);
        break;
    case 18: { // SendText
        const char *text = r() % 4 == 0 ? nullptr : kTexts[r() % 4];
        if (w.mode == 0)
            ctx.Original<void(__stdcall *)(const char *)>(0xbca590)(text);
        else
            Mgr::SendText(text);
        break;
    }
    case 19: { // LoadEffects
        if (!m->mRenderer.mObject || !m->mParser.Get()) break;
        gKeyCount = r() % 6;
        w.parser.value[1] = r() % 2;
        if (r() % 2) m->SetPaths(kEffectNames[r() % 6], r() % 2 ? kEffectNames[r() % 6] : nullptr);
        Invoke<void>(ctx, w, 0xbc9840, 9, &A::LoadEffects);
        gKeyCount = 0;
        break;
    }
    case 20:
    case 21: { // Init (the maps object stays out of the later steps)
        b.push_back(Invoke<bool>(ctx, w, 0xbc8fa0, 3, &A::Init));
        // The plane collider Init makes: its two vtables and its count, read back through the name.
        if (const auto *plane = reinterpret_cast<const uint32_t *>(m->GetNamedA("plane")))
            for (uint32_t i = 0; i < 3; ++i) b.push_back(plane[i]);
        if (m->mRefBCC.Get() && !(m->mRefBCC.Get() >= reinterpret_cast<cIGZUnknown *>(&w.unknowns[0]) &&
                                  m->mRefBCC.Get() < reinterpret_cast<cIGZUnknown *>(&w.unknowns[24])))
            m->mRefBCC.Assign(w.Unknown(r() % 20));
        break;
    }
    case 22: // Shutdown
        b.push_back(Invoke<bool>(ctx, w, 0xbcaa00, 4, &A::Shutdown));
        break;
    case 23: { // Reload with the real ApplyParserState
        w.parser.value[0] = r() % 3 != 0;
        if (!m->mRenderer.mObject && m->mParser.Get() && w.parser.value[0]) break;
        InvokeMember<void>(ctx, w, 0xbc6670, [&] { m->Reload(); }, &A::Reload);
        break;
    }
    }
}
constexpr uint32_t kLifeWeights[kLifeOps] = {3, 2, 3, 2, 2, 4, 5, 2, 2, 4, 3, 2, 4, 3, 2, 2, 3, 2, 2, 2, 1, 1, 1, 1};

uint32_t PickLife(std::mt19937 &r)
{
    uint32_t total = 0;
    for (uint32_t w : kLifeWeights) total += w;
    uint32_t x = r() % total;
    for (uint32_t op = 0;; ++op) {
        if (x < kLifeWeights[op]) return op;
        x -= kLifeWeights[op];
    }
}

} // namespace

// The lifecycle members (Init, Shutdown, LoadEffects, CreateEffect, CreateVisualEffect, Update, ListActive, ListEffects,
// DoMessage, OnTimer, SendText and their helpers): random operations over a manager whose services are fakes; three modes.
DIFF_TEST(EffectsManager_Lifecycle, 0xbc8fa0)
{
    svcfx::AllocatorScope allocator;
    LifeStands stands;
    gTimerCallbackImage = GameAddress<void>(0xbc7350);
    gMaskUnset = false;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            OptionCells cells;
            Override<void *> frameworkGlobal(0x10c9378, nullptr);
            World w;
            *GameAddress<void *>(0x10c9378) = &w.framework;
            gEv.clear();
            w.Build(ctx, mode, seed);
            std::mt19937 r(seed * 1000003u + 29);
            PrepareLife(w, r);
            OptionCells::Randomise(r);
            Blob &b = blobs[mode];
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
            PushManager(b, w.obj);
            const uint32_t steps = 30 + seed * 3;
            for (uint32_t step = 0; step < steps; ++step) {
                const uint32_t op = PickLife(r);
                b.push_back(0x0b000000u | op);
                OperateLife(ctx, w, b, op, r);
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushManager(b, w.obj);
            }
            gServicesOn = true;
            difftest::FreeRecorder rec;
            w.Destroy(ctx);
            rec.Stop();
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
            PushFakes(b, w);
            FinishLife(w);
            gBase = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Lifecycle (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "Lifecycle (adapters)", seed);
    }
}

// The constructor, random operations over every reimplemented slot and member, the destructor; three modes compared.
DIFF_TEST(EffectsManager_Operations, 0xbc4870)
{
    StandIns stands;
    gMaskUnset = false;
    for (uint32_t seed = 0; seed < 50; ++seed) {
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            Override<void *> frameworkGlobal(0x10c9378, nullptr);
            World w;
            *GameAddress<void *>(0x10c9378) = &w.framework;
            gEv.clear();
            w.Build(ctx, mode, seed);
            Blob &b = blobs[mode];
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
            PushManager(b, w.obj);
            std::mt19937 r(seed * 1000003u + 17);
            const uint32_t steps = 30 + seed * 4;
            for (uint32_t step = 0; step < steps; ++step) {
                const uint32_t op = PickOp(r);
                b.push_back(0x0b000000u | op);
                Operate(ctx, w, b, op, r);
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushManager(b, w.obj);
            }
            difftest::FreeRecorder rec;
            w.Destroy(ctx);
            rec.Stop();
            b.insert(b.end(), gEv.begin(), gEv.end());
            size_t sizes[512];
            const size_t n = rec.SortedSizes(sizes, 512);
            b.push_back(uint32_t(n));
            for (size_t i = 0; i < n && i < 512; ++i) b.push_back(uint32_t(sizes[i]));
            PushFakes(b, w);
            gBase = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Operations (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "Operations (adapters)", seed);
    }
}

// The factory 0xbc86f0, the Construct adapter, Release down to deletion (0xbca210 with a count of 1: the deleting destructor
// through the collection face's slot 12), and that slot directly (0xbc5970 behind the thunk 0xbc591d) with flags 1 and 0.
DIFF_TEST(EffectsManager_CreateDelete, 0xbc86f0)
{
    StandIns stands;
    // Pass 0 is thrown away: the first run in a process frees one block more on whichever side goes first (first-call state in
    // the image, not this class's), which a test before this one hides in a full run and a filtered run shows.
    for (uint32_t pass = 0; pass <= 24; ++pass) {
        const uint32_t seed = pass == 0 ? 0 : pass - 1;
        Blob blobs[3];
        for (int mode = 0; mode < 3; ++mode) {
            Blob &b = blobs[mode];
            Override<void *> frameworkGlobal(0x10c9378, nullptr);
            World w;
            *GameAddress<void *>(0x10c9378) = &w.framework;
            std::mt19937 r(seed);
            w.mode = mode;
            w.MakeFakes(r);
            gEv.clear();
            Mgr *m;
            void *block = nullptr;
            const uint32_t how = seed % 3;
            if (how == 2) {
                block = ::operator new(sizeof(Mgr));
                std::memset(block, 0x5c, sizeof(Mgr));
            }
            if (mode == 0)
                m = how == 2 ? ctx.Original<Mgr *(__thiscall *)(void *)>(0xbc4870)(block) : ctx.Original<Mgr *(__cdecl *)()>(0xbc86f0)();
            else if (mode == 1)
                m = how == 2 ? ::new (block) Mgr : Mgr::Create();
            else
                m = how == 2 ? A::Construct(static_cast<Mgr *>(block), nullptr) : Mgr::Create();
            gBase = reinterpret_cast<uint8_t *>(m);
            gMaskUnset = how != 2;
            b.insert(b.end(), gEv.begin(), gEv.end());
            gEv.clear();
            PushManager(b, m);
            for (int i = 0; i < 4; ++i) m->mPool.push_back(Mgr::EffectRef(reinterpret_cast<cITSVisualEffect *>(&w.effects[i])));
            m->mNamedA["x"].Assign(w.Unknown(1));
            m->mRefCount = seed % 4 == 3 ? 2 : seed % 2; // 2: one counting Release first
            difftest::FreeRecorder rec;
            if (how == 0) {
                // Release at a count of 0 or 1 deletes (from 2: counts down first).
                uint32_t res;
                do {
                    res = mode == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xbca210)(m)
                          : mode == 1 ? CallSlot<uint32_t>(m, 2)
                                      : A::Release(m, nullptr);
                    b.push_back(res);
                } while (res != 0);
            } else {
                // Slot 12 of the collection face (flags 1 frees; flags 0 only destroys, the test frees).
                const uint32_t flags = how == 1 ? 1u : 0u;
                Mgr *res;
                if (mode == 0)
                    res = ctx.Original<Mgr *(__thiscall *)(void *, uint32_t)>(0xbc5970)(m, flags);
                else if (mode == 1)
                    res = reinterpret_cast<Mgr *>(static_cast<uint8_t *>(CallSlot<void *>(Face8(m), 12, flags)));
                else
                    res = A::DeletingDestructor(m, nullptr, flags);
                b.push_back(uint32_t(res == m || static_cast<void *>(res) == Face8(m)));
            }
            rec.Stop();
            b.push_back(uint32_t(rec.Freed(m)));
            if (how == 2 && !rec.Freed(m)) ::operator delete(block);
            b.insert(b.end(), gEv.begin(), gEv.end());
            size_t sizes[512];
            const size_t n = rec.SortedSizes(sizes, 512);
            b.push_back(uint32_t(n));
            for (size_t i = 0; i < n && i < 512; ++i) b.push_back(uint32_t(sizes[i]));
            PushFakes(b, w);
            gBase = nullptr;
        }
        if (pass == 0) continue;
        Same(ctx, blobs[0], blobs[1], "CreateDelete (ours)", seed);
        Same(ctx, blobs[0], blobs[2], "CreateDelete (adapters)", seed);
    }
}

// FlushPending 0xbc59d0's round cap: activation (ApplyDetail with 1, stood in) queues more pending effects, `requeue` in all,
// so with one pending effect at the start the flush needs requeue + 1 rounds: 8, 9, 10 and 11 requeues give 9, 10, 11 and
// 12 rounds against the cap of 10 (and 2 or 3 at the start, more per round). Compared after the flush: the activations in
// order, the whole manager (the active list, what is left pending, the pool), the requeues not used.
DIFF_TEST(EffectsManager_FlushRounds, 0xbc59d0)
{
    StandIns stands;
    gMaskUnset = false;
    const uint32_t requeues[] = {0, 7, 8, 9, 10, 11, 14, 25};
    uint32_t seed = 0;
    for (uint32_t start = 1; start <= 3; ++start) {
        for (uint32_t requeue : requeues) {
            Blob blobs[3];
            for (int mode = 0; mode < 3; ++mode) {
                Override<void *> frameworkGlobal(0x10c9378, nullptr);
                World w;
                *GameAddress<void *>(0x10c9378) = &w.framework;
                gEv.clear();
                w.Build(ctx, mode, seed);
                gEv.clear();
                Mgr *m = w.obj;
                for (uint32_t i = 0; i < 6; ++i) {
                    EffectFake &e = w.effects[w.effectsUsed++];
                    m->mPool.push_back(Mgr::EffectRef(reinterpret_cast<cITSVisualEffect *>(&e)));
                    if (i < 2) m->mActive.push_back(i);
                }
                for (uint32_t i = 0; i < start; ++i) m->mPending.push_back(2 + i);
                m->mNoFlush = false;
                gRequeueMgr = m;
                gRequeueLeft = requeue;
                gRequeueNext = 0;
                Blob &b = blobs[mode];
                InvokeMember<void>(ctx, w, 0xbc59d0, [&] { m->FlushPending(); }, &A::FlushPending);
                gRequeueMgr = nullptr;
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                b.push_back(gRequeueLeft);
                b.push_back(uint32_t(m->mActive.size()));
                b.push_back(uint32_t(m->mPending.size()));
                PushManager(b, m);
                w.Destroy(ctx);
                gEv.clear();
                gBase = nullptr;
            }
            Same(ctx, blobs[0], blobs[1], "FlushRounds (ours)", seed);
            Same(ctx, blobs[0], blobs[2], "FlushRounds (adapters)", seed);
            ++seed;
        }
    }
}
