// Differential test for cTSPerson::vf4_7 (0x51b870, src/simantics/PrimDispatch.h): the person's operand reader. The
// subobject is a block of memory carrying the few fields the owners read (the object's vtable at S - 4, the holder at
// S + 0x2ec, the person interface pointer at S + 0x44), each leading to a recording fake. The base reader (0x531700,
// verified separately) is patched to a stub on the original's side and passed as a stub to ours, so the arguments
// forwarded for the other owners are compared.
#include "simantics/PrimDispatch.h"
#include "simantics_prim_dispatch.h"

#include <memory>

namespace {

using namespace pd;

struct FakeObj {
    void **vt;
    uint32_t pad[3];
};

struct PWorld {
    alignas(16) uint8_t blk[0x400]; // the object at blk, the subobject S = blk + 4
    int16_t cells[0x200];
    FakeObj personIface;
    FakeObj runHolder;
};
PWorld gW;

// What the caller passed, so the stub can report pointer identity instead of addresses.
SimDataRef *gCallerDest = nullptr;
uint32_t *gCallerFloat = nullptr;
uint32_t *gCallerZero = nullptr;

uint32_t BaseBody(void *s, int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed, uint32_t clearFlag, uint32_t arg7)
{
    gTrace.push_back(0x3000);
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s)));
    gTrace.push_back(static_cast<uint32_t>(owner));
    gTrace.push_back(id);
    gTrace.push_back(dest == gCallerDest ? 1u : (dest ? 2u : 0u));
    if (dest) {
        uint32_t words[3];
        std::memcpy(words, dest, 12);
        for (uint32_t w : words) gTrace.push_back(w);
    }
    gTrace.push_back(floatDest == gCallerFloat ? 1u : (floatDest ? 2u : 0u));
    if (floatDest) gTrace.push_back(*floatDest);
    gTrace.push_back(zeroed == gCallerZero ? 1u : (zeroed ? 2u : 0u));
    if (zeroed) gTrace.push_back(*zeroed);
    gTrace.push_back(clearFlag);
    gTrace.push_back(arg7);
    // Scribble on the outputs so the caller's handling after the base returns shows up.
    if (dest) {
        const uint32_t w[3] = {Mix(gCfg.seed, 1, 1), Mix(gCfg.seed, 1, 2), Mix(gCfg.seed, 1, 3)};
        std::memcpy(dest, w, 12);
    }
    if (floatDest) *floatDest = Mix(gCfg.seed, 2, 1);
    if (zeroed) *zeroed = Mix(gCfg.seed, 2, 2);
    return Mix(gCfg.seed, 3, 1);
}
uint32_t __fastcall BaseStub(void *s, void *, int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed, uint32_t clearFlag,
                             uint32_t arg7)
{
    return BaseBody(s, owner, id, dest, floatDest, zeroed, clearFlag, arg7);
}
uint32_t BaseFake(void *s, int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed, uint32_t clearFlag, uint32_t arg7)
{
    return BaseBody(s, owner, id, dest, floatDest, zeroed, clearFlag, arg7);
}

using PersonRead = uint32_t(__thiscall *)(void *s, int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed,
                                          uint32_t clearFlag, uint32_t arg7);

const int16_t kCellValues[] = {0, 1, 2, 0x15, 0x16, 0x17, 0x18, -1, -2, INT16_MIN, INT16_MAX, 0x100};

struct CaseArgs {
    int32_t owner;
    uint32_t id;
    bool hasDest, hasFloat, hasZero;
    uint32_t clearFlag, arg7;
    SimDataRef destInit;
    uint32_t floatInit, zeroInit;
};

} // namespace

DIFF_TEST(PrimDispatch_PersonReader, 0x51b870)
{
    auto original = ctx.Original<PersonRead>(0x51b870);
    EnsureVtable();

    std::memset(&gW, 0, sizeof(gW));
    uint8_t *const object = gW.blk;
    uint8_t *const s = gW.blk + 4;
    *reinterpret_cast<void ***>(object) = gVtable;
    *reinterpret_cast<void ***>(s + 0x2ec) = gVtable;
    gW.personIface.vt = gVtable;
    gW.runHolder.vt = gVtable;
    *reinterpret_cast<void **>(s + 0x44) = &gW.personIface;
    for (uint32_t i = 0; i < 32; ++i) gObjects[i] = nullptr;
    gObjects[kPersonObject] = object;
    gObjects[kHolder2] = s + 0x2ec;
    gObjects[kPersonIface] = &gW.personIface;
    gObjects[kRunHolder] = &gW.runHolder;

    ImagePatch patch(ctx, 0x531700, reinterpret_cast<void *>(&BaseStub));

    std::mt19937 r(0x51b870);
    int reached[8] = {};

    auto pickOwner = [&]() -> int32_t {
        int32_t o;
        switch (r() % 4) {
        case 0: o = static_cast<int32_t>(r() % 0x30) - 2; break;
        case 1: o = static_cast<int32_t>(r() % 6) + 0xb + 0; break;
        case 2: { const int32_t own[] = {0xc, 0x12, 0x1c, 0x1d, 0x1e}; o = own[r() % 5]; break; }
        default: o = static_cast<int16_t>(r()); break;
        }
        return static_cast<int32_t>((r() & 0xffff0000u) | static_cast<uint16_t>(o));
    };
    auto pickId = [&]() -> uint32_t {
        const uint32_t edge[] = {0, 1, 2, 7, 8, 0xe, 0xf, 0x10, 0x11, 0xe5, 0xe6, 0xe7, 0xe8, 0x100, 0x7fff, 0x8000, 0xffff, 0xfffe};
        uint32_t v = (r() % 3 == 0) ? (r() % 0x120) : edge[r() % 18];
        if (r() % 2) v |= r() & 0xffff0000u;
        return v;
    };

    auto run = [&](int32_t owner, uint32_t id) {
        // The world.
        Config &c = gCfg;
        c = Config();
        c.seed = r();
        c.slot98 = (r() % 6 == 0) ? r() : 2u;
        if (r() % 20 == 0) c.slot98 = 0x10002u; // ax is 2 but eax is not: the compare is on the whole register
        const uint32_t s19[] = {0, 1, 7, 8, 0xe5, 0xe6, 0xe7, 0x100, 0xffff, r(), r() & 0xffff};
        c.slot19 = (r() & 0xffff0000u) | (s19[r() % 11] & 0xffff);
        c.slot20 = r();
        for (auto &v : gW.cells) v = (r() % 2) ? kCellValues[r() % 12] : static_cast<int16_t>(r());
        c.tableMem = (r() % 6 == 0) ? nullptr : &gW.cells[r() % 0x40];
        for (int i = 0; i < 3; ++i) c.runs[i] = (r() % 7 == 0) ? nullptr : &gW.cells[0x80 + r() % 0x40];

        CaseArgs a;
        a.owner = owner;
        a.id = id;
        a.hasDest = r() % 4 != 0;
        a.hasFloat = r() % 3 != 0;
        a.hasZero = r() % 3 != 0;
        const uint32_t cf[] = {0, 1, 0x100, 0xff, r()};
        a.clearFlag = cf[r() % 5];
        a.arg7 = r();
        a.destInit = SimDataRef{static_cast<int16_t>(r()), static_cast<int16_t>(r()), reinterpret_cast<int16_t *>(static_cast<uintptr_t>(r())),
                                static_cast<int16_t>(r()), static_cast<int16_t>(r())};
        a.floatInit = r();
        a.zeroInit = r();

        int16_t snapshot[0x200];
        std::memcpy(snapshot, gW.cells, sizeof(snapshot));

        auto exec = [&](bool ours) {
            std::memcpy(gW.cells, snapshot, sizeof(snapshot));
            SimDataRef dest = a.destInit;
            uint32_t f = a.floatInit, z = a.zeroInit;
            gCallerDest = a.hasDest ? &dest : nullptr;
            gCallerFloat = a.hasFloat ? &f : nullptr;
            gCallerZero = a.hasZero ? &z : nullptr;
            Outcome o = Run([&](Outcome &out) {
                out.ret = ours ? PersonReadOperand(s, a.owner, a.id, gCallerDest, gCallerFloat, gCallerZero, a.clearFlag, a.arg7, BaseFake)
                               : original(s, a.owner, a.id, gCallerDest, gCallerFloat, gCallerZero, a.clearFlag, a.arg7);
            });
            o.extra.resize(12 + 4 + 4 + sizeof(snapshot));
            std::memcpy(o.extra.data(), a.hasDest ? &dest : &a.destInit, 12);
            std::memcpy(o.extra.data() + 12, &f, 4);
            std::memcpy(o.extra.data() + 16, &z, 4);
            std::memcpy(o.extra.data() + 20, gW.cells, sizeof(snapshot));
            return o;
        };
        Outcome x = exec(false);
        Outcome y = exec(true);
        ++ctx.cases;
        const int16_t ow = static_cast<int16_t>(owner);
        if (ow == 0x12 || ow == 0x1e || ow == 0xc || ow == 0x1d || ow == 0x1c) {
            if (x.exception < 0) ++reached[0];
            else if (x.exception == 0x1c) ++reached[1];
            else if (x.exception == 0x1a) ++reached[2];
            else if (x.exception == 0x21) ++reached[3];
            else if (x.exception == 0x14) ++reached[4];
            else if (x.exception == 2) ++reached[5];
        } else if (x.exception < 0) {
            ++reached[6];
        }
        if (!Same(x, y)) {
            char text[300];
            Describe(x, y, text, sizeof(text));
            ctx.Fail("owner %08x id %08x flag %08x dest/float/zero %d%d%d: %s", owner, id, a.clearFlag, a.hasDest, a.hasFloat, a.hasZero, text);
        }
    };

    for (int32_t owner : {-1, 0, 1, 0xb, 0xc, 0xd, 0x11, 0x12, 0x13, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x33, 0x7fff, -0x8000})
        for (int k = 0; k < 300; ++k) run(static_cast<int32_t>((r() & 0xffff0000u) | static_cast<uint16_t>(owner)), pickId());
    for (int i = 0; i < 20000; ++i) run(pickOwner(), pickId());

    if (!reached[0] || !reached[1] || !reached[2] || !reached[3] || !reached[4] || !reached[5] || !reached[6])
        ctx.Fail("paths not all reached: ok %d, 0x1c %d, 0x1a %d, 0x21 %d, 0x14 %d, 2 %d, base %d", reached[0], reached[1], reached[2],
                 reached[3], reached[4], reached[5], reached[6]);
}
