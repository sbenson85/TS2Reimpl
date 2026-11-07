// Another thread's instruction pointer and return addresses: the rules are in threadstack.h.
#include "threadstack.h"

#include <tlhelp32.h>

#include <cstdio>
#include <cstring>

namespace threadstack {

namespace {

constexpr DWORD kReadable = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

// Executable, readable memory around an address (one region remembered: a stack's candidates cluster in a few modules).
struct CodeRegions {
    uintptr_t base = 0, end = 0;
    bool code = false;

    bool IsCode(uintptr_t address)
    {
        if (address < base || address >= end) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(reinterpret_cast<void *>(address), &mbi, sizeof mbi)) {
                base = end = 0;
                return false;
            }
            base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            end = base + mbi.RegionSize;
            code = mbi.State == MEM_COMMIT && (mbi.Protect & kReadable) && !(mbi.Protect & PAGE_GUARD);
        }
        return code;
    }
};

bool LooksLikeCallAt(uint32_t v);

// The candidate return addresses in `size` bytes of stack (a copy, or the live stack), appended to `out` up to `max`.
void Scan(const uint8_t *stack, size_t size, size_t max, std::vector<uint32_t> &out)
{
    CodeRegions regions;
    for (size_t at = 0; at + 4 <= size && out.size() < max; at += 4) {
        uint32_t v;
        std::memcpy(&v, stack + at, 4);
        if (v < 0x10000 + 8 || !regions.IsCode(v) || !regions.IsCode(v - 7)) continue;
        if (LooksLikeCallAt(v)) out.push_back(v);
    }
}

using QueryThread = LONG(NTAPI *)(HANDLE, ULONG, void *, ULONG, ULONG *);
using MappedName = DWORD(WINAPI *)(HANDLE, void *, wchar_t *, DWORD);

} // namespace

bool LooksLikeCall(const uint8_t *p)
{
    if (p[-5] == 0xe8) return true;                                                 // call rel32
    if (p[-6] == 0xff && p[-5] == 0x15) return true;                                // call [disp32]
    if (p[-2] == 0xff && (p[-1] & 0xf8) == 0xd0) return true;                       // call reg
    if (p[-2] == 0xff && (p[-1] & 0xf8) == 0x10 && (p[-1] & 7) != 4 && (p[-1] & 7) != 5) return true; // call [reg]
    if (p[-3] == 0xff && (p[-2] & 0xf8) == 0x50 && (p[-2] & 7) != 4) return true;   // call [reg+disp8]
    if (p[-6] == 0xff && (p[-5] & 0xf8) == 0x90 && (p[-5] & 7) != 4) return true;   // call [reg+disp32]
    if (p[-3] == 0xff && p[-2] == 0x14) return true;                                // call [sib]
    if (p[-4] == 0xff && p[-3] == 0x54) return true;                                // call [sib+disp8]
    if (p[-7] == 0xff && p[-6] == 0x94) return true;                                // call [sib+disp32]
    return false;
}

namespace {
bool LooksLikeCallAt(uint32_t v) { return LooksLikeCall(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(v))); }
} // namespace

Snapshot Capture(HANDLE thread, size_t maxReturns, size_t stackBytes)
{
    Snapshot out;
    if (GetThreadId(thread) == GetCurrentThreadId()) {
        out.error = "the calling thread";
        return out;
    }
    std::vector<uint8_t> copy(stackBytes); // before the suspension: nothing is allocated while the thread is stopped
    out.returns.reserve(maxReturns);
    SIZE_T got = 0;
    CONTEXT c{};
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_SEGMENTS;
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
        char text[64];
        std::snprintf(text, sizeof text, "SuspendThread failed (%lu)", GetLastError());
        out.error = text;
        return out;
    }
    bool context = GetThreadContext(thread, &c) != FALSE;
    uintptr_t stackBase = 0;
    if (context) {
        LDT_ENTRY e{};
        if (GetThreadSelectorEntry(thread, c.SegFs, &e)) {
            const uintptr_t teb = e.BaseLow | static_cast<uintptr_t>(e.HighWord.Bytes.BaseMid) << 16 |
                                  static_cast<uintptr_t>(e.HighWord.Bytes.BaseHi) << 24;
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(teb + offsetof(NT_TIB, StackBase)), &stackBase,
                              sizeof stackBase, nullptr);
        }
        size_t size = stackBytes;
        if (stackBase > c.Esp && stackBase - c.Esp < size) size = stackBase - c.Esp;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(static_cast<uintptr_t>(c.Esp)), copy.data(), size,
                               &got) &&
            GetLastError() != ERROR_PARTIAL_COPY)
            got = 0;
    }
    ResumeThread(thread);
    if (!context) {
        out.error = "GetThreadContext failed";
        return out;
    }
    out.ok = true;
    out.eip = c.Eip;
    out.esp = c.Esp;
    out.ebp = c.Ebp;
    Scan(copy.data(), got, maxReturns, out.returns);
    return out;
}

std::vector<uint32_t> Returns(size_t maxReturns, size_t stackBytes, const void *start)
{
    std::vector<uint32_t> out;
    out.reserve(maxReturns);
    volatile uint32_t here = 0;
    uintptr_t from = reinterpret_cast<uintptr_t>(&here);
    const uintptr_t asked = reinterpret_cast<uintptr_t>(start);
    const uintptr_t base = reinterpret_cast<uintptr_t>(reinterpret_cast<NT_TIB *>(NtCurrentTeb())->StackBase);
    if (asked > from && asked < base) from = asked & ~uintptr_t{3}; // in use by the callers: never below here
    size_t size = stackBytes;
    if (base > from && base - from < size) size = base - from;
    Scan(reinterpret_cast<const uint8_t *>(from), size & ~size_t{3}, maxReturns, out);
    return out;
}

std::string Where(uintptr_t address, Namer namer)
{
    if (namer) {
        std::string name = namer(address);
        if (!name.empty()) return name;
    }
    char text[300];
    MEMORY_BASIC_INFORMATION mbi{};
    static const auto mapped =
        reinterpret_cast<MappedName>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetMappedFileNameW")));
    if (mapped && VirtualQuery(reinterpret_cast<void *>(address), &mbi, sizeof mbi) && mbi.Type == MEM_IMAGE) {
        wchar_t path[MAX_PATH];
        const DWORD n = mapped(GetCurrentProcess(), mbi.AllocationBase, path, MAX_PATH);
        if (n) {
            const wchar_t *name = path;
            for (const wchar_t *p = path; *p; ++p)
                if (*p == L'\\') name = p + 1;
            std::snprintf(text, sizeof text, "%ls+0x%x", name,
                          static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(mbi.AllocationBase)));
            return text;
        }
    }
    std::snprintf(text, sizeof text, "0x%08x", static_cast<unsigned>(address));
    return text;
}

std::string Describe(const Snapshot &snapshot, Namer namer)
{
    if (!snapshot.ok) return "not captured: " + snapshot.error;
    std::string out = "eip " + Where(snapshot.eip, namer) + "; returns";
    if (snapshot.returns.empty()) out += " (none found)";
    for (size_t i = 0; i < snapshot.returns.size(); ++i) out += (i ? ", " : " ") + Where(snapshot.returns[i], namer);
    return out;
}

uintptr_t StartAddress(HANDLE thread)
{
    static const auto query =
        reinterpret_cast<QueryThread>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
    uintptr_t start = 0;
    if (!query || query(thread, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof start, nullptr) < 0) return 0;
    return start;
}

std::vector<DWORD> ProcessThreads()
{
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    THREADENTRY32 t{};
    t.dwSize = sizeof t;
    const DWORD self = GetCurrentProcessId();
    for (BOOL more = Thread32First(snap, &t); more; more = Thread32Next(snap, &t))
        if (t.th32OwnerProcessID == self) out.push_back(t.th32ThreadID);
    CloseHandle(snap);
    return out;
}

} // namespace threadstack
