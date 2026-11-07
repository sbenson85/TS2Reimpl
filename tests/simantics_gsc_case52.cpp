// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 52 (src/simantics/PrimGenericSimCallL.cpp, Case52): the nearer
// of the two tiles in front of an object. Recording-fake world (tests/simantics_gsc_world.h): the tile service, its grid and level,
// the owner (position), the object found on a tile and the cached singleton are fakes; the box maths (0x7dccd0, 0x1a6c40, 0x7eed90,
// 0x7eec00) runs as original code on both sides, the world-dependent 0x7deff0 (object on a tile) is a logging stand-in.
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

// 0x7deff0 (cdecl, two stack words): the object standing on a tile (3 ints).
uint32_t __cdecl StubObjectOnTile(const int32_t *tile, int32_t mode)
{
    Log(0x7deff0u);
    gTrace.insert(gTrace.end(), {static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]), static_cast<uint32_t>(tile[2]), static_cast<uint32_t>(mode)});
    const uint32_t r = Mix(gWorld->seed, static_cast<uint32_t>(tile[0]), static_cast<uint32_t>(tile[1]), 0x7d);
    if (r % 3 == 0) return 0;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Standing", static_cast<int>((r >> 8) % 2))));
}

float PickCoord(uint32_t r)
{
    static const float pool[] = {0.0f, 1.5f, -2.25f, 3.0f, 7.75f, 100.5f, 0.5f, -1.0f, 2.0f, 4.5f, 10.25f, -3.5f};
    return pool[r % 12];
}

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(0, "VO", "b", "Iface")}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable");
        t.push_back(c);
    }
    for (const char *name : {"EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable", "SvcA"}) {
        TypeSpec x;
        x.name = name;
        if (std::string(name) == "SvcA") x.slots = {S(26, "", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Iface";
        x.slots = {S(8, "", "o:Owner!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Owner";
        x.slots = {S(42, "p", "v", "", [](Call &c) -> uint32_t {
                       float *out = reinterpret_cast<float *>(static_cast<uintptr_t>(c.a[0]));
                       for (int i = 0; i < 3; ++i) out[i] = PickCoord(c.Rand(i + 1));
                       return c.a[0];
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "TileSvc";
        x.slots = {S(39, "", "o:Grid!"), S(71, "N", "b")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Grid";
        x.slots = {S(40, "V", "o:Level!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Level";
        x.slots = {S(19, "NNBVV", "b", "", [](Call &c) -> uint32_t {
                       // fills the box (min x/y/z, max x/y/z) around the points, sometimes far away
                       float *box = reinterpret_cast<float *>(static_cast<uintptr_t>(c.a[2]));
                       for (int i = 0; i < 6; ++i) box[i] = PickCoord(c.Rand(10 + i));
                       return c.Rand(99) % 4 == 0 ? 0xaa00u : (0xbb00u | 1u);
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Standing";
        x.count = 2;
        x.slots = {S(47, "", "h{0,1,2,0x7fff,-1,0x100,0x8000}")};
        t.push_back(x);
    }
    return t;
}

} // namespace

DIFF_TEST(simantics_gsc_case52, 0x58bc50)
{
    Setup(ctx, Types());
    CodePatch patchTile(ctx, 0x7deff0, reinterpret_cast<void *>(&StubObjectOnTile));
    uint32_t *const tileCache = ctx.image.At<uint32_t>(0x10cc45c);
    uint32_t *const singleton = ctx.image.At<uint32_t>(0x10cc288);
    const uint32_t saved[2] = {*tileCache, *singleton};
    *tileCache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("TileSvc")));
    *singleton = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("SvcA")));
    const Summary s = RunSeeds(ctx, 52, 8000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 1, 2, 3, 4, 7, 8, 15, 0x100, -1, 0x7fff}});
    });
    *tileCache = saved[0];
    *singleton = saved[1];
    Report(s);
    if (!s.threw || s.byResult.size() < 2) ctx.Fail("the worlds did not reach every outcome");
}
