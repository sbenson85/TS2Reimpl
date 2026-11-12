#pragma once
// Shared fixture for the nTSWorld helper-module difftests (lane RY; for the WU-2 / WU-3 / SP-1 lanes after it): recording
// fakes for the objects the helper functions call through a vtable, and the game globals they are plugged into.
//
//   worldfx::Fake        an object of up to kSlots slots. Set(slot, argc, value) makes the slot take `argc` dwords (the
//                        callee pops them, as the game's thiscall slots do) and return `value` in eax; SetFloat gives a slot
//                        that returns a float in st0; SetFn gives a slot computed from its arguments. Every call is
//                        appended to the log as {slot, argc, args...}. A slot that was not set aborts the process (a
//                        test bug: the code under test called something the test did not expect).
//   worldfx::Scope       plugs fakes into the game globals for its lifetime and restores the cells after: the app
//                        singleton (0x10cc288, GetCachedSingleton), the world DB cache (0x10cc45c, the cIWorldDB face).
//   worldfx::Catalog     a cITSGlobals fake whose slot 16 answers a catalog fake whose slot 19 finds an entry fake by id;
//                        entries answer the 0x60..0xa0 slots with programmed values (kind, flags...).
//
// Both sides (original and ours) run against the same fakes and the logs are compared by the test.
#include "difftest.h"
#include "game/game.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace worldfx {

constexpr unsigned kSlots = 192; // 192 for the object faces of the height queries (slot 178, lane RZ)
constexpr unsigned kMaxArgs = 8;

struct Fake;
using SlotFn = uint32_t (*)(Fake *self, const uint32_t *args);
using FloatFn = float (*)(Fake *self, const uint32_t *args);

struct Fake {
    void **vt;
    uint32_t id;
    uint32_t ret[kSlots];
    float retFloat[kSlots];
    SlotFn fn[kSlots];
    FloatFn ffn[kSlots];
    uint8_t argc[kSlots];
    bool isFloat[kSlots];
    bool isSet[kSlots];
    void *table[kSlots];

    void Reset(uint32_t newId);
    void Set(unsigned slot, unsigned args, uint32_t value = 0, SlotFn custom = nullptr);
    void SetFloat(unsigned slot, unsigned args, float value);
    void SetFloatFn(unsigned slot, unsigned args, FloatFn custom); // a float slot computed from its arguments
};

// The calls both sides made, flattened: slot, argument count, the arguments; each call preceded by the fake's id.
inline std::vector<uint32_t> gLog;
inline bool gLogging = true;

namespace detail {

inline void Dispatch(Fake *self, unsigned slot, unsigned count, const uint32_t *args)
{
    if (!self->isSet[slot]) {
        std::fprintf(stderr, "worldfx: fake #%u slot %u called but not set\n", self->id, slot);
        std::abort();
    }
    if (gLogging) {
        gLog.push_back(self->id);
        gLog.push_back(slot);
        gLog.push_back(count);
        for (unsigned i = 0; i < count; ++i) gLog.push_back(args[i]);
    }
}

template <size_t> using Word = uint32_t;
template <unsigned Slot, class Seq> struct GenInt;
template <unsigned Slot, size_t... I> struct GenInt<Slot, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(Fake *self, void *, Word<I>... a)
    {
        const uint32_t args[] = {a..., 0u};
        Dispatch(self, Slot, unsigned(sizeof...(I)), args);
        return self->fn[Slot] ? self->fn[Slot](self, args) : self->ret[Slot];
    }
};
template <unsigned Slot, class Seq> struct GenFloat;
template <unsigned Slot, size_t... I> struct GenFloat<Slot, std::index_sequence<I...>> {
    static float __fastcall Fn(Fake *self, void *, Word<I>... a)
    {
        const uint32_t args[] = {a..., 0u};
        Dispatch(self, Slot, unsigned(sizeof...(I)), args);
        return self->ffn[Slot] ? self->ffn[Slot](self, args) : self->retFloat[Slot];
    }
};

inline void *gInt[kSlots][kMaxArgs + 1];
inline void *gFlt[kSlots][kMaxArgs + 1];
inline bool gFilled = false;

template <unsigned Slot, size_t... N> void FillSlot(std::index_sequence<N...>)
{
    ((gInt[Slot][N] = reinterpret_cast<void *>(&GenInt<Slot, std::make_index_sequence<N>>::Fn),
      gFlt[Slot][N] = reinterpret_cast<void *>(&GenFloat<Slot, std::make_index_sequence<N>>::Fn)),
     ...);
}
template <size_t... S> void FillAll(std::index_sequence<S...>)
{
    (FillSlot<unsigned(S)>(std::make_index_sequence<kMaxArgs + 1>()), ...);
}

} // namespace detail

inline void Fake::Reset(uint32_t newId)
{
    if (!detail::gFilled) detail::FillAll(std::make_index_sequence<kSlots>()), detail::gFilled = true;
    vt = table;
    id = newId;
    for (unsigned i = 0; i < kSlots; ++i) {
        ret[i] = 0;
        retFloat[i] = 0;
        fn[i] = nullptr;
        ffn[i] = nullptr;
        argc[i] = 0;
        isFloat[i] = false;
        isSet[i] = false;
        table[i] = nullptr;
    }
}

inline void Fake::Set(unsigned slot, unsigned args, uint32_t value, SlotFn custom)
{
    if (slot >= kSlots || args > kMaxArgs) std::abort();
    ret[slot] = value;
    fn[slot] = custom;
    argc[slot] = uint8_t(args);
    isFloat[slot] = false;
    isSet[slot] = true;
    table[slot] = detail::gInt[slot][args];
}

inline void Fake::SetFloat(unsigned slot, unsigned args, float value)
{
    if (slot >= kSlots || args > kMaxArgs) std::abort();
    retFloat[slot] = value;
    fn[slot] = nullptr;
    ffn[slot] = nullptr;
    argc[slot] = uint8_t(args);
    isFloat[slot] = true;
    isSet[slot] = true;
    table[slot] = detail::gFlt[slot][args];
}

inline void Fake::SetFloatFn(unsigned slot, unsigned args, FloatFn custom)
{
    SetFloat(slot, args, 0.0f);
    ffn[slot] = custom;
}

// A fake whose interface has a virtual base (a vbptr at +4, as cIIndexedMeshBuilder's): derived's word at +4 (its id,
// so its log entries carry the vbtable's address) points at vbtable, whose second entry leads to base.
inline void SetVirtualBase(Fake &derived, Fake &base, int32_t (&vbtable)[2])
{
    vbtable[0] = -4;
    vbtable[1] = int32_t(reinterpret_cast<uint8_t *>(&base) - (reinterpret_cast<uint8_t *>(&derived) + 4));
    derived.id = uint32_t(reinterpret_cast<uintptr_t>(vbtable));
}

// Game globals (pointer cells or words) set for the scope and restored after it.
class Scope {
public:
    Scope() = default;
    ~Scope()
    {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) *GameAddress<uint32_t>(it->first) = it->second;
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

    void Set(uint32_t rva, const void *value)
    {
        saved_.push_back({rva, *GameAddress<uint32_t>(rva)});
        *GameAddress<uint32_t>(rva) = uint32_t(reinterpret_cast<uintptr_t>(value));
    }
    void SetWord(uint32_t rva, uint32_t value)
    {
        saved_.push_back({rva, *GameAddress<uint32_t>(rva)});
        *GameAddress<uint32_t>(rva) = value;
    }

private:
    std::vector<std::pair<uint32_t, uint32_t>> saved_;
};

constexpr uint32_t kAppCell = 0x10cc288;     // GetCachedSingleton's cache
constexpr uint32_t kWorldDbCell = 0x10cc45c; // the cIWorldDB face (CachedServiceGetter 0x4a037ee3 / 0x49ff7d6f)

// The world DB fake: every slot an nTSWorld helper may call must be Set by the test before it runs.
inline Fake gWorldDb;

// A fake catalog: ids map to entry fakes. Slot 16 of the app fake answers the catalog, slot 19 of the catalog the entry.
class Catalog {
public:
    static constexpr unsigned kEntries = 8;

    Catalog() { Rebuild(); }

    // Forgets the entries and the log; the app and catalog fakes are set afresh.
    void Rebuild()
    {
        gApp.Reset(300);
        gCatalog.Reset(301);
        for (unsigned i = 0; i < kEntries; ++i) gEntry[i].Reset(310 + i);
        gIds.clear();
        gApp.Set(16, 0, uint32_t(reinterpret_cast<uintptr_t>(&gCatalog)));
        gCatalog.Set(19, 1, 0, &Find);
        gLog.clear();
    }
    // Makes id `id` map to entry fake `index` (or to no entry with index >= kEntries).
    void Map(uint32_t id, unsigned index) { gIds[id] = index; }
    Fake &Entry(unsigned index) { return gEntry[index]; }
    Fake &App() { return gApp; }
    Fake &CatalogFake() { return gCatalog; } // the catalog itself: tests Set its other slots (26, 27, 31... lane RZ)
    void Plug(Scope &scope) { scope.Set(kAppCell, &gApp); }

private:
    static uint32_t Find(Fake *, const uint32_t *args)
    {
        auto it = gIds.find(args[0]);
        if (it == gIds.end() || it->second >= kEntries) return 0;
        return uint32_t(reinterpret_cast<uintptr_t>(&gEntry[it->second]));
    }
    static inline Fake gApp, gCatalog, gEntry[kEntries];
    static inline std::map<uint32_t, unsigned> gIds;
};

} // namespace worldfx
