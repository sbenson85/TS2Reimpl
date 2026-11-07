// Differential tests for nGZSceneGraph::cShader, part 1 (lane RT, src/sg/cShader.*).
//
// The game keeps its vtable and constructor; these are the bodies hooked at their RVAs. Each test builds objects in
// raw memory (the layout the header asserts), runs the original body and ours over the same bytes (the object put back
// between the runs), and compares the answer, the object afterwards and the log of every call made on the fakes.
//   SG_ShaderPartVecDiffer 0xaa3d20     SG_ShaderPassEquals 0xaa6510      SG_ShaderPassReset 0xaa6b60
//   SG_ShaderPassEndState 0xaa6e50      SG_ShaderQueryInterface 0xaa6950  SG_ShaderResetPasses 0xaa6b30
//   SG_ShaderIsEqual 0xaa6470           SG_ShaderApply 0xaa69e0
// The fake parts (cIShaderPart) log every slot; the fake shaders log QueryInterface / AddRef; the apply functions log
// their five arguments. The texture handles of a pass go through the texture manager the image caches at 0x10f8f94: a
// fake planted there answers slot 23 (the lookup) with a fake part.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "rz/cRZStd.h"
#include "rz/cRZString.h"
#include "sg/cShader.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <vector>

using namespace nGZSceneGraph;

namespace {

std::string gLog;

void Note(const char *what, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %x %x %x %x\n", what, a, b, c, d);
    gLog += buf;
}

struct FakePart : cIShaderPart {
    uint32_t id = 0;
    void Slot0() override { Note("s0", id); }
    void AddRef() override { Note("s1", id); }
    void Release() override { Note("release", id); }
    void Reset() override { Note("reset", id); }
    void Slot4() override { Note("s4", id); }
    void Slot5() override { Note("s5", id); }
    void Slot6() override { Note("s6", id); }
    void Slot7() override { Note("s7", id); }
    ShaderFrame *MapFrame(ShaderFrame *, ShaderRenderCtx *) override { Note("s8", id); return nullptr; }
    // Slot 9 maps a key: the answer is the part's own key array (the scratch argument is left alone).
    uint32_t mapped[3] = {0, 0, 0};
    const uint32_t *Slot9(void *, const void *key) override
    {
        const uint32_t *k = static_cast<const uint32_t *>(key);
        Note("s9", id, k[0], k[1], k[2]);
        return mapped;
    }
    void Slot10() override { Note("s10", id); }
    void Apply(ShaderRenderCtx *, ShaderFrame *) override { Note("s11", id); }
    void Slot12(void *device, const void *key) override
    {
        const uint32_t *k = static_cast<const uint32_t *>(key);
        Note("s12", id, uint32_t(uintptr_t(device)), k[0], k[1] * 7 + k[2]);
    }
    void EndState(void *c) override { Note("endstate", id, uint32_t(uintptr_t(c))); }
};

// QueryInterface answers: 0 false, 1 true and *out = gQiTarget, 2 true and *out untouched.
int gQiMode = 0;
cShader *gQiTarget = nullptr;
bool gSlot10 = false;

struct FakeShader : cShader {
    uint32_t id = 0;
    FakeShader()
    {
        mRefCount = 0;
        mEnabled = 0;
        std::memset(mPad9, 0, sizeof(mPad9));
        std::memset(&mPasses, 0, sizeof(mPasses));
        std::memset(mRanges, 0, sizeof(mRanges));
        std::memset(&mInfo, 0, sizeof(mInfo));
        mHash = 0;
    }
    bool QueryInterface(uint32_t iid, void **out) override
    {
        Note("qi", id, iid, out ? 1 : 0);
        if (gQiMode == 0) return false;
        if (gQiMode == 1) *out = gQiTarget;
        return true;
    }
    uint32_t AddRef() override { Note("addref", id); return ++mRefCount; }
    uint32_t Release() override { return 0; }
    void ResetPasses() override {}
    void SetEnabled(bool) override {}
    bool GetEnabled() override { return false; }
    uint32_t GetHash() override { return 0; }
    bool IsEqual(cShader *) override { return false; }
    bool Build(void *, const ShaderInitView *, const uint32_t *, const ShaderKey *, bool) override { return false; }
    ShaderInfo *GetInfo() override { Note("getinfo", id); return &mInfo; }
    // Slot 10 as Build asks it (the answer is the test's; the arguments are logged: the same objects on both sides).
    bool Slot10(void *a, void *b, void *c, void *d) override
    {
        Note("slot10", uint32_t(uintptr_t(a)), uint32_t(uintptr_t(b)), uint32_t(uintptr_t(c)), uint32_t(uintptr_t(d)));
        return gSlot10;
    }
    void Apply(ShaderRenderCtx *, ShaderFrame *) override {}
    void Dump(void *, uint32_t) override {}
    void Delete(uint32_t) override {}
};

// Everything one pass or shader needs to live: the parts, the vectors' storage.
struct Rig {
    FakePart parts[4];
    std::deque<std::vector<cIShaderPart *>> vecs;
    Rig()
    {
        for (uint32_t i = 0; i < 4; ++i) parts[i].id = i + 1;
    }
    cIShaderPart *Pick(std::mt19937 &rng, bool allowNull)
    {
        const uint32_t k = rng() % (allowNull ? 5 : 4);
        return k == 4 ? nullptr : &parts[k];
    }
    void SetVec(ShaderPartVec &v, std::vector<cIShaderPart *> contents)
    {
        vecs.push_back(std::move(contents));
        auto &s = vecs.back();
        v.mBegin = s.data();
        v.mEnd = s.data() + s.size();
        v.mCap = v.mEnd;
    }
    void RandVec(std::mt19937 &rng, ShaderPartVec &v)
    {
        std::vector<cIShaderPart *> c;
        for (uint32_t n = rng() % 4; n > 0; --n) c.push_back(&parts[rng() % 4]);
        SetVec(v, std::move(c));
    }
    void RandPass(std::mt19937 &rng, ShaderPass &p, bool handles)
    {
        std::memset(&p, 0, sizeof(p));
        p.mB4 = rng() % 3;
        p.mB5 = rng() % 2;
        p.mB6 = rng() % 2;
        p.mB7 = rng() % 2;
        p.mB3c = rng() % 2;
        p.mB3d = rng() % 2;
        p.mB3e = rng() % 2;
        p.m40 = rng() % 3;
        p.m44 = rng();
        p.mP8 = Pick(rng, true);
        p.mPc = Pick(rng, true);
        p.mP10 = Pick(rng, true);
        p.mP20 = Pick(rng, true);
        p.mP48 = Pick(rng, true);
        p.mP4c = Pick(rng, true);
        p.mP50 = Pick(rng, true);
        p.mP54 = Pick(rng, true);
        RandVec(rng, p.mV14);
        RandVec(rng, p.mV24);
        RandVec(rng, p.mV30);
        RandVec(rng, p.mV58);
        for (uint32_t &h : p.mHandles) h = (handles && rng() % 3 == 0) ? 1 + rng() % 50 : 0xffffffffu;
    }
    // A pass equal to src in value, with vectors of its own.
    void CopyPass(const ShaderPass &src, ShaderPass &dst)
    {
        dst = src;
        auto copy = [&](const ShaderPartVec &from, ShaderPartVec &to) {
            SetVec(to, std::vector<cIShaderPart *>(from.mBegin, from.mEnd));
        };
        copy(src.mV14, dst.mV14);
        copy(src.mV24, dst.mV24);
        copy(src.mV30, dst.mV30);
        copy(src.mV58, dst.mV58);
    }
};

void SetPasses(FakeShader &s, std::vector<ShaderPass> &passes)
{
    s.mPasses.mBegin = passes.data();
    s.mPasses.mEnd = passes.data() + passes.size();
    s.mPasses.mCap = s.mPasses.mEnd;
    s.mInfo.mFirstPass = passes.data();
    s.mInfo.mCount = uint32_t(passes.size());
}

// ---- the texture manager the image caches -------------------------------------------------------------------------
FakePart gTexture;
void *__fastcall MgrGet(void *, void *, uint32_t id)
{
    Note("texture", id);
    return &gTexture;
}
void __fastcall MgrDestroy(void *, void *, uint32_t id) { Note("destroy", id); }
void __fastcall MgrCount(void *, void *, uint32_t id) { Note("count", id); } // slot 18: a handle copied is counted
void *gMgrVt[40];
void *gMgrObject[1] = {gMgrVt};

struct PlantManager {
    void **cache;
    void *saved;
    explicit PlantManager(difftest::Context &ctx)
    {
        gTexture.id = 99;
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

} // namespace

// ---- 0xaa3d20 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPartVecDiffer, 0xaa3d20)
{
    auto original = ctx.Original<bool(__cdecl *)(const ShaderPartVec *, const ShaderPartVec *)>(0xaa3d20);
    std::mt19937 rng(0xaa3d20);
    Rig rig;
    for (int i = 0; i < 600; ++i) {
        std::vector<cIShaderPart *> a, b;
        const uint32_t na = rng() % 4;
        for (uint32_t k = 0; k < na; ++k) a.push_back(&rig.parts[rng() % 4]);
        b = a;
        switch (i % 5) {
        case 1: if (!b.empty()) b[rng() % b.size()] = &rig.parts[rng() % 4]; break; // may or may not differ
        case 2: b.push_back(&rig.parts[0]); break;
        case 3: if (!b.empty()) b.pop_back(); break;
        case 4: b.clear(); for (uint32_t k = rng() % 4; k > 0; --k) b.push_back(&rig.parts[rng() % 4]); break;
        default: break;
        }
        ShaderPartVec va{}, vb{};
        rig.SetVec(va, a);
        rig.SetVec(vb, b);
        const bool x = original(&va, &vb);
        const bool y = ShaderPartVecDiffer(&va, &vb);
        const bool x2 = original(&vb, &va);
        const bool y2 = ShaderPartVecDiffer(&vb, &va);
        ++ctx.cases;
        if (x != y || x2 != y2) ctx.Fail("case %d (%zu vs %zu): original %d/%d ours %d/%d", i, a.size(), b.size(), x, x2, y, y2);
    }
}

// ---- 0xaa6510 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassEquals, 0xaa6510)
{
    auto original = ctx.Original<bool(__thiscall *)(const ShaderPass *, const ShaderPass *)>(0xaa6510);
    std::mt19937 rng(0xaa6510);
    Rig rig;
    for (int base = 0; base < 60; ++base) {
        ShaderPass a;
        rig.RandPass(rng, a, false);
        for (int k = -1; k < 24; ++k) {
            ShaderPass b;
            rig.CopyPass(a, b);
            switch (k) {
            case 0: b.mB4 ^= 1; break;
            case 1: b.mB5 ^= 1; break;
            case 2: b.mB6 ^= 1; break;
            case 3: b.mB7 ^= 1; break;
            case 4: b.mB3c ^= 1; break;
            case 5: b.mB3d ^= 1; break;
            case 6: b.mB3e ^= 1; break;
            case 7: b.mP8 = b.mP8 ? nullptr : &rig.parts[0]; break;
            case 8: b.mPc = b.mPc ? nullptr : &rig.parts[0]; break;
            case 9: b.mP10 = b.mP10 ? nullptr : &rig.parts[0]; break;
            case 10: b.mP20 = b.mP20 ? nullptr : &rig.parts[0]; break;
            case 11: rig.SetVec(b.mV14, std::vector<cIShaderPart *>(a.mV14.mBegin, a.mV14.mEnd)); if (a.mV14.mBegin != a.mV14.mEnd) *b.mV14.mBegin = *b.mV14.mBegin == &rig.parts[3] ? &rig.parts[2] : &rig.parts[3]; break;
            case 12: { std::vector<cIShaderPart *> v(a.mV24.mBegin, a.mV24.mEnd); v.push_back(&rig.parts[1]); rig.SetVec(b.mV24, v); break; }
            case 13: rig.SetVec(b.mV24, std::vector<cIShaderPart *>(a.mV24.mBegin, a.mV24.mEnd)); if (a.mV24.mBegin != a.mV24.mEnd) *b.mV24.mBegin = *b.mV24.mBegin == &rig.parts[3] ? &rig.parts[2] : &rig.parts[3]; break;
            case 14: { std::vector<cIShaderPart *> v(a.mV30.mBegin, a.mV30.mEnd); v.push_back(&rig.parts[1]); rig.SetVec(b.mV30, v); break; }
            case 15: rig.SetVec(b.mV30, std::vector<cIShaderPart *>(a.mV30.mBegin, a.mV30.mEnd)); if (a.mV30.mBegin != a.mV30.mEnd) *b.mV30.mBegin = *b.mV30.mBegin == &rig.parts[3] ? &rig.parts[2] : &rig.parts[3]; break;
            case 16: b.m40 ^= 1; break;
            // fields the comparison ignores
            case 17: b.m44 ^= 0x55; break;
            case 18: b.mP48 = b.mP48 ? nullptr : &rig.parts[0]; break;
            case 19: b.mP4c = b.mP4c ? nullptr : &rig.parts[0]; break;
            case 20: rig.SetVec(b.mV58, {&rig.parts[0], &rig.parts[1]}); break;
            case 21: b.mHandles[3] ^= 1; break;
            case 22: b.mApply = reinterpret_cast<ShaderPass::ApplyFn>(uintptr_t(0x1234)); break;
            case 23: b.mPad3f = 7; break;
            default: break;
            }
            const bool x = original(&a, &b);
            const bool y = a.Equals(&b);
            ++ctx.cases;
            if (x != y) ctx.Fail("base %d change %d: original %d ours %d", base, k, x, y);
        }
    }
}

// ---- 0xaa6b60 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassReset, 0xaa6b60)
{
    auto original = ctx.Original<void(__thiscall *)(ShaderPass *)>(0xaa6b60);
    PlantManager plant(ctx);
    std::mt19937 rng(0xaa6b60);
    Rig rig;
    for (int i = 0; i < 300; ++i) {
        ShaderPass p;
        rig.RandPass(rng, p, true);
        if (i % 7 == 0) std::fill(std::begin(p.mHandles), std::end(p.mHandles), 0xffffffffu);
        if (i % 11 == 0) std::memset(&p.mP8, 0, 0xc); // the first three pointers null
        ShaderPass saved = p;
        gLog.clear();
        original(&p);
        const std::string logA = gLog;
        ShaderPass after = p;
        p = saved;
        gLog.clear();
        p.Reset();
        const std::string logB = gLog;
        ++ctx.cases;
        if (logA != logB || std::memcmp(&after, &p, sizeof(p)) != 0)
            ctx.Fail("case %d: original log\n%sours\n%s(state equal: %d)", i, logA.c_str(), logB.c_str(),
                     std::memcmp(&after, &p, sizeof(p)) == 0);
    }
}

// ---- 0xaa6e50 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassEndState, 0xaa6e50)
{
    auto original = ctx.Original<void(__thiscall *)(ShaderPass *, void *)>(0xaa6e50);
    std::mt19937 rng(0xaa6e50);
    Rig rig;
    for (int i = 0; i < 80; ++i) {
        ShaderPass p;
        rig.RandPass(rng, p, false);
        void *arg = reinterpret_cast<void *>(uintptr_t(rng()));
        gLog.clear();
        original(&p, arg);
        const std::string logA = gLog;
        gLog.clear();
        p.EndState(arg);
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d: original\n%sours\n%s", i, logA.c_str(), gLog.c_str());
    }
}

// ---- 0xaa6950 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderQueryInterface, 0xaa6950)
{
    auto original = ctx.Original<bool(__thiscall *)(cShader *, uint32_t, void **)>(0xaa6950);
    const uint32_t iids[] = {0, 1, 2, 0x9c544810, 0xfc17e651, 0xcd2e1086, 0xcd2e1781, 0x5c17ff18, 0xcd2e1087, 0xdeadbeef};
    for (uint32_t iid : iids) {
        for (int nullOut = 0; nullOut < 2; ++nullOut) {
            FakeShader a, b;
            a.id = b.id = 7;
            void *outA = reinterpret_cast<void *>(0x12345678), *outB = outA;
            gLog.clear();
            const bool x = original(&a, iid, nullOut ? nullptr : &outA);
            const std::string logA = gLog;
            gLog.clear();
            const bool y = b.QueryInterfaceBody(iid, nullOut ? nullptr : &outB);
            ++ctx.cases;
            const bool sameOut = (outA == reinterpret_cast<void *>(0x12345678)) == (outB == reinterpret_cast<void *>(0x12345678)) &&
                                 (outA != &a || outB == &b);
            if (x != y || logA != gLog || a.mRefCount != b.mRefCount || !sameOut)
                ctx.Fail("iid %x null %d: original %d out %p log '%s' refs %u, ours %d out %p log '%s' refs %u", iid, nullOut, x, outA,
                         logA.c_str(), a.mRefCount, y, outB, gLog.c_str(), b.mRefCount);
        }
    }
}

// ---- 0xaa6b30 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderResetPasses, 0xaa6b30)
{
    auto original = ctx.Original<void(__thiscall *)(cShader *)>(0xaa6b30);
    PlantManager plant(ctx);
    std::mt19937 rng(0xaa6b30);
    for (int i = 0; i < 60; ++i) {
        Rig rig;
        FakeShader s;
        std::vector<ShaderPass> passes(i % 5);
        for (ShaderPass &p : passes) rig.RandPass(rng, p, true);
        SetPasses(s, passes);
        const std::vector<ShaderPass> saved = passes;
        gLog.clear();
        original(&s);
        const std::string logA = gLog;
        const std::vector<ShaderPass> after = passes;
        passes = saved;
        gLog.clear();
        s.ResetPassesBody();
        ++ctx.cases;
        bool same = after.size() == passes.size();
        for (size_t k = 0; same && k < after.size(); ++k) same = std::memcmp(&after[k], &passes[k], sizeof(ShaderPass)) == 0;
        if (logA != gLog || !same) ctx.Fail("%zu passes: original\n%sours\n%s", passes.size(), logA.c_str(), gLog.c_str());
    }
}

// ---- 0xaa6470 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderIsEqual, 0xaa6470)
{
    auto original = ctx.Original<bool(__thiscall *)(cShader *, cShader *)>(0xaa6470);
    std::mt19937 rng(0xaa6470);
    for (int base = 0; base < 30; ++base) {
        Rig rig;
        FakeShader a, b;
        a.id = 1;
        b.id = 2;
        std::vector<ShaderPass> pa(base % 4), pb;
        pb.resize(pa.size());
        for (size_t k = 0; k < pa.size(); ++k) {
            rig.RandPass(rng, pa[k], false);
            rig.CopyPass(pa[k], pb[k]);
        }
        for (uint32_t &r : a.mRanges) r = rng() % 3 == 0 ? 0xffffffffu : rng() % 5;
        std::memcpy(b.mRanges, a.mRanges, sizeof(b.mRanges));
        // variations: none; each range dword; the count (one more / one less); each pass's byte 4; the QI answers
        for (int k = -1; k < 31; ++k) {
            std::vector<ShaderPass> qa = pa, qb = pb;
            std::memcpy(b.mRanges, a.mRanges, sizeof(b.mRanges));
            if (k >= 0 && k < 26) b.mRanges[k] ^= 1;
            if (k == 26) { qb.push_back(qb.empty() ? ShaderPass{} : qb.back()); }
            if (k == 27 && !qb.empty()) qb.pop_back();
            if (k >= 28 && k < 31 && size_t(k - 28) < qb.size()) qb[k - 28].mB4 ^= 1;
            SetPasses(a, qa);
            SetPasses(b, qb);
            FakeShader a2; // a third shader with a's contents: the answer of mode 3
            a2.id = 3;
            std::memcpy(a2.mRanges, a.mRanges, sizeof(a2.mRanges));
            SetPasses(a2, qa);
            // QueryInterface of the other: 0 false, 1 stores b, 2 true and untouched, 3 stores a2 (equal to a)
            for (int mode = 0; mode < 4; ++mode) {
                gQiMode = mode == 3 ? 1 : mode;
                gQiTarget = mode == 3 ? &a2 : &b;
                gLog.clear();
                const bool x = original(&a, &b);
                const std::string logA = gLog;
                gLog.clear();
                const bool y = a.IsEqualBody(&b);
                ++ctx.cases;
                if (x != y || logA != gLog)
                    ctx.Fail("base %d variation %d mode %d: original %d (%s), ours %d (%s)", base, k, mode, x, logA.c_str(), y,
                             gLog.c_str());
            }
            // and against itself
            gQiMode = 1;
            gQiTarget = &a;
            gLog.clear();
            const bool x = original(&a, &a);
            const std::string logA = gLog;
            gLog.clear();
            const bool y = a.IsEqualBody(&a);
            ++ctx.cases;
            if (x != y || logA != gLog) ctx.Fail("self: original %d ours %d", x, y);
        }
    }
    gQiMode = 0;
}

// ---- 0xaa69e0 -----------------------------------------------------------------------------------------------------------
namespace {
ShaderPass *gFirstPass = nullptr;
void __fastcall ApplyPass(ShaderPass *self, void *, ShaderRenderCtx *rc, ShaderFrame *frame, uint32_t *out, uint32_t flag)
{
    Note("apply", uint32_t(self - gFirstPass), uint32_t(uintptr_t(rc)) ^ uint32_t(uintptr_t(frame)), *out, flag);
    *out += 0x10 + uint32_t(self - gFirstPass);
}
} // namespace

DIFF_TEST(SG_ShaderApply, 0xaa69e0)
{
    auto original = ctx.Original<void(__thiscall *)(cShader *, ShaderRenderCtx *, ShaderFrame *)>(0xaa69e0);
    std::vector<uint8_t> rcMem(0x2c0);
    ShaderRenderCtx *rc = reinterpret_cast<ShaderRenderCtx *>(rcMem.data());
    std::vector<uint8_t> frameMem(0x40);
    ShaderFrame *frame = reinterpret_cast<ShaderFrame *>(frameMem.data());
    uint32_t pool = 0x77;
    std::mt19937 rng(0xaa69e0);
    int sameCases = 0;
    for (int iter = 0; iter < 2000; ++iter) {
        Rig rig;
        FakeShader cur, prev, *mode;
        cur.id = 1;
        prev.id = 2;
        const uint32_t bits = rng();
        const int count = 1 + (bits >> 8) % 3;
        std::vector<ShaderPass> pc(count), pp((bits >> 10) % 3);
        for (ShaderPass &p : pc) {
            rig.RandPass(rng, p, false);
            p.mApply = reinterpret_cast<ShaderPass::ApplyFn>(&ApplyPass);
        }
        for (ShaderPass &p : pp) rig.RandPass(rng, p, false);
        SetPasses(cur, pc);
        SetPasses(prev, pp);
        prev.mInfo.m90 = (bits >> 12) & 1;
        cur.mInfo.m90 = (bits >> 13) & 1;
        gFirstPass = pc.data();
        const int shape = (bits >> 14) % 4; // 0 and 3 the same shader, 1 another shader, 2 none
        mode = shape == 1 ? &prev : shape == 2 ? nullptr : &cur;
        for (auto &byte : rcMem) byte = uint8_t(rng());
        rc->mShader = mode;
        std::memset(frameMem.data(), 0, frameMem.size());
        pool = 0x77 + (bits & 1);
        frame->m0c = 0x40 + ((bits >> 1) & 1);
        frame->m2c = &pool;
        rc->m294 = 0x40 + (rng() & 1);
        rc->m290 = 0x77 + (rng() & 1);
        if (shape == 0 || shape == 3) { // mostly equal, so that the reuse answer is reached and each test fails in turn
            if (bits & (1u << 20)) rc->m294 = frame->m0c;
            if (bits & (1u << 21)) rc->m290 = pool;
        }
        frame->m16 = ((bits >> 4) & 3) == 0 ? 8 : ((bits >> 24) & 1) ? 0xf7 : 0;
        frame->m38 = ((bits >> 6) & 3) == 0 ? 2 : (bits >> 22) & 1 ? 5 : 0;
        std::vector<uint8_t> saved = rcMem;
        gLog.clear();
        original(&cur, rc, frame);
        const std::string logA = gLog;
        const std::vector<uint8_t> after = rcMem;
        rcMem = saved;
        gLog.clear();
        cur.ApplyBody(rc, frame);
        ++ctx.cases;
        if (mode == &cur && rc->mSame) ++sameCases;
        if (logA != gLog || after != rcMem) ctx.Fail("iter %d shape %d count %d: original\n%sours\n%s(state equal %d)", iter, shape, count, logA.c_str(), gLog.c_str(), after == rcMem);
    }
    if (sameCases < 20) ctx.Fail("only %d cases reached the reuse answer", sameCases);
}

// ---- 0xaa6c80 and 0xaa6e80 (lane RV) --------------------------------------------------------------------------------------
// The device, the render context and the part lists are other classes: fakes with the same slot layout (padding virtuals
// up to the slot used), each logging its arguments. cRZStd's report slot (0x1bfb92) is a logging stand-in on both sides.
namespace {

template <int N> struct Pads : Pads<N - 1> {
    virtual void pad(Pads<N> *) {}
};
template <> struct Pads<0> {};
template <int N, class B> struct PadsAfter : PadsAfter<N - 1, B> {
    virtual void pad(PadsAfter<N, B> *) {}
};
template <class B> struct PadsAfter<0, B> : B {};

uint32_t Id(const void *p) { return uint32_t(uintptr_t(p)); }

bool gDev27 = true, gDev28 = true;
struct FakeDevA : Pads<9> {
    virtual void S9(uint32_t v) { Note("d9", v); }
    virtual void S10() {}
    virtual void S11(void *p) { Note("d11", Id(p)); }
    virtual void S12() {}
    virtual void S13() {}
    virtual void S14(void *p) { Note("d14", Id(p)); }
    virtual void S15() {}
    virtual void S16(void *p) { Note("d16", Id(p)); }
    virtual uint32_t S17(uint32_t i, uint32_t t, uint32_t o, uint32_t z) { Note("d17", i, t, o, z); return 0; }
    virtual void S18(uint32_t v) { Note("d18", v); }
};
struct FakeDevB : PadsAfter<8, FakeDevA> {
    virtual bool S27(void *p, uint32_t report) { Note("d27", Id(p), report); return gDev27; }
    virtual bool S28(uint32_t report) { Note("d28", report); return gDev28; }
};

struct FakeCtxA : Pads<20> {
    void *dev = nullptr;
    virtual void *S20() { Note("c20"); return dev; }
};
bool gCtx35 = true;
struct FakeCtx : PadsAfter<14, FakeCtxA> {
    virtual bool S35() { Note("c35"); return gCtx35; }
};

struct FakeList : Pads<4> {
    uint32_t count = 0;
    virtual uint32_t Count() { Note("list-count", count); return count; }
};
struct FakeSet : Pads<11> {
    uint32_t id = 0;
    FakeList *list = nullptr;
    virtual uint32_t Handle(uint32_t i) { Note("set-handle", id, i); return id * 100 + i; }
    virtual FakeList *List() { Note("set-list", id); return list; }
};

int32_t __cdecl StandReport(cRZStd *self, const char *group, int32_t level, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // A format with no conversion has no argument to show (reading one would show whatever the stack held).
    const uint32_t pass = std::strchr(format, '%') ? va_arg(args, uint32_t) : 0;
    va_end(args);
    gLog += "report ";
    gLog += self->File();
    gLog += " ";
    gLog += group;
    char buf[64];
    std::snprintf(buf, sizeof(buf), " %u %d %x %x\n", self->mSecond, level, self->mFlags, pass);
    gLog += buf;
    gLog += format;
    return 0;
}

struct ValidateRig {
    Rig rig;
    FakeDevB dev;
    FakeSet sets[3];
    FakeList lists[3];
    uint32_t table[0x22];
    ShaderKey key;
    explicit ValidateRig(std::mt19937 &rng)
    {
        for (uint32_t i = 0; i < 4; ++i)
            for (uint32_t &m : rig.parts[i].mapped) m = rng();
        for (uint32_t i = 0; i < 3; ++i) {
            sets[i].id = i + 1;
            lists[i].count = rng() % 4;
            sets[i].list = &lists[i];
        }
        for (uint32_t &t : table) t = rng();
        for (uint32_t &w : key.w) w = rng();
    }
    // A pass drawn at random whose set members are the fake sets.
    void Pass(std::mt19937 &rng, ShaderPass &p)
    {
        rig.RandPass(rng, p, false);
        auto set = [&]() { return rng() % 5 == 0 ? nullptr : reinterpret_cast<cIShaderPart *>(&sets[rng() % 3]); };
        p.mP10 = set();
        p.mP48 = set();
        p.mP4c = set();
        if (!p.mP4c) p.mB5 = 0;
        if (!p.mP10 || !p.mP48) p.mPc = nullptr; // that block needs both sets
    }
};

} // namespace

DIFF_TEST(SG_ShaderPassValidate, 0xaa6e80)
{
    auto original = ctx.Original<bool(__thiscall *)(ShaderPass *, void *, uint32_t, const uint32_t *, const ShaderKey *, uint32_t)>(0xaa6e80);
    shape::CodePatch reportPatch(0x1bfb92, reinterpret_cast<const void *>(&StandReport));
    std::mt19937 rng(0xaa6e80);
    int reached[4] = {};
    for (int iter = 0; iter < 3000; ++iter) {
        ValidateRig v(rng);
        ShaderPass p;
        v.Pass(rng, p);
        if (iter % 3 == 0 && p.mP10 && p.mP48 && !p.mPc) p.mPc = &v.rig.parts[0];
        gDev27 = rng() % 3 != 0;
        gDev28 = rng() % 3 != 0;
        const uint32_t report = rng() % 2;
        const uint32_t index = rng() % 5;
        gLog.clear();
        const bool x = original(&p, &v.dev, index, v.table, &v.key, report);
        const std::string logA = gLog;
        gLog.clear();
        const bool y = p.Validate(&v.dev, index, v.table, &v.key, report != 0);
        ++ctx.cases;
        ++reached[(x ? 1 : 0) + (p.mPc ? 2 : 0)];
        if (x != y || logA != gLog) ctx.Fail("iter %d: original %d, ours %d\n%s---\n%s", iter, x, y, logA.c_str(), gLog.c_str());
    }
    for (int r : reached)
        if (r < 100) ctx.Fail("coverage: a (result, preprocess block) combination was reached only %d times", r);
}

DIFF_TEST(SG_ShaderValidate, 0xaa6c80)
{
    auto original = ctx.Original<bool(__thiscall *)(cShader *, const ShaderKey *, const ShaderKey *, const uint32_t *, void *)>(0xaa6c80);
    shape::CodePatch reportPatch(0x1bfb92, reinterpret_cast<const void *>(&StandReport));
    std::mt19937 rng(0xaa6c80);
    int bySteps[4] = {};
    for (int iter = 0; iter < 6000; ++iter) {
        ValidateRig v(rng);
        FakeShader s;
        for (uint32_t i = 0; i < 26; i += 2) {
            const uint32_t pick = rng() % 8;
            if (pick == 0) s.mRanges[i] = s.mRanges[i + 1] = 0xffffffffu;
            else if (pick == 1) { s.mRanges[i] = rng() % 3; s.mRanges[i + 1] = s.mRanges[i] + rng() % 2; }
            else { s.mRanges[i] = 0; s.mRanges[i + 1] = 1; }
        }
        // keys whose fields mostly step up by 0..3, so that the tests deep in the loop are reached
        auto fill = [&](ShaderKey &k) {
            uint32_t n = rng() % 4;
            k.w[0] = k.w[1] = 0;
            for (uint32_t i = 0; i < 13; ++i) {
                k.w[i >> 3] |= (n & 0xf) << ((i & 7) * 4);
                n += rng() % 2;
            }
            k.w[2] = rng() % 4;
        };
        ShaderKey k1, k2;
        fill(k2);
        k1 = k2;
        switch (rng() % 8) {
        case 1: fill(k1); k1.w[2] = k2.w[2]; break;
        case 2: k1.w[2] ^= 1; break;
        case 3: k1.w[0] ^= 1u << (4 * (rng() % 8)); k1.w[1] ^= 1u << (4 * (rng() % 5)); break;
        default: break;
        }
        const uint32_t count = rng() % 4;
        std::vector<ShaderPass> passes(count);
        for (ShaderPass &p : passes) v.Pass(rng, p);
        SetPasses(s, passes);
        gDev27 = rng() % 4 != 0;
        gDev28 = rng() % 4 != 0;
        gCtx35 = rng() % 4 != 0;
        FakeCtx fc;
        fc.dev = &v.dev;
        void *cx = rng() % 6 == 0 ? nullptr : &fc;
        const uint32_t *tbl = rng() % 6 == 0 ? nullptr : v.table;
        gLog.clear();
        const bool x = original(&s, &k1, &k2, tbl, cx);
        const std::string logA = gLog;
        gLog.clear();
        const bool y = s.ValidateBody(&k1, &k2, tbl, cx);
        ++ctx.cases;
        ++bySteps[(x ? 1 : 0) + (logA.empty() ? 0 : 2)];
        if (x != y || logA != gLog) ctx.Fail("iter %d: original %d, ours %d\n%s---\n%s", iter, x, y, logA.c_str(), gLog.c_str());
    }
    for (int r : bySteps)
        if (r < 100) ctx.Fail("coverage: a (result, device used) combination was reached only %d times", r);
}

// ---- lane RV: the destruction path --------------------------------------------------------------------------------------
namespace {

// A part vector whose storage the code under test frees: small blocks from operator new, or a block of 4 KB and more
// laid out the way the game's 32-byte aligned allocation is (the real pointer just before the block).
void HeapVec(ShaderPartVec &v, std::mt19937 &rng, Rig &rig)
{
    const uint32_t k = rng() % 6;
    if (k == 0) {
        v = ShaderPartVec{};
        return;
    }
    const uint32_t n = rng() % 4;
    const bool large = k == 5;
    const uint32_t cap = large ? 1024 + (rng() % 3) * 4 : n + 1 + rng() % 3;
    cIShaderPart **first;
    if (large) {
        uint8_t *raw = static_cast<uint8_t *>(::operator new(cap * 4 + 0x23));
        first = reinterpret_cast<cIShaderPart **>((reinterpret_cast<uintptr_t>(raw) + 0x23) & ~uintptr_t(0x1f));
        reinterpret_cast<void **>(first)[-1] = raw;
    } else {
        first = static_cast<cIShaderPart **>(::operator new(cap * 4));
    }
    for (uint32_t i = 0; i < n; ++i) first[i] = &rig.parts[rng() % 4];
    v.mBegin = first;
    v.mEnd = first + n;
    v.mCap = first + cap;
}

void BuildHeapPass(uint32_t seed, ShaderPass &p, Rig &rig)
{
    std::mt19937 rng(seed);
    rig.RandPass(rng, p, true);
    p.mApply = nullptr;
    HeapVec(p.mV14, rng, rig);
    HeapVec(p.mV24, rng, rig);
    HeapVec(p.mV30, rng, rig);
    HeapVec(p.mV58, rng, rig);
    if (seed % 5 == 0) std::fill(std::begin(p.mHandles), std::end(p.mHandles), 0xffffffffu);
    if (seed % 7 == 0) std::memset(&p.mP8, 0, 0xc);
    if (seed % 11 == 0) std::memset(&p.mP48, 0, 0x10);
}

// n passes (0 gives a null vector) in a block that is large when 25 or more fit.
void BuildPassVec(uint32_t seed, cShader::PassVec &v, Rig &rig)
{
    std::mt19937 rng(seed);
    const uint32_t k = rng() % 8;
    if (k == 0) {
        v = cShader::PassVec{};
        return;
    }
    const bool large = k == 7;
    const uint32_t n = large ? 1 + rng() % 3 : rng() % 4;
    const uint32_t cap = large ? 25 + rng() % 2 : n + 1 + rng() % 2;
    ShaderPass *first;
    if (large) {
        uint8_t *raw = static_cast<uint8_t *>(::operator new(cap * sizeof(ShaderPass) + 0x23));
        first = reinterpret_cast<ShaderPass *>((reinterpret_cast<uintptr_t>(raw) + 0x23) & ~uintptr_t(0x1f));
        reinterpret_cast<void **>(first)[-1] = raw;
    } else {
        first = static_cast<ShaderPass *>(::operator new(cap * sizeof(ShaderPass)));
    }
    for (uint32_t i = 0; i < n; ++i) BuildHeapPass(seed * 31 + i + 1, first[i], rig);
    v.mBegin = first;
    v.mEnd = first + n;
    v.mCap = first + cap;
}

// The shader pool's current page: 0x4000-aligned, header +8 the free list, +0xc the used count, far from empty and from
// full so neither side reaches the page bookkeeping (game code not under test).
struct ShaderPool {
    static constexpr uint32_t kPage = 0x4000, kBlock = 0x98;
    uint8_t *page;
    uint32_t saved;
    ShaderPool()
    {
        page = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
        saved = *GameAddress<uint32_t>(0x10f6658);
        *GameAddress<uint32_t>(0x10f6658) = uint32_t(reinterpret_cast<uintptr_t>(page));
    }
    ~ShaderPool()
    {
        *GameAddress<uint32_t>(0x10f6658) = saved;
        _aligned_free(page);
    }
    void Reset()
    {
        std::memset(page, 0, kPage);
        uint32_t *header = reinterpret_cast<uint32_t *>(page);
        header[3] = 1000;
        uint32_t next = 0;
        for (uint32_t at = 0x10 + ((kPage - 0x10) / kBlock - 1) * kBlock;; at -= kBlock) {
            *reinterpret_cast<uint32_t *>(page + at) = next;
            next = uint32_t(reinterpret_cast<uintptr_t>(page + at));
            if (at == 0x10) break;
        }
        header[2] = next;
    }
    // Take the head block by hand (what an allocation does).
    uint8_t *Take()
    {
        uint32_t *header = reinterpret_cast<uint32_t *>(page);
        uint8_t *block = reinterpret_cast<uint8_t *>(uintptr_t(header[2]));
        header[2] = *reinterpret_cast<uint32_t *>(block);
        ++header[3];
        return block;
    }
    std::string Bytes() const { return std::string(reinterpret_cast<const char *>(page), kPage); }
};

} // namespace

// ---- 0xaa4ea0 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassDestruct, 0xaa4ea0)
{
    auto original = ctx.Original<ShaderPass *(__thiscall *)(ShaderPass *, uint32_t)>(0xaa4ea0);
    PlantManager plant(ctx); gLog.reserve(1 << 16);
    Rig rig;
    int destroyed = 0, large = 0, freed = 0;
    for (uint32_t i = 0; i < 300; ++i) {
        const uint32_t flags = (i % 3 == 0) ? 1 : (i % 3 == 1 ? 0 : 0xfffffffe);
        ShaderPass stackA, stackB;
        ShaderPass *a = &stackA, *b = &stackB;
        if (flags & 1) {
            a = static_cast<ShaderPass *>(::operator new(sizeof(ShaderPass)));
            b = static_cast<ShaderPass *>(::operator new(sizeof(ShaderPass)));
        }
        BuildHeapPass(i + 1, *a, rig);
        BuildHeapPass(i + 1, *b, rig);
        for (auto *v : {&a->mV14, &a->mV24, &a->mV30, &a->mV58})
            if (v->mBegin && v->mCap - v->mBegin >= 1024) ++large;
        gLog.clear();
        difftest::AllocationCounter countA;
        ShaderPass *x = original(a, flags);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        ShaderPass *y = b->Destruct(flags);
        countB.Stop();
        ++ctx.cases;
        if (logA.find("destroy") != std::string::npos) ++destroyed;
        if (countA.Blocks() < 0) ++freed;
        bool same = x == a && y == b && logA == gLog && countA.Blocks() == countB.Blocks() && countA.Bytes() == countB.Bytes();
        if (!(flags & 1)) same = same && std::memcmp(a, b, sizeof(ShaderPass)) == 0;
        if (!same)
            ctx.Fail("case %u flags %x: blocks %lld/%lld bytes %lld/%lld\noriginal\n%sours\n%s", i, flags, countA.Blocks(),
                     countB.Blocks(), countA.Bytes(), countB.Bytes(), logA.c_str(), gLog.c_str());
    }
    if (destroyed < 20 || large < 10 || freed < 100) ctx.Fail("coverage: handles %d large %d freed %d", destroyed, large, freed);
}

// ---- 0xaa4ce0 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassVecDestroy, 0xaa4ce0)
{
    auto original = ctx.Original<void(__thiscall *)(cShader::PassVec *)>(0xaa4ce0);
    PlantManager plant(ctx); gLog.reserve(1 << 16);
    Rig rig;
    int nonEmpty = 0, large = 0;
    for (uint32_t i = 0; i < 160; ++i) {
        cShader::PassVec a, b;
        BuildPassVec(i + 1, a, rig);
        BuildPassVec(i + 1, b, rig);
        if (a.mBegin) ++nonEmpty;
        if (a.mBegin && a.mCap - a.mBegin >= 25) ++large;
        gLog.clear();
        difftest::AllocationCounter countA;
        original(&a);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        b.Destroy();
        countB.Stop();
        ++ctx.cases;
        if (logA != gLog || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes() ||
            std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("case %u: blocks %lld/%lld bytes %lld/%lld\noriginal\n%sours\n%s", i, countA.Blocks(), countB.Blocks(),
                     countA.Bytes(), countB.Bytes(), logA.c_str(), gLog.c_str());
    }
    if (nonEmpty < 100 || large < 10) ctx.Fail("coverage: non-empty %d large %d", nonEmpty, large);
}

// ---- 0xaa4d70 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderDestructBody, 0xaa4d70)
{
    auto original = ctx.Original<void(__thiscall *)(cShader *)>(0xaa4d70);
    PlantManager plant(ctx); gLog.reserve(1 << 16);
    Rig rig;
    for (uint32_t i = 0; i < 60; ++i) {
        alignas(16) uint8_t bufA[sizeof(cShader)], bufB[sizeof(cShader)];
        std::memset(bufA, 0x5a, sizeof(bufA));
        std::memset(bufB, 0x5a, sizeof(bufB));
        *reinterpret_cast<void **>(bufA) = GameAddress<void>(0xe758dc);
        *reinterpret_cast<const void **>(bufB) = static_cast<const void *>(cShader::kOwnVTable);
        auto *a = reinterpret_cast<cShader *>(bufA);
        auto *b = reinterpret_cast<cShader *>(bufB);
        BuildPassVec(i + 1, a->mPasses, rig);
        BuildPassVec(i + 1, b->mPasses, rig);
        gLog.clear();
        difftest::AllocationCounter countA;
        original(a);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        b->DestructBody();
        countB.Stop();
        ++ctx.cases;
        // After the body the object holds the base table: the image's 0xe758a0 and ours.
        if (*reinterpret_cast<void **>(bufA) != GameAddress<void>(0xe758a0) ||
            *reinterpret_cast<const void **>(bufB) != static_cast<const void *>(cShader::kOwnBaseVTable))
            ctx.Fail("case %u: base table word is not the expected one", i);
        *reinterpret_cast<void **>(bufA) = *reinterpret_cast<void **>(bufB) = nullptr;
        if (logA != gLog || countA.Blocks() != countB.Blocks() || std::memcmp(bufA, bufB, sizeof(bufA)) != 0)
            ctx.Fail("case %u: blocks %lld/%lld, bytes equal %d\noriginal\n%sours\n%s", i, countA.Blocks(), countB.Blocks(),
                     std::memcmp(bufA, bufB, sizeof(bufA)) == 0, logA.c_str(), gLog.c_str());
    }
}

// ---- 0xaa4e60 -----------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderDelete, 0xaa4e60)
{
    auto original = ctx.Original<cShader *(__thiscall *)(cShader *, uint32_t)>(0xaa4e60);
    PlantManager plant(ctx);
    ShaderPool pool;
    gLog.reserve(1 << 16); // so growing the log never counts as a block
    Rig rig;
    const uint32_t flagSets[] = {0, 1, 4, 5, 2, 3, 0xfffffff1, 0xfffffff4};
    int pooled = 0;
    for (uint32_t i = 0; i < 80; ++i) {
        const uint32_t flags = flagSets[i % 8];
        std::string pageA, pageB, memA, memB;
        long long blocksA = 0, blocksB = 0;
        std::string logA;
        cShader *retA = nullptr, *retB = nullptr;
        for (int side = 0; side < 2; ++side) {
            pool.Reset();
            alignas(16) uint8_t stack[sizeof(cShader)];
            uint8_t *mem = stack;
            const bool inPool = (flags & 1) && !(flags & 4);
            if (inPool) mem = pool.Take();
            std::memset(mem, 0x5a, sizeof(cShader));
            *reinterpret_cast<const void **>(mem) = side ? static_cast<const void *>(cShader::kOwnVTable) : GameAddress<void>(0xe758dc);
            auto *s = reinterpret_cast<cShader *>(mem);
            BuildPassVec(i + 1, s->mPasses, rig);
            gLog.clear();
            difftest::AllocationCounter count;
            cShader *r = side ? s->DeleteBody(flags) : original(s, flags);
            count.Stop();
            (side ? retB : retA) = r == s ? reinterpret_cast<cShader *>(1) : nullptr;
            (side ? blocksB : blocksA) = count.Blocks();
            (side ? pageB : pageA) = pool.Bytes();
            (side ? memB : memA).assign(reinterpret_cast<const char *>(mem), inPool ? 0 : sizeof(cShader));
            if (!inPool) { // the base table word: the image's on one side, ours on the other
                void *const expect = side ? static_cast<void *>(const_cast<void *>(static_cast<const void *>(cShader::kOwnBaseVTable)))
                                          : GameAddress<void>(0xe758a0);
                if (*reinterpret_cast<void **>(mem) != expect) ctx.Fail("case %u: base table word", i);
                (side ? memB : memA).replace(0, 4, 4, '\0');
            }
            if (side == 0) logA = gLog;
            if (inPool && side == 0) ++pooled;
        }
        ++ctx.cases;
        if (retA != retB || retA == nullptr || logA != gLog || blocksA != blocksB || pageA != pageB || memA != memB)
            ctx.Fail("case %u flags %x: blocks %lld/%lld page equal %d mem equal %d\noriginal\n%sours\n%s", i, flags, blocksA,
                     blocksB, pageA == pageB, memA == memB, logA.c_str(), gLog.c_str());
    }
    if (pooled < 10) ctx.Fail("coverage: pooled %d", pooled);
}

// ---- 0xaa4d90 and 0xaa4dc0 ----------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPoolNew, 0xaa4d90)
{
    auto original = ctx.Original<void *(__cdecl *)(size_t)>(0xaa4d90);
    ShaderPool pool;
    for (uint32_t i = 1; i < 8; ++i) {
        pool.Reset();
        std::string pageA, pageB;
        void *a[4], *b[4];
        for (int k = 0; k < 4; ++k) a[k] = original(0x98 + k);
        pageA = pool.Bytes();
        pool.Reset();
        for (int k = 0; k < 4; ++k) b[k] = cShader::PoolNew(0x98 + k);
        pageB = pool.Bytes();
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0 || pageA != pageB) ctx.Fail("round %u: pointers equal %d page equal %d", i, std::memcmp(a, b, sizeof(a)) == 0, pageA == pageB);
    }
}

DIFF_TEST(SG_ShaderPoolDelete, 0xaa4dc0)
{
    auto original = ctx.Original<void(__cdecl *)(void *)>(0xaa4dc0);
    ShaderPool pool;
    for (uint32_t i = 0; i < 12; ++i) {
        std::string pageA, pageB;
        for (int side = 0; side < 2; ++side) {
            pool.Reset();
            void *blocks[3];
            for (void *&blk : blocks) blk = pool.Take();
            for (uint32_t k = 0; k < 3; ++k) {
                void *target = (i % 4 == 3 && k == 1) ? nullptr : blocks[(k + i) % 3];
                if (side) cShader::PoolDelete(target); else original(target);
            }
            (side ? pageB : pageA) = pool.Bytes();
        }
        ++ctx.cases;
        if (pageA != pageB) ctx.Fail("round %u: pages differ", i);
    }
}

// ---- 0xaa65e0 -----------------------------------------------------------------------------------------------------------
namespace {

struct FakeStreamA : Pads<8> {
    virtual void Uint(uint32_t v) { Note("uint", v); }
    virtual void Int(int32_t v) { Note("int", uint32_t(v)); }
};
struct FakeStreamB : PadsAfter<4, FakeStreamA> {
    virtual void Text(const char *s)
    {
        gLog += "text:";
        gLog += s;
        gLog += '\n';
    }
    // The string object the manager filled: its first 0x20 bytes (the table word is the game's on both sides).
    virtual void Str(const uint8_t *s)
    {
        char buf[20];
        gLog += "string:";
        const bool heap = *reinterpret_cast<const uint32_t *>(s + 0x18) > 0xf;
        if (heap) { // the buffer pointer differs between runs: show its text instead
            gLog += *reinterpret_cast<const char *const *>(s + 4);
            std::snprintf(buf, sizeof(buf), " %x %x %x\n", *reinterpret_cast<const uint32_t *>(s + 0x14),
                          *reinterpret_cast<const uint32_t *>(s + 0x18), *reinterpret_cast<const uint32_t *>(s + 0x1c));
            gLog += buf;
            return;
        }
        for (int i = 0; i < 0x20; ++i) {
            std::snprintf(buf, sizeof(buf), "%02x", s[i]);
            gLog += buf;
        }
        gLog += '\n';
    }
};
struct FakeMgr : Pads<11> {
    virtual void Disassembly(uint8_t *out, uint32_t handle)
    {
        Note("manager", handle);
        // Odd handles give a text too long for the string's inline buffer, so it owns heap that the dump must free.
        if (handle & 1) reinterpret_cast<cRZString *>(out)->Construct("a disassembly line that does not fit inline");
        else out[0x10] = uint8_t(handle); // the string's length word: shows the manager got the object it was handed
    }
};

} // namespace

DIFF_TEST(SG_ShaderDump, 0xaa65e0)
{
    auto original = ctx.Original<void(__thiscall *)(cShader *, void *, uint32_t)>(0xaa65e0);
    gLog.reserve(1 << 18);
    FakeMgr mgr;
    void *saved = *ctx.image.At<void *>(0x10caa3c);
    *ctx.image.At<void *>(0x10caa3c) = &mgr;
    std::mt19937 rng(0xaa65e0);
    int blocks = 0, big = 0;
    for (uint32_t i = 0; i < 200; ++i) {
        const uint32_t indent = i % 17 == 0 ? 4100 : (i % 5 == 0 ? 0 : rng() % 40);
        const uint32_t n = rng() % 4;
        std::vector<ShaderPass> passes(n + 1);
        for (ShaderPass &p : passes) {
            std::memset(&p, 0, sizeof(p));
            if (rng() % 2) p.mPc = reinterpret_cast<cIShaderPart *>(uintptr_t(1 + rng() % 100));
            if (rng() % 2) p.mP50 = reinterpret_cast<cIShaderPart *>(uintptr_t(1 + rng() % 100));
            if (rng() % 2) p.mP54 = reinterpret_cast<cIShaderPart *>(uintptr_t(1 + rng() % 100));
        }
        FakeShader s;
        s.mInfo.mFirstPass = passes.data();
        s.mInfo.m84 = rng();
        s.mInfo.mCount = n;
        FakeStreamB stream;
        gLog.clear();
        difftest::AllocationCounter countA;
        original(&s, &stream, indent);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        s.DumpBody(&stream, indent);
        countB.Stop();
        ++ctx.cases;
        blocks += logA.find("manager") != std::string::npos;
        if (indent > 100) ++big;
        if (logA != gLog || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes())
            ctx.Fail("case %u indent %u: blocks %lld/%lld\noriginal\n%sours\n%s", i, indent, countA.Blocks(), countB.Blocks(),
                     logA.c_str(), gLog.c_str());
    }
    *ctx.image.At<void *>(0x10caa3c) = saved;
    if (blocks < 100 || big < 5) ctx.Fail("coverage: %d %d", blocks, big);
}

// ---- 0xaa4a40 and 0xaa4fa0 ----------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderConstruct, 0xaa4a40)
{
    auto original = ctx.Original<cShader *(__thiscall *)(cShader *)>(0xaa4a40);
    for (uint32_t i = 0; i < 20; ++i) {
        alignas(16) uint8_t bufA[sizeof(cShader)], bufB[sizeof(cShader)];
        std::memset(bufA, 0x11 * (i + 1), sizeof(bufA));
        std::memset(bufB, 0x11 * (i + 1), sizeof(bufB));
        cShader *x = original(reinterpret_cast<cShader *>(bufA));
        cShader *y = reinterpret_cast<cShader *>(bufB)->Construct();
        ++ctx.cases;
        // The table word: the image's 0xe758dc on one side, ours on the other (the table test compares the tables).
        if (*reinterpret_cast<void *const *>(bufA) != GameAddress<void>(0xe758dc) ||
            *reinterpret_cast<void *const *>(bufB) != static_cast<const void *>(cShader::kOwnVTable))
            ctx.Fail("fill %u: table word is not the expected one", i);
        *reinterpret_cast<void **>(bufA) = *reinterpret_cast<void **>(bufB) = nullptr;
        if (x !=reinterpret_cast<cShader *>(bufA) || y != reinterpret_cast<cShader *>(bufB) || std::memcmp(bufA, bufB, sizeof(bufA)) != 0)
            ctx.Fail("fill %u: result or bytes differ", i);
    }
}

DIFF_TEST(SG_ShaderCreate, 0xaa4fa0)
{
    auto original = ctx.Original<cShader *(__cdecl *)()>(0xaa4fa0);
    ShaderPool pool;
    for (uint32_t i = 0; i < 6; ++i) {
        pool.Reset();
        std::string pageA, pageB;
        cShader *a = original();
        pageA = pool.Bytes();
        pool.Reset();
        cShader *b = cShader::Create();
        pageB = pool.Bytes();
        ++ctx.cases;
        if (a == nullptr || a != b) {
            ctx.Fail("round %u: no object or different blocks", i);
            continue;
        }
        // The table word differs by design (the image's table, ours): check it, then blank it on both sides.
        const size_t at = size_t(reinterpret_cast<uint8_t *>(a) - pool.page);
        uint32_t wordA, wordB;
        std::memcpy(&wordA, pageA.data() + at, 4);
        std::memcpy(&wordB, pageB.data() + at, 4);
        if (wordA != uint32_t(uintptr_t(GameAddress<void>(0xe758dc))) || wordB != uint32_t(uintptr_t(static_cast<const void *>(cShader::kOwnVTable))))
            ctx.Fail("round %u: table words %x/%x", i, wordA, wordB);
        pageA.replace(at, 4, 4, '\0');
        pageB.replace(at, 4, 4, '\0');
        if (pageA != pageB) ctx.Fail("round %u: pointers %p/%p page equal %d", i, a, b, pageA == pageB);
    }
}

// ---- lane RX: the hash mixes, the copy constructors, Build and the shared slot bodies -----------------------------------
namespace {

// A part vector's storage as the copy constructor lays it out: freed here by the protocol the destroy uses.
void FreeBlock(ShaderPartVec &v)
{
    if (!v.mBegin) return;
    const size_t capBytes = size_t(v.mCap - v.mBegin) * sizeof(void *);
    if (capBytes >= 0x1000) ::operator delete(reinterpret_cast<void **>(v.mBegin)[-1]);
    else ::operator delete(v.mBegin);
}

void SnapBytes(std::string &o, const void *p, size_t n) { o.append(static_cast<const char *>(p), n); }

// A vector by value: its sizes and elements, never its addresses.
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

// A pass by value: vector words and the pad byte at +0x3f (the copy constructor leaves it) left out.
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

std::string SnapShader(const cShader &s)
{
    std::string o;
    SnapBytes(o, &s.mRefCount, 4);
    SnapBytes(o, &s.mEnabled, 1);
    SnapBytes(o, s.mRanges, sizeof(s.mRanges));
    ShaderInfo info = s.mInfo;
    uint32_t first = 0xffffffffu;
    if (info.mFirstPass) first = uint32_t(info.mFirstPass - s.mPasses.mBegin);
    info.mFirstPass = nullptr;
    SnapBytes(o, &info, sizeof(info));
    SnapBytes(o, &first, 4);
    SnapBytes(o, &s.mHash, 4);
    const uint32_t n[2] = {uint32_t(s.mPasses.mEnd - s.mPasses.mBegin), uint32_t(s.mPasses.mCap - s.mPasses.mBegin)};
    SnapBytes(o, n, sizeof(n));
    for (const ShaderPass *p = s.mPasses.mBegin; p != s.mPasses.mEnd; ++p) SnapPass(o, *p);
    return o;
}

// ---- fakes for the slot-body tests
void *gExpect = nullptr;
uint32_t __fastcall XAddRef(void *self, void *)
{
    Note("xadd", self == gExpect);
    return 0x77;
}
void *__fastcall XDelete(void *self, void *, uint32_t flags)
{
    Note("xdel", self == gExpect, flags);
    return self;
}
void __fastcall XCache27(void *, void *, void *shader) { Note("c27", shader == gExpect); }
void *gXVt[14];
void *gCacheVt[40];
void *gCacheObject[1] = {gCacheVt};

struct PlantCache {
    void **cache;
    void *saved;
    PlantCache(difftest::Context &ctx)
    {
        std::memset(gCacheVt, 0, sizeof(gCacheVt));
        gCacheVt[27] = reinterpret_cast<void *>(&XCache27);
        cache = ctx.image.At<void *>(0x10f8fcc);
        saved = *cache;
        *cache = gCacheObject;
    }
    ~PlantCache() { *cache = saved; }
};

struct RawShader {
    alignas(16) uint8_t b[sizeof(cShader)];
    cShader *Get() { return reinterpret_cast<cShader *>(b); }
    void Fill(std::mt19937 &rng, void *vptr)
    {
        for (uint8_t &x : b) x = uint8_t(rng());
        *reinterpret_cast<void **>(b) = vptr;
    }
};

template <class Fn> Fn Slot(const void *const *table, int i) { return reinterpret_cast<Fn>(const_cast<void *>(table[i])); }

// The fake of the ctx for Build: slot 16 gives X, slot 20 the device, slot 35 the switch.
struct BuildX : Pads<20> {
    virtual bool S20(uint32_t a)
    {
        Note("x20", a);
        return true;
    }
};
struct BuildCtxA : Pads<16> {
    void *x = nullptr;
    void *dev = nullptr;
    virtual void *S16()
    {
        Note("c16");
        return x;
    }
    virtual void S17() {}
    virtual void S18() {}
    virtual void S19() {}
    virtual void *S20()
    {
        Note("c20");
        return dev;
    }
};
bool gBuildCtx35 = true;
struct BuildCtx : PadsAfter<14, BuildCtxA> {
    virtual bool S35()
    {
        Note("c35");
        return gBuildCtx35;
    }
};

} // namespace

// ---- 0x98bf40 and 0x98c220 ----------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderHashMix, 0x98bf40)
{
    auto original = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x98bf40);
    std::mt19937 rng(0x98bf40);
    std::vector<uint32_t> inputs = {0, 1, 2, 0x7fffffff, 0x80000000u, 0xffffffffu, 0xfffffffeu, 0x55555555, 0xaaaaaaaau, 0xffff, 0x10000};
    for (int b = 0; b < 32; ++b) inputs.push_back(1u << b);
    for (int i = 0; i < 20000; ++i) inputs.push_back(rng());
    int bad = 0;
    for (uint32_t x : inputs) {
        ++ctx.cases;
        if (original(x) != ShaderHashMix(x) && bad++ < 5) ctx.Fail("mix %08x: original %08x ours %08x", x, original(x), ShaderHashMix(x));
    }
}

DIFF_TEST(SG_ShaderHashCombine, 0x98c220)
{
    auto original = ctx.Original<uint32_t(__cdecl *)(uint32_t, uint32_t)>(0x98c220);
    std::mt19937 rng(0x98c220);
    const uint32_t edges[] = {0, 1, 2, 0x7fffffff, 0x80000000u, 0xffffffffu, 0xfffffffeu, 0x55555555, 0xaaaaaaaau};
    int bad = 0;
    auto check = [&](uint32_t low, uint32_t high) {
        ++ctx.cases;
        const uint32_t x = original(low, high), y = ShaderHashCombine(low, high);
        if (x != y && bad++ < 5) ctx.Fail("combine %08x %08x: original %08x ours %08x", low, high, x, y);
    };
    for (uint32_t a : edges)
        for (uint32_t b : edges) check(a, b);
    for (int b = 0; b < 32; ++b) {
        check(1u << b, 0);
        check(0, 1u << b);
        check(1u << b, 1u << ((b + 7) & 31));
    }
    for (int i = 0; i < 20000; ++i) check(rng(), rng());
}

// ---- 0xaa4980 -------------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPartVecCopy, 0xaa4980)
{
    auto original = ctx.Original<ShaderPartVec *(__thiscall *)(ShaderPartVec *, const ShaderPartVec *)>(0xaa4980);
    gLog.reserve(1 << 18);
    std::mt19937 rng(0xaa4980);
    Rig rig;
    const uint32_t sizes[] = {0, 1, 2, 3, 4, 7, 100, 1023, 1024, 1025, 1100, 2048};
    int large = 0, withNull = 0, empty = 0;
    for (uint32_t i = 0; i < 160; ++i) {
        const uint32_t n = i < 12 ? sizes[i] : (rng() % 16 == 0 ? 1024 + rng() % 40 : rng() % 9);
        std::vector<cIShaderPart *> src;
        bool hasNull = false;
        for (uint32_t k = 0; k < n; ++k) {
            const bool isNull = rng() % 7 == 0;
            hasNull |= isNull;
            src.push_back(isNull ? nullptr : &rig.parts[rng() % 4]);
        }
        ShaderPartVec from;
        rig.SetVec(from, src);
        ShaderPartVec a, b;
        std::memset(&a, 0xcc, sizeof(a));
        std::memset(&b, 0xcc, sizeof(b));
        gLog.clear();
        difftest::AllocationCounter countA;
        ShaderPartVec *x = original(&a, &from);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        ShaderPartVec *y = b.CopyConstruct(&from);
        countB.Stop();
        ++ctx.cases;
        std::string sa, sb;
        SnapVec(sa, a);
        SnapVec(sb, b);
        bool aligned = true;
        if (a.mBegin && size_t(a.mCap - a.mBegin) * 4 >= 0x1000) {
            ++large;
            for (const ShaderPartVec *v : {&a, &b}) {
                void *raw = reinterpret_cast<void *const *>(v->mBegin)[-1];
                const uintptr_t at = reinterpret_cast<uintptr_t>(v->mBegin), r = reinterpret_cast<uintptr_t>(raw);
                if ((at & 0x1f) != 0 || r > at || at - r > 0x23 || at - r < 4) aligned = false;
            }
        }
        if (!a.mBegin) ++empty;
        withNull += hasNull;
        if (x != &a || y != &b || sa != sb || logA != gLog || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes() || !aligned)
            ctx.Fail("case %u (%u elements): blocks %lld/%lld bytes %lld/%lld aligned %d\noriginal\n%sours\n%s", i, n, countA.Blocks(),
                     countB.Blocks(), countA.Bytes(), countB.Bytes(), aligned, logA.c_str(), gLog.c_str());
        FreeBlock(a);
        FreeBlock(b);
    }
    if (large < 8 || withNull < 20 || empty < 5) ctx.Fail("coverage: large %d null elements %d empty %d", large, withNull, empty);
}

// ---- 0xaa4ae0 -------------------------------------------------------------------------------------------------------------
DIFF_TEST(SG_ShaderPassCopy, 0xaa4ae0)
{
    auto original = ctx.Original<ShaderPass *(__thiscall *)(ShaderPass *, const ShaderPass *)>(0xaa4ae0);
    PlantManager plant(ctx);
    gLog.reserve(1 << 18);
    std::mt19937 rng(0xaa4ae0);
    Rig rig;
    int handles = 0, large = 0;
    for (uint32_t i = 0; i < 200; ++i) {
        ShaderPass src;
        rig.RandPass(rng, src, true);
        src.mPad3f = 0x5a;
        if (i % 9 == 0) {
            std::vector<cIShaderPart *> big;
            for (uint32_t k = 0; k < 1100; ++k) big.push_back(&rig.parts[k % 4]);
            rig.SetVec(src.mV24, std::move(big));
            ++large;
        }
        if (i % 5 == 0) src.mP8 = src.mPc = src.mP10 = src.mP20 = nullptr;
        if (i % 6 == 0) src.mP48 = src.mP4c = src.mP50 = src.mP54 = nullptr;
        for (uint32_t h : src.mHandles) handles += h != 0xffffffffu;
        alignas(16) uint8_t bufA[sizeof(ShaderPass)], bufB[sizeof(ShaderPass)];
        std::memset(bufA, 0xcc, sizeof(bufA));
        std::memset(bufB, 0xcc, sizeof(bufB));
        auto *a = reinterpret_cast<ShaderPass *>(bufA);
        auto *b = reinterpret_cast<ShaderPass *>(bufB);
        gLog.clear();
        difftest::AllocationCounter countA;
        ShaderPass *x = original(a, &src);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        ShaderPass *y = b->CopyConstruct(&src);
        countB.Stop();
        ++ctx.cases;
        std::string sa, sb;
        SnapPass(sa, *a);
        SnapPass(sb, *b);
        if (x != a || y != b || sa != sb || bufA[0x3f] != 0xcc || bufB[0x3f] != 0xcc || logA != gLog ||
            countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes())
            ctx.Fail("case %u: blocks %lld/%lld bytes %lld/%lld contents equal %d pad %02x/%02x\noriginal\n%sours\n%s", i, countA.Blocks(),
                     countB.Blocks(), countA.Bytes(), countB.Bytes(), sa == sb, bufA[0x3f], bufB[0x3f], logA.c_str(), gLog.c_str());
        for (ShaderPass *p : {a, b})
            for (ShaderPartVec *v : {&p->mV14, &p->mV24, &p->mV30, &p->mV58}) FreeBlock(*v);
    }
    if (handles < 100 || large < 10) ctx.Fail("coverage: handles %d large %d", handles, large);
}

// ---- 0xaa5030 -------------------------------------------------------------------------------------------------------------
// Build: the success path of the pass setup (0xaa53e0, the image's, run for real on our objects) needs the graphics
// managers, so the passes it is given fail early (the device query of pass 0 answers yes and the key's bit 0 is clear: the
// image logs line 0x72 and gives false). That path still runs the whole resizing machinery (0xaa47f0, 0xaa45e0, the extra
// passes' destruction) and the logging; the tail (info block and hash) runs with no descriptors and with no context, no
// table, or a context that says no.
DIFF_TEST(SG_ShaderBuild, 0xaa5030)
{
    using BuildFn = bool(__thiscall *)(cShader *, void *, const ShaderInitView *, const uint32_t *, const ShaderKey *, bool);
    auto original = ctx.Original<BuildFn>(0xaa5030);
    shape::CodePatch reportPatch(0x1bfb92, reinterpret_cast<const void *>(&StandReport));
    PlantManager plant(ctx);
    gLog.reserve(1 << 18);
    std::mt19937 rng(0xaa5030);
    int result[2] = {}, grown = 0, shrunk = 0, reallocated = 0, tails = 0, reported = 0, hashed = 0;
    for (uint32_t iter = 0; iter < 900; ++iter) {
        Rig rig;
        for (FakePart &p : rig.parts)
            for (uint32_t &m : p.mapped) m = rng();
        for (FakePart &p : rig.parts) p.mapped[2] &= ~1u;
        const uint32_t mode = iter % 6; // 0 slot 10 says no, 1 no context, 2 no table, 3 slot 35 says no, 4 and 5 the full path
        const uint32_t wanted = (mode >= 4 || mode == 3) ? rng() % 5 : rng() % 3;
        std::vector<uint8_t> desc(size_t(wanted) * ShaderInitView::kDescriptorBytes, 0);
        for (uint32_t d = 0; d < wanted; ++d)
            for (uint32_t k = 0xc4; k < 0xc8; ++k) desc[d * ShaderInitView::kDescriptorBytes + k] = uint8_t(rng());
        ShaderInitView init;
        std::memset(&init, 0, sizeof(init));
        const uint32_t word = rng();
        std::memcpy(init.mPad0, &word, 4);
        init.mDescBegin = desc.data();
        init.mDescEnd = desc.data() + desc.size();
        for (uint32_t &r : init.mRanges) r = rng();
        uint32_t table[0x22];
        for (uint32_t &t : table) t = rng();
        ShaderKey key;
        for (uint32_t &w : key.w) w = rng();
        key.w[2] &= ~1u;
        const bool report = rng() % 2;
        BuildX x;
        FakeDevB dev;
        BuildCtx bctx;
        bctx.x = &x;
        bctx.dev = &dev;
        gBuildCtx35 = mode != 3;
        gSlot10 = mode != 0;
        void *cx = mode == 1 ? nullptr : &bctx;
        const uint32_t *tbl = mode == 2 ? nullptr : table;
        FakeShader s[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 prime(iter * 977 + 1);
            for (uint32_t &r : s[side].mRanges) r = prime();
            s[side].mHash = prime();
            s[side].mInfo.m84 = prime();
            s[side].mInfo.mCount = prime();
            s[side].mInfo.m90 = uint8_t(prime());
            for (uint8_t &f : s[side].mInfo.mFlags) f = uint8_t(prime());
            BuildPassVec(iter * 13 + 5, s[side].mPasses, rig);
        }
        const uint32_t before = uint32_t(s[0].mPasses.mEnd - s[0].mPasses.mBegin);
        const uint32_t capacity = uint32_t(s[0].mPasses.mCap - s[0].mPasses.mBegin);
        gLog.clear();
        difftest::AllocationCounter countA;
        const bool ra = original(&s[0], cx, &init, tbl, &key, report);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        const bool rb = s[1].BuildBody(cx, &init, tbl, &key, report);
        countB.Stop();
        ++ctx.cases;
        const std::string snapA = SnapShader(s[0]), snapB = SnapShader(s[1]);
        ++result[ra ? 1 : 0];
        if (mode >= 3 && gBuildCtx35 && wanted > before) (wanted <= capacity ? grown : reallocated)++;
        if (mode >= 3 && gBuildCtx35 && wanted < before) ++shrunk;
        if (ra) {
            ++tails;
            hashed += s[0].mInfo.mFirstPass != nullptr;
        }
        reported += logA.find("report") != std::string::npos;
        if (ra != rb || logA != gLog || snapA != snapB || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes())
            ctx.Fail("iter %u mode %u wanted %u: result %d/%d blocks %lld/%lld bytes %lld/%lld state equal %d\noriginal\n%sours\n%s", iter, mode,
                     wanted, ra, rb, countA.Blocks(), countB.Blocks(), countA.Bytes(), countB.Bytes(), snapA == snapB, logA.c_str(),
                     gLog.c_str());
        gLog.clear();
        s[0].mPasses.Destroy();
        s[1].mPasses.Destroy();
    }
    if (result[0] < 100 || result[1] < 100 || grown < 10 || reallocated < 10 || shrunk < 10 || reported < 30 || hashed < 10)
        ctx.Fail("coverage: false %d true %d grown in place %d reallocated %d shrunk %d reported %d with a first pass %d", result[0],
                 result[1], grown, reallocated, shrunk, reported, hashed);
}

// The tail with real passes (none of the setup runs: no context): the flags, the first pass and the hash fold over the
// passes' fields.
DIFF_TEST(SG_ShaderBuildTail, 0xaa5030)
{
    using BuildFn = bool(__thiscall *)(cShader *, void *, const ShaderInitView *, const uint32_t *, const ShaderKey *, bool);
    auto original = ctx.Original<BuildFn>(0xaa5030);
    gLog.reserve(1 << 16);
    std::mt19937 rng(0xaa5031);
    Rig rig;
    int withFlags[5] = {}, nonEmpty = 0;
    for (uint32_t iter = 0; iter < 600; ++iter) {
        const uint32_t n = rng() % 6;
        std::vector<ShaderPass> passes(n);
        for (ShaderPass &p : passes) {
            rig.RandPass(rng, p, false);
            if (rng() % 3 == 0) p.mV30.mEnd = p.mV30.mBegin;
        }
        ShaderInitView init;
        std::memset(&init, 0, sizeof(init));
        for (uint32_t &r : init.mRanges) r = rng();
        ShaderKey key;
        for (uint32_t &w : key.w) w = rng();
        gSlot10 = true;
        FakeShader s[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 prime(iter + 11);
            for (uint32_t &r : s[side].mRanges) r = prime();
            s[side].mHash = prime();
            SetPasses(s[side], passes);
            s[side].mInfo.mCount = prime();
            s[side].mInfo.m84 = prime();
        }
        gLog.clear();
        const bool ra = original(&s[0], nullptr, &init, nullptr, &key, iter % 2 == 0);
        const std::string logA = gLog;
        gLog.clear();
        const bool rb = s[1].BuildBody(nullptr, &init, nullptr, &key, iter % 2 == 0);
        ++ctx.cases;
        nonEmpty += n != 0;
        for (int f = 0; f < 4; ++f) withFlags[f] += s[0].mInfo.mFlags[f];
        withFlags[4] += s[0].mInfo.m90;
        if (!ra || ra != rb || logA != gLog || SnapShader(s[0]) != SnapShader(s[1]))
            ctx.Fail("iter %u (%u passes): result %d/%d\noriginal\n%sours\n%s", iter, n, ra, rb, logA.c_str(), gLog.c_str());
    }
    for (int f : withFlags)
        if (f < 50) ctx.Fail("coverage: a flag was set in only %d runs", f);
    if (nonEmpty < 300) ctx.Fail("coverage: non-empty %d", nonEmpty);
}

// ---- the shared slot bodies, tested through our table ------------------------------------------------------------------
namespace {

void InitFakeTables()
{
    std::memset(gXVt, 0, sizeof(gXVt));
    gXVt[1] = reinterpret_cast<void *>(&XAddRef);
    gXVt[13] = reinterpret_cast<void *>(&XDelete);
}

// AddRef-like: the count comes back incremented.
void CheckAddRef(difftest::Context &ctx, uint32_t rva, const void *ours)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(cShader *)>(rva);
    auto mine = reinterpret_cast<uint32_t(__thiscall *)(cShader *)>(const_cast<void *>(ours));
    std::mt19937 rng(rva);
    for (int i = 0; i < 100; ++i) {
        RawShader a, b;
        a.Fill(rng, nullptr);
        std::memcpy(b.b, a.b, sizeof(a.b));
        if (i == 0) std::memset(a.b + 4, 0xff, 4), std::memset(b.b + 4, 0xff, 4);
        const uint32_t x = original(a.Get()), y = mine(b.Get());
        ++ctx.cases;
        if (x != y || std::memcmp(a.b, b.b, sizeof(a.b)) != 0) ctx.Fail("case %d: %u/%u", i, x, y);
    }
}

} // namespace

DIFF_TEST(SG_ShaderAddRef, 0x146a50) { CheckAddRef(ctx, 0x146a50, cShader::kOwnVTable[1]); }
DIFF_TEST(SG_ShaderBaseAddRef, 0x148fb0) { CheckAddRef(ctx, 0x148fb0, cShader::kOwnBaseVTable[1]); }

DIFF_TEST(SG_ShaderSetEnabled, 0x1489f0)
{
    auto original = ctx.Original<void(__thiscall *)(cShader *, uint8_t)>(0x1489f0);
    auto mine = Slot<void(__thiscall *)(cShader *, uint8_t)>(cShader::kOwnVTable, 4);
    auto base = Slot<void(__thiscall *)(cShader *, uint8_t)>(cShader::kOwnBaseVTable, 4);
    std::mt19937 rng(0x1489f0);
    for (int i = 0; i < 100; ++i) {
        RawShader a, b, c;
        a.Fill(rng, nullptr);
        std::memcpy(b.b, a.b, sizeof(a.b));
        std::memcpy(c.b, a.b, sizeof(a.b));
        const uint8_t v = i < 5 ? uint8_t(i == 4 ? 0xff : i) : uint8_t(rng());
        original(a.Get(), v);
        mine(b.Get(), v);
        base(c.Get(), v);
        ++ctx.cases;
        if (std::memcmp(a.b, b.b, sizeof(a.b)) != 0 || std::memcmp(a.b, c.b, sizeof(a.b)) != 0) ctx.Fail("case %d value %02x", i, v);
    }
}

DIFF_TEST(SG_ShaderGetEnabled, 0x1480a0)
{
    auto original = ctx.Original<uint8_t(__thiscall *)(cShader *)>(0x1480a0);
    auto mine = Slot<uint8_t(__thiscall *)(cShader *)>(cShader::kOwnVTable, 5);
    auto base = Slot<uint8_t(__thiscall *)(cShader *)>(cShader::kOwnBaseVTable, 5);
    std::mt19937 rng(0x1480a0);
    for (int i = 0; i < 100; ++i) {
        RawShader a;
        a.Fill(rng, nullptr);
        RawShader b = a;
        const uint8_t x = original(a.Get()), y = mine(b.Get()), z = base(b.Get());
        ++ctx.cases;
        if (x != y || x != z || std::memcmp(a.b, b.b, sizeof(a.b)) != 0) ctx.Fail("case %d: %02x/%02x/%02x", i, x, y, z);
    }
}

DIFF_TEST(SG_ShaderGetHash, 0x24bde0)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(cShader *)>(0x24bde0);
    auto mine = Slot<uint32_t(__thiscall *)(cShader *)>(cShader::kOwnVTable, 6);
    std::mt19937 rng(0x24bde0);
    for (int i = 0; i < 100; ++i) {
        RawShader a;
        a.Fill(rng, nullptr);
        RawShader b = a;
        const uint32_t x = original(a.Get()), y = mine(b.Get());
        ++ctx.cases;
        if (x != y || std::memcmp(a.b, b.b, sizeof(a.b)) != 0) ctx.Fail("case %d: %08x/%08x", i, x, y);
    }
}

DIFF_TEST(SG_ShaderGetInfo, 0xaa6b20)
{
    auto original = ctx.Original<ShaderInfo *(__thiscall *)(cShader *)>(0xaa6b20);
    auto mine = Slot<ShaderInfo *(__thiscall *)(cShader *)>(cShader::kOwnVTable, 9);
    std::mt19937 rng(0xaa6b20);
    for (int i = 0; i < 20; ++i) {
        RawShader a;
        a.Fill(rng, nullptr);
        RawShader b = a;
        ShaderInfo *x = original(a.Get()), *y = mine(b.Get());
        ++ctx.cases;
        if (reinterpret_cast<uint8_t *>(x) != a.b + 0x80 || reinterpret_cast<uint8_t *>(y) != b.b + 0x80 || std::memcmp(a.b, b.b, sizeof(a.b)) != 0)
            ctx.Fail("case %d", i);
    }
}

DIFF_TEST(SG_ShaderBaseDoNothing, 0x140fe0)
{
    auto original = ctx.Original<void(__thiscall *)(cShader *)>(0x140fe0);
    auto mine = Slot<void(__thiscall *)(cShader *)>(cShader::kOwnBaseVTable, 3);
    std::mt19937 rng(0x140fe0);
    for (int i = 0; i < 10; ++i) {
        RawShader a;
        a.Fill(rng, nullptr);
        RawShader b = a;
        original(a.Get());
        mine(b.Get());
        ++ctx.cases;
        if (std::memcmp(a.b, b.b, sizeof(a.b)) != 0) ctx.Fail("case %d", i);
    }
}

// Release: the last reference lets the cache hold go (when enabled) and deletes through slot 13.
static void CheckRelease(difftest::Context &ctx, uint32_t rva, const void *ours)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(cShader *)>(rva);
    auto mine = reinterpret_cast<uint32_t(__thiscall *)(cShader *)>(const_cast<void *>(ours));
    InitFakeTables();
    PlantCache cache(ctx);
    gLog.reserve(1 << 16);
    std::mt19937 rng(rva);
    int deleted = 0, released = 0;
    for (int i = 0; i < 120; ++i) {
        RawShader a;
        a.Fill(rng, gXVt);
        const uint32_t counts[] = {0, 1, 1, 2, 3, 0x80000000u};
        *reinterpret_cast<uint32_t *>(a.b + 4) = counts[i % 6];
        a.b[8] = (i / 6) % 3 == 0 ? 0 : uint8_t((i / 6) % 3 == 1 ? 1 : rng() | 1);
        RawShader b = a;
        gExpect = a.b;
        gLog.clear();
        const uint32_t x = original(a.Get());
        const std::string logA = gLog;
        gExpect = b.b;
        gLog.clear();
        const uint32_t y = mine(b.Get());
        ++ctx.cases;
        deleted += logA.find("xdel") != std::string::npos;
        released += logA.find("c27") != std::string::npos;
        if (x != y || logA != gLog || std::memcmp(a.b, b.b, sizeof(a.b)) != 0)
            ctx.Fail("case %d: %u/%u\noriginal\n%sours\n%s", i, x, y, logA.c_str(), gLog.c_str());
    }
    if (deleted < 20 || released < 10) ctx.Fail("coverage: deleted %d released %d", deleted, released);
}
DIFF_TEST(SG_ShaderRelease, 0xaa69d0) { CheckRelease(ctx, 0xaa69d0, cShader::kOwnVTable[2]); }
DIFF_TEST(SG_ShaderBaseRelease, 0xaa69a0) { CheckRelease(ctx, 0xaa69a0, cShader::kOwnBaseVTable[2]); }

DIFF_TEST(SG_ShaderBaseQueryInterface, 0xaa6920)
{
    auto original = ctx.Original<bool(__thiscall *)(cShader *, uint32_t, void **)>(0xaa6920);
    auto mine = Slot<bool(__thiscall *)(cShader *, uint32_t, void **)>(cShader::kOwnBaseVTable, 0);
    InitFakeTables();
    gLog.reserve(1 << 16);
    std::mt19937 rng(0xaa6920);
    const uint32_t ids[] = {1, 0x9c544810, cShader::kIidCounted, 0, 2, cShader::kIidA, cShader::kIidB, 0xffffffffu, 0x9c544811, 0xfc17e650};
    int yes = 0;
    for (int i = 0; i < 100; ++i) {
        RawShader a;
        a.Fill(rng, gXVt);
        RawShader b = a;
        const uint32_t id = i < 10 ? ids[i] : (i < 40 ? ids[i % 10] : rng());
        const bool useNull = i % 7 == 6;
        void *outA = reinterpret_cast<void *>(0x1234), *outB = reinterpret_cast<void *>(0x1234);
        gExpect = a.b;
        gLog.clear();
        const bool x = original(a.Get(), id, useNull ? nullptr : &outA);
        const std::string logA = gLog;
        gExpect = b.b;
        gLog.clear();
        const bool y = mine(b.Get(), id, useNull ? nullptr : &outB);
        ++ctx.cases;
        const int kindA = outA == a.b ? 1 : (outA == reinterpret_cast<void *>(0x1234) ? 2 : 3);
        const int kindB = outB == b.b ? 1 : (outB == reinterpret_cast<void *>(0x1234) ? 2 : 3);
        yes += x;
        if (x != y || kindA != kindB || logA != gLog || std::memcmp(a.b, b.b, sizeof(a.b)) != 0)
            ctx.Fail("case %d id %08x: %d/%d out %d/%d\noriginal\n%sours\n%s", i, id, x, y, kindA, kindB, logA.c_str(), gLog.c_str());
    }
    if (yes < 8) ctx.Fail("coverage: %d accepted ids", yes);
}

DIFF_TEST(SG_ShaderBaseDelete, 0xaa4e40)
{
    auto original = ctx.Original<cShader *(__thiscall *)(cShader *, uint32_t)>(0xaa4e40);
    auto mine = Slot<cShader *(__thiscall *)(cShader *, uint32_t)>(cShader::kOwnBaseVTable, 13);
    const uint32_t flagSets[] = {0, 1, 2, 3, 0xfffffffe, 0xffffffff, 4, 5};
    std::mt19937 rng(0xaa4e40);
    int freed = 0;
    for (int i = 0; i < 40; ++i) {
        const uint32_t flags = flagSets[i % 8];
        const bool heap = flags & 1;
        uint8_t stackA[0xc], stackB[0xc];
        uint8_t *a = stackA, *b = stackB;
        if (heap) {
            a = static_cast<uint8_t *>(::operator new(0xc));
            b = static_cast<uint8_t *>(::operator new(0xc));
        }
        for (int k = 0; k < 0xc; ++k) a[k] = b[k] = uint8_t(rng());
        std::string memA, memB;
        difftest::AllocationCounter countA;
        cShader *x = original(reinterpret_cast<cShader *>(a), flags);
        countA.Stop();
        difftest::AllocationCounter countB;
        cShader *y = mine(reinterpret_cast<cShader *>(b), flags);
        countB.Stop();
        ++ctx.cases;
        freed += countA.Blocks() < 0;
        bool same = x == reinterpret_cast<cShader *>(a) && y == reinterpret_cast<cShader *>(b) && countA.Blocks() == countB.Blocks() &&
                    countA.Bytes() == countB.Bytes();
        if (!heap) { // the object is still there: the table word is the base's, the rest as it was
            same = same && *reinterpret_cast<void **>(a) == GameAddress<void>(0xe758a0) &&
                   *reinterpret_cast<const void *const *>(b) == static_cast<const void *>(cShader::kOwnBaseVTable) &&
                   std::memcmp(a + 4, b + 4, 8) == 0;
        }
        if (!same) ctx.Fail("case %d flags %x: blocks %lld/%lld", i, flags, countA.Blocks(), countB.Blocks());
    }
    if (freed < 10) ctx.Fail("coverage: freed %d", freed);
}

// ---- the tables: slot order and count as the image's --------------------------------------------------------------------
DIFF_TEST(SG_ShaderTables, 0xe758dc)
{
    const uint32_t derived[14] = {0xaa6950, 0x146a50, 0xaa69d0, 0xaa6b30, 0x1489f0, 0x1480a0, 0x24bde0,
                                  0xaa6470, 0xaa5030, 0xaa6b20, 0xaa6c80, 0xaa69e0, 0xaa65e0, 0xaa4e60};
    const uint32_t base[14] = {0xaa6920, 0x148fb0, 0xaa69a0, 0x140fe0, 0x1489f0, 0x1480a0, 0x8261ba,
                               0x8261ba, 0x8261ba, 0x8261ba, 0x8261ba, 0x8261ba, 0x8261ba, 0xaa4e40};
    const uint32_t *imageDerived = ctx.image.At<uint32_t>(0xe758dc);
    const uint32_t *imageBase = ctx.image.At<uint32_t>(0xe758a0);
    for (int i = 0; i < 14; ++i) {
        ++ctx.cases;
        if (imageDerived[i] != uint32_t(uintptr_t(GameAddress<void>(derived[i]))))
            ctx.Fail("image table 0xe758dc slot %d is not %x", i, derived[i]);
        if (imageBase[i] != uint32_t(uintptr_t(GameAddress<void>(base[i])))) ctx.Fail("image table 0xe758a0 slot %d is not %x", i, base[i]);
    }
    // Ours, slot by slot: the member (or hooked replacement) whose image counterpart is at that slot above.
    const void *const ours[14] = {MemberAddress(&cShader::QueryInterfaceBody), MemberAddress(&cShader::AddRefBody),
                                  MemberAddress(&cShader::ReleaseBody), MemberAddress(&cShader::ResetPassesBody),
                                  MemberAddress(&cShader::SetEnabledBody), MemberAddress(&cShader::GetEnabledBody),
                                  MemberAddress(&cShader::GetHashBody), MemberAddress(&cShader::IsEqualBody),
                                  MemberAddress(&cShader::BuildBody), MemberAddress(&cShader::GetInfoBody),
                                  MemberAddress(&cShader::ValidateBody), MemberAddress(&cShader::ApplyBody),
                                  MemberAddress(&cShader::DumpBody), MemberAddress(&cShader::DeleteBody)};
    for (int i = 0; i < 14; ++i)
        if (cShader::kOwnVTable[i] != ours[i]) ctx.Fail("our table slot %d", i);
    const void *const oursBase[14] = {MemberAddress(&cShader::BaseQueryInterfaceBody), MemberAddress(&cShader::AddRefBody),
                                      MemberAddress(&cShader::ReleaseBody), MemberAddress(&cShader::BaseDoNothing),
                                      MemberAddress(&cShader::SetEnabledBody), MemberAddress(&cShader::GetEnabledBody),
                                      cShader::kOwnBaseVTable[6], cShader::kOwnBaseVTable[7], cShader::kOwnBaseVTable[8],
                                      cShader::kOwnBaseVTable[9], cShader::kOwnBaseVTable[10], cShader::kOwnBaseVTable[11],
                                      cShader::kOwnBaseVTable[12], MemberAddress(&cShader::BaseDeleteBody)};
    for (int i = 0; i < 14; ++i)
        if (cShader::kOwnBaseVTable[i] != oursBase[i]) ctx.Fail("our base table slot %d", i);
    // The pure slots all hold one function (the image's slots 6..12 are one folded body too).
    for (int i = 7; i < 13; ++i)
        if (cShader::kOwnBaseVTable[i] != cShader::kOwnBaseVTable[6]) ctx.Fail("base table pure slot %d differs", i);
}
