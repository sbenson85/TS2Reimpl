// nGZGraphic4::cCanvasW32's window procedure and its helpers (lane OQ, rendering campaign R2b step 4b):
// src/gz/cCanvasW32.h. Every Win32 call goes through the game's import table, as the original's do.
#include "gz/cCanvasW32.h"
#include "gz/cCanvasEvents.h"

#define NOMINMAX
#include <windows.h>

#include "app/AppServices2.h" // as2::ConvertStringFrom
#include "game/game.h"
#include "game/import_slots.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/MultiMon.h"
#include "ifaces/cIGZFrameWork.h"
#include "gz/cEventFaces.h" // the null and scalar events' vtables
#include "rz/RZAutoRef.h"
#include "rz/RZTextConvert.h"
#include "rz/RZTextTrail.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include <intrin.h>

#include <cstdlib>

// The thread-safe statics helpers of our CRT, used on the game's guard (as src/gz/LoadUIScript.cpp).
extern "C" void __cdecl _Init_thread_header(int *target) noexcept;
extern "C" __declspec(thread) int _Init_thread_epoch; // our CRT's statics epoch, not the image's TLS block (docs/standalone-shape.md, "Owned statics")
extern "C" void __cdecl _Init_thread_footer(int *target) noexcept;

namespace nGZGraphic4 {

namespace {

// The import address table slots the original's own calls read.
constexpr IatSlot kIatGetWindowLong = &ts2_rva_d9a5d8, kIatDefWindowProc = &ts2_rva_d9a524, kIatSleep = &ts2_rva_d9a144;
constexpr IatSlot kIatBeginPaint = &ts2_rva_d9a588, kIatEndPaint = &ts2_rva_d9a58c, kIatGetClientRect = &ts2_rva_d9a59c;
constexpr IatSlot kIatFillRect = &ts2_rva_d9a5dc, kIatShowWindow = &ts2_rva_d9a538, kIatAdjustWindowRectEx = &ts2_rva_d9a5a0;
constexpr IatSlot kIatSystemParametersInfo = &ts2_rva_d9a51c, kIatGetLocaleInfo = &ts2_rva_d9a150, kIatGetAsyncKeyState = &ts2_rva_d9a548;
constexpr IatSlot kIatSetCapture = &ts2_rva_d9a54c, kIatReleaseCapture = &ts2_rva_d9a550, kIatScreenToClient = &ts2_rva_d9a49c;
constexpr IatSlot kIatGetWindowRect = &ts2_rva_d9a4a8, kIatGetModuleHandle = &ts2_rva_d9a114, kIatGetProcAddress = &ts2_rva_d9a124;
constexpr IatSlot kIatLoadIcon = &ts2_rva_d9a5cc;

// TrackMouseLeave's function-local static: TrackMouseEvent looked up once (cell and guard in the game's storage, the
// thread's epoch: our CRT's, as for every function-local static of the image we build).

constexpr int kUserData = -0x15;
constexpr uint32_t kWheelDeltaPerNotch = 120;

template <class Fn, void **Iat> Fn Import() { return ImportSlot<Fn, Iat>(); }

HWND AsWindow(void *hwnd) { return static_cast<HWND>(hwnd); }

BOOL SystemParameters(UINT action, UINT param, void *data)
{
    return Import<BOOL(WINAPI *)(UINT, UINT, PVOID, UINT), kIatSystemParametersInfo>()(action, param, data, 0);
}

ifaces::cIGZFrameWork *Framework() { return As<ifaces::cIGZFrameWork>(GetServiceObject()); }

// A framework service held for a scope (released through 0x1466e0's guarded release, also on unwinding).
class HeldService {
public:
    HeldService(uint32_t id, uint32_t iid)
    {
        mRef.mObject = nullptr;
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &mRef.mObject);
    }
    ~HeldService() { mRef.ReleaseObject(); }
    HeldService(const HeldService &) = delete;
    HeldService &operator=(const HeldService &) = delete;
    void *Get() const { return mRef.mObject; }

private:
    RZAutoRef mRef;
};

// cScalarEvent<unsigned long> (the game's class; vtable cEventFaces.h): its vtable and the value.
struct ScalarEvent {
    const void *mVTable;
    uint32_t mValue;
};

} // namespace

// ==== the procedure ====

int32_t __stdcall cCanvasW32::WindowProc(void *hwnd, uint32_t message, uint32_t wParam, int32_t lParam)
{
    auto *const behavior = reinterpret_cast<cICanvasW32Behavior *>(
        Import<LONG(WINAPI *)(HWND, int), kIatGetWindowLong>()(AsWindow(hwnd), kUserData));
    if (!behavior)
        return Import<LRESULT(WINAPI *)(HWND, UINT, WPARAM, LPARAM), kIatDefWindowProc>()(AsWindow(hwnd), message, wParam,
                                                                                          lParam);
    cCanvasW32 *const self = static_cast<cCanvasW32 *>(behavior);
    // The hooks in order, up to the end as it was before the first (the original keeps it in a local).
    void **const end = self->mHooks.mLast;
    for (void **it = self->mHooks.mFirst; it != end; ++it) {
        // The original leaves the flag uninitialised (a hook that never writes it is read as the stack had it); ours
        // reads such a hook as "not handled".
        bool handled = false;
        const int32_t result =
            static_cast<cICanvasMessageHook *>(*it)->OnWindowMessage(hwnd, message, wParam, lParam, &handled);
        if (handled) return result;
    }
    return self->OnMessage(hwnd, message, wParam, lParam);
}

int32_t cCanvasW32::OnMessage(void *hwnd, uint32_t message, uint32_t wParam, int32_t lParam)
{
    const HWND window = AsWindow(hwnd);
    const int32_t x = int16_t(lParam), y = int16_t(uint32_t(lParam) >> 16);
    switch (message) {
    case WM_MOVE: {
        uint32_t code = 1;
        if (mFlag1A) {
            code = 0x11;
            mFlag1C = true;
        }
        UpdateRect();
        if (IsMinimized()) return 0;
        SendSizeMove(code);
        return 0;
    }
    case WM_SIZE: {
        // The rectangle is taken first; minimizing sends an empty one, other kinds (shown / hidden by another window
        // being maximized) nothing.
        cCanvasRect client;
        cCanvasSizeMoveEvent event(*GetClientRect(&client), 2);
        switch (wParam) {
        case SIZE_RESTORED:
            if (mFlag1A) {
                event.mCode = 0x12;
                mFlag1B = true;
            }
            break;
        case SIZE_MINIMIZED:
            event.mRect = cCanvasRect{0, 0, 0, 0};
            event.mCode = 0xa;
            break;
        case SIZE_MAXIMIZED: event.mCode = 6; break;
        default: return 0;
        }
        CheckScreenSaver();
        UpdateRect();
        NotifyListeners(kSizeMoveEvent, 0, &event);
        return 0;
    }
    case WM_ACTIVATE: {
        const uint16_t state = LOWORD(wParam);
        CheckScreenSaver();
        HeldService service(kServiceId, kIidBehavior);
        static_cast<cICanvasW32Behavior *>(service.Get())->SetTopmost(state != 0);
        SendCanvasEvent(state == 0 ? 2 : 1);
        if (state == WA_ACTIVE && HIWORD(wParam) != 0) SendCanvasEvent(9);
        return 0;
    }
    case WM_PAINT: {
        if (!IsActiveWindow() && (mPowerFlags & 4)) Import<void(WINAPI *)(DWORD), kIatSleep>()(15);
        PAINTSTRUCT paint;
        const HDC dc = Import<HDC(WINAPI *)(HWND, LPPAINTSTRUCT), kIatBeginPaint>()(window, &paint);
        if (Framework()->IsTickEnabled() && mPaintPending) {
            if (mPaintLock != 0) {
                RECT client;
                Import<BOOL(WINAPI *)(HWND, LPRECT), kIatGetClientRect>()(window, &client);
                Import<int(WINAPI *)(HDC, const RECT *, HBRUSH), kIatFillRect>()(dc, &client, HBRUSH(mBrush));
            } else {
                mPaintPending = false;
                if (!mFullscreen && !(mPowerFlags & 0x20)) {
                    if (void *const monitor = GetMonitor()) {
                        // The other monitors get the background (multimon.h's EnumDisplayMonitors stub).
                        void *data[2] = {monitor, mBrush};
                        nMultiMon::EnumDisplayMonitorsStub(dc, nullptr, reinterpret_cast<MONITORENUMPROC>(&FillOtherMonitors),
                                                           reinterpret_cast<LPARAM>(data));
                    }
                }
                SendCanvasEvent(3);
            }
        }
        Import<BOOL(WINAPI *)(HWND, const PAINTSTRUCT *), kIatEndPaint>()(window, &paint);
        return 0;
    }
    case WM_CLOSE: {
        const void *event = nGZEvent::NullEventVTable();
        if (!AskListeners(kCloseQuery, 0, &event)) {
            Import<BOOL(WINAPI *)(HWND, int), kIatShowWindow>()(window, SW_HIDE);
            Framework()->Quit(0);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        if (!Framework()->IsTickEnabled() && !mFullscreen) {
            RECT client;
            Import<BOOL(WINAPI *)(HWND, LPRECT), kIatGetClientRect>()(window, &client);
            Import<int(WINAPI *)(HDC, const RECT *, HBRUSH), kIatFillRect>()(HDC(wParam), &client, HBRUSH(mBrush));
        }
        return 1;
    case WM_SETTINGCHANGE:
        // A setting Init saved changed (the wParam is the setter's action): read again (Shutdown puts it back).
        if (wParam == 0x5d) mHasSaved5E = SystemParameters(0x5e, 0, &mSaved5E) != 0;
        else if (wParam == 0x101b) mHasSaved1A = SystemParameters(0x101a, 0, &mSaved1A) != 0;
        else if (wParam == 0x101d) mHasSaved1C = SystemParameters(0x101c, 0, &mSaved1C) != 0;
        return 0;
    case WM_ACTIVATEAPP: {
        CheckScreenSaver();
        HeldService service(kServiceId, kIidBehavior);
        static_cast<cICanvasW32Behavior *>(service.Get())->SetTopmost(wParam != 0);
        SendCanvasEvent(wParam != 0 ? 5 : 6);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        // The smallest window: the minimum client size with the frame the styles add (the client rectangle's own
        // size cancels out: the frame is measured around it).
        RECT frame = {mRect.mLeft, mRect.mTop, mRect.mRight, mRect.mBottom};
        Import<BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD), kIatAdjustWindowRectEx>()(&frame, mWindowStyle, FALSE,
                                                                                     mWindowExStyle);
        auto *const info = reinterpret_cast<MINMAXINFO *>(lParam);
        info->ptMinTrackSize.x = LONG(uint32_t(mMinSize.mWidth) - uint32_t(mRect.mRight) - uint32_t(frame.left) +
                                      uint32_t(mRect.mLeft) + uint32_t(frame.right));
        info->ptMinTrackSize.y = LONG(uint32_t(mRect.mTop) - uint32_t(mRect.mBottom) + uint32_t(mMinSize.mHeight) -
                                      uint32_t(frame.top) + uint32_t(frame.bottom));
        return 0;
    }
    case WM_INPUTLANGCHANGE: {
        // The new layout's code page (0: UTF-16), then the default procedure.
        char text[0x80];
        Import<int(WINAPI *)(LCID, LCTYPE, LPSTR, int), kIatGetLocaleInfo>()(LCID(LOWORD(lParam)), 0x1004, text, 0x80);
        mCodePage = uint32_t(atoi(text));
        if (mCodePage == 0) mCodePage = 0x10;
        break;
    }
    case WM_DISPLAYCHANGE: SendCanvasEvent(8); return 0;
    case WM_KEYDOWN:
        // ORIGINAL-BUG: the key flags are lParam >> 16, so KeyModifiers' 0x40000000 test (the previous key state, bit 30
        // of lParam) never sees it.
        if (!mFlag19) SendKeyEvent(5, int32_t(wParam), x, KeyModifiers(uint32_t(lParam) >> 16));
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP: OnKey(6, wParam, x, uint32_t(lParam) >> 16); return 0;
    case WM_SYSKEYDOWN: OnKey(5, wParam, x, uint32_t(lParam) >> 16); return 0;
    case WM_CHAR: OnChar(hwnd, wParam, x); return 0;
    case WM_TIMER:
        // The screen saver kept from starting within a minute, unless the power flags let the display sleep.
        if (!(mPowerFlags & 2)) {
            int32_t timeout = 0;
            SystemParameters(SPI_GETSCREENSAVETIMEOUT, 0, &timeout);
            if (timeout < 60) timeout = 60;
            SystemParameters(SPI_SETSCREENSAVETIMEOUT, UINT(timeout), nullptr);
        }
        CheckScreenSaver();
        SendCanvasEvent(7);
        return 0;
    case WM_MOUSEMOVE:
        if (!mFlag19) {
            TrackMouseLeave(hwnd);
            const uint32_t modifiers = MouseModifiers(wParam);
            SendMouseEvent(0xd, x, y, modifiers, false, 0, 0.0f);
        }
        return 0;
    case WM_LBUTTONDOWN: OnButtonDown(7, hwnd, 0, x, y, wParam); return 0;
    case WM_LBUTTONDBLCLK: OnButtonDown(7, hwnd, 1, x, y, wParam); return 0;
    case WM_RBUTTONDOWN: OnButtonDown(8, hwnd, 0, x, y, wParam); return 0;
    case WM_RBUTTONDBLCLK: OnButtonDown(8, hwnd, 1, x, y, wParam); return 0;
    case WM_MBUTTONDOWN: OnButtonDown(9, hwnd, 0, x, y, wParam); return 0;
    case WM_MBUTTONDBLCLK: OnButtonDown(9, hwnd, 1, x, y, wParam); return 0;
    case WM_LBUTTONUP: OnButtonUp(0xa, wParam, lParam); return 0;
    case WM_RBUTTONUP: OnButtonUp(0xb, wParam, lParam); return 0;
    case WM_MBUTTONUP: OnButtonUp(0xc, wParam, lParam); return 0;
    case WM_MOUSEWHEEL:
        if (!mFlag19) {
            TrackMouseLeave(hwnd);
            POINT point = {x, y};
            Import<BOOL(WINAPI *)(HWND, LPPOINT), kIatScreenToClient>()(window, &point);
            uint32_t lines = 1;
            SystemParameters(SPI_GETWHEELSCROLLLINES, 0, &lines);
            const int32_t delta = int16_t(HIWORD(wParam));
            const float amount = float(delta) * float(lines) / float(kWheelDeltaPerNotch);
            const uint32_t modifiers = MouseModifiers(LOWORD(wParam));
            SendMouseEvent(0xe, point.x, point.y, modifiers, false, delta, amount);
        }
        return 0;
    case WM_SIZING:
        // Power flag 1 keeps the window's shape: the edge across from the one dragged follows it.
        if (mPowerFlags & 1) {
            RECT before;
            Import<BOOL(WINAPI *)(HWND, LPRECT), kIatGetWindowRect>()(window, &before);
            auto *const rect = reinterpret_cast<RECT *>(lParam);
            const uint32_t left = uint32_t(rect->left) - uint32_t(before.left);
            const uint32_t top = uint32_t(rect->top) - uint32_t(before.top);
            const uint32_t right = uint32_t(rect->right) - uint32_t(before.right);
            const uint32_t bottom = uint32_t(rect->bottom) - uint32_t(before.bottom);
            switch (wParam) {
            case WMSZ_LEFT: rect->top = LONG(uint32_t(before.top) + left); break;
            case WMSZ_RIGHT: rect->bottom = LONG(uint32_t(before.bottom) + right); break;
            case WMSZ_TOP:
            case WMSZ_TOPLEFT: rect->left = LONG(uint32_t(before.left) + top); break;
            case WMSZ_TOPRIGHT: rect->right = LONG(uint32_t(before.right) - top); break;
            case WMSZ_BOTTOM:
            case WMSZ_BOTTOMRIGHT: rect->right = LONG(uint32_t(before.right) + bottom); break;
            case WMSZ_BOTTOMLEFT: rect->left = LONG(uint32_t(before.left) - bottom); break;
            default: break;
            }
        }
        return 1;
    case WM_CAPTURECHANGED: ExitSizeMove(hwnd); return 0;
    case WM_ENTERSIZEMOVE:
        // The original inlines CheckScreenSaver here (the ticks stop: the drag flag is set).
        mFlag1A = true;
        CheckScreenSaver();
        return 0;
    case WM_EXITSIZEMOVE: ExitSizeMove(hwnd); return 0;
    case WM_MOUSELEAVE:
        if (!mFlag19) {
            // The settings TrackMouseLeave cleared put back.
            mFlag1D = false;
            if (mHasSaved1A) SystemParameters(0x101b, 0, reinterpret_cast<void *>(uintptr_t(mSaved1A)));
            if (mHasSaved5E) SystemParameters(0x5d, mSaved5E, nullptr);
            if (mHasSaved1C) SystemParameters(0x101d, 0, reinterpret_cast<void *>(uintptr_t(mSaved1C)));
            SendMouseEvent(0x14, 0, 0, 0, false, 0, 0.0f);
        }
        return 0;
    case 0x702: {
        ScalarEvent event{nGZEvent::ScalarEventVTable(), wParam};
        NotifyListeners(kUserMessageEvent, 0, &event);
        return 0;
    }
    default: break;
    }
    return Import<LRESULT(WINAPI *)(HWND, UINT, WPARAM, LPARAM), kIatDefWindowProc>()(window, message, wParam, lParam);
}

// ==== the helpers ====

void cCanvasW32::UpdateRect()
{
    if (mFullscreen || IsMaximized() || IsMinimized()) return;
    cCanvasRect client;
    mRect = *GetClientRect(&client);
}

void cCanvasW32::SendSizeMove(uint32_t code)
{
    cCanvasRect client;
    cCanvasSizeMoveEvent event(*GetClientRect(&client), code);
    NotifyListeners(kSizeMoveEvent, 0, &event);
}

void cCanvasW32::SendCanvasEvent(uint32_t code)
{
    cCanvasEvent event(code);
    NotifyListeners(kCanvasEvent, 0, &event);
}

void cCanvasW32::SendMouseEvent(uint32_t kind, int32_t x, int32_t y, uint32_t modifiers, bool doubleClick,
                                int32_t wheelDelta, float wheelLines)
{
    cMouseEvent event(kind, x, y, modifiers, doubleClick, wheelDelta, wheelLines);
    NotifyListeners(kMouseEvent, 0, &event);
}

void cCanvasW32::SendKeyEvent(uint32_t kind, int32_t key, int32_t repeat, uint32_t modifiers)
{
    cKeyEvent event(kind, key, repeat, modifiers);
    NotifyListeners(kKeyEvent, 0, &event);
}

uint32_t cCanvasW32::MouseModifiers(uint32_t buttons)
{
    uint32_t modifiers = (buttons & MK_SHIFT) ? 1 : 0;
    if (buttons & MK_CONTROL) modifiers |= 2;
    if (Import<SHORT(WINAPI *)(int), kIatGetAsyncKeyState>()(VK_MENU) < 0) modifiers |= 4;
    if (buttons & MK_LBUTTON) modifiers |= 0x10;
    if (buttons & MK_RBUTTON) modifiers |= 0x20;
    if (buttons & MK_MBUTTON) modifiers |= 0x40;
    return modifiers;
}

uint32_t __cdecl cCanvasW32::KeyModifiers(uint32_t keyFlags)
{
    const auto held = Import<SHORT(WINAPI *)(int), kIatGetAsyncKeyState>();
    uint32_t modifiers = held(VK_SHIFT) < 0 ? 1 : 0;
    if (held(VK_CONTROL) < 0) modifiers |= 2;
    if (held(VK_MENU) < 0) modifiers |= 4;
    if (held(VK_LBUTTON) < 0) modifiers |= 0x10;
    if (held(VK_RBUTTON) < 0) modifiers |= 0x20;
    if (held(VK_MBUTTON) < 0) modifiers |= 0x40;
    if (keyFlags & 0x40000000) modifiers |= 0x40000000;
    return modifiers;
}

// A character: as it is when the layout's code page is the string encoding (UTF-8); otherwise its bytes collected
// until the lead byte's length is in, then converted and sent one byte at a time.
void cCanvasW32::OnChar(void *, uint32_t character, int32_t repeat)
{
    if (mFlag19) return;
    if (RZText::DefaultEncoding() == int32_t(mCodePage)) {
        SendKeyEvent(4, int8_t(character), repeat, 0);
        return;
    }
    mStringA.mString.push_back(char(character));
    if (mPendingCharBytes == 0) {
        // The bytes after this lead byte in the code page (0x1b06a6).
        mPendingCharBytes = int32_t(RZText::TrailCount(character, int32_t(mCodePage))) + 1;
    }
    if (mStringA.Strlen() < mPendingCharBytes) return;
    as2::ConvertStringFrom(mStringA.AsInterface(), int32_t(mCodePage), mStringB.AsInterface());
    const int32_t count = int32_t(mStringB.Strlen());
    for (int32_t i = 0; i < count; ++i) SendKeyEvent(4, int8_t(mStringB.mString[size_t(i)]), repeat, 0);
    mStringA.mString.clear();
    mPendingCharBytes = 0;
    mStringB.mString.clear();
}

// The end of a size / move drag: the code says whether it moved (bit 0) or sized (bit 1), the flags cleared, the
// rectangle kept, the ticks back on, the listeners told.
void cCanvasW32::ExitSizeMove(void *)
{
    if (!mFlag1A) return;
    uint32_t code = (mFlag1C ? 1 : 0) + 0x20;
    mFlag1C = false;
    if (mFlag1B) code |= 2;
    mFlag1A = false;
    mFlag1B = false;
    UpdateRect();
    CheckScreenSaver();
    SendSizeMove(code);
}

void cCanvasW32::OnKey(uint32_t kind, uint32_t key, int32_t repeat, uint32_t keyFlags)
{
    if (mFlag19) return;
    SendKeyEvent(kind, int32_t(key), repeat, KeyModifiers(keyFlags));
}

void cCanvasW32::OnButtonDown(uint32_t kind, void *hwnd, uint32_t doubleClick, int32_t x, int32_t y, uint32_t buttons)
{
    if (mFlag19) return;
    TrackMouseLeave(hwnd);
    Import<HWND(WINAPI *)(HWND), kIatSetCapture>()(AsWindow(hwnd));
    const uint32_t modifiers = MouseModifiers(buttons);
    SendMouseEvent(kind, x, y, modifiers, doubleClick == 1, 0, 0.0f);
}

void cCanvasW32::OnButtonUp(uint32_t kind, uint32_t buttons, int32_t lParam)
{
    if (mFlag19) return;
    Import<BOOL(WINAPI *)(), kIatReleaseCapture>()();
    const uint32_t modifiers = MouseModifiers(buttons);
    SendMouseEvent(kind, int16_t(lParam), int16_t(uint32_t(lParam) >> 16), modifiers, false, 0, 0.0f);
}

// The window asks to hear when the mouse leaves it (once until it has), and the settings Init saved are switched off
// meanwhile (WM_MOUSELEAVE puts them back). Without TrackMouseEvent nothing happens.
void cCanvasW32::TrackMouseLeave(void *hwnd)
{
    if (mFlag1D) return;
    using TrackFn = BOOL(WINAPI *)(LPTRACKMOUSEEVENT);
    int *const guard = &ts2_rva_10c96f8;
    TrackFn *const cell = reinterpret_cast<TrackFn *>(&ts2_rva_10c96f4);
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            const HMODULE user32 = Import<HMODULE(WINAPI *)(LPCSTR), kIatGetModuleHandle>()("user32.dll");
            *cell = reinterpret_cast<TrackFn>(
                Import<FARPROC(WINAPI *)(HMODULE, LPCSTR), kIatGetProcAddress>()(user32, "TrackMouseEvent"));
            _Init_thread_footer(guard);
        }
    }
    const TrackFn track = *cell;
    if (!track) return;
    TRACKMOUSEEVENT request = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, AsWindow(hwnd), 0};
    mFlag1D = true;
    track(&request);
    if (mHasSaved1A) SystemParameters(0x101b, 0, nullptr);
    if (mHasSaved5E) SystemParameters(0x5d, 0, nullptr);
    if (mHasSaved1C) SystemParameters(0x101d, 0, nullptr);
}

int32_t __stdcall cCanvasW32::FillOtherMonitors(void *monitor, void *dc, void *rect, int32_t data)
{
    void *const *const pair = reinterpret_cast<void *const *>(data);
    if (monitor != pair[0])
        Import<int(WINAPI *)(HDC, const RECT *, HBRUSH), kIatFillRect>()(HDC(dc), static_cast<const RECT *>(rect),
                                                                         HBRUSH(pair[1]));
    return 1;
}

int32_t __stdcall cCanvasW32::LoadClassIcon(void *module, const char *, const char *name, int32_t wndClass)
{
    reinterpret_cast<WNDCLASSEXA *>(wndClass)->hIcon =
        Import<HICON(WINAPI *)(HINSTANCE, LPCSTR), kIatLoadIcon>()(HINSTANCE(module), name);
    return 0;
}

} // namespace nGZGraphic4
