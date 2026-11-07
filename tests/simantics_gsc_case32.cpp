// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 32 (src/simantics/PrimGenericSimCallG.cpp, Case32): counts
// written to argument 1. Recording-fake world (tests/simantics_gsc_world.h): the gate, the entry table, the entry, the object table,
// the object's chain and graph, the key service (listing keys into a vector the original frees through the game allocator, global
// 0x10c95d4, a quiet fake) and the property resource.
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
        c.slots = PrologueSlots("Entries", "PeopleTable", "Objects", "ObjectDefTable");
        c.slots.push_back(S(3, "", "o:Gate!"));
        t.push_back(c);
    }
    for (const char *name : {"PeopleTable", "ObjectDefTable"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Gate";
        x.slots = {S(15, "V", "h{0,1,5,0x100,-3,0x7cff}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entries";
        x.slots = {S(11, "V", "o:Entry?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Entry";
        x.count = 2;
        x.slots = {S(15, "VVVV", "i{0,1,2,0x10000,0x12345678,-1}")};
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
        x.slots = {S(61, "", "o:Chain?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Chain";
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
        x.name = "Slot23";
        x.slots = {S(11, "", "i{0,1,0x77,0x1234}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "KeySvc";
        x.slots = {S(36, "VVV", "v", "", [](Call &c) -> uint32_t {
                       uint32_t *vec = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c.a[0]));
                       const uint32_t n = c.Rand(1) % 6;
                       if (n == 0) return 0;
                       uint32_t *buf = static_cast<uint32_t *>(::operator new(n * 16));
                       for (uint32_t i = 0; i < n; ++i) {
                           buf[i * 4 + 0] = c.Rand(10 + i) % 3 ? 0xac598eacu : c.Rand(20 + i);
                           buf[i * 4 + 1] = c.Rand(30 + i);
                           buf[i * 4 + 2] = c.Rand(40 + i);
                           buf[i * 4 + 3] = c.Rand(50 + i) % 3 ? 0xb0858c5fu : c.Rand(60 + i);
                       }
                       vec[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
                       vec[1] = vec[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf + n * 4));
                       return 0;
                   }),
                   S(5, "GVOVV", "b", "Res")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Res";
        x.slots = {S(42, "VV", "p", "", [](Call &c) -> uint32_t {
                       // a span of two words (equal: empty), or none
                       const uint32_t r = c.Rand(NormWord(c.a[0]) + 77 * c.a[1]);
                       if (r % 4 == 0) return 0;
                       uint32_t *span = reinterpret_cast<uint32_t *>(c.self->data);
                       span[0] = 0x1000;
                       span[1] = (r % 4 == 1) ? 0x1000u : 0x1010u;
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(span));
                   }),
                   S(101, "V", "b")};
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

DIFF_TEST(simantics_gsc_case32, 0x58bc50)
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
    put(0x10cc8ec, 1u); // the text service cache: only touched, never used (any nonzero value keeps the getter from querying)
    put(0x10faf6c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("KeySvc"))));
    put(0x10cc2a8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Slot23"))));
    put(0x10c95d4, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Alloc"))));
    put(0x10c95d8, 0x80000000u);
    const Summary s = RunSeeds(ctx, 32, 12000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, -1, 0x100}, {-1, 0, 1, 2, 3, 0x10, 0x12, 0x13, 0x33, 0x7fff},
                          {0, 1, 2, -1}, {0, 0, -1, -1, 1, 5, 0x100, -2}});
    });
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->where = it->value;
    Report(s);
    if (!s.threw || s.byResult.size() < 1) ctx.Fail("the worlds did not reach every outcome");
}
