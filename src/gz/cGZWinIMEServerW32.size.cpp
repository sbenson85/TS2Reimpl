// cGZWinIMEServer / cGZWinIMEServerW32 / cGZWinIMEKeyMessage (lane KR): the IME server in the standalone shape
// (src/gz/cGZWinIMEServerW32.h). Built for size (/O1) in the game.
#include "rz/RZMessageServer.h"
#include "gz/cGZWinIMEServerW32.h"

#include "game/import.h"
#include "gz/cCanvasW32.h" // cCanvasW32::KeyModifiers
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZMessageServer.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

#include <string>

namespace {

// The framework's GetSystemService (slot 5) into a counted holder (cleared first; untouched without a framework): the
// original's holder constructors 0x762db4 (message server), 0x20db44 (language service) and 0x5b7e68 (window service).
template <class Ref> void GetService(uint32_t id, uint32_t iid, Ref &out)
{
    void **const slot = out.AsPPVoid();
    if (void *const framework = GetServiceObject()) As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, slot);
}

// A bool's byte as stored or passed: the original compares, tests and stores the raw byte (callers may pass any byte).
// The read is volatile so the compiler cannot assume 0 / 1 (it folds `b ? 0x10 : 0x11` into arithmetic otherwise).
uint8_t RawByte(const bool &value) { return *reinterpret_cast<const volatile uint8_t *>(&value); }
void SetRawByte(bool &target, uint8_t value) { *reinterpret_cast<uint8_t *>(&target) = value; }

constexpr uint32_t kLanguageServiceId = 0x441e5070, kLanguageServiceIid = 0x54b7d5;
constexpr int kLanguageSlotCurrent = 7;   // the current language id
constexpr int kLanguageSlotFromLayout = 12; // (keyboard layout) -> language id
constexpr int kWindowSlotHandle = 4;      // the window filter face: the main window
constexpr int kWindowSlotSetFilter = 6;   // (filter, add): adds or removes a window procedure filter
constexpr int kServerSlotSend = 3;        // the IME message server (+0x2c, 0x64063387): send {id, data1..3}

// The cached message server (0x9c5a89, an original callee; cell 0x10f519c).
cIGZUnknown *CachedMessageServer() { return static_cast<cIGZUnknown *>(GetCachedMessageServer()); }

// The language service's current language (0 without the service).
uint32_t CurrentLanguage()
{
    UnknownRef languages;
    GetService(kLanguageServiceId, kLanguageServiceIid, languages);
    return languages ? RawSlot<kLanguageSlotCurrent>(languages.Get()) : 0;
}

// The message mMessageServer sends (its handlers may write the data words).
struct ImeMessage {
    uint32_t id;
    void *data1;
    uint32_t data2;
    uint32_t data3;
};

// imm32 and user32 through the game's import slots (thunks 0xc4bf3b...).
using Himc = void *;
using ImmGetContextFn = Himc(__stdcall *)(void *window);
using ImmReleaseContextFn = int(__stdcall *)(void *window, Himc context);
using ImmGetCompositionStringWFn = int32_t(__stdcall *)(Himc context, uint32_t index, void *buffer, uint32_t bytes);
using ImmSetCompositionStringWFn = int(__stdcall *)(Himc context, uint32_t index, const void *text, uint32_t bytes,
                                                     const void *reading, uint32_t readingBytes);
using ImmGetCandidateListWFn = uint32_t(__stdcall *)(Himc context, uint32_t index, void *list, uint32_t bytes);
using ImmGetOpenStatusFn = int(__stdcall *)(Himc context);
using ImmSetOpenStatusFn = int(__stdcall *)(Himc context, int open);
using ImmNotifyIMEFn = int(__stdcall *)(Himc context, uint32_t action, uint32_t index, uint32_t value);
using DefWindowProcAFn = intptr_t(__stdcall *)(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam);

Himc GetContext(void *window) { return ImportFunction<ImmGetContextFn, 0xd9a0cc>()(window); }
void ReleaseContext(void *window, Himc context) { ImportFunction<ImmReleaseContextFn, 0xd9a0e4>()(window, context); }
int32_t GetCompositionString(Himc context, uint32_t index, void *buffer, uint32_t bytes)
{
    return ImportFunction<ImmGetCompositionStringWFn, 0xd9a0e0>()(context, index, buffer, bytes);
}
uint32_t GetCandidateList(Himc context, uint32_t index, void *list, uint32_t bytes)
{
    return ImportFunction<ImmGetCandidateListWFn, 0xd9a0d8>()(context, index, list, bytes);
}
int NotifyIME(Himc context, uint32_t action, uint32_t index, uint32_t value)
{
    return ImportFunction<ImmNotifyIMEFn, 0xd9a0d4>()(context, action, index, value);
}

// imm.h values.
constexpr uint32_t kGcsCompAttr = 0x10, kGcsCompStr = 8, kGcsCursorPos = 0x80, kGcsResultStr = 0x800;
constexpr uint32_t kScsSetStr = 9;
constexpr uint32_t kNiOpenCandidate = 0x10, kNiCloseCandidate = 0x11, kNiSelectCandidateStr = 0x12,
                   kNiCompositionStr = 0x15, kCpsCancel = 4;
constexpr uint32_t kImeCandCode = 2;
constexpr uint32_t kImnCloseCandidate = 4, kImnChangeCandidate = 3, kImnOpenCandidate = 5;
constexpr uint32_t kWmInputLangChange = 0x51, kWmImeStartComposition = 0x10d, kWmImeEndComposition = 0x10e,
                   kWmImeComposition = 0x10f, kWmImeSetContext = 0x281, kWmImeNotify = 0x282,
                   kWmImeCompositionFull = 0x284, kWmImeChar = 0x286;
constexpr uint32_t kKeyEventCharacter = 4;

// CANDIDATELIST's header (imm.h): the offsets are from the list's start.
struct CandidateList {
    uint32_t size, style, count, selection, pageStart, pageSize;
    uint32_t offsets[1];
};

} // namespace

// ---- cGZIMEComposition / cGZIMECandidates ----

cGZIMEComposition::cGZIMEComposition() : cursor(0), composing(false)
{
    language = CurrentLanguage();
    RefreshView();
}

void cGZIMEComposition::RefreshView()
{
    if (string.empty()) {
        text = nullptr;
        attributes = nullptr;
    } else {
        text = string.data();
        attributes = attributeBuffer.data();
    }
    length = static_cast<uint32_t>(string.size());
}

void cGZIMEComposition::Clear()
{
    string.clear();
    attributeBuffer.clear();
    hasResult = false;
    RefreshView();
}

cGZIMECandidates::cGZIMECandidates() : selection(0), open(false)
{
    language = CurrentLanguage();
    RefreshView();
}

void cGZIMECandidates::RefreshView()
{
    if (strings.empty()) {
        count = 0;
        items = nullptr;
        return;
    }
    count = static_cast<uint32_t>(strings.size());
    pointers.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        std::u16string &item = strings[i];
        if (item.empty() || item.back() != 0) item.push_back(0);
        pointers[i] = item.data();
    }
    items = pointers.data();
}

void cGZIMECandidates::Clear()
{
    strings.clear();
    pointers.clear();
    RefreshView();
}

// ---- cGZWinIMEKeyMessage ----

cGZWinIMEKeyMessage::cGZWinIMEKeyMessage()
    : mRefCount(0), mType(kType), mEventType(0xffffffff), mKey(0), mModifiers(0), mExtra(0)
{
}

cGZWinIMEKeyMessage::~cGZWinIMEKeyMessage() noexcept = default;

bool cGZWinIMEKeyMessage::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != cIGZKeyMessage::kIid && riid != cIGZMessage2::kIid) return false;
    *out = static_cast<cIGZKeyMessage *>(this);
    AddRef();
    return true;
}

uint32_t cGZWinIMEKeyMessage::AddRef() { return ++mRefCount; }

uint32_t cGZWinIMEKeyMessage::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

bool cGZWinIMEKeyMessage::Create(uint32_t, void **) { return false; }
uint32_t cGZWinIMEKeyMessage::GetType() { return mType; }
uint32_t cGZWinIMEKeyMessage::SetType(uint32_t type) { return mType = type; }
bool cGZWinIMEKeyMessage::IsEqual(cIGZMessage2 *other) { return mType == other->GetType(); }
bool cGZWinIMEKeyMessage::IsLess(cIGZMessage2 *other) { return mType < other->GetType(); }

void cGZWinIMEKeyMessage::SetKeyEvent(uint32_t type, uint32_t key, uint32_t modifiers, uint32_t extra)
{
    mEventType = type;
    mKey = key;
    mModifiers = modifiers;
    mExtra = extra;
}

uint32_t cGZWinIMEKeyMessage::GetEventType() { return mEventType; }
uint32_t cGZWinIMEKeyMessage::GetKey() { return mKey; }
uint32_t cGZWinIMEKeyMessage::GetModifiers() { return mModifiers; }
uint32_t cGZWinIMEKeyMessage::GetExtra() { return mExtra; }

// ---- cGZWinIMEServer ----

cGZWinIMEServer::cGZWinIMEServer()
    : rz::cRZSystemService(kServiceId, kPriority), mRefCount(0), mServerId(kServiceId), mInitialized(false),
      mEnabled(true)
{
}

cGZWinIMEServer::~cGZWinIMEServer() noexcept = default; // the members in reverse, then the service part

bool cGZWinIMEServer::QueryInterface(uint32_t riid, void **out)
{
    // No null test on `out` (the original writes it unchecked).
    if (riid == 1 || riid == cIGZWinIMEServer::kIid) {
        *out = static_cast<cIGZWinIMEServer *>(this);
        AddRef();
        return true;
    }
    return rz::cRZSystemService::QueryInterface(riid, out);
}

uint32_t cGZWinIMEServer::AddRef() { return ++mRefCount; }

uint32_t cGZWinIMEServer::Release()
{
    if (mRefCount > 1) return --mRefCount;
    Destroy();
    return 0;
}

void cGZWinIMEServer::Destroy()
{
    Shutdown();
    delete this;
}

bool cGZWinIMEServer::Init()
{
    if (mInitialized) return true;
    mInitialized = true;
    UnknownRef server;
    GetService(kMessageServerId, kMessageServerIid, server);
    mMessageServer.Assign(server.Get());
    mMessageServer2.Assign(CachedMessageServer());
    if (mEnabled) {
        mEnabled = false; // so the enable below is a change
        SetEnabled(true);
    }
    return true;
}

bool cGZWinIMEServer::Shutdown()
{
    if (!mInitialized) return true;
    mInitialized = false;
    mMessageServer.Assign(nullptr);
    mMessageServer2.Assign(nullptr);
    SetEnabled(false);
    return true;
}

bool cGZWinIMEServer::IsAvailable() { return false; }
bool cGZWinIMEServer::IsEnabled() { return mEnabled; }

void cGZWinIMEServer::SetEnabled(bool enable)
{
    const uint8_t raw = RawByte(enable);
    if (RawByte(mEnabled) != raw) SetRawByte(mEnabled, raw);
}

bool cGZWinIMEServer::IsOpen() { return true; }
bool cGZWinIMEServer::SetOpen(bool) { return true; }
bool cGZWinIMEServer::IsComposing() { return mComposition.composing; }

bool cGZWinIMEServer::SetComposing(bool)
{
    IsComposing(); // asked and ignored
    return true;
}

cGZIMEComposition *cGZWinIMEServer::GetComposition() { return &mComposition; }

bool cGZWinIMEServer::SetCompositionString(const char16_t *text, uint32_t length)
{
    std::u16string value;
    if (text && length) value.assign(text, length);
    mComposition.string.assign(value.data(), value.size()); // (the view is not refreshed)
    return true;
}

bool cGZWinIMEServer::IsCandidateListOpen() { return mCandidates.open; }

bool cGZWinIMEServer::SetCandidateListOpen(bool)
{
    IsCandidateListOpen(); // asked and ignored
    return true;
}

cGZIMECandidates *cGZWinIMEServer::GetCandidates() { return &mCandidates; }
bool cGZWinIMEServer::SelectCandidate(uint32_t index) { return index < mCandidates.strings.size(); }

// ---- cGZWinIMEServerW32 ----

cGZWinIMEServerW32 *cGZWinIMEServerW32::sInstance = nullptr;

cGZWinIMEServerW32::cGZWinIMEServerW32() : mUnusedA0(0), mWindow(nullptr) { sInstance = this; }

cGZWinIMEServerW32::~cGZWinIMEServerW32() noexcept { sInstance = nullptr; }

cIGZSystemService *__cdecl cGZWinIMEServerW32::Create() { return new cGZWinIMEServerW32; }

bool cGZWinIMEServerW32::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZWinProcFilterW32::kIid) {
        *out = static_cast<cIGZWinProcFilterW32 *>(this);
        AddRef();
        return true;
    }
    return cGZWinIMEServer::QueryInterface(riid, out);
}

uint32_t cGZWinIMEServerW32::AddRef() { return cGZWinIMEServer::AddRef(); }
uint32_t cGZWinIMEServerW32::Release() { return cGZWinIMEServer::Release(); }

bool cGZWinIMEServerW32::IsAvailable() { return true; }

void cGZWinIMEServerW32::SetEnabled(bool enable)
{
    const uint8_t raw = RawByte(enable);
    if (RawByte(mEnabled) == raw) return;
    SetRawByte(mEnabled, raw);
    UnknownRef windows;
    GetService(kWindowServiceId, kWindowServiceIid, windows);
    cIGZWinProcFilterW32 *const filter = this;
    if (raw) {
        if (windows) {
            cIGZUnknown *window = nullptr;
            if (windows->QueryInterface(kWindowFilterIid, reinterpret_cast<void **>(&window))) {
                mWindow = RawSlot<kWindowSlotHandle, void *>(window);
                RawSlot<kWindowSlotSetFilter>(window, filter, 1u);
            }
            if (window) window->Release();
        }
    } else {
        if (windows) {
            cIGZUnknown *window = nullptr;
            if (windows->QueryInterface(kWindowFilterIid, reinterpret_cast<void **>(&window)))
                RawSlot<kWindowSlotSetFilter>(window, filter, 0u);
            if (window) window->Release();
        }
        mWindow = nullptr;
    }
}

bool cGZWinIMEServerW32::IsOpen()
{
    const Himc context = GetContext(mWindow);
    if (!context) return false;
    const int open = ImportFunction<ImmGetOpenStatusFn, 0xd9a0d0>()(context);
    ReleaseContext(mWindow, context);
    return open != 0;
}

bool cGZWinIMEServerW32::SetOpen(bool open)
{
    const Himc context = GetContext(mWindow);
    if (!context) return false;
    const int done = ImportFunction<ImmSetOpenStatusFn, 0xd9a0e8>()(context, RawByte(open));
    ReleaseContext(mWindow, context);
    return done != 0;
}

bool cGZWinIMEServerW32::SetComposing(bool composing)
{
    const bool current = IsComposing();
    if (RawByte(composing) != RawByte(current) && !RawByte(composing)) {
        if (const Himc context = GetContext(mWindow)) {
            const int done = NotifyIME(context, kNiCompositionStr, kCpsCancel, 0);
            ReleaseContext(mWindow, context);
            return done != 0;
        }
    }
    return true;
}

bool cGZWinIMEServerW32::SetCompositionString(const char16_t *text, uint32_t length)
{
    if (!mWindow) return false;
    // ORIGINAL-BUG: the input context is never released here.
    const Himc context = GetContext(mWindow);
    if (!ImportFunction<ImmSetCompositionStringWFn, 0xd9a0dc>()(context, kScsSetStr, text, length * 2, nullptr, 0))
        return false;
    cGZWinIMEServer::SetCompositionString(text, length);
    return true;
}

bool cGZWinIMEServerW32::SetCandidateListOpen(bool open)
{
    const bool current = IsCandidateListOpen();
    if (RawByte(open) == RawByte(current)) return true;
    const Himc context = GetContext(mWindow);
    if (!context) return false;
    const int done = NotifyIME(context, RawByte(open) ? kNiOpenCandidate : kNiCloseCandidate, 0, 0);
    ReleaseContext(mWindow, context);
    return done != 0;
}

bool cGZWinIMEServerW32::SelectCandidate(uint32_t index)
{
    if (index < mCandidates.strings.size()) {
        if (const Himc context = GetContext(mWindow)) {
            const int done = NotifyIME(context, kNiSelectCandidateStr, 0, index);
            ReleaseContext(mWindow, context);
            return done != 0;
        }
    }
    return false;
}

void cGZWinIMEServerW32::ReadComposition(void *window, uint32_t, uintptr_t, intptr_t lParam)
{
    const Himc context = GetContext(window);
    if (!context) return;
    cGZIMEComposition &composition = mComposition;
    const uint32_t flags = static_cast<uint32_t>(lParam);
    if (flags & kGcsCompAttr) {
        std::vector<uint8_t> &attributes = composition.attributeBuffer;
        attributes.resize(static_cast<uint32_t>(GetCompositionString(context, kGcsCompAttr, nullptr, 0)));
        GetCompositionString(context, kGcsCompAttr, attributes.data(), static_cast<uint32_t>(attributes.size()));
        const int32_t count = static_cast<int32_t>(attributes.size());
        for (int32_t i = 0; i < count; ++i) {
            uint8_t &a = attributes[i];
            // ATTR_INPUT -> 1, ATTR_TARGET_CONVERTED -> 2, ATTR_INPUT_ERROR -> 3, anything else -> 0
            a = a == 0 ? 1 : a == 1 ? 2 : a == 4 ? 3 : 0;
        }
    }
    for (const uint32_t which : {kGcsCompStr, kGcsResultStr}) {
        if (!(flags & which)) continue;
        std::u16string &text = composition.string;
        text.resize(static_cast<uint32_t>(GetCompositionString(context, which, nullptr, 0)) >> 1, 0);
        GetCompositionString(context, which, text.data(), static_cast<uint32_t>(text.size() * 2));
        if (composition.attributeBuffer.size() < text.size()) composition.attributeBuffer.resize(text.size(), 0);
        if (which == kGcsResultStr) composition.hasResult = true;
    }
    if (flags & kGcsCursorPos) {
        const uint32_t cursor = static_cast<uint32_t>(GetCompositionString(context, kGcsCursorPos, nullptr, 0));
        const uint32_t length = static_cast<uint32_t>(composition.string.size());
        composition.cursor = cursor > length ? length : cursor;
    }
    composition.RefreshView();
    ReleaseContext(window, context);
}

void cGZWinIMEServerW32::ReadCandidates(void *window, uint32_t, uintptr_t, intptr_t lParam)
{
    if (const Himc context = GetContext(window)) {
        for (uint32_t index = 0; index < 32; ++index) {
            if (!(static_cast<uint32_t>(lParam) & 1u << index)) continue;
            const uint32_t bytes = GetCandidateList(context, index, nullptr, 0);
            if (!bytes) continue;
            std::vector<uint8_t> buffer(bytes); // zeroed, as the original's malloc + memset
            GetCandidateList(context, index, buffer.data(), bytes);
            const auto *list = reinterpret_cast<const CandidateList *>(buffer.data());
            // (The original also moves the list's page start to the selection's page: in the buffer, freed unread.)
            std::vector<std::u16string> &strings = mCandidates.strings;
            if (list->style == kImeCandCode && list->count == 1) {
                strings.resize(1);
                strings[0] = static_cast<char16_t>(list->offsets[0]); // the code itself
            } else {
                strings.resize(list->count);
                for (uint32_t i = 0; i < list->count; ++i) {
                    const auto *item = reinterpret_cast<const char16_t *>(buffer.data() + list->offsets[i]);
                    strings[i].assign(item, std::char_traits<char16_t>::length(item));
                }
            }
            mCandidates.selection = list->selection;
        }
        ReleaseContext(window, context);
    }
    mCandidates.RefreshView();
}

void cGZWinIMEServerW32::PostCharacter(void *, uint16_t character, uint32_t extra, uint32_t keyFlags)
{
    // 0x155860 (cCanvasW32::KeyModifiers): the shift / control / alt / mouse button state, with keyFlags' 0x40000000 bit.
    const uint32_t modifiers = nGZGraphic4::cCanvasW32::KeyModifiers(keyFlags);
    uint8_t bytes[0x20];
    uint32_t count = sizeof(bytes);
    RZText::Convert(&character, 1, RZText::kUtf16, bytes, &count, RZText::kDefault);
    for (uint32_t i = 0; i < count; ++i) {
        cGZWinIMEKeyMessage *const message = new cGZWinIMEKeyMessage;
        message->AddRef();
        message->SetKeyEvent(kKeyEventCharacter, bytes[i], modifiers, extra);
        As<ifaces::cIGZMessageServer>(mMessageServer2.Get())->MessageSend(static_cast<cIGZKeyMessage *>(message));
        message->Release();
    }
}

intptr_t cGZWinIMEServerW32::FilterMessage(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam, bool *handled)
{
    *handled = false;
    cIGZUnknown *const server = mMessageServer.Get(); // (null only before Init; the original does not test it)
    const auto send = [server](ImeMessage &m) { RawSlot<kServerSlotSend>(server, &m); };
    switch (message) {
    case kWmInputLangChange: {
        UnknownRef languages;
        GetService(kLanguageServiceId, kLanguageServiceIid, languages);
        if (languages) {
            const uint32_t language = RawSlot<kLanguageSlotFromLayout>(languages.Get(), lParam);
            mComposition.language = language;
            mCandidates.language = language;
        }
        return 0;
    }
    case kWmImeStartComposition:
        if (!mComposition.composing) {
            mComposition.Clear();
            mComposition.composing = true;
            uint32_t started = 1; // (the original points data2 at a 1 on its stack)
            ImeMessage m = {kMsgStartComposition, &mComposition, reinterpret_cast<uintptr_t>(&started), 0};
            send(m);
        }
        break;
    case kWmImeEndComposition: {
        ImeMessage m = {kMsgEndComposition, &mComposition, RawByte(mComposition.hasResult) != 0 ? 1u : 0u, 0};
        send(m);
        mComposition.Clear();
        mComposition.composing = false;
        break;
    }
    case kWmImeComposition: {
        const uint32_t cursor = mComposition.cursor;
        const std::u16string before = mComposition.string;
        ReadComposition(window, message, wParam, lParam);
        if (before != mComposition.string) {
            ImeMessage m = {kMsgCompositionChanged, &mComposition, 0, 0};
            send(m);
        }
        if (cursor != mComposition.cursor) {
            ImeMessage m = {kMsgCursorMoved, &mComposition, 0, 0};
            send(m);
        }
        if (mComposition.hasResult) {
            const int32_t length = static_cast<int32_t>(mComposition.string.size());
            for (int32_t i = 0; i < length; ++i) PostCharacter(window, mComposition.string[i], 0, 0);
            ImeMessage m = {kMsgResult, &mComposition, 0, 0};
            send(m);
            mComposition.hasResult = false;
        }
        break;
    }
    case kWmImeSetContext:
        *handled = true;
        // The IME's own composition window is not shown (ISC_SHOWUICOMPOSITIONWINDOW cleared).
        return ImportFunction<DefWindowProcAFn, 0xd9a524>()(window, message, wParam, lParam & 0x7fffffff);
    case kWmImeNotify:
        *handled = true;
        if (wParam == kImnChangeCandidate) {
            ReadCandidates(window, message, wParam, lParam);
            ImeMessage m = {kMsgCandidatesChanged, &mCandidates, 0, 0};
            send(m);
        } else if (wParam == kImnCloseCandidate) {
            ImeMessage m = {kMsgCloseCandidates, &mCandidates, 0, 0};
            send(m);
            mCandidates.Clear();
        } else if (wParam == kImnOpenCandidate) {
            ReadCandidates(window, message, wParam, lParam);
            ImeMessage m = {kMsgOpenCandidates, nullptr, 0, 0};
            send(m);
            m.id = kMsgCandidatesChanged; // (data2 / data3 as the first send left them)
            m.data1 = &mCandidates;
            send(m);
        }
        return 1;
    case kWmImeCompositionFull:
    case kWmImeChar:
        break;
    default:
        return 0;
    }
    *handled = true;
    return 1;
}
