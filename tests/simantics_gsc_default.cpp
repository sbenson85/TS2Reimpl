// Differential test for GenericSimCall (opcode 0x01, 0x58bc50): the selector values that go to the default body (7, 8, 9, 15, 16, 0x55
// and above, negative words) must throw 0x2a after the prologue's four context calls, in both. Recording-fake world
// (tests/simantics_gsc_world.h).
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
        c.slots = PrologueSlots("Entries", "People", "Objects", "Defs");
        t.push_back(c);
    }
    for (const char *name : {"Entries", "People", "Objects", "Defs"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    return t;
}

void RunDefault(difftest::Context &ctx, int which)
{
    Setup(ctx, Types());
    const Summary s = RunSeeds(ctx, which, 40, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {});
    });
    Report(s);
    if (s.threw + s.crashed + 0 == 0 && s.byResult.empty()) ctx.Fail("nothing ran");
}

} // namespace

#define DEFAULT_CASE(NAME, N) \
    DIFF_TEST(simantics_gsc_default_##NAME, 0x58bc50) \
    {                                                 \
        RunDefault(ctx, N);                           \
    }

DEFAULT_CASE(7, 7)
DEFAULT_CASE(8, 8)
DEFAULT_CASE(9, 9)
DEFAULT_CASE(15, 15)
DEFAULT_CASE(16, 16)
DEFAULT_CASE(85, 0x55)
DEFAULT_CASE(86, 0x56)
DEFAULT_CASE(big, 0x1000)
DEFAULT_CASE(negative, -1)
DEFAULT_CASE(negative_small, -80)
DEFAULT_CASE(wrap, -32768)
