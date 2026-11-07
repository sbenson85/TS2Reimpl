# Contract: the Steam stub (steam_api.dll)

**Contract: stubbing Steam approved by the user (2026-10-17); this text awaits the coordinator's review.**
Switch: `ts2reimpl.ini` `[Steam] Stub=1` (default 0: the real `steam_api.dll` as before; instances made by
`tools/instances.ps1` get 1 unless `-RealSteam`), `Language=<Steam language name>` (default `english`).
Source: `src/platform/SteamStub.{h,cpp}`, resolved by the host in `host/instance.cpp`
(`InstanceImport`). Tests: `tests/platform_steam_instance.cpp` (`Steam_*`).

What is replaced is the imported DLL, not image code: the game's own Steam glue (SteamInit 0xc2a358,
the context initialisers 0xc2a36e/0xc2a389, InitLanguage 0xc29a0b, WinMain, the callback code) runs
unchanged against the stub, original or replacement alike. No `db/status` entries; it removes the
image's dependency on `steam_api.dll` and the running Steam client.

## 1. Interface

The image imports eight `steam_api.dll` functions (import slots 0xd9a6f8..0xd9a714), all `__cdecl`:

| Import (slot) | Signature | Image callers |
|---|---|---|
| `SteamInternal_SteamAPI_Init` (0xd9a70c) | `int (const char *versions, char *errMsg)`, 0 = OK | SteamInit 0xc2a358 |
| `SteamInternal_ContextInit` (0xd9a700) | `void **(void *context)` | WinMain (context 0x1081a7c), InitLanguage (0x1081a88) |
| `SteamInternal_FindOrCreateUserInterface` (0xd9a704) | `void *(int hUser, const char *version)` | context initialisers 0xc2a36e (`STEAMAPPS_INTERFACE_VERSION008`), 0xc2a389 (`SteamUser023`) |
| `SteamAPI_GetHSteamUser` (0xd9a714) | `int ()` | the same two initialisers |
| `SteamAPI_RegisterCallback` (0xd9a6fc) | `void (CCallbackBase *, int id)` | cTSSimulator ctor 0x480210 (GameOverlayActivated, id 331) |
| `SteamAPI_UnregisterCallback` (0xd9a710) | `void (CCallbackBase *)` | callback destructor 0x4804d0, cTSSimulator slot 4 0x480510 |
| `SteamAPI_RunCallbacks` (0xd9a6f8) | `void ()` | cGZFramework::OnIdle 0x142dbb, every frame |
| `SteamAPI_Shutdown` (0xd9a708) | `void ()` | WinMain, after the application returns |

A context (Steamworks' accessor static) is `{init function, counter, interface pointer}`; ContextInit
calls `init(&context[2])` when the counter is stale and returns `&context[2]`. Interface objects are
C++ objects whose first word is a vtable; the image calls exactly two slots, found by searching every
use of the context results: ISteamUser slot 2 `GetSteamID` (thiscall, CSteamID returned through a
hidden pointer, `ret 4`) and ISteamApps slot 4 `GetCurrentGameLanguage` (thiscall, returns `const char *`).
`CCallbackBase` is `{vtable, uint8 flags (+4), int id (+8)}`; bit 0 of flags is "registered".

## 2. What the real DLL does

Connects to the running Steam client (fails without it: the game then shows "Steam Initialization
Failed." and exits), checks ownership of app 3314070, injects the overlay, returns the logged-in user's
SteamID and the language the user chose for the game in Steam, dispatches callbacks (overlay
activation) on RunCallbacks, and disconnects on Shutdown.

## 3. What the game reads back

- Init's result: OK or exit (SteamInit 0xc2a358).
- The SteamID: only as the argument of the launch analytics call (0xc2df67), which is itself
  `replaced` by a no-op (docs/contracts/analytics.md). Nothing else reads it.
- The language name: InitLanguage maps it through its table of 21 names (english 1, french 3, ...,
  brazilian 35; src/app/AppInit.h) to the language id it gives the language service, -1 for a name not
  in the table. Only used when the command line has no `-l` switch. This is the one behaviour that
  matters: it selects the game's text language.
- The callback flags: the destructor 0x4804d0 unregisters only a callback flagged as registered.
- The overlay callback (cTSSimulator, id 331) only fires from RunCallbacks with the overlay present.

## 4. Contract

### Kept

| Kept | How |
|---|---|
| Init succeeds | returns 0 and clears the error message when one is passed |
| ContextInit's protocol | the game's own initialiser runs once per context (counter 1), the result points at the stored interface |
| The interfaces the image asks for | `SteamUser023` and `STEAMAPPS_INTERFACE_VERSION008` by exact name; any other version gets null (logged) |
| A SteamID | fixed: account 1, desktop instance, individual, public universe (0x0110000100000001) |
| The language | the `Language=` string, passed on unchanged; the game's mapping is untouched, so a listed name gives the same language id as with Steam, an unlisted one -1 (logged at start) |
| Callback bookkeeping | Register sets flag bit 0 and the id, Unregister clears bit 0, as the DLL does |

### Dropped, with reasons

| Dropped | Reason |
|---|---|
| The Steam client connection, ownership check, overlay | The point of the stub: runs without Steam, several at once (smoke batches). |
| The user's real SteamID | Only reached the analytics call, which sends nothing (contract `analytics`). |
| The overlay-activated callback | Without an overlay it never fires; the game pauses on it only when the overlay opens. |
| RunCallbacks' dispatch, Shutdown's disconnect | Nothing registered can fire; nothing to disconnect. |

### Judgement calls (for review)

1. The language comes from our ini, not from Steam's per-game setting; default English, the game's
   own default. Unknown names are passed on rather than corrected, so the game behaves as it would
   with that Steam language.
2. Interface slots the image never calls answer 0 and log "unexpected call" instead of being
   implemented from Steamworks headers (they would be dead code). ISteamUser 0/1 and ISteamApps 0-3, 5
   are implemented trivially (handle 1, logged on, subscribed, not low-violence, not a cybercafe, not
   VAC-banned, available languages = the language).

## 5. Tests

`tests/platform_steam_instance.cpp`, with the image's eight import slots pointed at the stub (as the host
does) and the game's two static contexts saved and restored:

- `Steam_StubInit`: the original SteamInit (0xc2a358) and ours return al = 1; Init clears the message.
- `Steam_StubUserContext`: WinMain's path: ContextInit on 0x1081a7c runs the game's initialiser
  0xc2a389, slot 2 writes the fixed SteamID through the hidden pointer and returns it; a second
  ContextInit returns the same object; the context holds counter and interface.
- `Steam_StubAppsLanguage`: InitLanguage's path on 0x1081a88: default "english" (game id 1); each of the
  game's 21 names comes back unchanged and maps to a known id; an unlisted name is passed on and maps
  to -1; an unknown interface version gets null.
- `Steam_StubCallbacks`: Register flags and records id 331; the game's callback destructor 0x4804d0
  then unregisters through the stub (flag cleared), nothing written past the object.

Mutation check (tools/mutate.py, filter `Steam_`): Init failing, SteamID halves swapped, ContextInit not
calling the initialiser, Register not flagging, the language ignored, Unregister not clearing, any
version answered: all killed.

In game (coordinator): with `Stub=1` the host log has "steam: stub (language "english")", no
`dll loaded: steam_api.dll` under TS2HOST_TRACEDLLS, and the game reaches the neighbourhood in English.
