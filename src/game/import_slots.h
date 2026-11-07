#pragma once
#include "game/owned.h"

// Import address table slots owned as statics (milestone O, lane O-c; docs/subsystems/scenegraph.md, "Milestone O").
// The same call as ImportFunction (game/import.h), `call dword ptr [slot]`, through a named slot instead of game base +
// RVA:
//
//   ImportSlot<BOOL(WINAPI *)(HWND, LPRECT), &ts2_rva_d9a4a8>()(hwnd, &rect)   // GetWindowRect
//
// - Hooked builds: the symbol is the game's slot (tools/origsyms.py), so the game's loader-resolved import is called
//   and the differential test harness's fakes (tests/app_fixture.h, IatPatch, which write the game's slot) still apply.
// - Image-free programs: the program defines the slot, initialised with the function its own import table resolves
//   (standalone/owned_graphics.cpp, `= reinterpret_cast<void *>(::GetWindowRect)`).
//
// One declaration per slot, here, whichever class calls it; a class switching from ImportFunction to its slot adds the
// slot here and its definition to the image-free program that links it.
template <class Fn, void **Slot> __forceinline Fn ImportSlot() { return reinterpret_cast<Fn>(*Slot); }

// A named slot constant: `constexpr IatSlot kIatGetDC = &ts2_rva_d9a518;`, then ImportSlot<Fn, kIatGetDC>().
using IatSlot = void **;

// GDI32
TS2_OWNED_STATIC(void *, d9a0a8); // DeleteObject
TS2_OWNED_STATIC(void *, d9a0ac); // CreateSolidBrush
TS2_OWNED_STATIC(void *, d9a0b0); // GetDCOrgEx
TS2_OWNED_STATIC(void *, d9a0b4); // GetClipBox
TS2_OWNED_STATIC(void *, d9a0b8); // DeleteDC
TS2_OWNED_STATIC(void *, d9a0bc); // CreateDCA
TS2_OWNED_STATIC(void *, d9a0c0); // GetDeviceCaps
// KERNEL32
TS2_OWNED_STATIC(void *, d9a10c); // SetEvent
TS2_OWNED_STATIC(void *, d9a114); // GetModuleHandleA
TS2_OWNED_STATIC(void *, d9a124); // GetProcAddress
TS2_OWNED_STATIC(void *, d9a138); // GetVersionExA
TS2_OWNED_STATIC(void *, d9a13c); // lstrcpyA
TS2_OWNED_STATIC(void *, d9a140); // SetLastError
TS2_OWNED_STATIC(void *, d9a144); // Sleep
TS2_OWNED_STATIC(void *, d9a148); // EnumResourceNamesA
TS2_OWNED_STATIC(void *, d9a14c); // SetThreadExecutionState
TS2_OWNED_STATIC(void *, d9a150); // GetLocaleInfoA
TS2_OWNED_STATIC(void *, d9a164); // GetEnvironmentVariableA
TS2_OWNED_STATIC(void *, d9a16c); // GetModuleFileNameA
TS2_OWNED_STATIC(void *, d9a17c); // CloseHandle
TS2_OWNED_STATIC(void *, d9a184); // WaitForSingleObject
TS2_OWNED_STATIC(void *, d9a188); // SleepEx
TS2_OWNED_STATIC(void *, d9a190); // GetCurrentThread
TS2_OWNED_STATIC(void *, d9a198); // SetThreadPriority
TS2_OWNED_STATIC(void *, d9a19c); // GetThreadPriority
TS2_OWNED_STATIC(void *, d9a1b8); // GetCurrentProcess
TS2_OWNED_STATIC(void *, d9a1bc); // GetCurrentProcessId
TS2_OWNED_STATIC(void *, d9a1e4); // VirtualAlloc
TS2_OWNED_STATIC(void *, d9a1e8); // VirtualFree
TS2_OWNED_STATIC(void *, d9a1ec); // GetFullPathNameA
TS2_OWNED_STATIC(void *, d9a244); // CreateEventA
TS2_OWNED_STATIC(void *, d9a24c); // QueryPerformanceCounter
TS2_OWNED_STATIC(void *, d9a250); // QueryPerformanceFrequency
TS2_OWNED_STATIC(void *, d9a254); // SetPriorityClass
TS2_OWNED_STATIC(void *, d9a258); // GetPriorityClass
TS2_OWNED_STATIC(void *, d9a3d8); // ResetEvent
TS2_OWNED_STATIC(void *, d9a3dc); // PulseEvent
// USER32
TS2_OWNED_STATIC(void *, d9a49c); // ScreenToClient
TS2_OWNED_STATIC(void *, d9a4a0); // IsIconic
TS2_OWNED_STATIC(void *, d9a4a4); // GetSystemMetrics
TS2_OWNED_STATIC(void *, d9a4a8); // GetWindowRect
TS2_OWNED_STATIC(void *, d9a4d0); // GetCursorPos
TS2_OWNED_STATIC(void *, d9a4e8); // IntersectRect
TS2_OWNED_STATIC(void *, d9a4ec); // OffsetRect
TS2_OWNED_STATIC(void *, d9a50c); // GetWindowPlacement
TS2_OWNED_STATIC(void *, d9a510); // SetWindowPos
TS2_OWNED_STATIC(void *, d9a514); // ReleaseDC
TS2_OWNED_STATIC(void *, d9a518); // GetDC
TS2_OWNED_STATIC(void *, d9a51c); // SystemParametersInfoA
TS2_OWNED_STATIC(void *, d9a520); // GetKeyboardLayout
TS2_OWNED_STATIC(void *, d9a524); // DefWindowProcA
TS2_OWNED_STATIC(void *, d9a528); // UnregisterClassA
TS2_OWNED_STATIC(void *, d9a52c); // RegisterClassExA
TS2_OWNED_STATIC(void *, d9a530); // CreateWindowExA
TS2_OWNED_STATIC(void *, d9a534); // DestroyWindow
TS2_OWNED_STATIC(void *, d9a538); // ShowWindow
TS2_OWNED_STATIC(void *, d9a53c); // MoveWindow
TS2_OWNED_STATIC(void *, d9a540); // IsZoomed
TS2_OWNED_STATIC(void *, d9a544); // GetActiveWindow
TS2_OWNED_STATIC(void *, d9a548); // GetAsyncKeyState
TS2_OWNED_STATIC(void *, d9a54c); // SetCapture
TS2_OWNED_STATIC(void *, d9a550); // ReleaseCapture
TS2_OWNED_STATIC(void *, d9a554); // SetTimer
TS2_OWNED_STATIC(void *, d9a558); // KillTimer
TS2_OWNED_STATIC(void *, d9a55c); // UpdateWindow
TS2_OWNED_STATIC(void *, d9a560); // SetActiveWindow
TS2_OWNED_STATIC(void *, d9a568); // MessageBoxA
TS2_OWNED_STATIC(void *, d9a570); // PostMessageA
TS2_OWNED_STATIC(void *, d9a588); // BeginPaint
TS2_OWNED_STATIC(void *, d9a58c); // EndPaint
TS2_OWNED_STATIC(void *, d9a590); // InvalidateRect
TS2_OWNED_STATIC(void *, d9a594); // ValidateRect
TS2_OWNED_STATIC(void *, d9a598); // SetWindowTextA
TS2_OWNED_STATIC(void *, d9a59c); // GetClientRect
TS2_OWNED_STATIC(void *, d9a5a0); // AdjustWindowRectEx
TS2_OWNED_STATIC(void *, d9a5c0); // GetWindowThreadProcessId
TS2_OWNED_STATIC(void *, d9a5c4); // IsWindow
TS2_OWNED_STATIC(void *, d9a5c8); // GetAncestor
TS2_OWNED_STATIC(void *, d9a5cc); // LoadIconA
TS2_OWNED_STATIC(void *, d9a5d0); // GetDesktopWindow
TS2_OWNED_STATIC(void *, d9a5d4); // SetWindowLongA
TS2_OWNED_STATIC(void *, d9a5d8); // GetWindowLongA
TS2_OWNED_STATIC(void *, d9a5dc); // FillRect
TS2_OWNED_STATIC(void *, d9a5e0); // ClientToScreen
// d3d9, ole32
TS2_OWNED_STATIC(void *, d9a6c8); // Direct3DCreate9
TS2_OWNED_STATIC(void *, d9a6d8); // StringFromGUID2
