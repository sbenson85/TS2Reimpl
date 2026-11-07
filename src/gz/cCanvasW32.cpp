// nGZGraphic4::cCanvasW32 (lane ON): src/gz/cCanvasW32.h.
#include "gz/cCanvasW32.h"
#include "gz/cBehaviorRegistry.h"
#include <iterator>
#include <optional>
#include "gz/cCanvasEvents.h"

#define NOMINMAX
#include <windows.h>

#include "game/game.h"
#include "game/import_slots.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/MultiMon.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include <cstring>
#include <new>
#include <string>

namespace nGZGraphic4 {

namespace {

// The import address table slots the original's own calls read.
constexpr IatSlot kIatGetWindowRect = &ts2_rva_d9a4a8, kIatSetWindowPos = &ts2_rva_d9a510, kIatSetLastError = &ts2_rva_d9a140;
constexpr IatSlot kIatGetWindowLong = &ts2_rva_d9a5d8, kIatSetWindowLong = &ts2_rva_d9a5d4, kIatShowWindow = &ts2_rva_d9a538;
constexpr IatSlot kIatGetDesktopWindow = &ts2_rva_d9a5d0, kIatIsIconic = &ts2_rva_d9a4a0, kIatIsZoomed = &ts2_rva_d9a540;
constexpr IatSlot kIatGetAncestor = &ts2_rva_d9a5c8, kIatValidateRect = &ts2_rva_d9a594, kIatInvalidateRect = &ts2_rva_d9a590;
constexpr IatSlot kIatUpdateWindow = &ts2_rva_d9a55c, kIatSetWindowText = &ts2_rva_d9a598, kIatGetClientRect = &ts2_rva_d9a59c;
constexpr IatSlot kIatClientToScreen = &ts2_rva_d9a5e0, kIatGetActiveWindow = &ts2_rva_d9a544, kIatMoveWindow = &ts2_rva_d9a53c;
constexpr IatSlot kIatSetActiveWindow = &ts2_rva_d9a560, kIatFillRect = &ts2_rva_d9a5dc, kIatGetDC = &ts2_rva_d9a518;
constexpr IatSlot kIatReleaseDC = &ts2_rva_d9a514, kIatSetThreadExecutionState = &ts2_rva_d9a14c;
constexpr IatSlot kIatAdjustWindowRectEx = &ts2_rva_d9a5a0, kIatSystemParametersInfo = &ts2_rva_d9a51c;
constexpr IatSlot kIatGetKeyboardLayout = &ts2_rva_d9a520, kIatGetLocaleInfo = &ts2_rva_d9a150, kIatCreateSolidBrush = &ts2_rva_d9a0ac;
constexpr IatSlot kIatGetModuleHandle = &ts2_rva_d9a114, kIatEnumResourceNames = &ts2_rva_d9a148;
constexpr IatSlot kIatRegisterClassEx = &ts2_rva_d9a52c, kIatCreateWindowEx = &ts2_rva_d9a530, kIatSetTimer = &ts2_rva_d9a554;
constexpr IatSlot kIatKillTimer = &ts2_rva_d9a558, kIatDestroyWindow = &ts2_rva_d9a534, kIatUnregisterClass = &ts2_rva_d9a528;
constexpr IatSlot kIatDeleteObject = &ts2_rva_d9a0a8;

// Game code and data Init and Show use.
constexpr const char *kClassName = "{0xdb637c87}"; // the window class (the image's 0xd9cd04)
constexpr RECT kProbeRect = {50, 50, 100, 100}; // the rectangle the title bar is measured with (0xd9cd40)
constexpr uint32_t kFrameworkServiceId = 0x23ea70a1;

constexpr int kStyle = -0x10, kExStyle = -0x14, kUserData = -0x15;
const HWND kTopmost = HWND(-1), kNoTopmost = HWND(-2);

template <class Fn, void **Iat> Fn Import() { return ImportSlot<Fn, Iat>(); }

HWND AsWindow(void *hwnd) { return static_cast<HWND>(hwnd); }

// The framework's service 0x23ea70a1 (the one that wants the window): fetched through QueryInterface into a local that
// is released after the call, as the original does (no check that it answered).
void CallWindowService(void *hwnd)
{
    cIGZUnknown *service = nullptr;
    As<ifaces::cIGZFrameWork>(GetServiceObject())->QueryInterface(kFrameworkServiceId, reinterpret_cast<void **>(&service));
    RawSlot<6, void>(service, hwnd);
    if (service) service->Release();
}

} // namespace

namespace {
std::optional<nGZEvent::cEventAttributeMap> gRegistry;
const nGZEvent::ClassEvent kClassEvents[] = {
    {0x1b637474, "CanvasEvent", 0x7b637514, 2},          {0x3b6decd1, "CanvasSizeMoveEvent", 0xfb6dec65, 2},
    {0xed016522, "CanvasCloseEvent", 0x7b256782, 1},     {0x1b637482, "MouseEvent", 0xfb637610, 2},
    {0x5b63748b, "KeyEvent", 0x1b6377b0, 2},             {0x278fdc1f, "PostedMsgEvent", 0x7b22dd0f, 2},
};
} // namespace

nGZEvent::cEventAttributeMap *cCanvasW32Traits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

// 0x156a70.
nGZEvent::cEventAttributeMap *__cdecl cCanvasW32Traits::RegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute)
{
    return nGZEvent::ClassRegistryFor(gRegistry, kClassEvents, std::size(kClassEvents), id, attribute);
}

void cCanvasW32Traits::ResetRegistryForTest() { gRegistry.reset(); }

// 0x155c70 (the creator) stores the three vtables and these values; the allocator's other bytes stay (+0x1f, +0xa2,
// +0xc7, +0xcd..+0xcf).
cCanvasW32::cCanvasW32()
    : mStyleFlags(0xb), mPowerFlags(10), mFullscreen(false), mFlag19(false), mFlag1A(false), mFlag1B(false),
      mFlag1C(false), mFlag1D(false), mPaintPending(false), mPendingCharBytes(0), mPaintLock(0), mMinSize{0, 0},
      mRect{0, 0, 0, 0}, mAtom(0), mHwnd(nullptr), mBrush(nullptr), mWindowStyle(0xc80000), mWindowExStyle(0),
      mCodePage(0), mSaved1A(0), mSaved5E(0), mSaved1C(0), mHasSaved1A(false), mHasSaved5E(false),
      mHasSaved1C(false), mServiceId(kServiceId), mRunning(false), mHooks{nullptr, nullptr, nullptr}
{
    mStringA.Construct();
    mStringB.Construct();
    mTitle.Construct();
}

// 0x154d40: the hooks (counted pointers) released and freed, then the three strings reset, last first.
cCanvasW32::~cCanvasW32()
{
    mHooks.Destroy();
    mTitle.Destruct();
    mStringB.Destruct();
    mStringA.Destruct();
}

cICanvasW32Behavior *__cdecl cCanvasW32::Create() { return static_cast<cICanvasW32Behavior *>(new cCanvasW32); }

// Null out: false. The five behaviour ids answer this face, counted; the service and message target faces counted; the
// behaviour type uncounted.
bool cCanvasW32::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    void *face = nullptr;
    switch (riid) {
    case kIidCanvas:
    case kIidBehavior:
    case 1:
    case kIidBehavior2: face = static_cast<cICanvasW32Behavior *>(this); break;
    case cIGZSystemService::kIID: face = static_cast<cIGZSystemService *>(this); break;
    case cIGZMessageTarget::kIid: face = static_cast<cIGZMessageTarget *>(this); break;
    case kIidBehaviorType: *out = static_cast<cICanvasW32Behavior *>(this); return true;
    default: return false;
    }
    *out = face;
    AddRef();
    return true;
}

uint32_t cCanvasW32::AddRef() { return Behavior::AddRef(); }
uint32_t cCanvasW32::Release() { return Behavior::Release(); }

// Only the connection manager's change keeps the active bit.
void cCanvasW32::OnEvent(uint32_t type, uint32_t, void *, uint32_t)
{
    if (type == kConnectionChangedEvent) SyncActive();
}

// ==== the window ====

// 0x155ab0: the window moved to mRect (with the frame the styles add), unless fullscreen, maximized or minimized.
void cCanvasW32::ApplyRect()
{
    if (mFullscreen || IsMaximized() || IsMinimized()) return;
    RECT rect = {mRect.mLeft, mRect.mTop, mRect.mRight, mRect.mBottom};
    Import<BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD), kIatAdjustWindowRectEx>()(&rect, mWindowStyle, FALSE, mWindowExStyle);
    Import<BOOL(WINAPI *)(HWND, int, int, int, int, BOOL), kIatMoveWindow>()(
        AsWindow(mHwnd), rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);
}

// 0x157780: the framework's tick flag follows whether the screen saver runs, the window is minimized or the flags
// say to keep running.
void cCanvasW32::CheckScreenSaver()
{
    bool run = !IsMinimized() || ((mPowerFlags >> 4) & 1);
    DWORD saver = 0;
    Import<BOOL(WINAPI *)(UINT, UINT, PVOID, UINT), kIatSystemParametersInfo>()(SPI_GETSCREENSAVERRUNNING, 0, &saver, 0);
    run = saver == 0 && run && !mFlag1A && !mFlag19;
    As<ifaces::cIGZFrameWork>(GetServiceObject())->ToggleTick(run);
}

// 0x157220: shows (or restyles) the window as the flags say; the event goes out unless nothing changed.
void cCanvasW32::Show(int32_t flags, bool restore)
{
    if (flags != -1) mStyleFlags = uint32_t(flags);
    const uint32_t f = mStyleFlags;
    uint32_t style = 0x80000;
    mWindowStyle = style;
    mWindowExStyle = 0;
    if (f & 2) style = 0xa0000;
    if (f & 4) style |= 0x10000;
    if (f & 1) {
        style |= 0x40000;
        mWindowExStyle = 0x300;
    }
    if (f & 8) style |= 0xc00000;
    mWindowStyle = style;

    const HWND hwnd = AsWindow(mHwnd);
    RECT rectA;
    const auto getWindowRect = Import<BOOL(WINAPI *)(HWND, LPRECT), kIatGetWindowRect>();
    getWindowRect(hwnd, &rectA);
    const bool captionless = !(f & 8);
    if (mFullscreen) return;

    Import<void(WINAPI *)(DWORD), kIatSetLastError>()(0);
    const uint32_t current = Import<LONG(WINAPI *)(HWND, int), kIatGetWindowLong>()(hwnd, kStyle);
    const uint32_t currentEx = Import<LONG(WINAPI *)(HWND, int), kIatGetWindowLong>()(hwnd, kExStyle);
    if ((current & 0x80cf0000) == style && (currentEx & 0x300) == mWindowExStyle) return;

    const uint32_t newStyle = (current & 0x7f30ffff) | style;
    const uint32_t newEx = (currentEx & 0xfffffcff) | mWindowExStyle;
    Import<void(WINAPI *)(DWORD), kIatSetLastError>()(0);
    const auto setLong = Import<LONG(WINAPI *)(HWND, int, LONG), kIatSetWindowLong>();
    setLong(hwnd, kStyle, LONG(newStyle));
    setLong(hwnd, kExStyle, LONG(newEx));
    getWindowRect(hwnd, &rectA);
    Import<BOOL(WINAPI *)(HWND, int), kIatShowWindow>()(hwnd, captionless ? 3 : 1);
    const auto setPos = Import<BOOL(WINAPI *)(HWND, HWND, int, int, int, int, UINT), kIatSetWindowPos>();
    bool *const savedFlag = &ts2_rva_10c96dc;
    RECT *const savedRect = reinterpret_cast<RECT *>(ts2_rva_10c96e0);
    if (captionless) {
        *savedFlag = true;
        *savedRect = rectA;
        setPos(hwnd, kTopmost, 0, 0, 0, 0, 0x2b);
    } else {
        RECT rectB;
        getWindowRect(hwnd, &rectB);
        if (!restore) {
            setPos(hwnd, kNoTopmost, 0, 0, 0, 0, 0x2b);
        } else if (*savedFlag) {
            rectB = *savedRect;
            setPos(hwnd, kNoTopmost, savedRect->left, savedRect->top, savedRect->right - savedRect->left,
                   savedRect->bottom - savedRect->top, 0x28);
        } else {
            const int width = rectB.right - rectB.left;
            const int height = rectB.bottom - rectB.top;
            setPos(hwnd, kNoTopmost, (rectA.right - rectA.left - width) / 2 + rectA.left,
                   (rectA.bottom - rectA.top - height) / 2 + rectA.top, width, height, 0x28);
        }
        *savedFlag = false;
    }
    cCanvasEvent event(4);
    NotifyListeners(kCanvasEvent, 0, &event);
}

int32_t cCanvasW32::SetTopmost(bool topmost)
{
    const HWND after = IsCaptionless() && topmost ? kTopmost : kNoTopmost;
    return Import<BOOL(WINAPI *)(HWND, HWND, int, int, int, int, UINT), kIatSetWindowPos>()(AsWindow(mHwnd), after, 0, 0,
                                                                                           0, 0, 0x503);
}

// The title kept as given, then converted (to the window's encoding) for the window.
void cCanvasW32::SetTitle(const char *title)
{
    mTitle.mString.assign(title);
    cRZString converted;
    converted.Construct();
    RZText::ConvertStringToString(*mTitle.AsInterface(), *converted.AsInterface(), -1);
    Import<BOOL(WINAPI *)(HWND, LPCSTR), kIatSetWindowText>()(AsWindow(mHwnd), converted.mString.c_str());
    converted.Destruct();
}

void cCanvasW32::SetSize(int32_t width, int32_t height)
{
    mRect.mRight = mRect.mLeft + width;
    mRect.mBottom = mRect.mTop + height;
    ApplyRect();
}

// Stores the minimum, then grows the window to it if the client area is smaller.
void cCanvasW32::SetMinSize(int32_t width, int32_t height)
{
    mMinSize.mWidth = width;
    mMinSize.mHeight = height;
    cCanvasRect client;
    GetClientRect(&client);
    int32_t w = client.mRight - client.mLeft;
    if (w < width || client.mBottom - client.mTop < height) {
        int32_t h = client.mBottom - client.mTop;
        if (h < height) h = height;
        if (w < width) w = width;
        SetSize(w, h);
    }
}

// Centres the window (as wide and tall as it is, at most the monitor's) in the monitor, below the title bar offset the
// first call measures.
void cCanvasW32::PlaceOnMonitor(cICanvasMonitor *monitor)
{
    int32_t *const offset = &ts2_rva_10c96f0;
    if (*offset == 0) {
        RECT probe = kProbeRect;
        Import<BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD), kIatAdjustWindowRectEx>()(&probe, 0x80c00000, FALSE, 0);
        *offset = 0x32 - probe.top;
    }
    const int32_t windowHeight = mRect.mBottom - mRect.mTop;
    const int32_t windowWidth = mRect.mRight - mRect.mLeft;
    cCanvasRect area;
    monitor->GetRect(&area);
    const int32_t top = *offset + area.mTop;
    const int32_t availableWidth = area.mRight - area.mLeft;
    const int32_t width = windowWidth <= availableWidth ? windowWidth : availableWidth;
    const int32_t availableHeight = area.mBottom - top;
    const int32_t height = windowHeight <= availableHeight ? windowHeight : availableHeight;
    mRect.mLeft = (availableWidth - width) / 2 + area.mLeft;
    mRect.mTop = (availableHeight - height) / 2 + top;
    mRect.mRight = width + mRect.mLeft;
    mRect.mBottom = height + mRect.mTop;
    ApplyRect();
}

uint32_t cCanvasW32::SetPowerFlags(uint32_t flags)
{
    mPowerFlags = flags;
    return Import<DWORD(WINAPI *)(DWORD), kIatSetThreadExecutionState>()((flags & 2) ? 0x80000000 : 0x80000003);
}

void *cCanvasW32::SetActive(bool active)
{
    return Import<HWND(WINAPI *)(HWND), kIatSetActiveWindow>()(active ? AsWindow(mHwnd) : nullptr);
}

void cCanvasW32::LockPaint()
{
    if (mPaintLock == 0 && mPaintPending)
        Import<BOOL(WINAPI *)(HWND, const RECT *), kIatValidateRect>()(AsWindow(mHwnd), nullptr);
    ++mPaintLock;
}

void cCanvasW32::UnlockPaint()
{
    if (mPaintLock == 0) return;
    if (--mPaintLock != 0) return;
    mPaintPending = true;
    Import<BOOL(WINAPI *)(HWND, const RECT *, BOOL), kIatInvalidateRect>()(AsWindow(mHwnd), nullptr, FALSE);
}

void cCanvasW32::Repaint()
{
    mPaintPending = true;
    if (mPaintLock != 0) return;
    Import<BOOL(WINAPI *)(HWND, const RECT *, BOOL), kIatInvalidateRect>()(AsWindow(mHwnd), nullptr, FALSE);
    Import<BOOL(WINAPI *)(HWND), kIatUpdateWindow>()(AsWindow(mHwnd));
}

void cCanvasW32::FillBackground()
{
    const HDC dc = Import<HDC(WINAPI *)(HWND), kIatGetDC>()(AsWindow(mHwnd));
    if (!dc) return;
    RECT client;
    Import<BOOL(WINAPI *)(HWND, LPRECT), kIatGetClientRect>()(AsWindow(mHwnd), &client);
    Import<int(WINAPI *)(HDC, const RECT *, HBRUSH), kIatFillRect>()(dc, &client, HBRUSH(mBrush));
    Import<int(WINAPI *)(HWND, HDC), kIatReleaseDC>()(AsWindow(mHwnd), dc);
}

uint32_t cCanvasW32::GetStyleFlags() { return mStyleFlags; }

// A copy of the title handed to the caller's string.
void cCanvasW32::GetTitle(cIGZString *out)
{
    cRZString copy;
    copy.Construct(mTitle, 0, std::string::npos);
    RawSlot<17, void>(out, copy.AsInterface());
    copy.Destruct();
}

// The client area in screen coordinates.
cCanvasRect *cCanvasW32::GetClientRect(cCanvasRect *out)
{
    RECT client;
    Import<BOOL(WINAPI *)(HWND, LPRECT), kIatGetClientRect>()(AsWindow(mHwnd), &client);
    POINT topLeft = {client.left, client.top};
    Import<BOOL(WINAPI *)(HWND, LPPOINT), kIatClientToScreen>()(AsWindow(mHwnd), &topLeft);
    POINT bottomRight = {client.right, client.bottom};
    Import<BOOL(WINAPI *)(HWND, LPPOINT), kIatClientToScreen>()(AsWindow(mHwnd), &bottomRight);
    out->mLeft = topLeft.x;
    out->mRight = bottomRight.x;
    out->mTop = topLeft.y;
    out->mBottom = bottomRight.y;
    return out;
}

cCanvasSize *cCanvasW32::GetMinSize(cCanvasSize *out)
{
    *out = mMinSize;
    return out;
}

uint32_t cCanvasW32::GetPowerFlags() { return mPowerFlags; }

bool cCanvasW32::IsActiveWindow()
{
    return Import<HWND(WINAPI *)(), kIatGetActiveWindow>()() == AsWindow(mHwnd);
}

bool cCanvasW32::IsMinimized()
{
    const HWND root = Import<HWND(WINAPI *)(HWND, UINT), kIatGetAncestor>()(AsWindow(mHwnd), 3);
    return Import<BOOL(WINAPI *)(HWND), kIatIsIconic>()(root) != 0;
}

bool cCanvasW32::IsMaximized()
{
    const HWND root = Import<HWND(WINAPI *)(HWND, UINT), kIatGetAncestor>()(AsWindow(mHwnd), 3);
    return Import<BOOL(WINAPI *)(HWND), kIatIsZoomed>()(root) != 0;
}

bool cCanvasW32::IsCaptionless()
{
    return (Import<LONG(WINAPI *)(HWND, int), kIatGetWindowLong>()(AsWindow(mHwnd), kStyle) & 0xc00000) == 0;
}

bool cCanvasW32::IsPaintUnlocked() { return mPaintLock == 0; }

// On: borderless over the desktop, topmost. Off: the styles Show gives back.
void cCanvasW32::SetFullscreen(bool fullscreen)
{
    if (fullscreen) {
        if (mFullscreen) return;
        mFullscreen = true;
        Import<void(WINAPI *)(DWORD), kIatSetLastError>()(0);
        const HWND hwnd = AsWindow(mHwnd);
        const uint32_t style = Import<LONG(WINAPI *)(HWND, int), kIatGetWindowLong>()(hwnd, kStyle);
        const uint32_t exStyle = Import<LONG(WINAPI *)(HWND, int), kIatGetWindowLong>()(hwnd, kExStyle);
        const auto setLong = Import<LONG(WINAPI *)(HWND, int, LONG), kIatSetWindowLong>();
        setLong(hwnd, kStyle, LONG((style & 0xff30ffff) | 0x80000000));
        setLong(hwnd, kExStyle, LONG((exStyle & 0xfffffcff) | 8));
        RECT desktop;
        Import<BOOL(WINAPI *)(HWND, LPRECT), kIatGetWindowRect>()(
            Import<HWND(WINAPI *)(), kIatGetDesktopWindow>()(), &desktop);
        Import<BOOL(WINAPI *)(HWND, HWND, int, int, int, int, UINT), kIatSetWindowPos>()(hwnd, kTopmost, desktop.left,
                                                                                       desktop.top, 0, 0, 0x31);
        Import<BOOL(WINAPI *)(HWND, int), kIatShowWindow>()(hwnd, 1);
    } else if (mFullscreen) {
        mFullscreen = false;
        Import<void(WINAPI *)(DWORD), kIatSetLastError>()(0);
        Show(-1, false);
        ApplyRect();
    }
}

void cCanvasW32::SetFlag19(bool value)
{
    mFlag19 = value;
    CheckScreenSaver();
}

bool cCanvasW32::GetFlag18() { return mFullscreen; }
void *cCanvasW32::GetHwnd() { return mHwnd; }
uint16_t cCanvasW32::GetWindowClass() { return mAtom; }

void *cCanvasW32::GetMonitor()
{
    return nMultiMon::MonitorFromWindowStub(AsWindow(mHwnd), 0);
}

// Adds the hook (counted) unless it is there; removes it (the later ones shifted down, the last one released) when it
// is there; false for adding a present one or removing an absent one.
bool cCanvasW32::SetMessageHook(cICanvasMessageHook *hook, bool add)
{
    void **it = mHooks.mFirst;
    for (; it != mHooks.mLast; ++it)
        if (*it == hook) break;
    if (it == mHooks.mLast) {
        if (!add) return false;
        if (hook) hook->AddRef();
        if (mHooks.mLast != mHooks.mEnd) {
            *mHooks.mLast = hook;
            if (hook) hook->AddRef();
            ++mHooks.mLast;
        } else {
            void *value = hook;
            mHooks.EmplaceReallocate(mHooks.mLast, &value);
        }
        if (hook) hook->Release();
        return true;
    }
    if (add) return false;
    void **const end = mHooks.mLast;
    for (; it + 1 != end; ++it) {
        auto *const next = static_cast<cIGZUnknown *>(it[1]);
        auto *const old = static_cast<cIGZUnknown *>(*it);
        if (next != old) {
            if (next) next->AddRef();
            *it = next;
            if (old) old->Release();
        }
    }
    if (auto *const last = static_cast<cIGZUnknown *>(mHooks.mLast[-1])) last->Release();
    --mHooks.mLast;
    return true;
}

// ==== the service faces ====

uint32_t cCanvasW32::GetServiceID() { return mServiceId; }
void cCanvasW32::SetServiceID(uint32_t id) { mServiceId = id; }
int32_t cCanvasW32::GetServicePriority() { return kPriority; }
bool cCanvasW32::IsServiceRunning() { return mRunning; }
void cCanvasW32::SetServiceRunning(bool running) { mRunning = running; }
bool cCanvasW32::OnIdle(uint32_t) { return true; }
int32_t cCanvasW32::GetServiceTickPriority() { return kPriority; }
bool cCanvasW32::DoMessage(cGZMessage *) { return true; }

// Bit 3 of the power flags: a repaint each tick.
bool cCanvasW32::OnTick(uint32_t)
{
    if (mPowerFlags & 8) {
        mPaintPending = true;
        if (mPaintLock == 0)
            Import<BOOL(WINAPI *)(HWND, const RECT *, BOOL), kIatInvalidateRect>()(AsWindow(mHwnd), nullptr, FALSE);
    }
    return true;
}

// The code page of the keyboard layout, the background brush, the window class (our window procedure and icon
// callback) and the window, the framework's window service told, the timer, the tick, the power flags,
// and the three system settings saved.
bool cCanvasW32::Init()
{
    char text[0x80];
    const HKL layout = Import<HKL(WINAPI *)(DWORD), kIatGetKeyboardLayout>()(0);
    Import<int(WINAPI *)(LCID, LCTYPE, LPSTR, int), kIatGetLocaleInfo>()(LCID(WORD(uintptr_t(layout))), 0x1004, text, 0x80);
    mCodePage = uint32_t(atoi(text));
    if (mCodePage == 0) mCodePage = 0x10;
    mBrush = Import<HBRUSH(WINAPI *)(COLORREF), kIatCreateSolidBrush>()(0xa85e02);

    const auto getModule = Import<HMODULE(WINAPI *)(LPCSTR), kIatGetModuleHandle>();
    WNDCLASSEXA wc;
    wc.cbSize = 0x30;
    wc.style = 8;
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&WindowProc);
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = getModule(nullptr);
    wc.hIcon = nullptr;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = kClassName;
    wc.hIconSm = nullptr;
    Import<BOOL(WINAPI *)(HMODULE, LPCSTR, ENUMRESNAMEPROCA, LONG_PTR), kIatEnumResourceNames>()(
        getModule(nullptr), MAKEINTRESOURCEA(0xe), reinterpret_cast<ENUMRESNAMEPROCA>(&LoadClassIcon),
        reinterpret_cast<LONG_PTR>(&wc));
    mAtom = Import<ATOM(WINAPI *)(const WNDCLASSEXA *), kIatRegisterClassEx>()(&wc);
    const HMODULE module = getModule(nullptr);
    mHwnd = Import<HWND(WINAPI *)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID),
                   kIatCreateWindowEx>()(0, MAKEINTATOM(mAtom), nullptr, 0x90000000, CW_USEDEFAULT, CW_USEDEFAULT,
                                         CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, module, nullptr);
    const HWND hwnd = AsWindow(mHwnd);
    Import<LONG(WINAPI *)(HWND, int, LONG), kIatSetWindowLong>()(hwnd, kUserData,
                                                                 LONG(reinterpret_cast<uintptr_t>(
                                                                     static_cast<cICanvasW32Behavior *>(this))));
    Import<BOOL(WINAPI *)(HWND, int), kIatShowWindow>()(hwnd, 1);
    Import<BOOL(WINAPI *)(HWND), kIatUpdateWindow>()(hwnd);
    cCanvasRect client;
    GetClientRect(&client);
    mRect = client;
    cIGZUnknown *windowService = nullptr;
    As<ifaces::cIGZFrameWork>(GetServiceObject())->QueryInterface(kFrameworkServiceId, reinterpret_cast<void **>(&windowService));
    RawSlot<6, void>(windowService, mHwnd);
    Import<UINT_PTR(WINAPI *)(HWND, UINT_PTR, UINT, TIMERPROC), kIatSetTimer>()(hwnd, 0, 2000, nullptr);
    As<ifaces::cIGZFrameWork>(GetServiceObject())->AddToTick(reinterpret_cast<ifaces::cIGZSystemService *>(static_cast<cIGZSystemService *>(this)));
    SetPowerFlags(10);
    const auto parameters = Import<BOOL(WINAPI *)(UINT, UINT, PVOID, UINT), kIatSystemParametersInfo>();
    mHasSaved1A = parameters(0x101a, 0, &mSaved1A, 0) != 0;
    mHasSaved5E = parameters(0x5e, 0, &mSaved5E, 0) != 0;
    mHasSaved1C = parameters(0x101c, 0, &mSaved1C, 0) != 0;
    if (windowService) windowService->Release();
    return true;
}

// The window told it is going (the canvas event, code 0), the hooks let go, the timer and the window gone (the
// framework's window service told), the class and the brush deleted, the system settings put back.
bool cCanvasW32::Shutdown()
{
    if (mHwnd) {
        cCanvasEvent event(0);
        NotifyListeners(kCanvasEvent, 0, &event);
    }
    for (void **it = mHooks.mFirst; it != mHooks.mLast; ++it)
        if (auto *const hook = static_cast<cIGZUnknown *>(*it)) hook->Release();
    mHooks.mLast = mHooks.mFirst;
    if (mHwnd) {
        Import<BOOL(WINAPI *)(HWND, UINT_PTR), kIatKillTimer>()(AsWindow(mHwnd), 0);
        Import<BOOL(WINAPI *)(HWND), kIatDestroyWindow>()(AsWindow(mHwnd));
        mHwnd = nullptr;
        CallWindowService(nullptr);
    }
    if (mAtom) {
        Import<BOOL(WINAPI *)(LPCSTR, HINSTANCE), kIatUnregisterClass>()(
            MAKEINTATOM(mAtom), Import<HMODULE(WINAPI *)(LPCSTR), kIatGetModuleHandle>()(nullptr));
        mAtom = 0;
    }
    if (mBrush) {
        Import<BOOL(WINAPI *)(HGDIOBJ), kIatDeleteObject>()(mBrush);
        mBrush = nullptr;
    }
    const auto parameters = Import<BOOL(WINAPI *)(UINT, UINT, PVOID, UINT), kIatSystemParametersInfo>();
    if (mHasSaved1A) parameters(0x101b, 0, reinterpret_cast<PVOID>(uintptr_t(mSaved1A)), 0);
    if (mHasSaved5E) parameters(0x5d, mSaved5E, nullptr, 0);
    if (mHasSaved1C) parameters(0x101d, 0, reinterpret_cast<PVOID>(uintptr_t(mSaved1C)), 0);
    return true;
}

} // namespace nGZGraphic4
