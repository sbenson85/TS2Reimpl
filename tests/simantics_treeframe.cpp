// Differential tests for the SimAntics stack frame (cTSTreeStackElem, src/simantics/TreeFrame.cpp): the field
// accessors, the argument and local vectors, the stream functions and the destructor. Each case builds two identical
// frames (real memory, the game's vtable, std::vector storage), runs the original on one and ours on the other, and
// compares results, the frames and the calls the fakes of simantics_treeio.h recorded.
#include "simantics_pool.h"
#include "simantics_treeio.h"

#include "rz/RZFastFixedAllocators.h"

#include <windows.h>

namespace {

using namespace tsw;

struct FrameObj {
    alignas(16) uint8_t raw[sizeof(cTSTreeStackElem)];
    cTSTreeStackElem *f;

    FrameObj(uint32_t seed, IoFixture &io)
    {
        std::mt19937 r(seed * 31337u + 17);
        f = new (raw) cTSTreeStackElem();
        f->mVTable = GameAddress<void>(cTSTreeStackElem::kVTable);
        f->mRefCount = 1 + r() % 3;
        const uint16_t words[] = {0, 1, 2, 0x7fff, 0x8000, 0x8001, 0xffff, 0x100, 0x1234, 0x9abc};
        f->mTreeWord = words[r() % 10];
        f->mStackObjectId = static_cast<uint16_t>(r());
        f->mNodeIndex = static_cast<uint16_t>(r());
        f->mPad0e[0] = static_cast<uint8_t>(r());
        f->mPad0e[1] = static_cast<uint8_t>(r());
        f->mResumeState = r();
        f->mByte16 = static_cast<uint8_t>(r());
        f->mPad17 = static_cast<uint8_t>(r());
        f->mWord18 = static_cast<uint16_t>(r());
        f->mWord1a = static_cast<uint16_t>(r());
        f->mWord1c = static_cast<uint16_t>(r());
        f->mByte1e = static_cast<uint8_t>(r());
        f->mPad1f = static_cast<uint8_t>(r());
        f->mOwner = (r() % 4 == 0) ? nullptr : static_cast<void *>(&io.owner);
        f->mArgCount = static_cast<uint8_t>(r() % 9);
        f->mLocalCount = static_cast<uint8_t>(r() % 9);
        f->mArgs.reserve(f->mArgCount + r() % 4);
        f->mArgs.resize(f->mArgCount);
        for (uint16_t &a : f->mArgs) a = static_cast<uint16_t>(r());
        f->mLocals.reserve(f->mLocalCount + r() % 4);
        f->mLocals.resize(f->mLocalCount);
        for (uint16_t &a : f->mLocals) a = static_cast<uint16_t>(r());
        // Spare capacity holds nonzero words (fresh heap is often zero): a grow within capacity must store its own zeros (DL).
        for (std::vector<uint16_t> *v : {&f->mArgs, &f->mLocals})
            for (size_t i = v->size(); i < v->capacity(); ++i) v->data()[i] = static_cast<uint16_t>(r() | 1);
    }
    ~FrameObj() { f->~cTSTreeStackElem(); }
    FrameObj(const FrameObj &) = delete;
    FrameObj &operator=(const FrameObj &) = delete;

    void Snapshot(std::vector<uint32_t> &s) const
    {
        const uint8_t *b = raw;
        for (int off = 4; off < 0x24; off += 4) {
            uint32_t v;
            std::memcpy(&v, b + off, 4);
            if (off == 0x20) v = Norm(v);
            s.push_back(v);
        }
        for (const std::vector<uint16_t> *v : {&f->mArgs, &f->mLocals}) {
            s.push_back(static_cast<uint32_t>(v->size()));
            s.push_back(static_cast<uint32_t>(v->capacity()));
            for (uint16_t w : *v) s.push_back(w);
        }
    }
};

struct Result {
    uint32_t ret = 0;
    int kind = 0;
    int thrown = -1;
    std::vector<uint32_t> trace, state;
};

template <typename Call> Result RunOne(uint32_t seed, Call call, bool original)
{
    (void)original;
    IoFixture io;
    BuildIo(io, seed);
    FrameObj frame(seed, io);
    gTrace.clear();
    gHitCount = 0;
    gThrowAt = 0;
    Result res;
    try {
        res.ret = call(*frame.f, io, seed);
    } catch (short code) {
        res.kind = 1;
        res.thrown = code;
    } catch (...) {
        res.kind = 3;
    }
    res.trace = gTrace;
    frame.Snapshot(res.state);
    res.state.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame.f->mOwner))));
    uint32_t slot = io.ownerSlot;
    if (slot == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame.f))) slot = 0xf4a3e001u; // QueryInterface stored `this`
    res.state.insert(res.state.end(), {io.in.counter, io.out.counter, slot, io.canary});
    gBase = nullptr;
    gBaseSize = 0;
    gIo = nullptr;
    return res;
}

template <typename Orig, typename Ours>
void Compare(difftest::Context &ctx, const char *name, uint32_t seeds, Orig original, Ours ours)
{
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Result a = RunOne(seed, original, true);
        Result b = RunOne(seed, ours, false);
        ++ctx.cases;
        if (a.kind != b.kind || a.thrown != b.thrown || a.ret != b.ret || a.trace != b.trace || a.state != b.state) {
            size_t k = 0;
            while (k < a.trace.size() && k < b.trace.size() && a.trace[k] == b.trace[k]) ++k;
            size_t j = 0;
            while (j < a.state.size() && j < b.state.size() && a.state[j] == b.state[j]) ++j;
            ctx.Fail("%s seed %u: kind %d/%d thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (diff at %zu) state %zu/%zu (diff at %zu: 0x%x/0x%x)", name, seed,
                     a.kind, b.kind, a.thrown, b.thrown, a.ret, b.ret, a.trace.size(), b.trace.size(), k, a.state.size(), b.state.size(), j,
                     j < a.state.size() ? a.state[j] : 0xdead, j < b.state.size() ? b.state[j] : 0xdead);
        }
    }
}

#define FRAME_TEST(name, rva, seeds, sig, origArgs, oursExpr, mask)                                                  \
    DIFF_TEST(TreeFrame_##name, rva)                                                                                 \
    {                                                                                                                \
        auto fn = ctx.Original<sig>(rva);                                                                            \
        Compare(                                                                                                     \
            ctx, #name, seeds, [&](cTSTreeStackElem &f, IoFixture &io, uint32_t seed) -> uint32_t {                  \
                (void)io; (void)seed; (void)f;                                                                       \
                return static_cast<uint32_t>(fn origArgs) & (mask);                                                  \
            },                                                                                                       \
            [&](cTSTreeStackElem &f, IoFixture &io, uint32_t seed) -> uint32_t {                                     \
                (void)io; (void)seed; (void)f;                                                                       \
                return static_cast<uint32_t>(oursExpr) & (mask);                                                     \
            });                                                                                                      \
    }

// Argument pickers (functions of the seed so both sides get the same).
uint32_t ArgIndex(uint32_t seed)
{
    return seed % 11;
}
uint32_t ArgValue(uint32_t seed)
{
    return Mix(seed, 3, 4) & 0xffff;
}

} // namespace

FRAME_TEST(GetTreeId, 0x577400, 3000, uint32_t(__thiscall *)(void *), (&f), f.GetTreeId(), 0xffffffffu)
FRAME_TEST(SetTreeId, 0x578420, 6000, uint32_t(__thiscall *)(void *, uint32_t), (&f, Mix(seed, 1, 2) | (seed & 1 ? 0xffff0000u : 0)), (f.SetTreeId(Mix(seed, 1, 2) | (seed & 1 ? 0xffff0000u : 0)), 0), 0)
FRAME_TEST(SetFlag16, 0x578290, 1000, uint32_t(__thiscall *)(void *, uint32_t), (&f, Mix(seed, 5, 2)), (f.SetFlag16(Mix(seed, 5, 2)), 0), 0)
FRAME_TEST(SetByte1e, 0x5782a0, 1000, uint32_t(__thiscall *)(void *, uint32_t), (&f, Mix(seed, 6, 2)), (f.SetByte1e(Mix(seed, 6, 2)), 0), 0)
FRAME_TEST(IsPaused, 0x576e50, 3000, uint32_t(__thiscall *)(void *), (&f), f.IsPaused(), 0xff)
FRAME_TEST(SetPaused, 0x578260, 3000, uint32_t(__thiscall *)(void *, uint32_t), (&f, seed & 3), (f.SetPaused(static_cast<uint8_t>(seed & 3)), 0), 0)
FRAME_TEST(SetArgCount, 0x578350, 6000, uint32_t(__thiscall *)(void *, uint32_t), (&f, seed % 40), (f.SetArgCount(static_cast<uint8_t>(seed % 40)), 0), 0)
FRAME_TEST(SetLocalCount, 0x5782e0, 6000, uint32_t(__thiscall *)(void *, uint32_t), (&f, seed % 40), (f.SetLocalCount(static_cast<uint8_t>(seed % 40)), 0), 0)
FRAME_TEST(GetArg, 0x5773c0, 4000, uint32_t(__thiscall *)(void *, int32_t), (&f, ArgIndex(seed)), f.GetArg(static_cast<int32_t>(ArgIndex(seed))), 0xffff)
FRAME_TEST(GetLocal, 0x5772c0, 4000, uint32_t(__thiscall *)(void *, int32_t), (&f, ArgIndex(seed)), f.GetLocal(static_cast<int32_t>(ArgIndex(seed))), 0xffff)
FRAME_TEST(SetArg, 0x5783c0, 4000, uint32_t(__thiscall *)(void *, int32_t, uint32_t), (&f, ArgIndex(seed), ArgValue(seed)), (f.SetArg(static_cast<int32_t>(ArgIndex(seed)), ArgValue(seed)), 0), 0)
// SetLocal has no bounds check (and writes through a null vector when there are no locals): only valid indices are used.
DIFF_TEST(TreeFrame_SetLocal, 0x5782b0)
{
    auto fn = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x5782b0);
    Compare(
        ctx, "SetLocal", 4000,
        [&](cTSTreeStackElem &f, IoFixture &, uint32_t seed) -> uint32_t {
            if (f.mLocalCount) fn(&f, ArgIndex(seed) % f.mLocalCount, ArgValue(seed));
            return 0;
        },
        [&](cTSTreeStackElem &f, IoFixture &, uint32_t seed) -> uint32_t {
            if (f.mLocalCount) f.SetLocal(ArgIndex(seed) % f.mLocalCount, ArgValue(seed));
            return 0;
        });
}
FRAME_TEST(GetArgAddress, 0x5779f0, 3000, uint32_t(__thiscall *)(void *, uint32_t),
           (&f, ArgIndex(seed) % (f.mArgCount ? f.mArgCount : 1)) - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f.mArgs.data())),
           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f.GetArgAddress(ArgIndex(seed) % (f.mArgCount ? f.mArgCount : 1)))) -
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f.mArgs.data())), 0xffffffffu)
FRAME_TEST(GetLocalAddress, 0x5779c0, 3000, uint32_t(__thiscall *)(void *, uint32_t),
           (&f, ArgIndex(seed) % (f.mLocalCount ? f.mLocalCount : 1)) - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f.mLocals.data())),
           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f.GetLocalAddress(ArgIndex(seed) % (f.mLocalCount ? f.mLocalCount : 1)))) -
               static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f.mLocals.data())), 0xffffffffu)

namespace {

// SetArgs takes a source array; a null pointer and a short count are among the cases.
uint16_t gSource[16];
const uint16_t *Source(uint32_t seed)
{
    for (int i = 0; i < 16; ++i) gSource[i] = static_cast<uint16_t>(Mix(seed, static_cast<uint32_t>(i), 9));
    return seed % 5 == 0 ? nullptr : gSource;
}

} // namespace

FRAME_TEST(SetArgs, 0x5783e0, 6000, uint32_t(__thiscall *)(void *, int32_t, const uint16_t *), (&f, static_cast<int32_t>(seed % 14) - 2, Source(seed)),
           (f.SetArgs(static_cast<int32_t>(seed % 14) - 2, Source(seed)), 0), 0)
FRAME_TEST(QueryInterface, 0x577b50, 3000, uint32_t(__thiscall *)(void *, uint32_t, void **),
           (&f, (seed % 3 == 0 ? 0x7a53b3ddu : (seed % 3 == 1 ? 1u : 0x1234u)), reinterpret_cast<void **>(&io.ownerSlot)),
           f.QueryInterface(seed % 3 == 0 ? 0x7a53b3ddu : (seed % 3 == 1 ? 1u : 0x1234u), reinterpret_cast<void **>(&io.ownerSlot)), 0xff)
FRAME_TEST(GetTreeName, 0x577420, 3000, uint32_t(__thiscall *)(void *, void *), (&f, &io.sink), (f.GetTreeName(&io.sink), 0), 0)
FRAME_TEST(Write, 0x578520, 8000, uint32_t(__thiscall *)(void *, void *, void *), (&f, &io.out, &io.ownerWriter), f.Write(&io.out, &io.ownerWriter), 0xff)
FRAME_TEST(Read, 0x577cf0, 12000, uint32_t(__thiscall *)(void *, void *, void *, uint32_t), (&f, &io.in, &io.ownerReader, Mix(seed, 7, 7)),
           f.Read(&io.in, &io.ownerReader, Mix(seed, 7, 7)), 0xff)
FRAME_TEST(Destroy, 0x575d70, 3000, uint32_t(__thiscall *)(void *, uint32_t), (&f, (seed % 2) * 4), (f.Destroy((seed % 2) * 4), 0), 0)

// Destroy with the block in a pool page (bit 0 of the flags gives it back; bit 2 does not): the page's use count,
// the rings and the pages the pool frees are compared.
DIFF_TEST(TreeFrame_DestroyPooled, 0x575d70)
{
    auto fn = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x575d70);
    const uint32_t kUsed[] = {1, 2, 0x40000034u, 0x40000033u, 0x40000040u, 5, 60};
    const uint32_t kFlags[] = {0, 1, 1, 1, 3, 4, 5};
    for (uint32_t seed = 0; seed < 8000; ++seed) {
        std::vector<uint32_t> state[2];
        uint32_t ret[2];
        for (int side = 0; side < 2; ++side) {
            BuildPool(seed);
            gIo = nullptr;
            gBase = nullptr;
            gTrace.clear();
            std::mt19937 r(seed * 17 + 3);
            const int page = static_cast<int>(r() % 2);
            const uint32_t k = r() % kBlocksPerPage;
            uint8_t *block = gPool.BlockAt(page, k);
            cTSTreeStackElem *f = new (block) cTSTreeStackElem();
            f->mVTable = GameAddress<void>(cTSTreeStackElem::kVTable);
            f->mRefCount = 1;
            f->mArgCount = static_cast<uint8_t>(r() % 9);
            f->mLocalCount = static_cast<uint8_t>(r() % 9);
            f->mArgs.resize(f->mArgCount);
            f->mLocals.resize(f->mLocalCount);
            gPool.Page(page)->used = kUsed[r() % 7];
            const uint32_t flags = kFlags[r() % 7];
            void *result = side == 0 ? fn(f, flags) : f->Destroy(flags);
            ret[side] = result == f ? 1 : 0;
            state[side].push_back(static_cast<uint32_t>(f->mArgs.size() + f->mLocals.size()));
            CapturePool(state[side]);
            state[side].insert(state[side].end(), gTrace.begin(), gTrace.end());
            RestorePool();
        }
        ++ctx.cases;
        if (ret[0] != ret[1] || state[0] != state[1]) {
            size_t k = 0;
            while (k < state[0].size() && k < state[1].size() && state[0][k] == state[1][k]) ++k;
            ctx.Fail("DestroyPooled seed %u: ret %u/%u state %zu/%zu (first diff at %zu: 0x%x/0x%x)", seed, ret[0], ret[1], state[0].size(),
                     state[1].size(), k, k < state[0].size() ? state[0][k] : 0xdead, k < state[1].size() ? state[1][k] : 0xdead);
        }
    }
}
