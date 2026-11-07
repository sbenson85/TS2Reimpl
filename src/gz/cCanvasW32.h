#pragma once
// nGZGraphic4::cCanvasW32 (RTTI of the window service; vftables 0xd9cc04, 45 slots, 0xd9ccbc at +8, 13 slots, and
// 0xd9ccf4 at +0xc, 4 slots; 0xdc bytes): lane ON, rendering campaign R2b step 4.
//
// The game window. It is the framework's window service 0xfb6372ee (iids 0x5b637209 for the behaviour face and
// 0xbb6377fa for the window canvas face, the same object): Init registers the window class "{0xdb637c87}", creates the
// window and hands its HWND to the framework's service 0x23ea70a1; Shutdown tears it down and puts the system settings
// back. Its slots show, size, title and place the window (Win32 through the game's import table), keep the paint lock,
// and report the window's state to cGZCanvas, cGraphicSystemDX and the rest of the graphics code. The window procedure
// (WindowProc, src/gz/cCanvasW32Proc.cpp, lane OQ) turns the Win32 messages into the canvas, size/move, mouse and key
// events (src/gz/cCanvasEvents.h) listeners get through the behaviour (cAbstractBehaviorTypeMT).
//
// Standalone shape (docs/standalone-shape.md): the creator 0x155c70 (the graphics COM director's class table) is hooked
// to Create and is the switch. It and the deleting destructor 0x154d40 are the only code storing the three vtables, so
// every slot is in effect through callers. Init hands our window procedure and icon callback to the window class, so the
// originals (0x157800, 0x155b30) and the procedure's helpers are reached by nothing (in effect through callers).
#include "gz/cAbstractBehaviorTypeMT.h"
#include "gz/cIGZMessageTarget.h"
#include "gz/cIGZSystemService.h"
#include "rz/cRZString.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstddef>
#include <cstdint>

class cIGZString;

#include "game/owned.h"
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10c9708); // GetEventManager's cache
// Show's saved state (the flag and the rectangle) and PlaceOnMonitor's title bar offset.
TS2_OWNED_STATIC(bool, 10c96dc);
TS2_OWNED_STATIC(int32_t, 10c96e0)[4]; // a RECT
TS2_OWNED_STATIC(int32_t, 10c96f0);
// TrackMouseLeave's TrackMouseEvent pointer (looked up once) and its thread-safe-statics guard.
TS2_OWNED_STATIC(void *, 10c96f4);
TS2_OWNED_STATIC(int, 10c96f8);
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, febc60); // slot 14's record for an unknown event

namespace nGZGraphic4 {

// A window rectangle as the slots hand it out: left, top, right, bottom.
struct cCanvasRect {
    int32_t mLeft, mTop, mRight, mBottom;
};
static_assert(sizeof(cCanvasRect) == 16, "cCanvasRect layout");

// The window's size (the minimum size slot's answer).
struct cCanvasSize {
    int32_t mWidth, mHeight;
};

// A display monitor as slot 20 takes it: its slot 4 writes the monitor's rectangle.
class cICanvasMonitor : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual cCanvasRect *GetRect(cCanvasRect *out) = 0; // 4
};

// A window message hook (the objects slot 43 keeps): its slot 3 sees every message first. When it sets *handled, the
// window procedure returns what it returned (the message's LRESULT); *handled is not cleared before the call.
class cICanvasMessageHook : public cIGZUnknown {
public:
    virtual int32_t OnWindowMessage(void *hwnd, uint32_t message, uint32_t wParam, int32_t lParam, bool *handled) = 0; // 3
};

// The interface: the behaviour's slots 3-14 and the window slots 15-43, then the deleting destructor (44).
class cICanvasW32Behavior : public nGZEvent::cIAbstractBehavior {
public:
    virtual void Show(int32_t flags, bool restore) = 0;                 // 15 (0x157220, ret 8)
    virtual int32_t SetTopmost(bool topmost) = 0;                       // 16 (0x156f00)
    virtual void SetTitle(const char *title) = 0;                       // 17 (0x156f60)
    virtual void SetSize(int32_t width, int32_t height) = 0;            // 18 (0x157040)
    virtual void SetMinSize(int32_t width, int32_t height) = 0;         // 19 (0x157190)
    virtual void PlaceOnMonitor(cICanvasMonitor *monitor) = 0;          // 20 (0x1555b0)
    virtual uint32_t SetPowerFlags(uint32_t flags) = 0;                 // 21 (0x157070): the previous execution state
    virtual void *SetActive(bool active) = 0;                           // 22 (0x156ee0)
    virtual void LockPaint() = 0;                                       // 23 (0x1561a0)
    virtual void UnlockPaint() = 0;                                     // 24 (0x157720)
    virtual void Repaint() = 0;                                         // 25 (0x157750)
    virtual void FillBackground() = 0;                                  // 26 (0x1556c0)
    virtual uint32_t GetStyleFlags() = 0;                               // 27 (shared 0x149cf0)
    virtual void GetTitle(cIGZString *out) = 0;                         // 28 (0x155470)
    virtual cCanvasRect *GetClientRect(cCanvasRect *out) = 0;           // 29 (0x156d70)
    virtual cCanvasSize *GetMinSize(cCanvasSize *out) = 0;              // 30 (0x1561f0)
    virtual uint32_t GetPowerFlags() = 0;                               // 31 (shared 0x149d00)
    virtual bool IsActiveWindow() = 0;                                  // 32 (0x1560f0)
    virtual bool IsMinimized() = 0;                                     // 33 (0x156160)
    virtual bool IsMaximized() = 0;                                     // 34 (0x156140)
    virtual bool IsCaptionless() = 0;                                   // 35 (0x156120)
    virtual bool IsPaintUnlocked() = 0;                                 // 36 (0x156190)
    virtual void SetFullscreen(bool fullscreen) = 0;                    // 37 (0x1570a0)
    virtual void SetFlag19(bool value) = 0;                             // 38 (0x155460)
    virtual bool GetFlag18() = 0;                                       // 39 (shared 0x156110)
    virtual void *GetHwnd() = 0;                                        // 40 (shared 0x155e40)
    virtual uint16_t GetWindowClass() = 0;                              // 41 (0x1556b0)
    virtual void *GetMonitor() = 0;                                     // 42 (0x154f10)
    virtual bool SetMessageHook(cICanvasMessageHook *hook, bool add) = 0; // 43 (0x154f20, ret 8)
    virtual ~cICanvasW32Behavior() = default;                           // 44: the deleting destructor (0x154d40)
};

struct cCanvasW32Traits {
    static constexpr uint32_t kEventManagerCell = 0x10c9708;
    static const nGZEvent::cEventAttribute &MissingAttributeRecord() { return ts2_rva_febc60; }
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10c9708; }
    // 0x156a70 (cdecl): the registry, built on first use (id 0: nothing stored). Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    static nGZEvent::cEventAttributeMap *Registry();
    static void ResetRegistryForTest();
};

class cCanvasW32 : public nGZEvent::cAbstractBehaviorTypeMT<cICanvasW32Behavior, cCanvasW32Traits>,
                   public cIGZSystemService,
                   public cIGZMessageTarget {
    using Behavior = nGZEvent::cAbstractBehaviorTypeMT<cICanvasW32Behavior, cCanvasW32Traits>;

public:
    static constexpr size_t kSize = 0xdc;
    static constexpr uint32_t kServiceId = 0xfb6372ee;
    static constexpr int32_t kPriority = 0x16e362;
    static constexpr uint32_t kIidBehavior = 0x5b637209;
    static constexpr uint32_t kIidCanvas = 0xbb6377fa;
    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;
    static constexpr uint32_t kIidBehaviorType = 0xfb859324;

    // The events it sends and listens to.
    static constexpr uint32_t kConnectionChangedEvent = 0x5b51e806;
    static constexpr uint32_t kCanvasEvent = 0x1b637474;

    cCanvasW32();
    ~cCanvasW32() override; // 0x154d40 (slot 44)

    // 0x155c70 (cdecl; the COM director's creator, hooked): a new window service (uncounted).
    static cICanvasW32Behavior *__cdecl Create();

    // ---- cIGZUnknown (three faces) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1569e0
    uint32_t AddRef() override;                              // shared 0x146a20
    uint32_t Release() override;                             // 0x156d00

    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0x155e50

    // ---- the window slots ----
    void Show(int32_t flags, bool restore) override;
    int32_t SetTopmost(bool topmost) override;
    void SetTitle(const char *title) override;
    void SetSize(int32_t width, int32_t height) override;
    void SetMinSize(int32_t width, int32_t height) override;
    void PlaceOnMonitor(cICanvasMonitor *monitor) override;
    uint32_t SetPowerFlags(uint32_t flags) override;
    void *SetActive(bool active) override;
    void LockPaint() override;
    void UnlockPaint() override;
    void Repaint() override;
    void FillBackground() override;
    uint32_t GetStyleFlags() override;
    void GetTitle(cIGZString *out) override;
    cCanvasRect *GetClientRect(cCanvasRect *out) override;
    cCanvasSize *GetMinSize(cCanvasSize *out) override;
    uint32_t GetPowerFlags() override;
    bool IsActiveWindow() override;
    bool IsMinimized() override;
    bool IsMaximized() override;
    bool IsCaptionless() override;
    bool IsPaintUnlocked() override;
    void SetFullscreen(bool fullscreen) override;
    void SetFlag19(bool value) override;
    bool GetFlag18() override;
    void *GetHwnd() override;
    uint16_t GetWindowClass() override;
    void *GetMonitor() override;
    bool SetMessageHook(cICanvasMessageHook *hook, bool add) override;

    // ---- cIGZSystemService (+8) ----
    uint32_t GetServiceID() override;
    void SetServiceID(uint32_t id) override;
    int32_t GetServicePriority() override;
    bool IsServiceRunning() override;
    void SetServiceRunning(bool running) override;
    bool Init() override;     // 0x155e90
    bool Shutdown() override; // 0x1574f0
    bool OnTick(uint32_t counter) override;
    bool OnIdle(uint32_t counter) override;
    int32_t GetServiceTickPriority() override;

    // ---- cIGZMessageTarget (+0xc) ----
    bool DoMessage(cGZMessage *message) override;

    uint32_t mStyleFlags;     // +0x10: the Show flags (1 sizable border/thick frame, 2 ..., 8 caption), init 0xb
    uint32_t mPowerFlags;     // +0x14: bit 1 keeps the display awake, bit 3 repaints on ticks, init 10
    bool mFullscreen;         // +0x18
    bool mFlag19;             // +0x19: input off (no mouse or key events)
    bool mFlag1A;             // +0x1a: in a size / move drag (WM_ENTERSIZEMOVE until its end)
    bool mFlag1B;             // +0x1b: sized during the drag
    bool mFlag1C;             // +0x1c: moved during the drag
    bool mFlag1D;             // +0x1d: the mouse's leaving is tracked (TrackMouseEvent asked)
    bool mPaintPending;       // +0x1e
    uint8_t mRaw1F;           // +0x1f: left as the allocator gave it
    cRZString mStringA;       // +0x20: WM_CHAR bytes of a multibyte character still being collected (OnChar)
    uint32_t mPendingCharBytes;         // +0x40: the bytes that character takes (0: none pending)
    cRZString mStringB;       // +0x44: the character converted from mCodePage (OnChar's scratch)
    uint32_t mPaintLock;      // +0x64: how many times the paint is locked
    cRZString mTitle;         // +0x68: the window title
    cCanvasSize mMinSize;     // +0x88
    cCanvasRect mRect;        // +0x90: the rectangle the window should have
    uint16_t mAtom;           // +0xa0: the window class
    uint8_t mRawA2[2];        // +0xa2
    void *mHwnd;              // +0xa4
    void *mBrush;             // +0xa8: the background brush
    uint32_t mWindowStyle;    // +0xac, init 0xc80000
    uint32_t mWindowExStyle;  // +0xb0
    uint32_t mCodePage;       // +0xb4: the keyboard layout's code page (Init)
    uint32_t mSaved1A;        // +0xb8: SystemParametersInfo 0x101a (Init), put back by Shutdown (0x101b)
    uint32_t mSaved5E;        // +0xbc: 0x5e, put back as 0x5d
    uint32_t mSaved1C;        // +0xc0: 0x101c, put back as 0x101d
    bool mHasSaved1A;         // +0xc4: the call worked
    bool mHasSaved5E;         // +0xc5
    bool mHasSaved1C;         // +0xc6
    uint8_t mRawC7;           // +0xc7
    uint32_t mServiceId;      // +0xc8
    bool mRunning;            // +0xcc
    uint8_t mRawCD[3];        // +0xcd
    RefPtrVector mHooks;      // +0xd0: the message hooks (counted)

    // 0x157780 (thiscall, hooked): tells the framework whether the ticks may run: not while the screen saver runs,
    // flags 0x19 / 0x1a are set, or the window is minimized without power flag 4 (the framework's ToggleTick, slot 18).
    void CheckScreenSaver();

    // ---- the window procedure (src/gz/cCanvasW32Proc.cpp) ----
    // The event ids the procedure sends with (NotifyListeners, slot 5) or asks with (AskListeners, slot 6).
    static constexpr uint32_t kSizeMoveEvent = 0x3b6decd1;   // cCanvasSizeMoveEvent
    static constexpr uint32_t kMouseEvent = 0x1b637482;      // cMouseEvent
    static constexpr uint32_t kKeyEvent = 0x5b63748b;        // cKeyEvent
    static constexpr uint32_t kCloseQuery = 0xed016522;      // WM_CLOSE asked (a cNullEvent): true keeps the window
    static constexpr uint32_t kUserMessageEvent = 0x278fdc1f; // message 0x702 (a cScalarEvent<unsigned long>: wParam)

    // 0x157800 (stdcall; the window class's procedure, Init hands it out): the window from its user data (none: the
    // default procedure), the message hooks first (the first that says handled answers), then the messages the window
    // turns into events or state; the rest to DefWindowProcA.
    static int32_t __stdcall WindowProc(void *hwnd, uint32_t message, uint32_t wParam, int32_t lParam);
    // 0x155b30 (stdcall; Init's EnumResourceNamesA callback): the window class's icon (lParam, the WNDCLASSEXA) loaded
    // from the named resource; returns 0 (the first icon ends the enumeration).
    static int32_t __stdcall LoadClassIcon(void *module, const char *type, const char *name, int32_t wndClass);
    // 0x155860 (cdecl): the modifier keys and mouse buttons held (GetAsyncKeyState: 1 shift, 2 control, 4 alt, 0x10 /
    // 0x20 / 0x40 left / right / middle button), with keyFlags' 0x40000000 bit (the key was down before) kept.
    static uint32_t __cdecl KeyModifiers(uint32_t keyFlags);

private:
    void ApplyRect(); // 0x155ab0: the window moved to mRect (unless fullscreen, maximized or minimized)

    int32_t OnMessage(void *hwnd, uint32_t message, uint32_t wParam, int32_t lParam); // 0x157800 past the hooks
    void UpdateRect(); // mRect = the client rectangle, unless fullscreen, maximized or minimized (inlined 3 times)
    void SendSizeMove(uint32_t code); // the client rectangle and the code to the size/move listeners
    void SendCanvasEvent(uint32_t code);
    void SendMouseEvent(uint32_t kind, int32_t x, int32_t y, uint32_t modifiers, bool doubleClick, int32_t wheelDelta,
                        float wheelLines);
    void SendKeyEvent(uint32_t kind, int32_t key, int32_t repeat, uint32_t modifiers);
    uint32_t MouseModifiers(uint32_t buttons); // the MK_ bits as the event's, alt from GetAsyncKeyState
    void OnChar(void *hwnd, uint32_t character, int32_t repeat);                       // 0x156260
    void ExitSizeMove(void *hwnd);                                                      // 0x1563f0
    // 0x1564d0 (kind 5, WM_SYSKEYDOWN) and 0x156550 (kind 6, WM_KEYUP / WM_SYSKEYUP), one body each in the original
    // (thiscall, 5 arguments; the third, 1 / 0 for down / up, unused).
    void OnKey(uint32_t kind, uint32_t key, int32_t repeat, uint32_t keyFlags);
    // 0x1565d0 (kind 7, left), 0x1568c0 (8, right) and 0x1566c0 (9, middle): a button pressed or double-clicked.
    void OnButtonDown(uint32_t kind, void *hwnd, uint32_t doubleClick, int32_t x, int32_t y, uint32_t buttons);
    void OnButtonUp(uint32_t kind, uint32_t buttons, int32_t lParam); // the three inlined button-up cases
    void TrackMouseLeave(void *hwnd);                                                   // 0x1567b0
    // 0x155710 (stdcall; EnumDisplayMonitors' callback in WM_PAINT): every monitor but data[0] filled with the brush
    // data[1].
    static int32_t __stdcall FillOtherMonitors(void *monitor, void *dc, void *rect, int32_t data);
};

} // namespace nGZGraphic4
