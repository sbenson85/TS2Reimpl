// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 60 (src/simantics/PrimGenericSimCallH.cpp, Case60): applies an
// outfit GUID (from a string-table text, or from the entry a picker names) to the sim. Recording-fake world
// (tests/simantics_gsc_world.h). The string-table loader (cTSString, 0x1d0e90) runs for real against a fake resource manager
// (tests/ts_string_fake.h) that logs its key and answers with a scripted text; the GUID parser, the GUID test and the "marked object" test run as original code on both sides.
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
    t.push_back(SelfType({S(0, "VO", "b", "Iface")}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable");
        c.slots.push_back(S(27, "", "o:Picker?"));
        t.push_back(c);
    }
    {
        TypeSpec x;
        x.name = "EntryTable";
        t.push_back(x);
        x.name = "ObjectDefTable";
        t.push_back(x);
        x.name = "PeopleTable";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "ObjectTable";
        x.slots = {S(29, "V", "o:Obj?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Obj";
        x.slots = {S(90, "", "o:ObjType?"), S(61, "", "o:Chain!")};
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
        TypeSpec x;
        x.name = "Outfit";
        x.slots = {S(65, "", "b")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Iface";
        x.slots = {S(8, "", "o:Owner!"), S(57, "", "i{0,1,2,0x100,0x10002}"), S(10, "VV", "h{0,1,5,0x100,-3}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Owner";
        x.slots = {S(61, "", "o:Sim!"), S(9, "VVO", "v", "Component?"), S(64, "", "o:Kind!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Kind";
        x.slots = {S(3, "V", "i{0xd07b9ce9,0xd07b9ce9,0,5}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Sim";
        x.slots = {S(67, "", "o:Graph!"), S(28, "", "o:Tag!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Graph";
        x.slots = {S(11, "", "i{0,1,7,0x1234}")};
        t.push_back(x);
        x.name = "Tag";
        x.slots.clear();
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Component";
        x.vbase = true;
        x.slots = {S(8, "VVVVV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "TextSvc";
        x.slots = {S(36, "VV", "v"), S(8, "VVVGV", "b")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "KeySvc";
        x.slots = {S(5, "GVOVV", "b", "Property")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "TagSvc";
        x.slots = {S(3, "V", "v"), S(12, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Property";
        x.count = 2;
        x.slots = {S(28, "", "p"), S(55, "G", "v"), S(96, "", "i")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Picker";
        x.slots = {S(4, "VO", "b", "Source")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Source";
        x.slots = {S(6, "VV", "v", "", [](Call &c) -> uint32_t {
                       // fills the pair at a[0] with two (AddRef'd) items and returns its address
                       void **pair = reinterpret_cast<void **>(static_cast<uintptr_t>(c.a[0]));
                       for (int i = 0; i < 2; ++i) {
                           Fake *item = Instance("Item", static_cast<int>((c.Rand(i + 1) >> 8) % 3));
                           ++item->refs;
                           pair[i] = item;
                       }
                       // (the real slot answers the pair it filled; sometimes this one answers another pair, so a caller that reads
                       // the pair it passed in instead of the one returned is caught)
                       if (c.Rand(9) % 3 == 0) {
                           void **other = reinterpret_cast<void **>(c.self->data);
                           for (int i = 0; i < 2; ++i) {
                               Fake *item = Instance("Item", static_cast<int>((c.Rand(i + 5) >> 8) % 3));
                               other[i] = item;
                           }
                           return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(other));
                       }
                       return c.a[0];
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Item";
        x.count = 3;
        x.slots = {S(7, "", "i{0,1,2}"), S(5, "", "c", "", [](Call &c) -> uint32_t {
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kGuidTexts[c.Rand() % 6]));
                   })};
        t.push_back(x);
    }
    return t;
}

} // namespace

DIFF_TEST(simantics_gsc_case60, 0x58bc50)
{
    Setup(ctx, Types());
    tsfake::ManagerFixture resourceManager(ctx);
    resourceManager.InstallManagerOnly();
    tsfake::gAnswerer = AnswerGuidText;
    // cached services
    uint32_t *const textCache = ctx.image.At<uint32_t>(0x10cc8ec);
    uint32_t *const keyCache = ctx.image.At<uint32_t>(0x10faf6c);
    uint32_t *const tagCache = ctx.image.At<uint32_t>(0x10faf70);
    uint32_t *const optionMask = ctx.image.At<uint32_t>(0x10faf94);
    const uint32_t saved[4] = {*textCache, *keyCache, *tagCache, *optionMask};
    *textCache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("TextSvc")));
    *keyCache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("KeySvc")));
    *tagCache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("TagSvc")));
    uint8_t guids[2][16];
    gscorig::ParseGuid(guids[0], kGuidTexts[2]);
    gscorig::ParseGuid(guids[1], kGuidTexts[3]);
    const Summary s = RunSeeds(ctx, 60, 10000, "Frame", [&](uint32_t seed) {
        // the world is rebuilt per side; the cached services point at the same fake instances each time
        *optionMask = (seed % 3 == 0) ? 0x08u : 0x11u;
        SetObjectFields("Context", "Outfit", nullptr);
        std::memcpy(gWorld->blobs[1], guids[0], 16);
        std::memcpy(gWorld->blobs[2], guids[1], 16);
        RandomArgs(seed, {{0, 0, 1, 2, 7, 0x100}, {0, 1, 2, 7, -1}, {0, 1}, {0, 1, 2}});
    });
    *textCache = saved[0];
    *keyCache = saved[1];
    *tagCache = saved[2];
    *optionMask = saved[3];
    Report(s);
    if (!s.threw || s.byResult.size() < 2) ctx.Fail("the worlds did not reach every outcome");
}
