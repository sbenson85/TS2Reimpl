// Differential tests for cTSBehaviorNode (src/simantics/cTSBehaviorNode.*): QueryInterface 0x46d0b0, Release
// 0x46dbc0, the opcode accessors 0x46c100/0x46c110, SetOpcode 0x46e300, the target accessors (0x432e30, 0x4328e0,
// 0x46bff0, 0x4351b0, 0x4350e0), the operand accessors 0x46bc80/0x46e180, the parameter copies 0x46c330/0x46e3c0,
// the flag bit 0x46a270/0x46de60, Write 0x46ef30, Read 0x46d2b0, the class-id constant 0x46a110 and the deleting
// destructor 0x469680.
//
// Both sides run on the same node buffer and the same pool state: the buffers (and the pools, see
// simantics_params_fixture.h) are restored from a snapshot between the original and ours, so every address
// (the node, the operand object, pool blocks) is identical on both sides and the results compare byte for
// byte. The node's vtable is a copy of the game's with fakes in the slots the node calls itself (1 AddRef,
// 12 operand count, 18/19 flag, 25 deleting destructor) that record their arguments; the stream is a fake
// whose slots 5, 6, 7 and 18 record every call (pointers normalised) and fill read destinations from a
// generator reseeded for each side.
#include "difftest.h"
#include "game/game.h"
#include "game/vtable.h"
#include "simantics/cTSBehaviorNode.h"
#include "simantics_params_fixture.h"

#include <Windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {

using namespace simfix;

struct alignas(8) NodeBuf {
    uint8_t b[0x20];
};

std::vector<uint32_t> gTrace;
uint32_t gRand = 1;
uint8_t gError = 0;
int32_t gCount = 8;
uint8_t gFlagResult = 0;
const uint8_t *gNodeBase = nullptr;
void *gNodeVT[26];
void *gStreamVT[24];
const uint8_t *gExtraBase = nullptr; // a second watched buffer (the operand object in the accessor tests)

uint32_t NextRand()
{
    gRand ^= gRand << 13;
    gRand ^= gRand >> 17;
    gRand ^= gRand << 5;
    return gRand;
}

// A pointer in the trace: an offset into the node, into a pool region, into the extra buffer, or a marker
// for anything else (locals of the code under test).
uint32_t Norm(const void *p)
{
    const uint8_t *q = static_cast<const uint8_t *>(p);
    if (q >= gNodeBase && q < gNodeBase + 0x20) return 0x1000 + static_cast<uint32_t>(q - gNodeBase);
    if (gExtraBase && q >= gExtraBase && q < gExtraBase + 0x40) return 0x2000 + static_cast<uint32_t>(q - gExtraBase);
    uint32_t idx = 0;
    for (PoolBase *pool : ActivePools()) {
        if (q >= pool->region && q < pool->region + kPages * kPageBytes)
            return 0x10000 * (idx + 1) + static_cast<uint32_t>(q - pool->region);
        ++idx;
    }
    return 0x3000;
}

void __fastcall RdByte(void *, void *, void *out)
{
    gTrace.push_back(5);
    gTrace.push_back(Norm(out));
    uint32_t r = NextRand();
    // Mostly the special target bytes 0xfd..0xff and small values, some full range.
    uint8_t v = r % 3 == 0 ? static_cast<uint8_t>(r >> 8) : static_cast<uint8_t>(0xf8 + (r >> 8) % 8);
    *static_cast<uint8_t *>(out) = v;
}

void __fastcall RdWord(void *, void *, void *out)
{
    gTrace.push_back(6);
    gTrace.push_back(Norm(out));
    uint32_t r = NextRand();
    uint16_t v = static_cast<uint16_t>(r % 6 == 0 ? 0 : r >> 8);
    // The opcode word of the node: mostly real primitive opcodes (so the classes with conversions come up),
    // sometimes with the flag bit, sometimes anything.
    if (out == gNodeBase + 6 && r % 8 != 0) v = static_cast<uint16_t>(((r >> 8) % 0x7e) | (r % 8 == 1 ? 0x8000 : 0));
    std::memcpy(out, &v, sizeof(v));
}

void __fastcall RdWord7(void *, void *, void *out)
{
    gTrace.push_back(7);
    gTrace.push_back(Norm(out));
    uint32_t r = NextRand();
    uint16_t v = static_cast<uint16_t>(r % 6 == 0 ? 0xffff : r >> 8);
    std::memcpy(out, &v, sizeof(v));
}

void __fastcall WrByte(void *, void *, uint32_t v)
{
    gTrace.push_back(5);
    gTrace.push_back(v);
}

void __fastcall WrWord6(void *, void *, uint32_t v)
{
    gTrace.push_back(6);
    gTrace.push_back(v);
}

void __fastcall WrWord7(void *, void *, uint32_t v)
{
    gTrace.push_back(7);
    gTrace.push_back(v);
}

uint8_t __fastcall StreamErr(void *, void *)
{
    gTrace.push_back(18);
    return gError;
}

uint32_t __fastcall NodeAddRef(void *self, void *)
{
    gTrace.push_back(101);
    gTrace.push_back(Norm(self));
    return 7;
}

int32_t __fastcall NodeCount(void *, void *)
{
    return gCount;
}

uint8_t __fastcall NodeGetFlag(void *, void *)
{
    gTrace.push_back(118);
    return gFlagResult;
}

void __fastcall NodeSetFlag(void *self, void *, uint32_t value)
{
    gTrace.push_back(119);
    gTrace.push_back(Norm(self));
    gTrace.push_back(value);
}

void *__fastcall NodeDelete(void *self, void *, uint32_t flags)
{
    gTrace.push_back(125);
    gTrace.push_back(Norm(self));
    gTrace.push_back(flags);
    return self;
}

void InitVTables(const difftest::Context &ctx, bool reading)
{
    std::memcpy(gNodeVT, ctx.image.At(cTSBehaviorNode::kVTable), sizeof(gNodeVT));
    gNodeVT[1] = reinterpret_cast<void *>(&NodeAddRef);
    gNodeVT[12] = reinterpret_cast<void *>(&NodeCount);
    gNodeVT[18] = reinterpret_cast<void *>(&NodeGetFlag);
    gNodeVT[19] = reinterpret_cast<void *>(&NodeSetFlag);
    gNodeVT[25] = reinterpret_cast<void *>(&NodeDelete);
    std::memset(gStreamVT, 0, sizeof(gStreamVT));
    gStreamVT[5] = reinterpret_cast<void *>(reading ? reinterpret_cast<void *>(&RdByte) : reinterpret_cast<void *>(&WrByte));
    gStreamVT[6] = reinterpret_cast<void *>(reading ? reinterpret_cast<void *>(&RdWord) : reinterpret_cast<void *>(&WrWord6));
    gStreamVT[7] = reinterpret_cast<void *>(reading ? reinterpret_cast<void *>(&RdWord7) : reinterpret_cast<void *>(&WrWord7));
    gStreamVT[18] = reinterpret_cast<void *>(&StreamErr);
}

cTSBehaviorNode *N(NodeBuf &n)
{
    return reinterpret_cast<cTSBehaviorNode *>(n.b);
}

void FillNode(NodeBuf &n, uint32_t seed, void *param)
{
    std::mt19937 rng(seed);
    for (auto &x : n.b) x = static_cast<uint8_t>(rng());
    uint32_t vt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gNodeVT));
    std::memcpy(n.b, &vt, 4);
    static constexpr uint16_t kCounts[] = {0, 1, 2, 3, 0xffff, 0x8000};
    uint16_t c = rng() % 3 ? kCounts[rng() % 6] : static_cast<uint16_t>(rng());
    std::memcpy(n.b + 4, &c, 2);
    uint32_t p = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param));
    std::memcpy(n.b + 0xc, &p, 4);
}

// Runs the original then ours from the same node bytes and pool state; compares the return (under retMask),
// the node bytes, the recorded trace and the pools.
template <typename Orig, typename Ours>
void Compare(difftest::Context &ctx, const char *what, uint32_t seed, NodeBuf &node, Orig orig, Ours ours, uint32_t retMask = 0xffffffffu)
{
    NodeBuf saved = node;
    Snapshot start = Take();
    gNodeBase = node.b;
    gTrace.clear();
    uint32_t ra = orig();
    NodeBuf afterA = node;
    Snapshot poolA = Take();
    std::vector<uint32_t> ta = gTrace;
    node = saved;
    Restore(start);
    gTrace.clear();
    uint32_t rb = ours();
    std::vector<uint32_t> tb = gTrace;
    ++ctx.cases;
    if ((ra & retMask) != (rb & retMask)) ctx.Fail("%s seed %u: original returned %08x, ours %08x", what, seed, ra, rb);
    if (std::memcmp(afterA.b, node.b, sizeof(node.b)) != 0) {
        int at = 0;
        while (afterA.b[at] == node.b[at]) ++at;
        ctx.Fail("%s seed %u: node bytes differ at +0x%x (original %02x, ours %02x)", what, seed, at, afterA.b[at], node.b[at]);
    }
    if (ta != tb) ctx.Fail("%s seed %u: recorded calls differ (%zu vs %zu words)", what, seed, ta.size(), tb.size());
    if (!(poolA == Take())) ctx.Fail("%s seed %u: pool state differs", what, seed);
    gExtraBase = nullptr;
}

} // namespace

// ===== accessors and small slots =============================================================================

DIFF_TEST(cTSBehaviorNode_GetOpcode, 0x46c100)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x46c100);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        Compare(ctx, "GetOpcode", seed, n, [&] { return orig(n.b); }, [&] { return N(n)->GetOpcode(); }, 0xffff);
    }
}

DIFF_TEST(cTSBehaviorNode_GetOpcodeWord, 0x46c110)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint16_t *(__thiscall *)(void *)>(0x46c110);
    NodeBuf n;
    FillNode(n, 1, nullptr);
    ++ctx.cases;
    if (orig(n.b) != N(n)->GetOpcodeWord() || N(n)->GetOpcodeWord() != reinterpret_cast<uint16_t *>(n.b + 6)) ctx.Fail("GetOpcodeWord differs");
}

DIFF_TEST(cTSBehaviorNode_GetTrueTarget, 0x432e30)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x432e30);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        Compare(ctx, "GetTrueTarget", seed, n, [&] { return orig(n.b); }, [&] { return N(n)->GetTrueTarget(); }, 0xffff);
    }
}

DIFF_TEST(cTSBehaviorNode_GetFalseTarget, 0x4328e0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x4328e0);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        Compare(ctx, "GetFalseTarget", seed, n, [&] { return orig(n.b); }, [&] { return N(n)->GetFalseTarget(); }, 0xffff);
    }
}

DIFF_TEST(cTSBehaviorNode_GetFalseTargetPtr, 0x46bff0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint16_t *(__thiscall *)(void *)>(0x46bff0);
    NodeBuf n;
    FillNode(n, 1, nullptr);
    ++ctx.cases;
    if (orig(n.b) != N(n)->GetFalseTargetPtr() || N(n)->GetFalseTargetPtr() != reinterpret_cast<uint16_t *>(n.b + 0xa))
        ctx.Fail("GetFalseTargetPtr differs");
}

DIFF_TEST(cTSBehaviorNode_SetTrueTarget, 0x4351b0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x4351b0);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        uint32_t v = seed * 2654435761u;
        Compare(ctx, "SetTrueTarget", seed, n, [&] { orig(n.b, v); return 0u; }, [&] { N(n)->SetTrueTarget(v); return 0u; });
    }
}

DIFF_TEST(cTSBehaviorNode_SetFalseTarget, 0x4350e0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x4350e0);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        uint32_t v = seed * 2654435761u;
        Compare(ctx, "SetFalseTarget", seed, n, [&] { orig(n.b, v); return 0u; }, [&] { N(n)->SetFalseTarget(v); return 0u; });
    }
}

DIFF_TEST(cTSBehaviorNode_GetFlag, 0x46a270)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(0x46a270);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        // Only al is defined (the original leaves the shifted sign bits above it).
        Compare(ctx, "GetFlag", seed, n, [&] { return orig(n.b); }, [&] { return N(n)->GetFlag(); }, 0xff);
    }
}

DIFF_TEST(cTSBehaviorNode_SetFlag, 0x46de60)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x46de60);
    std::mt19937 rng(0x66);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        // Only the low byte counts: random upper bytes with a zero or nonzero low byte.
        uint32_t v = (static_cast<uint32_t>(rng()) & ~0xffu) | (seed % 3 == 0 ? 0u : static_cast<uint32_t>(rng() % 255 + 1));
        if (seed % 5 == 0) v = seed % 2;
        Compare(ctx, "SetFlag", seed, n, [&] { orig(n.b, v); return 0u; }, [&] { N(n)->SetFlag(v); return 0u; });
    }
}

DIFF_TEST(cTSBehaviorNode_GetClassId, 0x46a110)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x46a110);
    NodeBuf n;
    FillNode(n, 1, nullptr);
    ++ctx.cases;
    if (orig(n.b) != N(n)->GetClassId()) ctx.Fail("GetClassId differs");
}

DIFF_TEST(cTSBehaviorNode_QueryInterface, 0x46d0b0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x46d0b0);
    std::mt19937 rng(0x77);
    static constexpr uint32_t kIds[] = {1, 0x1b2497eb, 0, 2, 0x1b2497ea, 0x1b2497ec, 0xffffffffu};
    for (uint32_t seed = 0; seed < 200; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        uint32_t iid = seed % 4 == 3 ? static_cast<uint32_t>(rng()) : kIds[seed % 7];
        void *outA = reinterpret_cast<void *>(0x1234);
        void *outB = outA;
        NodeBuf saved = n;
        gNodeBase = n.b;
        gTrace.clear();
        uint8_t ra = orig(n.b, iid, &outA);
        std::vector<uint32_t> ta = gTrace;
        gTrace.clear();
        uint8_t rb = N(n)->QueryInterface(iid, &outB);
        ++ctx.cases;
        if (ra != rb || ta != gTrace || outA != outB || std::memcmp(saved.b, n.b, sizeof(n.b)) != 0)
            ctx.Fail("QueryInterface iid %08x: original %u/%p, ours %u/%p, or calls/bytes differ", iid, ra, outA, rb, outB);
    }
}

DIFF_TEST(cTSBehaviorNode_Release, 0x46dbc0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x46dbc0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        NodeBuf n;
        FillNode(n, seed, nullptr);
        if (seed % 4 == 0) n.b[4] = 1, n.b[5] = 0;
        Compare(ctx, "Release", seed, n, [&] { return orig(n.b); }, [&] { return N(n)->Release(); });
    }
}

// ===== operands and the parameter copies ======================================================================

namespace {

struct alignas(8) ParamBuf {
    uint8_t b[0x40];
};

} // namespace

DIFF_TEST(cTSBehaviorNode_GetOperand, 0x46bc80)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, int32_t)>(0x46bc80);
    static constexpr int32_t kCounts[] = {-3, 0, 1, 5, 8, 9, 100};
    uint32_t seed = 0;
    for (int32_t count : kCounts) {
        for (int32_t index = -2; index <= 10; ++index, ++seed) {
            ParamBuf p;
            std::mt19937 rng(seed);
            for (auto &x : p.b) x = static_cast<uint8_t>(rng());
            NodeBuf n;
            FillNode(n, seed, p.b);
            gCount = count;
            gExtraBase = p.b;
            // Bytes: the 16-bit result only (the original leaves the pointer's upper half above it).
            Compare(ctx, "GetOperand", seed, n, [&] { return orig(n.b, index); }, [&] { return N(n)->GetOperand(index); }, 0xffff);
        }
    }
    gCount = 8;
}

DIFF_TEST(cTSBehaviorNode_SetOperand, 0x46e180)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, int32_t, uint32_t)>(0x46e180);
    static constexpr int32_t kCounts[] = {-3, 0, 1, 5, 8, 9, 100};
    uint32_t seed = 0;
    for (int32_t count : kCounts) {
        for (int32_t index = -2; index <= 10; ++index, ++seed) {
            ParamBuf p;
            std::mt19937 rng(seed);
            for (auto &x : p.b) x = static_cast<uint8_t>(rng());
            NodeBuf n;
            FillNode(n, seed, p.b);
            gCount = count;
            gExtraBase = p.b;
            uint32_t v = static_cast<uint32_t>(rng());
            // The operand object is a second watched buffer: compare it after each run by folding it into the node
            // comparison (restored between the sides like the node).
            ParamBuf savedParam = p;
            NodeBuf savedNode = n;
            gNodeBase = n.b;
            orig(n.b, index, v);
            ParamBuf afterA = p;
            p = savedParam;
            n = savedNode;
            N(n)->SetOperand(index, v);
            ++ctx.cases;
            if (std::memcmp(afterA.b, p.b, sizeof(p.b)) != 0) ctx.Fail("SetOperand count %d index %d: operand bytes differ", count, index);
        }
    }
    gCount = 8;
    gExtraBase = nullptr;
}

DIFF_TEST(cTSBehaviorNode_CopyParamTo, 0x46c330)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<XPrimParam *(__thiscall *)(void *, void *)>(0x46c330);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        ParamBuf src, destA, destB;
        std::mt19937 rng(seed);
        for (auto &x : src.b) x = static_cast<uint8_t>(rng());
        for (auto &x : destA.b) x = static_cast<uint8_t>(rng());
        destB = destA;
        NodeBuf n;
        FillNode(n, seed, src.b);
        XPrimParam *ra = orig(n.b, destA.b);
        XPrimParam *rb = N(n)->CopyParamTo(reinterpret_cast<XPrimParam *>(destB.b));
        ++ctx.cases;
        if (ra != reinterpret_cast<XPrimParam *>(destA.b) || rb != reinterpret_cast<XPrimParam *>(destB.b))
            ctx.Fail("CopyParamTo returned the wrong pointer");
        if (std::memcmp(destA.b, destB.b, sizeof(destA.b)) != 0) ctx.Fail("CopyParamTo seed %u: bytes differ", seed);
    }
}

DIFF_TEST(cTSBehaviorNode_CopyParamFrom, 0x46e3c0)
{
    InitVTables(ctx, true);
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(0x46e3c0);
    // One buffer holds the operand object at +0x10; the source sits at an offset from it (0 = the same
    // object, small offsets overlap it, so a forward byte loop and a memmove would differ).
    for (int delta = -16; delta <= 16; ++delta) {
        for (uint32_t seed = 0; seed < 4; ++seed) {
            ParamBuf a;
            std::mt19937 rng(seed * 100 + static_cast<uint32_t>(delta + 30));
            for (auto &x : a.b) x = static_cast<uint8_t>(rng());
            ParamBuf b = a;
            NodeBuf n;
            FillNode(n, seed, a.b + 0x10);
            NodeBuf nb = n;
            uint32_t pb = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b.b + 0x10));
            std::memcpy(nb.b + 0xc, &pb, 4);
            orig(n.b, a.b + 0x10 + delta);
            N(nb)->CopyParamFrom(reinterpret_cast<const XPrimParam *>(b.b + 0x10 + delta));
            ++ctx.cases;
            if (std::memcmp(a.b, b.b, sizeof(a.b)) != 0) ctx.Fail("CopyParamFrom delta %d seed %u: bytes differ", delta, seed);
        }
    }
}

// ===== pool-backed operations =================================================================================

namespace {

// A node whose operand object is a block of the param pool (or null), picked from the pool's held blocks.
// The node takes ownership: the block leaves the held list (the operation under test frees it).
void *PickParam(PoolBase &pool, std::mt19937 &rng)
{
    if (pool.held.empty() || rng() % 5 == 0) return nullptr;
    size_t k = rng() % pool.held.size();
    void *block = pool.held[k];
    pool.held.erase(pool.held.begin() + static_cast<long>(k));
    return block;
}

} // namespace

DIFF_TEST(cTSBehaviorNode_SetOpcode, 0x46e300)
{
    InitVTables(ctx, true);
    PoolFixture pool(ctx);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x46e300);
    std::mt19937 rng(0x5e);
    const std::vector<PoolCase> cases = PoolCases(120);
    uint32_t seed = 0;
    for (const PoolCase &pc : cases) {
        pool.Randomize(pc.seed, pc.steps, pc.allocOnly, pc.postFrees);
        for (int rep = 0; rep < 3; ++rep, ++seed) {
            NodeBuf n;
            FillNode(n, seed, PickParam(pool, rng));
            gFlagResult = seed % 3 == 0 ? 0 : static_cast<uint8_t>(rng());
            uint32_t op = seed % 4 == 0 ? static_cast<uint32_t>(rng()) : (seed % 4 == 1 ? 0x7e + rng() % 4 : rng() % 0x7e);
            if (seed % 9 == 0) op |= 0x8000;
            Compare(ctx, "SetOpcode", seed, n, [&] { orig(n.b, op); return 0u; }, [&] { N(n)->SetOpcode(op); return 0u; });
        }
    }
}

DIFF_TEST(cTSBehaviorNode_DeletingDestructor, 0x469680)
{
    InitVTables(ctx, true);
    PoolFixture params(ctx);
    NodePoolFixture nodes(ctx);
    auto orig = ctx.Original<cTSBehaviorNode *(__thiscall *)(void *, uint32_t)>(0x469680);
    std::mt19937 rng(0xde);
    const std::vector<PoolCase> pcases = PoolCases(60);
    const std::vector<PoolCase> ncases = PoolCases(60, 127);
    static constexpr uint32_t kFlags[] = {0, 1, 4, 5, 2, 3, 0xfffffffeu, 0xffffffffu, 8, 9};
    uint32_t seed = 0;
    for (size_t i = 0; i < pcases.size(); ++i) {
        params.Randomize(pcases[i].seed, pcases[i].steps, pcases[i].allocOnly, pcases[i].postFrees);
        const PoolCase &nc = ncases[i % ncases.size()];
        nodes.Randomize(nc.seed, nc.steps, nc.allocOnly, nc.postFrees);
        for (int rep = 0; rep < 3 && !nodes.held.empty(); ++rep, ++seed) {
            // The node being deleted is a real block of the node pool, so the pool free can be compared.
            size_t k = rng() % nodes.held.size();
            uint8_t *block = static_cast<uint8_t *>(nodes.held[k]);
            NodeBuf n;
            FillNode(n, seed, PickParam(params, rng));
            uint32_t flags = kFlags[rng() % 10];
            // Run both sides on the pool block itself: copy the node's 16 bytes into the block.
            std::memcpy(block, n.b, 0x10);
            Snapshot start = Take();
            uint8_t before[0x10];
            std::memcpy(before, block, 0x10);
            gNodeBase = block;
            gTrace.clear();
            cTSBehaviorNode *ra = orig(block, flags);
            Snapshot afterA = Take();
            Restore(start);
            cTSBehaviorNode *rb = reinterpret_cast<cTSBehaviorNode *>(block)->DeletingDestructor(flags);
            Snapshot afterB = Take();
            ++ctx.cases;
            if (ra != rb || ra != reinterpret_cast<cTSBehaviorNode *>(block)) ctx.Fail("DeletingDestructor flags %x: wrong return", flags);
            if (!(afterA == afterB)) ctx.Fail("DeletingDestructor seed %u flags %x: pool or node bytes differ", seed, flags);
            // Continue from the original's state, and drop the node from the held list when it was freed.
            Restore(afterA);
            if ((flags & 1) && !(flags & 4)) nodes.held.erase(nodes.held.begin() + static_cast<long>(k));
        }
    }
}

// ===== serialisation ==========================================================================================

DIFF_TEST(cTSBehaviorNode_Write, 0x46ef30)
{
    InitVTables(ctx, false);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x46ef30);
    struct Stream {
        const void *vt;
    } stream{gStreamVT};
    // The operand object is a block-like buffer carrying a real parameter vtable, so its Write slot (the original
    // 0xc0b0b7 in the harness) writes through the same fake stream.
    static constexpr uint32_t kParamVTables[] = {0xdc93f4, 0xdc9624, 0xdc95ac, 0xdc9354, 0xdc9430};
    for (uint32_t seed = 0; seed < 200; ++seed) {
        ParamBuf p;
        std::mt19937 rng(seed);
        for (auto &x : p.b) x = static_cast<uint8_t>(rng());
        uint32_t vt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(kParamVTables[seed % 5])));
        std::memcpy(p.b, &vt, 4);
        NodeBuf n;
        FillNode(n, seed, p.b);
        gError = seed % 3 == 0 ? 0 : static_cast<uint8_t>(rng());
        Compare(ctx, "Write", seed, n, [&] { return orig(n.b, &stream); }, [&] { return N(n)->Write(&stream); }, 0xff);
    }
}

DIFF_TEST(cTSBehaviorNode_Read, 0x46d2b0)
{
    InitVTables(ctx, true);
    PoolFixture pool(ctx);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *, int32_t)>(0x46d2b0);
    struct Stream {
        const void *vt;
    } stream{gStreamVT};
    std::mt19937 rng(0x4ead);
    std::vector<int32_t> versions = {1,
                                     static_cast<int32_t>(0xffff8000),
                                     static_cast<int32_t>(0xffff8001),
                                     static_cast<int32_t>(0xffff8002),
                                     static_cast<int32_t>(0xffff8003),
                                     static_cast<int32_t>(0xffff8004),
                                     static_cast<int32_t>(0xffff8005),
                                     static_cast<int32_t>(0xffff8006),
                                     static_cast<int32_t>(0xffff8007),
                                     static_cast<int32_t>(0xffff8008),
                                     static_cast<int32_t>(0xffff7fff),
                                     0,
                                     2,
                                     3,
                                     0x7fffffff,
                                     static_cast<int32_t>(0x80000000)};
    const std::vector<PoolCase> cases = PoolCases(40);
    uint32_t seed = 0;
    for (const PoolCase &pc : cases) {
        pool.Randomize(pc.seed, pc.steps, pc.allocOnly, pc.postFrees);
        for (int32_t version : versions) {
            ++seed;
            NodeBuf n;
            FillNode(n, seed, PickParam(pool, rng));
            gError = seed % 3 == 0 ? 0 : static_cast<uint8_t>(rng());
            // Reseeds the generator identically for each side: the Compare lambdas set it.
            uint32_t gen = seed * 2654435761u + 1;
            Compare(ctx, "Node Read", seed, n, [&] { gRand = gen; return orig(n.b, &stream, version); },
                    [&] { gRand = gen; return N(n)->Read(&stream, version); }, 0xff);
        }
    }
}
