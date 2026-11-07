// The x87 control word probe (TS2HOST_X87PROBE; lane PJ, notes/plan.md "x87"): a diagnostic like host/control.cpp that
// changes nothing the game computes. The game creates its Direct3D 9 device without D3DCREATE_FPU_PRESERVE, so D3D9 is
// expected to set 24-bit precision on that thread; the differential tests run at 53-bit unless told otherwise
// (TS2_X87_PC, docs/lane/test.md "x87 precision"). This logs what the game's threads actually run at:
//   - once at start-up, on the thread that runs the image's entry point;
//   - at every framework tick on the main thread, when its control word differs from the last one logged (so the
//     change made by the device creation shows with its frame);
//   - every second, every thread of the process (suspended for GetThreadContext), when its control word differs from
//     the last one logged for it, with the thread's start address;
//   - at the entry of a few hooked replacements that the game's x87 code calls (Dot87 0x18fbf0 and others below; more
//     with TS2HOST_X87PROBE=<rva>,<rva>...), once per thread and control word, with the caller's return address.
// Every line starts "x87probe: "; "pc=24" / "pc=53" / "pc=64" is the precision control.
#include "control.h"

#include "hook/log.h"

#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

bool gEnabled = false;
uintptr_t gImageBase = 0, gImageEnd = 0;
DWORD gMainThread = 0;
uint16_t gMainLogged = 0xffff; // the main thread's control word last logged at a tick

uint16_t ControlWord()
{
    uint16_t cw;
    __asm fnstcw cw
    return cw;
}

const char *Precision(uint32_t cw)
{
    switch ((cw >> 8) & 3) {
    case 0: return "24";
    case 2: return "53";
    case 3: return "64";
    default: return "reserved";
    }
}

const char *Rounding(uint32_t cw)
{
    static const char *const names[4] = {"nearest", "down", "up", "zero"};
    return names[(cw >> 10) & 3];
}

// Where a code address is: "image+0x<rva>" in the game's image, else "<module>+0x<offset>", else the address.
void Describe(uintptr_t at, char *out, size_t size)
{
    if (at >= gImageBase && at < gImageEnd) {
        _snprintf_s(out, size, _TRUNCATE, "image+0x%x", static_cast<unsigned>(at - gImageBase));
        return;
    }
    HMODULE module = nullptr;
    if (at && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<const char *>(at), &module)) {
        char path[MAX_PATH];
        GetModuleFileNameA(module, path, MAX_PATH);
        const char *leaf = std::strrchr(path, '\\');
        _snprintf_s(out, size, _TRUNCATE, "%s+0x%x", leaf ? leaf + 1 : path,
                    static_cast<unsigned>(at - reinterpret_cast<uintptr_t>(module)));
        return;
    }
    _snprintf_s(out, size, _TRUNCATE, "0x%08x", static_cast<unsigned>(at));
}

uintptr_t ThreadStart(HANDLE thread)
{
    using QueryFn = LONG(NTAPI *)(HANDLE, ULONG, void *, ULONG, ULONG *);
    static const QueryFn query =
        reinterpret_cast<QueryFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    uintptr_t start = 0;
    if (query) query(thread, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), nullptr);
    return start;
}

// ---- entry probes -----------------------------------------------------------------------------------------------

struct Site {
    uint32_t rva;
    const char *name;
    std::atomic<uint64_t> seen[32]; // (thread id << 16 | control word) + 1 for each pair logged, 0 = free
};

constexpr struct {
    uint32_t rva;
    const char *name;
} kDefaultSites[] = {
    {0x18fbf0, "Dot87"},                         // the x87 dot product, called by ~50 original functions (audio too)
    {0x18ff90, "Cuboid::BoxInFrame"},            // its first callers of ours
    {0x19d730, "BoundBox::Transform"},
    {0x21a1d5, "cTSAudioSoundManager::GetCategoryVolume"}, // audio: an x87 product in double
};

std::vector<Site *> gSites;

void __cdecl OnEntry(uint32_t index, uintptr_t returnAddress)
{
    Site &site = *gSites[index];
    const uint16_t cw = ControlWord();
    const DWORD tid = GetCurrentThreadId();
    const uint64_t key = (static_cast<uint64_t>(tid) << 16 | cw) + 1; // never 0
    for (auto &slot : site.seen) {
        uint64_t v = slot.load(std::memory_order_relaxed);
        if (v == key) return;
        if (v == 0 && slot.compare_exchange_strong(v, key)) {
            char from[MAX_PATH + 32];
            Describe(returnAddress, from, sizeof(from));
            Log("x87probe: entry %s 0x%x: thread %lu%s cw=%04x pc=%s rc=%s frame %u, called from %s", site.name, site.rva,
                tid, tid == gMainThread ? " (main)" : "", cw, Precision(cw), Rounding(cw), ControlFrame(), from);
            return;
        } // taken by another thread meanwhile: try the next slot
    }
}

// A thunk in front of the hook's jump: saves every register, calls OnEntry(index, return address), restores them and
// goes on to the replacement. Only hooked sites (a jmp rel32 at the RVA) are probed: nothing of the original is moved.
bool ProbeSite(uint32_t rva, const char *name)
{
    auto *at = reinterpret_cast<uint8_t *>(gImageBase + rva);
    if (at[0] != 0xE9) {
        Log("x87probe: 0x%x (%s) is not hooked; not probed", rva, name);
        return false;
    }
    int32_t rel;
    std::memcpy(&rel, at + 1, 4);
    const uint8_t *target = at + 5 + rel;
    auto *site = new Site{rva, name, {}};
    const uint32_t index = static_cast<uint32_t>(gSites.size());
    gSites.push_back(site);

    auto *code = static_cast<uint8_t *>(VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!code) return false;
    uint8_t *p = code;
    auto rel32 = [](uint8_t *field, const void *to) {
        const int32_t r = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(field + 4));
        std::memcpy(field, &r, 4);
    };
    *p++ = 0x60;                         // pushad
    *p++ = 0x9C;                         // pushfd
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x24; // push dword [esp+0x24]: the return address (after 9 dwords)
    *p++ = 0x68;                         // push index
    std::memcpy(p, &index, 4);
    p += 4;
    *p++ = 0xE8;                         // call OnEntry
    rel32(p, reinterpret_cast<const void *>(&OnEntry));
    p += 4;
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x08; // add esp, 8
    *p++ = 0x9D;                         // popfd
    *p++ = 0x61;                         // popad
    *p++ = 0xE9;                         // jmp replacement
    rel32(p, target);
    DWORD old;
    VirtualProtect(code, 64, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), code, 64);
    VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
    rel32(at + 1, code);
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
    return true;
}

// ---- thread sampler ---------------------------------------------------------------------------------------------

DWORD WINAPI Sampler(void *)
{
    struct Known {
        DWORD tid;
        uint16_t cw;
    };
    std::vector<Known> known;
    const DWORD self = GetCurrentThreadId();
    const DWORD pid = GetCurrentProcessId();
    for (unsigned round = 0;; ++round) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te{sizeof(te)};
            for (BOOL more = Thread32First(snapshot, &te); more; more = Thread32Next(snapshot, &te)) {
                if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
                HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                      te.th32ThreadID);
                if (!t) continue;
                CONTEXT c{};
                c.ContextFlags = CONTEXT_FLOATING_POINT;
                bool got = false;
                if (SuspendThread(t) != static_cast<DWORD>(-1)) {
                    got = GetThreadContext(t, &c) != 0;
                    ResumeThread(t);
                }
                if (got) {
                    const uint16_t cw = static_cast<uint16_t>(c.FloatSave.ControlWord);
                    Known *entry = nullptr;
                    for (Known &k : known)
                        if (k.tid == te.th32ThreadID) entry = &k;
                    const bool fresh = !entry || entry->cw != cw;
                    if (entry) entry->cw = cw;
                    else known.push_back({te.th32ThreadID, cw});
                    if (fresh) {
                        char start[MAX_PATH + 32];
                        Describe(ThreadStart(t), start, sizeof(start));
                        Log("x87probe: sample %u: thread %lu%s (start %s) cw=%04x pc=%s rc=%s frame %u", round,
                            te.th32ThreadID, te.th32ThreadID == gMainThread ? " (main)" : "", start, cw, Precision(cw),
                            Rounding(cw), ControlFrame());
                    }
                }
                CloseHandle(t);
            }
            CloseHandle(snapshot);
        }
        Sleep(1000);
    }
}

} // namespace

void StartX87Probe(uintptr_t imageBase, uint32_t imageSize)
{
    char value[512];
    const DWORD n = GetEnvironmentVariableA("TS2HOST_X87PROBE", value, sizeof(value));
    if (!n || n >= sizeof(value)) return;
    gEnabled = true;
    gImageBase = imageBase;
    gImageEnd = imageBase + imageSize;
    gMainThread = GetCurrentThreadId();
    const uint16_t cw = ControlWord();
    gMainLogged = cw;
    Log("x87probe: start: thread %lu (main) cw=%04x pc=%s rc=%s", gMainThread, cw, Precision(cw), Rounding(cw));
    unsigned probed = 0;
    for (const auto &s : kDefaultSites) probed += ProbeSite(s.rva, s.name);
    // TS2HOST_X87PROBE=<rva>,<rva>...: more hooked sites (hex); "1" or "on": the defaults only.
    for (char *p = value; *p;) {
        char *end = nullptr;
        const unsigned long rva = std::strtoul(p, &end, 16);
        if (end == p) {
            ++p;
            continue;
        }
        if (rva > 1 && rva < imageSize) probed += ProbeSite(static_cast<uint32_t>(rva), "requested");
        p = end;
    }
    Log("x87probe: %u entry probes; sampling every thread each second", probed);
    if (HANDLE t = CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr)) CloseHandle(t);
}

void X87ProbeOnFrame(uint32_t frame)
{
    if (!gEnabled) return;
    const uint16_t cw = ControlWord();
    if (cw == gMainLogged) return;
    Log("x87probe: frame %u: thread %lu%s cw=%04x pc=%s rc=%s (was %04x)", frame, GetCurrentThreadId(),
        GetCurrentThreadId() == gMainThread ? " (main)" : "", cw, Precision(cw), Rounding(cw), gMainLogged);
    gMainLogged = cw;
}
