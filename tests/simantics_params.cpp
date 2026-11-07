// Differential tests for XPrimParam and the *Param classes (src/simantics/SimAnticsParams.*): construction
// (0xc0aa6a, 0xc0a9f5), the pool wrappers (0xc0aa89 alloc, 0xc0aa93 free-or-null, 0xc0aacf pool Free), the
// serialisation slots (Write 0xc0b0b7, Read 0xc0ac22 and its five wrappers 0xc0abf3, 0xc0ab29, 0xc0ab83,
// 0xc0ab4c, 0xc0ab08), the eleven conversion slots (0xc0afd2, 0xc0b072, 0xc0ae6e, 0xc0ad18, 0xc0ad32,
// 0xc0b02b, 0xc0accb, 0xc0ae3f, 0xc0ad87, 0xc0addb, 0xc0afe6) and the node's operand builder 0x46a860.
//
// Pool tests: the global allocator object of the XPrimParam pool (image 0x10fd4c4) is pointed at a 16-page
// region of our own, with a vtable whose NewPage/FreePage slots are fakes. The pool is put into a random
// state by running random allocations and frees through the original, then a snapshot (region bytes,
// allocator words, fake-page counter, freed pages) is taken. Each side runs from the same snapshot, so
// the returned block, every byte of the region and the allocator compare exactly (pad bytes included).
//
// Stream tests: a fake stream object whose slots 5, 6 and 18 record the call and (for reads) fill the
// destination from a generator reseeded for each side.
#include "difftest.h"
#include "game/game.h"
#include "game/vtable.h"
#include "simantics/SimAnticsParams.h"
#include "simantics_params_fixture.h"

#include <Windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace {


// ----- the fake stream -----------------------------------------------------------------------------------

std::vector<uint32_t> gTrace;
uint32_t gRand = 1;
uint8_t gError = 0;
const uint8_t *gObjBase = nullptr;
uint8_t gVersionResult = 0;
void *gStreamVT[24];
void *gObjVT[4];

uint32_t NextRand()
{
    gRand ^= gRand << 13;
    gRand ^= gRand >> 17;
    gRand ^= gRand << 5;
    return gRand;
}

uint32_t Off(const void *p)
{
    return static_cast<uint32_t>(static_cast<const uint8_t *>(p) - gObjBase);
}

void __fastcall RdByte(void *, void *, void *out)
{
    gTrace.push_back(5);
    gTrace.push_back(Off(out));
    uint32_t r = NextRand();
    // Mostly small values: the version byte steers the conversions (0..5 matter), the rest is full range.
    *static_cast<uint8_t *>(out) = static_cast<uint8_t>(r % 4 == 0 ? (r >> 8) : (r >> 8) % 6);
}

void __fastcall RdWord(void *, void *, void *out)
{
    gTrace.push_back(6);
    gTrace.push_back(Off(out));
    uint32_t r = NextRand();
    uint16_t v = static_cast<uint16_t>(r % 5 == 0 ? 0 : r >> 8);
    std::memcpy(out, &v, sizeof(v));
}

void __fastcall WrByte(void *, void *, uint32_t v)
{
    gTrace.push_back(5);
    gTrace.push_back(v);
}

void __fastcall WrWord(void *, void *, uint32_t v)
{
    gTrace.push_back(6);
    gTrace.push_back(v);
}

uint8_t __fastcall StreamErr(void *, void *)
{
    gTrace.push_back(18);
    return gError;
}

void __fastcall ObjConvert(void *self, void *)
{
    gTrace.push_back(2);
    gTrace.push_back(Off(self));
}

uint8_t __fastcall ObjVersion(void *, void *)
{
    return gVersionResult;
}

void SetStreamVT(bool reading)
{
    std::memset(gStreamVT, 0, sizeof(gStreamVT));
    gStreamVT[5] = reinterpret_cast<void *>(reading ? reinterpret_cast<void *>(&RdByte) : reinterpret_cast<void *>(&WrByte));
    gStreamVT[6] = reinterpret_cast<void *>(reading ? reinterpret_cast<void *>(&RdWord) : reinterpret_cast<void *>(&WrWord));
    gStreamVT[18] = reinterpret_cast<void *>(&StreamErr);
}

struct alignas(8) Obj {
    uint8_t b[0x20];
};

void FillObj(Obj &o, uint32_t seed)
{
    std::mt19937 rng(seed);
    for (auto &x : o.b) x = static_cast<uint8_t>(rng());
    uint32_t vt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gObjVT));
    std::memcpy(o.b, &vt, 4);
    static constexpr uint8_t kFlags[] = {0, 1, 2, 3, 4, 5, 0x80, 0xff};
    o.b[0x14] = rng() % 3 ? kFlags[rng() % 8] : static_cast<uint8_t>(rng());
}

void InitObjVT(const difftest::Context &ctx)
{
    std::memcpy(gObjVT, ctx.image.At(0xdc93f4), sizeof(gObjVT));
    gObjVT[2] = reinterpret_cast<void *>(&ObjConvert);
    gObjVT[3] = reinterpret_cast<void *>(&ObjVersion);
}

XPrimParam *P(Obj &o)
{
    return reinterpret_cast<XPrimParam *>(o.b);
}

} // namespace

// ===== construction ==========================================================================================

DIFF_TEST(XPrimParam_Construct, 0xc0aa6a)
{
    auto orig = ctx.Original<XPrimParam *(__thiscall *)(void *)>(0xc0aa6a);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Obj a, b;
        FillObj(a, seed);
        std::memcpy(&b, &a, sizeof(a));
        XPrimParam *ra = orig(a.b);
        XPrimParam *rb = P(b)->Construct();
        ++ctx.cases;
        if (ra != P(a) || rb != P(b)) ctx.Fail("Construct returned wrong pointer (%p, %p)", ra, rb);
        if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("Construct seed %u: bytes differ", seed);
        if (P(b)->mVTable != GameAddress(XPrimParam::kBaseVTable) || P(b)->mVersion != 0) ctx.Fail("Construct: unexpected state");
    }
}

DIFF_TEST(XPrimParam_CopyConstruct, 0xc0a9f5)
{
    auto orig = ctx.Original<XPrimParam *(__thiscall *)(void *, const void *)>(0xc0a9f5);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        Obj src, a, b;
        FillObj(src, seed);
        FillObj(a, seed + 1000);
        std::memcpy(&b, &a, sizeof(a));
        XPrimParam *ra = orig(a.b, src.b);
        XPrimParam *rb = P(b)->CopyConstruct(*P(src));
        ++ctx.cases;
        if (ra != P(a) || rb != P(b)) ctx.Fail("CopyConstruct returned wrong pointer");
        if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("CopyConstruct seed %u: bytes differ", seed);
        if (std::memcmp(a.b + 4, src.b + 4, 0x11) != 0) ctx.Fail("CopyConstruct: bytes +4..+0x14 were not copied");
    }
}

// ===== the pool wrappers =====================================================================================

DIFF_TEST(XPrimParam_AllocateBlock, 0xc0aa89)
{
    simfix::PoolFixture pool(ctx);
    auto orig = ctx.Original<void *(__cdecl *)(uint32_t)>(0xc0aa89);
    for (const simfix::PoolCase &c : simfix::PoolCases(300)) {
        pool.Randomize(c.seed, c.steps, c.allocOnly, c.postFrees);
        simfix::Snapshot start = simfix::Take();
        // The size argument is ignored: vary it.
        uint32_t size = c.seed % 3 == 0 ? 0x18 : c.seed;
        void *a = orig(size);
        simfix::Snapshot afterA = simfix::Take();
        simfix::Restore(start);
        void *b = XPrimParam::AllocateBlock(size);
        simfix::Snapshot afterB = simfix::Take();
        ++ctx.cases;
        if (a != b) ctx.Fail("AllocateBlock seed %u steps %d: blocks differ (%p, %p)", c.seed, c.steps, a, b);
        if (!(afterA == afterB)) ctx.Fail("AllocateBlock seed %u steps %d: pool state differs", c.seed, c.steps);
    }
}

namespace {

// Frees through `which`: 0 = pool Free 0xc0aacf (this = the allocator), 1 = FreeBlock 0xc0aa93 (cdecl).
void RunFree(difftest::Context &ctx, int which, bool original, void *block)
{
    if (which == 0) {
        if (original) ctx.Original<void(__thiscall *)(void *, void *)>(0xc0aacf)(ctx.image.At(XPrimParam::kPoolAllocator), block);
        else reinterpret_cast<RZFastFixed0x18 *>(ctx.image.At(XPrimParam::kPoolAllocator))->Free(block);
    } else {
        if (original) ctx.Original<void(__cdecl *)(void *)>(0xc0aa93)(block);
        else XPrimParam::FreeBlock(block);
    }
}

void FreeTest(difftest::Context &ctx, int which)
{
    simfix::PoolFixture pool(ctx);
    std::mt19937 rng(0x77 + which);
    int n = 0;
    for (const simfix::PoolCase &c : simfix::PoolCases(500)) {
        pool.Randomize(c.seed, c.steps, c.allocOnly, c.postFrees);
        if (pool.held.empty() && which == 0) continue;
        // Several frees in a row (emptying pages, returning full pages to the ring), each compared.
        int rounds = 1 + static_cast<int>(rng() % 6);
        for (int r = 0; r < rounds && !pool.held.empty(); ++r) {
            size_t k = rng() % pool.held.size();
            void *block = pool.held[k];
            pool.held.erase(pool.held.begin() + static_cast<long>(k));
            simfix::Snapshot start = simfix::Take();
            RunFree(ctx, which, true, block);
            simfix::Snapshot afterA = simfix::Take();
            simfix::Restore(start);
            RunFree(ctx, which, false, block);
            simfix::Snapshot afterB = simfix::Take();
            ++ctx.cases;
            if (!(afterA == afterB)) ctx.Fail("Free(%d) seed %u steps %d round %d: pool state differs", which, c.seed, c.steps, r);
            // Carry the original's result forward.
            simfix::Restore(afterA);
            ++n;
        }
        if (which == 1) {
            // The null pointer releases nothing.
            simfix::Snapshot start = simfix::Take();
            RunFree(ctx, which, true, nullptr);
            simfix::Snapshot afterA = simfix::Take();
            simfix::Restore(start);
            RunFree(ctx, which, false, nullptr);
            ++ctx.cases;
            if (!(afterA == simfix::Take()) || !(start == afterA)) ctx.Fail("FreeBlock(null) changed the pool");
        }
    }
    if (n == 0) ctx.Fail("no frees ran");
}

} // namespace

DIFF_TEST(XPrimParam_PoolFree, 0xc0aacf)
{
    FreeTest(ctx, 0);
}

DIFF_TEST(XPrimParam_FreeBlock, 0xc0aa93)
{
    FreeTest(ctx, 1);
}

// ===== the operand builder ===================================================================================

DIFF_TEST(XPrimParam_BuildForOpcode, 0x46a860)
{
    simfix::PoolFixture pool(ctx);
    auto orig = ctx.Original<XPrimParam *(__stdcall *)(uint32_t)>(0x46a860);
    std::mt19937 rng(0xb1d);
    std::vector<uint32_t> ops;
    for (uint32_t op = 0; op < 0x200; ++op) ops.push_back(op);
    for (uint32_t op : {0x7ffeu, 0x7fffu, 0x8000u, 0x8001u, 0xfe00u, 0xff00u, 0xfffeu, 0xffffu})
        ops.push_back(op);
    // Upper bits of the argument are ignored (only the low word is used).
    for (int i = 0; i < 200; ++i) ops.push_back(static_cast<uint32_t>(rng()));
    const std::vector<simfix::PoolCase> cases = simfix::PoolCases(0);
    int cell = 0;
    for (uint32_t op : ops) {
        const simfix::PoolCase &pc = cases[static_cast<size_t>(cell++) % cases.size()];
        pool.Randomize(pc.seed + op, pc.steps + static_cast<int>(op % 5), op % 2 == 0, pc.postFrees);
        simfix::Snapshot start = simfix::Take();
        XPrimParam *a = orig(op);
        simfix::Snapshot afterA = simfix::Take();
        simfix::Restore(start);
        XPrimParam *b = XPrimParam::BuildForOpcode(op);
        simfix::Snapshot afterB = simfix::Take();
        ++ctx.cases;
        if (a != b) {
            ctx.Fail("BuildForOpcode(0x%x): objects at different blocks (%p, %p)", op, a, b);
            continue;
        }
        if (!(afterA == afterB)) ctx.Fail("BuildForOpcode(0x%x): pool or object bytes differ", op);
        if (a && std::memcmp(a, b, 0x18) != 0) ctx.Fail("BuildForOpcode(0x%x): object bytes differ", op);
    }
}

// ===== serialisation =========================================================================================

DIFF_TEST(XPrimParam_Write, 0xc0b0b7)
{
    InitObjVT(ctx);
    SetStreamVT(false);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0xc0b0b7);
    struct Stream {
        const void *vt;
    } stream{gStreamVT};
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Obj o;
        FillObj(o, seed);
        std::mt19937 rng(seed);
        gVersionResult = seed % 4 == 0 ? 0 : static_cast<uint8_t>(rng());
        gError = seed % 3 == 0 ? 0 : static_cast<uint8_t>(rng());
        Obj copy = o;
        gTrace.clear();
        uint8_t ra = orig(o.b, &stream);
        std::vector<uint32_t> ta = gTrace;
        gTrace.clear();
        uint8_t rb = P(o)->Write(&stream);
        std::vector<uint32_t> tb = gTrace;
        ++ctx.cases;
        if (ra != rb) ctx.Fail("Write seed %u: returned %u, ours %u", seed, ra, rb);
        if (ta != tb) ctx.Fail("Write seed %u: stream calls differ (%zu vs %zu values)", seed, ta.size(), tb.size());
        if (ta.size() != 2 * 9 + 1) ctx.Fail("Write: expected 9 puts and the error probe, got %zu words", ta.size());
        if (std::memcmp(&copy, &o, sizeof(o)) != 0) ctx.Fail("Write changed the object");
    }
}

namespace {

using ReadFn = uint8_t(__thiscall *)(void *, void *, int32_t);

std::vector<int32_t> Versions()
{
    std::vector<int32_t> v;
    for (int32_t x = -32770; x <= -32755; ++x) v.push_back(x);
    for (int32_t x : {0, 1, -1, 2, 0x8003, 0x8005, 0x7fffffff, static_cast<int32_t>(0x80000000), -100000, 100000}) v.push_back(x);
    return v;
}

void ReadTest(difftest::Context &ctx, uint32_t rva, bool (XPrimParam::*ours)(void *, int32_t))
{
    InitObjVT(ctx);
    SetStreamVT(true);
    auto orig = ctx.Original<ReadFn>(rva);
    struct Stream {
        const void *vt;
    } stream{gStreamVT};
    uint32_t seed = 0;
    for (int32_t version : Versions()) {
        for (int rep = 0; rep < 24; ++rep, ++seed) {
            Obj a, b;
            FillObj(a, seed);
            b = a;
            gError = seed % 3 == 0 ? 0 : static_cast<uint8_t>(seed * 37);
            gTrace.clear();
            gRand = seed * 2654435761u + 1;
            gObjBase = a.b;
            uint8_t ra = orig(a.b, &stream, version);
            std::vector<uint32_t> ta = gTrace;
            gTrace.clear();
            gRand = seed * 2654435761u + 1;
            gObjBase = b.b;
            uint8_t rb = static_cast<uint8_t>((P(b)->*ours)(&stream, version));
            std::vector<uint32_t> tb = gTrace;
            ++ctx.cases;
            if (ra != rb) ctx.Fail("Read(0x%x) version %d seed %u: returned %u, ours %u", rva, version, seed, ra, rb);
            if (ta != tb) ctx.Fail("Read(0x%x) version %d seed %u: stream/slot calls differ", rva, version, seed);
            if (std::memcmp(&a, &b, sizeof(a)) != 0) ctx.Fail("Read(0x%x) version %d seed %u: object bytes differ", rva, version, seed);
        }
    }
}

} // namespace

DIFF_TEST(XPrimParam_Read, 0xc0ac22)
{
    ReadTest(ctx, 0xc0ac22, &XPrimParam::Read);
}

DIFF_TEST(RunPrivateTreeParam_Read, 0xc0abf3)
{
    ReadTest(ctx, 0xc0abf3, &XPrimParam::ReadRunPrivateTree);
}

DIFF_TEST(BudgetParam_Read, 0xc0ab29)
{
    ReadTest(ctx, 0xc0ab29, &XPrimParam::ReadBudget);
}

DIFF_TEST(MakeActionStringParam_Read, 0xc0ab83)
{
    ReadTest(ctx, 0xc0ab83, &XPrimParam::ReadMakeActionString);
}

DIFF_TEST(DialogParam_Read, 0xc0ab4c)
{
    ReadTest(ctx, 0xc0ab4c, &XPrimParam::ReadDialog);
}

DIFF_TEST(XPrimParam_ReadThenConvert, 0xc0ab08)
{
    ReadTest(ctx, 0xc0ab08, &XPrimParam::ReadThenConvert);
}

// ===== conversions ===========================================================================================

namespace {

void ConvertTest(difftest::Context &ctx, uint32_t rva, void (XPrimParam::*ours)())
{
    InitObjVT(ctx);
    auto orig = ctx.Original<void(__thiscall *)(void *)>(rva);
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        Obj a, b;
        FillObj(a, seed);
        // Byte 5 steers the NewRelationship switch: cover 0..5 and beyond evenly.
        if (seed % 2) a.b[5] = static_cast<uint8_t>(seed % 7);
        b = a;
        orig(a.b);
        (P(b)->*ours)();
        ++ctx.cases;
        if (std::memcmp(&a, &b, sizeof(a)) != 0) {
            int at = 0;
            while (a.b[at] == b.b[at]) ++at;
            ctx.Fail("Convert(0x%x) seed %u: first difference at +0x%x (original %02x, ours %02x)", rva, seed, at, a.b[at], b.b[at]);
        }
    }
}

} // namespace

DIFF_TEST(SetToNextParam_Convert, 0xc0afd2)
{
    ConvertTest(ctx, 0xc0afd2, &XPrimParam::ConvertSetToNext);
}

DIFF_TEST(TurnTowardsParam_Convert, 0xc0b072)
{
    ConvertTest(ctx, 0xc0b072, &XPrimParam::ConvertTurnTowards);
}

DIFF_TEST(NewRelationshipParam_Convert, 0xc0ae6e)
{
    ConvertTest(ctx, 0xc0ae6e, &XPrimParam::ConvertNewRelationship);
}

DIFF_TEST(FindGoodLocationParam_Convert, 0xc0ad18)
{
    ConvertTest(ctx, 0xc0ad18, &XPrimParam::ConvertFindGoodLocation);
}

DIFF_TEST(GotoRoutingSlotParam_Convert, 0xc0ad32)
{
    ConvertTest(ctx, 0xc0ad32, &XPrimParam::ConvertGotoRoutingSlot);
}

DIFF_TEST(SnapParam_Convert, 0xc0b02b)
{
    ConvertTest(ctx, 0xc0b02b, &XPrimParam::ConvertSnap);
}

DIFF_TEST(AnimateOverlayParam_Convert, 0xc0accb)
{
    ConvertTest(ctx, 0xc0accb, &XPrimParam::ConvertAnimateOverlay);
}

DIFF_TEST(MaterialChangeParam_Convert, 0xc0ae3f)
{
    ConvertTest(ctx, 0xc0ae3f, &XPrimParam::ConvertMaterialChange);
}

DIFF_TEST(LookAtParam_Convert, 0xc0ad87)
{
    ConvertTest(ctx, 0xc0ad87, &XPrimParam::ConvertLookAt);
}

DIFF_TEST(ManageInventoryParam_Convert, 0xc0addb)
{
    ConvertTest(ctx, 0xc0addb, &XPrimParam::ConvertManageInventory);
}

DIFF_TEST(SnapIntoParam_Convert, 0xc0afe6)
{
    ConvertTest(ctx, 0xc0afe6, &XPrimParam::ConvertSnapInto);
}
