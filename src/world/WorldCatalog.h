#pragma once
#include <cstdint>

// The catalog accessors of nTSWorld's helper module (lane RY, 0x7ef640-0x7efa00): one entry of the game's catalog (the
// object cITSGlobals::GetCatalog answers, slot 19 of which finds an entry by id) and a flag or number from it. All are
// __cdecl with one argument, a pointer to the 32-bit id; a missing entry gives the function's default. RVAs 1.18.0.187.
//
// The entry's slots (offsets in the vtable): 0x60 kind (default -1), 0x64, 0x68, 0x6c, 0x70, 0x74, 0xa0 flags (default false),
// 0x80 (default 0), 0x8c, 0x90, 0x94, 0x98 flags (default true). Their meaning is not known yet; the names say which
// slot they read. The flag slots return a byte (the original tail-jumps into them, so the caller sees their al).
namespace worldcatalog {

// 0x7ef640: the entry for an id: GetCachedSingleton()'s slot 16 object's slot 19 (id). Null when there is none.
void *__cdecl Entry(const uint32_t *id);

// Flags from an entry slot, default true when the entry is missing.
uint8_t __cdecl FlagSlot94OrTrue(const uint32_t *id); // 0x7ef660
uint8_t __cdecl FlagSlot98OrTrue(const uint32_t *id); // 0x7ef680
uint8_t __cdecl FlagSlot8cOrTrue(const uint32_t *id); // 0x7ef6a0
uint8_t __cdecl FlagSlot90OrTrue(const uint32_t *id); // 0x7ef6c0

// 0x7ef6e0: the entry exists.
bool __cdecl Exists(const uint32_t *id);
// 0x7ef700: the entry exists, its kind is 1 or 3 and its slot 0x94 is true.
bool __cdecl IsKind1Or3AndSlot94(const uint32_t *id);
// 0x7ef760: kind is 1 or 3 (the kind slot is asked twice when it is not 1).
bool __cdecl IsKind1Or3(const uint32_t *id);
// 0x7ef790 / 0x7ef7b0: kind is 2 / kind is 3.
bool __cdecl IsKind2(const uint32_t *id);
bool __cdecl IsKind3(const uint32_t *id);
// 0x7ef7d0 / 0x7ef800: the id itself is 0x1d / 0x12d (no catalog needed).
bool __cdecl IdIs1d(const uint32_t *id);
bool __cdecl IdIs12d(const uint32_t *id);
// 0x7ef810: kind is 0 or 3 (the kind slot is asked twice when it is not 0).
bool __cdecl IsKind0Or3(const uint32_t *id);

// Flags from an entry slot, default false when the entry is missing.
uint8_t __cdecl FlagSlotA0(const uint32_t *id);   // 0x7ef7e0
uint8_t __cdecl FlagSlot70(const uint32_t *id);   // 0x7ef840
uint8_t __cdecl FlagSlot74(const uint32_t *id);   // 0x7ef860
uint8_t __cdecl FlagSlot68(const uint32_t *id);   // 0x7ef920
uint8_t __cdecl FlagSlot6c(const uint32_t *id);   // 0x7ef940
uint8_t __cdecl FlagSlot64(const uint32_t *id);   // 0x7ef990

// 0x7ef9b0: entry slot 0x80 (a number; 0 when the entry is missing) and its tests: 0x7ef8a0 / 0x7ef8c0 / 0x7ef8e0 are
// "equals 1 / 4 / 2".
int32_t __cdecl Slot80(const uint32_t *id);
bool __cdecl Slot80Is1(const uint32_t *id);
bool __cdecl Slot80Is4(const uint32_t *id);
bool __cdecl Slot80Is2(const uint32_t *id);
// 0x7ef9d0: entry slot 0x60, the kind (-1 when the entry is missing).
int32_t __cdecl Kind(const uint32_t *id);

} // namespace worldcatalog
