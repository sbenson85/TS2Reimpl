// Differential tests for nGZLog's small members (src/rz/nGZLogBackEndSlots.size.cpp): the output back end's slots
// (cOutputBackEnd, shared by four classes), the text formatter's string setter, and the log service's OnTick. Each case builds one
// random state twice, runs the original on one copy and ours on the other, and compares return values
// and side effects. Calls the objects make through their own vtables (AddRef, the deleting destructor,
// the filter's and the result's slots, the service's subobject) go to recording fakes, so the sequence of
// calls is compared as well as the results.
#include "difftest.h"
#include "game/vtable.h"
#include "rz/nGZLog.h"

#include <climits>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using Cob = nGZLog::cLogOutputBackEnd;
using Str = std::string;

// Recorded fake calls, in order: (kind, detail). Each side of a case clears them first.
using Events = std::vector<std::pair<uint32_t, uint32_t>>;
Events gEvents;
const void *gSelf = nullptr; // object under test; fakes record their `this` relative to it

enum : uint32_t { kAddRef = 1, kDelete, kDeleteFlags, kFilterQI, kFilterSlot3, kFilterSlot4, kTmpSlot10, kTmpRelease, kSub26, kSubArg, kSub29 };

uint32_t Rel(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gSelf));
}

// Return values the fakes give (set per case). The byte is what a bool-returning slot leaves in al.
uint32_t gRetU32 = 0;
uint8_t gRetByte = 0;
uint32_t gRetFilterQI = 0;

// An object is a pointer to a word holding its vtable pointer.
struct FakeObject {
    const void *vtable;
};

// The filter (its slots 0, 3, 4) and the object its QueryInterface hands back (its slots 2, 10).
void *gFilterSlots[5];
void *gTmpSlots[11];
FakeObject gFilterObject;
FakeObject gTmpObject;

// Fakes. __fastcall with a dummy edx is __thiscall with the same stack arguments.
uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gEvents.emplace_back(kAddRef, Rel(self));
    return 0x5a5a0001;
}
void __fastcall FakeDelete(void *self, void *, uint32_t flags)
{
    gEvents.emplace_back(kDelete, Rel(self));
    gEvents.emplace_back(kDeleteFlags, flags);
}
uint32_t __fastcall FakeFilterQI(void *, void *, uint32_t riid, void **ppv)
{
    gEvents.emplace_back(kFilterQI, riid);
    *ppv = &gTmpObject;
    return gRetFilterQI;
}
uint8_t __fastcall FakeFilterSlot3(void *, void *, uint32_t arg)
{
    gEvents.emplace_back(kFilterSlot3, arg);
    return gRetByte;
}
uint8_t __fastcall FakeFilterSlot4(void *, void *, uint32_t arg)
{
    gEvents.emplace_back(kFilterSlot4, arg);
    return gRetByte;
}
uint32_t __fastcall FakeTmpSlot10(void *, void *, uint32_t arg)
{
    gEvents.emplace_back(kTmpSlot10, arg);
    return gRetU32;
}
uint32_t __fastcall FakeTmpRelease(void *, void *)
{
    gEvents.emplace_back(kTmpRelease, 0);
    return 0;
}
void __fastcall FakeSub26(void *self, void *, uint32_t arg)
{
    gEvents.emplace_back(kSub26, Rel(self));
    gEvents.emplace_back(kSubArg, arg);
}
void __fastcall FakeSub29(void *self, void *)
{
    gEvents.emplace_back(kSub29, Rel(self));
}

// Slots for the filter and the result object are filled once, before the tests run.
void InitFakeSlots()
{
    gFilterSlots[0] = reinterpret_cast<void *>(&FakeFilterQI);
    gFilterSlots[3] = reinterpret_cast<void *>(&FakeFilterSlot3);
    gFilterSlots[4] = reinterpret_cast<void *>(&FakeFilterSlot4);
    gFilterObject.vtable = gFilterSlots;
    gTmpSlots[2] = reinterpret_cast<void *>(&FakeTmpRelease);
    gTmpSlots[10] = reinterpret_cast<void *>(&FakeTmpSlot10);
    gTmpObject.vtable = gTmpSlots;
}

struct ObVTable {
    void *slots[25];
};

// The original back end vtable, with the object's own AddRef and deleting destructor replaced by fakes.
ObVTable OriginalSlots(const difftest::Context &ctx)
{
    ObVTable v;
    std::memcpy(v.slots, ctx.image.At(Cob::kVTable), sizeof(v.slots));
    return v;
}
ObVTable RecordingSlots(const difftest::Context &ctx)
{
    ObVTable v = OriginalSlots(ctx);
    v.slots[1] = reinterpret_cast<void *>(&FakeAddRef);
    v.slots[Cob::kSlotDelete] = reinterpret_cast<void *>(&FakeDelete);
    return v;
}

struct Init {
    uint32_t refs = 0;
    void *filter = nullptr;
    void *gate = nullptr;
    uint8_t enabled = 0;
    uint8_t flag = 0;
    int32_t interval = 1;
    uint32_t counter = 0;
    std::string text;
};

std::string RandomText(std::mt19937 &rng)
{
    size_t n = rng() % 3 == 0 ? rng() % 4 : rng() % 40;
    std::string s;
    for (size_t i = 0; i < n; ++i) s.push_back(static_cast<char>('a' + rng() % 26));
    return s;
}

Init RandomInit(std::mt19937 &rng)
{
    Init s;
    s.refs = rng();
    s.filter = (rng() & 1) ? static_cast<void *>(&gFilterObject) : nullptr;
    s.gate = (rng() & 1) ? static_cast<void *>(&gFilterObject) : nullptr; // only tested for null
    s.enabled = (rng() % 4 == 0) ? 0 : static_cast<uint8_t>(rng());
    s.flag = static_cast<uint8_t>(rng());
    s.interval = static_cast<int32_t>(rng() % 9) - 4;
    s.counter = rng();
    s.text = RandomText(rng);
    return s;
}

// Heap objects with the fields filled in. Unused bytes are 0xCC so a stray read shows up.
Cob *Make(const void *vtable, const Init &s)
{
    auto *p = static_cast<Cob *>(::operator new(sizeof(Cob)));
    std::memset(static_cast<void *>(p), 0xCC, sizeof(Cob));
    p->mVTable = const_cast<void *>(vtable);
    p->mRefCount = s.refs;
    p->mFilter = s.filter;
    p->mFormatter = s.gate;
    p->mEnabled = s.enabled;
    p->mFlag = s.flag;
    p->mInterval = s.interval;
    p->mCounter = s.counter;
    new (&p->mText.mString) Str(s.text);
    return p;
}

void Free(Cob *p)
{
    p->mText.mString.~Str();
    ::operator delete(p);
}

// Compares the fields the reimplemented functions can change, the string including its capacity.
bool SameObject(const Cob *x, const Cob *y)
{
    return x->mVTable == y->mVTable && x->mRefCount == y->mRefCount && x->mFilter == y->mFilter &&
           x->mFormatter == y->mFormatter && x->mEnabled == y->mEnabled && x->mFlag == y->mFlag &&
           x->mInterval == y->mInterval && x->mCounter == y->mCounter && x->mText.mString == y->mText.mString &&
           x->mText.mString.size() == y->mText.mString.size() && x->mText.mString.capacity() == y->mText.mString.capacity();
}

// Where a returned pointer points, relative to its object (pointers themselves differ between the copies).
uint32_t RelTo(const void *p, const void *obj)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(obj));
}

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// Keeps the one-time fake setup from running once per test body.
struct FakeSetup {
    FakeSetup()
    {
        InitFakeSlots();
    }
};
FakeSetup gFakeSetup;

} // namespace

DIFF_TEST(GZLog_OutputBackEnd_SetText, 0xc9f4f4)
{
    using Fn = Str *(__fastcall *)(void *, void *, const char *);
    auto orig = ctx.Original<Fn>(0xc9f4f4);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        Init s = RandomInit(rng);
        std::string text = RandomText(rng);
        Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
        gSelf = a;
        Str *ra = orig(a, nullptr, text.c_str());
        gSelf = b;
        Str *rb = b->SetName(text.c_str());
        ++ctx.cases;
        if (RelTo(ra, a) != RelTo(rb, b)) ctx.Fail("SetText return: original %u, ours %u", RelTo(ra, a), RelTo(rb, b));
        if (!SameObject(a, b) || a->mText.mString != text) ctx.Fail("SetText object state differs for text '%s'", text.c_str());
        Free(a);
        Free(b);
    }
}

DIFF_TEST(GZLog_OutputBackEnd_GetText, 0xc9f4ad)
{
    using Fn = const char *(__fastcall *)(void *, void *);
    auto orig = ctx.Original<Fn>(0xc9f4ad);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(12);
    for (int i = 0; i < 20000; ++i) {
        Init s = RandomInit(rng);
        Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
        gSelf = a;
        const char *ra = orig(a, nullptr);
        gSelf = b;
        const char *rb = b->Name();
        ++ctx.cases;
        // Short strings are returned as a pointer into the object, long ones as a heap pointer: compare which, and the text.
        bool inlineA = ra == reinterpret_cast<const char *>(a) + 0x18, inlineB = rb == reinterpret_cast<const char *>(b) + 0x18;
        if (inlineA != inlineB || std::strcmp(ra, rb) != 0) ctx.Fail("GetText differs: inline %d vs %d, text '%s' vs '%s'", inlineA, inlineB, ra, rb);
        Free(a);
        Free(b);
    }
}

DIFF_TEST(GZLog_OutputBackEnd_SetFlag, 0xc9f4fc)
{
    using Fn = void(__fastcall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<Fn>(0xc9f4fc);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(13);
    // Every low byte, with random upper bits (the original stores only al).
    for (uint32_t low = 0; low < 256; ++low) {
        for (int i = 0; i < 40; ++i) {
            Init s = RandomInit(rng);
            uint32_t arg = (rng() & 0xFFFFFF00u) | low;
            Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
            gSelf = a;
            orig(a, nullptr, arg);
            gSelf = b;
            b->SetSynchronous(static_cast<uint8_t>(arg));
            ++ctx.cases;
            if (!SameObject(a, b) || a->mFlag != static_cast<uint8_t>(low)) ctx.Fail("SetFlag(%08x): flag %02x vs %02x", arg, a->mFlag, b->mFlag);
            Free(a);
            Free(b);
        }
    }
}

DIFF_TEST(GZLog_OutputBackEnd_TickEveryN, 0xc9f446)
{
    using Fn = bool(__fastcall *)(void *, void *);
    auto orig = ctx.Original<Fn>(0xc9f446);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(14);
    // Intervals other than zero (the division traps) and -1 with a count of INT_MIN (the quotient overflows).
    const int32_t intervals[] = {1, 2, 3, 5, 7, 16, 100, 1000, 0x7fffffff, -1, -2, -3, -7, -1000, -0x7fffffff, INT_MIN};
    const uint32_t counts[] = {0, 1, 2, 3, 0x7ffffffe, 0x7fffffff, 0x80000000u, 0x80000001u, 0xfffffffeu, 0xffffffffu};
    for (int i = 0; i < 40000; ++i) {
        Init s = RandomInit(rng);
        s.interval = intervals[rng() % (sizeof(intervals) / sizeof(intervals[0]))];
        s.counter = (rng() % 2) ? counts[rng() % (sizeof(counts) / sizeof(counts[0]))] : rng();
        if (s.interval == -1 && s.counter == 0x80000000u) continue;
        Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
        gSelf = a;
        bool ra = orig(a, nullptr);
        gSelf = b;
        bool rb = b->FlushDue();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("TickEveryN(count %u, interval %d): original %d, ours %d", s.counter, s.interval, ra, rb);
        if (!SameObject(a, b)) ctx.Fail("TickEveryN(count %u, interval %d): counter %u vs %u", s.counter, s.interval, a->mCounter, b->mCounter);
        Free(a);
        Free(b);
    }
}

DIFF_TEST(GZLog_OutputBackEnd_Release, 0xc9f4dd)
{
    using Fn = uint32_t(__fastcall *)(void *, void *);
    auto orig = ctx.Original<Fn>(0xc9f4dd);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(15);
    const uint32_t counts[] = {0, 1, 2, 3, 5, 0x7fffffff, 0x80000000u, 0xffffffffu};
    for (int i = 0; i < 20000; ++i) {
        Init s = RandomInit(rng);
        s.refs = (rng() % 2) ? counts[rng() % (sizeof(counts) / sizeof(counts[0]))] : rng() % 6;
        Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
        gSelf = a;
        gEvents.clear();
        uint32_t ra = orig(a, nullptr);
        Events ea = gEvents;
        gSelf = b;
        gEvents.clear();
        uint32_t rb = b->Release();
        Events eb = gEvents;
        ++ctx.cases;
        if (ra != rb || ea != eb) ctx.Fail("Release(refs %u): return %u vs %u, %zu vs %zu calls", s.refs, ra, rb, ea.size(), eb.size());
        if (!SameObject(a, b)) ctx.Fail("Release(refs %u): counts %u vs %u", s.refs, a->mRefCount, b->mRefCount);
        Free(a);
        Free(b);
    }
}

DIFF_TEST(GZLog_OutputBackEnd_QueryInterface, 0xc9f4b9)
{
    using Fn = bool(__fastcall *)(void *, void *, uint32_t, void **);
    auto orig = ctx.Original<Fn>(0xc9f4b9);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(16);
    const uint32_t iids[] = {0, 1, 2, 0x49b84afa, 0x49b84afb, 0x49b84afc, 0xffffffffu, 0x49b84afb - 1, 0x49b84afb + 0x100};
    for (int i = 0; i < 20000; ++i) {
        Init s = RandomInit(rng);
        uint32_t riid = (rng() % 3 == 0) ? rng() : iids[rng() % (sizeof(iids) / sizeof(iids[0]))];
        Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
        void *pa = reinterpret_cast<void *>(0x5151), *pb = reinterpret_cast<void *>(0x5151);
        gSelf = a;
        gEvents.clear();
        bool ra = orig(a, nullptr, riid, &pa);
        Events ea = gEvents;
        gSelf = b;
        gEvents.clear();
        bool rb = b->QueryInterface(riid, &pb);
        Events eb = gEvents;
        ++ctx.cases;
        // What *ppv holds: this, or the untouched sentinel (compared by value, since it is not in the object).
        uint32_t tagA = pa == a ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pa));
        uint32_t tagB = pb == b ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pb));
        if (ra != rb || ea != eb || tagA != tagB)
            ctx.Fail("QueryInterface(%08x): %d vs %d, ppv %08x vs %08x", riid, ra, rb, tagA, tagB);
        if (!SameObject(a, b)) ctx.Fail("QueryInterface(%08x): object state differs", riid);
        Free(a);
        Free(b);
    }
}

DIFF_TEST(GZLog_OutputBackEnd_AcceptFilter3, 0xc9f45b)
{
    using Fn = bool(__fastcall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<Fn>(0xc9f45b);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(17);
    const uint8_t results[] = {0, 1, 2, 0x80, 0xff};
    for (int i = 0; i < 20000; ++i) {
        Init s = RandomInit(rng);
        gRetByte = results[rng() % 5];
        uint32_t arg = rng();
        Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
        gSelf = a;
        gEvents.clear();
        bool ra = orig(a, nullptr, arg);
        Events ea = gEvents;
        gSelf = b;
        gEvents.clear();
        bool rb = b->AcceptFirst(arg);
        Events eb = gEvents;
        ++ctx.cases;
        if (ra != rb || ea != eb) ctx.Fail("AcceptFilter3: %d vs %d, %zu vs %zu calls", ra, rb, ea.size(), eb.size());
        if (!SameObject(a, b)) ctx.Fail("AcceptFilter3: object state differs");
        Free(a);
        Free(b);
    }
}

DIFF_TEST(GZLog_OutputBackEnd_AcceptFilter4, 0xc9f484)
{
    using Fn = bool(__fastcall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<Fn>(0xc9f484);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(18);
    const uint8_t results[] = {0, 1, 2, 0x80, 0xff};
    for (int i = 0; i < 20000; ++i) {
        Init s = RandomInit(rng);
        gRetByte = results[rng() % 5];
        uint32_t arg = rng();
        Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
        gSelf = a;
        gEvents.clear();
        bool ra = orig(a, nullptr, arg);
        Events ea = gEvents;
        gSelf = b;
        gEvents.clear();
        bool rb = b->Accept(arg);
        Events eb = gEvents;
        ++ctx.cases;
        if (ra != rb || ea != eb) ctx.Fail("AcceptFilter4: %d vs %d, %zu vs %zu calls", ra, rb, ea.size(), eb.size());
        if (!SameObject(a, b)) ctx.Fail("AcceptFilter4: object state differs");
        Free(a);
        Free(b);
    }
}

DIFF_TEST(GZLog_OutputBackEnd_Forward, 0xc9f405)
{
    using Fn = uint32_t(__fastcall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<Fn>(0xc9f405);
    ObVTable slots = RecordingSlots(ctx);
    std::mt19937 rng(19);
    for (int i = 0; i < 20000; ++i) {
        Init s = RandomInit(rng); // a null filter returns 0 without calls; the fake filter always answers its QueryInterface
        gRetU32 = rng();
        gRetFilterQI = rng() & 1;
        uint32_t arg = rng();
        Cob *a = Make(slots.slots, s), *b = Make(slots.slots, s);
        gSelf = a;
        gEvents.clear();
        uint32_t ra = orig(a, nullptr, arg);
        Events ea = gEvents;
        gSelf = b;
        gEvents.clear();
        uint32_t rb = b->GroupLevel(arg);
        Events eb = gEvents;
        ++ctx.cases;
        if (ra != rb || ea != eb) ctx.Fail("Forward(%u): return %u vs %u, %zu vs %zu calls", arg, ra, rb, ea.size(), eb.size());
        if (!SameObject(a, b)) ctx.Fail("Forward: object state differs");
        Free(a);
        Free(b);
    }
}

DIFF_TEST(GZLog_FormatterText_SetText, 0xc9e41e)
{
    using Fn = Str *(__fastcall *)(void *, void *, const char *);
    auto orig = ctx.Original<Fn>(0xc9e41e);
    std::mt19937 rng(20);
    for (int i = 0; i < 20000; ++i) {
        // The formatter's string sits at +0x10; the rest of the object is unused here.
        alignas(8) uint8_t fa[0x40], fb[0x40];
        std::memset(fa, 0xCC, sizeof(fa));
        std::memset(fb, 0xCC, sizeof(fb));
        std::string before = RandomText(rng);
        new (fa + 0x10) Str(before);
        new (fb + 0x10) Str(before);
        std::string text = RandomText(rng);
        gSelf = fa;
        Str *ra = orig(fa, nullptr, text.c_str());
        gSelf = fb;
        Str *rb = reinterpret_cast<nGZLog::cLogFormatterText *>(fb)->SetName(text.c_str());
        ++ctx.cases;
        auto *sa = reinterpret_cast<Str *>(fa + 0x10);
        auto *sb = reinterpret_cast<Str *>(fb + 0x10);
        if (RelTo(ra, fa) != RelTo(rb, fb)) ctx.Fail("Formatter SetText return: +%u vs +%u", RelTo(ra, fa), RelTo(rb, fb));
        if (*sa != *sb || *sb != text || sa->capacity() != sb->capacity()) ctx.Fail("Formatter SetText text differs for '%s'", text.c_str());
        sa->~Str();
        sb->~Str();
    }
}

DIFF_TEST(GZLog_Service_OnTick, 0xc9dd3a)
{
    using Fn = bool(__fastcall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<Fn>(0xc9dd3a);
    // The subobject's vtable: the original's, with slots 26 and 29 (the calls OnTick makes) faked.
    void *slotsA[30], *slotsB[30];
    std::memcpy(slotsA, ctx.image.At(nGZLog::cLogService::kServiceVTable), sizeof(slotsA));
    std::memcpy(slotsB, slotsA, sizeof(slotsA));
    slotsA[26] = slotsB[26] = reinterpret_cast<void *>(&FakeSub26);
    slotsA[29] = slotsB[29] = reinterpret_cast<void *>(&FakeSub29);
    std::mt19937 rng(21);
    for (int i = 0; i < 20000; ++i) {
        alignas(8) uint8_t sa[0x80], sb[0x80];
        std::memset(sa, 0xCC, sizeof(sa));
        std::memset(sb, 0xCC, sizeof(sb));
        *reinterpret_cast<void **>(sa + nGZLog::cLogService::kServiceSubobjectOffset) = slotsA;
        *reinterpret_cast<void **>(sb + nGZLog::cLogService::kServiceSubobjectOffset) = slotsB;
        uint32_t arg = rng();
        gSelf = sa;
        gEvents.clear();
        bool ra = orig(sa, nullptr, arg);
        Events ea = gEvents;
        gSelf = sb;
        gEvents.clear();
        bool rb = nGZLog::cLogServiceAdapters::OnTick(reinterpret_cast<nGZLog::cLogService *>(sb), nullptr, arg);
        Events eb = gEvents;
        ++ctx.cases;
        if (ra != rb || ea != eb) ctx.Fail("OnTick(%u): %d vs %d, %zu vs %zu calls", arg, ra, rb, ea.size(), eb.size());
    }
}
