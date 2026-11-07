#pragma once
#include <cstdint>

// Helpers of the person's FindBestAction machinery (opcode 0x03; lane AF) that are free functions or belong to no class of
// their own. RVAs are 1.18.0.187.

namespace ifaces {
class cTSInteraction;
class cTSObject;
} // namespace ifaces

// 0x4529b0 (__cdecl, one argument) and 0x452690 (__cdecl, none): the byte at game global 0x10da96c that says an action string was
// made (MakeActionString, opcode 0x32) since the check of the current interaction began. The setter stores the low byte of its
// argument; the getter returns the byte.
void __cdecl SetActionStringShown(uint32_t shown);
uint8_t __cdecl WasActionStringShown();

// 0x516e00 (__stdcall, two stack arguments): whether the check tree of `interaction` has any of the flag bits in `mask` (low
// byte). Trees whose header flag bit 0 is clear are prepared for their owner first (tree slot 34), and the flags read afterwards
// are remembered per (tree id, tree identity) in a map (game global 0x10dac2c); trees with bit 0 set use their flags as they
// are. False when the owner has no such tree.
bool __stdcall InteractionTreeHasFlags(ifaces::cTSInteraction *interaction, uint32_t mask);

// 0x512860 (__cdecl, one argument, float result in st0): a score factor for interactions on `stackObject`. 1.0 when game option
// bit 0x80 is set (0xb39490); otherwise, when the object's GUID is known to the cached service (global 0x10cc45c) and the
// current selection (0x10cc288 slot 33) exists, the entry of a 4 x 4 table of floats (game global 0x10dac68) chosen by the
// selection's two levels. The table starts as all ones and, on the first call, takes its inner 3 x 3 from BCON 0x141 entries
// 0x41..0x49 divided by 100. The levels index the table unchecked.
float GetStackObjectScoreFactor(ifaces::cTSObject *stackObject);
