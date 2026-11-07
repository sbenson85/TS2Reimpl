// Differential test for cTSPerson::vf332 (0x51e4c0, src/simantics/PrimDispatch.h). The original's callees are patched
// in the mapped image to stubs shared with our side: 0x521860, 0x521b90 and 0x525520 (the person's own handlers,
// originals for ours as well) and the base dispatch 0x534650. The person's slots 361 and 362 and the frame's slot 12
// are recording fakes.
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

// The base dispatch: logs (0x2000, this, frame, node), writes a result and answers a byte.
uint8_t BaseBody(void *self, void *frame, void *node, int32_t *result)
{
    gTrace.push_back(0x2000);
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)));
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame)));
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node)));
    if (gHandlerMode.throws) throw static_cast<short>(0x7ff);
    *result = static_cast<int32_t>(Mix(gCfg.seed, 0x2000, 1));
    return static_cast<uint8_t>(Mix(gCfg.seed, 0x2000, 2));
}
uint8_t __fastcall BaseStub(void *self, void *, void *frame, void *node, int32_t *result)
{
    return BaseBody(self, frame, node, result);
}

uint8_t BaseFake(void *self, void *frame, void *node, int32_t *result)
{
    return BaseBody(self, frame, node, result);
}

using PersonDispatch = uint8_t(__thiscall *)(void *self, void *frame, void *node, int32_t *result);

void RandomWorld(std::mt19937 &r, int opcode16)
{
    Config &c = gCfg;
    c = Config();
    c.seed = r();
    c.opcodeWord = (r() & 0xffff0000u) | static_cast<uint16_t>(opcode16);
    switch (r() % 4) {
    case 0: c.param = nullptr; break;
    default: c.param = gParam; break;
    }
    const uint32_t frameWords[] = {0, 1, 0x10000, 0xffff0000u, 0x8000, r(), r()};
    c.frameWord = frameWords[r() % 7];
    gHandlerMode.throws = r() % 12 == 0;
    if (r() % 20 == 0) c.nodeThrows = (r() % 2) ? 3 : 16;
}

} // namespace

DIFF_TEST(PrimDispatch_Person, 0x51e4c0)
{
    auto original = ctx.Original<PersonDispatch>(0x51e4c0);
    EnsureVtable();
    EnsureStubs();
    for (uint32_t i = 0; i < kObjectCount; ++i) {
        gFake[i].vt = gVtable;
        gObjects[i] = &gFake[i];
    }

    std::vector<std::unique_ptr<ImagePatch>> patches;
    patches.push_back(std::make_unique<ImagePatch>(ctx, 0x521860, reinterpret_cast<void *>(gStubs[3])));
    patches.push_back(std::make_unique<ImagePatch>(ctx, 0x521b90, reinterpret_cast<void *>(gStubs[0x11])));
    patches.push_back(std::make_unique<ImagePatch>(ctx, 0x525520, reinterpret_cast<void *>(gStubs[0x1e])));
    patches.push_back(std::make_unique<ImagePatch>(ctx, 0x534650, reinterpret_cast<void *>(&BaseStub)));

    // Our side gets the same stubs through the handler table (the game's table holds lane Y's handlers).
    const PersonPrimHandlers fakes = {gFakes[3], gFakes[0x11], gFakes[0x1e]};

    std::mt19937 r(0x51e4c0);
    void *const self = gObjects[kSelf], *const frame = gObjects[kFrame], *const node = gObjects[kNode];
    int selectors[8] = {};

    auto run = [&](int opcode16) {
        const uint32_t seed = r();
        std::mt19937 w1(seed), w2(seed);
        for (auto &b : gParam) b = static_cast<uint8_t>(r());
        const uint16_t sels[] = {0, 1, 2, 3, 4, 0x8000, 0xffff, 0x0100, static_cast<uint16_t>(r())};
        const uint16_t sel = sels[r() % 9];
        std::memcpy(gParam + 4, &sel, 2);
        RandomWorld(w1, opcode16);
        int32_t resA = 0x5a5a5a5a, resB = 0x5a5a5a5a;
        Outcome a = Run([&](Outcome &o) { o.ret = original(self, frame, node, &resA) & 0xff; });
        a.result = resA;
        RandomWorld(w2, opcode16);
        Outcome b = Run([&](Outcome &o) { o.ret = DispatchPersonPrimitive(self, frame, node, &resB, BaseFake, fakes) & 0xff; });
        b.result = resB;
        ++ctx.cases;
        if (a.exception == 0xd) ++selectors[0];
        if (a.exception == 0x15) ++selectors[1];
        if (a.exception == 0xa) ++selectors[2];
        if (a.result == -1 && a.ret == 1 && a.exception < 0 && opcode16 == 0x1e) ++selectors[3];
        if (!Same(a, b)) {
            char text[300];
            Describe(a, b, text, sizeof(text));
            ctx.Fail("opcode word %08x selector %04x: %s", gCfg.opcodeWord, sel, text);
        }
    };

    for (int op = -4; op <= 0x30; ++op)
        for (int k = 0; k < 40; ++k) run(op);
    for (int op : {0x7e, 0x7f, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10003, 0x10011, 0x1001d})
        for (int k = 0; k < 10; ++k) run(static_cast<uint16_t>(op));
    std::mt19937 rr(5);
    for (int i = 0; i < 2000; ++i) run(static_cast<int>(rr() & 0xffff));
    if (!selectors[0] || !selectors[1] || !selectors[2] || !selectors[3])
        ctx.Fail("opcode 0x1e paths not all reached (0xd:%d 0x15:%d 0xa:%d result -1:%d)", selectors[0], selectors[1], selectors[2], selectors[3]);
}
