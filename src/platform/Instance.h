#pragma once
// Parallel game instances (tools/instances.ps1, tools/smoke.py): what differs between instances, as
// pure functions the host (host/instance.cpp) applies and the tests check. Instance 0 is "no
// instance": the game copy in local/game, exactly the original behaviour.
//
// Shared state between two games on one machine, and how an instance N > 0 separates it:
//   - the single-instance mutexes "Global\The Sims 2" and "Global\The Sims 2 Body Shop" (made by the
//     application constructor through cRZMutex, 0xc6c85e): renamed per instance (MutexNameForInstance);
//   - the user-data folder (Documents\EA Games\The Sims 2 Legacy) and the all-users data folder
//     (Public Documents\EA Games\...): the game's own switches -activeUserDataDir:<dir> and
//     -activeAllUsersDataDir:<dir> (read by cTSAppFW face +0x44 slot 15, 0xc2a7de) are added to the
//     command line the game sees (CommandLineForInstance);
//   - the Steam client: the Steam stub (src/platform/SteamStub.h, docs/contracts/steam.md);
//   - files the host writes next to itself (ts2host.log, ts2control.txt, simtrace.txt, ts2reimpl.ini):
//     each instance has its own TSBin folder.
#include <string>

namespace instance {

// The name a mutex gets in instance `id`. Only the game's two single-instance names (and any other
// name starting "Global\The Sims 2") are changed, by appending " [instance <id>]"; every other name,
// a null name and instance 0 are left alone (`renamed` untouched, false returned).
bool MutexNameForInstance(const char *name, int id, std::string &renamed);

// Whether a path can be passed as a switch value: not empty, absolute (drive letter and backslash),
// no spaces, quotes or tabs (the game's command-line parser splits at spaces), ending in a backslash.
bool IsUsableDataDir(const std::wstring &dir);

// The command line the game sees in instance mode: `original`, plus -activeUserDataDir:<userData>
// and -activeAllUsersDataDir:<allUsers> unless the command line already has that switch (any case).
// An empty directory adds nothing.
std::wstring CommandLineForInstance(const std::wstring &original, const std::wstring &userData, const std::wstring &allUsers);

} // namespace instance
