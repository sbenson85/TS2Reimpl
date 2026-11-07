#include "gz/cDisplayMonitorW32.h"

#include <cstring>

#include "game/import_slots.h"
#include "gz/MultiMon.h"

namespace nGZGraphic4 {

namespace {

constexpr IatSlot kIatSystemParametersInfo = &ts2_rva_d9a51c, kIatGetSystemMetrics = &ts2_rva_d9a4a4;
constexpr IatSlot kIatLstrcpy = &ts2_rva_d9a13c, kIatCreateDC = &ts2_rva_d9a0bc, kIatGetDeviceCaps = &ts2_rva_d9a0c0;
constexpr IatSlot kIatDeleteDC = &ts2_rva_d9a0b8;

constexpr uint32_t kIidUnknown = 1;
constexpr const char kUnknownName[] = "UNKNOWN";  // the name until a monitor is set
constexpr const char kFallbackDevice[] = "DISPLAY"; // the one-monitor fallback's device name
constexpr RECT kDefaultRect = {0, 0, 800, 600};     // the rectangle until a monitor is set

// multimon.h's GetMonitorInfo stub, inlined in SetMonitor: USER32's when the stubs found it, else the one-monitor
// fallback's answer for the stand-in handle (the screen, the work area, primary, "DISPLAY"), which writes nothing for
// any other handle or when the work area can't be read. The result is not used.
void MonitorInfo(HMONITOR monitor, MONITORINFOEXA &info)
{
    if (nMultiMon::InitMultipleMonitorStubs()) {
        reinterpret_cast<BOOL(WINAPI *)(HMONITOR, MONITORINFO *)>(ts2_rva_10c96c8)(monitor, &info);
        return;
    }
    if (reinterpret_cast<uintptr_t>(monitor) != nMultiMon::kFakeMonitor || info.cbSize < sizeof(MONITORINFO)) return;
    RECT work;
    if (!ImportSlot<BOOL(WINAPI *)(UINT, UINT, void *, UINT), kIatSystemParametersInfo>()(SPI_GETWORKAREA, 0, &work, 0))
        return;
    const auto metric = ImportSlot<int(WINAPI *)(int), kIatGetSystemMetrics>();
    info.rcMonitor.left = 0;
    info.rcMonitor.top = 0;
    info.rcMonitor.right = metric(SM_CXSCREEN);
    info.rcMonitor.bottom = metric(SM_CYSCREEN);
    info.rcWork = work;
    info.dwFlags = MONITORINFOF_PRIMARY;
    if (info.cbSize >= sizeof(MONITORINFOEXA))
        ImportSlot<LPSTR(WINAPI *)(LPSTR, LPCSTR), kIatLstrcpy>()(info.szDevice, kFallbackDevice);
}

} // namespace

cDisplayMonitorW32 *__cdecl cDisplayMonitorW32::Create() { return new cDisplayMonitorW32; }

bool cDisplayMonitorW32::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != kIidUnknown && riid != kIidOther && riid != kIidDisplayMonitor) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cDisplayMonitorW32::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

RECT *cDisplayMonitorW32::GetWorkRect(RECT *out)
{
    *out = mWorkRect;
    return out;
}

RECT *cDisplayMonitorW32::GetMonitorRect(RECT *out)
{
    *out = mMonitorRect;
    return out;
}

void cDisplayMonitorW32::SetMonitor(HMONITOR monitor)
{
    mMonitor = monitor;
    mDeviceName.assign(kUnknownName, sizeof(kUnknownName) - 1);
    mAspect = 0.0f;
    mMonitorRect = kDefaultRect;
    if (!monitor) return;

    // ORIGINAL-BUG: the original leaves the structure (but its size) uninitialised, so where the stub writes nothing
    // (the fallback for another handle, or no work area) it takes stack bytes as the name and rectangles. Ours starts
    // it zeroed: an empty name, empty rectangles.
    MONITORINFOEXA info{};
    info.cbSize = sizeof(info);
    MonitorInfo(monitor, info);
    mDeviceName.assign(info.szDevice, std::strlen(info.szDevice));
    mMonitorRect = info.rcMonitor;
    mWorkRect = info.rcWork;

    const HDC dc = ImportSlot<HDC(WINAPI *)(LPCSTR, LPCSTR, LPCSTR, const DEVMODEA *), kIatCreateDC>()(
        info.szDevice, info.szDevice, nullptr, nullptr);
    if (!dc) return;
    const auto caps = ImportSlot<int(WINAPI *)(HDC, int), kIatGetDeviceCaps>();
    const int width = caps(dc, HORZSIZE);
    const int height = caps(dc, VERTSIZE);
    if (width != 0 && height != 0) mAspect = static_cast<float>(width) / static_cast<float>(height);
    ImportSlot<BOOL(WINAPI *)(HDC), kIatDeleteDC>()(dc);
}

} // namespace nGZGraphic4
