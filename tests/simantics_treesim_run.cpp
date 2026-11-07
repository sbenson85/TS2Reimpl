// Differential tests for pushing and running trees: cTSTreeSim::PushTree (0x577460), CallTree (0x577e80),
// RunToCompletion (0x577890), StartTree (0x577e10) and ResumeFrame (0x577810), on the fake world of
// simantics_treesim.h (the step they drive, DoNodeAction, is a scripted fake there).
#include "simantics_treesim_world.h"

namespace {

using namespace tsw;

struct PushArgs {
    void *owner;
    uint16_t *args;
    uint32_t treeId, flag16, w18, w1a, w1c, result;
};

PushArgs MakeArgs(World &w, uint32_t seed)
{
    std::mt19937 r(seed ^ 0x77777777u);
    PushArgs a;
    switch (r() % 6) {
    case 0: a.owner = nullptr; break;
    case 1: a.owner = reinterpret_cast<void *>(static_cast<uintptr_t>(0x12340000u)); break; // not dereferenced... see below
    default: a.owner = &w.owner[r() % kOwners]; break;
    }
    if (a.owner && reinterpret_cast<uintptr_t>(a.owner) == 0x12340000u) a.owner = &w.owner[0];
    switch (r() % 4) {
    case 0: a.args = nullptr; break;
    case 1: a.args = w.defaultArgs; break;
    default: a.args = reinterpret_cast<uint16_t *>(w.node[r() % kNodes].param + 4); break;
    }
    a.treeId = Pick(r, {0, 1, 2, 3, 4, 5, 6, 7, 0x100, 0x101, 0x1000, 0xffff, 0x8001, r() & 0xffff});
    a.flag16 = r() & 0xff;
    a.w18 = Pick(r, {0, 1, 2, 3, 0xffff, r() & 0xffff});
    a.w1a = Pick(r, {0, 1, 2, 3, 0xffff, r() & 0xffff});
    a.w1c = r() & 0xffff;
    a.result = r();
    if (gInjectWorld) {
        // TREESIM_INJECT_WORLD: the frame words (16-bit; the callees log them as passed) sometimes carry a high half that
        // makes them a word inside the shared world (see SharedWorld in simantics_treesim_world.h).
        for (uint32_t *v : {&a.w18, &a.w1a, &a.w1c})
            if (r() % 2) *v = InjectWorldLow16(*v);
    }
    return a;
}

// The stack starts with at least one frame; sometimes it is empty (PushTree with a null owner then dereferences a null
// top frame in the original and crashes, so those worlds always pass an owner).
void Tweak(World &w, uint32_t seed)
{
    std::mt19937 r(seed ^ 0x3c3c3c3cu);
    if (r() % 6 == 0) {
        for (void *f : Frames(w)) --reinterpret_cast<uint32_t *>(f)[1];
        Frames(w).clear();
    }
}

void TweakPush(World &w, uint32_t seed)
{
    Tweak(w, seed);
    w.context.table = &w.table; // PushTree uses the object table without a null test (ORIGINAL-BUG)
}

PushArgs ArgsFor(World &w)
{
    PushArgs a = MakeArgs(w, gSeed);
    if (Frames(w).empty() && !a.owner) a.owner = &w.owner[0];
    return a;
}

} // namespace

DIFF_TEST(TreeSim_PushTree, 0x577460)
{
    using Fn = uint32_t(__thiscall *)(void *, void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x577460);
    Stats st;
    Compare(ctx, "PushTree", 40000, TweakPush,
            [&](World &w) {
                PushArgs a = ArgsFor(w);
                return original(Sim(w), a.owner, a.args, a.treeId, a.flag16, a.w18, a.w1a, a.w1c) & 0xff;
            },
            [&](World &w) {
                PushArgs a = ArgsFor(w);
                return static_cast<uint32_t>(Sim(w)->PushTree(a.owner, a.args, a.treeId, a.flag16, a.w18, a.w1a, a.w1c)) & 0xff;
            },
            st);
    Report("PushTree", st);
}

DIFF_TEST(TreeSim_CallTree, 0x577e80)
{
    using Fn = uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, void *, uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x577e80);
    Stats st;
    Compare(ctx, "CallTree", 40000, Tweak,
            [&](World &w) {
                PushArgs a = ArgsFor(w);
                return original(Sim(w), a.owner, a.w1a, a.treeId, a.w18, a.w1c, a.args, a.result, a.flag16) & 0xff;
            },
            [&](World &w) {
                PushArgs a = ArgsFor(w);
                return static_cast<uint32_t>(Sim(w)->CallTree(a.owner, a.w1a, a.treeId, a.w18, a.w1c, a.args, a.result, a.flag16)) & 0xff;
            },
            st);
    Report("CallTree", st);
}

DIFF_TEST(TreeSim_RunToCompletion, 0x577890)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x577890);
    Stats st;
    Compare(ctx, "RunToCompletion", 30000, Tweak, [&](World &w) { return original(Sim(w), 1, 2, 3) & 0xff; },
            [&](World &w) { return static_cast<uint32_t>(Sim(w)->RunToCompletion(1, 2, 3)) & 0xff; }, st);
    Report("RunToCompletion", st);
}

DIFF_TEST(TreeSim_StartTree, 0x577e10)
{
    using Fn = uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x577e10);
    Stats st;
    // The original passes the address of two zero dwords as the argument block and PushTree copies up to eight words
    // from it (ORIGINAL-BUG: words 4..7 are whatever the stack holds, ours are zero), so only the four defined words
    // are compared.
    gArgWords = 4;
    Compare(ctx, "StartTree", 20000, TweakPush,
            [&](World &w) {
                PushArgs a = ArgsFor(w);
                return original(Sim(w), a.owner ? a.owner : &w.owner[1], a.treeId, a.w18);
            },
            [&](World &w) {
                PushArgs a = ArgsFor(w);
                return Sim(w)->StartTree(a.owner ? a.owner : &w.owner[1], a.treeId, a.w18);
            },
            st);
    gArgWords = 8;
    Report("StartTree", st);
}

DIFF_TEST(TreeSim_ResumeFrame, 0x577810)
{
    using Fn = uint32_t(__thiscall *)(void *, void *, void *);
    auto original = ctx.Original<Fn>(0x577810);
    Stats st;
    auto pick = [](World &w, int which) -> void * {
        std::mt19937 r(gSeed ^ 0x1234u);
        const uint32_t frameChoice = r() % 4, nodeChoice = r() % 4;
        if (which == 0) return frameChoice == 0 ? nullptr : (Frames(w).empty() ? &w.frame[kFrames - 1] : Frames(w)[(frameChoice - 1) % Frames(w).size()]);
        return nodeChoice == 0 ? nullptr : &w.node[r() % kNodes];
    };
    Compare(ctx, "ResumeFrame", 20000, Tweak, [&](World &w) { return original(Sim(w), pick(w, 0), pick(w, 1)); },
            [&](World &w) { return Sim(w)->ResumeFrame(pick(w, 0), pick(w, 1)); }, st);
    Report("ResumeFrame", st);
}
