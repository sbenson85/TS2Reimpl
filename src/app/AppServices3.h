#pragma once
#include <cstdint>

// Steps of AddApplicationServicesHere (0xc267fb) that were blocking it. RVAs are 1.18.0.187.
namespace appservices3 {

// 0xc2b078 (__cdecl, no arguments): the application's ini file may add command-line arguments.
// Asks cTSAppCommonImpl (option 0x6c253729, through the 0x1d0c00 forwarder) for the ini path; when
// that file exists, reads [CommandLine] Append= through a cTSIniFile (no extra prefix) and, if present,
// splits the value with cRZCmdLine and appends each argument to the framework's command line
// (framework slot 21, then its InsertArgument at the current ArgC).
void __cdecl AppendIniCommandLine();

} // namespace appservices3
