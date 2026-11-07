// MemberAddress (src/hook/hooks.h) on single- and multiple-inheritance member pointers. Not differential (no original
// function): rva 0. The refusals are compile-time (static_assert in MemberAddress<P>()), so they are checked here on
// the parse they rest on (hook_detail::ThisAdjustment), with the values MSVC's layout gives.
#include "difftest.h"
#include "hook/hooks.h"

namespace {

struct Single {
    int value = 7;
    __declspec(noinline) int Get(int add) { return value + add; }
};

struct PrimaryBase {
    virtual ~PrimaryBase() = default;
    int primary = 11;
    __declspec(noinline) int Primary(int add) { return primary + add; }
};

struct SecondaryBase {
    virtual ~SecondaryBase() = default;
    int secondary = 23;
    __declspec(noinline) int Secondary(int add) { return secondary + add; }
};

struct FarBase { // at offset 16 in Both: a two-letter encoded adjustment ("BA@")
    char pad[20];
    __declspec(noinline) int Far(int add) { return pad[0] + add; }
};

struct Both : PrimaryBase, SecondaryBase, FarBase {
    int own = 41;
    __declspec(noinline) int Own(int add) { return own + add; }
    virtual int Virtual(int add) { return own + add + 1; }
    __declspec(noinline) int NoArgs() { return own; }
};

using ThisCall = int(__thiscall *)(void *, int);
using BothFn = int (Both::*)(int);

static_assert(sizeof(&Single::Get) == 4 && sizeof(BothFn) == 8, "MSVC member pointer sizes");
static_assert(offsetof(Both, own) > 16 && sizeof(SecondaryBase) == 8, "layout the adjustments below assume");

// As a hook table holds them: static data the compiler fills without a dynamic initializer.
void *const kAddresses[] = {
    MemberAddress(&Single::Get),
    MemberAddress<&Single::Get>(),
    MemberAddress<static_cast<BothFn>(&Both::Own)>(),
    MemberAddress<static_cast<BothFn>(&Both::Primary)>(),
};

} // namespace

DIFF_TEST(HookMemberAddress, 0)
{
    Single s;
    Both b;
    struct Case {
        const char *what;
        void *code;
        void *self;
        int expect;
    } cases[] = {
        {"single inheritance, function argument", kAddresses[0], &s, 8},
        {"single inheritance, template argument", kAddresses[1], &s, 8},
        {"own member of a multiple-inheritance class", kAddresses[2], &b, 42},
        {"primary base member", kAddresses[3], &b, 12},
    };
    for (const Case &c : cases) {
        ++ctx.cases;
        if (!c.code) ctx.Fail("%s: no address", c.what);
        else if (int got = reinterpret_cast<ThisCall>(c.code)(c.self, 1); got != c.expect)
            ctx.Fail("%s: called with the object's address, got %d, expected %d", c.what, got, c.expect);
    }

    // The parse behind MemberAddress<P>()'s compile-time refusals, against MSVC's own decorated names (evaluated at run
    // time here, so a broken parse fails this test rather than the build).
    using namespace hook_detail;
    struct Parse {
        const char *what;
        const char *name;
        int expect;
    } parses[] = {
        {"own member", DecoratedName<static_cast<BothFn>(&Both::Own), 0x5a5a>(), 0},
        {"primary base member", DecoratedName<static_cast<BothFn>(&Both::Primary), 0x5a5a>(), 0},
        {"own member without arguments", DecoratedName<&Both::NoArgs, 0x5a5a>(), 0},
        {"secondary base member (offset 8)", DecoratedName<static_cast<BothFn>(&Both::Secondary), 0x5a5a>(), 8},
        {"far base member (offset 16)", DecoratedName<static_cast<BothFn>(&Both::Far), 0x5a5a>(), 16},
        {"virtual function (vcall thunk)", DecoratedName<static_cast<BothFn>(&Both::Virtual), 0x5a5a>(), kAdjustmentVirtual},
        {"hashed long name", "??@0123456789abcdef0123456789abcdef@", kAdjustmentUnreadable},
    };
    for (const Parse &p : parses) {
        ++ctx.cases;
        if (int got = ThisAdjustmentFromDecoratedName(p.name); got != p.expect)
            ctx.Fail("%s: adjustment %d, expected %d (%s)", p.what, got, p.expect, p.name);
    }
    static_assert(ThisAdjustment<static_cast<BothFn>(&Both::Own)>() == 0, "the constant evaluation MemberAddress uses");
}
