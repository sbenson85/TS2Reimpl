// Differential test for the formats' lower bound 0x18ad50 nGZGraphic4::FindFormatLowerBound (lane VT,
// src/gz/GraphicModeSearch.cpp): the first format of a range that is the wanted one or after it in the formats' order.
//
// Every object it touches is a recording fake: the iterators (slot 2 Release, 4 IsEqual, 5 Get, 6 Next) and the wanted
// format (slot 10 Less(format, strict), answering a generated pattern of yes and no). Compared: the answer and every call
// in order with its receiver and arguments.
//
// The generators reach every branch: empty ranges and up to six entries, ranges with and without a begin iterator, the
// wanted format listed (at any place) or not, Less answering yes at any place or never (the answer then the last format
// walked over), and null entries.
#include "difftest.h"

#include "gz/GraphicModeSearch.h"

#include <cstring>
#include <vector>

namespace {

using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cPixelFormatRange;

struct Call {
    int slot;
    const void *self;
    uint32_t args[2];
};
std::vector<Call> gCalls;

void Record(int slot, const void *self, uint32_t a0 = 0, uint32_t a1 = 0)
{
    gCalls.push_back(Call{slot, self, {a0, a1}});
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(Call)) != 0) return false;
    return true;
}

uint32_t gSeed = 1;
uint32_t Rand()
{
    gSeed = gSeed * 1103515245u + 12345u;
    return gSeed >> 8;
}

constexpr int kMaxEntries = 6;

// The formats: plain objects (only their addresses are compared); the wanted one answers Less from a bit pattern
// indexed by the format's place.
struct Format {
    void *vtable;
    int place;
};
void *gWantedTable[12];
Format gFormats[kMaxEntries + 1]; // the last one: wanted, when it is not listed
Format *gListed[kMaxEntries];
uint32_t gLessAnswers = 0;

bool __fastcall WantedLess(void *self, void *, const Format *other, bool strict)
{
    Record(10, self, uint32_t(reinterpret_cast<uintptr_t>(other)), strict);
    if (!other) return (gLessAnswers >> 31) & 1;
    return (gLessAnswers >> other->place) & 1;
}

struct Iterator {
    void *vtable;
    int pos;
};
void *gIterTable[8];
Iterator gCurrent, gBegin, gEnd;

uint32_t __fastcall IterRelease(void *self, void *)
{
    Record(2, self);
    return 0;
}
bool __fastcall IterIsEqual(void *self, void *, const Iterator *other)
{
    Record(4, self, uint32_t(reinterpret_cast<uintptr_t>(other)));
    return static_cast<Iterator *>(self)->pos == other->pos;
}
void *__fastcall IterGet(void *self, void *)
{
    Record(5, self);
    return gListed[static_cast<Iterator *>(self)->pos];
}
void __fastcall IterNext(void *self, void *)
{
    Record(6, self);
    ++static_cast<Iterator *>(self)->pos;
}

void Setup()
{
    std::memset(gIterTable, 0, sizeof(gIterTable));
    gIterTable[2] = reinterpret_cast<void *>(&IterRelease);
    gIterTable[4] = reinterpret_cast<void *>(&IterIsEqual);
    gIterTable[5] = reinterpret_cast<void *>(&IterGet);
    gIterTable[6] = reinterpret_cast<void *>(&IterNext);
    std::memset(gWantedTable, 0, sizeof(gWantedTable));
    gWantedTable[10] = reinterpret_cast<void *>(&WantedLess);
}

cPixelFormatRange MakeRange(int n, bool withBegin)
{
    gCurrent = {gIterTable, 0};
    gBegin = {gIterTable, 0};
    gEnd = {gIterTable, n};
    cPixelFormatRange range;
    range.mCurrent = reinterpret_cast<decltype(range.mCurrent)>(&gCurrent);
    range.mBegin = withBegin ? reinterpret_cast<decltype(range.mBegin)>(&gBegin) : nullptr;
    range.mEnd = reinterpret_cast<decltype(range.mEnd)>(&gEnd);
    return range;
}

} // namespace

DIFF_TEST(GraphicModeSearch_FindFormatLowerBound, 0x18ad50)
{
    using Fn = cIPixelFormat *(__cdecl *)(cPixelFormatRange, cIPixelFormat *);
    const Fn orig = ctx.Original<Fn>(0x18ad50);
    const Fn ours = &nGZGraphic4::FindFormatLowerBound;
    Setup();
    gSeed = 0x18ad50;
    for (int round = 0; round < 6000; ++round) {
        const int n = int(Rand() % (kMaxEntries + 1));
        // Every format is a "wanted" one too (its vtable answers Less), so any of them may be the one asked for.
        for (int i = 0; i <= kMaxEntries; ++i) gFormats[i] = {gWantedTable, i};
        for (int i = 0; i < n; ++i) gListed[i] = Rand() % 8 == 0 ? nullptr : &gFormats[i];
        // The wanted format: one listed (found by identity), or one that is not.
        const int wantedAt = n && Rand() % 2 ? int(Rand() % n) : kMaxEntries;
        Format *const wanted = &gFormats[wantedAt];
        switch (Rand() % 4) {
        case 0: gLessAnswers = 0; break;                           // never after: the last one walked over
        case 1: gLessAnswers = ~0u; break;                         // the first one
        default: gLessAnswers = Rand() & ~1u; break;               // somewhere in the range
        }
        const bool withBegin = Rand() % 4 != 0;
        auto *const want = reinterpret_cast<cIPixelFormat *>(wanted);
        gCalls.clear();
        cIPixelFormat *const a = orig(MakeRange(n, withBegin), want);
        const std::vector<Call> callsA = gCalls;
        gCalls.clear();
        cIPixelFormat *const b = ours(MakeRange(n, withBegin), want);
        ++ctx.cases;
        if (a != b || !SameCalls(callsA, gCalls))
            ctx.Fail("lower bound round %d (%d formats, wanted at %d, less %08x): original %p (%zu calls), ours %p "
                     "(%zu calls)",
                     round, n, wantedAt, gLessAnswers, static_cast<void *>(a), callsA.size(), static_cast<void *>(b),
                     gCalls.size());
    }
}
