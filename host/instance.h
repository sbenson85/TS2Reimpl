#pragma once
// Instance mode and the Steam stub, from ts2reimpl.ini next to the host (written by
// tools/instances.ps1 for local/game-N; local/game has neither section, so nothing changes there):
//   [Instance] Id=N (0 or absent: off), UserDataDir=<dir\>, AllUsersDataDir=<dir\>
//   [Steam]    Stub=1 (0 or absent: the real steam_api.dll), Language=english
// What instance mode changes and why: src/platform/Instance.h. The stub: docs/contracts/steam.md.
#include <string>

// Reads the settings and logs them (and ts2deploy.txt, the deployed commit). False when the host must
// not start: instance mode without a usable user-data folder (the game would use Documents).
bool LoadInstanceSettings(const std::wstring &dir);

// The replacement for one of the image's imports in instance mode or with the Steam stub, or null.
void *InstanceImport(const char *dll, const char *fn);

// Points the host's own CreateMutexA import at the instance-aware version (our cRZMutex calls it).
void PatchHostImportsForInstance();
