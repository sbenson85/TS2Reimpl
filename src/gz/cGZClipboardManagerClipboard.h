#pragma once
// The clipboard object of the clipboard manager (standalone shape): cGZClipboardManagerClipboard (game vtable 0xe9eda4,
// 21 slots, 0x2c bytes), its Win32 clipboard viewer (0x1c bytes, no vtable), and the message-server cache its
// notifications go through.
//
// A clipboard holds a map from format to clipboard data (cIGZClipboardData, counted) behind its own lock. Clipboard 0
// (the one the manager's Init makes) also mirrors text to and from the system clipboard through the viewer: a hidden
// window in the system's clipboard-viewer chain that reads the system's text into the clipboard when it changes, and
// writes the clipboard's text to the system when it is set. Changes are announced through the message server (message
// 0x87ec0025: the clipboard, its id and the format, or true / false from Init / Shutdown) when notification is on.
//
// Construction: the constructor 0xc6d26f is hooked to a placement adapter (its only caller, the manager's Init, is ours
// and builds the class directly); the viewer is made only by the clipboard (0xc6dbc2 / 0xc6d2cf), and its window
// procedure 0xc6e73f is reached only through the window class our viewer registers. Every slot is ours through our
// vtable.
//
// The Win32 calls go through the game's import slots (game/import.h).
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>
#include <map>

class cIGZMessageServer;
struct ScopedRef;
namespace ifaces {
class cIGZClipboardData;
}

// ---- the message server cache ----

// The message server service (id 0x64063387, face cIGZMessageServer 0xc1085722).
constexpr uint32_t kMessageServerServiceId = 0x64063387;
// The cached message server of 0x9c5ab4 (uncounted).
constexpr uint32_t kMessageServerCell = 0x10f5198;

// 0x762db4 (__thiscall on a ScopedRef): null, then the framework's GetSystemService for the message server into it
// (nothing without a framework). Returns ref.
ScopedRef *AcquireMessageServer(ScopedRef *ref);
// 0x9c5ab4 (__cdecl): the cached message server; when the cell is empty, acquired into a local reference, stored and the
// local released, so the cell holds no reference of its own. Re-reads the cell.
cIGZMessageServer *GetMessageServer();

// ---- the clipboard ----

// The clipboard's interface (RTTI cIGZClipboard, iid 0x87ec000b). Formats are numbers; 0x67ec001d is text (the data's
// object answers cIGZString's iid 0x89b7dc8). -1 as a format means every format.
class cIGZClipboard : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x87ec000b;
    virtual bool Init() = 0;                                                  // slot 3
    virtual bool Shutdown() = 0;                                              // slot 4
    virtual uint32_t GetId() = 0;                                             // slot 5 (folded 0x145770)
    virtual void SetId(uint32_t id) = 0;                                      // slot 6
    virtual bool GetNotify() = 0;                                             // slot 7 (folded 0x24b3e0)
    virtual void SetNotify(bool notify) = 0;                                  // slot 8
    virtual uint32_t GetChangeCount() = 0;                                    // slot 9 (folded 0x149d00)
    virtual bool HasFormat(uint32_t format) = 0;                              // slot 10
    // With a null `formats`, *count = the number of formats; otherwise up to *count formats (*count = how many).
    virtual uint32_t GetFormats(uint32_t *formats, uint32_t *count) = 0;      // slot 11
    // The data's QueryInterface (false without data for the format).
    virtual bool QueryData(uint32_t format, uint32_t iid, void **out) = 0;    // slot 12
    virtual ifaces::cIGZClipboardData *GetData(uint32_t format) = 0;          // slot 13: uncounted
    virtual bool SetData(ifaces::cIGZClipboardData *data, bool clear) = 0;    // slot 14
    virtual void RemoveData(uint32_t format) = 0;                             // slot 15
    virtual bool Slot16(uint32_t a1, uint32_t a2, uint32_t a3) = 0;           // slot 16: false
    virtual bool GetText(cIGZString *out) = 0;                                // slot 17
    virtual bool Slot18(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) = 0; // slot 18: false
    virtual bool SetText(const cIGZString &text, bool clear) = 0;             // slot 19
};

class cGZClipboardViewer;

class cGZClipboardManagerClipboard : public cIGZClipboard {
public:
    static constexpr uint32_t kSize = 0x2c;
    static constexpr uint32_t kTextFormat = 0x67ec001d;
    static constexpr uint32_t kAllFormats = 0xffffffff;
    static constexpr uint32_t kNoId = 0xfffffffd;
    static constexpr uint32_t kChangedMessage = 0x87ec0025;

    cGZClipboardManagerClipboard();            // 0xc6d26f
    virtual ~cGZClipboardManagerClipboard();   // 0xc6d3c1; slot 20 the deleting destructor 0xc6d437

    cGZClipboardManagerClipboard(const cGZClipboardManagerClipboard &) = delete;
    cGZClipboardManagerClipboard &operator=(const cGZClipboardManagerClipboard &) = delete;

    bool QueryInterface(uint32_t riid, void **ppv) override;                     // 0xc6dd35
    uint32_t AddRef() override;                                                  // 0xc6d50d
    uint32_t Release() override;                                                 // 0xc6e1a0
    bool Init() override;                                                        // 0xc6db80
    bool Shutdown() override;                                                    // 0xc6e689
    uint32_t GetId() override;                                                   // folded 0x145770
    void SetId(uint32_t id) override;                                            // 0xc6e634
    bool GetNotify() override;                                                   // folded 0x24b3e0
    void SetNotify(bool notify) override;                                        // 0xc6e31b
    uint32_t GetChangeCount() override;                                          // folded 0x149d00
    bool HasFormat(uint32_t format) override;                                    // 0xc6dc62
    uint32_t GetFormats(uint32_t *formats, uint32_t *count) override;            // 0xc6d6e6
    bool QueryData(uint32_t format, uint32_t iid, void **out) override;          // 0xc6d832
    ifaces::cIGZClipboardData *GetData(uint32_t format) override;                // 0xc6d877
    bool SetData(ifaces::cIGZClipboardData *data, bool clear) override;          // 0xc6e399
    void RemoveData(uint32_t format) override;                                   // 0xc6d54e
    bool Slot16(uint32_t a1, uint32_t a2, uint32_t a3) override;                 // 0xc6da06
    bool GetText(cIGZString *out) override;                                      // 0xc6d99c
    bool Slot18(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) override;    // 0xc6e61c
    bool SetText(const cIGZString &text, bool clear) override;                   // 0xc6e596

    // 0xc6dbc2: the viewer, for clipboard 0 when it has none. Returns true.
    bool CreateViewer();
    // 0xc6e6d4: the viewer out of the chain and its window destroyed, then freed. Returns true.
    bool DestroyViewer();
    // 0xc6dc94: announces a change of `format` (posted), unless notification is off, suppressed or the format is 0.
    void NotifyChanged(uint32_t format);
    // 0xc6e2e0: announces Init (true) or Shutdown (false) (sent), when notification is on.
    void NotifyState(bool up);

    bool mInitialised;                 // 0x04
    bool mNotify;                      // 0x05 (bytes 6 and 7 are never written)
    uint32_t mRefCount;                // 0x08 changed under the lock; starts at 0
    uint32_t mId;                      // 0x0c kNoId until the manager sets it
    int32_t mNotifySuppressed;         // 0x10 raised around the removals SetData makes
    uint32_t mChangeCount;             // 0x14
    cGZClipboardViewer *mViewer;       // 0x18 clipboard 0's, between Init and Shutdown
    std::map<uint32_t, ifaces::cIGZClipboardData *> mData; // 0x1c format -> data (counted)
    cRZCriticalSection mLock;          // 0x24
};

static_assert(sizeof(cGZClipboardManagerClipboard) == cGZClipboardManagerClipboard::kSize, "cGZClipboardManagerClipboard size");
static_assert(offsetof(cGZClipboardManagerClipboard, mInitialised) == 0x4 && offsetof(cGZClipboardManagerClipboard, mNotify) == 0x5 &&
                  offsetof(cGZClipboardManagerClipboard, mRefCount) == 0x8 && offsetof(cGZClipboardManagerClipboard, mId) == 0xc &&
                  offsetof(cGZClipboardManagerClipboard, mNotifySuppressed) == 0x10 &&
                  offsetof(cGZClipboardManagerClipboard, mChangeCount) == 0x14 && offsetof(cGZClipboardManagerClipboard, mViewer) == 0x18 &&
                  offsetof(cGZClipboardManagerClipboard, mData) == 0x1c && offsetof(cGZClipboardManagerClipboard, mLock) == 0x24,
              "cGZClipboardManagerClipboard layout");

// The system clipboard viewer of clipboard 0 (0x1c bytes): a hidden window ("Win32ClipboardSupport", class
// "Win32ClipboardSupportClass", the game's strings) with the viewer in its GWL_USERDATA, linked into the clipboard-viewer
// chain. The two counters keep the mirror from echoing: text read from the system is not written back, and the system's
// change notice for text just written is not read.
class cGZClipboardViewer {
public:
    explicit cGZClipboardViewer(cGZClipboardManagerClipboard *owner); // 0xc6d2cf
    // 0xc6d3f0: out of the chain and the window destroyed (not a destructor: DestroyViewer frees the block after it).
    void Detach();
    // 0xc6dd9a: the system's text (Unicode when offered, else ANSI) into `out` as UTF-8; true when there was some.
    bool ReadText(cIGZString *out);
    // 0xc6e80f: `text` (UTF-8) onto the system clipboard as ANSI and Unicode text; true when the clipboard opened.
    bool WriteText(cIGZString *text);
    // 0xc6e73f: the window procedure (WM_CHANGECBCHAIN, WM_DRAWCLIPBOARD; the rest to DefWindowProcA).
    static intptr_t __stdcall WindowProc(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam);

    bool mEnabled;                        // 0x00 set by the constructor (bytes 1..3 are never written)
    int32_t mReading;                     // 0x04 raised while text read from the system is handed to the owner
    int32_t mWriting;                     // 0x08 raised while text is written to the system
    cGZClipboardManagerClipboard *mOwner; // 0x0c
    void *mWindow;                        // 0x10
    void *mNextViewer;                    // 0x14 the next window in the viewer chain
    uint32_t mLanguage;                   // 0x18 0x409, never read
};

static_assert(sizeof(cGZClipboardViewer) == 0x1c, "cGZClipboardViewer size");
static_assert(offsetof(cGZClipboardViewer, mOwner) == 0xc && offsetof(cGZClipboardViewer, mNextViewer) == 0x14,
              "cGZClipboardViewer layout");

// The constructor hook's entry point (docs/standalone-shape.md): `this` in ecx, the dummy edx.
struct cGZClipboardManagerClipboardAdapters {
    static cGZClipboardManagerClipboard *__fastcall Construct(void *memory, void *edx); // 0xc6d26f
    // 0x762db4's entry (__thiscall: the reference in ecx).
    static ScopedRef *__fastcall AcquireMessageServer(ScopedRef *ref, void *edx);
    // 0xc6d2a5 (__thiscall, ret 8): a cGZClipboardManagerClipboardData (src/misc/ClassesRound4.h, game vtable 0xe9ed84)
    // holding `object` (counted) under `format`, its own count 0. Only SetText (ours) calls it.
    static ifaces::cIGZClipboardData *__fastcall ConstructData(void *memory, void *edx, uint32_t format, cIGZUnknown *object);
};
