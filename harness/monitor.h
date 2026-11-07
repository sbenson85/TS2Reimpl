#pragma once
// Crash diagnostics for difftest.exe (included by main.cpp only).
//
// A test run is two processes: the exe started by the user (the monitor) starts itself again with the same command line
// plus `--monitor-state <handle>` (the runner, which does all the work) and waits for it. The runner keeps what it is
// doing in a shared memory block: the running test, its Context (so ctx.cases is readable after death), an optional
// mark a test sets with ctx.Mark(), the last exception its vectored handler saw, and notes from the CRT's fatal paths
// (fast fail, abort, terminate, purecall, invalid parameter) with a scan of the stack at that point. When the runner
// ends without finishing (a fast fail, a second stack overflow, a corrupted SEH chain, heap corruption, exit() or
// TerminateProcess anywhere), the monitor prints that block, resolved: an RVA in the original image, or module+offset
// and symbol (from difftest.map) in ours. Nothing a test sees changes: the runner's handlers only record, then let the
// CRT's own path run (the exit code is the runner's).
//
// Why the runner's own SEH catch (RunGuarded) misses these: a fast fail (__fastfail: /GS cookie failures, abort(),
// std::terminate, purecall and invalid-parameter in our CRT all end in one) raises no exception and runs no handler in
// the process; a stack overflow is caught once, but the guard page is gone afterwards, so the next one killed the process
// (RunGuarded now calls _resetstkoflw); an exception whose SEH chain the code under test overwrote is never dispatched
// to a frame handler; heap corruption terminates from ntdll. Only a process outside sees all of them.
// DIFFTEST_NO_MONITOR=1 runs in one process as before (no report).
#include "difftest.h"
#include "image.h"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <new>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace monitor {

enum NoteKind : uint32_t { kFastFail = 1, kTerminate, kPurecall, kInvalidParameter, kAbort };
constexpr uint32_t kMaxNotes = 4, kMaxFrames = 8, kMaxChain = 12;

struct State {
    volatile LONG phase;  // 0: setting up, 1: running tests, 2: finished (totals printed)
    uint32_t started;     // tests started so far
    uint32_t testRva;
    char test[160];
    char file[200];
    char mark[96];        // ctx.Mark()
    char lastNote[64];    // the last drawfx::Note label (NoteBreadcrumb), and how many notes the test made
    uint32_t noteCount;
    uint32_t imageBase, imageSize;                  // the original image
    uint32_t codeBegin[2], codeEnd[2];              // executable ranges: [0] ours, [1] the original's
    uint32_t exceptions;                            // seen by the vectored handler during this test
    uint32_t firstCode, firstAddress, firstTarget;
    uint32_t lastCode, lastAddress, lastTarget;
    char lastImport[96];                            // for import traps
    uint32_t notes;
    uint32_t noteKind[kMaxNotes], noteAddress[kMaxNotes];
    uint32_t chains, frames;
    uint32_t chain[kMaxChain], frame[kMaxFrames];
    volatile LONG contextLive;
    alignas(8) unsigned char context[sizeof(difftest::Context)];
};

inline State *gState;

inline bool InCode(const State &s, uintptr_t v)
{
    return (v >= s.codeBegin[0] && v < s.codeEnd[0]) || (v >= s.codeBegin[1] && v < s.codeEnd[1]);
}

// Looks like a return address: in code, right after a call instruction.
inline bool LooksReturned(const State &s, uintptr_t v)
{
    if (!InCode(s, v) || !InCode(s, v - 7)) return false;
    const uint8_t *b = reinterpret_cast<const uint8_t *>(v);
    auto callRm = [](uint8_t op, uint8_t modrm) { return op == 0xff && (modrm & 0x38) == 0x10; };
    return b[-5] == 0xe8 || callRm(b[-2], b[-1]) || callRm(b[-3], b[-2]) || callRm(b[-6], b[-5]) || callRm(b[-7], b[-6]);
}

// Once per test: the frame-pointer chain above `from` (the caller the note names; it breaks at code without frame
// pointers, much of the original's, or at a smashed frame), then the first return-address-like words on the stack
// (innermost first; only the committed stack region esp is in), which catch what the chain misses but include stale ones.
__declspec(noinline) inline void ScanStack(State &s, uint32_t from)
{
    if (s.chains || s.frames) return;
    void *trace[48];
    const USHORT n = CaptureStackBackTrace(0, 48, trace, nullptr);
    USHORT i = 0;
    while (i < n && static_cast<uint32_t>(reinterpret_cast<uintptr_t>(trace[i])) != from) ++i;
    if (i == n) i = from ? 0 : 2; // no caller named (or not in the chain): all but ScanStack and the handler
    else ++i;
    for (; i < n && s.chains < kMaxChain; ++i) {
        const auto at = reinterpret_cast<uintptr_t>(trace[i]);
        if (InCode(s, at)) s.chain[s.chains++] = static_cast<uint32_t>(at);
    }
    const uintptr_t *p = static_cast<const uintptr_t *>(_AddressOfReturnAddress()) + 1;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return;
    const uintptr_t *end = reinterpret_cast<const uintptr_t *>(static_cast<const char *>(mbi.BaseAddress) + mbi.RegionSize);
    if (end - p > 65536) end = p + 65536;
    // Start past the chain's last frame when it is found (the words below it are mostly stale locals).
    if (s.chains)
        for (const uintptr_t *q = p; q < end; ++q)
            if (*q == s.chain[s.chains - 1]) {
                p = q + 1;
                break;
            }
    for (; p < end && s.frames < kMaxFrames; ++p)
        if (LooksReturned(s, *p) && *p != from) s.frame[s.frames++] = static_cast<uint32_t>(*p);
}

inline void AddNote(NoteKind kind, void *at)
{
    State *s = gState;
    if (!s || s->phase != 1) return;
    if (s->notes < kMaxNotes) {
        s->noteKind[s->notes] = kind;
        s->noteAddress[s->notes] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(at));
        ++s->notes;
    }
    ScanStack(*s, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(at)));
}

// Records every exception (first chance) during a test; registered last, so the tracer's and tests' own handlers run
// first, and never handles anything.
inline LONG CALLBACK Vectored(EXCEPTION_POINTERS *info)
{
    State *s = gState;
    const EXCEPTION_RECORD *r = info->ExceptionRecord;
    if (!s || s->phase != 1) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = r->ExceptionCode;
    if (code == EXCEPTION_SINGLE_STEP || code == 0x40010006 || code == 0x4001000a || code == 0x406d1388) // debug print, thread name
        return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t at = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r->ExceptionAddress));
    const uint32_t target = r->NumberParameters >= 2 ? static_cast<uint32_t>(r->ExceptionInformation[1]) : 0;
    if (s->exceptions++ == 0) {
        s->firstCode = code;
        s->firstAddress = at;
        s->firstTarget = target;
    }
    s->lastCode = code;
    s->lastAddress = at;
    s->lastTarget = target;
    if (code == kImportTrapCode) strncpy_s(s->lastImport, OriginalImage::LastTrappedImport().c_str(), _TRUNCATE);
    if (code == 0xc0000374) ScanStack(*s, 0); // heap corruption: ntdll terminates right after; who freed it
    return EXCEPTION_CONTINUE_SEARCH;
}

// Our CRT asks IsProcessorFeaturePresent(PF_FASTFAIL_AVAILABLE) right before every __fastfail (__report_gsfailure,
// abort, _invoke_watson): the import is patched in our IAT to note the caller, then answers as before.
inline BOOL(WINAPI *gIsProcessorFeaturePresent)(DWORD);
inline BOOL WINAPI NotingIsProcessorFeaturePresent(DWORD feature)
{
    if (feature == PF_FASTFAIL_AVAILABLE) AddNote(kFastFail, _ReturnAddress());
    return gIsProcessorFeaturePresent(feature);
}

inline void OnTerminate()
{
    AddNote(kTerminate, _ReturnAddress());
    std::abort(); // the default terminate handler's behaviour
}
inline void __cdecl OnPurecall()
{
    AddNote(kPurecall, _ReturnAddress()); // returning lets _purecall abort(), as with no handler
}
inline void __cdecl OnInvalidParameter(const wchar_t *expression, const wchar_t *function, const wchar_t *file, unsigned line, uintptr_t)
{
    AddNote(kInvalidParameter, _ReturnAddress());
    _invoke_watson(expression, function, file, line, 0); // what the CRT does with no handler
}
inline void __cdecl OnAbortSignal(int)
{
    AddNote(kAbort, _ReturnAddress()); // returning lets abort() go on (fast fail), as with no handler
}

inline void CodeRange(uintptr_t base, uint32_t &begin, uint32_t &end)
{
    begin = end = 0;
    auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            const uint32_t b = static_cast<uint32_t>(base + sec[i].VirtualAddress), e = b + sec[i].Misc.VirtualSize;
            if (!begin || b < begin) begin = b;
            if (e > end) end = e;
        }
}

inline void PatchOwnImport(const char *name, void *replacement, void **saved)
{
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + reinterpret_cast<const IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (auto *d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; ++d) {
        if (!d->OriginalFirstThunk) continue;
        auto *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + d->OriginalFirstThunk);
        auto *iat = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), name) != 0) continue;
            DWORD old;
            if (!VirtualProtect(&iat->u1.Function, 4, PAGE_READWRITE, &old)) return;
            *saved = reinterpret_cast<void *>(iat->u1.Function);
            iat->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(replacement));
            VirtualProtect(&iat->u1.Function, 4, old, &old);
        }
    }
}

// Runner side: attaches the shared block (or, unmonitored, a private one) and installs the recorders. Call once the
// image is mapped.
inline void InstallRunner(const std::wstring &handleText, uintptr_t imageBase, uint32_t imageSize)
{
    static State local;
    State *s = &local;
    if (!handleText.empty()) {
        HANDLE map = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::wcstoul(handleText.c_str(), nullptr, 16)));
        if (void *view = MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, sizeof(State))) s = static_cast<State *>(view);
    }
    s->imageBase = static_cast<uint32_t>(imageBase);
    s->imageSize = imageSize;
    CodeRange(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), s->codeBegin[0], s->codeEnd[0]);
    CodeRange(imageBase, s->codeBegin[1], s->codeEnd[1]);
    if (!s->codeBegin[1]) {
        s->codeBegin[1] = static_cast<uint32_t>(imageBase);
        s->codeEnd[1] = static_cast<uint32_t>(imageBase + imageSize);
    }
    gState = s;
    if (s == &local) return; // unmonitored: nothing would report what they record
    AddVectoredExceptionHandler(0, &Vectored);
    if (!gIsProcessorFeaturePresent)
        PatchOwnImport("IsProcessorFeaturePresent", reinterpret_cast<void *>(&NotingIsProcessorFeaturePresent),
                       reinterpret_cast<void **>(&gIsProcessorFeaturePresent));
    std::set_terminate(&OnTerminate);
    _set_purecall_handler(&OnPurecall);
    _set_invalid_parameter_handler(&OnInvalidParameter);
    std::signal(SIGABRT, &OnAbortSignal);
}

// The Context the harness runs a test with lives in the shared block, so the monitor can read ctx.cases.
inline difftest::Context &BeginTest(const difftest::Test &test, const OriginalImage &image)
{
    State &s = *gState;
    ++s.started;
    s.testRva = test.rva;
    strncpy_s(s.test, test.name, _TRUNCATE);
    strncpy_s(s.file, test.file ? test.file : "", _TRUNCATE);
    s.mark[0] = 0;
    s.lastNote[0] = 0;
    s.noteCount = 0;
    s.exceptions = s.notes = s.chains = s.frames = 0;
    s.lastImport[0] = 0;
    auto *ctx = new (s.context) difftest::Context{image};
    s.contextLive = 1;
    s.phase = 1;
    return *ctx;
}

inline void EndTest(difftest::Context &ctx)
{
    gState->phase = 0;
    gState->contextLive = 0;
    ctx.~Context();
}

inline void Finish()
{
    if (gState) gState->phase = 2;
}

// ---- monitor side ----

struct Symbol {
    uint32_t address;
    std::string name, object;
};

// Public and static symbols of our exe from its linker map (difftest.map next to it), by address.
inline const std::vector<Symbol> &MapSymbols()
{
    static std::vector<Symbol> symbols;
    static bool loaded;
    if (loaded) return symbols;
    loaded = true;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring map = path;
    map = map.substr(0, map.find_last_of(L'.')) + L".map";
    FILE *f = _wfopen(map.c_str(), L"r");
    if (!f) return symbols;
    char line[4096];
    while (std::fgets(line, sizeof(line), f)) {
        unsigned section, offset, address;
        char name[2048], rest[1024];
        rest[0] = 0;
        if (std::sscanf(line, " %x:%x %2047s %x %1023[^\n]", &section, &offset, name, &address, rest) < 4 || section == 0) continue;
        std::string object = rest;
        while (!object.empty() && object.back() == ' ') object.pop_back();
        const size_t space = object.find_last_of(' ');
        if (space != std::string::npos) object = object.substr(space + 1);
        symbols.push_back({address, name, object});
    }
    std::fclose(f);
    std::sort(symbols.begin(), symbols.end(), [](const Symbol &a, const Symbol &b) { return a.address < b.address; });
    return symbols;
}

inline std::string Undecorate(const std::string &name)
{
    using Fn = DWORD(WINAPI *)(const char *, char *, DWORD, DWORD);
    static Fn undecorate = [] {
        HMODULE m = LoadLibraryW(L"dbghelp.dll");
        return m ? reinterpret_cast<Fn>(GetProcAddress(m, "UnDecorateSymbolName")) : nullptr;
    }();
    char out[1024];
    if (name.size() > 1 && name[0] == '?' && undecorate && undecorate(name.c_str(), out, sizeof(out), 0x1000 /* UNDNAME_NAME_ONLY */))
        return out;
    return name;
}

// The original's functions (tools/difftest_funcs.py writes build\difftest.funcs.txt from db/: "rva size name" per line;
// difftest.ps1 sets DIFFTEST_FUNCS to it, and the file next to build\<Config>\ is the fallback), to name the function a
// fault address in the original image is in.
struct OriginalFunction {
    uint32_t rva, size;
    std::string name;
};
inline const std::vector<OriginalFunction> &OriginalFunctions()
{
    static std::vector<OriginalFunction> funcs;
    static bool loaded;
    if (loaded) return funcs;
    loaded = true;
    std::wstring file;
    if (const wchar_t *env = _wgetenv(L"DIFFTEST_FUNCS")) file = env;
    else {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        file = path;
        file = file.substr(0, file.find_last_of(L'\\')); // build\<Config>
        file = file.substr(0, file.find_last_of(L'\\')) + L"\\difftest.funcs.txt";
    }
    FILE *f = _wfopen(file.c_str(), L"r");
    if (!f) return funcs;
    char line[2048];
    while (std::fgets(line, sizeof(line), f)) {
        unsigned rva, size;
        int used = 0;
        if (std::sscanf(line, "%x %u %n", &rva, &size, &used) < 2 || used == 0) continue;
        std::string name = line + used;
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
        funcs.push_back({rva, size, name});
    }
    std::fclose(f);
    return funcs;
}

// "rva 0x1234 (the original) in function 0x1200 Name+0x34"; "after function ..." when the nearest function before it
// ends earlier (the table is Ghidra's: an address past a function's size is in code it did not find as a function).
inline std::string DescribeOriginalRva(uint32_t rva)
{
    char b[1024];
    std::snprintf(b, sizeof(b), "rva 0x%x (the original)", rva);
    const std::vector<OriginalFunction> &funcs = OriginalFunctions();
    auto it = std::upper_bound(funcs.begin(), funcs.end(), rva, [](uint32_t a, const OriginalFunction &fn) { return a < fn.rva; });
    if (it == funcs.begin()) return funcs.empty() ? std::string(b) + " (no difftest.funcs.txt to name it)" : std::string(b);
    --it;
    const bool inside = rva - it->rva < (it->size ? it->size : 1);
    const size_t used = std::strlen(b);
    std::snprintf(b + used, sizeof(b) - used, " %s function 0x%x %s+0x%x", inside ? "in" : "after", it->rva, it->name.c_str(),
                  rva - it->rva);
    return b;
}

// An address as text: the original (rva and function), or module+offset and the map's symbol in ours.
inline std::string DescribeAddress(uintptr_t imageBase, uintptr_t imageSize, uint32_t at)
{
    char b[1024];
    if (at >= imageBase && at - imageBase < imageSize) return DescribeOriginalRva(static_cast<uint32_t>(at - imageBase));
    HMODULE module = nullptr;
    char where[MAX_PATH] = "";
    const auto self = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // The runner is this same exe at the same fixed base; other modules are the system's, at the runner's addresses
    // (shared DLLs load at one address per boot).
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<const char *>(static_cast<uintptr_t>(at)), &module)) {
        char path[MAX_PATH];
        GetModuleFileNameA(module, path, MAX_PATH);
        const char *leaf = std::strrchr(path, '\\');
        std::snprintf(where, sizeof(where), "%s+0x%x", leaf ? leaf + 1 : path, static_cast<unsigned>(at - reinterpret_cast<uintptr_t>(module)));
    } else {
        std::snprintf(where, sizeof(where), "0x%x (no module)", at);
    }
    std::string text = where;
    if (reinterpret_cast<uintptr_t>(module) == self) {
        const std::vector<Symbol> &symbols = MapSymbols();
        auto it = std::upper_bound(symbols.begin(), symbols.end(), at, [](uint32_t a, const Symbol &sym) { return a < sym.address; });
        if (it != symbols.begin()) {
            --it;
            const bool ours = it->object.rfind("ts2core:", 0) == 0;
            const bool crt = it->object.find("lib") == 0 || it->object.find("msvcprt") != std::string::npos;
            std::string name = Undecorate(it->name);
            if (name.size() > 110) name = name.substr(0, 100) + "...";
            std::snprintf(b, sizeof(b), " %s+0x%x [%s]%s", name.c_str(), at - it->address, it->object.c_str(),
                          ours ? " (ours, src/)" : crt ? " (CRT/STL)" : "");
            text += b;
        } else if (symbols.empty()) {
            text += " (no difftest.map to name it)";
        }
    }
    return text;
}

inline std::string Describe(const State &s, uint32_t at)
{
    return DescribeAddress(s.imageBase, s.imageSize, at);
}

inline const char *ExitMeaning(DWORD code)
{
    switch (code) {
    case 0xc0000409: return "fast fail: a /GS stack cookie check, abort(), std::terminate, a pure virtual call or an invalid CRT "
                            "parameter in our code, or __fastfail";
    case 0xc00000fd: return "stack overflow";
    case 0xc0000005: return "access violation that no handler took (a corrupted SEH chain, or one during exception dispatch)";
    case 0xc0000374: return "heap corruption (ntdll)";
    case 0xc0000028: return "bad stack during exception dispatch (corrupted SEH chain or esp)";
    case 0xc0000194: return "possible deadlock";
    case 0xe06d7363: return "unhandled C++ exception";
    case 0xc000001d: return "illegal instruction";
    case 0xc0000096: return "privileged instruction";
    case 3: return "abort() (exit code 3)";
    default: return "the process exited";
    }
}

inline const char *NoteMeaning(uint32_t kind)
{
    switch (kind) {
    case kFastFail: return "fast fail requested (IsProcessorFeaturePresent(PF_FASTFAIL_AVAILABLE)) by";
    case kTerminate: return "std::terminate called (unhandled C++ exception or noexcept violation); handler entered from";
    case kPurecall: return "pure virtual call; _purecall at";
    case kInvalidParameter: return "invalid parameter to a CRT function; handler entered from";
    case kAbort: return "abort() called; SIGABRT raised from";
    default: return "?";
    }
}

inline void Report(const State &s, DWORD code)
{
    std::fflush(stdout);
    const auto *ctx = reinterpret_cast<const difftest::Context *>(s.context);
    const unsigned long long cases = s.contextLive ? ctx->cases : 0;
    if (s.phase == 1) {
        std::printf("DIED in test %s (rva 0x%x, %s), test %u of this run, at ctx.cases %llu%s%s%s: exit code 0x%08lx, %s\n", s.test,
                    s.testRva, s.file, s.started, cases, s.mark[0] ? ", mark \"" : "", s.mark, s.mark[0] ? "\"" : "", code, ExitMeaning(code));
        if (s.noteCount) std::printf("    last drawfx::Note: \"%s\" (note %u of the test)\n", s.lastNote, s.noteCount);
        for (uint32_t i = 0; i < s.notes && i < kMaxNotes; ++i)
            std::printf("    %s %s\n", NoteMeaning(s.noteKind[i]), Describe(s, s.noteAddress[i]).c_str());
        if (s.chains) {
            std::printf("    its callers (frame-pointer chain; stops at code without frame pointers or a smashed frame):\n");
            for (uint32_t i = 0; i < s.chains && i < kMaxChain; ++i) std::printf("        %s\n", Describe(s, s.chain[i]).c_str());
        }
        if (s.frames) {
            std::printf("    return-address-like words on the stack (innermost first; a scan, includes stale ones):\n");
            for (uint32_t i = 0; i < s.frames && i < kMaxFrames; ++i) std::printf("        %s\n", Describe(s, s.frame[i]).c_str());
        }
        if (s.exceptions) {
            auto exception = [&](const char *which, uint32_t c, uint32_t at, uint32_t target) {
                std::printf("    %s exception in this test: 0x%08x at %s", which, c, Describe(s, at).c_str());
                if (c == EXCEPTION_ACCESS_VIOLATION) std::printf(" (accessing 0x%x)", target);
                if (c == kImportTrapCode) std::printf(" (import trap: %s)", s.lastImport);
                std::printf("\n");
            };
            std::printf("    %u exception%s seen during the test (first chance; any may have been handled):\n", s.exceptions,
                        s.exceptions == 1 ? "" : "s");
            if (s.exceptions > 1) exception("first", s.firstCode, s.firstAddress, s.firstTarget);
            exception("last", s.lastCode, s.lastAddress, s.lastTarget);
        } else {
            std::printf("    no exception seen during the test\n");
        }
        std::printf("difftest.exe died in test %s at ctx.cases %llu: exit code 0x%08lx (details above)\n", s.test, cases, code);
    } else {
        std::printf("DIED between tests (after %u started): exit code 0x%08lx, %s\n", s.started, code, ExitMeaning(code));
        std::printf("difftest.exe died outside a test: exit code 0x%08lx\n", code);
    }
    std::fflush(stdout);
}

// Monitor side: runs this exe again as the runner and reports if it dies. Returns false when the runner could not be
// started under a job (then the caller runs unmonitored).
inline bool RunMonitored(int &exitCode)
{
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(State), nullptr);
    if (!map) return false;
    auto *s = static_cast<State *>(MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, sizeof(State)));
    // The runner dies with the monitor (a test runner killed on timeout must not leave it behind).
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!s || !job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) return false;
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wchar_t handleText[32];
    std::swprintf(handleText, 32, L" --monitor-state %x", static_cast<unsigned>(reinterpret_cast<uintptr_t>(map)));
    std::wstring cmd = std::wstring(GetCommandLineW()) + handleText;
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::fflush(stdout);
    std::fflush(stderr);
    // Inherits the console or the redirected stdout/stderr, so the runner's output goes where ours would.
    if (!CreateProcessW(self, cmd.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) return false;
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    exitCode = static_cast<int>(code);
    if (s->phase != 2) {
        Report(*s, code);
        if (exitCode == 0) exitCode = 1; // exit(0) mid-run (a test or our code called it) is no pass
    }
    return true;
}

} // namespace monitor
