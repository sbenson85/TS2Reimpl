#pragma once
// Shared by the cTSLotInfo tests (tests/ts_lot_info.cpp, tests/ts_lot_info_services.cpp): the class in the standalone shape
// (docs/standalone-shape.md). Every test runs twice:
// - the vtable route: our side is built by our constructor and each function is called through our object's own vtable, in the
//   slot where the game's vtable of that face holds the original RVA (so a slot-order mistake calls the wrong function);
// - the adapter route: our side is built by the original constructor (the game's vtables, as when the constructor hooks are off)
//   and the RVA hook's adapter is called, with the game's vtables copied and the adapter's own slot replaced by a trap that
//   counts (an adapter that dispatched virtually would land there; in game it would be its own hook, an endless loop).
// Vtable words are the only bytes normalised: the game's vtable of face f, ours of face f and the trap copy of face f compare
// as one code, and the cRZUnknown vtable a destroyed object is left with (the game's 0xda2fd0, ours) as another.
#include "difftest.h"
#include "game/game.h"
#include "shape.h"
#include "ts/cTSLotInfo.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <new>

namespace lotshape {

constexpr unsigned kFaces = 5;
constexpr uint32_t kFaceOffset[kFaces] = {0, lotinfo::kLotInfoFace, lotinfo::kSerialFace, lotinfo::kPersistFace,
                                          lotinfo::kDescribeFace};
constexpr uint32_t kGameVTable[kFaces] = {cTSLotInfo::kOriginalVTable, cTSLotInfo::kOriginalLotInfoVTable,
                                          cTSLotInfo::kOriginalSerialVTable, cTSLotInfo::kOriginalPersistVTable,
                                          cTSLotInfo::kOriginalDescribeVTable};
constexpr unsigned kSlotCount[kFaces] = {6, 58, 6, 12, 5};
constexpr uint32_t kGameUnknownVTable = 0xda2fd0; // cRZUnknown's, what the original destructor leaves at +0

using shape::P;
using shape::Word;

struct OurVTables {
    uint32_t face[kFaces];
    uint32_t unknown;
};

// Ours: the words our constructor stores, and what our destructor leaves at +0. Built once on a scratch buffer (the base it
// makes is the game's cTSBaseLotInfo, released again by the destructor).
inline const OurVTables &Ours()
{
    static OurVTables v{};
    static bool done = false;
    if (!done) {
        const auto &ours = shape::Ours<cTSLotInfo>(kFaceOffset);
        for (unsigned f = 0; f < kFaces; ++f) v.face[f] = P(ours.face[f]);
        v.unknown = P(ours.destroyed);
        done = true;
    }
    return v;
}

// The faces of our class sit where the original's do (base offsets are not constant expressions: checked here).
inline bool FacesWhereExpected(cTSLotInfo *lot)
{
    auto at = [&](const void *face) { return uint32_t(static_cast<const uint8_t *>(face) - reinterpret_cast<uint8_t *>(lot)); };
    return at(static_cast<rz::cRZUnknown *>(lot)) == 0 && at(static_cast<cITSLotInfo *>(lot)) == kFaceOffset[1] &&
           at(static_cast<cIGZSerializable *>(lot)) == kFaceOffset[2] &&
           at(static_cast<cIGZPersistResource *>(lot)) == kFaceOffset[3] &&
           at(static_cast<cIGZSelfDescribing *>(lot)) == kFaceOffset[4];
}

// ---- traps ----

inline int gTrapHits = 0;
inline void *gCopies[kFaces][64];

inline void *TrapFor(unsigned words) { return shape::TrapFor<shape::Count<gTrapHits>>(words); }

// Gives the object copies of the game's five vtables with a trap (popping `words` stack words) wherever the game's holds `rva`.
inline void InstallTrap(void *object, uint32_t rva, unsigned words)
{
    const void *target = GameAddress<void>(rva);
    for (unsigned f = 0; f < kFaces; ++f) {
        void *const *game = shape::GameVTable(kGameVTable[f]);
        for (unsigned s = 0; s < kSlotCount[f]; ++s) gCopies[f][s] = game[s] == target ? TrapFor(words) : game[s];
        shape::VTableAt(object, kFaceOffset[f]) = gCopies[f];
    }
}

// ---- vtable words ----

inline uint32_t VtCode(uint32_t word)
{
    const unsigned f = shape::FaceOf(word, kFaces, shape::GameTables{kGameVTable}, Ours().face, gCopies);
    if (f != ~0u) return shape::kFaceCode | f;
    // The cRZUnknown vtable a destroyed object is left with (the game's, ours) is a code of its own.
    if (word == P(GameAddress<void>(kGameUnknownVTable)) || word == Ours().unknown) return 0xd1000000u;
    return word;
}

// ---- routes and calls ----

enum Route { kVTableRoute, kAdapterRoute };
inline Route gRoute = kVTableRoute;

// The slot of a face's game vtable that holds the RVA (~0u when none does).
inline unsigned SlotOf(unsigned face, uint32_t rva) { return shape::SlotOf(kGameVTable[face], kSlotCount[face], rva); }

using shape::Words;

// Calls the function at `rva` (a slot of `face`) the side's way: the original RVA; our object's own vtable slot (vtable route);
// the adapter on an object with the trapped copies of the game's vtables (adapter route).
template <class Fn, class... A>
auto Call(difftest::Context &ctx, bool orig, void *object, unsigned face, uint32_t rva, void *adapter, A... args)
{
    void *self = static_cast<uint8_t *>(object) + kFaceOffset[face];
    if (orig) return ctx.Original<Fn>(rva)(self, args...);
    if (gRoute == kAdapterRoute) {
        InstallTrap(object, rva, Words<A...>());
        return reinterpret_cast<Fn>(adapter)(self, args...);
    }
    unsigned slot = SlotOf(face, rva);
    if (slot == ~0u) {
        ctx.Fail("rva %x is in no slot of face %u", rva, face);
        slot = 0;
    }
    void **vtable = *reinterpret_cast<void ***>(self);
    return reinterpret_cast<Fn>(vtable[slot])(self, args...);
}

// A slot called through the object's own vtable on both sides (the slots no adapter covers: folded bodies and thunks).
template <class Fn, class... A> auto CallOwn(void *object, unsigned face, unsigned slot, A... args)
{
    void *self = static_cast<uint8_t *>(object) + kFaceOffset[face];
    void **vtable = *reinterpret_cast<void ***>(self);
    return reinterpret_cast<Fn>(vtable[slot])(self, args...);
}

// Redirects a function (of the mapped image, or ours) to a stand-in (a jmp over its first five bytes) while it lives.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(difftest::Context &ctx, uint32_t rva, void *stand) : CodePatch(ctx.image.At<uint8_t>(rva), stand) {}
    CodePatch(void *where, void *stand) : site(static_cast<uint8_t *>(where))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

} // namespace lotshape

#define LOTINFO_ADAPTER_ADDRESS(f) reinterpret_cast<void *>(&cTSLotInfoAdapters::f)
