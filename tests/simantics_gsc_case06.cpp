// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 6 (src/simantics/PrimGenericSimCallG.cpp, Case06): hands the
// object table the object a person's interaction concerns. Recording-fake world (tests/simantics_gsc_world.h).
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(63, "", "o:Holder!")}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("Entries", "People", "Objects", "ObjectDefTable");
        c.slots.push_back(S(3, "", "o:Gate!"));
        t.push_back(c);
    }
    {
        TypeSpec x;
        x.name = "ObjectDefTable";
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
        x.slots = {S(16, "V", "h{0,1,2,3,0x100,-1}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entries";
        x.slots = {S(11, "V", "o:Entry?"), S(18, "VV", "i{0,0,1,0x100}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entry";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Gate";
        x.slots = {S(15, "V", "h{0,1,2,3,0x100,-1}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Objects";
        x.slots = {S(29, "V", "o:Target?"), S(40, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Target";
        x.slots = {S(0, "VO", "b", "Iface")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Iface";
        x.slots = {S(10, "VV", "h{0,1,2,3,0x100,-1}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Holder";
        x.slots = {S(45, "", "p", "", [](Call &c) -> uint32_t {
                       // a vector's first and last pointers over 0..4 element pointers
                       uint32_t *words = reinterpret_cast<uint32_t *>(c.self->data);
                       const uint32_t n = c.Rand(1) % 5;
                       for (uint32_t i = 0; i < n; ++i) words[2 + i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Instance("Elem", static_cast<int>((c.Rand(10 + i) >> 8) % 3))));
                       words[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(words + 2));
                       words[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(words + 2 + n));
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(words));
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Elem";
        x.count = 3;
        x.slots = {S(10, "VV", "h{0,1,2,3,0x100,-1}"), S(31, "", "b")};
        t.push_back(x);
    }
    return t;
}

} // namespace

DIFF_TEST(simantics_gsc_case06, 0x58bc50)
{
    Setup(ctx, Types());
    const Summary s = RunSeeds(ctx, 6, 10000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {});
    });
    Report(s);
    if (!s.threw || s.byResult.size() < 2) ctx.Fail("the worlds did not reach every outcome");
}
