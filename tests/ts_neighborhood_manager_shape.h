#pragma once
// The routes of the cTSNeighborhoodManager tests (tests/ts_neighborhood_manager.cpp, tests/ts_neighborhood_manager_load.cpp),
// on the shared helpers of tests/shape.h (docs/standalone-shape.md, "Tests").
//
// Route 0: the original side: an object of the game's shape (the original builds it inline in Create 0x4a3f20, so there is no
// constructor to call: our constructor's fields with the game's vtables, which NeighborhoodManager_Create compares with the
// original's object), the original RVA called. Route 1: our object from our constructor, the slot called through our pristine
// vtable (the slot where the game's vtable of that face holds the RVA). Route 2: an object of the game's shape (what the adapters
// serve when the Create hook is off or the game calls an old entry point), the adapter called with an arity-matched trap in the
// slot it forwards to, so an adapter that dispatched through the vtable is caught. Every object carries copies of its route's
// own three vtables with the fixture's fakes patched over them; only the vtable words are normalised.
#include "difftest.h"
#include "shape.h"
#include "ts/cTSNeighborhoodManager.h"

#include <new>

namespace hoodmgrshape {
using Mgr = cTSNeighborhoodManager;

inline int gRoute = 0;
inline unsigned gTrapHits = 0;
// Route 2 of a slot whose adapter was dropped (lane GJ: nothing reaches the original while the Create hook is in; its
// status entry is "in_effect": "callers"): the original runs on the game-shaped object, as on route 0 (NO_ADAPTER).
inline bool gNoAdapter = false;
#define NO_ADAPTER nullptr
constexpr unsigned kFaces = 3;
constexpr uint32_t kFaceOffset[kFaces] = {0, hoodmgr::kPersistFace, hoodmgr::kSerialFace};
constexpr unsigned kFaceSlots[kFaces] = {21, 12, 6};
constexpr uint32_t kGameVt[kFaces] = {Mgr::kVTable, Mgr::kPersistVTable, Mgr::kSerialVTable};
// Stack words per slot (a trap returns with the slot's `ret N`).
constexpr uint8_t kArity[kFaces][21] = {{2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 3, 3, 2, 2, 1, 1, 1, 1},
                                        {2, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0},
                                        {2, 0, 0, 1, 1, 0}};

inline void *gCopy[kFaces][21];  // the vtables the object carries
inline void *gPatch[kFaces][21]; // the fixture's fakes, patched over the copies (null: the route's own function)

inline void *const *GameVt(unsigned face) { return shape::GameVTable(kGameVt[face]); }
inline void *const *OurVt(unsigned face) { return shape::Ours<Mgr>(kFaceOffset).face[face]; }
// The class vtable of `face` that the route's code stores (a destructor's stores): the game's on route 0, ours on the others
// (the adapter destroys with our destructor).
inline void *const *RouteVt(unsigned face) { return gRoute == 0 || gNoAdapter ? GameVt(face) : OurVt(face); }

// Builds the route's object in `buffer` (sizeof(Mgr), aligned): our constructor; on routes 0 and 2 the game's vtables over it.
inline Mgr *Construct(void *buffer)
{
    gNoAdapter = false;
    Mgr *m = ::new (buffer) Mgr;
    if (gRoute != 1)
        for (unsigned f = 0; f < kFaces; ++f) shape::VTableAt(buffer, kFaceOffset[f]) = const_cast<void **>(GameVt(f));
    return m;
}

// Destroys the object whatever vtables it carries (our destructor, called directly).
inline void Destroy(void *buffer) { static_cast<Mgr *>(buffer)->Mgr::~Mgr(); }

// Copies the route's own vtables (game's on routes 0 and 2, ours on route 1), patches the fakes over them and installs them.
inline void Install(void *buffer)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        void *const *own = gRoute == 1 ? OurVt(f) : GameVt(f);
        for (unsigned s = 0; s < kFaceSlots[f]; ++s) gCopy[f][s] = gPatch[f][s] ? gPatch[f][s] : own[s];
        shape::VTableAt(buffer, kFaceOffset[f]) = gCopy[f];
    }
}

// A vtable word as a code: the installed copy of face f (0x7c00 + f), the route's class vtable of face f (0x7d00 + f), or raw.
inline uint32_t VtCode(unsigned face, uint32_t word)
{
    if (word == shape::P(gCopy[face])) return 0x7c00 + face;
    if (word == shape::P(RouteVt(face))) return 0x7d00 + face;
    return word;
}

inline uint32_t TrapHit(void *, unsigned)
{
    ++gTrapHits;
    return 0;
}

// Runs the function at `rva` (a slot of face `face`) the route's way on `object`: the original RVA, our vtable's slot, or the
// adapter with a trap in the slot (restored after the call).
template <class Fn, class... A>
auto Call(difftest::Context &ctx, void *object, uint32_t rva, unsigned face, void *adapter, A... args)
{
    auto self = shape::SelfAt<Fn>(object, kFaceOffset[face]);
    if (gRoute == 2 && !adapter) gNoAdapter = true;
    if (gRoute == 0 || gNoAdapter) return ctx.Original<Fn>(rva)(self, args...);
    unsigned slot = shape::SlotOf(kGameVt[face], kFaceSlots[face], rva);
    if (slot == ~0u) ctx.Fail("rva %x is not in face %u's vtable", rva, face), slot = 0;
    if (gRoute == 1) return reinterpret_cast<Fn>(OurVt(face)[slot])(self, args...);
    void *const saved = gCopy[face][slot];
    gCopy[face][slot] = shape::TrapFor<TrapHit>(kArity[face][slot]);
    return shape::ThenAfter([&] { return reinterpret_cast<Fn>(adapter)(self, args...); },
        [&] { gCopy[face][slot] = saved; });
}
#define HOODMGR_ADAPTER(name) reinterpret_cast<void *>(&cTSNeighborhoodManagerAdapters::name)

} // namespace hoodmgrshape
