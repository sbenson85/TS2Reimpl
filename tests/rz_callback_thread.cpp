// Differential tests for cRZCallbackThread's slots and its thread body (src/rz/cRZCallbackThreadSlots.size.cpp) on
// impfx: the thread's imports and the CRT's _beginthreadex are fakes, its routine never runs by itself (the body is
// called directly), and the stopwatch reads impfx's performance counter on both sides (the original's through the
// import slot, ours through cRZStopwatch's counter override, which calls the same slot). Each side builds its object
// with its own constructor (the original's 0x9cfa5d, ours); the object is a registered range. The embedded thread is
// reached through the game's vtable on both sides (the original thread slots, the same on both).
#include "difftest.h"
#include "free_recorder.h"
#include "impfx.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZThread.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0x9cfa5d, kDeleting = 0x9cfb3e;
constexpr uint32_t kQueryInterface = 0x9cfc6f, kAddRef = 0x9cfc40, kRelease = 0x9cfc93, kAddCallback = 0x9cfb5d,
                   kRemoveCallback = 0x9cfcb4, kSetPriority = 0x9cfd09, kGetPriority = 0x9cfc67, kGetThreadId = 0x9cfc52,
                   kRun = 0x9cfd11;
constexpr uint32_t kTagObject = 0x7e000000, kTagState = 0x7f000000, kIatQueryPerformanceCounter = 0xd9a24c;

using Self = cRZCallbackThread;
using U0 = uint32_t(__thiscall *)(Self *);
using U1 = uint32_t(__thiscall *)(Self *, uint32_t);
using U2 = uint32_t(__thiscall *)(Self *, uint32_t, uint32_t);
using AddFn = uint32_t(__thiscall *)(Self *, Self::Callback, void *, uint32_t, uint32_t, uint32_t);
using RunFn = uint32_t(__thiscall *)(void *, void *);

// Ours reads the counter through the same import slot as the original.
void CounterViaImport(uint64_t *out)
{
    reinterpret_cast<BOOL(WINAPI *)(LARGE_INTEGER *)>(*GameAddress<void *>(kIatQueryPerformanceCounter))(reinterpret_cast<LARGE_INTEGER *>(out));
}

// The stopwatch in mode 4 scales by a game global the start-up calibration sets: one tick is one unit here and the
// fake counter runs at 1000 ticks a second, so units are milliseconds, as in the game.
struct ClockScope {
    float savedScale;
    uint64_t savedOverhead;
    explicit ClockScope(impfx::Imports &imp)
    {
        savedScale = GameAddress<float>(cRZStopwatch::kScalesRva)[2];
        savedOverhead = *GameAddress<uint64_t>(cRZStopwatch::kOverheadRva);
        GameAddress<float>(cRZStopwatch::kScalesRva)[2] = 1.0f;
        *GameAddress<uint64_t>(cRZStopwatch::kOverheadRva) = 0;
        cRZStopwatch::sPerformanceCounterOverride = &CounterViaImport;
        imp.settings().perfFrequency = 1000;
        imp.settings().perfCount = 0x5000;
    }
    ~ClockScope()
    {
        cRZStopwatch::sPerformanceCounterOverride = nullptr;
        GameAddress<float>(cRZStopwatch::kScalesRva)[2] = savedScale;
        *GameAddress<uint64_t>(cRZStopwatch::kOverheadRva) = savedOverhead;
    }
};

// The fake _beginthreadex: the argument block is a fresh heap block on each side, so its pointer logs as a marker
// (its contents are cRZThread's, tested in rz_thread.cpp).
void FakeBeginThread(impfx::Imports &imp)
{
    imp.Get("_beginthreadex").On([](drawfx::Fake &f, const uint32_t *a) -> uint32_t {
        drawfx::gLog.back().words[3] = 0xb10c;
        return impfx::on::CreateThread(f, a);
    });
}

Self *Build(difftest::Context &ctx, bool original)
{
    auto *self = static_cast<Self *>(::operator new(sizeof(Self)));
    std::memset(self, 0xcd, sizeof(Self));
    drawfx::AddRange(self, sizeof(Self), kTagObject); // first: the constructor's counter read points into it
    if (original)
        ctx.Original<U0>(kCtor)(self);
    else
        self->Construct();
    drawfx::AddRange(self->mThread.mState, sizeof(cRZThreadState), kTagState);
    return self;
}

// The object's state: flags, count, next id, the queue's entries (their padding is never written: left out) and its
// capacity, the thread's handle and remembered priority.
void NoteObject(Self *self)
{
    drawfx::Note("object", {self->mRunning, self->mOwnedByCaller, self->mField0c, self->mField40,
                            drawfx::Norm(self->mThread.mState->mField4), self->mThread.mState->mMagic});
    auto *begin = reinterpret_cast<const Self::Entry *>(uintptr_t(self->mField10));
    auto *end = reinterpret_cast<const Self::Entry *>(uintptr_t(self->mField14));
    for (auto *e = begin; e != end; ++e)
        drawfx::Note("entry", {e->mId, uint32_t(uintptr_t(e->mCallback)), uint32_t(uintptr_t(e->mData)), e->mOnce,
                               e->mInterval, e->mDue, e->mFixedRate});
    drawfx::Note("queue", {uint32_t(end - begin), uint32_t(self->mField18 - self->mField10) / uint32_t(sizeof(Self::Entry))});
}

// Frees a side's object: the fake thread handle dropped first (so the destructor neither waits nor closes it).
void Free(difftest::Context &ctx, Self *self, bool original)
{
    self->mThread.mState->mField4 = 0;
    if (original)
        ctx.Original<U1>(kDeleting)(self, 1);
    else
        self->DeletingDestruct(1);
}

// The side under test, for the callbacks and the fakes that act on it.
difftest::Context *gCtx = nullptr;
Self *gSelf = nullptr;
bool gOriginal = false;
uint32_t gCalls = 0, gStopAfter = 0, gSleeps = 0, gSleepLimit = 0;

uint32_t SideAdd(Self::Callback cb, uint32_t data, uint32_t delay, bool once, bool fixedRate)
{
    if (gOriginal) return gCtx->Original<AddFn>(kAddCallback)(gSelf, cb, reinterpret_cast<void *>(uintptr_t(data)), delay, once, fixedRate);
    return gSelf->AddCallback(cb, reinterpret_cast<void *>(uintptr_t(data)), delay, once, fixedRate);
}
uint32_t SideRemove(uint32_t id)
{
    if (gOriginal) return gCtx->Original<U1>(kRemoveCallback)(gSelf, id) & 0xff;
    return gSelf->RemoveCallback(id) ? 1u : 0u;
}

// The callbacks log their data; data's low nibble asks for more: 1 removes the entry whose id is in bits 8..15, 2 adds
// a once-entry due after bits 8..15 ms, 3 stops the thread (clears the running flag). Each run stops after gStopAfter.
void __cdecl Callback(void *data)
{
    const uint32_t d = uint32_t(uintptr_t(data));
    drawfx::Note("callback", d);
    switch (d & 0xf) {
    case 1: drawfx::Note("removed", SideRemove((d >> 8) & 0xff)); break;
    case 2: drawfx::Note("added", SideAdd(&Callback, 0x100, (d >> 8) & 0xff, true, false)); break;
    case 3: gSelf->mRunning = 0; break;
    default: break;
    }
    if (++gCalls >= gStopAfter) gSelf->mRunning = 0;
}
void __cdecl OtherCallback(void *data) { drawfx::Note("other callback", uint32_t(uintptr_t(data))); }

// Runs one case on both sides and compares the logs: build, then `body` with the side set.
template <class Setup, class Body>
void Case(difftest::Context &ctx, impfx::Imports &imp, const char *what, uint32_t seed, Setup setup, Body body)
{
    Self *so = nullptr, *su = nullptr;
    auto run = [&](bool original, Self *&self) {
        return drawfx::Side(
            [&] {
                imp.BeginSide();
                self = Build(ctx, original);
                gSelf = self;
                gOriginal = original;
                gCalls = gSleeps = 0;
                setup(self);
            },
            [&] {
                gSelf = self;
                gOriginal = original;
                body(self, original);
                NoteObject(self);
            });
    };
    auto a = run(true, so);
    auto b = run(false, su);
    drawfx::Same(ctx, a.first, b.first, what, seed, b.second.empty() ? a.second : b.second);
    Free(ctx, so, true);
    Free(ctx, su, false);
}

} // namespace

// Slots 0-7 in random sequences: queueing into an empty and a busy queue (the thread started, resumed and woken),
// removing present and absent ids, the priority and id slots through the thread, the reference count with and without
// mOwnedByCaller, all with the running flag set or not.
DIFF_TEST(RZCallbackThread_Slots, 0x9cfb5d)
{
    impfx::Imports imp(ctx);
    imp.Threads();
    imp.Sync();
    imp.Clock();
    imp.CrtThreads();
    FakeBeginThread(imp);
    ClockScope clock(imp);
    gCtx = &ctx;
    std::mt19937 rng(0xcb7);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        imp.settings().perfStep = rng() % 3;
        const bool running = seed % 7 != 6, owned = rng() % 2;
        const uint32_t count = 2 + rng() % 4;
        struct Op {
            int kind;
            uint32_t a, b, c, d, e;
        };
        std::vector<Op> ops;
        const int n = 1 + int(rng() % 14);
        for (int i = 0; i < n; ++i) {
            Op op{int(rng() % 9), rng(), rng(), rng(), rng(), rng()};
            op.b = std::array<uint32_t, 7>{0, 1, 5, 100, 0xfffffff0, 0xffffffff, op.b}[rng() % 7]; // delay
            ops.push_back(op);
        }
        auto setup = [&](Self *self) {
            self->mRunning = running;
            self->mOwnedByCaller = owned;
            self->mField0c = count;
        };
        auto body = [&](Self *self, bool original) {
            void *out = reinterpret_cast<void *>(0xa5a5a5a5);
            for (const Op &op : ops) {
                uint32_t r = 0;
                switch (op.kind) {
                case 0:
                case 1: // add (twice as often)
                    r = SideAdd(op.a % 2 ? &Callback : &OtherCallback, op.c, op.b, op.d % 2, op.e % 2);
                    break;
                case 2: // remove: a queued id (low ids) or another
                    r = SideRemove(op.a % 3 ? op.a % 6 : op.a);
                    break;
                case 3:
                    r = original ? ctx.Original<U1>(kSetPriority)(self, op.a % 7) & 0xff : self->SetPriority(int32_t(op.a % 7));
                    break;
                case 4: r = original ? ctx.Original<U0>(kGetPriority)(self) : uint32_t(self->GetPriority()); break;
                case 5: r = original ? ctx.Original<U0>(kGetThreadId)(self) : self->GetThreadId(); break;
                case 6: {
                    const uint32_t iid = std::array<uint32_t, 3>{1, Self::kIid, op.a}[op.b % 3];
                    r = original ? ctx.Original<U2>(kQueryInterface)(self, iid, uint32_t(uintptr_t(&out))) & 0xff
                                 : (self->QueryInterface(iid, &out) ? 1u : 0u);
                    drawfx::Note("out", drawfx::Norm(uint32_t(uintptr_t(out))));
                    break;
                }
                case 7: r = original ? ctx.Original<U0>(kAddRef)(self) : self->AddRef(); break;
                default: // Release while the count stays above zero
                    if (self->mField0c < 2) self->mField0c = 2;
                    r = original ? ctx.Original<U0>(kRelease)(self) : self->Release();
                    break;
                }
                drawfx::Note("result", {uint32_t(op.kind), r});
            }
        };
        Case(ctx, imp, "slots", seed, setup, body);
    }
    // Queueing into a queue emptied again: the thread exists (and runs, unless the fake thread has ended), so it is not
    // started a second time; resumed and woken as before.
    for (uint32_t seed = 0; seed < 6; ++seed) {
        auto setup = [&](Self *self) { self->mRunning = 1; };
        auto body = [&](Self *self, bool) {
            const uint32_t first = SideAdd(&Callback, 0x10, seed * 3, seed % 2, false);
            drawfx::Note("first", first);
            drawfx::Note("removed", SideRemove(first));
            if (seed >= 3) self->mThread.mState->mField4 = 0; // the thread gone: started again
            drawfx::Note("second", SideAdd(&Callback, 0x20, seed, false, seed % 2));
        };
        Case(ctx, imp, "refill", seed, setup, body);
    }
    // Release to zero: slot 8 deletes the object (its thread handle dropped first), with mOwnedByCaller; without it
    // nothing happens.
    for (uint32_t seed = 0; seed < 4; ++seed) {
        auto release = [&](bool original) {
            uint32_t r = 0;
            bool freed = false;
            auto log = drawfx::Side(
                [&] {
                    imp.BeginSide();
                    gSelf = Build(ctx, original);
                    gSelf->mOwnedByCaller = seed % 2;
                    gSelf->mField0c = seed < 2 ? 1 : 0;
                    gSelf->mThread.mState->mField4 = 0;
                },
                [&] {
                    difftest::FreeRecorder rec;
                    r = original ? ctx.Original<U0>(kRelease)(gSelf) : gSelf->Release();
                    rec.Stop();
                    freed = rec.Freed(gSelf);
                    drawfx::Note("release", {r, freed ? 1u : 0u});
                });
            if (!freed) Free(ctx, gSelf, original);
            return log;
        };
        auto a = release(true);
        auto b = release(false);
        drawfx::Same(ctx, a.first, b.first, "release to zero", seed);
    }
}

// The thread body on a queue built by the side's own slot 3: due and not yet due entries, once and repeating, fixed
// rate or not, delays that wrap; callbacks that remove or add entries or stop the thread; an empty queue suspending
// the thread; the sleeps between rounds (impfx's SleepEx advances the counter). Each run stops after a few callbacks,
// a few sleeps (the fake SleepEx clears the running flag) or a suspension (the fake SuspendThread clears it).
DIFF_TEST(RZCallbackThread_Run, 0x9cfd11)
{
    impfx::Imports imp(ctx);
    imp.Threads();
    imp.Sync();
    imp.Clock();
    imp.CrtThreads();
    FakeBeginThread(imp);
    imp.Get("SleepEx").On([](drawfx::Fake &f, const uint32_t *a) -> uint32_t {
        if (++gSleeps >= gSleepLimit) gSelf->mRunning = 0;
        return impfx::on::SleepEx(f, a);
    });
    imp.Get("SuspendThread").On([](drawfx::Fake &f, const uint32_t *a) -> uint32_t {
        gSelf->mRunning = 0;
        return impfx::on::SuspendThread(f, a);
    });
    ClockScope clock(imp);
    gCtx = &ctx;
    std::mt19937 rng(0x7e11);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        imp.settings().perfStep = rng() % 3;
        gStopAfter = 1 + rng() % 12;
        gSleepLimit = 1 + rng() % 8;
        const bool startStopped = seed % 23 == 22;
        struct Add {
            uint32_t data, delay;
            bool once, fixedRate;
        };
        std::vector<Add> adds;
        const int n = int(rng() % 6);
        for (int i = 0; i < n; ++i) {
            const uint32_t action = std::array<uint32_t, 6>{0, 0, 0, 1, 2, 3}[rng() % 6];
            const uint32_t data = ((rng() % 8) << 8) | (i << 4) | action;
            const uint32_t delay = std::array<uint32_t, 8>{0, 0, 1, 3, 10, 50, 0xfffffff0, rng() % 200}[rng() % 8];
            adds.push_back({data, delay, rng() % 2 == 0, rng() % 2 == 0});
        }
        const uint32_t advance = rng() % 60;
        auto setup = [&](Self *self) {
            for (const Add &add : adds) SideAdd(&Callback, add.data, add.delay, add.once, add.fixedRate);
            // An empty queue has no thread yet: give it one, so the suspension is a call.
            if (self->mThread.mState->mField4 == 0) {
                const uint32_t h = impfx::on::CreateThread(impfx::gFake, std::array<uint32_t, 6>{0, 0, 0, 0, CREATE_SUSPENDED, 0}.data());
                self->mThread.mState->mField4 = h;
            }
            impfx::on::Sleep(impfx::gFake, &advance); // time passes before the body runs
            if (startStopped) self->mRunning = 0;
        };
        auto body = [&](Self *self, bool original) {
            const uint32_t r = original ? ctx.Original<RunFn>(kRun)(&self->mStartableVTable, &self->mThread)
                                        : Self::Run(&self->mStartableVTable, nullptr, &self->mThread);
            drawfx::Note("run", {r, gCalls, gSleeps});
        };
        Case(ctx, imp, "run", seed, setup, body);
    }
}
