#pragma once
// The routes of the cTSWantSimulator tests (tests/ts_want_simulator.cpp, ts_want_simulator_slots.cpp), on the shared helpers
// of tests/shape.h (docs/standalone-shape.md, "Tests").
//
// Route 0: the original side: a simulator from our constructor carrying copies of the game's three vtables (the constructor
// itself is compared with the original's by WantSimulator_Construct), the original RVA called. Route 1: our object with
// copies of our vtables, the slot called through our pristine vtable (the slot where the game's vtable of that face holds the
// RVA). Route 2: copies of the game's vtables again (what the adapters serve when the constructor hook is off), the adapter
// called with an arity-matched trap in the slot it forwards to, so an adapter that dispatched through the vtable is caught.
// A member that is no slot (GetMemoryStore, ...) runs its adapter on routes 1 and 2. The fixtures' fakes are patched over each
// route's copies (gPatch); only vtable words are normalised.
#include "difftest.h"
#include "shape.h"
#include "ts/cTSWantSimulator.h"

#include <cstring>
#include <new>

namespace wantshape {
using W = cTSWantSimulator;

inline int gRoute = 0;
inline unsigned gTrapHits = 0;
constexpr int kRoutes = 3;
constexpr unsigned kFaces = 3;
constexpr unsigned kMaxSlots = 64;
constexpr unsigned kNoFace = ~0u; // Call: a member that is no slot
constexpr uint32_t kFaceOffset[kFaces] = {0, wantsim::kMessageFace, wantsim::kContainerFace};
constexpr unsigned kFaceSlots[kFaces] = {64, 4, 6};
constexpr uint32_t kGameVt[kFaces] = {W::kVTable, W::kMessageVTable, W::kContainerVTable};
// Stack words per slot (a trap returns with the slot's `ret N`).
constexpr uint8_t kArity[kFaces][kMaxSlots] = {
    {2, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 3, 3, 2, 2, 1, 1, 1, 1, 0, 1, 0, 3, 2, 2, 2, 2,
     2, 2, 4, 2, 1, 3, 0, 3, 3, 2, 2, 2, 1, 1, 1, 2, 2, 3, 3, 2, 4, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1, 2},
    {2, 0, 0, 1},
    {2, 0, 0, 1, 1, 1}};

inline void *gCopy[kFaces][kMaxSlots];  // the vtables the object carries
inline void *gPatch[kFaces][kMaxSlots]; // the fixture's fakes, patched over the copies (null: the route's own function)

inline void *const *GameVt(unsigned face) { return shape::GameVTable(kGameVt[face]); }
inline void *const *OurVt(unsigned face) { return shape::Ours<W>(kFaceOffset).face[face]; }
// The route's own function in a slot (what a logging forwarder goes on to).
inline void *OwnSlot(unsigned face, unsigned slot) { return (gRoute == 1 ? OurVt(face) : GameVt(face))[slot]; }

inline void ClearPatches() { std::memset(gPatch, 0, sizeof(gPatch)); }

// Builds the route's simulator in `buffer` (sizeof(W), aligned): our constructor, then copies of the route's own vtables with
// the fakes patched over them.
inline W *Construct(void *buffer)
{
    W *w = ::new (buffer) W;
    for (unsigned f = 0; f < kFaces; ++f) {
        void *const *own = gRoute == 1 ? OurVt(f) : GameVt(f);
        for (unsigned s = 0; s < kFaceSlots[f]; ++s) gCopy[f][s] = gPatch[f][s] ? gPatch[f][s] : own[s];
        shape::VTableAt(buffer, kFaceOffset[f]) = gCopy[f];
    }
    return w;
}

// Destroys the simulator whatever vtables it carries (our destructor, called directly).
inline void Destroy(W *w) { w->W::~W(); }

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
    if (gRoute == 0) return ctx.Original<Fn>(rva)(self, args...);
    if (face == kNoFace) return reinterpret_cast<Fn>(adapter)(self, args...);
    unsigned slot = shape::SlotOf(kGameVt[face], kFaceSlots[face], rva);
    if (slot == ~0u) ctx.Fail("rva %x is not in face %u's vtable", rva, face), slot = 0;
    if (gRoute == 1) return reinterpret_cast<Fn>(OurVt(face)[slot])(self, args...);
    if (!adapter) return ctx.Original<Fn>(rva)(self, args...); // WANT_NA: no adapter left, the route runs the original
    void *const saved = gCopy[face][slot];
    gCopy[face][slot] = shape::TrapFor<TrapHit>(kArity[face][slot]);
    return shape::ThenAfter([&] { return reinterpret_cast<Fn>(adapter)(self, args...); },
        [&] { gCopy[face][slot] = saved; });
}
#define WANT_ADAPTER(name) reinterpret_cast<void *>(&cTSWantSimulatorAdapters::name)
// A slot whose adapter lane GT dropped (only the game's vtables reached the original, and no simulator carries them while
// the constructor hook is in; the original is trapped): route 2 runs the original, route 1 our vtable's slot as before.
#define WANT_NA(name) static_cast<void *>(nullptr)

} // namespace wantshape
