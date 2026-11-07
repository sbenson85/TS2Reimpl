#pragma once
// Wide recording fakes for the cTSSGSystem tests (lane JD): like svcfx::Obj (tests/ts_services_fixture.h), but with 192
// slots, float-returning slots (the result in st0, as the game's float getters), and a virtual-base table at +4 whose
// entries reach other objects (references the code counts through `obj + 4 + vbtable[entry]`).
//
// Every call appends {evCall, id, slot, argument count, arguments...} to svcfx::gEv, each by its declared kind (as svcfx's
// fakes: 'p' pointer, 'd' data, '-' undeclared, which fails unless zero; the text and low masks come first). Slots 1 / 2
// count references (refs). The array of wide fakes is registered as a range (tag kWideTag), so pointers to them
// normalise to tag | offset.
#include "ts_services_fixture.h"

#include <cstdint>
#include <cstring>
#include <source_location>
#include <string>
#include <utility>

namespace sgwide {

using svcfx::Blob;
using svcfx::Ev;
using svcfx::Norm;
using svcfx::P;

constexpr unsigned kSlots = 192;
constexpr unsigned kWide = 64;
constexpr uint32_t kWideTag = 0x77000000;

struct Wide;
using Custom = uint32_t (*)(Wide *self, uint32_t *args);

struct Wide {
    void **vt;
    int32_t *vbt;          // +4: the virtual-base table pointer
    int32_t vbtable[4];    // entry 1 / 2: offsets from &vbt to the virtual bases
    uint32_t id;
    int32_t refs;
    uint32_t value[8];
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    uint8_t text[kSlots];  // bit i: argument i is a C string (logged as its text)
    uint8_t low[kSlots];   // bit i: only argument i's low byte is logged
    void *table[kSlots];

    void Reset(uint32_t newId);
    // `args` arguments of undeclared kinds, or (a string) one kind per argument: 'p' pointer, 'd' data, '-' undeclared.
    void Set(unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr,
             std::source_location where = std::source_location::current());
    void Set(unsigned slot, unsigned args, const void *result, std::source_location where = std::source_location::current())
    {
        Set(slot, args, P(result), nullptr, where);
    }
    template <size_t N>
    void Set(unsigned slot, const char (&kinds)[N], uint32_t result = 0, Custom fn = nullptr,
             std::source_location where = std::source_location::current())
    {
        Set(slot, unsigned(N - 1), result, fn, where), Declare(slot, kinds);
    }
    template <size_t N>
    void Set(unsigned slot, const char (&kinds)[N], const void *result, std::source_location where = std::source_location::current())
    {
        Set(slot, unsigned(N - 1), P(result), nullptr, where), Declare(slot, kinds);
    }
    // A slot that returns `result` in st0 (`fn`, when given, computes its bits).
    void SetFloat(unsigned slot, unsigned args, float result, Custom fn = nullptr,
                  std::source_location where = std::source_location::current());
    template <size_t N>
    void SetFloat(unsigned slot, const char (&kinds)[N], float result, Custom fn = nullptr,
                  std::source_location where = std::source_location::current())
    {
        SetFloat(slot, unsigned(N - 1), result, fn, where), Declare(slot, kinds);
    }
    // The kinds of value[0..] (CaptureWide): 'p', 'd' or '-' per word.
    template <size_t N> void Values(const char (&kinds)[N]);
    void Declare(unsigned slot, const char *kinds);
    // Entry `entry` of the virtual-base table reaches `base` (an svcfx fake or another wide fake).
    void Base(int entry, const void *base) { vbtable[entry] = int32_t(P(base) - P(&vbt)); }
};

// Aligned so every fourth fake (sizeof(Wide) is 0xac0) has a zero low byte: a pointer answer then tells a full test
// from a low-byte (al) one.
alignas(256) inline Wide gWide[kWide];
static_assert(sizeof(Wide) % 0x100 == 0xc0);
// The declarations, beside the fakes (not in them: the layout above is load-bearing).
struct Declared {
    char args[kSlots][8];
    char values[8];
    const char *file[kSlots];
    unsigned line[kSlots];
};
inline Declared gDeclared[kWide];
inline Declared &Decl(const Wide *w) { return gDeclared[w - gWide]; }
inline char Kind(char k)
{
    if (k == '-') return 0;
    if (k != 'p' && k != 'd') std::abort(); // a kind string has only 'p', 'd' and '-'
    return k;
}
inline void Wide::Declare(unsigned slot, const char *kinds)
{
    for (unsigned i = 0; i < 8; ++i) Decl(this).args[slot][i] = 0;
    for (unsigned i = 0; kinds[i]; ++i) Decl(this).args[slot][i] = Kind(kinds[i]);
}
template <size_t N> void Wide::Values(const char (&kinds)[N])
{
    static_assert(N - 1 <= 8, "eight value words");
    for (unsigned i = 0; i < 8; ++i) Decl(this).values[i] = i < N - 1 ? Kind(kinds[i]) : 0;
}
inline std::string SlotName(const Wide *self, unsigned slot)
{
    const char *file = Decl(self).file[slot] ? Decl(self).file[slot] : "?";
    for (const char *p = file; *p; ++p)
        if (*p == '\\' || *p == '/') file = p + 1;
    return "fake #" + std::to_string(self->id) + " slot " + std::to_string(slot) + " (Set at " + file + ":" +
           std::to_string(Decl(self).line[slot]) + ")";
}

namespace detail {
inline void *gInt[kSlots][8];
inline void *gFloat[kSlots][8];
inline bool gFilled = false;

inline uint32_t Dispatch(Wide *self, unsigned slot, uint32_t *args, unsigned count)
{
    Ev(svcfx::evCall);
    Ev(self->id);
    Ev(slot);
    Ev(count);
    for (unsigned i = 0; i < count; ++i) {
        if (self->text[slot] & (1u << i)) { // a C string: its text, not its (heap or stack) address
            for (const char *c = reinterpret_cast<const char *>(uintptr_t(args[i])); c && *c; ++c) Ev(uint8_t(*c));
            Ev(0x7e57);
        } else if (self->low[slot] & (1u << i)) { // a bool the caller pushes in a word it only partly wrote
            Ev(args[i] & 0xff);
        } else {
            Ev(svcfx::Word(Decl(self).args[slot][i], args[i],
                           [&] { return SlotName(self, slot) + " argument " + std::to_string(i); },
                           "in the Set: W(...).Set(slot, \"...\") with 'p' (pointer) or 'd' (data) for each of its arguments"));
        }
    }
    if (self->custom[slot]) return self->custom[slot](self, args);
    if (slot == 1) return uint32_t(++self->refs);
    if (slot == 2) return uint32_t(--self->refs);
    return self->ret[slot];
}

template <size_t> using Word = uint32_t;
template <unsigned Slot, class Seq> struct Gen;
template <unsigned Slot, size_t... I> struct Gen<Slot, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(Wide *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        return Dispatch(self, Slot, args, unsigned(sizeof...(I)));
    }
    static float __fastcall FnF(Wide *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        const uint32_t bits = Dispatch(self, Slot, args, unsigned(sizeof...(I)));
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
};
template <unsigned Slot, size_t... N> void FillSlot(std::index_sequence<N...>)
{
    ((gInt[Slot][N] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<N>>::Fn)), ...);
    ((gFloat[Slot][N] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<N>>::FnF)), ...);
}
template <size_t... S> void FillAll(std::index_sequence<S...>) { (FillSlot<unsigned(S)>(std::make_index_sequence<8>()), ...); }
} // namespace detail

inline void Wide::Reset(uint32_t newId)
{
    if (!detail::gFilled) detail::FillAll(std::make_index_sequence<kSlots>()), detail::gFilled = true;
    vt = table;
    vbt = vbtable;
    for (int32_t &e : vbtable) e = 0;
    id = newId;
    refs = 1000;
    for (auto &v : value) v = 0;
    for (unsigned i = 0; i < kSlots; ++i) ret[i] = 0, custom[i] = nullptr, table[i] = nullptr, text[i] = 0, low[i] = 0;
    std::memset(&Decl(this), 0, sizeof(Declared));
    Set(1, 0);
    Set(2, 0);
}

inline void Wide::Set(unsigned slot, unsigned args, uint32_t result, Custom fn, std::source_location where)
{
    table[slot] = detail::gInt[slot][args];
    ret[slot] = result;
    custom[slot] = fn;
    Declare(slot, "");
    Decl(this).file[slot] = where.file_name(), Decl(this).line[slot] = where.line();
}

inline void Wide::SetFloat(unsigned slot, unsigned args, float result, Custom fn, std::source_location where)
{
    table[slot] = detail::gFloat[slot][args];
    std::memcpy(&ret[slot], &result, 4);
    custom[slot] = fn;
    Declare(slot, "");
    Decl(this).file[slot] = where.file_name(), Decl(this).line[slot] = where.line();
}

inline Wide &W(unsigned i) { return gWide[i]; }

// Resets every wide fake (ids 500 + index) and registers their range; call after svcfx::ResetAll.
inline void ResetWide()
{
    for (unsigned i = 0; i < kWide; ++i) gWide[i].Reset(500 + i);
    svcfx::AddRange(gWide, sizeof(gWide), kWideTag);
}

// Every wide fake's reference count and value words.
inline void CaptureWide(Blob &b)
{
    for (const Wide &w : gWide) {
        b.push_back(uint32_t(w.refs));
        for (unsigned i = 0; i < 8; ++i)
            b.push_back(svcfx::Word(Decl(&w).values[i], w.value[i],
                                    [&] { return "wide fake #" + std::to_string(w.id) + " value[" + std::to_string(i) + "]"; },
                                    "with the wide fake's Values(\"...\"): 'p' pointer, 'd' data per value word"));
    }
}

// Writes `value` through the out-pointer argument `args[index]`.
inline void Out(uint32_t *args, unsigned index, uint32_t value) { *reinterpret_cast<uint32_t *>(uintptr_t(args[index])) = value; }

} // namespace sgwide
