// Differential tests for the shader name pool (src/sg/cShaderNamePool.cpp, nGZSceneGraph::NamePool):
//   SG_NamePoolConstruct 0xc62f50   SG_NamePoolCopyConstruct 0xc62ee0   SG_NamePoolClear 0xc630e0
//   SG_NamePoolDestroy 0xc62f90     SG_NamePoolAssign 0xc63030          SG_NamePoolAdd 0xc63120
//
// Every case builds two equal pools with the image's own Add (the same seed: entries of fake names of 12 to 300 bytes,
// sometimes over a thousand of them so both buffers pass the 4 KB large-block size), runs the image's function on one and
// ours on the other, and compares the pools afterwards (sizes, capacities, whether each buffer stayed where it was, the
// bytes of the character buffer and the offsets), the log of the calls on the names (slot 0 destroy, slot 1 copy) and the
// blocks and bytes allocated.
#include "difftest.h"
#include "sg/cShaderInit.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

using namespace nGZSceneGraph;

namespace {

std::string gLog;

void Note(const char *what, uint32_t a = 0, uint32_t b = 0)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %x %x\n", what, a, b);
    gLog += buf;
}

// A name: slot 0 destroys (logs), slot 1 writes a copy (a fill byte of its own, then the header).
struct FakeName {
    void **vt;
    uint16_t m4;
    uint16_t size;
    uint32_t id;
};
void __fastcall NameDestroy(FakeName *self, void *, uint32_t flags) { Note("destroy", self->id, flags); }
void __fastcall NameCopy(FakeName *self, void *, char *dest)
{
    Note("copy", self->id, self->size);
    std::memset(dest, int(0x40 + (self->id & 0x3f)), self->size);
    std::memcpy(dest, self, sizeof(FakeName));
}
void *gNameVt[2] = {reinterpret_cast<void *>(&NameDestroy), reinterpret_cast<void *>(&NameCopy)};

constexpr uint32_t kNames = 8;
FakeName gNames[kNames];
void InitNames()
{
    const uint16_t sizes[kNames] = {12, 13, 16, 17, 40, 63, 128, 300}; // at least the header
    for (uint32_t i = 0; i < kNames; ++i) gNames[i] = FakeName{gNameVt, uint16_t(0x1000 + i), sizes[i], 0x100 + i};
    gLog.reserve(1 << 20);
}

using AddFn = void(__thiscall *)(NamePool *, const void *);
using DestroyFn = void(__thiscall *)(NamePool *);

// A pool of n entries added by the image's Add (the copies number from `seed`, so each pool's entries are its own).
void Build(difftest::Context &ctx, NamePool &pool, uint32_t seed, uint32_t n)
{
    const AddFn add = ctx.Original<AddFn>(0xc63120);
    std::memset(&pool, 0, sizeof(pool));
    std::mt19937 rng(seed);
    for (uint32_t i = 0; i < n; ++i) add(&pool, &gNames[rng() % kNames]);
}

uint32_t Entries(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 1030 + rng() % 40; // the offsets pass 4 KB
    case 3: return 15 + rng() % 30;   // the characters may
    default: return rng() % 12;
    }
}

std::string Snap(const NamePool &p, const NamePool &before)
{
    std::string s;
    char buf[96];
    const char *cb = reinterpret_cast<const char *>(uintptr_t(p.w[0]));
    std::snprintf(buf, sizeof(buf), "chars %x/%x moved %d offs %x/%x moved %d;", p.w[1] - p.w[0], p.w[2] - p.w[0],
                  p.w[0] != before.w[0], p.w[4] - p.w[3], p.w[5] - p.w[3], p.w[3] != before.w[3]);
    s += buf;
    if (cb) s.append(cb, p.w[1] - p.w[0]);
    const uint32_t *ob = reinterpret_cast<const uint32_t *>(uintptr_t(p.w[3]));
    for (uint32_t i = 0; ob && i < (p.w[4] - p.w[3]) / 4; ++i) {
        std::snprintf(buf, sizeof(buf), "%x,", ob[i]);
        s += buf;
    }
    return s;
}

// Runs `original` on a and `ours` on b (equal pools), compares, and frees both with the image's Destroy.
template <class A, class B>
void Compare(difftest::Context &ctx, const char *what, uint32_t i, NamePool &a, NamePool &b, A original, B ours)
{
    const NamePool beforeA = a, beforeB = b;
    gLog.clear();
    difftest::AllocationCounter countA;
    const uint32_t retA = original(&a);
    countA.Stop();
    const std::string logA = gLog;
    gLog.clear();
    difftest::AllocationCounter countB;
    const uint32_t retB = ours(&b);
    countB.Stop();
    ++ctx.cases;
    const std::string sa = Snap(a, beforeA), sb = Snap(b, beforeB);
    if (sa != sb || logA != gLog || retA != retB || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes())
        ctx.Fail("%s case %u: ret %x/%x, blocks %lld/%lld bytes %lld/%lld, pools equal %d (%.80s / %.80s)\noriginal\n%.600sours\n%.600s",
                 what, i, retA, retB, countA.Blocks(), countB.Blocks(), countA.Bytes(), countB.Bytes(), sa == sb,
                 sa.c_str(), sb.c_str(), logA.c_str(), gLog.c_str());
    const DestroyFn destroy = ctx.Original<DestroyFn>(0xc62f90);
    destroy(&a);
    destroy(&b);
}

// The return value of a function returning this, as an offset from the pool (0 when it is the pool).
uint32_t Rel(const void *ret, const NamePool *pool) { return uint32_t(uintptr_t(ret) - uintptr_t(pool)); }

} // namespace

DIFF_TEST(SG_NamePoolConstruct, 0xc62f50)
{
    using Fn = NamePool *(__thiscall *)(NamePool *);
    const Fn original = ctx.Original<Fn>(0xc62f50);
    std::mt19937 rng(0xc62f50);
    for (uint32_t i = 0; i < 50; ++i) {
        uint32_t a[8], b[8];
        for (uint32_t &v : a) v = rng();
        std::memcpy(b, a, sizeof(a));
        const uint32_t ra = Rel(original(reinterpret_cast<NamePool *>(a + 1)), reinterpret_cast<NamePool *>(a + 1));
        const uint32_t rb = Rel(reinterpret_cast<NamePool *>(b + 1)->Construct(), reinterpret_cast<NamePool *>(b + 1));
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0 || ra != rb) ctx.Fail("case %u: ret %x/%x, memory equal %d", i, ra, rb, std::memcmp(a, b, sizeof(a)) == 0);
    }
}

DIFF_TEST(SG_NamePoolClear, 0xc630e0)
{
    InitNames();
    using Fn = void(__thiscall *)(NamePool *);
    const Fn original = ctx.Original<Fn>(0xc630e0);
    std::mt19937 rng(0xc630e0);
    for (uint32_t i = 0; i < 200; ++i) {
        const uint32_t seed = rng(), n = Entries(rng);
        NamePool a, b;
        Build(ctx, a, seed, n);
        Build(ctx, b, seed, n);
        Compare(ctx, "clear", i, a, b, [&](NamePool *p) { original(p); return 0u; }, [](NamePool *p) { p->Clear(); return 0u; });
    }
}

DIFF_TEST(SG_NamePoolDestroy, 0xc62f90)
{
    InitNames();
    using Fn = void(__thiscall *)(NamePool *);
    const Fn original = ctx.Original<Fn>(0xc62f90);
    std::mt19937 rng(0xc62f90);
    for (uint32_t i = 0; i < 200; ++i) {
        const uint32_t seed = rng(), n = Entries(rng);
        NamePool a, b;
        Build(ctx, a, seed, n);
        Build(ctx, b, seed, n);
        Compare(ctx, "destroy", i, a, b, [&](NamePool *p) { original(p); return 0u; }, [](NamePool *p) { p->Destroy(); return 0u; });
    }
}

DIFF_TEST(SG_NamePoolAdd, 0xc63120)
{
    InitNames();
    using Fn = void(__thiscall *)(NamePool *, const void *);
    const Fn original = ctx.Original<Fn>(0xc63120);
    std::mt19937 rng(0xc63120);
    uint32_t grown = 0, inPlace = 0;
    for (uint32_t i = 0; i < 400; ++i) {
        const uint32_t seed = rng(), n = Entries(rng);
        const FakeName *name = &gNames[rng() % kNames];
        NamePool a, b;
        Build(ctx, a, seed, n);
        Build(ctx, b, seed, n);
        const uint32_t room = a.w[2] - a.w[1];
        (room >= name->size ? inPlace : grown) += 1;
        Compare(ctx, "add", i, a, b, [&](NamePool *p) { original(p, name); return 0u; },
                [&](NamePool *p) { p->Add(reinterpret_cast<const cIShaderName *>(name)); return 0u; });
    }
    if (grown < 50 || inPlace < 50) ctx.Fail("coverage: grown %u, in place %u", grown, inPlace);
}

DIFF_TEST(SG_NamePoolAssign, 0xc63030)
{
    InitNames();
    using Fn = void(__thiscall *)(NamePool *, const NamePool *);
    const Fn original = ctx.Original<Fn>(0xc63030);
    std::mt19937 rng(0xc63030);
    uint32_t self = 0, reserved = 0;
    for (uint32_t i = 0; i < 300; ++i) {
        const uint32_t seed = rng(), n = Entries(rng), srcSeed = rng(), srcN = Entries(rng);
        const bool toSelf = rng() % 8 == 0;
        NamePool src;
        Build(ctx, src, srcSeed, srcN);
        NamePool a, b;
        Build(ctx, a, seed, n);
        Build(ctx, b, seed, n);
        self += toSelf;
        reserved += !toSelf && src.w[1] - src.w[0] > a.w[2] - a.w[0];
        Compare(ctx, "assign", i, a, b, [&](NamePool *p) { original(p, toSelf ? p : &src); return 0u; },
                [&](NamePool *p) { p->Assign(toSelf ? p : &src); return 0u; });
        ctx.Original<DestroyFn>(0xc62f90)(&src);
    }
    if (self < 20 || reserved < 50) ctx.Fail("coverage: self %u, reserved %u", self, reserved);
}

DIFF_TEST(SG_NamePoolCopyConstruct, 0xc62ee0)
{
    InitNames();
    using Fn = NamePool *(__thiscall *)(NamePool *, const NamePool *);
    const Fn original = ctx.Original<Fn>(0xc62ee0);
    std::mt19937 rng(0xc62ee0);
    for (uint32_t i = 0; i < 200; ++i) {
        const uint32_t srcSeed = rng(), srcN = Entries(rng), fill = rng();
        NamePool src;
        Build(ctx, src, srcSeed, srcN);
        NamePool a, b;
        std::memset(&a, int(fill & 0xff), sizeof(a)); // raw storage: the constructor zeroes it first
        std::memset(&b, int(fill & 0xff), sizeof(b));
        const NamePool before = a;
        gLog.clear();
        difftest::AllocationCounter countA;
        const uint32_t retA = Rel(original(&a, &src), &a);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        const uint32_t retB = Rel(b.CopyConstruct(&src), &b);
        countB.Stop();
        ++ctx.cases;
        const std::string sa = Snap(a, before), sb = Snap(b, before);
        if (sa != sb || logA != gLog || retA != retB || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes())
            ctx.Fail("case %u: ret %x/%x, blocks %lld/%lld bytes %lld/%lld, pools equal %d\noriginal\n%.600sours\n%.600s", i, retA,
                     retB, countA.Blocks(), countB.Blocks(), countA.Bytes(), countB.Bytes(), sa == sb, logA.c_str(), gLog.c_str());
        const DestroyFn destroy = ctx.Original<DestroyFn>(0xc62f90);
        destroy(&a);
        destroy(&b);
        destroy(&src);
    }
}
