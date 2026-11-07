#pragma once
#include <cstdint>

// The original game image our code runs alongside: the game module in-game, the mapped copy in the
// differential test harness. Reimplemented code uses it for data it doesn't own yet (globals,
// vtables of game classes), always by RVA (1.18.0.187).
//
// The image is always mapped at kGameBase, the original exe's preferred base, so no relocation is
// needed and its addresses are link-time constants like the original's own. `ts2_rva_0` is an
// absolute symbol (tools/gamebase.asm, linked into ts2core) whose value is kGameBase, so
// GameAddress(0xe61b48) compiles to `offset ts2_rva_0 + 0xe61b48`: one relocated immediate, as the
// original has for `mov [ecx], offset vtable`, with no call and no register clobbered. A dynamic RVA
// is the same symbol plus a register. tools/matchcheck.py reads `ts2_rva_<hex>` + addend as the
// original RVA (docs/archive/proposals/byte-match-verification.md). Host and harness map the image there or
// fail loudly (SetGameBase checks).
constexpr uintptr_t kGameBase = 0xa0000000;

extern "C" char ts2_rva_0[]; // absolute symbol = kGameBase; never dereference it as an object

// Out of line (a call, the old runtime form): GameBase() + rva is the opaque form of GameAddress, for the rare
// site where the constant form ICEs the compiler (thread-safe-statics guards: _Init_thread_header and
// _footer on the same constant-address guard, MSVC 14.40 C1001 in p2/main.c). tools/matchcheck.py still
// reads the call and the add as an RVA (the "gameaddress" equivalence).
uintptr_t GameBase();

// Host and harness call this after mapping the image; it terminates the process if the image isn't at kGameBase.
void SetGameBase(uintptr_t base);

// The address of the game's data/code at `rva`. With a constant RVA (a literal, constexpr or an inlined
// constant) this is a single link-time constant.
template <typename T = void> inline T *GameAddress(uint32_t rva)
{
    return reinterpret_cast<T *>(ts2_rva_0 + rva);
}

template <typename T = void> T *GameAddressOpaque(uint32_t rva)
{
    return reinterpret_cast<T *>(GameBase() + rva);
}
