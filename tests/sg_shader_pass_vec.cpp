// Differential tests for the shader's pass vector growth (src/sg/cShader.cpp):
//   SG_ShaderPassFillDefault 0xaa47f0   (cShader::PassVec::FillDefault, default passes on raw storage)
//   SG_ShaderPassReallocate  0xaa45e0   (cShader::PassVec::Reallocate, grow the block and move the passes over)
//
// FillDefault runs on storage prefilled with the same random bytes on both sides; the whole storage (the pad byte +0x3f
// included, which it leaves) and the end pointer it gives are compared. Reallocate runs on two vectors built alike from
// one seed (heap passes with part vectors, texture handles, small and large blocks); the passes are compared by value,
// with the sizes, the allocations and frees, and the calls the copies and destructions make on the parts and on the
// texture manager the image caches at 0x10f8f94 (a fake planted there logs them).
#include "difftest.h"
#include "sg/cShader.h"

#include <algorithm>
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

struct FakePart : cIShaderPart {
    uint32_t id = 0;
    void Slot0() override { Note("s0", id); }
    void AddRef() override { Note("addref", id); }
    void Release() override { Note("release", id); }
    void Reset() override { Note("reset", id); }
    void Slot4() override { Note("s4", id); }
    void Slot5() override { Note("s5", id); }
    void Slot6() override { Note("s6", id); }
    void Slot7() override { Note("s7", id); }
    ShaderFrame *MapFrame(ShaderFrame *, ShaderRenderCtx *) override { Note("s8", id); return nullptr; }
    const uint32_t *Slot9(void *, const void *) override { Note("s9", id); return nullptr; }
    void Slot10() override { Note("s10", id); }
    void Apply(ShaderRenderCtx *, ShaderFrame *) override { Note("s11", id); }
    void Slot12(void *, const void *) override { Note("s12", id); }
    void EndState(void *) override { Note("endstate", id); }
};

FakePart gParts[5];

// The texture manager: 18 counts a copied handle, 19 releases one, 23 looks one up.
void *__fastcall MgrGet(void *, void *, uint32_t id)
{
    Note("texture", id);
    return &gParts[4];
}
void __fastcall MgrDestroy(void *, void *, uint32_t id) { Note("destroy", id); }
void __fastcall MgrCount(void *, void *, uint32_t id) { Note("count", id); }
void *gMgrVt[40];
void *gMgrObject[1] = {gMgrVt};

struct PlantManager {
    void **cache;
    void *saved;
    explicit PlantManager(difftest::Context &ctx)
    {
        for (uint32_t i = 0; i < 5; ++i) gParts[i].id = i + 1;
        std::memset(gMgrVt, 0, sizeof(gMgrVt));
        gMgrVt[18] = reinterpret_cast<void *>(&MgrCount);
        gMgrVt[19] = reinterpret_cast<void *>(&MgrDestroy);
        gMgrVt[23] = reinterpret_cast<void *>(&MgrGet);
        cache = ctx.image.At<void *>(0x10f8f94);
        saved = *cache;
        *cache = gMgrObject;
    }
    ~PlantManager() { *cache = saved; }
};

// A block as the game's allocator lays it out: operator new below 4 KB, else padded, 32-byte aligned, the real pointer in
// the dword before it.
void *NewBlock(size_t bytes)
{
    if (bytes < 0x1000) return ::operator new(bytes);
    uint8_t *raw = static_cast<uint8_t *>(::operator new(bytes + 0x23));
    void **first = reinterpret_cast<void **>((reinterpret_cast<uintptr_t>(raw) + 0x23) & ~uintptr_t(0x1f));
    first[-1] = raw;
    return first;
}

void HeapVec(ShaderPartVec &v, std::mt19937 &rng)
{
    const uint32_t k = rng() % 6;
    if (k == 0) {
        v = ShaderPartVec{};
        return;
    }
    const uint32_t n = rng() % 4;
    const uint32_t cap = k == 5 ? 1024 + (rng() % 3) * 4 : n + 1 + rng() % 3;
    auto **first = static_cast<cIShaderPart **>(NewBlock(cap * sizeof(void *)));
    for (uint32_t i = 0; i < n; ++i) first[i] = &gParts[rng() % 4];
    v.mBegin = first;
    v.mEnd = first + n;
    v.mCap = first + cap;
}

cIShaderPart *Pick(std::mt19937 &rng) { return rng() % 5 == 4 ? nullptr : &gParts[rng() % 4]; }

void HeapPass(std::mt19937 &rng, ShaderPass &p)
{
    for (uint32_t i = 0; i < sizeof(p); ++i) reinterpret_cast<uint8_t *>(&p)[i] = uint8_t(rng());
    p.mApply = nullptr;
    p.mB4 = rng() % 3;
    p.mP8 = Pick(rng);
    p.mPc = Pick(rng);
    p.mP10 = Pick(rng);
    p.mP20 = Pick(rng);
    p.mP48 = Pick(rng);
    p.mP4c = Pick(rng);
    p.mP50 = Pick(rng);
    p.mP54 = Pick(rng);
    HeapVec(p.mV14, rng);
    HeapVec(p.mV24, rng);
    HeapVec(p.mV30, rng);
    HeapVec(p.mV58, rng);
    const bool handles = rng() % 3 != 0;
    for (uint32_t &h : p.mHandles) h = (handles && rng() % 3 == 0) ? 1 + rng() % 50 : 0xffffffffu;
}

// A vector of n heap passes with room for cap (cap 0: the null vector).
void BuildVec(uint32_t seed, uint32_t n, uint32_t cap, cShader::PassVec &v)
{
    std::mt19937 rng(seed);
    if (cap == 0) {
        v = cShader::PassVec{};
        return;
    }
    auto *first = static_cast<ShaderPass *>(NewBlock(cap * sizeof(ShaderPass)));
    for (uint32_t i = 0; i < n; ++i) HeapPass(rng, first[i]);
    v.mBegin = first;
    v.mEnd = first + n;
    v.mCap = first + cap;
}

void SnapBytes(std::string &o, const void *p, size_t n) { o.append(static_cast<const char *>(p), n); }

void SnapVec(std::string &o, const ShaderPartVec &v)
{
    uint32_t n[2] = {0xffffffffu, 0xffffffffu};
    if (v.mBegin) {
        n[0] = uint32_t(v.mEnd - v.mBegin);
        n[1] = uint32_t(v.mCap - v.mBegin);
    }
    SnapBytes(o, n, sizeof(n));
    if (v.mBegin) SnapBytes(o, v.mBegin, size_t(v.mEnd - v.mBegin) * sizeof(void *));
}

// A pass by value: its words with the vectors' addresses replaced by their contents; the pad byte +0x3f (the copy
// constructor leaves it, so it is whatever the storage held) left out.
void SnapPass(std::string &o, const ShaderPass &p)
{
    ShaderPass c;
    std::memcpy(&c, &p, sizeof(c));
    std::memset(&c.mV14, 0, sizeof(c.mV14));
    std::memset(&c.mV24, 0, sizeof(c.mV24));
    std::memset(&c.mV30, 0, sizeof(c.mV30));
    std::memset(&c.mV58, 0, sizeof(c.mV58));
    c.mPad3f = 0;
    SnapBytes(o, &c, sizeof(c));
    SnapVec(o, p.mV14);
    SnapVec(o, p.mV24);
    SnapVec(o, p.mV30);
    SnapVec(o, p.mV58);
}

std::string SnapPassVec(const cShader::PassVec &v)
{
    std::string o;
    uint32_t n[3] = {v.mBegin ? 1u : 0u, 0, 0};
    if (v.mBegin) {
        n[1] = uint32_t(v.mEnd - v.mBegin);
        n[2] = uint32_t(v.mCap - v.mBegin);
    }
    SnapBytes(o, n, sizeof(n));
    for (const ShaderPass *p = v.mBegin; p && p != v.mEnd; ++p) SnapPass(o, *p);
    return o;
}

} // namespace

// ---- 0xaa47f0 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassFillDefault, 0xaa47f0)
{
    using Fn = ShaderPass *(__cdecl *)(ShaderPass *, uint32_t, cShader::PassVec *);
    const Fn original = ctx.Original<Fn>(0xaa47f0);
    std::mt19937 rng(0xaa47f0);
    constexpr uint32_t kMax = 6;
    int filled = 0;
    for (uint32_t i = 0; i < 300; ++i) {
        alignas(16) uint8_t bufA[kMax * sizeof(ShaderPass)], bufB[kMax * sizeof(ShaderPass)];
        for (auto &b : bufA) b = uint8_t(rng());
        std::memcpy(bufB, bufA, sizeof(bufA));
        const uint32_t start = rng() % 2, count = rng() % (kMax - start + 1);
        cShader::PassVec vec{};
        auto *a = reinterpret_cast<ShaderPass *>(bufA) + start;
        auto *b = reinterpret_cast<ShaderPass *>(bufB) + start;
        ShaderPass *x = original(a, count, &vec);
        ShaderPass *y = cShader::PassVec::FillDefault(b, count, &vec);
        ++ctx.cases;
        filled += count != 0;
        if (x - a != y - b || std::memcmp(bufA, bufB, sizeof(bufA)) != 0)
            ctx.Fail("case %u (start %u count %u): end +%d / +%d, storage equal %d", i, start, count, int(x - a), int(y - b),
                     std::memcmp(bufA, bufB, sizeof(bufA)) == 0);
    }
    if (filled < 200) ctx.Fail("coverage: filled %d", filled);
}

// ---- 0xaa45e0 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassReallocate, 0xaa45e0)
{
    using Fn = void(__thiscall *)(cShader::PassVec *, uint32_t, void *);
    const Fn original = ctx.Original<Fn>(0xaa45e0);
    PlantManager plant(ctx);
    gLog.reserve(1 << 18);
    std::mt19937 rng(0xaa45e0);
    int fromNull = 0, largeOld = 0, largeNew = 0, handles = 0, grownByHalf = 0;
    for (uint32_t i = 0; i < 300; ++i) {
        const uint32_t kind = rng() % 6;
        uint32_t cap = kind == 0 ? 0 : kind == 5 ? 25 + rng() % 4 : 1 + rng() % 6;
        const uint32_t n = cap == 0 ? 0 : rng() % (cap + 1);
        // The size asked: past the capacity (what Build asks), sometimes just one more, sometimes far more.
        const uint32_t pick = rng() % 4;
        const uint32_t size = pick == 0 ? cap + 1 : pick == 1 ? cap + 1 + rng() % 4 : pick == 2 ? cap + rng() % 40 + 1 : n + rng() % 3;
        const uint32_t seed = rng();
        cShader::PassVec a, b;
        BuildVec(seed, n, cap, a);
        BuildVec(seed, n, cap, b);
        for (const ShaderPass *p = a.mBegin; p && p != a.mEnd; ++p)
            for (uint32_t h : p->mHandles) handles += h != 0xffffffffu;
        uint32_t scratch = 0;
        gLog.clear();
        difftest::AllocationCounter countA;
        original(&a, size, &scratch);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        b.Reallocate(size, &scratch);
        countB.Stop();
        ++ctx.cases;
        const std::string sa = SnapPassVec(a), sb = SnapPassVec(b);
        if (sa != sb || logA != gLog || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes())
            ctx.Fail("case %u (n %u cap %u size %u): sizes %d/%d / %d/%d, blocks %lld/%lld bytes %lld/%lld contents equal %d\n"
                     "original\n%sours\n%s",
                     i, n, cap, size, int(a.mEnd - a.mBegin), int(a.mCap - a.mBegin), int(b.mEnd - b.mBegin),
                     int(b.mCap - b.mBegin), countA.Blocks(), countB.Blocks(), countA.Bytes(), countB.Bytes(), sa == sb,
                     logA.c_str(), gLog.c_str());
        fromNull += cap == 0;
        largeOld += cap * sizeof(ShaderPass) >= 0x1000;
        largeNew += size_t(a.mCap - a.mBegin) * sizeof(ShaderPass) >= 0x1000;
        grownByHalf += uint32_t(a.mCap - a.mBegin) > size;
        gLog.clear();
        a.Destroy();
        b.Destroy();
    }
    if (fromNull < 20 || largeOld < 20 || largeNew < 40 || handles < 100 || grownByHalf < 50)
        ctx.Fail("coverage: from null %d large old %d large new %d handles %d grown past size %d", fromNull, largeOld, largeNew,
                 handles, grownByHalf);
}
