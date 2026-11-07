# Contract: terminate and purecall handlers (0xcac1f0 installer, 0xcac170 handler)

**Contract: approved by the user (2026-10-11).** Hooks enabled in-game. Status: `replaced` (both). Source: `src/app/WinMainInfra.{h,cpp}` (`winmaininfra::InstallTerminateAndPurecallHandlers`,
`winmaininfra::ReportAndReturn`, `winmaininfra::InstallHandlers`, the two `CrtSeam`s).
Tests: `tests/winmain_infra_handlers.cpp` (`WinMainInfra_ReportAndReturn`, `WinMainInfra_InstallHandlers`,
`WinMainInfra_InstallHandlersSeams`). Hook: `WinMainInfra_InstallHandlers` (0xcac1f0). The handler 0xcac170 is not
hooked: only the installer refers to it, and ours installs our own handler instead.

## 1. Interface

- Installer, 0xcac1f0: `void __cdecl()`, 24 bytes, no arguments, no result. Called once by WinMain on the
  main thread, after the analytics call and before `DisableImeTextFrameService`.
- Handler, 0xcac170: `void __cdecl()`, no arguments, no result. The game CRT calls it with no arguments:
  as the C++ `terminate` handler (an exception nobody caught, an exception from a destructor during
  unwinding, `std::terminate()`), and as the pure-virtual-call handler (`_purecall`).

## 2. What the original does

Installer: `set_terminate(handler)` (0x8444d7), then `_set_purecall_handler(handler)` (0x8261d6),
both with the handler 0xcac170. Both are the game CRT's own functions:

- `set_terminate` stores the pointer in the `_terminate` field (+0xc) of the *calling thread's* CRT
  per-thread data. It is a per-thread setting: only the main thread gets the handler. (The data is
  found through the FLS slot whose index is in the CRT global at RVA 0x104f89c; the CRT's lookup also
  preserves the thread's last-error value, and creates the data if the thread has none.)
- `_set_purecall_handler` exchanges the process-wide pointer at RVA 0x10dfb80.

Handler: reports an unhandled error through the application's exception reporter by making a crash on
purpose.

1. `old = SetUnhandledExceptionFilter(nullptr)`. If `old` is not null, `SetUnhandledExceptionFilter(old)`
   puts it back; otherwise `old` becomes the system's `UnhandledExceptionFilter`.
2. `__try { read the byte at address 0; write it back to address 0 } __except (old(GetExceptionInformation()))
   { }` with no body in the handler. The read faults (access violation, code 0xC0000005, two
   parameters: 0 = read, address 0). The exception filter expression calls `old` with that exception.
3. If `old` answers `EXCEPTION_EXECUTE_HANDLER` (1), the empty handler runs and the function returns
   normally; the CRT then goes on to `abort()`. If it answers `EXCEPTION_CONTINUE_SEARCH` (0) the
   access violation continues to outer handlers (and then to the process's default). The in-game
   filter at that point is the exception reporter's stub (0x1bd687), which writes the local minidump
   and text report, and returns 1 or the previous filter's answer (`docs/startup-map.md` section 3).

External effects: whatever the filter does (the reporter's report, files), then the CRT's abort.

## 3. What the rest of the game reads back

- `_terminate` of the main thread's per-thread data and the purecall variable are read only by the
  game CRT (`terminate()`, `_purecall`). Nothing in the game's own code reads them (xrefs: 0x10dfb80 is
  referenced only inside the CRT: the setter 0x8261d6, the getter 0x8261b3 that `_purecall` calls, and one reference in the CRT's EH-registration validator 0x829c90).
- The handler is referenced only by the installer.

## 4. Contract

### Kept (preserved exactly)

1. The handler routes an unhandled C++ exception, a direct `std::terminate()`, and a pure virtual call
   into the *application's top-level exception filter* with a genuine access violation at address 0
   (read, address 0, 2 parameters), in the same order of operations: take the current filter with
   `SetUnhandledExceptionFilter(nullptr)`, put it back if there was one, else use the system's
   `UnhandledExceptionFilter`; call it for the new exception; return when it answers "handle",
   let the exception go on when it answers "continue search".
2. Installation order and place: terminate handler first, then purecall handler; the terminate handler
   in the main thread's per-thread data (so it covers the main thread only, as before) and the purecall
   handler process-wide. Nothing else of the per-thread data changes. The thread's last-error value is
   preserved across the lookup.

### Dropped or changed, with reasons

| Change | Reason |
|---|---|
| Our handler (`winmaininfra::ReportAndReturn`) instead of the image's code at 0xcac170 | The point of the milestone: no image code. Same behaviour (section 5). Compiled with `__try`/`__except` in the current MSVC (SEH4 frame; the original is SEH3): not observable here. |
| A thread whose per-thread data does not exist yet, or holds -1, keeps its default terminate handler | The original would create the data first (calloc, fill from the CRT's initial state). Doing that from outside the CRT would be a copy of CRT internals. WinMain's thread always has its data (the CRT start-up used it), so nothing differs in the game; the purecall handler is still installed. An FLS index of "out of indexes" (-1) is not looked up. |
| In the standalone build the installation uses this program's own CRT (`std::set_terminate`, `_set_purecall_handler`) | There is no game CRT then. The `CrtSeam` selects: `kImageCrt` for the hybrid host (writes the game CRT's variables), `kOwnCrt` for the standalone exe. `kImageCrt` is an image dependency (two RVAs, 0x10dfb80 and 0x104f89c, and one field offset, +0xc) that goes away with the game CRT. |

### Judgement calls (approved 2026-10-11)

1. **The deliberate crash is kept.** It is odd (a null read and write instead of calling the reporter
   directly), but it is the one way the reporter receives a proper exception record and context for a
   terminate or a pure virtual call, and other filters (a debugger, a mod's filter) see the same event
   as before. A cleaner design (call the reporter with a synthesised record) would change what filters
   observe. Kept as is. The user approved it as an interim compatibility mechanism: no further
   fidelity work or redesign here for now.
2. **The hybrid seam reads the game CRT's per-thread layout** (field +0xc, FLS slot index at a fixed RVA).
   That is the only way to keep the main thread's terminate handler while the game's own CRT is still in
   the process. It is a data layout of the CRT in this one binary, not game logic.
3. If the user prefers, the thread-data case (a thread without data) could be made to behave exactly like the
   original by letting the seam call the CRT's own lookup. That calls image code from a replacement, so it
   was not done.

## 5. Tests

`WinMainInfra_ReportAndReturn` (vs 0xcac170, handler run on both sides): SetUnhandledExceptionFilter and
UnhandledExceptionFilter are faked in the image's import table and in `OsTable()`; the CRT's SEH
registration validator (0x829c90), which the harness cannot satisfy, is redirected to "valid" as in
`tests/winmain_run_app.cpp`. For "a top-level filter installed" and "none", with the filter answering
1 and 0, three repeats each: the log (filter set to null, set back, filter called with code
0xC0000005, flags, parameter count, [read/write, address]) and the outcome (returned, or which
exception reached the caller) are equal. A literal expected log is also checked, so two equal but wrong
logs cannot pass.

`WinMainInfra_InstallHandlers` (vs 0xcac1f0): the image's FLS-value wrapper (0x8480bf), GetLastError and
SetLastError are redirected to fakes on both sides; the per-thread data is a block of our own in an FLS
slot we allocate, filled with a pattern. Twelve variants of the old handler values and last error: the log
(GetLastError, FlsGetValue(index), SetLastError(previous)), whether each of the two variables now holds
"the handler" (the original's 0xcac170 in the image, ours for ours), and every other byte of the
per-thread data are equal. Ours alone: a thread without data and a -1 slot still get the purecall
handler; an out-of-range FLS index is not looked up.

`WinMainInfra_InstallHandlersSeams` (ours alone): the order terminate-then-purecall with the handler
given; the standalone seam sets this program's `std::get_terminate()` and `_get_purecall_handler()`.

Mutation check (tools/mutate.py), all killed except one equivalent: the filter put back, the null
address, read dropped, `UnhandledExceptionFilter` fallback, order swapped, per-thread field offset,
purecall RVA, last-error save and restore, the -1 and out-of-index guards, the seam wiring. The
surviving mutant swaps the two setters in `kOwnCrt`, which hands the same handler to both and is
therefore equivalent.
