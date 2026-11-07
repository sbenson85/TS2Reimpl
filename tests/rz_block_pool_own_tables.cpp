// The block pools' owned tables (lane VX, src/rz/RZBlockPool.size.cpp): the image-free program stores ts2_own_vt_da3c68 /
// da3c74 / da3c80 / da3c8c where the hooked game stores the image's tables 0xda3c68 / 0xda3c74 / 0xda3c80 / 0xda3c8c.
// Checked here: the owned statics are the image's tables in this build, each image slot is the RVA we name, and our
// table holds, slot for slot, what the hooked game runs for that slot (the hook's replacement; _purecall for the
// abstract bases' pure slots, the image's being the library's 0x8261ba). The pools' behaviour through these tables is
// RZBlockPool_* (tests/rz_block_pool.cpp); RZBlockPool_Constructors below checks the table word each constructor stores.
#include "difftest.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "rz/RZBlockPool.h"

#include <cstdint>
#include <cstring>
#include <random>

extern "C" const void *ts2_own_vt_da3c68[2];
extern "C" const void *ts2_own_vt_da3c74[2];
extern "C" const void *ts2_own_vt_da3c80[2];
extern "C" const void *ts2_own_vt_da3c8c[2];
extern "C" int __cdecl _purecall();

namespace {

constexpr uint32_t kPurecall = 0x8261ba; // the image's _purecall (library)

const HookSpec *HookAt(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return h;
    return nullptr;
}

struct Table {
    const char *name;
    const void *owned;      // the owned static (the image's table in this build)
    uint32_t rva;           // the image's table
    const void *const *ours; // the image-free program's
    uint32_t slots[2];      // the image's entries
};

} // namespace

DIFF_TEST(RZBlockPool_OwnTables, 0xda3c74)
{
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0)));
    const Table tables[] = {
        {"large base", &ts2_rva_da3c68, 0xda3c68, ts2_own_vt_da3c68, {kPurecall, kPurecall}},
        {"large", &ts2_rva_da3c74, 0xda3c74, ts2_own_vt_da3c74, {0x1be007, 0x1be216}},
        {"small base", &ts2_rva_da3c80, 0xda3c80, ts2_own_vt_da3c80, {kPurecall, kPurecall}},
        {"small", &ts2_rva_da3c8c, 0xda3c8c, ts2_own_vt_da3c8c, {0x1be01e, 0x1be22a}},
    };
    for (const Table &t : tables) {
        const uint32_t *image = GameAddress<uint32_t>(t.rva);
        ++ctx.cases;
        if (t.owned != static_cast<const void *>(image)) ctx.Fail("%s: the owned static is not the image's table", t.name);
        for (int i = 0; i < 2; ++i) {
            ++ctx.cases;
            if (image[i] - base != t.slots[i]) ctx.Fail("%s slot %d: the image's entry is %x", t.name, i, image[i] - base);
            if (t.slots[i] == kPurecall) {
                if (t.ours[i] != reinterpret_cast<const void *>(&_purecall)) ctx.Fail("%s slot %d: not _purecall", t.name, i);
                continue;
            }
            const HookSpec *h = HookAt(t.slots[i]);
            if (!h) ctx.Fail("%s slot %d: %x is not hooked", t.name, i, t.slots[i]);
            else if (h->replacement != t.ours[i]) ctx.Fail("%s slot %d: ours is not what the hook of %x installs", t.name, i, t.slots[i]);
        }
    }
    // The pools and their lock are the image's cells in this build.
    const struct {
        const void *owned;
        uint32_t rva;
    } cells[] = {{&ts2_rva_10cb088, 0x10cb088}, {&ts2_rva_10cb08c, 0x10cb08c}, {&ts2_rva_10cb090, 0x10cb090}};
    for (const auto &c : cells) {
        ++ctx.cases;
        if (c.owned != ctx.image.At(c.rva)) ctx.Fail("cell %x: the owned static is at %p, the image's at %p", c.rva, c.owned, ctx.image.At(c.rva));
    }
}

// The three constructors (0x1bdd0e, 0x1bdce5, 0x1bdd4e) on each side, on the same random 0x20-byte object: the table
// word each stores (the owned statics, the image's tables in this build), the parent, the search position, and the two
// maps (empty: size 0, a head whose links point at itself; mCurrent is the partial map's head). The maps' heads are
// freed afterwards (the process heap, the allocator both sides use).
DIFF_TEST(RZBlockPool_Constructors, 0x1bdd0e)
{
    using Ctor0 = void *(__thiscall *)(void *);
    using Ctor1 = void *(__thiscall *)(void *, void *);
    const Ctor0 base = ctx.Original<Ctor0>(0x1bdd0e), smallBase = ctx.Original<Ctor0>(0x1bdce5);
    const Ctor1 smallCtor = ctx.Original<Ctor1>(0x1bdd4e);
    // The tables each stores: the bases' (0xda3c68, 0xda3c80), and the small pool's 0xda3c8c.
    const void *const kTables[3] = {&ts2_rva_da3c68, &ts2_rva_da3c80, &ts2_rva_da3c8c};
    std::mt19937 rng(0xb10c);
    for (int run = 0; run < 60; ++run) {
        const int which = run % 3;
        alignas(4) uint8_t a[0x20], b[0x20];
        for (uint8_t &x : a) x = static_cast<uint8_t>(rng());
        std::memcpy(b, a, sizeof(a));
        void *parent = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        void *ra = which == 0 ? base(a) : which == 1 ? smallBase(a) : smallCtor(a, parent);
        auto *pb = reinterpret_cast<RZBlockPool *>(b);
        void *rb = which == 0 ? pb->ConstructBase() : which == 1 ? pb->ConstructSmallBase() : pb->ConstructSmall(static_cast<RZBlockPool *>(parent));
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("constructor %d: returns", which);
        const auto *pa = reinterpret_cast<const RZBlockPool *>(a);
        if (pa->mVTable != pb->mVTable) ctx.Fail("constructor %d: table word %p / %p", which, pa->mVTable, pb->mVTable);
        if (pb->mVTable != kTables[which]) ctx.Fail("constructor %d: not table %p", which, kTables[which]);
        if (pa->mNextUnit != pb->mNextUnit || pa->mParent != pb->mParent) ctx.Fail("constructor %d: next / parent", which);
        if (pa->mPartial[1] != 0 || pb->mPartial[1] != 0 || pa->mFull[1] != 0 || pb->mFull[1] != 0) ctx.Fail("constructor %d: map sizes", which);
        if (pa->mCurrent != reinterpret_cast<void *>(static_cast<uintptr_t>(pa->mPartial[0])) ||
            pb->mCurrent != reinterpret_cast<void *>(static_cast<uintptr_t>(pb->mPartial[0])))
            ctx.Fail("constructor %d: the search does not start at the partial map's head", which);
        for (RZBlockPool *p : {reinterpret_cast<RZBlockPool *>(a), pb}) {
            p->Partial().~RZRegionMap();
            p->Full().~RZRegionMap();
        }
    }
}
