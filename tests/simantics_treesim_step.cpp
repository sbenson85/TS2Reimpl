// Differential tests for the SimAntics interpreter step cTSTreeSim::DoNodeAction (slot 13, 0x5762d0) and the functions
// that start and run trees (PushTree 0x577460, CallTree 0x577e80, RunToCompletion 0x577890, StartTree 0x577e10,
// ResumeFrame 0x577810). The world is the fake of simantics_treesim.h; see there.
#include "simantics_treesim_world.h"

#include "hook/hooks.h" // MemberAddress only

#include <windows.h>

namespace {

using namespace tsw;

uint32_t gPickFrame = 0; // which frame the test passes: 0 none, 1..depth the stack's, depth+1 a spare frame

void ChooseFrame(World &w, uint32_t seed)
{
    std::mt19937 r(seed ^ 0x51ed270bu);
    const uint32_t depth = static_cast<uint32_t>(Frames(w).size());
    switch (r() % 8) {
    case 0: gPickFrame = 0; break;
    case 1: gPickFrame = depth + 1; break; // not on the stack
    case 2: gPickFrame = 1 + r() % depth; break;
    default: gPickFrame = depth; break;   // the top frame
    }
}

void *FrameArg(World &w)
{
    if (gPickFrame == 0) return nullptr;
    if (gPickFrame <= Frames(w).size()) return Frames(w)[gPickFrame - 1];
    return &w.frame[kFrames - 1];
}

} // namespace

DIFF_TEST(TreeSim_DoNodeAction, 0x5762d0)
{
    using Fn = uint32_t(__thiscall *)(void *, void *, uint32_t);
    auto original = ctx.Original<Fn>(0x5762d0);
    Stats st;
    auto setup = [](World &w, uint32_t seed) {
        std::mt19937 r(seed ^ 0xa5a5a5a5u);
        ChooseFrame(w, seed);
        // The frame the test passes should often sit on a node that exists and is not a special target.
        if (gPickFrame != 0 && r() % 3 != 0) {
            cTSTreeStackElem *f = reinterpret_cast<cTSTreeStackElem *>(FrameArg(w));
            f->mNodeIndex = static_cast<uint16_t>(r() % kNodesPerTree);
            f->mTreeWord = static_cast<uint16_t>(f->mTreeWord & 0x7fff);
        }
    };
    Compare(ctx, "DoNodeAction", 60000, setup, [&](World &w) { return original(Sim(w), FrameArg(w), 0); },
            [&](World &w) { return Sim(w)->DoNodeAction(FrameArg(w), 0); }, st);
    Report("DoNodeAction", st);
}

// Several steps in a row on one world (the replay table is filled by the pops of early steps and consulted by the later
// calls of the same trees), the frame passed being the top one (sometimes, in the first step, one of the others).
DIFF_TEST(TreeSim_DoNodeActionRun, 0x5762d0)
{
    using Fn = uint32_t(__thiscall *)(void *, void *, uint32_t);
    auto original = ctx.Original<Fn>(0x5762d0);
    Stats st;
    auto setup = [](World &w, uint32_t seed) {
        std::mt19937 r(seed ^ 0x6d6d6d6du);
        ChooseFrame(w, seed);
        // A world where the tree flags and cache bits are often set, so that records and replays happen.
        for (TreeCell &t : w.tree) {
            if (r() % 2) t.flags |= 8;
            t.cache = static_cast<uint8_t>(r() % 4 == 0 ? 0 : r());
        }
        if (r() % 3) Sim(w)->mRecording = 1;
    };
    auto run = [](World &w, auto &&step) -> uint32_t {
        uint32_t h = 17;
        const uint32_t n = 1 + gSeed % 5;
        for (uint32_t k = 0; k < n; ++k) {
            void *top = Frames(w).empty() ? nullptr : Frames(w).back();
            if (gSeed % 7 == 0 && k == 0) top = FrameArg(w);
            try {
                h = h * 131 + step(top) + 1;
            } catch (short code) {
                h = h * 131 + 0x10000 + static_cast<uint16_t>(code);
            } catch (decltype(cTSTreeStack::kStackErrorNoFrame)) {
                h = h * 131 + 0x20000;
            }
        }
        return h;
    };
    Compare(ctx, "DoNodeActionRun", 40000, setup, [&](World &w) { return run(w, [&](void *f) { return original(Sim(w), f, 0); }); },
            [&](World &w) { return run(w, [&](void *f) { return Sim(w)->DoNodeAction(f, 0); }); }, st);
    Report("DoNodeActionRun", st);
}

// The record/replay functions (0x575ed0 and 0x576e70, the original's and ours) replaced by stubs that log
// their arguments and answer from a script: the interpreter's glue around them (when it calls them, with which flags, what
// it does with a replayed result) is then compared exactly, including the replay hits that real worlds rarely produce.
namespace {

struct ImagePatch {
    uint8_t *site;
    uint8_t saved[5];
    ImagePatch(difftest::Context &ctx, uint32_t rva, void *target) : ImagePatch(ctx.image.At<uint8_t>(rva), target) {}
    // A jump at any code address (the image's, or one of our own functions).
    ImagePatch(uint8_t *where, void *target) : site(where)
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~ImagePatch() { std::memcpy(site, saved, 5); }
    ImagePatch(const ImagePatch &) = delete;
    ImagePatch &operator=(const ImagePatch &) = delete;
};

uint32_t __fastcall ReplayStub(void *self, void *, uint32_t flags, uint32_t cache, uint32_t opcode, uint32_t argCount, const uint16_t *args,
                               uint32_t *out, void *, void *)
{
    Rec(self, 0x40, {flags, cache, opcode, argCount, args ? 1u : 0u, out ? 1u : 0u}, false);
    if (args)
        for (uint32_t i = 0; i < argCount && i < 8; ++i) gTrace.push_back(args[i]);
    const uint32_t v = Mix(gSeed, opcode, 0x40);
    if (v % 3 == 0) {
        *out = v % 5 == 0 ? 0xffffffffu : (v >> 4) % 6;
        return 1;
    }
    return 0;
}

void __fastcall RecordStub(void *self, void *, uint32_t flags, uint32_t cache, uint32_t opcode, void *frame)
{
    Rec(self, 0x41, {flags, cache, opcode, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame))}, false);
}

} // namespace

DIFF_TEST(TreeSim_DoNodeActionReplay, 0x576e70)
{
    using Fn = uint32_t(__thiscall *)(void *, void *, uint32_t);
    auto original = ctx.Original<Fn>(0x5762d0);
    ImagePatch patchReplay(ctx, 0x576e70, reinterpret_cast<void *>(&ReplayStub));
    ImagePatch patchRecord(ctx, 0x575ed0, reinterpret_cast<void *>(&RecordStub));
    // Our own record/replay functions get the same stubs, so both sides of the comparison see the scripted answers.
    ImagePatch patchOurReplay(static_cast<uint8_t *>(MemberAddress(&cTSTreeSim::ReplayResult)), reinterpret_cast<void *>(&ReplayStub));
    ImagePatch patchOurRecord(static_cast<uint8_t *>(MemberAddress(&cTSTreeSim::RecordResult)), reinterpret_cast<void *>(&RecordStub));
    Stats st;
    auto setup = [](World &w, uint32_t seed) {
        std::mt19937 r(seed ^ 0x2468ace1u);
        ChooseFrame(w, seed);
        for (TreeCell &t : w.tree) {
            if (r() % 2) t.flags |= 8;
            t.cache = static_cast<uint8_t>(r() % 4 == 0 ? 0 : r());
        }
        Sim(w)->mRecording = 1;
    };
    auto run = [](World &w, auto &&step) -> uint32_t {
        uint32_t h = 17;
        const uint32_t n = 1 + gSeed % 4;
        for (uint32_t k = 0; k < n; ++k) {
            void *top = Frames(w).empty() ? nullptr : Frames(w).back();
            try {
                h = h * 131 + step(top) + 1;
            } catch (short code) {
                h = h * 131 + 0x10000 + static_cast<uint16_t>(code);
            } catch (decltype(cTSTreeStack::kStackErrorNoFrame)) {
                h = h * 131 + 0x20000;
            }
        }
        return h;
    };
    Compare(ctx, "DoNodeActionReplay", 40000, setup, [&](World &w) { return run(w, [&](void *f) { return original(Sim(w), f, 0); }); },
            [&](World &w) { return run(w, [&](void *f) { return Sim(w)->DoNodeAction(f, 0); }); }, st);
    Report("DoNodeActionReplay", st);
}
