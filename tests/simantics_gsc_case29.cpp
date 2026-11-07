// Differential tests for GenericSimCall (opcode 0x01, 0x58bc50) cases 29 and 31 (src/simantics/PrimGenericSimCallG.cpp, Case29 and
// Case31). Recording-fake world (tests/simantics_gsc_world.h). Case 29 applies a property set through the tag service and counts
// down in the frame's resume state; case 31 preloads the models listed in an object's STR# table (the director makes an empty table
// when none is found: through the COM director singleton's vtable slot 12 in the image, as in the case 70 test).
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

const char *const kNames[] = {"", "foo", "foo_cres", "FOO_CRES", "_cres", "a", "x_cres_c", "a_CRES"};

std::vector<TypeSpec> Types(bool for31)
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({}));
    {
        TypeSpec f = FrameType("i{0,0,0,105,100,102,99,-3}");
        t.push_back(f);
    }
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "ObjectTable", "Defs");
        c.slots.push_back(S(3, "", "o:Gate!"));
        t.push_back(c);
    }
    for (const char *name : {"EntryTable", "ObjectTable"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Gate";
        x.slots = {S(21, "", "i{0,1,3,3,4,5}")};
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
        x.slots = {S(10, "", "o:Obj29?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Obj29";
        x.slots = {S(67, "", "o:Graph?"), S(28, "", "i{0,1,0x77,0x1234}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Graph";
        x.slots = {S(11, "", "i{0,1,7,0x1234}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "TagSvc";
        x.slots = {S(44, "V", "b"), S(45, "VVV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "KeySvc";
        x.slots = {S(5, "GVOVV", "b", for31 ? "StrTable?" : "Prop"), S(6, "GVVVV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Prop";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Defs";
        x.slots = {S(14, "V", "o:Record?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Record";
        x.slots = {S(18, "", "o:Rec2!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Rec2";
        x.slots = {S(11, "", "i{0,1,7,0x1234}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Slot23";
        x.slots = {S(11, "", "i{0,1,0x77}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Director";
        x.slots = {S(12, "", "o:Framework!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Framework";
        x.slots = {S(3, "VVO", "v", "StrTable?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "StrTable";
        x.slots = {S(5, "V", "i{0,1,2,3,4}"), S(6, "VV", "T", "", [](Call &c) -> uint32_t {
                       cRZString &text = gWorld->texts[c.Rand(NormWord(c.a[0])) % 8];
                       text.mString = kNames[c.Rand(NormWord(c.a[0]) + 5) % 8];
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&text));
                   })};
        t.push_back(x);
    }
    return t;
}

} // namespace

DIFF_TEST(simantics_gsc_case29, 0x58bc50)
{
    Setup(ctx, Types(false));
    struct Saved {
        uint32_t *where;
        uint32_t value;
    };
    std::vector<Saved> saved;
    auto put = [&](uint32_t rva, uint32_t value) {
        uint32_t *p = ctx.image.At<uint32_t>(rva);
        saved.push_back({p, *p});
        *p = value;
    };
    put(0x10faf78, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("TagSvc"))));
    put(0x10faf6c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("KeySvc"))));
    const Summary s = RunSeeds(ctx, 29, 10000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 1, 2, 5, 0x100}, {0, 1, 2, 3, -1, -2, 5}});
    });
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->where = it->value;
    Report(s);
    if (!s.threw || s.byResult.size() < 2) ctx.Fail("the worlds did not reach every outcome");
}

DIFF_TEST(simantics_gsc_case31, 0x58bc50)
{
    Setup(ctx, Types(true));
    struct Saved {
        uint32_t *where;
        uint32_t value;
    };
    std::vector<Saved> saved;
    auto put = [&](uint32_t rva, uint32_t value) {
        uint32_t *p = ctx.image.At<uint32_t>(rva);
        saved.push_back({p, *p});
        *p = value;
    };
    put(0x10faf6c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("KeySvc"))));
    put(0x10c93f4, 0x80000000u);
    put(0x10c9390, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVtables[TypeIndex("Director")].data())));
    const Summary s = RunSeeds(ctx, 31, 10000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 1, 2, 5, 0x100}, {0, 1, 2, 5, -1}});
    });
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->where = it->value;
    Report(s);
    if (!s.byResult.size()) ctx.Fail("the worlds did not reach every outcome");
}
