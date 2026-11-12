// Lane 261111g (row group SU6): the geometry data container helpers. GeometrySelection (src/sg/GeometrySelection.h):
// its constructors 0xaed500 / 0xaed570 and the selections 0xaedbb0 (elements) / 0xaedcd0 (groups); the free helpers
// FindLinkedElement 0xaeda40, FindLinkedElementPosition 0xaedfa0, ReadLinkedFloats 0xaee2c0, ReadLinkedVectors3 0xaee570
// (src/sg/SGUtilitiesGeometry.cpp). Lane 261111l: GeometrySelection's accessors 0xaeda10 (ElementHeader) / 0xaedb10
// (GroupInfo), on the same containers (SG_Utilities_Geometry_ElementHeader / _GroupInfo). Both call LinkedElementHeader 0xaee030 (ours on our side, the image's on the
// original's) and the readers build their place table with the STL's vector<int>(n, value) 0xa7b0f0 (library).
//
// Both sides run against the same fake containers (a raw vtable: AddRef / Release 1 / 2, counts 31 / 44, headers 32 /
// 40 / 45, ranges 33 / 35 / 38 / 41 / 42) whose iterators are logging fakes that never free themselves. Each fake call
// logs its slot and arguments; the header slots may answer a buffer other than their out (which one the code reads);
// the count slots answer a different number on each call (when the count is asked); range fakes log the heap counts
// (the place table's allocation and free against the slots) and the out vectors' sizes (when they are cleared and
// filled against the reads). Selections start from a held container that is null, the same as the new one or another,
// and pre-filled lists; readers' out vectors are pre-filled too. After each call the test notes the result, the object
// or vectors (contents, size, capacity), every container's and iterator's count.
#include "difftest.h"
#include "free_recorder.h"

#include "sg/GeometrySelection.h"
#include "sg/SGUtilities.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cGeometryIterators.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <vector>

using namespace nGZSceneGraph;

namespace {

// ---- the log ----

uint32_t gLog[1 << 15];
size_t gN;

void Log(uint32_t a) { if (gN < sizeof(gLog) / sizeof(gLog[0])) gLog[gN++] = a; }
void Log(uint32_t a, uint32_t b) { Log(a), Log(b); }
void Log(uint32_t a, uint32_t b, uint32_t c) { Log(a), Log(b), Log(c); }

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
float Float(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// The heap counts while a call runs (logged by the fakes), and the readers' out vectors' sizes.
difftest::AllocationCounter *gCounter;
const std::vector<uint16_t> *gAliasesOut;
const void *gValuesOut;
size_t (*gValuesSize)(const void *);

void LogHeap()
{
    if (gCounter) Log(0x99, uint32_t(gCounter->Allocations()), uint32_t(gCounter->Frees()));
    if (gAliasesOut) Log(0x98, uint32_t(gAliasesOut->size()), uint32_t(gValuesSize(gValuesOut)));
}

// ---- the values the fakes read ----

uint32_t gWords[1024];
uint16_t gIndices[1024];
uint32_t gSmall; // the uint16 values' bound (below every reader's element number)

template <class T> T Value(uint32_t set, int32_t p);
template <> float Value<float>(uint32_t set, int32_t p) { return Float(gWords[(set * 61 + uint32_t(p)) & 1023]); }
template <> uint16_t Value<uint16_t>(uint32_t set, int32_t p)
{
    return uint16_t(gIndices[(set * 29 + uint32_t(p)) & 1023] % gSmall);
}
template <> cGeometryVector<3> Value<cGeometryVector<3>>(uint32_t set, int32_t p)
{
    cGeometryVector<3> v;
    for (int i = 0; i < 3; ++i) v.v[i] = Float(gWords[(set * 61 + uint32_t(p) * 3 + i) & 1023]);
    return v;
}

void LogValue(float v) { Log(Bits(v)); }
void LogValue(uint16_t v) { Log(v); }
void LogValue(const cGeometryVector<3> &v)
{
    for (int i = 0; i < 3; ++i) Log(Bits(v.v[i]));
}

// ---- the fake iterators (never freed; ids from 1 per side) ----

uint32_t gNextId;

template <class T> struct FakeIter final : cIGeometryIterator<T> {
    uint32_t id = 0;
    uint32_t count = 0;
    uint32_t set = 0;
    int32_t pos = 0;

    bool QueryInterface(uint32_t, void **) override { std::abort(); }
    uint32_t AddRef() override { Log(id, 0x41); return ++count; }
    uint32_t Release() override
    {
        Log(id, 0x52);
        LogHeap();
        return --count;
    }
    cIGeometryIterator<T> *Clone() override { std::abort(); }
    bool IsEqual(const cIGeometryIterator<T> *other) override
    {
        const auto *o = static_cast<const FakeIter *>(other);
        Log(id, 0x45, o->id);
        return o->pos == pos;
    }
    T Get() override
    {
        Log(id, 0x47);
        LogHeap();
        return Value<T>(set, pos);
    }
    void Next() override { Log(id, 0x4e); ++pos; }
    void Previous() override { std::abort(); }
    void Advance(int32_t) override { std::abort(); }
    T At(int32_t n) override
    {
        Log(id, 0x54, uint32_t(n));
        LogHeap();
        return Value<T>(set, pos + n);
    }
};

template <class T> struct Pool {
    static std::array<FakeIter<T>, 512> items;
    static size_t used;
    static FakeIter<T> *New(uint32_t set, int32_t pos)
    {
        if (used == items.size()) std::abort();
        FakeIter<T> *f = &items[used++];
        f->id = ++gNextId;
        f->count = 1;
        f->set = set;
        f->pos = pos;
        return f;
    }
    static void NoteCounts()
    {
        for (size_t i = 0; i < used; ++i) Log(0xc0, items[i].id, items[i].count);
    }
};
template <class T> std::array<FakeIter<T>, 512> Pool<T>::items;
template <class T> size_t Pool<T>::used;

// ---- the fake containers ----

struct FakeBox {
    void **vtable;
    uint32_t id;
    uint32_t count;
};

struct BoxConfig {
    cGeometryElementHeader given[16], answered[16];
    cGeometryLinkageHeader linkGiven[8], linkAnswered[8];
    cGeometryGroupInfo groupGiven[16], groupAnswered[16];
    bool answerOther;
    uint32_t counts[16]; // what the count slots answer, call after call
    uint32_t countCalls;
    uint32_t lengthSeed;  // the ranges' lengths
};
BoxConfig gBox;
void *gBoxTable[64];
FakeBox gBoxes[3] = {{gBoxTable, 1, 0}, {gBoxTable, 2, 0}, {gBoxTable, 3, 0}};

uint32_t IdOfBox(const void *p)
{
    for (const FakeBox &b : gBoxes)
        if (p == &b) return b.id;
    return p ? 0xeeu : 0u;
}

template <class T> void MakeRange(cGeometryRange<T> *out, uint32_t set)
{
    const int32_t length = int32_t((set * 7 + gBox.lengthSeed) % 13);
    out->mEnd = Pool<T>::New(set, length);
    out->mBegin = Pool<T>::New(set, 0);
    out->mCurrent = Pool<T>::New(set, 0);
}

void __fastcall BoxAbort(void *, void *) { std::abort(); }

uint32_t __fastcall BoxAddRef(FakeBox *self, void *)
{
    Log(0xa1, self->id);
    return ++self->count;
}
uint32_t __fastcall BoxRelease(FakeBox *self, void *)
{
    Log(0xa2, self->id);
    return --self->count;
}
uint32_t __fastcall BoxCount(FakeBox *self, void *)
{
    const uint32_t n = gBox.counts[gBox.countCalls++ & 15];
    Log(0xa3, self->id, n);
    return n;
}

// The accessor cases' out buffer: the header and group fakes note whether they were handed it itself (the accessors pass
// their caller's buffer straight through, no local copy).
const void *gCallerOut = nullptr;

cGeometryElementHeader *__fastcall BoxHeader(void *, void *, cGeometryElementHeader *out, uint32_t index)
{
    Log(0xb0, index, out == gCallerOut ? 1u : 0u);
    LogHeap();
    *out = gBox.given[index & 15];
    return gBox.answerOther ? &gBox.answered[index & 15] : out;
}

template <class T> cGeometryRange<T> *__fastcall BoxValues(void *, void *, cGeometryRange<T> *out, uint32_t index)
{
    Log(0xb1, index);
    LogHeap();
    MakeRange<T>(out, index);
    return out;
}

cGeometryRange<uint16_t> *__fastcall BoxIndices(void *, void *, cGeometryRange<uint16_t> *out, uint32_t index)
{
    Log(0xb4, index);
    LogHeap();
    MakeRange<uint16_t>(out, 300 + index);
    return out;
}

cGeometryLinkageHeader *__fastcall BoxLinkage(void *, void *, cGeometryLinkageHeader *out, uint32_t linkage)
{
    Log(0xb5, linkage);
    *out = gBox.linkGiven[linkage & 7];
    return gBox.answerOther ? &gBox.linkAnswered[linkage & 7] : out;
}

cGeometryRange<uint16_t> *__fastcall BoxElements(void *, void *, cGeometryRange<uint16_t> *out, uint32_t linkage)
{
    Log(0xb2, linkage);
    MakeRange<uint16_t>(out, 100 + linkage);
    return out;
}

cGeometryRange<uint16_t> *__fastcall BoxAliases(void *, void *, cGeometryRange<uint16_t> *out, uint32_t linkage,
                                               uint32_t which)
{
    Log(0xb3, linkage, which);
    LogHeap();
    MakeRange<uint16_t>(out, 200 + linkage * 7 + which);
    return out;
}

cGeometryGroupInfo *__fastcall BoxGroup(void *, void *, cGeometryGroupInfo *out, uint32_t index)
{
    Log(0xb6, index, out == gCallerOut ? 1u : 0u);
    *out = gBox.groupGiven[index & 15];
    return gBox.answerOther ? &gBox.groupAnswered[index & 15] : out;
}

void BuildTable()
{
    for (void *&slot : gBoxTable) slot = reinterpret_cast<void *>(&BoxAbort);
    gBoxTable[1] = reinterpret_cast<void *>(&BoxAddRef);
    gBoxTable[2] = reinterpret_cast<void *>(&BoxRelease);
    gBoxTable[31] = reinterpret_cast<void *>(&BoxCount);
    gBoxTable[32] = reinterpret_cast<void *>(&BoxHeader);
    gBoxTable[33] = reinterpret_cast<void *>(&BoxValues<float>);
    gBoxTable[35] = reinterpret_cast<void *>(&BoxValues<cGeometryVector<3>>);
    gBoxTable[38] = reinterpret_cast<void *>(&BoxIndices);
    gBoxTable[40] = reinterpret_cast<void *>(&BoxLinkage);
    gBoxTable[41] = reinterpret_cast<void *>(&BoxElements);
    gBoxTable[42] = reinterpret_cast<void *>(&BoxAliases);
    gBoxTable[44] = reinterpret_cast<void *>(&BoxCount);
    gBoxTable[45] = reinterpret_cast<void *>(&BoxGroup);
}

cIGeometryDataContainer *Box(unsigned i) { return reinterpret_cast<cIGeometryDataContainer *>(&gBoxes[i]); }

void ResetSide()
{
    gN = 0;
    gNextId = 0;
    gCounter = nullptr;
    gAliasesOut = nullptr;
    gBox.countCalls = 0;
    for (FakeBox &b : gBoxes) b.count = 10;
    Pool<float>::used = Pool<uint16_t>::used = Pool<cGeometryVector<3>>::used = 0;
}

void NoteAllCounts()
{
    for (const FakeBox &b : gBoxes) Log(0xc1, b.id, b.count);
    Pool<float>::NoteCounts();
    Pool<uint16_t>::NoteCounts();
    Pool<cGeometryVector<3>>::NoteCounts();
}

void InitTables(std::mt19937 &rng)
{
    for (uint32_t &w : gWords) {
        switch (rng() % 8) {
        case 0: w = 0x7fc00000u | (rng() & 0x3fffffu); break; // quiet NaNs, positive
        case 1: w = 0xffc00000u | (rng() & 0x3fffffu); break; // and negative
        case 2: w = rng(); break;
        default: w = Bits(float(int32_t(rng() % 2001) - 1000) / 8.0f); break;
        }
    }
    for (uint16_t &x : gIndices) x = uint16_t(rng());
}

// The filter values: a few small ones, both "any" constants (each also as a plain value of the other filters).
uint32_t FieldValue(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return GeometrySelection::kAny;
    case 1: return GeometrySelection::kAnyIdentity;
    case 2: return GeometrySelection::kAny - 1;
    default: return rng() % 3;
    }
}

const char *const kNames[] = {"Body", "body", "BODY", "hair", "", "bod", "bodyx", "Hair"};

// The fakes' answers for one case: element headers (number at least the uint16 values' bound), linkage headers, groups.
void RandomBox(std::mt19937 &rng)
{
    gSmall = 1 + rng() % 16;
    gBox.answerOther = rng() % 4 == 0;
    gBox.lengthSeed = rng();
    for (int i = 0; i < 16; ++i) {
        for (cGeometryElementHeader *h : {&gBox.given[i], &gBox.answered[i]}) {
            h->mIndex = rng() % 8;
            h->mNumber = gSmall + rng() % 3;
            h->mIdentity = FieldValue(rng);
            h->mRepetition = FieldValue(rng);
            h->mBlockFormat = FieldValue(rng);
            h->mSetFormat = rng() % 4 == 0 ? FieldValue(rng) : rng() % 3;
        }
        for (cGeometryGroupInfo *g : {&gBox.groupGiven[i], &gBox.groupAnswered[i]}) {
            g->mIndex = rng();
            g->mPrimType = FieldValue(rng);
            g->mIndexCount = rng();
            g->mLinkIndex = FieldValue(rng);
            g->mWord10 = int32_t(rng());
            g->mName = kNames[rng() % 8];
            g->mBoneCount = rng();
        }
    }
    for (int i = 0; i < 8; ++i) {
        for (cGeometryLinkageHeader *l : {&gBox.linkGiven[i], &gBox.linkAnswered[i]}) {
            l->mIndex = rng();
            l->mActiveCount = rng() % 10;
            l->mReferencedSize = rng();
        }
    }
    for (uint32_t &c : gBox.counts) c = rng() % 5 == 0 ? 0 : rng() % 12;
}

void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const std::vector<uint32_t> &a,
             const std::vector<uint32_t> &b)
{
    ctx.cases++;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: logs differ at word %u (original %u words, ours %u): original %08x, ours %08x", what, seed,
             unsigned(i), unsigned(a.size()), unsigned(b.size()), i < a.size() ? a[i] : 0u, i < b.size() ? b[i] : 0u);
}

difftest::X87Pc PrecisionOf(uint32_t seed) { return seed & 1 ? difftest::X87Pc::Bits24 : difftest::X87Pc::Bits53; }

// ---- the selections ----

using ElementsCtor = void *(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t);
using ElementsSelect = void(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t);
using GroupsCtor = void *(__thiscall *)(void *, void *, uint32_t, const char *, uint32_t);
using GroupsSelect = void(__thiscall *)(void *, void *, uint32_t, const char *, uint32_t);

// One selection case on one side: `op` 0 elements constructor, 1 elements selection, 2 groups constructor, 3 groups.
std::vector<uint32_t> SelectSide(difftest::Context &ctx, uint32_t seed, unsigned op, bool original)
{
    std::mt19937 rng(seed);
    RandomBox(rng);
    ResetSide();
    const unsigned held = rng() % 4;   // 0 null, 1 the same as the new one, 2-3 another
    const unsigned target = rng() % 3; // the new container
    const unsigned prefill = rng() % 4 == 0 ? 0 : rng() % 9;
    const uint32_t a = FieldValue(rng), b = FieldValue(rng), c = FieldValue(rng), d = FieldValue(rng);
    const char *const name = rng() % 4 == 0 ? nullptr : kNames[rng() % 8];
    alignas(4) uint8_t storage[16];
    std::memset(storage, 0xcd, sizeof(storage));
    auto *const sel = reinterpret_cast<GeometrySelection *>(storage);
    if (op == 1 || op == 3) {
        ::new (&sel->mIndices) std::vector<uint32_t>();
        for (unsigned k = 0; k < prefill; ++k) sel->mIndices.push_back(0x500 + k);
        sel->mContainer = held == 0 ? nullptr : held == 1 ? Box(target) : Box((target + held - 1) % 3);
    }
    cIGeometryDataContainer *const container = Box(target);
    void *result = storage;
    {
        difftest::X87Precision pc(PrecisionOf(seed));
        difftest::AllocationCounter counter;
        gCounter = &counter;
        switch (op) {
        case 0:
            result = original ? ctx.Original<ElementsCtor>(0xaed500)(storage, container, a, b, c, d)
                              : sel->ConstructElements(container, a, b, c, d);
            break;
        case 1:
            if (original) ctx.Original<ElementsSelect>(0xaedbb0)(storage, container, a, b, c, d);
            else sel->SelectElements(container, a, b, c, d);
            break;
        case 2:
            result = original ? ctx.Original<GroupsCtor>(0xaed570)(storage, container, a, name, d)
                              : sel->ConstructGroups(container, a, name, d);
            break;
        default:
            if (original) ctx.Original<GroupsSelect>(0xaedcd0)(storage, container, a, name, d);
            else sel->SelectGroups(container, a, name, d);
            break;
        }
        gCounter = nullptr;
        counter.Stop();
        Log(0xf1, uint32_t(counter.Allocations()), uint32_t(counter.Frees()));
    }
    Log(0xf0, result == storage ? 1u : 0u, IdOfBox(sel->mContainer));
    Log(uint32_t(sel->mIndices.size()), uint32_t(sel->mIndices.capacity()));
    for (uint32_t v : sel->mIndices) Log(v);
    NoteAllCounts();
    sel->mIndices.~vector();
    return std::vector<uint32_t>(gLog, gLog + gN);
}

void RunSelect(difftest::Context &ctx, const char *what, unsigned op, uint32_t salt)
{
    BuildTable();
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        char mark[64];
        std::snprintf(mark, sizeof(mark), "%s seed %u", what, seed);
        ctx.Mark(mark);
        const std::vector<uint32_t> a = SelectSide(ctx, seed * 7919u + salt, op, true);
        const std::vector<uint32_t> b = SelectSide(ctx, seed * 7919u + salt, op, false);
        Compare(ctx, what, seed, a, b);
    }
}

// ---- the accessors (lane 261111l) ----

using HeaderAt = cGeometryElementHeader *(__thiscall *)(void *, cGeometryElementHeader *, uint32_t);
using GroupAt = cGeometryGroupInfo *(__thiscall *)(void *, cGeometryGroupInfo *, uint32_t);

// One accessor case on one side: a selection holding one of the boxes and up to 12 indices (some past the fakes' 16,
// which they wrap), asked for every position of its list. The fakes may answer a buffer other than the out one (whose
// address the code must not return). Notes, per call, whether the out pointer came back and the out's words.
std::vector<uint32_t> AccessSide(difftest::Context &ctx, uint32_t seed, bool groups, bool original)
{
    std::mt19937 rng(seed);
    RandomBox(rng);
    ResetSide();
    alignas(4) uint8_t storage[16];
    auto *const sel = reinterpret_cast<GeometrySelection *>(storage);
    ::new (&sel->mIndices) std::vector<uint32_t>();
    const unsigned n = 1 + rng() % 12;
    for (unsigned k = 0; k < n; ++k) sel->mIndices.push_back(rng() % 3 == 0 ? rng() : rng() % 16);
    sel->mContainer = Box(rng() % 3);
    for (unsigned i = 0; i < n; ++i) {
        if (groups) {
            cGeometryGroupInfo out;
            std::memset(&out, 0xcd, sizeof(out));
            gCallerOut = &out;
            cGeometryGroupInfo *const r = original ? ctx.Original<GroupAt>(0xaedb10)(storage, &out, i)
                                                   : sel->GroupInfo(&out, i);
            Log(0xf0, r == &out ? 1u : 0u);
            const uint32_t *w = reinterpret_cast<const uint32_t *>(&out);
            for (size_t k = 0; k < sizeof(out) / 4; ++k) Log(w[k]);
        } else {
            cGeometryElementHeader out;
            std::memset(&out, 0xcd, sizeof(out));
            gCallerOut = &out;
            cGeometryElementHeader *const r = original ? ctx.Original<HeaderAt>(0xaeda10)(storage, &out, i)
                                                       : sel->ElementHeader(&out, i);
            Log(0xf0, r == &out ? 1u : 0u);
            const uint32_t *w = reinterpret_cast<const uint32_t *>(&out);
            for (size_t k = 0; k < sizeof(out) / 4; ++k) Log(w[k]);
        }
    }
    gCallerOut = nullptr;
    Log(IdOfBox(sel->mContainer), uint32_t(sel->mIndices.size()));
    NoteAllCounts();
    sel->mIndices.~vector();
    return std::vector<uint32_t>(gLog, gLog + gN);
}

void RunAccess(difftest::Context &ctx, const char *what, bool groups, uint32_t salt)
{
    BuildTable();
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        char mark[64];
        std::snprintf(mark, sizeof(mark), "%s seed %u", what, seed);
        ctx.Mark(mark);
        const std::vector<uint32_t> a = AccessSide(ctx, seed * 7919u + salt, groups, true);
        const std::vector<uint32_t> b = AccessSide(ctx, seed * 7919u + salt, groups, false);
        Compare(ctx, what, seed, a, b);
    }
}

// ---- the linked element searches ----

using FindFn = cGeometryElementHeader *(__cdecl *)(cGeometryElementHeader *, cIGeometryDataContainer *, uint32_t, uint32_t,
                                                   uint32_t, uint32_t);
using PositionFn = uint32_t(__cdecl *)(cIGeometryDataContainer *, uint32_t, uint32_t, uint32_t, uint32_t);

std::vector<uint32_t> FindSide(difftest::Context &ctx, uint32_t seed, bool position, bool original)
{
    std::mt19937 rng(seed);
    RandomBox(rng);
    ResetSide();
    const uint32_t linkage = rng() % 8;
    const uint32_t identity = FieldValue(rng), repetition = FieldValue(rng), blockFormat = FieldValue(rng);
    cIGeometryDataContainer *const container = Box(rng() % 3);
    difftest::X87Precision pc(PrecisionOf(seed));
    if (position) {
        const PositionFn fn = original ? ctx.Original<PositionFn>(0xaedfa0) : &FindLinkedElementPosition;
        Log(0xf0, fn(container, linkage, identity, repetition, blockFormat));
    } else {
        cGeometryElementHeader out;
        std::memset(&out, 0xcd, sizeof(out));
        const FindFn fn = original ? ctx.Original<FindFn>(0xaeda40) : &FindLinkedElement;
        Log(0xf0, fn(&out, container, linkage, identity, repetition, blockFormat) == &out ? 1u : 0u);
        const uint32_t *w = reinterpret_cast<const uint32_t *>(&out);
        for (int k = 0; k < 6; ++k) Log(w[k]);
    }
    NoteAllCounts();
    return std::vector<uint32_t>(gLog, gLog + gN);
}

void RunFind(difftest::Context &ctx, const char *what, bool position, uint32_t salt)
{
    BuildTable();
    std::mt19937 tables(salt);
    InitTables(tables);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        char mark[64];
        std::snprintf(mark, sizeof(mark), "%s seed %u", what, seed);
        ctx.Mark(mark);
        const std::vector<uint32_t> a = FindSide(ctx, seed * 7919u + salt, position, true);
        const std::vector<uint32_t> b = FindSide(ctx, seed * 7919u + salt, position, false);
        Compare(ctx, what, seed, a, b);
    }
}

// ---- the readers ----

template <class T> using ReadFn = void(__cdecl *)(cIGeometryDataContainer *, uint32_t, uint32_t, std::vector<uint16_t> *,
                                                   std::vector<T> *);

template <class T> size_t SizeOf(const void *v) { return static_cast<const std::vector<T> *>(v)->size(); }

template <class T> T Prefill(uint32_t k);
template <> float Prefill<float>(uint32_t k) { return float(k) + 0.5f; }
template <> cGeometryVector<3> Prefill<cGeometryVector<3>>(uint32_t k)
{
    cGeometryVector<3> v;
    v.v[0] = float(k), v.v[1] = -float(k), v.v[2] = 2.0f;
    return v;
}

template <class T> std::vector<uint32_t> ReadSide(ReadFn<T> fn, uint32_t seed)
{
    std::mt19937 rng(seed);
    RandomBox(rng);
    ResetSide();
    const uint32_t linkage = rng() % 8, position = rng() % 9;
    cIGeometryDataContainer *const container = Box(rng() % 3);
    std::vector<uint16_t> aliases;
    std::vector<T> values;
    // Reserved before the counter starts (so both sides' blocks come from the same state), then pre-filled.
    const unsigned prefillA = rng() % 3 == 0 ? 0 : rng() % 7, prefillV = rng() % 3 == 0 ? 0 : rng() % 7;
    aliases.reserve(rng() % 3 == 0 ? 0 : 1 + rng() % 12);
    values.reserve(rng() % 3 == 0 ? 0 : 1 + rng() % 12);
    for (unsigned k = 0; k < prefillA; ++k) aliases.push_back(uint16_t(0x700 + k));
    for (unsigned k = 0; k < prefillV; ++k) values.push_back(Prefill<T>(k));
    {
        difftest::X87Precision pc(PrecisionOf(seed));
        difftest::AllocationCounter counter;
        gCounter = &counter;
        gAliasesOut = &aliases;
        gValuesOut = &values;
        gValuesSize = &SizeOf<T>;
        fn(container, linkage, position, &aliases, &values);
        gCounter = nullptr;
        gAliasesOut = nullptr;
        counter.Stop();
        Log(0xf1, uint32_t(counter.Allocations()), uint32_t(counter.Frees()));
        Log(uint32_t(counter.Blocks()));
    }
    Log(0xf0, uint32_t(aliases.size()), uint32_t(values.size()));
    Log(uint32_t(aliases.capacity()), uint32_t(values.capacity()));
    for (uint16_t a : aliases) Log(a);
    for (const T &v : values) LogValue(v);
    NoteAllCounts();
    return std::vector<uint32_t>(gLog, gLog + gN);
}

template <class T> void RunRead(difftest::Context &ctx, const char *what, uint32_t rva, ReadFn<T> ours)
{
    BuildTable();
    std::mt19937 tables(rva);
    InitTables(tables);
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        char mark[64];
        std::snprintf(mark, sizeof(mark), "%s seed %u", what, seed);
        ctx.Mark(mark);
        const std::vector<uint32_t> a = ReadSide<T>(ctx.Original<ReadFn<T>>(rva), seed * 7919u + rva);
        const std::vector<uint32_t> b = ReadSide<T>(ours, seed * 7919u + rva);
        Compare(ctx, what, seed, a, b);
    }
}

} // namespace

DIFF_TEST(SG_Utilities_Geometry_ElementsCtor, 0xaed500) { RunSelect(ctx, "elements ctor", 0, 0xaed500); }

DIFF_TEST(SG_Utilities_Geometry_SelectElements, 0xaedbb0) { RunSelect(ctx, "select elements", 1, 0xaedbb0); }

DIFF_TEST(SG_Utilities_Geometry_GroupsCtor, 0xaed570) { RunSelect(ctx, "groups ctor", 2, 0xaed570); }

DIFF_TEST(SG_Utilities_Geometry_SelectGroups, 0xaedcd0) { RunSelect(ctx, "select groups", 3, 0xaedcd0); }

DIFF_TEST(SG_Utilities_Geometry_ElementHeader, 0xaeda10) { RunAccess(ctx, "element header", false, 0xaeda10); }

DIFF_TEST(SG_Utilities_Geometry_GroupInfo, 0xaedb10) { RunAccess(ctx, "group info", true, 0xaedb10); }

DIFF_TEST(SG_Utilities_Geometry_FindLinkedElement, 0xaeda40) { RunFind(ctx, "find linked", false, 0xaeda40); }

DIFF_TEST(SG_Utilities_Geometry_FindLinkedPosition, 0xaedfa0) { RunFind(ctx, "find position", true, 0xaedfa0); }

DIFF_TEST(SG_Utilities_Geometry_ReadLinkedFloats, 0xaee2c0)
{
    RunRead<float>(ctx, "read floats", 0xaee2c0, &ReadLinkedFloats);
}

DIFF_TEST(SG_Utilities_Geometry_ReadLinkedVectors3, 0xaee570)
{
    RunRead<cGeometryVector<3>>(ctx, "read vectors3", 0xaee570, &ReadLinkedVectors3);
}
