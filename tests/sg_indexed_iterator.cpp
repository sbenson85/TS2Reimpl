// Lane VS: the linked element ranges (src/sg/cIndexedIterator.h), the known vertex element test 0x18da20 and the skin
// extension search 0xaea060.
//
// The ranges: both sides run against one fake container (a raw vtable: slot 32 the element header, 33-37 the element
// values, 41 the linkage's element list, 42 its alias lists) whose iterators are fakes that never free themselves and
// log every call on them (id, slot, argument). Each case fills a range (the original filler or ours), notes the out
// range's three iterators (count, values iterator, index iterator), walks them through their vtables (Get, At, Next,
// Previous, Advance, IsEqual, Clone and Release, QueryInterface), releases them and notes the heap blocks left (both
// sides' allocations: the image's operator new and ours) and every fake's count. So the walk runs the game's
// cIndexedIterator slots on the original side (per type: Clone, Get, At; folded across the five: Release 0xaedb40,
// IsEqual 0xaedad0, Next 0xaedaf0, Previous 0xaedb00, Advance 0xaed5e0) and ours on ours. Per type: floats Clone
// 0xaed690, Get 0xaed930, At 0xaed9b0; vectors Clone 0xaed730 / 0xaed7d0 / 0xaed870 (2, 3, 4), Get 0xaed950 and At
// 0xaed9e0 (folded across the three); words Clone 0xaed5f0, Get 0xaed910, At 0xaed980. Some cases give null members to the container's ranges and clones that
// fail (the constructors' and Release's null checks; no walk then). The header 0xaee030 has its own test (the out
// words, and a container that answers a pointer other than the out it was given).
#include "difftest.h"
#include "free_recorder.h"

#include "sg/cGeometryNodeFormats.h"
#include "sg/cIndexedIterator.h"
#include "sg/cSkinExtension.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// ---- the values the fakes read ----

uint32_t gWords[1024];
uint16_t gIndices[1024];

template <class T> T Value(uint32_t set, int32_t p);
template <> float Value<float>(uint32_t set, int32_t p) { return Float(gWords[(set * 61 + uint32_t(p)) & 1023]); }
template <> uint32_t Value<uint32_t>(uint32_t set, int32_t p) { return gWords[(set * 61 + uint32_t(p)) & 1023]; }
template <> uint16_t Value<uint16_t>(uint32_t set, int32_t p) { return gIndices[(set * 29 + uint32_t(p)) & 1023]; }
template <int N> cGeometryVector<N> VectorValue(uint32_t set, int32_t p)
{
    cGeometryVector<N> v;
    for (int i = 0; i < N; ++i) v.v[i] = Float(gWords[(set * 61 + uint32_t(p) * N + i) & 1023]);
    return v;
}
template <> cGeometryVector<2> Value<cGeometryVector<2>>(uint32_t set, int32_t p) { return VectorValue<2>(set, p); }
template <> cGeometryVector<3> Value<cGeometryVector<3>>(uint32_t set, int32_t p) { return VectorValue<3>(set, p); }
template <> cGeometryVector<4> Value<cGeometryVector<4>>(uint32_t set, int32_t p) { return VectorValue<4>(set, p); }

void LogValue(float v) { Log(Bits(v)); }
void LogValue(uint32_t v) { Log(v); }
void LogValue(uint16_t v) { Log(v); }
template <int N> void LogValue(const cGeometryVector<N> &v)
{
    for (int i = 0; i < N; ++i) Log(Bits(v.v[i]));
}

// ---- the fake iterators (never freed; ids from 1 per side) ----

uint32_t gNextId;
uint32_t gCloneFails; // ids whose Clone answers null (bit id & 31)

template <class T> struct FakeIter final : cIGeometryIterator<T> {
    uint32_t id = 0;    // +4 (the description reads it there)
    uint32_t count = 0;
    uint32_t set = 0;
    int32_t pos = 0;

    bool QueryInterface(uint32_t iid, void **out) override
    {
        Log(id, 0x51, iid);
        if (iid != 7) return false;
        *out = this;
        ++count;
        return true;
    }
    uint32_t AddRef() override { Log(id, 0x41); return ++count; }
    uint32_t Release() override { Log(id, 0x52); return --count; }
    cIGeometryIterator<T> *Clone() override;
    bool IsEqual(const cIGeometryIterator<T> *other) override
    {
        const auto *o = static_cast<const FakeIter *>(other);
        Log(id, 0x45, o->id);
        return o->pos == pos;
    }
    T Get() override { Log(id, 0x47); return Value<T>(set, pos); }
    void Next() override { Log(id, 0x4e); ++pos; }
    void Previous() override { Log(id, 0x50); --pos; }
    void Advance(int32_t n) override { Log(id, 0x56, uint32_t(n)); pos += n; }
    T At(int32_t n) override { Log(id, 0x54, uint32_t(n)); return Value<T>(set, pos + n); }
};

template <class T> struct Pool {
    static std::array<FakeIter<T>, 256> items;
    static size_t used;
    static FakeIter<T> *New(uint32_t set, int32_t pos, uint32_t count)
    {
        if (used == items.size()) std::abort();
        FakeIter<T> *f = &items[used++];
        f->id = ++gNextId;
        f->count = count;
        f->set = set;
        f->pos = pos;
        return f;
    }
    static void NoteCounts()
    {
        for (size_t i = 0; i < used; ++i) Log(0xc0, items[i].id, items[i].count);
    }
};
template <class T> std::array<FakeIter<T>, 256> Pool<T>::items;
template <class T> size_t Pool<T>::used;

template <class T> cIGeometryIterator<T> *FakeIter<T>::Clone()
{
    Log(id, 0x43);
    if (gCloneFails >> (id & 31) & 1) return nullptr;
    FakeIter *c = Pool<T>::New(set, pos, 0);
    Log(c->id);
    return c;
}

uint32_t IdOf(const void *p) { return p ? reinterpret_cast<const uint32_t *>(p)[1] : 0; }

// ---- the fake container ----

struct BoxConfig {
    uint32_t nullMask;     // which of the values / aliases ranges' members are null (3 bits each)
    uint32_t length;       // the ranges' end position
    cGeometryElementHeader given[8], answered[8];
    bool answerOther;      // the header slot answers `answered` rather than the out it filled
};
BoxConfig gBox;

template <class T> void MakeRange(cGeometryRange<T> *out, uint32_t set, uint32_t nulls)
{
    out->mEnd = nulls & 4 ? nullptr : Pool<T>::New(set, int32_t(gBox.length), 1);
    out->mBegin = nulls & 2 ? nullptr : Pool<T>::New(set, 0, 1);
    out->mCurrent = nulls & 1 ? nullptr : Pool<T>::New(set, 0, 1);
}

void __fastcall BoxAbort(void *, void *) { std::abort(); }

cGeometryElementHeader *__fastcall BoxHeader(void *, void *, cGeometryElementHeader *out, uint32_t index)
{
    Log(0xb0, index);
    *out = gBox.given[index & 7];
    return gBox.answerOther ? &gBox.answered[index & 7] : out;
}

template <class T> cGeometryRange<T> *__fastcall BoxValues(void *, void *, cGeometryRange<T> *out, uint32_t index)
{
    Log(0xb1, index);
    MakeRange<T>(out, index, gBox.nullMask & 7);
    return out;
}

cGeometryRange<uint16_t> *__fastcall BoxElements(void *, void *, cGeometryRange<uint16_t> *out, uint32_t linkage)
{
    Log(0xb2, linkage);
    MakeRange<uint16_t>(out, 100 + linkage, 0);
    return out;
}

cGeometryRange<uint16_t> *__fastcall BoxAliases(void *, void *, cGeometryRange<uint16_t> *out, uint32_t linkage,
                                               uint32_t which)
{
    Log(0xb3, linkage, which);
    MakeRange<uint16_t>(out, 200 + linkage * 7 + which, gBox.nullMask >> 3 & 7);
    return out;
}

struct FakeBox {
    void **vtable;
};
void *gBoxTable[64];
FakeBox gFakeBox{gBoxTable};

cIGeometryDataContainer *Box()
{
    for (void *&slot : gBoxTable) slot = reinterpret_cast<void *>(&BoxAbort);
    gBoxTable[32] = reinterpret_cast<void *>(&BoxHeader);
    gBoxTable[33] = reinterpret_cast<void *>(&BoxValues<float>);
    gBoxTable[34] = reinterpret_cast<void *>(&BoxValues<cGeometryVector<2>>);
    gBoxTable[35] = reinterpret_cast<void *>(&BoxValues<cGeometryVector<3>>);
    gBoxTable[36] = reinterpret_cast<void *>(&BoxValues<cGeometryVector<4>>);
    gBoxTable[37] = reinterpret_cast<void *>(&BoxValues<uint32_t>);
    gBoxTable[41] = reinterpret_cast<void *>(&BoxElements);
    gBoxTable[42] = reinterpret_cast<void *>(&BoxAliases);
    return reinterpret_cast<cIGeometryDataContainer *>(&gFakeBox);
}

void ResetSide()
{
    gN = 0;
    gNextId = 0;
    Pool<float>::used = Pool<uint32_t>::used = Pool<uint16_t>::used = 0;
    Pool<cGeometryVector<2>>::used = Pool<cGeometryVector<3>>::used = Pool<cGeometryVector<4>>::used = 0;
}

void NoteAllCounts()
{
    Pool<float>::NoteCounts();
    Pool<uint32_t>::NoteCounts();
    Pool<uint16_t>::NoteCounts();
    Pool<cGeometryVector<2>>::NoteCounts();
    Pool<cGeometryVector<3>>::NoteCounts();
    Pool<cGeometryVector<4>>::NoteCounts();
}

// One of the out range's iterators: its count, values and index (fake ids).
void Describe(const void *it)
{
    if (!it) {
        Log(0xd0);
        return;
    }
    const uint32_t *w = static_cast<const uint32_t *>(it);
    Log(0xd1, w[1]);
    Log(IdOf(reinterpret_cast<const void *>(uintptr_t(w[2]))), IdOf(reinterpret_cast<const void *>(uintptr_t(w[3]))));
}

bool Walkable(const void *it)
{
    if (!it) return false;
    const uint32_t *w = static_cast<const uint32_t *>(it);
    return w[2] != 0 && w[3] != 0;
}

template <class T> void Walk(cGeometryRange<T> &r, std::mt19937 &rng)
{
    cIGeometryIterator<T> *its[3] = {r.mCurrent, r.mBegin, r.mEnd};
    const int steps = 6 + int(rng() % 20);
    for (int s = 0; s < steps; ++s) {
        cIGeometryIterator<T> *x = its[rng() % 3];
        Log(0xe0 + s);
        switch (rng() % 9) {
        case 0: LogValue(x->Get()); break;
        case 1: LogValue(x->At(int32_t(rng() % 9) - 2)); break;
        case 2: x->Next(); break;
        case 3: x->Previous(); break;
        case 4: x->Advance(int32_t(rng() % 7) - 3); break;
        case 5: Log(x->IsEqual(its[rng() % 3]) ? 1u : 0u); break;
        case 6: {
            cIGeometryIterator<T> *c = x->Clone();
            Describe(c);
            if (c) {
                Log(c->AddRef());
                LogValue(c->Get());
                Log(c->IsEqual(x) ? 1u : 0u);
                c->Next();
                Log(c->IsEqual(x) ? 1u : 0u);
                Log(c->Release());
            }
            break;
        }
        case 7: {
            void *out = nullptr;
            const bool ok = x->QueryInterface(1, &out);
            Log(ok ? 1u : 0u, out == x ? 1u : 0u);
            if (ok) Log(x->Release());
            break;
        }
        default: {
            void *out = reinterpret_cast<void *>(0x1234);
            const bool ok = x->QueryInterface(rng() % 2 ? 7 : 0x3ca0e8ab, &out);
            Log(ok ? 1u : 0u, out == reinterpret_cast<void *>(0x1234) ? 1u : 0u);
            Log(x->QueryInterface(1, nullptr) ? 1u : 0u);
            break;
        }
        }
        Describe(x);
    }
}

void InitTables(std::mt19937 &rng)
{
    for (uint32_t &w : gWords) {
        switch (rng() % 8) {
        case 0: w = 0x7f800000u | (rng() & 0x803fffffu) | 1; break; // NaNs with payloads
        case 1: w = rng(); break;
        default: w = Bits(float(int32_t(rng() % 2001) - 1000) / 8.0f); break;
        }
    }
    for (uint16_t &x : gIndices) x = uint16_t(rng() % 3 ? rng() % 40 : rng());
}

void RandomBox(std::mt19937 &rng, bool walk)
{
    // Never the aliases' begin (bit 4): both sides clone the begin iterator, which clones its index unchecked.
    gBox.nullMask = walk ? 0 : rng() % 64 & ~0x10u;
    if (!walk && gBox.nullMask == 0) gBox.nullMask = 1u << (rng() % 4);
    gBox.length = rng() % 12;
    gBox.answerOther = rng() % 4 == 0;
    for (int i = 0; i < 8; ++i) {
        uint32_t *g = reinterpret_cast<uint32_t *>(&gBox.given[i]);
        uint32_t *a = reinterpret_cast<uint32_t *>(&gBox.answered[i]);
        for (int k = 0; k < 6; ++k) g[k] = rng() % 3 ? rng() % 8 : rng(), a[k] = rng() % 3 ? rng() % 8 : rng();
    }
    gCloneFails = walk ? 0 : rng();
}

template <class T> using FillFn = cGeometryRange<T> *(__cdecl *)(cGeometryRange<T> *, cIGeometryDataContainer *, uint32_t,
                                                                  uint32_t);

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

template <class T> std::vector<uint32_t> FillSide(FillFn<T> fill, uint32_t seed, bool walk)
{
    std::mt19937 rng(seed);
    ResetSide();
    const uint32_t linkage = rng() % 5, position = rng() % 6;
    long long blocks;
    {
        difftest::AllocationCounter counter;
        cGeometryRange<T> out;
        std::memset(&out, 0xcd, sizeof(out));
        cGeometryRange<T> *const answer = fill(&out, Box(), linkage, position);
        Log(0xf0, answer == &out ? 1u : 0u);
        Describe(out.mCurrent);
        Describe(out.mBegin);
        Describe(out.mEnd);
        if (walk && Walkable(out.mCurrent) && Walkable(out.mBegin) && Walkable(out.mEnd)) Walk(out, rng);
        Log(0xf1);
        if (out.mCurrent) Log(out.mCurrent->Release());
        if (out.mBegin) Log(out.mBegin->Release());
        if (out.mEnd) Log(out.mEnd->Release());
        counter.Stop();
        blocks = counter.Blocks();
    }
    Log(0xf2, uint32_t(blocks));
    NoteAllCounts();
    return std::vector<uint32_t>(gLog, gLog + gN);
}

template <class T> void RunFill(difftest::Context &ctx, const char *what, uint32_t rva, FillFn<T> ours)
{
    std::mt19937 tables(rva);
    InitTables(tables);
    Box();
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::mt19937 rng(seed * 7919u + rva);
        const bool walk = rng() % 5 != 0;
        RandomBox(rng, walk);
        char mark[64];
        std::snprintf(mark, sizeof(mark), "%s seed %u", what, seed);
        ctx.Mark(mark);
        const std::vector<uint32_t> a = FillSide<T>(ctx.Original<FillFn<T>>(rva), seed, walk);
        const std::vector<uint32_t> b = FillSide<T>(ours, seed, walk);
        Compare(ctx, what, seed, a, b);
    }
}

} // namespace

DIFF_TEST(SG_LinkedRange_Floats, 0xaeddd0) { RunFill<float>(ctx, "floats", 0xaeddd0, &LinkedFloats); }

DIFF_TEST(SG_LinkedRange_Vectors2, 0xaee820) { RunFill<cGeometryVector<2>>(ctx, "vectors2", 0xaee820, &LinkedVectors2); }

DIFF_TEST(SG_LinkedRange_Vectors3, 0xaee9f0) { RunFill<cGeometryVector<3>>(ctx, "vectors3", 0xaee9f0, &LinkedVectors3); }

DIFF_TEST(SG_LinkedRange_Vectors4, 0xaeebc0) { RunFill<cGeometryVector<4>>(ctx, "vectors4", 0xaeebc0, &LinkedVectors4); }

// The words type: also its slots Clone 0xaed5f0, Get 0xaed910, At 0xaed980 through the walk.
DIFF_TEST(SG_LinkedRange_Words, 0xaee0f0) { RunFill<uint32_t>(ctx, "words", 0xaee0f0, &LinkedWords); }

// 0xaee030: the header words, the element list's calls and its release.
DIFF_TEST(SG_LinkedElementHeader, 0xaee030)
{
    using HeaderFn = cGeometryElementHeader *(__cdecl *)(cGeometryElementHeader *, cIGeometryDataContainer *, uint32_t,
                                                          uint32_t);
    std::mt19937 tables(0xaee030);
    InitTables(tables);
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            RandomBox(rng, true);
            ResetSide();
            const uint32_t linkage = rng() % 5, position = rng() % 9;
            cGeometryElementHeader out;
            std::memset(&out, 0xcd, sizeof(out));
            HeaderFn fn = side == 0 ? ctx.Original<HeaderFn>(0xaee030) : &LinkedElementHeader;
            Log(0xf0, fn(&out, Box(), linkage, position) == &out ? 1u : 0u);
            const uint32_t *w = reinterpret_cast<const uint32_t *>(&out);
            for (int k = 0; k < 6; ++k) Log(w[k]);
            NoteAllCounts();
            logs[side].assign(gLog, gLog + gN);
        }
        Compare(ctx, "header", seed, logs[0], logs[1]);
    }
}

// 0x18da20: every identity near the fifteen it knows, and random ones; the answer's low byte (al).
DIFF_TEST(SG_KnownVertexElement, 0x18da20)
{
    using KnownFn = uint8_t(__cdecl *)(uint32_t);
    const KnownFn original = ctx.Original<KnownFn>(0x18da20);
    const uint32_t known[] = {0x5cf2cfe1, 0x3b83078b, 0x114113c3, 0x114113cd, 0x3bd70105, 0x5b830781, 0xcb7206a1, 0xbb8307ab,
                              0x69d92b93, 0x89d92ba0, 0xcb6f3a6a, 0xeb720693, 0xdb830795, 0xdcf2cfdc, 0xfbd70111,
                              0, 0xffffffff, 0x80000000, 0x7fffffff};
    std::vector<uint32_t> values;
    for (uint32_t k : known)
        for (int d = -3; d <= 3; ++d) values.push_back(k + uint32_t(d));
    std::mt19937 rng(0x18da20);
    for (int i = 0; i < 20000; ++i) values.push_back(rng());
    for (uint32_t v : values) {
        ctx.cases++;
        const uint8_t a = original(v);
        const uint8_t b = IsKnownVertexElement(v) ? 1 : 0;
        if (a != b) ctx.Fail("identity %08x: original %u, ours %u", v, a, b);
    }
}

// ---- 0xaea060: the skin extension search ----
namespace {

// An extension as the search sees it: the cISkinExtension face at +0 (its vtable, then its vbptr), the virtual base
// cIExtension at +8 (slot 2 Release, slot 20 GetNext).
struct FakeSkin {
    void **skinTable;
    const int32_t *vbptr;
    void **extTable;
    uint32_t id;
    uint32_t count;
    FakeSkin *next;
};
const int32_t kVbTable[2] = {-4, 4};
void *gSkinTable[32];
void *gExtTable[32];
FakeSkin gSkins[6];

struct SearchPlan {
    uint32_t mode;   // 0 found, 1 not found, 2 not found but out set
    uint32_t answer; // the skin it answers (index)
};
SearchPlan gPlan;

FakeSkin *SkinOfExtension(void *ext) { return reinterpret_cast<FakeSkin *>(static_cast<uint8_t *>(ext) - 8); }

uint32_t __fastcall SkinRelease(void *ext, void *) { FakeSkin *s = SkinOfExtension(ext); Log(0x90, s->id); return --s->count; }

void *__fastcall SkinGetNext(void *ext, void *)
{
    FakeSkin *s = SkinOfExtension(ext);
    Log(0x91, s->id);
    return s->next ? &s->next->extTable : nullptr;
}

void SkinAbort() { std::abort(); }

struct FakeNode {
    void **table;
};
void *gNodeTable[40];
FakeNode gNode{gNodeTable};

bool __fastcall NodeFind(void *, void *, uint32_t iid, void **out, void *start)
{
    Log(0x92, iid, start ? SkinOfExtension(start)->id : 0u);
    Log(*out == nullptr ? 1u : 0u);
    if (gPlan.mode == 1) return false;
    FakeSkin *s = &gSkins[gPlan.answer];
    ++s->count;
    *out = s;
    return gPlan.mode == 0;
}

void BuildSkins(std::mt19937 &rng)
{
    for (void *&p : gSkinTable) p = reinterpret_cast<void *>(&SkinAbort);
    for (void *&p : gExtTable) p = reinterpret_cast<void *>(&SkinAbort);
    gExtTable[2] = reinterpret_cast<void *>(&SkinRelease);
    gExtTable[20] = reinterpret_cast<void *>(&SkinGetNext);
    for (void *&p : gNodeTable) p = reinterpret_cast<void *>(&SkinAbort);
    gNodeTable[24] = reinterpret_cast<void *>(&NodeFind);
    for (int i = 0; i < 6; ++i) {
        FakeSkin &s = gSkins[i];
        s.skinTable = gSkinTable;
        s.vbptr = kVbTable;
        s.extTable = gExtTable;
        s.id = 1 + i;
        s.count = 1 + rng() % 3;
        s.next = rng() % 3 ? &gSkins[rng() % 6] : nullptr;
    }
    gPlan.mode = rng() % 3;
    gPlan.answer = rng() % 6;
}

} // namespace

DIFF_TEST(SG_NextSkinExtension, 0xaea060)
{
    using NextFn = void *(__cdecl *)(void *, void *);
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            BuildSkins(rng);
            gN = 0;
            const uint32_t from = rng() % 7; // 6: no previous
            void *previous = from == 6 ? nullptr : &gSkins[from];
            void *result = side == 0 ? ctx.Original<NextFn>(0xaea060)(&gNode, previous)
                                     : NextSkinExtension(reinterpret_cast<cIObjectGraphNode *>(&gNode),
                                                         static_cast<cISkinExtension *>(previous));
            uint32_t id = 0;
            for (const FakeSkin &s : gSkins)
                if (result == &s) id = s.id;
            Log(0xf0, id, result && !id ? 1u : 0u);
            for (const FakeSkin &s : gSkins) Log(s.count);
            logs[side].assign(gLog, gLog + gN);
        }
        Compare(ctx, "next skin", seed, logs[0], logs[1]);
    }
}
