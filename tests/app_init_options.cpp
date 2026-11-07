// Differential tests for the option-flag helpers in src/app/AppInit.* :
//   0xb39380  GetOptionMask      (32 queries of cTSAppCommonImpl slot 4 through 0x1d0c00, cached in a global)
//   0xb39470  HighestOptionBit   (index of the highest set bit of that mask)
//   0xc274e3  ReadApplicationOptions (command line text, three text options, one query per mask bit)
//
// The framework (game global 0x10c9378) and the cTSAppCommonImpl object (cache global 0x10cc040) are
// fakes with recording vtables. The strings the code passes are stack locals, so the fake records what
// they hold (vtable present, text, small or heap buffer) instead of their addresses.
#include "difftest.h"
#include "app/AppInit.h"
#include "rz/cRZString.h"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

struct Call {
    int what;
    uint32_t a, b, c, d;
    std::string text;
    bool operator==(const Call &o) const { return what == o.what && a == o.a && b == o.b && c == o.c && d == o.d && text == o.text; }
};

std::vector<Call> gCalls;
uint32_t gResponses = 0; // slot 4 answers true for 0x4bcfce5e when (flag & gResponses) != 0

struct FakeObject {
    void **vtable;
    uint32_t pad[3];
};

std::string Describe(const cRZString *s)
{
    std::string seen = s->mVTable ? "vptr:" : "novptr:";
    seen += s->mString;
    seen += s->mString.capacity() == 15 ? "|sso" : "|big";
    return seen;
}

// cTSAppCommonImpl slot 4 (ret 0x14), as the option code calls it.
uint32_t __fastcall Slot4(void *, void *, uint32_t id, cRZString *text, uint32_t c, uint32_t d, uint32_t e)
{
    gCalls.push_back({4, id, c, d, e, Describe(text)});
    if (id == 0x4bcfce5e) return (e & gResponses) != 0 ? 0xABCDEF01u : 0xABCDEF00u;
    return 0x55;
}

// cIGZCmdLine slot 5, GetCommandLineText.
uint32_t __fastcall CmdLineGetText(void *, void *, cRZString *out)
{
    gCalls.push_back({5, 0, 0, 0, 0, Describe(out)});
    return 1;
}

void *__fastcall FwGetCmdLine(void *, void *);

void *gFwVtable[64];
void *gCmdVtable[16];
void *gCommonVtable[16];
FakeObject gFramework, gCmd, gCommon;

void *__fastcall FwGetCmdLine(void *, void *)
{
    return &gCmd;
}

void Setup()
{
    std::memset(gFwVtable, 0, sizeof(gFwVtable));
    std::memset(gCmdVtable, 0, sizeof(gCmdVtable));
    std::memset(gCommonVtable, 0, sizeof(gCommonVtable));
    gFwVtable[21] = reinterpret_cast<void *>(&FwGetCmdLine);
    gCmdVtable[5] = reinterpret_cast<void *>(&CmdLineGetText);
    gCommonVtable[4] = reinterpret_cast<void *>(&Slot4);
    gFramework.vtable = gFwVtable;
    gCmd.vtable = gCmdVtable;
    gCommon.vtable = gCommonVtable;
}

constexpr uint32_t kFrameworkGlobal = 0x10c9378, kCommonCache = 0x10cc040;

void Install(difftest::Context &ctx, uint32_t mask)
{
    *ctx.image.At<void *>(kFrameworkGlobal) = &gFramework;
    *ctx.image.At<void *>(kCommonCache) = &gCommon;
    *ctx.image.At<uint32_t>(appinit::kOptionMaskRva) = mask;
}

void Clear(difftest::Context &ctx)
{
    *ctx.image.At<void *>(kFrameworkGlobal) = nullptr;
    *ctx.image.At<void *>(kCommonCache) = nullptr;
    *ctx.image.At<uint32_t>(appinit::kOptionMaskRva) = 0;
}

} // namespace

DIFF_TEST(AppInit_OptionMask, 0xb39380)
{
    Setup();
    using Fn = uint32_t(__cdecl *)();
    Fn original = ctx.Original<Fn>(0xb39380);
    Fn ours = &appinit::GetOptionMask;
    std::mt19937 rng(0xb39380);
    uint32_t fixed[] = {0, 1, 0x80000000u, 0xffffffffu, 0x1000, 0x00ff00ffu, 0x55555555u, 0xaaaaaaaau};
    for (int i = 0; i < 40; ++i) {
        uint32_t responses = i < 8 ? fixed[i] : rng();
        uint32_t preset = (i % 5 == 4) ? (rng() | 1) : 0; // a mask that is already built is returned as it is
        gResponses = responses;
        Install(ctx, preset);
        gCalls.clear();
        uint32_t a = original();
        uint32_t maskA = *ctx.image.At<uint32_t>(appinit::kOptionMaskRva);
        std::vector<Call> callsA = gCalls;
        Install(ctx, preset);
        gCalls.clear();
        uint32_t b = ours();
        uint32_t maskB = *ctx.image.At<uint32_t>(appinit::kOptionMaskRva);
        ++ctx.cases;
        if (a != b || maskA != maskB || callsA != gCalls)
            ctx.Fail("responses %08x preset %08x: original %08x (global %08x, %zu calls), ours %08x (global %08x, %zu calls)", responses, preset, a, maskA, callsA.size(), b, maskB,
                     gCalls.size());
        if (preset == 0 && ((a & 0x1000) == 0 || callsA.size() != 32)) ctx.Fail("responses %08x: built mask %08x, %zu queries", responses, a, callsA.size());
        if (preset != 0 && !callsA.empty()) ctx.Fail("preset %08x: the original queried", preset);
    }
    Clear(ctx);
}

DIFF_TEST(AppInit_HighestOptionBit, 0xb39470)
{
    Setup();
    using Fn = uint32_t(__cdecl *)();
    Fn original = ctx.Original<Fn>(0xb39470);
    Fn ours = &appinit::HighestOptionBit;
    std::mt19937 rng(0xb39470);
    std::vector<uint32_t> masks = {1, 2, 3, 4, 0x1000, 0x1001, 0x7fffffff, 0x80000000u, 0xffffffffu, 0x10, 0x8000, 0x10000};
    for (int i = 0; i < 40; ++i) masks.push_back((rng() >> (rng() % 32)) | 1);
    for (uint32_t m : masks) {
        Install(ctx, m);
        uint32_t a = original();
        uint32_t b = ours();
        ++ctx.cases;
        if (a != b) ctx.Fail("mask %08x: original %u, ours %u", m, a, b);
    }
    Clear(ctx);
}

// 0xb39490: the whole eax is compared (the upper 24 bits are those of mask & bits).
DIFF_TEST(AppInit_HasOptionBits, 0xb39490)
{
    Setup();
    using Fn = uint32_t(__cdecl *)(uint32_t);
    Fn original = ctx.Original<Fn>(0xb39490);
    Fn ours = &appinit::HasOptionBits;
    std::mt19937 rng(0xb39490);
    uint32_t yes = 0, no = 0;
    for (int i = 0; i < 400; ++i) {
        const uint32_t mask = (i % 5 == 0) ? 0xffffffffu : ((rng() >> (rng() % 32)) | 0x1000);
        Install(ctx, mask);
        uint32_t bits;
        switch (i % 4) {
        case 0: bits = mask & rng(); break;                    // a subset: true
        case 1: bits = (mask & rng()) | (1u << (rng() % 32)); break; // maybe one bit too many
        case 2: bits = 0; break;
        default: bits = rng(); break;
        }
        const uint32_t a = original(bits);
        const uint32_t b = ours(bits);
        ++ctx.cases;
        (a & 0xff ? yes : no)++;
        if (a != b) ctx.Fail("mask %08x bits %08x: original %08x, ours %08x", mask, bits, a, b);
    }
    if (yes < 50 || no < 50) ctx.Fail("coverage: %u true, %u false", yes, no);
    Clear(ctx);
}

DIFF_TEST(AppInit_ReadApplicationOptions, 0xc274e3)
{
    Setup();
    using Fn = void(__fastcall *)(void *, void *);
    Fn original = ctx.Original<Fn>(0xc274e3);
    Fn ours = &appinit::ReadApplicationOptions;
    uint32_t masks[] = {1, 0x1000, 0x1001, 0x80000001u, 0x7fffffffu, 0x3, 0x100, 0xffffffffu};
    for (uint32_t mask : masks)
        for (uint32_t responses : {0u, 0xffffffffu, 0x5u}) {
            gResponses = responses;
            Install(ctx, mask);
            gCalls.clear();
            original(&gFramework, nullptr);
            std::vector<Call> callsA = gCalls;
            gCalls.clear();
            ours(&gFramework, nullptr);
            ++ctx.cases;
            if (callsA != gCalls) ctx.Fail("mask %08x responses %08x: original %zu calls, ours %zu calls", mask, responses, callsA.size(), gCalls.size());
            uint32_t top = 0;
            for (uint32_t m = mask >> 1; m; m >>= 1) ++top;
            size_t expected = 1 + 3 + (top + 1); // command line text, three options, a query per bit 0..top
            if (callsA.size() != expected) ctx.Fail("mask %08x: the original made %zu calls, expected %zu", mask, callsA.size(), expected);
        }
    Clear(ctx);
}
