// cRZStd's three slots and their helpers (lane LZ, F4c): the assertion (logged to the log service, reported to the
// framework's output streams or the debugger and an optional file, asked about in a message box), its printf form and
// the report line.
#include "rz/cRZStd.h"

#include "app/AppDebugger.h" // LogServiceHolder, StoreCurrentThreadId
#include "game/game.h"
#include "game/import_slots.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZCriticalSection.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZFrameWorkW32.h"
#include "ifaces/cIGZMessageServer.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZSystemService.h"
#include "rz/RZFileStreams.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

#pragma warning(disable : 4996) // _vsnprintf_l, _snprintf and fopen, as the game calls them

namespace {

constexpr IatSlot kIatGetEnvironmentVariableA = &ts2_rva_d9a164;
constexpr IatSlot kIatOutputDebugStringA = &ts2_rva_d9a110;
constexpr IatSlot kIatGetModuleFileNameA = &ts2_rva_d9a16c;
constexpr IatSlot kIatGetFullPathNameA = &ts2_rva_d9a1ec;
constexpr IatSlot kIatGetCurrentProcessId = &ts2_rva_d9a1bc;
constexpr IatSlot kIatMessageBoxA = &ts2_rva_d9a568;
constexpr IatSlot kIatIsWindow = &ts2_rva_d9a5c4;
constexpr IatSlot kIatGetWindowThreadProcessId = &ts2_rva_d9a5c0;

constexpr size_t kTextSize = 0x400;          // the slots' formatted text
constexpr size_t kLocatedTextSize = 0x400;   // Output's "file (line): text"
constexpr size_t kLocatedTextLimit = 0x3ff;  // what Output lets _snprintf write
constexpr size_t kPathSize = 0x200;          // RZSTD_OUTPUT_FILE_PATH
constexpr size_t kPrefixSize = 0x80;         // RZSTD_OUTPUT_PREFIX
constexpr size_t kLevelTextSize = 0x10;      // GZFRAMEWORK_DEBUG_LEVEL
constexpr uint32_t kModulePathSize = 0x104;

constexpr int32_t kLevelOff = 0;
constexpr int32_t kLevelNoAsk = 1;
constexpr int32_t kLevelReport = 2;
constexpr int32_t kLevelFromMode = -1;

constexpr int32_t kAnswerContinue = 0;
constexpr int32_t kAnswerBreak = 1;
constexpr int32_t kAnswerIgnore = 2;
constexpr UINT kBoxStyle = MB_TOPMOST | MB_TASKMODAL | MB_DEFBUTTON2 | MB_ICONEXCLAMATION | MB_ABORTRETRYIGNORE;

// The log service's enabled check and write (no method facts for its cIService face).
constexpr int kLogServiceSlotWrite = 28;
// The output streams' text write (cRZTextOStream::SetRZCharStr).
constexpr int kStreamSlotWriteText = 14;

// The framework's window face and message server, and the two messages around the box.
constexpr uint32_t kSystemServiceIid = 0x287fb697; // the log service's cIGZSystemService face
constexpr uint32_t kWindowFaceId = 0x23ea70a1;
constexpr uint32_t kMessageServerId = 0x64063387;
constexpr uint32_t kMessageServerIid = 0xc1085722;
constexpr uint32_t kAssertBeginMessage = 0x676885e7;
constexpr uint32_t kAssertEndMessage = 0xa7688676;

// The message the box is announced with (a cGZMessage-shaped block: type and three words).
struct AssertMessage {
    uint32_t mType;
    uint32_t mData[3];
};

ifaces::cIGZFrameWork *Framework()
{
    return reinterpret_cast<ifaces::cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
}

int32_t &DebugLevel()
{
    return ts2_rva_10cb5d4;
}

// The framework's stream for a report: StdOut with the flag, else StdErr.
void *ReportStream(ifaces::cIGZFrameWork *framework, bool stdOut)
{
    return stdOut ? framework->StdOut() : framework->StdErr();
}

// Output through the framework's stream (held for the call), or to the debugger without a framework.
void ToStream(ifaces::cIGZFrameWork *framework, bool stdOut, const char *text, const char *file, uint32_t line)
{
    if (!framework) {
        rzstd::Output(nullptr, text, file, line);
        return;
    }
    void *stream = ReportStream(framework, stdOut);
    if (stream) As<ifaces::cIGZOStream>(stream)->AddRef();
    rzstd::Output(stream, text, file, line);
    if (stream) As<ifaces::cIGZOStream>(stream)->Release();
}

// The framework's debug level: its own when there is one, else GZFRAMEWORK_DEBUG_LEVEL (a number other than 2, of 1 to
// 15 characters); otherwise the level stays.
void UpdateDebugLevel(ifaces::cIGZFrameWork *framework)
{
    if (framework) {
        DebugLevel() = framework->GetDebugLevel();
        return;
    }
    char text[kLevelTextSize];
    const uint32_t length = rzstd::EnvironmentText("GZFRAMEWORK_DEBUG_LEVEL", text, sizeof(text));
    if (length - 1 < sizeof(text) - 1 && std::atoi(text) != kLevelReport) DebugLevel() = std::atoi(text);
}

// The assertion's work, inside the slot's catch(...). `result` is the box's answer once it has one.
void AssertBody(cRZStd *self, const char *text, int32_t *result)
{
    const uint32_t flags = self->mFlags;
    const char *file = (flags & cRZStd::kFlagWithFile) ? self->File() : nullptr;
    const uint8_t report = (flags & cRZStd::kFlagReport) ? 1 : 0;
    const bool stdOut = report && (flags & cRZStd::kFlagStdOut);
    uint8_t ask = (flags & cRZStd::kFlagAsk) ? 1 : 0;
    ifaces::cIGZFrameWork *framework = Framework();
    UpdateDebugLevel(framework);

    const int32_t level = DebugLevel();
    if (level == kLevelOff) return;
    uint8_t show = report;
    if (level == kLevelNoAsk) {
        ask = 0;
    } else if (level != kLevelReport) {
        // ORIGINAL-BUG: above level 2 the box is asked for when the text pointer's top byte is non-zero, and the
        // report flag is never set (whatever was on the stack). Here it is the report bit, as for levels 1 and 2.
        ask = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(text) >> 24);
    }
    rzstd::LogLine(ts2_rva_da3f88, kLevelFromMode, self->Mode(), text, nullptr,
                   self->File(), self->mSecond, &show);
    if (show) ToStream(framework, stdOut, text, file, self->mSecond);
    if (!ask) return;
    const int32_t answer = framework ? rzstd::AskWithFramework(framework, text, file, self->mSecond)
                                     : rzstd::AskBox(nullptr, text, file, self->mSecond);
    *result = answer;
    if (answer == kAnswerIgnore) self->mFlags &= ~(cRZStd::kFlagReport | cRZStd::kFlagAsk);
}

void ReportBody(cRZStd *self, const char *group, int32_t level, const char *format, va_list args)
{
    ifaces::cIGZFrameWork *framework = Framework();
    uint8_t show = 1;
    rzstd::LogLine(group, level, self->Mode(), format, args, self->File(), self->mSecond, &show);
    if (!show) return;
    // ORIGINAL-BUG: a line of 0x400 characters or more is cut without its terminator.
    char text[kTextSize];
    _vsnprintf_l(text, sizeof(text), format, nullptr, args);
    ToStream(framework, (self->mFlags & cRZStd::kFlagStdOut) != 0, text, self->File(), self->mSecond);
}

// The level a mode logs at when the caller gives none.
int32_t LevelForMode(uint32_t mode, int32_t level)
{
    switch (mode) {
    case 0:
    case 1:
    case 2:
    case 6:
        return 2;
    case 3:
        return 3;
    case 4:
        return 6;
    case 5:
    case 7:
        return 5;
    default:
        return level;
    }
}

// Sends one of the box's messages through the framework's message server, when it has one.
void Announce(ifaces::cIGZFrameWork *framework, uint32_t type)
{
    void *server = nullptr;
    if (!framework->GetSystemService(kMessageServerId, kMessageServerIid, &server)) return;
    AssertMessage message = {type, {0, 0, 0}};
    As<ifaces::cIGZMessageServer>(server)->MessageSend(&message);
    static_cast<cIGZUnknown *>(server)->Release();
}

} // namespace

int32_t __cdecl cRZStd::Printf(cRZStd *self, const char *format, ...)
{
    if (!format) return 0;
    // ORIGINAL-BUG: a text of 0x400 characters or more is cut without its terminator.
    char text[kTextSize];
    va_list args;
    va_start(args, format);
    _vsnprintf_l(text, sizeof(text), format, nullptr, args);
    va_end(args);
    return VirtualCall<int32_t(__thiscall *)(cRZStd *, const char *), kSlotAssert>(self, text);
}

int32_t cRZStd::Assert(const char *text)
{
    int32_t result = kAnswerContinue;
    if (!(mFlags & (kFlagAsk | kFlagReport)) || !text) return result;
    // The original's catch(...) (built /EHa) hands the text to the debugger and returns the answer so far.
    __try {
        AssertBody(this, text, &result);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rzstd::DebugText(text);
    }
    return result;
}

int32_t __cdecl cRZStd::Report(cRZStd *self, const char *group, int32_t level, const char *format, ...)
{
    if (!format) return 0;
    va_list args;
    va_start(args, format);
    // As in Assert: the catch(...) gives the format to the debugger.
    __try {
        ReportBody(self, group, level, format, args);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rzstd::DebugText(format);
    }
    va_end(args);
    return 0;
}

namespace rzstd {

void __cdecl LogLine(const char *group, int32_t level, uint32_t mode, const char *format, va_list args, const char *file,
                     uint32_t line, uint8_t *pending)
{
    appdebugger::LogServiceHolder holder;
    holder.Acquire();
    if (holder.mService) {
        void *system = nullptr;
        if (static_cast<cIGZUnknown *>(holder.mService)->QueryInterface(kSystemServiceIid, &system) &&
            As<ifaces::cIGZSystemService>(system)->IsServiceRunning()) {
            if (level == kLevelFromMode) level = LevelForMode(mode, level);
            using WriteFn = void(__thiscall *)(void *, const char *, uint32_t, const char *, int32_t, const char *, va_list);
            VirtualCall<WriteFn, kLogServiceSlotWrite>(holder.mService, file, line, group, level, format, args);
            *pending = 0;
        }
        if (system) As<ifaces::cIGZSystemService>(system)->Release();
    }
    holder.Release();
}

void __cdecl Output(void *stream, const char *text, const char *file, uint32_t line)
{
    char located[kLocatedTextSize];
    int32_t length = 0;
    const char *shown = text;
    if (file) {
        // ORIGINAL-BUG: a line of 0x3ff characters or more is cut without its terminator.
        length = _snprintf(located, kLocatedTextLimit, "%s (%d): %s", file, line, text);
        shown = located;
    }
    if (stream && VirtualCall<uint8_t(__thiscall *)(void *, const char *), kStreamSlotWriteText>(stream, shown)) return;

    alignas(4) uint8_t debuggerBlock[sizeof(cRZTextODSStream)];
    cRZTextODSStream *debugger = reinterpret_cast<cRZTextODSStream *>(debuggerBlock)->Construct();
    debugger->SetRZCharStr(shown);
    char path[kPathSize];
    std::memset(path, 0, sizeof(path));
    const uint32_t pathLength = EnvironmentText("RZSTD_OUTPUT_FILE_PATH", path, sizeof(path));
    path[sizeof(path) - 1] = 0;
    if (pathLength != 0 && pathLength < sizeof(path)) {
        char prefix[kPrefixSize];
        std::memset(prefix, 0, sizeof(prefix));
        EnvironmentText("RZSTD_OUTPUT_PREFIX", prefix, sizeof(prefix));
        prefix[sizeof(prefix) - 1] = 0;
        if (FILE *out = std::fopen(path, "ab")) {
            if (prefix[0]) std::fwrite(prefix, std::strlen(prefix), 1, out);
            if (length == 0) length = static_cast<int32_t>(std::strlen(text));
            // ORIGINAL-BUG: a cut line (_snprintf's -1) is written with that length.
            std::fwrite(shown, static_cast<size_t>(length), 1, out);
            std::fclose(out);
        }
    }
    debugger->Destruct();
}

int32_t __cdecl AskBox(void *window, const char *text, const char *file, uint32_t line)
{
    int32_t answer = kAnswerBreak;
    cRZString caption;
    caption.Construct("Assert failed");
    char modulePath[kModulePathSize];
    char fullPath[kModulePathSize];
    char *filePart = nullptr;
    if (ImportSlot<decltype(&GetModuleFileNameA), kIatGetModuleFileNameA>()(nullptr, modulePath, kModulePathSize) &&
        ImportSlot<decltype(&GetFullPathNameA), kIatGetFullPathNameA>()(modulePath, kModulePathSize, fullPath, &filePart)) {
        // The caption with the program's name is built but not used: the box shows the plain one.
        caption.Sprintf("Assert failed - %s (process id: %d)", filePart,
                        ImportSlot<decltype(&GetCurrentProcessId), kIatGetCurrentProcessId>()());
    }
    cRZString message;
    message.Construct();
    if (file)
        message.Sprintf("%sFailed in %s at line %d\nAbort to break, Retry to continue, Ignore to remove assert", text, file, line);
    else
        message.Sprintf("%sFailed\nAbort to break, Retry to continue, Ignore to remove assert", text);
    switch (ImportSlot<decltype(&MessageBoxA), kIatMessageBoxA>()(static_cast<HWND>(window), message.ToChar(),
                                                                      "Assert failed", kBoxStyle)) {
    case IDRETRY:
        answer = kAnswerContinue;
        break;
    case IDIGNORE:
        answer = kAnswerIgnore;
        break;
    default:
        break;
    }
    message.Destruct();
    caption.Destruct();
    return answer;
}

int32_t __cdecl AskWithFramework(void *framework, const char *text, const char *file, uint32_t line)
{
    auto *fw = static_cast<ifaces::cIGZFrameWork *>(framework);
    auto *lock = As<ifaces::cIGZCriticalSection>(static_cast<void *>(&ts2_rva_10cb5cc));
    lock->Lock();
    Announce(fw, kAssertBeginMessage);
    HWND window = nullptr;
    void *face = nullptr;
    if (fw->QueryInterface(kWindowFaceId, &face)) {
        window = static_cast<HWND>(As<ifaces::cIGZFrameWorkW32>(face)->GetMainHWND());
        uint32_t thread = 0;
        appdebugger::StoreCurrentThreadId(&thread);
        if (window && !(ImportSlot<decltype(&IsWindow), kIatIsWindow>()(window) &&
                        ImportSlot<decltype(&GetWindowThreadProcessId), kIatGetWindowThreadProcessId>()(window, nullptr) == thread))
            window = nullptr;
        As<ifaces::cIGZFrameWorkW32>(face)->Release();
    }
    const int32_t answer = AskBox(window, text, file, line);
    Announce(fw, kAssertEndMessage);
    lock->Unlock();
    return answer;
}

uint32_t __cdecl EnvironmentText(const char *name, char *buffer, uint32_t size)
{
    return ImportSlot<decltype(&GetEnvironmentVariableA), kIatGetEnvironmentVariableA>()(name, buffer, size);
}

uint8_t __cdecl DebugText(const char *text)
{
    ImportSlot<decltype(&OutputDebugStringA), kIatOutputDebugStringA>()(text);
    return 1;
}

} // namespace rzstd
