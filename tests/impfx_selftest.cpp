// impfx's own checks (tests/impfx.h): the import lookup against IAT slots the original's code calls through, each
// kind's state machine through the image's import slots, the CRT stand-ins on both sides, the log's text, and one
// original function (the SleepEx helper 0x1b9f95) under the fake.
#include "difftest.h"
#include "impfx.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

// Calls an import the way the original's code does: through the image's IAT slot.
template <class Fn> Fn Slot(const char *name) { return *GameAddress<Fn>(impfx::FindImport(name)); }

uint32_t gRoutineCalls = 0;
uint32_t __stdcall Routine(uint32_t param)
{
    ++gRoutineCalls;
    return param * 2;
}
uint32_t gApcData = 0;
void __stdcall Apc(uint32_t data) { gApcData += data; }
uint32_t __stdcall SleepingRoutine(uint32_t)
{
    // Inside a fake thread: an alertable SleepEx runs the APCs queued to it.
    return Slot<DWORD(WINAPI *)(DWORD, BOOL)>("SleepEx")(5, TRUE);
}
uint32_t __stdcall EndingRoutine(uint32_t code)
{
    _endthreadex(code + 1);
    return 0xdead;
}

void Expect(difftest::Context &ctx, const char *what, uint32_t got, uint32_t want)
{
    ++ctx.cases;
    if (got != want) ctx.Fail("%s: %#x, expected %#x", what, got, want);
}

} // namespace

DIFF_TEST(Impfx_SelfTest, 0x1b9f95)
{
    // The lookup: slots the original's own calls read (cRZThread / cRZThreadSignal / cRZMutex disassembly).
    struct Known {
        const char *name;
        uint32_t rva;
    } known[] = {{"WaitForSingleObject", 0xd9a184}, {"CloseHandle", 0xd9a17c}, {"CreateMutexA", 0xd9a3ec},
                 {"ReleaseMutex", 0xd9a3f0},        {"CreateEventA", 0xd9a244}, {"SleepEx", 0xd9a188},
                 {"SuspendThread", 0xd9a1a4},       {"ResumeThread", 0xd9a1a8}, {"QueueUserAPC", 0xd9a18c},
                 {"GetCurrentThreadId", 0xd9a194}, {"SetThreadPriority", 0xd9a198}, {"GetThreadPriority", 0xd9a19c},
                 {"GetExitCodeThread", 0xd9a1a0},  {"PulseEvent", 0xd9a3dc}, {"SetEvent", 0xd9a10c}, {"ResetEvent", 0xd9a3d8}};
    for (const Known &k : known) Expect(ctx, k.name, impfx::FindImport(k.name), k.rva);
    Expect(ctx, "not imported", impfx::FindImport("NoSuchImport"), 0);
    Expect(ctx, "wrong dll", impfx::FindImport("SetEvent", "user32.dll"), 0);
    Expect(ctx, "dll given", impfx::FindImport("SetEvent", "KERNEL32.dll"), 0xd9a10c);

    void *const savedSleepEx = *GameAddress<void *>(0xd9a188);
    {
        impfx::Imports imp(ctx);
        imp.Threads();
        imp.Sync();
        imp.Clock();
        imp.Libraries();
        imp.CrtThreads();
        imp.Module("codec.dll");
        imp.Export("codec.dll", "Decode", 2).Returns(77);
        IMPFX_ADD(imp, SetCursor).Returns(0x5c000010);
        Expect(ctx, "patched", uint32_t(*GameAddress<void *>(0xd9a188) != savedSleepEx), 1);

        // Threads: inline run at creation, exit code, waits, suspend / resume, priorities, APCs.
        imp.RunThreads(impfx::Run::Inline);
        imp.BeginSide();
        drawfx::ResetAll();
        gRoutineCalls = 0;
        auto create = Slot<decltype(&::CreateThread)>("CreateThread");
        DWORD id = 0;
        HANDLE t1 = create(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(&Routine), reinterpret_cast<void *>(21), 0, &id);
        Expect(ctx, "first handle", uint32_t(uintptr_t(t1)), impfx::kFirstHandle);
        Expect(ctx, "first id", id, 0x7101);
        Expect(ctx, "ran inline", gRoutineCalls, 1);
        DWORD code = 0;
        Expect(ctx, "exit code ok", Slot<decltype(&::GetExitCodeThread)>("GetExitCodeThread")(t1, &code), TRUE);
        Expect(ctx, "exit code", code, 42);
        Expect(ctx, "wait ran thread", Slot<decltype(&::WaitForSingleObject)>("WaitForSingleObject")(t1, INFINITE), WAIT_OBJECT_0);
        HANDLE t2 = create(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(&Routine), reinterpret_cast<void *>(1), CREATE_SUSPENDED, nullptr);
        Expect(ctx, "suspended not run", gRoutineCalls, 1);
        Slot<decltype(&::GetExitCodeThread)>("GetExitCodeThread")(t2, &code);
        Expect(ctx, "still active", code, STILL_ACTIVE);
        Expect(ctx, "set priority", Slot<decltype(&::SetThreadPriority)>("SetThreadPriority")(t2, 2), TRUE);
        Expect(ctx, "get priority", Slot<decltype(&::GetThreadPriority)>("GetThreadPriority")(t2), 2);
        Expect(ctx, "suspend again", Slot<decltype(&::SuspendThread)>("SuspendThread")(t2), 1);
        Expect(ctx, "resume to 1", Slot<decltype(&::ResumeThread)>("ResumeThread")(t2), 2);
        Expect(ctx, "resume to 0", Slot<decltype(&::ResumeThread)>("ResumeThread")(t2), 1);
        Expect(ctx, "resumed ran", gRoutineCalls, 2);
        Expect(ctx, "resume unknown", Slot<decltype(&::ResumeThread)>("ResumeThread")(reinterpret_cast<HANDLE>(0x1234)), 0xffffffff);
        HANDLE t3 = create(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(&SleepingRoutine), nullptr, CREATE_SUSPENDED, nullptr);
        gApcData = 0;
        auto queue = Slot<decltype(&::QueueUserAPC)>("QueueUserAPC");
        Expect(ctx, "apc queued", queue(reinterpret_cast<PAPCFUNC>(&Apc), t3, 5), 1);
        queue(reinterpret_cast<PAPCFUNC>(&Apc), t3, 7);
        Slot<decltype(&::ResumeThread)>("ResumeThread")(t3);
        Expect(ctx, "apcs ran", gApcData, 12);
        Slot<decltype(&::GetExitCodeThread)>("GetExitCodeThread")(t3, &code);
        Expect(ctx, "alertable sleep result", code, WAIT_IO_COMPLETION);
        Expect(ctx, "apc to finished thread", queue(reinterpret_cast<PAPCFUNC>(&Apc), t3, 1), 0);
        Expect(ctx, "current thread", Slot<decltype(&::GetCurrentThreadId)>("GetCurrentThreadId")(), impfx::kMainThreadId);

        // CRT threads, both sides' _beginthreadex / _endthreadex.
        using BeginFn = uintptr_t(__cdecl *)(void *, unsigned, unsigned(__stdcall *)(void *), void *, unsigned, unsigned *);
        unsigned tid = 0;
        const uintptr_t c1 = ctx.Original<BeginFn>(impfx::Imports::kBeginThreadEx)(
            nullptr, 0, reinterpret_cast<unsigned(__stdcall *)(void *)>(&EndingRoutine), reinterpret_cast<void *>(9), 0, &tid);
        const uintptr_t c2 = _beginthreadex(nullptr, 0, reinterpret_cast<unsigned(__stdcall *)(void *)>(&EndingRoutine),
                                            reinterpret_cast<void *>(19), 0, &tid);
        Slot<decltype(&::GetExitCodeThread)>("GetExitCodeThread")(reinterpret_cast<HANDLE>(c1), &code);
        Expect(ctx, "image _endthreadex code", code, 10);
        Slot<decltype(&::GetExitCodeThread)>("GetExitCodeThread")(reinterpret_cast<HANDLE>(c2), &code);
        Expect(ctx, "our _endthreadex code", code, 20);

        // Sync: auto and manual events, pulse, mutex ownership, semaphore counts, multiple waits, close.
        auto wait = Slot<decltype(&::WaitForSingleObject)>("WaitForSingleObject");
        auto event = Slot<decltype(&::CreateEventA)>("CreateEventA");
        HANDLE autoEvent = event(nullptr, FALSE, TRUE, "name");
        HANDLE manualEvent = event(nullptr, TRUE, FALSE, nullptr);
        Expect(ctx, "auto signalled", wait(autoEvent, 0), WAIT_OBJECT_0);
        Expect(ctx, "auto reset by wait", wait(autoEvent, 10), WAIT_TIMEOUT);
        Slot<decltype(&::SetEvent)>("SetEvent")(manualEvent);
        Expect(ctx, "manual stays 1", wait(manualEvent, 0), WAIT_OBJECT_0);
        Expect(ctx, "manual stays 2", wait(manualEvent, 0), WAIT_OBJECT_0);
        Slot<decltype(&::PulseEvent)>("PulseEvent")(manualEvent);
        Expect(ctx, "pulse leaves unsignalled", wait(manualEvent, 0), WAIT_TIMEOUT);
        HANDLE handles[2] = {manualEvent, autoEvent};
        Slot<decltype(&::SetEvent)>("SetEvent")(autoEvent);
        Expect(ctx, "wait any", Slot<decltype(&::WaitForMultipleObjects)>("WaitForMultipleObjects")(2, handles, FALSE, 0), WAIT_OBJECT_0 + 1);
        HANDLE mutex = Slot<decltype(&::CreateMutexA)>("CreateMutexA")(nullptr, FALSE, nullptr);
        Expect(ctx, "unowned release", Slot<decltype(&::ReleaseMutex)>("ReleaseMutex")(mutex), FALSE);
        Expect(ctx, "mutex wait", wait(mutex, 0), WAIT_OBJECT_0);
        Expect(ctx, "owned release", Slot<decltype(&::ReleaseMutex)>("ReleaseMutex")(mutex), TRUE);
        Expect(ctx, "close", Slot<decltype(&::CloseHandle)>("CloseHandle")(mutex), TRUE);
        Expect(ctx, "close twice", Slot<decltype(&::CloseHandle)>("CloseHandle")(mutex), FALSE);
        Expect(ctx, "wait closed", wait(mutex, 0), WAIT_FAILED);
        Expect(ctx, "no errors yet", unsigned(drawfx::gErrors.size()), 0);
        Expect(ctx, "infinite wait never returns", wait(manualEvent, INFINITE), WAIT_FAILED);
        Expect(ctx, "that is an error", unsigned(drawfx::gErrors.size()), 1);
        drawfx::gErrors.clear();

        // Clock.
        imp.settings().msStep = 3;
        imp.BeginSide();
        auto tick = Slot<decltype(&::GetTickCount)>("GetTickCount");
        const uint32_t a = tick(), b = tick();
        Expect(ctx, "clock start", a, 0x10000);
        Expect(ctx, "clock step", b - a, 3);
        LARGE_INTEGER counter{}, frequency{};
        Slot<decltype(&::QueryPerformanceCounter)>("QueryPerformanceCounter")(&counter);
        Slot<decltype(&::QueryPerformanceFrequency)>("QueryPerformanceFrequency")(&frequency);
        Expect(ctx, "perf counter", uint32_t(counter.QuadPart), 0x23456789);
        Expect(ctx, "perf frequency", uint32_t(frequency.QuadPart), 3579545);
        Slot<DWORD(WINAPI *)(DWORD, BOOL)>("SleepEx")(40, FALSE);
        Expect(ctx, "sleep advances", tick(), 0x10000 + 6 + 40);
        Slot<decltype(&::QueryPerformanceCounter)>("QueryPerformanceCounter")(&counter);
        Expect(ctx, "sleep advances the counter", uint32_t(counter.QuadPart), 0x23456789 + 40 * 3579545 / 1000);

        // Libraries.
        HMODULE module = Slot<decltype(&::LoadLibraryA)>("LoadLibraryA")("CODEC.DLL");
        Expect(ctx, "module loaded", module != nullptr, 1);
        Expect(ctx, "unknown module", Slot<decltype(&::LoadLibraryA)>("LoadLibraryA")("other.dll") == nullptr, 1);
        auto decode = reinterpret_cast<uint32_t(__stdcall *)(uint32_t, uint32_t)>(
            Slot<decltype(&::GetProcAddress)>("GetProcAddress")(module, "Decode"));
        Expect(ctx, "export found", decode != nullptr, 1);
        if (decode) Expect(ctx, "export answers", decode(1, 2), 77);
        Expect(ctx, "no such export", Slot<decltype(&::GetProcAddress)>("GetProcAddress")(module, "Encode") == nullptr, 1);
        Expect(ctx, "free", Slot<decltype(&::FreeLibrary)>("FreeLibrary")(module), TRUE);
        Expect(ctx, "plain fake", uint32_t(uintptr_t(Slot<decltype(&::SetCursor)>("SetCursor")(nullptr))), 0x5c000010);

        // The log's text.
        const drawfx::Call *last = &drawfx::gLog.back();
        const std::string text = drawfx::Describe(*last);
        ++ctx.cases;
        if (text != "#444530689 import.SetCursor(0)") ctx.Fail("describe: %s", text.c_str());

        // An original under the fake on both sides: the SleepEx helper rounds microseconds up to milliseconds and
        // sleeps alertably.
        const uint32_t inputs[] = {0, 1, 999, 1000, 1001, 0xfffffc18, 0xfffffc19, 0xffffffff};
        for (uint32_t us : inputs) {
            imp.BeginSide();
            drawfx::ResetAll();
            ctx.Original<void(__cdecl *)(uint32_t)>(0x1b9f95)(us);
            Expect(ctx, "one call", unsigned(drawfx::gLog.size()), 1);
            if (!drawfx::gLog.empty()) {
                Expect(ctx, "SleepEx ms", drawfx::gLog[0].words[0], uint32_t((uint64_t(us) + 999) % 0x100000000ull / 1000));
                Expect(ctx, "SleepEx alertable", drawfx::gLog[0].words[1], 1);
            }
        }
        drawfx::ResetAll();
    }
    Expect(ctx, "restored", uint32_t(*GameAddress<void *>(0xd9a188) == savedSleepEx), 1);
}
