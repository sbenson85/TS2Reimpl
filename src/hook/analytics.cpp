// Blocks the launch analytics event (docs/startup-map.md): on every start, WinMain's helper 0xc2df67
// POSTs a "boot_start" event to EA (a hash derived from the Steam ID, timestamps, storefront). It
// opens WinINet with InternetOpenA and skips the request if that fails, so the filter fails exactly
// that call (identified by its return address) and passes every other InternetOpenA through.
#include "hook/analytics.h"

#include "game/game.h"
#include "hook/log.h"

#include <windows.h>
#include <intrin.h>

namespace {

constexpr uint32_t kCallSiteReturn = 0xc2df83; // after `call [InternetOpenA]` in 0xc2df67

using InternetOpenAFn = void *(WINAPI *)(const char *, DWORD, const char *, const char *, DWORD);
InternetOpenAFn gReal = nullptr;

void *WINAPI FilteredInternetOpenA(const char *agent, DWORD access, const char *proxy, const char *bypass, DWORD flags)
{
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == GameBase() + kCallSiteReturn) {
        Log("analytics: blocked the launch event");
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    return gReal(agent, access, proxy, bypass, flags);
}

} // namespace

void *AnalyticsFilterFor(void *realInternetOpenA)
{
    gReal = reinterpret_cast<InternetOpenAFn>(realInternetOpenA);
    return reinterpret_cast<void *>(&FilteredInternetOpenA);
}
