#pragma once

#include <cstdint>

// Reimplementations of the unnamed utility functions in the UtilitiesB group.

// 0x18bf80 (__thiscall, no arguments, plain ret): stores -1 in the 32-bit word at `this` and
// returns `this`. Declared __fastcall with a dummy edx so it takes the same registers (ecx) as the
// thiscall original when used as a hook.
void *__fastcall UtilitiesB_SetInvalidIndex(void *self, void *unusedEdx);
