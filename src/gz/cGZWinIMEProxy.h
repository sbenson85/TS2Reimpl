#pragma once
#include "gz/cGZWinIMEServerW32.h" // cIGZWinIMEServer
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount
#include "ui/cTSMainToolbar.h"             // cIGZMessageTarget, cGZMessage

#include <cstddef>
#include <cstdint>

// cGZWinIMEProxy (lane KN): the UI side of input-method editing (service id 0x2ac45449, priority -1000000, interface
// 0x2ac44ae9). It listens for four messages from the IME server (src/gz/cGZWinIMEServerW32.h) and opens or closes the
// composition and candidate windows (game classes 0x2ac45170 / 0x2ac45173, made through the COM object or built here),
// and on every fourth tick follows the focused window: while the focus is on a window that takes text it leaves the IME
// alone, otherwise it asks the message server for the caret (message 0xbe10a20) and closes the IME when nobody answers.
//
// Faces (RTTI order): +0 cIGZWinIMEProxy (game vtable 0xe5a3cc, 15 slots), +4 cIGZMessageTarget (0xe5a40c, 4), +8
// rz::cRZSystemService (0xe5a420, 13) with its cRZUnknownMT at +0xc (0xe5a458, 6). 0x68 bytes. The proxy keeps its own
// (plain) reference count at +0x24: AddRef / Release override the service's on every face. The cIGZSystemService face's
// Init and Shutdown are the compiler's thunks to the proxy face's slots 3 and 4.
//
// Creation: the cGZWinCOMDirector registers the factory 0x92f6aa (new 0x68, a tail jump to the constructor 0x96ca9b) for
// class 0x2ac4516d; it is the constructor's only caller and the vtables are referenced only by the constructor and the
// destructor 0x96cc19. With the factory hooked to Create, every proxy carries our vtables
// (db/status/cGZWinIMEProxy.jsonl).

using UnknownRef = cRZAutoRefCount<cIGZUnknown>;
using IMEServerRef = cRZAutoRefCount<cIGZWinIMEServer>;

// cIGZWinIMEProxy (iid 0x2ac44ae9), 15 slots.
class cIGZWinIMEProxy : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x2ac44ae9;

    virtual bool Init() = 0;                                // slot 3
    virtual bool Shutdown() = 0;                            // slot 4
    virtual bool IsCompositionWindowEnabled() = 0;          // slot 5 (folded 0x96d2b7: the byte +0x50)
    virtual void EnableCompositionWindow(bool enable) = 0;  // slot 6 (ret 4)
    virtual cIGZUnknown *GetCompositionWindow() = 0;        // slot 7 (folded 0x16e970: +0x54, not counted)
    virtual cIGZUnknown *GetCompositionFont() = 0;          // slot 8 (folded 0x1dff70: +0x58)
    virtual UnknownRef *SetCompositionFont(cIGZUnknown *font) = 0; // slot 9 (folded 0x7495d2, ret 4): the field's address
    virtual bool IsCandidateWindowEnabled() = 0;            // slot 10 (folded 0x96d269: the byte +0x5c)
    virtual void EnableCandidateWindow(bool enable) = 0;    // slot 11 (ret 4)
    virtual cIGZUnknown *GetCandidateWindow() = 0;          // slot 12 (folded 0x230120: +0x60)
    virtual cIGZUnknown *GetCandidateFont() = 0;            // slot 13 (folded 0x22fca0: +0x64)
    virtual UnknownRef *SetCandidateFont(cIGZUnknown *font) = 0;   // slot 14 (folded 0x96de3f, ret 4)
};

class cGZWinIMEProxy : public cIGZWinIMEProxy, public cIGZMessageTarget, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x68;
    static constexpr uint32_t kServiceId = 0x2ac45449;
    static constexpr int32_t kPriority = -1000000;
    static constexpr uint32_t kMessageTargetIid = 0xe98b2f57; // cIGZMessageTarget
    // The messages it registers for (the IME server posts them): open / close the composition and candidate windows.
    static constexpr uint32_t kMsgOpenComposition = 0x2ac44c0e, kMsgCloseComposition = 0x2ac44c11;
    static constexpr uint32_t kMsgOpenCandidates = 0x2ac44ed1, kMsgCloseCandidates = 0x2ac44ed4;
    // The services it uses (framework slot 5: service id, interface).
    static constexpr uint32_t kMessageServerId = 0x64063387, kMessageServerIid = 0xc1085722;
    static constexpr uint32_t kIMEServerId = 0x2ac45447;
    static constexpr uint32_t kWinRootServiceId = 0xa417445e, kWinRootServiceIid = 0x5a4;
    static constexpr uint32_t kFontServiceId = 0x20673960, kFontServiceIid = 0x673960;
    static constexpr uint32_t kWinMgrServiceId = 0xc2c2eb0f, kWinMgrIid = 0x22c2eb1f;
    // The windows: COM class ids, the interface asked for, the size and constructor of the game's own class (built when
    // the COM object has none).
    static constexpr uint32_t kCompositionClass = 0x2ac45170, kCompositionIid = 0x2ac44a90;
    static constexpr uint32_t kCandidateClass = 0x2ac45173, kCandidateIid = 0x2ac44a93;
    static constexpr uint32_t kCompositionWindowSize = 0x368, kCandidateWindowSize = 0x170;
    static constexpr uint32_t kTextFocusIid = 0x231a1c57; // a focused window answering this takes text itself
    static constexpr uint32_t kCaretMessage = 0xbe10a20;  // sent to the message server; the answer is the caret
    static constexpr uint32_t kFontWindowId = 0x68963c4c; // the window whose caption names the IME windows' font
    static constexpr int32_t kNoPosition = 0x7fffffff;

    cGZWinIMEProxy();            // 0x96ca9b
    ~cGZWinIMEProxy() noexcept override; // 0x96cc19; deleting 0x96cd5a (+0xc slot 3, thunk 0x96cd06)

    cGZWinIMEProxy(const cGZWinIMEProxy &) = delete;
    cGZWinIMEProxy &operator=(const cGZWinIMEProxy &) = delete;

    // 0x92f6aa (the factory the director registers): new, construct, the object (the +0 face).
    static cIGZWinIMEProxy *__cdecl Create();

    // cIGZUnknown (every face; +4 / +8 / +0xc through the thunks).
    bool QueryInterface(uint32_t riid, void **out) override; // 0x96dbe7
    uint32_t AddRef() override;                              // 0x96cdd1: the plain count at +0x24
    uint32_t Release() override;                             // 0x96dcc4: Destroy at a count of 1 or below

    // cIGZWinIMEProxy, and cIGZSystemService's Init / Shutdown / OnTick
    bool Init() override;                                    // 0x96d8a4 (service face: thunk 0x96d9c1)
    bool Shutdown() override;                                // 0x96eb5b (service face: thunk 0x96ec3d)
    bool IsCompositionWindowEnabled() override;
    void EnableCompositionWindow(bool enable) override;      // 0x96e16a
    cIGZUnknown *GetCompositionWindow() override;
    cIGZUnknown *GetCompositionFont() override;
    UnknownRef *SetCompositionFont(cIGZUnknown *font) override;
    bool IsCandidateWindowEnabled() override;
    void EnableCandidateWindow(bool enable) override;        // 0x96de80
    cIGZUnknown *GetCandidateWindow() override;
    cIGZUnknown *GetCandidateFont() override;
    UnknownRef *SetCandidateFont(cIGZUnknown *font) override;
    bool OnTick(uint32_t counter) override;                  // 0x96da43 (service face slot 10)

    // cIGZMessageTarget
    bool DoMessage(cGZMessage *message) override;            // 0x96cf71

    // 0x96d02c: Shutdown (through the +0 vtable), then the deleting destructor (through the +0xc face).
    void Destroy();
    // 0x96ed48 / 0x96ec7b: the composition / candidate window, made when enabled and not open yet.
    void OpenCompositionWindow();
    void OpenCandidateWindow();
    // 0x96cff3 / 0x96cfba: the window taken off the root window and released.
    void CloseCompositionWindow();
    void CloseCandidateWindow();
    // 0x96e5d8: the two fonts from the font service (named by the caption of window 0x68963c4c), when either is missing.
    void LoadFonts();
    // 0x96c8ee: the IME server service (cleared first; left null without a framework or service).
    static void GetIMEServer(IMEServerRef &server);

    uint32_t mRefCount;             // +0x24 (the proxy's own count; the service's at +0x10 is not used)
    uint32_t mProxyId;              // +0x28 (the service id again; nothing reads it)
    bool mInitialized;              // +0x2c
    uint32_t mTickCount;            // +0x30
    UnknownRef mMessageServer;      // +0x34
    int32_t mCaretX;                // +0x38 (kNoPosition until the message server answers)
    int32_t mCaretY;                // +0x3c
    bool mTickDisabled;             // +0x40 (only the constructor writes it)
    IMEServerRef mIMEServer;        // +0x44
    UnknownRef mWinRoot;            // +0x48 (service 0xa417445e: slot 37 is the focused window)
    cIGZUnknown *mFocusWindow;      // +0x4c (not counted; compared with the focused window each check)
    bool mCompositionEnabled;       // +0x50
    UnknownRef mCompositionWindow;  // +0x54
    UnknownRef mCompositionFont;    // +0x58
    bool mCandidateEnabled;         // +0x5c
    UnknownRef mCandidateWindow;    // +0x60
    UnknownRef mCandidateFont;      // +0x64
};

static_assert(sizeof(cGZWinIMEProxy) == cGZWinIMEProxy::kSize, "cGZWinIMEProxy size");
static_assert(offsetof(cGZWinIMEProxy, mRefCount) == 0x24 && offsetof(cGZWinIMEProxy, mInitialized) == 0x2c,
              "cGZWinIMEProxy layout: the service part at +8");
static_assert(offsetof(cGZWinIMEProxy, mMessageServer) == 0x34 && offsetof(cGZWinIMEProxy, mTickDisabled) == 0x40 &&
                  offsetof(cGZWinIMEProxy, mFocusWindow) == 0x4c && offsetof(cGZWinIMEProxy, mCandidateFont) == 0x64,
              "cGZWinIMEProxy layout");
