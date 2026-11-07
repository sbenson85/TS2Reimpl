// Differential tests for nTSSG::cIDiffusionLightExtensionSub (src/sg/cDiffusionLightExtension.cpp), the secondary
// subobject of nTSSG::cDiffusionLightExtension (vtable 0xdb6bdc). Each function is entered through a vtordisp thunk
// that subtracts the dword at this - 4 to reach the full object, so every case builds one 0x300-byte buffer: the full
// object at offset 0x200, the subobject `this` at 0x100 (its displacement, 0x100 - 0x200, at 0x0fc), and random bytes
// everywhere else. The flags are randomly 0 or set, so both branches of each getter occur.
//
// The argument of the setters and copiers is a pointer: most cases point it at a separate 16-byte block, and a
// quarter point it into the buffer itself, so the copy order against the object's own fields is compared too.
//
// The original and the replacement run from the same starting bytes. The compared outcome is the return value (full
// eax, only al for the byte getter, the float's 32-bit pattern for the float getter), the whole buffer and the
// argument block.
#include "difftest.h"
#include "sg/cDiffusionLightExtension.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <random>

namespace {

using nTSSG::cIDiffusionLightExtensionSub;

constexpr uint32_t kBufSize = 0x300;
constexpr uint32_t kFull = 0x200;  // the full object F
constexpr uint32_t kSub = 0x100;   // the subobject `this`; the displacement at kSub - 4 is kSub - kFull
constexpr uint32_t kArgSize = 16;

// Flag bytes, as offsets from the full object (see the header).
constexpr int32_t kFlags[] = {-0x5c, -0x50, -0x48, -0x38, -0x36, -0x30, -0x28, -0x20, -0x18, -0x10};

struct State {
    uint8_t buf[kBufSize];
    uint8_t arg[kArgSize];
    int32_t aliasOff; // -1: the argument is `arg`; otherwise the argument points at buf + aliasOff
};

alignas(16) uint8_t gBuf[kBufSize];
alignas(16) uint8_t gArg[kArgSize];
State gA, gB;

void Load(const State &s)
{
    std::memcpy(gBuf, s.buf, sizeof(gBuf));
    std::memcpy(gArg, s.arg, sizeof(gArg));
}

void Store(State &s)
{
    std::memcpy(s.buf, gBuf, sizeof(gBuf));
    std::memcpy(s.arg, gArg, sizeof(gArg));
}

void Put32(uint8_t *p, uint32_t v)
{
    std::memcpy(p, &v, 4);
}

void Randomize(std::mt19937 &gen, State &s)
{
    for (auto &x : s.buf) x = static_cast<uint8_t>(gen());
    for (auto &x : s.arg) x = static_cast<uint8_t>(gen());
    for (int32_t off : kFlags) s.buf[kFull + off] = gen() % 3 == 0 ? 0 : static_cast<uint8_t>(gen());
    Put32(s.buf + kSub - 4, kSub - kFull);
    // A quarter of the cases alias the argument with the buffer: half of those over the fields (-0xa0..-0x40 from F),
    // so the copies overlap the fields the setters write, the rest anywhere in the buffer.
    if (gen() % 4 == 0) {
        s.aliasOff = gen() % 2 ? static_cast<int32_t>(kFull - 0xa0 + gen() % 0x60) : static_cast<int32_t>(gen() % (kBufSize - 12));
    } else {
        s.aliasOff = -1;
    }
}

// The argument pointer for a case. It only depends on the starting state, so both runs get the same address.
const void *ArgOf(const State &s)
{
    return s.aliasOff < 0 ? static_cast<const void *>(gArg) : static_cast<const void *>(gBuf + s.aliasOff);
}

cIDiffusionLightExtensionSub *Ours()
{
    return reinterpret_cast<cIDiffusionLightExtensionSub *>(gBuf + kSub);
}

void *SubPtr()
{
    return gBuf + kSub;
}

uint64_t FloatBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

using Run = std::function<uint64_t(bool original)>;

// Runs the original and the replacement from `start` and compares the outcome. `run` returns the result widened to
// 64 bits, with only the defined bits kept.
void Check(difftest::Context &ctx, const char *name, const State &start, const Run &run)
{
    Load(start);
    uint64_t want = run(true);
    Store(gA);
    Load(start);
    uint64_t got = run(false);
    Store(gB);
    if (want != got) {
        ctx.Fail("%s: result original %llx ours %llx", name, static_cast<unsigned long long>(want),
                 static_cast<unsigned long long>(got));
    }
    if (std::memcmp(gA.buf, gB.buf, sizeof(gA.buf)) != 0) ctx.Fail("%s: object bytes differ", name);
    if (std::memcmp(gA.arg, gB.arg, sizeof(gA.arg)) != 0) ctx.Fail("%s: argument block differs", name);
    ++ctx.cases;
}

} // namespace

// 0x2ea27b (`ret`): clears the flags. Nothing else changes.
DIFF_TEST(sg_diffusion_light_reset_flags, 0x2ea27b)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x2ea27b);
    std::mt19937 gen(0x2ea27b);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        Check(ctx, "sg_diffusion_light_reset_flags", s, [&](bool original) {
            if (original) {
                orig(SubPtr());
            } else {
                Ours()->ResetFlags();
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea948 (`ret 4`): copies the 8-byte value at the argument to P1 (-0x64) and sets its flag (-0x5c).
DIFF_TEST(sg_diffusion_light_set_pair_p1, 0x2ea948)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(0x2ea948);
    std::mt19937 gen(0x2ea948);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        const void *arg = ArgOf(s);
        Check(ctx, "sg_diffusion_light_set_pair_p1", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), arg);
            } else {
                Ours()->SetPairP1(arg);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea842 (`ret 4`): stores the 32-bit argument (float bits) in F3 (-0x4c) and sets its flag (-0x48).
DIFF_TEST(sg_diffusion_light_set_float_f3, 0x2ea842)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2ea842);
    std::mt19937 gen(0x2ea842);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t value = static_cast<uint32_t>(gen());
        Check(ctx, "sg_diffusion_light_set_float_f3", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), value);
            } else {
                Ours()->SetFloatF3(value);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea749 (`ret 4`): copies 12 bytes from the argument to V4 (-0x44: 8 bytes, -0x3c: the dword at +8) and sets the
// flag at -0x38. The original reads the head before the stores and the tail after them.
DIFF_TEST(sg_diffusion_light_set_vec3_v4, 0x2ea749)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(0x2ea749);
    std::mt19937 gen(0x2ea749);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        const void *arg = ArgOf(s);
        Check(ctx, "sg_diffusion_light_set_vec3_v4", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), arg);
            } else {
                Ours()->SetVec3V4(arg);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea8ba (`ret 4`): stores the low byte of the argument in B5 (-0x37) and sets its flag (-0x36). Only the low byte
// of the stack argument is read; the random upper bits must not matter.
DIFF_TEST(sg_diffusion_light_set_byte_b5, 0x2ea8ba)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2ea8ba);
    std::mt19937 gen(0x2ea8ba);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t value = static_cast<uint32_t>(gen());
        Check(ctx, "sg_diffusion_light_set_byte_b5", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), value);
            } else {
                Ours()->SetByteB5(value);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea966 (`ret 4`): stores the float bits in F8 (-0x24) and sets its flag (-0x20).
DIFF_TEST(sg_diffusion_light_set_float_f8, 0x2ea966)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2ea966);
    std::mt19937 gen(0x2ea966);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t value = static_cast<uint32_t>(gen());
        Check(ctx, "sg_diffusion_light_set_float_f8", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), value);
            } else {
                Ours()->SetFloatF8(value);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea87e (`ret 4`): stores the float bits in F10 (-0x14) and sets its flag (-0x10).
DIFF_TEST(sg_diffusion_light_set_float_f10, 0x2ea87e)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2ea87e);
    std::mt19937 gen(0x2ea87e);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t value = static_cast<uint32_t>(gen());
        Check(ctx, "sg_diffusion_light_set_float_f10", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), value);
            } else {
                Ours()->SetFloatF10(value);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea926 (`ret 4`): copies the 8-byte argument to the default D11 (-0x9c, -0x98). No flag.
DIFF_TEST(sg_diffusion_light_set_pair_d11, 0x2ea926)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(0x2ea926);
    std::mt19937 gen(0x2ea926);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        const void *arg = ArgOf(s);
        Check(ctx, "sg_diffusion_light_set_pair_d11", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), arg);
            } else {
                Ours()->SetPairD11(arg);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea8de (`ret 4`): copies the 8-byte argument to the default D12 (-0x94, -0x90). No flag.
DIFF_TEST(sg_diffusion_light_set_pair_d12, 0x2ea8de)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(0x2ea8de);
    std::mt19937 gen(0x2ea8de);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        const void *arg = ArgOf(s);
        Check(ctx, "sg_diffusion_light_set_pair_d12", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), arg);
            } else {
                Ours()->SetPairD12(arg);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea728 (`ret 4`): copies 12 bytes from the argument to the default D14 (-0x88, -0x80). No flag.
DIFF_TEST(sg_diffusion_light_set_vec3_d14, 0x2ea728)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(0x2ea728);
    std::mt19937 gen(0x2ea728);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        const void *arg = ArgOf(s);
        Check(ctx, "sg_diffusion_light_set_vec3_d14", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), arg);
            } else {
                Ours()->SetVec3D14(arg);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea8a2 (`ret 4`): stores the low byte of the argument in the default D15 (-0x7c). No flag.
DIFF_TEST(sg_diffusion_light_set_byte_d15, 0x2ea8a2)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2ea8a2);
    std::mt19937 gen(0x2ea8a2);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t value = static_cast<uint32_t>(gen());
        Check(ctx, "sg_diffusion_light_set_byte_d15", s, [&](bool original) {
            if (original) {
                orig(SubPtr(), value);
            } else {
                Ours()->SetByteD15(value);
            }
            return uint64_t(0);
        });
    }
}

// 0x2ea531 (`ret`): the address of P1 (-0x64) when its flag (-0x5c) is set, else of D11 (-0x9c).
DIFF_TEST(sg_diffusion_light_get_pointer_p1_or_d11, 0x2ea531)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x2ea531);
    std::mt19937 gen(0x2ea531);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        Check(ctx, "sg_diffusion_light_get_pointer_p1_or_d11", s, [&](bool original) {
            void *result = original ? orig(SubPtr()) : Ours()->GetPointerP1OrD11();
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(result));
        });
    }
}

// 0x2ea4ae (`ret`): F3 (-0x4c) when its flag (-0x48) is set, else the default float at -0x8c. Compared as bits.
DIFF_TEST(sg_diffusion_light_get_float_f3, 0x2ea4ae)
{
    auto orig = ctx.Original<float(__thiscall *)(void *)>(0x2ea4ae);
    std::mt19937 gen(0x2ea4ae);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        Check(ctx, "sg_diffusion_light_get_float_f3", s, [&](bool original) {
            float result = original ? orig(SubPtr()) : Ours()->GetFloatF3OrDefault();
            return FloatBits(result);
        });
    }
}

// 0x2ea4ee (`ret`): B5 (-0x37) when its flag (-0x36) is set, else the default byte at -0x7c. Only al is defined.
DIFF_TEST(sg_diffusion_light_get_byte_b5, 0x2ea4ee)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(0x2ea4ee);
    std::mt19937 gen(0x2ea4ee);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        Check(ctx, "sg_diffusion_light_get_byte_b5", s, [&](bool original) {
            uint8_t result = original ? orig(SubPtr()) : Ours()->GetByteB5OrD15();
            return static_cast<uint64_t>(result);
        });
    }
}

// 0x2ea436 (`ret`): the address of V4 (-0x44) when its flag (-0x38) is set, else of the default D14 (-0x88).
DIFF_TEST(sg_diffusion_light_get_vec3_v4, 0x2ea436)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x2ea436);
    std::mt19937 gen(0x2ea436);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        Check(ctx, "sg_diffusion_light_get_vec3_v4", s, [&](bool original) {
            void *result = original ? orig(SubPtr()) : Ours()->GetVec3V4OrD14();
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(result));
        });
    }
}
