# Startup map: from the CRT entry to the main loop

A chart of how the game starts, runs and stops: names, call order, what each step does, and which
services and systems get created where. Facts only (RVAs, sizes, names, call edges, string
references); nothing here is code or disassembly. It is the aim sheet for roadmap stage 4 ("own the
startup path"). Statuses were refreshed by lane EC (2026-10-18): most of the framework, the COM
registry, the directors, the application constructor and its services are verified in the hook shape
(section 6); the class structure, the standalone-shape headers and the remaining work are in
`docs/subsystems/startup.md`.

RVAs are for the unpacked image (`tools/unpack.ps1`); the image base is 0xA0000000, so a pointer
value 0xA0xxxxxx in the code is RVA 0xxxxxx. **Status** column: `V` verified, `R` reimplemented,
`M` mapped, `L` library (our toolchain provides it), `-` nothing yet. A name in *italics* is a
descriptive name I made up (the db has no better one); `FUN_` ones have no name yet.

How this was derived: the call graph in `db/functions.jsonl`, the RTTI vtables in
`db/classes.jsonl`, interface slot names from `db/interface-slots.jsonl`, and reading the
disassembly for the indirect calls (vtable slots) that the db does not list. Where a slot name
comes from the interface table, it is the community header order, checked against what the code
does at the call site. Things I could not settle are in "Open questions" at the end.

## 0. The whole thing in one screen

```
entry 0x8248a3
  __security_init_cookie                                   [L]
  __scrt_common_main_seh 0x824727                          [-, CRT glue that embeds the WinMain call]
    __scrt_initialize_crt, startup lock                    [L]
    _initterm_e(10 pre-C++ initialisers)                   (CRT init, locale, heap, QPC, ...)
    _initterm(800 dynamic initialisers)                    (every global object of the game)
    WinMain 0xc2b87c(hInstance = image base, cmdline)
      crashpad client + Steam init + analytics POST + terminate/purecall handlers
      cRZCmdLine from GetCommandLineW
      static cCTGFramework @0x10fdb50  (ctor 0xc2ccdb)
      static cTSAppFW      @0x10fdcf0  (ctor 0xc26147)
      RunApp 0xc2d23b(framework, app, cmdline)             (optionally inside an SEH guard)
        RunFramework 0xc2d0a8
          install globals g_Framework 0x10c9378, g_App 0x10c9380
          GZFrameWork_Startup 0x144514   (states 0..6)
          GZFrameWork_Run     0x1446ba   (state 7; message pump = framework slot 48)
          GZFrameWork_Shutdown 0x14472b  (states 8..14)
      SteamAPI_Shutdown, return exit code (g_ExitCode 0x10c9384)
    exit(code): _cexit, static destructors (atexit list)
```

## 1. CRT entry down to WinMain

| RVA | Name | St | What it does |
|---|---|---|---|
| 0x8248a3 | `entry` (10 B) | - | `call __security_init_cookie; jmp __scrt_common_main_seh`. The PE entry point. |
| 0x8258a9 | `__security_init_cookie` | L | Seeds the stack cookie at 0x104f540 (and its complement at 0x104f580). |
| 0x824727 | `__scrt_common_main_seh` (about 0x180 B incl. its SEH funclets) | - | The MSVC common main with WinMain inlined at one point. Not in `library-matches` because of that embedded call; its callees all are library. Sequence below. |
| 0x824af8 | `__scrt_initialize_crt(1)` | L | CRT state; failure -> `__scrt_fastfail(7)`. |
| 0x824ac6 / 0x824c4c | acquire / release startup lock | L | Bracket the initialisers. |
| 0x84353f | `_initterm_e` | L | Runs table 0xd9b3ac..0xd9b3d8: **10** pre-C++ initialisers; a non-zero return aborts with exit code 0xff. |
| 0x843514 | `_initterm` | L | Runs table 0xd9a724..0xd9b3a8: **800** C++ dynamic initialisers (section 2). |
| 0x825950 / 0x825956 | tiny table getters | - | Return the dynamic-TLS-dtor / on-exit table slots (0x110a388, 0x110a384); the code calls what they hold if the table is non-writable (`__scrt_is_nonwritable_in_current_image`, L). |
| 0x825a71, 0x8434c1 | `__scrt_get_show_window_mode`, `__get_narrow_winmain_command_line` | L | Arguments for WinMain. |
| **0xc2b87c** | **`WinMain(hInstance=0xA0000000, hPrev=0, cmdline, show)`** | M | Section 3. Returns the exit code. A candidate replacement exists (`winmaininfra::WinMainBody`), blocked by the CRT seam: the game's CRT still owns atexit, the thread-safe-statics epoch and `exit`. |
| 0x825aa2, 0x82f6bf, 0x82f674 (`_cexit`), 0x824c69 | exit path | L | Not a managed app (normal case): `exit(code)` = 0x82f6bf (the db calls it `_loaddll`; the framework's own `AbortiveQuit` logs "Calling exit()" before calling it), which runs the atexit list. The managed branch (`_cexit`, `__scrt_uninitialize_crt`, return) is not taken. The SEH filter is `_seh_filter_exe` (0x842b14, L); `_Exit` 0x82f683 is on the exception path. |

Static destructors run from `_cexit`/`exit` through the atexit list the initialisers filled (about
610 of the 800 initialisers register a destructor with `atexit` 0x824cbe, L).

### The 10 `_initterm_e` entries (XI)

All CRT or CRT-adjacent; none is game logic except the 9th:

| # | RVA | Size | What |
|---|---|---|---|
| 1 | 0x824662 | 163 | `pre_c_initialization`: onexit tables, `_RTC_Initialize`, `_set_fmode`, FP precision, `_configthreadlocale`, user matherr, etc. |
| 2 | 0x82470d | 8 | `__scrt_initialize_default_local_stdio_options` (L) |
| 3 | 0x82f7fd | 53 | fills a small CRT state block at 0x10dfc98.. |
| 4 | 0x838d0d | 106 | `_calloc_base` based table sizing (stdio/fd table, default 0x200) |
| 5 | 0x83a409 | 101 | `__acrt_initialize_clock` (L): QueryPerformanceCounter/Frequency |
| 6 | 0x8476f9 | 16 | caches `IsProcessorFeaturePresent(10)` at 0x110a38c |
| 7 | 0x856ce0 | 15 | `__acrt_initialize_multibyte` (L) |
| 8 | 0x85381f | 22 | default value for a CRT size variable (0x4000) |
| 9 | 0xc4984c | 69 | resolves `GetCurrentPackageId`, `GetSystemTimePreciseAsFileTime`, `GetTempPath2W` from kernel32 into 0x10fe570..0x10fe578 |
| 10 | 0xd17f1a | 31 | sets the local printf/scanf option flags (same pair of getters as the CRT's `__scrt_initialize_default_local_stdio_options`) |

## 2. The 800 dynamic initialisers (XC table)

Entry 1 is CRT (0x824715: `_set_new_mode`, onexit setup). Entries 2..800 are the game's own and sit
in one compiler-generated block, **0x136e60..0x1409ff** (798 of the 800). Only 23 of the 800 have a
function record in `db/functions.jsonl` (Ghidra did not create the small stubs), so
`candidates.py`/reachability cannot see them today (see "Tool and brief notes"). What they build,
by shape (my classification of the initialiser bodies; counts are initialisers):

| Count | Initialiser shape | What it is | Status of the called code |
|---|---|---|---|
| 56 | a pixel format constructor | The `nGZGraphic4::cPixelFormat` objects: 20x `cPixelFormat::Construct` (0x15b840), 12x ARGB (0x15bf40), 7x DepthStencil (0x15cc10), 6x ABGRFloat (0x15ba30), 4x AlphaLuminance (0x15c5b0), 2x YUV (0x15d230), plus 5 DXT formats whose name is built with `cRZString::Sprintf` (the 392-byte initialisers at 0x1372d0, 0x137460, 0x1375f0, 0x137780, 0x137910). Objects at 0x10c9790..0x10c9fd0. | constructors V (PixelFormat group) |
| 111 | `StringKeyParser::Crc24` / `Crc32` | Static hash keys, `static const uint32_t k = Crc24("name")`; each initialiser computes one key from a string literal and stores it. Could be `constexpr` in our build. | 0x9cbbdd, 0x9cbc3e V |
| ~110 | `std::string` construction | Static string globals (const char* -> std::string, e.g. table names, paths). Library. | L |
| 58 | `operator new` then fill | Static lookup tables allocated and filled at startup (sizes 0x8..0x100 elements). | L (+ data) |
| 56 | `FUN_a0af6560` (27 B each, objects at 0x10d9ff8..) | A 12-byte object (three zeroed pointers) filled by an EH-protected routine that constructs a `std::string` from a literal; the same shape 56 times. Purpose not worked out. | - |
| 27 | `FUN_a057c3c0` + 3 dword stores | Small objects (0xc bytes) zeroed after an empty constructor, 0x10dbdb4... | - |
| 14 | `FUN_a01be354` (92-102 B each) | Block pool constructions: allocate a pool block (`RZBlockPoolAlloc`), thread the free list. Element sizes passed as the argument (e.g. 0xc). | 0x1be354 V |
| 10 | `cRZCriticalSection::cRZCriticalSection` (0x1af64e) | Global locks (0x10cb090, 0x10cb5cc, 0x10cc3f4, 0x10cc5e8, ...). | V |
| 10+3 | `std::_Tree` head nodes (`_Buyheadnode`) | Empty static `std::map`/`std::set` objects (0x13e09d.., 0x13e293..). | L |
| 5 | `cRZRandom::cRZRandom(uint32)` (0x1b8bc8) | Global random generators. | V |
| 5 | `FUN_a04f8eb0` with a GUID string | Static GUID parse (`"13ae91e7-..."` form) into a global (0x10dbf20..). | - |
| 1 | `GeneralAllocator::Construct` (0x1c655d), at 0x13832 | The general allocator object 0x10cb098 (+ a lock at 0x10cb090). | V |
| 1 | `cRZStd::Construct` (0x1bf920) at 0x138385 | Global at 0x10cb5bc. | V |
| 1 | **`cGZMadCowCOMDirector` static** (init 0x1408d0) | The first COM director, global at 0x10fff78; constructor 0x1adffa then registers 4 class ids with `AddCls` (0x1ae080) at 0x1408fd..0x140939. | base ctor V |
| 1 | `CAtlBaseModule` (0x140f1b) | ATL module object at 0x1108008. | L |
| 2 | `std::_Init_locks` (0x13e030, 0x13e05e) | STL lock init (objects 0x10df5e8, 0x10df744). | L |
| ~224 | only `atexit(dtor)` | Objects with trivial constructors; the initialiser just registers the destructor. | - |
| rest | assorted one-offs | E.g. 0x1395b0 (396 B, tail-jumps to 0x1395fc), 0x138396..0x1383b4 (function-local statics stored inline), `eh_vector_constructor_iterator` for arrays (4), the singleton-ish setup at 0x13d011 (_EH_prolog based). | mixed |

Take-aways for stage 3 (own the data): about 300 of the 800 initialisers consist only of already
verified or library calls (pixel formats, CRC keys, pools, locks, RNGs, strings), and about 220 more
do nothing but register a destructor. The director static (0x1408d0) is the first place where a
COM director is built before WinMain runs.

## 3. WinMain (0xc2b87c, 681 B)

WinMain is mostly environment setup. The window is *not* created here and the framework is only
constructed here, not run (see section 4).

| Step | RVA | What |
|---|---|---|
| 1 | 0xc27b04 | `GetModuleFileNameW` wrapper (exe path); the directory is joined with the literal `crashpad_handler.exe`. (`CLAUDE.md`: our game copy has this exe disabled.) |
| 2 | `GetTempPathW`, 0xc2a75a (wstring append) | crash database directory = temp path + `TS2Legacy`. A narrow `std::string` holds the crash-report upload URL (a backtrace.io submit URL; the token is in the binary's string table, not repeated here). |
| 3 | 0xc2b206 (14 B + body via `_EH_prolog`) | *StartCrashReporter*: builds an annotation `std::map<string,string>` (`platform=steam`, `binary_version` from the exe's version resource, `minidump` flag), queries WMI (`root\CIMV2`, WQL; CoInitializeEx/CoInitializeSecurity/CoCreateInstance) for hardware info, and starts the crashpad handler process. |
| 4 | 0xc2a358 | *SteamInit*: `SteamInternal_SteamAPI_Init` with interface versions `SteamUtils010`, `STEAMAPPS_INTERFACE_VERSION008`, `SteamUser023`; returns bool. **On failure**: `MessageBoxA` ("Steam Initialization Failed." / "The game failed to successfully launch from steam.") and `exit(0)`. |
| 5 | `SteamInternal_ContextInit` (import 0xd9a700), then ISteamUser slot 2 | Obtains the user's SteamID (64-bit). |
| 6 | 0xc2df67 (609 B) | *SendAnalyticsEvent(steamid)*: WinINet `InternetOpenA`/`InternetConnectA`/`HttpOpenRequestA` POST of an event to a hard-coded EA analytics endpoint, header `x-ea-app-type`; thread-safe statics at 0x10fe44c/0x10fe454 cache `GetLastError`. A candidate to stub or drop in the standalone build. |
| 7 | 0xcac1f0 | Installs `FUN_cac170` as the C++ `terminate` handler (0x8444d7 = `set_terminate`) and the purecall handler (0x8261d6). `cac170` calls `SetUnhandledExceptionFilter`. |
| 8 | 0xc2a3a4 (50 B) | Loads imm32 and calls `ImmDisableTextFrameService` (so IMEs do not use TSF). |
| 9 | `GetCommandLineW` -> 0x144f5b (empty cRZString, V) -> 0x1afc9d (wide to narrow, `RZText::Convert`) -> 0x1ae5a3 | Builds the **cRZCmdLine** object (vtable 0xd9b4fc, class `cRZCmdLine`) on WinMain's stack. |
| 10 | `cRZCmdLine::IsSwitchPresent#2` (0x1af09e, vslot 9) | Switch `memoryReport`: when absent, 0x4dc940 is called on the global at 0x10fda2c (memory tracking setup; purpose unconfirmed). |
| 11 | thread-safe static (guard 0x10fdce8) | `cCTGFramework` at **0x10fdb50**, constructed by **0xc2ccdb** (section 4.1). Registered for destruction with `atexit` (0xd988fa). |
| 12 | thread-safe static (guard 0x10fdf58) | `cTSAppFW` at **0x10fdcf0**, constructed by **0xc26147(id=0x5afdaae5, name=ToChar(global cRZString 0x10fda54), cmdline)** (section 4.2). `atexit` 0xd988f0. |
| 13 | **0xc2d23b `RunApp(framework, app, cmdline)`** | If the app's exception-handler flag (byte at `(*app)+0x98+0x19b`, getter 0x1bbb09) is clear: `RunFramework` 0xc2d0a8 directly. If set: 0xc2d121, which runs `RunFramework` inside an SEH `__try`; the filter's handler picks an error message string by exception class, shows a `MessageBoxA` (via 0xc2da5a and the globals at 0x10fda54..0x10fdaf4) and calls `TerminateProcess`. Returns the exit code read from `g_ExitCode` (0x10c9384). |
| 14 | `SteamAPI_Shutdown` (import 0xd9a708) | After RunApp returns. Temporary strings are destroyed, WinMain returns the code. |

Status of the steps (lane EC, 2026-10-18; `db/status/WinMain.jsonl`, `WinMainInfra.jsonl`): 1 V
(0xc27b04); 3 crash reporter 0xc2b206 **replaced** (contract); 4 SteamInit 0xc2a358 V; 6 analytics
0xc2df67 **replaced**; 7 handlers 0xcac1f0 / 0xcac170 **replaced**; 8 V (0xc2a3a4); 9 V (cRZCmdLine
34/34, cRZString 38/38); 10 0x4dc940 M; 11 framework ctor 0xc2ccdb V; 12 app ctor 0xc26147 V (with the
exception reporter's construction 0x1ba914 V); 13 RunApp 0xc2d23b R, RunFrameworkGuarded 0xc2d121 V,
RunFramework 0xc2d0a8 V. Inside the `replaced` scope (roadmap; eligible, whether or not done yet): crash reporting (steps 1-3 and the
reporter at app+0x98), analytics (6), the terminate/purecall/unhandled-exception handlers (7), the
Steam glue (4, 5, 14), process setup (8, single-instance mutexes); later also the window
(`cCanvasW32::Init` 0x155e90) and the Direct3D device.

Single-instance and the crash handler are in the *app constructor* (0xc26147), not in WinMain:

* Switches read there: `ExceptionHandling` (values `enabled`/`off`/`disabled`, matched with
  0x1c3cb0), `quietCrash`, `noExceptionUpload`, `installUserData`, `crashTest` (forces a crash).
* Window message `EA_LAUNCHER_SHUTDOWN_REQUEST_SIMS2EP` is registered (`RegisterWindowMessageA`);
  `Sims2Exception` strings are set up for the exception reporter; `CoInitialize` is called.
* Two named mutexes through `cRZMutex` (0xc6c85e, vtable 0xe9ebf0, wraps `CreateMutexA`):
  `Global\The Sims 2` and `Global\The Sims 2 Body Shop`.
* EA's in-process exception reporting is the family around **0x1bb108..0x1bd87a**: 0x1bd3d8
  installs `SetUnhandledExceptionFilter(0x1bd687)` (the reporter's constructor already calls it);
  0x1bd687 is a stdcall stub (0 when the instance global 0x10caff0 is null, else a jump to `Report`
  0x1bd69a, thiscall ret 4). `Report` restores the previous filter, names the files with a time
  stamp (0x1bd49b), broadcasts 0xb627ad2, writes the minidump (0x1bd87a) and the text report
  (0x1bb410), broadcasts 0xb627ad3, reinstalls the stub and waits 1000 ms; it returns 1 when byte
  +0x19a is set, else the previous filter's answer under a debugger, else terminates (0x1bd001).
  There is no upload in this path. 0x1bd019 resolves `IsDebuggerPresent`; **0x1bc8a7
  (955 B)** is the lazy loader of the DbgHelp/ImageHlp wrapper (`SymInitialize`, `StackWalk`,
  `SymFromAddr`, `MiniDumpWriteDump`, ... through function-pointer globals at 0x10cb000..),
  called from ten reporting functions (0x1bb108, 0x1bb2b6, 0x1bb410, 0x1bbf88, 0x1bc1fc,
  0x1bcc62, 0x1bcdd4, 0x1bcf4a, 0x1bd87a, 0x1bd69a). The app object embeds the reporter at
  `app+0x98` (0x1ac bytes, no vtable; constructed by 0x1ba914 in 0xc26147): five cRZStrings
  (module directory, "Exception Report", "Minidump", the .txt and .mdmp names), a cRZTextFile log at
  +0xa0, level bytes +0x194.., flags +0x19a/+0x19b (enabled), a listener list at +0x1a0 and the
  previous filter at +0x1a8. PreFrameworkInit calls SetDirectory 0x1bd42a and AddListener 0x1bac86
  on it; 0xc28e32 calls AddExtraData 0x1bac12. Its broadcasts are its only coupling to the game.

## 4. The framework

### 4.1 Objects and globals

| Object | Class | Where | Built by |
|---|---|---|---|
| framework | `cCTGFramework` : `cGZFramework` : `cIGZFrameWork`, `cIGZFrameWorkW32` (+4), `cIGZMessageTarget2` (+384) | static 0x10fdb50 | ctor **0xc2ccdb** (vtables 0xe995cc / 0xe99694 / 0xe996b4; uses `cGZFramework` base ctor 0x141d76) |
| application | `cTSAppFW` : `cGZApp` : `cRZSystemService`, `cIGZApp` (+28), `cITSAppFW` (+64), `cTSAppCommonImpl` (+68), `cIGZMessageTarget2` (+92) | static 0x10fdcf0 | ctor **0xc26147**; service id 0x5afdaae5, priority -2,000,000 |
| COM object | `cGZCOM` (embedded in the framework at +8, service id 0xa3cd8db3, priority 10,000,000) | in framework | `cGZFramework` base ctor |
| root director | `cGZCOMDirector` | function-local static 0x10c9390, getter `GetCOMDirector` 0x14491b (V), ctor 0x144897 (V) | on first use |
| app director | `cTSAppFWCOMDirector` | static 0x10fdf60, getter 0xc2be73, ctor 0xc2bba4 | on first use |
| globals | `g_Framework` 0x10c9378 and 0x10c937c, `g_App` (cIGZApp interface) 0x10c9380, `g_ExitCode` 0x10c9384, `g_AppForMsgHook` 0x10fdfe8 | | set by 0xc2d0a8 |

Framework accessors that the rest of the code uses everywhere: `GetFramework()` is 0x1427b5 (V,
returns g_Framework; reached through the thunk 0x14323f), `GetDirectorFrameworkObject()` is
0x1ae18d (V). Service lookup is framework slot 5 `GetSystemService(id, iid, void**)` (0x1428e2: find
by id in the service map at fw+0x8c, then QueryInterface); the cached-getter templates
`CachedServiceGetter<...>` (0xb390c0 and 7 more, V), `ScopedRef::AcquireFromService` (0x20dc91, V)
and the `GetCachedSingleton*` helpers (0x20ddfb, 0x20de51, 0x20dde0, V) wrap it.

Framework fields seen: +0x7c hook list, +0x84/+0x8c service containers, +0xac on-idle list, +0xb4
(list), +0xbc tick list, +0xc4 (list), +0xd0 on-idle interval, +0xd4 / +0xd8 tick / idle counters,
+0xe0 **state** (0..14), +0xe4..+0xe8 re-entrancy flags, +0xec nesting count, +0xf4
`cRZCriticalSection` (the framework lock, slot calls `[+4]` = lock, `[+8]` = unlock), +0xfc embedded
`cRZCmdLine` (slot 21 `CommandLine()` returns it), +0x150 install flag, +0x15c/+0x164 streams and debug
level, +0x180 `cIGZMessageTarget2` subobject, +0x184 a helper object built by 0x1c02fb (a key map;
0x1c081e / 0x1c0458 walk it), +0x18c a map/set keyed by service id, built in the constructor (0xc2ccdb adds ids such as 0xeb7d6513,
0x9b3798f7, 0xdae3783f, 0x7a3e667d, 0x056b906e, 0x8af76dc8 ...; the startup loops skip
`SetServiceRunning(true)` for services in this set).

### 4.2 Interfaces and slot meaning (from `db/interface-slots.jsonl`, checked at the call sites)

`cIGZFrameWork` slots used on the startup path: 3 AddSystemService, 5 GetSystemService, 7 AddHook,
9 AddToTick, 11 AddToOnIdle, 14 SetOnIdleInterval, 15 OnTick, 16 OnIdle, 17 IsTickEnabled,
18 ToggleTick, 19 Quit, 20 AbortiveQuit, 21 CommandLine, 23 GetCOMObject (returns fw+8), 24 GetState
(reads +0xe0), 36 SetApplication, 37 Application, 38 ReportException. `cCTGFramework` adds the
following (names mine, derived from behaviour): 40 scalar deleting destructor (0xc2cec9), **41
InitHighPriorityServices** (0xc2d6e6), **42 InitAppBandServices** (0xc2cf03), **43
InitLowPriorityServices** (0xc2d506), **44 ShutdownLowPriorityServices** (0xc2d8d2), **45
ShutdownAppBandServices** (0xc2cfde), **46 ShutdownHighPriorityServices** (0xc2d5bb), 47 (0x140fe0,
empty), **48 MainLoop** (0xc2d9c5).

`cIGZSystemService`: 3 GetServiceID (field +0xc), 4 SetServiceID, 5 GetServicePriority (+0x10),
6 IsServiceRunning (+0x18), 7 SetServiceRunning, 8 Init, 9 Shutdown, 10 OnTick(uint32), 11 OnIdle,
12 GetServiceTickPriority (+0x14). Defaults are the shared stubs 0x145770 / 0x149c60 / 0x149cf0 /
0x156110 / 0x1b9690 / 0x144910 (returns true) / 0x148260 (returns true, `ret 4`) / 0x149d00.
`cRZSystemService::cRZSystemService(id, priority)` is 0x1b9584 (V).

`cIGZFrameWorkHooks` (implemented by every `cRZCOMDllDirector` at subobject +4, default stubs return
true): 3 PreFrameWorkInit, 4 PreAppInit, 5 PostAppInit, 6 PreAppShutdown, 7 PostAppShutdown,
8 PostSystemServiceShutdown, 9 AbortiveQuit, 10 OnInstall. The hook runners are the small loops
0x142b97 (slot 3), 0x142ad7 (4), 0x1429b7 (5), 0x142b37 (6), 0x142a17 (7), 0x142a77 (8): each takes
a locked copy of the hook list (0x142c2a, fw+0x7c) and calls the slot on every hook; `AddHook` is
slot 7 (0x14229a).

`cIGZApp` (subobject +28 of `cTSAppFW`, vtable 0xe984b8): 3 AsIGZSystemService, 4
AddApplicationService (0xc59fe6), 6 PreFrameworkInit (0xc290d8), 7 PostFrameworkInit, 8 GZRun
(default 0x147480 returns false), 9 PreFrameworkShutdown, 10 Framework, 11
AddDynamicLibrariesHere (0xc26c30), 12 AddCOMDirectorsHere (0xc26bf5), 13
AddApplicationServicesHere (0xc267fb, returns true), 14 LoadRegistry. The app's system-service
face (primary vtable 0xe98464): Init = 0xc27c3e, Shutdown = 0xc2a1e3, OnTick = 0xc28fcc.

`cIGZCOMDirector` (primary vtable of every director, 16 interface slots + the deleting destructor at
16): 0 QI, 3 InitializeCOM(com, path) (0x1ae24d), 4 OnStart(com), 5 EnumClassObjects, 6 GetClassObject,
7 CanUnloadNow, 8 OnUnload, 9 RefCount, 10 RemoveRef, 11 FrameWork (returns +0x34, the
`cIGZFrameWork`), 12 GZCOM (returns +0x30, the `cIGZCOM`), 13 GetDirectorID, 14 GetLibraryPath, 15
GetHeapAllocatedSize. Correction (lane EC): "the framework object" that `GetDirectorFrameworkObject`
0x1ae18d returns, `AddDirector` passes on and `OnStart` receives is the **cGZCOM** (cIGZCOM, fw+8), not
the framework: InitializeCOM stores it at +0x30 and its `FrameWork()` at +0x34. The ad-hoc interface
names `ComFramework` / `NbrFramework` in `db/methods` are cIGZCOM, and `SimServiceHub` slot 5 is
cIGZFrameWork slot 5 (GetSystemService). Full slot lists: `src/gz/cIGZ*.h`.

### 4.3 The run, step by step (all in the order the code executes)

`RunFramework` 0xc2d0a8(framework, app, cmdline):

1. 0x14470b(framework): g_Framework (0x10c9378 and 0x10c937c) = framework, two AddRefs.
2. `app->QueryInterface(0x66, &p)` -> if ok 0x1446fc(p): g_App (0x10c9380) = the `cIGZApp` face, AddRef.
3. `g_AppForMsgHook` (0x10fdfe8) = app.
4. **GZFrameWork_Startup 0x144514(cmdline, 0)** below.
5. If it returned true: **GZFrameWork_Run 0x1446ba**; then **GZFrameWork_Shutdown 0x14472b** always.
6. Releases `p`; returns.

**GZFrameWork_Startup 0x144514** (state field fw+0xe0 in brackets):

| # | Step | Detail |
|---|---|---|
| 0 | state = 0; 0x143660(cmdline) stores the command line copy (locked), 0x1427a7 parses/commits it into fw+0xfc, 0x14368c(0) sets the install flag (+0x150) | |
| 1 | `com = fw->GetCOMObject()`; `com->RealInit()` (cGZCOM slot 9, 0x145b7e); if ok `com->SetServiceRunning(true)` (slot 11); `QI(com, 0x287fb697)` gives its `cIGZSystemService`; `fw->AddSystemService(it)` | the COM object is the first registered service; 0x287fb697 is the id of the `cIGZSystemService` interface (it is the id every "register this object as a service" site passes to QueryInterface) |
| 2 | `app->AddApplicationServicesHere()` (slot 13) | creates and registers the application services: 4.5. If false -> abort |
| 3 | `app->AddCOMDirectorsHere()` (slot 12) | 4.4 |
| 4 | `app->AddDynamicLibrariesHere()` (slot 11) | debug DLL list (EdithEditorD, TSDebugD, GZLogMFCWinD.dll, GZPerforceD.dll ...) |
| 5 | state = 1; `app->PreFrameworkInit()` (slot 6). False -> abort (COM `RealShutdown`, g_ExitCode = -1, return false) | cTSAppFW::PreFrameworkInit 0xc290d8 reads config, locale, `Locale` switch, file system roots |
| 6 | hooks: **PreFrameWorkInit** (0x142b97); state = 2; state = 3 | |
| 7 | `fw->InitHighPriorityServices()` (slot 41): every service with priority > -2,000,000, in descending priority order: `Init()` (slot 8); on success and unless its id is in the +0x18c set `SetServiceRunning(true)`; afterwards it appears to create a `cRZMessage2Standard` and register the framework's message target (id 0xce1505fd) with the message server | on an Init failure the loop QIs and aborts |
| 8 | hooks: **PreAppInit** (0x142ad7); state = 4 | |
| 9 | `fw->InitAppBandServices()` (slot 42, bool): priority in [-2,999,999, -2,000,000], i.e. the app itself (`cTSAppFW::Init` 0xc27c3e) | false -> abort |
| 10 | `fw->ToggleTick(true)` (slot 18); state = 5 | the main loop only blocks in `WaitMessage` while this is off |
| 11 | `fw->InitLowPriorityServices()` (slot 43): priority < -2,999,999 (the CAS/catalog systems at -3,000,000); state = 6 | |
| 12 | `app->PostFrameworkInit()` (slot 7); hooks: **PostAppInit** (0x1429b7); returns `g_ExitCode == 0` | |

**GZFrameWork_Run 0x1446ba**: `app = fw->Application()`, state = 7, `app->GZRun()` (slot 8). The
default returns false, in which case the framework runs **`fw->MainLoop()` (slot 48)** itself.
Returns `g_ExitCode == 0`.

**GZFrameWork_Shutdown 0x14472b**: state 8, 9, 10; hooks **PreAppShutdown** (0x142b37);
`app->PreFrameworkShutdown()` (slot 9); `fw->ShutdownLowPriorityServices()` (44); state 11;
`fw->ShutdownAppBandServices()` (45; `cTSAppFW::Shutdown` 0xc2a1e3); hooks **PostAppShutdown**
(0x142a17); state 12; `fw->ShutdownHighPriorityServices()` (46; descending order reversed, calls
`Shutdown()` slot 9); `ToggleTick(false)`; hooks **PostSystemServiceShutdown** (0x142a77); clears the
tick/idle/hook/service containers (0x1427bb, 0x142bf7) and releases the streams (0x1437c5); states 13
and 14; releases g_App, g_Framework (0x143782); returns.

Service order inside each band follows the container at fw+0x84 (a `std::multimap<int32_t priority,
...>`), enumerated by `BuildServiceList` 0x142cb2 (V): flag 0 (init) walks it from the highest
priority down, flag 1 (shutdown) from the lowest up. (Settled by the verified BuildServiceList, `src/gz/Framework.h`.)

### 4.4 COM directors: how systems register their factories

The engine is a set of statically linked "DLLs", each with a `cRZCOMDllDirector` subclass (43
classes with RTTI vtables; `db/classes.jsonl` lists them all). A director's constructor (base ctor `cRZCOMDllDirector::Construct`
0x1adffa, V; destructor 0x1ae048, V) registers its class factories with `AddCls(clsid, factory)`
(0x1ae080; 0x1ae0ad is the variant with kind 2) and pulls in the directors it depends on with
`AddDirector(sub)` (0x1ae0da: calls `sub->InitializeCOM(com, path)` with this director's cGZCOM, recurses into the sub's
own directors, then appends). Directors are function-local statics behind small getters. Callers of
the base constructor (35 functions; `tools/xrefs.py 0x1adffa`): 0x144897 (root), 0x18e4f0 CompGeom,
0x1a8346 Event, 0x1ab195 Expat, 0x1fc3d0 PathPlan, 0x244e30 TSSG, 0x415ae0 TSSim, 0x7e5b50 TSWorld,
0x85b203 UIScript, 0x87b39a GX, 0x909f57 Font, 0x92f3a7 Win, 0x970cda Win32, 0x98be40 Numerics,
0x9d0c60 SceneDBAnim, 0xa33bf0 SceneGraph, 0xb0e046 Freshness, 0xb12b68 SndEa, 0xb167ef Speex,
0xb22254 Sound, 0xb29d78 HitMan, 0xbb4710 Effects, 0xc6eb80 RZMessage, 0xcb4ec0 TSCommon, 0xcb52d0
TSCAS, 0xd0acb0 TSUICommon, 0x145dd0 Draw2D, 0x154290 Graphics, 0x1d3b70 Persist, the Lua5 getter
body (0xc76e40) and the static MadCow initialiser (0x1408d0); 0x8b7f30, 0x9b3210, 0x9b34b0 and
0xb0a900 are directors reached from the parents above (their classes are not named in the db).

Tree, in the order it is assembled at startup:

1. Before WinMain: static `cGZMadCowCOMDirector` (0x10fff78), initialiser 0x1408d0, registers 4 clsids.
2. `app->AddCOMDirectorsHere()` = 0xc26bf5: `GetCOMDirector()->AddDirector(AppDirector())`
   (0xc2be73 builds `cTSAppFWCOMDirector`, ctor 0xc2bba4, which registers 6 class factories
   (message-related, `cTSGameTipService` factory 0xcb5ce0, `cTSTradeableRewardsService` 0xcb7b50, ...) and
   adds the sub-directors RZMessage (0xc6ebe7), TSSG (0x140fe3), TSCommon (0xcb4e00), TSUICommon
   (0xd0ad20), then 0x140ff3: Event (0x1a839b), CompGeom (0x18e620), Graphics (0x154290), Draw2D
   (0x145dd0), a director at 0x5b6847, Persist (0x1d3b70), Sim (0x4162a0), PathPlan (0x1fc490),
   0x1ab2c3 and 0x1cb89d; then TSCAS (0xcb5410), MadCow (0xc5a240), Lua5 (0xc76e40, vtable 0xea05e0)),
   then `appDirector->OnStart(frameworkObject)` (`cTSAppFWCOMDirector::OnStart` 0xc2beec), then 0xc29a0b
   (Steam UI language -> the game's language setting; a chain of 21 Steam language names) and a tail jump to 0x140f40: if
   the `nosound` switch is absent, adds the audio director (getter 0x20efaf) and starts it.
3. `cTSAppFWCOMDirector::OnStart` (0xc2beec) obtains several services by id, calls 0x14107c, which
   `OnStart`s the engine directors in a fixed order (Event, CompGeom, Graphics, Draw2D, TSSG, 0x5b6847,
   Persist, Sim, ...), then TSCommon, TSCAS and Lua5, then MadCow; finally it constructs and
   registers `cGZRealtimeDebugDisplay` (0x9704bb, id 0x8414a2c2, priority 4,000,000).

Directors whose `OnStart` (vtable slot 4) is non-default, with the services they create:
cTSPersistCOMDirector 0x1d3e20 (`cTSPersistSystem`, `cGZPersistResourceManager` via 0x1d8b90),
cTSSimCOMDirector 0x416370 (`cTSSimSystem`, id 0xdae3783f), nTSSG::cDllCOMDirector 0x2454c0
(`nTSSG::cTSSGSystem`), cGZUIScriptCOMDirector 0x85b355 (`cGZUIScriptService`),
nGZEvent::cEventCOMDirector 0x1a83f5, nGZGraphic4::cDllCOMDirector 0x154460,
nGZGraphic4::cDraw2DDLLCOMDirector 0x145f20, nGZSceneGraph::cDllCOMDirector 0xa34210,
nGZSceneDBAnim::cDllCOMDirector 0x9d11d0, cGZWinCOMDirector 0x92f8da, cGZDrawCOMDirector 0x8b8030,
cGZResManCOMDirector 0x9b33b0, cRZMessageCOMDirector 0xc6ec41, cHitManCOMDirector 0xb2a422,
cGZSoundCOMDirector 0xb22420, cGZSndEaCOMDirector 0xb12c5f, cTSCommonCOMDirector 0xcb5110,
cTSCASCOMDirector 0xcb54a0, cTSUICOMDirector 0x5b72e7, cTSWebCOMDirector 0x1cb910,
cFontCOMDirector 0x90a048, cTSAudioCOMDirector 0x20f454.

### 4.5 Application services (`AddApplicationServicesHere`, 0xc267fb)

Each is `new T(...)`, then `fw->AddSystemService(svc->AsSystemService())` (slot 3), in this order.
All twelve constructors, 0xc274e3, 0xc2b078 and 0xc29d5a are verified (groups AppInit, AppServices,
AppServicesBlockers, the per-class groups); the function itself (0xc267fb, 1018 B) is mapped.

| # | Service | Ctor | Notes |
|---|---|---|---|
| 1 | `cGZAllocatorService` (id 0x3ae4bea3, prio 9,000,000) | 0x1be59c (V) | whole class partly verified (`cGZAllocatorService` group) |
| 2 | `nGZLog::cService` (id 0x09b69c4e, prio 8,999,999) | 0xc9c309 | configured with group `NoGroup` / name `AppDebugger`, level 4; `QI(0x287fb697)` then registered |
| 3 | `cGZPropertyManagerT<string>` | 0xc26045 | held in an `RZUnknownSlot` (0x142080, V), registered |
| 4 | `cRZNetManager` | 0xc65932 | |
| 5 | `cRZNetSSLManager` | 0xc644aa | |
| 6 | `cGZFileSystem` | 0xc75ccd | then slot 0x3c on it, 0xc2b078 and 0xc29d5a (file system roots / registry setup) |
| 7 | `cGZTimerService` | 0xc6d0da | OnTick 0xc6d190 |
| 8 | `cGZRegistry` (id 0x25076b9e, prio 10,000,010) | 0xc6cb27 | |
| 9 | `cGZClipboardManager` (id 0x27ebfffd, prio 1,250,100) | 0xc6d222 | |
| 10 | `cRZCallbackSystemService` (id 0xa76c78e7, prio 2,999,999) | 0xc68264 | |
| 11 | `cTSGlobals` | 0xc2c4cf | |
| 12 | `nTSWorld::cWorldDB` (id 0x4a037ee3, prio -1,000,001) | 0xd0b6c0 | |
| then | 0xc274e3 (reads the command line text and the text options 0x2bcfce83 / 0xebcfce8e / 0x6bcfce93, then queries cTSAppCommonImpl's option flag bits), releases | | |

Other services appear from the directors' `OnStart` (4.4) or from classes' own getters. IDs and
priorities, taken from the constructor arguments (`cRZSystemService(id, priority)`, 0x1b9584); the
list is incomplete (some services use a derived constructor):

| Band | Priority | Services (id) |
|---|---|---|
| high | 10,000,010 / 10,000,000 | cGZRegistry 0x25076b9e; cGZCOM 0xa3cd8db3 (embedded) |
| | 9,000,000 / 8,999,999 | cGZAllocatorService 0x3ae4bea3; nGZLog::cService 0x09b69c4e |
| | 5,000,001 / 5,000,000 | nGZEvent::cEventSystemService 0x5b1e896f; cGZMessageServer 0x64063387, cGZMessageServer2 0x04fa845b, cGZMessageQueueManager2 0x64fa8434 |
| | 4,000,000 | cGZRealtimeDebugDisplay 0x8414a2c2; cTSCheatSystem 0x2a4915ea |
| | 2,999,999 | cRZCallbackSystemService 0xa76c78e7 |
| | 1,500,000 / 1,250,100 / 1,000,000 / 999,999 | cGZDrawSystem 0xeb2ea3cb; cGZClipboardManager 0x27ebfffd; cGZPersistResourceManager 0x056b906e; cGZWinProcMgr 0x17aa3541 |
| | 499,998 / 499,990 | cGZSndMgr 0x1af85bfb; cHitMan 0x1b2ce671 |
| | 200,000 / 199,999 | nGZSceneGraph::cSceneGraphSystem 0xe5074b6b; nGZSceneDBAnim::cSceneDBAnimSystem 0xbafeed76 |
| | -999,999 / -1,000,000 / -1,000,001 | cTSCommonSystem 0x8af76dc8; cTSSimSystem 0xdae3783f, cTSContentRegistry 0x0dc7057a, cTSCustomContentSupport 0xac59cfaf; nTSWorld::cWorldDB 0x4a037ee3 |
| app band | -2,000,000 | cTSAppFW 0x5afdaae5 (constructed by WinMain) |
| low | -3,000,000 | nTSCASCore::cTSCASSystem 0x4cf22ab5; cGZUIScriptService 0x5a356e15; cTSSkinCatalog 0x4bcf1bb0; cTSBinCatalog 0x0c4f252f; cRZLanguageManager 0x441e5070; cTSPatternCatalog 0x8c8cc5ed; cTSCASBodyPartSystem 0xeb7d6513; cTSMaterialCatalog 0xac6965bc |

Window creation: the main window is created by the graphics layer, not by WinMain:
`nGZGraphic4::cCanvasW32::Init` (0x155e90) calls `RegisterClassExA` and `CreateWindowExA`; the
canvas is itself a system service (`cIGZSystemService` at +8). Direct3D comes from
`Direct3DCreate9` (import thunk 0x859f34, used by the device enumeration, `cDeviceEnumDX`).

## 5. The main loop and shutdown

### 5.1 `cCTGFramework::MainLoop` (slot 48, 0xc2d9c5, 149 B)

```
for (;;) {
  if (!fw->IsTickEnabled())   WaitMessage();        // block while ticking is off
  while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT /*0x12*/) { g_ExitCode = msg.wParam; return; }
    if (!appMsgHook->vslot3(&msg))                  // g_AppForMsgHook + 0x40, cITSAppFW slot 3 = 0xc296b4
        { TranslateMessage(&msg); DispatchMessageA(&msg); }
  }
  appMsgHook->vslot3(nullptr);                      // idle call with no message
  if (fw->IsTickEnabled()) fw->OnIdle();            // slot 16
}
```

`cITSAppFW` slot 3 (0xc296b4) is the app's message pre-translation hook; it also owns the shutdown
request: the `cRZMessage2Standard` message server broadcast and `SetEvent` on the launcher's shutdown
event `EA_LAUNCHER_SHUTDOWN_REQUEST_SIMS2EP`.

### 5.2 Per frame

* `cGZFramework::OnIdle` (slot 16, 0x142dbb, 271 B): `SteamAPI_RunCallbacks`; ++tick counter (+0xd4);
  **`fw->OnTick(counter)`** (slot 15 = `cCTGFramework::OnTick`, 0xc2d427); then, every
  `OnIdleInterval` ticks (+0xd0, set by `SetOnIdleInterval`, minimum 1), ++idle counter (+0xd8) and
  call **`OnIdle(idleCounter)`** (service slot 11) on every service registered with `AddToOnIdle`
  (list +0xac). The lists are copied under the framework lock first.
* `cCTGFramework::OnTick` (0xc2d427): copies the tick list (+0xbc, filled by `AddToTick`) under the
  lock and, for each service that `IsServiceRunning()` (slot 6), calls `OnTick(counter)` (slot 10).
  `AddToTick`/`AddToOnIdle` are slots 9 and 11 (0x1424fe, 0x142474).
* So the frame is: Windows messages drained -> app message hook idle call -> Steam callbacks ->
  tick every running, tick-registered service (registration order) -> OnIdle services.

Services with a non-default `OnTick` (per-frame work), from their vtables: cGZTimerService
(0xc6d190), nGZLog::cService (0xc9dd3a, V), cTSAppFW (0xc28fcc: `Sleep(onTickSleepTime)`
throttle, then drives the per-frame services through cached service getters), cTSSimSystem
(0x419fb0), cTSCommonSystem (0xcbae10), cGZDrawSystem, nGZSceneGraph::cSceneGraphSystem (0xa4d040),
nGZSceneDBAnim system, cGZMessageServer (0xc7558d), cGZMessageServer2 (0xc6785a), cTSCASSystem
(0x506720), cTSBinCatalog (0xb8d510), cTSWinCheatConsole (0xcaba10), cGZWinIMEProxy, cGZWWWSystem.
(Slot addresses taken from the cIGZSystemService subobject vtable; entries equal to 0x148260 or
0x144910 are the do-nothing defaults.)

### 5.3 Quit

`cGZFramework::Quit` (slot 19, 0x1431fc) calls `PostQuitMessage(code)`; the loop above stores the
code in `g_ExitCode` and returns. `AbortiveQuit` (slot 20, 0x142270) logs through `OutputDebugStringA`,
runs 0x142957 (the hooks' AbortiveQuit) and calls `exit()`. `RunApp` returns the exit code, WinMain runs `SteamAPI_Shutdown` and returns it, and the
CRT runs the `atexit` list (static destructors in reverse registration order).

## 6. What is already verified on this path

Refreshed by lane EC (2026-10-18), from `db/status` (hook shape: the original's vtables, our
functions hooked onto their entries). Per group, verified / total:

| Group | V / total | What is left |
|---|---|---|
| Framework (cGZFramework, cCTGFramework, Startup/Run/Shutdown, hook runners, band loops, MainLoop) | 70 / 76 | AbortiveQuit 0x142270 (calls `exit`), StdOut/StdErr/StdIn 0x143887/0x1437e7/0x143837 (stream classes 0x1b4dbe/0x1b4da6 not mapped), the command-line pair 0x143660/0x1427a7 (mapped; their callees are now verified) |
| GZCOM (cGZCOM, cGZCOMLibrary) | 38 / 38 | CreateGuid/CreateGuids 0x145325/0x1453ab are not in the group (time-zone helpers) |
| cRZCmdLine / cRZString | 34 / 34, 38 / 38 | |
| COMDirectors (41 directors, data-driven) | 196 / 197 | the remaining OnStarts: nTSSG 0x2454c0, cTSAppFW 0xc2beec, cTSCAS 0xcb54a0, cTSCommon 0xcb5110, cTSPersist 0x1d3e20, cTSSim 0x416370, cTSUI 0x5b72e7; hooks-face overrides of cGZSound, cHitMan, cTSUI |
| AppCtor / AppInit | 10 / 10, 14 / 14 | |
| TSApp (cTSAppFW slots, cTSAppCommonImpl) | 30 / 46 | mapped (16): Init 0xc27c3e, Shutdown 0xc2a1e3, OnTick 0xc28fcc, PreFrameworkInit 0xc290d8, AddDynamicLibrariesHere 0xc26c30, AddApplicationServicesHere 0xc267fb, DoMessage 0xc27605, PreTranslateMessage 0xc296b4, SetupUserDataDirectories 0xc2a7de, and the cTSAppCommonImpl workers 0xca7230, 0xca5e80, 0xca69f0, 0xca5ed0, 0xca6200, 0xca75c0, 0xca6c10 |
| WinMain / WinMainInfra | 21 / 23 + 4 replaced | WinMain 0xc2b87c (M; the CRT seam), RunApp 0xc2d23b (R) |
| App services (12 constructors) | all V | the classes behind them are separate subsystems |

Also verified: `cRZSystemService` ctor/dtor (0x1b9584, 0x1b95fe), `cGZAllocatorService`,
`cRZCOMDllDirector` Construct/Destruct and slots, `GetCOMDirector`/`ConstructDirector`,
`GetDirectorFrameworkObject`, `GetFramework`, `RZUnknownSlot::Assign`, `cRZCriticalSection`,
`cRZRandom`. No status yet: the cGZFramework base slots 40..48 (0x142207, 0x14307f, 0x1425c7,
0x142f87, 0x143133, 0x142662, 0x142ffb, 0x140fe0, 0x1435c2: never run, as the live object is a
cCTGFramework, but a C++ base class needs them), the forwards and thunks of the three framework faces,
the second cRZSystemService constructor 0x1b95bf (id, priority, tickPriority), 0x14107c
(StartEngineDirectors), `entry` and `__scrt_common_main_seh`.

None of it is in the standalone shape yet: every class above still installs the game's vtables. The
headers for the shape and the split into lanes are in `docs/subsystems/startup.md` and
`notes/feedback/EC.md`.

## 7. Open questions

* ~~Service iteration order inside a band~~ (answered, 4.3: init highest priority first). Still open:
  the meaning of the +0x18c id set (apparently "do not auto-run", probably services driven by the sim
  loop; the start/stop messages 0xcd/0xce drive them).
* What `0x4dc940(0x10fda2c)` (the `memoryReport`-absent path) installs.
* The roles of cITSAppFW slot 3 return value in `MainLoop` (non-zero = message consumed).
* `cTSAppFW::Init` (0xc27c3e) in full: it reads `control%d.dat`, the registry (`DisplayName`),
  `activeUserDataDir`, sets up paths and the string roots (`0x2ce990aa`, `0x4bcfce5e` style keys);
  not charted past its first screen. (Init only tests `activeUserDataDir` for presence; the value is
  read by cTSAppFW face +0x44 slot 15, 0xc2a7de: default user data = My Documents (GetSpecialFolderPath
  0x1c50d6, id 8) + `EA Games/<name>/`; `-activeUserDataDir:<dir>` and `-activeAllUsersDataDir:<dir>`
  replace them (made absolute, created), unless `-userDataMode:develop`. Instances use these: host/instance.h.)
* Exact class of the director at 0x5b6847, 0x1ab2c3, 0x1cb89d (added by 0x140ff3).
* `0xc2d121`'s exception class to message mapping (four classes -> four strings at 0x10fda54..).

## 8. Suggested order to own startup

The startup path is shallow in code but wide in data: a small number of framework functions (a few
KB) drive 43 directors, ~40 services and 800 static objects. The cheapest, most isolated pieces
first:

1. **The framework core (about 4 KB, high leverage).** `GZFrameWork_Startup/Run/Shutdown`
   (0x144514, 0x1446ba, 0x14472b; ~700 B), the six hook runners (0x142b97, 0x142ad7, 0x1429b7,
   0x142b37, 0x142a17, 0x142a77; ~100 B each, identical shape: a table-driven test), `MainLoop`
   (0xc2d9c5, 149 B), `OnIdle` (0x142dbb) and `OnTick` (0xc2d427), the six band loops (slots 41..46,
   ~300 B each, shared shape), `GetSystemService` (0x1428e2), `AddSystemService` (0x14234f),
   `AddHook`/`AddToTick`/`AddToOnIdle` and the removes. Everything they call is already verified or
   library (`cRZCriticalSection`, `RZUnknownSlot`, `std::list`/`map`) apart from the framework state
   itself, so this is class-lane work for `cGZFramework`/`cCTGFramework` (49 slots; done in the hook
   shape since: 70 of 76 functions verified, section 6; next is the standalone shape, handoff in
   `notes/feedback/EC.md`).
   The exit code, state field, and global pointers are plain data.
2. **The cRZSystemService family and the director base.** `cRZSystemService` is done; finish
   `cRZCOMDllDirector` (QI, `InitializeCOM`, `EnumClassObjects`, `GetClassObject`, `CanUnloadNow`,
   `AddCls` 0x1ae080/0x1ae0ad, `AddDirector` 0x1ae0da; 17 slots, mostly stubs shared with other
   classes), then the 43 director classes as a **generator**: each director's constructor is "base
   ctor, install vtables, N x `AddCls(clsid, factory)`, M x `AddDirector`", plus a small `OnStart`.
   A table-driven test that diffs the registered (id, factory) pairs against the original's covers
   them in bulk, in the spirit of the vtordisp approach.
3. **WinMain and the CRT seam.** Reimplement `WinMain` (0xc2b87c) and `RunApp`/`RunFramework`
   (0xc2d23b, 0xc2d0a8, 0xc2d121) around a decision about the crash reporter, the analytics POST
   (0xc2df67) and Steam: crashpad (0xc2b206) and the analytics call are optional in a standalone
   build, the Steam init (0xc2a358) is three import calls. `cRZCmdLine` (17 slots; the wide-to-narrow
   conversion 0x1afc9d) is the dependency that matters here; `cRZMutex` (0xc6c85e) for
   single-instance. `entry`/`__scrt_common_main_seh` are replaced by our own CRT start, which
   removes the `_initterm` tables as the entry point and makes stage 3 (static data) concrete.
4. **Static data (stage 3 seam).** Define the ~300 static objects whose constructors are already
   verified in our source (pixel formats, CRC keys as `constexpr`, block pools, locks, RNGs, the
   allocator) and drop their initialisers; the ~220 destructor-only initialisers disappear with them.
   The static `cGZMadCowCOMDirector` is the first director to own.
5. **The application services (0xc267fb).** A flat list of 12 constructors; each needs its class:
   `nGZLog::cService` (with `cRZStdAdapter` / caption back-ends), `cGZTimerService`, `cGZRegistry`,
   `cGZClipboardManager`, `cRZCallbackSystemService`, `cGZFileSystem`, `cRZNetManager` (low value),
   `cTSGlobals`, `nTSWorld::cWorldDB` (large). Do the small ones as classes; keep the large ones
   (world DB, file system) as `reimplemented` shells that construct the original object until their
   classes are done.
6. **`cTSAppFW` last.** Its `Init` (0xc27c3e), `PreFrameworkInit` (0xc290d8), `OnTick`, shutdown and
   the 4.4 KB `cTSAppCommonImpl::vf14` pull in config, paths, registry and the exception reporter;
   it is where the game-specific policy lives.

**What the startup path depends on most** (so these classes pay back first): `cRZString` and the
std string/list/map/vector instantiations (library), the refcount/`cRZUnknown` bases and
`RZUnknownSlot`, `cRZCriticalSection`, the service getter helpers (`GetFramework`,
`ScopedRef::AcquireFromService`, `CachedServiceGetter`), `cRZCmdLine` (every `IsSwitchPresent` call
in sections 3-4), the log service (`nGZLog::cService`: nearly every subsystem logs through it),
`cGZAllocatorService`, `cRZMessage2Standard` + the message servers (framework, app and many
services register message targets), and `cRZFile`/`cGZFileSystem` for the first config reads.
Graphics (`nGZGraphic4`), persist (`cTSPersistSystem`) and the Sim system are leaves of the
startup path from this chart's point of view: they are created and started by directors but nothing
in section 3 or 4 needs their internals.

## 9. Tool and brief notes

* `db/functions.jsonl` has records for only 23 of the 800 static initialisers (and none for most
  of 0x136e60..0x1409ff). Add function records for the initialiser table entries so
  `candidates.py` and the planned reachability metric ("coverage of code reachable from the entry
  point") can see them; the tables are 0xd9b3ac..0xd9b3d8 (10) and 0xd9a724..0xd9b3a8 (800).
* `db/` has no record of static objects (address, class, constructor, destructor, guard variable).
  The stage 3 milestone (own the data) needs one; the initialiser bodies above give it almost for
  free (each is `mov ecx, <object>; call <ctor>; push <dtor>; call atexit`).
* `xrefs.py` shows direct calls and absolute references only. The startup path is mostly indirect
  (`call [reg+off]` on interfaces); a helper that lists vtable-slot calls per function with the
  receiver's likely interface would have saved a lot here. `db/interface-slots.jsonl` was
  essential (it names `cIGZApp`, `cIGZFrameWork`, `cIGZFrameWorkHooks`, `cIGZSystemService`
  slots); more interfaces in it (`cIGZMessageServer2`, `cIGZMessageTarget2`) would help the next map.
* `classinfo.py` prints names for the first vtable only for classes that use multiple inheritance;
  `cIGZSystemService`'s Init/OnTick sit in the subobject at the base's offset (+4 or +12), which is
  easy to misread. A `--iface cIGZSystemService` flag that picks the right subobject would help.
* `library-matches` names `exit` (0x82f6bf) as `_loaddll`; `set_terminate` (0x8444d7) and
  `_set_purecall_handler` (0x8261d6) are unnamed. These are CRT, so they belong in
  `crt-equivalents` with the right names.
