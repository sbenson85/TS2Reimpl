#pragma once
// nGZGraphic4::cGZCanvas (RTTI 0x10951c4; vftables 0xd9c080, 16 slots, 0xd9c0c4 at +8, 23 slots, 0xd9c124 at +0xc,
// 7 slots, 0xd9c144 at +0x10, 4 slots, and 0xd9c158 at +0x14, 13 slots; 0x30 bytes) and the messages it sends:
// cGZCanvasMessage (RTTI 0x10951e8, vftable 0xd9c190, 0x10 bytes), cGZMouseMessage (0x1095214, 0xd9c1c0, 0x20) and
// cGZKeyMessage (0x1095240, 0xd9c1fc, 0x1c): lane NQ, rendering campaign R2b step 3.
//
// The canvas is the framework's service 0xa3460deb (cIGZCanvas, iid 0x66b217a1, at +8): the GZ face of the game window
// (the window service 0xfb6372ee, cCanvasW32). Init holds the message server and the window and listens to
// cGraphicSystemDX's device events and the window's canvas, mouse and key events; OnEvent turns each into a message
// (taken from the message class's fixed-size pool) and sends it through the message server. Its other slots forward
// to the window (size, title, show, the HWND).
//
// Standalone shape (docs/standalone-shape.md): the creator 0x1495f0 (the graphics COM director's class 0x445fabbb) is
// hooked to Create and is the switch. It is the only code storing the canvas's vtables (the deleting destructor 0x148e20
// the other); the messages' vtables are stored only by the canvas's OnEvent, the mouse message's constructor 0x148da0
// (called from there) and the messages' own slot 3 factories, so every slot of all four classes is in effect through
// callers. The pools are owned statics (0xfeb660 / 0xfeb670 / 0xfeb680); their allocator 0x149040 (shared with other
// pools) is ours, hooked at its RVA (lane RB).
#include "gz/cAbstractBehaviorTypeMT.h"
#include "gz/cIGZSystemService.h"

#include <cstddef>
#include <cstdint>

class cIGZString;

#include "game/owned.h"
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10c9500); // GetEventManager's cache
TS2_OWNED_STATIC(const nGZEvent::cEventAttribute, feb410); // slot 14's record for an unknown event

namespace nGZGraphic4 {

// ---- the messages ----

// A message class's pool (the game's static: element size, block size, the block list, the free list). Allocation is
// 0x149040 (ours, hooked: other pools share it); a freed message goes back on the free list.
struct cGZMessagePool {
    uint32_t mElementSize; // +0x0
    uint32_t mBlockSize;   // +0x4
    void *mBlocks;         // +0x8: each block's first word links the previous one
    void *mFree;           // +0xc: each free element's first word links the next one

    void *Allocate();      // 0x149040
    void Free(void *element)
    {
        *static_cast<void **>(element) = mFree;
        mFree = element;
    }
};

constexpr uint32_t kCanvasMessagePool = 0xfeb660, kMouseMessagePool = 0xfeb670, kKeyMessagePool = 0xfeb680;

} // namespace nGZGraphic4

// The pools (kCanvasMessagePool and the others) and slot 22's HWND cell.
TS2_OWNED_STATIC(nGZGraphic4::cGZMessagePool, feb660);
TS2_OWNED_STATIC(nGZGraphic4::cGZMessagePool, feb670);
TS2_OWNED_STATIC(nGZGraphic4::cGZMessagePool, feb680);
TS2_OWNED_STATIC(void *, 10c9570);

namespace nGZGraphic4 {

// cIGZMessage2 (slots 0-9): the type at +8, a word at +0xc.
class cIGZMessage2 : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xa52294b4;

    virtual bool Create(uint32_t iid, void **out) = 0;      // 3: a fresh message of this class as `iid`
    virtual uint32_t GetType() = 0;                         // 4 (shared 0x46390)
    virtual void SetType(uint32_t type) = 0;                // 5 (shared 0x14a260)
    virtual bool IsType(cIGZMessage2 *other) = 0;           // 6 (shared 0x148de0): the same type as `other`'s
    virtual bool IsTypeBelow(cIGZMessage2 *other) = 0;      // 7 (shared 0x148e00): a type below `other`'s
};

// The counted part every message shares (the unlocked count of shared 0x148fb0; the last Release frees the message
// through its slot `DeletingSlot`).
template <class Iface> class cGZMessageT : public Iface {
public:
    uint32_t AddRef() override { return ++mRefCount; } // shared 0x148fb0
    // 0x14a1d0 / shared 0x14a1f0: the last reference (a count of 0 or 1) frees the message, the count left as it was.
    uint32_t Release() override
    {
        if (mRefCount > 1) return --mRefCount;
        this->Destroy(1);
        return 0;
    }
    uint32_t GetType() override { return mType; }
    void SetType(uint32_t type) override { mType = type; }
    bool IsType(cIGZMessage2 *other) override { return mType == other->GetType(); }
    bool IsTypeBelow(cIGZMessage2 *other) override { return mType < other->GetType(); }

    uint32_t mRefCount = 0;  // +0x4
    uint32_t mType;          // +0x8
};

// cIGZCanvasMessage (iid 0xda1062fa): the canvas state code at +0xc.
class cIGZCanvasMessage : public cIGZMessage2 {
public:
    static constexpr uint32_t kIid = 0xda1062fa;
    virtual void SetCode(uint32_t code) = 0; // 8 (shared 0x149c60)
    virtual uint32_t GetCode() = 0;          // 9 (shared 0x145770)
    virtual void Destroy(uint32_t flags) = 0; // 10: the deleting destructor (0x148ec0)
};

class cGZCanvasMessage : public cGZMessageT<cIGZCanvasMessage> {
public:
    static constexpr size_t kSize = 0x10;
    static constexpr uint32_t kType = 0x1a10630a;
    // The codes the canvas sends.
    static constexpr uint32_t kSizeChanged = 1, kMinimized = 2, kRestored = 3, kRenderScene = 4, kDeviceReady = 5;

    cGZCanvasMessage() { mType = kType; }
    static cGZCanvasMessage *New(); // from the pool, uncounted

    bool QueryInterface(uint32_t riid, void **out) override; // 0x149e60
    // Release: the template's (0x14a1d0)
    bool Create(uint32_t iid, void **out) override;          // 0x149470
    void SetCode(uint32_t code) override { mCode = code; }
    uint32_t GetCode() override { return mCode; }
    void Destroy(uint32_t flags) override;                   // 0x148ec0 (eax-ignored: the original leaves this in eax; kept from the lane that wrote the class, callers indirect, none known to read it)

    int32_t mCode = -1; // +0xc
};

// cIGZMouseMessage (iid 0x1a0c615f): the button / event code, the position, two more words.
struct cGZMousePosition {
    int32_t mX, mY;
};

class cIGZMouseMessage : public cIGZMessage2 {
public:
    static constexpr uint32_t kIid = 0x1a0c615f;
    virtual void Set(uint32_t code, const cGZMousePosition *position, uint32_t keys, uint32_t wheel) = 0; // 8 (0x149c90)
    virtual uint32_t GetCode() = 0;                     // 9 (shared 0x145770)
    virtual cGZMousePosition *GetPosition(cGZMousePosition *out) = 0; // 10 (0x149d60, a struct return)
    virtual uint32_t GetWheel() = 0;                    // 11 (shared 0x148ba0, +0x18)
    virtual uint32_t GetKeys() = 0;                     // 12 (shared 0x147d00, +0x1c)
    virtual void Destroy(uint32_t flags) = 0;           // 13: the deleting destructor (0x148f40)
};

class cGZMouseMessage : public cGZMessageT<cIGZMouseMessage> {
public:
    static constexpr size_t kSize = 0x20;
    static constexpr uint32_t kType = 0xfa0c6390;

    cGZMouseMessage() { mType = kType; } // 0x148da0
    static cGZMouseMessage *New();

    bool QueryInterface(uint32_t riid, void **out) override; // 0x149ec0
    // Release: the template's (shared 0x14a1f0)
    bool Create(uint32_t iid, void **out) override;          // 0x149560
    void Set(uint32_t code, const cGZMousePosition *position, uint32_t keys, uint32_t wheel) override;
    uint32_t GetCode() override { return mCode; }
    cGZMousePosition *GetPosition(cGZMousePosition *out) override
    {
        *out = mPosition;
        return out;
    }
    uint32_t GetWheel() override { return mWheel; }
    uint32_t GetKeys() override { return mKeys; }
    void Destroy(uint32_t flags) override;                   // 0x148f40 (eax-ignored: as the message Destroy above)

    int32_t mCode = -1;               // +0x0c
    cGZMousePosition mPosition{0, 0}; // +0x10
    uint32_t mWheel = 0;              // +0x18
    uint32_t mKeys = 0;               // +0x1c
};

// cIGZKeyMessage (iid 0x1a104742): four words.
class cIGZKeyMessage : public cIGZMessage2 {
public:
    static constexpr uint32_t kIid = 0x1a104742;
    virtual void Set(uint32_t code, uint32_t key, uint32_t modifiers, uint32_t repeat) = 0; // 8 (shared 0x149c70)
    virtual uint32_t GetCode() = 0;       // 9 (shared 0x145770)
    virtual uint32_t GetKey() = 0;        // 10 (shared 0x149cf0)
    virtual uint32_t GetModifiers() = 0;  // 11 (shared 0x149d00)
    virtual uint32_t GetRepeat() = 0;     // 12 (shared 0x148ba0)
    virtual void Destroy(uint32_t flags) = 0; // 13: the deleting destructor (0x148f00)
};

class cGZKeyMessage : public cGZMessageT<cIGZKeyMessage> {
public:
    static constexpr size_t kSize = 0x1c;
    static constexpr uint32_t kType = 0x7a104750;

    cGZKeyMessage() { mType = kType; }
    static cGZKeyMessage *New();

    bool QueryInterface(uint32_t riid, void **out) override; // 0x149e90
    // Release: the template's (shared 0x14a1f0)
    bool Create(uint32_t iid, void **out) override;          // 0x1494e0
    void Set(uint32_t code, uint32_t key, uint32_t modifiers, uint32_t repeat) override
    {
        mCode = code;
        mKey = key;
        mModifiers = modifiers;
        mRepeat = repeat;
    }
    uint32_t GetCode() override { return mCode; }
    uint32_t GetKey() override { return mKey; }
    uint32_t GetModifiers() override { return mModifiers; }
    uint32_t GetRepeat() override { return mRepeat; }
    void Destroy(uint32_t flags) override;                   // 0x148f00 (eax-ignored: as the message Destroy above)

    int32_t mCode = -1;     // +0x0c
    uint32_t mKey = 0;      // +0x10
    uint32_t mModifiers = 0; // +0x14
    uint32_t mRepeat;       // +0x18: left as the pool gave it until Set
};

// ---- the canvas ----

class cGZCanvasBehavior : public nGZEvent::cIAbstractBehavior {
public:
    virtual ~cGZCanvasBehavior() = default; // 15: the deleting destructor
};

// The window's rectangle as slot 12 returns it (the window's slot 29).
struct cGZCanvasRect {
    int32_t mLeft, mTop, mRight, mBottom;
};

// cIGZCanvas (iid 0x66b217a1, 23 slots).
class cIGZCanvas : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x66b217a1;

    virtual bool CanvasSlot3(uint32_t, uint32_t, uint32_t) = 0;      // 3 (shared 0x147a70): true
    virtual bool CanvasSlot4(uint32_t) = 0;                          // 4 (shared 0x148260): true
    virtual bool IsConnected() = 0;                            // 5 (0x149cd0): Init connected it
    virtual bool Shutdown() = 0;                               // 6 (0x14a280): the service's Shutdown
    virtual bool CanvasSlot7() = 0;                                  // 7 (shared 0x144910): true
    virtual bool CanvasSlot8() = 0;                                  // 8 (shared 0x144910): true
    virtual bool IsMaximized() = 0;                            // 9 (0x149ce0): the window's slot 34
    virtual bool IsCaptionless() = 0;                          // 10 (0x149cc0): the window's slot 35
    virtual int32_t SetRect(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0; // 11 (0x14a210): SetWindowPos's
    virtual cGZCanvasRect *GetClientRect(cGZCanvasRect *out) = 0; // 12 (0x1496c0): the window's slot 29 (a struct return)
    virtual uint32_t Show() = 0;                               // 13 (0x14a270): the window's slot 15 (-1, 0)
    virtual void CanvasSlot14() = 0;                                 // 14 (shared 0x140fe0)
    virtual uint32_t SetTitle(cIGZString *title) = 0;          // 15 (0x14a240): the window's slot 17
    virtual void CanvasSlot16() = 0;                                 // 16 (shared 0x140fe0)
    virtual void CanvasSlot17() = 0;                                 // 17 (shared 0x140fe0)
    virtual int32_t GetMouseX() = 0;                           // 18 (shared 0x148ba0)
    virtual int32_t GetMouseY() = 0;                           // 19 (shared 0x147d00)
    virtual uint32_t CanvasSlot20() = 0;                             // 20 (shared 0x6c6b0): 0
    virtual bool PostWindowMessage(cIGZUnknown *message) = 0;  // 21 (0x149d80)
    virtual void *GetHwndCell() = 0;                           // 22 (0x1496e0): the HWND, in a static
};

// cIGZCanvasW32 (iid 0xe46397db, 7 slots).
class cIGZCanvasW32 : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xe46397db;
    virtual cIGZCanvas *GetCanvas() = 0;              // 3 (0x1490a0)
    virtual void *GetHwnd() = 0;                      // 4 (0x149700): the window's canvas interface's slot 40
    virtual uint16_t GetSlot41() = 0;                 // 5 (0x149400): its slot 41
    virtual bool Slot43(uint32_t a, uint32_t b) = 0;  // 6 (0x148fc0): its slot 43
};

// cIGZCanvasMessaging (iid 0x3ac7a210, 4 slots).
class cIGZCanvasMessaging : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x3ac7a210;
    virtual cIGZUnknown *GetMessageServer() = 0;      // 3 (shared 0x147d00)
};

struct cGZCanvasBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10c9500; // shared with cGZGraphicSystem (0x1471f0's cache)
    static const nGZEvent::cEventAttribute &MissingAttributeRecord() { return ts2_rva_feb410; }
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10c9500; }
    // 0x149ef0 (cdecl): the registry, built on first use (id 0: nothing stored). Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    static nGZEvent::cEventAttributeMap *Registry();
    static void ResetRegistryForTest();
};

class cGZCanvas : public nGZEvent::cAbstractBehaviorTypeMT<cGZCanvasBehavior, cGZCanvasBehaviorTraits>,
                  public cIGZCanvas,
                  public cIGZCanvasW32,
                  public cIGZCanvasMessaging,
                  public cIGZSystemService {
    using Behavior = nGZEvent::cAbstractBehaviorTypeMT<cGZCanvasBehavior, cGZCanvasBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x30;
    static constexpr uint32_t kServiceId = 0xa3460deb;
    static constexpr int32_t kPriority = 0x16e35e;
    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;
    static constexpr uint32_t kIidBehaviorType = 0xfb859324;

    // The events it listens to.
    static constexpr uint32_t kConnectionChangedEvent = 0x5b51e806;
    static constexpr uint32_t kDeviceStateEvent = 0xca7ccbbc;  // cGraphicSystemDX's
    static constexpr uint32_t kRenderSceneEvent = 0x2b5745f8;  // cGraphicSystemDX's
    static constexpr uint32_t kCanvasEvent = 0x1b637474;       // the window's
    static constexpr uint32_t kMouseEvent = 0x1b637482;        // the window's
    static constexpr uint32_t kKeyEvent = 0x5b63748b;          // the window's

    cGZCanvas();
    ~cGZCanvas() override; // 0x148e20 (slot 15)

    // 0x1495f0 (cdecl; the COM director's creator, hooked): a new canvas (uncounted) as its cIGZCanvas face.
    static cIGZCanvas *__cdecl Create();

    // ---- cIGZUnknown (five faces) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x149de0
    uint32_t AddRef() override;                              // shared 0x148f80 (0x146a20)
    uint32_t Release() override;                             // 0x14a1a0 (0x1487e0)

    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0x149770

    // ---- cIGZCanvas (+8) ----
    bool CanvasSlot3(uint32_t, uint32_t, uint32_t) override;
    bool CanvasSlot4(uint32_t) override;
    bool IsConnected() override;
    bool Shutdown() override; // 0x14a280 (+0x14 thunk 0x14a2c5)
    bool CanvasSlot7() override;
    bool CanvasSlot8() override;
    bool IsMaximized() override;
    bool IsCaptionless() override;
    int32_t SetRect(int32_t left, int32_t top, int32_t right, int32_t bottom) override;
    cGZCanvasRect *GetClientRect(cGZCanvasRect *out) override;
    uint32_t Show() override;
    void CanvasSlot14() override;
    uint32_t SetTitle(cIGZString *title) override;
    void CanvasSlot16() override;
    void CanvasSlot17() override;
    int32_t GetMouseX() override;
    int32_t GetMouseY() override;
    uint32_t CanvasSlot20() override;
    bool PostWindowMessage(cIGZUnknown *message) override;
    void *GetHwndCell() override;

    // ---- cIGZCanvasW32 (+0xc) ----
    cIGZCanvas *GetCanvas() override;
    void *GetHwnd() override;
    uint16_t GetSlot41() override;
    bool Slot43(uint32_t a, uint32_t b) override;

    // ---- cIGZCanvasMessaging (+0x10) ----
    cIGZUnknown *GetMessageServer() override;

    // ---- cIGZSystemService (+0x14) ----
    uint32_t GetServiceID() override;
    void SetServiceID(uint32_t id) override;
    int32_t GetServicePriority() override;
    bool IsServiceRunning() override;
    void SetServiceRunning(bool running) override;
    bool Init() override; // 0x149a90
    bool OnTick(uint32_t counter) override;
    bool OnIdle(uint32_t counter) override;
    int32_t GetServiceTickPriority() override;

    uint32_t mServiceId;          // +0x18
    bool mRunning;                // +0x1c
    bool mActive;                 // +0x1d: Init connected it, Shutdown clears it
    int32_t mMouseX;              // +0x20: the last mouse event's position
    int32_t mMouseY;              // +0x24
    cIGZUnknown *mWindow;         // +0x28: the window service's 0x5b637209 interface (counted)
    cIGZUnknown *mMessageServer;  // +0x2c: the message server (counted)

private:
    void Send(cIGZUnknown *message);
};

} // namespace nGZGraphic4
