// cGZWinIMEProxy (lane KN): the whole class in the standalone shape (src/gz/cGZWinIMEProxy.h). Built for size (/O1) in
// the game.
#include "gz/cGZWinIMEProxy.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZWin.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZString.h"

#include <new>

namespace {

// The framework's GetSystemService (slot 5) into a counted holder (cleared first; untouched without a framework). The
// original's small holder constructors (0x762db4, 0x96c8ee, 0x20dbd8, 0x20db1f, 0x20dbb3) do exactly this.
template <class Ref> void GetService(uint32_t id, uint32_t iid, Ref &out)
{
    void **const slot = out.AsPPVoid();
    if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, slot);
}

// A cRZString on the stack (constructed by Construct 0x144f5b, destroyed by Destruct 0x141193, as the original's inline
// vtable store and std::string teardown).
struct LocalString {
    union {
        cRZString text;
    };
    LocalString() { text.Construct(); }
    ~LocalString() { text.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// The message server's message (slot 3 sends it and the handlers fill it in): the caret query.
struct CaretMessage {
    uint32_t id;
    uint32_t answered; // set by a handler
    int32_t x, y;
};

} // namespace

cGZWinIMEProxy::cGZWinIMEProxy()
    : rz::cRZSystemService(kServiceId, kPriority), mRefCount(0), mProxyId(kServiceId), mInitialized(false), mTickCount(0),
      mCaretX(kNoPosition), mCaretY(kNoPosition), mTickDisabled(false), mFocusWindow(nullptr), mCompositionEnabled(true),
      mCandidateEnabled(true)
{
}

cGZWinIMEProxy::~cGZWinIMEProxy() noexcept = default; // the counted members, then the service part

cIGZWinIMEProxy *__cdecl cGZWinIMEProxy::Create() { return new cGZWinIMEProxy; }

bool cGZWinIMEProxy::QueryInterface(uint32_t riid, void **out)
{
    // No null test on `out` (the original writes it unchecked).
    if (riid == 1 || riid == cIGZWinIMEProxy::kIid) {
        *out = static_cast<cIGZWinIMEProxy *>(this);
        AddRef();
        return true;
    }
    if (riid == kMessageTargetIid) {
        *out = static_cast<cIGZMessageTarget *>(this);
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

uint32_t cGZWinIMEProxy::AddRef() { return ++mRefCount; }

uint32_t cGZWinIMEProxy::Release()
{
    if (mRefCount > 1) return --mRefCount;
    Destroy();
    return 0;
}

void cGZWinIMEProxy::Destroy()
{
    Shutdown();
    delete this;
}

void cGZWinIMEProxy::GetIMEServer(IMEServerRef &server) { GetService(kIMEServerId, cIGZWinIMEServer::kIid, server); }

bool cGZWinIMEProxy::Init()
{
    if (mInitialized) return true;
    mInitialized = true;

    UnknownRef messageServer;
    GetService(kMessageServerId, kMessageServerIid, messageServer);
    mMessageServer.Assign(messageServer.Get());

    IMEServerRef imeServer;
    GetIMEServer(imeServer);
    if (imeServer) {
        mIMEServer.Assign(imeServer.Get());
        mIMEServer->SetEnabled(true);
    }

    UnknownRef winRoot;
    GetService(kWinRootServiceId, kWinRootServiceIid, winRoot);
    if (winRoot) mWinRoot.Assign(winRoot.Get());

    UnknownRef notifier; // the message server again: the four notifications (its slot 5: target, message type)
    GetService(kMessageServerId, kMessageServerIid, notifier);
    if (notifier) {
        cIGZMessageTarget *const target = static_cast<cIGZMessageTarget *>(this);
        RawSlot<5, bool>(notifier.Get(), target, kMsgOpenComposition);
        RawSlot<5, bool>(notifier.Get(), target, kMsgCloseComposition);
        RawSlot<5, bool>(notifier.Get(), target, kMsgOpenCandidates);
        RawSlot<5, bool>(notifier.Get(), target, kMsgCloseCandidates);
    }

    if (mIMEServer && mIMEServer->IsAvailable()) {
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->AddToTick(
                As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
    }
    return true;
}

bool cGZWinIMEProxy::Shutdown()
{
    if (!mInitialized) return true;
    mInitialized = false;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->RemoveFromTick(
            As<ifaces::cIGZSystemService>(static_cast<rz::cRZSystemService *>(this)));
    mMessageServer.Assign(nullptr);
    mIMEServer.Assign(nullptr);
    mWinRoot.Assign(nullptr);
    mFocusWindow = nullptr;
    mCompositionFont.Assign(nullptr);
    mCandidateFont.Assign(nullptr);
    // The open windows (+0x54, +0x60) stay.

    IMEServerRef imeServer;
    GetIMEServer(imeServer);
    if (imeServer) imeServer->SetEnabled(false);

    UnknownRef notifier; // its slot 6: the notification removed
    GetService(kMessageServerId, kMessageServerIid, notifier);
    if (notifier) {
        cIGZMessageTarget *const target = static_cast<cIGZMessageTarget *>(this);
        RawSlot<6, bool>(notifier.Get(), target, kMsgOpenComposition);
        RawSlot<6, bool>(notifier.Get(), target, kMsgCloseComposition);
        RawSlot<6, bool>(notifier.Get(), target, kMsgOpenCandidates);
        RawSlot<6, bool>(notifier.Get(), target, kMsgCloseCandidates);
    }
    return true;
}

bool cGZWinIMEProxy::IsCompositionWindowEnabled() { return mCompositionEnabled; }

void cGZWinIMEProxy::EnableCompositionWindow(bool enable)
{
    if (mCompositionEnabled != enable) mCompositionEnabled = enable;
}

cIGZUnknown *cGZWinIMEProxy::GetCompositionWindow() { return mCompositionWindow.Get(); }
cIGZUnknown *cGZWinIMEProxy::GetCompositionFont() { return mCompositionFont.Get(); }

UnknownRef *cGZWinIMEProxy::SetCompositionFont(cIGZUnknown *font)
{
    mCompositionFont.Assign(font);
    return &mCompositionFont;
}

bool cGZWinIMEProxy::IsCandidateWindowEnabled() { return mCandidateEnabled; }

void cGZWinIMEProxy::EnableCandidateWindow(bool enable)
{
    if (mCandidateEnabled != enable) mCandidateEnabled = enable;
}

cIGZUnknown *cGZWinIMEProxy::GetCandidateWindow() { return mCandidateWindow.Get(); }
cIGZUnknown *cGZWinIMEProxy::GetCandidateFont() { return mCandidateFont.Get(); }

UnknownRef *cGZWinIMEProxy::SetCandidateFont(cIGZUnknown *font)
{
    mCandidateFont.Assign(font);
    return &mCandidateFont;
}

bool cGZWinIMEProxy::OnTick(uint32_t)
{
    if (++mTickCount & 3) return true;
    if (!mWinRoot || !mIMEServer) return true;
    cIGZUnknown *const focus = RawSlot<37, cIGZUnknown *>(mWinRoot.Get()); // the focused window
    if (mFocusWindow == focus && (mTickCount & 7)) return true;
    mFocusWindow = focus;
    mCaretX = kNoPosition;
    mCaretY = kNoPosition;
    if (mTickDisabled) return true;

    if (focus) {
        // A window that edits text itself, or one of the IME windows, has the focus: nothing to do.
        UnknownRef textFocus, composition, candidates;
        const bool handled = focus->QueryInterface(kTextFocusIid, textFocus.AsPPVoid()) ||
                             mFocusWindow->QueryInterface(kCompositionIid, composition.AsPPVoid()) ||
                             mFocusWindow->QueryInterface(kCandidateIid, candidates.AsPPVoid());
        if (handled) return true;
    }

    CaretMessage message = {kCaretMessage, 0, 2, 2};
    RawSlot<3, bool>(mMessageServer.Get(), &message);
    if (message.answered) {
        mCaretX = message.x;
        mCaretY = message.y;
    } else if (mIMEServer->IsOpen()) {
        mIMEServer->SetOpen(false); // nobody shows a caret: close the input method
    }
    return true;
}

bool cGZWinIMEProxy::DoMessage(cGZMessage *message)
{
    const uint32_t id = message->mId;
    if (id == kMsgOpenComposition) OpenCompositionWindow();
    else if (id == kMsgCloseComposition) CloseCompositionWindow();
    else if (id == kMsgOpenCandidates) OpenCandidateWindow();
    else if (id == kMsgCloseCandidates) CloseCandidateWindow();
    return true;
}

void cGZWinIMEProxy::LoadFonts()
{
    if (mCompositionFont && mCandidateFont) return;
    UnknownRef fonts, winMgr;
    GetService(kFontServiceId, kFontServiceIid, fonts);
    GetService(kWinMgrServiceId, kWinMgrIid, winMgr);
    if (!fonts || !winMgr) return;
    void *const window = RawSlot<5, void *>(winMgr.Get(), kFontWindowId); // not counted
    LocalString name;
    if (window) RawSlot<5, uint32_t>(window, name.text.AsInterface()); // its caption into `name`
    // The font service's slot 11 (name, 0, size 0x12, 0, 1) hands out a counted font: stored without AddRef.
    if (!mCompositionFont) *mCompositionFont.AsPPVoid() = RawSlot<11, void *>(fonts.Get(), name.text.AsInterface(), 0u, 0x12u, 0u, 1u);
    if (!mCandidateFont) *mCandidateFont.AsPPVoid() = RawSlot<11, void *>(fonts.Get(), name.text.AsInterface(), 0u, 0x12u, 0u, 1u);
}

void cGZWinIMEProxy::OpenCompositionWindow()
{
    if (!mCompositionEnabled || mCompositionWindow) return;
    UnknownRef winRoot;
    GetService(kWinRootServiceId, kWinRootServiceIid, winRoot);
    void *const mainWindow = RawSlot<3, void *>(winRoot.Get()); // no null test (as the original)
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (!static_cast<uint8_t>(com->GetClassObject(kCompositionClass, kCompositionIid, mCompositionWindow.AsPPVoid()))) {
        void *const memory = ::operator new(kCompositionWindowSize);
        mCompositionWindow.Assign(OriginalFunction<cIGZUnknown *(__thiscall *)(void *), 0x96ca20>()(memory));
    }
    if (!mCompositionWindow) return;
    LoadFonts();
    RawSlot<7, uint32_t>(mCompositionWindow.Get(), mCompositionFont.Get()); // its font
    void *const window = RawSlot<3, void *>(mCompositionWindow.Get());        // its cIGZWin
    RawSlot<19, uint32_t>(mainWindow, window);                               // a child of the main window
    RawSlot<28, uint32_t>(window);
    if (mCaretX != kNoPosition) RawSlot<66, uint32_t>(window, mCaretX, mCaretY); // at the caret
    As<ifaces::cIGZWin>(window)->SetFlag(0x800, 0);
}

void cGZWinIMEProxy::OpenCandidateWindow()
{
    if (!mCandidateEnabled || mCandidateWindow) return;
    UnknownRef winRoot;
    GetService(kWinRootServiceId, kWinRootServiceIid, winRoot);
    void *const mainWindow = RawSlot<3, void *>(winRoot.Get());
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (!static_cast<uint8_t>(com->GetClassObject(kCandidateClass, kCandidateIid, mCandidateWindow.AsPPVoid()))) {
        void *const memory = ::operator new(kCandidateWindowSize);
        mCandidateWindow.Assign(OriginalFunction<cIGZUnknown *(__thiscall *)(void *), 0x96c9c9>()(memory));
    }
    if (!mCandidateWindow) return;
    LoadFonts();
    RawSlot<7, uint32_t>(mCandidateWindow.Get(), mCandidateFont.Get());
    void *const window = RawSlot<3, void *>(mCandidateWindow.Get());
    RawSlot<19, uint32_t>(mainWindow, window);
    RawSlot<28, uint32_t>(window);
    As<ifaces::cIGZWin>(window)->SetFlag(0x800, 0);
}

void cGZWinIMEProxy::CloseCompositionWindow()
{
    if (!mCompositionWindow) return;
    UnknownRef winRoot;
    GetService(kWinRootServiceId, kWinRootServiceIid, winRoot);
    RawSlot<21, uint32_t>(winRoot.Get(), RawSlot<3, void *>(mCompositionWindow.Get())); // off the root (no null test)
    mCompositionWindow.Assign(nullptr);
}

void cGZWinIMEProxy::CloseCandidateWindow()
{
    if (!mCandidateWindow) return;
    UnknownRef winRoot;
    GetService(kWinRootServiceId, kWinRootServiceIid, winRoot);
    RawSlot<21, uint32_t>(winRoot.Get(), RawSlot<3, void *>(mCandidateWindow.Get()));
    mCandidateWindow.Assign(nullptr);
}
