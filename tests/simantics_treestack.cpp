// Differential tests for the SimAntics call stack (cTSTreeStack, src/simantics/TreeStack.cpp): the object is real
// memory with the game's vtable and a std::vector of fake frames (recording AddRef / Release / Write / Read), so the
// original's and our calls are compared on the references taken and given back, the vector's contents and capacity,
// the thrown errors and the stream calls.
#include "simantics_pool.h"
#include "simantics_treeio.h"

#include <windows.h>

namespace {

using namespace tsw;

struct FakeFrames {
    void *vt[48];
    Cell cell[8];
};

TSW_FN2(FfQuery, iid, out)
{
    Rec(self, 0, {iid});
    *reinterpret_cast<void **>(static_cast<uintptr_t>(out)) = self;
    return 1;
}
TSW_FN2(FfWrite, stream, arg)
{
    Rec(self, 38, {stream, arg});
    return Mix(gSeed, Off(self), 38) & 1;
}
inline uint32_t __fastcall FfRead(void *self, void *, uint32_t stream, uint32_t a, uint32_t b)
{
    Rec(self, 39, {stream, a, b});
    return 1;
}

struct StackFx {
    IoFixture io;
    FakeFrames frames;
    alignas(16) uint8_t raw[sizeof(cTSTreeStack)];
    cTSTreeStack *s;
    uint32_t sentinel;
};

void BuildStack(StackFx &fx, uint32_t seed)
{
    BuildIo(fx.io, seed);
    std::mt19937 r(seed * 7919u + 3);
    auto P = [](auto fn) { return reinterpret_cast<void *>(fn); };
    fx.frames.vt[0] = P(&FfQuery);
    fx.frames.vt[1] = P(&AddRefFake);
    fx.frames.vt[2] = P(&ReleaseFake);
    fx.frames.vt[38] = P(&FfWrite);
    fx.frames.vt[39] = P(&FfRead);
    for (Cell &c : fx.frames.cell) {
        c.vt = fx.frames.vt;
        c.refs = 5;
    }
    fx.s = new (fx.raw) cTSTreeStack();
    fx.s->mVTable = GameAddress<void>(cTSTreeStack::kVTable);
    const uint32_t count = r() % 6;
    // Built through the plain-pointer view: the setup must not call the frames' AddRef (the references the stack would
    // hold are the fakes' initial counts).
    RawFrames(fx.s->mFrames).reserve(count + r() % 3);
    for (uint32_t i = 0; i < count; ++i) RawFrames(fx.s->mFrames).push_back(&fx.frames.cell[i]);
    fx.sentinel = 0x5a5a5a5au;
    gBase = &fx;
    gBaseSize = sizeof(StackFx);
}

struct Result {
    uint32_t ret = 0;
    int kind = 0, thrown = -1;
    std::vector<uint32_t> trace, state;
};

template <typename Call> Result RunOne(uint32_t seed, Call call)
{
    StackFx *fx = new StackFx();
    std::memset(static_cast<void *>(fx), 0, sizeof(StackFx));
    BuildStack(*fx, seed);
    BuildPool(seed, seed % 7 == 0);
    gTrace.clear();
    gHitCount = 0;
    gThrowAt = 0;
    gW = nullptr;
    Result res;
    try {
        res.ret = call(*fx, seed);
    } catch (short code) {
        res.kind = 1;
        res.thrown = code;
    } catch (...) {
        res.kind = 3;
    }
    res.trace = gTrace;
    res.state.push_back(static_cast<uint32_t>(fx->s->mFrames.size()));
    res.state.push_back(static_cast<uint32_t>(fx->s->mFrames.capacity()));
    for (void *f : RawFrames(fx->s->mFrames)) res.state.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f))));
    for (const Cell &c : fx->frames.cell) res.state.push_back(c.refs);
    res.state.insert(res.state.end(), {fx->io.in.counter, fx->io.out.counter, fx->sentinel});
    // Frames the call took from the pool own vectors on the heap: their contents go into the state, then the vectors are
    // released (their pointers differ between the two runs and must not reach the pool's checksum).
    for (void *f : RawFrames(fx->s->mFrames)) {
        if (gPool.IndexOf(f) == 99 || gPool.IndexOf(f) < 0) continue;
        cTSTreeStackElem *fe = static_cast<cTSTreeStackElem *>(f);
        res.state.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fe->mOwner))));
        fe->mOwner = nullptr;
        for (std::vector<uint16_t> *v : {&fe->mArgs, &fe->mLocals}) {
            res.state.push_back(static_cast<uint32_t>(v->size()));
            for (uint16_t w : *v) res.state.push_back(w);
            std::vector<uint16_t>().swap(*v);
        }
    }
    CapturePool(res.state);
    RestorePool();
    RawFrames(fx->s->mFrames).~vector(); // no releases: the cells are going away
    delete fx;
    gBase = nullptr;
    gBaseSize = 0;
    gIo = nullptr;
    return res;
}

template <typename Orig, typename Ours>
void Run(difftest::Context &ctx, const char *name, uint32_t seeds, Orig original, Ours ours)
{
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Result a = RunOne(seed, original);
        Result b = RunOne(seed, ours);
        ++ctx.cases;
        if (a.kind != b.kind || a.thrown != b.thrown || a.ret != b.ret || a.trace != b.trace || a.state != b.state) {
            size_t k = 0;
            while (k < a.trace.size() && k < b.trace.size() && a.trace[k] == b.trace[k]) ++k;
            size_t j = 0;
            while (j < a.state.size() && j < b.state.size() && a.state[j] == b.state[j]) ++j;
            ctx.Fail("%s seed %u: kind %d/%d thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (diff at %zu) state %zu/%zu (diff at %zu: 0x%x/0x%x)", name, seed, a.kind,
                     b.kind, a.thrown, b.thrown, a.ret, b.ret, a.trace.size(), b.trace.size(), k, a.state.size(), b.state.size(), j,
                     j < a.state.size() ? a.state[j] : 0xdead, j < b.state.size() ? b.state[j] : 0xdead);
        }
    }
}

void *FrameArg(StackFx &fx, uint32_t seed)
{
    return seed % 4 == 0 ? nullptr : &fx.frames.cell[seed % 8];
}

} // namespace

// The stack answers with itself and takes a reference through its own vtable (slot 1, a plain `return 1` in the game):
// the object gets a vtable copy whose slot 1 counts, so a missing call shows.
uint32_t gStackAddRefs = 0;
uint32_t __fastcall CountStackAddRef(void *, void *)
{
    ++gStackAddRefs;
    return 1;
}

DIFF_TEST(TreeStack_QueryInterface, 0x577b20)
{
    auto fn = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x577b20);
    void *vt[16];
    std::memcpy(vt, GameAddress<void>(cTSTreeStack::kVTable), sizeof(vt));
    vt[1] = reinterpret_cast<void *>(&CountStackAddRef);
    auto iidFor = [](uint32_t seed) { return seed % 3 == 0 ? 0x7abea0d5u : (seed % 3 == 1 ? 1u : 77u); };
    Run(
        ctx, "QueryInterface", 3000,
        [&](StackFx &fx, uint32_t seed) {
            void *out = nullptr;
            fx.s->mVTable = vt;
            gStackAddRefs = 0;
            const uint32_t ok = fn(fx.s, iidFor(seed), &out) & 0xff;
            return ok | (out == fx.s ? 0x100u : 0u) | gStackAddRefs << 12;
        },
        [&](StackFx &fx, uint32_t seed) {
            void *out = nullptr;
            fx.s->mVTable = vt;
            gStackAddRefs = 0;
            const uint32_t ok = fx.s->QueryInterface(iidFor(seed), &out) & 0xff;
            return ok | (out == fx.s ? 0x100u : 0u) | gStackAddRefs << 12;
        });
}

DIFF_TEST(TreeStack_Reset, 0x577850)
{
    auto fn = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x577850);
    const uint32_t sizes[] = {0, 1, 3, 5, 6, 7, 8, 20, 100, 0x3fffffffu, 0x40000000u, 0xffffffffu};
    Run(
        ctx, "Reset", 4000, [&](StackFx &fx, uint32_t seed) { return (fn(fx.s, sizes[seed % 12]), 0u); },
        [&](StackFx &fx, uint32_t seed) { return (fx.s->Reset(sizes[seed % 12]), 0u); });
}

DIFF_TEST(TreeStack_Push, 0x577a70)
{
    auto fn = ctx.Original<void(__thiscall *)(void *, void *, uint32_t, void *)>(0x577a70);
    Run(
        ctx, "Push", 4000,
        [&](StackFx &fx, uint32_t seed) { return (fn(fx.s, FrameArg(fx, seed), seed & 1, nullptr), 0u); },
        [&](StackFx &fx, uint32_t seed) {
            fx.s->Push(static_cast<cTSTreeStackElem *>(FrameArg(fx, seed)), seed & 1, nullptr);
            return 0u;
        });
}

DIFF_TEST(TreeStack_Pop, 0x577a00)
{
    auto fn = ctx.Original<void(__thiscall *)(void *, uint32_t, void *)>(0x577a00);
    Run(
        ctx, "Pop", 3000, [&](StackFx &fx, uint32_t seed) { return (fn(fx.s, seed & 1, nullptr), 0u); },
        [&](StackFx &fx, uint32_t seed) { return (fx.s->Pop(seed & 1, nullptr), 0u); });
}

DIFF_TEST(TreeStack_Clear, 0x577e60)
{
    auto fn = ctx.Original<void(__thiscall *)(void *)>(0x577e60);
    Run(
        ctx, "Clear", 3000, [&](StackFx &fx, uint32_t) { return (fn(fx.s), 0u); }, [&](StackFx &fx, uint32_t) { return (fx.s->Clear(), 0u); });
}

DIFF_TEST(TreeStack_GetAt, 0x577390)
{
    auto fn = ctx.Original<void *(__thiscall *)(void *, int32_t)>(0x577390);
    Run(
        ctx, "GetAt", 4000,
        [&](StackFx &fx, uint32_t seed) { return Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn(fx.s, static_cast<int32_t>(seed % 9) - 2)))); },
        [&](StackFx &fx, uint32_t seed) { return Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fx.s->GetAt(static_cast<int32_t>(seed % 9) - 2)))); });
}

DIFF_TEST(TreeStack_Write, 0x5784c0)
{
    auto fn = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x5784c0);
    Run(
        ctx, "Write", 4000, [&](StackFx &fx, uint32_t) { return fn(fx.s, &fx.io.out, &fx.io.ownerWriter) & 0xff; },
        [&](StackFx &fx, uint32_t) { return static_cast<uint32_t>(fx.s->Write(&fx.io.out, &fx.io.ownerWriter)) & 0xff; });
}

DIFF_TEST(TreeStack_Read, 0x577b80)
{
    auto fn = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *, uint32_t)>(0x577b80);
    Run(
        ctx, "Read", 4000, [&](StackFx &fx, uint32_t seed) { return fn(fx.s, &fx.io.in, &fx.io.ownerReader, seed) & 0xff; },
        [&](StackFx &fx, uint32_t seed) { return static_cast<uint32_t>(fx.s->Read(&fx.io.in, &fx.io.ownerReader, seed)) & 0xff; });
}

DIFF_TEST(TreeStack_NewFrame, 0x576200)
{
    auto fn = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, void **)>(0x576200);
    Run(
        ctx, "NewFrame", 4000,
        [&](StackFx &fx, uint32_t seed) {
            void *out = nullptr;
            const uint32_t ok = fn(fx.s, seed % 3 == 0 ? 0x7a53b3ddu : (seed % 3 == 1 ? 1u : 77u), &out) & 0xff;
            return ok ^ Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out))) << 1;
        },
        [&](StackFx &fx, uint32_t seed) {
            void *out = nullptr;
            const uint32_t ok = fx.s->NewFrame(seed % 3 == 0 ? 0x7a53b3ddu : (seed % 3 == 1 ? 1u : 77u), &out) & 0xff;
            return ok ^ Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out))) << 1;
        });
}

DIFF_TEST(TreeStack_Destroy, 0x575d40)
{
    auto fn = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x575d40);
    auto make = [](StackFx &fx) {
        cTSTreeStack *o = new (::operator new(sizeof(cTSTreeStack))) cTSTreeStack();
        o->mVTable = GameAddress<void>(cTSTreeStack::kVTable);
        for (void *f : RawFrames(fx.s->mFrames)) RawFrames(o->mFrames).push_back(f);
        return o;
    };
    auto finish = [](cTSTreeStack *o, uint32_t flags) {
        if (flags & 1) return; // the callee freed it
        RawFrames(o->mFrames).~vector();
        ::operator delete(o, sizeof(cTSTreeStack));
    };
    Run(
        ctx, "Destroy", 3000,
        [&](StackFx &fx, uint32_t seed) {
            cTSTreeStack *o = make(fx);
            const uint32_t flags = seed & 3;
            const bool same = fn(o, flags) == o;
            finish(o, flags);
            return same ? 1u : 0u;
        },
        [&](StackFx &fx, uint32_t seed) {
            cTSTreeStack *o = make(fx);
            const uint32_t flags = seed & 3;
            const bool same = o->Destroy(flags) == o;
            finish(o, flags);
            return same ? 1u : 0u;
        });
}
