#include "app/ReportWrite.h"

#include "app/AppBase.h"
#include "app/ReportBlockers.h"
#include "app/ReportFinish.h"
#include "app/ReportText.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZString.h"

#include <tlhelp32.h>

#include <cstdlib>
#include <cstring>
#include <intrin.h>

namespace appbase {
namespace {

constexpr uint32_t kIatSleep = 0xd9a144, kIatCloseHandle = 0xd9a17c, kIatGetCurrentThreadId = 0xd9a194, kIatGetLocalTime = 0xd9a1c8;
constexpr uint32_t kIatSetUnhandledExceptionFilter = 0xd9a1b4, kIatIsBadReadPtr = 0xd9a1d0, kIatIsBadWritePtr = 0xd9a1d4;

// Texts in the image.
constexpr uint32_t kTextTime = 0xda3464;           // "Exception time: %02d/%02d/%d, at %02d:%02d:%02d."
constexpr uint32_t kTextCode = 0xda3498;           // "Exception code: 0x%08X (%d) %s."
constexpr uint32_t kTextThread = 0xda34b8;         // "Current thread ID: %u (0x%08x)."
constexpr uint32_t kTextVersionInfo = 0xda34d8;    // "Version information:"
constexpr uint32_t kTextString = 0xda34f0;         // "%s"
constexpr uint32_t kTextModule = 0xda34f4;         // "Exception module:  %s."
constexpr uint32_t kTextAddress = 0xda350c;        // "Exception address: 0x%08x. Section:Offset: 0x%02x:0x%08x."
constexpr uint32_t kTextRegisters = 0xda3548;      // "\r\nRegisters"
constexpr uint32_t kTextGeneral = 0xda3558;        // "   EAX: %08x\r\n   EBX: ..."
constexpr uint32_t kTextCsEip = 0xda35ac;          // "   CS:EIP: %04x:%08x"
constexpr uint32_t kTextSsEsp = 0xda35c4;          // "   SS:ESP: %04x:%08x  EBP:%08x"
constexpr uint32_t kTextSegments = 0xda35e4;       // "   DS:%04x  ES:%04x  FS:%04x  GS:%04x"
constexpr uint32_t kTextFlags = 0xda360c;          // "   Flags: %08x"
constexpr uint32_t kTextDbgHelpFailed = 0xda361c;  // "DbgHelp.dll load failed. Attempting manual stack walk..."
constexpr uint32_t kTextLoadedModules = 0xda3658;  // "\r\nLoaded Modules"
constexpr uint32_t kTextModuleHeader = 0xda366c;   // "   Address          Size Module             Path"
constexpr uint32_t kTextModuleRule = 0xda36a0;     // "   ------...------"
constexpr uint32_t kTextModuleLine = 0xda3704;     // "   0x%08x %10u %-18.18s %s"
constexpr uint32_t kTextManualHeading = 0xda3aa4;  // "Call stack:"
constexpr uint32_t kTextManualColumns = 0xda3ab0;  // "Address     Frame     Logical addr  Module."
constexpr uint32_t kTextManualFrame = 0xda3adc;    // "0x%08x  0x%08x  0x%04x:0x%08x %s."
constexpr uint32_t kTextLineAddress = 0xda3b00;    // "0x%08x   "
constexpr uint32_t kTextByte = 0xda3b0c;           // "%02x"
constexpr uint32_t kTextStackData = 0xda3b14;      // "\r\nStack data: (ESP is 0x%08x)"
constexpr uint32_t kTextStackInvalid = 0xda3b34;   // "   Stack data is not readable -- invalid range."
constexpr uint32_t kTextStackFault = 0xda3b64;     // "   Stack data is not readable -- exception during read."
constexpr uint32_t kTextInstructionData = 0xda3b9c; // "\r\nInstruction data: (EIP is 0x%08x)"
constexpr uint32_t kTextInstructionBad = 0xda3bc0;  // the "not readable" note

// Slots of the report file (cRZTextFile).
constexpr int kSlotOpen = 3, kSlotClose = 5, kSlotLength = 7, kSlotFlush = 18, kSlotSetPath = 29;

constexpr uint32_t kSymbolOptions = 0x14;
constexpr uint32_t kMaxReportLength = 0x30d40;
constexpr uint32_t kSymbolsLoadedFlag = 0x10caffd; // byte: LoadSymbolModulesOnce has run

struct PsapiModuleInfo { // MODULEINFO
    void *baseOfDll;
    DWORD sizeOfImage;
    void *entryPoint;
};

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

DbgHelpTable *Table()
{
    return GameAddress<DbgHelpTable>(DbgHelpTable::kGlobal);
}

void WriteText(ExceptionReporterObject *self, const cRZString &text)
{
    WriteLine(&self->mLog, nullptr, text.mString.c_str(), static_cast<uint32_t>(text.mString.size()));
}

} // namespace

// ---- ModuleInfo: 0x1ba8df, 0x1baabe ----

ModuleInfo *ModuleInfo::CopyConstruct(const ModuleInfo *other)
{
    path.Construct(other->path, 0, static_cast<uint32_t>(-1));
    name.Construct(other->name, 0, static_cast<uint32_t>(-1));
    base = other->base;
    size = other->size;
    return this;
}

void ModuleInfo::Destruct()
{
    name.Destruct();
    path.Destruct();
}

// ---- 0x1bb2b6 ----

uint8_t __cdecl EnumerateModulesToolhelp(std::vector<ModuleInfo> *modules, uint32_t processId)
{
    // (The original also constructs and destroys an unused string here.)
    modules->clear();
    DbgHelpTable *const table = Table();
    if (!table->Load()) return 0;
    if (table->createToolhelp32Snapshot && table->module32First && table->module32Next) {
        static_assert(sizeof(MODULEENTRY32) == 0x224, "MODULEENTRY32 size");
        MODULEENTRY32 entry;
        std::memset(reinterpret_cast<uint8_t *>(&entry) + 4, 0, 0x220);
        entry.dwSize = 0x224;
        ModuleInfo info;
        void *const snapshot = reinterpret_cast<void *(WINAPI *)(DWORD, DWORD)>(table->createToolhelp32Snapshot)(TH32CS_SNAPMODULE, processId);
        if (snapshot == INVALID_HANDLE_VALUE) return 0; // the original returns its caller's ebx here
        BOOL more = reinterpret_cast<BOOL(WINAPI *)(void *, MODULEENTRY32 *)>(table->module32First)(snapshot, &entry);
        while (more) {
            info.path.mString.assign(entry.szExePath, std::strlen(entry.szExePath));
            info.name.mString.assign(entry.szModule, std::strlen(entry.szModule));
            info.base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry.modBaseAddr));
            info.size = entry.modBaseSize;
            modules->push_back(info);
            more = reinterpret_cast<BOOL(WINAPI *)(void *, MODULEENTRY32 *)>(table->module32Next)(snapshot, &entry);
        }
        Import<BOOL(WINAPI *)(void *)>(kIatCloseHandle)(snapshot);
    }
    return !modules->empty();
}

// ---- 0x1bb108 ----

uint8_t __cdecl EnumerateModules(std::vector<ModuleInfo> *modules, uint32_t processId, void *process)
{
    if (EnumerateModulesToolhelp(modules, processId)) return 1;

    // The PSAPI route (0x1bb126).
    modules->clear();
    DbgHelpTable *const table = Table();
    if (!table->Load()) return 0;
    if (!table->enumProcessModules || !table->getModuleFileNameExA || !table->getModuleBaseNameA || !table->getModuleInformation) return 0;

    ModuleInfo info;
    constexpr uint32_t kBufferSize = 0x10000;
    void **const handles = static_cast<void **>(std::malloc(kBufferSize));
    char *const buffer = static_cast<char *>(std::malloc(kBufferSize));
    DWORD needed = 0;
    // The module's information keeps its stack slot between modules: a failing GetModuleInformation
    // leaves the previous module's numbers in it (for the first module, whatever the stack held).
    PsapiModuleInfo moduleInfo;
    std::memset(&moduleInfo, 0, sizeof(moduleInfo)); // the first module's garbage is not reproduced
    if (reinterpret_cast<BOOL(WINAPI *)(void *, void **, DWORD, DWORD *)>(table->enumProcessModules)(process, handles, kBufferSize, &needed) &&
        needed <= kBufferSize && (needed & 0xfffffffc) != 0) {
        for (uint32_t i = 0; i < needed / 4; ++i) {
            reinterpret_cast<BOOL(WINAPI *)(void *, void *, PsapiModuleInfo *, DWORD)>(table->getModuleInformation)(process, handles[i], &moduleInfo, 0xc);
            info.base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(moduleInfo.baseOfDll));
            info.size = moduleInfo.sizeOfImage;
            buffer[0] = 0;
            reinterpret_cast<DWORD(WINAPI *)(void *, void *, char *, DWORD)>(table->getModuleFileNameExA)(process, handles[i], buffer, kBufferSize);
            info.path.mString.assign(buffer, std::strlen(buffer));
            buffer[0] = 0;
            reinterpret_cast<DWORD(WINAPI *)(void *, void *, char *, DWORD)>(table->getModuleBaseNameA)(process, handles[i], buffer, kBufferSize);
            info.name.mString.assign(buffer, std::strlen(buffer));
            modules->push_back(info);
        }
    }
    std::free(buffer);
    std::free(handles);
    return !modules->empty();
}

// ---- 0x1bb026 ----

void __cdecl LoadSymbolModules(void *process, uint32_t processId, std::vector<ModuleInfo> *modules)
{
    DbgHelpTable *const table = Table();
    if (!table->symLoadModule) return;
    // (The original also constructs and destroys an unused string here.)
    EnumerateModules(modules, processId, process);
    for (size_t i = 0; i < modules->size(); ++i) {
        const ModuleInfo &module = (*modules)[i];
        cRZString image, shortName;
        image.Construct(module.path, 0, static_cast<uint32_t>(-1));
        image.mString.push_back('\0');
        shortName.Construct(module.name, 0, static_cast<uint32_t>(-1));
        shortName.mString.push_back('\0');
        reinterpret_cast<uint32_t(WINAPI *)(void *, void *, const char *, const char *, uint32_t, uint32_t)>(table->symLoadModule)(
            process, nullptr, image.mString.c_str(), shortName.mString.c_str(), module.base, module.size);
        shortName.Destruct();
        image.Destruct();
    }
}

// ---- 0x1bcf4a ----

uint8_t __cdecl LoadSymbolModulesOnce()
{
    uint8_t *const loaded = GameAddress<uint8_t>(kSymbolsLoadedFlag);
    if (*loaded) return 1;
    DbgHelpTable *const table = Table();
    if (!table->Load()) return 0;
    reinterpret_cast<uint32_t(WINAPI *)(uint32_t)>(table->symSetOptions)(kSymbolOptions);
    std::vector<ModuleInfo> modules;
    LoadSymbolModules(table->process, table->processId, &modules);
    *loaded = 1;
    return 1;
}

// ---- 0x1bcdd4 ----

uint8_t __fastcall WriteModules(ExceptionReporterObject *self, void *)
{
    DbgHelpTable *const table = Table();
    if (!table->Load()) return 0;
    reinterpret_cast<uint32_t(WINAPI *)(uint32_t)>(table->symSetOptions)(kSymbolOptions);
    std::vector<ModuleInfo> modules;
    LoadSymbolModules(table->process, table->processId, &modules);
    if (self->mLevels[3]) {
        cRZString text;
        text.Construct();
        text.Sprintf(GameAddress<const char>(kTextLoadedModules));
        WriteText(self, text);
        text.Sprintf(GameAddress<const char>(kTextModuleHeader));
        WriteText(self, text);
        text.Sprintf(GameAddress<const char>(kTextModuleRule));
        WriteText(self, text);
        for (const ModuleInfo &module : modules) {
            text.Sprintf(GameAddress<const char>(kTextModuleLine), module.base, module.size, module.name.mString.c_str(), module.path.mString.c_str());
            WriteText(self, text);
        }
        text.Destruct();
    }
    return 1;
}

// ---- 0x1bd744 ----

void __fastcall WriteHexDump(ExceptionReporterObject *self, void *, uint32_t address, const uint8_t *data, uint32_t count, int32_t groupsPerLine,
                             int32_t bytesPerGroup, int32_t align)
{
    while (static_cast<int32_t>(address) % align != 0 && count != 0) {
        ++address;
        ++data;
        --count;
    }
    cRZString line, piece;
    line.Construct();
    piece.Construct();
    line.Sprintf(GameAddress<const char>(kTextLineAddress), address);
    int32_t group = 0;
    uint32_t index = 0;
    if (count != 0) {
        do {
            int32_t inGroup = 0;
            while (inGroup < bytesPerGroup && index < count) {
                piece.Sprintf(GameAddress<const char>(kTextByte), static_cast<uint32_t>(data[index]));
                line.mString.append(piece.mString);
                ++inGroup;
                ++index;
            }
            line.mString.push_back(' ');
            ++group;
            if (group >= groupsPerLine) {
                WriteText(self, line);
                address += static_cast<uint32_t>(groupsPerLine * bytesPerGroup);
                line.Sprintf(GameAddress<const char>(kTextLineAddress), address);
                group = 0;
            }
            // ORIGINAL-BUG: a last line that is not full (fewer than groupsPerLine groups) is dropped here.
        } while (index < count);
    }
    piece.Destruct();
    line.Destruct();
}

// ---- 0x1bcc6c ----

void __fastcall WriteManualCallStack(ExceptionReporterObject *self, void *, const CONTEXT *context)
{
    CONTEXT copy;
    static_assert(sizeof(CONTEXT) == 0x2cc, "x86 CONTEXT");
    std::memcpy(&copy, context, sizeof(copy));
    cRZString text;
    text.Construct();
    text.Sprintf(GameAddress<const char>(kTextManualHeading));
    WriteText(self, text);
    text.Sprintf(GameAddress<const char>(kTextManualColumns));
    WriteText(self, text);
    uint32_t ebp = copy.Ebp;
    uint32_t eip = copy.Eip;
    for (;;) {
        cRZString module;
        module.Construct();
        uint32_t section = 0, offset = 0;
        FindModuleOfAddress(reinterpret_cast<const void *>(static_cast<uintptr_t>(eip)), module.AsInterface(), &section, &offset);
        text.Sprintf(GameAddress<const char>(kTextManualFrame), eip, ebp, section, offset, module.mString.c_str());
        WriteText(self, text);
        // ORIGINAL-BUG: the first frame pointer (the context's) is read without a check.
        const uint32_t *const frame = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(ebp));
        const uint32_t returnAddress = frame[1];
        const uint32_t current = ebp;
        ebp = frame[0];
        const bool stop = (ebp & 3) != 0 || ebp <= current ||
                          Import<BOOL(WINAPI *)(void *, UINT_PTR)>(kIatIsBadWritePtr)(reinterpret_cast<void *>(static_cast<uintptr_t>(ebp)), 8) != 0;
        module.Destruct();
        if (stop) break;
        eip = returnAddress;
    }
    text.Destruct();
}

// ---- 0x1bd064 ----

namespace {

// One byte appended to the vector, the read of it guarded: false when it faults. Kept free of objects
// with destructors so that it can hold the __try.
bool TryAppendByte(std::vector<char> *bytes, const char *at)
{
    __try {
        bytes->push_back(*at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void __fastcall WriteInstructionData(ExceptionReporterObject *self, void *, const CONTEXT *context)
{
    cRZString text;
    text.Construct();
    std::vector<char> bytes;
    const uint32_t eip = context->Eip;
    uint32_t shownStart = eip - 0x80;
    const uint32_t end = eip + 0x80;
    if (Import<BOOL(WINAPI *)(const void *, UINT_PTR)>(kIatIsBadReadPtr)(reinterpret_cast<const void *>(static_cast<uintptr_t>(eip)), 1) == 0) {
        text.Sprintf(GameAddress<const char>(kTextInstructionData), eip);
        WriteText(self, text);
        bool any = false;
        for (uint32_t at = eip - 0x80; at < end; ++at) {
            if (TryAppendByte(&bytes, reinterpret_cast<const char *>(static_cast<uintptr_t>(at)))) any = true;
            else if (!any) ++shownStart; // leading unreadable bytes are not shown
        }
        WriteHexDump(self, nullptr, shownStart, reinterpret_cast<const uint8_t *>(bytes.data()), static_cast<uint32_t>(bytes.size()), 0x20, 1, 1);
    } else {
        text.Sprintf(GameAddress<const char>(kTextInstructionBad));
        WriteText(self, text);
    }
    text.Destruct();
}

// ---- 0x1bd1de ----

namespace {

// The part of the stack dump inside the guard (no objects with destructors in this function).
void StackDataBody(ExceptionReporterObject *self, cRZString *text, std::vector<char> *bytes, uint32_t esp)
{
    __try {
        const uint32_t base = __readfsdword(4);  // the stack base of the thread information block
        const uint32_t limit = __readfsdword(8); // the stack limit
        if (esp >= limit && esp < base) {
            const uint32_t low = esp - 0x180;
            const uint32_t start = limit >= low ? limit : low;
            const uint32_t high = esp + 0x180;
            const uint32_t end = high >= base ? base : high;
            for (uint32_t at = start; at < end; ++at) bytes->push_back(*reinterpret_cast<const char *>(static_cast<uintptr_t>(at)));
            WriteHexDump(self, nullptr, start, reinterpret_cast<const uint8_t *>(bytes->data()), static_cast<uint32_t>(bytes->size()), 8, 4, 4);
        } else {
            text->Sprintf(GameAddress<const char>(kTextStackInvalid));
            WriteText(self, *text);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        text->Sprintf(GameAddress<const char>(kTextStackFault));
        WriteText(self, *text);
    }
}

} // namespace

void __fastcall WriteStackData(ExceptionReporterObject *self, void *, const CONTEXT *context)
{
    std::vector<char> bytes;
    cRZString text;
    text.Construct();
    const uint32_t esp = context->Esp;
    text.Sprintf(GameAddress<const char>(kTextStackData), esp);
    WriteText(self, text);
    StackDataBody(self, &text, &bytes, esp);
    text.Destruct();
}

// ---- 0x1c9211 ----

void __fastcall WaitStopwatch(const cRZStopwatch *stopwatch, void *, uint32_t units)
{
    cRZStopwatch watch;
    watch.Construct(stopwatch->mMode);
    watch.Start();
    while (watch.Elapsed() < units) Import<void(WINAPI *)(DWORD)>(kIatSleep)(0);
}

// ---- 0x1bb410 ----

void __fastcall WriteReport(ExceptionReporterObject *self, void *, EXCEPTION_POINTERS *info)
{
    const CONTEXT *const context = info->ContextRecord;
    const EXCEPTION_RECORD *const record = info->ExceptionRecord;
    cRZTextFile *const log = &self->mLog;
    VirtualCall<void(__thiscall *)(void *, const void *), kSlotSetPath>(log, &self->mReportFile);
    if (!VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), kSlotOpen>(log, 3, 3, 1)) return;

    cRZString text;
    text.Construct();
    if (VirtualCall<uint32_t(__thiscall *)(void *), kSlotLength>(log) > kMaxReportLength) {
        VirtualCall<void(__thiscall *)(void *), kSlotFlush>(log);
        VirtualCall<void(__thiscall *)(void *), kSlotClose>(log);
        RemoveFilterAndTerminate();
    }

    if (self->mLevels[0]) {
        SYSTEMTIME time;
        Import<void(WINAPI *)(SYSTEMTIME *)>(kIatGetLocalTime)(&time);
        text.Sprintf(GameAddress<const char>(kTextTime), static_cast<uint32_t>(time.wMonth), static_cast<uint32_t>(time.wDay), static_cast<uint32_t>(time.wYear),
                     static_cast<uint32_t>(time.wHour), static_cast<uint32_t>(time.wMinute), static_cast<uint32_t>(time.wSecond));
        WriteText(self, text);
        if (self->mLevels[0]) {
            cRZString description;
            description.Construct();
            DescribeExceptionCode(record->ExceptionCode, &description);
            text.Sprintf(GameAddress<const char>(kTextCode), record->ExceptionCode, record->ExceptionCode, description.mString.c_str());
            WriteText(self, text);
            description.Destruct();
            if (self->mLevels[0]) {
                const uint32_t thread = Import<DWORD(WINAPI *)()>(kIatGetCurrentThreadId)();
                text.Sprintf(GameAddress<const char>(kTextThread), thread, thread);
                WriteText(self, text);
                if (self->mLevels[0]) {
                    cRZString system;
                    system.Construct();
                    WriteSystemInfo(&system);
                    text.Sprintf(GameAddress<const char>(kTextVersionInfo));
                    WriteText(self, text);
                    text.Sprintf(GameAddress<const char>(kTextString), system.mString.c_str());
                    WriteText(self, text);
                    system.Destruct();
                }
            }
        }
    }

    if (self->mLevels[5]) {
        VirtualCall<void(__thiscall *)(void *), kSlotFlush>(log);
        self->mExtraStarted = 0;
        self->Broadcast(0xb627ad4);
        self->WriteExtraDataEnd();
    }

    {
        cRZString module;
        module.Construct();
        uint32_t section, offset;
        FindModuleOfAddress(record->ExceptionAddress, module.AsInterface(), &section, &offset);
        if (self->mLevels[0]) {
            text.Sprintf(GameAddress<const char>(kTextModule), module.mString.c_str());
            WriteText(self, text);
            text.Sprintf(GameAddress<const char>(kTextAddress), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(record->ExceptionAddress)), section, offset);
            WriteText(self, text);
        }
        module.Destruct();
    }

    if (self->mLevels[2]) {
        text.Sprintf(GameAddress<const char>(kTextRegisters));
        WriteText(self, text);
        text.Sprintf(GameAddress<const char>(kTextGeneral), context->Eax, context->Ebx, context->Ecx, context->Edx, context->Esi, context->Edi);
        WriteText(self, text);
        text.Sprintf(GameAddress<const char>(kTextCsEip), context->SegCs, context->Eip);
        WriteText(self, text);
        text.Sprintf(GameAddress<const char>(kTextSsEsp), context->SegSs, context->Esp, context->Ebp);
        WriteText(self, text);
        text.Sprintf(GameAddress<const char>(kTextSegments), context->SegDs, context->SegEs, context->SegFs, context->SegGs);
        WriteText(self, text);
        text.Sprintf(GameAddress<const char>(kTextFlags), context->EFlags);
        WriteText(self, text);
    }

    if (self->mLevels[1]) {
        if (Table()->Load() && WriteModules(self, nullptr)) {
            WriteCallStack(self, nullptr, context);
        } else {
            text.Sprintf(GameAddress<const char>(kTextDbgHelpFailed));
            WriteText(self, text);
            WriteManualCallStack(self, nullptr, context);
        }
        if (self->mLevels[1]) {
            WriteStackData(self, nullptr, context);
            if (self->mLevels[1]) WriteInstructionData(self, nullptr, context);
        }
    }

    if (self->mLevels[5]) {
        VirtualCall<void(__thiscall *)(void *), kSlotFlush>(log);
        self->mExtraStarted = 0;
        self->Broadcast(0xb627ad5);
        self->WriteExtraDataEnd();
    }
    VirtualCall<void(__thiscall *)(void *), kSlotFlush>(log);
    VirtualCall<void(__thiscall *)(void *), kSlotClose>(log);
    text.Destruct();
}

// ---- 0x1bd69a ----

uint32_t __fastcall Report(ExceptionReporterObject *self, void *, EXCEPTION_POINTERS *info)
{
    using SetFilter = void *(WINAPI *)(void *);
    const SetFilter setFilter = Import<SetFilter>(kIatSetUnhandledExceptionFilter);
    setFilter(reinterpret_cast<void *>(static_cast<uintptr_t>(self->mPreviousFilter)));
    NameFiles(self, nullptr);
    self->Broadcast(0xb627ad2);
    WriteMiniDump(self, nullptr, info);
    WriteReport(self, nullptr, info);
    self->Broadcast(0xb627ad3);
    setFilter(GameAddress<void>(cExceptionReporter::kFilter));
    cRZStopwatch stopwatch;
    stopwatch.Construct(4);
    WaitStopwatch(&stopwatch, nullptr, 1000);
    uint32_t result;
    if (self->mFlag19a) {
        result = 1;
    } else if (DebuggerPresentCached()) {
        const uint32_t previous = self->mPreviousFilter;
        if (previous) result = reinterpret_cast<uint32_t(WINAPI *)(EXCEPTION_POINTERS *)>(static_cast<uintptr_t>(previous))(info);
        else result = 0;
    } else {
        RemoveFilterAndTerminate();
        result = 0;
    }
    return result;
}

} // namespace appbase
