// Differential tests for the construction and destruction of cTSTreeSim (constructor 0x5755e0, destructor body 0x5759f0,
// deleting destructor 0x575c30). The services the constructor takes come from game globals that the test points at
// recording fakes; the objects are raw memory of the same size, filled with the same random bytes on both sides.
#include "simantics_treesim_world.h"

namespace {

using namespace tsw;

constexpr uint32_t kGlobalContext = 0x10cc288;
constexpr uint32_t kGlobalServiceC = 0x10cc290;

struct LifeFx {
    void *vtContext[32];
    void *vtCell[8];
    void *vtFrame[8];
    ContextCell context;
    Cell gate, serviceC, debug, frame[4];
};

struct Saved {
    void *context, *serviceC, *debug;
};

void BuildLife(LifeFx &fx, uint32_t seed, bool withDebug)
{
    std::memset(&fx, 0, sizeof(fx));
    gSeed = seed;
    fx.vtContext[1] = reinterpret_cast<void *>(&AddRefFake);
    fx.vtContext[2] = reinterpret_cast<void *>(&ReleaseFake);
    fx.vtContext[3] = reinterpret_cast<void *>(&CxGate);
    fx.vtCell[1] = reinterpret_cast<void *>(&AddRefFake);
    fx.vtCell[2] = reinterpret_cast<void *>(&ReleaseFake);
    fx.context.vt = fx.vtContext;
    fx.context.refs = 1;
    fx.context.gate = &fx.gate;
    for (Cell *c : {&fx.gate, &fx.serviceC, &fx.debug, &fx.frame[0], &fx.frame[1], &fx.frame[2], &fx.frame[3]}) {
        c->vt = fx.vtCell;
        c->refs = 3;
    }
    (void)withDebug;
    gBase = &fx;
    gBaseSize = sizeof(fx);
}

Saved Install(LifeFx &fx, bool withDebug)
{
    Saved s{*GameAddress<void *>(kGlobalContext), *GameAddress<void *>(kGlobalServiceC), *GameAddress<void *>(0x10cc294)};
    *GameAddress<void *>(kGlobalContext) = &fx.context;
    *GameAddress<void *>(kGlobalServiceC) = &fx.serviceC;
    *GameAddress<void *>(0x10cc294) = withDebug ? static_cast<void *>(&fx.debug) : nullptr;
    return s;
}

void Uninstall(const Saved &s)
{
    *GameAddress<void *>(kGlobalContext) = s.context;
    *GameAddress<void *>(kGlobalServiceC) = s.serviceC;
    *GameAddress<void *>(0x10cc294) = s.debug;
}

struct Result {
    std::vector<uint32_t> trace, state;
    uint32_t ret = 0;
    int kind = 0;
};

// The object after construction: the plain bytes except the heap pointers of the replay map, whose shape is checked
// instead (head linked to itself, sixteen buckets all at the head).
void SnapshotConstructed(const cTSTreeSim *sim, Result &res)
{
    const uint8_t *b = reinterpret_cast<const uint8_t *>(sim);
    for (uint32_t off = 0; off < sizeof(cTSTreeSim); ++off) {
        const bool heap = (off >= 0x50 && off < 0x54) || (off >= 0x58 && off < 0x64);
        res.state.push_back(heap ? 0u : b[off]);
    }
    TreeReplayNode *head = sim->mReplayMap.mList.mHead;
    res.state.push_back(head && head->next == head && head->prev == head);
    bool buckets = true;
    for (TreeReplayNode *p : sim->mReplayMap.mBuckets) buckets = buckets && p == head;
    res.state.push_back(buckets);
    res.state.push_back(static_cast<uint32_t>(sim->mReplayMap.mBuckets.size()));
    res.state.push_back(static_cast<uint32_t>(sim->mReplayMap.mBuckets.capacity()));
}

void FreeReplayMap(cTSTreeSim *sim)
{
    std::vector<TreeReplayNode *>().swap(sim->mReplayMap.mBuckets);
    ::operator delete(sim->mReplayMap.mList.mHead, 0x20);
}

} // namespace

DIFF_TEST(TreeSim_Construct, 0x5755e0)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0x5755e0);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Result res[2];
        for (int side = 0; side < 2; ++side) {
            LifeFx fx;
            const bool withDebug = seed % 3 != 0;
            BuildLife(fx, seed, withDebug);
            Saved saved = Install(fx, withDebug);
            alignas(16) uint8_t raw[sizeof(cTSTreeSim) + 16];
            std::mt19937 r(seed);
            for (uint8_t &b : raw) b = static_cast<uint8_t>(r());
            cTSTreeSim *sim = reinterpret_cast<cTSTreeSim *>(raw);
            gTrace.clear();
            gHitCount = 0;
            gThrowAt = 0;
            void *ret = side == 0 ? original(sim) : sim->Construct();
            res[side].ret = ret == sim ? 1 : 0;
            res[side].trace = gTrace;
            SnapshotConstructed(sim, res[side]);
            for (uint32_t i = sizeof(cTSTreeSim); i < sizeof(raw); ++i) res[side].state.push_back(raw[i]); // beyond the object
            for (Cell *c : {static_cast<Cell *>(&fx.context), &fx.gate, &fx.serviceC, &fx.debug}) res[side].state.push_back(c->refs);
            FreeReplayMap(sim);
            Uninstall(saved);
            gBase = nullptr;
            gBaseSize = 0;
        }
        ++ctx.cases;
        if (res[0].trace != res[1].trace || res[0].state != res[1].state || res[0].ret != res[1].ret) {
            size_t k = 0;
            while (k < res[0].state.size() && k < res[1].state.size() && res[0].state[k] == res[1].state[k]) ++k;
            ctx.Fail("Construct seed %u: ret %u/%u trace %zu/%zu state %zu/%zu (first diff at %zu: 0x%x/0x%x)", seed, res[0].ret, res[1].ret,
                     res[0].trace.size(), res[1].trace.size(), res[0].state.size(), res[1].state.size(), k,
                     k < res[0].state.size() ? res[0].state[k] : 0xdead, k < res[1].state.size() ? res[1].state[k] : 0xdead);
        }
    }
}

namespace {

// A constructed object (ours) with frames on its stack, built the same way on both sides; the services held are the
// fixture's fakes (each carries the reference the constructor took).
cTSTreeSim *Make(LifeFx &fx, uint32_t seed, void *memory)
{
    cTSTreeSim *sim = reinterpret_cast<cTSTreeSim *>(memory);
    sim->Construct();
    std::mt19937 r(seed + 99);
    const uint32_t frames = r() % 4;
    for (uint32_t i = 0; i < frames; ++i) RawFrames(sim->mStack.mFrames).push_back(&fx.frame[i]);
    return sim;
}

} // namespace

static void RunDestructor(difftest::Context &ctx, const char *name, uint32_t seeds, bool deleting)
{
    auto destructor = ctx.Original<void *(__thiscall *)(void *)>(0x5759f0);
    auto deletingDestructor = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x575c30);
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Result res[2];
        for (int side = 0; side < 2; ++side) {
            LifeFx fx;
            const bool withDebug = seed % 3 != 0;
            BuildLife(fx, seed, withDebug);
            Saved saved = Install(fx, withDebug);
            const uint32_t flags = deleting ? (seed & 3) : 0;
            void *memory = flags & 1 ? ::operator new(sizeof(cTSTreeSim)) : nullptr;
            alignas(16) uint8_t local[sizeof(cTSTreeSim)];
            if (!memory) memory = local;
            cTSTreeSim *sim = Make(fx, seed, memory);
            gTrace.clear();
            gHitCount = 0;
            gThrowAt = 0;
            // The replay map is freed by the destructor itself (through its original helper); keep a copy of nothing.
            void *ret;
            if (deleting) ret = side == 0 ? deletingDestructor(sim, flags) : sim->DeletingDestructor(flags);
            else ret = side == 0 ? destructor(sim) : (sim->Destruct(), static_cast<void *>(sim));
            res[side].ret = deleting ? (ret == sim ? 1 : 0) : 0; // the non-deleting destructor leaves eax as it was: nothing to compare
            res[side].trace = gTrace;
            if (!(flags & 1)) {
                const uint8_t *b = reinterpret_cast<const uint8_t *>(sim);
                for (uint32_t off = 0; off < 0x4c; ++off) res[side].state.push_back(off >= 0x1c && off < 0x28 ? 0u : b[off]);
                RawFrames(sim->mStack.mFrames).~vector();
            }
            for (Cell *c : {static_cast<Cell *>(&fx.context), &fx.gate, &fx.serviceC, &fx.debug, &fx.frame[0], &fx.frame[1], &fx.frame[2], &fx.frame[3]})
                res[side].state.push_back(c->refs);
            Uninstall(saved);
            gBase = nullptr;
            gBaseSize = 0;
        }
        ++ctx.cases;
        if (res[0].trace != res[1].trace || res[0].state != res[1].state || res[0].ret != res[1].ret) {
            size_t k = 0;
            while (k < res[0].state.size() && k < res[1].state.size() && res[0].state[k] == res[1].state[k]) ++k;
            ctx.Fail("%s seed %u: ret %u/%u trace %zu/%zu state %zu/%zu (first diff at %zu: 0x%x/0x%x)", name, seed, res[0].ret, res[1].ret,
                     res[0].trace.size(), res[1].trace.size(), res[0].state.size(), res[1].state.size(), k,
                     k < res[0].state.size() ? res[0].state[k] : 0xdead, k < res[1].state.size() ? res[1].state[k] : 0xdead);
        }
    }
}

DIFF_TEST(TreeSim_Destruct, 0x5759f0)
{
    RunDestructor(ctx, "Destruct", 3000, false);
}

DIFF_TEST(TreeSim_DeletingDestructor, 0x575c30)
{
    RunDestructor(ctx, "DeletingDestructor", 3000, true);
}
