// Differential tests for cRZCallbackTimer (src/rz/cRZCallbackTimer.size.cpp) on drawfx fakes: the callback services
// (two framework ones and the static default, a fake embedded at 0x10f59e8), the framework at 0x10c9378 and the
// object it hands the service through, and the holders the timer keeps alive are recording fakes. The live-timer set
// (0x10f59d8) is a fresh std::set for the test and its lock (0x10f59e0) a constructed cRZCriticalSection; both are
// restored afterwards. Each side builds its timer with its own constructor; ours gets a copy of the game's vtable with
// our slots in it, so its self-calls (the restart in slots 1 and 3) reach ours, as the hooks make them in the game.
#include "difftest.h"
#include "drawfx.h"
#include "free_recorder.h"
#include "hook/hooks.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZCriticalSection.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <set>
#include <vector>

namespace {

using Self = cRZCallbackTimer;
constexpr uint32_t kCtor = 0x9ccf32, kDtor = 0x9ccfa9, kDeleting = 0x9cd02c, kSetField10 = 0x9cd1f3,
                   kSetPeriod = 0x9cd19d, kStartRepeating = 0x9cd2b7, kStartOnce = 0x9cd249, kStop = 0x9cd32b,
                   kSetCallback = 0x9cd18a, kSetUseService = 0x9cd04b, kOnTimer = 0x9cd0ec;
constexpr uint32_t kFrameworkRva = 0x10c9378;
constexpr uint32_t kTagTimer = 0x7e000000, kTagOnTimer = 0x7d000000, kTagDefault = 0x7c000000, kTagStranger = 0x7b000000;

using U0 = uint32_t(__thiscall *)(void *);
using U1 = uint32_t(__thiscall *)(void *, uint32_t);
using U2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using OnTimerFn = void(__cdecl *)(void *);

inline constexpr drawfx::Slot kServiceSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ii"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {5, drawfx::Ret::Int, true, "AddCallback", "iiuuu"},
    {1, drawfx::Ret::Int, true, "RemoveCallback", "u"},
};
inline constexpr drawfx::Iface kService{"CallbackService", 5, kServiceSlots};
inline constexpr drawfx::Slot kUnknownSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ii"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
};
inline constexpr drawfx::Iface kUnknown{"Unknown", 3, kUnknownSlots};
inline constexpr drawfx::Slot kFrameworkSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ii"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {3, drawfx::Ret::Int, true, "GetSystemService", "uui"},
};
inline constexpr drawfx::Iface kFramework{"Framework", 6, kFrameworkSlots};
inline constexpr drawfx::Slot kSourceSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ii"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, true, "GetCallbackService", ""},
};
inline constexpr drawfx::Iface kSource{"ServiceSource", 4, kSourceSlots};

drawfx::Fake gServiceA, gServiceB, gDefault, gHolderA, gHolderB, gFramework, gSource;

// The side under test.
difftest::Context *gCtx = nullptr;
bool gOriginal = false;
Self *gSelf = nullptr;
uint32_t gNextId = 0, gFailEvery = 0, gAdds = 0;
OnTimerFn gFired = nullptr; // the callback the last AddCallback was handed
void *gFiredData = nullptr;

// The service's AddCallback: hands out ids, every gFailEvery-th call fails (0); remembers the callback.
uint32_t ServiceAdd(drawfx::Fake &, const uint32_t *a)
{
    gFired = reinterpret_cast<OnTimerFn>(uintptr_t(a[0]));
    gFiredData = reinterpret_cast<void *>(uintptr_t(a[1]));
    ++gAdds;
    if (gFailEvery && gAdds % gFailEvery == 0) return 0;
    return ++gNextId;
}
uint32_t FrameworkGet(drawfx::Fake &self, const uint32_t *a)
{
    if (!self.value[0]) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gSource;
    return 1;
}

// Our side's vtable: the game's, with our slots.
void *gOurVTable[13];
void BuildOurVTable()
{
    std::memcpy(gOurVTable, GameAddress<void *>(Self::kVTable), sizeof gOurVTable);
    gOurVTable[0] = MemberAddress(&Self::DeletingDestructor);
    gOurVTable[1] = MemberAddress(&Self::SetField10);
    gOurVTable[3] = MemberAddress(&Self::SetPeriod);
    gOurVTable[5] = MemberAddress(&Self::StartRepeating);
    gOurVTable[6] = MemberAddress(&Self::StartOnce);
    gOurVTable[7] = MemberAddress(&Self::Stop);
    gOurVTable[9] = gOurVTable[10] = MemberAddress(&Self::SetCallback);
    gOurVTable[11] = MemberAddress(&Self::SetUseService);
}

// The game globals the timer uses, for the test's duration: a fresh live set and lock, the default service's vtable
// word, the framework pointer.
struct World {
    alignas(4) unsigned char saved[0x14];
    drawfx::Cells cells;
    World()
    {
        std::memcpy(saved, GameAddress<unsigned char>(Self::kLiveSetRva), sizeof saved);
        new (GameAddress<void>(Self::kLiveSetRva)) std::set<uint32_t>();
        GameAddress<cRZCriticalSection>(Self::kLiveLockRva)->Construct();
        cells.Set(kFrameworkRva, uint32_t(0));
        BuildOurVTable();
    }
    ~World()
    {
        GameAddress<cRZCriticalSection>(Self::kLiveLockRva)->Destruct();
        std::destroy_at(GameAddress<std::set<uint32_t>>(Self::kLiveSetRva));
        std::memcpy(GameAddress<unsigned char>(Self::kLiveSetRva), saved, sizeof saved);
    }
};
std::set<uint32_t> &Live() { return *GameAddress<std::set<uint32_t>>(Self::kLiveSetRva); }

// The fakes, programmed for a case: whether the framework answers, which service its source hands out.
struct Script {
    bool framework, answers;
    int service; // 0 A, 1 B, 2 the default (a null one would crash the original's next start)
    uint32_t failEvery;
    int seeded; // a state set over the constructed one: 0 none, 1 the service flag flipped, 2 active without an id, 3 an id while idle
};
void ResetFakes(const Script &s)
{
    gServiceA.Reset<kService>(0x801);
    gServiceB.Reset<kService>(0x802);
    gDefault.Reset<kService>(0x803);
    for (drawfx::Fake *f : {&gServiceA, &gServiceB, &gDefault}) {
        f->On(3, &ServiceAdd);
        f->Returns(4, 1u);
    }
    gDefault.Embed(GameAddress<void>(Self::kDefaultServiceRva));
    drawfx::AddRange(GameAddress<void>(Self::kDefaultServiceRva), 4, kTagDefault);
    gHolderA.Reset<kUnknown>(0x901);
    gHolderB.Reset<kUnknown>(0x902);
    gFramework.Reset<kFramework>(0x601);
    gFramework.value[0] = s.answers;
    gFramework.On(5, &FrameworkGet);
    gSource.Reset<kSource>(0x701);
    void *services[] = {&gServiceA, &gServiceB, GameAddress<void>(Self::kDefaultServiceRva)};
    gSource.Returns(3, services[s.service]);
    *GameAddress<void *>(kFrameworkRva) = s.framework ? &gFramework : nullptr;
    drawfx::AddRange(GameAddress<void>(kOnTimer), 1, kTagOnTimer);
    drawfx::AddRange(reinterpret_cast<const void *>(&Self::OnTimer), 1, kTagOnTimer);
    gNextId = 0;
    gAdds = 0;
    gFailEvery = s.failEvery;
    gFired = nullptr;
    gFiredData = nullptr;
}

Self *Build(bool original)
{
    auto *self = static_cast<Self *>(::operator new(sizeof(Self)));
    std::memset(self, 0xcd, sizeof(Self));
    drawfx::AddRange(self, sizeof(Self), kTagTimer);
    if (original) {
        gCtx->Original<U0>(kCtor)(self);
    } else {
        self->Construct();
        self->mVTable = gOurVTable;
    }
    return self;
}

uint32_t Word(const void *p) { return drawfx::Norm(uint32_t(uintptr_t(p))); }
void NoteTimer(Self *t)
{
    drawfx::Note("timer", {t->mUseService, Word(reinterpret_cast<const void *>(t->mCallback)), Word(t->mData),
                           t->mField10, t->mPeriod, t->mActive, t->mOnce, t->mId, Word(t->mService), Word(t->mHolder),
                           uint32_t(Live().count(uint32_t(uintptr_t(t)))), uint32_t(Live().size())});
    drawfx::Note("refs", {uint32_t(gServiceA.refs), uint32_t(gServiceB.refs), uint32_t(gDefault.refs),
                          uint32_t(gHolderA.refs), uint32_t(gHolderB.refs), uint32_t(gSource.refs)});
}

// The side's slots.
uint32_t SideStop() { return gOriginal ? gCtx->Original<U0>(kStop)(gSelf) & 0xff : gSelf->Stop(); }
uint32_t SideStartOnce(uint32_t delay, void *holder)
{
    return gOriginal ? gCtx->Original<U2>(kStartOnce)(gSelf, delay, uint32_t(uintptr_t(holder))) & 0xff
                     : gSelf->StartOnce(delay, holder);
}
uint32_t SideStartRepeating(uint32_t period, void *holder)
{
    return gOriginal ? gCtx->Original<U2>(kStartRepeating)(gSelf, period, uint32_t(uintptr_t(holder))) & 0xff
                     : gSelf->StartRepeating(period, holder);
}
void SideOnTimer(void *timer)
{
    if (gOriginal)
        gCtx->Original<OnTimerFn>(kOnTimer)(timer);
    else
        Self::OnTimer(timer);
}

// The timer's callback: logs its data; data's low two bits ask for more: 1 stops the timer, 2 starts it once again.
void __cdecl UserCallback(void *data)
{
    const uint32_t d = uint32_t(uintptr_t(data));
    drawfx::Note("user callback", d);
    if ((d & 3) == 1) drawfx::Note("stop in callback", SideStop());
    if ((d & 3) == 2) drawfx::Note("start in callback", SideStartOnce(7, nullptr));
}

// A buffer shaped as a timer whose callback is set but that is not in the live set: OnTimer must drop it.
Self gStranger;

} // namespace

// The whole class in random sequences: construction with and without a framework service (the framework absent,
// declining, or handing out a service or the default), the callback set or cleared, starts once and repeating
// with and without a holder, delays and periods zero or not, the service failing every few adds, stops, the setters'
// restarts, service switches, the service calling back (the callback stopping or restarting the timer, or none set),
// a callback for a timer that is not live, and destruction (plain, deleting with the flag clear or set). One case in six
// starts from a state no slot sequence of these fakes reaches (the service flag flipped, active without an id, an id while
// idle), so the branches that tell those apart are compared too.
DIFF_TEST(RZCallbackTimer_All, 0x9ccf32)
{
    World world;
    gCtx = &ctx;
    std::mt19937 rng(0x7173);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Script script{rng() % 4 != 0, rng() % 3 != 0, int(rng() % 3), std::array<uint32_t, 5>{0, 0, 2, 3, 5}[rng() % 5],
                      rng() % 6 == 0 ? int(1 + rng() % 3) : 0};
        struct Op {
            int kind;
            uint32_t a, b, c;
        };
        std::vector<Op> ops;
        const int n = 1 + int(rng() % 16);
        for (int i = 0; i < n; ++i) ops.push_back({int(rng() % 11), rng(), rng(), rng()});
        const uint32_t ending = rng() % 3;
        auto side = [&](bool original) {
            difftest::FreeRecorder *rec = nullptr;
            bool freed = false;
            Self *self = nullptr;
            auto log = drawfx::Side(
                [&] {
                    ResetFakes(script);
                    gOriginal = original;
                    std::memset(&gStranger, 0, sizeof gStranger);
                    gStranger.mCallback = &UserCallback;
                    gStranger.mData = reinterpret_cast<void *>(uintptr_t(0x50));
                    drawfx::AddRange(&gStranger, sizeof gStranger, kTagStranger);
                    self = Build(original);
                    gSelf = self;
                    switch (script.seeded) {
                    case 1: self->mUseService ^= 1; break;
                    case 2: self->mActive = 1; break;
                    case 3: self->mId = 0x77; break;
                    default: break;
                    }
                },
                [&] {
                    NoteTimer(self);
                    void *holders[] = {nullptr, &gHolderA, &gHolderB};
                    for (const Op &op : ops) {
                        uint32_t r = 0;
                        void *holder = holders[op.b % 3];
                        const uint32_t time = std::array<uint32_t, 4>{0, 1, 250, op.c}[op.a % 4];
                        switch (op.kind) {
                        case 0: { // set or clear the callback
                            Self::Callback cb = op.a % 4 ? &UserCallback : nullptr;
                            void *data = reinterpret_cast<void *>(uintptr_t(op.c & 0xff));
                            r = original ? gCtx->Original<U2>(kSetCallback)(self, uint32_t(uintptr_t(cb)), uint32_t(uintptr_t(data))) & 0xff
                                         : self->SetCallback(cb, data);
                            break;
                        }
                        case 1: r = original ? gCtx->Original<U1>(kSetField10)(self, time) & 0xff : self->SetField10(time); break;
                        case 2: r = original ? gCtx->Original<U1>(kSetPeriod)(self, time) & 0xff : self->SetPeriod(time); break;
                        case 3: r = SideStartRepeating(time, holder); break;
                        case 4: r = SideStartOnce(time, holder); break;
                        case 5: r = SideStop(); break;
                        case 6: { // switch the service use; the framework's answer may change first
                            if (op.c % 2) gFramework.value[0] = op.c % 4 == 1;
                            r = original ? gCtx->Original<U1>(kSetUseService)(self, op.a % 2) & 0xff
                                         : self->SetUseService(op.a % 2 != 0);
                            break;
                        }
                        case 7:
                        case 8: // the service calls back (the last callback it was handed, else the timer's OnTimer)
                            if (gFired)
                                gFired(gFiredData);
                            else
                                SideOnTimer(self);
                            break;
                        case 9: SideOnTimer(&gStranger); break;
                        default: // a holder's references, as the code under test left them
                            r = uint32_t(gHolderA.refs * 7 + gHolderB.refs);
                            break;
                        }
                        drawfx::Note("result", {uint32_t(op.kind), r});
                        NoteTimer(self);
                    }
                    difftest::FreeRecorder recorder;
                    if (ending == 0) {
                        if (original) gCtx->Original<U0>(kDtor)(self); else self->Destruct();
                    } else if (original) {
                        gCtx->Original<U1>(kDeleting)(self, ending - 1);
                    } else {
                        self->DeletingDestructor(ending - 1);
                    }
                    recorder.Stop();
                    freed = recorder.Freed(self);
                    drawfx::Note("end", {freed ? 1u : 0u, uint32_t(Live().size())});
                    drawfx::Note("refs", {uint32_t(gServiceA.refs), uint32_t(gServiceB.refs), uint32_t(gDefault.refs),
                                          uint32_t(gHolderA.refs), uint32_t(gHolderB.refs), uint32_t(gSource.refs)});
                });
            (void)rec;
            if (!freed) ::operator delete(self);
            return log;
        };
        auto a = side(true);
        auto b = side(false);
        drawfx::Same(ctx, a.first, b.first, "timer", seed, b.second.empty() ? a.second : b.second);
    }
}
