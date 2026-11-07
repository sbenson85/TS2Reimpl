// cTSInteraction (lane GV): differential tests in the standalone shape (docs/standalone-shape.md, "Tests").
//
// Two routes per case, each on its own interaction seeded alike in a fake world built alike from the case's seed:
//   route 0: the interaction built by the original creator 0x450a70 (game vtable 0xdc8324), the slot called through that
//            vtable (the original function: every test first checks the game's slot holds the RVA of kSlotRva);
//   route 1: the interaction built by our creator (cTSInteraction::Create, the creator hook's replacement), the same slot
//            called through our vtable (a slot-order mistake calls the wrong function).
// Compared: the result, every fake call in order (with its arguments; pointers as codes), the serial counter (0x10da95c),
// the fake world's state the calls change, and the whole interaction (Capture: every field, the name's vtable, reference
// count, text and capacity). Not compared: the vtable word (route's own: one code) and the padding word at +0x36 (the
// creator 0x450ab8 writes the word at +0x34 only, and no function reads or writes +0x36). The byte-matched slots keep an
// adapter (their RVA hook), so their sweeps add
//   route 2: the interaction built by the original creator carrying a copy of the game's vtable with an arity-matched trap in
//            the swept slot, the adapter called (an adapter that dispatched virtually lands on the trap: in game it would be
//            its own hook, an endless loop); its other slots stay the game's.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/cIGZString.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "shape.h"
#include "simantics/cTSInteraction.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace {
namespace gv {
using X = cTSInteraction;
using Blob = std::vector<uint32_t>;

constexpr uint32_t kCreator = 0x450a70;
constexpr uint32_t kLanguageCache = 0x10f51c8; // GetLanguageService's cache (0x9c5a5d and ours read it)
constexpr uint32_t kSelfCode = 0xE0E0E0E0;

// The game's vtable 0xdc8324, slot by slot (folded slots included).
const uint32_t kSlotRva[X::kVTableSlots] = {
    0x451560, 0x148fb0, 0x451990, 0x452540, 0x4515d0, 0x450b50, 0x4514c0, 0x4510e0, 0x4512d0, 0x450540, // 0..9
    0x451d90, 0x46390, 0x450b70, 0x450f80, 0x451d00, 0x158f90, 0x450f10, 0x450e70, 0x149d00, 0x229e40,  // 10..19
    0x450fc0, 0x450ff0, 0x15e550, 0x300a0, 0x450f90, 0x450c40, 0x451be0, 0x451c60, 0x451d30, 0x451050,  // 20..29
    0x22fca0, 0x450b80, 0x451a90, 0x450bd0, 0x451b30, 0x450c30, 0x451bc0, 0x450bc0, 0x451b10, 0x450b90, // 30..39
    0x451ab0, 0x450bf0, 0x451b70, 0x450f00, 0x451c40, 0x450bb0, 0x451af0, 0x450f30, 0x451c90, 0x451010, // 40..49
    0x451d40, 0x450e50, 0x451c00, 0x450ba0, 0x451ad0, 0x450f40, 0x451cb0, 0x450be0, 0x451b50, 0x450e60, // 50..59
    0x451c20, 0x451020, 0x451040, 0x450f50, 0x450f70, 0x451cd0, 0x450520, 0x452360, 0x451000, 0x450c20, // 60..69
    0x451bb0, 0x450900, 0x451b90, 0x450c00, 0x450fd0, 0x451d10, 0x450fe0, 0x451d20, 0x451500, 0x44f640, // 70..79
    0x451d60,                                                                                             // 80
};

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t Bits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }

template <class R, class... Args> R CallSlot(const void *object, unsigned slot, Args... args)
{
    using Fn = R(__thiscall *)(const void *, Args...);
    return (*static_cast<Fn *const *>(object))[slot](object, args...);
}

const void *OurVTable()
{
    static const void *table = nullptr;
    if (!table) {
        X *x = X::Create();
        table = *reinterpret_cast<void **>(x);
        delete x;
    }
    return table;
}

// The byte-matched slots' adapters (src/hook/tables/cTSInteraction.cpp), by RVA.
using Ad = cTSInteractionAdapters;
template <class F> void *A(F f) { return reinterpret_cast<void *>(f); }
const struct { uint32_t rva; void *adapter; } kAdapters[] = {
    {0x451560, A(&Ad::QueryInterface)}, {0x451990, A(&Ad::Release)}, {0x452540, A(&Ad::Write)},
    {0x450b50, A(&Ad::GetGZCLSID)}, {0x4514c0, A(&Ad::IsDefault)}, {0x451d90, A(&Ad::ClearObjectIds)},
    {0x450f10, A(&Ad::GetStackObject)}, {0x450fc0, A(&Ad::GetTypeSubQueue)}, {0x451be0, A(&Ad::SetName)},
    {0x451c60, A(&Ad::SetArguments)}, {0x451d30, A(&Ad::AssignSerial)}, {0x450b80, A(&Ad::GetAutoFirstSelect)},
    {0x451a90, A(&Ad::SetAutoFirstSelect)}, {0x450bd0, A(&Ad::GetFlag1)}, {0x451b30, A(&Ad::SetFlag1)},
    {0x450c30, A(&Ad::GetFlag14)}, {0x451bc0, A(&Ad::SetFlag14)}, {0x450bc0, A(&Ad::GetCheckPending)},
    {0x451b10, A(&Ad::SetCheckPending)}, {0x450b90, A(&Ad::HasPassedCheck)}, {0x451ab0, A(&Ad::SetCheckPassed)},
    {0x450bf0, A(&Ad::GetFlag4)}, {0x451b70, A(&Ad::SetFlag4)}, {0x450f00, A(&Ad::GetBranchFlag)},
    {0x451c40, A(&Ad::SetBranchFlag)}, {0x450bb0, A(&Ad::GetFlag6)}, {0x451af0, A(&Ad::SetFlag6)},
    {0x450f30, A(&Ad::GetFlag10)}, {0x451c90, A(&Ad::SetFlag10)}, {0x451010, A(&Ad::GetFlag11)},
    {0x451d40, A(&Ad::SetFlag11)}, {0x450e50, A(&Ad::GetNameSet)}, {0x451c00, A(&Ad::SetNameSet)},
    {0x450ba0, A(&Ad::GetFlag8)}, {0x451ad0, A(&Ad::SetFlag8)}, {0x450f40, A(&Ad::IsVetoed)}, {0x451cb0, A(&Ad::SetVetoed)},
    {0x450be0, A(&Ad::GetFlag12)}, {0x451b50, A(&Ad::SetFlag12)}, {0x450e60, A(&Ad::GetFlag13)},
    {0x451c20, A(&Ad::SetFlag13)}, {0x451020, A(&Ad::GetExtraObject)}, {0x450f50, A(&Ad::GetGroupObject)},
    {0x451cd0, A(&Ad::SetGroupObject)}, {0x450520, A(&Ad::AssignLinkId)}, {0x451000, A(&Ad::GetLinkId)},
    {0x450c20, A(&Ad::GetLinkNext)}, {0x451bb0, A(&Ad::SetLinkNext)}, {0x451b90, A(&Ad::SetParameters)},
    {0x450c00, A(&Ad::GetParameters)}, {0x451d10, A(&Ad::SetWord32)}, {0x451d20, A(&Ad::SetWord34)},
    {0x451500, A(&Ad::HasGroup)}, {0x451d60, A(&Ad::SetExtraObject)},
};
void *AdapterOf(unsigned slot)
{
    for (const auto &a : kAdapters) {
        if (a.rva == kSlotRva[slot]) return a.adapter;
    }
    return nullptr;
}

// Route 2's vtable copy, the slot swept (its trap and the adapter called), and the trap's hits.
void *gTrapVT[X::kVTableSlots];
unsigned gSweptSlot = ~0u;
int gTrapHits = 0;

// ---- the fake world ----

Blob gLog;
void Note(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) { gLog.insert(gLog.end(), {0xCA11, a, b, c, d}); }

struct Fake {
    void **vt;
    uint32_t code;
};

constexpr int kObjects = 5, kPersons = 3, kEntries = 3, kNames = 3, kInters = 6;
void *gObjectVT[100], *gPersonVT[140], *gTableVT[20], *gEntryVT[100], *gNameVT[40], *gInterVT[90], *gSingletonVT[30],
    *gObjectTableVT[50], *gLinkSourceVT[50], *gFrameworkVT[10], *gLookupVT[20], *gResourceVT[10], *gLanguageVT[10], *gInVT[24],
    *gOutVT[24], *gOwnerVT[80];
Fake gObjects[kObjects], gPersons[kPersons], gTable{gTableVT, 0x7AB1E000}, gEntries[kEntries], gNames[kNames], gInters[kInters],
    gSingleton{gSingletonVT, 0x5196E000}, gObjectTable{gObjectTableVT, 0x0B7AB000}, gLinkSource{gLinkSourceVT, 0x111C0000},
    gFramework{gFrameworkVT, 0xF4A3E000}, gLookup{gLookupVT, 0x100C0000}, gResource{gResourceVT, 0x4E5C0000},
    gLanguage{gLanguageVT, 0x1A6C0000}, gIn{gInVT, 0x15720000}, gOut{gOutVT, 0x05720000}, gOwner{gOwnerVT, 0x0A4E0000};

X *gSelf; // the interaction under test (logged as kSelfCode)
X *gOther; // a second real interaction (CopyFrom's fast path), logged as its code
constexpr uint32_t kOtherCode = 0x07E40000;

uint32_t Code(const void *p)
{
    if (p == nullptr) return 0;
    if (p == gSelf) return kSelfCode;
    if (p == gOther) return kOtherCode;
    const uint8_t *b = static_cast<const uint8_t *>(p), *self = reinterpret_cast<const uint8_t *>(gSelf);
    if (gSelf && b > self && b < self + X::kSize) return 0xE0000000u | uint32_t(b - self); // a field of the interaction
    for (const Fake *group : {gObjects, gPersons, gEntries, gNames, gInters}) {
        const int n = group == gObjects ? kObjects : group == gPersons ? kPersons : group == gEntries ? kEntries :
                      group == gNames ? kNames : kInters;
        for (int i = 0; i < n; ++i)
            if (p == &group[i]) return group[i].code;
    }
    for (const Fake *f : {&gTable, &gSingleton, &gObjectTable, &gLinkSource, &gFramework, &gLookup, &gResource, &gLanguage})
        if (p == f) return f->code;
    return P(p);
}
template <class T> int IndexOf(const Fake *self, const T &group) { return int(self - &group[0]); }

struct EntryValues {
    uint32_t values[100]; // slot answers
    float attenuation[2];
    std::string name;
};
struct PersonValues {
    int object;              // index into gObjects, -1 none
    uint32_t attenuationByte;
    int current, selected;   // index into gInters (or kInters + 0: gOther), -1 none
    int pending[4], npending;
    int queued[4], nqueued;
    uint32_t refs;
};
struct InterValues {
    uint32_t values[90];
    int16_t arguments[4];
    float attenuation;
    int actor, stack, group, extra, name; // object / person / name indices, -1 none
};
struct World {
    uint16_t objectIds[kObjects];
    int objectPerson[kObjects]; // the person its QI answers, -1 none
    bool objectTable[kObjects];
    uint32_t entryLimit;
    bool zeroIdFound; // the object table answers id 0 (SetUpBetween: an actor whose object id is 0)
    EntryValues entries[kEntries];
    PersonValues persons[kPersons];
    uint32_t personCount;
    void *personList[kPersons];
    InterValues inters[kInters];
    uint32_t linkSource;
    std::string names[kNames];
    // streams
    uint32_t version, streamError, streamResource, resourceRecord[2], frameworkOk, resourceLanguage, gameLanguage;
    uint32_t counter;
    std::mt19937 stream;
    // Reads the in-stream answers before its end; past it a read fails without writing its out-parameter, as the game's
    // stream does when a read runs past the end of the data (and the stream reports the error).
    uint32_t streamLeft;
} gW;
// One read from the in-stream: false past its end (nothing written).
bool StreamRead()
{
    if (gW.streamLeft == 0) {
        gW.streamError = 1;
        return false;
    }
    --gW.streamLeft;
    return true;
}
struct { void **first, **last; } gPersonList;

cITSInteraction *InterAt(int index)
{
    if (index < 0) return nullptr;
    if (index >= kInters) return reinterpret_cast<cITSInteraction *>(gOther);
    return reinterpret_cast<cITSInteraction *>(&gInters[index]);
}

// objects (cTSObject: 0 QI, 47 id, 87 interaction table)
uint8_t __fastcall ObjectQI(Fake *self, void *, uint32_t riid, void **out)
{
    const int i = IndexOf(self, gObjects);
    Note(0x0B000000, self->code, riid);
    if (riid == 0x5a69d3d5 && gW.objectPerson[i] >= 0) {
        *out = &gPersons[gW.objectPerson[i]];
        ++gW.persons[gW.objectPerson[i]].refs;
        return 1;
    }
    return 0;
}
uint32_t __fastcall ObjectId(Fake *self, void *) { Note(47, self->code); return 0xabcd0000u | gW.objectIds[IndexOf(self, gObjects)]; }
void *__fastcall ObjectTableOf(Fake *self, void *, uint32_t flag)
{
    Note(87, self->code, flag);
    return gW.objectTable[IndexOf(self, gObjects)] ? &gTable : nullptr;
}

// persons (cITSPerson)
uint32_t __fastcall PersonAddRef(Fake *self, void *) { Note(0x9E01, self->code); return ++gW.persons[IndexOf(self, gPersons)].refs; }
uint32_t __fastcall PersonRelease(Fake *self, void *) { Note(0x9E02, self->code); return --gW.persons[IndexOf(self, gPersons)].refs; }
void *__fastcall PersonObject(Fake *self, void *)
{
    Note(0x9E08, self->code);
    const int o = gW.persons[IndexOf(self, gPersons)].object;
    return o < 0 ? nullptr : &gObjects[o];
}
void __fastcall PersonSetData(Fake *self, void *, uint32_t a, uint32_t b, uint32_t c) { Note(0x9E0B, self->code, a, b + (c << 16)); }
uint32_t __fastcall PersonAttenuation(Fake *self, void *) { Note(0x9E1E, self->code); return gW.persons[IndexOf(self, gPersons)].attenuationByte; }
void __fastcall PersonRemove(Fake *self, void *, uint32_t serial) { Note(0x9E7B, self->code, serial); }
uint32_t __fastcall PersonPendingCount(Fake *self, void *) { Note(0x9E7E, self->code); return 0x55550000u | uint32_t(gW.persons[IndexOf(self, gPersons)].npending); }
uint32_t __fastcall PersonQueuedCount(Fake *self, void *) { Note(0x9E7F, self->code); return 0x55550000u | uint32_t(gW.persons[IndexOf(self, gPersons)].nqueued); }
void *__fastcall PersonPendingAt(Fake *self, void *, int32_t i) { Note(0x9E81, self->code, uint32_t(i)); return InterAt(gW.persons[IndexOf(self, gPersons)].pending[i & 3]); }
void *__fastcall PersonQueuedAt(Fake *self, void *, int32_t i) { Note(0x9E82, self->code, uint32_t(i)); return InterAt(gW.persons[IndexOf(self, gPersons)].queued[i & 3]); }
void *__fastcall PersonCurrent(Fake *self, void *) { Note(0x9E84, self->code); return InterAt(gW.persons[IndexOf(self, gPersons)].current); }
void *__fastcall PersonSelected(Fake *self, void *) { Note(0x9E85, self->code); return InterAt(gW.persons[IndexOf(self, gPersons)].selected); }

// the interaction table (13: the entry at an index)
void *__fastcall TableEntry(Fake *, void *, uint32_t index)
{
    Note(0x7AB1E, index);
    return index < gW.entryLimit ? &gEntries[index % kEntries] : nullptr;
}

// table entries: every used slot answers its value; 84 a float, 90 a name
template <int S> uint32_t __fastcall EntryGet(Fake *self, void *) { Note(0xE700 + S, self->code); return gW.entries[IndexOf(self, gEntries)].values[S]; }
float __fastcall EntryAttenuation(Fake *self, void *, uint32_t alternate)
{
    Note(0xE754, self->code, alternate);
    return gW.entries[IndexOf(self, gEntries)].attenuation[alternate & 1];
}
void *__fastcall EntryName(Fake *self, void *) { Note(0xE75A, self->code); return &gNames[gW.entries[IndexOf(self, gEntries)].values[90] % kNames]; }

// names (cIGZString: 5/6 the text, 7 the length, 8 empty)
const char *__fastcall NameText(Fake *self, void *) { Note(0x4A06, self->code); return gW.names[IndexOf(self, gNames)].c_str(); }
uint32_t __fastcall NameLength(Fake *self, void *) { Note(0x4A07, self->code); return uint32_t(gW.names[IndexOf(self, gNames)].size()); }
uint8_t __fastcall NameEmpty(Fake *self, void *) { Note(0x4A08, self->code); return gW.names[IndexOf(self, gNames)].empty(); }
uint32_t __fastcall NameRef(Fake *self, void *) { Note(0x4A01, self->code); return 1; }

// fake interactions (another cITSInteraction implementation: QI answers no)
uint8_t __fastcall InterQI(Fake *self, void *, uint32_t riid, void **) { Note(0x1700, self->code, riid); return 0; }
template <int S> uint32_t __fastcall InterGet(Fake *self, void *) { Note(0x1700 + S, self->code); return gW.inters[IndexOf(self, gInters)].values[S]; }
float __fastcall InterAttenuation(Fake *self, void *) { Note(0x170c, self->code); return gW.inters[IndexOf(self, gInters)].attenuation; }
void *__fastcall InterArguments(Fake *self, void *) { Note(0x170f, self->code); return gW.inters[IndexOf(self, gInters)].arguments; }
template <int S> void *__fastcall InterObject(Fake *self, void *)
{
    Note(0x1700 + S, self->code);
    const InterValues &v = gW.inters[IndexOf(self, gInters)];
    const int index = S == 16 ? v.stack : S == 61 ? v.extra : v.group;
    return index < 0 ? nullptr : &gObjects[index];
}
void *__fastcall InterActor(Fake *self, void *)
{
    Note(0x1711, self->code);
    const int p = gW.inters[IndexOf(self, gInters)].actor;
    return p < 0 ? nullptr : &gPersons[p];
}
void *__fastcall InterName(Fake *self, void *)
{
    Note(0x1719, self->code);
    return &gNames[gW.inters[IndexOf(self, gInters)].name];
}
void __fastcall InterSetLinkNext(Fake *self, void *, uint32_t id)
{
    Note(0x1746, self->code, id);
    gW.inters[IndexOf(self, gInters)].values[69] = id & 0xffff;
}
void __fastcall InterParameters(Fake *self, void *, uint32_t *a, uint32_t *b)
{
    Note(0x1749, self->code, Code(a), Code(b));
    *a = gW.inters[IndexOf(self, gInters)].values[72];
    *b = gW.inters[IndexOf(self, gInters)].values[73];
}

// the cached singleton (3 the link-id source, 26 the object table), the object table (29 lookup by id, 45 the persons)
void *__fastcall SingletonLinkSource(Fake *, void *) { Note(0x5103); return &gLinkSource; }
void *__fastcall SingletonObjects(Fake *, void *) { Note(0x511a); return &gObjectTable; }
uint32_t __fastcall LinkSourceNext(Fake *, void *) { Note(0x512b); return gW.linkSource; }
void *__fastcall ObjectsLookUp(Fake *, void *, int32_t id)
{
    Note(0x511d, uint32_t(id));
    for (int i = 0; i < kObjects; ++i)
        if (int16_t(gW.objectIds[i]) == id && (id != 0 || gW.zeroIdFound)) return &gObjects[i];
    return nullptr;
}
void *__fastcall ObjectsPersons(Fake *, void *)
{
    Note(0x512d);
    for (uint32_t i = 0; i < gW.personCount; ++i) gW.personList[i] = &gPersons[i];
    gPersonList.first = gW.personList;
    gPersonList.last = gW.personList + gW.personCount;
    return &gPersonList;
}
void *__cdecl StandSingleton() { Note(0x20dd); return &gSingleton; }

// Read's language lookup: the framework (3: make an object), the lookup (11 set, 6 get, 2 release), the stream's resource
// (3 its key record, 2 release), the language service (7 the game's language)
void *__cdecl StandFramework() { Note(0x1ae1); return &gFramework; }
uint8_t __fastcall FrameworkMake(Fake *, void *, uint32_t clsid, uint32_t iid, void **out)
{
    Note(0xF403, clsid, iid);
    if (!gW.frameworkOk) return 0;
    *out = &gLookup;
    return 1;
}
void __fastcall LookupSet(Fake *, void *, uint32_t key, uint32_t flag) { Note(0x100B, key, flag); }
uint8_t __fastcall LookupGet(Fake *, void *, uint32_t index, uint32_t *out)
{
    Note(0x1006, index);
    *out = gW.resourceLanguage;
    return 1;
}
uint32_t __fastcall LookupRelease(Fake *, void *) { Note(0x1002); return 0; }
const uint32_t *__fastcall ResourceKey(Fake *, void *) { Note(0x4E03); return gW.resourceRecord; }
uint32_t __fastcall ResourceRelease(Fake *, void *) { Note(0x4E02); return 0; }
uint32_t __fastcall LanguageCurrent(Fake *, void *) { Note(0x1A07); return gW.gameLanguage; }

// streams: in (0 QI, 5, 6, 7, 8, 12, 15, 18, 20 -> owner 78), out (6, 7, 8, 12, 15, 18)
uint8_t __fastcall InQI(Fake *, void *, uint32_t riid, void **out)
{
    Note(0x1500, riid);
    if (riid != 0xe56b8f03 || !gW.streamResource) return 0;
    *out = &gResource;
    return 1;
}
template <int S, class T> uint8_t __fastcall InGet(Fake *, void *, T *out)
{
    if (!StreamRead()) {
        Note(0x15E0 + S);
        return 0;
    }
    uint32_t v = gW.stream();
    if constexpr (std::is_same_v<T, float>) v = Bits(float(int32_t(v % 2000) - 1000) / 8.0f);
    else if constexpr (S == 8) {
        if (v % 3 == 0) v %= 5;
    }
    Note(0x1500 + S, v);
    std::memcpy(out, &v, sizeof(T));
    return 0x41;
}
uint8_t __fastcall InString(Fake *, void *, void *out)
{
    if (!StreamRead()) {
        Note(0x15EF);
        return 0;
    }
    const uint32_t v = gW.stream();
    Note(0x150f, v % 23);
    static_cast<cRZString *>(out)->mString.assign(v % 23, char('A' + v % 26));
    return 0x41;
}
uint8_t __fastcall StreamError(Fake *, void *) { Note(0x1512); return uint8_t(gW.streamError); }
void *__fastcall InOwner(Fake *, void *) { Note(0x1514); return &gOwner; }
uint32_t *__fastcall OwnerVersion(Fake *, void *) { Note(0x0A4E); return &gW.version; }
template <int S, class T> uint8_t __fastcall OutSet(Fake *, void *, T value)
{
    uint32_t v = 0;
    std::memcpy(&v, &value, sizeof(T));
    Note(0x0500 + S, v);
    return 0x42;
}
uint8_t __fastcall OutString(Fake *, void *, const void *in)
{
    const cRZString *s = static_cast<const cRZString *>(in);
    Note(0x050f, Code(in), uint32_t(s->mString.size()), s->mString.empty() ? 0 : uint8_t(s->mString[0]));
    return 0x42;
}

template <class F> void Put(void **vt, int slot, F f) { vt[slot] = reinterpret_cast<void *>(f); }
template <int... S> void PutInterGetters() { ((gInterVT[S] = reinterpret_cast<void *>(&InterGet<S>)), ...); }

void SetupFakes()
{
    for (int i = 0; i < kObjects; ++i) gObjects[i] = {gObjectVT, 0x0B000000u | i};
    for (int i = 0; i < kPersons; ++i) gPersons[i] = {gPersonVT, 0x9E000000u | i};
    for (int i = 0; i < kEntries; ++i) gEntries[i] = {gEntryVT, 0xE7000000u | i};
    for (int i = 0; i < kNames; ++i) gNames[i] = {gNameVT, 0x4A000000u | i};
    for (int i = 0; i < kInters; ++i) gInters[i] = {gInterVT, 0x17000000u | i};
    Put(gObjectVT, 0, &ObjectQI);
    Put(gObjectVT, 47, &ObjectId);
    Put(gObjectVT, 87, &ObjectTableOf);
    Put(gPersonVT, 1, &PersonAddRef);
    Put(gPersonVT, 2, &PersonRelease);
    Put(gPersonVT, 8, &PersonObject);
    Put(gPersonVT, 11, &PersonSetData);
    Put(gPersonVT, 30, &PersonAttenuation);
    Put(gPersonVT, 123, &PersonRemove);
    Put(gPersonVT, 126, &PersonPendingCount);
    Put(gPersonVT, 127, &PersonQueuedCount);
    Put(gPersonVT, 129, &PersonPendingAt);
    Put(gPersonVT, 130, &PersonQueuedAt);
    Put(gPersonVT, 132, &PersonCurrent);
    Put(gPersonVT, 133, &PersonSelected);
    Put(gTableVT, 13, &TableEntry);
    Put(gEntryVT, 19, &EntryGet<19>);
    Put(gEntryVT, 27, &EntryGet<27>);
    Put(gEntryVT, 31, &EntryGet<31>);
    Put(gEntryVT, 37, &EntryGet<37>);
    Put(gEntryVT, 39, &EntryGet<39>);
    Put(gEntryVT, 41, &EntryGet<41>);
    Put(gEntryVT, 46, &EntryGet<46>);
    Put(gEntryVT, 48, &EntryGet<48>);
    Put(gEntryVT, 50, &EntryGet<50>);
    Put(gEntryVT, 76, &EntryGet<76>);
    Put(gEntryVT, 84, &EntryAttenuation);
    Put(gEntryVT, 90, &EntryName);
    Put(gNameVT, 1, &NameRef);
    Put(gNameVT, 2, &NameRef);
    Put(gNameVT, 5, &NameText);
    Put(gNameVT, 6, &NameText);
    Put(gNameVT, 7, &NameLength);
    Put(gNameVT, 8, &NameEmpty);
    Put(gInterVT, 0, &InterQI);
    PutInterGetters<11, 13, 18, 19, 20, 21, 22, 30, 31, 33, 35, 37, 39, 41, 43, 45, 51, 53, 68, 69, 74>();
    Put(gInterVT, 12, &InterAttenuation);
    Put(gInterVT, 15, &InterArguments);
    Put(gInterVT, 16, &InterObject<16>);
    Put(gInterVT, 17, &InterActor);
    Put(gInterVT, 25, &InterName);
    Put(gInterVT, 61, &InterObject<61>);
    Put(gInterVT, 63, &InterObject<63>);
    Put(gInterVT, 70, &InterSetLinkNext);
    Put(gInterVT, 73, &InterParameters);
    Put(gSingletonVT, 3, &SingletonLinkSource);
    Put(gSingletonVT, 26, &SingletonObjects);
    Put(gLinkSourceVT, 43, &LinkSourceNext);
    Put(gObjectTableVT, 29, &ObjectsLookUp);
    Put(gObjectTableVT, 45, &ObjectsPersons);
    Put(gFrameworkVT, 3, &FrameworkMake);
    Put(gLookupVT, 2, &LookupRelease);
    Put(gLookupVT, 6, &LookupGet);
    Put(gLookupVT, 11, &LookupSet);
    Put(gResourceVT, 2, &ResourceRelease);
    Put(gResourceVT, 3, &ResourceKey);
    Put(gLanguageVT, 7, &LanguageCurrent);
    Put(gInVT, 0, &InQI);
    Put(gInVT, 5, &InGet<5, uint8_t>);
    Put(gInVT, 6, &InGet<6, int16_t>);
    Put(gInVT, 7, &InGet<7, uint16_t>);
    Put(gInVT, 8, &InGet<8, int32_t>);
    Put(gInVT, 12, &InGet<12, float>);
    Put(gInVT, 15, &InString);
    Put(gInVT, 18, &StreamError);
    Put(gInVT, 20, &InOwner);
    Put(gOwnerVT, 78, &OwnerVersion);
    Put(gOutVT, 6, &OutSet<6, int16_t>);
    Put(gOutVT, 7, &OutSet<7, uint16_t>);
    Put(gOutVT, 8, &OutSet<8, int32_t>);
    Put(gOutVT, 12, &OutSet<12, float>);
    Put(gOutVT, 15, &OutString);
    Put(gOutVT, 18, &StreamError);
}

int16_t AnyId(std::mt19937 &r)
{
    const int16_t ids[] = {-1, 0, 1, 2, 3, 4, 5, 7, int16_t(r())};
    return ids[r() % 9];
}

// The world from the seed: objects 0..4 (ids 1..5 mostly; 0 and 1 are Sims of persons 0 and 1), tables, entries, persons,
// fake interactions with link ids among 1..6, streams.
void BuildWorld(uint32_t seed)
{
    std::mt19937 r(seed * 7919u + 17);
    gW.zeroIdFound = false;
    for (int i = 0; i < kObjects; ++i) {
        gW.objectIds[i] = uint16_t(r() % 9 == 0 ? 0x7000 + r() % 5 : i + 1);
        gW.objectPerson[i] = i < 2 && r() % 6 ? i : i == 2 && r() % 3 == 0 ? 2 : -1;
        gW.objectTable[i] = r() % 5 != 0;
    }
    gW.entryLimit = r() % 6 == 0 ? 0 : 1 + r() % 8;
    for (int i = 0; i < kEntries; ++i) {
        EntryValues &e = gW.entries[i];
        for (uint32_t &v : e.values) v = r();
        for (int s : {19, 27, 39, 41, 50}) e.values[s] = r() % 3 == 0 ? 0 : r() % 2 ? 1 : 0x80 | (r() & 0xffffff00u);
        e.values[37] = r() % 4 == 0 ? 0xffffffffu : r() % 10;
        e.values[90] = r() % kNames;
        e.attenuation[0] = float(r() % 1000) / 7.0f;
        e.attenuation[1] = float(r() % 1000) / 3.0f;
    }
    for (int i = 0; i < kNames; ++i) gW.names[i].assign(r() % 3 == 0 ? 0 : r() % 30, char('a' + r() % 26));
    gW.personCount = r() % 4;
    for (int i = 0; i < kPersons; ++i) {
        PersonValues &p = gW.persons[i];
        p.object = int(r() % kObjects); // (never none: every caller of slot 8 uses the object unchecked)
        p.attenuationByte = r() % 2 ? 1 : r() % 2 ? 0 : r();
        p.current = r() % 3 == 0 ? -1 : int(r() % (kInters + 1));
        p.selected = r() % 3 == 0 ? -1 : int(r() % (kInters + 1));
        p.npending = int(r() % 5);
        p.nqueued = int(r() % 5);
        for (int k = 0; k < 4; ++k) {
            p.pending[k] = r() % 4 == 0 ? -1 : int(r() % kInters);
            p.queued[k] = r() % 4 == 0 ? -1 : int(r() % kInters);
        }
        p.refs = 1 + r() % 3;
    }
    for (int i = 0; i < kInters; ++i) {
        InterValues &v = gW.inters[i];
        for (uint32_t &w : v.values) w = r();
        for (int s : {31, 33, 35, 37, 39, 41, 43, 45, 51, 53}) v.values[s] = r() % 3 == 0 ? 0 : r() % 2 ? 1 : 0x80 | (r() & 0xffffff00u);
        v.values[68] = 1 + r() % 6;
        v.values[69] = r() % 3 == 0 ? 0 : 1 + r() % 7;
        for (int16_t &a : v.arguments) a = int16_t(r());
        v.attenuation = float(int32_t(r() % 5000)) / 9.0f;
        v.actor = r() % 4 == 0 ? -1 : int(r() % kPersons);
        v.stack = r() % 4 == 0 ? -1 : int(r() % kObjects);
        v.group = r() % 3 == 0 ? -1 : int(r() % kObjects);
        v.extra = r() % 3 == 0 ? -1 : int(r() % kObjects);
        v.name = int(r() % kNames);
    }
    gW.linkSource = r();
    const uint32_t versions[] = {0x30, 0x3b, 0x3c, 0x3d, 0x3e, 0x6e, 0x6f, 0x71, 0x72, 0x7b, 0x7c, 0x96, 0x97, 0xa5, 0xa6, 0xba,
                                 0xbb, 0xc0, r()};
    gW.version = versions[r() % 19];
    gW.streamError = r() % 5 == 0 ? (r() % 2 ? 1 : 0x100) : 0;
    gW.streamResource = r() % 4 != 0;
    // A third of the streams end early, from Read's 14th read on (the flag word, or +0x12 for versions from 0xa6) up to its
    // last (23 reads). Not earlier: the original reads the source kind, the index and the priority into uninitialised stack
    // temporaries ([ebp-0x1c] at 0x45161a, [ebp-0x20] at 0x45166d, [ebp-0x24] at 0x451696, none stored to before the read),
    // so a failed read there copies stack garbage into +0x08 / +0x14 / +0x20, which no replacement can reproduce.
    gW.streamLeft = r() % 3 == 0 ? 13 + r() % 11 : 0xffffffffu;
    gW.resourceRecord[0] = r();
    gW.resourceRecord[1] = r();
    gW.frameworkOk = r() % 4 != 0;
    gW.resourceLanguage = 1 + r() % 3;
    gW.gameLanguage = 1 + r() % 3;
    const int32_t counters[] = {0, 5, 100, -3, int32_t(r())};
    gW.counter = uint32_t(counters[r() % 5]);
    gW.stream.seed(seed + 99);
}

// The interaction's fields from the seed (on both routes alike).
void Seed(X *x, std::mt19937 &r)
{
    x->mRefCount = 1 + r() % 3;
    x->mSourceKind = r() % 3;
    x->mActorId = AnyId(r);
    x->mStackObjectId = AnyId(r);
    x->mGroupObjectId = AnyId(r);
    x->mExtraObjectId = AnyId(r);
    const int32_t indices[] = {-1, 0, 1, 2, 5, 7, 9, int32_t(r())};
    x->mInteractionIndex = indices[r() % 8];
    for (int16_t &a : x->mArguments) a = r() % 3 ? int16_t(r()) : 0;
    x->mPriority = r() % 3 ? r() : 0;
    x->mActionTreeId = r() % 3 ? int16_t(r()) : 0;
    x->mAttenuation = float(int32_t(r() % 3000) - 1000) / 11.0f;
    x->mAllowSubQueue = uint16_t(r());
    x->mTypeSubQueue = uint16_t(r());
    x->mUIDisplayType = int16_t(r());
    x->mWord32 = int16_t(r());
    x->mWord34 = int16_t(r());
    const uint32_t params[] = {0, 0xffffffffu, 1, r()};
    x->mParameterA = params[r() % 4];
    x->mParameterB = params[r() % 4];
    x->mLinkId = uint16_t(r() % 3 ? 1 + r() % 6 : r());
    x->mLinkNext = uint16_t(r() % 3 ? r() % 7 : r());
    x->mName.mString.mString.assign(r() % 2 ? 0 : r() % 40, char('a' + r() % 26));
    x->mSerial = r() % 3 ? r() % 200 : r();
    x->mFlags = r();
    if (r() % 6 == 0) { // the creator's values, so IsDefault can answer yes
        x->mPriority = 0;
        x->mActionTreeId = 0;
        x->mInteractionIndex = -1;
        x->mGroupObjectId = -1;
        x->mSerial = 0;
        x->mActorId = int16_t(-(int)(r() % 2));
        x->mStackObjectId = int16_t(-(int)(r() % 2));
    }
}

void Capture(Blob &out, int route, const X *x)
{
    const void *vt = *reinterpret_cast<const void *const *>(x);
    const void *expected = route == 1 ? OurVTable() : route == 2 ? static_cast<const void *>(gTrapVT) : GameAddress<void>(X::kOriginalVTable);
    out.push_back(vt == expected ? kSelfCode : P(vt));
    out.insert(out.end(), {x->mRefCount, x->mSourceKind, uint16_t(x->mActorId), uint16_t(x->mStackObjectId), uint16_t(x->mGroupObjectId),
        uint16_t(x->mExtraObjectId), uint32_t(x->mInteractionIndex), uint16_t(x->mArguments[0]), uint16_t(x->mArguments[1]),
        uint16_t(x->mArguments[2]), uint16_t(x->mArguments[3]), x->mPriority, uint16_t(x->mActionTreeId), Bits(x->mAttenuation),
        x->mAllowSubQueue, x->mTypeSubQueue, uint16_t(x->mUIDisplayType), uint16_t(x->mWord32), uint16_t(x->mWord34), x->mParameterA,
        x->mParameterB, x->mLinkId, x->mLinkNext, x->mSerial, x->mFlags});
    const cRZString &name = x->mName.mString;
    out.push_back(*reinterpret_cast<const uint32_t *>(&name) == P(GameAddress<void>(cRZString::kVTable)) ? 0x5A : 0x5B);
    out.push_back(name.mRefCount);
    out.push_back(uint32_t(name.mString.size()));
    out.push_back(uint32_t(name.mString.capacity()));
    for (char c : name.mString) out.push_back(uint8_t(c));
}

// The world's state the calls change.
void CaptureWorld(Blob &out)
{
    for (const PersonValues &p : gW.persons) out.push_back(p.refs);
    for (const InterValues &v : gW.inters) out.push_back(v.values[69]);
    out.push_back(*GameAddress<uint32_t>(X::kSerialCounter));
}

// ---- running a case on both routes ----

struct Ctx {
    difftest::Context &ctx;
    int route;
    X *x;
    std::mt19937 &r;
    bool destroyed;
};
using Case = uint32_t (*)(Ctx &c);

template <class R> uint32_t Wide(R value)
{
    if constexpr (std::is_same_v<R, float>) return Bits(value);
    else if constexpr (std::is_same_v<R, bool>) return uint8_t(value);
    else if constexpr (std::is_pointer_v<R>) return Code(value);
    else return uint32_t(value);
}

// The slot on the route's own vtable (route 0: the game's, i.e. the original function at kSlotRva[slot]); on route 2 the
// swept slot's adapter.
template <class R, class... Args> R Invoke(Ctx &c, unsigned slot, Args... args)
{
    if (c.route == 2 && slot == gSweptSlot)
        return reinterpret_cast<R(__fastcall *)(X *, void *, Args...)>(AdapterOf(slot))(c.x, nullptr, args...);
    return CallSlot<R, Args...>(c.x, slot, args...);
}
template <class R, class... Args> uint32_t Call(Ctx &c, unsigned slot, Args... args)
{
    if constexpr (std::is_void_v<R>) {
        Invoke<void, Args...>(c, slot, args...);
        return 0;
    } else {
        return Wide<R>(Invoke<R, Args...>(c, slot, args...));
    }
}

X *MakeX(difftest::Context &ctx, int route)
{
    if (route == 1) return X::Create();
    X *const x = static_cast<X *>(ctx.Original<void *(__cdecl *)()>(kCreator)());
    if (route == 2) *reinterpret_cast<void **>(x) = gTrapVT;
    return x;
}

// Frees an interaction with our destructor (not under test here: Release / the deleting destructor have their own case).
void Destroy(X *x)
{
    x->X::~X();
    ::operator delete(x);
}

Blob RunRoute(difftest::Context &ctx, int route, uint32_t seed, Case call, uint32_t caseSeed)
{
    BuildWorld(seed);
    X *x = MakeX(ctx, route);
    std::mt19937 seeder(seed * 2246822519u + 3);
    Seed(x, seeder);
    gSelf = x;
    gOther = nullptr;
    gLog.clear();
    *GameAddress<uint32_t>(X::kSerialCounter) = gW.counter;
    std::mt19937 r(caseSeed);
    Ctx c{ctx, route, x, r, false};
    const uint32_t result = call(c);
    Blob blob{result};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    CaptureWorld(blob);
    if (!c.destroyed) {
        Capture(blob, route, x);
        Destroy(x);
    }
    if (gOther) {
        Capture(blob, 0, gOther);
        Destroy(gOther);
        gOther = nullptr;
    }
    gSelf = nullptr;
    return blob;
}

// The environment every test runs in: the fakes, the cached singleton and the framework getter stood in for (the image's
// and ours), the language service cache and the serial counter saved and restored.
struct Env {
    shape::StandIn singleton{0x20ddfb, &GetCachedSingleton, &StandSingleton};
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandFramework};
    void **languageCache = GameAddress<void *>(kLanguageCache);
    void *savedLanguage = *languageCache;
    uint32_t savedCounter = *GameAddress<uint32_t>(X::kSerialCounter);
    explicit Env(difftest::Context &ctx)
    {
        SetupFakes();
        *languageCache = &gLanguage;
        for (unsigned slot = 0; slot < X::kVTableSlots; ++slot) {
            if (GameAddress<void *>(X::kOriginalVTable)[slot] != GameAddress<void>(kSlotRva[slot]))
                ctx.Fail("game vtable slot %u does not hold 0x%x", slot, kSlotRva[slot]);
        }
    }
    ~Env()
    {
        *languageCache = savedLanguage;
        *GameAddress<uint32_t>(X::kSerialCounter) = savedCounter;
    }
};

void Sweep(difftest::Context &ctx, const char *name, unsigned slot, Case call, uint32_t seeds)
{
    const bool adapterRoute = AdapterOf(slot) != nullptr;
    std::memcpy(gTrapVT, GameAddress<void *>(X::kOriginalVTable), sizeof gTrapVT);
    gTrapVT[slot] = shape::TrapFor<&shape::Count<gTrapHits>>(8); // (popping more words than any slot takes: a hit fails anyway)
    gSweptSlot = slot;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        const uint32_t caseSeed = seed * 2654435761u + slot * 977u + 1;
        const Blob original = RunRoute(ctx, 0, seed, call, caseSeed);
        for (int route = 1; route <= (adapterRoute ? 2 : 1); ++route) {
        gTrapHits = 0;
        const Blob ours = RunRoute(ctx, route, seed, call, caseSeed);
        if (gTrapHits != 0) {
            ctx.Fail("%s (slot %u) seed %u: the adapter dispatched through the vtable", name, slot, seed);
            return;
        }
        if (original != ours) {
            size_t at = 0;
            while (at < original.size() && at < ours.size() && original[at] == ours[at]) ++at;
            ctx.Fail("%s (slot %u, 0x%x) seed %u route %d: differs at word %zu (%zu / %zu words): %08x / %08x", name, slot, kSlotRva[slot],
                seed, route, at, original.size(), ours.size(), at < original.size() ? original[at] : 0, at < ours.size() ? ours[at] : 0);
            return;
        }
        ++ctx.cases;
        }
    }
    gSweptSlot = ~0u;
}

uint32_t AnyFlagArg(std::mt19937 &r) { return r() % 3 == 0 ? 0x100 : r() % 2 ? 1 : 0x80 | (r() & 0xffff0000u); }

void *AnyObject(std::mt19937 &r) { return r() % 5 == 0 ? nullptr : &gObjects[r() % kObjects]; }
void *AnyPerson(std::mt19937 &r) { return r() % 6 == 0 ? nullptr : &gPersons[r() % kPersons]; }

} // namespace gv
} // namespace

using namespace gv;

// Every slot with no other test: the plain getters and setters (folded ones included), the flag pairs, the ids, the serial,
// the parameters, QueryInterface / AddRef / Release / GetGZCLSID, IsDefault and the lookups by id.
DIFF_TEST(Interaction_Slots, 0x451560)
{
    Env env(ctx);
    static unsigned slot;
    // No-argument getters (results compared at their full register width where the original returns one).
    struct Getter { unsigned slot; int kind; }; // kind: 0 u32, 1 u16, 2 i16, 3 bool, 4 float, 5 pointer
    static const Getter getters[] = {{5, 0}, {6, 3}, {11, 0}, {12, 4}, {13, 2}, {15, 5}, {16, 5}, {17, 5}, {18, 0}, {19, 1},
        {20, 1}, {21, 2}, {22, 0}, {24, 5}, {29, 3}, {30, 0}, {31, 3}, {33, 3}, {35, 3}, {37, 3}, {39, 3}, {41, 3}, {43, 3},
        {45, 3}, {47, 3}, {49, 3}, {51, 3}, {53, 3}, {55, 3}, {57, 3}, {59, 3}, {61, 5}, {62, 2}, {63, 5}, {64, 2}, {68, 1},
        {69, 1}, {74, 2}, {76, 2}, {78, 3}};
    static int kind;
    for (const Getter &g : getters) {
        slot = g.slot;
        kind = g.kind;
        char name[32];
        std::snprintf(name, sizeof name, "Get%u", g.slot);
        Sweep(ctx, name, g.slot, [](Ctx &c) -> uint32_t {
            switch (kind) {
            case 1: return Call<uint16_t>(c, slot);
            case 2: return Call<int16_t>(c, slot);
            case 3: return Call<bool>(c, slot);
            case 4: return Call<float>(c, slot);
            case 5: {
                void *p = Invoke<void *>(c, slot);
                if (p == c.x->mArguments) return 0xA5650000; // GetArguments: the field's address
                return Code(p);
            }
            default: return Call<uint32_t>(c, slot);
            }
        }, 40);
    }
    // One-argument setters (the flag setters read a byte; the word setters a word).
    static const unsigned setters[] = {14, 23, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 70, 75, 77};
    for (unsigned s : setters) {
        slot = s;
        char name[32];
        std::snprintf(name, sizeof name, "Set%u", s);
        Sweep(ctx, name, s, [](Ctx &c) -> uint32_t { return Call<void>(c, slot, AnyFlagArg(c.r) | (c.r() % 2 ? 0 : c.r() & 0xffff)); }, 30);
    }
    Sweep(ctx, "ClearObjectIds", 10, [](Ctx &c) -> uint32_t { return Call<void>(c, 10); }, 10);
    Sweep(ctx, "SetArguments", 27, [](Ctx &c) -> uint32_t {
        int16_t args[4] = {int16_t(c.r()), int16_t(c.r()), int16_t(c.r()), int16_t(c.r())};
        return Call<void>(c, 27, static_cast<const int16_t *>(args));
    }, 10);
    Sweep(ctx, "AssignSerial", 28, [](Ctx &c) -> uint32_t { return Call<void>(c, 28); }, 20);
    Sweep(ctx, "SetGroupObject", 65, [](Ctx &c) -> uint32_t { return Call<void>(c, 65, AnyObject(c.r)); }, 30);
    Sweep(ctx, "SetExtraObject", 80, [](Ctx &c) -> uint32_t { return Call<void>(c, 80, AnyObject(c.r)); }, 30);
    Sweep(ctx, "AssignLinkId", 66, [](Ctx &c) -> uint32_t { return Call<void>(c, 66); }, 10);
    Sweep(ctx, "SetParameters", 72, [](Ctx &c) -> uint32_t { return Call<void>(c, 72, uint32_t(c.r()), uint32_t(c.r())); }, 10);
    Sweep(ctx, "GetParameters", 73, [](Ctx &c) -> uint32_t {
        uint32_t a = 0x11111111, b = 0x22222222;
        Call<void>(c, 73, &a, &b);
        return a ^ (b * 3);
    }, 10);
    // cIGZUnknown
    Sweep(ctx, "QueryInterface", 0, [](Ctx &c) -> uint32_t {
        const uint32_t ids[] = {1, 0x5aca6692, 0xe4fda3d4, 0xfc5ecc3d, 0, 2, 0x5aca6691, 0xe4fda3d5, 0xfc5ecc3c, 0xffffffffu, uint32_t(c.r())};
        void *out = reinterpret_cast<void *>(0x0D0D0D0D);
        const bool withOut = c.r() % 6 != 0;
        const uint32_t r = Call<bool>(c, 0, ids[c.r() % 11], withOut ? &out : static_cast<void **>(nullptr));
        return r | (out == c.x ? 0x100 : out == reinterpret_cast<void *>(0x0D0D0D0D) ? 0x200 : 0x400);
    }, 60);
    // The creator's fields as created (Seed overwrites them in every other case): a fresh interaction from the route's creator.
    Sweep(ctx, "Create", 1, [](Ctx &c) -> uint32_t {
        const int route = c.route == 1 ? 1 : 0;
        X *const fresh = MakeX(c.ctx, route);
        Capture(gLog, route, fresh);
        Destroy(fresh);
        return 0;
    }, 2);
    Sweep(ctx, "AddRef", 1, [](Ctx &c) -> uint32_t { return Call<uint32_t>(c, 1); }, 10);
    Sweep(ctx, "Release", 2, [](Ctx &c) -> uint32_t {
        c.x->mRefCount = 2 + c.r() % 3;
        return Call<uint32_t>(c, 2);
    }, 10);
}

// Release of the last reference: the deleting destructor (slot 79) frees the name and the object.
DIFF_TEST(Interaction_Destroy, 0x44f640)
{
    Env env(ctx);
    Sweep(ctx, "Release(last)", 2, [](Ctx &c) -> uint32_t {
        c.x->mRefCount = 1;
        X *const x = c.x;
        difftest::FreeRecorder rec;
        const uint32_t result = Call<uint32_t>(c, 2);
        rec.Stop();
        c.destroyed = true;
        size_t sizes[8] = {};
        const size_t n = rec.SortedSizes(sizes, 8);
        return result | (rec.Freed(x) ? 0x100 : 0) | uint32_t(n << 12) | uint32_t(sizes[n ? n - 1 : 0] << 16);
    }, 20);
    Sweep(ctx, "DeletingDestructor(0)", 79, [](Ctx &c) -> uint32_t {
        X *const x = c.x;
        difftest::FreeRecorder rec;
        const uint32_t self = P(CallSlot<void *>(x, 79, uint32_t(c.r() % 2 ? 0 : 2)));
        rec.Stop();
        // Not freed: the object stays readable (its vtable word now the class's own again; the name is emptied).
        return (self == P(x) ? 1 : 0) | (rec.Freed(x) ? 0x100 : 0) | uint32_t(rec.Count() << 12);
    }, 20);
}

// SetUp (slot 8): actor, stack object, index, priority, extra object; the entry the stack object's table holds at the index.
DIFF_TEST(Interaction_SetUp, 0x4512d0)
{
    Env env(ctx);
    Sweep(ctx, "SetUp", 8, [](Ctx &c) -> uint32_t {
        void *const actor = AnyPerson(c.r);
        void *const stack = AnyObject(c.r);
        const int32_t indices[] = {-1, 0, 1, 2, 4, 8, int32_t(c.r())};
        const int32_t index = indices[c.r() % 7];
        const uint32_t priority = c.r();
        void *const extra = AnyObject(c.r);
        return Call<bool>(c, 8, actor, stack, index, priority, extra);
    }, 300);
}

// SetUpBetween (slot 7): the source person's current interaction copied (a fake: slots; a real one: fields) and joined.
DIFF_TEST(Interaction_SetUpBetween, 0x4510e0)
{
    Env env(ctx);
    Sweep(ctx, "SetUpBetween", 7, [](Ctx &c) -> uint32_t {
        const int source = int(c.r() % kPersons);
        const int pick = int(c.r() % (kInters + 2));
        // The actor is a Sim whose object answers the person query: with an entry the original reads the actor's slot 30
        // unchecked (0x4511f7) when the actor id is not 0.
        const int actorIndex = int(c.r() % 2);
        // Sometimes its object id is 0 and the table finds it: the original then takes the entry's values only for a non-zero id.
        gW.zeroIdFound = c.r() % 4 == 0;
        gW.objectIds[actorIndex] = gW.zeroIdFound ? uint16_t(0) : uint16_t(actorIndex + 1);
        gW.objectPerson[actorIndex] = actorIndex;
        gW.persons[actorIndex].object = actorIndex;
        if (pick >= kInters) { // a real interaction (fast path of CopyFrom)
            gOther = MakeX(c.ctx, 0);
            std::mt19937 seeder(c.r());
            Seed(gOther, seeder);
            gOther->mRefCount = 3;
        }
        gW.persons[source].current = pick >= kInters ? kInters : pick;
        void *const actor = c.r() % 8 == 0 ? nullptr : static_cast<void *>(&gPersons[actorIndex]);
        if (actor == nullptr) gW.entryLimit = 0; // (no entry: the actor id -1 is looked up but not used)
        return Call<bool>(c, 7, actor, static_cast<void *>(&gPersons[source]));
    }, 300);
}

// CopyFrom (slot 9): null, a fake (through its slots) and a real interaction built by the original creator (its fields).
DIFF_TEST(Interaction_CopyFrom, 0x450540)
{
    Env env(ctx);
    Sweep(ctx, "CopyFrom", 9, [](Ctx &c) -> uint32_t {
        const uint32_t pick = c.r() % 8;
        cITSInteraction *other = nullptr;
        if (pick >= 2 && pick < 5) {
            other = InterAt(int(c.r() % kInters));
        } else if (pick >= 5) {
            gOther = MakeX(c.ctx, 0);
            std::mt19937 seeder(c.r());
            Seed(gOther, seeder);
            gOther->mRefCount = 2;
            other = reinterpret_cast<cITSInteraction *>(gOther);
        }
        if (pick == 7) other = reinterpret_cast<cITSInteraction *>(c.x); // itself
        return Call<bool>(c, 9, other);
    }, 300);
}

// GetName (slot 25: built from the entry when empty) and SetName (slot 26).
DIFF_TEST(Interaction_Name, 0x450c40)
{
    Env env(ctx);
    Sweep(ctx, "GetName", 25, [](Ctx &c) -> uint32_t {
        void *const name = CallSlot<void *>(c.x, 25);
        return name == &c.x->mName ? 0x4A4A : Code(name);
    }, 200);
    Sweep(ctx, "SetName", 26, [](Ctx &c) -> uint32_t { return Call<void>(c, 26, static_cast<void *>(&gNames[c.r() % kNames])); }, 30);
}

// CancelLinked (slot 67) and FindByLinkId (slot 71) over the persons' current, selected, pending and queued interactions.
DIFF_TEST(Interaction_Links, 0x452360)
{
    Env env(ctx);
    Sweep(ctx, "CancelLinked", 67, [](Ctx &c) -> uint32_t {
        gW.personCount = 1 + c.r() % 3;
        void *const except = c.r() % 3 == 0 ? nullptr : static_cast<void *>(&gPersons[c.r() % kPersons]);
        return Call<void>(c, 67, except);
    }, 400);
    Sweep(ctx, "FindByLinkId", 71, [](Ctx &c) -> uint32_t {
        const int16_t ids[] = {0, 1, 2, 3, 4, 5, 6, 7, -1, int16_t(c.r())};
        return Call<void *>(c, 71, uint32_t(uint16_t(ids[c.r() % 10])) | (c.r() & 0xffff0000u));
    }, 300);
}

// Write (slot 3) and Read (slot 4) on scripted streams; Read at every format version boundary, with and without the
// language lookup.
DIFF_TEST(Interaction_Stream, 0x4515d0)
{
    Env env(ctx);
    Sweep(ctx, "Write", 3, [](Ctx &c) -> uint32_t { return Call<bool>(c, 3, static_cast<void *>(&gOut)); }, 60);
    Sweep(ctx, "Read", 4, [](Ctx &c) -> uint32_t { return Call<bool>(c, 4, static_cast<void *>(&gIn)); }, 400);
}

// Every RVA the status file names for this class (the folded slots included), so the tools find them here: 0x148fb0 0x46390
// 0x450b70 0x450f80 0x451d00 0x158f90 0x149d00 0x229e40 0x450ff0 0x15e550 0x300a0 0x451050 0x22fca0 0x451040 0x450f70 0x450fd0
// 0x450fe0 0x451990 0x452540 0x450b50 0x4514c0 0x451d90 0x450f10 0x450e70 0x450fc0 0x450f90 0x451be0 0x451c60 0x451d30
// 0x450b80 0x451a90 0x450bd0 0x451b30 0x450c30 0x451bc0 0x450bc0 0x451b10 0x450b90 0x451ab0 0x450bf0 0x451b70 0x450f00 0x451c40
// 0x450bb0 0x451af0 0x450f30 0x451c90 0x451010 0x451d40 0x450e50 0x451c00 0x450ba0 0x451ad0 0x450f40 0x451cb0 0x450be0 0x451b50
// 0x450e60 0x451c20 0x451020 0x450f50 0x451cd0 0x450520 0x451000 0x450c20 0x451bb0 0x450900 0x451b90 0x450c00 0x451d10
// 0x451d20 0x451500 0x451d60 0x450a70 (the creator: every route-1 object is built by it).
