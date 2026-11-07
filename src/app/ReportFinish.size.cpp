#include "app/ReportFinish.h"

#include "app/ReportBlockers.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/GZCOM.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/RZTextConvert.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"

#include <cstring>

namespace appbase {
namespace {

constexpr uint32_t kIatGetCurrentThread = 0xd9a190, kIatGetCurrentProcess = 0xd9a1b8;

// Texts in the image.
constexpr uint32_t kTextCallStack = 0xda39c8;     // "\r\nCall stack:"
constexpr uint32_t kTextMinimalInfo = 0xda39d8;   // "(Minimal info present due to outdated or absent DbgHelp.dll)."
constexpr uint32_t kTextFrame = 0xda3a18;         // "0x%04x:0x%08x %s %hs() + %d"
constexpr uint32_t kTextSourceLine = 0xda3a34;    // " \"%hs\", line %d"
constexpr uint32_t kTextUnknownCode = 0xda3a44;   // ": <unknown code>"
constexpr uint32_t kTextPdbAbsent = 0xda3a58;     // "(Debug information (.pdb files) appears to be absent)."
constexpr uint32_t kTextModuleOnly = 0xda3a90;    // "0x%04x:0x%08x %s."
constexpr uint32_t kTextColon = 0xda2cd8;         // ": "
constexpr uint32_t kUnknownCodeLength = 0x10;

constexpr int kSlotLength = 7; // cIGZFile::Length
constexpr uint32_t kMaxReportLength = 200000; // 0x30d40: the walk stops once the report file is this long
constexpr int kMaxFrames = 500;               // 0x1f4
constexpr uint32_t kMachineI386 = 0x14c;

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

void WriteText(ExceptionReporterObject *self, const cRZString &text)
{
    WriteLine(&self->mLog, nullptr, text.mString.c_str(), static_cast<uint32_t>(text.mString.size()));
}

} // namespace

// ---- 0x1bc29e ----

void __fastcall WriteCallStack(ExceptionReporterObject *self, void *, const CONTEXT *context)
{
    DbgHelpTable *const table = GameAddress<DbgHelpTable>(DbgHelpTable::kGlobal);
    cRZString text;
    text.Construct();
    void *const process = reinterpret_cast<void *>(static_cast<uintptr_t>(Import<DWORD_PTR(WINAPI *)()>(kIatGetCurrentProcess)()));
    void *const thread = reinterpret_cast<void *>(static_cast<uintptr_t>(Import<DWORD_PTR(WINAPI *)()>(kIatGetCurrentThread)()));

    CONTEXT ctx;
    static_assert(sizeof(CONTEXT) == 0x2cc, "x86 CONTEXT");
    std::memcpy(&ctx, context, sizeof(ctx));

    text.Sprintf(GameAddress<const char>(kTextCallStack));
    WriteText(self, text);
    if (!table->symGetSymFromAddr || !table->versionAtLeast51) {
        text.Sprintf(GameAddress<const char>(kTextMinimalInfo));
        WriteText(self, text);
    }

    StackFrame32 frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.pc.offset = ctx.Eip;
    frame.stack.offset = ctx.Esp;
    frame.pc.mode = 3;
    frame.frame.offset = ctx.Ebp;
    frame.frame.mode = 3;
    frame.stack.mode = 3;

    using StackWalkFn = BOOL(WINAPI *)(DWORD, void *, void *, StackFrame32 *, CONTEXT *, void *, void *, void *, void *);
    using SymFromAddrFn = BOOL(WINAPI *)(void *, DWORD, DWORD *, ImageHelpSymbol *);
    using LineFromAddrFn = BOOL(WINAPI *)(void *, DWORD, DWORD *, ImageHelpLine *);
    using UndecorateFn = DWORD(WINAPI *)(const char *, char *, DWORD, DWORD);

    for (int frameIndex = 0; frameIndex < kMaxFrames; ++frameIndex) {
        // The report file is a text file: slot 7 is its length.
        if (VirtualSlot<uint32_t(__thiscall *)(void *)>(&self->mLog, kSlotLength)(&self->mLog) >= kMaxReportLength) break;
        if (!table->stackWalk) break;
        if (!reinterpret_cast<StackWalkFn>(table->stackWalk)(kMachineI386, process, thread, &frame, &ctx, nullptr, table->symFunctionTableAccess,
                                                              table->symGetModuleBase, nullptr))
            break;
        if (!frame.frame.offset) break;

        DWORD displacement = 0;
        ImageHelpSymbol symbol; // not cleared: the lookup fills what is read
        symbol.sizeOfStruct = 0x218;
        symbol.maxNameLength = 0x200;
        bool haveSymbol = false;
        if (table->symGetSymFromAddr)
            haveSymbol = reinterpret_cast<SymFromAddrFn>(table->symGetSymFromAddr)(process, frame.pc.offset, &displacement, &symbol) != 0;

        if (haveSymbol) {
            cRZString module;
            module.Construct();
            uint32_t section = 0, offset = 0;
            FindModuleOfAddress(reinterpret_cast<const void *>(static_cast<uintptr_t>(frame.pc.offset)), module.AsInterface(), &section, &offset);
            const char *name = symbol.name;
            char undecorated[0x200];
            if (table->unDecorateSymbolName &&
                reinterpret_cast<UndecorateFn>(table->unDecorateSymbolName)(symbol.name, undecorated, 0x200, 0))
                name = undecorated;
            text.Sprintf(GameAddress<const char>(kTextFrame), section, offset, module.mString.c_str(), name, displacement);
            WriteText(self, text);
            if (table->symGetLineFromAddr) {
                ImageHelpLine line;
                line.sizeOfStruct = 0x14;
                // Every frame but the first is a return address: look up the call, one byte earlier.
                DWORD lineAddress = frameIndex == 0 ? frame.pc.offset : frame.pc.offset - 1;
                if (reinterpret_cast<LineFromAddrFn>(table->symGetLineFromAddr)(process, lineAddress, &displacement, &line)) {
                    cRZString source;
                    source.Construct();
                    if (ReadFileLine(line.fileName, static_cast<int32_t>(line.lineNumber) - 1, &source) & 0xff) { // the original tests al only
                        reinterpret_cast<cRZIniStringHelpers *>(&source)->TrimBoth();
                        // ORIGINAL-BUG: prints the number one too high here (the other path prints it as it is).
                        text.Sprintf(GameAddress<const char>(kTextSourceLine), line.fileName, line.lineNumber + 1);
                        WriteText(self, text);
                        text.mString.assign(GameAddress<const char>(kTextColon), 2);
                        text.mString.append(source.mString);
                        WriteText(self, text);
                    } else {
                        text.Sprintf(GameAddress<const char>(kTextSourceLine), line.fileName, line.lineNumber);
                        WriteText(self, text);
                        text.mString.assign(GameAddress<const char>(kTextUnknownCode), kUnknownCodeLength);
                        WriteText(self, text);
                    }
                    text.mString.clear();
                    WriteText(self, text);
                    source.Destruct();
                }
            }
            module.Destruct();
        } else {
            // No symbol (or no lookup): the module and section only.
            if (frameIndex == 0 && table->symGetSymFromAddr && table->versionAtLeast51) {
                text.Sprintf(GameAddress<const char>(kTextPdbAbsent));
                WriteText(self, text);
            }
            cRZString module;
            module.Construct();
            uint32_t section = 0, offset = 0;
            FindModuleOfAddress(reinterpret_cast<const void *>(static_cast<uintptr_t>(frame.pc.offset)), module.AsInterface(), &section, &offset);
            text.Sprintf(GameAddress<const char>(kTextModuleOnly), section, offset, module.mString.c_str());
            WriteText(self, text);
            module.Destruct();
        }
    }
    text.Destruct();
}


// ---- cRZVersion ----

namespace {

constexpr uint32_t kIatVerSetConditionMask = 0xd9a268, kIatGlobalMemoryStatusEx = 0xd9a26c, kIatVerifyVersionInfoW = 0xd9a274;
constexpr uint32_t kIatWsprintfA = 0xd9a5bc, kIatGetFileVersionInfoA = 0xd9a5e8, kIatVerQueryValueA = 0xd9a5ec, kIatGetFileVersionInfoSizeA = 0xd9a5f0;
constexpr uint32_t kMemoryOverrideTotal = 0x10cb6a8, kMemoryOverrideAvailable = 0x10cb6ac; // game globals: when set, used instead of the OS

// Texts in the image.
constexpr uint32_t kTextTranslationBlock = 0xda4584; // "\\VarFileInfo\\Translation"
constexpr uint32_t kTextStringBlock = 0xda45b0;      // "\\StringFileInfo\\%04lx%04lx\\%s"
constexpr uint32_t kTextFileDescription = 0xda45a0, kTextFileVersion = 0xda45d0, kTextDebugVersion = 0xda45dc;
constexpr uint32_t kTextWindowsNt = 0xda45ec;        // "Windows NT %u.%u"
constexpr uint32_t kTextCrLf = 0xda31d8;
constexpr int kSlotSprintf = 31;

constexpr uint32_t kHeadingPath = 0xda38c8, kHeadingPathLength = 0x19;
constexpr uint32_t kHeadingDescription = 0xda38e4, kHeadingDescriptionLength = 0x21;
constexpr uint32_t kHeadingVersion = 0xda3908, kHeadingVersionLength = 0x1c;
constexpr uint32_t kHeadingDebug = 0xda3928, kHeadingDebugLength = 0x22;
constexpr uint32_t kHeadingSystem = 0xda394c, kHeadingSystemLength = 0x10;
constexpr uint32_t kHeadingMemory = 0xda3960, kHeadingMemoryLength = 0xf, kTextMemory = 0xda3970;
constexpr uint32_t kHeadingConfiguration = 0xda3998, kHeadingConfigurationLength = 0x22, kTextConfiguration = 0xda39bc, kTextConfigurationLength = 0xa;

} // namespace

cRZVersion *cRZVersion::ConstructFromPath(const cRZString *path)
{
    mVTable = GameAddress<const void>(kVTable);
    mPath.Construct(*path, 0, static_cast<uint32_t>(-1));
    mUnused24 = 0;
    mData = nullptr;
    mTranslation = nullptr;
    return this;
}

cRZVersion *cRZVersion::Construct()
{
    mVTable = GameAddress<const void>(kVTable);
    mPath.Construct();
    mUnused24 = 0;
    mData = nullptr;
    mTranslation = nullptr;
    GetModulePath(mPath.AsInterface());
    return this;
}

void cRZVersion::Destruct()
{
    mVTable = GameAddress<const void>(kVTable);
    if (mData) {
        delete[] mData;
        mData = nullptr;
    }
    mPath.Destruct();
}

cRZVersion *cRZVersion::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) operator delete(this, kSize);
    return this;
}

void cRZVersion::Load()
{
    if (mData) return;
    cRZString narrow;
    narrow.Construct();
    RZText::ConvertStringToString(*mPath.AsInterface(), *narrow.AsInterface(), RZText::kSystemCodePage);
    DWORD handle;
    const DWORD size = Import<DWORD(WINAPI *)(const char *, DWORD *)>(kIatGetFileVersionInfoSizeA)(narrow.mString.c_str(), &handle);
    if (size) {
        mData = new char[size];
        if (Import<BOOL(WINAPI *)(const char *, DWORD, DWORD, void *)>(kIatGetFileVersionInfoA)(narrow.mString.c_str(), handle, size, mData)) {
            UINT length;
            if (!Import<BOOL(WINAPI *)(const void *, const char *, void **, UINT *)>(kIatVerQueryValueA)(
                    mData, GameAddress<const char>(kTextTranslationBlock), reinterpret_cast<void **>(&mTranslation), &length)) {
                delete[] mData;
                mData = nullptr;
            }
        }
    }
    narrow.Destruct();
}

uint8_t cRZVersion::GetPath(cRZString *out) const
{
    reinterpret_cast<RZStringSlot *>(&out->mString)->Assign(mPath.mString);
    return 1;
}

namespace {

// The three string queries are the same call with a different value name.
uint8_t QueryValue(cRZVersion *self, cRZString *out, uint32_t nameRva)
{
    if (!self->mData) {
        self->Load();
        if (!self->mData) return 0;
    }
    char block[0x100];
    Import<int(__cdecl *)(char *, const char *, ...)>(kIatWsprintfA)(block, GameAddress<const char>(kTextStringBlock), self->mTranslation[0],
                                                                     self->mTranslation[1], GameAddress<const char>(nameRva));
    void *value = nullptr; // only read when the query succeeds
    UINT length;
    const bool found = Import<BOOL(WINAPI *)(const void *, const char *, void **, UINT *)>(kIatVerQueryValueA)(self->mData, block, &value, &length) != 0;
    if (found)
        out->mString.assign(static_cast<const char *>(value));
    else
        out->mString.erase(0);
    return found;
}

} // namespace

uint8_t cRZVersion::GetDescription(cRZString *out)
{
    return QueryValue(this, out, kTextFileDescription);
}

uint8_t cRZVersion::GetFileVersion(cRZString *out)
{
    return QueryValue(this, out, kTextFileVersion);
}

uint8_t cRZVersion::GetDebugVersion(cRZString *out)
{
    return QueryValue(this, out, kTextDebugVersion);
}

uint8_t __cdecl VerifyWindowsVersion(uint32_t major, uint32_t minor, uint32_t servicePackMajor)
{
    OSVERSIONINFOEXW info;
    std::memset(&info, 0, sizeof(info)); // the original sets every field one by one
    info.dwOSVersionInfoSize = sizeof(info);
    info.dwMajorVersion = major & 0xffff; // the original reads each argument as a 16-bit word
    info.dwMinorVersion = minor & 0xffff;
    info.wServicePackMajor = static_cast<WORD>(servicePackMajor);
    auto setCondition = Import<ULONGLONG(WINAPI *)(ULONGLONG, DWORD, BYTE)>(kIatVerSetConditionMask);
    ULONGLONG mask = setCondition(0, VER_MAJORVERSION, VER_GREATER_EQUAL);
    mask = setCondition(mask, VER_MINORVERSION, VER_GREATER_EQUAL);
    mask = setCondition(mask, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
    return Import<BOOL(WINAPI *)(OSVERSIONINFOEXW *, DWORD, ULONGLONG)>(kIatVerifyVersionInfoW)(&info, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR,
                                                                                                   mask) != 0;
}

uint8_t cRZVersion::GetSystemVersion(cIGZString *out) const
{
    const uint8_t atLeast10 = VerifyWindowsVersion(10, 0, 0);
    // ORIGINAL-BUG: formats the test's result where the major version belongs.
    VirtualSlot<void(__cdecl *)(cIGZString *, const char *, ...)>(out, kSlotSprintf)(out, GameAddress<const char>(kTextWindowsNt), static_cast<uint32_t>(atLeast10), 0);
    return 1;
}

uint8_t cRZVersion::GetMemory(uint32_t *totalMegabytes, uint32_t *availableMegabytes) const
{
    if (const uint32_t total = *GameAddress<uint32_t>(kMemoryOverrideTotal)) {
        *totalMegabytes = total;
        *availableMegabytes = *GameAddress<uint32_t>(kMemoryOverrideAvailable);
        return 1;
    }
    MEMORYSTATUSEX status;
    std::memset(&status, 0, sizeof(status)); // the original leaves it unset when the call fails; a working call fills it all
    // ORIGINAL-BUG: the length is 0x20, the size of the old MEMORYSTATUS; GlobalMemoryStatusEx wants
    // sizeof(MEMORYSTATUSEX) (0x40) and fails, leaving the fields as they were (stack garbage in the
    // original), so the reported memory is meaningless unless the game global is set.
    status.dwLength = 0x20;
    Import<BOOL(WINAPI *)(MEMORYSTATUSEX *)>(kIatGlobalMemoryStatusEx)(&status);
    *totalMegabytes = static_cast<uint32_t>(status.ullTotalPhys >> 20);
    *availableMegabytes = static_cast<uint32_t>(status.ullAvailPhys >> 20);
    // Rounded up to a multiple of 4 (0 stays 0).
    *totalMegabytes = ((*totalMegabytes - 1) | 3) + 1;
    *availableMegabytes = ((*availableMegabytes - 1) | 3) + 1;
    return 1;
}

// ---- 0x1bb94c ----

void __stdcall WriteSystemInfo(cRZString *out)
{
    cRZVersion version;
    version.Construct();
    cRZString text;
    text.Construct();
    std::string &s = out->mString;
    const char *const crlf = GameAddress<const char>(kTextCrLf);
    auto addLine = [&](uint32_t heading, uint32_t headingLength) {
        s.append(GameAddress<const char>(heading), headingLength);
        s.append(text.mString);
        s.append(crlf, 2);
    };

    if (version.GetPath(&text)) addLine(kHeadingPath, kHeadingPathLength);
    if (version.GetDescription(&text)) addLine(kHeadingDescription, kHeadingDescriptionLength);
    if (version.GetFileVersion(&text)) addLine(kHeadingVersion, kHeadingVersionLength);
    if (version.GetDebugVersion(&text) && !text.mString.empty()) addLine(kHeadingDebug, kHeadingDebugLength);
    if (version.GetSystemVersion(text.AsInterface()) && !text.mString.empty()) addLine(kHeadingSystem, kHeadingSystemLength);
    uint32_t total, available;
    if (version.GetMemory(&total, &available)) {
        s.append(GameAddress<const char>(kHeadingMemory), kHeadingMemoryLength);
        text.Sprintf(GameAddress<const char>(kTextMemory), total, available);
        s.append(text.mString);
        s.append(crlf, 2);
    }
    s.append(GameAddress<const char>(kHeadingConfiguration), kHeadingConfigurationLength);
    s.append(GameAddress<const char>(kTextConfiguration), kTextConfigurationLength);
    s.append(crlf, 2);
    text.Destruct();
    version.Destruct();
}

} // namespace appbase
