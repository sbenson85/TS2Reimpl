#pragma once
// cTSMainToolbar (lane JE): the in-lot main toolbar service (system service id 0xe92db716, priority 0; class 0x2bf7a2ff
// registered by the cTSUICOMDirector constructor with the creator 0x61e0e9). 0x130 bytes, built for size (/O1). It keeps
// the lot's control state the toolbar shows and the game reads back: live / build / ceiling mode (+0x2c), the game
// speed and pause state (+0x50 / +0x54 / +0x70, posted as messages 0x4b46aebf / 0xcbccdece), a pause counter (+0x4c),
// the zoom step (+0x6c, 1..7), the panel mode (+0x58), the toolbar's window objects (counted, +0x8c.. / +0xd8..), the
// message ids it listens to (+0x40), objects waiting for the next tick (+0x80) and the help text (+0x104).
//
// Faces (RTTI order): +0 cRZSystemService / cIGZSystemService (vtable 0xde1cd4, 13 slots), +4 cRZUnknownMT (0xde1d0c, 6),
// +0x1c cITSMainToolbar (0xde1d28, 94 slots), +0x20 cIGZMessageTarget (0xde1ea4, 4), +0x24 cIGZMessageTarget2 (0xde1eb8,
// 4), +0x28 cIGZSerializable (0xde1ecc, 6). The original's slot bodies take `this` as their face and reach the fields at
// face-relative offsets; ours are members (the offsets below are the object's).
//
// Standalone shape (docs/standalone-shape.md): the creator 0x61e0e9 (`new`, the cITSMainToolbar face returned) is the only
// caller of the constructor 0x619068; both are hooked. The vtables are referenced only by the constructor and the
// destructor 0x61a06b, so every object carries ours. Slots still game code forward to their originals (the original called
// on the slot's face, every argument and result a whole dword); their RVAs are the constructor entry's original_calls.
#include "gz/cIGZMessageTarget.h"
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <list>
#include <set>

struct RZUnknownSlot;

// cGZMessage and cIGZMessageTarget (the old-style message and its receiver): gz/cIGZMessageTarget.h (lane KU, with the
// message server that delivers them).

// cITSMainToolbar (iid 0x292db38c): slots 3-93. Slots still game code take and return whole dwords.
class cITSMainToolbar : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x292db38c;

    virtual bool Init() = 0;                                  // 3 (thunk 0x61e6d3 to the service's Init)
    virtual void MarkModified() = 0;                          // 4 0x61fed0: +0x31 = 1
    virtual bool IsModified() = 0;                            // 5 0x61b94c
    virtual bool Flag32() = 0;                                // 6 folded 0x577030 (byte +0x32)
    virtual bool SaveTo(uint32_t stream) = 0;                 // 7 0x6208e5: the persist system's slot 72
    virtual bool LoadFrom(uint32_t stream) = 0;               // 8 0x61fddd: the persist system's slot 73
    virtual void ResetGameState() = 0;                        // 9 0x61e6db
    virtual void SetWindow78(void *window) = 0;               // 10 folded 0x230ec0 (+0x78, not counted)
    virtual void *Window78() = 0;                             // 11 folded 0x229b60
    virtual void SetController(cIGZUnknown *controller) = 0;  // 12 0x621b33
    virtual uint32_t ControllerSlot15() = 0;                  // 13 0x61e388
    virtual void *UCPState() = 0;                             // 14 0x61e32f
    virtual uint32_t Window78Value(uint32_t key) = 0;         // 15 0x61e248
    virtual void SetGameSpeed(int32_t speed, bool fromUser, bool sound) = 0; // 16 0x62190a
    virtual void SetPaused(bool paused, bool sound) = 0;      // 17 0x6217a6
    virtual void PushPause() = 0;                             // 18 0x61fe0f
    virtual void PopPause() = 0;                              // 19 0x622462
    virtual bool PausePushed() = 0;                           // 20 0x61fc1e
    virtual void TogglePauseBy(bool paused) = 0;              // 21 0x620bd9
    virtual void Nothing22(uint32_t unused) = 0;              // 22 folded 0x300a0
    virtual int32_t PanelMode() = 0;                          // 23 folded 0x1674b0 (+0x58)
    virtual uint32_t Word5C() = 0;                            // 24 folded 0x1674c0 (+0x5c)
    virtual uint32_t Slot25(uint32_t a, uint32_t b) = 0;      // 25 0x621006 (game code)
    virtual int32_t Word50() = 0;                             // 26 folded 0x147800 (+0x50)
    virtual uint32_t Slot27(uint32_t a) = 0;                  // 27 0x620c7e (game code)
    virtual int32_t Word54() = 0;                             // 28 folded 0x1694c0 (+0x54)
    virtual uint32_t Slot29(uint32_t a) = 0;                  // 29 0x621ba2 (game code)
    virtual void SetWordB4(uint16_t value) = 0;               // 30 0x620f58
    virtual uint16_t WordB4() = 0;                            // 31 0x61e017
    virtual void NotifyWindow78(uint8_t value) = 0;           // 32 0x61dfc9
    virtual void SetFlag9F(bool value) = 0;                   // 33 0x621a9c
    virtual bool Flag9F() = 0;                                // 34 0x62245b
    virtual bool FlagA0() = 0;                                // 35 folded 0x6208de
    virtual void *Window90() = 0;                             // 36 folded 0x251bf0
    virtual uint32_t Slot37(uint32_t a) = 0;                  // 37 0x61b1d5 (game code)
    virtual uint32_t Slot38(uint32_t a) = 0;                  // 38 0x61b36c (game code)
    virtual void FocusSelected() = 0;                         // 39 0x61b33c
    virtual void Slot40(uint32_t flag) = 0;                   // 40 0x61b09a (a bool whose whole word is passed on)
    virtual void Slot41(void *object, uint32_t flag) = 0;     // 41 0x61b120 (likewise)
    virtual void SelectedSlot41() = 0;                        // 42 0x620997
    virtual uint32_t WordA4() = 0;                            // 43 0x61e1bd
    virtual void ShowWindowDC(uint32_t show) = 0;             // 44 0x622301
    virtual cRZString *HelpText() = 0;                        // 45 0x61b909
    virtual void *Window98() = 0;                             // 46 folded 0x24a310
    virtual void *Window94() = 0;                             // 47 folded 0x24bdc0
    virtual void PanelSelect48(bool on) = 0;                  // 48 0x61dfe6
    virtual void PanelSelect49(uint32_t id, bool on) = 0;     // 49 0x6209c2
    virtual void PanelSlot50(uint32_t a, uint32_t b) = 0;     // 50 0x620915
    virtual void ToggleFlag9D(bool value) = 0;                // 51 0x621a6b
    virtual void SetFlagAC(bool value) = 0;                   // 52 folded 0x41a700
    virtual void SetFlagAD(bool value) = 0;                   // 53 folded 0x41a820
    virtual bool Flag9C() = 0;                                // 54 folded 0x61e010
    virtual bool Flag9DClear() = 0;                           // 55 0x622450
    virtual bool Flag9E() = 0;                                // 56 0x621f49
    virtual bool FlagAC() = 0;                                // 57 folded 0x418a90
    virtual bool FlagAD() = 0;                                // 58 0x62026e
    virtual void SetValueB0(float value) = 0;                 // 59 0x620ff5
    virtual bool ValueB0(float *out) = 0;                     // 60 0x61e4d0
    virtual void PanelSlot61(uint32_t a, uint32_t b) = 0;     // 61 0x620926
    virtual void PanelSelect62(uint32_t id) = 0;              // 62 0x6201dd
    virtual void SetWord64(int32_t value) = 0;                // 63 0x621cf5
    virtual int32_t Word64() = 0;                             // 64 folded 0x145520
    virtual int32_t Word68() = 0;                             // 65 folded 0x169f60
    virtual int32_t Zoom() = 0;                               // 66 folded 0x1e0950 (+0x6c)
    virtual void SetZoom(int32_t value) = 0;                  // 67 folded 0x1e5370
    virtual void StepZoom(int32_t delta) = 0;                 // 68 0x61b59f
    virtual void ClockSlot69() = 0;                           // 69 0x61b910
    virtual void SetBuildMode(bool on) = 0;                   // 70 0x621aa9
    virtual bool IsBuildMode() = 0;                           // 71 0x5bb9f1
    virtual void SetCeilingMode(bool on) = 0;                 // 72 0x620c03
    virtual bool IsCeilingMode() = 0;                         // 73 folded 0x61e327
    virtual bool Flag129Clear() = 0;                          // 74 0x61e2fd
    virtual void Window90Setup(uint32_t a, uint32_t b) = 0;   // 75 0x620937
    virtual uint32_t Slot76(uint32_t a, uint32_t b, uint32_t c) = 0; // 76 0x61fc26 (game code)
    virtual void Window90Key(uint32_t key) = 0;               // 77 0x620448
    virtual void Window90Bind() = 0;                          // 78 0x61abfd
    virtual void Window90Unbind() = 0;                        // 79 0x6204bf
    virtual uint32_t ControllerSlot15Unchecked() = 0;         // 80 0x622554
    virtual uint32_t RunPaused(void *object, bool viaService, uint32_t fallback) = 0; // 81 0x61df34
    virtual uint32_t Slot82(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) = 0; // 82 0x61fed5 (game code)
    virtual void *WindowB8() = 0;                             // 83 folded 0x24b400
    virtual void *WindowBCPart() = 0;                         // 84 0x61e1aa
    virtual void *WindowD8() = 0;                             // 85 folded 0x24bdb0
    virtual RZUnknownSlot *SetWindowD8(void *window) = 0;     // 86 0x62179b
    virtual bool Flag125() = 0;                               // 87 0x61af51
    virtual void *WindowE0() = 0;                             // 88 folded 0x475440
    virtual RZUnknownSlot *SetWindowE0(void *window) = 0;     // 89 0x621772
    virtual bool Flag127() = 0;                               // 90 0x621f42
    virtual bool FlagAB() = 0;                                // 91 folded 0x418bc0
    virtual bool Flag128() = 0;                               // 92 0x61fc17
    virtual void PostRefresh(uint32_t unused) = 0;            // 93 0x6200be (ret 4: one dword, never read)
};

using ToolbarRef = cRZAutoRefCount<cIGZUnknown>;

// A cRZString member (the hook-shaped string, built and destroyed by hand as the original's inlined code does).
struct ToolbarString {
    union {
        cRZString s;
    };
    ToolbarString() { s.Construct(); }
    ToolbarString(const ToolbarString &) = delete;
    ToolbarString &operator=(const ToolbarString &) = delete;
    ~ToolbarString() { s.Destruct(); }
};
static_assert(sizeof(ToolbarString) == 0x20, "cRZString");

// The stopwatch member (constructed with mode 4 by the constructor; its destructor does nothing).
struct ToolbarStopwatch {
    union {
        cRZStopwatch w;
    };
    ToolbarStopwatch() { w.Construct(4); }
    ToolbarStopwatch(const ToolbarStopwatch &) = delete;
    ToolbarStopwatch &operator=(const ToolbarStopwatch &) = delete;
    ~ToolbarStopwatch() {}
};
static_assert(sizeof(ToolbarStopwatch) == 0x18, "cRZStopwatch");

class cTSMainToolbar : public rz::cRZSystemService,
                       public cITSMainToolbar,
                       public cIGZMessageTarget,
                       public cIGZMessageTarget2,
                       public cIGZSerializable {
public:
    static constexpr uint32_t kVTable = 0xde1cd4;
    static constexpr uint32_t kVTable4 = 0xde1d0c;
    static constexpr uint32_t kToolbarVTable = 0xde1d28;
    static constexpr uint32_t kMessageVTable = 0xde1ea4;
    static constexpr uint32_t kMessage2VTable = 0xde1eb8;
    static constexpr uint32_t kSerialVTable = 0xde1ecc;
    static constexpr uint32_t kSize = 0x130;
    static constexpr uint32_t kServiceId = 0xe92db716;
    static constexpr uint32_t kSerialIid = 0xe4fda3d4;
    static constexpr uint32_t kClassId = 0x8b0c79d6; // GetGZCLSID

    cTSMainToolbar();                       // 0x619068
    ~cTSMainToolbar() noexcept override;    // 0x61a06b; deleting 0x61aa67 (+4 thunk 0x61aa20)
    cTSMainToolbar(const cTSMainToolbar &) = delete;
    cTSMainToolbar &operator=(const cTSMainToolbar &) = delete;

    // 0x61e0e9 (__cdecl): the COM director's creator: a new service, its cITSMainToolbar face (null when new failed).
    static cITSMainToolbar *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x620275; thunks 0x6202b2 / ba / c2 / ca / d2
    uint32_t AddRef() override;                              // folded 0x1cc320; thunks 0x61acba, 0x61acc2 and folded
    uint32_t Release() override;                             // folded 0x1cd7a0; thunks 0x6204af, 0x6204b7 and folded

    // ---- cIGZSystemService ----
    bool Init() override;                // 0x61e515 (also cITSMainToolbar slot 3)
    bool Shutdown() override;            // 0x62232a
    bool OnTick(uint32_t counter) override; // 0x62013a

    // ---- cITSMainToolbar (+0x1c) ----
    void MarkModified() override;
    bool IsModified() override;
    bool Flag32() override;
    bool SaveTo(uint32_t stream) override;
    bool LoadFrom(uint32_t stream) override;
    void ResetGameState() override;
    void SetWindow78(void *window) override;
    void *Window78() override;
    void SetController(cIGZUnknown *controller) override;
    uint32_t ControllerSlot15() override;
    void *UCPState() override;
    uint32_t Window78Value(uint32_t key) override;
    void SetGameSpeed(int32_t speed, bool fromUser, bool sound) override;
    void SetPaused(bool paused, bool sound) override;
    void PushPause() override;
    void PopPause() override;
    bool PausePushed() override;
    void TogglePauseBy(bool paused) override;
    void Nothing22(uint32_t unused) override;
    int32_t PanelMode() override;
    uint32_t Word5C() override;
    uint32_t Slot25(uint32_t a, uint32_t b) override;
    int32_t Word50() override;
    uint32_t Slot27(uint32_t a) override;
    int32_t Word54() override;
    uint32_t Slot29(uint32_t a) override;
    void SetWordB4(uint16_t value) override;
    uint16_t WordB4() override;
    void NotifyWindow78(uint8_t value) override;
    void SetFlag9F(bool value) override;
    bool Flag9F() override;
    bool FlagA0() override;
    void *Window90() override;
    uint32_t Slot37(uint32_t a) override;
    uint32_t Slot38(uint32_t a) override;
    void FocusSelected() override;
    void Slot40(uint32_t flag) override;
    void Slot41(void *object, uint32_t flag) override;
    void SelectedSlot41() override;
    uint32_t WordA4() override;
    void ShowWindowDC(uint32_t show) override;
    cRZString *HelpText() override;
    void *Window98() override;
    void *Window94() override;
    void PanelSelect48(bool on) override;
    void PanelSelect49(uint32_t id, bool on) override;
    void PanelSlot50(uint32_t a, uint32_t b) override;
    void ToggleFlag9D(bool value) override;
    void SetFlagAC(bool value) override;
    void SetFlagAD(bool value) override;
    bool Flag9C() override;
    bool Flag9DClear() override;
    bool Flag9E() override;
    bool FlagAC() override;
    bool FlagAD() override;
    void SetValueB0(float value) override;
    bool ValueB0(float *out) override;
    void PanelSlot61(uint32_t a, uint32_t b) override;
    void PanelSelect62(uint32_t id) override;
    void SetWord64(int32_t value) override;
    int32_t Word64() override;
    int32_t Word68() override;
    int32_t Zoom() override;
    void SetZoom(int32_t value) override;
    void StepZoom(int32_t delta) override;
    void ClockSlot69() override;
    void SetBuildMode(bool on) override;
    bool IsBuildMode() override;
    void SetCeilingMode(bool on) override;
    bool IsCeilingMode() override;
    bool Flag129Clear() override;
    void Window90Setup(uint32_t a, uint32_t b) override;
    uint32_t Slot76(uint32_t a, uint32_t b, uint32_t c) override;
    void Window90Key(uint32_t key) override;
    void Window90Bind() override;
    void Window90Unbind() override;
    uint32_t ControllerSlot15Unchecked() override;
    uint32_t RunPaused(void *object, bool viaService, uint32_t fallback) override;
    uint32_t Slot82(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) override;
    void *WindowB8() override;
    void *WindowBCPart() override;
    void *WindowD8() override;
    RZUnknownSlot *SetWindowD8(void *window) override;
    bool Flag125() override;
    void *WindowE0() override;
    RZUnknownSlot *SetWindowE0(void *window) override;
    bool Flag127() override;
    bool FlagAB() override;
    bool Flag128() override;
    void PostRefresh(uint32_t unused) override;

    // ---- cIGZMessageTarget (+0x20) / cIGZMessageTarget2 (+0x24) ----
    bool DoMessage(cGZMessage *message) override; // 0x61b99c
    bool DoMessage(void *message) override;       // 0x61bc58

    // ---- cIGZSerializable (+0x28) ----
    bool Write(ifaces::cIGZOStream *stream) override; // 0x62255c
    bool Read(ifaces::cIGZIStream *stream) override;  // 0x6202da
    uint32_t GetGZCLSID() override;                   // 0x61e129

    // The saved state the "modified" test compares against (Write / Read / the constructor).
    void Snapshot();

    // +0x00 .. +0x1b: cRZSystemService; +0x1c .. +0x28: the faces' vtables
    int32_t mMode;                  // 0x2c 0 live, 1 build, 2 ceiling
    uint8_t mByte30;                // 0x30
    bool mModified;                 // 0x31 slot 4
    bool mFlag32;                   // 0x32
    bool mFlag33;                   // 0x33
    bool mSavedPaused;              // 0x34 Snapshot: +0x70
    int32_t mSavedWord50;           // 0x38 Snapshot: +0x50
    int32_t mSavedWord54;           // 0x3c Snapshot: +0x54
    std::set<uint32_t> mMessageIds; // 0x40 the message ids the service registers for (Init) and drops (Shutdown)
    int32_t mWord48;                // 0x48
    int32_t mPauseCount;            // 0x4c slots 18 / 19
    int32_t mWord50;                // 0x50
    int32_t mWord54;                // 0x54 2 at start (1, 2, 3: slot 29)
    int32_t mPanelMode;             // 0x58 3, 4, 0xb select the panel object's routes
    uint32_t mWord5C;               // 0x5c
    uint32_t mWord60;               // 0x60 never written by the constructor
    int32_t mWord64;                // 0x64
    int32_t mWord68;                // 0x68 2 at start
    int32_t mZoom;                  // 0x6c 4 at start, 1..7
    bool mPaused;                   // 0x70 1 at start
    uint8_t mPad71[3];              // 0x71 never written
    void *mPanel;                   // 0x74 the panel object (helpers 0x61b843, 0x620643, 0x61b61e, 0x61b75f)
    void *mWindow78;                // 0x78 not counted
    bool mTickPending;              // 0x7c OnTick walks +0x80 when set
    uint8_t mPad7D[3];              // 0x7d
    std::list<ToolbarRef> mTickObjects; // 0x80 counted objects for the next tick
    bool mPauseToggle;              // 0x88 slot 21
    uint8_t mPad89[3];              // 0x89
    ToolbarRef mController;         // 0x8c slot 12
    ToolbarRef mWindow90;           // 0x90
    ToolbarRef mWindow94;           // 0x94
    ToolbarRef mWindow98;           // 0x98
    bool mFlag9C;                   // 0x9c 1 at start
    bool mFlag9D;                   // 0x9d 1 at start, slot 51
    bool mFlag9E;                   // 0x9e 1 at start
    bool mFlag9F;                   // 0x9f
    bool mFlagA0;                   // 0xa0
    uint8_t mPadA1[3];              // 0xa1 never written
    uint32_t mWordA4;               // 0xa4 never written by the constructor (slot 43)
    bool mFlagA8;                   // 0xa8
    bool mFlagA9;                   // 0xa9 1 at start
    bool mFlagAA;                   // 0xaa 1 at start (slot 21 does nothing while set)
    bool mBusy;                     // 0xab slot 81's re-entry guard
    bool mFlagAC;                   // 0xac never written by the constructor
    bool mFlagAD;                   // 0xad
    uint8_t mPadAE[2];              // 0xae never written
    float mValueB0;                 // 0xb0
    uint16_t mWordB4;               // 0xb4 1 at start
    uint8_t mPadB6[2];              // 0xb6 never written
    ToolbarRef mWindowB8;           // 0xb8
    ToolbarRef mWindowBC;           // 0xbc
    ToolbarStopwatch mStopwatch;    // 0xc0 mode 0
    ToolbarRef mWindowD8;           // 0xd8
    ToolbarRef mWindowDC;           // 0xdc
    ToolbarRef mWindowE0;           // 0xe0
    ToolbarRef mUCPState;           // 0xe4 slot 14
    ToolbarRef mWindowE8;           // 0xe8
    ToolbarRef mWindowEC;           // 0xec
    ToolbarRef mWindowF0;           // 0xf0
    ToolbarRef mWindowF4;           // 0xf4
    ToolbarRef mWindowF8;           // 0xf8
    ToolbarRef mWindowFC;           // 0xfc
    ToolbarRef mWindow100;          // 0x100
    ToolbarString mHelpText;        // 0x104 the text 0x7fa7dc7c (Init)
    bool mFlag124;                  // 0x124 1 at start
    bool mFlag125;                  // 0x125
    bool mFlag126;                  // 0x126
    bool mFlag127;                  // 0x127 1 at start
    bool mFlag128;                  // 0x128
    bool mFlag129;                  // 0x129
    uint8_t mPad12A[6];             // 0x12a never written
};

static_assert(sizeof(cTSMainToolbar) == cTSMainToolbar::kSize, "cTSMainToolbar size");
static_assert(offsetof(cTSMainToolbar, mMode) == 0x2c, "mMode");
static_assert(offsetof(cTSMainToolbar, mSavedPaused) == 0x34, "mSavedPaused");
static_assert(offsetof(cTSMainToolbar, mMessageIds) == 0x40, "mMessageIds");
static_assert(offsetof(cTSMainToolbar, mWord48) == 0x48, "mWord48");
static_assert(offsetof(cTSMainToolbar, mPaused) == 0x70, "mPaused");
static_assert(offsetof(cTSMainToolbar, mPanel) == 0x74, "mPanel");
static_assert(offsetof(cTSMainToolbar, mTickObjects) == 0x80, "mTickObjects");
static_assert(offsetof(cTSMainToolbar, mController) == 0x8c, "mController");
static_assert(offsetof(cTSMainToolbar, mFlag9C) == 0x9c, "mFlag9C");
static_assert(offsetof(cTSMainToolbar, mWordA4) == 0xa4, "mWordA4");
static_assert(offsetof(cTSMainToolbar, mValueB0) == 0xb0, "mValueB0");
static_assert(offsetof(cTSMainToolbar, mWindowB8) == 0xb8, "mWindowB8");
static_assert(offsetof(cTSMainToolbar, mStopwatch) == 0xc0, "mStopwatch");
static_assert(offsetof(cTSMainToolbar, mWindowD8) == 0xd8, "mWindowD8");
static_assert(offsetof(cTSMainToolbar, mWindow100) == 0x100, "mWindow100");
static_assert(offsetof(cTSMainToolbar, mHelpText) == 0x104, "mHelpText");
static_assert(offsetof(cTSMainToolbar, mFlag124) == 0x124, "mFlag124");
static_assert(offsetof(cTSMainToolbar, mPad12A) == 0x12a, "mPad12A");

// The hooks' entry points: the constructor (placement new of ours); slot 71, which cTSWinSceneGraph's slots 162 / 163 call
// directly on the toolbar face (our member, called non-virtually).
struct cTSMainToolbarAdapters {
    static cTSMainToolbar *__fastcall Construct(void *memory, void *edx); // 0x619068
    static bool __fastcall IsBuildMode(cITSMainToolbar *face, void *edx); // 0x5bb9f1
};
