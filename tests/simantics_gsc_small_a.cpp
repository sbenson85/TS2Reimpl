// Differential tests for the short GenericSimCall (opcode 0x01, 0x58bc50) cases of src/simantics/PrimGenericSimCallA.cpp / B / E: calls
// on the object, its context holder and the objects those return. One shared recording-fake world (tests/simantics_gsc_world.h), one
// test per case: 1, 3, 2, 5, 10, 11, 12, 13, 14, 18, 21, 22, 23, 25, 27, 33, 34, 35, 36, 38, 63, 64, 65, 71, 73, 79.
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(0, "VO", "b", "SIface"), S(63, "", "o:Owner63!"), S(260, "V", "i"), S(350, "", "v")}));
    {
        TypeSpec f;
        f.name = "Frame";
        f.slots = {S(12, "", "h")};
        t.push_back(f);
    }
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("Entries", "People", "Objects", "Defs");
        c.slots.push_back(S(3, "", "o:Target3!"));
        c.slots.push_back(S(6, "", "o:Lookup6!"));
        c.slots.push_back(S(7, "", "o:List7!"));
        c.slots.push_back(S(29, "", "o:Target29?"));
        t.push_back(c);
    }
    {
        TypeSpec x;
        x.name = "Defs";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entries";
        x.slots = {Lookup(11, "V", "Entry5", 15), Lookup(10, "V", "Found36", 20)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entry5";
        x.slots = {S(60, "", "i{0,1,5,-7,100000}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Found36";
        x.slots = {S(41, "", "i{0,1,0xffff,0x12345,-1}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "People";
        x.slots = {Lookup(16, "VV", "Person", 20), S(22, "VV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Person";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Objects";
        x.slots = {Lookup(29, "V", "Obj", 12), S(40, "V", "v")};
        t.push_back(x);
    }
    {
        // the stack object / any object of the object table
        TypeSpec x;
        x.name = "Obj";
        x.count = 2;
        x.slots = {S(0, "VO", "b", "OIface"), S(64, "", "o:Owner2!"), S(101, "p", "v", "", [](Call &c) -> uint32_t {
                       // two signed bytes: the callee fills no more than these in the original's local
                       uint8_t *pair = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(c.a[0]));
                       pair[0] = static_cast<uint8_t>(c.Rand(1));
                       pair[1] = static_cast<uint8_t>(c.Rand(2));
                       return 0;
                   }),
                   S(221, "", "v"), S(193, "V", "v"), S(201, "VVVV", "v"), S(121, "", "b"), S(88, "VVV", "b")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "OIface";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "SIface";
        x.slots = {S(132, "", "o:Helper!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Helper";
        x.slots = {S(29, "", "b"), S(65, "V", "v"), S(72, "VV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Owner2";
        x.slots = {S(3, "V", "o:Block!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Block";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Owner63";
        x.slots = {S(67, "V", "v"), S(68, "V", "v"), S(50, "V", "v"), Lookup(29, "V", "Obj", 12)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Target3";
        x.slots = {S(44, "VV", "v"), S(16, "VV", "v"), S(20, "VV", "v"), S(25, "V", "v"), S(24, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Target29";
        x.slots = {S(9, "VVV", "v"), S(8, "VV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Lookup6";
        x.slots = {Lookup(57, "V", "Rect", 20), S(13, "", "o:List13!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Rect";
        x.count = 3;
        x.slots = {S(12, "p", "v", "", [](Call &c) -> uint32_t {
            int32_t *r = reinterpret_cast<int32_t *>(static_cast<uintptr_t>(c.a[0]));
            for (int i = 0; i < 4; ++i) r[i] = static_cast<int32_t>(c.Rand(i + 1) % 201) - 100;
            return 0;
        })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "List13";
        x.slots = {Lookup(18, "V", "Found35", 20)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Found35";
        x.slots = {S(4, "", "o:Inner35!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Inner35";
        x.slots = {S(8, "", "h{0,1,0x7fff,-1,0x100}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "List7";
        // a walk over 0..3 items: slot 19 starts it, slot 20 steps on (a stable answer would never end)
        x.slots = {S(19, "", "o:Item?", "", [](Call &c) -> uint32_t {
                       gWorld->user[1] = 0;
                       if (c.Rand(1) % 4 == 0) return 0;
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Item", static_cast<int>(c.Rand(2) % 2))));
                   }),
                   S(20, "", "o:Item?", "", [](Call &c) -> uint32_t {
                       const uint32_t step = ++gWorld->user[1];
                       if (step >= c.Rand(3) % 4) return 0;
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Item", static_cast<int>(c.Rand(4 + step) % 2))));
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Item";
        x.count = 2;
        x.slots = {S(4, "", "o:Inner27!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Inner27";
        x.slots = {S(8, "", "i{0,1,1,2}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "SvcB";
        x.slots = {S(23, "", "i{0xa,0,1,0xa,5}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Cached211";
        x.slots = {S(30, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Attr64";
        x.slots = {S(41, "VVVVVV", "v")};
        t.push_back(x);
    }
    return t;
}

void RunSmall(difftest::Context &ctx, int which, const std::vector<std::vector<int32_t>> &pools)
{
    Setup(ctx, Types());
    uint32_t *const singletonB = ctx.image.At<uint32_t>(0x10cc28c);
    uint32_t *const cached211 = ctx.image.At<uint32_t>(0x10cc458);
    const uint32_t saved[2] = {*singletonB, *cached211};
    *singletonB = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("SvcB")));
    *cached211 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Cached211")));
    const Summary s = RunSeeds(ctx, which, 4000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr, "Attr64");
        RandomArgs(seed, pools);
    });
    *singletonB = saved[0];
    *cached211 = saved[1];
    Report(s);
    if (!s.byResult.size()) ctx.Fail("the worlds did not reach every outcome");
}

const std::vector<int32_t> kIds = {0, 1, 2, 5, 0x100, -1, 0x7fff};

} // namespace

#define SMALL_CASE(N, ...)                         \
    DIFF_TEST(simantics_gsc_small_##N, 0x58bc50)   \
    {                                              \
        RunSmall(ctx, N, {__VA_ARGS__});           \
    }

SMALL_CASE(1, kIds)
SMALL_CASE(3, kIds)
SMALL_CASE(2, kIds)
SMALL_CASE(5, kIds)
SMALL_CASE(10, kIds)
SMALL_CASE(11, kIds)
SMALL_CASE(12, kIds)
SMALL_CASE(13, kIds)
SMALL_CASE(14, kIds)
SMALL_CASE(18, {0, 1, 2, 3, -1, 0x7fff})
SMALL_CASE(21, kIds, kIds)
SMALL_CASE(22, kIds, kIds)
SMALL_CASE(23, kIds, kIds)
SMALL_CASE(25, kIds)
SMALL_CASE(27, kIds)
SMALL_CASE(33, kIds)
SMALL_CASE(34, kIds, kIds)
SMALL_CASE(35, kIds)
SMALL_CASE(36, kIds)
SMALL_CASE(38, kIds)
SMALL_CASE(63, kIds)
SMALL_CASE(64, kIds, kIds, kIds, kIds, kIds, {0, 1, 2, 0x8000, 0xffff})
SMALL_CASE(65, kIds)
SMALL_CASE(71, kIds, kIds, kIds)
SMALL_CASE(73, kIds)
SMALL_CASE(79, kIds)
