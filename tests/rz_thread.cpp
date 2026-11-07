// Differential tests for cRZThread's slots, its thread routines and the SleepEx helper (src/rz/cRZThreadSlots.size.cpp),
// on impfx (tests/impfx.h): the thread imports and the CRT's _beginthreadex / _endthreadex are fakes on both sides,
// threads are records whose routines run inline when a test asks, and every import call is compared in one log with
// the thread object's state. Each side builds its thread with its own constructor (the original's 0x1b9b19, ours); the
// thread object and its state block are registered ranges, so pointers into them log as offsets.
#include "difftest.h"
#include "free_recorder.h"
#include "impfx.h"
#include "rz/cRZThread.h"
#include "rz/cRZThreadRelease.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <random>
#include <string>

namespace {

constexpr uint32_t kCtor = 0x1b9b19, kDeletingRva = 0x1b9b7f;
constexpr uint32_t kRelease = 0x1b9ebb, kStartStartable = 0x1b9c32, kStartRoutine = 0x1b9bc1, kExit = 0x1b9c9c;
constexpr uint32_t kSuspend = 0x1b9fb1, kResume = 0x1b9ec9, kSleep = 0x1b9f77, kWake = 0x1b9ff1, kJoin = 0x1b9df1;
constexpr uint32_t kGetPriority = 0x1b9cd2, kSetPriority = 0x1b9edc, kIsRunning = 0x1b9db2, kGetExitCode = 0x1b9cba;
constexpr uint32_t kGetThreadId = 0x1b9d42, kGetHandle = 0x1b9d30, kSleepMicroseconds = 0x1b9f95, kExitWith = 0x1b9fd2;
constexpr uint32_t kRunStartable = 0x1b9d82, kRunRoutine = 0x1b9d56, kWakeApc = 0x300a0;
constexpr uint32_t kTagThread = 0x7e000000, kTagState = 0x7f000000, kTagCode = 0x7d000000, kTagStartable = 0x7c000000;

using U0 = uint32_t(__thiscall *)(cRZThread *);
using U1 = uint32_t(__thiscall *)(cRZThread *, uint32_t);
using U2 = uint32_t(__thiscall *)(cRZThread *, uint32_t, uint32_t);

// The startable slot 1 runs: slot 0 logs the thread it was given and answers the exit code in its second word.
struct Startable {
    void **vtable;
    uint32_t exitCode;
};
uint32_t __fastcall StartableRun(Startable *self, void *, cRZThread *thread)
{
    drawfx::Note("startable runs", drawfx::Norm(uint32_t(uintptr_t(thread))));
    return self->exitCode;
}
void *gStartableVTable[1] = {reinterpret_cast<void *>(&StartableRun)};

uint32_t __stdcall Routine(void *data)
{
    drawfx::Note("routine runs", uint32_t(uintptr_t(data)));
    return uint32_t(uintptr_t(data)) ^ 0x55;
}
// Our RunRoutine also survives a cdecl routine (the speculative loader's), which the original would leave unbalanced.
uint32_t __cdecl CdeclRoutine(void *data) { return uint32_t(uintptr_t(data)) + 1; }

// One side's thread.
struct World {
    cRZThread *thread;
    cRZThreadState *state;
    Startable startable;
};

World Build(difftest::Context &ctx, bool original)
{
    World w{};
    w.thread = static_cast<cRZThread *>(::operator new(sizeof(cRZThread)));
    if (original)
        ctx.Original<cRZThread *(__thiscall *)(cRZThread *)>(kCtor)(w.thread);
    else
        w.thread->Construct();
    w.state = w.thread->mState;
    w.startable = {gStartableVTable, 0};
    drawfx::AddRange(w.thread, sizeof(cRZThread), kTagThread);
    drawfx::AddRange(w.state, sizeof(cRZThreadState), kTagState);
    drawfx::AddRange(&w.startable, sizeof(Startable), kTagStartable);
    // The code addresses each side hands the OS (thread routines, the APC) log as the same tags.
    if (original) {
        drawfx::AddRange(GameAddress(kRunStartable), 1, kTagCode | 1);
        drawfx::AddRange(GameAddress(kRunRoutine), 1, kTagCode | 2);
        drawfx::AddRange(GameAddress(kWakeApc), 1, kTagCode | 3);
    } else {
        drawfx::AddRange(reinterpret_cast<void *>(&cRZThread::RunStartable), 1, kTagCode | 1);
        drawfx::AddRange(reinterpret_cast<void *>(&cRZThread::RunRoutine), 1, kTagCode | 2);
        drawfx::AddRange(reinterpret_cast<void *>(&cRZThreadWakeApc), 1, kTagCode | 3);
    }
    return w;
}

// The state's fields, as notes (the name and lock are not touched by the slots: the name's text is compared too).
void NoteState(const World &w)
{
    const cRZThreadState *s = w.state;
    drawfx::Note("state", {s->mField0, drawfx::Norm(s->mField4), s->mThreadId, s->mMagic, s->mRefCount});
    drawfx::Note("thread", {uint32_t(uintptr_t(w.thread->mVTable)), drawfx::Norm(uint32_t(uintptr_t(w.thread->mState)))});
    drawfx::Note(s->mName.mString.c_str()[0] ? "named" : "unnamed", uint32_t(s->mName.mString.size()));
}

// Frees a side's thread without closing fake handles for real: the handle is dropped first.
void Free(difftest::Context &ctx, World &w, bool original)
{
    w.state->mField4 = 0;
    w.state->mRefCount = 1;
    if (original)
        ctx.Original<U1>(kDeletingRva)(w.thread, 1);
    else
        w.thread->DeletingDestructor(1);
}

// A fake thread record for the state (made suspended, so it never runs; `suspended` false then clears the count).
uint32_t FakeThread(bool suspended = true)
{
    const uint32_t h = impfx::on::CreateThread(impfx::gFake, std::array<uint32_t, 6>{0, 0, 0, 0, CREATE_SUSPENDED, 0}.data());
    if (!suspended) impfx::Find(h)->suspend = 0;
    return h;
}

// Runs a body inside a fake thread (so _endthreadex has a thread to end); answers the thread's exit code.
std::function<uint32_t()> gBody;
uint32_t __stdcall RunBody(uint32_t) { return gBody(); }
template <class Body> uint32_t InThread(Body body)
{
    gBody = body;
    const uint32_t h = impfx::on::CreateThread(
        impfx::gFake, std::array<uint32_t, 6>{0, 0, uint32_t(uintptr_t(&RunBody)), 0, CREATE_SUSPENDED, 0}.data());
    impfx::RunThread(h);
    gBody = nullptr;
    return impfx::Find(h)->exitCode;
}

// Runs one case on both sides and compares the logs.
template <class Setup, class OrigRun, class OurRun>
void Case(difftest::Context &ctx, impfx::Imports &imp, const char *what, uint32_t seed, Setup setup, OrigRun orig, OurRun ours)
{
    World wo{}, wu{};
    auto a = drawfx::Side([&] { imp.BeginSide(); wo = Build(ctx, true); setup(wo); },
                          [&] { drawfx::Note("result", orig(wo)); NoteState(wo); });
    auto b = drawfx::Side([&] { imp.BeginSide(); wu = Build(ctx, false); setup(wu); },
                          [&] { drawfx::Note("result", ours(wu)); NoteState(wu); });
    drawfx::Same(ctx, a.first, b.first, what, seed, b.second.empty() ? a.second : b.second);
    Free(ctx, wo, true);
    Free(ctx, wu, false);
}

} // namespace

// Slots 4-11 and 14-16 with no thread, a suspended fake thread, a finished one and a closed handle.
DIFF_TEST(RZThread_Slots, 0x1b9db2)
{
    impfx::Imports imp(ctx);
    imp.Threads();
    imp.Sync();
    std::mt19937 rng(0x1f7a);
    enum { kNone, kSuspended, kFinished, kClosed, kRunning, kKinds };
    for (uint32_t seed = 0; seed < 120; ++seed) {
        const int kind = int(seed % kKinds);
        const int32_t level = int32_t(rng() % 7);
        const uint32_t magic = rng() % 3 == 0 ? cRZThreadState::kPriorityUnset : rng() % 9;
        const int32_t winPriority = std::array<int32_t, 9>{-15, -2, -1, 0, 1, 2, 15, 7, THREAD_PRIORITY_ERROR_RETURN}[rng() % 9];
        const uint32_t us = std::array<uint32_t, 8>{0, 1, 999, 1000, 1001, 0xfffffc18, 0xfffffc19, rng()}[rng() % 8];
        const uint32_t exitCode = rng(), threadId = rng() % 2 ? impfx::kMainThreadId : 0x7101;
        const uint32_t waitResult = std::array<uint32_t, 4>{WAIT_OBJECT_0, WAIT_TIMEOUT, WAIT_FAILED, WAIT_ABANDONED}[rng() % 4];
        imp.ScriptWaits({waitResult});
        auto setup = [&](World &w) {
            w.state->mMagic = magic;
            w.state->mField0 = exitCode;
            w.state->mThreadId = threadId;
            if (kind == kNone) return;
            const uint32_t h = FakeThread(kind != kRunning);
            impfx::Object *t = impfx::Find(h);
            t->priority = winPriority;
            if (kind == kFinished) {
                t->signaled = true;
                t->exitCode = exitCode ^ 1;
            }
            if (kind == kClosed) t->open = false;
            w.state->mField4 = h;
        };
        struct Slot {
            const char *name;
            uint32_t rva;
            int args;
        };
        const Slot slots[] = {{"Suspend", kSuspend, 0},      {"Resume", kResume, 0},     {"Sleep", kSleep, 1},
                              {"Wake", kWake, 0},            {"Join", kJoin, 1},         {"GetPriority", kGetPriority, 0},
                              {"SetPriority", kSetPriority, 2}, {"IsRunning", kIsRunning, 0}, {"GetExitCode", kGetExitCode, 3},
                              {"GetThreadId", kGetThreadId, 3}, {"GetHandle", kGetHandle, 3}};
        for (const Slot &s : slots) {
            uint32_t outO = 0xa5a5a5a5, outU = 0xa5a5a5a5;
            auto orig = [&](World &w) -> uint32_t {
                switch (s.args) {
                case 0: return ctx.Original<U0>(s.rva)(w.thread);
                case 1: return ctx.Original<U1>(s.rva)(w.thread, s.rva == kSleep ? us : rng());
                case 2: return ctx.Original<U1>(s.rva)(w.thread, uint32_t(level));
                default: {
                    const uint32_t r = ctx.Original<U1>(s.rva)(w.thread, uint32_t(uintptr_t(&outO)));
                    drawfx::Note("out", drawfx::Norm(outO));
                    return s.rva == kGetExitCode ? (r & 0xff) : r;
                }
                }
            };
            auto ours = [&](World &w) -> uint32_t {
                cRZThread *t = w.thread;
                switch (s.rva) {
                case kSuspend: return t->Suspend();
                case kResume: return t->Resume();
                case kSleep: return t->Sleep(us);
                case kWake: return t->Wake();
                case kJoin: return t->Join(rng());
                case kGetPriority: return uint32_t(t->GetPriority());
                case kSetPriority: return t->SetPriority(level) ? 1u : 0u;
                case kIsRunning: return t->IsRunning();
                case kGetExitCode: {
                    const uint32_t r = t->GetExitCode(&outU) ? 1u : 0u;
                    drawfx::Note("out", drawfx::Norm(outU));
                    return r;
                }
                case kGetThreadId: {
                    const uint32_t r = t->GetThreadId(&outU);
                    drawfx::Note("out", drawfx::Norm(outU));
                    return r;
                }
                default: {
                    void *h = nullptr;
                    const uint32_t r = t->GetHandle(&h) ? 1u : 0u;
                    drawfx::Note("out", drawfx::Norm(uint32_t(uintptr_t(h))));
                    return r;
                }
                }
            };
            // Suspend / Resume / Wake without a thread answer the state block's address: normalised by its range.
            auto origN = [&](World &w) { return drawfx::Norm(orig(w)); };
            auto oursN = [&](World &w) { return drawfx::Norm(ours(w)); };
            Case(ctx, imp, s.name, seed, setup, origN, oursN);
        }
    }
}

// Slots 1 and 2: starting a thread (inline or not), failing to, and a thread that is still running.
DIFF_TEST(RZThread_Start, 0x1b9c32)
{
    impfx::Imports imp(ctx);
    imp.Threads();
    imp.Sync();
    imp.CrtThreads();
    // The argument block is a fresh heap block on each side: _beginthreadex's argument logs as its contents instead.
    static bool sFail;
    imp.Get("_beginthreadex").On([](drawfx::Fake &f, const uint32_t *a) -> uint32_t {
        const auto *block = impfx::Ptr<const uint32_t>(a[3]);
        const bool four = drawfx::Norm(a[2]) == (kTagCode | 2);
        drawfx::gLog.back().words[3] = 0xb10c;
        drawfx::Note("block", {drawfx::Norm(block[0]), drawfx::Norm(block[1]), drawfx::Norm(block[2]),
                               four ? drawfx::Norm(block[3]) : 0u});
        if (sFail) return 0;
        return impfx::on::CreateThread(f, a);
    });
    std::mt19937 rng(0x5747);
    for (uint32_t seed = 0; seed < 64; ++seed) {
        sFail = seed % 4 == 1;
        const bool running = seed % 4 == 2, finished = seed % 4 == 3;
        imp.RunThreads(seed % 8 < 4 ? impfx::Run::Inline : impfx::Run::Never);
        const uint32_t code = rng();
        const uint32_t data = rng();
        auto setup = [&](World &w) {
            w.startable.exitCode = code;
            if (running || finished) {
                const uint32_t h = FakeThread();
                if (finished) impfx::Find(h)->signaled = true;
                w.state->mField4 = h;
            }
        };
        Case(
            ctx, imp, "Start(startable)", seed, setup,
            [&](World &w) { return ctx.Original<U1>(kStartStartable)(w.thread, uint32_t(uintptr_t(&w.startable))); },
            [&](World &w) { return w.thread->Start(&w.startable); });
        Case(
            ctx, imp, "Start(routine)", seed, setup,
            [&](World &w) { return ctx.Original<U2>(kStartRoutine)(w.thread, uint32_t(uintptr_t(&Routine)), data); },
            [&](World &w) { return w.thread->Start(&Routine, reinterpret_cast<void *>(uintptr_t(data))); });
    }
}

// Slot 3 and 0x1b9fd2 (the end of a thread), the routines on blocks of their own, slot 0 and slot 13.
DIFF_TEST(RZThread_Exit, 0x1b9fd2)
{
    impfx::Imports imp(ctx);
    imp.Threads();
    imp.Sync();
    imp.CrtThreads();
    std::mt19937 rng(0xe417);
    for (uint32_t seed = 0; seed < 40; ++seed) {
        const uint32_t code = rng(), refs = 2 + rng() % 3;
        // _endthreadex is recorded against the fake thread the routine runs on: run each inside a fake thread.
        auto setup = [&](World &w) { w.state->mRefCount = refs; };
        auto inThread = [](auto body) { return InThread(body); };
        Case(
            ctx, imp, "Exit", seed, setup,
            [&](World &w) { return inThread([&] { ctx.Original<U1>(kExit)(w.thread, code); return 0u; }); },
            [&](World &w) { return inThread([&] { w.thread->Exit(code); return 0u; }); });
        Case(
            ctx, imp, "ExitWith", seed, setup,
            [&](World &w) {
                return inThread([&] { ctx.Original<void(__cdecl *)(cRZThreadState *, uint32_t)>(kExitWith)(w.state, code); return 0u; });
            },
            [&](World &w) { return inThread([&] { cRZThread::ExitWith(w.state, code); return 0u; }); });
        // The routines: the block freed first, then the startable's slot 0 / the routine, then the end.
        Case(
            ctx, imp, "RunStartable", seed, setup,
            [&](World &w) {
                w.startable.exitCode = code;
                auto *args = static_cast<cRZThreadStartableArgs *>(::operator new(sizeof(cRZThreadStartableArgs)));
                *args = {&w.startable, w.thread, w.state};
                difftest::FreeRecorder rec;
                const uint32_t r = inThread([&] { return ctx.Original<uint32_t(__stdcall *)(void *)>(kRunStartable)(args); });
                rec.Stop();
                drawfx::Note("freed", rec.Freed(args) ? 1u : 0u);
                return r;
            },
            [&](World &w) {
                w.startable.exitCode = code;
                auto *args = static_cast<cRZThreadStartableArgs *>(::operator new(sizeof(cRZThreadStartableArgs)));
                *args = {&w.startable, w.thread, w.state};
                difftest::FreeRecorder rec;
                const uint32_t r = inThread([&] { return cRZThread::RunStartable(args); });
                rec.Stop();
                drawfx::Note("freed", rec.Freed(args) ? 1u : 0u);
                return r;
            });
        Case(
            ctx, imp, "RunRoutine", seed, setup,
            [&](World &w) {
                auto *args = static_cast<cRZThreadRoutineArgs *>(::operator new(sizeof(cRZThreadRoutineArgs)));
                *args = {&Routine, reinterpret_cast<void *>(uintptr_t(code)), w.thread, w.state};
                difftest::FreeRecorder rec;
                const uint32_t r = inThread([&] { return ctx.Original<uint32_t(__stdcall *)(void *)>(kRunRoutine)(args); });
                rec.Stop();
                drawfx::Note("freed", rec.Freed(args) ? 1u : 0u);
                return r;
            },
            [&](World &w) {
                auto *args = static_cast<cRZThreadRoutineArgs *>(::operator new(sizeof(cRZThreadRoutineArgs)));
                *args = {&Routine, reinterpret_cast<void *>(uintptr_t(code)), w.thread, w.state};
                difftest::FreeRecorder rec;
                const uint32_t r = inThread([&] { return cRZThread::RunRoutine(args); });
                rec.Stop();
                drawfx::Note("freed", rec.Freed(args) ? 1u : 0u);
                return r;
            });
        // Ours alone: a cdecl routine returns through RunRoutine with the stack intact.
        {
            imp.BeginSide();
            drawfx::ResetAll();
            World w = Build(ctx, false);
            w.state->mRefCount = 2;
            auto *args = static_cast<cRZThreadRoutineArgs *>(::operator new(sizeof(cRZThreadRoutineArgs)));
            *args = {reinterpret_cast<cRZThread::Routine>(&CdeclRoutine), reinterpret_cast<void *>(uintptr_t(code)), w.thread, w.state};
            const uint32_t r = inThread([&] { return cRZThread::RunRoutine(args); });
            ++ctx.cases;
            if (r != code + 1) ctx.Fail("seed %u: cdecl routine's exit code %#x", seed, r);
            drawfx::ResetAll();
            Free(ctx, w, false);
        }
        // Slot 13 with flags 0 / 1 (the state survives: two references), and slot 0 on null and on a thread.
        for (uint32_t flags : {0u, 1u, 2u}) {
            World wo{}, wu{};
            auto a = drawfx::Side([&] { imp.BeginSide(); wo = Build(ctx, true); wo.state->mRefCount = refs; },
                                  [&] {
                                      difftest::FreeRecorder rec;
                                      const uint32_t r = ctx.Original<U1>(kDeletingRva)(wo.thread, flags);
                                      rec.Stop();
                                      drawfx::Note("deleting", {drawfx::Norm(r), rec.Freed(wo.thread) ? 1u : 0u, wo.state->mRefCount});
                                  });
            auto b = drawfx::Side([&] { imp.BeginSide(); wu = Build(ctx, false); wu.state->mRefCount = refs; },
                                  [&] {
                                      difftest::FreeRecorder rec;
                                      const uint32_t r = uint32_t(uintptr_t(wu.thread->DeletingDestructor(flags)));
                                      rec.Stop();
                                      drawfx::Note("deleting", {drawfx::Norm(r), rec.Freed(wu.thread) ? 1u : 0u, wu.state->mRefCount});
                                  });
            drawfx::Same(ctx, a.first, b.first, "DeletingDestructor", seed * 4 + flags);
            for (World *w : {&wo, &wu}) {
                if (!(flags & 1)) ::operator delete(w->thread, sizeof(cRZThread));
                w->state->mRefCount = 1;
                appdebugger::ReleaseThreadState(w->state, nullptr);
            }
        }
        {
            World wo{}, wu{};
            auto a = drawfx::Side([&] { imp.BeginSide(); wo = Build(ctx, true); wo.state->mRefCount = 1; },
                                  [&] {
                                      difftest::FreeRecorder rec;
                                      const uint32_t r0 = ctx.Original<U0>(kRelease)(nullptr);
                                      const uint32_t r = ctx.Original<U0>(kRelease)(wo.thread);
                                      rec.Stop();
                                      drawfx::Note("release", {r0, r, rec.Freed(wo.thread) ? 1u : 0u, rec.Freed(wo.state) ? 1u : 0u});
                                  });
            auto b = drawfx::Side([&] { imp.BeginSide(); wu = Build(ctx, false); wu.state->mRefCount = 1; },
                                  [&] {
                                      difftest::FreeRecorder rec;
                                      const uint32_t r0 = cRZThread::Release(nullptr, nullptr);
                                      const uint32_t r = cRZThread::Release(wu.thread, nullptr);
                                      rec.Stop();
                                      drawfx::Note("release", {r0, r, rec.Freed(wu.thread) ? 1u : 0u, rec.Freed(wu.state) ? 1u : 0u});
                                  });
            drawfx::Same(ctx, a.first, b.first, "Release", seed);
        }
    }
}

// 0x1b9f95, the static SleepEx helper (slot 6's twin).
DIFF_TEST(RZThread_SleepMicroseconds, 0x1b9f95)
{
    impfx::Imports imp(ctx);
    imp.Threads();
    imp.Get("SleepEx").On([](drawfx::Fake &, const uint32_t *a) -> uint32_t { return a[0] * 3 + a[1]; });
    std::mt19937 rng(0x51ee);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        const uint32_t us = seed < 8 ? std::array<uint32_t, 8>{0, 1, 999, 1000, 1001, 0xfffffc18, 0xfffffc19, 0xffffffff}[seed] : rng();
        auto a = drawfx::Side([&] { imp.BeginSide(); },
                              [&] { drawfx::Note("result", ctx.Original<uint32_t(__cdecl *)(uint32_t)>(kSleepMicroseconds)(us)); });
        auto b = drawfx::Side([&] { imp.BeginSide(); }, [&] { drawfx::Note("result", cRZThread::SleepMicroseconds(us)); });
        drawfx::Same(ctx, a.first, b.first, "SleepMicroseconds", seed);
    }
}
