// Differential tests for the shader descriptor helpers (src/sg/cShaderInit.cpp, nGZSceneGraph::DescriptorVec, ShaderDescriptor):
//   SG_DescriptorDestroyRange 0xaa7500   SG_DescriptorFillDefault 0xaa7a90   SG_DescriptorTextureSlotCopy 0xaa7c80
//   SG_DescriptorCopyRange 0xaa77c0      SG_DescriptorCopyCount 0xaa7930     SG_DescriptorAssignCount 0xaa72f0
//   SG_DescriptorAssign 0xaa7db0         SG_DescriptorReallocate 0xaa75e0    SG_DescriptorVecAssign 0xaa7130
//
// The descriptors are built from a seed (Build: fake counted objects, part vectors on the heap, name pools filled by the
// image's Add, texture handles live or -1 with random values, random bytes), twice, one copy for the image's function and one
// for ours. Compared are the log of every call on the fakes (AddRef, Release, the names' slot 0, each texture manager slot by
// number), the blocks and bytes allocated and freed, and the descriptors afterwards as values (Snap: heap blocks by content).
// DestroyRange runs over a random sub-range; FillDefault and the copies write random storage; the assignments run with the
// sources apart, the same descriptors, or the ones after them; the vector functions start from vectors of random size and
// spare room, small and over 4 KB.
#include "difftest.h"
#include "sg/cShaderInit.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace nGZSceneGraph;

namespace {

std::string gLog;

void Note(const char *what, uint32_t a = 0, uint32_t b = 0)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %x %x\n", what, a, b);
    gLog += buf;
}

struct FakeObj : cIShaderObject {
    uint32_t id = 0;
    bool QueryInterface(uint32_t iid, void **) override { Note("qi", id, iid); return false; }
    uint32_t AddRef() override { Note("addref", id); return 1; }
    uint32_t Release() override { Note("release", id); return 1; }
};
FakeObj gObjs[6];

// A name in a pool (slot 0 destroys, slot 1 copies; the size at +6).
struct FakeName {
    void **vt;
    uint16_t m4;
    uint16_t size;
    uint32_t id;
};
void __fastcall NameDestroy(FakeName *self, void *, uint32_t flags) { Note("name-destroy", self->id, flags); }
void __fastcall NameCopy(FakeName *self, void *, char *dest)
{
    std::memset(dest, 0x6b, self->size);
    std::memcpy(dest, self, sizeof(FakeName));
}
void *gNameVt[2] = {reinterpret_cast<void *>(&NameDestroy), reinterpret_cast<void *>(&NameCopy)};
FakeName gNames[3];

// The texture manager the handles report to (cached at 0x10f8f94): every slot logs its number and the id.
template <uint32_t Slot> uint32_t __fastcall MgrSlot(void *, void *, uint32_t id)
{
    Note("tex", Slot, id);
    return 0;
}
template <uint32_t... Slots> void FillMgr(void **vt, std::integer_sequence<uint32_t, Slots...>)
{
    ((vt[Slots] = reinterpret_cast<void *>(&MgrSlot<Slots>)), ...);
}
void *gMgrVt[40];
void *gMgrObject[1] = {gMgrVt};

struct Plant {
    void **cache;
    void *saved;
    explicit Plant(difftest::Context &ctx)
    {
        for (uint32_t i = 0; i < 6; ++i) gObjs[i].id = i + 1;
        const uint16_t sizes[3] = {12, 20, 64};
        for (uint32_t i = 0; i < 3; ++i) gNames[i] = FakeName{gNameVt, 0, sizes[i], 0x200 + i};
        FillMgr(gMgrVt, std::make_integer_sequence<uint32_t, 40>{});
        cache = ctx.image.At<void *>(0x10f8f94);
        saved = *cache;
        *cache = gMgrObject;
        gLog.reserve(1 << 18);
    }
    ~Plant() { *cache = saved; }
};

cIShaderObject *MaybeObj(std::mt19937 &rng) { return rng() % 3 == 0 ? nullptr : &gObjs[rng() % 6]; }

void FillParts(ObjectVec &v, std::mt19937 &rng)
{
    const uint32_t n = rng() % 4;
    if (n == 0 && rng() % 2 == 0) {
        v = {};
        return;
    }
    const uint32_t cap = n + rng() % 3 + (n == 0);
    v.mBegin = static_cast<cIShaderObject **>(::operator new(cap * sizeof(void *)));
    for (uint32_t i = 0; i < n; ++i) v.mBegin[i] = &gObjs[rng() % 6];
    v.mEnd = v.mBegin + n;
    v.mCap = v.mBegin + cap;
}

// n descriptors from the seed; the name pools are filled by the image's Add.
void Build(difftest::Context &ctx, ShaderDescriptor *d, uint32_t n, uint32_t seed)
{
    using AddFn = void(__thiscall *)(NamePool *, const void *);
    const AddFn add = ctx.Original<AddFn>(0xc63120);
    std::mt19937 rng(seed);
    std::memset(d, 0, n * sizeof(ShaderDescriptor));
    for (uint32_t k = 0; k < n; ++k) {
        ShaderDescriptor &x = d[k];
        x.mObject00 = MaybeObj(rng);
        FillParts(x.mPartsA, rng);
        FillParts(x.mPartsB, rng);
        x.mObject1c = MaybeObj(rng);
        x.mObject20 = MaybeObj(rng);
        x.mObject24 = MaybeObj(rng);
        x.mObject28 = MaybeObj(rng);
        for (uint32_t i = 0, names = rng() % 4; i < names; ++i) add(&x.mNames, &gNames[rng() % 3]);
        for (ShaderTextureSlot &t : x.mTextures) {
            t.mHandle = rng() % 3 == 0 ? rng() % 0x40 : 0xffffffffu;
            t.mValue = rng();
        }
        x.mByteC4 = uint8_t(rng());
        x.mByteC5 = uint8_t(rng());
        x.mByteC6 = uint8_t(rng());
        x.mByteC7 = uint8_t(rng());
    }
}

// The descriptors as values: every dword, with the heap blocks (part vectors, name pools) as their contents and whether
// each pointer is null instead of the address.
std::string Snap(const ShaderDescriptor *d, uint32_t n)
{
    std::string s;
    char buf[48];
    auto block = [&](const void *b, const void *e, const void *c) {
        std::snprintf(buf, sizeof(buf), "[%d %x %x]", b != nullptr, uint32_t(uintptr_t(e) - uintptr_t(b)), uint32_t(uintptr_t(c) - uintptr_t(b)));
        s += buf;
        if (b) s.append(static_cast<const char *>(b), static_cast<const char *>(e));
    };
    for (uint32_t k = 0; k < n; ++k) {
        const ShaderDescriptor &x = d[k];
        const uint32_t *w = reinterpret_cast<const uint32_t *>(&x);
        for (uint32_t i = 0; i < sizeof(x) / 4; ++i) {
            const bool heap = (i >= 1 && i <= 6) || (i >= 11 && i <= 16);
            std::snprintf(buf, sizeof(buf), "%x,", heap ? 0 : w[i]);
            s += buf;
        }
        block(x.mPartsA.mBegin, x.mPartsA.mEnd, x.mPartsA.mCap);
        block(x.mPartsB.mBegin, x.mPartsB.mEnd, x.mPartsB.mCap);
        const uint32_t *pw = x.mNames.w;
        block(reinterpret_cast<const void *>(uintptr_t(pw[0])), reinterpret_cast<const void *>(uintptr_t(pw[1])), reinterpret_cast<const void *>(uintptr_t(pw[2])));
        block(reinterpret_cast<const void *>(uintptr_t(pw[3])), reinterpret_cast<const void *>(uintptr_t(pw[4])), reinterpret_cast<const void *>(uintptr_t(pw[5])));
        s += ';';
    }
    return s;
}

using DestroyRangeFn = void(__cdecl *)(ShaderDescriptor *, ShaderDescriptor *, DescriptorVec *);
using VecDestroyFn = void(__thiscall *)(DescriptorVec *);

// A block for count descriptors as the vector allocates one (operator new below 4 KB, else padded and 32-byte aligned with
// the raw pointer before it), so that the image's and our frees take it back.
ShaderDescriptor *AllocBlock(uint32_t count)
{
    const uint32_t bytes = count * uint32_t(sizeof(ShaderDescriptor));
    if (bytes == 0) return nullptr;
    if (bytes < 0x1000) return static_cast<ShaderDescriptor *>(::operator new(bytes));
    void *const raw = ::operator new(bytes + 0x23);
    void **const block = reinterpret_cast<void **>((reinterpret_cast<uintptr_t>(raw) + 0x23) & ~uintptr_t(0x1f));
    block[-1] = raw;
    return reinterpret_cast<ShaderDescriptor *>(block);
}

// A vector of n descriptors from the seed in room for cap.
void MakeVec(difftest::Context &ctx, DescriptorVec &v, uint32_t n, uint32_t cap, uint32_t seed)
{
    v.mBegin = AllocBlock(cap);
    if (n) Build(ctx, v.mBegin, n, seed);
    v.mEnd = v.mBegin + n;
    v.mCap = v.mBegin + cap;
}

// A vector as a value: its size, its capacity and its descriptors. From index `fresh` on the descriptors are default-filled
// ones in new storage, whose texture values are left as the allocator gave them: those are zeroed first (the handles are
// compared).
std::string VecSnap(const DescriptorVec &v, uint32_t fresh = 0xffffffffu)
{
    const uint32_t n = uint32_t(v.mEnd - v.mBegin), cap = uint32_t(v.mCap - v.mBegin);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%d %u/%u:", v.mBegin != nullptr, n, cap);
    for (uint32_t k = fresh; k < n; ++k)
        for (ShaderTextureSlot &t : v.mBegin[k].mTextures) t.mValue = 0;
    return buf + Snap(v.mBegin, n);
}

// Compares one call of the image's function and ours: the call logs, the allocations and the given values afterwards.
struct Compare {
    difftest::Context &ctx;
    std::string logA, logB;
    long long blocksA = 0, blocksB = 0, bytesA = 0, bytesB = 0;

    template <class F> void RunA(F f)
    {
        gLog.clear();
        difftest::AllocationCounter count;
        f();
        count.Stop();
        logA = gLog;
        blocksA = count.Blocks();
        bytesA = count.Bytes();
    }
    template <class F> void RunB(F f)
    {
        gLog.clear();
        difftest::AllocationCounter count;
        f();
        count.Stop();
        logB = gLog;
        blocksB = count.Blocks();
        bytesB = count.Bytes();
    }
    void Check(uint32_t i, const char *what, const std::string &a, const std::string &b)
    {
        ++ctx.cases;
        if (a != b || logA != logB || blocksA != blocksB || bytesA != bytesB)
            ctx.Fail("case %u (%s): values equal %d, blocks %lld/%lld bytes %lld/%lld\noriginal\n%.600s\n%.300s\nours\n%.600s\n%.300s", i,
                     what, a == b, blocksA, blocksB, bytesA, bytesB, logA.c_str(), a.c_str(), logB.c_str(), b.c_str());
    }
};

} // namespace

DIFF_TEST(SG_DescriptorDestroyRange, 0xaa7500)
{
    Plant plant(ctx);
    using Fn = void(__cdecl *)(ShaderDescriptor *, ShaderDescriptor *, DescriptorVec *);
    const Fn original = ctx.Original<Fn>(0xaa7500);
    std::mt19937 rng(0xaa7500);
    uint32_t empty = 0, several = 0;
    for (uint32_t i = 0; i < 300; ++i) {
        const uint32_t n = 1 + rng() % 6, seed = rng();
        const uint32_t from = rng() % (n + 1), to = from + rng() % (n + 1 - from);
        std::vector<ShaderDescriptor> a(n), b(n);
        Build(ctx, a.data(), n, seed);
        Build(ctx, b.data(), n, seed);
        DescriptorVec vec{};
        gLog.clear();
        difftest::AllocationCounter countA;
        original(a.data() + from, a.data() + to, &vec);
        countA.Stop();
        const std::string logA = gLog;
        gLog.clear();
        difftest::AllocationCounter countB;
        DescriptorVec::DestroyRange(b.data() + from, b.data() + to, &vec);
        countB.Stop();
        ++ctx.cases;
        // The descriptors outside the range still own their parts (destroyed by the image afterwards).
        const bool same = Snap(a.data(), n) == Snap(b.data(), n);
        if (!same || logA != gLog || countA.Blocks() != countB.Blocks() || countA.Bytes() != countB.Bytes())
            ctx.Fail("case %u (n %u, [%u, %u)): memory equal %d, blocks %lld/%lld bytes %lld/%lld\noriginal\n%.800sours\n%.800s", i, n,
                     from, to, same, countA.Blocks(), countB.Blocks(), countA.Bytes(), countB.Bytes(), logA.c_str(), gLog.c_str());
        original(a.data(), a.data() + from, &vec);
        original(a.data() + to, a.data() + n, &vec);
        original(b.data(), b.data() + from, &vec);
        original(b.data() + to, b.data() + n, &vec);
        empty += from == to;
        several += to - from >= 2;
    }
    if (empty < 30 || several < 60) ctx.Fail("coverage: empty %u, several %u", empty, several);
}

DIFF_TEST(SG_DescriptorFillDefault, 0xaa7a90)
{
    using Fn = ShaderDescriptor *(__cdecl *)(ShaderDescriptor *, uint32_t, DescriptorVec *);
    const Fn original = ctx.Original<Fn>(0xaa7a90);
    std::mt19937 rng(0xaa7a90);
    constexpr uint32_t kMax = 6;
    for (uint32_t i = 0; i < 300; ++i) {
        std::vector<uint8_t> a((kMax + 1) * sizeof(ShaderDescriptor));
        for (uint8_t &x : a) x = uint8_t(rng());
        std::vector<uint8_t> b = a;
        const uint32_t count = rng() % (kMax + 1);
        DescriptorVec vec{};
        ShaderDescriptor *const da = reinterpret_cast<ShaderDescriptor *>(a.data());
        ShaderDescriptor *const db = reinterpret_cast<ShaderDescriptor *>(b.data());
        const intptr_t endA = original(da, count, &vec) - da;
        const intptr_t endB = DescriptorVec::FillDefault(db, count, &vec) - db;
        ++ctx.cases;
        if (a != b || endA != endB) ctx.Fail("case %u (count %u): end %d/%d, storage equal %d", i, count, int(endA), int(endB), a == b);
    }
}

DIFF_TEST(SG_DescriptorTextureSlotCopy, 0xaa7c80)
{
    Plant plant(ctx);
    using Fn = ShaderTextureSlot *(__thiscall *)(ShaderTextureSlot *, const ShaderTextureSlot *);
    const Fn original = ctx.Original<Fn>(0xaa7c80);
    std::mt19937 rng(0xaa7c80);
    Compare cmp{ctx};
    for (uint32_t i = 0; i < 200; ++i) {
        const ShaderTextureSlot from{rng() % 3 == 0 ? 0xffffffffu : rng() % 0x40, rng()};
        ShaderTextureSlot a{rng(), rng()};
        ShaderTextureSlot b = a;
        ShaderTextureSlot *ra = nullptr, *rb = nullptr;
        cmp.RunA([&] { ra = original(&a, &from); });
        cmp.RunB([&] { rb = b.CopyConstruct(&from); });
        char va[48], vb[48];
        std::snprintf(va, sizeof(va), "%x %x %d", a.mHandle, a.mValue, ra == &a);
        std::snprintf(vb, sizeof(vb), "%x %x %d", b.mHandle, b.mValue, rb == &b);
        cmp.Check(i, "slot", va, vb);
    }
}

namespace {

// The copy constructions into raw storage: n source descriptors copied by `copyA` (the image's) and `copyB` (ours) into two
// equal blocks of random bytes; compared are the copies (and the bytes past them), the logs, the allocations and the ends.
template <class A, class B> void CopyCases(difftest::Context &ctx, uint32_t seed0, A copyA, B copyB)
{
    Plant plant(ctx);
    const DestroyRangeFn destroy = ctx.Original<DestroyRangeFn>(0xaa7500);
    std::mt19937 rng(seed0);
    Compare cmp{ctx};
    for (uint32_t i = 0; i < 300; ++i) {
        const uint32_t n = rng() % 6, seed = rng();
        std::vector<ShaderDescriptor> src(n + 1);
        Build(ctx, src.data(), n, seed);
        std::vector<uint8_t> a((n + 1) * sizeof(ShaderDescriptor));
        for (uint8_t &x : a) x = uint8_t(rng());
        std::vector<uint8_t> b = a;
        ShaderDescriptor *const da = reinterpret_cast<ShaderDescriptor *>(a.data());
        ShaderDescriptor *const db = reinterpret_cast<ShaderDescriptor *>(b.data());
        intptr_t endA = 0, endB = 0;
        cmp.RunA([&] { endA = copyA(src.data(), n, da) - da; });
        cmp.RunB([&] { endB = copyB(src.data(), n, db) - db; });
        char tail[32];
        std::snprintf(tail, sizeof(tail), "end %d ", int(endA));
        std::string va = tail + Snap(da, n) + std::string(a.end() - sizeof(ShaderDescriptor), a.end());
        std::snprintf(tail, sizeof(tail), "end %d ", int(endB));
        std::string vb = tail + Snap(db, n) + std::string(b.end() - sizeof(ShaderDescriptor), b.end());
        cmp.Check(i, "copy", va, vb);
        destroy(da, da + n, nullptr);
        destroy(db, db + n, nullptr);
        destroy(src.data(), src.data() + n, nullptr);
    }
}

// The assignments onto built descriptors: count sources onto dest descriptors, the sources apart (mode 0), the
// descriptors themselves (1) or the ones after them (2). Compared: every descriptor afterwards, logs, allocations, ends.
template <class A, class B> void AssignCases(difftest::Context &ctx, uint32_t seed0, bool single, A assignA, B assignB)
{
    Plant plant(ctx);
    const DestroyRangeFn destroy = ctx.Original<DestroyRangeFn>(0xaa7500);
    std::mt19937 rng(seed0);
    Compare cmp{ctx};
    uint32_t modes[3] = {};
    for (uint32_t i = 0; i < 400; ++i) {
        const uint32_t n = 1 + rng() % 6, seedD = rng(), seedS = rng();
        uint32_t mode = rng() % 4;
        mode = mode == 3 ? 0 : mode;
        if (mode == 2 && n < 2) mode = 1;
        const uint32_t room = mode == 2 ? n - 1 : n;
        const uint32_t count = single ? 1 : rng() % (room + 1);
        std::vector<ShaderDescriptor> a(n), b(n), sa(n), sb(n);
        Build(ctx, a.data(), n, seedD);
        Build(ctx, b.data(), n, seedD);
        Build(ctx, sa.data(), n, seedS);
        Build(ctx, sb.data(), n, seedS);
        const ShaderDescriptor *const fromA = mode == 0 ? sa.data() : a.data() + (mode == 2);
        const ShaderDescriptor *const fromB = mode == 0 ? sb.data() : b.data() + (mode == 2);
        intptr_t endA = 0, endB = 0;
        cmp.RunA([&] { endA = assignA(fromA, count, a.data()) - a.data(); });
        cmp.RunB([&] { endB = assignB(fromB, count, b.data()) - b.data(); });
        char tail[32];
        std::snprintf(tail, sizeof(tail), "end %d ", int(endA));
        const std::string va = tail + Snap(a.data(), n) + "|" + Snap(sa.data(), n);
        std::snprintf(tail, sizeof(tail), "end %d ", int(endB));
        const std::string vb = tail + Snap(b.data(), n) + "|" + Snap(sb.data(), n);
        cmp.Check(i, "assign", va, vb);
        ++modes[mode];
        for (std::vector<ShaderDescriptor> *v : {&a, &b, &sa, &sb}) destroy(v->data(), v->data() + n, nullptr);
    }
    if (modes[0] < 100 || modes[1] < 50 || modes[2] < 50) ctx.Fail("coverage: apart %u, self %u, shifted %u", modes[0], modes[1], modes[2]);
}

} // namespace

DIFF_TEST(SG_DescriptorCopyRange, 0xaa77c0)
{
    using Fn = ShaderDescriptor *(__cdecl *)(const ShaderDescriptor *, const ShaderDescriptor *, ShaderDescriptor *, DescriptorVec *);
    const Fn original = ctx.Original<Fn>(0xaa77c0);
    CopyCases(
        ctx, 0xaa77c0, [&](const ShaderDescriptor *s, uint32_t n, ShaderDescriptor *d) { return original(s, s + n, d, nullptr); },
        [](const ShaderDescriptor *s, uint32_t n, ShaderDescriptor *d) { return DescriptorVec::CopyRange(s, s + n, d, nullptr); });
}

DIFF_TEST(SG_DescriptorCopyCount, 0xaa7930)
{
    using Fn = ShaderDescriptor *(__cdecl *)(const ShaderDescriptor *, uint32_t, ShaderDescriptor *, DescriptorVec *);
    const Fn original = ctx.Original<Fn>(0xaa7930);
    CopyCases(
        ctx, 0xaa7930, [&](const ShaderDescriptor *s, uint32_t n, ShaderDescriptor *d) { return original(s, n, d, nullptr); },
        [](const ShaderDescriptor *s, uint32_t n, ShaderDescriptor *d) { return DescriptorVec::CopyCount(s, n, d, nullptr); });
}

DIFF_TEST(SG_DescriptorAssignCount, 0xaa72f0)
{
    using Fn = ShaderDescriptor *(__cdecl *)(const ShaderDescriptor *, uint32_t, ShaderDescriptor *);
    const Fn original = ctx.Original<Fn>(0xaa72f0);
    AssignCases(
        ctx, 0xaa72f0, false, [&](const ShaderDescriptor *s, uint32_t n, ShaderDescriptor *d) { return original(s, n, d); },
        [](const ShaderDescriptor *s, uint32_t n, ShaderDescriptor *d) { return DescriptorVec::AssignCount(s, n, d); });
}

DIFF_TEST(SG_DescriptorAssign, 0xaa7db0)
{
    using Fn = ShaderDescriptor *(__thiscall *)(ShaderDescriptor *, const ShaderDescriptor *);
    const Fn original = ctx.Original<Fn>(0xaa7db0);
    AssignCases(
        ctx, 0xaa7db0, true, [&](const ShaderDescriptor *s, uint32_t, ShaderDescriptor *d) { return original(d, s) + 1; },
        [](const ShaderDescriptor *s, uint32_t, ShaderDescriptor *d) { return d->Assign(s) + 1; });
}

DIFF_TEST(SG_DescriptorReallocate, 0xaa75e0)
{
    Plant plant(ctx);
    using Fn = void(__thiscall *)(DescriptorVec *, uint32_t, const void *);
    const Fn original = ctx.Original<Fn>(0xaa75e0);
    const VecDestroyFn destroy = ctx.Original<VecDestroyFn>(0xaa7cf0);
    std::mt19937 rng(0xaa75e0);
    Compare cmp{ctx};
    uint32_t fromEmpty = 0, large = 0, inRoom = 0;
    for (uint32_t i = 0; i < 300; ++i) {
        const bool big = rng() % 4 == 0;
        const bool none = rng() % 8 == 0;
        const uint32_t n = none ? 0 : rng() % (big ? 26 : 6), cap = none ? 0 : n + rng() % 4, seed = rng();
        const uint32_t count = n + rng() % (big ? 30 : 8);
        DescriptorVec a{}, b{};
        MakeVec(ctx, a, n, cap, seed);
        MakeVec(ctx, b, n, cap, seed);
        const uint32_t scratch = rng();
        cmp.RunA([&] { original(&a, count, &scratch); });
        cmp.RunB([&] { b.Reallocate(count, &scratch); });
        cmp.Check(i, "reallocate", VecSnap(a, n), VecSnap(b, n));
        fromEmpty += cap == 0;
        large += uint32_t(a.mCap - a.mBegin) * sizeof(ShaderDescriptor) >= 0x1000;
        inRoom += count <= cap;
        destroy(&a);
        destroy(&b);
    }
    if (fromEmpty < 10 || large < 40 || inRoom < 30) ctx.Fail("coverage: from empty %u, large %u, in room %u", fromEmpty, large, inRoom);
}

DIFF_TEST(SG_DescriptorVecAssign, 0xaa7130)
{
    Plant plant(ctx);
    using Fn = void(__thiscall *)(DescriptorVec *, const ShaderDescriptor *, uint32_t);
    const Fn original = ctx.Original<Fn>(0xaa7130);
    const VecDestroyFn destroy = ctx.Original<VecDestroyFn>(0xaa7cf0);
    const DestroyRangeFn destroyRange = ctx.Original<DestroyRangeFn>(0xaa7500);
    std::mt19937 rng(0xaa7130);
    Compare cmp{ctx};
    uint32_t shrink = 0, within = 0, grow = 0, own = 0, large = 0;
    for (uint32_t i = 0; i < 400; ++i) {
        const bool big = rng() % 4 == 0;
        const uint32_t n = big ? 16 + rng() % 10 : rng() % 6, cap = n + rng() % 4, seed = rng(), seedS = rng();
        const bool self = n != 0 && rng() % 6 == 0;
        const uint32_t regime = rng() % 3;
        uint32_t count = rng() % (n + 1);
        if (!self && regime == 1 && cap > n) count = n + 1 + rng() % (cap - n);
        if (!self && regime == 2) count = cap + 1 + rng() % (big ? 30 : 6);
        DescriptorVec a{}, b{};
        MakeVec(ctx, a, n, cap, seed);
        MakeVec(ctx, b, n, cap, seed);
        std::vector<ShaderDescriptor> sa(count + 1), sb(count + 1);
        Build(ctx, sa.data(), count, seedS);
        Build(ctx, sb.data(), count, seedS);
        const ShaderDescriptor *const fromA = self ? a.mBegin : sa.data();
        const ShaderDescriptor *const fromB = self ? b.mBegin : sb.data();
        cmp.RunA([&] { original(&a, fromA, count); });
        cmp.RunB([&] { b.Assign(fromB, count); });
        cmp.Check(i, "vector assign", VecSnap(a) + "|" + Snap(sa.data(), count), VecSnap(b) + "|" + Snap(sb.data(), count));
        own += self;
        shrink += !self && count <= n;
        within += n < count && count <= cap;
        grow += count > cap;
        large += uint32_t(a.mCap - a.mBegin) * sizeof(ShaderDescriptor) >= 0x1000;
        destroy(&a);
        destroy(&b);
        destroyRange(sa.data(), sa.data() + count, nullptr);
        destroyRange(sb.data(), sb.data() + count, nullptr);
    }
    if (shrink < 50 || within < 50 || grow < 50 || own < 30 || large < 30)
        ctx.Fail("coverage: shrink %u, within %u, grow %u, own %u, large %u", shrink, within, grow, own, large);
}
