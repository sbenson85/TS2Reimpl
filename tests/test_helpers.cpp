// Checks of the shared test helpers themselves (lane GB): shape::MemberCode / VirtualMemberCode / CodeOf (tests/shape.h),
// shape::CodePatch / StandIn (tests/code_patch.h), and pworld's tables and patch set (tests/ts_persist_world.h). The RVA is
// the game's table constructor 0x1d8a50, the image function these checks patch and build tables with; nothing here
// verifies it.
#include "difftest.h"

#include "code_patch.h"
#include "ts_persist_world.h"

#include <cstring>
#include <set>
#include <string>

namespace {

// Member pointers: a class with two bases (two-word member pointers) and a class with virtual members.
struct LeftBase {
    int left = 11;
    __declspec(noinline) int LeftValue() { return left * 3; }
};
struct RightBase {
    int right = 22;
    __declspec(noinline) int RightValue() { return right * 5 + 1; }
};
struct TwoBases : LeftBase, RightBase {
    int own = 33;
    __declspec(noinline) int OwnValue() { return own * 7 + left + right; }
};
struct Poly {
    int v = 4;
    virtual ~Poly() = default;
    __declspec(noinline) virtual int First() { return v + 100; }
    __declspec(noinline) virtual int Second() { return v * 1000 + 7; }
};

using ThisCall = int(__fastcall *)(void *, void *);

// CodePatch / StandIn: our "replacement" of 0x1d8a50 and the stand-in both sides get.
int gOursCalls = 0, gStandCalls = 0;
void *gStandSelf = nullptr;
__declspec(noinline) void __fastcall OursBuild(void *, void *) { gOursCalls += 3; }
__declspec(noinline) void __fastcall StandBuild(void *self, void *)
{
    ++gStandCalls;
    gStandSelf = self;
}

} // namespace

DIFF_TEST(TestHelpers_CodeAndTables, 0x1d8a50)
{
    // ---- MemberCode on a class with two bases ----
    static_assert(sizeof(&TwoBases::OwnValue) == 2 * sizeof(void *), "two-word member pointer");
    TwoBases two;
    void *own = shape::MemberCode(&TwoBases::OwnValue);
    if (reinterpret_cast<ThisCall>(own)(&two, nullptr) != two.OwnValue()) ctx.Fail("MemberCode(OwnValue) is not the body");
    // A second base's member through the derived class's pointer: the code word is the base's body (the adjustment is the
    // other word, not applied here).
    int (TwoBases::*right)() = &TwoBases::RightValue;
    if (reinterpret_cast<ThisCall>(shape::MemberCode(right))(static_cast<RightBase *>(&two), nullptr) != two.RightValue())
        ctx.Fail("MemberCode(RightValue) is not the body");
    if (shape::MemberCode(&TwoBases::LeftValue) != shape::MemberCode(&LeftBase::LeftValue)) ctx.Fail("MemberCode differs between the base and derived pointer");
    ++ctx.cases;

    // ---- virtual members: the thunk is recognised, VirtualMemberCode gives the vtable's body ----
    Poly poly;
    void *const *vtable = *reinterpret_cast<void *const *const *>(&poly);
    uint32_t offset = ~0u;
    if (!shape::VcallThunkOffset(shape::MemberCodeWord(&Poly::Second), &offset) || offset != 8)
        ctx.Fail("Second's member pointer is not seen as a vcall thunk of slot 2 (offset 0x%x)", offset);
    if (shape::VcallThunkOffset(own, &offset)) ctx.Fail("OwnValue's body taken for a vcall thunk");
    void *first = shape::VirtualMemberCode(&Poly::First, vtable);
    void *second = shape::VirtualMemberCode(&Poly::Second, vtable);
    if (first != vtable[1] || second != vtable[2]) ctx.Fail("VirtualMemberCode is not the vtable's entry");
    if (reinterpret_cast<ThisCall>(second)(&poly, nullptr) != 4007 || reinterpret_cast<ThisCall>(first)(&poly, nullptr) != 104)
        ctx.Fail("VirtualMemberCode's code does not run the bodies");
    if (shape::CodeOf(&OursBuild) != reinterpret_cast<void *>(&OursBuild)) ctx.Fail("CodeOf(function)");
    ++ctx.cases;

    // ---- StandIn: both sides reach the stand-in while it lives, and the bytes come back ----
    uint8_t *image = ctx.image.At<uint8_t>(0x1d8a50);
    uint8_t savedImage[5], savedOurs[5];
    std::memcpy(savedImage, image, 5);
    std::memcpy(savedOurs, reinterpret_cast<void *>(&OursBuild), 5);
    uint8_t scratch[0x20] = {};
    {
        shape::StandIn both(0x1d8a50, &OursBuild, &StandBuild);
        ctx.Original<void(__thiscall *)(void *)>(0x1d8a50)(scratch);
        OursBuild(scratch + 4, nullptr);
    }
    if (gStandCalls != 2 || gOursCalls != 0 || gStandSelf != scratch + 4) ctx.Fail("StandIn: stand %d, ours %d", gStandCalls, gOursCalls);
    if (std::memcmp(savedImage, image, 5) || std::memcmp(savedOurs, reinterpret_cast<void *>(&OursBuild), 5)) ctx.Fail("StandIn left bytes patched");
    OursBuild(scratch, nullptr);
    if (gOursCalls != 3) ctx.Fail("ours does not run after the StandIn");
    ++ctx.cases;

    // ---- pworld: a real table, filled and dumped ----
    {
        pworld::Patches patches;
        patches.Heap();
        void *table = pworld::gPrimary + 0xf4;
        pworld::BuildTable(ctx, table);
        static int values[3];
        auto *a = pworld::Emplace<void *>(ctx, 0x1d7df0, table, "alpha");
        a->value = &values[0];
        pworld::Emplace<void *>(ctx, 0x1d7df0, table, "a key long enough for the heap")->value = &values[1];
        auto *again = pworld::Emplace<void *>(ctx, 0x1d7df0, table, "alpha");
        if (again != a) ctx.Fail("Emplace of an existing key gave another node");
        const auto dump = pworld::DumpTable<void *>(table, [&](void *v) { return std::to_string(static_cast<int *>(v) - values); });
        const std::set<std::string> entries(dump.begin(), dump.end());
        const std::set<std::string> expected{"alpha=0", "a key long enough for the heap=1", "size 0x2"};
        if (dump.size() != 3 || entries != expected) ctx.Fail("table dump: %s", dump.empty() ? "" : dump.front().c_str());
    }
    ++ctx.cases;
}
