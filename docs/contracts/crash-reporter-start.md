# Contract: crash reporter start-up (0xc2b206)

**Contract: approved by the user (2026-10-11).** Hooks enabled in-game. Status: `replaced`. Source: `src/app/WinMainInfra.{h,cpp}` (`winmaininfra::StartCrashReporter`).
Tests: `tests/winmain_infra_crash.cpp` (`WinMainInfra_StartCrashReporter`). Hook: `WinMainInfra_StartCrashReporter`.

## 1. Interface

`bool __cdecl StartCrashReporter(const std::string &uploadUrl, const std::wstring &handlerPath, const std::wstring &databasePath)`

- `__cdecl`, three pointer-sized stack arguments (references), the caller pops them. WinMain builds
  all three on its stack: the upload address (a narrow string), the path of `crashpad_handler.exe`
  in the program's directory, and the crash database directory (`%TEMP%\TS2Legacy`).
- Result in `al`: the original returns whether the handler process was started. WinMain ignores it.
  Ours always returns `false`. The other bits of `eax` are not defined in either.
- Size in the image: 1654 bytes (the function database records 14: it ends at the `_EH_prolog` call).

## 2. What the original does (from the disassembly)

In this order:

1. Builds a `std::map<std::string, std::string>` of crash annotations: `format=minidump`,
   `platform=steam`, and `binary_version=a.b.c.d` from the version resource of the running exe
   (`GetModuleFileNameA`, `GetFileVersionInfoSizeA`, `GetFileVersionInfoA`, `VerQueryValueA`).
2. `CoInitializeEx(nullptr, 0)` (COINIT_MULTITHREADED) on the calling thread. If the result is
   negative, skips step 3 (it still does steps 4 to 6).
3. `CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT (0),
   RPC_C_IMP_LEVEL_IMPERSONATE (3), nullptr, EOAC_NONE (0), nullptr)`. If that result is
   negative, skips the survey below. Otherwise a WMI survey: `CoCreateInstance` of the WMI
   locator, connect to `root\CIMV2`, `CoSetProxyBlanket`, a WQL query of the video controllers,
   and for each one an annotation `videoN = <caption> <driver version>`. COM is never
   uninitialised.
4. Opens the crash database at `databasePath` (a crashpad `CrashReportDatabase`: creates the
   directory and its sub-directories, takes file locks) and stores it in the global at RVA
   0x10fdb3c. Then asks its settings object to enable uploads (writes crashpad's settings file in
   that directory; identified by the `CPds` magic and the settings-file operations, an inference
   from the code, not from names).
5. `CrashpadClient::StartHandler` (0xc52180): starts `handlerPath` as a child process
   (`CreateProcessW`) with the upload address, the database and the annotations, creates a named
   pipe, an event and a thread, and installs its own top-level exception filter
   (`SetUnhandledExceptionFilter`). The handler writes minidumps and uploads them.
6. Frees everything local. The global in step 4 stays set; its only other reader is the
   `atexit` deleter 0xd98904, which tests it for null.

External effects, complete: files and directories under the database path, file locks, one child
process, a named pipe, an event, a thread, the process's top-level exception filter, COM
initialisation (thread and process), WMI and DCOM traffic, loads of the version/WMI DLLs, and later
network uploads of crash dumps by the child.

## 3. What the rest of the game reads back

Searched with `tools/xrefs.py` over the whole image:

- The global at 0x10fdb3c: written only by this function; read only by this function and by the
  `atexit` deleter 0xd98904 (null-safe). Nothing else.
- The strings and constants used here are referenced by no other function.
- The top-level exception filter: the application constructor (0xc26147) installs the in-process
  exception reporter's filter (0x1bd687) later, replacing whatever was there, when its
  `ExceptionHandling` switch is not `off`/`disabled`. The reporter remembers the previous filter
  (`+0x1a8`), puts it back when it reports, and calls it only when a debugger is attached; it
  checks it for null. Nothing needs crashpad's filter to be there.
- The COM apartment of the main thread (section 4, kept effect 1) and the process's COM security
  (kept effect 2): COM clients later in start-up (the application constructor's `CoInitialize`, the DirectShow video decoder's `CoInitialize` at 0xc5eebe), and any mod or middleware that uses COM, see these.
- Gameplay, saves and mods: nothing in this function touches them.

## 4. Contract

### Kept (preserved exactly)

1. `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` on the calling thread, first.
2. If that result is non-negative (S_OK or S_FALSE): `CoInitializeSecurity(nullptr, -1, nullptr,
   nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr)`.
   Its result is ignored.
3. Neither is undone: no `CoUninitialize`.

### Dropped (deliberately), each with its reason

| Dropped | Reason |
|---|---|
| The upload of crash dumps to a third-party service and everything that serves it: the child process, its named pipe, event and thread, the settings entry that turns uploads on | The project must never upload crash data (CLAUDE.md). Also unavailable in the local game copy, which has the handler disabled. |
| The crash database directory under `%TEMP%\TS2Legacy` and its settings file | Existed only for the handler and for the upload queue. Nothing reads it back. |
| Crashpad's top-level exception filter | Existed only to feed the handler. |
| The WMI hardware survey (video controller names and driver versions) | Existed only to be attached to the upload. Collecting hardware details to send away is the kind of thing the project does not do; the COM and DLL loading it needs is removed with it. |
| The exe version and the `format`/`platform` annotations | Only annotations. |
| The write to 0x10fdb3c | Only read by a null-safe deleter. It stays null. |

### Local crash handling that remains

Nothing is added; what the game already has stays and is unaffected:

- the in-process exception reporter installed by the application constructor (local minidump and
  text report, no upload: documented in `docs/startup-map.md`), which `RunApp` (0xc2d23b) also calls
  from its SEH filter;
- the terminate and purecall handlers (`terminate-handlers.md`), which route into that reporter.

## 5. Judgement calls (approved 2026-10-11)

1. **COM initialisation is kept, although only the WMI survey needed it.** The original initialises
   the main thread as multithreaded (`COINIT_MULTITHREADED`). The application constructor later calls
   `CoInitialize`, which asks for a single-threaded apartment; on this thread that call gets
   `RPC_E_CHANGED_MODE`. So today the main thread is an MTA only because of this function. Dropping it
   would silently turn the main thread into an STA and could change how any later COM client (DirectShow,
   shell dialogs, the Steam overlay, mods) behaves. Same for the process-wide security setting, which
   can be set only once. Keeping both costs nothing and changes nothing. If the user would rather not
   carry this accident, the change is two lines plus an STA decision, to be tested in game.
2. **No handler means no crashpad filter when `ExceptionHandling` is off.** If the user passes
   `-ExceptionHandling:off`, the in-process reporter installs no filter, and in the original crashpad's
   was the active one (when its handler started, which it does not in the local copy, since the exe is
   disabled there). In ours there is then no filter and Windows' default applies. That affects crashes
   in a mode used for debugging, not play.
3. **The identity of the settings call (step 4) is inferred**, from the settings-file magic and the
   file operations in 0xc51420, not from symbols. It is dropped either way.
4. **The upload address and the handler's authentication token are not recorded** anywhere in this
   repository. The contract and the code ignore the first argument.
5. The three arguments are still built by WinMain (original or ours). They cost a few small
   allocations and read the environment (`GetModuleFileNameW`, `GetTempPathW`); WinMain's own
   reimplementation may stop building them once nothing consumes them (not decided here).

## 6. Tests

`WinMainInfra_StartCrashReporter` (differential on the kept effects only). The original runs with the
COM, version-resource and `CoCreateInstance` entry points faked in the import table; ours reads the
same fakes (`winmaininfra::OsTable()`). The original is stopped at its first dropped effect by a
fake that throws (the WMI locator, or the crash database's open, redirected in the image).
For all 64 combinations of eight results of `CoInitializeEx` and of `CoInitializeSecurity`
(S_OK, S_FALSE, large positive, RPC_E_CHANGED_MODE, E_FAIL, the extremes) it checks:

- the original's calls to the kept entry points (names, order, every argument) equal ours exactly;
- the original's dropped effects are exactly the expected list (so a drop cannot turn into a
  change unnoticed), and ours has none;
- ours returns `false`.

Mutation check (tools/mutate.py): 9 mutants of the function, all killed (apartment flag, success test `>= 0` `> 0`, security skipped, security forced, impersonation level, authentication level, authority count, result value, a second `CoInitializeEx`).
