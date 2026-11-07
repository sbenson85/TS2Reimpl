// Differential tests for cRZMessage2Standard: QueryInterface, Release, the id setter and
// comparisons, the value setters and the flag getters. Objects carry the original vtable, so the
// calls the originals make through it (AddRef, the id getter of an argument, the deleting
// destructor) run original code, except where a recording vtable makes them observable.
#include "difftest.h"
#include "rz/cRZMessage2Standard.h"

#include <windows.h>

#include <cstring>
#include <random>

namespace {

constexpr uint32_t kSize = cRZMessage2Standard::kSize;

using ReturnU32 = uint32_t(__thiscall *)(void *);
using ReturnAl = uint8_t(__thiscall *)(void *);
using ReturnAlPtr = uint8_t(__thiscall *)(void *, const void *);
using ReturnAlUintPtr = uint8_t(__thiscall *)(void *, uint32_t, void **);
using VoidU32 = void(__thiscall *)(void *, uint32_t);

// A random object image: the same bytes go into both sides' objects.
struct State {
    uint8_t bytes[kSize];
    uint32_t refs;
};

State RandomState(std::mt19937 &rng, uint32_t refs)
{
    State s;
    for (uint32_t i = 0; i < kSize; i += 4) {
        uint32_t w = rng();
        std::memcpy(s.bytes + i, &w, 4);
    }
    s.refs = refs;
    return s;
}

cRZMessage2Standard *Make(const void *vtable, const State &s)
{
    auto *p = static_cast<cRZMessage2Standard *>(::operator new(sizeof(cRZMessage2Standard)));
    std::memcpy(static_cast<void *>(p), s.bytes, kSize);
    p->mVTable = vtable;
    p->mRefCount = s.refs;
    return p;
}

// Object contents compared over the whole layout (the vtable and count are part of it).
bool SameObject(const cRZMessage2Standard *a, const cRZMessage2Standard *b)
{
    return std::memcmp(a, b, kSize) == 0;
}

// Deletions are made observable: the object's vtable copy records the deleting-destructor call and
// its flags, and frees when asked to (as the original does).
int gDeleted = 0;
uint32_t gDeleteFlags = 0;
void *__fastcall RecordingDelete(void *self, void * /*edx*/, uint32_t flags)
{
    ++gDeleted;
    gDeleteFlags = flags;
    if (flags & 1) ::operator delete(self);
    return self;
}

struct RecordingVTable {
    void *slots[38];
    explicit RecordingVTable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZMessage2Standard::kVTable), sizeof(slots));
        slots[37] = reinterpret_cast<void *>(&RecordingDelete);
    }
};

// A stand-in argument: slot 4 (the id getter) is ours and counts its calls, so a difference in how
// often each side reads the other message's id shows up. Layout as a message: vtable, then id.
int gGetIdCalls = 0;
uint32_t __fastcall CountingGetId(void *self, void * /*edx*/)
{
    ++gGetIdCalls;
    return static_cast<uint32_t *>(self)[1];
}

void *gFakeSlots[8];

struct FakeMessage {
    void **vtable;
    uint32_t id;
    explicit FakeMessage(uint32_t value) : vtable(gFakeSlots), id(value)
    {
        for (void *&s : gFakeSlots) s = nullptr;
        gFakeSlots[4] = reinterpret_cast<void *>(&CountingGetId);
    }
};

void QueryInterfaceTest(difftest::Context &ctx, std::mt19937 &rng)
{
    auto orig = ctx.Original<ReturnAlUintPtr>(0x1c0660);
    const void *vtable = ctx.image.At(cRZMessage2Standard::kVTable);
    const uint32_t iids[] = {1, 0x65297976, 0xa52294b4, 0, 2, 0x65297977, 0xa52294b5, 0xffffffff};
    for (int i = 0; i < 4000; ++i) {
        uint32_t iid = i < 2000 ? iids[rng() % 8] : static_cast<uint32_t>(rng());
        State s = RandomState(rng, 1 + rng() % 4);
        cRZMessage2Standard *a = Make(vtable, s), *b = Make(vtable, s);
        void *outA = nullptr, *outB = nullptr;
        uint8_t ra = orig(a, iid, &outA);
        uint8_t rb = static_cast<uint8_t>(b->QueryInterface(iid, &outB));
        ++ctx.cases;
        if (ra != rb) ctx.Fail("QueryInterface(%08x): returned %u, ours %u", iid, ra, rb);
        // The out pointer is written only when accepted, and then points at the object.
        if ((outA == nullptr) != (outB == nullptr)) ctx.Fail("QueryInterface(%08x): out written on one side only", iid);
        if (outA && ((outA == a) != (outB == b))) ctx.Fail("QueryInterface(%08x): out points at the wrong object", iid);
        if (!SameObject(a, b)) ctx.Fail("QueryInterface(%08x): object state differs", iid);
        ::operator delete(a);
        ::operator delete(b);
    }
}

void ReleaseTest(difftest::Context &ctx, std::mt19937 &rng)
{
    auto orig = ctx.Original<ReturnU32>(0x1c0804);
    RecordingVTable vtable(ctx);
    for (int i = 0; i < 20000; ++i) {
        uint32_t refs = rng() % 6;
        State s = RandomState(rng, refs);
        cRZMessage2Standard *a = Make(vtable.slots, s), *b = Make(vtable.slots, s);
        gDeleted = 0;
        gDeleteFlags = 0;
        uint32_t ra = orig(a);
        int deletedA = gDeleted;
        uint32_t flagsA = gDeleteFlags;
        gDeleted = 0;
        gDeleteFlags = 0;
        uint32_t rb = b->Release();
        int deletedB = gDeleted;
        uint32_t flagsB = gDeleteFlags;
        ++ctx.cases;
        if (ra != rb || deletedA != deletedB || flagsA != flagsB)
            ctx.Fail("Release refs %u: original %u (deleted %d, flags %u), ours %u (deleted %d, flags %u)", refs, ra, deletedA,
                     flagsA, rb, deletedB, flagsB);
        if (deletedA != deletedB) continue; // leak the survivor rather than double free
        if (!deletedA) {
            if (!SameObject(a, b)) ctx.Fail("Release refs %u: object state differs", refs);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

// Id setter: the stored id, the id flag (set for a nonzero id, cleared for zero), other flags kept.
void SetIdTest(difftest::Context &ctx, std::mt19937 &rng)
{
    auto orig = ctx.Original<VoidU32>(0x1c0915);
    const void *vtable = ctx.image.At(cRZMessage2Standard::kVTable);
    for (int i = 0; i < 20000; ++i) {
        State s = RandomState(rng, 1);
        uint32_t id = (rng() % 4 == 0) ? 0 : static_cast<uint32_t>(rng());
        cRZMessage2Standard *a = Make(vtable, s), *b = Make(vtable, s);
        orig(a, id);
        b->SetId(id);
        ++ctx.cases;
        if (!SameObject(a, b)) ctx.Fail("SetId(%08x): object state differs (flags %02x vs %02x)", id, a->mFlags, b->mFlags);
        ::operator delete(a);
        ::operator delete(b);
    }
}

// Id comparisons: the other message's id is read through its slot 4 exactly once, and the result
// is compared with this message's id (unsigned for IdLess). Ids come from a small range so equal
// and ordered cases both turn up often.
void IdCompareTest(difftest::Context &ctx, std::mt19937 &rng)
{
    auto equals = ctx.Original<ReturnAlPtr>(0x1c03c8);
    auto less = ctx.Original<ReturnAlPtr>(0x1c03de);
    const void *vtable = ctx.image.At(cRZMessage2Standard::kVTable);
    for (int i = 0; i < 20000; ++i) {
        State s = RandomState(rng, 1);
        cRZMessage2Standard *a = Make(vtable, s), *b = Make(vtable, s);
        uint32_t mine = (rng() % 3 == 0) ? static_cast<uint32_t>(rng()) : rng() % 4;
        a->mId = b->mId = mine;
        // The argument is either a counting stand-in or a real message with the original code.
        const bool stand_in = rng() % 2;
        FakeMessage fake(rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4);
        cRZMessage2Standard *real = Make(vtable, s);
        real->mId = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
        const void *arg = stand_in ? static_cast<const void *>(&fake) : static_cast<const void *>(real);
        const uint32_t argId = stand_in ? fake.id : real->mId;
        for (int which = 0; which < 2; ++which) {
            gGetIdCalls = 0;
            uint8_t ra = which == 0 ? equals(a, arg) : less(a, arg);
            int callsA = gGetIdCalls;
            gGetIdCalls = 0;
            bool rb = which == 0 ? b->IdEquals(arg) : b->IdLess(arg);
            int callsB = gGetIdCalls;
            ++ctx.cases;
            if (ra != static_cast<uint8_t>(rb) || callsA != callsB)
                ctx.Fail("%s(%u, %u): original %u (%d calls), ours %d (%d calls)", which == 0 ? "IdEquals" : "IdLess", mine,
                         argId, ra, callsA, rb, callsB);
        }
        if (!SameObject(a, b)) ctx.Fail("id compare: object state differs");
        ::operator delete(a);
        ::operator delete(b);
        ::operator delete(real);
    }
}

// Value setters (slots 11, 16, 21, 26): the value and the flag bit they set; the other bits are kept.
void SetValueTest(difftest::Context &ctx, std::mt19937 &rng)
{
    const uint32_t setters[4] = {0x1c0874, 0x1c0882, 0x1c0890, 0x1c089e};
    const void *vtable = ctx.image.At(cRZMessage2Standard::kVTable);
    for (int n = 0; n < 4; ++n) {
        auto orig = ctx.Original<VoidU32>(setters[n]);
        for (int i = 0; i < 5000; ++i) {
            State s = RandomState(rng, 1);
            uint32_t value = static_cast<uint32_t>(rng());
            cRZMessage2Standard *a = Make(vtable, s), *b = Make(vtable, s);
            orig(a, value);
            switch (n) {
            case 0: b->SetValue0(value); break;
            case 1: b->SetValue1(value); break;
            case 2: b->SetValue2(value); break;
            default: b->SetValue3(value); break;
            }
            ++ctx.cases;
            if (!SameObject(a, b)) ctx.Fail("SetValue%d(%08x): object state differs", n, value);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

// Flag getters over every flag byte. The originals return a 0/1 byte in al and leave the rest of
// eax alone, so only al is compared.
void FlagGetterTest(difftest::Context &ctx)
{
    static const char *const names[6] = {"HasValue0", "HasValue1", "HasValue2", "HasValue3", "HasPtr20", "HasPtr24"};
    static const uint32_t rvas[6] = {0x1c05a0, 0x1c05a8, 0x1c05b1, 0x1c05ba, 0x1c05e0, 0x1c05cc};
    static const uint8_t masks[6] = {0x02, 0x04, 0x08, 0x10, 0x40, 0x80};
    const void *vtable = ctx.image.At(cRZMessage2Standard::kVTable);
    for (int g = 0; g < 6; ++g) {
        auto orig = ctx.Original<ReturnAl>(rvas[g]);
        for (uint32_t flags = 0; flags < 256; ++flags) {
            cRZMessage2Standard *a = Make(vtable, State{{}, 1}), *b = Make(vtable, State{{}, 1});
            a->mFlags = b->mFlags = static_cast<uint8_t>(flags);
            uint8_t ra = orig(a);
            bool rb = false;
            switch (g) {
            case 0: rb = b->HasValue0(); break;
            case 1: rb = b->HasValue1(); break;
            case 2: rb = b->HasValue2(); break;
            case 3: rb = b->HasValue3(); break;
            case 4: rb = b->HasPtr20(); break;
            default: rb = b->HasPtr24(); break;
            }
            ++ctx.cases;
            const uint8_t expected = (flags & masks[g]) ? 1 : 0;
            if (ra != static_cast<uint8_t>(rb) || ra != expected)
                ctx.Fail("%s flags %02x: original %u, ours %u", names[g], flags, ra, static_cast<uint8_t>(rb));
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

} // namespace

DIFF_TEST(cRZMessage2Standard_All, 0x1c0660)
{
    std::mt19937 rng(17);
    QueryInterfaceTest(ctx, rng);
    ReleaseTest(ctx, rng);
    SetIdTest(ctx, rng);
    IdCompareTest(ctx, rng);
    SetValueTest(ctx, rng);
    FlagGetterTest(ctx);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
