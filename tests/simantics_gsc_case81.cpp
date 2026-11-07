// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 81 (src/simantics/PrimGenericSimCallI.cpp, Case81): hands the
// resources two outfit-table rows point to over to the entry of a sim. Recording-fake world (tests/simantics_gsc_world.h): the
// text service fills a vector of rows (nine references and a tenth word) that the original frees through the game allocator
// (global 0x10c95d4, a quiet fake); the row indices are kept inside the eight rows the fake always gives (the original and ours read
// the row at the index without a bounds check).
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
        x.slots = {S(0, "VO", "b", "Iface")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Iface";
        x.slots = {S(10, "VV", "h{0,1,5,0x100,-3,0x7cff}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Single";
        x.slots = {S(9, "", "o:Entries!")};
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
        x.slots = {S(10, "G", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "KeySvc";
        x.slots = {S(5, "GVOVV", "b", "Table")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Table";
        x.count = 2;
        x.slots = {S(12, "", "i{0x10,0x10,0x10,8,0}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "TextSvc";
        x.slots = {S(26, "VVVVVVVVVV", "v", "", [](Call &c) -> uint32_t {
                       // always eight rows of nine references (the column 2 one is the holder) and a tenth word
                       uint32_t *vec = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c.a[1]));
                       const uint32_t n = 8;
                       uint8_t *buf = static_cast<uint8_t *>(::operator new(n * 0x28));
                       std::memset(buf, 0, n * 0x28);
                       for (uint32_t row = 0; row < n; ++row) {
                           void **refs = reinterpret_cast<void **>(buf + row * 0x28);
                           for (int i = 0; i < 9; ++i) {
                               if (i != 2 && c.Rand(10 + row * 16 + i) % 3 != 0) continue;
                               if (i == 2 && c.Rand(300 + row) % 6 == 0) continue; // sometimes no holder
                               Fake *item = Instance("Holder", static_cast<int>((c.Rand(100 + row * 16 + i) >> 8) % 3));
                               ++item->refs;
                               refs[i] = item;
                           }
                           refs[9] = (c.Rand(77 + row) & 1) ? reinterpret_cast<void *>(0x1badf00d) : nullptr;
                       }
                       vec[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
                       vec[1] = vec[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf + n * 0x28));
                       return 0;
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Holder";
        x.count = 3;
        x.slots = {S(0, "VO", "v", "Typed?"), S(3, "p", "p", "", [](Call &c) -> uint32_t {
                       uint32_t *key = reinterpret_cast<uint32_t *>(c.self->data);
                       for (int i = 0; i < 4; ++i) key[i] = c.Rand(i + 1) % 30;
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key));
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Typed";
        x.count = 2;
        x.slots = {S(4, "", "i{8,8,8,0,1}")};
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

DIFF_TEST(simantics_gsc_case81, 0x58bc50)
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
    put(0x10cc8ec, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("TextSvc"))));
    put(0x10faf6c, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("KeySvc"))));
    put(0x10cc288, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Single"))));
    put(0x10c95d4, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Alloc"))));
    put(0x10c95d8, 0x80000000u);
    const Summary s = RunSeeds(ctx, 81, 10000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, {{0, 1, 2, 5, 0x100}, {0, 1, 2, 3, 4, 5, 6, 7}, {-1, -1, 0, 1, 2, 3, 4, 5, 6, 7}}, true);
    });
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->where = it->value;
    Report(s);
    if (!s.byResult.size()) ctx.Fail("the worlds did not reach every outcome");
}
