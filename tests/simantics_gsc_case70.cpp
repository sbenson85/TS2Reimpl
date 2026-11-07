// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 70 (src/simantics/PrimGenericSimCallK.cpp, Case70): a collection
// pass over a sim's belongings. Recording-fake world (tests/simantics_gsc_world.h): the director framework (through the COM director
// singleton's vtable slot 12 in the image, so the original's 0x1ae18d and ours reach the same fake), the list object and its head,
// the cached singleton (people, entries, object table), the key service, the property/resource objects, and the allocator the
// original frees the key vector through (global 0x10c95d4, a quiet fake that frees the test's buffer).
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(61, "", "o:Owner70!")}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable");
        t.push_back(c);
    }
    for (const char *name : {"EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Owner70";
        x.slots = {S(66, "", "i{0,1,7,0x1234}")};
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
        x.slots = {S(3, "VVO", "v", "List")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "List";
        x.slots = {S(3, "V", "v"), S(10, "", "o:Head!"), S(7, "", "i{0,1,2,10,500,-3,100000}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Head";
        x.slots = {S(10, "", "b"), S(22, "", "h{0,1,2,5,0x100,-1}"), S(16, "", "i{0,1,0x1234,0x10000}"), S(15, "", "i{0,1,0x1234,5}"),
                   S(21, "V", "v", "", [](Call &c) -> uint32_t {
                       uint32_t *vec = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c.a[0]));
                       const uint32_t n = c.Rand(1) % 5;
                       if (n == 0) return 0;
                       uint32_t *buf = static_cast<uint32_t *>(::operator new(n * 16));
                       for (uint32_t i = 0; i < n * 4; ++i) buf[i] = c.Rand(10 + i) % 40;
                       vec[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
                       vec[1] = vec[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf + n * 4));
                       return 0;
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Single";
        x.slots = {S(10, "", "o:People!"), S(9, "", "o:Entries!"), S(26, "", "o:ObjTable!")};
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
        x.count = 2;
        x.slots = {S(15, "", "i{0,1,0x77}"), S(16, "V", "h{0,1,5,0x100,-3,0x7cff}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "ObjTable";
        x.slots = {S(43, "V", "o:Obj?"), S(29, "V", "o:Target?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Obj";
        x.slots = {S(8, "", "o:ObjOwner!"), S(10, "VV", "h{0,1,5,0x100,-3,0x7cff}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "ObjOwner";
        x.slots = {S(9, "VVO", "b", "Component")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Component";
        x.vbase = true;
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entries";
        x.slots = {S(11, "V", "o:Entry?", "", [](Call &c) -> uint32_t {
                       if (c.Rand(NormWord(c.a[0])) % 20 == 0) return 0;
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Entry", static_cast<int>((c.Rand(NormWord(c.a[0])) >> 8) % 2))));
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entry";
        x.count = 2;
        x.slots = {S(12, "G", "b"), S(10, "G", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Target";
        x.slots = {S(20, "VV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "KeySvc";
        x.slots = {S(5, "GVOVV", "b", "Res"), S(3, "", "o:Primary!"), S(34, "VVO", "v", "Res?"), S(28, "V", "b"), S(17, "G", "v"), S(16, "VV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Primary";
        x.slots = {S(34, "VO", "v", "Res?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Res";
        x.count = 3;
        x.slots = {S(73, "V", "v"), S(3, "V", "p"), S(22, "VG", "i{0,1,0x1234}"), S(0, "VO", "b", "Sink")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Sink";
        x.slots = {S(6, "", "i{0,1,2,3}")};
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

DIFF_TEST(simantics_gsc_case70, 0x58bc50)
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
    put(0x10cc288, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Single")))); // GetCachedSingleton
    put(0x10faf6c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("KeySvc")))); // the key service
    put(0x10c95d4, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Alloc"))));
    put(0x10c95d8, 0x80000000u);
    put(0x10c93f4, 0x80000000u);                                                              // the COM director singleton counts as built
    put(0x10c9390, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gVtables[TypeIndex("Director")].data()))); // its vtable: slot 12 answers the framework
    const Summary s = RunSeeds(ctx, 70, 10000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 1, 2, 5, -1, 0x100}, {0, 1, 2}});
    });
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->where = it->value;
    Report(s);
    if (!s.threw || s.byResult.size() < 2) ctx.Fail("the worlds did not reach every outcome");
}
