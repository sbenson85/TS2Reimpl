// Differential tests for cRZThreadSignal's signalling slots (src/rz/cRZThreadSignalSlots.size.cpp) on impfx's event
// fakes: each side builds its signal with its own constructor (the original's 0x9ccaa9, ours), then runs the same
// random sequence of slot calls; results, the event calls and the event's state are compared in one log.
#include "difftest.h"
#include "impfx.h"
#include "rz/cRZThreadSignal.h"

#include <array>
#include <cstdint>
#include <random>

namespace {

constexpr uint32_t kCtor = 0x9ccaa9, kDeleting = 0x9ccb38;
constexpr uint32_t kSignal = 0x9ccb93, kPulse = 0x9ccb57, kWait = 0x9ccbb9, kIsValid = 0x9ccb7a, kReset = 0x9ccbfb;

using U0 = uint32_t(__thiscall *)(cRZThreadSignal *);
using U1 = uint32_t(__thiscall *)(cRZThreadSignal *, uint32_t);

bool gNoEvent = false;
uint32_t CreateEventOrFail(drawfx::Fake &f, const uint32_t *a) { return gNoEvent ? 0 : impfx::on::CreateEventA(f, a); }

} // namespace

DIFF_TEST(RZThreadSignal_Slots, 0x9ccb93)
{
    impfx::Imports imp(ctx);
    imp.Sync();
    imp.Get("CreateEventA").On(&CreateEventOrFail);
    std::mt19937 rng(0x5e7e);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        const uint32_t manual = std::array<uint32_t, 4>{0, 1, 2, 0x80000000}[rng() % 4];
        gNoEvent = seed % 10 == 9;
        uint32_t ops[12], args[12];
        for (int i = 0; i < 12; ++i) {
            ops[i] = rng() % 5;
            args[i] = std::array<uint32_t, 6>{0, 999, 1000, 0xffffffff, 0xfffffffe, rng()}[rng() % 6];
        }
        std::vector<uint32_t> waits;
        for (int i = 0; i < 12; ++i) waits.push_back(std::array<uint32_t, 4>{WAIT_OBJECT_0, WAIT_TIMEOUT, WAIT_FAILED, WAIT_ABANDONED}[rng() % 4]);
        const bool scripted = seed % 3 == 0;
        imp.ScriptWaits(scripted ? waits : std::vector<uint32_t>{});
        // An INFINITE wait on an unsignalled fake event never returns (impfx reports it): unscripted cases wait finitely.
        if (!scripted)
            for (uint32_t &arg : args)
                if (arg == 0xffffffff) arg = 0xfffffffe;
        auto noteEvent = [](cRZThreadSignal *s) {
            const impfx::Object *e = impfx::Find(uint32_t(uintptr_t(s->mState->mEvent)));
            drawfx::Note("event", {e ? 1u : 0u, e && e->signaled ? 1u : 0u, e && e->manual ? 1u : 0u, s->mState->mManualReset});
        };
        cRZThreadSignal *so = nullptr, *su = nullptr;
        auto a = drawfx::Side(
            [&] {
                imp.BeginSide();
                so = static_cast<cRZThreadSignal *>(::operator new(sizeof(cRZThreadSignal)));
                ctx.Original<U1>(kCtor)(so, manual);
            },
            [&] {
                for (int i = 0; i < 12; ++i) {
                    const uint32_t rvas[5] = {kSignal, kPulse, kWait, kIsValid, kReset};
                    const uint32_t r = ops[i] == 2 ? ctx.Original<U1>(kWait)(so, args[i]) : ctx.Original<U0>(rvas[ops[i]])(so);
                    drawfx::Note("result", ops[i] == 2 ? r : (r & 0xff));
                    noteEvent(so);
                }
            });
        auto b = drawfx::Side(
            [&] {
                imp.BeginSide();
                su = static_cast<cRZThreadSignal *>(::operator new(sizeof(cRZThreadSignal)));
                su->Construct(manual);
            },
            [&] {
                for (int i = 0; i < 12; ++i) {
                    uint32_t r = 0;
                    switch (ops[i]) {
                    case 0: r = su->Signal(); break;
                    case 1: r = su->Pulse(); break;
                    case 2: r = su->Wait(args[i]); break;
                    case 3: r = su->IsValid(); break;
                    default: r = su->Reset(); break;
                    }
                    drawfx::Note("result", r);
                    noteEvent(su);
                }
            });
        drawfx::Same(ctx, a.first, b.first, "slots", seed, a.second.empty() ? b.second : a.second);
        // The table word each constructor stores (ours the owned static ts2_rva_e61b7c, the image's table in this build).
        if (so->mVTable != su->mVTable) ctx.Fail("seed %u: table word %p / %p", seed, so->mVTable, su->mVTable);
        // The event handles are fake: the deleting destructors' CloseHandle goes to the fake (both through the slot).
        ctx.Original<U1>(kDeleting)(so, 1);
        su->DeletingDestructor(1);
    }
}
