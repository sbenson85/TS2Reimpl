// Differential tests for nTSSG::cTSSGSystem leaf slots (src/sg/cTSSGSystem.cpp). None of these calls out, so the
// original runs on a plain buffer. Each case starts from random object bytes (the whole 0x400-byte object, so the
// fields and any neighbouring bytes are random); the original and the replacement each run from that state. The
// compared outcome is the return value (full eax, or only al for byte getters), and the object bytes afterwards.
#include "difftest.h"
#include "sg/cTSSGSystem.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <random>

namespace {

using nTSSG::cTSSGSystemPrimary;
using nTSSG::cTSSGSystemServiceSub;

constexpr uint32_t kObjSize = 0x400;
constexpr uint32_t kCases = 2000;

struct State {
    uint8_t obj[kObjSize];
};

alignas(16) uint8_t gObj[kObjSize];
State gA, gB;

void Load(const State &s)
{
    std::memcpy(gObj, s.obj, sizeof(gObj));
}

void Store(State &s)
{
    std::memcpy(s.obj, gObj, sizeof(gObj));
}

void Randomize(std::mt19937 &gen, State &s)
{
    for (auto &x : s.obj) x = static_cast<uint8_t>(gen());
}

void Put32(uint8_t *p, uint32_t v)
{
    std::memcpy(p, &v, 4);
}

using Run = std::function<uint64_t(bool original)>;

// Runs the original and the replacement from `start` and compares the outcome: the result (as passed in `run`) and the
// object bytes.
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
    if (std::memcmp(gA.obj, gB.obj, sizeof(gA.obj)) != 0) ctx.Fail("%s: object bytes differ", name);
    ++ctx.cases;
}

// Dword getter at `off` (eax compared in full).
void TestGetDword(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t off,
                  uint32_t (cTSSGSystemPrimary::*ours)())
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(rva);
    std::mt19937 gen(rva);
    for (uint32_t i = 0; i < kCases; ++i) {
        State s;
        Randomize(gen, s);
        if (i % 4 == 0) Put32(s.obj + off, gen() % 2 ? 0u : 0xffffffffu);
        Check(ctx, name, s, [&](bool useOrig) -> uint64_t {
            if (useOrig) return orig(gObj);
            return (reinterpret_cast<cTSSGSystemPrimary *>(gObj)->*ours)();
        });
    }
}

// Byte getter at `off`: only al is compared, as the caller tests al. The upper bits of eax are left as the caller had
// them, so the full eax is not defined.
void TestGetByte(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t off,
                 uint8_t (cTSSGSystemPrimary::*ours)())
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(rva);
    std::mt19937 gen(rva);
    for (uint32_t i = 0; i < kCases; ++i) {
        State s;
        Randomize(gen, s);
        if (i % 4 == 0) s.obj[off] = gen() % 2 ? 0u : 1u;
        Check(ctx, name, s, [&](bool useOrig) -> uint64_t {
            if (useOrig) return static_cast<uint8_t>(orig(gObj));
            return static_cast<uint8_t>((reinterpret_cast<cTSSGSystemPrimary *>(gObj)->*ours)());
        });
    }
}

// Address getter (lea): the result is the address of the field, the same in both runs since they share gObj.
void TestAddress(difftest::Context &ctx, const char *name, uint32_t rva, void *(cTSSGSystemPrimary::*ours)())
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(rva);
    std::mt19937 gen(rva);
    for (uint32_t i = 0; i < kCases; ++i) {
        State s;
        Randomize(gen, s);
        Check(ctx, name, s, [&](bool useOrig) -> uint64_t {
            void *p = useOrig ? orig(gObj) : (reinterpret_cast<cTSSGSystemPrimary *>(gObj)->*ours)();
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// Setter of a dword at `off` with one argument (ret 4).
void TestSetDword(difftest::Context &ctx, const char *name, uint32_t rva, void (cTSSGSystemPrimary::*ours)(uint32_t))
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(rva);
    std::mt19937 gen(rva);
    const uint32_t edges[] = {0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    for (uint32_t i = 0; i < kCases; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t value = i < std::size(edges) ? edges[i] : gen();
        Check(ctx, name, s, [&](bool useOrig) -> uint64_t {
            if (useOrig) {
                orig(gObj, value);
            } else {
                (reinterpret_cast<cTSSGSystemPrimary *>(gObj)->*ours)(value);
            }
            return 0;
        });
    }
}

// Constant getter: the full eax.
void TestConst(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t (cTSSGSystemServiceSub::*ours)())
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(rva);
    std::mt19937 gen(rva);
    for (uint32_t i = 0; i < 200; ++i) {
        State s;
        Randomize(gen, s);
        Check(ctx, name, s, [&](bool useOrig) -> uint64_t {
            if (useOrig) return orig(gObj);
            return (reinterpret_cast<cTSSGSystemServiceSub *>(gObj)->*ours)();
        });
    }
}

} // namespace

// 0x24ad20 (7 bytes): dword +0x2c4.
DIFF_TEST(sg_tssg_system_get_dword_2c4, 0x24ad20)
{
    TestGetDword(ctx, "sg_tssg_system_get_dword_2c4", 0x24ad20, 0x2c4, &cTSSGSystemPrimary::GetFieldDword2C4);
}

// 0x254db0 (13 bytes, ret 4): stores the argument at dword +0x2c4.
DIFF_TEST(sg_tssg_system_set_dword_2c4, 0x254db0)
{
    TestSetDword(ctx, "sg_tssg_system_set_dword_2c4", 0x254db0, &cTSSGSystemPrimary::SetFieldDword2C4);
}

// 0x255690 (8 bytes): byte +0x3a8 = 1.
DIFF_TEST(sg_tssg_system_set_flag_3a8, 0x255690)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0x255690);
    std::mt19937 gen(0x255690);
    for (uint32_t i = 0; i < kCases; ++i) {
        State s;
        Randomize(gen, s);
        if (i % 3 == 0) s.obj[0x3a8] = 1;
        Check(ctx, "sg_tssg_system_set_flag_3a8", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) {
                orig(gObj);
            } else {
                reinterpret_cast<cTSSGSystemPrimary *>(gObj)->SetFlag3A8();
            }
            return 0;
        });
    }
}

// 0x254670 (7 bytes): dword +0x3a4.
DIFF_TEST(sg_tssg_system_get_dword_3a4, 0x254670)
{
    TestGetDword(ctx, "sg_tssg_system_get_dword_3a4", 0x254670, 0x3a4, &cTSSGSystemPrimary::GetFieldDword3A4);
}

// 0x24b260 (7 bytes): byte +0x162 in al.
DIFF_TEST(sg_tssg_system_get_byte_162, 0x24b260)
{
    TestGetByte(ctx, "sg_tssg_system_get_byte_162", 0x24b260, 0x162, &cTSSGSystemPrimary::GetFieldByte162);
}

// 0x24b200 (7 bytes): byte +0x18e in al.
DIFF_TEST(sg_tssg_system_get_byte_18e, 0x24b200)
{
    TestGetByte(ctx, "sg_tssg_system_get_byte_18e", 0x24b200, 0x18e, &cTSSGSystemPrimary::GetFieldByte18E);
}

// 0x255000 (7 bytes): byte +0x18a in al.
DIFF_TEST(sg_tssg_system_get_byte_18a, 0x255000)
{
    TestGetByte(ctx, "sg_tssg_system_get_byte_18a", 0x255000, 0x18a, &cTSSGSystemPrimary::GetFieldByte18A);
}

// 0x254fe0 (7 bytes): byte +0x18b in al.
DIFF_TEST(sg_tssg_system_get_byte_18b, 0x254fe0)
{
    TestGetByte(ctx, "sg_tssg_system_get_byte_18b", 0x254fe0, 0x18b, &cTSSGSystemPrimary::GetFieldByte18B);
}

// 0x254ff0 (7 bytes): byte +0x18c in al.
DIFF_TEST(sg_tssg_system_get_byte_18c, 0x254ff0)
{
    TestGetByte(ctx, "sg_tssg_system_get_byte_18c", 0x254ff0, 0x18c, &cTSSGSystemPrimary::GetFieldByte18C);
}

// 0x247fb0 / 0x247fa0 / 0x247fc0 (7 bytes each): addresses object + 0x374 / 0x380 / 0x38c.
DIFF_TEST(sg_tssg_system_address_374, 0x247fb0)
{
    TestAddress(ctx, "sg_tssg_system_address_374", 0x247fb0, &cTSSGSystemPrimary::AddressOf374);
}

DIFF_TEST(sg_tssg_system_address_380, 0x247fa0)
{
    TestAddress(ctx, "sg_tssg_system_address_380", 0x247fa0, &cTSSGSystemPrimary::AddressOf380);
}

DIFF_TEST(sg_tssg_system_address_38c, 0x247fc0)
{
    TestAddress(ctx, "sg_tssg_system_address_38c", 0x247fc0, &cTSSGSystemPrimary::AddressOf38C);
}

// 0x250510 (24 bytes, ret 4): dword array at +0xc4 (nine entries), zero from index 9 up. Indices are signed: every
// negative value reads [this + index * 4 + 0xc4] (the address wraps modulo 2^32). The test keeps reads inside the
// object for the negative range (-49 gives offset 0), and covers the wrap at 0x80000000 (reads +0xc4 itself).
DIFF_TEST(sg_tssg_system_get_dword_array_c4, 0x250510)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x250510);
    std::mt19937 gen(0x250510);
    const int32_t edges[] = {0, 1, 8, 9, 10, -1, -49, -48, 0x7fffffff, INT32_MIN, 0x40000000};
    for (uint32_t i = 0; i < kCases; ++i) {
        State s;
        Randomize(gen, s);
        // Random draws stay in [-49, 8] (reads inside the object) or in [9, 0x7fffffff] (zero branch). Other negative
        // values would read outside the object, so they are only used through the edge list above (INT32_MIN wraps to
        // +0xc4 itself).
        int32_t index;
        if (i < std::size(edges)) {
            index = edges[i];
        } else if (i % 2) {
            index = static_cast<int32_t>(gen() % 58) - 49;
        } else {
            index = static_cast<int32_t>(9u + gen() % 0x7ffffff7u);
        }
        Check(ctx, "sg_tssg_system_get_dword_array_c4", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) return orig(gObj, static_cast<uint32_t>(index));
            return reinterpret_cast<cTSSGSystemPrimary *>(gObj)->GetDwordArrayC4(index);
        });
    }
}

// 0x24a950 / 0x24a960 (6 bytes each): constants, on cIGZSystemService (this = object + 0xc).
DIFF_TEST(sg_tssg_system_service_id, 0x24a950)
{
    TestConst(ctx, "sg_tssg_system_service_id", 0x24a950, &cTSSGSystemServiceSub::GetServiceID);
}

DIFF_TEST(sg_tssg_system_service_priority, 0x24a960)
{
    TestConst(ctx, "sg_tssg_system_service_priority", 0x24a960, &cTSSGSystemServiceSub::GetServicePriority);
}
