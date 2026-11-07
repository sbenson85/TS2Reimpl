// Differential tests for the thunked slots of nTSSG::cLocomotable's secondary subobjects (src/sg/cLocomotable.cpp).
// Each original thunk is `sub ecx,[ecx-4]; sub ecx,N; jmp leaf`, and the leaf is a field read (or an address
// computation), so the thunks read memory and write nothing. Each case builds one random host buffer, places `this`
// inside it, and writes a random vtordisp (0 to 0x3f) at this - 4. The original runs on the buffer, its result and
// the buffer are recorded, the buffer is restored, and the replacement runs on the same bytes. Compared per run:
// the result (full eax for dwords and pointers, st0 bits for floats, al for the byte slot) and the buffer afterwards.
#include "difftest.h"
#include "sg/cLocomotable.h"

#include <cstring>
#include <random>

namespace {

using nTSSG::cLocomotableSubobject;

constexpr uint32_t kThisOffset = 0x500; // `this` inside the buffer; the largest read reaches 0x500 - 0x3f - 0x2e0 - 0x78
struct Host {
    uint8_t bytes[0x800];
};

Host gHost, gStart, gAfter;

void *ThisPtr()
{
    return gHost.bytes + kThisOffset;
}

// Random buffer, and a vtordisp at this - 4 that is 0 to 0x3f (the adjusted pointer stays inside the buffer).
void Build(std::mt19937 &gen)
{
    for (auto &b : gHost.bytes) b = static_cast<uint8_t>(gen());
    const uint32_t displacement = gen() % 0x40;
    std::memcpy(gHost.bytes + kThisOffset - 4, &displacement, sizeof(displacement));
}

uint64_t Pack(uint32_t v)
{
    return v;
}

uint64_t Pack(uint8_t v)
{
    return v;
}

uint64_t Pack(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

uint64_t Pack(void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

// Runs the original and the replacement on the same buffer bytes and compares the packed result and the buffer.
template <typename Original, typename Ours>
void Run(difftest::Context &ctx, const char *name, uint32_t iteration, Original original, Ours ours)
{
    gStart = gHost;
    const uint64_t a = original();
    gAfter = gHost;
    gHost = gStart;
    const uint64_t b = ours();
    ++ctx.cases;
    if (a != b || std::memcmp(&gAfter, &gHost, sizeof(Host)) != 0) {
        ctx.Fail("%s iteration %u: original %llx, ours %llx, buffer %s", name, iteration, static_cast<unsigned long long>(a),
                 static_cast<unsigned long long>(b), std::memcmp(&gAfter, &gHost, sizeof(Host)) ? "differs" : "same");
    }
}

// Runs one thunk against its replacement: `Ret` is the original's return type, `ours` calls the replacement on
// ThisPtr().
template <typename Ret, typename Ours>
void RunSlot(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t seed, Ours ours)
{
    auto original = ctx.Original<Ret(__thiscall *)(void *)>(rva);
    std::mt19937 gen(seed);
    for (uint32_t i = 0; i < 2000; ++i) {
        Build(gen);
        Run(ctx, name, i, [&] { return Pack(original(ThisPtr())); }, [&] { return Pack(ours()); });
    }
}

cLocomotableSubobject *Self()
{
    return reinterpret_cast<cLocomotableSubobject *>(ThisPtr());
}

} // namespace

// 0x280d35 (14 bytes, N 0x2e0): dword at [adjusted - 0x50].
DIFF_TEST(sg_locomotable_vf860_55, 0x280d35)
{
    RunSlot<uint32_t>(ctx, "Vf860_55", 0x280d35, 0x2001, [] { return Self()->Vf860_55(); });
}

// 0x28270c (14 bytes, N 0x1ac): dword at [adjusted - 0x74].
DIFF_TEST(sg_locomotable_vf872_25, 0x28270c)
{
    RunSlot<uint32_t>(ctx, "Vf872_25", 0x28270c, 0x2002, [] { return Self()->Vf872_25(); });
}

// 0x282a1c (14 bytes, N 0x1ac): dword at [adjusted - 0x78].
DIFF_TEST(sg_locomotable_vf872_24, 0x282a1c)
{
    RunSlot<uint32_t>(ctx, "Vf872_24", 0x282a1c, 0x2003, [] { return Self()->Vf872_24(); });
}

// 0x2837ec (14 bytes, N 0x2e0): dword at [adjusted - 0x6c].
DIFF_TEST(sg_locomotable_vf860_20, 0x2837ec)
{
    RunSlot<uint32_t>(ctx, "Vf860_20", 0x2837ec, 0x2004, [] { return Self()->Vf860_20(); });
}

// 0x2837fa (14 bytes, N 0x218): float at [adjusted - 0x4c], returned in st0.
DIFF_TEST(sg_locomotable_vf860_126, 0x2837fa)
{
    RunSlot<float>(ctx, "Vf860_126", 0x2837fa, 0x2005, [] { return Self()->Vf860_126(); });
}

// 0x283808 (14 bytes, N 0x1ac): float at [adjusted - 0x70], returned in st0.
DIFF_TEST(sg_locomotable_vf872_26, 0x283808)
{
    RunSlot<float>(ctx, "Vf872_26", 0x283808, 0x2006, [] { return Self()->Vf872_26(); });
}

// 0x2839d3 (14 bytes, N 0x2e0): dword at [adjusted - 0x58].
DIFF_TEST(sg_locomotable_vf860_39, 0x2839d3)
{
    RunSlot<uint32_t>(ctx, "Vf860_39", 0x2839d3, 0x2007, [] { return Self()->Vf860_39(); });
}

// 0x28651d (14 bytes, N 0x218): byte at [adjusted - 0x2d] into al only. Upper bits of eax are not compared.
DIFF_TEST(sg_locomotable_vf860_134, 0x28651d)
{
    RunSlot<uint8_t>(ctx, "Vf860_134", 0x28651d, 0x2008, [] { return Self()->Vf860_134(); });
}

// 0x286fd9 (14 bytes, N 0x2e0): dword at [adjusted - 0x54].
DIFF_TEST(sg_locomotable_vf860_49, 0x286fd9)
{
    RunSlot<uint32_t>(ctx, "Vf860_49", 0x286fd9, 0x2009, [] { return Self()->Vf860_49(); });
}

// 0x286fe7 (14 bytes, N 0x2e0): dword at [adjusted - 0x5c].
DIFF_TEST(sg_locomotable_vf860_37, 0x286fe7)
{
    RunSlot<uint32_t>(ctx, "Vf860_37", 0x286fe7, 0x200a, [] { return Self()->Vf860_37(); });
}

// 0x2874e8 (14 bytes, N 0x2e0): dword at [adjusted - 0x64].
DIFF_TEST(sg_locomotable_vf860_33, 0x2874e8)
{
    RunSlot<uint32_t>(ctx, "Vf860_33", 0x2874e8, 0x200b, [] { return Self()->Vf860_33(); });
}

// 0x2874f6 (14 bytes, N 0x2e0): dword at [adjusted - 0x30].
DIFF_TEST(sg_locomotable_vf860_68, 0x2874f6)
{
    RunSlot<uint32_t>(ctx, "Vf860_68", 0x2874f6, 0x200c, [] { return Self()->Vf860_68(); });
}

// 0x287504 (14 bytes, N 0x2e0): dword at [adjusted - 0x2c].
DIFF_TEST(sg_locomotable_vf860_76, 0x287504)
{
    RunSlot<uint32_t>(ctx, "Vf860_76", 0x287504, 0x200d, [] { return Self()->Vf860_76(); });
}

// 0x287683 (14 bytes, N 0x2e0): dword at [adjusted - 0x60].
DIFF_TEST(sg_locomotable_vf860_35, 0x287683)
{
    RunSlot<uint32_t>(ctx, "Vf860_35", 0x287683, 0x200e, [] { return Self()->Vf860_35(); });
}

// 0x287691 (14 bytes, N 0x1ac): lea eax,[adjusted - 0x6c]; the address itself, not a read.
DIFF_TEST(sg_locomotable_vf872_27, 0x287691)
{
    RunSlot<void *>(ctx, "Vf872_27", 0x287691, 0x200f, [] { return Self()->Vf872_27(); });
}
