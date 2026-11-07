// cGZWinMgrW32 (lane KQ): the window manager in the standalone shape (src/gz/cGZWinMgrW32.h). Built for size in the game.
#include "rz/RZMessageServer.h"
#include "gz/cGZWinMgrW32.h"

#include "game/game.h"
#include "game/import.h"
#include "game/original.h"
#include "rz/cRZMessage2Standard.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"          // gz::AnyUint32
#include "gz/cGZClipboardManagerClipboard.h" // GetMessageServer
#include "gz/cIGZCOM.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZFrameWorkW32.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/cIGZWin.h"
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZVariant.h"
#include "gz/cRZCursor.h" // cRZCursorAdapters::ConstructManager

#include <new>

namespace {

// The cIGZWin slots called here without a method fact (by how the manager uses them).
enum WinSlot : int {
    kWinAdoptModal = 19,      // (window) bool: the main window takes a parentless modal window (DoModal)
    kWinModalDone = 20,       // (window): the parent's notice after DoModal
    kWinRemoveChild = 22,     // (window) bool: DestroyWindowNow through the registered parent
    kWinIsInside = 26,        // (window) bool: this window lies inside `window` (the removed one)
    kWinContains = 27,        // (window) bool: `window` lies inside this one (DoModal's focus restore)
    kWinBringToFront = 28,    // (): a modal window, a captured window clicked
    kWinChildAtPoint = 47,    // (x, y) window
    kWinScreenToClient = 74,  // (int32_t *x, int32_t *y)
    kWinHitTest = 77,         // (x, y) bool
    kWinGetRegistryId = 80,   // () id (0: none yet)
    kWinSetRegistryId = 81,   // (id)
    kWinAcceptsMouse = 86,    // () bool: asked before a modal window gets a mouse event
    kWinAcceptsInput = 88,    // () bool: asked of the capture, the modal, the focus and the main window
    kWinMouseEvent = 96,      // (MouseEvent *) bool
    kWinFocusSuccessor = 100, // () window: who takes the focus when this one goes
    kWinUpdate = 102,         // (): UpdateMainWindow
    kWinUpdateAll = 104,      // (): UpdateAllWindows
};

ifaces::cIGZWin *Win(const void *window) { return As<ifaces::cIGZWin>(window); }

// The window message ids the manager sends.
enum : uint32_t {
    kMsgFocusLost = 0x10,     // (new focus)
    kMsgFocusGained = 0x11,   // (old focus)
    kMsgCaptureChanged = 0x12, // (old capture, new capture)
    kMsgMouseEnter = 0x13,    // (previous mouse-over)
    kMsgMouseLeave = 0x14,    // (next mouse-over)
    kMsgKey = 0x1a,           // (target, event type, event)
    kMsgKeyUnhandled = 0x1b,
    kMsgTimer = 0x1c,         // (stopwatch time)
};

// The notifier's (0x9c5ab4) messages around a modal run, and the message box request.
enum : uint32_t { kNoteModalBegin = 0x62ba7b42, kNoteModalEnd = 0x62ba7b43, kMsgBoxRequest = 0x62ba7b44 };
constexpr uint32_t kMsgBoxIid = 0x57c, kMsgBoxClass = 0x593;
constexpr uint32_t kCursorManagerClass = 0x4352fef3, kCursorManagerIid = 0xa34c498a;
constexpr uint32_t kModalFlag = 0x1000;     // cIGZWin flag set while the window runs modal
constexpr uint32_t kFocusableFlag = 0x8000; // cIGZWin flag: may take the focus
constexpr uint32_t kNoModalResult = 0x80000000, kQuitModalResult = 0x80000001;

// Services: graphics (0x7ae7f4: slot 14 the canvas, slot 30 the cursor) and settings (0x1cbaf3).
constexpr uint32_t kGraphicsId = 0xc416025c, kGraphicsIid = 0x73283c;
constexpr uint32_t kFrameWorkW32Iid = 0x23ea70a1; // the framework's Win32 face (GetMainHWND)
constexpr uint32_t kSettingsId = 0x0ac1887e, kSettingsIid = 0x4bb0d269;
enum GraphicsSlot : int { kGfxCanvas = 14, kGfxShowCursor = 30 };
// The canvas: its rectangle, a state word, the input event (a variant), the input source face.
enum CanvasSlot : int { kCanvasRect = 12, kCanvasState = 20, kCanvasInput = 21 };
constexpr uint32_t kInputSourceIid = 0x3ac7a210;
enum InputSourceSlot : int { kInputSource = 3, kInputAddTarget = 5, kInputRemoveTarget = 6 };
constexpr uint32_t kCanvasTargetId = 0x1a10630a, kMouseTargetId = 0xfa0c6390, kKeyTargetId = 0x7a104750;
// The canvas messages the targets accept, and their slots.
constexpr uint32_t kCanvasMessageIid = 0xda1062fa, kKeyMessageIid = 0x1a104742, kMouseMessageIid = 0x1a0c615f;
enum CanvasMessageSlot : int { kEvType = 9, kEvData10 = 10, kEvData11 = 11, kEvData12 = 12 };
constexpr uint32_t kCanvasChanged = 0x4348b111; // posted to every window when the canvas changes

// The UI scale's globals: the minimum height scaled, the design height (also LegacyCalculateUIScale's), the heights
// for the factors 2 and 3.
constexpr uint32_t kMinScaledHeight = 0x105f808, kDesignHeight = 0x10ecc88, kHeightFactor2 = 0x105f80c,
                   kHeightFactor3 = 0x105f810;
constexpr uint32_t kMinFactor = 1, kMaxFactor = 3;

// The game's import slots (game/import.h).
struct Point {
    int32_t x, y;
};
struct Msg { // MSG
    void *hwnd;
    uint32_t message;
    uint32_t wParam;
    uint32_t lParam;
    uint32_t time;
    Point pt;
};
using ShowCursorFn = int(__stdcall *)(int);
using ClientToScreenFn = int(__stdcall *)(void *, Point *);
using SetCursorPosFn = int(__stdcall *)(int, int);
using SleepFn = void(__stdcall *)(uint32_t);
using PeekMessageFn = int(__stdcall *)(Msg *, void *, uint32_t, uint32_t, uint32_t);
using TranslateMessageFn = int(__stdcall *)(const Msg *);
using DispatchMessageFn = intptr_t(__stdcall *)(const Msg *);
using PostMessageFn = int(__stdcall *)(void *, uint32_t, uint32_t, uint32_t);
constexpr uint32_t kWmQuit = 0x12, kPmRemove = 1;

// Original callees nobody has replaced (the RVA spelled at each call for tools/origsyms.py).
void *Notifier() { return static_cast<void *>(GetMessageServer()); } // 0x9c5ab4, ours
void *MessageServer() { return GetCachedMessageServer(); }

// The service `id` from the framework into `ref` (0x7ae7f4 / 0x1cbaf3: null first, nothing without a framework).
void QueryService(ScopedRef &ref, uint32_t id, uint32_t iid)
{
    ref.mObject = nullptr;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &ref.mObject);
}

// The "uiScaleFactor" setting into `value` (left alone without the setting).
void ReadScaleFactor(void *settings, gz::AnyUint32 &value)
{
    GZServiceString key("uiScaleFactor");
    As<ifaces::cIGZPropertySetString>(settings)->GetPropertyValue(&key, static_cast<cIGZAny *>(&value));
}

// The class object `classId` queried for `iid` from the COM object (a word result; its low byte tested).
bool GetClassObject(uint32_t classId, uint32_t iid, void **out)
{
    return static_cast<uint8_t>(static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(classId, iid, out)) != 0;
}

// 0x9739af (cdecl, recursive): the message to `window`, then up its parents, until one handles it.
bool BubbleMessage(void *window, const cIGZWinMgr::WinMessage *message)
{
    if (!window) return false;
    if (Win(window)->DoMessage(const_cast<cIGZWinMgr::WinMessage *>(message))) return true;
    return BubbleMessage(Win(window)->GetParent(), message);
}

// The posted-message count DispatchPostedMessages works down (the game's 0x10f4e94) and FlushDeferredDestroys' guard (the
// game's 0x10f4e98): only these two functions use them, so they are ours.
int32_t sPostedToDispatch = 0;
int32_t sFlushing = 0;

} // namespace

// ---- cGZWinMgrBase ----

// 0x972aab: the service (id, priority, tick priority), the members in order, the mode-4 stopwatch, the option bytes.
cGZWinMgrBase::cGZWinMgrBase()
    : rz::cRZSystemService(kServiceId, kPriority, kTickPriority), mInitialized(false), mFramework(nullptr),
      mCursorManager(nullptr), mCursorX(0), mCursorY(0), mUnknown184(0), mUnknown188(0), mModifiers(0), mModalDepth(0),
      mUpdateMainWindow(true), mDeferDepth(0), mNextWindowId(1), mTimerNext(mTimerWindows.end()),
      mCaptureReplaceable(true), mModalMouseOverMain(false), mModalReleasesCapture(true), mModalShows(true),
      mModalActivates(true), mModalNotifiesParent(false), mModalNotifiesNewParent(true)
{
    mStopwatch.Construct(4);
}

// 0x972bcd: the members in reverse (the counted windows released, the lock destroyed); no Shutdown.
cGZWinMgrBase::~cGZWinMgrBase() noexcept = default;

bool cGZWinMgrBase::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZWinMgr::kIid) {
        *out = static_cast<cIGZWinMgr *>(this);
        static_cast<cIGZWinMgr *>(this)->AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

// 0x973692: once: the root window (a cGZWin, 0x92fded, given the manager and the keyboard) made main and activated, the
// framework cached, this service added to its tick, the cursor manager (class 0x4352fef3, else built: 0x9d0158) started,
// the stopwatch restarted.
bool cGZWinMgrBase::Init()
{
    if (mInitialized) return true;
    mInitialized = true;
    if (!mMainWindow) {
        void *const memory = ::operator new(0xf8);
        cIGZUnknown *const root =
            memory ? OriginalFunction<cIGZUnknown *(__thiscall *)(void *, cIGZWinMgr *, cIGZKeyboard *), 0x92fded>()(
                         memory, static_cast<cIGZWinMgr *>(this), &mKeyboard)
                   : nullptr;
        mMainWindow.Assign(root);
        Win(mMainWindow.Get())->Activate();
    }
    if (!mFramework) mFramework = GetServiceObject();
    As<ifaces::cIGZFrameWork>(mFramework)->AddToTick(As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
    if (!mCursorManager) {
        if (!GetClassObject(kCursorManagerClass, kCursorManagerIid, reinterpret_cast<void **>(&mCursorManager))) {
            void *const memory = ::operator new(0x2c);
            mCursorManager = memory ? static_cast<cIGZUnknown *>(cRZCursorAdapters::ConstructManager(memory, nullptr)) : nullptr;
            mCursorManager->AddRef();
        }
        RawSlot<3, uint32_t>(mCursorManager); // its start
    }
    OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(&mStopwatch); // restart
    return true;
}

// 0x973e55: the windows held while the capture, modal and mouse-over windows are dropped, the main window torn down and
// dropped, the cursor manager stopped and released, the focus dropped, the service off the tick.
bool cGZWinMgrBase::Shutdown()
{
    if (!mInitialized) return true;
    mInitialized = false;
    const WinRef capture(mCapture), focus(mFocus), modal(mModal), mouseOver(mMouseOver), main(mMainWindow);
    mCapture.Assign(nullptr);
    mModal.Assign(nullptr);
    mMouseOver.Assign(nullptr);
    if (mMainWindow) {
        Win(mMainWindow.Get())->Teardown();
        mMainWindow.Assign(nullptr);
    }
    if (mCursorManager) {
        RawSlot<4, uint32_t>(mCursorManager); // its stop
        mCursorManager->Release();
        mCursorManager = nullptr;
    }
    mFocus.Assign(nullptr);
    As<ifaces::cIGZFrameWork>(mFramework)->RemoveFromTick(As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
    return true;
}

// 0x9737eb: the posted messages, then the timers.
bool cGZWinMgrBase::OnTick(uint32_t)
{
    DispatchPostedMessages();
    SendTimerMessages();
    return true;
}

cIGZUnknown *cGZWinMgrBase::GetMainWindow() { return mMainWindow.Get(); }

bool cGZWinMgrBase::QueryMainWindow(uint32_t iid, void **out)
{
    if (!mMainWindow) return false;
    return mMainWindow->QueryInterface(iid, out);
}

void cGZWinMgrBase::UpdateMainWindow()
{
    if (mUpdateMainWindow) RawSlot<kWinUpdate, uint32_t>(mMainWindow.Get());
}

// 0x9735e8: the point scaled up, packed (y high, x low), as an input event.
bool cGZWinMgrBase::SendMouseInput(uint32_t type, int32_t x, int32_t y, uint32_t flags)
{
    ScaleUp(&x, &y);
    return SendInputEvent(type, flags, static_cast<uint32_t>(y) << 16 | static_cast<uint16_t>(x));
}

bool cGZWinMgrBase::SendInputEvent2(uint32_t type, uint32_t flags, uint32_t param)
{
    return SendInputEvent(type, flags, param);
}

bool cGZWinMgrBase::SendWinMessage(cIGZUnknown *window, uint32_t id, uint32_t a, uint32_t b, uint32_t c)
{
    if (!IsWindowRegistered(window)) return false;
    WinMessage message{id, a, b, c};
    return Win(window)->DoMessage(&message);
}

bool cGZWinMgrBase::SendWinMessageStruct(cIGZUnknown *window, const WinMessage *message)
{
    if (!window || !IsWindowRegistered(window)) return false;
    return Win(window)->DoMessage(const_cast<WinMessage *>(message));
}

void cGZWinMgrBase::PostWinMessage(cIGZUnknown *target, uint32_t id, uint32_t a, uint32_t b, uint32_t c)
{
    mLock.Lock();
    mPosted.push_back(PostedMessage{target, {id, a, b, c}});
    mLock.Unlock();
}

void cGZWinMgrBase::PostWinMessageStruct(cIGZUnknown *target, const WinMessage *message)
{
    mLock.Lock();
    mPosted.push_back(PostedMessage{target, *message});
    mLock.Unlock();
}

// 0x973b83: the messages queued when the count was taken (a nested call works the same count down), one at a time out of
// the lock: to a registered target, or (no target) to every registered window inside a deferral of destruction.
void cGZWinMgrBase::DispatchPostedMessages()
{
    PostedMessage posted{};
    if (sPostedToDispatch <= 0) {
        mLock.Lock();
        sPostedToDispatch = static_cast<int32_t>(mPosted.size());
        mLock.Unlock();
    }
    while (sPostedToDispatch > 0) {
        --sPostedToDispatch;
        mLock.Lock();
        posted = mPosted.front();
        mPosted.pop_front();
        mLock.Unlock();
        if (IsWindowRegistered(posted.target)) {
            Win(posted.target)->DoMessage(&posted.message);
        } else if (!posted.target) {
            BeginDeferDestroy();
            for (cIGZUnknown *window : mWindows) Win(window)->DoMessage(&posted.message);
            EndDeferDestroy();
        }
    }
    --sPostedToDispatch;
}

// 0x9739e6: a mouse event to the capture (in its coordinates), else to the modal window when it is under the mouse and
// takes it, else to the main window; the mouse-over window follows.
bool cGZWinMgrBase::DispatchMouseEvent(MouseEvent *event)
{
    const uint32_t type = event->type;
    if (type == kMsgMouseLeave) {
        if (!mCapture) SetMouseOverWindow(nullptr);
        return true;
    }
    mCursorX = event->x;
    mCursorY = event->y;
    mModifiers = event->modifiers;
    if (cIGZUnknown *const capture = mCapture.Get()) {
        bool result = false;
        capture->AddRef();
        bool deliver = true;
        if (type == 0xd) {
            RawSlot<kWinScreenToClient, uint32_t>(capture, &event->x, &event->y);
        } else if (RawSlot<kWinAcceptsInput, bool>(capture)) {
            RawSlot<kWinScreenToClient, uint32_t>(capture, &event->x, &event->y);
            if (type == 7 || type == 8) {
                RawSlot<kWinBringToFront, uint32_t>(capture);
                SetFocus(capture);
            }
        } else {
            deliver = false;
            if (mMainWindow)
                SetMouseOverWindow(RawSlot<kWinChildAtPoint, cIGZUnknown *>(mMainWindow.Get(), event->x, event->y));
        }
        if (deliver) {
            SetMouseOverWindow(capture);
            result = Win(capture)->DoMessage(event);
        }
        capture->Release();
        return result;
    }
    if (cIGZUnknown *const modal = mModal.Get()) {
        const bool hit = RawSlot<kWinHitTest, bool>(modal, event->x, event->y);
        if (type == 0xd) {
            if (hit && RawSlot<kWinAcceptsMouse, bool>(mModal.Get())) return RawSlot<kWinMouseEvent, bool>(mModal.Get(), event);
        } else if (hit && RawSlot<kWinAcceptsMouse, bool>(mModal.Get()) && RawSlot<kWinAcceptsInput, bool>(mModal.Get())) {
            return RawSlot<kWinMouseEvent, bool>(mModal.Get(), event);
        }
        if (mModalMouseOverMain && mMainWindow)
            SetMouseOverWindow(RawSlot<kWinChildAtPoint, cIGZUnknown *>(mMainWindow.Get(), event->x, event->y));
        return false;
    }
    if (mMainWindow) return RawSlot<kWinMouseEvent, bool>(mMainWindow.Get(), event);
    return false;
}

// 0x9738b7: the keyboard state first; then the focus (else the main) window when it takes input: the hooks, the key
// message up its parents, the raw event, then the unhandled message up to the modal window, and the hooks again.
bool cGZWinMgrBase::DispatchKeyEvent(const KeyEvent *event)
{
    if (event->type == 5) mKeyboard.KeyDown(event->key);
    else if (event->type == 6) mKeyboard.KeyUp(event->key);
    cIGZUnknown *target;
    if (mFocus && RawSlot<kWinAcceptsInput, bool>(mFocus.Get())) target = mFocus.Get();
    else if (mMainWindow && RawSlot<kWinAcceptsInput, bool>(mMainWindow.Get())) target = mMainWindow.Get();
    else return false;
    if (!target) return false;
    WinMessage message{kMsgKey, reinterpret_cast<uintptr_t>(target), event->type, reinterpret_cast<uintptr_t>(event)};
    if (CallMessageHooks(&message)) return true;
    if (BubbleMessage(target, &message)) return true;
    if (Win(target)->DoMessage(const_cast<KeyEvent *>(event))) return true;
    message.id = kMsgKeyUnhandled;
    do {
        if (Win(target)->DoMessage(&message)) return true;
        if (target == mModal.Get()) break;
        target = static_cast<cIGZUnknown *>(Win(target)->GetParent());
    } while (target);
    return CallMessageHooks(&message);
}

// 0x972cf6: a new window gets an id when it has none; false when it is registered already.
bool cGZWinMgrBase::RegisterWindow(cIGZUnknown *window)
{
    if (mWindows.find(window) != mWindows.end()) return false;
    if (RawSlot<kWinGetRegistryId, uint32_t>(window) == 0) RawSlot<kWinSetRegistryId, uint32_t>(window, mNextWindowId++);
    mWindows.insert(window);
    return true;
}

bool cGZWinMgrBase::UnregisterWindow(cIGZUnknown *window)
{
    if (mWindows.find(window) == mWindows.end()) return false;
    mWindows.erase(window);
    return true;
}

// 0x972d5f: a window going away (or one it contains) gives up the capture, the focus (to its successor, else the main
// window) and the mouse-over.
void cGZWinMgrBase::OnWindowRemoved(cIGZUnknown *window)
{
    if (IsWindowRegistered(mCapture.Get()) &&
        (window == mCapture.Get() || RawSlot<kWinIsInside, bool>(mCapture.Get(), window)))
        ReleaseCapture(mCapture.Get());
    if (IsWindowRegistered(mFocus.Get())) {
        if (window == mFocus.Get() || RawSlot<kWinIsInside, bool>(mFocus.Get(), window)) {
            if (RawSlot<kWinFocusSuccessor, cIGZUnknown *>(window))
                SetFocus(RawSlot<kWinFocusSuccessor, cIGZUnknown *>(window));
            else
                SetFocus(mMainWindow.Get());
        }
    } else {
        SetFocus(mMainWindow.Get());
    }
    if (!IsWindowRegistered(mMouseOver.Get()) || window == mMouseOver.Get() ||
        RawSlot<kWinIsInside, bool>(mMouseOver.Get(), window))
        mMouseOver.Assign(nullptr);
}

// 0x972ea1: destroyed now, or (inside a deferral) queued once.
bool cGZWinMgrBase::DestroyWindow(cIGZUnknown *window)
{
    if (mDeferDepth <= 0) return DestroyWindowNow(window);
    if (!IsDestroyDeferred(window)) mDeferred.push_back(window);
    return true;
}

bool cGZWinMgrBase::IsWindowRegistered(cIGZUnknown *window)
{
    if (!window) return false;
    return mWindows.find(window) != mWindows.end();
}

cIGZUnknown *cGZWinMgrBase::WindowAtPoint(int32_t x, int32_t y)
{
    if (!mMainWindow) return nullptr;
    return RawSlot<kWinChildAtPoint, cIGZUnknown *>(mMainWindow.Get(), x, y);
}

// 0x973d9f: the old window told it is left (0x14), the new (registered) one told it is entered (0x13).
bool cGZWinMgrBase::SetMouseOverWindow(cIGZUnknown *window)
{
    if (mMouseOver.Get() == window) return false;
    if (window) window->AddRef();
    WinMessage message{0, 0, 0, 0};
    if (!IsWindowRegistered(mMouseOver.Get())) mMouseOver.Assign(nullptr);
    cIGZUnknown *const next = IsWindowRegistered(window) ? window : nullptr;
    if (mMouseOver) {
        message.id = kMsgMouseLeave;
        message.a = reinterpret_cast<uintptr_t>(next);
        Win(mMouseOver.Get())->DoMessage(&message);
    }
    if (next) {
        message.a = reinterpret_cast<uintptr_t>(mMouseOver.Get());
        message.id = kMsgMouseEnter;
        Win(next)->DoMessage(&message);
    }
    mMouseOver.Assign(next);
    if (window) window->Release();
    return true;
}

cIGZUnknown *cGZWinMgrBase::GetMouseOverWindow() { return mMouseOver.Get(); }

void cGZWinMgrBase::AddTimerWindow(uint32_t window) { mTimerWindows.insert(window); }

// 0x973c9a: SendTimerMessages' position steps past the window first.
void cGZWinMgrBase::RemoveTimerWindow(uint32_t window)
{
    const auto it = mTimerWindows.find(window);
    if (it == mTimerWindows.end()) return;
    if (it == mTimerNext) ++mTimerNext;
    mTimerWindows.erase(it);
}

cIGZUnknown *cGZWinMgrBase::GetCapture() { return mCapture.Get(); }

// 0x973442: the old and the new capture told (0x12, old, new); a capture stays unless replaceable.
bool cGZWinMgrBase::SetCapture(cIGZUnknown *window)
{
    if (window == mCapture.Get()) return true;
    if (!mCaptureReplaceable && mCapture) return true;
    if (window) window->AddRef();
    cIGZUnknown *const old = mCapture.Get();
    if (old) old->AddRef();
    mCapture.Assign(window);
    const uint32_t oldWord = reinterpret_cast<uintptr_t>(old), newWord = reinterpret_cast<uintptr_t>(window);
    if (old) SendWinMessage(old, kMsgCaptureChanged, oldWord, newWord, 0);
    if (mCapture) SendWinMessage(mCapture.Get(), kMsgCaptureChanged, oldWord, newWord, 0);
    if (old) old->Release();
    if (window) window->Release();
    return true;
}

// 0x97340a: the capture's release, then a mouse move (0xd) at the last position.
bool cGZWinMgrBase::ReleaseCapture(cIGZUnknown *window)
{
    if (window != mCapture.Get()) return false;
    SetCapture(nullptr);
    SendMouseInput(0xd, mCursorX, mCursorY, mModifiers);
    return true;
}

// 0x973561: the graphics service's cursor (no check for the service).
uint32_t cGZWinMgrBase::ShowCursor(uint32_t show)
{
    ScopedRef graphics;
    QueryService(graphics, kGraphicsId, kGraphicsIid);
    const uint32_t result = RawSlot<kGfxShowCursor, uint32_t>(graphics.mObject, show);
    graphics.ReleaseObject();
    return result;
}

cIGZUnknown *cGZWinMgrBase::GetCursorManager() { return mCursorManager; }

void cGZWinMgrBase::GetCursorPos(int32_t *x, int32_t *y)
{
    *x = mCursorX;
    *y = mCursorY;
}

void cGZWinMgrBase::GetCursorPosIn(cIGZUnknown *window, int32_t *x, int32_t *y)
{
    GetCursorPos(x, y);
    RawSlot<kWinScreenToClient, uint32_t>(window, x, y);
}

cIGZUnknown *cGZWinMgrBase::GetFocus() { return mFocus.Get(); }

// 0x9734cb: a registered window that may take the focus (flag 0x8000) gets it; the old one is told first (0x10, new),
// the new one after (0x11, old). Returns the focus.
cIGZUnknown *cGZWinMgrBase::SetFocus(cIGZUnknown *window)
{
    if (window == mFocus.Get() || !IsWindowRegistered(window)) return mFocus.Get();
    if (window) {
        window->AddRef();
        if (!Win(window)->IsFlagSet(kFocusableFlag)) {
            window->Release();
            return mFocus.Get();
        }
    }
    cIGZUnknown *const old = mFocus.Get();
    if (old) {
        old->AddRef();
        SendWinMessage(old, kMsgFocusLost, reinterpret_cast<uintptr_t>(window), 0, 0);
    }
    mFocus.Assign(window);
    if (mFocus) SendWinMessage(mFocus.Get(), kMsgFocusGained, reinterpret_cast<uintptr_t>(old), 0, 0);
    if (old) old->Release();
    if (window) window->Release();
    return mFocus.Get();
}

cIGZKeyboard *cGZWinMgrBase::GetKeyboard() { return &mKeyboard; }

// 0x97338d: appended once (a counted entry).
void cGZWinMgrBase::AddMessageHook(cIGZUnknown *hook)
{
    auto it = mHooks.begin();
    {
        const WinRef key(hook);
        while (it != mHooks.end() && it->Get() != key.Get()) ++it;
    }
    if (it == mHooks.end()) {
        const WinRef entry(hook);
        mHooks.push_back(entry);
    }
}

void cGZWinMgrBase::RemoveMessageHook(cIGZUnknown *hook)
{
    auto it = mHooks.begin();
    {
        const WinRef key(hook);
        while (it != mHooks.end() && it->Get() != key.Get()) ++it;
    }
    if (it != mHooks.end()) mHooks.erase(it);
}

// 0x972f48: a registered window with a registered parent (or, parentless, adopted by the registered main window) runs
// modal: the notifier told before and after, the focus and capture taken, the window shown and brought up, flagged
// 0x1000 for the loop (RunModalLoop), its result returned (0x80000000 when it never ran). A result posted for the window
// the modal one interrupted is delivered after; the focus goes back to where it was when that is outside the window.
uint32_t cGZWinMgrBase::DoModal(cIGZUnknown *window)
{
    uint32_t result = kNoModalResult;
    if (!IsWindowRegistered(window)) return result;
    cIGZUnknown *const main = GetMainWindow();
    cIGZUnknown *const parent = static_cast<cIGZUnknown *>(Win(window)->GetParent());
    if (!(parent && IsWindowRegistered(parent)) && !(main && IsWindowRegistered(main))) return result;
    void *const notifier = Notifier();
    if (!notifier) return result;
    cIGZUnknown *const focus = GetFocus();
    if (focus) focus->AddRef();
    cIGZUnknown *const interrupted = mModal.Get();
    if (interrupted) interrupted->AddRef();
    const bool noParent = !(parent && IsWindowRegistered(parent));
    window->AddRef();
    WinMessage note{kNoteModalBegin, Win(window)->GetID(), reinterpret_cast<uintptr_t>(window),
                    static_cast<uint32_t>(mModalDepth)};
    RawSlot<3, uint32_t>(notifier, &note);
    if (parent || RawSlot<kWinAdoptModal, bool>(main, window)) {
        SetFocus(window);
        if (mModalReleasesCapture && GetCapture() != window) SetCapture(nullptr);
        if (mModalShows) Win(window)->SetVisible(1);
        if (mModalActivates) RawSlot<kWinBringToFront, uint32_t>(window);
        mModal.Assign(window);
        Win(window)->SetFlag(kModalFlag, 1);
        ++mModalDepth;
        result = RunModalLoop(window, mFramework, kNoModalResult, kQuitModalResult);
        --mModalDepth;
        Win(window)->SetFlag(kModalFlag, 0);
        mModal.Assign(interrupted);
        if (cIGZUnknown *const now = mModal.Get()) {
            const auto it = mModalResults.find(reinterpret_cast<uintptr_t>(now));
            if (it != mModalResults.end()) {
                PostModalResult(now, it->second);
                mModalResults.erase(it);
            }
        }
        if (mModalNotifiesParent || (mModalNotifiesNewParent && noParent))
            RawSlot<kWinModalDone, uint32_t>(Win(window)->GetParent(), window);
        if (IsWindowRegistered(focus) && !RawSlot<kWinContains, bool>(window, focus)) SetFocus(focus);
        else if (IsWindowRegistered(mModal.Get()) && !RawSlot<kWinContains, bool>(window, mModal.Get()))
            SetFocus(mModal.Get());
        else if (IsWindowRegistered(mFocus.Get()) && RawSlot<kWinContains, bool>(window, mFocus.Get()))
            SetFocus(nullptr);
        As<ifaces::cIGZFrameWork>(mFramework)->OnIdle();
    }
    note = {kNoteModalEnd, Win(window)->GetID(), reinterpret_cast<uintptr_t>(window), static_cast<uint32_t>(mModalDepth)};
    RawSlot<3, uint32_t>(notifier, &note);
    window->Release();
    if (interrupted) interrupted->Release();
    if (focus) focus->Release();
    return result;
}

bool cGZWinMgrBase::IsModal() { return mModalDepth != 0; }
uint32_t cGZWinMgrBase::GetModalDepth() { return static_cast<uint32_t>(mModalDepth); }

// 0x973264: a message box (from the message server's request 0x62ba7b44, else class 0x593, else a cGZWinMsgBox, 0x952874)
// set up with (a, b, c) and its window run modal; -1 when it is not shown. d and e are unused.
uint32_t cGZWinMgrBase::DoMessageBox(uint32_t a, uint32_t b, uint32_t c, uint32_t, uint32_t)
{
    cIGZUnknown *box = nullptr;
    uint32_t result = 0xffffffff;
    if (MessageServer()) {
        void *const memory = RZMemory::Malloc(0x2c);
        cRZMessage2Standard *const built = memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr;
        cIGZMessage2Standard *message = reinterpret_cast<cIGZMessage2Standard *>(built);
        if (message) message->AddRef();
        message->SetType(kMsgBoxRequest);
        message->SetData1(kMsgBoxIid);
        message->SetVoid2(&box);
        RawSlot<3, uint32_t>(MessageServer(), message);
        if (message) message->Release();
    }
    if (!box && !GetClassObject(kMsgBoxClass, kMsgBoxIid, reinterpret_cast<void **>(&box))) {
        void *const memory = ::operator new(0x144);
        void *const object = memory ? OriginalFunction<void *(__thiscall *)(void *), 0x952874>()(memory) : nullptr;
        box = object ? reinterpret_cast<cIGZUnknown *>(static_cast<uint8_t *>(object) + 0xfc) : nullptr;
        box->AddRef();
    }
    if (RawSlot<5, bool>(box, a, b, c)) {
        if (cIGZUnknown *const window = RawSlot<3, cIGZUnknown *>(box)) {
            window->AddRef();
            result = DoModal(window);
            Win(window)->Teardown();
            window->Release();
        }
    }
    box->Release();
    return result;
}

bool cGZWinMgrBase::Unused46(uint32_t, uint32_t, uint32_t) { return false; }
bool cGZWinMgrBase::Unused47(uint32_t, uint32_t, uint32_t) { return false; }
bool cGZWinMgrBase::Unused56() { return false; }
void cGZWinMgrBase::Unused57(uint32_t) {}
bool cGZWinMgrBase::Unused58() { return false; }
void cGZWinMgrBase::Unused59(uint32_t) {}
bool cGZWinMgrBase::Unused60() { return false; }
void cGZWinMgrBase::Unused61(uint32_t) {}
void cGZWinMgrBase::Unused62() {}

// 0x973252: the result as input event 0xf (the modal loop's message 0x700).
bool cGZWinMgrBase::PostModalResult(cIGZUnknown *window, uint32_t value)
{
    return SendInputEvent(0xf, reinterpret_cast<uintptr_t>(window), value);
}

void cGZWinMgrBase::UpdateAllWindows()
{
    for (cIGZUnknown *window : mWindows) RawSlot<kWinUpdateAll, uint32_t>(window);
}

// 0x972edd: a registered window leaves the manager's slots, then its registered parent removes it, else it is torn down.
bool cGZWinMgrBase::DestroyWindowNow(cIGZUnknown *window)
{
    const bool registered = IsWindowRegistered(window);
    OnWindowRemoved(window);
    cIGZUnknown *const parent = registered ? static_cast<cIGZUnknown *>(Win(window)->GetParent()) : nullptr;
    if (IsWindowRegistered(parent)) return RawSlot<kWinRemoveChild, bool>(parent, window);
    if (!registered) return false;
    window->AddRef();
    Win(window)->Teardown();
    window->Release();
    return true;
}

// 0x972e47: the deferred windows destroyed (outside a deferral), not reentered.
bool cGZWinMgrBase::FlushDeferredDestroys()
{
    if (sFlushing > 0) return false;
    ++sFlushing;
    if (mDeferDepth <= 0) {
        for (cIGZUnknown *window : mDeferred) DestroyWindowNow(window);
        mDeferred.clear();
    }
    --sFlushing;
    return true;
}

// 0x973788: the window or one of its parents is queued.
bool cGZWinMgrBase::IsDestroyDeferred(cIGZUnknown *window)
{
    for (cIGZUnknown *deferred : mDeferred)
        for (void *w = window; w; w = Win(w)->GetParent())
            if (w == deferred) return true;
    return false;
}

int32_t cGZWinMgrBase::BeginDeferDestroy() { return ++mDeferDepth; }

// 0x972e14: the deferral's end (the queue flushed at the outermost); an unbalanced end resets the depth.
int32_t cGZWinMgrBase::EndDeferDestroy()
{
    if (mDeferDepth <= 0) {
        mDeferDepth = 0;
        return 0;
    }
    if (--mDeferDepth == 0) FlushDeferredDestroys();
    return mDeferDepth;
}

// 0x973f77: every timer window gets 0x1c with the stopwatch's time (the position steps first: a window may remove itself).
void cGZWinMgrBase::SendTimerMessages()
{
    if (mTimerWindows.size() == 0) return;
    const uint32_t time = mStopwatch.Elapsed();
    mTimerNext = mTimerWindows.begin();
    while (mTimerNext != mTimerWindows.end()) {
        const uint32_t window = *mTimerNext;
        ++mTimerNext;
        SendWinMessage(reinterpret_cast<cIGZUnknown *>(window), kMsgTimer, time, 0, 0);
    }
}

bool cGZWinMgrBase::CallMessageHooks(const WinMessage *message)
{
    for (const WinRef &hook : mHooks)
        if (RawSlot<3, bool>(hook.Get(), 0u, message)) return true;
    return false;
}

void cGZWinMgrBase::SetModalResult(uint32_t window, uint32_t value) { mModalResults[window] = value; }

// ---- cGZWinMgrW32 ----

// 0x970ea0: the base, the unused set, the three message targets (each pointing at the manager); no canvas or input source.
cGZWinMgrW32::cGZWinMgrW32()
    : mCanvasTarget(this), mMouseTarget(this), mKeyTarget(this), mCanvas(nullptr), mInputSource(nullptr)
{
}

// 0x970f16: Shutdown, then the members and the base.
cGZWinMgrW32::~cGZWinMgrW32() noexcept { cGZWinMgrW32::Shutdown(); }

cIGZSystemService *__cdecl cGZWinMgrW32::Create() { return new cGZWinMgrW32; }

// 0x9714f4: the base's Init, then (once) the canvas from the graphics service, its input source (the canvas's 0x3ac7a210
// face's slot 3) given the three targets, and the "uiScaleFactor" setting (1 without one).
bool cGZWinMgrW32::Init()
{
    if (mInitialized) return true;
    cGZWinMgrBase::Init();
    if (!mCanvas) {
        ScopedRef graphics;
        QueryService(graphics, kGraphicsId, kGraphicsIid);
        mCanvas = RawSlot<kGfxCanvas, cIGZUnknown *>(graphics.mObject);
        if (mCanvas) {
            mCanvas->AddRef();
            cIGZUnknown *face = nullptr;
            if (mCanvas->QueryInterface(kInputSourceIid, reinterpret_cast<void **>(&face))) {
                if (!mInputSource) {
                    mInputSource = RawSlot<kInputSource, cIGZUnknown *>(face);
                    mInputSource->AddRef();
                }
                if (mInputSource) {
                    RawSlot<kInputAddTarget, uint32_t>(mInputSource, &mCanvasTarget, kCanvasTargetId);
                    RawSlot<kInputAddTarget, uint32_t>(mInputSource, &mMouseTarget, kMouseTargetId);
                    RawSlot<kInputAddTarget, uint32_t>(mInputSource, &mKeyTarget, kKeyTargetId);
                }
            }
            if (face) face->Release();
        }
        graphics.ReleaseObject();
    }
    ScopedRef settings;
    QueryService(settings, kSettingsId, kSettingsIid);
    gz::AnyUint32 factor(1);
    mUIScaleFactor = 1;
    if (settings.mObject) {
        ReadScaleFactor(settings.mObject, factor);
        mUIScaleFactor = factor.mValue;
    }
    settings.ReleaseObject();
    mInitialized = true;
    return true;
}

// 0x971955: the canvas released, the targets taken off the input source and it released, then the base's Shutdown.
bool cGZWinMgrW32::Shutdown()
{
    if (!mInitialized) return true;
    if (mCanvas) {
        mCanvas->Release();
        mCanvas = nullptr;
    }
    if (mInputSource) {
        RawSlot<kInputRemoveTarget, uint32_t>(mInputSource, &mCanvasTarget, kCanvasTargetId);
        RawSlot<kInputRemoveTarget, uint32_t>(mInputSource, &mMouseTarget, kMouseTargetId);
        RawSlot<kInputRemoveTarget, uint32_t>(mInputSource, &mKeyTarget, kKeyTargetId);
        mInputSource->Release();
        mInputSource = nullptr;
    }
    cGZWinMgrBase::Shutdown();
    mInitialized = false;
    return true;
}

// 0x971897: the focus and the capture dropped, the window made main; with the manager running, sized to the canvas (in
// UI units), activated and focused.
bool cGZWinMgrW32::SetMainWindow(cIGZUnknown *window)
{
    if (mMainWindow.Get() == window) return true;
    if (mInitialized) {
        SetFocus(nullptr);
        ReleaseCapture(mCapture.Get());
    }
    cIGZUnknown *const old = mMainWindow.Get();
    if (old) old->AddRef();
    mMainWindow.Assign(window);
    if (old) old->Release();
    if (mInitialized && mMainWindow) {
        if (mCanvas) {
            int32_t rect[4];
            RawSlot<kCanvasRect, uint32_t>(mCanvas, rect);
            ScaleRectDown(rect);
            Win(mMainWindow.Get())->SetArea(0, 0, rect[2] - rect[0], rect[3] - rect[1]);
        }
        Win(mMainWindow.Get())->Activate();
        SetFocus(mMainWindow.Get());
    }
    return true;
}

// 0x970f72.
uint32_t __cdecl cGZWinMgrW32::MapMouseFlags(uint32_t flags)
{
    uint32_t mapped = (flags & 1) << 2;
    if (flags & 2) mapped |= 8;
    if (flags & 0x10) mapped |= 1;
    if (flags & 0x20) mapped |= 2;
    if (flags & 0x40) mapped |= 0x10;
    return mapped;
}

// 0x970fad: key down / up WM_KEYDOWN / WM_KEYUP, the buttons' down / up, the move, the wheel, a modal result 0x700, the
// canvas change 0x1c.
uint32_t __cdecl cGZWinMgrW32::MapEventType(uint32_t type)
{
    switch (type) {
    case 5: return 0x100;
    case 6: return 0x101;
    case 7: return 0x201;
    case 8: return 0x204;
    case 10: return 0x202;
    case 11: return 0x205;
    case 13: return 0x200;
    case 14: return 0x20a;
    case 15: return 0x700;
    case kCanvasChanged: return 0x1c;
    default: return 0;
    }
}

// 0x971333: {message, flags (the mouse ones mapped), param} as a uint32 array variant to the canvas's input.
bool cGZWinMgrW32::SendInputEvent(uint32_t type, uint32_t flags, uint32_t param)
{
    bool result = false;
    cRZVariant variant;
    const uint32_t mappedFlags = type - 7 <= 7 ? MapMouseFlags(flags) : flags;
    const uint32_t words[3] = {MapEventType(type), mappedFlags, param};
    variant.SetUint32Array(words, 3);
    if (mCanvas) result = RawSlot<kCanvasInput, bool>(mCanvas, &variant);
    return result;
}

uint32_t cGZWinMgrW32::GetCanvasState()
{
    if (!mCanvas) return 0;
    return RawSlot<kCanvasState, uint32_t>(mCanvas);
}

cIGZUnknown *cGZWinMgrW32::GetCanvas() { return mCanvas; }

// 0x971314: Win32's cursor count (the low byte of show), then the graphics service's.
uint32_t cGZWinMgrW32::ShowCursor(uint32_t show)
{
    ImportFunction<ShowCursorFn, 0xd9a4b0>()(static_cast<uint8_t>(show));
    return cGZWinMgrBase::ShowCursor(show);
}

// 0x971823: the position kept, and the Win32 cursor moved there (from the main window's client area).
void cGZWinMgrW32::SetCursorPos(int32_t x, int32_t y)
{
    mCursorX = x;
    mCursorY = y;
    void *const framework = GetServiceObject();
    void *w32 = nullptr;
    if (!framework || !As<ifaces::cIGZFrameWork>(framework)->QueryInterface(kFrameWorkW32Iid, &w32)) return;
    void *const hwnd = As<ifaces::cIGZFrameWorkW32>(w32)->GetMainHWND();
    As<ifaces::cIGZFrameWorkW32>(w32)->Release();
    if (!hwnd) return;
    Point point{x, y};
    ImportFunction<ClientToScreenFn, 0xd9a5e0>()(hwnd, &point);
    ImportFunction<SetCursorPosFn, 0xd9a4b4>()(point.x, point.y);
}

// 0x9713be: the scale for a screen rectangle's height: 1 below the minimum height, else by the "uiScaleFactor" setting
// (clamped to 1..3): 1 the height over the design height, 2 over the factor-2 height, 3 over the factor-3 height (also when
// the design height is the factor-3 height); 1 when the height is not above the design height.
float cGZWinMgrW32::GetUIScale(const int32_t *rect)
{
    ScopedRef settings;
    QueryService(settings, kSettingsId, kSettingsIid);
    gz::AnyUint32 factor(1);
    const float height = static_cast<float>(rect[3] - rect[1]);
    float scale;
    if (*GameAddress<float>(kMinScaledHeight) > height) {
        scale = 1.0f;
    } else {
        if (settings.mObject) ReadScaleFactor(settings.mObject, factor);
        if (factor.mValue < kMinFactor) factor.mValue = kMinFactor;
        else if (factor.mValue > kMaxFactor) factor.mValue = kMaxFactor;
        const float design = *GameAddress<float>(kDesignHeight);
        if (height > design) {
            const float factor3 = *GameAddress<float>(kHeightFactor3);
            scale = factor.mValue == 3 || design == factor3 ? height / factor3 : 1.0f;
            if (factor.mValue == 2) scale = height / *GameAddress<float>(kHeightFactor2);
            if (factor.mValue == 1) scale = height / design;
        } else {
            scale = 1.0f;
        }
    }
    settings.ReleaseObject();
    return scale;
}

// 0x97163c: screen to UI units (truncated).
void cGZWinMgrW32::ScaleDown(int32_t *x, int32_t *y)
{
    int32_t rect[4];
    RawSlot<kCanvasRect, uint32_t>(mCanvas, rect);
    const float scale = GetUIScale(rect);
    *x = static_cast<int32_t>(static_cast<float>(*x) / scale);
    *y = static_cast<int32_t>(static_cast<float>(*y) / scale);
}

// 0x971694: UI to screen units (truncated, then one more when scaled up).
void cGZWinMgrW32::ScaleUp(int32_t *x, int32_t *y)
{
    int32_t rect[4];
    RawSlot<kCanvasRect, uint32_t>(mCanvas, rect);
    const float scale = GetUIScale(rect);
    *x = static_cast<int32_t>(static_cast<float>(*x) * scale);
    *y = static_cast<int32_t>(static_cast<float>(*y) * scale);
    if (scale > 1.0f) {
        ++*x;
        ++*y;
    }
}

// 0x9710b5: the rectangle divided by its own scale.
void cGZWinMgrW32::ScaleRectDown(int32_t *rect)
{
    const float scale = GetUIScale(rect);
    for (int i = 0; i < 4; ++i) rect[i] = static_cast<int32_t>(static_cast<float>(rect[i]) / scale);
}

// 0x971035: the rectangle times the canvas's scale.
void cGZWinMgrW32::ScaleRectUp(int32_t *rect)
{
    int32_t canvas[4];
    RawSlot<kCanvasRect, uint32_t>(mCanvas, canvas);
    const float scale = GetUIScale(canvas);
    for (int i = 0; i < 4; ++i) rect[i] = static_cast<int32_t>(static_cast<float>(rect[i]) * scale);
}

// 0x9717c7: the design height stored (the game's global), then the main window resized.
void cGZWinMgrW32::SetDesignHeight(float height)
{
    *GameAddress<float>(kDesignHeight) = height;
    ApplyUIScale();
}

// 0x971a55: the main window sized to the canvas in UI units.
void cGZWinMgrW32::ApplyUIScale()
{
    int32_t rect[4];
    RawSlot<kCanvasRect, uint32_t>(GetCanvas(), rect);
    ScaleRectDown(rect);
    Win(GetMainWindow())->SetArea(0, 0, rect[2] - rect[0], rect[3] - rect[1]);
}

// 0x9719de: the setting read again; a change applied.
void cGZWinMgrW32::ReloadUIScale()
{
    ScopedRef settings;
    QueryService(settings, kSettingsId, kSettingsIid);
    gz::AnyUint32 factor(1);
    if (settings.mObject) {
        ReadScaleFactor(settings.mObject, factor);
        if (mUIScaleFactor != factor.mValue) {
            mUIScaleFactor = factor.mValue;
            ApplyUIScale();
        }
    }
    settings.ReleaseObject();
}

// 0x9716fc: the Win32 message pump: the idle object's slot 16 each round, Sleep(0) every fourth; the result message
// (0x700) for `window` ends it with its value, others are kept (SetModalResult); WM_QUIT is put back and ends it.
uint32_t cGZWinMgrW32::RunModalLoop(cIGZUnknown *window, void *idle, uint32_t, uint32_t quitResult)
{
    const uint32_t resultMessage = MapEventType(0xf);
    uint32_t round = 0;
    Msg msg;
    for (;;) {
        if (idle) As<ifaces::cIGZFrameWork>(idle)->OnIdle();
        if ((round++ & 3) == 0) ImportFunction<SleepFn, 0xd9a144>()(0);
        while (ImportFunction<PeekMessageFn, 0xd9a578>()(&msg, nullptr, 0, 0, kPmRemove)) {
            if (msg.message == kWmQuit) {
                ImportFunction<PostMessageFn, 0xd9a570>()(msg.hwnd, kWmQuit, msg.wParam, msg.lParam);
                return quitResult;
            }
            if (msg.message == resultMessage) {
                if (msg.wParam == reinterpret_cast<uintptr_t>(window)) return msg.lParam;
                SetModalResult(msg.wParam, msg.lParam);
            } else {
                ImportFunction<TranslateMessageFn, 0xd9a580>()(&msg);
                ImportFunction<DispatchMessageFn, 0xd9a57c>()(&msg);
            }
        }
    }
}

// ---- the message targets ----

bool cGZWinMgrW32::cCanvasMessageTarget::QueryInterface(uint32_t, void **) { return false; }
uint32_t cGZWinMgrW32::cCanvasMessageTarget::AddRef() { return 1; }
uint32_t cGZWinMgrW32::cCanvasMessageTarget::Release() { return 1; }
bool cGZWinMgrW32::cMouseMessageTarget::QueryInterface(uint32_t, void **) { return false; }
uint32_t cGZWinMgrW32::cMouseMessageTarget::AddRef() { return 1; }
uint32_t cGZWinMgrW32::cMouseMessageTarget::Release() { return 1; }
bool cGZWinMgrW32::cKeyMessageTarget::QueryInterface(uint32_t, void **) { return false; }
uint32_t cGZWinMgrW32::cKeyMessageTarget::AddRef() { return 1; }
uint32_t cGZWinMgrW32::cKeyMessageTarget::Release() { return 1; }

// 0x97111c: the canvas's notices (the message queried in place): 0, 5, 7 the scale applied; 4 slot 6 then the scale
// reloaded; 3 the capture dropped, then (as 2) the canvas change posted to every window ({0x4348b111, kind == 2}).
bool cGZWinMgrW32::cCanvasMessageTarget::DoMessage(void *message)
{
    if (!static_cast<cIGZUnknown *>(message)->QueryInterface(kCanvasMessageIid, &message)) return false;
    const uint32_t kind = RawSlot<kEvType, uint32_t>(message);
    switch (kind) {
    case 0:
    case 5:
    case 7: mOwner->ApplyUIScale(); break;
    case 4:
        mOwner->UpdateMainWindow();
        mOwner->ReloadUIScale();
        break;
    case 3: mOwner->SetCapture(nullptr); [[fallthrough]];
    case 2: {
        const cIGZWinMgr::WinMessage change{kCanvasChanged, kind == 2 ? 1u : 0u, 0, 0};
        mOwner->PostWinMessageStruct(nullptr, &change);
        break;
    }
    default: break;
    }
    static_cast<cIGZUnknown *>(message)->Release();
    return true;
}

// 0x9711d1: a key message as a key event (slots 9 to 12) to DispatchKeyEvent.
bool cGZWinMgrW32::cKeyMessageTarget::DoMessage(void *message)
{
    if (!static_cast<cIGZUnknown *>(message)->QueryInterface(kKeyMessageIid, &message)) return false;
    cIGZWinMgr::KeyEvent event;
    event.type = RawSlot<kEvType, uint32_t>(message);
    event.key = RawSlot<kEvData10, uint32_t>(message);
    event.a = RawSlot<kEvData11, uint32_t>(message);
    event.b = RawSlot<kEvData12, uint32_t>(message);
    mOwner->DispatchKeyEvent(&event);
    static_cast<cIGZUnknown *>(message)->Release();
    return true;
}

// 0x971239: a mouse message as a mouse event (the point from slot 10, buttons slot 11, modifiers slot 12), the point
// scaled down for button and move events (types 7 to 14), up for 0x14, to DispatchMouseEvent.
bool cGZWinMgrW32::cMouseMessageTarget::DoMessage(void *message)
{
    if (!static_cast<cIGZUnknown *>(message)->QueryInterface(kMouseMessageIid, &message)) return false;
    int32_t scratch[2];
    cIGZWinMgr::MouseEvent event;
    const int32_t x = RawSlot<kEvData10, const int32_t *>(message, scratch)[0];
    const int32_t y = RawSlot<kEvData10, const int32_t *>(message, scratch)[1];
    event.type = RawSlot<kEvType, uint32_t>(message);
    event.x = x;
    event.y = y;
    event.modifiers = static_cast<uint16_t>(RawSlot<kEvData12, uint32_t>(message));
    event.buttons = static_cast<uint16_t>(RawSlot<kEvData11, uint32_t>(message));
    if (event.type - 7 <= 7) mOwner->ScaleDown(&event.x, &event.y);
    if (event.type == kMsgMouseLeave) mOwner->ScaleUp(&event.x, &event.y);
    mOwner->DispatchMouseEvent(&event);
    static_cast<cIGZUnknown *>(message)->Release();
    return true;
}
