// Differential tests for the cTSObject end of the interpreter: cTSObject::vf4_15 (0x53d2d0, CallTree then the pending
// action) and vf4_36 (0x5315a0, ends a run) on a fake object. The generic cTSTreeSim::CallTree (0x577e80) and
// RunToCompletion (0x577890) they wrap are replaced, in the image and in our own code, by stubs that log their arguments
// and answer from the seed (both are verified on their own); the stack, its frames, the listener subobject and the object's
// slots 204 and 343 are fakes that record every call.
#include "simantics_treesim_world.h"

#include "hook/hooks.h" // MemberAddress only
#include "simantics/TreeSimObject.h"

#include <windows.h>

namespace {

using namespace tsw;

constexpr int kFxFrames = 6;

struct ObjFx {
    alignas(16) uint8_t object[0x300];
    void *vtObject[400];
    void *vtSim[8];
    void *vtStack[24];
    void *vtFrame[48];
    void *vtFrameNull[48];
    void *vtListener[16];
    FrameCell frame[kFxFrames];
    int32_t countDelta;
    uint32_t shrinkAtSelfCall; // the nth Self call removes the stack's last frame (0: never)
    uint32_t selfCalls;
    uint32_t canary;
};

ObjFx *gObj = nullptr;

std::vector<void *> &FxFrames(ObjFx &fx)
{
    return RawFrames(reinterpret_cast<TreeSimObject *>(fx.object + 4)->mSim.mStack.mFrames);
}

TSW_FN0(FxCount)
{
    Rec(self, 10, {});
    return static_cast<uint32_t>(static_cast<int32_t>(FxFrames(*gObj).size()) + gObj->countDelta);
}
TSW_FN0(FxSelf)
{
    Rec(self, 41, {});
    if (gObj->shrinkAtSelfCall != 0 && ++gObj->selfCalls == gObj->shrinkAtSelfCall && !FxFrames(*gObj).empty()) FxFrames(*gObj).pop_back();
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self));
}
TSW_FN0(FxSelfNull)
{
    Rec(self, 41, {});
    return 0;
}
TSW_FN2(FxTryStart, other, flag)
{
    Rec(self, 204, {other, flag});
    return 0;
}
// The original pushes the flag as a whole register whose upper 24 bits are left over from earlier code (a stack address
// here); the real callee (0x534600) ignores both arguments, and the log keeps only the byte the code sets.
TSW_FN2(FxRunFinished, actor, succeeded)
{
    Rec(self, 343, {actor, succeeded & 0xff});
    return 0;
}
TSW_FN1(FxRunStarting, actor)
{
    Rec(self, 11, {actor});
    return 0;
}

// The stubs of the two generic slots. __fastcall with the stack arguments after the two register ones is what a
// __thiscall callee that cleans its own arguments looks like.
uint32_t __fastcall CallTreeStub(void *self, void *, uint32_t owner, uint32_t w1a, uint32_t tree, uint32_t w18, uint32_t w1c, uint32_t args,
                                 uint32_t result, uint32_t flag16)
{
    Rec(self, 15, {owner, w1a, tree, w18, w1c, args, result, flag16});
    const uint32_t v = Mix(gSeed, 15, 1);
    // CallTree restores the depth when it finishes; a nested call that ends the outermost one leaves it at zero.
    reinterpret_cast<TreeSimObject *>(static_cast<uint8_t *>(self))->mSim.mCallDepth = static_cast<uint16_t>(v % 4 == 0 ? 1 : 0);
    return (v >> 8 & ~0xffu) | (v % 5 != 0);
}
uint32_t __fastcall RunToCompletionStub(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Rec(self, 36, {a, b, c});
    const uint32_t v = Mix(gSeed, 36, 1);
    return (v >> 8 & ~0xffu) | (v % 3 != 0);
}

struct Patch {
    uint8_t *site;
    uint8_t saved[5];
    Patch(uint8_t *where, void *target) : site(where)
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Patch() { std::memcpy(site, saved, 5); }
    Patch(const Patch &) = delete;
    Patch &operator=(const Patch &) = delete;
};

void BuildObj(ObjFx &fx, uint32_t seed)
{
    std::memset(&fx, 0, sizeof(fx));
    gSeed = seed;
    gObj = &fx;
    std::mt19937 r(seed * 977u + 13);
    auto P = [](auto fn) { return reinterpret_cast<void *>(fn); };
    fx.vtObject[204] = P(&FxTryStart);
    fx.vtObject[343] = P(&FxRunFinished);
    fx.vtListener[11] = P(&FxRunStarting);
    fx.vtStack[10] = P(&FxCount);
    fx.vtFrame[41] = P(&FxSelf);
    fx.vtFrameNull[41] = P(&FxSelfNull);
    for (uint8_t &b : fx.object) b = static_cast<uint8_t>(r());
    TreeSimObject *sim = reinterpret_cast<TreeSimObject *>(fx.object + 4);
    *reinterpret_cast<void ***>(fx.object) = fx.vtObject;
    sim->mSim.mVTable = fx.vtSim;
    sim->mSim.mStack.mVTable = fx.vtStack;
    new (&sim->mSim.mStack.mFrames) std::vector<TreeFrameRef>();
    sim->mListener.mVTable = fx.vtListener;
    sim->mSim.mCallDepth = static_cast<uint16_t>(Pick(r, {0, 0, 0, 1, 2}));
    sim->mAnnounceRuns = static_cast<uint8_t>(Pick(r, {0, 1, 1, 2, 0x80}));
    sim->mActionPending = static_cast<uint8_t>(Pick(r, {0, 1, 1, 3}));
    sim->mCountdownA = static_cast<uint16_t>(Pick(r, {0, 1, 2, 0xffff, r() & 0xffff}));
    sim->mCountdownB = static_cast<uint16_t>(Pick(r, {0, 1, 2, 0xffff, r() & 0xffff}));

    const uint32_t frames = r() % (kFxFrames + 1);
    for (uint32_t i = 0; i < frames; ++i) {
        FrameCell &cell = fx.frame[i];
        std::memset(cell.real, 0, sizeof(cell.real));
        reinterpret_cast<void ***>(cell.real)[0] = (r() % 12 == 0) ? fx.vtFrameNull : fx.vtFrame;
        reinterpret_cast<uint32_t *>(cell.real)[1] = 1;
        // The leading run of frames with a tree: the words are mostly real ids, now and then the "no tree" word.
        reinterpret_cast<cTSTreeStackElem *>(cell.real)->mTreeWord = static_cast<uint16_t>(Pick(r, {1, 2, 0x8001, 5, 0, 0x7fff, 0xffff, 3, 4, 6}));
        FxFrames(fx).push_back(&cell);
    }
    fx.countDelta = static_cast<int32_t>(Pick(r, {0, 0, 0, 0, 1, 2, 0xffffffffu, 0xfffffffeu}));
    fx.shrinkAtSelfCall = (r() % 10 == 0) ? 1 + r() % 8 : 0;
    fx.canary = 0xC0DEC0DEu;
    gBase = &fx;
    gBaseSize = sizeof(fx);
}

struct Result {
    uint32_t ret = 0;
    int kind = 0, thrown = -1;
    std::vector<uint32_t> trace, state;
};

template <typename Call> Result RunObj(uint32_t seed, Call call)
{
    ObjFx *fx = new ObjFx();
    BuildObj(*fx, seed);
    gTrace.clear();
    gHitCount = 0;
    gThrowAt = (seed % 7 == 0) ? 1 + seed % 6 : 0;
    gW = nullptr;
    Result res;
    try {
        res.ret = call(*fx) & 0xff;
    } catch (short code) {
        res.kind = 1;
        res.thrown = code;
    } catch (decltype(cTSTreeStack::kStackErrorNoFrame)) {
        res.kind = 2;
    } catch (...) {
        res.kind = 3;
    }
    res.trace = gTrace;
    TreeSimObject *sim = reinterpret_cast<TreeSimObject *>(fx->object + 4);
    res.state = {sim->mSim.mCallDepth, sim->mActionPending, sim->mAnnounceRuns, sim->mCountdownA, sim->mCountdownB, fx->canary,
                 static_cast<uint32_t>(FxFrames(*fx).size())};
    // The rest of the object (random bytes nobody should touch), past the sim, the listener's vtable pointer left out.
    for (uint32_t off = 0x78; off < sizeof(fx->object); ++off) res.state.push_back(fx->object[off]);
    for (const FrameCell &f : fx->frame) res.state.push_back(reinterpret_cast<const uint32_t *>(f.real)[1]);
    FxFrames(*fx).~vector();
    delete fx;
    gObj = nullptr;
    gBase = nullptr;
    gBaseSize = 0;
    return res;
}

void Check(difftest::Context &ctx, const char *name, uint32_t seed, const Result &a, const Result &b)
{
    ++ctx.cases;
    if (a.kind != b.kind || a.thrown != b.thrown || a.ret != b.ret || a.trace != b.trace || a.state != b.state) {
        size_t k = 0;
        while (k < a.trace.size() && k < b.trace.size() && a.trace[k] == b.trace[k]) ++k;
        ctx.Fail("%s seed %u: kind %d/%d thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (diff at %zu) state %s", name, seed, a.kind, b.kind, a.thrown,
                 b.thrown, a.ret, b.ret, a.trace.size(), b.trace.size(), k, a.state == b.state ? "same" : "DIFFERENT");
    }
}

} // namespace

DIFF_TEST(TreeSim_ObjectCallTree, 0x53d2d0)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x53d2d0);
    Patch imageStub(ctx.image.At<uint8_t>(0x577e80), reinterpret_cast<void *>(&CallTreeStub));
    Patch ourStub(static_cast<uint8_t *>(MemberAddress(&cTSTreeSim::CallTree)), reinterpret_cast<void *>(&CallTreeStub));
    for (uint32_t seed = 0; seed < 8000; ++seed) {
        const uint32_t a1 = seed * 3 + 1, a2 = seed ^ 0x5555, a3 = seed + 0x100, a4 = seed >> 1, a5 = seed * 7, a6 = 0x10 + seed, a7 = seed & 3, a8 = seed & 0xff;
        Result a = RunObj(seed, [&](ObjFx &fx) { return original(fx.object + 4, a1, a2, a3, a4, a5, a6, a7, a8); });
        Result b = RunObj(seed, [&](ObjFx &fx) {
            return reinterpret_cast<TreeSimObject *>(fx.object + 4)
                ->CallTree(reinterpret_cast<void *>(static_cast<uintptr_t>(a1)), a2, a3, a4, a5, reinterpret_cast<uint16_t *>(static_cast<uintptr_t>(a6)), a7, a8);
        });
        Check(ctx, "ObjectCallTree", seed, a, b);
    }
}

DIFF_TEST(TreeSim_ObjectRunToCompletion, 0x5315a0)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(0x5315a0);
    Patch imageStub(ctx.image.At<uint8_t>(0x577890), reinterpret_cast<void *>(&RunToCompletionStub));
    Patch ourStub(static_cast<uint8_t *>(MemberAddress(&cTSTreeSim::RunToCompletion)), reinterpret_cast<void *>(&RunToCompletionStub));
    for (uint32_t seed = 0; seed < 30000; ++seed) {
        const uint32_t a1 = seed * 5 + 2;
        const uint32_t a2 = seed % 3 == 0 ? 0u : (seed % 3 == 1 ? 1u : 0x100u); // only the low byte counts
        const uint32_t a3 = seed % 4 == 0 ? 0u : (seed % 4 == 1 ? 1u : (seed % 4 == 2 ? 0x200u : 0x201u));
        Result a = RunObj(seed, [&](ObjFx &fx) { return original(fx.object + 4, a1, a2, a3); });
        Result b = RunObj(seed, [&](ObjFx &fx) { return reinterpret_cast<TreeSimObject *>(fx.object + 4)->RunToCompletion(a1, a2, a3); });
        Check(ctx, "ObjectRunToCompletion", seed, a, b);
    }
}
