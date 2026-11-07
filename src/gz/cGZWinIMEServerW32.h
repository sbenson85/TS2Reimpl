#pragma once
#include "gz/cIGZMessage2Standard.h" // cIGZMessage2
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The input-method (IME) server (lanes KN, KR): service 0x2ac45447, priority -999999, interface cIGZWinIMEServer. It
// keeps the composition (the text being typed, its clause attributes and the cursor) and the candidate list, posts the
// IME messages the proxy (src/gz/cGZWinIMEProxy.h) listens for, and turns a finished composition into key messages for
// the window manager. The Win32 subclass hooks the main window's procedure (cIGZWinProcFilterW32) and maps the
// interface onto imm32.
//
// Creation: the Win32 COM director (0x970cda) registers the factory 0x970d15 (new 0xa8, the constructor 0x971ca3, the
// +4 face back). Nothing else builds either class: the base constructor 0x96cb1d is called only by 0x971ca3, and the
// vtables (base 0xe5a32c / 0xe5a378 / 0xe5a3b0, Win32 0xe5b3c4 / 0xe5b410 / 0xe5b448 / 0xe5b464) are referenced only by
// the constructors and destructors. With the factory hooked to Create, every server carries our vtables
// (db/status/cGZWinIMEServerW32.jsonl). Every Win32 call goes through the game's import slot (ImportFunction).

// The composition (cIGZWinIMEServer slot 12 hands out its address; the server's +0x34). 0x3c bytes. The first three
// words are a view of the string and attributes refreshed by RefreshView.
struct cGZIMEComposition {
    const char16_t *text;        // +0x00 the string's characters, null while empty
    const uint8_t *attributes;   // +0x04 the attribute bytes (one per character: 0 none, 1 input, 2 target, 3 converted)
    uint32_t length;             // +0x08 the string's length
    uint32_t cursor;             // +0x0c the cursor (GCS_CURSORPOS, at most the length)
    uint32_t language;           // +0x10 the language id (language service slot 7, then slot 12 of the keyboard layout)
    std::u16string string;       // +0x14
    std::vector<uint8_t> attributeBuffer; // +0x2c
    bool hasResult;              // +0x38 a result string arrived (the constructor leaves it unset: 0x96c965)
    bool composing;              // +0x39 between WM_IME_STARTCOMPOSITION and WM_IME_ENDCOMPOSITION

    cGZIMEComposition();         // 0x96c965
    void RefreshView();          // 0x96ddd4
    void Clear();                // 0x96ce2d: string and attributes emptied, no result, the view refreshed
};
static_assert(sizeof(cGZIMEComposition) == 0x3c, "cGZIMEComposition size");
static_assert(offsetof(cGZIMEComposition, string) == 0x14 && offsetof(cGZIMEComposition, attributeBuffer) == 0x2c &&
                  offsetof(cGZIMEComposition, hasResult) == 0x38,
              "cGZIMEComposition layout");

// The candidate list (slot 16 hands out its address; the server's +0x70). 0x2c bytes. The first two words are a view:
// an array of the candidates' character pointers and its count, refreshed by RefreshView.
struct cGZIMECandidates {
    const char16_t *const *items; // +0x00 null while empty
    uint32_t count;               // +0x04
    uint32_t selection;           // +0x08 the IME's selected candidate
    uint32_t language;            // +0x0c as cGZIMEComposition::language
    std::vector<std::u16string> strings;  // +0x10
    std::vector<const char16_t *> pointers; // +0x1c
    bool open;                    // +0x28 (nothing but the constructor writes it)

    cGZIMECandidates();           // 0x96c913
    void RefreshView();           // 0x96dd2c: each string ends in an explicit 0 (appended when missing)
    void Clear();                 // 0x96ce0f
};
static_assert(sizeof(cGZIMECandidates) == 0x2c, "cGZIMECandidates size");
static_assert(offsetof(cGZIMECandidates, strings) == 0x10 && offsetof(cGZIMECandidates, open) == 0x28,
              "cGZIMECandidates layout");

// cIGZWinIMEServer (iid 0x2ac44ae7, 18 slots): the input-method server the IME proxy talks to. Slot meanings from both
// implementations' bodies (lane KN); the Win32 one maps them onto imm32 calls.
class cIGZWinIMEServer : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x2ac44ae7;

    virtual bool Init() = 0;                 // slot 3 (the service face's Init is a thunk to it)
    virtual bool Shutdown() = 0;             // slot 4
    virtual bool IsAvailable() = 0;          // slot 5: the base answers false, the Win32 server true
    virtual bool IsEnabled() = 0;            // slot 6: the byte +0x29
    virtual void SetEnabled(bool enable) = 0; // slot 7 (ret 4)
    virtual bool IsOpen() = 0;               // slot 8: the input context's open status (Win32)
    virtual bool SetOpen(bool open) = 0;     // slot 9 (ret 4)
    virtual bool IsComposing() = 0;          // slot 10: the byte +0x6d
    virtual bool SetComposing(bool composing) = 0; // slot 11 (ret 4): false cancels the composition (Win32)
    virtual cGZIMEComposition *GetComposition() = 0; // slot 12: +0x34
    virtual bool SetCompositionString(const char16_t *text, uint32_t length) = 0; // slot 13 (ret 8)
    virtual bool IsCandidateListOpen() = 0;  // slot 14: the byte +0x98
    virtual bool SetCandidateListOpen(bool open) = 0; // slot 15 (ret 4)
    virtual cGZIMECandidates *GetCandidates() = 0;    // slot 16: +0x70
    virtual bool SelectCandidate(uint32_t index) = 0; // slot 17 (ret 4): false past the candidate count
};

// cIGZWinProcFilterW32 (iid 0xcad6cbf0): sees a window's messages before its procedure (the Win32 window service,
// interface 0xe46397db, slot 6 adds or removes one).
class cIGZWinProcFilterW32 : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xcad6cbf0;
    // slot 3 (ret 0x14): *handled (cleared first) true means the message stops here, with the result returned.
    virtual intptr_t FilterMessage(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam, bool *handled) = 0;
};

// cIGZKeyMessage (iid 0x1a104742): a cIGZMessage2 carrying a key event (the window manager's key target reads slots 9
// to 12 into its KeyEvent, src/gz/cGZWinMgrW32.size.cpp).
class cIGZKeyMessage : public cIGZMessage2 {
public:
    static constexpr uint32_t kIid = 0x1a104742;
    virtual void SetKeyEvent(uint32_t type, uint32_t key, uint32_t modifiers, uint32_t extra) = 0; // slot 8 (ret 0x10)
    virtual uint32_t GetEventType() = 0;  // slot 9
    virtual uint32_t GetKey() = 0;        // slot 10
    virtual uint32_t GetModifiers() = 0;  // slot 11
    virtual uint32_t GetExtra() = 0;      // slot 12
};

// cGZWinIMEKeyMessage (game vtable 0xe5b478, 14 slots, 0x1c bytes): the key message the server posts for each byte of a
// result string. Only 0x971df7 (PostCharacter) builds it.
class cGZWinIMEKeyMessage : public cIGZKeyMessage {
public:
    static constexpr uint32_t kType = 0x7a104750; // the window manager's key target

    cGZWinIMEKeyMessage();                        // 0x971c81
    virtual ~cGZWinIMEKeyMessage() noexcept;      // 0x971ce1 (the vtable store); deleting 0x971d3e (slot 13)

    bool QueryInterface(uint32_t riid, void **out) override; // 0x971e95: 1, cIGZKeyMessage, cIGZMessage2
    uint32_t AddRef() override;                              // folded 0x148fb0 (+4)
    uint32_t Release() override;                             // folded 0x14a1f0: the deleting destructor at 1 or below
    bool Create(uint32_t iid, void **out) override;          // folded 0x1474b0: false
    uint32_t GetType() override;                             // folded 0x46390
    uint32_t SetType(uint32_t type) override;                // folded 0x14a260
    bool IsEqual(cIGZMessage2 *other) override;              // folded 0x148de0
    bool IsLess(cIGZMessage2 *other) override;               // folded 0x148e00
    void SetKeyEvent(uint32_t type, uint32_t key, uint32_t modifiers, uint32_t extra) override; // folded 0x149c70
    uint32_t GetEventType() override;                        // folded 0x145770
    uint32_t GetKey() override;                              // folded 0x149cf0
    uint32_t GetModifiers() override;                        // folded 0x149d00
    uint32_t GetExtra() override;                            // folded 0x148ba0

    cGZWinIMEKeyMessage(const cGZWinIMEKeyMessage &) = delete;
    cGZWinIMEKeyMessage &operator=(const cGZWinIMEKeyMessage &) = delete;

    uint32_t mRefCount;   // +0x04
    uint32_t mType;       // +0x08
    uint32_t mEventType;  // +0x0c (-1 until set)
    uint32_t mKey;        // +0x10
    uint32_t mModifiers;  // +0x14
    uint32_t mExtra;      // +0x18
};
static_assert(sizeof(cGZWinIMEKeyMessage) == 0x1c, "cGZWinIMEKeyMessage size");

using UnknownRef = cRZAutoRefCount<cIGZUnknown>;

// cGZWinIMEServer: the platform-neutral part (RTTI bases cIGZWinIMEServer +0, cRZSystemService +4 with cRZUnknownMT +8).
// 0x9c bytes. Its own count at +0x20 (AddRef / Release override the service's on every face).
class cGZWinIMEServer : public cIGZWinIMEServer, public rz::cRZSystemService {
public:
    static constexpr uint32_t kSize = 0x9c;
    static constexpr uint32_t kServiceId = 0x2ac45447;
    static constexpr int32_t kPriority = -999999; // (0xfff0bdc1; the proxy's is -1000000)
    static constexpr uint32_t kMessageServerId = 0x64063387, kMessageServerIid = 0xc1085722;

    cGZWinIMEServer();                    // 0x96cb1d
    ~cGZWinIMEServer() noexcept override; // 0x96cc96; deleting 0x96cd79 (+8 face slot 3, thunk 0x96cd0e)

    cGZWinIMEServer(const cGZWinIMEServer &) = delete;
    cGZWinIMEServer &operator=(const cGZWinIMEServer &) = delete;

    // cIGZUnknown (every face; the others through the compiler's thunks)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x96dc53
    uint32_t AddRef() override;                              // folded 0x85c074: the plain count at +0x20
    uint32_t Release() override;                             // 0x96dcf1: Destroy at a count of 1 or below

    // cIGZWinIMEServer (and the service face's Init / Shutdown)
    bool Init() override;                    // 0x96d9c9: the message servers; a pending enable is applied
    bool Shutdown() override;                // 0x96ec45
    bool IsAvailable() override;             // folded 0x147480: false
    bool IsEnabled() override;               // folded 0x418a40
    void SetEnabled(bool enable) override;   // 0x96e179
    bool IsOpen() override;                  // folded 0x144910: true
    bool SetOpen(bool open) override;        // folded 0x148260: true
    bool IsComposing() override;             // folded 0x95e9e3
    bool SetComposing(bool composing) override; // 0x96dec3: true
    cGZIMEComposition *GetComposition() override; // folded 0x49d5c0
    bool SetCompositionString(const char16_t *text, uint32_t length) override; // 0x96e0ec
    bool IsCandidateListOpen() override;     // 0x6ef784
    bool SetCandidateListOpen(bool open) override; // 0x96ddf8: true
    cGZIMECandidates *GetCandidates() override;   // folded 0x60a5b4
    bool SelectCandidate(uint32_t index) override; // 0x96de64: index below the candidate count

    // 0x96d040: Shutdown (through the +0 vtable), then the deleting destructor (through the +8 face).
    void Destroy();

    uint32_t mRefCount;             // +0x20
    uint32_t mServerId;             // +0x24 (the service id again; nothing reads it)
    bool mInitialized;              // +0x28
    bool mEnabled;                  // +0x29 (true from the constructor: Init applies it)
    UnknownRef mMessageServer;      // +0x2c service 0x64063387: slot 3 sends a cGZMessage
    UnknownRef mMessageServer2;     // +0x30 the cached message server (0x9c5a89): slot 3 posts a cIGZMessage2
    cGZIMEComposition mComposition; // +0x34
    cGZIMECandidates mCandidates;   // +0x70
};
static_assert(sizeof(cGZWinIMEServer) == cGZWinIMEServer::kSize, "cGZWinIMEServer size");
static_assert(offsetof(cGZWinIMEServer, mRefCount) == 0x20 && offsetof(cGZWinIMEServer, mEnabled) == 0x29 &&
                  offsetof(cGZWinIMEServer, mMessageServer2) == 0x30 && offsetof(cGZWinIMEServer, mComposition) == 0x34 &&
                  offsetof(cGZWinIMEServer, mCandidates) == 0x70,
              "cGZWinIMEServer layout");

// cGZWinIMEServerW32: the Win32 server (RTTI: cGZWinIMEServer +0, cIGZWinProcFilterW32 +0x9c). 0xa8 bytes.
class cGZWinIMEServerW32 : public cGZWinIMEServer, public cIGZWinProcFilterW32 {
public:
    static constexpr uint32_t kSize = 0xa8;
    static constexpr uint32_t kWindowServiceId = 0xa3460deb, kWindowServiceIid = 0x66b217a1;
    static constexpr uint32_t kWindowFilterIid = 0xe46397db; // the window service's face: slot 4 the window, 6 filters

    // The messages posted to mMessageServer (cGZMessage {id, data1, data2, data3}).
    static constexpr uint32_t kMsgStartComposition = 0x2ac44c0e;  // the composition, a pointer to a 1
    static constexpr uint32_t kMsgEndComposition = 0x2ac44c11;    // the composition, whether a result arrived
    static constexpr uint32_t kMsgCompositionChanged = 0x2ac44c14;
    static constexpr uint32_t kMsgCursorMoved = 0x2ac44c1a;
    static constexpr uint32_t kMsgResult = 0x2ac44c1b;           // after the result's key messages
    static constexpr uint32_t kMsgOpenCandidates = 0x2ac44ed1;   // no data
    static constexpr uint32_t kMsgCloseCandidates = 0x2ac44ed4;
    static constexpr uint32_t kMsgCandidatesChanged = 0x2ac44ed8;

    cGZWinIMEServerW32();                    // 0x971ca3
    ~cGZWinIMEServerW32() noexcept override; // 0x971ce8; deleting 0x971d5d (+8 face slot 3, thunk 0x971d36)

    // 0x970d15 (the factory the director registers): new, construct, the +4 face (null when new fails).
    static cIGZSystemService *__cdecl Create();

    // The +0 face's AddRef / Release / Init / Shutdown / IsComposing / GetComposition / IsCandidateListOpen /
    // GetCandidates are jump stubs to the base's bodies (0x971d7f, 0x971f1e, 0x971dea, 0x97249e, 0x971da9, 0x971de5,
    // 0x971d9f, 0x971da4): inherited. AddRef / Release are declared again only so they cover the filter face too.
    bool QueryInterface(uint32_t riid, void **out) override; // 0x971ed6: the filter face, else the base's
    uint32_t AddRef() override;
    uint32_t Release() override;

    bool IsAvailable() override;             // folded 0x144910: true
    void SetEnabled(bool enable) override;   // 0x9720d5: the filter added to / removed from the window service
    bool IsOpen() override;                  // 0x971dae
    bool SetOpen(bool open) override;        // 0x97204f
    bool SetComposing(bool composing) override; // 0x971fff
    bool SetCompositionString(const char16_t *text, uint32_t length) override; // 0x97208e
    bool SetCandidateListOpen(bool open) override; // 0x971f3e
    bool SelectCandidate(uint32_t index) override; // 0x971fa6

    // cIGZWinProcFilterW32
    intptr_t FilterMessage(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam, bool *handled) override; // 0x9724ab

    // 0x97231a: the composition from the input context (lParam's GCS_ flags: attributes, string, result, cursor).
    void ReadComposition(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam);
    // 0x9721a6: the candidate lists named by lParam's bits (the last one read wins).
    void ReadCandidates(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam);
    // 0x971df7: a character as key messages (type 4) to mMessageServer2, one per byte of its UTF-8 form.
    void PostCharacter(void *window, uint16_t character, uint32_t extra, uint32_t keyFlags);

    // The game's global 0x10f4e90 (set by the constructor, cleared by the destructor; nothing in the image reads it).
    static cGZWinIMEServerW32 *sInstance;

    uint32_t mUnusedA0;  // +0xa0 (zeroed by the constructor; nothing reads it)
    void *mWindow;       // +0xa4 the main window (from the window service while enabled)
};
static_assert(sizeof(cGZWinIMEServerW32) == cGZWinIMEServerW32::kSize, "cGZWinIMEServerW32 size");
static_assert(offsetof(cGZWinIMEServerW32, mUnusedA0) == 0xa0 && offsetof(cGZWinIMEServerW32, mWindow) == 0xa4,
              "cGZWinIMEServerW32 layout: the filter face at +0x9c");
