// nGZGraphic4::cGZCanvas and its messages (lane NQ): src/gz/cGZCanvas.h.
#include "gz/cGZCanvas.h"
#include "gz/cBehaviorRegistry.h"
#include <iterator>
#include <optional>

#define NOMINMAX
#include <windows.h>

#include "game/game.h"
#include "game/import_slots.h"
#include "game/original.h"
#include "rz/RZMemory.h"
#include "game/vtable.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include <new>

namespace nGZGraphic4 {

namespace {

// Framework services.
constexpr uint32_t kMessageServerId = 0x4fa845b, kMessageServerIid = 0x652294c7;
constexpr uint32_t kWindowServiceId = 0xfb6372ee, kWindowBehaviorIid = 0x5b637209, kWindowCanvasIid = 0xbb6377fa;
constexpr uint32_t kGraphicSystemDXId = 0x85522803, kGraphicSystemDXIid = 0xa55227bb;

// PostWindowMessage's message: type 0x8006 with at least three words (its slot 108: Msg, wParam, lParam).
constexpr uint32_t kWindowMessageType = 0x8006;

constexpr IatSlot kIatSetWindowPos = &ts2_rva_d9a510, kIatPostMessageA = &ts2_rva_d9a570;
constexpr UINT kSetRectFlags = SWP_NOZORDER | SWP_NOACTIVATE; // 0x14

// The deleting destructors' flag that frees nothing (the game's array form; the body it calls does nothing).
constexpr uint32_t kDeleteFlag = 1, kNoFreeFlag = 4;

// A framework service fetched into a counted member (the original's RZAutoRef assignment: the new one counted, stored,
// the old one released, then the fetched reference let go).
void FetchService(cIGZUnknown *&member, uint32_t id, uint32_t iid)
{
    RZAutoRef fetched{nullptr};
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &fetched.mObject);
    cIGZUnknown *const object = static_cast<cIGZUnknown *>(fetched.mObject);
    cIGZUnknown *const old = member;
    if (object != old) {
        if (object) object->AddRef();
        member = object;
        if (old) old->Release();
    }
    fetched.ReleaseObject();
}

// The window's canvas interface (0xbb6377fa), counted for the call.
template <class R, unsigned Slot, class... Args> R CallWindowCanvas(cIGZUnknown *window, Args... args)
{
    void *canvas = nullptr;
    window->QueryInterface(kWindowCanvasIid, &canvas);
    const R result = RawSlot<Slot, R>(canvas, args...);
    if (canvas) static_cast<cIGZUnknown *>(canvas)->Release();
    return result;
}

} // namespace

// ==== the messages ====

// 0x149040: the head of the free list; when it is empty, a new block first (its first word links the block list, the
// elements after it each link the next, the last links nothing). A block holds mBlockSize / mElementSize elements;
// RZMemory::Malloc is asked for 8 bytes over mBlockSize (only 4 are the link).
void *cGZMessagePool::Allocate()
{
    if (!mFree) {
        void **const block = static_cast<void **>(RZMemory::Malloc(mBlockSize + 8));
        *block = mBlocks;
        mBlocks = block;
        uint8_t *const first = reinterpret_cast<uint8_t *>(block + 1);
        uint8_t *const last = first + (mBlockSize / mElementSize - 1) * mElementSize;
        for (uint8_t *element = first; element < last; element += mElementSize)
            *reinterpret_cast<void **>(element) = element + mElementSize;
        *reinterpret_cast<void **>(last) = nullptr;
        mFree = first;
    }
    void *const element = mFree;
    mFree = *static_cast<void **>(element);
    return element;
}

namespace {
// A message created by a message's slot 3: as `iid`, else freed.
template <class Message> bool CreateAs(uint32_t iid, void **out)
{
    Message *const message = Message::New();
    if (message->QueryInterface(iid, out)) return true;
    message->Destroy(kDeleteFlag);
    return false;
}

template <class Message> void DestroyInPool(Message *message, uint32_t flags, cGZMessagePool &pool)
{
    if (!(flags & kDeleteFlag) || (flags & kNoFreeFlag)) return;
    pool.Free(message);
}

// The messages' QueryInterface: 1, cIGZMessage2 and the class's own interface, counted (no null check).
template <class Message> bool QueryMessage(Message *message, uint32_t riid, uint32_t own, void **out)
{
    if (riid != 1 && riid != own && riid != cIGZMessage2::kIid) return false;
    *out = message;
    message->AddRef();
    return true;
}
} // namespace

cGZCanvasMessage *cGZCanvasMessage::New() { return ::new (ts2_rva_feb660.Allocate()) cGZCanvasMessage; }
bool cGZCanvasMessage::QueryInterface(uint32_t riid, void **out) { return QueryMessage(this, riid, kIid, out); }
bool cGZCanvasMessage::Create(uint32_t iid, void **out) { return CreateAs<cGZCanvasMessage>(iid, out); }
void cGZCanvasMessage::Destroy(uint32_t flags) { DestroyInPool(this, flags, ts2_rva_feb660); }

cGZMouseMessage *cGZMouseMessage::New() { return ::new (ts2_rva_feb670.Allocate()) cGZMouseMessage; }
bool cGZMouseMessage::QueryInterface(uint32_t riid, void **out) { return QueryMessage(this, riid, kIid, out); }
bool cGZMouseMessage::Create(uint32_t iid, void **out) { return CreateAs<cGZMouseMessage>(iid, out); }
void cGZMouseMessage::Destroy(uint32_t flags) { DestroyInPool(this, flags, ts2_rva_feb670); }

void cGZMouseMessage::Set(uint32_t code, const cGZMousePosition *position, uint32_t keys, uint32_t wheel)
{
    mCode = int32_t(code);
    mPosition = *position;
    mWheel = wheel;
    mKeys = keys;
}

cGZKeyMessage *cGZKeyMessage::New() { return ::new (ts2_rva_feb680.Allocate()) cGZKeyMessage; }
bool cGZKeyMessage::QueryInterface(uint32_t riid, void **out) { return QueryMessage(this, riid, kIid, out); }
bool cGZKeyMessage::Create(uint32_t iid, void **out) { return CreateAs<cGZKeyMessage>(iid, out); }
void cGZKeyMessage::Destroy(uint32_t flags) { DestroyInPool(this, flags, ts2_rva_feb680); }

// ==== the canvas ====

namespace {
std::optional<nGZEvent::cEventAttributeMap> gRegistry;
const nGZEvent::ClassEvent kClassEvents[] = {
    {0xca7ccbbc, "CurrentDeviceEvent", 0x3b6c3e0a, 2},
    {0x2b5745f8, "CurrentDeviceRenderSceneEvent", 0xcb573da1, 2},
    {0x1b637474, "CanvasEvent", 0x7b637514, 2},
    {0x1b637482, "MouseEvent", 0xfb637610, 2},
    {0x5b63748b, "KeyEvent", 0x1b6377b0, 2},
};
} // namespace

nGZEvent::cEventAttributeMap *cGZCanvasBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

// 0x149ef0.
nGZEvent::cEventAttributeMap *__cdecl cGZCanvasBehaviorTraits::RegistryFor(uint32_t id,
                                                                           const nGZEvent::cEventAttribute *attribute)
{
    return nGZEvent::ClassRegistryFor(gRegistry, kClassEvents, std::size(kClassEvents), id, attribute);
}

void cGZCanvasBehaviorTraits::ResetRegistryForTest() { gRegistry.reset(); }

cGZCanvas::cGZCanvas()
    : mServiceId(kServiceId), mRunning(false), mActive(false), mMouseX(0), mMouseY(0), mWindow(nullptr),
      mMessageServer(nullptr)
{
}

// 0x148e20: the message server, then the window released.
cGZCanvas::~cGZCanvas()
{
    if (mMessageServer) mMessageServer->Release();
    if (mWindow) mWindow->Release();
}

cIGZCanvas *__cdecl cGZCanvas::Create() { return static_cast<cIGZCanvas *>(new cGZCanvas); }

// The four interfaces counted without a null-out check; else the behaviour's answers (0x148310).
bool cGZCanvas::QueryInterface(uint32_t riid, void **out)
{
    void *face = nullptr;
    switch (riid) {
    case cIGZCanvas::kIid: face = static_cast<cIGZCanvas *>(this); break;
    case cIGZSystemService::kIID: face = static_cast<cIGZSystemService *>(this); break;
    case cIGZCanvasMessaging::kIid: face = static_cast<cIGZCanvasMessaging *>(this); break;
    case cIGZCanvasW32::kIid: face = static_cast<cIGZCanvasW32 *>(this); break;
    }
    if (face) {
        *out = face;
        AddRef();
        return true;
    }
    if (!out) return false;
    if (riid == 1 || riid == kIidBehavior2) {
        *out = static_cast<cGZCanvasBehavior *>(this);
        AddRef();
        return true;
    }
    if (riid == kIidBehaviorType) {
        *out = static_cast<cGZCanvasBehavior *>(this); // not counted (the original answers 0xfb859301)
        return true;
    }
    return false;
}

uint32_t cGZCanvas::AddRef() { return Behavior::AddRef(); }
uint32_t cGZCanvas::Release() { return Behavior::Release(); }

void cGZCanvas::Send(cIGZUnknown *message) { RawSlot<3, bool>(mMessageServer, message); }

// The connection change keeps the active bit (the behaviour's own OnEvent); cGraphicSystemDX's device ready and
// render-scene events and the window's canvas, mouse and key events go to the message server as messages.
void cGZCanvas::OnEvent(uint32_t type, uint32_t, void *data, uint32_t)
{
    if (type == kConnectionChangedEvent) SyncActive();
    const uint32_t *const d = static_cast<const uint32_t *>(data);
    uint32_t code = 0;
    switch (type) {
    case kRenderSceneEvent:
        if (d[1] != 1) return;
        code = cGZCanvasMessage::kRenderScene;
        break;
    case kCanvasEvent:
        switch (d[1]) {
        case 0: code = cGZCanvasMessage::kSizeChanged; break;
        case 1:
        case 5: code = cGZCanvasMessage::kMinimized; break;
        case 2:
        case 6: code = cGZCanvasMessage::kRestored; break;
        default: return;
        }
        break;
    case kDeviceStateEvent:
        if (d[1] != 1) return;
        code = cGZCanvasMessage::kDeviceReady;
        break;
    case kMouseEvent: {
        mMouseX = int32_t(d[2]);
        mMouseY = int32_t(d[3]);
        cGZMouseMessage *const message = cGZMouseMessage::New();
        message->AddRef();
        message->Set(d[1], reinterpret_cast<const cGZMousePosition *>(d + 2), d[4], d[6]);
        Send(message);
        message->Release();
        return;
    }
    case kKeyEvent: {
        if (d[1] != 4 && d[1] != 5 && d[1] != 6) return;
        cGZKeyMessage *const message = cGZKeyMessage::New();
        message->AddRef();
        message->Set(d[1], d[2], d[4], d[3]);
        Send(message);
        message->Release();
        return;
    }
    default: return;
    }
    cGZCanvasMessage *const message = cGZCanvasMessage::New();
    message->AddRef();
    message->SetCode(code);
    Send(message);
    message->Release();
}

// ---- cIGZCanvas ----

bool cGZCanvas::CanvasSlot3(uint32_t, uint32_t, uint32_t) { return true; }
bool cGZCanvas::CanvasSlot4(uint32_t) { return true; }
bool cGZCanvas::IsConnected() { return mActive; }

// 0x14a280 (also the service's Shutdown): disconnected from everything, the window and the message server let go.
bool cGZCanvas::Shutdown()
{
    mActive = false;
    GetEventManager()->DisconnectAll(static_cast<cGZCanvasBehavior *>(this));
    if (cIGZUnknown *const window = mWindow) {
        mWindow = nullptr;
        window->Release();
    }
    if (cIGZUnknown *const server = mMessageServer) {
        mMessageServer = nullptr;
        server->Release();
    }
    return true;
}

bool cGZCanvas::CanvasSlot7() { return true; }
bool cGZCanvas::CanvasSlot8() { return true; }
bool cGZCanvas::IsMaximized() { return RawSlot<34, bool>(mWindow); }
bool cGZCanvas::IsCaptionless() { return RawSlot<35, bool>(mWindow); }

int32_t cGZCanvas::SetRect(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    const HWND window = static_cast<HWND>(GetHwnd());
    return ImportSlot<BOOL(WINAPI *)(HWND, HWND, int, int, int, int, UINT), kIatSetWindowPos>()(
        window, nullptr, left, top, right - left, bottom - top, kSetRectFlags);
}

cGZCanvasRect *cGZCanvas::GetClientRect(cGZCanvasRect *out) { return RawSlot<29, cGZCanvasRect *>(mWindow, out), out; }
uint32_t cGZCanvas::Show() { return RawSlot<15, uint32_t>(mWindow, 0xffffffffu, 0u); }
void cGZCanvas::CanvasSlot14() {}
uint32_t cGZCanvas::SetTitle(cIGZString *title) { return RawSlot<17, uint32_t>(mWindow, RawSlot<5, const char *>(title)); }
void cGZCanvas::CanvasSlot16() {}
void cGZCanvas::CanvasSlot17() {}
int32_t cGZCanvas::GetMouseX() { return mMouseX; }
int32_t cGZCanvas::GetMouseY() { return mMouseY; }
uint32_t cGZCanvas::CanvasSlot20() { return 0; }

// A window message (type 0x8006, three words or more) posted to the window.
bool cGZCanvas::PostWindowMessage(cIGZUnknown *message)
{
    if (!message) return false;
    if (RawSlot<3, uint32_t>(message) != kWindowMessageType) return false;
    if (RawSlot<4, uint32_t>(message) < 3) return false;
    const uint32_t *const words = RawSlot<108, const uint32_t *>(message);
    const UINT msg = words[0];
    const WPARAM wParam = words[1];
    const LPARAM lParam = LPARAM(words[2]);
    const HWND window = static_cast<HWND>(GetHwnd());
    return ImportSlot<BOOL(WINAPI *)(HWND, UINT, WPARAM, LPARAM), kIatPostMessageA>()(window, msg, wParam, lParam) != 0;
}

void *cGZCanvas::GetHwndCell()
{
    void **const cell = &ts2_rva_10c9570;
    *cell = GetHwnd();
    return cell;
}

// ---- cIGZCanvasW32 ----

cIGZCanvas *cGZCanvas::GetCanvas() { return static_cast<cIGZCanvas *>(this); }
void *cGZCanvas::GetHwnd() { return CallWindowCanvas<void *, 40>(mWindow); }
uint16_t cGZCanvas::GetSlot41() { return CallWindowCanvas<uint16_t, 41>(mWindow); }
bool cGZCanvas::Slot43(uint32_t a, uint32_t b) { return CallWindowCanvas<bool, 43>(mWindow, a, b); }

// ---- cIGZCanvasMessaging ----

cIGZUnknown *cGZCanvas::GetMessageServer() { return mMessageServer; }

// ---- cIGZSystemService ----

uint32_t cGZCanvas::GetServiceID() { return mServiceId; }
void cGZCanvas::SetServiceID(uint32_t id) { mServiceId = id; }
int32_t cGZCanvas::GetServicePriority() { return kPriority; }
bool cGZCanvas::IsServiceRunning() { return mRunning; }
void cGZCanvas::SetServiceRunning(bool running) { mRunning = running; }
bool cGZCanvas::OnTick(uint32_t) { return true; }
bool cGZCanvas::OnIdle(uint32_t) { return true; }
int32_t cGZCanvas::GetServiceTickPriority() { return kPriority; }

// The message server, then the window (shut down without one), then cGraphicSystemDX: the canvas listens to the
// graphic system's device-state and render-scene events and the window's canvas, mouse and key events. False (shut
// down) without the graphic system.
bool cGZCanvas::Init()
{
    FetchService(mMessageServer, kMessageServerId, kMessageServerIid);
    if (!mMessageServer) return false;
    FetchService(mWindow, kWindowServiceId, kWindowBehaviorIid);
    if (!mWindow) {
        Shutdown();
        return false;
    }
    RZAutoRef system{nullptr};
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kGraphicSystemDXId, kGraphicSystemDXIid, &system.mObject);
    const bool ok = system.mObject != nullptr;
    if (ok) {
        mActive = true;
        cGZCanvasBehavior *const self = static_cast<cGZCanvasBehavior *>(this);
        RawSlot<8, void>(system.mObject, kDeviceStateEvent, self, kDeviceStateEvent);
        RawSlot<8, void>(system.mObject, kRenderSceneEvent, self, kRenderSceneEvent);
        RawSlot<8, void>(mWindow, kCanvasEvent, self, kCanvasEvent);
        RawSlot<8, void>(mWindow, kMouseEvent, self, kMouseEvent);
        RawSlot<8, void>(mWindow, kKeyEvent, self, kKeyEvent);
    } else {
        Shutdown();
    }
    system.ReleaseObject();
    return ok;
}

} // namespace nGZGraphic4
