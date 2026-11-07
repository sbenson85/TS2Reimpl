#pragma once
#include "game/game.h"

#include <cstdint>

// A Win32 import our code calls through the game's import address table (notes/decisions.md 2026-10-22, "Win32 calls
// in replaced classes"):
//
//   ImportFunction<BOOL(WINAPI *)(int), 0xd9a4b0>()(show) // ShowCursor, the slot the original calls through
//
// The slot is the one the original's own call reads (`call dword ptr [0xd9a4b0]`; find it in the original's call site).
// In game it holds the loader-resolved import, so ours makes the same call. In the differential test harness every
// import slot is a trap, and a test points the slot at a fake (tests/app_fixture.h, IatPatch): both sides then call the
// fake, and nothing real (the cursor, the message queue) is touched. With a constant RVA the call compiles to the
// original's form, `call dword ptr [game base + rva]` (tools/matchcheck.py reads the ts2_rva symbol as that slot).
//
// The one place to switch to direct linking when the standalone build no longer has the game's import table.
template <typename Fn, uint32_t IatRva> __forceinline Fn ImportFunction()
{
    static_assert(IatRva >= 0x1000, "ImportFunction needs the RVA of an import address table slot");
    return *GameAddress<Fn>(IatRva);
}
