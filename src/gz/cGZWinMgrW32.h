#pragma once
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "gz/cRZKeyboard.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZStopwatch.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <unordered_set>

// cGZWinMgrW32 (lane KQ): the window manager service (service id 0xa417445e, priority 0x1312d0, tick priority 0x4c4b41,
// interface 0x5a4; TS2-Extender's RZ::WM, cached at 0x10f51ac by the getter 0x9c5bd6). It owns the root window and the
// registry of live windows, routes the canvas's mouse and key events to the capture, modal, focus and main windows, keeps
// the focus / capture / mouse-over / modal windows, posts and dispatches window messages (a locked queue drained on
// tick), runs modal loops (a Win32 message pump), defers window destruction, sends timer messages, scales between screen
// and UI coordinates (the "uiScaleFactor" setting) and holds the keyboard state (cRZKeyboard) and the cursor manager.
//
// Classes (RTTI): cGZWinMgrW32 : cGZWinMgrBase, which is cIGZWinMgr at +0 (game vtable 0xe5b5b8 / W32 0xe5b240, 74 slots)
// and cRZSystemService at +4 (0xe5b6e4 / 0xe5b36c, 13 slots; its cRZUnknownMT at +8, 0xe5b71c / 0xe5b3a4, 6). The base is
// abstract (purecalls at slots 4, 7, 17, 26, 36, 48 to 55 and 73 are W32's). 0x218 bytes. The only creator is the factory
// 0x970d39 in the Win32 COM director's table (class 0x84174448): game new 0x218, the constructor 0x970ea0 (which calls the
// base constructor 0x972aab), the +4 face. With the factory hooked to Create, the game's vtables are on no object: the
// constructors, the destructors 0x970f16 / 0x972bcd, the deleting destructors 0x970f50 / 0x972c94 and every slot are in
// effect through it (db/status/cGZWinMgrW32.jsonl). The keyboard (cRZKeyboard, +0x6c) is built only by the base
// constructor.
//
// The Win32 calls (ShowCursor, ClientToScreen, SetCursorPos, Sleep, PeekMessageA, TranslateMessage, DispatchMessageA,
// PostMessageA) go through the game's import slots (game/import.h). Windows are other classes' objects (cGZWin and its
// subclasses: their cIGZWin face); their slots are called by name where the method has a fact (ifaces::cIGZWin), else by
// number (the WinSlot enum in cGZWinMgrW32.size.cpp).

using WinRef = cRZAutoRefCount<cIGZUnknown>;

// cIGZWinMgr (iid 0x5a4), 74 slots. The names avoid the Win32 A/W macros (SendMessage, PostMessage, MessageBox).
class cIGZWinMgr : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x5a4;

    // A window message (a cIGZWin's DoMessage argument): the id and three words.
    struct WinMessage {
        uint32_t id, a, b, c;
    };
    // The input events the canvas targets build (slot 15's and slot 16's argument).
    struct MouseEvent {
        uint32_t type;
        int32_t x, y;
        uint16_t buttons;   // +0xc
        uint16_t modifiers; // +0xe
    };
    struct KeyEvent {
        uint32_t type; // 5 down, 6 up
        uint32_t key;
        uint32_t a, b;
    };

    virtual cIGZUnknown *GetMainWindow() = 0;                                      // slot 3, folded 0x1674d0 (+0x44)
    virtual bool SetMainWindow(cIGZUnknown *window) = 0;                           // slot 4, W32 0x971897
    virtual bool QueryMainWindow(uint32_t iid, void **out) = 0;                    // slot 5, 0x973660
    virtual void UpdateMainWindow() = 0;                                           // slot 6, 0x973806 (main slot 102)
    // Slot 7, W32 0x971333: the event as a variant {Win32 message, flags, param} to the canvas's slot 21.
    virtual bool SendInputEvent(uint32_t type, uint32_t flags, uint32_t param) = 0;
    virtual bool SendMouseInput(uint32_t type, int32_t x, int32_t y, uint32_t flags) = 0; // slot 8, 0x9735e8
    virtual bool SendInputEvent2(uint32_t type, uint32_t flags, uint32_t param) = 0;   // slot 9, folded 0x2b5b10 (slot 7)
    virtual bool SendWinMessage(cIGZUnknown *window, uint32_t id, uint32_t a, uint32_t b, uint32_t c) = 0; // 10, 0x973d5f
    virtual bool SendWinMessageStruct(cIGZUnknown *window, const WinMessage *message) = 0; // slot 11, 0x973d39
    virtual void PostWinMessage(cIGZUnknown *target, uint32_t id, uint32_t a, uint32_t b, uint32_t c) = 0; // 12, 0x97386a
    virtual void PostWinMessageStruct(cIGZUnknown *target, const WinMessage *message) = 0; // slot 13, 0x97381b
    virtual void DispatchPostedMessages() = 0;                                     // slot 14, 0x973b83
    virtual bool DispatchMouseEvent(MouseEvent *event) = 0;                        // slot 15, 0x9739e6
    virtual bool DispatchKeyEvent(const KeyEvent *event) = 0;                      // slot 16, 0x9738b7
    virtual uint32_t GetCanvasState() = 0;                                         // slot 17, W32 0x971302 (canvas slot 20)
    virtual bool RegisterWindow(cIGZUnknown *window) = 0;                          // slot 18, 0x972cf6
    virtual bool UnregisterWindow(cIGZUnknown *window) = 0;                        // slot 19, 0x973cd4
    virtual void OnWindowRemoved(cIGZUnknown *window) = 0;                         // slot 20, 0x972d5f
    virtual bool DestroyWindow(cIGZUnknown *window) = 0;                           // slot 21, 0x972ea1
    virtual bool IsWindowRegistered(cIGZUnknown *window) = 0;                      // slot 22, 0x9737bf
    virtual cIGZUnknown *WindowAtPoint(int32_t x, int32_t y) = 0;                  // slot 23, 0x973670
    virtual bool SetMouseOverWindow(cIGZUnknown *window) = 0;                      // slot 24, 0x973d9f
    virtual cIGZUnknown *GetMouseOverWindow() = 0;                                 // slot 25, folded 0x1674c0 (+0x40)
    virtual cIGZUnknown *GetCanvas() = 0;                                          // slot 26, W32 0x9713b7
    virtual void AddTimerWindow(uint32_t window) = 0;                              // slot 27, 0x972cda
    virtual void RemoveTimerWindow(uint32_t window) = 0;                           // slot 28, 0x973c9a
    virtual cIGZUnknown *GetCapture() = 0;                                         // slot 29, folded 0x147800 (+0x34)
    virtual bool SetCapture(cIGZUnknown *window) = 0;                              // slot 30, 0x973442
    virtual bool ReleaseCapture(cIGZUnknown *window) = 0;                          // slot 31, 0x97340a
    virtual uint32_t ShowCursor(uint32_t show) = 0;                                // slot 32, W32 0x971314, base 0x973561
    virtual cIGZUnknown *GetCursorManager() = 0;                                   // slot 33, folded 0x7b9fad (+0x178)
    virtual void GetCursorPos(int32_t *x, int32_t *y) = 0;                         // slot 34, 0x973645
    virtual void GetCursorPosIn(cIGZUnknown *window, int32_t *x, int32_t *y) = 0;  // slot 35, 0x97361f
    virtual void SetCursorPos(int32_t x, int32_t y) = 0;                           // slot 36, W32 0x971823
    virtual cIGZUnknown *GetFocus() = 0;                                           // slot 37, folded 0x1694c0 (+0x38)
    virtual cIGZUnknown *SetFocus(cIGZUnknown *window) = 0;                        // slot 38, 0x9734cb
    virtual cIGZKeyboard *GetKeyboard() = 0;                                       // slot 39, folded 0x237d60 (+0x6c)
    virtual void AddMessageHook(cIGZUnknown *hook) = 0;                            // slot 40, 0x97338d
    virtual void RemoveMessageHook(cIGZUnknown *hook) = 0;                         // slot 41, 0x97358a
    virtual uint32_t DoModal(cIGZUnknown *window) = 0;                             // slot 42, 0x972f48
    virtual bool IsModal() = 0;                                                    // slot 43, 0x97377d
    virtual uint32_t GetModalDepth() = 0;                                          // slot 44, folded 0x6f27ab (+0x190)
    virtual uint32_t DoMessageBox(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) = 0; // slot 45, 0x973264
    virtual bool Unused46(uint32_t a, uint32_t b, uint32_t c) = 0;                 // slot 46, folded 0x1474a0 (false)
    virtual bool Unused47(uint32_t a, uint32_t b, uint32_t c) = 0;                 // slot 47, folded 0x1474a0 (false)
    virtual float GetUIScale(const int32_t *rect) = 0;                             // slot 48, W32 0x9713be
    virtual void ScaleDown(int32_t *x, int32_t *y) = 0;                            // slot 49, W32 0x97163c
    virtual void ScaleUp(int32_t *x, int32_t *y) = 0;                              // slot 50, W32 0x971694
    virtual void ScaleRectDown(int32_t *rect) = 0;                                 // slot 51, W32 0x9710b5
    virtual void ScaleRectUp(int32_t *rect) = 0;                                   // slot 52, W32 0x971035
    virtual void SetDesignHeight(float height) = 0;                                // slot 53, W32 0x9717c7
    virtual void ApplyUIScale() = 0;                                               // slot 54, W32 0x971a55
    virtual void ReloadUIScale() = 0;                                              // slot 55, W32 0x9719de
    virtual bool Unused56() = 0;                                                   // slot 56, folded 0x147480 (false)
    virtual void Unused57(uint32_t a) = 0;                                         // slot 57, folded 0x300a0
    virtual bool Unused58() = 0;                                                   // slot 58, folded 0x147480
    virtual void Unused59(uint32_t a) = 0;                                         // slot 59, folded 0x300a0
    virtual bool Unused60() = 0;                                                   // slot 60, folded 0x147480
    virtual void Unused61(uint32_t a) = 0;                                         // slot 61, folded 0x300a0
    virtual void Unused62() = 0;                                                   // slot 62, folded 0x140fe0
    virtual bool PostModalResult(cIGZUnknown *window, uint32_t value) = 0;         // slot 63, 0x973252
    virtual void UpdateAllWindows() = 0;                                           // slot 64, 0x97375b (each slot 104)
    virtual bool DestroyWindowNow(cIGZUnknown *window) = 0;                        // slot 65, 0x972edd
    virtual bool FlushDeferredDestroys() = 0;                                      // slot 66, 0x972e47
    virtual bool IsDestroyDeferred(cIGZUnknown *window) = 0;                       // slot 67, 0x973788
    virtual int32_t BeginDeferDestroy() = 0;                                       // slot 68, 0x973684
    virtual int32_t EndDeferDestroy() = 0;                                         // slot 69, 0x972e14
    virtual void SendTimerMessages() = 0;                                          // slot 70, 0x973f77
    virtual bool CallMessageHooks(const WinMessage *message) = 0;                  // slot 71, 0x973d0a
    virtual void SetModalResult(uint32_t window, uint32_t value) = 0;              // slot 72, 0x972cb6
    // Slot 73, W32 0x9716fc: the Win32 message pump until `window`'s result arrives (or WM_QUIT: quitResult).
    virtual uint32_t RunModalLoop(cIGZUnknown *window, void *idle, uint32_t unused, uint32_t quitResult) = 0;
};

// cGZWinMgrBase: everything but the Win32 parts. Abstract (the slots above it leaves pure are cGZWinMgrW32's).
class cGZWinMgrBase : public cIGZWinMgr, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x1e8;
    static constexpr uint32_t kServiceId = 0xa417445e;
    static constexpr int32_t kPriority = 0x1312d0, kTickPriority = 0x4c4b41;

    cGZWinMgrBase();            // 0x972aab
    ~cGZWinMgrBase() noexcept override;  // 0x972bcd (no Shutdown); deleting 0x972c94 (+8 slot 3, thunk 0x972c8c)

    cGZWinMgrBase(const cGZWinMgrBase &) = delete;
    cGZWinMgrBase &operator=(const cGZWinMgrBase &) = delete;

    // cIGZUnknown (every face; +4 / +8 through the thunks 0x9717b7 / 0x9717bf). AddRef / Release: cRZSystemService's.
    bool QueryInterface(uint32_t riid, void **out) override; // 0x973c78
    uint32_t AddRef() override { return rz::cRZSystemService::AddRef(); }
    uint32_t Release() override { return rz::cRZSystemService::Release(); }

    // cIGZSystemService (+4 face)
    bool Init() override;                   // 0x973692
    bool Shutdown() override;               // 0x973e55
    bool OnTick(uint32_t counter) override; // 0x9737eb

    // cIGZWinMgr
    cIGZUnknown *GetMainWindow() override;
    bool QueryMainWindow(uint32_t iid, void **out) override;
    void UpdateMainWindow() override;
    bool SendMouseInput(uint32_t type, int32_t x, int32_t y, uint32_t flags) override;
    bool SendInputEvent2(uint32_t type, uint32_t flags, uint32_t param) override;
    bool SendWinMessage(cIGZUnknown *window, uint32_t id, uint32_t a, uint32_t b, uint32_t c) override;
    bool SendWinMessageStruct(cIGZUnknown *window, const WinMessage *message) override;
    void PostWinMessage(cIGZUnknown *target, uint32_t id, uint32_t a, uint32_t b, uint32_t c) override;
    void PostWinMessageStruct(cIGZUnknown *target, const WinMessage *message) override;
    void DispatchPostedMessages() override;
    bool DispatchMouseEvent(MouseEvent *event) override;
    bool DispatchKeyEvent(const KeyEvent *event) override;
    bool RegisterWindow(cIGZUnknown *window) override;
    bool UnregisterWindow(cIGZUnknown *window) override;
    void OnWindowRemoved(cIGZUnknown *window) override;
    bool DestroyWindow(cIGZUnknown *window) override;
    bool IsWindowRegistered(cIGZUnknown *window) override;
    cIGZUnknown *WindowAtPoint(int32_t x, int32_t y) override;
    bool SetMouseOverWindow(cIGZUnknown *window) override;
    cIGZUnknown *GetMouseOverWindow() override;
    void AddTimerWindow(uint32_t window) override;
    void RemoveTimerWindow(uint32_t window) override;
    cIGZUnknown *GetCapture() override;
    bool SetCapture(cIGZUnknown *window) override;
    bool ReleaseCapture(cIGZUnknown *window) override;
    uint32_t ShowCursor(uint32_t show) override;
    cIGZUnknown *GetCursorManager() override;
    void GetCursorPos(int32_t *x, int32_t *y) override;
    void GetCursorPosIn(cIGZUnknown *window, int32_t *x, int32_t *y) override;
    cIGZUnknown *GetFocus() override;
    cIGZUnknown *SetFocus(cIGZUnknown *window) override;
    cIGZKeyboard *GetKeyboard() override;
    void AddMessageHook(cIGZUnknown *hook) override;
    void RemoveMessageHook(cIGZUnknown *hook) override;
    uint32_t DoModal(cIGZUnknown *window) override;
    bool IsModal() override;
    uint32_t GetModalDepth() override;
    uint32_t DoMessageBox(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) override;
    bool Unused46(uint32_t a, uint32_t b, uint32_t c) override;
    bool Unused47(uint32_t a, uint32_t b, uint32_t c) override;
    bool Unused56() override;
    void Unused57(uint32_t a) override;
    bool Unused58() override;
    void Unused59(uint32_t a) override;
    bool Unused60() override;
    void Unused61(uint32_t a) override;
    void Unused62() override;
    bool PostModalResult(cIGZUnknown *window, uint32_t value) override;
    void UpdateAllWindows() override;
    bool DestroyWindowNow(cIGZUnknown *window) override;
    bool FlushDeferredDestroys() override;
    bool IsDestroyDeferred(cIGZUnknown *window) override;
    int32_t BeginDeferDestroy() override;
    int32_t EndDeferDestroy() override;
    void SendTimerMessages() override;
    bool CallMessageHooks(const WinMessage *message) override;
    void SetModalResult(uint32_t window, uint32_t value) override;

    // A posted message: the target (null: every registered window) and the message.
    struct PostedMessage {
        cIGZUnknown *target;
        WinMessage message;
    };
    static_assert(sizeof(PostedMessage) == 20);

    // The registry's hash: the pointer's word shifted right by 2 (0x972a7c).
    struct WindowHash {
        size_t operator()(cIGZUnknown *window) const { return reinterpret_cast<uintptr_t>(window) >> 2; }
    };
    using WindowSet = std::unordered_set<cIGZUnknown *, WindowHash>;

    // The critical section as a member: built (0x1af64e) and destroyed (0x1af68a) in member order.
    struct OwnedLock : cRZCriticalSection {
        OwnedLock() { Construct(); }
        ~OwnedLock() { Destruct(); }
        OwnedLock(const OwnedLock &) = delete;
        OwnedLock &operator=(const OwnedLock &) = delete;
    };

    std::list<PostedMessage> mPosted;           // +0x20 under mLock (posted from any thread)
    OwnedLock mLock;                            // +0x28 its slots 1 / 2 lock and unlock
    bool mInitialized;                          // +0x30
    WinRef mCapture;                            // +0x34
    WinRef mFocus;                              // +0x38
    WinRef mModal;                              // +0x3c
    WinRef mMouseOver;                          // +0x40
    WinRef mMainWindow;                         // +0x44 the root window (Init: a cGZWin, 0x92fded)
    WindowSet mWindows;                         // +0x48 the registered windows (not counted)
    void *mFramework;                           // +0x68 GetServiceObject's, cached by Init (not counted)
    cRZKeyboard mKeyboard;                      // +0x6c
    cIGZUnknown *mCursorManager;                // +0x178 counted by hand (Init to Shutdown)
    int32_t mCursorX, mCursorY;                 // +0x17c / +0x180 the last mouse event's position (SetCursorPos too)
    uint32_t mUnknown184, mUnknown188;          // +0x184 / +0x188 zero, never used
    uint32_t mModifiers;                        // +0x18c the last mouse event's modifier word
    int32_t mModalDepth;                        // +0x190
    bool mUpdateMainWindow;                     // +0x194 slot 6 asks the main window (true)
    int32_t mDeferDepth;                        // +0x198 BeginDeferDestroy's depth
    std::list<cIGZUnknown *> mDeferred;         // +0x19c windows whose destruction waits (not counted)
    uint32_t mNextWindowId;                     // +0x1a4 for windows registered without an id (from 1)
    std::map<uint32_t, uint32_t> mModalResults; // +0x1a8 window -> result arrived for a modal window not on top
    std::list<WinRef> mHooks;                   // +0x1b0 message hooks (slot 3 (0, message))
    std::set<uint32_t> mTimerWindows;           // +0x1b8 windows sent 0x1c on tick
    std::set<uint32_t>::iterator mTimerNext;    // +0x1c0 SendTimerMessages' position (RemoveTimerWindow steps it)
    uint32_t mUnknown1C4;                       // +0x1c4 never written
    cRZStopwatch mStopwatch;                    // +0x1c8 mode 4: the timer messages' time
    bool mCaptureReplaceable;                   // +0x1e0 SetCapture may replace a capture (true)
    bool mModalMouseOverMain;                   // +0x1e1 outside a modal window the mouse-over follows the main window
    bool mModalReleasesCapture;                 // +0x1e2 (true)
    bool mModalShows;                           // +0x1e3 DoModal shows the window (true)
    bool mModalActivates;                       // +0x1e4 DoModal calls its slot 28 (true)
    bool mModalNotifiesParent;                  // +0x1e5 after DoModal, the parent's slot 20 (false)
    bool mModalNotifiesNewParent;               // +0x1e6 the same when the window had no registered parent (true)
};

static_assert(sizeof(cGZWinMgrBase) == cGZWinMgrBase::kSize, "cGZWinMgrBase size");
static_assert(offsetof(cGZWinMgrBase, mServiceId) == 0x10, "cGZWinMgrBase layout: the service part at +4");
static_assert(offsetof(cGZWinMgrBase, mPosted) == 0x20 && offsetof(cGZWinMgrBase, mLock) == 0x28 &&
                  offsetof(cGZWinMgrBase, mInitialized) == 0x30 && offsetof(cGZWinMgrBase, mCapture) == 0x34 &&
                  offsetof(cGZWinMgrBase, mMainWindow) == 0x44 && offsetof(cGZWinMgrBase, mWindows) == 0x48 &&
                  offsetof(cGZWinMgrBase, mFramework) == 0x68 && offsetof(cGZWinMgrBase, mKeyboard) == 0x6c &&
                  offsetof(cGZWinMgrBase, mCursorManager) == 0x178 && offsetof(cGZWinMgrBase, mModifiers) == 0x18c &&
                  offsetof(cGZWinMgrBase, mModalDepth) == 0x190 && offsetof(cGZWinMgrBase, mUpdateMainWindow) == 0x194 &&
                  offsetof(cGZWinMgrBase, mDeferDepth) == 0x198 && offsetof(cGZWinMgrBase, mDeferred) == 0x19c &&
                  offsetof(cGZWinMgrBase, mNextWindowId) == 0x1a4 && offsetof(cGZWinMgrBase, mModalResults) == 0x1a8 &&
                  offsetof(cGZWinMgrBase, mHooks) == 0x1b0 && offsetof(cGZWinMgrBase, mTimerWindows) == 0x1b8 &&
                  offsetof(cGZWinMgrBase, mTimerNext) == 0x1c0 && offsetof(cGZWinMgrBase, mStopwatch) == 0x1c8 &&
                  offsetof(cGZWinMgrBase, mCaptureReplaceable) == 0x1e0 &&
                  offsetof(cGZWinMgrBase, mModalNotifiesNewParent) == 0x1e6,
              "cGZWinMgrBase layout");

class cGZWinMgrW32 : public cGZWinMgrBase {
public:
    static constexpr uint32_t kSize = 0x218;
    static constexpr uint32_t kClassId = 0x84174448; // the COM director's class id for the factory

    cGZWinMgrW32();            // 0x970ea0
    ~cGZWinMgrW32() noexcept override;  // 0x970f16 (Shutdown first); deleting 0x970f50 (+8 slot 3, thunk 0x970f48)

    // 0x970d39 (the factory the Win32 COM director registers): new, construct, the +4 face.
    static cIGZSystemService *__cdecl Create();

    bool Init() override;     // 0x9714f4
    bool Shutdown() override; // 0x971955

    bool SetMainWindow(cIGZUnknown *window) override;
    bool SendInputEvent(uint32_t type, uint32_t flags, uint32_t param) override;
    uint32_t GetCanvasState() override;
    cIGZUnknown *GetCanvas() override;
    uint32_t ShowCursor(uint32_t show) override;
    void SetCursorPos(int32_t x, int32_t y) override;
    float GetUIScale(const int32_t *rect) override;
    void ScaleDown(int32_t *x, int32_t *y) override;
    void ScaleUp(int32_t *x, int32_t *y) override;
    void ScaleRectDown(int32_t *rect) override;
    void ScaleRectUp(int32_t *rect) override;
    void SetDesignHeight(float height) override;
    void ApplyUIScale() override;
    void ReloadUIScale() override;
    uint32_t RunModalLoop(cIGZUnknown *window, void *idle, uint32_t unused, uint32_t quitResult) override;

    // 0x970f72 (cdecl): the canvas's mouse flags as the manager's (1 -> 4, 2 -> 8, 0x10 -> 1, 0x20 -> 2, 0x40 -> 0x10).
    static uint32_t __cdecl MapMouseFlags(uint32_t flags);
    // 0x970fad (cdecl): an input event type as the Win32 message it stands for (0 for none).
    static uint32_t __cdecl MapEventType(uint32_t type);

    // The three message targets the input source sends the canvas's messages to (each: a vtable, the manager). Not
    // counted (QueryInterface false, AddRef / Release 1: the folded 0x1474b0 / 0x886f4).
    struct cCanvasMessageTarget : cIGZMessageTarget2 { // game vtable 0xe5b204
        cIGZWinMgr *mOwner;
        explicit cCanvasMessageTarget(cIGZWinMgr *owner) : mOwner(owner) {}
        bool QueryInterface(uint32_t riid, void **out) override;
        uint32_t AddRef() override;
        uint32_t Release() override;
        bool DoMessage(void *message) override; // 0x97111c
    };
    struct cMouseMessageTarget : cIGZMessageTarget2 { // game vtable 0xe5b218
        cIGZWinMgr *mOwner;
        explicit cMouseMessageTarget(cIGZWinMgr *owner) : mOwner(owner) {}
        bool QueryInterface(uint32_t riid, void **out) override;
        uint32_t AddRef() override;
        uint32_t Release() override;
        bool DoMessage(void *message) override; // 0x971239
    };
    struct cKeyMessageTarget : cIGZMessageTarget2 { // game vtable 0xe5b22c
        cIGZWinMgr *mOwner;
        explicit cKeyMessageTarget(cIGZWinMgr *owner) : mOwner(owner) {}
        bool QueryInterface(uint32_t riid, void **out) override;
        uint32_t AddRef() override;
        uint32_t Release() override;
        bool DoMessage(void *message) override; // 0x9711d1
    };

    // The set at +0x1e8: its comparator holds a word (0), and nothing but the constructor (0x970e7a) and the destructor
    // uses it.
    struct UnusedCompare {
        uint32_t mWord = 0;
        bool operator()(uint32_t a, uint32_t b) const { return a < b; }
    };

    std::set<uint32_t, UnusedCompare> mUnusedSet; // +0x1e8 (comparator), +0x1ec head, +0x1f0 size
    cCanvasMessageTarget mCanvasTarget;           // +0x1f4 the canvas's messages (input id 0x1a10630a)
    cMouseMessageTarget mMouseTarget;             // +0x1fc mouse messages (0xfa0c6390)
    cKeyMessageTarget mKeyTarget;                 // +0x204 key messages (0x7a104750)
    cIGZUnknown *mCanvas;                         // +0x20c graphics service slot 14's (counted, Init to Shutdown)
    cIGZUnknown *mInputSource;                    // +0x210 the canvas's 0x3ac7a210 face's slot 3 (counted)
    uint32_t mUIScaleFactor;                      // +0x214 the "uiScaleFactor" setting (1 to 3); set by Init
};

static_assert(sizeof(cGZWinMgrW32) == cGZWinMgrW32::kSize, "cGZWinMgrW32 size");
static_assert(offsetof(cGZWinMgrW32, mUnusedSet) == 0x1e8 && offsetof(cGZWinMgrW32, mCanvasTarget) == 0x1f4 &&
                  offsetof(cGZWinMgrW32, mMouseTarget) == 0x1fc && offsetof(cGZWinMgrW32, mKeyTarget) == 0x204 &&
                  offsetof(cGZWinMgrW32, mCanvas) == 0x20c && offsetof(cGZWinMgrW32, mInputSource) == 0x210 &&
                  offsetof(cGZWinMgrW32, mUIScaleFactor) == 0x214,
              "cGZWinMgrW32 layout");
