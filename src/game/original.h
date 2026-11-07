#pragma once
#include "game/game.h"

#include <cstdint>

// An original function a replacement still calls because nobody has replaced it yet (CLAUDE.md,
// "Original callees"). The only sanctioned way to call game code from src/:
//
//   OriginalFunction<int(__thiscall *)(void *, int), 0x9ae5c0>()(this, 3)
//
// The RVA must be a hex literal at the call site: tools/origcalls.py finds every use, checks the
// callee (a function start, not library code, not the caller itself, not already replaced), and
// requires the calling function's status entry to declare it in "original_calls". tools/status.py
// counts a verified function as standalone only when nothing it reaches calls original code.
// In-game, a callee that gets replaced later is reached through its hook; in the harness it stays
// the original on both sides of a differential test.
//
// The call compiles to `call rel32` to a link-time symbol, as the original binary's own calls do:
// OriginalEntry<RVA> is a function template that is declared here and never defined; its decorated
// name (??$OriginalEntry@$0<hex digits>@@@YAXXZ) is defined by the build as an absolute symbol
// (tools/origsyms.py writes the assembler file at build time from the OriginalFunction uses it finds;
// tools/checkgamebase.py checks every address in the linker map). The cast gives the pointer the calling
// convention of Fn and the optimiser folds the call into a direct one.
//
// Two consequences. The linker resolves a rel32 reference to an absolute symbol as image-relative, so the
// symbol is valued (game base + RVA) - exe base and is right for calls and tail jumps only: taking the
// address of the function would be off by the image base. The optimised build folds every use into a call
// (checkgamebase.py fails the build if an object references the symbol any other way); an unoptimised build
// (/Od: TS2_ESPCHECK, Debug) doesn't, so it takes the address from the game base as GameAddress does. And
// the executable must sit at its preferred base: ts2host and difftest link with /FIXED /BASE.
template <uint32_t Rva> void OriginalEntry(); // never defined: the build supplies the symbol

// The address of an original function as a value (to register it with atexit, store it in a table), where there
// is no call to make direct. Same rules as OriginalFunction (a hex literal, declared in "original_calls").
template <typename Fn, uint32_t Rva> Fn OriginalAddress()
{
    static_assert(Rva >= 0x1000, "OriginalAddress needs a function RVA");
    return reinterpret_cast<Fn>(GameAddress<void>(Rva));
}

template <typename Fn, uint32_t Rva> __forceinline Fn OriginalFunction()
{
    static_assert(Rva >= 0x1000, "OriginalFunction needs a function RVA (tools/origsyms.py names the symbol by its hex digits)");
#if defined(TS2_ESPCHECK) || defined(_DEBUG)
    return reinterpret_cast<Fn>(GameAddress<void>(Rva));
#else
    return reinterpret_cast<Fn>(&OriginalEntry<Rva>);
#endif
}
