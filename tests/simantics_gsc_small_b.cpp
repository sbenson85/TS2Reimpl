// Differential tests for the shorter GenericSimCall (opcode 0x01, 0x58bc50) cases that post framework messages or hold references:
// 0, 28, 37, 40, 44, 45, 51, 72 (messages), 24, 30, 41, 42, 47 (virtual-base holders), 48, 74, 75, 76, 67, 78 (key lookups) and
// 54..58 (the per-person store). One shared recording-fake world (tests/simantics_gsc_world.h), one test per case.
#include "rz/RZMessageServer.h"
#include "simantics_gsc_world.h"

using namespace gscw;

namespace {

uint32_t __cdecl StubMessageServer()
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("MsgServer")));
}

const char kGuidText[] = "{12345678-1234-1234-1234-123456789abc}";

std::vector<TypeSpec> Types()
{
    std::vector<TypeSpec> t;
    t.push_back(SelfType({S(0, "VO", "b", "OIface"), S(9, "VVO", "v", "VBH?"), S(61, "", "o:Sim61!")}));
    {
        TypeSpec f;
        f.name = "Frame";
        f.slots = {S(12, "", "h"), S(3, "", "h{0,1,0x1234,-1}")};
        t.push_back(f);
    }
    {
        TypeSpec c;
        c.name = "Context";
        c.slots = PrologueSlots("Entries", "People", "Objects", "Defs");
        c.slots.push_back(S(3, "", "o:Gate3!"));
        c.slots.push_back(S(4, "", "o:Store4?"));
        c.slots.push_back(S(6, "", "o:Lookup6!"));
        c.slots.push_back(S(8, "", "o:List8!"));
        c.slots.push_back(S(23, "", "o:Holder23!"));
        t.push_back(c);
    }
    for (const char *name : {"Entries", "Defs", "Lookup6", "Thing0", "Val", "Inner45a", "Person"}) {
        TypeSpec x;
        x.name = name;
        t.push_back(x);
    }
    t.back().slots = {S(15, "", "i{0,1,0x77}")};
    {
        TypeSpec x;
        x.name = "Gate3";
        x.slots = {S(21, "", "i{0,1,3,3,4,5}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "People";
        x.slots = {Lookup(16, "VV", "Person", 20)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Objects";
        x.slots = {Lookup(29, "V", "Obj", 12), Lookup(43, "V", "Obj", 12), S(39, "", "o:Info45?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Obj";
        x.count = 2;
        x.slots = {S(0, "VO", "b", "OIface"), S(9, "VVO", "v", "VBH?"), S(61, "", "o:Chain45!"), S(64, "", "o:Kind!"),
                   S(86, "p", "v", "", [](Call &c) -> uint32_t {
                       uint32_t *w = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(c.a[0]));
                       for (int i = 0; i < 4; ++i) w[i] = c.Rand(i + 1);
                       return 0;
                   }),
                   S(8, "", "o:Owner67!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Kind";
        x.slots = {S(3, "V", "i{0xd07b9ce9,0xd07b9ce9,0,5}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "OIface";
        x.slots = {S(10, "VV", "h{0,1,5,0x100,-3,0x7cff}"), S(43, "", "b")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Holder23";
        x.slots = {S(6, "", "i{0,1,0,2}"), S(9, "", "i{0,1}"), S(5, "", "i{0x0bea28c2,0x2c1805b0,5}"), S(10, "", "o:Thing48!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Thing48";
        x.slots = {S(8, "", "i{0,5,0x77}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "List8";
        x.slots = {Lookup(9, "V", "Thing0", 30)};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Sim61";
        x.slots = {S(28, "", "i{0,1,0x1234}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Chain45";
        x.slots = {S(54, "", "o:Sub54!"), S(4, "", "o:T15!"), S(67, "V", "o:Val!"), S(3, "", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Sub54";
        x.slots = {S(7, "t", "v"), S(12, "T", "v"), S(4, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "T15";
        x.slots = {S(3, "V", "o:Inner45a!")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Info45";
        x.slots = {S(8, "", "o:Inner45!"), S(57, "", "h{0,1,0x7fff,-1,0x100}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Inner45";
        x.slots = {S(47, "", "h{0,1,0x7fff,-1,0x100}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Store4";
        x.slots = {S(17, "VVV", "v", "", [](Call &c) -> uint32_t {
                       *reinterpret_cast<int32_t *>(static_cast<uintptr_t>(c.a[1])) = static_cast<int32_t>(c.Rand(1) % 20) - 5;
                       *reinterpret_cast<int32_t *>(static_cast<uintptr_t>(c.a[2])) = static_cast<int32_t>(c.Rand(2) % 20) - 5;
                       return 0;
                   }),
                   S(19, "VV", "v", "", [](Call &c) -> uint32_t {
                       *reinterpret_cast<int32_t *>(static_cast<uintptr_t>(c.a[1])) = static_cast<int32_t>(c.Rand(3) % 20) - 5;
                       return 0;
                   }),
                   S(50, "VVV", "v"), S(51, "VV", "v")};
        t.push_back(x);
    }
    {
        // a holder queried through the object's slot 9; its virtual base answers the second interface (slot 0 of the sub-object)
        TypeSpec x;
        x.name = "VBH";
        x.vbase = true;
        x.count = 2;
        x.subSlots = {S(0, "VO", "b", "VBH2?")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "VBH2";
        x.vbase = true;
        x.slots = {S(6, "V", "v"), S(7, "V", "v"), S(3, "GV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Owner67";
        x.slots = {S(9, "VVO", "b", "Held67")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Held67";
        x.vbase = true;
        x.slots = {S(0, "", "b"), S(12, "E", "i")};
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
        x.name = "TextSvc";
        x.slots = {S(10, "GVV", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "KeySvc";
        x.slots = {S(11, "V", "i{0,1,7,0x1234}"), S(3, "", "o:Lookup48!"), S(5, "GVOVV", "b", "Res76")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Lookup48";
        x.slots = {S(3, "GVOVV", "b", "Held48")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Held48";
        x.slots = {S(32, "VV", "i", "", [](Call &c) -> uint32_t {
                       *reinterpret_cast<int32_t *>(static_cast<uintptr_t>(c.a[1])) = static_cast<int32_t>(c.Rand(1) % 200) - 20;
                       return 0;
                   }),
                   S(25, "VV", "i", "", [](Call &c) -> uint32_t {
                       *reinterpret_cast<int32_t *>(static_cast<uintptr_t>(c.a[1])) = static_cast<int32_t>(c.Rand(2) % 200) - 20;
                       return 0;
                   })};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Res76";
        x.slots = {S(22, "", "i{0,1,2,0x10,0x40}"), S(23, "", "i{0,1,2,3}")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "Msg";
        x.slots = {S(1, "", "i{1}"), S(2, "", "i{0}"), S(5, "V", "v"), S(11, "V", "v"), S(12, "D", "v"), S(16, "V", "v"), S(17, "G", "v"),
                   S(21, "V", "v"), S(33, "V", "v")};
        t.push_back(x);
    }
    {
        TypeSpec x;
        x.name = "MsgServer";
        x.slots = {S(3, "p", "v"), S(4, "pV", "v")};
        t.push_back(x);
    }
    return t;
}

void RunSmall(difftest::Context &ctx, int which, const std::vector<std::vector<int32_t>> &pools)
{
    Setup(ctx, Types());
    CodePatch patchServer(ctx, 0x9c5a89, reinterpret_cast<void *>(&StubMessageServer));
    CodePatch patchServerOurs(reinterpret_cast<void *>(&GetCachedMessageServer), reinterpret_cast<void *>(&StubMessageServer)); // ours
    MessageSupport messages(ctx);
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
    put(0x10cc2a8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(InstancePtr("Slot23"))));
    put(0x10171cc, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kGuidText)));
    put(0x10faf94, 0x18u); // the option mask (bit 3 set)
    const Summary s = RunSeeds(ctx, which, 4000, "Frame", [&](uint32_t seed) {
        SetObjectFields("Context", nullptr, nullptr);
        RandomArgs(seed, pools);
    });
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) *it->where = it->value;
    Report(s);
    if (!s.byResult.size()) ctx.Fail("the worlds did not reach every outcome");
}

const std::vector<int32_t> kIds = {0, 1, 2, 5, 0x100, -1, 0x7fff};

} // namespace

#define SMALL_CASE(N, ...)                         \
    DIFF_TEST(simantics_gsc_small_b##N, 0x58bc50)  \
    {                                              \
        RunSmall(ctx, N, {__VA_ARGS__});           \
    }

SMALL_CASE(0, kIds)
SMALL_CASE(28, kIds, kIds, {0, 1, 2, -1, 3})
SMALL_CASE(72, kIds, kIds, kIds)
SMALL_CASE(37, kIds)
SMALL_CASE(40, kIds)
SMALL_CASE(44, {0, 0, 1, 2})
SMALL_CASE(51, kIds)
SMALL_CASE(45, kIds)
SMALL_CASE(54, kIds)
SMALL_CASE(55, kIds)
SMALL_CASE(56, kIds)
SMALL_CASE(57, kIds)
SMALL_CASE(58, kIds)
SMALL_CASE(24, kIds)
SMALL_CASE(30, kIds)
SMALL_CASE(47, kIds)
SMALL_CASE(41, {0, 1, 2})
SMALL_CASE(42, kIds)
SMALL_CASE(48, kIds)
SMALL_CASE(74, kIds)
SMALL_CASE(75, kIds)
SMALL_CASE(76, kIds, kIds)
SMALL_CASE(67, kIds, kIds, kIds, kIds)
SMALL_CASE(78, kIds, kIds, {0, 1, 2})
