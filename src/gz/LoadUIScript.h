#pragma once
// LoadUIScript and its window helpers (lane MD, foundations F5b): src/gz/LoadUIScript.cpp. Free __cdecl functions of the
// game's (about 150 UI classes load their layouts through LoadUIScript), built for speed as the game's.
//
// A UI script's windows carry their conditions as properties (cIGZWin::GetProperty, variants holding text):
//   0x30000ef  true: the window is announced (message 0xe9dd622 posted with the window)
//   0x30000f0  a condition for flag 1 (visible), 0x30000f1 one for flag 2 (enabled)
//   0x30000f2  false: the window is held back (handed to slot 21 of its parent after the others are prepared)
//   0x30000f3  any text: sent as message 0xf70f296 with the window
// A condition is terms "name=value" or "name!=value" separated by spaces, joined by "and" / "or" (left to right, no
// precedence). A name the settings property set has (the property manager service 0x0ac1887e) is compared with the
// value parsed for the setting's type; any other name is asked of the message server (message 0x8e9dce8e, the name and
// value as strings; the answer is the message's first data word).
#include <cstdint>

class cIGZUnknown;
class cIGZScriptableControlSet;

// 0xb40d20: the window's condition property `propertyId` evaluated into *out (each term combined into it: the first
// assigns, "and" / "or" combine). False (out as it got) when the window or its property is missing, there is no console
// or settings service, a setting has a type it cannot compare, a joiner is neither "and" nor "or", or the message server
// refuses a term's message; true otherwise (an empty condition leaves *out alone).
bool __cdecl EvaluateWindowCondition(uint32_t propertyId, cIGZUnknown *window, bool *out);

// 0xb42250: the window's conditions 0x30000f0 / 0x30000f1 applied to flags 1 / 2 and its 0x30000f3 text sent; true when
// any of the three applied. False for a null window.
bool __cdecl ApplyScriptConditions(cIGZUnknown *window);

// 0xb429b0: the main window's width and height into the script screen size (game words 0x10fb340 / 0x10fb344), 800 x 600
// when either is below 1024 x 768. Nothing without the window manager or its main window.
void __cdecl UpdateScriptScreenSize();

// 0xb41b50: the UI script `instance` (group 0xa99d8a11, or the screen size's group when `screenSized` and that resource
// exists) compiled by the UI script service, after a new script object for `scriptArg` was pushed. Every window of the
// resulting control set is prepared (above); the first gets `winProc` when given. The set goes to *out when `out` is
// given, else its windows are left to the UI (SetOwnsWindows(false)) and it is released. False without the service, its
// legacy face, or a compiled set.
bool __cdecl LoadUIScript(uint32_t instance, uint32_t scriptArg, cIGZUnknown *winProc, cIGZScriptableControlSet **out,
                          bool screenSized);
