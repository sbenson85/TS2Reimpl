// Differential test for cTSObject::vf246 (0x53e560), the node execution the interpreter's RunNode slot reaches: the
// object's slots 332 / 333, the sim subobject's slots 38 / 39, the gate's word 0x35 and the notification to the service
// hub, on fake objects. The unhandled path into opcode 0x1c (PrimRObject::Prim1C, verified on its own) is not exercised:
// nodes nobody handles use another opcode and raise error 5.
#include "simantics/TreeSimRunNode.h"
#include "simantics_treesim_world.h"

#include <climits>
#include <windows.h>

namespace {

using namespace tsw;

constexpr uint32_t kHubGlobal = 0x10c9378;

// The game's std::string allocates through the allocator service it caches in a function-local static (cache 0x10c95d4,
// guard 0x10c95d8): the original builds "edithConfigFlags" (16 characters) on the heap that way. A fake whose allocate
// (slot 3) and free (slot 4) go to the process heap stands in; ours allocates with the toolchain's operator new.
void *__fastcall FakeAlloc3(void *, void *, uint32_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}
void __fastcall FakeFree4(void *, void *, void *mem)
{
    HeapFree(GetProcessHeap(), 0, mem);
}
struct AllocatorFx {
    void *vt[8] = {};
    void *object = nullptr;
    void *savedCache = nullptr;
    int savedGuard = 0;
    AllocatorFx()
    {
        vt[3] = reinterpret_cast<void *>(&FakeAlloc3);
        vt[4] = reinterpret_cast<void *>(&FakeFree4);
        object = vt;
        savedCache = *GameAddress<void *>(0x10c95d4);
        savedGuard = *GameAddress<int>(0x10c95d8);
        *GameAddress<void *>(0x10c95d4) = &object;
        *GameAddress<int>(0x10c95d8) = INT_MIN;
    }
    ~AllocatorFx()
    {
        *GameAddress<void *>(0x10c95d4) = savedCache;
        *GameAddress<int>(0x10c95d8) = savedGuard;
    }
};

struct GateWordCell : Cell {
    uint32_t word35;
};

struct RunFx {
    alignas(16) uint8_t object[0x2d0];
    void *vtObject[400];
    void *vtSim[64];
    void *vtGate[32];
    void *vtHub[8];
    void *vtSinkObj[8];
    void *vtNode[32];
    GateWordCell gate;
    NodeCell node;
    FrameCell frame;
    Cell hub, sink;
    uint32_t canary;
};

RunFx *gFx = nullptr;

uint32_t __fastcall ObjRunPrimitive(void *self, void *, uint32_t frame, uint32_t node, int32_t *result)
{
    Rec(self, 332, {frame, node, result ? 1u : 0u});
    const uint32_t v = Mix(gSeed, 332, 1);
    if (v % 3 == 0) gFx->gate.word35 ^= 1u << (v % 7); // the configuration flags change while the node runs
    if (v % 4 != 0) *result = static_cast<int32_t>(Mix(gSeed, 332, 2) % 7) - 1;
    return v % 5 != 0;
}
TSW_FN0(ObjAfter)
{
    Rec(self, 333, {});
    return 0;
}
uint32_t __fastcall SimEdith(void *self, void *, uint32_t frame, uint32_t node, int32_t *result)
{
    Rec(self, 38, {frame, node, result ? 1u : 0u});
    const uint32_t v = Mix(gSeed, 38, 1);
    if (v % 4 == 0) *result = static_cast<int32_t>(v % 5);
    return v % 3 == 0;
}
TSW_FN2(SimAfterEdith, frame, node)
{
    Rec(self, 39, {frame, node});
    return 0;
}
TSW_FN1(GateWord, i)
{
    Rec(self, 15, {i});
    return static_cast<GateWordCell *>(self)->word35 & 0xffff;
}
uint32_t __fastcall HubGet(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    Rec(self, 5, {a, b, out ? 1u : 0u});
    ++gFx->sink.refs;
    *out = &gFx->sink;
    return 1;
}
TSW_FN2(SinkSet, name, value)
{
    Rec(self, 3, {});
    const std::string *text = reinterpret_cast<const std::string *>(static_cast<uintptr_t>(name));
    gTrace.push_back(static_cast<uint32_t>(text->size()));
    for (char c : *text) gTrace.push_back(static_cast<uint8_t>(c));
    const uint32_t *any = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(value));
    gTrace.push_back(any[0]);
    gTrace.push_back(any[1]);
    return 0;
}

void BuildRun(RunFx &fx, uint32_t seed)
{
    std::memset(&fx, 0, sizeof(fx));
    gSeed = seed;
    gFx = &fx;
    std::mt19937 r(seed * 31 + 7);
    auto P = [](auto fn) { return reinterpret_cast<void *>(fn); };
    fx.vtObject[332] = P(&ObjRunPrimitive);
    fx.vtObject[333] = P(&ObjAfter);
    fx.vtSim[38] = P(&SimEdith);
    fx.vtSim[39] = P(&SimAfterEdith);
    fx.vtGate[15] = P(&GateWord);
    fx.vtHub[5] = P(&HubGet);
    fx.vtSinkObj[1] = P(&AddRefFake);
    fx.vtSinkObj[2] = P(&ReleaseFake);
    fx.vtSinkObj[3] = P(&SinkSet);
    fx.vtNode[3] = P(&NdOpcode);
    fx.vtNode[16] = P(&NdParam);
    TreeRunNodeObject *obj = reinterpret_cast<TreeRunNodeObject *>(fx.object);
    for (uint8_t &b : fx.object) b = static_cast<uint8_t>(r());
    obj->mVTable = fx.vtObject;
    *reinterpret_cast<void ***>(fx.object + 4) = fx.vtSim;
    fx.gate.vt = fx.vtGate;
    fx.gate.refs = 1;
    fx.gate.word35 = r() & 0xffff;
    obj->mGate = &fx.gate;
    fx.hub.vt = fx.vtHub;
    fx.sink.vt = fx.vtSinkObj;
    fx.sink.refs = 1;
    fx.node.vt = fx.vtNode;
    fx.node.refs = 1;
    // Never opcode 0x1c: an unhandled node then raises error 5.
    uint16_t opcode = static_cast<uint16_t>(r() % 0x7f);
    if (opcode == 0x1c) opcode = 0x1d;
    fx.node.opcode = opcode;
    for (uint8_t &b : fx.node.param) b = static_cast<uint8_t>(r());
    gBase = &fx;
    gBaseSize = sizeof(fx);
}

struct Result {
    uint32_t ret = 0;
    int kind = 0, thrown = -1;
    std::vector<uint32_t> trace, state;
};

template <typename Call> Result RunOne(uint32_t seed, Call call)
{
    if (DumpRequested()) std::printf("runnode seed %u\n", seed), std::fflush(stdout), gVerbose = seed >= 1;
    RunFx *fx = new RunFx();
    BuildRun(*fx, seed);
    AllocatorFx allocator;
    void *savedHub = *GameAddress<void *>(kHubGlobal);
    *GameAddress<void *>(kHubGlobal) = &fx->hub;
    gTrace.clear();
    gHitCount = 0;
    gThrowAt = (seed % 9 == 0) ? 1 + seed % 11 : 0;
    gW = nullptr;
    Result res;
    try {
        res.ret = call(*fx);
    } catch (short code) {
        res.kind = 1;
        res.thrown = code;
    } catch (...) {
        res.kind = 3;
    }
    res.trace = gTrace;
    res.state = {fx->gate.refs, fx->gate.word35, fx->sink.refs, fx->node.refs};
    *GameAddress<void *>(kHubGlobal) = savedHub;
    delete fx;
    gBase = nullptr;
    gBaseSize = 0;
    gFx = nullptr;
    return res;
}

} // namespace

DIFF_TEST(TreeSim_RunNode, 0x53e560)
{
    auto fn = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x53e560);
    for (uint32_t seed = 0; seed < 20000; ++seed) {
        Result a = RunOne(seed, [&](RunFx &fx) {
            if (gVerbose) std::printf("  --- original\n");
            return fn(fx.object, &fx.frame, &fx.node);
        });
        Result b = RunOne(seed, [&](RunFx &fx) {
            if (gVerbose) std::printf("  --- ours\n");
            return static_cast<uint32_t>(reinterpret_cast<TreeRunNodeObject *>(fx.object)->RunNode(&fx.frame, &fx.node));
        });
        ++ctx.cases;
        if (a.kind != b.kind || a.thrown != b.thrown || a.ret != b.ret || a.trace != b.trace || a.state != b.state) {
            size_t k = 0;
            while (k < a.trace.size() && k < b.trace.size() && a.trace[k] == b.trace[k]) ++k;
            ctx.Fail("RunNode seed %u: kind %d/%d thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (diff at %zu: 0x%x/0x%x) state %s", seed, a.kind, b.kind,
                     a.thrown, b.thrown, a.ret, b.ret, a.trace.size(), b.trace.size(), k, k < a.trace.size() ? a.trace[k] : 0xdead,
                     k < b.trace.size() ? b.trace[k] : 0xdead, a.state == b.state ? "same" : "DIFFERENT");
        }
    }
}
