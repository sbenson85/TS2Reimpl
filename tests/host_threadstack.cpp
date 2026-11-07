// Another thread's stack (host/shared/threadstack.h): the lockstep scheduler's stuck report says where each thread is.
// Not a differential test (no original function): rva 0. What the report relies on: a blocked thread's instruction
// pointer is named by module and offset, the return addresses of the calls it is in are found on its stack (the
// innermost first), the call encodings are recognised and other bytes are not, the start address is the routine the
// thread was created with, and the process's thread list includes it.
#include "difftest.h"

#include "../host/shared/threadstack.h"

#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

using namespace threadstack;

HANDLE gGo, gBlocked;
volatile uintptr_t gInner = 0, gOuter = 0; // return addresses of the two calls the thread blocks in

void BlockOuter();
volatile uintptr_t gDecoy = 0; // a code address on the blocked thread's stack that no call returns to

__declspec(noinline) void BlockInner()
{
    volatile uintptr_t decoy = reinterpret_cast<uintptr_t>(&BlockOuter); // a function's entry: not after a call
    gDecoy = decoy;
    gInner = reinterpret_cast<uintptr_t>(_ReturnAddress());
    SetEvent(gBlocked);
    ::WaitForSingleObject(gGo, INFINITE);
    gDecoy = decoy;
}

__declspec(noinline) void BlockOuter()
{
    gOuter = reinterpret_cast<uintptr_t>(_ReturnAddress());
    BlockInner();
    _ReadWriteBarrier(); // not a tail call: BlockInner's return address stays in this function
    gOuter = gOuter + 0;
}

DWORD WINAPI Blocker(void *)
{
    BlockOuter();
    return 0;
}

std::string NameTest(uintptr_t address)
{
    return address == reinterpret_cast<uintptr_t>(&Blocker) ? "Blocker" : "";
}

bool Has(const Snapshot &s, uintptr_t v)
{
    return std::find(s.returns.begin(), s.returns.end(), static_cast<uint32_t>(v)) != s.returns.end();
}

size_t IndexOf(const Snapshot &s, uintptr_t v)
{
    return static_cast<size_t>(std::find(s.returns.begin(), s.returns.end(), static_cast<uint32_t>(v)) - s.returns.begin());
}

} // namespace

DIFF_TEST(HostThreadStack_LooksLikeCall, 0)
{
    struct Case {
        uint8_t bytes[8]; // the 7 bytes before the return address, then one after (never looked at)
        bool call;
        const char *what;
    };
    const Case cases[] = {
        {{0x90, 0x90, 0xe8, 0x10, 0x20, 0x30, 0x40, 0xcc}, true, "call rel32"},
        {{0x90, 0xff, 0x15, 0x10, 0x20, 0x30, 0x40, 0xcc}, true, "call [disp32]"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0xd6, 0xcc}, true, "call esi"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0x10, 0xcc}, true, "call [eax]"},
        {{0x90, 0x90, 0x90, 0x90, 0xff, 0x50, 0x14, 0xcc}, true, "call [eax+0x14]"},
        {{0x90, 0xff, 0x90, 0x00, 0x01, 0x00, 0x00, 0xcc}, true, "call [eax+0x100]"},
        {{0x90, 0x90, 0x90, 0x90, 0xff, 0x14, 0x24, 0xcc}, true, "call [esp]"},
        {{0x90, 0x90, 0x90, 0xff, 0x54, 0x24, 0x08, 0xcc}, true, "call [esp+8]"},
        {{0xff, 0x94, 0x24, 0x00, 0x01, 0x00, 0x00, 0xcc}, true, "call [esp+0x100]"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xcc}, false, "nops"},
        {{0x90, 0x90, 0xe9, 0x10, 0x20, 0x30, 0x40, 0xcc}, false, "jmp rel32"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0xe0, 0xcc}, false, "jmp eax"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0x30, 0xcc}, false, "push [eax]"},
        {{0x90, 0x90, 0x90, 0x90, 0xff, 0x70, 0x14, 0xcc}, false, "push [eax+0x14]"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0x15, 0xcc}, false, "call [disp32] cut short"},
        {{0x90, 0x90, 0x90, 0x90, 0x90, 0xff, 0x14, 0xcc}, false, "call [sib] cut short"},
        {{0x90, 0x90, 0x90, 0x90, 0xff, 0x54, 0x08, 0xcc}, false, "call [sib+disp8] cut short"},
        {{0x90, 0x90, 0x90, 0x90, 0xff, 0x54, 0x14, 0xcc}, false, "call [esp+disp8] as [reg+disp8], rm 4"},
        {{0x90, 0xff, 0x94, 0x00, 0x01, 0x00, 0x00, 0xcc}, false, "call [sib+disp32] as [reg+disp32], rm 4"},
    };
    for (const Case &c : cases) {
        if (LooksLikeCall(c.bytes + 7) != c.call) ctx.Fail("%s: %s", c.what, c.call ? "not recognised" : "taken for a call");
        ++ctx.cases;
    }
}

DIFF_TEST(HostThreadStack_Capture, 0)
{
    gGo = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    gBlocked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD id = 0;
    HANDLE h = CreateThread(nullptr, 0, &Blocker, nullptr, 0, &id);
    ::WaitForSingleObject(gBlocked, 5000);
    ::Sleep(50); // into the kernel wait
    const Snapshot s = Capture(h);
    const uintptr_t start = StartAddress(h);
    const std::vector<DWORD> threads = ProcessThreads();
    const Snapshot self = Capture(GetCurrentThread());
    SetEvent(gGo);
    ::WaitForSingleObject(h, 5000);

    if (!s.ok) ctx.Fail("not captured: %s", s.error.c_str());
    const std::string where = Where(s.eip);
    if (where.rfind("ntdll.dll+0x", 0) != 0) ctx.Fail("eip %s, expected in ntdll (the wait's system call)", where.c_str());
    // Both calls the thread is in, the inner one first; the wait functions' own frames (kernelbase) above them.
    if (!Has(s, gInner) || !Has(s, gOuter)) ctx.Fail("returns lack BlockInner's (%d) or BlockOuter's (%d) call", Has(s, gInner), Has(s, gOuter));
    else if (IndexOf(s, gInner) > IndexOf(s, gOuter)) ctx.Fail("the outer call before the inner one");
    if (Has(s, gDecoy)) ctx.Fail("a function's entry (a code pointer not after a call) taken for a return address");
    if (!s.returns.empty() && Where(s.returns[0]).find(".dll+0x") == std::string::npos)
        ctx.Fail("first return %s, expected a system DLL (the wait function)", Where(s.returns[0]).c_str());
    const std::string text = Describe(s);
    if (text.rfind("eip ntdll.dll+0x", 0) != 0 || text.find("; returns ") == std::string::npos ||
        text.find("difftest.exe+0x") == std::string::npos)
        ctx.Fail("describe: %s", text.c_str());
    ++ctx.cases;

    if (start != reinterpret_cast<uintptr_t>(&Blocker)) ctx.Fail("start %#x, expected %p", static_cast<unsigned>(start), &Blocker);
    if (Where(start, &NameTest) != "Blocker") ctx.Fail("namer not asked first: %s", Where(start, &NameTest).c_str());
    if (std::find(threads.begin(), threads.end(), id) == threads.end() ||
        std::find(threads.begin(), threads.end(), GetCurrentThreadId()) == threads.end())
        ctx.Fail("thread list (%u threads) lacks the blocker or this thread", static_cast<unsigned>(threads.size()));
    if (self.ok || Describe(self).rfind("not captured: ", 0) != 0) ctx.Fail("captured the calling thread");
    char bare[16];
    std::snprintf(bare, sizeof bare, "0x%08x", 0x1000u);
    if (Where(0x1000) != bare) ctx.Fail("unmapped address: %s", Where(0x1000).c_str());
    // A module's address: its file name and the offset from the module's base (not from the page or section).
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const auto f = reinterpret_cast<uintptr_t>(GetProcAddress(kernel, "GetTickCount"));
    char want[48];
    std::snprintf(want, sizeof want, "kernel32.dll+0x%x", static_cast<unsigned>(f - reinterpret_cast<uintptr_t>(kernel)));
    if (_stricmp(Where(f).c_str(), want) != 0) ctx.Fail("GetTickCount: %s, expected %s", Where(f).c_str(), want);
    ctx.cases += 3;
    CloseHandle(h);
    CloseHandle(gGo);
    CloseHandle(gBlocked);
}

namespace {

volatile uintptr_t gSelfInner = 0, gSelfOuter = 0;

// Returns, from a frame whose distance to the callers' return addresses depends on how many arguments the caller
// pushed (variadic: the caller pops them).
// Where the scan starts: 0 here; 1 just above SelfInner's return-address slot; 2 a global (not on the stack: ignored).
int gStartAt = 0;
const void *gStart = nullptr;
uint32_t gNotStack = 0;

__declspec(noinline) std::vector<uint32_t> Middle(size_t max, size_t bytes, int extra, ...)
{
    std::vector<uint32_t> r = Returns(max, bytes, gStart);
    _ReadWriteBarrier();
    return extra < 0 ? std::vector<uint32_t>{} : r;
}

__declspec(noinline) std::vector<uint32_t> SelfInner(size_t max, size_t bytes, bool shift)
{
    volatile uintptr_t decoy = reinterpret_cast<uintptr_t>(&BlockOuter); // a function's entry on this stack
    gSelfInner = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const void *slotAbove = static_cast<const uint8_t *>(_AddressOfReturnAddress()) + 4;
    gStart = gStartAt == 1 ? slotAbove : gStartAt == 2 ? static_cast<const void *>(&gNotStack) : nullptr;
    // One argument more moves the scan's start 4 bytes away from the callers: every slot is scanned, not every other.
    std::vector<uint32_t> r = shift ? Middle(max, bytes, 1, 0) : Middle(max, bytes, 0);
    gDecoy = decoy;
    return r;
}

__declspec(noinline) std::vector<uint32_t> SelfOuter(size_t max, size_t bytes, bool shift = false)
{
    gSelfOuter = reinterpret_cast<uintptr_t>(_ReturnAddress());
    std::vector<uint32_t> r = SelfInner(max, bytes, shift);
    _ReadWriteBarrier(); // not a tail call
    gSelfOuter = gSelfOuter + 0;
    return r;
}

bool HasIn(const std::vector<uint32_t> &v, uintptr_t x)
{
    return std::find(v.begin(), v.end(), static_cast<uint32_t>(x)) != v.end();
}

size_t At(const std::vector<uint32_t> &v, uintptr_t x)
{
    return static_cast<size_t>(std::find(v.begin(), v.end(), static_cast<uint32_t>(x)) - v.begin());
}

} // namespace

// The calling thread's own stack: its callers in order, the inner one first, no function entries; the limits respected.
DIFF_TEST(HostThreadStack_Returns, 0)
{
    const std::vector<uint32_t> r = SelfOuter(32, 16 * 1024);
    if (!HasIn(r, gSelfInner) || !HasIn(r, gSelfOuter))
        ctx.Fail("returns lack SelfInner's (%d) or SelfOuter's (%d) call", HasIn(r, gSelfInner), HasIn(r, gSelfOuter));
    else if (At(r, gSelfInner) > At(r, gSelfOuter)) ctx.Fail("the outer call before the inner one");
    if (HasIn(r, gDecoy)) ctx.Fail("a function's entry taken for a return address");
    ++ctx.cases;
    const std::vector<uint32_t> few = SelfOuter(1, 16 * 1024);
    if (few.size() != 1) ctx.Fail("max 1: %u returns", static_cast<unsigned>(few.size()));
    // Up to the stack's base and no further: a byte limit far past it reads only the stack (no fault).
    const std::vector<uint32_t> all = SelfOuter(4096, 64u << 20);
    if (!HasIn(all, gSelfOuter)) ctx.Fail("a byte limit past the stack's base lost the callers");
    const std::vector<uint32_t> none = SelfOuter(32, 0);
    if (!none.empty()) ctx.Fail("no bytes: %u returns", static_cast<unsigned>(none.size()));
    const std::vector<uint32_t> shifted = SelfOuter(32, 16 * 1024, true);
    if (!HasIn(shifted, gSelfInner) || !HasIn(shifted, gSelfOuter))
        ctx.Fail("a caller missed with the scan 4 bytes further (%d %d)", HasIn(shifted, gSelfInner), HasIn(shifted, gSelfOuter));
    ctx.cases += 4;
    // From a caller's return-address slot: what lies below it (SelfInner's own return) is skipped, the next caller first.
    gStartAt = 1;
    const std::vector<uint32_t> above = SelfOuter(32, 16 * 1024);
    if (above.empty() || above[0] != gSelfOuter || HasIn(above, gSelfInner))
        ctx.Fail("from SelfInner's slot: first %#x (SelfOuter's call %#x), SelfInner's call %s", above.empty() ? 0u : above[0],
                 static_cast<unsigned>(gSelfOuter), HasIn(above, gSelfInner) ? "seen" : "skipped");
    // A start that isn't on this stack above here is ignored.
    gStartAt = 2;
    const std::vector<uint32_t> elsewhere = SelfOuter(32, 16 * 1024);
    if (!HasIn(elsewhere, gSelfInner) || !HasIn(elsewhere, gSelfOuter)) ctx.Fail("a start off the stack was used");
    gStartAt = 0;
    ctx.cases += 2;
}
