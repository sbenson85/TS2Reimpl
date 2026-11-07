// Our C runtime as the frame clock sees it (host/shared/ourcrt.h; TS2HOST_FRAMECLOCK, host/control_frameclock.cpp), in
// the runner, which links the same static UCRT as the host. Not differential for the probes (rva 0): the thread start
// routine found is the one a real _beginthreadex thread starts at, and the clock() start count found is where the
// runner's clock() starts counting. HostOurCrt_ClockMatchesImage is differential: the image CRT's clock() (0x83a46e) fed
// a counter through the image's import slot, against our clock() fed ClockCounter's answer through the runner's import,
// for the same counters and image start counts.
#include "difftest.h"

#include "../host/shared/ourcrt.h"
#include "../host/shared/threadstack.h"

#include "game/game.h"

#include <windows.h>
#include <intrin.h>
#include <process.h>

#include <cerrno>
#include <climits>
#include <cstdint>
#include <ctime>

namespace {

unsigned __stdcall Returns5(void *)
{
    return 5;
}

// What the runner's import slot holds (read by an exchange with the real function, then put back): the probes leave
// each slot as they found it.
void *ImportSlot(const char *name)
{
    void *real = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name));
    void *was = nullptr;
    if (!ourcrt::PatchImport(GetModuleHandleW(nullptr), "KERNEL32.dll", name, real, &was)) return nullptr;
    ourcrt::PatchImport(GetModuleHandleW(nullptr), "KERNEL32.dll", name, was);
    return was;
}

// Our clock() with the runner's QueryPerformanceCounter import answering `counter` (this thread only, during the call).
thread_local bool tFeeding = false;
thread_local int64_t tFeed = 0;
thread_local const void *tFedFrom = nullptr;

BOOL WINAPI FeedQpc(LARGE_INTEGER *out)
{
    if (!tFeeding) {
        using Qpc = decltype(&::QueryPerformanceCounter);
        static const Qpc real = reinterpret_cast<Qpc>(
            reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "QueryPerformanceCounter")));
        return real(out);
    }
    tFedFrom = _ReturnAddress();
    out->QuadPart = tFeed;
    return TRUE;
}

struct OurClock {
    void *previous = nullptr;
    bool patched;
    OurClock()
    {
        patched = ourcrt::PatchImport(GetModuleHandleW(nullptr), "KERNEL32.dll", "QueryPerformanceCounter",
                                      reinterpret_cast<void *>(&FeedQpc), &previous);
    }
    ~OurClock()
    {
        if (patched) ourcrt::PatchImport(GetModuleHandleW(nullptr), "KERNEL32.dll", "QueryPerformanceCounter", previous);
    }
    long At(int64_t counter)
    {
        tFeeding = true;
        tFeed = counter;
        const long c = std::clock();
        tFeeding = false;
        return c;
    }
};

// The image CRT's clock() (0x83a46e) with its import slot answering a counter, its start count and frequency (0x10dfcd0,
// 0x10dfcc8) set; all three restored after.
constexpr uint32_t kImageClock = 0x83a46e, kImageQpcSlot = 0xd9a24c, kImageClockStart = 0x10dfcd0,
                   kImageClockFrequency = 0x10dfcc8;
int64_t gImageFeed = 0;

BOOL WINAPI ImageFeedQpc(LARGE_INTEGER *out)
{
    out->QuadPart = gImageFeed;
    return TRUE;
}

struct ImageClock {
    void **slot = GameAddress<void *>(kImageQpcSlot);
    int64_t *start = GameAddress<int64_t>(kImageClockStart);
    int64_t *frequency = GameAddress<int64_t>(kImageClockFrequency);
    void *savedSlot;
    int64_t savedStart, savedFrequency;
    DWORD protect = 0;
    ImageClock(int64_t frequencyNow)
    {
        VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protect);
        savedSlot = *slot;
        savedStart = *start;
        savedFrequency = *frequency;
        *slot = reinterpret_cast<void *>(&ImageFeedQpc);
        *frequency = frequencyNow;
    }
    ~ImageClock()
    {
        *slot = savedSlot;
        *start = savedStart;
        *frequency = savedFrequency;
        DWORD old;
        VirtualProtect(slot, sizeof(void *), protect, &old);
    }
    long At(int64_t imageStart, int64_t counter)
    {
        *start = imageStart;
        gImageFeed = counter;
        return GameAddress<long(__cdecl)()>(kImageClock)();
    }
};

} // namespace

DIFF_TEST(HostOurCrt_ThreadStart, 0)
{
    // Found by a _beginthreadex that makes no thread, errno and the last error kept.
    void *const slot = ImportSlot("CreateThread");
    errno = EDOM;
    SetLastError(4321);
    const void *start = ourcrt::FindThreadStart(GetModuleHandleW(nullptr));
    if (errno != EDOM || GetLastError() != 4321) ctx.Fail("errno %d, last error %lu after the probe", errno, GetLastError());
    if (!slot || ImportSlot("CreateThread") != slot) ctx.Fail("the CreateThread import was not put back");
    if (!start) {
        ctx.Fail("no thread start routine found");
        return;
    }
    // A real _beginthreadex thread starts there, with its parameter starting {procedure, context}.
    for (int i = 0; i < 3; ++i) {
        unsigned id = 0;
        auto h = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &Returns5, &ctx, CREATE_SUSPENDED, &id));
        if (!h) {
            ctx.Fail("_beginthreadex failed");
            return;
        }
        const uintptr_t at = threadstack::StartAddress(h);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_INTEGER;
        GetThreadContext(h, &c); // a new thread's ebx holds its parameter (RtlUserThreadStart's second argument)
        const auto *param = reinterpret_cast<void *const *>(static_cast<uintptr_t>(c.Ebx));
        if (at != reinterpret_cast<uintptr_t>(start))
            ctx.Fail("thread %d starts at %#x, the probe found %p", i, static_cast<unsigned>(at), start);
        if (param && (param[0] != reinterpret_cast<void *>(&Returns5) || param[1] != &ctx))
            ctx.Fail("thread %d: parameter {%p, %p}", i, param[0], param[1]);
        ResumeThread(h);
        WaitForSingleObject(h, 5000);
        DWORD code = 0;
        GetExitCodeThread(h, &code);
        if (code != 5) ctx.Fail("thread %d: exit code %lu", i, code);
        CloseHandle(h);
        ++ctx.cases;
    }
    // Found again the same.
    if (ourcrt::FindThreadStart(GetModuleHandleW(nullptr)) != start) ctx.Fail("a second probe found another routine");
    ++ctx.cases;
}

DIFF_TEST(HostOurCrt_ClockStart, 0)
{
    void *const slot = ImportSlot("QueryPerformanceCounter");
    const ourcrt::ClockStart found = ourcrt::FindClockStart(GetModuleHandleW(nullptr));
    if (!slot || ImportSlot("QueryPerformanceCounter") != slot) ctx.Fail("the QueryPerformanceCounter import was not put back");
    if (!found.found || !found.site) {
        ctx.Fail("clock()'s start count not found (%u calls)", found.calls);
        return;
    }
    if (found.calls > 70) ctx.Fail("%u clock() calls", found.calls);
    LARGE_INTEGER f, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&now);
    if (found.start <= 0 || found.start > now.QuadPart) ctx.Fail("start count %lld, now %lld", found.start, now.QuadPart);
    OurClock clock;
    if (!clock.patched) {
        ctx.Fail("no QueryPerformanceCounter import");
        return;
    }
    // clock() is -1 just below the start count and counts milliseconds from it, read from the site found.
    struct Case {
        int64_t offset;
        long want;
    };
    const int64_t q = f.QuadPart;
    const Case cases[] = {{-1, -1}, {-q, -1}, {0, 0}, {1, 0}, {q / 1000 - 1, 0}, {q / 1000 + q % 1000, 1},
                          {q * 7 / 2, 3500}, {q * 100 + q / 4, 100'250}};
    for (const Case &k : cases) {
        tFedFrom = nullptr;
        const long got = clock.At(found.start + k.offset);
        if (got != k.want) ctx.Fail("clock() at start%+lld: %ld, want %ld", k.offset, got, k.want);
        if (tFedFrom != found.site) ctx.Fail("clock() read the counter from %p, the probe found %p", tFedFrom, found.site);
        ++ctx.cases;
    }
    // A second search finds the same.
    const ourcrt::ClockStart again = ourcrt::FindClockStart(GetModuleHandleW(nullptr));
    if (!again.found || again.start != found.start || again.site != found.site) ctx.Fail("a second search found another start");
    ++ctx.cases;
}

DIFF_TEST(HostOurCrt_ClockMatchesImage, 0x83a46e)
{
    const ourcrt::ClockStart ours = ourcrt::FindClockStart(GetModuleHandleW(nullptr));
    if (!ours.found) {
        ctx.Fail("clock()'s start count not found");
        return;
    }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    const int64_t q = f.QuadPart;
    OurClock our;
    ImageClock image(q);
    // Image start counts: the frame clock's tick 0 (one day, as the virtual counter has it), others, and -1 (the image
    // CRT's start-up read failed).
    const int64_t starts[] = {86'400 * q, 86'400 * q + 12'345, 3 * q + 1, -1};
    // Counters from the image start: below it, at it, around millisecond edges, far on, and past LONG_MAX ms (-1).
    const int64_t offsets[] = {-q, -1, 0, 1, q / 1000 - 1, q / 1000, q / 1000 + 1, q * 7 / 2 + 3, q * 3600 + q / 7,
                               (static_cast<int64_t>(LONG_MAX) / 1000 + 1) * q, static_cast<int64_t>(LONG_MAX) / 1000 * q};
    for (int64_t start : starts) {
        for (int64_t offset : offsets) {
            const int64_t base = start == -1 ? 86'400 * q : start;
            const int64_t counter = base + offset;
            const long want = image.At(start, counter);
            const long got = our.At(ourcrt::ClockCounter(ours, counter, start));
            if (got != want)
                ctx.Fail("image start %lld, counter start%+lld: ours %ld, the image's %ld", start, offset, got, want);
            ++ctx.cases;
        }
    }
}
