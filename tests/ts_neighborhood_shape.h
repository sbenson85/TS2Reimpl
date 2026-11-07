#pragma once
// Shared by the cTSNeighborhood tests (tests/ts_neighborhood*.cpp): the class in the standalone shape
// (docs/standalone-shape.md), after ED's two-route helper (tests/ts_lot_info_shape.h). Every Pair runs twice:
// - the vtable route: our side is built by our constructor and each function is called through our object's own (pristine)
//   vtable, in the slot where the game's vtable of that face holds the original RVA (a slot-order mistake calls the wrong
//   function);
// - the adapter route: our side is built by the original constructor 0x471d90 (the game's vtables, as when the constructor
//   hooks are off) and the RVA hook's adapter is called, on copies of the object's current vtables with the adapter's own
//   slot replaced by a trap of the slot's arity (an adapter that dispatched virtually would land there; in game it would be
//   its own hook, an endless loop).
// The original side is always built by the original constructor and destroyed by the original destructor 0x472490.
// Vtable words are the only bytes normalised: the game's vtable of face f, ours of face f, and the trap copy of face f compare
// as one code (Norm). Fakes that patch slots copy the side's *own* vtable first (CopyOwn), then patch.
#include "difftest.h"
#include "game/game.h"
#include "shape.h"
#include "ts/cTSNeighborhood.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

namespace nbshape {

using Nb = cTSNeighborhood;
constexpr unsigned kFaces = 4;
constexpr uint32_t kFaceOffset[kFaces] = {0, nbhood::kPersistFace, nbhood::kContainerFace, nbhood::kMessageFace};
constexpr uint32_t kGameVTable[kFaces] = {Nb::kVTable, Nb::kPersistVTable, Nb::kContainerVTable, Nb::kMessageVTable};
constexpr unsigned kSlotCount[kFaces] = {77, 12, 6, 4};
constexpr uint32_t kConstruct = 0x471d90;
constexpr uint32_t kDestruct = 0x472490;

using shape::P;

// The original's sync table (0x4b8ff0) is a std::vector<uint32_t> in the image global 0x10dab28 (first, last, end), built on
// first use and never freed. A test whose original path reaches it puts the three words back when it ends, so the image's
// statics stay in their file state for later tests (StaticInitAtExit_Form checks them). The buffer is left allocated: freeing
// it would go through the original's heap.
struct SyncTableGlobal {
    static constexpr uint32_t kRva = 0x10dab28;
    difftest::Context &ctx;
    uint32_t saved[3];
    explicit SyncTableGlobal(difftest::Context &c) : ctx(c) { std::memcpy(saved, ctx.image.At<uint32_t>(kRva), sizeof(saved)); }
    ~SyncTableGlobal() { std::memcpy(ctx.image.At<uint32_t>(kRva), saved, sizeof(saved)); }
    SyncTableGlobal(const SyncTableGlobal &) = delete;
    SyncTableGlobal &operator=(const SyncTableGlobal &) = delete;
};

inline void **&VTableOf(void *object, unsigned face) { return shape::VTableAt(object, kFaceOffset[face]); }

// Ours: the words our constructor stores. Built once on a scratch buffer.
inline void **const *Ours() { return shape::Ours<Nb>(kFaceOffset).face; }

// The faces of our class sit where the original's do (base offsets are not constant expressions: checked here).
inline bool FacesWhereExpected(Nb *nb)
{
    auto at = [&](const void *face) { return uint32_t(static_cast<const uint8_t *>(face) - reinterpret_cast<uint8_t *>(nb)); };
    return at(static_cast<cITSNeighborhood *>(nb)) == 0 && at(static_cast<cIGZSerializable *>(nb)) == 0 &&
           at(static_cast<cIGZPersistResource *>(nb)) == kFaceOffset[1] && at(static_cast<cITSDataContainer *>(nb)) == kFaceOffset[2] &&
           at(static_cast<cIGZMessageTarget2 *>(nb)) == kFaceOffset[3];
}

// ---- routes ----

enum Route { kVTableRoute, kAdapterRoute };
inline Route gRoute = kVTableRoute;
inline const char *RouteName() { return gRoute == kVTableRoute ? "vtable" : "adapter"; }
// The vtables the side's object carried right after construction (before any fake patched copies over them).
inline void **gPristine[kFaces];

inline bool GameBuilt(bool original) { return original || gRoute == kAdapterRoute; }

// Builds the side's object in `memory`: the original constructor (original side, adapter route) or ours.
inline Nb *Build(difftest::Context &ctx, bool original, void *memory)
{
    Nb *nb = GameBuilt(original) ? ctx.Original<Nb *(__thiscall *)(void *)>(kConstruct)(memory) : ::new (memory) Nb;
    for (unsigned f = 0; f < kFaces; ++f) gPristine[f] = VTableOf(memory, f);
    return nb;
}
// Destroys it the same side's way (the original destructor, or ours without dispatch).
inline void Destroy(difftest::Context &ctx, bool original, Nb *nb)
{
    if (GameBuilt(original)) ctx.Original<void(__thiscall *)(void *)>(kDestruct)(nb);
    else nb->Nb::~Nb();
}

// Copies the side's own vtable of `face` (as built) into `table` (which must hold the face's slots) for a fake to patch.
inline void CopyOwn(void **table, unsigned face) { std::memcpy(table, gPristine[face], kSlotCount[face] * sizeof(void *)); }

// ---- traps ----

inline int gTrapHits = 0;
inline void *gCopies[kFaces][80];

inline void *TrapFor(unsigned words) { return shape::TrapFor<shape::Count<gTrapHits>>(words); }

// The slot of a face's game vtable that holds the RVA (~0u when none does).
inline unsigned SlotOf(unsigned face, uint32_t rva) { return shape::SlotOf(kGameVTable[face], kSlotCount[face], rva); }

// Gives the object copies of its current vtables with a trap (popping `words` stack words) wherever the game's holds `rva`.
inline void InstallTrap(void *object, uint32_t rva, unsigned words, void **saved)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        saved[f] = VTableOf(object, f);
        const unsigned slot = SlotOf(f, rva);
        std::memcpy(gCopies[f], saved[f], kSlotCount[f] * sizeof(void *));
        if (slot != ~0u) gCopies[f][slot] = TrapFor(words);
        VTableOf(object, f) = gCopies[f];
    }
}
// Puts the vtables the object had back, unless the call replaced them (a destructor).
inline void RemoveTrap(void *object, void **saved)
{
    for (unsigned f = 0; f < kFaces; ++f)
        if (VTableOf(object, f) == gCopies[f]) VTableOf(object, f) = static_cast<void **>(saved[f]);
}

// ---- vtable words ----

// The code of a vtable word: one per face for the game's vtable, ours and the trap copy; anything else raw.
inline uint32_t VtCode(uint32_t word) { return shape::VtCode(word, kFaces, shape::GameTables{kGameVTable}, Ours(), gCopies); }
// Normalises the four vtable words of a copy of the object's bytes.
inline void Norm(uint8_t *copy)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        auto *word = reinterpret_cast<uint32_t *>(copy + kFaceOffset[f]);
        *word = VtCode(*word);
    }
}

// ---- calls ----

using shape::Sig;

// Calls the function at `rva` (a slot of `face`) the side's way: the original RVA; our object's pristine vtable slot (vtable
// route); the adapter on the object with trapped copies of its current vtables (adapter route). `object` is the +0 address.
template <class Fn, class... A>
auto Call(difftest::Context &ctx, bool original, void *object, unsigned face, uint32_t rva, void *adapter, A... args)
{
    using S = typename Sig<Fn>::Self;
    S self = static_cast<S>(static_cast<void *>(static_cast<uint8_t *>(object) + kFaceOffset[face]));
    if (original) return ctx.Original<Fn>(rva)(self, args...);
    if (gRoute == kAdapterRoute) {
        void *saved[kFaces];
        const int hitsBefore = gTrapHits;
        InstallTrap(object, rva, Sig<Fn>::kWords, saved);
        return shape::ThenAfter([&] { return reinterpret_cast<Fn>(adapter)(self, args...); },
                                [&] {
                                    RemoveTrap(object, saved);
                                    if (gTrapHits != hitsBefore) ctx.Fail("adapter of %x dispatched through its own slot", rva);
                                });
    } else {
        unsigned slot = SlotOf(face, rva);
        if (slot == ~0u) {
            ctx.Fail("rva %x is in no slot of face %u", rva, face);
            slot = 0;
        }
        return reinterpret_cast<Fn>(gPristine[face][slot])(self, args...);
    }
}

// A slot called through the object's own pristine vtable on our side and through the game's vtable on the original side
// (the slots no adapter covers: folded bodies and adjustor thunks).
template <class Fn, class... A> auto CallOwn(bool original, void *object, unsigned face, unsigned slot, A... args)
{
    using S = typename Sig<Fn>::Self;
    S self = static_cast<S>(static_cast<void *>(static_cast<uint8_t *>(object) + kFaceOffset[face]));
    void *const *table = original ? GameAddress<void *>(kGameVTable[face]) : const_cast<void *const *>(gPristine[face]);
    return reinterpret_cast<Fn>(table[slot])(self, args...);
}

} // namespace nbshape

#define NB_ADAPTER(f) reinterpret_cast<void *>(&cTSNeighborhoodAdapters::f)
