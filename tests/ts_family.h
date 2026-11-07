#pragma once
// Fake world shared by the cTSFamily difftests (tests/ts_family*.cpp).
//
// A World is one family object in a guarded raw buffer plus a handful of recording fakes (member objects, services, streams).
// Every call a fake receives appends to `events`; objects hand out counted references (refs), and the final counts are
// compared as well. Two worlds built from one seed start byte-identical (apart from heap addresses and vtable words); one runs
// the original, the other ours, and everything observable is compared.
//
// The standalone shape (docs/standalone-shape.md, lane FA): every comparison runs on two routes.
// - The vtable route: our side is built by our constructor, carries a copy of our vtables with the world's recording fakes
//   patched over it, and the function under test is called through our pristine vtable, in the slot where the game's vtable
//   holds the original RVA (a slot-order mistake calls the wrong function).
// - The adapter route: our side is built by the original constructor and carries a copy of the game's vtables with the same
//   fakes, plus an arity-matched trap wherever the game's vtable holds the RVA under test; the RVA hook's adapter is called (an
//   adapter dispatching virtually would hit the trap; in game it would be its own hook, an endless loop).
// The original side is built by the original constructor and carries the game's vtables with the fakes. Vtable words are the
// only bytes normalised (VtCode): the game's, ours and the copy of face f compare as one code. Adapter-route seeds carry the
// top bit in failure messages.
#include "difftest.h"
#include "game/game.h"
#include "shape.h"
#include "ts/cTSFamily.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace tsfam {
using Family = cTSFamily;

constexpr unsigned kFaces = 2;
constexpr uint32_t kFaceOffset[kFaces] = {0, family::kPersistFace};
constexpr uint32_t kGameVTable[kFaces] = {Family::kOriginalVTable, Family::kOriginalPersistVTable};
constexpr unsigned kSlotCount[kFaces] = {80, 12};
constexpr uint32_t kConstructRva = 0x49b340;

enum Route { kVTableRoute, kAdapterRoute };
extern Route gRoute;
extern bool gOurSide; // which side Build (and Reconstruct) builds: ours (by the route) or the original
extern int gTrapHits;

void **GameVTable(unsigned face);
void **OurVTable(unsigned face);    // read from an object our constructor built
void **CopyVTable(unsigned face);   // the copy the world's family carries (fakes and traps patched over)
uint32_t VtCode(uint32_t word);     // a vtable word as a code per face (game's, ours or the copy alike); anything else as is
unsigned SlotOf(unsigned face, uint32_t rva);
// Over the copies, wherever the game's vtables hold `rva`: a trap that counts (gTrapHits) and forwards to what the copy held
// there (the side's own function or the world's fake), popping `words` stack words.
void InstallTrap(uint32_t rva, unsigned words);
// Builds a fresh family in the world's buffer, the side's and route's way (no fakes, no field values).
void Reconstruct();
// Whether our class's faces sit where the original's do.
bool FacesWhereExpected();

using shape::Words;

// The function at `rva` (a slot of `face`, or a hooked non-slot member) called the side's and route's way on the world's
// family: the original RVA; our pristine vtable's slot (vtable route; a non-slot member's adapter on our object); the adapter on
// the game-built object with traps (adapter route).
template <class Fn, class... A>
auto CallOn(difftest::Context &ctx, void *object, bool orig, unsigned face, uint32_t rva, void *adapter, A... args)
{
    auto self = shape::SelfAt<Fn>(object, kFaceOffset[face]);
    // Every side gets the counting trap in the slot under test (forwarding to the side's own function there), so a body that
    // legitimately calls its own slot (InsertKeyIntoGroup1 through slot 10, InsertObjectIntoGroup2 through slot 18) counts
    // alike on both sides, and an adapter that dispatches virtually counts once more.
    InstallTrap(rva, Words<A...>());
    if (orig) return ctx.Original<Fn>(rva)(self, args...);
    const unsigned slot = SlotOf(face, rva);
    if (gRoute == kAdapterRoute) return reinterpret_cast<Fn>(adapter)(self, args...);
    if (slot == ~0u) return reinterpret_cast<Fn>(adapter)(self, args...);
    return reinterpret_cast<Fn>(OurVTable(face)[slot])(self, args...);
}
Family *F();
template <class Fn, class... A> auto Call(difftest::Context &ctx, bool orig, uint32_t rva, void *adapter, A... args)
{
    return CallOn<Fn>(ctx, F(), orig, 0, rva, adapter, args...);
}

// A slot no adapter covers (folded bodies, thunks): the game's vtable's function on the original side, our pristine vtable's on
// ours (both routes alike).
template <class Fn, class... A> auto CallSlot(void *object, bool orig, unsigned face, unsigned slot, A... args)
{
    auto self = shape::SelfAt<Fn>(object, kFaceOffset[face]);
    void **vt = orig ? GameVTable(face) : OurVTable(face);
    return reinterpret_cast<Fn>(vt[slot])(self, args...);
}

#define FAM_ADAPTER(f) reinterpret_cast<void *>(&cTSFamilyAdapters::f)

constexpr unsigned kObjects = 12;

struct Obj {
    void **vt;
    uint32_t id;
    int32_t refs;
    FamilyKey key;
    uint32_t answer; // QueryInterface hands out the object itself (counted) when nonzero
    uint32_t out[4]; // what a fake getter writes
};

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[0xc8];
    uint8_t after[16];
};

struct World {
    Guarded work;
    Obj objs[kObjects];
    std::vector<uint32_t> events;
    uint32_t seed = 0;
    uint32_t config[8] = {};
};

World &W();
uint32_t Id(const void *p); // 0 for null, 100 + index for a fake object, 1 for anything else

enum : uint32_t { evQI = 1, evAddRef, evRelease, evGetKey, evFamDelete, evLoad1, evLoad2, evFamAddRef, evService, evText, evFramework };
void Ev(std::initializer_list<uint32_t> items);

// The family's fake vtable (slot 1 count, 77 delete, 33 / 78 load, 22 / 23 flag) and the member objects' vtable.
void InstallFakes(Family *f);
// Extra fake family slots for one test (they survive Build until cleared).
void SetFamilySlot(unsigned slot, void *fn);
void ClearFamilySlots();

// Builds the deterministic world for a seed: random guard bytes, a constructed family (the side's and route's way: gOurSide,
// gRoute) carrying the vtable copies with the fakes, random scalar fields and contents. `fill` bits: 1 member ids, 2 id set,
// 4 maps (group objects), 8 texts.
void Build(World &w, uint32_t seed, uint32_t fill = 0xf);

// A comparable record of everything observable: the normalised object bytes, the contents of its containers, the fakes' counts
// and the event log.
using Blob = std::vector<uint32_t>;
void Capture(Blob &b, bool zeroTextBuffers = false);
bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed);

// Seeds of the adapter route carry the top bit in failure messages.
inline uint32_t RouteSeed(uint32_t seed) { return gRoute == kAdapterRoute ? seed | 0x80000000u : seed; }

// Runs `run(original)` on two identically built worlds per route and compares the returned words, the state and the events.
template <class Run>
bool Pair(difftest::Context &ctx, const char *what, uint32_t seed, uint32_t fill, Run run)
{
    bool ok = true;
    for (int route = 0; route < 2; ++route) {
        gRoute = Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gOurSide = side != 0;
            Build(W(), seed, fill);
            gTrapHits = 0;
            uint32_t returned = 0;
            returned = uint32_t(run(side == 0));
            blobs[side].push_back(returned);
            blobs[side].push_back(uint32_t(gTrapHits));
            Capture(blobs[side]);
        }
        ok &= Same(ctx, blobs[0], blobs[1], what, RouteSeed(seed));
    }
    return ok;
}
} // namespace tsfam
