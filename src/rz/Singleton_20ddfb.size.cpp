#include "rz/Singleton_20ddfb.h"

#include "game/game.h"
#include "rz/RZFrameworkHelpers.h"
#include "game/vtable.h"

#include <windows.h>

namespace {

// Identifiers passed to the service's slot 5. The original pushes the output pointer first, then
// 0x7a3e566d, then 0x7a3e667d, so the latter is the first argument.
constexpr uint32_t kServiceIdFirst = 0x7a3e667d;
constexpr uint32_t kServiceIdSecond = 0x7a3e566d;

} // namespace

void *GetServiceObject()
{
    return ts2_rva_10c9378;
}

ScopedRef *ScopedRef::AcquireFromService()
{
    mObject = nullptr;
    void *service = GetServiceObject();
    if (service) {
        // The result of slot 5 is not used by the original.
        using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<Query, 5>(service, kServiceIdFirst, kServiceIdSecond, &mObject);
    }
    return this;
}

void ScopedRef::ReleaseObject()
{
    // The original is `try { p->Release(); } catch (...) {}` compiled with /EHs (its FuncInfo
    // flags are 5: EHs | noexcept): C++ exceptions are swallowed, SEH exceptions propagate. Our
    // /EHsc catch(...) behaves the same.
    using Release = uint32_t(__thiscall *)(void *);
    if (!mObject) return;
    try {
        VirtualCall<Release, 2>(mObject);
    } catch (...) {
    }
}

void *GetCachedSingleton()
{
    void **cached = GameAddress<void *>(0x10cc288);
    if (*cached == nullptr) {
        ScopedRef ref{nullptr};
        *cached = ref.AcquireFromService()->mObject;
        ref.ReleaseObject();
    }
    return *cached;
}
