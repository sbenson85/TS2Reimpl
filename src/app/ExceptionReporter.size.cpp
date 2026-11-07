#include "app/ExceptionReporter.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/GZCOM.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"

#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <new>

namespace appbase {
namespace {

constexpr uint32_t kMaxPart = 0xff; // the part size limit SplitPath applies

// Slots the original calls through vtables.
constexpr int kSlotFromChar = 3; // cIGZString::FromChar(const char *, uint32_t)
constexpr int kSlotToChar = 5;   // cIGZString::ToChar

// The two names the constructor gives the report and the dump, and their file extensions.
constexpr const char kReportName[] = "Exception Report";
constexpr const char kDumpName[] = "Minidump";
constexpr const char kReportExtension[] = ".txt";
constexpr const char kDumpExtension[] = ".mdmp";

constexpr int kSlotWrite = 16;    // cIGZFile::Write(const void *, uint32_t)
constexpr int kSlotListener = 3;  // the listener's notification slot
constexpr int kSlotSetId = 5;     // cRZMessage2Standard::SetId

// Texts in the image: the extra-data section's header, line format and end line.
constexpr uint32_t kExtraHeader = 0xda3424, kExtraFormat = 0xda3440, kExtraFooter = 0xda3448;
constexpr uint32_t kExtraHeaderLength = 0x19, kExtraFooterLength = 0x1b;

constexpr uint32_t kIatSetUnhandledExceptionFilter = 0xd9a1b4, kIatGetCurrentProcess = 0xd9a1b8, kIatTerminateProcess = 0xd9a1c0;

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

uint32_t Min255(uint32_t n)
{
    return n < kMaxPart ? n : kMaxPart;
}

// strncpy of n bytes and a terminator at [n]; n is at most 255 here and the text has no NUL before
// it, so a plain copy gives the same bytes.
void CopyPart(char *out, const char *from, uint32_t n)
{
    std::memcpy(out, from, n);
    out[n] = 0;
}

} // namespace

// ---- 0x1c3a97 ----

void __cdecl SplitPath(const char *path, char *drive, char *dir, char *name, char *ext)
{
    const char *dot = nullptr;
    if (std::strlen(path) >= 1 && path[1] == ':') {
        if (drive) {
            std::memcpy(drive, path, 2);
            drive[2] = 0;
        }
        path += 2;
    } else if (drive) {
        drive[0] = 0;
    }
    const char *nameStart = path;
    const char *end = path;
    const char *lastSeparator = nullptr;
    for (; *end; ++end) {
        if (*end == '\\' || *end == '/') lastSeparator = end + 1;
        else if (*end == '.') dot = end;
    }
    if (lastSeparator) {
        if (dir) CopyPart(dir, path, Min255(static_cast<uint32_t>(lastSeparator - path)));
        nameStart = lastSeparator;
    } else if (dir) {
        dir[0] = 0;
    }
    // A dot before the name start (in the directory part) is no extension.
    if (dot && dot >= nameStart) {
        if (name) CopyPart(name, nameStart, Min255(static_cast<uint32_t>(dot - nameStart)));
        if (ext) CopyPart(ext, dot, Min255(static_cast<uint32_t>(end - dot)));
    } else {
        if (name) CopyPart(name, nameStart, Min255(static_cast<uint32_t>(end - nameStart)));
        if (ext) ext[0] = 0;
    }
}

// ---- 0x1c1c8c ----

uint8_t __cdecl GetModuleDirectory(cIGZString *path, cIGZString *out)
{
    char drive[3] = {};
    char dir[0x100] = {};
    char name[0x100] = {};
    char ext[0x100] = {};
    const char *text = VirtualCall<const char *(__thiscall *)(cIGZString *), kSlotToChar>(path);
    SplitPath(text, drive, dir, name, ext);
    std::string joined;
    joined.assign(drive, std::strlen(drive));
    joined.append(dir, std::strlen(dir));
    VirtualCall<void(__thiscall *)(cIGZString *, const char *, uint32_t), kSlotFromChar>(out, joined.c_str(), static_cast<uint32_t>(joined.size()));
    return joined.empty() ? 0 : 1;
}

// ---- 0x1c3f3e ----

cRZTextFile *__fastcall ConstructTextFile(cRZTextFile *self, void *)
{
    self->cRZFile::Construct();
    self->mVTable = GameAddress(cRZTextFile::kVTable);
    self->mCharSize = 0;
    self->mByteOrderMark = false;
    std::memset(&self->mLine, 0, sizeof(self->mLine));
    new (&self->mLine) std::string();
    std::memset(&self->mWide, 0, sizeof(self->mWide));
    new (&self->mWide) std::u16string();
    self->mText.Construct();
    self->SetBuffering(cRZTextFile::kBufferSize, cRZTextFile::kBufferSize);
    return self;
}

// ---- 0x1c4347 ----

uint8_t __fastcall WriteLine(cRZTextFile *file, void *, const char *text, uint32_t length)
{
    using Write = uint8_t(__thiscall *)(void *, const void *, uint32_t);
    if (!VirtualCall<Write, kSlotWrite>(file, text, length)) return 0;
    if (file->mCharSize == 2) {
        const uint16_t lineEnd[2] = {0x0d, 0x0a};
        return VirtualCall<Write, kSlotWrite>(file, lineEnd, 4);
    }
    const uint16_t lineEnd = 0x0a0d;
    return VirtualCall<Write, kSlotWrite>(file, &lineEnd, 2);
}

// ---- 0x1bd001 ----

void __cdecl RemoveFilterAndTerminate()
{
    Import<void *(WINAPI *)(void *)>(kIatSetUnhandledExceptionFilter)(nullptr);
    HANDLE process = Import<HANDLE(WINAPI *)()>(kIatGetCurrentProcess)();
    Import<BOOL(WINAPI *)(HANDLE, UINT)>(kIatTerminateProcess)(process, 0xffffffffu);
}

// ---- 0x1bd42a, 0x1bac86, 0x1bac12, 0x1bb00a ----

void ExceptionReporterObject::SetDirectory(const cRZString *directory)
{
    reinterpret_cast<RZStringSlot *>(&mDirectory.mString)->Assign(directory->mString);
    // The last character is read at data()[size - 1] without a size check, as in the original.
    char last = mDirectory.mString.data()[mDirectory.mString.size() - 1];
    if (last != '\\' && last != '/') mDirectory.mString.push_back('\\');
}

void ExceptionReporterObject::AddListener(cIGZUnknown *listener)
{
    using Ref = uint32_t(__thiscall *)(void *);
    if (listener) VirtualCall<Ref, 1>(listener);
    // The list's node holds a counted reference (its element is an auto-reference type: the node's
    // copy takes one), so one reference stays after the Release below. ORIGINAL-BUG: the reporter's
    // destructor frees the nodes with the plain list cleanup and never releases them (a leak at exit).
    if (listener) VirtualCall<Ref, 1>(listener);
    mListeners.push_back(listener);
    if (listener) VirtualCall<Ref, 2>(listener);
}

void ExceptionReporterObject::AddExtraData(const char *label, const char *value, uint32_t)
{
    if (!mExtraStarted) {
        mExtraStarted = 1;
        WriteLine(&mLog, nullptr, GameAddress<const char>(kExtraHeader), kExtraHeaderLength);
    }
    cRZString text;
    text.Construct();
    text.Sprintf(GameAddress<const char>(kExtraFormat), label, value);
    WriteLine(&mLog, nullptr, text.mString.c_str(), static_cast<uint32_t>(text.mString.size()));
    text.Destruct();
}

void ExceptionReporterObject::WriteExtraDataEnd()
{
    if (mExtraStarted) WriteLine(&mLog, nullptr, GameAddress<const char>(kExtraFooter), kExtraFooterLength);
}

// ---- 0x1bd39f, 0x1bd339 ----

namespace {
// No C++ objects in a function with __try.
uint32_t CallListener(cIGZUnknown *listener, void *message)
{
    __try {
        return VirtualCall<uint32_t(__thiscall *)(void *, void *), kSlotListener>(listener, message);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0; // unspecified in the original (whatever eax held)
    }
}
} // namespace

uint32_t ExceptionReporterObject::NotifyListener(cIGZUnknown *listener, void *message)
{
    return CallListener(listener, message);
}

void ExceptionReporterObject::Broadcast(uint32_t id)
{
    using Ref = uint32_t(__thiscall *)(void *);
    auto *message = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (message) {
        // The message constructor (0x1c033d): vtable, zero count, flags, id, values and pointers.
        message->mVTable = &ts2_rva_da4064;
        message->mRefCount = 0;
        message->mFlags = 0;
        message->mId = 0;
        for (uint32_t &value : message->mValue) value = 0;
        message->mPtr20 = message->mPtr24 = message->mPtr28 = nullptr;
        VirtualCall<Ref, 1>(message);
    }
    // A failed allocation crashes here, as in the original (a call through a null object).
    VirtualCall<void(__thiscall *)(void *, uint32_t), kSlotSetId>(message, id);
    for (auto it = mListeners.begin(); it != mListeners.end(); ++it) NotifyListener(static_cast<cIGZUnknown *>(*it), message);
    if (message) VirtualCall<Ref, 2>(message);
}

// ---- 0x1ba914 ----

ExceptionReporterObject *ExceptionReporterObject::Construct()
{
    auto *setters = reinterpret_cast<cExceptionReporter *>(this);
    mDirectory.Construct();
    mReportName.Construct(kReportName);
    mDumpName.Construct(kDumpName);
    mReportFile.Construct();
    mDumpFile.Construct();
    ConstructTextFile(&mLog, nullptr);
    std::memset(mLevels, 0, sizeof(mLevels));
    mFlag19a = 0;
    mEnabled = 0;
    new (&mListeners) std::list<uint32_t>();
    for (uint32_t level = 0; level < 6; ++level) setters->SetLevel(level, 1);

    cRZString module;
    module.Construct();
    GetModulePath(module.AsInterface());
    GetModuleDirectory(module.AsInterface(), mDirectory.AsInterface());

    // The file names: the report and dump names plus their extensions (the original's concatenation 0x1ba0fb is
    // std::string's operator+, library code).
    reinterpret_cast<RZStringSlot *>(&mReportFile.mString)->Assign(mReportName.mString + kReportExtension);
    reinterpret_cast<RZStringSlot *>(&mDumpFile.mString)->Assign(mDumpName.mString + kDumpExtension);

    *GameAddress<ExceptionReporterObject *>(kInstanceGlobal) = this;
    setters->SetEnabled(1);
    module.Destruct();
    return this;
}

// ---- 0x1baae3 ----

void ExceptionReporterObject::Destruct()
{
    reinterpret_cast<cExceptionReporter *>(this)->SetEnabled(0);
    *GameAddress<ExceptionReporterObject *>(kInstanceGlobal) = nullptr;
    mListeners.~list();
    mLog.Destruct();
    mDumpFile.Destruct();
    mReportFile.Destruct();
    mDumpName.Destruct();
    mReportName.Destruct();
    mDirectory.Destruct();
}

} // namespace appbase
