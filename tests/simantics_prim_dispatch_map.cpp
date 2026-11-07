// Differential test of the opcode -> handler mapping of cTSObject::vf332 (0x534650, src/simantics/PrimDispatch.cpp)
// with the real handlers on both sides (ours: the default table, the original: the unpatched image).
//
// The world is a trap world: `this` is a block whose every pointer field leads to a distinct trap object, the frame is
// another trap, and each virtual call on a trap logs (object, slot) and throws a 16-bit code, so a handler stops at its
// first virtual call. The operand reader's slot (7) also logs its first two arguments (scope, id), which come from the
// random param words. Which trap object and slot a handler reaches first, and with which operand, identifies it well
// enough to catch an opcode wired to the wrong handler (the handlers themselves are verified by their own tests).
#include "simantics/PrimDispatch.h"
#include "simantics_prim_dispatch.h"

#include <set>

namespace {

using namespace pd;

constexpr int kFields = 0x3000 / 4;

struct Trap {
    void **vt;
};
Trap gTraps[kFields + 8];
alignas(16) uint8_t gThis[0x3000];
void *gTrapVt[kSlots];
void *gSecondaryVt[kSlots];
uint8_t gParam[0x40];

struct FakeObj {
    void **vt;
    uint32_t pad[3];
};
FakeObj gNode;

uint32_t TrapId(const void *self)
{
    if (self >= gTraps && self < gTraps + kFields + 8) return static_cast<uint32_t>(static_cast<const Trap *>(self) - gTraps);
    if (self == gThis) return 0xfff0;
    if (self == gThis + 4) return 0xfff1;
    return 0xfffe;
}

template <int S> uint32_t __fastcall TrapThunk(void *self, void *)
{
    gTrace.push_back(TrapId(self));
    gTrace.push_back(S);
    throw static_cast<short>(0x4000 + S);
}

// Slot 7 of the secondary vtable: the operand reader (scope, id, ...).
template <int S> uint32_t __fastcall SecondaryThunk(void *self, void *, uint32_t a1, uint32_t a2)
{
    gTrace.push_back(TrapId(self));
    gTrace.push_back(S);
    if constexpr (S == 7) {
        gTrace.push_back(a1);
        gTrace.push_back(a2);
    }
    throw static_cast<short>(0x4000 + S);
}

template <int... S> void FillTraps(std::integer_sequence<int, S...>)
{
    ((gTrapVt[S] = reinterpret_cast<void *>(&TrapThunk<S>), gSecondaryVt[S] = reinterpret_cast<void *>(&SecondaryThunk<S>)), ...);
}

void Setup()
{
    EnsureVtable();
    FillTraps(std::make_integer_sequence<int, kSlots>());
    for (Trap &t : gTraps) t.vt = gTrapVt;
    std::memset(gThis, 0, sizeof(gThis));
    *reinterpret_cast<void **>(gThis) = gTrapVt;
    *reinterpret_cast<void **>(gThis + 4) = gSecondaryVt;
    for (int k = 2; k < kFields; ++k) *reinterpret_cast<void **>(gThis + 4 * k) = &gTraps[k];
    gNode.vt = gVtable;
    for (uint32_t i = 0; i < 32; ++i) gObjects[i] = nullptr;
    gObjects[kNode] = &gNode;
}

using ObjDispatch = uint8_t(__thiscall *)(void *self, void *frame, void *node, int32_t *result);

} // namespace

DIFF_TEST(PrimDispatch_Mapping, 0x534650)
{
    auto original = ctx.Original<ObjDispatch>(0x534650);
    Setup();
    void *const self = gThis;
    void *const frame = &gTraps[kFields + 1];
    void *const node = &gNode;
    const PrimHandlerTable &table = DefaultPrimHandlers();

    std::mt19937 r(0x7e);
    std::set<std::vector<uint32_t>> distinct;
    int stoppedAtTrap = 0, total = 0;

    auto run = [&](int opcode16) {
        for (auto &b : gParam) b = static_cast<uint8_t>(r());
        Config &c = gCfg;
        c = Config();
        c.seed = r();
        c.opcodeWord = (r() & 0xffff0000u) | static_cast<uint16_t>(opcode16);
        c.param = gParam;
        int32_t resA = 0x5a5a5a5a, resB = 0x5a5a5a5a;
        Outcome a = Run([&](Outcome &o) { o.ret = original(self, frame, node, &resA) & 0xff; });
        a.result = resA;
        Outcome b = Run([&](Outcome &o) { o.ret = DispatchPrimitive(self, frame, node, &resB, table) & 0xff; });
        b.result = resB;
        ++ctx.cases;
        ++total;
        if (a.exception >= 0x4000 && a.exception < 0x4000 + kSlots) ++stoppedAtTrap;
        if (opcode16 >= 0 && opcode16 < kPrimOpcodeCount) {
            // The trace without the node's two lookups identifies the handler.
            std::vector<uint32_t> sig;
            if (a.trace.size() > 4) sig.assign(a.trace.begin() + 4, a.trace.end());
            distinct.insert(sig);
        }
        if (!Same(a, b)) {
            char text[300];
            Describe(a, b, text, sizeof(text));
            ctx.Fail("opcode %04x: %s", static_cast<unsigned>(opcode16), text);
        }
    };

    for (int op = 0; op < kPrimOpcodeCount; ++op) {
        // 0x73 reads a cached singleton (a game global, null in the harness) before anything else, so the
        // original faults in this world; tests/simantics_prims_x73.cpp covers it with that global redirected.
        if (op == 0x73) continue;
        for (int k = 0; k < 24; ++k) run(op);
    }
    std::printf("      mapping: %d runs, %d stopped at a trap, %zu distinct first-call signatures over the opcodes\n", total, stoppedAtTrap,
                distinct.size());
}
