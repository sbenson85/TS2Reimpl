// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 61 (src/simantics/PrimGenericSimCallG.cpp, Case61, the game's
// kAddExternalOutfitToFamilyClothingBin): gives an entry the property resources that fit a sim's own. Recording-fake world
// (tests/simantics_gsc_world.h): the key list the persist system fills into a vector the original frees through the game
// allocator (global 0x10c95d4, a quiet fake), the entry, the person, the two records and the resources.
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
        c.slots = PrologueSlots("Entries", "People", "ObjectTable", "Defs");
        t.push_back(c);
    }
    {
        TypeSpec x;
        x.name = "ObjectTable";
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "LogObj";
        x.slots = {S(28, "S", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "People";
        x.slots = {Lookup(16, "VV", "Person", 6)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Person";
        x.slots = {S(16, "V", "h{0,1,2,0x7cff,0x7d00,-1,0x100}"), S(15, "", "i{0,1,2,3,0x1234}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entries";
        x.slots = {Lookup(11, "V", "Entry", 6)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entry";
        x.slots = {S(11, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Defs";
        x.slots = {Lookup(14, "V", "Record", 6)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Record";
        x.count = 2;
        x.slots = {S(67, "", "o:Graph!")};
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
        x.name = "KeySvc";
        x.slots = {S(5, "GVOVV", "b", "Res"),
                   S(36, "VVV", "v", "", [](Call &c) -> uint32_t {
                       uint32_t *vec = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c.a[0]));
                       const uint32_t n = c.Rand(1) % 5;
                       if (n == 0) return 0;
                       uint32_t *buf = static_cast<uint32_t *>(::operator new(n * 16));
                       for (uint32_t i = 0; i < n; ++i) {
                           buf[i * 4 + 0] = c.Rand(10 + i) % 4 ? 0xac598eacu : c.Rand(20 + i);
                           buf[i * 4 + 1] = c.Rand(30 + i) % 50;
                           buf[i * 4 + 2] = c.Rand(40 + i) % 50;
                           buf[i * 4 + 3] = c.Rand(50 + i) % 50;
                       }
                       vec[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
                       vec[1] = vec[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf + n * 4));
                       return 0;
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Res";
        x.count = 3;
        x.slots = {S(23, "", "i{0,1,2,4,6,0xff}")};
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

DIFF_TEST(simantics_gsc_case61, 0x58bc50)
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
    const Summary s = RunSeeds(ctx, 61, 12000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, "LogObj");
        RandomArgs(seed, {{0, 1, 2, 5}, {0, 1, 2, 5, -1}, {0, 1, 2, 3, 0x100}});
    });
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->where = it->value;
    Report(s);
    if (!s.threw || s.byResult.size() < 2) ctx.Fail("the worlds did not reach every outcome");
}
