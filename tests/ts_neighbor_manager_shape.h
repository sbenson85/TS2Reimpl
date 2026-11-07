#pragma once
// The routes of the cTSNeighborManager tests (tests/ts_neighbor_manager.cpp, ts_neighbor_manager_services.cpp,
// ts_neighbor_manager_container.cpp, ts_neighbor_manager_slots.cpp), on the shared helpers of tests/shape.h
// (docs/standalone-shape.md, "Tests").
//
// Route 0: the original side: an object of the game's shape (the original builds it inline in Create 0x4bc570, so there is no
// constructor to call: our constructor's fields with the game's vtables, which NeighborManager_Create compares with the
// original's object), the original RVA called. Route 1: our object from our constructor, the slot called through our pristine
// vtable (the slot where the game's vtable of that face holds the RVA). Route 2: an object of the game's shape (what the adapters
// serve when the Create hook is off or the game calls an old entry point), the adapter called with an arity-matched trap in the
// slot it forwards to, so an adapter that dispatched through the vtable is caught. A member that is no slot (SaveNeighbors,
// LoadNeighbors, ...) is called through its adapter on routes 1 and 2. Every object carries copies of its route's own four
// vtables with the fixture's fakes patched over them; only the vtable words are normalised.
#include "difftest.h"
#include "shape.h"
#include "ts/cTSNeighborManager.h"

#include <cstring>
#include <new>

namespace nbrmgrshape {
using Mgr = cTSNeighborManager;

inline int gRoute = 0;
inline unsigned gTrapHits = 0;
// Route 2 of a slot whose adapter was dropped (lane GP: only the game's vtables reach the original, and no object carries them
// while the Create hook is in; its status entry is "in_effect": "callers"): the original runs on the game-shaped object, as on
// route 0 (NBRMGR_NO_ADAPTER). Set by Call, cleared by Install.
inline bool gNoAdapter = false;
#define NBRMGR_NO_ADAPTER nullptr
constexpr int kRoutes = 3;
constexpr unsigned kFaces = 4;
constexpr unsigned kMaxSlots = 36;
constexpr unsigned kNoFace = ~0u; // Call: a member that is no slot
constexpr uint32_t kFaceOffset[kFaces] = {0, nbrmgr::kPersistFace, nbrmgr::kContainerFace, nbrmgr::kMessageFace};
constexpr unsigned kFaceSlots[kFaces] = {36, 12, 6, 4};
constexpr uint32_t kGameVt[kFaces] = {Mgr::kVTable, Mgr::kPersistVTable, Mgr::kContainerVTable, Mgr::kMessageVTable};
// Stack words per slot (a trap returns with the slot's `ret N`).
constexpr uint8_t kArity[kFaces][kMaxSlots] = {
    {2, 0, 0, 1, 1, 0, 0, 0, 2, 0, 1, 0, 0, 1, 3, 2, 2, 1, 3, 1, 5, 1, 2, 2, 1, 0, 1, 3, 1, 1, 0, 0, 0, 0, 1, 0},
    {2, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0},
    {2, 0, 0, 1, 1, 1},
    {2, 0, 0, 1}};

inline void *gCopy[kFaces][kMaxSlots];  // the vtables the object carries
inline void *gPatch[kFaces][kMaxSlots]; // the fixture's fakes, patched over the copies (null: the route's own function)

inline void *const *GameVt(unsigned face) { return shape::GameVTable(kGameVt[face]); }
inline void *const *OurVt(unsigned face) { return shape::Ours<Mgr>(kFaceOffset).face[face]; }
// The class vtable of `face` that the route's code stores (a destructor's stores): the game's on route 0, ours on the others
// (the adapter destroys with our destructor).
inline void *const *RouteVt(unsigned face) { return gRoute == 0 || gNoAdapter ? GameVt(face) : OurVt(face); }

inline void ClearPatches() { std::memset(gPatch, 0, sizeof(gPatch)); }

// Builds the route's object in `buffer` (sizeof(Mgr), aligned): our constructor; on routes 0 and 2 the game's vtables over it.
inline Mgr *Construct(void *buffer)
{
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
    gNoAdapter = false;
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
// The four vtable words of an object (or of a byte copy of one), coded, written back in place.
inline void CodeVtableWords(void *bytes)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        uint32_t &w = *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(bytes) + kFaceOffset[f]);
        w = VtCode(f, w);
    }
}

inline uint32_t TrapHit(void *, unsigned)
{
    ++gTrapHits;
    return 0;
}

// Runs the function at `rva` (a slot of face `face`, or kNoFace) the route's way on `object`: the original RVA, our vtable's
// slot, or the adapter with a trap in the slot (restored after the call). kNoFace: the adapter on routes 1 and 2.
template <class Fn, class... A>
auto Call(difftest::Context &ctx, void *object, uint32_t rva, unsigned face, void *adapter, A... args)
{
    auto self = shape::SelfAt<Fn>(object, face == kNoFace ? 0 : kFaceOffset[face]);
    gNoAdapter = gRoute == 2 && !adapter;
    if (gRoute == 0 || gNoAdapter) return ctx.Original<Fn>(rva)(self, args...);
    if (face == kNoFace) return reinterpret_cast<Fn>(adapter)(self, args...);
    unsigned slot = shape::SlotOf(kGameVt[face], kFaceSlots[face], rva);
    if (slot == ~0u) ctx.Fail("rva %x is not in face %u's vtable", rva, face), slot = 0;
    if (gRoute == 1) return reinterpret_cast<Fn>(OurVt(face)[slot])(self, args...);
    void *const saved = gCopy[face][slot];
    gCopy[face][slot] = shape::TrapFor<TrapHit>(kArity[face][slot]);
    return shape::ThenAfter([&] { return reinterpret_cast<Fn>(adapter)(self, args...); },
        [&] { gCopy[face][slot] = saved; });
}
#define NBRMGR_ADAPTER(name) reinterpret_cast<void *>(&cTSNeighborManagerAdapters::name)

} // namespace nbrmgrshape
