// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 4 (src/simantics/PrimGenericSimCallH.cpp, Case04): the
// interaction start/yield case. Recording-fake world (tests/simantics_gsc_world.h): the person list, the entry table, the entries,
// the holder, the lookup list and the message server are fakes; the original and ours run on identical worlds and the whole log,
// the result (or thrown code) and the reference counts must match.
#include "rz/RZMessageServer.h"
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

// 0x9c5a89 (cdecl, no arguments): the message server; its slot 4 is the post call.
uint32_t __cdecl StubMessageServer()
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("MsgServer")));
}

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({}));
    {
        // The resume state; once it is nonzero the later-visit path runs, where the original dereferences the person's entry
        // without a null check (a null entry crashes the original and ours alike), so the table then always finds one.
        TypeSpec f = FrameType("i{0,0,0,5,0x7cff,0x1234,-4}");
        f.slots[1].hook = [](Call &c) -> uint32_t {
            static const uint32_t pool[] = {0, 0, 0, 5, 0x7cff, 0x1234, 0xfffffffcu};
            const uint32_t v = pool[c.Rand() % 7];
            gWorld->user[0] = v != 0;
            return v;
        };
        t.push_back(f);
    }
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable");
        c.slots.push_back(S(23, "", "o:Holder!"));
        c.slots.push_back(S(6, "", "o:Lookup?"));
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
        TypeSpec p;
        p.name = "PeopleTable";
        p.slots = {S(16, "VV", "o:Person?")};
        t.push_back(p);
    }
    {
        TypeSpec p;
        p.name = "Person";
        p.count = 2;
        p.slots = {S(10, "", "o:PersonOwner?"), S(16, "V", "h{0,0,1,0x100,0x7cff,0x2000,-1}")};
        t.push_back(p);
    }
    {
        TypeSpec p;
        p.name = "PersonOwner";
        p.slots = {S(51, "t", "b")};
        t.push_back(p);
    }
    {
        TypeSpec e;
        e.name = "EntryTable";
        e.slots = {S(11, "V", "o:Entry?", "", [](Call &c) -> uint32_t {
                       const uint32_t r = c.Rand(NormWord(c.a[0]));
                       if (!gWorld->user[0] && r % 100 < 28) return 0;
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Entry", static_cast<int>((r >> 8) % 4))));
                   }), S(13, "OV", "v", "Entry?"), S(14, "V", "v"), S(17, "VVV", "i{0,1,2,0x100}"), S(18, "VV", "i{0,1,2,0x100}")};
        t.push_back(e);
    }
    {
        TypeSpec e;
        e.name = "Entry";
        e.count = 4;
        e.slots = {S(58, "V", "v"),
                   S(38, "T", "v"),
                   S(37, "", "o:Str!"),
                   S(41, "", "i{0,1,2,0x7cff,0x7d00,-1,7}"),
                   S(24, "V", "i{0,1,2,3}"),
                   S(42, "", "i{0,0,1,2,0x100}"),
                   S(43, "V", "v"),
                   S(53, "", "i{0,1,0x10001,0x7777}")};
        t.push_back(e);
    }
    {
        TypeSpec s;
        s.name = "Str";
        s.slots = {S(7, "", "i{0,1,2}")};
        t.push_back(s);
    }
    {
        TypeSpec h;
        h.name = "Holder";
        h.slots = {S(14, "", "o:Known?"), S(16, "", "o:Entry?")};
        t.push_back(h);
    }
    {
        TypeSpec k;
        k.name = "Known";
        k.slots = {S(37, "", "T")};
        t.push_back(k);
    }
    {
        TypeSpec l;
        l.name = "Lookup";
        l.slots = {S(57, "V", "o:Named?")};
        t.push_back(l);
    }
    {
        TypeSpec n;
        n.name = "Named";
        n.slots = {S(19, "", "T")};
        t.push_back(n);
    }
    {
        TypeSpec m;
        m.name = "MsgServer";
        m.slots = {S(4, "pV", "v")};
        t.push_back(m);
        TypeSpec g;
        g.name = "Msg";
        g.slots = {S(1, "", "i{1}"), S(2, "", "i{0}"), S(5, "V", "v"), S(11, "V", "v"), S(30, "T", "v")};
        t.push_back(g);
    }
    return t;
}

} // namespace

DIFF_TEST(simantics_gsc_case04, 0x58bc50)
{
    Setup(ctx, Types());
    CodePatch patchServer(ctx, 0x9c5a89, reinterpret_cast<void *>(&StubMessageServer));
    CodePatch patchServerOurs(reinterpret_cast<void *>(&GetCachedMessageServer), reinterpret_cast<void *>(&StubMessageServer)); // ours
    MessageSupport messages(ctx);
    const Summary s = RunSeeds(ctx, 4, 6000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 0, 1, 5, 0x7cff, 0x100, -2}, {0, 1, 7, -3, 0x100}});
    });
    Report(s);
    if (!s.threw || s.byResult.size() < 3) ctx.Fail("the worlds did not reach every outcome");
}
