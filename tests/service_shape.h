#pragma once
// Helpers for lane KD's service tests (stage A, docs/standalone-shape.md "Tests"): calling a vtable slot of either side's
// object by its stack-word count, and comparing two objects word by word with the vtable words (and other pointers that
// differ by design, e.g. each side's own fake) mapped to codes first. Used by tests/rz_callback_system_service.cpp,
// tests/gz_timer_service.cpp, tests/gz_registry.cpp, tests/services_b_clipboard.cpp and the other KD service tests.
#include "game/game.h"
#include "rz/RZSystemServiceBase.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace svc {

inline uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The vtable word of the face at `offset`.
inline void **VTableAt(const void *object, uint32_t offset)
{
    return *reinterpret_cast<void **const *>(static_cast<const uint8_t *>(object) + offset);
}

// Calls `fn` (a __thiscall slot body popping `words` stack words) on `self` with the first `words` of `args`; returns eax.
inline uint32_t CallWords(const void *fn, void *self, unsigned words, const uint32_t *args)
{
    using F0 = uint32_t(__thiscall *)(void *);
    using F1 = uint32_t(__thiscall *)(void *, uint32_t);
    using F2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    using F3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    using F4 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
    using F5 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    void *f = const_cast<void *>(fn);
    switch (words) {
    case 0: return reinterpret_cast<F0>(f)(self);
    case 1: return reinterpret_cast<F1>(f)(self, args[0]);
    case 2: return reinterpret_cast<F2>(f)(self, args[0], args[1]);
    case 3: return reinterpret_cast<F3>(f)(self, args[0], args[1], args[2]);
    case 4: return reinterpret_cast<F4>(f)(self, args[0], args[1], args[2], args[3]);
    case 5: return reinterpret_cast<F5>(f)(self, args[0], args[1], args[2], args[3], args[4]);
    default: std::fprintf(stderr, "svc::CallWords: %u words not supported\n", words); std::abort();
    }
}

// What a slot returns, for the comparison: nothing (void: eax is garbage), the low byte (bool / raw byte in al), a dword,
// or a pointer compared through the code map.
enum Ret : uint8_t { kVoid, kByte, kWord, kPtr };

inline uint32_t Masked(Ret ret, uint32_t eax)
{
    switch (ret) {
    case kVoid: return 0;
    case kByte: return eax & 0xff;
    default: return eax;
    }
}

// Words that differ by design between the sides, each mapped to a code: the game's and our vtable of each face, the bases'
// vtables a destructor leaves, each side's own fakes. A word found in the map compares as its code.
struct CodeMap {
    std::vector<std::pair<uint32_t, uint32_t>> pairs; // word -> code
    void Add(const void *word, uint32_t code) { pairs.emplace_back(P(word), code); }
    void AddRva(uint32_t rva, uint32_t code) { pairs.emplace_back(P(GameAddress<void>(rva)), code); }
    uint32_t Code(uint32_t word) const
    {
        for (const auto &p : pairs)
            if (p.first == word) return 0xc0de0000u | p.second;
        return word;
    }
};

// The object's dwords, each through the side's code map.
inline std::vector<uint32_t> Words(const void *object, uint32_t size, const CodeMap &map)
{
    std::vector<uint32_t> out(size / 4);
    for (uint32_t i = 0; i < size / 4; ++i) out[i] = map.Code(reinterpret_cast<const uint32_t *>(object)[i]);
    return out;
}

// The first differing word, as "+off a/b"; empty when equal.
inline std::string Diff(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b)
{
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i] != b[i]) {
            char text[64];
            std::snprintf(text, sizeof(text), "+0x%zx %08x/%08x", i * 4, a[i], b[i]);
            return text;
        }
    }
    return a.size() == b.size() ? std::string() : std::string("size");
}

// The vtables a destroyed service's rz::cRZSystemService part is left with, on both sides, mapped to codes base + 0..2:
// the cIGZSystemService face's and cRZUnknownMT face's vtables of cRZSystemService (the game's 0xda3058 / 0xda3090, ours
// from an object of our class), and cRZUnknownMT's own (0xda2fec; ours as our destructor leaves it).
inline void AddServiceBaseCodes(CodeMap &game, CodeMap &ours, uint32_t base)
{
    static uint32_t vt[3] = {};
    if (!vt[0]) {
        alignas(8) static uint8_t buffer[sizeof(rz::cRZSystemService)];
        auto *service = ::new (buffer) rz::cRZSystemService(0, 0);
        vt[0] = reinterpret_cast<uint32_t *>(buffer)[0];
        vt[1] = reinterpret_cast<uint32_t *>(buffer)[1];
        service->~cRZSystemService();
        vt[2] = reinterpret_cast<uint32_t *>(buffer)[1];
    }
    game.AddRva(0xda3058, base);
    game.AddRva(0xda3090, base + 1);
    game.AddRva(0xda2fec, base + 2);
    for (uint32_t i = 0; i < 3; ++i) ours.pairs.emplace_back(vt[i], base + i);
}

} // namespace svc
