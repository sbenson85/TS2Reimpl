// First-chance fault logging for the game process (the host): logs the first faults with
// their address as an image RVA where possible, likely return addresses on the stack, and the
// frame-pointer chain, so a crash leaves a trace in the log. Debug-print notifications are
// skipped; the first few C++ exceptions are logged with their type.
#include "hook/faultlog.h"
#include "hook/log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace {

uintptr_t gImageBase = 0;
uintptr_t gImageEnd = 0;

bool InImage(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= gImageBase && a < gImageEnd;
}

LONG CALLBACK FaultLogger(EXCEPTION_POINTERS *info);

// The decorated type name of the first catchable type of an MSVC C++ exception (x86 ThrowInfo: catchable type
// array at +0xc, its first entry's type descriptor at +4, the name at +8), or "?" if the record is not readable.
const char *ThrownTypeName(const EXCEPTION_RECORD *r)
{
    if (r->NumberParameters < 3) return "?";
    __try {
        auto throwInfo = static_cast<uintptr_t>(r->ExceptionInformation[2]);
        if (!throwInfo) return "(rethrow)";
        auto types = *reinterpret_cast<const uintptr_t *>(throwInfo + 0xc);
        auto first = reinterpret_cast<const uintptr_t *>(types + 4)[0];
        auto descriptor = *reinterpret_cast<const uintptr_t *>(first + 4);
        return reinterpret_cast<const char *>(descriptor + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

// The thrown object's first words, and any of them that points at readable text (an error's message), for the log.
void DescribeThrownObject(const EXCEPTION_RECORD *r, char *out, size_t size)
{
    out[0] = 0;
    if (r->NumberParameters < 2 || !r->ExceptionInformation[1]) return;
    size_t len = 0;
    __try {
        const auto *words = reinterpret_cast<const uintptr_t *>(r->ExceptionInformation[1]);
        for (int i = 0; i < 8 && len < size - 160; ++i) {
            len += sprintf_s(out + len, size - len, " %08x", static_cast<unsigned>(words[i]));
            const auto *text = reinterpret_cast<const char *>(words[i]);
            if (words[i] < 0x10000 || IsBadReadPtr(text, 4)) continue;
            int n = 0;
            while (n < 120 && !IsBadReadPtr(text + n, 1) && text[n] >= 0x20 && text[n] < 0x7f) ++n;
            if (n >= 4 && !IsBadReadPtr(text + n, 1) && text[n] == 0)
                len += sprintf_s(out + len, size - len, "=\"%.*s\"", n, text);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

LONG CALLBACK FaultLogger(EXCEPTION_POINTERS *info)
{
    static LONG logged = 0;
    static LONG cppLogged = 0;
    const auto *r = info->ExceptionRecord;
    // C++ exceptions are usually caught, but an uncaught one ends the game with no other trace: log the first few
    // with their type and stack.
    if (r->ExceptionCode == 0xE06D7363) {
        if (InterlockedIncrement(&cppLogged) > 10) return EXCEPTION_CONTINUE_SEARCH;
        char object[640];
        DescribeThrownObject(r, object, sizeof(object));
        Log("c++ exception %s, thread %lu, object:%s", ThrownTypeName(r), GetCurrentThreadId(), object);
    } else if (r->ExceptionCode == DBG_PRINTEXCEPTION_C || r->ExceptionCode == 0x4001000A /* wide debug print */
               || r->ExceptionCode == 0x406D1388 /* thread naming */ || InterlockedIncrement(&logged) > 20)
        return EXCEPTION_CONTINUE_SEARCH;
    auto at = reinterpret_cast<uintptr_t>(r->ExceptionAddress);
    char where[64];
    if (InImage(r->ExceptionAddress)) sprintf_s(where, "image rva 0x%x", static_cast<unsigned>(at - gImageBase));
    else {
        HMODULE m = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(r->ExceptionAddress), &m);
        wchar_t name[MAX_PATH] = L"?";
        if (m) GetModuleFileNameW(m, name, MAX_PATH);
        const wchar_t *base = wcsrchr(name, L'\\');
        sprintf_s(where, "%ls+0x%x", base ? base + 1 : name, static_cast<unsigned>(at - reinterpret_cast<uintptr_t>(m)));
    }
    Log("exception 0x%08lx at %p (%s), info %p %p, thread %lu", r->ExceptionCode, r->ExceptionAddress, where,
        r->NumberParameters > 0 ? reinterpret_cast<void *>(r->ExceptionInformation[0]) : nullptr,
        r->NumberParameters > 1 ? reinterpret_cast<void *>(r->ExceptionInformation[1]) : nullptr, GetCurrentThreadId());
    // The registers, and what the likely object pointers hold: a call through a bad vtable names no object otherwise.
    {
        const CONTEXT *const c = info->ContextRecord;
        Log("  registers: eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx", c->Eax, c->Ebx,
            c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        const auto readable = [](uintptr_t address) {
            MEMORY_BASIC_INFORMATION m{};
            return VirtualQuery(reinterpret_cast<const void *>(address), &m, sizeof(m)) != 0 && m.State == MEM_COMMIT
                && !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD));
        };
        const auto describe = [&](const char *name, uintptr_t object) {
            if (!readable(object) || !readable(object + 15)) return;
            const auto *words = reinterpret_cast<const uint32_t *>(object);
            Log("  %s -> %08x %08x %08x %08x", name, words[0], words[1], words[2], words[3]);
            const uintptr_t table = words[0];
            if (!readable(table) || !readable(table + 31)) return;
            const auto *slots = reinterpret_cast<const uint32_t *>(table);
            Log("  %s vtable %08x (rva 0x%x): %08x %08x %08x %08x %08x %08x %08x %08x", name, words[0],
                InImage(reinterpret_cast<void *>(table)) ? static_cast<unsigned>(table - gImageBase) : 0u, slots[0], slots[1],
                slots[2], slots[3], slots[4], slots[5], slots[6], slots[7]);
        };
        describe("ecx", c->Ecx);
        describe("esi", c->Esi);
        describe("eax", c->Eax);
        describe("ebx", c->Ebx);
        describe("edi", c->Edi);
        // The top of the stack as it is: the scan below only keeps what looks like a return address.
        if (readable(c->Esp) && readable(c->Esp + 63)) {
            const auto *top = reinterpret_cast<const uint32_t *>(c->Esp);
            for (int i = 0; i < 16; i += 8)
                Log("  esp+%02x: %08x %08x %08x %08x %08x %08x %08x %08x", i * 4, top[i], top[i + 1], top[i + 2], top[i + 3],
                    top[i + 4], top[i + 5], top[i + 6], top[i + 7]);
        }
    }
    // Likely return addresses on the stack: values pointing into the image or into the host.
    HMODULE self = nullptr; // the module this code is in (the host exe)
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&FaultLogger), &self);
    auto selfBase = reinterpret_cast<uintptr_t>(self);
    auto *selfNt = reinterpret_cast<IMAGE_NT_HEADERS32 *>(selfBase + reinterpret_cast<IMAGE_DOS_HEADER *>(self)->e_lfanew);
    uintptr_t selfEnd = selfBase + selfNt->OptionalHeader.SizeOfImage;
    const auto *sp = reinterpret_cast<const uintptr_t *>(info->ContextRecord->Esp);
    char line[2048] = "  stack:";
    size_t len = strlen(line);
    // A return address follows a call: e8 rel32, ff 15 [abs], ff /2 with a register or [reg+disp].
    auto afterCall = [](uintptr_t v) {
        const auto *p = reinterpret_cast<const uint8_t *>(v);
        return p[-5] == 0xE8 || (p[-6] == 0xFF && p[-5] == 0x15) || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10)
            || (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10);
    };
    MEMORY_BASIC_INFORMATION stackInfo{};
    VirtualQuery(sp, &stackInfo, sizeof(stackInfo));
    auto stackTop = reinterpret_cast<uintptr_t>(stackInfo.BaseAddress) + stackInfo.RegionSize;
    for (int i = 0, found = 0; reinterpret_cast<uintptr_t>(sp + i) < stackTop && found < 24 && len < sizeof(line) - 24; ++i) {
        uintptr_t v = sp[i];
        bool code = (InImage(reinterpret_cast<void *>(v)) && InImage(reinterpret_cast<void *>(v - 6)))
                    || (v >= selfBase + 0x1006 && v < selfEnd);
        if (!code || !afterCall(v)) continue;
        if (InImage(reinterpret_cast<void *>(v)))
            len += sprintf_s(line + len, sizeof(line) - len, " i%x", static_cast<unsigned>(v - gImageBase)), ++found;
        else if (v >= selfBase && v < selfEnd)
            len += sprintf_s(line + len, sizeof(line) - len, " h%x", static_cast<unsigned>(v - selfBase)), ++found;
    }
    Log("%s", line);
    // The frame-pointer chain (system DLLs keep frame pointers), each return address as module+offset.
    len = 0;
    line[0] = 0;
    auto ebp = static_cast<uintptr_t>(info->ContextRecord->Ebp);
    for (int depth = 0; depth < 48 && ebp >= reinterpret_cast<uintptr_t>(sp) && ebp < stackTop - 8 && len < sizeof(line) - 64; ++depth) {
        uintptr_t ret = reinterpret_cast<const uintptr_t *>(ebp)[1];
        HMODULE m = nullptr;
        if (InImage(reinterpret_cast<void *>(ret))) {
            len += sprintf_s(line + len, sizeof(line) - len, " image+%x", static_cast<unsigned>(ret - gImageBase));
        } else if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCWSTR>(ret), &m)) {
            wchar_t name[MAX_PATH] = L"?";
            GetModuleFileNameW(m, name, MAX_PATH);
            const wchar_t *b = wcsrchr(name, L'\\');
            len += sprintf_s(line + len, sizeof(line) - len, " %ls+%x", b ? b + 1 : name, static_cast<unsigned>(ret - reinterpret_cast<uintptr_t>(m)));
        } else {
            len += sprintf_s(line + len, sizeof(line) - len, " %p", reinterpret_cast<void *>(ret));
        }
        uintptr_t next = reinterpret_cast<const uintptr_t *>(ebp)[0];
        if (next <= ebp) break;
        ebp = next;
    }
    Log("  frames:%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}


} // namespace

void StartFaultLog(uintptr_t imageBase, uint32_t imageSize)
{
    gImageBase = imageBase;
    gImageEnd = imageBase + imageSize;
    AddVectoredExceptionHandler(1, &FaultLogger);
}