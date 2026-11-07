// Differential tests for GenericSimCall (opcode 0x01, 0x58bc50) cases 17, 19, 20, 26 and 80 (src/simantics/PrimGenericSimCallA.cpp /
// ...B.cpp): the attribute-match cases. Recording-fake world (tests/simantics_gsc_world.h). The attribute test 0x57ee10 (it reaches
// the cached slot-23 object and the message server) is redirected to a stand-in that logs its two arguments in full (so a
// sign-extension difference shows) and answers from the seed; the cached singletons the cases reach are fake objects too.
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

// 0x57ee10 (cdecl, two stack words, result in al).
uint32_t __cdecl StubAttribute(int32_t a, int32_t b)
{
    Log(0x57ee10u);
    Log(static_cast<uint32_t>(a));
    Log(static_cast<uint32_t>(b));
    return (Mix(gWorld->seed, static_cast<uint32_t>(a), static_cast<uint32_t>(b), 0x57) & 3) == 0 ? 0xaabbcc00u : 0xaabbcc01u;
}

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable");
        c.slots.push_back(S(23, "", "o:Holder!"));
        t.push_back(c);
    }
    {
        TypeSpec x;
        x.name = "ObjectTable";
        t.push_back(x);
        x.name = "ObjectDefTable";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "PeopleTable";
        x.slots = {S(16, "VV", "o:Person?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Person";
        x.slots = {S(16, "V", "h{0,1,2,0x7cff,0x7d00,-1,0x100,0x8000}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "EntryTable";
        x.slots = {S(11, "V", "o:Entry?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entry";
        x.slots = {S(42, "", "i{0,1,5,0x7cff,0x12345,-1,0x8000}"), S(46, "", "i{0,0,1,5,0x7cff,0x12345,-1}")};
        t.push_back(x);
    }
    {
        // the context's holder (slot 23) and the cached one (a different instance: the receivers must not be mixed up)
        TypeSpec x;
        x.name = "Holder";
        x.count = 2;
        x.slots = {S(24, "", "b"), S(9, "", "o:Sub!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Sub";
        x.slots = {S(18, "", "i{0,1,5,0x7cff,0x12345,-1}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Attr";
        x.slots = {S(15, "V", "h{0,1,2,0x28,0x100,-1,0x7fff,-0x8000}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "SvcA";
        x.slots = {S(8, "", "o:List!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "List";
        x.slots = {S(9, "V", "o:Info!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Info";
        x.slots = {S(31, "", "b")};
        t.push_back(x);
    }
    return t;
}

void RunAttrCase(difftest::Context &ctx, int which, const std::vector<std::vector<int32_t>> &pools)
{
    Setup(ctx, Types());
    CodePatch patch(ctx, 0x57ee10, reinterpret_cast<void *>(&StubAttribute));
    uint32_t *const singleton = ctx.image.At<uint32_t>(0x10cc288);
    uint32_t *const slot23 = ctx.image.At<uint32_t>(0x10cc2a8);
    const uint32_t saved[2] = {*singleton, *slot23};
    *singleton = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("SvcA")));
    *slot23 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Holder", 1)));
    const Summary s = RunSeeds(ctx, which, 6000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr, "Attr");
        RandomArgs(seed, pools);
        // argument 2 must be observable: the stub log shows the stores through slot 20
    });
    *singleton = saved[0];
    *slot23 = saved[1];
    Report(s);
}

} // namespace

DIFF_TEST(simantics_gsc_case17, 0x58bc50)
{
    RunAttrCase(ctx, 17, {{0, 1, 2, 5, 0x100, -1, 0x8000, 0x7fff}});
}

DIFF_TEST(simantics_gsc_case19, 0x58bc50)
{
    RunAttrCase(ctx, 19, {{0, 1, 2, 5, -1, 0x100}});
}

DIFF_TEST(simantics_gsc_case20, 0x58bc50)
{
    RunAttrCase(ctx, 20, {{0, 1, 2, 5, 0x7cff, 0x7d00, -1, 0x100, 0x8000}});
}

DIFF_TEST(simantics_gsc_case26, 0x58bc50)
{
    RunAttrCase(ctx, 26, {{0, 0, 1, 2, 5, 0x100, -1}, {0, 1, 2, 5, 0x100, -1, 0x8000, 0x7fff}});
}

DIFF_TEST(simantics_gsc_case80, 0x58bc50)
{
    RunAttrCase(ctx, 80, {{0, 0, 1, 2, 5, 0x100, -1}, {0, 1, 2, 5, 0x100, -1, 0x8000, 0x7fff}});
}
