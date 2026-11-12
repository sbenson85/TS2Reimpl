// difftest runner: loads the original image, runs registered differential tests, prints a summary
// and optionally writes JSON.
// Usage: difftest.exe --image <unpacked exe> [--filter <substring or file stem>[,...] | --filter @<file of test names>]
//                     [--json <path>] [--trace <n>] [--jobs <n> [--times <file>]]
// --jobs n (n > 1): runs the selected tests in n runner processes at once and prints one run's output (harness/shards.h);
// --times: the per-test times file it balances on and updates (default difftest.times.txt next to this exe).
// --trace n: also traces the first n calls of each test's original function (record/tracer.h) and
// prints what was captured; a check of the record-and-replay tracer against the tests' inputs.
// --capture-dir d (with --trace): runs tests on the capture stack and writes each capture to
// d/<rva>/<test>-<n>.bin. --replay d: replays every capture file under d in a child process each
// (record/replay.h) and reports whether the original reproduces it; with --ours 1, whether our
// replacement (the hook table's entry for that RVA) does. --replay-one f: the child side.
// --shuffle s (s > 0, one process only, not with --jobs): runs the selected tests in an order shuffled with seed s
// (printed first), to find tests that depend on state an earlier test left; the same seed gives the same order.
// --repeat n (n > 1, one process only, not with --jobs): runs the selected tests n times over in this one process (an
// intermittent failure's rate, heap layouts and predecessors varying as they do in a long run). With --shuffle s,
// iteration k (from 0) runs in the order of seed s + k. Each result line names its iteration ("iteration k" after the
// cases); at the end one "repeat:" line per test that failed, with its failing iterations and count.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/espcheck.h"
#include "sandbox.h"
#include "hook/hooks.h"
#include "monitor.h"
#include "replay.h"
#include "shards.h"
#include "tracer.h"

#include <windows.h>
#include <float.h>
#include <malloc.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace difftest {
std::vector<Test> &Registry()
{
    static std::vector<Test> tests;
    return tests;
}

char gLastMark[96]; // the last ctx.Mark of the running test (also kept when there is no monitor), for the error line
char gLastNote[64]; // the last drawfx::Note label and the test's note count (NoteBreadcrumb), for the error line
unsigned gNoteCount;
void NoteBreadcrumb(const char *label)
{
    ++gNoteCount;
    strncpy_s(gLastNote, label ? label : "", _TRUNCATE);
    if (monitor::gState) {
        monitor::gState->noteCount = gNoteCount;
        strncpy_s(monitor::gState->lastNote, gLastNote, _TRUNCATE);
    }
}
bool gStackFill; // TS2_STACK_FILL (difftest.ps1 -StackFill): see FillStackBelow
void FillStackBelow(unsigned bytes)
{
    // Writes the pattern into the stack below the caller's frame, then returns: the next calls (a test body, the
    // original, ours) get frames over 0xCDCDCDCD words instead of whatever the last call left, so a read of an
    // uninitialised local shows as 0xcdcdcdcd (or the float -431602080).
    volatile uint32_t *p = static_cast<volatile uint32_t *>(_alloca(bytes));
    for (unsigned i = 0; i < bytes / 4; ++i) p[i] = 0xcdcdcdcdu;
}
void Context::Mark(const char *label)
{
    strncpy_s(gLastMark, label ? label : "", _TRUNCATE);
    if (monitor::gState) strncpy_s(monitor::gState->mark, label ? label : "", _TRUNCATE);
}

X87Precision::X87Precision(X87Pc pc)
{
    unsigned int set = 0;
    __control87_2(0, 0, &saved_, nullptr);
    __control87_2(static_cast<unsigned>(pc), _MCW_PC, &set, nullptr);
}

X87Precision::~X87Precision()
{
    unsigned int set = 0;
    __control87_2(saved_, _MCW_PC, &set, nullptr);
}

// Set to true once the in-game probe (TS2HOST_X87PROBE, host/control_x87probe.cpp) shows 24-bit precision on the threads
// running the game's float code; then AtGamePrecision runs at 24-bit without TS2_X87_GAME.
constexpr bool kGameX87PcConfirmed = false;

X87Pc GameX87Precision()
{
    static const bool asked = [] {
        const char *v = std::getenv("TS2_X87_GAME");
        return v && std::strcmp(v, "0") != 0;
    }();
    return kGameX87PcConfirmed || asked ? X87Pc::Bits24 : X87Pc::Bits53;
}
} // namespace difftest

namespace {

std::string JsonEscape(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

// Runs a test body under SEH so a crash or import trap fails only that test.
// Kept free of C++ objects with destructors, as __try requires.
uintptr_t gExceptionAddress;  // where the last test exception was raised
uintptr_t gExceptionTarget;   // for access violations: the address accessed
bool gSweepNotes;             // TS2_FAILURE_NOTES: print passing tests' failure notes too

int RecordException(EXCEPTION_POINTERS *info)
{
    gExceptionAddress = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    gExceptionTarget = info->ExceptionRecord->NumberParameters >= 2 ? info->ExceptionRecord->ExceptionInformation[1] : 0;
    return EXCEPTION_EXECUTE_HANDLER;
}

bool RunGuarded(const difftest::Test &test, difftest::Context &ctx, unsigned long &code);

// Image globals that tests borrow and must hand back as they found them. Fixtures build the game's general allocator only
// when its bytes are all zero and point the allocator service's function-local static at an object of their own, so a
// test that leaves either behind (built and not shut down, shut down and not zeroed, or a crash that skipped a fixture's
// destructor: the runner's SEH handler does not unwind C++ objects) hands every later test a stale allocator. Each is
// compared with its state before the test; a change fails the test that made it and is undone, so later tests start
// clean.
struct WatchedGlobal {
    const char *name;
    uint32_t rva;
    uint32_t size;
};
constexpr WatchedGlobal kWatchedGlobals[] = {
    {"the game's general allocator", 0x10cb098, 0x518}, // sizeof(GeneralAllocator), src/rz/GeneralAllocator.h
    {"the allocator service's cache", 0x10c95d4, 4},
    {"the allocator service's guard", 0x10c95d8, 4},
};
constexpr size_t kWatchedBytes = 0x518 + 4 + 4;

void SnapshotWatched(const OriginalImage &image, uint8_t (&out)[kWatchedBytes])
{
    size_t at = 0;
    for (const WatchedGlobal &g : kWatchedGlobals) {
        std::memcpy(out + at, image.At<uint8_t>(g.rva), g.size);
        at += g.size;
    }
}

// The process's allocations (VirtualAlloc reservations and images), from one walk of the address space, in address order.
// Kept in static arrays: the check must not touch the process heap, or the heap layout a test sees would depend on it
// (some tests compare heap addresses the two sides get, e.g. PropertySetXml_Object's Destruct).
struct Allocation {
    uintptr_t base;
    uint32_t size;
    bool headReadWrite; // the first region is committed, private and plainly read/write
};
constexpr size_t kMaxAllocations = 1 << 16;
Allocation gAllocationsBefore[kMaxAllocations], gAllocationsAfter[kMaxAllocations];

// Returns the count, or kMaxAllocations + 1 when there were more.
size_t Allocations(Allocation (&out)[kMaxAllocations])
{
    size_t n = 0;
    MEMORY_BASIC_INFORMATION mbi;
    for (uintptr_t at = 0x10000; VirtualQuery(reinterpret_cast<void *>(at), &mbi, sizeof(mbi)) == sizeof(mbi);) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t allocationBase = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
        if (mbi.State != MEM_FREE) {
            if (n == 0 || allocationBase != out[n - 1].base) {
                if (n == kMaxAllocations) return kMaxAllocations + 1;
                out[n++] = {allocationBase, 0,
                            base == allocationBase && mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE};
            }
            out[n - 1].size += static_cast<uint32_t>(mbi.RegionSize);
        }
        const uintptr_t next = base + mbi.RegionSize;
        if (next <= at) break;
        at = next;
    }
    return n;
}

// Game general-allocator cores reserved during a test and still reserved after it. A core's header sits at the base of its
// reservation (src/rz/GeneralAllocator.h GeneralAllocatorCore: firstChunk = (core + 0x27) & ~7 at +0, the reserved size at
// +8), which heap segments and the tests' own buffers don't look like. GeneralAllocator::Shutdown releases none of them
// (tests/game_allocator.h), so a fixture that builds an allocator and only shuts it down leaks 16 MB of address space or
// more each time; a full run used to run out of address space that way, and a later, innocent test crashed in the
// original's allocator (MallocInternal puts the null core VirtualAlloc returns into SetTop).
void CheckCoreLeaks(size_t beforeCount, std::vector<std::string> &failures)
{
    const size_t afterCount = Allocations(gAllocationsAfter);
    if (beforeCount > kMaxAllocations || afterCount > kMaxAllocations) {
        failures.push_back("more than 65536 allocations in the process: the core-leak check cannot run");
        return;
    }
    const Allocation *before = gAllocationsBefore;
    int count = 0;
    uint64_t bytes = 0;
    size_t i = 0;
    for (size_t k = 0; k < afterCount; ++k) {
        const Allocation &a = gAllocationsAfter[k];
        while (i < beforeCount && before[i].base < a.base) ++i;
        if (i < beforeCount && before[i].base == a.base) continue; // there before the test
        if (!a.headReadWrite || a.size < 0x10000) continue;
        const uint32_t *header = reinterpret_cast<const uint32_t *>(a.base);
        if (header[0] != ((a.base + 0x27) & ~uintptr_t(7)) || header[2] != a.size) continue;
        ++count;
        bytes += a.size;
    }
    if (count) {
        char b[256];
        std::snprintf(b, sizeof(b), "left %d game allocator core%s (%llu MB of address space) reserved: release them after Shutdown "
                      "(tests/game_allocator.h)", count, count == 1 ? "" : "s", bytes >> 20);
        failures.push_back(b);
    }
}

// Fails a test that leaves the floating-point state changed for the next one, and restores it: the x87 or SSE control word
// (precision, rounding, exception masks, denormal handling) or values left on the x87 stack. The original computes in x87
// and ours in SSE2, so a precision or rounding mode an earlier test left behind would make results that are a few ulps
// apart differ in the full suite only (lane LU looked for this behind AudioObjectUpdateDemon_Run's flake; no test does it).
void CheckFloatingPoint(unsigned int x87Before, unsigned int sseBefore, std::vector<std::string> &failures)
{
    char b[256];
    uint8_t env[28]; // the protected-mode 32-bit environment: the tag word at +8
    __asm {
        fnstenv env
        fldenv env
    }
    const uint16_t tags = *reinterpret_cast<const uint16_t *>(env + 8);
    if (tags != 0xffff) {
        std::snprintf(b, sizeof(b), "left values on the x87 stack (tag word %04x)", tags);
        failures.push_back(b);
        __asm emms
    }
    unsigned int x87After = 0, sseAfter = 0;
    __control87_2(0, 0, &x87After, &sseAfter);
    if (x87After != x87Before || sseAfter != sseBefore) {
        std::snprintf(b, sizeof(b), "left the floating-point control words changed (x87 %x -> %x, SSE %x -> %x)", x87Before,
                      x87After, sseBefore, sseAfter);
        failures.push_back(b);
        unsigned int set = 0;
        __control87_2(x87Before, _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC, &set, nullptr);
        __control87_2(sseBefore, _MCW_EM | _MCW_RC | _MCW_DN, nullptr, &set);
    }
}

// Appends one failure per watched global the test changed and restores it.
void CheckWatched(const OriginalImage &image, const uint8_t (&before)[kWatchedBytes], std::vector<std::string> &failures)
{
    size_t at = 0;
    for (const WatchedGlobal &g : kWatchedGlobals) {
        uint8_t *live = image.At<uint8_t>(g.rva);
        if (std::memcmp(live, before + at, g.size) != 0) {
            uint32_t first = 0;
            while (live[first] == before[at + first]) ++first;
            char b[256];
            std::snprintf(b, sizeof(b), "left %s (rva 0x%x) changed, first at +0x%x; restored for the next test", g.name, g.rva, first);
            failures.push_back(b);
            std::memcpy(live, before + at, g.size);
        }
        at += g.size;
    }
}

// RunGuarded on the capture stack (record::RunOnCaptureStack), for capture runs.
struct GuardedCall {
    const difftest::Test *test;
    difftest::Context *ctx;
    unsigned long code;
    bool completed;
};
void RunGuardedThunk(void *arg)
{
    auto *call = static_cast<GuardedCall *>(arg);
    call->completed = RunGuarded(*call->test, *call->ctx, call->code);
}

bool RunGuarded(const difftest::Test &test, difftest::Context &ctx, unsigned long &code)
{
    __try {
        if (difftest::gStackFill) difftest::FillStackBelow();
        test.body(ctx);
        return true;
    } __except (code = GetExceptionCode(), RecordException(GetExceptionInformation())) {
        // The overflow used up the stack's guard page: put it back, or the next overflow (in any later test) kills the
        // process instead of failing that test.
        if (code == EXCEPTION_STACK_OVERFLOW) _resetstkoflw();
        return false;
    }
}

// --replay: every capture file under dir (whose path contains filter), each in a child process.
int ReplayAll(const std::wstring &imagePath, const std::wstring &dir, const std::wstring &filter, bool ours)
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::vector<std::wstring> files;
    std::vector<std::wstring> pending{dir};
    while (!pending.empty()) {
        const std::wstring d = pending.back();
        pending.pop_back();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) pending.push_back(d + L"\\" + n);
            else if (n.size() > 4 && n.substr(n.size() - 4) == L".bin") files.push_back(d + L"\\" + n);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    int pass = 0, fail = 0, skip = 0;
    std::map<std::string, int> skipReasons;
    for (const std::wstring &file : files) {
        if (!filter.empty() && file.find(filter) == std::wstring::npos) continue;
        const record::ReplayResult r = record::ReplayInChildProcess(self, L"--image \"" + imagePath + L"\" --replay-one \"" + file + L"\"" + (ours ? L" --ours 1" : L""), file);
        const std::string name(file.begin() + static_cast<std::ptrdiff_t>(dir.size()) + 1, file.end());
        if (r.outcome == record::ReplayOutcome::Pass) {
            ++pass;
        } else if (r.outcome == record::ReplayOutcome::Fail) {
            ++fail;
            std::printf("[fail] %s: %s\n", name.c_str(), r.detail.c_str());
        } else {
            ++skip;
            // Group skips by reason, without the addresses.
            std::string reason = r.detail.substr(0, r.detail.find(" 0x"));
            ++skipReasons[reason];
            std::printf("[skip] %s: %s\n", name.c_str(), r.detail.c_str());
        }
        std::fflush(stdout);
    }
    std::printf("replay: %d pass, %d fail, %d skipped\n", pass, fail, skip);
    for (auto &[reason, n] : skipReasons) std::printf("  skipped %d: %s\n", n, reason.c_str());
    return fail ? 1 : 0;
}

void PreloadDlls()
{
    if (SUCCEEDED(CoInitialize(nullptr))) {
        IUnknown *link = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&link))))
            link->Release();
        CoUninitialize();
    }
}

bool InitOriginalCrt(const OriginalImage &image, unsigned long &code)
{
    auto initCrt = image.At<bool __cdecl(int)>(0x824af8);
    __try {
        return initCrt(1);
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    std::wstring imagePath, filter, jsonPath;
    uint32_t traceLimit = 0;
    std::wstring captureDir, replayDir, replayOne, monitorState, timesPath;
    bool ours = false;
    int jobs = 1;
    uint32_t shuffleSeed = 0;
    int repeat = 1;
    // The monitor appends this pair to the runner's command line (harness/monitor.h); found wherever it is.
    for (int i = 1; i + 1 < argc; ++i)
        if (argv[i] == std::wstring(L"--monitor-state")) monitorState = argv[i + 1];
    for (int i = 1; i + 1 < argc; i += 2) {
        std::wstring a = argv[i];
        if (a == L"--image") imagePath = argv[i + 1];
        else if (a == L"--filter") filter = argv[i + 1];
        else if (a == L"--json") jsonPath = argv[i + 1];
        else if (a == L"--trace") traceLimit = static_cast<uint32_t>(_wtoi(argv[i + 1]));
        else if (a == L"--capture-dir") captureDir = argv[i + 1];
        else if (a == L"--replay") replayDir = argv[i + 1];
        else if (a == L"--replay-one") replayOne = argv[i + 1];
        else if (a == L"--ours") ours = argv[i + 1] == std::wstring(L"1");
        else if (a == L"--jobs") jobs = _wtoi(argv[i + 1]);
        else if (a == L"--times") timesPath = argv[i + 1];
        else if (a == L"--shuffle") shuffleSeed = static_cast<uint32_t>(_wtoi(argv[i + 1]));
        else if (a == L"--repeat") repeat = (std::max)(1, _wtoi(argv[i + 1]));
    }
    if ((shuffleSeed || repeat > 1) && jobs > 1) {
        std::fprintf(stderr, "--shuffle and --repeat run in one process: drop --jobs\n");
        return 2;
    }
    // --list-hooks: every registered hook as "name original-rva replacement-rva" (the replacement's RVA in
    // this exe), for tools/stackcheck.py. Needs no image.
    if (argc == 2 && argv[1] == std::wstring(L"--list-hooks")) {
        const auto self = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        for (const HookSpec *h : RegisteredHooks())
            std::printf("%s 0x%x 0x%x\n", h->name, h->rva, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h->replacement) - self));
        return 0;
    }
    if (imagePath.empty()) {
        std::fprintf(stderr, "usage: difftest --image <unpacked exe> [--filter <substr>] [--json <path>]\n");
        return 2;
    }
    // Test names are ASCII, so a plain narrowing of the filter is enough.
    std::string narrowFilter;
    for (wchar_t c : filter) narrowFilter += static_cast<char>(c);
    // "--filter @file": run exactly the tests named in the file (one per line, in registry order), e.g. to bisect
    // which earlier test leaves state behind that a later one trips over.
    std::set<std::string> names;
    const bool byName = !narrowFilter.empty() && narrowFilter[0] == '@';
    if (byName) {
        std::ifstream list(narrowFilter.substr(1));
        for (std::string line; std::getline(list, line);) {
            while (!line.empty() && (std::isspace(static_cast<unsigned char>(line.back())))) line.pop_back();
            if (!line.empty()) names.insert(line);
        }
    }
    // A plain filter matches a test name (substring) or the stem of the file that registered it
    // (`ts_family_relationship_array` or `ts_family_relationship_array.cpp` runs every test in that file).
    // A comma-separated list selects the tests any of its parts selects (test names and file stems have no commas).
    std::vector<std::string> parts;
    if (!byName) {
        for (size_t at = 0;;) {
            const size_t comma = narrowFilter.find(',', at);
            std::string part = narrowFilter.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            if (!part.empty()) parts.push_back(part);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
    }
    auto selected = [&](const difftest::Test &test) {
        if (byName) return names.count(test.name) != 0;
        if (parts.empty()) return true;
        std::string stem = test.file ? test.file : "";
        stem = stem.substr(stem.find_last_of("/\\") == std::string::npos ? 0 : stem.find_last_of("/\\") + 1);
        if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".cpp") == 0) stem.resize(stem.size() - 4);
        for (const std::string &part : parts) {
            if (std::string(test.name).find(part) != std::string::npos) return true;
            std::string want = part;
            if (want.size() > 4 && want.compare(want.size() - 4, 4, ".cpp") == 0) want.resize(want.size() - 4);
            if (!stem.empty() && _stricmp(stem.c_str(), want.c_str()) == 0) return true;
        }
        return false;
    };

    // --jobs N (N > 1): the selected tests over N runner processes, one run's output (harness/shards.h). Not with --trace,
    // captures or replays, whose output and files are per process.
    if (jobs > 1 && monitorState.empty() && replayOne.empty() && replayDir.empty() && captureDir.empty() && !traceLimit) {
        std::vector<const difftest::Test *> chosen;
        for (const difftest::Test &test : difftest::Registry())
            if (selected(test)) chosen.push_back(&test);
        if (!chosen.empty()) return shards::RunSharded(chosen, imagePath, jsonPath, timesPath, jobs);
        // Nothing selected: the single runner says so as always.
    }
    // A test run goes through a monitor process that reports a runner that dies (harness/monitor.h).
    if (monitorState.empty() && replayOne.empty() && replayDir.empty() && !std::getenv("DIFFTEST_NO_MONITOR")) {
        int exitCode = 0;
        if (monitor::RunMonitored(exitCode)) return exitCode;
    }

    // Let exceptions reach handlers in the mapped image (see CMakeLists.txt).
    SetProcessDEPPolicy(0);
    difftest::InstallFreeRecorderHooks();
    if (!InitSandbox()) {
        std::fprintf(stderr, "cannot create the file sandbox directory\n");
        return 2;
    }
    OriginalImage image;
    std::string error;
    if (!image.Load(imagePath, error)) {
        std::fprintf(stderr, "failed to load image: %s\n", error.c_str());
        return 2;
    }
    if (image.Base() != kGameBase) {
        std::fprintf(stderr, "the image is at %p, not at the required base %p\n", reinterpret_cast<void *>(image.Base()), reinterpret_cast<void *>(kGameBase));
        return 2;
    }
    SetGameBase(image.Base()); // our code reads game data/vtables from the mapped original
    InstallEspCheckReporter([](const char *line) { std::fprintf(stderr, "%s\n", line); }, image.Base(),
                            image.Base() + image.Size());
    // DLLs that tests load later (COM's shell link for .lnk files) would make the loader reallocate
    // the thread's TLS array mid-test; load them now, before the image's TLS slot is added.
    PreloadDlls();
    if (!image.AttachTlsToThisThread(error)) {
        std::fprintf(stderr, "image TLS setup failed: %s\n", error.c_str());
        return 2;
    }

    // Initialise the original's statically linked CRT (vcruntime + UCRT: locks, per-thread data,
    // heap handle, locale tables), so library code it reaches (printf, errno, locale) works. Only
    // CRT state; none of the game's own static constructors run.
    // __scrt_initialize_crt(__scrt_module_type::exe), RVA 0x824af8 in 1.18.0.187.
    {
        unsigned long code = 0;
        if (!InitOriginalCrt(image, code)) {
            std::fprintf(stderr, "original CRT init failed (%s)\n",
                code == kImportTrapCode ? ("import " + OriginalImage::LastTrappedImport()).c_str() : "returned false or crashed");
            return 2;
        }
    }
    if (*image.At<HANDLE>(0x10e0380) != GetProcessHeap()) {
        std::fprintf(stderr, "original CRT heap (__acrt_heap, RVA 0x10e0380) is not the process heap\n");
        return 2;
    }
    {
        // Our CRT must allocate from the same heap, or objects can't be shared with original code.
        void *p = std::malloc(24);
        bool shared = HeapSize(GetProcessHeap(), 0, p) == 24;
        difftest::FreeRecorder rec; // the recorder must see our CRT's frees (free_recorder.h)
        std::free(p);
        rec.Stop();
        if (!rec.Freed(p) || rec.Count() != 1) {
            std::fprintf(stderr, "free recorder does not see our CRT's frees\n");
            return 2;
        }
        if (!shared) {
            std::fprintf(stderr, "our CRT does not allocate from the process heap; cannot share objects\n");
            return 2;
        }
        // The allocation counter must see our CRT's malloc, realloc and free (free_recorder.h).
        difftest::AllocationCounter counter;
        void *q = std::malloc(40);
        const bool counted = counter.Blocks() == 1 && counter.Bytes() == 40 && counter.Allocated(q);
        q = std::realloc(q, 4000);
        const bool grown = counter.Blocks() == 1 && counter.Bytes() == 4000;
        std::free(q);
        counter.Stop();
        if (!counted || !grown || counter.Blocks() != 0 || counter.Bytes() != 0) {
            std::fprintf(stderr, "allocation counter does not see our CRT's malloc / realloc / free\n");
            return 2;
        }
    }

    if (!replayOne.empty()) {
        void *replacement = nullptr;
        if (ours) {
            record::CaptureFile f;
            if (!record::ReadCapture(replayOne, f, error)) {
                std::printf("skip: %s\n", error.c_str());
                return 3;
            }
            for (const HookSpec *h : RegisteredHooks())
                if (h->rva == f.rva) replacement = h->replacement;
            if (!replacement) {
                std::printf("skip: no hook for rva 0x%x\n", f.rva);
                return 3;
            }
        }
        return record::ReplayInChild(image.Base(), replayOne, replacement);
    }
    if (!replayDir.empty()) return ReplayAll(imagePath, replayDir, filter, ours);

    // DIFFTEST_VMSTATS=1: report tests that leave more than 8 MB of address space reserved (the process has 4 GB in all).
    // Leaked game allocator cores fail the test whatever this says (CheckCoreLeaks).
    const bool vmStats = std::getenv("DIFFTEST_VMSTATS") != nullptr;
    gSweepNotes = std::getenv("TS2_FAILURE_NOTES") != nullptr;
    // TS2_X87_PC=24|53|64: every test runs with the x87 at that precision (difftest::X87Precision; the game's is 24).
    unsigned int x87PcOverride = ~0u;
    if (const char *pc = std::getenv("TS2_X87_PC")) {
        if (!std::strcmp(pc, "24")) x87PcOverride = _PC_24;
        else if (!std::strcmp(pc, "53")) x87PcOverride = _PC_53;
        else if (!std::strcmp(pc, "64")) x87PcOverride = _PC_64;
        else {
            std::fprintf(stderr, "TS2_X87_PC=%s: expected 24, 53 or 64\n", pc);
            return 2;
        }
        std::printf("x87 precision: %s-bit for every test (TS2_X87_PC)\n", pc);
    }
    auto usedAddressSpace = []() {
        uint64_t used = 0;
        MEMORY_BASIC_INFORMATION mbi;
        for (uintptr_t at = 0x10000; VirtualQuery(reinterpret_cast<void *>(at), &mbi, sizeof(mbi)) == sizeof(mbi);) {
            if (mbi.State != MEM_FREE) used += mbi.RegionSize;
            const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (next <= at) break;
            at = next;
        }
        return used;
    };
    monitor::InstallRunner(monitorState, image.Base(), image.Size());
    std::string json = "{\"tests\":[";
    int failed = 0, run = 0;
    std::map<std::string, std::vector<int>> failedIn; // --repeat: each failing test's iterations
    std::vector<std::string> failedOrder;
    for (int iteration = 0; iteration < repeat; ++iteration) {
    std::vector<difftest::Test *> order;
    for (auto &test : difftest::Registry()) order.push_back(&test);
    if (shuffleSeed) {
        std::mt19937 shuffleRng(shuffleSeed + static_cast<uint32_t>(iteration));
        std::shuffle(order.begin(), order.end(), shuffleRng);
        std::printf("test order shuffled with seed %u\n", shuffleSeed + static_cast<uint32_t>(iteration));
    }
    for (difftest::Test *each : order) {
        auto &test = *each;
        std::string name = test.name;
        if (!selected(test)) continue;

        difftest::Context &ctx = monitor::BeginTest(test, image); // in the monitor's shared block
        unsigned long code = 0;
        std::string tlsError;
        if (!image.EnsureTls(tlsError)) {
            std::fprintf(stderr, "image TLS: %s\n", tlsError.c_str());
            return 2;
        }
        const uint64_t vmBefore = vmStats ? usedAddressSpace() : 0;
        const size_t allocationsBefore = Allocations(gAllocationsBefore);
        const uint64_t abandonedBefore = record::Stats().abandoned;
        uint8_t watchedBefore[kWatchedBytes];
        SnapshotWatched(image, watchedBefore);
        for (const difftest::FailureNotes &s : difftest::FailureNoteSources()) s.begin();
        unsigned int x87Before = 0, sseBefore = 0;
        __control87_2(0, 0, &x87Before, &sseBefore);
        unsigned int x87During = x87Before; // TS2_X87_PC: the precision both sides run at
        if (x87PcOverride != ~0u) __control87_2(x87PcOverride, _MCW_PC, &x87During, nullptr);
        if (traceLimit) {
            record::SetImage(image.Base(), image.Base() + image.Size());
            record::Arm(reinterpret_cast<uintptr_t>(image.At(test.rva)), traceLimit);
        }
        bool completed;
        difftest::gLastMark[0] = 0;
        difftest::TakeBufferMoves(nullptr);
        difftest::gNoteCount = 0;
        difftest::gLastNote[0] = 0;
        difftest::gStackFill = std::getenv("TS2_STACK_FILL") != nullptr;
        if (!captureDir.empty()) {
            GuardedCall call{&test, &ctx, 0, false};
            record::RunOnCaptureStack(RunGuardedThunk, &call);
            completed = call.completed;
            code = call.code;
        } else {
            completed = RunGuarded(test, ctx, code);
        }
        if (traceLimit) {
            record::Vector<record::Capture> captures = record::Disarm();
            uint64_t steps = 0, reads = 0, writes = 0, calls = 0;
            for (auto &c : captures) {
                steps += c.steps;
                reads += c.reads.size();
                writes += c.writes.size();
                calls += c.boundaries.size();
            }
            if (!captureDir.empty()) {
                wchar_t rvaHex[16];
                std::swprintf(rvaHex, 16, L"%x", test.rva);
                const std::wstring dir = captureDir + L"\\" + rvaHex;
                std::filesystem::create_directories(captureDir);
                std::filesystem::create_directories(dir);
                for (size_t k = 0; k < captures.size(); ++k) {
                    record::CaptureFile file{test.rva, static_cast<uint32_t>(image.Base()), image.Size(), captures[k]};
                    record::WriteCapture(dir + L"\\" + std::wstring(name.begin(), name.end()) + L"-" + std::to_wstring(k) + L".bin", file);
                }
            }
            std::printf("  trace: %zu captured, %llu abandoned, %llu steps, %llu bytes read, %llu written, %llu boundary calls\n",
                        captures.size(), record::Stats().abandoned - abandonedBefore, steps, reads, writes, calls);
        }
        if (vmStats) {
            const int64_t grew = int64_t(usedAddressSpace()) - int64_t(vmBefore);
            if (grew > (8 << 20)) std::printf("  vm: %s left %lld MB more address space in use\n", name.c_str(), grew >> 20);
        }
        std::string status = "pass";
        if (!completed) {
            status = "error";
            ctx.failures.push_back(code == kImportTrapCode
                ? "original called import " + OriginalImage::LastTrappedImport() + " (function is not isolated)"
                    + (difftest::gLastMark[0] ? std::string(", last mark \"") + difftest::gLastMark + "\"" : std::string())
                : [&] {
                      // Name the location (an RVA and function in the original image, or module+offset and symbol in
                      // ours), and the last ctx.Mark / sgfx::MarkStep text: the side, seed and step.
                      char b[1536];
                      std::snprintf(b, sizeof(b), "exception 0x%08lx at %s (accessing 0x%x)", code,
                                    monitor::DescribeAddress(image.Base(), image.Size(), static_cast<uint32_t>(gExceptionAddress)).c_str(),
                                    static_cast<unsigned>(gExceptionTarget));
                      std::string line = b;
                      if (difftest::gLastMark[0]) line += std::string(", last mark \"") + difftest::gLastMark + "\"";
                      if (difftest::gNoteCount)
                          line += std::string(", last drawfx::Note \"") + difftest::gLastNote + "\" (note " + std::to_string(difftest::gNoteCount) + ")";
                      return line;
                  }());
        }
        {
            const char *movedName = nullptr;
            if (const unsigned moves = difftest::TakeBufferMoves(&movedName)) {
                char b[400];
                std::snprintf(b, sizeof(b),
                              "%s reallocated %u time(s) inside a FreeRecorder window: its old block's free was counted as the code "
                              "under test's (reserve its capacity before the window opens; drawfx::Side does so for each side)",
                              movedName ? movedName : "a watched buffer", moves);
                ctx.failures.push_back(b);
            }
        }
        CheckWatched(image, watchedBefore, ctx.failures);
        CheckCoreLeaks(allocationsBefore, ctx.failures);
        CheckFloatingPoint(x87During, sseBefore, ctx.failures);
        if (x87During != x87Before) {
            unsigned int set = 0;
            __control87_2(x87Before, _MCW_PC, &set, nullptr);
        }
        for (const difftest::FailureNotes &s : difftest::FailureNoteSources())
            if (s.errors) s.errors(ctx.failures);
        {
            // What the fixtures can say about a failure (difftest::FailureNoteSources); a passing test's notes only for
            // sweeps (TS2_FAILURE_NOTES), as lines that are not failures.
            const bool failedHere = !ctx.failures.empty();
            std::vector<std::string> notes;
            for (const difftest::FailureNotes &s : difftest::FailureNoteSources()) s.report(failedHere, notes);
            if (failedHere) ctx.failures.insert(ctx.failures.end(), notes.begin(), notes.end());
            else if (gSweepNotes)
                for (const std::string &n : notes) std::printf("  note: %s: %s\n", test.name, n.c_str());
        }
        if (status == "pass" && !ctx.failures.empty()) status = "fail";
        ++run;
        failed += status != "pass";

        if (repeat > 1) {
            std::printf("[%s] %s (rva 0x%x, %llu cases, iteration %d)\n", status.c_str(), test.name, test.rva, ctx.cases, iteration);
            if (status != "pass") {
                if (!failedIn.count(name)) failedOrder.push_back(name);
                failedIn[name].push_back(iteration);
            }
        } else {
            std::printf("[%s] %s (rva 0x%x, %llu cases)\n", status.c_str(), test.name, test.rva, ctx.cases);
        }
        for (auto &f : ctx.failures) std::printf("    %s\n", f.c_str());
        std::fflush(stdout); // the failure lines too: a crash in the next test must not lose them

        char rva[16];
        std::snprintf(rva, sizeof(rva), "0x%x", test.rva);
        json += std::string(run > 1 ? "," : "") + "{\"name\":\"" + JsonEscape(name) + "\",\"rva\":\"" + rva
            + "\",\"status\":\"" + status + "\",\"cases\":" + std::to_string(ctx.cases) + ",\"failures\":[";
        for (size_t i = 0; i < ctx.failures.size(); ++i) {
            json += (i ? ",\"" : "\"") + JsonEscape(ctx.failures[i]) + "\"";
        }
        json += "]}";
        monitor::EndTest(ctx);
    }
    }
    for (const std::string &name : failedOrder) {
        const std::vector<int> &its = failedIn[name];
        std::string list;
        for (size_t k = 0; k < its.size() && k < 40; ++k) list += (k ? "," : "") + std::to_string(its[k]);
        if (its.size() > 40) list += ",...";
        std::printf("repeat: %s failed %zu of %d iterations (%s)\n", name.c_str(), its.size(), repeat, list.c_str());
    }
    if (repeat > 1 && failedOrder.empty()) std::printf("repeat: no test failed in %d iterations\n", repeat);
    json += "],\"run\":" + std::to_string(run) + ",\"failed\":" + std::to_string(failed) + "}\n";

    CleanupSandbox();
    image.DetachTlsFromThisThread();
    if (!jsonPath.empty()) std::ofstream(jsonPath) << json;
    std::printf("%d run, %d failed\n", run, failed);
    std::fflush(stdout);
    monitor::Finish();
    if (run == 0 && !narrowFilter.empty()) {
        // Loudly: a filter that selects nothing looks like a clean run otherwise (it matches a test-name substring
        // or a test file's stem, case-sensitive for names).
        std::printf("NO TESTS MATCHED --filter \"%s\" (a test-name substring, a tests/ file stem or @file of names)\n",
                    narrowFilter.c_str());
        std::fflush(stdout);
        return 3;
    }
    if (traceLimit) {
        auto &st = record::Stats();
        std::printf("trace totals: %llu captured, %llu abandoned (%llu at the step limit), %llu steps, %llu decode errors\n",
                    st.captured, st.abandoned, st.tooLong, st.steps, st.decodeErrors);
        for (auto &[mnemonic, n] : st.unhandled) std::printf("  unsized memory operand: %s x%llu\n", mnemonic.c_str(), n);
    }
    std::fflush(stdout);
    return failed ? 1 : 0;
}
