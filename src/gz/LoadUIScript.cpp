// LoadUIScript and its window helpers (lane MD, foundations F5b): src/gz/LoadUIScript.h. Built for speed, as the game's.
#include "rz/RZMessageServer.h"
#include "gz/LoadUIScript.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZLegacyControl.h"          // cIGZWinProcHost
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cGZScriptableControlSet.h"   // cIGZScriptableControlSet
#include "gz/cGZUIScriptService.h"        // cIGZUIScriptService, cIGZUILegacyScriptService
#include "gz/cGZWinMgrW32.h"              // cIGZWinMgr
#include "gz/cGZPropertySet.h"       // gzany:: type ids
#include "gz/cIGZAny.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPropertySet.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cIGZWin.h"
#include "ifaces/cIGZWinScrollbar2.h"
#include "misc/HexParse.h"
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/cIGZVariant.h"
#include "rz/cRZIniFile.h" // cRZIniStringHelpers::TrimBoth
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"

#include <intrin.h>

#include <cstdio>
#include <new>
#include <string>
#include <vector>

// The thread-safe statics helpers of our CRT, used on the game's guard (as src/rz/COMDirectors.cpp).
extern "C" void __cdecl _Init_thread_header(int *target) noexcept;
extern "C" __declspec(thread) int _Init_thread_epoch; // our CRT's statics epoch, not the image's TLS block (docs/standalone-shape.md, "Owned statics")
extern "C" void __cdecl _Init_thread_footer(int *target) noexcept;
extern "C" void __cdecl _Init_thread_abort(int *target) noexcept;

#pragma warning(disable : 4996) // sprintf, as the game calls it

namespace {

// ---- game words ----
constexpr uint32_t kSettingsCell = 0x10fb348;  // EvaluateWindowCondition's settings face (a function-local static)
constexpr uint32_t kSettingsGuard = 0x10fb34c; // its guard
constexpr uint32_t kScreenWidthCell = 0x10fb340;
constexpr uint32_t kScreenHeightCell = 0x10fb344;

// ---- services ----
constexpr uint32_t kSettingsId = 0x0ac1887e;  // the property manager
constexpr uint32_t kSettingsIid = 0x6bb22f38; // its face whose slot 3 is the string-keyed property set
constexpr uint32_t kResourceManagerId = 0x056b906e;
constexpr uint32_t kResourceManagerIid = 0x656b8efc;

// ---- interfaces asked of a script's windows ----
constexpr uint32_t kButtonIid = 0x8810;        // cIGZWinBtn: slot 3 is its window
constexpr uint32_t kTextEditIid = 0x231a1c57;  // cGZWinTextEdit's face: slot 5 its window, 95 / 96 attach scrollbars
constexpr uint32_t kScrollbarIid = 0x4b42da53; // cIGZWinScrollbar2

// ---- the script ----
constexpr uint32_t kScriptGroup = 0xa99d8a11;
constexpr char kScreenGroupFormat[] = "0x%.4u%.4u";
constexpr uint32_t kMinimumWidth = 0x400;
constexpr uint32_t kMinimumHeight = 0x300;
constexpr uint32_t kFallbackWidth = 800;
constexpr uint32_t kFallbackHeight = 600;

// ---- window properties and flags ----
constexpr uint32_t kAnnounceProperty = 0x30000ef;
constexpr uint32_t kVisibleCondition = 0x30000f0;
constexpr uint32_t kEnabledCondition = 0x30000f1;
constexpr uint32_t kShownCondition = 0x30000f2;
constexpr uint32_t kNotifyProperty = 0x30000f3;
constexpr uint32_t kVisibleFlag = 1;
constexpr uint32_t kEnabledFlag = 2;
constexpr uint32_t kButtonWindowFlag = 0x8000;

// ---- win proc host slot 6's arguments ----
constexpr uint32_t kWinProcHostMode = 4;
constexpr uint32_t kWinProcHostModeArg = 0;

// ---- scrollbars ----
constexpr uint32_t kFirstScrollbarId = 0;
constexpr uint32_t kSecondScrollbarId = 1;
constexpr uint32_t kVertical = 1;

// ---- messages ----
constexpr uint32_t kAskConditionMessage = 0x8e9dce8e;
constexpr uint32_t kNotifyMessage = 0x0f70f296;
constexpr uint32_t kAnnounceMessage = 0x0e9dd622;

// ---- condition text ----
constexpr char kTermSeparator = ' ';
constexpr char kNotEqualText[] = "!=";
constexpr char kEqualText[] = "=";
constexpr char kNotEqualMark = '!';
constexpr char kEqualMark = '=';
constexpr char kAndText[] = "and";
constexpr char kOrText[] = "or";
constexpr char kValueSeparator = ' ';

ifaces::cIGZWin *Win(cIGZUnknown *window) { return As<ifaces::cIGZWin>(window); }
cIGZFrameWork *Framework() { return static_cast<cIGZFrameWork *>(GetServiceObject()); }

// The cached message server (0x9c5a89, an original callee nobody has replaced; cell 0x10f519c).
ifaces::cIGZMessageServer *MessageServer()
{
    return As<ifaces::cIGZMessageServer>(GetCachedMessageServer());
}

// The cached cheat console (0xb38f10, an original callee; cell 0x10faf68). Its slot 3 is the converter whose slots parse
// a text: 30 a bool (al), 31 a float (ST0), 32 a sint32, 33 a uint32.
void *Console() { return OriginalFunction<void *(__cdecl *)(), 0xb38f10>()(); }

// A cRZString on the stack (the game's vtable), destroyed at the end of its scope.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    LocalString(const char *data, uint32_t length)
    {
        s.Construct();
        s.mString.assign(data, length);
    }
    explicit LocalString(const std::string &text) : LocalString(text.data(), uint32_t(text.size())) {}
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// A heap string of the game's class (refcount 0) holding a copy of `text`; null when the allocation fails.
cRZString *NewString(const cRZString &text)
{
    auto *memory = static_cast<cRZString *>(::operator new(sizeof(cRZString), std::nothrow));
    return memory ? memory->Construct(text, 0, 0xffffffffu) : nullptr;
}

void TrimBoth(cRZString &text) { reinterpret_cast<cRZIniStringHelpers *>(&text)->TrimBoth(); }

// The cRZString helper 0x1ad622's split (as src/ts/DynamicLibraryList.size.cpp): the text before the first `delimiter`
// into `head`, removed from `text` with the delimiter; without one, all of `text` moves to `head` (head cleared, then
// the two swapped).
void SplitAt(std::string &text, char delimiter, std::string &head)
{
    const size_t at = text.find(delimiter);
    if (at == std::string::npos) {
        head.erase(0);
        text.swap(head);
        return;
    }
    head.assign(text, 0, at);
    text.erase(0, at + 1);
}

// A standard message from RZMemory (0x2c bytes, constructed), counted when the allocation worked and used untested
// (a failed allocation faults on the first call, as the original); released at the end of the scope.
struct Message {
    ifaces::cIGZMessage2Standard *m = nullptr;
    Message()
    {
        if (void *memory = RZMemory::Malloc(cRZMessage2Standard::kSize))
            m = reinterpret_cast<ifaces::cIGZMessage2Standard *>(static_cast<cRZMessage2Standard *>(memory)->Construct());
        if (m) m->AddRef();
    }
    ~Message()
    {
        if (m) m->Release();
    }
    Message(const Message &) = delete;
    Message &operator=(const Message &) = delete;
};

// A counted local released (when set) at the end of its scope.
template <class T> struct Held {
    T *p = nullptr;
    Held() = default;
    explicit Held(T *object) : p(object) {}
    ~Held() { Clear(); }
    void Clear()
    {
        if (p) reinterpret_cast<cIGZUnknown *>(p)->Release();
    }
    void **Out() { return reinterpret_cast<void **>(&p); }
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
};

// The settings face, a function-local static of the game's (cell and guard in the game's storage, the thread's epoch
// our CRT's): asked of the framework once (the reference handed back at once, so the static holds none).
void *Settings()
{
    int *guard = GameAddressOpaque<int>(kSettingsGuard); // opaque: ICE with the constant form (game.h)
    void **settings = GameAddress<void *>(kSettingsCell);
    if (*guard > _Init_thread_epoch) {
        _Init_thread_header(guard);
        if (*guard == -1) {
            try {
                Held<cIGZUnknown> service;
                if (cIGZFrameWork *framework = Framework()) framework->GetSystemService(kSettingsId, kSettingsIid, service.Out());
                *settings = service.p;
            } catch (...) {
                _Init_thread_abort(guard);
                throw;
            }
            _Init_thread_footer(guard);
        }
    }
    return *settings;
}

// The payload view of a cIGZAny value (vtable, then the value).
struct AnyValue {
    const void *mVTable;
    union {
        uint32_t mUint32;
        int32_t mSint32;
        uint8_t mBoolean;
        float mFloat32;
        cIGZString *mString;
    };
};

// One term "name=value" (or "!=") against a setting: the value parsed as the setting's type and compared. False in
// `known` for a type it cannot compare.
bool CompareSetting(const cIGZAny *setting, const cRZString &value, void *console, bool *known)
{
    const auto *any = reinterpret_cast<const AnyValue *>(setting);
    *known = true;
    switch (setting->GetType()) {
    case gzany::kTypeString: {
        cIGZString *text = any->mString;
        const uint32_t length = text->Strlen();
        LocalString copy(text->Data(), length);
        return value.IsEqualForward(*copy.s.AsInterface(), false);
    }
    case gzany::kTypeSint32: {
        void *converter = RawSlot<3, void *>(console);
        return RawSlot<32, int32_t>(converter, value.ToChar()) == any->mSint32;
    }
    case gzany::kTypeUint32: {
        void *converter = RawSlot<3, void *>(console);
        return RawSlot<33, uint32_t>(converter, value.ToChar()) == any->mUint32;
    }
    case gzany::kTypeBoolean: {
        void *converter = RawSlot<3, void *>(console);
        return RawSlot<30, uint8_t>(converter, value.ToChar()) == any->mBoolean;
    }
    case gzany::kTypeFloat32: {
        void *converter = RawSlot<3, void *>(console);
        return RawSlot<31, float>(converter, value.ToChar()) == any->mFloat32;
    }
    default:
        *known = false;
        return false;
    }
}

// One term whose name is no setting: asked of the message server (the name as the message's string, the value as its
// object; the answer is data 1). False in `sent` when the server refuses the message; no server answers false.
bool AskServer(const cRZString &name, const cRZString &value, bool *sent)
{
    *sent = true;
    if (!MessageServer()) return false;
    Message message;
    message.m->SetType(kAskConditionMessage);
    message.m->SetData1(0);
    message.m->SetExtra(NewString(name));
    message.m->SetObject(NewString(value));
    if (!MessageServer()->MessageSend(message.m)) {
        *sent = false;
        return false;
    }
    return message.m->GetData1() != 0;
}

// The condition text evaluated into *out (EvaluateWindowCondition after the property was found).
bool EvaluateText(cIGZVariant *property, uint8_t *out)
{
    LocalString text;
    property->GetValString(*text.s.AsInterface(), 0, kValueSeparator);
    void *const console = Console();
    void *const settings = Settings();
    if (!console || !settings) return false;

    std::vector<std::string> terms;
    while (!text.s.mString.empty()) {
        std::string term;
        SplitAt(text.s.mString, kTermSeparator, term);
        terms.push_back(std::move(term));
    }

    LocalString joiner;
    for (const std::string &term : terms) {
        LocalString value(term);
        LocalString name;
        bool negate = false;
        const uint32_t length = value.s.Strlen();
        if (value.s.mString.find(kNotEqualText, 0) < length) {
            negate = true;
            SplitAt(value.s.mString, kNotEqualMark, name.s.mString);
            value.s.Erase(0, 1); // the '=' after the '!'
        } else if (value.s.mString.find(kEqualText, 0) < length) {
            SplitAt(value.s.mString, kEqualMark, name.s.mString);
        } else {
            joiner.s.mString.assign(value.s.mString.data(), value.s.mString.size());
            TrimBoth(joiner.s);
            continue;
        }
        TrimBoth(name.s);
        TrimBoth(value.s);

        bool equal = false;
        bool known = false;
        {
            GZServiceString key(name.s.ToChar());
            known = RawSlot<3, cIGZPropertySetT<cKeyStringTraits> *>(settings)->HasProperty(&key);
        }
        if (known) {
            const cIGZAny *setting;
            {
                GZServiceString key(name.s.ToChar());
                setting = static_cast<const cIGZAny *>(RawSlot<3, cIGZPropertySetT<cKeyStringTraits> *>(settings)->GetProperty(&key));
            }
            equal = CompareSetting(setting, value.s, console, &known);
            if (!known) return false;
        } else {
            bool sent = true;
            equal = AskServer(name.s, value.s, &sent);
            if (!sent) return false;
        }
        const uint8_t result = negate ? !equal : equal;
        if (joiner.s.mString.empty())
            *out = result;
        else if (joiner.s.IsEqual(kAndText, false))
            *out &= result;
        else if (joiner.s.IsEqual(kOrText, false))
            *out |= result;
        else
            return false;
    }
    return true;
}

// Sends the window's notify text (message kNotifyMessage: the text as the message's string, the window as its object).
void SendNotify(cIGZVariant *property, cIGZUnknown *window)
{
    LocalString text;
    property->GetValString(*text.s.AsInterface(), 0, kValueSeparator);
    Message message;
    message.m->SetType(kNotifyMessage);
    message.m->SetExtra(NewString(text.s));
    message.m->SetObject(window);
    MessageServer()->MessageSend(message.m);
}

// A text edit's two scrollbars (ids 0 and 1 under its window) attached by their orientation: vertical through slot 95,
// any other through slot 96.
void AttachScrollbars(void *textEdit)
{
    Held<ifaces::cIGZWinScrollbar2> scrollbar;
    for (uint32_t id = kFirstScrollbarId; id <= kSecondScrollbarId; ++id) {
        auto *window = As<ifaces::cIGZWin>(RawSlot<5, void *>(textEdit));
        if (ifaces::cIGZWinScrollbar2 *old = scrollbar.p) {
            scrollbar.p = nullptr;
            reinterpret_cast<cIGZUnknown *>(old)->Release();
        }
        if (!window->GetChildInterface(id, kScrollbarIid, scrollbar.Out())) continue;
        uint32_t orientation;
        if (!scrollbar.p->GetOrientation(&orientation)) continue;
        if (orientation == kVertical)
            RawSlot<95>(textEdit, scrollbar.p);
        else
            RawSlot<96>(textEdit, scrollbar.p);
    }
}

// A script window made ready for the UI by its kind: a win proc host's slot 6 (4, 0); a button's window keeps no flag
// 0x8000; a text edit gets its scrollbars. Returns the win proc host (counted) or null.
cIGZWinProcHost *PrepareWindow(cIGZUnknown *window)
{
    cIGZWinProcHost *host = nullptr;
    if (window->QueryInterface(cIGZWinProcHost::kIID, reinterpret_cast<void **>(&host))) {
        RawSlot<6>(host, kWinProcHostMode, kWinProcHostModeArg);
        return host;
    }
    Held<cIGZUnknown> button;
    if (window->QueryInterface(kButtonIid, button.Out())) {
        As<ifaces::cIGZWin>(RawSlot<3, void *>(button.p))->SetFlag(kButtonWindowFlag, 0);
        return host;
    }
    Held<cIGZUnknown> textEdit;
    if (window->QueryInterface(kTextEditIid, textEdit.Out())) AttachScrollbars(textEdit.p);
    return host;
}

} // namespace

bool __cdecl EvaluateWindowCondition(uint32_t propertyId, cIGZUnknown *window, bool *out)
{
    if (!window) return false;
    Held<cIGZVariant> property;
    if (!Win(window)->GetProperty(propertyId, property.Out())) return false;
    return EvaluateText(property.p, reinterpret_cast<uint8_t *>(out));
}

bool __cdecl ApplyScriptConditions(cIGZUnknown *window)
{
    if (!window) return false;
    bool applied = false;
    bool condition = false;
    if (EvaluateWindowCondition(kVisibleCondition, window, &condition)) {
        Win(window)->SetFlag(kVisibleFlag, condition);
        applied = true;
    }
    if (EvaluateWindowCondition(kEnabledCondition, window, &condition)) {
        Win(window)->SetFlag(kEnabledFlag, condition);
        applied = true;
    }
    Held<cIGZVariant> notify;
    if (Win(window)->GetProperty(kNotifyProperty, notify.Out())) {
        applied = true;
        SendNotify(notify.p, window);
    }
    return applied;
}

void __cdecl UpdateScriptScreenSize()
{
    Held<cIGZWinMgr> manager;
    if (cIGZFrameWork *framework = Framework())
        framework->GetSystemService(cGZWinMgrW32::kServiceId, cIGZWinMgr::kIid, manager.Out());
    if (!manager.p) return;
    ifaces::cIGZWin *main = As<ifaces::cIGZWin>(manager.p->GetMainWindow());
    if (!main) return;
    uint32_t &width = *GameAddress<uint32_t>(kScreenWidthCell);
    uint32_t &height = *GameAddress<uint32_t>(kScreenHeightCell);
    width = uint32_t(main->GetWidth());
    height = uint32_t(main->GetHeight());
    if (width < kMinimumWidth || height < kMinimumHeight) {
        width = kFallbackWidth;
        height = kFallbackHeight;
    }
}

bool __cdecl LoadUIScript(uint32_t instance, uint32_t scriptArg, cIGZUnknown *winProc, cIGZScriptableControlSet **out,
                          bool screenSized)
{
    Held<cIGZUIScriptService> service;
    if (cIGZFrameWork *framework = Framework())
        framework->GetSystemService(cGZUIScriptService::kServiceId, cIGZUIScriptService::kIID, service.Out());
    if (!service.p) return false;
    Held<cIGZUILegacyScriptService> legacy;
    if (!service.p->QueryInterface(cIGZUILegacyScriptService::kIID, legacy.Out())) return false;
    legacy.p->PushNewScript(scriptArg);
    legacy.p->SetCompilerArgument(nullptr);

    cGZPersistResourceKey key = {0, kScriptGroup, instance, 0};
    if (screenSized) {
        Held<ifaces::cIGZPersistResourceManager> manager;
        if (cIGZFrameWork *framework = Framework())
            framework->GetSystemService(kResourceManagerId, kResourceManagerIid, manager.Out());
        if (manager.p) {
            const uint32_t *width = GameAddress<uint32_t>(kScreenWidthCell);
            const uint32_t *height = GameAddress<uint32_t>(kScreenHeightCell);
            if (!*height || !*width) UpdateScriptScreenSize();
            char text[0x48];
            std::sprintf(text, kScreenGroupFormat, *width, *height);
            key.group = hexparse::ParseHex32(text);
            if (!manager.p->TestForKey(&key)) key.group = kScriptGroup;
        }
    }

    auto *set = static_cast<cIGZScriptableControlSet *>(service.p->CompileNode(&key));
    if (!set) return false;

    std::vector<cIGZUnknown *> heldBack; // counted
    const uint32_t count = set->GetCount();
    for (uint32_t i = 0; i < count; ++i) {
        Held<cIGZUnknown> window(set->GetWindowAt(i));
        if (!window.p) continue;
        window.p->AddRef();
        Held<cIGZWinProcHost> host(PrepareWindow(window.p));
        bool shown = false;
        if (EvaluateWindowCondition(kShownCondition, window.p, &shown) && !shown) {
            window.p->AddRef();
            heldBack.push_back(window.p);
        } else {
            ApplyScriptConditions(window.p);
        }
        if (MessageServer()) {
            Held<cIGZVariant> announce;
            if (Win(window.p)->GetProperty(kAnnounceProperty, announce.Out()) && announce.p->ValBool()) {
                Message message;
                message.m->SetType(kAnnounceMessage);
                message.m->SetObject(window.p);
                MessageServer()->MessagePost(message.m, 0);
            }
        }
    }

    for (cIGZUnknown *window : heldBack) {
        window->AddRef();
        if (Win(window)->GetParent()) RawSlot<21>(Win(window)->GetParent(), window);
        window->Release();
    }

    if (winProc) {
        Held<cIGZWinProcHost> host;
        if (set->GetWindowAt(0)->QueryInterface(cIGZWinProcHost::kIID, host.Out())) host.p->SetWinProc(winProc);
    }

    for (cIGZUnknown *window : heldBack) window->Release();
    heldBack.clear();

    if (out) {
        *out = set;
    } else {
        set->SetOwnsWindows(false);
        set->Release();
    }
    return true;
}
