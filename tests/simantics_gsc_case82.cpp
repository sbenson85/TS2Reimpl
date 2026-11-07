// Differential tests for GenericSimCall (opcode 0x01, 0x58bc50) cases 82, 83 and 84 (src/simantics/PrimGenericSimCallB.cpp,
// KeyedEntry; PrimGenericSimCallD.cpp, Case84): a four-dword key built from the argument words. The log shows the order in which the
// words are read (the original reads the last pair first, each pair low word first) and the key bytes handed to the callee.
// Recording-fake world (tests/simantics_gsc_world.h).
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("Entries", "People", "ObjectTable", "ObjectDefTable");
        t.push_back(c);
    }
    for (const char *name : {"ObjectTable", "ObjectDefTable"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "People";
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
        x.name = "Entries";
        x.slots = {S(11, "V", "o:Entry?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entry";
        x.slots = {S(10, "G", "v"), S(12, "G", "b")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "ResSvc";
        x.slots = {S(37, "GVO", "b", "Held?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Held";
        t.push_back(x);
    }
    return t;
}

void RunKeyCase(difftest::Context &ctx, int which)
{
    Setup(ctx, Types());
    uint32_t *const resourceCache = ctx.image.At<uint32_t>(0x10faf74);
    const uint32_t saved = *resourceCache;
    *resourceCache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("ResSvc")));
    const Summary s = RunSeeds(ctx, which, 6000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{}, {0, 1, 2, 5}});
    });
    *resourceCache = saved;
    Report(s);
    if (!s.byResult.size()) ctx.Fail("the worlds did not reach every outcome");
}

} // namespace

DIFF_TEST(simantics_gsc_case82, 0x58bc50)
{
    RunKeyCase(ctx, 82);
}

DIFF_TEST(simantics_gsc_case83, 0x58bc50)
{
    RunKeyCase(ctx, 83);
}

DIFF_TEST(simantics_gsc_case84, 0x58bc50)
{
    RunKeyCase(ctx, 84);
}
