// Differential test for IdleForInput (0x521b90, src/simantics/PrimPersonIdle.cpp). Everything the handler calls is real on both
// sides: the person's interaction bookkeeping, the queues (real heap deques), the check-tree runner and the trace log, over the
// recording fakes of the shared person world (tests/simantics_person_world.h); only the fakes behind the vtables answer.
// The range error of an empty queue (a C++ out_of_range) is mapped to the harness's thrown code 0x77.
#include "simantics_person_world.h"

#include <stdexcept>

namespace {

uint32_t Val16(std::mt19937 &r, bool nonzero)
{
    const uint32_t noise = r() & 0xffff0000u;
    return nonzero ? (noise | (1 + r() % 0xffff)) : noise;
}

uint16_t *CounterPtr(WorldP &w)
{
    return reinterpret_cast<uint16_t *>(w.scratch + 0x300);
}

// The frame's slot 35: a pointer to the stack counter.
uint32_t __fastcall FrameCounter(void *self, void *, uint32_t index)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(35u << 8 | 1u);
    gTrace.push_back(index);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(CounterPtr(*gW)));
}

void BuildIdle(WorldP &w, uint32_t seed)
{
    Params p;
    p.previousAlways = true;
    p.historyNotEmpty = true;
    BuildPersonWorld(w, seed, 0x521b90, p);
    std::mt19937 r(seed * 2654435761u + 0x11);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);

    // The node: bit 0 of byte +8 picks the branch; the stack index word is mostly small and in range.
    const uint32_t index = r() % 12 == 0 ? 0xfff0 + r() % 16 : r() % 4;
    w.param[4] = static_cast<uint8_t>(index);
    w.param[5] = static_cast<uint8_t>(index >> 8);
    if (r() % 2) w.param[6] = w.param[7] = 0;
    if (r() % 2) w.param[8] |= 1; else w.param[8] &= ~1;

    Fake &frame = Fk(w, kFrame), &holder = Fk(w, kHolder), &target = Fk(w, kTarget);
    self->mHolder48 = &holder;

    // The person: slots on this, the tree sim subobject and the cITSPerson subobject.
    Use(w.vtThis, 13, 1);
    RetValue(w.obj, 13, Val16(r, r() % 2), w);
    Use(w.vtThis, 14, 2);
    Use(w.vtThis, 17, 1);
    RetValue(w.obj, 17, Val16(r, r() % 2), w);
    Use(w.vtThis, 248, 5); // cTSObject::vf248, ret 0x14
    RetValue(w.obj, 248, NoiseAl(r, r() % 4 != 0), w);
    Use(w.vtTree, 20, 0);
    RetObject(w.obj + 4, 20, &target, w);
    Use(target.vt, 13, 1);
    Use(w.vtPerson, 127, 0);
    RetValue(w.obj + 0x2f0, 127, Val16(r, r() % 4 != 0), w);
    Use(w.vtPerson, 138, 0);
    RetValue(w.obj + 0x2f0, 138, NoiseAl(r, r() % 2), w);

    // The frame.
    Use(frame.vt, 26, 0);
    RetValue(&frame, 26, NoiseAl(r, true) & ~0xffu | (1 + r() % 4), w);
    frame.vt[35] = reinterpret_cast<void *>(&FrameCounter);
    Use(frame.vt, 18, 0);
    RetValue(&frame, 18, r() % 2 ? 1u : r() % 3, w);
    Use(frame.vt, 19, 1);
    *CounterPtr(w) = static_cast<uint16_t>(r() % 3 == 0 ? 0 : r() % 3);

    // The holder (its slot 63 hands out the recorder of the check trees).
    Use(holder.vt, 53, 0);
    RetValue(&holder, 53, NoiseAl(r, r() % 2), w);
    Use(holder.vt, 63, 0);
    RetObject(&holder, 63, r() % 4 == 0 ? nullptr : &Fk(w, kRecorder), w);
}

} // namespace

DIFF_TEST(PrimPerson_IdleForInput, 0x521b90)
{
    const SavedGlobals saved(ctx);
    gBeforeRun = [&](WorldP &w) { SetGlobals(ctx, w); };

    auto original = ctx.Original<OrigFn>(0x521b90);
    // The empty-queue range error: the original raises the game's out_of_range, ours the toolchain's; both become code 0x77.
    auto mapped = [](auto &&call) -> uint32_t {
        try {
            return call();
        } catch (const std::out_of_range &) {
            throw static_cast<short>(0x77);
        }
    };
    uint32_t ret[2][4] = {};
    uint32_t throws = 0, injected = 0, rangeErrors = 0;
    RunSeeds(
        ctx, 40000, BuildIdle,
        [&](uint8_t *o, void *f, const void *p) {
            Queues guard(o);
            return mapped([&] { return original(o, f, p); });
        },
        [&](uint8_t *o, void *f, const void *p) {
            Queues guard(o);
            return mapped([&] { return reinterpret_cast<PrimPersonObject *>(o)->IdleForInput(f, static_cast<const PrimParamView *>(p)); });
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++throws;
                if (t.oa.thrown == 0x66) ++injected;
                if (t.oa.thrown == 0x77) ++rangeErrors;
            } else if (t.oa.ret < 4) {
                ++ret[w.param[8] & 1][t.oa.ret];
            }
        });
    gBeforeRun = nullptr;
    saved.Restore(ctx);
    for (int b = 0; b < 2; ++b)
        for (int v = 1; v <= 3; ++v)
            if (!(b == 1 && v == 2) && ret[b][v] < 50) ctx.Fail("coverage: branch %d return %d seen %u", b, v, ret[b][v]);
    if (throws < 500 || injected < 100 || rangeErrors < 20) ctx.Fail("coverage: throws %u injected %u range errors %u", throws, injected, rangeErrors);
}
