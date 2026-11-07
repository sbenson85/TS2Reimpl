// Differential tests for ObjectSaveTypeTable and GUIDTranslation::Resolve (src/ts/ObjectSaveTypeTable.h, lane MB).
//
// A world, built from a seed the same way for each side: a folder block (0xa4 bytes; its +4 face's slot 14 FindObject a
// logging fake answering by GUID; +0x54 the lot's objects and +0x70 the neighbourhood's, real FolderObjects), selector
// fakes (the slots the table calls, each logged with the selector's index), their definition, file-ref and catalog fakes,
// the folder's missing-object list (a real FolderObjects, or none), a GUID translation (a GUIDTranslation with a logging
// vtable copy), the persist service (cache 0x10faf6c: slot 5 GetResource, logged, handing out a string-set fake), the
// director (0x1ae18d stood in on both sides: slot 3 GetClassObject handing out missing-object fakes). The streams are
// recording fakes: the reader hands out a scripted list of numbers and names, failing (writing 0) past a truncation point;
// the writer logs every value and name.
//
// The original side runs the game's functions on a 0x14 block holding the game's vtable 0xdcd0c0 and the same members;
// ours runs our class (built by our constructor) through our vtable's slot. Each pair compares the result (al for the
// bools), the event log, the missing list (which fakes, in order), every fake's count and the translation's marks.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/cIGZString.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "shape.h"
#include "ts/ObjectSaveTypeTable.h"

#include <cstring>
#include <initializer_list>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {
using Table = ObjectSaveTypeTable;
// The game's vtable for the save table (the original side's blocks only; our code never holds it).
constexpr uint32_t kTableVTable = 0xdcd0c0;
using Blob = std::vector<uint32_t>;

Blob gEv;
void Ev(std::initializer_list<uint32_t> items) { gEv.insert(gEv.end(), items); }
enum : uint32_t {
    evSelAddRef = 0x100, evSelRelease, evDefinition, evName, evGuid, evPortal, evCheckA, evCheckB, evCatalog, evSaveIndex,
    evSetSaveIndex, evSetMismatch, evFileRef, evField, evSingle, evLead, evGroup, evCatalogName, evStrAddRef, evStrRelease,
    evResource, evSetRelease, evStringCount, evClassObject, evInit, evMissAddRef, evMissRelease, evFind, evTrAddRef,
    evTrRelease, evGet16, evGet32, evGetU32, evGetStr, evQuery, evRecordRelease, evRecordString, evGetError, evSetMeta,
    evSet16, evSet32, evSetU32, evSetStr, evDelete
};

void EvText(uint32_t tag, const void *string)
{
    const auto *s = static_cast<const cIGZString *>(string);
    const std::string text(s->Data(), s->Strlen());
    Ev({tag, uint32_t(text.size())});
    for (char c : text) gEv.push_back(uint8_t(c));
}

// Names, short and long (a long one makes the strings allocate).
const char *const kNames[] = {"", "Chair", "Lamp - Deluxe", "A rather long object catalog name, over sixteen", "x"};
const char *PickName(std::mt19937 &r) { return kNames[r() % 5]; }

// ---- definitions, file refs, catalogs, counted strings ----
constexpr unsigned kDefs = 6;
struct Def {
    void **vt;
    unsigned index;
    int32_t objectType, numAttributes, numArrays, fieldD0;
    uint32_t single, lead; // answers in al, junk above
};
Def gDefs[kDefs];
void *gDefVT[10];
int32_t __fastcall DefField(Def *d, void *, uint32_t field)
{
    Ev({evField, d->index, field});
    switch (field) {
    case 9: return d->objectType;
    case 0x3a: return d->numAttributes;
    case 0x3b: return d->numArrays;
    case 0xd0: return d->fieldD0;
    default: return int32_t(0x5a5a0000u | field);
    }
}
uint32_t __fastcall DefSingle(Def *d, void *) { Ev({evSingle, d->index}); return d->single; }
uint32_t __fastcall DefLead(Def *d, void *) { Ev({evLead, d->index}); return d->lead; }

struct FileRef {
    void **vt;
    unsigned index;
    uint32_t group;
};
FileRef gFiles[3];
void *gFileVT[12];
uint32_t __fastcall FileGroup(FileRef *f, void *) { Ev({evGroup, f->index}); return f->group; }

// A counted string handed out by a catalog: a cRZString whose vtable copy logs AddRef / Release.
alignas(8) uint8_t gNameBuffer[sizeof(cRZString)];
cRZString *gName;
void *gNameVT[40];
uint32_t __fastcall StrAddRef(void *, void *) { Ev({evStrAddRef}); return 2; }
uint32_t __fastcall StrRelease(void *, void *) { Ev({evStrRelease}); return 1; }

struct Catalog {
    void **vt;
    unsigned index;
    uint32_t answer; // al
    bool give;       // *out set (also on a false answer)
};
Catalog gCatalogs[3];
void *gCatalogVT[8];
uint32_t __fastcall CatalogName(Catalog *c, void *, void **out)
{
    Ev({evCatalogName, c->index});
    if (c->give) *out = gName;
    return c->answer;
}

// ---- selectors ----
constexpr unsigned kSels = 8;
struct Sel {
    void **vt;
    unsigned index;
    int32_t refs;
    uint32_t guid;
    int32_t checkA, checkB;
    uint32_t saveIndex; // ax, junk above
    uint32_t portal;    // al, junk above
    Def *def;
    FileRef *file;
    Catalog *catalog;
    const char *name;
};
Sel gSels[kSels];
void *gSelVT[71];
uint32_t __fastcall SelAddRef(Sel *s, void *) { Ev({evSelAddRef, s->index}); return uint32_t(++s->refs); }
uint32_t __fastcall SelRelease(Sel *s, void *) { Ev({evSelRelease, s->index}); return uint32_t(--s->refs); }
Def *__fastcall SelDefinition(Sel *s, void *) { Ev({evDefinition, s->index}); return s->def; }
uint32_t __fastcall SelName(Sel *s, void *, void *out)
{
    Ev({evName, s->index});
    EvText(evName, out);
    static_cast<cIGZString *>(out)->FromChar(s->name);
    return 1;
}
uint32_t __fastcall SelGuid(Sel *s, void *) { Ev({evGuid, s->index}); return s->guid; }
uint32_t __fastcall SelPortal(Sel *s, void *) { Ev({evPortal, s->index}); return s->portal; }
int32_t __fastcall SelCheckA(Sel *s, void *) { Ev({evCheckA, s->index}); return s->checkA; }
int32_t __fastcall SelCheckB(Sel *s, void *) { Ev({evCheckB, s->index}); return s->checkB; }
Catalog *__fastcall SelCatalog(Sel *s, void *) { Ev({evCatalog, s->index}); return s->catalog; }
uint32_t __fastcall SelSaveIndex(Sel *s, void *) { Ev({evSaveIndex, s->index}); return s->saveIndex; }
// The argument words as the callees read them: SetSaveIndex a word, SetSaveMismatch a byte.
void __fastcall SelSetSaveIndex(Sel *s, void *, uint32_t index) { Ev({evSetSaveIndex, s->index, index & 0xffff}); }
void __fastcall SelSetMismatch(Sel *s, void *, uint32_t on) { Ev({evSetMismatch, s->index, on & 0xff}); }
FileRef *__fastcall SelFileRef(Sel *s, void *) { Ev({evFileRef, s->index}); return s->file; }

uint32_t SelIndex(const void *p)
{
    for (unsigned i = 0; i < kSels; ++i)
        if (p == &gSels[i]) return i;
    return p ? 0xeeee : 0xffff;
}

// ---- persist service, string sets ----
struct StringSet {
    void **vt;
    uint32_t count;
} gSet;
void *gSetVT[8];
uint32_t __fastcall SetRelease(StringSet *, void *) { Ev({evSetRelease}); return 0; }
uint32_t __fastcall SetCount(StringSet *s, void *, int32_t language) { Ev({evStringCount, uint32_t(language)}); return s->count; }

struct Persist {
    void **vt;
} gPersist;
void *gPersistVT[8];
uint32_t gResourceAnswer; // al, junk above
bool gResourceGive;
uint32_t __fastcall PersistGetResource(Persist *, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    Ev({evResource, key[0], key[1], key[2], key[3], iid, a, b});
    if (gResourceGive) *out = &gSet;
    return gResourceAnswer;
}

// ---- director, missing objects ----
constexpr unsigned kMissing = 24;
struct Missing {
    void **vt;
    unsigned index;
    int32_t refs;
};
Missing gMissing[kMissing];
unsigned gNextMissing;
void *gMissingVT[4];
uint32_t __fastcall MissAddRef(Missing *m, void *) { Ev({evMissAddRef, m->index}); return uint32_t(++m->refs); }
uint32_t __fastcall MissRelease(Missing *m, void *) { Ev({evMissRelease, m->index}); return uint32_t(--m->refs); }
uint32_t __fastcall MissInit(Missing *m, void *, const void *name, uint32_t guid, int32_t type, uint32_t index, uint32_t flag,
                             uint32_t value)
{
    Ev({evInit, m->index, guid, uint32_t(type), index & 0xffff, flag, value});
    EvText(evInit, name);
    return 1;
}
uint32_t MissingIndex(const void *p)
{
    for (unsigned i = 0; i < kMissing; ++i)
        if (p == &gMissing[i]) return i;
    return p ? 0xeeee : 0xffff;
}

struct Director {
    void **vt;
} gDirector;
void *gDirectorVT[8];
uint32_t __fastcall DirectorClassObject(Director *, void *, uint32_t clsid, uint32_t iid, void **out)
{
    Ev({evClassObject, clsid, iid});
    // A fresh object, counted once for the caller (the factory's AddRef, not logged).
    Missing *m = &gMissing[gNextMissing++ % kMissing];
    ++m->refs;
    *out = m;
    return 1;
}
void *__cdecl FakeDirector() { return &gDirector; }

// ---- the translation ----
GUIDTranslation gTranslation;
void *gTranslationVT[8];
uint32_t __fastcall TrAddRef(GUIDTranslation *t, void *) { Ev({evTrAddRef}); return ++t->mRefCount; }
uint32_t __fastcall TrRelease(GUIDTranslation *t, void *) { Ev({evTrRelease}); return --t->mRefCount; }

// ---- the folder block ----
alignas(8) uint8_t gFolder[0xa4];
void *gFolderFaceVT[20];
FolderObjects *gObjects;  // at +0x54
FolderObjects gGlobals;   // +0x70 points here
FolderObjects gMissingList;
void *__fastcall FolderFind(void *, void *, uint32_t guid)
{
    Ev({evFind, guid});
    for (Sel &s : gSels)
        if (s.guid == guid && s.index % 4 != 3) return &s; // every fourth selector is not in the folder
    return nullptr;
}

// ---- streams ----
struct Stream {
    void **vt;
} gStream;
void *gStreamVT[21];
std::vector<uint32_t> gWords;      // the reader's numbers
std::vector<const char *> gTexts;  // its names (GetGZStr and the record's strings)
size_t gNext, gNextText, gTruncate; // reads at or past gTruncate fail
bool gError, gForceError;
uint32_t gQueryAnswer; // al
bool gQueryGive;

struct Record {
    void **vt;
} gRecord;
void *gRecordVT[22];
uint32_t __fastcall RecordRelease(Record *, void *) { Ev({evRecordRelease}); return 0; }
bool NextText(const char *&text)
{
    if (gNext >= gTruncate || gNextText >= gTexts.size()) {
        gError = true;
        return false;
    }
    text = gTexts[gNextText++];
    return true;
}
uint32_t __fastcall RecordString(Record *, void *, void *out)
{
    const char *text;
    const bool ok = NextText(text);
    Ev({evRecordString, ok});
    if (ok) static_cast<cIGZString *>(out)->FromChar(text);
    return ok;
}

bool NextWord(uint32_t &v)
{
    if (gNext >= gTruncate || gNext >= gWords.size()) {
        gError = true;
        v = 0;
        ++gNext;
        return false;
    }
    v = gWords[gNext++];
    return true;
}
// A failed read writes 0 (the original then reads what was written).
uint32_t __fastcall SGet16(Stream *, void *, int16_t *out)
{
    uint32_t v;
    const bool ok = NextWord(v);
    *out = int16_t(v);
    Ev({evGet16, v & 0xffff, ok});
    return ok;
}
uint32_t __fastcall SGet32(Stream *, void *, int32_t *out)
{
    uint32_t v;
    const bool ok = NextWord(v);
    *out = int32_t(v);
    Ev({evGet32, v, ok});
    return ok;
}
uint32_t __fastcall SGetU32(Stream *, void *, uint32_t *out)
{
    uint32_t v;
    const bool ok = NextWord(v);
    *out = v;
    Ev({evGetU32, v, ok});
    return ok;
}
// A failed string read leaves the string as it was.
uint32_t __fastcall SGetStr(Stream *, void *, void *out)
{
    const char *text;
    const bool ok = NextText(text);
    Ev({evGetStr, ok});
    if (ok) static_cast<cIGZString *>(out)->FromChar(text);
    return ok;
}
uint32_t __fastcall SQuery(Stream *, void *, uint32_t riid, void **out)
{
    Ev({evQuery, riid});
    if (gQueryGive) *out = &gRecord;
    return gQueryAnswer;
}
uint32_t __fastcall SGetError(Stream *, void *)
{
    Ev({evGetError});
    return 0xabcd00u | uint32_t(gError || gForceError);
}
void *__fastcall SSetMeta(Stream *, void *, void *metadata)
{
    uint32_t words[5];
    std::memcpy(words, metadata, sizeof(words));
    Ev({evSetMeta, words[0] == uint32_t(uintptr_t(GameAddress<void>(0xe6180c))), words[1], words[2], words[3], words[4]});
    ::operator delete(metadata, 0x14);
    return nullptr;
}
uint32_t __fastcall SSet16(Stream *, void *, uint32_t v) { Ev({evSet16, v & 0xffff}); return 1; }
uint32_t __fastcall SSet32(Stream *, void *, uint32_t v) { Ev({evSet32, v}); return 1; }
uint32_t __fastcall SSetU32(Stream *, void *, uint32_t v) { Ev({evSetU32, v}); return 1; }
uint32_t __fastcall SSetStr(Stream *, void *, void *s) { EvText(evSetStr, s); return 1; }

void ResetStream(bool reading)
{
    for (auto &s : gStreamVT) s = nullptr;
    if (reading) {
        gStreamVT[0] = reinterpret_cast<void *>(&SQuery);
        gStreamVT[6] = reinterpret_cast<void *>(&SGet16);
        gStreamVT[8] = reinterpret_cast<void *>(&SGet32);
        gStreamVT[9] = reinterpret_cast<void *>(&SGetU32);
        gStreamVT[15] = reinterpret_cast<void *>(&SGetStr);
    } else {
        gStreamVT[6] = reinterpret_cast<void *>(&SSet16);
        gStreamVT[8] = reinterpret_cast<void *>(&SSet32);
        gStreamVT[9] = reinterpret_cast<void *>(&SSetU32);
        gStreamVT[15] = reinterpret_cast<void *>(&SSetStr);
    }
    gStreamVT[18] = reinterpret_cast<void *>(&SGetError);
    gStreamVT[19] = reinterpret_cast<void *>(&SSetMeta);
    gStream.vt = gStreamVT;
    gNext = gNextText = 0;
    gError = false;
}

template <class F> void Set(void **vt, unsigned slot, F f) { vt[slot] = reinterpret_cast<void *>(f); }

uint32_t Junk(std::mt19937 &r) { return r() & 0xffffff00u; }
uint32_t Bool(std::mt19937 &r, unsigned oneIn = 2) { return Junk(r) | uint32_t(r() % oneIn == 0); }

// A GUID from the world: a selector's, a translation's source, or another.
uint32_t PickGuid(std::mt19937 &r)
{
    switch (r() % 4) {
    case 0:
    case 1: return gSels[r() % kSels].guid;
    case 2:
        if (!gTranslation.mPairs.empty()) return gTranslation.mPairs[r() % gTranslation.mPairs.size()].from;
        return r() % 40 + 1;
    default: return r() % 40 + 1;
    }
}

void BuildWorld(std::mt19937 &r)
{
    gEv.clear();
    for (unsigned i = 0; i < kDefs; ++i) {
        Def &d = gDefs[i];
        d.vt = gDefVT;
        d.index = i;
        d.objectType = r() % 3 ? int32_t(r() % 0x20) : int32_t(r());
        d.numAttributes = r() % 4 ? int32_t(r() % 8) : 0;
        d.numArrays = r() % 4 ? int32_t(r() % 8) : 0;
        d.fieldD0 = int32_t(r());
        d.single = Bool(r);
        d.lead = Bool(r);
    }
    Set(gDefVT, 3, &DefField);
    Set(gDefVT, 8, &DefSingle);
    Set(gDefVT, 9, &DefLead);
    for (unsigned i = 0; i < 3; ++i) gFiles[i] = {gFileVT, i, r() % 3 ? r() % 0x100 : r()};
    Set(gFileVT, 11, &FileGroup);
    for (unsigned i = 0; i < 3; ++i) {
        // A catalog answering true gives a name (the original copies it without a null test).
        const uint32_t answer = Bool(r, 3) ^ 1u;
        gCatalogs[i] = {gCatalogVT, i, answer, (answer & 1) || r() % 4 != 0};
    }
    Set(gCatalogVT, 6, &CatalogName);
    if (!gName) {
        gName = reinterpret_cast<cRZString *>(gNameBuffer);
        gName->Construct();
    }
    gName->AsInterface()->FromChar(PickName(r));
    std::memcpy(gNameVT, *reinterpret_cast<void ***>(gName), sizeof(gNameVT));
    Set(gNameVT, 1, &StrAddRef);
    Set(gNameVT, 2, &StrRelease);
    *reinterpret_cast<void ***>(gName) = gNameVT;

    Set(gSelVT, 1, &SelAddRef);
    Set(gSelVT, 2, &SelRelease);
    Set(gSelVT, 4, &SelDefinition);
    Set(gSelVT, 7, &SelName);
    Set(gSelVT, 28, &SelGuid);
    Set(gSelVT, 38, &SelPortal);
    Set(gSelVT, 40, &SelCheckA);
    Set(gSelVT, 42, &SelCheckB);
    Set(gSelVT, 54, &SelCatalog);
    Set(gSelVT, 57, &SelSaveIndex);
    Set(gSelVT, 58, &SelSetSaveIndex);
    Set(gSelVT, 59, &SelSetMismatch);
    Set(gSelVT, 70, &SelFileRef);
    for (unsigned i = 0; i < kSels; ++i) {
        Sel &s = gSels[i];
        s.vt = gSelVT;
        s.index = i;
        s.refs = 100;
        s.guid = r() % 5 ? r() % 40 + 1 : r();
        s.checkA = r() % 3 ? int32_t(r() % 4) : int32_t(r());
        s.checkB = r() % 3 ? int32_t(r() % 4) : int32_t(r());
        s.saveIndex = (r() & 0xffff0000u) | (r() % 4 ? r() % 30 + 1 : r() % 3 ? 0 : r() & 0xffff);
        s.portal = Bool(r);
        s.def = &gDefs[r() % kDefs];
        s.file = r() % 3 ? &gFiles[r() % 3] : nullptr;
        s.catalog = &gCatalogs[r() % 3];
        s.name = PickName(r);
    }

    gSet.vt = gSetVT;
    gSet.count = r() % 3 ? r() % 6 : r();
    Set(gSetVT, 2, &SetRelease);
    Set(gSetVT, 5, &SetCount);
    gPersist.vt = gPersistVT;
    Set(gPersistVT, 5, &PersistGetResource);
    gResourceAnswer = Bool(r, 3) ^ 1u;
    gResourceGive = (gResourceAnswer & 1) || r() % 3 == 0;

    for (unsigned i = 0; i < kMissing; ++i) gMissing[i] = {gMissingVT, i, 10};
    gNextMissing = 0;
    Set(gMissingVT, 1, &MissAddRef);
    Set(gMissingVT, 2, &MissRelease);
    Set(gMissingVT, 3, &MissInit);
    gDirector.vt = gDirectorVT;
    Set(gDirectorVT, 3, &DirectorClassObject);

    gTranslation.mVTable = gTranslationVT;
    Set(gTranslationVT, 1, &TrAddRef);
    Set(gTranslationVT, 2, &TrRelease);
    gTranslation.mRefCount = 1;
    gTranslation.mPairs.clear();
    const unsigned pairs = r() % 4 == 0 ? 0 : r() % 8;
    for (unsigned i = 0; i < pairs; ++i) gTranslation.mPairs.push_back({r() % 40 + 1, r() % 3 ? r() % 40 + 1 : gSels[r() % kSels].guid, uint8_t(r() % 2)});

    std::memset(gFolder, 0, sizeof(gFolder));
    *reinterpret_cast<void ***>(gFolder + 4) = gFolderFaceVT;
    Set(gFolderFaceVT, 14, &FolderFind);
    gObjects = ::new (gFolder + 0x54) FolderObjects;
    *reinterpret_cast<FolderObjects **>(gFolder + 0x70) = &gGlobals;
    // Fresh vectors (clear() would keep the last run's capacity, and with it whether a push grows the list).
    FolderObjects().swap(gGlobals);
    FolderObjects().swap(gMissingList);
    const unsigned lot = r() % 6, globals = r() % 4, missing = r() % 4;
    for (unsigned i = 0; i < lot; ++i) gObjects->emplace_back(&gSels[r() % kSels]);
    for (unsigned i = 0; i < globals; ++i) gGlobals.emplace_back(&gSels[r() % kSels]);
    // Some capacity spare or not, so the missing list's push grows it or not.
    gMissingList.reserve(missing + r() % 3);
    for (unsigned i = 0; i < missing; ++i) gMissingList.emplace_back(&gMissing[kMissing - 1 - i]);
    gEv.clear();
}

void TearDownWorld()
{
    gObjects->~FolderObjects();
    gGlobals.clear();
    gMissingList.clear();
}

void Capture(Blob &b)
{
    b.push_back(0xca9);
    for (const Sel &s : gSels) b.push_back(uint32_t(s.refs));
    for (const Missing &m : gMissing) b.push_back(uint32_t(m.refs));
    b.push_back(gTranslation.mRefCount);
    for (const GUIDPair &p : gTranslation.mPairs) b.push_back(p.visited);
    b.push_back(uint32_t(gMissingList.size()));
    for (const FolderRef &m : gMissingList) b.push_back(MissingIndex(m.Get()));
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    if (at == a.size() && at == b.size()) return true;
    ctx.Fail("%s seed %u differs at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, at,
             at < a.size() ? a[at] : 0xdeadbeef, at < b.size() ? b[at] : 0xdeadbeef, a.size(), b.size());
    std::string around[2];
    for (int side = 0; side < 2; ++side) {
        const Blob &x = side ? b : a;
        for (size_t i = at > 12 ? at - 12 : 0; i < x.size() && i < at + 12; ++i) around[side] += std::to_string(x[i]) + (i + 1 == at ? " | " : " ");
    }
    ctx.Fail("  original: %s", around[0].c_str());
    ctx.Fail("  ours:     %s", around[1].c_str());
    return false;
}

void *const *OurVTable()
{
    static void *const *vt = nullptr;
    if (!vt) {
        alignas(8) static uint8_t buffer[sizeof(Table)];
        Table *t = ::new (buffer) Table(nullptr, nullptr, nullptr);
        vt = shape::VTableAt(buffer, 0);
        t->Table::~Table();
    }
    return vt;
}

// The table each side runs: the original's a block with the game's vtable (the translation counted by hand, as the folder
// does), ours built by our constructor.
struct Sides {
    uint32_t block[5];
    alignas(Table) uint8_t ours[sizeof(Table)];
    Table *table = nullptr;
    void *Build(bool original, bool withMissing)
    {
        FolderObjects *missing = withMissing ? &gMissingList : nullptr;
        if (original) {
            block[0] = uint32_t(uintptr_t(shape::GameVTable(kTableVTable)));
            block[1] = uint32_t(uintptr_t(gFolder));
            block[2] = uint32_t(uintptr_t(missing));
            block[3] = uint32_t(uintptr_t(&gTranslation));
            block[4] = 1;
            ++gTranslation.mRefCount;
            return block;
        }
        table = ::new (ours) Table(reinterpret_cast<cTSObjectFolder *>(gFolder), missing, &gTranslation);
        return table;
    }
    // The table's words after the call: the vtable as a code, the members as positions.
    void CaptureTable(Blob &b, bool original) const
    {
        const uint32_t *w = original ? block : reinterpret_cast<const uint32_t *>(ours);
        const void *vt = original ? static_cast<const void *>(shape::GameVTable(kTableVTable)) : static_cast<const void *>(OurVTable());
        b.insert(b.end(), {w[0] == uint32_t(uintptr_t(vt)), w[1] == uint32_t(uintptr_t(gFolder)), w[2] == uint32_t(uintptr_t(&gMissingList)),
                           w[3] == uint32_t(uintptr_t(&gTranslation)), w[4]});
    }
    // The original's destructor 0x4cf490 on the block, ours (the hook's adapter) on ours (both release the translation).
    void Destroy(difftest::Context &ctx, bool original)
    {
        if (original) ctx.Original<void(__thiscall *)(void *)>(0x4cf490)(block);
        else ObjectSaveTypeTableAdapters::Destroy(table, nullptr);
    }
};

// Runs `run(original, rng, sides, blob)` on a fresh world per side and compares.
template <class Run> void Pair(difftest::Context &ctx, const char *what, uint32_t seed, Run run)
{
    Blob blobs[2];
    void **cache = GameAddress<void *>(0x10faf6c);
    void *saved = *cache;
    *cache = &gPersist;
    for (int side = 0; side < 2; ++side) {
        std::mt19937 r(seed * 2654435761u + 11);
        BuildWorld(r);
        Blob &b = blobs[side];
        Sides sides;
        b.push_back(run(side == 0, r, sides, b));
        b.push_back(0xe7e7);
        b.insert(b.end(), gEv.begin(), gEv.end());
        Capture(b);
        TearDownWorld();
    }
    *cache = saved;
    Same(ctx, blobs[0], blobs[1], what, seed);
}

using SerialFn = uint32_t(__thiscall *)(void *, void *);

// A record for Read, version `version`: entries for the world's GUIDs, their values matching the objects or not.
void Script(std::mt19937 &r, uint32_t version)
{
    gWords.clear();
    gTexts.clear();
    gWords.push_back(r() % 4 ? Table::kClassId : r());
    gWords.push_back(version);
    gWords.push_back(r() % 4 ? 0 : r());
    const unsigned entries = r() % 8 == 0 ? 0 : r() % 7;
    for (unsigned e = 0; e < entries; ++e) {
        const uint32_t guid = PickGuid(r);
        gWords.push_back(guid);
        const Sel *s = nullptr;
        for (const Sel &c : gSels)
            if (c.guid == guid) s = &c;
        const bool match = s && r() % 3;
        if (version != 0) {
            gWords.push_back(match && r() % 4 ? uint32_t(s->checkA) : r() % 4);
            gWords.push_back(match && r() % 4 ? uint32_t(s->checkB) : r() % 4);
        }
        if (version >= 0x46) {
            gWords.push_back(r() % 6 == 0 ? 0xffffffffu : match && r() % 4 ? uint32_t(s->def->numAttributes) : r() % 8);
            gWords.push_back(match && r() % 4 ? uint32_t(s->def->numArrays) : r() % 8);
        }
        if (version >= 0x47) gWords.push_back(match && r() % 3 ? gSet.count : r() % 6);
        gWords.push_back(r() % 5 ? r() % 30 + 1 : r());
        if (version > 1) gWords.push_back(r() % 4 ? r() % 0x20 : r());
        gTexts.push_back(PickName(r));
        if (version >= 0x49) {
            const uint32_t flags[] = {0, 1, 3, 2};
            gWords.push_back(r() % 6 ? flags[r() % 4] : r());
            gWords.push_back(r());
        }
    }
    gWords.push_back(0);
    if (r() % 6 == 0) gWords.push_back(r() % 40 + 1); // words after the end: never read
    gTruncate = r() % 5 == 0 ? r() % (gWords.size() + 1) : gWords.size() + 8;
    gForceError = r() % 8 == 0;
    gQueryAnswer = Bool(r, 4) ^ 1u;
    gQueryGive = (gQueryAnswer & 1) || r() % 3 == 0;
    for (auto &s : gRecordVT) s = nullptr;
    Set(gRecordVT, 2, &RecordRelease);
    Set(gRecordVT, 20, &RecordString);
    gRecord.vt = gRecordVT;
}

const uint32_t kVersions[] = {0, 1, 2, 0x3e, 0x3f, 0x45, 0x46, 0x47, 0x48, 0x49, 0x55, 0x60};
} // namespace

DIFF_TEST(ObjectSaveTypeTable_Read, 0x4d4c30)
{
    shape::StandIn director(0x1ae18d, &GetDirectorFrameworkObject, &FakeDirector);
    uint32_t seed = 0;
    for (uint32_t version : kVersions)
        for (uint32_t k = 0; k < 120; ++k, ++seed)
            Pair(ctx, "Read", seed, [&](bool original, std::mt19937 &r, Sides &sides, Blob &b) {
                const bool withMissing = r() % 6 != 0;
                void *self = sides.Build(original, withMissing);
                Script(r, version);
                ResetStream(true);
                gEv.clear();
                const uint32_t res = original ? ctx.Original<SerialFn>(0x4d4c30)(self, &gStream)
                                              : reinterpret_cast<SerialFn>(OurVTable()[4])(self, &gStream);
                b.push_back(uint32_t(gNext));
                b.push_back(uint32_t(gNextText));
                sides.CaptureTable(b, original);
                sides.Destroy(ctx, original);
                return res & 0xff;
            });
}

DIFF_TEST(ObjectSaveTypeTable_Write, 0x4d7040)
{
    for (uint32_t seed = 0; seed < 600; ++seed)
        Pair(ctx, "Write", seed, [&](bool original, std::mt19937 &r, Sides &sides, Blob &b) {
            void *self = sides.Build(original, true);
            ResetStream(false);
            gForceError = r() % 6 == 0;
            gEv.clear();
            const uint32_t res = original ? ctx.Original<SerialFn>(0x4d7040)(self, &gStream)
                                          : reinterpret_cast<SerialFn>(OurVTable()[3])(self, &gStream);
            sides.CaptureTable(b, original);
            sides.Destroy(ctx, original);
            return res & 0xff;
        });
}

DIFF_TEST(ObjectSaveTypeTable_WriteEntry, 0x4d7180)
{
    using EntryFn = void(__thiscall *)(void *, void *, void *);
    for (uint32_t seed = 0; seed < 400; ++seed)
        Pair(ctx, "WriteEntry", seed, [&](bool original, std::mt19937 &r, Sides &sides, Blob &b) {
            void *self = sides.Build(original, true);
            ResetStream(false);
            Sel *s = &gSels[r() % kSels];
            gEv.clear();
            if (original) ctx.Original<EntryFn>(0x4d7180)(self, &gStream, s);
            else sides.table->WriteEntry(reinterpret_cast<ifaces::cIGZOStream *>(&gStream), reinterpret_cast<ifaces::cITSObjectSelector *>(s));
            sides.CaptureTable(b, original);
            sides.Destroy(ctx, original);
            return SelIndex(s);
        });
}

// QueryInterface (shared 0x4d4ba0), AddRef (shared 0x146a40), Release 0x4d55c0 and GetGZCLSID 0x4d3350 through each side's
// vtable slot; Release's deletion (slot 6) goes to a logging fake in a copy of each side's vtable.
namespace {
void *__fastcall FakeDelete(void *, void *, uint32_t flags)
{
    Ev({evDelete, flags});
    return nullptr;
}
} // namespace

DIFF_TEST(ObjectSaveTypeTable_Slots, 0x4d55c0)
{
    const uint32_t ids[] = {0, 1, 2, 0xe4fda3d4, 0xe4fda3d5, 0x6f626a74, 0xffffffff};
    uint32_t seed = 0;
    for (unsigned slot = 0; slot < 6; ++slot) {
        if (slot == 3 || slot == 4) continue; // Write / Read: their own tests
        for (uint32_t k = 0; k < 28; ++k, ++seed)
            Pair(ctx, "Slots", seed, [&](bool original, std::mt19937 &r, Sides &sides, Blob &b) {
                void *self = sides.Build(original, true);
                void *copy[7];
                std::memcpy(copy, original ? shape::GameVTable(kTableVTable) : OurVTable(), sizeof(copy));
                void *const fn = copy[slot];
                copy[6] = reinterpret_cast<void *>(&FakeDelete);
                *static_cast<void **>(self) = copy;
                uint32_t *count = original ? &sides.block[4] : reinterpret_cast<uint32_t *>(sides.ours) + 4;
                *count = r() % 4 ? r() % 4 : r();
                gEv.clear();
                uint32_t res;
                void *out = reinterpret_cast<void *>(0x1234);
                if (slot == 0) {
                    res = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, void **)>(fn)(self, ids[k % 7], &out) & 0xff;
                    b.push_back(out == self ? 1 : out == reinterpret_cast<void *>(0x1234) ? 2 : 0xbad);
                } else {
                    res = reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self);
                }
                b.push_back(*count);
                *static_cast<void **>(self) = original ? shape::GameVTable(kTableVTable) : const_cast<void **>(OurVTable());
                sides.CaptureTable(b, original);
                sides.Destroy(ctx, original);
                return res;
            });
    }
}

// The deleting destructor (slot 6, 0x4cfb60) on a heap table: the translation released, the block freed on bit 0.
DIFF_TEST(ObjectSaveTypeTable_DeletingDestructor, 0x4cfb60)
{
    for (uint32_t seed = 0; seed < 40; ++seed)
        Pair(ctx, "DeletingDestructor", seed, [&](bool original, std::mt19937 &r, Sides &, Blob &b) {
            const uint32_t flags = r() % 3;
            void *self;
            if (original) {
                auto *w = static_cast<uint32_t *>(::operator new(0x14));
                w[0] = uint32_t(uintptr_t(shape::GameVTable(kTableVTable)));
                w[1] = uint32_t(uintptr_t(gFolder));
                w[2] = uint32_t(uintptr_t(&gMissingList));
                w[3] = r() % 4 ? uint32_t(uintptr_t(&gTranslation)) : 0;
                w[4] = 1;
                self = w;
            } else {
                GUIDTranslation *translation = r() % 4 ? &gTranslation : nullptr;
                self = new Table(reinterpret_cast<cTSObjectFolder *>(gFolder), &gMissingList, translation);
                if (translation) --gTranslation.mRefCount; // (the original's block was not counted)
            }
            gEv.clear();
            using DeleteFn = void *(__thiscall *)(void *, uint32_t);
            difftest::FreeRecorder rec;
            void *res = original ? ctx.Original<DeleteFn>(0x4cfb60)(self, flags) : reinterpret_cast<DeleteFn>(OurVTable()[6])(self, flags);
            rec.Stop();
            const bool freed = rec.Freed(self);
            b.push_back(freed);
            if (!freed) {
                b.push_back(*static_cast<void **>(self) == (original ? shape::GameVTable(kTableVTable) : OurVTable()));
                ::operator delete(self, 0x14);
            }
            return uint32_t(res == self);
        });
}

DIFF_TEST(ObjectSaveTypeTable_Destructor, 0x4cf490)
{
    for (uint32_t seed = 0; seed < 20; ++seed)
        Pair(ctx, "Destructor", seed, [&](bool original, std::mt19937 &, Sides &sides, Blob &b) {
            sides.Build(original, true);
            gEv.clear();
            sides.Destroy(ctx, original);
            b.push_back(0xd7);
            return 0u;
        });
}

DIFF_TEST(GUIDTranslation_Resolve, 0xc0b617)
{
    using ResolveFn = uint32_t(__thiscall *)(void *, uint32_t);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 2246822519u + 3);
            GUIDTranslation t{};
            t.mVTable = nullptr;
            const unsigned n = r() % 6 == 0 ? 0 : r() % 10;
            const uint32_t space = r() % 3 ? 6 : 40;
            for (unsigned i = 0; i < n; ++i) t.mPairs.push_back({r() % space, r() % space, uint8_t(r() % 3 ? r() % 2 : r())});
            Blob &b = blobs[side];
            for (int k = 0; k < 4; ++k) {
                const uint32_t id = r() % (space + 2);
                b.push_back(side == 0 ? ctx.Original<ResolveFn>(0xc0b617)(&t, id) : t.Resolve(id));
                for (const GUIDPair &p : t.mPairs) b.insert(b.end(), {p.from, p.to, p.visited});
            }
        }
        Same(ctx, blobs[0], blobs[1], "Resolve", seed);
    }
}
