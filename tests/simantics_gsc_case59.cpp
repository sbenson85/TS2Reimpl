// Differential test for GenericSimCall (opcode 0x01, 0x58bc50) case 59 (src/simantics/PrimGenericSimCallH.cpp, Case59): the failed
// plastic surgery outfit pick. Recording-fake world (tests/simantics_gsc_world.h): the text, key and tag services, the property
// set, the outfit rows the text service fills into a std::vector, the random source (a real cRZRandom state both sides advance)
// and the sim's owner chain are fakes. The vector's buffer is freed by the original through the game allocator's slot 4
// (global 0x10c95d4), which is a quiet fake that frees the test's buffer.
#include "simantics_gsc_world.h"

#include "simantics/PrimGenericSimCallMisc.h"

using namespace gscw;

namespace {

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(0, "VO", "b", "Iface")}));
    t.push_back(FrameType());
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable");
        t.push_back(c);
    }
    for (const char *name : {"EntryTable", "PeopleTable", "ObjectTable", "ObjectDefTable", "Table"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Outfit240";
        x.slots = {S(65, "", "b")};
        t.push_back(x);
    }
    {
        // this+0x48: slot 55 answers a cRZRandom (12 bytes) living in the fake's scratch memory
        TypeSpec x;
        x.name = "LogObj";
        x.slots = {S(55, "", "p", "", [](Call &c) -> uint32_t {
                       uint32_t *state = reinterpret_cast<uint32_t *>(c.self->data);
                       state[0] = c.Rand(1);
                       state[1] = c.Rand(2);
                       state[2] = 0;
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(state));
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Iface";
        x.slots = {S(8, "", "o:Owner!"), S(10, "VV", "h{0,1,5,0x100,-3}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Owner";
        x.slots = {S(61, "", "o:Sim!"), S(9, "VVO", "v", "Component?")};
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
        x.slots = {S(19, "SVO", "v", "Table?"), S(36, "VV", "v"),
                   S(25, "VVV", "v", "", [](Call &c) -> uint32_t {
                       // fills the vector (three words: first, last, end) with 0..3 rows of nine references and a tenth word
                       uint32_t *vec = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c.a[1]));
                       const uint32_t n = c.Rand(1) % 4;
                       if (n == 0) return 0;
                       uint8_t *buf = static_cast<uint8_t *>(::operator new(n * 0x28));
                       std::memset(buf, 0, n * 0x28);
                       for (uint32_t row = 0; row < n; ++row) {
                           void **refs = reinterpret_cast<void **>(buf + row * 0x28);
                           for (int i = 0; i < 9; ++i) {
                               if (i != 0 && c.Rand(10 + row * 16 + i) % 3 == 0) continue;
                               Fake *item = Instance("RowItem", static_cast<int>((c.Rand(100 + row * 16 + i) >> 8) % 4));
                               ++item->refs;
                               refs[i] = item;
                           }
                           refs[9] = (c.Rand(77 + row) & 1) ? reinterpret_cast<void *>(0x1badf00d) : nullptr; // not a pointer: never released
                       }
                       vec[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf));
                       vec[1] = vec[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf + n * 0x28));
                       return 0;
                   }),
                   S(8, "VVVVV", "b")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "RowItem";
        x.count = 4;
        x.slots = {S(21, "", "p", "", [](Call &c) -> uint32_t {
                       // a resource key: type, group, instance, 0 (small numbers so the name stays short)
                       uint32_t *key = reinterpret_cast<uint32_t *>(c.self->data);
                       key[0] = c.Rand(1) % 100;
                       key[1] = c.Rand(2) % 50;
                       key[2] = c.Rand(3) % 7;
                       key[3] = 0;
                       return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key));
                   })};
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
        x.name = "Property";
        x.count = 2;
        x.slots = {S(24, "", "i{1,1,1,0,2}"), S(23, "VVVV", "i{0,1,0xffff,0x100}"), S(22, "V", "i{0,1,7,0x12345}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "TagSvc";
        x.slots = {S(3, "V", "v"), S(5, "VS", "v"), S(48, "SVV", "v"), S(12, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Alloc";
        TypeSpec y;
        Slot free = S(4, "V", "v", "", [](Call &c) -> uint32_t {
            ::operator delete(reinterpret_cast<void *>(static_cast<uintptr_t>(c.a[0])));
            return 0;
        });
        free.quiet = true;
        x.slots = {free};
        t.push_back(x);
    }
    return t;
}

} // namespace

DIFF_TEST(simantics_gsc_case59, 0x58bc50)
{
    Setup(ctx, Types());
    uint32_t *const textCache = ctx.image.At<uint32_t>(0x10cc8ec);
    uint32_t *const keyCache = ctx.image.At<uint32_t>(0x10faf6c);
    uint32_t *const tagCache = ctx.image.At<uint32_t>(0x10faf70);
    uint32_t *const allocator = ctx.image.At<uint32_t>(0x10c95d4);
    uint32_t *const allocatorGuard = ctx.image.At<uint32_t>(0x10c95d8);
    const uint32_t saved[5] = {*textCache, *keyCache, *tagCache, *allocator, *allocatorGuard};
    *textCache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("TextSvc")));
    *keyCache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("KeySvc")));
    *tagCache = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("TagSvc")));
    *allocator = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Alloc")));
    *allocatorGuard = 0x80000000u; // (below any thread epoch: the lazy allocator initialisation is skipped)
    const Summary s = RunSeeds(ctx, 59, 10000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", "Outfit240", "LogObj");
        RandomArgs(seed, {});
    });
    *textCache = saved[0];
    *keyCache = saved[1];
    *tagCache = saved[2];
    *allocator = saved[3];
    *allocatorGuard = saved[4];
    Report(s);
    if (!s.threw || s.byResult.size() < 2) ctx.Fail("the worlds did not reach every outcome");
}
