// Differential tests for cTSBehaviorTree (src/simantics/cTSBehaviorTree.*): the interface slots QueryInterface
// 0x46d0e0, GetGZCLSID 0x46a120, vf12/vf14/vf15/vf17/vf20/vf22 (0x46bd30, 0x46a7d0, 0x46e150, 0x46e0d0, 0x46a190,
// 0x46a170), the node list (AddNode 0x469910, Clear 0x469c90, IsJumpTarget 0x46c3f0, CheckNode 0x46c470, ExportTo
// 0x469cb0, SetName 0x46e170, AnalyseFlags 0x469a30), Write 0x46ef80, Read 0x46d530, the constructor 0x46a020, the
// deleting destructor 0x469740 and the two vector helpers 0x176f00 / 0x14c060.
//
// One work environment holds the tree buffer and 48 mock objects (nodes, sub-trees, streams, a sink, a provider,
// a validator). Their vtables are recording fakes: every call appends (code, object id, arguments) to a trace, so
// the order and arguments of every virtual call are compared, not just the results. Each case runs the original and
// then ours from the same initial state (the work environment is rebuilt from the same plan: bytes, name string,
// freshly allocated vector storage) and compares the return value (under a mask), the final state (tree bytes, name,
// vector contents and capacity, the mock objects), the trace and the node/param pools (see
// simantics_params_fixture.h). Read, Write and AddNode also run real nodes: Read and AddNode allocate them from the
// image's pools, so the node and param pools are the fakes of the fixture.
#include "difftest.h"
#include "free_scope.h"
#include "game/game.h"
#include "game/vtable.h"
#include "simantics/cTSBehaviorTree.h"
#include "simantics_params_fixture.h"

#include <Windows.h>

#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace simfix;

// ----- mock objects ----------------------------------------------------------------------------------

struct Obj {
    const void *vt;      // +0x00
    uint32_t kind;       // +0x04
    uint32_t opcode;     // +0x08 (full 32 bits: garbage above the low 16 on purpose)
    uint32_t trueT;      // +0x0c
    uint32_t falseT;     // +0x10
    uint32_t flags[2];   // +0x14: a sub-tree's flags before / after its analysis
    uint32_t flagsNow;   // +0x1c
    uint32_t ret;        // +0x20
    uint8_t param[0x18]; // +0x24
    uint32_t pad[1];     // +0x3c
};
static_assert(sizeof(Obj) == 0x40, "Obj");

constexpr int kMaxObjs = 48;
constexpr int kSubTrees = 32;   // objs[32..39]: sub-trees handed out by the provider
constexpr int kStreamObj = 40;
constexpr int kSinkObj = 41;
constexpr int kProviderObj = 42;
constexpr int kValidatorObj = 43;
constexpr int kNameObj = 44; // an object handed to SetName

struct Env {
    alignas(8) uint8_t tree[0x60];
    Obj objs[kMaxObjs];
};

Env gWork;
std::vector<uint32_t> gTrace;
uint32_t gRand = 1;
void *gFakeNodes[64];
uint32_t gFakeN = 0;     // how many nodes the tree's fake slot 22 knows
int32_t gFakeCount = 0;  // what the tree's fake slot 20 returns (low 16 bits)
uint32_t gFakeCountHigh = 0;
uint32_t gErrAt = 0xffffffffu; // slot-18 calls from this index on report an error
uint32_t gErrCalls = 0;
uint32_t gScript6[2] = {0, 0}; // the first two slot-6 reads of Read: format word and node count
uint32_t g6Calls = 0;
void *gTreeVT[38];
void *gTree2VT[13];
void *gNodeVT[26];
void *gStreamVT[24];
void *gSinkVT[32];
void *gProvVT[32];
void *gValVT[16];

uint32_t NextRand()
{
    gRand ^= gRand << 13;
    gRand ^= gRand >> 17;
    gRand ^= gRand << 5;
    return gRand;
}

uint32_t Norm(const void *p)
{
    const uint8_t *q = static_cast<const uint8_t *>(p);
    if (!q) return 0;
    if (q >= gWork.tree && q < gWork.tree + 0x60) return 0x100 + static_cast<uint32_t>(q - gWork.tree);
    const uint8_t *o = reinterpret_cast<const uint8_t *>(gWork.objs);
    if (q >= o && q < o + sizeof(gWork.objs)) return 0x1000 + static_cast<uint32_t>(q - o);
    uint32_t idx = 0;
    for (PoolBase *pool : ActivePools()) {
        if (q >= pool->region && q < pool->region + kPages * kPageBytes) return 0x100000 * (idx + 1) + static_cast<uint32_t>(q - pool->region);
        ++idx;
    }
    return 0x3000;
}

void T(uint32_t code, const void *self) { gTrace.push_back(code); gTrace.push_back(Norm(self)); }
template <typename... A> void T(uint32_t code, const void *self, A... a)
{
    T(code, self);
    (gTrace.push_back(static_cast<uint32_t>(a)), ...);
}

// -- node
uint32_t __fastcall NAddRef(Obj *s, void *) { T(0x101, s); return 1; }
uint32_t __fastcall NRelease(Obj *s, void *) { T(0x102, s); return 1; }
uint32_t __fastcall NOpcode(Obj *s, void *) { T(0x103, s); return s->opcode; }
void __fastcall NSetOpcode(Obj *s, void *, uint32_t v) { T(0x105, s, v); }
uint32_t __fastcall NTrue(Obj *s, void *) { T(0x106, s); return s->trueT; }
uint32_t __fastcall NFalse(Obj *s, void *) { T(0x108, s); return s->falseT; }
void __fastcall NSetTrue(Obj *s, void *, uint32_t v) { T(0x10a, s, v); }
void __fastcall NSetFalse(Obj *s, void *, uint32_t v) { T(0x10b, s, v); }
void *__fastcall NCopyTo(Obj *s, void *, void *dest)
{
    T(0x10f, s, Norm(dest));
    std::memcpy(dest, s->param, 0x18);
    return dest;
}
uint8_t *__fastcall NGetParam(Obj *s, void *) { T(0x110, s); return s->param; }
void __fastcall NWrite(Obj *s, void *, void *stream) { T(0x114, s, Norm(stream)); }
void __fastcall NRead(Obj *s, void *, void *stream, int32_t v) { T(0x115, s, Norm(stream), v); }

// -- tree (the main tree buffer and the sub-trees)
uint32_t __fastcall TAddRef(void *s, void *) { T(0x201, s); return 1; }
uint32_t __fastcall TRelease(void *s, void *) { T(0x202, s); return 1; }
uint32_t __fastcall TFlags(Obj *s, void *) { T(0x210, s); return s->flagsNow; }
uint32_t __fastcall TCount(void *s, void *)
{
    T(0x214, s);
    return (gFakeCountHigh << 16) | (static_cast<uint32_t>(gFakeCount) & 0xffff);
}
void *__fastcall TGetNode(void *s, void *, int32_t i)
{
    T(0x216, s, i);
    return static_cast<uint32_t>(i) < gFakeN ? gFakeNodes[i] : nullptr;
}
void __fastcall TAnalyse(Obj *s, void *, void *provider)
{
    T(0x222, s, Norm(provider));
    s->flagsNow = s->flags[1];
}

// -- provider and validator
void *__fastcall ProvGet(Obj *s, void *, uint32_t opcode)
{
    T(0x517, s, opcode);
    uint32_t k = opcode % 9;
    return k == 8 ? nullptr : &gWork.objs[kSubTrees + k];
}
uint32_t __fastcall ValCheck(Obj *s, void *, void *node)
{
    T(0x608, s, Norm(node));
    return s->ret;
}

// -- sink (ExportTo)
void __fastcall SinkBegin(Obj *s, void *) { T(0x41a, s); }
void __fastcall SinkVersion(Obj *s, void *, uint32_t v) { T(0x407, s, v); }
void __fastcall SinkType(Obj *s, void *, uint32_t v) { T(0x40b, s, v); }
void __fastcall SinkArgs(Obj *s, void *, uint32_t v) { T(0x40d, s, v); }
void __fastcall SinkLocals(Obj *s, void *, uint32_t v) { T(0x40f, s, v); }
void __fastcall SinkFlags(Obj *s, void *, uint32_t v) { T(0x411, s, v); }
void __fastcall SinkCache(Obj *s, void *, uint32_t v) { T(0x413, s, v); }
void __fastcall SinkTreeVersion(Obj *s, void *, uint32_t v) { T(0x409, s, v); }
void __fastcall SinkNode(Obj *s, void *, uint32_t op, uint32_t t, uint32_t f, const uint32_t *param)
{
    T(0x418, s, op, t, f);
    for (int i = 0; i < 6; ++i) gTrace.push_back(param[i]);
}

// -- streams (reading and writing)
uint8_t __fastcall StrErr(void *s, void *)
{
    T(0x312, s);
    return gErrCalls++ >= gErrAt ? 1 : 0;
}
void __fastcall RdByte(void *s, void *, void *out)
{
    T(0x305, s, Norm(out));
    *static_cast<uint8_t *>(out) = static_cast<uint8_t>(NextRand() >> 7);
}
void __fastcall RdWord(void *s, void *, void *out)
{
    T(0x306, s, Norm(out));
    uint32_t n = g6Calls++;
    uint16_t v;
    if (n < 2) {
        v = static_cast<uint16_t>(gScript6[n]);
    } else {
        uint32_t r = NextRand();
        v = static_cast<uint16_t>(r % 6 == 0 ? 0 : r >> 8);
        // The opcode word of a node block read by the node's Read: mostly real opcodes (block offset 6).
        const uint8_t *q = static_cast<const uint8_t *>(out);
        for (PoolBase *pool : ActivePools())
            if (q >= pool->region && q < pool->region + kPages * kPageBytes && ((q - pool->region) & 0xf) == 6 && r % 8 != 0)
                v = static_cast<uint16_t>(((r >> 8) % 0x7e) | (r % 8 == 1 ? 0x8000 : 0));
    }
    std::memcpy(out, &v, 2);
}
void __fastcall RdWord7(void *s, void *, void *out)
{
    T(0x307, s, Norm(out));
    uint32_t r = NextRand();
    uint16_t v = static_cast<uint16_t>(r % 6 == 0 ? 0xffff : r >> 8);
    std::memcpy(out, &v, 2);
}
void __fastcall RdDword(void *s, void *, void *out)
{
    T(0x308, s, Norm(out));
    uint32_t v = NextRand();
    std::memcpy(out, &v, 4);
}
void __fastcall RdBlock(void *s, void *, void *out, uint32_t len)
{
    T(0x311, s, Norm(out), len);
    uint8_t *b = static_cast<uint8_t *>(out);
    uint32_t r = NextRand();
    static constexpr uint32_t kLens[] = {0, 1, 15, 16, 17, 31, 32, 63};
    uint32_t n = r % 3 == 0 ? kLens[(r >> 4) % 8] : (r >> 4) % 64;
    for (uint32_t i = 0; i < len; ++i) b[i] = static_cast<uint8_t>(NextRand());
    for (uint32_t i = 0; i < n && i < len; ++i) b[i] = static_cast<uint8_t>('a' + NextRand() % 26);
    if (n < len) b[n] = 0;
}
void __fastcall WrByte(void *s, void *, uint32_t v) { T(0x305, s, v); }
void __fastcall WrWord(void *s, void *, uint32_t v) { T(0x306, s, v); }
void __fastcall WrWord7(void *s, void *, uint32_t v) { T(0x307, s, v); }
void __fastcall WrDword(void *s, void *, uint32_t v) { T(0x308, s, v); }
void __fastcall WrBlock(void *s, void *, const uint8_t *p, uint32_t len)
{
    T(0x311, s, len);
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, p + i, 4);
        gTrace.push_back(w);
    }
}

void InitVTables(const difftest::Context &ctx, bool reading)
{
    std::memcpy(gTreeVT, ctx.image.At(cTSBehaviorTree::kVTable), sizeof(gTreeVT));
    std::memcpy(gTree2VT, ctx.image.At(cTSBehaviorTree::kVTable2), sizeof(gTree2VT));
    gTreeVT[1] = reinterpret_cast<void *>(&TAddRef);
    gTreeVT[2] = reinterpret_cast<void *>(&TRelease);
    gTreeVT[16] = reinterpret_cast<void *>(&TFlags);
    gTreeVT[20] = reinterpret_cast<void *>(&TCount);
    gTreeVT[22] = reinterpret_cast<void *>(&TGetNode);
    gTreeVT[34] = reinterpret_cast<void *>(&TAnalyse);
    gTree2VT[1] = reinterpret_cast<void *>(&TAddRef);
    std::memset(gNodeVT, 0, sizeof(gNodeVT));
    gNodeVT[1] = reinterpret_cast<void *>(&NAddRef);
    gNodeVT[2] = reinterpret_cast<void *>(&NRelease);
    gNodeVT[3] = reinterpret_cast<void *>(&NOpcode);
    gNodeVT[5] = reinterpret_cast<void *>(&NSetOpcode);
    gNodeVT[6] = reinterpret_cast<void *>(&NTrue);
    gNodeVT[8] = reinterpret_cast<void *>(&NFalse);
    gNodeVT[10] = reinterpret_cast<void *>(&NSetTrue);
    gNodeVT[11] = reinterpret_cast<void *>(&NSetFalse);
    gNodeVT[15] = reinterpret_cast<void *>(&NCopyTo);
    gNodeVT[16] = reinterpret_cast<void *>(&NGetParam);
    gNodeVT[20] = reinterpret_cast<void *>(&NWrite);
    gNodeVT[21] = reinterpret_cast<void *>(&NRead);
    std::memset(gStreamVT, 0, sizeof(gStreamVT));
    gStreamVT[5] = reading ? reinterpret_cast<void *>(&RdByte) : reinterpret_cast<void *>(&WrByte);
    gStreamVT[6] = reading ? reinterpret_cast<void *>(&RdWord) : reinterpret_cast<void *>(&WrWord);
    gStreamVT[7] = reading ? reinterpret_cast<void *>(&RdWord7) : reinterpret_cast<void *>(&WrWord7);
    gStreamVT[8] = reading ? reinterpret_cast<void *>(&RdDword) : reinterpret_cast<void *>(&WrDword);
    gStreamVT[17] = reading ? reinterpret_cast<void *>(&RdBlock) : reinterpret_cast<void *>(&WrBlock);
    gStreamVT[18] = reinterpret_cast<void *>(&StrErr);
    std::memset(gSinkVT, 0, sizeof(gSinkVT));
    gSinkVT[26] = reinterpret_cast<void *>(&SinkBegin);
    gSinkVT[7] = reinterpret_cast<void *>(&SinkVersion);
    gSinkVT[11] = reinterpret_cast<void *>(&SinkType);
    gSinkVT[13] = reinterpret_cast<void *>(&SinkArgs);
    gSinkVT[15] = reinterpret_cast<void *>(&SinkLocals);
    gSinkVT[17] = reinterpret_cast<void *>(&SinkFlags);
    gSinkVT[19] = reinterpret_cast<void *>(&SinkCache);
    gSinkVT[9] = reinterpret_cast<void *>(&SinkTreeVersion);
    gSinkVT[24] = reinterpret_cast<void *>(&SinkNode);
    std::memset(gProvVT, 0, sizeof(gProvVT));
    gProvVT[23] = reinterpret_cast<void *>(&ProvGet);
    std::memset(gValVT, 0, sizeof(gValVT));
    gValVT[8] = reinterpret_cast<void *>(&ValCheck);
}

// ----- the plan: what the tree looks like before a call -------------------------------------------------

struct Plan {
    std::vector<int> elems;  // object indices in the node vector
    uint32_t cap = 0;        // vector capacity (>= elems.size())
    std::string name;        // the name string
    int32_t fakeCount = 0;   // what slot 20 returns
    uint32_t fakeHigh = 0;
    bool fakeSlots = true;   // whether the tree uses the fake slots 20/22 (otherwise the image's own)
};

struct Init {
    uint8_t tree[0x60];
    Obj objs[kMaxObjs];
};

void RandomInit(Init &in, uint32_t seed)
{
    std::mt19937 rng(seed);
    for (auto &x : in.tree) x = static_cast<uint8_t>(rng());
    for (int i = 0; i < kMaxObjs; ++i) {
        Obj &o = in.objs[i];
        std::memset(&o, 0, sizeof(o));
        o.kind = static_cast<uint32_t>(i);
        o.opcode = rng();
        o.trueT = rng();
        o.falseT = rng();
        o.flags[0] = rng();
        o.flags[1] = rng();
        o.flagsNow = o.flags[0];
        o.ret = rng();
        for (auto &x : o.param) x = static_cast<uint8_t>(rng());
        o.vt = i < kSubTrees ? static_cast<const void *>(gNodeVT) : nullptr;
        if (i >= kSubTrees && i < 40) o.vt = gTreeVT;
        if (i == kStreamObj) o.vt = gStreamVT;
        if (i == kSinkObj) o.vt = gSinkVT;
        if (i == kProviderObj) o.vt = gProvVT;
        if (i == kValidatorObj) o.vt = gValVT;
        if (i == kNameObj) o.vt = gNodeVT; // unused
    }
}

uint32_t gStorageBytes = 0;

// Rebuilds the work environment from `in` and `plan`: the tree bytes with both vtables, the name string, the
// vector in freshly allocated storage.
void Prepare(const Init &in, const Plan &plan)
{
    std::memcpy(gWork.tree, in.tree, 0x60);
    std::memcpy(gWork.objs, in.objs, sizeof(gWork.objs));
    cTSBehaviorTree *t = reinterpret_cast<cTSBehaviorTree *>(gWork.tree);
    t->mVTable = gTreeVT;
    t->mVTable2 = gTree2VT;
    t->mOwned3C = &gWork.objs[kValidatorObj];
    t->mName.mVTable = GameAddress<void>(cRZString::kVTable);
    new (&t->mName.mString) std::string(plan.name);
    uint32_t cap = plan.cap < plan.elems.size() ? static_cast<uint32_t>(plan.elems.size()) : plan.cap;
    if (cap == 0) {
        t->mNodes.mFirst = t->mNodes.mLast = t->mNodes.mEnd = nullptr;
    } else {
        void **st = static_cast<void **>(::operator new(cap * 4));
        for (uint32_t i = 0; i < cap; ++i) st[i] = i < plan.elems.size() ? static_cast<void *>(&gWork.objs[plan.elems[i]]) : reinterpret_cast<void *>(0xdead0000u + i);
        t->mNodes.mFirst = st;
        t->mNodes.mLast = st + plan.elems.size();
        t->mNodes.mEnd = st + cap;
    }
    gFakeN = 0;
    for (int e : plan.elems)
        if (gFakeN < 64) gFakeNodes[gFakeN++] = &gWork.objs[e];
    gFakeCount = plan.fakeCount;
    gFakeCountHigh = plan.fakeHigh;
    if (!plan.fakeSlots) {
        gTreeVT[20] = GameAddress<void>(0x46a190);
        gTreeVT[22] = GameAddress<void>(0x46a170);
    } else {
        gTreeVT[20] = reinterpret_cast<void *>(&TCount);
        gTreeVT[22] = reinterpret_cast<void *>(&TGetNode);
    }
    gTrace.clear();
    gErrCalls = 0;
    g6Calls = 0;
}

struct State {
    std::vector<uint8_t> tree;
    std::string name;
    size_t nameCap = 0;
    std::vector<uint32_t> elems;
    uint32_t cap = 0;
    std::vector<uint8_t> objs;
    bool operator==(const State &o) const
    {
        return tree == o.tree && name == o.name && nameCap == o.nameCap && elems == o.elems && cap == o.cap && objs == o.objs;
    }
};

// Captures the state and tears the environment down (string, vector storage).
State Finish()
{
    cTSBehaviorTree *t = reinterpret_cast<cTSBehaviorTree *>(gWork.tree);
    State s;
    s.tree.assign(gWork.tree, gWork.tree + 0x60);
    std::memset(s.tree.data() + 0x2c, 0, 12);   // vector pointers: compared below, normalised
    std::memset(s.tree.data() + 0x44, 0, 0x18); // string internals: compared below
    s.name = t->mName.mString;
    s.nameCap = t->mName.mString.capacity();
    for (void **p = t->mNodes.mFirst; p != t->mNodes.mLast; ++p) s.elems.push_back(Norm(*p));
    s.cap = static_cast<uint32_t>(t->mNodes.mEnd - t->mNodes.mFirst);
    s.objs.assign(reinterpret_cast<uint8_t *>(gWork.objs), reinterpret_cast<uint8_t *>(gWork.objs) + sizeof(gWork.objs));
    t->mName.mString.~basic_string();
    if (t->mNodes.mFirst) ::operator delete(t->mNodes.mFirst);
    t->mNodes.mFirst = t->mNodes.mLast = t->mNodes.mEnd = nullptr;
    return s;
}

template <typename Orig, typename Ours>
void Run(difftest::Context &ctx, const char *what, uint32_t seed, const Init &in, const Plan &plan, Orig orig, Ours ours,
         uint32_t retMask = 0xffffffffu)
{
    Snapshot start = Take();
    Prepare(in, plan);
    uint32_t ra = orig(gWork.tree);
    State sa = Finish();
    std::vector<uint32_t> ta = gTrace;
    Snapshot pa = Take();
    Restore(start);
    Prepare(in, plan);
    uint32_t rb = ours(gWork.tree);
    State sb = Finish();
    ++ctx.cases;
    if ((ra & retMask) != (rb & retMask)) ctx.Fail("%s seed %u: original returned %08x, ours %08x", what, seed, ra, rb);
    if (!(sa == sb)) {
        const char *which = "tree bytes";
        if (sa.tree == sb.tree) which = sa.name != sb.name || sa.nameCap != sb.nameCap ? "name" : sa.elems != sb.elems || sa.cap != sb.cap ? "vector" : "mock objects";
        ctx.Fail("%s seed %u: final state differs (%s)", what, seed, which);
    }
    if (ta != gTrace) ctx.Fail("%s seed %u: recorded calls differ (%zu vs %zu words)", what, seed, ta.size(), gTrace.size());
    if (!(pa == Take())) ctx.Fail("%s seed %u: pool state differs", what, seed);
}

cTSBehaviorTree *Tree() { return reinterpret_cast<cTSBehaviorTree *>(gWork.tree); }

std::string RandomName(std::mt19937 &rng, size_t maxLen = 40)
{
    static constexpr size_t kLens[] = {0, 1, 14, 15, 16, 17, 31, 32, 0x3f, 0x40};
    size_t n = rng() % 3 == 0 ? kLens[rng() % 10] : rng() % (maxLen + 1);
    std::string s;
    for (size_t i = 0; i < n; ++i) s.push_back(static_cast<char>('A' + rng() % 50));
    return s;
}

} // namespace

// ===== small slots ===========================================================================================

DIFF_TEST(cTSBehaviorTree_GetClassId, 0x46a120)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x46a120);
    uint8_t buf[0x60] = {};
    ++ctx.cases;
    if (orig(buf) != reinterpret_cast<cTSBehaviorTree *>(buf)->GetClassId()) ctx.Fail("GetClassId differs");
}

DIFF_TEST(cTSBehaviorTree_QueryInterface, 0x46d0e0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x46d0e0);
    std::mt19937 rng(0x51);
    static constexpr uint32_t kIds[] = {1, 0x5b267c7c, 0xe4fda3d4, 0x456b8f1d, 0, 2, 0x456b8f1c, 0x456b8f1e, 0x5b267c7d, 0xffffffffu};
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        plan.name = RandomName(rng);
        uint32_t iid = seed % 5 == 4 ? static_cast<uint32_t>(rng()) : kIds[seed % 10];
        void *outA = reinterpret_cast<void *>(0x1234);
        void *outB = outA;
        Run(ctx, "QueryInterface", seed, in, plan,
            [&](uint8_t *t) { outA = reinterpret_cast<void *>(0x1234); return static_cast<uint32_t>(ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x46d0e0)(t, iid, &outA)) | static_cast<uint32_t>(Norm(outA)) << 8; },
            [&](uint8_t *t) { outB = reinterpret_cast<void *>(0x1234); return static_cast<uint32_t>(Tree()->QueryInterface(iid, &outB)) | static_cast<uint32_t>(Norm(outB)) << 8; }, 0xffffff00u | 0xff);
    }
    (void)orig;
}

DIFF_TEST(cTSBehaviorTree_GetArgCount, 0x46bd30)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x46bd30);
    std::mt19937 rng(0x12);
    std::vector<uint16_t> versions = {0x8000, 0x8001, 0x8002, 0x8003, 0x8008, 0x8009, 0x8100, 0x7fff, 0, 1, 0xffff, 0x7ffe, 0x80ff};
    for (int i = 0; i < 60; ++i) versions.push_back(static_cast<uint16_t>(rng()));
    uint32_t seed = 0;
    for (uint16_t v : versions)
        for (int b : {0, 1, 3, 4, 5, 8, 0x80, 0xff}) {
            Init in;
            RandomInit(in, ++seed);
            std::memcpy(in.tree + 0x1c, &v, 2);
            in.tree[0x21] = static_cast<uint8_t>(b);
            Plan plan;
            Run(ctx, "GetArgCount", seed, in, plan, [&](uint8_t *t) { return orig(t); }, [&](uint8_t *) { return Tree()->GetArgCount(); });
        }
}

DIFF_TEST(cTSBehaviorTree_GetLocalCount, 0x46a7d0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x46a7d0);
    std::mt19937 rng(0x13);
    std::vector<uint16_t> versions = {0x8000, 0x8001, 0x8002, 0x8003, 0x8008, 0x8009, 0x8100, 0x7fff, 0, 1, 0xffff, 0x7ffe, 0x80ff};
    for (int i = 0; i < 60; ++i) versions.push_back(static_cast<uint16_t>(rng()));
    uint32_t seed = 0;
    for (uint16_t v : versions)
        for (int b : {0, 1, 3, 4, 5, 8, 0x80, 0xff}) {
            Init in;
            RandomInit(in, ++seed);
            std::memcpy(in.tree + 0x1c, &v, 2);
            in.tree[0x22] = static_cast<uint8_t>(b);
            Plan plan;
            Run(ctx, "GetLocalCount", seed, in, plan, [&](uint8_t *t) { return orig(t); }, [&](uint8_t *) { return Tree()->GetLocalCount(); });
        }
}

DIFF_TEST(cTSBehaviorTree_SetLocalCount, 0x46e150)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x46e150);
    std::mt19937 rng(0x14);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        uint32_t v = rng();
        Run(ctx, "SetLocalCount", seed, in, plan, [&](uint8_t *t) { orig(t, v); return 0u; }, [&](uint8_t *) { Tree()->SetLocalCount(v); return 0u; });
    }
}

DIFF_TEST(cTSBehaviorTree_SetFlags, 0x46e0d0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x46e0d0);
    std::mt19937 rng(0x15);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        uint32_t v = rng();
        Run(ctx, "SetFlags", seed, in, plan, [&](uint8_t *t) { orig(t, v); return 0u; }, [&](uint8_t *) { Tree()->SetFlags(v); return 0u; });
    }
}

DIFF_TEST(cTSBehaviorTree_GetNodeCount, 0x46a190)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint16_t(__thiscall *)(void *)>(0x46a190);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        // Only ax is defined: the original leaves the upper half of eax as it was.
        Run(ctx, "GetNodeCount", seed, in, plan, [&](uint8_t *t) { return static_cast<uint32_t>(orig(t)); }, [&](uint8_t *) { return static_cast<uint32_t>(Tree()->GetNodeCount()); }, 0xffff);
    }
}

DIFF_TEST(cTSBehaviorTree_GetNode, 0x46a170)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x46a170);
    std::mt19937 rng(0x16);
    uint32_t seed = 0;
    for (uint32_t n : {0u, 1u, 2u, 5u, 17u}) {
        for (uint32_t cap : {0u, 17u}) {
            for (uint32_t index : {0u, 1u, 2u, 4u, 5u, 16u, 17u, 18u, 0x80000000u, 0xffffffffu}) {
                Init in;
                RandomInit(in, ++seed);
                Plan plan;
                for (uint32_t i = 0; i < n; ++i) plan.elems.push_back(static_cast<int>(rng() % 30));
                plan.cap = cap > n ? cap : n;
                Run(ctx, "GetNode", seed, in, plan, [&](uint8_t *t) { return Norm(orig(t, index)); }, [&](uint8_t *) { return Norm(Tree()->GetNode(index)); });
            }
        }
    }
}

DIFF_TEST(cTSBehaviorTree_SetName, 0x46e170)
{
    InitVTables(ctx, true);
    std::mt19937 rng(0x17);
    // The call goes to slot 16 of the name object (cRZString::operator=, the image's own code on both sides).
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0x46e170);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        plan.name = RandomName(rng);
        // The argument: a cRZString of the game, built per side from the same text (it is read, not changed).
        std::string text = RandomName(rng);
        cRZString arg;
        arg.Construct(text.c_str());
        Run(ctx, "SetName", seed, in, plan, [&](uint8_t *t) { return Norm(orig(t, &arg)); }, [&](uint8_t *) { return Norm(Tree()->SetName(&arg)); });
        arg.Destruct();
    }
}

// ===== the node list =========================================================================================

DIFF_TEST(cTSBehaviorTree_ClearNodes, 0x469c90)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x469c90);
    std::mt19937 rng(0x18);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        uint32_t n = seed % 7 == 0 ? 0 : rng() % 12;
        for (uint32_t i = 0; i < n; ++i) plan.elems.push_back(static_cast<int>(rng() % 30));
        if (seed % 5 == 1 && n) plan.elems[rng() % n] = kNameObj; // a null-free vector; objects may repeat
        plan.cap = n + rng() % 3;
        Run(ctx, "ClearNodes", seed, in, plan, [&](uint8_t *t) { orig(t); return 0u; }, [&](uint8_t *) { Tree()->ClearNodes(); return 0u; });
    }
}

DIFF_TEST(cTSBehaviorTree_IsJumpTarget, 0x46c3f0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, int32_t)>(0x46c3f0);
    std::mt19937 rng(0x19);
    uint32_t seed = 0;
    for (int round = 0; round < 40; ++round) {
        Init in;
        RandomInit(in, ++seed);
        Plan plan;
        uint32_t n = rng() % 8;
        for (uint32_t i = 0; i < n; ++i) {
            int idx = static_cast<int>(i);
            plan.elems.push_back(idx);
            // Targets: small values so that matches happen; some with garbage above the low 16 bits.
            in.objs[idx].trueT = (rng() % 3 ? rng() % (n + 2) : 0xfffe) | (rng() % 2 ? 0xabcd0000u : 0u);
            in.objs[idx].falseT = (rng() % 3 ? rng() % (n + 2) : 0xfffe) | (rng() % 2 ? 0x12340000u : 0u);
        }
        // A count above the vector size would make the original dereference a null node; smaller counts are fine.
        plan.fakeCount = rng() % 5 == 0 && n ? static_cast<int32_t>(n) - static_cast<int32_t>(rng() % 2) : static_cast<int32_t>(n);
        if (rng() % 8 == 0) plan.fakeCount = -static_cast<int32_t>(rng() % 5) - 1;
        plan.fakeHigh = rng() % 2 ? rng() & 0xffff : 0;
        for (int32_t index : {-3, -1, 0, 1, 2, 3, 7, 8, 9, 0xfffe, 0x7fffffff}) {
            Run(ctx, "IsJumpTarget", seed, in, plan, [&](uint8_t *t) { return static_cast<uint32_t>(orig(t, index)); }, [&](uint8_t *) { return static_cast<uint32_t>(Tree()->IsJumpTarget(index)); }, 0xff);
        }
    }
}

DIFF_TEST(cTSBehaviorTree_CheckNode, 0x46c470)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, int16_t)>(0x46c470);
    std::mt19937 rng(0x1a);
    uint32_t seed = 0;
    for (int round = 0; round < 120; ++round) {
        Init in;
        RandomInit(in, ++seed);
        Plan plan;
        uint32_t n = rng() % 7;
        for (uint32_t i = 0; i < n; ++i) {
            int idx = static_cast<int>(i);
            plan.elems.push_back(idx);
            // Opcodes around the 0x100 limit (primitive vs subroutine call), including negative 16-bit values.
            static constexpr uint32_t kOps[] = {0, 1, 0xff, 0x100, 0x101, 0x7fff, 0x8000, 0xffff, 0x8100};
            in.objs[idx].opcode = (rng() % 2 ? kOps[rng() % 9] : rng() % 0x200) | (rng() % 2 ? 0x55550000u : 0u);
            in.objs[idx].trueT = rng() % 4 == 0 ? 0xfffe : rng() % 6;
            in.objs[idx].falseT = rng() % 4 == 0 ? 0xfffe : rng() % 6;
            if (rng() % 3 == 0) in.objs[idx].trueT |= 0x10000;
            if (rng() % 3 == 0) in.objs[idx].falseT |= 0xabcd0000u;
        }
        in.objs[kValidatorObj].ret = rng() % 3 == 0 ? 0 : rng();
        plan.fakeCount = rng() % 6 == 0 ? static_cast<int32_t>(n) + static_cast<int32_t>(rng() % 3) - 1 : static_cast<int32_t>(n);
        if (rng() % 10 == 0) plan.fakeCount = -2;
        plan.fakeHigh = rng() % 2 ? rng() & 0xffff : 0;
        for (int16_t index : {int16_t(-2), int16_t(-1), int16_t(0), int16_t(1), int16_t(2), int16_t(3), int16_t(5), int16_t(6), int16_t(7)}) {
            Run(ctx, "CheckNode", seed, in, plan, [&](uint8_t *t) { return static_cast<uint32_t>(orig(t, index)); }, [&](uint8_t *) { return static_cast<uint32_t>(Tree()->CheckNode(index)); }, 0xff);
        }
    }
}

DIFF_TEST(cTSBehaviorTree_ExportTo, 0x469cb0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0x469cb0);
    std::mt19937 rng(0x1b);
    for (uint32_t seed = 0; seed < 150; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        uint32_t n = seed % 6 == 0 ? 0 : rng() % 8;
        for (uint32_t i = 0; i < n; ++i) plan.elems.push_back(static_cast<int>(i));
        plan.cap = n;
        // The count in the tree's own field (slot 22 serves the nodes); it decides the loop, as a signed 16-bit value.
        uint16_t count = seed % 7 == 1 ? 0 : seed % 7 == 2 ? 0x8000 + rng() % 4 : static_cast<uint16_t>(n);
        std::memcpy(in.tree + 0x1e, &count, 2);
        Run(ctx, "ExportTo", seed, in, plan, [&](uint8_t *t) { orig(t, &gWork.objs[kSinkObj]); return 0u; }, [&](uint8_t *) { Tree()->ExportTo(&gWork.objs[kSinkObj]); return 0u; });
    }
}

DIFF_TEST(cTSBehaviorTree_AnalyseFlags, 0x469a30)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0x469a30);
    std::mt19937 rng(0x1c);
    // Opcodes: the ones the switch names (2, 20, 28, 50, 126), their neighbours, the 0x100 limit, subroutine calls.
    const std::vector<uint32_t> ops = {0, 1, 2, 3, 19, 20, 21, 27, 28, 29, 49, 50, 51, 100, 125, 126, 127, 128, 0xff, 0x100, 0x101, 0x1ff, 0x2000, 0x7fff, 0x8000, 0xffff};
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        uint32_t n = seed % 9 == 0 ? 0 : rng() % 7;
        for (uint32_t i = 0; i < n; ++i) {
            int idx = static_cast<int>(i);
            plan.elems.push_back(idx);
            in.objs[idx].opcode = (rng() % 4 ? ops[rng() % ops.size()] : rng() % 0x300) | (rng() % 2 ? 0x77770000u : 0u);
            // Operand bytes: the values the nested tables pick on (+0xa: 12, 28, 29 and neighbours; +9: 3..21).
            static constexpr uint8_t kA[] = {11, 12, 13, 14, 15, 16, 17, 18, 27, 28, 29, 30, 0, 255};
            in.objs[idx].param[0xa] = rng() % 2 ? kA[rng() % 14] : rng() % 2 ? static_cast<uint8_t>(rng()) : static_cast<uint8_t>(12 + (rng() % 3) * 8 + (rng() % 3 == 0)); // 12, 20, 28, 29
            in.objs[idx].param[9] = rng() % 3 ? static_cast<uint8_t>(2 + rng() % 21) : static_cast<uint8_t>(rng());
        }
        for (int k = 0; k < 9; ++k) {
            Obj &o = in.objs[kSubTrees + k];
            if (rng() % 2) o.flags[0] = (o.flags[0] & ~7u) | 1;
            o.flagsNow = o.flags[0];
        }
        uint16_t count = seed % 8 == 1 ? static_cast<uint16_t>(n + 2) : seed % 8 == 2 ? 0x8001 : seed % 8 == 3 ? 0 : static_cast<uint16_t>(n);
        std::memcpy(in.tree + 0x1e, &count, 2);
        plan.cap = n;
        // count > n would read past the vector: the original reads a null node and crashes; keep it consistent.
        if (static_cast<int16_t>(count) > static_cast<int16_t>(n)) {
            count = static_cast<uint16_t>(n);
            std::memcpy(in.tree + 0x1e, &count, 2);
        }
        Run(ctx, "AnalyseFlags", seed, in, plan, [&](uint8_t *t) { orig(t, &gWork.objs[kProviderObj]); return 0u; }, [&](uint8_t *) { Tree()->AnalyseFlags(&gWork.objs[kProviderObj]); return 0u; });
    }
    // Opcode 2 alone, over every operand byte pair around the values the nested tables pick on: byte +0xa in
    // 10..32 and 0xff, byte +9 in 0..31 and 0xff.
    for (uint32_t a = 10; a <= 33; ++a) {
        for (uint32_t b = 0; b <= 32; ++b) {
            Init in;
            RandomInit(in, 5000 + a * 64 + b);
            Plan plan;
            plan.elems.push_back(0);
            plan.cap = 1;
            in.objs[0].opcode = 2;
            in.objs[0].param[0xa] = static_cast<uint8_t>(a == 33 ? 0xff : a);
            in.objs[0].param[9] = static_cast<uint8_t>(b == 32 ? 0xff : b);
            uint16_t count = 1;
            std::memcpy(in.tree + 0x1e, &count, 2);
            in.tree[0x23] = 0;
            Run(ctx, "AnalyseFlags(op 2)", a * 64 + b, in, plan, [&](uint8_t *t) { orig(t, &gWork.objs[kProviderObj]); return 0u; }, [&](uint8_t *) { Tree()->AnalyseFlags(&gWork.objs[kProviderObj]); return 0u; });
        }
    }
}

// ===== serialisation =========================================================================================

DIFF_TEST(cTSBehaviorTree_Write, 0x46ef80)
{
    InitVTables(ctx, false);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x46ef80);
    std::mt19937 rng(0x1d);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Init in;
        RandomInit(in, seed);
        Plan plan;
        // Names up to 0x40 bytes. ORIGINAL-BUG: a longer name overflows the 0x40-byte block (not tested).
        plan.name = RandomName(rng);
        uint32_t n = seed % 6 == 0 ? 0 : rng() % 7;
        for (uint32_t i = 0; i < n; ++i) plan.elems.push_back(static_cast<int>(i));
        plan.cap = n;
        gErrAt = seed % 3 == 0 ? 0xffffffffu : rng() % (n + 6);
        Run(ctx, "Write", seed, in, plan, [&](uint8_t *t) { return static_cast<uint32_t>(orig(t, &gWork.objs[kStreamObj])); }, [&](uint8_t *) { return static_cast<uint32_t>(Tree()->Write(&gWork.objs[kStreamObj])); }, 0xff);
    }
    gErrAt = 0xffffffffu;
}

DIFF_TEST(cTSBehaviorTree_Read, 0x46d530)
{
    InitVTables(ctx, true);
    PoolFixture pool(ctx);
    NodePoolFixture npool(ctx);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x46d530);
    std::mt19937 rng(0x1e);
    // Format words: the old format (a positive node count) and every signature 0x8000..0x8009, then others.
    std::vector<uint16_t> versions = {0, 1, 2, 3, 7, 0x7fff, 0x8000, 0x8001, 0x8002, 0x8003, 0x8004, 0x8005, 0x8006, 0x8007, 0x8008, 0x8009, 0x800a, 0xffff, 0xc000};
    const std::vector<PoolCase> cases = PoolCases(6, 127);
    uint32_t seed = 0;
    for (size_t ci = 0; ci < cases.size(); ci += 2) {
        const PoolCase &pc = cases[ci];
        pool.Randomize(pc.seed, pc.steps % 90, pc.allocOnly, pc.postFrees);
        npool.Randomize(pc.seed ^ 0x5a5a, pc.steps, pc.allocOnly, pc.postFrees);
        for (uint16_t v : versions) {
            ++seed;
            Init in;
            RandomInit(in, seed);
            Plan plan;
            plan.name = RandomName(rng);
            uint32_t before = rng() % 5;
            for (uint32_t i = 0; i < before; ++i) plan.elems.push_back(static_cast<int>(rng() % 30));
            plan.cap = before + (rng() % 3 == 0 ? 0 : rng() % 20);
            // Node count of the new format: small, sometimes none, negative, or large with a stream error that stops it.
            static constexpr uint16_t kCounts[] = {0, 1, 2, 5, 9, 12, 0x8000, 0xffff, 0x7fff};
            gScript6[0] = v;
            gScript6[1] = kCounts[rng() % 9];
            bool huge = (v == 0x7fff || gScript6[1] == 0x7fff);
            gErrAt = huge ? 14 + rng() % 8 : seed % 4 == 0 ? rng() % 30 : 0xffffffffu;
            uint32_t gen = seed * 2654435761u + 1;
            Run(ctx, "Read", seed, in, plan, [&](uint8_t *t) { gRand = gen; return static_cast<uint32_t>(orig(t, &gWork.objs[kStreamObj])); },
                [&](uint8_t *) { gRand = gen; return static_cast<uint32_t>(Tree()->Read(&gWork.objs[kStreamObj])); }, 0xff);
        }
    }
    gErrAt = 0xffffffffu;
}

DIFF_TEST(cTSBehaviorTree_AddNode, 0x469910)
{
    InitVTables(ctx, true);
    PoolFixture pool(ctx);
    NodePoolFixture npool(ctx);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, const void *)>(0x469910);
    std::mt19937 rng(0x1f);
    const std::vector<PoolCase> cases = PoolCases(6, 127);
    uint32_t seed = 0;
    for (size_t ci = 0; ci < cases.size(); ci += 2) {
        const PoolCase &pc = cases[ci];
        pool.Randomize(pc.seed, pc.steps % 90, pc.allocOnly, pc.postFrees);
        npool.Randomize(pc.seed ^ 0x77, pc.steps, pc.allocOnly, pc.postFrees);
        for (int k = 0; k < 6; ++k) {
            ++seed;
            Init in;
            RandomInit(in, seed);
            Plan plan;
            uint32_t n = rng() % 6;
            for (uint32_t i = 0; i < n; ++i) plan.elems.push_back(static_cast<int>(rng() % 30));
            plan.cap = k % 2 ? n : n + 1 + rng() % 4; // full (grows) or with room
            plan.fakeSlots = true;
            uint32_t opcode = rng() % 3 ? rng() % 0x80 : static_cast<uint32_t>(rng());
            uint32_t tt = rng(), ff = rng();
            alignas(4) uint8_t param[0x18];
            for (auto &x : param) x = static_cast<uint8_t>(rng());
            Run(ctx, "AddNode", seed, in, plan, [&](uint8_t *t) { return static_cast<uint32_t>(orig(t, opcode, tt, ff, param)); },
                [&](uint8_t *) { return static_cast<uint32_t>(Tree()->AddNode(opcode, tt, ff, reinterpret_cast<const XPrimParam *>(param))); }, 0xff);
        }
    }
    // The size limit: a vector of 0xfffb pointers is full (no element is dereferenced).
    for (uint32_t size : {0xfffbu, 0xfffcu, 0xffffu}) {
        Init in;
        RandomInit(in, size);
        Plan plan;
        uint8_t param[0x18] = {};
        // Fake vector: addresses only (nothing is dereferenced when the limit stops the call).
        Prepare(in, plan);
        void **fake = reinterpret_cast<void **>(0x10000000);
        Tree()->mNodes.mFirst = fake;
        Tree()->mNodes.mLast = fake + size;
        Tree()->mNodes.mEnd = fake + size;
        Snapshot start = Take();
        uint8_t ra = orig(gWork.tree, 1, 2, 3, param);
        Snapshot pa = Take();
        Restore(start);
        bool rb = Tree()->AddNode(1, 2, 3, reinterpret_cast<const XPrimParam *>(param));
        ++ctx.cases;
        if ((ra != 0) != rb || !(pa == Take()) || Tree()->mNodes.Size() != size) ctx.Fail("AddNode at the limit %u differs", size);
        Tree()->mNodes.mFirst = Tree()->mNodes.mLast = Tree()->mNodes.mEnd = nullptr;
        Tree()->mName.mString.~basic_string();
    }
}

// ===== construction and destruction ==========================================================================

DIFF_TEST(cTSBehaviorTree_Create, 0x46a020)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<cTSBehaviorTree *(__cdecl *)()>(0x46a020);
    for (int i = 0; i < 20; ++i) {
        cTSBehaviorTree *a = orig();
        cTSBehaviorTree *b = cTSBehaviorTree::Create();
        ++ctx.cases;
        std::vector<uint8_t> ba(reinterpret_cast<uint8_t *>(a), reinterpret_cast<uint8_t *>(a) + 0x60);
        std::vector<uint8_t> bb(reinterpret_cast<uint8_t *>(b), reinterpret_cast<uint8_t *>(b) + 0x60);
        // +0x25..0x27 are padding the constructor never writes.
        for (int k = 0x25; k < 0x28; ++k) ba[k] = bb[k] = 0;
        if (ba != bb) ctx.Fail("Create: the constructed objects differ");
        a->mName.mString.~basic_string();
        b->mName.mString.~basic_string();
        ::operator delete(a);
        ::operator delete(b);
    }
}

namespace {

// A tree on the heap for the destructor: vtables, name, owned objects, vector storage (small or the large
// aligned layout), optionally with a null element.
struct HeapTree {
    uint8_t *tree = nullptr;
    void *rawStorage = nullptr; // the block to free for the vector
    void **first = nullptr;
};

HeapTree BuildHeapTree(std::mt19937 &rng, uint32_t nElems, uint32_t cap, bool big, bool owned)
{
    HeapTree h;
    h.tree = static_cast<uint8_t *>(::operator new(0x60));
    for (int i = 0; i < 0x60; ++i) h.tree[i] = static_cast<uint8_t>(rng());
    cTSBehaviorTree *t = reinterpret_cast<cTSBehaviorTree *>(h.tree);
    t->mVTable = gTreeVT;
    t->mVTable2 = gTree2VT;
    t->mName.mVTable = GameAddress<void>(cRZString::kVTable);
    new (&t->mName.mString) std::string(RandomName(rng, 80));
    t->mOwned3C = owned ? &gWork.objs[kValidatorObj] : nullptr;
    t->mOwned38 = owned ? &gWork.objs[kSinkObj] : nullptr;
    if (cap == 0) {
        t->mNodes.mFirst = t->mNodes.mLast = t->mNodes.mEnd = nullptr;
    } else if (!big) {
        h.rawStorage = ::operator new(cap * 4);
        h.first = static_cast<void **>(h.rawStorage);
    } else {
        // 32-byte aligned with the original pointer stored in the dword before.
        h.rawStorage = ::operator new(cap * 4 + 0x23);
        uintptr_t a = (reinterpret_cast<uintptr_t>(h.rawStorage) + 0x23) & ~static_cast<uintptr_t>(0x1f);
        h.first = reinterpret_cast<void **>(a);
        h.first[-1] = h.rawStorage;
    }
    if (cap) {
        for (uint32_t i = 0; i < cap; ++i) h.first[i] = i < nElems ? static_cast<void *>(&gWork.objs[i % 30]) : reinterpret_cast<void *>(0xdead0000u + i);
        if (nElems > 2 && rng() % 2) h.first[1] = nullptr; // null elements are skipped
        t->mNodes.mFirst = h.first;
        t->mNodes.mLast = h.first + nElems;
        t->mNodes.mEnd = h.first + cap;
    }
    return h;
}

bool Live(void *p)
{
    return p && !difftest::FreedSinceBegin(p); // the call under test ran after difftest::BeginFrees()
}

} // namespace

DIFF_TEST(cTSBehaviorTree_DeletingDestructor, 0x469740)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<cTSBehaviorTree *(__thiscall *)(void *, uint32_t)>(0x469740);
    gTrace.reserve(1 << 14); // the recording must not allocate during the call
    Init in;
    RandomInit(in, 99);
    for (uint32_t variant = 0; variant < 160; ++variant) {
        std::mt19937 pick(variant * 77 + 1);
        bool big = variant % 4 == 3;
        // 1024 elements are exactly 0x1000 bytes (the smallest aligned block), 1023 the largest plain one.
        uint32_t cap = big ? (variant % 8 == 3 ? 1024 : 1024 + pick() % 40) : variant % 8 == 1 ? 1023 : pick() % 6 == 0 ? 0 : 1 + pick() % 10;
        uint32_t nElems = cap ? pick() % (cap + 1) : 0;
        bool owned = variant % 3 != 0;
        uint32_t flags = variant % 2 ? 1 : (variant % 6 == 0 ? 4 : 0); // bit 0 deletes the object
        std::vector<uint32_t> trace[2];
        std::vector<uint8_t> after[2];
        bool freedTree[2], freedVec[2], freedStr[2];
        bool retOk[2];
        for (int side = 0; side < 2; ++side) {
            std::memcpy(gWork.objs, in.objs, sizeof(gWork.objs));
            gWork.objs[kValidatorObj].vt = gNodeVT; // owned objects are released through slot 2
            gWork.objs[kSinkObj].vt = gNodeVT;
            std::mt19937 r2(variant + 1000);
            HeapTree h = BuildHeapTree(r2, nElems, cap, big, owned);
            cTSBehaviorTree *t = reinterpret_cast<cTSBehaviorTree *>(h.tree);
            const std::string &s = t->mName.mString;
            void *strBlock = s.capacity() > 15 ? const_cast<char *>(s.data()) : nullptr;
            gTrace.clear();
            difftest::BeginFrees();
            cTSBehaviorTree *ret = side == 0 ? orig(h.tree, flags) : t->DeletingDestructor(flags);
            // Measured right after the call, before anything allocates (a later allocation can reuse a freed block).
            retOk[side] = ret == t;
            freedTree[side] = !Live(h.tree);
            freedVec[side] = h.rawStorage ? !Live(h.rawStorage) : true;
            freedStr[side] = strBlock ? !Live(strBlock) : true;
            trace[side] = gTrace;
            if (!(flags & 1)) {
                after[side].assign(h.tree, h.tree + 0x60);
                for (int k = 0x45; k < 0x54; ++k) after[side][k] = 0; // heap pointer / small-string tail
                ::operator delete(h.tree);
            }
        }
        ++ctx.cases;
        if (trace[0] != trace[1]) ctx.Fail("destructor variant %u: recorded releases differ (%zu vs %zu words)", variant, trace[0].size(), trace[1].size());
        if (!retOk[0] || !retOk[1]) ctx.Fail("destructor variant %u: return value is not this (original %d, ours %d)", variant, retOk[0], retOk[1]);
        if (freedTree[0] != freedTree[1] || freedTree[1] != ((flags & 1) != 0)) ctx.Fail("destructor variant %u: tree block freed original %d, ours %d", variant, freedTree[0], freedTree[1]);
        if (!freedVec[0] || !freedVec[1]) ctx.Fail("destructor variant %u: vector storage freed original %d, ours %d", variant, freedVec[0], freedVec[1]);
        if (!freedStr[0] || !freedStr[1]) ctx.Fail("destructor variant %u: long name freed original %d, ours %d", variant, freedStr[0], freedStr[1]);
        if (after[0] != after[1]) ctx.Fail("destructor variant %u: object bytes after differ", variant);
    }
}

DIFF_TEST(cTSBehaviorTree_VectorClear, 0x176f00)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x176f00);
    Init in;
    RandomInit(in, 7);
    std::mt19937 rng(0x21);
    for (uint32_t k = 0; k < 80; ++k) {
        uint32_t n = k % 5 == 0 ? 0 : rng() % 9;
        uint32_t cap = n + rng() % 3;
        std::vector<uint32_t> trace[2];
        uint32_t after[2][3];
        for (int side = 0; side < 2; ++side) {
            std::memcpy(gWork.objs, in.objs, sizeof(gWork.objs));
            void **st = static_cast<void **>(::operator new(cap * 4 + 4));
            // Null elements are skipped (same positions on both sides).
            for (uint32_t i = 0; i < cap; ++i)
                st[i] = i < n ? ((i * 7 + k) % 5 == 0 ? nullptr : static_cast<void *>(&gWork.objs[i])) : reinterpret_cast<void *>(0xdead0000u + i);
            RefPtrVector v{st, st + n, st + cap};
            gTrace.clear();
            if (side == 0) orig(&v); else v.Clear();
            trace[side] = gTrace;
            after[side][0] = static_cast<uint32_t>(v.mFirst - st);
            after[side][1] = static_cast<uint32_t>(v.mLast - st);
            after[side][2] = static_cast<uint32_t>(v.mEnd - st);
            ::operator delete(st);
        }
        ++ctx.cases;
        if (trace[0] != trace[1] || std::memcmp(after[0], after[1], sizeof(after[0])) != 0) ctx.Fail("vector Clear case %u differs", k);
    }
}

DIFF_TEST(cTSBehaviorTree_VectorDestroy, 0x14c060)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x14c060);
    gTrace.reserve(1 << 14);
    Init in;
    RandomInit(in, 8);
    std::mt19937 rng(0x22);
    for (uint32_t k = 0; k < 80; ++k) {
        bool big = k % 3 == 2;
        uint32_t cap = k % 7 == 0 ? 0 : big ? (k % 2 ? 1024 : 1024 + rng() % 50) : k % 5 == 1 ? 1023 : 1 + rng() % 12;
        uint32_t n = cap ? rng() % (cap + 1) : 0;
        std::vector<uint32_t> trace[2];
        uint32_t after[2][3];
        bool freed[2];
        for (int side = 0; side < 2; ++side) {
            std::memcpy(gWork.objs, in.objs, sizeof(gWork.objs));
            void *raw = nullptr;
            void **first = nullptr;
            if (cap) {
                if (big) {
                    raw = ::operator new(cap * 4 + 0x23);
                    first = reinterpret_cast<void **>((reinterpret_cast<uintptr_t>(raw) + 0x23) & ~static_cast<uintptr_t>(0x1f));
                    first[-1] = raw;
                } else {
                    raw = ::operator new(cap * 4);
                    first = static_cast<void **>(raw);
                }
                for (uint32_t i = 0; i < cap; ++i) first[i] = i < n && (i * 5 + k) % 4 != 0 ? static_cast<void *>(&gWork.objs[i % 30]) : nullptr;
            }
            RefPtrVector v{first, first ? first + n : nullptr, first ? first + cap : nullptr};
            gTrace.clear();
            difftest::BeginFrees();
            if (side == 0) orig(&v); else v.Destroy();
            freed[side] = raw ? !Live(raw) : true; // right after the call
            trace[side] = gTrace;
            after[side][0] = reinterpret_cast<uintptr_t>(v.mFirst) != 0;
            after[side][1] = reinterpret_cast<uintptr_t>(v.mLast) != 0;
            after[side][2] = reinterpret_cast<uintptr_t>(v.mEnd) != 0;
        }
        ++ctx.cases;
        if (trace[0] != trace[1] || std::memcmp(after[0], after[1], sizeof(after[0])) != 0) ctx.Fail("vector Destroy case %u differs (trace/pointers)", k);
        if (!freed[0] || !freed[1]) ctx.Fail("vector Destroy case %u: storage freed original %d, ours %d", k, freed[0], freed[1]);
    }
}
