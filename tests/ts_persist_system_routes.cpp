// cTSPersistSystem's RVA-hook adapters (lane GD, stage B). The game reaches cTSPersistSystem only through the game's
// vtables, i.e. through these adapters (the constructor hook 0x1d8b90 is the bisect switch; with it off, or for an object
// built before the hooks, no object carries our vtables). An adapter must run our body for its slot without going through
// the object's vtable: in game that is the adapter's own hook, an endless loop.
//
// The adapters are `[[msvc::forceinline_calls]]` qualified calls, so each one is a copy of its body, not a jump to it: no
// static check can tie it to our vtable's function. The check is behavioural, on the functional tests' adapter route
// (tests/ts_persist_system_shape.h, route 2): while the adapter runs, the slot of the vtable the object carries (a
// fixture's fake table in place, or a copy of a game vtable) holds an arity-matched trap, and a hit fails the test. This
// test makes sure every hooked adapter went through that: each must have run on route 2 with its trap installed. Tests
// that ran before it in this run count; for the rest it runs the tests registered for that RVA, route 2 only.
#include "ts_persist_system_shape.h"

#include <cstdint>
#include <cstring>

namespace {

struct AdapterRow {
    uint32_t rva;
    const char *name;
};

// Every RVA hook of src/hook/tables/cTSPersistSystem.cpp but the constructor / destructor adapters (FX's tests) and the
// four non-virtual members (hooked at the member). Not listed: SetKeyedObject 0x1e5200 and vf168_56 0x1e3b10, verified by
// byte match through our vtable (matchcheck v8) and without a difftest, so no test runs their adapters (a gap: lane GD's notes).
const AdapterRow kAdapters[] = {
    {0x1e0930, "GetServicePriority"},
    {0x1dae50, "GetResourceManager"},
    {0x1dea20, "ClearDeferWrites"},
    {0x1dae80, "LockSystem"},
    {0x1dea10, "UnlockSystem"},
    {0x1e5e40, "StopBackgroundOpen"},
    {0x1e2d50, "vf56"},
    {0x1e0890, "vf3"},
    {0x1dfd00, "vf49"},
    {0x1dd9d0, "vf52"},
    {0x1defd0, "ClearSpeculativeLoader"},
};

} // namespace

DIFF_TEST(TSPersistSystemGD_Adapters, 0x1e0930)
{
    unsigned reran = 0;
    for (const AdapterRow &a : kAdapters) {
        ++ctx.cases;
        unsigned f = 0, s = 0;
        if (!psshape::FindSlot(a.rva, f, s)) {
            ctx.Fail("%s 0x%x: in no slot of the game's six vtables", a.name, a.rva);
            continue;
        }
        if (psshape::gTrapped.count(a.rva)) continue;
        for (const difftest::Test &t : difftest::Registry()) {
            if (t.rva != a.rva || std::strncmp(t.name, "TSPersistSystem", 15) != 0 || !std::strcmp(t.name, "TSPersistSystemGD_Adapters"))
                continue;
            ++reran;
            psshape::gOnlyRoute = 2;
            t.body(ctx);
            psshape::gOnlyRoute = 0;
        }
        if (!psshape::gTrapped.count(a.rva))
            ctx.Fail("%s 0x%x (face +0x%x slot %u): no test ran its adapter with the trap in its slot", a.name, a.rva,
                     psshape::kFaces[f], s);
    }
    std::printf("    TSPersistSystemGD_Adapters: %zu adapters, %u tests rerun on route 2\n", std::size(kAdapters), reran);
}
