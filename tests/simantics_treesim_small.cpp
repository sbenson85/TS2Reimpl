// Differential tests for the small cTSTreeSim slots (queries on the stack, accessors, the shutdown) and for the trace log
// functions the interpreter calls (SimTraceLog), on the fake world of simantics_treesim.h.
#include "simantics_treesim_world.h"

namespace {

using namespace tsw;

uint32_t PtrBits(const void *p)
{
    return Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)));
}

SimTraceLog *Log()
{
    return SimTraceLog::Get();
}

void NoSetup(World &, uint32_t) {}

// GetPosition masks the tree word of the top frame: give it words with bit 14 set, and the "none" marker, among others.
void TopTreeWord(World &w)
{
    static const uint16_t words[] = {0x4000, 0x4001, 0x7fff, 0xffff, 0x8000, 0xc123, 0x0042};
    if (!Frames(w).empty()) reinterpret_cast<cTSTreeStackElem *>(Frames(w).back())->mTreeWord = words[gSeed % 7];
}

} // namespace

#define SMALL_TEST(name, rva, seeds, origCall, oursCall)                                                       \
    DIFF_TEST(TreeSim_##name, rva)                                                                             \
    {                                                                                                          \
        Stats st;                                                                                              \
        Compare(ctx, #name, seeds, NoSetup, [&](World &w) { return static_cast<uint32_t>(origCall); },        \
                [&](World &w) { return static_cast<uint32_t>(oursCall); }, st);                                \
        Report(#name, st);                                                                                     \
    }

#define ORIG(rva, Sig) ctx.Original<Sig>(rva)

SMALL_TEST(ClearErrorCode, 0x5761d0, 3000, (ORIG(0x5761d0, void(__thiscall *)(void *))(Sim(w)), 0),
           (Sim(w)->ClearErrorCode(), 0))
SMALL_TEST(ClearResult, 0x577e70, 3000, (ORIG(0x577e70, void(__thiscall *)(void *))(Sim(w)), 0), (Sim(w)->ClearResult(), 0))
SMALL_TEST(GetResult, 0x5773f0, 3000, ORIG(0x5773f0, uint32_t(__thiscall *)(void *))(Sim(w)) & 0xffff, Sim(w)->GetResult() & 0xffff)
SMALL_TEST(ReturnedTrue, 0x5772b0, 3000, ORIG(0x5772b0, uint32_t(__thiscall *)(void *))(Sim(w)) & 0xff, Sim(w)->ReturnedTrue())
SMALL_TEST(GetStepLimit, 0x577360, 3000, ORIG(0x577360, uint32_t(__thiscall *)(void *))(Sim(w)), Sim(w)->GetStepLimit())
SMALL_TEST(SetStepLimit, 0x5782d0, 3000, (ORIG(0x5782d0, void(__thiscall *)(void *, uint32_t))(Sim(w), gSeed * 7), 0),
           (Sim(w)->SetStepLimit(static_cast<int32_t>(gSeed * 7)), 0))
SMALL_TEST(GetRunFlag, 0x5779b0, 3000, ORIG(0x5779b0, uint32_t(__thiscall *)(void *))(Sim(w)) & 0xff, Sim(w)->GetRunFlag())

SMALL_TEST(GetTopFrame, 0x577040, 5000, PtrBits(ORIG(0x577040, void *(__thiscall *)(void *))(Sim(w))), PtrBits(Sim(w)->GetTopFrame()))
SMALL_TEST(GetFrameAt, 0x577370, 8000, PtrBits(ORIG(0x577370, void *(__thiscall *)(void *, uint32_t))(Sim(w), gSeed % 7 - 1 + (gSeed & 0x10000) + (gSeed & 0x200 ? 0x100 : 0))),
           PtrBits(Sim(w)->GetFrameAt(static_cast<int32_t>(gSeed % 7 - 1 + (gSeed & 0x10000) + (gSeed & 0x200 ? 0x100 : 0)))))
SMALL_TEST(NewFrame, 0x5761f0, 3000,
           ([&] {
               void *out = nullptr;
               const uint32_t ok = ORIG(0x5761f0, uint32_t(__thiscall *)(void *, uint32_t, void **))(Sim(w), 0x7a53b3dd, &out) & 0xff;
               return ok | PtrBits(out) << 1;
           }()),
           ([&] {
               void *out = nullptr;
               const uint32_t ok = Sim(w)->NewFrame(0x7a53b3dd, &out) & 0xff;
               return ok | PtrBits(out) << 1;
           }()))
SMALL_TEST(PrepareStack, 0x5779d0, 3000,
           (ORIG(0x5779d0, void(__thiscall *)(void *, uint32_t, void *))(Sim(w), gSeed & 15, gSeed & 1 ? static_cast<void *>(w.defaultArgs) : nullptr), 0),
           (Sim(w)->PrepareStack(gSeed & 15, gSeed & 1 ? w.defaultArgs : nullptr), 0))
SMALL_TEST(GetPosition, 0x577060, 10000,
           ([&] {
               TopTreeWord(w);
               uint16_t out[2] = {0x1111, 0x2222};
               ORIG(0x577060, void(__thiscall *)(void *, uint16_t *, uint16_t *))(Sim(w), &out[0], &out[1]);
               return static_cast<uint32_t>(out[0]) << 16 | out[1];
           }()),
           ([&] {
               TopTreeWord(w);
               uint16_t out[2] = {0x1111, 0x2222};
               Sim(w)->GetPosition(&out[0], &out[1]);
               return static_cast<uint32_t>(out[0]) << 16 | out[1];
           }()))
SMALL_TEST(FindOwnerBaseFrame, 0x577220, 10000, PtrBits(ORIG(0x577220, void *(__thiscall *)(void *))(Sim(w))),
           PtrBits(Sim(w)->FindOwnerBaseFrame()))
SMALL_TEST(FindLastRealFrame, 0x5772e0, 10000, PtrBits(ORIG(0x5772e0, void *(__thiscall *)(void *))(Sim(w))),
           PtrBits(Sim(w)->FindLastRealFrame()))
SMALL_TEST(GetCurrentOpcode, 0x5770e0, 10000, ORIG(0x5770e0, uint32_t(__thiscall *)(void *))(Sim(w)) & 0xffff,
           Sim(w)->GetCurrentOpcode() & 0xffff)
SMALL_TEST(Detach, 0x578450, 5000, (ORIG(0x578450, void(__thiscall *)(void *))(Sim(w)), 0), (Sim(w)->Detach(), 0))
SMALL_TEST(QueryInterface, 0x577af0, 5000,
           ([&] {
               void *out = nullptr;
               const uint32_t iid = gSeed % 3 == 0 ? 0x3ac12b12u : (gSeed % 3 == 1 ? 1u : 0x99u);
               const uint32_t ok = ORIG(0x577af0, uint32_t(__thiscall *)(void *, uint32_t, void **))(Sim(w), iid, &out) & 0xff;
               return ok | PtrBits(out) << 1;
           }()),
           ([&] {
               void *out = nullptr;
               const uint32_t iid = gSeed % 3 == 0 ? 0x3ac12b12u : (gSeed % 3 == 1 ? 1u : 0x99u);
               const uint32_t ok = Sim(w)->QueryInterface(iid, &out) & 0xff;
               return ok | PtrBits(out) << 1;
           }()))

// ---- the trace log -------------------------------------------------------------------------------------------------

SMALL_TEST(SimTraceLog_IsActive, 0x526fb0, 2000, ORIG(0x526fb0, uint32_t(__thiscall *)(void *))(Log()) & 0xff, Log()->IsActive())
SMALL_TEST(SimTraceLog_LogCall, 0x526e20, 6000,
           (ORIG(0x526e20, void(__thiscall *)(void *, int32_t, int32_t, const char *))(Log(), static_cast<int16_t>(gSeed), static_cast<int16_t>(gSeed >> 4), "bhav42"), 0),
           (Log()->LogCall(static_cast<int16_t>(gSeed), static_cast<int16_t>(gSeed >> 4), "bhav42"), 0))
SMALL_TEST(SimTraceLog_LogErrored, 0x526be0, 3000, (ORIG(0x526be0, void(__thiscall *)(void *))(Log()), 0), (Log()->LogErrored(), 0))
// WriteIndent writes through the sink without testing it: the worlds without one are skipped on both sides.
SMALL_TEST(SimTraceLog_WriteIndent, 0x527540, 3000,
           (Log()->mSink ? (ORIG(0x527540, void(__thiscall *)(void *))(Log()), 0) : 0),
           (Log()->mSink ? (Log()->WriteIndent(), 0) : 0))

namespace {

void *FrameFor(World &w, uint32_t seed)
{
    std::mt19937 r(seed ^ 0x5eed5eedu);
    switch (r() % 5) {
    case 0: return nullptr;
    case 1: return &w.frame[kFrames - 1];
    default: return Frames(w)[r() % Frames(w).size()];
    }
}

} // namespace

SMALL_TEST(SimTraceLog_LogNode, 0x5270a0, 40000,
           (ORIG(0x5270a0, void(__thiscall *)(void *, void *, uint32_t))(Log(), FrameFor(w, gSeed), gSeed & 1), 0),
           (Log()->LogNode(FrameFor(w, gSeed), gSeed & 1), 0))

// HasNoOperands (0x577990) reads a node parameter block directly, so it needs no world: blocks whose bytes +4..+0xb are
// all 0xff, ones with a single byte there that is not 0xff (every position), and random ones.
DIFF_TEST(TreeSim_HasNoOperands, 0x577990)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(const void *)>(0x577990);
    std::mt19937 r(0x0b5e55edu);
    for (int i = 0; i < 4000; ++i) {
        uint8_t block[16];
        for (uint8_t &b : block) b = static_cast<uint8_t>(r());
        if (i % 3 != 2) {
            for (int k = 4; k < 12; ++k) block[k] = 0xff;
            if (i % 3 == 1) block[4 + r() % 8] = static_cast<uint8_t>(r() % 255);
        }
        const bool expected = (original(block) & 0xff) != 0;
        const bool ours = reinterpret_cast<const TreeNodeParam *>(block)->HasNoOperands();
        if (expected != ours) ctx.Fail("HasNoOperands case %d: original %d, ours %d", i, expected, ours);
    }
}
