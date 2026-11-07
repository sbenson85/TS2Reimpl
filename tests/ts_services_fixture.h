#pragma once
// Shared fixture for the service-heavy cTSLotInfo / cTSNeighbor difftests (tests/ts_lot_info_services.cpp,
// tests/ts_neighbor_services.cpp): a set of recording fakes plus the game globals they are plugged into.
//
// A fake (Obj) answers any slot set on it with a recording function of the right argument count (0..7 dwords): every call
// appends {evCall, object id, slot, argument count, normalised arguments...} to the event log and returns the slot's
// programmed value, or what a custom handler computes (for out-parameters). Slots 1 / 2 are AddRef / Release and count.
//
// Every argument has a declared kind (tests/norm_words.h; docs/lane/test.md, "Normalised words"): Set(slot, "pd", ...) says
// argument 0 is a pointer ('p': logged as its object and offset: a fake as 200 + index, a registered block as tag | offset)
// and argument 1 data ('d': logged as it is); '-' leaves one undeclared. Set(slot, count, ...) leaves them all undeclared
// (Data / Ptr can declare some): an undeclared argument that is not zero fails the test with what to declare. Norm(word) is the pointer kind for a
// test's own logging; Values("...") declares the fake's value[] words for CaptureFakes.
//
// Globals plugs the fakes into the game: the app singleton (0x10cc288, GetCachedSingleton), the persist system cache
// (0x10faf6c), the director static (0x10c9390; its slot 12, the framework, answers a chosen fake and its slot 3 is the
// framework's CreateInstance), and any other cell a test sets (saved and restored). AllocatorScope builds the game's allocator service.

#include "code_patch.h"
#include "game_allocator.h"
#include "difftest.h"
#include "free_recorder.h"
#include "norm_words.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"

#include <Windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

namespace svcfx {

using Blob = std::vector<uint32_t>;
constexpr unsigned kSlots = 80;
constexpr unsigned kFakes = 24;
enum : uint32_t { evCall = 0x60, evNote = 0x61 };

struct Obj;
using Custom = uint32_t (*)(Obj *self, uint32_t *args);

constexpr unsigned kMaxArgs = 8;
struct Obj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[8]; // free words for custom handlers
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
    char kinds[kSlots][kMaxArgs]; // per slot and argument: 'p' pointer, 'd' data, 0 undeclared
    char valueKinds[8];           // the same for value[] (CaptureFakes)
    const char *setFile[kSlots];  // where the slot was Set, for the errors
    unsigned setLine[kSlots];

    void Reset(uint32_t newId);
    // Makes `slot` take dword arguments of the given kinds ('p' pointer, 'd' data, 'x' logged by the slot's custom
    // instead, as the Set's comment says; as many arguments as letters) and
    // return `result` (or what `fn` computes).
    template <size_t N>
    void Set(unsigned slot, const char (&argKinds)[N], uint32_t result = 0, Custom fn = nullptr,
             std::source_location where = std::source_location::current())
    {
        static_assert(N - 1 < kMaxArgs, "at most 7 arguments");
        Set(slot, unsigned(N - 1), result, fn, where);
        for (unsigned i = 0; i < kMaxArgs; ++i) kinds[slot][i] = i < N - 1 ? Kind(argKinds[i]) : 0;
    }
    template <size_t N>
    void Set(unsigned slot, const char (&argKinds)[N], const void *result, std::source_location where = std::source_location::current())
    {
        Set(slot, argKinds, uint32_t(uintptr_t(result)), nullptr, where);
    }
    // The same with `args` arguments of undeclared kinds (one that is not zero fails the test: declare it, here or with
    // Data / Ptr after this Set).
    void Set(unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr,
             std::source_location where = std::source_location::current());
    void Set(unsigned slot, unsigned args, const void *result, std::source_location where = std::source_location::current())
    {
        Set(slot, args, uint32_t(uintptr_t(result)), nullptr, where);
    }
    // Declares arguments of `slot` (bit i: argument i) as data (logged as they are) or as pointers (logged as records).
    void Data(unsigned slot, uint8_t mask) { Mark(slot, mask, 'd'); }
    void Ptr(unsigned slot, uint8_t mask) { Mark(slot, mask, 'p'); }
    // Declares the kinds of value[0..]: CaptureFakes logs them by these.
    template <size_t N> void Values(const char (&wordKinds)[N])
    {
        static_assert(N - 1 <= 8, "eight value words");
        for (unsigned i = 0; i < 8; ++i) valueKinds[i] = i < N - 1 ? Kind(wordKinds[i]) : 0;
    }

    static char Kind(char k)
    {
        if (k == '-') return 0;                 // undeclared: the test only ever sees it zero
        if (k != 'p' && k != 'd' && k != 'x') std::abort(); // a kind string has only 'p', 'd', 'x' and '-'
        return k;
    }

private:
    void Mark(unsigned slot, uint8_t mask, char kind)
    {
        for (unsigned i = 0; i < kMaxArgs; ++i)
            if (mask >> i & 1) kinds[slot][i] = kind;
    }
};

inline Obj gObjs[kFakes];
inline Blob gEv;
using Range = normwords::Range;
inline std::vector<Range> gRanges;
inline unsigned gFrameworkIndex = 0;
inline bool gTrace = false;

inline Obj &F(unsigned i) { return gObjs[i]; }
inline uint32_t P(const void *p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }
// The log's and the ranges' own growth is the test's, never the code under test's (difftest::AllocationCounter).
inline void Ev(uint32_t a)
{
    difftest::NotCounted bookkeeping;
    gEv.push_back(a);
}
// Registers a block both sides build at different addresses as the object `tag`: pointers into it log as tag | offset.
inline void AddRange(const void *p, size_t size, uint32_t tag, std::source_location where = std::source_location::current())
{
    difftest::NotCounted bookkeeping;
    const uintptr_t lo = reinterpret_cast<uintptr_t>(p);
    normwords::CheckTag(gRanges, lo, lo + size, tag, where);
    gRanges.push_back({lo, lo + size, tag});
}

// A pointer's record (tests/norm_words.h): null 0, a fake 200 + index (inside one: 0xfa000000 | index << 12 | offset), a
// registered block tag | offset, the stack 0x5ac00000. An unregistered pointer fails the test with describe()'s text.
template <class Describe> uint32_t PointerAt(uint32_t value, Describe &&describe)
{
    if (!value) return 0;
    for (unsigned i = 0; i < kFakes; ++i) {
        const uint32_t at = P(&gObjs[i]);
        uint32_t record = 0;
        if (value == at) record = 200 + i;
        else if (value > at && value < at + sizeof(Obj)) record = normwords::kFakeInterior | i << 12 | (value - at);
        if (!record) continue;
        if (normwords::KindsFile()) normwords::Registered(record, describe());
        return record;
    }
    uint32_t record;
    if (!normwords::Pointer(value, gRanges, record)) normwords::Unregistered(value, describe());
    else if (normwords::KindsFile()) normwords::Registered(record, describe());
    return record;
}
// The pointer kind, for a test's own logging: `value` must be a pointer (data is logged as it is, with Ev).
inline uint32_t Norm(uint32_t value, std::source_location where = std::source_location::current())
{
    return PointerAt(value, [&] { return "Norm at " + normwords::Site(where); });
}
// An out pointer the call may leave as the test set it (`unset`, a sentinel no pointer has): a union of "left alone"
// (normwords::kUntouched) and a pointer.
inline uint32_t NormOut(uint32_t value, uint32_t unset = 0x5e5e5e5e,
                        std::source_location where = std::source_location::current())
{
    return value == unset ? normwords::kUntouched : PointerAt(value, [&] { return "NormOut at " + normwords::Site(where); });
}
// A word the test filled with either one of its fakes or data (a union the test made): tagged by identity with the
// fakes' exact addresses, never by range. Pushes 1 and the fake's id, or 0 and the word as it is.
inline void FakeOrData(std::vector<uint32_t> &b, uint32_t value)
{
    for (unsigned i = 0; i < kFakes; ++i)
        if (value == P(&gObjs[i])) return b.push_back(1), b.push_back(200 + i);
    b.push_back(0), b.push_back(value);
}

namespace detail {
inline void *gTable[kSlots][8];
inline bool gFilled = false;

inline std::string SlotName(const Obj *self, unsigned slot)
{
    const char *file = self->setFile[slot] ? self->setFile[slot] : "?";
    for (const char *p = file; *p; ++p)
        if (*p == '\\' || *p == '/') file = p + 1;
    char text[160];
    std::snprintf(text, sizeof(text), "fake #%u slot %u (Set at %s:%u)", self->id, slot, file, self->setLine[slot]);
    return text;
}
// One argument of a generic slot, by its declared kind.
inline uint32_t Arg(const Obj *self, unsigned slot, unsigned i, uint32_t a)
{
    const char kind = self->kinds[slot][i];
    if (kind == 'd') return a;
    if (kind == 'p') return PointerAt(a, [&] { return SlotName(self, slot) + " argument " + std::to_string(i); });
    if (kind == 'x') return 0x5c1d0000; // not compared here: the slot's custom logs it its own way (said at the Set)
    if (!a) return 0;
    char declare[160];
    std::snprintf(declare, sizeof(declare), "in the Set: Set(%u, \"...\") with 'p' (pointer) or 'd' (data) for each of "
                  "its arguments, or Data / Ptr(%u, %#x)", slot, slot, 1u << i);
    return normwords::Undeclared(a, SlotName(self, slot) + " argument " + std::to_string(i), declare, gRanges);
}

inline uint32_t Dispatch(Obj *self, unsigned slot, uint32_t *args, unsigned count)
{
    if (gTrace) fprintf(stderr, "call obj %u slot %u args %u\n", self->id, slot, count);
    Ev(evCall);
    Ev(self->id);
    Ev(slot);
    Ev(count);
    for (unsigned i = 0; i < count; ++i) Ev(Arg(self, slot, i, args[i]));
    if (self->custom[slot]) return self->custom[slot](self, args);
    if (slot == 1) return uint32_t(++self->refs);
    if (slot == 2) return uint32_t(--self->refs);
    return self->ret[slot];
}

template <size_t> using Word = uint32_t;
template <unsigned Slot, class Seq> struct Gen;
template <unsigned Slot, size_t... I> struct Gen<Slot, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(Obj *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        return Dispatch(self, Slot, args, unsigned(sizeof...(I)));
    }
};
template <unsigned Slot> void FillSlot()
{
    gTable[Slot][0] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<0>>::Fn);
    gTable[Slot][1] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<1>>::Fn);
    gTable[Slot][2] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<2>>::Fn);
    gTable[Slot][3] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<3>>::Fn);
    gTable[Slot][4] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<4>>::Fn);
    gTable[Slot][5] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<5>>::Fn);
    gTable[Slot][6] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<6>>::Fn);
    gTable[Slot][7] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<7>>::Fn);
}
template <size_t... S> void FillAll(std::index_sequence<S...>) { (FillSlot<unsigned(S)>(), ...); }
} // namespace detail

inline void Obj::Reset(uint32_t newId)
{
    if (!detail::gFilled) detail::FillAll(std::make_index_sequence<kSlots>()), detail::gFilled = true;
    vt = table;
    id = newId;
    refs = 1000;
    for (auto &v : value) v = 0;
    std::memset(valueKinds, 0, sizeof(valueKinds));
    std::memset(kinds, 0, sizeof(kinds));
    for (unsigned i = 0; i < kSlots; ++i) ret[i] = 0, custom[i] = nullptr, table[i] = nullptr, setFile[i] = nullptr, setLine[i] = 0;
    Set(1, 0);
    Set(2, 0);
}

inline void Obj::Set(unsigned slot, unsigned args, uint32_t result, Custom fn, std::source_location where)
{
    table[slot] = detail::gTable[slot][args];
    ret[slot] = result;
    custom[slot] = fn;
    setFile[slot] = where.file_name();
    setLine[slot] = where.line();
    std::memset(kinds[slot], 0, sizeof(kinds[slot])); // a Set starts the slot's declarations afresh
}

// Blocks a fake hands to the code under test from the allocator service; whether each was freed afterwards is compared.
inline std::vector<void *> gAllocated;
inline bool BlockFreed(const void *mem)
{
    // dlmalloc chunk: size at -4 (bit 0 PREV_INUSE, bit 2 IN_FAST_BIN); a chunk that is not in a fast bin is free when the next
    // chunk no longer says its predecessor is in use.
    const auto *chunk = static_cast<const uint8_t *>(mem) - 8;
    const uint32_t size = *reinterpret_cast<const uint32_t *>(chunk + 4);
    if (size & 4) return true;
    const uint32_t next = *reinterpret_cast<const uint32_t *>(chunk + (size & ~7u) + 4);
    return !(next & 1);
}
inline void CaptureAllocated(Blob &b)
{
    for (void *block : gAllocated) b.push_back(BlockFreed(block));
}

// Resets every fake (ids 200 + index, no slots set but AddRef / Release, refs 1000), the event log and the ranges.
// (Not gFrameworkIndex: tests rebuild their fakes per seed with the index already chosen; Globals restores it.)
inline void ResetAll()
{
    for (unsigned i = 0; i < kFakes; ++i) gObjs[i].Reset(200 + i);
    gEv.clear();
    gAllocated.clear();
    gRanges.clear();
    normwords::StartHeap(); // a side starts: its heap blocks are numbered from here
}

// A word of declared kind `kind` ('p', 'd', or 0: undeclared, which fails unless the word is zero). `what` names it in
// the errors, `declare` says how to declare it.
template <class What> uint32_t Word(char kind, uint32_t v, What &&what, const char *declare)
{
    if (kind == 'd') return v;
    if (kind == 'p') return PointerAt(v, what);
    return v ? normwords::Undeclared(v, what(), declare, gRanges) : 0;
}

namespace detail {
// Logs `count` words (read(i)) by a kind per word in `kinds`: 'p' pointer, 'd' data, 'b' / 'h' data of which only the
// low byte / half is written (the rest is whatever the memory held), 'n' a pointer into a freed block (logged as
// nonzero or not), 'o' an out pointer that may still hold the 0x5e
// fill (NormOut), 'x' not logged (a word the test does not compare: say why next to the string), '-' undeclared (fails
// unless zero). Spaces are skipped (group words for reading); words past the string's end are undeclared.
template <class Name> void KindWord(Blob &b, char kind, uint32_t v, Name &&name)
{
    switch (kind) {
    case 'x': break;
    case 'b': b.push_back(v & 0xff); break;
    case 'h': b.push_back(v & 0xffff); break;
    case 'n': b.push_back(v != 0); break; // a pointer into a block already freed (a destroyed object's member)
    case 'o': b.push_back(v == 0x5e5e5e5e ? normwords::kUntouched : PointerAt(v, name)); break;
    case 'p':
    case 'd':
    case '-':
        b.push_back(svcfx::Word(kind == '-' ? 0 : kind, v, name,
                         "in the kinds string: one character per word, 'p' pointer, 'd' data ('b' / 'h' low byte / "
                         "half, 'o' out, 'x' skipped)"));
        break;
    default: std::abort(); // a test bug: an unknown kind
    }
}
template <class Read, class What> void DumpKinds(Blob &b, size_t count, const char *kinds, Read &&read, What &&what)
{
    const char *k = kinds;
    for (size_t i = 0; i < count; ++i) {
        while (*k == ' ') ++k;
        KindWord(b, *k ? *k++ : '-', read(i), [&] { return what(i); });
    }
}
} // namespace detail

// One word of a test's own dump loop, at byte offset `off` of its object: the kind of word off / 4 in `kinds`
// (detail::DumpKinds' letters and spaces; past the string's end undeclared). For loops that skip or decode words.
inline void Field(Blob &b, uint32_t v, const char *kinds, uint32_t off,
                  std::source_location where = std::source_location::current())
{
    const char *k = kinds;
    for (uint32_t i = 0;; ++k) {
        if (*k == ' ') continue;
        if (!*k || i++ == off / 4) break;
    }
    detail::KindWord(b, *k ? *k : '-', v, [&] {
        char text[200];
        std::snprintf(text, sizeof(text), "Field at %s word +%#x", normwords::Site(where).c_str(), unsigned(off));
        return std::string(text);
    });
}

// Logs `bytes` (a multiple of 4) at `p` word by word, by a kind per word (detail::DumpKinds).
inline void DumpWords(Blob &b, const void *p, size_t bytes, const char *kinds,
                      std::source_location where = std::source_location::current())
{
    const auto *w = static_cast<const uint32_t *>(p);
    detail::DumpKinds(b, bytes / 4, kinds, [&](size_t i) { return w[i]; }, [&](size_t i) {
        char text[200];
        std::snprintf(text, sizeof(text), "DumpWords at %s word +%#x", normwords::Site(where).c_str(), unsigned(i * 4));
        return std::string(text);
    });
}
// Logs the original's global words at `cells` (rvas), by a kind per cell (detail::DumpKinds).
template <size_t N>
void DumpCells(Blob &b, const uint32_t (&cells)[N], const char *kinds, std::source_location where = std::source_location::current())
{
    detail::DumpKinds(b, N, kinds, [&](size_t i) { return *GameAddress<uint32_t>(cells[i]); }, [&](size_t i) {
        char text[200];
        std::snprintf(text, sizeof(text), "DumpCells at %s cell #%u (%#x)", normwords::Site(where).c_str(), unsigned(i), cells[i]);
        return std::string(text);
    });
}

// A test's table of its fakes' value-word kinds (next to its fakes): the fake's index, a kind per value word from value[0]
// ('p' pointer, 'd' data, '-' undeclared). CaptureFakes(b, table) logs by it; a fake's own Values() declares the same.
struct ValueKinds {
    unsigned fake;
    const char *kinds;
};

// Adds every fake's reference count and value words (by their kinds: Obj::Values, or the table) to a blob.
inline void CaptureFakes(Blob &b, const ValueKinds *table, size_t count,
                         std::source_location where = std::source_location::current())
{
    char kinds[kFakes][8];
    for (unsigned f = 0; f < kFakes; ++f) std::memcpy(kinds[f], gObjs[f].valueKinds, 8);
    for (size_t t = 0; t < count; ++t) {
        if (table[t].fake >= kFakes || std::strlen(table[t].kinds) > 8) std::abort(); // a test bug
        for (unsigned i = 0; table[t].kinds[i]; ++i) kinds[table[t].fake][i] = Obj::Kind(table[t].kinds[i]);
    }
    for (const Obj &o : gObjs) {
        b.push_back(uint32_t(o.refs));
        for (unsigned i = 0; i < 8; ++i)
            b.push_back(Word(kinds[&o - gObjs][i], o.value[i],
                             [&] {
                                 return "fake #" + std::to_string(o.id) + " value[" + std::to_string(i) + "] (CaptureFakes at " +
                                        normwords::Site(where) + ")";
                             },
                             "in the test's svcfx::ValueKinds table (or with the fake's Values(\"...\")): 'p' pointer, 'd' data per "
                             "value word"));
    }
}
inline void CaptureFakes(Blob &b, std::source_location where = std::source_location::current())
{
    CaptureFakes(b, nullptr, 0, where);
}
template <size_t N>
void CaptureFakes(Blob &b, const ValueKinds (&table)[N], std::source_location where = std::source_location::current())
{
    CaptureFakes(b, table, N, where);
}

namespace detail {
inline void *gDirectorVT[16];
inline void *__fastcall DirectorFramework(void *, void *) { return &gObjs[gFrameworkIndex]; }
} // namespace detail

// Plugs the fakes into the game's globals for the scope. Cells set through Set() are restored afterwards, and so is
// gFrameworkIndex (a test that picks the director framework's fake leaves it as it found it).
class Globals {
public:
    Globals() : frameworkIndex_(gFrameworkIndex)
    {
        Remember(0x10c9390);
        Remember(0x10c93f4);
        detail::gDirectorVT[12] = reinterpret_cast<void *>(&detail::DirectorFramework);
        *GameAddress<void *>(0x10c9390) = detail::gDirectorVT;
        *GameAddress<int32_t>(0x10c93f4) = INT32_MIN;
    }
    ~Globals()
    {
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) *GameAddress<uint32_t>(it->first) = it->second;
        gFrameworkIndex = frameworkIndex_;
    }
    Globals(const Globals &) = delete;
    Globals &operator=(const Globals &) = delete;

    // Sets a game global (a pointer cell, or a word) and remembers its old value.
    void Set(uint32_t rva, const void *value)
    {
        Remember(rva);
        *GameAddress<uint32_t>(rva) = P(value);
    }
    void SetWord(uint32_t rva, uint32_t value)
    {
        Remember(rva);
        *GameAddress<uint32_t>(rva) = value;
    }

private:
    void Remember(uint32_t rva) { saved_.push_back({rva, *GameAddress<uint32_t>(rva)}); }
    unsigned frameworkIndex_;
    std::vector<std::pair<uint32_t, uint32_t>> saved_;
};

// The game's allocator service (0x10c95d4) over the game's general allocator, for the whole scope of a test (build it once:
// shutting the allocator down and building it again for every case exhausts the address space after a few hundred cases), so
// the RZMemory blocks both sides make and the original frees through the service meet.
class AllocatorScope {
public:
    AllocatorScope() : savedService_(*GameAddress<uint32_t>(0x10c95d4)), savedGuard_(*GameAddress<int32_t>(0x10c95d8))
    {
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr), built_ = true;
        // The game's vtable, with its allocation slots reporting their blocks to the normalised words' heap registry (the
        // general allocator takes its cores from VirtualAlloc: the heap wrappers never see its blocks).
        std::memcpy(gServiceVT, GameAddress<void>(0xda3cd4), sizeof(gServiceVT));
        std::memcpy(gGameSlots, gServiceVT + 3, sizeof(gGameSlots));
        gServiceVT[3] = reinterpret_cast<void *>(&Allocate);
        gServiceVT[4] = reinterpret_cast<void *>(&Free);
        gServiceVT[5] = reinterpret_cast<void *>(&Reallocate);
        const void *serviceVtable = gServiceVT;
        std::memcpy(service_, &serviceVtable, sizeof(serviceVtable));
        *GameAddress<void *>(0x10c95d4) = service_;
        *GameAddress<int32_t>(0x10c95d8) = INT32_MIN;
    }
    ~AllocatorScope()
    {
        *GameAddress<uint32_t>(0x10c95d4) = savedService_;
        *GameAddress<int32_t>(0x10c95d8) = savedGuard_;
        if (built_) {
            auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
            difftest_alloc::ShutdownAndRelease(ga);
        }
    }
    AllocatorScope(const AllocatorScope &) = delete;
    AllocatorScope &operator=(const AllocatorScope &) = delete;

private:
    // The service's slots 3-5 (thiscall: Allocate(bytes), Free(mem), Reallocate(mem, bytes)): the game's, then reported.
    static inline void *gServiceVT[6];
    static inline void *gGameSlots[3];
    static void *__fastcall Allocate(void *self, void *, uint32_t bytes)
    {
        void *p = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(gGameSlots[0])(self, bytes);
        difftest::WatchedAllocation(p, bytes);
        return p;
    }
    static void __fastcall Free(void *self, void *, void *mem)
    {
        difftest::WatchedFree(mem);
        reinterpret_cast<void(__thiscall *)(void *, void *)>(gGameSlots[1])(self, mem);
    }
    static void *__fastcall Reallocate(void *self, void *, void *mem, uint32_t bytes)
    {
        void *p = reinterpret_cast<void *(__thiscall *)(void *, void *, uint32_t)>(gGameSlots[2])(self, mem, bytes);
        if (p && p == mem) difftest::WatchedResize(p, bytes);
        else if (p) difftest::WatchedFree(mem), difftest::WatchedAllocation(p, bytes);
        else if (!bytes) difftest::WatchedFree(mem); // a realloc to 0 bytes frees
        return p;
    }
    // RZMemory's Malloc / Free / Realloc (0x1be3b0 / 0x1be3a1 / 0x1be3c1), the image's and ours, stood in by the same
    // calls on the general allocator that report their blocks too: code that calls them directly (not through the
    // service) makes blocks the heap registry would otherwise never see, so pointers into them would be unregistered.
    static void *__cdecl WatchedMalloc(uint32_t bytes)
    {
        void *p = RZMemory::GlobalAllocator()->Malloc(bytes, 0);
        difftest::WatchedAllocation(p, bytes);
        return p;
    }
    static void __cdecl WatchedFreeBlock(void *mem)
    {
        difftest::WatchedFree(mem);
        RZMemory::GlobalAllocator()->Free(mem);
    }
    static void *__cdecl WatchedRealloc(void *mem, uint32_t bytes)
    {
        void *p = RZMemory::GlobalAllocator()->Realloc(mem, bytes, 0);
        if (p && p == mem) difftest::WatchedResize(p, bytes);
        else if (p) difftest::WatchedFree(mem), difftest::WatchedAllocation(p, bytes);
        else if (!bytes) difftest::WatchedFree(mem);
        return p;
    }
    alignas(8) uint8_t service_[16] = {};
    uint32_t savedService_;
    int32_t savedGuard_;
    bool built_ = false;
    shape::CodePatch malloc_{0x1be3b0, reinterpret_cast<const void *>(&WatchedMalloc)};
    shape::CodePatch free_{0x1be3a1, reinterpret_cast<const void *>(&WatchedFreeBlock)};
    shape::CodePatch realloc_{0x1be3c1, reinterpret_cast<const void *>(&WatchedRealloc)};
    shape::CodePatch ourMalloc_{shape::CodeOf(&RZMemory::Malloc), reinterpret_cast<const void *>(&WatchedMalloc)};
    shape::CodePatch ourFree_{shape::CodeOf(&RZMemory::Free), reinterpret_cast<const void *>(&WatchedFreeBlock)};
    shape::CodePatch ourRealloc_{shape::CodeOf(&RZMemory::Realloc), reinterpret_cast<const void *>(&WatchedRealloc)};
};

// The common cells: app singleton, persist system cache.
constexpr uint32_t kAppCell = 0x10cc288;
constexpr uint32_t kPersistCell = 0x10faf6c;
constexpr uint32_t kSlot23Cache = 0x10cc2a8;
constexpr uint32_t kSecondLanguageCache = 0x10cc2dc;

// ---- message posting (additive: used by tests/ts_neighbor_manager_services.cpp) ----
//
// The message server the code reaches through 0x9c5a89 (cache cell 0x10f519c): the fake's slot 3 (post) is a Custom handler that
// calls LogMessage, because the message is a heap block the generic log would print raw. The message is a real
// cRZMessage2Standard; the log gets its count, flags, id, the four values and whether each pointer field is set.
constexpr uint32_t kMessageServerCell = 0x10f519c;
constexpr uint32_t evMessage = 0x62;
// The kinds of a message's four values, by message id (DeclareMessage): a message's values are pointers or data by its id
// (the discriminator). Cleared before each test.
struct MessageKinds {
    uint32_t id;
    char kinds[4];
};
inline std::vector<MessageKinds> gMessageKinds;
template <size_t N> void DeclareMessage(uint32_t id, const char (&kinds)[N])
{
    static_assert(N == 5, "four value kinds");
    for (MessageKinds &m : gMessageKinds)
        if (m.id == id) return void(std::memcpy(m.kinds, kinds, 4));
    MessageKinds m{id, {}};
    std::memcpy(m.kinds, kinds, 4);
    gMessageKinds.push_back(m);
}
inline void ClearMessageKinds() { gMessageKinds.clear(); }
inline void NoReport(bool, std::vector<std::string> &) {}
inline const bool gMessageKindsHooked = (difftest::FailureNoteSources().push_back({&ClearMessageKinds, &NoReport}), true);

// A message's four values (words 4..7), by its id's declared kinds.
// The game's messages whose values every test logs alike (a test's DeclareMessage comes first): the kinds of the four
// values ('-' undeclared: fails unless zero). The ids' meanings are in src/ where they are sent.
inline const MessageKinds kKnownMessages[] = {
    {0x9c, {'d', '-', '-', '-'}},       // a neighbour's effects removed: the neighbour id
    {0x9d, {'d', '-', '-', '-'}},       // a neighbour removed: its id
    {0x9e, {'d', '-', '-', '-'}},       // a neighbour retired: its id
    {0xae, {'d', '-', '-', '-'}},       // a lot added: its id
    {0xaf, {'d', '-', '-', '-'}},       // a lot removed: its id
    {0xc6, {'d', '-', '-', '-'}},       // a neighbourhood member removed: its id
    {0x4b46aebf, {'d', '-', '-', '-'}}, // the game speed (cTSMainToolbar)
    {0xcbccdece, {'d', '-', '-', '-'}}, // the pause state (cTSMainToolbar)
    {0x62ba7b44, {'d', 'p', '-', '-'}}, // a message box request (cGZWinMgrW32): a code, the request
    {0xaf6129b0, {'p', '-', '-', '-'}}, // a route done (PrimR, cTSSimSystem): the object
};
inline void LogMessageValues(const uint32_t *words)
{
    const MessageKinds *declared = nullptr;
    for (const MessageKinds &m : kKnownMessages)
        if (m.id == words[3]) declared = &m;
    for (const MessageKinds &m : gMessageKinds)
        if (m.id == words[3]) declared = &m;
    for (int i = 4; i < 8; ++i) { // values, by the id's kinds
        char declare[120];
        std::snprintf(declare, sizeof(declare), "with svcfx::DeclareMessage(%#x, \"....\") ('p' pointer, 'd' data per value)",
                      words[3]);
        Ev(Word(declared ? declared->kinds[i - 4] : 0, words[i],
                [&] { return "message " + std::to_string(words[3]) + " value " + std::to_string(i - 4) + " (LogMessage)"; },
                declare));
    }
}
inline void LogMessage(uint32_t pointer)
{
    Ev(evMessage);
    if (!pointer) {
        Ev(0);
        return;
    }
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(pointer));
    Ev(words[1]);                    // reference count
    Ev(words[2] & 0xff);             // flags
    Ev(words[3]);                    // id
    LogMessageValues(words);
    for (int i = 8; i < 11; ++i) Ev(words[i] != 0); // pointer fields: whether each is set
}
// Slot 3 of a message server fake: logs the call like a generic slot does, but with the message's contents instead of its address.
inline uint32_t __fastcall PostMessageShim(Obj *self, void *, uint32_t message)
{
    Ev(evCall);
    Ev(self->id);
    Ev(3);
    Ev(1);
    LogMessage(message);
    return self->ret[3];
}
// Makes `server` take posted messages (slot 3, one argument) and log them by content.
inline void UseAsMessageServer(Obj &server, uint32_t result = 0)
{
    server.Set(3, 1, result);
    server.table[3] = reinterpret_cast<void *>(&PostMessageShim);
}

inline bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return true;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: differs at word %u of %u/%u (orig %08x ours %08x)", what, seed, unsigned(i), unsigned(a.size()),
             unsigned(b.size()), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
    if (std::getenv("TS2_SAME_DUMP")) // the words around the difference, both sides (debugging aid)
        for (size_t k = i > 24 ? i - 24 : 0; k < i + 8 && (k < a.size() || k < b.size()); ++k)
            std::fprintf(stderr, "  [%4u] %08x %08x%s\n", unsigned(k), k < a.size() ? a[k] : 0, k < b.size() ? b[k] : 0,
                         k == i ? "  <-" : "");
    return false;
}

} // namespace svcfx
