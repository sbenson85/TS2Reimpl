# First-call coverage of the original code

How the in-game coverage measurement works and how to run it. Usage of the tool: the docstring of `tools/coverage.py`
(`headline`, `queue`, `deps`, `report`); what the numbers drive: `docs/roadmap.md` (Metrics 1, Campaign order). First
written in lane HY's milestone proposal (`docs/archive/proposals/next-milestones.md`, section 2).

## Mechanism

`src/hook/coverage.h` (mechanism, header-only, tested in `tests/hook_coverage.cpp`), `src/hook/coverage.cpp` (host side),
`tools/coverage.py` (site list and reports), `tools/scenarios/coverage-phases.txt` (the run).

- **One-shot breakpoints on function starts.** The host writes `int3` over the first byte of every listed original
  function, just before the image's entry point runs (after every hook, trap and control patch is in). The first call
  of a function raises one breakpoint exception; a vectored handler (first in line) records the function, the return
  address on the stack, the time and the thread, writes the original byte back and resumes at the same address. Every
  later call runs at full speed. Cost: one exception per function that runs (a few microseconds each; some tens of
  thousands in a run, well under a second in all), nothing per call after that.
- **Rounds.** The host control command `coverage <label>` (`ctl:atframe N coverage <label>` in a scenario) puts the
  breakpoint back on every function hit so far and starts a new round, so each phase of a run (start to menu, a steady
  menu frame, the neighbourhood load, a steady neighbourhood frame, the lot load, live mode) gets its own first-call set.
- **What is listed.** `tools/coverage.py sites` writes every function start of `tsdb.functions()` (Ghidra's functions,
  the static initialisers, the missed functions) that something references: a direct call or tail jump, or a relocated
  dword equal to the start (vtables, callback tables, pushed callbacks), plus the entry point and 12 starts db/status
  knows that Ghidra's list lacks (GZFrameWork_Shutdown 0x14472b and others status.py --check notes). 66,167 sites; 953
  unreferenced starts are left out (dead or misidentified: an `int3` written mid-instruction would corrupt code), and so
  are switch tables Ghidra made into functions (an indexed-table operand, `jmp [eax*4+table]` or `movzx r, byte
  [ecx+table]`, names them; four, 2026-11-03: armed, their int3 byte changed a switch target and crashed lot 10's load).
  Each
  site carries the byte the unpacked exe has there; the host arms only where the mapped image still holds it.
- **What is left alone.** A site starting with our own `jmp` out of the image (a hook, a trap, the frame counter's
  detour) is "ours": our code runs there, nothing to measure. Any other byte difference is "mismatched" and logged.
  So on a replacement run the log shows the original code that still runs; on a reference run (`-Reference`), all of
  it.
- **Output.** `ts2coverage.txt` next to the host, flushed every 250 ms and at each round (format in
  `src/hook/coverage.cpp`): `F <rva> <from> <ms> <thread>` per first call, `R` per round, `O`/`M` for sites left
  alone. `<from>` is `i<rva>` for a return address in the image, `x<address>` outside it (our code calling an original
  through `OriginalFunction`, or a system DLL calling back).
- **Switch.** `TS2HOST_COVERAGE=<site list>` (or `=1` for `ts2coverage-sites.txt` next to the host); `game.ps1` and
  `scenario.ps1` take `-Coverage <site list>`. Unset, `StartCoverage` returns at its first line: no handler, no
  writes, no thread.
- **Why not the alternatives.** Page guards (no-execute code pages, re-protected after each step) see only pages, or
  cost a fault per instruction to see functions; sampling misses short and rare functions and gives no first-call order
  or caller; a tracer (record/replay's) is far too slow for a whole run. Single-byte breakpoints give exact functions,
  order, caller and phase, at a negligible cost.
- **Limits.** Inlined code is invisible (it belongs to its caller); a jump stub counts as its own function; a tail
  jump's target records the caller's caller as `<from>` (the report calls it "tail"); a thread that runs image code
  before arming (none do: arming is before the entry point) would be missed; a site Ghidra placed mid-instruction would
  corrupt code, which the referenced-starts filter is there to prevent, and which the run's own checks (both marks
  reached, no new fault-log lines) would show.

## The run (coordinator)

1. `tools/py -I tools/coverage.py sites` (writes `local/coverage/sites.txt`; again after db/ changes that add functions).
2. Reference: `scenario.ps1 @coverage-phases -ResetUserData -Quiet -Reference -Coverage <main>\local\coverage\sites.txt`,
   then keep `local\game\TSBin\ts2coverage.txt` as `local/coverage/ref-1.txt`. A second reference run (`ref-2.txt`)
   shows how much the sets vary between runs (`coverage.py diff ref-1.txt ref-2.txt --round N`).
3. Replacements on: the same without `-Reference` (`ours-1.txt`). `-Instance N` works too (the file is then in
   `local\game-N\TSBin`).
4. `tools/py -I tools/coverage.py report ref-1.txt` (per round: functions and bytes that ran, by status, library and
   third-party, how much is outside H's direct-call set, not-done bytes by subsystem and by class, originals reached
   from our code); `--todo` for the not-done list of a round; `--json` for everything per function;
   `diff ours-1.txt ref-1.txt` for what our replacements stopped (or started) running.
5. Check the instrument changed nothing: both scenario marks are reached, the host log has no fault lines the
   load-check run doesn't have, and the replacement run's hook call counts look like a load-check run's.

The numbers in section 3 marked "(run)" come from step 4 and should be filled in before the milestones go to the user.

## The reference set (lanes)

`coverage.py queue`, `deps` and `headline` given no runs read the reference set from the main checkout's
`local/coverage/` (from a worktree too) as one session, the union of what ran: `run-1023.txt` (the phases run,
2026-10-23) and `lot6-ref.txt`, `lot10-ref.txt`, `lot50-ref.txt`, `lot50s-ref.txt`, `lot106-ref.txt`, `lot127-ref.txt`
(2026-11-03: the same six rounds, start to lot-live, each on a different occupied lot's live play). All seven have the
whole round sequence (0 start, 1 menu-idle, 2 hood-load, 3 hood-idle, 4 lot-load, 5 lot-live); the lot runs are
`-Reference` runs (every site armed but one), run-1023 left our 8,452 sites unarmed, which the lot runs measure. A
site counts as "not measured" only when every run in the session left it alone. The list is `DEFAULT_RUNS` in the
tool; it prints the set it read on stderr. Runs named on the command line are read instead, and a name that does not
exist where you are is looked up in the main checkout and its `local/coverage/` (`coverage.py queue run-1023.txt`
works from a worktree). The set on 2026-11-04: 5,206 KB of EA code ran, 3,419 KB of it not done (run-1023 alone:
3,227 KB and 2,548 KB).

**Measured 2026-10-19** (commit fa969fe, `local/coverage/run1-3.txt`; two reference runs, one replacement run, quiet,
instances 1-3). The two reference runs recorded the same first-call set (`coverage.py diff`: 0 functions either way);
the replacement run reached no original function the reference runs didn't, and 3,590 functions (411 KB) fewer. Host
logs: every site armed (reference 66,166, replacement 60,670 plus 5,497 ours), 0 mismatched, both marks reached, no
fault lines beyond a smoke run's. Per round, original code that ran in the reference run (EA code not done / all bytes):

| Round | Functions | Bytes ran | EA not done | Third-party | Largest not-done subsystems |
|---|---|---|---|---|---|
| 0 start (entry to frame 900, the menu up) | 13,804 | 2,240 KB | 1,606 KB | 146 KB | framework-gz 345 KB, rendering-gfx 281, scenegraph 143, command-parser 132, ui-game 107, ui-windowing 63 |
| 1 menu-idle (frames 900-950) | 913 | 151 KB | 122 KB | 3 KB | rendering-gfx 54, audio-video 20, ui-windowing 14, ui-game 9, framework-gz 6 |
| 2 hood-load | 10,283 | 2,114 KB | 1,618 KB | 68 KB | scenegraph 307, rendering-gfx 278, framework-gz 277, ui-game 106, audio-video 99 |
| 3 hood-idle | 2,536 | 494 KB | 422 KB | 15 KB | rendering-gfx 123, scenegraph 100, audio-video 77, framework-gz 35, other-ts 26 |
| 4 lot-load | 15,509 | 3,472 KB | 2,711 KB | 250 KB | |
| 5 lot-live | 2,074 | 448 KB | 385 KB | 5 KB | |

Already ours (verified) in round 0: 2,481 functions, 315 KB; library code 171 KB. In the replacement run round 0 the
EA not-done part is 1,568 KB (the rest is ours). Sizes for section 3: **J** (menu-idle + hood-idle minus K's drawing
families and the scene graph): about 120-150 KB (framework-gz 41, audio-video 97 of which the per-frame mixer is most,
other-ts 26, shared-utility 15, ui-game logic); **K** (rounds 0-1 in ui-windowing, ui-game and the 2D/draw part of
rendering-gfx): ui-windowing 77 KB + ui-game 116 KB + rendering-gfx up to 335 KB (device set-up included);
**M** (rounds 0-1, everything): about 1.73 MB EA code + 149 KB third-party + 181 KB library (toolchain's) — inside the
1-2.5 MB estimate. Note command-parser (132 KB in round 0: the start-up's script/cheat parser runs) belongs to M.

