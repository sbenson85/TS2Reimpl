// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 50 (src/simantics/PrimGenericSimCallG.cpp, Case50): a scaled
// amount written to the arguments. Recording-fake world (tests/simantics_gsc_world.h): the person, the entry table (listing its
// ids into a vector<int16_t> the original frees through the game allocator, global 0x10c95d4, a quiet fake), the context's slot-7 list,
// the two interfaces the director makes (through the COM director singleton's vtable slot 12 in the image) and the persist system.
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
        c.slots.push_back(S(7, "", "o:List7!"));
        t.push_back(c);
    }
    for (const char *name : {"ObjectTable", "ObjectDefTable"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    t.back().name = "ObjectDefTable";
    {
        TypeSpec x;
        x.name = "Owner2c8";
        x.slots = {S(29, "", "i{0,1,100,-5,20000}"), S(30, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "People";
        x.slots = {Lookup(16, "VV", "Person", 8)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Person";
        x.slots = {S(10, "", "o:PObj!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "PObj";
        x.slots = {S(28, "", "i{0,1,0x77,0x1234}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entries";
        x.slots = {S(19, "V", "v", "", [](Call &c) -> uint32_t {
                       uint32_t *vec = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c.a[0]));
                       const uint32_t n = c.Rand(1) % 5;
                       if (n == 0) return 0;
                       int16_t *buf = static_cast<int16_t *>(::operator new(n * 2));
                       for (uint32_t i = 0; i < n; ++i) buf[i] = static_cast<int16_t>(c.Rand(10 + i) % 6);
                       vec[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
                       vec[1] = vec[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf + n));
                       return 0;
                   }),
                   Lookup(11, "V", "Entry", 15)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entry";
        x.count = 2;
        x.slots = {S(29, "V", "b"), S(60, "", "i{0,1,50,-20,12345,100000}"), S(42, "", "i{0,1,2,3,4}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "List7";
        x.slots = {Lookup(18, "V", "Thing", 15)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Thing";
        x.slots = {S(8, "V", "i{0,1,2}")};
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
        x.slots = {S(3, "VVO", "v", "Interface")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Interface";
        x.count = 4;
        x.slots = {S(29, "", "i{0,1,50,-20,12345,100000}"), S(6, "V", "i{0,0,1,2}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "KeySvc";
        x.slots = {S(73, "VVVV", "i{0,0,1,0x100}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Alloc";
        Slot freeSlot = S(4, "V", "v", "", [](Call &c) -> uint32_t {
            ::operator delete(reinterpret_cast<void *>(static_cast<uintptr_t>(c.a[0])));
            return 0;
        });
        freeSlot.quiet = true;
        x.slots = {freeSlot};
        t.push_back(x);
    }
    return t;
}

} // namespace

DIFF_TEST(simantics_gsc_case50, 0x58bc50)
{
    Setup(ctx, Types());
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
    put(0x10c95d4, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Alloc"))));
    put(0x10c95d8, 0x80000000u);
    put(0x10c93f4, 0x80000000u);
    put(0x10c9390, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVtables[TypeIndex("Director")].data())));
    const Summary s = RunSeeds(ctx, 50, 12000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr, "Owner2c8");
        RandomArgs(seed, {{0, 1, 2}, {0, 1, 2, 5}, {0, 1, 50, 100, 200, -30, 0x7fff}, {}, {}, {0, 1, 2}});
    });
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->where = it->value;
    Report(s);
    if (!s.threw || s.byResult.size() < 1) ctx.Fail("the worlds did not reach every outcome");
}
