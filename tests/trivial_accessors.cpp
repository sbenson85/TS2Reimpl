// Differential tests for the folded one-line getters/setters (src/rz/TrivialAccessors.h): each is
// run on the same random memory block on both sides; return value (the byte only, where the
// original returns in al) and the block afterwards are compared.
#include "difftest.h"
#include "rz/TrivialAccessors.h"

#include <cstring>
#include <random>

namespace {

constexpr uint32_t kBlock = 0x40;

void Fill(unsigned char *block, std::mt19937 &rng)
{
    for (uint32_t i = 0; i < kBlock; ++i) block[i] = static_cast<unsigned char>(rng());
}

using GetFn = uint32_t(__thiscall *)(const void *);
using SetFn = void(__thiscall *)(void *, uint32_t);

void CheckGetter(difftest::Context &ctx, uint32_t rva, uint32_t (__fastcall *ours)(const void *, void *), uint32_t offset)
{
    auto orig = ctx.Original<GetFn>(rva);
    std::mt19937 rng(rva);
    for (int run = 0; run < 200; ++run) {
        alignas(4) unsigned char a[kBlock], b[kBlock];
        Fill(a, rng);
        std::memcpy(b, a, kBlock);
        uint32_t ra = orig(a), rb = ours(b, nullptr);
        ++ctx.cases;
        if (ra != rb || ra != *reinterpret_cast<uint32_t *>(a + offset) || std::memcmp(a, b, kBlock) != 0) ctx.Fail("0x%x run %d: %08x/%08x", rva, run, ra, rb);
    }
}

void CheckSetter(difftest::Context &ctx, uint32_t rva, void (__fastcall *ours)(void *, void *, uint32_t), uint32_t offset)
{
    auto orig = ctx.Original<SetFn>(rva);
    std::mt19937 rng(rva);
    for (int run = 0; run < 200; ++run) {
        alignas(4) unsigned char a[kBlock], b[kBlock];
        Fill(a, rng);
        std::memcpy(b, a, kBlock);
        uint32_t value = static_cast<uint32_t>(rng());
        orig(a, value);
        ours(b, nullptr, value);
        ++ctx.cases;
        if (std::memcmp(a, b, kBlock) != 0 || *reinterpret_cast<uint32_t *>(a + offset) != value) ctx.Fail("0x%x run %d: block differs", rva, run);
    }
}

} // namespace

DIFF_TEST(TrivialAccessors_Getters, 0x145770)
{
    CheckGetter(ctx, 0x46390, acc::GetDword08, 0x8);
    CheckGetter(ctx, 0x145770, acc::GetDword0C, 0xc);
    CheckGetter(ctx, 0x149cf0, acc::GetDword10, 0x10);
}

DIFF_TEST(TrivialAccessors_Setters, 0x149c60)
{
    CheckSetter(ctx, 0x149c60, acc::SetDword0C, 0xc);
    CheckSetter(ctx, 0x47fc80, acc::SetDword18, 0x18);
    CheckSetter(ctx, 0x5506e0, acc::SetDword30, 0x30);
}

DIFF_TEST(TrivialAccessors_ByteField, 0x156110)
{
    auto get = ctx.Original<uint8_t(__thiscall *)(const void *)>(0x156110);
    auto set = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x1b9690);
    std::mt19937 rng(0x156110);
    for (int run = 0; run < 300; ++run) {
        alignas(4) unsigned char a[kBlock], b[kBlock];
        Fill(a, rng);
        std::memcpy(b, a, kBlock);
        ++ctx.cases;
        if (get(a) != acc::GetByte18(b, nullptr) || get(a) != a[0x18]) ctx.Fail("GetByte18 run %d", run);
        // The argument is a dword; only its low byte is stored, the neighbours are untouched.
        uint32_t value = static_cast<uint32_t>(rng());
        set(a, value);
        acc::SetByte18(b, nullptr, value);
        if (std::memcmp(a, b, kBlock) != 0 || a[0x18] != static_cast<uint8_t>(value)) ctx.Fail("SetByte18 run %d", run);
    }
}

DIFF_TEST(TrivialAccessors_ByteField09, 0x4e9cc0)
{
    auto get = ctx.Original<uint8_t(__thiscall *)(const void *)>(0x4e9cc0);
    auto set = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x4ea470);
    std::mt19937 rng(0x4e9cc0);
    for (int run = 0; run < 300; ++run) {
        alignas(4) unsigned char a[kBlock], b[kBlock];
        Fill(a, rng);
        std::memcpy(b, a, kBlock);
        ++ctx.cases;
        if (get(a) != acc::GetByte09(b, nullptr) || get(a) != a[9]) ctx.Fail("GetByte09 run %d", run);
        uint32_t value = static_cast<uint32_t>(rng());
        set(a, value);
        acc::SetByte09(b, nullptr, value);
        if (std::memcmp(a, b, kBlock) != 0 || a[9] != static_cast<uint8_t>(value)) ctx.Fail("SetByte09 run %d", run);
    }
}

DIFF_TEST(TrivialAccessors_DoNothing, 0x140fe0)
{
    auto nothing = ctx.Original<void(__thiscall *)(void *)>(0x140fe0);
    std::mt19937 rng(0x140fe0);
    for (int run = 0; run < 50; ++run) {
        alignas(4) unsigned char a[kBlock], b[kBlock];
        Fill(a, rng);
        std::memcpy(b, a, kBlock);
        nothing(a);
        acc::DoNothing(b, nullptr);
        ++ctx.cases;
        if (std::memcmp(a, b, kBlock) != 0) ctx.Fail("DoNothing changed the block");
        Fill(b, rng);
        std::memcpy(a, b, kBlock);
        nothing(a);
        if (std::memcmp(a, b, kBlock) != 0) ctx.Fail("the original changed the block");
    }
}

DIFF_TEST(TrivialAccessors_ReturnTrue, 0x144910)
{
    auto yes = ctx.Original<uint8_t(__thiscall *)(const void *)>(0x144910);
    auto yesArg = ctx.Original<uint8_t(__thiscall *)(const void *, uint32_t)>(0x148260);
    std::mt19937 rng(0x144910);
    for (int run = 0; run < 100; ++run) {
        alignas(4) unsigned char a[kBlock], b[kBlock];
        Fill(a, rng);
        std::memcpy(b, a, kBlock);
        uint32_t arg = static_cast<uint32_t>(rng());
        ++ctx.cases;
        if (yes(a) != 1 || !acc::ReturnTrue(b, nullptr)) ctx.Fail("ReturnTrue run %d", run);
        if (yesArg(a, arg) != 1 || !acc::ReturnTrueIgnoringArgument(b, nullptr, arg)) ctx.Fail("ReturnTrueIgnoringArgument run %d", run);
        if (std::memcmp(a, b, kBlock) != 0) ctx.Fail("block changed");
    }
}
