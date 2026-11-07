#include "gz/MultiMon.h"

#include <cstring>

#include "game/game.h"
#include "game/import_slots.h"

namespace nMultiMon {

namespace {

// The game's statics (multimon.h's g_ pointers and flags, MultiMon.h) and the import slots the stubs call through.
FARPROC &kGetSystemMetrics = ts2_rva_10c96b8, &kMonitorFromWindow = ts2_rva_10c96bc, &kMonitorFromRect = ts2_rva_10c96c0;
FARPROC &kMonitorFromPoint = ts2_rva_10c96c4, &kGetMonitorInfo = ts2_rva_10c96c8, &kEnumDisplayMonitors = ts2_rva_10c96cc;
FARPROC &kEnumDisplayDevices = ts2_rva_10c96d0;
uint32_t &kInitDone = ts2_rva_10c96d4, &kPlatformNT = ts2_rva_10c96d8;
constexpr const char *kUserModuleName = "USER32";
// The seven functions in the order they are looked up, with their USER32 names.
FARPROC *const kTargets[7] = {&kGetSystemMetrics, &kMonitorFromWindow, &kMonitorFromRect, &kMonitorFromPoint,
                              &kEnumDisplayMonitors, &kGetMonitorInfo, &kEnumDisplayDevices};
constexpr const char *kNames[7] = {"GetSystemMetrics", "MonitorFromWindow", "MonitorFromRect", "MonitorFromPoint",
                                   "EnumDisplayMonitors", "GetMonitorInfoA", "EnumDisplayDevicesA"};

constexpr IatSlot kIatGetVersionEx = &ts2_rva_d9a138, kIatGetModuleHandle = &ts2_rva_d9a114, kIatGetProcAddress = &ts2_rva_d9a124;
constexpr IatSlot kIatGetSystemMetrics = &ts2_rva_d9a4a4, kIatGetClipBox = &ts2_rva_d9a0b4, kIatGetDCOrgEx = &ts2_rva_d9a0b0;
constexpr IatSlot kIatOffsetRect = &ts2_rva_d9a4ec, kIatIntersectRect = &ts2_rva_d9a4e8, kIatIsIconic = &ts2_rva_d9a4a0;
constexpr IatSlot kIatGetWindowPlacement = &ts2_rva_d9a50c, kIatGetWindowRect = &ts2_rva_d9a4a8;

template <class Fn, void **Iat> Fn Import() { return ImportSlot<Fn, Iat>(); }

FARPROC &Slot(FARPROC &cell) { return cell; }

// The USER32 GetSystemMetrics the screen size is asked of (the resolved pointer; the stubs call it with it null only on
// the fallback, where the import is used).
int Metric(int index) { return Import<int(WINAPI *)(int), kIatGetSystemMetrics>()(index); }

} // namespace

bool InitMultipleMonitorStubs()
{
    if (kInitDone) return Slot(kGetMonitorInfo) != nullptr;
    OSVERSIONINFOA version;
    std::memset(&version.dwMajorVersion, 0, 0x90);
    version.dwOSVersionInfoSize = 0x94;
    Import<BOOL(WINAPI *)(OSVERSIONINFOA *), kIatGetVersionEx>()(&version);
    kPlatformNT = version.dwPlatformId == 2;
    const HMODULE user = Import<HMODULE(WINAPI *)(LPCSTR), kIatGetModuleHandle>()(kUserModuleName);
    if (user) {
        // The seven functions, in this order, each stored as it is found; the first missing one ends the search.
        const auto getProc = Import<FARPROC(WINAPI *)(HMODULE, LPCSTR), kIatGetProcAddress>();
        bool all = true;
        for (int i = 0; i < 7 && all; ++i) {
            *kTargets[i] = getProc(user, kNames[i]);
            all = *kTargets[i] != nullptr;
        }
        if (all) {
            kInitDone = 1;
            return true;
        }
    }
    for (FARPROC *const slot : kTargets) *slot = nullptr;
    kInitDone = 1;
    return FALSE;
}

int WINAPI GetSystemMetricsStub(int index)
{
    if (InitMultipleMonitorStubs()) return reinterpret_cast<int(WINAPI *)(int)>(Slot(kGetSystemMetrics))(index);
    switch (index) {
    case SM_XVIRTUALSCREEN:
    case SM_YVIRTUALSCREEN: return 0;
    case SM_CXVIRTUALSCREEN: index = SM_CXSCREEN; break;
    case SM_CYVIRTUALSCREEN: index = SM_CYSCREEN; break;
    case SM_CMONITORS:
    case SM_SAMEDISPLAYFORMAT: return 1;
    default: break;
    }
    return Metric(index);
}

BOOL WINAPI EnumDisplayMonitorsStub(HDC dc, const RECT *clip, MONITORENUMPROC enumProc, LPARAM data)
{
    if (InitMultipleMonitorStubs())
        return reinterpret_cast<BOOL(WINAPI *)(HDC, const RECT *, MONITORENUMPROC, LPARAM)>(Slot(kEnumDisplayMonitors))(
            dc, clip, enumProc, data);
    if (!enumProc) return FALSE;
    RECT screen;
    screen.left = 0;
    screen.top = 0;
    screen.right = Metric(0);
    screen.bottom = Metric(1);
    if (dc) {
        RECT box;
        switch (Import<int(WINAPI *)(HDC, RECT *), kIatGetClipBox>()(dc, &box)) {
        case 0: return FALSE;
        case 1: return true;
        default: {
            POINT origin;
            if (!Import<BOOL(WINAPI *)(HDC, POINT *), kIatGetDCOrgEx>()(dc, &origin)) return FALSE;
            Import<BOOL(WINAPI *)(RECT *, int, int), kIatOffsetRect>()(&screen, -origin.x, -origin.y);
            if (!Import<BOOL(WINAPI *)(RECT *, const RECT *, const RECT *), kIatIntersectRect>()(&screen, &screen, &box))
                return true;
        }
        }
    }
    if (clip && !Import<BOOL(WINAPI *)(RECT *, const RECT *, const RECT *), kIatIntersectRect>()(&screen, &screen, clip))
        return true;
    return enumProc(reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(kFakeMonitor)), dc, &screen, data);
}

HMONITOR WINAPI MonitorFromWindowStub(HWND window, DWORD flags)
{
    if (InitMultipleMonitorStubs())
        return reinterpret_cast<HMONITOR(WINAPI *)(HWND, DWORD)>(Slot(kMonitorFromWindow))(window, flags);
    if (flags & 3) return reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(kFakeMonitor));
    // ORIGINAL-BUG: the placement's length is never set (GetWindowPlacement wants it), so for an iconic window the
    // call works only by luck of the stack.
    WINDOWPLACEMENT placement;
    BOOL found;
    if (Import<BOOL(WINAPI *)(HWND), kIatIsIconic>()(window))
        found = Import<BOOL(WINAPI *)(HWND, WINDOWPLACEMENT *), kIatGetWindowPlacement>()(window, &placement);
    else
        found = Import<BOOL(WINAPI *)(HWND, RECT *), kIatGetWindowRect>()(window, &placement.rcNormalPosition);
    if (!found) return nullptr;
    const RECT rect = placement.rcNormalPosition;
    if (InitMultipleMonitorStubs())
        return reinterpret_cast<HMONITOR(WINAPI *)(const RECT *, DWORD)>(Slot(kMonitorFromRect))(&rect, flags);
    if (rect.right > 0 && rect.bottom > 0 && rect.left < Metric(0) && rect.top < Metric(1))
        return reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(kFakeMonitor));
    return nullptr;
}

} // namespace nMultiMon
