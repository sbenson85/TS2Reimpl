#pragma once

// Owned statics (docs/standalone-shape.md, "Owned statics"): a static of the image that our code owns, as one named
// object with a provider per build.
//
//   TS2_OWNED_STATIC(GeneralAllocator, 10cb098);   // declares: extern "C" GeneralAllocator ts2_rva_10cb098;
//
// - Hooked builds (ts2host, the difftest harness, everything linking ts2core): tools/origsyms.py finds the declaration
//   in the sources and defines `_ts2_rva_10cb098` in the generated origsyms.asm as the absolute symbol game base +
//   0x10cb098. Our code and the image share the image's object, exactly as with GameAddress (the same relocated
//   immediate, `offset ts2_rva_10cb098`; tools/matchcheck.py reads it as the RVA and tools/checkgamebase.py checks its
//   value).
// - Image-free programs (ts2core_noimage: origsyms.obj removed): the program defines the object itself, zero-initialised
//   like the image's .bss, in standalone/owned_statics.cpp (`TS2_OWNED_STATIC(T, hex){};`), and runs whatever
//   initialises it in the game (an XC static initialiser) from its own start-up.
//
// The hex is the RVA without 0x (lowercase), one declaration per RVA, in the header of the code that owns the object.
#define TS2_OWNED_STATIC(Type, hex) extern "C" Type ts2_rva_##hex

// A vtable owned as a static (`TS2_OWNED_STATIC(const OwnedVTable, d9b470);`): only its address is used, as the word
// objects store. Hooked builds get the game's table; an image-free program aliases the symbol to our own class's
// vtable (standalone/owned_statics.cpp, `/alternatename`). Never defined, never complete.
struct OwnedVTable;
