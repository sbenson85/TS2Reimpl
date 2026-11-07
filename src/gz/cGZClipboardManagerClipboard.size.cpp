#include "gz/cGZClipboardManagerClipboard.h"

#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"
#include "gz/cGZMessageServer.h"
#include "gz/cIGZMessageTarget.h"
#include "ifaces/cIGZClipboardData.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZFrameWork.h"
#include "misc/ClassesRound4.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>
#include <string>

namespace {

// ---- Win32, through the game's import slots ----

struct WindowClass { // WNDCLASSA
    uint32_t style;
    intptr_t(__stdcall *procedure)(void *, uint32_t, uintptr_t, intptr_t);
    int32_t classExtra;
    int32_t windowExtra;
    void *instance;
    void *icon;
    void *cursor;
    void *background;
    const char *menuName;
    const char *className;
};
static_assert(sizeof(WindowClass) == 0x28, "WNDCLASSA");

using OpenClipboardFn = int32_t(__stdcall *)(void *);
using CloseClipboardFn = int32_t(__stdcall *)();
using IsClipboardFormatAvailableFn = int32_t(__stdcall *)(uint32_t);
using GetClipboardDataFn = void *(__stdcall *)(uint32_t);
using SetClipboardDataFn = void *(__stdcall *)(uint32_t, void *);
using EmptyClipboardFn = int32_t(__stdcall *)();
using GlobalAllocFn = void *(__stdcall *)(uint32_t, uint32_t);
using GlobalSizeFn = uint32_t(__stdcall *)(void *);
using GlobalLockFn = void *(__stdcall *)(void *);
using GlobalUnlockFn = int32_t(__stdcall *)(void *);
using GlobalFreeFn = void *(__stdcall *)(void *);
using ChangeClipboardChainFn = int32_t(__stdcall *)(void *, void *);
using SetClipboardViewerFn = void *(__stdcall *)(void *);
using DestroyWindowFn = int32_t(__stdcall *)(void *);
using GetVersionFn = uint32_t(__stdcall *)();
using GetModuleHandleFn = void *(__stdcall *)(const char *);
using RegisterClassFn = uint16_t(__stdcall *)(const WindowClass *);
using CreateWindowExFn = void *(__stdcall *)(uint32_t, const char *, const char *, uint32_t, int32_t, int32_t, int32_t,
                                             int32_t, void *, void *, void *, void *);
using SetWindowLongFn = int32_t(__stdcall *)(void *, int32_t, int32_t);
using GetWindowLongFn = int32_t(__stdcall *)(void *, int32_t);
using SendMessageFn = intptr_t(__stdcall *)(void *, uint32_t, uintptr_t, intptr_t);
using DefWindowProcFn = intptr_t(__stdcall *)(void *, uint32_t, uintptr_t, intptr_t);

bool OpenClipboard() { return ImportFunction<OpenClipboardFn, 0xd9a494>()(nullptr) != 0; }
void CloseClipboard() { ImportFunction<CloseClipboardFn, 0xd9a490>()(); }
bool IsClipboardFormatAvailable(uint32_t format) { return ImportFunction<IsClipboardFormatAvailableFn, 0xd9a500>()(format) != 0; }
void *GetClipboardData(uint32_t format) { return ImportFunction<GetClipboardDataFn, 0xd9a4f8>()(format); }
void *SetClipboardData(uint32_t format, void *memory) { return ImportFunction<SetClipboardDataFn, 0xd9a4f4>()(format, memory); }
void EmptyClipboard() { ImportFunction<EmptyClipboardFn, 0xd9a4fc>()(); }
void *GlobalAlloc(uint32_t flags, uint32_t size) { return ImportFunction<GlobalAllocFn, 0xd9a0f8>()(flags, size); }
uint32_t GlobalSize(void *memory) { return ImportFunction<GlobalSizeFn, 0xd9a424>()(memory); }
void *GlobalLock(void *memory) { return ImportFunction<GlobalLockFn, 0xd9a0f0>()(memory); }
void GlobalUnlock(void *memory) { ImportFunction<GlobalUnlockFn, 0xd9a428>()(memory); }
void GlobalFree(void *memory) { ImportFunction<GlobalFreeFn, 0xd9a0f4>()(memory); }
void ChangeClipboardChain(void *window, void *next) { ImportFunction<ChangeClipboardChainFn, 0xd9a4d8>()(window, next); }
void *SetClipboardViewer(void *window) { return ImportFunction<SetClipboardViewerFn, 0xd9a4f0>()(window); }
void DestroyWindow(void *window) { ImportFunction<DestroyWindowFn, 0xd9a534>()(window); }
void GetVersion() { ImportFunction<GetVersionFn, 0xd9a11c>()(); }
void *GetModuleHandle() { return ImportFunction<GetModuleHandleFn, 0xd9a114>()(nullptr); }
void RegisterClass(const WindowClass *windowClass) { ImportFunction<RegisterClassFn, 0xd9a4b8>()(windowClass); }
void *CreateWindowEx(const char *className, const char *windowName, int32_t position, void *instance)
{
    return ImportFunction<CreateWindowExFn, 0xd9a530>()(0, className, windowName, 0, position, position, position, position,
                                                         nullptr, nullptr, instance, nullptr);
}
void SetWindowLong(void *window, int32_t index, int32_t value) { ImportFunction<SetWindowLongFn, 0xd9a5d4>()(window, index, value); }
int32_t GetWindowLong(void *window, int32_t index) { return ImportFunction<GetWindowLongFn, 0xd9a5d8>()(window, index); }
void SendMessage(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam)
{
    ImportFunction<SendMessageFn, 0xd9a498>()(window, message, wParam, lParam);
}
intptr_t DefWindowProc(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam)
{
    return ImportFunction<DefWindowProcFn, 0xd9a524>()(window, message, wParam, lParam);
}

// The SDK's clipboard formats, messages and flags.
constexpr uint32_t kCfText = 1;             // CF_TEXT
constexpr uint32_t kCfUnicodeText = 13;     // CF_UNICODETEXT
constexpr uint32_t kDrawClipboard = 0x308;  // WM_DRAWCLIPBOARD
constexpr uint32_t kChangeChain = 0x30d;    // WM_CHANGECBCHAIN
constexpr uint32_t kMoveable = 2;           // GMEM_MOVEABLE
constexpr int32_t kUserData = -21;          // GWL_USERDATA
constexpr int32_t kDefaultPosition = static_cast<int32_t>(0x80000000); // CW_USEDEFAULT

// The window class and window names (the game's strings).
constexpr uint32_t kViewerClassName = 0xe9eea4;
constexpr uint32_t kViewerWindowName = 0xe9eec0;
// The viewer's language word (MAKELANGID English, US).
constexpr uint32_t kLanguageEnglishUs = 0x409;

// cIGZUnknown's interface id, and cIGZString's (asked of the text format's object).
constexpr uint32_t kUnknownIid = 1;
constexpr uint32_t kStringIid = 0x89b7dc8;
// The id of the clipboard with a viewer.
constexpr uint32_t kSystemClipboardId = 0;

// The converted text's room: 7 bytes per UTF-16 unit, 4 per ANSI byte.
constexpr uint32_t kBytesPerUnit = 7;
constexpr uint32_t kBytesPerAnsiByte = 4;

// Holds a lock through its cIGZCriticalSection vtable, as the original does.
void Lock(cRZCriticalSection &lock) { As<ifaces::cIGZCriticalSection>(&lock)->Lock(); }
void Unlock(cRZCriticalSection &lock) { As<ifaces::cIGZCriticalSection>(&lock)->Unlock(); }

cIGZUnknown *Unknown(ifaces::cIGZClipboardData *data) { return As<cIGZUnknown>(data); }

// The text a data object holds, through its object's cIGZString (counted), or null.
cIGZString *TextOf(ifaces::cIGZClipboardData *data)
{
    cIGZUnknown *const object = data->GetObject();
    if (!object) return nullptr;
    cIGZString *text = nullptr;
    if (!object->QueryInterface(kStringIid, reinterpret_cast<void **>(&text))) return nullptr;
    return text;
}

// 0xc6d2a5 (__thiscall, ret 8): a cGZClipboardManagerClipboardData (game vtable 0xe9ed84) holding `object` (counted)
// under `format`, its own count 0.
ifaces::cIGZClipboardData *ConstructClipboardData(void *memory, uint32_t format, cIGZUnknown *object)
{
    auto *const data = static_cast<cGZClipboardManagerClipboardData *>(memory);
    data->mUnknown4 = 0;
    data->mVTable = GameAddress<void>(cGZClipboardManagerClipboardData::kVTable);
    data->mUnknown8 = format;
    data->mData = object;
    if (object) object->AddRef();
    return reinterpret_cast<ifaces::cIGZClipboardData *>(data);
}

// A cRZString on the stack.
struct LocalString {
    union {
        cRZString s;
    };
    LocalString() { s.Construct(); }
    ~LocalString() { s.Destruct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
};

// The clipboard's change message: the id, the clipboard, its id and the format (or Init's / Shutdown's flag).
cGZMessage ChangedMessage(cGZClipboardManagerClipboard *clipboard, uint32_t value)
{
    cGZMessage message;
    message.mId = cGZClipboardManagerClipboard::kChangedMessage;
    message.mWord4 = reinterpret_cast<uintptr_t>(clipboard);
    message.mObject = reinterpret_cast<void *>(static_cast<uintptr_t>(clipboard->mId));
    message.mWordC = value;
    return message;
}

} // namespace

// ---- the message server cache ----

ScopedRef *AcquireMessageServer(ScopedRef *ref)
{
    ref->mObject = nullptr;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kMessageServerServiceId, cIGZMessageServer::kIid, &ref->mObject);
    return ref;
}

cIGZMessageServer *GetMessageServer()
{
    auto *const cell = GameAddress<cIGZMessageServer *>(kMessageServerCell);
    if (!*cell) {
        ScopedRef ref;
        AcquireMessageServer(&ref);
        *cell = static_cast<cIGZMessageServer *>(ref.mObject);
        ref.ReleaseObject();
    }
    return *cell;
}

// ---- cGZClipboardManagerClipboard ----

cGZClipboardManagerClipboard::cGZClipboardManagerClipboard()
    : mInitialised(false), mNotify(false), mRefCount(0), mId(kNoId), mNotifySuppressed(0), mChangeCount(0), mViewer(nullptr)
{
    mLock.Construct();
}

// The lock first, then the map (the member's destructor): the data still in it is not released.
cGZClipboardManagerClipboard::~cGZClipboardManagerClipboard()
{
    mLock.Destruct();
}

bool cGZClipboardManagerClipboard::QueryInterface(uint32_t riid, void **ppv)
{
    bool found = false;
    Lock(mLock);
    if (riid == kUnknownIid || riid == kIid) {
        *ppv = static_cast<cIGZClipboard *>(this);
        AddRef();
        found = true;
    }
    Unlock(mLock);
    return found;
}

uint32_t cGZClipboardManagerClipboard::AddRef()
{
    Lock(mLock);
    const uint32_t count = ++mRefCount;
    Unlock(mLock);
    return count;
}

// At a count below 2 (the count itself is left alone): Shutdown, then the deleting destructor.
uint32_t cGZClipboardManagerClipboard::Release()
{
    Lock(mLock);
    if (mRefCount < 2) {
        Unlock(mLock);
        Shutdown();
        delete this;
        return 0;
    }
    const uint32_t count = --mRefCount;
    Unlock(mLock);
    return count;
}

// Once: the viewer (clipboard 0), then the Init notice outside the lock.
bool cGZClipboardManagerClipboard::Init()
{
    Lock(mLock);
    if (!mInitialised) {
        mInitialised = true;
        CreateViewer();
        Unlock(mLock);
        NotifyState(true);
        Lock(mLock);
    }
    Unlock(mLock);
    return true;
}

// The Shutdown notice outside the lock, then every format removed and the viewer destroyed.
bool cGZClipboardManagerClipboard::Shutdown()
{
    Lock(mLock);
    if (mInitialised) {
        Unlock(mLock);
        NotifyState(false);
        Lock(mLock);
        RemoveData(kAllFormats);
        DestroyViewer();
        mInitialised = false;
    }
    Unlock(mLock);
    return true;
}

uint32_t cGZClipboardManagerClipboard::GetId() { return mId; }

// Not locked. Once initialised, id 0 gets the viewer and any other id loses it.
void cGZClipboardManagerClipboard::SetId(uint32_t id)
{
    mId = id;
    if (!mInitialised) return;
    if (id == kSystemClipboardId) {
        CreateViewer();
    } else {
        DestroyViewer();
    }
}

bool cGZClipboardManagerClipboard::GetNotify() { return mNotify; }

void cGZClipboardManagerClipboard::SetNotify(bool notify) { mNotify = notify; }

uint32_t cGZClipboardManagerClipboard::GetChangeCount() { return mChangeCount; }

bool cGZClipboardManagerClipboard::HasFormat(uint32_t format)
{
    Lock(mLock);
    const bool found = mData.find(format) != mData.end();
    Unlock(mLock);
    return found;
}

uint32_t cGZClipboardManagerClipboard::GetFormats(uint32_t *formats, uint32_t *count)
{
    Lock(mLock);
    uint32_t result = static_cast<uint32_t>(mData.size());
    if (!formats) {
        *count = result;
    } else {
        *count = *count < result ? *count : result;
        auto it = mData.begin();
        for (uint32_t i = 0; i < *count; ++i, ++it) formats[i] = it->first;
        result = *count;
    }
    Unlock(mLock);
    return result;
}

bool cGZClipboardManagerClipboard::QueryData(uint32_t format, uint32_t iid, void **out)
{
    Lock(mLock);
    const auto found = mData.find(format);
    const bool answered = found != mData.end() && Unknown(found->second)->QueryInterface(iid, out);
    Unlock(mLock);
    return answered;
}

ifaces::cIGZClipboardData *cGZClipboardManagerClipboard::GetData(uint32_t format)
{
    Lock(mLock);
    const auto found = mData.find(format);
    ifaces::cIGZClipboardData *const data = found != mData.end() ? found->second : nullptr;
    Unlock(mLock);
    return data;
}

// `clear` first empties the clipboard (format -1 for the notice); the data then replaces its format's entry, and text
// set on clipboard 0 goes to the system clipboard. One change and one notice for the whole call (none when there was
// nothing to clear and no data).
bool cGZClipboardManagerClipboard::SetData(ifaces::cIGZClipboardData *data, bool clear)
{
    uint32_t format = 0;
    bool cleared = false;
    Lock(mLock);
    if (clear && !mData.empty()) {
        format = kAllFormats;
        ++mNotifySuppressed;
        RemoveData(kAllFormats);
        --mNotifySuppressed;
        cleared = true;
    }
    if (!data && !cleared) {
        Unlock(mLock);
        return true;
    }
    if (data) {
        format = data->GetFormat();
        if (!mData.empty()) {
            ++mNotifySuppressed;
            RemoveData(format);
            --mNotifySuppressed;
        }
        Unknown(data)->AddRef();
        mData.insert({format, data});
        if (mId == kSystemClipboardId && format == kTextFormat && mViewer) {
            if (cIGZString *const text = TextOf(data)) {
                Unlock(mLock);
                mViewer->WriteText(text);
                Lock(mLock);
                text->Release();
            }
        }
    }
    ++mChangeCount;
    Unlock(mLock);
    NotifyChanged(format);
    Lock(mLock);
    Unlock(mLock);
    return true;
}

// One format or every one (-1): released and dropped, one change and one notice (under the lock); nothing when the
// clipboard is empty or the format absent.
void cGZClipboardManagerClipboard::RemoveData(uint32_t format)
{
    Lock(mLock);
    if (!mData.empty()) {
        if (format == kAllFormats) {
            for (auto &entry : mData) Unknown(entry.second)->Release();
            mData.clear();
        } else {
            const auto found = mData.find(format);
            if (found == mData.end()) {
                Unlock(mLock);
                return;
            }
            Unknown(found->second)->Release();
            mData.erase(found);
        }
        ++mChangeCount;
        NotifyChanged(format);
    }
    Unlock(mLock);
}

bool cGZClipboardManagerClipboard::Slot16(uint32_t, uint32_t, uint32_t)
{
    Lock(mLock);
    Unlock(mLock);
    return false;
}

// The text format's object as cIGZString, copied into `out`.
bool cGZClipboardManagerClipboard::GetText(cIGZString *out)
{
    bool copied = false;
    Lock(mLock);
    if (ifaces::cIGZClipboardData *const data = GetData(kTextFormat)) {
        if (cIGZString *const text = TextOf(data)) {
            out->Copy(*text);
            text->Release();
            copied = true;
        }
    }
    Unlock(mLock);
    return copied;
}

bool cGZClipboardManagerClipboard::Slot18(uint32_t, uint32_t, uint32_t, uint32_t)
{
    Lock(mLock);
    Unlock(mLock);
    return false;
}

// A copy of `text` (a new cRZString) as the text format's data, through SetData.
bool cGZClipboardManagerClipboard::SetText(const cIGZString &text, bool clear)
{
    Lock(mLock);
    auto *const copy = static_cast<cRZString *>(::operator new(sizeof(cRZString)))->Construct(text);
    cIGZUnknown *const object = As<cIGZUnknown>(copy);
    object->AddRef();
    ifaces::cIGZClipboardData *const data =
        ConstructClipboardData(::operator new(sizeof(cGZClipboardManagerClipboardData)), kTextFormat, object);
    Unknown(data)->AddRef();
    const bool set = SetData(data, clear);
    Unknown(data)->Release();
    object->Release();
    Unlock(mLock);
    return set;
}

bool cGZClipboardManagerClipboard::CreateViewer()
{
    if (mId == kSystemClipboardId && !mViewer) mViewer = new cGZClipboardViewer(this);
    return true;
}

bool cGZClipboardManagerClipboard::DestroyViewer()
{
    if (mViewer) {
        mViewer->Detach();
        delete mViewer;
        mViewer = nullptr;
    }
    return true;
}

void cGZClipboardManagerClipboard::NotifyChanged(uint32_t format)
{
    if (!mNotify || mNotifySuppressed != 0 || format == 0) return;
    cGZMessage message = ChangedMessage(this, format);
    if (cIGZMessageServer *const server = GetMessageServer()) server->MessagePost(&message, false);
}

void cGZClipboardManagerClipboard::NotifyState(bool up)
{
    if (!mNotify) return;
    cGZMessage message = ChangedMessage(this, up);
    if (cIGZMessageServer *const server = GetMessageServer()) server->MessageSend(&message);
}

// ---- cGZClipboardViewer ----

// The window class (procedure WindowProc, room for the viewer pointer) is registered every time, the window created,
// the viewer stored in it and the window linked into the viewer chain.
cGZClipboardViewer::cGZClipboardViewer(cGZClipboardManagerClipboard *owner)
    : mEnabled(true), mReading(0), mWriting(0), mOwner(owner), mWindow(nullptr), mNextViewer(nullptr), mLanguage(kLanguageEnglishUs)
{
    GetVersion(); // the answer is not used
    WindowClass windowClass;
    windowClass.style = 0;
    windowClass.procedure = &WindowProc;
    windowClass.classExtra = 0;
    windowClass.windowExtra = sizeof(void *);
    windowClass.instance = GetModuleHandle();
    windowClass.icon = nullptr;
    windowClass.cursor = nullptr;
    windowClass.background = nullptr;
    windowClass.menuName = nullptr;
    windowClass.className = GameAddress<const char>(kViewerClassName);
    RegisterClass(&windowClass);
    mWindow = CreateWindowEx(GameAddress<const char>(kViewerClassName), GameAddress<const char>(kViewerWindowName),
                             kDefaultPosition, GetModuleHandle());
    if (mWindow) {
        SetWindowLong(mWindow, kUserData, static_cast<int32_t>(reinterpret_cast<uintptr_t>(this)));
        mNextViewer = SetClipboardViewer(mWindow);
    }
}

void cGZClipboardViewer::Detach()
{
    if (!mWindow) return;
    ChangeClipboardChain(mWindow, mNextViewer);
    DestroyWindow(mWindow);
}

// RZText::DefaultEncoding is UTF-8, so the original's branches for a UTF-16 default encoding never run and are left out.
bool cGZClipboardViewer::ReadText(cIGZString *out)
{
    if (!mEnabled || mWriting != 0) return false;
    bool read = false;
    ++mReading;
    if (OpenClipboard()) {
        const int32_t encoding = RZText::DefaultEncoding();
        const int32_t codePage = RZText::SystemCodePage();
        if (IsClipboardFormatAvailable(kCfUnicodeText)) {
            if (void *const memory = GetClipboardData(kCfUnicodeText)) {
                std::u16string text;
                const uint32_t units = GlobalSize(memory) >> 1;
                uint32_t length = units;
                if (const char16_t *unit = static_cast<const char16_t *>(GlobalLock(memory))) {
                    // ORIGINAL-BUG: the bound moves with the pointer, so only a terminator stops the copy (of a non-empty
                    // block).
                    if (units != 0) {
                        for (; *unit; ++unit) text.push_back(*unit);
                    }
                    length = static_cast<uint32_t>(text.size());
                }
                GlobalUnlock(memory);
                if (length != 0) {
                    uint32_t size = length * kBytesPerUnit;
                    std::string converted(size, '\0');
                    RZText::Convert(text.data(), static_cast<uint32_t>(text.size()), RZText::kUtf16, converted.data(), &size,
                                    encoding);
                    converted.resize(size);
                    out->FromChar(converted.data(), static_cast<uint32_t>(converted.size()));
                    read = true;
                }
            }
        } else if (IsClipboardFormatAvailable(kCfText)) {
            if (void *const memory = GetClipboardData(kCfText)) {
                std::string text;
                const uint32_t bytes = GlobalSize(memory);
                uint32_t length = bytes;
                const char *source = nullptr;
                if (const char *byte = static_cast<const char *>(GlobalLock(memory))) {
                    // ORIGINAL-BUG: as above, only a terminator stops the copy.
                    if (bytes != 0) {
                        for (; *byte; ++byte) text.push_back(*byte);
                    }
                    length = static_cast<uint32_t>(text.size());
                    source = text.data();
                }
                GlobalUnlock(memory);
                // ORIGINAL-BUG: when the lock fails, the block's size is converted from the empty copy (the original's
                // zeroed 16-byte inline buffer, then whatever follows it); ours converts that many zero bytes.
                const std::string zeros(source ? 0 : length, '\0');
                if (!source) source = zeros.data();
                if (length != 0) {
                    uint32_t size = length * kBytesPerAnsiByte;
                    std::string converted(size, '\0');
                    RZText::Convert(source, length, codePage, converted.data(), &size, encoding);
                    converted.resize(size);
                    out->FromChar(converted.data(), static_cast<uint32_t>(converted.size()));
                    read = true;
                }
            }
        }
        CloseClipboard();
    }
    --mReading;
    return read;
}

// The clipboard emptied, then the text as ANSI (the system code page) and as Unicode, each in a new moveable block
// (freed if the clipboard refuses it). RZText::DefaultEncoding is UTF-8, so the original's extra pass for a UTF-16
// default encoding never runs and is left out.
bool cGZClipboardViewer::WriteText(cIGZString *text)
{
    if (!mEnabled || mReading != 0) return false;
    bool written = false;
    ++mWriting;
    if (OpenClipboard()) {
        const int32_t encoding = RZText::DefaultEncoding();
        const int32_t codePage = RZText::SystemCodePage();
        EmptyClipboard();

        uint32_t size = text->Strlen() * kBytesPerUnit;
        std::string ansi(size, '\0');
        uint32_t length = text->Strlen();
        RZText::Convert(text->Data(), length, encoding, ansi.data(), &size, codePage);
        void *const ansiMemory = GlobalAlloc(kMoveable, size + 1);
        char *const bytes = static_cast<char *>(GlobalLock(ansiMemory));
        std::memcpy(bytes, ansi.data(), size);
        bytes[size] = '\0';
        GlobalUnlock(ansiMemory);
        if (!SetClipboardData(kCfText, ansiMemory)) GlobalFree(ansiMemory);

        uint32_t units = text->Strlen();
        std::u16string wide(units, u'\0');
        length = text->Strlen();
        RZText::Convert(text->Data(), length, RZText::kUtf8, wide.data(), &units, RZText::kUtf16);
        void *const wideMemory = GlobalAlloc(kMoveable, units * sizeof(char16_t) + sizeof(char16_t));
        char16_t *const wideUnits = static_cast<char16_t *>(GlobalLock(wideMemory));
        std::memcpy(wideUnits, wide.data(), units * sizeof(char16_t));
        wideUnits[units] = u'\0';
        GlobalUnlock(wideMemory);
        if (!SetClipboardData(kCfUnicodeText, wideMemory)) GlobalFree(wideMemory);

        CloseClipboard();
        written = true;
    }
    --mWriting;
    return written;
}

// WM_CHANGECBCHAIN repairs the chain; WM_DRAWCLIPBOARD reads the system's text into the owner (replacing everything, with
// the reading counter up so it is not written back) and passes the notice on. Anything else, or a window without an
// enabled viewer, goes to DefWindowProcA.
intptr_t __stdcall cGZClipboardViewer::WindowProc(void *window, uint32_t message, uintptr_t wParam, intptr_t lParam)
{
    auto *const viewer = reinterpret_cast<cGZClipboardViewer *>(static_cast<uintptr_t>(GetWindowLong(window, kUserData)));
    if (!viewer || !viewer->mEnabled) return DefWindowProc(window, message, wParam, lParam);
    if (message == kChangeChain) {
        if (reinterpret_cast<void *>(wParam) == viewer->mNextViewer) {
            viewer->mNextViewer = reinterpret_cast<void *>(lParam);
        } else if (viewer->mNextViewer) {
            SendMessage(viewer->mNextViewer, kChangeChain, wParam, lParam);
        }
        return 0;
    }
    if (message != kDrawClipboard) return DefWindowProc(window, message, wParam, lParam);
    LocalString text;
    if (viewer->mOwner && viewer->ReadText(text.s.AsInterface())) {
        ++viewer->mReading;
        viewer->mOwner->SetText(*text.s.AsInterface(), true);
        --viewer->mReading;
    }
    if (viewer->mNextViewer) SendMessage(viewer->mNextViewer, kDrawClipboard, wParam, lParam);
    return 0;
}

// ---- adapters ----

cGZClipboardManagerClipboard *__fastcall cGZClipboardManagerClipboardAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZClipboardManagerClipboard;
}

ScopedRef *__fastcall cGZClipboardManagerClipboardAdapters::AcquireMessageServer(ScopedRef *ref, void *)
{
    return ::AcquireMessageServer(ref);
}

ifaces::cIGZClipboardData *__fastcall cGZClipboardManagerClipboardAdapters::ConstructData(void *memory, void *, uint32_t format,
                                                                                         cIGZUnknown *object)
{
    return ConstructClipboardData(memory, format, object);
}
