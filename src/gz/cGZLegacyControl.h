#pragma once
// cGZLegacyControl (lane MC, foundations F5): one window of a UI script (COM class 0xfa450242, made by the creator
// 0x85b2b3 the UIScript director registers). The UI script service makes one per LEGACY tag and hands it the tag's
// attribute list (Create, slot 3): the control makes the window that list names (a GZCOM class), fills it in from the
// attributes (the window class's handler, then the properties every window has, ApplyCommonProperties 0x86549d), hangs
// it under its parent (or the main window) and keeps it. 0x20 bytes.
//
// Faces (RTTI order): +0 cIGZScriptableControl (game vtable 0xe482a4, 11 slots: slot 10 the deleting destructor), +4
// cIGZRenderControl (0xe482d4, 7), +8 cIGZLegacyControl (0xe482f4, 8). The constructor 0x85d62d (reached only from the
// creator's tail jump) and the destructor 0x85d82b are the only code storing the vtables, so with the creator hooked
// every control carries ours.
//
// The property tables (filled by the 22 registration functions the first constructor runs, RunRegistrations; foundations
// F6 reimplements them): per window class, the script vocabulary (the shared one, kVocabCell) maps the class's script
// names to ids (its class id, its type id, its property ids); kTypesCell maps the class id to the type id, and
// kHandlersCell the type id to the class's handler (a LegacyHandler: it applies the class's own properties to a new
// window, apply true, and writes them back, false).
#include "gz/cGZScriptTerp.h"
#include "gz/cGZScriptableControlSet.h"
#include "rz/cRZCmdLine.h" // cRZStringVector
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>

namespace ifaces {
class cIGZWin;
}

// A window class's handler: `apply` true applies the class's properties from `attributes` to `window`, false the
// pass after the window is placed (cdecl; the result is not read).
using LegacyHandler = bool(__cdecl *)(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);
// The game's tables (MSVC std::map: the head pointer at +0, the size at +4; nodes left, parent, right, colour, nil, then
// the key at +0x10 and the value at +0x14), the same layout as ours, so ours reads them in place.
using LegacyTypeMap = std::map<uint32_t, uint32_t>;
using LegacyHandlerMap = std::map<uint32_t, LegacyHandler>;
static_assert(sizeof(LegacyTypeMap) == 8, "the game's std::map is the head and the size");

// A window's 0x5386d516 face (cGZWinProcMgr::kWinIid): slot 7 gives it a window procedure (null clears it).
class cIGZWinProcHost : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x5386d516;
    virtual void vf3_cIGZWinProcHost() = 0;
    virtual void vf4_cIGZWinProcHost() = 0;
    virtual void vf5_cIGZWinProcHost() = 0;
    virtual void vf6_cIGZWinProcHost() = 0;
    virtual bool SetWinProc(cIGZUnknown *proc) = 0; // 7
};

// 0x86401e (cdecl): the first attribute with this id whose value is a uint32 variant (type 6) writes its value to
// *out; false (out untouched) without one.
bool __cdecl FindUint32Attribute(const cGZScriptAttributes *attributes, uint32_t id, uint32_t *out);

// ---- the attribute readers the window classes' handlers share (lane MD, foundations F5b; all cdecl) ----------------
// A cRZString local as the readers' callers keep one: constructed empty (0x144f5b), destroyed with cRZString's body.
struct LegacyText {
    union {
        cRZString mText;
    };
    LegacyText() { mText.Construct(); }
    LegacyText(const LegacyText &) = delete;
    LegacyText &operator=(const LegacyText &) = delete;
    ~LegacyText() { mText.Destruct(); }
    const char *Chars() const { return mText.ToChar(); }
};

// 0x863fae: *out emptied; then the first attribute with this id whose value is a text variant (type 0x800e) writes its
// text into *out (the value's GetValString, flags 0, separator ' ') and that answer is returned; false without one.
bool __cdecl FindTextAttribute(const cGZScriptAttributes *attributes, uint32_t id, cRZString *out);
// 0x863e87: the attribute's text read as "(%d,%d)"; true when both numbers were read (each read one is written).
bool __cdecl FindPairAttribute(const cGZScriptAttributes *attributes, uint32_t id, int32_t *first, int32_t *second);
// 0x863e06: the attribute's text read as "(%d,%d,%d)"; when all three were read, their low bytes go to the outs (one
// byte each) and the answer is true; otherwise nothing is written.
bool __cdecl FindColorAttribute(const cGZScriptAttributes *attributes, uint32_t id, uint8_t *red, uint8_t *green,
                                uint8_t *blue);
// 0x864077: any attribute with this id, whatever its value.
bool __cdecl HasAttribute(const cGZScriptAttributes *attributes, uint32_t id);
// 0x86dceb: the text resource {0x2026960b, group, instance, 0} into *out: through the UI script service's context
// (when there is one) or, without one, the resource manager's cIGZString resource assigned to *out.
bool __cdecl LoadLocalizedText(uint32_t group, uint32_t instance, cIGZString *out);
// 0x87b35c: with a window, its SetProperty of `id` with a new untyped variant of the 16-byte key (not released).
void __cdecl SetResourceKeyProperty(ifaces::cIGZWin *window, uint32_t id, const void *key);
// 0x87b143 (lane NH): the window's property `id` is a list of 16-byte keys kept in an untyped variant; `key` goes in at
// `index` (0 .. the list's length). Without the property a new variant holds just the key (true). With it the variant's
// bytes are rebuilt with the key inserted (true), unless `index` is outside 0 .. length or the variant has no bytes (false;
// the variant is left alone). The window must not be null. The rebuilt list is put together in a 32-entry buffer on the stack,
// or in a heap block when it has 32 keys or more (the original's small-buffer helper 0x87b2ac).
bool __cdecl AppendFileBrowserKey(ifaces::cIGZWin *window, uint32_t id, int32_t index, const void *key);
// 0x86549d: the properties every window has. `apply` true sets the window's flags, id, area, colour, caption, caption
// key and tooltips from the attributes; false applies the late flags and the typed properties ("id,type,value").
void __cdecl ApplyCommonProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// The UI script service's context (its slot 9, set through slot 10): slot 3 loads an image resource (a counted image
// object, null when there is none; the legacy controls' handlers), slot 4 a text resource into a string.
class cIGZUIScriptContext : public cIGZUnknown {
public:
    virtual cIGZUnknown *LoadImage(const void *key) = 0; // slot 3 (ret 4)
    virtual bool LoadText(const void *key, cIGZString *out) = 0; // slot 4 (ret 8)
};

// The tooltip texts ApplyCommonProperties hands a window (property 0xbaca6879, inside a variant): the game's
// cRZUnknownAutoObject<std::vector<cRZString>> (vtable 0xe486ac, 0x14 bytes), a counted vector of the eight strings.
// Its vtable is referenced only by 0x86549d, so with that ours every list carries our vtable.
class cGZLegacyTextList : public cIGZUnknown {
public:
    static constexpr uint32_t kSize = 0x14;
    cGZLegacyTextList() = default;
    bool QueryInterface(uint32_t riid, void **out) override; // shared 0x1b945c: iid 1 only, counted
    uint32_t AddRef() override;                              // folded shared_vf_146a40
    uint32_t Release() override;                             // 0x86de0b: the last count frees the strings and itself
    cGZLegacyTextList(const cGZLegacyTextList &) = delete;
    cGZLegacyTextList &operator=(const cGZLegacyTextList &) = delete;

    cRZStringVector mTexts{};  // +4
    uint32_t mRefCount = 0;    // +0x10
};

class cGZLegacyControl : public cIGZScriptableControl, public cIGZRenderControl, public cIGZLegacyControlWindow {
public:
    static constexpr uint32_t kVTable = 0xe482a4; // the game's, for tests
    static constexpr uint32_t kRenderVTable = 0xe482d4;
    static constexpr uint32_t kLegacyVTable = 0xe482f4;
    static constexpr uint32_t kClassId = 0xfa450242;
    static constexpr uint32_t kSize = 0x20;
    static constexpr uint32_t kVocabCell = 0x10e6890;    // cIGZScriptVocab *, shared by every control
    static constexpr uint32_t kHandlersCell = 0x10e6894; // LegacyHandlerMap
    static constexpr uint32_t kTypesCell = 0x10e689c;    // LegacyTypeMap
    static constexpr uint32_t kClassIdAttribute = 0xffffffff;
    static constexpr uint32_t kTypeIdAttribute = 0xfffffffe;
    static constexpr uint32_t kDefaultWindowClass = 0x478d1e6f;
    static constexpr uint32_t kWinIid = 0x22ba0121;
    static constexpr uint32_t kClassIdProperty = 0xe85eebde; // the window's SetProperty ids
    static constexpr uint32_t kTypeIdProperty = 0x08d6e462;
    static constexpr uint32_t kAnchorFlags = 0x3800010;
    static constexpr uint32_t kVocabClass = 0xba2e7954; // cGZScriptVocab

    // 0x85d62d: counted 0, no window. The first control (no shared vocabulary yet) makes the vocabulary (GZCOM class
    // 0xba2e7954) and, when that worked, runs the registrations; later ones count it.
    cGZLegacyControl();
    // 0x85d82b: an owned window taken from its parent (slot 20) and torn down (slot 5), then released; the parent window
    // and the pending window procedure released; the shared vocabulary released (the cell cleared when that was the last
    // count). Slot 10 the deleting destructor 0x85da06.
    virtual ~cGZLegacyControl();
    cGZLegacyControl(const cGZLegacyControl &) = delete;
    cGZLegacyControl &operator=(const cGZLegacyControl &) = delete;

    // 0x85b2b3 (cdecl, the UIScript director's factory): a new control (null when new failed), not counted.
    static cIGZScriptableControl *__cdecl Create();

    // cIGZUnknown (+4 and +8 through the thunks 0x86ddfb / 0x86de03, 0x1f4daf / 0x1f4db7, 0x86dee0 / 0x86dee8)
    bool QueryInterface(uint32_t riid, void **out) override; // 0x86dd98: 1 and 0x1a356083, render, legacy; counted
    uint32_t AddRef() override;                              // folded 0x15e520
    uint32_t Release() override;                             // 0x86dec9

    // cIGZScriptableControl
    // 0x86dfdc: the window for the attribute list (see the .cpp); false always (the original never returns true).
    bool Create(cGZScriptAttributes *attributes) override;
    cIGZUnknown *GetVocab() override;                        // 0x863dee: the shared vocabulary, not counted
    uint32_t GetId() override;                               // 0x863df4: the window's GetID, 0 without one
    void SetId(uint32_t id) override;                        // 0x86dfc2: the window's SetID
    void SetOwnsWindow(bool owns) override;                  // folded 0x2281f0 (legacy slot 4: thunk 0x86dfd4)
    bool Bind(cIGZScriptableControlSet *set, uint32_t index, uint32_t iid, uint32_t arg) override; // folded 0x1ac160
    bool SetParent(cIGZUnknown *parent) override;            // 0x86e211: the parent control's window, counted

    // cIGZRenderControl
    bool Render(void *context) override;                     // folded 0x148260: true
    bool SetRect(const cGZRect *rect) override;              // 0x86def0: the window's SetAreaRect
    bool GetRect(cGZRect *rect) override;                    // 0x863b87: the window's GetArea
    bool HitTest(const cGZPoint *point, void *arg) override; // folded 0x1477b0: true

    // cIGZLegacyControl
    cIGZUnknown *GetWindow() override;                       // folded 0x46390
    void SetWindow(cIGZUnknown *window) override;            // 0x863b5b
    void SetWinProc(cIGZUnknown *proc) override;             // 0x86e29c
    bool GetScript(cIGZString **out) override;               // 0x86e758

    // 0x863aec (thiscall, ret 8): with `apply`, the window's 0x5386d516 face (if any) gets the pending window procedure
    // (its slot 7), which is then released; without, nothing.
    void HandWinProc(cIGZUnknown *window, bool apply);

    uint32_t mRefCount = 0;              // +0x0c
    cIGZUnknown *mWindow = nullptr;      // +0x10 (a cIGZWin)
    cIGZUnknown *mParentWindow = nullptr; // +0x14: until Create hangs the window under it
    cIGZUnknown *mWinProc = nullptr;     // +0x18: until Create hands it to the window
    bool mOwnsWindow = false;            // +0x1c
};
static_assert(sizeof(cGZLegacyTextList) == cGZLegacyTextList::kSize, "cGZLegacyTextList size");
static_assert(offsetof(cGZLegacyTextList, mTexts) == 4, "cGZLegacyTextList layout");
static_assert(offsetof(cGZLegacyTextList, mRefCount) == 0x10, "cGZLegacyTextList layout");
static_assert(sizeof(cGZLegacyControl) == cGZLegacyControl::kSize, "cGZLegacyControl size");
static_assert(offsetof(cGZLegacyControl, mRefCount) == 0xc, "cGZLegacyControl layout");
static_assert(offsetof(cGZLegacyControl, mWindow) == 0x10, "cGZLegacyControl layout");
static_assert(offsetof(cGZLegacyControl, mParentWindow) == 0x14, "cGZLegacyControl layout");
static_assert(offsetof(cGZLegacyControl, mWinProc) == 0x18, "cGZLegacyControl layout");
static_assert(offsetof(cGZLegacyControl, mOwnsWindow) == 0x1c, "cGZLegacyControl layout");
