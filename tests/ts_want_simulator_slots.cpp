// cTSWantSimulator in the standalone shape (lane GA): the slots no other test calls, and the game slots our vtable forwards.
//
// WantSimulator_Slots: the folded slots (AddRef 0x15e520, GetTick 0x22fa70, the data-container face's two `xor al, al` slots
// 0x18a7a0), the tree-manager forwards (slots 23 / 24) and the secondary faces' QueryInterface / AddRef / Release thunks, each
// called through each side's own vtable: the game's on an object carrying the game's vtables (route 0), ours on our object
// (route 1). Results, the fakes' log, the count and the out-parameter (as an offset from the object) must agree.
//
// WantSimulator_GameSlots: every slot whose body is still the game's: the original's entry is stood in for by a recording
// stub (a 5-byte jump patched over the image), our vtable's slot is called with distinct arguments, and the stub must be
// reached once with the slot's face as `this`, the arguments unchanged and in order, and its result returned.
#include "difftest.h"
#include "game/game.h"
#include "cas_body_part_system.h" // Env: the game allocator and allocator-service cache
#include "ts_want_simulator_shape.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <vector>

namespace wantslots {

using namespace wantshape;

std::vector<uint32_t> gLog;
void Log(uint32_t a, uint32_t b = 0, uint32_t c = 0) { gLog.insert(gLog.end(), {a, b, c}); }

// Slot 62 (Release's delete): logged, nothing freed.
void *__fastcall FakeDelete(W *w, void *, uint32_t flags)
{
    Log(162, flags);
    return w;
}

// The want tree manager at +0x24: slot 5 looks a guid up, slot 7 returns its table.
struct FakeTrees {
    void **vt;
    uint32_t salt;
};
uint32_t __fastcall T_Find(FakeTrees *t, void *, uint32_t guid)
{
    Log(205, guid);
    return guid * 3 + t->salt;
}
uint32_t __fastcall T_Table(FakeTrees *t, void *)
{
    Log(207);
    return t->salt ^ 0x3c3c;
}
void *gTreesVt[16];
FakeTrees gTrees;

struct SlotCase {
    unsigned face, slot;
    uint32_t rva;
    unsigned args; // stack words
    bool byteResult;
    bool qi;
};
constexpr SlotCase kSlotCases[] = {
    {0, 1, 0x15e520, 0, false, false},  // AddRef (folded)
    {0, 23, 0x48dfc0, 1, false, false}, // GetWantDefinition
    {0, 24, 0x48dfd0, 0, false, false}, // DefinitionsSlot7
    {0, 26, 0x22fa70, 0, false, false}, // GetTick (folded)
    {1, 0, 0x4927f9, 2, true, true},    // QueryInterface thunk (+4)
    {1, 1, 0x1f4daf, 0, false, false},  // AddRef thunk (+4, folded)
    {1, 2, 0x49481a, 0, false, false},  // Release thunk (+4)
    {2, 0, 0x492801, 2, true, true},    // QueryInterface thunk (+8)
    {2, 1, 0x1f4db7, 0, false, false},  // AddRef thunk (+8, folded)
    {2, 2, 0x494822, 0, false, false},  // Release thunk (+8)
    {2, 3, 0x18a7a0, 1, true, false},   // CollectReferenced (folded: false)
    {2, 4, 0x18a7a0, 1, true, false},   // Export (folded: false)
};

uint32_t CallWords(void *fn, void *self, unsigned args, const uint32_t *w)
{
    switch (args) {
    case 0: return reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self);
    case 1: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(self, w[0]);
    case 2: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(fn)(self, w[0], w[1]);
    case 3: return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(fn)(self, w[0], w[1], w[2]);
    default:
        return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(fn)(self, w[0], w[1], w[2], w[3]);
    }
}

} // namespace wantslots
using namespace wantslots;

DIFF_TEST(WantSimulator_Slots, 0x4927f9)
{
    bpt::Env env(ctx);
    gLog.reserve(4096);
    ClearPatches();
    gPatch[0][62] = reinterpret_cast<void *>(&FakeDelete);
    for (auto &s : gTreesVt) s = nullptr;
    gTreesVt[5] = reinterpret_cast<void *>(&T_Find);
    gTreesVt[7] = reinterpret_cast<void *>(&T_Table);
    alignas(8) static unsigned char buf[2][sizeof(W)];
    {
        W *w = ::new (buf[0]) W;
        if (shape::P(static_cast<cIGZMessageTarget2 *>(w)) - shape::P(w) != wantsim::kMessageFace ||
            shape::P(static_cast<cITSDataContainer *>(w)) - shape::P(w) != wantsim::kContainerFace)
            ctx.Fail("base offsets");
        Destroy(w);
    }
    for (const SlotCase &c : kSlotCases)
        if (GameVt(c.face)[c.slot] != GameAddress<void>(c.rva)) ctx.Fail("game slot %u.%u is not %x", c.face, c.slot, c.rva);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        for (const SlotCase &c : kSlotCases) {
            ++ctx.cases;
            uint32_t result[2], count[2], outOffset[2];
            std::vector<uint32_t> logs[2];
            for (int side = 0; side < 2; ++side) {
                gRoute = side;
                std::mt19937 rng(seed * 13 + c.slot * 7 + c.face);
                W *w = Construct(buf[side]);
                w->mRefCount = int32_t(rng() % 4);
                w->mTick = rng();
                gTrees = {gTreesVt, rng()};
                w->mDefinitions.object = &gTrees;
                void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
                uint32_t words[2] = {rng(), rng()};
                if (c.qi) {
                    const uint32_t ids[] = {1, 0xd7ee268, 0x8db5bf48, 0x8db5bf49, rng()};
                    words[0] = ids[rng() % 5];
                    words[1] = shape::P(&out);
                }
                void *self = shape::SelfAt<uint32_t(__thiscall *)(void *)>(w, kFaceOffset[c.face]);
                void *fn = (side == 0 ? GameVt(c.face) : OurVt(c.face))[c.slot];
                gLog.clear();
                uint32_t r = CallWords(fn, self, c.args, words);
                result[side] = c.byteResult ? r & 0xff : r;
                count[side] = uint32_t(w->mRefCount);
                outOffset[side] = shape::P(out) == 0x5a5a5a5a ? 0xffffffffu : shape::P(out) - shape::P(w);
                logs[side] = gLog;
                w->mDefinitions.object = nullptr;
                Destroy(w);
            }
            gRoute = 0;
            if (result[0] != result[1] || count[0] != count[1] || outOffset[0] != outOffset[1] || logs[0] != logs[1])
                ctx.Fail("slot %u.%u seed %u: result %x/%x count %u/%u out %x/%x log %zu/%zu words", c.face, c.slot, seed, result[0],
                         result[1], count[0], count[1], outOffset[0], outOffset[1], logs[0].size(), logs[1].size());
        }
    }
}

namespace wantslots {

// A recording stand-in for an original entry: `this`, the stack words and the hit count.
void *gSelf;
uint32_t gWords[shape::kMaxTrapWords];
unsigned gCount, gHits;
template <unsigned... I> uint32_t __fastcall Record(void *self, void *, shape::StackWord<I>... w)
{
    gSelf = self;
    const uint32_t words[] = {0u, w...};
    for (unsigned i = 0; i < sizeof...(I); ++i) gWords[i] = words[i + 1];
    gCount = sizeof...(I);
    ++gHits;
    return 0xc0de005au + (sizeof...(I) << 8);
}
template <unsigned... I> void *RecordAddress(std::integer_sequence<unsigned, I...>) { return reinterpret_cast<void *>(&Record<I...>); }
void *RecordFor(unsigned words)
{
    void *const all[] = {RecordAddress(std::make_integer_sequence<unsigned, 0>()), RecordAddress(std::make_integer_sequence<unsigned, 1>()),
                         RecordAddress(std::make_integer_sequence<unsigned, 2>()), RecordAddress(std::make_integer_sequence<unsigned, 3>()),
                         RecordAddress(std::make_integer_sequence<unsigned, 4>())};
    return all[words];
}

struct WantCodePatch {
    uint8_t *site;
    uint8_t saved[5];
    WantCodePatch(void *at, const void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xe9;
        std::memcpy(site + 1, &rel, 4);
    }
    WantCodePatch(const WantCodePatch &) = delete;
    ~WantCodePatch() { std::memcpy(site, saved, 5); }
};

struct GameSlot {
    unsigned face, slot;
    uint32_t rva;
};
constexpr GameSlot kGameSlots[] = {
    {0, 25, 0x48d840}, {0, 27, 0x48dad0}, {0, 31, 0x496930},
    {0, 34, 0x491960}, {0, 36, 0x494540}, {0, 37, 0x4944c0}, {0, 39, 0x495d00}, {0, 40, 0x495e30},
    {0, 43, 0x492840}, {0, 44, 0x48ec20}, {0, 46, 0x497480}, {0, 50, 0x496010},
    {0, 51, 0x4962c0}, {0, 54, 0x497590}, {0, 58, 0x4908e0}, {0, 59, 0x494a70},
    {0, 60, 0x490300}, {0, 61, 0x48ca60},
    // Slots 38, 47, 48, 53, DoMessage {1, 3} and Import {2, 5}: ours since lane GG (tests/ts_want_simulator_gg*.cpp); slot 52 since lane GF (tests/ts_want_simulator_gf.cpp).
};

} // namespace wantslots
using namespace wantslots;

// Slots 3-5 (Init, Shutdown, AddCategoryWant): ours since lane IV (tests/ts_want_simulator_iv.cpp).
DIFF_TEST(WantSimulator_GameSlots, 0x48d840)
{
    alignas(8) static unsigned char buf[sizeof(W)];
    gRoute = 1;
    ClearPatches();
    W *w = Construct(buf);
    for (const GameSlot &g : kGameSlots) {
        ++ctx.cases;
        if (GameVt(g.face)[g.slot] != GameAddress<void>(g.rva)) ctx.Fail("game slot %u.%u is not %x", g.face, g.slot, g.rva);
        const unsigned words = kArity[g.face][g.slot];
        WantCodePatch stand(ctx.image.At<uint8_t>(g.rva), RecordFor(words));
        const uint32_t args[4] = {0x11110000u + g.slot, 0x22220000u + g.slot, 0x33330000u + g.slot, 0x44440000u + g.slot};
        void *self = shape::SelfAt<uint32_t(__thiscall *)(void *)>(w, kFaceOffset[g.face]);
        gHits = 0;
        gSelf = nullptr;
        const uint32_t r = CallWords(OurVt(g.face)[g.slot], self, words, args);
        const bool byteResult = g.face != 0; // DoMessage and Import return bool
        const uint32_t expected = 0xc0de005au + (words << 8);
        if (gHits != 1 || gSelf != self || gCount != words) {
            ctx.Fail("slot %u.%u: %u hits, this %p (face %p), %u words", g.face, g.slot, gHits, gSelf, self, gCount);
            continue;
        }
        for (unsigned i = 0; i < words; ++i)
            if (gWords[i] != args[i]) ctx.Fail("slot %u.%u: argument %u is %x, not %x", g.face, g.slot, i, gWords[i], args[i]);
        if ((byteResult ? r & 0xff : r) != (byteResult ? expected & 0xff : expected))
            ctx.Fail("slot %u.%u: result %x", g.face, g.slot, r);
    }
    Destroy(w);
    gRoute = 0;
}
