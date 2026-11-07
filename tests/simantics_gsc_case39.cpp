// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 39 (src/simantics/PrimGenericSimCallG.cpp, Case39): a
// command for the context's store, picked by argument 0. Recording-fake world (tests/simantics_gsc_world.h).
#include "rz/RZMessageServer.h"
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

uint32_t __cdecl StubMessageServer()
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("MsgServer")));
}

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(0, "VO", "b", "Iface?")}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable");
        c.slots.push_back(S(4, "", "o:Store!"));
        t.push_back(c);
    }
    {
        TypeSpec x;
        x.name = "EntryTable";
        t.push_back(x);
        x.name = "ObjectDefTable";
        t.push_back(x);
    }
    {
        TypeSpec p;
        p.name = "PeopleTable";
        p.slots = {S(16, "VV", "o:Person?")};
        t.push_back(p);
    }
    {
        TypeSpec p;
        p.name = "Person";
        p.count = 2;
        p.slots = {S(15, "", "i{0,1,0x100,0x12345}")};
        t.push_back(p);
    }
    {
        TypeSpec p;
        p.name = "ObjectTable";
        p.slots = {S(43, "V", "o:Iface?")};
        t.push_back(p);
    }
    {
        TypeSpec p;
        p.name = "Iface";
        p.count = 3;
        p.slots = {S(57, "", "h{0,1,5,0x100,-3}")};
        t.push_back(p);
    }
    {
        TypeSpec s;
        s.name = "Store";
        s.slots = {S(35, "VV", "v"), S(45, "V", "v"), S(40, "VVV", "v"), S(39, "VVV", "v"), S(36, "V", "v"), S(47, "VV", "v"),
                   S(48, "VV", "v"), S(37, "VVV", "v"), S(57, "", "b"), S(56, "", "v")};
        t.push_back(s);
    }
    {
        TypeSpec m;
        m.name = "MsgServer";
        m.slots = {S(4, "pV", "v")};
        t.push_back(m);
        TypeSpec g;
        g.name = "Msg";
        g.slots = {S(1, "", "i{1}"), S(2, "", "i{0}"), S(5, "V", "v")};
        t.push_back(g);
    }
    return t;
}

} // namespace

DIFF_TEST(simantics_gsc_case39, 0x58bc50)
{
    Setup(ctx, Types());
    CodePatch patchServer(ctx, 0x9c5a89, reinterpret_cast<void *>(&StubMessageServer));
    CodePatch patchServerOurs(reinterpret_cast<void *>(&GetCachedMessageServer), reinterpret_cast<void *>(&StubMessageServer)); // ours
    MessageSupport messages(ctx);
    const Summary s = RunSeeds(ctx, 39, 8000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, -1, 0x100},
                          {0, 1, 2, 5, 100, -1, 0x7fff},
                          {0, 1, 2, 3, -1, 5, 100, 0x7fff, -32768, 10}});
    });
    Report(s);
    if (!s.threw || s.byResult.size() < 1) ctx.Fail("the worlds did not reach every outcome");
}
