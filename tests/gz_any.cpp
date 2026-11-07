// Differential tests for the cIGZAny values (src/gz/cGZAny.*): the scalar Anys, AnyAny and AnyString. AnyStringSet
// and its set are in tests/gz_any_string_set.cpp.
//
// Calls the values make through other objects' vtables (GetType of the source and of this in Assign, AddRef /
// Release / CompareTo of the held strings) reach recording fakes, and the logs are compared with the results.
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZAny.h"
#include "heap_poison.h"
#include "rz/cRZString.h"

#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

std::vector<uint32_t> gEvents;

// An Any whose vtable is a fake: slot 4 logs the object's id and returns its type. Assign reads only +4.
struct FakeAny {
    void **vt;
    uint32_t value; // +4
    uint32_t type;
    uint32_t id;
};

uint32_t __fastcall FakeGetType(void *self, void *)
{
    auto *any = static_cast<FakeAny *>(self);
    gEvents.push_back(0x400 + any->id);
    return any->type;
}

void *gFakeAnyVTable[6] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&FakeGetType), nullptr};

// A counted string: slot 1 AddRef, slot 2 Release, slot 15 CompareTo(other, caseSensitive), all logged.
struct FakeString {
    void **vt;
    uint32_t refs;
    uint32_t id;
};

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    auto *s = static_cast<FakeString *>(self);
    gEvents.push_back(0x100 + s->id);
    return ++s->refs;
}

uint32_t __fastcall FakeRelease(void *self, void *)
{
    auto *s = static_cast<FakeString *>(self);
    gEvents.push_back(0x200 + s->id);
    return --s->refs;
}

int32_t __fastcall FakeCompareTo(void *self, void *, const void *other, uint32_t caseSensitive)
{
    auto *s = static_cast<FakeString *>(self);
    auto *o = static_cast<const FakeString *>(other);
    gEvents.push_back(0x300 + s->id * 16 + o->id);
    gEvents.push_back(caseSensitive);
    return static_cast<int32_t>(s->id * 7) - static_cast<int32_t>(o->id * 5) + static_cast<int32_t>(caseSensitive << 20);
}

void *gFakeStringVTable[16];

void SetupFakeStrings()
{
    gFakeStringVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gFakeStringVTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gFakeStringVTable[15] = reinterpret_cast<void *>(&FakeCompareTo);
}

// The value types and their payload width in bytes, for the random choices below.
const uint32_t kTypes[] = {gzany::kTypeUint32, gzany::kTypeSint32, gzany::kTypeFloat32, gzany::kTypeBoolean,
                           gzany::kTypeString, gzany::kTypeStringSet, gzany::kTypeAnyAny, 0x12345678};

uint32_t RandomPayload(std::mt19937 &rng)
{
    switch (rng() % 6) {
    case 0: return rng() % 3;
    case 1: return 0x80000000u + rng() % 3 - 1;
    case 2: return 0xffffffffu - rng() % 2;
    default: return rng();
    }
}

// Floats with the edge cases: zeros of both signs, infinities, NaNs, denormals, equal values.
uint32_t RandomFloatBits(std::mt19937 &rng)
{
    static const uint32_t special[] = {0x00000000, 0x80000000, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00001,
                                       0x00000001, 0x3f800000, 0xbf800000, 0x7f7fffff};
    if (rng() % 3 == 0) return special[rng() % 10];
    float f = std::uniform_real_distribution<float>(-4.0f, 4.0f)(rng);
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// ---- Assign: same type copies the payload, else nothing; GetType of source, then of this ----

template <typename Ours> void RunScalarAssign(difftest::Context &ctx, uint32_t rva, bool byteOnly, uint32_t seed)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, const void *)>(rva);
    std::mt19937 rng(seed);
    for (int i = 0; i < 20000; ++i) {
        uint32_t typeThis = kTypes[rng() % 8], typeSource = rng() % 2 ? typeThis : kTypes[rng() % 8];
        uint32_t valueThis = rng(), valueSource = rng();
        bool self = rng() % 16 == 0;
        uint8_t result[2];
        uint32_t after[2];
        std::vector<uint32_t> events[2];
        for (int side = 0; side < 2; ++side) {
            FakeAny target{gFakeAnyVTable, valueThis, typeThis, 1};
            FakeAny source{gFakeAnyVTable, valueSource, typeSource, 2};
            const void *from = self ? &target : &source;
            gEvents.clear();
            result[side] = side == 0 ? orig(&target, from) : reinterpret_cast<Ours *>(&target)->Assign(from);
            after[side] = target.value;
            events[side] = gEvents;
            if (source.value != valueSource) ctx.Fail("Assign wrote the source");
        }
        ++ctx.cases;
        if (result[0] != result[1] || after[0] != after[1] || events[0] != events[1])
            ctx.Fail("Assign %08x <- %08x: %u/%u, value %08x/%08x", typeThis, typeSource, result[0], result[1], after[0],
                     after[1]);
        if (byteOnly && result[0] && after[0] != ((valueThis & 0xffffff00u) | ((self ? valueThis : valueSource) & 0xff)))
            ctx.Fail("AnyBoolean Assign copied more than the byte");
    }
}

// ---- Compare: -1 / 0 / 1 ----

template <typename Ours> void RunCompare(difftest::Context &ctx, uint32_t rva, uint32_t (*payload)(std::mt19937 &),
                                         uint32_t seed)
{
    auto orig = ctx.Original<int32_t(__thiscall *)(const void *, const void *)>(rva);
    std::mt19937 rng(seed);
    for (int i = 0; i < 40000; ++i) {
        uint32_t a = payload(rng), b = rng() % 4 == 0 ? a : payload(rng);
        if (rng() % 8 == 0) b = a ^ (1u << (rng() % 32)); // neighbours, sign flips
        uint32_t objA[2] = {0, a}, objB[2] = {0, b};
        int32_t want = orig(objA, objB);
        int32_t got = reinterpret_cast<const Ours *>(objA)->Compare(objB);
        ++ctx.cases;
        if (want != got) ctx.Fail("Compare %08x with %08x: %d/%d", a, b, want, got);
    }
}

uint32_t Payload(std::mt19937 &rng)
{
    return RandomPayload(rng);
}

// Only the low byte is the value; the other bytes are random and must not count.
uint32_t BooleanPayload(std::mt19937 &rng)
{
    return (rng() & 0xffffff00u) | (rng() % 3 == 0 ? rng() & 0xff : rng() % 2);
}

// ---- Clone: a new 8-byte object with the class's vtable and the payload (AnyBoolean: the byte) ----

template <typename Ours> void RunClone(difftest::Context &ctx, uint32_t rva, uint32_t vtable, bool byteOnly, uint32_t seed)
{
    auto orig = ctx.Original<void *(__thiscall *)(const void *)>(rva);
    const void *gameVTable = ctx.image.At<void>(vtable);
    std::mt19937 rng(seed);
    for (int i = 0; i < 5000; ++i) {
        uint32_t source[2] = {rng(), rng()}; // the source's vtable is not read
        uint32_t made[2][2];
        heappoison::Scope poison(ctx, i % 2 ? 0x5a : 0xcd); // every heap block poisoned on both sides (DT)
        for (int side = 0; side < 2; ++side) {
            void *copy = side == 0 ? orig(source) : static_cast<void *>(reinterpret_cast<const Ours *>(source)->Clone());
            if (!copy) {
                ctx.Fail("Clone returned null");
                return;
            }
            std::memcpy(made[side], copy, 8);
            ::operator delete(copy);
        }
        ++ctx.cases;
        // AnyBoolean's clone leaves bytes 5-7 as operator new returned them (0x14df70 writes only the vtable and the
        // byte at +4): under the poisoned heap they hold the pattern on both sides, so the two clones are compared whole;
        // only the comparison with the source is limited to the byte.
        uint32_t mask = byteOnly ? 0xffu : 0xffffffffu;
        if (made[0][0] != made[1][0] || made[0][0] != reinterpret_cast<uintptr_t>(gameVTable) ||
            made[0][1] != made[1][1] || (made[0][1] & mask) != (source[1] & mask))
            ctx.Fail("Clone of %08x: %08x %08x / %08x %08x", source[1], made[0][0], made[0][1], made[1][0], made[1][1]);
    }
}

// ---- deleting destructors: the vtable becomes cIGZAny's, the object is freed on flags & 1, this returned ----

template <typename Ours> void RunDeletingDestructor(difftest::Context &ctx, uint32_t rva, uint32_t size, uint32_t seed)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(rva);
    const uintptr_t anyVTable = reinterpret_cast<uintptr_t>(ctx.image.At<void>(gzany::kAnyVTable));
    std::mt19937 rng(seed);
    for (int i = 0; i < 4000; ++i) {
        uint32_t flags = i < 4 ? static_cast<uint32_t>(i) : rng();
        uint32_t payload = rng();
        for (int side = 0; side < 2; ++side) {
            auto *obj = static_cast<uint32_t *>(::operator new(size));
            obj[0] = rng();
            obj[1] = payload;
            difftest::FreeRecorder rec;
            void *ret = side == 0 ? orig(obj, flags) : reinterpret_cast<Ours *>(obj)->DeletingDestructor(flags);
            rec.Stop();
            bool freed = rec.Freed(obj);
            ++ctx.cases;
            if (ret != obj) ctx.Fail("deleting destructor side %d returned %p, not this", side, ret);
            if (freed != ((flags & 1) != 0) || rec.Count() != (freed ? 1u : 0u))
                ctx.Fail("deleting destructor side %d flags %08x: freed %d, %zu frees", side, flags, freed, rec.Count());
            if (!freed) {
                if (obj[0] != anyVTable || obj[1] != payload) ctx.Fail("deleting destructor side %d left %08x %08x", side, obj[0], obj[1]);
                ::operator delete(obj);
            }
        }
    }
}

// ---- GetType / GetTypeName ----

template <typename Ours> void RunTypeSlots(difftest::Context &ctx, uint32_t typeRva, uint32_t nameRva)
{
    auto type = ctx.Original<uint32_t(__thiscall *)(const void *)>(typeRva);
    auto name = ctx.Original<const char *(__thiscall *)(const void *)>(nameRva);
    uint32_t obj[2] = {0, 0};
    ++ctx.cases;
    if (type(obj) != reinterpret_cast<const Ours *>(obj)->GetType()) ctx.Fail("GetType differs");
    ++ctx.cases;
    if (name(obj) != reinterpret_cast<const Ours *>(obj)->GetTypeName()) ctx.Fail("GetTypeName differs");
}

} // namespace

DIFF_TEST(GZAny_cIGZAnyDeletingDestructor, 0x14c840)
{
    RunDeletingDestructor<cGZAny>(ctx, 0x14c840, 4, 1);
}

// AnyUint32, AnyBoolean and AnyFloat32 are in the standalone shape: tests/gz_any_shape.cpp.

// 0x14c5f0: AnyUint32's and AnyBoolean's slot 0 (8-byte objects), hooked as cGZAny::ValueDeletingDestructor.
namespace {
struct ValueAny : cGZAny {
    void *DeletingDestructor(uint32_t flags) { return ValueDeletingDestructor(flags); }
};
} // namespace

DIFF_TEST(GZAny_ValueDeletingDestructor, 0x14c5f0)
{
    RunDeletingDestructor<ValueAny>(ctx, 0x14c5f0, 8, 2);
}

DIFF_TEST(GZAny_ScalarDeletingDestructors, 0x1bedcb)
{
    RunDeletingDestructor<AnySint32>(ctx, 0x1bedcb, 8, 3);
}

DIFF_TEST(GZAny_ScalarAssign, 0x1bf068)
{
    RunScalarAssign<AnySint32>(ctx, 0x1bf068, false, 6);
}

DIFF_TEST(GZAny_ScalarCompare, 0x1befcf)
{
    RunCompare<AnySint32>(ctx, 0x1befcf, &Payload, 9);
    RunCompare<AnyAny>(ctx, 0x1bef94, &Payload, 11);
}

DIFF_TEST(GZAny_ScalarClone, 0x1bef56)
{
    RunClone<AnySint32>(ctx, 0x1bef56, gzany::kSint32VTable, false, 14);
    RunClone<AnyAny>(ctx, 0x1bef14, gzany::kAnyAnyVTable, false, 16);
}

DIFF_TEST(GZAny_TypeSlots, 0x1bf5c5)
{
    RunTypeSlots<AnySint32>(ctx, 0x1bf5c5, 0x1bf5dd);
    RunTypeSlots<AnyAny>(ctx, 0x1bf5b9, 0x1bf5d1);
    RunTypeSlots<AnyString>(ctx, 0x1bf5cb, 0x1bf5e3);
    RunTypeSlots<AnyStringSet>(ctx, 0x238790, 0x238796);
}

// ---- AnyString ----

namespace {

struct StringWorld {
    FakeString strings[3];
    FakeString *Get(int index) { return index < 0 ? nullptr : &strings[index]; }
    int IndexOf(const void *p) const
    {
        if (!p) return -1;
        for (int k = 0; k < 3; ++k)
            if (p == &strings[k]) return k;
        return 9;
    }
};

} // namespace

// Copy: the vtable, the shared string AddRef'd (when there is one).
DIFF_TEST(GZAny_StringCopyConstruct, 0x1bec79)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x1bec79);
    const uintptr_t vt = reinterpret_cast<uintptr_t>(ctx.image.At<void>(gzany::kStringVTable));
    SetupFakeStrings();
    std::mt19937 rng(20);
    for (int i = 0; i < 2000; ++i) {
        int held = static_cast<int>(rng() % 4) - 1;
        uint32_t refs = rng() % 5;
        uint32_t made[2][2];
        std::vector<uint32_t> events[2];
        uint32_t finalRefs[2];
        for (int side = 0; side < 2; ++side) {
            StringWorld w{{{gFakeStringVTable, refs, 0}, {gFakeStringVTable, refs, 1}, {gFakeStringVTable, refs, 2}}};
            uint32_t source[2] = {rng(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.Get(held)))};
            uint32_t target[2] = {rng(), rng()};
            gEvents.clear();
            void *ret = side == 0 ? orig(target, source)
                                  : reinterpret_cast<AnyString *>(target)->Construct(*reinterpret_cast<const AnyString *>(source));
            if (ret != target) ctx.Fail("copy construct returned another object");
            made[side][0] = target[0];
            made[side][1] = static_cast<uint32_t>(w.IndexOf(reinterpret_cast<void *>(static_cast<uintptr_t>(target[1]))));
            events[side] = gEvents;
            finalRefs[side] = held < 0 ? 0 : w.strings[held].refs;
        }
        ++ctx.cases;
        if (made[0][0] != vt || made[1][0] != vt || made[0][1] != made[1][1] || events[0] != events[1] ||
            finalRefs[0] != finalRefs[1])
            ctx.Fail("AnyString copy of string %d differs", held);
    }
}

// Destruct: Release of the held string (when there is one), the vtable reset; the pointer stays.
DIFF_TEST(GZAny_StringDestruct, 0x1bed50)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x1bed50);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1bedeb);
    const uintptr_t anyVT = reinterpret_cast<uintptr_t>(ctx.image.At<void>(gzany::kAnyVTable));
    SetupFakeStrings();
    std::mt19937 rng(21);
    for (int i = 0; i < 4000; ++i) {
        int held = static_cast<int>(rng() % 4) - 1;
        bool viaDeleting = rng() % 2;
        uint32_t flags = rng();
        uint32_t made[2][2];
        std::vector<uint32_t> events[2];
        bool freed[2] = {};
        for (int side = 0; side < 2; ++side) {
            StringWorld w{{{gFakeStringVTable, 3, 0}, {gFakeStringVTable, 3, 1}, {gFakeStringVTable, 3, 2}}};
            auto *obj = static_cast<uint32_t *>(::operator new(8));
            obj[0] = rng();
            obj[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.Get(held)));
            gEvents.clear();
            difftest::FreeRecorder rec;
            if (!viaDeleting) {
                if (side == 0) orig(obj);
                else reinterpret_cast<AnyString *>(obj)->Destruct();
            } else {
                void *ret = side == 0 ? deleting(obj, flags) : reinterpret_cast<AnyString *>(obj)->DeletingDestructor(flags);
                if (ret != obj) ctx.Fail("deleting destructor returned another object");
            }
            rec.Stop();
            freed[side] = rec.Freed(obj);
            events[side] = gEvents;
            if (!freed[side]) {
                made[side][0] = obj[0];
                made[side][1] = static_cast<uint32_t>(w.IndexOf(reinterpret_cast<void *>(static_cast<uintptr_t>(obj[1]))));
                ::operator delete(obj);
            } else {
                made[side][0] = made[side][1] = 0;
            }
        }
        ++ctx.cases;
        bool shouldFree = viaDeleting && (flags & 1);
        if (freed[0] != shouldFree || freed[1] != shouldFree || events[0] != events[1] ||
            (!shouldFree && (made[0][0] != anyVT || made[1][0] != anyVT || made[0][1] != made[1][1])))
            ctx.Fail("AnyString destruct (deleting %d, flags %08x, string %d) differs", viaDeleting, flags, held);
    }
}

// Assign: same type only; the source's string shared (AddRef new, then Release old), nothing when source is this.
DIFF_TEST(GZAny_StringAssign, 0x1bf097)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, const void *)>(0x1bf097);
    SetupFakeStrings();
    std::mt19937 rng(22);
    for (int i = 0; i < 20000; ++i) {
        uint32_t typeThis = kTypes[rng() % 8], typeSource = rng() % 2 ? typeThis : kTypes[rng() % 8];
        int heldThis = static_cast<int>(rng() % 4) - 1, heldSource = static_cast<int>(rng() % 4) - 1;
        bool self = rng() % 8 == 0;
        uint8_t result[2];
        int after[2];
        std::vector<uint32_t> events[2];
        uint32_t refs[2][3];
        for (int side = 0; side < 2; ++side) {
            StringWorld w{{{gFakeStringVTable, 2, 0}, {gFakeStringVTable, 2, 1}, {gFakeStringVTable, 2, 2}}};
            FakeAny target{gFakeAnyVTable, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.Get(heldThis))), typeThis, 1};
            FakeAny source{gFakeAnyVTable, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.Get(heldSource))), typeSource, 2};
            const void *from = self ? &target : &source;
            gEvents.clear();
            result[side] = side == 0 ? orig(&target, from) : reinterpret_cast<AnyString *>(&target)->Assign(from);
            after[side] = w.IndexOf(reinterpret_cast<void *>(static_cast<uintptr_t>(target.value)));
            events[side] = gEvents;
            for (int k = 0; k < 3; ++k) refs[side][k] = w.strings[k].refs;
        }
        ++ctx.cases;
        if (result[0] != result[1] || after[0] != after[1] || events[0] != events[1] ||
            std::memcmp(refs[0], refs[1], sizeof(refs[0])) != 0)
            ctx.Fail("AnyString Assign %08x <- %08x (strings %d <- %d, self %d): %u/%u, held %d/%d", typeThis, typeSource,
                     heldThis, heldSource, self, result[0], result[1], after[0], after[1]);
    }
}

// Compare: this string's CompareTo(other's string, case-sensitive), its result returned as it is.
DIFF_TEST(GZAny_StringCompare, 0x1befeb)
{
    auto orig = ctx.Original<int32_t(__thiscall *)(const void *, const void *)>(0x1befeb);
    SetupFakeStrings();
    StringWorld w{{{gFakeStringVTable, 1, 0}, {gFakeStringVTable, 1, 1}, {gFakeStringVTable, 1, 2}}};
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            uint32_t objA[2] = {0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.strings[a]))};
            uint32_t objB[2] = {0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.strings[b]))};
            gEvents.clear();
            int32_t want = orig(objA, objB);
            std::vector<uint32_t> wantEvents = gEvents;
            gEvents.clear();
            int32_t got = reinterpret_cast<const AnyString *>(objA)->Compare(objB);
            ++ctx.cases;
            if (want != got || wantEvents != gEvents) ctx.Fail("AnyString Compare %d %d: %d/%d", a, b, want, got);
        }
}

// Clone: a new AnyString sharing the string (AddRef'd).
DIFF_TEST(GZAny_StringClone, 0x1bef77)
{
    auto orig = ctx.Original<void *(__thiscall *)(const void *)>(0x1bef77);
    const uintptr_t vt = reinterpret_cast<uintptr_t>(ctx.image.At<void>(gzany::kStringVTable));
    SetupFakeStrings();
    for (int held = -1; held < 3; ++held) {
        uint32_t made[2][2];
        std::vector<uint32_t> events[2];
        uint32_t refs[2];
        for (int side = 0; side < 2; ++side) {
            StringWorld w{{{gFakeStringVTable, 4, 0}, {gFakeStringVTable, 4, 1}, {gFakeStringVTable, 4, 2}}};
            uint32_t source[2] = {0x5a5a5a5a, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.Get(held)))};
            gEvents.clear();
            auto *copy = static_cast<uint32_t *>(side == 0 ? orig(source)
                                                           : static_cast<void *>(reinterpret_cast<const AnyString *>(source)->Clone()));
            made[side][0] = copy[0];
            made[side][1] = static_cast<uint32_t>(w.IndexOf(reinterpret_cast<void *>(static_cast<uintptr_t>(copy[1]))));
            ::operator delete(copy);
            events[side] = gEvents;
            refs[side] = held < 0 ? 0 : w.strings[held].refs;
        }
        ++ctx.cases;
        if (made[0][0] != vt || made[1][0] != vt || made[0][1] != made[1][1] || events[0] != events[1] || refs[0] != refs[1])
            ctx.Fail("AnyString Clone of string %d differs", held);
    }
}

// The constructors from text: a new cRZString holding a copy (refcount 1 after the slot's AddRef).
DIFF_TEST(GZAny_StringConstructFromText, 0x1ce6ab)
{
    auto fromString = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x1ce6ab);
    auto fromChar = ctx.Original<void *(__thiscall *)(void *, const char *)>(0x213fbb);
    const uintptr_t vt = reinterpret_cast<uintptr_t>(ctx.image.At<void>(gzany::kStringVTable));
    const uintptr_t stringVT = reinterpret_cast<uintptr_t>(ctx.image.At<void>(cRZString::kVTable));
    const char *texts[] = {"", "a", "fifteen chars..", "sixteen chars...", "a longer text of more than thirty-one chars"};
    for (const char *text : texts) {
        for (int useChar = 0; useChar < 2; ++useChar) {
            std::string seen[2];
            uint32_t refs[2], vts[2], strVts[2];
            for (int side = 0; side < 2; ++side) {
                auto *src = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
                src->Construct(text);
                uint32_t obj[2] = {0x11111111, 0x22222222};
                void *ret;
                if (useChar)
                    ret = side == 0 ? fromChar(obj, text) : reinterpret_cast<AnyString *>(obj)->Construct(text);
                else
                    ret = side == 0 ? fromString(obj, src)
                                    : reinterpret_cast<AnyString *>(obj)->Construct(*reinterpret_cast<const cIGZString *>(src));
                if (ret != obj) ctx.Fail("text constructor returned another object");
                auto *made = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(obj[1]));
                vts[side] = obj[0];
                strVts[side] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(made->mVTable));
                seen[side] = made->mString;
                refs[side] = made->mRefCount;
                made->Destruct();
                ::operator delete(made);
                src->Destruct();
                ::operator delete(src);
            }
            ++ctx.cases;
            if (vts[0] != vt || vts[1] != vt || strVts[0] != stringVT || strVts[1] != stringVT || seen[0] != text ||
                seen[1] != text || refs[0] != 1 || refs[1] != 1)
                ctx.Fail("AnyString from \"%s\" (char %d): refs %u/%u", text, useChar, refs[0], refs[1]);
        }
    }
}
