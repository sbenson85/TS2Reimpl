// Differential tests for nGZEvent::cAttributeIDIterator (lane VX, src/gz/cAttributeIDIterator.cpp) against the image's
// functions: the constructors 0x1a9942 / 0x1a8434, the destructor 0x1a8462 and the slots 0x1468b0, 0x1a849d, 0x148fb0,
// 0x1a84c0, 0x1a8475, 0x178550, 0x1a8492, 0x1a8498.
//
// Each side gets the same random 16-byte object (and the same fakes): the id vector is a recording fake (slot 1 AddRef,
// slot 2 Release), the iterator's own vtable a recording fake where the code calls through it (QueryInterface's AddRef,
// Release's deleting destructor). Compared: the result, the whole object after the call, the calls made (in order, with
// their this and arguments) and the frees. The vtable word the constructors store is the owned static 0xd9bd10, the
// image's table in this build: compared as is. GZAttributeIDIterator_OwnTable checks the image-free program's table.
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/cAttributeIDIterator.h"
#include "hook/hooks.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

extern "C" const void *ts2_own_vt_d9bd10[8];

using nGZEvent::cAttributeIDIterator;

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

std::vector<uint32_t> gCalls;
uint32_t gResult; // what the fakes' AddRef / Release return

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gCalls.push_back(1);
    gCalls.push_back(P(self));
    return gResult;
}
uint32_t __fastcall FakeRelease(void *self, void *)
{
    gCalls.push_back(2);
    gCalls.push_back(P(self));
    return gResult + 1;
}
void *__fastcall FakeDelete(void *self, void *, uint32_t flags)
{
    gCalls.push_back(7);
    gCalls.push_back(P(self));
    gCalls.push_back(flags);
    return self;
}
uint32_t __fastcall FakeOther(void *self, void *)
{
    gCalls.push_back(0xbad);
    gCalls.push_back(P(self));
    return 0;
}

const void *const kFakeVTable[8] = {
    reinterpret_cast<const void *>(&FakeOther),  reinterpret_cast<const void *>(&FakeAddRef),
    reinterpret_cast<const void *>(&FakeRelease), reinterpret_cast<const void *>(&FakeOther),
    reinterpret_cast<const void *>(&FakeOther),  reinterpret_cast<const void *>(&FakeOther),
    reinterpret_cast<const void *>(&FakeOther),  reinterpret_cast<const void *>(&FakeDelete),
};

// The id vector: only its vtable word is used.
struct FakeIds {
    const void *vtable = kFakeVTable;
    uint32_t pad[4] = {};
};

struct Words {
    uint32_t w[4];
};

Words Read(const void *p)
{
    Words r;
    std::memcpy(r.w, p, sizeof(r.w));
    return r;
}

void Fill(std::mt19937 &rng, void *p)
{
    auto *w = static_cast<uint32_t *>(p);
    for (int i = 0; i < 4; ++i) w[i] = rng();
}

// Compare the two sides' calls, words and result; `what` names the function.
void Compare(difftest::Context &ctx, const char *what, int i, const std::vector<uint32_t> calls[2], const Words words[2],
             uint32_t result0, uint32_t result1)
{
    ++ctx.cases;
    if (calls[0] != calls[1]) ctx.Fail("%s case %d: calls differ (%zu / %zu words)", what, i, calls[0].size(), calls[1].size());
    if (std::memcmp(&words[0], &words[1], sizeof(Words)) != 0)
        ctx.Fail("%s case %d: object %08x %08x %08x %08x / %08x %08x %08x %08x", what, i, words[0].w[0], words[0].w[1],
                 words[0].w[2], words[0].w[3], words[1].w[0], words[1].w[1], words[1].w[2], words[1].w[3]);
    if (result0 != result1) ctx.Fail("%s case %d: result %08x / %08x", what, i, result0, result1);
}

// The calls recorded with `self` (each side's own object) written as 0, so the two sides compare.
std::vector<uint32_t> Normalised(uint32_t self)
{
    std::vector<uint32_t> calls = gCalls;
    for (uint32_t &w : calls)
        if (w == self) w = 0;
    return calls;
}

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

template <typename T> const void *Member(T pmf)
{
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

} // namespace

DIFF_TEST(GZAttributeIDIterator_Construct, 0x1a9942)
{
    using Fn = void *(__thiscall *)(void *, void *, const uint32_t *const *);
    auto orig = ctx.Original<Fn>(0x1a9942);
    std::mt19937 rng(1);
    FakeIds ids;
    for (int i = 0; i < 4000; ++i) {
        const bool withIds = i % 3 != 0;
        const uint32_t *cursor = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(rng()));
        const uint32_t seed = rng();
        std::vector<uint32_t> calls[2];
        Words words[2];
        uint32_t result[2];
        for (int side = 0; side < 2; ++side) {
            alignas(4) uint8_t obj[16];
            std::mt19937 fill(seed);
            Fill(fill, obj);
            gCalls.clear();
            void *ret = side == 0 ? orig(obj, withIds ? &ids : nullptr, &cursor)
                                  : reinterpret_cast<cAttributeIDIterator *>(obj)->Construct(
                                        withIds ? reinterpret_cast<cIGZUnknown *>(&ids) : nullptr, &cursor);
            result[side] = ret == obj ? 1 : P(ret);
            calls[side] = Normalised(P(obj));
            words[side] = Read(obj);
        }
        Compare(ctx, "Construct", i, calls, words, result[0], result[1]);
        if (words[0].w[0] != P(GameAddress<void>(0xd9bd10))) ctx.Fail("Construct: the vtable word is not 0xd9bd10");
    }
}

DIFF_TEST(GZAttributeIDIterator_CopyConstruct, 0x1a8434)
{
    using Fn = void *(__thiscall *)(void *, const void *);
    auto orig = ctx.Original<Fn>(0x1a8434);
    std::mt19937 rng(2);
    FakeIds ids;
    for (int i = 0; i < 4000; ++i) {
        alignas(4) uint8_t source[16];
        Fill(rng, source);
        if (i % 3 != 0) {
            const uint32_t at = P(&ids);
            std::memcpy(source + 8, &at, 4);
        } else std::memset(source + 8, 0, 4);
        const Words before = Read(source);
        const uint32_t seed = rng();
        std::vector<uint32_t> calls[2];
        Words words[2];
        uint32_t result[2];
        for (int side = 0; side < 2; ++side) {
            alignas(4) uint8_t obj[16];
            std::mt19937 fill(seed);
            Fill(fill, obj);
            gCalls.clear();
            void *ret = side == 0 ? orig(obj, source)
                                  : reinterpret_cast<cAttributeIDIterator *>(obj)->CopyConstruct(
                                        *reinterpret_cast<const cAttributeIDIterator *>(source));
            result[side] = ret == obj ? 1 : P(ret);
            calls[side] = Normalised(P(obj));
            words[side] = Read(obj);
        }
        Compare(ctx, "CopyConstruct", i, calls, words, result[0], result[1]);
        if (std::memcmp(&before, source, 16) != 0) ctx.Fail("CopyConstruct case %d: the source changed", i);
    }
}

DIFF_TEST(GZAttributeIDIterator_Destruct, 0x1a8462)
{
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x1a8462);
    std::mt19937 rng(3);
    FakeIds ids;
    for (int i = 0; i < 4000; ++i) {
        const uint32_t seed = rng();
        const bool withIds = i % 3 != 0;
        std::vector<uint32_t> calls[2];
        Words words[2];
        for (int side = 0; side < 2; ++side) {
            alignas(4) uint32_t obj[4];
            std::mt19937 fill(seed);
            Fill(fill, obj);
            obj[2] = withIds ? P(&ids) : 0;
            gCalls.clear();
            if (side == 0) orig(obj);
            else reinterpret_cast<cAttributeIDIterator *>(obj)->Destruct();
            calls[side] = Normalised(P(obj));
            words[side] = Read(obj);
        }
        Compare(ctx, "Destruct", i, calls, words, 0, 0);
    }
}

DIFF_TEST(GZAttributeIDIterator_DeletingDestructor, 0x1468b0)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x1468b0);
    std::mt19937 rng(4);
    FakeIds ids;
    for (int i = 0; i < 4000; ++i) {
        const uint32_t flags = i < 4 ? static_cast<uint32_t>(i) : rng();
        const uint32_t seed = rng();
        const bool withIds = i % 3 != 0;
        std::vector<uint32_t> calls[2];
        Words words[2] = {};
        uint32_t result[2];
        for (int side = 0; side < 2; ++side) {
            auto *obj = static_cast<uint32_t *>(::operator new(16));
            std::mt19937 fill(seed);
            Fill(fill, obj);
            obj[2] = withIds ? P(&ids) : 0;
            gCalls.clear();
            gCalls.reserve(64); // the fakes' log must not reallocate (a free) inside the recorder's window
            difftest::FreeRecorder rec;
            void *ret = side == 0 ? orig(obj, flags) : reinterpret_cast<cAttributeIDIterator *>(obj)->DeletingDestructor(flags);
            rec.Stop();
            const bool freed = rec.Freed(obj);
            result[side] = ret == obj ? 1 : P(ret);
            calls[side] = Normalised(P(obj));
            if (freed != ((flags & 1) != 0) || rec.Count() != (freed ? 1u : 0u))
                ctx.Fail("DeletingDestructor side %d flags %08x: freed %d, %zu frees", side, flags, freed, rec.Count());
            if (!freed) {
                words[side] = Read(obj);
                ::operator delete(obj);
            }
        }
        Compare(ctx, "DeletingDestructor", i, calls, words, result[0], result[1]);
    }
}

DIFF_TEST(GZAttributeIDIterator_QueryInterface, 0x1a849d)
{
    using Fn = bool(__thiscall *)(void *, uint32_t, void **);
    auto orig = ctx.Original<Fn>(0x1a849d);
    std::mt19937 rng(5);
    const uint32_t riids[] = {0, 1, 2, 0xffffffff};
    for (int i = 0; i < 4000; ++i) {
        const uint32_t riid = i < 8 ? riids[i % 4] : (rng() % 2 ? 1 : rng());
        const bool withOut = i % 8 < 4 || rng() % 4 != 0;
        const uint32_t seed = rng();
        std::vector<uint32_t> calls[2];
        Words words[2];
        uint32_t result[2];
        uint32_t outs[2];
        for (int side = 0; side < 2; ++side) {
            alignas(4) uint32_t obj[4];
            std::mt19937 fill(seed);
            Fill(fill, obj);
            obj[0] = P(kFakeVTable);
            void *out = reinterpret_cast<void *>(static_cast<uintptr_t>(seed));
            gCalls.clear();
            const bool ret = side == 0 ? orig(obj, riid, withOut ? &out : nullptr)
                                       : reinterpret_cast<cAttributeIDIterator *>(obj)->QueryInterface(riid, withOut ? &out : nullptr);
            result[side] = ret;
            outs[side] = out == obj ? 1 : P(out);
            calls[side] = Normalised(P(obj));
            words[side] = Read(obj);
        }
        Compare(ctx, "QueryInterface", i, calls, words, result[0], result[1]);
        if (outs[0] != outs[1]) ctx.Fail("QueryInterface case %d: out %08x / %08x", i, outs[0], outs[1]);
    }
}

DIFF_TEST(GZAttributeIDIterator_RefCount, 0x1a84c0)
{
    using Fn = uint32_t(__thiscall *)(void *);
    auto addRef = ctx.Original<Fn>(0x148fb0);
    auto release = ctx.Original<Fn>(0x1a84c0);
    std::mt19937 rng(6);
    const uint32_t edges[] = {0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xffffffff};
    for (int i = 0; i < 6000; ++i) {
        const uint32_t count = i < 7 ? edges[i] : (rng() % 2 ? rng() % 4 : rng());
        const bool isRelease = i % 2 == 0 || i < 7;
        const uint32_t seed = rng();
        std::vector<uint32_t> calls[2];
        Words words[2];
        uint32_t result[2];
        for (int side = 0; side < 2; ++side) {
            alignas(4) uint32_t obj[4];
            std::mt19937 fill(seed);
            Fill(fill, obj);
            obj[0] = P(kFakeVTable);
            obj[1] = count;
            gCalls.clear();
            auto *it = reinterpret_cast<cAttributeIDIterator *>(obj);
            if (isRelease) result[side] = side == 0 ? release(obj) : it->Release();
            else result[side] = side == 0 ? addRef(obj) : it->AddRef();
            calls[side] = Normalised(P(obj));
            words[side] = Read(obj);
        }
        Compare(ctx, isRelease ? "Release" : "AddRef", i, calls, words, result[0], result[1]);
    }
}

DIFF_TEST(GZAttributeIDIterator_Clone, 0x1a8475)
{
    using Fn = void *(__thiscall *)(const void *);
    auto orig = ctx.Original<Fn>(0x1a8475);
    std::mt19937 rng(7);
    FakeIds ids;
    for (int i = 0; i < 4000; ++i) {
        alignas(4) uint32_t source[4];
        Fill(rng, source);
        source[2] = i % 3 != 0 ? P(&ids) : 0;
        const Words before = Read(source);
        std::vector<uint32_t> calls[2];
        Words words[2];
        for (int side = 0; side < 2; ++side) {
            gCalls.clear();
            void *copy = side == 0 ? orig(source) : reinterpret_cast<const cAttributeIDIterator *>(source)->Clone();
            if (!copy) {
                ctx.Fail("Clone side %d: null", side);
                return;
            }
            calls[side] = Normalised(P(copy));
            words[side] = Read(copy);
            ::operator delete(copy);
        }
        Compare(ctx, "Clone", i, calls, words, 0, 0);
        if (std::memcmp(&before, source, 16) != 0) ctx.Fail("Clone case %d: the source changed", i);
    }
}

DIFF_TEST(GZAttributeIDIterator_Cursor, 0x178550)
{
    using Equal = bool(__thiscall *)(const void *, const void *);
    using GetFn = uint32_t(__thiscall *)(const void *);
    using NextFn = void(__thiscall *)(void *);
    auto isEqual = ctx.Original<Equal>(0x178550);
    auto get = ctx.Original<GetFn>(0x1a8492);
    auto next = ctx.Original<NextFn>(0x1a8498);
    std::mt19937 rng(8);
    uint32_t ids[64];
    for (uint32_t &id : ids) id = rng();
    for (int i = 0; i < 6000; ++i) {
        alignas(4) uint32_t a[4], b[4];
        Fill(rng, a);
        Fill(rng, b);
        a[3] = P(&ids[rng() % 63]);
        b[3] = rng() % 3 == 0 ? a[3] : (rng() % 2 ? P(&ids[rng() % 63]) : a[3] ^ (1u << (rng() % 32)));
        const Words wa = Read(a), wb = Read(b);
        auto *ia = reinterpret_cast<cAttributeIDIterator *>(a);
        ++ctx.cases;
        if (isEqual(a, b) != ia->IsEqual(reinterpret_cast<cAttributeIDIterator *>(b)))
            ctx.Fail("IsEqual case %d differs", i);
        ++ctx.cases;
        if (get(a) != ia->Get()) ctx.Fail("Get case %d differs", i);
        if (std::memcmp(&wa, a, 16) != 0 || std::memcmp(&wb, b, 16) != 0) ctx.Fail("IsEqual / Get case %d wrote", i);
        Words after[2];
        for (int side = 0; side < 2; ++side) {
            alignas(4) uint32_t c[4];
            std::memcpy(c, &wa, 16);
            if (side == 0) next(c);
            else reinterpret_cast<cAttributeIDIterator *>(c)->Next();
            after[side] = Read(c);
        }
        ++ctx.cases;
        if (std::memcmp(&after[0], &after[1], 16) != 0) ctx.Fail("Next case %d differs", i);
    }
}

// The image-free program's table: slot for slot the image's entry's hook where it is hooked, else (the folded bodies)
// our member; the owned static is the image's table in this build.
DIFF_TEST(GZAttributeIDIterator_OwnTable, 0xd9bd10)
{
    const uint32_t base = P(GameAddress<void>(0));
    const uint32_t *image = GameAddress<uint32_t>(0xd9bd10);
    ++ctx.cases;
    if (static_cast<const void *>(&ts2_rva_d9bd10) != image) ctx.Fail("the owned static is not the image's table");
    const void *const folded[8] = {
        Member(&cAttributeIDIterator::QueryInterface), Member(&cAttributeIDIterator::AddRef),
        Member(&cAttributeIDIterator::Release),        nullptr,
        Member(&cAttributeIDIterator::IsEqual),        nullptr,
        nullptr,                                       nullptr,
    };
    const uint32_t expect[8] = {0x1a849d, 0x148fb0, 0x1a84c0, 0x1a8475, 0x178550, 0x1a8492, 0x1a8498, 0x1468b0};
    for (int i = 0; i < 8; ++i) {
        ++ctx.cases;
        if (image[i] - base != expect[i]) ctx.Fail("slot %d: the image's entry is %x", i, image[i] - base);
        if (folded[i]) {
            if (ts2_own_vt_d9bd10[i] != folded[i]) ctx.Fail("slot %d: not our member", i);
            if (HookAt(expect[i])) ctx.Fail("slot %d: the folded body %x is hooked", i, expect[i]);
            continue;
        }
        const HookSpec *h = HookAt(expect[i]);
        if (!h) ctx.Fail("slot %d: %x is not hooked", i, expect[i]);
        else if (h->replacement != ts2_own_vt_d9bd10[i]) ctx.Fail("slot %d: ours is not what the hook of %x installs", i, expect[i]);
    }
}
