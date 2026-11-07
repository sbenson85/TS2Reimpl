// Differential tests for the SimAntics primitive dispatch (src/simantics/PrimDispatch.h): cTSObject::vf332 0x534650,
// cTSPerson::vf332 0x51e4c0, and the opcode -> handler mapping against the real handlers.
//
// Handlers are replaced by stubs: the original's call targets (0x58bc50 ...) are patched in the mapped image to stubs
// that log (tag, this, frame, param) and answer with a hash (or throw); our side runs DispatchPrimitive with a table of
// the same stubs, plus the six OriginalFunction wrappers of the default table, which reach the patched image. Both sides
// run on one scripted world of recording fake objects, so the logs, results and exceptions can be compared.
#include "simantics/PrimDispatch.h"
#include "simantics_prim_dispatch.h"

#include <memory>

namespace {

using namespace pd;

struct FakeObj {
    void **vt;
    uint32_t pad[3];
};
FakeObj gFake[kObjectCount];
uint8_t gParam[0x20];

void SetupObjects()
{
    EnsureVtable();
    EnsureStubs();
    for (uint32_t i = 0; i < kObjectCount; ++i) {
        gFake[i].vt = gVtable;
        gObjects[i] = &gFake[i];
    }
}

// The opcode -> rva map of the nine-and-thirty handlers (docs/subsystems.md; the 0x534aac byte table decoded).
struct HandlerRva {
    int opcode;
    uint32_t rva;
    bool replaced; // ours exists (a table entry of ours); otherwise the default table reaches the patched image
};
const HandlerRva kHandlers[] = {
    {0x00, 0x5988a0, true},  {0x01, 0x58bc50, true}, {0x02, 0x5890c0, true},  {0x07, 0x5a86b0, true},  {0x08, 0x5a1060, true},
    {0x0b, 0x588eb0, true},  {0x0c, 0x588bb0, true},  {0x0d, 0x5a0560, true},  {0x0e, 0x58a2c0, true},  {0x0f, 0x5a7bb0, true},
    {0x10, 0x58ae50, true},  {0x12, 0x59cba0, true},  {0x14, 0x580360, true},  {0x17, 0x59f370, true},  {0x19, 0x5801b0, true},
    {0x1a, 0x59e6e0, true},  {0x1c, 0x53de40, true},  {0x1f, 0x5a12c0, true},  {0x20, 0x5a79f0, true},  {0x21, 0x58a110, true},
    {0x22, 0x5a7c60, true},  {0x23, 0x5a87f0, true},  {0x24, 0x5879a0, true},  {0x2a, 0x585a60, true},  {0x2e, 0x5a5210, true},
    {0x30, 0x59d590, true},  {0x31, 0x59eb90, true},  {0x32, 0x59d740, true},  {0x33, 0x598ac0, true}, {0x6d, 0x5827e0, true},
    {0x70, 0x5805c0, true}, {0x71, 0x5a6e40, true},  {0x73, 0x586560, true}, {0x76, 0x57f2b0, true},  {0x77, 0x59de90, true},
    {0x7a, 0x59efa0, true},  {0x7b, 0x585590, true},  {0x7c, 0x5a8b30, true},  {0x7e, 0x5a1260, true},
};

bool IsShared(int op)
{
    return op == 0x69 || op == 0x6a || op == 0x6b || op == 0x6c || op == 0x6e || op == 0x6f;
}

using ObjDispatch = uint8_t(__thiscall *)(void *self, void *frame, void *node, int32_t *result);

void RandomWorld(std::mt19937 &r, int opcode16)
{
    Config &c = gCfg;
    c = Config();
    c.seed = r();
    c.opcodeWord = (r() & 0xffff0000u) | static_cast<uint16_t>(opcode16);
    switch (r() % 3) {
    case 0: c.param = nullptr; break;
    case 1: c.param = gParam; break;
    default: c.param = reinterpret_cast<void *>(static_cast<uintptr_t>(r() | 1)); break;
    }
    const uint32_t s104[] = {0, 1, 0x100, 0xffffff00u, 0x80, r()};
    c.slot104 = s104[r() % 6];
    c.iface = gObjects[kIface];
    c.step = gObjects[kStep];
    c.target = (r() % 4 == 0) ? gObjects[kSelf] : gObjects[kTarget];
    const uint32_t s64[] = {0, 1, 0x100, 0x10000, 0x80000000u, r()};
    c.slot64 = s64[r() % 6];
    c.throwIn246 = r() % 8 == 0;
    c.tagThrow = 0x300 + r() % 16;
    gHandlerMode.throws = r() % 10 == 0;
    if (r() % 20 == 0) c.nodeThrows = (r() % 2) ? 3 : 16;
}

} // namespace

DIFF_TEST(PrimDispatch_Object, 0x534650)
{
    auto original = ctx.Original<ObjDispatch>(0x534650);
    SetupObjects();

    // Patch the original's handler targets to the stubs of their opcodes.
    std::vector<std::unique_ptr<ImagePatch>> patches;
    for (const HandlerRva &h : kHandlers) patches.push_back(std::make_unique<ImagePatch>(ctx, h.rva, reinterpret_cast<void *>(gStubs[h.opcode])));

    // Our table: the default one (original wrappers for the six), with the replaced handlers swapped for the same stubs.
    PrimHandlerTable table = DefaultPrimHandlers();
    for (const HandlerRva &h : kHandlers)
        if (h.replaced) table.mByOpcode[h.opcode] = gFakes[h.opcode];

    std::mt19937 r(0x534650);
    void *const self = gObjects[kSelf], *const frame = gObjects[kFrame], *const node = gObjects[kNode];
    int outcomes[2] = {0, 0};

    auto run = [&](int opcode16) {
        const uint32_t seed = r();
        std::mt19937 w1(seed), w2(seed);
        for (auto &b : gParam) b = static_cast<uint8_t>(r());
        RandomWorld(w1, opcode16);
        int32_t resA = 0x5a5a5a5a, resB = 0x5a5a5a5a;
        Outcome a = Run([&](Outcome &o) { o.ret = original(self, frame, node, &resA) & 0xff; });
        a.result = resA;
        RandomWorld(w2, opcode16);
        Outcome b = Run([&](Outcome &o) { o.ret = DispatchPrimitive(self, frame, node, &resB, table) & 0xff; });
        b.result = resB;
        ++ctx.cases;
        ++outcomes[a.ret ? 1 : 0];
        if (!Same(a, b)) {
            char text[300];
            Describe(a, b, text, sizeof(text));
            ctx.Fail("opcode word %08x: %s", gCfg.opcodeWord, text);
        }
    };

    // Every opcode around the table (negative ones sign-extend; 0x7f and up take the default case), several worlds each.
    for (int op = -4; op <= 0x90; ++op)
        for (int k = 0; k < (IsShared(op) ? 60 : 6); ++k) run(op);
    for (int op : {0xff, 0x100, 0x101, 0x7fff, 0x8000, 0x8001, 0xffff, 0xfff0, 0x1007e, 0x1000a})
        for (int k = 0; k < 6; ++k) run(static_cast<uint16_t>(op));
    std::mt19937 rr(77);
    for (int i = 0; i < 4000; ++i) run(static_cast<int>(rr() & 0xffff));
    if (outcomes[0] == 0 || outcomes[1] == 0) ctx.Fail("no case reached one of the outcomes (%d handled, %d default)", outcomes[1], outcomes[0]);
}
