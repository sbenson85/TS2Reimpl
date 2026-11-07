// Differential tests for the rest of cRZOStream: constructor, destructor, deleting destructor,
// Release, the byte-order setter, SetSint8, SetSint64, the packed-integer writer, the two string
// writers, SetOwner and the 64-bit byte swap (tests/rz_ostream.cpp has QueryInterface and the other
// setters).
//
// Every call the code makes is logged into one ordered event list (gEv): raw writes (size and a hash
// of the bytes), SetUint8 calls (the full 32-bit argument), deleting-destructor calls (the flags),
// owner Release calls (with whether the stream already carries the base class's vtable), string
// slot calls, AddRef/Release on smart-pointer targets. Original and replacement run on identical
// objects against the same fakes, and the lists, return values and object bytes must match.
#include "difftest.h"
#include "rz/cRZOStream.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

std::vector<uint64_t> gEv;
uint32_t gRes = 0;   // what the raw write and the deleting destructor slot return
uint32_t gRes8 = 0;  // what SetUint8 returns
const void *gStream = nullptr;
const void *gOwnerSelf = nullptr;
const void *gImageVt = nullptr;

enum : uint64_t { kEvWrite = 1, kEvSetUint8, kEvDelete, kEvOwnerRelease, kEvStrlen, kEvToChar, kEvAddRef, kEvRelease };

uint64_t Ev(uint64_t kind, uint64_t value) { return (kind << 56) | (value & 0x00ffffffffffffffull); }

uint32_t Fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

uint32_t __fastcall EvWrite(void *, void *, const void *buf, uint32_t n)
{
    gEv.push_back(Ev(kEvWrite, (uint64_t(n) << 32) | Fnv(static_cast<const uint8_t *>(buf), n)));
    return gRes;
}

uint32_t __fastcall EvSetUint8(void *, void *, uint32_t v)
{
    gEv.push_back(Ev(kEvSetUint8, v));
    return gRes8;
}

uint32_t __fastcall EvDelete(void *self, void *, uint32_t flags)
{
    gEv.push_back(Ev(kEvDelete, (uint64_t(self == gStream) << 32) | flags));
    return gRes;
}

uint32_t __fastcall EvAddRef(void *self, void *)
{
    auto *count = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 4);
    return ++*count;
}

uint32_t __fastcall EvOwnerRelease(void *self, void *)
{
    void *vt;
    std::memcpy(&vt, gStream, sizeof(vt));
    gEv.push_back(Ev(kEvOwnerRelease, uint64_t(self == gOwnerSelf) | (uint64_t(vt == gImageVt) << 1)));
    return gRes;
}

// A string with fixed content; slot 5 (ToChar) and slot 7 (Strlen) log their calls.
struct FakeString {
    void *vtable;
    const char *chars;
    uint32_t length;
};
uint32_t __fastcall FakeStrlen(void *self, void *)
{
    gEv.push_back(Ev(kEvStrlen, 0));
    return static_cast<FakeString *>(self)->length;
}
uint32_t __fastcall FakeToChar(void *self, void *)
{
    gEv.push_back(Ev(kEvToChar, 0));
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<FakeString *>(self)->chars));
}

// A copy of the original vtable with the stream slots replaced by the recorders.
struct EvVtable {
    void *slots[24];
    explicit EvVtable(const difftest::Context &ctx)
    {
        std::memcpy(slots, ctx.image.At(cRZOStream::kVTable), sizeof(slots));
        slots[1] = reinterpret_cast<void *>(&EvAddRef);
        slots[5] = reinterpret_cast<void *>(&EvSetUint8);
        slots[cRZOStream::kRawWriteSlot] = reinterpret_cast<void *>(&EvWrite);
        slots[cRZOStream::kDeletingDestructorSlot] = reinterpret_cast<void *>(&EvDelete);
    }
};

// Storage for one object (16 bytes at +16) with canaries around it; junk everywhere else.
struct Slot {
    uint8_t bytes[64];

    void Init(const void *vtable, uint32_t refs, uint32_t flagWord, uint32_t owner = 0xa5a5a5a5u)
    {
        std::memset(bytes, 0xA5, sizeof(bytes));
        std::memcpy(bytes + 16, &vtable, sizeof(vtable));
        std::memcpy(bytes + 20, &refs, 4);
        std::memcpy(bytes + 24, &flagWord, 4);
        std::memcpy(bytes + 28, &owner, 4);
    }
    cRZOStream *Object() { return reinterpret_cast<cRZOStream *>(bytes + 16); }
    const void *ObjectAddress() const { return bytes + 16; }
    void PutOwner(const void *owner) { std::memcpy(bytes + 28, &owner, sizeof(owner)); }
};

constexpr uint32_t kFlags[] = {0x00000000u, 0x00000001u, 0x00000080u, 0x000000ffu, 0x00000100u,
                               0x0000ff00u, 0xffffff00u, 0x01000000u, 0xffffffffu, 0x12345600u};
constexpr uint32_t kWriteResults[] = {0u, 1u, 0x100u, 0xffffffffu, 0x80000001u};

uint32_t PickFlag(std::mt19937 &rng, uint32_t i)
{
    return i < std::size(kFlags) ? kFlags[i] : rng();
}

struct Outcome {
    uint64_t ret = 0;
    std::vector<uint64_t> events;
};

template <typename Call> Outcome Observe(const void *stream, Call call)
{
    gEv.clear();
    gStream = stream;
    Outcome o;
    o.ret = call();
    o.events = gEv;
    return o;
}

void Compare(difftest::Context &ctx, const char *what, const Slot &a, const Slot &b, const Outcome &want, const Outcome &got,
             bool compareObjects = true)
{
    ++ctx.cases;
    if (want.ret != got.ret) {
        ctx.Fail("%s: return original 0x%llx, ours 0x%llx", what, (unsigned long long)want.ret, (unsigned long long)got.ret);
    }
    if (want.events != got.events) {
        ctx.Fail("%s: call logs differ (original %zu events, ours %zu)", what, want.events.size(), got.events.size());
    }
    if (compareObjects && std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) != 0) ctx.Fail("%s: object memory differs", what);
}

// Random text without NULs, of lengths that exercise one- and two-byte packed lengths.
std::vector<char> RandomText(std::mt19937 &rng)
{
    uint32_t pick = rng() % 8;
    uint32_t len = pick == 0 ? 0 : pick < 5 ? rng() % 12 : pick < 7 ? rng() % 140 : 120 + rng() % 30;
    if (rng() % 40 == 0) {
        // Longer than a byte, a 14-bit group, and into three-byte packed lengths.
        static const uint32_t big[] = {127, 128, 255, 256, 300, 16383, 16384, 70000};
        len = big[rng() % std::size(big)];
    }
    std::vector<char> t(len + 1);
    for (uint32_t k = 0; k < len; ++k) t[k] = static_cast<char>(1 + rng() % 255);
    t[len] = 0;
    return t;
}

// Smart-pointer targets: AddRef and Release record which object (by index) they were called on.
struct Counted {
    void *vtable;
    uint32_t refs;
    int id;
};
uint32_t __fastcall CountedAddRef(void *self, void *)
{
    auto *c = static_cast<Counted *>(self);
    gEv.push_back(Ev(kEvAddRef, c->id));
    return ++c->refs;
}
uint32_t __fastcall CountedRelease(void *self, void *)
{
    auto *c = static_cast<Counted *>(self);
    gEv.push_back(Ev(kEvRelease, c->id));
    return --c->refs;
}

} // namespace

DIFF_TEST(cRZOStream_Construct, 0x1b4f75)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1b4f75);
    EvVtable vt(ctx);
    std::mt19937 rng(100);
    for (int i = 0; i < 4000; ++i) {
        uint32_t order = PickFlag(rng, i);
        Slot a, b;
        a.Init(vt.slots, rng(), rng(), rng());
        b = a;
        Outcome want = Observe(a.ObjectAddress(), [&] { return uint64_t(orig(a.Object(), order) == a.Object()); });
        Outcome got = Observe(b.ObjectAddress(), [&] { return uint64_t(b.Object()->Construct(order) == b.Object()); });
        char what[64];
        std::snprintf(what, sizeof(what), "Construct(0x%08x)", order);
        Compare(ctx, what, a, b, want, got);
        if (want.ret != 1) ctx.Fail("%s: original did not return this", what);
    }
}

DIFF_TEST(cRZOStream_Destruct, 0x1b52fb)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x1b52fb);
    EvVtable vt(ctx);
    gImageVt = ctx.image.At(cRZOStream::kVTable);
    void *ownerVt[3] = {nullptr, nullptr, reinterpret_cast<void *>(&EvOwnerRelease)};
    struct Owner { void *vtable; } owner{ownerVt};
    gOwnerSelf = &owner;
    std::mt19937 rng(101);
    for (int i = 0; i < 2000; ++i) {
        Slot a, b;
        a.Init(vt.slots, rng(), rng());
        a.PutOwner(i % 3 ? &owner : nullptr);
        b = a;
        gRes = rng();
        Outcome want = Observe(a.ObjectAddress(), [&] { orig(a.Object()); return 0; });
        Outcome got = Observe(b.ObjectAddress(), [&] { b.Object()->Destruct(); return 0; });
        Compare(ctx, "Destruct", a, b, want, got);
        ++ctx.cases;
        if (i % 3 && want.events.size() != 1) ctx.Fail("Destruct: owner not released exactly once by the original");
    }
}

DIFF_TEST(cRZOStream_DeletingDestructor, 0x1b580a)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1b580a);
    EvVtable vt(ctx);
    gImageVt = ctx.image.At(cRZOStream::kVTable);
    void *ownerVt[3] = {nullptr, nullptr, reinterpret_cast<void *>(&EvOwnerRelease)};
    struct Owner { void *vtable; } owner{ownerVt};
    gOwnerSelf = &owner;
    std::mt19937 rng(102);
    const uint32_t flagSet[] = {0, 1, 2, 3, 0xfffffffeu, 0xffffffffu, 0x100, 0x101};
    for (int i = 0; i < 3000; ++i) {
        uint32_t flags = i < 8 ? flagSet[i] : flagSet[rng() % 8];
        // Heap blocks, so a real delete is safe; memory is compared only when nothing was freed.
        auto *ha = static_cast<uint8_t *>(::operator new(sizeof(cRZOStream)));
        auto *hb = static_cast<uint8_t *>(::operator new(sizeof(cRZOStream)));
        Slot sa;
        sa.Init(vt.slots, rng(), rng());
        sa.PutOwner(i % 3 ? &owner : nullptr);
        std::memcpy(ha, sa.bytes + 16, 16);
        std::memcpy(hb, sa.bytes + 16, 16);
        gRes = rng();
        Outcome want = Observe(ha, [&] { return uint64_t(orig(ha, flags) == ha); });
        Outcome got = Observe(hb, [&] { return uint64_t(reinterpret_cast<cRZOStream *>(hb)->DeletingDestructor(flags) == reinterpret_cast<cRZOStream *>(hb)); });
        Slot a, b;
        std::memset(a.bytes, 0, sizeof(a.bytes));
        std::memset(b.bytes, 0, sizeof(b.bytes));
        if (!(flags & 1)) {
            std::memcpy(a.bytes, ha, 16);
            std::memcpy(b.bytes, hb, 16);
            ::operator delete(ha);
            ::operator delete(hb);
        }
        char what[64];
        std::snprintf(what, sizeof(what), "DeletingDestructor(0x%x)", flags);
        Compare(ctx, what, a, b, want, got);
        if (want.ret != 1) ctx.Fail("%s: original did not return this", what);
    }
}

// Release (slot 2) and the byte-order setter (slot 22) are shared with the input stream base.
DIFF_TEST(cRZOStream_Release, 0x1b5fc4)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1b5fc4);
    EvVtable vt(ctx);
    std::mt19937 rng(103);
    const uint32_t counts[] = {0, 1, 2, 3, 0x80000000u, 0xffffffffu};
    for (int i = 0; i < 6000; ++i) {
        uint32_t refs = i % 2 ? counts[rng() % 6] : rng() % 5;
        gRes = rng();
        Slot a, b;
        a.Init(vt.slots, refs, rng());
        b = a;
        Outcome want = Observe(a.ObjectAddress(), [&] { return uint64_t(orig(a.Object())); });
        Outcome got = Observe(b.ObjectAddress(), [&] { return uint64_t(b.Object()->Release()); });
        char what[64];
        std::snprintf(what, sizeof(what), "Release (refs %u)", refs);
        Compare(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetByteOrder, 0x1b5f3c)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1b5f3c);
    EvVtable vt(ctx);
    std::mt19937 rng(104);
    for (int i = 0; i < 4000; ++i) {
        uint32_t order = PickFlag(rng, i);
        Slot a, b;
        a.Init(vt.slots, rng(), rng());
        b = a;
        Outcome want = Observe(a.ObjectAddress(), [&] { return uint64_t(orig(a.Object(), order) & 0xff); });
        Outcome got = Observe(b.ObjectAddress(), [&] { return uint64_t(b.Object()->SetByteOrder(order) ? 1 : 0); });
        Compare(ctx, "SetByteOrder", a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetSint8, 0x1b63e7)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1b63e7);
    EvVtable vt(ctx);
    std::mt19937 rng(105);
    for (int i = 0; i < 4000; ++i) {
        // The stack slot is a dword whose low byte is written.
        uint32_t arg = i < 256 ? uint32_t(i) | (uint32_t(rng()) & 0xffffff00u) : rng();
        gRes = kWriteResults[i % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, rng());
        b = a;
        Outcome want = Observe(a.ObjectAddress(), [&] { return uint64_t(orig(a.Object(), arg)); });
        Outcome got = Observe(b.ObjectAddress(), [&] { return uint64_t(b.Object()->SetSint8(static_cast<uint8_t>(arg))); });
        char what[64];
        std::snprintf(what, sizeof(what), "SetSint8(0x%08x)", arg);
        Compare(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_ByteSwap64, 0x1b6734)
{
    auto orig = ctx.Original<uint64_t(__stdcall *)(uint64_t)>(0x1b6734);
    std::mt19937 rng(106);
    const uint64_t edges[] = {0, 1, 0xffffffffffffffffull, 0x0102030405060708ull, 0x8000000000000000ull,
                              0x00000000ffffffffull, 0xffffffff00000000ull};
    for (int i = 0; i < 20000; ++i) {
        uint64_t v = i < int(std::size(edges)) ? edges[i] : (uint64_t(rng()) << 32) | rng();
        ++ctx.cases;
        uint64_t w = orig(v), g = cRZOStream::ByteSwap64(v);
        if (w != g) {
            ctx.Fail("ByteSwap64(0x%016llx): original 0x%016llx, ours 0x%016llx", (unsigned long long)v, (unsigned long long)w,
                     (unsigned long long)g);
        }
    }
}

DIFF_TEST(cRZOStream_SetSint64, 0x1b634c)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x1b634c);
    EvVtable vt(ctx);
    std::mt19937 rng(107);
    const uint64_t edges[] = {0, 1, 0xffffffffffffffffull, 0x0102030405060708ull, 0x8000000000000000ull,
                              0x00000000ffffffffull, 0xffffffff00000000ull};
    for (int i = 0; i < 20000; ++i) {
        uint64_t v = i < int(std::size(edges)) ? edges[i] : (uint64_t(rng()) << 32) | rng();
        uint32_t flag = PickFlag(rng, i);
        gRes = kWriteResults[i % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, flag);
        b = a;
        Outcome want = Observe(a.ObjectAddress(), [&] { return uint64_t(orig(a.Object(), uint32_t(v), uint32_t(v >> 32))); });
        Outcome got = Observe(b.ObjectAddress(), [&] { return uint64_t(b.Object()->SetSint64(static_cast<int64_t>(v))); });
        char what[80];
        std::snprintf(what, sizeof(what), "SetSint64(0x%016llx) flag 0x%08x", (unsigned long long)v, flag);
        Compare(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetPackedUint32, 0x1b58ee)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x1b58ee);
    EvVtable vt(ctx);
    std::mt19937 rng(108);
    const uint32_t edges[] = {0, 1, 0x7f, 0x80, 0x3fff, 0x4000, 0x1fffff, 0x200000, 0xfffffff, 0x10000000,
                              0xffffffffu, 0x80000000u, 0x12345678};
    for (int i = 0; i < 20000; ++i) {
        uint32_t v = i < int(std::size(edges)) ? edges[i] : (rng() >> (rng() % 32));
        gRes8 = kWriteResults[i % std::size(kWriteResults)];
        Slot a, b;
        a.Init(vt.slots, 0, rng());
        b = a;
        Outcome want = Observe(a.ObjectAddress(), [&] { return uint64_t(orig(a.Object(), v)); });
        Outcome got = Observe(b.ObjectAddress(), [&] { return uint64_t(b.Object()->SetPackedUint32(v)); });
        char what[64];
        std::snprintf(what, sizeof(what), "SetPackedUint32(0x%08x)", v);
        Compare(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetRZCharStr, 0x1b61c9)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, const char *)>(0x1b61c9);
    EvVtable vt(ctx);
    std::mt19937 rng(109);
    for (int i = 0; i < 10000; ++i) {
        std::vector<char> text = RandomText(rng);
        gRes = kWriteResults[i % std::size(kWriteResults)];
        gRes8 = rng();
        Slot a, b;
        a.Init(vt.slots, 0, rng());
        b = a;
        Outcome want = Observe(a.ObjectAddress(), [&] { return uint64_t(orig(a.Object(), text.data()) & 0xff); });
        Outcome got = Observe(b.ObjectAddress(), [&] { return uint64_t(b.Object()->SetRZCharStr(text.data())); });
        char what[64];
        std::snprintf(what, sizeof(what), "SetRZCharStr (len %zu)", text.size() - 1);
        Compare(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetGZStr, 0x1b6118)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x1b6118);
    EvVtable vt(ctx);
    void *strVt[33] = {};
    strVt[5] = reinterpret_cast<void *>(&FakeToChar);
    strVt[7] = reinterpret_cast<void *>(&FakeStrlen);
    std::mt19937 rng(110);
    for (int i = 0; i < 10000; ++i) {
        std::vector<char> text = RandomText(rng);
        FakeString str{strVt, text.data(), uint32_t(text.size() - 1)};
        gRes = kWriteResults[i % std::size(kWriteResults)];
        gRes8 = rng();
        Slot a, b;
        a.Init(vt.slots, 0, rng());
        b = a;
        Outcome want = Observe(a.ObjectAddress(), [&] { return uint64_t(orig(a.Object(), &str) & 0xff); });
        Outcome got = Observe(b.ObjectAddress(), [&] { return uint64_t(b.Object()->SetGZStr(reinterpret_cast<cIGZString *>(&str))); });
        char what[64];
        std::snprintf(what, sizeof(what), "SetGZStr (len %u)", str.length);
        Compare(ctx, what, a, b, want, got);
    }
}

DIFF_TEST(cRZOStream_SetOwner, 0x1b6617)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0x1b6617);
    EvVtable vt(ctx);
    void *countedVt[3] = {nullptr, reinterpret_cast<void *>(&CountedAddRef), reinterpret_cast<void *>(&CountedRelease)};
    std::mt19937 rng(111);
    for (int i = 0; i < 6000; ++i) {
        int initial = int(rng() % 4) - 1, next = int(rng() % 4) - 1;
        uint32_t refs[3] = {rng() % 4, rng() % 4, rng() % 4};
        Slot a, b;
        a.Init(vt.slots, 0, rng());
        b = a;
        Counted objsA[3], objsB[3];
        for (int k = 0; k < 3; ++k) objsA[k] = objsB[k] = {countedVt, refs[k], k};
        a.PutOwner(initial < 0 ? nullptr : &objsA[initial]);
        b.PutOwner(initial < 0 ? nullptr : &objsB[initial]);
        Outcome want = Observe(a.ObjectAddress(), [&] {
            return uint64_t(orig(a.Object(), next < 0 ? nullptr : &objsA[next]) == a.bytes + 28);
        });
        Outcome got = Observe(b.ObjectAddress(), [&] {
            return uint64_t(b.Object()->SetOwner(next < 0 ? nullptr : &objsB[next]) == b.bytes + 28);
        });
        // The owner field is compared as an index into each world's objects, then the reference counts.
        auto index = [](const Slot &s, Counted *objs) {
            void *p;
            std::memcpy(&p, s.bytes + 28, sizeof(p));
            for (int k = 0; k < 3; ++k)
                if (p == &objs[k]) return k;
            return p ? 9 : -1;
        };
        ++ctx.cases;
        if (index(a, objsA) != index(b, objsB)) ctx.Fail("SetOwner initial %d next %d: stored owner differs", initial, next);
        for (int k = 0; k < 3; ++k) {
            if (objsA[k].refs != objsB[k].refs) {
                ctx.Fail("SetOwner initial %d next %d: reference count of object %d differs", initial, next, k);
            }
        }
        Compare(ctx, "SetOwner", a, b, want, got, false);
    }
}
