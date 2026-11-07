#include "rz/MiscHelpersA.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"

#include <new>

namespace {

// Identifiers passed to the service's slot 5 by 0x18de70 (the output pointer is pushed first, so
// the first identifier is the last one pushed).
constexpr uint32_t kQueryFirstA = 0x85522803;
constexpr uint32_t kQuerySecondA = 0xc55289ba;
// Identifiers passed to the service's slot 5 by 0xb41750.
constexpr uint32_t kQueryFirstB = 0xa417445e;
constexpr uint32_t kQuerySecondB = 0x5a4;

// The texts (the fallback 0x18de70 returns when the held object's result is null, and the category and format
// 0x18def0 passes to cRZStd's slot 2) and the byte the constructor tests for mode 4 and 5 (non-zero enables the
// bit 3 rule) are owned statics (MiscHelpersA.h).

// Releases a held object when the enclosing function exits, on the normal path and when an
// exception unwinds through it. The original releases its holder in the same places (through
// the unwind cleanup of its frame).
struct ReleaseHeldOnExit {
    void *&object;
    ~ReleaseHeldOnExit() { ReleaseHeldObject(object); }
};

struct ScopedRefOnExit {
    ScopedRef &ref;
    ~ScopedRefOnExit() { ref.ReleaseObject(); }
};

} // namespace

__declspec(noinline) cRZStd *cRZStd::Construct(uint32_t mode, uint32_t first, uint32_t second) // a call in 0x18def0
{
    mVTable = const_cast<OwnedVTable *>(&ts2_rva_da3e6c);
    mFirst = first;
    mSecond = second;

    uint32_t bits = (mFlags & ~5u) | 0xau;
    uint32_t nibble = mode & 0x0fu;
    uint32_t value = (bits & 0x0fu) | (nibble << 4);
    mFlags = value;
    if (nibble == 8) return this;

    if (ts2_rva_ff246c != 0 && (nibble == 4 || nibble == 5)) value &= ~8u;
    if (value >> 4 == 4 || value >> 4 == 5 || value >> 4 == 7) value |= 4;
    if (value < 0x10 || value >> 4 == 6 || value >> 4 == 1 || value >> 4 == 2) value |= 1;
    mFlags = value;
    return this;
}

void cTSAudioAmbienceGlobalPrimitiveBase::Construct()
{
    // A single store, so eax and the other registers are untouched, as in the original.
    mVTable = GameAddress(kVTable);
}

void *TreeEraser::EraseSubtree(void *head, TreeNodeMapped *node) noexcept
{
    using Release = uint32_t(__thiscall *)(void *);
    while (!node->mIsNil) {
        EraseSubtree(head, node->mRight);
        TreeNodeMapped *left = node->mLeft;
        if (void *value = node->mValue) VirtualCall<Release, 2>(value);
        ::operator delete(node, TreeNodeMapped::kSize);
        node = left;
    }
    return this;
}

__declspec(noinline) void *QueryServiceValue(uint32_t arg) // a call in 0x18def0, as in the original
{
    using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
    using Method = uint32_t(__thiscall *)(void *, uint32_t);

    void *held = nullptr;
    ReleaseHeldOnExit release{held};
    void *service = GetServiceObject();
    if (service) VirtualCall<Query, 5>(service, kQueryFirstA, kQuerySecondA, &held);
    // The original does not check held here; a missing service ends in a null call.
    void *result = reinterpret_cast<void *>(VirtualCall<Method, 30>(held, arg));
    return result ? result : const_cast<char *>(ts2_rva_da1088);
}

namespace {

// cRZStd's vtable as a C++ interface, for its varargs slot 2 (never constructed: game objects are viewed
// through it).
struct cRZStdReport {
    virtual void __cdecl Slot0(const char *, ...) = 0;
    virtual void Slot1() = 0;
    // Slot 2: (category, number, format, format arguments...).
    virtual void __cdecl Report(const char *category, uint32_t number, const char *format, ...) = 0;
};

} // namespace

void LogD3DError(uint32_t code, uint32_t first, uint32_t second)
{

    cRZStd raw; // Construct fixes every bit of mFlags it keeps, so the stack contents don't matter
    cRZStd *log = raw.Construct(3, first, second);
    // Slot 2 is a varargs member (cdecl, this first); MSVC reads the vtable of a varargs virtual call
    // before evaluating the arguments, as the original does.
    reinterpret_cast<cRZStdReport *>(log)->Report(ts2_rva_da10b8, 3u, ts2_rva_da10a0, QueryServiceValue(code));
}

void ServiceCallWithHeld(uint32_t arg1, uint32_t arg2)
{
    using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
    using Method = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

    ScopedRef held{nullptr};
    ScopedRefOnExit release{held};
    void *service = GetServiceObject();
    if (service) VirtualCall<Query, 5>(service, kQueryFirstB, kQuerySecondB, &held.mObject);
    VirtualCall<Method, 7>(held.mObject, 0xf, arg1, arg2);
}
