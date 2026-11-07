// Differential tests for the secondary subobject of nTSSG::cAnimRequest (subobject at +248, vtable 0xdb3dc4). Each
// slot is a thunk (`sub ecx,[ecx-4]; jmp body`). The bodies only read and write memory at offsets from the adjusted
// pointer and return a value in eax, so each case runs the original thunk and our member on two copies of the same
// random bytes: the subobject sits at +0x200 of a 1 KB block, the vtordisp at subobject - 4 is one of several values
// (so the adjustment is exercised), and each side has canaries on both ends. The test compares the whole block,
// canaries and the returned eax (for getters; setters return nothing that is compared).
#include "difftest.h"
#include "sg/cAnimRequest.h"

#include <cstring>
#include <functional>
#include <random>

namespace {

using Sub = nTSSG::cAnimRequestSubobject;

constexpr int kBlock = 1024;
constexpr int kSubOffset = 0x200; // subobject position inside the block; adjusted pointers stay inside it

struct State {
    uint8_t block[kBlock];
    uint32_t disp; // vtordisp stored at subobject - 4
    uint32_t args[3];
};

struct Side {
    uint8_t front[32];
    uint8_t block[kBlock];
    uint8_t back[32];
};

State RandomState(std::mt19937 &rng)
{
    State st;
    for (auto &b : st.block) b = static_cast<uint8_t>(rng());
    static const uint32_t kDisps[5] = {0, 4, 8, 0x18, 0xf8};
    st.disp = kDisps[rng() % 5];
    for (auto &a : st.args) a = static_cast<uint32_t>(rng());
    return st;
}

void Build(Side &s, const State &st)
{
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.block, st.block, sizeof(s.block));
    std::memcpy(s.block + kSubOffset - 4, &st.disp, sizeof(st.disp));
}

bool CanariesIntact(const Side &s)
{
    for (uint8_t b : s.front)
        if (b != 0xA5) return false;
    for (uint8_t b : s.back)
        if (b != 0x5A) return false;
    return true;
}

// Runs one slot: `original` and `ours` receive the subobject pointer and the arguments, and return eax.
using Call = std::function<uint32_t(void *sub, const uint32_t *args)>;

void Run(difftest::Context &ctx, const char *name, std::mt19937 &rng, int trials, bool returns, const Call &original,
         const Call &ours)
{
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        Side a, b;
        Build(a, st);
        Build(b, st);
        const uint32_t ra = original(a.block + kSubOffset, st.args);
        const uint32_t rb = ours(b.block + kSubOffset, st.args);
        ++ctx.cases;
        if (returns && ra != rb) ctx.Fail("%s: result original %08x, ours %08x (disp %u)", name, ra, rb, st.disp);
        if (std::memcmp(a.block, b.block, sizeof(a.block)) != 0)
            ctx.Fail("%s: block differs (disp %u, trial %d)", name, st.disp, i);
        if (!CanariesIntact(a) || !CanariesIntact(b)) ctx.Fail("%s: canaries overwritten", name);
        if (ctx.failures.size() > 0) return;
    }
}

} // namespace

DIFF_TEST(cAnimRequest_SetDwordEc, 0x2b6c31)
{
    std::mt19937 rng(0x7001);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2b6c31);
    Run(ctx, "SetDwordEc", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, a[0]); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetDwordEc(a[0]); return 0u; });
}

DIFF_TEST(cAnimRequest_SetDwordA8, 0x2b6e7d)
{
    std::mt19937 rng(0x7002);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2b6e7d);
    Run(ctx, "SetDwordA8", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, a[0]); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetDwordA8(a[0]); return 0u; });
}

DIFF_TEST(cAnimRequest_SetWord98, 0x2b6daa)
{
    std::mt19937 rng(0x7003);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint16_t)>(0x2b6daa);
    Run(ctx, "SetWord98", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, static_cast<uint16_t>(a[0])); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetWord98(static_cast<uint16_t>(a[0])); return 0u; });
}

DIFF_TEST(cAnimRequest_SetWord96, 0x2b6bef)
{
    std::mt19937 rng(0x7004);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint16_t)>(0x2b6bef);
    Run(ctx, "SetWord96", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, static_cast<uint16_t>(a[0])); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetWord96(static_cast<uint16_t>(a[0])); return 0u; });
}

DIFF_TEST(cAnimRequest_SetByte94, 0x2b6c4d)
{
    std::mt19937 rng(0x7005);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint8_t)>(0x2b6c4d);
    Run(ctx, "SetByte94", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, static_cast<uint8_t>(a[0])); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetByte94(static_cast<uint8_t>(a[0])); return 0u; });
}

DIFF_TEST(cAnimRequest_SetByte84, 0x2b6e3e)
{
    std::mt19937 rng(0x7006);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint8_t)>(0x2b6e3e);
    Run(ctx, "SetByte84", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, static_cast<uint8_t>(a[0])); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetByte84(static_cast<uint8_t>(a[0])); return 0u; });
}

DIFF_TEST(cAnimRequest_SetByte83, 0x2b6e5d)
{
    std::mt19937 rng(0x7007);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint8_t)>(0x2b6e5d);
    Run(ctx, "SetByte83", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, static_cast<uint8_t>(a[0])); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetByte83(static_cast<uint8_t>(a[0])); return 0u; });
}

DIFF_TEST(cAnimRequest_SetDword80, 0x2b6a6d)
{
    std::mt19937 rng(0x7008);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2b6a6d);
    Run(ctx, "SetDword80", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, a[0]); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetDword80(a[0]); return 0u; });
}

DIFF_TEST(cAnimRequest_SetByte58, 0x2b6d78)
{
    std::mt19937 rng(0x7009);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint8_t)>(0x2b6d78);
    Run(ctx, "SetByte58", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, static_cast<uint8_t>(a[0])); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetByte58(static_cast<uint8_t>(a[0])); return 0u; });
}

DIFF_TEST(cAnimRequest_SetDword4C, 0x2b6c6d)
{
    std::mt19937 rng(0x700a);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2b6c6d);
    Run(ctx, "SetDword4C", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, a[0]); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetDword4C(a[0]); return 0u; });
}

DIFF_TEST(cAnimRequest_SetDwords24And20, 0x2b6d0a)
{
    std::mt19937 rng(0x700b);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0x2b6d0a);
    Run(ctx, "SetDwords24And20", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, a[0], a[1]); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetDwords24And20(a[0], a[1]); return 0u; });
}

DIFF_TEST(cAnimRequest_GetDwordEc, 0x2b6447)
{
    std::mt19937 rng(0x700c);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x2b6447);
    Run(ctx, "GetDwordEc", rng, 4000, true,
        [&](void *s, const uint32_t *) { return orig(s); },
        [&](void *s, const uint32_t *) { return reinterpret_cast<Sub *>(s)->GetDwordEc(); });
}

DIFF_TEST(cAnimRequest_GetDwordA8, 0x2b6ed4)
{
    std::mt19937 rng(0x700d);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x2b6ed4);
    Run(ctx, "GetDwordA8", rng, 4000, true,
        [&](void *s, const uint32_t *) { return orig(s); },
        [&](void *s, const uint32_t *) { return reinterpret_cast<Sub *>(s)->GetDwordA8(); });
}

DIFF_TEST(cAnimRequest_GetDword80, 0x2b6283)
{
    std::mt19937 rng(0x700e);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0x2b6283);
    Run(ctx, "GetDword80", rng, 4000, true,
        [&](void *s, const uint32_t *) { return orig(s); },
        [&](void *s, const uint32_t *) { return reinterpret_cast<Sub *>(s)->GetDword80(); });
}

DIFF_TEST(cAnimRequest_SetDwords18_14_10, 0x2b6d4f)
{
    std::mt19937 rng(0x700f);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x2b6d4f);
    Run(ctx, "SetDwords18_14_10", rng, 4000, false,
        [&](void *s, const uint32_t *a) { orig(s, a[0], a[1], a[2]); return 0u; },
        [&](void *s, const uint32_t *a) { reinterpret_cast<Sub *>(s)->SetDwords18_14_10(a[0], a[1], a[2]); return 0u; });
}
