// Differential tests for the reference holders in src/rz/RZAutoRef.h. The objects they hold are
// fakes whose vtable slots 1 (AddRef) and 2 (Release) record their calls, so the call sequences
// are compared along with the results and the holders' fields. The virtual-base functions use a
// fake object laid out with a vbptr at +4 and a displacement table, and the subobject's vtable
// records its calls.
//
// Exceptions: ReleaseObject (0x1466e0) has a try/catch(...) built with /EHs, so C++ exceptions from
// Release are swallowed and SEH propagates. The other holders have no handler and propagate both.
#include "difftest.h"
#include "rz/RZAutoRef.h"

#include <windows.h>

#include <cstring>
#include <new>
#include <vector>

namespace {

using Method = void(__thiscall *)(void *);
using MethodReturningThis = RZAutoRef *(__thiscall *)(void *);
using DeletingDtor = RZAutoRef *(__thiscall *)(void *, uint32_t);
using AssignMethod = void(__thiscall *)(void *, void *);

struct Call {
    int slot;
    const void *self;
};

std::vector<Call> gCalls;

// Fakes are called as __thiscall: `this` in ecx, and a __fastcall function with a dummy edx takes
// the same registers with nothing left on the stack.
uint32_t __fastcall RecordAddRef(void *self, void * /*edx*/)
{
    gCalls.push_back({1, self});
    return 2;
}

uint32_t __fastcall RecordRelease(void *self, void * /*edx*/)
{
    gCalls.push_back({2, self});
    return 0;
}

uint32_t __fastcall RecordThenThrow(void *self, void * /*edx*/)
{
    gCalls.push_back({2, self});
    throw 42;
}

uint32_t __fastcall RecordThenRaise(void *self, void * /*edx*/)
{
    gCalls.push_back({2, self});
    RaiseException(0xE0001234, 0, 0, nullptr);
    return 0;
}

void *gVTable[8];       // slots 1 and 2 record
void *gThrowVTable[8];  // slot 2 records, then throws a C++ exception
void *gRaiseVTable[8];  // slot 2 records, then raises an SEH exception

struct FakeObject {
    void *vtable;
    uint32_t pad;
};

FakeObject gA{}, gB{}, gThrowing{}, gRaising{};

void SetupFakes()
{
    std::memset(gVTable, 0, sizeof(gVTable));
    std::memset(gThrowVTable, 0, sizeof(gThrowVTable));
    std::memset(gRaiseVTable, 0, sizeof(gRaiseVTable));
    gVTable[1] = reinterpret_cast<void *>(&RecordAddRef);
    gVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gThrowVTable[2] = reinterpret_cast<void *>(&RecordThenThrow);
    gRaiseVTable[2] = reinterpret_cast<void *>(&RecordThenRaise);
    gA.vtable = gB.vtable = gVTable;
    gThrowing.vtable = gThrowVTable;
    gRaising.vtable = gRaiseVTable;
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].self != b[i].self) return false;
    return true;
}

// Calls fn(ref) and returns the code of the exception that escapes it: 0 for none, the SEH code
// for an SEH exception, and 0xE06D7363 for a C++ exception (which __except sees as an SEH code).
// The function has no C++ objects, as __try requires.
void *gMethodFn = nullptr;
void CallOriginal(void *ref)
{
    reinterpret_cast<Method>(gMethodFn)(ref);
}
void CallOurs(void *ref)
{
    static_cast<RZAutoRef *>(ref)->ReleaseObject();
}
void CallOursUnguarded(void *ref)
{
    static_cast<RZAutoRef *>(ref)->ReleaseUnguarded();
}
unsigned long EscapedCode(void (*fn)(void *), void *ref)
{
    __try {
        fn(ref);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

// Virtual-base fixture: the object sits in gVBMem with its vbptr at +4 pointing at a two-entry
// table whose entry 1 is the displacement to the base subobject, which has its own vtable.
alignas(16) uint8_t gVBMem[256];
int32_t gVBTable[2];

void *SetupVirtualBase(int32_t displacement)
{
    std::memset(gVBMem, 0, sizeof(gVBMem));
    uint8_t *object = gVBMem + 64;
    gVBTable[0] = 0;
    gVBTable[1] = displacement;
    *reinterpret_cast<void **>(object + 4) = gVBTable;
    *reinterpret_cast<void **>(object + 4 + displacement) = gVTable; // the base subobject's vtable
    return object;
}

template <uint32_t Offset> void FillOwner(RZRefField<Offset> &owner, void *reference)
{
    std::memset(&owner, 0xAB, sizeof(owner)); // bytes before the reference must not change
    owner.mRef = reference;
}

} // namespace

DIFF_TEST(RZAutoRef_ReleaseObject, 0x1466e0)
{
    SetupFakes();
    auto orig = ctx.Original<Method>(0x1466e0);
    for (void *object : {static_cast<void *>(nullptr), static_cast<void *>(&gA)}) {
        RZAutoRef a{object}, b{object};
        gCalls.clear();
        orig(&a);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        b.ReleaseObject();
        ++ctx.cases;
        if (a.mObject != b.mObject || !SameCalls(callsA, gCalls))
            ctx.Fail("ReleaseObject object=%p: original %zu calls, ours %zu calls", object, callsA.size(), gCalls.size());
    }
}

DIFF_TEST(RZAutoRef_ReleaseObjectThrows, 0x1466e0)
{
    SetupFakes();
    gMethodFn = ctx.image.At(0x1466e0);
    for (FakeObject *object : {&gThrowing, &gRaising}) {
        RZAutoRef a{object}, b{object};
        gCalls.clear();
        unsigned long ea = EscapedCode(&CallOriginal, &a);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        unsigned long eb = EscapedCode(&CallOurs, &b);
        ++ctx.cases;
        if (ea != eb || !SameCalls(callsA, gCalls))
            ctx.Fail("ReleaseObject with %s: original escaped 0x%08lx (%zu calls), ours 0x%08lx (%zu calls)",
                     object == &gThrowing ? "C++ throw" : "SEH raise", ea, callsA.size(), eb, gCalls.size());
    }
}

DIFF_TEST(RZAutoRef_ReleaseUnguarded, 0x1cbd45)
{
    SetupFakes();
    auto orig = ctx.Original<Method>(0x1cbd45);
    auto orig2 = ctx.Original<Method>(0x145d50);
    for (Method o : {orig, orig2}) {
        for (void *object : {static_cast<void *>(nullptr), static_cast<void *>(&gA)}) {
            RZAutoRef a{object}, b{object};
            gCalls.clear();
            o(&a);
            std::vector<Call> callsA = gCalls;
            gCalls.clear();
            b.ReleaseUnguarded();
            ++ctx.cases;
            if (a.mObject != b.mObject || !SameCalls(callsA, gCalls))
                ctx.Fail("ReleaseUnguarded object=%p: original %zu calls, ours %zu calls", object, callsA.size(), gCalls.size());
        }
    }
}

DIFF_TEST(RZAutoRef_ReleaseUnguardedThrows, 0x145d50)
{
    SetupFakes();
    gMethodFn = ctx.image.At(0x145d50);
    // A C++ exception from Release is not tested: the original's frame is noexcept, so the C++
    // exception terminates the process there (0xC0000409), and ours does the same. Running that
    // would end the test runner, so only the SEH case, which propagates in both, is compared.
    {
        RZAutoRef a{&gRaising}, b{&gRaising};
        gCalls.clear();
        unsigned long ea = EscapedCode(&CallOriginal, &a);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        unsigned long eb = EscapedCode(&CallOursUnguarded, &b);
        ++ctx.cases;
        if (ea != eb || !SameCalls(callsA, gCalls))
            ctx.Fail("ReleaseUnguarded SEH raise: original escaped 0x%08lx (%zu calls), ours 0x%08lx (%zu calls)", ea, callsA.size(), eb,
                     gCalls.size());
    }
}

DIFF_TEST(RZAutoRef_DeletingDestructor, 0x142196)
{
    SetupFakes();
    auto orig = ctx.Original<DeletingDtor>(0x142196);
    for (uint32_t flags = 0; flags < 4; ++flags) {
        for (void *object : {static_cast<void *>(nullptr), static_cast<void *>(&gA)}) {
            // The holders are heap blocks of the right size. Flags with bit 0 delete them, so
            // neither is inspected after the call.
            auto *a = static_cast<RZAutoRef *>(::operator new(sizeof(RZAutoRef)));
            auto *b = static_cast<RZAutoRef *>(::operator new(sizeof(RZAutoRef)));
            a->mObject = b->mObject = object;
            gCalls.clear();
            RZAutoRef *ra = orig(a, flags);
            std::vector<Call> callsA = gCalls;
            gCalls.clear();
            RZAutoRef *rb = b->DeletingDestructor(flags);
            ++ctx.cases;
            if (ra != a || rb != b || !SameCalls(callsA, gCalls))
                ctx.Fail("DeletingDestructor object=%p flags %u: original %zu calls, ours %zu calls", object, flags, callsA.size(), gCalls.size());
            if (!(flags & 1)) {
                ::operator delete(a, sizeof(RZAutoRef));
                ::operator delete(b, sizeof(RZAutoRef));
            }
        }
    }
}

DIFF_TEST(RZAutoRef_Reset, 0x1dae60)
{
    SetupFakes();
    auto orig = ctx.Original<MethodReturningThis>(0x1dae60);
    for (void *object : {static_cast<void *>(nullptr), static_cast<void *>(&gA)}) {
        RZAutoRef a{object}, b{object};
        gCalls.clear();
        RZAutoRef *ra = orig(&a);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        RZAutoRef *rb = b.Reset();
        ++ctx.cases;
        if (ra != &a || rb != &b || a.mObject != b.mObject || !SameCalls(callsA, gCalls))
            ctx.Fail("Reset object=%p: original (%p, %p, %zu calls), ours (%p, %p, %zu calls)", object, ra, a.mObject, callsA.size(), rb,
                     b.mObject, gCalls.size());
    }
}

DIFF_TEST(RZAutoRef_VirtualBase, 0x210cfc)
{
    SetupFakes();
    auto orig = ctx.Original<Method>(0x210cfc);
    auto orig2 = ctx.Original<Method>(0x1fcd10);
    for (int32_t displacement : {8, 0x10, 0x20, -8, 0x40}) {
        void *object = SetupVirtualBase(displacement);
        for (Method o : {orig, orig2}) {
            for (void *holder : {static_cast<void *>(nullptr), object}) {
                RZAutoRef a{holder}, b{holder};
                gCalls.clear();
                o(&a);
                std::vector<Call> callsA = gCalls;
                gCalls.clear();
                b.ReleaseVirtualBase();
                ++ctx.cases;
                if (a.mObject != b.mObject || !SameCalls(callsA, gCalls))
                    ctx.Fail("ReleaseVirtualBase displacement %d holder %p: original %zu calls, ours %zu calls", displacement, holder,
                             callsA.size(), gCalls.size());
            }
        }
    }
}

DIFF_TEST(RZAutoRef_ResetVirtualBase, 0x2296b4)
{
    SetupFakes();
    auto orig = ctx.Original<MethodReturningThis>(0x2296b4);
    auto orig2 = ctx.Original<MethodReturningThis>(0x258c70);
    for (int32_t displacement : {8, 0x10, 0x20, -8, 0x40}) {
        void *object = SetupVirtualBase(displacement);
        for (MethodReturningThis o : {orig, orig2}) {
            for (void *holder : {static_cast<void *>(nullptr), object}) {
                RZAutoRef a{holder}, b{holder};
                gCalls.clear();
                RZAutoRef *ra = o(&a);
                std::vector<Call> callsA = gCalls;
                gCalls.clear();
                RZAutoRef *rb = b.ResetVirtualBase();
                ++ctx.cases;
                if (ra != &a || rb != &b || a.mObject != b.mObject || !SameCalls(callsA, gCalls))
                    ctx.Fail("ResetVirtualBase displacement %d holder %p: original (%p, %zu calls), ours (%p, %zu calls)", displacement,
                             holder, a.mObject, callsA.size(), rb == &b ? b.mObject : nullptr, gCalls.size());
            }
        }
    }
}

DIFF_TEST(RZAutoRef_RefFieldRelease, 0x416c90)
{
    SetupFakes();
    auto orig = ctx.Original<Method>(0x416c90);
    for (void *reference : {static_cast<void *>(nullptr), static_cast<void *>(&gA)}) {
        RZRefField<0x10> a{}, b{};
        FillOwner(a, reference);
        FillOwner(b, reference);
        gCalls.clear();
        orig(&a);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        b.ReleaseRef();
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0 || !SameCalls(callsA, gCalls))
            ctx.Fail("RefField release reference=%p: original %zu calls, ours %zu calls", reference, callsA.size(), gCalls.size());
    }
}

namespace {

// Assignments of every pair of current and new reference: the same reference (the comparison's
// early exit), null, and a different object.
template <uint32_t Offset, typename Ours>
void AssignTest(difftest::Context &ctx, const char *name, uint32_t rva, Ours ours)
{
    auto orig = ctx.Original<AssignMethod>(rva);
    void *currents[] = {nullptr, &gA, &gB};
    void *news[] = {nullptr, &gA, &gB};
    for (void *current : currents)
        for (void *news_ : news) {
            RZRefField<Offset> a{}, b{};
            FillOwner(a, current);
            FillOwner(b, current);
            gCalls.clear();
            orig(&a, news_);
            std::vector<Call> callsA = gCalls;
            gCalls.clear();
            (b.*ours)(news_);
            ++ctx.cases;
            if (std::memcmp(&a, &b, sizeof(a)) != 0 || !SameCalls(callsA, gCalls))
                ctx.Fail("%s current %p new %p: original %zu calls, ours %zu calls", name, current, news_, callsA.size(), gCalls.size());
        }
}

} // namespace

DIFF_TEST(RZAutoRef_RefFieldAssign, 0x9981d0)
{
    SetupFakes();
    AssignTest<0x10>(ctx, "RefField<0x10>::AssignRef", 0x9981d0, &RZRefField<0x10>::AssignRef);
}

DIFF_TEST(RZAutoRef_RefFieldAssignAlways, 0x991253)
{
    SetupFakes();
    AssignTest<0x10>(ctx, "RefField<0x10>::AssignRefAlways", 0x991253, &RZRefField<0x10>::AssignRefAlways);
}

DIFF_TEST(RZAutoRef_RefFieldAssignAt28, 0x9d0c32)
{
    SetupFakes();
    AssignTest<0x28>(ctx, "RefField<0x28>::AssignRef", 0x9d0c32, &RZRefField<0x28>::AssignRef);
}
