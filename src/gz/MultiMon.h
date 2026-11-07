#pragma once
// The multi-monitor shim (lane OS, rendering plan 4c): the stubs of multimon.h the game was built with. They look the
// multi-monitor functions up in USER32 once (InitMultipleMonitorStubs, keeping the pointers in the game's statics) and
// fall back to a one-monitor desktop where USER32 lacks them. docs/subsystems/rendering.md.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

#include "game/owned.h"

// The stubs' statics: the seven USER32 pointers (GetSystemMetrics, MonitorFromWindow, MonitorFromRect,
// MonitorFromPoint, GetMonitorInfoA, EnumDisplayMonitors, EnumDisplayDevicesA, in address order), the done flag and
// the NT flag.
TS2_OWNED_STATIC(FARPROC, 10c96b8);
TS2_OWNED_STATIC(FARPROC, 10c96bc);
TS2_OWNED_STATIC(FARPROC, 10c96c0);
TS2_OWNED_STATIC(FARPROC, 10c96c4);
TS2_OWNED_STATIC(FARPROC, 10c96c8);
TS2_OWNED_STATIC(FARPROC, 10c96cc);
TS2_OWNED_STATIC(FARPROC, 10c96d0);
TS2_OWNED_STATIC(uint32_t, 10c96d4);
TS2_OWNED_STATIC(uint32_t, 10c96d8);

namespace nMultiMon {

// The stand-in monitor handle the one-monitor fallback answers with.
constexpr uint32_t kFakeMonitor = 0x12340042;

// 0x1547f0 (cdecl): resolves the seven USER32 functions once and says whether the real ones are there (the check is
// GetMonitorInfoA's). Shared with game code that reads the same statics (the display monitor class), so it is hooked.
bool InitMultipleMonitorStubs();

// 0x154a80 (stdcall): GetSystemMetrics, or, on the one-monitor fallback, the virtual screen as the screen: its origin 0,
// its size the screen's, one monitor of one display format; any other index asked of the GetSystemMetrics import.
int WINAPI GetSystemMetricsStub(int index);

// 0x154950 (stdcall): EnumDisplayMonitors, or, on the one-monitor fallback, one call of `enumProc` with the screen
// (cut to the device context's clip box and `clip`).
BOOL WINAPI EnumDisplayMonitorsStub(HDC dc, const RECT *clip, MONITORENUMPROC enumProc, LPARAM data);

// 0x154ae0 (stdcall): MonitorFromWindow, or, on the one-monitor fallback, the stand-in handle when the window (or, with
// `flags` bits 0-1 set, always) touches the screen, else 0.
HMONITOR WINAPI MonitorFromWindowStub(HWND window, DWORD flags);

} // namespace nMultiMon
