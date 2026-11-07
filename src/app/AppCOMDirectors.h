#pragma once
#include <cstdint>

// The application's COM director registration (docs/startup-map.md 4.4). RVAs are 1.18.0.187.
namespace appcom {

// 0x140f40 (__cdecl, no arguments, EH frame): when the `nosound` switch is absent from the command
// line (the framework's command line object, framework slot 21; its slot 9 IsSwitchPresent with a
// cRZString "nosound", destroyed right after), adds the audio director (GetAudioDirector, 0x20efaf)
// to the root director and starts it: slot 4 (OnStart) with the root director's framework object.
// An exception from the switch test destroys the string.
void AddAudioDirector();

// cTSAppFW::AddCOMDirectorsHere (0xc26bf5) is ts::cTSAppFW::AddCOMDirectorsHere (ts/cTSAppFW.size.cpp, lane FG); it ends with
// AddAudioDirector.

} // namespace appcom
