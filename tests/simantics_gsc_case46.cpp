// Differential tests for GenericSimCall (opcode 0x01, 0x58bc50) cases 46 and 53 (src/simantics/PrimGenericSimCallG.cpp, Case46 and
// Case53): a GUID text from the string tables handed to the component interface of an object. Recording-fake world
// (tests/simantics_gsc_world.h); the string-table loader runs for real against a fake resource manager (tests/ts_string_fake.h) as in the case 60 test, the GUID parser runs
// as original code on both sides.
#include "simantics_gsc_world.h"

#include "simantics/PrimGenericSimCallOriginals.h"
#include "ts_string_fake.h"

using namespace gscw;

namespace {

const char *const kGuidTexts[] = {"", "{00000000-0000-0000-0000-000000000000}", "{12345678-1234-1234-1234-123456789abc}",
                                  "{AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE}", "junk", "{12345678-1234-1234-1234-123456789abc}"};

// What the resource manager answers when the string-table loader (0x1d0e90, 0x1d10d0) asks for a text: logs the key it was built
// from and gives a scripted GUID text.
bool AnswerGuidText(const tsfake::Ask &ask, std::string &text)
{
    const uint32_t table = ask.instance >> 16, entry = ask.instance & 0xffff;
    Log(0x1d0e90u);
    gTrace.insert(gTrace.end(), {table, entry, ask.group, ask.type});
    const uint32_t r = Mix(gWorld->seed, table, entry, ask.group);
    text = kGuidTexts[r % 6];
    return true;
}

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(9, "VVO", "v", "Component?")}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "Objects", "ObjectDefTable");
        t.push_back(c);
    }
    for (const char *name : {"EntryTable", "PeopleTable", "ObjectDefTable"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Objects";
        x.slots = {S(29, "V", "o:Obj?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Obj";
        x.count = 2;
        x.slots = {S(90, "", "o:ObjType?"), S(61, "", "o:Chain!"), S(9, "VVO", "v", "Component?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "ObjType";
        x.slots = {S(11, "", "i{0,1,0x7fd0,0x12345678}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Chain";
        x.slots = {S(69, "", "i{0,0,0x7fd1,0x100}")};
        t.push_back(x);
    }
    {
        // the component holder: its virtual base answers the sink interface (slot 0 of the sub-object)
        TypeSpec x;
        x.name = "Component";
        x.vbase = true;
        x.subSlots = {S(0, "VO", "b", "Sink?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Sink";
        x.vbase = true;
        x.slots = {S(0, "G", "v"), S(3, "GV", "v")};
        t.push_back(x);
    }
    return t;
}

void RunGuidCase(difftest::Context &ctx, int which)
{
    Setup(ctx, Types());
    tsfake::ManagerFixture resourceManager(ctx);
    resourceManager.InstallManagerOnly();
    tsfake::gAnswerer = AnswerGuidText;
    const Summary s = RunSeeds(ctx, which, 10000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 0, 1, 2, 7, 0x100}, {0, 1, 2, 7, -1}, {0, 1, 2}, {0, 1}, {0, 1, 2, 3, 5}});
    });
    Report(s);
    if (!s.threw || s.byResult.size() < 2) ctx.Fail("the worlds did not reach every outcome");
}

} // namespace

DIFF_TEST(simantics_gsc_case46, 0x58bc50)
{
    RunGuidCase(ctx, 46);
}

DIFF_TEST(simantics_gsc_case53, 0x58bc50)
{
    RunGuidCase(ctx, 53);
}
