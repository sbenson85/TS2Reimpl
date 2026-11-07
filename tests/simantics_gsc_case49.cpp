// Differential tests for GenericSimCall (opcode 0x01, 0x58bc50) cases 49, 62, 66 and 68 (src/simantics/PrimGenericSimCallF.cpp): the
// two that move an object through the original's placement function 0x7e8c60 (49, 68) and the two with single-precision arithmetic
// (62, 66). Recording-fake world (tests/simantics_gsc_world.h). 0x7e8c60 (cdecl, seven stack words: object, block, position address,
// two floats, two words) is redirected to a logging stand-in that logs what it is handed (block bytes, position, floats).
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

float Bits2F(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

uint32_t __cdecl StubPlace(void *object, const uint32_t *block, const uint32_t *position, float f1, float f2, int32_t mode, int32_t last)
{
    Log(0x7e8c60u);
    gTrace.push_back(NormWord(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object))));
    for (int i = 0; i < 24; ++i) gTrace.push_back(block[i]);
    gTrace.push_back(*position);
    uint32_t b1, b2;
    std::memcpy(&b1, &f1, 4);
    std::memcpy(&b2, &f2, 4);
    gTrace.insert(gTrace.end(), {b1, b2, static_cast<uint32_t>(mode), static_cast<uint32_t>(last)});
    return Mix(gWorld->seed, 0x7e, *position, block[0]) % 3 == 0 ? 0xaabbcc00u : 0xaabbcc01u;
}

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(0, "VO", "b", "Iface66")}));
    {
        TypeSpec f;
        f.name = "Frame";
        f.slots = {S(12, "", "h"), S(26, "", "i{3,4,4,5,6,0x104,0x1ff}"), S(30, "V", "h{0,1,5,100,-1001,-1002,1001,1002,1000,-5}")};
        t.push_back(f);
    }
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("Entries", "People", "Objects", "Defs");
        t.push_back(c);
    }
    for (const char *name : {"People", "Defs"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Objects";
        x.slots = {Lookup(29, "V", "Obj", 10)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Obj";
        x.slots = {S(13, "V", "h{0,1,2,3,4,0x100}"),
                   S(42, "p", "v", "", [](Call &c) -> uint32_t {
                       uint32_t *block = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c.a[0]));
                       for (int i = 0; i < 24; ++i) block[i] = c.Rand(i + 1) % 7 == 0 ? 0u : c.Rand(i + 1);
                       return c.a[0];
                   }),
                   S(43, "", "i{0,1,2,3,0x3f800000}"), S(224, "V", "v"), S(222, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entries";
        x.slots = {Lookup(11, "V", "E62", 10)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "E62";
        x.slots = {S(53, "", "i{0,1,100,12345,-5,1000000,99999}"), S(54, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Iface66";
        x.slots = {S(10, "VV", "h{0,1,5,100,-3,1000,1001}"), S(11, "VVV", "v")};
        t.push_back(x);
    }
    return t;
}

void RunCase(difftest::Context &ctx, int which, const std::vector<std::vector<int32_t>> &pools)
{
    Setup(ctx, Types());
    CodePatch patchPlace(ctx, 0x7e8c60, reinterpret_cast<void *>(&StubPlace));
    const Summary s = RunSeeds(ctx, which, 8000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, pools);
    });
    Report(s);
    if (!s.byResult.size()) ctx.Fail("the worlds did not reach every outcome");
}

} // namespace

DIFF_TEST(simantics_gsc_case49, 0x58bc50)
{
    RunCase(ctx, 49, {{0, 1, 2, 3, 4, 0x100, -1}});
}

DIFF_TEST(simantics_gsc_case68, 0x58bc50)
{
    RunCase(ctx, 68, {});
}

DIFF_TEST(simantics_gsc_case62, 0x58bc50)
{
    RunCase(ctx, 62, {{0, 1, 2, 5}, {0, 0, 1, 2}, {0, 1, 5, 9999, 10000, -1, 0x7fff}, {0, 1, 9999, 10000, -1, 5000}, {0, 1, 50, 100, 200, -30, 0x7fff}});
}

DIFF_TEST(simantics_gsc_case66, 0x58bc50)
{
    RunCase(ctx, 66, {});
}
