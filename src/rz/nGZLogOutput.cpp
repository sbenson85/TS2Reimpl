// nGZLog's output back ends (lane LY, F4): the base's destructor, deleting destructor and configuration text
// (slot 19), the default (stream) back end, the log file back end and the window caption back end. The base's
// small slots are in nGZLogBackEndSlots.size.cpp; the constructors of the base
// and the caption back end are in nGZLog.size.cpp.
#include "rz/nGZLog.h"

#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"
#include "rz/RZFileStreams.h"
#include "rz/RZFrameworkHelpers.h"

#include <new>

#include <windows.h>

namespace nGZLog {
namespace {

// Slots of the objects a back end holds.
constexpr int kSlotRelease = 2;            // cIGZUnknown::Release
using ReleaseFn = uint32_t(__thiscall *)(void *);
constexpr int kNamedSlotName = 7;         // a filter's or formatter's name (c_str)
constexpr int kFormatterSlotFormat = 4;    // a formatter's text for a record (cRZString *)
constexpr int kStreamSlotFlush = 3;        // the stream (cRZOStream) flushes
constexpr int kStreamSlotInteger = 8;      // writes a decimal number
constexpr int kStreamSlotText = 14;        // writes a C string
constexpr int kStreamSlotString = 15;      // writes a cIGZString; false on failure
using NameFn = const char *(__thiscall *)(void *);
using FormatFn = const cIGZString *(__thiscall *)(void *, void *);
using FlushFn = void(__thiscall *)(void *);
using IntegerFn = void(__thiscall *)(void *, int32_t);
using TextFn = void(__thiscall *)(void *, const char *);
using StringFn = bool(__thiscall *)(void *, const cIGZString *);

// Slots of the back end's own vtable called through it.
constexpr int kSlotSetEnabled = 12;
constexpr int kSlotTickEveryN = 18;
using SetEnabledFn = void(__thiscall *)(void *, uint8_t);
using TickFn = bool(__thiscall *)(void *);

// The configuration text's words.
constexpr char kReporterWord[] = "logReporterProp ";
constexpr char kFormatterOption[] = " -formatter ";
constexpr char kFilterOption[] = " -filter ";
constexpr char kFlushOption[] = " -flush ";
constexpr char kSynchOption[] = " -synch ";
constexpr char kEnabledOption[] = " -enabled ";
constexpr char kTrue[] = "true";
constexpr char kFalse[] = "false";
constexpr char kNewline[] = "\n";
constexpr char kNoName[] = "\"\""; // a missing filter or formatter is written as a pair of quotes

// The caption back end: the window's interface and its slot that gives the window handle.
constexpr uint32_t kWindowInterface = 0x23ea70a1;
constexpr int kWindowSlotHandle = 5;
constexpr int kFrameworkSlotQuery = 0;
constexpr uint32_t kCaptionLimit = 0x50;
constexpr uint32_t kIatIsWindow = 0xd9a5c4;
constexpr uint32_t kIatSetWindowTextA = 0xd9a598;
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using HandleFn = HWND(__thiscall *)(void *);
using IsWindowFn = BOOL(WINAPI *)(HWND);
using SetWindowTextAFn = BOOL(WINAPI *)(HWND, const char *);

const char *NameOf(void *named)
{
    return named ? VirtualCall<NameFn, kNamedSlotName>(named) : kNoName;
}

void ReleaseIfAny(void *object)
{
    if (object) VirtualCall<ReleaseFn, kSlotRelease>(object);
}

} // namespace

// ---- the base ----------------------------------------------------------------------------------------

void cLogOutputBackEnd::Destruct()
{
    mVTable = GameAddress(kVTable);
    mText.Destruct();
    ReleaseIfAny(mFormatter);
    ReleaseIfAny(mFilter);
    ReleaseIfAny(mStream.mObject);
}

cLogOutputBackEnd *cLogOutputBackEnd::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

void cLogOutputBackEnd::WriteConfig(void *stream)
{
    const char *filter = NameOf(mFilter);
    const char *formatter = NameOf(mFormatter);
    auto text = [stream](const char *s) { VirtualCall<TextFn, kStreamSlotText>(stream, s); };
    text(kReporterWord);
    VirtualCall<StringFn, kStreamSlotString>(stream, mText.AsInterface());
    text(kFormatterOption);
    text(formatter);
    text(kFilterOption);
    text(filter);
    text(kFlushOption);
    VirtualCall<IntegerFn, kStreamSlotInteger>(stream, mInterval);
    text(kSynchOption);
    text(mFlag ? kTrue : kFalse);
    text(kEnabledOption);
    text(mEnabled ? kTrue : kFalse);
    text(kNewline);
}

// ---- the default back end: a stream ---------------------------------------------------------------------

cLogDefaultOutputBackEnd *cLogDefaultOutputBackEnd::Construct(const char *text, void *stream)
{
    mBase.Construct();
    mBase.mVTable = GameAddress(kVTable);
    mBase.mText.mString.assign(text); // the original calls the base's SetText (slot 10) directly
    mBase.mStream.Assign(stream);
    return this;
}

void cLogDefaultOutputBackEnd::Destruct()
{
    mBase.mVTable = GameAddress(kVTable);
    mBase.Destruct();
}

cLogDefaultOutputBackEnd *cLogDefaultOutputBackEnd::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

void cLogDefaultOutputBackEnd::Output(void *record)
{
    void *stream = mBase.mStream.mObject;
    if (!stream) return;
    const cIGZString *text = VirtualCall<FormatFn, kFormatterSlotFormat>(mBase.mFormatter, record);
    if (!VirtualCall<StringFn, kStreamSlotString>(stream, text)) {
        // The stream failed: drop it and turn this back end off.
        mBase.mStream.Assign(nullptr);
        VirtualCall<SetEnabledFn, kSlotSetEnabled>(this, uint8_t{0});
        return;
    }
    if (VirtualCall<TickFn, kSlotTickEveryN>(this)) VirtualCall<FlushFn, kStreamSlotFlush>(mBase.mStream.mObject);
}

// ---- the log file back end ----------------------------------------------------------------------------

cLogFileOutputBackEnd *cLogFileOutputBackEnd::Construct(const char *text, const char *path, uint8_t append, uint8_t deferOpen)
{
    // Opened now unless deferred; a deferred open (slot 5 or 25) uses mPath and mAppend, which nothing here sets.
    cRZOFStream *stream = nullptr;
    if (!deferOpen) {
        void *memory = ::operator new(sizeof(cRZOFStream));
        if (memory) stream = static_cast<cRZOFStream *>(memory)->Construct(path, 0, 0, 0, append);
    }
    mDefault.Construct(text, stream);
    mDefault.mBase.mVTable = GameAddress(kVTable);
    mPath.Construct();
    mDeferOpen = deferOpen;
    return this;
}

void cLogFileOutputBackEnd::Destruct()
{
    mPath.Destruct();
    mDefault.Destruct();
}

cLogFileOutputBackEnd *cLogFileOutputBackEnd::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

cRZOFStream *cLogFileOutputBackEnd::OpenStream()
{
    void *memory = ::operator new(sizeof(cRZOFStream));
    if (!memory) return nullptr;
    return static_cast<cRZOFStream *>(memory)->Construct(*mPath.AsInterface(), 0, 0, 0, mAppend);
}

void cLogFileOutputBackEnd::Output(void *record)
{
    if (mDeferOpen && !mDefault.mBase.mStream.mObject) mDefault.mBase.mStream.Assign(OpenStream());
    mDefault.Output(record);
}

void cLogFileOutputBackEnd::Reopen()
{
    if (mAppend && mDefault.mBase.mStream.mObject) return;
    mDefault.mBase.mStream.Assign(nullptr);
    mDefault.mBase.mStream.Assign(OpenStream());
}

// ---- the window caption back end ---------------------------------------------------------------------

void cLogCaptionOutputBackEnd::Destruct()
{
    mBase.mVTable = GameAddress(kVTable);
    mLock.Destruct();
    mCaption.Destruct();
    mBase.Destruct();
}

cLogCaptionOutputBackEnd *cLogCaptionOutputBackEnd::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

void cLogCaptionOutputBackEnd::Output(void *record)
{
    mLock.Lock();
    const cIGZString *text = VirtualCall<FormatFn, kFormatterSlotFormat>(mBase.mFormatter, record);
    mCaption.Assign(*text);
    if (mCaption.Strlen() > kCaptionLimit) mCaption.mString.resize(kCaptionLimit, 0);
    for (uint32_t i = 0; i < mCaption.Strlen(); ++i) {
        if (mCaption.mString[i] == '\n') mCaption.mString[i] = ' ';
    }
    mLock.Unlock();
}

void cLogCaptionOutputBackEnd::Flush()
{
    mLock.Lock();
    if (!mCaption.mString.empty()) {
        void *window = nullptr;
        void *framework = reinterpret_cast<void *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
        if (VirtualCall<QueryFn, kFrameworkSlotQuery>(framework, kWindowInterface, &window)) {
            HWND handle = VirtualCall<HandleFn, kWindowSlotHandle>(window);
            if (handle && ImportFunction<IsWindowFn, kIatIsWindow>()(handle)) {
                ImportFunction<SetWindowTextAFn, kIatSetWindowTextA>()(handle, mCaption.ToChar());
                mCaption.mString.clear();
            }
        }
        ReleaseIfAny(window);
    }
    mLock.Unlock();
}

// ---- the service's slots that make back ends -------------------------------------------------------------

namespace {
constexpr int kServiceSlotAddOutput = 11;
using AddOutputFn = uint32_t(__thiscall *)(void *, const char *, void *);
} // namespace

uint32_t __fastcall ServiceAddStreamOutput(void *sub, void *, const char *name, void *stream)
{
    void *memory = ::operator new(cLogDefaultOutputBackEnd::kSize);
    cLogDefaultOutputBackEnd *output = nullptr;
    if (memory) output = static_cast<cLogDefaultOutputBackEnd *>(memory)->Construct(name, stream);
    return VirtualCall<AddOutputFn, kServiceSlotAddOutput>(sub, name, output);
}

uint32_t __fastcall ServiceAddFileOutput(void *sub, void *, const char *name, const char *path, uint8_t append, uint8_t deferOpen)
{
    void *memory = ::operator new(cLogFileOutputBackEnd::kSize);
    cLogFileOutputBackEnd *output = nullptr;
    if (memory) output = static_cast<cLogFileOutputBackEnd *>(memory)->Construct(name, path, append, deferOpen);
    return VirtualCall<AddOutputFn, kServiceSlotAddOutput>(sub, name, output);
}

} // namespace nGZLog
