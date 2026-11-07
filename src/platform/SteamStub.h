#pragma once
// The Steam stub: our stand-in for steam_api.dll, so the game starts without the Steam client
// (contract: docs/contracts/steam.md; `replaced` infrastructure, Steam/launcher glue). The host
// (host/instance.cpp) resolves the image's eight steam_api.dll imports to these functions when
// ts2reimpl.ini has [Steam] Stub=1; otherwise the real DLL is loaded as before.
//
// What the image uses (every reference to the eight import slots, 0xd9a6f8..0xd9a714):
//   SteamInternal_SteamAPI_Init   SteamInit 0xc2a358 (WinMain): 0 = OK, else the game shows
//                                 "Steam Initialization Failed." and exits
//   SteamInternal_ContextInit     WinMain (user context 0x1081a7c, then ISteamUser slot 2,
//                                 GetSteamID, for the analytics call) and InitLanguage 0xc29a0b
//                                 (apps context 0x1081a88, then ISteamApps slot 4,
//                                 GetCurrentGameLanguage, mapped by the game to its language id)
//   SteamAPI_GetHSteamUser,       the context initialisers 0xc2a36e (STEAMAPPS_INTERFACE_VERSION008)
//   SteamInternal_FindOrCreate-   and 0xc2a389 (SteamUser023)
//     UserInterface
//   SteamAPI_RegisterCallback /   cTSSimulator's GameOverlayActivated callback (id 331), constructor
//     UnregisterCallback          0x480210, callback destructor 0x4804d0, cTSSimulator slot 4 0x480510
//   SteamAPI_RunCallbacks         cGZFramework::OnIdle 0x142dbb, every frame
//   SteamAPI_Shutdown             WinMain, after the application returns
#include <cstdint>
#include <string>

namespace steamstub {

// The fixed SteamID: account 1, desktop instance (1), individual account type (1), public universe (1).
constexpr uint32_t kSteamIdLow = 0x00000001, kSteamIdHigh = 0x01100001;
constexpr int32_t kSteamUser = 1; // the HSteamUser handle

// The language GetCurrentGameLanguage returns (Steam's API language names: "english", "german",
// "schinese", ...). Default "english". Any string is passed on as the Steam client would pass it;
// the game maps it to its language id (InitLanguage 0xc29a0b), -1 for a name it does not list.
void SetLanguage(const std::string &name);
const std::string &Language();
// Whether the game's language table (InitLanguage) lists the name.
bool IsGameLanguage(const std::string &name);

// Optional log for unexpected calls (an interface slot or version the image is not known to use).
void SetLogger(void (*log)(const char *line));

// The stub for a steam_api.dll export by name; null for a name the image is not known to import.
void *Resolve(const char *name);

// The exports, as the image calls them (all __cdecl).
int32_t __cdecl SteamAPIInit(const char *interfaceVersions, char *errorMessage);
void **__cdecl ContextInit(void *contextData);
void *__cdecl FindOrCreateUserInterface(int32_t user, const char *version);
int32_t __cdecl GetHSteamUser();
void __cdecl RunCallbacks();
void __cdecl RegisterCallback(void *callback, int32_t id);
void __cdecl UnregisterCallback(void *callback);
void __cdecl Shutdown();

// Steamworks' CCallbackBase, the part the DLL touches: the registered flag (bit 0) and the id.
struct CallbackBase {
    void *vtable;
    uint8_t flags;   // +4
    int32_t id;      // +8
};

} // namespace steamstub
