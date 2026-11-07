#include "image.h"
#include "free_recorder.h"
#include "sandbox.h"
#include "game/game.h"

#include <windows.h>
#include <intrin.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {

std::vector<std::string> gImportNames;
std::string gLastTrapped;

// Imports resolved to the real function instead of a trap: only functions whose effects stay inside
// this process (heap, thread/fiber-local storage, locks, code pages, clocks), enough for the game's
// CRT to initialise and run (printf, locale, errno). The game's CRT allocates from the process heap
// like ours, so both sides can free each other's memory. Anything touching files, windows, other
// processes or process exit still traps, except file functions confined to the sandbox directory
// (sandbox.cpp).
const char *const kPassthrough[] = {
    "KERNEL32.dll!HeapAlloc", "KERNEL32.dll!HeapFree", "KERNEL32.dll!HeapReAlloc",
    "KERNEL32.dll!HeapSize", "KERNEL32.dll!GetProcessHeap", "KERNEL32.dll!HeapValidate",
    "KERNEL32.dll!FlsAlloc", "KERNEL32.dll!FlsFree", "KERNEL32.dll!FlsGetValue", "KERNEL32.dll!FlsSetValue",
    "KERNEL32.dll!TlsAlloc", "KERNEL32.dll!TlsFree", "KERNEL32.dll!TlsGetValue", "KERNEL32.dll!TlsSetValue",
    "KERNEL32.dll!InitializeCriticalSection", "KERNEL32.dll!InitializeCriticalSectionEx",
    "KERNEL32.dll!InitializeCriticalSectionAndSpinCount", "KERNEL32.dll!EnterCriticalSection",
    "KERNEL32.dll!LeaveCriticalSection", "KERNEL32.dll!TryEnterCriticalSection", "KERNEL32.dll!DeleteCriticalSection",
    "KERNEL32.dll!InitializeSListHead", "KERNEL32.dll!EncodePointer", "KERNEL32.dll!DecodePointer",
    "KERNEL32.dll!GetLastError", "KERNEL32.dll!SetLastError", "KERNEL32.dll!GetCurrentThreadId",
    "KERNEL32.dll!GetCurrentProcessId", "KERNEL32.dll!GetCurrentThread", "KERNEL32.dll!GetCurrentProcess",
    "KERNEL32.dll!GetModuleHandleA", "KERNEL32.dll!GetModuleHandleW", "KERNEL32.dll!GetModuleHandleExW",
    "KERNEL32.dll!GetProcAddress", "KERNEL32.dll!IsProcessorFeaturePresent", "KERNEL32.dll!IsDebuggerPresent",
    "KERNEL32.dll!QueryPerformanceCounter", "KERNEL32.dll!QueryPerformanceFrequency",
    "KERNEL32.dll!GetSystemTimeAsFileTime", "KERNEL32.dll!GetTickCount", "KERNEL32.dll!GetTickCount64",
    "KERNEL32.dll!GetCommandLineA", "KERNEL32.dll!GetCommandLineW", "KERNEL32.dll!GetEnvironmentStringsW",
    "KERNEL32.dll!FreeEnvironmentStringsW", "KERNEL32.dll!GetStartupInfoW", "KERNEL32.dll!GetStdHandle",
    "KERNEL32.dll!GetFileType", "KERNEL32.dll!GetACP", "KERNEL32.dll!GetOEMCP", "KERNEL32.dll!GetCPInfo",
    "KERNEL32.dll!IsValidCodePage", "KERNEL32.dll!MultiByteToWideChar", "KERNEL32.dll!WideCharToMultiByte",
    "KERNEL32.dll!LCMapStringW", "KERNEL32.dll!LCMapStringEx", "KERNEL32.dll!GetStringTypeW",
    "KERNEL32.dll!CompareStringW", "KERNEL32.dll!CompareStringEx", "KERNEL32.dll!GetLocaleInfoW",
    "KERNEL32.dll!GetLocaleInfoEx", "KERNEL32.dll!GetUserDefaultLCID", "KERNEL32.dll!IsValidLocale",
    "KERNEL32.dll!LoadLibraryExW", "KERNEL32.dll!FreeLibrary", "KERNEL32.dll!GetModuleFileNameA",
    "KERNEL32.dll!GetModuleFileNameW", "KERNEL32.dll!RtlUnwind", "KERNEL32.dll!RaiseException",
    "KERNEL32.dll!VirtualQuery", "KERNEL32.dll!InterlockedFlushSList", "KERNEL32.dll!InterlockedPushEntrySList",
    // Thread-safe function-local statics (_Init_thread_header/footer/wait): an SRW lock and a
    // condition variable inside the image.
    "KERNEL32.dll!AcquireSRWLockExclusive", "KERNEL32.dll!ReleaseSRWLockExclusive",
    "KERNEL32.dll!WakeAllConditionVariable", "KERNEL32.dll!SleepConditionVariableSRW",
    // The game's allocator reserves and commits its own memory; process-local.
    "KERNEL32.dll!VirtualAlloc", "KERNEL32.dll!VirtualFree", "KERNEL32.dll!GetSystemInfo",
    "KERNEL32.dll!OutputDebugStringA",
    // Read-only queries of the system (no effect outside the process; both sides of a test see the
    // same answer), chosen by tools/importblockers.py.
    "KERNEL32.dll!EnumSystemLocalesW", "KERNEL32.dll!GetTimeZoneInformation", "KERNEL32.dll!GetLocalTime",
    "KERNEL32.dll!GetVersionExA", "KERNEL32.dll!GetVersion", "KERNEL32.dll!GetWindowsDirectoryA",
    "KERNEL32.dll!GlobalMemoryStatus", "KERNEL32.dll!GlobalMemoryStatusEx", "USER32.dll!wsprintfA",
};

// Free recording (free_recorder.h). The state is plain arrays: recording must not allocate.
constexpr size_t kMaxRecordedFrees = 1 << 16;
struct RecordedFree { const void *ptr; size_t size; };
RecordedFree gFrees[kMaxRecordedFrees];
volatile size_t gFreeCount = 0;
volatile bool gFreeOverflow = false;
volatile LONG gRecorderThread = 0; // thread id while recording, else 0
HANDLE gProcessHeap = nullptr;
using HeapFreeFn = BOOL(WINAPI *)(HANDLE, DWORD, LPVOID);
using HeapReAllocFn = LPVOID(WINAPI *)(HANDLE, DWORD, LPVOID, SIZE_T);
HeapFreeFn gRealHeapFree = nullptr;
HeapReAllocFn gRealHeapReAlloc = nullptr;

bool Recording(HANDLE heap)
{
    return heap == gProcessHeap && gRecorderThread != 0 && static_cast<DWORD>(gRecorderThread) == GetCurrentThreadId();
}

void NoteFree(void *p, size_t size)
{
    const size_t n = gFreeCount;
    if (n >= kMaxRecordedFrees) { gFreeOverflow = true; return; }
    gFrees[n] = {p, size};
    gFreeCount = n + 1;
}

// Allocation counting (free_recorder.h, AllocationCounter). Only the counting thread writes the counts, so they need no
// interlocking; the thread id and the pause depth are what other threads read.
constexpr size_t kMaxCountedAllocations = 1 << 16;
const void *gAllocations[kMaxCountedAllocations];
volatile size_t gAllocationCount = 0;
volatile bool gAllocationOverflow = false;
volatile LONG gCounterThread = 0; // thread id while counting, else 0
unsigned gCounterGeneration = 0;  // which counter: a NotCounted resumes only the one it paused
unsigned gCounterPaused = 0;      // NotCounted depth on the counting thread
long long gCountedBlocks = 0, gCountedBytes = 0;
size_t gCountedFrees = 0, gCountedAllocations = 0;

bool Counting(HANDLE heap)
{
    return gCounterThread != 0 && heap == gProcessHeap && gCounterPaused == 0 &&
           static_cast<DWORD>(gCounterThread) == GetCurrentThreadId();
}

// The heap watch (free_recorder.h, HeapWatcher): the watcher, its thread and the NotCounted depth that pauses it there.
const difftest::HeapWatcher *volatile gWatcher = nullptr;
volatile LONG gWatchThread = 0;
unsigned gWatchPaused = 0;
const difftest::HeapWatcher *Watcher()
{
    return gWatchThread != 0 && static_cast<DWORD>(gWatchThread) == GetCurrentThreadId() ? gWatcher : nullptr;
}

// A block handed out: logged for Allocated() (the counts are the callers').
void LogAllocation(const void *p)
{
    ++gCountedAllocations;
    const size_t n = gAllocationCount;
    if (n >= kMaxCountedAllocations) { gAllocationOverflow = true; return; }
    gAllocations[n] = p;
    gAllocationCount = n + 1;
}

BOOL WINAPI CountingHeapFree(HANDLE heap, DWORD flags, LPVOID p)
{
    const bool record = p && Recording(heap), count = p && Counting(heap);
    const size_t size = record || count ? HeapSize(heap, 0, p) : 0;
    if (record) NoteFree(p, size);
    const BOOL ok = gRealHeapFree(heap, flags, p);
    if (ok && p)
        if (const difftest::HeapWatcher *w = Watcher()) w->freed(p);
    if (ok && count) {
        --gCountedBlocks;
        gCountedBytes -= static_cast<long long>(size);
        ++gCountedFrees;
    }
    return ok;
}

LPVOID WINAPI CountingHeapReAlloc(HANDLE heap, DWORD flags, LPVOID p, SIZE_T bytes)
{
    const bool count = p && Counting(heap);
    const size_t old = count ? HeapSize(heap, 0, p) : 0;
    LPVOID q = gRealHeapReAlloc(heap, flags, p, bytes);
    // The old block is freed only when the block moved; its size is gone by now, so record 0 bytes.
    if (q && p && q != p && Recording(heap)) NoteFree(p, 0);
    if (q)
        if (const difftest::HeapWatcher *w = Watcher()) {
            if (q == p) w->resized(q, bytes);
            else {
                if (p) w->freed(p);
                if (!gWatchPaused) w->allocated(q, bytes);
            }
        }
    if (q && count) {
        gCountedBytes += static_cast<long long>(bytes) - static_cast<long long>(old);
        if (q != p) { // a moved block: the old one freed, the new one handed out (the block count stays)
            ++gCountedFrees;
            LogAllocation(q);
        }
    }
    return q;
}

// Heap fill sweep (TS2_HEAP_FILL=1, lane LU): every process-heap block HeapAlloc hands out without HEAP_ZERO_MEMORY is
// filled with a byte that changes from one allocation to the next, both the original's and ours (our CRT's malloc reaches
// HeapAlloc through our IAT). A test that compares bytes nothing wrote (padding, fields a constructor leaves) then differs
// between the sides at once instead of only when the heap's leftovers happen to differ (in the full suite). A sweep, not
// a check: run the suite with it set and read the failures.
using HeapAllocFn = LPVOID(WINAPI *)(HANDLE, DWORD, SIZE_T);
HeapAllocFn gRealHeapAlloc = nullptr;
volatile LONG gFillCounter = 0;
bool gHeapFill = false; // read once by InitRecorderReals: getenv must not run inside the HeapAlloc wrapper

bool HeapFillSweep()
{
    static const bool on = std::getenv("TS2_HEAP_FILL") != nullptr;
    return on;
}

// Every HeapAlloc, the image's and ours: counted for an AllocationCounter, filled for the sweep.
LPVOID WINAPI CountingHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    LPVOID p = gRealHeapAlloc(heap, flags, bytes);
    if (p && heap == gProcessHeap) {
        if (gHeapFill && !(flags & HEAP_ZERO_MEMORY)) {
            const LONG n = InterlockedIncrement(&gFillCounter);
            std::memset(p, 0xa1 + (n % 29) * 2, bytes); // odd bytes 0xa1..0xd9: never a valid pointer's or a small count's
        }
        if (Counting(heap)) {
            ++gCountedBlocks;
            gCountedBytes += static_cast<long long>(bytes);
            LogAllocation(p);
        }
    }
    if (p && !gWatchPaused)
        if (const difftest::HeapWatcher *w = Watcher()) w->allocated(p, bytes);
    return p;
}

void InitRecorderReals()
{
    if (gRealHeapFree) return;
    HMODULE k = GetModuleHandleA("kernel32.dll");
    gRealHeapFree = reinterpret_cast<HeapFreeFn>(GetProcAddress(k, "HeapFree"));
    gRealHeapReAlloc = reinterpret_cast<HeapReAllocFn>(GetProcAddress(k, "HeapReAlloc"));
    gRealHeapAlloc = reinterpret_cast<HeapAllocFn>(GetProcAddress(k, "HeapAlloc"));
    gProcessHeap = GetProcessHeap();
    gHeapFill = HeapFillSweep();
}

void *ResolvePassthrough(const std::string &dll, const std::string &fn)
{
    std::string full = dll + "!" + fn;
    if (_stricmp(full.c_str(), "KERNEL32.dll!HeapFree") == 0) { InitRecorderReals(); return reinterpret_cast<void *>(&CountingHeapFree); }
    if (_stricmp(full.c_str(), "KERNEL32.dll!HeapReAlloc") == 0) { InitRecorderReals(); return reinterpret_cast<void *>(&CountingHeapReAlloc); }
    if (_stricmp(full.c_str(), "KERNEL32.dll!HeapAlloc") == 0) { InitRecorderReals(); return reinterpret_cast<void *>(&CountingHeapAlloc); }
    for (const char *p : kPassthrough) {
        if (_stricmp(p, full.c_str()) == 0) {
            HMODULE m = LoadLibraryA(dll.c_str());
            return m ? reinterpret_cast<void *>(GetProcAddress(m, fn.c_str())) : nullptr;
        }
    }
    return nullptr;
}

// Every import thunk loads its index into ecx and jumps here.
void __fastcall ImportTrap(uint32_t index)
{
    gLastTrapped = index < gImportNames.size() ? gImportNames[index] : "<unknown import>";
    RaiseException(kImportTrapCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

} // namespace

[[noreturn]] void TrapImport(const std::string &what)
{
    gLastTrapped = what;
    RaiseException(kImportTrapCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    __assume(0);
}

// Implicit TLS for the mapped image. The Windows loader only does this for modules it loads, so we
// give the image a TLS index past the loader's ones, put the image's index variable there, and
// extend the thread's TLS pointer array (fs:[0x2c], allocated by the loader from the process heap)
// with a block holding the image's TLS template. Original code then finds its per-thread data (e.g.
// _Init_thread_epoch for function-local statics) as it would in-game.
// When a DLL with TLS loads later, the loader reallocates the array (copying only the entries it
// knows about) and frees ours, so EnsureTls re-adds the slot; tests call it before each test.
namespace {
struct ImageTls {
    void **ourArray = nullptr;    // the array we installed
    void **loaderArray = nullptr; // the loader's latest array (handed back at exit)
    uint8_t *block = nullptr;
    uint32_t index = 0;
    uint32_t *indexVariable = nullptr;
};
ImageTls gTls;

bool InstallTlsArray(std::string &error)
{
    // The loader's per-thread TLS vector is a heap block { Length; previous vector; slots[] } with
    // fs:[0x2c] pointing at the slots. Ours has the same shape, with Length covering our slot, so a
    // loader reallocation (when a DLL with TLS loads) copies our slot along with its own.
    auto **current = reinterpret_cast<void **>(__readfsdword(0x2c));
    if (!current) {
        error = "thread has no TLS vector";
        return false;
    }
    const auto *header = reinterpret_cast<const uintptr_t *>(current) - 2;
    uint32_t count = static_cast<uint32_t>(header[0]);
    if (count > 4096) {
        error = "unexpected TLS vector header";
        return false;
    }
    if (gTls.index < count) gTls.index = count + 16; // clear of indices the loader may hand out
    uint32_t length = gTls.index + 1;
    auto *block = static_cast<uintptr_t *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (2 + length) * sizeof(void *)));
    if (!block) {
        error = "out of memory for TLS";
        return false;
    }
    block[0] = length;
    block[1] = header[1];
    auto **array = reinterpret_cast<void **>(block + 2);
    std::memcpy(array, current, count * sizeof(void *));
    array[gTls.index] = gTls.block;
    *gTls.indexVariable = gTls.index;
    gTls.loaderArray = current;
    gTls.ourArray = array;
    __writefsdword(0x2c, static_cast<DWORD>(reinterpret_cast<uintptr_t>(array)));
    return true;
}
} // namespace

bool OriginalImage::AttachTlsToThisThread(std::string &error) const
{
    auto *dos = At<IMAGE_DOS_HEADER>(0);
    auto *nt = At<IMAGE_NT_HEADERS32>(dos->e_lfanew);
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (!dir.Size) return true;
    auto *tls = At<IMAGE_TLS_DIRECTORY32>(dir.VirtualAddress);
    const auto *start = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(tls->StartAddressOfRawData));
    uint32_t rawSize = tls->EndAddressOfRawData - tls->StartAddressOfRawData;
    uint32_t total = rawSize + tls->SizeOfZeroFill;
    gTls.block = static_cast<uint8_t *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total ? total : 1));
    if (!gTls.block) {
        error = "out of memory for TLS";
        return false;
    }
    std::memcpy(gTls.block, start, rawSize);
    gTls.indexVariable = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(tls->AddressOfIndex));
    return InstallTlsArray(error);
}

bool OriginalImage::EnsureTls(std::string &error) const
{
    if (!gTls.block || reinterpret_cast<void **>(__readfsdword(0x2c)) == gTls.ourArray) return true;
    return InstallTlsArray(error);
}

void OriginalImage::DetachTlsFromThisThread() const
{
    // The loader frees the thread's TLS array at exit with its own bookkeeping: hand back its own
    // latest array (unless it has replaced ours since, in which case the current one is its own).
    if (gTls.ourArray && reinterpret_cast<void **>(__readfsdword(0x2c)) == gTls.ourArray)
        __writefsdword(0x2c, static_cast<DWORD>(reinterpret_cast<uintptr_t>(gTls.loaderArray)));
}

const std::string &OriginalImage::LastTrappedImport()
{
    return gLastTrapped;
}

bool OriginalImage::Load(const std::wstring &path, std::string &error, ImportResolver resolver)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open image";
        return false;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(file.data());
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(file.data() + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE
        || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        error = "not an x86 PE image";
        return false;
    }

    mSize = nt->OptionalHeader.SizeOfImage;
    void *at = reinterpret_cast<void *>(kGameBase);
    mBase = static_cast<uint8_t *>(VirtualAlloc(at, mSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    // A replay process finds the range already reserved for it (record/replay.cpp): commit it.
    if (!mBase) mBase = static_cast<uint8_t *>(VirtualAlloc(at, mSize, MEM_COMMIT, PAGE_READWRITE));
    if (!mBase) {
        error = "cannot map the image at its fixed base 0xa0000000 (kGameBase): the address range is taken";
        return false;
    }

    if (reinterpret_cast<uintptr_t>(mBase) != kGameBase) { // no fallback to another base: our code is linked against kGameBase
        error = "the image was mapped at a base other than kGameBase (0xa0000000)";
        return false;
    }

    std::memcpy(mBase, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto *section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        uint32_t raw = (std::min)(section->SizeOfRawData, section->Misc.VirtualSize);
        std::memcpy(mBase + section->VirtualAddress, file.data() + section->PointerToRawData, raw);
    }

    // Base relocations: only HIGHLOW is used by x86 images.
    const uint32_t delta = static_cast<uint32_t>(Base() - nt->OptionalHeader.ImageBase);
    const auto &relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for (uint32_t off = 0; off < relocDir.Size;) {
        auto *block = At<IMAGE_BASE_RELOCATION>(relocDir.VirtualAddress + off);
        if (block->SizeOfBlock == 0) {
            break;
        }
        auto *entries = reinterpret_cast<uint16_t *>(block + 1);
        unsigned count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
        for (unsigned i = 0; i < count; ++i) {
            if ((entries[i] >> 12) == IMAGE_REL_BASED_HIGHLOW) {
                *At<uint32_t>(block->VirtualAddress + (entries[i] & 0xFFF)) += delta;
            }
        }
        off += block->SizeOfBlock;
    }

    // Imports: count them, then point each IAT slot at its own trap thunk.
    const auto &importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    gImportNames.clear();
    for (auto *d = At<IMAGE_IMPORT_DESCRIPTOR>(importDir.VirtualAddress); d->Name; ++d) {
        auto *names = At<IMAGE_THUNK_DATA32>(d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk);
        for (; names->u1.AddressOfData; ++names) {
            std::string fn = IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)
                ? "#" + std::to_string(IMAGE_ORDINAL32(names->u1.Ordinal))
                : std::string(At<IMAGE_IMPORT_BY_NAME>(names->u1.AddressOfData)->Name);
            gImportNames.push_back(std::string(At<char>(d->Name)) + "!" + fn);
        }
    }

    // Thunk: mov ecx, imm32 ; jmp rel32  (10 bytes)
    mThunks = static_cast<uint8_t *>(
        VirtualAlloc(nullptr, gImportNames.size() * 10, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    uint32_t index = 0;
    for (auto *d = At<IMAGE_IMPORT_DESCRIPTOR>(importDir.VirtualAddress); d->Name; ++d) {
        auto *names = At<IMAGE_THUNK_DATA32>(d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk);
        auto *iat = At<IMAGE_THUNK_DATA32>(d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat, ++index) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal) && resolver) {
                // An ordinal goes to the resolver as GetProcAddress takes it: a pointer below 0x10000.
                const auto *ordinal = reinterpret_cast<const char *>(static_cast<uintptr_t>(IMAGE_ORDINAL32(names->u1.Ordinal)));
                if (void *real = resolver(At<char>(d->Name), ordinal)) {
                    iat->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(real));
                    continue;
                }
            }
            if (!IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) {
                const char *fn = At<IMAGE_IMPORT_BY_NAME>(names->u1.AddressOfData)->Name;
                void *real = resolver ? resolver(At<char>(d->Name), fn) : nullptr;
                if (!real) real = ResolvePassthrough(At<char>(d->Name), fn);
                if (!real) real = SandboxedImport(At<char>(d->Name), fn);
                if (real) {
                    iat->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(real));
                    continue;
                }
            }
            uint8_t *t = mThunks + index * 10;
            t[0] = 0xB9;
            std::memcpy(t + 1, &index, 4);
            t[5] = 0xE9;
            int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&ImportTrap) - reinterpret_cast<uintptr_t>(t + 10));
            std::memcpy(t + 6, &rel, 4);
            iat->u1.Function = static_cast<DWORD>(reinterpret_cast<uintptr_t>(t));
        }
    }

    DWORD old;
    VirtualProtect(mThunks, gImportNames.size() * 10, PAGE_EXECUTE_READ, &old);
    VirtualProtect(mBase, mSize, PAGE_EXECUTE_READWRITE, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    return true;
}

namespace difftest {

// Redirects this exe's own HeapAlloc/HeapFree/HeapReAlloc import entries (our CRT's malloc / free / realloc reach them
// there).
void InstallFreeRecorderHooks()
{
    InitRecorderReals();
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (auto *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; ++d) {
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto *iat = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char *fn = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData)->Name;
            void *wrapper = !std::strcmp(fn, "HeapFree") ? reinterpret_cast<void *>(&CountingHeapFree)
                          : !std::strcmp(fn, "HeapReAlloc") ? reinterpret_cast<void *>(&CountingHeapReAlloc)
                          : !std::strcmp(fn, "HeapAlloc") ? reinterpret_cast<void *>(&CountingHeapAlloc)
                          : nullptr;
            if (!wrapper) continue;
            DWORD old;
            if (VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) {
                iat->u1.Function = reinterpret_cast<ULONG_PTR>(wrapper);
                VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            }
        }
    }
}

// Replaces this exe's own import entry for `name` (any DLL) with `replacement` and returns the previous entry (nullptr
// if the exe doesn't import it). For fakes a test sets on both sides: the image's import slot and ours (our CRT's
// functions call the system through our IAT, e.g. _time64 -> GetSystemTimeAsFileTime).
void *SwapOwnImport(const char *name, void *replacement)
{
    auto *base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; ++d) {
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto *iat = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            if (std::strcmp(reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData)->Name, name)) continue;
            void *previous = reinterpret_cast<void *>(iat->u1.Function);
            DWORD old;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return nullptr;
            iat->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
            VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
            return previous;
        }
    }
    return nullptr;
}

// Buffers a test's own bookkeeping owns (drawfx::gLog): a probe returns the buffer's address; if it differs between the
// window's start and end, the bookkeeping reallocated inside the window and the old block's free was counted as the
// code under test's. Fixed arrays: recording never allocates.
namespace {
constexpr size_t kMaxProbes = 8;
struct BufferProbe {
    const char *name;
    const void *(*probe)();
    const void *atStart;
};
BufferProbe gProbes[kMaxProbes];
size_t gProbeCount;
unsigned gBufferMoves;
const char *gMovedName;
} // namespace

void WatchBufferInRecorders(const char *name, const void *(*probe)())
{
    if (gProbeCount < kMaxProbes) gProbes[gProbeCount++] = {name, probe, nullptr};
}
unsigned TakeBufferMoves(const char **firstName)
{
    const unsigned n = gBufferMoves;
    if (firstName) *firstName = gMovedName;
    gBufferMoves = 0;
    gMovedName = nullptr;
    return n;
}

FreeRecorder::FreeRecorder()
{
    gFreeCount = 0;
    gFreeOverflow = false;
    for (size_t i = 0; i < gProbeCount; ++i) gProbes[i].atStart = gProbes[i].probe();
    InterlockedExchange(&gRecorderThread, static_cast<LONG>(GetCurrentThreadId()));
}
FreeRecorder::~FreeRecorder() { Stop(); }
void FreeRecorder::Stop()
{
    // Stop() may run twice (explicitly, then from the destructor): only the call that ends the window checks.
    if (InterlockedExchange(&gRecorderThread, 0) == 0) return;
    for (size_t i = 0; i < gProbeCount; ++i)
        if (gProbes[i].probe() != gProbes[i].atStart && gBufferMoves++ == 0) gMovedName = gProbes[i].name;
}
size_t FreeRecorder::Count() const { return gFreeCount; }
bool FreeRecorder::Freed(const void *p) const
{
    for (size_t i = 0; i < gFreeCount; ++i) if (gFrees[i].ptr == p) return true;
    return false;
}
const void *FreeRecorder::Pointer(size_t i) const { return i < gFreeCount ? gFrees[i].ptr : nullptr; }
size_t FreeRecorder::Size(size_t i) const { return i < gFreeCount ? gFrees[i].size : 0; }
size_t FreeRecorder::SortedSizes(size_t *out, size_t capacity) const
{
    const size_t total = gFreeCount;
    const size_t n = total < capacity ? total : capacity;
    for (size_t i = 0; i < n; ++i) out[i] = gFrees[i].size;
    std::sort(out, out + n);
    return n;
}
bool FreeRecorder::Overflowed() const { return gFreeOverflow; }

AllocationCounter::AllocationCounter()
{
    if (gCounterThread != 0) std::abort(); // one at a time: a test bug
    gAllocationCount = 0;
    gAllocationOverflow = false;
    gCountedBlocks = gCountedBytes = 0;
    gCountedFrees = gCountedAllocations = 0;
    gCounterPaused = 0;
    if (++gCounterGeneration == 0) gCounterGeneration = 1;
    InterlockedExchange(&gCounterThread, static_cast<LONG>(GetCurrentThreadId()));
}
AllocationCounter::~AllocationCounter() { Stop(); }
void AllocationCounter::Stop() { InterlockedExchange(&gCounterThread, 0); }
long long AllocationCounter::Blocks() const { return gCountedBlocks; }
long long AllocationCounter::Bytes() const { return gCountedBytes; }
size_t AllocationCounter::Allocations() const { return gCountedAllocations; }
size_t AllocationCounter::Frees() const { return gCountedFrees; }
bool AllocationCounter::Allocated(const void *p, size_t upTo) const
{
    const size_t n = gAllocationCount < upTo ? gAllocationCount : upTo;
    for (size_t i = 0; i < n; ++i) if (gAllocations[i] == p) return true;
    return false;
}
bool AllocationCounter::Overflowed() const { return gAllocationOverflow; }

void WatchHeap(const HeapWatcher *watcher)
{
    InterlockedExchange(&gWatchThread, 0);
    gWatcher = watcher;
    gWatchPaused = 0;
    if (watcher) InterlockedExchange(&gWatchThread, static_cast<LONG>(GetCurrentThreadId()));
}
void WatchedAllocation(void *p, size_t bytes)
{
    if (p && !gWatchPaused)
        if (const HeapWatcher *w = Watcher()) w->allocated(p, bytes);
}
void WatchedResize(void *p, size_t bytes)
{
    if (p)
        if (const HeapWatcher *w = Watcher()) w->resized(p, bytes);
}
void WatchedFree(void *p)
{
    if (p)
        if (const HeapWatcher *w = Watcher()) w->freed(p);
}

NotCounted::NotCounted() : generation_(0), watchPaused_(false)
{
    if (Watcher()) watchPaused_ = true, ++gWatchPaused;
    if (gCounterThread == 0 || static_cast<DWORD>(gCounterThread) != GetCurrentThreadId()) return;
    generation_ = gCounterGeneration;
    ++gCounterPaused;
}
NotCounted::~NotCounted()
{
    if (generation_ && generation_ == gCounterGeneration && gCounterPaused) --gCounterPaused;
    if (watchPaused_ && gWatchPaused) --gWatchPaused;
}

} // namespace difftest
