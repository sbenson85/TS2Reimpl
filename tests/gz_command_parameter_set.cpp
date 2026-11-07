#include "difftest.h"
#include "gz/cGZCommandParameterSet.h"
#include "rz/cRZString.h"

#include <windows.h>
#undef FindText // the commdlg macro

#include <cstring>
#include <new>
#include <string>

namespace {
constexpr uint32_t kCtor = 0xd1817b;
constexpr uint32_t kDtor = 0xd181c2;

struct Pair {
    void *original;
    cGZCommandParameterSet *ours;
    cIGZCommandParameterSet *face;
    void(__thiscall *destroyOriginal)(void *);
    explicit Pair(const difftest::Context &ctx)
    {
        original = ::operator new(sizeof(cGZCommandParameterSet));
        std::memset(original, 0xcd, sizeof(cGZCommandParameterSet));
        ctx.Original<void *(__thiscall *)(void *)>(kCtor)(original);
        destroyOriginal = ctx.Original<void(__thiscall *)(void *)>(kDtor);
        ours = new cGZCommandParameterSet();
        face = ours;
    }
    ~Pair()
    {
        destroyOriginal(original);
        delete ours;
        ::operator delete(original);
    }
};

struct AdapterPair {
    void *original;
    void *hooked;
    bool originalDestroyed = false;
    bool hookedDestroyed = false;
    void(__thiscall *destroyOriginal)(void *);
    void **hookedSlots;

    explicit AdapterPair(const difftest::Context &ctx)
    {
        original = ::operator new(sizeof(cGZCommandParameterSet));
        hooked = ::operator new(sizeof(cGZCommandParameterSet));
        std::memset(original, 0xcd, sizeof(cGZCommandParameterSet));
        std::memset(hooked, 0xcd, sizeof(cGZCommandParameterSet));
        ctx.Original<void *(__thiscall *)(void *)>(kCtor)(original);
        ctx.Original<void *(__thiscall *)(void *)>(kCtor)(hooked);
        destroyOriginal = ctx.Original<void(__thiscall *)(void *)>(kDtor);

        hookedSlots = new void *[16];
        std::memcpy(hookedSlots, ctx.image.At<void *>(cGZCommandParameterSet::kVTable), 16 * sizeof(void *));
        hookedSlots[0] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::QueryInterface);
        hookedSlots[4] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::SetParameterCount);
        hookedSlots[5] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::GetParameter);
        hookedSlots[6] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::FindParameter);
        hookedSlots[7] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::SetParameter);
        hookedSlots[8] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::AppendParameter);
        hookedSlots[9] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::RemoveParameter);
        hookedSlots[10] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::SetVersion);
        hookedSlots[11] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::FindText);
        hookedSlots[12] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::IsSpecialParameter);
        hookedSlots[13] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::GetFirstParameterIndex);
        hookedSlots[14] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::GetNextParameterIndex);
        hookedSlots[15] = reinterpret_cast<void *>(&cGZCommandParameterSetAdapters::DeletingDestructor);
        *static_cast<void ***>(hooked) = hookedSlots;
    }

    ~AdapterPair()
    {
        if (!originalDestroyed) destroyOriginal(original);
        if (!hookedDestroyed) destroyOriginal(hooked);
        ::operator delete(original);
        ::operator delete(hooked);
        delete[] hookedSlots;
    }
};

using SetFn = void(__thiscall *)(void *, uint32_t, const cIGZVariant *);
using FindFn = cRZVariant *(__thiscall *)(void *, uint32_t);
using CountFn = void(__thiscall *)(void *, uint32_t);
using OneIntFn = void(__thiscall *)(void *, uint32_t);

template <typename R, typename... Args> R CallSlot(void *face, int slot, Args... args)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, Args...)>((*reinterpret_cast<void *const *const *>(face))[slot]);
    return fn(face, args...);
}

void Put(difftest::Context &ctx, Pair &p, uint32_t key, const cIGZVariant *value)
{
    ctx.Original<SetFn>(0xd183e1)(p.original, key, value);
    CallSlot<void>(p.face, 7, key, value);
}

std::string VariantValue(cRZVariant *v)
{
    int32_t value = 0x13579bdf;
    const bool ok = v->GetValSint32(&value);
    return std::to_string(v->GetType()) + ":" + (ok ? "1:" : "0:") + std::to_string(value);
}

void CompareFind(difftest::Context &ctx, Pair &p, uint32_t key, const char *label)
{
    auto a = ctx.Original<FindFn>(0xd18360)(p.original, key);
    auto b = CallSlot<cRZVariant *>(p.face, 6, key);
    const bool sameNull = (a == nullptr) == (b == nullptr);
    if (!sameNull || (a && VariantValue(a) != VariantValue(b)))
        ctx.Fail("%s differs (original %s; ours %s)", label, a ? VariantValue(a).c_str() : "<missing>", b ? VariantValue(b).c_str() : "<missing>");
}
} // namespace

DIFF_TEST(GZCommandParameterSet_QueryInterface, 0xd18380)
{
    Pair p(ctx);
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    auto original = ctx.Original<Fn>(0xd18380);
    for (uint32_t iid : {1u, 0x4b8c39e4u, 0x4b8c39e5u, 0xdeadbeefu}) {
        void *a = nullptr, *b = nullptr;
        const uint8_t ra = original(p.original, iid, &a);
        const uint8_t rb = CallSlot<uint8_t>(p.face, 0, iid, &b);
        if (ra != rb || (ra && (a != p.original || b != p.ours))) ctx.Fail("iid %08x query result mismatch", iid);
    }
    if (reinterpret_cast<uint32_t *>(p.original)[1] != p.ours->mRefCount) ctx.Fail("query refcounts differ");
    if (CallSlot<uint32_t>(p.face, 1) != ctx.Original<uint32_t(__thiscall *)(void *)>(0x148fb0)(p.original)) ctx.Fail("AddRef differs");
    if (CallSlot<uint32_t>(p.face, 2) != ctx.Original<uint32_t(__thiscall *)(void *)>(0x1af4d8)(p.original)) ctx.Fail("Release differs");
}

DIFF_TEST(GZCommandParameterSet_SetParameterCount, 0xd1840a)
{
    Pair p(ctx);
    cRZVariant seed(int32_t(71));
    Put(ctx, p, 4, &seed); Put(ctx, p, 9, &seed);
    // Avoid only the UINT32_MAX wraparound case marked ORIGINAL-BUG in the implementation.
    ctx.Original<CountFn>(0xd1840a)(p.original, 5);
    CallSlot<void>(p.face, 4, 5u);
    if (reinterpret_cast<uint32_t *>(p.original)[3] != p.ours->mValues.size()) ctx.Fail("growth count differs");
    if (CallSlot<uint32_t>(p.face, 3) != ctx.Original<uint32_t(__thiscall *)(void *)>(0x145770)(p.original)) ctx.Fail("slot 3 count differs");
    if (ctx.Original<int32_t(__thiscall *)(void *)>(0xd18318)(p.original) != CallSlot<int32_t>(p.face, 13)) ctx.Fail("growth first key differs");
    ctx.Original<CountFn>(0xd1840a)(p.original, 2);
    CallSlot<void>(p.face, 4, 2u);
    if (reinterpret_cast<uint32_t *>(p.original)[3] != p.ours->mValues.size()) ctx.Fail("shrink count differs");
    CompareFind(ctx, p, 9, "shrink high key");
}

DIFF_TEST(GZCommandParameterSet_GetParameter, 0xd181de)
{
    Pair p(ctx); cRZVariant value(int32_t(-55)); Put(ctx, p, 12, &value);
    auto fn = ctx.Original<cRZVariant *(__thiscall *)(void *, uint32_t)>(0xd181de);
    for (uint32_t key : {12u, 13u}) {
        auto *a = fn(p.original, key); auto *b = CallSlot<cRZVariant *>(p.face, 5, key);
        if (VariantValue(a) != VariantValue(b)) ctx.Fail("key %u value differs", key);
    }
}

DIFF_TEST(GZCommandParameterSet_FindParameter, 0xd18360)
{
    Pair p(ctx); cRZVariant value(int32_t(44)); Put(ctx, p, 2, &value);
    CompareFind(ctx, p, 2, "present key"); CompareFind(ctx, p, 3, "absent key");
}

DIFF_TEST(GZCommandParameterSet_SetParameter, 0xd183e1)
{
    Pair p(ctx); cRZVariant first(int32_t(5)), second(int32_t(-123));
    Put(ctx, p, 0xdead, &first); Put(ctx, p, 0xdead, &second);
    CompareFind(ctx, p, 0xdead, "overwritten key");
}

DIFF_TEST(GZCommandParameterSet_AppendParameter, 0xd18221)
{
    Pair p(ctx); cRZVariant value(int32_t(9)); Put(ctx, p, 8, &value); Put(ctx, p, 2, &value);
    ctx.Original<void(__thiscall *)(void *, const cIGZVariant *)>(0xd18221)(p.original, &value);
    CallSlot<void>(p.face, 8, &value);
    if (ctx.Original<int32_t(__thiscall *)(void *)>(0xd18318)(p.original) != CallSlot<int32_t>(p.face, 13)) ctx.Fail("first key differs");
    CompareFind(ctx, p, 9, "appended key");
}

DIFF_TEST(GZCommandParameterSet_RemoveParameter, 0xd183c1)
{
    Pair p(ctx); cRZVariant value(int32_t(10)); Put(ctx, p, 1, &value); Put(ctx, p, 7, &value);
    ctx.Original<OneIntFn>(0xd183c1)(p.original, 1); CallSlot<void>(p.face, 9, 1u);
    CompareFind(ctx, p, 1, "removed key"); CompareFind(ctx, p, 7, "retained key");
}

DIFF_TEST(GZCommandParameterSet_SetVersion, 0xd18460)
{
    Pair p(ctx);
    ctx.Original<OneIntFn>(0xd18460)(p.original, 0x12345678); CallSlot<void>(p.face, 10, 0x12345678u);
    CompareFind(ctx, p, 0x2b999d2b, "version value");
}

DIFF_TEST(GZCommandParameterSet_FindText, 0xd18254)
{
    // Only an RZChar array (or a string object) gives GetValString's true; a Char array, the constructor from const char *,
    // writes the text and answers false, so it must not be the only kind here.
    Pair p(ctx);
    cRZVariant chars("Mixed Case"), number(int32_t(7)), text;
    text.SetRZCharArray("Mixed Case", 10);
    Put(ctx, p, 4, &text); Put(ctx, p, 9, &chars); Put(ctx, p, 11, &number);
    using Fn = uint8_t(__thiscall *)(void *, int32_t, const char *, uint32_t);
    auto fn = ctx.Original<Fn>(0xd18254);
    int found = 0;
    for (int32_t index : {4, 5, 9, 11, -1, -2})
        for (const char *needle : {"Mixed Case", "mixed case", "Case", "missing", ""})
            for (uint32_t flags : {0u, 1u, 0x100u, 0x101u}) {
                const uint8_t a = fn(p.original, index, needle, flags);
                const uint8_t b = CallSlot<uint8_t>(p.face, 11, index, needle, flags);
                if (a != b) ctx.Fail("index %d text %s flags %x differs", index, needle, flags);
                found += a != 0;
            }
    if (found == 0) ctx.Fail("no search found anything");
}

DIFF_TEST(GZCommandParameterSet_IsSpecialParameter, 0xd18490)
{
    Pair p(ctx); cRZVariant value(int32_t(1)); Put(ctx, p, 2, &value);
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    auto fn = ctx.Original<Fn>(0xd18490);
    for (uint32_t key : {2u, 3u}) for (uint32_t type : {0xffu, 0xfeu}) {
        if (fn(p.original, key, type) != CallSlot<uint8_t>(p.face, 12, key, type)) ctx.Fail("key %u type %u differs", key, type);
    }
}

DIFF_TEST(GZCommandParameterSet_GetFirstParameterIndex, 0xd18318)
{
    Pair p(ctx); auto fn = ctx.Original<int32_t(__thiscall *)(void *)>(0xd18318);
    if (fn(p.original) != CallSlot<int32_t>(p.face, 13)) ctx.Fail("empty result differs");
    cRZVariant value(int32_t(3)); Put(ctx, p, 8, &value); Put(ctx, p, 2, &value);
    if (fn(p.original) != CallSlot<int32_t>(p.face, 13)) ctx.Fail("nonempty result differs");
}

DIFF_TEST(GZCommandParameterSet_GetNextParameterIndex, 0xd1832b)
{
    Pair p(ctx); cRZVariant value(int32_t(1)); Put(ctx, p, 2, &value); Put(ctx, p, 9, &value);
    auto fn = ctx.Original<int32_t(__thiscall *)(void *, uint32_t)>(0xd1832b);
    for (uint32_t key : {0u, 2u, 8u, 9u, 10u}) if (fn(p.original, key) != CallSlot<int32_t>(p.face, 14, key)) ctx.Fail("after %u differs", key);
}

// Adapter route: the second object is built by the original constructor and carries a copy of its
// game vtable with all owned slots redirected to our adapters. Internal vcalls therefore follow
// the same adapter path as hooked game objects; a wrapper that calls itself virtually recurses.
DIFF_TEST(GZCommandParameterSet_Adapters, 0xd18380)
{
    AdapterPair p(ctx);
    using A = cGZCommandParameterSetAdapters;
    auto qi = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0xd18380);
    for (uint32_t iid : {1u, 0x4b8c39e4u, 0x4b8c39e5u, 0xdeadbeefu}) {
        void *a = nullptr, *b = nullptr;
        const uint8_t ra = qi(p.original, iid, &a);
        const uint8_t rb = A::QueryInterface(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, iid, &b);
        if (ra != rb || (ra && (a != p.original || b != p.hooked))) ctx.Fail("adapter iid %08x query differs", iid);
    }

    ctx.Original<CountFn>(0xd1840a)(p.original, 3);
    A::SetParameterCount(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, 3);
    cRZVariant number(int32_t(37));
    ctx.Original<SetFn>(0xd183e1)(p.original, 7, &number);
    A::SetParameter(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, 7, &number);
    ctx.Original<OneIntFn>(0xd18460)(p.original, 1234);
    A::SetVersion(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, 1234);
    cRZVariant text("Adapter Match");
    ctx.Original<SetFn>(0xd183e1)(p.original, 20, &text);
    A::SetParameter(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, 20, &text);
    ctx.Original<void(__thiscall *)(void *, const cIGZVariant *)>(0xd18221)(p.original, &number);
    A::AppendParameter(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, &number);

    auto get = ctx.Original<FindFn>(0xd181de);
    for (uint32_t key : {0u, 7u, 8u, 20u, 0x2b999d2bu, 99u}) {
        auto *a = get(p.original, key);
        auto *b = A::GetParameter(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, key);
        if (VariantValue(a) != VariantValue(b)) ctx.Fail("adapter get key %u differs", key);
        auto *fa = ctx.Original<FindFn>(0xd18360)(p.original, key);
        auto *fb = A::FindParameter(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, key);
        if ((fa == nullptr) != (fb == nullptr) || (fa && VariantValue(fa) != VariantValue(fb))) ctx.Fail("adapter find key %u differs", key);
    }

    using FindTextFn = uint8_t(__thiscall *)(void *, int32_t, const char *, uint32_t);
    auto findText = ctx.Original<FindTextFn>(0xd18254);
    for (int32_t index : {20, 8, 5, -2}) {
        const uint8_t ra = findText(p.original, index, "Adapter Match", 1);
        const uint8_t rb = A::FindText(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, index, "Adapter Match", 1);
        if (ra != rb) ctx.Fail("adapter text index %d differs", index);
    }
    using SpecialFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    auto special = ctx.Original<SpecialFn>(0xd18490);
    if (special(p.original, 7, 0xff) != A::IsSpecialParameter(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, 7, 0xff))
        ctx.Fail("adapter special parameter differs");

    if (ctx.Original<int32_t(__thiscall *)(void *)>(0xd18318)(p.original) !=
        A::GetFirstParameterIndex(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr)) ctx.Fail("adapter first index differs");
    for (uint32_t key : {0u, 7u, 8u, 20u, 0x2b999d2bu}) {
        const int32_t ra = ctx.Original<int32_t(__thiscall *)(void *, uint32_t)>(0xd1832b)(p.original, key);
        const int32_t rb = A::GetNextParameterIndex(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, key);
        if (ra != rb) ctx.Fail("adapter next index after %u differs", key);
    }
    ctx.Original<OneIntFn>(0xd183c1)(p.original, 7);
    A::RemoveParameter(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, 7);
    if (ctx.Original<uint32_t(__thiscall *)(void *)>(0x145770)(p.original) !=
        A::GetParameterCount(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr)) ctx.Fail("adapter count differs after erase");
    ctx.Original<CountFn>(0xd1840a)(p.original, 2);
    A::SetParameterCount(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, 2);
    if (ctx.Original<uint32_t(__thiscall *)(void *)>(0x145770)(p.original) !=
        A::GetParameterCount(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr)) ctx.Fail("adapter count differs after resize");

    using DeleteFn = void *(__thiscall *)(void *, uint32_t);
    void *ra = ctx.Original<DeleteFn>(0xd18202)(p.original, 0);
    void *rb = A::DeletingDestructor(static_cast<cGZCommandParameterSet *>(p.hooked), nullptr, 0);
    p.originalDestroyed = p.hookedDestroyed = true;
    if (ra != p.original || rb != p.hooked) ctx.Fail("adapter deleting-destructor return differs");
}

DIFF_TEST(GZCommandParameterSet_DeletingDestructor, 0xd18202)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto original = ctx.Original<Fn>(0xd18202);
    for (uint32_t flags : {0u, 1u}) {
        void *a = ::operator new(sizeof(cGZCommandParameterSet));
        std::memset(a, 0, sizeof(cGZCommandParameterSet)); ctx.Original<void *(__thiscall *)(void *)>(kCtor)(a);
        auto *b = new cGZCommandParameterSet();
        void *ra = original(a, flags);
        auto *rb = cGZCommandParameterSetAdapters::DeletingDestructor(b, nullptr, flags); // the hooked body
        if (ra != a || rb != b) ctx.Fail("flags %u return differs", flags);
        // Freed or not, by the heap itself (both sides' memory is the process heap's).
        const bool liveA = HeapValidate(GetProcessHeap(), 0, a) != 0, liveB = HeapValidate(GetProcessHeap(), 0, b) != 0;
        if (liveA != liveB || liveA != !(flags & 1)) ctx.Fail("flags %u freed: original %d ours %d", flags, !liveA, !liveB);
        if (!(flags & 1)) { ::operator delete(a); ::operator delete(b); }
    }

    void *a = ::operator new(sizeof(cGZCommandParameterSet));
    std::memset(a, 0, sizeof(cGZCommandParameterSet)); ctx.Original<void *(__thiscall *)(void *)>(kCtor)(a);
    auto *b = new cGZCommandParameterSet();
    void *ra = original(a, 0);
    auto *rb = cGZCommandParameterSetAdapters::DeletingDestructor(b, nullptr, 0);
    if (ra != a || rb != b) ctx.Fail("deleting-destructor adapter result differs");
    ::operator delete(a); ::operator delete(b);
}
